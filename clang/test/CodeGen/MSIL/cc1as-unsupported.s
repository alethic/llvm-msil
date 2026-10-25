# The MC assembly parser has no MSIL dialect (ImplementationPlan.md M11.c,
# item 6 of the upstream-merge follow-ups): the IsMSIL case that the fork adds
# to the object-file switch of the AsmParser constructor
# (llvm/lib/MC/MCParser/AsmParser.cpp) reports a usage error. Without it the
# switch leaves the platform parser null and the constructor dereferences it.
# 'clang -cc1as' with an MSIL triple reaches that constructor; the driver
# never does (it rejects assembling for MSIL triples, see
# clang/test/Driver/msil-toolchain.c). Every MSIL triple of plan decision D6
# takes the same path, whether assembly output (-filetype asm) or none
# (-filetype null) is asked for, and the result is a clean error, not a crash.
# From M18 the target registers MSILAsmBackend and MSILMCCodeEmitter, so
# '-filetype obj' passes the assertion of cc1as that the target has an
# MCAsmBackend and reaches the same IsMSIL error. M18's gate step 20 checks
# that on the four D6 triples, and M19 adds the test lines for it here.

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

# The same input assembles for the native triple, so the error above comes
# from the MSIL object format and not from the input.
# RUN: %if x86-registered-target %{ %clang -cc1as -triple x86_64-unknown-linux-gnu -filetype null %s %}

  nop
