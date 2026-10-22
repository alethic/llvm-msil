; A named section on a global variable definition is an error in
; whole-program mode (ImplementationPlan.md M12.a (1c), decision D26 as
; amended by the section 12 row of 2026-10-21 for M12; recon
; p2/judge-c-fidelity.md F4). Until then the section was dropped silently, so
; a constructor registered through '.init_array' (ELF) or '.CRT$XCU' (MSVC),
; as in judge-c-fidelity's p8.c, never ran. MSILPrepareIR now reports every
; global variable definition with an explicit section, except what
; MSIL::isSpecialLLVMGlobal accepts (llvm.used, llvm.compiler.used and globals
; in section "llvm.metadata"), with LLVMContext::emitError and the text
; 'global variable @<name> in section "<section>" is not supported for MSIL
; yet (phase 2 CIL objects)' (IR name, section verbatim), once per global, in
; module order and before any function diagnostic; the global stays ordinary
; data, so llc exits with 1 and the IL on stdout still has its fields and
; assembles. A declaration with a section, a function with a section and the
; llvm.metadata globals give nothing, and neither do clang's own globals
; without a section (an MSVC string literal in a COMDAT and a '__const' local
; initializer). M20 retires this error (whole-program output then lays out
; named sections as a one-object link does) and updates this test.

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s > %t.lnx.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s > %t.win.o0.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.dll %t.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

source_filename = "section-errors.c"

; The globals, in module order, then the one function diagnostic.
; CHECK:      {{^}}error: {{.*}}global variable @init_array_ctor in section ".init_array" is not supported for MSIL yet (phase 2 CIL objects){{$}}
; CHECK:      {{^}}error: {{.*}}global variable @crt_ctor in section ".CRT$XCU" is not supported for MSIL yet (phase 2 CIL objects){{$}}
; CHECK:      {{^}}error: {{.*}}global variable @registry in section "myreg" is not supported for MSIL yet (phase 2 CIL objects){{$}}
; CHECK:      {{^}}error: {{.*}}global variable @custom_data in section ".data.custom" is not supported for MSIL yet (phase 2 CIL objects){{$}}
; CHECK:      {{^}}error: {{.*}}global variable @ro_const in section ".rodata.x" is not supported for MSIL yet (phase 2 CIL objects){{$}}
; CHECK:      {{^}}error: {{.*}}in function uses_asm {{.*}}: inline assembly is not supported for MSIL{{$}}

$"??_C@_02DPKJAMEF@ok?$AA@" = comdat any

@counter = global i32 1, align 4
@init_array_ctor = internal global ptr @init_fn, section ".init_array", align 8
@crt_ctor = internal global ptr @init_fn, section ".CRT$XCU", align 8
@registry = global [2 x i32] [i32 3, i32 4], section "myreg", align 4
@custom_data = global i32 5, section ".data.custom", align 4
@ro_const = constant i32 6, section ".rodata.x", align 4
@"??_C@_02DPKJAMEF@ok?$AA@" = linkonce_odr dso_local unnamed_addr constant [3 x i8] c"ok\00", comdat, align 1
@__const.main.pair = private unnamed_addr constant [2 x i32] [i32 7, i32 8], align 4
@ext_in_section = external global i32, section "ext.sec", align 4
@.annotation = private unnamed_addr constant [5 x i8] c"note\00", section "llvm.metadata"
@llvm.used = appending global [3 x ptr] [ptr @init_array_ctor, ptr @crt_ctor, ptr @registry], section "llvm.metadata"

; IL-LABEL: {{^}}.method assembly static void 'init_fn'() cil managed{{$}}
define internal void @init_fn() {
  store i32 40, ptr @counter, align 4
  ret void
}

; A function in a section compiles as before.
; IL-LABEL: {{^}}.method public static int32 'in_text_section'(int32) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 2{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.1{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
define i32 @in_text_section(i32 %x) section ".text.hot" {
  %r = add i32 %x, 1
  ret i32 %r
}

define void @uses_asm() {
  call void asm sideeffect "nop", ""()
  ret void
}

; IL-LABEL: {{^}}.method public static int32 'main'() cil managed{{$}}
; IL:       {{^}}  ldsfld native int 'counter'{{$}}
; IL:       {{^}}  ldsfld native int 'registry'{{$}}
; IL:       {{^}}  ldsfld native int 'custom_data'{{$}}
; IL:       {{^}}  ldsfld native int 'ro_const'{{$}}
; IL:       {{^}}  ldsfld native int '__const.main.pair'{{$}}
; IL:       {{^}}  ldsfld native int '??_C@_02DPKJAMEF@ok?$AA@'{{$}}
; IL:       {{^}}} // end of method 'main'{{$}}
define i32 @main() {
  %c = load i32, ptr @counter, align 4
  %r = load i32, ptr @registry, align 4
  %d = load i32, ptr @custom_data, align 4
  %o = load i32, ptr @ro_const, align 4
  %p = load i32, ptr @__const.main.pair, align 4
  %k = load i8, ptr @"??_C@_02DPKJAMEF@ok?$AA@", align 1
  %kz = zext i8 %k to i32
  %s1 = add i32 %c, %r
  %s2 = add i32 %s1, %d
  %s3 = add i32 %s2, %o
  %s4 = add i32 %s3, %p
  %s5 = add i32 %s4, %kz
  ret i32 %s5
}

; Every global with a section stays ordinary data with its field; the
; llvm.metadata globals and the unused declaration have none.
; IL:      {{^}}.field public static initonly native int 'counter'{{$}}
; IL-NEXT: {{^}}.field assembly static initonly native int 'init_array_ctor'{{$}}
; IL-NEXT: {{^}}.field assembly static initonly native int 'crt_ctor'{{$}}
; IL-NEXT: {{^}}.field public static initonly native int 'registry'{{$}}
; IL-NEXT: {{^}}.field public static initonly native int 'custom_data'{{$}}
; IL-NEXT: {{^}}.field public static initonly native int 'ro_const'{{$}}
; IL-NEXT: {{^}}.field public static initonly native int '??_C@_02DPKJAMEF@ok?$AA@'{{$}}
; IL-NEXT: {{^}}.field assembly static initonly native int '__const.main.pair'{{$}}
; IL-NOT:  {{^}}.field {{.*'(ext_in_section|\.annotation|llvm\.used)'}}
; IL:      {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
