"""PROTOTYPE of the 'manifest' leg of the proposed upstream-hooks check (recon M11 item 7).

  python check_manifest.py <repo> <manifest> [--changed <file with paths>]

1. upstream base = second parent of the newest first-parent merge whose subject starts with
   '[MSIL] Merge upstream llvm-project main at ' (override: $MSIL_UPSTREAM_BASE).
2. hooked paths = git diff --name-only <base> HEAD minus fork-only prefixes and 'fork-only' lines.
3. every hooked path has a 'hook' block with a lit/gtest/compile/utc/none line; every 'hook' is in
   the diff (no stale blocks); lit globs match at least one file; 'fork' paths exist.
4. switch coverage: every file with 'case [llvm::]Triple::XCOFF:' or 'case [llvm::]MCContext::IsXCOFF:'
   names Triple::MSIL / IsMSIL or is a 'switch-default' line.
Exit 1 with one line per problem."""
import glob
import os
import re
import subprocess
import sys

repo, manifest = sys.argv[1], sys.argv[2]
changed_override = sys.argv[4] if len(sys.argv) > 4 and sys.argv[3] == "--changed" else None
FORK_PREFIXES = ("llvm/lib/Target/MSIL/", "llvm/test/CodeGen/MSIL/", "llvm/docs/MSIL/",
                 "llvm/unittests/Target/MSIL/", "clang/test/CodeGen/MSIL/", "msil/", ".github/msil/")
KINDS = ("lit", "gtest", "compile", "utc", "none")


def git(*args):
    return subprocess.run(["git", "-C", repo] + list(args), check=True, capture_output=True,
                          text=True).stdout


problems = []
hooks, fork_only, switch_default, cur = {}, set(), set(), None
for n, raw in enumerate(open(manifest, encoding="utf-8"), 1):
    line = raw.split(" #")[0].rstrip() if not raw.lstrip().startswith("#") else ""
    if not line.strip():
        continue
    word, _, rest = line.strip().partition(" ")
    if not raw[0].isspace():
        cur = None
        if word == "hook":
            cur = hooks.setdefault(rest, {k: [] for k in KINDS + ("fork",)})
        elif word == "fork-only":
            fork_only.add(rest)
        elif word == "switch-default":
            switch_default.add(rest)
        elif word != "allow":
            problems.append("%s:%d: unknown line '%s'" % (manifest, n, line))
    elif cur is None or word not in KINDS + ("fork",):
        problems.append("%s:%d: '%s' outside a hook block or unknown" % (manifest, n, line.strip()))
    else:
        cur[word].append(rest)
        if word == "none" and not rest:
            problems.append("%s:%d: 'none' needs a reason" % (manifest, n))

base = os.environ.get("MSIL_UPSTREAM_BASE")
if not base:
    parents = git("log", "--first-parent", "--merges", "-1",
                  "--grep=^\\[MSIL\\] Merge upstream llvm-project main at ", "--format=%P", "HEAD").split()
    base = parents[1] if len(parents) > 1 else None
if changed_override:
    changed = [l.strip() for l in open(changed_override) if l.strip()]
elif base:
    changed = git("diff", "--name-only", base, "HEAD").split()
else:
    changed, _ = [], problems.append("no upstream base (no '[MSIL] Merge upstream' merge; set MSIL_UPSTREAM_BASE)")
hooked = sorted(p for p in changed if not p.startswith(FORK_PREFIXES) and p not in fork_only)
for p in hooked:
    blk = hooks.get(p)
    if blk is None:
        problems.append("unlisted hook: %s (changed since %s; add a 'hook' block)" % (p, base))
    elif not any(blk[k] for k in KINDS):
        problems.append("hook %s has no lit/gtest/compile/utc/none line" % p)
for p in sorted(set(hooks) - set(hooked)):
    problems.append("stale hook block: %s is not changed since %s" % (p, base))
for p in sorted(fork_only - set(changed)):
    problems.append("stale fork-only line: %s" % p)
for p, blk in hooks.items():
    for pat in blk["lit"]:
        if not glob.glob(os.path.join(repo, pat)):
            problems.append("hook %s: lit '%s' matches no file" % (p, pat))
    for f in blk["fork"]:
        if not os.path.exists(os.path.join(repo, f)):
            problems.append("hook %s: fork test '%s' does not exist" % (p, f))
sw = git("grep", "-l", "-E", r"case (llvm::)?Triple::XCOFF *:|case (llvm::)?MCContext::IsXCOFF *:",
         "--", "*.cpp", "*.h", "*.cc", "*.inc", "*.def").split()
for f in sw:
    text = open(os.path.join(repo, f), encoding="utf-8", errors="replace").read()
    if not re.search(r"Triple::MSIL\b|\bIsMSIL\b", text) and f not in switch_default:
        problems.append("switch coverage: %s switches over the object format without an MSIL case" % f)
print("base %s: %d changed, %d hooked, %d hook blocks, %d switch files" % (
    base, len(changed), len(hooked), len(hooks), len(sw)))
for p in problems:
    print("FAIL " + p)
sys.exit(1 if problems else 0)
