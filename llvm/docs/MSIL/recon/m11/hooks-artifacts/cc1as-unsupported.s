# The MC assembly parser has no MSIL dialect: the IsMSIL case of the
# AsmParser constructor (llvm/lib/MC/MCParser/AsmParser.cpp) reports a usage
# error instead of reaching an uninitialized PlatformParser. 'clang -cc1as'
# with an MSIL triple reaches that constructor; the driver never does (it
# rejects assembling for MSIL triples, see clang/test/Driver/msil-toolchain.c).
# Every MSIL triple of plan decision D6 takes the same path. '-filetype obj'
# is not tested: cc1as asserts that the target has an MCAsmBackend before it
# creates the parser (true for every target without one).

# REQUIRES: msil-registered-target

# RUN: not %clang -cc1as -triple x86_64-unknown-linux-gnu-msil -filetype asm %s -o - 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Stack dump"
# RUN: not %clang -cc1as -triple x86_64-pc-windows-msvc-msil -filetype asm %s -o - 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Stack dump"
# RUN: not %clang -cc1as -triple aarch64-unknown-linux-gnu-msil -filetype null %s 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Stack dump"
# RUN: not %clang -cc1as -triple aarch64-w64-windows-gnu-msil -filetype null %s 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Stack dump"

# CHECK: error in backend: assembly parsing is not supported for MSIL

# The same file assembles for the native triple, so the error above comes from
# the MSIL object format and not from the input.
# RUN: %if x86-registered-target %{ %clang -cc1as -triple x86_64-unknown-linux-gnu -filetype null %s %}

  nop
