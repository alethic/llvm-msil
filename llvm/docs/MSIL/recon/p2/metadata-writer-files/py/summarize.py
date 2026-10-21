"""Summarizes mdump output: sizes, heaps, table rows, body header kinds."""
import os
import re
import sys

for dump in sys.argv[1:]:
    t = open(dump).read().splitlines()
    md = [l for l in t if l.startswith("COR.Metadata")][0].split()[1]
    text = [l for l in t if l.startswith("PE.Section .text")][0].split()[5]
    rows = {l.split()[1]: int(l.split()[4]) for l in t if l.startswith("MD.Table ")}
    heaps = {l.split()[1]: int(l.split()[3], 16) for l in t if l.startswith("MD.Heap")}
    bodies = [l for l in t if l.strip().startswith("Body ")]
    fat = sum(1 for b in bodies if " fat " in b)
    il = sum(int(l.split()[1]) for l in t if l.strip().startswith("IL "))
    withhdr = sum(int(re.search(r"size (\d+)", b).group(1)) for b in bodies)
    maxil = max((int(l.split()[1]) for l in t if l.strip().startswith("IL ")), default=0)
    print("%s: .text %s metadata %s IL code %d (with headers/EH %d, largest %d) bodies fat %d tiny %d"
          % (os.path.basename(dump), text, md, il, withhdr, maxil, fat, len(bodies) - fat))
    print("   heaps " + ", ".join("%s 0x%X" % kv for kv in heaps.items()))
    print("   rows  " + ", ".join("%s %d" % kv for kv in rows.items()))
