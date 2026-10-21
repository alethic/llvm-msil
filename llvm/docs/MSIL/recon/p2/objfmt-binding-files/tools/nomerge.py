#!/usr/bin/env python3
"""nomerge.py: option 4 ("no merge") prototype. Every object stays its own assembly; nothing in an
object changes at link time. The link product is
  * msil.extern.dll, a facade whose ExportedType forwarders send every global symbol's type
    '<sym>NAME' to the assembly whose definition won (strong over weak, first COMDAT copy,
    largest common), and
  * the program assembly (driver): it defines what no object defines (unresolved weak symbols),
    and its Main runs the objects' initializers in two phases (allocate segments and set address
    fields, then apply data relocations), the constructors in global priority order, then main.

Compile-time shape (what the backend would emit for option 4), derived here from an option-2
object's ILAsm and link info:
  * each global symbol NAME is a public class '<sym>NAME' in the object's assembly, holding the
    function 'NAME' (or the address field 'NAME') and the function-address field '<msil>fn';
  * every reference to a global symbol, even to one this object defines, goes through
    [msil.extern]'<sym>NAME' so the facade can pick the winning definition;
  * TU-local symbols live in the private class '<msil>OBJ'.
usage: nomerge.py <outdir> <program name> <obj.o.il> <obj.link.json> [<obj.o.il> <obj.link.json> ...]
"""
import json
import os
import re
import sys

QN = r"('(?:[^'\\]|\\.)*')"
MSCORLIB = '.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }'
outdir, prog = sys.argv[1], sys.argv[2]
pairs = list(zip(sys.argv[3::2], sys.argv[4::2]))


def unq(s):
    return s[1:-1] if s.startswith("'") else s


objs = []
for ilf, lf in pairs:
    text = open(ilf, encoding='utf-8-sig').read().splitlines()
    link = json.load(open(lf, encoding='utf-8'))
    asm = link['object'].replace('.o', '')
    methods, stubs, i = [], [], 0
    while i < len(text):
        l = text[i]
        if l.startswith('.method') and 'pinvokeimpl(' in l:
            stubs.append(l)
        elif l.startswith('.method'):
            j = i
            while not text[j].startswith('} // end of method'):
                j += 1
            methods.append(text[i:j + 1])
            i = j
        i += 1
    objs.append(dict(asm=asm, link=link, methods=methods, stubs=stubs,
                     sym={s['name']: s for s in link['symbols']}))

# ---- symbol resolution (the same rules as msilobj's Linker.AddDefs) ----------------------------
winner = {}
for o in objs:
    for s in o['link']['symbols']:
        if s['bind'] == 'local':
            continue
        n = s['name']
        if n not in winner:
            winner[n] = (o, s)
            continue
        eo, es = winner[n]
        if es['bind'] == 'common' and s['bind'] != 'common':
            winner[n] = (o, s)
        elif es['bind'] == 'weak' and s['bind'] == 'global':
            winner[n] = (o, s)
        elif es['bind'] == 'common' and s['bind'] == 'common' and s['size'] > es['size']:
            winner[n] = (o, s)
    for a, t in o['link']['aliases'].items():
        winner[a] = (o, dict(name=a, kind=o['sym'][t]['kind'], bind='global', alias=t))


def provider(n):
    return winner[n][0]['asm'] if n in winner else prog


def gtype(n):
    return "[msil.extern]'<sym>%s'" % n


# ---- per-object assemblies ---------------------------------------------------------------------
allrefs = set()
undef_stub = {}
IMPORTS = {'puts': 'ucrtbase', 'putchar': 'ucrtbase'}   # msil-ld: import libraries / .so export tables
for o in objs:
    asm, link, sym = o['asm'], o['link'], o['sym']
    stubmap = {}
    for s in o['stubs']:
        n = unq(re.search(QN + r"\(", s).group(1))
        stubmap[n] = 'msil.unresolved' in s
        if 'msil.unresolved' in s:
            undef_stub[n] = s
    loc = "'<msil>%s'" % asm
    out = [MSCORLIB, ".assembly extern 'msil.extern' { .ver 0:0:0:0 }", ".assembly '%s' { .ver 0:0:0:0 }" % asm]

    def rw(l):
        s = l.strip()
        m = re.match(r"(ldsfld|stsfld|ldsflda) native int \[msil\.extern\]'<msil>(Symbols|FnAddr)'::" + QN + "$", s)
        if m:
            n = unq(m.group(3))
            allrefs.add(n)
            fld = "'<msil>fn'" if m.group(2) == 'FnAddr' else m.group(3)
            return l.replace(m.group(0), "%s native int %s::%s" % (m.group(1), gtype(n), fld))
        m = re.match(r"(ldsfld|stsfld|ldsflda) native int " + QN + "$", s)
        if m:
            n = unq(m.group(2))
            if sym.get(n, {}).get('bind') == 'local':
                return l.replace(m.group(0), "%s native int %s::%s" % (m.group(1), loc, m.group(2)))
            allrefs.add(n)
            return l.replace(m.group(0), "%s native int %s::%s" % (m.group(1), gtype(n), m.group(2)))
        m = re.match(r"(call|ldftn) (.+?) " + QN + r"\((.*)\)$", s)
        if m:
            n = unq(m.group(3))
            if sym.get(n, {}).get('bind') == 'local' or stubmap.get(n) is False:
                return l.replace(m.group(3) + '(', "%s::%s(" % (loc, m.group(3)), 1)
            allrefs.add(n)
            return l.replace(m.group(3) + '(', "%s::%s(" % (gtype(n), m.group(3)), 1)
        m = re.match(r"(call|ldftn) (.+?) \[msil\.extern\]'<msil>Symbols'::" + QN + r"\((.*)\)$", s)
        if m:
            n = unq(m.group(3))
            allrefs.add(n)
            return l.replace("[msil.extern]'<msil>Symbols'::", gtype(n) + '::', 1)
        return l

    sigs = {}
    for m in o['methods']:
        hdr = m[0]
        n = unq(re.search(QN + r"\(", hdr).group(1))
        sigs[n] = hdr
        body = [rw(x) for x in m[1:]]
        if sym[n]['bind'] == 'local':
            o.setdefault('localm', []).append([hdr.replace('privatescope', 'public')] + body)
        else:
            out.append(".class public abstract sealed auto ansi '<sym>%s' extends [mscorlib]System.Object\n{" % n)
            out.append("  .field public static native int '<msil>fn'")
            out.append(hdr)
            out.extend(body)
            out.append('}')
    for a, t in link['aliases'].items():   # an alias is a type whose method jumps to the target
        hdr = sigs[t].replace("'%s'(" % t, "'%s'(" % a)
        params = re.search(r"\((.*)\) cil managed", hdr).group(1)
        ret = re.match(r"\.method public static (.+?) '", hdr).group(1)
        out.append(".class public abstract sealed auto ansi '<sym>%s' extends [mscorlib]System.Object\n{" % a)
        out.append("  .field public static native int '<msil>fn'")
        out.append(hdr + "\n{\n  jmp %s %s::'%s'(%s)\n}" % (ret, gtype(t), t, params))
        allrefs.add(t)
        out.append('}')
    for s in link['symbols']:
        if s['kind'] == 'data' and s['bind'] != 'local':
            out.append(".class public abstract sealed auto ansi '<sym>%s' extends [mscorlib]System.Object\n{\n  .field public static native int '%s'\n}" % (s['name'], s['name']))
    # local class: TU-local functions and data, the data image, init1, init2, constructor access
    out.append(".class public abstract sealed auto ansi %s extends [mscorlib]System.Object\n{" % loc)
    out.append("  .field private static native int '<msil>data'")
    for s in link['symbols']:
        if s['kind'] == 'data' and s['bind'] == 'local':
            out.append("  .field public static native int '%s'" % s['name'])
    for m in o.get('localm', []):
        out.extend(m)
    for s in o['stubs']:
        if 'msil.unresolved' not in s:
            out.append(s)
    img = bytes.fromhex(link['image'])
    size = max([s['offset'] + s['size'] for s in link['symbols'] if s['kind'] == 'data'] + [len(img), 1])
    init1 = ['  .method public static void init1() cil managed\n  {\n    .maxstack 4',
             '    ldc.i4 %d conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)' % (size + 15),
             '    ldc.i4.s 15 conv.i add ldc.i4.s -16 conv.i and stsfld native int %s::\'<msil>data\'' % loc,
             '    ldsfld native int %s::\'<msil>data\' ldsflda valuetype %s/\'<msil>image_t\' %s::\'<msil>image\' ldc.i4 %d cpblk' % (loc, loc, loc, len(img)),
             '    ldsfld native int %s::\'<msil>data\' ldc.i4 %d add ldc.i4.0 ldc.i4 %d initblk' % (loc, len(img), size - len(img))]
    for s in link['symbols']:
        if s['kind'] != 'data':
            continue
        if s['bind'] != 'local' and winner[s['name']][0] is not o:
            continue   # a losing weak/COMDAT/common copy: its type exists, but the facade points elsewhere
        tgt = "%s::'%s'" % (loc, s['name']) if s['bind'] == 'local' else "'<sym>%s'::'%s'" % (s['name'], s['name'])
        init1.append('    ldsfld native int %s::\'<msil>data\' ldc.i4 %d add stsfld native int %s' % (loc, s['offset'], tgt))
    for s in link['symbols']:
        if s['kind'] == 'func' and s['bind'] != 'local' and winner[s['name']][0] is o:
            init1.append("    ldftn %s stsfld native int '<sym>%s'::'<msil>fn'" % (
                re.sub(r"^\.method public static (.+?) " + QN + r"\((.*)\) cil managed.*$", r"\1 '<sym>%s'::\2(\3)" % s['name'], sigs[s['name']]), s['name']))
    for a, t in link['aliases'].items():
        init1.append("    ldsfld native int %s::'<msil>fn' stsfld native int '<sym>%s'::'<msil>fn'" % (gtype(t), a))
    init1.append('    ret\n  }')
    init2 = ['  .method public static void init2() cil managed\n  {\n    .maxstack 4']
    for r in link['relocs']:
        owner = [s for s in link['symbols'] if s['kind'] == 'data' and s['offset'] <= r['site'] < s['offset'] + s['size']][0]
        if owner['bind'] != 'local' and winner[owner['name']][0] is not o:
            continue
        t = r['target']
        allrefs.add(t)
        if r['kind'] == 'fnaddr64':
            val = "ldsfld native int %s::'<msil>fn'" % (gtype(t) if sym.get(t, {}).get('bind') != 'local' else loc)
        elif sym.get(t, {}).get('bind') == 'local':
            val = "ldsfld native int %s::'%s'" % (loc, t)
        else:
            val = "ldsfld native int %s::'%s'" % (gtype(t), t)
        if r['addend']:
            val += ' ldc.i4 %d add' % r['addend']
        init2.append("    ldsfld native int %s::'<msil>data' ldc.i4 %d add %s stind.i" % (loc, r['site'], val))
    init2.append('    ret\n  }')
    out += init1 + init2
    out.append("  .class nested private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType { .pack 1 .size %d }" % max(len(img), 1))
    out.append("  .field private static valuetype %s/'<msil>image_t' '<msil>image' at D_%s" % (loc, asm))
    out.append('}')
    out.append('.data D_%s = bytearray (%s)' % (asm, ' '.join('%02X' % b for b in (img or b'\0'))))
    o['il'] = '\n'.join(out) + '\n'
    open(os.path.join(outdir, asm + '.il'), 'w', encoding='utf-8').write(o['il'])

# ---- the link product: facade + driver -----------------------------------------------------------
names = sorted(set(winner) | allrefs)
fac = [".assembly 'msil.extern' { .ver 0:0:0:0 }"] + [".assembly extern '%s' { .ver 0:0:0:0 }" % a for a in [o['asm'] for o in objs] + [prog]]
for n in names:
    fac.append(".class extern forwarder '<sym>%s' { .assembly extern '%s' }" % (n, provider(n)))
open(os.path.join(outdir, 'msil.extern.il'), 'w', encoding='utf-8').write('\n'.join(fac) + '\n')
drv = [MSCORLIB, ".assembly extern 'msil.extern' { .ver 0:0:0:0 }"] + [".assembly extern '%s' { .ver 0:0:0:0 }" % o['asm'] for o in objs]
drv.append(".assembly '%s' { .ver 0:0:0:0 }" % prog)
for n in names:
    if n not in winner and n in IMPORTS:   # a native import: the P/Invoke lives in the program
        drv.append(".class public abstract sealed auto ansi '<sym>%s' extends [mscorlib]System.Object\n{\n"
                   "  .field public static native int '<msil>fn'\n  %s\n}" % (
                       n, undef_stub[n].replace('"msil.unresolved"', '"%s"' % IMPORTS[n])))
    elif n not in winner:   # unresolved weak: null address, a call traps
        drv.append(".class public abstract sealed auto ansi '<sym>%s' extends [mscorlib]System.Object\n{\n  .field public static native int '<msil>fn'\n  .field public static native int '%s'\n}" % (n, n))
ctors = sorted(((c['priority'], k, i, o['asm'], c['func']) for k, o in enumerate(objs) for i, c in enumerate(o['link']['ctors'])))
drv.append(".method private static int32 Main(string[] a) cil managed\n{\n  .entrypoint\n  .maxstack 1")
for o in objs:
    drv.append("  call void ['%s']'<msil>%s'::init1()" % (o['asm'], o['asm']))
for o in objs:
    drv.append("  call void ['%s']'<msil>%s'::init2()" % (o['asm'], o['asm']))
for p, k, i, a, f in ctors:
    drv.append("  call void ['%s']'<msil>%s'::'%s'()" % (a, a, f))
drv.append("  call int32 %s::'main'()\n  ret\n}" % gtype('main'))
open(os.path.join(outdir, prog + '.il'), 'w', encoding='utf-8').write('\n'.join(drv) + '\n')
print('objects: %s; facade forwarders: %d; constructors: %s' % (
    ', '.join(o['asm'] for o in objs), len(names), ' '.join('%s(%d)' % (f, p) for p, k, i, a, f in ctors)))
