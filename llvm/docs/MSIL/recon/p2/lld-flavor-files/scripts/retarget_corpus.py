"""Retarget experiment: compile every C program of msil/test/M1 with the
*stock* native triple (no -msil suffix) to bitcode ('-flto -c', i.e. after
clang's pre-link pipeline with the native X86 TTI), then compile that bitcode
with llc for the corresponding -msil triple, assemble with ilasm and run on
.NET 8, comparing stdout+exit status with the checked-in .expected file.

Usage: python retarget_corpus.py <native-triple> <opt> [extra clang flags...]
Results go to C:/llvm-msil-scratch/p2/lld-flavor/retarget/<triple>-<opt>/.
Windows triples are executed; Linux triples are compiled and assembled only.
"""
import os, subprocess, sys, glob, json, shutil, re, collections

SRC = 'D:/llvm-msil'
B = SRC + '/build/bin'
SCR = 'C:/llvm-msil-scratch/p2/lld-flavor'
ILASM = 'C:/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe'
triple = sys.argv[1]
opt = sys.argv[2]
extra = sys.argv[3:]
tag = os.environ.get('MODE', 'llc') + '-' + triple + '-' + opt + ('-' + '_'.join(x.strip('-') for x in extra) if extra else '')
out = f'{SCR}/retarget/{tag}'
os.makedirs(out, exist_ok=True)
rc_json = f'{SCR}/retarget/net8.runtimeconfig.json'
with open(rc_json, 'w') as f:
    json.dump({'runtimeOptions': {'tfm': 'net8.0', 'framework': {
        'name': 'Microsoft.NETCore.App', 'version': '8.0.0'}}}, f)

run_it = 'windows' in triple
progs = sorted(glob.glob(SRC + '/msil/test/M1/*/*.c'))
stats = collections.Counter()
fails = collections.defaultdict(list)
for c in progs:
    name = os.path.splitext(os.path.basename(c))[0]
    d = os.path.dirname(c)
    exp = os.path.join(d, name + '.expected')
    base = f'{out}/{os.path.basename(d)}__{name}'
    cc = [B + '/clang.exe', '--target=' + triple, '-ffreestanding', '-' + opt,
          '-flto', '-c', '-I' + SRC + '/msil/test/Inputs', c, '-o', base + '.bc'] + extra
    p = subprocess.run(cc, capture_output=True, text=True)
    if p.returncode:
        stats['clang-fail'] += 1
        fails['clang'].append((name, p.stderr.strip().splitlines()[-1:]))
        continue
    if os.environ.get('MODE') == 'ld':
        lib = 'C:/Windows/System32' if 'windows' in triple else ''
        cmd = [SCR + '/proto/bin/msil-ld.exe', '--lto-emit-asm', '--lto-O' + opt[1:], '-o', base + '.il', base + '.bc',
               '-mllvm', '-msil-assembly-name=' + name]
        if 'windows' in triple:
            cmd += ['--target=' + triple + '-msil', '-L', lib, '-lucrtbase']
        p = subprocess.run(cmd, capture_output=True, text=True)
    else:
        p = subprocess.run([B + '/llc.exe', '-mtriple=' + triple + '-msil', base + '.bc',
                            '-o', base + '.il'], capture_output=True, text=True)
    if p.returncode:
        stats['llc-fail'] += 1
        msg = [l for l in p.stderr.splitlines() if 'error' in l][:1]
        fails['llc'].append((name, msg))
        continue
    p = subprocess.run([ILASM, '-nologo', '-quiet', '-dll', '-pe64', '-x64',
                        '-output=' + base + '.dll', base + '.il'], capture_output=True, text=True)
    if p.returncode:
        stats['ilasm-fail'] += 1
        fails['ilasm'].append((name, p.stdout.strip().splitlines()[-2:]))
        continue
    if not run_it or not os.path.exists(exp):
        stats['assembled'] += 1
        continue
    rd = f'{base}.run'
    os.makedirs(rd, exist_ok=True)
    shutil.copy(base + '.dll', rd + '/' + name + '.dll')
    try:
        p = subprocess.run(['dotnet', 'exec', '--runtimeconfig', rc_json, rd + '/' + name + '.dll'],
                           capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        stats['timeout'] += 1
        fails['run'].append((name, ['timeout']))
        continue
    got = p.stdout.replace('\r\n', '\n')
    if not got.endswith('\n') and got:
        got += '\n'
    got += f'[exit {p.returncode & 0xff}]\n'
    want = open(exp).read().replace('\r\n', '\n')
    if not want.endswith('\n'):
        want += '\n'
    if got == want:
        stats['pass'] += 1
    else:
        stats['mismatch'] += 1
        fails['run'].append((name, [f'exit {p.returncode}']))
print(tag, dict(stats), 'of', len(progs))
for k, v in fails.items():
    for n, m in v:
        print(f'  {k}: {n}: {m}')
