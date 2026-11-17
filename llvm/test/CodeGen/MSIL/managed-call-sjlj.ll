; Managed-call stubs in a setjmp caller (ImplementationPlan.md 5.15.4 'The
; transformation', 5.20.5 ML2 (1); milestone MC-1). P0 (MSILPrepareIR, which
; runs first of the target's IR passes) makes each declaration a stub, an
; internal 'noinline' definition whose IR body is 'unreachable', and P8
; (msil-lower-sjlj) treats a call of a stub as it treats a call of any other
; callee that may longjmp, because a C function that .NET code calls back
; can longjmp across the stub (for example .NET code that calls a C function
; pointer it was given as a 'native int' argument). MSILLowerSjLj has no
; exemption for "msil-managed-stub" callees (MC-1's review-r5 item 2 removed
; it). So in @caller every call after the setjmp site goes through a
; wrapper, one per (callee, call-site type) in first-use order:
; '__msil_sjlj_invoke.0' around '__msil_mc.m_abs' (both of its calls),
; '__msil_sjlj_invoke.1' around '__msil_mc.m_keepalive' and
; '__msil_sjlj_invoke.2' around the external @ext. Each outer call passes the
; call's arguments with their attributes (the first m_abs call the result of
; the setjmp site, m_keepalive the handle %h, ext and the second m_abs call
; %a) and then the status block, and loads the label after it; each wrapper
; calls its callee with its own parameters, and in IL inside its '.try'.
; No direct call of a stub is left, the caller calls no stub, and the stubs
; keep their bodies ('unreachable' in IR, the BCL call in IL). Checked after
; msil-lower-sjlj and in object-form text (undefined functions are allowed
; there) at -O0 and -O2 on both host x86_64 triples and
; msil64-unknown-dotnet.

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{run} = llc -mtriple=%{t} %{o} -stop-after=msil-lower-sjlj < %s -o %t.%{t}%{o}.mir && \
; DEFINE:   FileCheck %s --input-file=%t.%{t}%{o}.mir --implicit-check-not=@__msil_sjlj_invoke \
; DEFINE:     --implicit-check-not="call i32 @_setjmp" --implicit-check-not="{{call [a-z0-9]+ @__msil_mc}}" && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:     -o %t.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefix=IL --input-file=%t.%{t}%{o}.il --implicit-check-not=__msil_sjlj_invoke \
; DEFINE:     --implicit-check-not="{{call [a-z0-9]+ .__msil_mc}}"

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{run}
; REDEFINE: %{o} = -O0
; RUN: %{run}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{run}
; REDEFINE: %{o} = -O2
; RUN: %{run}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{run}
; REDEFINE: %{o} = -O0
; RUN: %{run}

source_filename = "managed-call-sjlj.c"

declare i32 @_setjmp(ptr) #0
declare void @ext(i32)

; CHECK-LABEL: define internal {{.*}}i32 @__msil_mc.m_abs(i32 noundef {{%[^)]*}}) #{{[0-9]+}} {
; CHECK-NEXT:  {{^  entry:$}}
; CHECK-NEXT:  {{^    unreachable$}}
; CHECK-NEXT:  {{^  }$}}
declare i32 @__msil_mc.m_abs(i32 noundef) #2
; CHECK-LABEL: define internal {{.*}}void @__msil_mc.m_keepalive(ptr noundef {{%[^)]*}}) #{{[0-9]+}} {
; CHECK-NEXT:  {{^  entry:$}}
; CHECK-NEXT:  {{^    unreachable$}}
; CHECK-NEXT:  {{^  }$}}
declare void @__msil_mc.m_keepalive(ptr noundef) #3

; CHECK-LABEL: define i32 @caller(ptr %buf, ptr %h) {
; CHECK:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; CHECK:       %[[R:[^ ]+]] = phi i32 [ 0, %{{[^ ]+}} ], [ %{{[^ ]+}}, %{{[^ ]+}} ]
; CHECK:       %a = call i32 @__msil_sjlj_invoke.0(i32 noundef %[[R]], ptr %[[ST]]){{$}}
; CHECK-NEXT:  load i32, ptr %[[ST]]
; CHECK:       call void @__msil_sjlj_invoke.1(ptr noundef %h, ptr %[[ST]]){{$}}
; CHECK-NEXT:  load i32, ptr %[[ST]]
; CHECK:       call void @__msil_sjlj_invoke.2(i32 %a, ptr %[[ST]]){{$}}
; CHECK-NEXT:  load i32, ptr %[[ST]]
; CHECK:       %b = call i32 @__msil_sjlj_invoke.0(i32 noundef %a, ptr %[[ST]]){{$}}
; CHECK-NEXT:  load i32, ptr %[[ST]]
define i32 @caller(ptr %buf, ptr %h) {
entry:
  %r = call i32 @_setjmp(ptr %buf) #1
  %a = call i32 @__msil_mc.m_abs(i32 noundef %r)
  call void @__msil_mc.m_keepalive(ptr noundef %h)
  call void @ext(i32 %a)
  %b = call i32 @__msil_mc.m_abs(i32 noundef %a)
  ret i32 %b
}

; The wrappers, in the order they were made; each calls its callee with its
; own parameters and returns the result.
; CHECK:      define internal i32 @__msil_sjlj_invoke.0(i32 noundef [[A:%[^ ,]+]], ptr {{[^)]*}}) #[[W:[0-9]+]] {
; CHECK:      [[V:%[^ ]+]] = call i32 @__msil_mc.m_abs(i32 noundef [[A]]){{$}}
; CHECK-NEXT: ret i32 [[V]]
; CHECK:      define internal void @__msil_sjlj_invoke.1(ptr noundef [[A:%[^ ,]+]], ptr {{[^)]*}}) #[[W]] {
; CHECK:      call void @__msil_mc.m_keepalive(ptr noundef [[A]]){{$}}
; CHECK-NEXT: ret void
; CHECK:      define internal void @__msil_sjlj_invoke.2(i32 [[A:%[^ ,]+]], ptr {{[^)]*}}) #[[W]] {
; CHECK:      call void @ext(i32 [[A]]){{$}}
; CHECK-NEXT: ret void
; CHECK:      attributes #[[W]] = { noinline "msil-sjlj-invoke"="1" }

; IL-LABEL: .method assembly static int32 '__msil_mc.m_abs'(int32) cil managed
; IL:       call int32 [msil.bcl]System.Math::Abs(int32)
; IL-LABEL: .method assembly static void '__msil_mc.m_keepalive'(native int) cil managed
; IL:       call void [msil.bcl]System.GC::KeepAlive(object)
; IL-LABEL: .method public static int32 'caller'(native int, native int) cil managed
; IL:       {{^}}  call int32 '__msil_sjlj_invoke.0'(int32, native int){{$}}
; IL:       {{^}}  call void '__msil_sjlj_invoke.1'(native int, native int){{$}}
; IL:       {{^}}  call void '__msil_sjlj_invoke.2'(int32, native int){{$}}
; IL:       {{^}}  call int32 '__msil_sjlj_invoke.0'(int32, native int){{$}}
; IL-LABEL: .method assembly static int32 '__msil_sjlj_invoke.0'(int32, native int) cil managed
; IL:       {{^  \.try \{$}}
; IL-NEXT:  {{^@EH[0-9]+_[0-9]+:$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  call int32 '__msil_mc.m_abs'(int32){{$}}
; IL:       {{^}}} // end of method '__msil_sjlj_invoke.0'{{$}}
; IL-LABEL: .method assembly static void '__msil_sjlj_invoke.1'(native int, native int) cil managed
; IL:       {{^  \.try \{$}}
; IL-NEXT:  {{^@EH[0-9]+_[0-9]+:$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  call void '__msil_mc.m_keepalive'(native int){{$}}
; IL:       {{^}}} // end of method '__msil_sjlj_invoke.1'{{$}}
; IL-LABEL: .method assembly static void '__msil_sjlj_invoke.2'(int32, native int) cil managed
; IL:       {{^  \.try \{$}}
; IL-NEXT:  {{^@EH[0-9]+_[0-9]+:$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  call void [msil.extern]'<msil>Symbols'::'ext'(int32){{$}}
; IL:       {{^}}} // end of method '__msil_sjlj_invoke.2'{{$}}

attributes #0 = { nounwind returns_twice }
attributes #1 = { returns_twice }
attributes #2 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #3 = { "msil-managed-conv"="obj;void" "msil-managed-member"="KeepAlive" "msil-managed-sig"="void(object)" "msil-managed-type"="System.GC" }

!msil.managed.calls = !{!0, !1}
!0 = !{!"__msil_mc.m_abs", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!1 = !{!"__msil_mc.m_keepalive", !"System.GC", !"KeepAlive", !"void(object)", !"", !"obj;void", !""}
