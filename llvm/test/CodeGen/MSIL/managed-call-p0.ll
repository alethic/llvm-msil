; The P0 transformation of managed-call declarations (ImplementationPlan.md
; 5.20.5 ML2 (1), milestone MC-1): MSIL::createManagedCallStubs, called from
; MSILPrepareIRLegacy::doInitialization, turns every declaration with
; "msil-managed-type" in place into an internal 'noinline' definition whose IR
; body is a lone 'unreachable' and which carries "msil-managed-stub" besides
; its msil-managed-* attributes (kept for the AsmPrinter). Being in place, it
; redirects no call: the direct calls of @use still call
; '__msil_mc.<symbol>' with their own arguments, and a declaration without
; calls becomes a stub too. A declaration without "msil-managed-type" (here
; an intrinsic) stays a declaration, and a defined function is untouched. Checked after msil-prepare-ir (P0 runs in
; its doInitialization) at -O0 and -O2 on both host x86_64 triples and
; msil64-unknown-dotnet; the module then compiles to whole-program text with
; the stubs defined as 'assembly' methods (-S of llc), which assembles.
; The stub's IR parameter names are not pinned (a declaration has none); its
; body is the block 'entry' holding only 'unreachable'.
; -stop-after prints the IR inside the MIR file, indented by two spaces.

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{run} = llc -mtriple=%{t} %{o} -stop-after=msil-prepare-ir < %s -o %t.%{t}%{o}.ll && \
; DEFINE:   FileCheck %s --input-file=%t.%{t}%{o}.ll && \
; DEFINE:   llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %s -o %t.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefix=IL --input-file=%t.%{t}%{o}.il

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %{run}
; REDEFINE: %{o} = -O2
; RUN: %{run}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{run}
; REDEFINE: %{o} = -O0
; RUN: %{run}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{run}
; REDEFINE: %{o} = -O2
; RUN: %{run}

; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.win.il && %ilasm-verify -output=%t.win.dll %t.win.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.lnx.il && %ilasm-verify -output=%t.lnx.dll %t.lnx.il %}

source_filename = "managed-call-p0.c"

; CHECK-LABEL: define internal {{.*}}i32 @__msil_mc.m_abs(
; CHECK-SAME:  i32 noundef {{%[^)]*}}) [[ABS:#[0-9]+]] {
; CHECK-NEXT:  {{^  entry:$}}
; CHECK-NEXT:  {{^    unreachable$}}
; CHECK-NEXT:  {{^  }$}}
declare i32 @__msil_mc.m_abs(i32 noundef) #0

; CHECK-LABEL: define internal {{.*}}void @__msil_mc.m_unused(
; CHECK-SAME:  ptr noundef {{%[^)]*}}) [[UNUSED:#[0-9]+]] {
; CHECK-NEXT:  {{^  entry:$}}
; CHECK-NEXT:  {{^    unreachable$}}
; CHECK-NEXT:  {{^  }$}}
declare void @__msil_mc.m_unused(ptr noundef) #1

; CHECK-LABEL: define internal {{.*}}ptr @__msil_mc.m_open(
; CHECK-SAME:  ptr noundef {{%[^,]*}}, i32 noundef {{%[^,]*}}, ptr noundef {{%[^)]*}}) [[OPEN:#[0-9]+]] {
; CHECK-NEXT:  {{^  entry:$}}
; CHECK-NEXT:  {{^    unreachable$}}
; CHECK-NEXT:  {{^  }$}}
declare ptr @__msil_mc.m_open(ptr noundef, i32 noundef, ptr noundef) #2

; CHECK: declare i32 @llvm.smax.i32(i32, i32)
declare i32 @llvm.smax.i32(i32, i32)

; CHECK-LABEL: define internal i32 @helper(i32 %x) {
; CHECK-NEXT:    %r = add i32 %x, 1
; CHECK-NEXT:    ret i32 %r
define internal i32 @helper(i32 %x) {
  %r = add i32 %x, 1
  ret i32 %r
}

; CHECK-LABEL: define i32 @use(i32 %x, ptr %p, ptr %err)
; CHECK:         %a = call i32 @__msil_mc.m_abs(i32 noundef %x)
; CHECK:         %b = call i32 @__msil_mc.m_abs(i32 noundef %a)
; CHECK:         %h = call ptr @__msil_mc.m_open(ptr noundef %p, i32 noundef %b, ptr noundef %err)
; CHECK:         %i = call i32 @llvm.smax.i32(i32 %b, i32 3)
; CHECK:         %j = call i32 @helper(i32 %i)
define i32 @use(i32 %x, ptr %p, ptr %err) {
  %a = call i32 @__msil_mc.m_abs(i32 noundef %x)
  %b = call i32 @__msil_mc.m_abs(i32 noundef %a)
  %h = call ptr @__msil_mc.m_open(ptr noundef %p, i32 noundef %b, ptr noundef %err)
  %i = call i32 @llvm.smax.i32(i32 %b, i32 3)
  %j = call i32 @helper(i32 %i)
  %k = ptrtoint ptr %h to i32
  %r = add i32 %j, %k
  ret i32 %r
}

; CHECK-DAG: attributes [[ABS]] = { {{([a-z_]+ )*}}noinline {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-stub" "msil-managed-type"="System.Math"{{( .*)?}} }
; CHECK-DAG: attributes [[UNUSED]] = { {{([a-z_]+ )*}}noinline {{([a-z_]+ )*}}"msil-managed-conv"="obj;void" "msil-managed-member"="KeepAlive" "msil-managed-sig"="void(object)" "msil-managed-stub" "msil-managed-type"="System.GC"{{( .*)?}} }
; CHECK-DAG: attributes [[OPEN]] = { {{([a-z_]+ )*}}noinline {{([a-z_]+ )*}}"msil-managed-asm"="mscorlib" "msil-managed-catch"="System.IO.FileNotFoundException=2;System.IO.IOException=5" "msil-managed-conv"="utf8,enum,err;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(string, valuetype System.IO.FileMode)" "msil-managed-stub" "msil-managed-type"="System.IO.FileStream"{{( .*)?}} }

; IL-LABEL: .method assembly static int32 '__msil_mc.m_abs'(int32) cil managed
; IL:       call int32 [mscorlib]System.Math::Abs(int32)
; IL-LABEL: .method assembly static void '__msil_mc.m_unused'(native int) cil managed
; IL:       call void [mscorlib]System.GC::KeepAlive(object)
; IL-LABEL: .method assembly static native int '__msil_mc.m_open'(native int, int32, native int) cil managed
; IL:       newobj instance void [mscorlib]System.IO.FileStream::.ctor(string, valuetype [mscorlib]System.IO.FileMode)
; IL-LABEL: .method public static int32 'use'(int32, native int, native int) cil managed
; IL:       call int32 '__msil_mc.m_abs'(int32)
; IL:       call int32 '__msil_mc.m_abs'(int32)
; IL:       call native int '__msil_mc.m_open'(native int, int32, native int)

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #1 = { "msil-managed-conv"="obj;void" "msil-managed-member"="KeepAlive" "msil-managed-sig"="void(object)" "msil-managed-type"="System.GC" }
attributes #2 = { "msil-managed-asm"="mscorlib" "msil-managed-catch"="System.IO.FileNotFoundException=2;System.IO.IOException=5" "msil-managed-conv"="utf8,enum,err;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(string, valuetype System.IO.FileMode)" "msil-managed-type"="System.IO.FileStream" }

!msil.managed.calls = !{!0, !1, !2}
!0 = !{!"__msil_mc.m_abs", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!1 = !{!"__msil_mc.m_unused", !"System.GC", !"KeepAlive", !"void(object)", !"", !"obj;void", !""}
!2 = !{!"__msil_mc.m_open", !"System.IO.FileStream", !".ctor", !"instance void(string, valuetype System.IO.FileMode)", !"mscorlib", !"utf8,enum,err;obj", !"System.IO.FileNotFoundException=2;System.IO.IOException=5"}
