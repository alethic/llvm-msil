; Arguments whose address is taken (ImplementationPlan.md M7.b and decision
; D13; recon passes-frame.md 6.1, 6.7 and 7): a C parameter whose address
; escapes lives in a stack slot of the static frame, into which the function
; stores the incoming argument; clang -O0 does this for every parameter. The
; prologue comes right after the ARGUMENT block, so 'ldc.i4 <size>; localloc;
; stloc FP' is the first code of the method and every 'ldarg' follows it; the
; incoming arguments themselves are never copied to locals: no 'ldarg' is
; followed by a 'stloc' (or a 'dup; stloc'), checked over the whole output.
; Each argument is stored to its own slot with the store of its CIL type
; (stind.i4, stind.i, stind.r4, stind.r8), the slot addresses passed on are the
; same FP-relative addresses, and a value read back after a call is reloaded
; from the slot (the callee may have written it). Other locals are allowed: at
; -O0 RegStackify only stackifies a def that sits right before its use (recon
; passes-frame.md 5.1), so a value defined earlier waits in a local of its own
; type, and at any level RegColoring may give a temporary the slot of a dead
; argument of the same CIL type, which is written with 'starg' (passes-frame.md
; 7, ARG_SET/ARG_TEE in plan 5.4).
;
; 'mixed_args' has a 32-byte frame of 8- and 4-byte slots, laid out from the
; top in IR order (d FP+24, b FP+16, a FP+12, c FP+8, e FP+0), so its layout
; does not depend on the target's stack alignment and is checked exactly; the
; order of its five stores is not fixed, so each store is matched as a whole
; with CHECK-DAG. The single-slot and clang -O0 shaped functions have frames
; whose size depends on the stack alignment, so only their opcodes are checked.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=localloc --implicit-check-not=".locals init" \
; DEFINE:   --implicit-check-not="{{ldarg[.a-z0-9 ]*[[:space:]]+(dup[[:space:]]+)?stloc}}" \
; DEFINE:   --implicit-check-not=int64 --implicit-check-not=ldc.i8
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "frame-address-taken-args.c"

define void @sink(ptr %p) {
  ret void
}

define void @sink5(ptr %p, ptr %q, ptr %r, ptr %s, ptr %t) {
  ret void
}

; CHECK-LABEL: .method {{.*}}'mixed_args'(
; CHECK-SAME:  int32, native int, float32, float64, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 5{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-DAG:   {{^  ldloc\.0[[:space:]]+(ldc\.i4\.s 24|ldc\.i4 24)[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+ldarg\.3[[:space:]]+stind\.r8$}}
; CHECK-DAG:   {{^  ldloc\.0[[:space:]]+(ldc\.i4\.s 16|ldc\.i4 16)[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+ldarg\.1[[:space:]]+stind\.i$}}
; CHECK-DAG:   {{^  ldloc\.0[[:space:]]+(ldc\.i4\.s 12|ldc\.i4 12)[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+ldarg\.0[[:space:]]+stind\.i4$}}
; CHECK-DAG:   {{^  ldloc\.0[[:space:]]+(ldc\.i4\.8|ldc\.i4 8)[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+ldarg\.2[[:space:]]+stind\.r4$}}
; CHECK-DAG:   {{^  ldloc\.0[[:space:]]+ldarg\.s 4[[:space:]]+stind\.i$}}
; CHECK:       {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 24|ldc\.i4 24}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 16|ldc\.i4 16}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 12|ldc\.i4 12}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.8|ldc\.i4 8}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink5'(native int, native int, native int, native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 12|ldc\.i4 12}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'mixed_args'{{$}}
define i32 @mixed_args(i32 %a, i64 %b, float %c, double %d, ptr %e) {
  %d.addr = alloca double, align 8
  %b.addr = alloca i64, align 8
  %a.addr = alloca i32, align 4
  %c.addr = alloca float, align 4
  %e.addr = alloca ptr, align 8
  store double %d, ptr %d.addr, align 8
  store i64 %b, ptr %b.addr, align 8
  store i32 %a, ptr %a.addr, align 4
  store float %c, ptr %c.addr, align 4
  store ptr %e, ptr %e.addr, align 8
  call void @sink5(ptr %d.addr, ptr %b.addr, ptr %a.addr, ptr %c.addr, ptr %e.addr)
  %r = load i32, ptr %a.addr, align 4
  ret i32 %r
}

; C: int one_arg(int a) { sink(&a); return a; }
; CHECK-LABEL: .method {{.*}}'one_arg'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.4|ldc\.i4\.8|ldc\.i4\.s 16}}{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK:       {{^}}  call void 'sink'(native int){{$}}
; CHECK:       {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'one_arg'{{$}}
define i32 @one_arg(i32 %a) {
  %a.addr = alloca i32, align 4
  store i32 %a, ptr %a.addr, align 4
  call void @sink(ptr %a.addr)
  %r = load i32, ptr %a.addr, align 4
  ret i32 %r
}

; clang -O0 shape: every parameter spilled to a slot and reloaded (optnone, so
; nothing is forwarded at any -O level).
; CHECK-LABEL: .method {{.*}}'add_o0'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int{{.*}}){{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.8|ldc\.i4\.s 16}}{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK:       {{^}}  ldind.i4{{$}}
; CHECK:       {{^}}  ldind.i4{{$}}
; CHECK:       {{^}}  add{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'add_o0'{{$}}
define i32 @add_o0(i32 noundef %a, i32 noundef %b) #0 {
entry:
  %a.addr = alloca i32, align 4
  %b.addr = alloca i32, align 4
  store i32 %a, ptr %a.addr, align 4
  store i32 %b, ptr %b.addr, align 4
  %0 = load i32, ptr %a.addr, align 4
  %1 = load i32, ptr %b.addr, align 4
  %add = add nsw i32 %0, %1
  ret i32 %add
}

; clang -O0 shape of a pointer and a double parameter.
; CHECK-LABEL: .method {{.*}}'scale_o0'(
; CHECK-SAME:  native int, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int{{.*}}){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.r8{{$}}
; CHECK:       {{^}}  ldind.r8{{$}}
; CHECK:       {{^}}  mul{{$}}
; CHECK:       {{^}}  stind.r8{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'scale_o0'{{$}}
define void @scale_o0(ptr noundef %p, double noundef %k) #0 {
entry:
  %p.addr = alloca ptr, align 8
  %k.addr = alloca double, align 8
  store ptr %p, ptr %p.addr, align 8
  store double %k, ptr %k.addr, align 8
  %0 = load ptr, ptr %p.addr, align 8
  %1 = load double, ptr %0, align 8
  %2 = load double, ptr %k.addr, align 8
  %mul = fmul double %1, %2
  %3 = load ptr, ptr %p.addr, align 8
  store double %mul, ptr %3, align 8
  ret void
}

; The windows-msvc -O0 'main': a return-value slot that is written and never
; read (decision D32). At -O2 the 0 is stackified right before the store; at
; -O0 it is defined before the FP-relative slot address, not right before its
; use, so it waits in an int32 local (passes-frame.md 5.1).
; CHECK-LABEL: .method {{.*}}'main'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  .locals (native int){{$}}
; O0-NEXT:     {{^}}  .locals (native int, int32){{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.4|ldc\.i4\.8|ldc\.i4\.s 16}}{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; O2:          {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  stind.i4{{$}}
; O0-NEXT:     {{^}}  ldc.i4.0{{$}}
; O0-NEXT:     {{^}}  stloc.1{{$}}
; O0:          {{^}}  ldloc.1{{$}}
; O0-NEXT:     {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 42{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'main'{{$}}
define dso_local i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  ret i32 42
}

attributes #0 = { noinline nounwind optnone }
