; Every frame-operation diagnostic of Phase 1 (ImplementationPlan.md decision
; D13, M7.b, and plan section 12, rows 'M7.b diagnostic texts amended (frame
; operations)' and 'M7.b diagnostic texts extended (setjmp/longjmp, eh.return,
; stack address)'; Design.md 4.2 puts dynamic allocas and
; stacksave/stackrestore on a per-thread shadow stack, which needs the phase-3
; runtime; plan section 2 'Diagnostics leave well-formed output'). Each case
; is reported with DiagnosticInfoUnsupported and its exact text:
;  - an alloca with a variable count (a C VLA or alloca(n)) or an alloca
;    outside the entry block (also dynamic): 'dynamic alloca / VLA is not
;    supported for MSIL yet (phase 3 shadow stack)';
;  - llvm.stacksave and llvm.stackrestore in a function without a dynamic
;    alloca: 'llvm.stacksave is not supported for MSIL yet (phase 3 shadow
;    stack)' and 'llvm.stackrestore ...'. In a function with one (clang
;    brackets every VLA scope with them, at -O0 too) they report nothing, so
;    one VLA gives exactly one error at every optimization level;
;  - llvm.frameaddress: 'llvm.frameaddress is not supported for MSIL yet (phase
;    3 shadow stack)'; llvm.eh.dwarf.cfa (__builtin_dwarf_cfa, whose generic
;    expansion is a frame address): 'llvm.eh.dwarf.cfa is not supported for
;    MSIL';
;  - llvm.stackaddress (__builtin_stack_address): 'llvm.stackaddress is not
;    supported for MSIL yet (phase 3 shadow stack)', not LLVM's generic
;    'llvm.stackaddress is not supported on this target.', which names no
;    function;
;  - llvm.returnaddress, llvm.addressofreturnaddress and llvm.sponentry:
;    '<intrinsic> is not supported for MSIL';
;  - llvm.init.trampoline and llvm.adjust.trampoline: 'trampolines are not
;    supported for MSIL', once for each;
;  - llvm.eh.sjlj.setjmp and llvm.eh.sjlj.longjmp (__builtin_setjmp and
;    __builtin_longjmp): '<intrinsic> is not supported for MSIL yet (phase 3
;    setjmp/longjmp)'; llvm.eh.sjlj.setup.dispatch: 'llvm.eh.sjlj.setup.dispatch
;    is not supported for MSIL'; llvm.eh.return.i32 and llvm.eh.return.i64
;    (__builtin_eh_return): 'llvm.eh.return is not supported for MSIL'. Without
;    these the legalizer would silently drop the longjmp and the eh.return and
;    fold the setjmp to 0, and an unlowered setup.dispatch could not be
;    selected;
;  - a static frame above 2 GiB: 'stack frame of <N> bytes is larger than the
;    2 GiB that MSIL supports', where N is the frame size plus the MaxAlign - 8
;    bytes of the D13 realignment; the localloc is clamped to 2147483647 bytes,
;    so the output stays well-formed.
; llc reports every one, exits with 1, never prints 'LLVM ERROR' or a crash
; report, and keeps compiling with a well-formed dummy value, so the functions
; after a diagnosed one compile normally and the IL on stdout still assembles.
; Every error is checked with its whole text, in function order, and no other
; error or warning is allowed (--implicit-check-not), so a doubled, a missing
; or a different diagnostic fails. Each function is diagnosed at -O0 and -O2
; on both triples.
;
; llc deletes its -o file after an error, so the module that is checked and
; assembled is taken from stdout.

; DEFINE: %{diag} = FileCheck %s --check-prefix=ERR --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not="Stack dump" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.lnx.o2.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.lnx.o2.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.win.o2.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.win.o2.il
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.lnx.o0.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.lnx.o0.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.win.o0.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o2.dll %t.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o2.dll %t.win.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o0.dll %t.lnx.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

source_filename = "dynamic-alloca-error.c"

define void @use(ptr %p) {
  ret void
}

; ---- Dynamic allocas ----

; ERR: error: {{.*}}in function vla void (i64): dynamic alloca / VLA is not supported for MSIL yet (phase 3 shadow stack){{$}}
define void @vla(i64 %n) {
  %p = alloca i8, i64 %n, align 1
  call void @use(ptr %p)
  ret void
}

; An element count of another type and an over-aligned element.
; ERR: error: {{.*}}in function vla_i32 void (i32): dynamic alloca / VLA is not supported for MSIL yet (phase 3 shadow stack){{$}}
define void @vla_i32(i32 %n) {
  %p = alloca double, i32 %n, align 16
  call void @use(ptr %p)
  ret void
}

; A constant-size alloca outside the entry block is dynamic too.
; ERR: error: {{.*}}in function alloca_in_block void (i32): dynamic alloca / VLA is not supported for MSIL yet (phase 3 shadow stack){{$}}
define void @alloca_in_block(i32 %c) {
entry:
  %t = icmp ne i32 %c, 0
  br i1 %t, label %then, label %exit
then:
  %p = alloca i32, align 4
  call void @use(ptr %p)
  br label %exit
exit:
  ret void
}

; clang's VLA in a block scope: stacksave, the dynamic alloca, stackrestore.
; Only the alloca is reported, once.
; ERR: error: {{.*}}in function vla_scope void (i64): dynamic alloca / VLA is not supported for MSIL yet (phase 3 shadow stack){{$}}
define void @vla_scope(i64 %n) {
  %s = call ptr @llvm.stacksave.p0()
  %p = alloca i32, i64 %n, align 4
  call void @use(ptr %p)
  call void @llvm.stackrestore.p0(ptr %s)
  ret void
}

; A static frame next to a diagnosed dynamic alloca does not get in the way.
; ERR: error: {{.*}}in function static_and_vla void (i64): dynamic alloca / VLA is not supported for MSIL yet (phase 3 shadow stack){{$}}
define void @static_and_vla(i64 %n) {
  %x = alloca [16 x i8], align 8
  call void @use(ptr %x)
  %p = alloca i8, i64 %n, align 1
  call void @use(ptr %p)
  ret void
}

; ---- Stack and frame intrinsics in functions without a dynamic alloca ----

; ERR: error: {{.*}}in function save_only ptr (): llvm.stacksave is not supported for MSIL yet (phase 3 shadow stack){{$}}
define ptr @save_only() {
  %s = call ptr @llvm.stacksave.p0()
  ret ptr %s
}

; ERR: error: {{.*}}in function restore_only void (ptr): llvm.stackrestore is not supported for MSIL yet (phase 3 shadow stack){{$}}
define void @restore_only(ptr %s) {
  call void @llvm.stackrestore.p0(ptr %s)
  ret void
}

; ERR: error: {{.*}}in function frame_address ptr (): llvm.frameaddress is not supported for MSIL yet (phase 3 shadow stack){{$}}
define ptr @frame_address() {
  %f = call ptr @llvm.frameaddress.p0(i32 0)
  ret ptr %f
}

; __builtin_stack_address(): the sibling of the frame address, with its own
; text. The dummy result is a null pointer.
; ERR: error: {{.*}}in function stack_address ptr (): llvm.stackaddress is not supported for MSIL yet (phase 3 shadow stack){{$}}
define ptr @stack_address() {
  %s = call ptr @llvm.stackaddress.p0()
  ret ptr %s
}

; __builtin_dwarf_cfa(): its own text, not the frame-address one.
; ERR: error: {{.*}}in function dwarf_cfa ptr (): llvm.eh.dwarf.cfa is not supported for MSIL{{$}}
define ptr @dwarf_cfa() {
  %c = call ptr @llvm.eh.dwarf.cfa(i32 0)
  ret ptr %c
}

; ---- Return addresses, the entry stack pointer and trampolines ----

; ERR: error: {{.*}}in function return_address ptr (): llvm.returnaddress is not supported for MSIL{{$}}
define ptr @return_address() {
  %r = call ptr @llvm.returnaddress(i32 0)
  ret ptr %r
}

; ERR: error: {{.*}}in function address_of_return_address ptr (): llvm.addressofreturnaddress is not supported for MSIL{{$}}
define ptr @address_of_return_address() {
  %r = call ptr @llvm.addressofreturnaddress.p0()
  ret ptr %r
}

; ERR: error: {{.*}}in function sp_entry ptr (): llvm.sponentry is not supported for MSIL{{$}}
define ptr @sp_entry() {
  %r = call ptr @llvm.sponentry.p0()
  ret ptr %r
}

; The trampoline target has no 'nest' parameter, so only the two trampoline
; operations are diagnosed here (a nest parameter has its own diagnostic,
; unsupported-function-errors.ll).
define i32 @tramp_target(i32 %x) {
  ret i32 %x
}

; ERR: error: {{.*}}in function trampoline ptr (ptr): trampolines are not supported for MSIL{{$}}
; ERR: error: {{.*}}in function trampoline ptr (ptr): trampolines are not supported for MSIL{{$}}
define ptr @trampoline(ptr %n) {
  %t = alloca [64 x i8], align 16
  call void @llvm.init.trampoline(ptr %t, ptr @tramp_target, ptr %n)
  %f = call ptr @llvm.adjust.trampoline(ptr %t)
  ret ptr %f
}

; ---- setjmp/longjmp and eh.return ----

; Each intrinsic is alone in its function, so each function gives exactly one
; error. The setjmp's dummy result is 0.
; ERR: error: {{.*}}in function sjlj_setjmp i32 (ptr): llvm.eh.sjlj.setjmp is not supported for MSIL yet (phase 3 setjmp/longjmp){{$}}
define i32 @sjlj_setjmp(ptr %buf) {
  %r = call i32 @llvm.eh.sjlj.setjmp(ptr %buf)
  ret i32 %r
}

; ERR: error: {{.*}}in function sjlj_longjmp void (ptr): llvm.eh.sjlj.longjmp is not supported for MSIL yet (phase 3 setjmp/longjmp){{$}}
define void @sjlj_longjmp(ptr %buf) {
  call void @llvm.eh.sjlj.longjmp(ptr %buf)
  unreachable
}

; ERR: error: {{.*}}in function sjlj_setup_dispatch void (): llvm.eh.sjlj.setup.dispatch is not supported for MSIL{{$}}
define void @sjlj_setup_dispatch() {
  call void @llvm.eh.sjlj.setup.dispatch()
  ret void
}

; Both widths of llvm.eh.return become the same node and the same text.
; ERR: error: {{.*}}in function eh_return_i64 void (i64, ptr): llvm.eh.return is not supported for MSIL{{$}}
define void @eh_return_i64(i64 %offset, ptr %handler) {
  call void @llvm.eh.return.i64(i64 %offset, ptr %handler)
  unreachable
}

; ERR: error: {{.*}}in function eh_return_i32 void (i32, ptr): llvm.eh.return is not supported for MSIL{{$}}
define void @eh_return_i32(i32 %offset, ptr %handler) {
  call void @llvm.eh.return.i32(i32 %offset, ptr %handler)
  unreachable
}

; ---- A static frame above 2 GiB ----

; 3000000000 bytes plus 16 - 8 for the realignment of the 16-aligned object.
; ERR: error: {{.*}}in function big_frame void (): stack frame of 3000000008 bytes is larger than the 2 GiB that MSIL supports{{$}}
define void @big_frame() {
  %a = alloca [3000000000 x i8], align 16
  call void @use(ptr %a)
  ret void
}

; The module goes on after the errors: every method is printed, the dummy
; results of stack_address and sjlj_setjmp are a zero native int and 0, the
; unreachable after the longjmp and the eh.returns is the usual 'ldnull; throw',
; big_frame's localloc is clamped to 2147483647 bytes, and the function after
; them compiles as usual.
; IL: .method {{.*}}'vla'(native int) cil managed{{$}}
; IL: .method {{.*}}'vla_i32'(int32) cil managed{{$}}
; IL: .method {{.*}}'alloca_in_block'(int32) cil managed{{$}}
; IL: .method {{.*}}'vla_scope'(native int) cil managed{{$}}
; IL: .method {{.*}}'static_and_vla'(native int) cil managed{{$}}
; IL: .method {{.*}}'save_only'() cil managed{{$}}
; IL: .method {{.*}}'restore_only'(native int) cil managed{{$}}
; IL: .method {{.*}}'frame_address'() cil managed{{$}}
; IL:       .method {{.*}} native int 'stack_address'() cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 1{{$}}
; IL-NEXT:  {{^}}  ldc.i4 0{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'stack_address'{{$}}
; IL: .method {{.*}}'dwarf_cfa'() cil managed{{$}}
; IL: .method {{.*}}'return_address'() cil managed{{$}}
; IL: .method {{.*}}'address_of_return_address'() cil managed{{$}}
; IL: .method {{.*}}'sp_entry'() cil managed{{$}}
; IL: .method {{.*}}'tramp_target'(int32) cil managed{{$}}
; IL: .method {{.*}}'trampoline'(native int) cil managed{{$}}
; IL:       .method {{.*}} int32 'sjlj_setjmp'(native int) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 1{{$}}
; IL-NEXT:  {{^}}  ldc.i4.0{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'sjlj_setjmp'{{$}}
; IL:       .method {{.*}}'sjlj_longjmp'(native int) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 1{{$}}
; IL-NEXT:  {{^}}  ldnull{{$}}
; IL-NEXT:  {{^}}  throw{{$}}
; IL-NEXT:  {{^}}} // end of method 'sjlj_longjmp'{{$}}
; IL:       .method {{.*}}'sjlj_setup_dispatch'() cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 0{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'sjlj_setup_dispatch'{{$}}
; IL:       .method {{.*}}'eh_return_i64'(native int, native int) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 1{{$}}
; IL-NEXT:  {{^}}  ldnull{{$}}
; IL-NEXT:  {{^}}  throw{{$}}
; IL-NEXT:  {{^}}} // end of method 'eh_return_i64'{{$}}
; IL:       .method {{.*}}'eh_return_i32'(int32, native int) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 1{{$}}
; IL-NEXT:  {{^}}  ldnull{{$}}
; IL-NEXT:  {{^}}  throw{{$}}
; IL-NEXT:  {{^}}} // end of method 'eh_return_i32'{{$}}
; IL-LABEL: .method {{.*}}'big_frame'(
; IL-SAME:  ) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 2{{$}}
; IL-NEXT:  {{^}}  .locals ({{.+}}){{$}}
; IL-NEXT:  {{^}}  ldc.i4 2147483647{{$}}
; IL-NEXT:  {{^}}  localloc{{$}}
; IL-NOT:   localloc
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'big_frame'{{$}}
; IL-LABEL: .method {{.*}}'after'(
; IL-SAME:  int32) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 2{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.1{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'after'{{$}}
define i32 @after(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

declare ptr @llvm.stacksave.p0()
declare void @llvm.stackrestore.p0(ptr)
declare ptr @llvm.frameaddress.p0(i32)
declare ptr @llvm.stackaddress.p0()
declare ptr @llvm.eh.dwarf.cfa(i32)
declare ptr @llvm.returnaddress(i32)
declare ptr @llvm.addressofreturnaddress.p0()
declare ptr @llvm.sponentry.p0()
declare void @llvm.init.trampoline(ptr, ptr, ptr)
declare ptr @llvm.adjust.trampoline(ptr)
declare i32 @llvm.eh.sjlj.setjmp(ptr)
declare void @llvm.eh.sjlj.longjmp(ptr)
declare void @llvm.eh.sjlj.setup.dispatch()
declare void @llvm.eh.return.i64(i64, ptr)
declare void @llvm.eh.return.i32(i32, ptr)
