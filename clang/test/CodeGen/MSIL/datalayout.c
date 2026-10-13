// The data layout of an MSIL triple is the native triple's, byte for byte
// (Design.md section 3, decision D3, recon registry-tm.md 3.2-3.3): the MSIL
// backend compiles C with the native ABI, and bitcode that stock clang produced
// for the native triple must be accepted. Without that rule the layout would
// follow the object format (no 'm:w' mangling and a different aarch64 layout on
// Windows). This test only runs the frontend; the backend is not needed.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=X64-LNX %s
// X64-LNX: target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
// X64-LNX-NEXT: target triple = "x86_64-unknown-linux-gnu-msil"

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=X64-WIN %s
// X64-WIN: target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
// X64-WIN-NEXT: target triple = "x86_64-pc-windows-msvc-msil"

// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=A64-LNX %s
// A64-LNX: target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"
// A64-LNX-NEXT: target triple = "aarch64-unknown-linux-gnu-msil"

// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=A64-WIN %s
// A64-WIN: target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-p:64:64-i32:32-i64:64-i128:128-n32:64-S128-Fn32"
// A64-WIN-NEXT: target triple = "aarch64-pc-windows-msvc-msil"

// Every Phase-1 triple (decision D6) has exactly its native triple's layout.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | grep "^target datalayout" > %t.x64-gnu.native
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - %s | grep "^target datalayout" > %t.x64-gnu.msil
// RUN: diff %t.x64-gnu.native %t.x64-gnu.msil
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl -emit-llvm -o - %s | grep "^target datalayout" > %t.x64-musl.native
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -emit-llvm -o - %s | grep "^target datalayout" > %t.x64-musl.msil
// RUN: diff %t.x64-musl.native %t.x64-musl.msil
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -emit-llvm -o - %s | grep "^target datalayout" > %t.x64-msvc.native
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o - %s | grep "^target datalayout" > %t.x64-msvc.msil
// RUN: diff %t.x64-msvc.native %t.x64-msvc.msil
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu -emit-llvm -o - %s | grep "^target datalayout" > %t.x64-mingw.native
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -emit-llvm -o - %s | grep "^target datalayout" > %t.x64-mingw.msil
// RUN: diff %t.x64-mingw.native %t.x64-mingw.msil
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm -o - %s | grep "^target datalayout" > %t.a64-gnu.native
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o - %s | grep "^target datalayout" > %t.a64-gnu.msil
// RUN: diff %t.a64-gnu.native %t.a64-gnu.msil
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl -emit-llvm -o - %s | grep "^target datalayout" > %t.a64-musl.native
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -emit-llvm -o - %s | grep "^target datalayout" > %t.a64-musl.msil
// RUN: diff %t.a64-musl.native %t.a64-musl.msil
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc -emit-llvm -o - %s | grep "^target datalayout" > %t.a64-msvc.native
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -emit-llvm -o - %s | grep "^target datalayout" > %t.a64-msvc.msil
// RUN: diff %t.a64-msvc.native %t.a64-msvc.msil
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu -emit-llvm -o - %s | grep "^target datalayout" > %t.a64-mingw.native
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -emit-llvm -o - %s | grep "^target datalayout" > %t.a64-mingw.msil
// RUN: diff %t.a64-mingw.native %t.a64-mingw.msil

// So do the alignment limits: Windows caps vector alignment and the aligned
// attribute at 8192 bytes, Linux does not (frontend only).
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fsyntax-only -DVECTOR_ALIGN=8192 -DALIGNED_ATTR %s -verify=cap
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -fsyntax-only -DVECTOR_ALIGN=8192 -DALIGNED_ATTR %s -verify=cap
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fsyntax-only -DALIGNED_ATTR %s -verify=cap
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -fsyntax-only -DALIGNED_ATTR %s -verify=cap
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fsyntax-only -DVECTOR_ALIGN=16384 -DALIGNED_ATTR %s -verify=nocap
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fsyntax-only -DVECTOR_ALIGN=8192 -DALIGNED_ATTR %s -verify=cap
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsyntax-only -DVECTOR_ALIGN=16384 -DALIGNED_ATTR %s -verify=nocap
// nocap-no-diagnostics

#ifdef VECTOR_ALIGN
typedef char V __attribute__((vector_size(16384)));
struct S {
  char c;
  V v;
};
#if VECTOR_ALIGN == 8192
_Static_assert(_Alignof(V) == 8192, "vector alignment capped at 8192");
_Static_assert(sizeof(struct S) == 24576, "");
_Static_assert(__builtin_offsetof(struct S, v) == 8192, "");
#else
_Static_assert(_Alignof(V) == 16384, "vector alignment not capped");
_Static_assert(sizeof(struct S) == 32768, "");
_Static_assert(__builtin_offsetof(struct S, v) == 16384, "");
#endif
#endif

#ifdef ALIGNED_ATTR
int g __attribute__((aligned(16384))); // cap-error {{requested alignment must be 8192 bytes or smaller}}
#endif

// The data model follows: LP64 on Linux, LLP64 on Windows.
#ifdef _WIN32
_Static_assert(sizeof(long) == 4, "LLP64");
#else
_Static_assert(sizeof(long) == 8, "LP64");
#endif
_Static_assert(sizeof(void *) == 8, "64-bit pointers");
_Static_assert(sizeof(long long) == 8, "");

int answer(void) { return 42; }
