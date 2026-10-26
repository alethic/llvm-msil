; Named sections on global variables in whole-program mode: what stays an error
; (ImplementationPlan.md 5.9.9 and 5.8.10, M20; D26 as amended by the section
; 12 rows of 2026-10-21 for M12 and M20; recon p2/judge-c-fidelity.md F4).
; M12 made every global variable definition with an explicit section an error
; ('global variable @<name> in section "<section>" is not supported for MSIL
; yet (phase 2 CIL objects)'), because Phase 1 dropped the section silently
; and a constructor registered through '.init_array' (ELF) or '.CRT$XCU'
; (MSVC), as in judge-c-fidelity's p8.c, never ran. From M20 whole-program
; output lays named sections out as a one-object link does
; (sections-whole-program.ll), so that text is gone: data in 'myreg',
; '.data.custom' or '.rodata.x' compiles, and a constructor slot in
; '.init_array' (ELF) or '.CRT$XCU' (COFF) runs from .cctor. What stays an
; error has 5.9.9's texts, with the module's source file name as <file>:
; - a destructor table (.fini_array*, .CRT$XT*): 'global destructors are not
;   supported yet: <s> in <file>' (P3-A);
; - .ctors*/.dtors* and the other .CRT$X* sections: 'section '<name>' in
;   <file> is not supported' (no silent drop, judge-c-fidelity J6);
; - a section named .tdata or .tbss: 'thread-local storage is not supported
;   yet: <s> in <file>';
; - an 8-byte constructor-table slot that holds no function address:
;   'constructor table entry at <section>+<offset> in <file> is not a
;   function address' (here the first and only global of its section, at
;   offset 8).
; A declaration with a section, a function with a section, the llvm.metadata
; globals and clang's own globals (an MSVC string literal in a COMDAT, a
; '__const' local initializer) give nothing. Section diagnostics and function
; diagnostics (here inline assembly) are reported together, in any order,
; llc exits with 1 without a crash, and the IL that llc still writes keeps
; every field and assembles (plan section 2). One module per triple, since
; .init_array and .CRT$X* mean something on one triple each.

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=warning: --implicit-check-not="phase 2 CIL objects"
; RUN: split-file %s %t
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/lnx.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LNX
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/lnx.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LNX
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/lnx.ll -o /dev/null 2>&1 \
; RUN:   | grep -c "error: " | FileCheck %s --check-prefix=NERR
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/win.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=WIN
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/win.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=WIN
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/win.ll -o /dev/null 2>&1 \
; RUN:   | grep -c "error: " | FileCheck %s --check-prefix=NERR
; NERR: {{^}}5{{$}}

; The IL on stdout.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/lnx.ll > %t/lnx.il
; RUN: FileCheck %s --check-prefixes=IL,IL-LNX --input-file=%t/lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/win.ll \
; RUN:   > %t/win.o0.il
; RUN: FileCheck %s --check-prefixes=IL,IL-WIN --input-file=%t/win.o0.il
; DEFINE: %{ilban} = FileCheck %s --check-prefix=ILBAN --implicit-check-not="'ext_in_section'" \
; DEFINE:   --implicit-check-not="'.annotation'" --implicit-check-not="'llvm.used'"
; RUN: %{ilban} --input-file=%t/lnx.il
; RUN: %{ilban} --input-file=%t/win.o0.il
; ILBAN: {{^}}.assembly 'section-errors' { .ver 0:0:0:0 }{{$}}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/lnx.dll %t/lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/win.o0.dll %t/win.o0.il %}

;--- lnx.ll
source_filename = "section-errors.c"

$"??_C@_02DPKJAMEF@ok?$AA@" = comdat any

@counter = global i32 1, align 4
; Compiles: a constructor slot after a bad one, and three data sections.
@bad_slot = internal global [2 x ptr] [ptr @init_fn, ptr @counter], section ".init_array", align 8
@init_array_ctor = internal global ptr @init_fn, section ".init_array", align 8
@registry = global [2 x i32] [i32 3, i32 4], section "myreg", align 4
@custom_data = global i32 5, section ".data.custom", align 4
@ro_const = constant i32 6, section ".rodata.x", align 4
; Errors.
@fini_ptr = internal global ptr @init_fn, section ".fini_array", align 8
@old_ctors = internal global ptr @init_fn, section ".ctors.65435", align 8
@tls_bss = global i32 0, section ".tbss", align 4
; Nothing.
@"??_C@_02DPKJAMEF@ok?$AA@" = linkonce_odr dso_local unnamed_addr constant [3 x i8] c"ok\00", comdat, align 1
@__const.main.pair = private unnamed_addr constant [2 x i32] [i32 7, i32 8], align 4
@ext_in_section = external global i32, section "ext.sec", align 4
@.annotation = private unnamed_addr constant [5 x i8] c"note\00", section "llvm.metadata"
@llvm.used = appending global [5 x ptr] [ptr @bad_slot, ptr @init_array_ctor, ptr @registry, ptr @fini_ptr, ptr @old_ctors], section "llvm.metadata"

; LNX-DAG: {{^}}error: {{.*}}global destructors are not supported yet: {{.+}} in section-errors.c{{$}}
; LNX-DAG: {{^}}error: {{.*}}section '.ctors.65435' in section-errors.c is not supported{{$}}
; LNX-DAG: {{^}}error: {{.*}}thread-local storage is not supported yet: {{.+}} in section-errors.c{{$}}
; LNX-DAG: {{^}}error: {{.*}}constructor table entry at .init_array+8 in section-errors.c is not a function address{{$}}
; LNX-DAG: {{^}}error: {{.*}}in function uses_asm {{.*}}: inline assembly is not supported for MSIL{{$}}

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

; The globals that compile keep their fields; the declaration and the
; llvm.metadata globals have none (ILBAN).
; IL-DAG:     {{^}}.field public static initonly native int 'counter'{{$}}
; IL-DAG:     {{^}}.field public static initonly native int 'registry'{{$}}
; IL-DAG:     {{^}}.field public static initonly native int 'custom_data'{{$}}
; IL-DAG:     {{^}}.field public static initonly native int 'ro_const'{{$}}
; IL-DAG:     {{^}}.field public static initonly native int '??_C@_02DPKJAMEF@ok?$AA@'{{$}}
; IL-DAG:     {{^}}.field assembly static initonly native int '__const.main.pair'{{$}}
; IL-LNX-DAG: {{^}}.field assembly static initonly native int 'init_array_ctor'{{$}}
; IL-WIN-DAG: {{^}}.field assembly static initonly native int 'crt_ctor'{{$}}
; IL:         {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}

;--- win.ll
source_filename = "section-errors.c"

$"??_C@_02DPKJAMEF@ok?$AA@" = comdat any

@counter = global i32 1, align 4
; Compiles: a constructor slot after a bad one, and three data sections.
@bad_slot = internal constant [2 x ptr] [ptr @init_fn, ptr @counter], section ".CRT$XCU", align 8
@crt_ctor = internal constant ptr @init_fn, section ".CRT$XCU", align 8
@registry = global [2 x i32] [i32 3, i32 4], section "myreg", align 4
@custom_data = global i32 5, section ".data.custom", align 4
@ro_const = constant i32 6, section ".rodata.x", align 4
; Errors.
@term_ptr = internal constant ptr @init_fn, section ".CRT$XTZ", align 8
@crt_xic = internal constant ptr @init_fn, section ".CRT$XIC", align 8
@tls_data = global i32 3, section ".tdata", align 4
; Nothing.
@"??_C@_02DPKJAMEF@ok?$AA@" = linkonce_odr dso_local unnamed_addr constant [3 x i8] c"ok\00", comdat, align 1
@__const.main.pair = private unnamed_addr constant [2 x i32] [i32 7, i32 8], align 4
@ext_in_section = external global i32, section "ext.sec", align 4
@.annotation = private unnamed_addr constant [5 x i8] c"note\00", section "llvm.metadata"
@llvm.used = appending global [5 x ptr] [ptr @bad_slot, ptr @crt_ctor, ptr @registry, ptr @term_ptr, ptr @crt_xic], section "llvm.metadata"

; WIN-DAG: {{^}}error: {{.*}}global destructors are not supported yet: {{.+}} in section-errors.c{{$}}
; WIN-DAG: {{^}}error: {{.*}}section '.CRT$XIC' in section-errors.c is not supported{{$}}
; WIN-DAG: {{^}}error: {{.*}}thread-local storage is not supported yet: {{.+}} in section-errors.c{{$}}
; WIN-DAG: {{^}}error: {{.*}}constructor table entry at .CRT$XCU+8 in section-errors.c is not a function address{{$}}
; WIN-DAG: {{^}}error: {{.*}}in function uses_asm {{.*}}: inline assembly is not supported for MSIL{{$}}

define internal void @init_fn() {
  store i32 40, ptr @counter, align 4
  ret void
}

define i32 @in_text_section(i32 %x) section ".text.hot" {
  %r = add i32 %x, 1
  ret i32 %r
}

define void @uses_asm() {
  call void asm sideeffect "nop", ""()
  ret void
}

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
