"""Verifies the II.22 table schema and the II.24.2.6 index-size rules of
cliwriter.py against System.Reflection.Metadata: for every table present in
an assembly, the row size computed from the row counts and HeapSizes must
equal the row size SRM reports (mdump's 'MD.Table ... rowsize N').
Usage: verify_schema.py <mdump output>..."""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliwriter import SCHEMA, CODED  # noqa: E402

SRM_NAME = {"FieldRva": "FieldRVA", "MethodSpec": "MethodSpec", "AssemblyRefOS": "AssemblyRefOS",
            "EncLog": "ENCLog", "EncMap": "ENCMap", "AssemblyOS": "AssemblyOS"}
ok = True
tables_seen = set()
for path in sys.argv[1:]:
    rows, sizes, heap = {}, {}, None
    for l in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"MD\.Table (\w+) \(0x[0-9A-F]+\) rows (\d+) rowsize (\d+)", l)
        if m:
            name = SRM_NAME.get(m.group(1), m.group(1))
            rows[name], sizes[name] = int(m.group(2)), int(m.group(3))
        m = re.match(r"MD\.Tables .* HeapSizes 0x([0-9A-F]+)", l)
        if m:
            heap = int(m.group(1), 16)
    hs = {"S": 4 if heap & 1 else 2, "G": 4 if heap & 2 else 2, "B": 4 if heap & 4 else 2}

    def simple(t):
        return 2 if rows.get(t, 0) < (1 << 16) else 4

    def coded(c):
        bits, ts = CODED[c]
        return 2 if max(rows.get(t, 0) for t in ts if t) < (1 << (16 - bits)) else 4

    bad = []
    for t, srm in sizes.items():
        if t not in SCHEMA:
            bad.append((t, "not in schema"))
            continue
        tables_seen.add(t)
        n = 0
        for k in SCHEMA[t]:
            if k in (1, 2):
                n += 2
            elif k == 4:
                n += 4
            elif k in ("S", "G", "B"):
                n += hs[k]
            elif k[0] == "T":
                n += simple(k[1])
            else:
                n += coded(k[1])
        if n != srm:
            bad.append((t, n, srm))
    print("%s: %d tables, HeapSizes 0x%02X, %s" % (os.path.basename(path), len(sizes), heap,
                                                    "all row sizes match SRM" if not bad else bad))
    ok &= not bad
print("tables covered across inputs: %d of %d: %s" % (len(tables_seen), len(SCHEMA), sorted(tables_seen)))
print("RESULT:", "PASS" if ok else "FAIL")
