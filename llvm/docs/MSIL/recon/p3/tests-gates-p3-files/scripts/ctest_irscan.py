"""Compile every c-testsuite single-exec test to LLVM IR for an MSIL triple (no backend) and
scan the IR for Phase 3 constructs."""
import os, re, sys, subprocess, json, collections
clang, tree, triple, out = sys.argv[1:5]
EXTRA = sys.argv[5:]
pats = {
  "dyn-alloca": re.compile(r"= alloca [^,\n]+, (i32|i64) %"),
  "stacksave": re.compile(r"llvm\.stack(save|restore)"),
  "va_start": re.compile(r"llvm\.va_start"),
  "vararg-def": re.compile(r"^define [^\n]*\.\.\.\)", re.M),
  "vararg-call": re.compile(r"call [^\n]*\(([^()]*)\.\.\.\)"),
  "thread_local": re.compile(r"thread_local"),
  "atomic": re.compile(r"\b(atomicrmw|cmpxchg|load atomic|store atomic)\b|fence "),
  "setjmp": re.compile(r"@_?setjmp|@longjmp|@_setjmpex"),
  "global_dtors": re.compile(r"llvm\.global_dtors"),
  "complex-libcall": re.compile(r"@__(mul|div)[sdx]c3"),
  "half": re.compile(r"\bhalf\b"),
  "fp128/x86_fp80": re.compile(r"\b(fp128|x86_fp80)\b"),
  "i128": re.compile(r"\bi128\b"),
  "indirectbr/blockaddress": re.compile(r"blockaddress|indirectbr"),
}
res = {}
names = sorted(f[:-2] for f in os.listdir(tree) if f.endswith(".c"))
for n in names:
    feats = {}
    for lvl in ("O0", "O2"):
        p = subprocess.run([clang, "--target=" + triple] + EXTRA + [ "-std=gnu17", "-w", "-" + lvl, "-S", "-emit-llvm",
                            "-o", "-", os.path.join(tree, n + ".c")], capture_output=True, text=True)
        if p.returncode != 0:
            feats["compile-error-" + lvl] = p.stderr.strip().splitlines()[-1][:120] if p.stderr.strip() else "?"
            continue
        ir = p.stdout
        for k, r in pats.items():
            if r.search(ir):
                feats.setdefault(k, []).append(lvl)
    res[n] = feats
json.dump(res, open(out, "w"), indent=1)
c = collections.Counter(k for f in res.values() for k in f)
print(dict(c))
for n, f in res.items():
    if f: print(n, f)
