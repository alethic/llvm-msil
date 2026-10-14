; Floating-point operations that LLVM turns into library calls, and
; floating-point features that CIL does not have, end in a clean diagnostic
; (until M8 for the library calls; ImplementationPlan.md M5.a, M5.c and
; section 2 'Diagnostics leave well-formed output'; decisions D14 and D16;
; plan section 12, rows 'M5.a extended ...', 'M2.a item (5) extended ...' and
; 'fix-loop ownership: fp-expand.ll -> M8.d'):
; - fma, sqrt, floor, ceil, trunc, rint, nearbyint, round, minnum and maxnum
;   (C fmin and fmax), sin, cos, tan, pow, exp, exp2, log, log2, log10, the
;   inverse and hyperbolic functions, atan2, ldexp and powi are Expand (or
;   LibCall): the legalizer emits a call of the C function (the 'f' variant
;   for float32), and LowerCall reports "runtime library call '<name>' is not
;   supported for MSIL yet" and builds no call (M8 makes the D14 math
;   functions P/Invokes and changes this text; M8.d owns this file then).
; - half (f16) values are promoted, and the conversions between half and
;   float32 or float64 are the compiler-rt calls __extendhfsf2, __truncsfhf2
;   and __truncdfhf2, diagnosed the same way (f16 is unsupported in Phase 1).
; - Library functions with more than one result (frexp, modf, sincos, and a
;   sin and a cos of the same value, which the legalizer may combine into
;   sincos) return the extra results through memory; they are diagnosed like
;   the others and must not reach the frame-index stub of M7.
; - The MSVC C runtime has no frexpf or ldexpf: on x86_64-pc-windows-msvc-msil
;   float32 frexp and ldexp are the double functions 'frexp' and 'ldexp' (ldexp
;   through fpext and fptrunc), on x86_64-unknown-linux-gnu-msil 'frexpf' and
;   'ldexpf', and modf is 'modff' on both (row 'M5.a extended (float32 frexp
;   and ldexp on windows-msvc)'); never '?' and never a frame index.
; - fptosi and fptoui from float32 and float64 to i128, plain, constrained
;   and saturating, are the compiler-rt calls __fixsfti, __fixdfti,
;   __fixunssfti and __fixunsdfti; their two-value results would be demoted
;   to a stack slot, so ReplaceNodeResults reports them with the same text
;   and returns UNDEF (row 'M5.a extended'). From half and bfloat, plain and
;   constrained, they name the float32 functions __fixsfti and __fixunssfti,
;   which the legalizer calls after it extends the operand to float32; never
;   '?' and never __fixhfti (row "Row 'M5.a extended' (floating point to
;   i128) amended").
; - CIL rounds only to nearest (row 'M5.a extended (constrained floating
;   point and rounding mode)'): llvm.set.rounding, and llvm.fptrunc.round with
;   any other mode than round.tonearest, report "changing the floating-point
;   rounding mode is not supported for MSIL"; llvm.fptrunc.round with
;   round.tonearest is a plain conv.r4, and llvm.get.rounding is 1. On a half,
;   bfloat or vector result (row 'M5.a extended (llvm.fptrunc.round on f16,
;   bf16 and vector types)') llvm.fptrunc.round behaves the same, and
;   round.tonearest legalizes like the plain fptrunc: one conv.r4 per element
;   of a float32 vector, and one diagnosed __truncsfhf2 or __truncsfbf2 per
;   half or bfloat element.
; - bfloat values are promoted to float32 like half, and the conversions are
;   the compiler-rt calls __extendbfsf2, __truncsfbf2 and __truncdfbf2 that
;   LowerBF16Conversion builds instead of the generic integer expansions
;   through a stack slot; LowerCall diagnoses them (same row). A truncation
;   only stays alive when its result is used, here through 'bitcast bfloat to
;   i16'; an extension of a bfloat argument is a no-op (the argument is
;   already a float32), so the extensions start from 'bitcast i16 to bfloat'.
; - Constrained operations (strictfp) on half and bfloat end exactly as their
;   plain forms do (row 'M5.a extended (constrained operations on half and
;   bfloat)'): fpext, fptrunc, fadd, fcmp, fcmps, sitofp and fptosi report the
;   compiler-rt conversion of the plain form, never 'LLVM ERROR' (the type
;   legalizer has no soft promotion for them, and STRICT_FP16_TO_FP and the
;   other strict conversions it builds have no instruction).
; - fp128, x86_fp80 and ppc_fp128 have no CIL type (their operations become
;   compiler-rt calls with results in memory, and their values are passed in
;   memory), and llvm.get.fpenv, llvm.set.fpenv, llvm.get.fpmode and
;   llvm.set.fpmode pass the environment through a stack temporary:
;   MSILPrepareIR reports "floating-point type '<type>' is not supported for
;   MSIL" or "floating-point environment access ('<intrinsic>') is not
;   supported for MSIL" once per definition that uses them (in its signature
;   or in any instruction, as a result, operand or allocated type, also as
;   the element of a vector, array or struct type) and deletes its body (row
;   'M2.a item (5) extended (unsupported floating-point types and environment
;   intrinsics)').
; Each diagnostic names its function. Compilation goes on after it (later
; functions still compile, the 'after_*' functions check their bodies), llc
; exits with 1 and prints neither 'LLVM ERROR' nor a crash report, no call of
; the library function is printed, and the IL on stdout still assembles. Where
; one function has exactly one library call or one unsupported type, every
; error line is accounted for (no other error or warning, in this order); for
; f16, bfloat (plain and constrained) and the multi-result functions each
; function needs at least one error naming one of its library calls, since
; how many of the calls are diagnosed depends on the legalizer.
; Every part runs on both triples at -O2 and at least once at -O0. fabs,
; copysign and the bitcasts between integers and floating point go through
; memory and are tested in M7.c (plan 5.5).

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; The %{names} checks are CHECK-DAG groups over the errors in %t/names.err,
; and implicit negative checks only look before the first match of such a
; group, not between or after its matches. So a second FileCheck run over the
; same file (%{nameban}), whose only directive is the first error that names
; a library function, bans an error naming '?', any warning and the crash
; markers everywhere in it; each group also ends with explicit NOT lines.
; DEFINE: %{names} = FileCheck %s --input-file=%t/names.err --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=warning:
; DEFINE: %{nameban} = FileCheck %s --input-file=%t/names.err --check-prefix=NAMEBAN \
; DEFINE:   --implicit-check-not="call '?'" --implicit-check-not=warning: \
; DEFINE:   --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; NAMEBAN: {{^}}error: {{.*}}: runtime library call '{{[_a-z0-9]+}}' is not supported for MSIL yet{{$}}
; RUN: split-file %s %t

; Math library functions, float32 and float64.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/libm32.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefixes=LIBM32,LIBM32-LNX
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/libm32.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefixes=LIBM32,LIBM32-WIN
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/libm32.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefixes=LIBM32,LIBM32-LNX
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/libm32.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefixes=LIBM32,LIBM32-WIN
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/libm32.ll \
; RUN:   > %t/libm32.lnx.il
; RUN: FileCheck %s --check-prefix=LIBM32-IL --implicit-check-not=call --input-file=%t/libm32.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/libm32.ll \
; RUN:   > %t/libm32.win.il
; RUN: FileCheck %s --check-prefix=LIBM32-IL --implicit-check-not=call --input-file=%t/libm32.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/libm32.dll %t/libm32.win.il %}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/libm64.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LIBM64
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/libm64.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LIBM64
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/libm64.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LIBM64
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/libm64.ll \
; RUN:   > %t/libm64.lnx.il
; RUN: FileCheck %s --check-prefix=LIBM64-IL --implicit-check-not=call --input-file=%t/libm64.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/libm64.ll \
; RUN:   > %t/libm64.win.il
; RUN: FileCheck %s --check-prefix=LIBM64-IL --implicit-check-not=call --input-file=%t/libm64.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/libm64.dll %t/libm64.lnx.il %}

; half.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/f16.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefix=F16
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/f16.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefix=F16
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/f16.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefix=F16
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/f16.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefix=F16
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/f16.ll \
; RUN:   > %t/f16.lnx.il
; RUN: FileCheck %s --check-prefix=F16-IL --implicit-check-not=call --input-file=%t/f16.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/f16.ll \
; RUN:   > %t/f16.win.il
; RUN: FileCheck %s --check-prefix=F16-IL --implicit-check-not=call --input-file=%t/f16.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/f16.dll %t/f16.win.il %}

; Functions with two results.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/multi.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefixes=MULTI,MULTI-LNX --implicit-check-not="call '?'"
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/multi.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefixes=MULTI,MULTI-WIN --implicit-check-not="call '?'"
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/multi.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefixes=MULTI,MULTI-LNX --implicit-check-not="call '?'"
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/multi.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefixes=MULTI,MULTI-WIN --implicit-check-not="call '?'"
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/multi.ll \
; RUN:   > %t/multi.lnx.il
; RUN: FileCheck %s --check-prefix=MULTI-IL --implicit-check-not=call --input-file=%t/multi.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/multi.ll \
; RUN:   > %t/multi.win.il
; RUN: FileCheck %s --check-prefix=MULTI-IL --implicit-check-not=call --input-file=%t/multi.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/multi.dll %t/multi.lnx.il %}

; Floating point to i128: plain, constrained and saturating.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/i128.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=I128
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/i128.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=I128
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/i128.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=I128
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/i128.ll \
; RUN:   > %t/i128.lnx.il
; RUN: FileCheck %s --check-prefix=I128-IL --implicit-check-not=__fix --input-file=%t/i128.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/i128.ll \
; RUN:   > %t/i128.win.il
; RUN: FileCheck %s --check-prefix=I128-IL --implicit-check-not=__fix --input-file=%t/i128.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/i128.dll %t/i128.win.il %}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/i128-strict.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=I128S
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/i128-strict.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=I128S
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/i128-strict.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=I128S
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/i128-strict.ll \
; RUN:   > %t/i128-strict.lnx.il
; RUN: FileCheck %s --check-prefix=I128S-IL --implicit-check-not=__fix --input-file=%t/i128-strict.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/i128-strict.ll \
; RUN:   > %t/i128-strict.win.il
; RUN: FileCheck %s --check-prefix=I128S-IL --implicit-check-not=__fix --input-file=%t/i128-strict.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/i128-strict.dll %t/i128-strict.lnx.il %}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/i128-sat.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=I128SAT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/i128-sat.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=I128SAT
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/i128-sat.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=I128SAT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/i128-sat.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=I128SAT
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/i128-sat.ll \
; RUN:   > %t/i128-sat.lnx.il
; RUN: FileCheck %s --check-prefix=I128SAT-IL --implicit-check-not=__fix --input-file=%t/i128-sat.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/i128-sat.ll \
; RUN:   > %t/i128-sat.win.il
; RUN: FileCheck %s --check-prefix=I128SAT-IL --implicit-check-not=__fix --input-file=%t/i128-sat.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/i128-sat.dll %t/i128-sat.win.il %}

; Rounding modes.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/rounding.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ROUND
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/rounding.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ROUND
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/rounding.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ROUND
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/rounding.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ROUND
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/rounding.ll \
; RUN:   > %t/rounding.lnx.il
; RUN: FileCheck %s --check-prefixes=ROUND-IL,ROUND-IL-O2 --implicit-check-not=call --input-file=%t/rounding.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/rounding.ll \
; RUN:   > %t/rounding.win.o0.il
; RUN: FileCheck %s --check-prefixes=ROUND-IL,ROUND-IL-O0 --implicit-check-not=call --input-file=%t/rounding.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/rounding.dll %t/rounding.lnx.il %}

; bfloat.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/bf16.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefix=BF16
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/bf16.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefix=BF16
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/bf16.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefix=BF16
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/bf16.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefix=BF16
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/bf16.ll \
; RUN:   > %t/bf16.lnx.il
; RUN: FileCheck %s --check-prefix=BF16-IL --implicit-check-not=call --input-file=%t/bf16.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/bf16.ll \
; RUN:   > %t/bf16.win.il
; RUN: FileCheck %s --check-prefix=BF16-IL --implicit-check-not=call --input-file=%t/bf16.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/bf16.dll %t/bf16.win.il %}

; Constrained operations on half and bfloat.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/strict16.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefix=STRICT16
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/strict16.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefix=STRICT16
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/strict16.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefix=STRICT16
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/strict16.ll -o /dev/null \
; RUN:   2> %t/names.err
; RUN: %{names} --check-prefix=STRICT16
; RUN: %{nameban}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/strict16.ll \
; RUN:   > %t/strict16.lnx.il
; RUN: FileCheck %s --check-prefix=STRICT16-IL --implicit-check-not=call --input-file=%t/strict16.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/strict16.ll \
; RUN:   > %t/strict16.win.il
; RUN: FileCheck %s --check-prefix=STRICT16-IL --implicit-check-not=call --input-file=%t/strict16.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/strict16.dll %t/strict16.lnx.il %}

; fp128, x86_fp80, ppc_fp128 and the floating-point environment.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/fptypes.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=FPTY
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/fptypes.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=FPTY
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/fptypes.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=FPTY
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/fptypes.ll \
; RUN:   > %t/fptypes.lnx.il
; RUN: FileCheck %s --check-prefix=FPTY-IL --implicit-check-not=call --implicit-check-not=fpenv \
; RUN:   --implicit-check-not=fpmode --input-file=%t/fptypes.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/fptypes.ll \
; RUN:   > %t/fptypes.win.il
; RUN: FileCheck %s --check-prefix=FPTY-IL --implicit-check-not=call --implicit-check-not=fpenv \
; RUN:   --implicit-check-not=fpmode --input-file=%t/fptypes.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/fptypes.dll %t/fptypes.win.il %}

;--- libm32.ll
; float32: the C functions with the f suffix.
source_filename = "fp-expand-libm32.c"

; LIBM32:   error: {{.*}}in function fma_f32 {{.*}}: runtime library call 'fmaf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function sqrt_f32 {{.*}}: runtime library call 'sqrtf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function floor_f32 {{.*}}: runtime library call 'floorf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function ceil_f32 {{.*}}: runtime library call 'ceilf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function trunc_f32 {{.*}}: runtime library call 'truncf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function rint_f32 {{.*}}: runtime library call 'rintf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function nearbyint_f32 {{.*}}: runtime library call 'nearbyintf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function round_f32 {{.*}}: runtime library call 'roundf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function fmin_f32 {{.*}}: runtime library call 'fminf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function fmax_f32 {{.*}}: runtime library call 'fmaxf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function sin_f32 {{.*}}: runtime library call 'sinf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function cos_f32 {{.*}}: runtime library call 'cosf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function tan_f32 {{.*}}: runtime library call 'tanf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function pow_f32 {{.*}}: runtime library call 'powf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function exp_f32 {{.*}}: runtime library call 'expf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function exp2_f32 {{.*}}: runtime library call 'exp2f' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function log_f32 {{.*}}: runtime library call 'logf' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function log2_f32 {{.*}}: runtime library call 'log2f' is not supported for MSIL yet
; LIBM32:   error: {{.*}}in function log10_f32 {{.*}}: runtime library call 'log10f' is not supported for MSIL yet
; LIBM32-LNX: error: {{.*}}in function ldexp_f32 {{.*}}: runtime library call 'ldexpf' is not supported for MSIL yet
; LIBM32-WIN: error: {{.*}}in function ldexp_f32 {{.*}}: runtime library call 'ldexp' is not supported for MSIL yet

; LIBM32-IL-LABEL: .method {{.*}}'after_libm32'(
; LIBM32-IL-NEXT:  {{^\{$}}
; LIBM32-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; LIBM32-IL-NEXT:  {{^}}  ldarg.0{{$}}
; LIBM32-IL-NEXT:  {{^}}  ldarg.1{{$}}
; LIBM32-IL-NEXT:  {{^}}  sub{{$}}
; LIBM32-IL-NEXT:  {{^}}  ret{{$}}
; LIBM32-IL-NEXT:  {{^}}} // end of method 'after_libm32'{{$}}

define float @fma_f32(float %a, float %b, float %c) {
  %r = call float @llvm.fma.f32(float %a, float %b, float %c)
  ret float %r
}

define float @sqrt_f32(float %a) {
  %r = call float @llvm.sqrt.f32(float %a)
  ret float %r
}

define float @floor_f32(float %a) {
  %r = call float @llvm.floor.f32(float %a)
  ret float %r
}

define float @ceil_f32(float %a) {
  %r = call float @llvm.ceil.f32(float %a)
  ret float %r
}

define float @trunc_f32(float %a) {
  %r = call float @llvm.trunc.f32(float %a)
  ret float %r
}

define float @rint_f32(float %a) {
  %r = call float @llvm.rint.f32(float %a)
  ret float %r
}

define float @nearbyint_f32(float %a) {
  %r = call float @llvm.nearbyint.f32(float %a)
  ret float %r
}

define float @round_f32(float %a) {
  %r = call float @llvm.round.f32(float %a)
  ret float %r
}

define float @fmin_f32(float %a, float %b) {
  %r = call float @llvm.minnum.f32(float %a, float %b)
  ret float %r
}

define float @fmax_f32(float %a, float %b) {
  %r = call float @llvm.maxnum.f32(float %a, float %b)
  ret float %r
}

define float @sin_f32(float %a) {
  %r = call float @llvm.sin.f32(float %a)
  ret float %r
}

define float @cos_f32(float %a) {
  %r = call float @llvm.cos.f32(float %a)
  ret float %r
}

define float @tan_f32(float %a) {
  %r = call float @llvm.tan.f32(float %a)
  ret float %r
}

define float @pow_f32(float %a, float %b) {
  %r = call float @llvm.pow.f32(float %a, float %b)
  ret float %r
}

define float @exp_f32(float %a) {
  %r = call float @llvm.exp.f32(float %a)
  ret float %r
}

define float @exp2_f32(float %a) {
  %r = call float @llvm.exp2.f32(float %a)
  ret float %r
}

define float @log_f32(float %a) {
  %r = call float @llvm.log.f32(float %a)
  ret float %r
}

define float @log2_f32(float %a) {
  %r = call float @llvm.log2.f32(float %a)
  ret float %r
}

define float @log10_f32(float %a) {
  %r = call float @llvm.log10.f32(float %a)
  ret float %r
}

; ldexpf on GNU; windows-msvc has no ldexpf, so ldexp on the widened value.
define float @ldexp_f32(float %a, i32 %e) {
  %r = call float @llvm.ldexp.f32.i32(float %a, i32 %e)
  ret float %r
}

define float @after_libm32(float %a, float %b) {
  %r = fsub float %a, %b
  ret float %r
}

declare float @llvm.ceil.f32(float)
declare float @llvm.cos.f32(float)
declare float @llvm.exp.f32(float)
declare float @llvm.exp2.f32(float)
declare float @llvm.floor.f32(float)
declare float @llvm.fma.f32(float, float, float)
declare float @llvm.ldexp.f32.i32(float, i32)
declare float @llvm.log.f32(float)
declare float @llvm.log10.f32(float)
declare float @llvm.log2.f32(float)
declare float @llvm.maxnum.f32(float, float)
declare float @llvm.minnum.f32(float, float)
declare float @llvm.nearbyint.f32(float)
declare float @llvm.pow.f32(float, float)
declare float @llvm.rint.f32(float)
declare float @llvm.round.f32(float)
declare float @llvm.sin.f32(float)
declare float @llvm.sqrt.f32(float)
declare float @llvm.tan.f32(float)
declare float @llvm.trunc.f32(float)

;--- libm64.ll
; float64; ldexp keeps its int32 exponent, and powi becomes the
; compiler-rt helper __powidf2, or pow of the converted exponent where
; the C runtime has no __powidf2 (windows-msvc).
source_filename = "fp-expand-libm64.c"

; LIBM64:   error: {{.*}}in function fma_f64 {{.*}}: runtime library call 'fma' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function sqrt_f64 {{.*}}: runtime library call 'sqrt' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function floor_f64 {{.*}}: runtime library call 'floor' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function ceil_f64 {{.*}}: runtime library call 'ceil' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function trunc_f64 {{.*}}: runtime library call 'trunc' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function rint_f64 {{.*}}: runtime library call 'rint' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function nearbyint_f64 {{.*}}: runtime library call 'nearbyint' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function round_f64 {{.*}}: runtime library call 'round' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function fmin_f64 {{.*}}: runtime library call 'fmin' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function fmax_f64 {{.*}}: runtime library call 'fmax' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function sin_f64 {{.*}}: runtime library call 'sin' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function cos_f64 {{.*}}: runtime library call 'cos' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function tan_f64 {{.*}}: runtime library call 'tan' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function pow_f64 {{.*}}: runtime library call 'pow' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function exp_f64 {{.*}}: runtime library call 'exp' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function exp2_f64 {{.*}}: runtime library call 'exp2' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function log_f64 {{.*}}: runtime library call 'log' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function log2_f64 {{.*}}: runtime library call 'log2' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function log10_f64 {{.*}}: runtime library call 'log10' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function asin_f64 {{.*}}: runtime library call 'asin' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function acos_f64 {{.*}}: runtime library call 'acos' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function atan_f64 {{.*}}: runtime library call 'atan' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function atan2_f64 {{.*}}: runtime library call 'atan2' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function sinh_f64 {{.*}}: runtime library call 'sinh' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function cosh_f64 {{.*}}: runtime library call 'cosh' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function tanh_f64 {{.*}}: runtime library call 'tanh' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function ldexp_f64 {{.*}}: runtime library call 'ldexp' is not supported for MSIL yet
; LIBM64:   error: {{.*}}in function powi_f64 {{.*}}: runtime library call '{{__powidf2|pow}}' is not supported for MSIL yet

; LIBM64-IL-LABEL: .method {{.*}}'after_libm64'(
; LIBM64-IL-NEXT:  {{^\{$}}
; LIBM64-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; LIBM64-IL-NEXT:  {{^}}  ldarg.0{{$}}
; LIBM64-IL-NEXT:  {{^}}  ldarg.1{{$}}
; LIBM64-IL-NEXT:  {{^}}  sub{{$}}
; LIBM64-IL-NEXT:  {{^}}  ret{{$}}
; LIBM64-IL-NEXT:  {{^}}} // end of method 'after_libm64'{{$}}

define double @fma_f64(double %a, double %b, double %c) {
  %r = call double @llvm.fma.f64(double %a, double %b, double %c)
  ret double %r
}

define double @sqrt_f64(double %a) {
  %r = call double @llvm.sqrt.f64(double %a)
  ret double %r
}

define double @floor_f64(double %a) {
  %r = call double @llvm.floor.f64(double %a)
  ret double %r
}

define double @ceil_f64(double %a) {
  %r = call double @llvm.ceil.f64(double %a)
  ret double %r
}

define double @trunc_f64(double %a) {
  %r = call double @llvm.trunc.f64(double %a)
  ret double %r
}

define double @rint_f64(double %a) {
  %r = call double @llvm.rint.f64(double %a)
  ret double %r
}

define double @nearbyint_f64(double %a) {
  %r = call double @llvm.nearbyint.f64(double %a)
  ret double %r
}

define double @round_f64(double %a) {
  %r = call double @llvm.round.f64(double %a)
  ret double %r
}

define double @fmin_f64(double %a, double %b) {
  %r = call double @llvm.minnum.f64(double %a, double %b)
  ret double %r
}

define double @fmax_f64(double %a, double %b) {
  %r = call double @llvm.maxnum.f64(double %a, double %b)
  ret double %r
}

define double @sin_f64(double %a) {
  %r = call double @llvm.sin.f64(double %a)
  ret double %r
}

define double @cos_f64(double %a) {
  %r = call double @llvm.cos.f64(double %a)
  ret double %r
}

define double @tan_f64(double %a) {
  %r = call double @llvm.tan.f64(double %a)
  ret double %r
}

define double @pow_f64(double %a, double %b) {
  %r = call double @llvm.pow.f64(double %a, double %b)
  ret double %r
}

define double @exp_f64(double %a) {
  %r = call double @llvm.exp.f64(double %a)
  ret double %r
}

define double @exp2_f64(double %a) {
  %r = call double @llvm.exp2.f64(double %a)
  ret double %r
}

define double @log_f64(double %a) {
  %r = call double @llvm.log.f64(double %a)
  ret double %r
}

define double @log2_f64(double %a) {
  %r = call double @llvm.log2.f64(double %a)
  ret double %r
}

define double @log10_f64(double %a) {
  %r = call double @llvm.log10.f64(double %a)
  ret double %r
}

define double @asin_f64(double %a) {
  %r = call double @llvm.asin.f64(double %a)
  ret double %r
}

define double @acos_f64(double %a) {
  %r = call double @llvm.acos.f64(double %a)
  ret double %r
}

define double @atan_f64(double %a) {
  %r = call double @llvm.atan.f64(double %a)
  ret double %r
}

define double @atan2_f64(double %a, double %b) {
  %r = call double @llvm.atan2.f64(double %a, double %b)
  ret double %r
}

define double @sinh_f64(double %a) {
  %r = call double @llvm.sinh.f64(double %a)
  ret double %r
}

define double @cosh_f64(double %a) {
  %r = call double @llvm.cosh.f64(double %a)
  ret double %r
}

define double @tanh_f64(double %a) {
  %r = call double @llvm.tanh.f64(double %a)
  ret double %r
}

define double @ldexp_f64(double %a, i32 %e) {
  %r = call double @llvm.ldexp.f64.i32(double %a, i32 %e)
  ret double %r
}

define double @powi_f64(double %a, i32 %n) {
  %r = call double @llvm.powi.f64.i32(double %a, i32 %n)
  ret double %r
}

define double @after_libm64(double %a, double %b) {
  %r = fsub double %a, %b
  ret double %r
}

declare double @llvm.acos.f64(double)
declare double @llvm.asin.f64(double)
declare double @llvm.atan.f64(double)
declare double @llvm.atan2.f64(double, double)
declare double @llvm.ceil.f64(double)
declare double @llvm.cos.f64(double)
declare double @llvm.cosh.f64(double)
declare double @llvm.exp.f64(double)
declare double @llvm.exp2.f64(double)
declare double @llvm.floor.f64(double)
declare double @llvm.fma.f64(double, double, double)
declare double @llvm.ldexp.f64.i32(double, i32)
declare double @llvm.log.f64(double)
declare double @llvm.log10.f64(double)
declare double @llvm.log2.f64(double)
declare double @llvm.maxnum.f64(double, double)
declare double @llvm.minnum.f64(double, double)
declare double @llvm.nearbyint.f64(double)
declare double @llvm.pow.f64(double, double)
declare double @llvm.powi.f64.i32(double, i32)
declare double @llvm.rint.f64(double)
declare double @llvm.round.f64(double)
declare double @llvm.sin.f64(double)
declare double @llvm.sinh.f64(double)
declare double @llvm.sqrt.f64(double)
declare double @llvm.tan.f64(double)
declare double @llvm.tanh.f64(double)
declare double @llvm.trunc.f64(double)

;--- f16.ll
; half values: every conversion between half and float32 or float64 is a
; compiler-rt call, and half arithmetic is done in float32 between such
; conversions. LowerCall diagnoses a call and builds none, and the legalizer
; handles the users of a node before the node and deletes nodes left without
; uses, so a conversion whose only user is a diagnosed call is dropped without
; a diagnostic of its own (f16_round_trip reports __extendhfsf2 but not the
; __truncsfhf2 that feeds it). Each function therefore has at least one
; diagnostic, and the half conversion it names is matched by a pattern.
source_filename = "fp-expand-f16.c"

; F16-DAG: error: {{.*}}in function f16_round_trip {{.*}}: runtime library call '{{__(extendhf[sd]f|trunc[sd]fhf)2}}' is not supported for MSIL yet
; F16-DAG: error: {{.*}}in function f16_add {{.*}}: runtime library call '{{__(extendhf[sd]f|trunc[sd]fhf)2}}' is not supported for MSIL yet
; F16-DAG: error: {{.*}}in function f16_from_f64 {{.*}}: runtime library call '{{__(extendhf[sd]f|trunc[sd]fhf)2}}' is not supported for MSIL yet
; F16-DAG: error: {{.*}}in function f16_mul_args {{.*}}: runtime library call '{{__(extendhf[sd]f|trunc[sd]fhf)2}}' is not supported for MSIL yet
; F16-DAG: error: {{.*}}in function f16_compare {{.*}}: runtime library call '{{__(extendhf[sd]f|trunc[sd]fhf)2}}' is not supported for MSIL yet
; F16-NOT: error: {{.*}}in function after_f16
; F16-NOT: LLVM ERROR
; F16-NOT: PLEASE submit

; F16-IL-LABEL: .method {{.*}}'after_f16'(
; F16-IL-NEXT:  {{^\{$}}
; F16-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; F16-IL-NEXT:  {{^}}  ldarg.0{{$}}
; F16-IL-NEXT:  {{^}}  ldarg.1{{$}}
; F16-IL-NEXT:  {{^}}  mul{{$}}
; F16-IL-NEXT:  {{^}}  ret{{$}}
; F16-IL-NEXT:  {{^}}} // end of method 'after_f16'{{$}}

define float @f16_round_trip(float %a) {
  %h = fptrunc float %a to half
  %r = fpext half %h to float
  ret float %r
}

define float @f16_add(float %a, float %b) {
  %x = fptrunc float %a to half
  %y = fptrunc float %b to half
  %s = fadd half %x, %y
  %r = fpext half %s to float
  ret float %r
}

define double @f16_from_f64(double %a) {
  %h = fptrunc double %a to half
  %r = fpext half %h to double
  ret double %r
}

define half @f16_mul_args(half %a, half %b) {
  %m = fmul half %a, %b
  ret half %m
}

define i32 @f16_compare(half %a, half %b) {
  %c = fcmp olt half %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define float @after_f16(float %a, float %b) {
  %r = fmul float %a, %b
  ret float %r
}

;--- multi.ll
; Library functions with two results: the second result comes back through
; memory, so these must be diagnosed without reaching a frame index.
source_filename = "fp-expand-multi.c"

; MULTI-DAG: error: {{.*}}in function sin_and_cos_f64 {{.*}}: runtime library call '{{sin|cos|sincos}}' is not supported for MSIL yet
; MULTI-DAG: error: {{.*}}in function frexp_f64 {{.*}}: runtime library call 'frexp' is not supported for MSIL yet
; MULTI-DAG: error: {{.*}}in function modf_f64 {{.*}}: runtime library call 'modf' is not supported for MSIL yet
; MULTI-DAG: error: {{.*}}in function sincos_f64 {{.*}}: runtime library call '{{sin|cos|sincos}}' is not supported for MSIL yet
; MULTI-DAG: error: {{.*}}in function sin_and_cos_f32 {{.*}}: runtime library call '{{sinf|cosf|sincosf}}' is not supported for MSIL yet
; MULTI-LNX-DAG: error: {{.*}}in function frexp_f32 {{.*}}: runtime library call 'frexpf' is not supported for MSIL yet
; MULTI-WIN-DAG: error: {{.*}}in function frexp_f32 {{.*}}: runtime library call 'frexp' is not supported for MSIL yet
; MULTI-DAG: error: {{.*}}in function modf_f32 {{.*}}: runtime library call 'modff' is not supported for MSIL yet
; MULTI-NOT: error: {{.*}}in function after_multi
; MULTI-NOT: LLVM ERROR
; MULTI-NOT: PLEASE submit

; MULTI-IL-LABEL: .method {{.*}}'after_multi'(
; MULTI-IL-NEXT:  {{^\{$}}
; MULTI-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; MULTI-IL-NEXT:  {{^}}  ldarg.0{{$}}
; MULTI-IL-NEXT:  {{^}}  ldarg.1{{$}}
; MULTI-IL-NEXT:  {{^}}  div{{$}}
; MULTI-IL-NEXT:  {{^}}  ret{{$}}
; MULTI-IL-NEXT:  {{^}}} // end of method 'after_multi'{{$}}

; A sin and a cos of the same value (sincos on GNU targets).
define double @sin_and_cos_f64(double %x) {
  %s = call double @llvm.sin.f64(double %x)
  %c = call double @llvm.cos.f64(double %x)
  %r = fadd double %s, %c
  ret double %r
}

define double @frexp_f64(double %x) {
  %fe = call { double, i32 } @llvm.frexp.f64.i32(double %x)
  %f = extractvalue { double, i32 } %fe, 0
  %e = extractvalue { double, i32 } %fe, 1
  %ef = sitofp i32 %e to double
  %r = fadd double %f, %ef
  ret double %r
}

define double @modf_f64(double %x) {
  %fi = call { double, double } @llvm.modf.f64(double %x)
  %f = extractvalue { double, double } %fi, 0
  %i = extractvalue { double, double } %fi, 1
  %r = fsub double %i, %f
  ret double %r
}

define double @sincos_f64(double %x) {
  %sc = call { double, double } @llvm.sincos.f64(double %x)
  %s = extractvalue { double, double } %sc, 0
  %c = extractvalue { double, double } %sc, 1
  %r = fmul double %s, %c
  ret double %r
}

define float @sin_and_cos_f32(float %x) {
  %s = call float @llvm.sin.f32(float %x)
  %c = call float @llvm.cos.f32(float %x)
  %r = fsub float %s, %c
  ret float %r
}

; frexpf on GNU; windows-msvc has no frexpf, so the double function frexp.
define float @frexp_f32(float %x) {
  %fe = call { float, i32 } @llvm.frexp.f32.i32(float %x)
  %f = extractvalue { float, i32 } %fe, 0
  %e = extractvalue { float, i32 } %fe, 1
  %ef = sitofp i32 %e to float
  %r = fadd float %f, %ef
  ret float %r
}

define float @modf_f32(float %x) {
  %fi = call { float, float } @llvm.modf.f32(float %x)
  %f = extractvalue { float, float } %fi, 0
  %i = extractvalue { float, float } %fi, 1
  %r = fsub float %i, %f
  ret float %r
}

define double @after_multi(double %a, double %b) {
  %r = fdiv double %a, %b
  ret double %r
}

declare double @llvm.sin.f64(double)
declare double @llvm.cos.f64(double)
declare float @llvm.sin.f32(float)
declare float @llvm.cos.f32(float)
declare { double, i32 } @llvm.frexp.f64.i32(double)
declare { double, double } @llvm.modf.f64(double)
declare { double, double } @llvm.sincos.f64(double)
declare { float, i32 } @llvm.frexp.f32.i32(float)
declare { float, float } @llvm.modf.f32(float)

;--- i128.ll
; fptosi and fptoui to i128: the RTLIB helper is named in the error.
source_filename = "fp-expand-i128.c"

; I128:     error: {{.*}}in function fptosi_f32_i128 {{.*}}: runtime library call '__fixsfti' is not supported for MSIL yet
; I128:     error: {{.*}}in function fptosi_f64_i128 {{.*}}: runtime library call '__fixdfti' is not supported for MSIL yet
; I128:     error: {{.*}}in function fptoui_f32_i128 {{.*}}: runtime library call '__fixunssfti' is not supported for MSIL yet
; I128:     error: {{.*}}in function fptoui_f64_i128 {{.*}}: runtime library call '__fixunsdfti' is not supported for MSIL yet
; The legalizer converts a half or bfloat operand to float32 first.
; I128:     error: {{.*}}in function fptosi_f16_i128 {{.*}}: runtime library call '__fixsfti' is not supported for MSIL yet
; I128:     error: {{.*}}in function fptoui_f16_i128 {{.*}}: runtime library call '__fixunssfti' is not supported for MSIL yet
; I128:     error: {{.*}}in function fptosi_bf16_i128 {{.*}}: runtime library call '__fixsfti' is not supported for MSIL yet
; I128:     error: {{.*}}in function fptoui_bf16_i128 {{.*}}: runtime library call '__fixunssfti' is not supported for MSIL yet

; I128-IL-LABEL: .method {{.*}}'use128'(
; I128-IL-SAME:  native int, native int) cil managed{{$}}
; I128-IL-LABEL: .method {{.*}}'fptosi_f32_i128'(
; I128-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128-IL:       {{^}}  ret{{$}}
; I128-IL-NEXT:  {{^}}} // end of method 'fptosi_f32_i128'{{$}}
; I128-IL-LABEL: .method {{.*}}'fptosi_f64_i128'(
; I128-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128-IL:       {{^}}  ret{{$}}
; I128-IL-NEXT:  {{^}}} // end of method 'fptosi_f64_i128'{{$}}
; I128-IL-LABEL: .method {{.*}}'fptoui_f32_i128'(
; I128-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128-IL:       {{^}}  ret{{$}}
; I128-IL-NEXT:  {{^}}} // end of method 'fptoui_f32_i128'{{$}}
; I128-IL-LABEL: .method {{.*}}'fptoui_f64_i128'(
; I128-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128-IL:       {{^}}  ret{{$}}
; I128-IL-NEXT:  {{^}}} // end of method 'fptoui_f64_i128'{{$}}
; I128-IL-LABEL: .method {{.*}}'fptosi_f16_i128'(
; I128-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128-IL:       {{^}}  ret{{$}}
; I128-IL-NEXT:  {{^}}} // end of method 'fptosi_f16_i128'{{$}}
; I128-IL-LABEL: .method {{.*}}'fptoui_f16_i128'(
; I128-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128-IL:       {{^}}  ret{{$}}
; I128-IL-NEXT:  {{^}}} // end of method 'fptoui_f16_i128'{{$}}
; I128-IL-LABEL: .method {{.*}}'fptosi_bf16_i128'(
; I128-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128-IL:       {{^}}  ret{{$}}
; I128-IL-NEXT:  {{^}}} // end of method 'fptosi_bf16_i128'{{$}}
; I128-IL-LABEL: .method {{.*}}'fptoui_bf16_i128'(
; I128-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128-IL:       {{^}}  ret{{$}}
; I128-IL-NEXT:  {{^}}} // end of method 'fptoui_bf16_i128'{{$}}
; I128-IL-LABEL: .method {{.*}}'after_i128'(
; I128-IL-NEXT:  {{^\{$}}
; I128-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; I128-IL-NEXT:  {{^}}  ldarg.0{{$}}
; I128-IL-NEXT:  {{^}}  conv.i{{$}}
; I128-IL-NEXT:  {{^}}  ret{{$}}
; I128-IL-NEXT:  {{^}}} // end of method 'after_i128'{{$}}

; An i128 result is passed on (an i128 return value would be demoted to
; memory, M7).
define void @use128(i128 %v) {
  ret void
}

define void @fptosi_f32_i128(float %x) {
  %r = fptosi float %x to i128
  call void @use128(i128 %r)
  ret void
}

define void @fptosi_f64_i128(double %x) {
  %r = fptosi double %x to i128
  call void @use128(i128 %r)
  ret void
}

define void @fptoui_f32_i128(float %x) {
  %r = fptoui float %x to i128
  call void @use128(i128 %r)
  ret void
}

define void @fptoui_f64_i128(double %x) {
  %r = fptoui double %x to i128
  call void @use128(i128 %r)
  ret void
}

; half and bfloat start from their i16 bits (see bf16.ll).
define void @fptosi_f16_i128(i16 %bits) {
  %x = bitcast i16 %bits to half
  %r = fptosi half %x to i128
  call void @use128(i128 %r)
  ret void
}

define void @fptoui_f16_i128(i16 %bits) {
  %x = bitcast i16 %bits to half
  %r = fptoui half %x to i128
  call void @use128(i128 %r)
  ret void
}

define void @fptosi_bf16_i128(i16 %bits) {
  %x = bitcast i16 %bits to bfloat
  %r = fptosi bfloat %x to i128
  call void @use128(i128 %r)
  ret void
}

define void @fptoui_bf16_i128(i16 %bits) {
  %x = bitcast i16 %bits to bfloat
  %r = fptoui bfloat %x to i128
  call void @use128(i128 %r)
  ret void
}

define i64 @after_i128(i32 %a) {
  %r = sext i32 %a to i64
  ret i64 %r
}


;--- i128-strict.ll
; The constrained (strictfp) conversions to i128: the same errors.
source_filename = "fp-expand-i128-strict.c"

; I128S:    error: {{.*}}in function fptosi_f32_i128_strict {{.*}}: runtime library call '__fixsfti' is not supported for MSIL yet
; I128S:    error: {{.*}}in function fptosi_f64_i128_strict {{.*}}: runtime library call '__fixdfti' is not supported for MSIL yet
; I128S:    error: {{.*}}in function fptoui_f32_i128_strict {{.*}}: runtime library call '__fixunssfti' is not supported for MSIL yet
; I128S:    error: {{.*}}in function fptoui_f64_i128_strict {{.*}}: runtime library call '__fixunsdfti' is not supported for MSIL yet
; I128S:    error: {{.*}}in function fptosi_f16_i128_strict {{.*}}: runtime library call '__fixsfti' is not supported for MSIL yet
; I128S:    error: {{.*}}in function fptoui_f16_i128_strict {{.*}}: runtime library call '__fixunssfti' is not supported for MSIL yet
; I128S:    error: {{.*}}in function fptosi_bf16_i128_strict {{.*}}: runtime library call '__fixsfti' is not supported for MSIL yet
; I128S:    error: {{.*}}in function fptoui_bf16_i128_strict {{.*}}: runtime library call '__fixunssfti' is not supported for MSIL yet

; I128S-IL-LABEL: .method {{.*}}'use128'(
; I128S-IL-SAME:  native int, native int) cil managed{{$}}
; I128S-IL-LABEL: .method {{.*}}'fptosi_f32_i128_strict'(
; I128S-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128S-IL:       {{^}}  ret{{$}}
; I128S-IL-NEXT:  {{^}}} // end of method 'fptosi_f32_i128_strict'{{$}}
; I128S-IL-LABEL: .method {{.*}}'fptosi_f64_i128_strict'(
; I128S-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128S-IL:       {{^}}  ret{{$}}
; I128S-IL-NEXT:  {{^}}} // end of method 'fptosi_f64_i128_strict'{{$}}
; I128S-IL-LABEL: .method {{.*}}'fptoui_f32_i128_strict'(
; I128S-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128S-IL:       {{^}}  ret{{$}}
; I128S-IL-NEXT:  {{^}}} // end of method 'fptoui_f32_i128_strict'{{$}}
; I128S-IL-LABEL: .method {{.*}}'fptoui_f64_i128_strict'(
; I128S-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128S-IL:       {{^}}  ret{{$}}
; I128S-IL-NEXT:  {{^}}} // end of method 'fptoui_f64_i128_strict'{{$}}
; I128S-IL-LABEL: .method {{.*}}'fptosi_f16_i128_strict'(
; I128S-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128S-IL:       {{^}}  ret{{$}}
; I128S-IL-NEXT:  {{^}}} // end of method 'fptosi_f16_i128_strict'{{$}}
; I128S-IL-LABEL: .method {{.*}}'fptoui_f16_i128_strict'(
; I128S-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128S-IL:       {{^}}  ret{{$}}
; I128S-IL-NEXT:  {{^}}} // end of method 'fptoui_f16_i128_strict'{{$}}
; I128S-IL-LABEL: .method {{.*}}'fptosi_bf16_i128_strict'(
; I128S-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128S-IL:       {{^}}  ret{{$}}
; I128S-IL-NEXT:  {{^}}} // end of method 'fptosi_bf16_i128_strict'{{$}}
; I128S-IL-LABEL: .method {{.*}}'fptoui_bf16_i128_strict'(
; I128S-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128S-IL:       {{^}}  ret{{$}}
; I128S-IL-NEXT:  {{^}}} // end of method 'fptoui_bf16_i128_strict'{{$}}
; I128S-IL-LABEL: .method {{.*}}'after_i128_strict'(
; I128S-IL-NEXT:  {{^\{$}}
; I128S-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; I128S-IL-NEXT:  {{^}}  ldarg.0{{$}}
; I128S-IL-NEXT:  {{^}}  conv.i{{$}}
; I128S-IL-NEXT:  {{^}}  ret{{$}}
; I128S-IL-NEXT:  {{^}}} // end of method 'after_i128_strict'{{$}}

; An i128 result is passed on (an i128 return value would be demoted to
; memory, M7).
define void @use128(i128 %v) {
  ret void
}

define void @fptosi_f32_i128_strict(float %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptosi.i128.f32(float %x, metadata !"fpexcept.strict") #0
  call void @use128(i128 %r) #0
  ret void
}

define void @fptosi_f64_i128_strict(double %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptosi.i128.f64(double %x, metadata !"fpexcept.strict") #0
  call void @use128(i128 %r) #0
  ret void
}

define void @fptoui_f32_i128_strict(float %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptoui.i128.f32(float %x, metadata !"fpexcept.strict") #0
  call void @use128(i128 %r) #0
  ret void
}

define void @fptoui_f64_i128_strict(double %x) #0 {
  %r = call i128 @llvm.experimental.constrained.fptoui.i128.f64(double %x, metadata !"fpexcept.strict") #0
  call void @use128(i128 %r) #0
  ret void
}

define void @fptosi_f16_i128_strict(i16 %bits) #0 {
  %x = bitcast i16 %bits to half
  %r = call i128 @llvm.experimental.constrained.fptosi.i128.f16(half %x, metadata !"fpexcept.strict") #0
  call void @use128(i128 %r) #0
  ret void
}

define void @fptoui_f16_i128_strict(i16 %bits) #0 {
  %x = bitcast i16 %bits to half
  %r = call i128 @llvm.experimental.constrained.fptoui.i128.f16(half %x, metadata !"fpexcept.strict") #0
  call void @use128(i128 %r) #0
  ret void
}

define void @fptosi_bf16_i128_strict(i16 %bits) #0 {
  %x = bitcast i16 %bits to bfloat
  %r = call i128 @llvm.experimental.constrained.fptosi.i128.bf16(bfloat %x, metadata !"fpexcept.strict") #0
  call void @use128(i128 %r) #0
  ret void
}

define void @fptoui_bf16_i128_strict(i16 %bits) #0 {
  %x = bitcast i16 %bits to bfloat
  %r = call i128 @llvm.experimental.constrained.fptoui.i128.bf16(bfloat %x, metadata !"fpexcept.strict") #0
  call void @use128(i128 %r) #0
  ret void
}

define i64 @after_i128_strict(i32 %a) {
  %r = sext i32 %a to i64
  ret i64 %r
}

declare i128 @llvm.experimental.constrained.fptosi.i128.f32(float, metadata)
declare i128 @llvm.experimental.constrained.fptosi.i128.f64(double, metadata)
declare i128 @llvm.experimental.constrained.fptoui.i128.f32(float, metadata)
declare i128 @llvm.experimental.constrained.fptoui.i128.f64(double, metadata)
declare i128 @llvm.experimental.constrained.fptosi.i128.f16(half, metadata)
declare i128 @llvm.experimental.constrained.fptoui.i128.f16(half, metadata)
declare i128 @llvm.experimental.constrained.fptosi.i128.bf16(bfloat, metadata)
declare i128 @llvm.experimental.constrained.fptoui.i128.bf16(bfloat, metadata)

attributes #0 = { strictfp }

;--- i128-sat.ll
; The saturating conversions to i128 expand around a plain fptosi or
; fptoui to i128 (compares and selects): the same errors.
source_filename = "fp-expand-i128-sat.c"

; I128SAT:  error: {{.*}}in function fptosi_f32_i128_sat {{.*}}: runtime library call '__fixsfti' is not supported for MSIL yet
; I128SAT:  error: {{.*}}in function fptosi_f64_i128_sat {{.*}}: runtime library call '__fixdfti' is not supported for MSIL yet
; I128SAT:  error: {{.*}}in function fptoui_f32_i128_sat {{.*}}: runtime library call '__fixunssfti' is not supported for MSIL yet
; I128SAT:  error: {{.*}}in function fptoui_f64_i128_sat {{.*}}: runtime library call '__fixunsdfti' is not supported for MSIL yet

; I128SAT-IL-LABEL: .method {{.*}}'use128'(
; I128SAT-IL-SAME:  native int, native int) cil managed{{$}}
; I128SAT-IL-LABEL: .method {{.*}}'fptosi_f32_i128_sat'(
; I128SAT-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128SAT-IL:       {{^}}  ret{{$}}
; I128SAT-IL-NEXT:  {{^}}} // end of method 'fptosi_f32_i128_sat'{{$}}
; I128SAT-IL-LABEL: .method {{.*}}'fptosi_f64_i128_sat'(
; I128SAT-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128SAT-IL:       {{^}}  ret{{$}}
; I128SAT-IL-NEXT:  {{^}}} // end of method 'fptosi_f64_i128_sat'{{$}}
; I128SAT-IL-LABEL: .method {{.*}}'fptoui_f32_i128_sat'(
; I128SAT-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128SAT-IL:       {{^}}  ret{{$}}
; I128SAT-IL-NEXT:  {{^}}} // end of method 'fptoui_f32_i128_sat'{{$}}
; I128SAT-IL-LABEL: .method {{.*}}'fptoui_f64_i128_sat'(
; I128SAT-IL:       {{^}}  call void 'use128'(native int, native int){{$}}
; I128SAT-IL:       {{^}}  ret{{$}}
; I128SAT-IL-NEXT:  {{^}}} // end of method 'fptoui_f64_i128_sat'{{$}}
; I128SAT-IL-LABEL: .method {{.*}}'after_i128_sat'(
; I128SAT-IL-NEXT:  {{^\{$}}
; I128SAT-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; I128SAT-IL-NEXT:  {{^}}  ldarg.0{{$}}
; I128SAT-IL-NEXT:  {{^}}  conv.i{{$}}
; I128SAT-IL-NEXT:  {{^}}  ret{{$}}
; I128SAT-IL-NEXT:  {{^}}} // end of method 'after_i128_sat'{{$}}

; An i128 result is passed on (an i128 return value would be demoted to
; memory, M7).
define void @use128(i128 %v) {
  ret void
}

define void @fptosi_f32_i128_sat(float %x) {
  %r = call i128 @llvm.fptosi.sat.i128.f32(float %x)
  call void @use128(i128 %r)
  ret void
}

define void @fptosi_f64_i128_sat(double %x) {
  %r = call i128 @llvm.fptosi.sat.i128.f64(double %x)
  call void @use128(i128 %r)
  ret void
}

define void @fptoui_f32_i128_sat(float %x) {
  %r = call i128 @llvm.fptoui.sat.i128.f32(float %x)
  call void @use128(i128 %r)
  ret void
}

define void @fptoui_f64_i128_sat(double %x) {
  %r = call i128 @llvm.fptoui.sat.i128.f64(double %x)
  call void @use128(i128 %r)
  ret void
}

define i64 @after_i128_sat(i32 %a) {
  %r = sext i32 %a to i64
  ret i64 %r
}

declare i128 @llvm.fptosi.sat.i128.f32(float)
declare i128 @llvm.fptosi.sat.i128.f64(double)
declare i128 @llvm.fptoui.sat.i128.f32(float)
declare i128 @llvm.fptoui.sat.i128.f64(double)

;--- rounding.ll
; CIL rounds only to nearest. The IL is checked exactly at -O2 (ROUND-IL-O2)
; and only for its key opcodes at -O0 (ROUND-IL-O0) where it differs.
source_filename = "fp-expand-rounding.c"

; ROUND: error: {{.*}}in function set_rounding {{.*}}: changing the floating-point rounding mode is not supported for MSIL{{$}}
; ROUND: error: {{.*}}in function fptrunc_round_upward {{.*}}: changing the floating-point rounding mode is not supported for MSIL{{$}}
; ROUND: error: {{.*}}in function fptrunc_round_f16_tonearest {{.*}}: runtime library call '__truncsfhf2' is not supported for MSIL yet{{$}}
; ROUND: error: {{.*}}in function fptrunc_round_bf16_tonearest {{.*}}: runtime library call '__truncsfbf2' is not supported for MSIL yet{{$}}
; ROUND: error: {{.*}}in function fptrunc_round_v2f32_upward {{.*}}: changing the floating-point rounding mode is not supported for MSIL{{$}}
; One call per element.
; ROUND: error: {{.*}}in function fptrunc_round_v2f16_tonearest {{.*}}: runtime library call '__truncsfhf2' is not supported for MSIL yet{{$}}
; ROUND: error: {{.*}}in function fptrunc_round_v2f16_tonearest {{.*}}: runtime library call '__truncsfhf2' is not supported for MSIL yet{{$}}

; ROUND-IL-LABEL: .method {{.*}}'fptrunc_round_tonearest'(
; ROUND-IL-SAME:  float64) cil managed{{$}}
; ROUND-IL-NEXT:  {{^\{$}}
; ROUND-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; ROUND-IL-NEXT:  {{^}}  ldarg.0{{$}}
; ROUND-IL-NEXT:  {{^}}  conv.r4{{$}}
; ROUND-IL-NEXT:  {{^}}  ret{{$}}
; ROUND-IL-NEXT:  {{^}}} // end of method 'fptrunc_round_tonearest'{{$}}
; ROUND-IL-LABEL:      .method {{.*}}'fptrunc_round_v2f32_tonearest'(
; ROUND-IL-SAME:       float64, float64) cil managed{{$}}
; ROUND-IL-O2-NEXT:    {{^\{$}}
; ROUND-IL-O2-NEXT:    {{^}}  .maxstack 2{{$}}
; ROUND-IL-O2-NEXT:    {{^}}  ldarg.0{{$}}
; ROUND-IL-O2-NEXT:    {{^}}  conv.r4{{$}}
; ROUND-IL-O2-NEXT:    {{^}}  ldarg.1{{$}}
; ROUND-IL-O2-NEXT:    {{^}}  conv.r4{{$}}
; ROUND-IL-O2-NEXT:    {{^}}  sub{{$}}
; ROUND-IL-O2-NEXT:    {{^}}  ret{{$}}
; ROUND-IL-O0-COUNT-2: {{^}}  conv.r4{{$}}
; ROUND-IL-O0-NOT:     conv.r4
; ROUND-IL-O0:         {{^}}  sub{{$}}
; ROUND-IL-O0-NEXT:    {{^}}  ret{{$}}
; ROUND-IL-NEXT:       {{^}}} // end of method 'fptrunc_round_v2f32_tonearest'{{$}}
; ROUND-IL-LABEL: .method {{.*}}'get_rounding'(
; ROUND-IL-SAME:  ) cil managed{{$}}
; ROUND-IL-NEXT:  {{^\{$}}
; ROUND-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; ROUND-IL-NEXT:  {{^}}  ldc.i4.1{{$}}
; ROUND-IL-NEXT:  {{^}}  ret{{$}}
; ROUND-IL-NEXT:  {{^}}} // end of method 'get_rounding'{{$}}
; ROUND-IL-LABEL: .method {{.*}}'after_rounding'(
; ROUND-IL-NEXT:  {{^\{$}}
; ROUND-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; ROUND-IL-NEXT:  {{^}}  ldarg.0{{$}}
; ROUND-IL-NEXT:  {{^}}  ldarg.1{{$}}
; ROUND-IL-NEXT:  {{^}}  add{{$}}
; ROUND-IL-NEXT:  {{^}}  ret{{$}}
; ROUND-IL-NEXT:  {{^}}} // end of method 'after_rounding'{{$}}

; C __builtin_set_flt_rounds.
define void @set_rounding(i32 %mode) {
  call void @llvm.set.rounding(i32 %mode)
  ret void
}

define float @fptrunc_round_upward(double %x) {
  %r = call float @llvm.fptrunc.round.f32.f64(double %x, metadata !"round.upward")
  ret float %r
}

; Rounding to nearest is what conv.r4 does.
define float @fptrunc_round_tonearest(double %x) {
  %r = call float @llvm.fptrunc.round.f32.f64(double %x, metadata !"round.tonearest")
  ret float %r
}

; To half and bfloat: the compiler-rt call of the plain fptrunc. As in
; bf16.ll, 'bitcast ... to i16' keeps the truncation alive.
define i16 @fptrunc_round_f16_tonearest(float %x) {
  %h = call half @llvm.fptrunc.round.f16.f32(float %x, metadata !"round.tonearest")
  %r = bitcast half %h to i16
  ret i16 %r
}

define i16 @fptrunc_round_bf16_tonearest(float %x) {
  %b = call bfloat @llvm.fptrunc.round.bf16.f32(float %x, metadata !"round.tonearest")
  %r = bitcast bfloat %b to i16
  ret i16 %r
}

; Vectors: the arguments are one float64 per element, and both elements are
; used (fsub, so that the order of the operands is visible).
define float @fptrunc_round_v2f32_tonearest(<2 x double> %x) {
  %v = call <2 x float> @llvm.fptrunc.round.v2f32.v2f64(<2 x double> %x, metadata !"round.tonearest")
  %a = extractelement <2 x float> %v, i32 0
  %b = extractelement <2 x float> %v, i32 1
  %r = fsub float %a, %b
  ret float %r
}

define float @fptrunc_round_v2f32_upward(<2 x double> %x) {
  %v = call <2 x float> @llvm.fptrunc.round.v2f32.v2f64(<2 x double> %x, metadata !"round.upward")
  %a = extractelement <2 x float> %v, i32 0
  %b = extractelement <2 x float> %v, i32 1
  %r = fsub float %a, %b
  ret float %r
}

define i16 @fptrunc_round_v2f16_tonearest(<2 x float> %x) {
  %v = call <2 x half> @llvm.fptrunc.round.v2f16.v2f32(<2 x float> %x, metadata !"round.tonearest")
  %a = extractelement <2 x half> %v, i32 0
  %b = extractelement <2 x half> %v, i32 1
  %ai = bitcast half %a to i16
  %bi = bitcast half %b to i16
  %r = xor i16 %ai, %bi
  ret i16 %r
}

; C FLT_ROUNDS: 1, to nearest.
define i32 @get_rounding() {
  %r = call i32 @llvm.get.rounding()
  ret i32 %r
}

define double @after_rounding(double %a, double %b) {
  %r = fadd double %a, %b
  ret double %r
}

declare void @llvm.set.rounding(i32)
declare float @llvm.fptrunc.round.f32.f64(double, metadata)
declare half @llvm.fptrunc.round.f16.f32(float, metadata)
declare bfloat @llvm.fptrunc.round.bf16.f32(float, metadata)
declare <2 x float> @llvm.fptrunc.round.v2f32.v2f64(<2 x double>, metadata)
declare <2 x half> @llvm.fptrunc.round.v2f16.v2f32(<2 x float>, metadata)
declare i32 @llvm.get.rounding()

;--- bf16.ll
; bfloat: every conversion between bfloat and float32 or float64 is a
; compiler-rt call. As for half, a conversion whose only user is a diagnosed
; call may be dropped without a diagnostic of its own, so the fadd (two
; extensions and a truncation) is matched by a pattern.
source_filename = "fp-expand-bf16.c"

; BF16-DAG: error: {{.*}}in function bf16_ext_f32 {{.*}}: runtime library call '__extendbfsf2' is not supported for MSIL yet
; BF16-DAG: error: {{.*}}in function bf16_ext_f64 {{.*}}: runtime library call '__extendbfsf2' is not supported for MSIL yet
; BF16-DAG: error: {{.*}}in function bf16_trunc_f32 {{.*}}: runtime library call '__truncsfbf2' is not supported for MSIL yet
; BF16-DAG: error: {{.*}}in function bf16_trunc_f64 {{.*}}: runtime library call '__truncdfbf2' is not supported for MSIL yet
; BF16-DAG: error: {{.*}}in function bf16_add {{.*}}: runtime library call '{{__extendbfsf2|__truncsfbf2}}' is not supported for MSIL yet
; BF16-NOT: error: {{.*}}in function after_bf16
; BF16-NOT: LLVM ERROR
; BF16-NOT: PLEASE submit

; BF16-IL-LABEL: .method {{.*}}'after_bf16'(
; BF16-IL-NEXT:  {{^\{$}}
; BF16-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; BF16-IL-NEXT:  {{^}}  ldarg.0{{$}}
; BF16-IL-NEXT:  {{^}}  ldarg.1{{$}}
; BF16-IL-NEXT:  {{^}}  sub{{$}}
; BF16-IL-NEXT:  {{^}}  ret{{$}}
; BF16-IL-NEXT:  {{^}}} // end of method 'after_bf16'{{$}}

define float @bf16_ext_f32(i16 %bits) {
  %b = bitcast i16 %bits to bfloat
  %r = fpext bfloat %b to float
  ret float %r
}

define double @bf16_ext_f64(i16 %bits) {
  %b = bitcast i16 %bits to bfloat
  %r = fpext bfloat %b to double
  ret double %r
}

define i16 @bf16_trunc_f32(float %x) {
  %b = fptrunc float %x to bfloat
  %r = bitcast bfloat %b to i16
  ret i16 %r
}

define i16 @bf16_trunc_f64(double %x) {
  %b = fptrunc double %x to bfloat
  %r = bitcast bfloat %b to i16
  ret i16 %r
}

define i16 @bf16_add(i16 %x, i16 %y) {
  %a = bitcast i16 %x to bfloat
  %b = bitcast i16 %y to bfloat
  %s = fadd bfloat %a, %b
  %r = bitcast bfloat %s to i16
  ret i16 %r
}

define float @after_bf16(float %a, float %b) {
  %r = fsub float %a, %b
  ret float %r
}

;--- strict16.ll
; Constrained operations on half and bfloat in strictfp functions end as their
; plain forms do, with the compiler-rt conversion of that form and never
; 'LLVM ERROR'. As in bf16.ll the values start from and end in their i16
; bits: an extension of a bfloat argument is a no-op, and a truncation only
; stays alive when its result is used. The conversions whose only user is a
; diagnosed call may be dropped (see f16.ll), so the fadd is matched by a
; pattern; every other function has one possible library call.
source_filename = "fp-expand-strict16.c"

; STRICT16-DAG: error: {{.*}}in function strict_fpext_f16_f32 {{.*}}: runtime library call '__extendhfsf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fpext_f16_f64 {{.*}}: runtime library call '__extendhfsf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fpext_bf16_f32 {{.*}}: runtime library call '__extendbfsf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fpext_bf16_f64 {{.*}}: runtime library call '__extendbfsf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fptrunc_f32_f16 {{.*}}: runtime library call '__truncsfhf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fptrunc_f64_f16 {{.*}}: runtime library call '__truncdfhf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fptrunc_f32_bf16 {{.*}}: runtime library call '__truncsfbf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fptrunc_f64_bf16 {{.*}}: runtime library call '__truncdfbf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fadd_f16 {{.*}}: runtime library call '{{__extendhfsf2|__truncsfhf2}}' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fadd_bf16 {{.*}}: runtime library call '{{__extendbfsf2|__truncsfbf2}}' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fcmp_f16 {{.*}}: runtime library call '__extendhfsf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fcmps_f16 {{.*}}: runtime library call '__extendhfsf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fcmp_bf16 {{.*}}: runtime library call '__extendbfsf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fcmps_bf16 {{.*}}: runtime library call '__extendbfsf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_sitofp_i32_f16 {{.*}}: runtime library call '__truncsfhf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_sitofp_i32_bf16 {{.*}}: runtime library call '__truncsfbf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fptosi_f16_i32 {{.*}}: runtime library call '__extendhfsf2' is not supported for MSIL yet
; STRICT16-DAG: error: {{.*}}in function strict_fptosi_bf16_i32 {{.*}}: runtime library call '__extendbfsf2' is not supported for MSIL yet
; STRICT16-NOT: error: {{.*}}in function after_strict16
; STRICT16-NOT: LLVM ERROR
; STRICT16-NOT: PLEASE submit

; STRICT16-IL-LABEL: .method {{.*}}'after_strict16'(
; STRICT16-IL-NEXT:  {{^\{$}}
; STRICT16-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; STRICT16-IL-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT16-IL-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT16-IL-NEXT:  {{^}}  add{{$}}
; STRICT16-IL-NEXT:  {{^}}  ret{{$}}
; STRICT16-IL-NEXT:  {{^}}} // end of method 'after_strict16'{{$}}

define float @strict_fpext_f16_f32(i16 %bits) #0 {
  %h = bitcast i16 %bits to half
  %r = call float @llvm.experimental.constrained.fpext.f32.f16(half %h, metadata !"fpexcept.strict") #0
  ret float %r
}

define double @strict_fpext_f16_f64(i16 %bits) #0 {
  %h = bitcast i16 %bits to half
  %r = call double @llvm.experimental.constrained.fpext.f64.f16(half %h, metadata !"fpexcept.strict") #0
  ret double %r
}

define float @strict_fpext_bf16_f32(i16 %bits) #0 {
  %b = bitcast i16 %bits to bfloat
  %r = call float @llvm.experimental.constrained.fpext.f32.bf16(bfloat %b, metadata !"fpexcept.strict") #0
  ret float %r
}

define double @strict_fpext_bf16_f64(i16 %bits) #0 {
  %b = bitcast i16 %bits to bfloat
  %r = call double @llvm.experimental.constrained.fpext.f64.bf16(bfloat %b, metadata !"fpexcept.strict") #0
  ret double %r
}

define i16 @strict_fptrunc_f32_f16(float %x) #0 {
  %h = call half @llvm.experimental.constrained.fptrunc.f16.f32(float %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  %r = bitcast half %h to i16
  ret i16 %r
}

define i16 @strict_fptrunc_f64_f16(double %x) #0 {
  %h = call half @llvm.experimental.constrained.fptrunc.f16.f64(double %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  %r = bitcast half %h to i16
  ret i16 %r
}

define i16 @strict_fptrunc_f32_bf16(float %x) #0 {
  %b = call bfloat @llvm.experimental.constrained.fptrunc.bf16.f32(float %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  %r = bitcast bfloat %b to i16
  ret i16 %r
}

define i16 @strict_fptrunc_f64_bf16(double %x) #0 {
  %b = call bfloat @llvm.experimental.constrained.fptrunc.bf16.f64(double %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  %r = bitcast bfloat %b to i16
  ret i16 %r
}

define i16 @strict_fadd_f16(i16 %x, i16 %y) #0 {
  %a = bitcast i16 %x to half
  %b = bitcast i16 %y to half
  %s = call half @llvm.experimental.constrained.fadd.f16(half %a, half %b, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  %r = bitcast half %s to i16
  ret i16 %r
}

define i16 @strict_fadd_bf16(i16 %x, i16 %y) #0 {
  %a = bitcast i16 %x to bfloat
  %b = bitcast i16 %y to bfloat
  %s = call bfloat @llvm.experimental.constrained.fadd.bf16(bfloat %a, bfloat %b, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  %r = bitcast bfloat %s to i16
  ret i16 %r
}

define i32 @strict_fcmp_f16(i16 %x, i16 %y) #0 {
  %a = bitcast i16 %x to half
  %b = bitcast i16 %y to half
  %c = call i1 @llvm.experimental.constrained.fcmp.f16(half %a, half %b, metadata !"olt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @strict_fcmps_f16(i16 %x, i16 %y) #0 {
  %a = bitcast i16 %x to half
  %b = bitcast i16 %y to half
  %c = call i1 @llvm.experimental.constrained.fcmps.f16(half %a, half %b, metadata !"olt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @strict_fcmp_bf16(i16 %x, i16 %y) #0 {
  %a = bitcast i16 %x to bfloat
  %b = bitcast i16 %y to bfloat
  %c = call i1 @llvm.experimental.constrained.fcmp.bf16(bfloat %a, bfloat %b, metadata !"olt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @strict_fcmps_bf16(i16 %x, i16 %y) #0 {
  %a = bitcast i16 %x to bfloat
  %b = bitcast i16 %y to bfloat
  %c = call i1 @llvm.experimental.constrained.fcmps.bf16(bfloat %a, bfloat %b, metadata !"olt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

define i16 @strict_sitofp_i32_f16(i32 %x) #0 {
  %h = call half @llvm.experimental.constrained.sitofp.f16.i32(i32 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  %r = bitcast half %h to i16
  ret i16 %r
}

define i16 @strict_sitofp_i32_bf16(i32 %x) #0 {
  %b = call bfloat @llvm.experimental.constrained.sitofp.bf16.i32(i32 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  %r = bitcast bfloat %b to i16
  ret i16 %r
}

define i32 @strict_fptosi_f16_i32(i16 %bits) #0 {
  %h = bitcast i16 %bits to half
  %r = call i32 @llvm.experimental.constrained.fptosi.i32.f16(half %h, metadata !"fpexcept.strict") #0
  ret i32 %r
}

define i32 @strict_fptosi_bf16_i32(i16 %bits) #0 {
  %b = bitcast i16 %bits to bfloat
  %r = call i32 @llvm.experimental.constrained.fptosi.i32.bf16(bfloat %b, metadata !"fpexcept.strict") #0
  ret i32 %r
}

define float @after_strict16(float %a, float %b) {
  %r = fadd float %a, %b
  ret float %r
}

declare float @llvm.experimental.constrained.fpext.f32.f16(half, metadata)
declare double @llvm.experimental.constrained.fpext.f64.f16(half, metadata)
declare float @llvm.experimental.constrained.fpext.f32.bf16(bfloat, metadata)
declare double @llvm.experimental.constrained.fpext.f64.bf16(bfloat, metadata)
declare half @llvm.experimental.constrained.fptrunc.f16.f32(float, metadata, metadata)
declare half @llvm.experimental.constrained.fptrunc.f16.f64(double, metadata, metadata)
declare bfloat @llvm.experimental.constrained.fptrunc.bf16.f32(float, metadata, metadata)
declare bfloat @llvm.experimental.constrained.fptrunc.bf16.f64(double, metadata, metadata)
declare half @llvm.experimental.constrained.fadd.f16(half, half, metadata, metadata)
declare bfloat @llvm.experimental.constrained.fadd.bf16(bfloat, bfloat, metadata, metadata)
declare i1 @llvm.experimental.constrained.fcmp.f16(half, half, metadata, metadata)
declare i1 @llvm.experimental.constrained.fcmps.f16(half, half, metadata, metadata)
declare i1 @llvm.experimental.constrained.fcmp.bf16(bfloat, bfloat, metadata, metadata)
declare i1 @llvm.experimental.constrained.fcmps.bf16(bfloat, bfloat, metadata, metadata)
declare half @llvm.experimental.constrained.sitofp.f16.i32(i32, metadata, metadata)
declare bfloat @llvm.experimental.constrained.sitofp.bf16.i32(i32, metadata, metadata)
declare i32 @llvm.experimental.constrained.fptosi.i32.f16(half, metadata)
declare i32 @llvm.experimental.constrained.fptosi.i32.bf16(bfloat, metadata)

attributes #0 = { strictfp }

;--- fptypes.ll
; fp128 (C __float128), x86_fp80 and ppc_fp128 in a signature or an
; instruction, also as the element of a vector, struct or array type, and the
; floating-point environment intrinsics: one error per definition, from
; MSILPrepareIR, before instruction selection.
source_filename = "fp-expand-fptypes.c"

; FPTY: error: {{.*}}in function fp128_fadd {{.*}}: floating-point type 'fp128' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function fp128_fmul {{.*}}: floating-point type 'fp128' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function fp128_fpext {{.*}}: floating-point type 'fp128' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function fp128_fptrunc {{.*}}: floating-point type 'fp128' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function fp128_call {{.*}}: floating-point type 'fp128' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function fp128_identity {{.*}}: floating-point type 'fp128' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function x86_fp80_fadd {{.*}}: floating-point type 'x86_fp80' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function ppc_fp128_fadd {{.*}}: floating-point type 'ppc_fp128' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function fp128_vector_identity {{.*}}: floating-point type 'fp128' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function ppc_fp128_struct_arg {{.*}}: floating-point type 'ppc_fp128' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function x86_fp80_struct_alloca {{.*}}: floating-point type 'x86_fp80' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function fp128_array_alloca {{.*}}: floating-point type 'fp128' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function fp128_struct_call {{.*}}: floating-point type 'fp128' is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function get_fpenv {{.*}}: floating-point environment access ('llvm.get.fpenv') is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function set_fpenv {{.*}}: floating-point environment access ('llvm.set.fpenv') is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function get_fpmode {{.*}}: floating-point environment access ('llvm.get.fpmode') is not supported for MSIL{{$}}
; FPTY: error: {{.*}}in function set_fpmode {{.*}}: floating-point environment access ('llvm.set.fpmode') is not supported for MSIL{{$}}

; A definition that lost its body is not printed.
; FPTY-IL-NOT:    {{fp128|fp80|'fp128_|'x86_fp80_|'ppc_fp128_|'get_fp|'set_fp}}
; FPTY-IL-LABEL:  .method {{.*}}'after_fptypes'(
; FPTY-IL-NEXT:   {{^\{$}}
; FPTY-IL-NEXT:   {{^}}  .maxstack 2{{$}}
; FPTY-IL-NEXT:   {{^}}  ldarg.0{{$}}
; FPTY-IL-NEXT:   {{^}}  ldarg.1{{$}}
; FPTY-IL-NEXT:   {{^}}  mul{{$}}
; FPTY-IL-NEXT:   {{^}}  ret{{$}}
; FPTY-IL-NEXT:   {{^}}} // end of method 'after_fptypes'{{$}}
; FPTY-IL-NOT:    {{fp128|fp80|'fp128_|'x86_fp80_|'ppc_fp128_|'get_fp|'set_fp}}

; In the signature and the instruction.
define fp128 @fp128_fadd(fp128 %a, fp128 %b) {
  %r = fadd fp128 %a, %b
  ret fp128 %r
}

; Only in instructions: (double)((__float128)a * b).
define double @fp128_fmul(double %a, double %b) {
  %x = fpext double %a to fp128
  %y = fpext double %b to fp128
  %m = fmul fp128 %x, %y
  %r = fptrunc fp128 %m to double
  ret double %r
}

; The body is deleted before instruction selection, so the memory accesses
; that keep these values alive are never compiled.
define void @fp128_fpext(double %x, ptr %p) {
  %q = fpext double %x to fp128
  store fp128 %q, ptr %p
  ret void
}

define double @fp128_fptrunc(ptr %p) {
  %q = load fp128, ptr %p
  %r = fptrunc fp128 %q to double
  ret double %r
}

; Only the result type of a call.
declare fp128 @make_fp128(double)

define void @fp128_call(double %x) {
  %q = call fp128 @make_fp128(double %x)
  ret void
}

; Only the signature.
define fp128 @fp128_identity(fp128 %x) {
  ret fp128 %x
}

define x86_fp80 @x86_fp80_fadd(x86_fp80 %a, x86_fp80 %b) {
  %r = fadd x86_fp80 %a, %b
  ret x86_fp80 %r
}

define ppc_fp128 @ppc_fp128_fadd(ppc_fp128 %a, ppc_fp128 %b) {
  %r = fadd ppc_fp128 %a, %b
  ret ppc_fp128 %r
}

; The types as the element of a vector, struct or array type, where no value
; of the type itself appears: in the signature only (the vector is returned
; unchanged, the struct argument is not used), only in the allocated type of
; an alloca whose other field alone is accessed, and only in the result type
; of a call whose other field alone is used.
define <2 x fp128> @fp128_vector_identity(<2 x fp128> %v) {
  ret <2 x fp128> %v
}

define i32 @ppc_fp128_struct_arg({ ppc_fp128, i32 } %s) {
  ret i32 1
}

define i32 @x86_fp80_struct_alloca() {
  %p = alloca { x86_fp80, i32 }
  %q = getelementptr { x86_fp80, i32 }, ptr %p, i32 0, i32 1
  store i32 7, ptr %q
  %v = load i32, ptr %q
  ret i32 %v
}

define i32 @fp128_array_alloca() {
  %p = alloca [4 x fp128]
  store i32 7, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

declare { fp128, i32 } @make_fp128_pair(double)

define i32 @fp128_struct_call(double %x) {
  %s = call { fp128, i32 } @make_fp128_pair(double %x)
  %i = extractvalue { fp128, i32 } %s, 1
  ret i32 %i
}

; C fegetenv, fesetenv, fegetmode and fesetmode as intrinsics.
define i64 @get_fpenv() {
  %e = call i64 @llvm.get.fpenv.i64()
  ret i64 %e
}

define void @set_fpenv(i64 %e) {
  call void @llvm.set.fpenv.i64(i64 %e)
  ret void
}

define i32 @get_fpmode() {
  %m = call i32 @llvm.get.fpmode.i32()
  ret i32 %m
}

define void @set_fpmode(i32 %m) {
  call void @llvm.set.fpmode.i32(i32 %m)
  ret void
}

define double @after_fptypes(double %a, double %b) {
  %r = fmul double %a, %b
  ret double %r
}

declare i64 @llvm.get.fpenv.i64()
declare void @llvm.set.fpenv.i64(i64)
declare i32 @llvm.get.fpmode.i32()
declare void @llvm.set.fpmode.i32(i32)
