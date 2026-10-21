"""Prototype (M11 recon, item 2): every 'uses: <owner>/<repo>[/<path>]@<ref>' in the
msil-*.yml workflows must use the ref and version comment that the upstream workflows
and actions in .github use for the same <owner>/<repo>; an action upstream never uses
is reported as 'not pinned upstream'."""
import collections, glob, os, re, sys

repo = sys.argv[1] if len(sys.argv) > 1 else "."
USES = re.compile(r"^\s*(?:-\s*)?uses:\s*([\w.-]+/[\w.-]+)(/[\w./-]+)?@(\S+)\s*(#.*)?$")
files = sorted(glob.glob(os.path.join(repo, ".github", "**", "*.y*ml"), recursive=True))
ours = [f for f in files if os.path.basename(f).startswith("msil-")]
upstream = collections.defaultdict(collections.Counter)
for f in files:
    if f in ours:
        continue
    for line in open(f, encoding="utf-8"):
        m = USES.match(line)
        if m:
            ver = (m.group(4) or "").lstrip("#").strip().split(" ")[0]
            upstream[m.group(1)][(m.group(3), ver)] += 1
bad = 0
for f in ours:
    for n, line in enumerate(open(f, encoding="utf-8"), 1):
        m = USES.match(line)
        if not m:
            continue
        action, ref = m.group(1), m.group(3)
        ver = (m.group(4) or "").lstrip("#").strip().split(" ")[0]
        seen = upstream.get(action)
        where = "%s:%d" % (os.path.relpath(f, repo).replace(os.sep, "/"), n)
        if not seen:
            print("%s: %s@%s: not pinned upstream" % (where, action, ref))
            continue
        if (ref, ver) not in seen:
            want = ", ".join("%s # %s (%d uses)" % (r, v, c) for (r, v), c in seen.most_common())
            print("%s: %s@%s # %s; upstream: %s" % (where, action, ref, ver, want))
            bad += 1
print("%d mismatched pin(s)" % bad)
sys.exit(1 if bad else 0)
