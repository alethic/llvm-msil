// A forwarder name that another declaration of the translation unit already
// uses (ImplementationPlan.md 5.20.3 T-M25 and 5.20.4's D6 forwarders, as
// amended in section 12, the row '5.20.3's text table gains T-M24 and T-M25,
// and 5.20.4 MR4 amended (...)' of 2026-11-06; milestone MC-1). A managed
// call with dllexport (the Windows triples), constructor or destructor gets
// a forwarder named '<s>', '__msil_mc_ctor.<s>' or '__msil_mc_dtor.<s>'
// (msil-method-forwarders.c). When that name is already a variable or a
// definition in the unit, clang's CodeGen reports, at the managed call,
//   '<dllexport|constructor|destructor>' on managed call '<f>' needs the forwarder symbol '<n>', which another declaration in this translation unit already uses
// with the note '<g>' declared here at that declaration, in place of
// upstream's 'definition with same mangled name '<n>' as another definition'
// and 'previous definition is here' (which -verify would report as
// unexpected), since the managed call has no definition.
// - var.c: a tentative definition 'int g' whose asm label is 'wguard'.
// - vardef.c: the same with an initializer.
// - fndef.c: a function definition (declaration and definition on one line,
//   so the note may name either).
// - later.c: the variable comes after the managed call and its call.
// - rext.c: '#pragma redefine_extname g wguard' gives the variable g the name.
// - declspec.c: __declspec(dllexport) (-fms-extensions) is 'dllexport' too.
// Each unit has one clash. Accepted: a function declaration that is not a
// definition may use the name, and its calls go to the forwarder:
// - fndecl.c: 'int other(int) __asm__("wguard")' beside the exported wguard
//   (the Windows triple): no diagnostic, one forwarder 'wguard', and other's
//   call is 'call i32 @wguard'.
// The names of the constructor and destructor forwarders hold a '.', which
// only an asm label can write, and Sema reserves them: a label that starts
// with '__msil_mc_ctor.' or '__msil_mc_dtor.' is T-M22's forwarder form,
// reported at the label (section 12, the row '5.20.3's text table gains
// T-M19 to T-M23 (...)'),
//   asm label '<n>' is reserved for the MSIL toolchain (a managed-call forwarder)
// so T-M25's 'constructor' and 'destructor' forms stay in CodeGen only as a
// guard that no source reaches:
// - ctor.c, dtor.c and ctordecl.c, on every triple: '__msil_mc_ctor.k' on a
//   variable, '__msil_mc_dtor.k' on a function defined on the same line, and
//   '__msil_mc_ctor.k' on a function declaration that is not a definition
//   (no longer accepted), each beside the constructor or destructor managed
//   call k: one error, at the label.

// RUN: rm -rf %t && split-file %s %t
// DEFINE: %{t} =
// DEFINE: %{v} = %clang_cc1 -triple %{t} -emit-llvm-only -verify
// DEFINE: %{startup} = %{v} %t/ctor.c && %{v} %t/dtor.c && %{v} %t/ctordecl.c
// REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
// RUN: %{v} %t/var.c && %{v} %t/vardef.c && %{v} %t/fndef.c && %{v} %t/later.c && %{v} %t/rext.c
// RUN: %{v} -fms-extensions %t/declspec.c
// RUN: %clang_cc1 -triple %{t} -verify -emit-llvm -o %t/fndecl.ll %t/fndecl.c
// RUN: FileCheck %s --check-prefix=FNDECL --input-file=%t/fndecl.ll
// RUN: grep "^define .*@wguard(" %t/fndecl.ll | count 1
// RUN: %{startup}
// REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
// RUN: %{startup}
// REDEFINE: %{t} = msil64-unknown-dotnet
// RUN: %{startup}

// FNDECL:          {{^}}define {{.*}}i32 @use(
// FNDECL:          {{^}}  %{{.+}} = call i32 @wguard(i32 noundef %{{.+}}){{$}}
// FNDECL:          {{^}}  %{{.+}} = call i32 @__msil_mc.wguard(i32 noundef %{{.+}}){{$}}

//--- var.c
int g __asm__("wguard"); // expected-note-re {{{{^}}'g' declared here{{$}}}}
int wguard(int) __attribute__((dllexport, msil_method("Contoso.Widget", "Guarded", "int32(int32)"))); // expected-error-re {{{{^}}'dllexport' on managed call 'wguard' needs the forwarder symbol 'wguard', which another declaration in this translation unit already uses{{$}}}}
int use(int x) { return wguard(x) + g; }

//--- vardef.c
int g __asm__("wguard") = 1; // expected-note-re {{{{^}}'g' declared here{{$}}}}
int wguard(int) __attribute__((dllexport, msil_method("Contoso.Widget", "Guarded", "int32(int32)"))); // expected-error-re {{{{^}}'dllexport' on managed call 'wguard' needs the forwarder symbol 'wguard', which another declaration in this translation unit already uses{{$}}}}
int use(int x) { return wguard(x) + g; }

//--- fndef.c
int g(int x) __asm__("wguard"); int g(int x) { return x; } // expected-note-re {{{{^}}'g' declared here{{$}}}}
int wguard(int) __attribute__((dllexport, msil_method("Contoso.Widget", "Guarded", "int32(int32)"))); // expected-error-re {{{{^}}'dllexport' on managed call 'wguard' needs the forwarder symbol 'wguard', which another declaration in this translation unit already uses{{$}}}}
int use(int x) { return wguard(x); }

//--- later.c
int wguard(int) __attribute__((dllexport, msil_method("Contoso.Widget", "Guarded", "int32(int32)"))); // expected-error-re {{{{^}}'dllexport' on managed call 'wguard' needs the forwarder symbol 'wguard', which another declaration in this translation unit already uses{{$}}}}
int use(int x) { return wguard(x); }
int g __asm__("wguard") = 1; // expected-note-re {{{{^}}'g' declared here{{$}}}}

//--- rext.c
#pragma redefine_extname g wguard
int g = 1; // expected-note-re {{{{^}}'g' declared here{{$}}}}
int wguard(int) __attribute__((dllexport, msil_method("Contoso.Widget", "Guarded", "int32(int32)"))); // expected-error-re {{{{^}}'dllexport' on managed call 'wguard' needs the forwarder symbol 'wguard', which another declaration in this translation unit already uses{{$}}}}
int use(int x) { return wguard(x) + g; }

//--- declspec.c
int g __asm__("wguard") = 1; // expected-note-re {{{{^}}'g' declared here{{$}}}}
__declspec(dllexport) int wguard(int) __attribute__((msil_method("Contoso.Widget", "Guarded", "int32(int32)"))); // expected-error-re {{{{^}}'dllexport' on managed call 'wguard' needs the forwarder symbol 'wguard', which another declaration in this translation unit already uses{{$}}}}
int use(int x) { return wguard(x) + g; }

//--- ctor.c
int other __asm__("__msil_mc_ctor.k") = 1; // expected-error-re {{{{^}}asm label '__msil_mc_ctor.k' is reserved for the MSIL toolchain (a managed-call forwarder){{$}}}}
void k(void) __attribute__((constructor, msil_method("System.GC", "Collect", "void()")));
int use(void) { k(); return other; }

//--- dtor.c
void other(void) __asm__("__msil_mc_dtor.k"); void other(void) {} // expected-error-re {{{{^}}asm label '__msil_mc_dtor.k' is reserved for the MSIL toolchain (a managed-call forwarder){{$}}}}
void k(void) __attribute__((destructor, msil_method("System.GC", "Collect", "void()")));
void use(void) { k(); other(); }

//--- fndecl.c
// expected-no-diagnostics
int other(int) __asm__("wguard");
int wguard(int) __attribute__((dllexport, msil_method("Contoso.Widget", "Guarded", "int32(int32)")));
int use(int x) { return other(x) + wguard(x); }

//--- ctordecl.c
void other(void) __asm__("__msil_mc_ctor.k"); // expected-error-re {{{{^}}asm label '__msil_mc_ctor.k' is reserved for the MSIL toolchain (a managed-call forwarder){{$}}}}
void k(void) __attribute__((constructor, msil_method("System.GC", "Collect", "void()")));
void use(void) { other(); }
