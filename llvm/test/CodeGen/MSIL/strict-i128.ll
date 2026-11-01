; Constrained (strictfp) conversions between float32 or float64 and integers
; wider than 64 bits, scalar or vector (ImplementationPlan.md M24; plan
; 5.13.6, slot P0; plan section 12, M24 rows, among them 'M24.c (1) and plan
; 5.13.3's MSILPreExpand.{h,cpp} entry as built'). CIL has one rounding mode,
; to nearest, and raises no floating-point exception, so
; MSIL::relaxStrictInt128Conversions replaces each
; llvm.experimental.constrained.sitofp/uitofp whose integer operand, and each
; llvm.experimental.constrained.fptosi/fptoui whose integer result, is wider
; than 64 bits, scalar or vector, by the plain instruction before
; ExpandIRInsts runs, and ExpandIRInsts then expands it inline as it expands
; the plain conversion (i128-divrem.ll); 64 bits is the widest integer that
; instruction selection converts itself (M12). The cases: the four kinds with
; i128 and float32 or float64, and the other widths and a vector: i96 sitofp,
; i256 uitofp and fptoui, fptosi to i96, and <2 x i128> sitofp. No
; __floattisf, __floattidf, __floatuntisf, __floatuntidf, __fixsfti,
; __fixdfti, __fixunssfti or __fixunsdfti, nor any other __float or __fix
; name, is named, nothing is called and nothing is reported. Until M24 the
; constrained conversions were not expanded, and ReplaceNodeResults reported
; the i128 ones with Phase 1's runtime-helper error; since M25 it reports
; such a helper as "runtime helper '<name>' is not supported for MSIL: the
; helper archive clang_rt.builtins has no 128-bit integer helpers"
; (helper-archive-errors.ll). fp-expand.ll's i128-strict.ll has the
; conversions from half and bfloat.
;
; Each body is its plain twin's: strict.ll and plain.ll define the same
; thirteen functions under the same names and module name, so the
; whole-program text (llc -S), the object-form text (-msil-object-form) and
; the object (-filetype=obj) of strict.ll equal those of plain.ll byte for
; byte, on x86_64-unknown-linux-gnu-msil, x86_64-pc-windows-msvc-msil and
; aarch64-unknown-linux-gnu-msil at -O2 and -O0. strict.ll has the arguments
; round.tonearest and fpexcept.strict; dynamic.ll has round.dynamic, which
; clang emits under '#pragma STDC FENV_ACCESS ON', and maytrap.ll has
; fpexcept.maytrap (-ffp-exception-behavior=maytrap): the same output. The
; text has no call, no helper name and no P/Invoke, an object has no
; UNDEFINED symbol, and the text assembles.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{run} =
; DEFINE: %{tag} =
; DEFINE: %{check} = FileCheck %s --implicit-check-not="{{^ +call}}" --implicit-check-not=__floattisf \
; DEFINE:   --implicit-check-not=__floattidf --implicit-check-not=__floatuntisf --implicit-check-not=__floatuntidf \
; DEFINE:   --implicit-check-not=__fixsfti --implicit-check-not=__fixdfti --implicit-check-not=__fixunssfti \
; DEFINE:   --implicit-check-not=__fixunsdfti --implicit-check-not=__fix --implicit-check-not=__float \
; DEFINE:   --implicit-check-not=pinvokeimpl
; DEFINE: %{twins} = llc %{run} -asm-verbose=false -verify-machineinstrs < %t/strict.ll -o %t/strict.%{tag}.il \
; DEFINE:   2> %t/strict.%{tag}.err && count 0 < %t/strict.%{tag}.err && \
; DEFINE:   llc %{run} -asm-verbose=false -verify-machineinstrs < %t/plain.ll -o %t/plain.%{tag}.il && \
; DEFINE:   diff %t/strict.%{tag}.il %t/plain.%{tag}.il && %{check} --input-file=%t/strict.%{tag}.il && \
; DEFINE:   llc %{run} -asm-verbose=false -msil-object-form -verify-machineinstrs < %t/strict.ll \
; DEFINE:     -o %t/strict.%{tag}.of.il 2> %t/strict.%{tag}.of.err && count 0 < %t/strict.%{tag}.of.err && \
; DEFINE:   llc %{run} -asm-verbose=false -msil-object-form -verify-machineinstrs < %t/plain.ll -o %t/plain.%{tag}.of.il && \
; DEFINE:   diff %t/strict.%{tag}.of.il %t/plain.%{tag}.of.il && %{check} --input-file=%t/strict.%{tag}.of.il && \
; DEFINE:   llc %{run} -filetype=obj -verify-machineinstrs < %t/strict.ll -o %t/strict.%{tag}.o \
; DEFINE:     2> %t/strict.%{tag}.o.err && count 0 < %t/strict.%{tag}.o.err && \
; DEFINE:   llc %{run} -filetype=obj -verify-machineinstrs < %t/plain.ll -o %t/plain.%{tag}.o && \
; DEFINE:   cmp %t/strict.%{tag}.o %t/plain.%{tag}.o
; REDEFINE: %{run} = -mtriple=x86_64-unknown-linux-gnu-msil
; REDEFINE: %{tag} = lnx
; RUN: %{twins}
; REDEFINE: %{run} = -mtriple=x86_64-unknown-linux-gnu-msil -O0
; REDEFINE: %{tag} = lnx.o0
; RUN: %{twins}
; REDEFINE: %{run} = -mtriple=x86_64-pc-windows-msvc-msil
; REDEFINE: %{tag} = win
; RUN: %{twins}
; REDEFINE: %{run} = -mtriple=x86_64-pc-windows-msvc-msil -O0
; REDEFINE: %{tag} = win.o0
; RUN: %{twins}
; REDEFINE: %{run} = -mtriple=aarch64-unknown-linux-gnu-msil
; REDEFINE: %{tag} = a64
; RUN: %{twins}
; REDEFINE: %{run} = -mtriple=aarch64-unknown-linux-gnu-msil -O0
; REDEFINE: %{tag} = a64.o0
; RUN: %{twins}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/strict.lnx.dll %t/strict.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/strict.win.o0.dll %t/strict.win.o0.il %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/strict.lnx.o | FileCheck %s --check-prefix=LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/strict.a64.o0.o | FileCheck %s --check-prefix=LINK %}

; The other rounding and exception arguments.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/dynamic.ll \
; RUN:   -o %t/dynamic.lnx.il 2>&1 | count 0
; RUN: diff %t/dynamic.lnx.il %t/plain.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/dynamic.ll \
; RUN:   -o %t/dynamic.win.o0.il 2>&1 | count 0
; RUN: diff %t/dynamic.win.o0.il %t/plain.win.o0.il
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/dynamic.ll \
; RUN:   -o %t/dynamic.a64.o 2>&1 | count 0
; RUN: cmp %t/dynamic.a64.o %t/plain.a64.o
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/maytrap.ll \
; RUN:   -o %t/maytrap.win.il 2>&1 | count 0
; RUN: diff %t/maytrap.win.il %t/plain.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -msil-object-form -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/maytrap.ll -o %t/maytrap.lnx.o0.of.il 2>&1 | count 0
; RUN: diff %t/maytrap.lnx.o0.of.il %t/plain.lnx.o0.of.il
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -O0 -filetype=obj -verify-machineinstrs < %t/maytrap.ll \
; RUN:   -o %t/maytrap.a64.o0.o 2>&1 | count 0
; RUN: cmp %t/maytrap.a64.o0.o %t/plain.a64.o0.o

; An integer wider than 64 bits is passed as native int parts (i96 and i128
; two, i256 four, <2 x i128> two per lane) and returned through the hidden
; result pointer (M7), as a <2 x double> is, in both forms.
; CHECK-LABEL: {{^}}.method public static float32 'sitofp_i128_f32'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'sitofp_i128_f32'{{$}}
; CHECK-LABEL: {{^}}.method public static float64 'sitofp_i128_f64'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'sitofp_i128_f64'{{$}}
; CHECK-LABEL: {{^}}.method public static float32 'uitofp_i128_f32'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'uitofp_i128_f32'{{$}}
; CHECK-LABEL: {{^}}.method public static float64 'uitofp_i128_f64'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'uitofp_i128_f64'{{$}}
; CHECK-LABEL: {{^}}.method public static void 'fptosi_f32_i128'(native int, float32) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'fptosi_f32_i128'{{$}}
; CHECK-LABEL: {{^}}.method public static void 'fptosi_f64_i128'(native int, float64) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'fptosi_f64_i128'{{$}}
; CHECK-LABEL: {{^}}.method public static void 'fptoui_f32_i128'(native int, float32) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'fptoui_f32_i128'{{$}}
; CHECK-LABEL: {{^}}.method public static void 'fptoui_f64_i128'(native int, float64) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'fptoui_f64_i128'{{$}}
; CHECK-LABEL: {{^}}.method public static float32 'sitofp_i96_f32'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'sitofp_i96_f32'{{$}}
; CHECK-LABEL: {{^}}.method public static float32 'uitofp_i256_f32'(native int, native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'uitofp_i256_f32'{{$}}
; CHECK-LABEL: {{^}}.method public static void 'fptoui_f64_i256'(native int, float64) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'fptoui_f64_i256'{{$}}
; CHECK-LABEL: {{^}}.method public static void 'fptosi_f64_i96'(native int, float64) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'fptosi_f64_i96'{{$}}
; CHECK-LABEL: {{^}}.method public static void 'sitofp_v2i128_v2f64'(native int, native int, native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'sitofp_v2i128_v2f64'{{$}}
; CHECK-NOT:   {{^}}.method

; LINK:      {{^}}LinkInfo v1.0 triple={{.*}}
; LINK-NEXT: {{^}}Symbol 0 FUNCTION GLOBAL DEFAULT 'sitofp_i128_f32' method=0x06000001{{$}}
; LINK-NEXT: {{^}}Symbol 1 FUNCTION GLOBAL DEFAULT 'sitofp_i128_f64' method=0x06000002{{$}}
; LINK-NEXT: {{^}}Symbol 2 FUNCTION GLOBAL DEFAULT 'uitofp_i128_f32' method=0x06000003{{$}}
; LINK-NEXT: {{^}}Symbol 3 FUNCTION GLOBAL DEFAULT 'uitofp_i128_f64' method=0x06000004{{$}}
; LINK-NEXT: {{^}}Symbol 4 FUNCTION GLOBAL DEFAULT 'fptosi_f32_i128' method=0x06000005{{$}}
; LINK-NEXT: {{^}}Symbol 5 FUNCTION GLOBAL DEFAULT 'fptosi_f64_i128' method=0x06000006{{$}}
; LINK-NEXT: {{^}}Symbol 6 FUNCTION GLOBAL DEFAULT 'fptoui_f32_i128' method=0x06000007{{$}}
; LINK-NEXT: {{^}}Symbol 7 FUNCTION GLOBAL DEFAULT 'fptoui_f64_i128' method=0x06000008{{$}}
; LINK-NEXT: {{^}}Symbol 8 FUNCTION GLOBAL DEFAULT 'sitofp_i96_f32' method=0x06000009{{$}}
; LINK-NEXT: {{^}}Symbol 9 FUNCTION GLOBAL DEFAULT 'uitofp_i256_f32' method=0x0600000A{{$}}
; LINK-NEXT: {{^}}Symbol 10 FUNCTION GLOBAL DEFAULT 'fptoui_f64_i256' method=0x0600000B{{$}}
; LINK-NEXT: {{^}}Symbol 11 FUNCTION GLOBAL DEFAULT 'fptosi_f64_i96' method=0x0600000C{{$}}
; LINK-NEXT: {{^}}Symbol 12 FUNCTION GLOBAL DEFAULT 'sitofp_v2i128_v2f64' method=0x0600000D{{$}}
; LINK-NOT:  UNDEFINED

;--- strict.ll
; round.tonearest, fpexcept.strict.
source_filename = "strict-i128.c"

define float @sitofp_i128_f32(i128 %x) #0 {
  %r = call float @llvm.experimental.constrained.sitofp.f32.i128(i128 %x, metadata !"round.tonearest", metadata !"fpexcept.strict") #0
  ret float %r
}

define double @sitofp_i128_f64(i128 %x) #0 {
  %r = call double @llvm.experimental.constrained.sitofp.f64.i128(i128 %x, metadata !"round.tonearest", metadata !"fpexcept.strict") #0
  ret double %r
}

define float @uitofp_i128_f32(i128 %x) #0 {
  %r = call float @llvm.experimental.constrained.uitofp.f32.i128(i128 %x, metadata !"round.tonearest", metadata !"fpexcept.strict") #0
  ret float %r
}

define double @uitofp_i128_f64(i128 %x) #0 {
  %r = call double @llvm.experimental.constrained.uitofp.f64.i128(i128 %x, metadata !"round.tonearest", metadata !"fpexcept.strict") #0
  ret double %r
}

define i128 @fptosi_f32_i128(float %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptosi.i128.f32(float %x, metadata !"fpexcept.strict") #0
  ret i128 %r
}

define i128 @fptosi_f64_i128(double %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptosi.i128.f64(double %x, metadata !"fpexcept.strict") #0
  ret i128 %r
}

define i128 @fptoui_f32_i128(float %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptoui.i128.f32(float %x, metadata !"fpexcept.strict") #0
  ret i128 %r
}

define i128 @fptoui_f64_i128(double %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptoui.i128.f64(double %x, metadata !"fpexcept.strict") #0
  ret i128 %r
}

define float @sitofp_i96_f32(i96 %x) #0 {
  %r = call float @llvm.experimental.constrained.sitofp.f32.i96(i96 %x, metadata !"round.tonearest", metadata !"fpexcept.strict") #0
  ret float %r
}

define float @uitofp_i256_f32(i256 %x) #0 {
  %r = call float @llvm.experimental.constrained.uitofp.f32.i256(i256 %x, metadata !"round.tonearest", metadata !"fpexcept.strict") #0
  ret float %r
}

define i256 @fptoui_f64_i256(double %x) #0 {
  %r = call i256 @llvm.experimental.constrained.fptoui.i256.f64(double %x, metadata !"fpexcept.strict") #0
  ret i256 %r
}

define i96 @fptosi_f64_i96(double %x) #0 {
  %r = call i96 @llvm.experimental.constrained.fptosi.i96.f64(double %x, metadata !"fpexcept.strict") #0
  ret i96 %r
}

define <2 x double> @sitofp_v2i128_v2f64(<2 x i128> %x) #0 {
  %r = call <2 x double> @llvm.experimental.constrained.sitofp.v2f64.v2i128(<2 x i128> %x, metadata !"round.tonearest", metadata !"fpexcept.strict") #0
  ret <2 x double> %r
}

declare float @llvm.experimental.constrained.sitofp.f32.i128(i128, metadata, metadata)
declare double @llvm.experimental.constrained.sitofp.f64.i128(i128, metadata, metadata)
declare float @llvm.experimental.constrained.uitofp.f32.i128(i128, metadata, metadata)
declare double @llvm.experimental.constrained.uitofp.f64.i128(i128, metadata, metadata)
declare i128 @llvm.experimental.constrained.fptosi.i128.f32(float, metadata)
declare i128 @llvm.experimental.constrained.fptosi.i128.f64(double, metadata)
declare i128 @llvm.experimental.constrained.fptoui.i128.f32(float, metadata)
declare i128 @llvm.experimental.constrained.fptoui.i128.f64(double, metadata)
declare float @llvm.experimental.constrained.sitofp.f32.i96(i96, metadata, metadata)
declare float @llvm.experimental.constrained.uitofp.f32.i256(i256, metadata, metadata)
declare i256 @llvm.experimental.constrained.fptoui.i256.f64(double, metadata)
declare i96 @llvm.experimental.constrained.fptosi.i96.f64(double, metadata)
declare <2 x double> @llvm.experimental.constrained.sitofp.v2f64.v2i128(<2 x i128>, metadata, metadata)

attributes #0 = { strictfp }

;--- dynamic.ll
; round.dynamic, fpexcept.strict: clang under '#pragma STDC FENV_ACCESS ON'.
source_filename = "strict-i128.c"

define float @sitofp_i128_f32(i128 %x) #0 {
  %r = call float @llvm.experimental.constrained.sitofp.f32.i128(i128 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret float %r
}

define double @sitofp_i128_f64(i128 %x) #0 {
  %r = call double @llvm.experimental.constrained.sitofp.f64.i128(i128 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret double %r
}

define float @uitofp_i128_f32(i128 %x) #0 {
  %r = call float @llvm.experimental.constrained.uitofp.f32.i128(i128 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret float %r
}

define double @uitofp_i128_f64(i128 %x) #0 {
  %r = call double @llvm.experimental.constrained.uitofp.f64.i128(i128 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret double %r
}

define i128 @fptosi_f32_i128(float %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptosi.i128.f32(float %x, metadata !"fpexcept.strict") #0
  ret i128 %r
}

define i128 @fptosi_f64_i128(double %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptosi.i128.f64(double %x, metadata !"fpexcept.strict") #0
  ret i128 %r
}

define i128 @fptoui_f32_i128(float %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptoui.i128.f32(float %x, metadata !"fpexcept.strict") #0
  ret i128 %r
}

define i128 @fptoui_f64_i128(double %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptoui.i128.f64(double %x, metadata !"fpexcept.strict") #0
  ret i128 %r
}

define float @sitofp_i96_f32(i96 %x) #0 {
  %r = call float @llvm.experimental.constrained.sitofp.f32.i96(i96 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret float %r
}

define float @uitofp_i256_f32(i256 %x) #0 {
  %r = call float @llvm.experimental.constrained.uitofp.f32.i256(i256 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret float %r
}

define i256 @fptoui_f64_i256(double %x) #0 {
  %r = call i256 @llvm.experimental.constrained.fptoui.i256.f64(double %x, metadata !"fpexcept.strict") #0
  ret i256 %r
}

define i96 @fptosi_f64_i96(double %x) #0 {
  %r = call i96 @llvm.experimental.constrained.fptosi.i96.f64(double %x, metadata !"fpexcept.strict") #0
  ret i96 %r
}

define <2 x double> @sitofp_v2i128_v2f64(<2 x i128> %x) #0 {
  %r = call <2 x double> @llvm.experimental.constrained.sitofp.v2f64.v2i128(<2 x i128> %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret <2 x double> %r
}

declare float @llvm.experimental.constrained.sitofp.f32.i128(i128, metadata, metadata)
declare double @llvm.experimental.constrained.sitofp.f64.i128(i128, metadata, metadata)
declare float @llvm.experimental.constrained.uitofp.f32.i128(i128, metadata, metadata)
declare double @llvm.experimental.constrained.uitofp.f64.i128(i128, metadata, metadata)
declare i128 @llvm.experimental.constrained.fptosi.i128.f32(float, metadata)
declare i128 @llvm.experimental.constrained.fptosi.i128.f64(double, metadata)
declare i128 @llvm.experimental.constrained.fptoui.i128.f32(float, metadata)
declare i128 @llvm.experimental.constrained.fptoui.i128.f64(double, metadata)
declare float @llvm.experimental.constrained.sitofp.f32.i96(i96, metadata, metadata)
declare float @llvm.experimental.constrained.uitofp.f32.i256(i256, metadata, metadata)
declare i256 @llvm.experimental.constrained.fptoui.i256.f64(double, metadata)
declare i96 @llvm.experimental.constrained.fptosi.i96.f64(double, metadata)
declare <2 x double> @llvm.experimental.constrained.sitofp.v2f64.v2i128(<2 x i128>, metadata, metadata)

attributes #0 = { strictfp }

;--- maytrap.ll
; round.tonearest, fpexcept.maytrap.
source_filename = "strict-i128.c"

define float @sitofp_i128_f32(i128 %x) #0 {
  %r = call float @llvm.experimental.constrained.sitofp.f32.i128(i128 %x, metadata !"round.tonearest", metadata !"fpexcept.maytrap") #0
  ret float %r
}

define double @sitofp_i128_f64(i128 %x) #0 {
  %r = call double @llvm.experimental.constrained.sitofp.f64.i128(i128 %x, metadata !"round.tonearest", metadata !"fpexcept.maytrap") #0
  ret double %r
}

define float @uitofp_i128_f32(i128 %x) #0 {
  %r = call float @llvm.experimental.constrained.uitofp.f32.i128(i128 %x, metadata !"round.tonearest", metadata !"fpexcept.maytrap") #0
  ret float %r
}

define double @uitofp_i128_f64(i128 %x) #0 {
  %r = call double @llvm.experimental.constrained.uitofp.f64.i128(i128 %x, metadata !"round.tonearest", metadata !"fpexcept.maytrap") #0
  ret double %r
}

define i128 @fptosi_f32_i128(float %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptosi.i128.f32(float %x, metadata !"fpexcept.maytrap") #0
  ret i128 %r
}

define i128 @fptosi_f64_i128(double %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptosi.i128.f64(double %x, metadata !"fpexcept.maytrap") #0
  ret i128 %r
}

define i128 @fptoui_f32_i128(float %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptoui.i128.f32(float %x, metadata !"fpexcept.maytrap") #0
  ret i128 %r
}

define i128 @fptoui_f64_i128(double %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptoui.i128.f64(double %x, metadata !"fpexcept.maytrap") #0
  ret i128 %r
}

define float @sitofp_i96_f32(i96 %x) #0 {
  %r = call float @llvm.experimental.constrained.sitofp.f32.i96(i96 %x, metadata !"round.tonearest", metadata !"fpexcept.maytrap") #0
  ret float %r
}

define float @uitofp_i256_f32(i256 %x) #0 {
  %r = call float @llvm.experimental.constrained.uitofp.f32.i256(i256 %x, metadata !"round.tonearest", metadata !"fpexcept.maytrap") #0
  ret float %r
}

define i256 @fptoui_f64_i256(double %x) #0 {
  %r = call i256 @llvm.experimental.constrained.fptoui.i256.f64(double %x, metadata !"fpexcept.maytrap") #0
  ret i256 %r
}

define i96 @fptosi_f64_i96(double %x) #0 {
  %r = call i96 @llvm.experimental.constrained.fptosi.i96.f64(double %x, metadata !"fpexcept.maytrap") #0
  ret i96 %r
}

define <2 x double> @sitofp_v2i128_v2f64(<2 x i128> %x) #0 {
  %r = call <2 x double> @llvm.experimental.constrained.sitofp.v2f64.v2i128(<2 x i128> %x, metadata !"round.tonearest", metadata !"fpexcept.maytrap") #0
  ret <2 x double> %r
}

declare float @llvm.experimental.constrained.sitofp.f32.i128(i128, metadata, metadata)
declare double @llvm.experimental.constrained.sitofp.f64.i128(i128, metadata, metadata)
declare float @llvm.experimental.constrained.uitofp.f32.i128(i128, metadata, metadata)
declare double @llvm.experimental.constrained.uitofp.f64.i128(i128, metadata, metadata)
declare i128 @llvm.experimental.constrained.fptosi.i128.f32(float, metadata)
declare i128 @llvm.experimental.constrained.fptosi.i128.f64(double, metadata)
declare i128 @llvm.experimental.constrained.fptoui.i128.f32(float, metadata)
declare i128 @llvm.experimental.constrained.fptoui.i128.f64(double, metadata)
declare float @llvm.experimental.constrained.sitofp.f32.i96(i96, metadata, metadata)
declare float @llvm.experimental.constrained.uitofp.f32.i256(i256, metadata, metadata)
declare i256 @llvm.experimental.constrained.fptoui.i256.f64(double, metadata)
declare i96 @llvm.experimental.constrained.fptosi.i96.f64(double, metadata)
declare <2 x double> @llvm.experimental.constrained.sitofp.v2f64.v2i128(<2 x i128>, metadata, metadata)

attributes #0 = { strictfp }

;--- plain.ll
; The plain twins.
source_filename = "strict-i128.c"

define float @sitofp_i128_f32(i128 %x) {
  %r = sitofp i128 %x to float
  ret float %r
}

define double @sitofp_i128_f64(i128 %x) {
  %r = sitofp i128 %x to double
  ret double %r
}

define float @uitofp_i128_f32(i128 %x) {
  %r = uitofp i128 %x to float
  ret float %r
}

define double @uitofp_i128_f64(i128 %x) {
  %r = uitofp i128 %x to double
  ret double %r
}

define i128 @fptosi_f32_i128(float %x) {
  %r = fptosi float %x to i128
  ret i128 %r
}

define i128 @fptosi_f64_i128(double %x) {
  %r = fptosi double %x to i128
  ret i128 %r
}

define i128 @fptoui_f32_i128(float %x) {
  %r = fptoui float %x to i128
  ret i128 %r
}

define i128 @fptoui_f64_i128(double %x) {
  %r = fptoui double %x to i128
  ret i128 %r
}

define float @sitofp_i96_f32(i96 %x) {
  %r = sitofp i96 %x to float
  ret float %r
}

define float @uitofp_i256_f32(i256 %x) {
  %r = uitofp i256 %x to float
  ret float %r
}

define i256 @fptoui_f64_i256(double %x) {
  %r = fptoui double %x to i256
  ret i256 %r
}

define i96 @fptosi_f64_i96(double %x) {
  %r = fptosi double %x to i96
  ret i96 %r
}

define <2 x double> @sitofp_v2i128_v2f64(<2 x i128> %x) {
  %r = sitofp <2 x i128> %x to <2 x double>
  ret <2 x double> %r
}
