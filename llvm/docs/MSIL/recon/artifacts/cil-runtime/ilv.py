import sys,re,collections
c=collections.Counter(); tail=''
for l in sys.stdin:
    m=re.search(r'Error \[(\w*)\]: \[.*?\.dll : ([^\]]*)\](?:\[offset[^\]]*\])?(.*)',l)
    if m: c[(m.group(1) or '?', m.group(2), m.group(3).strip()[:80])]+=1
    elif 'Error(s)' in l or 'All Classes' in l: tail=re.sub(r' C:.*','',l.strip())
for (k,meth,msg),n in sorted(c.items()): print('   %2d x %-18s %-34s %s'%(n,k,meth,msg))
print('   ->', tail)
