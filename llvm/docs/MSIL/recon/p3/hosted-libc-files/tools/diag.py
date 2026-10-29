#!/usr/bin/env python3
"""Summarize the diagnostics of hl.py logs: diag.py <dir> [kind=obj|link-lto|link-asm|link-obj]"""
import re, sys, glob, os, collections

d = sys.argv[1]
kind = sys.argv[2] if len(sys.argv) > 2 else "obj"
msgs = collections.Counter()
files_failing = []
per_msg_files = collections.defaultdict(set)
for f in sorted(glob.glob(os.path.join(d, f"*.{kind}.log"))):
    stem = os.path.basename(f).split(".")[0]
    lines = open(f, encoding="utf-8", errors="replace").read().splitlines()[1:]
    rc = None
    for l in lines:
        m = re.match(r"\[rc (\d+)\]", l)
        if m:
            rc = int(m.group(1))
        if not re.search(r"\b(error|warning)\b:", l):
            continue
        if l.startswith("clang: error: ") and "command failed" in l:
            continue
        # normalize location and paths
        l2 = re.sub(r"^.*?([^/\\]+):\d+:\d+: ", r"\1: ", l)
        l2 = re.sub(r"<unknown>:0:0: in function (\S+) [^:]*: ", r"in function \1: ", l2)
        l2 = re.sub(r"C:/llvm-msil-scratch/\S+", "<path>", l2)
        msgs[l2] += 1
        per_msg_files[l2].add(stem)
    if rc:
        files_failing.append(stem)
print(f"failing ({len(files_failing)}): {' '.join(files_failing)}")
for m, c in sorted(msgs.items(), key=lambda x: (-x[1], x[0])):
    print(f"{c:5d}  {m}   [{','.join(sorted(per_msg_files[m]))[:120]}]")
