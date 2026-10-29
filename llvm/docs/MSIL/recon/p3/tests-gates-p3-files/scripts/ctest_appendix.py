"""Write the per-test table of c-testsuite's 70 excluded tests (appendix A of tests-gates-p3.md) from
ctest-classify.json and ctest-irscan-linux.json (this run) and the variadics recon's 8.4 prototype results."""
import json, sys
cls = {d["name"]: d for d in json.load(open(sys.argv[1]))}
irs = json.load(open(sys.argv[2]))
irs = irs.get("tests", irs) if isinstance(irs, dict) else irs
ir = {}
if isinstance(irs, dict):
    ir = irs
else:
    for d in irs: ir[d["name"]] = d
# variadics recon 8.4 (prototype of variadic calls, .NET 8, -O0/-O2, both triples)
NOTE = {
 "00025": ("pass (no variadics; empty expected)", "libc call `strlen`; binds since M15"),
 "00040": ("pass (no variadics; empty expected)", "libc call `calloc`; binds since M15"),
 "00204": ("linux-gnu: `exclude linux-gnu long-double`; windows: pass", "defines variadic functions, `va_start`; `%Lf` through glibc `vprintf` prints `nan` in 20 of 174 lines"),
 "00207": ("`diag` at -O0 until P3-A dynamic alloca (both triples); -O2 pass", "VLA (`dynamic alloca` + `stacksave` at -O0)"),
 "00212": ("windows: `native windows-msvc` (expected assumes LP64); linux pass", ""),
 "00216": ("windows: `native windows-msvc` (GCC bit-field layout); linux pass", ""),
 "00218": ("windows: `native windows-msvc`, and `diag` at -O0 until V10; linux pass", "declares `printf` itself; empty `.c.expected`"),
 "00210": ("windows: `diag` until V10 (UCRT exports no `printf`); linux pass", "declares `printf` itself"),
 "00211": ("windows: `diag` until V10; linux pass", "declares `printf` itself"),
 "00213": ("windows: `diag` until V10; linux pass", "declares `printf` itself"),
 "00215": ("windows: `diag` until V10; linux pass", "declares `printf` itself"),
 "00217": ("windows: `diag` until V10; linux pass", "declares `printf` itself"),
 "00214": ("windows: `exclude windows-msvc not-portable`; linux pass", "`typedef unsigned long size_t` (no native Windows build either)"),
 "00208": ("pass (empty `.c.expected`)", ""),
}
rows = [d for d in cls.values() if not d["free"]]
rows.sort(key=lambda d: d["name"])
out = ["| Test | Rule matches (why excluded today) | `.c.expected` lines | IR, linux-gnu (-O0/-O2) | Phase 3 entry (prototype result) | Note |",
       "|---|---|---|---|---|---|"]
for d in rows:
    n = d["name"]
    feats = ir.get(n, {})
    if isinstance(feats, dict):
        f = ", ".join("%s %s" % (k, "/".join(v)) for k, v in sorted(feats.items()) if k != "i128")
    else:
        f = ""
    st, note = NOTE.get(n, ("pass (both triples, equal to `.c.expected`)", ""))
    out.append("| %s | %s | %d | %s | %s | %s |" % (n, ", ".join("`%s`" % c for c in d["calls"]), d["expected_lines"], f or "none", st, note))
print("\n".join(out))
print("\n%d tests" % len(rows), file=sys.stderr)
