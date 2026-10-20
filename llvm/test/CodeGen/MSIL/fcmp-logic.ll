; Logical and/or of two floating-point compares with a common operand. Upstream
; f3f09622a2c2 (#223655) taught DAGCombiner's foldAndOrOfSETCC to rewrite
; (a olt c) or (b olt c) as (minimumnum(a, b) olt c), and likewise an or of
; two ordered compares and an and of two unordered ones, whenever FMINIMUMNUM
; and FMAXIMUMNUM are Legal or Custom and
; TargetLowering::isProfitableToCombineMinNumMaxNum allows it. MSIL makes them
; Custom only so that llvm.minimumnum and llvm.maximumnum compile (fp-arith.ll,
; MINMAX): LowerFMinMax builds NaN compares, a 1.0/x test of the sign of a
; zero and a select triangle (brtrue, starg), which cost more than the
; compares. MSILTargetLowering::isProfitableToCombineMinNumMaxNum returns false,
; as WebAssembly's does, so each fcmp stays the single CIL compare of
; fp-compare.ll (followed by 'ldc.i4.0; ceq' for the negated predicates ole,
; oge and uge) and the two int32 results are joined by an or or an and. No
; div, floating-point constant, branch or starg may appear, at -O0 or -O2.
;
; Compares of plain predicates compile to the same CIL at -O0 and -O2 (CHECK).
; The negated predicates are checked exactly at -O2 (O2) and only for their
; opcodes at -O0 (O0), where the 0 of both 'ceq's is one value kept in a local.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=div --implicit-check-not=brtrue \
; DEFINE:   --implicit-check-not=starg --implicit-check-not=ldc.r --implicit-check-not=call \
; DEFINE:   --implicit-check-not=conv --implicit-check-not="{{^ b}}"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o2.il
; RUN: %{check} --check-prefixes=CHECK,O2 --input-file=%t.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o2.il
; RUN: %{check} --check-prefixes=CHECK,O2 --input-file=%t.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o0.il
; RUN: %{check} --check-prefixes=CHECK,O0 --input-file=%t.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o0.il
; RUN: %{check} --check-prefixes=CHECK,O0 --input-file=%t.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o2.dll %t.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

source_filename = "fcmp-logic.c"

; (a olt c) or (b olt c): not minimumnum(a, b) olt c.
; CHECK-LABEL: .method {{.*}}'or_olt'(
; CHECK-SAME:  float32, float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  or{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'or_olt'{{$}}
define i32 @or_olt(float %a, float %b, float %c) {
  %x = fcmp olt float %a, %c
  %y = fcmp olt float %b, %c
  %o = or i1 %x, %y
  %r = zext i1 %o to i32
  ret i32 %r
}

; (a ogt c) or (b ogt c): not maximumnum(a, b) ogt c.
; CHECK-LABEL: .method {{.*}}'or_ogt'(
; CHECK-SAME:  float64, float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  cgt{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  cgt{{$}}
; CHECK-NEXT:  {{^}}  or{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'or_ogt'{{$}}
define i32 @or_ogt(double %a, double %b, double %c) {
  %x = fcmp ogt double %a, %c
  %y = fcmp ogt double %b, %c
  %o = or i1 %x, %y
  %r = zext i1 %o to i32
  ret i32 %r
}

; (a ole c) or (b ole c), ole = !ugt (cgt.un): not minimumnum(a, b) ole c.
; CHECK-LABEL: .method {{.*}}'or_ole'(
; CHECK-SAME:  float64, float64, float64) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  cgt.un{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  cgt.un{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  or{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  cgt.un{{$}}
; O0:          {{^}}  ceq{{$}}
; O0:          {{^}}  cgt.un{{$}}
; O0-NEXT:     {{^}}  ceq{{$}}
; O0-NEXT:     {{^}}  or{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'or_ole'{{$}}
define i32 @or_ole(double %a, double %b, double %c) {
  %x = fcmp ole double %a, %c
  %y = fcmp ole double %b, %c
  %o = or i1 %x, %y
  %r = zext i1 %o to i32
  ret i32 %r
}

; (a oge c) or (b oge c), oge = !ult (clt.un): not maximumnum(a, b) oge c.
; CHECK-LABEL: .method {{.*}}'or_oge'(
; CHECK-SAME:  float32, float32, float32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  clt.un{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  clt.un{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  or{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  clt.un{{$}}
; O0:          {{^}}  ceq{{$}}
; O0:          {{^}}  clt.un{{$}}
; O0-NEXT:     {{^}}  ceq{{$}}
; O0-NEXT:     {{^}}  or{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'or_oge'{{$}}
define i32 @or_oge(float %a, float %b, float %c) {
  %x = fcmp oge float %a, %c
  %y = fcmp oge float %b, %c
  %o = or i1 %x, %y
  %r = zext i1 %o to i32
  ret i32 %r
}

; (a uge c) and (b uge c), uge = !olt (clt): not minimumnum(a, b) uge c.
; CHECK-LABEL: .method {{.*}}'and_uge'(
; CHECK-SAME:  float32, float32, float32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  clt{{$}}
; O0:          {{^}}  ceq{{$}}
; O0:          {{^}}  clt{{$}}
; O0-NEXT:     {{^}}  ceq{{$}}
; O0-NEXT:     {{^}}  and{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'and_uge'{{$}}
define i32 @and_uge(float %a, float %b, float %c) {
  %x = fcmp uge float %a, %c
  %y = fcmp uge float %b, %c
  %o = and i1 %x, %y
  %r = zext i1 %o to i32
  ret i32 %r
}

; (a ult c) and (b ult c): not maximumnum(a, b) ult c.
; CHECK-LABEL: .method {{.*}}'and_ult'(
; CHECK-SAME:  float64, float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'and_ult'{{$}}
define i32 @and_ult(double %a, double %b, double %c) {
  %x = fcmp ult double %a, %c
  %y = fcmp ult double %b, %c
  %o = and i1 %x, %y
  %r = zext i1 %o to i32
  ret i32 %r
}
