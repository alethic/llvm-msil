"""Count the object files that depend on each header (ninja deps log dump),
and estimate their compile time from .ninja_log durations."""
import sys, re, collections
deps_file, log_file = sys.argv[1], sys.argv[2]
headers = [h.replace(chr(92), '/').lower() for h in sys.argv[3:]]
targets = {}
cur = None
with open(deps_file, encoding='utf-8', errors='replace') as f:
    for line in f:
        if not line.startswith(' '):
            if ':' in line and '#deps' in line:
                cur = line.split(': #deps')[0]
                targets[cur] = set()
            else:
                cur = None
        elif cur is not None:
            p = line.strip().replace(chr(92), '/').lower()
            if p:
                targets[cur].add(p)
dur = {}
with open(log_file) as f:
    for line in f:
        if line.startswith('#'): continue
        parts = line.rstrip('\n').split('\t')
        if len(parts) < 5: continue
        s, e, _, out = int(parts[0]), int(parts[1]), parts[2], parts[3]
        dur[out] = e - s
objs = [t for t in targets if t.endswith('.obj')]
total_ms = sum(dur.get(t, 0) for t in objs)
print(f"object files in deps log: {len(objs)}; summed compile time {total_ms/3600000:.2f} CPU-h")
def comp(t):
    m = re.match(r'(tools/clang/(lib|tools|unittests)/[^/]+|lib/[^/]+(/[^/]+)?|tools/[^/]+|unittests/[^/]+(/[^/]+)?|utils/[^/]+)', t)
    return m.group(1) if m else t.split('/')[0]
for h in headers:
    hit = [t for t in objs if h in targets[t]]
    ms = sum(dur.get(t, 0) for t in hit)
    c = collections.Counter()
    for t in hit:
        k = comp(t)
        k = re.sub(r'/CMakeFiles.*', '', k)
        c[k] += 1
    top = ', '.join(f'{k} {v}' for k, v in c.most_common(8))
    print(f"\n{h}\n  dependents: {len(hit)} objs ({100*len(hit)/len(objs):.1f}%), {ms/3600000:.2f} CPU-h (~{ms/60000/7:.0f} min at -j8 eff. 7)\n  top: {top}")
