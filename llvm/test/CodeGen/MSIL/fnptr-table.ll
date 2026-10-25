; Function-pointer tables in data and dispatch through them (ImplementationPlan.md
; M9.a with the M8 data segment, decisions D12 and D18; plan 5.1 'Calls' and
; 'Globals' as amended by section 12 'Phase-1 function addresses' (2026-10-15,
; M9); Design.md 12 'a function-pointer dispatch table'). A function address
; in an initializer keeps zeros in the image bytes. Every function whose
; address is used, in data or in code, has one address field
; '.field private static initonly native int '<msil>fn_<IL name>'', declared
; after the address fields of the globals and their empty line, in module
; order, then one empty line. <Module>::.cctor sets each one once, in the same
; order, with 'ldftn <member-ref>' + 'stsfld', right after the address fields
; of the globals are set and before the relocations. Each relocation then
; stores 'ldsfld' of the function's field into its slot with 'stind.i', one
; relocation per slot in site order: for public and internal functions, for a
; P/Invoke import (whose ldftn names its pinvokeimpl declaration) in a
; constant table, and next to a data relocation in a struct; op_sub, in two
; slots, is set once and read twice. Code reads a slot with ldsfld of the
; table's field, an offset ('ldc.i4 <off>; add', or a scaled variable index)
; and 'ldind.i', and calls it with a managed 'calli' whose signature is the
; slot's function signature; the arguments are pushed before the slot is
; read. A slot written at run time gets the function's address field
; ('ldsfld') + 'stind.i'. 'ldftn' appears only in the .cctor, once per
; function. The image itself is never written (no '&(' data labels).
;
; Data layout (MaxAlign 16): ops +0 (three slots), out +24 (two slots, the
; second null), vt +40 ({ i32 3, pad, ptr, ptr }: the function at +48, the
; address of ops at +56); 64 initialized bytes, no zero tail.
;
; Straight-line functions compile to the same CIL at -O0 and -O2 (CHECK); the
; others are exact at -O2 (O2) and checked for the read and the calli at -O0
; (O0). The data segment and the .cctor do not depend on the level.
;
; Constructors (llvm.global_ctors, M8.c) run at the end of the .cctor, so
; every function address field is set before the first 'call void
; '<ctor>'()': a constructor that takes a function's address, or calls
; through a function pointer in data, must not read a field that is still
; zero (a calli through null; the swapped order crashes with an access
; violation on .NET 8). ctor.ll has no data segment: the .cctor sets the
; field first, then calls the constructor (CTOR). ctor-data.ll has one global
; that holds a function pointer: the data setup and the address field of the
; global come first, then the function fields, then the relocation, then the
; constructor call (CTORD). A constructor that the .cctor only calls is not
; "used" (section 12 'Phase-1 function addresses'): 'init' has no address
; field. Both triples, -O2 and -O0; the .cctor does not depend on the level.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=unmanaged \
; DEFINE:   --implicit-check-not="&(" --implicit-check-not="LLVM ERROR" --implicit-check-not=error: \
; DEFINE:   --implicit-check-not="{{^ +ldftn }}"
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O2,LNX
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O2,WIN
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O0,LNX
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O0,WIN
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t/main.ll -o %t/main.o2.il \
; RUN:   && %ilasm-verify -output=%t/main.o2.dll %t/main.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/main.ll -o %t/main.o0.il \
; RUN:   && %ilasm-verify -output=%t/main.o0.dll %t/main.o0.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/ctor.ll 2>&1 \
; RUN:   | %{check} --check-prefix=CTOR --implicit-check-not=".field "
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/ctor.ll 2>&1 \
; RUN:   | %{check} --check-prefix=CTOR --implicit-check-not=".field "
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/ctor.ll 2>&1 \
; RUN:   | %{check} --check-prefix=CTOR --implicit-check-not=".field "
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/ctor.ll 2>&1 \
; RUN:   | %{check} --check-prefix=CTOR --implicit-check-not=".field "
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %t/ctor.ll -o %t/ctor.o2.il \
; RUN:   && %ilasm-verify -output=%t/ctor.o2.dll %t/ctor.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/ctor.ll -o %t/ctor.o0.il \
; RUN:   && %ilasm-verify -output=%t/ctor.o0.dll %t/ctor.o0.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/ctor-data.ll 2>&1 \
; RUN:   | %{check} --check-prefix=CTORD --implicit-check-not=".field "
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/ctor-data.ll 2>&1 \
; RUN:   | %{check} --check-prefix=CTORD --implicit-check-not=".field "
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/ctor-data.ll 2>&1 \
; RUN:   | %{check} --check-prefix=CTORD --implicit-check-not=".field "
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/ctor-data.ll 2>&1 \
; RUN:   | %{check} --check-prefix=CTORD --implicit-check-not=".field "
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t/ctor-data.ll -o %t/ctor-data.o2.il \
; RUN:   && %ilasm-verify -output=%t/ctor-data.o2.dll %t/ctor-data.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/ctor-data.ll -o %t/ctor-data.o0.il \
; RUN:   && %ilasm-verify -output=%t/ctor-data.o0.dll %t/ctor-data.o0.il %}

;--- main.ll
source_filename = "fnptr-table.c"

@ops = global [3 x ptr] [ptr @op_add, ptr @op_sub, ptr @op_mul], align 16
@out = constant [2 x ptr] [ptr @putchar, ptr null], align 8
@vt = internal global { i32, ptr, ptr } { i32 3, ptr @op_sub, ptr @ops }, align 8

; CHECK: {{^}}.method public static [[BIN_RET:int32]] 'op_add'[[BIN_PARAMS:\(int32, int32\)]] cil managed{{$}}
define i32 @op_add(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

; CHECK: {{^}}.method public static int32 'op_sub'(int32, int32) cil managed{{$}}
define i32 @op_sub(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}

; CHECK: {{^}}.method assembly static int32 'op_mul'(int32, int32) cil managed{{$}}
define internal i32 @op_mul(i32 %a, i32 %b) {
  %r = mul i32 %a, %b
  ret i32 %r
}

declare i32 @putchar(i32)

; ops[i](a, b): the arguments, then the slot at ops + i * 8, then calli.
; CHECK-LABEL: .method {{.*}}'dispatch'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 5{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.3{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'ops'{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  calli [[BIN_RET]][[BIN_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'dispatch'{{$}}
define i32 @dispatch(i32 %i, i32 %a, i32 %b) {
  %idx = sext i32 %i to i64
  %p = getelementptr inbounds [3 x ptr], ptr @ops, i64 0, i64 %idx
  %f = load ptr, ptr %p, align 8
  %r = call i32 %f(i32 %a, i32 %b)
  ret i32 %r
}

; ops[2](a, b): a constant slot offset.
; CHECK-LABEL: .method {{.*}}'call_slot2'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'ops'{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4(\.s)? 16$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  calli [[BIN_RET]][[BIN_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_slot2(i32 %a, i32 %b) {
  %p = getelementptr inbounds [3 x ptr], ptr @ops, i64 0, i64 2
  %f = load ptr, ptr %p, align 8
  %r = call i32 %f(i32 %a, i32 %b)
  ret i32 %r
}

; vt.fn(a, vt.n): a function pointer in a struct, at vt + 8.
; CHECK-LABEL: .method {{.*}}'call_vt'(
; CHECK-SAME:  int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 4{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'vt'{{$}}
; O2-NEXT:     {{^}}  ldind.i4{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'vt'{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.8$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldind.i{{$}}
; O0:          {{^}}  ldind.i4{{$}}
; O0:          {{^}}  ldloc.{{[0-9]}}{{$}}
; CHECK-NEXT:  {{^}}  calli [[BIN_RET]][[BIN_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_vt(i32 %a) {
  %pf = getelementptr inbounds i8, ptr @vt, i64 8
  %f = load ptr, ptr %pf, align 8
  %n = load i32, ptr @vt, align 8
  %r = call i32 %f(i32 %a, i32 %n)
  ret i32 %r
}

; out[0](c): a slot of a constant table that holds a P/Invoke import.
; CHECK-LABEL: .method {{.*}}'emit'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'out'{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @emit(i32 %c) {
  %f = load ptr, ptr @out, align 8
  %r = call i32 %f(i32 %c)
  ret i32 %r
}

; ops[0] = op_mul at run time: the address is op_mul's field.
; CHECK-LABEL: .method {{.*}}'set_slot0'(
; CHECK-SAME:  ) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'ops'{{$}}
; O2-NEXT:     {{^}}  ldsfld native int '<msil>fn_op_mul'{{$}}
; O0:          {{^}}  ldsfld native int '<msil>fn_op_mul'{{$}}
; O0:          {{^}}  ldsfld native int 'ops'{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @set_slot0() {
  store ptr @op_mul, ptr @ops, align 16
  ret void
}

; Every slot of ops in a loop: the arguments, the slot, calli, then the sum.
; CHECK-LABEL: .method {{.*}}'sum_all'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}@BB{{[0-9]+}}_1:{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldloc.1{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'ops'{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O0:          {{^}}  ldsfld native int 'ops'{{$}}
; O0-NEXT:     {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  calli [[BIN_RET]][[BIN_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}  bne.un.s @BB{{[0-9]+}}_1{{$}}
; CHECK:       {{^}}} // end of method 'sum_all'{{$}}
define i32 @sum_all(i32 %a, i32 %b) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %sum, %loop ]
  %p = getelementptr inbounds [3 x ptr], ptr @ops, i64 0, i64 %i
  %f = load ptr, ptr %p, align 8
  %r = call i32 %f(i32 %a, i32 %b)
  %sum = add i32 %acc, %r
  %next = add i64 %i, 1
  %done = icmp eq i64 %next, 3
  br i1 %done, label %exit, label %loop
exit:
  ret i32 %sum
}

; ---- module trailer ---------------------------------------------------------

; CHECK:       {{^}}// ---- undefined external functions: P/Invoke ----{{$}}
; LNX-NEXT:    {{^}}.method public hidebysig static pinvokeimpl("libc" nomangle cdecl) int32 'putchar'(int32) cil managed preservesig {}{{$}}
; WIN-NEXT:    {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" nomangle cdecl) int32 'putchar'(int32) cil managed preservesig {}{{$}}

; Every slot of the image that holds a function address is zero; only the
; i32 of vt is data.
; CHECK:       {{^}}// ---- data segment: 64 bytes (64 initialized + 0 zero), align 16 ----{{$}}
; CHECK:       {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; CHECK-NEXT:  {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   // 0x0000{{$}}
; CHECK-NEXT:  {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   // 0x0010{{$}}
; CHECK-NEXT:  {{^}}  00 00 00 00 00 00 00 00 03 00 00 00 00 00 00 00   // 0x0020{{$}}
; CHECK-NEXT:  {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ) // 0x0030{{$}}
; CHECK:       {{^}}.field public static initonly native int 'ops'{{$}}
; CHECK-NEXT:  {{^}}.field public static initonly native int 'out'{{$}}
; CHECK-NEXT:  {{^}}.field assembly static initonly native int 'vt'{{$}}
; CHECK-EMPTY:

; The function address fields, one per used function in module order (the
; definitions, then the putchar import), then one empty line.
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_op_add'{{$}}
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_op_sub'{{$}}
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_op_mul'{{$}}
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_putchar'{{$}}
; CHECK-EMPTY:
; CHECK-NEXT:  {{^}}.method {{.*}} .cctor() cil managed{{$}}

; After the address fields of the globals: each function field once, with the
; ldftn of the method's signature (putchar's is its P/Invoke declaration's);
; then the relocations, in site order, which read those fields.
; CHECK:       {{^}}  stsfld native int 'vt'{{$}}
; CHECK-NEXT:  {{^}}  ldftn [[BIN_RET]] 'op_add'[[BIN_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_op_add'{{$}}
; CHECK-NEXT:  {{^}}  ldftn int32 'op_sub'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_op_sub'{{$}}
; CHECK-NEXT:  {{^}}  ldftn int32 'op_mul'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_op_mul'{{$}}
; CHECK-NEXT:  {{^}}  ldftn int32 'putchar'(int32){{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_putchar'{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>data'{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_op_add'{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>data'{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.8$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_op_sub'{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>data'{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4(\.s)? 16$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_op_mul'{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>data'{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4(\.s)? 24$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_putchar'{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>data'{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4(\.s)? 48$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_op_sub'{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>data'{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4(\.s)? 56$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'ops'{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '.cctor'{{$}}

;--- ctor.ll
; No data segment: init takes add1's address (through a volatile slot, as
; clang emits 'int (*volatile p)(int) = add1;') and calls through it. The
; .cctor sets add1's field first and then calls init.
source_filename = "ctor.c"

@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @init, ptr null }]

; CTOR:      {{^}}.method assembly static int32 'add1'(int32) cil managed{{$}}
define internal i32 @add1(i32 %x) {
  %r = add i32 %x, 1
  ret i32 %r
}

; CTOR-LABEL: .method assembly static void 'init'() cil managed{{$}}
; CTOR:       {{^}}  ldsfld native int '<msil>fn_add1'{{$}}
; CTOR:       {{^}}  calli int32(int32){{$}}
; CTOR:       {{^}}} // end of method 'init'{{$}}
define internal void @init() {
  %slot = alloca ptr, align 8
  store volatile ptr @add1, ptr %slot, align 8
  %f = load volatile ptr, ptr %slot, align 8
  %r = call i32 %f(i32 41)
  ret void
}

define i32 @main() {
  ret i32 0
}

; CTOR:      {{^}}.field private static initonly native int '<msil>fn_add1'{{$}}
; CTOR-EMPTY:
; CTOR-NEXT: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CTOR-NEXT: {{^\{$}}
; CTOR-NEXT: {{^}}  .maxstack 1{{$}}
; CTOR-NEXT: {{^}}  ldftn int32 'add1'(int32){{$}}
; CTOR-NEXT: {{^}}  stsfld native int '<msil>fn_add1'{{$}}
; CTOR-NEXT: {{^}}  call void 'init'(){{$}}
; CTOR-NEXT: {{^}}  ret{{$}}
; CTOR-NEXT: {{^}}} // end of method '.cctor'{{$}}

;--- ctor-data.ll
; One global, fp, holding add1 (a relocation); init takes twice's address
; (through a volatile slot) and calls through it, then calls through fp. The
; .cctor fills the segment and sets fp's field, sets the fields of add1 and
; twice (module order), applies the relocation, and only then calls init.
source_filename = "ctor-data.c"

@fp = global ptr @add1, align 8
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @init, ptr null }]

; CTORD:      {{^}}.method assembly static int32 'add1'(int32) cil managed{{$}}
define internal i32 @add1(i32 %x) {
  %r = add i32 %x, 1
  ret i32 %r
}

; CTORD:      {{^}}.method assembly static int32 'twice'(int32) cil managed{{$}}
define internal i32 @twice(i32 %x) {
  %r = shl i32 %x, 1
  ret i32 %r
}

; CTORD-LABEL: .method assembly static void 'init'() cil managed{{$}}
; CTORD:       {{^}}  ldsfld native int '<msil>fn_twice'{{$}}
; CTORD:       {{^}}  calli int32(int32){{$}}
; CTORD-NEXT:  {{^}}  ldsfld native int 'fp'{{$}}
; CTORD-NEXT:  {{^}}  ldind.i{{$}}
; CTORD-NEXT:  {{^}}  calli int32(int32){{$}}
; CTORD:       {{^}}} // end of method 'init'{{$}}
define internal void @init() {
  %slot = alloca ptr, align 8
  store volatile ptr @twice, ptr %slot, align 8
  %g = load volatile ptr, ptr %slot, align 8
  %a = call i32 %g(i32 20)
  %f = load ptr, ptr @fp, align 8
  %r = call i32 %f(i32 %a)
  ret void
}

define i32 @main() {
  ret i32 0
}

; CTORD:      {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; CTORD-NEXT: {{^}}.field private static initonly native int '<msil>data'{{$}}
; CTORD-NEXT: {{^}}.field public static initonly native int 'fp'{{$}}
; CTORD-EMPTY:
; CTORD-NEXT: {{^}}.field private static initonly native int '<msil>fn_add1'{{$}}
; CTORD-NEXT: {{^}}.field private static initonly native int '<msil>fn_twice'{{$}}
; CTORD-EMPTY:
; CTORD-NEXT: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CTORD:      {{^}}  stsfld native int '<msil>data'{{$}}
; CTORD:      {{^}}  cpblk{{$}}
; CTORD-NEXT: {{^}}  ldsfld native int '<msil>data'{{$}}
; CTORD-NEXT: {{^}}  stsfld native int 'fp'{{$}}
; CTORD-NEXT: {{^}}  ldftn int32 'add1'(int32){{$}}
; CTORD-NEXT: {{^}}  stsfld native int '<msil>fn_add1'{{$}}
; CTORD-NEXT: {{^}}  ldftn int32 'twice'(int32){{$}}
; CTORD-NEXT: {{^}}  stsfld native int '<msil>fn_twice'{{$}}
; CTORD-NEXT: {{^}}  ldsfld native int '<msil>data'{{$}}
; CTORD-NEXT: {{^}}  ldsfld native int '<msil>fn_add1'{{$}}
; CTORD-NEXT: {{^}}  stind.i{{$}}
; CTORD-NEXT: {{^}}  call void 'init'(){{$}}
; CTORD-NEXT: {{^}}  ret{{$}}
; CTORD-NEXT: {{^}}} // end of method '.cctor'{{$}}
