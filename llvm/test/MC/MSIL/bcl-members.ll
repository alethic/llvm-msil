; The .NET members the backend uses, all from llvm/include/llvm/CLI/BCLMembers.def
; (ImplementationPlan.md M18.a (6), plan 5.11.7): System.ValueType (the base
; of the data image type) and Phase 1's five methods, Marshal::AllocHGlobal
; (the module initializer and Main), Marshal::StringToHGlobalAnsi and
; Environment::GetCommandLineArgs (Main's argv), Buffer::MemoryCopy
; (memmove: conv.i8, dup, call; decision D15) and Interlocked::MemoryBarrier
; (a fence; D36). printBCLMemberRef prints '<Return> [<Scope>]<Ns>.<Type>::
; <Name>(<Params>)'.
; - Whole-program text (llc, -S, plan 5.11.8) keeps Phase 1's text exactly,
;   with the [mscorlib] scope of the built-in table, and assembles.
; - Under the InstPrinter option object-form (llc -M object-form, which M19's
;   object-form text uses) the instructions name the unbound scope
;   [msil.bcl] instead (plan 5.8.3, 5.8.4); msil-ld binds it. Only operands
;   change: the option leaves the directives alone, so that output is not
;   assembled here.

; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -asm-verbose=false \
; RUN:   | FileCheck %s --check-prefixes=CHECK,WP --implicit-check-not=msil.bcl --implicit-check-not=msil.extern
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -asm-verbose=false \
; RUN:   | FileCheck %s --check-prefixes=CHECK,WP --implicit-check-not=msil.bcl --implicit-check-not=msil.extern
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -asm-verbose=false -M object-form \
; RUN:   | FileCheck %s --check-prefixes=CHECK,OBJ --implicit-check-not="[mscorlib]System.Runtime" \
; RUN:       --implicit-check-not="[mscorlib]System.Environment" --implicit-check-not="[mscorlib]System.Buffer" \
; RUN:       --implicit-check-not="[mscorlib]System.Threading"
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -asm-verbose=false -M object-form \
; RUN:   | FileCheck %s --check-prefixes=CHECK,OBJ --implicit-check-not="[mscorlib]System.Runtime" \
; RUN:       --implicit-check-not="[mscorlib]System.Environment" --implicit-check-not="[mscorlib]System.Buffer" \
; RUN:       --implicit-check-not="[mscorlib]System.Threading"
; RUN: %if ilasm %{ llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -o %t.il && %ilasm-verify -output=%t.dll %t.il %}

@g = global i32 3

; CHECK-LABEL: .method {{.*}}'move'(
; CHECK:      ldarg.2
; CHECK-NEXT: conv.i8
; CHECK-NEXT: dup
; WP-NEXT:    call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64)
; OBJ-NEXT:   call void [msil.bcl]System.Buffer::MemoryCopy(void*, void*, int64, int64)
; CHECK-NEXT: ret
define void @move(ptr %d, ptr %s, i64 %n) {
  call void @llvm.memmove.p0.p0.i64(ptr %d, ptr %s, i64 %n, i1 false)
  ret void
}

; CHECK-LABEL: .method {{.*}}'barrier'(
; CHECK:      .maxstack 0
; WP-NEXT:    call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()
; OBJ-NEXT:   call void [msil.bcl]System.Threading.Interlocked::MemoryBarrier()
; CHECK-NEXT: ret
define void @barrier() {
  fence seq_cst
  ret void
}

; CHECK-LABEL: .method {{.*}}'main'(
define i32 @main(i32 %argc, ptr %argv) {
  %v = load i32, ptr @g
  %r = add i32 %v, %argc
  ret i32 %r
}

; WP: .class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType

; CHECK-LABEL: .method {{.*}} .cctor()
; CHECK:      .maxstack 3
; CHECK-NEXT: ldc.i4.s 19
; CHECK-NEXT: conv.i
; WP-NEXT:    call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
; OBJ-NEXT:   call native int [msil.bcl]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)

; CHECK-LABEL: .method {{.*}}'<msil>Main'(
; CHECK:      .locals (string[], int32, native int, int32)
; WP-NEXT:    call string[] [mscorlib]System.Environment::GetCommandLineArgs()
; OBJ-NEXT:   call string[] [msil.bcl]System.Environment::GetCommandLineArgs()
; CHECK:      mul
; WP-NEXT:    call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
; OBJ-NEXT:   call native int [msil.bcl]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
; CHECK:      ldelem.ref
; WP-NEXT:    call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
; OBJ-NEXT:   call native int [msil.bcl]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
; CHECK:      call int32 'main'(int32, native int)
; CHECK-NEXT: ret
; CHECK-NEXT: } // end of method '<msil>Main'
