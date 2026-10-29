; i128 division and remainder and the floating-point <-> i128 conversions
; expand inline (ImplementationPlan.md M12.b (1), Q36; the section 12 row of
; 2026-10-21 for M12, 'D14/D33 note and P3-B text'; recon p2/backlog.md 2.2).
; MSILTargetLowering sets setMaxDivRemBitWidthSupported(64) and
; setMaxLargeFPConvertBitWidthSupported(64), so ExpandIRInsts, which every
; target runs before instruction selection, rewrites udiv, urem, sdiv and srem
; of i128 by a variable, the eight plain conversions between float32 or
; float64 and signed or unsigned i128, and llvm.fptosi.sat.i128.f64 into
; inline IR (600-1,050 IL instructions each; NVPTX and AMDGPU do the same).
; No compiler-rt helper is asked for: no __divti3, __udivti3, __modti3,
; __umodti3, __fixsfti, __fixdfti, __fixunssfti, __fixunsdfti, __floattisf,
; __floattidf, __floatuntisf or __floatuntidf, no call at all, and none of
; Phase 1's 'runtime helper ... (phase 3)' errors. i128 results are returned
; through the hidden result pointer (the first parameter, M7), so each method
; has no call. llc reports nothing at -O0 and -O2 on both triples, and the
; output assembles; the values are checked by the execution tests
; msil/test/M1/01-int/i128-divrem.c and 02-fp/i128-fp-conv.c (M12.d).
;
; helpers.ll (a split part): the expansion covers the plain conversions from
; and to float32 and float64, and from bfloat (fp-expand.ll's i128.ll). The
; constrained (strictfp) conversions are not expanded and still name
; __fixdfti, __fixunssfti, __floattidf and __floatuntisf; the half
; conversions name the float32 <-> half helpers that the expansion goes
; through (__extendhfsf2, __truncsfhf2); and sitofp and uitofp from i128 to
; bfloat name __extendbfsf2 (the section 12 row 'Row 'D14/D33 note and P3-B
; text' (2026-10-21) and M12.b (1) corrected'). They also named __truncsfbf2
; until the upstream merge at 1838cefa99c0: since upstream 5110131a330f the
; exact f32 -> bf16 step (an FP_ROUND marked exact, which turns the result's
; float32 copy back into bfloat) is a bitcast plus the high 16 bits, so only
; __extendbfsf2 is named (the expansion's rounding fptrunc feeds only the
; diagnosed __extendbfsf2 conversion and is dropped with it). Each is Phase
; 1's 'runtime helper '<name>' is not available for MSIL yet (phase 3)', once
; per helper and function, with no call built and no crash; the IL on stdout
; assembles.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not="{{^ +call}}" \
; DEFINE:   --implicit-check-not=__divti3 --implicit-check-not=__udivti3 --implicit-check-not=__modti3 \
; DEFINE:   --implicit-check-not=__umodti3 --implicit-check-not=__fixsfti --implicit-check-not=__fixdfti \
; DEFINE:   --implicit-check-not=__fixunssfti --implicit-check-not=__fixunsdfti \
; DEFINE:   --implicit-check-not=__floattisf --implicit-check-not=__floattidf \
; DEFINE:   --implicit-check-not=__floatuntisf --implicit-check-not=__floatuntidf --implicit-check-not=pinvokeimpl
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.lnx.o2.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/main.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.win.o2.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/main.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.lnx.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/main.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.win.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/main.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/main.lnx.o2.dll %t/main.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/main.win.o0.dll %t/main.win.o0.il %}

; DEFINE: %{diag} = FileCheck %s --check-prefix=HELPER --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/helpers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/helpers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/helpers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/helpers.ll \
; RUN:   > %t/helpers.win.il
; RUN: FileCheck %s --check-prefix=HELPER-IL --input-file=%t/helpers.win.il --implicit-check-not="{{^ +call}}" \
; RUN:   --implicit-check-not=pinvokeimpl
; RUN: %if ilasm %{ %ilasm-verify -output=%t/helpers.dll %t/helpers.win.il %}

;--- main.ll
source_filename = "i128-divrem.c"

; CHECK-LABEL: {{^}}.method public static void 'udiv128'(native int, native int, native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'udiv128'{{$}}
define i128 @udiv128(i128 %a, i128 %b) {
  %r = udiv i128 %a, %b
  ret i128 %r
}

; CHECK-LABEL: {{^}}.method public static void 'urem128'(native int, native int, native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'urem128'{{$}}
define i128 @urem128(i128 %a, i128 %b) {
  %r = urem i128 %a, %b
  ret i128 %r
}

; CHECK-LABEL: {{^}}.method public static void 'sdiv128'(native int, native int, native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'sdiv128'{{$}}
define i128 @sdiv128(i128 %a, i128 %b) {
  %r = sdiv i128 %a, %b
  ret i128 %r
}

; CHECK-LABEL: {{^}}.method public static void 'srem128'(native int, native int, native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'srem128'{{$}}
define i128 @srem128(i128 %a, i128 %b) {
  %r = srem i128 %a, %b
  ret i128 %r
}

; A 128-by-64-bit division whose quotient is truncated: still expanded.
; CHECK-LABEL: {{^}}.method public static native int 'udiv128_by_u64'(native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'udiv128_by_u64'{{$}}
define i64 @udiv128_by_u64(i64 %lo, i64 %hi, i64 %d) {
  %l = zext i64 %lo to i128
  %h = zext i64 %hi to i128
  %hs = shl i128 %h, 64
  %a = or i128 %l, %hs
  %b = zext i64 %d to i128
  %q = udiv i128 %a, %b
  %t = trunc i128 %q to i64
  ret i64 %t
}

; The eight plain conversions. Floating-point bits move through a frame slot
; (decision D16), so the conversions have a static frame.
; CHECK-LABEL: {{^}}.method public static void 'fptosi_f32_i128'(native int, float32) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'fptosi_f32_i128'{{$}}
define i128 @fptosi_f32_i128(float %x) {
  %r = fptosi float %x to i128
  ret i128 %r
}

; CHECK-LABEL: {{^}}.method public static void 'fptosi_f64_i128'(native int, float64) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'fptosi_f64_i128'{{$}}
define i128 @fptosi_f64_i128(double %x) {
  %r = fptosi double %x to i128
  ret i128 %r
}

; CHECK-LABEL: {{^}}.method public static void 'fptoui_f32_i128'(native int, float32) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'fptoui_f32_i128'{{$}}
define i128 @fptoui_f32_i128(float %x) {
  %r = fptoui float %x to i128
  ret i128 %r
}

; CHECK-LABEL: {{^}}.method public static void 'fptoui_f64_i128'(native int, float64) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'fptoui_f64_i128'{{$}}
define i128 @fptoui_f64_i128(double %x) {
  %r = fptoui double %x to i128
  ret i128 %r
}

; CHECK-LABEL: {{^}}.method public static float32 'sitofp_i128_f32'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'sitofp_i128_f32'{{$}}
define float @sitofp_i128_f32(i128 %x) {
  %r = sitofp i128 %x to float
  ret float %r
}

; CHECK-LABEL: {{^}}.method public static float64 'sitofp_i128_f64'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'sitofp_i128_f64'{{$}}
define double @sitofp_i128_f64(i128 %x) {
  %r = sitofp i128 %x to double
  ret double %r
}

; CHECK-LABEL: {{^}}.method public static float32 'uitofp_i128_f32'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'uitofp_i128_f32'{{$}}
define float @uitofp_i128_f32(i128 %x) {
  %r = uitofp i128 %x to float
  ret float %r
}

; CHECK-LABEL: {{^}}.method public static float64 'uitofp_i128_f64'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'uitofp_i128_f64'{{$}}
define double @uitofp_i128_f64(i128 %x) {
  %r = uitofp i128 %x to double
  ret double %r
}

; The saturating conversion Rust's 'as i128' emits.
; CHECK-LABEL: {{^}}.method public static void 'fptosi_sat_f64_i128'(native int, float64) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'fptosi_sat_f64_i128'{{$}}
define i128 @fptosi_sat_f64_i128(double %x) {
  %r = call i128 @llvm.fptosi.sat.i128.f64(double %x)
  ret i128 %r
}

; CHECK-LABEL: {{^}}.method public static int32 'after_i128'(int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'after_i128'{{$}}
; CHECK-NOT:   {{^}}.method
define i32 @after_i128(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}

declare i128 @llvm.fptosi.sat.i128.f64(double)

;--- helpers.ll
source_filename = "i128-helpers.c"

; HELPER: {{^}}error: {{.*}}in function fptosi_f64_strict {{.*}}: runtime helper '__fixdfti' is not available for MSIL yet (phase 3){{$}}
; HELPER: {{^}}error: {{.*}}in function fptoui_f32_strict {{.*}}: runtime helper '__fixunssfti' is not available for MSIL yet (phase 3){{$}}
; HELPER: {{^}}error: {{.*}}in function sitofp_f64_strict {{.*}}: runtime helper '__floattidf' is not available for MSIL yet (phase 3){{$}}
; HELPER: {{^}}error: {{.*}}in function uitofp_f32_strict {{.*}}: runtime helper '__floatuntisf' is not available for MSIL yet (phase 3){{$}}
; HELPER: {{^}}error: {{.*}}in function fptosi_f16 {{.*}}: runtime helper '__extendhfsf2' is not available for MSIL yet (phase 3){{$}}
; HELPER: {{^}}error: {{.*}}in function fptoui_f16 {{.*}}: runtime helper '__extendhfsf2' is not available for MSIL yet (phase 3){{$}}
; HELPER: {{^}}error: {{.*}}in function sitofp_f16 {{.*}}: runtime helper '__truncsfhf2' is not available for MSIL yet (phase 3){{$}}
; HELPER: {{^}}error: {{.*}}in function uitofp_f16 {{.*}}: runtime helper '__truncsfhf2' is not available for MSIL yet (phase 3){{$}}
; HELPER: {{^}}error: {{.*}}in function sitofp_bf16 {{.*}}: runtime helper '__extendbfsf2' is not available for MSIL yet (phase 3){{$}}
; HELPER: {{^}}error: {{.*}}in function uitofp_bf16 {{.*}}: runtime helper '__extendbfsf2' is not available for MSIL yet (phase 3){{$}}

; HELPER-IL-LABEL: {{^}}.method public static void 'fptosi_f64_strict'(native int, float64) cil managed{{$}}
; HELPER-IL-LABEL: {{^}}.method public static void 'fptoui_f32_strict'(native int, float32) cil managed{{$}}
; HELPER-IL-LABEL: {{^}}.method public static float64 'sitofp_f64_strict'(native int, native int) cil managed{{$}}
; HELPER-IL-LABEL: {{^}}.method public static float32 'uitofp_f32_strict'(native int, native int) cil managed{{$}}
; HELPER-IL-LABEL: {{^}}.method public static void 'fptosi_f16'(native int, int32) cil managed{{$}}
; HELPER-IL-LABEL: {{^}}.method public static void 'fptoui_f16'(native int, int32) cil managed{{$}}
; HELPER-IL-LABEL: {{^}}.method public static int32 'sitofp_f16'(native int, native int) cil managed{{$}}
; HELPER-IL-LABEL: {{^}}.method public static int32 'uitofp_f16'(native int, native int) cil managed{{$}}
; HELPER-IL-LABEL: {{^}}.method public static int32 'sitofp_bf16'(native int, native int) cil managed{{$}}
; HELPER-IL-LABEL: {{^}}.method public static int32 'uitofp_bf16'(native int, native int) cil managed{{$}}
; HELPER-IL-LABEL: {{^}}.method public static int32 'after_helpers'(int32, int32) cil managed{{$}}
; HELPER-IL-NEXT:  {{^\{$}}
; HELPER-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; HELPER-IL-NEXT:  {{^}}  ldarg.0{{$}}
; HELPER-IL-NEXT:  {{^}}  ldarg.1{{$}}
; HELPER-IL-NEXT:  {{^}}  add{{$}}
; HELPER-IL-NEXT:  {{^}}  ret{{$}}

define i128 @fptosi_f64_strict(double %x) strictfp {
  %r = call i128 @llvm.experimental.constrained.fptosi.i128.f64(double %x, metadata !"fpexcept.strict") strictfp
  ret i128 %r
}

define i128 @fptoui_f32_strict(float %x) strictfp {
  %r = call i128 @llvm.experimental.constrained.fptoui.i128.f32(float %x, metadata !"fpexcept.strict") strictfp
  ret i128 %r
}

define double @sitofp_f64_strict(i128 %x) strictfp {
  %r = call double @llvm.experimental.constrained.sitofp.f64.i128(i128 %x, metadata !"round.tonearest", metadata !"fpexcept.strict") strictfp
  ret double %r
}

define float @uitofp_f32_strict(i128 %x) strictfp {
  %r = call float @llvm.experimental.constrained.uitofp.f32.i128(i128 %x, metadata !"round.tonearest", metadata !"fpexcept.strict") strictfp
  ret float %r
}

; half starts and ends as its i16 bits (fp-expand.ll's bf16.ll).
define i128 @fptosi_f16(i16 %bits) {
  %x = bitcast i16 %bits to half
  %r = fptosi half %x to i128
  ret i128 %r
}

define i128 @fptoui_f16(i16 %bits) {
  %x = bitcast i16 %bits to half
  %r = fptoui half %x to i128
  ret i128 %r
}

define i16 @sitofp_f16(i128 %x) {
  %r = sitofp i128 %x to half
  %b = bitcast half %r to i16
  ret i16 %b
}

define i16 @uitofp_f16(i128 %x) {
  %r = uitofp i128 %x to half
  %b = bitcast half %r to i16
  ret i16 %b
}

; bfloat ends as its i16 bits too; fptosi and fptoui from bfloat expand
; inline (fp-expand.ll's i128.ll), these two do not.
define i16 @sitofp_bf16(i128 %x) {
  %r = sitofp i128 %x to bfloat
  %b = bitcast bfloat %r to i16
  ret i16 %b
}

define i16 @uitofp_bf16(i128 %x) {
  %r = uitofp i128 %x to bfloat
  %b = bitcast bfloat %r to i16
  ret i16 %b
}

define i32 @after_helpers(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

declare i128 @llvm.experimental.constrained.fptosi.i128.f64(double, metadata)
declare i128 @llvm.experimental.constrained.fptoui.i128.f32(float, metadata)
declare double @llvm.experimental.constrained.sitofp.f64.i128(i128, metadata, metadata)
declare float @llvm.experimental.constrained.uitofp.f32.i128(i128, metadata, metadata)
