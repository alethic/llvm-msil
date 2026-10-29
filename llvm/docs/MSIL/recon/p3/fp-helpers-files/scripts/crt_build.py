"""Build a compiler-rt helper archive (the P3-B families only) for an MSIL triple or a native one.

usage: python -I crt_build.py <triple> <outdir> <archive-name> [extra clang args...]
Sources come from compiler-rt/lib/builtins, except the ones in crt/patched (udivmodti4.c without the
x86_64 divq asm under __MSIL__; truncsfbf2.c/truncdfbf2.c with a uint16_t bf16 result under __MSIL__).
"""
import os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = 'D:/llvm-msil'
SRC = ROOT + '/compiler-rt/lib/builtins'
PATCHED = 'C:/llvm-msil-scratch/p3/fp-helpers/crt/patched'
CLANG = ROOT + '/build/bin/clang.exe'
AR = ROOT + '/build/bin/llvm-ar.exe'
triple, outdir, arname = sys.argv[1], sys.argv[2], sys.argv[3]
extra = sys.argv[4:]
os.makedirs(outdir, exist_ok=True)
FAMILIES = {
    'f16': ['extendhfsf2.c', 'extendhfdf2.c', 'truncsfhf2.c', 'truncdfhf2.c'],
    'bf16': ['extendbfsf2.c', 'P:truncsfbf2.c', 'P:truncdfbf2.c'],
    'complex': ['mulsc3.c', 'muldc3.c', 'divsc3.c', 'divdc3.c'],
    'powi': ['powisf2.c', 'powidf2.c'],
    'i128': ['P:udivmodti4.c', 'udivti3.c', 'umodti3.c', 'divti3.c', 'modti3.c', 'divmodti4.c',
             'floattidf.c', 'floattisf.c', 'floatuntidf.c', 'floatuntisf.c', 'fixdfti.c', 'fixsfti.c',
             'fixunsdfti.c', 'fixunssfti.c', 'muloti4.c', 'multi3.c', 'clzti2.c'],
}
srcs = [s for fam in FAMILIES.values() for s in fam]

def one(s):
    path = PATCHED + '/' + s[2:] if s.startswith('P:') else SRC + '/' + s
    obj = os.path.join(outdir, os.path.basename(path) + '.o')
    cmd = [CLANG, '--target=' + triple, '-c', '-O2', '-fno-builtin', '-std=c11', '-fvisibility=hidden',
           '-ffreestanding', '-Wno-c2y-extensions', '-DCOMPILER_RT_HAS_FLOAT16', '-I', SRC, path,
           '-o', obj] + extra
    p = subprocess.run(cmd, capture_output=True, text=True)
    return s, p.returncode, obj, [l for l in p.stderr.splitlines() if 'error' in l][:2]

with ThreadPoolExecutor(4) as ex:
    res = list(ex.map(one, srcs))
objs = [r[2] for r in res if r[1] == 0]
for s, rc, obj, err in res:
    if rc:
        print('FAIL', s, err)
ar = os.path.join(outdir, arname)
if os.path.exists(ar):
    os.remove(ar)
subprocess.run([AR, 'rcs', ar] + objs, check=True)
print(f'{triple}: {len(objs)}/{len(srcs)} objects -> {ar} ({os.path.getsize(ar)} bytes)')
