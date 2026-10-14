; Undefined values on branchy paths (ImplementationPlan.md M6, plan 5.4 'Stack
; effect contract'; port of llvm/test/CodeGen/WebAssembly/implicit-def.ll,
; whose functions are all branches; the v4i32 one is dropped, Phase 1 has no
; vectors). An undef or poison PHI input becomes an IMPLICIT_DEF. When
; MSILRegStackify stackifies one, it turns it into CONST_<RC> 0 so that the
; value its user pops is really pushed ('ldc.i4.0', 'ldc.i4.0; conv.i',
; 'ldc.r4 float32(0x00000000)', 'ldc.r8 float64(0x0000000000000000)'); one
; that is not stackified only leaves its local unwritten, and
; MSILExplicitLocals deletes it. Either way no IMPLICIT_DEF is left after
; MSILExplicitLocals, MSILStackCheck accepts every method (the stack is empty
; at every block boundary and each 'ret' pops exactly one value), and the
; output assembles. The '*_on_path' functions return undef on one path of a
; branch: at -O0, where MSILRegStackify only stackifies a def right before
; its use, the undef is the pushed constant; at -O2 it may also be the
; unwritten local. In the 'implicit_def_*' functions more blocks can follow
; the first 'ret' at -O2 (the A->C edge is tail-duplicated), so the check that
; the method ends in 'ret' is one pattern spanning the last two lines:
; '{{[[:space:]]}}' matches the newline between 'ret' and the end of method.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=switch \
; DEFINE:   --implicit-check-not="{{^ +b[a-z.]+\.s }}" --implicit-check-not=throw
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs \
; RUN:   -stop-after=msil-explicit-locals < %s \
; RUN:   | FileCheck %s --check-prefix=EXPL --implicit-check-not=IMPLICIT_DEF
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs \
; RUN:   -stop-after=msil-explicit-locals < %s \
; RUN:   | FileCheck %s --check-prefix=EXPL --implicit-check-not=IMPLICIT_DEF
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "implicit-def.c"

; EXPL-LABEL: name: implicit_def_i32{{$}}
; EXPL:       RETURN

; CHECK-LABEL: .method {{.*}}'implicit_def_i32'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.1{{$}}
; CHECK:       {{^}}  ldc.i4.0{{$}}
; CHECK:       {{^}}  ret{{$}}{{[[:space:]]}}{{^}}} // end of method 'implicit_def_i32'{{$}}
define i32 @implicit_def_i32() {
  br i1 undef, label %A, label %X

A:                                                ; preds = %0
  %d = icmp slt i1 0, 0
  br i1 %d, label %C, label %B

B:                                                ; preds = %A
  br label %C

C:                                                ; preds = %B, %A
  %h = phi i32 [ undef, %A ], [ 0, %B ]
  br label %X

X:                                                ; preds = %0, C
  %i = phi i32 [ 1, %0 ], [ %h, %C ]
  ret i32 %i
}

; EXPL-LABEL: name: implicit_def_i64{{$}}
; EXPL:       RETURN

; CHECK-LABEL: .method {{.*}}'implicit_def_i64'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4{{(\.1| 1)}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK:       {{^}}  conv.i{{$}}
; CHECK:       {{^}}  ret{{$}}{{[[:space:]]}}{{^}}} // end of method 'implicit_def_i64'{{$}}
define i64 @implicit_def_i64() {
  br i1 undef, label %A, label %X

A:                                                ; preds = %0
  %d = icmp slt i1 0, 0
  br i1 %d, label %C, label %B

B:                                                ; preds = %A
  br label %C

C:                                                ; preds = %B, %A
  %h = phi i64 [ undef, %A ], [ 0, %B ]
  br label %X

X:                                                ; preds = %0, C
  %i = phi i64 [ 1, %0 ], [ %h, %C ]
  ret i64 %i
}

; EXPL-LABEL: name: implicit_def_f32{{$}}
; EXPL:       RETURN

; CHECK-LABEL: .method {{.*}}'implicit_def_f32'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK:       {{^}}  ldc.r4 float32(0x3F800000){{$}}
; CHECK:       {{^}}  ldc.r4 float32(0x00000000){{$}}
; CHECK:       {{^}}  ret{{$}}{{[[:space:]]}}{{^}}} // end of method 'implicit_def_f32'{{$}}
define float @implicit_def_f32() {
  br i1 undef, label %A, label %X

A:                                                ; preds = %0
  %d = icmp slt i1 0, 0
  br i1 %d, label %C, label %B

B:                                                ; preds = %A
  br label %C

C:                                                ; preds = %B, %A
  %h = phi float [ undef, %A ], [ 0.0, %B ]
  br label %X

X:                                                ; preds = %0, C
  %i = phi float [ 1.0, %0 ], [ %h, %C ]
  ret float %i
}

; EXPL-LABEL: name: implicit_def_f64{{$}}
; EXPL:       RETURN

; CHECK-LABEL: .method {{.*}}'implicit_def_f64'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK:       {{^}}  ldc.r8 float64(0x3FF0000000000000){{$}}
; CHECK:       {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}{{[[:space:]]}}{{^}}} // end of method 'implicit_def_f64'{{$}}
define double @implicit_def_f64() {
  br i1 undef, label %A, label %X

A:                                                ; preds = %0
  %d = icmp slt i1 0, 0
  br i1 %d, label %C, label %B

B:                                                ; preds = %A
  br label %C

C:                                                ; preds = %B, %A
  %h = phi double [ undef, %A ], [ 0.0, %B ]
  br label %X

X:                                                ; preds = %0, C
  %i = phi double [ 1.0, %0 ], [ %h, %C ]
  ret double %i
}

; Returning undef on one path of a branch.
; EXPL-LABEL: name: undef_i32_on_path{{$}}
; EXPL:       RETURN

; CHECK-LABEL: .method {{.*}}'undef_i32_on_path'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[A:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[A]]:{{$}}
; O0-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  {{ldc|ldloc|ldarg}}{{.*}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'undef_i32_on_path'{{$}}
define i32 @undef_i32_on_path(i1 zeroext %c) {
entry:
  br i1 %c, label %a, label %b
a:
  ret i32 undef
b:
  ret i32 7
}

; EXPL-LABEL: name: poison_i64_on_path{{$}}
; EXPL:       RETURN

; CHECK-LABEL: .method {{.*}}'poison_i64_on_path'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[A:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[A]]:{{$}}
; O0-NEXT:     {{^}}  ldc.i4{{(\.0| 0)}}{{$}}
; O0-NEXT:     {{^}}  conv.i{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4{{(\.7| 7)}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'poison_i64_on_path'{{$}}
define i64 @poison_i64_on_path(i1 zeroext %c) {
entry:
  br i1 %c, label %a, label %b
a:
  ret i64 poison
b:
  ret i64 7
}

; EXPL-LABEL: name: undef_f32_on_path{{$}}
; EXPL:       RETURN

; CHECK-LABEL: .method {{.*}}'undef_f32_on_path'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[A:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[A]]:{{$}}
; O0-NEXT:     {{^}}  ldc.r4 float32(0x00000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x40E00000){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'undef_f32_on_path'{{$}}
define float @undef_f32_on_path(i1 zeroext %c) {
entry:
  br i1 %c, label %a, label %b
a:
  ret float undef
b:
  ret float 7.0
}

; EXPL-LABEL: name: undef_f64_on_path{{$}}
; EXPL:       RETURN

; CHECK-LABEL: .method {{.*}}'undef_f64_on_path'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[A:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[A]]:{{$}}
; O0-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x401C000000000000){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'undef_f64_on_path'{{$}}
define double @undef_f64_on_path(i1 zeroext %c) {
entry:
  br i1 %c, label %a, label %b
a:
  ret double undef
b:
  ret double 7.0
}
