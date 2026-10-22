// Inline assembly on MSIL targets (Design.md section 3; ImplementationPlan.md
// M1.d and the section 12 rows 'M1.d amended' and 'D36 amended' (M12)): there
// is no machine code to put it in, so CodeGen rejects every GNU asm statement
// with a non-empty template, every asm goto and every non-empty file-scope asm
// in code it emits. Two forms of empty asm statement are accepted. Without
// outputs, inputs or labels (any clobbers, volatile or not), which includes
// the compiler barrier 'asm volatile("" ::: "memory")', it becomes a
// single-thread fence followed by a call to llvm.sideeffect, so -O2 keeps
// loops that spin or delay on an empty asm, as it does for the native triples.
// With operands (M12, the black_box and DoNotOptimize idioms), when the
// template is empty after trimming white space, there are no labels and every
// output is read-write ("+r", "+m"), tied to an input ("=r" with "0"; "=rm"
// and "=g" with "0", which clang emits as indirect outputs; "=r,r" with
// "0,0", a tie written with alternatives) or memory-only ("=m"), CodeGen
// emits the native triple's IR unchanged: the OPERANDS runs check every such
// call against x86_64-unknown-linux-gnu and x86_64-pc-windows-msvc, and
// MSILPrepareIR in the backend lowers it to the fence, llvm.fake.use of the
// inputs and llvm.sideeffect (llvm/test/CodeGen/MSIL/asm-operands.ll,
// asm-barrier.c's BBOX runs; msil/test/M1/10-calls/black-box.c runs the
// tied outputs); the OPERANDS-BACKEND runs compile every such form with -S,
// so a form clang accepts and the backend rejects fails here (section 12
// rows 'D36 row of 2026-10-21 (...) clarified' and 'D36 rows of 2026-10-21
// clarified (review-r2)': clang's rule is the only acceptance rule). An
// output register that no input ties ("=r", "=rm") stays an error, since
// nothing would give it a value, and so do a condition-flag output ("+@ccz",
// "=@ccz" even when tied to "0", AArch64's "+@cceq"), which native code sets
// from the flags after the statement, and an indirect output tied to an input
// of another kind (a float "=rm" output tied to a double input), which native
// instruction selection cannot handle. Parsing and Sema are unchanged, so
// functions that contain asm but are never emitted (system headers) compile
// cleanly.
// Builtins that native CodeGen expands to inline assembly are rejected as
// builtins this target does not support, or lowered without assembly, and Sema
// rejects global register variables. Module asm never reaches an MSIL module:
// not from file-scope asm, not from -fsanitize=kcfi, and not from IR or
// bitcode input.
//
// CodeGen stops emitting top-level declarations after its first error, so each
// error case is compiled on its own (GNU_ASM, GNU_ASM_AARCH64, FILE_SCOPE_ASM,
// USED_INLINE, MS_BUILTINS).

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

// Empty asm statements with operands are emitted as for the native triples
// (frontend only), on both x86_64 MSIL triples, without a diagnostic and
// without a fence or llvm.sideeffect (MSILPrepareIR adds those later).
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - -DOPERANDS %s -verify=ok \
// RUN:   | FileCheck --check-prefix=OPERANDS --implicit-check-not=fence --implicit-check-not=llvm.sideeffect %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - -DOPERANDS %s -verify=ok \
// RUN:   | FileCheck --check-prefix=OPERANDS --implicit-check-not=fence --implicit-check-not=llvm.sideeffect %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o - -DOPERANDS %s -verify=ok \
// RUN:   | FileCheck --check-prefix=OPERANDS --implicit-check-not=fence --implicit-check-not=llvm.sideeffect %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -emit-llvm -o - -DOPERANDS %s -verify=ok \
// RUN:   | FileCheck --check-prefix=OPERANDS --implicit-check-not=fence --implicit-check-not=llvm.sideeffect %s

// OPERANDS-BACKEND: the backend compiles every OPERANDS form on both x86_64
// MSIL triples at -O0 and -O2 without a diagnostic (-verify=ok fails on any
// backend error, such as 'inline assembly is not supported for MSIL').
// RUN: %if msil-registered-target %{ %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -o %t.lnx.O0.il \
// RUN:   -DOPERANDS %s -verify=ok %}
// RUN: %if msil-registered-target %{ %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -S -o %t.lnx.O2.il \
// RUN:   -DOPERANDS %s -verify=ok %}
// RUN: %if msil-registered-target %{ %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -o %t.win.O0.il \
// RUN:   -DOPERANDS %s -verify=ok %}
// RUN: %if msil-registered-target %{ %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -S -o %t.win.O2.il \
// RUN:   -DOPERANDS %s -verify=ok %}

// The optimizer keeps loops around an empty asm statement (frontend only).
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=O2 --implicit-check-not="asm sideeffect" %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm-only -DGNU_ASM %s -verify=gnu
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm-only -DGNU_ASM_AARCH64 %s -verify=gnua64
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm-only -DFILE_SCOPE_ASM %s -verify=file
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm-only -DUSED_INLINE %s -verify=used

// Nothing is diagnosed before CodeGen.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fsyntax-only -DGNU_ASM -DFILE_SCOPE_ASM -DUSED_INLINE \
// RUN:   %s -verify=syntax
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -fsyntax-only -DGNU_ASM_AARCH64 %s -verify=syntax
// syntax-no-diagnostics

// The exact diagnostic text names the kind of assembly and the triple.
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm-only -DGNU_ASM %s 2>&1 \
// RUN:   | FileCheck --check-prefix=GNU-DIAG --implicit-check-not="PLEASE submit" %s
// RUN: not %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm-only -DGNU_ASM %s 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN-DIAG --implicit-check-not="PLEASE submit" %s
// RUN: not %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm-only -DFILE_SCOPE_ASM %s 2>&1 \
// RUN:   | FileCheck --check-prefix=FILE-DIAG --implicit-check-not="PLEASE submit" %s
// GNU-DIAG-COUNT-8: error: inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'
// GNU-DIAG: 8 errors generated.
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

// Asm labels are not inline assembly. The OPERANDS runs leave this out, since
// the backend diagnoses the call of the undefined real_name.
#ifndef OPERANDS
int renamed(void) __asm__("real_name");
int call_renamed(void) { return renamed(); }
// CHECK-LABEL: define {{.*}}i32 @call_renamed()
// CHECK: call i32 @real_name()
#endif

// Every accepted empty asm statement without operands becomes a single-thread
// fence followed by llvm.sideeffect: the compiler barrier, with or without
// 'volatile' and with other clobbers next to "memory", and an empty asm without
// operands or clobbers. The OPERANDS runs leave these functions out, so that
// no fence or llvm.sideeffect may appear anywhere in their output.
#ifndef OPERANDS
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
#endif

// Diagnosed only if emitted: unused (the system-header pattern, intrin.h)
// except under USED_INLINE.
static inline long long helper_with_asm(long long x) {
  __asm__("bswap %0" : "+r"(x)); // used-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  return x;
}

// Every other GNU asm statement is an error: a non-empty template, an empty
// one with an output register that no input ties ("=r", and "=rm", which
// allows a register), a condition-flag output, read-write or tied ("+@ccz",
// and "=@ccz" with "0": native code sets it from the flags after the
// statement, so it would not keep its input), an indirect output tied to an
// input of another kind (a float "=rm" output tied to a double input), and
// 'asm goto' (which is not turned into a fence). The empty statements with
// operands here are accepted (M12). GNU_ASM_AARCH64 checks AArch64's flag
// outputs ("+@cceq").
#ifdef GNU_ASM
int gnu_asm(int x, double d) {
  int y, z, fz = x, fe;
  float f = 1.0f;
  __asm__ volatile("nop"); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  __asm__("" : "+r"(x));
  __asm__ volatile("" : : "r"(x));
  __asm__ volatile("" : : "r"(x) : "memory");
  __asm__ volatile("nop" ::: "memory"); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  __asm__ volatile("" ::: "memory");
  __asm__ volatile("" : "=r"(y)); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  __asm__ volatile("" : "=rm"(z) : "r"(x)); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  __asm__ volatile("" : "+@ccz"(fz)); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  __asm__("" : "=@ccz"(fe) : "0"(x)); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  __asm__ volatile("" : "=rm"(f) : "0"(d)); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  __asm__ goto("" ::: "memory" : out); // gnu-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
  return x + y + z + fz + fe + (int)f;
out:
  return 0;
}
#endif

#ifdef GNU_ASM_AARCH64
int gnu_asm_aarch64(int x) {
  __asm__ volatile("" : "+@cceq"(x)); // gnua64-error {{inline assembly is not supported for the MSIL target 'aarch64-unknown-linux-gnu-msil'}}
  return x;
}
#endif

// The accepted empty asm statements with operands (backlog 2.3): inputs of
// every kind ("r", "r,m" alternatives, "g", "i", "m", 64-bit, pointer and FP
// values), read-write register and memory outputs, an output tied to input
// "0", two tied outputs, a memory-only output, a template of white space,
// volatile and non-volatile statements, "=rm", "=g" and "=r,r" outputs tied
// to "0" or "0,0", and ties of different types (at the end of the block).
// Each call must equal the native one; the srcloc metadata and the attribute
// group numbers are not compared.
#ifdef OPERANDS
void op_in_r(int v) { __asm__ volatile("" : : "r"(v) : "memory"); }
// OPERANDS-LABEL: define {{.*}}void @op_in_r(
// OPERANDS: call void asm sideeffect "", "r,~{memory},~{dirflag},~{fpsr},~{flags}"(i32 %{{[0-9]+}}) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: ret void

void op_in_rm(int *p) { __asm__ volatile("" : : "r,m"(p) : "memory"); }
// OPERANDS-LABEL: define {{.*}}void @op_in_rm(
// OPERANDS: call void asm sideeffect "", "r|m,~{memory},~{dirflag},~{fpsr},~{flags}"(ptr %{{[0-9]+}}) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: ret void

void op_inout_r(int *v) { __asm__ volatile("" : "+r"(*v) : : "memory"); }
// OPERANDS-LABEL: define {{.*}}void @op_inout_r(
// OPERANDS: [[V:%[0-9]+]] = load i32, ptr
// OPERANDS-NEXT: [[R:%[0-9]+]] = call i32 asm sideeffect "", "=r,0,~{memory},~{dirflag},~{fpsr},~{flags}"(i32 [[V]]) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: store i32 [[R]], ptr

void op_inout_m(int *v) { __asm__ volatile("" : "+m"(*v) : : "memory"); }
// OPERANDS-LABEL: define {{.*}}void @op_inout_m(
// OPERANDS: call void asm sideeffect "", "=*m,*m,~{memory},~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) [[P:%[0-9]+]], ptr elementtype(i32) [[P]]) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: ret void

int op_tied(int v) {
  int r;
  __asm__("" : "=r"(r) : "0"(v));
  return r;
}
// OPERANDS-LABEL: define {{.*}}i32 @op_tied(
// OPERANDS: [[R:%[0-9]+]] = call i32 asm "", "=r,0,~{dirflag},~{fpsr},~{flags}"(i32 %{{[0-9]+}}) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: store i32 [[R]], ptr %r

void *op_launder(void *p) {
  __asm__("" : "+r"(p));
  return p;
}
// OPERANDS-LABEL: define {{.*}}ptr @op_launder(
// OPERANDS: call ptr asm "", "=r,0,~{dirflag},~{fpsr},~{flags}"(ptr %{{[0-9]+}}) #{{[0-9]+}}, !srcloc

void op_two(int *a, long long *b) { __asm__ volatile("" : "+r"(*a), "+r"(*b)); }
// OPERANDS-LABEL: define {{.*}}void @op_two(
// OPERANDS: [[T:%[0-9]+]] = call { i32, i64 } asm sideeffect "", "=r,=r,0,1,~{dirflag},~{fpsr},~{flags}"(i32 %{{[0-9]+}}, i64 %{{[0-9]+}}) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: extractvalue { i32, i64 } [[T]], 0
// OPERANDS-NEXT: extractvalue { i32, i64 } [[T]], 1

void op_in_g(int v) { __asm__ volatile("" : : "g"(v)); }
// OPERANDS-LABEL: define {{.*}}void @op_in_g(
// OPERANDS: call void asm sideeffect "", "imr,~{dirflag},~{fpsr},~{flags}"(i32 %{{[0-9]+}}) #{{[0-9]+}}, !srcloc

void op_in_i(void) { __asm__ volatile("" : : "i"(42)); }
// OPERANDS-LABEL: define {{.*}}void @op_in_i(
// OPERANDS: call void asm sideeffect "", "i,~{dirflag},~{fpsr},~{flags}"(i32 42) #{{[0-9]+}}, !srcloc

void op_in_m(int *p) { __asm__ volatile("" : : "m"(*p)); }
// OPERANDS-LABEL: define {{.*}}void @op_in_m(
// OPERANDS: call void asm sideeffect "", "*m,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) %{{[0-9]+}}) #{{[0-9]+}}, !srcloc

void op_out_m(int *p) { __asm__ volatile("" : "=m"(*p)); }
// OPERANDS-LABEL: define {{.*}}void @op_out_m(
// OPERANDS: call void asm sideeffect "", "=*m,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) %{{[0-9]+}}) #{{[0-9]+}}, !srcloc

void op_wide(long long x, void *p, double d, float f) {
  __asm__ volatile("" : : "r"(x), "r"(p), "g"(d), "rm"(f) : "memory");
}
// OPERANDS-LABEL: define {{.*}}void @op_wide(
// OPERANDS: call void asm sideeffect "", "r,r,imr,rm,~{memory},~{dirflag},~{fpsr},~{flags}"(i64 %{{[0-9]+}}, ptr %{{[0-9]+}}, double %{{[0-9]+}}, float %{{[0-9]+}}) #{{[0-9]+}}, !srcloc

void op_blank(int v) { __asm__ volatile(" \t\n" : : "r"(v)); }
// OPERANDS-LABEL: define {{.*}}void @op_blank(
// OPERANDS: call void asm sideeffect " \09\0A", "r,~{dirflag},~{fpsr},~{flags}"(i32 %{{[0-9]+}}) #{{[0-9]+}}, !srcloc

// Outputs tied to input "0" that also allow memory ("=rm", and "=g", which is
// "imr"): clang emits them indirect, through the address of the initialized
// x, with the tied input after it. Native x86 takes the register alternative,
// so x holds the input afterwards, not 5 (section 12 row 'D36 rows of
// 2026-10-21 clarified (review-r2)'; black-box.c runs both).
int op_tied_rm(int y) {
  int x = 5;
  __asm__ volatile("" : "=rm"(x) : "0"(y));
  return x;
}
// OPERANDS-LABEL: define {{.*}}i32 @op_tied_rm(
// OPERANDS: store i32 5, ptr %x
// OPERANDS-NEXT: [[Y:%[0-9]+]] = load i32, ptr %y.addr
// OPERANDS-NEXT: call void asm sideeffect "", "=*rm,0,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) %x, i32 [[Y]]) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: load i32, ptr %x

int op_tied_g(int y) {
  int x = 5;
  __asm__("" : "=g"(x) : "0"(y));
  return x;
}
// OPERANDS-LABEL: define {{.*}}i32 @op_tied_g(
// OPERANDS: store i32 5, ptr %x
// OPERANDS-NEXT: [[Y:%[0-9]+]] = load i32, ptr %y.addr
// OPERANDS-NEXT: call void asm "", "=*imr,0,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) %x, i32 [[Y]]) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: load i32, ptr %x

// A tie written with alternatives: the output is tied to the input of its
// first alternative's matching constraint, as for "=r" with "0".
int op_tied_alt(int y) {
  int x;
  __asm__ volatile("" : "=r,r"(x) : "0,0"(y));
  return x;
}
// OPERANDS-LABEL: define {{.*}}i32 @op_tied_alt(
// OPERANDS: [[Y:%[0-9]+]] = load i32, ptr %y.addr
// OPERANDS-NEXT: [[R:%[0-9]+]] = call i32 asm sideeffect "", "=r|r,0|0,~{dirflag},~{fpsr},~{flags}"(i32 [[Y]]) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: store i32 [[R]], ptr %x

// The ties whose output and input types differ, all of which clang accepts
// (section 12 row 'D36 row of 2026-10-21 (...) clarified'): "+r" on a _Bool
// ties an i1 input to an i8 output; "+r" on a local register variable ties
// the output to the input of its own register ("={eax},{eax}"); a pointer
// output tied to a 64-bit integer input, and the reverse (Linux's
// RELOC_HIDE); and a pointer output tied to an int, which clang first
// zero-extends to 64 bits.
_Bool op_bool(_Bool b) {
  __asm__ volatile("" : "+r"(b));
  return b;
}
// OPERANDS-LABEL: define {{.*}}i1 @op_bool(
// OPERANDS: [[B:%[A-Za-z0-9.]+]] = icmp ne i8 %{{[0-9]+}}, 0
// OPERANDS-NEXT: [[R:%[0-9]+]] = call i8 asm sideeffect "", "=r,0,~{dirflag},~{fpsr},~{flags}"(i1 [[B]]) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: store i8 [[R]], ptr %b.addr

int op_regvar(int v) {
  register int x __asm__("eax") = v;
  __asm__ volatile("" : "+r"(x));
  return x;
}
// OPERANDS-LABEL: define {{.*}}i32 @op_regvar(
// OPERANDS: [[X:%[0-9]+]] = load i32, ptr %x
// OPERANDS-NEXT: [[R:%[0-9]+]] = call i32 asm sideeffect "", "={eax},{eax},~{dirflag},~{fpsr},~{flags}"(i32 [[X]]) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: store i32 [[R]], ptr %x

void *op_int_to_ptr(long long n) {
  void *p;
  __asm__("" : "=r"(p) : "0"(n));
  return p;
}
// OPERANDS-LABEL: define {{.*}}ptr @op_int_to_ptr(
// OPERANDS: [[N:%[0-9]+]] = load i64, ptr %n.addr
// OPERANDS-NEXT: [[R:%[0-9]+]] = call ptr asm "", "=r,0,~{dirflag},~{fpsr},~{flags}"(i64 [[N]]) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: store ptr [[R]], ptr %p

long long op_ptr_to_int(void *p) {
  long long n;
  __asm__("" : "=r"(n) : "0"(p));
  return n;
}
// OPERANDS-LABEL: define {{.*}}i64 @op_ptr_to_int(
// OPERANDS: [[P:%[0-9]+]] = load ptr, ptr %p.addr
// OPERANDS-NEXT: [[R:%[0-9]+]] = call i64 asm "", "=r,0,~{dirflag},~{fpsr},~{flags}"(ptr [[P]]) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: store i64 [[R]], ptr %n

void *op_int32_to_ptr(int v) {
  void *p;
  __asm__("" : "=r"(p) : "0"(v));
  return p;
}
// OPERANDS-LABEL: define {{.*}}ptr @op_int32_to_ptr(
// OPERANDS: [[W:%[0-9]+]] = zext i32 %{{[0-9]+}} to i64
// OPERANDS-NEXT: [[R:%[0-9]+]] = call ptr asm "", "=r,0,~{dirflag},~{fpsr},~{flags}"(i64 [[W]]) #{{[0-9]+}}, !srcloc
// OPERANDS-NEXT: store ptr [[R]], ptr %p
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
