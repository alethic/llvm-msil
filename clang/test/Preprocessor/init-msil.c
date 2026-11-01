// Predefined macros of '<native triple>-msil' targets (Design.md section 3):
// clang uses the native TargetInfo, so the predefines are exactly the native
// triple's, except that
//  * __MSIL__ is defined,
//  * long double is IEEE double (the __LDBL_* macros and
//    __SIZEOF_LONG_DOUBLE__ change where the native long double is wider), and
//  * __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16 is never defined: every MSIL triple
//    caps MaxAtomicInlineWidth at 64 bits, its 16-byte atomics taking the lock
//    path, and removes the macro, which the native AArch64 TargetInfo always
//    defines and the x86-64 one with cx16 (ImplementationPlan.md M25.b,
//    decision brief R12; the aarch64 diffs below check it, and
//    clang/test/CodeGen/MSIL/atomic-lock-free.c checks -mcx16,
//    -march=x86-64-v2 and the lock-free queries).
// Since M25, __ELF__ follows the native object format as on the native
// triples: the Linux '-msil' triples define it and the Windows ones do not
// (ImplementationPlan.md section 10 item 9 as M25 settles it; until M25 no
// '-msil' triple defined it).

// Native-by-construction guard: one predefine diff per Phase-1 triple
// (decision D6). Only the lines that are expected to differ are removed, and
// only on the side where they appear, so any other difference (including a
// Linux -msil side without __ELF__, or an aarch64 -msil side with
// __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16) fails the diff.

// x86_64 Linux, glibc and musl: native long double is x87; both sides define
// __ELF__.
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " > %t.x64-gnu.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " -e "#define __MSIL__ " > %t.x64-gnu.msil
// RUN: diff %t.x64-gnu.native %t.x64-gnu.msil
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-musl < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " > %t.x64-musl.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-musl-msil < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " -e "#define __MSIL__ " > %t.x64-musl.msil
// RUN: diff %t.x64-musl.native %t.x64-musl.msil

// aarch64 Linux, glibc and musl: native long double is IEEE quad; both sides
// define __ELF__. The native __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16 is removed on
// the native side only (here and in the aarch64 MSVC and MinGW diffs below),
// so a definition on the -msil side fails the diff.
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-gnu < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " \
// RUN:     -e "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16 " > %t.a64-gnu.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " -e "#define __MSIL__ " > %t.a64-gnu.msil
// RUN: diff %t.a64-gnu.native %t.a64-gnu.msil
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-musl < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "#define __SIZEOF_LONG_DOUBLE__ " \
// RUN:     -e "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16 " > %t.a64-musl.native
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
// natively, so only __MSIL__ may differ (the long double macros are compared
// too), and on aarch64 the native __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16, removed
// on the native side only.
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-pc-windows-msvc -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 < /dev/null > %t.x64-msvc.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-pc-windows-msvc-msil -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 < /dev/null | grep -v -e "#define __MSIL__ " > %t.x64-msvc.msil
// RUN: diff %t.x64-msvc.native %t.x64-msvc.msil
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-pc-windows-msvc -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 < /dev/null \
// RUN:   | grep -v -e "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16 " > %t.a64-msvc.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-pc-windows-msvc-msil -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 < /dev/null | grep -v -e "#define __MSIL__ " > %t.a64-msvc.msil
// RUN: diff %t.a64-msvc.native %t.a64-msvc.msil
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-w64-windows-gnu < /dev/null \
// RUN:   | grep -v -e "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16 " > %t.a64-mingw.native
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

// Section 10 item 9 (since M25): clang defines __ELF__ for an ELF object
// format, and a '-msil' triple, whose objects are CIL, follows its native
// triple's format, so the Linux '-msil' triples define it and the Windows
// '-msil' triples (MSVC and MinGW) do not, exactly as their native triples.
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefix=ELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-musl-msil < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefix=ELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefix=ELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-musl-msil < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefix=ELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-pc-windows-msvc-msil < /dev/null \
// RUN:   | FileCheck --check-prefix=NOELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-pc-windows-msvc-msil < /dev/null \
// RUN:   | FileCheck --check-prefix=NOELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-w64-windows-gnu-msil < /dev/null \
// RUN:   | FileCheck --check-prefix=NOELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-w64-windows-gnu-msil < /dev/null \
// RUN:   | FileCheck --check-prefix=NOELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefix=ELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-gnu < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefix=ELF %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-pc-windows-msvc < /dev/null \
// RUN:   | FileCheck --check-prefix=NOELF %s
// NOELF-NOT: __ELF__
// ELF: #define __ELF__ 1
