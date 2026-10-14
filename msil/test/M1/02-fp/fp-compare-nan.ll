; Floating-point comparisons as values (Milestone 5): every fcmp predicate on
; float32 and float64, with exact NaN semantics. Ordered predicates are false
; and unordered ones true when an operand is NaN (quiet, negative, with a
; payload, float64 signaling, and NaN computed at run time); -0.0 equals 0.0.
; The predicates run together in one method (fcmp_mask_*: bit i is predicate i
; in LLVM order, false = bit 0 ... true = bit 15) and each alone in its own
; method, and the results are used as integers (sext, zext, xor-negation).
; Each noinline check function returns 1 when a result is wrong; main ORs one
; bit per check into the exit status (8 bits, D37): [exit 0] means every check
; passed.
; RUN: %{msil_ll}

define i32 @fcmp_mask_f64(double %a, double %b) noinline {
  %c0 = fcmp false double %a, %b
  %z0 = zext i1 %c0 to i32
  %c1 = fcmp oeq double %a, %b
  %z1 = zext i1 %c1 to i32
  %s1 = shl i32 %z1, 1
  %c2 = fcmp ogt double %a, %b
  %z2 = zext i1 %c2 to i32
  %s2 = shl i32 %z2, 2
  %c3 = fcmp oge double %a, %b
  %z3 = zext i1 %c3 to i32
  %s3 = shl i32 %z3, 3
  %c4 = fcmp olt double %a, %b
  %z4 = zext i1 %c4 to i32
  %s4 = shl i32 %z4, 4
  %c5 = fcmp ole double %a, %b
  %z5 = zext i1 %c5 to i32
  %s5 = shl i32 %z5, 5
  %c6 = fcmp one double %a, %b
  %z6 = zext i1 %c6 to i32
  %s6 = shl i32 %z6, 6
  %c7 = fcmp ord double %a, %b
  %z7 = zext i1 %c7 to i32
  %s7 = shl i32 %z7, 7
  %c8 = fcmp uno double %a, %b
  %z8 = zext i1 %c8 to i32
  %s8 = shl i32 %z8, 8
  %c9 = fcmp ueq double %a, %b
  %z9 = zext i1 %c9 to i32
  %s9 = shl i32 %z9, 9
  %c10 = fcmp ugt double %a, %b
  %z10 = zext i1 %c10 to i32
  %s10 = shl i32 %z10, 10
  %c11 = fcmp uge double %a, %b
  %z11 = zext i1 %c11 to i32
  %s11 = shl i32 %z11, 11
  %c12 = fcmp ult double %a, %b
  %z12 = zext i1 %c12 to i32
  %s12 = shl i32 %z12, 12
  %c13 = fcmp ule double %a, %b
  %z13 = zext i1 %c13 to i32
  %s13 = shl i32 %z13, 13
  %c14 = fcmp une double %a, %b
  %z14 = zext i1 %c14 to i32
  %s14 = shl i32 %z14, 14
  %c15 = fcmp true double %a, %b
  %z15 = zext i1 %c15 to i32
  %s15 = shl i32 %z15, 15
  %m1 = or i32 %z0, %s1
  %m2 = or i32 %m1, %s2
  %m3 = or i32 %m2, %s3
  %m4 = or i32 %m3, %s4
  %m5 = or i32 %m4, %s5
  %m6 = or i32 %m5, %s6
  %m7 = or i32 %m6, %s7
  %m8 = or i32 %m7, %s8
  %m9 = or i32 %m8, %s9
  %m10 = or i32 %m9, %s10
  %m11 = or i32 %m10, %s11
  %m12 = or i32 %m11, %s12
  %m13 = or i32 %m12, %s13
  %m14 = or i32 %m13, %s14
  %m15 = or i32 %m14, %s15
  ret i32 %m15
}

define i32 @fcmp_mask_f32(float %a, float %b) noinline {
  %c0 = fcmp false float %a, %b
  %z0 = zext i1 %c0 to i32
  %c1 = fcmp oeq float %a, %b
  %z1 = zext i1 %c1 to i32
  %s1 = shl i32 %z1, 1
  %c2 = fcmp ogt float %a, %b
  %z2 = zext i1 %c2 to i32
  %s2 = shl i32 %z2, 2
  %c3 = fcmp oge float %a, %b
  %z3 = zext i1 %c3 to i32
  %s3 = shl i32 %z3, 3
  %c4 = fcmp olt float %a, %b
  %z4 = zext i1 %c4 to i32
  %s4 = shl i32 %z4, 4
  %c5 = fcmp ole float %a, %b
  %z5 = zext i1 %c5 to i32
  %s5 = shl i32 %z5, 5
  %c6 = fcmp one float %a, %b
  %z6 = zext i1 %c6 to i32
  %s6 = shl i32 %z6, 6
  %c7 = fcmp ord float %a, %b
  %z7 = zext i1 %c7 to i32
  %s7 = shl i32 %z7, 7
  %c8 = fcmp uno float %a, %b
  %z8 = zext i1 %c8 to i32
  %s8 = shl i32 %z8, 8
  %c9 = fcmp ueq float %a, %b
  %z9 = zext i1 %c9 to i32
  %s9 = shl i32 %z9, 9
  %c10 = fcmp ugt float %a, %b
  %z10 = zext i1 %c10 to i32
  %s10 = shl i32 %z10, 10
  %c11 = fcmp uge float %a, %b
  %z11 = zext i1 %c11 to i32
  %s11 = shl i32 %z11, 11
  %c12 = fcmp ult float %a, %b
  %z12 = zext i1 %c12 to i32
  %s12 = shl i32 %z12, 12
  %c13 = fcmp ule float %a, %b
  %z13 = zext i1 %c13 to i32
  %s13 = shl i32 %z13, 13
  %c14 = fcmp une float %a, %b
  %z14 = zext i1 %c14 to i32
  %s14 = shl i32 %z14, 14
  %c15 = fcmp true float %a, %b
  %z15 = zext i1 %c15 to i32
  %s15 = shl i32 %z15, 15
  %m1 = or i32 %z0, %s1
  %m2 = or i32 %m1, %s2
  %m3 = or i32 %m2, %s3
  %m4 = or i32 %m3, %s4
  %m5 = or i32 %m4, %s5
  %m6 = or i32 %m5, %s6
  %m7 = or i32 %m6, %s7
  %m8 = or i32 %m7, %s8
  %m9 = or i32 %m8, %s9
  %m10 = or i32 %m9, %s10
  %m11 = or i32 %m10, %s11
  %m12 = or i32 %m11, %s12
  %m13 = or i32 %m12, %s13
  %m14 = or i32 %m13, %s14
  %m15 = or i32 %m14, %s15
  ret i32 %m15
}

define i32 @fcmp_oeq_f64(double %a, double %b) noinline {
  %c = fcmp oeq double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ogt_f64(double %a, double %b) noinline {
  %c = fcmp ogt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_oge_f64(double %a, double %b) noinline {
  %c = fcmp oge double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_olt_f64(double %a, double %b) noinline {
  %c = fcmp olt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ole_f64(double %a, double %b) noinline {
  %c = fcmp ole double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_one_f64(double %a, double %b) noinline {
  %c = fcmp one double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ord_f64(double %a, double %b) noinline {
  %c = fcmp ord double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_uno_f64(double %a, double %b) noinline {
  %c = fcmp uno double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ueq_f64(double %a, double %b) noinline {
  %c = fcmp ueq double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ugt_f64(double %a, double %b) noinline {
  %c = fcmp ugt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_uge_f64(double %a, double %b) noinline {
  %c = fcmp uge double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ult_f64(double %a, double %b) noinline {
  %c = fcmp ult double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ule_f64(double %a, double %b) noinline {
  %c = fcmp ule double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_une_f64(double %a, double %b) noinline {
  %c = fcmp une double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_oeq_f32(float %a, float %b) noinline {
  %c = fcmp oeq float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ogt_f32(float %a, float %b) noinline {
  %c = fcmp ogt float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_oge_f32(float %a, float %b) noinline {
  %c = fcmp oge float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_olt_f32(float %a, float %b) noinline {
  %c = fcmp olt float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ole_f32(float %a, float %b) noinline {
  %c = fcmp ole float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_one_f32(float %a, float %b) noinline {
  %c = fcmp one float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ord_f32(float %a, float %b) noinline {
  %c = fcmp ord float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_uno_f32(float %a, float %b) noinline {
  %c = fcmp uno float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ueq_f32(float %a, float %b) noinline {
  %c = fcmp ueq float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ugt_f32(float %a, float %b) noinline {
  %c = fcmp ugt float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_uge_f32(float %a, float %b) noinline {
  %c = fcmp uge float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ult_f32(float %a, float %b) noinline {
  %c = fcmp ult float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ule_f32(float %a, float %b) noinline {
  %c = fcmp ule float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_une_f32(float %a, float %b) noinline {
  %c = fcmp une float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_values_f64(double %a, double %b) noinline {
  %lt = fcmp olt double %a, %b
  %ge = fcmp uge double %a, %b
  %eq = fcmp oeq double %a, %b
  %nlt = xor i1 %lt, true
  %nge = xor i1 %ge, true
  %x0 = sext i1 %lt to i32
  %x1 = zext i1 %nlt to i32
  %x2 = sext i1 %nge to i32
  %x3 = zext i1 %eq to i32
  %y0 = mul i32 %x0, 1000
  %y1 = mul i32 %x1, 100
  %y2 = mul i32 %x2, 10
  %w0 = add i32 %y0, %y1
  %w1 = add i32 %w0, %y2
  %w2 = add i32 %w1, %x3
  ret i32 %w2
}

define i32 @fcmp_const_f64(double %a) noinline {
  %c0 = fcmp olt double %a, 0.0
  %c1 = fcmp ugt double 1.0, %a
  %c2 = fcmp oge double %a, -2.5
  %c3 = fcmp une double %a, 1.0
  %c4 = fcmp ord double %a, 0.0
  %c5 = fcmp uno double 0.0, %a
  %c6 = fcmp oeq double %a, 0x7FF0000000000000
  %c7 = fcmp ult double %a, 0xFFF0000000000000
  %z0 = zext i1 %c0 to i32
  %z1 = zext i1 %c1 to i32
  %z2 = zext i1 %c2 to i32
  %z3 = zext i1 %c3 to i32
  %z4 = zext i1 %c4 to i32
  %z5 = zext i1 %c5 to i32
  %z6 = zext i1 %c6 to i32
  %z7 = zext i1 %c7 to i32
  %s1 = shl i32 %z1, 1
  %s2 = shl i32 %z2, 2
  %s3 = shl i32 %z3, 3
  %s4 = shl i32 %z4, 4
  %s5 = shl i32 %z5, 5
  %s6 = shl i32 %z6, 6
  %s7 = shl i32 %z7, 7
  %m1 = or i32 %z0, %s1
  %m2 = or i32 %m1, %s2
  %m3 = or i32 %m2, %s3
  %m4 = or i32 %m3, %s4
  %m5 = or i32 %m4, %s5
  %m6 = or i32 %m5, %s6
  %m7 = or i32 %m6, %s7
  ret i32 %m7
}

define i32 @fcmp_values_f32(float %a, float %b) noinline {
  %lt = fcmp olt float %a, %b
  %ge = fcmp uge float %a, %b
  %eq = fcmp oeq float %a, %b
  %nlt = xor i1 %lt, true
  %nge = xor i1 %ge, true
  %x0 = sext i1 %lt to i32
  %x1 = zext i1 %nlt to i32
  %x2 = sext i1 %nge to i32
  %x3 = zext i1 %eq to i32
  %y0 = mul i32 %x0, 1000
  %y1 = mul i32 %x1, 100
  %y2 = mul i32 %x2, 10
  %w0 = add i32 %y0, %y1
  %w1 = add i32 %w0, %y2
  %w2 = add i32 %w1, %x3
  ret i32 %w2
}

define i32 @fcmp_const_f32(float %a) noinline {
  %c0 = fcmp olt float %a, 0.0
  %c1 = fcmp ugt float 1.0, %a
  %c2 = fcmp oge float %a, -2.5
  %c3 = fcmp une float %a, 1.0
  %c4 = fcmp ord float %a, 0.0
  %c5 = fcmp uno float 0.0, %a
  %c6 = fcmp oeq float %a, 0x7FF0000000000000
  %c7 = fcmp ult float %a, 0xFFF0000000000000
  %z0 = zext i1 %c0 to i32
  %z1 = zext i1 %c1 to i32
  %z2 = zext i1 %c2 to i32
  %z3 = zext i1 %c3 to i32
  %z4 = zext i1 %c4 to i32
  %z5 = zext i1 %c5 to i32
  %z6 = zext i1 %c6 to i32
  %z7 = zext i1 %c7 to i32
  %s1 = shl i32 %z1, 1
  %s2 = shl i32 %z2, 2
  %s3 = shl i32 %z3, 3
  %s4 = shl i32 %z4, 4
  %s5 = shl i32 %z5, 5
  %s6 = shl i32 %z6, 6
  %s7 = shl i32 %z7, 7
  %m1 = or i32 %z0, %s1
  %m2 = or i32 %m1, %s2
  %m3 = or i32 %m2, %s3
  %m4 = or i32 %m3, %s4
  %m5 = or i32 %m4, %s5
  %m6 = or i32 %m5, %s6
  %m7 = or i32 %m6, %s7
  ret i32 %m7
}

define double @fdiv_f64(double %a, double %b) noinline {
  %r = fdiv double %a, %b
  ret double %r
}

define double @fsub_f64(double %a, double %b) noinline {
  %r = fsub double %a, %b
  ret double %r
}

define float @fdiv_f32(float %a, float %b) noinline {
  %r = fdiv float %a, %b
  ret float %r
}

define float @fsub_f32(float %a, float %b) noinline {
  %r = fsub float %a, %b
  ret float %r
}

define double @fpext(float %a) noinline {
  %r = fpext float %a to double
  ret double %r
}

define float @fptrunc(double %a) noinline {
  %r = fptrunc double %a to float
  ret float %r
}

; Bit 0: all 16 predicates on float64 with a NaN operand: only the
; unordered predicates and 'true' hold (mask 0xFF00), for every NaN
; encoding, on either side and against inf.
define internal i32 @check_nan_mask_f64() noinline {
  %r1 = call i32 @fcmp_mask_f64(double 0x7FF8000000000000, double 1.0) ; 0xFF00
  %k2 = icmp eq i32 %r1, 65280
  %r3 = call i32 @fcmp_mask_f64(double -1.5, double 0x7FF8000000000000) ; 0xFF00
  %k4 = icmp eq i32 %r3, 65280
  %r5 = call i32 @fcmp_mask_f64(double 0xFFF8000000000000, double 1.0) ; 0xFF00
  %k6 = icmp eq i32 %r5, 65280
  %r7 = call i32 @fcmp_mask_f64(double -1.5, double 0xFFF8000000000000) ; 0xFF00
  %k8 = icmp eq i32 %r7, 65280
  %r9 = call i32 @fcmp_mask_f64(double 0x7FF8000000000123, double 1.0) ; 0xFF00
  %k10 = icmp eq i32 %r9, 65280
  %r11 = call i32 @fcmp_mask_f64(double -1.5, double 0x7FF8000000000123) ; 0xFF00
  %k12 = icmp eq i32 %r11, 65280
  %r13 = call i32 @fcmp_mask_f64(double 0x7FF0000000000001, double 1.0) ; 0xFF00
  %k14 = icmp eq i32 %r13, 65280
  %r15 = call i32 @fcmp_mask_f64(double -1.5, double 0x7FF0000000000001) ; 0xFF00
  %k16 = icmp eq i32 %r15, 65280
  %r17 = call i32 @fcmp_mask_f64(double 0x7FF8000000000000, double 0x7FF8000000000000) ; 0xFF00
  %k18 = icmp eq i32 %r17, 65280
  %r19 = call i32 @fcmp_mask_f64(double 0x7FF8000000000000, double 0x7FF0000000000000) ; 0xFF00
  %k20 = icmp eq i32 %r19, 65280
  %r21 = call i32 @fcmp_mask_f64(double 0xFFF0000000000000, double 0x7FF8000000000000) ; 0xFF00
  %k22 = icmp eq i32 %r21, 65280
  %r23 = call i32 @fcmp_mask_f64(double 0.0, double 0xFFF8000000000000) ; 0xFF00
  %k24 = icmp eq i32 %r23, 65280
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %all6 = and i1 %all5, %k14
  %all7 = and i1 %all6, %k16
  %all8 = and i1 %all7, %k18
  %all9 = and i1 %all8, %k20
  %all10 = and i1 %all9, %k22
  %all11 = and i1 %all10, %k24
  %bad = xor i1 %all11, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 1: all 16 predicates on float32 with a NaN operand: only the
; unordered predicates and 'true' hold (mask 0xFF00), for every NaN
; encoding, on either side and against inf.
define internal i32 @check_nan_mask_f32() noinline {
  %r1 = call i32 @fcmp_mask_f32(float 0x7FF8000000000000, float 1.0) ; 0xFF00
  %k2 = icmp eq i32 %r1, 65280
  %r3 = call i32 @fcmp_mask_f32(float -1.5, float 0x7FF8000000000000) ; 0xFF00
  %k4 = icmp eq i32 %r3, 65280
  %r5 = call i32 @fcmp_mask_f32(float 0xFFF8000000000000, float 1.0) ; 0xFF00
  %k6 = icmp eq i32 %r5, 65280
  %r7 = call i32 @fcmp_mask_f32(float -1.5, float 0xFFF8000000000000) ; 0xFF00
  %k8 = icmp eq i32 %r7, 65280
  %r9 = call i32 @fcmp_mask_f32(float 0x7FF8002460000000, float 1.0) ; 0xFF00
  %k10 = icmp eq i32 %r9, 65280
  %r11 = call i32 @fcmp_mask_f32(float -1.5, float 0x7FF8002460000000) ; 0xFF00
  %k12 = icmp eq i32 %r11, 65280
  %r13 = call i32 @fcmp_mask_f32(float 0x7FF8000000000000, float 0x7FF8000000000000) ; 0xFF00
  %k14 = icmp eq i32 %r13, 65280
  %r15 = call i32 @fcmp_mask_f32(float 0x7FF8000000000000, float 0x7FF0000000000000) ; 0xFF00
  %k16 = icmp eq i32 %r15, 65280
  %r17 = call i32 @fcmp_mask_f32(float 0xFFF0000000000000, float 0x7FF8000000000000) ; 0xFF00
  %k18 = icmp eq i32 %r17, 65280
  %r19 = call i32 @fcmp_mask_f32(float 0.0, float 0xFFF8000000000000) ; 0xFF00
  %k20 = icmp eq i32 %r19, 65280
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %all6 = and i1 %all5, %k14
  %all7 = and i1 %all6, %k16
  %all8 = and i1 %all7, %k18
  %all9 = and i1 %all8, %k20
  %bad = xor i1 %all9, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 2: all 16 predicates on ordered float64 operands: less, greater,
; equal, -0.0 == 0.0, infinities, subnormals and the largest values.
define internal i32 @check_ordered_mask_f64() noinline {
  %r1 = call i32 @fcmp_mask_f64(double 1.0, double 2.0)            ; 0xF0F0
  %k2 = icmp eq i32 %r1, 61680
  %r3 = call i32 @fcmp_mask_f64(double 2.0, double 1.0)            ; 0xCCCC
  %k4 = icmp eq i32 %r3, 52428
  %r5 = call i32 @fcmp_mask_f64(double 1.0, double 1.0)            ; 0xAAAA
  %k6 = icmp eq i32 %r5, 43690
  %r7 = call i32 @fcmp_mask_f64(double -0.0, double 0.0)           ; 0xAAAA
  %k8 = icmp eq i32 %r7, 43690
  %r9 = call i32 @fcmp_mask_f64(double 0.0, double -0.0)           ; 0xAAAA
  %k10 = icmp eq i32 %r9, 43690
  %r11 = call i32 @fcmp_mask_f64(double -3.5, double -3.25)        ; 0xF0F0
  %k12 = icmp eq i32 %r11, 61680
  %r13 = call i32 @fcmp_mask_f64(double 0x7FF0000000000000, double 0x7FF0000000000000) ; 0xAAAA
  %k14 = icmp eq i32 %r13, 43690
  %r15 = call i32 @fcmp_mask_f64(double 0xFFF0000000000000, double 0x7FF0000000000000) ; 0xF0F0
  %k16 = icmp eq i32 %r15, 61680
  %r17 = call i32 @fcmp_mask_f64(double 0x7FF0000000000000, double 0x7FEFFFFFFFFFFFFF) ; 0xCCCC
  %k18 = icmp eq i32 %r17, 52428
  %r19 = call i32 @fcmp_mask_f64(double 0xFFF0000000000000, double 0xFFEFFFFFFFFFFFFF) ; 0xF0F0
  %k20 = icmp eq i32 %r19, 61680
  %r21 = call i32 @fcmp_mask_f64(double 0x0000000000000001, double 0.0) ; 0xCCCC
  %k22 = icmp eq i32 %r21, 52428
  %r23 = call i32 @fcmp_mask_f64(double 0x8000000000000001, double 0.0) ; 0xF0F0
  %k24 = icmp eq i32 %r23, 61680
  %r25 = call i32 @fcmp_mask_f64(double 0x3FD5555555555555, double 0x3FD5555555555555) ; 0xAAAA
  %k26 = icmp eq i32 %r25, 43690
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %all6 = and i1 %all5, %k14
  %all7 = and i1 %all6, %k16
  %all8 = and i1 %all7, %k18
  %all9 = and i1 %all8, %k20
  %all10 = and i1 %all9, %k22
  %all11 = and i1 %all10, %k24
  %all12 = and i1 %all11, %k26
  %bad = xor i1 %all12, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 3: all 16 predicates on ordered float32 operands: less, greater,
; equal, -0.0 == 0.0, infinities, subnormals and the largest values.
define internal i32 @check_ordered_mask_f32() noinline {
  %r1 = call i32 @fcmp_mask_f32(float 1.0, float 2.0)              ; 0xF0F0
  %k2 = icmp eq i32 %r1, 61680
  %r3 = call i32 @fcmp_mask_f32(float 2.0, float 1.0)              ; 0xCCCC
  %k4 = icmp eq i32 %r3, 52428
  %r5 = call i32 @fcmp_mask_f32(float 1.0, float 1.0)              ; 0xAAAA
  %k6 = icmp eq i32 %r5, 43690
  %r7 = call i32 @fcmp_mask_f32(float -0.0, float 0.0)             ; 0xAAAA
  %k8 = icmp eq i32 %r7, 43690
  %r9 = call i32 @fcmp_mask_f32(float 0.0, float -0.0)             ; 0xAAAA
  %k10 = icmp eq i32 %r9, 43690
  %r11 = call i32 @fcmp_mask_f32(float -3.5, float -3.25)          ; 0xF0F0
  %k12 = icmp eq i32 %r11, 61680
  %r13 = call i32 @fcmp_mask_f32(float 0x7FF0000000000000, float 0x7FF0000000000000) ; 0xAAAA
  %k14 = icmp eq i32 %r13, 43690
  %r15 = call i32 @fcmp_mask_f32(float 0xFFF0000000000000, float 0x7FF0000000000000) ; 0xF0F0
  %k16 = icmp eq i32 %r15, 61680
  %r17 = call i32 @fcmp_mask_f32(float 0x7FF0000000000000, float 0x47EFFFFFE0000000) ; 0xCCCC
  %k18 = icmp eq i32 %r17, 52428
  %r19 = call i32 @fcmp_mask_f32(float 0xFFF0000000000000, float 0xC7EFFFFFE0000000) ; 0xF0F0
  %k20 = icmp eq i32 %r19, 61680
  %r21 = call i32 @fcmp_mask_f32(float 0x36A0000000000000, float 0.0) ; 0xCCCC
  %k22 = icmp eq i32 %r21, 52428
  %r23 = call i32 @fcmp_mask_f32(float 0xB6A0000000000000, float 0.0) ; 0xF0F0
  %k24 = icmp eq i32 %r23, 61680
  %r25 = call i32 @fcmp_mask_f32(float 0x3FD5555560000000, float 0x3FD5555560000000) ; 0xAAAA
  %k26 = icmp eq i32 %r25, 43690
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %all6 = and i1 %all5, %k14
  %all7 = and i1 %all6, %k16
  %all8 = and i1 %all7, %k18
  %all9 = and i1 %all8, %k20
  %all10 = and i1 %all9, %k22
  %all11 = and i1 %all10, %k24
  %all12 = and i1 %all11, %k26
  %bad = xor i1 %all12, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 4: compare results used as integers: sext (-1), zext, and the
; negation 'xor i1 c, true' of olt (uge) and of uge (olt), with and
; without NaN; then compares against constants on either side.
define internal i32 @check_values() noinline {
  %r1 = call i32 @fcmp_values_f64(double 1.0, double 2.0)          ; -1010
  %k2 = icmp eq i32 %r1, -1010
  %r3 = call i32 @fcmp_values_f64(double 2.0, double 1.0)          ; 100
  %k4 = icmp eq i32 %r3, 100
  %r5 = call i32 @fcmp_values_f64(double 3.0, double 3.0)          ; 101
  %k6 = icmp eq i32 %r5, 101
  %r7 = call i32 @fcmp_values_f64(double 0x7FF8000000000000, double 1.0) ; 100
  %k8 = icmp eq i32 %r7, 100
  %r9 = call i32 @fcmp_values_f64(double 1.0, double 0x7FF8000000000000) ; 100
  %k10 = icmp eq i32 %r9, 100
  %r11 = call i32 @fcmp_const_f64(double -1.0)                     ; 0x1F
  %k12 = icmp eq i32 %r11, 31
  %r13 = call i32 @fcmp_const_f64(double 0.0)                      ; 0x1E
  %k14 = icmp eq i32 %r13, 30
  %r15 = call i32 @fcmp_const_f64(double -0.0)                     ; 0x1E
  %k16 = icmp eq i32 %r15, 30
  %r17 = call i32 @fcmp_const_f64(double 1.0)                      ; 0x14
  %k18 = icmp eq i32 %r17, 20
  %r19 = call i32 @fcmp_const_f64(double 0.5)                      ; 0x1E
  %k20 = icmp eq i32 %r19, 30
  %r21 = call i32 @fcmp_const_f64(double -2.5)                     ; 0x1F
  %k22 = icmp eq i32 %r21, 31
  %r23 = call i32 @fcmp_const_f64(double -3.0)                     ; 0x1B
  %k24 = icmp eq i32 %r23, 27
  %r25 = call i32 @fcmp_const_f64(double 0x7FF0000000000000)       ; 0x5C
  %k26 = icmp eq i32 %r25, 92
  %r27 = call i32 @fcmp_const_f64(double 0xFFF0000000000000)       ; 0x1B
  %k28 = icmp eq i32 %r27, 27
  %r29 = call i32 @fcmp_const_f64(double 0x7FF8000000000000)       ; 0xAA
  %k30 = icmp eq i32 %r29, 170
  %r31 = call i32 @fcmp_values_f32(float 1.0, float 2.0)           ; -1010
  %k32 = icmp eq i32 %r31, -1010
  %r33 = call i32 @fcmp_values_f32(float 2.0, float 1.0)           ; 100
  %k34 = icmp eq i32 %r33, 100
  %r35 = call i32 @fcmp_values_f32(float 3.0, float 3.0)           ; 101
  %k36 = icmp eq i32 %r35, 101
  %r37 = call i32 @fcmp_values_f32(float 0x7FF8000000000000, float 1.0) ; 100
  %k38 = icmp eq i32 %r37, 100
  %r39 = call i32 @fcmp_values_f32(float 1.0, float 0x7FF8000000000000) ; 100
  %k40 = icmp eq i32 %r39, 100
  %r41 = call i32 @fcmp_const_f32(float -1.0)                      ; 0x1F
  %k42 = icmp eq i32 %r41, 31
  %r43 = call i32 @fcmp_const_f32(float 0.0)                       ; 0x1E
  %k44 = icmp eq i32 %r43, 30
  %r45 = call i32 @fcmp_const_f32(float -0.0)                      ; 0x1E
  %k46 = icmp eq i32 %r45, 30
  %r47 = call i32 @fcmp_const_f32(float 1.0)                       ; 0x14
  %k48 = icmp eq i32 %r47, 20
  %r49 = call i32 @fcmp_const_f32(float 0.5)                       ; 0x1E
  %k50 = icmp eq i32 %r49, 30
  %r51 = call i32 @fcmp_const_f32(float -2.5)                      ; 0x1F
  %k52 = icmp eq i32 %r51, 31
  %r53 = call i32 @fcmp_const_f32(float -3.0)                      ; 0x1B
  %k54 = icmp eq i32 %r53, 27
  %r55 = call i32 @fcmp_const_f32(float 0x7FF0000000000000)        ; 0x5C
  %k56 = icmp eq i32 %r55, 92
  %r57 = call i32 @fcmp_const_f32(float 0xFFF0000000000000)        ; 0x1B
  %k58 = icmp eq i32 %r57, 27
  %r59 = call i32 @fcmp_const_f32(float 0x7FF8000000000000)        ; 0xAA
  %k60 = icmp eq i32 %r59, 170
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %all6 = and i1 %all5, %k14
  %all7 = and i1 %all6, %k16
  %all8 = and i1 %all7, %k18
  %all9 = and i1 %all8, %k20
  %all10 = and i1 %all9, %k22
  %all11 = and i1 %all10, %k24
  %all12 = and i1 %all11, %k26
  %all13 = and i1 %all12, %k28
  %all14 = and i1 %all13, %k30
  %all15 = and i1 %all14, %k32
  %all16 = and i1 %all15, %k34
  %all17 = and i1 %all16, %k36
  %all18 = and i1 %all17, %k38
  %all19 = and i1 %all18, %k40
  %all20 = and i1 %all19, %k42
  %all21 = and i1 %all20, %k44
  %all22 = and i1 %all21, %k46
  %all23 = and i1 %all22, %k48
  %all24 = and i1 %all23, %k50
  %all25 = and i1 %all24, %k52
  %all26 = and i1 %all25, %k54
  %all27 = and i1 %all26, %k56
  %all28 = and i1 %all27, %k58
  %all29 = and i1 %all28, %k60
  %bad = xor i1 %all29, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 5: NaN computed at run time (0 / 0, inf - inf), and NaN through
; fpext and fptrunc, is unordered with everything, itself included.
define internal i32 @check_runtime_nan() noinline {
  %r1 = call double @fdiv_f64(double 0.0, double 0.0)              ; NaN
  %r2 = call double @fsub_f64(double 0x7FF0000000000000, double 0x7FF0000000000000) ; NaN
  %r3 = call i32 @fcmp_mask_f64(double %r1, double 1.0)            ; 0xFF00
  %k4 = icmp eq i32 %r3, 65280
  %r5 = call i32 @fcmp_mask_f64(double %r2, double 0.0)            ; 0xFF00
  %k6 = icmp eq i32 %r5, 65280
  %r7 = call i32 @fcmp_mask_f64(double %r1, double %r2)            ; 0xFF00
  %k8 = icmp eq i32 %r7, 65280
  %r9 = call i32 @fcmp_mask_f64(double %r2, double %r2)            ; 0xFF00
  %k10 = icmp eq i32 %r9, 65280
  %r11 = call float @fdiv_f32(float 0.0, float 0.0)                ; NaN
  %r12 = call float @fsub_f32(float 0x7FF0000000000000, float 0x7FF0000000000000) ; NaN
  %r13 = call i32 @fcmp_mask_f32(float %r11, float 1.0)            ; 0xFF00
  %k14 = icmp eq i32 %r13, 65280
  %r15 = call i32 @fcmp_mask_f32(float %r12, float 0.0)            ; 0xFF00
  %k16 = icmp eq i32 %r15, 65280
  %r17 = call i32 @fcmp_mask_f32(float %r11, float %r12)           ; 0xFF00
  %k18 = icmp eq i32 %r17, 65280
  %r19 = call i32 @fcmp_mask_f32(float %r12, float %r12)           ; 0xFF00
  %k20 = icmp eq i32 %r19, 65280
  %r21 = call double @fpext(float 0x7FF8000000000000)              ; NaN
  %r22 = call i32 @fcmp_mask_f64(double %r21, double %r21)         ; 0xFF00
  %k23 = icmp eq i32 %r22, 65280
  %r24 = call float @fptrunc(double 0x7FF8000000000123)            ; NaN
  %r25 = call i32 @fcmp_mask_f32(float %r24, float 0.0)            ; 0xFF00
  %k26 = icmp eq i32 %r25, 65280
  %r27 = call double @fpext(float 0x7FF0000000000000)              ; inf
  %r28 = call i32 @fcmp_mask_f64(double %r27, double 0x7FF0000000000000) ; 0xAAAA
  %k29 = icmp eq i32 %r28, 43690
  %all1 = and i1 %k4, %k6
  %all2 = and i1 %all1, %k8
  %all3 = and i1 %all2, %k10
  %all4 = and i1 %all3, %k14
  %all5 = and i1 %all4, %k16
  %all6 = and i1 %all5, %k18
  %all7 = and i1 %all6, %k20
  %all8 = and i1 %all7, %k23
  %all9 = and i1 %all8, %k26
  %all10 = and i1 %all9, %k29
  %bad = xor i1 %all10, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 6: each predicate alone in its own method (float64), for an
; ordered less, greater and equal pair and for a NaN operand.
define internal i32 @check_single_f64() noinline {
  %r1 = call i32 @fcmp_oeq_f64(double 1.0, double 2.0)             ; 0
  %k2 = icmp eq i32 %r1, 0
  %r3 = call i32 @fcmp_oeq_f64(double 2.0, double 1.0)             ; 0
  %k4 = icmp eq i32 %r3, 0
  %r5 = call i32 @fcmp_oeq_f64(double -0.0, double 0.0)            ; 1
  %k6 = icmp eq i32 %r5, 1
  %r7 = call i32 @fcmp_oeq_f64(double 0x7FF8000000000000, double 1.0) ; 0
  %k8 = icmp eq i32 %r7, 0
  %r9 = call i32 @fcmp_ogt_f64(double 1.0, double 2.0)             ; 0
  %k10 = icmp eq i32 %r9, 0
  %r11 = call i32 @fcmp_ogt_f64(double 2.0, double 1.0)            ; 1
  %k12 = icmp eq i32 %r11, 1
  %r13 = call i32 @fcmp_ogt_f64(double -0.0, double 0.0)           ; 0
  %k14 = icmp eq i32 %r13, 0
  %r15 = call i32 @fcmp_ogt_f64(double 0x7FF8000000000000, double 1.0) ; 0
  %k16 = icmp eq i32 %r15, 0
  %r17 = call i32 @fcmp_oge_f64(double 1.0, double 2.0)            ; 0
  %k18 = icmp eq i32 %r17, 0
  %r19 = call i32 @fcmp_oge_f64(double 2.0, double 1.0)            ; 1
  %k20 = icmp eq i32 %r19, 1
  %r21 = call i32 @fcmp_oge_f64(double -0.0, double 0.0)           ; 1
  %k22 = icmp eq i32 %r21, 1
  %r23 = call i32 @fcmp_oge_f64(double 0x7FF8000000000000, double 1.0) ; 0
  %k24 = icmp eq i32 %r23, 0
  %r25 = call i32 @fcmp_olt_f64(double 1.0, double 2.0)            ; 1
  %k26 = icmp eq i32 %r25, 1
  %r27 = call i32 @fcmp_olt_f64(double 2.0, double 1.0)            ; 0
  %k28 = icmp eq i32 %r27, 0
  %r29 = call i32 @fcmp_olt_f64(double -0.0, double 0.0)           ; 0
  %k30 = icmp eq i32 %r29, 0
  %r31 = call i32 @fcmp_olt_f64(double 0x7FF8000000000000, double 1.0) ; 0
  %k32 = icmp eq i32 %r31, 0
  %r33 = call i32 @fcmp_ole_f64(double 1.0, double 2.0)            ; 1
  %k34 = icmp eq i32 %r33, 1
  %r35 = call i32 @fcmp_ole_f64(double 2.0, double 1.0)            ; 0
  %k36 = icmp eq i32 %r35, 0
  %r37 = call i32 @fcmp_ole_f64(double -0.0, double 0.0)           ; 1
  %k38 = icmp eq i32 %r37, 1
  %r39 = call i32 @fcmp_ole_f64(double 0x7FF8000000000000, double 1.0) ; 0
  %k40 = icmp eq i32 %r39, 0
  %r41 = call i32 @fcmp_one_f64(double 1.0, double 2.0)            ; 1
  %k42 = icmp eq i32 %r41, 1
  %r43 = call i32 @fcmp_one_f64(double 2.0, double 1.0)            ; 1
  %k44 = icmp eq i32 %r43, 1
  %r45 = call i32 @fcmp_one_f64(double -0.0, double 0.0)           ; 0
  %k46 = icmp eq i32 %r45, 0
  %r47 = call i32 @fcmp_one_f64(double 0x7FF8000000000000, double 1.0) ; 0
  %k48 = icmp eq i32 %r47, 0
  %r49 = call i32 @fcmp_ord_f64(double 1.0, double 2.0)            ; 1
  %k50 = icmp eq i32 %r49, 1
  %r51 = call i32 @fcmp_ord_f64(double 2.0, double 1.0)            ; 1
  %k52 = icmp eq i32 %r51, 1
  %r53 = call i32 @fcmp_ord_f64(double -0.0, double 0.0)           ; 1
  %k54 = icmp eq i32 %r53, 1
  %r55 = call i32 @fcmp_ord_f64(double 0x7FF8000000000000, double 1.0) ; 0
  %k56 = icmp eq i32 %r55, 0
  %r57 = call i32 @fcmp_uno_f64(double 1.0, double 2.0)            ; 0
  %k58 = icmp eq i32 %r57, 0
  %r59 = call i32 @fcmp_uno_f64(double 2.0, double 1.0)            ; 0
  %k60 = icmp eq i32 %r59, 0
  %r61 = call i32 @fcmp_uno_f64(double -0.0, double 0.0)           ; 0
  %k62 = icmp eq i32 %r61, 0
  %r63 = call i32 @fcmp_uno_f64(double 0x7FF8000000000000, double 1.0) ; 1
  %k64 = icmp eq i32 %r63, 1
  %r65 = call i32 @fcmp_ueq_f64(double 1.0, double 2.0)            ; 0
  %k66 = icmp eq i32 %r65, 0
  %r67 = call i32 @fcmp_ueq_f64(double 2.0, double 1.0)            ; 0
  %k68 = icmp eq i32 %r67, 0
  %r69 = call i32 @fcmp_ueq_f64(double -0.0, double 0.0)           ; 1
  %k70 = icmp eq i32 %r69, 1
  %r71 = call i32 @fcmp_ueq_f64(double 0x7FF8000000000000, double 1.0) ; 1
  %k72 = icmp eq i32 %r71, 1
  %r73 = call i32 @fcmp_ugt_f64(double 1.0, double 2.0)            ; 0
  %k74 = icmp eq i32 %r73, 0
  %r75 = call i32 @fcmp_ugt_f64(double 2.0, double 1.0)            ; 1
  %k76 = icmp eq i32 %r75, 1
  %r77 = call i32 @fcmp_ugt_f64(double -0.0, double 0.0)           ; 0
  %k78 = icmp eq i32 %r77, 0
  %r79 = call i32 @fcmp_ugt_f64(double 0x7FF8000000000000, double 1.0) ; 1
  %k80 = icmp eq i32 %r79, 1
  %r81 = call i32 @fcmp_uge_f64(double 1.0, double 2.0)            ; 0
  %k82 = icmp eq i32 %r81, 0
  %r83 = call i32 @fcmp_uge_f64(double 2.0, double 1.0)            ; 1
  %k84 = icmp eq i32 %r83, 1
  %r85 = call i32 @fcmp_uge_f64(double -0.0, double 0.0)           ; 1
  %k86 = icmp eq i32 %r85, 1
  %r87 = call i32 @fcmp_uge_f64(double 0x7FF8000000000000, double 1.0) ; 1
  %k88 = icmp eq i32 %r87, 1
  %r89 = call i32 @fcmp_ult_f64(double 1.0, double 2.0)            ; 1
  %k90 = icmp eq i32 %r89, 1
  %r91 = call i32 @fcmp_ult_f64(double 2.0, double 1.0)            ; 0
  %k92 = icmp eq i32 %r91, 0
  %r93 = call i32 @fcmp_ult_f64(double -0.0, double 0.0)           ; 0
  %k94 = icmp eq i32 %r93, 0
  %r95 = call i32 @fcmp_ult_f64(double 0x7FF8000000000000, double 1.0) ; 1
  %k96 = icmp eq i32 %r95, 1
  %r97 = call i32 @fcmp_ule_f64(double 1.0, double 2.0)            ; 1
  %k98 = icmp eq i32 %r97, 1
  %r99 = call i32 @fcmp_ule_f64(double 2.0, double 1.0)            ; 0
  %k100 = icmp eq i32 %r99, 0
  %r101 = call i32 @fcmp_ule_f64(double -0.0, double 0.0)          ; 1
  %k102 = icmp eq i32 %r101, 1
  %r103 = call i32 @fcmp_ule_f64(double 0x7FF8000000000000, double 1.0) ; 1
  %k104 = icmp eq i32 %r103, 1
  %r105 = call i32 @fcmp_une_f64(double 1.0, double 2.0)           ; 1
  %k106 = icmp eq i32 %r105, 1
  %r107 = call i32 @fcmp_une_f64(double 2.0, double 1.0)           ; 1
  %k108 = icmp eq i32 %r107, 1
  %r109 = call i32 @fcmp_une_f64(double -0.0, double 0.0)          ; 0
  %k110 = icmp eq i32 %r109, 0
  %r111 = call i32 @fcmp_une_f64(double 0x7FF8000000000000, double 1.0) ; 1
  %k112 = icmp eq i32 %r111, 1
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %all6 = and i1 %all5, %k14
  %all7 = and i1 %all6, %k16
  %all8 = and i1 %all7, %k18
  %all9 = and i1 %all8, %k20
  %all10 = and i1 %all9, %k22
  %all11 = and i1 %all10, %k24
  %all12 = and i1 %all11, %k26
  %all13 = and i1 %all12, %k28
  %all14 = and i1 %all13, %k30
  %all15 = and i1 %all14, %k32
  %all16 = and i1 %all15, %k34
  %all17 = and i1 %all16, %k36
  %all18 = and i1 %all17, %k38
  %all19 = and i1 %all18, %k40
  %all20 = and i1 %all19, %k42
  %all21 = and i1 %all20, %k44
  %all22 = and i1 %all21, %k46
  %all23 = and i1 %all22, %k48
  %all24 = and i1 %all23, %k50
  %all25 = and i1 %all24, %k52
  %all26 = and i1 %all25, %k54
  %all27 = and i1 %all26, %k56
  %all28 = and i1 %all27, %k58
  %all29 = and i1 %all28, %k60
  %all30 = and i1 %all29, %k62
  %all31 = and i1 %all30, %k64
  %all32 = and i1 %all31, %k66
  %all33 = and i1 %all32, %k68
  %all34 = and i1 %all33, %k70
  %all35 = and i1 %all34, %k72
  %all36 = and i1 %all35, %k74
  %all37 = and i1 %all36, %k76
  %all38 = and i1 %all37, %k78
  %all39 = and i1 %all38, %k80
  %all40 = and i1 %all39, %k82
  %all41 = and i1 %all40, %k84
  %all42 = and i1 %all41, %k86
  %all43 = and i1 %all42, %k88
  %all44 = and i1 %all43, %k90
  %all45 = and i1 %all44, %k92
  %all46 = and i1 %all45, %k94
  %all47 = and i1 %all46, %k96
  %all48 = and i1 %all47, %k98
  %all49 = and i1 %all48, %k100
  %all50 = and i1 %all49, %k102
  %all51 = and i1 %all50, %k104
  %all52 = and i1 %all51, %k106
  %all53 = and i1 %all52, %k108
  %all54 = and i1 %all53, %k110
  %all55 = and i1 %all54, %k112
  %bad = xor i1 %all55, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 7: each predicate alone in its own method (float32), for an
; ordered less, greater and equal pair and for a NaN operand.
define internal i32 @check_single_f32() noinline {
  %r1 = call i32 @fcmp_oeq_f32(float 1.0, float 2.0)               ; 0
  %k2 = icmp eq i32 %r1, 0
  %r3 = call i32 @fcmp_oeq_f32(float 2.0, float 1.0)               ; 0
  %k4 = icmp eq i32 %r3, 0
  %r5 = call i32 @fcmp_oeq_f32(float -0.0, float 0.0)              ; 1
  %k6 = icmp eq i32 %r5, 1
  %r7 = call i32 @fcmp_oeq_f32(float 0x7FF8000000000000, float 1.0) ; 0
  %k8 = icmp eq i32 %r7, 0
  %r9 = call i32 @fcmp_ogt_f32(float 1.0, float 2.0)               ; 0
  %k10 = icmp eq i32 %r9, 0
  %r11 = call i32 @fcmp_ogt_f32(float 2.0, float 1.0)              ; 1
  %k12 = icmp eq i32 %r11, 1
  %r13 = call i32 @fcmp_ogt_f32(float -0.0, float 0.0)             ; 0
  %k14 = icmp eq i32 %r13, 0
  %r15 = call i32 @fcmp_ogt_f32(float 0x7FF8000000000000, float 1.0) ; 0
  %k16 = icmp eq i32 %r15, 0
  %r17 = call i32 @fcmp_oge_f32(float 1.0, float 2.0)              ; 0
  %k18 = icmp eq i32 %r17, 0
  %r19 = call i32 @fcmp_oge_f32(float 2.0, float 1.0)              ; 1
  %k20 = icmp eq i32 %r19, 1
  %r21 = call i32 @fcmp_oge_f32(float -0.0, float 0.0)             ; 1
  %k22 = icmp eq i32 %r21, 1
  %r23 = call i32 @fcmp_oge_f32(float 0x7FF8000000000000, float 1.0) ; 0
  %k24 = icmp eq i32 %r23, 0
  %r25 = call i32 @fcmp_olt_f32(float 1.0, float 2.0)              ; 1
  %k26 = icmp eq i32 %r25, 1
  %r27 = call i32 @fcmp_olt_f32(float 2.0, float 1.0)              ; 0
  %k28 = icmp eq i32 %r27, 0
  %r29 = call i32 @fcmp_olt_f32(float -0.0, float 0.0)             ; 0
  %k30 = icmp eq i32 %r29, 0
  %r31 = call i32 @fcmp_olt_f32(float 0x7FF8000000000000, float 1.0) ; 0
  %k32 = icmp eq i32 %r31, 0
  %r33 = call i32 @fcmp_ole_f32(float 1.0, float 2.0)              ; 1
  %k34 = icmp eq i32 %r33, 1
  %r35 = call i32 @fcmp_ole_f32(float 2.0, float 1.0)              ; 0
  %k36 = icmp eq i32 %r35, 0
  %r37 = call i32 @fcmp_ole_f32(float -0.0, float 0.0)             ; 1
  %k38 = icmp eq i32 %r37, 1
  %r39 = call i32 @fcmp_ole_f32(float 0x7FF8000000000000, float 1.0) ; 0
  %k40 = icmp eq i32 %r39, 0
  %r41 = call i32 @fcmp_one_f32(float 1.0, float 2.0)              ; 1
  %k42 = icmp eq i32 %r41, 1
  %r43 = call i32 @fcmp_one_f32(float 2.0, float 1.0)              ; 1
  %k44 = icmp eq i32 %r43, 1
  %r45 = call i32 @fcmp_one_f32(float -0.0, float 0.0)             ; 0
  %k46 = icmp eq i32 %r45, 0
  %r47 = call i32 @fcmp_one_f32(float 0x7FF8000000000000, float 1.0) ; 0
  %k48 = icmp eq i32 %r47, 0
  %r49 = call i32 @fcmp_ord_f32(float 1.0, float 2.0)              ; 1
  %k50 = icmp eq i32 %r49, 1
  %r51 = call i32 @fcmp_ord_f32(float 2.0, float 1.0)              ; 1
  %k52 = icmp eq i32 %r51, 1
  %r53 = call i32 @fcmp_ord_f32(float -0.0, float 0.0)             ; 1
  %k54 = icmp eq i32 %r53, 1
  %r55 = call i32 @fcmp_ord_f32(float 0x7FF8000000000000, float 1.0) ; 0
  %k56 = icmp eq i32 %r55, 0
  %r57 = call i32 @fcmp_uno_f32(float 1.0, float 2.0)              ; 0
  %k58 = icmp eq i32 %r57, 0
  %r59 = call i32 @fcmp_uno_f32(float 2.0, float 1.0)              ; 0
  %k60 = icmp eq i32 %r59, 0
  %r61 = call i32 @fcmp_uno_f32(float -0.0, float 0.0)             ; 0
  %k62 = icmp eq i32 %r61, 0
  %r63 = call i32 @fcmp_uno_f32(float 0x7FF8000000000000, float 1.0) ; 1
  %k64 = icmp eq i32 %r63, 1
  %r65 = call i32 @fcmp_ueq_f32(float 1.0, float 2.0)              ; 0
  %k66 = icmp eq i32 %r65, 0
  %r67 = call i32 @fcmp_ueq_f32(float 2.0, float 1.0)              ; 0
  %k68 = icmp eq i32 %r67, 0
  %r69 = call i32 @fcmp_ueq_f32(float -0.0, float 0.0)             ; 1
  %k70 = icmp eq i32 %r69, 1
  %r71 = call i32 @fcmp_ueq_f32(float 0x7FF8000000000000, float 1.0) ; 1
  %k72 = icmp eq i32 %r71, 1
  %r73 = call i32 @fcmp_ugt_f32(float 1.0, float 2.0)              ; 0
  %k74 = icmp eq i32 %r73, 0
  %r75 = call i32 @fcmp_ugt_f32(float 2.0, float 1.0)              ; 1
  %k76 = icmp eq i32 %r75, 1
  %r77 = call i32 @fcmp_ugt_f32(float -0.0, float 0.0)             ; 0
  %k78 = icmp eq i32 %r77, 0
  %r79 = call i32 @fcmp_ugt_f32(float 0x7FF8000000000000, float 1.0) ; 1
  %k80 = icmp eq i32 %r79, 1
  %r81 = call i32 @fcmp_uge_f32(float 1.0, float 2.0)              ; 0
  %k82 = icmp eq i32 %r81, 0
  %r83 = call i32 @fcmp_uge_f32(float 2.0, float 1.0)              ; 1
  %k84 = icmp eq i32 %r83, 1
  %r85 = call i32 @fcmp_uge_f32(float -0.0, float 0.0)             ; 1
  %k86 = icmp eq i32 %r85, 1
  %r87 = call i32 @fcmp_uge_f32(float 0x7FF8000000000000, float 1.0) ; 1
  %k88 = icmp eq i32 %r87, 1
  %r89 = call i32 @fcmp_ult_f32(float 1.0, float 2.0)              ; 1
  %k90 = icmp eq i32 %r89, 1
  %r91 = call i32 @fcmp_ult_f32(float 2.0, float 1.0)              ; 0
  %k92 = icmp eq i32 %r91, 0
  %r93 = call i32 @fcmp_ult_f32(float -0.0, float 0.0)             ; 0
  %k94 = icmp eq i32 %r93, 0
  %r95 = call i32 @fcmp_ult_f32(float 0x7FF8000000000000, float 1.0) ; 1
  %k96 = icmp eq i32 %r95, 1
  %r97 = call i32 @fcmp_ule_f32(float 1.0, float 2.0)              ; 1
  %k98 = icmp eq i32 %r97, 1
  %r99 = call i32 @fcmp_ule_f32(float 2.0, float 1.0)              ; 0
  %k100 = icmp eq i32 %r99, 0
  %r101 = call i32 @fcmp_ule_f32(float -0.0, float 0.0)            ; 1
  %k102 = icmp eq i32 %r101, 1
  %r103 = call i32 @fcmp_ule_f32(float 0x7FF8000000000000, float 1.0) ; 1
  %k104 = icmp eq i32 %r103, 1
  %r105 = call i32 @fcmp_une_f32(float 1.0, float 2.0)             ; 1
  %k106 = icmp eq i32 %r105, 1
  %r107 = call i32 @fcmp_une_f32(float 2.0, float 1.0)             ; 1
  %k108 = icmp eq i32 %r107, 1
  %r109 = call i32 @fcmp_une_f32(float -0.0, float 0.0)            ; 0
  %k110 = icmp eq i32 %r109, 0
  %r111 = call i32 @fcmp_une_f32(float 0x7FF8000000000000, float 1.0) ; 1
  %k112 = icmp eq i32 %r111, 1
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %all6 = and i1 %all5, %k14
  %all7 = and i1 %all6, %k16
  %all8 = and i1 %all7, %k18
  %all9 = and i1 %all8, %k20
  %all10 = and i1 %all9, %k22
  %all11 = and i1 %all10, %k24
  %all12 = and i1 %all11, %k26
  %all13 = and i1 %all12, %k28
  %all14 = and i1 %all13, %k30
  %all15 = and i1 %all14, %k32
  %all16 = and i1 %all15, %k34
  %all17 = and i1 %all16, %k36
  %all18 = and i1 %all17, %k38
  %all19 = and i1 %all18, %k40
  %all20 = and i1 %all19, %k42
  %all21 = and i1 %all20, %k44
  %all22 = and i1 %all21, %k46
  %all23 = and i1 %all22, %k48
  %all24 = and i1 %all23, %k50
  %all25 = and i1 %all24, %k52
  %all26 = and i1 %all25, %k54
  %all27 = and i1 %all26, %k56
  %all28 = and i1 %all27, %k58
  %all29 = and i1 %all28, %k60
  %all30 = and i1 %all29, %k62
  %all31 = and i1 %all30, %k64
  %all32 = and i1 %all31, %k66
  %all33 = and i1 %all32, %k68
  %all34 = and i1 %all33, %k70
  %all35 = and i1 %all34, %k72
  %all36 = and i1 %all35, %k74
  %all37 = and i1 %all36, %k76
  %all38 = and i1 %all37, %k78
  %all39 = and i1 %all38, %k80
  %all40 = and i1 %all39, %k82
  %all41 = and i1 %all40, %k84
  %all42 = and i1 %all41, %k86
  %all43 = and i1 %all42, %k88
  %all44 = and i1 %all43, %k90
  %all45 = and i1 %all44, %k92
  %all46 = and i1 %all45, %k94
  %all47 = and i1 %all46, %k96
  %all48 = and i1 %all47, %k98
  %all49 = and i1 %all48, %k100
  %all50 = and i1 %all49, %k102
  %all51 = and i1 %all50, %k104
  %all52 = and i1 %all51, %k106
  %all53 = and i1 %all52, %k108
  %all54 = and i1 %all53, %k110
  %all55 = and i1 %all54, %k112
  %bad = xor i1 %all55, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

define i32 @main() {
  %b0 = call i32 @check_nan_mask_f64()
  %b1 = call i32 @check_nan_mask_f32()
  %s1 = shl i32 %b1, 1
  %b2 = call i32 @check_ordered_mask_f64()
  %s2 = shl i32 %b2, 2
  %b3 = call i32 @check_ordered_mask_f32()
  %s3 = shl i32 %b3, 3
  %b4 = call i32 @check_values()
  %s4 = shl i32 %b4, 4
  %b5 = call i32 @check_runtime_nan()
  %s5 = shl i32 %b5, 5
  %b6 = call i32 @check_single_f64()
  %s6 = shl i32 %b6, 6
  %b7 = call i32 @check_single_f32()
  %s7 = shl i32 %b7, 7
  %m1 = or i32 %b0, %s1
  %m2 = or i32 %m1, %s2
  %m3 = or i32 %m2, %s3
  %m4 = or i32 %m3, %s4
  %m5 = or i32 %m4, %s5
  %m6 = or i32 %m5, %s6
  %m7 = or i32 %m6, %s7
  ret i32 %m7
}
