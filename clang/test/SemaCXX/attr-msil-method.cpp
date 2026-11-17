// The managed-call attributes in C++ (ImplementationPlan.md 5.20.2 and
// 5.20.3, milestone MC-1): msil_method and msil_catch on namespace-scope
// functions of the MSIL object-format triples.
// - __has_attribute and __has_cpp_attribute(clang::...) are 1 on every MSIL
//   triple (msil32-unknown-dotnet included) and 0 on native x86_64 Linux,
//   which ignores both attributes with clang's 'unknown attribute ...
//   ignored' warning (the NATIVE run).
// - Accepted: functions at global scope, in a named namespace and in an
//   'extern "C"' block; overloaded functions, each declaration naming its own
//   member (each has its own mangled name); noexcept; the C++ types bool,
//   char8_t (utf8), char16_t (char and utf16), char32_t (utf32) and wchar_t;
//   the [[clang::...]] spelling; a redeclaration that inherits.
// - T-M13 whole for each <kind>: a member function (also a static one, and
//   a lambda's call operator), a function template, a constexpr and a
//   consteval function; an explicit specialization of a function template is
//   T-M13 too (its <kind> is matched by pattern). msil_catch on a lambda,
//   which can never carry msil_method, is T-M5 whole.
// - wchar_t is a C++ keyword type, so the 'char' row shows the width
//   through -triple exactly: 16 bits on the Windows triple (accepted, 'win'),
//   32 bits on the Linux triple and both portable triples (T-M3, 'lnx'),
//   while 'const wchar_t *' carries 'string' on both.
// - P32: msil32-unknown-dotnet's ILP32 widths, compiled only on that triple,
//   as in Sema/attr-msil-method.c: 'long' carries int32 but not int64, 'long'
//   and size_t carry native int and native uint, and 'long long', 'int' and
//   'unsigned int' do not. That run leaves out the __int128 cases (the target
//   has no __int128) and the T-M19 cases (no T-M19 text of its own until its
//   backend runs; section 12's T-M19 row defines the portable form for
//   msil64-unknown-dotnet only).
// - The C++-only C types in T-M3 texts (a reference is not an object
//   pointer), and T-M1, T-M12 and T-M14 on C++ functions, T-M14 also for
//   chains of three or more declarations (each later one is compared with the
//   first one written).
// - DELEGATES: a class's '.ctor' with the signature 'instance void(object,
//   native int)', the shape of a delegate's constructor, on a function in a
//   named namespace, is T-M2 with its reason pinned whole (delegates over C
//   functions are not in MC-1: 5.20.14 'Delegates', brief Q27 (a));
//   Sema/attr-msil-method.c has the other forms and the accepted ones.
// - msil_catch with a CODE that depends on a template parameter and no
//   msil_method is T-M5 on the templated declaration.
// - INTERNAL: a used managed-call declaration with internal linkage (in an
//   anonymous namespace, or 'static') gets no -Wundefined-internal warning
//   (5.20.3 as amended in section 12 on 2026-11-04). Sema gives that warning
//   only in a translation unit without errors, so these declarations have
//   runs of their own.
// - UNUSED (-Wunused-function with -Wunneeded-internal-declaration, and
//   -Wall), as in Sema/attr-msil-method.c: in an anonymous namespace (also
//   reopened) and for a 'static' function, a managed call with constructor
//   or destructor gets neither warning, since clang's CodeGen calls it from
//   its forwarder (contract D6) and the backend makes its body. That holds
//   with the attribute written on the first declaration (before or after
//   msil_method), added by a later redeclaration or inherited by a later
//   plain one, and when an unevaluated operand is its only reference. One
//   with neither attribute keeps native clang's warnings, pinned whole, and
//   so does a declaration with constructor and no msil_method; a
//   constructor definition gets none. Sema gives these warnings only in a
//   translation unit without errors, so these declarations have runs of
//   their own.
// - T-M3 for integers that no CIL integer or enum carries, as in C: __int128
//   for an enum, as a parameter and as the result (permanent, D4: no CIL
//   enum is wider than 64 bits; where the target has __int128), and
//   _BitInt(N), which clang takes in C++ as
//   an extension, for int32, int64, an enum and bool, both ways (a temporary
//   refusal, the row 'Managed-calls input (_BitInt(N) conversions)', D19).
// - The replaceable global allocation and deallocation functions (operator
//   new and new[], operator delete and delete[]) are T-M13 whole (D5): clang
//   declares them implicitly before any declaration written in the source,
//   and new and delete expressions call them.
// - constructor and destructor on a managed call (contract D6), as in
//   Sema/attr-msil-method.c: accepted with no parameters (at global scope, in
//   a namespace, with internal linkage, with a priority, in both spellings);
//   T-M19 in its four forms ('host' on the Windows and Linux triples, 'coff'
//   for the refusals on the Windows triple only, 'p64' on
//   msil64-unknown-dotnet; the msil32 run does not compile these cases, P32
//   above), with the ELF prefix (int, char **, char **)
//   accepted on the Linux triple; T-M20 alone when msil_catch is present;
//   and constructor or destructor written on a redeclaration (D5).
// - DLL: dllexport and dllimport, as __declspec(...) and as
//   __attribute__((...)), at global scope and in a namespace, give a managed
//   call no diagnostic on the four Windows triples (cc1 takes __declspec with
//   -fms-extensions on the MSVC triples; the MinGW triples define it as a
//   macro). The Linux, musl, aarch64 Linux and portable triples keep clang's
//   own texts: 'unknown attribute 'dllexport' ignored', and the __declspec
//   error without -fdeclspec; with -fdeclspec, '__declspec attribute
//   'dllexport' is not supported' ('dlldeclspec').
// - LABEL: an asm label that starts with '__msil_mc.' (with or without a
//   leading '\01') on a function declaration, on the declaration of a
//   function defined later, or on a variable (an 'extern "C"' one too), is
//   T-M22 on the MSIL triples and accepted by native x86_64 Linux and wasm32
//   (D11); one that starts with '__msil_mc_dtor.' on a function in a named
//   namespace is T-M22's forwarder form '... (a managed-call forwarder)'
//   (Sema/attr-msil-method.c has the other forms). '#pragma
//   redefine_extname' takes identifiers, which never hold the '.', so clang
//   ignores that form everywhere with its own warning.
// - AFTER, on every MSIL triple of these RUN lines, as in
//   Sema/attr-msil-method.c: '#pragma weak f' after a managed-call
//   declaration of f is T-M6 at the pragma's identifier and adds no 'weak'
//   (one error per function, also with a later plain redeclaration), for a
//   function at namespace scope with C++ linkage and for one with C linkage
//   (the pragma names an identifier, which it looks up at global scope); and
//   msil_method first written after a definition (a body, also with
//   msil_catch and with C linkage, an 'alias' attribute, or the alias that
//   '#pragma weak f = g' declares) is T-M14's 'must appear on the first
//   declaration' with the note 'first declared here' at the definition.
// - Every T-M pin and every 'first declared here' note is a regex directive
//   anchored at both ends ('{{^}}' then the text then '{{$}}'), because a
//   plain directive matches any diagnostic that merely contains its text.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c++20 -fsyntax-only -verify=expected,lnx,host %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c++20 -fsyntax-only -verify=expected,win,host,coff %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c++20 -fsyntax-only -verify=expected,lnx,p64 %s
// RUN: %clang_cc1 -triple msil32-unknown-dotnet -std=c++20 -fsyntax-only -verify=expected,lnx,p32 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fsyntax-only -DNATIVE -verify=native %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c++20 -fsyntax-only -DINTERNAL -Wundefined-internal -verify=internal %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c++20 -fsyntax-only -DINTERNAL -Wundefined-internal -verify=internal %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c++20 -fsyntax-only -DINTERNAL -Wundefined-internal -verify=internal %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c++20 -fsyntax-only -DUNUSED -Wunused-function -Wunneeded-internal-declaration -verify=unused %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c++20 -fsyntax-only -DUNUSED -Wunused-function -Wunneeded-internal-declaration -verify=unused %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c++20 -fsyntax-only -DUNUSED -Wunused-function -Wunneeded-internal-declaration -verify=unused %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c++20 -fsyntax-only -DUNUSED -Wall -verify=unused %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c++20 -fsyntax-only -DUNUSED -Wall -verify=unused %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c++20 -fsyntax-only -DUNUSED -Wall -verify=unused %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -std=c++20 -fsyntax-only -DDLL -verify=dllwin %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fms-extensions -std=c++20 -fsyntax-only -DDLL -verify=dllwin %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -std=c++20 -fsyntax-only -DDLL -verify=dllwin %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c++20 -fsyntax-only -DDLL -verify=dllwin %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c++20 -fsyntax-only -DDLL -verify=dllelse %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -std=c++20 -fsyntax-only -DDLL -verify=dllelse %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=c++20 -fsyntax-only -DDLL -verify=dllelse %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c++20 -fsyntax-only -DDLL -verify=dllelse %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fdeclspec -std=c++20 -fsyntax-only -DDLL -verify=dlldeclspec %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c++20 -fsyntax-only -DLABEL -verify=label,pragma %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c++20 -fsyntax-only -DLABEL -verify=label,pragma %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c++20 -fsyntax-only -DLABEL -verify=label,pragma %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fsyntax-only -DLABEL -verify=pragma %s
// RUN: %clang_cc1 -triple wasm32-unknown-unknown -std=c++20 -fsyntax-only -DLABEL -verify=pragma %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c++20 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -std=c++20 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c++20 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -std=c++20 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=c++20 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -std=c++20 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c++20 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c++20 -fsyntax-only -DAFTER -verify=after %s

typedef __SIZE_TYPE__ size_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef struct __dn_object *dn_handle;

#ifdef NATIVE

#if __has_attribute(msil_method) || __has_cpp_attribute(clang::msil_method) || __has_cpp_attribute(clang::msil_catch)
#error "msil_method and msil_catch exist only on MSIL triples"
#endif

int n_abs(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // native-warning {{unknown attribute 'msil_method' ignored}}
[[clang::msil_catch("System.IO.IOException", 5)]] int n_div(int a, int *err); // native-warning {{unknown attribute 'clang::msil_catch' ignored}}

#elif defined(INTERNAL)

// internal-no-diagnostics
// Internal linkage: the backend defines the K5 stub of a managed-call
// declaration, so one that is used but never defined is not 'undefined'
// (-verify allows no diagnostic at all here).
namespace {
int anon_abs(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
bool anon_exists(const char *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
} // namespace
static int static_sign(int x) __attribute__((msil_method("System.Math", "Sign", "int32(int32)")));
int main() { return anon_abs(-1) + anon_exists("x") + static_sign(2); }

#elif defined(UNUSED)

namespace {
// No warning: constructor or destructor on the first declaration (before or
// after msil_method; a priority and a result in the [[...]] spelling), or
// added by a later redeclaration, here in a reopened anonymous namespace.
void u_init() __attribute__((constructor, msil_method("System.GC", "Collect", "void()")));
void u_fini() __attribute__((msil_method("System.GC", "Collect", "void()"), destructor));
[[gnu::constructor(101)]] [[clang::msil_method("System.Diagnostics.Stopwatch", "GetTimestamp", "int64()", "System")]] long long u_ts();
void u_re_ctor() __attribute__((msil_method("System.GC", "Collect", "void()")));
void u_re_ctor() __attribute__((constructor));
void u_re_dtor() __attribute__((msil_method("System.GC", "Collect", "void()")));
} // namespace
namespace {
void u_re_dtor() __attribute__((destructor));
// No warning: inherited by a later plain redeclaration.
void u_inherit() __attribute__((constructor, msil_method("System.GC", "Collect", "void()")));
void u_inherit();
// Native clang's warning: neither attribute, and constructor without
// msil_method (nothing defines it); a constructor definition gets none.
void plain() __attribute__((msil_method("System.GC", "Collect", "void()"))); // unused-warning-re {{{{^}}unused function 'plain'{{$}}}}
void n_ctor_decl() __attribute__((constructor)); // unused-warning-re {{{{^}}unused function 'n_ctor_decl'{{$}}}}
void n_ctor_def() __attribute__((constructor));
void n_ctor_def() {}
// -Wunneeded-internal-declaration: an unevaluated operand is the only
// reference. No warning with constructor or destructor; native clang's
// warning with neither.
int u_ref_ctor() __attribute__((constructor, msil_method("System.Environment", "get_ProcessorCount", "int32()")));
int u_ref_dtor() __attribute__((destructor, msil_method("System.Environment", "get_ProcessorCount", "int32()")));
int u_ref_plain() __attribute__((msil_method("System.Environment", "get_ProcessorCount", "int32()"))); // unused-warning-re {{{{^}}function 'u_ref_plain' is not needed and will not be emitted{{$}}}}
} // namespace
// 'static' functions the same way (the redeclaration of u_static_re omits
// 'static', and the function keeps its internal linkage).
static void u_static() __attribute__((destructor, msil_method("System.GC", "Collect", "void()")));
static void u_static_re() __attribute__((msil_method("System.GC", "Collect", "void()")));
void u_static_re() __attribute__((constructor));
static void plain_static() __attribute__((msil_method("System.GC", "Collect", "void()"))); // unused-warning-re {{{{^}}unused function 'plain_static'{{$}}}}
int u_sizes() { return int(sizeof(u_ref_ctor()) + sizeof(u_ref_dtor()) + sizeof(u_ref_plain())); }

#elif defined(DLL)

// dllwin-no-diagnostics
__declspec(dllexport) int x_exp1(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // dllelse-error-re {{{{^}}'__declspec' attributes are not enabled; use '-fdeclspec' or '-fms-extensions' to enable support for __declspec attributes{{$}}}} dlldeclspec-warning-re {{{{^}}__declspec attribute 'dllexport' is not supported{{$}}}}
int x_exp2(int x) __attribute__((dllexport, msil_method("System.Math", "Sign", "int32(int32)"))); // dllelse-warning-re {{{{^}}unknown attribute 'dllexport' ignored{{$}}}} dlldeclspec-warning-re {{{{^}}unknown attribute 'dllexport' ignored{{$}}}}
__declspec(dllimport) int x_imp1(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // dllelse-error-re {{{{^}}'__declspec' attributes are not enabled; use '-fdeclspec' or '-fms-extensions' to enable support for __declspec attributes{{$}}}} dlldeclspec-warning-re {{{{^}}__declspec attribute 'dllimport' is not supported{{$}}}}
int x_imp2(int x) __attribute__((dllimport, msil_method("System.Math", "Sign", "int32(int32)"))); // dllelse-warning-re {{{{^}}unknown attribute 'dllimport' ignored{{$}}}} dlldeclspec-warning-re {{{{^}}unknown attribute 'dllimport' ignored{{$}}}}
namespace ns { __declspec(dllexport) int x_ns(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); } // dllelse-error-re {{{{^}}'__declspec' attributes are not enabled; use '-fdeclspec' or '-fms-extensions' to enable support for __declspec attributes{{$}}}} dlldeclspec-warning-re {{{{^}}__declspec attribute 'dllexport' is not supported{{$}}}}
// With constructor and destructor, and unused.
__declspec(dllexport) void x_boot() __attribute__((constructor(300), destructor(500), msil_method("System.GC", "Collect", "void()"))); // dllelse-error-re {{{{^}}'__declspec' attributes are not enabled; use '-fdeclspec' or '-fms-extensions' to enable support for __declspec attributes{{$}}}} dlldeclspec-warning-re {{{{^}}__declspec attribute 'dllexport' is not supported{{$}}}}
int use_dll() { return x_exp1(1) + x_exp2(2) + x_imp1(3) + x_imp2(4) + ns::x_ns(5); }

#elif defined(LABEL)

// T-M22 (D11): '__msil_mc.' names managed-call stubs.
int fake_count() __asm__("__msil_mc.widget_count"); // label-error-re {{{{^}}asm label '__msil_mc.widget_count' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
int fake_count() { return 5; }
int fake_decl(int x) __asm__("__msil_mc.widget_decl"); // label-error-re {{{{^}}asm label '__msil_mc.widget_decl' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
int fake_var __asm__("__msil_mc.widget_var") = 1; // label-error-re {{{{^}}asm label '__msil_mc.widget_var' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
extern "C" int fake_ext __asm__("__msil_mc.widget_ext"); // label-error-re {{{{^}}asm label '__msil_mc.widget_ext' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
int fake_esc() __asm__("\01__msil_mc.widget_esc"); // label-error-re {{{{^}}asm label '__msil_mc.widget_esc' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
// T-M22's forwarder form: '__msil_mc_dtor.' names the forwarder of a
// destructor managed call.
namespace ns { void fwd_fini() __asm__("__msil_mc_dtor.widget_fini"); } // label-error-re {{{{^}}asm label '__msil_mc_dtor.widget_fini' is reserved for the MSIL toolchain (a managed-call forwarder){{$}}}}
// Not the reserved prefix.
int ok_label1() __asm__("__msil_mc_widget");
int ok_label2() __asm__("__msil_mcx.widget");
// pragma-warning-re@+1 {{{{^}}extra tokens at end of '#pragma redefine_extname' - ignored{{$}}}}
#pragma redefine_extname fake_prag __msil_mc.widget_count
extern "C" int fake_prag();
int use_labels() { return fake_count() + fake_decl(1) + fake_var + fake_ext + fake_esc() + ok_label1() + ok_label2() + fake_prag(); }

#elif defined(AFTER)

// '#pragma weak' after a managed-call declaration: T-M6 at the pragma's
// identifier, and no 'weak' for the redeclaration that follows; C++ linkage,
// then C linkage.
__attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) int pw_after(int x);
#pragma weak pw_after // after-error-re {{{{^}}'msil_method' cannot be combined with 'weak'{{$}}}}
__attribute__((msil_method("System.Math", "Sign", "int32(int32)"))) int pw_redecl(int x);
#pragma weak pw_redecl // after-error-re {{{{^}}'msil_method' cannot be combined with 'weak'{{$}}}}
int pw_redecl(int x);
extern "C" __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) int pw_c(int x);
#pragma weak pw_c // after-error-re {{{{^}}'msil_method' cannot be combined with 'weak'{{$}}}}
extern "C" __attribute__((msil_method("System.Math", "Sign", "int32(int32)"))) int pw_c_redecl(int x);
#pragma weak pw_c_redecl // after-error-re {{{{^}}'msil_method' cannot be combined with 'weak'{{$}}}}
extern "C" int pw_c_redecl(int x);

// msil_method first written after a definition: T-M14's first form, with
// the note at the definition (for the pragma's alias, at its 'f').
int ad_body(int x) { return x; } // after-note-re {{{{^}}first declared here{{$}}}}
__attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) int ad_body(int x); // after-error-re {{{{^}}'msil_method' must appear on the first declaration of 'ad_body'{{$}}}}
int ad_catch(int x, int *e) { return x; } // after-note-re {{{{^}}first declared here{{$}}}}
__attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 75))) int ad_catch(int x, int *e); // after-error-re {{{{^}}'msil_method' must appear on the first declaration of 'ad_catch'{{$}}}}
extern "C" int ad_cbody(int x) { return x; } // after-note-re {{{{^}}first declared here{{$}}}}
extern "C" __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) int ad_cbody(int x); // after-error-re {{{{^}}'msil_method' must appear on the first declaration of 'ad_cbody'{{$}}}}
extern "C" int ad_target(int x) { return x + 1; }
int ad_alias(int x) __attribute__((alias("ad_target"))); // after-note-re {{{{^}}first declared here{{$}}}}
__attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) int ad_alias(int x); // after-error-re {{{{^}}'msil_method' must appear on the first declaration of 'ad_alias'{{$}}}}
#pragma weak ad_pragma = ad_target // after-note-re {{{{^}}first declared here{{$}}}}
__attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) int ad_pragma(int x); // after-error-re {{{{^}}'msil_method' must appear on the first declaration of 'ad_pragma'{{$}}}}

#else

#if !__has_attribute(msil_method) || !__has_attribute(msil_catch)
#error "msil_method and msil_catch are MSIL attributes"
#endif
#if !__has_cpp_attribute(clang::msil_method) || !__has_cpp_attribute(clang::msil_catch)
#error "msil_method and msil_catch are MSIL attributes"
#endif

// Accepted.
int ok_abs(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
long long ok_abs(long long x) __attribute__((msil_method("System.Math", "Abs", "int64(int64)")));
double ok_abs(double x) noexcept __attribute__((msil_method("System.Math", "Abs", "float64(float64)")));
namespace ns {
int ok_max(int a, int b) __attribute__((msil_method("System.Math", "Max", "int32(int32, int32)")));
namespace inner {
[[clang::msil_method("System.Math", "Min", "int32(int32, int32)")]] int ok_min(int a, int b);
}
} // namespace ns
extern "C" {
bool ok_exists(const char *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
void ok_delete(const char *path, int *err) __attribute__((msil_method("System.IO.File", "Delete", "void(string)"), msil_catch("System.IO.IOException", 5)));
}
bool ok_exists8(const char8_t *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
bool ok_exists16(const char16_t *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
bool ok_exists32(const char32_t *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
bool ok_existsw(const wchar_t *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
bool ok_isdigit(char16_t c) __attribute__((msil_method("System.Char", "IsDigit", "bool(char)")));
void ok_cursor(bool on) __attribute__((msil_method("System.Console", "set_CursorVisible", "void(bool)")));
void ok_free(dn_handle h) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "Free", "instance void()")));
void *ok_mkex(const char *msg) __attribute__((msil_method("System.EntryPointNotFoundException", ".ctor", "instance void(string)")));
long long ok_ticks(dn_handle d) __attribute__((msil_method("valuetype System.DateTime", "get_Ticks", "instance int64()")));
dn_handle ok_newbytes(int n) __attribute__((msil_method("uint8[]", ".ctor", "instance void(int32)")));
int ok_redecl(int x) __attribute__((msil_method("System.Math", "Sign", "int32(int32)")));
int ok_redecl(int x);

int use_ok() {
  int err = 0;
  ok_delete("x", &err);
  return ok_abs(1) + (int)ok_abs(2LL) + (int)ok_abs(3.0) + ns::ok_max(4, 5) + ns::inner::ok_min(6, 7) + ok_redecl(8) + err;
}

// The 'char' row and wchar_t.
bool w_digit(wchar_t c) __attribute__((msil_method("System.Char", "IsDigit", "bool(char)"))); // lnx-error-re {{{{^}}parameter 1 of 'w_digit' has C type 'wchar_t', which cannot carry CIL type 'char'{{$}}}}

// P32: msil32-unknown-dotnet's ILP32 widths (MS5: native int is 4 bytes;
// 'long' is 32 bits and the type of intptr_t, and size_t is 'unsigned
// long'). 'long' carries int32 but not int64; 'long' and size_t carry native
// int and native uint, while 'long long', 'int' and 'unsigned int' do not.
// wchar_t is 32 bits: w_digit above is refused there through 'lnx', and
// 'const wchar_t *' carries 'string'.
#ifdef __msil32__
long p32_l32(long x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
long p32_nint(long a, size_t b) __attribute__((msil_method("Ns.Native", "Mix", "native int(native int, native int)")));
size_t p32_nuint(long a, size_t b) __attribute__((msil_method("Ns.Native", "Mix", "native uint(native uint, native uint)")));
bool p32_existsw(const wchar_t *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
long p32_l64(long x) __attribute__((msil_method("System.Math", "Abs", "int64(int64)"))); // p32-error-re {{{{^}}parameter 1 of 'p32_l64' has C type 'long', which cannot carry CIL type 'int64'{{$}}}}
long p32_r64(int x) __attribute__((msil_method("System.Convert", "ToInt64", "int64(int32)"))); // p32-error-re {{{{^}}the result of 'p32_r64' has C type 'long', which cannot carry CIL type 'int64'{{$}}}}
void *p32_nint_ll(long long n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "AllocHGlobal", "native int(native int)"))); // p32-error-re {{{{^}}parameter 1 of 'p32_nint_ll' has C type 'long long', which cannot carry CIL type 'native int'{{$}}}}
long long p32_nint_rll() __attribute__((msil_method("Ns.Native", "Get", "native int()"))); // p32-error-re {{{{^}}the result of 'p32_nint_rll' has C type 'long long', which cannot carry CIL type 'native int'{{$}}}}
void *p32_nint_i(int n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "AllocHGlobal", "native int(native int)"))); // p32-error-re {{{{^}}parameter 1 of 'p32_nint_i' has C type 'int', which cannot carry CIL type 'native int'{{$}}}}
unsigned p32_nuint_u(unsigned n) __attribute__((msil_method("Ns.Native", "Id", "native uint(native uint)"))); // p32-error-re {{{{^}}parameter 1 of 'p32_nuint_u' has C type 'unsigned int', which cannot carry CIL type 'native uint'{{$}}}}
#endif

// T-M13.
struct S {
  int m(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' on a C++ member function is not supported for MSIL yet{{$}}}}
  static int sm(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' on a C++ member function is not supported for MSIL yet{{$}}}}
};
template <class T> int tf(T x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' on a C++ function template is not supported for MSIL yet{{$}}}}
template <class T> int tspec(T x);
template <> int tspec<int>(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' on a C++ function template{{( specialization)?}} is not supported for MSIL yet{{$}}}}
constexpr int cf(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' on a C++ constexpr function is not supported for MSIL yet{{$}}}}
consteval int cef(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' on a C++ consteval function is not supported for MSIL yet{{$}}}}
// A lambda's call operator is a member function; GNU attributes after its
// parameter list apply to that operator.
auto lam_m = [](int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) { return x; }; // expected-error-re {{{{^}}'msil_method' on a C++ member function is not supported for MSIL yet{{$}}}}
auto lam_c = [](int a, int *err) __attribute__((msil_catch("System.IO.IOException", 5))) { return a; }; // expected-error-re {{{{^}}'msil_catch' needs 'msil_method' on the same declaration{{$}}}}

// T-M3 with a reference; T-M1; T-M12; T-M14.
void t_ref(int &p) __attribute__((msil_method("Ns.Pointers", "Take", "void(int32*)"))); // expected-error-re {{{{^}}parameter 1 of 't_ref' has C type 'int &', which cannot carry CIL type 'int32*'{{$}}}}
inline int d_inline(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); inline int d_inline(int x) { return x; } // expected-error-re {{{{^}}'msil_method' is only allowed on a function declaration without a body{{$}}}}
int v_va(int n, ...) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' is only allowed on a prototyped, non-variadic function other than 'main'{{$}}}}
int m_late(int x); // expected-note-re {{{{^}}first declared here{{$}}}}
int m_late(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' must appear on the first declaration of 'm_late'{{$}}}}
int m_diff(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-note-re {{{{^}}first declared here{{$}}}}
int m_diff(int x) __attribute__((msil_method("System.Math", "Abs", "int32(uint32)"))); // expected-error-re {{{{^}}'msil_method' on this declaration of 'm_diff' differs from its first declaration{{$}}}}
// Chains of three or more declarations, as in Sema/attr-msil-method.c: a
// later declaration is compared with the first one written, and the note is
// at that first declaration ('@-N' offsets from the error's line); 'Abs,
// Sign, Abs' gets exactly one error, at the second declaration.
int m_chain(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
int m_chain(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
int m_chain(int x) __attribute__((msil_method("System.Math", "Sign", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' on this declaration of 'm_chain' differs from its first declaration{{$}}}} expected-note-re@-2 {{{{^}}first declared here{{$}}}}
int m_chain_c(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 75)));
int m_chain_c(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 75)));
int m_chain_c(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 34))); // expected-error-re {{{{^}}'msil_catch' on this declaration of 'm_chain_c' differs from its first declaration{{$}}}} expected-note-re@-2 {{{{^}}first declared here{{$}}}}
int m_chain_o(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
int m_chain_o(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
int m_chain_o(int x);
int m_chain_o(int x) __attribute__((msil_method("System.Math", "Sign", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' on this declaration of 'm_chain_o' differs from its first declaration{{$}}}} expected-note-re@-3 {{{{^}}first declared here{{$}}}}
int m_aba(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
int m_aba(int x) __attribute__((msil_method("System.Math", "Sign", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' on this declaration of 'm_aba' differs from its first declaration{{$}}}} expected-note-re@-1 {{{{^}}first declared here{{$}}}}
int m_aba(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
[[clang::msil_catch("System.IO.IOException", 5)]] int c_alone(int a, int *err); // expected-error-re {{{{^}}'msil_catch' needs 'msil_method' on the same declaration{{$}}}}

// T-M2 for a delegate constructor's shape (DELEGATES in the header).
namespace cb {
[[clang::msil_method("System.Action", ".ctor", "instance void(object, native int)")]] void *s_dlg(void *target, intptr_t fn); // expected-error-re {{{{^}}'msil_method' signature 'instance void(object, native int)' is not valid: a class constructor with the signature '(object, native int)', the shape of a delegate's constructor, is not supported for MSIL yet{{$}}}}
} // namespace cb

// msil_catch with a CODE that depends on a template parameter and no
// msil_method: the templated declaration is T-M5, whole, and nothing else.
template <int N> int c_dep(int a, int *err) __attribute__((msil_catch("System.IO.IOException", N))); // expected-error-re {{{{^}}'msil_catch' needs 'msil_method' on the same declaration{{$}}}}
template <int N> [[clang::msil_catch("System.IO.IOException", N + 1)]] int c_dep2(int a, int *err); // expected-error-re {{{{^}}'msil_catch' needs 'msil_method' on the same declaration{{$}}}}

// The replaceable global allocation and deallocation functions (T-M13, D5).
void *operator new(size_t n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "AllocHGlobal", "native int(native int)"))); // expected-error-re {{{{^}}'msil_method' on a C++ replaceable global allocation function is not supported for MSIL yet{{$}}}}
void *operator new[](size_t n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "AllocHGlobal", "native int(native int)"))); // expected-error-re {{{{^}}'msil_method' on a C++ replaceable global allocation function is not supported for MSIL yet{{$}}}}
void operator delete(void *p) noexcept __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "FreeHGlobal", "void(native int)"))); // expected-error-re {{{{^}}'msil_method' on a C++ replaceable global deallocation function is not supported for MSIL yet{{$}}}}
void operator delete[](void *p) noexcept __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "FreeHGlobal", "void(native int)"))); // expected-error-re {{{{^}}'msil_method' on a C++ replaceable global deallocation function is not supported for MSIL yet{{$}}}}

// T-M3: __int128 for an enum, both ways (D4, permanent), where the target
// has __int128 (msil32-unknown-dotnet has none).
#ifdef __SIZEOF_INT128__
int e128(__int128 d) __attribute__((msil_method("System.Convert", "ToInt32", "int32(valuetype System.DayOfWeek)"))); // expected-error-re {{{{^}}parameter 1 of 'e128' has C type '__int128', which cannot carry CIL type 'valuetype System.DayOfWeek'{{$}}}}
__int128 re128(int x) __attribute__((msil_method("System.Convert", "ToInt32", "valuetype System.DayOfWeek(int32)"))); // expected-error-re {{{{^}}the result of 're128' has C type '__int128', which cannot carry CIL type 'valuetype System.DayOfWeek'{{$}}}}
#endif
// T-M3: _BitInt(N), whose storage width is not its value width (D19,
// temporary).
int abs32(_BitInt(32) x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}parameter 1 of 'abs32' has C type '_BitInt(32)', which cannot carry CIL type 'int32'{{$}}}}
long long abs64(_BitInt(64) x) __attribute__((msil_method("System.Math", "Abs", "int64(int64)"))); // expected-error-re {{{{^}}parameter 1 of 'abs64' has C type '_BitInt(64)', which cannot carry CIL type 'int64'{{$}}}}
void set_fg(_BitInt(24) c) __attribute__((msil_method("System.Console", "set_ForegroundColor", "void(valuetype System.ConsoleColor)"))); // expected-error-re {{{{^}}parameter 1 of 'set_fg' has C type '_BitInt(24)', which cannot carry CIL type 'valuetype System.ConsoleColor'{{$}}}}
_BitInt(8) tobool(int v) __attribute__((msil_method("System.Convert", "ToBoolean", "bool(int32)"))); // expected-error-re {{{{^}}the result of 'tobool' has C type '_BitInt(8)', which cannot carry CIL type 'bool'{{$}}}}
void cursor(_BitInt(8) on) __attribute__((msil_method("System.Console", "set_CursorVisible", "void(bool)"))); // expected-error-re {{{{^}}parameter 1 of 'cursor' has C type '_BitInt(8)', which cannot carry CIL type 'bool'{{$}}}}

// constructor and destructor (contract D6). Accepted: no parameters, a
// priority, a result (the forwarder drops it), internal linkage, a
// namespace, both spellings, and both attributes on one declaration.
void k_init() __attribute__((constructor, msil_method("System.GC", "Collect", "void()")));
void k_init101() __attribute__((constructor(101), msil_method("System.GC", "Collect", "void()")));
long long k_ts() __attribute__((constructor, msil_method("System.Diagnostics.Stopwatch", "GetTimestamp", "int64()", "System")));
void k_fini() __attribute__((destructor, msil_method("System.GC", "Collect", "void()")));
void k_both() __attribute__((constructor(300), destructor(500), msil_method("System.GC", "Collect", "void()")));
static void k_static() __attribute__((constructor, msil_method("System.GC", "Collect", "void()")));
namespace { void k_anon() __attribute__((destructor, msil_method("System.GC", "Collect", "void()"))); }
namespace ns { void k_ns() __attribute__((constructor, msil_method("System.GC", "Collect", "void()"))); }
[[gnu::constructor]] [[clang::msil_method("System.GC", "Collect", "void()")]] void k_cxx11();
// T-M19: the prefixes of (int32, native int, native int) are accepted under
// ELF rules only; any other list is refused on every triple. Not compiled on
// msil32-unknown-dotnet, which has no T-M19 text of its own yet (P32 in the
// header).
#ifndef __msil32__
void k_argc(int argc) __attribute__((constructor, msil_method("Ns.Startup", "Init", "void(int32)"))); // coff-error-re {{{{^}}global constructor 'k_argc' takes arguments, which MSIL does not pass{{$}}}} p64-error-re {{{{^}}constructor 'k_argc' takes arguments, which msil64-unknown-dotnet does not pass{{$}}}}
void k_argv(int argc, char **argv) __attribute__((constructor, msil_method("Ns.Startup", "Init", "void(int32, native int)"))); // coff-error-re {{{{^}}global constructor 'k_argv' takes arguments, which MSIL does not pass{{$}}}} p64-error-re {{{{^}}constructor 'k_argv' takes arguments, which msil64-unknown-dotnet does not pass{{$}}}}
void k_envp(int argc, char **argv, char **envp) __attribute__((constructor, msil_method("Ns.Startup", "Init", "void(int32, native int, native int)"))); // coff-error-re {{{{^}}global constructor 'k_envp' takes arguments, which MSIL does not pass{{$}}}} p64-error-re {{{{^}}constructor 'k_envp' takes arguments, which msil64-unknown-dotnet does not pass{{$}}}}
void k_envp_n(unsigned argc, intptr_t argv, size_t envp) __attribute__((constructor, msil_method("Ns.Startup", "Init", "void(int32, native int, native int)"))); // coff-error-re {{{{^}}global constructor 'k_envp_n' takes arguments, which MSIL does not pass{{$}}}} p64-error-re {{{{^}}constructor 'k_envp_n' takes arguments, which msil64-unknown-dotnet does not pass{{$}}}}
void k_four(int argc, char **argv, char **envp, char **auxv) __attribute__((constructor, msil_method("Ns.Startup", "Init", "void(int32, native int, native int, native int)"))); // host-error-re {{{{^}}global constructor 'k_four' takes arguments, which MSIL does not pass{{$}}}} p64-error-re {{{{^}}constructor 'k_four' takes arguments, which msil64-unknown-dotnet does not pass{{$}}}}
void k_noargc(char **argv) __attribute__((constructor, msil_method("Ns.Startup", "Init", "void(native int)"))); // host-error-re {{{{^}}global constructor 'k_noargc' takes arguments, which MSIL does not pass{{$}}}} p64-error-re {{{{^}}constructor 'k_noargc' takes arguments, which msil64-unknown-dotnet does not pass{{$}}}}
void k_wide(long long argc) __attribute__((constructor, msil_method("Ns.Startup", "Init", "void(int64)"))); // host-error-re {{{{^}}global constructor 'k_wide' takes arguments, which MSIL does not pass{{$}}}} p64-error-re {{{{^}}constructor 'k_wide' takes arguments, which msil64-unknown-dotnet does not pass{{$}}}}
void k_fini_arg(int code) __attribute__((destructor, msil_method("Ns.Startup", "Fini", "void(int32)"))); // host-error-re {{{{^}}global destructor 'k_fini_arg' takes arguments, which MSIL does not pass{{$}}}} p64-error-re {{{{^}}destructor 'k_fini_arg' takes arguments, which msil64-unknown-dotnet does not pass{{$}}}}
#endif
// T-M20: msil_catch, alone even when the parameters break T-M19 too.
void k_catch(const char *path, int *err) __attribute__((constructor, msil_method("System.IO.File", "Delete", "void(string)"), msil_catch("System.IO.IOException", 5))); // expected-error-re {{{{^}}'msil_catch' cannot be combined with 'constructor'{{$}}}}
void k_catch2(const char *path, int *err) __attribute__((msil_catch("System.IO.IOException", 5), msil_method("System.IO.File", "Delete", "void(string)"), destructor)); // expected-error-re {{{{^}}'msil_catch' cannot be combined with 'destructor'{{$}}}}
// On a redeclaration (D5): checked where constructor or destructor is
// written; a later redeclaration that inherits it reports nothing again.
void k_re() __attribute__((msil_method("System.GC", "Collect", "void()")));
void k_re() __attribute__((constructor));
void k_re();
#ifndef __msil32__
void k_re_arg(double d) __attribute__((msil_method("Ns.Startup", "Init", "void(float64)")));
void k_re_arg(double d) __attribute__((constructor)); // host-error-re {{{{^}}global constructor 'k_re_arg' takes arguments, which MSIL does not pass{{$}}}} p64-error-re {{{{^}}constructor 'k_re_arg' takes arguments, which msil64-unknown-dotnet does not pass{{$}}}}
void k_re_arg(double d);
#endif
void k_re_catch(const char *path, int *err) __attribute__((msil_method("System.IO.File", "Delete", "void(string)"), msil_catch("System.IO.IOException", 5)));
void k_re_catch(const char *path, int *err) __attribute__((destructor)); // expected-error-re {{{{^}}'msil_catch' cannot be combined with 'destructor'{{$}}}}

#endif
