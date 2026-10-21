# Applies the prototype MSIL driver hooks to scratch copies of Driver.cpp and
# ToolChain.cpp (recon driver-toolchain). Usage: python patch-driver.py <proto dir>
import sys, pathlib

d = pathlib.Path(sys.argv[1])

def patch(name, edits):
    p = d / name
    s = p.read_text(encoding='utf-8')
    for old, new in edits:
        if s.count(old) != 1:
            sys.exit(f'{name}: expected exactly one match for {old!r}, got {s.count(old)}')
        s = s.replace(old, new)
    p.write_text(s, encoding='utf-8', newline='\n')

patch('Driver.cpp', [
    # 1. include
    ('#include "ToolChains/WebAssembly.h"\n',
     '#include "ToolChains/MSIL.h"\n#include "ToolChains/WebAssembly.h"\n'),
    # 2. every MSIL triple gets an MSIL toolchain wrapping its native one
    ('  auto &TC = ToolChains[Target.str()];\n  if (!TC) {\n    switch (Target.getOS()) {\n',
     '  auto &TC = ToolChains[Target.str()];\n'
     '  if (!TC && Target.isOSBinFormatMSIL())\n'
     '    TC = toolchains::createMSILToolChain(*this, Target, Args);\n'
     '  if (!TC) {\n    switch (Target.getOS()) {\n'),
    # 3. LTO on windows-msvc needs lld natively; msil-ld is the MSIL linker
    ('    if (C.getDefaultToolChain().getTriple().isWindowsMSVCEnvironment() &&\n',
     '    if (C.getDefaultToolChain().getTriple().isWindowsMSVCEnvironment() &&\n'
     '        !C.getDefaultToolChain().getTriple().isOSBinFormatMSIL() &&\n'),
    # 4. a program that 'dotnet' can run: the host needs .exe or .dll
    ('  return Target.isOSWindows() ? "a.exe" : "a.out";\n',
     '  if (Target.isOSBinFormatMSIL())\n    return "a.exe";\n'
     '  return Target.isOSWindows() ? "a.exe" : "a.out";\n'),
])

patch('ToolChain.cpp', [
    # getLink(): the MSIL toolchain now builds msil::Linker; drop the Phase-1 error
    ('  if (getTriple().isOSBinFormatMSIL()) {\n'
     '    D.Diag(clang::diag::err_drv_msil_no_object_output)\n'
     '        << (hasEffectiveTriple() ? getEffectiveTriple().getTriple()\n'
     '                                 : getTripleString());\n'
     '    return nullptr;\n  }\n  if (!Link)\n',
     '  if (!Link)\n'),
])
print('patched')
