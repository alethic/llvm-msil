// Phase 1 emits ILAsm text only (Design.md 5.5): 'clang -cc1 -emit-obj' on an
// MSIL triple, also reached through the driver's '-S -Xclang -emit-obj', is a
// clean error with exit code 1 that tells the user to use -S, never a crash.
// MSILTargetMachine::createMCStreamer refuses object files, the codegen
// pipeline reports that message, and clang reports 'unable to interface with
// target machine' for the failed pipeline and consumes its Error (section 12
// row 'fix-loop ownership: clang/lib/CodeGen/BackendUtil.cpp -> fix-6'); an
// unconsumed Error aborts with 'Program aborted due to an unhandled Error'.
// llvm/test/CodeGen/MSIL/obj-unsupported.ll checks the same message through
// llc. The translation unit has only a declaration, so -S succeeds on it.

// REQUIRES: msil-registered-target

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-obj %s -o %t.lnx.o 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="LLVM ERROR" \
// RUN:       --implicit-check-not="Program aborted"
// RUN: not %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-obj %s -o %t.win.o 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="LLVM ERROR" \
// RUN:       --implicit-check-not="Program aborted"
// RUN: not %clang --target=x86_64-unknown-linux-gnu-msil -S -Xclang -emit-obj %s -o %t.drv.o 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="LLVM ERROR" \
// RUN:       --implicit-check-not="Program aborted"

// CHECK: error: MSIL object file emission is not implemented; use -S (ILAsm text)
// CHECK: error: unable to interface with target machine

// The same translation unit compiles with -S.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S %s -o %t.il

int f(void);
