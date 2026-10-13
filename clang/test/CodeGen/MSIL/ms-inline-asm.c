// Microsoft-style inline assembly on MSIL targets (ImplementationPlan.md M1.d;
// clang.md 3.2): the parser builds the statement without looking up an MC
// target (the MSIL target has no assembler, and an MSIL triple never selects
// the native backend), for every architecture, and CodeGen rejects it only when
// the enclosing function is emitted.

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -fasm-blocks -emit-llvm-only %s -verify
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fms-extensions -fasm-blocks -emit-llvm-only %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fasm-blocks -emit-llvm-only %s -verify

// Nothing is diagnosed before CodeGen, not even under -Wall and not even on
// aarch64, where the native parser rejects MS-style assembly for the
// architecture. As natively, a local whose only use is as an operand of
// MS-style assembly counts as used (no -Wunused-variable or
// -Wunused-but-set-variable).
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -fasm-blocks -Wall -Werror -fsyntax-only %s \
// RUN:   -verify=syntax
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fms-extensions -fasm-blocks -Wall -Werror -fsyntax-only %s \
// RUN:   -verify=syntax
// RUN: %if x86-registered-target %{ %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions -fasm-blocks -Wall -Werror \
// RUN:   -emit-llvm-only %s -verify=syntax %}
// syntax-no-diagnostics

// A translation unit whose MS-style assembly is never emitted compiles cleanly,
// also under -Wall.
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -fasm-blocks -emit-llvm -o - -DNO_EMITTED_ASM \
// RUN:   %s -verify=syntax | FileCheck --check-prefix=CLEAN --implicit-check-not=inteldialect \
// RUN:   --implicit-check-not="asm sideeffect" --implicit-check-not=@unused_ms_asm %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -fasm-blocks -Wall -Werror -emit-llvm-only \
// RUN:   -DNO_EMITTED_ASM %s -verify=syntax
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fms-extensions -fasm-blocks -Wall -Werror -emit-llvm-only \
// RUN:   -DNO_EMITTED_ASM %s -verify=syntax
// CLEAN: define {{.*}}i32 @no_asm(

// The exact diagnostic text names the kind of assembly and the triple.
// RUN: not %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -fasm-blocks -emit-llvm-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN-DIAG --implicit-check-not="PLEASE submit" %s
// RUN: not %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fms-extensions -fasm-blocks -emit-llvm-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=A64-DIAG --implicit-check-not="PLEASE submit" %s
// WIN-DIAG-COUNT-2: error: Microsoft-style inline assembly is not supported for the MSIL target 'x86_64-pc-windows-msvc-msil'
// WIN-DIAG: 2 errors generated.
// A64-DIAG-COUNT-2: error: Microsoft-style inline assembly is not supported for the MSIL target 'aarch64-pc-windows-msvc-msil'
// A64-DIAG: 2 errors generated.

// The native x86_64 triple still parses MS-style assembly with the X86 MC
// assembler.
// RUN: %if x86-registered-target %{ %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions -fasm-blocks -emit-llvm -o - %s | FileCheck --check-prefix=NATIVE %s %}
// NATIVE-LABEL: define {{.*}}i32 @ms_asm(
// NATIVE: call void asm sideeffect inteldialect "int $$3"
// NATIVE: call void asm sideeffect inteldialect "mov eax, $0{{.*}}add eax, $$1"

// Not emitted, so not diagnosed. The 'unused' attributes only keep -Wall from
// reporting the functions themselves (-Wunused-function), as it would
// natively.
static void __attribute__((unused)) unused_ms_asm(void) {
  __asm int 3
}

// A never-emitted static inline function, as in headers such as intrin.h: x is
// used only as an assembly operand, and y is set and otherwise used only there.
static inline void __attribute__((unused)) unused_ms_asm_operands(void) {
  int x = 1;
  __asm mov eax, x
  int y;
  y = 1;
  __asm mov eax, y
}

int no_asm(int x) { return x + 1; }

#ifndef NO_EMITTED_ASM
int ms_asm(int x) {
  __asm int 3 // expected-error {{Microsoft-style inline assembly is not supported for the MSIL target}}
  x += 1;
  __asm { // expected-error {{Microsoft-style inline assembly is not supported for the MSIL target}}
    mov eax, x
    add eax, 1
  }
  return x;
}
#endif
