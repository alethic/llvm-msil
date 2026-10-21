"""Collect IR inputs for the EH-pass analysis: every .ll of llvm/test/CodeGen/MSIL
and msil/test (split-file parts extracted), plus IR that clang emits for every .c
of msil/test and clang/test/CodeGen/MSIL, per triple and level."""
import os, re, subprocess, sys, glob, concurrent.futures as cf
REPO = 'D:/llvm-msil'
BIN = REPO + '/build/bin'
OUT = sys.argv[1]
TRIPLES = ['x86_64-pc-windows-msvc-msil', 'x86_64-unknown-linux-gnu-msil']
LEVELS = ['O0', 'O2']
os.makedirs(OUT + '/ll', exist_ok=True)
os.makedirs(OUT + '/c', exist_ok=True)
lls = sorted(glob.glob(REPO + '/llvm/test/CodeGen/MSIL/**/*.ll', recursive=True) +
             glob.glob(REPO + '/msil/test/**/*.ll', recursive=True))
inputs = []  # (name, path, triple-or-None, level-or-None)
for f in lls:
    rel = os.path.relpath(f, REPO).replace(os.sep, '/')
    tag = rel.replace('/', '__')
    txt = open(f, encoding='utf-8', errors='replace').read()
    if re.search(r'^;--- ', txt, re.M):
        d = OUT + '/ll/' + tag + '.split'
        subprocess.run([BIN + '/split-file.exe', f, d], check=False)
        for p in sorted(glob.glob(d + '/**/*.ll', recursive=True)):
            inputs.append((rel + ':' + os.path.relpath(p, d), p, None, None))
    else:
        inputs.append((rel, f, None, None))
cs = sorted(glob.glob(REPO + '/msil/test/**/*.c', recursive=True) +
            glob.glob(REPO + '/clang/test/CodeGen/MSIL/**/*.c', recursive=True))
jobs = []
for f in cs:
    rel = os.path.relpath(f, REPO).replace(os.sep, '/')
    for t in TRIPLES:
        for l in LEVELS:
            o = OUT + '/c/' + rel.replace('/', '__') + '.' + t + '.' + l + '.ll'
            jobs.append((rel, f, t, l, o))
def emit(j):
    rel, f, t, l, o = j
    r = subprocess.run([BIN + '/clang.exe', '--target=' + t, '-ffreestanding', '-I',
                        REPO + '/msil/test/Inputs', '-' + l, '-S', '-emit-llvm', f, '-o', o],
                       capture_output=True)
    return (rel, o, t, l, r.returncode)
ok = 0
with cf.ThreadPoolExecutor(8) as ex:
    for rel, o, t, l, rc in ex.map(emit, jobs):
        if rc == 0:
            inputs.append((rel, o, t, l)); ok += 1
with open(OUT + '/inputs.txt', 'w') as w:
    for i in inputs:
        w.write('\t'.join(x or '-' for x in i) + '\n')
print('ll inputs', len(inputs) - ok, 'c->ir ok', ok, 'of', len(jobs))
