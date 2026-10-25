; Floating-point arithmetic (ImplementationPlan.md M5.a, plan 5.4 'FP'; recon
; isel.md 3.2 and 5.6): fadd, fsub, fmul and fdiv on float32 (class F32) and on
; float64 (class F64) are one CIL opcode each (add, sub, mul, div), which pops
; its operands in IR operand order, and fneg is 'neg' (isel.md 3.2: neg 0.0 is
; -0.0, so neg is exactly fneg). float32 arithmetic stays float32: no conv.r8
; widens an operand and no conv.r4 follows an operation (64-bit runtimes keep
; single precision, isel.md 3.2), so every 'conv' is an implicit negative
; check. 0.0 - x is a subtraction and never 'neg' (the two differ for x = +0.0:
; 0.0 - 0.0 is +0.0, neg 0.0 is -0.0), while -0.0 - x is 'neg' (LLVM folds it
; to fneg). llvm.fmuladd is a separate multiply and add, never a call to fma
; (isFMAFasterThanFMulAndFAdd keeps its default, false). frem is in frem.ll,
; compares in fp-compare.ll, conversions in fp-conv.ll, constants in
; fp-const.ll and the operations that become library calls in fp-expand.ll.
;
; Functions whose body is a single operation on arguments and at most one
; constant compile to the same CIL at -O0 and -O2 (CHECK); the rest is checked
; exactly at -O2 (O2) and only for its key opcodes at -O0 (O0), where
; MSILRegStackify moves nothing. The MIR lines check the plan 5.4 record names
; and their operand order right after instruction selection.
;
; minmax.ll has llvm.minimum, llvm.maximum, llvm.minimumnum and
; llvm.maximumnum on float32 and float64, and the constrained minimum and
; maximum (plan section 12, row 'M5.a amended (minimum and maximum)'). LLVM
; has no library call for them, and their generic expansions test the sign of
; a zero on the integer bits (IS_FPCLASS through a BITCAST and a stack slot,
; the M7 frame-index stub), so M5.a lowers them to compares and select
; triangles (D34) that test the sign of a zero arithmetically (1.0 / x
; compared with 0.0); the constrained forms become the plain nodes in
; LowerStrictFP. The row allows two mechanisms, so the checks fix the shape,
; not the sequence: a floating-point compare, a 'brtrue' to a label of the
; same method, a 'div' (the sign of a zero), and no call, localloc, ldind,
; stind, diagnostic, 'LLVM ERROR' or crash report, at -O0 and -O2. Fast-math
; fmin and fmax (minnum and maxnum with nnan) take the same route, which is
; why a diagnostic is not acceptable there: expandFMINNUM_FMAXNUM makes them
; FMINIMUM and FMAXIMUM (checked for the compare and the triangle; with nsz
; there is no sign test). With one constant operand that is neither a NaN nor
; a zero there is no sign test either, and the compare alone decides a NaN x:
; the checks fix it (MINMAX at -O0 and -O2) and the whole sequence at -O2
; (MMO2).
; M5.d's fp-minmax.ll checks the values (NaN operands, -0.0 against +0.0).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=conv \
; DEFINE:   --implicit-check-not=call --implicit-check-not=localloc \
; DEFINE:   --implicit-check-not="{{^ b}}"
; The forbidden opcodes, diagnostics and crash markers of minmax.ll are
; checked over the whole output (stdout and stderr) by a second FileCheck run
; (MMBAN) whose only directive is the '.assembly' line, because implicit
; negative checks do not look between or after CHECK-DAG matches.
; DEFINE: %{minmax} = FileCheck %s --check-prefix=MINMAX
; DEFINE: %{mmo2} = FileCheck %s --check-prefix=MMO2
; DEFINE: %{mmban} = FileCheck %s --check-prefix=MMBAN --implicit-check-not=call \
; DEFINE:   --implicit-check-not=localloc --implicit-check-not=ldind --implicit-check-not=stind \
; DEFINE:   --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; MMBAN: {{^}}.assembly '
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/arith.ll \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/arith.ll \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/arith.ll \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/arith.ll \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-after=finalize-isel < %t/arith.ll \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %t/arith.ll \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %t/arith.ll -o %t/arith.o2.il \
; RUN:   && %ilasm-verify -output=%t/arith.o2.dll %t/arith.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/arith.ll -o %t/arith.o0.il \
; RUN:   && %ilasm-verify -output=%t/arith.o0.dll %t/arith.o0.il %}

; minimum and maximum: both triples, -O2 and -O0, stderr included.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/minmax.ll \
; RUN:   > %t/minmax.lnx.o2.out 2>&1
; RUN: %{minmax} --input-file=%t/minmax.lnx.o2.out
; RUN: %{mmo2} --input-file=%t/minmax.lnx.o2.out
; RUN: %{mmban} --input-file=%t/minmax.lnx.o2.out
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/minmax.ll \
; RUN:   > %t/minmax.lnx.o0.out 2>&1
; RUN: %{minmax} --input-file=%t/minmax.lnx.o0.out
; RUN: %{mmban} --input-file=%t/minmax.lnx.o0.out
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/minmax.ll \
; RUN:   > %t/minmax.win.o2.out 2>&1
; RUN: %{minmax} --input-file=%t/minmax.win.o2.out
; RUN: %{mmo2} --input-file=%t/minmax.win.o2.out
; RUN: %{mmban} --input-file=%t/minmax.win.o2.out
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/minmax.ll \
; RUN:   > %t/minmax.win.o0.out 2>&1
; RUN: %{minmax} --input-file=%t/minmax.win.o0.out
; RUN: %{mmban} --input-file=%t/minmax.win.o0.out
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t/minmax.ll -o %t/minmax.o2.il \
; RUN:   && %ilasm-verify -output=%t/minmax.o2.dll %t/minmax.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/minmax.ll -o %t/minmax.o0.il \
; RUN:   && %ilasm-verify -output=%t/minmax.o0.dll %t/minmax.o0.il %}

;--- arith.ll
source_filename = "fp-arith.c"

; ---- float32 ---------------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'fadd_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fadd_f32'{{$}}
; MIR-LABEL:   name: fadd_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR:         {{%[0-9]+}}:f32 = {{([a-z]+ )*}}ADD_F32 [[A]], [[B]],
define float @fadd_f32(float %a, float %b) {
  %r = fadd float %a, %b
  ret float %r
}

; The operand order matters for sub and div.
; CHECK-LABEL: .method {{.*}}'fsub_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fsub_f32'{{$}}
; MIR-LABEL:   name: fsub_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR:         {{%[0-9]+}}:f32 = {{([a-z]+ )*}}SUB_F32 [[A]], [[B]],
define float @fsub_f32(float %a, float %b) {
  %r = fsub float %a, %b
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'fmul_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fmul_f32'{{$}}
; MIR-LABEL:   name: fmul_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR:         {{%[0-9]+}}:f32 = {{([a-z]+ )*}}MUL_F32 [[A]], [[B]],
define float @fmul_f32(float %a, float %b) {
  %r = fmul float %a, %b
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'fdiv_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fdiv_f32'{{$}}
; MIR-LABEL:   name: fdiv_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR:         {{%[0-9]+}}:f32 = {{([a-z]+ )*}}DIV_F32 [[A]], [[B]],
define float @fdiv_f32(float %a, float %b) {
  %r = fdiv float %a, %b
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'fneg_f32'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fneg_f32'{{$}}
; MIR-LABEL:   name: fneg_f32{{$}}
; MIR:         [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:f32 = {{([a-z]+ )*}}NEG_F32 [[A]],
define float @fneg_f32(float %a) {
  %r = fneg float %a
  ret float %r
}

; 0.0 - x keeps the constant and the subtraction: neg would turn +0.0 into
; -0.0.
; CHECK-LABEL: .method {{.*}}'fsub_from_pos_zero_f32'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x00000000){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fsub_from_pos_zero_f32'{{$}}
define float @fsub_from_pos_zero_f32(float %x) {
  %r = fsub float 0.0, %x
  ret float %r
}

; -0.0 - x is fneg.
; CHECK-LABEL: .method {{.*}}'fsub_from_neg_zero_f32'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fsub_from_neg_zero_f32'{{$}}
define float @fsub_from_neg_zero_f32(float %x) {
  %r = fsub float -0.0, %x
  ret float %r
}

; A float32 expression stays float32 from start to end: nothing is widened
; and nothing is rounded between the operations.
; CHECK-LABEL: .method {{.*}}'chain_f32'(
; CHECK-SAME:  float32, float32, float32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  sub{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  add{{$}}
; O0:          {{^}}  mul{{$}}
; O0:          {{^}}  sub{{$}}
; O0:          {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'chain_f32'{{$}}
define float @chain_f32(float %a, float %b, float %c) {
  %s = fadd float %a, %b
  %m = fmul float %s, %c
  %r = fsub float %m, %a
  ret float %r
}

; fmuladd is a multiply followed by an add: no fma call, no fused operation.
; CHECK-LABEL: .method {{.*}}'fmuladd_f32'(
; CHECK-SAME:  float32, float32, float32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  mul{{$}}
; O0:          {{^}}  add{{$}}
; O0:          {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fmuladd_f32'{{$}}
define float @fmuladd_f32(float %a, float %b, float %c) {
  %r = call float @llvm.fmuladd.f32(float %a, float %b, float %c)
  ret float %r
}

; The same for float32: the locals are float32.
; CHECK-LABEL: .method {{.*}}'local_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK:       {{^}}  .locals (float32{{(, float32)*}}){{$}}
; CHECK:       {{^}}  div{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'local_f32'{{$}}
define float @local_f32(float %a, float %b) {
  %s = fdiv float %a, %b
  %t = fmul float %s, %a
  %u = fsub float %t, %s
  %v = fadd float %u, %b
  %w = fmul float %v, %s
  %x = fadd float %w, %a
  ret float %x
}

; ---- float64 ---------------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'fadd_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fadd_f64'{{$}}
; MIR-LABEL:   name: fadd_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}ADD_F64 [[A]], [[B]],
define double @fadd_f64(double %a, double %b) {
  %r = fadd double %a, %b
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'fsub_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fsub_f64'{{$}}
; MIR-LABEL:   name: fsub_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}SUB_F64 [[A]], [[B]],
define double @fsub_f64(double %a, double %b) {
  %r = fsub double %a, %b
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'fmul_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fmul_f64'{{$}}
; MIR-LABEL:   name: fmul_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}MUL_F64 [[A]], [[B]],
define double @fmul_f64(double %a, double %b) {
  %r = fmul double %a, %b
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'fdiv_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fdiv_f64'{{$}}
; MIR-LABEL:   name: fdiv_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}DIV_F64 [[A]], [[B]],
define double @fdiv_f64(double %a, double %b) {
  %r = fdiv double %a, %b
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'fneg_f64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fneg_f64'{{$}}
; MIR-LABEL:   name: fneg_f64{{$}}
; MIR:         [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}NEG_F64 [[A]],
define double @fneg_f64(double %a) {
  %r = fneg double %a
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'fsub_from_pos_zero_f64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fsub_from_pos_zero_f64'{{$}}
define double @fsub_from_pos_zero_f64(double %x) {
  %r = fsub double 0.0, %x
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'fsub_from_neg_zero_f64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fsub_from_neg_zero_f64'{{$}}
define double @fsub_from_neg_zero_f64(double %x) {
  %r = fsub double -0.0, %x
  ret double %r
}

; x + x reads the argument twice.
; CHECK-LABEL: .method {{.*}}'fadd_same_f64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fadd_same_f64'{{$}}
define double @fadd_same_f64(double %x) {
  %r = fadd double %x, %x
  ret double %r
}

; Two independent products need a third stack slot: .maxstack 3.
; CHECK-LABEL: .method {{.*}}'tree_f64'(
; CHECK-SAME:  float64, float64, float64, float64) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  ldarg.3{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  sub{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  mul{{$}}
; O0:          {{^}}  mul{{$}}
; O0:          {{^}}  sub{{$}}
; O0:          {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tree_f64'{{$}}
define double @tree_f64(double %a, double %b, double %c, double %d) {
  %ab = fmul double %a, %b
  %cd = fmul double %c, %d
  %r = fsub double %ab, %cd
  ret double %r
}

; A float64 value with two uses is kept with a tee: dup and a store into a
; float64 slot (a float64 CIL local, or the slot of a float64 argument that is
; dead by then), then a load of the same slot.
; CHECK-LABEL: .method {{.*}}'square_of_sum_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NOT:      {{^}}  {{[^.]}}
; O2:          {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  dup{{$}}
; O2-NEXT:     {{^}}  st[[KIND:arg|loc]]{{\.s |\.}}[[SLOT:[0-9]]]{{$}}
; O2-NEXT:     {{^}}  ld[[KIND]].[[SLOT]]{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  add{{$}}
; O0:          {{^}}  mul{{$}}
; O0:          {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'square_of_sum_f64'{{$}}
define double @square_of_sum_f64(double %a, double %b) {
  %s = fadd double %a, %b
  %m = fmul double %s, %s
  ret double %m
}

; A float64 value that stays live while both arguments are still needed is
; kept in a float64 CIL local; every local of this all-float64 function is a
; float64.
; CHECK-LABEL: .method {{.*}}'local_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK:       {{^}}  .locals (float64{{(, float64)*}}){{$}}
; CHECK:       {{^}}  div{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'local_f64'{{$}}
define double @local_f64(double %a, double %b) {
  %s = fdiv double %a, %b
  %t = fmul double %s, %a
  %u = fsub double %t, %s
  %v = fadd double %u, %b
  %w = fmul double %v, %s
  %x = fadd double %w, %a
  ret double %x
}

; CHECK-LABEL: .method {{.*}}'fmuladd_f64'(
; CHECK-SAME:  float64, float64, float64) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  mul{{$}}
; O0:          {{^}}  add{{$}}
; O0:          {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fmuladd_f64'{{$}}
define double @fmuladd_f64(double %a, double %b, double %c) {
  %r = call double @llvm.fmuladd.f64(double %a, double %b, double %c)
  ret double %r
}

declare float @llvm.fmuladd.f32(float, float, float)
declare double @llvm.fmuladd.f64(double, double, double)

;--- minmax.ll
; minimum, maximum, minimumnum and maximumnum: compares and select triangles
; with an arithmetic test of the sign of a zero, never a call or memory.
source_filename = "fp-arith-minmax.c"

; A NaN operand gives NaN; -0.0 is less than +0.0.
; MINMAX-LABEL: .method {{.*}}'minimum_f32'(
; MINMAX-SAME:  float32, float32) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  div{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'minimum_f32'{{$}}
define float @minimum_f32(float %a, float %b) {
  %r = call float @llvm.minimum.f32(float %a, float %b)
  ret float %r
}

; MINMAX-LABEL: .method {{.*}}'maximum_f32'(
; MINMAX-SAME:  float32, float32) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  div{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'maximum_f32'{{$}}
define float @maximum_f32(float %a, float %b) {
  %r = call float @llvm.maximum.f32(float %a, float %b)
  ret float %r
}

; A NaN operand gives the other operand; -0.0 is less than +0.0.
; MINMAX-LABEL: .method {{.*}}'minimumnum_f32'(
; MINMAX-SAME:  float32, float32) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  div{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'minimumnum_f32'{{$}}
define float @minimumnum_f32(float %a, float %b) {
  %r = call float @llvm.minimumnum.f32(float %a, float %b)
  ret float %r
}

; MINMAX-LABEL: .method {{.*}}'maximumnum_f32'(
; MINMAX-SAME:  float32, float32) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  div{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'maximumnum_f32'{{$}}
define float @maximumnum_f32(float %a, float %b) {
  %r = call float @llvm.maximumnum.f32(float %a, float %b)
  ret float %r
}

; MINMAX-LABEL: .method {{.*}}'minimum_f64'(
; MINMAX-SAME:  float64, float64) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  div{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'minimum_f64'{{$}}
define double @minimum_f64(double %a, double %b) {
  %r = call double @llvm.minimum.f64(double %a, double %b)
  ret double %r
}

; MINMAX-LABEL: .method {{.*}}'maximum_f64'(
; MINMAX-SAME:  float64, float64) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  div{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'maximum_f64'{{$}}
define double @maximum_f64(double %a, double %b) {
  %r = call double @llvm.maximum.f64(double %a, double %b)
  ret double %r
}

; MINMAX-LABEL: .method {{.*}}'minimumnum_f64'(
; MINMAX-SAME:  float64, float64) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  div{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'minimumnum_f64'{{$}}
define double @minimumnum_f64(double %a, double %b) {
  %r = call double @llvm.minimumnum.f64(double %a, double %b)
  ret double %r
}

; MINMAX-LABEL: .method {{.*}}'maximumnum_f64'(
; MINMAX-SAME:  float64, float64) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  div{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'maximumnum_f64'{{$}}
define double @maximumnum_f64(double %a, double %b) {
  %r = call double @llvm.maximumnum.f64(double %a, double %b)
  ret double %r
}

; The constrained minimum and maximum of a strictfp function: LowerStrictFP
; makes them the plain nodes.
; MINMAX-LABEL: .method {{.*}}'strict_minimum_f32'(
; MINMAX-SAME:  float32, float32) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  div{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'strict_minimum_f32'{{$}}
define float @strict_minimum_f32(float %a, float %b) #0 {
  %r = call float @llvm.experimental.constrained.minimum.f32(float %a, float %b, metadata !"fpexcept.strict") #0
  ret float %r
}

; MINMAX-LABEL: .method {{.*}}'strict_maximum_f32'(
; MINMAX-SAME:  float32, float32) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  div{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'strict_maximum_f32'{{$}}
define float @strict_maximum_f32(float %a, float %b) #0 {
  %r = call float @llvm.experimental.constrained.maximum.f32(float %a, float %b, metadata !"fpexcept.strict") #0
  ret float %r
}

; MINMAX-LABEL: .method {{.*}}'strict_minimum_f64'(
; MINMAX-SAME:  float64, float64) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  div{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'strict_minimum_f64'{{$}}
define double @strict_minimum_f64(double %a, double %b) #0 {
  %r = call double @llvm.experimental.constrained.minimum.f64(double %a, double %b, metadata !"fpexcept.strict") #0
  ret double %r
}

; MINMAX-LABEL: .method {{.*}}'strict_maximum_f64'(
; MINMAX-SAME:  float64, float64) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  div{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'strict_maximum_f64'{{$}}
define double @strict_maximum_f64(double %a, double %b) #0 {
  %r = call double @llvm.experimental.constrained.maximum.f64(double %a, double %b, metadata !"fpexcept.strict") #0
  ret double %r
}

; Fast-math fmin and fmax: nnan minnum and maxnum (with and without nsz)
; become FMINIMUM and FMAXIMUM in the legalizer, never the fmin/fmax call of
; fp-expand.ll or a diagnostic.
; MINMAX-LABEL: .method {{.*}}'minnum_nnan_nsz_f64'(
; MINMAX-SAME:  float64, float64) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'minnum_nnan_nsz_f64'{{$}}
define double @minnum_nnan_nsz_f64(double %a, double %b) {
  %r = call nnan nsz double @llvm.minnum.f64(double %a, double %b)
  ret double %r
}

; MINMAX-LABEL: .method {{.*}}'maxnum_nnan_f32'(
; MINMAX-SAME:  float32, float32) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'maxnum_nnan_f32'{{$}}
define float @maxnum_nnan_f32(float %a, float %b) {
  %r = call nnan float @llvm.maxnum.f32(float %a, float %b)
  ret float %r
}

; MINMAX-LABEL: .method {{.*}}'minnum_nnan_f32'(
; MINMAX-SAME:  float32, float32) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'minnum_nnan_f32'{{$}}
define float @minnum_nnan_f32(float %a, float %b) {
  %r = call nnan float @llvm.minnum.f32(float %a, float %b)
  ret float %r
}

; MINMAX-LABEL: .method {{.*}}'maxnum_nnan_nsz_f64'(
; MINMAX-SAME:  float64, float64) cil managed{{$}}
; MINMAX-DAG:   {{^}}  {{c(eq|gt|lt)(\.un)?}}{{$}}
; MINMAX-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX-DAG:   {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'maxnum_nnan_nsz_f64'{{$}}
define double @maxnum_nnan_nsz_f64(double %a, double %b) {
  %r = call nnan nsz double @llvm.maxnum.f64(double %a, double %b)
  ret double %r
}

; One constant operand, 1.0: x is the result when the compare holds. For
; minimum and maximum a NaN x must give a NaN, so the compare is the
; unordered one (x <u 1.0, x >u 1.0); for minimumnum and maximumnum (constant
; first operand) a NaN x must give 1.0, so it is the ordered one.
; MINMAX-LABEL: .method {{.*}}'minimum_x_1_f64'(
; MINMAX-SAME:  float64) cil managed{{$}}
; MINMAX-NOT:   {{^}}  c{{(eq|gt|lt)}}
; MINMAX:       {{^}}  clt.un{{$}}
; MINMAX-NOT:   {{^}}  c{{(eq|gt|lt)}}
; MINMAX:       {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX:       {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'minimum_x_1_f64'{{$}}
; MMO2-LABEL:   .method {{.*}}'minimum_x_1_f64'(
; MMO2-NEXT:    {{^\{$}}
; MMO2-NEXT:    {{^}}  .maxstack 2{{$}}
; MMO2-NEXT:    {{^}}  ldarg.0{{$}}
; MMO2-NEXT:    {{^}}  ldc.r8 float64(0x3FF0000000000000){{$}}
; MMO2-NEXT:    {{^}}  clt.un{{$}}
; MMO2-NEXT:    {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MMO2-NEXT:    {{^}}  ldc.r8 float64(0x3FF0000000000000){{$}}
; MMO2-NEXT:    {{^}}  starg.s 0{{$}}
; MMO2-NEXT:    {{^}}@BB[[F]]_[[B]]:{{$}}
; MMO2-NEXT:    {{^}}  ldarg.0{{$}}
; MMO2-NEXT:    {{^}}  ret{{$}}
; MMO2-NEXT:    {{^}}} // end of method 'minimum_x_1_f64'{{$}}
define double @minimum_x_1_f64(double %x) {
  %r = call double @llvm.minimum.f64(double %x, double 1.0)
  ret double %r
}

; MINMAX-LABEL: .method {{.*}}'maximum_x_1_f32'(
; MINMAX-SAME:  float32) cil managed{{$}}
; MINMAX-NOT:   {{^}}  c{{(eq|gt|lt)}}
; MINMAX:       {{^}}  cgt.un{{$}}
; MINMAX-NOT:   {{^}}  c{{(eq|gt|lt)}}
; MINMAX:       {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX:       {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'maximum_x_1_f32'{{$}}
; MMO2-LABEL:   .method {{.*}}'maximum_x_1_f32'(
; MMO2-NEXT:    {{^\{$}}
; MMO2-NEXT:    {{^}}  .maxstack 2{{$}}
; MMO2-NEXT:    {{^}}  ldarg.0{{$}}
; MMO2-NEXT:    {{^}}  ldc.r4 float32(0x3F800000){{$}}
; MMO2-NEXT:    {{^}}  cgt.un{{$}}
; MMO2-NEXT:    {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MMO2-NEXT:    {{^}}  ldc.r4 float32(0x3F800000){{$}}
; MMO2-NEXT:    {{^}}  starg.s 0{{$}}
; MMO2-NEXT:    {{^}}@BB[[F]]_[[B]]:{{$}}
; MMO2-NEXT:    {{^}}  ldarg.0{{$}}
; MMO2-NEXT:    {{^}}  ret{{$}}
; MMO2-NEXT:    {{^}}} // end of method 'maximum_x_1_f32'{{$}}
define float @maximum_x_1_f32(float %x) {
  %r = call float @llvm.maximum.f32(float %x, float 1.0)
  ret float %r
}

; MINMAX-LABEL: .method {{.*}}'minimumnum_1_x_f64'(
; MINMAX-SAME:  float64) cil managed{{$}}
; MINMAX-NOT:   {{^}}  c{{(eq|gt|lt)}}
; MINMAX:       {{^}}  clt{{$}}
; MINMAX-NOT:   {{^}}  c{{(eq|gt|lt)}}
; MINMAX:       {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX:       {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'minimumnum_1_x_f64'{{$}}
; MMO2-LABEL:   .method {{.*}}'minimumnum_1_x_f64'(
; MMO2-NEXT:    {{^\{$}}
; MMO2-NEXT:    {{^}}  .maxstack 2{{$}}
; MMO2-NEXT:    {{^}}  ldarg.0{{$}}
; MMO2-NEXT:    {{^}}  ldc.r8 float64(0x3FF0000000000000){{$}}
; MMO2-NEXT:    {{^}}  clt{{$}}
; MMO2-NEXT:    {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MMO2-NEXT:    {{^}}  ldc.r8 float64(0x3FF0000000000000){{$}}
; MMO2-NEXT:    {{^}}  starg.s 0{{$}}
; MMO2-NEXT:    {{^}}@BB[[F]]_[[B]]:{{$}}
; MMO2-NEXT:    {{^}}  ldarg.0{{$}}
; MMO2-NEXT:    {{^}}  ret{{$}}
; MMO2-NEXT:    {{^}}} // end of method 'minimumnum_1_x_f64'{{$}}
define double @minimumnum_1_x_f64(double %x) {
  %r = call double @llvm.minimumnum.f64(double 1.0, double %x)
  ret double %r
}

; MINMAX-LABEL: .method {{.*}}'maximumnum_1_x_f32'(
; MINMAX-SAME:  float32) cil managed{{$}}
; MINMAX-NOT:   {{^}}  c{{(eq|gt|lt)}}
; MINMAX:       {{^}}  cgt{{$}}
; MINMAX-NOT:   {{^}}  c{{(eq|gt|lt)}}
; MINMAX:       {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MINMAX:       {{^}}@BB[[F]]_[[B]]:{{$}}
; MINMAX:       {{^}}  ret{{$}}
; MINMAX-NEXT:  {{^}}} // end of method 'maximumnum_1_x_f32'{{$}}
; MMO2-LABEL:   .method {{.*}}'maximumnum_1_x_f32'(
; MMO2-NEXT:    {{^\{$}}
; MMO2-NEXT:    {{^}}  .maxstack 2{{$}}
; MMO2-NEXT:    {{^}}  ldarg.0{{$}}
; MMO2-NEXT:    {{^}}  ldc.r4 float32(0x3F800000){{$}}
; MMO2-NEXT:    {{^}}  cgt{{$}}
; MMO2-NEXT:    {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; MMO2-NEXT:    {{^}}  ldc.r4 float32(0x3F800000){{$}}
; MMO2-NEXT:    {{^}}  starg.s 0{{$}}
; MMO2-NEXT:    {{^}}@BB[[F]]_[[B]]:{{$}}
; MMO2-NEXT:    {{^}}  ldarg.0{{$}}
; MMO2-NEXT:    {{^}}  ret{{$}}
; MMO2-NEXT:    {{^}}} // end of method 'maximumnum_1_x_f32'{{$}}
define float @maximumnum_1_x_f32(float %x) {
  %r = call float @llvm.maximumnum.f32(float 1.0, float %x)
  ret float %r
}

declare float @llvm.maximum.f32(float, float)
declare float @llvm.maximumnum.f32(float, float)
declare float @llvm.minimum.f32(float, float)
declare float @llvm.minimumnum.f32(float, float)
declare double @llvm.maximum.f64(double, double)
declare double @llvm.maximumnum.f64(double, double)
declare double @llvm.minimum.f64(double, double)
declare double @llvm.minimumnum.f64(double, double)
declare float @llvm.maxnum.f32(float, float)
declare float @llvm.minnum.f32(float, float)
declare double @llvm.maxnum.f64(double, double)
declare double @llvm.minnum.f64(double, double)
declare float @llvm.experimental.constrained.maximum.f32(float, float, metadata)
declare float @llvm.experimental.constrained.minimum.f32(float, float, metadata)
declare double @llvm.experimental.constrained.maximum.f64(double, double, metadata)
declare double @llvm.experimental.constrained.minimum.f64(double, double, metadata)

attributes #0 = { strictfp }
