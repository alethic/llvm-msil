; Undefined extern_weak symbols in whole-program mode (ImplementationPlan.md
; M12.a (1b), decision D26 as amended by the section 12 row of 2026-10-21 for
; M12; plan 5.9.6, whose trap stub msil-ld makes the same way). -S and llc
; compile the whole program, so a weak reference that nothing defines is
; null. MSILPrepareIR rewrites an extern_weak function declaration that the
; Phase-1 import policy (D14: puts, putchar, the C math library,
; msil-import-module) does not resolve: its direct calls, its invokes and the
; llvm.global_ctors entries naming it call one internal stub
; '<msil>undef_<name>' of the declaration's type (created once; body
; llvm.trap and unreachable, printed 'ldnull' and 'throw'; an 'assembly'
; method), every other use (compares, initializers, stored and passed
; addresses, no_cfi and dso_local_equivalent) becomes 'ptr null', and the
; declaration leaves llvm.used and stays unused, so it gets no P/Invoke, no
; '<msil>fn_<name>' address field and no 'undefined function' error. A call
; whose type differs from the declaration's goes through
; MSILFixFunctionBitcasts' wrapper '<msil>undef_maybe_bitcast', which runs
; after MSILPrepareIR. An extern_weak variable becomes 'ptr null' in every use
; and is erased, so it is no 'external global variable' error and has no
; field; an initializer that held its address, or a weak function's, is zero
; data. Weak declarations that the import policy resolves are untouched: weak
; puts and a weak declaration with "msil-import-module" are called and
; declared as P/Invokes, and the address of puts is its '<msil>fn_puts' field,
; not null. A variadic weak function gets a stub with the declaration's fixed
; parameters only (no variadic definition, D19), which its call reaches
; through the wrapper '<msil>undef_vmaybe_bitcast'.
;
; llc reports nothing at -O0 and -O2 on both triples, and the output
; assembles. The STUB prefix checks the stubs and the wrappers, which may come
; anywhere in the module; the IR prefixes check what MSILPrepareIR leaves.
;
; errors.ll (a split part): an extern_weak thread_local variable is left
; alone, not made null (the section 12 row 'M12.a (1b) and the D26 row of
; 2026-10-21 (whole-program lowerings) clarified'), so like an external
; thread_local declaration it keeps D26's 'thread-local variable @tv is not
; supported for MSIL yet', exit 1, with no crash, on both triples at -O0 and
; -O2. The external-declaration path may add an error of its own, so no other
; line is required or forbidden.

; DEFINE: %{il} = FileCheck %s --implicit-check-not=int64 --implicit-check-not="'maybe'" \
; DEFINE:   --implicit-check-not="<msil>fn_maybe" --implicit-check-not="'weak_data'" \
; DEFINE:   --implicit-check-not="'weak_ctor'" --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not="'vmaybe'" --implicit-check-not="<msil>fn_vmaybe"
; DEFINE: %{stub} = FileCheck %s --check-prefix=STUB
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t.lnx.o2.il 2>&1 | count 0
; RUN: %{il} --check-prefixes=CHECK,LNX --input-file=%t.lnx.o2.il
; RUN: %{stub} --input-file=%t.lnx.o2.il
; RUN: FileCheck %s --check-prefix=ONCE --input-file=%t.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t.win.o2.il 2>&1 | count 0
; RUN: %{il} --check-prefixes=CHECK,WIN --input-file=%t.win.o2.il
; RUN: %{stub} --input-file=%t.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t.lnx.o0.il 2>&1 | count 0
; RUN: %{il} --check-prefixes=CHECK,LNX --input-file=%t.lnx.o0.il
; RUN: %{stub} --input-file=%t.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t.win.o0.il 2>&1 | count 0
; RUN: %{il} --check-prefixes=CHECK,WIN --input-file=%t.win.o0.il
; RUN: %{stub} --input-file=%t.win.o0.il
; RUN: FileCheck %s --check-prefix=ONCE --input-file=%t.win.o0.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/main.ll -o %t.prep.ll
; RUN: FileCheck %s --check-prefix=IR --input-file=%t.prep.ll --implicit-check-not=@weak_data \
; RUN:   --implicit-check-not="call i32 @maybe" --implicit-check-not="invoke i32 @maybe" \
; RUN:   --implicit-check-not="ptr @maybe" --implicit-check-not="@weak_ctor," \
; RUN:   --implicit-check-not="@vmaybe(i32 %"
; RUN: FileCheck %s --check-prefix=IRSTUB --input-file=%t.prep.ll
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o2.dll %t.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

; DEFINE: %{tls} = FileCheck %s --check-prefix=TLS --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/errors.ll -o /dev/null 2>&1 \
; RUN:   | %{tls}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/errors.ll -o /dev/null 2>&1 \
; RUN:   | %{tls}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/errors.ll -o /dev/null 2>&1 \
; RUN:   | %{tls}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/errors.ll -o /dev/null 2>&1 \
; RUN:   | %{tls}

;--- main.ll
source_filename = "extern-weak.c"

@str = private unnamed_addr constant [3 x i8] c"hi\00", align 1
@after = global i32 7, align 4
@maybe_ptr = global ptr @maybe, align 8
@data_ptr = global ptr @weak_data, align 8
@weak_data = extern_weak global i32, align 4
@llvm.used = appending global [2 x ptr] [ptr @maybe, ptr @after], section "llvm.metadata"
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @weak_ctor, ptr null }]

; IR:      @maybe_ptr = global ptr null, align 8
; IR:      @data_ptr = global ptr null, align 8
; IR:      @llvm.used = appending global [1 x ptr] [ptr @after], section "llvm.metadata"
; IR:      @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @"<msil>undef_weak_ctor", ptr null }]
; IR:      declare extern_weak i32 @maybe(i32)

declare extern_weak i32 @maybe(i32)
declare extern_weak void @weak_ctor()
declare extern_weak i32 @puts(ptr)
declare extern_weak i32 @imported(i32) #0
declare extern_weak i32 @vmaybe(i32, ...)
declare i32 @__gxx_personality_v0(...)

; if (maybe) return maybe(x); return -1;  The compare is with null.
; IR-LABEL: define i32 @call_if_present(
; IR:       icmp ne ptr null, null
; IR:       call i32 @"<msil>undef_maybe"(i32 %x)
; CHECK-LABEL: {{^}}.method public static int32 'call_if_present'(int32) cil managed{{$}}
; CHECK-NOT:   {{^}}.method
; CHECK:       {{^}}  call int32 '<msil>undef_maybe'(int32){{$}}
; CHECK-NOT:   {{^}}.method
; CHECK:       {{^}}  ldc.i4.m1{{$}}
; CHECK:       {{^}}} // end of method 'call_if_present'{{$}}
define i32 @call_if_present(i32 %x) {
entry:
  %present = icmp ne ptr @maybe, null
  br i1 %present, label %call, label %absent
call:
  %r = call i32 @maybe(i32 %x)
  ret i32 %r
absent:
  ret i32 -1
}

; CHECK-LABEL: {{^}}.method public static int32 'call_unguarded'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 '<msil>undef_maybe'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_unguarded'{{$}}
define i32 @call_unguarded(i32 %x) {
  %r = call i32 @maybe(i32 %x)
  ret i32 %r
}

; A call with another function type than the declaration's.
; CHECK-LABEL: {{^}}.method public static int32 'call_mismatched'(int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call int32 '<msil>undef_maybe_bitcast'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_mismatched(i32 %x, i32 %y) {
  %r = call i32 @maybe(i32 %x, i32 %y)
  ret i32 %r
}

; A call of a variadic weak function goes to its stub through the wrapper.
; IR-LABEL: define i32 @call_variadic(
; IR:       call i32 (i32, ...) @"<msil>undef_vmaybe"(i32 %x, i32 2, double %d)
; CHECK-LABEL: {{^}}.method public static int32 'call_variadic'(int32, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call int32 '<msil>undef_vmaybe_bitcast'(int32, int32, float64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_variadic(i32 %x, double %d) {
  %r = call i32 (i32, ...) @vmaybe(i32 %x, i32 2, double %d)
  ret i32 %r
}

; An invoke calls the stub as well (LowerInvoke makes it a call).
; IR-LABEL: define i32 @invoke_maybe(
; IR:       invoke i32 @"<msil>undef_maybe"(i32 %x)
; CHECK-LABEL: {{^}}.method public static int32 'invoke_maybe'(int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 '<msil>undef_maybe'(int32){{$}}
; CHECK:       {{^}}} // end of method 'invoke_maybe'{{$}}
define i32 @invoke_maybe(i32 %x) personality ptr @__gxx_personality_v0 {
entry:
  %r = invoke i32 @maybe(i32 %x)
          to label %ok unwind label %lp
ok:
  ret i32 %r
lp:
  %l = landingpad { ptr, i32 } cleanup
  ret i32 0
}

; Stored, passed, no_cfi and dso_local_equivalent addresses are null.
; CHECK-LABEL: {{^}}.method public static void 'store_address'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_address(ptr %p) {
  store ptr @maybe, ptr %p, align 8
  ret void
}

; CHECK-LABEL: {{^}}.method public static void 'sink'(native int) cil managed{{$}}
define void @sink(ptr %p) noinline {
  ret void
}

; CHECK-LABEL: {{^}}.method public static void 'pass_address'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @pass_address() {
  call void @sink(ptr @maybe)
  ret void
}

; CHECK-LABEL: {{^}}.method public static native int 'nocfi_address'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @nocfi_address() {
  ret ptr no_cfi @maybe
}

; CHECK-LABEL: {{^}}.method public static native int 'dsole_address'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @dsole_address() {
  ret ptr dso_local_equivalent @maybe
}

; &weak_data != 0 is false.
; CHECK-LABEL: {{^}}.method public static int32 'data_present'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i1 @data_present() {
  %c = icmp ne ptr @weak_data, null
  ret i1 %c
}

; CHECK-LABEL: {{^}}.method public static native int 'data_address'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @data_address() {
  ret ptr @weak_data
}

; Weak declarations that the import policy resolves stay P/Invokes.
; CHECK-LABEL: {{^}}.method public static int32 'say_hi'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'str'{{$}}
; CHECK-NEXT:  {{^}}  call int32 'puts'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @say_hi() {
  %r = call i32 @puts(ptr @str)
  ret i32 %r
}

; CHECK-LABEL: {{^}}.method public static int32 'puts_present'() cil managed{{$}}
; CHECK:       {{^}}  ldsfld native int '<msil>fn_puts'{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}} // end of method 'puts_present'{{$}}
define i1 @puts_present() {
  %c = icmp ne ptr @puts, null
  ret i1 %c
}

; CHECK-LABEL: {{^}}.method public static int32 'call_imported'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'imported'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_imported(i32 %x) {
  %r = call i32 @imported(i32 %x)
  ret i32 %r
}

attributes #0 = { "msil-import-module"="mylib" "msil-import-name"="entry_point" }

; The P/Invokes, then the data: 'str' and 'after' are initialized, and the
; two pointers that held weak addresses are in the zero tail; .cctor stores no
; address into them and runs the constructor through its stub.
; LNX:         {{^}}.method public hidebysig static pinvokeimpl("libc" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; WIN:         {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; CHECK-NEXT:  {{^}}.method public hidebysig static pinvokeimpl("mylib" as "entry_point" nomangle cdecl) int32 'imported'(int32) cil managed preservesig {}{{$}}
; CHECK:       {{^}}// ---- data segment: 24 bytes (8 initialized + 16 zero), align 16 ----{{$}}
; CHECK:       {{^}}.field assembly static initonly native int 'str'{{$}}
; CHECK-NEXT:  {{^}}.field public static initonly native int 'after'{{$}}
; CHECK-NEXT:  {{^}}.field public static initonly native int 'maybe_ptr'{{$}}
; CHECK-NEXT:  {{^}}.field public static initonly native int 'data_ptr'{{$}}
; CHECK:       {{^}}.field private static initonly native int '<msil>fn_puts'{{$}}
; CHECK:       {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK:       {{^}}  stsfld native int 'data_ptr'{{$}}
; CHECK-NEXT:  {{^}}  ldftn int32 'puts'(native int){{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_puts'{{$}}
; CHECK-NEXT:  {{^}}  call void '<msil>undef_weak_ctor'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '.cctor'{{$}}

; One stub per declaration, however many calls it has (ONCE), and one
; wrapper.
; STUB-DAG:    {{^}}.method assembly static void '<msil>undef_weak_ctor'() cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldnull[[:space:]]+throw[[:space:]]+\} // end of method '<msil>undef_weak_ctor'$}}
; STUB-DAG:    {{^}}.method assembly static int32 '<msil>undef_maybe_bitcast'(int32, int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldarg\.0[[:space:]]+call int32 '<msil>undef_maybe'\(int32\)[[:space:]]+ret[[:space:]]+\} // end of method '<msil>undef_maybe_bitcast'$}}
; STUB-DAG:    {{^}}.method assembly static int32 '<msil>undef_maybe'(int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldnull[[:space:]]+throw[[:space:]]+\} // end of method '<msil>undef_maybe'$}}
; ONCE:        {{^}}.method {{.*}}'<msil>undef_maybe'(
; ONCE-NOT:    {{^}}.method {{.*}}'<msil>undef_maybe'(

; The variadic declaration's stub takes its fixed parameter only.
; STUB-DAG:    {{^}}.method assembly static int32 '<msil>undef_vmaybe'(int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldnull[[:space:]]+throw[[:space:]]+\} // end of method '<msil>undef_vmaybe'$}}
; STUB-DAG:    {{^}}.method assembly static int32 '<msil>undef_vmaybe_bitcast'(int32, int32, float64) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldarg\.0[[:space:]]+call int32 '<msil>undef_vmaybe'\(int32\)[[:space:]]+ret[[:space:]]+\} // end of method '<msil>undef_vmaybe_bitcast'$}}

; IRSTUB-DAG: define internal i32 @"<msil>undef_maybe"(i32
; IRSTUB-DAG: define internal void @"<msil>undef_weak_ctor"()
; IRSTUB-DAG: define internal i32 @"<msil>undef_vmaybe"(i32 %{{[^,.)]*}})

;--- errors.ll
source_filename = "extern-weak-errors.c"

; TLS: {{^}}error: thread-local variable @tv is not supported for MSIL yet{{$}}

@tv = extern_weak thread_local global i32, align 4

; &tv != 0 for 'extern __thread int tv __attribute__((weak))'.
define i32 @tv_present() {
  %p = call ptr @llvm.threadlocal.address.p0(ptr @tv)
  %c = icmp ne ptr %p, null
  %r = zext i1 %c to i32
  ret i32 %r
}

declare nonnull ptr @llvm.threadlocal.address.p0(ptr nonnull)
