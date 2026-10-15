; sret parameters and arguments (ImplementationPlan.md M9.a 'sret stays a
; plain pointer argument'; recon isel.md 2.12 'sret: nothing special. The IR
; ptr sret param is just a pointer'). A parameter with sret is an ordinary
; 'native int' parameter wherever it is (first, or second after 'this'), and
; the function returns void; a call passes the pointer it is given, which is
; usually the address of a temporary in the caller's static frame (D13) and
; is never copied (no cpblk). The same holds for indirect calls (calli with
; the void signature) and for the address of an sret function (its address
; field, which <Module>::.cctor sets with 'ldftn' of the plain signature;
; section 12 'Phase-1 function addresses', 2026-10-15, M9).
; Results that need two values without an sret in the IR are demoted by the
; backend instead (call-indirect.ll, struct-args-sysv.ll). No diagnostic. The
; CIL is the same at -O0 and -O2.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning: --implicit-check-not=cpblk \
; DEFINE:   --implicit-check-not="{{^ +ldftn }}"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "sret.c"

%struct.S = type { i64, i64, i64 }

; The sret parameter is the first native int; the callee stores the result
; through it and returns void.
; CHECK-LABEL: .method {{.*}}'make'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 1{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 16{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 7{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @make(ptr sret(%struct.S) align 8 %r, i64 %a) {
  store i64 %a, ptr %r, align 8
  %p1 = getelementptr inbounds i8, ptr %r, i64 8
  %b = add i64 %a, 1
  store i64 %b, ptr %p1, align 8
  %p2 = getelementptr inbounds i8, ptr %r, i64 16
  store i64 7, ptr %p2, align 8
  ret void
}

; sret in second position (MSVC C++ methods put 'this' first): still a plain
; pointer.
; CHECK-LABEL: .method {{.*}}'method'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @method(ptr %this, ptr sret(%struct.S) align 8 %r) {
  %v = load i64, ptr %this, align 8
  store i64 %v, ptr %r, align 8
  ret void
}

; The caller passes the address of its own temporary (FP + 8 of a 32-byte
; frame) and reads the result back.
; CHECK-LABEL: .method {{.*}}'use_make'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'make'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 16{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @use_make(i64 %a) {
  %t = alloca %struct.S, align 8
  call void @make(ptr sret(%struct.S) align 8 %t, i64 %a)
  %p1 = getelementptr inbounds i8, ptr %t, i64 8
  %v = load i64, ptr %p1, align 8
  ret i64 %v
}

; The caller's own sret pointer is passed on unchanged: no frame, no copy.
; CHECK-LABEL: .method {{.*}}'fwd'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call void 'make'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @fwd(ptr sret(%struct.S) align 8 %r, i64 %a) {
  call void @make(ptr sret(%struct.S) align 8 %r, i64 %a)
  ret void
}

; Any pointer can be the sret argument.
; CHECK-LABEL: .method {{.*}}'into'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call void 'make'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @into(ptr %out, i64 %a) {
  call void @make(ptr sret(%struct.S) align 8 %out, i64 %a)
  ret void
}

; The temporary's address is the second argument.
; CHECK-LABEL: .method {{.*}}'use_method'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  call void 'method'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @use_method(ptr %obj) {
  %t = alloca %struct.S, align 8
  call void @method(ptr %obj, ptr sret(%struct.S) align 8 %t)
  %v = load i64, ptr %t, align 8
  ret i64 %v
}

; The address of an sret function is its address field; the .cctor's ldftn
; of it has its plain signature (end of the module).
; CHECK-LABEL: .method {{.*}}'addr_make'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_make'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @addr_make() {
  ret ptr @make
}

; An indirect call with an sret argument: the temporary's address, the other
; argument, then the callee and a calli with the sret function's signature.
; CHECK-LABEL: .method {{.*}}'use_make_indirect'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli void(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 24{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @use_make_indirect(ptr %f, i64 %a) {
  %t = alloca %struct.S, align 8
  call void %f(ptr sret(%struct.S) align 8 %t, i64 %a)
  %p2 = getelementptr inbounds i8, ptr %t, i64 16
  %v = load i64, ptr %p2, align 8
  ret i64 %v
}

; The only address taken is make's: one address field, after the methods (no
; P/Invoke and no data), and a .cctor that only sets it, with the ldftn of
; make's plain signature.
; CHECK:       {{^}}.field private static initonly native int '<msil>fn_make'{{$}}
; CHECK-EMPTY:
; CHECK-NEXT:  {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldftn void 'make'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_make'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '.cctor'{{$}}
