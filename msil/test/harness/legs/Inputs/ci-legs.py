#!/usr/bin/env python3
# ===-- ci-legs.py - Run the CI leg scripts against a stand-in build -------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Run msil/utils/ci/lit-legs.ps1 or lit-legs.sh against a stand-in build
directory, for ci-legs-corpus.test and ci-legs-corpus-sh.test.

    ci-legs.py setup BUILD
        Make BUILD/bin/llvm-lit.py (what msil-lit.ps1 runs) and
        BUILD/bin/llvm-lit (what lit-legs.sh runs): a stand-in lit that
        appends its arguments ('arg: <a>') and the lines of every '@<file>'
        response file ('rsp: <line>') to BUILD/lit-calls.log, prints lit's
        summary line '  Passed: 1 (100.00%)' and exits 0.
    ci-legs.py ps1 BUILD [VAR=VALUE...] -- [ARG...]
        pwsh -NoProfile -File lit-legs.ps1 -BuildDir BUILD ARG...
    ci-legs.py sh [--distro D] BUILD [VAR=VALUE...] -- [ARG...]
        bash lit-legs.sh ARG... BUILD: inside the WSL distribution D (default
        Ubuntu) on Windows, with every absolute Windows path among BUILD, the
        VALUEs and the ARGs written as /mnt/<drive>/...; directly elsewhere.

The script runs with MSIL_CORPUS_SKIP, MSIL_CORPUS_CACHE, MSIL_CI_RESULTS and
GITHUB_STEP_SUMMARY removed from its environment and then the VAR=VALUE pairs
set, so neither a gate's results file nor a CI step summary gets these rows.
Its stdout and stderr are printed together, without colour escapes and CRs;
ci-legs.py exits with its exit status.
"""

import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, *[os.pardir] * 5))
CI = os.path.join(REPO, "msil", "utils", "ci")
CLEARED = ("MSIL_CORPUS_SKIP", "MSIL_CORPUS_CACHE", "MSIL_CI_RESULTS", "GITHUB_STEP_SUMMARY")

STAND_IN_LIT = r'''import os, sys
log = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "lit-calls.log")
with open(log, "a", encoding="utf-8", newline="\n") as f:
    for a in sys.argv[1:]:
        f.write("arg: %s\n" % a)
        if a.startswith("@"):
            with open(a[1:], encoding="utf-8-sig") as r:
                for line in r.read().splitlines():
                    f.write("rsp: %s\n" % line)
print("Total Discovered Tests: 1")
print("  Passed: 1 (100.00%)")
'''

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
DRIVE_RE = re.compile(r"^([A-Za-z]):[\\/](.*)$")


def usage(msg):
    sys.stderr.write("ci-legs.py: %s\n" % msg)
    return 2


def wsl_path(text):
    """X:\\a\\b -> /mnt/x/a/b; anything else unchanged."""
    m = DRIVE_RE.match(text)
    if not m:
        return text
    return "/mnt/%s/%s" % (m.group(1).lower(), m.group(2).replace("\\", "/"))


def split(argv):
    """(VAR=VALUE pairs, ARGs) of '[VAR=VALUE...] -- [ARG...]'."""
    if "--" not in argv:
        raise ValueError("missing '--' before the script's arguments")
    i = argv.index("--")
    pairs = []
    for p in argv[:i]:
        if "=" not in p or not re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", p):
            raise ValueError("not VAR=VALUE: %r" % p)
        pairs.append(tuple(p.split("=", 1)))
    return pairs, argv[i + 1 :]


def run(cmd, env):
    res = subprocess.run(
        cmd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    text = ANSI_RE.sub("", res.stdout.decode("utf-8", "replace")).replace("\r", "")
    sys.stdout.write(text)
    sys.stdout.flush()
    return res.returncode


def clean_env(pairs):
    env = {k: v for k, v in os.environ.items() if k.upper() not in CLEARED}
    env.update(pairs)
    # pwsh's own colour (PowerShell 7.2 and later honour NO_COLOR).
    env["NO_COLOR"] = "1"
    return env


def setup(build):
    bindir = os.path.join(build, "bin")
    os.makedirs(bindir, exist_ok=True)
    for name in ("llvm-lit.py", "llvm-lit"):
        with open(os.path.join(bindir, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(STAND_IN_LIT)
    return 0


def ps1(build, pairs, args):
    pwsh = shutil.which("pwsh")
    if not pwsh:
        return usage("pwsh not found")
    cmd = [pwsh, "-NoProfile", "-NonInteractive", "-File", os.path.join(CI, "lit-legs.ps1")]
    return run(cmd + ["-BuildDir", os.path.abspath(build)] + args, clean_env(pairs))


def sh(distro, build, pairs, args):
    build = os.path.abspath(build)
    if os.name != "nt":
        cmd = ["bash", os.path.join(CI, "lit-legs.sh")] + args + [build]
        return run(cmd, clean_env(pairs))
    wsl = shutil.which("wsl.exe") or shutil.which("wsl")
    if not wsl:
        return usage("wsl.exe not found")
    unset = []
    for v in CLEARED:
        unset += ["-u", v]
    sets = ["%s=%s" % (k, wsl_path(v)) for k, v in pairs]
    cmd = [wsl, "-d", distro, "--cd", REPO, "--exec", "env"] + unset + sets
    cmd += ["bash", "msil/utils/ci/lit-legs.sh"] + [wsl_path(a) for a in args] + [wsl_path(build)]
    env = dict(os.environ)
    env["WSL_UTF8"] = "1"  # wsl.exe's own messages in UTF-8, not UTF-16
    return run(cmd, env)


def main(argv):
    if len(argv) >= 2 and argv[0] == "setup":
        return setup(argv[1])
    try:
        if len(argv) >= 2 and argv[0] == "ps1":
            pairs, args = split(argv[2:])
            return ps1(argv[1], pairs, args)
        if len(argv) >= 2 and argv[0] == "sh":
            rest = argv[1:]
            distro = "Ubuntu"
            if len(rest) >= 2 and rest[0] == "--distro":
                distro, rest = rest[1], rest[2:]
            if not rest:
                return usage("sh needs BUILD")
            pairs, args = split(rest[1:])
            return sh(distro, rest[0], pairs, args)
    except ValueError as e:
        return usage(str(e))
    return usage("usage: setup BUILD | ps1 BUILD [VAR=VALUE...] -- [ARG...] | "
                 "sh [--distro D] BUILD [VAR=VALUE...] -- [ARG...]")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
