; The forwarders of exported and startup managed calls in the backend
; (ImplementationPlan.md 5.20.4 as amended for dllexport, constructor and
; destructor on a managed-call declaration; milestone MC-1). clang's CodeGen
; defines them (clang/test/CodeGen/MSIL/msil-method-forwarders.c); each
; carries "msil-managed-forwarder"="<s>", <s> being the managed call's C
; name, and calls the stub '__msil_mc.<s>':
; - dllexp.ll, on the Windows triples: dllexport is '<s>' itself, weak_odr
;   dllexport in a COMDAT of its own name, and the object's link info gives
;   it as a WEAK DLLEXPORT function with that COMDAT, so msil-ld exports it
;   once. The stub stays a LOCAL function.
; - ctors.ll, on both host x86_64 triples and msil64-unknown-dotnet:
;   constructor on an external declaration is '__msil_mc_ctor.<s>',
;   linkonce_odr hidden in its own COMDAT, whose llvm.global_ctors entry is
;   keyed to that COMDAT (WEAK HIDDEN, 'Init ... comdat=<n>', so it runs once
;   per program); on a 'static' declaration it is internal and unkeyed (a
;   LOCAL function and an 'Init' without a COMDAT, so it runs once per unit),
;   and it calls the internal stub '__msil_mc.<s>.__uniq.<n>'. A priority is
;   kept. The module initializer calls the forwarders in priority order, then
;   in their order in llvm.global_ctors.
;   Both use only the link-info records MC-1 had before, with no new record
;   minor (plan 5.20, 'no link-info record minor'): the dumps are 'LinkInfo
;   v1.0', then Symbol, Comdat and Init lines and nothing else.
; - dtor.ll: a destructor forwarder '__msil_mc_dtor.<s>' meets the error
;   that every global destructor gets until destructors work (M29 on the
;   host triples, P-2's LowerGlobalDtors on the portable ones), naming the
;   C function: 'global destructor @<s> is not supported for MSIL yet', on
;   the three triples at -O0 and -O2, in text and objects.
; - cargs.ll: a constructor forwarder with a parameter gets the native
;   constructor's text naming the C function: 'global constructor '<s>'
;   takes arguments, which MSIL does not pass' on a Windows triple and
;   'constructor '<s>' takes arguments, which msil64-unknown-dotnet does not
;   pass' on the portable triple, in text and objects; under ELF rules
;   (x86_64-unknown-linux-gnu-msil) an (int32) parameter list is a prefix of
;   (argc, argv, envp) and compiles. clang's Sema refuses the C forms first
;   (T-M19, clang/test/Sema/attr-msil-method.c).
; - addrfwd.ll, on two Windows triples: T-M10 stays as it is where a
;   dllexport forwarder exists (plan 5.20.14 lifts it with the fuller managed
;   calls): the address of the managed-call declaration is refused once,
;   'taking the address of the managed-call function '<s>' is not supported
;   for MSIL yet', and never becomes the forwarder's address, in text and
;   objects.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{one} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not="Stack dump" --implicit-check-not=error: --implicit-check-not=warning:
; DEFINE: %{t} =
; DEFINE: %{o} =

; DEFINE: %{exp} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %t/dllexp.ll \
; DEFINE:   -o %t/dllexp.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --link-info %t/dllexp.%{t}%{o}.o | FileCheck %s --check-prefix=EXP
; REDEFINE: %{o} = -O0
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %if llvm-cil-dump %{ %{exp} %}
; REDEFINE: %{t} = aarch64-w64-windows-gnu-msil
; RUN: %if llvm-cil-dump %{ %{exp} %}
; REDEFINE: %{o} = -O2
; REDEFINE: %{t} = x86_64-w64-windows-gnu-msil
; RUN: %if llvm-cil-dump %{ %{exp} %}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; RUN: %if llvm-cil-dump %{ %{exp} %}

; DEFINE: %{ctors} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/ctors.ll \
; DEFINE:   -o %t/ctors.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefix=CTORS-IL --input-file=%t/ctors.%{t}%{o}.il && \
; DEFINE:   llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %t/ctors.ll \
; DEFINE:   -o %t/ctors.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --link-info %t/ctors.%{t}%{o}.o | FileCheck %s --check-prefix=CTORS
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{ctors} %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{ctors} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %if llvm-cil-dump %{ %{ctors} %}
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{ctors} %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/ctors.lnx.dll %t/ctors.x86_64-unknown-linux-gnu-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/ctors.p64.dll %t/ctors.msil64-unknown-dotnet-O2.il %}

; DEFINE: %{dtor} = not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/dtor.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=DTOR %{one} && \
; DEFINE:   not llc -mtriple=%{t} %{o} -filetype=obj < %t/dtor.ll -o %t/dtor.o 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=DTOR %{one}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{dtor}
; REDEFINE: %{o} = -O2
; RUN: %{dtor}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; RUN: %{dtor}
; REDEFINE: %{o} = -O0
; RUN: %{dtor}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{dtor}
; REDEFINE: %{o} = -O2
; RUN: %{dtor}

; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/cargs.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=CARGS-WIN %{one}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -filetype=obj < %t/cargs.ll -o %t/cargs.o 2>&1 \
; RUN:   | FileCheck %s --check-prefix=CARGS-WIN %{one}
; RUN: not llc -mtriple=msil64-unknown-dotnet -O2 -verify-machineinstrs < %t/cargs.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=CARGS-P64 %{one}
; RUN: not llc -mtriple=msil64-unknown-dotnet -O0 -filetype=obj < %t/cargs.ll -o %t/cargs.o 2>&1 \
; RUN:   | FileCheck %s --check-prefix=CARGS-P64 %{one}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -verify-machineinstrs < %t/cargs.ll -o /dev/null 2>&1 | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -filetype=obj < %t/cargs.ll -o %t/cargs.o 2>&1 | count 0

; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/addrfwd.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=ADDR %{one}
; RUN: not llc -mtriple=aarch64-w64-windows-gnu-msil -O2 -filetype=obj < %t/addrfwd.ll -o %t/addrfwd.o 2>&1 \
; RUN:   | FileCheck %s --check-prefix=ADDR %{one}

; EXP:      {{^}}LinkInfo v1.0 triple={{.+}}{{$}}
; EXP-NEXT: {{^}}Symbol [[#FX:]] FUNCTION WEAK DEFAULT DLLEXPORT 'fx_abs' method=0x06{{[0-9A-F]+}}{{$}}
; EXP-NEXT: {{^}}Symbol [[#INIT:]] FUNCTION WEAK HIDDEN '__msil_mc_ctor.fx_init' method=0x06{{[0-9A-F]+}}{{$}}
; EXP-NEXT: {{^}}Symbol [[#]] FUNCTION LOCAL DEFAULT '__msil_mc.fx_abs' method=0x06{{[0-9A-F]+}}{{$}}
; EXP-NEXT: {{^}}Symbol [[#]] FUNCTION LOCAL DEFAULT '__msil_mc.fx_init' method=0x06{{[0-9A-F]+}}{{$}}
; EXP-NEXT: {{^}}Comdat 'fx_abs' ANY function=[[#FX]]{{$}}
; EXP-NEXT: {{^}}Comdat '__msil_mc_ctor.fx_init' ANY function=[[#INIT]]{{$}}
; EXP-NEXT: {{^}}Init priority=65535 symbol=[[#INIT]] '__msil_mc_ctor.fx_init' comdat=1{{$}}
; EXP-NOT:  {{.}}

; CTORS:      {{^}}LinkInfo v1.0 triple={{.+}}{{$}}
; CTORS-NEXT: {{^}}Symbol [[#INIT:]] FUNCTION WEAK HIDDEN '__msil_mc_ctor.fx_init' method=0x06{{[0-9A-F]+}}{{$}}
; CTORS-NEXT: {{^}}Symbol [[#SINIT:]] FUNCTION LOCAL DEFAULT '__msil_mc_ctor.fx_sinit' method=0x06{{[0-9A-F]+}}{{$}}
; CTORS-NEXT: {{^}}Symbol [[#PRIO:]] FUNCTION WEAK HIDDEN '__msil_mc_ctor.fx_prio' method=0x06{{[0-9A-F]+}}{{$}}
; CTORS-NEXT: {{^}}Symbol [[#]] FUNCTION LOCAL DEFAULT '__msil_mc.fx_init' method=0x06{{[0-9A-F]+}}{{$}}
; CTORS-NEXT: {{^}}Symbol [[#]] FUNCTION LOCAL DEFAULT '__msil_mc.fx_sinit.__uniq.1234' method=0x06{{[0-9A-F]+}}{{$}}
; CTORS-NEXT: {{^}}Symbol [[#]] FUNCTION LOCAL DEFAULT '__msil_mc.fx_prio' method=0x06{{[0-9A-F]+}}{{$}}
; CTORS-NEXT: {{^}}Comdat '__msil_mc_ctor.fx_init' ANY function=[[#INIT]]{{$}}
; CTORS-NEXT: {{^}}Comdat '__msil_mc_ctor.fx_prio' ANY function=[[#PRIO]]{{$}}
; CTORS-NEXT: {{^}}Init priority=65535 symbol=[[#INIT]] '__msil_mc_ctor.fx_init' comdat=0{{$}}
; CTORS-NEXT: {{^}}Init priority=65535 symbol=[[#SINIT]] '__msil_mc_ctor.fx_sinit'{{$}}
; CTORS-NEXT: {{^}}Init priority=101 symbol=[[#PRIO]] '__msil_mc_ctor.fx_prio' comdat=1{{$}}
; CTORS-NOT:  {{.}}

; CTORS-IL-LABEL: {{^}}.method {{.+}} void '__msil_mc_ctor.fx_init'() cil managed{{$}}
; CTORS-IL:       {{^}}  call void '__msil_mc.fx_init'(){{$}}
; CTORS-IL-NEXT:  {{^}}  ret{{$}}
; CTORS-IL-LABEL: {{^}}.method {{.+}} void '__msil_mc_ctor.fx_sinit'() cil managed{{$}}
; CTORS-IL:       {{^}}  call void '__msil_mc.fx_sinit.__uniq.1234'(){{$}}
; CTORS-IL-NEXT:  {{^}}  ret{{$}}
; CTORS-IL-LABEL: {{^}}.method {{.+}} void '__msil_mc_ctor.fx_prio'() cil managed{{$}}
; CTORS-IL:       {{^}}  call void '__msil_mc.fx_prio'(){{$}}
; CTORS-IL-NEXT:  {{^}}  ret{{$}}
; CTORS-IL-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CTORS-IL:       {{^}}  call void '__msil_mc_ctor.fx_prio'(){{$}}
; CTORS-IL-NEXT:  {{^}}  call void '__msil_mc_ctor.fx_init'(){{$}}
; CTORS-IL-NEXT:  {{^}}  call void '__msil_mc_ctor.fx_sinit'(){{$}}
; CTORS-IL-NEXT:  {{^}}  ret{{$}}

; DTOR: {{^}}error: global destructor @fx_fini is not supported for MSIL yet{{$}}

; CARGS-WIN: {{^}}error: global constructor 'fx_args' takes arguments, which MSIL does not pass{{$}}

; CARGS-P64: {{^}}error: constructor 'fx_args' takes arguments, which msil64-unknown-dotnet does not pass{{$}}

; ADDR: {{^}}error: <unknown>:0:0: in function addr ptr (): taking the address of the managed-call function 'fx_abs' is not supported for MSIL yet{{$}}

;--- dllexp.ll
source_filename = "dllexp.c"

$fx_abs = comdat any
$__msil_mc_ctor.fx_init = comdat any

@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @__msil_mc_ctor.fx_init, ptr @__msil_mc_ctor.fx_init }]

define weak_odr dllexport i32 @fx_abs(i32 noundef %x) #2 comdat {
  %r = call i32 @__msil_mc.fx_abs(i32 noundef %x)
  ret i32 %r
}

define linkonce_odr hidden void @__msil_mc_ctor.fx_init() #3 comdat {
  call void @__msil_mc.fx_init()
  ret void
}

declare i32 @__msil_mc.fx_abs(i32 noundef) #0
declare void @__msil_mc.fx_init() #1

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #1 = { "msil-managed-conv"=";void" "msil-managed-member"="Collect" "msil-managed-sig"="void()" "msil-managed-type"="System.GC" }
attributes #2 = { "msil-managed-forwarder"="fx_abs" }
attributes #3 = { "msil-managed-forwarder"="fx_init" }

!msil.managed.calls = !{!0, !1}
!0 = !{!"__msil_mc.fx_abs", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !"", !"dllexport"}
!1 = !{!"__msil_mc.fx_init", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(65535)"}

;--- ctors.ll
source_filename = "ctors.c"

$__msil_mc_ctor.fx_init = comdat any
$__msil_mc_ctor.fx_prio = comdat any

@llvm.global_ctors = appending global [3 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @__msil_mc_ctor.fx_init, ptr @__msil_mc_ctor.fx_init }, { i32, ptr, ptr } { i32 65535, ptr @__msil_mc_ctor.fx_sinit, ptr null }, { i32, ptr, ptr } { i32 101, ptr @__msil_mc_ctor.fx_prio, ptr @__msil_mc_ctor.fx_prio }]

define linkonce_odr hidden void @__msil_mc_ctor.fx_init() #2 comdat {
  call void @__msil_mc.fx_init()
  ret void
}

define internal void @__msil_mc_ctor.fx_sinit() #3 {
  call void @__msil_mc.fx_sinit.__uniq.1234()
  ret void
}

define linkonce_odr hidden void @__msil_mc_ctor.fx_prio() #4 comdat {
  call void @__msil_mc.fx_prio()
  ret void
}

declare void @__msil_mc.fx_init() #1
declare void @__msil_mc.fx_sinit.__uniq.1234() #1
declare void @__msil_mc.fx_prio() #1

attributes #1 = { "msil-managed-conv"=";void" "msil-managed-member"="Collect" "msil-managed-sig"="void()" "msil-managed-type"="System.GC" }
attributes #2 = { "msil-managed-forwarder"="fx_init" }
attributes #3 = { "msil-managed-forwarder"="fx_sinit" }
attributes #4 = { "msil-managed-forwarder"="fx_prio" }

!msil.managed.calls = !{!0, !1, !2}
!0 = !{!"__msil_mc.fx_init", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(65535)"}
!1 = !{!"__msil_mc.fx_sinit.__uniq.1234", !"System.GC", !"Collect", !"void()", !"", !";void", !""}
!2 = !{!"__msil_mc.fx_prio", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(101)"}

;--- dtor.ll
source_filename = "dtor.c"

$__msil_mc_dtor.fx_fini = comdat any

@llvm.global_dtors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @__msil_mc_dtor.fx_fini, ptr @__msil_mc_dtor.fx_fini }]

define linkonce_odr hidden void @__msil_mc_dtor.fx_fini() #1 comdat {
  call void @__msil_mc.fx_fini()
  ret void
}

declare void @__msil_mc.fx_fini() #0

attributes #0 = { "msil-managed-conv"=";void" "msil-managed-member"="Collect" "msil-managed-sig"="void()" "msil-managed-type"="System.GC" }
attributes #1 = { "msil-managed-forwarder"="fx_fini" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.fx_fini", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"destructor(65535)"}

;--- cargs.ll
source_filename = "cargs.c"

$__msil_mc_ctor.fx_args = comdat any

@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @__msil_mc_ctor.fx_args, ptr @__msil_mc_ctor.fx_args }]

define linkonce_odr hidden void @__msil_mc_ctor.fx_args(i32 noundef %a) #1 comdat {
  call void @__msil_mc.fx_args(i32 noundef %a)
  ret void
}

declare void @__msil_mc.fx_args(i32 noundef) #0

attributes #0 = { "msil-managed-conv"="val;void" "msil-managed-member"="Collect" "msil-managed-sig"="void(int32)" "msil-managed-type"="System.GC" }
attributes #1 = { "msil-managed-forwarder"="fx_args" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.fx_args", !"System.GC", !"Collect", !"void(int32)", !"", !"val;void", !"", !"constructor(65535)"}

;--- addrfwd.ll
source_filename = "addrfwd.c"

$fx_abs = comdat any

define weak_odr dllexport i32 @fx_abs(i32 noundef %x) #1 comdat {
  %r = call i32 @__msil_mc.fx_abs(i32 noundef %x)
  ret i32 %r
}

define ptr @addr() {
  ret ptr @__msil_mc.fx_abs
}

declare i32 @__msil_mc.fx_abs(i32 noundef) #0

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #1 = { "msil-managed-forwarder"="fx_abs" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.fx_abs", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !"", !"dllexport"}
