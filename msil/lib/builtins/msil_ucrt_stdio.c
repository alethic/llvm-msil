//===-- msil_ucrt_stdio.c - The UCRT printf family for MSIL ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The UCRT exports no printf: its <stdio.h> defines the printf and scanf
// families as inline functions over the exports __stdio_common_vfprintf,
// __stdio_common_vsprintf, __stdio_common_vfscanf, __stdio_common_vsscanf and
// their wide forms, and every translation unit that includes the header
// carries its own COMDAT copies. Code that declares printf without the
// header, or defines _NO_CRT_STDIO_INLINE, links natively against
// legacy_stdio_definitions.lib, whose members are machine code. The helper
// archive clang_rt.builtins of the *-windows-msvc-msil triples holds this
// file instead (ImplementationPlan.md 5.15.2, 5.14.3 A6): it defines the
// standard printf and scanf functions, their wide counterparts, and
// _snprintf, _vsnprintf, _scprintf, _vscprintf, _snwprintf and _vsnwprintf
// over the same exports, with __acrt_iob_func(0) and (1) for stdin and
// stdout. Each passes the options word that the UCRT 10.0.26100 header
// passes for the same function: *__local_stdio_printf_options() or
// *__local_stdio_scanf_options() (msil_ucrt_stdio_options.c, or the COMDAT
// copy of a translation unit that includes <stdio.h>), with
// _CRT_INTERNAL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION (1) for sprintf,
// vsprintf, _snprintf, _vsnprintf, _snwprintf and _vsnwprintf, and
// _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR (2) for snprintf,
// vsnprintf, _scprintf and _vscprintf. The string forms return -1 for any
// negative result, as the header's do; swprintf and vswprintf are ISO C's,
// with a count. The file is written from those documented entry points and
// constants, never compiled from the Windows SDK's headers (R14 (b)), and,
// as every toolchain-authored source of the archive, includes no system
// header (5.14.3 A8).
//
// Every definition is weak, so a member extracted for one name never
// collides with the inline COMDAT copies of the same function in the objects
// of translation units that include <stdio.h>. build-builtins.py compiles
// the file once per function (#if MSIL_MEMBER(<name>)), so each member
// defines one function and references only the UCRT exports and the options
// function that it calls.
//
//===----------------------------------------------------------------------===//

#ifndef MSIL_MEMBER
#define MSIL_MEMBER(name) 1
#endif

#define MSIL_WEAK __attribute__((weak))

typedef __SIZE_TYPE__ __msil_size_t;
typedef __WCHAR_TYPE__ __msil_wchar_t;
typedef __builtin_va_list __msil_va_list;
typedef struct __msil_file __msil_FILE;
typedef struct __msil_locale *__msil_locale_t;

// The option bits of <corecrt_stdio_config.h> that the string forms add.
#define MSIL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION 1ULL
#define MSIL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR 2ULL

// The options words (msil_ucrt_stdio_options.c).
unsigned long long *__local_stdio_printf_options(void);
unsigned long long *__local_stdio_scanf_options(void);

// The UCRT's exports (api-ms-win-crt-stdio-l1-1-0.dll).
__msil_FILE *__acrt_iob_func(unsigned);
int __stdio_common_vfprintf(unsigned long long, __msil_FILE *, const char *,
                            __msil_locale_t, __msil_va_list);
int __stdio_common_vsprintf(unsigned long long, char *, __msil_size_t,
                            const char *, __msil_locale_t, __msil_va_list);
int __stdio_common_vfscanf(unsigned long long, __msil_FILE *, const char *,
                           __msil_locale_t, __msil_va_list);
int __stdio_common_vsscanf(unsigned long long, const char *, __msil_size_t,
                           const char *, __msil_locale_t, __msil_va_list);
int __stdio_common_vfwprintf(unsigned long long, __msil_FILE *,
                             const __msil_wchar_t *, __msil_locale_t,
                             __msil_va_list);
int __stdio_common_vswprintf(unsigned long long, __msil_wchar_t *,
                             __msil_size_t, const __msil_wchar_t *,
                             __msil_locale_t, __msil_va_list);
int __stdio_common_vfwscanf(unsigned long long, __msil_FILE *,
                            const __msil_wchar_t *, __msil_locale_t,
                            __msil_va_list);
int __stdio_common_vswscanf(unsigned long long, const __msil_wchar_t *,
                            __msil_size_t, const __msil_wchar_t *,
                            __msil_locale_t, __msil_va_list);

#define MSIL_STDIN (__acrt_iob_func(0))
#define MSIL_STDOUT (__acrt_iob_func(1))
#define MSIL_PRINTF_OPTIONS (*__local_stdio_printf_options())
#define MSIL_SCANF_OPTIONS (*__local_stdio_scanf_options())
#define MSIL_NO_COUNT (~(__msil_size_t)0)

//===----------------------------------------------------------------------===//
// Narrow formatted output
//===----------------------------------------------------------------------===//

#if MSIL_MEMBER(printf)
MSIL_WEAK int printf(const char *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r =
      __stdio_common_vfprintf(MSIL_PRINTF_OPTIONS, MSIL_STDOUT, format, 0, ap);
  __builtin_va_end(ap);
  return r;
}
#endif

#if MSIL_MEMBER(fprintf)
MSIL_WEAK int fprintf(__msil_FILE *stream, const char *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vfprintf(MSIL_PRINTF_OPTIONS, stream, format, 0, ap);
  __builtin_va_end(ap);
  return r;
}
#endif

#if MSIL_MEMBER(sprintf)
MSIL_WEAK int sprintf(char *buffer, const char *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vsprintf(
      MSIL_PRINTF_OPTIONS | MSIL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION,
      buffer, MSIL_NO_COUNT, format, 0, ap);
  __builtin_va_end(ap);
  return r < 0 ? -1 : r;
}
#endif

#if MSIL_MEMBER(snprintf)
MSIL_WEAK int snprintf(char *buffer, __msil_size_t count, const char *format,
                       ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vsprintf(MSIL_PRINTF_OPTIONS |
                                      MSIL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR,
                                  buffer, count, format, 0, ap);
  __builtin_va_end(ap);
  return r < 0 ? -1 : r;
}
#endif

#if MSIL_MEMBER(_snprintf)
MSIL_WEAK int _snprintf(char *buffer, __msil_size_t count, const char *format,
                        ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vsprintf(
      MSIL_PRINTF_OPTIONS | MSIL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION,
      buffer, count, format, 0, ap);
  __builtin_va_end(ap);
  return r < 0 ? -1 : r;
}
#endif

#if MSIL_MEMBER(_scprintf)
MSIL_WEAK int _scprintf(const char *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vsprintf(MSIL_PRINTF_OPTIONS |
                                      MSIL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR,
                                  0, 0, format, 0, ap);
  __builtin_va_end(ap);
  return r < 0 ? -1 : r;
}
#endif

#if MSIL_MEMBER(vprintf)
MSIL_WEAK int vprintf(const char *format, __msil_va_list ap) {
  return __stdio_common_vfprintf(MSIL_PRINTF_OPTIONS, MSIL_STDOUT, format, 0,
                                 ap);
}
#endif

#if MSIL_MEMBER(vfprintf)
MSIL_WEAK int vfprintf(__msil_FILE *stream, const char *format,
                       __msil_va_list ap) {
  return __stdio_common_vfprintf(MSIL_PRINTF_OPTIONS, stream, format, 0, ap);
}
#endif

#if MSIL_MEMBER(vsprintf)
MSIL_WEAK int vsprintf(char *buffer, const char *format, __msil_va_list ap) {
  int r = __stdio_common_vsprintf(
      MSIL_PRINTF_OPTIONS | MSIL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION,
      buffer, MSIL_NO_COUNT, format, 0, ap);
  return r < 0 ? -1 : r;
}
#endif

#if MSIL_MEMBER(vsnprintf)
MSIL_WEAK int vsnprintf(char *buffer, __msil_size_t count, const char *format,
                        __msil_va_list ap) {
  int r = __stdio_common_vsprintf(MSIL_PRINTF_OPTIONS |
                                      MSIL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR,
                                  buffer, count, format, 0, ap);
  return r < 0 ? -1 : r;
}
#endif

#if MSIL_MEMBER(_vsnprintf)
MSIL_WEAK int _vsnprintf(char *buffer, __msil_size_t count, const char *format,
                         __msil_va_list ap) {
  int r = __stdio_common_vsprintf(
      MSIL_PRINTF_OPTIONS | MSIL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION,
      buffer, count, format, 0, ap);
  return r < 0 ? -1 : r;
}
#endif

#if MSIL_MEMBER(_vscprintf)
MSIL_WEAK int _vscprintf(const char *format, __msil_va_list ap) {
  int r = __stdio_common_vsprintf(MSIL_PRINTF_OPTIONS |
                                      MSIL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR,
                                  0, 0, format, 0, ap);
  return r < 0 ? -1 : r;
}
#endif

//===----------------------------------------------------------------------===//
// Narrow formatted input
//===----------------------------------------------------------------------===//

#if MSIL_MEMBER(scanf)
MSIL_WEAK int scanf(const char *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vfscanf(MSIL_SCANF_OPTIONS, MSIL_STDIN, format, 0, ap);
  __builtin_va_end(ap);
  return r;
}
#endif

#if MSIL_MEMBER(fscanf)
MSIL_WEAK int fscanf(__msil_FILE *stream, const char *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vfscanf(MSIL_SCANF_OPTIONS, stream, format, 0, ap);
  __builtin_va_end(ap);
  return r;
}
#endif

#if MSIL_MEMBER(sscanf)
MSIL_WEAK int sscanf(const char *buffer, const char *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vsscanf(MSIL_SCANF_OPTIONS, buffer, MSIL_NO_COUNT,
                                 format, 0, ap);
  __builtin_va_end(ap);
  return r;
}
#endif

#if MSIL_MEMBER(vscanf)
MSIL_WEAK int vscanf(const char *format, __msil_va_list ap) {
  return __stdio_common_vfscanf(MSIL_SCANF_OPTIONS, MSIL_STDIN, format, 0, ap);
}
#endif

#if MSIL_MEMBER(vfscanf)
MSIL_WEAK int vfscanf(__msil_FILE *stream, const char *format,
                      __msil_va_list ap) {
  return __stdio_common_vfscanf(MSIL_SCANF_OPTIONS, stream, format, 0, ap);
}
#endif

#if MSIL_MEMBER(vsscanf)
MSIL_WEAK int vsscanf(const char *buffer, const char *format,
                      __msil_va_list ap) {
  return __stdio_common_vsscanf(MSIL_SCANF_OPTIONS, buffer, MSIL_NO_COUNT,
                                format, 0, ap);
}
#endif

//===----------------------------------------------------------------------===//
// Wide formatted output
//===----------------------------------------------------------------------===//

#if MSIL_MEMBER(wprintf)
MSIL_WEAK int wprintf(const __msil_wchar_t *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r =
      __stdio_common_vfwprintf(MSIL_PRINTF_OPTIONS, MSIL_STDOUT, format, 0, ap);
  __builtin_va_end(ap);
  return r;
}
#endif

#if MSIL_MEMBER(fwprintf)
MSIL_WEAK int fwprintf(__msil_FILE *stream, const __msil_wchar_t *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vfwprintf(MSIL_PRINTF_OPTIONS, stream, format, 0, ap);
  __builtin_va_end(ap);
  return r;
}
#endif

#if MSIL_MEMBER(swprintf)
MSIL_WEAK int swprintf(__msil_wchar_t *buffer, __msil_size_t count,
                       const __msil_wchar_t *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vswprintf(MSIL_PRINTF_OPTIONS, buffer, count, format,
                                   0, ap);
  __builtin_va_end(ap);
  return r < 0 ? -1 : r;
}
#endif

#if MSIL_MEMBER(_snwprintf)
MSIL_WEAK int _snwprintf(__msil_wchar_t *buffer, __msil_size_t count,
                         const __msil_wchar_t *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vswprintf(
      MSIL_PRINTF_OPTIONS | MSIL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION,
      buffer, count, format, 0, ap);
  __builtin_va_end(ap);
  return r < 0 ? -1 : r;
}
#endif

#if MSIL_MEMBER(vwprintf)
MSIL_WEAK int vwprintf(const __msil_wchar_t *format, __msil_va_list ap) {
  return __stdio_common_vfwprintf(MSIL_PRINTF_OPTIONS, MSIL_STDOUT, format, 0,
                                  ap);
}
#endif

#if MSIL_MEMBER(vfwprintf)
MSIL_WEAK int vfwprintf(__msil_FILE *stream, const __msil_wchar_t *format,
                        __msil_va_list ap) {
  return __stdio_common_vfwprintf(MSIL_PRINTF_OPTIONS, stream, format, 0, ap);
}
#endif

#if MSIL_MEMBER(vswprintf)
MSIL_WEAK int vswprintf(__msil_wchar_t *buffer, __msil_size_t count,
                        const __msil_wchar_t *format, __msil_va_list ap) {
  int r = __stdio_common_vswprintf(MSIL_PRINTF_OPTIONS, buffer, count, format,
                                   0, ap);
  return r < 0 ? -1 : r;
}
#endif

#if MSIL_MEMBER(_vsnwprintf)
MSIL_WEAK int _vsnwprintf(__msil_wchar_t *buffer, __msil_size_t count,
                          const __msil_wchar_t *format, __msil_va_list ap) {
  int r = __stdio_common_vswprintf(
      MSIL_PRINTF_OPTIONS | MSIL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION,
      buffer, count, format, 0, ap);
  return r < 0 ? -1 : r;
}
#endif

//===----------------------------------------------------------------------===//
// Wide formatted input
//===----------------------------------------------------------------------===//

#if MSIL_MEMBER(wscanf)
MSIL_WEAK int wscanf(const __msil_wchar_t *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r =
      __stdio_common_vfwscanf(MSIL_SCANF_OPTIONS, MSIL_STDIN, format, 0, ap);
  __builtin_va_end(ap);
  return r;
}
#endif

#if MSIL_MEMBER(fwscanf)
MSIL_WEAK int fwscanf(__msil_FILE *stream, const __msil_wchar_t *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vfwscanf(MSIL_SCANF_OPTIONS, stream, format, 0, ap);
  __builtin_va_end(ap);
  return r;
}
#endif

#if MSIL_MEMBER(swscanf)
MSIL_WEAK int swscanf(const __msil_wchar_t *buffer,
                      const __msil_wchar_t *format, ...) {
  __msil_va_list ap;
  __builtin_va_start(ap, format);
  int r = __stdio_common_vswscanf(MSIL_SCANF_OPTIONS, buffer, MSIL_NO_COUNT,
                                  format, 0, ap);
  __builtin_va_end(ap);
  return r;
}
#endif

#if MSIL_MEMBER(vwscanf)
MSIL_WEAK int vwscanf(const __msil_wchar_t *format, __msil_va_list ap) {
  return __stdio_common_vfwscanf(MSIL_SCANF_OPTIONS, MSIL_STDIN, format, 0, ap);
}
#endif

#if MSIL_MEMBER(vfwscanf)
MSIL_WEAK int vfwscanf(__msil_FILE *stream, const __msil_wchar_t *format,
                       __msil_va_list ap) {
  return __stdio_common_vfwscanf(MSIL_SCANF_OPTIONS, stream, format, 0, ap);
}
#endif

#if MSIL_MEMBER(vswscanf)
MSIL_WEAK int vswscanf(const __msil_wchar_t *buffer,
                       const __msil_wchar_t *format, __msil_va_list ap) {
  return __stdio_common_vswscanf(MSIL_SCANF_OPTIONS, buffer, MSIL_NO_COUNT,
                                 format, 0, ap);
}
#endif
