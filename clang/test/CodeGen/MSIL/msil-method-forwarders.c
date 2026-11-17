// The forwarders of exported and startup managed calls in clang's CodeGen
// (ImplementationPlan.md 5.20.4 as amended for dllexport, constructor and
// destructor on a managed-call declaration, contract D6; milestone MC-1).
// For the managed call '__msil_mc.<s>', <s> being its C name:
// - dllexport, on the four Windows triples only: '<s>' itself, 'define
//   weak_odr dllexport' in a COMDAT of its own name ('$<s> = comdat any'),
//   with the managed call's parameters, result and their attributes; the
//   body calls '@__msil_mc.<s>' with all its arguments and returns the
//   result (the call carries the managed call's result and parameter
//   attributes, so 'zeroext' appears exactly where the declaration has it).
//   Calls in the unit still go to the stub.
// - constructor on an external declaration: '__msil_mc_ctor.<s>', 'define
//   linkonce_odr hidden void' in a COMDAT of its own name, which keys its
//   llvm.global_ctors entry '{ i32 <prio>, ptr F, ptr F }'; on a 'static'
//   declaration, 'define internal void' with no COMDAT, calling the internal
//   declaration's IR name '@__msil_mc.<s>.__uniq.<n>', with the entry
//   '{ i32 <prio>, ptr F, ptr null }'. The forwarder drops the result.
// - destructor: the same with '__msil_mc_dtor.' and llvm.global_dtors, also
//   under -fregister-global-dtors-with-atexit (no atexit call, which would
//   run the forwarder once per unit).
// - <prio> is the attribute's, 65535 when none is written; constructor
//   written on a redeclaration counts (the most recent declaration is read
//   at the end of the unit, D5).
// - Every forwarder carries "msil-managed-forwarder"="<s>" and none of the
//   managed-call attributes ("msil-managed-type" and the rest), which stay
//   on the declaration.
// - Forwarders are emitted when the unit never calls the managed call: only
//   x_abs is called here.
// - The !msil.managed.calls operand of an external declaration with any of
//   the three gains an 8th string: 'dllexport', 'constructor(<prio>)' and
//   'destructor(<prio>)', joined by ',' with no spaces, in that order. Other
//   entries keep 7 operands (dllimport adds none), and an internal
//   declaration never gets the 8th.
// - On the Linux, musl, aarch64 Linux and portable triples dllexport is
//   ignored (clang's 'unknown attribute' warning, Sema/attr-msil-method.c,
//   silenced here): no export forwarder, no 'dllexport' in the IR, and an
//   unused managed call with only dllexport is not declared at all.
//   constructor and destructor work on every MSIL triple,
//   msil64-unknown-dotnet included; under ELF rules (__ELF__: x86_64 and
//   aarch64 Linux, musl) a constructor's forwarder may take (int, char **,
//   char **), the parameters of the managed call.
// - The forwarders' names never start with '__msil_mc.' ('__msil_mc_ctor.'
//   and '__msil_mc_dtor.' differ at the '_'; the export forwarder is the C
//   name). A source can write the constructor and destructor forwarders'
//   names only as asm labels, and T-M22's forwarder form reserves them: a
//   label that starts with '__msil_mc_ctor.' or '__msil_mc_dtor.' gives
//   'asm label '<n>' is reserved for the MSIL toolchain (a managed-call
//   forwarder)' at the label (Sema/attr-msil-method.c,
//   msil-method-forwarders-clash.c).
// - The IMPORT runs: __declspec(dllimport) and __attribute__((dllimport)) on
//   a managed call compile through the backend on a Windows triple (-S): the
//   calls go to the 'assembly' stubs, which call the BCL member, and nothing
//   is imported from a native DLL (no 'pinvokeimpl').
// managed-call-forwarders.ll (llvm/test/CodeGen/MSIL) checks the backend's
// side; each group of forwarders is checked by a FileCheck run of its own,
// so the checks do not depend on the order of the forwarders.
// msil-method-forwarders-clash.c checks T-M25, the export forwarder's name
// that a variable or a definition of the unit already uses, and T-M22 for
// the labels that take a constructor or destructor forwarder's name;
// msil-method-forwarders-block.c the forwarders of managed calls whose
// dllexport, constructor or destructor comes from a declaration at block
// scope; msil-method-forwarders-pch.c the same forwarders when the
// declarations come from a precompiled header or a module.

// DEFINE: %{t} =
// DEFINE: %{e} =
// DEFINE: %{ll} = %t.%{t}.ll
// DEFINE: %{fwd} = FileCheck %s --input-file=%{ll} --check-prefix
// DEFINE: %{run} = %clang_cc1 -triple %{t} -Wno-unknown-attributes -emit-llvm -o %{ll} %s && \
// DEFINE:   FileCheck %s --input-file=%{ll} --check-prefix=CHECK && \
// DEFINE:   grep "^@llvm.global_ctors = " %{ll} | FileCheck %s --check-prefixes=CTORS,CTORS-%{e} && \
// DEFINE:   grep "^@llvm.global_dtors = " %{ll} | FileCheck %s --check-prefix=DTORS && \
// DEFINE:   grep '"msil-managed-forwarder"=' %{ll} | not grep '"msil-managed-type"' && \
// DEFINE:   %{fwd}=KINIT && %{fwd}=KTS && %{fwd}=KFINI && %{fwd}=KALL && %{fwd}=KSTATIC && %{fwd}=KRE

// REDEFINE: %{e} = NOELF
// REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
// RUN: %{run} && %{fwd}=XABS && %{fwd}=XVIS && %{fwd}=XALL
// REDEFINE: %{t} = x86_64-w64-windows-gnu-msil
// RUN: %{run} && %{fwd}=XABS && %{fwd}=XVIS && %{fwd}=XALL
// REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
// RUN: %{run} && %{fwd}=XABS && %{fwd}=XVIS && %{fwd}=XALL
// REDEFINE: %{t} = aarch64-w64-windows-gnu-msil
// RUN: %{run} && %{fwd}=XABS && %{fwd}=XVIS && %{fwd}=XALL
// REDEFINE: %{t} = msil64-unknown-dotnet
// RUN: %{run} && %{fwd}=NOWIN && not grep -E "dllexport|x_vis" %{ll}
// REDEFINE: %{e} = ELF
// REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
// RUN: %{run} && %{fwd}=NOWIN && not grep -E "dllexport|x_vis" %{ll} && %{fwd}=KARGS
// REDEFINE: %{t} = x86_64-unknown-linux-musl-msil
// RUN: %{run} && %{fwd}=NOWIN && not grep -E "dllexport|x_vis" %{ll} && %{fwd}=KARGS
// REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
// RUN: %{run} && %{fwd}=NOWIN && not grep -E "dllexport|x_vis" %{ll} && %{fwd}=KARGS

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -Wno-unknown-attributes -fregister-global-dtors-with-atexit -emit-llvm -o %t.atexit.ll %s
// RUN: grep "^@llvm.global_dtors = " %t.atexit.ll | FileCheck %s --check-prefix=DTORS
// RUN: not grep atexit %t.atexit.ll

// RUN: %if msil-registered-target %{ %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -DIMPORT -S -o %t.imp.il %s %}
// RUN: %if msil-registered-target %{ FileCheck %s --check-prefix=IMPIL --input-file=%t.imp.il --implicit-check-not=pinvokeimpl %}
// RUN: %if ilasm %{ %ilasm-verify -output=%t.imp.dll %t.imp.il %}

#if defined(IMPORT)

int x_imp(int x) __attribute__((dllimport, msil_method("System.Math", "Sign", "int32(int32)")));
__declspec(dllimport) int x_imp2(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
int use_imp(int v) { return x_imp(v) + x_imp2(v); }

// IMPIL-LABEL: .method public static int32 'use_imp'(int32) cil managed
// IMPIL:       {{^}}  call int32 '__msil_mc.x_imp'(int32){{$}}
// IMPIL:       {{^}}  call int32 '__msil_mc.x_imp2'(int32){{$}}
// IMPIL:       {{^}}} // end of method 'use_imp'{{$}}
// IMPIL-LABEL: .method assembly static int32 '__msil_mc.x_imp'(int32) cil managed
// IMPIL:       {{^}}  call int32 [mscorlib]System.Math::Sign(int32){{$}}
// IMPIL-NEXT:  {{^}}  ret{{$}}
// IMPIL-NEXT:  {{^}}} // end of method '__msil_mc.x_imp'{{$}}
// IMPIL-LABEL: .method assembly static int32 '__msil_mc.x_imp2'(int32) cil managed
// IMPIL:       {{^}}  call int32 [mscorlib]System.Math::Abs(int32){{$}}
// IMPIL-NEXT:  {{^}}  ret{{$}}
// IMPIL-NEXT:  {{^}}} // end of method '__msil_mc.x_imp2'{{$}}

#else

// dllexport: forwarders on the Windows triples; x_vis is never called.
int x_abs(int x) __attribute__((dllexport, msil_method("System.Math", "Abs", "int32(int32)")));
_Bool x_vis(void) __attribute__((dllexport, msil_method("System.Console", "get_CursorVisible", "bool()")));
// constructor and destructor, never called.
void k_init(void) __attribute__((constructor, msil_method("System.GC", "Collect", "void()")));
long long k_ts(void) __attribute__((constructor(101), msil_method("System.Diagnostics.Stopwatch", "GetTimestamp", "int64()", "System")));
void k_fini(void) __attribute__((destructor, msil_method("System.GC", "Collect", "void()")));
void k_all(void) __attribute__((dllexport, constructor(300), destructor(500), msil_method("System.GC", "Collect", "void()")));
static void k_static(void) __attribute__((constructor(200), destructor, msil_method("System.GC", "Collect", "void()")));
// constructor written on a redeclaration (D5).
void k_re(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
void k_re(void) __attribute__((constructor));
#if defined(__ELF__)
// Under ELF rules a constructor may take (argc, argv, envp).
void k_args(int argc, char **argv, char **envp) __attribute__((constructor, msil_method("Ns.Startup", "Init", "void(int32, native int, native int)")));
#endif
// No forwarder: no attribute, and dllimport.
int m_sign(int x) __attribute__((msil_method("System.Math", "Sign", "int32(int32)")));
int x_imp(int x) __attribute__((dllimport, msil_method("System.Math", "Sign", "int32(int32)")));

int use(int v) { return x_abs(v) + m_sign(v) + x_imp(v); }

#endif

// Every triple: the calls go to the stubs, and the declarations without the
// three attributes keep 7 operands.
// CHECK-LABEL: @use(
// CHECK:       {{^}}  %call = call i32 @__msil_mc.x_abs(i32 noundef %{{[0-9]+}}){{$}}
// CHECK:       {{^}}  %call1 = call i32 @__msil_mc.m_sign(i32 noundef %{{[0-9]+}}){{$}}
// CHECK:       {{^}}  %call2 = call i32 @__msil_mc.x_imp(i32 noundef %{{[0-9]+}}){{$}}
// CHECK-DAG:   {{^}}!{{[0-9]+}} = !{!"__msil_mc.m_sign", !"System.Math", !"Sign", !"int32(int32)", !"", !"val;val", !""}{{$}}
// CHECK-DAG:   {{^}}!{{[0-9]+}} = !{!"__msil_mc.x_imp", !"System.Math", !"Sign", !"int32(int32)", !"", !"val;val", !""}{{$}}

// The entries: each forwarder once, keyed to its own COMDAT unless it is
// internal.
// CTORS-NOELF: {{^}}@llvm.global_ctors = appending global [5 x { i32, ptr, ptr }] [
// CTORS-ELF:   {{^}}@llvm.global_ctors = appending global [6 x { i32, ptr, ptr }] [
// CTORS-DAG:   { i32, ptr, ptr } { i32 65535, ptr @__msil_mc_ctor.k_init, ptr @__msil_mc_ctor.k_init }
// CTORS-DAG:   { i32, ptr, ptr } { i32 101, ptr @__msil_mc_ctor.k_ts, ptr @__msil_mc_ctor.k_ts }
// CTORS-DAG:   { i32, ptr, ptr } { i32 300, ptr @__msil_mc_ctor.k_all, ptr @__msil_mc_ctor.k_all }
// CTORS-DAG:   { i32, ptr, ptr } { i32 200, ptr @__msil_mc_ctor.k_static, ptr null }
// CTORS-DAG:   { i32, ptr, ptr } { i32 65535, ptr @__msil_mc_ctor.k_re, ptr @__msil_mc_ctor.k_re }
// CTORS-ELF-DAG: { i32, ptr, ptr } { i32 65535, ptr @__msil_mc_ctor.k_args, ptr @__msil_mc_ctor.k_args }
// DTORS:       {{^}}@llvm.global_dtors = appending global [3 x { i32, ptr, ptr }] [
// DTORS-DAG:   { i32, ptr, ptr } { i32 65535, ptr @__msil_mc_dtor.k_fini, ptr @__msil_mc_dtor.k_fini }
// DTORS-DAG:   { i32, ptr, ptr } { i32 500, ptr @__msil_mc_dtor.k_all, ptr @__msil_mc_dtor.k_all }
// DTORS-DAG:   { i32, ptr, ptr } { i32 65535, ptr @__msil_mc_dtor.k_static, ptr null }

// The forwarders, one group per managed call.
// KINIT:       {{^}}$__msil_mc_ctor.k_init = comdat any{{$}}
// KINIT:       {{^}}declare void @__msil_mc.k_init() #{{[0-9]+}}{{$}}
// KINIT:       {{^}}define linkonce_odr hidden void @__msil_mc_ctor.k_init() #[[KINIT_A:[0-9]+]] comdat {{\{$}}
// KINIT-NEXT:  {{^}}entry:{{$}}
// KINIT-NEXT:  {{^}}  call void @__msil_mc.k_init(){{$}}
// KINIT-NEXT:  {{^}}  ret void{{$}}
// KINIT-NEXT:  {{^}}}{{$}}
// KINIT:       {{^}}attributes #[[KINIT_A]] = { {{.*}}"msil-managed-forwarder"="k_init"{{.*}} }{{$}}
// KINIT:       {{^}}!{{[0-9]+}} = !{!"__msil_mc.k_init", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(65535)"}{{$}}

// KTS:         {{^}}$__msil_mc_ctor.k_ts = comdat any{{$}}
// KTS:         {{^}}declare i64 @__msil_mc.k_ts() #{{[0-9]+}}{{$}}
// KTS:         {{^}}define linkonce_odr hidden void @__msil_mc_ctor.k_ts() #[[KTS_A:[0-9]+]] comdat {{\{$}}
// KTS-NEXT:    {{^}}entry:{{$}}
// KTS-NEXT:    {{^}}  %0 = call i64 @__msil_mc.k_ts(){{$}}
// KTS-NEXT:    {{^}}  ret void{{$}}
// KTS-NEXT:    {{^}}}{{$}}
// KTS:         {{^}}attributes #[[KTS_A]] = { {{.*}}"msil-managed-forwarder"="k_ts"{{.*}} }{{$}}
// KTS:         {{^}}!{{[0-9]+}} = !{!"__msil_mc.k_ts", !"System.Diagnostics.Stopwatch", !"GetTimestamp", !"int64()", !"System", !";val", !"", !"constructor(101)"}{{$}}

// KFINI:       {{^}}$__msil_mc_dtor.k_fini = comdat any{{$}}
// KFINI:       {{^}}declare void @__msil_mc.k_fini() #{{[0-9]+}}{{$}}
// KFINI:       {{^}}define linkonce_odr hidden void @__msil_mc_dtor.k_fini() #[[KFINI_A:[0-9]+]] comdat {{\{$}}
// KFINI-NEXT:  {{^}}entry:{{$}}
// KFINI-NEXT:  {{^}}  call void @__msil_mc.k_fini(){{$}}
// KFINI-NEXT:  {{^}}  ret void{{$}}
// KFINI-NEXT:  {{^}}}{{$}}
// KFINI:       {{^}}attributes #[[KFINI_A]] = { {{.*}}"msil-managed-forwarder"="k_fini"{{.*}} }{{$}}
// KFINI:       {{^}}!{{[0-9]+}} = !{!"__msil_mc.k_fini", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"destructor(65535)"}{{$}}

// The constructor forwarder, then the destructor one (the export forwarder
// on the Windows triples is XALL's).
// KALL-DAG:    {{^}}$__msil_mc_ctor.k_all = comdat any{{$}}
// KALL-DAG:    {{^}}$__msil_mc_dtor.k_all = comdat any{{$}}
// KALL:        {{^}}declare void @__msil_mc.k_all() #{{[0-9]+}}{{$}}
// KALL:        {{^}}define linkonce_odr hidden void @__msil_mc_ctor.k_all() #[[KALL_A:[0-9]+]] comdat {{\{$}}
// KALL-NEXT:   {{^}}entry:{{$}}
// KALL-NEXT:   {{^}}  call void @__msil_mc.k_all(){{$}}
// KALL-NEXT:   {{^}}  ret void{{$}}
// KALL-NEXT:   {{^}}}{{$}}
// KALL:        {{^}}define linkonce_odr hidden void @__msil_mc_dtor.k_all() #{{[0-9]+}} comdat {{\{$}}
// KALL-NEXT:   {{^}}entry:{{$}}
// KALL-NEXT:   {{^}}  call void @__msil_mc.k_all(){{$}}
// KALL-NEXT:   {{^}}  ret void{{$}}
// KALL-NEXT:   {{^}}}{{$}}
// KALL:        {{^}}attributes #[[KALL_A]] = { {{.*}}"msil-managed-forwarder"="k_all"{{.*}} }{{$}}

// 'static': internal, no COMDAT, no 8th operand.
// KSTATIC:      {{^}}declare void @__msil_mc.k_static.__uniq.[[KSTATIC_U:[0-9]+]]() #{{[0-9]+}}{{$}}
// KSTATIC:      {{^}}define internal void @__msil_mc_ctor.k_static() #[[KSTATIC_A:[0-9]+]] {{\{$}}
// KSTATIC-NEXT: {{^}}entry:{{$}}
// KSTATIC-NEXT: {{^}}  call void @__msil_mc.k_static.__uniq.[[KSTATIC_U]](){{$}}
// KSTATIC-NEXT: {{^}}  ret void{{$}}
// KSTATIC-NEXT: {{^}}}{{$}}
// KSTATIC:      {{^}}define internal void @__msil_mc_dtor.k_static() #{{[0-9]+}} {{\{$}}
// KSTATIC-NEXT: {{^}}entry:{{$}}
// KSTATIC-NEXT: {{^}}  call void @__msil_mc.k_static.__uniq.[[KSTATIC_U]](){{$}}
// KSTATIC-NEXT: {{^}}  ret void{{$}}
// KSTATIC-NEXT: {{^}}}{{$}}
// KSTATIC:      {{^}}attributes #[[KSTATIC_A]] = { {{.*}}"msil-managed-forwarder"="k_static"{{.*}} }{{$}}
// KSTATIC:      {{^}}!{{[0-9]+}} = !{!"__msil_mc.k_static.__uniq.[[KSTATIC_U]]", !"System.GC", !"Collect", !"void()", !"", !";void", !""}{{$}}

// KRE:         {{^}}$__msil_mc_ctor.k_re = comdat any{{$}}
// KRE:         {{^}}declare void @__msil_mc.k_re() #{{[0-9]+}}{{$}}
// KRE:         {{^}}define linkonce_odr hidden void @__msil_mc_ctor.k_re() #[[KRE_A:[0-9]+]] comdat {{\{$}}
// KRE-NEXT:    {{^}}entry:{{$}}
// KRE-NEXT:    {{^}}  call void @__msil_mc.k_re(){{$}}
// KRE-NEXT:    {{^}}  ret void{{$}}
// KRE-NEXT:    {{^}}}{{$}}
// KRE:         {{^}}attributes #[[KRE_A]] = { {{.*}}"msil-managed-forwarder"="k_re"{{.*}} }{{$}}
// KRE:         {{^}}!{{[0-9]+}} = !{!"__msil_mc.k_re", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(65535)"}{{$}}

// The Windows triples: the export forwarders.
// XABS:        {{^}}$x_abs = comdat any{{$}}
// XABS:        {{^}}declare i32 @__msil_mc.x_abs(i32 noundef) #{{[0-9]+}}{{$}}
// XABS:        {{^}}define weak_odr dllexport i32 @x_abs(i32 noundef %0) #[[XABS_A:[0-9]+]] comdat {{\{$}}
// XABS-NEXT:   {{^}}entry:{{$}}
// XABS-NEXT:   {{^}}  %1 = call i32 @__msil_mc.x_abs(i32 noundef %0){{$}}
// XABS-NEXT:   {{^}}  ret i32 %1{{$}}
// XABS-NEXT:   {{^}}}{{$}}
// XABS:        {{^}}attributes #[[XABS_A]] = { {{.*}}"msil-managed-forwarder"="x_abs"{{.*}} }{{$}}
// XABS:        {{^}}!{{[0-9]+}} = !{!"__msil_mc.x_abs", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !"", !"dllexport"}{{$}}

// XVIS:        {{^}}$x_vis = comdat any{{$}}
// XVIS:        {{^}}declare [[ZX:(zeroext )?]]i1 @__msil_mc.x_vis() #{{[0-9]+}}{{$}}
// XVIS:        {{^}}define weak_odr dllexport [[ZX]]i1 @x_vis() #[[XVIS_A:[0-9]+]] comdat {{\{$}}
// XVIS-NEXT:   {{^}}entry:{{$}}
// XVIS-NEXT:   {{^}}  %0 = call [[ZX]]i1 @__msil_mc.x_vis(){{$}}
// XVIS-NEXT:   {{^}}  ret i1 %0{{$}}
// XVIS-NEXT:   {{^}}}{{$}}
// XVIS:        {{^}}attributes #[[XVIS_A]] = { {{.*}}"msil-managed-forwarder"="x_vis"{{.*}} }{{$}}
// XVIS:        {{^}}!{{[0-9]+}} = !{!"__msil_mc.x_vis", !"System.Console", !"get_CursorVisible", !"bool()", !"", !";bool", !"", !"dllexport"}{{$}}

// XALL:        {{^}}$k_all = comdat any{{$}}
// XALL:        {{^}}define weak_odr dllexport void @k_all() #[[XALL_A:[0-9]+]] comdat {{\{$}}
// XALL-NEXT:   {{^}}entry:{{$}}
// XALL-NEXT:   {{^}}  call void @__msil_mc.k_all(){{$}}
// XALL-NEXT:   {{^}}  ret void{{$}}
// XALL-NEXT:   {{^}}}{{$}}
// XALL:        {{^}}attributes #[[XALL_A]] = { {{.*}}"msil-managed-forwarder"="k_all"{{.*}} }{{$}}
// XALL:        {{^}}!{{[0-9]+}} = !{!"__msil_mc.k_all", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"dllexport,constructor(300),destructor(500)"}{{$}}

// The other triples: no export forwarder, and the 8th operand without
// 'dllexport'.
// NOWIN-DAG:   {{^}}!{{[0-9]+}} = !{!"__msil_mc.x_abs", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}{{$}}
// NOWIN-DAG:   {{^}}!{{[0-9]+}} = !{!"__msil_mc.k_all", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(300),destructor(500)"}{{$}}

// The ELF triples: the constructor forwarder has the managed call's
// parameters and passes them on.
// KARGS:       {{^}}$__msil_mc_ctor.k_args = comdat any{{$}}
// KARGS:       {{^}}declare void @__msil_mc.k_args(i32 noundef, ptr noundef, ptr noundef) #{{[0-9]+}}{{$}}
// KARGS:       {{^}}define linkonce_odr hidden void @__msil_mc_ctor.k_args(i32 noundef %0, ptr noundef %1, ptr noundef %2) #[[KARGS_A:[0-9]+]] comdat {{\{$}}
// KARGS-NEXT:  {{^}}entry:{{$}}
// KARGS-NEXT:  {{^}}  call void @__msil_mc.k_args(i32 noundef %0, ptr noundef %1, ptr noundef %2){{$}}
// KARGS-NEXT:  {{^}}  ret void{{$}}
// KARGS-NEXT:  {{^}}}{{$}}
// KARGS:       {{^}}attributes #[[KARGS_A]] = { {{.*}}"msil-managed-forwarder"="k_args"{{.*}} }{{$}}
// KARGS:       {{^}}!{{[0-9]+}} = !{!"__msil_mc.k_args", !"Ns.Startup", !"Init", !"void(int32, native int, native int)", !"", !"val,val,val;void", !"", !"constructor(65535)"}{{$}}
