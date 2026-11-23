// Functions with .NET references (ImplementationPlan.md 5.21.4 RS15-RS22 and
// 5.21.14 RV6, milestone R-a). In the first slice a reference is a parameter
// or the result only of a static, prototyped, non-variadic function whose
// address is never taken (RS3), or of a managed call; everything else that
// would let another party call the function, or call it through something
// other than a direct call, is refused.
// - RS15: T-R10 for external linkage, naming the first reference parameter
//   or else the result: declarations, 'extern', definitions, C99 'inline'
//   definitions, a block-scope declaration and a declaration through a
//   function typedef. A static function, a later redeclaration of it without
//   'static' and a managed call with external linkage are accepted.
// - RS16: T-R11 for every decay outside a direct call: '&f', a cast, an
//   argument, '(&f)(r)', '(*f)(r)', __builtin_function_start, a '?:' and an
//   equality operand; a direct call, also parenthesized, is accepted, and so
//   is a static function declared through a function typedef. A managed call
//   with a reference is no exception (RM5).
// - RS17: T-R12 for a function pointer type with a reference: a variable, a
//   pointer parameter, both adjusted parameter forms ('void h(String
//   ^cb(String ^))' and 'typedef String ^F(String ^); void h(F cb);'), a
//   typedef, a result, a field, a cast, an array; with -fblocks (the BLOCKS
//   runs) block pointers and block literals; msil-ref-type.c pins the block
//   pointer without -fblocks.
// - RS18: T-R13 for a declaration without a prototype ('()' in C17; in C23
//   '()' is a prototype, the C23 runs); the KR runs pin K&R definitions that
//   take or return a reference.
// - RS19: clang's err_cannot_pass_to_vararg whole (also its format-string
//   form), T-R14 for an argument to an unprototyped function (with clang's
//   deprecation warning) and for 'va_arg' of a reference.
// - RS20: T-R15 for a variadic function with a reference.
// - RS21: T-R16 for 'constructor', 'destructor', 'weak', 'weakref',
//   'annotate' and 'alias' on a function whose type has a reference (the WIN
//   runs add 'dllexport' in both spellings, the IFUNC runs 'ifunc' on the
//   aarch64 Windows triples, where clang knows it), also on managed calls
//   (RM5); 'weakref' (RS21 as amended) whether or not the unit defines its
//   target, and with 'alias' each of the two gets the text. The alias that
//   '#pragma weak <alias> = <f>' declares (section 12's row '5.21.4 RS21 as
//   built for `#pragma weak <alias> = <f>`') gets T-R16 for 'weak' on
//   <alias> once, at the pragma's first identifier, and the pragma then
//   declares no alias, so the name stays free: after <f>'s definition, after
//   its declaration and before its definition, before an external
//   declaration of <f>, which the pragma waits for and which gets its own
//   T-R10, and for a managed call; the pragma for a function without a
//   reference gets no text. 'used' is accepted, and so is 'weakref' on a
//   function without a reference. An 'alias' whose own type has no
//   reference takes no address in the AST: the ALIAS runs compile it to an
//   object with cc1's -emit-obj (the Sema directory forbids the driver) and
//   the back end refuses the aliasee with T-RB2 (5.21.7 RB11 rule 4).
// - RS22: T-R17 with its note at the first reference (the result, else the
//   first reference parameter, else the body) for a function that calls a
//   returns_twice function ('setjmp', '__builtin_setjmp' on the x86_64
//   triples, the only ones of these where clang has it, and a declaration
//   with the attribute), once per function; a setjmp caller without
//   references compiles. A declared '_setjmp3' gets no T-R17 (clang marks it
//   returns_twice only where it lowers '_setjmp' to it, on 32-bit x86), but
//   P8 lowers its calls by name, so the back end refuses each caller that has
//   a reference with T-RB3 (5.21.7 RB11 rule 8 as amended): the SETJMP3 runs
//   (a prototype with '...') and the SETJMP3_KR runs (no prototype, C17, so
//   that each call has a function type of its own) compile a function with a
//   reference local and a static function with a reference parameter that
//   call it to an object at -O0, as the ALIAS runs do (at -O0 nothing is
//   inlined, so each function reaches the back end as written).
// Every text is pinned whole ('{{^}}' text '{{$}}').

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify=expected,c17 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -std=c17 -fsyntax-only -verify=expected,c17 %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify=expected,c17 %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -std=c17 -fsyntax-only -verify=expected,c17 %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -verify=expected,c17 %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -std=c17 -fsyntax-only -verify=expected,c17 %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -std=c17 -fsyntax-only -verify=expected,c17 %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c17 -fsyntax-only -verify=expected,c17 %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -verify=expected,c17 %s
// RUN: %clang_cc1 -triple msil32-unknown-dotnet -std=c17 -fsyntax-only -verify=expected,c17 %s
// RS19: passing a reference to '...' stays a hard error with -Wno-error and
// without clang's non-POD vararg warning (which would compile a trap).
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -Wno-error -Wno-non-pod-varargs -fsyntax-only -verify=expected,c17 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c23 -fsyntax-only -verify=expected,c23 %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c23 -fsyntax-only -verify=expected,c23 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fblocks -fsyntax-only -DBLOCKS -verify=blocks %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fblocks -fsyntax-only -DBLOCKS -verify=blocks %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fblocks -fsyntax-only -DBLOCKS -verify=blocks %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -Wno-deprecated-non-prototype -fsyntax-only -DKR -verify=kr %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=gnu89 -Wno-deprecated-non-prototype -fsyntax-only -DKR -verify=kr %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fms-extensions -fsyntax-only -DWIN -verify=win,win-declspec %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -std=c17 -fms-extensions -fsyntax-only -DWIN -verify=win,win-declspec %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -std=c17 -fsyntax-only -DWIN -verify=win,win-declspec %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c17 -fsyntax-only -DWIN -verify=win,win-declspec %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -std=c17 -fsyntax-only -DIFUNC -verify=ifunc %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c17 -fsyntax-only -DIFUNC -verify=ifunc %s
// RUN: %if msil-registered-target %{ not %clang_cc1 -triple x86_64-pc-windows-msvc-msil -Wno-attribute-alias -emit-obj -o %t.win.o -DALIAS %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ALIAS --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" %}
// RUN: %if msil-registered-target %{ not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -Wno-attribute-alias -emit-obj -o %t.lnx.o -DALIAS %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ALIAS --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" %}
// RUN: %if msil-registered-target %{ not %clang_cc1 -triple msil64-unknown-dotnet -Wno-attribute-alias -emit-obj -o %t.p64.o -DALIAS %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ALIAS --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" %}
// RUN: %if msil-registered-target %{ not %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O0 -emit-obj -o %t.sj3.win.o -DSETJMP3 %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SETJMP3 --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" %}
// RUN: %if msil-registered-target %{ not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O0 -emit-obj -o %t.sj3.lnx.o -DSETJMP3 %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SETJMP3 --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" %}
// RUN: %if msil-registered-target %{ not %clang_cc1 -triple msil64-unknown-dotnet -O0 -emit-obj -o %t.sj3.p64.o -DSETJMP3 %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SETJMP3 --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" %}
// RUN: %if msil-registered-target %{ not %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -Wno-deprecated-non-prototype -O0 -emit-obj -o %t.sj3kr.win.o -DSETJMP3_KR %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SETJMP3_KR --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" %}
// RUN: %if msil-registered-target %{ not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -Wno-deprecated-non-prototype -O0 -emit-obj -o %t.sj3kr.lnx.o -DSETJMP3_KR %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SETJMP3_KR --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" %}
// RUN: %if msil-registered-target %{ not %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -Wno-deprecated-non-prototype -O0 -emit-obj -o %t.sj3kr.p64.o -DSETJMP3_KR %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SETJMP3_KR --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" %}

#define CLI_TYPE(N) __attribute__((cli_type(N)))
#define NULL ((void *)0)

typedef struct CLI_TYPE("System.String") String String;
typedef struct CLI_TYPE("System.Object") Object Object;
typedef String ^SRef;

#if defined(ALIAS)

// RS21: the alias's own type has no reference, so Sema sees no address of
// 'aliased'; the back end refuses the aliasee (P1 rule 4) in object mode.
// ALIAS: {{.*}}msil-ref-functions.c:[[@LINE+1]]:{{[0-9]+}}: error: a function with a .NET reference in its signature whose address is taken is not supported for MSIL yet{{$}}
static String ^aliased(String ^s) { return s; }
void alias_of_aliased(void) __attribute__((alias("aliased")));
int alias_user(void) { return aliased(NULL) == NULL; }

#elif defined(SETJMP3)

// RS22: a declared '_setjmp3' has no returns_twice, so Sema is silent; the
// back end refuses each caller that has a reference (P1 rule 8) in object
// mode. It reports in the module's order, where the static function follows
// its caller.
typedef long long jmp_buf[16];
int _setjmp3(jmp_buf, ...);
static jmp_buf sj3_buf;
// SETJMP3: {{.*}}msil-ref-functions.c:[[@LINE+1]]:{{[0-9]+}}: error: a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
int sj3_local(void) {
  Object ^o = NULL;
  return _setjmp3(sj3_buf, 0) + (o == NULL);
}
// SETJMP3: {{.*}}msil-ref-functions.c:[[@LINE+1]]:{{[0-9]+}}: error: a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
static int sj3_param(String ^s) { return _setjmp3(sj3_buf, 0) + (s == NULL); }
int sj3_user(void) { return sj3_param(NULL); }

#elif defined(SETJMP3_KR)

// RS22: the same with '_setjmp3' declared without a prototype (C17), so
// that each call has a function type of its own.
typedef long long jmp_buf[16];
int _setjmp3();
static jmp_buf sjk_buf;
// SETJMP3_KR: {{.*}}msil-ref-functions.c:[[@LINE+1]]:{{[0-9]+}}: error: a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
int sjk_local(void) {
  Object ^o = NULL;
  return _setjmp3(sjk_buf, 0) + (o == NULL);
}
// SETJMP3_KR: {{.*}}msil-ref-functions.c:[[@LINE+1]]:{{[0-9]+}}: error: a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
static int sjk_param(String ^s) { return _setjmp3(sjk_buf, 0) + (s == NULL); }
int sjk_user(void) { return sjk_param(NULL); }

#elif defined(BLOCKS)

// RS17 with -fblocks: block pointers and block literals.
String ^(^b_ptr)(String ^);  // blocks-error-re {{{{^}}a block pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
void (^b_ptr2)(Object ^);    // blocks-error-re {{{{^}}a block pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
SRef (^b_ptr3)(void);        // blocks-error-re {{{{^}}a block pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
static void b_param(int (^cb)(String ^)); // blocks-error-re {{{{^}}a block pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
int (^b_plain)(int);
static void b_literals(void) {
  (void)^(String ^x) { return x; };       // blocks-error-re {{{{^}}a block type with a .NET reference is not supported for MSIL yet{{$}}}}
  (void)^String ^(void) { return NULL; }; // blocks-error-re {{{{^}}a block type with a .NET reference is not supported for MSIL yet{{$}}}}
  (void)^(Object ^o, int i) { return i; }; // blocks-error-re {{{{^}}a block type with a .NET reference is not supported for MSIL yet{{$}}}}
  (void)^(int i) { return i; };
}

#elif defined(KR)

// RS18: a K&R definition has no prototype: T-R13 when it takes or returns a
// reference.
static int kr_takes(s) String ^s; { return s == NULL; }     // kr-error-re {{{{^}}a function without a prototype that takes or returns a .NET reference is not supported for MSIL{{$}}}}
static SRef kr_returns(i) int i; { return NULL; }          // kr-error-re {{{{^}}a function without a prototype that takes or returns a .NET reference is not supported for MSIL{{$}}}}
static int kr_second(i, o) int i; Object ^o; { return o == NULL; } // kr-error-re {{{{^}}a function without a prototype that takes or returns a .NET reference is not supported for MSIL{{$}}}}
static int kr_plain(i) int i; { return i; }

#elif defined(WIN)

// RS21: dllexport, in both spellings, on the Windows triples (T-R16, and
// T-R10 for the external declarations; also on a managed call, RM5).
String ^w_exp1(String ^s) __attribute__((dllexport)); // win-error-re {{{{^}}'dllexport' on 'w_exp1', whose type has a .NET reference, is not supported for MSIL yet{{$}}}} win-error-re {{{{^}}'w_exp1' has external linkage; a .NET reference as parameter 1 is not supported for MSIL yet (declare 'w_exp1' 'static'){{$}}}}
static String ^w_exp2(String ^s) __attribute__((dllexport)); // win-error-re {{{{^}}'dllexport' on 'w_exp2', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
String ^w_mc(String ^s) __attribute__((dllexport, msil_method("System.String", "Copy", "string(string)"))); // win-error-re {{{{^}}'dllexport' on 'w_mc', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
int w_plain(int x) __attribute__((dllexport));
// __declspec: with -fms-extensions on the MSVC triples, a predefined macro on
// the MinGW triples.
__declspec(dllexport) String ^w_exp3(String ^s); // win-declspec-error-re {{{{^}}'dllexport' on 'w_exp3', whose type has a .NET reference, is not supported for MSIL yet{{$}}}} win-declspec-error-re {{{{^}}'w_exp3' has external linkage; a .NET reference as parameter 1 is not supported for MSIL yet (declare 'w_exp3' 'static'){{$}}}}

#elif defined(IFUNC)

static void *resolve(void) { return 0; }
static String ^i_ifunc(String ^s) __attribute__((ifunc("resolve"))); // ifunc-error-re {{{{^}}'ifunc' on 'i_ifunc', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static int i_plain(int x) __attribute__((ifunc("resolve")));

#else // the MSIL triples, C17 and C23

typedef String ^Fn(String ^);

//===----------------------------------------------------------------------===//
// RS3: accepted (no diagnostics).
//===----------------------------------------------------------------------===//

static String ^ok_static(String ^s, Object ^o, int i);
static String ^ok_static2(String ^s);
String ^ok_static2(String ^s); // keeps the internal linkage of its first declaration
static inline String ^ok_inline(String ^s) { return s; }
static Fn ok_typedef_decl;
static String ^ok_typedef_decl(String ^s) { return ok_inline(s); }
String ^ok_managed(String ^s) __attribute__((msil_method("System.String", "Copy", "string(string)")));
static String ^ok_used(String ^s) __attribute__((used));
static String ^ok_used(String ^s) { return s; }
static String ^ok_static(String ^s, Object ^o, int i) {
  if (i)
    return ok_static(s, o, i - 1);
  return (ok_static2)(s) == ok_typedef_decl(s) ? ok_managed(s) : ok_used(s);
}

//===----------------------------------------------------------------------===//
// RS15: T-R10.
//===----------------------------------------------------------------------===//

String ^x_result(void);                    // expected-error-re {{{{^}}'x_result' has external linkage; a .NET reference as its result is not supported for MSIL yet (declare 'x_result' 'static'){{$}}}}
void x_param2(int a, String ^b);           // expected-error-re {{{{^}}'x_param2' has external linkage; a .NET reference as parameter 2 is not supported for MSIL yet (declare 'x_param2' 'static'){{$}}}}
void x_both(String ^a, Object ^b);         // expected-error-re {{{{^}}'x_both' has external linkage; a .NET reference as parameter 1 is not supported for MSIL yet (declare 'x_both' 'static'){{$}}}}
Object ^x_param_first(int a, SRef b);      // expected-error-re {{{{^}}'x_param_first' has external linkage; a .NET reference as parameter 2 is not supported for MSIL yet (declare 'x_param_first' 'static'){{$}}}}
extern void x_extern(String ^s);           // expected-error-re {{{{^}}'x_extern' has external linkage; a .NET reference as parameter 1 is not supported for MSIL yet (declare 'x_extern' 'static'){{$}}}}
String ^x_definition(String ^s) { return s; } // expected-error-re {{{{^}}'x_definition' has external linkage; a .NET reference as parameter 1 is not supported for MSIL yet (declare 'x_definition' 'static'){{$}}}}
inline String ^x_inline(String ^s) { return s; } // expected-error-re {{{{^}}'x_inline' has external linkage; a .NET reference as parameter 1 is not supported for MSIL yet (declare 'x_inline' 'static'){{$}}}}
Fn x_typedef_decl;                         // expected-error-re {{{{^}}'x_typedef_decl' has external linkage; a .NET reference as parameter 1 is not supported for MSIL yet (declare 'x_typedef_decl' 'static'){{$}}}}
static void x_block_scope(void) {
  String ^x_local_decl(void);              // expected-error-re {{{{^}}'x_local_decl' has external linkage; a .NET reference as its result is not supported for MSIL yet (declare 'x_local_decl' 'static'){{$}}}}
}

//===----------------------------------------------------------------------===//
// RS16: T-R11.
//===----------------------------------------------------------------------===//

static void *take_ptr(void *p) { return p; }
static int addresses(String ^s, int c) {
  int r = 0;
  (void)&ok_inline;                        // expected-error-re {{{{^}}taking the address of 'ok_inline', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
  void *p = (void *)ok_inline;             // expected-error-re {{{{^}}taking the address of 'ok_inline', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
  take_ptr(ok_inline);                     // expected-error-re {{{{^}}taking the address of 'ok_inline', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
  (&ok_inline)(s);                         // expected-error-re {{{{^}}taking the address of 'ok_inline', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
  (*ok_inline)(s);                         // expected-error-re {{{{^}}taking the address of 'ok_inline', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
  (void)__builtin_function_start(ok_inline); // expected-error-re {{{{^}}taking the address of 'ok_inline', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
  (void)(c ? ok_inline : ok_inline);       // expected-error-re {{{{^}}taking the address of 'ok_inline', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
  r += ok_inline == ok_inline;             // expected-error-re {{{{^}}taking the address of 'ok_inline', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
  (void)&ok_managed;                       // expected-error-re {{{{^}}taking the address of 'ok_managed', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
  r += ok_inline(s) == (ok_inline)(s);
  return r;
}

//===----------------------------------------------------------------------===//
// RS17: T-R12.
//===----------------------------------------------------------------------===//

String ^(*p_variable)(String ^);           // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
void (*p_void)(Object ^);                  // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
SRef (*p_result)(void);                    // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
String ^(*p_array[2])(String ^);           // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
typedef String ^(*FnPtr)(String ^);        // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
typedef Fn *FnPtr2;                        // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
static void p_param(String ^(*cb)(String ^)); // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
static void p_adjusted(String ^cb(String ^)); // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
static void p_adjusted_typedef(Fn cb);     // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
static String ^(*p_returns(void))(String ^); // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
struct WithCallback { int (*cb)(Object ^); }; // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
static void p_cast(void) {
  (void)(String ^(*)(String ^))0;          // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
  (void)sizeof(void (*)(SRef));            // expected-error-re {{{{^}}a function pointer type with a .NET reference is not supported for MSIL yet{{$}}}}
}
int (*p_plain)(int);

//===----------------------------------------------------------------------===//
// RS18: T-R13 (in C17; '()' is a prototype in C23).
//===----------------------------------------------------------------------===//

static String ^np_result();                // c17-error-re {{{{^}}a function without a prototype that takes or returns a .NET reference is not supported for MSIL{{$}}}}
static SRef np_typedef_result();           // c17-error-re {{{{^}}a function without a prototype that takes or returns a .NET reference is not supported for MSIL{{$}}}}
static int np_plain();

//===----------------------------------------------------------------------===//
// RS19: variadic arguments, unprototyped arguments and va_arg.
//===----------------------------------------------------------------------===//

static int v_variadic(int n, ...);
static int v_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
static int v_unprototyped();
#endif
static int variadic_args(String ^s, Object ^o, __builtin_va_list ap) {
  int r = 0;
  r += v_variadic(1, s);                   // expected-error-re {{{{^}}cannot pass expression of type 'String ^' (aka 'System.String ^') to variadic function{{$}}}}
  r += v_variadic(2, 0, o);                // expected-error-re {{{{^}}cannot pass expression of type 'Object ^' (aka 'System.Object ^') to variadic function{{$}}}}
  r += v_printf("%p", s);                  // expected-error-re {{{{^}}cannot pass expression of type 'String ^' (aka 'System.String ^') to variadic function; expected type from format string was 'void *'{{$}}}}
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
  // c17-warning-re@+2 {{{{^}}passing arguments to 'v_unprototyped' without a prototype is deprecated in all versions of C and is not supported in C23{{$}}}}
  // c17-error-re@+1 {{{{^}}passing the .NET reference type 'String ^' (aka 'System.String ^') to a function without a prototype is not supported for MSIL{{$}}}}
  r += v_unprototyped(s);
#endif
  String ^x = __builtin_va_arg(ap, String ^); // expected-error-re {{{{^}}'va_arg' of the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL{{$}}}}
  SRef y = __builtin_va_arg(ap, SRef);     // expected-error-re {{{{^}}'va_arg' of the .NET reference type 'SRef' (aka 'System.String ^') is not supported for MSIL{{$}}}}
  r += v_variadic(1, 2, 3);
  return r;
}

//===----------------------------------------------------------------------===//
// RS20: T-R15.
//===----------------------------------------------------------------------===//

static String ^vf_result(int n, ...);      // expected-error-re {{{{^}}a variadic function that takes or returns a .NET reference is not supported for MSIL yet{{$}}}}
static void vf_param(String ^s, ...);      // expected-error-re {{{{^}}a variadic function that takes or returns a .NET reference is not supported for MSIL yet{{$}}}}
static SRef vf_format(const char *fmt, ...); // expected-error-re {{{{^}}a variadic function that takes or returns a .NET reference is not supported for MSIL yet{{$}}}}

//===----------------------------------------------------------------------===//
// RS21: T-R16.
//===----------------------------------------------------------------------===//

static String ^a_ctor(String ^s) __attribute__((constructor));      // expected-error-re {{{{^}}'constructor' on 'a_ctor', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static void a_ctor_ref(Object ^o) __attribute__((constructor(101))); // expected-error-re {{{{^}}'constructor' on 'a_ctor_ref', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static String ^a_dtor(void) __attribute__((destructor));            // expected-error-re {{{{^}}'destructor' on 'a_dtor', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static String ^a_weak(String ^s) __attribute__((weak));             // expected-error-re {{{{^}}'weak' on 'a_weak', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static String ^a_weakref(String ^s) __attribute__((weakref("a_weakref_target"))); // expected-error-re {{{{^}}'weakref' on 'a_weakref', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static String ^a_weakref_def(String ^s) __attribute__((weakref("ok_used"))); // expected-error-re {{{{^}}'weakref' on 'a_weakref_def', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static String ^a_annotate(String ^s) __attribute__((annotate("tag"))); // expected-error-re {{{{^}}'annotate' on 'a_annotate', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static String ^a_annotate_def(String ^s) __attribute__((annotate("tag"))) { return s; } // expected-error-re {{{{^}}'annotate' on 'a_annotate_def', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static String ^a_alias(String ^s) __attribute__((alias("ok_used"))); // expected-error-re {{{{^}}'alias' on 'a_alias', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static String ^a_weakref_alias(String ^s) __attribute__((weakref, alias("ok_used"))); // expected-error-re {{{{^}}'weakref' on 'a_weakref_alias', whose type has a .NET reference, is not supported for MSIL yet{{$}}}} expected-error-re {{{{^}}'alias' on 'a_weakref_alias', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static String ^a_alias_weakref(String ^s) __attribute__((alias("ok_used"), weakref)); // expected-error-re {{{{^}}'alias' on 'a_alias_weakref', whose type has a .NET reference, is not supported for MSIL yet{{$}}}} expected-error-re {{{{^}}'weakref' on 'a_alias_weakref', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
String ^a_mc_ctor(String ^s) __attribute__((constructor, msil_method("System.String", "Copy", "string(string)"))); // expected-error-re {{{{^}}'constructor' on 'a_mc_ctor', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static String ^a_mc_dtor(String ^s) __attribute__((msil_method("System.String", "Copy", "string(string)"), destructor)); // expected-error-re {{{{^}}'destructor' on 'a_mc_dtor', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static void a_plain_ctor(void) __attribute__((constructor));
static void a_plain_ctor(void) {}
static int a_plain_weakref(int i) __attribute__((weakref("a_plain_target")));

// '#pragma weak <alias> = <f>': T-R16 at <alias>, and no alias, so that a
// variable may take its name.
static String ^pw_def(String ^s) { return s; }
#pragma weak pw_alias_def = pw_def // expected-error-re {{{{^}}'weak' on 'pw_alias_def', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
int pw_alias_def;
static String ^pw_decl(String ^s);
#pragma weak pw_alias_decl = pw_decl // expected-error-re {{{{^}}'weak' on 'pw_alias_decl', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static String ^pw_decl(String ^s) { return s; }
#pragma weak pw_alias_ext = pw_ext // expected-error-re {{{{^}}'weak' on 'pw_alias_ext', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
String ^pw_ext(String ^s);         // expected-error-re {{{{^}}'pw_ext' has external linkage; a .NET reference as parameter 1 is not supported for MSIL yet (declare 'pw_ext' 'static'){{$}}}}
int pw_alias_ext;
#pragma weak pw_alias_mc = ok_managed // expected-error-re {{{{^}}'weak' on 'pw_alias_mc', whose type has a .NET reference, is not supported for MSIL yet{{$}}}}
static int pw_plain(int x) { return x; }
#pragma weak pw_alias_plain = pw_plain

//===----------------------------------------------------------------------===//
// RS22: T-R17.
//===----------------------------------------------------------------------===//

typedef long long jmp_buf[16];
int setjmp(jmp_buf);
int my_setjmp(void *env) __attribute__((returns_twice));
static int sj_param(int i, String ^s) {    // expected-note-re {{{{^}}a .NET reference is used here{{$}}}}
  jmp_buf b;
  if (setjmp(b))                           // expected-error-re {{{{^}}a .NET reference in a function that calls 'setjmp' is not supported for MSIL yet{{$}}}}
    return 1;
  return i;
}
static String ^sj_result(void) {           // expected-note-re {{{{^}}a .NET reference is used here{{$}}}}
  char b[8];
  if (my_setjmp(b))                        // expected-error-re {{{{^}}a .NET reference in a function that calls 'my_setjmp' is not supported for MSIL yet{{$}}}}
    return NULL;
  return NULL;
}
static int sj_local(void) {
  char b[8];
  int r = 0;
  String ^l = NULL;                        // expected-note-re {{{{^}}a .NET reference is used here{{$}}}}
  if (my_setjmp(b))                        // expected-error-re {{{{^}}a .NET reference in a function that calls 'my_setjmp' is not supported for MSIL yet{{$}}}}
    r = 1;
  return r + (l == NULL);
}
#if defined(__x86_64__) // clang has __builtin_setjmp for x86_64 only of these triples
static int sj_builtin(Object ^o) {         // expected-note-re {{{{^}}a .NET reference is used here{{$}}}}
  void *b[5];
  if (__builtin_setjmp(b))                 // expected-error-re {{{{^}}a .NET reference in a function that calls '__builtin_setjmp' is not supported for MSIL yet{{$}}}}
    return 1;
  return 0;
}
#endif
static int sj_twice(String ^s) {           // expected-note-re {{{{^}}a .NET reference is used here{{$}}}}
  char b[8];
  my_setjmp(b);                            // expected-error-re {{{{^}}a .NET reference in a function that calls 'my_setjmp' is not supported for MSIL yet{{$}}}}
  my_setjmp(b);
  return 0;
}
static int sj_expression(void) {
  char b[8];
  if (my_setjmp(b))                        // expected-error-re {{{{^}}a .NET reference in a function that calls 'my_setjmp' is not supported for MSIL yet{{$}}}}
    return 1;
  return ok_managed(NULL) == NULL;         // expected-note-re {{{{^}}a .NET reference is used here{{$}}}}
}
static int sj_no_reference(int x) {
  jmp_buf b;
  char c[8];
  return setjmp(b) + my_setjmp(c) + x;
}

#endif
