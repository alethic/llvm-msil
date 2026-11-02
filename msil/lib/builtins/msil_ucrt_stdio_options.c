//===-- msil_ucrt_stdio_options.c - The UCRT stdio options words ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The UCRT's inline printf and scanf functions (and msil_ucrt_stdio.c's)
// pass the word that __local_stdio_printf_options() (resp.
// __local_stdio_scanf_options()) returns to __stdio_common_v*: one
// unsigned long long per program, which the UCRT's <stdio.h> defines as a
// COMDAT function with its own static variable in every translation unit
// that includes it. This member of the helper archive of the
// *-windows-msvc-msil triples (ImplementationPlan.md 5.15.2, 5.14.3 A6)
// defines both functions for programs that include no <stdio.h> and call
// msil_ucrt_stdio.c's functions, so it is extracted only for such code. The
// definitions are weak, so they never collide with a COMDAT copy. When the
// link defines either function, msil-ld's module initializer ORs vcruntime's
// startup defaults into its word before the constructors run (hosted-libc
// H3), as the native CRT startup does.
//
//===----------------------------------------------------------------------===//

__attribute__((weak)) unsigned long long *__local_stdio_printf_options(void) {
  static unsigned long long options;
  return &options;
}

__attribute__((weak)) unsigned long long *__local_stdio_scanf_options(void) {
  static unsigned long long options;
  return &options;
}
