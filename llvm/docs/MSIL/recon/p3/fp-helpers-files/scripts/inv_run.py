"""Compile every inventory case for both MSIL triples (and the native twins) at -O0/-O2.

usage: python -I inv_run.py <outdir>
Writes <outdir>/<case>/... and prints a TSV summary on stdout.
"""
import os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from inv_cases import C

BIN = 'D:/llvm-msil/build/bin/'
CLANG = BIN + 'clang.exe'
NM = BIN + 'llvm-nm.exe'
out = sys.argv[1]
TRIPLES = {'win': 'x86_64-pc-windows-msvc-msil', 'lin': 'x86_64-unknown-linux-gnu-msil'}
NATIVE = {'win': 'x86_64-pc-windows-msvc', 'lin': 'x86_64-unknown-linux-gnu'}
SKIPNM = re.compile(r'^(_fltused|__security_cookie|__chkstk)$')

def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stderr

def diag(stderr):
    msgs = []
    for l in stderr.splitlines():
        m = re.search(r'(error|warning): (.*)$', l)
        if m and 'overriding the module target triple' not in l and 'argument unused' not in l:
            msgs.append(m.group(1)[0].upper() + ':' + m.group(2).strip())
    # unique, keep order
    seen = []
    for x in msgs:
        if x not in seen:
            seen.append(x)
    return seen

def ilcount(path):
    txt = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r"\.method [^\n]*?'?\bf\b'?\(.*?\n(.*?)\} // end of method", txt, re.S)
    if not m:
        return None, []
    body = m.group(1)
    lines = [l.strip() for l in body.split('\n')]
    ins = [l for l in lines if l and not l.startswith(('.', '//', '{', '}')) and not re.match(r"^'?@?[\w.$]+'?:$", l)]
    calls = sorted(set(re.sub(r'\s+', ' ', x) for x in ins if x.startswith(('call ', 'localloc'))))
    return len(ins), calls

def nmu(path):
    p = subprocess.run([NM, '-u', path], capture_output=True, text=True)
    syms = [l.split()[-1] for l in p.stdout.splitlines() if l.strip()]
    return [s for s in syms if not SKIPNM.match(s)]

def irCalls(path):
    txt = open(path, encoding='utf-8', errors='replace').read()
    return sorted(set(re.findall(r'call [^@\n]*@([\w.$"]+)\(', txt)))

def one(job):
    name, tk, opt = job
    src, flags, kind = C[name]
    d = os.path.join(out, name)
    os.makedirs(d, exist_ok=True)
    ext = '.c' if kind == 'c' else '.ll'
    sp = os.path.join(d, 'src' + ext)
    if not os.path.exists(sp):
        open(sp, 'w').write(src)
    base = [CLANG, '--target=' + TRIPLES[tk], opt, '-w' if False else '-Wno-unused-command-line-argument'] + flags
    if kind == 'll':
        base += ['-x', 'ir']
    res = {}
    stem = os.path.join(d, f'{tk}{opt}')
    # IR as clang emits it (frontend calls), only for C
    if kind == 'c':
        rc, err = run(base + ['-S', '-emit-llvm', sp, '-o', stem + '.ll'])
        res['ir'] = irCalls(stem + '.ll') if rc == 0 else ['<fe-error>'] + diag(err)
    rc, err = run(base + ['-S', sp, '-o', stem + '.il'])
    if rc == 0:
        n, calls = ilcount(stem + '.il')
        res['S'] = ('ok', n, calls, diag(err))
    else:
        res['S'] = ('err', None, [], diag(err))
    rc, err = run(base + ['-c', sp, '-o', stem + '.o'])
    if rc == 0:
        res['c'] = ('ok', nmu(stem + '.o'), diag(err))
    else:
        res['c'] = ('err', [], diag(err))
    # native twin
    nb = [CLANG, '--target=' + NATIVE[tk], opt, '-Wno-unused-command-line-argument'] + flags
    if kind == 'll':
        nb += ['-x', 'ir']
    rc, err = run(nb + ['-c', sp, '-o', stem + '.native.o'])
    res['native'] = nmu(stem + '.native.o') if rc == 0 else ['<err>'] + diag(err)[:1]
    return name, tk, opt, res

jobs = [(n, t, o) for n in C for t in TRIPLES for o in ('-O0', '-O2')]
with ThreadPoolExecutor(4) as ex:
    results = list(ex.map(one, jobs))
import json
json.dump(results, open(os.path.join(out, 'results.json'), 'w'), indent=1)
for name, tk, opt, r in results:
    S = r['S']
    cc = r['c']
    s_txt = f"IL {S[1]} {';'.join(S[2])}" if S[0] == 'ok' else 'ERR ' + ' | '.join(S[3])
    if S[0] == 'ok' and S[3]:
        s_txt += ' [' + ' | '.join(S[3]) + ']'
    c_txt = ('undef=' + ','.join(cc[1])) if cc[0] == 'ok' else 'ERR ' + ' | '.join(cc[2])
    print(f"{name}\t{tk}{opt}\tir={','.join(r.get('ir', []))}\t-S: {s_txt}\t-c: {c_txt}\tnative={','.join(r['native'])}")
