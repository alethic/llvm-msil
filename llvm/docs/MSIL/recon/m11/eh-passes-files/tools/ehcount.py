"""Run llc -debug-pass=Details over the corpus and attribute every 'Made Modification'
of LowerInvoke and UnreachableBlockElim to its instance (0-based, per function)."""
import re, subprocess, sys, collections, concurrent.futures as cf
LLC = sys.argv[1]
INPUTS = sys.argv[2]
TRIPLES = ['x86_64-pc-windows-msvc-msil', 'x86_64-unknown-linux-gnu-msil']
LEVELS = ['O0', 'O2']
LI = 'Lower invoke and unwind, for unwindless code generators'
UBE = 'Remove unreachable blocks from the CFG'
ex_re = re.compile(r"Executing Pass '(.*)' on Function '(.*)'\.\.\.$")
mm_re = re.compile(r"Made Modification '(.*)' on Function '(.*)'\.\.\.$")
runs = []
for line in open(INPUTS):
    name, path, t, l = line.rstrip('\n').split('\t')
    ts = TRIPLES if t == '-' else [t]
    ls = LEVELS if l == '-' else [l]
    for tt in ts:
        for ll in ls:
            runs.append((name, path, tt, ll))
def run(r):
    name, path, t, l = r
    p = subprocess.run([LLC, '-mtriple=' + t, '-' + l, '-debug-pass=Details', path, '-o', 'NUL'],
                       capture_output=True, text=True, errors='replace')
    cnt = collections.Counter(); mods = collections.Counter(); execs = collections.Counter()
    for line in p.stderr.splitlines():
        m = ex_re.search(line)
        if m and m.group(1) in (LI, UBE):
            cnt[(m.group(1), m.group(2))] += 1
            execs[(m.group(1), cnt[(m.group(1), m.group(2))] - 1)] += 1
            continue
        m = mm_re.search(line)
        if m and m.group(1) in (LI, UBE):
            mods[(m.group(1), cnt[(m.group(1), m.group(2))] - 1)] += 1
    return r, p.returncode, execs, mods
tot_exec = collections.Counter(); tot_mod = collections.Counter(); where = collections.defaultdict(list)
rcs = collections.Counter()
with cf.ThreadPoolExecutor(8) as ex:
    for r, rc, execs, mods in ex.map(run, runs):
        rcs[rc != 0] += 1
        tot_exec.update(execs); tot_mod.update(mods)
        for k in mods:
            where[k].append('%s [%s %s]' % (r[0], r[2], r[3]))
print('llc runs:', len(runs), ' exit!=0:', rcs[True])
for k in sorted(set(tot_exec) | set(tot_mod)):
    print('%-28s instance %d: executed on %6d functions, modified %4d' %
          ('LowerInvoke' if k[0] == LI else 'UnreachableBlockElim', k[1], tot_exec[k], tot_mod[k]))
for k in sorted(where):
    print('== modified by', 'LowerInvoke' if k[0] == LI else 'UnreachableBlockElim', 'instance', k[1], '(%d runs)' % len(where[k]))
    for w in sorted(set(where[k]))[:400]:
        print('   ', w)
