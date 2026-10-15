; Global addresses with constant offsets in code (ImplementationPlan.md M8.a
; and M8.c, plan 5.1, recon isel.md 5.7 and asmprinter.md 11): a constant
; offset from a global variable is folded into the address
; (isOffsetFoldingLegal is true for data), and GLOBAL_ADDR prints 'ldsfld
; native int '<g>'' followed by 'ldc.i4 <off>' and 'add' when the offset is
; not zero (any ldc.i4 encoding), or by 'ldc.i8 <off>', 'conv.i' and 'add'
; when the offset does not fit in int32. Array elements, struct fields (offset
; 0 included), addresses returned as values, negative offsets and the int32
; limits are covered. -O2 bodies are checked exactly; at -O0 only the field
; load and the memory access. Both triples, and every module assembles.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=".field " --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not=int64
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.lnx.il 2>&1 \
; RUN:   | count 0
; RUN: %{check} --check-prefixes=CHECK,O2 --input-file=%t.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.win.il 2>&1 \
; RUN:   | count 0
; RUN: %{check} --check-prefixes=CHECK,O2 --input-file=%t.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O0 --input-file=%t.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O0 --input-file=%t.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.dll %t.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

source_filename = "global-offsets.c"

; arr +0 (32 bytes), s +32 (16 bytes: i8, padding, i32, i64); big +48 is
; zero-initialized.
@arr = global [8 x i32] [i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7], align 16
@s = global { i8, i32, i64 } { i8 1, i32 2, i64 3 }, align 8
@big = global [16 x i8] zeroinitializer, align 16

; arr[3]: the offset is folded into the address.
; CHECK-LABEL: {{^}}.method public static int32 'load_arr3'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'arr'{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4(\.s)? 12$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldind.i4{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'arr'{{$}}
; O0:          {{^}}  ldind.i4{{$}}
; CHECK:       {{^}}} // end of method 'load_arr3'{{$}}
define i32 @load_arr3() {
  %v = load i32, ptr getelementptr inbounds ([8 x i32], ptr @arr, i64 0, i64 3), align 4
  ret i32 %v
}

; s.c (offset 8).
; CHECK-LABEL: {{^}}.method public static void 'store_s2'(native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 's'{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4(\.| |\.s )8$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  stind.i{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 's'{{$}}
; O0:          {{^}}  stind.i{{$}}
; CHECK:       {{^}}} // end of method 'store_s2'{{$}}
define void @store_s2(i64 %v) {
  store i64 %v, ptr getelementptr inbounds ({ i8, i32, i64 }, ptr @s, i64 0, i32 2), align 8
  ret void
}

; s.b (offset 4).
; CHECK-LABEL: {{^}}.method public static int32 'load_s1'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 's'{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4(\.| |\.s )4$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldind.i4{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 's'{{$}}
; O0:          {{^}}  ldind.i4{{$}}
; CHECK:       {{^}}} // end of method 'load_s1'{{$}}
define i32 @load_s1() {
  %v = load i32, ptr getelementptr inbounds ({ i8, i32, i64 }, ptr @s, i64 0, i32 1), align 4
  ret i32 %v
}

; s.a (offset 0): no 'ldc.i4; add'.
; CHECK-LABEL: {{^}}.method public static int32 'load_s0'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 's'{{$}}
; O2-NEXT:     {{^}}  ldind.u1{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 's'{{$}}
; O0:          {{^}}  ldind.u1{{$}}
; CHECK:       {{^}}} // end of method 'load_s0'{{$}}
define i32 @load_s0() {
  %v = load i8, ptr @s, align 8
  %w = zext i8 %v to i32
  ret i32 %w
}

; &arr[2].
; CHECK-LABEL: {{^}}.method public static native int 'addr_arr2'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'arr'{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4(\.| |\.s )8$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'arr'{{$}}
; CHECK:       {{^}}} // end of method 'addr_arr2'{{$}}
define ptr @addr_arr2() {
  ret ptr getelementptr inbounds ([8 x i32], ptr @arr, i64 0, i64 2)
}

; A negative offset.
; CHECK-LABEL: {{^}}.method public static native int 'addr_neg'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'arr'{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4(\.s)? -4$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'arr'{{$}}
; CHECK:       {{^}}} // end of method 'addr_neg'{{$}}
define ptr @addr_neg() {
  ret ptr getelementptr (i8, ptr @arr, i64 -4)
}

; The largest and the smallest offsets that fit in int32 still use ldc.i4.
; CHECK-LABEL: {{^}}.method public static native int 'addr_max32'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'big'{{$}}
; O2-NEXT:     {{^}}  ldc.i4 2147483647{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'big'{{$}}
; CHECK:       {{^}}} // end of method 'addr_max32'{{$}}
define ptr @addr_max32() {
  ret ptr getelementptr (i8, ptr @big, i64 2147483647)
}

; CHECK-LABEL: {{^}}.method public static native int 'addr_min32'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'big'{{$}}
; O2-NEXT:     {{^}}  ldc.i4 -2147483648{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'big'{{$}}
; CHECK:       {{^}}} // end of method 'addr_min32'{{$}}
define ptr @addr_min32() {
  ret ptr getelementptr (i8, ptr @big, i64 -2147483648)
}

; Offsets outside int32: ldc.i8, conv.i, add.
; CHECK-LABEL: {{^}}.method public static native int 'addr_huge'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'big'{{$}}
; O2-NEXT:     {{^}}  ldc.i8 4294967296{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'big'{{$}}
; CHECK:       {{^}}} // end of method 'addr_huge'{{$}}
define ptr @addr_huge() {
  ret ptr getelementptr (i8, ptr @big, i64 4294967296)
}

; CHECK-LABEL: {{^}}.method public static native int 'addr_neg_huge'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'big'{{$}}
; O2-NEXT:     {{^}}  ldc.i8 -2147483649{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'big'{{$}}
; CHECK:       {{^}}} // end of method 'addr_neg_huge'{{$}}
define ptr @addr_neg_huge() {
  ret ptr getelementptr (i8, ptr @big, i64 -2147483649)
}

; A variable index starts from the field as well.
; CHECK-LABEL: {{^}}.method public static int32 'load_var'(native int) cil managed{{$}}
; CHECK:       {{^}}  ldsfld native int 'arr'{{$}}
; CHECK:       {{^}}  ldind.i4{{$}}
; CHECK:       {{^}}} // end of method 'load_var'{{$}}
define i32 @load_var(i64 %i) {
  %p = getelementptr inbounds [8 x i32], ptr @arr, i64 0, i64 %i
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

; Two offsets from the same global in one function.
; CHECK-LABEL:   {{^}}.method public static int32 'two_elems'() cil managed{{$}}
; O2:            {{^}}  ldsfld native int 'arr'{{$}}
; O2-NEXT:       {{^}}  {{ldc\.i4(\.| |\.s )(4|8)$}}
; O2-NEXT:       {{^}}  add{{$}}
; O2-NEXT:       {{^}}  ldind.i4{{$}}
; O2-NEXT:       {{^}}  ldsfld native int 'arr'{{$}}
; O2-NEXT:       {{^}}  {{ldc\.i4(\.| |\.s )(4|8)$}}
; O2-NEXT:       {{^}}  add{{$}}
; O2-NEXT:       {{^}}  ldind.i4{{$}}
; O2-NEXT:       {{^}}  add{{$}}
; O2-NEXT:       {{^}}  ret{{$}}
; O0-COUNT-2:    {{^}}  ldind.i4{{$}}
; CHECK:         {{^}}} // end of method 'two_elems'{{$}}
define i32 @two_elems() {
  %a = load i32, ptr getelementptr inbounds ([8 x i32], ptr @arr, i64 0, i64 1), align 4
  %b = load i32, ptr getelementptr inbounds ([8 x i32], ptr @arr, i64 0, i64 2), align 4
  %r = add i32 %a, %b
  ret i32 %r
}

; The trailer: arr +0 and s +32 in the image, big +48 in the zero tail.
; CHECK-NOT:     {{^}}.method {{(public|assembly) static}}
; CHECK:        {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; CHECK:        {{^ *}}.pack 1{{( |$)}}
; CHECK:        {{^ *}}.size 48{{( |$)}}
; CHECK:        {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; CHECK-NEXT:   {{^}}  00 00 00 00 01 00 00 00 02 00 00 00 03 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  04 00 00 00 05 00 00 00 06 00 00 00 07 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  01 00 00 00 02 00 00 00 03 00 00 00 00 00 00 00 ){{( |$)}}
; CHECK:        {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; CHECK:        {{^}}.field private static initonly native int '<msil>data'{{$}}
; CHECK:        {{^}}.field public static initonly native int 'arr'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 's'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'big'{{( |$)}}
; CHECK:        {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK-NEXT:   {{^\{$}}
; CHECK-NEXT:   {{^}}  .maxstack {{[3-9]}}{{$}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 79( |$)}}
; CHECK-NEXT:   {{^}}  conv.i{{( |$)}}
; CHECK-NEXT:   {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 15( |$)}}
; CHECK-NEXT:   {{^}}  conv.i{{( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? -16( |$)}}
; CHECK-NEXT:   {{^}}  conv.i{{( |$)}}
; CHECK-NEXT:   {{^}}  and{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; CHECK-NEXT:   {{^}}  cpblk{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )0( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; CHECK-NEXT:   {{^}}  initblk{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'arr'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 's'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'big'{{( |$)}}
; CHECK-NEXT:   {{^}}  ret{{( |$)}}
; CHECK-NEXT:   {{^}}} // end of method '.cctor'{{$}}
; CHECK-NOT:     {{^}}.method
