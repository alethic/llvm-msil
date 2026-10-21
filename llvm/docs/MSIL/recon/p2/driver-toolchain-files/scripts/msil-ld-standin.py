# Stand-in for msil-ld (recon driver-toolchain): runs the lld-flavor recon's prototype
# msil-ld (LTO only, writes ILAsm text) and then ilasm, so that the clang driver's
# link job produces a runnable assembly today. Not a design proposal for msil-ld.
#  - programs become EXE images (ilasm -exe), --shared outputs DLL images (ilasm -dll);
#  - the prototype reads DLL export tables but not import libraries or ELF .so files,
#    so -lucrt/-lkernel32 are served from C:/Windows/System32 (ucrtbase.dll) and the
#    glibc default libraries (-lc -lm) are dropped (Linux test programs call no libc);
#  - unknown options are passed through.
import os, subprocess, sys, tempfile

PROTO = 'C:/llvm-msil-scratch/p2/lld-flavor/proto/bin/msil-ld.exe'
ILASM = 'C:/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe'
LOG = os.environ.get('MSIL_LD_STANDIN_LOG')

args = sys.argv[1:]
if LOG:
    with open(LOG, 'a') as f:
        f.write('msil-ld ' + ' '.join(args) + '\n')
out, shared, target, rest = None, False, '', []
i = 0
while i < len(args):
    a = args[i]
    if a == '-o':
        out = args[i + 1]; i += 2; continue
    if a in ('--shared', '-shared'):
        shared = True
    elif a.startswith('--target='):
        target = a.split('=', 1)[1]
    elif a.startswith('--sysroot='):
        i += 1; continue
    elif a in ('-lc', '-lm', '-lpthread') and 'linux' in target:
        i += 1; continue
    elif a == '-lucrt':
        rest += ['-LC:/Windows/System32', '-lucrtbase']; i += 1; continue
    elif a == '-lkernel32':
        rest += ['-LC:/Windows/System32', '-lkernel32']; i += 1; continue
    rest.append(a)
    i += 1
if not out:
    sys.exit('msil-ld stand-in: no -o')
fd, il = tempfile.mkstemp(suffix='.il'); os.close(fd)
r = subprocess.run([PROTO, '--lto-emit-asm', *rest, '-o', il])
if r.returncode:
    sys.exit(r.returncode)
kind = '-dll' if shared else '-exe'
arch = '-arm64' if target.startswith('aarch64') else '-x64'
r = subprocess.run([ILASM, '-nologo', '-quiet', kind, '-pe64', arch, '-output=' + out, il],
                   capture_output=True, text=True)
if r.returncode:
    sys.stderr.write(r.stdout + r.stderr)
    sys.exit(r.returncode)
os.remove(il)
