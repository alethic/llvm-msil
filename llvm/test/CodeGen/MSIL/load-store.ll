; Scalar loads and stores (ImplementationPlan.md M7.a, plan 5.4 'Memory'; recon
; isel.md 5.5): the address is one native int operand with no offset
; immediate, pushed before the stored value. int32 is ldind.i4/stind.i4; i64
; and pointers are native int (decision D2), so they are ldind.i/stind.i and
; never ldind.i8/stind.i8; float and double are ldind.r4/ldind.r8 and
; stind.r4/stind.r8, and half and bfloat are the 16-bit integer accesses
; ldind.u2/stind.i2 of their bits. An i8 or i16 result that is zero- or
; sign-extended is ldind.u1/ldind.u2 or ldind.i1/ldind.i2, and an i8 or i16
; store of an int32 is stind.i1/stind.i2 (extending loads and truncating
; stores in general are in ext-loads.ll and trunc-stores.ll, prefixes in
; load-store-unaligned.ll and volatile.ll). isLegalAddressingMode accepts [reg] only (plan 5.3), so a
; constant offset is an explicit 'add' of a native int constant. A load is
; never moved across a store that may alias it: RegStackify keeps the loaded
; value in a local instead. i128 is two native int halves (low half at +0);
; a returned i128 does not fit one CIL return value, so CanLowerReturn demotes
; it to a hidden sret pointer, which is the first parameter of a method that
; returns void, and the two halves are stored through it (plan section 12, row
; 'M7.c extended: load-store.ll also returns an i128'). No function here has a
; frame (frame-index.ll has the stack-slot cases, frame-overalign.ll the caller
; of an i128 function).
;
; Single-operation functions compile to the same CIL at -O0 and -O2 (CHECK);
; where instruction order or constant placement may differ, the body is exact
; at -O2 (O2) and checked by opcode at -O0 (O0). Every module assembles.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=ldind.i8 --implicit-check-not=stind.i8 \
; DEFINE:   --implicit-check-not=ldind.u8 --implicit-check-not=int64 --implicit-check-not=localloc \
; DEFINE:   --implicit-check-not=unaligned. --implicit-check-not=volatile. --implicit-check-not=ldobj \
; DEFINE:   --implicit-check-not=stobj
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.lnx.o2.il
; RUN: %{check} --check-prefixes=CHECK,O2 --input-file=%t.lnx.o2.il
; RUN: FileCheck %s --check-prefix=SIG --input-file=%t.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.win.o2.il
; RUN: %{check} --check-prefixes=CHECK,O2 --input-file=%t.win.o2.il
; RUN: FileCheck %s --check-prefix=SIG --input-file=%t.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o0.il
; RUN: %{check} --check-prefixes=CHECK,O0 --input-file=%t.lnx.o0.il
; RUN: FileCheck %s --check-prefix=SIG --input-file=%t.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o0.il
; RUN: %{check} --check-prefixes=CHECK,O0 --input-file=%t.win.o0.il
; RUN: FileCheck %s --check-prefix=SIG --input-file=%t.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o2.dll %t.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o2.dll %t.win.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o0.dll %t.lnx.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

; Method headers: parameter and result types (D2: i64 and pointers are native
; int, narrow integers int32), and the hidden sret pointer of an i128 result.
; SIG:      {{^}}.method public static int32 'load_i32'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static native int 'load_i64'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static native int 'load_ptr'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static float32 'load_f32'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static float64 'load_f64'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'load_u8'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'load_s8'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'load_u16'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'load_s16'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'load_i1'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_i32'(native int, int32) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_i64'(native int, native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_ptr'(native int, native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_f32'(native int, float32) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_f64'(native int, float64) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_i8'(native int, int32) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_i16'(native int, int32) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_i1'(native int, int32) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_imm_i32'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_imm_f64'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_null'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'load_const_addr'() cil managed{{$}}
; SIG:      {{^}}.method public static void 'copy_i32'(native int, native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'copy_f64'(native int, native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'copy_half'(native int, native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'copy_bfloat'(native int, native int) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'load_load'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'load_offset'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_offset'(native int, float64) cil managed{{$}}
; SIG:      {{^}}.method public static native int 'load_neg_offset'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'load_index'(native int, native int) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'store_then_load'(native int, native int, int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'load_then_store'(native int, native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'store_i128'(native int, native int, native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'copy_i128'(native int, native int) cil managed{{$}}
; SIG:      {{^}}.method public static native int 'load_i128_lo'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'ret_i128_const'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static void 'rv'(native int, native int, native int) cil managed{{$}}
; SIG-NOT:  .method

source_filename = "load-store.c"

; ---- Loads of every width ----

; CHECK-LABEL: .method {{.*}}'load_i32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'load_i32'{{$}}
define i32 @load_i32(ptr %p) {
  %v = load i32, ptr %p
  ret i32 %v
}

; CHECK-LABEL: .method {{.*}}'load_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'load_i64'{{$}}
define i64 @load_i64(ptr %p) {
  %v = load i64, ptr %p
  ret i64 %v
}

; CHECK-LABEL: .method {{.*}}'load_ptr'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @load_ptr(ptr %p) {
  %v = load ptr, ptr %p
  ret ptr %v
}

; CHECK-LABEL: .method {{.*}}'load_f32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @load_f32(ptr %p) {
  %v = load float, ptr %p
  ret float %v
}

; CHECK-LABEL: .method {{.*}}'load_f64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @load_f64(ptr %p) {
  %v = load double, ptr %p
  ret double %v
}

; A zeroext or signext narrow result is the extending load itself.
; CHECK-LABEL: .method {{.*}}'load_u8'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i8 @load_u8(ptr %p) {
  %v = load i8, ptr %p
  ret i8 %v
}

; CHECK-LABEL: .method {{.*}}'load_s8'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i8 @load_s8(ptr %p) {
  %v = load i8, ptr %p
  ret i8 %v
}

; CHECK-LABEL: .method {{.*}}'load_u16'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i16 @load_u16(ptr %p) {
  %v = load i16, ptr %p
  ret i16 %v
}

; CHECK-LABEL: .method {{.*}}'load_s16'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i16 @load_s16(ptr %p) {
  %v = load i16, ptr %p
  ret i16 %v
}

; An i1 in memory is a byte holding 0 or 1, read with a byte load (any
; masking after it is allowed).
; CHECK-LABEL: .method {{.*}}'load_i1'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldind\.u1|ldind\.i1}}{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'load_i1'{{$}}
define zeroext i1 @load_i1(ptr %p) {
  %v = load i1, ptr %p
  ret i1 %v
}

; ---- Stores of every width: the address first, then the value ----

; CHECK-LABEL: .method {{.*}}'store_i32'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'store_i32'{{$}}
define void @store_i32(ptr %p, i32 %v) {
  store i32 %v, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'store_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_i64(ptr %p, i64 %v) {
  store i64 %v, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'store_ptr'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_ptr(ptr %p, ptr %v) {
  store ptr %v, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'store_f32'(
; CHECK-SAME:  native int, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_f32(ptr %p, float %v) {
  store float %v, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'store_f64'(
; CHECK-SAME:  native int, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_f64(ptr %p, double %v) {
  store double %v, ptr %p
  ret void
}

; An i8 or i16 value is an int32 whose low bits are stored; no mask or
; conversion is needed in front of stind.i1/stind.i2.
; CHECK-LABEL: .method {{.*}}'store_i8'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_i8(ptr %p, i8 %v) {
  store i8 %v, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'store_i16'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_i16(ptr %p, i16 %v) {
  store i16 %v, ptr %p
  ret void
}

; An i1 is stored as one byte.
; CHECK-LABEL: .method {{.*}}'store_i1'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK:       {{^}}  stind.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_i1(ptr %p, i1 zeroext %v) {
  store i1 %v, ptr %p
  ret void
}

; ---- Constant values and constant addresses ----

; CHECK-LABEL: .method {{.*}}'store_imm_i32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 42{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_imm_i32(ptr %p) {
  store i32 42, ptr %p
  ret void
}

; A stored FP constant becomes an integer store of its bit pattern before
; legalization, at -O0 as well as -O2: the target-independent
; DAGCombiner::visitSTORE -> replaceStoreOfFPConstant rewrites 'store double C'
; into 'store i64 <bits of C>' (and 'store float C' into 'store i32 <bits>',
; ldc.i4 <bits> / stind.i4) because i64 is a legal type. The same 8 bytes
; reach memory: 1.5 is 0x3FF8000000000000 = 4609434218613702656, a native int
; constant (D2) stored with stind.i. stind.r8 of a variable value is store_f64.
; CHECK-LABEL: .method {{.*}}'store_imm_f64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 4609434218613702656{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_imm_f64(ptr %p) {
  store double 1.5, ptr %p
  ret void
}

; A null pointer is a native int 0.
; CHECK-LABEL: .method {{.*}}'store_null'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.0|ldc\.i4 0}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_null(ptr %p) {
  store ptr null, ptr %p
  ret void
}

; A constant address is a native int constant.
; CHECK-LABEL: .method {{.*}}'load_const_addr'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 4096{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @load_const_addr() {
  %v = load i32, ptr inttoptr (i64 4096 to ptr)
  ret i32 %v
}

; ---- Loads feeding stores ----

; CHECK-LABEL: .method {{.*}}'copy_i32'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @copy_i32(ptr %dst, ptr %src) {
  %v = load i32, ptr %src
  store i32 %v, ptr %dst
  ret void
}

; CHECK-LABEL: .method {{.*}}'copy_f64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  stind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @copy_f64(ptr %dst, ptr %src) {
  %v = load double, ptr %src
  store double %v, ptr %dst
  ret void
}

; half and bfloat have no CIL type: a load or store of either is a 16-bit
; integer access of its bits (ldind.u2, stind.i2), with no conversion and no
; library call. Extending loads from them and truncating stores to them are
; Expand (M7.b), so loading one and storing it is a plain 2-byte copy; the
; conversions themselves are in ext-loads.ll and trunc-stores.ll.
; CHECK-LABEL: .method {{.*}}'copy_half'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.u2{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'copy_half'{{$}}
define void @copy_half(ptr %src, ptr %dst) {
  %v = load half, ptr %src
  store half %v, ptr %dst
  ret void
}

; CHECK-LABEL: .method {{.*}}'copy_bfloat'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.u2{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'copy_bfloat'{{$}}
define void @copy_bfloat(ptr %src, ptr %dst) {
  %v = load bfloat, ptr %src
  store bfloat %v, ptr %dst
  ret void
}

; A pointer loaded from memory is dereferenced in turn.
; CHECK-LABEL: .method {{.*}}'load_load'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @load_load(ptr %pp) {
  %p = load ptr, ptr %pp
  %v = load i32, ptr %p
  ret i32 %v
}

; ---- Offsets are explicit native int additions ----

; CHECK-LABEL: .method {{.*}}'load_offset'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.8|ldc\.i4 8}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @load_offset(ptr %p) {
  %q = getelementptr inbounds i8, ptr %p, i64 8
  %v = load i32, ptr %q
  ret i32 %v
}

; CHECK-LABEL: .method {{.*}}'store_offset'(
; CHECK-SAME:  native int, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 24|ldc\.i4 24}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_offset(ptr %p, double %v) {
  %q = getelementptr inbounds double, ptr %p, i64 3
  store double %v, ptr %q
  ret void
}

; A negative offset.
; CHECK-LABEL: .method {{.*}}'load_neg_offset'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s -8|ldc\.i4 -8}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @load_neg_offset(ptr %p) {
  %q = getelementptr inbounds i8, ptr %p, i64 -8
  %v = load i64, ptr %q
  ret i64 %v
}

; A variable index is scaled and added; the load reads the sum.
; CHECK-LABEL: .method {{.*}}'load_index'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  shl{{$}}
; CHECK:       {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @load_index(ptr %p, i64 %i) {
  %q = getelementptr inbounds i32, ptr %p, i64 %i
  %v = load i32, ptr %q
  ret i32 %v
}

; ---- Memory order ----

; A load after a store to a pointer that may alias it stays after the store.
; CHECK-LABEL: .method {{.*}}'store_then_load'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @store_then_load(ptr %p, ptr %q, i32 %v) {
  store i32 %v, ptr %q
  %r = load i32, ptr %p
  ret i32 %r
}

; A load whose only use follows a store is not sunk below the store into the
; 'ret': its value waits in an int32 local.
; CHECK-LABEL: .method {{.*}}'load_then_store'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @load_then_store(ptr %p, ptr %q) {
  %v = load i32, ptr %p
  store i32 7, ptr %q
  ret i32 %v
}

; ---- i128: two native int halves, the high half at +8 ----
; The two halves may be accessed in either order; each CHECK-DAG below matches
; one whole access (address, value, opcode) as consecutive lines.

; CHECK-LABEL: .method {{.*}}'store_i128'(
; CHECK-SAME:  native int, native int, native int) cil managed{{$}}
; CHECK-DAG:   {{^  ldarg\.0[[:space:]]+ldarg\.1[[:space:]]+stind\.i$}}
; CHECK-DAG:   {{^  ldarg\.0[[:space:]]+(ldc\.i4\.8|ldc\.i4 8)[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+ldarg\.2[[:space:]]+stind\.i$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'store_i128'{{$}}
define void @store_i128(ptr %p, i128 %v) {
  store i128 %v, ptr %p
  ret void
}

; Copying an i128 loads and stores both halves (in an order that depends on
; scheduling and stackification, so only the opcodes are counted).
; CHECK-LABEL: .method {{.*}}'copy_i128'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-DAG:   {{^}}  ldind.i{{$}}
; CHECK-DAG:   {{^}}  ldind.i{{$}}
; CHECK-DAG:   {{^}}  stind.i{{$}}
; CHECK-DAG:   {{^}}  stind.i{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'copy_i128'{{$}}
define void @copy_i128(ptr %dst, ptr %src) {
  %v = load i128, ptr %src
  store i128 %v, ptr %dst
  ret void
}

; The low half of an i128 is the native int at +0.
; CHECK-LABEL: .method {{.*}}'load_i128_lo'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @load_i128_lo(ptr %p) {
  %v = load i128, ptr %p
  %lo = trunc i128 %v to i64
  ret i64 %lo
}

; A returned i128 is demoted to a hidden sret pointer, the first parameter; the
; method returns void and stores the low half at +0 and the high half at +8.
; At -O2 each store reads the pointer argument directly; at -O0 the pointer
; may first be copied to a local, so only the two stores are checked.
; CHECK-LABEL: .method {{.*}}'ret_i128_const'(
; CHECK-SAME:  native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-DAG:      {{^  ldarg\.0[[:space:]]+(ldc\.i4\.1|ldc\.i4 1)[[:space:]]+conv\.i[[:space:]]+stind\.i$}}
; O2-DAG:      {{^  ldarg\.0[[:space:]]+(ldc\.i4\.8|ldc\.i4 8)[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+(ldc\.i4\.0|ldc\.i4 0)[[:space:]]+conv\.i[[:space:]]+stind\.i$}}
; O0-DAG:      {{^}}  stind.i{{$}}
; O0-DAG:      {{^}}  stind.i{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_i128_const'{{$}}
define i128 @ret_i128_const() {
  ret i128 1
}

; CHECK-LABEL: .method {{.*}}'rv'(
; CHECK-SAME:  native int, native int, native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-DAG:      {{^  ldarg\.0[[:space:]]+ldarg\.1[[:space:]]+stind\.i$}}
; O2-DAG:      {{^  ldarg\.0[[:space:]]+(ldc\.i4\.8|ldc\.i4 8)[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+ldarg\.2[[:space:]]+stind\.i$}}
; O0-DAG:      {{^}}  ldarg.1{{$}}
; O0-DAG:      {{^}}  ldarg.2{{$}}
; O0-DAG:      {{^}}  stind.i{{$}}
; O0-DAG:      {{^}}  stind.i{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'rv'{{$}}
define i128 @rv(i128 %x) {
  ret i128 %x
}
