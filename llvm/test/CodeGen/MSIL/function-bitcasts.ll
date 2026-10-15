; Calls whose call-site type differs from the callee's type get wrappers
; (ImplementationPlan.md M9.b, decisions D18 and D27; Design.md 2.1; recon
; isel.md 2.5 and 8.2 'Direct-call consistency'), ported from
; llvm/test/CodeGen/WebAssembly/function-bitcasts.ll. MSILFixFunctionBitcasts
; is a copy of WebAssemblyFixFunctionBitcasts and runs at every optimization
; level: a direct call (or invoke) of a function F with another function type
; T calls a private wrapper '<F>_bitcast' (an 'assembly' method; a second
; wrapper type of the same F gets a '.<N>' suffix) whose signature is T and
; whose body calls F with F's own signature, so every 'call' token matches its
; callee's '.method' text (a mismatch would be a MissingMethodException at JIT
; time). The wrapper drops extra arguments, passes poison for missing ones
; (any value), drops an unwanted result, returns poison for a missing one, and
; casts arguments and results that are bit- or pointer-castable (ptr <-> i64,
; float <-> i32 through a frame slot, D16). Types that cannot be converted give
; '<F>_bitcast_invalid', whose body is 'unreachable' (ldnull; throw) instead of
; a compile-time error. The same T for the same F shares one wrapper. Calls
; whose type already matches, and addresses that are stored, passed or called
; through a pointer, use F itself (no wrapper): an address is F's address
; field '<msil>fn_<F>', which <Module>::.cctor sets with 'ldftn' of F's own
; signature (section 12 'Phase-1 function addresses', 2026-10-15, M9).
; Wrappers are appended to the module in the order of their callees.
;
; MSIL changes to the copy (M9.b): WebAssembly's main renaming is removed
; ('int main(void)' keeps its name and '<msil>Main' calls it, D27), and a K&R
; call with a variadic call type ('call i32 (i32, double, ...) @f(...)' for a
; non-variadic definition @f) becomes an ordinary fixed-arity call: no
; variadic wrapper and no 'calls to variadic functions' error (D19 applies to
; real variadic callees, varargs-call-error.ll). clang emits fixed-arity K&R
; calls for every MSIL triple since M9's CGCall.cpp change (section 12,
; 2026-10-15; clang/test/CodeGen/MSIL/no-proto-call.c), so the variadic shape
; comes from IR made by native x86-64 SysV clang (x86_64-unknown-linux-gnu,
; X86_64TargetCodeGenInfo::isNoProtoCallVariadic), or written by hand, and fed
; to llc with an MSIL triple. Every wrapper is valid IR, because it keeps a
; parameter attribute of the callee only where its parameter type is the
; callee's, and drops 'returned' and the return attributes when its result
; type differs (WebAssembly copies them all, and the IR verifier rejects the
; wrapper; attrs.ll). Struct-typed mismatches are not wrapped and stay
; LowerCall's 'mismatched call signature' error (call-errors.ll).
; A wrapper never carries 'naked' (naked.ll): a castable wrapper of a naked
; function has a real body that uses its arguments, which the IR verifier
; rejects in a naked function ('cannot use argument of naked function'), and
; an invalid wrapper would repeat the callee's error. A mismatched call of a
; naked function is one problem, reported once (plan section 2; as in
; unsupported-function-errors.ll): the callee's 'naked functions are not
; supported for MSIL', and nothing about its wrappers.
;
; The IR prefixes check the module after -stop-after=msil-fix-function-bitcasts;
; the IL prefixes check the methods. Poison arguments and results compile to
; any value, so only the calls around them are checked.

; DEFINE: %{il} = FileCheck %s --implicit-check-not=int64 --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning: --implicit-check-not="{{^ +ldftn }}"
; RUN: split-file %s %t

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll 2>&1 \
; RUN:   | %{il} --check-prefix=CHECK
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll 2>&1 \
; RUN:   | %{il} --check-prefix=CHECK
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll 2>&1 \
; RUN:   | %{il} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll 2>&1 \
; RUN:   | %{il} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-fix-function-bitcasts < %t/main.ll -o - \
; RUN:   | FileCheck %s --check-prefix=IR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -stop-after=msil-fix-function-bitcasts < %t/main.ll -o - \
; RUN:   | FileCheck %s --check-prefix=IR
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t/main.ll -o %t/main.o2.il \
; RUN:   && %ilasm-verify -output=%t/main.o2.dll %t/main.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/main.ll -o %t/main.o0.il \
; RUN:   && %ilasm-verify -output=%t/main.o0.dll %t/main.o0.il %}

; Two call types for one callee: two wrappers, each checked in its own pass
; over the same output because their order follows the use lists.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/two-types.ll \
; RUN:   -o %t/two.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=TWO --input-file=%t/two.lnx.il --implicit-check-not=int64
; RUN: FileCheck %s --check-prefix=TWO-SHORT --input-file=%t/two.lnx.il
; RUN: FileCheck %s --check-prefix=TWO-LONG --input-file=%t/two.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/two-types.ll \
; RUN:   -o %t/two.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=TWO --input-file=%t/two.win.il --implicit-check-not=int64
; RUN: FileCheck %s --check-prefix=TWO-SHORT --input-file=%t/two.win.il
; RUN: FileCheck %s --check-prefix=TWO-LONG --input-file=%t/two.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/two.dll %t/two.lnx.il %}

; K&R calls with a variadic call type (the shape of native x86-64 SysV clang's
; IR): no diagnostic at all; the callers (KNR) and the calls of the K&R
; definitions wherever they are (KNR-CALLS).
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/knr.ll \
; RUN:   -o %t/knr.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=KNR --input-file=%t/knr.lnx.il --implicit-check-not=int64
; RUN: FileCheck %s --check-prefix=KNR-CALLS --input-file=%t/knr.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/knr.ll \
; RUN:   -o %t/knr.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=KNR --input-file=%t/knr.win.il --implicit-check-not=int64
; RUN: FileCheck %s --check-prefix=KNR-CALLS --input-file=%t/knr.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/knr.ll \
; RUN:   -o %t/knr.lnx.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=KNR --input-file=%t/knr.lnx.o0.il --implicit-check-not=int64
; RUN: FileCheck %s --check-prefix=KNR-CALLS --input-file=%t/knr.lnx.o0.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-fix-function-bitcasts < %t/knr.ll -o - \
; RUN:   | FileCheck %s --check-prefix=KNR-IR
; RUN: %if ilasm %{ %ilasm-verify -output=%t/knr.dll %t/knr.lnx.il %}

; Wrappers of a callee with parameter and return attributes: valid IR
; (ATTR-IR, after the pass) that compiles (ATTR).
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-fix-function-bitcasts < %t/attrs.ll -o - \
; RUN:   | FileCheck %s --check-prefix=ATTR-IR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=msil-fix-function-bitcasts < %t/attrs.ll -o - \
; RUN:   | FileCheck %s --check-prefix=ATTR-IR
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -stop-after=msil-fix-function-bitcasts < %t/attrs.ll -o - \
; RUN:   | FileCheck %s --check-prefix=ATTR-IR
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/attrs.ll 2>&1 \
; RUN:   | %{il} --check-prefix=ATTR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/attrs.ll 2>&1 \
; RUN:   | %{il} --check-prefix=ATTR
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/attrs.ll 2>&1 \
; RUN:   | %{il} --check-prefix=ATTR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/attrs.ll 2>&1 \
; RUN:   | %{il} --check-prefix=ATTR
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t/attrs.ll -o %t/attrs.il \
; RUN:   && %ilasm-verify -output=%t/attrs.dll %t/attrs.il %}

; int main(void) keeps its name.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/main-void.ll 2>&1 \
; RUN:   | %{il} --check-prefix=MAIN --implicit-check-not=__original_main
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main-void.ll 2>&1 \
; RUN:   | %{il} --check-prefix=MAIN --implicit-check-not=__original_main
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-fix-function-bitcasts < %t/main-void.ll -o - \
; RUN:   | FileCheck %s --check-prefix=MAIN-IR --implicit-check-not=__original_main
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t/main-void.ll -o %t/main-void.il \
; RUN:   && %ilasm-verify -output=%t/main-void.dll %t/main-void.il %}

; Mismatched calls of a naked function: exactly one error, the callee's
; (NAKED), and wrappers without 'naked' (NAKED-IR and NAKED-ATTRS; the only
; 'naked' in the module after the pass is nk's own attribute group).
; DEFINE: %{nkdiag} = FileCheck %s --check-prefix=NAKED --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=error:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/naked.ll -o /dev/null 2>&1 \
; RUN:   | %{nkdiag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/naked.ll -o /dev/null 2>&1 \
; RUN:   | %{nkdiag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/naked.ll -o /dev/null 2>&1 \
; RUN:   | %{nkdiag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/naked.ll -o /dev/null 2>&1 \
; RUN:   | %{nkdiag}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-fix-function-bitcasts < %t/naked.ll \
; RUN:   -o %t/naked.lnx.mir
; RUN: FileCheck %s --check-prefix=NAKED-IR --input-file=%t/naked.lnx.mir
; RUN: FileCheck %s --check-prefix=NAKED-ATTRS --implicit-check-not=naked --input-file=%t/naked.lnx.mir
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -stop-after=msil-fix-function-bitcasts < %t/naked.ll \
; RUN:   -o %t/naked.win.mir
; RUN: FileCheck %s --check-prefix=NAKED-IR --input-file=%t/naked.win.mir
; RUN: FileCheck %s --check-prefix=NAKED-ATTRS --implicit-check-not=naked --input-file=%t/naked.win.mir

;--- main.ll
source_filename = "function-bitcasts.c"

@global_func = internal global ptr null, align 8

; CHECK: {{^}}.method public static void 'has_i32_arg'(int32) cil managed{{$}}
define void @has_i32_arg(i32 %x) {
  ret void
}

; CHECK: {{^}}.method public static int32 'has_i32_ret'() cil managed{{$}}
define i32 @has_i32_ret() {
  ret i32 7
}

define void @foo0() {
  ret void
}

define void @foo1() {
  ret void
}

define void @foo2() {
  ret void
}

define void @foo3() {
  ret void
}

; CHECK: {{^}}.method public static int32 'two_args'(int32, int32) cil managed{{$}}
define i32 @two_args(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}

; CHECK: {{^}}.method public static native int 'takes_ptr'(native int) cil managed{{$}}
define i64 @takes_ptr(ptr %p) {
  %i = ptrtoint ptr %p to i64
  ret i64 %i
}

; CHECK: {{^}}.method public static native int 'returns_ptr'(native int) cil managed{{$}}
define ptr @returns_ptr(i64 %x) {
  %p = inttoptr i64 %x to ptr
  ret ptr %p
}

; CHECK: {{^}}.method public static float32 'fret'(float32) cil managed{{$}}
define float @fret(float %x) {
  %r = fadd float %x, 1.0
  ret float %r
}

; CHECK: {{^}}.method public static native int 'wide'(native int) cil managed{{$}}
define i64 @wide(i64 %x) {
  ret i64 %x
}

define void @call_func(ptr %f) {
  ret void
}

; Fewer arguments, a dropped result, extra arguments, a missing result, and
; calls whose type matches (direct). Each wrapper is used for every call of
; its type.
; IR-LABEL: define void @test()
; IR:         call void @has_i32_arg_bitcast()
; IR-NEXT:    call void @has_i32_arg_bitcast()
; IR-NEXT:    call void @has_i32_ret_bitcast()
; IR-NEXT:    call i32 @has_i32_ret()
; IR-NEXT:    call void @foo0_bitcast(i32 0)
; IR-NEXT:    call void @foo0_bitcast(i32 0)
; IR-NEXT:    call void @foo0_bitcast(i32 0)
; IR-NEXT:    call void @foo0()
; IR-NEXT:    call i32 @foo1_bitcast()
; IR-NEXT:    call void @foo2()
; IR-NEXT:    call void @foo1()
; IR-NEXT:    call void @foo3()
; IR-NEXT:    ret void
; CHECK-LABEL: .method {{.*}}'test'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  call void 'has_i32_arg_bitcast'(){{$}}
; CHECK-NEXT:  {{^}}  call void 'has_i32_arg_bitcast'(){{$}}
; CHECK-NEXT:  {{^}}  call void 'has_i32_ret_bitcast'(){{$}}
; CHECK-NEXT:  {{^}}  call int32 'has_i32_ret'(){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK:       {{^}}  call void 'foo0_bitcast'(int32){{$}}
; CHECK:       {{^}}  call void 'foo0_bitcast'(int32){{$}}
; CHECK:       {{^}}  call void 'foo0_bitcast'(int32){{$}}
; CHECK-NEXT:  {{^}}  call void 'foo0'(){{$}}
; CHECK-NEXT:  {{^}}  call int32 'foo1_bitcast'(){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo2'(){{$}}
; CHECK-NEXT:  {{^}}  call void 'foo1'(){{$}}
; CHECK-NEXT:  {{^}}  call void 'foo3'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'test'{{$}}
define void @test() {
entry:
  call void @has_i32_arg()
  call void @has_i32_arg()
  call void @has_i32_ret()
  call i32 @has_i32_ret()
  call void @foo0(i32 0)
  call void @foo0(i32 0)
  call void @foo0(i32 0)
  call void @foo0()
  %t = call i32 @foo1()
  call void @foo2()
  call void @foo1()
  call void @foo3()
  ret void
}

; A missing second argument; the matching call is direct.
; CHECK-LABEL: .method {{.*}}'call_two_args_short'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'two_args_bitcast'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_two_args_short(i32 %x) {
  %r = call i32 @two_args(i32 %x)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'call_two_args_long'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'two_args'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_two_args_long(i32 %x, i32 %y) {
  %r = call i32 @two_args(i32 %x, i32 %y)
  ret i32 %r
}

; ptr <-> i64 are pointer-castable: wrappers with no-op casts.
; CHECK-LABEL: .method {{.*}}'call_takes_ptr'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call native int 'takes_ptr_bitcast'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call_takes_ptr(i64 %x) {
  %r = call i64 @takes_ptr(i64 %x)
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'call_returns_ptr'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call native int 'returns_ptr_bitcast'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call_returns_ptr(i64 %x) {
  %r = call i64 @returns_ptr(i64 %x)
  ret i64 %r
}

; float <-> i32 are bit-castable.
; CHECK-LABEL: .method {{.*}}'call_fret'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'fret_bitcast'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_fret(i32 %x) {
  %r = call i32 @fret(i32 %x)
  ret i32 %r
}

; i32 -> i64 cannot be converted: the wrapper traps at run time.
; CHECK-LABEL: .method {{.*}}'call_wide'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'wide_bitcast_invalid'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_wide(i32 %x) {
  %r = call i32 @wide(i32 %x)
  ret i32 %r
}

; No wrapper when the address is stored in memory, loaded and called through
; a pointer, or passed to a function.
; IR-LABEL: define void @test_store()
; IR-NEXT:    store ptr @has_i32_ret, ptr @global_func
; CHECK-LABEL: .method {{.*}}'test_store'(
; CHECK:       {{^}}  ldsfld native int '<msil>fn_has_i32_ret'{{$}}
; CHECK:       {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @test_store() {
  store ptr @has_i32_ret, ptr @global_func, align 8
  ret void
}

; CHECK-LABEL: .method {{.*}}'test_load'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'global_func'{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  calli int32(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @test_load() {
  %f = load ptr, ptr @global_func, align 8
  %r = call i32 %f()
  ret i32 %r
}

; IR-LABEL: define void @test_argument()
; IR-NEXT:    call void @call_func(ptr @has_i32_ret)
; IR-NEXT:    call void @call_func(ptr @has_i32_arg)
; CHECK-LABEL: .method {{.*}}'test_argument'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_has_i32_ret'{{$}}
; CHECK-NEXT:  {{^}}  call void 'call_func'(native int){{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_has_i32_arg'{{$}}
; CHECK-NEXT:  {{^}}  call void 'call_func'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @test_argument() {
  call void @call_func(ptr @has_i32_ret)
  call void @call_func(ptr @has_i32_arg)
  ret void
}

; Invokes are treated like calls: the mismatched one invokes the wrapper of
; test (LowerInvoke then makes both plain calls).
; IR-LABEL: define void @test_invoke()
; IR:         invoke void @call_func(ptr @has_i32_ret)
; IR:         invoke void @has_i32_arg_bitcast()
; CHECK-LABEL: .method {{.*}}'test_invoke'(
; CHECK:       {{^}}  ldsfld native int '<msil>fn_has_i32_ret'{{$}}
; CHECK-NEXT:  {{^}}  call void 'call_func'(native int){{$}}
; CHECK:       {{^}}  call void 'has_i32_arg_bitcast'(){{$}}
; CHECK:       {{^}}} // end of method 'test_invoke'{{$}}
declare i32 @__gxx_personality_v0(...)
define void @test_invoke() personality ptr @__gxx_personality_v0 {
entry:
  invoke void @call_func(ptr @has_i32_ret)
          to label %cont unwind label %lpad

cont:
  invoke void @has_i32_arg()
          to label %end unwind label %lpad

lpad:
  %0 = landingpad { ptr, i32 }
          catch ptr null
  br label %end

end:
  ret void
}

; ---- the wrappers, in the order of their callees ------------------------------

; IR-LABEL: define private void @has_i32_arg_bitcast()
; IR-NOT:     define
; IR:         call void @has_i32_arg(i32 poison)
; IR-NEXT:    ret void
; CHECK:       {{^}}.method assembly static void 'has_i32_arg_bitcast'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK:       {{^}}  call void 'has_i32_arg'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'has_i32_arg_bitcast'{{$}}

; IR-LABEL: define private void @has_i32_ret_bitcast()
; IR-NOT:     define
; IR:         call i32 @has_i32_ret()
; IR-NEXT:    ret void
; CHECK:       {{^}}.method assembly static void 'has_i32_ret_bitcast'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'has_i32_ret'(){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'has_i32_ret_bitcast'{{$}}

; IR-LABEL: define private void @foo0_bitcast(i32
; IR-NOT:     define
; IR:         call void @foo0()
; IR-NEXT:    ret void
; CHECK:       {{^}}.method assembly static void 'foo0_bitcast'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo0'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'foo0_bitcast'{{$}}

; IR-LABEL: define private i32 @foo1_bitcast()
; IR-NOT:     define
; IR:         call void @foo1()
; IR-NEXT:    ret i32 poison
; CHECK:       {{^}}.method assembly static int32 'foo1_bitcast'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK:       {{^}}  call void 'foo1'(){{$}}
; CHECK-NEXT:  {{^}}  {{ldloc\.0|ldc\.i4\.0}}{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'foo1_bitcast'{{$}}

; IR-LABEL: define private i32 @two_args_bitcast(i32 %0)
; IR-NOT:     define
; IR:         call i32 @two_args(i32 %0, i32 poison)
; CHECK:       {{^}}.method assembly static int32 'two_args_bitcast'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldarg\.0|ldc\.i4\.0|ldloc\.0}}{{$}}
; CHECK-NEXT:  {{^}}  call int32 'two_args'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'two_args_bitcast'{{$}}

; IR-LABEL: define private i64 @takes_ptr_bitcast(i64 %0)
; IR-NOT:     define
; IR:         [[P:%.+]] = inttoptr i64 %0 to ptr
; IR-NEXT:    call i64 @takes_ptr(ptr [[P]])
; CHECK:       {{^}}.method assembly static native int 'takes_ptr_bitcast'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call native int 'takes_ptr'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'takes_ptr_bitcast'{{$}}

; IR-LABEL: define private i64 @returns_ptr_bitcast(i64 %0)
; IR-NOT:     define
; IR:         [[R:%.+]] = call ptr @returns_ptr(i64 %0)
; IR-NEXT:    [[I:%.+]] = ptrtoint ptr [[R]] to i64
; IR-NEXT:    ret i64 [[I]]
; CHECK:       {{^}}.method assembly static native int 'returns_ptr_bitcast'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call native int 'returns_ptr'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'returns_ptr_bitcast'{{$}}

; The float <-> i32 casts go through a frame slot (a 16-byte frame, D13/D16).
; IR-LABEL: define private i32 @fret_bitcast(i32 %0)
; IR-NOT:     define
; IR:         [[F:%.+]] = bitcast i32 %0 to float
; IR-NEXT:    [[R:%.+]] = call float @fret(float [[F]])
; IR-NEXT:    [[I:%.+]] = bitcast float [[R]] to i32
; IR-NEXT:    ret i32 [[I]]
; CHECK:       {{^}}.method assembly static int32 'fret_bitcast'(int32) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK:       {{^}}  ldind.r4{{$}}
; CHECK-NEXT:  {{^}}  call float32 'fret'(float32){{$}}
; CHECK:       {{^}}  stind.r4{{$}}
; CHECK:       {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fret_bitcast'{{$}}

; IR-LABEL: define private i32 @wide_bitcast_invalid(i32 %0)
; IR-NOT:     define
; IR:         unreachable
; CHECK:       {{^}}.method assembly static int32 'wide_bitcast_invalid'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'wide_bitcast_invalid'{{$}}
; IR-NOT:     define
; CHECK-NOT:   .method {{.*}}_bitcast

; ---- the address fields -----------------------------------------------------

; The addresses taken above are the fields of has_i32_arg and has_i32_ret, in
; module order after global_func's field; the .cctor sets them with the ldftn
; of the functions' own signatures (not of a wrapper), after global_func's
; field. There are no relocations.
; CHECK:       {{^}}.field assembly static initonly native int 'global_func'{{$}}
; CHECK-EMPTY:
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_has_i32_arg'{{$}}
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_has_i32_ret'{{$}}
; CHECK-EMPTY:
; CHECK-NEXT:  {{^}}.method {{.*}} .cctor() cil managed{{$}}
; CHECK:       {{^}}  stsfld native int 'global_func'{{$}}
; CHECK-NEXT:  {{^}}  ldftn void 'has_i32_arg'(int32){{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_has_i32_arg'{{$}}
; CHECK-NEXT:  {{^}}  ldftn int32 'has_i32_ret'(){{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_has_i32_ret'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '.cctor'{{$}}

;--- two-types.ll
; Calls of 'two' with one argument and with three arguments: two wrappers,
; '<F>_bitcast' and '<F>_bitcast.<N>', each calling 'two' with its own
; signature (the missing argument is any value; the third is dropped).
source_filename = "two-types.c"

; TWO: {{^}}.method public static void 'two'(int32, int32) cil managed{{$}}
define void @two(i32 %a, i32 %b) {
  ret void
}

; TWO-LABEL: .method {{.*}}'call_short'(
; TWO:       {{^}}  ldarg.0{{$}}
; TWO-NEXT:  {{^}}  call void 'two_bitcast{{(\.[0-9]+)?}}'(int32){{$}}
; TWO-NEXT:  {{^}}  ret{{$}}
define void @call_short(i32 %x) {
  call void @two(i32 %x)
  ret void
}

; TWO-LABEL: .method {{.*}}'call_long'(
; TWO:       {{^}}  ldarg.0{{$}}
; TWO-NEXT:  {{^}}  ldarg.1{{$}}
; TWO-NEXT:  {{^}}  ldarg.2{{$}}
; TWO-NEXT:  {{^}}  call void 'two_bitcast{{(\.[0-9]+)?}}'(int32, int32, int32){{$}}
; TWO-NEXT:  {{^}}  ret{{$}}
define void @call_long(i32 %x, i32 %y, i32 %z) {
  call void @two(i32 %x, i32 %y, i32 %z)
  ret void
}

; TWO-LABEL: .method {{.*}}'call_exact'(
; TWO:       {{^}}  ldarg.0{{$}}
; TWO-NEXT:  {{^}}  ldarg.1{{$}}
; TWO-NEXT:  {{^}}  call void 'two'(int32, int32){{$}}
; TWO-NEXT:  {{^}}  ret{{$}}
define void @call_exact(i32 %x, i32 %y) {
  call void @two(i32 %x, i32 %y)
  ret void
}

; TWO-SHORT:      {{^}}.method assembly static void 'two_bitcast{{(\.[0-9]+)?}}'(int32) cil managed{{$}}
; TWO-SHORT-NEXT: {{^\{$}}
; TWO-SHORT-NEXT: {{^}}  .maxstack 2{{$}}
; TWO-SHORT:      {{^}}  call void 'two'(int32, int32){{$}}
; TWO-SHORT-NEXT: {{^}}  ret{{$}}
; TWO-SHORT-NEXT: {{^}}} // end of method 'two_bitcast{{(\.[0-9]+)?}}'{{$}}

; TWO-LONG:      {{^}}.method assembly static void 'two_bitcast{{(\.[0-9]+)?}}'(int32, int32, int32) cil managed{{$}}
; TWO-LONG-NEXT: {{^\{$}}
; TWO-LONG-NEXT: {{^}}  .maxstack 2{{$}}
; TWO-LONG-NEXT: {{^}}  ldarg.0{{$}}
; TWO-LONG-NEXT: {{^}}  ldarg.1{{$}}
; TWO-LONG-NEXT: {{^}}  call void 'two'(int32, int32){{$}}
; TWO-LONG-NEXT: {{^}}  ret{{$}}
; TWO-LONG-NEXT: {{^}}} // end of method 'two_bitcast{{(\.[0-9]+)?}}'{{$}}

;--- knr.ll
; Native x86-64 SysV clang (x86_64-unknown-linux-gnu,
; X86_64TargetCodeGenInfo::isNoProtoCallVariadic) emits a call through a
; declaration without a prototype with a variadic call type that lists every
; promoted argument, also when the function is defined in the module with a
; K&R definition. clang for MSIL triples emits fixed-arity calls, so this IR
; comes from native clang, or is written by hand, and is compiled with an MSIL
; triple (recon registry-tm.md 6 'Must handle as fixed-arity'). The call
; compiles without a
; diagnostic, kr_def is called with its own signature (directly or through a
; non-variadic wrapper), and no variadic definition is left after
; MSILFixFunctionBitcasts. The same holds for a pointer argument.
source_filename = "knr.c"

; KNR: {{^}}.method public static int32 'kr_def'(int32, float64) cil managed{{$}}
define i32 @kr_def(i32 %a, double %b) {
  %c = fptosi double %b to i32
  %r = add i32 %a, %c
  ret i32 %r
}

; KNR: {{^}}.method public static int32 'kr_ptr'(native int) cil managed{{$}}
define i32 @kr_ptr(ptr %p) {
  %v = load i32, ptr %p
  ret i32 %v
}

; KNR-LABEL: .method {{.*}}'use_kr'(
; KNR-SAME:  int32, float64) cil managed{{$}}
; KNR:       {{^}}  ldarg.0{{$}}
; KNR-NEXT:  {{^}}  ldarg.1{{$}}
; KNR-NEXT:  {{^}}  call int32 'kr_def{{(_bitcast(\.[0-9]+)?)?}}'(int32, float64){{$}}
; KNR-NEXT:  {{^}}  ret{{$}}
define i32 @use_kr(i32 %a, double %b) {
  %r = call i32 (i32, double, ...) @kr_def(i32 %a, double %b)
  ret i32 %r
}

; KNR-LABEL: .method {{.*}}'use_kr_ptr'(
; KNR-SAME:  native int) cil managed{{$}}
; KNR:       {{^}}  ldarg.0{{$}}
; KNR-NEXT:  {{^}}  call int32 'kr_ptr{{(_bitcast(\.[0-9]+)?)?}}'(native int){{$}}
; KNR-NEXT:  {{^}}  ret{{$}}
define i32 @use_kr_ptr(ptr %p) {
  %r = call i32 (ptr, ...) @kr_ptr(ptr %p)
  ret i32 %r
}

; Whatever calls kr_def and kr_ptr uses their method signatures.
; KNR-CALLS-DAG: {{^}}  call int32 'kr_def'(int32, float64){{$}}
; KNR-CALLS-DAG: {{^}}  call int32 'kr_ptr'(native int){{$}}

; KNR-IR:     source_filename = "knr.c"
; KNR-IR-NOT: {{^ *define .*\.\.\.\)}}

;--- attrs.ll
; attrs has 'signext' on its i32 parameter and 'returned' on its pointer
; parameter. call_attrs_cast calls it with an i64 result: its wrapper keeps
; 'signext' (the parameter is still i32) but drops 'returned', which must
; match the result type (ptr) and does not match i64. call_attrs_invalid passes
; a double for the i32: its wrapper (which traps) drops 'signext', which a
; double cannot have, and 'returned', since it returns void. The wrappers'
; order follows the use list, so they are matched in any order.
source_filename = "attrs.c"

; ATTR-IR:       define ptr @attrs(i32 signext %x, ptr returned %p) {{\{$}}
; ATTR:          {{^}}.method public static native int 'attrs'(int32, native int) cil managed{{$}}
define ptr @attrs(i32 signext %x, ptr returned %p) {
  ret ptr %p
}

; ATTR-IR-LABEL: define void @call_attrs_invalid(
; ATTR-IR-NEXT:    call void @attrs_bitcast_invalid(double %d, ptr %p)
; ATTR-LABEL:    .method {{.*}}'call_attrs_invalid'(
; ATTR-SAME:     float64, native int) cil managed{{$}}
; ATTR:          {{^}}  ldarg.0{{$}}
; ATTR-NEXT:     {{^}}  ldarg.1{{$}}
; ATTR-NEXT:     {{^}}  call void 'attrs_bitcast_invalid'(float64, native int){{$}}
; ATTR-NEXT:     {{^}}  ret{{$}}
define void @call_attrs_invalid(double %d, ptr %p) {
  call void @attrs(double %d, ptr %p)
  ret void
}

; ATTR-IR-LABEL: define i64 @call_attrs_cast(
; ATTR-IR-NEXT:    %r = call i64 @attrs_bitcast(i32 %x, ptr %p)
; ATTR-LABEL:    .method {{.*}}'call_attrs_cast'(
; ATTR-SAME:     int32, native int) cil managed{{$}}
; ATTR:          {{^}}  ldarg.0{{$}}
; ATTR-NEXT:     {{^}}  ldarg.1{{$}}
; ATTR-NEXT:     {{^}}  call native int 'attrs_bitcast'(int32, native int){{$}}
; ATTR-NEXT:     {{^}}  ret{{$}}
define i64 @call_attrs_cast(i32 %x, ptr %p) {
  %r = call i64 @attrs(i32 %x, ptr %p)
  ret i64 %r
}

; ATTR-IR-DAG:   define private i64 @attrs_bitcast(i32 signext %0, ptr %1) {{\{$}}
; ATTR-IR-DAG:   define private void @attrs_bitcast_invalid(double %0, ptr %1) {{\{$}}
; ATTR-DAG:      {{^\.method assembly static native int 'attrs_bitcast'\(int32, native int\) cil managed[[:space:]]+\{[[:space:]]+\.maxstack 2[[:space:]]+ldarg\.0[[:space:]]+ldarg\.1[[:space:]]+call native int 'attrs'\(int32, native int\)[[:space:]]+ret[[:space:]]+\} // end of method 'attrs_bitcast'$}}
; ATTR-DAG:      {{^\.method assembly static void 'attrs_bitcast_invalid'\(float64, native int\) cil managed[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldnull[[:space:]]+throw[[:space:]]+\} // end of method 'attrs_bitcast_invalid'$}}

;--- main-void.ll
; int main(void): no __original_main and no main(i32, ptr) wrapper; the
; generated entry point calls main() itself.
source_filename = "main-void.c"

; MAIN-IR:     define i32 @main()
; MAIN-IR-NOT: define {{.*}}@main
; MAIN-LABEL: .method public static int32 'main'(
; MAIN-SAME:  ) cil managed{{$}}
; MAIN-NOT:   .method {{.*}}'main
; MAIN:       {{^}}.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed{{$}}
; MAIN-NEXT:  {{^\{$}}
; MAIN-NEXT:  {{^}}  .entrypoint{{$}}
; MAIN-NEXT:  {{^}}  .maxstack 1{{$}}
; MAIN-NEXT:  {{^}}  call int32 'main'(){{$}}
; MAIN-NEXT:  {{^}}  ret{{$}}
define i32 @main() {
  ret i32 42
}

;--- naked.ll
; nk is naked with an 'unreachable' body, the shape of clang's IR for
; '__attribute__((naked)) int nk(int x) {}'.
; call_cast calls it with one extra i32 argument (castable: nk_bitcast, whose
; body calls nk), call_invalid with an i64 first argument (not castable:
; nk_bitcast_invalid, whose body is 'unreachable'). Neither wrapper is naked.
source_filename = "nk.c"

; NAKED:            {{^}}error: {{.*}}in function nk {{.*}}naked functions are not supported for MSIL{{$}}
; NAKED-ATTRS:      ; Function Attrs: naked{{$}}
; NAKED-ATTRS-NEXT: define i32 @nk(i32 %x) #[[NKGRP:[0-9]+]] {{\{$}}
define i32 @nk(i32 %x) #0 {
  unreachable
}

; NAKED-IR-LABEL: define i32 @call_cast(
; NAKED-IR-NEXT:    %r = call i32 @nk_bitcast{{(\.[0-9]+)?}}(i32 %a, i32 3)
define i32 @call_cast(i32 %a) {
  %r = call i32 @nk(i32 %a, i32 3)
  ret i32 %r
}

; NAKED-IR-LABEL: define i32 @call_invalid(
; NAKED-IR-NEXT:    %r = call i32 @nk_bitcast_invalid(i64 %b, i32 3)
define i32 @call_invalid(i64 %b) {
  %r = call i32 @nk(i64 %b, i32 3)
  ret i32 %r
}

; NAKED-IR-DAG:     define private i32 @nk_bitcast{{(\.[0-9]+)?}}(i32 %0, i32 %1)
; NAKED-IR-DAG:     define private i32 @nk_bitcast_invalid(i64 %0, i32 %1)
; NAKED-ATTRS:      attributes #[[NKGRP]] = { naked }
attributes #0 = { naked }
