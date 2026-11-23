// .NET references never live in C memory (ImplementationPlan.md 5.21.4 RS13,
// RS14, RS25, RS26 and 5.21.14 RV5, milestone R-a; Design.md section 4: "C
// memory never holds a GC reference."). A reference is held only in IL
// locals, arguments, results and the evaluation stack, so every way of putting
// one into memory is refused:
// - RS13: taking its address, T-R9 (unary '&' on a local, a 'const' one, a
//   parameter, through parentheses, in sizeof's operand, and
//   __builtin_addressof).
// - RS14, by the sizeless rules with clang's texts: struct and union fields,
//   bit-fields, a field of an anonymous struct, arrays (local, of arrays, a
//   typedef, a parameter written as an array, a flexible array member),
//   VLAs, file-scope variables (a definition, a tentative definition,
//   'extern', 'static', '_Thread_local'), static locals, block-scope
//   'extern' and '_Thread_local', and the 'aligned' attribute and
//   '_Alignas' (msil-ref-type.c covers '_Atomic', msil-ref-ops.c sizeof,
//   compound literals, braced initializers and __builtin_bit_cast).
// - RS25: T-R20 for 'cleanup' (whose function would get the address),
//   'uninitialized' (which could only be ignored silently), with -fblocks
//   '__block', and 'annotate' on a reference variable (after the declarator
//   and before the type) and on a reference parameter, whose
//   llvm.var.annotation would take the slot's address (RS25 as amended by
//   section 12's row '5.21.4 RS25 amended' (2026-11-21); 'annotate' on a
//   function whose type has a reference is T-R16, msil-ref-functions.c).
//   A variable whose type is deduced from its initializer gets the same:
//   '__auto_type' with each of the four, and on the -std=c23 lines C23 'auto'
//   with 'cleanup' and 'annotate' (RS25 as amended by section 12's row
//   '5.21.4 RS25 and 5.21.13's clang/lib/Sema/SemaDecl.cpp row amended'
//   (2026-11-21)). A parameter without a name ('__block' and 'annotate' on a
//   prototype's parameter) is named by its type as clang prints a type, and a
//   named one by its name (section 12's row '5.21.12 T-R20's <v> as built'
//   (2026-11-21)).
// - RS26: T-R21 for a capture in a block (-fblocks), in a captured statement
//   ('#pragma clang __debug captured') and in an OpenMP region (-fopenmp:
//   'parallel' and 'task'); a block, captured statement or OpenMP region
//   that uses no reference of the enclosing function compiles.
// Every text is pinned whole ('{{^}}' text '{{$}}').

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple msil32-unknown-dotnet -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fblocks -fsyntax-only -verify=expected,blocks %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fblocks -fsyntax-only -verify=expected,blocks %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fblocks -fsyntax-only -verify=expected,blocks %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fopenmp -fsyntax-only -verify=expected,omp %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fopenmp -fsyntax-only -verify=expected,omp %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c23 -fsyntax-only -verify=expected,c23 %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c23 -fsyntax-only -verify=expected,c23 %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c23 -fsyntax-only -verify=expected,c23 %s

#define CLI_TYPE(N) __attribute__((cli_type(N)))
#define NULL ((void *)0)

typedef struct CLI_TYPE("System.String") String String;
typedef struct CLI_TYPE("System.Object") Object Object;
typedef String ^SRef;

//===----------------------------------------------------------------------===//
// RS13: T-R9.
//===----------------------------------------------------------------------===//

static int addr(String ^s, Object ^const o) {
  String ^l;
  SRef t = s;
  int r = 0;
  (void)&l;                      // expected-error-re {{{{^}}taking the address of the .NET reference 'l' is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
  (void)&s;                      // expected-error-re {{{{^}}taking the address of the .NET reference 's' is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
  (void)&o;                      // expected-error-re {{{{^}}taking the address of the .NET reference 'o' is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
  (void)&t;                      // expected-error-re {{{{^}}taking the address of the .NET reference 't' is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
  (void)&(l);                    // expected-error-re {{{{^}}taking the address of the .NET reference 'l' is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
  r += sizeof(&l);               // expected-error-re {{{{^}}taking the address of the .NET reference 'l' is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
  void *p = &l;                  // expected-error-re {{{{^}}taking the address of the .NET reference 'l' is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
  (void)__builtin_addressof(l);  // expected-error-re {{{{^}}taking the address of the .NET reference 'l' is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
  (void)__builtin_addressof(s);  // expected-error-re {{{{^}}taking the address of the .NET reference 's' is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
  return r;
}

//===----------------------------------------------------------------------===//
// RS14: storage, by the sizeless rules.
//===----------------------------------------------------------------------===//

struct Field { String ^f; };                      // expected-error-re {{{{^}}field has sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
union Member { String ^u; int i; };               // expected-error-re {{{{^}}field has sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
struct BitField { String ^b : 1; };               // expected-error-re {{{{^}}field has sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
struct Anonymous { struct { SRef inner; }; int x; }; // expected-error-re {{{{^}}field has sizeless type 'SRef' (aka 'System.String ^'){{$}}}}
struct Flexible { int n; String ^tail[]; };       // expected-error-re {{{{^}}array has sizeless element type 'String ^' (aka 'System.String ^'){{$}}}}
typedef String ^Array2[2];                        // expected-error-re {{{{^}}array has sizeless element type 'String ^' (aka 'System.String ^'){{$}}}}
static void array_param(String ^a[]);            // expected-error-re {{{{^}}array has sizeless element type 'String ^' (aka 'System.String ^'){{$}}}}
static void array_param2(Object ^a[4]);          // expected-error-re {{{{^}}array has sizeless element type 'Object ^' (aka 'System.Object ^'){{$}}}}
String ^g_definition = NULL;                      // expected-error-re {{{{^}}non-local variable with sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
String ^g_tentative;                              // expected-error-re {{{{^}}non-local variable with sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
extern String ^g_extern;                          // expected-error-re {{{{^}}non-local variable with sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
static Object ^g_static;                          // expected-error-re {{{{^}}non-local variable with sizeless type 'Object ^' (aka 'System.Object ^'){{$}}}}
_Thread_local String ^g_thread;                   // expected-error-re {{{{^}}non-local variable with sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
static SRef const g_const;                        // expected-error-re {{{{^}}non-local variable with sizeless type 'const SRef' (aka 'System.String ^const'){{$}}}}

static int storage(String ^s, int n) {
  String ^arr[2];                                 // expected-error-re {{{{^}}array has sizeless element type 'String ^' (aka 'System.String ^'){{$}}}}
  String ^arr2[2][3];                             // expected-error-re {{{{^}}array has sizeless element type 'String ^' (aka 'System.String ^'){{$}}}}
  String ^vla[n];                                 // expected-error-re {{{{^}}array has sizeless element type 'String ^' (aka 'System.String ^'){{$}}}}
  static String ^l_static;                        // expected-error-re {{{{^}}non-local variable with sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
  extern String ^l_extern;                        // expected-error-re {{{{^}}non-local variable with sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
  static _Thread_local String ^l_thread;          // expected-error-re {{{{^}}non-local variable with sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
  String ^al1 __attribute__((aligned(16)));       // expected-error-re {{{{^}}'aligned' attribute cannot be applied to sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
  __attribute__((aligned)) String ^al2;           // expected-error-re {{{{^}}'aligned' attribute cannot be applied to sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
  _Alignas(16) String ^al3;                       // expected-error-re {{{{^}}'_Alignas' attribute cannot be applied to sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
  return s == NULL;
}

//===----------------------------------------------------------------------===//
// RS25: T-R20.
//===----------------------------------------------------------------------===//

static void release(void *slot);
static void reference_attributes(String ^s) {
  String ^c __attribute__((cleanup(release))) = s;   // expected-error-re {{{{^}}'cleanup' on the .NET reference 'c' is not supported for MSIL{{$}}}}
  String ^u __attribute__((uninitialized));          // expected-error-re {{{{^}}'uninitialized' on the .NET reference 'u' is not supported for MSIL{{$}}}}
  SRef u2 __attribute__((uninitialized));            // expected-error-re {{{{^}}'uninitialized' on the .NET reference 'u2' is not supported for MSIL{{$}}}}
  int i __attribute__((cleanup(release), uninitialized));
#ifdef __BLOCKS__
  __block String ^b;                                 // blocks-error-re {{{{^}}'__block' on the .NET reference 'b' is not supported for MSIL{{$}}}}
  __block int bi = 0;
#endif
  (void)c; (void)u; (void)u2; (void)i;
}

static int annotated_local(String ^a) {
  String ^s __attribute__((annotate("note"))) = a;   // expected-error-re {{{{^}}'annotate' on the .NET reference 's' is not supported for MSIL{{$}}}}
  int i __attribute__((annotate("note"))) = 0;
  return (s == NULL) + i;
}

static int annotated_prefix(String ^a) {
  __attribute__((annotate("x"))) String ^s = a;      // expected-error-re {{{{^}}'annotate' on the .NET reference 's' is not supported for MSIL{{$}}}}
  return s == NULL;
}

static int annotated_parameter(String ^a __attribute__((annotate("p")))) { // expected-error-re {{{{^}}'annotate' on the .NET reference 'a' is not supported for MSIL{{$}}}}
  return a == NULL;
}

// A parameter without a name is named by its type.
#ifdef __BLOCKS__
static int ub(__block String ^);                     // blocks-error-re {{{{^}}'__block' on the .NET reference 'String ^' (aka 'System.String ^') is not supported for MSIL{{$}}}}
static int ubp(__block String ^p);                   // blocks-error-re {{{{^}}'__block' on the .NET reference 'p' is not supported for MSIL{{$}}}}
#endif
static int ua(__attribute__((annotate("p"))) String ^); // expected-error-re {{{{^}}'annotate' on the .NET reference 'String ^' (aka 'System.String ^') is not supported for MSIL{{$}}}}

// A variable whose type is deduced from its initializer.
static int deduced(String ^a) {
  int r = 0;
  {
    __auto_type s __attribute__((cleanup(release))) = a; // expected-error-re {{{{^}}'cleanup' on the .NET reference 's' is not supported for MSIL{{$}}}}
    r += s == NULL;
  }
  {
    __auto_type s __attribute__((annotate("note"))) = a; // expected-error-re {{{{^}}'annotate' on the .NET reference 's' is not supported for MSIL{{$}}}}
    r += s == NULL;
  }
  {
    __auto_type s __attribute__((uninitialized)) = a;    // expected-error-re {{{{^}}'uninitialized' on the .NET reference 's' is not supported for MSIL{{$}}}}
    r += s == NULL;
  }
#ifdef __BLOCKS__
  {
    __block __auto_type s = a;                           // blocks-error-re {{{{^}}'__block' on the .NET reference 's' is not supported for MSIL{{$}}}}
    r += s == NULL;
  }
#endif
#if __STDC_VERSION__ >= 202311L
  {
    auto s __attribute__((cleanup(release))) = a;        // c23-error-re {{{{^}}'cleanup' on the .NET reference 's' is not supported for MSIL{{$}}}}
    r += s == NULL;
  }
  {
    auto s __attribute__((annotate("note"))) = a;        // c23-error-re {{{{^}}'annotate' on the .NET reference 's' is not supported for MSIL{{$}}}}
    r += s == NULL;
  }
#endif
  return r;
}

//===----------------------------------------------------------------------===//
// RS26: T-R21.
//===----------------------------------------------------------------------===//

static int captures(String ^s, Object ^o, int n) {
  int r = 0;
#ifdef __BLOCKS__
  int (^b1)(void) = ^{ return s == NULL; };         // blocks-error-re {{{{^}}capturing the .NET reference 's' in a block is not supported for MSIL{{$}}}}
  int (^b2)(void) = ^{ return o != NULL; };         // blocks-error-re {{{{^}}capturing the .NET reference 'o' in a block is not supported for MSIL{{$}}}}
  int (^b3)(int) = ^(int x) { return x + n; };
  r += b3(1);
#endif
  #pragma clang __debug captured
  {
    r += s == NULL;                                 // expected-error-re {{{{^}}capturing the .NET reference 's' in a captured statement is not supported for MSIL{{$}}}}
  }
  #pragma clang __debug captured
  {
    r += n;
  }
#ifdef _OPENMP
  #pragma omp parallel
  {
    (void)(o == NULL);                              // omp-error-re {{{{^}}capturing the .NET reference 'o' in an OpenMP region is not supported for MSIL{{$}}}}
  }
  #pragma omp task
  {
    (void)(s == NULL);                              // omp-error-re {{{{^}}capturing the .NET reference 's' in an OpenMP region is not supported for MSIL{{$}}}}
  }
  #pragma omp parallel
  {
    r += n;
  }
#endif
  return r;
}
