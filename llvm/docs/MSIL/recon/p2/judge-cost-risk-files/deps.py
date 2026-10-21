# Read-only parser for ninja's .ninja_deps (format v4) and .ninja_log (v7).
# Usage: python deps.py <builddir> <header-suffix> [...]
import struct, sys, os, collections
bd = sys.argv[1]
data = open(os.path.join(bd, '.ninja_deps'), 'rb').read()
assert data[:12] == b'# ninjadeps\n'
ver = struct.unpack_from('<i', data, 12)[0]; assert ver == 4, ver
pos = 16; paths = []; deps = {}
while pos + 4 <= len(data):
    sz = struct.unpack_from('<I', data, pos)[0]; pos += 4
    isdep = sz >> 31; sz &= 0x7fffffff
    rec = data[pos:pos+sz]; pos += sz
    if isdep:
        out = struct.unpack_from('<I', rec, 0)[0]
        ids = struct.unpack_from('<%dI' % ((sz-12)//4), rec, 12)
        deps[out] = ids            # later records override earlier ones
    else:
        p = rec[:-4].rstrip(b'\0').decode('utf-8', 'replace')
        paths.append(p)
# durations from .ninja_log v7: start end mtime path hash (keep the last entry per output)
dur = {}
for line in open(os.path.join(bd, '.ninja_log'), encoding='utf-8', errors='replace'):
    if line.startswith('#'): continue
    f = line.rstrip('\n').split('\t')
    if len(f) >= 4: dur[f[3]] = int(f[1]) - int(f[0])
objs = {paths[o]: [paths[i].replace(chr(92),'/').lower() for i in ids] for o, ids in deps.items()}
total = sum(dur.get(o, 0) for o in objs)
print('objects with deps: %d, compile ms total: %d (%.2f CPU-h)' % (len(objs), total, total/3.6e6))
for suf in sys.argv[2:]:
    s = suf.lower()
    hit = [o for o, ins in objs.items() if any(i.endswith(s) for i in ins)]
    ms = sum(dur.get(o, 0) for o in hit)
    print('%-45s %5d objects %5.1f%% of compile CPU' % (suf, len(hit), 100.0*ms/total if total else 0))
