"""Run each candidate group through the private lit root; one xunit + log per group,
then a summary of PASS/FAIL/UNSUPPORTED per group and per failing test."""
import glob
import json
import os
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from groups import GROUPS  # noqa: E402

REPO = r"D:\llvm-msil"
LIT = os.path.join(HERE, "litroot", "bin", "llvm-lit.py")
OUT = os.path.join(HERE, "lit-groups")
os.makedirs(OUT, exist_ok=True)
jobs = sys.argv[1] if len(sys.argv) > 1 else "8"
only = sys.argv[2:] if len(sys.argv) > 2 else None

summary = []
for i, (name, pats) in enumerate(GROUPS):
    if only and str(i) not in only:
        continue
    paths = []
    for p in pats:
        full = os.path.join(REPO, p.replace("/", os.sep))
        hits = sorted(glob.glob(full)) if any(c in p for c in "*?[") else [full]
        paths += [h for h in hits if os.path.exists(h)]
    paths = sorted(set(paths))
    xml = os.path.join(OUT, "g%02d.xml" % i)
    log = os.path.join(OUT, "g%02d.log" % i)
    t0 = time.time()
    with open(log, "w", encoding="utf-8") as f:
        rc = subprocess.call([sys.executable, LIT, "-sv", "-j" + jobs, "--xunit-xml-output", xml] + paths,
                             stdout=f, stderr=subprocess.STDOUT)
    secs = time.time() - t0
    res = {"PASS": [], "FAIL": [], "UNSUPPORTED": [], "XFAIL": [], "OTHER": []}
    try:
        for tc in ET.parse(xml).getroot().iter("testcase"):
            tname = tc.get("classname", "") + "/" + tc.get("name", "")
            if tc.find("failure") is not None:
                res["FAIL"].append(tname)
            elif tc.find("skipped") is not None:
                res["UNSUPPORTED"].append(tname)
            else:
                res["PASS"].append(tname)
    except Exception as e:  # noqa: BLE001
        res["OTHER"].append("xunit parse error: %s" % e)
    summary.append({"group": i, "file": name, "rc": rc, "secs": round(secs, 1), "inputs": len(paths),
                    "pass": len(res["PASS"]), "fail": res["FAIL"], "unsupported": len(res["UNSUPPORTED"]),
                    "other": res["OTHER"]})
    print("g%02d %-55s rc=%d %5.1fs inputs=%d pass=%d fail=%d unsup=%d" % (
        i, name, rc, secs, len(paths), len(res["PASS"]), len(res["FAIL"]), len(res["UNSUPPORTED"])), flush=True)
    for t in res["FAIL"]:
        print("     FAIL", t, flush=True)
with open(os.path.join(OUT, "summary-%s.json" % time.strftime("%H%M%S")), "w") as f:
    json.dump(summary, f, indent=1)
