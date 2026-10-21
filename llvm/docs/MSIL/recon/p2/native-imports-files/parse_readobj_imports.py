# Parses 'llvm-readobj --coff-imports' output of an import library into rows:
# dll, type, nametype, exportname, symbols
import sys, re, collections
rows=[]; cur=None
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    line=line.rstrip('\n')
    if line.startswith('File: '):
        if cur and 'type' in cur: rows.append(cur)
        cur={'dll':line[6:], 'syms':[]}
    elif cur is not None:
        m=re.match(r'(Format|Type|Name type|Export name|Symbol): (.*)', line)
        if m:
            k,v=m.groups()
            if k=='Symbol': cur['syms'].append(v)
            else: cur[{'Format':'fmt','Type':'type','Name type':'nt','Export name':'exp'}[k]]=v
if cur and 'type' in cur: rows.append(cur)
mode=sys.argv[2] if len(sys.argv)>2 else 'summary'
if mode=='summary':
    print('members', len(rows))
    print('by type', collections.Counter(r['type'] for r in rows))
    print('by name type', collections.Counter(r['nt'] for r in rows))
    print('dlls', len(set(r['dll'] for r in rows)))
    for d,c in sorted(collections.Counter(r['dll'] for r in rows).items()): print('  ',d,c)
elif mode=='find':
    names=set(sys.argv[3].split(','))
    for r in rows:
        if r.get('exp') in names or any(s in names for s in r['syms']):
            print(f"{r.get('exp')!s:24} {r['dll']:40} type={r['type']:5} nametype={r['nt']:10} syms={','.join(r['syms'])}")
elif mode=='type':
    for r in rows:
        if r['type']==sys.argv[3]:
            print(f"{r.get('exp')!s:28} {r['dll']:40} nametype={r['nt']} syms={','.join(r['syms'])}")
elif mode=='nt':
    for r in rows:
        if r['nt']!=sys.argv[3]:
            print(f"{r.get('exp')!s:28} {r['dll']:40} type={r['type']} nametype={r['nt']} syms={','.join(r['syms'])}")
