// The C import attributes on MSIL triples (ImplementationPlan.md 5.10.7, M13;
// decision D14 as amended, user decision Q35): WebAssembly's
// __attribute__((import_module("<module>"))) and
// __attribute__((import_name("<entry>"))), and their C23 spellings
// [[clang::import_module(...)]] and [[clang::import_name(...)]], are accepted
// on every MSIL triple (__has_attribute and __has_c_attribute are 1). CodeGen
// gives a function declaration without a body the string function attributes
// "msil-import-module" and "msil-import-name", the Phase-1 IR keys that the
// backend turns into 'pinvokeimpl' (llvm/test/CodeGen/MSIL/pinvoke.ll), and an
// external variable declaration ('extern', no initializer) the same keys as
// GlobalVariable attributes (the data imports of 5.10.12). Either attribute
// alone gives only its own key; both give both.
//
// The keys are those of the entity's most recent declaration, which inherits
// the import attributes of the earlier ones (Sema merges them), so the two
// keys may come from different declarations, and an attribute written on a
// redeclaration after a use still reaches the IR declaration and -S (section
// 12, 2026-10-22, review-r2 finding 1): r_fn is used by r_early() before its
// redeclaration adds the name; r_inl is used only by the static inline
// r_wrap, whose definition CodeGen defers to the end of the translation unit
// (r_caller uses it after the redeclaration), so the IR declaration is
// created only then; and the external variable r_var is used by r_vearly()
// before its redeclaration adds the module. A block-scope redeclaration counts
// too (section 12, 2026-10-22, the M13.a fix-loop residual): the function b_fn
// is used by b_early() before b_late() redeclares it inside its body with the
// name, and the external variable b_var is used by b_vearly() before b_vlate()
// redeclares it ('extern', inside its body) with the module. CodeGen never
// emits a block-scope declaration, but it is the entity's most recent
// declaration, so the IR declaration still carries both keys.
//
// Definitions get neither key. Sema is target-neutral (SemaWasm), so on MSIL
// triples it warns as on wasm32: an attribute added after a function's
// definition ('attribute declaration must precede definition'), and an
// attribute on a variable that is or becomes a definition (a tentative
// definition, an initializer, or a later definition of an 'extern'
// declaration; 'import module|name cannot be applied to a variable with a
// definition'). Sema accepts an attribute on the declarator of a function
// definition itself (d_own), because it is applied before the body is
// parsed; CodeGen gives that definition no key and warns with Sema's text
// for a function ('import module cannot be applied to a function with a
// definition'; wasm32 reports a CodeGen error there instead). An attribute
// that a later definition inherits from a declaration (d_before) is dropped
// silently, as on wasm32. That holds too when the declaration is used before
// the definition (uf, vuf, vn and the static s_fwd, all referenced from
// early() above their definitions): the use creates the IR function or
// variable from the attributed declaration, with its keys, and the
// definition must remove them; vuf and vn get Sema's variable warning on their
// 'extern' lines, as v_redef does. Every value a definition is given starts
// with 'def_', and no 'def_' string may reach the IR.
//
// The NMOD and NNAME runs count the attribute groups that carry each key:
// eleven declarations have a module (f_mod, f_both, f_c23, v_mod, v_both,
// v_c23, r_fn, r_inl, r_var, b_fn, b_var) and ten a name (f_name, f_both,
// f_c23, v_name, v_both, r_fn, r_inl, r_var, b_fn, b_var); each has its own
// values, so each key is in its own group and a stray key on any other
// declaration or definition changes a count.
//
// export_name stays wasm-only (M13.a): __has_attribute(export_name) is 0 on
// the MSIL triples and on native x86_64 Linux and 1 on wasm32
// (has_export_name), and the MSIL triples ignore it as an unknown attribute
// (x_exp).
//
// Controls: native x86_64 Linux does not know the attributes (__has_attribute
// is 0, nothing is emitted), and wasm32 keeps "wasm-import-module" and
// "wasm-import-name" (the variables and d_own are left out there, because
// wasm32 CodeGen rejects imports of address-space-0 variables and on
// definitions, clang/test/CodeGen/WebAssembly/wasm-import-error.c; so are the
// used-first functions, whose definitions keep the declaration's keys on
// wasm32, and the redeclared r_fn, r_inl, r_var, b_fn and b_var: wasm32 keeps
// the upstream gap there, an attribute added after a use is lost, and this
// test does not pin it).
//
// The C++ runs (-DCXX, through cc1 without -mconstructor-aliases, so that
// every structor variant is a definition of its own) compile only a
// constructor and a virtual destructor, each defined with one import
// attribute on its own declarator. The Itanium ABI emits them as C1 and C2,
// and D0, D1 and D2, the MSVC ABI as the constructor, ??1 and ??_G; each
// attribute still warns exactly once, as a C function does, and no variant
// gets a key (section 12, 2026-10-22, review-r2 finding 3). The C body is not
// compiled as C++, because __has_c_attribute is C-only.
//
// The -S run (MSIL backend) compiles only the PINVOKE part: six imported
// functions become the pinvokeimpl declarations of pinvoke.ll,
// 'pinvokeimpl("<module>" cdecl)' and 'pinvokeimpl("<module>" as "<entry>"
// cdecl)': p_mod, p_both and p_c23 as their attributes give; p_redecl, whose
// name comes from a redeclaration after its use in p_early(); p_block, whose
// name comes from a block-scope redeclaration inside p_blate() after its use
// in p_bearly(); and putchar, a function of D14's table renamed with
// import_name alone, which keeps the table's library for the triple (ucrtbase
// on windows-msvc) and takes the new entry. Variables are left out there: -S
// rejects external variables (decision D26), and import_name alone on a
// function outside D14's table is not a Phase-1 import (D14, frozen for -S by
// 5.10.13).

// DEFINE: %{msil_not} = --implicit-check-not=def_ --implicit-check-not=wasm-import \
// DEFINE:   --implicit-check-not=export-name

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -verify -emit-llvm -o %t.lnx.ll %s
// RUN: FileCheck --input-file=%t.lnx.ll --check-prefix=MSIL %s
// RUN: FileCheck --input-file=%t.lnx.ll --check-prefix=NMOD %{msil_not} %s
// RUN: FileCheck --input-file=%t.lnx.ll --check-prefix=NNAME %s

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -verify -emit-llvm -o %t.win.ll %s
// RUN: FileCheck --input-file=%t.win.ll --check-prefix=MSIL %s
// RUN: FileCheck --input-file=%t.win.ll --check-prefix=NMOD %{msil_not} %s
// RUN: FileCheck --input-file=%t.win.ll --check-prefix=NNAME %s

// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -verify -emit-llvm -o %t.a64.ll %s
// RUN: FileCheck --input-file=%t.a64.ll --check-prefix=MSIL %s
// RUN: FileCheck --input-file=%t.a64.ll --check-prefix=NMOD %{msil_not} %s
// RUN: FileCheck --input-file=%t.a64.ll --check-prefix=NNAME %s

// RUN: %clang_cc1 -x c++ -triple x86_64-unknown-linux-gnu-msil -DCXX -verify=cxx -emit-llvm -o %t.cxx-lnx.ll %s
// RUN: FileCheck --input-file=%t.cxx-lnx.ll --check-prefixes=CXX,CXX-LNX \
// RUN:   --implicit-check-not=msil-import %{msil_not} %s
// RUN: %clang_cc1 -x c++ -triple x86_64-pc-windows-msvc-msil -DCXX -verify=cxx -emit-llvm -o %t.cxx-win.ll %s
// RUN: FileCheck --input-file=%t.cxx-win.ll --check-prefixes=CXX,CXX-WIN \
// RUN:   --implicit-check-not=msil-import %{msil_not} %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -w -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=NATIVE --implicit-check-not=import-module --implicit-check-not=import-name %s
// RUN: %clang_cc1 -triple wasm32-unknown-unknown -w -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=WASM --implicit-check-not=msil-import %s

// RUN: %if msil-registered-target %{ %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -DPINVOKE -S -o - %s \
// RUN:   | FileCheck --check-prefix=IL %s %}

#ifdef CXX

// One warning per attribute, however many variants CodeGen emits.
struct CtorImport {
  CtorImport() __attribute__((import_module("def_ctor"))) {} // cxx-warning {{import module cannot be applied to a function with a definition}}
};
struct DtorImport {
  virtual ~DtorImport() __attribute__((import_name("def_dtor"))) {} // cxx-warning {{import name cannot be applied to a function with a definition}}
};
CtorImport ctor_obj;
DtorImport dtor_obj;

#else // !CXX

#ifndef PINVOKE

#if __has_attribute(import_module) && __has_attribute(import_name)
int has_import_attributes = 1;
#else
int has_import_attributes = 0;
#endif

#if __has_c_attribute(clang::import_module) && __has_c_attribute(clang::import_name)
int has_c23_import_attributes = 1;
#else
int has_c23_import_attributes = 0;
#endif

#if __has_attribute(export_name)
int has_export_name = 1;
#else
int has_export_name = 0;
#endif

// Declarations: the keys.
void f_mod(void) __attribute__((import_module("mod_f")));
void f_name(void) __attribute__((import_name("entry_f")));
void f_both(void) __attribute__((import_module("mod_b"), import_name("entry_b")));
[[clang::import_module("mod_c23")]] [[clang::import_name("entry_c23")]] void f_c23(void);

#ifndef __wasm__
extern int v_mod __attribute__((import_module("mod_v")));
extern int v_name __attribute__((import_name("entry_vn")));
extern int v_both __attribute__((import_module("mod_vb"), import_name("entry_vb")));
[[clang::import_module("mod_vc23")]] extern int v_c23;

// Redeclared after a use: the IR declaration gets both keys.
void r_fn(void) __attribute__((import_module("mod_rf")));
void r_early(void) { r_fn(); }
void r_fn(void) __attribute__((import_name("entry_rf")));

void r_inl(void) __attribute__((import_module("mod_ri")));
static inline void r_wrap(void) { r_inl(); }
void r_inl(void) __attribute__((import_name("entry_ri")));
void r_caller(void) { r_wrap(); }

extern int r_var __attribute__((import_name("entry_rv")));
int r_vearly(void) { return r_var; }
extern int r_var __attribute__((import_module("mod_rv")));

// Redeclared at block scope after a use: the IR declaration gets both keys.
void b_fn(void) __attribute__((import_module("mod_bf")));
void b_early(void) { b_fn(); }
void b_late(void) {
  void b_fn(void) __attribute__((import_name("entry_bf")));
  b_fn();
}

extern int b_var __attribute__((import_name("entry_bv")));
int b_vearly(void) { return b_var; }
int b_vlate(void) {
  extern int b_var __attribute__((import_module("mod_bv")));
  return b_var;
}

// export_name is not an MSIL attribute.
void x_exp(void) __attribute__((export_name("x_entry"))); // expected-warning {{unknown attribute 'export_name' ignored}}

// Definitions: no key.
void d_own(void) __attribute__((import_module("def_own"))) {} // expected-warning {{import module cannot be applied to a function with a definition}}
#endif

void d_after(void) {} // expected-note {{previous definition is here}}
void d_after(void) __attribute__((import_module("def_after"))); // expected-warning {{attribute declaration must precede definition}}

void d_before(void) __attribute__((import_name("def_before")));
void d_before(void) {}

#ifndef __wasm__
int v_tent __attribute__((import_module("def_vt"))); // expected-warning {{import module cannot be applied to a variable with a definition}}
int v_init __attribute__((import_name("def_vi"))) = 5; // expected-warning {{import name cannot be applied to a variable with a definition}}
extern int v_redef __attribute__((import_module("def_vr"))); // expected-warning {{import module cannot be applied to a variable with a definition}}
int v_redef = 7;

// Used first, defined later: early() creates the IR function or variable of
// each declaration, with its keys, before the definition is seen; the
// definition must remove them. s_fwd is static, so CodeGen emits its
// definition only at the end of the translation unit (a deferred definition).
void uf(void) __attribute__((import_module("def_ufm"), import_name("def_ufn")));
extern int vuf __attribute__((import_module("def_vufm"))); // expected-warning {{import module cannot be applied to a variable with a definition}}
extern int vn __attribute__((import_name("def_vnn"))); // expected-warning {{import name cannot be applied to a variable with a definition}}
static void s_fwd(void) __attribute__((import_module("def_sfm"), import_name("def_sfn")));

int early(void) {
  uf();
  s_fwd();
  return vuf + vn;
}

void uf(void) {}
int vuf = 11;
int vn = 13;
static void s_fwd(void) {}
#endif

int use(void) {
  f_mod();
  f_name();
  f_both();
  f_c23();
  d_after();
  d_before();
#ifndef __wasm__
  d_own();
  return v_mod + v_name + v_both + v_c23 + v_tent + v_init + v_redef;
#else
  return 0;
#endif
}

#else // PINVOKE

void p_mod(int) __attribute__((import_module("mylib")));
int p_both(int) __attribute__((import_module("mylib"), import_name("real_name")));
[[clang::import_module("mylib2")]] [[clang::import_name("entry2")]] int p_c23(void);

// Renamed by a redeclaration after its first use.
int p_redecl(int) __attribute__((import_module("mylib3")));
int p_early(int x) { return p_redecl(x); }
int p_redecl(int) __attribute__((import_name("real_redecl")));

// Renamed by a block-scope redeclaration after its first use.
int p_block(int) __attribute__((import_module("mylib4")));
int p_bearly(int x) { return p_block(x); }
int p_blate(int x) {
  int p_block(int) __attribute__((import_name("real_block")));
  return p_block(x);
}

// A D14 table function renamed with import_name alone.
int putchar(int) __attribute__((import_name("my_putchar")));

int call_imports(int x) {
  p_mod(x);
  putchar(x);
  return p_both(x) + p_c23() + p_early(x) + p_bearly(x) + p_blate(x);
}

#endif // PINVOKE

#endif // CXX

// MSIL-DAG: @has_import_attributes = {{.*}}global i32 1, align 4{{$}}
// MSIL-DAG: @has_c23_import_attributes = {{.*}}global i32 1, align 4{{$}}
// MSIL-DAG: @has_export_name = {{.*}}global i32 0, align 4{{$}}
// MSIL-DAG: @v_mod = external {{.*}}global i32, align 4 [[V_MOD:#[0-9]+]]{{$}}
// MSIL-DAG: @v_name = external {{.*}}global i32, align 4 [[V_NAME:#[0-9]+]]{{$}}
// MSIL-DAG: @v_both = external {{.*}}global i32, align 4 [[V_BOTH:#[0-9]+]]{{$}}
// MSIL-DAG: @v_c23 = external {{.*}}global i32, align 4 [[V_C23:#[0-9]+]]{{$}}
// MSIL-DAG: @r_var = external {{.*}}global i32, align 4 [[R_VAR:#[0-9]+]]{{$}}
// MSIL-DAG: @b_var = external {{.*}}global i32, align 4 [[B_VAR:#[0-9]+]]{{$}}
// MSIL-DAG: @v_tent = {{.*}}global i32 0, align 4{{$}}
// MSIL-DAG: @v_init = {{.*}}global i32 5, align 4{{$}}
// MSIL-DAG: @v_redef = {{.*}}global i32 7, align 4{{$}}
// MSIL-DAG: @vuf = {{.*}}global i32 11, align 4{{$}}
// MSIL-DAG: @vn = {{.*}}global i32 13, align 4{{$}}
// MSIL-DAG: define {{.*}}void @d_own()
// MSIL-DAG: define {{.*}}void @d_after()
// MSIL-DAG: define {{.*}}void @d_before()
// MSIL-DAG: define {{.*}}i32 @early()
// MSIL-DAG: define {{.*}}void @uf()
// MSIL-DAG: define internal void @s_fwd()
// MSIL-DAG: define {{.*}}void @r_early()
// MSIL-DAG: define {{.*}}void @r_caller()
// MSIL-DAG: define internal void @r_wrap()
// MSIL-DAG: define {{.*}}i32 @r_vearly()
// MSIL-DAG: define {{.*}}void @b_early()
// MSIL-DAG: define {{.*}}void @b_late()
// MSIL-DAG: define {{.*}}i32 @b_vearly()
// MSIL-DAG: define {{.*}}i32 @b_vlate()
// MSIL-DAG: declare {{.*}}void @f_mod() [[F_MOD:#[0-9]+]]{{$}}
// MSIL-DAG: declare {{.*}}void @f_name() [[F_NAME:#[0-9]+]]{{$}}
// MSIL-DAG: declare {{.*}}void @f_both() [[F_BOTH:#[0-9]+]]{{$}}
// MSIL-DAG: declare {{.*}}void @f_c23() [[F_C23:#[0-9]+]]{{$}}
// MSIL-DAG: declare {{.*}}void @r_fn() [[R_FN:#[0-9]+]]{{$}}
// MSIL-DAG: declare {{.*}}void @r_inl() [[R_INL:#[0-9]+]]{{$}}
// MSIL-DAG: declare {{.*}}void @b_fn() [[B_FN:#[0-9]+]]{{$}}
// MSIL-DAG: attributes [[V_MOD]] = { "msil-import-module"="mod_v" }
// MSIL-DAG: attributes [[V_NAME]] = { "msil-import-name"="entry_vn" }
// MSIL-DAG: attributes [[V_BOTH]] = { "msil-import-module"="mod_vb" "msil-import-name"="entry_vb" }
// MSIL-DAG: attributes [[V_C23]] = { "msil-import-module"="mod_vc23" }
// MSIL-DAG: attributes [[R_VAR]] = { "msil-import-module"="mod_rv" "msil-import-name"="entry_rv" }
// MSIL-DAG: attributes [[B_VAR]] = { "msil-import-module"="mod_bv" "msil-import-name"="entry_bv" }
// MSIL-DAG: attributes [[F_MOD]] = { {{.*}}"msil-import-module"="mod_f"{{.*}} }
// MSIL-DAG: attributes [[F_NAME]] = { {{.*}}"msil-import-name"="entry_f"{{.*}} }
// MSIL-DAG: attributes [[F_BOTH]] = { {{.*}}"msil-import-module"="mod_b" "msil-import-name"="entry_b"{{.*}} }
// MSIL-DAG: attributes [[F_C23]] = { {{.*}}"msil-import-module"="mod_c23" "msil-import-name"="entry_c23"{{.*}} }
// MSIL-DAG: attributes [[R_FN]] = { {{.*}}"msil-import-module"="mod_rf" "msil-import-name"="entry_rf"{{.*}} }
// MSIL-DAG: attributes [[R_INL]] = { {{.*}}"msil-import-module"="mod_ri" "msil-import-name"="entry_ri"{{.*}} }
// MSIL-DAG: attributes [[B_FN]] = { {{.*}}"msil-import-module"="mod_bf" "msil-import-name"="entry_bf"{{.*}} }

// NMOD-COUNT-11: "msil-import-module"=
// NMOD-NOT:     "msil-import-module"=
// NNAME-COUNT-10: "msil-import-name"=
// NNAME-NOT:     "msil-import-name"=

// The C++ runs: every structor variant is defined, and the trailing plain
// CXX line makes the --implicit-check-not patterns cover the attribute
// groups after it.
// CXX-LNX-DAG: define linkonce_odr void @_ZN10CtorImportC1Ev(
// CXX-LNX-DAG: define linkonce_odr void @_ZN10CtorImportC2Ev(
// CXX-LNX-DAG: define linkonce_odr void @_ZN10DtorImportD0Ev(
// CXX-LNX-DAG: define linkonce_odr void @_ZN10DtorImportD1Ev(
// CXX-LNX-DAG: define linkonce_odr void @_ZN10DtorImportD2Ev(
// CXX-WIN-DAG: define linkonce_odr {{.*}}@"??0CtorImport@@QEAA@XZ"(
// CXX-WIN-DAG: define linkonce_odr {{.*}}@"??1DtorImport@@UEAA@XZ"(
// CXX-WIN-DAG: define linkonce_odr {{.*}}@"??_GDtorImport@@UEAAPEAXI@Z"(
// CXX:         {{^}}attributes #

// The control runs use plain CHECK lines in output order, so that their
// --implicit-check-not patterns cover the whole output (FileCheck applies
// them before, between and after positive matches, but not around a
// trailing CHECK-DAG group; the MSIL runs get theirs in the NMOD run).
// NATIVE:      @has_import_attributes = {{.*}}global i32 0, align 4{{$}}
// NATIVE-NEXT: @has_c23_import_attributes = {{.*}}global i32 0, align 4{{$}}
// NATIVE-NEXT: @has_export_name = {{.*}}global i32 0, align 4{{$}}
// NATIVE:      @v_mod = external global i32, align 4{{$}}
// NATIVE:      declare {{.*}}void @f_mod()

// WASM:      @has_import_attributes = {{.*}}global i32 1, align 4{{$}}
// WASM-NEXT: @has_c23_import_attributes = {{.*}}global i32 1, align 4{{$}}
// WASM-NEXT: @has_export_name = {{.*}}global i32 1, align 4{{$}}
// WASM:      declare {{.*}}void @f_mod() [[F_MOD:#[0-9]+]]{{$}}
// WASM:      declare {{.*}}void @f_both() [[F_BOTH:#[0-9]+]]{{$}}
// WASM:      attributes [[F_MOD]] = { {{.*}}"wasm-import-module"="mod_f"{{.*}} }
// WASM:      attributes [[F_BOTH]] = { {{.*}}"wasm-import-module"="mod_b" "wasm-import-name"="entry_b"{{.*}} }

// IL-DAG: {{^}}.method public hidebysig static pinvokeimpl("mylib" cdecl) void 'p_mod'(int32) cil managed preservesig {}{{$}}
// IL-DAG: {{^}}.method public hidebysig static pinvokeimpl("mylib" as "real_name" cdecl) int32 'p_both'(int32) cil managed preservesig {}{{$}}
// IL-DAG: {{^}}.method public hidebysig static pinvokeimpl("mylib2" as "entry2" cdecl) int32 'p_c23'() cil managed preservesig {}{{$}}
// IL-DAG: {{^}}.method public hidebysig static pinvokeimpl("mylib3" as "real_redecl" cdecl) int32 'p_redecl'(int32) cil managed preservesig {}{{$}}
// IL-DAG: {{^}}.method public hidebysig static pinvokeimpl("mylib4" as "real_block" cdecl) int32 'p_block'(int32) cil managed preservesig {}{{$}}
// IL-DAG: {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" as "my_putchar" cdecl) int32 'putchar'(int32) cil managed preservesig {}{{$}}
