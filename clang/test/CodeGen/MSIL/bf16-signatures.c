// __bf16 parameters and results, scalars and GNU vectors (vector_size), on
// the MSIL triples (ImplementationPlan.md M24.c (5) and M24.f, fp-helpers
// F2 (a); llvm/test/CodeGen/MSIL/bf16-signatures.ll has the llc cases).
// Since M24 a bfloat in a signature is its 16-bit pattern in an int32, as a
// half is, and so is each lane of a vector of bfloat. clang gives a vector of
// __bf16 the IR of the vector of _Float16 of the same size, with bfloat for
// half, so the backend prints the two alike: this file is compiled with
// LANE=__bf16 and with LANE=_Float16, and the two IL modules are equal byte
// for byte, on x86_64-pc-windows-msvc-msil and aarch64-unknown-linux-gnu-msil
// at -O2 and -O0.
// - vector_size(4), two lanes: on x86_64-pc-windows-msvc-msil clang passes it
//   as a <2 x bfloat>, two int32 parameters ('elt2'(int32, int32)), as it
//   passes the _Float16 vector; on aarch64-unknown-linux-gnu-msil clang
//   coerces either vector to an i32, one int32.
// - vector_size(2), one lane: a <1 x bfloat> (an i32 parameter on aarch64) is
//   one int32 and compiles with no helper call, neither __truncsfbf2 nor
//   __extendbfsf2.
// - vector_size(8), four lanes: a <4 x bfloat> on both triples, four int32.
// Two-lane and four-lane results go through the hidden result pointer. At -O0
// call2 and call1 call id2 and id1 (the O0 lines); at -O2 the callees are
// inlined. No output has a diagnostic, a float32 or a helper name, and the
// outputs assemble (the aarch64 one as an ARM64 image, as in
// aarch64-aggregate-abi.c). Before M24 every lane was a float32: id1, elt1
// and call1 called __truncsfbf2, an error in this text, except on aarch64 at
// -O2, where id1 and call1 returned a float32 and id4 and elt4 took four.

// REQUIRES: msil-registered-target

// RUN: %clang --target=x86_64-pc-windows-msvc-msil -O2 -fno-verbose-asm -S -DLANE=__bf16 \
// RUN:   -o %t.win.bf.il %s 2>&1 | count 0
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -O2 -fno-verbose-asm -S -DLANE=_Float16 \
// RUN:   -o %t.win.h.il %s 2>&1 | count 0
// RUN: diff %t.win.bf.il %t.win.h.il
// RUN: FileCheck %s --check-prefixes=CHECK,WIN --implicit-check-not=__truncsfbf2 \
// RUN:   --implicit-check-not=__extendbfsf2 --implicit-check-not=float32 --implicit-check-not="'__" \
// RUN:   --input-file=%t.win.bf.il
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -O0 -fno-verbose-asm -S -DLANE=__bf16 \
// RUN:   -o %t.win.o0.bf.il %s 2>&1 | count 0
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -O0 -fno-verbose-asm -S -DLANE=_Float16 \
// RUN:   -o %t.win.o0.h.il %s 2>&1 | count 0
// RUN: diff %t.win.o0.bf.il %t.win.o0.h.il
// RUN: FileCheck %s --check-prefixes=CHECK,WIN,O0,WIN-O0 --implicit-check-not=__truncsfbf2 \
// RUN:   --implicit-check-not=__extendbfsf2 --implicit-check-not=float32 --implicit-check-not="'__" \
// RUN:   --input-file=%t.win.o0.bf.il
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -O2 -fno-verbose-asm -S -DLANE=__bf16 \
// RUN:   -o %t.a64.bf.il %s 2>&1 | count 0
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -O2 -fno-verbose-asm -S -DLANE=_Float16 \
// RUN:   -o %t.a64.h.il %s 2>&1 | count 0
// RUN: diff %t.a64.bf.il %t.a64.h.il
// RUN: FileCheck %s --check-prefixes=CHECK,A64 --implicit-check-not=__truncsfbf2 \
// RUN:   --implicit-check-not=__extendbfsf2 --implicit-check-not=float32 --implicit-check-not="'__" \
// RUN:   --input-file=%t.a64.bf.il
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -O0 -fno-verbose-asm -S -DLANE=__bf16 \
// RUN:   -o %t.a64.o0.bf.il %s 2>&1 | count 0
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -O0 -fno-verbose-asm -S -DLANE=_Float16 \
// RUN:   -o %t.a64.o0.h.il %s 2>&1 | count 0
// RUN: diff %t.a64.o0.bf.il %t.a64.o0.h.il
// RUN: FileCheck %s --check-prefixes=CHECK,A64,O0,A64-O0 --implicit-check-not=__truncsfbf2 \
// RUN:   --implicit-check-not=__extendbfsf2 --implicit-check-not=float32 --implicit-check-not="'__" \
// RUN:   --input-file=%t.a64.o0.bf.il
// RUN: %if ilasm %{ %ilasm-verify -output=%t.win.dll %t.win.bf.il %}
// RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.a64.o0.dll %t.a64.o0.bf.il %}

typedef LANE v1 __attribute__((vector_size(2)));
typedef LANE v2 __attribute__((vector_size(4)));
typedef LANE v4 __attribute__((vector_size(8)));

// CHECK-LABEL: {{^}}.method public static int32 'scal'(int32) cil managed{{$}}
LANE scal(LANE x) { return x; }

// WIN-LABEL: {{^}}.method public static int32 'elt2'(int32, int32) cil managed{{$}}
// A64-LABEL: {{^}}.method public static int32 'elt2'(int32) cil managed{{$}}
LANE elt2(v2 v) { return v[1]; }

// WIN-LABEL: {{^}}.method public static void 'id2'(native int, int32, int32) cil managed{{$}}
// A64-LABEL: {{^}}.method public static void 'id2'(native int, int32) cil managed{{$}}
v2 id2(v2 v) { return v; }

// WIN-LABEL: {{^}}.method public static void 'call2'(native int, int32, int32) cil managed{{$}}
// WIN-O0:    {{^}}  call void 'id2'(native int, int32, int32){{$}}
// A64-LABEL: {{^}}.method public static void 'call2'(native int, int32) cil managed{{$}}
// A64-O0:    {{^}}  call void 'id2'(native int, int32){{$}}
v2 call2(v2 v) { return id2(v); }

// CHECK-LABEL: {{^}}.method public static int32 'id1'(int32) cil managed{{$}}
v1 id1(v1 v) { return v; }

// CHECK-LABEL: {{^}}.method public static int32 'elt1'(int32) cil managed{{$}}
LANE elt1(v1 v) { return v[0]; }

// CHECK-LABEL: {{^}}.method public static int32 'call1'(int32) cil managed{{$}}
// O0:          {{^}}  call int32 'id1'(int32){{$}}
v1 call1(v1 v) { return id1(v); }

// CHECK-LABEL: {{^}}.method public static void 'id4'(native int, int32, int32, int32, int32) cil managed{{$}}
v4 id4(v4 v) { return v; }

// CHECK-LABEL: {{^}}.method public static int32 'elt4'(int32, int32, int32, int32) cil managed{{$}}
// CHECK:       {{^}}} // end of method 'elt4'{{$}}
// CHECK-NOT:   {{^}}.method
LANE elt4(v4 v) { return v[3]; }
