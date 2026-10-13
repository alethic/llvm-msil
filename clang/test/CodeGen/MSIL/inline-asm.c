// Inline assembly on MSIL targets (Design.md section 3; ImplementationPlan.md
// M1.d and the section 12 row 'M1.d amended'): there is no machine code to put
// it in, so CodeGen rejects every GNU asm statement and every non-empty
// file-scope asm in code it emits. The one form accepted is an empty asm
// statement without outputs, inputs or labels (any clobbers, volatile or not),
// which includes the compiler barrier 'asm volatile("" ::: "memory")'. It
// becomes a single-thread fence followed by a call to llvm.sideeffect, so -O2
// keeps loops that spin or delay on an empty asm, as it does for the native
// triples. Parsing and Sema are unchanged, so functions that contain asm but are
// never emitted (system headers) compile cleanly. Builtins that native CodeGen
// expands to inline assembly are rejected as builtins this target does not
// support, or lowered without assembly, and Sema rejects global register
// variables. Module asm never reaches an MSIL module: not from file-scope asm,
// not from -fsanitize=kcfi, and not from IR or bitcode input.
//
// CodeGen stops emitting top-level declarations after its first error, so each
// error case is compiled on its own (GNU_ASM, FILE_SCOPE_ASM, USED_INLINE,
// MS_BUILTINS).

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - %s -verify=ok \
// RUN:   | FileCheck %s --implicit-check-not="asm sideeffect" --implicit-check-not="module asm" \
// RUN:     --implicit-check-not=bswap --implicit-check-not=@helper_with_asm
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o - %s -verify=ok \
// RUN:   | FileCheck %s --implicit-check-not="asm sideeffect" --implicit-check-not="module asm" \
// RUN:     --implicit-check-not=bswap --implicit-check-not=@helper_with_asm
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o - %s -verify=ok \
// RUN:   | FileCheck %s --implicit-check-not="asm sideeffect" --implicit-check-not="module asm" \
// RUN:     --implicit-check-not=bswap --implicit-check-not=@helper_with_asm
// ok-no-diagnostics

// The optimizer keeps loops around an empty asm statement (frontend only).
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=O2 --implicit-check-not="asm sideeffect" %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm-only -DGNU_ASM %s -verify=gnu
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm-only -DFILE_SCOPE_ASM %s -verify=file
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm-only -DUSED_INLINE %s -verify=used

// Nothing is diagnosed before CodeGen.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fsyntax-only -DGNU_ASM -DFILE_SCOPE_ASM -DUSED_INLINE \
// RUN:   %s -verify=syntax
// syntax-no-diagnostics

// The exact diagnostic text names the kind of assembly and the triple.
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm-only -DGNU_ASM %s 2>&1 \
// RUN:   | FileCheck --check-prefix=GNU-DIAG --implicit-check-not="PLEASE submit" %s
// RUN: not %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm-only -DGNU_ASM %s 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN-DIAG --implicit-check-not="PLEASE submit" %s
// RUN: not %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm-only -DFILE_SCOPE_ASM %s 2>&1 \
// RUN:   | FileCheck --check-prefix=FILE-DIAG --implicit-check-not="PLEASE submit" %s
// GNU-DIAG-COUNT-6: error: inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'
// GNU-DIAG: 6 errors generated.
// WIN-DIAG: error: inline assembly is not supported for the MSIL target 'x86_64-pc-windows-msvc-msil'
// FILE-DIAG: error: file-scope assembly is not supported for the MSIL target 'aarch64-unknown-linux-gnu-msil'

// IR or bitcode input that carries module asm, here stock-clang bitcode for the
// native triple (Design.md section 3), gets the file-scope assembly error when
// it is compiled for MSIL (decision D26), without a crash: the module asm is
// dropped before the LTO bitcode writers, which would need an MC asm parser for
// the target, run (section 12 row 'fix-loop ownership:
// clang/lib/CodeGen/CodeGenAction.cpp -> fix-6'). The native triple compiles the
// same bitcode.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm-bc -DFILE_SCOPE_ASM %s -o %t.native.bc
// RUN: not %clang --target=x86_64-unknown-linux-gnu-msil -flto -c -x ir %t.native.bc -o %t.lto.o 2>&1 \
// RUN:   | FileCheck --check-prefix=IR-MODASM --implicit-check-not="PLEASE submit" --implicit-check-not=Assertion %s
// RUN: not %clang --target=x86_64-unknown-linux-gnu-msil -flto=thin -c -x ir %t.native.bc -o %t.thinlto.o 2>&1 \
// RUN:   | FileCheck --check-prefix=IR-MODASM --implicit-check-not="PLEASE submit" --implicit-check-not=Assertion %s
// RUN: %if x86-registered-target %{ %clang --target=x86_64-unknown-linux-gnu -flto -c -x ir %t.native.bc -o %t.native-lto.o %}
// IR-MODASM: error: file-scope assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'

// -fsanitize=kcfi, which the driver does not offer for MSIL triples
// (Driver/msil-toolchain.c), adds no module asm when cc1 gets it anyway: the
// native triple writes the KCFI type id of an address-taken external function
// as module asm.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fsanitize=kcfi -emit-llvm -o - -DKCFI %s -verify=ok \
// RUN:   | FileCheck --check-prefix=KCFI --implicit-check-not="module asm" %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=kcfi -emit-llvm -o - -DKCFI %s \
// RUN:   | FileCheck --check-prefix=KCFI-NATIVE %s

// MSVC builtins that native CodeGen expands to inline assembly are diagnosed
// as builtins this target does not support (the diagnostic Sema gives
// __builtin___CFStringMakeConstantString, CodeGen/MSIL/triples.c); the
// bit-test builtins use the generic lowering.
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 -ffreestanding -emit-llvm-only -DMS_BUILTINS %s -verify=msb
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 -ffreestanding -emit-llvm-only -DMS_BUILTINS %s -verify=msb
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 -ffreestanding -emit-llvm -o - -DMS_BITTEST %s \
// RUN:   | FileCheck --check-prefix=BITTEST --implicit-check-not="{{ asm }}" %s

// Global register variables name a machine register, so Sema rejects them.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fsyntax-only -DGLOBAL_REG %s -verify=regvar

// Native triples are unaffected.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - -DGNU_ASM -DFILE_SCOPE_ASM -DUSED_INLINE \
// RUN:   -DGLOBAL_REG %s | FileCheck --check-prefix=NATIVE %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 -ffreestanding -emit-llvm -o - -DMS_BUILTINS -DMS_BITTEST %s \
// RUN:   | FileCheck --check-prefix=NATIVE-MS %s
// NATIVE: module asm(target_features: "{{.*}}")
// NATIVE-NEXT: ".globl foo"
// NATIVE-LABEL: define {{.*}}void @barrier()
// NATIVE: call void asm sideeffect "", "~{memory}
// NATIVE-LABEL: define {{.*}}void @wait_flag()
// NATIVE: call void asm sideeffect "", "
// NATIVE-LABEL: define {{.*}}i32 @gnu_asm(
// NATIVE: call void asm sideeffect "nop"
// NATIVE: callbr void asm sideeffect "", "!i,~{memory}
// NATIVE-LABEL: define {{.*}}i64 @use_helper(
// NATIVE-LABEL: define internal {{.*}}i64 @helper_with_asm(
// NATIVE: asm "bswap $0"
// NATIVE-LABEL: define {{.*}}i64 @read_sp()
// NATIVE: call i64 @llvm.read_register.i64(
// NATIVE-MS-LABEL: define {{.*}}void @ms_builtins(
// NATIVE-MS: asm "xchgq %rbx, ${1:q}\0Acpuid
// NATIVE-MS: asm sideeffect "int $$0x2c"
// NATIVE-MS: asm sideeffect "int $$0x29"
// NATIVE-MS-LABEL: define {{.*}}i8 @bt(
// NATIVE-MS: asm sideeffect "btsl
// NATIVE-MS-LABEL: define {{.*}}i8 @ibt(
// NATIVE-MS: asm sideeffect "lock btsl

// An empty file-scope asm is accepted and adds nothing.
__asm__("");

// Asm labels are not inline assembly.
int renamed(void) __asm__("real_name");
int call_renamed(void) { return renamed(); }
// CHECK-LABEL: define {{.*}}i32 @call_renamed()
// CHECK: call i32 @real_name()

// Every accepted empty asm statement becomes a single-thread fence followed by
// llvm.sideeffect: the compiler barrier, with or without 'volatile' and with
// other clobbers next to "memory", and an empty asm without operands or
// clobbers.
void barrier(void) { __asm__ __volatile__("" ::: "memory"); }
// CHECK-LABEL: define {{.*}}void @barrier()
// CHECK: fence syncscope("singlethread") seq_cst
// CHECK-NEXT: call void @llvm.sideeffect()
// CHECK-NEXT: ret void

void barrier_nonvolatile(void) { __asm__("" ::: "memory"); }
// CHECK-LABEL: define {{.*}}void @barrier_nonvolatile()
// CHECK: fence syncscope("singlethread") seq_cst
// CHECK-NEXT: call void @llvm.sideeffect()
// CHECK-NEXT: ret void

void barrier_cc(void) { __asm__ volatile("" ::: "cc", "memory"); }
// CHECK-LABEL: define {{.*}}void @barrier_cc()
// CHECK: fence syncscope("singlethread") seq_cst
// CHECK-NEXT: call void @llvm.sideeffect()
// CHECK-NEXT: ret void

void empty_asm(void) { __asm__ volatile(""); }
// CHECK-LABEL: define {{.*}}void @empty_asm()
// CHECK: fence syncscope("singlethread") seq_cst
// CHECK-NEXT: call void @llvm.sideeffect()
// CHECK-NEXT: ret void

void empty_asm_cc(void) { __asm__ volatile("" ::: "cc"); }
// CHECK-LABEL: define {{.*}}void @empty_asm_cc()
// CHECK: fence syncscope("singlethread") seq_cst
// CHECK-NEXT: call void @llvm.sideeffect()
// CHECK-NEXT: ret void

// At -O2 the loops survive with the fence and llvm.sideeffect in their body:
// the load of 'flag' stays inside the loop, and the delay loops are not
// deleted (the fence alone would let -O2 delete them).
int flag;
void wait_flag(void) { while (!flag) __asm__ volatile(""); }
// O2-LABEL: define {{.*}}void @wait_flag()
// O2: [[LOOP:[A-Za-z0-9._]+]]: {{.*}}preds = {{.*}}%[[LOOP]]{{$|,}}
// O2-NEXT: fence syncscope("singlethread") seq_cst
// O2-NEXT: call void @llvm.sideeffect()
// O2-NEXT: load i32, ptr @flag
// O2: br i1 {{.*}}label %[[LOOP]]{{$|,}}

void delay(int n) {
  for (int i = 0; i < n; i++)
    __asm__ volatile("");
}
// O2-LABEL: define {{.*}}void @delay(
// O2: [[LOOP:[A-Za-z0-9._]+]]: {{.*}}preds = {{.*}}%[[LOOP]]{{$|,}}
// O2: fence syncscope("singlethread") seq_cst
// O2-NEXT: call void @llvm.sideeffect()
// O2: br i1 {{.*}}label %[[LOOP]]{{$|,}}

void delay_mem(int n) {
  for (int i = 0; i < n; i++)
    __asm__ volatile("" ::: "memory");
}
// O2-LABEL: define {{.*}}void @delay_mem(
// O2: [[LOOP:[A-Za-z0-9._]+]]: {{.*}}preds = {{.*}}%[[LOOP]]{{$|,}}
// O2: fence syncscope("singlethread") seq_cst
// O2-NEXT: call void @llvm.sideeffect()
// O2: br i1 {{.*}}label %[[LOOP]]{{$|,}}

// Diagnosed only if emitted: unused (the system-header pattern, intrin.h)
// except under USED_INLINE.
static inline long long helper_with_asm(long long x) {
  __asm__("bswap %0" : "+r"(x)); // used-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  return x;
}

// Every other GNU asm statement is an error, including empty ones with
// operands and 'asm goto' (which is not turned into a fence).
#ifdef GNU_ASM
int gnu_asm(int x) {
  __asm__ volatile("nop"); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  __asm__("" : "+r"(x)); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  __asm__ volatile("" : : "r"(x)); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  __asm__ volatile("" : : "r"(x) : "memory"); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  __asm__ volatile("nop" ::: "memory"); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  __asm__ volatile("" ::: "memory");
  __asm__ goto("" ::: "memory" : out); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  return x;
out:
  return 0;
}
#endif

// Emitted, so diagnosed.
#ifdef USED_INLINE
long long use_helper(long long x) { return helper_with_asm(x); }
#endif

#ifdef FILE_SCOPE_ASM
__asm__(".globl foo"); // file-error {{file-scope assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
#endif

#ifdef GLOBAL_REG
register unsigned long sp __asm__("rsp"); // regvar-error {{register 'rsp' unsuitable for global register variables on this target}}
unsigned long read_sp(void) { return sp; }
#endif

#if defined(MS_BUILTINS) || defined(MS_BITTEST)
#include <intrin.h>
#endif

// __fastfail comes last because it does not return.
#ifdef MS_BUILTINS
void ms_builtins(int *r) {
#ifdef __x86_64__
  __cpuid(r, 0); // msb-error {{builtin is not supported on this target}}
  __cpuidex(r, 7, 0); // msb-error {{builtin is not supported on this target}}
  __int2c(); // msb-error {{builtin is not supported on this target}}
#endif
  __fastfail(7); // msb-error {{builtin is not supported on this target}}
}
#endif

#ifdef KCFI
extern int kcfi_ext(int);
int (*kcfi_take(void))(int) { return kcfi_ext; }
// KCFI-LABEL: define {{.*}}ptr @kcfi_take()
// KCFI: ret ptr @kcfi_ext
// KCFI: declare {{.*}}i32 @kcfi_ext(i32
// KCFI-NATIVE: module asm
// KCFI-NATIVE-NEXT: ".weak __kcfi_typeid_kcfi_ext"
#endif

#ifdef MS_BITTEST
unsigned char bt(long *p, long b) { return _bittestandset(p, b); }
// BITTEST-LABEL: define {{.*}}i8 @bt(
// BITTEST: load i8, ptr
// BITTEST: or i8
// BITTEST: store i8
unsigned char ibt(long *p, long b) { return _interlockedbittestandset(p, b); }
// BITTEST-LABEL: define {{.*}}i8 @ibt(
// BITTEST: atomicrmw or ptr {{.*}} seq_cst
#endif
