"""Compile lld TUs with the shared build's exact flags, but with every output
redirected to the scratch directory (the shared build tree is only read).
Usage: python time_lld_tus.py <dir-filter> [<dir-filter> ...] [-j N]
Generated inputs (Options.inc, VCSVersion.inc) are produced into scratch/gen.
"""
import json, os, re, subprocess, sys, time, concurrent.futures

BS = chr(92)
BUILD = 'D:/llvm-msil/build'
SRC = 'D:/llvm-msil'
SCR = 'C:/llvm-msil-scratch/p2/lld-flavor'
TBLGEN = BUILD + '/bin/llvm-tblgen.exe'

args = sys.argv[1:]
jobs = 4
if '-j' in args:
    i = args.index('-j')
    jobs = int(args[i + 1])
    del args[i:i + 2]
filters = args

os.makedirs(SCR + '/gen/Common', exist_ok=True)
with open(SCR + '/gen/Common/VCSVersion.inc', 'w') as f:
    f.write('// generated for timing only\n')
for flav in ('wasm', 'ELF', 'COFF', 'MachO', 'MinGW'):
    out = f'{SCR}/gen/{flav}/Options.inc'
    if not os.path.exists(out):
        os.makedirs(os.path.dirname(out), exist_ok=True)
        subprocess.check_call([TBLGEN, '-gen-opt-parser-defs', '-I', SRC + '/llvm/include',
                               f'{SRC}/lld/{flav}/Options.td', '-o', out])

cc = json.load(open(BUILD + '/compile_commands.json'))
work = []
for e in cc:
    f = e['file'].replace(BS, '/')
    if '/lld/' not in f:
        continue
    rel = f.split('D:/llvm-msil/lld/')[-1]
    if not any(rel.startswith(x + '/') for x in filters):
        continue
    cmd = e['command']
    flav = rel.split('/')[0]
    objdir = f'{SCR}/obj/{flav}'
    os.makedirs(objdir, exist_ok=True)
    obj = objdir + '/' + os.path.basename(f) + '.obj'
    cmd = re.sub(r'/Fo\S+', lambda m: '/Fo' + obj.replace('/', BS), cmd)
    cmd = re.sub(r'/Fd\S+', lambda m: '/Fd' + (objdir + '/x.pdb').replace('/', BS), cmd)
    cmd = cmd.replace(f'-I{BUILD}/tools/lld/{flav} ', f'-I{SCR}/gen/{flav} ')
    if flav == 'Common':
        cmd = cmd.replace('-ID:/llvm-msil/lld/include', f'-I{SCR}/gen/Common -ID:/llvm-msil/lld/include')
    work.append((rel, cmd))


def run(item):
    rel, cmd = item
    t0 = time.time()
    p = subprocess.run(cmd, cwd=SCR, capture_output=True, text=True)
    dt = time.time() - t0
    return rel, dt, p.returncode, (p.stdout + p.stderr)[-2000:]


t0 = time.time()
results = []
with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
    for r in ex.map(run, work):
        results.append(r)
        print(f'{r[1]:7.1f}s rc={r[2]} {r[0]}', flush=True)
        if r[2] != 0:
            print(r[3])
wall = time.time() - t0
tot = sum(r[1] for r in results)
print(f'TUs={len(results)} sum={tot:.1f}s wall={wall:.1f}s jobs={jobs}')
