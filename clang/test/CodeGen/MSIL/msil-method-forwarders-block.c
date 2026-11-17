// The forwarders of managed calls whose dllexport, constructor or destructor
// comes from a declaration at block scope (ImplementationPlan.md 5.20.4 as
// amended for dllexport, constructor and destructor on a managed-call
// declaration, contract D6; milestone MC-1). CodeGen emits the forwarders
// even when the unit never calls the managed call, and that holds when the
// attribute comes only from a block-scope declaration or when the only
// declaration is at block scope, although clang's CodeGen emits nothing for a
// block-scope function declaration by itself. Apart from tplpat.cpp and
// tpldep.cpp, no unit calls its managed call, and each gets exactly what
// msil-method-forwarders.c pins for a file-scope declaration: the forwarder,
// its one llvm.global_ctors or llvm.global_dtors entry, the managed call's
// declaration with its "msil-managed-*" attributes, and the one
// '!msil.managed.calls' entry, with the 8th operand unless the managed call
// is internal:
// - blk3.c: a block-scope redeclaration adds constructor to a file-scope
//   managed call: '__msil_mc_ctor.k', 'define linkonce_odr hidden' in a COMDAT
//   of its own name, which keys its llvm.global_ctors entry.
// - blk4.c: the only declaration, with msil_method and constructor, is at
//   block scope: the same.
// - blk6.c: the redeclaration is in a 'static' function that nothing uses,
//   whose body clang never emits: the same.
// - blk7.c: the redeclaration is in a compound statement nested in an 'if'
//   statement: the same.
// - blocks.c (-fblocks) and captured.c: the redeclaration is in a block
//   literal, or in a statement that '#pragma clang __debug captured'
//   captures: the same.
// - externc.cpp: C++, the managed call and the function that redeclares it
//   are in an 'extern "C"' block: the same names as in C.
// - lambda.cpp, member.cpp, friend.cpp and localclass.cpp: C++, the
//   redeclaration is in a lambda, a member function defined in its class, a
//   friend function defined in its class or a member function of a local
//   class, none of which clang emits. The managed call is 'extern "C"', so
//   the names are those of blk3.c.
// - tplinst.cpp: C++, the redeclaration is in a function template that the
//   unit instantiates explicitly: the same.
// - tplprio.cpp: the same with a priority that depends on the template
//   parameter, sizeof(T) + 200: 204 in the llvm.global_ctors entry and in
//   the 8th operand.
// - ns.cpp: C++, the managed call and the function that redeclares it are in
//   a namespace: the forwarder is '__msil_mc_ctor.<mangled name>' ('_ZN1n1kEv'
//   on x86_64-unknown-linux-gnu-msil and msil64-unknown-dotnet, '?k@n@@YAXXZ'
//   on x86_64-pc-windows-msvc-msil).
// - blk5.c: a block-scope redeclaration adds destructor: '__msil_mc_dtor.k'
//   with its llvm.global_dtors entry (-emit-llvm only: the backend refuses
//   global destructors).
// - st.c: a 'static' managed call. The block-scope redeclaration (no storage
//   class) has its internal linkage, so the forwarder is 'define internal'
//   with no COMDAT and calls the '.__uniq.<n>' name, its entry has no key,
//   and the '!msil.managed.calls' entry has no 8th operand.
// - blkdll.c (x86_64-pc-windows-msvc-msil, -fms-extensions): a block-scope
//   redeclaration adds __declspec(dllexport), with upstream's warning
//   'redeclaration of 'k' should not add 'dllexport' attribute' and its note:
//   the export forwarder 'k', 'define weak_odr dllexport' in COMDAT k.
// A function template that the unit never instantiates declares nothing, so
// a block-scope redeclaration in it adds no attribute (tplpat.cpp, and
// tpldep.cpp, whose priority depends on the template parameter and so has no
// value): f() calls the managed call, which gets no forwarder and no
// llvm.global_ctors entry, and its '!msil.managed.calls' entry has no 8th
// operand.
// Where the declaration sits does not depend on the triple, so blk7.c,
// blocks.c, captured.c, lambda.cpp, member.cpp, friend.cpp, localclass.cpp
// and the four template units run on msil64-unknown-dotnet only; the others
// run on x86_64-unknown-linux-gnu-msil, msil64-unknown-dotnet and
// x86_64-pc-windows-msvc-msil (blkdll.c on the last only).
// Each unit is a translation unit of its own, so each forwarder and each
// llvm.global_ctors or llvm.global_dtors line is pinned whole and counted.
// The managed call's declaration is checked by a FileCheck run of its own
// (DECL, XDECL), so the checks do not depend on whether it comes before or
// after the forwarder. msil-method-clash.c's block.c, blockdll.c, blockattr.c
// and blockonly.c check T-M24 for declarations at block scope.

// RUN: rm -rf %t && split-file %s %t
// DEFINE: %{t} =
// DEFINE: %{u} =
// DEFINE: %{abi} =
// DEFINE: %{f} =
// DEFINE: %{ll} = %t/%{u}.%{t}.ll
// DEFINE: %{gen} = %clang_cc1 -triple %{t} %{f} -verify -emit-llvm -o %{ll} %t/%{u} && \
// DEFINE:   FileCheck %s --input-file=%{ll} --check-prefix=DECL && \
// DEFINE:   grep "^declare " %{ll} | count 1
// DEFINE: %{chk} = FileCheck %s --input-file=%{ll} --check-prefix
// DEFINE: %{ctors} = grep "^@llvm.global_ctors = " %{ll}
// DEFINE: %{dtors} = grep "^@llvm.global_dtors = " %{ll}
// DEFINE: %{ctor} = %{gen} && %{chk}=CTOR && \
// DEFINE:   grep "^define .*@__msil_mc_ctor\." %{ll} | count 1 && \
// DEFINE:   %{ctors} | count 1 && %{ctors} | FileCheck %s --check-prefix=CTORS && \
// DEFINE:   not grep -E "llvm.global_dtors|__msil_mc_dtor" %{ll}
// DEFINE: %{prio} = %{gen} && %{chk}=PRIO && \
// DEFINE:   grep "^define .*@__msil_mc_ctor\." %{ll} | count 1 && \
// DEFINE:   %{ctors} | count 1 && %{ctors} | FileCheck %s --check-prefix=PRIOCTORS && \
// DEFINE:   not grep -E "llvm.global_dtors|__msil_mc_dtor" %{ll}
// DEFINE: %{none} = %{gen} && %{chk}=NONE && \
// DEFINE:   not grep -E "comdat|llvm.global_[cd]tors|__msil_mc_[cd]tor" %{ll}
// DEFINE: %{ns} = %{gen} && %{chk}=NS-%{abi} && \
// DEFINE:   grep "^define .*__msil_mc_ctor\." %{ll} | count 1 && \
// DEFINE:   %{ctors} | count 1 && %{ctors} | FileCheck %s --check-prefix=NSCTORS-%{abi} && \
// DEFINE:   not grep -E "llvm.global_dtors|__msil_mc_dtor" %{ll}
// DEFINE: %{dtor} = %{gen} && %{chk}=DTOR && \
// DEFINE:   grep "^define .*@__msil_mc_dtor\." %{ll} | count 1 && \
// DEFINE:   %{dtors} | count 1 && %{dtors} | FileCheck %s --check-prefix=DTORS && \
// DEFINE:   not grep -E "llvm.global_ctors|__msil_mc_ctor" %{ll}
// DEFINE: %{st} = %{gen} && %{chk}=ST && \
// DEFINE:   grep "^define .*@__msil_mc_ctor\." %{ll} | count 1 && \
// DEFINE:   %{ctors} | count 1 && %{ctors} | FileCheck %s --check-prefix=STCTORS && \
// DEFINE:   not grep -E "comdat|llvm.global_dtors|__msil_mc_dtor" %{ll}

// REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
// REDEFINE: %{abi} = ITANIUM
// REDEFINE: %{u} = blk3.c
// RUN: %{ctor}
// REDEFINE: %{u} = blk4.c
// RUN: %{ctor}
// REDEFINE: %{u} = blk6.c
// RUN: %{ctor}
// REDEFINE: %{u} = externc.cpp
// RUN: %{ctor}
// REDEFINE: %{u} = ns.cpp
// RUN: %{ns}
// REDEFINE: %{u} = blk5.c
// RUN: %{dtor}
// REDEFINE: %{u} = st.c
// RUN: %{st}

// REDEFINE: %{t} = msil64-unknown-dotnet
// REDEFINE: %{u} = blk3.c
// RUN: %{ctor}
// REDEFINE: %{u} = blk4.c
// RUN: %{ctor}
// REDEFINE: %{u} = blk6.c
// RUN: %{ctor}
// REDEFINE: %{u} = externc.cpp
// RUN: %{ctor}
// REDEFINE: %{u} = blk7.c
// RUN: %{ctor}
// REDEFINE: %{u} = blocks.c
// REDEFINE: %{f} = -fblocks
// RUN: %{ctor}
// REDEFINE: %{f} =
// REDEFINE: %{u} = captured.c
// RUN: %{ctor}
// REDEFINE: %{u} = lambda.cpp
// RUN: %{ctor}
// REDEFINE: %{u} = member.cpp
// RUN: %{ctor}
// REDEFINE: %{u} = friend.cpp
// RUN: %{ctor}
// REDEFINE: %{u} = localclass.cpp
// RUN: %{ctor}
// REDEFINE: %{u} = tplinst.cpp
// RUN: %{ctor}
// REDEFINE: %{u} = tplprio.cpp
// RUN: %{prio}
// REDEFINE: %{u} = tplpat.cpp
// RUN: %{none}
// REDEFINE: %{u} = tpldep.cpp
// RUN: %{none}
// REDEFINE: %{u} = ns.cpp
// RUN: %{ns}
// REDEFINE: %{u} = blk5.c
// RUN: %{dtor}
// REDEFINE: %{u} = st.c
// RUN: %{st}

// REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
// REDEFINE: %{abi} = MS
// REDEFINE: %{u} = blk3.c
// RUN: %{ctor}
// REDEFINE: %{u} = blk4.c
// RUN: %{ctor}
// REDEFINE: %{u} = blk6.c
// RUN: %{ctor}
// REDEFINE: %{u} = externc.cpp
// RUN: %{ctor}
// REDEFINE: %{u} = ns.cpp
// RUN: %{ns}
// REDEFINE: %{u} = blk5.c
// RUN: %{dtor}
// REDEFINE: %{u} = st.c
// RUN: %{st}

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -verify -emit-llvm -o %t/blkdll.ll %t/blkdll.c && \
// RUN:   FileCheck %s --input-file=%t/blkdll.ll --check-prefix=XDLL && \
// RUN:   FileCheck %s --input-file=%t/blkdll.ll --check-prefix=XDECL && \
// RUN:   grep "^declare " %t/blkdll.ll | count 1 && \
// RUN:   grep "^define .*@k(" %t/blkdll.ll | count 1 && \
// RUN:   not grep -E "llvm.global_[cd]tors|__msil_mc_[cd]tor" %t/blkdll.ll

// The managed call's declaration: the one 'declare' of the unit, with the
// "msil-managed-*" attributes that make it a managed call.
// DECL:        {{^}}declare void @{{"?}}__msil_mc.{{[^(]+}}() #[[DECL_A:[0-9]+]]{{$}}
// DECL:        {{^}}attributes #[[DECL_A]] = { {{.*}}"msil-managed-conv"=";void" "msil-managed-member"="Collect" "msil-managed-sig"="void()" "msil-managed-type"="System.GC"{{.*}} }{{$}}

// blk3.c, blk4.c, blk6.c, blk7.c, blocks.c, captured.c, externc.cpp,
// lambda.cpp, member.cpp, friend.cpp, localclass.cpp and tplinst.cpp.
// CTOR:        {{^}}$__msil_mc_ctor.k = comdat any{{$}}
// CTOR:        {{^}}define linkonce_odr hidden void @__msil_mc_ctor.k() #[[CTOR_A:[0-9]+]] comdat {{\{$}}
// CTOR-NEXT:   {{^}}entry:{{$}}
// CTOR-NEXT:   {{^}}  call void @__msil_mc.k(){{$}}
// CTOR-NEXT:   {{^}}  ret void{{$}}
// CTOR-NEXT:   {{^}}}{{$}}
// CTOR:        {{^}}attributes #[[CTOR_A]] = { {{.*}}"msil-managed-forwarder"="k"{{.*}} }{{$}}
// CTOR:        {{^}}!msil.managed.calls = !{![[#CTOR_M:]]}{{$}}
// CTOR:        {{^}}![[#CTOR_M]] = !{!"__msil_mc.k", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(65535)"}{{$}}
// CTORS:       {{^}}@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @__msil_mc_ctor.k, ptr @__msil_mc_ctor.k }]{{$}}

// tplprio.cpp: the priority of the instantiation, sizeof(int) + 200.
// PRIO:        {{^}}$__msil_mc_ctor.k = comdat any{{$}}
// PRIO:        {{^}}define linkonce_odr hidden void @__msil_mc_ctor.k() #[[PRIO_A:[0-9]+]] comdat {{\{$}}
// PRIO-NEXT:   {{^}}entry:{{$}}
// PRIO-NEXT:   {{^}}  call void @__msil_mc.k(){{$}}
// PRIO-NEXT:   {{^}}  ret void{{$}}
// PRIO-NEXT:   {{^}}}{{$}}
// PRIO:        {{^}}attributes #[[PRIO_A]] = { {{.*}}"msil-managed-forwarder"="k"{{.*}} }{{$}}
// PRIO:        {{^}}!msil.managed.calls = !{![[#PRIO_M:]]}{{$}}
// PRIO:        {{^}}![[#PRIO_M]] = !{!"__msil_mc.k", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(204)"}{{$}}
// PRIOCTORS:   {{^}}@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 204, ptr @__msil_mc_ctor.k, ptr @__msil_mc_ctor.k }]{{$}}

// tplpat.cpp and tpldep.cpp: f()'s call, and an entry with no 8th operand.
// NONE:        {{^}}  call void @__msil_mc.k(){{$}}
// NONE:        {{^}}!msil.managed.calls = !{![[#NONE_M:]]}{{$}}
// NONE:        {{^}}![[#NONE_M]] = !{!"__msil_mc.k", !"System.GC", !"Collect", !"void()", !"", !";void", !""}{{$}}

// ns.cpp on x86_64-unknown-linux-gnu-msil and msil64-unknown-dotnet.
// NS-ITANIUM:      {{^}}$__msil_mc_ctor._ZN1n1kEv = comdat any{{$}}
// NS-ITANIUM:      {{^}}define linkonce_odr hidden void @__msil_mc_ctor._ZN1n1kEv() #[[NSI_A:[0-9]+]] comdat {{\{$}}
// NS-ITANIUM-NEXT: {{^}}entry:{{$}}
// NS-ITANIUM-NEXT: {{^}}  call void @__msil_mc._ZN1n1kEv(){{$}}
// NS-ITANIUM-NEXT: {{^}}  ret void{{$}}
// NS-ITANIUM-NEXT: {{^}}}{{$}}
// NS-ITANIUM:      {{^}}attributes #[[NSI_A]] = { {{.*}}"msil-managed-forwarder"="_ZN1n1kEv"{{.*}} }{{$}}
// NS-ITANIUM:      {{^}}!msil.managed.calls = !{![[#NSI_M:]]}{{$}}
// NS-ITANIUM:      {{^}}![[#NSI_M]] = !{!"__msil_mc._ZN1n1kEv", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(65535)"}{{$}}
// NSCTORS-ITANIUM: {{^}}@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @__msil_mc_ctor._ZN1n1kEv, ptr @__msil_mc_ctor._ZN1n1kEv }]{{$}}

// ns.cpp on x86_64-pc-windows-msvc-msil.
// NS-MS:           {{^}}$"__msil_mc_ctor.?k@n@@YAXXZ" = comdat any{{$}}
// NS-MS:           {{^}}define linkonce_odr hidden void @"__msil_mc_ctor.?k@n@@YAXXZ"() #[[NSM_A:[0-9]+]] comdat {{\{$}}
// NS-MS-NEXT:      {{^}}entry:{{$}}
// NS-MS-NEXT:      {{^}}  call void @"__msil_mc.?k@n@@YAXXZ"(){{$}}
// NS-MS-NEXT:      {{^}}  ret void{{$}}
// NS-MS-NEXT:      {{^}}}{{$}}
// NS-MS:           {{^}}attributes #[[NSM_A]] = { {{.*}}"msil-managed-forwarder"="?k@n@@YAXXZ"{{.*}} }{{$}}
// NS-MS:           {{^}}!msil.managed.calls = !{![[#NSM_M:]]}{{$}}
// NS-MS:           {{^}}![[#NSM_M]] = !{!"__msil_mc.?k@n@@YAXXZ", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(65535)"}{{$}}
// NSCTORS-MS:      {{^}}@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @"__msil_mc_ctor.?k@n@@YAXXZ", ptr @"__msil_mc_ctor.?k@n@@YAXXZ" }]{{$}}

// blk5.c.
// DTOR:        {{^}}$__msil_mc_dtor.k = comdat any{{$}}
// DTOR:        {{^}}define linkonce_odr hidden void @__msil_mc_dtor.k() #[[DTOR_A:[0-9]+]] comdat {{\{$}}
// DTOR-NEXT:   {{^}}entry:{{$}}
// DTOR-NEXT:   {{^}}  call void @__msil_mc.k(){{$}}
// DTOR-NEXT:   {{^}}  ret void{{$}}
// DTOR-NEXT:   {{^}}}{{$}}
// DTOR:        {{^}}attributes #[[DTOR_A]] = { {{.*}}"msil-managed-forwarder"="k"{{.*}} }{{$}}
// DTOR:        {{^}}!msil.managed.calls = !{![[#DTOR_M:]]}{{$}}
// DTOR:        {{^}}![[#DTOR_M]] = !{!"__msil_mc.k", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"destructor(65535)"}{{$}}
// DTORS:       {{^}}@llvm.global_dtors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @__msil_mc_dtor.k, ptr @__msil_mc_dtor.k }]{{$}}

// st.c: internal, no COMDAT, no key, no 8th operand.
// ST:          {{^}}define internal void @__msil_mc_ctor.k() #[[ST_A:[0-9]+]] {{\{$}}
// ST-NEXT:     {{^}}entry:{{$}}
// ST-NEXT:     {{^}}  call void @__msil_mc.k.__uniq.[[ST_U:[0-9]+]](){{$}}
// ST-NEXT:     {{^}}  ret void{{$}}
// ST-NEXT:     {{^}}}{{$}}
// ST:          {{^}}attributes #[[ST_A]] = { {{.*}}"msil-managed-forwarder"="k"{{.*}} }{{$}}
// ST:          {{^}}!msil.managed.calls = !{![[#ST_M:]]}{{$}}
// ST:          {{^}}![[#ST_M]] = !{!"__msil_mc.k.__uniq.[[ST_U]]", !"System.GC", !"Collect", !"void()", !"", !";void", !""}{{$}}
// STCTORS:     {{^}}@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @__msil_mc_ctor.k, ptr null }]{{$}}

// blkdll.c: the export forwarder.
// XDECL:       {{^}}declare i32 @__msil_mc.k(i32 noundef) #[[XDECL_A:[0-9]+]]{{$}}
// XDECL:       {{^}}attributes #[[XDECL_A]] = { {{.*}}"msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math"{{.*}} }{{$}}
// XDLL:        {{^}}$k = comdat any{{$}}
// XDLL:        {{^}}define weak_odr dllexport i32 @k(i32 noundef %0) #[[XDLL_A:[0-9]+]] comdat {{\{$}}
// XDLL-NEXT:   {{^}}entry:{{$}}
// XDLL-NEXT:   {{^}}  %1 = call i32 @__msil_mc.k(i32 noundef %0){{$}}
// XDLL-NEXT:   {{^}}  ret i32 %1{{$}}
// XDLL-NEXT:   {{^}}}{{$}}
// XDLL:        {{^}}attributes #[[XDLL_A]] = { {{.*}}"msil-managed-forwarder"="k"{{.*}} }{{$}}
// XDLL:        {{^}}!msil.managed.calls = !{![[#XDLL_M:]]}{{$}}
// XDLL:        {{^}}![[#XDLL_M]] = !{!"__msil_mc.k", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !"", !"dllexport"}{{$}}

//--- blk3.c
// expected-no-diagnostics
void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
void use(void) { void k(void) __attribute__((constructor)); }

//--- blk4.c
// expected-no-diagnostics
void use(void) { void k(void) __attribute__((msil_method("System.GC", "Collect", "void()"), constructor)); }

//--- blk6.c
// expected-no-diagnostics
void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
static void unused(void) { void k(void) __attribute__((constructor)); }

//--- blk7.c
// expected-no-diagnostics
void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
void use(int x) { if (x) { { void k(void) __attribute__((constructor)); } } }

//--- blocks.c
// expected-no-diagnostics
void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
void use(void) { void (^b)(void) = ^{ void k(void) __attribute__((constructor)); }; (void)b; }

//--- captured.c
// expected-no-diagnostics
void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
void use(void) {
#pragma clang __debug captured
  { void k(void) __attribute__((constructor)); }
}

//--- externc.cpp
// expected-no-diagnostics
extern "C" {
void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
void use(void) { void k(void) __attribute__((constructor)); }
}

//--- lambda.cpp
// expected-no-diagnostics
extern "C" void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
void use() { auto l = [] { void k(void) __attribute__((constructor)); }; (void)l; }

//--- member.cpp
// expected-no-diagnostics
extern "C" void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
struct S { void m() { void k(void) __attribute__((constructor)); } };

//--- friend.cpp
// expected-no-diagnostics
extern "C" void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
struct S { friend void f(S) { void k(void) __attribute__((constructor)); } };

//--- localclass.cpp
// expected-no-diagnostics
extern "C" void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
void use() { struct L { static void m() { void k(void) __attribute__((constructor)); } }; }

//--- tplinst.cpp
// expected-no-diagnostics
extern "C" void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
template <class T> void use() { void k(void) __attribute__((constructor)); }
template void use<int>();

//--- tplprio.cpp
// expected-no-diagnostics
extern "C" void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
template <class T> void use() { void k(void) __attribute__((constructor(sizeof(T) + 200))); }
template void use<int>();

//--- tplpat.cpp
// expected-no-diagnostics
extern "C" void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
template <class T> void use() { void k(void) __attribute__((constructor)); }
void f() { k(); }

//--- tpldep.cpp
// expected-no-diagnostics
extern "C" void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
template <class T> void use() { void k(void) __attribute__((constructor(sizeof(T) + 200))); }
void f() { k(); }

//--- ns.cpp
// expected-no-diagnostics
namespace n {
void k() __attribute__((msil_method("System.GC", "Collect", "void()")));
void use() { void k() __attribute__((constructor)); }
}

//--- blk5.c
// expected-no-diagnostics
void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
void use(void) { void k(void) __attribute__((destructor)); }

//--- st.c
// expected-no-diagnostics
static void k(void) __attribute__((msil_method("System.GC", "Collect", "void()")));
void use(void) { void k(void) __attribute__((constructor)); }

//--- blkdll.c
int k(int) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-note-re {{{{^}}previous declaration is here{{$}}}}
void use(void) { __declspec(dllexport) int k(int); } // expected-warning-re {{{{^}}redeclaration of 'k' should not add 'dllexport' attribute{{$}}}}
