// A compiler barrier, 'asm volatile("" ::: "memory")', between two stores
// compiles for the MSIL triples (ImplementationPlan.md decision D36 as M24
// amends it, 5.14.4 T2 and T6, 5.15.6; M7.a, M7.c; plan section 12, row 'M1.d
// amended'): CodeGen turns the empty asm statement into
// 'fence syncscope("singlethread") seq_cst' plus a call to llvm.sideeffect,
// and the backend lowers the fence to "call void '__msil_compiler_barrier'()"
// between the two stind.i4, a call of the trailer definition that the module
// defines once, after every other method:
//   .method assembly static void '__msil_compiler_barrier'() cil managed noinlining
// whose body is 'ret'. The call keeps both stores and does not let a load move
// across it. The CALLS runs check that an output calls the barrier three
// times, defines it once at its end and names it nowhere else, and no output
// names the MEMBARRIER pseudo that the call replaces.
// __atomic_signal_fence is the same compiler barrier, and
// __atomic_thread_fence(__ATOMIC_SEQ_CST) is a full barrier,
// 'call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()'.
// 'clang --target=x86_64-pc-windows-msvc-msil -O2 -S' (the Design.md 12
// command line, without -ffreestanding, which these functions do not need)
// succeeds without a diagnostic, on Linux and aarch64-unknown-linux-gnu-msil
// as well, and at -O0, where every parameter is spilled to the static frame
// (M7). The objects of 'clang -c' have the barrier as an UNDEFINED FUNCTION
// symbol and define no method of that name (5.14.4 T1): msil-ld's '<trailer>'
// input defines it at link time.
//
// The BBOX runs (M12, plan section 12 row 'D36 amended'; backlog 2.3) compile
// the black_box form 'asm volatile("" : : "r"(v) : "memory")' with -O2 -S:
// clang emits it as the native triples do, and MSILPrepareIR lowers it to the
// same singlethread fence, an llvm.fake.use of v and llvm.sideeffect. In
// bbox() the asm comes after a computation, whose only use is the asm, and a
// store, and before a second store to the same address: the product is still
// computed (BBOXV), the first store, which the second would make dead without
// the barrier, is kept before the barrier call, and the second follows it
// (BBOX). The function is compiled only under -DBBOX, so the CALLS count
// above stays three.
//
// The -O2 and -O0 outputs assemble when an ilasm is available;
// llvm/test/CodeGen/MSIL/fence.ll also runs llc and ilasm on a copy of the IR
// that clang emits for barrier() here (function 'asm_barrier_clang'), and
// msil/test/M1/05-memory/barrier.c assembles and runs C code of this kind.

// REQUIRES: msil-registered-target

// RUN: %clang --target=x86_64-pc-windows-msvc-msil -O2 -S -o %t.win.il %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DIAG --allow-empty
// RUN: FileCheck %s --input-file=%t.win.il --check-prefix=MOD
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -O2 -S -o %t.lnx.il %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DIAG --allow-empty
// RUN: FileCheck %s --input-file=%t.lnx.il --check-prefix=MOD

// Without verbose comments the method bodies are exact.
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -O2 -fno-verbose-asm -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=EXACT
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -O2 -fno-verbose-asm -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=EXACT
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -O2 -fno-verbose-asm -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=EXACT
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -O2 -fno-verbose-asm -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=CALLS --implicit-check-not=__msil_compiler_barrier \
// RUN:     --implicit-check-not=MEMBARRIER
// RUN: %if ilasm %{ %ilasm-verify -output=%t.win.dll %t.win.il %}

// -O0: the parameters live in the frame; the barrier still separates the two
// stores of the constants.
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -O0 -fno-verbose-asm -S -o %t.win.o0.il %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DIAG --allow-empty
// RUN: FileCheck %s --input-file=%t.win.o0.il --check-prefix=O0
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -O0 -fno-verbose-asm -S -o %t.lnx.o0.il %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DIAG --allow-empty
// RUN: FileCheck %s --input-file=%t.lnx.o0.il --check-prefix=O0
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -O0 -fno-verbose-asm -S -o %t.a64.o0.il %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DIAG --allow-empty
// RUN: FileCheck %s --input-file=%t.a64.o0.il --check-prefix=O0
// RUN: FileCheck %s --input-file=%t.lnx.o0.il --check-prefix=CALLS --implicit-check-not=__msil_compiler_barrier \
// RUN:   --implicit-check-not=MEMBARRIER
// RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o0.dll %t.lnx.o0.il %}

// 'clang -c' on the three triples.
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -O2 -c -o %t.lnx.o %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DIAG --allow-empty
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -O0 -c -o %t.win.o0.o %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DIAG --allow-empty
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -O2 -c -o %t.a64.o %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DIAG --allow-empty
// DEFINE: %{obj} = FileCheck %s --check-prefix=OBJ --implicit-check-not=__msil_compiler_barrier
// RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t.lnx.o | %{obj} %}
// RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t.win.o0.o | %{obj} %}
// RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t.a64.o | %{obj} %}

// The black_box form (bbox below).
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -O2 -fno-verbose-asm -S -DBBOX -o %t.bbox.win.il %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DIAG --allow-empty
// RUN: FileCheck %s --input-file=%t.bbox.win.il --check-prefix=BBOX
// RUN: FileCheck %s --input-file=%t.bbox.win.il --check-prefix=BBOXV
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -O2 -fno-verbose-asm -S -DBBOX -o %t.bbox.lnx.il %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DIAG --allow-empty
// RUN: FileCheck %s --input-file=%t.bbox.lnx.il --check-prefix=BBOX
// RUN: FileCheck %s --input-file=%t.bbox.lnx.il --check-prefix=BBOXV

// DIAG-NOT: {{warning:|error:|LLVM ERROR|PLEASE submit}}

// CALLS-COUNT-3: {{^}}  call void '__msil_compiler_barrier'(){{$}}
// CALLS:         {{^}}.method assembly static void '__msil_compiler_barrier'() cil managed noinlining{{$}}
// CALLS-NEXT:    {{^\{$}}
// CALLS-NEXT:    {{^}}  .maxstack 0{{$}}
// CALLS-NEXT:    {{^}}  ret{{$}}
// CALLS-NEXT:    {{^}}} // end of method '__msil_compiler_barrier'{{$}}
// CALLS-NOT:     {{.}}

// MOD:      {{^}}// MSIL module generated by LLVM{{$}}
// MOD:      {{^}}.method public static void 'barrier'(native int, native int) cil managed{{$}}
// MOD:      {{^}}} // end of method 'barrier'
// MOD:      {{^}}.method public static int32 'reload'(native int) cil managed{{$}}
// MOD:      {{^}}.method public static void 'fences'(native int) cil managed{{$}}
// MOD-NOT:  __sync_synchronize
// MOD-NOT:  pinvokeimpl
// MOD-NOT:  MEMBARRIER
// MOD:      {{^}}.method assembly static void '__msil_compiler_barrier'() cil managed noinlining{{$}}

// OBJ:      {{^}}LinkInfo v1.0 triple={{.*}}
// OBJ-NEXT: {{^}}Symbol 0 FUNCTION GLOBAL DEFAULT 'barrier' method=0x06000001{{$}}
// OBJ-NEXT: {{^}}Symbol 1 FUNCTION GLOBAL DEFAULT 'reload' method=0x06000002{{$}}
// OBJ-NEXT: {{^}}Symbol 2 FUNCTION GLOBAL DEFAULT 'fences' method=0x06000003{{$}}
// OBJ-NEXT: {{^}}Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED '__msil_compiler_barrier'{{$}}
// OBJ-NOT:  {{.}}

void barrier(int *p, int *q) {
  *p = 1;
  __asm__ volatile("" ::: "memory");
  *q = 2;
}

// EXACT-LABEL: .method {{.*}}'barrier'(
// EXACT-NEXT:  {{^\{$}}
// EXACT-NEXT:  {{^}}  .maxstack 2{{$}}
// EXACT-NEXT:  {{^}}  ldarg.0{{$}}
// EXACT-NEXT:  {{^}}  ldc.i4.1{{$}}
// EXACT-NEXT:  {{^}}  stind.i4{{$}}
// EXACT-NEXT:  {{^}}  call void '__msil_compiler_barrier'(){{$}}
// EXACT-NEXT:  {{^}}  ldarg.1{{$}}
// EXACT-NEXT:  {{^}}  ldc.i4.2{{$}}
// EXACT-NEXT:  {{^}}  stind.i4{{$}}
// EXACT-NEXT:  {{^}}  ret{{$}}
// EXACT-NEXT:  {{^}}} // end of method 'barrier'{{$}}

// O0-LABEL:    .method {{.*}}'barrier'(
// O0:          {{^}}  localloc{{$}}
// O0:          {{^}}  ldc.i4.1{{$}}
// O0-NEXT:     {{^}}  stind.i4{{$}}
// O0-NEXT:     {{^}}  call void '__msil_compiler_barrier'(){{$}}
// O0:          {{^}}  ldc.i4.2{{$}}
// O0-NEXT:     {{^}}  stind.i4{{$}}
// O0:          {{^}}  ret{{$}}
// O0-NEXT:     {{^}}} // end of method 'barrier'{{$}}

int reload(int *p) {
  int a = *p;
  __asm__ volatile("" ::: "memory");
  return a + *p;
}

// The value loaded before the barrier waits in a local; *p is loaded again
// after it.
// EXACT-LABEL: .method {{.*}}'reload'(
// EXACT:       {{^}}  ldind.i4{{$}}
// EXACT-NEXT:  {{^}}  stloc.0{{$}}
// EXACT-NEXT:  {{^}}  call void '__msil_compiler_barrier'(){{$}}
// EXACT:       {{^}}  ldind.i4{{$}}
// EXACT:       {{^}}  add{{$}}
// EXACT-NEXT:  {{^}}  ret{{$}}
// EXACT-NEXT:  {{^}}} // end of method 'reload'{{$}}

void fences(int *p) {
  *p = 1;
  __atomic_signal_fence(__ATOMIC_SEQ_CST);
  *p = 2;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  *p = 3;
}

// EXACT-LABEL: .method {{.*}}'fences'(
// EXACT-NEXT:  {{^\{$}}
// EXACT-NEXT:  {{^}}  .maxstack 2{{$}}
// EXACT-NEXT:  {{^}}  ldarg.0{{$}}
// EXACT-NEXT:  {{^}}  ldc.i4.1{{$}}
// EXACT-NEXT:  {{^}}  stind.i4{{$}}
// EXACT-NEXT:  {{^}}  call void '__msil_compiler_barrier'(){{$}}
// EXACT-NEXT:  {{^}}  ldarg.0{{$}}
// EXACT-NEXT:  {{^}}  ldc.i4.2{{$}}
// EXACT-NEXT:  {{^}}  stind.i4{{$}}
// EXACT-NEXT:  {{^}}  call void [mscorlib]System.Threading.Interlocked::MemoryBarrier(){{$}}
// EXACT-NEXT:  {{^}}  ldarg.0{{$}}
// EXACT-NEXT:  {{^}}  ldc.i4.3{{$}}
// EXACT-NEXT:  {{^}}  stind.i4{{$}}
// EXACT-NEXT:  {{^}}  ret{{$}}
// EXACT-NEXT:  {{^}}} // end of method 'fences'{{$}}

#ifdef BBOX
void bbox(int *p, int x, int y) {
  int v = x * y;
  *p = 1;
  __asm__ volatile("" : : "r"(v) : "memory");
  *p = 2;
}
#endif

// BBOX-LABEL:  .method {{.*}}'bbox'(
// BBOX:        {{^}}  ldc.i4.1{{$}}
// BBOX-NEXT:   {{^}}  stind.i4{{$}}
// BBOX:        {{^}}  call void '__msil_compiler_barrier'(){{$}}
// BBOX:        {{^}}  ldc.i4.2{{$}}
// BBOX-NEXT:   {{^}}  stind.i4{{$}}
// BBOX:        {{^}}  ret{{$}}
// BBOX-NEXT:   {{^}}} // end of method 'bbox'{{$}}

// BBOXV-LABEL: .method {{.*}}'bbox'(
// BBOXV:       {{^}}  mul{{$}}
// BBOXV:       {{^}}} // end of method 'bbox'{{$}}
