"""Compile the five LLVM TUs that bin/lld.exe needs but the shared build has not
built (LLVMDTLTO, LLVMLibDriver, LLVMWindowsManifest), then link a full
multi-flavor lld.exe from the scratch objects, to measure link time and size.
Run inside the VS x64 dev shell. Reads the shared build; writes only scratch.
"""
import json, os, re, subprocess, sys, time

BS = chr(92)
BUILD = 'D:/llvm-msil/build'
SRC = 'D:/llvm-msil'
SCR = 'C:/llvm-msil-scratch/p2/lld-flavor'
LLD_LINK = 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/lld-link.exe'

os.makedirs(SCR + '/gen/llvm-lib', exist_ok=True)
subprocess.check_call([BUILD + '/bin/llvm-tblgen.exe', '-gen-opt-parser-defs', '-I', SRC + '/llvm/include',
                       SRC + '/llvm/lib/ToolDrivers/llvm-lib/Options.td', '-o', SCR + '/gen/llvm-lib/Options.inc'])
cc = json.load(open(BUILD + '/compile_commands.json'))
want = ('llvm/lib/DTLTO/', 'llvm/lib/ToolDrivers/llvm-lib/', 'llvm/lib/WindowsManifest/')
objs = []
os.makedirs(SCR + '/obj/llvm', exist_ok=True)
t0 = time.time()
for e in cc:
    f = e['file'].replace(BS, '/')
    if not any(w in f for w in want):
        continue
    obj = f'{SCR}/obj/llvm/{os.path.basename(f)}.obj'
    cmd = re.sub(r'/Fo\S+', lambda m: '/Fo' + obj.replace('/', BS), e['command'])
    cmd = re.sub(r'/Fd\S+', lambda m: '/Fd' + (SCR + '/obj/llvm/x.pdb').replace('/', BS), cmd)
    cmd = cmd.replace('-ID:/llvm-msil/build/lib/ToolDrivers/llvm-lib ', f'-I{SCR}/gen/llvm-lib ')
    t = time.time()
    r = subprocess.run(cmd, cwd=SCR, capture_output=True, text=True)
    print(f'{time.time() - t:6.1f}s rc={r.returncode} {f}')
    if r.returncode:
        print(r.stdout[-3000:], r.stderr[-3000:])
        sys.exit(1)
    objs.append(obj)
print(f'llvm TUs: {len(objs)} in {time.time() - t0:.1f}s')

nin = open(BUILD + '/build.ninja', encoding='utf-8', errors='replace').read()
i = nin.index('build bin/lld.exe:')
blk = nin[i:nin.index('\nbuild ', i + 10)]
libs = re.search(r'LINK_LIBRARIES = (.*)', blk).group(1).split()
flags = re.search(r'LINK_FLAGS = (.*)', blk).group(1).split()
mine = {'lib/lldCOFF.lib': 'COFF', 'lib/lldELF.lib': 'ELF', 'lib/lldMachO.lib': 'MachO',
        'lib/lldMinGW.lib': 'MinGW', 'lib/lldWasm.lib': 'wasm', 'lib/lldCommon.lib': 'Common'}
items = []
for d in ('tools',):
    items += [f'{SCR}/obj/{d}/{f}' for f in os.listdir(f'{SCR}/obj/{d}') if f.endswith('.obj')]
items += objs
for l in libs:
    if l in mine:
        d = mine[l]
        items += [f'{SCR}/obj/{d}/{f}' for f in sorted(os.listdir(f'{SCR}/obj/{d}')) if f.endswith('.obj')]
    elif l in ('lib/LLVMDTLTO.lib', 'lib/LLVMLibDriver.lib', 'lib/LLVMWindowsManifest.lib'):
        continue
    else:
        items.append(BUILD + '/' + l if l.startswith('lib/') else l)
rsp = SCR + '/obj/lld-full.rsp'
open(rsp, 'w').write('\n'.join(items))
out = SCR + '/bin-full/lld.exe'
os.makedirs(os.path.dirname(out), exist_ok=True)
t = time.time()
r = subprocess.run([LLD_LINK, '/nologo', '@' + rsp, '/out:' + out] + flags, capture_output=True, text=True)
print(f'link {time.time() - t:.1f}s rc={r.returncode}')
if r.returncode:
    print(r.stdout[-5000:], r.stderr[-5000:])
else:
    print('size', os.path.getsize(out))
