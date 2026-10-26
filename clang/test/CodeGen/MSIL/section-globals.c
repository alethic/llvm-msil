// An explicit section on a global variable definition in whole-program output
// (ImplementationPlan.md 5.8.10 and 5.9.9, M20; D26 as amended by the section
// 12 rows of 2026-10-21 for M12 and M20; judge-c-fidelity F4). Phase 1
// ignored the section, so a constructor registered through '.init_array'
// (ELF) or '.CRT$XCU' (MSVC), which the native startup code runs, silently
// never ran (judge-c-fidelity probe p8.c: 42 natively, 3 on MSIL); M12 made
// it the error 'global variable @<name> in section "<section>" is not
// supported for MSIL yet (phase 2 CIL objects)'. From M20 '-S' output lays
// named sections out as a one-object link does: p8.c compiles without a
// diagnostic on both x86_64 MSIL triples, its 8-byte '.init_array' or
// '.CRT$XCU' slot is a constructor that <Module>::.cctor calls after the
// relocations (after storing init_fn's address into the slot), and the data
// in "myreg" is ordinary data in its own section. A function's section is
// still ignored. The frontend emits the sections unchanged, without a
// diagnostic (IR runs). Constructs that stay errors are pinned by
// llvm/test/CodeGen/MSIL/section-errors.ll; layouts, start/stop symbols and
// constructor order by sections-whole-program.ll.
//
// This is p8.c on both x86_64 MSIL triples (-fms-extensions for MSVC's
// '#pragma section' and '__declspec(allocate)'), plus data in a section named
// "myreg" and a function in a section of its own. Layout: counter +0,
// p_init +8, myreg_data +16.

// REQUIRES: msil-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -o %t.lnx.il %s 2>&1 | count 0
// RUN: FileCheck %s --check-prefix=IL --implicit-check-not="phase 2 CIL objects" --input-file=%t.lnx.il
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -S -o %t.win.il %s 2>&1 | count 0
// RUN: FileCheck %s --check-prefix=IL --implicit-check-not="phase 2 CIL objects" --input-file=%t.win.il
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -S -o %t.lnx.o2.il %s 2>&1 | count 0
// RUN: FileCheck %s --check-prefix=IL --input-file=%t.lnx.o2.il
// RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.dll %t.lnx.il %}
// RUN: %if ilasm %{ %ilasm-verify -output=%t.win.dll %t.win.il %}

// IL-DAG:  {{^}}.field public static initonly native int 'counter'{{( |$)}}
// IL-DAG:  {{^}}.field assembly static initonly native int 'p_init'{{( |$)}}
// IL-DAG:  {{^}}.field public static initonly native int 'myreg_data'{{( |$)}}
// IL-DAG:  {{^}}.field private static initonly native int '<msil>fn_init_fn'{{( |$)}}
// IL:      {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
// IL:      {{^}}  {{ldc\.i4(\.s)? 20( |$)}}
// IL-NEXT: {{^}}  cpblk{{$}}
// IL-NEXT: {{^}}  ldsfld native int '<msil>data'{{$}}
// IL-NEXT: {{^}}  stsfld native int 'counter'{{$}}
// IL-NEXT: {{^}}  ldsfld native int '<msil>data'{{$}}
// IL-NEXT: {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
// IL-NEXT: {{^}}  add{{$}}
// IL-NEXT: {{^}}  stsfld native int 'p_init'{{$}}
// IL-NEXT: {{^}}  ldsfld native int '<msil>data'{{$}}
// IL-NEXT: {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
// IL-NEXT: {{^}}  add{{$}}
// IL-NEXT: {{^}}  stsfld native int 'myreg_data'{{$}}
// IL-NEXT: {{^}}  ldftn void 'init_fn'(){{$}}
// IL-NEXT: {{^}}  stsfld native int '<msil>fn_init_fn'{{$}}
// IL-NEXT: {{^}}  ldsfld native int '<msil>data'{{$}}
// IL-NEXT: {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
// IL-NEXT: {{^}}  add{{$}}
// IL-NEXT: {{^}}  ldsfld native int '<msil>fn_init_fn'{{$}}
// IL-NEXT: {{^}}  stind.i{{$}}
// IL-NEXT: {{^}}  call void 'init_fn'(){{$}}
// IL-NEXT: {{^}}  ret{{$}}
// IL-NEXT: {{^}}} // end of method '.cctor'{{$}}

// The frontend keeps the sections and diagnoses nothing.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - %s -verify=ok \
// RUN:   | FileCheck %s --check-prefixes=IR,IR-LNX
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -emit-llvm -o - %s -verify=ok \
// RUN:   | FileCheck %s --check-prefixes=IR,IR-WIN
// ok-no-diagnostics

// IR:     @counter = global i32 1, align 4
// IR-LNX: @p_init = internal global ptr @init_fn, section ".init_array"
// IR-WIN: @p_init = internal constant ptr @init_fn, section ".CRT$XCU"
// IR:     @myreg_data = global i32 5, section "myreg"
// IR:     @llvm.used = appending global [1 x ptr] [ptr @p_init], section "llvm.metadata"
// IR:     define {{.*}}i32 @hot_fn() {{.*}}section ".text.hot_fn"

/* p8.c: a constructor registered through a named section, a common C idiom:
   ELF '.init_array' and MSVC '.CRT$XCU' (both read by the native startup code). */
int counter = 1;
static void init_fn(void) { counter = 40; }
#if defined(_WIN32)
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) __attribute__((used)) static void (*p_init)(void) = init_fn;
#else
__attribute__((used, section(".init_array"))) static void (*p_init)(void) = init_fn;
#endif
int main(void) { return counter + 2; }   /* native: 42 */

// Data in a section of its own, and a function in a section: the function's
// section is ignored.
__attribute__((section("myreg"))) int myreg_data = 5;
__attribute__((section(".text.hot_fn"))) int hot_fn(void) { return myreg_data; }
