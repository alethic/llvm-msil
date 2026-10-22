; Unreferenced discardable functions are dropped before they are diagnosed
; (ImplementationPlan.md M12.a (2), decision D26 as amended by the section 12
; row of 2026-10-21 for M12; recon p2/driver-toolchain.md 5.4 and
; driver-toolchain-files/logs/hosted-probe.log). UCRT's stdio.h defines its
; printf family as inline functions, which clang -O0 emits as linkonce_odr
; definitions in COMDATs even when nothing calls them: the variadic sprintf
; (a Phase 1 error, D19) calls _vsprintf_l, which calls the undefined
; __stdio_common_vsprintf (an 'undefined function' error, D14) and
; __local_stdio_printf_options, which returns the address of an internal
; global. A program that only calls puts failed to compile at -O0. Before any
; diagnostic, MSILPrepareIR deletes the body of every discardable function
; that nothing live reaches (as a diagnosed definition loses it, COMDAT
; cleared). It decides by reachability, as GlobalDCE does (the section 12 row
; 'D26 row of 2026-10-21 (unreferenced discardable functions) and M12.a (2)
; clarified', 2026-10-21): the candidates are every linkonce and linkonce_odr
; function definition and every internal or private function definition that
; a candidate's body refers to, directly or through other candidates; a
; candidate is live when something other than a candidate's body uses it (a
; function that is not a candidate, a global initializer, llvm.used,
; llvm.global_ctors) or a live candidate's body refers to it, and every other
; candidate loses its body. So dead cycles go too. Global variables are never
; dropped, and a local function that was unreferenced from the start, or a
; cycle of local functions that no linkonce function reaches, is not a
; candidate and stays (ret.ll).
;
; Here the UCRT shape (trimmed from clang -O0's output for a 'hello' program
; that includes stdio.h, without its datalayout and triple), an internal and a
; private helper only the dropped _vsprintf_l calls, an unreferenced linkonce
; function, a self-recursive linkonce_odr function, two unreferenced
; linkonce_odr functions that call each other, and an unreferenced
; linkonce_odr function that calls a cycle of two internal functions are
; dropped (both cycles call the undefined @not_an_import, which would be an
; 'undefined function' error, D14): none of them has a method, and nothing
; is reported (llc exits with 0 and prints nothing), on both triples at -O0
; and -O2. The internal global keeps its field. A linkonce_odr function that
; main calls, one in llvm.used, one whose address is in a global initializer,
; one in llvm.global_ctors, an internal helper that both main and the dropped
; body call, an internal function that nothing ever referenced, a cycle of
; two linkonce_odr functions that main calls and a cycle of two internal
; functions that nothing references keep their methods. The output
; assembles.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=".method " --implicit-check-not=".field " \
; DEFINE:   --implicit-check-not=__stdio_common_vsprintf --implicit-check-not=not_an_import
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o2.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,LNX --input-file=%t.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o2.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,WIN --input-file=%t.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,LNX --input-file=%t.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,WIN --input-file=%t.win.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=msil-prepare-ir < %s -o %t.prep.ll
; RUN: FileCheck %s --check-prefix=IR --input-file=%t.prep.ll --implicit-check-not="define {{.*}}@sprintf(" \
; RUN:   --implicit-check-not="define {{.*}}@_vsprintf_l(" \
; RUN:   --implicit-check-not="define {{.*}}@__local_stdio_printf_options(" \
; RUN:   --implicit-check-not="define {{.*}}@clamp_result(" --implicit-check-not="define {{.*}}@priv_helper(" \
; RUN:   --implicit-check-not="define {{.*}}@lo_unused(" --implicit-check-not="define {{.*}}@self_recursive(" \
; RUN:   --implicit-check-not="define {{.*}}@dead_"
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o2.dll %t.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

source_filename = "discardable-defs.c"

$sprintf = comdat any
$_vsprintf_l = comdat any
$__local_stdio_printf_options = comdat any
$"??_C@_05CJBACGMB@hello?$AA@" = comdat any

@"??_C@_05CJBACGMB@hello?$AA@" = linkonce_odr dso_local unnamed_addr constant [6 x i8] c"hello\00", comdat, align 1
@__local_stdio_printf_options._OptionsStorage = internal global i64 0, align 8
@fn_in_init = global ptr @odr_in_init, align 8
@ctor_ran = global i32 0, align 4
@llvm.used = appending global [1 x ptr] [ptr @odr_used], section "llvm.metadata"
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @odr_ctor, ptr null }]

; IR: @__local_stdio_printf_options._OptionsStorage = internal global i64 0, align 8

; ---- dropped -----------------------------------------------------------------

define linkonce_odr dso_local i32 @sprintf(ptr noundef %buf, ptr noundef %fmt, ...) comdat {
entry:
  %ap = alloca ptr, align 8
  call void @llvm.va_start.p0(ptr %ap)
  %list = load ptr, ptr %ap, align 8
  %r = call i32 @_vsprintf_l(ptr noundef %buf, ptr noundef %fmt, ptr noundef null, ptr noundef %list)
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %r
}

define linkonce_odr dso_local i32 @_vsprintf_l(ptr noundef %buf, ptr noundef %fmt, ptr noundef %loc, ptr noundef %list) comdat {
entry:
  %opts = call ptr @__local_stdio_printf_options()
  %o = load i64, ptr %opts, align 8
  %o1 = or i64 %o, 1
  %r = call i32 @__stdio_common_vsprintf(i64 noundef %o1, ptr noundef %buf, i64 noundef -1, ptr noundef %fmt, ptr noundef %loc, ptr noundef %list)
  %c = call i32 @clamp_result(i32 %r)
  %s = call i32 @shared_helper(i32 %c)
  %p = call i32 @priv_helper(i32 %s)
  ret i32 %p
}

define linkonce_odr dso_local ptr @__local_stdio_printf_options() comdat {
entry:
  ret ptr @__local_stdio_printf_options._OptionsStorage
}

declare dso_local i32 @__stdio_common_vsprintf(i64 noundef, ptr noundef, i64 noundef, ptr noundef, ptr noundef, ptr noundef)

define internal i32 @clamp_result(i32 %r) {
  %neg = icmp slt i32 %r, 0
  %v = select i1 %neg, i32 -1, i32 %r
  ret i32 %v
}

define private i32 @priv_helper(i32 %x) {
  %r = xor i32 %x, 5
  ret i32 %r
}

define linkonce i32 @lo_unused(i32 %x) {
  %r = mul i32 %x, 9
  ret i32 %r
}

define linkonce_odr i32 @self_recursive(i32 %n) {
entry:
  %z = icmp eq i32 %n, 0
  br i1 %z, label %done, label %more
more:
  %m = sub i32 %n, 1
  %r = call i32 @self_recursive(i32 %m)
  ret i32 %r
done:
  ret i32 0
}

; Two unreferenced linkonce_odr functions that call each other (a dead cycle);
; one calls an undefined function that is not an import.
declare i32 @not_an_import(i32)

define linkonce_odr i32 @dead_even(i32 %n) {
entry:
  %z = icmp eq i32 %n, 0
  br i1 %z, label %done, label %rec
rec:
  %x = call i32 @not_an_import(i32 %n)
  %m = sub i32 %n, 1
  %r = call i32 @dead_odd(i32 %m)
  %s = add i32 %r, %x
  ret i32 %s
done:
  ret i32 1
}

define linkonce_odr i32 @dead_odd(i32 %n) {
entry:
  %z = icmp eq i32 %n, 0
  br i1 %z, label %done, label %rec
rec:
  %m = sub i32 %n, 1
  %r = call i32 @dead_even(i32 %m)
  ret i32 %r
done:
  ret i32 0
}

; An unreferenced linkonce_odr function that calls a cycle of two internal
; functions, one of which calls the undefined function.
define linkonce_odr i32 @dead_entry(i32 %n) {
  %r = call i32 @dead_local_a(i32 %n)
  ret i32 %r
}

define internal i32 @dead_local_a(i32 %n) {
entry:
  %z = icmp eq i32 %n, 0
  br i1 %z, label %done, label %rec
rec:
  %x = call i32 @not_an_import(i32 %n)
  %m = sub i32 %n, 1
  %r = call i32 @dead_local_b(i32 %m)
  %s = add i32 %r, %x
  ret i32 %s
done:
  ret i32 0
}

define internal i32 @dead_local_b(i32 %n) {
  %r = call i32 @dead_local_a(i32 %n)
  ret i32 %r
}

; ---- kept --------------------------------------------------------------------

; Called by main and by the dropped _vsprintf_l.
; IR:          define internal i32 @shared_helper(
; CHECK-LABEL: {{^}}.method assembly static int32 'shared_helper'(int32) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'shared_helper'{{$}}
define internal i32 @shared_helper(i32 %x) {
  %r = add i32 %x, 3
  ret i32 %r
}

; IR:          define linkonce_odr i32 @odr_called(
; CHECK-LABEL: {{^}}.method public static int32 'odr_called'(int32) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'odr_called'{{$}}
define linkonce_odr i32 @odr_called(i32 %x) {
  %r = shl i32 %x, 1
  ret i32 %r
}

; IR:          define linkonce_odr i32 @odr_used(
; CHECK-LABEL: {{^}}.method public static int32 'odr_used'() cil managed{{$}}
; CHECK:       {{^}}} // end of method 'odr_used'{{$}}
define linkonce_odr i32 @odr_used() {
  ret i32 11
}

; IR:          define linkonce_odr i32 @odr_in_init(
; CHECK-LABEL: {{^}}.method public static int32 'odr_in_init'() cil managed{{$}}
; CHECK:       {{^}}} // end of method 'odr_in_init'{{$}}
define linkonce_odr i32 @odr_in_init() {
  ret i32 12
}

; IR:          define linkonce_odr void @odr_ctor(
; CHECK-LABEL: {{^}}.method public static void 'odr_ctor'() cil managed{{$}}
; CHECK:       {{^}}} // end of method 'odr_ctor'{{$}}
define linkonce_odr void @odr_ctor() {
  store i32 1, ptr @ctor_ran, align 4
  ret void
}

; IR:          define internal i32 @unreferenced_internal(
; CHECK-LABEL: {{^}}.method assembly static int32 'unreferenced_internal'() cil managed{{$}}
; CHECK:       {{^}}} // end of method 'unreferenced_internal'{{$}}
define internal i32 @unreferenced_internal() {
  ret i32 13
}

; A cycle of two linkonce_odr functions that main calls: both are live.
; IR:          define linkonce_odr i32 @odr_even(
; CHECK-LABEL: {{^}}.method public static int32 'odr_even'(int32) cil managed{{$}}
; CHECK:       {{^}}  call int32 'odr_odd'(int32){{$}}
; CHECK:       {{^}}} // end of method 'odr_even'{{$}}
define linkonce_odr i32 @odr_even(i32 %n) {
entry:
  %z = icmp eq i32 %n, 0
  br i1 %z, label %done, label %rec
rec:
  %m = sub i32 %n, 1
  %r = call i32 @odr_odd(i32 %m)
  ret i32 %r
done:
  ret i32 1
}

; IR:          define linkonce_odr i32 @odr_odd(
; CHECK-LABEL: {{^}}.method public static int32 'odr_odd'(int32) cil managed{{$}}
; CHECK:       {{^}}  call int32 'odr_even'(int32){{$}}
; CHECK:       {{^}}} // end of method 'odr_odd'{{$}}
define linkonce_odr i32 @odr_odd(i32 %n) {
entry:
  %z = icmp eq i32 %n, 0
  br i1 %z, label %done, label %rec
rec:
  %m = sub i32 %n, 1
  %r = call i32 @odr_even(i32 %m)
  ret i32 %r
done:
  ret i32 0
}

; A cycle of two internal functions that nothing references and no linkonce
; function reaches: not candidates, so both stay.
; IR:          define internal i32 @local_cycle_a(
; CHECK-LABEL: {{^}}.method assembly static int32 'local_cycle_a'(int32) cil managed{{$}}
; CHECK:       {{^}}  call int32 'local_cycle_b'(int32){{$}}
; CHECK:       {{^}}} // end of method 'local_cycle_a'{{$}}
define internal i32 @local_cycle_a(i32 %n) {
entry:
  %z = icmp eq i32 %n, 0
  br i1 %z, label %done, label %rec
rec:
  %m = sub i32 %n, 1
  %r = call i32 @local_cycle_b(i32 %m)
  ret i32 %r
done:
  ret i32 0
}

; IR:          define internal i32 @local_cycle_b(
; CHECK-LABEL: {{^}}.method assembly static int32 'local_cycle_b'(int32) cil managed{{$}}
; CHECK:       {{^}}  call int32 'local_cycle_a'(int32){{$}}
; CHECK:       {{^}}} // end of method 'local_cycle_b'{{$}}
define internal i32 @local_cycle_b(i32 %n) {
  %r = call i32 @local_cycle_a(i32 %n)
  ret i32 %r
}

; IR:          define dso_local i32 @main(
; CHECK-LABEL: {{^}}.method public static int32 'main'() cil managed{{$}}
; CHECK:       {{^}}  ldsfld native int '??_C@_05CJBACGMB@hello?$AA@'{{$}}
; CHECK-NEXT:  {{^}}  call int32 'puts'(native int){{$}}
; CHECK-NEXT:  {{^}}  call int32 'odr_called'(int32){{$}}
; CHECK-NEXT:  {{^}}  call int32 'shared_helper'(int32){{$}}
; CHECK-NEXT:  {{^}}  call int32 'odr_even'(int32){{$}}
; CHECK:       {{^}}} // end of method 'main'{{$}}
define dso_local i32 @main() {
entry:
  %call = call i32 @puts(ptr noundef @"??_C@_05CJBACGMB@hello?$AA@")
  %a = call i32 @odr_called(i32 %call)
  %b = call i32 @shared_helper(i32 %a)
  %d = call i32 @odr_even(i32 %b)
  %c = and i32 %d, 0
  ret i32 %c
}

declare dso_local i32 @puts(ptr noundef)
declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)

; One P/Invoke (puts); the data keeps the internal global's field.
; LNX:         {{^}}.method public hidebysig static pinvokeimpl("libc" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; WIN:         {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; CHECK:       {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>data'{{$}}
; CHECK-NEXT:  {{^}}.field public static initonly native int '??_C@_05CJBACGMB@hello?$AA@'{{$}}
; CHECK-NEXT:  {{^}}.field public static initonly native int 'fn_in_init'{{$}}
; CHECK-NEXT:  {{^}}.field assembly static initonly native int '__local_stdio_printf_options._OptionsStorage'{{$}}
; CHECK-NEXT:  {{^}}.field public static initonly native int 'ctor_ran'{{$}}
; CHECK:       {{^}}.field private static initonly native int '<msil>fn_odr_in_init'{{$}}
; CHECK:       {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK:       {{^}}  call void 'odr_ctor'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '.cctor'{{$}}
; CHECK:       {{^}}.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed{{$}}
