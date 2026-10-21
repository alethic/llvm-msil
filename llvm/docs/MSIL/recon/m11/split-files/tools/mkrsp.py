# Usage: python mkrsp.py <build.ninja> <target e.g. bin/llc.exe> <out.exe> <extra obj>... > rsp
import sys, shlex, os
ninja, target, out, extra = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
bdir = os.path.dirname(os.path.abspath(ninja))
lines = open(ninja, encoding='utf-8').read().split('\n')
for i, l in enumerate(lines):
    if l.startswith('build ' + target + ':'):
        break
else:
    sys.exit('target not found')
head = l[len('build ' + target + ':'):].split('|')[0].split()
explicit = head[1:]  # skip rule name
vars = {}
j = i + 1
while j < len(lines) and lines[j].startswith('  '):
    k, _, v = lines[j].strip().partition(' = ')
    vars[k] = v
    j += 1
def absify(p):
    if p.endswith(('.obj', '.lib', '.res')) and not os.path.isabs(p) and os.path.exists(os.path.join(bdir, p)):
        return os.path.join(bdir, p)
    return p
args = ['/nologo'] + extra + [absify(p) for p in explicit] + ['/out:' + out]
args += vars.get('LINK_FLAGS', '').split()
args += vars.get('LINK_PATH', '').split()
args += [absify(p) for p in vars.get('LINK_LIBRARIES', '').split()]
print('\n'.join('"%s"' % a if ' ' in a else a for a in args))
