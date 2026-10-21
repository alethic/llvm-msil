"""WebAssembly counterpart of sweep-proto.py: llc -stop-after=wasm-isel (and
-stop-before=wasm-argument-move) over llvm/test/CodeGen/WebAssembly/*.ll on
wasm32, counting the files that fail only because of the end-of-pipeline
machine verifier (they pass with -disable-mir-output-verify)."""
import concurrent.futures as cf
import glob
import os
import re
import subprocess
import sys
from collections import Counter

LLC = r"D:\llvm-msil\build\bin\llc.exe"
files = sorted(glob.glob(r"D:\llvm-msil\llvm\test\CodeGen\WebAssembly\*.ll"))
stops = sys.argv[1].split(",")


def run(args):
    f, s = args
    base = [LLC, "-mtriple=wasm32-unknown-unknown", s, f, "-o", os.devnull]
    r = subprocess.run(base, capture_output=True, text=True, errors="replace")
    if r.returncode == 0:
        return (os.path.basename(f), s, "ok", [])
    r2 = subprocess.run(base + ["-disable-mir-output-verify"], capture_output=True,
                        text=True, errors="replace")
    if r2.returncode != 0:
        return (os.path.basename(f), s, "other-error", [])
    errs = re.findall(r"\*\*\* Bad machine code: (.*?) \*\*\*", r.stderr)
    return (os.path.basename(f), s, "verifier", errs[:1])


jobs = [(f, s) for f in files for s in stops]
res = []
with cf.ThreadPoolExecutor(int(os.environ.get("JOBS", "3"))) as ex:
    res = list(ex.map(run, jobs))
print("files", len(files), "jobs", len(jobs))
print(Counter((r[1], r[2]) for r in res))
print(Counter((r[1], e) for r in res for e in r[3]))
for r in res:
    if r[2] == "verifier":
        print(r[0], r[1], r[3])
