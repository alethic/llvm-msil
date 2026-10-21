#!/usr/bin/env python3
"""objectify.py: turn Phase-1 ILAsm text of one TU into an option-2 ("symbolic")
CIL object in ILAsm form plus its link-info record.

Object shape (what P2-A's backend would emit; see objfmt-binding.md):
  * an ECMA-335 module without manifest ('.module <name>', no '.assembly');
  * defined functions: global methods of <Module>, 'public' (global/weak) or
    'privatescope' (TU-local);
  * defined data: one 'initonly native int' address field per global variable,
    public or privatescope; no data segment, no .cctor, no Main;
  * undefined functions: either the Phase-1 P/Invoke stub shape with the reserved
    ModuleRef 'msil.unresolved' (form 'stub') or a MemberRef on the placeholder
    TypeRef [msil.extern]'<msil>Symbols' (form 'memberref');
  * undefined data: MemberRef [msil.extern]'<msil>Symbols'::'name' (sig native int);
  * function addresses: MemberRef [msil.extern]'<msil>FnAddr'::'name' (sig native int);
  * link info (data image, symbol flags, data relocations, ctors, aliases) in the
    embedded resource 'msil.link' (JSON in this prototype).
usage: objectify.py <phase1.il> <info.json from irinfo.py> <module name> <out.il> <out-linkinfo.json>
                    [<default undef form: stub|memberref> [name=form ...]]
"""
import json
import re
import sys

src, infof, modname, outil, outlink = sys.argv[1:6]
default_form = sys.argv[6] if len(sys.argv) > 6 else 'stub'
form_over = dict(a.split('=', 1) for a in sys.argv[7:])
text = open(src, encoding='utf-8').read()
irinfo = json.load(open(infof))
G = irinfo['globals']


def bind_of(gv):
    l = gv['linkage']
    if l in ('private', 'internal'):
        return 'local'
    if l == 'common':
        return 'common'
    if l in ('weak', 'weak_odr', 'linkonce', 'linkonce_odr', 'extern_weak'):
        return 'weak'
    return 'global'


undef_data = [n for n, g in G.items() if g['kind'] == 'data' and g['undefined']]
undef_funcs = {n: form_over.get(n, default_form) for n, g in G.items()
               if g['kind'] == 'func' and g['undefined'] and not n.startswith('llvm.')}
spec = dict(module=modname, undef_data=undef_data, undef_funcs=undef_funcs,
            weak_undef=[n for n, g in G.items() if g['undefined'] and g['linkage'] == 'extern_weak'],
            data={n: dict(bind=bind_of(g), size=g['size'], align=g['align'],
                          comdat=(n if g['comdat'] else None),
                          merge_strings=(g['cstring'] and g['unnamed_addr'] and g['constant']
                                         and g['linkage'] in ('private', 'internal')),
                          visibility=g['visibility'])
                  for n, g in G.items() if g['kind'] == 'data' and not g['undefined']},
            funcs={n: dict(bind=bind_of(g), comdat=(n if g['comdat'] else None), visibility=g['visibility'])
                   for n, g in G.items() if g['kind'] == 'func' and not g['undefined']},
            ctors=irinfo['ctors'], aliases=irinfo['aliases'])
lines = text.splitlines()

SYM = "[msil.extern]'<msil>Symbols'"
FNA = "[msil.extern]'<msil>FnAddr'"
QN = r"('(?:[^'\\]|\\.)*')"


def unq(s):
    return s[1:-1] if s.startswith("'") else s


# --- split the module into pieces --------------------------------------------
methods, stubs, fields, image, cctor = [], [], {}, bytearray(), []
i = 0
while i < len(lines):
    l = lines[i]
    if l.startswith('.method') and 'pinvokeimpl(' in l:
        stubs.append(l)
        i += 1
        continue
    if l.startswith('.method private hidebysig specialname rtspecialname static void .cctor'):
        j = i
        while not lines[j].startswith('} // end of method'):
            j += 1
        cctor = [x.strip() for x in lines[i:j + 1]]
        i = j + 1
        continue
    if l.startswith(".method private hidebysig static int32 '<msil>Main'"):
        while not lines[i].startswith('} // end of method'):
            i += 1
        i += 1
        continue
    if l.startswith('.method'):
        j = i
        while not lines[j].startswith('} // end of method'):
            j += 1
        methods.append(lines[i:j + 1])
        i = j + 1
        continue
    if l.startswith(".data cil '<msil>image_d'"):
        j = i
        blob = []
        while True:
            blob.append(lines[j])
            if ')' in lines[j].split('//')[0]:
                break
            j += 1
        for b in blob:
            b = b.split('//')[0]
            b = b.replace(".data cil '<msil>image_d' = bytearray (", '').replace(')', '')
            image += bytes(int(x, 16) for x in b.split())
        i = j + 1
        continue
    m = re.match(r"\.field (public|assembly) static initonly native int " + QN + r" // \+(\d+)", l)
    if m:
        fields[unq(m.group(2))] = int(m.group(3))
    i += 1

# --- relocations and constructors from the Phase-1 .cctor ---------------------
relocs, ctors = [], []
body = [x for x in cctor if x and not x.startswith('//')]
k = 0
LDC = r'ldc\.i4(?:\.s)? (-?\d+)$'
while k < len(body):
    # site: ldsfld '<msil>data' [ldc.i4 S; add]; target: ldsfld T [ldc.i4 A; add]; stind.i
    if body[k] == "ldsfld native int '<msil>data'":
        k2, site = k + 1, 0
        m = re.match(LDC, body[k2]) if k2 < len(body) else None
        if m and body[k2 + 1] == 'add':
            site = int(m.group(1))
            k2 += 2
        m = re.match(r"ldsfld native int " + QN + "$", body[k2]) if k2 < len(body) else None
        if m and unq(m.group(1)) != '<msil>data':
            tgt = unq(m.group(1))
            k3 = k2 + 1
            add = 0
            m2 = re.match(LDC, body[k3])
            if m2 and body[k3 + 1] == 'add':
                add = int(m2.group(1))
                k3 += 2
            unal = 0
            m3 = re.match(r'unaligned\. (\d)$', body[k3])
            if m3:
                unal = int(m3.group(1))
                k3 += 1
            if body[k3] in ('stind.i', 'stind.i4'):
                kind = 'addr64' if body[k3] == 'stind.i' else 'addr32'
                if tgt.startswith('<msil>fn_'):
                    relocs.append(dict(site=site, kind='fnaddr64', target=tgt[len('<msil>fn_'):], addend=0, unaligned=unal))
                else:
                    relocs.append(dict(site=site, kind=kind, target=tgt, addend=add, unaligned=unal))
                k = k3 + 1
                continue
    m = re.match(r"call void " + QN + r"\(\)$", body[k])
    if m:
        ctors.append(unq(m.group(1)))
    k += 1

# --- link info ----------------------------------------------------------------
undef_data = set(spec.get('undef_data', []))
undef_form = spec.get('undef_funcs', {})
symbols = []
for name, off in sorted(fields.items(), key=lambda kv: kv[1]):
    if name in undef_data:
        continue
    d = spec['data'][name]
    symbols.append(dict(name=name, kind='data', offset=off, size=d['size'], align=d['align'],
                        bind=d['bind'], comdat=d.get('comdat'), visibility=d['visibility'],
                        merge_strings=d.get('merge_strings', False)))
for name, f in spec.get('funcs', {}).items():
    symbols.append(dict(name=name, kind='func', bind=f['bind'], comdat=f.get('comdat'),
                        visibility=f['visibility']))
prio = dict(spec.get('ctors', []))
link = dict(object=spec['module'], image=image.hex(), init_size=len(image),
            symbols=symbols, relocs=relocs,
            ctors=[dict(func=c, priority=p) for c, p in irinfo['ctors']],
            aliases=spec.get('aliases', {}), weak_undef=spec.get('weak_undef', []))

# --- object text ----------------------------------------------------------------
local_funcs = {n for n, f in spec.get('funcs', {}).items() if f['bind'] == 'local'}


def rewrite(l):
    s = l.strip()
    m = re.match(r"(ldsfld|stsfld|ldsflda) native int " + QN + "$", s)
    if m:
        n = unq(m.group(2))
        if n.startswith('<msil>fn_'):
            return l.replace(m.group(2), "%s::'%s'" % (FNA, n[len('<msil>fn_'):]))
        if n in undef_data:
            return l.replace(m.group(2), "%s::'%s'" % (SYM, n))
    m = re.match(r"(call|ldftn) (.+?) " + QN + r"\((.*)\)$", s)
    if m and undef_form.get(unq(m.group(3))) == 'memberref':
        return l.replace(m.group(3) + '(', "%s::%s(" % (SYM, m.group(3)), 1)
    return l


out = ['// CIL object (option 2, symbolic) generated by objectify.py from '
       + src.replace('\\', '/').split('/')[-1],
       ".module '%s'" % spec['module'],
       '.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }',
       ".assembly extern 'msil.extern' { .ver 0:0:0:0 }",
       ".module extern 'msil.unresolved'",
       ".mresource private 'msil.link' { }", '']
for m in methods:
    hdr = m[0]
    name = unq(re.search(QN + r"\(", hdr).group(1))
    if name in local_funcs:
        hdr = hdr.replace('.method assembly static', '.method privatescope static')
    out.append(hdr)
    out.extend(rewrite(x) for x in m[1:])
    out.append('')
for s in stubs:
    name = unq(re.search(QN + r"\(", s).group(1))
    if undef_form.get(name, 'stub') == 'stub':
        out.append(s)
out.append('')
for sym in symbols:
    if sym['kind'] == 'data':
        acc = 'privatescope' if sym['bind'] == 'local' else 'public'
        q = ''.join('\\134' if c == '\\' else "\\'" if c == "'" else ('\\%03o' % ord(c)) if ord(c) < 32 else c
                    for c in sym['name'])   # D11 escapes
        out.append(".field %s static initonly native int '%s'" % (acc, q))
text_out = '\n'.join(out) + '\n'
# D11: a BOM when any name is non-ASCII, or the Framework ilasm reads the file as ANSI
open(outil, 'w', encoding=('utf-8-sig' if any(ord(c) > 127 for c in text_out) else 'utf-8')).write(text_out)
json.dump(link, open(outlink, 'w'), indent=1)
print('%s: %d methods, %d stubs, %d data symbols, %d relocs, %d ctors, image %d bytes' %
      (spec['module'], len(methods), len(stubs), sum(1 for s in symbols if s['kind'] == 'data'),
       len(relocs), len(ctors), len(image)))
