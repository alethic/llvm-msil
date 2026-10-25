; Token operand classes and their two spellings (ImplementationPlan.md M18.a
; (2), (4) and (5); plan 5.8.3 R1-R3 and R7, 5.8.4). The class of an operand
; depends on the IR alone:
; - Method: a function defined in the module ('f', the internal 'helper'),
;   and a declaration with msil-import-module or msil-import-name ('imp'),
;   which is its import stub in this module (R7);
; - ExternMethod: any other declaration ('puts', a Phase-1 import) and a
;   library call ('sqrt', from llvm.sqrt);
; - Field: a defined global, common included ('g', 'c');
; - FnAddr: the address of any function, defined or not (FUNC_ADDR; R2);
; - StandAloneSig: the signature of calli;
; - BCLMember: a member of llvm/include/llvm/CLI/BCLMembers.def.
; (ExternField, a declared or aliased global, exists only in object mode,
; which M19 tests.)
; In whole-program form, the text of llc and -S, every class prints as Phase
; 1 did and the extern kinds print like their own-row counterparts. Under the
; InstPrinter option object-form (llc -M object-form, for M19's object-form
; text) an ExternMethod names the placeholder [msil.extern]'<msil>Symbols',
; a function address the placeholder [msil.extern]'<msil>FnAddr' and a BCL
; member the scope [msil.bcl]; Method, Field and StandAloneSig operands name
; rows of the object itself and print as before. The fixup comments of
; -show-mc-encoding print the whole-program form in both cases. Object-form
; text names placeholder scopes that this module does not declare, so it is
; not assembled.

; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -asm-verbose=false \
; RUN:   | FileCheck %s --check-prefixes=CHECK,WP --implicit-check-not=msil.extern --implicit-check-not=msil.bcl
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -asm-verbose=false \
; RUN:   | FileCheck %s --check-prefixes=CHECK,WP --implicit-check-not=msil.extern --implicit-check-not=msil.bcl
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -asm-verbose=false -M object-form \
; RUN:   | FileCheck %s --check-prefixes=CHECK,OBJ
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -asm-verbose=false -M object-form \
; RUN:   | FileCheck %s --check-prefixes=CHECK,OBJ
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -M object-form -show-mc-encoding \
; RUN:   | FileCheck %s --check-prefix=ENC

@g = global i32 1
@c = common global i32 0, align 4

declare i32 @puts(ptr)
declare i32 @imp(i32) "msil-import-module"="mylib" "msil-import-name"="entry"

; CHECK-LABEL: .method assembly static int32 'helper'(int32) cil managed
define internal i32 @helper(i32 %x) {
  ret i32 %x
}

; CHECK-LABEL: .method public static int32 'f'(int32) cil managed
; CHECK:      ldarg.0
; CHECK-NEXT: call int32 'helper'(int32)
; CHECK-NEXT: ret
; ENC-LABEL:  .method public static int32 'f'(int32) cil managed
; ENC:        call int32 'helper'(int32) // encoding: [0x28,A,A,A,A]
; ENC-NEXT:   // fixup A - offset: 1, value: int32 'helper'(int32), kind: fixup_cil_method_token
define i32 @f(i32 %x) {
  %r = call i32 @helper(i32 %x)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'calls'(
; CHECK:      ldc.i4.1
; CHECK-NEXT: call int32 'f'(int32)
; CHECK-NEXT: ldarg.0
; WP-NEXT:    call int32 'puts'(native int)
; OBJ-NEXT:   call int32 [msil.extern]'<msil>Symbols'::'puts'(native int)
; CHECK-NEXT: add
; CHECK-NEXT: ldc.i4.3
; CHECK-NEXT: call int32 'imp'(int32)
; CHECK-NEXT: add
; CHECK-NEXT: ldc.i4.4
; CHECK-NEXT: ldarg.1
; CHECK-NEXT: calli int32(int32)
; CHECK-NEXT: add
; CHECK-NEXT: ret
; ENC-LABEL:  .method {{.*}}'calls'(
; ENC:        call int32 'f'(int32) // encoding: [0x28,A,A,A,A]
; ENC-NEXT:   // fixup A - offset: 1, value: int32 'f'(int32), kind: fixup_cil_method_token
; ENC-NEXT:   ldarg.0 // encoding: [0x02]
; ENC-NEXT:   call int32 [msil.extern]'<msil>Symbols'::'puts'(native int) // encoding: [0x28,A,A,A,A]
; ENC-NEXT:   // fixup A - offset: 1, value: int32 'puts'(native int), kind: fixup_cil_method_token
; ENC-NEXT:   add // encoding: [0x58]
; ENC-NEXT:   ldc.i4.3 // encoding: [0x19]
; ENC-NEXT:   call int32 'imp'(int32) // encoding: [0x28,A,A,A,A]
; ENC-NEXT:   // fixup A - offset: 1, value: int32 'imp'(int32), kind: fixup_cil_method_token
; ENC-NEXT:   add // encoding: [0x58]
; ENC-NEXT:   ldc.i4.4 // encoding: [0x1a]
; ENC-NEXT:   ldarg.1 // encoding: [0x03]
; ENC-NEXT:   calli int32(int32) // encoding: [0x29,A,A,A,A]
; ENC-NEXT:   // fixup A - offset: 1, value: int32(int32), kind: fixup_cil_sig_token
define i32 @calls(ptr %p, ptr %fp) {
  %a = call i32 @f(i32 1)
  %b = call i32 @puts(ptr %p)
  %c = call i32 @imp(i32 3)
  %d = call i32 %fp(i32 4)
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %s3 = add i32 %s2, %d
  ret i32 %s3
}

; CHECK-LABEL: .method {{.*}}'data'(
; CHECK:      ldsfld native int 'g'
; CHECK-NEXT: ldind.i4
; CHECK-NEXT: ldsfld native int 'c'
; CHECK-NEXT: ldind.i4
; ENC-LABEL:  .method {{.*}}'data'(
; ENC:        ldsfld native int 'g' // encoding: [0x7e,A,A,A,A]
; ENC-NEXT:   // fixup A - offset: 1, value: native int 'g', kind: fixup_cil_field_token
; ENC-NEXT:   ldind.i4 // encoding: [0x4a]
; ENC-NEXT:   ldsfld native int 'c' // encoding: [0x7e,A,A,A,A]
; ENC-NEXT:   // fixup A - offset: 1, value: native int 'c', kind: fixup_cil_field_token
define i32 @data() {
  %a = load i32, ptr @g
  %b = load i32, ptr @c
  %s = add i32 %a, %b
  ret i32 %s
}

; CHECK-LABEL: .method {{.*}}'addr_f'(
; WP:         ldsfld native int '<msil>fn_f'
; OBJ:        ldsfld native int [msil.extern]'<msil>FnAddr'::'f'
; CHECK-NEXT: ret
; ENC-LABEL:  .method {{.*}}'addr_f'(
; ENC:        ldsfld native int [msil.extern]'<msil>FnAddr'::'f' // encoding: [0x7e,A,A,A,A]
; ENC-NEXT:   // fixup A - offset: 1, value: native int '<msil>fn_f', kind: fixup_cil_field_token
define ptr @addr_f() {
  ret ptr @f
}

; CHECK-LABEL: .method {{.*}}'addr_puts'(
; WP:         ldsfld native int '<msil>fn_puts'
; OBJ:        ldsfld native int [msil.extern]'<msil>FnAddr'::'puts'
; CHECK-NEXT: ret
; ENC-LABEL:  .method {{.*}}'addr_puts'(
; ENC:        ldsfld native int [msil.extern]'<msil>FnAddr'::'puts' // encoding: [0x7e,A,A,A,A]
; ENC-NEXT:   // fixup A - offset: 1, value: native int '<msil>fn_puts', kind: fixup_cil_field_token
define ptr @addr_puts() {
  ret ptr @puts
}

; CHECK-LABEL: .method {{.*}}'libcall'(
; CHECK:      ldarg.0
; WP-NEXT:    call float64 'sqrt'(float64)
; OBJ-NEXT:   call float64 [msil.extern]'<msil>Symbols'::'sqrt'(float64)
; CHECK-NEXT: ret
; ENC-LABEL:  .method {{.*}}'libcall'(
; ENC:        call float64 [msil.extern]'<msil>Symbols'::'sqrt'(float64) // encoding: [0x28,A,A,A,A]
; ENC-NEXT:   // fixup A - offset: 1, value: float64 'sqrt'(float64), kind: fixup_cil_method_token
define double @libcall(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'bcl'(
; WP:         call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()
; OBJ:        call void [msil.bcl]System.Threading.Interlocked::MemoryBarrier()
; CHECK-NEXT: ret
; ENC-LABEL:  .method {{.*}}'bcl'(
; ENC:        call void [msil.bcl]System.Threading.Interlocked::MemoryBarrier() // encoding: [0x28,A,A,A,A]
; ENC-NEXT:   // fixup A - offset: 1, value: void [mscorlib]System.Threading.Interlocked::MemoryBarrier(), kind: fixup_cil_method_token
define void @bcl() {
  fence seq_cst
  ret void
}

; The P/Invoke declarations are directives, not operands: whole-program text
; declares the stubs of the imports that calls and addresses reach.
; WP-DAG: .method public hidebysig static pinvokeimpl("{{ucrtbase|libc}}" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}
; WP-DAG: .method public hidebysig static pinvokeimpl("mylib" as "entry" nomangle cdecl) int32 'imp'(int32) cil managed preservesig {}
; WP-DAG: .method public hidebysig static pinvokeimpl("{{ucrtbase|libm.so.6}}" nomangle cdecl) float64 'sqrt'(float64) cil managed preservesig {}
