// .NET references as inline assembly operands (ImplementationPlan.md 5.21.4
// RS29, 5.21.12 T-R24 and 5.21.14 RV14, milestone R-a). MSIL accepts only
// empty asm statements, and those with operands take the native path
// (clang/test/CodeGen/MSIL/inline-asm.c), where an operand would be held in
// a register or passed by address. So CodeGen's MSIL asm check
// (emitMSILAsmStmt) reports T-R24, "yet" for the inline-IL goal, at every
// reference operand: inputs ("r", "m", a tied "0"), outputs ("+r", "=r"), in
// any mix with other operands, with each operand reported; a non-empty
// template with a reference operand reports T-R24 too, since the operand
// check comes first. An empty asm statement without reference operands in
// the same unit keeps compiling (OK). Sema accepts all of these on the host
// triples (-fsyntax-only, SYNTAX): the rule is CodeGen's. On the portable
// triples msil64-unknown-dotnet and msil32-unknown-dotnet (PORTABLE), Sema
// refuses every asm operand, references or not, as 'invalid input size' or
// 'invalid output size' for its constraint (clang/lib/Basic/Targets/MSIL.h),
// so CodeGen and T-R24 never see one there: each statement gets one error,
// for its first operand (outputs before inputs), which in two statements is
// the int operand 'x'. The OK unit fails there too and runs on the host
// triple only.

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o /dev/null -verify %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o /dev/null -verify %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o /dev/null -verify %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -emit-llvm -o /dev/null -verify %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -emit-llvm -o /dev/null -verify %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -emit-llvm -o /dev/null -verify %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -emit-llvm -o /dev/null -verify %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -emit-llvm -o /dev/null -verify %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fsyntax-only -verify=syntax %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -emit-llvm -o /dev/null -verify=portable %s
// RUN: %clang_cc1 -triple msil32-unknown-dotnet -emit-llvm -o /dev/null -verify=portable %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -DOK -emit-llvm -o - -verify=ok %s | FileCheck %s --check-prefix=OK

typedef struct __attribute__((cli_type("System.String"))) String String;
typedef struct __attribute__((cli_type("System.Object"))) Object Object;

#if defined(OK)

// ok-no-diagnostics
// A reference function with an empty asm statement whose operands are not
// references.
// OK-LABEL: define internal i32 @no_reference_operands(
// OK:       fence syncscope("singlethread") seq_cst
// OK:       call void @llvm.sideeffect()
// OK:       call i32 asm sideeffect "", "=r,0{{.*}}"(i32
static int no_reference_operands(String ^s, int x) {
  __asm__ volatile("" ::: "memory");
  __asm__ volatile("" : "+r"(x));
  return x + (s == 0);
}
int use_ok(void) { return no_reference_operands(0, 1); }

#else

// syntax-no-diagnostics

static int inputs(String ^s, Object ^o, int x) {
  __asm__ volatile("" : : "r"(s));            // expected-error-re {{{{^}}a .NET reference as an inline assembly operand is not supported for MSIL yet{{$}}}}
                                              // portable-error-re@-1 {{{{^}}invalid input size for constraint 'r'{{$}}}}
  __asm__ volatile("" : : "m"(o));            // expected-error-re {{{{^}}a .NET reference as an inline assembly operand is not supported for MSIL yet{{$}}}}
                                              // portable-error-re@-1 {{{{^}}invalid input size for constraint 'm'{{$}}}}
  __asm__ volatile("" : : "r"(x), "r"(s));    // expected-error-re {{{{^}}a .NET reference as an inline assembly operand is not supported for MSIL yet{{$}}}}
                                              // portable-error-re@-1 {{{{^}}invalid input size for constraint 'r'{{$}}}}
  __asm__("" : : "g"(s) : "memory");          // expected-error-re {{{{^}}a .NET reference as an inline assembly operand is not supported for MSIL yet{{$}}}}
                                              // portable-error-re@-1 {{{{^}}invalid input size for constraint 'g'{{$}}}}
  return x;
}

static int outputs(String ^s, int x) {
  __asm__ volatile("" : "+r"(s));             // expected-error-re {{{{^}}a .NET reference as an inline assembly operand is not supported for MSIL yet{{$}}}}
                                              // portable-error-re@-1 {{{{^}}invalid output size for constraint '+r'{{$}}}}
  __asm__ volatile("" : "=r"(s) : "0"(s));    // expected-error-re 2 {{{{^}}a .NET reference as an inline assembly operand is not supported for MSIL yet{{$}}}}
                                              // portable-error-re@-1 {{{{^}}invalid output size for constraint '=r'{{$}}}}
  __asm__ volatile("" : "+r"(x), "+r"(s) : "r"(s)); // expected-error-re 2 {{{{^}}a .NET reference as an inline assembly operand is not supported for MSIL yet{{$}}}}
                                                    // portable-error-re@-1 {{{{^}}invalid output size for constraint '+r'{{$}}}}
  return x;
}

static int non_empty(String ^s) {
  __asm__ volatile("nop" : : "r"(s));         // expected-error-re {{{{^}}a .NET reference as an inline assembly operand is not supported for MSIL yet{{$}}}}
                                              // portable-error-re@-1 {{{{^}}invalid input size for constraint 'r'{{$}}}}
  return 0;
}

int use(void) { return inputs(0, 0, 1) + outputs(0, 2) + non_empty(0); }

#endif
