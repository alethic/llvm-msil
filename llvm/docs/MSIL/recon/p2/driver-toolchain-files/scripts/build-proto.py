# Builds a scratch clang.exe with the prototype MSIL toolchain (recon driver-toolchain).
# Compiles 3 TUs with the shared build's exact flags (read-only use of its PCH and
# libraries), swaps them into a scratch copy of clangDriver.lib, and relinks clang.
# Run inside the VS x64 developer environment (msil-env.ps1). Writes only under SCR.
import json, os, shlex, shutil, subprocess, sys, time

BS = chr(92)
BUILD = 'D:/llvm-msil/build'
SCR = 'C:/llvm-msil-scratch/p2/driver-toolchain/proto'
OBJ = SCR + '/obj'
VSLLVM = 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin'
os.makedirs(OBJ, exist_ok=True)
os.makedirs(SCR + '/bin', exist_ok=True)

cc = json.load(open(BUILD + '/compile_commands.json'))
def cmd_for(suffix):
    for e in cc:
        if e['file'].replace(BS, '/').endswith(suffix):
            return e['command']
    sys.exit('no command for ' + suffix)

def compile_one(template_suffix, src, objname, extra_inc):
    args = shlex.split(cmd_for(template_suffix), posix=False)
    out = []
    for a in args:
        if a.startswith('/Fo'):
            a = '/Fo' + OBJ + '/' + objname
        elif a.startswith('/Fd'):
            a = '/Fd' + OBJ + '/' + objname + '.pdb'
        out.append(a)
    # replace the source (last argument after '--')
    out[-1] = src
    # extra includes go right after the compiler
    out = out[:1] + ['-I' + i for i in extra_inc] + out[1:]
    t = time.time()
    r = subprocess.run(out, cwd=BUILD, capture_output=True, text=True)
    print(f'{objname}: rc={r.returncode} {time.time()-t:.1f}s')
    if r.returncode:
        print(r.stdout[-6000:], r.stderr[-6000:])
        sys.exit(1)

inc = [SCR, 'D:/llvm-msil/clang/lib/Driver/ToolChains']
if 'link' not in sys.argv[1:]:
    compile_one('clang/lib/Driver/ToolChains/WebAssembly.cpp', SCR + '/MSIL.cpp', 'MSIL.cpp.obj', inc)
    compile_one('clang/lib/Driver/Driver.cpp', SCR + '/Driver.cpp', 'Driver.cpp.obj', inc)
    compile_one('clang/lib/Driver/ToolChain.cpp', SCR + '/ToolChain.cpp', 'ToolChain.cpp.obj', inc)

# The prototype objects go on the link line ahead of the libraries; lld-link then
# never extracts the original Driver.cpp.obj/ToolChain.cpp.obj members (every symbol
# they define is already defined).
proto_objs = [OBJ + '/MSIL.cpp.obj', OBJ + '/Driver.cpp.obj', OBJ + '/ToolChain.cpp.obj']
lib = 'lib/clangDriver.lib'

# link: the build.ninja edge for bin/clang.exe with clangDriver.lib swapped
nin = open(BUILD + '/build.ninja', encoding='utf-8').read().split('\n')
i = next(k for k, l in enumerate(nin) if l.startswith('build bin/clang.exe: CXX_EXECUTABLE_LINKER'))
edge = nin[i]
objs = edge.split(':', 1)[1].split('|')[0].split()[1:]
vars_ = {}
for l in nin[i + 1:i + 20]:
    if not l.startswith('  '):
        break
    k, _, v = l.strip().partition(' = ')
    vars_[k] = v
libs = [x if x != 'lib/clangDriver.lib' else lib for x in vars_['LINK_LIBRARIES'].split()]
flags = vars_['LINK_FLAGS'].split()
out = SCR + '/bin/clang.exe'
argv = [VSLLVM + '/lld-link.exe', '/nologo', '/out:' + out] + objs + proto_objs + flags + libs
rsp = OBJ + '/link.rsp'
open(rsp, 'w').write('\n'.join('"%s"' % a if ' ' in a else a for a in argv[1:]))
t = time.time()
r = subprocess.run([argv[0], '@' + rsp], cwd=BUILD, capture_output=True, text=True)
print(f'link: rc={r.returncode} {time.time()-t:.1f}s')
print(r.stdout[-3000:], r.stderr[-3000:])
