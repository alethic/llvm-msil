# llvm-mc has no MSIL assembly parser: ILAsm text is assembled by ilasm, and
# CIL objects come from the code generator (M19), not from text. The fork's
# IsMSIL case of the AsmParser constructor (llvm/lib/MC/MCParser/AsmParser.cpp)
# reports the usage error 'assembly parsing is not supported for MSIL'
# (ImplementationPlan.md M11.c, clang/test/CodeGen/MSIL/cc1as-unsupported.s).
# From M18 the MSIL target registers a code emitter and an asm backend
# (M18.b), so '-filetype=obj' creates them and an object streamer and then
# reaches the same error, where before M18 llvm-mc crashed without a backend:
# a clean error on every MSIL triple and file type, no assertion and no
# crash ('not' fails on a crash; the output checks catch an assertion, which
# exits with status 3 on Windows). Reporting a usage error runs the interrupt
# handlers, which print LLVM's bug-report banner on Windows, so only the
# assertion and exception texts are ruled out.

# RUN: not llvm-mc -triple=x86_64-pc-windows-msvc-msil -filetype=obj %s -o %t.win.o 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not=Assertion --implicit-check-not="Exception Code"
# RUN: not llvm-mc -triple=x86_64-unknown-linux-gnu-msil -filetype=obj %s -o %t.lnx.o 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not=Assertion --implicit-check-not="Exception Code"
# RUN: not llvm-mc -triple=aarch64-unknown-linux-gnu-msil -filetype=obj %s -o %t.a64.o 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not=Assertion --implicit-check-not="Exception Code"
# RUN: not llvm-mc -triple=x86_64-w64-windows-gnu-msil -filetype=obj %s -o %t.mingw.o 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not=Assertion --implicit-check-not="Exception Code"
# RUN: not llvm-mc -triple=x86_64-pc-windows-msvc-msil -filetype=asm %s -o - 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not=Assertion --implicit-check-not="Exception Code"
# RUN: not llvm-mc -triple=x86_64-pc-windows-msvc-msil -filetype=null %s 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not=Assertion --implicit-check-not="Exception Code"

# CHECK: LLVM ERROR: assembly parsing is not supported for MSIL

  ldc.i4.0
  ret
