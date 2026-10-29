import re, sys
txt=open(sys.argv[1]).read()
for m in re.finditer(r"\.method [^\n]*'([^']+)'\([^\n]*\n(.*?)\} // end of method", txt, re.S):
    name, body = m.group(1), m.group(2)
    if name in ('.cctor','<msil>Main'): continue
    lines=[l.strip() for l in body.split('\n') if l.strip() and not l.strip().startswith(('.','//','{','}'))]
    ins=[l for l in lines if not re.match(r"^'?@?[\w.$]+'?:$", l)]
    ext=sorted(set(re.sub(r'\s+',' ',c)[:80] for c in ins if c.startswith(('call','localloc'))))
    print(f'{name:12s} {len(ins):4d} {"; ".join(ext)}')
