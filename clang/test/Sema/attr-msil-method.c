// The managed-call attributes in C (ImplementationPlan.md 5.20.2 and 5.20.3,
// milestone MC-1): __attribute__((msil_method(TYPE, MEMBER, SIGNATURE
// [, ASSEMBLY]))) and the repeatable, ordered msil_catch(EXCEPTION_TYPE,
// CODE), with their [[clang::...]] spellings, on the MSIL object-format
// triples (the host triples, msil64-unknown-dotnet and msil32-unknown-dotnet,
// which clang accepts while the backend refuses it; TargetMSIL is
// T.isOSBinFormatMSIL()).
// - __has_attribute and __has_c_attribute are 1 for both on every MSIL
//   triple (msil32-unknown-dotnet included), and 0 on native x86_64 Linux and
//   wasm32, which ignore them with clang's 'unknown attribute ... ignored'
//   warning (the NATIVE run).
// - The accepted forms: one declaration per row of MS5's conversion table
//   (val, bool, enum, utf8, utf16, utf32, obj, boxed, gch, this-obj,
//   this-val, this-gch, void, err) and per member kind (static and instance
//   methods, interface methods, property accessors, .ctor of a class and of
//   a value type, the array .ctor (newarr), static and instance fields, a
//   nested type, an explicit ASSEMBLY), the catch list of 5.20.2's example,
//   both spellings, msil_catch written before msil_method, and
//   redeclarations that repeat the arguments or inherit them (MS2).
// - Every Sema text of 5.20.3 (T-M1 to T-M6, T-M11, T-M12, T-M14, T-M15,
//   T-M19, T-M20, T-M22; T-M13 is C++ only, SemaCXX/attr-msil-method.cpp)
//   whole: each pin is a regex directive anchored at both ends, '{{^}}' then
//   the text then '{{$}}', because a plain directive matches any diagnostic
//   that merely contains its text; the notes 'first declared here' are
//   pinned the same way. A T-M2 <reason> and a T-M4 <breakdown> that the
//   contract leaves free are matched by pattern (the empty ASSEMBLY's 'it is
//   empty' is pinned); a form that the fuller managed calls add must say 'is
//   not supported for MSIL yet', and 5.20.3's own example text is pinned
//   whole ('byref types are not supported for MSIL yet'; 'a 'this' handle, 2
//   CIL parameters and the 'msil_catch' error code'), as is T-M4 with one C
//   parameter, singular ('has 1 parameter'), and with none, plural. Each
//   rejected declaration breaks exactly one rule, so it gets exactly one
//   error. T-M14's 'differs' form compares a redeclaration with the first
//   declaration written and puts its note there (MS2), so on chains of three
//   or more declarations the note is pinned at the first one with an '@-N'
//   offset.
// - DELEGATES: delegates over C functions are not in MC-1 (5.20.14
//   'Delegates', brief Q27 (a)), so the two shapes that would make one are
//   T-M2 with their reasons pinned whole, on every MSIL triple of the RUN
//   lines (msil64 and msil32 included): the '.ctor' of a class (System.Action
//   or a third-party class) whose signature is 'instance void(object, native
//   int)', also written with 'class System.Object' or other spacing (the
//   text is quoted as written; the reason names the shape, not the type),
//   on SIGNATURE; and System.Runtime.InteropServices.Marshal's
//   GetDelegateForFunctionPointer on MEMBER, with TYPE in both spellings,
//   'System.Runtime.InteropServices.Marshal' and 'valuetype
//   System.Runtime.InteropServices.Marshal' (text output names a static
//   method's declaring type without its kind, so the second would still
//   reach Marshal). The same '.ctor' of a value type (HandleRef), a class's
//   '.ctor' of another shape, the delegate's Invoke and Marshal's other
//   members stay accepted.
// - NAMES: '<' and '`' start generic arguments only after a valid name, the
//   'valuetype' spelling of TYPE included, so a TYPE, an msil_catch type, a
//   type in SIGNATURE or a MEMBER with '<' or '`' that is not generic
//   ('<Module>', 'Ns.<Module>', '<Clone>$', '`Foo', 'Ns.`1',
//   '<>f__AnonymousType0`2', 'Ns.E/<Select>d__5`2') is T-M2 with the
//   invalid-name reason, pinned whole, while a generic type or instantiation
//   keeps its reason, also pinned whole: 'List`1', 'List`1<int32>' and
//   'Ns.Generic<int32>', and after 'valuetype' 'Ns.Generic<int32>',
//   'System.Nullable<int32>' and 'System.Nullable`1<int32>'. In SIGNATURE,
//   generic arguments after a generic type's arity ('class
//   System.Collections.Generic.List`1<int32>', 'valuetype
//   System.Nullable`1<int32>') are 'generic instantiations', pinned whole.
//   Nor is a TYPE after 'valuetype' an invalid name when it has a resolution
//   scope, a byref or a pointer: each gets its own reason, pinned whole ('a
//   type is named without a resolution scope; the assembly argument sets the
//   scope of text output', 'byref types are not supported for MSIL yet', 'a
//   pointer type has no members; name the type that declares the member'),
//   as does an msil_catch type with a scope (the same scope reason) or a
//   byref ('an exception type is a reference type named 'Ns.Name'').
// - The wide-string widths through -triple: wchar_t is 16 bits on the
//   Windows triple (it carries CIL 'char', the 'win' run) and 32 bits on the
//   Linux triples and both portable triples (it does not, the 'lnx' runs);
//   'const wchar_t *' carries 'string' on both widths (utf16 or utf32, which
//   CodeGen/MSIL/msil-method.c checks).
// - P32: msil32-unknown-dotnet's ILP32 widths (MS5; native int is 4 bytes
//   there), compiled only on that triple: 'long' carries int32 but not
//   int64, 'long' and size_t carry native int and native uint, and 'long
//   long', 'int' and 'unsigned int' do not; wchar_t is 32 bits. That run
//   leaves out the __int128 cases (the target has no __int128, so they are
//   compiled only where __SIZEOF_INT128__ is defined) and the T-M19 cases:
//   msil32-unknown-dotnet has no T-M19 text of its own until its backend
//   runs (5.18), and section 12's T-M19 row defines the portable form for
//   msil64-unknown-dotnet only.
// - CATCH_LATE: an msil_catch first written on a redeclaration is T-M14's
//   'must appear on the first declaration' (checked with FileCheck, since
//   the merged declaration may also break MS4).
// - IMPLICIT (-std=c89): msil_method on a declaration that follows the
//   implicit declaration C89 makes at a call is T-M14's 'must appear on the
//   first declaration', with the note 'first declared here' at that call
//   (D5); only a library builtin's implicit declaration does not count.
// - An msil_catch CODE that is not an integer constant gets clang's
//   'requires an integer constant' error alone (no T-M4 for the trailing
//   'int *').
// - INTERNAL: a used 'static' managed-call declaration gets no
//   -Wundefined-internal warning (5.20.3 as amended in section 12 on
//   2026-11-04). Sema gives that warning only in a translation unit without
//   errors, so these declarations have runs of their own.
// - UNUSED (-Wunused-function with -Wunneeded-internal-declaration, and
//   -Wall): a 'static' managed call with constructor or destructor gets
//   neither warning. Clang's CodeGen calls it from the forwarder that it
//   registers (contract D6) and the backend makes its body (the K5 stub), so
//   it is treated like a constructor or destructor definition, which native
//   clang never reports. That holds with the attribute written on the first
//   declaration (before or after msil_method), added by a later
//   redeclaration (msil_method itself must be on the first, T-M14) or
//   inherited by a later plain redeclaration, and when an unevaluated
//   operand is its only reference. A static managed call with neither
//   attribute keeps native clang's warnings, pinned whole: 'unused function
//   'plain'', and 'function '<f>' is not needed and will not be emitted'
//   when an unevaluated operand is its only reference. So does a static
//   declaration with constructor and no msil_method, which nothing defines,
//   while a static constructor definition gets none. Sema gives these
//   warnings only in a translation unit without errors, so these
//   declarations have runs of their own.
// - T-M3 for integers that no CIL integer or enum carries: __int128 for an
//   enum, as a parameter and as the result (permanent, D4: no CIL enum is
//   wider than 64 bits), and _BitInt(N) for int32, int64, an enum and bool,
//   both ways (a temporary refusal, the row 'Managed-calls input (_BitInt(N)
//   conversions)', D19).
// - constructor and destructor on a managed call (5.20.4 as amended, contract
//   D6): clang's CodeGen registers a forwarder that calls the managed call.
//   T-M19 is the MSIL backend's text for a native constructor whose
//   arguments it does not pass, per section rules: 'global constructor
//   '<f>' takes arguments, which MSIL does not pass' (and 'global
//   destructor') on the Windows and Linux-family triples ('host'), and
//   'constructor '<f>' takes arguments, which msil64-unknown-dotnet does not
//   pass' (and 'destructor') on msil64-unknown-dotnet ('p64'; the msil32 run
//   does not compile these cases, P32 above). Accepted: no
//   parameters, and for a constructor under ELF rules (x86_64 and aarch64
//   Linux, musl) a prefix of (int32, native int, native int) by the IL types
//   that the C parameters lower to, such as (int, char **, char **); a
//   destructor takes none ('coff' marks the Windows-only refusals). With
//   msil_catch it is T-M20 alone ('msil_catch' cannot be combined with
//   'constructor' or 'destructor'). A redeclaration that writes constructor
//   or destructor is checked the same way (D5).
// - DLL: dllexport and dllimport, as __declspec(...) and as
//   __attribute__((...)), give a managed call no diagnostic on the four
//   Windows triples (cc1 takes __declspec with -fms-extensions on the MSVC
//   triples; the MinGW triples define it as a macro). On the Linux, musl,
//   aarch64 Linux and portable triples clang's own texts stay: 'unknown
//   attribute 'dllexport' ignored', and without -fdeclspec the __declspec
//   error; with -fdeclspec, '__declspec attribute 'dllexport' is not
//   supported' ('dlldeclspec'). dllimport has no T-M6 row.
// - IFUNC: clang takes ifunc on the aarch64 Windows triples, where it is
//   T-M6 in both orders; elsewhere (x86_64-unknown-linux-gnu-msil) clang
//   ignores it as an unknown attribute.
// - PRINT (-ast-print): msil_method is printed with exactly the arguments
//   written, ASSEMBLY only when it is given, in both spellings (D8: ASSEMBLY
//   is variadic in Attr.td), and the printed file parses again with no
//   diagnostics. A second ASSEMBLY string is T-M2 'only one assembly can be
//   given', reported at that string.
// - LABEL: an asm label that starts with '__msil_mc.' (with or without a
//   leading '\01') on a function declaration, on the declaration of a
//   function defined later, or on a variable, is T-M22 on the MSIL triples
//   and accepted by native x86_64 Linux and wasm32 (D11). One that starts
//   with '__msil_mc_ctor.' or '__msil_mc_dtor.', the names of the forwarders
//   that clang's CodeGen defines for constructor and destructor managed
//   calls, is T-M22's forwarder form '... (a managed-call forwarder)', on the
//   same three kinds of declaration (section 12, the row '5.20.3's text
//   table gains T-M19 to T-M23 (...)'). '#pragma redefine_extname' takes
//   identifiers, which never hold the '.', so clang ignores that form
//   everywhere with its own warning and the name is never reserved.
// - AFTER, on every MSIL triple of these RUN lines: '#pragma weak f' after a
//   managed-call declaration of f is T-M6 ('msil_method' cannot be combined
//   with 'weak') at the pragma's identifier, and the pragma adds no 'weak',
//   so a later plain redeclaration, or one that repeats msil_method, reports
//   nothing more (exactly one error per function). msil_method first written
//   on a redeclaration that follows a definition (a body, an 'alias'
//   attribute, or the alias that '#pragma weak f = g' declares) is T-M14's
//   'must appear on the first declaration', with the note 'first declared
//   here' at the definition; with msil_catch beside it the one error is
//   still the msil_method form (section 12, the row '5.20.3's hook list,
//   5.13.5's MC-1 exemption text and MC-1's review focus 'Hooks only (L2)'
//   amended again (...)'). clang used to drop both attributes there with
//   only its 'attribute declaration must precede definition' warning.
// The texts never carry the '__msil_mc.' prefix of the IR name (MS7).

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify=expected,lnx,host %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -std=c17 -fsyntax-only -verify=expected,lnx,host %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -verify=expected,win,host,coff %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -std=c17 -fsyntax-only -verify=expected,win,host,coff %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify=expected,lnx,host %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -verify=expected,lnx,p64 %s
// RUN: %clang_cc1 -triple msil32-unknown-dotnet -std=c17 -fsyntax-only -verify=expected,lnx,p32 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c17 -fsyntax-only -DNATIVE -verify=native %s
// RUN: %clang_cc1 -triple wasm32-unknown-unknown -std=c17 -fsyntax-only -DNATIVE -verify=native %s
// RUN: not %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -DCATCH_LATE %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LATE --implicit-check-not="__msil_mc."
// RUN: not %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -DCATCH_LATE %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LATE --implicit-check-not="__msil_mc."
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -DINTERNAL -Wundefined-internal -verify=internal %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -DINTERNAL -Wundefined-internal -verify=internal %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -DINTERNAL -Wundefined-internal -verify=internal %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -DUNUSED -Wunused-function -Wunneeded-internal-declaration -verify=unused %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -DUNUSED -Wunused-function -Wunneeded-internal-declaration -verify=unused %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -DUNUSED -Wunused-function -Wunneeded-internal-declaration -verify=unused %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -DUNUSED -Wall -verify=unused %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -DUNUSED -Wall -verify=unused %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -DUNUSED -Wall -verify=unused %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c89 -fsyntax-only -DIMPLICIT -verify=implicit %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c89 -fsyntax-only -DIMPLICIT -verify=implicit %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -std=c17 -fsyntax-only -DDLL -verify=dllwin %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fms-extensions -std=c17 -fsyntax-only -DDLL -verify=dllwin %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -std=c17 -fsyntax-only -DDLL -verify=dllwin %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c17 -fsyntax-only -DDLL -verify=dllwin %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -DDLL -verify=dllelse %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -std=c17 -fsyntax-only -DDLL -verify=dllelse %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -DDLL -verify=dllelse %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -DDLL -verify=dllelse %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fdeclspec -std=c17 -fsyntax-only -DDLL -verify=dlldeclspec %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -std=c17 -fsyntax-only -DIFUNC -verify=ifunc %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c17 -fsyntax-only -DIFUNC -verify=ifunc %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -DIFUNC -verify=noifunc %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -DPRINT -ast-print %s -o %t.print.c
// RUN: FileCheck %s --check-prefix=PRINT --input-file=%t.print.c
// RUN: echo "// reparse-no-diagnostics" > %t.reparse.c
// RUN: cat %t.print.c >> %t.reparse.c
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify=reparse %t.reparse.c
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -DLABEL -verify=label,pragma %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -DLABEL -verify=label,pragma %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -DLABEL -verify=label,pragma %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c17 -fsyntax-only -DLABEL -verify=pragma %s
// RUN: %clang_cc1 -triple wasm32-unknown-unknown -std=c17 -fsyntax-only -DLABEL -verify=pragma %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -std=c17 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -std=c17 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -std=c17 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c17 -fsyntax-only -DAFTER -verify=after %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -DAFTER -verify=after %s

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __CHAR16_TYPE__ char16_t;
typedef __CHAR32_TYPE__ char32_t;
typedef __WCHAR_TYPE__ wchar_t;
typedef struct __dn_object *dn_handle; // a handle type: a pointer to an incomplete struct
struct complete { int x; };            // a pointer to it is not a handle type

#if defined(NATIVE)

#if __has_attribute(msil_method) || __has_attribute(msil_catch)
#error "msil_method and msil_catch exist only on MSIL triples"
#endif
#if __has_c_attribute(clang::msil_method) || __has_c_attribute(clang::msil_catch)
#error "msil_method and msil_catch exist only on MSIL triples"
#endif

int n_abs(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // native-warning {{unknown attribute 'msil_method' ignored}}
int n_div(int a, int *err) __attribute__((msil_catch("System.DivideByZeroException", 33))); // native-warning {{unknown attribute 'msil_catch' ignored}}
[[clang::msil_method("System.Math", "Abs", "int32(int32)")]] int n_abs2(int x); // native-warning {{unknown attribute 'clang::msil_method' ignored}}

#elif defined(CATCH_LATE)

// LATE: error: 'msil_catch' must appear on the first declaration of 'late_catch'{{$}}
// LATE: note: first declared here{{$}}
int late_catch(int a, int *e) __attribute__((msil_method("System.Math", "Max", "int32(int32, int32*)")));
int late_catch(int a, int *e) __attribute__((msil_catch("System.OverflowException", 75)));

#elif defined(INTERNAL)

// internal-no-diagnostics
// Internal linkage: the backend defines the K5 stub of a managed-call
// declaration, so a 'static' declaration that is used but never defined is
// not 'undefined' (-verify allows no diagnostic at all here).
static int i_static(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
static _Bool i_static_exists(const char *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
static void i_static_catch(const char *path, int *err) __attribute__((msil_method("System.IO.File", "Delete", "void(string)"), msil_catch("System.IO.IOException", 5)));
int main(void) {
  int err = 0;
  i_static_catch("y", &err);
  return i_static(-1) + i_static_exists("x") + err;
}

#elif defined(UNUSED)

// No warning: constructor or destructor on the first declaration (before or
// after msil_method; a priority; a result, which the forwarder drops; the
// [[...]] spelling).
static void u_init(void) __attribute__((constructor, msil_method("System.GC", "Collect", "void()")));
static void u_fini(void) __attribute__((msil_method("System.GC", "Collect", "void()"), destructor));
static long long u_ts(void) __attribute__((constructor(101), msil_method("System.Diagnostics.Stopwatch", "GetTimestamp", "int64()", "System")));
[[gnu::destructor]] [[clang::msil_method("System.GC", "Collect", "void()")]] static void u_c23(void);
// No warning: constructor or destructor added by a later redeclaration, or
// inherited by a later plain one (the last declaration of u_re_dtor omits
// 'static', which C allows: the function keeps its internal linkage).
static void u_re_ctor(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
static void u_re_ctor(void) __attribute__((constructor));
static void u_re_dtor(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
static void u_re_dtor(void) __attribute__((destructor));
void u_re_dtor(void);
static void u_inherit(void) __attribute__((constructor, msil_method("System.GC", "Collect", "void()")));
static void u_inherit(void);
// Native clang's warning: a static managed call with neither attribute, and
// a static declaration with constructor but no msil_method (nothing defines
// it); a static constructor definition gets none.
static void plain(void) __attribute__((msil_method("System.GC", "Collect", "void()"))); // unused-warning-re {{{{^}}unused function 'plain'{{$}}}}
static void n_ctor_decl(void) __attribute__((constructor)); // unused-warning-re {{{{^}}unused function 'n_ctor_decl'{{$}}}}
static void n_ctor_def(void) __attribute__((constructor));
static void n_ctor_def(void) {}
// -Wunneeded-internal-declaration: an unevaluated operand is the only
// reference. No warning with constructor or destructor; native clang's
// warning with neither.
static int u_ref_ctor(void) __attribute__((constructor, msil_method("System.Environment", "get_ProcessorCount", "int32()")));
static int u_ref_dtor(void) __attribute__((destructor, msil_method("System.Environment", "get_ProcessorCount", "int32()")));
static int u_ref_plain(void) __attribute__((msil_method("System.Environment", "get_ProcessorCount", "int32()"))); // unused-warning-re {{{{^}}function 'u_ref_plain' is not needed and will not be emitted{{$}}}}
int u_sizes(void) { return (int)(sizeof(u_ref_ctor()) + sizeof(u_ref_dtor()) + sizeof(u_ref_plain())); }

#elif defined(IMPLICIT)

// C89 declares 'g' implicitly at its call, so the declaration with
// msil_method is a redeclaration (T-M14, the note at the call).
int use_g(void) { return g(3); } // implicit-note-re {{{{^}}first declared here{{$}}}}
int g(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // implicit-error-re {{{{^}}'msil_method' must appear on the first declaration of 'g'{{$}}}}

#elif defined(DLL)

// dllwin-no-diagnostics
__declspec(dllexport) int x_exp1(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // dllelse-error-re {{{{^}}'__declspec' attributes are not enabled; use '-fdeclspec' or '-fms-extensions' to enable support for __declspec attributes{{$}}}} dlldeclspec-warning-re {{{{^}}__declspec attribute 'dllexport' is not supported{{$}}}}
int x_exp2(int x) __attribute__((dllexport, msil_method("System.Math", "Sign", "int32(int32)"))); // dllelse-warning-re {{{{^}}unknown attribute 'dllexport' ignored{{$}}}} dlldeclspec-warning-re {{{{^}}unknown attribute 'dllexport' ignored{{$}}}}
__declspec(dllimport) int x_imp1(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // dllelse-error-re {{{{^}}'__declspec' attributes are not enabled; use '-fdeclspec' or '-fms-extensions' to enable support for __declspec attributes{{$}}}} dlldeclspec-warning-re {{{{^}}__declspec attribute 'dllimport' is not supported{{$}}}}
int x_imp2(int x) __attribute__((dllimport, msil_method("System.Math", "Sign", "int32(int32)"))); // dllelse-warning-re {{{{^}}unknown attribute 'dllimport' ignored{{$}}}} dlldeclspec-warning-re {{{{^}}unknown attribute 'dllimport' ignored{{$}}}}
// With constructor and destructor, and unused.
__declspec(dllexport) void x_boot(void) __attribute__((constructor(300), destructor(500), msil_method("System.GC", "Collect", "void()"))); // dllelse-error-re {{{{^}}'__declspec' attributes are not enabled; use '-fdeclspec' or '-fms-extensions' to enable support for __declspec attributes{{$}}}} dlldeclspec-warning-re {{{{^}}__declspec attribute 'dllexport' is not supported{{$}}}}
int use_dll(void) { return x_exp1(1) + x_exp2(2) + x_imp1(3) + x_imp2(4); }

#elif defined(IFUNC)

void *resolve_abs(void);
int c_ifunc1(int x) __attribute__((ifunc("resolve_abs"), msil_method("System.Math", "Abs", "int32(int32)"))); // ifunc-error-re {{{{^}}'msil_method' cannot be combined with 'ifunc'{{$}}}} noifunc-warning-re {{{{^}}unknown attribute 'ifunc' ignored{{$}}}}
int c_ifunc2(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), ifunc("resolve_abs"))); // ifunc-error-re {{{{^}}'msil_method' cannot be combined with 'ifunc'{{$}}}} noifunc-warning-re {{{{^}}unknown attribute 'ifunc' ignored{{$}}}}

#elif defined(PRINT)

// The printed declarations are the written ones (one attribute per group,
// as -ast-print prints them).
// PRINT:      {{^}}int p_gnu(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));{{$}}
// PRINT-NEXT: {{^}}long long p_gnu_asm(void) __attribute__((msil_method("System.Diagnostics.Stopwatch", "GetTimestamp", "int64()", "System")));{{$}}
// PRINT-NEXT: {{^}}{{\[\[}}clang::msil_method("System.Math", "Sign", "int32(int32)")]] int p_c23(int x);{{$}}
// PRINT-NEXT: {{^}}{{\[\[}}clang::msil_method("System.Diagnostics.Stopwatch", "GetTimestamp", "int64()", "System")]] long long p_c23_asm(void);{{$}}
// PRINT-NEXT: {{^}}void p_catch(const char *path, int *err) __attribute__((msil_method("System.IO.File", "Delete", "void(string)"))) __attribute__((msil_catch("System.IO.IOException", 5)));{{$}}
// PRINT-NEXT: {{^}}long long p_catch_asm(void *s, long long off, int origin, int *err) __attribute__((msil_method("System.IO.Stream", "Seek", "instance int64(int64, valuetype System.IO.SeekOrigin)", "System.Runtime"))) __attribute__((msil_catch("System.IO.IOException", 5)));{{$}}
int p_gnu(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
long long p_gnu_asm(void) __attribute__((msil_method("System.Diagnostics.Stopwatch", "GetTimestamp", "int64()", "System")));
[[clang::msil_method("System.Math", "Sign", "int32(int32)")]] int p_c23(int x);
[[clang::msil_method("System.Diagnostics.Stopwatch", "GetTimestamp", "int64()", "System")]] long long p_c23_asm(void);
void p_catch(const char *path, int *err) __attribute__((msil_method("System.IO.File", "Delete", "void(string)"))) __attribute__((msil_catch("System.IO.IOException", 5)));
long long p_catch_asm(void *s, long long off, int origin, int *err) __attribute__((msil_method("System.IO.Stream", "Seek", "instance int64(int64, valuetype System.IO.SeekOrigin)", "System.Runtime"))) __attribute__((msil_catch("System.IO.IOException", 5)));

#elif defined(LABEL)

// T-M22 (D11): '__msil_mc.' names managed-call stubs.
int fake_count(void) __asm__("__msil_mc.widget_count"); // label-error-re {{{{^}}asm label '__msil_mc.widget_count' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
int fake_count(void) { return 5; }
int fake_decl(int x) __asm__("__msil_mc.widget_decl"); // label-error-re {{{{^}}asm label '__msil_mc.widget_decl' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
int fake_var __asm__("__msil_mc.widget_var") = 1; // label-error-re {{{{^}}asm label '__msil_mc.widget_var' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
extern int fake_ext __asm__("__msil_mc.widget_ext"); // label-error-re {{{{^}}asm label '__msil_mc.widget_ext' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
int fake_esc(void) __asm__("\01__msil_mc.widget_esc"); // label-error-re {{{{^}}asm label '__msil_mc.widget_esc' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
// T-M22's forwarder form: '__msil_mc_ctor.' and '__msil_mc_dtor.' name the
// forwarders of constructor and destructor managed calls.
void fwd_ctor_decl(void) __asm__("__msil_mc_ctor.widget_decl"); // label-error-re {{{{^}}asm label '__msil_mc_ctor.widget_decl' is reserved for the MSIL toolchain (a managed-call forwarder){{$}}}}
void fwd_ctor_def(void) __asm__("__msil_mc_ctor.widget_def"); // label-error-re {{{{^}}asm label '__msil_mc_ctor.widget_def' is reserved for the MSIL toolchain (a managed-call forwarder){{$}}}}
void fwd_ctor_def(void) {}
int fwd_ctor_var __asm__("__msil_mc_ctor.widget_var") = 1; // label-error-re {{{{^}}asm label '__msil_mc_ctor.widget_var' is reserved for the MSIL toolchain (a managed-call forwarder){{$}}}}
void fwd_dtor_decl(void) __asm__("__msil_mc_dtor.widget_decl"); // label-error-re {{{{^}}asm label '__msil_mc_dtor.widget_decl' is reserved for the MSIL toolchain (a managed-call forwarder){{$}}}}
void fwd_dtor_def(void) __asm__("__msil_mc_dtor.widget_def"); // label-error-re {{{{^}}asm label '__msil_mc_dtor.widget_def' is reserved for the MSIL toolchain (a managed-call forwarder){{$}}}}
void fwd_dtor_def(void) {}
int fwd_dtor_var __asm__("__msil_mc_dtor.widget_var") = 1; // label-error-re {{{{^}}asm label '__msil_mc_dtor.widget_var' is reserved for the MSIL toolchain (a managed-call forwarder){{$}}}}
void fwd_esc(void) __asm__("\01__msil_mc_ctor.widget_esc"); // label-error-re {{{{^}}asm label '__msil_mc_ctor.widget_esc' is reserved for the MSIL toolchain (a managed-call forwarder){{$}}}}
// Not the reserved prefixes.
int ok_label1(void) __asm__("__msil_mc_widget");
int ok_label2(void) __asm__("__msil_mcx.widget");
int ok_label3(void) __asm__("__msil_mc");
int ok_label4(void) __asm__("__msil_mc_ctor");
int ok_label5(void) __asm__("__msil_mc_dtorx.widget");
// pragma-warning-re@+1 {{{{^}}extra tokens at end of '#pragma redefine_extname' - ignored{{$}}}}
#pragma redefine_extname fake_prag __msil_mc.widget_count
int fake_prag(void);
int use_labels(void) { return fake_count() + fake_decl(1) + fake_var + fake_ext + fake_esc() + ok_label1() + ok_label2() + ok_label3() + ok_label4() + ok_label5() + fake_prag(); }

#elif defined(AFTER)

// '#pragma weak' after a managed-call declaration: T-M6 at the pragma's
// identifier, and no 'weak' for the redeclarations that follow.
__attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) int pw_after(int x);
#pragma weak pw_after // after-error-re {{{{^}}'msil_method' cannot be combined with 'weak'{{$}}}}
__attribute__((msil_method("System.Math", "Sign", "int32(int32)"))) int pw_redecl(int x);
#pragma weak pw_redecl // after-error-re {{{{^}}'msil_method' cannot be combined with 'weak'{{$}}}}
int pw_redecl(int x);
__attribute__((msil_method("System.Math", "Max", "int32(int32, int32)"))) int pw_again(int a, int b);
#pragma weak pw_again // after-error-re {{{{^}}'msil_method' cannot be combined with 'weak'{{$}}}}
__attribute__((msil_method("System.Math", "Max", "int32(int32, int32)"))) int pw_again(int a, int b);

// msil_method first written after a definition: T-M14's first form, with
// the note at the definition. A body; a body with msil_catch beside it (one
// error, the msil_method form); an 'alias' attribute; the alias that
// '#pragma weak f = g' declares (the note is at the pragma's 'f').
int ad_body(int x) { return x; } // after-note-re {{{{^}}first declared here{{$}}}}
__attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) int ad_body(int x); // after-error-re {{{{^}}'msil_method' must appear on the first declaration of 'ad_body'{{$}}}}
int ad_catch(int x, int *e) { return x; } // after-note-re {{{{^}}first declared here{{$}}}}
__attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 75))) int ad_catch(int x, int *e); // after-error-re {{{{^}}'msil_method' must appear on the first declaration of 'ad_catch'{{$}}}}
int ad_target(int x) { return x + 1; }
int ad_alias(int x) __attribute__((alias("ad_target"))); // after-note-re {{{{^}}first declared here{{$}}}}
__attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) int ad_alias(int x); // after-error-re {{{{^}}'msil_method' must appear on the first declaration of 'ad_alias'{{$}}}}
#pragma weak ad_pragma = ad_target // after-note-re {{{{^}}first declared here{{$}}}}
__attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) int ad_pragma(int x); // after-error-re {{{{^}}'msil_method' must appear on the first declaration of 'ad_pragma'{{$}}}}

#else // the MSIL triples

#if !__has_attribute(msil_method) || !__has_attribute(msil_catch)
#error "msil_method and msil_catch are MSIL attributes"
#endif
#if !__has_c_attribute(clang::msil_method) || !__has_c_attribute(clang::msil_catch)
#error "msil_method and msil_catch are MSIL attributes"
#endif

//===----------------------------------------------------------------------===//
// Accepted forms (no diagnostics).
//===----------------------------------------------------------------------===//

// 'val': integers of the CIL width (signedness not checked), float32 and
// float64, native int and native uint, unmanaged pointers.
int ok_abs(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
unsigned ok_maxu(int a, unsigned b) __attribute__((msil_method("System.Math", "Max", "uint32(uint32, uint32)")));
long long ok_abs64(long long x) __attribute__((msil_method("System.Math", "Abs", "int64(int64)")));
unsigned long long ok_max64(unsigned long long a, long long b) __attribute__((msil_method("System.Math", "Max", "uint64(uint64, uint64)")));
signed char ok_max8(signed char a, char b) __attribute__((msil_method("System.Math", "Max", "int8(int8, int8)")));
unsigned char ok_maxu8(unsigned char a, unsigned char b) __attribute__((msil_method("System.Math", "Max", "uint8(uint8, uint8)")));
short ok_max16(short a, unsigned short b) __attribute__((msil_method("System.Math", "Max", "int16(int16, int16)")));
unsigned short ok_maxu16(unsigned short a, unsigned short b) __attribute__((msil_method("System.Math", "Max", "uint16(uint16, uint16)")));
float ok_absf(float x) __attribute__((msil_method("System.Math", "Abs", "float32(float32)")));
double ok_sqrt(double x) __attribute__((msil_method("System.Math", "Sqrt", "float64(float64)")));
char16_t ok_upper(char16_t c) __attribute__((msil_method("System.Char", "ToUpperInvariant", "char(char)")));
void *ok_alloc(size_t n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "AllocHGlobal", "native int(native int)")));
intptr_t ok_alloc_i(intptr_t n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "AllocHGlobal", "native int(native int)")));
uintptr_t ok_alloc_u(ptrdiff_t n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "AllocHGlobal", "native int(native int)")));
char *ok_alloc_c(dn_handle n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "AllocHGlobal", "native int(native int)")));
uintptr_t ok_nuint(uintptr_t a, size_t b) __attribute__((msil_method("Ns.Native", "Mix", "native uint(native uint, native uint)")));
void ok_memcpy(void *dst, void *src, long long dstsize, long long n) __attribute__((msil_method("System.Buffer", "MemoryCopy", "void(void*, void*, int64, int64)")));
int ok_ptrs(int *p, struct complete *q, char **r) __attribute__((msil_method("Ns.Pointers", "Take", "int32(int32*, void*, uint8**)")));

// 'bool': _Bool or an integer of at most 32 bits, both ways.
_Bool ok_isdigit(char16_t c) __attribute__((msil_method("System.Char", "IsDigit", "bool(char)")));
int ok_isdigit_i(char16_t c) __attribute__((msil_method("System.Char", "IsDigit", "bool(char)")));
void ok_cursor(_Bool on) __attribute__((msil_method("System.Console", "set_CursorVisible", "void(bool)")));
void ok_cursor_i(int on) __attribute__((msil_method("System.Console", "set_CursorVisible", "void(bool)")));
void ok_cursor_u8(unsigned char on) __attribute__((msil_method("System.Console", "set_CursorVisible", "void(bool)")));

// 'enum': any integer type for a valuetype (msil-ld checks that it is an
// enum of that width); 'utf8' and 'err'; the catch list of 5.20.2's example.
dn_handle ok_fs_open(const char *path, int mode, int access, int share, int bufsize, int *err)
    __attribute__((msil_method("System.IO.FileStream", ".ctor",
                               "instance void(string, valuetype System.IO.FileMode, valuetype System.IO.FileAccess, "
                               "valuetype System.IO.FileShare, int32)"),
                   msil_catch("System.IO.FileNotFoundException", 2),
                   msil_catch("System.IO.DirectoryNotFoundException", 2),
                   msil_catch("System.UnauthorizedAccessException", 13),
                   msil_catch("System.IO.PathTooLongException", 36),
                   msil_catch("System.Text.DecoderFallbackException", 84),
                   msil_catch("System.IO.IOException", 5)));
long long ok_enum64(unsigned char kind) __attribute__((msil_method("Ns.Enums", "Widen", "int64(valuetype Ns.Kind)")));

// Strings: utf8 (also non-const), utf16, utf32; wchar_t is utf16 on the
// Windows triple and utf32 elsewhere, and accepted on both.
_Bool ok_exists(const char *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
_Bool ok_exists_nc(char *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
_Bool ok_exists16(const char16_t *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
_Bool ok_exists32(const char32_t *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
_Bool ok_existsw(const wchar_t *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
intptr_t ok_tohglobal(const char *s) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "StringToHGlobalUni", "native int(string)")));

// 'obj': string, object, class and array parameters and results as handles.
_Bool ok_exists_h(void *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
void ok_keepalive(intptr_t o) __attribute__((msil_method("System.GC", "KeepAlive", "void(object)")));
dn_handle ok_stdout(void) __attribute__((msil_method("System.Console", "OpenStandardOutput", "class System.IO.Stream()")));
void ok_copy(dn_handle src, int start, void *dst, int n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "Copy", "void(uint8[], int32, native int, int32)")));
uintptr_t ok_args(void) __attribute__((msil_method("System.Environment", "GetCommandLineArgs", "string[]()")));
void *ok_getenv(const char *name) __attribute__((msil_method("System.Environment", "GetEnvironmentVariable", "string(string)")));
dn_handle ok_bytes(int v) __attribute__((msil_method("System.BitConverter", "GetBytes", "uint8[](int32)")));

// 'boxed': value types other than enums and GCHandle, as opaque boxed
// handles; a value-type .ctor ('newobj', then 'box').
int ok_cmpdate(dn_handle a, void *b) __attribute__((msil_method("valuetype System.DateTime", "Compare", "int32(valuetype System.DateTime, valuetype System.DateTime)")));
dn_handle ok_now(void) __attribute__((msil_method("valuetype System.DateTime", "get_Now", "valuetype System.DateTime()")));
dn_handle ok_mkdate(long long ticks) __attribute__((msil_method("valuetype System.DateTime", ".ctor", "instance void(int64)")));

// 'this-val' (a property of a value type), 'this-obj' (a class, an
// interface, a property), 'this-gch' and 'gch'.
long long ok_ticks(dn_handle d) __attribute__((msil_method("valuetype System.DateTime", "get_Ticks", "instance int64()")));
void ok_write(dn_handle s, dn_handle a, int off, int n) __attribute__((msil_method("System.IO.Stream", "Write", "instance void(uint8[], int32, int32)")));
void ok_dispose(void *d) __attribute__((msil_method("System.IDisposable", "Dispose", "instance void()")));
int ok_length(uintptr_t a) __attribute__((msil_method("System.Array", "get_Length", "instance int32()")));
void ok_free(dn_handle h) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "Free", "instance void()")));
void *ok_target(void *h) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "get_Target", "instance object()")));
intptr_t ok_galloc(void *o) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "Alloc", "valuetype System.Runtime.InteropServices.GCHandle(object)")));
intptr_t ok_toptr(intptr_t h) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "ToIntPtr", "native int(valuetype System.Runtime.InteropServices.GCHandle)")));

// Constructors of classes and arrays (newarr: element types primitive,
// string, object and class).
dn_handle ok_mkex(const char *msg) __attribute__((msil_method("System.EntryPointNotFoundException", ".ctor", "instance void(string)")));
dn_handle ok_newbytes(int n) __attribute__((msil_method("uint8[]", ".ctor", "instance void(int32)")));
void *ok_newobjs(int n) __attribute__((msil_method("object[]", ".ctor", "instance void(int32)")));
void *ok_newstrs(int n) __attribute__((msil_method("string[]", ".ctor", "instance void(int32)")));
void *ok_newstreams(int n) __attribute__((msil_method("class System.IO.Stream[]", ".ctor", "instance void(int32)")));

// Not a delegate over a C function (DELEGATES in the header): a value
// type's '.ctor(object, native int)' (HandleRef), a class's '.ctor' of
// another shape, the delegate's own Invoke, and Marshal's other members,
// one of them with GetDelegateForFunctionPointer's parameters.
dn_handle ok_handleref(void *wrapper, intptr_t h) __attribute__((msil_method("valuetype System.Runtime.InteropServices.HandleRef", ".ctor", "instance void(object, native int)")));
dn_handle ok_handleref2(void *wrapper, intptr_t h) __attribute__((msil_method("valuetype System.Runtime.InteropServices.HandleRef", ".ctor", "instance void(class System.Object, native int)")));
dn_handle ok_ctor_other(intptr_t h, void *o) __attribute__((msil_method("Contoso.Callback", ".ctor", "instance void(native int, object)")));
void ok_invoke(dn_handle a) __attribute__((msil_method("System.Action", "Invoke", "instance void()")));
void *ok_ptrtostruct(void *p, dn_handle t) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "PtrToStructure", "object(native int, class System.Type)")));
intptr_t ok_fnptr(dn_handle d) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "GetFunctionPointerForDelegate", "native int(class System.Delegate)")));

// Fields (read-only), static and instance; an explicit ASSEMBLY; a nested
// type in a signature.
long long ok_freq(void) __attribute__((msil_method("System.Diagnostics.Stopwatch", "Frequency", "field int64", "System")));
_Bool ok_little(void) __attribute__((msil_method("System.BitConverter", "IsLittleEndian", "field bool")));
void *ok_empty(void) __attribute__((msil_method("System.String", "Empty", "field string")));
int ok_count(dn_handle h) __attribute__((msil_method("Ns.Holder", "Count", "instance field int32")));
long long ok_tsfield(dn_handle t) __attribute__((msil_method("valuetype Ns.Stamp", "Ticks", "instance field int64")));
void *ok_folder(int which) __attribute__((msil_method("System.Environment", "GetFolderPath", "string(valuetype System.Environment/SpecialFolder)")));
long long ok_ts(void) __attribute__((msil_method("System.Diagnostics.Stopwatch", "GetTimestamp", "int64()", "System")));
int ok_nested(void *h) __attribute__((msil_method("Ns.Outer/Inner", "get_Value", "instance int32()")));

// 'void'; a void function with msil_catch; the [[clang::...]] spelling;
// msil_catch before msil_method; separate attribute groups.
void ok_collect(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
void ok_delete(const char *path, int *err) __attribute__((msil_method("System.IO.File", "Delete", "void(string)"), msil_catch("System.IO.IOException", 5)));
[[clang::msil_method("System.IO.File", "Delete", "void(string)")]] [[clang::msil_catch("System.UnauthorizedAccessException", 13)]] [[clang::msil_catch("System.IO.IOException", 5)]] void ok_delete2(const char *path, int *err);
void ok_delete3(const char *path, int *err) __attribute__((msil_catch("System.IO.IOException", 5), msil_method("System.IO.File", "Delete", "void(string)")));
void ok_delete4(const char *path, int *err) __attribute__((msil_method("System.IO.File", "Delete", "void(string)"))) __attribute__((msil_catch("System.IO.IOException", 2147483647)));

// Redeclarations: repeating the same arguments, or omitting them.
int ok_redecl(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
int ok_redecl(int x);
int ok_redecl(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
void ok_redecl_c(const char *path, int *err) __attribute__((msil_method("System.IO.File", "Delete", "void(string)"), msil_catch("System.UnauthorizedAccessException", 13), msil_catch("System.IO.IOException", 5)));
void ok_redecl_c(const char *path, int *err);
void ok_redecl_c(const char *path, int *err) __attribute__((msil_method("System.IO.File", "Delete", "void(string)"), msil_catch("System.UnauthorizedAccessException", 13), msil_catch("System.IO.IOException", 5)));

// A call compiles as an ordinary call.
int use_ok(void) {
  int err = 0;
  ok_delete("x", &err);
  return ok_abs(-3) + ok_redecl(4) + err;
}

// The 'char' row and wchar_t: 16 bits only on the Windows triple.
int ok_wdigit(wchar_t c) __attribute__((msil_method("System.Char", "IsDigit", "bool(char)"))); // lnx-error-re {{{{^}}parameter 1 of 'ok_wdigit' has C type 'wchar_t'{{( \(aka '(unsigned )?int'\))?}}, which cannot carry CIL type 'char'{{$}}}}

// P32: msil32-unknown-dotnet's ILP32 widths (MS5: native int is 4 bytes;
// 'long' is 32 bits and the type of intptr_t and ptrdiff_t, and size_t is
// 'unsigned long'). 'long' carries int32 but not int64; 'long' and size_t
// carry native int and native uint, while 'long long', 'int' and 'unsigned
// int' do not; wchar_t is 32 bits, so 'const wchar_t *' carries 'string'
// (utf32) and wchar_t does not carry 'char'.
#ifdef __msil32__
long p32_l32(long x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
long p32_nint(long a, size_t b) __attribute__((msil_method("Ns.Native", "Mix", "native int(native int, native int)")));
size_t p32_nuint(long a, size_t b) __attribute__((msil_method("Ns.Native", "Mix", "native uint(native uint, native uint)")));
_Bool p32_existsw(const wchar_t *path) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
long p32_l64(long x) __attribute__((msil_method("System.Math", "Abs", "int64(int64)"))); // p32-error-re {{{{^}}parameter 1 of 'p32_l64' has C type 'long', which cannot carry CIL type 'int64'{{$}}}}
long p32_r64(int x) __attribute__((msil_method("System.Convert", "ToInt64", "int64(int32)"))); // p32-error-re {{{{^}}the result of 'p32_r64' has C type 'long', which cannot carry CIL type 'int64'{{$}}}}
void *p32_nint_ll(long long n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "AllocHGlobal", "native int(native int)"))); // p32-error-re {{{{^}}parameter 1 of 'p32_nint_ll' has C type 'long long', which cannot carry CIL type 'native int'{{$}}}}
long long p32_nint_rll(void) __attribute__((msil_method("Ns.Native", "Get", "native int()"))); // p32-error-re {{{{^}}the result of 'p32_nint_rll' has C type 'long long', which cannot carry CIL type 'native int'{{$}}}}
void *p32_nint_i(int n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "AllocHGlobal", "native int(native int)"))); // p32-error-re {{{{^}}parameter 1 of 'p32_nint_i' has C type 'int', which cannot carry CIL type 'native int'{{$}}}}
unsigned p32_nuint_u(unsigned n) __attribute__((msil_method("Ns.Native", "Id", "native uint(native uint)"))); // p32-error-re {{{{^}}parameter 1 of 'p32_nuint_u' has C type 'unsigned int', which cannot carry CIL type 'native uint'{{$}}}}
int p32_wdigit(wchar_t c) __attribute__((msil_method("System.Char", "IsDigit", "bool(char)"))); // p32-error-re {{{{^}}parameter 1 of 'p32_wdigit' has C type 'wchar_t' (aka 'int'), which cannot carry CIL type 'char'{{$}}}}
#endif

//===----------------------------------------------------------------------===//
// Rejected forms.
//===----------------------------------------------------------------------===//

// Not a function: clang's standard subject error.
int not_fn __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error {{'msil_method' attribute only applies to functions}}
int not_fn2 __attribute__((msil_catch("System.IO.IOException", 5))); // expected-error {{'msil_catch' attribute only applies to functions}}

// T-M1: a definition, and a definition that inherits the attribute (on one
// line, so either location of the error matches).
__attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) int d_own(int x) { return x; } // expected-error-re {{{{^}}'msil_method' is only allowed on a function declaration without a body{{$}}}}
int d_inherit(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); int d_inherit(int x) { return x; } // expected-error-re {{{{^}}'msil_method' is only allowed on a function declaration without a body{{$}}}}

// T-M12: no prototype, variadic, main, a library builtin.
int kr() __attribute__((msil_method("System.Environment", "get_ProcessorCount", "int32()"))); // expected-error-re {{{{^}}'msil_method' is only allowed on a prototyped, non-variadic function other than 'main'{{$}}}}
int va(int n, ...) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' is only allowed on a prototyped, non-variadic function other than 'main'{{$}}}}
int main(int argc, char **argv) __attribute__((msil_method("Ns.Program", "Run", "int32(int32, native int)"))); // expected-error-re {{{{^}}'msil_method' is only allowed on a prototyped, non-variadic function other than 'main'{{$}}}}
size_t strlen(const char *s) __attribute__((msil_method("Ns.Strings", "Length", "native int(native int)"))); // expected-error-re {{{{^}}'msil_method' is not allowed on 'strlen', which clang treats as a builtin function{{$}}}}

// T-M6: import_module (in both orders), import_name, alias, weak.
int alias_target(int x) { return x; }
int c_mod1(int x) __attribute__((import_module("m"), msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' cannot be combined with 'import_module'{{$}}}}
int c_mod2(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), import_module("m"))); // expected-error-re {{{{^}}'msil_method' cannot be combined with 'import_module'{{$}}}}
int c_name(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))) __attribute__((import_name("abs"))); // expected-error-re {{{{^}}'msil_method' cannot be combined with 'import_name'{{$}}}}
int c_alias(int x) __attribute__((alias("alias_target"), msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' cannot be combined with 'alias'{{$}}}}
__attribute__((weak, msil_method("System.Math", "Abs", "int32(int32)"))) int c_weak(int x); // expected-error-re {{{{^}}'msil_method' cannot be combined with 'weak'{{$}}}}

// T-M5: msil_catch without msil_method; a trailing parameter that is not
// 'int *'.
int c_alone(int x, int *err) __attribute__((msil_catch("System.OverflowException", 75))); // expected-error-re {{{{^}}'msil_catch' needs 'msil_method' on the same declaration{{$}}}}
int c_trail(int x, long *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 75))); // expected-error-re {{{{^}}the last parameter of 'c_trail' has C type 'long *'; 'msil_catch' needs a trailing 'int *' parameter{{$}}}}
int c_trail2(int x, unsigned *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 75))); // expected-error-re {{{{^}}the last parameter of 'c_trail2' has C type 'unsigned int *'; 'msil_catch' needs a trailing 'int *' parameter{{$}}}}

// T-M2: TYPE, MEMBER and SIGNATURE that do not parse, the forms of the
// fuller goal, and the MS3 inconsistencies; an empty ASSEMBLY (5.20.3 T-M2
// as amended in section 12 on 2026-11-04: its reason is pinned whole, and an
// absent ASSEMBLY stays the mscorlib default); a second ASSEMBLY string,
// reported at that string (D8).
int s_asm0(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)", ""))); // expected-error-re {{{{^}}'msil_method' assembly '' is not valid: it is empty{{$}}}}
int s_asm2(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)", "System", "System.Runtime"))); // expected-error-re {{{{^}}'msil_method' assembly 'System.Runtime' is not valid: only one assembly can be given{{$}}}}
[[clang::msil_method("System.Math", "Abs", "int32(int32)", "System", "")]] int s_asm2_c23(int x); // expected-error-re {{{{^}}'msil_method' assembly '' is not valid: only one assembly can be given{{$}}}}
int s_type0(int x) __attribute__((msil_method("", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type '' is not valid: {{.+$}}}}
int s_type1(void *l) __attribute__((msil_method("System.Collections.Generic.List`1<int32>", "get_Count", "instance int32()"))); // expected-error-re {{{{^}}'msil_method' type 'System.Collections.Generic.List`1<int32>' is not valid: {{.*}}{{(is|are)}} not supported for MSIL yet{{$}}}}
int s_type2(dn_handle h) __attribute__((msil_method("valuetype System.DateTime[]", ".ctor", "instance void(int32)"))); // expected-error-re {{{{^}}'msil_method' type 'valuetype System.DateTime[]' is not valid: {{.+$}}}}
int s_member0(int x) __attribute__((msil_method("System.Math", "", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' member '' is not valid: {{.+$}}}}
int s_sig0(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32"))); // expected-error-re {{{{^}}'msil_method' signature 'int32(int32' is not valid: {{.+$}}}}
int s_sig1(int x) __attribute__((msil_method("System.Math", "Abs", ""))); // expected-error-re {{{{^}}'msil_method' signature '' is not valid: {{.+$}}}}
int s_sig2(int x) __attribute__((msil_method("System.Math", "Abs", "int(int)"))); // expected-error-re {{{{^}}'msil_method' signature 'int(int)' is not valid: {{.+$}}}}
int s_sig3(void) __attribute__((msil_method("System.Math", "Abs", "int32(void)"))); // expected-error-re {{{{^}}'msil_method' signature 'int32(void)' is not valid: {{.+$}}}}
void s_byref(int *x) __attribute__((msil_method("System.Threading.Interlocked", "Increment", "int32(int32&)"))); // expected-error-re {{{{^}}'msil_method' signature 'int32(int32&)' is not valid: byref types are not supported for MSIL yet{{$}}}}
int s_gparam(int x) __attribute__((msil_method("Ns.Generic", "Id", "!!0(!!0)"))); // expected-error-re {{{{^}}'msil_method' signature '!!0(!!0)' is not valid: {{.*}}{{(is|are)}} not supported for MSIL yet{{$}}}}
int s_ginst(void *l) __attribute__((msil_method("Ns.Generic", "Count", "int32(class System.Collections.Generic.List`1<int32>)"))); // expected-error-re {{{{^}}'msil_method' signature 'int32(class System.Collections.Generic.List`1<int32>)' is not valid: generic instantiations are not supported for MSIL yet{{$}}}}
int s_ginst_vt(void *n) __attribute__((msil_method("Ns.Generic", "Count", "int32(valuetype System.Nullable`1<int32>)"))); // expected-error-re {{{{^}}'msil_method' signature 'int32(valuetype System.Nullable`1<int32>)' is not valid: generic instantiations are not supported for MSIL yet{{$}}}}
void s_vararg(int x) __attribute__((msil_method("Ns.Var", "Print", "vararg void(int32)"))); // expected-error-re {{{{^}}'msil_method' signature 'vararg void(int32)' is not valid: {{.*}}{{(is|are)}} not supported for MSIL yet{{$}}}}
void s_modopt(int x) __attribute__((msil_method("Ns.Mod", "Set", "void(int32 modopt(System.Runtime.CompilerServices.IsLong))"))); // expected-error-re {{{{^}}'msil_method' signature 'void(int32 modopt(System.Runtime.CompilerServices.IsLong))' is not valid: {{.*}}{{(is|are)}} not supported for MSIL yet{{$}}}}
void s_fnptr(void *f) __attribute__((msil_method("Ns.Fn", "Call", "void(method void*())"))); // expected-error-re {{{{^}}'msil_method' signature 'void(method void*())' is not valid: {{.*}}{{(is|are)}} not supported for MSIL yet{{$}}}}
void s_typedref(void *t) __attribute__((msil_method("Ns.Ref", "Take", "void(typedref)"))); // expected-error-re {{{{^}}'msil_method' signature 'void(typedref)' is not valid: {{.*}}{{(is|are)}} not supported for MSIL yet{{$}}}}
void s_mdarray(void *a) __attribute__((msil_method("Ns.Arr", "Take", "void(int32[,])"))); // expected-error-re {{{{^}}'msil_method' signature 'void(int32[,])' is not valid: {{.*}}{{(is|are)}} not supported for MSIL yet{{$}}}}
long long s_field(void) __attribute__((msil_method("System.Diagnostics.Stopwatch", "Frequency", "int64()"))); // ok: a method named like a field
long long s_fieldm(void) __attribute__((msil_method("System.Diagnostics.Stopwatch", ".ctor", "field int64"))); // expected-error-re {{{{^}}'msil_method' {{(member|signature)}} '{{(\.ctor|field int64)}}' is not valid: {{.+$}}}}
dn_handle s_ctor_static(const char *m) __attribute__((msil_method("System.EntryPointNotFoundException", ".ctor", "void(string)"))); // expected-error-re {{{{^}}'msil_method' signature 'void(string)' is not valid: {{.+$}}}}
int s_ctor_ret(dn_handle self) __attribute__((msil_method("System.Object", ".ctor", "instance int32()"))); // expected-error-re {{{{^}}'msil_method' signature 'instance int32()' is not valid: {{.+$}}}}
dn_handle s_arr_sig(long long n) __attribute__((msil_method("uint8[]", ".ctor", "instance void(int64)"))); // expected-error-re {{{{^}}'msil_method' signature 'instance void(int64)' is not valid: {{.+$}}}}
int s_arr_member(dn_handle a) __attribute__((msil_method("uint8[]", "get_Length", "instance int32()"))); // expected-error-re {{{{^}}'msil_method' {{(type|member)}} '{{(uint8\[\]|get_Length)}}' is not valid: {{.+$}}}}
int s_catch_type0(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("", 1))); // expected-error-re {{{{^}}'msil_catch' type '' is not valid: {{.+$}}}}
int s_catch_type1(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("valuetype System.DateTime", 1))); // expected-error-re {{{{^}}'msil_catch' type 'valuetype System.DateTime' is not valid: {{.+$}}}}
// NAMES in the header: a name with '<' that is not generic is an invalid
// name, as TYPE, as an msil_catch type and as MEMBER.
int s_module(int x) __attribute__((msil_method("<Module>", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type '<Module>' is not valid: '<Module>' is not a valid type name{{$}}}}
int s_nsmodule(int x) __attribute__((msil_method("Ns.<Module>", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type 'Ns.<Module>' is not valid: 'Ns.<Module>' is not a valid type name{{$}}}}
int s_catch_module(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("<Module>", 1))); // expected-error-re {{{{^}}'msil_catch' type '<Module>' is not valid: '<Module>' is not a valid type name{{$}}}}
// The same rule after 'valuetype', and for '`': generic arguments after a
// valid value type's name keep their reason, and a '`' that does not follow
// a valid name makes an invalid name, as TYPE (with and without 'valuetype'),
// as an msil_catch type and in SIGNATURE.
int s_vt_ginst(int x) __attribute__((msil_method("valuetype Ns.Generic<int32>", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type 'valuetype Ns.Generic<int32>' is not valid: generic instantiations are not supported for MSIL yet{{$}}}}
int s_vt_nullable(int x) __attribute__((msil_method("valuetype System.Nullable<int32>", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type 'valuetype System.Nullable<int32>' is not valid: generic instantiations are not supported for MSIL yet{{$}}}}
int s_vt_nullable1(int x) __attribute__((msil_method("valuetype System.Nullable`1<int32>", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type 'valuetype System.Nullable`1<int32>' is not valid: generic instantiations are not supported for MSIL yet{{$}}}}
int s_vt_module(int x) __attribute__((msil_method("valuetype <Module>", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type 'valuetype <Module>' is not valid: '<Module>' is not a valid type name{{$}}}}
int s_tick(int x) __attribute__((msil_method("`Foo", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type '`Foo' is not valid: '`Foo' is not a valid type name{{$}}}}
int s_ns_tick(int x) __attribute__((msil_method("Ns.`1", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type 'Ns.`1' is not valid: 'Ns.`1' is not a valid type name{{$}}}}
int s_anon(int x) __attribute__((msil_method("<>f__AnonymousType0`2", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type '<>f__AnonymousType0`2' is not valid: '<>f__AnonymousType0`2' is not a valid type name{{$}}}}
int s_iterator(int x) __attribute__((msil_method("Ns.E/<Select>d__5`2", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type 'Ns.E/<Select>d__5`2' is not valid: 'Ns.E/<Select>d__5`2' is not a valid type name{{$}}}}
int s_vt_tick(int x) __attribute__((msil_method("valuetype `Foo", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type 'valuetype `Foo' is not valid: '`Foo' is not a valid type name{{$}}}}
int s_catch_anon(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("<>f__AnonymousType0`2", 1))); // expected-error-re {{{{^}}'msil_catch' type '<>f__AnonymousType0`2' is not valid: '<>f__AnonymousType0`2' is not a valid type name{{$}}}}
void s_sig_tick(dn_handle h) __attribute__((msil_method("System.GC", "KeepAlive", "void(class `Foo)"))); // expected-error-re {{{{^}}'msil_method' signature 'void(class `Foo)' is not valid: '`Foo' is not a valid type name{{$}}}}
// Controls, pinned whole: a generic type, generic instantiations, and a
// MEMBER with a leading '`'.
int s_generic(int x) __attribute__((msil_method("List`1", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type 'List`1' is not valid: generic types are not supported for MSIL yet{{$}}}}
int s_ginst_list(int x) __attribute__((msil_method("List`1<int32>", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type 'List`1<int32>' is not valid: generic instantiations are not supported for MSIL yet{{$}}}}
int s_ginst_ns(int x) __attribute__((msil_method("Ns.Generic<int32>", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' type 'Ns.Generic<int32>' is not valid: generic instantiations are not supported for MSIL yet{{$}}}}
int s_member_tick(int x) __attribute__((msil_method("System.Math", "`Foo", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' member '`Foo' is not valid: '`Foo' is not a valid member name{{$}}}}
dn_handle s_clone(dn_handle self) __attribute__((msil_method("Ns.Record", "<Clone>$", "instance class Ns.Record()"))); // expected-error-re {{{{^}}'msil_method' member '<Clone>$' is not valid: '<Clone>$' is not a valid member name{{$}}}}
// NAMES in the header: after 'valuetype', a resolution scope, a byref and a
// pointer get their own reasons, not the invalid-name reason; an msil_catch
// type with a scope gets the same scope reason, and a byref one is not a
// reference type 'Ns.Name'.
long long s_vt_scope(dn_handle h) __attribute__((msil_method("valuetype [mscorlib]System.TimeSpan", "get_Ticks", "instance int64()"))); // expected-error-re {{{{^}}'msil_method' type 'valuetype [mscorlib]System.TimeSpan' is not valid: a type is named without a resolution scope; the assembly argument sets the scope of text output{{$}}}}
int s_vt_byref(dn_handle h) __attribute__((msil_method("valuetype System.Int32&", "GetHashCode", "instance int32()"))); // expected-error-re {{{{^}}'msil_method' type 'valuetype System.Int32&' is not valid: byref types are not supported for MSIL yet{{$}}}}
int s_vt_ptr(dn_handle h) __attribute__((msil_method("valuetype System.Int32*", "GetHashCode", "instance int32()"))); // expected-error-re {{{{^}}'msil_method' type 'valuetype System.Int32*' is not valid: a pointer type has no members; name the type that declares the member{{$}}}}
int s_catch_scope(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("[mscorlib]System.IO.IOException", 1))); // expected-error-re {{{{^}}'msil_catch' type '[mscorlib]System.IO.IOException' is not valid: a type is named without a resolution scope; the assembly argument sets the scope of text output{{$}}}}
int s_catch_byref(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.IO.IOException&", 1))); // expected-error-re {{{{^}}'msil_catch' type 'System.IO.IOException&' is not valid: an exception type is a reference type named 'Ns.Name'{{$}}}}
// DELEGATES in the header: a class's '.ctor' of a delegate constructor's
// shape, on SIGNATURE (quoted as written), for System.Action and for a
// third-party class, with 'class System.Object' and with other spacing; and
// Marshal::GetDelegateForFunctionPointer on MEMBER, with TYPE as a class and
// with 'valuetype'.
dn_handle s_dlg(dn_handle target, intptr_t fn) __attribute__((msil_method("System.Action", ".ctor", "instance void(object, native int)"))); // expected-error-re {{{{^}}'msil_method' signature 'instance void(object, native int)' is not valid: a class constructor with the signature '(object, native int)', the shape of a delegate's constructor, is not supported for MSIL yet{{$}}}}
void *s_dlg_obj(void *target, intptr_t fn) __attribute__((msil_method("System.Action", ".ctor", "instance void(class System.Object, native int)"))); // expected-error-re {{{{^}}'msil_method' signature 'instance void(class System.Object, native int)' is not valid: a class constructor with the signature '(object, native int)', the shape of a delegate's constructor, is not supported for MSIL yet{{$}}}}
dn_handle s_dlg_spaced(dn_handle target, intptr_t fn) __attribute__((msil_method("Contoso.Callback", ".ctor", "instance void (object,native int)"))); // expected-error-re {{{{^}}'msil_method' signature 'instance void (object,native int)' is not valid: a class constructor with the signature '(object, native int)', the shape of a delegate's constructor, is not supported for MSIL yet{{$}}}}
dn_handle s_dlg_fp(void *fn, dn_handle type) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "GetDelegateForFunctionPointer", "class System.Delegate(native int, class System.Type)"))); // expected-error-re {{{{^}}'msil_method' member 'GetDelegateForFunctionPointer' is not valid: delegates over function pointers are not supported for MSIL yet{{$}}}}
dn_handle s_dlg_fp_vt(void *fn, dn_handle type) __attribute__((msil_method("valuetype System.Runtime.InteropServices.Marshal", "GetDelegateForFunctionPointer", "class System.Delegate(native int, class System.Type)"))); // expected-error-re {{{{^}}'msil_method' member 'GetDelegateForFunctionPointer' is not valid: delegates over function pointers are not supported for MSIL yet{{$}}}}

// T-M3: one pair that no row of MS5 accepts, per declaration.
float t_f32(double x) __attribute__((msil_method("System.Math", "Abs", "float32(float32)"))); // expected-error-re {{{{^}}parameter 1 of 't_f32' has C type 'double', which cannot carry CIL type 'float32'{{$}}}}
double t_f64(float x) __attribute__((msil_method("System.Math", "Abs", "float64(float64)"))); // expected-error-re {{{{^}}parameter 1 of 't_f64' has C type 'float', which cannot carry CIL type 'float64'{{$}}}}
long long t_i64(int x) __attribute__((msil_method("System.Math", "Abs", "int64(int64)"))); // expected-error-re {{{{^}}parameter 1 of 't_i64' has C type 'int', which cannot carry CIL type 'int64'{{$}}}}
int t_i32(long long x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}parameter 1 of 't_i32' has C type 'long long', which cannot carry CIL type 'int32'{{$}}}}
short t_i16(int x) __attribute__((msil_method("System.Math", "Abs", "int16(int16)"))); // expected-error-re {{{{^}}parameter 1 of 't_i16' has C type 'int', which cannot carry CIL type 'int16'{{$}}}}
int t_char(int c) __attribute__((msil_method("System.Char", "IsDigit", "bool(char)"))); // expected-error-re {{{{^}}parameter 1 of 't_char' has C type 'int', which cannot carry CIL type 'char'{{$}}}}
void t_bool(long long b) __attribute__((msil_method("System.Console", "set_CursorVisible", "void(bool)"))); // expected-error-re {{{{^}}parameter 1 of 't_bool' has C type 'long long', which cannot carry CIL type 'bool'{{$}}}}
void *t_nint(int n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "AllocHGlobal", "native int(native int)"))); // expected-error-re {{{{^}}parameter 1 of 't_nint' has C type 'int', which cannot carry CIL type 'native int'{{$}}}}
void t_ptr(int p) __attribute__((msil_method("Ns.Pointers", "Take", "void(void*)"))); // expected-error-re {{{{^}}parameter 1 of 't_ptr' has C type 'int', which cannot carry CIL type 'void*'{{$}}}}
int t_enum(double m) __attribute__((msil_method("Ns.Enums", "Take", "int32(valuetype System.IO.FileMode)"))); // expected-error-re {{{{^}}parameter 1 of 't_enum' has C type 'double', which cannot carry CIL type 'valuetype System.IO.FileMode'{{$}}}}
void t_class(const char *s) __attribute__((msil_method("System.GC", "KeepAlive", "void(class System.IO.Stream)"))); // expected-error-re {{{{^}}parameter 1 of 't_class' has C type 'const char *', which cannot carry CIL type 'class System.IO.Stream'{{$}}}}
void t_object(float o) __attribute__((msil_method("System.GC", "KeepAlive", "void(object)"))); // expected-error-re {{{{^}}parameter 1 of 't_object' has C type 'float', which cannot carry CIL type 'object'{{$}}}}
void t_boxed(struct complete *d) __attribute__((msil_method("Ns.Dates", "Take", "void(valuetype System.DateTime)"))); // expected-error-re {{{{^}}parameter 1 of 't_boxed' has C type 'struct complete *', which cannot carry CIL type 'valuetype System.DateTime'{{$}}}}
void t_array(int n) __attribute__((msil_method("Ns.Arrays", "Take", "void(uint8[])"))); // expected-error-re {{{{^}}parameter 1 of 't_array' has C type 'int', which cannot carry CIL type 'uint8[]'{{$}}}}
void t_pos3(dn_handle s, dn_handle a, double off, int n) __attribute__((msil_method("System.IO.Stream", "Write", "instance void(uint8[], int32, int32)"))); // expected-error-re {{{{^}}parameter 3 of 't_pos3' has C type 'double', which cannot carry CIL type 'int32'{{$}}}}
const char *t_ret_str(void) __attribute__((msil_method("System.Environment", "get_MachineName", "string()"))); // expected-error-re {{{{^}}the result of 't_ret_str' has C type 'const char *', which cannot carry CIL type 'string'{{$}}}}
int t_ret_void(void) __attribute__((msil_method("System.GC", "Collect", "void()"))); // expected-error-re {{{{^}}the result of 't_ret_void' has C type 'int', which cannot carry CIL type 'void'{{$}}}}
void t_ret_int(void) __attribute__((msil_method("System.Environment", "get_ProcessorCount", "int32()"))); // expected-error-re {{{{^}}the result of 't_ret_int' has C type 'void', which cannot carry CIL type 'int32'{{$}}}}
int t_ret_class(void) __attribute__((msil_method("System.Console", "OpenStandardOutput", "class System.IO.Stream()"))); // expected-error-re {{{{^}}the result of 't_ret_class' has C type 'int', which cannot carry CIL type 'class System.IO.Stream'{{$}}}}
int t_this(int self) __attribute__((msil_method("System.Array", "get_Length", "instance int32()"))); // expected-error-re {{{{^}}the 'this' handle of 't_this' has C type 'int', which cannot carry CIL type '{{(class )?}}System.Array'{{$}}}}
// __int128 for an enum, both ways (D4, permanent), where the target has
// __int128 (msil32-unknown-dotnet has none).
#ifdef __SIZEOF_INT128__
int e128(__int128 d) __attribute__((msil_method("System.Convert", "ToInt32", "int32(valuetype System.DayOfWeek)"))); // expected-error-re {{{{^}}parameter 1 of 'e128' has C type '__int128', which cannot carry CIL type 'valuetype System.DayOfWeek'{{$}}}}
__int128 re128(int x) __attribute__((msil_method("System.Convert", "ToInt32", "valuetype System.DayOfWeek(int32)"))); // expected-error-re {{{{^}}the result of 're128' has C type '__int128', which cannot carry CIL type 'valuetype System.DayOfWeek'{{$}}}}
#endif
// _BitInt(N), whose storage width is not its value width (D19, temporary).
int abs32(_BitInt(32) x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}parameter 1 of 'abs32' has C type '_BitInt(32)', which cannot carry CIL type 'int32'{{$}}}}
long long abs64(_BitInt(64) x) __attribute__((msil_method("System.Math", "Abs", "int64(int64)"))); // expected-error-re {{{{^}}parameter 1 of 'abs64' has C type '_BitInt(64)', which cannot carry CIL type 'int64'{{$}}}}
void set_fg(_BitInt(24) c) __attribute__((msil_method("System.Console", "set_ForegroundColor", "void(valuetype System.ConsoleColor)"))); // expected-error-re {{{{^}}parameter 1 of 'set_fg' has C type '_BitInt(24)', which cannot carry CIL type 'valuetype System.ConsoleColor'{{$}}}}
_BitInt(8) tobool(int v) __attribute__((msil_method("System.Convert", "ToBoolean", "bool(int32)"))); // expected-error-re {{{{^}}the result of 'tobool' has C type '_BitInt(8)', which cannot carry CIL type 'bool'{{$}}}}
void cursor(_BitInt(8) on) __attribute__((msil_method("System.Console", "set_CursorVisible", "void(bool)"))); // expected-error-re {{{{^}}parameter 1 of 'cursor' has C type '_BitInt(8)', which cannot carry CIL type 'bool'{{$}}}}

// T-M4: the C parameter count against MS4's breakdown.
int a_example(dn_handle s, char16_t c, int *err) __attribute__((msil_method("System.String", "IndexOf", "instance int32(char, int32)"), msil_catch("System.ArgumentOutOfRangeException", 34))); // expected-error-re {{{{^}}'a_example' has 3 parameters; 'msil_method' needs 4 (a 'this' handle, 2 CIL parameters and the 'msil_catch' error code){{$}}}}
int a_static(int a, int b, int c) __attribute__((msil_method("System.Math", "Max", "int32(int32, int32)"))); // expected-error-re {{{{^}}'a_static' has 3 parameters; 'msil_method' needs 2 ({{.+}}){{$}}}}
dn_handle a_ctor(dn_handle self, const char *msg, int n) __attribute__((msil_method("Ns.Thing", ".ctor", "instance void(string, int32)"))); // expected-error-re {{{{^}}'a_ctor' has 3 parameters; 'msil_method' needs 2 ({{.+}}){{$}}}}
int a_this(int a, int b) __attribute__((msil_method("System.String", "IndexOf", "instance int32(char, int32)"))); // expected-error-re {{{{^}}'a_this' has 2 parameters; 'msil_method' needs 3 ({{.+}}){{$}}}}
// One C parameter is singular, and none stays plural (both pinned whole).
int a_one(int x) __attribute__((msil_method("System.Math", "Max", "int32(int32, int32)"))); // expected-error-re {{{{^}}'a_one' has 1 parameter; 'msil_method' needs 2 (2 CIL parameters){{$}}}}
int a_none(void) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'a_none' has 0 parameters; 'msil_method' needs 1 (1 CIL parameter){{$}}}}

// T-M11: the catch-all types (permanent, no 'yet').
int k_object(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.Object", 1))); // expected-error-re {{{{^}}catching 'System.Object' in 'msil_catch' is not supported for MSIL{{$}}}}
int k_exception(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.Exception", 1))); // expected-error-re {{{{^}}catching 'System.Exception' in 'msil_catch' is not supported for MSIL{{$}}}}
int k_system(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.SystemException", 1))); // expected-error-re {{{{^}}catching 'System.SystemException' in 'msil_catch' is not supported for MSIL{{$}}}}
int k_external(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.Runtime.InteropServices.ExternalException", 1))); // expected-error-re {{{{^}}catching 'System.Runtime.InteropServices.ExternalException' in 'msil_catch' is not supported for MSIL{{$}}}}
int k_seh(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.Runtime.InteropServices.SEHException", 1))); // expected-error-re {{{{^}}catching 'System.Runtime.InteropServices.SEHException' in 'msil_catch' is not supported for MSIL{{$}}}}

// T-M15: codes out of range, a type listed twice.
int r_zero(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 0))); // expected-error-re {{{{^}}'msil_catch' code 0 is not in the range 1 to 2147483647{{$}}}}
int r_neg(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", -1))); // expected-error-re {{{{^}}'msil_catch' code -1 is not in the range 1 to 2147483647{{$}}}}
int r_big(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 2147483648LL))); // expected-error-re {{{{^}}'msil_catch' code 2147483648 is not in the range 1 to 2147483647{{$}}}}
int r_twice(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 75), msil_catch("System.OverflowException", 34))); // expected-error-re {{{{^}}'msil_catch' lists 'System.OverflowException' twice{{$}}}}

// T-M14: msil_method first written on a redeclaration; a redeclaration
// whose msil_method or msil_catch arguments differ (a different member, a
// different code, the same entries in another order).
int m_late(int x); // expected-note-re {{{{^}}first declared here{{$}}}}
int m_late(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' must appear on the first declaration of 'm_late'{{$}}}}
int m_diff(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-note-re {{{{^}}first declared here{{$}}}}
int m_diff(int x) __attribute__((msil_method("System.Math", "Sign", "int32(int32)"))); // expected-error-re {{{{^}}'msil_method' on this declaration of 'm_diff' differs from its first declaration{{$}}}}
long long m_asm(void) __attribute__((msil_method("System.Diagnostics.Stopwatch", "GetTimestamp", "int64()", "System"))); // expected-note-re {{{{^}}first declared here{{$}}}}
long long m_asm(void) __attribute__((msil_method("System.Diagnostics.Stopwatch", "GetTimestamp", "int64()"))); // expected-error-re {{{{^}}'msil_method' on this declaration of 'm_asm' differs from its first declaration{{$}}}}
int m_code(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 75))); // expected-note-re {{{{^}}first declared here{{$}}}}
int m_code(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 34))); // expected-error-re {{{{^}}'msil_catch' on this declaration of 'm_code' differs from its first declaration{{$}}}}
int m_order(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.OverflowException", 75), msil_catch("System.ArithmeticException", 33))); // expected-note-re {{{{^}}first declared here{{$}}}}
int m_order(int x, int *err) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"), msil_catch("System.ArithmeticException", 33), msil_catch("System.OverflowException", 75))); // expected-error-re {{{{^}}'msil_catch' on this declaration of 'm_order' differs from its first declaration{{$}}}}
// Chains of three or more declarations (MS2): a later declaration is
// compared with the first one written, and the note is at that first
// declaration, pinned with an '@-N' offset from the error's line: 'same,
// same, different' for msil_method and for msil_catch (the note at the
// first declaration's first msil_catch), 'same, same, omitted, different',
// and 'Abs, Sign, Abs', which gets exactly one error, at the second
// declaration, while the third, which repeats the first, gets none.
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

// constructor and destructor (contract D6). Accepted on every triple: no
// parameters, a priority, a result (the forwarder drops it), 'static', both
// spellings, and both attributes on one declaration.
void k_init(void) __attribute__((constructor, msil_method("System.GC", "Collect", "void()")));
void k_init101(void) __attribute__((constructor(101), msil_method("System.GC", "Collect", "void()")));
long long k_ts(void) __attribute__((constructor, msil_method("System.Diagnostics.Stopwatch", "GetTimestamp", "int64()", "System")));
void k_fini(void) __attribute__((destructor, msil_method("System.GC", "Collect", "void()")));
void k_both(void) __attribute__((constructor(300), destructor(500), msil_method("System.GC", "Collect", "void()")));
static void k_static(void) __attribute__((constructor, msil_method("System.GC", "Collect", "void()")));
[[gnu::constructor]] [[clang::msil_method("System.GC", "Collect", "void()")]] void k_c23(void);
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
void k_re(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
void k_re(void) __attribute__((constructor));
void k_re(void);
#ifndef __msil32__
void k_re_arg(double d) __attribute__((msil_method("Ns.Startup", "Init", "void(float64)")));
void k_re_arg(double d) __attribute__((constructor)); // host-error-re {{{{^}}global constructor 'k_re_arg' takes arguments, which MSIL does not pass{{$}}}} p64-error-re {{{{^}}constructor 'k_re_arg' takes arguments, which msil64-unknown-dotnet does not pass{{$}}}}
void k_re_arg(double d);
#endif
void k_re_catch(const char *path, int *err) __attribute__((msil_method("System.IO.File", "Delete", "void(string)"), msil_catch("System.IO.IOException", 5)));
void k_re_catch(const char *path, int *err) __attribute__((destructor)); // expected-error-re {{{{^}}'msil_catch' cannot be combined with 'destructor'{{$}}}}

// Both spellings report the same texts.
[[clang::msil_method("System.Math", "Abs", "float32(float32)")]] float b_c23(double x); // expected-error-re {{{{^}}parameter 1 of 'b_c23' has C type 'double', which cannot carry CIL type 'float32'{{$}}}}
[[clang::msil_catch("System.IO.IOException", 5)]] int b_c23_alone(int x, int *err); // expected-error-re {{{{^}}'msil_catch' needs 'msil_method' on the same declaration{{$}}}}
[[clang::msil_method("System.Math", "Abs", "int32(int32)")]] [[clang::msil_catch("System.Exception", 1)]] int b_c23_all(int x, int *err); // expected-error-re {{{{^}}catching 'System.Exception' in 'msil_catch' is not supported for MSIL{{$}}}}

// A CODE that is not an integer constant expression: clang's own error and
// nothing else. The entry still belongs to the declaration, so the trailing
// 'int *' is the 'msil_catch' error code and MS4 counts it (no T-M4).
_Bool q_code(const char *p, int *err) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)"), msil_catch("System.IO.IOException", 1.5))); // expected-error {{'msil_catch' attribute requires an integer constant}}

#endif
