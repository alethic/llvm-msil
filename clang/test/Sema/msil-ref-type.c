// The reference type 'T ^' in C (ImplementationPlan.md 5.21.3 RC1-RC8, 5.21.4
// RS10-RS12, RS23, RS24 and 5.21.14 RV3, milestone R-a): the caret over a
// struct with 'cli_type' (msil-cli-type.c) is a .NET reference, a clang type of
// its own (CLIReferenceType), never Apple's block pointer.
// - RC1, RC2: the caret builds a reference in every declarator position
//   (locals, register locals, parameters and results of static functions,
//   typedefs, casts, sizeof's and __typeof__'s operands), with its own
//   qualifiers after it ('String ^const').
// - T-R4 for a caret over anything else that is not a function type (int, a
//   struct or union without the attribute, an enum, void, a pointer, another
//   reference, a typedef of int, with clang's aka text), with and without
//   -fblocks: it replaces the block error and its -fblocks advice. A function
//   pointee stays a block pointer (RC2, RC7): refused with clang's
//   'blocks support disabled' text without -fblocks (the noblocks prefix),
//   accepted with it (the BLOCKS runs); a block pointer whose function type
//   has a reference is T-R12 in both modes (msil-ref-functions.c covers the
//   other T-R12 forms).
// - Native triples keep clang's block errors and ignore the attribute (the
//   NATIVE runs: '^' over a struct is 'block pointer to non-function type').
// - RS10: T-R6 for a pointer to a class struct, named by its C spelling
//   (typedef or 'struct Stream'), also through a typedef that hides a
//   qualifier, as a parameter, a result, a cast and sizeof's operand.
// - RS11: T-R7 for the class struct by value (RV3's three forms 'extern
//   String g;', 'String f(void);', 'void f(String a);', a static function's
//   result and parameter, a local).
// - RS12: T-R8 for a pointer to a reference (also through the typedefs
//   'SRef' and 'CSRef', a pointer to pointer, a parameter, a result, a cast
//   and sizeof's operand).
// - RS23: T-R18 for 'volatile', address spaces and (the MS runs,
//   -fms-extensions) '__unaligned' on a reference, also through a typedef;
//   'restrict', '_Atomic' and nullability keep clang's texts.
// - RS24: T-R19 for a qualifier on the class ('const String ^', 'String const
//   ^', 'volatile', 'const volatile', '__unaligned', a qualifier hidden in a
//   typedef of the class).
// - aka: every diagnostic names the reference as written and, in clang's aka
//   text, by its display name (RC4): 'System.String ^', 'string[] ^' for two
//   spellings of one array, 'Ns.Outer/Inner ^', 'class System.IO.Stream[] ^',
//   'uint8[] ^'.
// - RC6: T-R5 in Objective-C (also with -fblocks), Objective-C++, OpenCL,
//   CUDA and HIP (the OTHER runs); C++ is SemaCXX/msil-ref-cxx.cpp.
// - The DUMP run (-ast-dump): the sugared node over the class as written and
//   its canonical node over the implicit struct, the parameters' source
//   ranges (the caret's TypeLoc), a local's NULL start (an
//   ImplicitValueInitExpr, RS2), and the casts' kinds. The PRINT run
//   (-ast-print): declarations, qualifiers after the caret and casts print as
//   written.
// Every text is pinned whole ('{{^}}' text '{{$}}').

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -std=c17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -std=c17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -std=c17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -std=c17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple msil32-unknown-dotnet -std=c17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fblocks -fsyntax-only -verify=expected,blocks %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fblocks -fsyntax-only -verify=expected,blocks %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fblocks -fsyntax-only -verify=expected,blocks %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fms-extensions -fsyntax-only -DMS -verify=ms %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -std=c17 -fms-extensions -fsyntax-only -DMS -verify=ms %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c17 -fsyntax-only -DNATIVE -verify=native,native-noblocks %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c17 -fblocks -fsyntax-only -DNATIVE -verify=native %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -x objective-c -fsyntax-only -DOTHER -verify=objc %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -x objective-c -fblocks -fsyntax-only -DOTHER -verify=objc %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -x objective-c++ -fsyntax-only -DOTHER -verify=objc %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -x cl -fsyntax-only -DOTHER -verify=ocl %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -x cl -cl-std=CL2.0 -fsyntax-only -DOTHER -verify=ocl %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -x cuda -fsyntax-only -DOTHER -verify=cuda %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -x hip -fsyntax-only -DOTHER -verify=cuda %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -DDUMP -ast-dump %s \
// RUN:   | FileCheck %s --check-prefix=DUMP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -DDUMP -ast-print %s \
// RUN:   | FileCheck %s --check-prefix=PRINT

#define CLI_TYPE(N) __attribute__((cli_type(N)))

#if defined(NATIVE)

// The attribute is unknown and the caret is a block pointer.
typedef struct CLI_TYPE("System.String") String String; // native-warning-re {{{{^}}unknown attribute 'cli_type' ignored{{$}}}}
// native-noblocks-error-re@+2 {{{{^}}blocks support disabled - compile with -fblocks or pick a deployment target that supports them{{$}}}}
// native-error-re@+1 {{{{^}}block pointer to non-function type is invalid{{$}}}}
static void n_local(String ^s);
void (^n_block)(void); // native-noblocks-error-re {{{{^}}blocks support disabled - compile with -fblocks or pick a deployment target that supports them{{$}}}}

#elif defined(OTHER)

// RC6: T-R5, permanent outside C and C++.
typedef struct CLI_TYPE("System.String") String String;
// objc-error-re@+3 {{{{^}}.NET references in Objective-C are not supported for MSIL{{$}}}}
// ocl-error-re@+2 {{{{^}}.NET references in OpenCL are not supported for MSIL{{$}}}}
// cuda-error-re@+1 {{{{^}}.NET references in CUDA are not supported for MSIL{{$}}}}
static void o_param(String ^s);
// objc-error-re@+3 {{{{^}}.NET references in Objective-C are not supported for MSIL{{$}}}}
// ocl-error-re@+2 {{{{^}}.NET references in OpenCL are not supported for MSIL{{$}}}}
// cuda-error-re@+1 {{{{^}}.NET references in CUDA are not supported for MSIL{{$}}}}
String ^o_global;

#elif defined(MS)

typedef struct CLI_TYPE("System.String") String String;
typedef String ^SRef;
static void m_unaligned(void) {
  String ^__unaligned u1;  // ms-error-re {{{{^}}'__unaligned' on the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL{{$}}}}
  __unaligned SRef u2;     // ms-error-re {{{{^}}'__unaligned' on the .NET reference type 'SRef' (aka 'System.String ^') is not supported for MSIL{{$}}}}
  __unaligned String ^u3;  // ms-error-re {{{{^}}'__unaligned' on the .NET class 'String' is not supported for MSIL; a reference's own qualifiers follow '^', as in 'String ^const'{{$}}}}
}

#elif defined(DUMP)

typedef struct CLI_TYPE("System.String") String String;
typedef struct CLI_TYPE("System.Object") Object Object;
typedef String ^SRef;
typedef String ^const CSRef;

// DUMP:      TypedefDecl {{.*}} SRef 'String ^':'System.String ^'{{$}}
// DUMP-NEXT: `-CLIReferenceType {{.*}} 'String ^' sugar{{$}}
// DUMP-NEXT:   |-TypedefType {{.*}} 'String' sugar{{$}}
// DUMP-NEXT:   | |-Typedef {{.*}} 'String'{{$}}
// DUMP-NEXT:   | `-RecordType {{.*}} 'struct String' owns_tag struct{{$}}
// DUMP-NEXT:   |   `-Record {{.*}} 'String'{{$}}
// DUMP-NEXT:   `-CLIReferenceType {{.*}} 'System.String ^'{{$}}
// DUMP-NEXT:     `-RecordType {{.*}} 'struct System.String' canonical{{$}}
// DUMP-NEXT:       `-Record {{.*}} 'System.String'{{$}}
// DUMP:      TypedefDecl {{.*}} CSRef 'String ^const':'System.String ^const'{{$}}
// DUMP-NEXT: `-QualType {{.*}} 'String ^const' const{{$}}
// DUMP-NEXT:   `-CLIReferenceType {{.*}} 'String ^' sugar{{$}}

// The parameters' ranges start at the class's name and end at the
// declarator's name: the caret's TypeLoc is filled.
// DUMP:      FunctionDecl {{.*}} d_func 'String ^(String ^, struct String ^, SRef)' static
// DUMP-NEXT: |-ParmVarDecl {{.*}} <col:23, col:31> col:31 used a 'String ^':'System.String ^'{{$}}
// DUMP-NEXT: |-ParmVarDecl {{.*}} <col:34, col:49> col:49 used b 'struct String ^':'System.String ^'{{$}}
// DUMP-NEXT: |-ParmVarDecl {{.*}} <col:52, col:57> col:57 c 'SRef':'System.String ^'{{$}}
// A local without an initializer starts as NULL (RS2).
// DUMP:      VarDecl {{.*}} used r 'String ^':'System.String ^' callinit{{$}}
// DUMP-NEXT: `-ImplicitValueInitExpr {{.*}} 'String ^':'System.String ^'{{$}}
// DUMP:      VarDecl {{.*}} o 'Object ^':'System.Object ^' cinit{{$}}
// DUMP-NEXT: `-ImplicitCastExpr {{.*}} 'Object ^':'System.Object ^' <BitCast>{{$}}
// DUMP:      CStyleCastExpr {{.*}} 'String ^':'System.String ^' <NoOp>{{$}}
// DUMP:      CStyleCastExpr {{.*}} 'String ^':'System.String ^' <BitCast>{{$}}
// DUMP:      CStyleCastExpr {{.*}} 'String ^':'System.String ^' <NullToPointer>{{$}}
static String ^d_func(String ^a, struct String ^b, SRef c) {
  String ^r;
  Object ^o = a;
  r = (String ^)b;
  r = (String ^)o;
  return r == a ? (String ^)0 : r;
}

// PRINT:      {{^}}typedef String ^SRef;{{$}}
// PRINT-NEXT: {{^}}typedef String ^const CSRef;{{$}}
// PRINT-NEXT: {{^}}static String ^d_func(String ^a, struct String ^b, SRef c) {{[{]$}}
// PRINT-NEXT: {{^}}    String ^r(/*implicit*/(String ^)0);{{$}}
// PRINT-NEXT: {{^}}    Object ^o = a;{{$}}
// PRINT-NEXT: {{^}}    r = (String ^)b;{{$}}
// PRINT-NEXT: {{^}}    r = (String ^)o;{{$}}
// PRINT-NEXT: {{^}}    return r == a ? (String ^)0 : r;{{$}}
// PRINT-NEXT: {{^}}}{{$}}

#else // the MSIL triples

typedef struct CLI_TYPE("System.String") String String;
typedef struct CLI_TYPE("System.Object") Object Object;
struct CLI_TYPE("System.IO.Stream") Stream;
typedef struct CLI_TYPE("string[]") Strings Strings;
typedef struct CLI_TYPE("class System.String[]") Strings2 Strings2;
typedef struct CLI_TYPE("Ns.Outer/Inner") Inner Inner;
typedef struct CLI_TYPE("class System.IO.Stream[]") Streams Streams;
typedef struct CLI_TYPE("uint8[]") Bytes Bytes;
struct Plain;
struct Complete { int x; };
union U { int i; };
enum E { e0 };
typedef int Int;
typedef String ^SRef;
typedef String ^const CSRef;
typedef const String CString;

//===----------------------------------------------------------------------===//
// Accepted forms (no diagnostics).
//===----------------------------------------------------------------------===//

// RC1, RC2: the caret in every declarator position of C.
static String ^r_sig(String ^a, struct Stream ^b, Strings ^c, Strings2 ^d, Inner ^e, Streams ^f, Bytes ^g, SRef h, CSRef i);
static Object ^r_unnamed(String ^, Object ^const);
static void r_locals(String ^a) {
  String ^b;
  register String ^c;
  String ^const d = a;
  SRef e;
  CSRef f = a;
  struct Stream ^g;
  String ^h, ^const k = 0, ^l = a;
  __typeof__(a) m;
  __typeof__(String ^const) n = a;
  (void)(String ^)a;
  (void)(struct String ^)a;
  (void)(SRef)a;
  { String ^nested = a; (void)nested; }
  for (String ^loop = a; loop; loop = 0)
    ;
}
// RC5: in an assume_nonnull region pointers get _Nonnull, references get no
// nullability (they are no pointers).
#pragma clang assume_nonnull begin
static String ^r_nonnull(String ^s, int *p, SRef t, String ^const u);
typedef Object ^ORef;
#pragma clang assume_nonnull end

// Two spellings of one array type and of String are one type.
_Static_assert(__builtin_types_compatible_p(Strings ^, Strings2 ^), "one key");
_Static_assert(__builtin_types_compatible_p(SRef, struct String ^const), "one key");

// aka: the display name of each class (RC4) in clang's conversion text.
static void a_aka(String ^s, Strings ^a, Strings2 ^b, Inner ^c, Streams ^d, Bytes ^e, struct Stream ^f, SRef g) {
  int i1 = s; // expected-error-re {{{{^}}initializing 'int' with an expression of incompatible type 'String ^' (aka 'System.String ^'){{$}}}}
  int i2 = a; // expected-error-re {{{{^}}initializing 'int' with an expression of incompatible type 'Strings ^' (aka 'string[] ^'){{$}}}}
  int i3 = b; // expected-error-re {{{{^}}initializing 'int' with an expression of incompatible type 'Strings2 ^' (aka 'string[] ^'){{$}}}}
  int i4 = c; // expected-error-re {{{{^}}initializing 'int' with an expression of incompatible type 'Inner ^' (aka 'Ns.Outer/Inner ^'){{$}}}}
  int i5 = d; // expected-error-re {{{{^}}initializing 'int' with an expression of incompatible type 'Streams ^' (aka 'class System.IO.Stream[] ^'){{$}}}}
  int i6 = e; // expected-error-re {{{{^}}initializing 'int' with an expression of incompatible type 'Bytes ^' (aka 'uint8[] ^'){{$}}}}
  int i7 = f; // expected-error-re {{{{^}}initializing 'int' with an expression of incompatible type 'struct Stream ^' (aka 'System.IO.Stream ^'){{$}}}}
  int i8 = g; // expected-error-re {{{{^}}initializing 'int' with an expression of incompatible type 'SRef' (aka 'System.String ^'){{$}}}}
}

//===----------------------------------------------------------------------===//
// RC2: T-R4, and Blocks.
//===----------------------------------------------------------------------===//

static void t_r4(void) {
  int ^a;               // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'int' is not one{{$}}}}
  struct Plain ^b;      // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'struct Plain' is not one{{$}}}}
  struct Complete ^c;   // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'struct Complete' is not one{{$}}}}
  union U ^d;           // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'union U' is not one{{$}}}}
  enum E ^e;            // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'enum E' is not one{{$}}}}
  Int ^f;               // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'Int' (aka 'int') is not one{{$}}}}
  void ^g;              // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'void' is not one{{$}}}}
  int *^h;              // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'int *' is not one{{$}}}}
  String ^ ^i;          // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'String ^' (aka 'System.String ^') is not one{{$}}}}
  String ^const ^j;     // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'String ^const' (aka 'System.String ^const') is not one{{$}}}}
  SRef ^k;              // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'SRef' (aka 'System.String ^') is not one{{$}}}}
}

// A function pointee is a block pointer: refused without -fblocks, accepted
// with it.
void (^b_block)(void); // noblocks-error-re {{{{^}}blocks support disabled - compile with -fblocks or pick a deployment target that supports them{{$}}}}
int (^b_block2)(int);  // noblocks-error-re {{{{^}}blocks support disabled - compile with -fblocks or pick a deployment target that supports them{{$}}}}
// RC7: with -fblocks both meanings live side by side, told apart by the
// pointee.
static int b_both(String ^s, int (^blk)(int)); // noblocks-error-re {{{{^}}blocks support disabled - compile with -fblocks or pick a deployment target that supports them{{$}}}}
// A block pointer with a reference in its function type: T-R12 in both
// modes.
String ^(^b_ref)(String ^); // expected-error-re {{{{^}}a block pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
void (^b_ref2)(String ^);   // expected-error-re {{{{^}}a block pointer type with a .NET reference is not supported for MSIL yet{{$}}}}

//===----------------------------------------------------------------------===//
// RS10: T-R6.
//===----------------------------------------------------------------------===//

String *p_1;                 // expected-error-re {{{{^}}'String *' points into C memory, which cannot hold the .NET class 'String'; a reference to it is written 'String ^'{{$}}}}
struct Stream *p_2;          // expected-error-re {{{{^}}'struct Stream *' points into C memory, which cannot hold the .NET class 'struct Stream'; a reference to it is written 'struct Stream ^'{{$}}}}
String **p_3;                // expected-error-re {{{{^}}'String *' points into C memory, which cannot hold the .NET class 'String'; a reference to it is written 'String ^'{{$}}}}
const String *p_4;           // expected-error-re {{{{^}}'String *' points into C memory, which cannot hold the .NET class 'String'; a reference to it is written 'String ^'{{$}}}}
String *const p_5;           // expected-error-re {{{{^}}'String *' points into C memory, which cannot hold the .NET class 'String'; a reference to it is written 'String ^'{{$}}}}
CString *p_6;                // expected-error-re {{{{^}}'struct String *' points into C memory, which cannot hold the .NET class 'struct String'; a reference to it is written 'struct String ^'{{$}}}}
Strings *p_7;                // expected-error-re {{{{^}}'Strings *' points into C memory, which cannot hold the .NET class 'Strings'; a reference to it is written 'Strings ^'{{$}}}}
typedef Object *ObjectPtr;   // expected-error-re {{{{^}}'Object *' points into C memory, which cannot hold the .NET class 'Object'; a reference to it is written 'Object ^'{{$}}}}
static String *p_ret(void);  // expected-error-re {{{{^}}'String *' points into C memory, which cannot hold the .NET class 'String'; a reference to it is written 'String ^'{{$}}}}
static void p_par(Object *); // expected-error-re {{{{^}}'Object *' points into C memory, which cannot hold the .NET class 'Object'; a reference to it is written 'Object ^'{{$}}}}
static void p_expr(void) {
  (void)(String *)0;         // expected-error-re {{{{^}}'String *' points into C memory, which cannot hold the .NET class 'String'; a reference to it is written 'String ^'{{$}}}}
  (void)sizeof(Inner *);     // expected-error-re {{{{^}}'Inner *' points into C memory, which cannot hold the .NET class 'Inner'; a reference to it is written 'Inner ^'{{$}}}}
}

//===----------------------------------------------------------------------===//
// RS11: T-R7.
//===----------------------------------------------------------------------===//

extern String v_extern;          // expected-error-re {{{{^}}the .NET class 'String' has no value in C; a reference to it is written 'String ^'{{$}}}}
String v_func(void);             // expected-error-re {{{{^}}the .NET class 'String' has no value in C; a reference to it is written 'String ^'{{$}}}}
void v_param(String a);          // expected-error-re {{{{^}}the .NET class 'String' has no value in C; a reference to it is written 'String ^'{{$}}}}
static Object v_static(void);    // expected-error-re {{{{^}}the .NET class 'Object' has no value in C; a reference to it is written 'Object ^'{{$}}}}
static void v_unnamed(struct Stream); // expected-error-re {{{{^}}the .NET class 'struct Stream' has no value in C; a reference to it is written 'struct Stream ^'{{$}}}}
static void v_local(void) {
  String s;                      // expected-error-re {{{{^}}the .NET class 'String' has no value in C; a reference to it is written 'String ^'{{$}}}}
}

//===----------------------------------------------------------------------===//
// RS12: T-R8.
//===----------------------------------------------------------------------===//

String ^*q_1;                // expected-error-re {{{{^}}a pointer to the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
String ^**q_2;               // expected-error-re {{{{^}}a pointer to the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
SRef *q_3;                   // expected-error-re {{{{^}}a pointer to the .NET reference type 'SRef' (aka 'System.String ^') is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
CSRef *q_4;                  // expected-error-re {{{{^}}a pointer to the .NET reference type 'CSRef' (aka 'System.String ^const') is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
static String ^*q_ret(void); // expected-error-re {{{{^}}a pointer to the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
static void q_par(Strings ^*a); // expected-error-re {{{{^}}a pointer to the .NET reference type 'Strings ^' (aka 'string[] ^') is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
static void q_expr(void) {
  (void)(String ^*)0;        // expected-error-re {{{{^}}a pointer to the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
  (void)sizeof(Object ^*);   // expected-error-re {{{{^}}a pointer to the .NET reference type 'Object ^' (aka 'System.Object ^') is not supported for MSIL: C memory never holds a .NET reference{{$}}}}
}

//===----------------------------------------------------------------------===//
// RS23: T-R18 and clang's texts; RS24: T-R19.
//===----------------------------------------------------------------------===//

static void q_quals(void) {
  String ^volatile v1;          // expected-error-re {{{{^}}'volatile' on the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL{{$}}}}
  volatile SRef v2;             // expected-error-re {{{{^}}'volatile' on the .NET reference type 'SRef' (aka 'System.String ^') is not supported for MSIL{{$}}}}
  SRef volatile v3;             // expected-error-re {{{{^}}'volatile' on the .NET reference type 'SRef' (aka 'System.String ^') is not supported for MSIL{{$}}}}
  String ^const volatile v4;    // expected-error-re {{{{^}}'volatile' on the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL{{$}}}}
  String ^__attribute__((address_space(1))) v5; // expected-error-re {{{{^}}'__attribute__((address_space(1)))' on the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL{{$}}}}
  String ^restrict r1;          // expected-error-re {{{{^}}restrict requires a pointer or reference ('String ^' (aka 'System.String ^') is invalid){{$}}}}
  _Atomic(String ^) a1;         // expected-error-re {{{{^}}_Atomic cannot be applied to sizeless type 'String ^' (aka 'System.String ^') {{$}}}}
  String ^_Atomic a2;           // expected-error-re {{{{^}}_Atomic cannot be applied to sizeless type 'String ^' (aka 'System.String ^') {{$}}}}
  String ^_Nonnull n1;          // expected-error-re {{{{^}}nullability specifier '_Nonnull' cannot be applied to non-pointer type 'String ^' (aka 'System.String ^'){{$}}}}
  String ^_Nullable n2;         // expected-error-re {{{{^}}nullability specifier '_Nullable' cannot be applied to non-pointer type 'String ^' (aka 'System.String ^'){{$}}}}
  String ^_Null_unspecified n3; // expected-error-re {{{{^}}nullability specifier '_Null_unspecified' cannot be applied to non-pointer type 'String ^' (aka 'System.String ^'){{$}}}}
  const String ^c1;             // expected-error-re {{{{^}}'const' on the .NET class 'String' is not supported for MSIL; a reference's own qualifiers follow '^', as in 'String ^const'{{$}}}}
  String const ^c2;             // expected-error-re {{{{^}}'const' on the .NET class 'String' is not supported for MSIL; a reference's own qualifiers follow '^', as in 'String ^const'{{$}}}}
  volatile String ^c3;          // expected-error-re {{{{^}}'volatile' on the .NET class 'String' is not supported for MSIL; a reference's own qualifiers follow '^', as in 'String ^const'{{$}}}}
  const volatile String ^c4;    // expected-error-re {{{{^}}'const volatile' on the .NET class 'String' is not supported for MSIL; a reference's own qualifiers follow '^', as in 'String ^const'{{$}}}}
  const struct Stream ^c5;      // expected-error-re {{{{^}}'const' on the .NET class 'struct Stream' is not supported for MSIL; a reference's own qualifiers follow '^', as in 'struct Stream ^const'{{$}}}}
  CString ^c6;                  // expected-error-re {{{{^}}'const' on the .NET class 'struct String' is not supported for MSIL; a reference's own qualifiers follow '^', as in 'struct String ^const'{{$}}}}
}
static void q_param_quals(const String ^p); // expected-error-re {{{{^}}'const' on the .NET class 'String' is not supported for MSIL; a reference's own qualifiers follow '^', as in 'String ^const'{{$}}}}
static String ^volatile q_ret_quals(void); // expected-error-re {{{{^}}'volatile' on the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL{{$}}}}

#endif
