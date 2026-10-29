"""Classify c-testsuite single-exec tests: libc-free (per libc-free.txt rule) vs the others,
and for each the C library calls, the Phase 3 constructs and whether .c.expected is empty."""
import os, re, sys, json, collections
tree = sys.argv[1]
listfile = sys.argv[2]
rule = None; listed = {}
for line in open(listfile, encoding="utf-8"):
    if line.startswith("# rule: "): rule = line[len("# rule: "):].rstrip("\n")
    elif line.strip() and not line.startswith("#"):
        name = line.split()[0]; listed[name] = line.rstrip("\n")
rx = re.compile(rule)
tok = re.compile(r"\b(printf|fprintf|sprintf|snprintf|vprintf|malloc|calloc|realloc|free|strlen|strcmp|strncmp|strcpy|strncpy|strcat|memcmp|exit|abort|atoi|strtol|qsort|bsearch|getchar|fopen|setjmp|longjmp|va_start|alloca|assert)\s*\(")
# Phase 3 constructs (heuristics on the source text)
p3 = {
  "variadic-def": re.compile(r"^[^;#/]*\b\w+\s*\([^;{)]*\.\.\.\s*\)\s*\{", re.M),
  "variadic-decl-call": re.compile(r"\.\.\.\s*\)\s*;"),
  "va_*": re.compile(r"\bva_(start|arg|end|list|copy)\b"),
  "setjmp": re.compile(r"\b(setjmp|longjmp|sigsetjmp)\b"),
  "_Thread_local": re.compile(r"\b(_Thread_local|__thread|thread_local)\b"),
  "_Atomic": re.compile(r"\b(_Atomic|atomic_\w+|__atomic_\w+|__sync_\w+)\b"),
  "_Complex": re.compile(r"\b(_Complex|complex)\b"),
  "VLA?": re.compile(r"\[\s*[a-z_]\w*\s*\]\s*;"),
  "alloca": re.compile(r"\balloca\b"),
  "destructor": re.compile(r"destructor|atexit"),
  "long double": re.compile(r"long\s+double"),
  "_Float16/__fp16": re.compile(r"\b(_Float16|__fp16)\b"),
  "float/double": re.compile(r"\b(float|double)\b"),
}
names = sorted(f[:-2] for f in os.listdir(tree) if f.endswith(".c"))
rows = []
for n in names:
    src = open(os.path.join(tree, n + ".c"), encoding="latin-1").read()
    exp = open(os.path.join(tree, n + ".c.expected"), encoding="latin-1").read()
    calls = sorted(set(m.group(1) for m in tok.finditer(src)))
    free = rx.search(src) is None
    feats = [k for k, r in p3.items() if r.search(src)]
    rows.append(dict(name=n, free=free, listed=n in listed, calls=calls, expected_empty=(exp == ""),
                     expected_lines=exp.count("\n"), feats=feats, lines=src.count("\n"),
                     diag=("diag" in listed.get(n, ""))))
print("total", len(rows), "libc-free", sum(r["free"] for r in rows), "listed", sum(r["listed"] for r in rows))
bad = [r["name"] for r in rows if r["free"] != r["listed"]]
print("rule/list mismatch:", bad)
excl = [r for r in rows if not r["free"]]
c = collections.Counter(x for r in excl for x in r["calls"])
print("excluded:", len(excl), "calls among excluded:", dict(c))
print("excluded with empty .expected:", [r["name"] for r in excl if r["expected_empty"]])
print("excluded calling only printf:", sum(1 for r in excl if r["calls"] == ["printf"]))
print()
print("name | calls | expected lines | phase-3 constructs (heuristic)")
for r in excl:
    print(f'{r["name"]} | {",".join(r["calls"])} | {r["expected_lines"]} | {",".join(f for f in r["feats"] if f not in ("float/double",))}{" float" if "float/double" in r["feats"] else ""}')
print()
print("libc-free tests with phase-3 constructs (heuristic):")
for r in rows:
    if r["free"]:
        f = [x for x in r["feats"] if x not in ("float/double", "variadic-decl-call")]
        if f: print(" ", r["name"], f, "(diag)" if r["diag"] else "")
json.dump(rows, open(sys.argv[3], "w"), indent=1)
