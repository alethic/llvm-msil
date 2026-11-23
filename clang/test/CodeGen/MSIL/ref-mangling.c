// Mangling .NET references (ImplementationPlan.md 5.21.5 RG10 and 5.21.14
// RV15, milestone R-a). R-a mangles no reference (R-a2's 5.21.16 RN6 does):
// a C 'overloadable' function whose type has a reference gets
// err_ms_mangle_unsupported, 'cannot mangle this .NET reference type yet',
// from the Itanium mangler (the Linux, MinGW and portable triples) and from
// the Microsoft mangler (the MSVC triples), once per place the mangler meets
// the type: the result and a parameter give two errors, a repeated parameter
// type adds none (a substitution or back reference), a parameter alone
// gives one. Mangling happens in CodeGen, so -fsyntax-only accepts the
// declarations (SYNTAX), and an overloadable function without references in
// the same unit is mangled as usual (PLAIN).

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o /dev/null -DRESULT -verify=result %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -emit-llvm -o /dev/null -DRESULT -verify=result %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -emit-llvm -o /dev/null -DRESULT -verify=result %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -emit-llvm -o /dev/null -DRESULT -verify=result %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o /dev/null -DRESULT -verify=result %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -emit-llvm -o /dev/null -DRESULT -verify=result %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -emit-llvm -o /dev/null -DRESULT -verify=result %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o /dev/null -DRESULT -verify=result %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -emit-llvm -o /dev/null -DRESULT -verify=result %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o /dev/null -DREPEATED -verify=repeated %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o /dev/null -DREPEATED -verify=repeated %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -emit-llvm -o /dev/null -DREPEATED -verify=repeated %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o /dev/null -DPARAM -verify=param %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o /dev/null -DPARAM -verify=param %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fsyntax-only -DRESULT -DREPEATED -DPARAM -verify=syntax %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - -verify=plain %s | FileCheck %s --check-prefix=PLAIN-ITANIUM
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o - -verify=plain %s | FileCheck %s --check-prefix=PLAIN-MSVC

typedef struct __attribute__((cli_type("System.String"))) String String;
typedef struct __attribute__((cli_type("System.Object"))) Object Object;

#define OVERLOADABLE __attribute__((overloadable))

// syntax-no-diagnostics

#if defined(RESULT)
// result-error-re@+1 2 {{{{^}}cannot mangle this .NET reference type yet{{$}}}}
static OVERLOADABLE String ^ov(String ^s) { return s; }
static OVERLOADABLE int ov(int x) { return x; }
int use_result(void) { return ov(1) + (ov((String ^)0) == 0); }
#endif

#if defined(REPEATED)
// repeated-error-re@+1 {{{{^}}cannot mangle this .NET reference type yet{{$}}}}
static OVERLOADABLE int same(Object ^a, Object ^b) { return a == b; }
static OVERLOADABLE int same(int a, int b) { return a == b; }
int use_repeated(void) { return same(1, 2) + same((Object ^)0, (Object ^)0); }
#endif

#if defined(PARAM)
// param-error-re@+1 {{{{^}}cannot mangle this .NET reference type yet{{$}}}}
static OVERLOADABLE void sink(String ^s) {}
static OVERLOADABLE void sink(int x) {}
void use_param(void) { sink(1); sink((String ^)0); }
#endif

#if !defined(RESULT) && !defined(REPEATED) && !defined(PARAM)
// plain-no-diagnostics
static OVERLOADABLE int twice(int x) { return 2 * x; }
static OVERLOADABLE double twice(double x) { return 2 * x; }
int use_plain(void) { return twice(1) + (int)twice(2.0); }
// PLAIN-ITANIUM-DAG: define internal i32 @_ZL5twicei(
// PLAIN-ITANIUM-DAG: define internal double @_ZL5twiced(
// PLAIN-MSVC-DAG: define internal i32 @"?twice@@YAHH@Z"(
// PLAIN-MSVC-DAG: define internal double @"?twice@@YANN@Z"(
#endif
