"""Turn inv/results.json into the markdown inventory table of the report."""
import json, re, sys

res = json.load(open(sys.argv[1]))
by = {}
for name, tk, opt, r in res:
    by.setdefault(name, {})[tk + opt] = r


def short_err(msgs):
    out = []
    for m in msgs:
        m = re.sub(r'^E:(<unknown>:0:0: in function \S+ [^:]+: )?', '', m)
        m = re.sub(r"runtime helper '([^']+)' is not available for MSIL yet \(phase 3\)", r'helper `\1` (p3 error)', m)
        m = re.sub(r"undefined function '([^']+)' is not defined in this module and is not a Phase-1 import.*", r'`\1`: not a Phase-1 import', m)
        m = re.sub(r"library call '([^']+)' is not a Phase-1 import", r'libcall `\1`: not a Phase-1 import', m)
        m = re.sub(r"floating-point type '([^']+)' is not supported for MSIL", r'type `\1` unsupported', m)
        m = re.sub(r'__float128 is not supported on this target', '`__float128` unsupported (front end)', m)
        if m.startswith('W:'):
            continue
        out.append(m)
    return '; '.join(dict.fromkeys(out))


def cellS(r):
    S = r['S']
    if S[0] == 'ok':
        calls = [re.sub(r"call \S+ '([^']+)'.*", r'\1', c) for c in S[2] if c.startswith('call')]
        loc = ' L' if any(c.startswith('localloc') for c in S[2]) else ''
        return f"{S[1]}{loc}" + (f" ({', '.join(calls)})" if calls else '')
    return 'ERR ' + short_err(S[3])


def cellC(r):
    c = r['c']
    if c[0] == 'ok':
        return ', '.join(c[1]) or '-'
    return 'ERR ' + short_err(c[2])


print('| Case | `-S` IL instrs: win -O0 / -O2, lin -O0 / -O2 (L = localloc) | `-c` undefined symbols (win / lin) | native x86_64 (win / lin) |')
print('|---|---|---|---|')
for name, d in by.items():
    s = [cellS(d[k]) for k in ('win-O0', 'win-O2', 'lin-O0', 'lin-O2')]
    if len(set(s)) == 1:
        scell = s[0] + ' (all four)'
    elif s[0] == s[2] and s[1] == s[3]:
        scell = f'{s[0]} / {s[1]} (both triples)'
    else:
        scell = ' / '.join(s)
    cw, cl = cellC(d['win-O2']), cellC(d['lin-O2'])
    c0w, c0l = cellC(d['win-O0']), cellC(d['lin-O0'])
    ccell = cw if (cw == cl and c0w == cw and c0l == cl) else f'{cw} / {cl}' + ('' if (c0w == cw and c0l == cl) else f' (-O0: {c0w} / {c0l})')
    nw, nl = ', '.join(d['win-O2']['native']) or '-', ', '.join(d['lin-O2']['native']) or '-'
    ncell = nw if nw == nl else f'{nw} / {nl}'
    print(f'| `{name}` | {scell} | {ccell} | {ncell} |')
