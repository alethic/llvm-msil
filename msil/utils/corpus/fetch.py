#!/usr/bin/env python3
# ===-- fetch.py - Fetch and verify the Phase 2 exit corpus ---------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Fetch the exit-corpus sources pinned by manifest.json into a cache.

    fetch.py [--cache DIR] [--check] [--only N[,N...]] [--skip N[,N...]]
    fetch.py --list [--only N[,N...]] [--skip N[,N...]]
    fetch.py --self-test

The Phase 2 exit corpus (plan 5.12.9) is third-party C code that is never
committed: manifest.json, next to this script, pins each entry and this script
downloads it into a cache outside the repository, where msil/test/Programs
finds it through lit's msil_corpus_dir parameter.

manifest.json is {"version": 1, "programs": [ENTRY, ...]}; an ENTRY has
  name            the entry's name ([a-z0-9-]), used by --only and --skip
  license         the licence of the sources, as the project states it
  license_file    the licence text's path in the tree, or null when the
                  sources carry none (TweetNaCl is in the public domain)
  url             one https URL (a .tar.gz/.tgz/.tar.xz/.tar.bz2/.tar or .zip
                  archive, or a single file), or a list of https URLs of
                  single files
  ref             the version or abbreviated commit; the tree is
                  <cache>/<name>-<ref>/
  commit          git sources only: the full 40-digit commit; url is then
                  the GitHub archive of exactly that commit
  archive_sha256  release tarballs and single files: the SHA-256 of the
                  downloaded bytes (for a list of files, the SHA-256 of their
                  sorted '<sha256> <file name>' lines, as for tree_sha256)
  tree_sha256     every entry: the SHA-256 of the sorted lines
                  '<sha256 of the file> <relative path>' (one per regular
                  file of the extracted tree, '/' separators, each line ended
                  by a newline, UTF-8), which pins GitHub archives, whose
                  bytes are not stable
  strip           leading path components removed from archive members
  files           optional: restrict the tree to these paths (a trailing '/'
                  keeps a whole directory)

An entry is fetched only when its tree is missing or does not match
tree_sha256. The download goes to a temporary file in the cache;
archive_sha256, when the entry has one, is checked before anything is
extracted. Downloads are untrusted: only https URLs (redirects included) are
fetched; every member name is checked first (absolute names, '..', names that
Windows cannot hold, case-insensitive duplicates and special files are
refused, as are links that leave the tree), regular files are extracted with
tarfile's 'data' filter, and a link inside the tree becomes a copy of its
target, so a tree is the same on every host. A tree is moved into place only
when it matches tree_sha256. fetch.py deletes nothing but its own
<name>-<ref> trees and temporary files (.msil-corpus-tmp-*), and refuses a
cache inside the repository (plan 5.6.1: nothing downloaded is committed).

--check verifies the trees without any network access. --only and --skip
select entries (a run with a selection downloads, verifies and stamps only
those; an empty --skip value skips nothing, so CI can pass
--skip "$MSIL_CORPUS_SKIP" whatever the variable holds). After every selected
entry verified, the stamp <cache>/msil-corpus.stamp is written:
  {"manifest_sha256": "<sha256 of manifest.json's bytes>",
   "trees": {"<name>": "<tree_sha256>", ...}}
listing exactly the selected entries; lit enables 'msil-corpus' only when the
stamp matches the checked-in manifest, and 'msil-corpus-<name>' only for the
trees it lists. A run that fails removes the stamp. --list prints the entries
and touches no cache. --self-test runs offline in a temporary directory.

The cache is --cache DIR, else $MSIL_CORPUS_CACHE, else
C:\\llvm-msil-scratch\\corpus-cache on Windows and $HOME/.cache/msil-corpus
elsewhere.

Exit status: 0 on success, 1 when a check, a hash or a download failed (the
manifest included), 2 on a usage error (an unknown entry name and a cache
inside the repository included). Python 3 standard library only.
"""

import argparse
import contextlib
import copy
import hashlib
import http.client
import io
import json
import os
import posixpath
import re
import shutil
import stat
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
MANIFEST = os.path.join(HERE, "manifest.json")
IS_WINDOWS = os.name == "nt"
DEFAULT_WINDOWS_CACHE = r"C:\llvm-msil-scratch\corpus-cache"
DEFAULT_POSIX_CACHE = os.path.join("~", ".cache", "msil-corpus")
STAMP = "msil-corpus.stamp"
LOCK = "msil-corpus.lock"
TMP_PREFIX = ".msil-corpus-tmp-"

# Limits for untrusted downloads (the largest entry, zlib, is 1.6 MB packed and
# 3.6 MB unpacked).
MAX_DOWNLOAD_BYTES = 64 << 20
MAX_TREE_BYTES = 256 << 20
MAX_MEMBERS = 20000
MAX_LINK_HOPS = 40
TIMEOUT_SECONDS = 60
ATTEMPTS = 3
LOCK_WAIT_SECONDS = 600
CHUNK = 1 << 20

HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
NAME_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
REF_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
GITHUB_ARCHIVE_RE = re.compile(
    r"^https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/archive/"
    r"([0-9a-f]{40})\.(?:tar\.gz|zip)$"
)
# Printable ASCII only, without the characters Windows refuses in a name.
NAME_PART_BAD = re.compile(r'[^\x20-\x7e]|[<>:"|?*\\]')
WINDOWS_RESERVED = re.compile(r"^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\..*)?$", re.I)
TAR_SUFFIXES = (".tar.gz", ".tgz", ".tar.bz2", ".tbz2", ".tar.xz", ".txz", ".tar")
REQUIRED_KEYS = (
    "name",
    "license",
    "license_file",
    "url",
    "ref",
    "tree_sha256",
    "strip",
)
OPTIONAL_KEYS = ("commit", "archive_sha256", "files")

# tarfile's extraction filters (Python 3.12, and the 3.8.17/3.9.17/3.10.12/3.11.4
# security releases). Without them the regular files are written by this
# script from the same validated member list. The self-test runs both paths.
HAVE_TAR_FILTER = hasattr(tarfile, "data_filter")
_force_manual_tar = False


class Error(Exception):
    """A check, hash or download failed (exit status 1)."""


class UsageError(Exception):
    """A usage error (exit status 2)."""


def say(text):
    print(text, flush=True)


def complain(text):
    sys.stderr.write("fetch.py: error: %s\n" % text)
    sys.stderr.flush()


# --------------------------------------------------------------------- hashes


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(CHUNK), b""):
            h.update(block)
    return h.hexdigest()


def lines_sha256(lines):
    """The tree hash construction: SHA-256 of the sorted lines, each ended by
    a newline, encoded as UTF-8."""
    data = "".join(line + "\n" for line in sorted(lines)).encode("utf-8")
    return hashlib.sha256(data).hexdigest()


def is_link(entry):
    """True for a symbolic link or a Windows junction (any reparse point)."""
    if entry.is_symlink():
        return True
    if IS_WINDOWS:
        attrs = getattr(entry.stat(follow_symlinks=False), "st_file_attributes", 0)
        return bool(attrs & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400))
    return False


def tree_lines(root):
    """'<sha256> <relative path>' for every regular file under root; a link
    or a special file is an error (fetch.py never creates either)."""
    lines = []

    def walk(directory, prefix):
        with os.scandir(directory) as it:
            for entry in it:
                rel = prefix + entry.name
                if is_link(entry):
                    raise Error("unexpected link %s in the tree" % rel)
                if entry.is_dir(follow_symlinks=False):
                    walk(entry.path, rel + "/")
                elif entry.is_file(follow_symlinks=False):
                    lines.append("%s %s" % (sha256_file(entry.path), rel))
                else:
                    raise Error("unexpected special file %s in the tree" % rel)

    walk(root, "")
    return lines


def plain_directory(path):
    """True when path is a directory that is not itself a link or junction."""
    if not os.path.isdir(path) or os.path.islink(path):
        return False
    if IS_WINDOWS:
        attrs = getattr(os.lstat(path), "st_file_attributes", 0)
        if attrs & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400):
            return False
    return True


def tree_state(path, expected):
    """(True, None) when the tree at path matches expected, else (False,
    reason)."""
    if not os.path.lexists(path):
        return False, "missing"
    if not plain_directory(path):
        return False, "not a plain directory"
    try:
        actual = lines_sha256(tree_lines(path))
    except (Error, OSError) as e:
        return False, str(e)
    if actual != expected:
        return False, "tree_sha256 mismatch: manifest %s, tree %s" % (expected, actual)
    return True, None


# ------------------------------------------------------------------- manifest


class Entry(object):
    def __init__(self, raw):
        self.name = raw["name"]
        self.ref = raw["ref"]
        self.license = raw["license"]
        self.license_file = raw["license_file"]
        url = raw["url"]
        self.urls = list(url) if isinstance(url, list) else [url]
        self.commit = raw.get("commit")
        self.archive_sha256 = raw.get("archive_sha256")
        self.tree_sha256 = raw["tree_sha256"]
        self.strip = raw["strip"]
        self.files = raw.get("files")
        if isinstance(url, list):
            self.kind = "files"
        else:
            path = urllib.parse.urlsplit(url).path.lower()
            if path.endswith(TAR_SUFFIXES):
                self.kind = "tar"
            elif path.endswith(".zip"):
                self.kind = "zip"
            else:
                self.kind = "files"
        self.dirname = "%s-%s" % (self.name, self.ref)


def check_url(url, what, allow_file_urls):
    scheme = urllib.parse.urlsplit(url).scheme.lower()
    if scheme == "https" or (scheme == "file" and allow_file_urls):
        return
    raise Error("%s: refusing %r: only https URLs are fetched" % (what, url))


def url_file_name(url):
    name = urllib.parse.unquote(urllib.parse.urlsplit(url).path.rstrip("/"))
    return name.rsplit("/", 1)[-1]


def validate_entry(raw, index, allow_file_urls):
    what = "manifest.json: programs[%d]" % index
    if not isinstance(raw, dict):
        raise Error("%s is not an object" % what)
    if isinstance(raw.get("name"), str):
        what = "manifest.json: entry %r" % raw["name"]
    for key in REQUIRED_KEYS:
        if key not in raw:
            raise Error("%s lacks %r" % (what, key))
    unknown = sorted(set(raw) - set(REQUIRED_KEYS) - set(OPTIONAL_KEYS))
    if unknown:
        raise Error("%s has unknown key(s) %s" % (what, ", ".join(unknown)))
    if not isinstance(raw["name"], str) or not NAME_RE.match(raw["name"]):
        raise Error("%s: name must match %s" % (what, NAME_RE.pattern))
    if not isinstance(raw["ref"], str) or not REF_RE.match(raw["ref"]):
        raise Error("%s: ref must match %s" % (what, REF_RE.pattern))
    if not isinstance(raw["license"], str) or not raw["license"].strip():
        raise Error("%s: license must be a non-empty string" % what)
    lf = raw["license_file"]
    if lf is not None:
        if not isinstance(lf, str):
            raise Error("%s: license_file must be a path or null" % what)
        check_relpath(lf, what + " license_file")
    if (
        not isinstance(raw["strip"], int)
        or isinstance(raw["strip"], bool)
        or raw["strip"] < 0
    ):
        raise Error("%s: strip must be a non-negative integer" % what)
    if not isinstance(raw["tree_sha256"], str) or not HEX64.match(raw["tree_sha256"]):
        raise Error("%s: tree_sha256 must be 64 lowercase hex digits" % what)
    commit = raw.get("commit")
    if commit is not None and (not isinstance(commit, str) or not HEX40.match(commit)):
        raise Error("%s: commit must be 40 lowercase hex digits" % what)
    archive = raw.get("archive_sha256")
    if archive is not None and (
        not isinstance(archive, str) or not HEX64.match(archive)
    ):
        raise Error("%s: archive_sha256 must be 64 lowercase hex digits" % what)
    if commit is None and archive is None:
        raise Error("%s needs a commit or an archive_sha256" % what)
    url = raw["url"]
    urls = url if isinstance(url, list) else [url]
    if not urls or not all(isinstance(u, str) and u for u in urls):
        raise Error("%s: url must be a URL or a non-empty list of URLs" % what)
    for u in urls:
        check_url(u, what, allow_file_urls)
    files = raw.get("files")
    if files is not None:
        if (
            not isinstance(files, list)
            or not files
            or not all(isinstance(f, str) for f in files)
        ):
            raise Error("%s: files must be a non-empty list of paths" % what)
    entry = Entry(raw)
    if files is not None:
        entry.files = [
            check_relpath(f, what + " files") + ("/" if f.endswith("/") else "")
            for f in files
        ]
    # A git source is the GitHub archive of exactly its commit, and a GitHub
    # archive is pinned by its commit.
    for u in urls:
        parts = urllib.parse.urlsplit(u)
        github = parts.netloc.lower() == "github.com" and "/archive/" in parts.path
        if not github and not (commit and parts.scheme.lower() == "https"):
            continue
        m = GITHUB_ARCHIVE_RE.match(u)
        if not m:
            raise Error("%s: %s is not a GitHub archive of a full commit" % (what, u))
        if m.group(1) != commit:
            raise Error(
                "%s: url names commit %s, the entry pins %s"
                % (what, m.group(1), commit)
            )
    if entry.kind == "files":
        if entry.strip != 0:
            raise Error("%s: strip must be 0 for single files" % what)
        if entry.files is not None:
            raise Error("%s: files applies only to archives" % what)
        names = [url_file_name(u) for u in urls]
        for n in names:
            if "/" in check_relpath(n, what + " url"):
                raise Error("%s: bad file name %r" % (what, n))
        if len(set(n.lower() for n in names)) != len(names):
            raise Error("%s: two URLs give the same file name" % what)
    elif len(urls) != 1:
        raise Error("%s: an archive entry has exactly one url" % what)
    return entry


def load_manifest(path, allow_file_urls=False):
    """(manifest bytes, [Entry]) or Error."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        raise Error("cannot read %s: %s" % (path, e))
    try:
        doc = json.loads(data.decode("utf-8"))
    except ValueError as e:
        raise Error("%s is not valid JSON: %s" % (path, e))
    if not isinstance(doc, dict) or set(doc) != {"version", "programs"}:
        raise Error('manifest.json must be {"version": 1, "programs": [...]}')
    if doc["version"] != 1:
        raise Error("manifest.json: unsupported version %r" % doc["version"])
    if not isinstance(doc["programs"], list) or not doc["programs"]:
        raise Error("manifest.json: programs must be a non-empty list")
    entries = [
        validate_entry(raw, i, allow_file_urls) for i, raw in enumerate(doc["programs"])
    ]
    seen = set()
    for e in entries:
        if e.name in seen:
            raise Error("manifest.json: entry %r appears twice" % e.name)
        seen.add(e.name)
    return data, entries


# ------------------------------------------------------------------ downloads


class HttpsOnlyRedirectHandler(urllib.request.HTTPRedirectHandler):
    """Follows a redirect only to another https URL."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if urllib.parse.urlsplit(newurl).scheme.lower() != "https":
            raise Error(
                "refusing a redirect from %s to %s: only https URLs are fetched"
                % (req.full_url, newurl)
            )
        return urllib.request.HTTPRedirectHandler.redirect_request(
            self, req, fp, code, msg, headers, newurl
        )


def open_url(url, allow_file_urls):
    check_url(url, "download", allow_file_urls)
    if urllib.parse.urlsplit(url).scheme.lower() == "file":
        return urllib.request.urlopen(url, timeout=TIMEOUT_SECONDS)
    opener = urllib.request.build_opener(HttpsOnlyRedirectHandler())
    req = urllib.request.Request(url, headers={"User-Agent": "msil-corpus-fetch/1"})
    return opener.open(req, timeout=TIMEOUT_SECONDS)


def download(url, path, allow_file_urls):
    """Write url's bytes to path (a new file); return (size, sha256)."""
    delay = 2
    for attempt in range(1, ATTEMPTS + 1):
        try:
            h = hashlib.sha256()
            size = 0
            with open_url(url, allow_file_urls) as resp, open(path, "wb") as out:
                while True:
                    block = resp.read(CHUNK)
                    if not block:
                        break
                    size += len(block)
                    if size > MAX_DOWNLOAD_BYTES:
                        raise Error(
                            "%s is larger than %d bytes" % (url, MAX_DOWNLOAD_BYTES)
                        )
                    h.update(block)
                    out.write(block)
            return size, h.hexdigest()
        except urllib.error.HTTPError as e:
            transient = e.code in (408, 429, 500, 502, 503, 504)
            reason = "HTTP %d %s" % (e.code, e.reason)
        except (http.client.HTTPException, OSError) as e:
            # URLError, timeouts, resets and TLS errors.
            transient = urllib.parse.urlsplit(url).scheme.lower() != "file"
            reason = str(getattr(e, "reason", e)) or type(e).__name__
        if not transient or attempt == ATTEMPTS:
            raise Error("cannot download %s: %s" % (url, reason))
        time.sleep(delay)
        delay *= 2
    raise Error("cannot download %s" % url)  # not reached


# ----------------------------------------------------------------- extraction


def check_relpath(path, what):
    """Return the '/'-separated relative path, normalized, or raise Error when
    it is absolute, climbs with '..' or names something Windows cannot hold."""
    if not path or path.startswith("/") or re.match(r"^[A-Za-z]:", path):
        raise Error("%s: absolute name %r" % (what, path))
    parts = [p for p in path.split("/") if p not in ("", ".")]
    if not parts:
        raise Error("%s: empty name %r" % (what, path))
    for p in parts:
        if p == "..":
            raise Error("%s: name %r climbs out of the tree" % (what, path))
        if NAME_PART_BAD.search(p) or p[-1] in ". " or WINDOWS_RESERVED.match(p):
            raise Error(
                "%s: name %r is not portable (Windows cannot hold it)" % (what, path)
            )
    return "/".join(parts)


def strip_name(name, strip, what):
    """The member's path in the tree, or None when strip removes all of it."""
    rel = check_relpath(name, what)
    parts = rel.split("/")
    if len(parts) <= strip:
        return None
    return "/".join(parts[strip:])


def kept(rel, files):
    if files is None:
        return True
    for f in files:
        if f.endswith("/"):
            if rel.startswith(f):
                return True
        elif rel == f:
            return True
    return False


def resolve_symlink(rel, target, what):
    """The tree path a symbolic link at rel points to, or Error when the link
    leaves the tree."""
    if target.startswith("/") or re.match(r"^[A-Za-z]:", target) or "\\" in target:
        raise Error("%s: link %s -> %s leaves the tree" % (what, rel, target))
    joined = posixpath.normpath(posixpath.join(posixpath.dirname(rel), target))
    if joined in (".", "..") or joined.startswith("../"):
        raise Error("%s: link %s -> %s leaves the tree" % (what, rel, target))
    return joined


def check_layout(rels, what):
    """Refuse names that would collide on a case-insensitive file system, and
    a file that is also a directory of another name."""
    seen = {}
    for rel in rels:
        key = rel.lower()
        if key in seen:
            raise Error("%s: %s and %s differ only in case" % (what, seen[key], rel))
        seen[key] = rel
    for rel in rels:
        parts = rel.lower().split("/")
        for i in range(1, len(parts)):
            prefix = "/".join(parts[:i])
            if prefix in seen:
                raise Error("%s: %s is a file and a directory" % (what, seen[prefix]))


def write_stream(src, root, rel, budget):
    """Copy the stream src to root/rel (a new file); return the bytes left in
    budget."""
    path = os.path.join(root, *rel.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "xb") as out:
        while True:
            block = src.read(CHUNK)
            if not block:
                break
            budget -= len(block)
            if budget < 0:
                raise Error("the tree is larger than %d bytes" % MAX_TREE_BYTES)
            out.write(block)
    return budget


def check_files_matched(entry, rels, what):
    if entry.files is None:
        return
    for f in entry.files:
        if not any(kept(rel, [f]) for rel in rels):
            raise Error("%s: files entry %r matches nothing in the archive" % (what, f))


def extract_tar(archive, dest, entry):
    what = entry.dirname
    try:
        tar = tarfile.open(archive, "r:*")
    except (tarfile.TarError, OSError) as e:
        raise Error("%s: not a readable tar archive: %s" % (what, e))
    with tar:
        try:
            members = []
            for count, m in enumerate(tar, 1):
                if count > MAX_MEMBERS:
                    raise Error("%s: more than %d members" % (what, MAX_MEMBERS))
                members.append(m)
        except tarfile.TarError as e:
            raise Error("%s: damaged tar archive: %s" % (what, e))
        comment = tar.pax_headers.get("comment", "")
        if entry.commit and HEX40.match(comment) and comment != entry.commit:
            raise Error(
                "%s: the archive is of commit %s, the manifest pins %s"
                % (what, comment, entry.commit)
            )
        # Every member is checked before anything is written.
        by_rel = {}
        regular, links = [], []
        total = 0
        for m in members:
            rel = strip_name(m.name, entry.strip, what)
            if rel is None:
                continue
            if m.isdir():
                by_rel[rel] = m
                continue
            if m.issym():
                target = resolve_symlink(rel, m.linkname, what)
            elif m.islnk():
                target = strip_name(m.linkname, entry.strip, what)
                if target is None:
                    raise Error(
                        "%s: link %s -> %s leaves the tree" % (what, rel, m.linkname)
                    )
            elif m.isreg():
                target = None
                total += m.size
                if total > MAX_TREE_BYTES:
                    raise Error(
                        "%s: the tree is larger than %d bytes" % (what, MAX_TREE_BYTES)
                    )
            else:
                raise Error("%s: special file %s (type %r)" % (what, rel, m.type))
            if rel in by_rel:
                raise Error("%s: %s appears twice" % (what, rel))
            by_rel[rel] = m
            if kept(rel, entry.files):
                if target is None:
                    regular.append((rel, m))
                else:
                    links.append((rel, m, target))
        rels = [rel for rel, _ in regular] + [rel for rel, _, _ in links]
        check_files_matched(entry, rels, what)
        check_layout(rels, what)

        # A link becomes a copy of the regular file it finally points to.
        sources = []
        for rel, m, target in links:
            hops = 0
            while True:
                t = by_rel.get(target)
                if t is None or t.isdir():
                    raise Error(
                        "%s: link %s points to a directory or a missing member"
                        % (what, rel)
                    )
                if t.isreg():
                    break
                hops += 1
                if hops > MAX_LINK_HOPS:
                    raise Error("%s: link %s does not resolve" % (what, rel))
                if t.issym():
                    target = resolve_symlink(target, t.linkname, what)
                else:
                    target = strip_name(t.linkname, entry.strip, what)
                    if target is None:
                        raise Error("%s: link %s leaves the tree" % (what, rel))
            total += t.size
            if total > MAX_TREE_BYTES:
                raise Error(
                    "%s: the tree is larger than %d bytes" % (what, MAX_TREE_BYTES)
                )
            sources.append((rel, t))

        budget = MAX_TREE_BYTES
        try:
            if HAVE_TAR_FILTER and not _force_manual_tar:
                renamed = []
                for rel, m in regular:
                    c = copy.copy(m)
                    c.name = rel
                    renamed.append(c)
                    budget -= m.size
                tar.extractall(dest, members=renamed, filter="data")
            else:
                for rel, m in regular:
                    src = tar.extractfile(m)
                    budget = write_stream(src, dest, rel, budget)
            for rel, t in sources:
                budget = write_stream(tar.extractfile(t), dest, rel, budget)
        except (tarfile.TarError, OSError) as e:
            raise Error("%s: extraction failed: %s" % (what, e))


def extract_zip(archive, dest, entry):
    what = entry.dirname
    try:
        z = zipfile.ZipFile(archive)
    except (zipfile.BadZipFile, OSError) as e:
        raise Error("%s: not a readable zip archive: %s" % (what, e))
    with z:
        infos = z.infolist()
        if len(infos) > MAX_MEMBERS:
            raise Error("%s: more than %d members" % (what, MAX_MEMBERS))
        by_rel = {}
        regular, links = [], []
        total = 0
        for info in infos:
            name = info.filename
            mode = (info.external_attr >> 16) & 0xFFFF
            ftype = stat.S_IFMT(mode)
            rel = strip_name(name.rstrip("/"), entry.strip, what)
            if rel is None:
                continue
            if name.endswith("/") or ftype == stat.S_IFDIR:
                by_rel[rel] = ("dir", None)
                continue
            if info.flag_bits & 0x1:
                raise Error("%s: %s is encrypted" % (what, rel))
            if ftype == stat.S_IFLNK:
                if info.file_size > 4096:
                    raise Error("%s: link %s is too long" % (what, rel))
                target = resolve_symlink(
                    rel, z.read(info).decode("utf-8", "replace"), what
                )
                kind = "link"
            elif ftype in (0, stat.S_IFREG):
                target = None
                kind = "file"
                total += info.file_size
                if total > MAX_TREE_BYTES:
                    raise Error(
                        "%s: the tree is larger than %d bytes" % (what, MAX_TREE_BYTES)
                    )
            else:
                raise Error("%s: special file %s (mode %o)" % (what, rel, mode))
            if rel in by_rel:
                raise Error("%s: %s appears twice" % (what, rel))
            by_rel[rel] = (kind, info if kind == "file" else target)
            if kept(rel, entry.files):
                if kind == "file":
                    regular.append((rel, info))
                else:
                    links.append((rel, target))
        rels = [rel for rel, _ in regular] + [rel for rel, _ in links]
        check_files_matched(entry, rels, what)
        check_layout(rels, what)
        sources = []
        for rel, target in links:
            for _ in range(MAX_LINK_HOPS):
                kind, value = by_rel.get(target, (None, None))
                if kind != "link":
                    break
                target = value
            if kind != "file":
                raise Error(
                    "%s: link %s points to a directory or a missing member"
                    % (what, rel)
                )
            sources.append((rel, value))
        budget = MAX_TREE_BYTES
        try:
            for rel, info in regular + sources:
                with z.open(info) as src:
                    budget = write_stream(src, dest, rel, budget)
        except (zipfile.BadZipFile, RuntimeError, OSError, ValueError) as e:
            raise Error("%s: extraction failed: %s" % (what, e))


# ---------------------------------------------------------------------- cache


def rmtree(path):
    def retry(func, p, _exc):
        os.chmod(p, stat.S_IWRITE | stat.S_IREAD)
        func(p)

    if sys.version_info >= (3, 12):
        shutil.rmtree(path, onexc=retry)
    else:
        shutil.rmtree(path, onerror=retry)


def remove_path(path):
    """Delete one of fetch.py's own files or directories."""
    if os.path.isdir(path) and not os.path.islink(path) and plain_directory(path):
        rmtree(path)
    elif os.path.lexists(path):
        os.remove(path)


def remove_stamp(cache):
    try:
        os.remove(os.path.join(cache, STAMP))
    except FileNotFoundError:
        pass


def write_stamp(cache, manifest_bytes, trees):
    stamp = {
        "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
        "trees": trees,
    }
    data = (json.dumps(stamp, indent=2, sort_keys=True) + "\n").encode("utf-8")
    fd, tmp = tempfile.mkstemp(prefix=TMP_PREFIX + "stamp-", dir=cache)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.replace(tmp, os.path.join(cache, STAMP))
    except BaseException:
        with contextlib.suppress(OSError):
            os.remove(tmp)
        raise


class CacheLock(object):
    """An exclusive lock on <cache>/msil-corpus.lock, released when the
    process ends, so concurrent runs on one cache take turns."""

    def __init__(self, cache):
        self.path = os.path.join(cache, LOCK)
        self.f = None

    def _try(self):
        if IS_WINDOWS:
            import msvcrt

            self.f.seek(0)
            msvcrt.locking(self.f.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl

            fcntl.flock(self.f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)

    def __enter__(self):
        self.f = open(self.path, "a+b")
        deadline = time.monotonic() + LOCK_WAIT_SECONDS
        waited = False
        while True:
            try:
                self._try()
                return self
            except OSError:
                if time.monotonic() > deadline:
                    self.f.close()
                    raise Error("another fetch.py still holds %s" % self.path)
                if not waited:
                    say("fetch.py: waiting for another fetch.py (%s)" % self.path)
                    waited = True
                time.sleep(0.5)

    def __exit__(self, *exc):
        try:
            if IS_WINDOWS:
                import msvcrt

                self.f.seek(0)
                msvcrt.locking(self.f.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(self.f.fileno(), fcntl.LOCK_UN)
        except OSError:
            pass
        self.f.close()
        return False


def remove_stale_temporaries(cache):
    """Under the lock no other fetch.py runs, so leftover temporary files
    are a dead run's."""
    for name in os.listdir(cache):
        if name.startswith(TMP_PREFIX):
            with contextlib.suppress(OSError):
                remove_path(os.path.join(cache, name))


def inside(path, root):
    path = os.path.normcase(os.path.realpath(path))
    root = os.path.normcase(os.path.realpath(root))
    try:
        return os.path.commonpath([path, root]) == root
    except ValueError:  # different drives
        return False


def fetch_entry(entry, cache, allow_file_urls):
    """Download, verify and place one entry's tree; return the bytes
    downloaded."""
    final = os.path.join(cache, entry.dirname)
    if os.path.lexists(final) and not plain_directory(final):
        raise Error(
            "refusing to replace %s: it is not a directory fetch.py made" % final
        )
    work = tempfile.mkdtemp(prefix=TMP_PREFIX + entry.name + "-", dir=cache)
    try:
        tree = os.path.join(work, "tree")
        os.mkdir(tree)
        nbytes = 0
        if entry.kind == "files":
            lines = []
            for i, url in enumerate(entry.urls):
                path = os.path.join(work, "download-%d" % i)
                size, digest = download(url, path, allow_file_urls)
                nbytes += size
                name = url_file_name(url)
                lines.append("%s %s" % (digest, name))
                if len(entry.urls) == 1:
                    actual = digest
            if len(entry.urls) != 1:
                actual = lines_sha256(lines)
            if entry.archive_sha256 and actual != entry.archive_sha256:
                raise Error(
                    "archive_sha256 mismatch: manifest %s, download %s"
                    % (entry.archive_sha256, actual)
                )
            for i, url in enumerate(entry.urls):
                os.replace(
                    os.path.join(work, "download-%d" % i),
                    os.path.join(tree, url_file_name(url)),
                )
        else:
            path = os.path.join(work, "download")
            nbytes, digest = download(entry.urls[0], path, allow_file_urls)
            if entry.archive_sha256 and digest != entry.archive_sha256:
                raise Error(
                    "archive_sha256 mismatch: manifest %s, download %s"
                    % (entry.archive_sha256, digest)
                )
            if entry.kind == "tar":
                extract_tar(path, tree, entry)
            else:
                extract_zip(path, tree, entry)
            os.remove(path)
        if entry.license_file and not os.path.isfile(
            os.path.join(tree, *entry.license_file.split("/"))
        ):
            raise Error("license_file %s is not in the tree" % entry.license_file)
        actual = lines_sha256(tree_lines(tree))
        if actual != entry.tree_sha256:
            raise Error(
                "tree_sha256 mismatch: manifest %s, download %s"
                % (entry.tree_sha256, actual)
            )
        # Move the verified tree into place, replacing a stale one.
        remove_stamp(cache)
        old = None
        if os.path.lexists(final):
            old = os.path.join(work, "old")
            os.replace(final, old)
        try:
            os.replace(tree, final)
        except OSError:
            if old:
                os.replace(old, final)
            raise
        return nbytes
    except OSError as e:
        raise Error("%s: %s" % (entry.dirname, e))
    finally:
        with contextlib.suppress(OSError):
            rmtree(work)


def size_text(n):
    return "%.1f MB" % (n / 1e6) if n >= 1e5 else "%.1f kB" % (n / 1e3)


def run(entries, selected, cache, manifest_bytes, check, allow_file_urls):
    if check and not os.path.isdir(cache):
        for e in entries:
            if e.name in selected:
                say(
                    "fetch.py: %s: FAILED: missing (no cache %s; run fetch.py --cache %s)"
                    % (e.dirname, cache, cache)
                )
            else:
                say("fetch.py: %s: skipped" % e.dirname)
        say("fetch.py: 0 of %d verified" % len(selected))
        return 1
    os.makedirs(cache, exist_ok=True)
    with CacheLock(cache):
        remove_stale_temporaries(cache)
        trees = {}
        failed = 0
        downloaded = 0
        t0 = time.monotonic()
        for e in entries:
            if e.name not in selected:
                say("fetch.py: %s: skipped" % e.dirname)
                continue
            ok, reason = tree_state(os.path.join(cache, e.dirname), e.tree_sha256)
            if ok:
                trees[e.name] = e.tree_sha256
                say(
                    "fetch.py: %s: verified%s"
                    % (e.dirname, "" if check else " (in the cache)")
                )
                continue
            if check:
                failed += 1
                if reason == "missing":
                    reason = "missing (run fetch.py --cache %s)" % cache
                say("fetch.py: %s: FAILED: %s" % (e.dirname, reason))
                continue
            t = time.monotonic()
            try:
                nbytes = fetch_entry(e, cache, allow_file_urls)
            except Error as ex:
                failed += 1
                say("fetch.py: %s: FAILED: %s" % (e.dirname, ex))
                continue
            downloaded += nbytes
            trees[e.name] = e.tree_sha256
            say(
                "fetch.py: %s: verified (downloaded %s in %.1f s)"
                % (e.dirname, size_text(nbytes), time.monotonic() - t)
            )
        if failed:
            remove_stamp(cache)
        else:
            write_stamp(cache, manifest_bytes, trees)
        if downloaded:
            say(
                "fetch.py: downloaded %s in %.1f s into %s"
                % (size_text(downloaded), time.monotonic() - t0, cache)
            )
        say("fetch.py: %d of %d verified" % (len(trees), len(selected)))
        return 1 if failed else 0


# ----------------------------------------------------------------------- main


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise UsageError(message)


def parse_names(value, known, option):
    names = [n.strip() for n in value.split(",") if n.strip()]
    unknown = [n for n in names if n not in known]
    if unknown:
        raise UsageError(
            "%s: unknown entry name(s) %s (the manifest has %s)"
            % (option, ", ".join(unknown), ", ".join(known))
        )
    return names


def default_cache():
    env = os.environ.get("MSIL_CORPUS_CACHE")
    if env:
        return env
    return (
        DEFAULT_WINDOWS_CACHE if IS_WINDOWS else os.path.expanduser(DEFAULT_POSIX_CACHE)
    )


def main(argv=None, manifest_path=MANIFEST, allow_file_urls=False):
    """The command line; allow_file_urls is for --self-test only."""
    ap = Parser(prog="fetch.py", description=__doc__.splitlines()[0])
    ap.add_argument(
        "--cache",
        metavar="DIR",
        help="the cache (default: $MSIL_CORPUS_CACHE, else %s on Windows and "
        "~/.cache/msil-corpus elsewhere)" % DEFAULT_WINDOWS_CACHE.replace("%", "%%"),
    )
    ap.add_argument(
        "--check", action="store_true", help="verify the trees without network access"
    )
    ap.add_argument("--only", metavar="N[,N...]", help="select only these entries")
    ap.add_argument("--skip", metavar="N[,N...]", help="leave these entries out")
    ap.add_argument("--list", action="store_true", help="print the entries and exit")
    ap.add_argument(
        "--self-test", action="store_true", help="run the offline self-test"
    )
    try:
        args = ap.parse_args(argv)
    except UsageError as e:
        complain(str(e))
        return 2
    except SystemExit as e:  # --help
        return e.code if isinstance(e.code, int) else 0
    if args.self_test:
        if (
            args.cache
            or args.check
            or args.only is not None
            or args.skip is not None
            or args.list
        ):
            complain("--self-test takes no other option")
            return 2
        return self_test()
    if args.list and args.check:
        complain("--list and --check exclude each other")
        return 2
    try:
        manifest_bytes, entries = load_manifest(manifest_path, allow_file_urls)
    except Error as e:
        complain(str(e))
        return 1
    known = [e.name for e in entries]
    try:
        selected = set(known)
        if args.only is not None:
            selected = set(parse_names(args.only, known, "--only"))
            if not selected:
                raise UsageError("--only names no entry")
        if args.skip is not None:
            selected -= set(parse_names(args.skip, known, "--skip"))
        if not selected:
            raise UsageError("the selection leaves no entry")
    except UsageError as e:
        complain(str(e))
        return 2
    if args.list:
        for e in entries:
            if e.name in selected:
                say("%-12s %-26s %s" % (e.name, e.dirname, e.license))
                for u in e.urls:
                    say("    %s" % u)
        return 0
    cache = os.path.abspath(args.cache or default_cache())
    if inside(cache, REPO):
        complain(
            "the cache %s is inside the repository %s; put it outside (for example %s)"
            % (
                cache,
                REPO,
                DEFAULT_WINDOWS_CACHE if IS_WINDOWS else DEFAULT_POSIX_CACHE,
            )
        )
        return 2
    try:
        return run(
            entries, selected, cache, manifest_bytes, args.check, allow_file_urls
        )
    except (Error, OSError) as e:
        complain(str(e))
        with contextlib.suppress(OSError):
            remove_stamp(cache)
        return 1


# ------------------------------------------------------------------ self-test


def _tar(path, members, pax_comment=None):
    """Write a .tar.gz of (name, kind, data-or-linkname) members."""
    kw = {"format": tarfile.PAX_FORMAT}
    if pax_comment:
        kw["pax_headers"] = {"comment": pax_comment}
    with tarfile.open(path, "w:gz", **kw) as tar:
        for name, kind, value in members:
            ti = tarfile.TarInfo(name)
            ti.mtime = 0
            ti.mode = 0o755 if kind == "dir" else 0o644
            if kind == "file":
                ti.size = len(value)
                tar.addfile(ti, io.BytesIO(value))
                continue
            ti.type = {
                "dir": tarfile.DIRTYPE,
                "sym": tarfile.SYMTYPE,
                "hard": tarfile.LNKTYPE,
                "fifo": tarfile.FIFOTYPE,
                "chr": tarfile.CHRTYPE,
            }[kind]
            if kind in ("sym", "hard"):
                ti.linkname = value
            tar.addfile(ti)


def _zip(path, members):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, kind, value in members:
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            if kind == "dir":
                info.external_attr = (stat.S_IFDIR | 0o755) << 16
                value = b""
            elif kind == "sym":
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
                value = value.encode("utf-8")
            else:
                info.external_attr = (stat.S_IFREG | 0o644) << 16
            z.writestr(info, value)


def _tree_hash(files):
    """tree_sha256 of {relative path: bytes}, computed independently of
    tree_lines()."""
    return lines_sha256(
        ["%s %s" % (hashlib.sha256(d).hexdigest(), p) for p, d in files.items()]
    )


def _read_tree(root):
    out = {}
    for dirpath, dirnames, filenames in os.walk(root):
        for f in filenames:
            p = os.path.join(dirpath, f)
            with open(p, "rb") as fh:
                out[os.path.relpath(p, root).replace(os.sep, "/")] = fh.read()
    return out


def self_test():
    failures = []

    def expect(cond, what, detail=""):
        print(("ok    " if cond else "FAIL  ") + what, flush=True)
        if not cond:
            for line in detail.strip().splitlines():
                print("      " + line, flush=True)
            failures.append(what)

    def run_main(argv, manifest, allow=True):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = main(argv, manifest_path=manifest, allow_file_urls=allow)
        return rc, out.getvalue(), err.getvalue()

    def leftovers(cache):
        if not os.path.isdir(cache):
            return []
        return [n for n in os.listdir(cache) if n.startswith(TMP_PREFIX)]

    def stamp_of(cache):
        try:
            with open(os.path.join(cache, STAMP), "rb") as f:
                return json.loads(f.read().decode("utf-8"))
        except OSError:
            return None

    def write_manifest(path, programs):
        data = json.dumps({"version": 1, "programs": programs}, indent=2) + "\n"
        with open(path, "wb") as f:
            f.write(data.encode("utf-8"))
        return hashlib.sha256(data.encode("utf-8")).hexdigest()

    global _force_manual_tar
    tmp = tempfile.mkdtemp(prefix="msil-corpus-selftest-")
    try:
        if inside(tmp, REPO):
            expect(False, "the temporary directory %s is outside the repository" % tmp)
            return 1
        src = os.path.join(tmp, "src")
        os.makedirs(src)

        def uri(name):
            import pathlib

            return pathlib.Path(os.path.join(src, name)).as_uri()

        # The tree hash construction (5.12.9), pinned by a known answer.
        expect(
            lines_sha256(["b x", "a y"]) == hashlib.sha256(b"a y\nb x\n").hexdigest(),
            "tree_sha256 is the SHA-256 of the sorted lines, each ended by a newline",
        )

        # A .tar.gz with a strip component, a link inside the tree and a hard
        # link; a zip; a single file; a list of single files.
        commit = "0123456789abcdef0123456789abcdef01234567"
        lic, a_c, b_h = b"Licence text\n", b"int a;\n", b"#define B 1\r\n"
        _tar(
            os.path.join(src, "pkg.tar.gz"),
            [
                ("pkg-1", "dir", None),
                ("pkg-1/LICENSE", "file", lic),
                ("pkg-1/src", "dir", None),
                ("pkg-1/src/a.c", "file", a_c),
                ("pkg-1/src/sub/b.h", "file", b_h),
                ("pkg-1/src/hard.c", "hard", "pkg-1/src/a.c"),
                ("pkg-1/doc/alias.txt", "sym", "../LICENSE"),
                ("pkg-1/doc/chain.txt", "sym", "alias.txt"),
            ],
            pax_comment=commit,
        )
        tgz_files = {
            "LICENSE": lic,
            "src/a.c": a_c,
            "src/sub/b.h": b_h,
            "src/hard.c": a_c,
            "doc/alias.txt": lic,
            "doc/chain.txt": lic,
        }
        restricted_files = {
            "LICENSE": lic,
            "src/a.c": a_c,
            "src/sub/b.h": b_h,
            "src/hard.c": a_c,
        }
        zdata = {"LICENSE.txt": b"zip licence\n", "data/x.bin": bytes(range(256))}
        _zip(
            os.path.join(src, "zz.zip"),
            [("zz/", "dir", None)] + [("zz/" + p, "file", d) for p, d in zdata.items()],
        )
        one = b"/* one */\n"
        with open(os.path.join(src, "one.h"), "wb") as f:
            f.write(one)
        pair = {"t1.c": b"int t1;\n", "t2.h": b"int t2(void);\n"}
        for n, d in pair.items():
            with open(os.path.join(src, n), "wb") as f:
                f.write(d)

        def sha(path):
            return sha256_file(os.path.join(src, path))

        good = [
            {
                "name": "tgz",
                "license": "L",
                "license_file": "LICENSE",
                "url": uri("pkg.tar.gz"),
                "ref": "1",
                "commit": commit,
                "tree_sha256": _tree_hash(tgz_files),
                "strip": 1,
            },
            {
                "name": "zip",
                "license": "L",
                "license_file": "LICENSE.txt",
                "url": uri("zz.zip"),
                "ref": "2.0",
                "archive_sha256": sha("zz.zip"),
                "tree_sha256": _tree_hash(zdata),
                "strip": 1,
            },
            {
                "name": "one",
                "license": "L",
                "license_file": None,
                "url": uri("one.h"),
                "ref": "3",
                "archive_sha256": sha("one.h"),
                "tree_sha256": _tree_hash({"one.h": one}),
                "strip": 0,
            },
            {
                "name": "pair",
                "license": "L",
                "license_file": None,
                "url": [uri("t1.c"), uri("t2.h")],
                "ref": "4",
                "archive_sha256": lines_sha256(["%s %s" % (sha(n), n) for n in pair]),
                "tree_sha256": _tree_hash(pair),
                "strip": 0,
            },
            {
                "name": "restricted",
                "license": "L",
                "license_file": "LICENSE",
                "url": uri("pkg.tar.gz"),
                "ref": "5",
                "commit": commit,
                "tree_sha256": _tree_hash(restricted_files),
                "strip": 1,
                "files": ["./src/", "LICENSE"],
            },
        ]
        mgood = os.path.join(tmp, "good.json")
        mgood_sha = write_manifest(mgood, good)
        names = [e["name"] for e in good]
        dirs = {e["name"]: "%s-%s" % (e["name"], e["ref"]) for e in good}

        # 1. Success, then a second run that downloads nothing.
        cache = os.path.join(tmp, "cache")
        os.makedirs(os.path.join(cache, "other"))
        for p in ("keep.txt", os.path.join("other", "x")):
            with open(os.path.join(cache, p), "w") as f:
                f.write("not fetch.py's\n")
        os.makedirs(os.path.join(cache, TMP_PREFIX + "dead-run"))
        rc, out, err = run_main(["--cache", cache], mgood)
        expect(
            rc == 0
            and out.count("verified (downloaded") == 5
            and "fetch.py: 5 of 5 verified" in out,
            "a first run fetches all five entries "
            "(tar.gz with strip, zip, single file, list of files, files restriction)",
            "rc %d\n%s%s" % (rc, out, err),
        )
        expect(
            _read_tree(os.path.join(cache, dirs["tgz"])) == tgz_files,
            "the tar.gz tree holds the stripped members; links became copies of their targets",
        )
        expect(
            not any(
                os.path.islink(os.path.join(dp, f))
                for dp, _, fs in os.walk(os.path.join(cache, dirs["tgz"]))
                for f in fs
            ),
            "no link is created in the cache",
        )
        expect(
            _read_tree(os.path.join(cache, dirs["restricted"])) == restricted_files,
            "files restricts a tree to the listed paths",
        )
        expect(
            _read_tree(os.path.join(cache, dirs["zip"])) == zdata
            and _read_tree(os.path.join(cache, dirs["pair"])) == pair
            and _read_tree(os.path.join(cache, dirs["one"])) == {"one.h": one},
            "the zip, the single file and the list of files are in place",
        )
        st = stamp_of(cache)
        expect(
            st
            == {
                "manifest_sha256": mgood_sha,
                "trees": {e["name"]: e["tree_sha256"] for e in good},
            },
            "the stamp holds the manifest's sha256 and every selected tree",
            repr(st),
        )
        expect(
            leftovers(cache) == [],
            "no temporary file is left, and a dead run's is removed",
        )
        expect(
            os.path.isfile(os.path.join(cache, "keep.txt"))
            and os.path.isfile(os.path.join(cache, "other", "x")),
            "files fetch.py did not make survive",
        )
        os.rename(src, src + "-away")
        rc, out, err = run_main(["--cache", cache], mgood)
        os.rename(src + "-away", src)
        expect(
            rc == 0
            and out.count("verified (in the cache)") == 5
            and "downloaded" not in out,
            "a second run downloads nothing",
            "rc %d\n%s%s" % (rc, out, err),
        )

        # --list.
        rc, out, err = run_main(["--list"], mgood)
        expect(
            rc == 0 and all(dirs[n] in out for n in names) and uri("t2.h") in out,
            "--list prints every entry and its URLs",
        )

        # 2. A tree changed after the fetch: --check fails and removes the
        # stamp; a fetch repairs the tree.
        a_path = os.path.join(cache, dirs["tgz"], "src", "a.c")
        with open(a_path, "ab") as f:
            f.write(b"/* planted */\n")
        rc, out, err = run_main(["--cache", cache, "--check"], mgood)
        expect(
            rc == 1
            and "tgz-1: FAILED: tree_sha256 mismatch" in out
            and out.count(": verified") == 4
            and stamp_of(cache) is None,
            "--check exits 1 on a planted edit and removes the stamp",
            "rc %d\n%s" % (rc, out),
        )
        rc, out, err = run_main(["--cache", cache], mgood)
        with open(a_path, "rb") as f:
            repaired = f.read() == a_c
        expect(
            rc == 0
            and "tgz-1: verified (downloaded" in out
            and repaired
            and stamp_of(cache) is not None
            and os.path.isfile(os.path.join(cache, "keep.txt")),
            "a fetch replaces the changed tree and writes the stamp again",
            "rc %d\n%s" % (rc, out),
        )
        link = os.path.join(cache, dirs["one"], "planted-link")
        try:
            os.symlink(os.path.join(cache, "keep.txt"), link)
        except (OSError, NotImplementedError):
            link = None
        if link:
            rc, out, err = run_main(
                ["--cache", cache, "--check", "--only", "one"], mgood
            )
            expect(
                rc == 1 and "unexpected link" in out, "a link in a tree fails --check"
            )
            rc, out, err = run_main(["--cache", cache, "--only", "one"], mgood)
            expect(
                rc == 0 and os.path.isfile(os.path.join(cache, "keep.txt")),
                "replacing a tree with a link in it removes the link, not its target",
            )

        # 3. --skip and --only stamp only the selected trees; a --check
        # without --skip then fails on the missing entry.
        cache2 = os.path.join(tmp, "cache2")
        rc, out, err = run_main(["--cache", cache2, "--skip", "zip, pair"], mgood)
        st = stamp_of(cache2) or {}
        expect(
            rc == 0
            and "zip-2.0: skipped" in out
            and "pair-4: skipped" in out
            and "fetch.py: 3 of 3 verified" in out
            and sorted(st.get("trees", {})) == ["one", "restricted", "tgz"]
            and not os.path.exists(os.path.join(cache2, dirs["zip"])),
            "a --skip run fetches and stamps only the selected entries",
            "rc %d\n%s%s" % (rc, out, err),
        )
        rc, out, err = run_main(["--cache", cache2, "--check"], mgood)
        expect(
            rc == 1 and "zip-2.0: FAILED: missing" in out and stamp_of(cache2) is None,
            "--check without --skip exits 1 on the left-out entry and removes the stamp",
        )
        rc, out, err = run_main(
            ["--cache", cache2, "--check", "--skip", "zip,pair"], mgood
        )
        expect(
            rc == 0
            and sorted((stamp_of(cache2) or {}).get("trees", {}))
            == ["one", "restricted", "tgz"],
            "--check --skip stamps the selected trees",
        )
        rc, out, err = run_main(["--cache", cache2, "--check", "--only", "one"], mgood)
        expect(
            rc == 0 and sorted((stamp_of(cache2) or {}).get("trees", {})) == ["one"],
            "--only stamps exactly the entries it names",
        )
        rc, out, err = run_main(["--cache", cache, "--check", "--skip", ""], mgood)
        expect(
            rc == 0 and "fetch.py: 5 of 5 verified" in out,
            "an empty --skip skips nothing",
        )

        # 4. Usage errors.
        rc, out, err = run_main(["--cache", cache, "--skip", "zip,nosuch"], mgood)
        expect(
            rc == 2 and "nosuch" in err,
            "an unknown --skip name exits 2",
            "rc %d\n%s" % (rc, err),
        )
        rc, out, err = run_main(["--cache", cache, "--only", "nosuch"], mgood)
        expect(rc == 2 and "nosuch" in err, "an unknown --only name exits 2")
        rc, out, err = run_main(
            ["--cache", cache, "--only", "one", "--skip", "one"], mgood
        )
        expect(rc == 2, "a selection that leaves no entry exits 2")
        rc, out, err = run_main(["--list", "--check"], mgood)
        expect(rc == 2, "--list with --check exits 2")
        rc, out, err = run_main(["--self-test", "--check"], mgood)
        expect(rc == 2, "--self-test with another option exits 2")
        rc, out, err = run_main(["--bogus"], mgood)
        expect(rc == 2, "an unknown option exits 2")

        # 5. A cache inside the repository is refused, and nothing is created.
        inner = os.path.join(
            REPO, "msil", "utils", "corpus", TMP_PREFIX + "selftest-cache"
        )
        variants = [inner, REPO]
        if IS_WINDOWS:
            variants.append(inner.upper())
        for v in variants:
            rc, out, err = run_main(["--cache", v], mgood)
            expect(
                rc == 2
                and "inside the repository" in err
                and not os.path.exists(inner),
                "a cache inside the repository is refused (%s)"
                % (
                    "the repository"
                    if v == REPO
                    else "upper case"
                    if v != inner
                    else "below it"
                ),
                "rc %d\n%s" % (rc, err),
            )
        rc, out, err = run_main(["--cache", inner, "--check"], mgood)
        expect(rc == 2 and not os.path.exists(inner), "--check refuses it too")

        # 6. Hash mismatches.
        cache3 = os.path.join(tmp, "cache3")
        bad = json.loads(json.dumps(good))
        bad[1]["archive_sha256"] = "0" * 64
        bad[2]["tree_sha256"] = "1" * 64
        mbad = os.path.join(tmp, "bad.json")
        write_manifest(mbad, bad)
        rc, out, err = run_main(["--cache", cache3, "--only", "zip"], mbad)
        expect(
            rc == 1
            and "zip-2.0: FAILED: archive_sha256 mismatch" in out
            and not os.path.exists(os.path.join(cache3, dirs["zip"]))
            and stamp_of(cache3) is None
            and leftovers(cache3) == [],
            "an archive hash mismatch fails before extraction and leaves nothing",
            "rc %d\n%s" % (rc, out),
        )
        rc, out, err = run_main(["--cache", cache3, "--only", "one"], mbad)
        expect(
            rc == 1
            and "one-3: FAILED: tree_sha256 mismatch" in out
            and not os.path.exists(os.path.join(cache3, dirs["one"]))
            and leftovers(cache3) == [],
            "a tree hash mismatch keeps the tree out of the cache",
        )
        bad = json.loads(json.dumps(good))
        bad[0]["commit"] = "f" * 40
        write_manifest(mbad, bad)
        rc, out, err = run_main(["--cache", cache3, "--only", "tgz"], mbad)
        expect(
            rc == 1 and "the archive is of commit %s" % commit in out,
            "an archive of another commit than the pinned one is refused",
        )
        bad = json.loads(json.dumps(good))
        bad[2]["license_file"] = "COPYING"
        write_manifest(mbad, bad)
        rc, out, err = run_main(["--cache", cache3, "--only", "one"], mbad)
        expect(
            rc == 1 and "license_file COPYING is not in the tree" in out,
            "a missing license_file fails",
        )
        bad = json.loads(json.dumps(good))
        bad[3]["url"] = [uri("t1.c"), uri("gone.h")]
        write_manifest(mbad, bad)
        rc, out, err = run_main(["--cache", cache3, "--only", "pair"], mbad)
        expect(
            rc == 1
            and "pair-4: FAILED: cannot download" in out
            and not os.path.exists(os.path.join(cache3, dirs["pair"]))
            and leftovers(cache3) == [],
            "a failed download leaves nothing behind",
            "rc %d\n%s%s" % (rc, out, err),
        )

        # 7. Hostile archives: traversal, absolute names, links leaving the
        # tree, special files, names Windows cannot hold, case collisions.
        hostile = [
            (
                "dotdot",
                "tar",
                [("p/../../evil.txt", "file", b"x")],
                "climbs out of the tree",
            ),
            ("absolute", "tar", [("/abs/evil.txt", "file", b"x")], "absolute name"),
            ("drive", "tar", [("C:/evil.txt", "file", b"x")], "absolute name"),
            (
                "symlink-out",
                "tar",
                [("p/f", "file", b"x"), ("p/link", "sym", "../../evil.txt")],
                "leaves the tree",
            ),
            (
                "symlink-abs",
                "tar",
                [("p/link", "sym", "/etc/passwd")],
                "leaves the tree",
            ),
            (
                "hardlink-out",
                "tar",
                [("p/f", "file", b"x"), ("p/hl", "hard", "evil.txt")],
                "leaves the tree",
            ),
            (
                "hardlink-up",
                "tar",
                [("p/hl", "hard", "p/../../evil.txt")],
                "climbs out",
            ),
            (
                "symlink-dir",
                "tar",
                [("p/d/f", "file", b"x"), ("p/l", "sym", "d")],
                "points to a directory or a missing member",
            ),
            ("fifo", "tar", [("p/fifo", "fifo", None)], "special file"),
            ("device", "tar", [("p/dev", "chr", None)], "special file"),
            ("reserved", "tar", [("p/aux.c", "file", b"x")], "not portable"),
            ("colon", "tar", [("p/a:b", "file", b"x")], "not portable"),
            (
                "case",
                "tar",
                [("p/A.c", "file", b"x"), ("p/a.c", "file", b"y")],
                "differ only in case",
            ),
            (
                "zip-dotdot",
                "zip",
                [("z/../../evil.txt", "file", b"x")],
                "climbs out of the tree",
            ),
            (
                "zip-link-out",
                "zip",
                [("z/f", "file", b"x"), ("z/l", "sym", "../../evil.txt")],
                "leaves the tree",
            ),
        ]
        progs = []
        for name, kind, members, _ in hostile:
            fn = name + (".tar.gz" if kind == "tar" else ".zip")
            (_tar if kind == "tar" else _zip)(os.path.join(src, fn), members)
            progs.append(
                {
                    "name": name,
                    "license": "L",
                    "license_file": None,
                    "url": uri(fn),
                    "ref": "1",
                    "archive_sha256": sha(fn),
                    "tree_sha256": "2" * 64,
                    "strip": 1,
                }
            )
        mhostile = os.path.join(tmp, "hostile.json")
        write_manifest(mhostile, progs)
        cache4 = os.path.join(tmp, "cache4")
        for name, kind, members, message in hostile:
            rc, out, err = run_main(["--cache", cache4, "--only", name], mhostile)
            escaped = [
                p
                for p in ("evil.txt", os.path.join("abs", "evil.txt"))
                if os.path.exists(os.path.join(tmp, p))
            ]
            expect(
                rc == 1
                and message in out
                and not escaped
                and not os.path.exists(os.path.join(cache4, name + "-1"))
                and leftovers(cache4) == [],
                "hostile archive '%s' is refused: %s" % (name, message),
                "rc %d\n%s" % (rc, out),
            )

        # 8. Only https: an http URL and, outside the self-test, a file URL.
        bad = json.loads(json.dumps(good))
        bad[0]["url"] = "http://example.invalid/pkg.tar.gz"
        bad[0].pop("commit")
        bad[0]["archive_sha256"] = "3" * 64
        write_manifest(mbad, bad)
        rc, out, err = run_main(["--cache", cache3], mbad)
        expect(
            rc == 1 and "only https URLs are fetched" in err,
            "an http URL is refused",
            "rc %d\n%s" % (rc, err),
        )
        rc, out, err = run_main(["--cache", cache3], mgood, allow=False)
        expect(
            rc == 1 and "only https URLs are fetched" in err, "a file URL is refused"
        )
        handler = HttpsOnlyRedirectHandler()
        req = urllib.request.Request("https://example.invalid/a.tar.gz")
        try:
            handler.redirect_request(
                req, None, 302, "Found", {}, "http://example.invalid/b"
            )
            refused = False
        except Error:
            refused = True
        expect(refused, "a redirect from https to http is refused")

        # 9. Manifest errors.
        for change, what in (
            ({"strip": -1}, "a negative strip"),
            ({"tree_sha256": "abc"}, "a short tree_sha256"),
            ({"archive_sha256": "0" * 64, "extra": 1}, "an unknown key"),
            (
                {"url": "https://example.invalid/pkg.tar.gz"},
                "a commit with a URL that is not its GitHub archive",
            ),
            (
                {"url": "https://github.com/o/r/archive/" + "e" * 40 + ".tar.gz"},
                "a GitHub archive of another commit",
            ),
        ):
            bad = json.loads(json.dumps(good))
            bad[0].update(change)
            write_manifest(mbad, bad)
            rc, out, err = run_main(["--list"], mbad)
            expect(rc == 1 and "manifest.json" in err, "%s is a manifest error" % what)

        # 10. The extraction without tarfile's filters gives the same tree.
        cache5 = os.path.join(tmp, "cache5")
        _force_manual_tar = True
        try:
            rc, out, err = run_main(
                ["--cache", cache5, "--only", "tgz,restricted"], mgood
            )
        finally:
            _force_manual_tar = False
        expect(
            rc == 0 and _read_tree(os.path.join(cache5, dirs["tgz"])) == tgz_files,
            "the extraction without tarfile's filters gives the same tree",
            "rc %d\n%s" % (rc, out),
        )

        # 11. --check of a cache that does not exist creates nothing.
        cache6 = os.path.join(tmp, "cache6")
        rc, out, err = run_main(["--cache", cache6, "--check"], mgood)
        expect(
            rc == 1
            and out.count("FAILED: missing") == 5
            and not os.path.exists(cache6),
            "--check of a missing cache exits 1 and creates nothing",
        )

        # 12. The checked-in manifest.
        try:
            _, entries = load_manifest(MANIFEST)
            got = sorted(e.name for e in entries)
        except Error as e:
            got = str(e)
        expect(
            got
            == sorted(
                [
                    "zlib",
                    "tinf",
                    "monocypher",
                    "coremark",
                    "lz4",
                    "blake2",
                    "tweetnacl",
                    "kissfft",
                    "jsmn",
                    "c-testsuite",
                ]
            ),
            "manifest.json is valid and has the ten exit-corpus entries",
            repr(got),
        )
    finally:
        _force_manual_tar = False
        with contextlib.suppress(OSError):
            rmtree(tmp)
    print(
        "fetch.py self-test: %s (%d failure(s))"
        % ("PASS" if not failures else "FAIL", len(failures)),
        flush=True,
    )
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
