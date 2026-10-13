// Phase-1 triple scope in clang (decision D6; ImplementationPlan.md M1.c):
// x86_64 and aarch64 on Linux (glibc, musl) and Windows (MSVC, MinGW) are
// accepted and keep their '-msil' suffix. Every other '-msil' triple (Darwin,
// i686, Android, OHOS, x32, Cygwin, Arm64EC, Linux or Windows without an
// environment, other architectures and operating systems) is an unknown target
// triple until a later phase. The accepted triples get no exception-model
// module flag and no CFString builtin, and the Windows ones keep the native
// COFF alignment limit.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=OK -DTRIPLE=x86_64-unknown-linux-gnu-msil %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=OK -DTRIPLE=x86_64-unknown-linux-musl-msil %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=OK -DTRIPLE=aarch64-unknown-linux-gnu-msil %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=OK -DTRIPLE=aarch64-unknown-linux-musl-msil %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=OK -DTRIPLE=x86_64-pc-windows-msvc-msil %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=OK -DTRIPLE=x86_64-w64-windows-gnu-msil %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=OK -DTRIPLE=aarch64-pc-windows-msvc-msil %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=OK -DTRIPLE=aarch64-w64-windows-gnu-msil %s
// OK: target triple = "[[TRIPLE]]"
// OK: define {{.*}}i32 @f()

// aarch64 Linux: the ELF-only pointer-authentication ABI module flags are not
// emitted for MSIL (and the ELF assertion that guards them is not reached),
// while the native triple emits them by default.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=A64-MSIL %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=A64-MSIL %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=A64-NATIVE %s
// A64-MSIL-NOT: pauthabi
// A64-NATIVE: !{i32 1, !"aarch64-elf-pauthabi-platform", i32 268435458}

// Rejected in Phase 1.
// RUN: not %clang_cc1 -triple aarch64-apple-darwin-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=aarch64-apple-darwin-msil %s
// RUN: not %clang_cc1 -triple x86_64-apple-macosx-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=x86_64-apple-macosx-msil %s
// RUN: not %clang_cc1 -triple i686-pc-windows-msvc-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=i686-pc-windows-msvc-msil %s
// RUN: not %clang_cc1 -triple i686-unknown-linux-gnu-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=i686-unknown-linux-gnu-msil %s
// RUN: not %clang_cc1 -triple powerpc64le-unknown-linux-gnu-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=powerpc64le-unknown-linux-gnu-msil %s
// RUN: not %clang_cc1 -triple riscv64-unknown-linux-gnu-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=riscv64-unknown-linux-gnu-msil %s
// RUN: not %clang_cc1 -triple wasm32-unknown-unknown-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=wasm32-unknown-unknown-msil %s
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-android-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=x86_64-unknown-linux-android-msil %s
// RUN: not %clang_cc1 -triple aarch64-unknown-linux-android-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=aarch64-unknown-linux-android-msil %s
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-ohos-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=x86_64-unknown-linux-ohos-msil %s
// RUN: not %clang_cc1 -triple aarch64-unknown-linux-ohos-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=aarch64-unknown-linux-ohos-msil %s
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnux32-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=x86_64-unknown-linux-gnux32-msil %s
// RUN: not %clang_cc1 -triple x86_64-pc-windows-cygnus-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=x86_64-pc-windows-cygnus-msil %s
// RUN: not %clang_cc1 -triple x86_64-unknown-freebsd-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=x86_64-unknown-freebsd-msil %s
// RUN: not %clang_cc1 -triple arm64ec-pc-windows-msvc-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=arm64ec-pc-windows-msvc-msil %s
// RUN: not %clang_cc1 -triple arm64e-apple-darwin-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=arm64e-apple-darwin-msil %s
// BAD: error: unknown target triple '[[TRIPLE]]'

// Linux and Windows triples must name the environment: gnu or musl on Linux,
// msvc or gnu on Windows. 'x86_64-pc-windows-msil' is what the driver makes of
// 'x86_64-pc-win32-msil' (section 12 row 'D3 amended').
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=x86_64-unknown-linux-msil %s
// RUN: not %clang_cc1 -triple aarch64-unknown-linux-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=aarch64-unknown-linux-msil %s
// RUN: not %clang_cc1 -triple x86_64-pc-windows-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=x86_64-pc-windows-msil %s
// RUN: not %clang_cc1 -triple aarch64-pc-windows-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD -DTRIPLE=aarch64-pc-windows-msil %s

// MSIL modules carry no "exception-model" module flag, even with the
// -exception-model=seh that the MinGW driver passes (section 12 row 'M1.d
// amended'); the native MinGW triple keeps the flag.
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -exception-model=seh -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=NO-EH-MODEL --implicit-check-not=exception-model %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -exception-model=seh -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=NO-EH-MODEL --implicit-check-not=exception-model %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu -exception-model=seh -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=EH-MODEL %s
// NO-EH-MODEL: define {{.*}}i32 @f()
// EH-MODEL: !{i32 1, !"exception-model", !"wineh"}

// IR input (-x ir) gets no flag either (section 12 row 'fix-loop ownership:
// clang/lib/CodeGen/CodeGenAction.cpp -> fix-5'). The first step of each pair
// runs without -exception-model and so writes IR with no flag (checked for the
// native triple); the native pair shows that compiling that IR with
// -exception-model=seh adds the flag.
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -emit-llvm -o %t.x64-mingw-msil.ll %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -exception-model=seh -x ir -emit-llvm -o - %t.x64-mingw-msil.ll \
// RUN:   | FileCheck --check-prefix=NO-EH-MODEL --implicit-check-not=exception-model %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -emit-llvm -o %t.a64-mingw-msil.ll %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -exception-model=seh -x ir -emit-llvm -o - %t.a64-mingw-msil.ll \
// RUN:   | FileCheck --check-prefix=NO-EH-MODEL --implicit-check-not=exception-model %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu -emit-llvm -o %t.x64-mingw.ll %s
// RUN: FileCheck --check-prefix=NO-EH-MODEL --implicit-check-not=exception-model --input-file=%t.x64-mingw.ll %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu -exception-model=seh -x ir -emit-llvm -o - %t.x64-mingw.ll \
// RUN:   | FileCheck --check-prefix=EH-MODEL %s

// __builtin___CFStringMakeConstantString needs a CFString section, which the
// msil object format does not have: Sema rejects it, while the native triple
// accepts it.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fsyntax-only -DCFSTRING %s -verify=cfstring
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsyntax-only -DCFSTRING %s -verify=cfnative
// cfnative-no-diagnostics

// Windows MSIL triples keep the native COFF alignment limit of 8192 bytes for
// aligned, _Alignas and __declspec(align), as the native Windows triple does
// (section 12 row 'fix-loop ownership: clang/lib/Sema/SemaDeclAttr.cpp ->
// fix-4'); Linux MSIL triples keep the native ELF limit.
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fsyntax-only -fdeclspec -DALIGN_CAP %s -verify=coff
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fsyntax-only -fdeclspec -DALIGN_CAP %s -verify=coff
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -fsyntax-only -fdeclspec -DALIGN_CAP %s -verify=coff
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -fsyntax-only -fdeclspec -DALIGN_CAP %s -verify=coff
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fsyntax-only -fdeclspec -DALIGN_CAP %s -verify=coff
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fsyntax-only -fdeclspec -DALIGN_CAP %s -verify=elf
// elf-no-diagnostics

int x;
int f(void) { return x; }

#ifdef CFSTRING
const void *cfstring(void) {
  return __builtin___CFStringMakeConstantString("x"); // cfstring-error {{builtin is not supported on this target}}
}
#endif

#ifdef ALIGN_CAP
int g __attribute__((aligned(16384))); // coff-error {{requested alignment must be 8192 bytes or smaller}}
_Alignas(16384) int h; // coff-error {{requested alignment must be 8192 bytes or smaller}}
__declspec(align(16384)) int k; // coff-error {{requested alignment must be 8192 bytes or smaller}}
int ok __attribute__((aligned(8192)));
#endif
