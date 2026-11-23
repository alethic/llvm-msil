// ClangIR and .NET references (ImplementationPlan.md 5.21.13 RH4 and 5.21.14
// RV16, milestone R-a). ClangIR is built and reachable on MSIL triples with
// -fclangir, though MSIL does not use it. Its switches over type classes know
// the reference type (a scalar for getEvaluationKind, never variably
// modified), so a reference that reaches it gets ClangIR's
// not-yet-implemented error from CIRGenTypes::convertType, whose default case
// gives the type a stand-in. RH4's hook in ScalarExprEmitter::VisitCastExpr
// gives a null pointer constant converted to a reference (CK_NullToPointer) a
// zero of that stand-in instead of casting it to a pointer type, so the unit
// ends with the error and exit status 1, never an assertion or a stack dump,
// for -emit-cir and -emit-llvm, in each of these forms:
// - main: a local without an initializer (RS2's NULL), the initializers 0 and
//   NULL, a truth test (RS6's 'r != NULL'), '!', '== NULL', and a static
//   function with a reference parameter called with 0 and with NULL;
// - RESULT: a static reference function that returns 0 and NULL. It is
//   'used' so that ClangIR emits its body at once: after its first error
//   ClangIR emits no later top-level declaration and no deferred function,
//   so that function is the only one of its run.
// convertType caches the stand-in, so each run reports the error once.

// REQUIRES: cir-support

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fclangir -emit-cir -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fclangir -emit-cir -DRESULT -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fclangir -emit-cir -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fclangir -emit-cir -DRESULT -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fclangir -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fclangir -emit-llvm -DRESULT -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple msil64-unknown-dotnet -fclangir -emit-cir -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple msil64-unknown-dotnet -fclangir -emit-cir -DRESULT -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-musl-msil -fclangir -emit-cir -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-musl-msil -fclangir -emit-cir -DRESULT -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple x86_64-w64-windows-gnu-msil -fclangir -emit-cir -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple x86_64-w64-windows-gnu-msil -fclangir -emit-cir -DRESULT -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fclangir -emit-cir -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fclangir -emit-cir -DRESULT -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -fclangir -emit-cir -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -fclangir -emit-cir -DRESULT -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple aarch64-unknown-linux-musl-msil -fclangir -emit-cir -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple aarch64-unknown-linux-musl-msil -fclangir -emit-cir -DRESULT -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple aarch64-w64-windows-gnu-msil -fclangir -emit-cir -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"
// RUN: not %clang_cc1 -triple aarch64-w64-windows-gnu-msil -fclangir -emit-cir -DRESULT -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Assertion" --implicit-check-not="Stack dump"

// CHECK: {{^}}error: ClangIR code gen Not Yet Implemented: processing of type: CLIReference{{$}}

#define NULL ((void *)0)

typedef struct __attribute__((cli_type("System.String"))) String String;

#if defined(RESULT)

static __attribute__((used)) String ^nil(int c) {
  if (c)
    return 0;
  return NULL;
}

#else

static void sink(String ^s) { (void)s; }

int main(void) {
  String ^local;
  String ^a = 0;
  String ^b = NULL;
  int r = 0;
  if (a)
    r += 1;
  r += !b;
  r += local == NULL;
  sink(0);
  sink(NULL);
  return r;
}

#endif
