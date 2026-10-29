"""Summarize lit xunit XML: total test-seconds per suite and per directory, top tests."""
import sys, xml.etree.ElementTree as ET, collections
path = sys.argv[1]
depth = int(sys.argv[2]) if len(sys.argv) > 2 else 2
root = ET.parse(path).getroot()
print("testsuites time (wall):", root.get("time"))
by = collections.defaultdict(lambda: [0, 0.0])
tests = []
for ts in root.iter("testsuite"):
    for tc in ts.iter("testcase"):
        cls = tc.get("classname"); name = tc.get("name"); t = float(tc.get("time") or 0)
        skipped = tc.find("skipped") is not None
        parts = cls.split(".", 1)
        suite = parts[0]; rel = parts[1] if len(parts) > 1 else ""
        key = suite + ":" + "/".join(rel.split("/")[:depth])
        by[key][0] += 1; by[key][1] += t
        tests.append((t, cls + "/" + name, skipped))
tot = sum(v[1] for v in by.values())
print(f"sum of test seconds: {tot:.0f} ({tot/60:.1f} min); tests {len(tests)}")
for k, (n, t) in sorted(by.items(), key=lambda kv: -kv[1][1]):
    print(f"{t:9.0f} s {100*t/tot:5.1f}%  n={n:4d}  avg={t/max(n,1):6.1f}  {k}")
print("top 25 tests:")
for t, n, s in sorted(tests, reverse=True)[:25]:
    print(f"{t:8.1f}  {n}{' (skipped)' if s else ''}")
