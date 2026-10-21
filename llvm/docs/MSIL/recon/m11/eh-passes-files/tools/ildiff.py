"""Compile every corpus input with the control llc (C0) and the variants (A, B) and
compare the IL text, the exit code and stderr."""
import os, subprocess, sys, hashlib, collections, concurrent.futures as cf
S = sys.argv[1]
INPUTS = sys.argv[2]
VARS = sys.argv[3].split(',')
TRIPLES = ['x86_64-pc-windows-msvc-msil', 'x86_64-unknown-linux-gnu-msil']
LEVELS = ['O0', 'O2']
OUT = S + '/ildiff'
os.makedirs(OUT, exist_ok=True)
runs = []
for line in open(INPUTS):
    name, path, t, l = line.rstrip('\n').split('\t')
    for tt in (TRIPLES if t == '-' else [t]):
        for ll in (LEVELS if l == '-' else [l]):
            runs.append((name, path, tt, ll))
def one(r):
    name, path, t, l = r
    res = {}
    for v in VARS:
        p = subprocess.run([S + '/build/' + v + '/bin/llc.exe', '-mtriple=' + t, '-' + l, path, '-o', '-'],
                           capture_output=True)
        res[v] = (p.returncode, hashlib.sha256(p.stdout).hexdigest(), p.stderr, p.stdout)
    return r, res
same = collections.Counter(); diff = collections.defaultdict(list); rcnz = 0
with cf.ThreadPoolExecutor(8) as ex:
    for r, res in ex.map(one, runs):
        base = res[VARS[0]]
        if base[0] != 0:
            rcnz += 1
        for v in VARS[1:]:
            x = res[v]
            if (x[0], x[1], x[2]) == (base[0], base[1], base[2]):
                same[v] += 1
            else:
                diff[v].append(r)
                tag = (r[0].replace('/', '__').replace(':', '_') + '.' + r[2] + '.' + r[3])
                for vv in (VARS[0], v):
                    open(OUT + '/' + tag + '.' + vv + '.il', 'wb').write(res[vv][3])
                    open(OUT + '/' + tag + '.' + vv + '.err', 'wb').write(res[vv][2])
print('runs', len(runs), '; control exit!=0 (diagnostic tests):', rcnz)
for v in VARS[1:]:
    print('variant %s: identical IL+exit+stderr in %d runs, different in %d' % (v, same[v], len(diff[v])))
    for r in diff[v][:200]:
        print('   DIFF', v, r[0], r[2], r[3])
