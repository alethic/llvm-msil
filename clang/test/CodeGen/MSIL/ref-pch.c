// .NET references through a precompiled header (ImplementationPlan.md 5.21.3
// RC3, RC8 (serialization: TYPE_CLI_REFERENCE, the TypeLoc reader and
// writer) and 5.21.14 RV16, milestone R-a). The PCH holds class structs,
// typedefs of references, a sugared and a qualified reference, and static
// inline functions that compare, convert and cast references. A unit built
// with -include-pch:
// - sees the same types: two spellings from the PCH, and a new spelling of
//   the same class written in the unit itself, are one type (the reader
//   makes the canonical node again through the getter, and the implicit
//   struct is never serialized), while different classes stay different;
// - gets the same diagnostics with clang's aka text ('SRef' (aka
//   'System.String ^')) (ERRORS);
// - compiles to the same IR as a unit that includes the header text (the
//   CHECK lines, for both), with the PCH's inline functions, the helper and
//   the cast stub;
// - prints the PCH's declarations back with -ast-print (PRINT).

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -x c-header -emit-pch -o %t.win.pch %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -include-pch %t.win.pch -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -include-pch %t.win.pch -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -include-pch %t.win.pch -DERRORS -fsyntax-only -verify=errors %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -x c-header -emit-pch -o %t.lnx.pch %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -include-pch %t.lnx.pch -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -include-pch %t.lnx.pch -DERRORS -fsyntax-only -verify=errors %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -x c-header -emit-pch -o %t.p64.pch %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -include-pch %t.p64.pch -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -x c-header -emit-pch -o %t.mus.pch %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -include-pch %t.mus.pch -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -x c-header -emit-pch -o %t.gnu.pch %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -include-pch %t.gnu.pch -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -x c-header -emit-pch -o %t.a64win.pch %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -include-pch %t.a64win.pch -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -x c-header -emit-pch -o %t.a64lnx.pch %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -include-pch %t.a64lnx.pch -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -x c-header -emit-pch -o %t.a64mus.pch %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -include-pch %t.a64mus.pch -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -x c-header -emit-pch -o %t.a64gnu.pch %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -include-pch %t.a64gnu.pch -emit-llvm -o - %s | FileCheck %s
// The same unit without a PCH (the header text included as it is).
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -include %s -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -include-pch %t.win.pch -ast-print -o - %s | FileCheck %s --check-prefix=PRINT

#ifndef HEADER
#define HEADER

typedef struct __attribute__((cli_type("System.String"))) String String;
typedef struct __attribute__((cli_type(" System.String "))) Text Text;
typedef struct __attribute__((cli_type("System.Object"))) Object Object;
typedef struct __attribute__((cli_type("System.IO.Stream"))) Stream Stream;
typedef struct __attribute__((cli_type("class System.String[]"))) Strings Strings;
typedef String ^SRef;
typedef Text ^const CTRef;

static inline int h_same(String ^a, Text ^b) { return a == b; }
static inline Object ^h_up(SRef a) { return a; }
static inline String ^h_down(Object ^o) { return (String ^)o; }
static inline int h_truth(CTRef t) { return t ? 1 : 2; }
String ^h_machine(void) __attribute__((msil_method("System.Environment", "get_MachineName", "string()")));

#else

// expected-no-diagnostics

// A new spelling of String in the unit is the PCH's String.
typedef struct __attribute__((cli_type("System.String"))) Local Local;
_Static_assert(__builtin_types_compatible_p(String ^, Text ^), "one class from the PCH");
_Static_assert(__builtin_types_compatible_p(SRef, Local ^), "the unit's spelling is the PCH's class");
_Static_assert(__builtin_types_compatible_p(CTRef, Local ^const), "qualified, through a typedef");
_Static_assert(!__builtin_types_compatible_p(String ^, Object ^), "two classes");
_Static_assert(!__builtin_types_compatible_p(String ^, Strings ^), "a class and an array");
_Static_assert(!__builtin_types_compatible_p(Stream ^, Object ^), "two classes");

#if defined(ERRORS)
static void errors(SRef s, Object ^o, Strings ^a) {
  s = o;       // errors-error-re {{{{^}}assigning to 'SRef' (aka 'System.String ^') from incompatible type 'Object ^' (aka 'System.Object ^'){{$}}}}
  int i = a;   // errors-error-re {{{{^}}initializing 'int' with an expression of incompatible type 'Strings ^' (aka 'string[] ^'){{$}}}}
  Local ^l = (Local ^)i; // errors-error-re {{{{^}}converting 'int' to the .NET reference type 'Local ^' (aka 'System.String ^') is not supported for MSIL; only a .NET reference or a null pointer constant converts to it{{$}}}}
}
#endif

int test(void) {
  Local ^m = h_machine();
  Object ^o = h_up(m);
  return h_same(m, h_down(o)) + h_truth(m);
}

// CHECK-LABEL: define {{.*}}i32 @test()
// CHECK:       call target("msil.ref") @__msil_mc.h_machine()
// CHECK:       call target("msil.ref") @h_up(target("msil.ref") %{{.+}})
// CHECK:       call target("msil.ref") @h_down(target("msil.ref") %{{.+}})
// CHECK:       call i32 @h_same(target("msil.ref") %{{.+}}, target("msil.ref") %{{.+}})
// CHECK:       call i32 @h_truth(target("msil.ref") %{{.+}})
// CHECK-DAG:   define internal target("msil.ref") @h_up(target("msil.ref") %a)
// CHECK-DAG:   define internal target("msil.ref") @h_down(target("msil.ref") %o)
// CHECK-DAG:   call target("msil.ref") @"__msil_mc.<castclass>.__uniq.{{[0-9]+}}"(target("msil.ref") %{{.+}})
// CHECK-DAG:   define internal i32 @h_same(target("msil.ref") %a, target("msil.ref") %b)
// CHECK-DAG:   define internal i32 @h_truth(target("msil.ref") %t)
// CHECK-DAG:   call i1 @__msil_ref.eq(target("msil.ref") %{{.+}}, target("msil.ref") zeroinitializer)
// CHECK-DAG:   declare i1 @__msil_ref.eq(target("msil.ref"), target("msil.ref"))
// CHECK-DAG:   attributes #{{[0-9]+}} = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="System.String" }

// PRINT: typedef String ^SRef;
// PRINT: typedef Text ^const CTRef;
// PRINT: static inline Object ^h_up(SRef a) {
// PRINT: static inline String ^h_down(Object ^o) {
// PRINT-NEXT: return (String ^)o;
// PRINT: typedef struct __attribute__((cli_type("System.String"))) Local Local;

#endif
