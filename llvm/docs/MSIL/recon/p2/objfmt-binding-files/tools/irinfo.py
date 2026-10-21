#!/usr/bin/env python3
"""irinfo.py: read the link-relevant facts of an LLVM IR text module (clang -S -emit-llvm
output for an MSIL triple) and prepare a copy that the Phase-1 backend compiles.

  irinfo.py prep <in.ll> <out.p1.ll> <out.info.json>

The prepared copy turns every undefined function into a Phase-1 import from the reserved
module 'msil.unresolved' (the ImplMap stub shape), every external global variable into a
zero definition (so the backend prints the code shape; objectify.py removes it again), and
drops aliases (Phase 1 rejects them, D26). The info file lists every global value with its
linkage, comdat, size, alignment and flags, the constructors with priorities and the aliases.
"""
import json
import re
import sys

PRIM = {'i1': 1, 'i8': 1, 'i16': 2, 'i32': 4, 'i64': 8, 'i128': 16, 'ptr': 8,
        'float': 4, 'double': 8, 'half': 2}


NAMED = {}
D14 = set(['puts', 'putchar'] + [f + s for f in (
    'sqrt cbrt floor ceil trunc round rint nearbyint fmod remainder fma fmin fmax fdim sin cos tan asin acos '
    'atan atan2 sinh cosh tanh exp exp2 expm1 log log2 log10 log1p pow ldexp frexp modf hypot').split()
    for s in ('', 'f')])


def unescape(name):
    """IR quoted names escape bytes as backslash-XX; IL names are the raw UTF-8 text."""
    b = bytearray()
    i = 0
    raw = name.encode('utf-8')
    while i < len(raw):
        if raw[i] == 0x5c and i + 2 < len(raw) + 1 and re.match(rb'[0-9A-Fa-f]{2}', raw[i + 1:i + 3]):
            b.append(int(raw[i + 1:i + 3], 16))
            i += 3
        else:
            b.append(raw[i])
            i += 1
    return b.decode('utf-8', errors='surrogateescape')


def parse_type(s, i=0):
    """Returns (size, align, next index) of the IR type starting at s[i]."""
    while s[i] == ' ':
        i += 1
    if s[i] == '%':
        m = re.compile(r'%("[^"]+"|[\w.$]+)').match(s, i)
        sz, al = NAMED[m.group(1)]
        return sz, al, m.end()
    if s[i] == '[':
        m = re.compile(r'\[(\d+) x ').match(s, i)
        n = int(m.group(1))
        sz, al, j = parse_type(s, m.end())
        assert s[j] == ']'
        return n * sz, al, j + 1
    if s[i] == '<' and s[i + 1] == '{':
        sz, al, j = parse_struct(s, i + 1, packed=True)
        return sz, al, j + 1
    if s[i] == '{':
        return parse_struct(s, i, packed=False)
    m = re.compile(r'(i\d+|ptr|float|double|half)').match(s, i)
    t = m.group(1)
    return PRIM[t], PRIM[t], m.end()


def parse_struct(s, i, packed):
    assert s[i] == '{'
    i += 1
    off, al = 0, 1
    while True:
        while s[i] == ' ':
            i += 1
        if s[i] == '}':
            i += 1
            break
        sz, a, i = parse_type(s, i)
        if packed:
            a = 1
        off = (off + a - 1) // a * a + sz
        al = max(al, a)
        while s[i] == ' ':
            i += 1
        if s[i] == ',':
            i += 1
    return (off + al - 1) // al * al, al, i


def info(ll):
    pending = {}
    for line in ll.splitlines():
        m = re.match(r'%("[^"]+"|[\w.$]+) = type (<?\{.*\}>?)\s*$', line)
        if m:
            pending[m.group(1)] = m.group(2)
    while pending:
        done = []
        for n, t in pending.items():
            try:
                NAMED[n] = parse_type(t)[:2]
                done.append(n)
            except KeyError:
                pass
        if not done:
            raise Exception('unresolvable struct types: %s' % list(pending))
        for n in done:
            del pending[n]
    g = {}
    ctors = []
    aliases = {}
    for line in ll.splitlines():
        m = re.match(r'@("(?:[^"]|\\")*"|[\w.$]+) = (.*)$', line)
        if m:
            name = unescape(m.group(1).strip('"'))
            rest = m.group(2)
            if name == 'llvm.global_ctors':
                for prio, fn in re.findall(r'\{ i32 (\d+), ptr @("[^"]+"|[\w.$]+), ptr [^}]*\}', rest):
                    ctors.append((unescape(fn.strip('"')), int(prio)))
                continue
            if name.startswith('llvm.'):
                continue
            am = re.match(r'(?:(\w+) )?(?:dso_local )?(?:unnamed_addr )?alias .*?, ptr @("[^"]+"|[\w.$]+)', rest)
            if am:
                aliases[name] = unescape(am.group(2).strip('"'))
                continue
            lm = re.match(r'((?:private|internal|external|common|weak|weak_odr|linkonce|linkonce_odr|'
                          r'available_externally|extern_weak) )?((?:dso_local |hidden |protected |'
                          r'local_unnamed_addr |unnamed_addr |thread_local |externally_initialized )*)'
                          r'(global|constant) ', rest)
            linkage = (lm.group(1) or 'external ').strip()
            flags = lm.group(2)
            body = rest[lm.end():]
            sz, al, j = parse_type(body)
            am = re.search(r', align (\d+)', body)
            align = int(am.group(1)) if am else al
            undefined = (lm.group(1) or '').strip() in ('external', 'extern_weak')
            init = body[j:].split(', align')[0].split(', comdat')[0].strip()
            g[name] = dict(kind='data', linkage=linkage, size=sz, align=align,
                           comdat=('comdat' in body), undefined=undefined,
                           constant=(lm.group(3) == 'constant'), unnamed_addr=('unnamed_addr' in flags
                                                                               and 'local_unnamed_addr' not in flags),
                           visibility=('hidden' if 'hidden' in flags else 'default'),
                           tls=('thread_local' in flags),
                           cstring=init.startswith('c"') and init.endswith('\\00"'))
            continue
        m = re.match(r'(define|declare) (.*?)@("(?:[^"]|\\")*"|[\w.$]+)\(', line)
        if m:
            name = unescape(m.group(3).strip('"'))
            pre = m.group(2).split()
            linkage = 'external'
            for w in pre:
                if w in ('private', 'internal', 'weak', 'weak_odr', 'linkonce', 'linkonce_odr',
                         'extern_weak', 'available_externally'):
                    linkage = w
            g[name] = dict(kind='func', linkage=linkage, undefined=(m.group(1) == 'declare'),
                           comdat=(' comdat' in line), visibility=('hidden' if 'hidden' in pre else 'default'))
    return dict(globals=g, ctors=ctors, aliases=aliases)


def bind_of(gv):
    l = gv['linkage']
    if l in ('private', 'internal'):
        return 'local'
    if l == 'common':
        return 'common'
    if l in ('weak', 'weak_odr', 'linkonce', 'linkonce_odr', 'extern_weak'):
        return 'weak'
    return 'global'


def prep(src, dst, infof):
    ll = open(src, encoding='utf-8').read()
    inf = info(ll)
    out = []
    for line in ll.splitlines():
        m = re.match(r'declare (extern_weak )?(.*?)@("(?:[^"]|\\")*"|[\w.$]+)\((.*)\)(.*)$', line)
        if m and not m.group(3).startswith('llvm.') and m.group(3).strip('"') not in D14:
            line = 'declare %s%s@%s(%s) #990' % (m.group(1) or '', m.group(2), m.group(3), m.group(4))
        m = re.match(r'@("(?:[^"]|\\")*"|[\w.$]+) = (?:dso_local )?alias ', line)
        if m:
            line = '; alias removed for the Phase-1 backend: ' + line
        m = re.match(r'@("(?:[^"]|\\")*"|[\w.$]+) = (external|extern_weak) ((?:\w+ )*)global (.*?)(, align \d+)?$', line)
        if m:
            nm = m.group(1).strip('"')
            line = '@%s = %sglobal %s zeroinitializer%s ; WAS external' % (
                m.group(1), m.group(3), m.group(4), m.group(5) or '')
        out.append(line)
    out.append('attributes #990 = { "msil-import-module"="msil.unresolved" }')
    open(dst, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    json.dump(inf, open(infof, 'w'), indent=1)


if __name__ == '__main__':
    if sys.argv[1] == 'prep':
        prep(*sys.argv[2:5])
    else:
        print(json.dumps(info(open(sys.argv[2]).read()), indent=1))
