import re, sys, collections
# Count atomic IR operations by kind, type and ordering in .ll files.
for path in sys.argv[1:]:
    c = collections.Counter()
    for line in open(path, encoding='utf-8', errors='replace'):
        s = line.strip()
        m = re.search(r'load atomic (?:volatile )?(\S+), ptr \S+ (?:syncscope\("\w+"\) )?(\w+)', s)
        if m: c[f"load {m.group(1)} {m.group(2)}"] += 1; continue
        m = re.search(r'store atomic (?:volatile )?(\S+) \S+, ptr \S+ (?:syncscope\("\w+"\) )?(\w+)', s)
        if m: c[f"store {m.group(1)} {m.group(2)}"] += 1; continue
        m = re.search(r'atomicrmw (?:volatile )?(\w+) ptr \S+, (\S+) \S+ (?:syncscope\("\w+"\) )?(\w+)', s)
        if m: c[f"rmw {m.group(1)} {m.group(2)} {m.group(3)}"] += 1; continue
        m = re.search(r'cmpxchg (weak )?(?:volatile )?ptr \S+, (\S+) \S+, \S+ \S+ (?:syncscope\("\w+"\) )?(\w+) (\w+)', s)
        if m: c[f"cmpxchg{' weak' if m.group(1) else ''} {m.group(2)} {m.group(3)}/{m.group(4)}"] += 1; continue
        m = re.search(r'\bfence (syncscope\("\w+"\) )?(\w+)', s)
        if m: c[f"fence {m.group(1) or ''}{m.group(2)}"] += 1; continue
        m = re.search(r'call .*@(llvm\.x86\.sse2\.pause|__atomic\w+)\(', s)
        if m: c[f"call {m.group(1)}"] += 1
    print(f"== {path}: {sum(c.values())} atomic operations")
    for k, v in sorted(c.items(), key=lambda kv: -kv[1]): print(f"  {v:5d}  {k}")
