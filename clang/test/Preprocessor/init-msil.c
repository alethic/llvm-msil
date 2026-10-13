// Predefined macros of '<native triple>-msil' targets (Design.md section 3):
// clang uses the native TargetInfo, so the predefines are exactly the native
// triple's, except that
//  * __MSIL__ is defined,
//  * long double is IEEE double (the __LDBL_* macros and
//    __SIZEOF_LONG_DOUBLE__ change where the native long double is wider), and
//  * __ELF__ is not defined, because the object format is not ELF
//    (ImplementationPlan.md section 10, decision 9).

// Native-by-construction guard: one predefine diff per Phase-1 triple
// (decision D6). Only the lines that are expected to differ are removed, and
// only on the side where they appear, so any other difference (including an
// __ELF__ on the -msil side) fails the diff.

// x86_64 Linux, glibc and musl: native long double is x87, native defines __ELF__.
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " -e "#define __ELF__ " > %t.x64-gnu.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " -e "#define __MSIL__ " > %t.x64-gnu.msil
// RUN: diff %t.x64-gnu.native %t.x64-gnu.msil
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-musl < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " -e "#define __ELF__ " > %t.x64-musl.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-musl-msil < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " -e "#define __MSIL__ " > %t.x64-musl.msil
// RUN: diff %t.x64-musl.native %t.x64-musl.msil

// aarch64 Linux, glibc and musl: native long double is IEEE quad.
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-gnu < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " -e "#define __ELF__ " > %t.a64-gnu.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " -e "#define __MSIL__ " > %t.a64-gnu.msil
// RUN: diff %t.a64-gnu.native %t.a64-gnu.msil
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-musl < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " -e "#define __ELF__ " > %t.a64-musl.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-musl-msil < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " -e "#define __MSIL__ " > %t.a64-musl.msil
// RUN: diff %t.a64-musl.native %t.a64-musl.msil

// x86_64 MinGW: native long double is x87; no __ELF__ on either side.
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-w64-windows-gnu < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " > %t.x64-mingw.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-w64-windows-gnu-msil < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " -e "#define __MSIL__ " > %t.x64-mingw.msil
// RUN: diff %t.x64-mingw.native %t.x64-mingw.msil

// MSVC (x86_64 and aarch64) and aarch64 MinGW: long double is already double
// natively, so only __MSIL__ may differ (the long double macros are compared too).
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-pc-windows-msvc -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 < /dev/null > %t.x64-msvc.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-pc-windows-msvc-msil -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 < /dev/null | grep -v -e "#define __MSIL__ " > %t.x64-msvc.msil
// RUN: diff %t.x64-msvc.native %t.x64-msvc.msil
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-pc-windows-msvc -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 < /dev/null > %t.a64-msvc.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-pc-windows-msvc-msil -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 < /dev/null | grep -v -e "#define __MSIL__ " > %t.a64-msvc.msil
// RUN: diff %t.a64-msvc.native %t.a64-msvc.msil
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-w64-windows-gnu < /dev/null > %t.a64-mingw.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-w64-windows-gnu-msil < /dev/null \
// RUN:   | grep -v -e "#define __MSIL__ " > %t.a64-mingw.msil
// RUN: diff %t.a64-mingw.native %t.a64-mingw.msil

// Direct checks of the macros the diffs filter out.
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefixes=MSIL,X64,LNX %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefixes=MSIL,A64,LNX %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-w64-windows-gnu-msil < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefixes=MSIL,X64,MINGW %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-pc-windows-msvc-msil -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefixes=MSIL,X64,MSVC %s
// MSIL-DAG: #define __MSIL__ 1
// MSIL-DAG: #define __SIZEOF_LONG_DOUBLE__ 8
// MSIL-DAG: #define __LDBL_MANT_DIG__ 53
// MSIL-DAG: #define __LDBL_DIG__ 15
// MSIL-DAG: #define __LDBL_MAX_EXP__ 1024
// MSIL-DAG: #define __LDBL_MAX__ 1.7976931348623157e+308L
// MSIL-DAG: #define __LDBL_EPSILON__ 2.2204460492503131e-16L
// MSIL-DAG: #define __SIZEOF_DOUBLE__ 8
// MSIL-DAG: #define __SIZEOF_POINTER__ 8
// X64-DAG: #define __x86_64__ 1
// A64-DAG: #define __aarch64__ 1
// LNX-DAG: #define __linux__ 1
// LNX-DAG: #define __LP64__ 1
// MINGW-DAG: #define __MINGW64__ 1
// MINGW-DAG: #define _WIN64 1
// MSVC-DAG: #define _WIN64 1
// MSVC-DAG: #define _M_X64 100
// MSVC-DAG: #define _MSC_VER 1940

// Native triples do not define __MSIL__.
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefix=NATIVE %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-pc-windows-msvc -fms-extensions < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefix=NATIVE %s
// NATIVE-NOT: #define __MSIL__ {{.*}}

// Section 10, decision 9 (Phase-1 default): clang defines __ELF__ only for the
// ELF object format, so the Linux '-msil' triples do not define it, while the
// native Linux triples do. No '-msil' triple defines it.
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | FileCheck --check-prefix=NOELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-musl-msil < /dev/null \
// RUN:   | FileCheck --check-prefix=NOELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | FileCheck --check-prefix=NOELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-pc-windows-msvc-msil < /dev/null \
// RUN:   | FileCheck --check-prefix=NOELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefix=ELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-gnu < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefix=ELF %s
// NOELF-NOT: __ELF__
// ELF: #define __ELF__ 1
