"""Sweep llc stop points over llvm/test/CodeGen/MSIL/*.ll.

usage: [LLC=<llc.exe>] [TRIPLES=a,b] python sweep-proto.py x <stop-opts,...> <opt-levels,...>

A job counts as a verifier failure when llc fails and the same command with
-disable-mir-output-verify succeeds (so error tests are ignored).
"""
import concurrent.futures as cf
import glob
import itertools
import os
import re
import subprocess
import sys
from collections import Counter

LLC = os.environ.get("LLC", r"D:\llvm-msil\build\bin\llc.exe")
files = sorted(glob.glob(r"D:\llvm-msil\llvm\test\CodeGen\MSIL\*.ll"))
triples = os.environ.get(
    "TRIPLES", "x86_64-unknown-linux-gnu-msil,x86_64-pc-windows-msvc-msil"
).split(",")
stops = sys.argv[2].split(",")
opts = sys.argv[3].split(",")


def run(args):
    f, t, s, o = args
    base = [LLC, "-mtriple=" + t, o, s, f, "-o", os.devnull]
    r = subprocess.run(base, capture_output=True, text=True, errors="replace")
    if r.returncode == 0:
        return None
    r2 = subprocess.run(base + ["-disable-mir-output-verify"], capture_output=True,
                        text=True, errors="replace")
    if r2.returncode != 0:
        return None
    errs = re.findall(
        r"\*\*\* Bad machine code: (.*?) \*\*\*\n- function:\s+(\S+)(?:.*?\n- instruction: (.*?)\n)?",
        r.stderr, re.S)
    return (os.path.basename(f), t, s, o, errs[:3])


jobs = list(itertools.product(files, triples, stops, opts))
out = []
with cf.ThreadPoolExecutor(int(os.environ.get("JOBS", "6"))) as ex:
    for res in ex.map(run, jobs):
        if res:
            out.append(res)
print("llc", LLC)
print("jobs", len(jobs), "failures", len(out))
print(Counter((r[2], r[3]) for r in out))
print(Counter(e[0] for r in out for e in r[4]))
for r in out:
    print(r[0], r[1], r[2], r[3],
          " | ".join("%s @%s: %s" % (e[0], e[1], e[2].strip()) for e in r[4]))
