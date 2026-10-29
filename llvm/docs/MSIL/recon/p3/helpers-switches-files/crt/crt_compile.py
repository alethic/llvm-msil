"""Compile compiler-rt builtins sources for an MSIL triple and classify failures.

usage: python -I crt_compile.py <triple> <outdir> [extra clang args...]
Reads crt/lists.txt (kind + path relative to compiler-rt/lib/builtins).
"""
import os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = 'D:/llvm-msil'
SRC = ROOT + '/compiler-rt/lib/builtins'
CLANG = ROOT + '/build/bin/clang.exe'
triple, outdir = sys.argv[1], sys.argv[2]
extra = sys.argv[3:]
os.makedirs(outdir, exist_ok=True)
items = [l.split() for l in open('C:/llvm-msil-scratch/p3/helpers-switches/crt/lists.txt') if l.strip()]

def one(it):
    kind, path = it
    obj = os.path.join(outdir, path.replace('/', '_') + '.o')
    cmd = [CLANG, '--target=' + triple, '-c', '-O2', '-fno-builtin', '-std=c11',
           '-fvisibility=hidden', '-Wno-c2y-extensions', '-I', SRC,
           SRC + '/' + path, '-o', obj] + extra
    p = subprocess.run(cmd, capture_output=True, text=True)
    err = [l for l in p.stderr.splitlines() if 'error' in l]
    return kind, path, p.returncode, err

with ThreadPoolExecutor(4) as ex:
    res = list(ex.map(one, items))
ok = [r for r in res if r[2] == 0]
bad = [r for r in res if r[2] != 0]
print(f'{triple}: {len(ok)} ok, {len(bad)} failed of {len(res)}')
for kind, path, rc, err in bad:
    msg = err[0] if err else '(no error line)'
    msg = re.sub(r'^.*?builtins[\\/]', '', msg)
    print(f'FAIL {kind} {path}: {msg[:300]}')
    for e in err[1:3]:
        print('     ' + re.sub(r'^.*?builtins[\\/]', '', e)[:300])
