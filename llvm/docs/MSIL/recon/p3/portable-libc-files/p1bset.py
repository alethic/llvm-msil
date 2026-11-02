import json,re,collections
d=json.load(open('obj/undef-all.json'))
rows=[l.rstrip('\n').split(' ',1) for l in open('results-all.txt') if re.match(r'^\d+ ',l)]
ok=[f for s,f in rows if s=='0']
def oname(f): return f.replace('/','_')+'.o'
cand_dirs=('string/','strings/','ctype/','inttypes/','stdbit/','search/','wchar/','locale/','stdlib/','math/','time/','stdio/')
rules=[('heap',r'^(malloc|free|realloc|aligned_alloc|__llvm_libc_dotnet_delete_\w+)$'),('atomics',r'^__atomic_'),
 ('exit-path',r'^(__cxa_finalize|_ZN18__llvm_libc_dotnet23at_quick_exit_callbacksE)$'),('clock',r'timespec_get'),
 ('File/stdio',r'_ZN18__llvm_libc_dotnet4File|_ZN18__llvm_libc_dotnet(5stdinE|6stdoutE|6stderrE)'),('environ',r'EnvironmentManager'),
 ('signal-names',r'get_signal_string'),('hash-table-global',r'global_hash_table')]
inv=collections.defaultdict(set)
for s,objs in d.items():
    for tag,rx in rules:
        if re.search(rx,s):
            for o in objs: inv[o].add(tag)
inv['stdlib_exit.cpp.o'].add('exit-path'); inv['stdlib__Exit.cpp.o'].add('exit-path'); inv['stdlib_quick_exit.cpp.o'].add('exit-path')
ship=[];blocked=collections.defaultdict(list)
for f in ok:
    if not f.startswith(cand_dirs) or f.startswith(('stdlib/baremetal','time/baremetal')): continue
    b=f.rsplit('/',1)[1]
    if re.search(r'f128|f16',b): continue
    if f in ('stdlib/rand_util.cpp','time/time_utils.cpp','errno/libc_errno.cpp'): continue
    t=inv.get(oname(f))
    if t: blocked[','.join(sorted(t))].append(f)
    else: ship.append(f)
print('P-1b shippable (candidate dirs, no f16/f128, closure free of heap/atomics/exit/clock/File/env):',len(ship))
c=collections.Counter(f.rsplit('/',1)[0] for f in ship); print(dict(sorted(c.items())))
for k,v in blocked.items(): print('BLOCKED',k,len(v),' '.join(x.rsplit('/',1)[1] for x in v))
