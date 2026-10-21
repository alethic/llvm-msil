"""Run the curated list in ONE lit invocation (as the gate would) and report counts and wall time."""
import glob
import os
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from curated import CURATED  # noqa: E402

REPO = r"D:\llvm-msil"
LIT = os.path.join(HERE, "litroot", "bin", "llvm-lit.py")
jobs = sys.argv[1] if len(sys.argv) > 1 else "8"
paths, owner = [], {}
for hook, pats in CURATED.items():
    for p in pats:
        full = os.path.join(REPO, p.replace("/", os.sep))
        hits = sorted(glob.glob(full)) if any(c in p for c in "*?[") else [full]
        if not hits or not all(os.path.exists(h) for h in hits):
            print("MISSING", p)
        for h in hits:
            owner.setdefault(os.path.normcase(h), []).append(hook)
            paths.append(h)
paths = sorted(set(paths))
with open(os.path.join(HERE, "curated-paths.txt"), "w") as f:
    f.write("\n".join(os.path.relpath(p, REPO).replace(os.sep, "/") for p in paths) + "\n")
xml = os.path.join(HERE, "curated.xml")
log = os.path.join(HERE, "curated.log")
t0 = time.time()
with open(log, "w", encoding="utf-8") as f:
    rc = subprocess.call([sys.executable, LIT, "-sv", "-j" + jobs, "--xunit-xml-output", xml] + paths,
                         stdout=f, stderr=subprocess.STDOUT)
wall = time.time() - t0
n = {"pass": 0, "fail": [], "unsup": []}
for tc in ET.parse(xml).getroot().iter("testcase"):
    name = tc.get("classname", "") + "/" + tc.get("name", "")
    if tc.find("failure") is not None:
        n["fail"].append(name)
    elif tc.find("skipped") is not None:
        n["unsup"].append(name + " (" + tc.find("skipped").get("message", "") + ")")
    else:
        n["pass"] += 1
print("inputs=%d rc=%d wall=%.1fs pass=%d fail=%d unsupported=%d" % (
    len(paths), rc, wall, n["pass"], len(n["fail"]), len(n["unsup"])))
for t in n["fail"]:
    print("  FAIL", t)
for t in n["unsup"]:
    print("  UNSUPPORTED", t)
