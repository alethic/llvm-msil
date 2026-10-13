// System headers that contain inline assembly in functions the program does not
// use compile cleanly for MSIL targets: assembly is rejected only in code that
// CodeGen emits (ImplementationPlan.md M1.d; clang.md 3.1). clang's intrin.h,
// for example, defines __movsb, __halt, __readmsr and friends with GNU asm.

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 -ffreestanding -emit-llvm-only -Werror %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 -ffreestanding -emit-llvm-only -Werror %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -ffreestanding -emit-llvm-only -Werror %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 -ffreestanding -emit-llvm -o - %s \
// RUN:   | FileCheck %s --implicit-check-not="asm sideeffect" --implicit-check-not=inteldialect
// CHECK: define {{.*}}i32 @main()

#ifdef _MSC_VER
#include <intrin.h>
#endif

#ifdef __x86_64__
#include <immintrin.h>
#include <x86intrin.h>
#ifndef _MSC_VER
#include <cpuid.h>
#endif
#endif

int main(void) { return 0; }
