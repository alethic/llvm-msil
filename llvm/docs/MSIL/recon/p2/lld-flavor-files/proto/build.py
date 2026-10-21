"""Build the msil-ld prototype against the shared build's headers and static
libraries (read-only), with every output under this scratch directory.
Run inside the VS x64 dev shell (msil-env.ps1). Prints timings.
"""
import json, os, re, subprocess, sys, time

BS = chr(92)
BUILD = 'D:/llvm-msil/build'
SRC = 'D:/llvm-msil'
P = 'C:/llvm-msil-scratch/p2/lld-flavor/proto'
SCR = 'C:/llvm-msil-scratch/p2/lld-flavor'
LLD_LINK = 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/lld-link.exe'

os.makedirs(P + '/gen/MSIL', exist_ok=True)
os.makedirs(P + '/obj', exist_ok=True)
os.makedirs(P + '/bin', exist_ok=True)

t = time.time()
subprocess.check_call([BUILD + '/bin/llvm-tblgen.exe', '-gen-opt-parser-defs', '-I', SRC + '/llvm/include',
                       P + '/MSIL/Options.td', '-o', P + '/gen/MSIL/Options.inc'])
print(f'tablegen {time.time() - t:.1f}s')

cc = json.load(open(BUILD + '/compile_commands.json'))
tmpl = next(e['command'] for e in cc if e['file'].replace(BS, '/').endswith('lld/wasm/Driver.cpp'))
tmpl = re.sub(r' /Fo\S+', '', tmpl)
tmpl = re.sub(r' /Fd\S+', '', tmpl)
tmpl = re.sub(r' -c -- \S+$', '', tmpl)
tmpl = tmpl.replace('-ID:/llvm-msil/build/tools/lld/wasm -ID:/llvm-msil/lld/wasm ',
                    f'-I{P}/gen/MSIL -I{P}/MSIL ')
tmpl = tmpl.replace('-ID:/llvm-msil/lld/include', f'-I{P}/include -ID:/llvm-msil/lld/include')

srcs = ['Common/DriverDispatcher.cpp', 'MSIL/Driver.cpp', 'tools/msil-ld.cpp', 'tools/msil-ld-driver.cpp']
objs = []
for s in srcs:
    obj = P + '/obj/' + s.replace('/', '_') + '.obj'
    cmd = f'{tmpl} /Fo{obj.replace("/", BS)} /Fd{(P + "/obj/x.pdb").replace("/", BS)} -c -- {(P + "/" + s).replace("/", BS)}'
    t = time.time()
    r = subprocess.run(cmd, cwd=P, capture_output=True, text=True)
    print(f'compile {s}: {time.time() - t:.1f}s rc={r.returncode}')
    if r.returncode:
        print(r.stdout[-6000:], r.stderr[-6000:])
        sys.exit(1)
    objs.append(obj)

# lld/Common objects compiled by time_lld_tus.py (unmodified sources), minus the
# dispatcher that the prototype replaces.
common = [f'{SCR}/obj/Common/{f}' for f in sorted(os.listdir(SCR + '/obj/Common'))
          if f.endswith('.obj') and not f.startswith('DriverDispatcher')]

# Libraries: bin/lld.exe's LINK_LIBRARIES without the other lld flavors.
nin = open(BUILD + '/build.ninja', encoding='utf-8', errors='replace').read()
i = nin.index('build bin/lld.exe:')
blk = nin[i:nin.index('\nbuild ', i + 10)]
libs = re.search(r'LINK_LIBRARIES = (.*)', blk).group(1).split()
flags = re.search(r'LINK_FLAGS = (.*)', blk).group(1).split()
drop = {'lib/lldCOFF.lib', 'lib/lldELF.lib', 'lib/lldMachO.lib', 'lib/lldMinGW.lib',
        'lib/lldWasm.lib', 'lib/lldCommon.lib', 'lib/LLVMDTLTO.lib', 'lib/LLVMLibDriver.lib',
        'lib/LLVMWindowsManifest.lib', 'lib/LLVMWindowsDriver.lib'}
libs = [(BUILD + '/' + l if l.startswith('lib/') else l) for l in libs if l not in drop]
rsp = P + '/obj/link.rsp'
with open(rsp, 'w') as f:
    f.write('\n'.join(objs + common + libs))
out = P + '/bin/msil-ld.exe'
cmd = [LLD_LINK, '/nologo', '@' + rsp, '/out:' + out, '/pdb:' + P + '/bin/msil-ld.pdb'] + flags
t = time.time()
r = subprocess.run(cmd, capture_output=True, text=True)
print(f'link: {time.time() - t:.1f}s rc={r.returncode}')
if r.returncode:
    print(r.stdout[-8000:], r.stderr[-8000:])
    sys.exit(1)
print('size', os.path.getsize(out))
