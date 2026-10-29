# summarize2.py logs-bench.txt: two tables, min over the 3 in-process rounds (median in parentheses).
import re, sys, statistics, collections
data = collections.defaultdict(dict)
cur = None
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    m = re.match(r'=== (\S+) (\S+) (.*)', line.rstrip())
    if m:
        cur = (m.group(1), m.group(2), m.group(3)); continue
    m = re.match(r'(.*?)\s+([\d.]+) ns$', line.rstrip())
    if m and cur:
        data[cur].setdefault(m.group(1).strip(), []).append(float(m.group(2)))
def cell(cfgs, k):
    for c in cfgs:
        v = data.get(c, {}).get(k)
        if v: return f'{min(v):.1f} ({statistics.median(v):.1f})'
    return '-'
cases = []
for c in data:
    for k in data[c]:
        if k not in cases: cases.append(k)
for title, mode in [('optimized code: Framework default, CoreCLR DOTNET_TieredCompilation=0', 'TC=0'), ('default tiering (CoreCLR)', 'default')]:
    rts = ['fx', 'n8', 'n10', 'w8', 'w10'] if mode == 'TC=0' else ['n8', 'n10', 'w8', 'w10']
    print(f'\n{title}\n')
    print('| case (ns per call) | ' + ' | '.join(rts) + ' |')
    print('|---|' + '---|' * len(rts))
    for k in cases:
        row = []
        for rt in rts:
            m = 'default' if rt == 'fx' else mode
            row.append(cell([('bench', rt, m), ('benchuco', rt, m)], k))
        print(f'| {k} | ' + ' | '.join(row) + ' |')
