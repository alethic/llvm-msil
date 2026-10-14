; uint64 -> float64 and uint64 -> float32 (ImplementationPlan.md decision D17,
; M5.a LowerUINT_TO_FP; recon isel.md 3.2 and plan section 12, row 'recon/
; isel.md decision on uint64->f32 ... superseded by the D17 sequence'): CIL has
; no unsigned 64-bit conversion that is correctly rounded on every runtime
; (recon cil-runtime.md e05 and section 6): conv.r.un rounds a 64-bit input
; twice on .NET 8 and .NET Framework, for float32 and float64 results alike,
; and on .NET 9 when the result is float32. So uitofp from i64 is
; custom-lowered for both result types to compiler-rt's halving with a sticky
; bit:
;   s = srl x, 63                      ldc.i4.s 63; shr.un
;   y = or (srl x, trunc s), (and x, s)  conv.i4; shr.un; and; or
;   r = sint_to_fp y                   conv.r8 (round to odd, conv.r4)
;   result = fmul r, sint_to_fp(add s, 1)  add; conv.r8 (conv.r4); mul
; For x < 2^63 this is sint_to_fp x times 1.0; otherwise y is x/2 with the
; lost bit kept as a sticky bit, the single rounding happens in the signed
; conversion of y, and the multiply by 2.0 is exact. The signed conversion of
; the native int y rounds once to the result type: float32 results never go
; through float64 (conv.r8 then conv.r4 would round twice). For float32 it is
; the int64 to float32 conversion of fp-conv.ll: y is first rounded to odd at
; bit 11 when it needs more than 53 bits, with integer operations only, so
; the result stays correctly rounded where the JIT folds conv.r4 through
; float64 (plan section 12, row 'M5.a extended (int64 to float32 under JIT
; constant folding)'). The sequence has no conv.r.un on a 64-bit operand, no
; select (so no branch), no memory and no frame (stack: [] after instruction
; selection, no localloc), and no call (__floatundidf, __floatundisf). Its
; results on every runtime are checked by M5.d's u64-to-fp-rounding.ll. The
; constrained conversion (llvm.experimental.constrained.uitofp in a strictfp
; function) is the same sequence: CIL has no floating-point exceptions and
; rounds only to nearest, so LowerStrictFP rewrites STRICT_UINT_TO_FP to the
; plain node (plan section 12, row 'M5.a extended (constrained floating point
; and rounding mode)').
;
; The CIL lines fix only what the data flow forces: the shift by 63 comes
; first, the multiply is the last operation, and the rest appears in between
; in any order. They are shared by -O0 and -O2. The MIR lines check the plan
; 5.4 records and the data flow right after instruction selection: one block,
; no stack objects, no PHI.

; The forbidden opcodes and records are checked over the whole output by a
; second FileCheck run (BAN, MIRBAN) whose only directive is an early line of
; the output, because implicit negative checks do not look between CHECK-DAG
; matches.
; DEFINE: %{il} = FileCheck %s
; DEFINE: %{ban} = FileCheck %s --check-prefix=BAN --implicit-check-not=conv.r.un \
; DEFINE:   --implicit-check-not=localloc --implicit-check-not=call --implicit-check-not=ldind \
; DEFINE:   --implicit-check-not=stind --implicit-check-not=int64 --implicit-check-not=conv.i8 \
; DEFINE:   --implicit-check-not=conv.u8 --implicit-check-not=conv.ovf --implicit-check-not="{{^ b}}"
; DEFINE: %{mirban} = FileCheck %s --check-prefix=MIRBAN --implicit-check-not=PHI \
; DEFINE:   --implicit-check-not=SELECT_ --implicit-check-not=CONV_R_UN --implicit-check-not=BR \
; DEFINE:   --implicit-check-not=bb.1 --implicit-check-not=LOCALLOC
; RUN: split-file %s %t
; BAN:    {{^}}.assembly '
; MIRBAN: {{^}}name:

; float64 results.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/f64.ll \
; RUN:   -o %t/f64.lnx.o2.il
; RUN: %{il} --check-prefix=F64 --input-file=%t/f64.lnx.o2.il
; RUN: %{ban} --implicit-check-not=conv.r4 --input-file=%t/f64.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/f64.ll \
; RUN:   -o %t/f64.win.o2.il
; RUN: %{il} --check-prefix=F64 --input-file=%t/f64.win.o2.il
; RUN: %{ban} --implicit-check-not=conv.r4 --input-file=%t/f64.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/f64.ll \
; RUN:   -o %t/f64.lnx.o0.il
; RUN: %{il} --check-prefix=F64 --input-file=%t/f64.lnx.o0.il
; RUN: %{ban} --implicit-check-not=conv.r4 --input-file=%t/f64.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/f64.ll \
; RUN:   -o %t/f64.win.o0.il
; RUN: %{il} --check-prefix=F64 --input-file=%t/f64.win.o0.il
; RUN: %{ban} --implicit-check-not=conv.r4 --input-file=%t/f64.win.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-after=finalize-isel < %t/f64.ll \
; RUN:   -o %t/f64.win.o2.mir
; RUN: FileCheck %s --check-prefix=MIR64 --input-file=%t/f64.win.o2.mir
; RUN: %{mirban} --implicit-check-not=CONV_R4 --input-file=%t/f64.win.o2.mir
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %t/f64.ll \
; RUN:   -o %t/f64.lnx.o0.mir
; RUN: FileCheck %s --check-prefix=MIR64 --input-file=%t/f64.lnx.o0.mir
; RUN: %{mirban} --implicit-check-not=CONV_R4 --input-file=%t/f64.lnx.o0.mir
; RUN: %if ilasm %{ %ilasm-verify -output=%t/f64.lnx.o2.dll %t/f64.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/f64.win.o0.dll %t/f64.win.o0.il %}

; float32 results: the same sequence with conv.r4, and no conv.r8 anywhere.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/f32.ll \
; RUN:   -o %t/f32.lnx.o2.il
; RUN: %{il} --check-prefix=F32 --input-file=%t/f32.lnx.o2.il
; RUN: %{ban} --implicit-check-not=conv.r8 --input-file=%t/f32.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/f32.ll \
; RUN:   -o %t/f32.win.o2.il
; RUN: %{il} --check-prefix=F32 --input-file=%t/f32.win.o2.il
; RUN: %{ban} --implicit-check-not=conv.r8 --input-file=%t/f32.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/f32.ll \
; RUN:   -o %t/f32.lnx.o0.il
; RUN: %{il} --check-prefix=F32 --input-file=%t/f32.lnx.o0.il
; RUN: %{ban} --implicit-check-not=conv.r8 --input-file=%t/f32.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/f32.ll \
; RUN:   -o %t/f32.win.o0.il
; RUN: %{il} --check-prefix=F32 --input-file=%t/f32.win.o0.il
; RUN: %{ban} --implicit-check-not=conv.r8 --input-file=%t/f32.win.o0.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -stop-after=finalize-isel < %t/f32.ll \
; RUN:   -o %t/f32.lnx.o2.mir
; RUN: FileCheck %s --check-prefix=MIR32 --input-file=%t/f32.lnx.o2.mir
; RUN: %{mirban} --implicit-check-not=CONV_R8 --input-file=%t/f32.lnx.o2.mir
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %t/f32.ll \
; RUN:   -o %t/f32.win.o0.mir
; RUN: FileCheck %s --check-prefix=MIR32 --input-file=%t/f32.win.o0.mir
; RUN: %{mirban} --implicit-check-not=CONV_R8 --input-file=%t/f32.win.o0.mir
; RUN: %if ilasm %{ %ilasm-verify -output=%t/f32.win.o2.dll %t/f32.win.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/f32.lnx.o0.dll %t/f32.lnx.o0.il %}

;--- f64.ll
source_filename = "u64-to-f64.c"

; F64-LABEL:  .method {{.*}}'u64_to_f64'(
; F64-SAME:   native int) cil managed{{$}}
; F64:        {{^}}  ldc.i4.s 63{{$}}
; F64:        {{^}}  shr.un{{$}}
; F64-DAG:    {{^}}  conv.i4{{$}}
; F64-DAG:    {{^}}  shr.un{{$}}
; F64-DAG:    {{^}}  and{{$}}
; F64-DAG:    {{^}}  or{{$}}
; F64-DAG:    {{^}}  add{{$}}
; F64-DAG:    {{^}}  conv.r8{{$}}
; F64-DAG:    {{^}}  conv.r8{{$}}
; F64:        {{^}}  mul{{$}}
; F64-NEXT:   {{^}}  ret{{$}}
; F64-NEXT:   {{^}}} // end of method 'u64_to_f64'{{$}}
; MIR64-LABEL: name: u64_to_f64{{$}}
; MIR64:       stack: []
; MIR64:       body:
; MIR64-NEXT:  bb.0
; MIR64-DAG:   [[X:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR64-DAG:   [[C63:%[0-9]+]]:i32 = CONST_I32 63,
; MIR64-DAG:   [[S:%[0-9]+]]:i64 = {{([a-z]+ )*}}SHR_U_I64 [[X]], [[C63]],
; MIR64-DAG:   [[T:%[0-9]+]]:i32 = {{([a-z]+ )*}}CONV_I4_I64 [[S]],
; MIR64-DAG:   {{%[0-9]+}}:i64 = {{([a-z]+ )*}}SHR_U_I64 [[X]], [[T]],
; MIR64-DAG:   {{%[0-9]+}}:i64 = {{([a-z]+ )*}}AND_I64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR64-DAG:   [[Y:%[0-9]+]]:i64 = {{([a-z]+ )*}}OR_I64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR64-DAG:   {{%[0-9]+}}:f64 = {{([a-z]+ )*}}CONV_R8_I64 [[Y]],
; MIR64-DAG:   {{%[0-9]+}}:f64 = {{([a-z]+ )*}}CONV_R8_I{{32|64}} {{%[0-9]+}},
; MIR64:       [[M:%[0-9]+]]:f64 = {{([a-z]+ )*}}MUL_F64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR64-NEXT:  RETURN [[M]],
define double @u64_to_f64(i64 %x) {
  %r = uitofp i64 %x to double
  ret double %r
}

; The converted value is computed and has several uses in the sequence.
; F64-LABEL:  .method {{.*}}'u64_sum_to_f64'(
; F64-SAME:   native int, native int) cil managed{{$}}
; F64:        {{^}}  add{{$}}
; F64:        {{^}}  ldc.i4.s 63{{$}}
; F64:        {{^}}  shr.un{{$}}
; F64-DAG:    {{^}}  conv.i4{{$}}
; F64-DAG:    {{^}}  shr.un{{$}}
; F64-DAG:    {{^}}  and{{$}}
; F64-DAG:    {{^}}  or{{$}}
; F64-DAG:    {{^}}  add{{$}}
; F64-DAG:    {{^}}  conv.r8{{$}}
; F64-DAG:    {{^}}  conv.r8{{$}}
; F64:        {{^}}  mul{{$}}
; F64-NEXT:   {{^}}  ret{{$}}
; F64-NEXT:   {{^}}} // end of method 'u64_sum_to_f64'{{$}}
; MIR64-LABEL: name: u64_sum_to_f64{{$}}
; MIR64:       stack: []
; MIR64:       {{%[0-9]+}}:f64 = {{([a-z]+ )*}}MUL_F64
define double @u64_sum_to_f64(i64 %a, i64 %b) {
  %x = add i64 %a, %b
  %r = uitofp i64 %x to double
  ret double %r
}

; The result feeds float64 arithmetic.
; F64-LABEL:  .method {{.*}}'u64_to_f64_plus'(
; F64-SAME:   native int, float64) cil managed{{$}}
; F64:        {{^}}  ldc.i4.s 63{{$}}
; F64:        {{^}}  shr.un{{$}}
; F64-DAG:    {{^}}  conv.r8{{$}}
; F64-DAG:    {{^}}  conv.r8{{$}}
; F64:        {{^}}  mul{{$}}
; F64:        {{^}}  ldarg.1{{$}}
; F64-NEXT:   {{^}}  add{{$}}
; F64-NEXT:   {{^}}  ret{{$}}
; F64-NEXT:   {{^}}} // end of method 'u64_to_f64_plus'{{$}}
define double @u64_to_f64_plus(i64 %x, double %y) {
  %f = uitofp i64 %x to double
  %r = fadd double %f, %y
  ret double %r
}

; With the sign bit known to be zero the value converts like a signed one;
; whatever is left of the sequence, there is still no conv.r.un, branch or
; memory access.
; F64-LABEL:  .method {{.*}}'u64_half_to_f64'(
; F64-SAME:   native int) cil managed{{$}}
; F64:        {{^}}  conv.r8{{$}}
; F64:        {{^}}  ret{{$}}
; F64-NEXT:   {{^}}} // end of method 'u64_half_to_f64'{{$}}
define double @u64_half_to_f64(i64 %x) {
  %h = lshr i64 %x, 1
  %r = uitofp i64 %h to double
  ret double %r
}

; The constrained conversion: the same sequence, still without conv.r.un.
; F64-LABEL:  .method {{.*}}'u64_to_f64_strict'(
; F64-SAME:   native int) cil managed{{$}}
; F64:        {{^}}  ldc.i4.s 63{{$}}
; F64:        {{^}}  shr.un{{$}}
; F64-DAG:    {{^}}  conv.i4{{$}}
; F64-DAG:    {{^}}  shr.un{{$}}
; F64-DAG:    {{^}}  and{{$}}
; F64-DAG:    {{^}}  or{{$}}
; F64-DAG:    {{^}}  add{{$}}
; F64-DAG:    {{^}}  conv.r8{{$}}
; F64-DAG:    {{^}}  conv.r8{{$}}
; F64:        {{^}}  mul{{$}}
; F64-NEXT:   {{^}}  ret{{$}}
; F64-NEXT:   {{^}}} // end of method 'u64_to_f64_strict'{{$}}
; MIR64-LABEL: name: u64_to_f64_strict{{$}}
; MIR64:       stack: []
; MIR64:       body:
; MIR64-NEXT:  bb.0
; MIR64-DAG:   [[X:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR64-DAG:   [[C63:%[0-9]+]]:i32 = CONST_I32 63,
; MIR64-DAG:   [[S:%[0-9]+]]:i64 = {{([a-z]+ )*}}SHR_U_I64 [[X]], [[C63]],
; MIR64-DAG:   [[T:%[0-9]+]]:i32 = {{([a-z]+ )*}}CONV_I4_I64 [[S]],
; MIR64-DAG:   {{%[0-9]+}}:i64 = {{([a-z]+ )*}}SHR_U_I64 [[X]], [[T]],
; MIR64-DAG:   {{%[0-9]+}}:i64 = {{([a-z]+ )*}}AND_I64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR64-DAG:   [[Y:%[0-9]+]]:i64 = {{([a-z]+ )*}}OR_I64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR64-DAG:   {{%[0-9]+}}:f64 = {{([a-z]+ )*}}CONV_R8_I64 [[Y]],
; MIR64-DAG:   {{%[0-9]+}}:f64 = {{([a-z]+ )*}}CONV_R8_I{{32|64}} {{%[0-9]+}},
; MIR64:       [[M:%[0-9]+]]:f64 = {{([a-z]+ )*}}MUL_F64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR64-NEXT:  RETURN [[M]],
define double @u64_to_f64_strict(i64 %x) #0 {
  %r = call double @llvm.experimental.constrained.uitofp.f64.i64(i64 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret double %r
}

declare double @llvm.experimental.constrained.uitofp.f64.i64(i64, metadata, metadata)

attributes #0 = { strictfp }

;--- f32.ll
source_filename = "u64-to-f32.c"

; y is rounded to odd before its conversion: y feeds an and of that step, and
; CONV_R4_I64 converts the step's final or (the second OR_I64), not y.
; F32-LABEL:  .method {{.*}}'u64_to_f32'(
; F32-SAME:   native int) cil managed{{$}}
; F32:        {{^}}  ldc.i4.s 63{{$}}
; F32:        {{^}}  shr.un{{$}}
; F32-DAG:    {{^}}  conv.i4{{$}}
; F32-DAG:    {{^}}  shr.un{{$}}
; F32-DAG:    {{^}}  and{{$}}
; F32-DAG:    {{^}}  or{{$}}
; F32-DAG:    {{^}}  add{{$}}
; F32-DAG:    {{^}}  conv.r4{{$}}
; F32-DAG:    {{^}}  conv.r4{{$}}
; F32:        {{^}}  mul{{$}}
; F32-NEXT:   {{^}}  ret{{$}}
; F32-NEXT:   {{^}}} // end of method 'u64_to_f32'{{$}}
; MIR32-LABEL: name: u64_to_f32{{$}}
; MIR32:       stack: []
; MIR32:       body:
; MIR32-NEXT:  bb.0
; MIR32-DAG:   [[X:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR32-DAG:   [[C63:%[0-9]+]]:i32 = CONST_I32 63,
; MIR32-DAG:   [[S:%[0-9]+]]:i64 = {{([a-z]+ )*}}SHR_U_I64 [[X]], [[C63]],
; MIR32-DAG:   [[T:%[0-9]+]]:i32 = {{([a-z]+ )*}}CONV_I4_I64 [[S]],
; MIR32-DAG:   {{%[0-9]+}}:i64 = {{([a-z]+ )*}}SHR_U_I64 [[X]], [[T]],
; MIR32-DAG:   {{%[0-9]+}}:i64 = {{([a-z]+ )*}}AND_I64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR32-DAG:   [[Y:%[0-9]+]]:i64 = {{([a-z]+ )*}}OR_I64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR32-DAG:   {{%[0-9]+}}:i64 = {{([a-z]+ )*}}AND_I64 {{.*}}[[Y]],
; MIR32-DAG:   [[YO:%[0-9]+]]:i64 = {{([a-z]+ )*}}OR_I64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR32-DAG:   {{%[0-9]+}}:f32 = {{([a-z]+ )*}}CONV_R4_I64 [[YO]],
; MIR32-DAG:   {{%[0-9]+}}:f32 = {{([a-z]+ )*}}CONV_R4_I{{32|64}} {{%[0-9]+}},
; MIR32:       [[M:%[0-9]+]]:f32 = {{([a-z]+ )*}}MUL_F32 {{%[0-9]+}}, {{%[0-9]+}},
; MIR32-NEXT:  RETURN [[M]],
define float @u64_to_f32(i64 %x) {
  %r = uitofp i64 %x to float
  ret float %r
}

; F32-LABEL:  .method {{.*}}'u64_sum_to_f32'(
; F32-SAME:   native int, native int) cil managed{{$}}
; F32:        {{^}}  add{{$}}
; F32:        {{^}}  ldc.i4.s 63{{$}}
; F32:        {{^}}  shr.un{{$}}
; F32-DAG:    {{^}}  conv.i4{{$}}
; F32-DAG:    {{^}}  shr.un{{$}}
; F32-DAG:    {{^}}  and{{$}}
; F32-DAG:    {{^}}  or{{$}}
; F32-DAG:    {{^}}  add{{$}}
; F32-DAG:    {{^}}  conv.r4{{$}}
; F32-DAG:    {{^}}  conv.r4{{$}}
; F32:        {{^}}  mul{{$}}
; F32-NEXT:   {{^}}  ret{{$}}
; F32-NEXT:   {{^}}} // end of method 'u64_sum_to_f32'{{$}}
; MIR32-LABEL: name: u64_sum_to_f32{{$}}
; MIR32:       stack: []
; MIR32:       {{%[0-9]+}}:f32 = {{([a-z]+ )*}}MUL_F32
define float @u64_sum_to_f32(i64 %a, i64 %b) {
  %x = add i64 %a, %b
  %r = uitofp i64 %x to float
  ret float %r
}

; A float32 result used in float32 arithmetic stays float32.
; F32-LABEL:  .method {{.*}}'u64_to_f32_times'(
; F32-SAME:   native int, float32) cil managed{{$}}
; F32:        {{^}}  ldc.i4.s 63{{$}}
; F32:        {{^}}  shr.un{{$}}
; F32-DAG:    {{^}}  conv.r4{{$}}
; F32-DAG:    {{^}}  conv.r4{{$}}
; F32:        {{^}}  mul{{$}}
; F32:        {{^}}  ldarg.1{{$}}
; F32-NEXT:   {{^}}  mul{{$}}
; F32-NEXT:   {{^}}  ret{{$}}
; F32-NEXT:   {{^}}} // end of method 'u64_to_f32_times'{{$}}
define float @u64_to_f32_times(i64 %x, float %y) {
  %f = uitofp i64 %x to float
  %r = fmul float %f, %y
  ret float %r
}

; With the sign bit known to be zero the value converts like a signed one,
; but it can still need more than 53 bits, so the rounding to odd of
; fp-conv.ll's sitofp_i64_f32 stays: a bare conv.r4 would round twice where
; the JIT folds it through float64 (plan section 12, row 'M5.a extended
; (int64 to float32 under JIT constant folding)'). Its sign step folds away
; and the lshr by 1 folds into the 53-bit threshold shift (54); then 1023, 10
; and 2047 (0x7FF), each with its operation, the final or and one conv.r4.
; F32-LABEL:  .method {{.*}}'u64_half_to_f32'(
; F32-SAME:   native int) cil managed{{$}}
; F32-NEXT:   {{^\{$}}
; F32-NOT:    {{^(  conv\.r|  b|  switch|@)}}
; F32:        {{^}}  ldc.i4.s 54{{$}}
; F32-NEXT:   {{^}}  shr.un{{$}}
; F32-NOT:    {{^(  conv\.r|  b|  switch|@)}}
; F32:        {{^}}  ldc.i4 1023{{$}}
; F32-NEXT:   {{^}}  conv.i{{$}}
; F32-NEXT:   {{^}}  add{{$}}
; F32-NOT:    {{^(  conv\.r|  b|  switch|@)}}
; F32:        {{^}}  ldc.i4.s 10{{$}}
; F32-NEXT:   {{^}}  shr.un{{$}}
; F32-NOT:    {{^(  conv\.r|  b|  switch|@)}}
; F32:        {{^}}  ldc.i4 2047{{$}}
; F32-NEXT:   {{^}}  conv.i{{$}}
; F32-NEXT:   {{^}}  mul{{$}}
; F32-NOT:    {{^(  conv\.r|  b|  switch|@)}}
; F32:        {{^}}  or{{$}}
; F32-NEXT:   {{^}}  conv.r4{{$}}
; F32-NEXT:   {{^}}  ret{{$}}
; F32-NEXT:   {{^}}} // end of method 'u64_half_to_f32'{{$}}
define float @u64_half_to_f32(i64 %x) {
  %h = lshr i64 %x, 1
  %r = uitofp i64 %h to float
  ret float %r
}

; The constrained conversion: the same sequence with the same rounding to odd.
; F32-LABEL:  .method {{.*}}'u64_to_f32_strict'(
; F32-SAME:   native int) cil managed{{$}}
; F32:        {{^}}  ldc.i4.s 63{{$}}
; F32:        {{^}}  shr.un{{$}}
; F32-DAG:    {{^}}  conv.i4{{$}}
; F32-DAG:    {{^}}  shr.un{{$}}
; F32-DAG:    {{^}}  and{{$}}
; F32-DAG:    {{^}}  or{{$}}
; F32-DAG:    {{^}}  add{{$}}
; F32-DAG:    {{^}}  conv.r4{{$}}
; F32-DAG:    {{^}}  conv.r4{{$}}
; F32:        {{^}}  mul{{$}}
; F32-NEXT:   {{^}}  ret{{$}}
; F32-NEXT:   {{^}}} // end of method 'u64_to_f32_strict'{{$}}
; MIR32-LABEL: name: u64_to_f32_strict{{$}}
; MIR32:       stack: []
; MIR32:       body:
; MIR32-NEXT:  bb.0
; MIR32-DAG:   [[X:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR32-DAG:   [[C63:%[0-9]+]]:i32 = CONST_I32 63,
; MIR32-DAG:   [[S:%[0-9]+]]:i64 = {{([a-z]+ )*}}SHR_U_I64 [[X]], [[C63]],
; MIR32-DAG:   [[T:%[0-9]+]]:i32 = {{([a-z]+ )*}}CONV_I4_I64 [[S]],
; MIR32-DAG:   {{%[0-9]+}}:i64 = {{([a-z]+ )*}}SHR_U_I64 [[X]], [[T]],
; MIR32-DAG:   {{%[0-9]+}}:i64 = {{([a-z]+ )*}}AND_I64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR32-DAG:   [[Y:%[0-9]+]]:i64 = {{([a-z]+ )*}}OR_I64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR32-DAG:   {{%[0-9]+}}:i64 = {{([a-z]+ )*}}AND_I64 {{.*}}[[Y]],
; MIR32-DAG:   [[YO:%[0-9]+]]:i64 = {{([a-z]+ )*}}OR_I64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR32-DAG:   {{%[0-9]+}}:f32 = {{([a-z]+ )*}}CONV_R4_I64 [[YO]],
; MIR32-DAG:   {{%[0-9]+}}:f32 = {{([a-z]+ )*}}CONV_R4_I{{32|64}} {{%[0-9]+}},
; MIR32:       [[M:%[0-9]+]]:f32 = {{([a-z]+ )*}}MUL_F32 {{%[0-9]+}}, {{%[0-9]+}},
; MIR32-NEXT:  RETURN [[M]],
define float @u64_to_f32_strict(i64 %x) #0 {
  %r = call float @llvm.experimental.constrained.uitofp.f32.i64(i64 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret float %r
}

declare float @llvm.experimental.constrained.uitofp.f32.i64(i64, metadata, metadata)

attributes #0 = { strictfp }
