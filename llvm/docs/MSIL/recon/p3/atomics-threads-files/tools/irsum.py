import re, sys
# Summarize atomic IR per function: atomic ops and __atomic calls, compressed.
def summ(path):
    out = {}
    cur = None
    for line in open(path):
        m = re.match(r'define .*@(\w+)\(', line)
        if m: cur = m.group(1); out[cur] = []; continue
        if cur is None: continue
        if line.startswith('}'): cur = None; continue
        s = line.strip()
        r = None
        m = re.search(r'load atomic (volatile )?(\S+), ptr \S+ (\w+), align (\d+)', s)
        if m: r = f"load atomic {m.group(1) or ''}{m.group(2)} {m.group(3)} a{m.group(4)}"
        m = re.search(r'store atomic (volatile )?(\S+) \S+, ptr \S+ (\w+), align (\d+)', s)
        if m: r = f"store atomic {m.group(1) or ''}{m.group(2)} {m.group(3)} a{m.group(4)}"
        m = re.search(r'atomicrmw (volatile )?(\w+) ptr \S+, (\S+) \S+ (\w+)(, align (\d+))?', s)
        if m: r = f"atomicrmw {m.group(2)} {m.group(3)} {m.group(4)} a{m.group(6)}"
        m = re.search(r'cmpxchg (weak )?(volatile )?ptr \S+, (\S+) \S+, \S+ \S+ (\w+) (\w+)(, align (\d+))?', s)
        if m: r = f"cmpxchg {m.group(1) or ''}{m.group(3)} {m.group(4)}/{m.group(5)} a{m.group(7)}"
        m = re.search(r'fence (syncscope\("\w+"\) )?(\w+)', s)
        if m: r = f"fence {m.group(1) or ''}{m.group(2)}"
        m = re.search(r'call .*@(__atomic\w*|__sync\w*|__c11\w*)\(', s)
        if m: r = f"call {m.group(1)}"
        m = re.search(r'load volatile (\S+)', s)
        if m: r = f"load volatile {m.group(1)}"
        m = re.search(r'store volatile (\S+)', s)
        if m: r = f"store volatile {m.group(1)}"
        m = re.match(r'ret (\S+) (\S+)$', s)
        if m and re.match(r'^-?\d+$|^true$|^false$', m.group(2)): r = f"ret const {m.group(2)}"
        if r and r not in out[cur]: out[cur].append(r)
    return out
paths = sys.argv[1:]
sums = [summ(p) for p in paths]
names = list(sums[0].keys())
for n in names:
    rows = ['; '.join(s.get(n, ['?'])) or '-' for s in sums]
    if len(set(rows)) == 1:
        print(f"{n}: {rows[0]}")
    else:
        print(f"{n}:")
        for p, r in zip(paths, rows): print(f"    [{p.replace('.ll','')}] {r}")
