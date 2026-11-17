// Two managed calls that share one symbol in a translation unit
// (ImplementationPlan.md 5.20.3 T-M24, 5.20.4 MR2 and MR4, as amended in
// section 12, the row '5.20.3's text table gains T-M24 and T-M25, and 5.20.4
// MR4 amended (...)' of 2026-11-06; milestone MC-1). Two managed-call
// declarations of different functions whose IR names are equal
// ('__msil_mc.<s>', <s> from an asm label, from '#pragma redefine_extname' or
// from a C++ asm label equal to another function's mangled name) are one
// managed call only when their '!msil.managed.calls' operands 1-6, their 8th
// operand and their canonical C function types are equal. Otherwise clang's
// CodeGen reports, at the declaration that comes later in the unit,
//   managed calls '<f>' and '<g>' share the symbol '<s>' but differ in their <what>
// with the note '<f>' declared here at the earlier one, <what> being the
// first that differs of 'msil_method' or 'msil_catch' arguments, function
// types and 'dllexport', 'constructor' or 'destructor' attributes. Without
// T-M24 both calls reached the earlier declaration's member silently.
// - label2.c: asm labels 'foo' on a (Abs) and b (Sign).
// - swap.c: the same with b called first; the error stays at b, the later
//   declaration.
// - rext.c: '#pragma redefine_extname b a' gives b the symbol 'a'.
// - cname.c: b's asm label 'a' is a's C name.
// - catch.c: only the msil_catch lists differ: the arguments.
// - types.c: equal arguments for C types 'int (int)' and 'int (short)', both
//   'bool;val' for int32(bool): the function types.
// - argstypes.c, argsattr.c and typesattr.c: the order (arguments, then
//   function types, then attributes) when two of them differ; argstypes.c's
//   arguments differ in the ASSEMBLY only.
// - dllexport.c (the Windows triple only; the Linux triples ignore
//   dllexport), ctor.c, dtor.c, prio.c (constructor(101) and
//   constructor(102)) and ctorlate.c (constructor written on a redeclaration
//   after the calls, read at the end of the unit): the attributes. ctorlate.c
//   has b's declarations and the calls on one line, so the error may name
//   either declaration of b.
// - cxx.cpp: C++ g's asm label is f's mangled name ('_Z1fi' on the Linux
//   triple, '?f@@YAHH@Z' on the Windows triple).
// - block.c and blockdll.c (the Windows triple only): b's only declaration is
//   at block scope, in a function that never calls b, with constructor
//   (block.c) or dllexport (blockdll.c). They give b a forwarder although the
//   unit never calls it (msil-method-forwarders-block.c), so b is checked
//   against a, which use() calls: the arguments. blockattr.c: block.c with
//   b naming a's member, so only the attributes differ. blockonly.c: a and b
//   are both declared only at block scope and never called, with destructor
//   and destructor(101): the attributes.
// Each unit has one clash: clang emits nothing at the end of a unit that has
// an error, which would hide a check made there.
// Accepted:
// - same.c: equal arguments, function types and attributes are one call: one
//   declaration '@__msil_mc.foo', which both calls use, and one
//   '!msil.managed.calls' entry; no second function '__msil_mc.foo.<x>'.
// - samector.c: the same with constructor(101) on both: one entry, whose 8th
//   operand is 'constructor(101)', one forwarder '__msil_mc_ctor.foo' and one
//   llvm.global_ctors entry, so the constructor runs once.
// - st.c: 'static' declarations with the asm label 'foo' and different
//   members get two different '__msil_mc.foo.__uniq.<n>' names (MR2), which
//   do not clash.

// RUN: rm -rf %t && split-file %s %t
// DEFINE: %{t} =
// DEFINE: %{v} = %clang_cc1 -triple %{t} -emit-llvm-only -verify
// DEFINE: %{c} = %{v} %t/label2.c && %{v} %t/swap.c && %{v} %t/rext.c && \
// DEFINE:   %{v} %t/cname.c && %{v} %t/catch.c && %{v} %t/types.c && \
// DEFINE:   %{v} %t/argstypes.c && %{v} %t/argsattr.c && %{v} %t/typesattr.c && \
// DEFINE:   %{v} %t/ctor.c && %{v} %t/dtor.c && %{v} %t/prio.c && %{v} %t/ctorlate.c && \
// DEFINE:   %{v} %t/block.c && %{v} %t/blockattr.c && %{v} %t/blockonly.c
// DEFINE: %{ok} = %clang_cc1 -triple %{t} -verify -emit-llvm -o %t/same.%{t}.ll %t/same.c && \
// DEFINE:   FileCheck %s --check-prefix=SAME --input-file=%t/same.%{t}.ll && \
// DEFINE:   grep "^declare .*@__msil_mc\." %t/same.%{t}.ll | count 1 && \
// DEFINE:   not grep "__msil_mc\.foo\." %t/same.%{t}.ll && \
// DEFINE:   %clang_cc1 -triple %{t} -verify -emit-llvm -o %t/sctor.%{t}.ll %t/samector.c && \
// DEFINE:   FileCheck %s --check-prefix=SCTOR --input-file=%t/sctor.%{t}.ll && \
// DEFINE:   grep "^@llvm.global_ctors = " %t/sctor.%{t}.ll | FileCheck %s --check-prefix=SCTORS && \
// DEFINE:   grep "^declare .*@__msil_mc\." %t/sctor.%{t}.ll | count 1 && \
// DEFINE:   grep "^define .*@__msil_mc_ctor\." %t/sctor.%{t}.ll | count 1 && \
// DEFINE:   %clang_cc1 -triple %{t} -verify -emit-llvm -o %t/st.%{t}.ll %t/st.c && \
// DEFINE:   FileCheck %s --check-prefix=ST --input-file=%t/st.%{t}.ll
// REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
// RUN: %{c}
// RUN: %{ok}
// RUN: %clang_cc1 -triple %{t} -emit-llvm-only -verify=expected,lnx %t/cxx.cpp
// REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
// RUN: %{c}
// RUN: %{ok}
// RUN: %{v} %t/dllexport.c
// RUN: %{v} %t/blockdll.c
// RUN: %clang_cc1 -triple %{t} -emit-llvm-only -verify=expected,win %t/cxx.cpp

// SAME:      {{^}}define {{.*}}i32 @use(
// SAME:      {{^}}  %{{.+}} = call i32 @__msil_mc.foo(i32 noundef %{{.+}}){{$}}
// SAME:      {{^}}  %{{.+}} = call i32 @__msil_mc.foo(i32 noundef %{{.+}}){{$}}
// SAME:      {{^}}!msil.managed.calls = !{![[#M:]]}{{$}}
// SAME:      {{^}}![[#M]] = !{!"__msil_mc.foo", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}{{$}}

// SCTOR:     {{^}}define {{.*}}void @use(
// SCTOR:     {{^}}  call void @__msil_mc.foo(){{$}}
// SCTOR:     {{^}}  call void @__msil_mc.foo(){{$}}
// SCTOR:     {{^}}!msil.managed.calls = !{![[#M:]]}{{$}}
// SCTOR:     {{^}}![[#M]] = !{!"__msil_mc.foo", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(101)"}{{$}}
// SCTORS:    {{^}}@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 101, ptr @__msil_mc_ctor.foo, ptr @__msil_mc_ctor.foo }]{{$}}

// ST:        {{^}}define {{.*}}i32 @use(
// ST:        {{^}}  %{{.+}} = call i32 @__msil_mc.foo.__uniq.[[A:[0-9]+]](i32 noundef %{{.+}}){{$}}
// ST:        {{^}}  %{{.+}} = call i32 @__msil_mc.foo.__uniq.[[B:[0-9]+]](i32 noundef %{{.+}}){{$}}
// ST:        {{^}}!msil.managed.calls = !{![[#]], ![[#]]}{{$}}
// ST-DAG:    {{^}}![[#]] = !{!"__msil_mc.foo.__uniq.[[A]]", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}{{$}}
// ST-DAG:    {{^}}![[#]] = !{!"__msil_mc.foo.__uniq.[[B]]", !"System.Math", !"Sign", !"int32(int32)", !"", !"val;val", !""}{{$}}

//--- label2.c
int a(int) __asm__("foo") __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
int b(int) __asm__("foo") __attribute__((msil_method("System.Math", "Sign", "int32(int32)"))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'msil_method' or 'msil_catch' arguments{{$}}}}
int use(int x) { return a(x) + b(x); }

//--- swap.c
int a(int) __asm__("foo") __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
int b(int) __asm__("foo") __attribute__((msil_method("System.Math", "Sign", "int32(int32)"))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'msil_method' or 'msil_catch' arguments{{$}}}}
int use(int x) { return b(x) + a(x); }

//--- rext.c
#pragma redefine_extname b a
int a(int) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
int b(int) __attribute__((msil_method("System.Math", "Sign", "int32(int32)"))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'a' but differ in their 'msil_method' or 'msil_catch' arguments{{$}}}}
int use(int x) { return a(x) + b(x); }

//--- cname.c
int a(int) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
int b(int) __asm__("a") __attribute__((msil_method("System.Math", "Sign", "int32(int32)"))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'a' but differ in their 'msil_method' or 'msil_catch' arguments{{$}}}}
int use(int x) { return a(x) + b(x); }

//--- catch.c
void a(const char *p, int *err) __asm__("foo") __attribute__((msil_method("System.IO.File", "Delete", "void(string)"), msil_catch("System.IO.IOException", 5))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
void b(const char *p, int *err) __asm__("foo") __attribute__((msil_method("System.IO.File", "Delete", "void(string)"), msil_catch("System.UnauthorizedAccessException", 13))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'msil_method' or 'msil_catch' arguments{{$}}}}
void use(int *e) { a("x", e); b("y", e); }

//--- types.c
int a(int) __asm__("foo") __attribute__((msil_method("System.Convert", "ToInt32", "int32(bool)"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
int b(short) __asm__("foo") __attribute__((msil_method("System.Convert", "ToInt32", "int32(bool)"))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their function types{{$}}}}
int use(int x) { return a(x) + b((short)x); }

//--- argstypes.c
int a(int) __asm__("foo") __attribute__((msil_method("System.Convert", "ToInt32", "int32(bool)"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
int b(short) __asm__("foo") __attribute__((msil_method("System.Convert", "ToInt32", "int32(bool)", "System.Runtime"))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'msil_method' or 'msil_catch' arguments{{$}}}}
int use(int x) { return a(x) + b((short)x); }

//--- argsattr.c
void a(void) __asm__("foo") __attribute__((msil_method("System.GC", "Collect", "void()"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
void b(void) __asm__("foo") __attribute__((constructor, msil_method("System.GC", "WaitForPendingFinalizers", "void()"))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'msil_method' or 'msil_catch' arguments{{$}}}}
void use(void) { a(); b(); }

//--- typesattr.c
int a(void) __asm__("foo") __attribute__((constructor, msil_method("System.Environment", "get_ProcessorCount", "int32()"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
unsigned b(void) __asm__("foo") __attribute__((msil_method("System.Environment", "get_ProcessorCount", "int32()"))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their function types{{$}}}}
int use(void) { return a() + (int)b(); }

//--- ctor.c
void a(void) __asm__("foo") __attribute__((msil_method("System.GC", "Collect", "void()"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
void b(void) __asm__("foo") __attribute__((constructor, msil_method("System.GC", "Collect", "void()"))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'dllexport', 'constructor' or 'destructor' attributes{{$}}}}
void use(void) { a(); b(); }

//--- dtor.c
void a(void) __asm__("foo") __attribute__((msil_method("System.GC", "Collect", "void()"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
void b(void) __asm__("foo") __attribute__((destructor, msil_method("System.GC", "Collect", "void()"))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'dllexport', 'constructor' or 'destructor' attributes{{$}}}}
void use(void) { a(); b(); }

//--- prio.c
void a(void) __asm__("foo") __attribute__((constructor(101), msil_method("System.GC", "Collect", "void()"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
void b(void) __asm__("foo") __attribute__((constructor(102), msil_method("System.GC", "Collect", "void()"))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'dllexport', 'constructor' or 'destructor' attributes{{$}}}}
void use(void) { a(); b(); }

//--- ctorlate.c
void a(void) __asm__("foo") __attribute__((msil_method("System.GC", "Collect", "void()"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
void b(void) __asm__("foo") __attribute__((msil_method("System.GC", "Collect", "void()"))); void use(void) { a(); b(); } void b(void) __attribute__((constructor)); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'dllexport', 'constructor' or 'destructor' attributes{{$}}}}

//--- dllexport.c
int a(int) __asm__("foo") __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
int b(int) __asm__("foo") __attribute__((dllexport, msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'dllexport', 'constructor' or 'destructor' attributes{{$}}}}
int use(int x) { return a(x) + b(x); }

//--- cxx.cpp
#ifdef _WIN32
#define F_SYMBOL "?f@@YAHH@Z"
#else
#define F_SYMBOL "_Z1fi"
#endif
int f(int) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-note-re {{{{^}}'f' declared here{{$}}}}
int g(int) __asm__(F_SYMBOL) __attribute__((msil_method("System.Math", "Sign", "int32(int32)"))); // lnx-error-re {{{{^}}managed calls 'f' and 'g' share the symbol '_Z1fi' but differ in their 'msil_method' or 'msil_catch' arguments{{$}}}} win-error-re {{{{^}}managed calls 'f' and 'g' share the symbol '?f@@YAHH@Z' but differ in their 'msil_method' or 'msil_catch' arguments{{$}}}}
int use(int x) { return f(x) + g(x); }

//--- block.c
void a(void) __asm__("foo") __attribute__((msil_method("System.GC", "Collect", "void()"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
void use(void) { a(); }
void init(void) { void b(void) __asm__("foo") __attribute__((constructor, msil_method("System.GC", "WaitForPendingFinalizers", "void()"))); } // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'msil_method' or 'msil_catch' arguments{{$}}}}

//--- blockdll.c
int a(int) __asm__("foo") __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
int use(int x) { return a(x); }
void init(void) { int b(int) __asm__("foo") __attribute__((dllexport, msil_method("System.Math", "Sign", "int32(int32)"))); } // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'msil_method' or 'msil_catch' arguments{{$}}}}

//--- blockattr.c
void a(void) __asm__("foo") __attribute__((msil_method("System.GC", "Collect", "void()"))); // expected-note-re {{{{^}}'a' declared here{{$}}}}
void use(void) { a(); }
void init(void) { void b(void) __asm__("foo") __attribute__((constructor, msil_method("System.GC", "Collect", "void()"))); } // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'dllexport', 'constructor' or 'destructor' attributes{{$}}}}

//--- blockonly.c
void init(void) { void a(void) __asm__("foo") __attribute__((destructor, msil_method("System.GC", "Collect", "void()"))); } // expected-note-re {{{{^}}'a' declared here{{$}}}}
void init2(void) { void b(void) __asm__("foo") __attribute__((destructor(101), msil_method("System.GC", "Collect", "void()"))); } // expected-error-re {{{{^}}managed calls 'a' and 'b' share the symbol 'foo' but differ in their 'dllexport', 'constructor' or 'destructor' attributes{{$}}}}

//--- same.c
// expected-no-diagnostics
int a(int) __asm__("foo") __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
int b(int) __asm__("foo") __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
int use(int x) { return a(x) + b(x); }

//--- samector.c
// expected-no-diagnostics
void a(void) __asm__("foo") __attribute__((constructor(101), msil_method("System.GC", "Collect", "void()")));
void b(void) __asm__("foo") __attribute__((constructor(101), msil_method("System.GC", "Collect", "void()")));
void use(void) { a(); b(); }

//--- st.c
// expected-no-diagnostics
static int a(int) __asm__("foo") __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
static int b(int) __asm__("foo") __attribute__((msil_method("System.Math", "Sign", "int32(int32)")));
int use(int x) { return a(x) + b(x); }
