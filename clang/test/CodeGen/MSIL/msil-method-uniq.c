// The IR name of an internal-linkage managed-call declaration
// (ImplementationPlan.md 5.20.4 MR2 as amended in section 12, the rows
// '5.20.4 (IR name, MR4) and T-M17 amended (...)' item (1) of 2026-11-04 and
// '5.20.4 (IR name, MR4) amended again (...)'; milestone MC-1):
// '__msil_mc.<symbol>.__uniq.<n>', where <n> hashes the declaration's
// '!msil.managed.calls' operands 1-6 (TYPE, MEMBER, SIGNATURE, ASSEMBLY, the
// conversions and the msil_catch list) and its function type, both as C
// types and as the IR types of its result and parameters with their
// extensions ('signext', 'zeroext') (llvm::cli::getManagedCallUniqPostfix),
// never the main file's name. Two internal declarations whose stubs differ
// never share an IR name, so the IR linker never merges two different stubs,
// and equal declarations share one; a hash of the main file's name gave two
// different declarations in two files named 'u.c' one name, and a false
// T-M17 under LTO.
// - d1/u.c and d2/u.c are each compiled from their own directory, so both
//   main files are 'u.c'. Their 'get' declarations call different members
//   (Abs and Sign) and get different names.
// - Their 'cvt' declarations differ only in the C type, 'short' and
//   'unsigned short', which the IR type does not tell apart (both i16, and
//   on x86_64-pc-windows-msvc-msil without an extension): the names differ,
//   so the hash takes the C type, not only the IR type. On
//   x86_64-pc-windows-msvc-msil the two IR declarations, and so their stubs,
//   are the same: declarations with equal stubs may still get two names.
// - v.c repeats d1/u.c's declarations in a main file of another name, and
//   gets d1/u.c's names.
// - c.c's 'static char to8(int)' (System.Convert::ToSByte, 'int8(int32)') is
//   compiled on msil64-unknown-dotnet twice, the second time with
//   -fno-signed-char (the driver's -funsigned-char; CHAR). A plain 'char'
//   result is 'signext i8' there by default and 'zeroext i8' with the option,
//   and the stub ends with conv.i1 or conv.u1 to match (5.20.3 MS5's 'val' row
//   as built), so the two declarations get different names. The host triples
//   give both a plain 'i8' result and equal stubs, and whether they give the
//   two one name or two is not pinned.
// The names are compared through FileCheck captures across the concatenated
// outputs, never as literal hash values.

// RUN: rm -rf %t && split-file %s %t
// DEFINE: %{t} =
// DEFINE: %{run} = cd %t/d1 && %clang_cc1 -triple %{t} -emit-llvm -o %t/d1.%{t}.ll u.c && \
// DEFINE:   cd %t/d2 && %clang_cc1 -triple %{t} -emit-llvm -o %t/d2.%{t}.ll u.c && \
// DEFINE:   cd %t && %clang_cc1 -triple %{t} -emit-llvm -o %t/v.%{t}.ll v.c && \
// DEFINE:   cat %t/d1.%{t}.ll %t/d2.%{t}.ll %t/v.%{t}.ll | FileCheck %s
// REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
// RUN: %{run}
// REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
// RUN: %{run}
// REDEFINE: %{t} = msil64-unknown-dotnet
// RUN: %{run}
// RUN: cd %t && %clang_cc1 -triple msil64-unknown-dotnet -emit-llvm -o %t/c.ll c.c
// RUN: cd %t && %clang_cc1 -triple msil64-unknown-dotnet -fno-signed-char -emit-llvm -o %t/cu.ll c.c
// RUN: cat %t/c.ll %t/cu.ll | FileCheck %s --check-prefix=CHAR

// d1/u.c:
// CHECK:      {{^}}source_filename = "u.c"{{$}}
// CHECK:      call {{.*}}i32 @__msil_mc.get.__uniq.[[ABS:[0-9]+]](
// CHECK:      call {{.*}}i16 @__msil_mc.cvt.__uniq.[[SHORT:[0-9]+]](
// CHECK:      {{^}}!{{[0-9]+}} = !{!"__msil_mc.get.__uniq.[[ABS]]", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}{{$}}
// CHECK:      {{^}}!{{[0-9]+}} = !{!"__msil_mc.cvt.__uniq.[[SHORT]]", !"System.Math", !"Abs", !"int16(int16)", !"", !"val;val", !""}{{$}}

// d2/u.c: neither name of d1/u.c appears, in the calls, the declarations or
// the operands.
// CHECK:      {{^}}source_filename = "u.c"{{$}}
// CHECK-NOT:  .__uniq.[[ABS]]{{[^0-9]}}
// CHECK-NOT:  .__uniq.[[SHORT]]{{[^0-9]}}
// CHECK:      {{^}}!msil.managed.calls = {{.+}}{{$}}
// CHECK-NOT:  .__uniq.[[ABS]]{{[^0-9]}}
// CHECK-NOT:  .__uniq.[[SHORT]]{{[^0-9]}}
// CHECK:      {{^}}!{{[0-9]+}} = !{!"__msil_mc.get.__uniq.{{[0-9]+}}", !"System.Math", !"Sign", !"int32(int32)", !"", !"val;val", !""}{{$}}
// CHECK-NOT:  .__uniq.[[ABS]]{{[^0-9]}}
// CHECK-NOT:  .__uniq.[[SHORT]]{{[^0-9]}}
// CHECK:      {{^}}!{{[0-9]+}} = !{!"__msil_mc.cvt.__uniq.{{[0-9]+}}", !"System.Math", !"Abs", !"int16(int16)", !"", !"val;val", !""}{{$}}
// CHECK-NOT:  .__uniq.[[ABS]]{{[^0-9]}}
// CHECK-NOT:  .__uniq.[[SHORT]]{{[^0-9]}}

// v.c: d1/u.c's names.
// CHECK:      {{^}}source_filename = "v.c"{{$}}
// CHECK:      call {{.*}}i32 @__msil_mc.get.__uniq.[[ABS]](
// CHECK:      call {{.*}}i16 @__msil_mc.cvt.__uniq.[[SHORT]](
// CHECK:      {{^}}declare {{.*}}i32 @__msil_mc.get.__uniq.[[ABS]](
// CHECK:      {{^}}declare {{.*}}i16 @__msil_mc.cvt.__uniq.[[SHORT]](
// CHECK:      {{^}}!{{[0-9]+}} = !{!"__msil_mc.get.__uniq.[[ABS]]", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}{{$}}
// CHECK:      {{^}}!{{[0-9]+}} = !{!"__msil_mc.cvt.__uniq.[[SHORT]]", !"System.Math", !"Abs", !"int16(int16)", !"", !"val;val", !""}{{$}}

// c.c on msil64-unknown-dotnet, 'char' signed (the default): 'signext i8'.
// CHAR:      {{^}}source_filename = "c.c"{{$}}
// CHAR:      call signext i8 @__msil_mc.to8.__uniq.[[SCHAR:[0-9]+]](
// CHAR:      {{^}}declare signext i8 @__msil_mc.to8.__uniq.[[SCHAR]](i32 noundef)
// CHAR:      {{^}}!{{[0-9]+}} = !{!"__msil_mc.to8.__uniq.[[SCHAR]]", !"System.Convert", !"ToSByte", !"int8(int32)", !"", !"val;val", !""}{{$}}

// c.c with -fno-signed-char: 'zeroext i8' and another name. The call is not
// matched, so the first CHAR-NOT sees its name; the name is captured from the
// declaration and reused in the operand.
// CHAR:      {{^}}source_filename = "c.c"{{$}}
// CHAR-NOT:  .__uniq.[[SCHAR]]{{[^0-9]}}
// CHAR:      {{^}}declare zeroext i8 @__msil_mc.to8.__uniq.[[UCHAR:[0-9]+]](i32 noundef)
// CHAR-NOT:  .__uniq.[[SCHAR]]{{[^0-9]}}
// CHAR:      {{^}}!{{[0-9]+}} = !{!"__msil_mc.to8.__uniq.[[UCHAR]]", !"System.Convert", !"ToSByte", !"int8(int32)", !"", !"val;val", !""}{{$}}
// CHAR-NOT:  .__uniq.[[SCHAR]]{{[^0-9]}}

//--- d1/u.c
static int get(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
static short cvt(short x) __attribute__((msil_method("System.Math", "Abs", "int16(int16)")));
int use(int x) { return get(x) + cvt((short)x); }

//--- d2/u.c
static int get(int x) __attribute__((msil_method("System.Math", "Sign", "int32(int32)")));
static unsigned short cvt(unsigned short x) __attribute__((msil_method("System.Math", "Abs", "int16(int16)")));
int use(int x) { return get(x) + cvt((unsigned short)x); }

//--- v.c
static int get(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
static short cvt(short x) __attribute__((msil_method("System.Math", "Abs", "int16(int16)")));
int use_v(int x) { return get(x) + cvt((short)x); }

//--- c.c
static char to8(int x) __attribute__((msil_method("System.Convert", "ToSByte", "int8(int32)")));
int use_c(int x) { return to8(x); }
