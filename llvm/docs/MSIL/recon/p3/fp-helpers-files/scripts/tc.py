"""Small toolchain wrapper for the fp-helpers recon: MSIL and native builds, and runs on every runtime.

Import with sys.path.insert(0, <scripts dir>).
"""
import json, os, subprocess, sys, time

BIN = 'D:/llvm-msil/build/bin/'
CLANG = BIN + 'clang.exe'
LDLLD = BIN + 'ld.lld.exe'
SCR = 'C:/llvm-msil-scratch/p3/fp-helpers'
LINSYS = 'C:/llvm-msil-scratch/linux-sysroot'
LINLIB = LINSYS + '/usr/lib/x86_64-linux-gnu'
_env = json.load(open(SCR + '/scripts/env.json'))
ENV = dict(os.environ)
ENV['LIB'] = _env['LIB']
ENV['INCLUDE'] = _env['INCLUDE']
VSRT = _env['VCINSTALLDIR'].rstrip('\\') + '\\Tools\\Llvm\\x64\\lib\\clang\\22\\lib\\windows\\clang_rt.builtins-x86_64.lib'
TRIPLE = {'win': 'x86_64-pc-windows-msvc-msil', 'lin': 'x86_64-unknown-linux-gnu-msil'}
ARCH = {'win': SCR + '/crt/win/clang_rt.builtins.lib', 'lin': SCR + '/crt/lin/libclang_rt.builtins.a'}
NATARCH = {'win': SCR + '/crt/native-win/clang_rt.builtins.lib', 'lin': SCR + '/crt/native-lin/libclang_rt.builtins.a'}
RC = {8: SCR + '/scripts/rc8.json', 10: SCR + '/scripts/rc10.json'}


def sh(cmd, check=True, cwd=None, timeout=None):
    p = subprocess.run(cmd, capture_output=True, text=True, env=ENV, cwd=cwd, timeout=timeout)
    if check and p.returncode != 0:
        raise RuntimeError('FAILED: %s\n%s%s' % (' '.join(cmd), p.stdout, p.stderr))
    return p


def msil(srcs, os_, out, opt='-O2', flags=(), helpers=True, extra_link=()):
    objs = []
    for s in srcs:
        o = out + '.' + os.path.basename(s) + '.o'
        sh([CLANG, '--target=' + TRIPLE[os_], opt, '-c', s, '-o', o] + list(flags))
        objs.append(o)
    link = [CLANG, '--target=' + TRIPLE[os_], opt] + objs + (([ARCH[os_]]) if helpers else []) + list(extra_link) + ['-o', out]
    if os_ == 'lin':
        link.insert(2, '--sysroot=' + LINSYS)
    return sh(link)


def native_win(srcs, out, opt='-O2', flags=(), rt='vs'):
    lib = VSRT if rt == 'vs' else NATARCH['win']
    return sh([CLANG, '--target=x86_64-pc-windows-msvc', opt, '-fuse-ld=lld'] + list(flags) + list(srcs) + [lib, '-o', out])


def native_lin(srcs, out, opt='-O2', flags=(), rt='libgcc'):
    objs = []
    for s in srcs:
        o = out + '.' + os.path.basename(s) + '.o'
        sh([CLANG, '--target=x86_64-unknown-linux-gnu', '-fno-pic', opt, '-c', s, '-o', o] + list(flags))
        objs.append(o)
    start = SCR + '/scripts/start.o'
    if not os.path.exists(start):
        sh([CLANG, '--target=x86_64-unknown-linux-gnu', '-ffreestanding', '-fno-pic', '-O2', '-c',
            'D:/llvm-msil/msil/utils/linux-oracle/start.c', '-o', start])
    libs = [LINLIB + '/libm.so.6', LINLIB + '/libc.so.6']
    if rt == 'libgcc':
        libs.append(LINLIB + '/libgcc_s.so.1')
    else:
        libs = [NATARCH['lin']] + libs
    return sh([LDLLD, '-dynamic-linker', '/lib64/ld-linux-x86-64.so.2', '--as-needed', start] + objs + libs + ['-o', out])


def wslpath(p):
    p = os.path.abspath(p).replace('\\', '/')
    return '/mnt/' + p[0].lower() + p[2:]


def run(kind, exe, args=(), timeout=1800):
    """kind: net8, net10, netfx, native-win, wsl8, wsl10, native-lin. Returns (stdout, rc, seconds)."""
    t = time.time()
    exe = os.path.abspath(exe)
    if kind in ('net8', 'net10'):
        cmd = ['dotnet', 'exec', '--runtimeconfig', RC[int(kind[3:])], exe] + list(args)
    elif kind in ('netfx', 'native-win'):
        cmd = [exe] + list(args)
    elif kind in ('wsl8', 'wsl10'):
        cmd = ['wsl', '-d', 'Ubuntu', '--', 'bash', wslpath(SCR + '/scripts/wslrun.sh'), kind[3:], wslpath(exe)] + list(args)
    elif kind == 'native-lin':
        cmd = ['wsl', '-d', 'Ubuntu', '--', wslpath(exe)] + list(args)
    else:
        raise ValueError(kind)
    e = dict(ENV)
    e['MSYS_NO_PATHCONV'] = '1'
    p = subprocess.run(cmd, capture_output=True, text=True, env=e, timeout=timeout)
    return p.stdout.replace('\r\n', '\n'), p.returncode, time.time() - t, p.stderr
