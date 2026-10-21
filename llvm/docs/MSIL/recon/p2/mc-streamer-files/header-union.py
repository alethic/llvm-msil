import sys
deps_file, log_file = sys.argv[1], sys.argv[2]
sets = {}
for spec in sys.argv[3:]:
    name, hs = spec.split('=', 1)
    sets[name] = [h.lower() for h in hs.split(',')]
targets = {}; cur = None
for line in open(deps_file, encoding='utf-8', errors='replace'):
    if not line.startswith(' '):
        cur = line.split(': #deps')[0] if '#deps' in line else None
        if cur: targets[cur] = set()
    elif cur is not None:
        targets[cur].add(line.strip().replace(chr(92), '/').lower())
dur = {}
for line in open(log_file):
    if line.startswith('#'): continue
    p = line.rstrip('\n').split('\t')
    if len(p) >= 5: dur[p[3]] = int(p[1]) - int(p[0])
objs = [t for t in targets if t.endswith('.obj')]
tot = sum(dur.get(t, 0) for t in objs)
for name, hs in sets.items():
    hit = [t for t in objs if any(h in targets[t] for h in hs)]
    ms = sum(dur.get(t, 0) for t in hit)
    print(f'{name}: {len(hit)} of {len(objs)} objs, {ms/3.6e6:.2f} CPU-h = {100*ms/tot:.1f}% of compile CPU (~{47*ms/tot:.0f} min of a 47-min full rebuild, plus relinks)')
