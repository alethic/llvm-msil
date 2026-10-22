// An explicit section on a global variable definition is an error in
// whole-program output (Milestone 12, judge-c-fidelity F4, plan D26 and the
// section 12 row 'D26 amended (whole-program mode, ...)'). Before M12 the
// backend ignored the section, so a constructor registered through
// '.init_array' (ELF) or '.CRT$XCU' (MSVC), which the native startup code
// runs, silently never ran (judge-c-fidelity probe p8.c: 42 natively, 3 on
// MSIL). Now MSILPrepareIR reports every global variable definition with a
// section, in module order and before any function diagnostic, as
//   global variable @<name> in section "<section>" is not supported for MSIL
//   yet (phase 2 CIL objects)
// and leaves the global as ordinary data, so the output stays well formed
// (plan section 2). Not reported: a function with a section (ignored), and
// clang's own globals in the "llvm.metadata" section (llvm.used from
// __attribute__((used))). The frontend emits the sections unchanged, without
// a diagnostic (IR runs). M20 retires this error, when whole-program output
// lays out named sections as a one-object link does (plan section 12 row
// 'D26 amended (Q5; ...)', M20.d updates this test); until then
// llvm/test/CodeGen/MSIL/section-errors.ll pins the same text for llc.
//
// This is p8.c on both x86_64 MSIL triples (-fms-extensions for MSVC's
// '#pragma section' and '__declspec(allocate)'), plus data in a section named
// "myreg" and a function in a section of its own.

// REQUIRES: msil-registered-target

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -o %t.lnx.il %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LNX --implicit-check-not=error: --implicit-check-not=warning: \
// RUN:     --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
// RUN: not %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -S -o %t.win.il %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WIN --implicit-check-not=error: --implicit-check-not=warning: \
// RUN:     --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"

// LNX:      {{^}}error: global variable @p_init in section ".init_array" is not supported for MSIL yet (phase 2 CIL objects){{$}}
// LNX-NEXT: {{^}}error: global variable @myreg_data in section "myreg" is not supported for MSIL yet (phase 2 CIL objects){{$}}
// LNX-NEXT: {{^}}2 errors generated.{{$}}

// WIN:      {{^}}error: global variable @p_init in section ".CRT$XCU" is not supported for MSIL yet (phase 2 CIL objects){{$}}
// WIN-NEXT: {{^}}error: global variable @myreg_data in section "myreg" is not supported for MSIL yet (phase 2 CIL objects){{$}}
// WIN-NEXT: {{^}}2 errors generated.{{$}}

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
