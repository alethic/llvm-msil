; Phase 1 emits ILAsm text only (Design.md 5.5): -filetype=obj is a clean error
; with exit code 1 that tells the user to use -S, never a crash or an empty
; object file (MSILTargetMachine::createMCStreamer, ImplementationPlan.md M2.a).
; -filetype=null still runs the whole pipeline without output.

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %s -o %t.lnx.o 2>&1 \
; RUN:   | FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %s -o %t.win.o 2>&1 \
; RUN:   | FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %s -o %t.a64.o 2>&1 \
; RUN:   | FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: not llc -march=msil -mtriple=x86_64-w64-windows-gnu -filetype=obj -verify-machineinstrs < %s -o %t.mingw.o 2>&1 \
; RUN:   | FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %s -o %t.o0.o 2>&1 \
; RUN:   | FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"

; CHECK: error: MSIL object file emission is not implemented; use -S (ILAsm text)

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=null -verify-machineinstrs < %s 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=null -verify-machineinstrs < %s 2>&1 \
; RUN:   | count 0

source_filename = "obj.c"

declare i32 @f(i32)
