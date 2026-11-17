// The D6 forwarders of managed calls declared in a precompiled header, in a
// clang-cl /Yc and /Yu pair, in a header module or in a C++20 named module
// (ImplementationPlan.md 5.20.4's D6 forwarders, which a unit gets also when
// it never calls the managed call, and section 12, the rows '5.20.3's hook
// list, 5.13.5's MC-1 exemption text and MC-1's review focus 'Hooks only
// (L2)' amended again (MC-1's leftover-audit fix D11, with review-r4's items
// 1 and 8)' and '5.20.3's hook list, 5.13.5's MC-1 exemption text and the
// rows (...) amended (D6 forwarders through modules)'; milestone MC-1). h.h
// declares a constructor(200) managed call and a dllexport one; tu.c holds
// only main and calls neither. The unit built with -include-pch gets what
// the same unit built with -include gets, checked with the same prefixes:
// - CTOR checks '__msil_mc_ctor.gc_at_startup', 'define linkonce_odr hidden
//   void' in a COMDAT of its own name, calling the stub, and the managed
//   call's !msil.managed.calls entry with the eighth operand
//   'constructor(200)'.
// - CTORS checks the llvm.global_ctors entry keyed to that COMDAT, the only
//   one.
// - XABS checks, on x86_64-pc-windows-msvc-msil (-fms-extensions), the export
//   forwarder 'define weak_odr dllexport i32 @exported_abs' in its COMDAT,
//   and the entry with the eighth operand 'dllexport'.
// Each group is checked by a FileCheck run of its own, so the checks do not
// depend on the order of the groups (msil-method-forwarders.c checks the
// forwarders of every kind). Also:
// - calls.c calls both: each forwarder and the ctor entry exactly once, with
//   -include-pch and with -include.
// - clang-cl: /Yc builds the PCH with its unit yc.c and /Yu uses it for yu.c
//   (LLVM IR through /clang:, so no backend runs); both units get the lines.
// - A header module: module.modulemap makes h.h the module H.
//   - H built with -fmodules -emit-module and used with
//     -fmodule-file=H=<file>, and H through implicit modules
//     (-fimplicit-module-maps and a module cache, no -fmodule-file): the
//     remark pins that the #include imports H, from H.pcm or from the
//     cache's H-<hash>.pcm. mod.c (only main) and modcalls.c (calls both)
//     get each forwarder and the ctor entry exactly once.
//   - tu.c, which never includes h.h, with the unnamed -fmodule-file=<file>:
//     a module file loaded at startup gives the forwarders, just as it gives
//     a constructor defined in h.h.
//   - An explicit submodule: in the module S only the explicit submodule
//     S.X's x.h declares a managed call, a constructor(300) one. s.c
//     includes only s.h, so it imports S and never S.X (x.h's macro
//     X_H_IMPORTED stays undefined), and gets that constructor's forwarder
//     and its llvm.global_ctors entry, the only one (SUB, SUBCTORS), and
//     nothing of h.h.
// - h.hpp with -x c++-header: the same declarations in namespace ns, under
//   their Microsoft mangled names (CXXCTOR, CXXCTORS, CXXABS, with NS set
//   to 'ns@').
// - A C++20 named module: m.cppm exports both managed calls at namespace
//   scope, so the same prefixes check them with NS empty
//   ('?exported_abs@@YAHH@Z', '__msil_mc_ctor.?gc_at_startup@@YAXXZ'). The
//   module unit's IR has each forwarder and the ctor entry exactly once,
//   compiled from its BMI (-emit-module-interface, then -emit-llvm of the
//   .pcm, as the driver's two-phase compile builds its object) and from its
//   source. An importer (-fmodule-file=M=<file>) defines them only when it
//   names the managed calls (COMDAT keeps one per program): mimport.cpp
//   names neither and has none of them; mcalls.cpp calls both and has each
//   exactly once.
// - x86_64-unknown-linux-gnu-msil and msil64-unknown-dotnet: the constructor
//   (CTOR, CTORS). dllexport is ignored there (clang's 'unknown attribute'
//   warning, silenced here), so nothing of exported_abs is emitted.

// RUN: rm -rf %t && split-file %s %t
// DEFINE: %{t} =
// DEFINE: %{ll} =
// DEFINE: %{ns} = ns@
// DEFINE: %{ctor} = FileCheck %s --input-file=%{ll} --check-prefix=CTOR && \
// DEFINE:   grep "^@llvm.global_ctors = " %{ll} | FileCheck %s --check-prefix=CTORS
// DEFINE: %{win} = %{ctor} && FileCheck %s --input-file=%{ll} --check-prefix=XABS
// DEFINE: %{once} = grep "^define .*@__msil_mc_ctor\.gc_at_startup" %{ll} | count 1 && \
// DEFINE:   grep "^define .*@exported_abs" %{ll} | count 1
// DEFINE: %{cxx} = FileCheck %s --input-file=%{ll} --check-prefix=CXXCTOR -DNS=%{ns} && \
// DEFINE:   grep "^@llvm.global_ctors = " %{ll} | FileCheck %s --check-prefix=CXXCTORS -DNS=%{ns} && \
// DEFINE:   FileCheck %s --input-file=%{ll} --check-prefix=CXXABS -DNS=%{ns}
// DEFINE: %{cxxonce} = grep "^define .*__msil_mc_ctor\.?gc_at_startup@" %{ll} | count 1 && \
// DEFINE:   grep "^define .*?exported_abs@" %{ll} | count 1
// DEFINE: %{noexp} = %{ctor} && not grep exported_abs %{ll}

// The Windows triple, C: the PCH, the unit with -include-pch and with
// -include, then calls.c both ways.
// REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
// RUN: %clang_cc1 -triple %{t} -fms-extensions -emit-pch -x c-header -o %t/h.pch %t/h.h
// REDEFINE: %{ll} = %t/tu-pch.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -include-pch %t/h.pch -emit-llvm -o %{ll} %t/tu.c && %{win}
// REDEFINE: %{ll} = %t/tu-inc.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -include %t/h.h -emit-llvm -o %{ll} %t/tu.c && %{win}
// REDEFINE: %{ll} = %t/calls-pch.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -include-pch %t/h.pch -emit-llvm -o %{ll} %t/calls.c && %{win} && %{once}
// REDEFINE: %{ll} = %t/calls-inc.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -include %t/h.h -emit-llvm -o %{ll} %t/calls.c && %{win} && %{once}

// clang-cl /Yc and /Yu.
// RUN: %clang_cl --target=%{t} /Ych.h /Fp%t/cl.pch /clang:-S /clang:-emit-llvm /Fo%t/yc.ll -- %t/yc.c
// REDEFINE: %{ll} = %t/yc.ll
// RUN: %{win}
// RUN: %clang_cl --target=%{t} /Yuh.h /Fp%t/cl.pch /clang:-S /clang:-emit-llvm /Fo%t/yu.ll -- %t/yu.c
// REDEFINE: %{ll} = %t/yu.ll
// RUN: %{win}

// The header module H, built with -emit-module and used with
// -fmodule-file=H=<file>.
// RUN: %clang_cc1 -triple %{t} -fms-extensions -fmodules -fmodule-name=H -emit-module -x c -o %t/H.pcm %t/module.modulemap
// REDEFINE: %{ll} = %t/mod.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -fmodules -fno-implicit-modules -fmodule-map-file=%t/module.modulemap -fmodule-file=H=%t/H.pcm -Rmodule-import -verify -emit-llvm -o %{ll} %t/mod.c && %{win} && %{once}
// REDEFINE: %{ll} = %t/modcalls.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -fmodules -fno-implicit-modules -fmodule-map-file=%t/module.modulemap -fmodule-file=H=%t/H.pcm -Rmodule-import -verify -emit-llvm -o %{ll} %t/modcalls.c && %{win} && %{once}
// H through implicit modules.
// REDEFINE: %{ll} = %t/mod-implicit.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache -Rmodule-import -verify -emit-llvm -o %{ll} %t/mod.c && %{win} && %{once}
// REDEFINE: %{ll} = %t/modcalls-implicit.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache -Rmodule-import -verify -emit-llvm -o %{ll} %t/modcalls.c && %{win} && %{once}
// H.pcm loaded at startup by the unnamed -fmodule-file, for tu.c.
// REDEFINE: %{ll} = %t/tu-modfile.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -fmodules -fno-implicit-modules -fmodule-file=%t/H.pcm -emit-llvm -o %{ll} %t/tu.c && %{win} && %{once}
// The module S, for s.c, which does not import its explicit submodule S.X.
// RUN: %clang_cc1 -triple %{t} -fms-extensions -fmodules -fmodule-name=S -emit-module -x c -o %t/S.pcm %t/module.modulemap
// REDEFINE: %{ll} = %t/s.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -fmodules -fno-implicit-modules -fmodule-map-file=%t/module.modulemap -fmodule-file=S=%t/S.pcm -Rmodule-import -verify -emit-llvm -o %{ll} %t/s.c
// RUN: FileCheck %s --input-file=%{ll} --check-prefix=SUB
// RUN: grep "^@llvm.global_ctors = " %{ll} | FileCheck %s --check-prefix=SUBCTORS
// RUN: not grep -E "gc_at_startup|exported_abs" %{ll}

// C++.
// RUN: %clang_cc1 -triple %{t} -fms-extensions -emit-pch -x c++-header -o %t/h.hpp.pch %t/h.hpp
// REDEFINE: %{ll} = %t/cxx-pch.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -include-pch %t/h.hpp.pch -emit-llvm -o %{ll} %t/tu.cpp && %{cxx}
// REDEFINE: %{ll} = %t/cxx-inc.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -include %t/h.hpp -emit-llvm -o %{ll} %t/tu.cpp && %{cxx}

// The C++20 named module M: the module unit from its BMI and from its
// source, then an importer that names neither managed call and one that
// calls both.
// REDEFINE: %{ns} =
// RUN: %clang_cc1 -triple %{t} -fms-extensions -std=c++20 -emit-module-interface -o %t/M.pcm %t/m.cppm
// REDEFINE: %{ll} = %t/m-bmi.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -std=c++20 -emit-llvm -o %{ll} %t/M.pcm && %{cxx} && %{cxxonce}
// REDEFINE: %{ll} = %t/m-src.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -std=c++20 -emit-llvm -o %{ll} %t/m.cppm && %{cxx} && %{cxxonce}
// REDEFINE: %{ll} = %t/mimport.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -std=c++20 -fmodule-file=M=%t/M.pcm -emit-llvm -o %{ll} %t/mimport.cpp
// RUN: not grep -E "gc_at_startup|exported_abs" %{ll}
// REDEFINE: %{ll} = %t/mcalls.ll
// RUN: %clang_cc1 -triple %{t} -fms-extensions -std=c++20 -fmodule-file=M=%t/M.pcm -emit-llvm -o %{ll} %t/mcalls.cpp && %{cxx} && %{cxxonce}

// The constructor on the other triples.
// REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
// RUN: %clang_cc1 -triple %{t} -Wno-unknown-attributes -emit-pch -x c-header -o %t/h.%{t}.pch %t/h.h
// REDEFINE: %{ll} = %t/tu-pch.%{t}.ll
// RUN: %clang_cc1 -triple %{t} -Wno-unknown-attributes -include-pch %t/h.%{t}.pch -emit-llvm -o %{ll} %t/tu.c && %{noexp}
// REDEFINE: %{ll} = %t/tu-inc.%{t}.ll
// RUN: %clang_cc1 -triple %{t} -Wno-unknown-attributes -include %t/h.h -emit-llvm -o %{ll} %t/tu.c && %{noexp}
// REDEFINE: %{t} = msil64-unknown-dotnet
// RUN: %clang_cc1 -triple %{t} -Wno-unknown-attributes -emit-pch -x c-header -o %t/h.%{t}.pch %t/h.h
// REDEFINE: %{ll} = %t/tu-pch.%{t}.ll
// RUN: %clang_cc1 -triple %{t} -Wno-unknown-attributes -include-pch %t/h.%{t}.pch -emit-llvm -o %{ll} %t/tu.c && %{noexp}
// REDEFINE: %{ll} = %t/tu-inc.%{t}.ll
// RUN: %clang_cc1 -triple %{t} -Wno-unknown-attributes -include %t/h.h -emit-llvm -o %{ll} %t/tu.c && %{noexp}

// CTOR:          {{^}}$__msil_mc_ctor.gc_at_startup = comdat any{{$}}
// CTOR:          {{^}}declare void @__msil_mc.gc_at_startup() #{{[0-9]+}}{{$}}
// CTOR:          {{^}}define linkonce_odr hidden void @__msil_mc_ctor.gc_at_startup() #[[CTOR_A:[0-9]+]] comdat {{\{$}}
// CTOR-NEXT:     {{^}}entry:{{$}}
// CTOR-NEXT:     {{^}}  call void @__msil_mc.gc_at_startup(){{$}}
// CTOR-NEXT:     {{^}}  ret void{{$}}
// CTOR-NEXT:     {{^}}}{{$}}
// CTOR:          {{^}}attributes #[[CTOR_A]] = { {{.*}}"msil-managed-forwarder"="gc_at_startup"{{.*}} }{{$}}
// CTOR:          {{^}}!{{[0-9]+}} = !{!"__msil_mc.gc_at_startup", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(200)"}{{$}}

// CTORS:         {{^}}@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 200, ptr @__msil_mc_ctor.gc_at_startup, ptr @__msil_mc_ctor.gc_at_startup }]{{$}}

// XABS:          {{^}}$exported_abs = comdat any{{$}}
// XABS:          {{^}}declare i32 @__msil_mc.exported_abs(i32 noundef) #{{[0-9]+}}{{$}}
// XABS:          {{^}}define weak_odr dllexport i32 @exported_abs(i32 noundef %0) #[[XABS_A:[0-9]+]] comdat {{\{$}}
// XABS-NEXT:     {{^}}entry:{{$}}
// XABS-NEXT:     {{^}}  %1 = call i32 @__msil_mc.exported_abs(i32 noundef %0){{$}}
// XABS-NEXT:     {{^}}  ret i32 %1{{$}}
// XABS-NEXT:     {{^}}}{{$}}
// XABS:          {{^}}attributes #[[XABS_A]] = { {{.*}}"msil-managed-forwarder"="exported_abs"{{.*}} }{{$}}
// XABS:          {{^}}!{{[0-9]+}} = !{!"__msil_mc.exported_abs", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !"", !"dllexport"}{{$}}

// SUB:           {{^}}$__msil_mc_ctor.x_startup = comdat any{{$}}
// SUB:           {{^}}declare void @__msil_mc.x_startup() #{{[0-9]+}}{{$}}
// SUB:           {{^}}define linkonce_odr hidden void @__msil_mc_ctor.x_startup() #[[SUB_A:[0-9]+]] comdat {{\{$}}
// SUB-NEXT:      {{^}}entry:{{$}}
// SUB-NEXT:      {{^}}  call void @__msil_mc.x_startup(){{$}}
// SUB-NEXT:      {{^}}  ret void{{$}}
// SUB-NEXT:      {{^}}}{{$}}
// SUB:           {{^}}attributes #[[SUB_A]] = { {{.*}}"msil-managed-forwarder"="x_startup"{{.*}} }{{$}}
// SUB:           {{^}}!{{[0-9]+}} = !{!"__msil_mc.x_startup", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(300)"}{{$}}

// SUBCTORS:      {{^}}@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 300, ptr @__msil_mc_ctor.x_startup, ptr @__msil_mc_ctor.x_startup }]{{$}}

// CXXCTOR:       {{^}}$"__msil_mc_ctor.?gc_at_startup@[[NS]]@YAXXZ" = comdat any{{$}}
// CXXCTOR:       {{^}}declare void @"__msil_mc.?gc_at_startup@[[NS]]@YAXXZ"() #{{[0-9]+}}{{$}}
// CXXCTOR:       {{^}}define linkonce_odr hidden void @"__msil_mc_ctor.?gc_at_startup@[[NS]]@YAXXZ"() #[[CXXCTOR_A:[0-9]+]] comdat {{\{$}}
// CXXCTOR-NEXT:  {{^}}entry:{{$}}
// CXXCTOR-NEXT:  {{^}}  call void @"__msil_mc.?gc_at_startup@[[NS]]@YAXXZ"(){{$}}
// CXXCTOR-NEXT:  {{^}}  ret void{{$}}
// CXXCTOR-NEXT:  {{^}}}{{$}}
// CXXCTOR:       {{^}}attributes #[[CXXCTOR_A]] = { {{.*}}"msil-managed-forwarder"="?gc_at_startup@[[NS]]@YAXXZ"{{.*}} }{{$}}
// CXXCTOR:       {{^}}!{{[0-9]+}} = !{!"__msil_mc.?gc_at_startup@[[NS]]@YAXXZ", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(200)"}{{$}}

// CXXCTORS:      {{^}}@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 200, ptr @"__msil_mc_ctor.?gc_at_startup@[[NS]]@YAXXZ", ptr @"__msil_mc_ctor.?gc_at_startup@[[NS]]@YAXXZ" }]{{$}}

// CXXABS:        {{^}}$"?exported_abs@[[NS]]@YAHH@Z" = comdat any{{$}}
// CXXABS:        {{^}}declare noundef i32 @"__msil_mc.?exported_abs@[[NS]]@YAHH@Z"(i32 noundef) #{{[0-9]+}}{{$}}
// CXXABS:        {{^}}define weak_odr dllexport noundef i32 @"?exported_abs@[[NS]]@YAHH@Z"(i32 noundef %0) #[[CXXABS_A:[0-9]+]] comdat {{\{$}}
// CXXABS-NEXT:   {{^}}entry:{{$}}
// CXXABS-NEXT:   {{^}}  %1 = call noundef i32 @"__msil_mc.?exported_abs@[[NS]]@YAHH@Z"(i32 noundef %0){{$}}
// CXXABS-NEXT:   {{^}}  ret i32 %1{{$}}
// CXXABS-NEXT:   {{^}}}{{$}}
// CXXABS:        {{^}}attributes #[[CXXABS_A]] = { {{.*}}"msil-managed-forwarder"="?exported_abs@[[NS]]@YAHH@Z"{{.*}} }{{$}}
// CXXABS:        {{^}}!{{[0-9]+}} = !{!"__msil_mc.?exported_abs@[[NS]]@YAHH@Z", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !"", !"dllexport"}{{$}}

//--- h.h
__attribute__((constructor(200), msil_method("System.GC", "Collect", "void()")))
void gc_at_startup(void);
__attribute__((dllexport, msil_method("System.Math", "Abs", "int32(int32)")))
int exported_abs(int);

//--- tu.c
int main(void) { return 0; }

//--- calls.c
int main(void) { gc_at_startup(); return exported_abs(-3); }

//--- yc.c
#include "h.h"

//--- yu.c
#include "h.h"
int main(void) { return 0; }

//--- module.modulemap
module H { header "h.h" export * }
module S { header "s.h" explicit module X { header "x.h" } }

//--- mod.c
#include "h.h" // expected-remark-re {{{{^}}importing module 'H' from '{{.*}}H{{(-[0-9A-Z]+)?}}.pcm'{{$}}}}
int main(void) { return 0; }

//--- modcalls.c
#include "h.h" // expected-remark-re {{{{^}}importing module 'H' from '{{.*}}H{{(-[0-9A-Z]+)?}}.pcm'{{$}}}}
int main(void) { gc_at_startup(); return exported_abs(-3); }

//--- s.h
int plain(int);

//--- x.h
__attribute__((constructor(300), msil_method("System.GC", "Collect", "void()")))
void x_startup(void);
#define X_H_IMPORTED 1

//--- s.c
#include "s.h" // expected-remark-re {{{{^}}importing module 'S' from '{{.*}}S.pcm'{{$}}}}
#ifdef X_H_IMPORTED
#error s.c imports S.X
#endif
int main(void) { return 0; }

//--- h.hpp
namespace ns {
__attribute__((constructor(200), msil_method("System.GC", "Collect", "void()")))
void gc_at_startup();
__attribute__((dllexport, msil_method("System.Math", "Abs", "int32(int32)")))
int exported_abs(int);
} // namespace ns

//--- tu.cpp
int main() { return 0; }

//--- m.cppm
export module M;
export {
__attribute__((constructor(200), msil_method("System.GC", "Collect", "void()")))
void gc_at_startup();
__attribute__((dllexport, msil_method("System.Math", "Abs", "int32(int32)")))
int exported_abs(int);
}

//--- mimport.cpp
import M;
int main() { return 0; }

//--- mcalls.cpp
import M;
int main() { gc_at_startup(); return exported_abs(-3); }
