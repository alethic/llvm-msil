; llvm.minimum, llvm.maximum, llvm.minimumnum and llvm.maximumnum on float64
; and float32 with one constant operand (Milestone 5; plan section 12, row
; 'M5.a amended (minimum and maximum)'). fp-minmax.ll passes two run-time
; operands, so either may be a NaN; here the constant c (1.0, or a zero) is
; known not to be a NaN, which selects a different lowering: one compare of x
; with c, unordered for minimum and maximum (a NaN x must give a NaN) and
; ordered for minimumnum and maximumnum (a NaN x must give c), plus the sign
; test of a zero result when c is a zero. minimum and maximum take c as the
; second operand, minimumnum and maximumnum as the first. Before M7 there is
; no fabs, copysign or bitcast (plan 5.5): -0.0 is made as 0.0 * -1.0 and
; NaN as 0.0 / 0.0 in noinline functions, a NaN result is tested with fcmp
; uno, and the sign of a zero result through 1.0 / r == -inf or +inf. Each
; noinline check function returns 1 when a result is wrong; main ORs one bit
; per check into the exit status (8 bits, D37): [exit 0] means every check
; passed.
; RUN: %{msil_ll}

define double @minimum_x_one_f64(double %x) noinline {
  %r = call double @llvm.minimum.f64(double %x, double 1.0)
  ret double %r
}

define double @minimum_x_zero_f64(double %x) noinline {
  %r = call double @llvm.minimum.f64(double %x, double 0.0)
  ret double %r
}

define float @minimum_x_one_f32(float %x) noinline {
  %r = call float @llvm.minimum.f32(float %x, float 1.0)
  ret float %r
}

define float @minimum_x_zero_f32(float %x) noinline {
  %r = call float @llvm.minimum.f32(float %x, float 0.0)
  ret float %r
}

define double @maximum_x_one_f64(double %x) noinline {
  %r = call double @llvm.maximum.f64(double %x, double 1.0)
  ret double %r
}

define double @maximum_x_negzero_f64(double %x) noinline {
  %r = call double @llvm.maximum.f64(double %x, double -0.0)
  ret double %r
}

define float @maximum_x_one_f32(float %x) noinline {
  %r = call float @llvm.maximum.f32(float %x, float 1.0)
  ret float %r
}

define float @maximum_x_negzero_f32(float %x) noinline {
  %r = call float @llvm.maximum.f32(float %x, float -0.0)
  ret float %r
}

define double @minimumnum_one_x_f64(double %x) noinline {
  %r = call double @llvm.minimumnum.f64(double 1.0, double %x)
  ret double %r
}

define double @minimumnum_zero_x_f64(double %x) noinline {
  %r = call double @llvm.minimumnum.f64(double 0.0, double %x)
  ret double %r
}

define float @minimumnum_one_x_f32(float %x) noinline {
  %r = call float @llvm.minimumnum.f32(float 1.0, float %x)
  ret float %r
}

define float @minimumnum_zero_x_f32(float %x) noinline {
  %r = call float @llvm.minimumnum.f32(float 0.0, float %x)
  ret float %r
}

define double @maximumnum_one_x_f64(double %x) noinline {
  %r = call double @llvm.maximumnum.f64(double 1.0, double %x)
  ret double %r
}

define double @maximumnum_negzero_x_f64(double %x) noinline {
  %r = call double @llvm.maximumnum.f64(double -0.0, double %x)
  ret double %r
}

define float @maximumnum_one_x_f32(float %x) noinline {
  %r = call float @llvm.maximumnum.f32(float 1.0, float %x)
  ret float %r
}

define float @maximumnum_negzero_x_f32(float %x) noinline {
  %r = call float @llvm.maximumnum.f32(float -0.0, float %x)
  ret float %r
}

define double @fmul_f64(double %a, double %b) noinline {
  %r = fmul double %a, %b
  ret double %r
}

define double @fdiv_f64(double %a, double %b) noinline {
  %r = fdiv double %a, %b
  ret double %r
}

define float @fmul_f32(float %a, float %b) noinline {
  %r = fmul float %a, %b
  ret float %r
}

define float @fdiv_f32(float %a, float %b) noinline {
  %r = fdiv float %a, %b
  ret float %r
}

; Bit 0: llvm.minimum.f64(x, c) with c = 1.0 and c = 0.0, where only x can be
; a NaN: a NaN x gives a NaN, so the compare must be the unordered one; x =
; -0.0 against c = 0.0 gives -0.0 (-0.0 orders below +0.0); and ordinary x.
define internal i32 @check_minimum_f64() noinline {
  %r1 = call double @fmul_f64(double 0.0, double -1.0)             ; 0.0 * -1.0 = -0.0
  %r2 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %r3 = call double @minimum_x_one_f64(double %r2)                 ; (NaN, 1.0) = NaN
  %k4 = fcmp uno double %r3, 0.0
  %r5 = call double @minimum_x_one_f64(double 0.5)                 ; (0.5, 1.0) = 0.5
  %k6 = fcmp oeq double %r5, 0.5
  %r7 = call double @minimum_x_one_f64(double 2.0)                 ; (2.0, 1.0) = 1.0
  %k8 = fcmp oeq double %r7, 1.0
  %r9 = call double @minimum_x_one_f64(double 1.0)                 ; (1.0, 1.0) = 1.0
  %k10 = fcmp oeq double %r9, 1.0
  %r11 = call double @minimum_x_one_f64(double -3.0)               ; (-3.0, 1.0) = -3.0
  %k12 = fcmp oeq double %r11, -3.0
  %r13 = call double @minimum_x_one_f64(double 0xFFF0000000000000) ; (-inf, 1.0) = -inf
  %k14 = fcmp oeq double %r13, 0xFFF0000000000000
  %r15 = call double @minimum_x_one_f64(double 0x7FF0000000000000) ; (inf, 1.0) = 1.0
  %k16 = fcmp oeq double %r15, 1.0
  %r17 = call double @minimum_x_one_f64(double %r1)                ; (-0.0, 1.0) = -0.0
  %k18 = fcmp oeq double %r17, -0.0
  %q19 = fdiv double 1.0, %r17
  %k20 = fcmp oeq double %q19, 0xFFF0000000000000
  %r21 = call double @minimum_x_one_f64(double 0.0)                ; (0.0, 1.0) = 0.0
  %k22 = fcmp oeq double %r21, 0.0
  %q23 = fdiv double 1.0, %r21
  %k24 = fcmp oeq double %q23, 0x7FF0000000000000
  %r25 = call double @minimum_x_zero_f64(double %r2)               ; (NaN, 0.0) = NaN
  %k26 = fcmp uno double %r25, 0.0
  %r27 = call double @minimum_x_zero_f64(double %r1)               ; (-0.0, 0.0) = -0.0
  %k28 = fcmp oeq double %r27, -0.0
  %q29 = fdiv double 1.0, %r27
  %k30 = fcmp oeq double %q29, 0xFFF0000000000000
  %r31 = call double @minimum_x_zero_f64(double 0.0)               ; (0.0, 0.0) = 0.0
  %k32 = fcmp oeq double %r31, 0.0
  %q33 = fdiv double 1.0, %r31
  %k34 = fcmp oeq double %q33, 0x7FF0000000000000
  %r35 = call double @minimum_x_zero_f64(double -1.0)              ; (-1.0, 0.0) = -1.0
  %k36 = fcmp oeq double %r35, -1.0
  %r37 = call double @minimum_x_zero_f64(double 2.0)               ; (2.0, 0.0) = 0.0
  %k38 = fcmp oeq double %r37, 0.0
  %q39 = fdiv double 1.0, %r37
  %k40 = fcmp oeq double %q39, 0x7FF0000000000000
  %r41 = call double @minimum_x_zero_f64(double 0xFFF0000000000000) ; (-inf, 0.0) = -inf
  %k42 = fcmp oeq double %r41, 0xFFF0000000000000
  %r43 = call double @minimum_x_zero_f64(double 0x7FF0000000000000) ; (inf, 0.0) = 0.0
  %k44 = fcmp oeq double %r43, 0.0
  %q45 = fdiv double 1.0, %r43
  %k46 = fcmp oeq double %q45, 0x7FF0000000000000
  %all1 = and i1 %k4, %k6
  %all2 = and i1 %all1, %k8
  %all3 = and i1 %all2, %k10
  %all4 = and i1 %all3, %k12
  %all5 = and i1 %all4, %k14
  %all6 = and i1 %all5, %k16
  %all7 = and i1 %all6, %k18
  %all8 = and i1 %all7, %k20
  %all9 = and i1 %all8, %k22
  %all10 = and i1 %all9, %k24
  %all11 = and i1 %all10, %k26
  %all12 = and i1 %all11, %k28
  %all13 = and i1 %all12, %k30
  %all14 = and i1 %all13, %k32
  %all15 = and i1 %all14, %k34
  %all16 = and i1 %all15, %k36
  %all17 = and i1 %all16, %k38
  %all18 = and i1 %all17, %k40
  %all19 = and i1 %all18, %k42
  %all20 = and i1 %all19, %k44
  %all21 = and i1 %all20, %k46
  %bad = xor i1 %all21, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 1: llvm.minimum.f32(x, c) with c = 1.0 and c = 0.0, where only x can be
; a NaN: a NaN x gives a NaN, so the compare must be the unordered one; x =
; -0.0 against c = 0.0 gives -0.0 (-0.0 orders below +0.0); and ordinary x.
define internal i32 @check_minimum_f32() noinline {
  %r1 = call float @fmul_f32(float 0.0, float -1.0)                ; 0.0 * -1.0 = -0.0
  %r2 = call float @fdiv_f32(float 0.0, float 0.0)                 ; 0.0 / 0.0 = NaN
  %r3 = call float @minimum_x_one_f32(float %r2)                   ; (NaN, 1.0) = NaN
  %k4 = fcmp uno float %r3, 0.0
  %r5 = call float @minimum_x_one_f32(float 0.5)                   ; (0.5, 1.0) = 0.5
  %k6 = fcmp oeq float %r5, 0.5
  %r7 = call float @minimum_x_one_f32(float 2.0)                   ; (2.0, 1.0) = 1.0
  %k8 = fcmp oeq float %r7, 1.0
  %r9 = call float @minimum_x_one_f32(float 1.0)                   ; (1.0, 1.0) = 1.0
  %k10 = fcmp oeq float %r9, 1.0
  %r11 = call float @minimum_x_one_f32(float -3.0)                 ; (-3.0, 1.0) = -3.0
  %k12 = fcmp oeq float %r11, -3.0
  %r13 = call float @minimum_x_one_f32(float 0xFFF0000000000000)   ; (-inf, 1.0) = -inf
  %k14 = fcmp oeq float %r13, 0xFFF0000000000000
  %r15 = call float @minimum_x_one_f32(float 0x7FF0000000000000)   ; (inf, 1.0) = 1.0
  %k16 = fcmp oeq float %r15, 1.0
  %r17 = call float @minimum_x_one_f32(float %r1)                  ; (-0.0, 1.0) = -0.0
  %k18 = fcmp oeq float %r17, -0.0
  %q19 = fdiv float 1.0, %r17
  %k20 = fcmp oeq float %q19, 0xFFF0000000000000
  %r21 = call float @minimum_x_one_f32(float 0.0)                  ; (0.0, 1.0) = 0.0
  %k22 = fcmp oeq float %r21, 0.0
  %q23 = fdiv float 1.0, %r21
  %k24 = fcmp oeq float %q23, 0x7FF0000000000000
  %r25 = call float @minimum_x_zero_f32(float %r2)                 ; (NaN, 0.0) = NaN
  %k26 = fcmp uno float %r25, 0.0
  %r27 = call float @minimum_x_zero_f32(float %r1)                 ; (-0.0, 0.0) = -0.0
  %k28 = fcmp oeq float %r27, -0.0
  %q29 = fdiv float 1.0, %r27
  %k30 = fcmp oeq float %q29, 0xFFF0000000000000
  %r31 = call float @minimum_x_zero_f32(float 0.0)                 ; (0.0, 0.0) = 0.0
  %k32 = fcmp oeq float %r31, 0.0
  %q33 = fdiv float 1.0, %r31
  %k34 = fcmp oeq float %q33, 0x7FF0000000000000
  %r35 = call float @minimum_x_zero_f32(float -1.0)                ; (-1.0, 0.0) = -1.0
  %k36 = fcmp oeq float %r35, -1.0
  %r37 = call float @minimum_x_zero_f32(float 2.0)                 ; (2.0, 0.0) = 0.0
  %k38 = fcmp oeq float %r37, 0.0
  %q39 = fdiv float 1.0, %r37
  %k40 = fcmp oeq float %q39, 0x7FF0000000000000
  %r41 = call float @minimum_x_zero_f32(float 0xFFF0000000000000)  ; (-inf, 0.0) = -inf
  %k42 = fcmp oeq float %r41, 0xFFF0000000000000
  %r43 = call float @minimum_x_zero_f32(float 0x7FF0000000000000)  ; (inf, 0.0) = 0.0
  %k44 = fcmp oeq float %r43, 0.0
  %q45 = fdiv float 1.0, %r43
  %k46 = fcmp oeq float %q45, 0x7FF0000000000000
  %all1 = and i1 %k4, %k6
  %all2 = and i1 %all1, %k8
  %all3 = and i1 %all2, %k10
  %all4 = and i1 %all3, %k12
  %all5 = and i1 %all4, %k14
  %all6 = and i1 %all5, %k16
  %all7 = and i1 %all6, %k18
  %all8 = and i1 %all7, %k20
  %all9 = and i1 %all8, %k22
  %all10 = and i1 %all9, %k24
  %all11 = and i1 %all10, %k26
  %all12 = and i1 %all11, %k28
  %all13 = and i1 %all12, %k30
  %all14 = and i1 %all13, %k32
  %all15 = and i1 %all14, %k34
  %all16 = and i1 %all15, %k36
  %all17 = and i1 %all16, %k38
  %all18 = and i1 %all17, %k40
  %all19 = and i1 %all18, %k42
  %all20 = and i1 %all19, %k44
  %all21 = and i1 %all20, %k46
  %bad = xor i1 %all21, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 2: llvm.maximum.f64(x, c) with c = 1.0 and c = -0.0, where only x can be
; a NaN: a NaN x gives a NaN, so the compare must be the unordered one; x =
; +0.0 against c = -0.0 gives +0.0 (+0.0 orders above -0.0); and ordinary x.
define internal i32 @check_maximum_f64() noinline {
  %r1 = call double @fmul_f64(double 0.0, double -1.0)             ; 0.0 * -1.0 = -0.0
  %r2 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %r3 = call double @maximum_x_one_f64(double %r2)                 ; (NaN, 1.0) = NaN
  %k4 = fcmp uno double %r3, 0.0
  %r5 = call double @maximum_x_one_f64(double 0.5)                 ; (0.5, 1.0) = 1.0
  %k6 = fcmp oeq double %r5, 1.0
  %r7 = call double @maximum_x_one_f64(double 2.0)                 ; (2.0, 1.0) = 2.0
  %k8 = fcmp oeq double %r7, 2.0
  %r9 = call double @maximum_x_one_f64(double 1.0)                 ; (1.0, 1.0) = 1.0
  %k10 = fcmp oeq double %r9, 1.0
  %r11 = call double @maximum_x_one_f64(double -3.0)               ; (-3.0, 1.0) = 1.0
  %k12 = fcmp oeq double %r11, 1.0
  %r13 = call double @maximum_x_one_f64(double 0xFFF0000000000000) ; (-inf, 1.0) = 1.0
  %k14 = fcmp oeq double %r13, 1.0
  %r15 = call double @maximum_x_one_f64(double 0x7FF0000000000000) ; (inf, 1.0) = inf
  %k16 = fcmp oeq double %r15, 0x7FF0000000000000
  %r17 = call double @maximum_x_one_f64(double %r1)                ; (-0.0, 1.0) = 1.0
  %k18 = fcmp oeq double %r17, 1.0
  %r19 = call double @maximum_x_one_f64(double 0.0)                ; (0.0, 1.0) = 1.0
  %k20 = fcmp oeq double %r19, 1.0
  %r21 = call double @maximum_x_negzero_f64(double %r2)            ; (NaN, -0.0) = NaN
  %k22 = fcmp uno double %r21, 0.0
  %r23 = call double @maximum_x_negzero_f64(double %r1)            ; (-0.0, -0.0) = -0.0
  %k24 = fcmp oeq double %r23, -0.0
  %q25 = fdiv double 1.0, %r23
  %k26 = fcmp oeq double %q25, 0xFFF0000000000000
  %r27 = call double @maximum_x_negzero_f64(double 0.0)            ; (0.0, -0.0) = 0.0
  %k28 = fcmp oeq double %r27, 0.0
  %q29 = fdiv double 1.0, %r27
  %k30 = fcmp oeq double %q29, 0x7FF0000000000000
  %r31 = call double @maximum_x_negzero_f64(double -1.0)           ; (-1.0, -0.0) = -0.0
  %k32 = fcmp oeq double %r31, -0.0
  %q33 = fdiv double 1.0, %r31
  %k34 = fcmp oeq double %q33, 0xFFF0000000000000
  %r35 = call double @maximum_x_negzero_f64(double 2.0)            ; (2.0, -0.0) = 2.0
  %k36 = fcmp oeq double %r35, 2.0
  %r37 = call double @maximum_x_negzero_f64(double 0xFFF0000000000000) ; (-inf, -0.0) = -0.0
  %k38 = fcmp oeq double %r37, -0.0
  %q39 = fdiv double 1.0, %r37
  %k40 = fcmp oeq double %q39, 0xFFF0000000000000
  %r41 = call double @maximum_x_negzero_f64(double 0x7FF0000000000000) ; (inf, -0.0) = inf
  %k42 = fcmp oeq double %r41, 0x7FF0000000000000
  %all1 = and i1 %k4, %k6
  %all2 = and i1 %all1, %k8
  %all3 = and i1 %all2, %k10
  %all4 = and i1 %all3, %k12
  %all5 = and i1 %all4, %k14
  %all6 = and i1 %all5, %k16
  %all7 = and i1 %all6, %k18
  %all8 = and i1 %all7, %k20
  %all9 = and i1 %all8, %k22
  %all10 = and i1 %all9, %k24
  %all11 = and i1 %all10, %k26
  %all12 = and i1 %all11, %k28
  %all13 = and i1 %all12, %k30
  %all14 = and i1 %all13, %k32
  %all15 = and i1 %all14, %k34
  %all16 = and i1 %all15, %k36
  %all17 = and i1 %all16, %k38
  %all18 = and i1 %all17, %k40
  %all19 = and i1 %all18, %k42
  %bad = xor i1 %all19, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 3: llvm.maximum.f32(x, c) with c = 1.0 and c = -0.0, where only x can be
; a NaN: a NaN x gives a NaN, so the compare must be the unordered one; x =
; +0.0 against c = -0.0 gives +0.0 (+0.0 orders above -0.0); and ordinary x.
define internal i32 @check_maximum_f32() noinline {
  %r1 = call float @fmul_f32(float 0.0, float -1.0)                ; 0.0 * -1.0 = -0.0
  %r2 = call float @fdiv_f32(float 0.0, float 0.0)                 ; 0.0 / 0.0 = NaN
  %r3 = call float @maximum_x_one_f32(float %r2)                   ; (NaN, 1.0) = NaN
  %k4 = fcmp uno float %r3, 0.0
  %r5 = call float @maximum_x_one_f32(float 0.5)                   ; (0.5, 1.0) = 1.0
  %k6 = fcmp oeq float %r5, 1.0
  %r7 = call float @maximum_x_one_f32(float 2.0)                   ; (2.0, 1.0) = 2.0
  %k8 = fcmp oeq float %r7, 2.0
  %r9 = call float @maximum_x_one_f32(float 1.0)                   ; (1.0, 1.0) = 1.0
  %k10 = fcmp oeq float %r9, 1.0
  %r11 = call float @maximum_x_one_f32(float -3.0)                 ; (-3.0, 1.0) = 1.0
  %k12 = fcmp oeq float %r11, 1.0
  %r13 = call float @maximum_x_one_f32(float 0xFFF0000000000000)   ; (-inf, 1.0) = 1.0
  %k14 = fcmp oeq float %r13, 1.0
  %r15 = call float @maximum_x_one_f32(float 0x7FF0000000000000)   ; (inf, 1.0) = inf
  %k16 = fcmp oeq float %r15, 0x7FF0000000000000
  %r17 = call float @maximum_x_one_f32(float %r1)                  ; (-0.0, 1.0) = 1.0
  %k18 = fcmp oeq float %r17, 1.0
  %r19 = call float @maximum_x_one_f32(float 0.0)                  ; (0.0, 1.0) = 1.0
  %k20 = fcmp oeq float %r19, 1.0
  %r21 = call float @maximum_x_negzero_f32(float %r2)              ; (NaN, -0.0) = NaN
  %k22 = fcmp uno float %r21, 0.0
  %r23 = call float @maximum_x_negzero_f32(float %r1)              ; (-0.0, -0.0) = -0.0
  %k24 = fcmp oeq float %r23, -0.0
  %q25 = fdiv float 1.0, %r23
  %k26 = fcmp oeq float %q25, 0xFFF0000000000000
  %r27 = call float @maximum_x_negzero_f32(float 0.0)              ; (0.0, -0.0) = 0.0
  %k28 = fcmp oeq float %r27, 0.0
  %q29 = fdiv float 1.0, %r27
  %k30 = fcmp oeq float %q29, 0x7FF0000000000000
  %r31 = call float @maximum_x_negzero_f32(float -1.0)             ; (-1.0, -0.0) = -0.0
  %k32 = fcmp oeq float %r31, -0.0
  %q33 = fdiv float 1.0, %r31
  %k34 = fcmp oeq float %q33, 0xFFF0000000000000
  %r35 = call float @maximum_x_negzero_f32(float 2.0)              ; (2.0, -0.0) = 2.0
  %k36 = fcmp oeq float %r35, 2.0
  %r37 = call float @maximum_x_negzero_f32(float 0xFFF0000000000000) ; (-inf, -0.0) = -0.0
  %k38 = fcmp oeq float %r37, -0.0
  %q39 = fdiv float 1.0, %r37
  %k40 = fcmp oeq float %q39, 0xFFF0000000000000
  %r41 = call float @maximum_x_negzero_f32(float 0x7FF0000000000000) ; (inf, -0.0) = inf
  %k42 = fcmp oeq float %r41, 0x7FF0000000000000
  %all1 = and i1 %k4, %k6
  %all2 = and i1 %all1, %k8
  %all3 = and i1 %all2, %k10
  %all4 = and i1 %all3, %k12
  %all5 = and i1 %all4, %k14
  %all6 = and i1 %all5, %k16
  %all7 = and i1 %all6, %k18
  %all8 = and i1 %all7, %k20
  %all9 = and i1 %all8, %k22
  %all10 = and i1 %all9, %k24
  %all11 = and i1 %all10, %k26
  %all12 = and i1 %all11, %k28
  %all13 = and i1 %all12, %k30
  %all14 = and i1 %all13, %k32
  %all15 = and i1 %all14, %k34
  %all16 = and i1 %all15, %k36
  %all17 = and i1 %all16, %k38
  %all18 = and i1 %all17, %k40
  %all19 = and i1 %all18, %k42
  %bad = xor i1 %all19, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 4: llvm.minimumnum.f64(c, x) with c = 1.0 and c = 0.0, where only x can
; be a NaN: a NaN x gives the constant, so the compare must be the ordered
; one; x = -0.0 against c = 0.0 gives -0.0 (-0.0 orders below +0.0); and
; ordinary x.
define internal i32 @check_minimumnum_f64() noinline {
  %r1 = call double @fmul_f64(double 0.0, double -1.0)             ; 0.0 * -1.0 = -0.0
  %r2 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %r3 = call double @minimumnum_one_x_f64(double %r2)              ; (1.0, NaN) = 1.0
  %k4 = fcmp oeq double %r3, 1.0
  %r5 = call double @minimumnum_one_x_f64(double 0.5)              ; (1.0, 0.5) = 0.5
  %k6 = fcmp oeq double %r5, 0.5
  %r7 = call double @minimumnum_one_x_f64(double 2.0)              ; (1.0, 2.0) = 1.0
  %k8 = fcmp oeq double %r7, 1.0
  %r9 = call double @minimumnum_one_x_f64(double 1.0)              ; (1.0, 1.0) = 1.0
  %k10 = fcmp oeq double %r9, 1.0
  %r11 = call double @minimumnum_one_x_f64(double -3.0)            ; (1.0, -3.0) = -3.0
  %k12 = fcmp oeq double %r11, -3.0
  %r13 = call double @minimumnum_one_x_f64(double 0xFFF0000000000000) ; (1.0, -inf) = -inf
  %k14 = fcmp oeq double %r13, 0xFFF0000000000000
  %r15 = call double @minimumnum_one_x_f64(double 0x7FF0000000000000) ; (1.0, inf) = 1.0
  %k16 = fcmp oeq double %r15, 1.0
  %r17 = call double @minimumnum_one_x_f64(double %r1)             ; (1.0, -0.0) = -0.0
  %k18 = fcmp oeq double %r17, -0.0
  %q19 = fdiv double 1.0, %r17
  %k20 = fcmp oeq double %q19, 0xFFF0000000000000
  %r21 = call double @minimumnum_one_x_f64(double 0.0)             ; (1.0, 0.0) = 0.0
  %k22 = fcmp oeq double %r21, 0.0
  %q23 = fdiv double 1.0, %r21
  %k24 = fcmp oeq double %q23, 0x7FF0000000000000
  %r25 = call double @minimumnum_zero_x_f64(double %r2)            ; (0.0, NaN) = 0.0
  %k26 = fcmp oeq double %r25, 0.0
  %q27 = fdiv double 1.0, %r25
  %k28 = fcmp oeq double %q27, 0x7FF0000000000000
  %r29 = call double @minimumnum_zero_x_f64(double %r1)            ; (0.0, -0.0) = -0.0
  %k30 = fcmp oeq double %r29, -0.0
  %q31 = fdiv double 1.0, %r29
  %k32 = fcmp oeq double %q31, 0xFFF0000000000000
  %r33 = call double @minimumnum_zero_x_f64(double 0.0)            ; (0.0, 0.0) = 0.0
  %k34 = fcmp oeq double %r33, 0.0
  %q35 = fdiv double 1.0, %r33
  %k36 = fcmp oeq double %q35, 0x7FF0000000000000
  %r37 = call double @minimumnum_zero_x_f64(double -1.0)           ; (0.0, -1.0) = -1.0
  %k38 = fcmp oeq double %r37, -1.0
  %r39 = call double @minimumnum_zero_x_f64(double 2.0)            ; (0.0, 2.0) = 0.0
  %k40 = fcmp oeq double %r39, 0.0
  %q41 = fdiv double 1.0, %r39
  %k42 = fcmp oeq double %q41, 0x7FF0000000000000
  %r43 = call double @minimumnum_zero_x_f64(double 0xFFF0000000000000) ; (0.0, -inf) = -inf
  %k44 = fcmp oeq double %r43, 0xFFF0000000000000
  %r45 = call double @minimumnum_zero_x_f64(double 0x7FF0000000000000) ; (0.0, inf) = 0.0
  %k46 = fcmp oeq double %r45, 0.0
  %q47 = fdiv double 1.0, %r45
  %k48 = fcmp oeq double %q47, 0x7FF0000000000000
  %all1 = and i1 %k4, %k6
  %all2 = and i1 %all1, %k8
  %all3 = and i1 %all2, %k10
  %all4 = and i1 %all3, %k12
  %all5 = and i1 %all4, %k14
  %all6 = and i1 %all5, %k16
  %all7 = and i1 %all6, %k18
  %all8 = and i1 %all7, %k20
  %all9 = and i1 %all8, %k22
  %all10 = and i1 %all9, %k24
  %all11 = and i1 %all10, %k26
  %all12 = and i1 %all11, %k28
  %all13 = and i1 %all12, %k30
  %all14 = and i1 %all13, %k32
  %all15 = and i1 %all14, %k34
  %all16 = and i1 %all15, %k36
  %all17 = and i1 %all16, %k38
  %all18 = and i1 %all17, %k40
  %all19 = and i1 %all18, %k42
  %all20 = and i1 %all19, %k44
  %all21 = and i1 %all20, %k46
  %all22 = and i1 %all21, %k48
  %bad = xor i1 %all22, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 5: llvm.minimumnum.f32(c, x) with c = 1.0 and c = 0.0, where only x can
; be a NaN: a NaN x gives the constant, so the compare must be the ordered
; one; x = -0.0 against c = 0.0 gives -0.0 (-0.0 orders below +0.0); and
; ordinary x.
define internal i32 @check_minimumnum_f32() noinline {
  %r1 = call float @fmul_f32(float 0.0, float -1.0)                ; 0.0 * -1.0 = -0.0
  %r2 = call float @fdiv_f32(float 0.0, float 0.0)                 ; 0.0 / 0.0 = NaN
  %r3 = call float @minimumnum_one_x_f32(float %r2)                ; (1.0, NaN) = 1.0
  %k4 = fcmp oeq float %r3, 1.0
  %r5 = call float @minimumnum_one_x_f32(float 0.5)                ; (1.0, 0.5) = 0.5
  %k6 = fcmp oeq float %r5, 0.5
  %r7 = call float @minimumnum_one_x_f32(float 2.0)                ; (1.0, 2.0) = 1.0
  %k8 = fcmp oeq float %r7, 1.0
  %r9 = call float @minimumnum_one_x_f32(float 1.0)                ; (1.0, 1.0) = 1.0
  %k10 = fcmp oeq float %r9, 1.0
  %r11 = call float @minimumnum_one_x_f32(float -3.0)              ; (1.0, -3.0) = -3.0
  %k12 = fcmp oeq float %r11, -3.0
  %r13 = call float @minimumnum_one_x_f32(float 0xFFF0000000000000) ; (1.0, -inf) = -inf
  %k14 = fcmp oeq float %r13, 0xFFF0000000000000
  %r15 = call float @minimumnum_one_x_f32(float 0x7FF0000000000000) ; (1.0, inf) = 1.0
  %k16 = fcmp oeq float %r15, 1.0
  %r17 = call float @minimumnum_one_x_f32(float %r1)               ; (1.0, -0.0) = -0.0
  %k18 = fcmp oeq float %r17, -0.0
  %q19 = fdiv float 1.0, %r17
  %k20 = fcmp oeq float %q19, 0xFFF0000000000000
  %r21 = call float @minimumnum_one_x_f32(float 0.0)               ; (1.0, 0.0) = 0.0
  %k22 = fcmp oeq float %r21, 0.0
  %q23 = fdiv float 1.0, %r21
  %k24 = fcmp oeq float %q23, 0x7FF0000000000000
  %r25 = call float @minimumnum_zero_x_f32(float %r2)              ; (0.0, NaN) = 0.0
  %k26 = fcmp oeq float %r25, 0.0
  %q27 = fdiv float 1.0, %r25
  %k28 = fcmp oeq float %q27, 0x7FF0000000000000
  %r29 = call float @minimumnum_zero_x_f32(float %r1)              ; (0.0, -0.0) = -0.0
  %k30 = fcmp oeq float %r29, -0.0
  %q31 = fdiv float 1.0, %r29
  %k32 = fcmp oeq float %q31, 0xFFF0000000000000
  %r33 = call float @minimumnum_zero_x_f32(float 0.0)              ; (0.0, 0.0) = 0.0
  %k34 = fcmp oeq float %r33, 0.0
  %q35 = fdiv float 1.0, %r33
  %k36 = fcmp oeq float %q35, 0x7FF0000000000000
  %r37 = call float @minimumnum_zero_x_f32(float -1.0)             ; (0.0, -1.0) = -1.0
  %k38 = fcmp oeq float %r37, -1.0
  %r39 = call float @minimumnum_zero_x_f32(float 2.0)              ; (0.0, 2.0) = 0.0
  %k40 = fcmp oeq float %r39, 0.0
  %q41 = fdiv float 1.0, %r39
  %k42 = fcmp oeq float %q41, 0x7FF0000000000000
  %r43 = call float @minimumnum_zero_x_f32(float 0xFFF0000000000000) ; (0.0, -inf) = -inf
  %k44 = fcmp oeq float %r43, 0xFFF0000000000000
  %r45 = call float @minimumnum_zero_x_f32(float 0x7FF0000000000000) ; (0.0, inf) = 0.0
  %k46 = fcmp oeq float %r45, 0.0
  %q47 = fdiv float 1.0, %r45
  %k48 = fcmp oeq float %q47, 0x7FF0000000000000
  %all1 = and i1 %k4, %k6
  %all2 = and i1 %all1, %k8
  %all3 = and i1 %all2, %k10
  %all4 = and i1 %all3, %k12
  %all5 = and i1 %all4, %k14
  %all6 = and i1 %all5, %k16
  %all7 = and i1 %all6, %k18
  %all8 = and i1 %all7, %k20
  %all9 = and i1 %all8, %k22
  %all10 = and i1 %all9, %k24
  %all11 = and i1 %all10, %k26
  %all12 = and i1 %all11, %k28
  %all13 = and i1 %all12, %k30
  %all14 = and i1 %all13, %k32
  %all15 = and i1 %all14, %k34
  %all16 = and i1 %all15, %k36
  %all17 = and i1 %all16, %k38
  %all18 = and i1 %all17, %k40
  %all19 = and i1 %all18, %k42
  %all20 = and i1 %all19, %k44
  %all21 = and i1 %all20, %k46
  %all22 = and i1 %all21, %k48
  %bad = xor i1 %all22, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 6: llvm.maximumnum.f64(c, x) with c = 1.0 and c = -0.0, where only x can
; be a NaN: a NaN x gives the constant, so the compare must be the ordered
; one; x = +0.0 against c = -0.0 gives +0.0 (+0.0 orders above -0.0); and
; ordinary x.
define internal i32 @check_maximumnum_f64() noinline {
  %r1 = call double @fmul_f64(double 0.0, double -1.0)             ; 0.0 * -1.0 = -0.0
  %r2 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %r3 = call double @maximumnum_one_x_f64(double %r2)              ; (1.0, NaN) = 1.0
  %k4 = fcmp oeq double %r3, 1.0
  %r5 = call double @maximumnum_one_x_f64(double 0.5)              ; (1.0, 0.5) = 1.0
  %k6 = fcmp oeq double %r5, 1.0
  %r7 = call double @maximumnum_one_x_f64(double 2.0)              ; (1.0, 2.0) = 2.0
  %k8 = fcmp oeq double %r7, 2.0
  %r9 = call double @maximumnum_one_x_f64(double 1.0)              ; (1.0, 1.0) = 1.0
  %k10 = fcmp oeq double %r9, 1.0
  %r11 = call double @maximumnum_one_x_f64(double -3.0)            ; (1.0, -3.0) = 1.0
  %k12 = fcmp oeq double %r11, 1.0
  %r13 = call double @maximumnum_one_x_f64(double 0xFFF0000000000000) ; (1.0, -inf) = 1.0
  %k14 = fcmp oeq double %r13, 1.0
  %r15 = call double @maximumnum_one_x_f64(double 0x7FF0000000000000) ; (1.0, inf) = inf
  %k16 = fcmp oeq double %r15, 0x7FF0000000000000
  %r17 = call double @maximumnum_one_x_f64(double %r1)             ; (1.0, -0.0) = 1.0
  %k18 = fcmp oeq double %r17, 1.0
  %r19 = call double @maximumnum_one_x_f64(double 0.0)             ; (1.0, 0.0) = 1.0
  %k20 = fcmp oeq double %r19, 1.0
  %r21 = call double @maximumnum_negzero_x_f64(double %r2)         ; (-0.0, NaN) = -0.0
  %k22 = fcmp oeq double %r21, -0.0
  %q23 = fdiv double 1.0, %r21
  %k24 = fcmp oeq double %q23, 0xFFF0000000000000
  %r25 = call double @maximumnum_negzero_x_f64(double %r1)         ; (-0.0, -0.0) = -0.0
  %k26 = fcmp oeq double %r25, -0.0
  %q27 = fdiv double 1.0, %r25
  %k28 = fcmp oeq double %q27, 0xFFF0000000000000
  %r29 = call double @maximumnum_negzero_x_f64(double 0.0)         ; (-0.0, 0.0) = 0.0
  %k30 = fcmp oeq double %r29, 0.0
  %q31 = fdiv double 1.0, %r29
  %k32 = fcmp oeq double %q31, 0x7FF0000000000000
  %r33 = call double @maximumnum_negzero_x_f64(double -1.0)        ; (-0.0, -1.0) = -0.0
  %k34 = fcmp oeq double %r33, -0.0
  %q35 = fdiv double 1.0, %r33
  %k36 = fcmp oeq double %q35, 0xFFF0000000000000
  %r37 = call double @maximumnum_negzero_x_f64(double 2.0)         ; (-0.0, 2.0) = 2.0
  %k38 = fcmp oeq double %r37, 2.0
  %r39 = call double @maximumnum_negzero_x_f64(double 0xFFF0000000000000) ; (-0.0, -inf) = -0.0
  %k40 = fcmp oeq double %r39, -0.0
  %q41 = fdiv double 1.0, %r39
  %k42 = fcmp oeq double %q41, 0xFFF0000000000000
  %r43 = call double @maximumnum_negzero_x_f64(double 0x7FF0000000000000) ; (-0.0, inf) = inf
  %k44 = fcmp oeq double %r43, 0x7FF0000000000000
  %all1 = and i1 %k4, %k6
  %all2 = and i1 %all1, %k8
  %all3 = and i1 %all2, %k10
  %all4 = and i1 %all3, %k12
  %all5 = and i1 %all4, %k14
  %all6 = and i1 %all5, %k16
  %all7 = and i1 %all6, %k18
  %all8 = and i1 %all7, %k20
  %all9 = and i1 %all8, %k22
  %all10 = and i1 %all9, %k24
  %all11 = and i1 %all10, %k26
  %all12 = and i1 %all11, %k28
  %all13 = and i1 %all12, %k30
  %all14 = and i1 %all13, %k32
  %all15 = and i1 %all14, %k34
  %all16 = and i1 %all15, %k36
  %all17 = and i1 %all16, %k38
  %all18 = and i1 %all17, %k40
  %all19 = and i1 %all18, %k42
  %all20 = and i1 %all19, %k44
  %bad = xor i1 %all20, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 7: llvm.maximumnum.f32(c, x) with c = 1.0 and c = -0.0, where only x can
; be a NaN: a NaN x gives the constant, so the compare must be the ordered
; one; x = +0.0 against c = -0.0 gives +0.0 (+0.0 orders above -0.0); and
; ordinary x.
define internal i32 @check_maximumnum_f32() noinline {
  %r1 = call float @fmul_f32(float 0.0, float -1.0)                ; 0.0 * -1.0 = -0.0
  %r2 = call float @fdiv_f32(float 0.0, float 0.0)                 ; 0.0 / 0.0 = NaN
  %r3 = call float @maximumnum_one_x_f32(float %r2)                ; (1.0, NaN) = 1.0
  %k4 = fcmp oeq float %r3, 1.0
  %r5 = call float @maximumnum_one_x_f32(float 0.5)                ; (1.0, 0.5) = 1.0
  %k6 = fcmp oeq float %r5, 1.0
  %r7 = call float @maximumnum_one_x_f32(float 2.0)                ; (1.0, 2.0) = 2.0
  %k8 = fcmp oeq float %r7, 2.0
  %r9 = call float @maximumnum_one_x_f32(float 1.0)                ; (1.0, 1.0) = 1.0
  %k10 = fcmp oeq float %r9, 1.0
  %r11 = call float @maximumnum_one_x_f32(float -3.0)              ; (1.0, -3.0) = 1.0
  %k12 = fcmp oeq float %r11, 1.0
  %r13 = call float @maximumnum_one_x_f32(float 0xFFF0000000000000) ; (1.0, -inf) = 1.0
  %k14 = fcmp oeq float %r13, 1.0
  %r15 = call float @maximumnum_one_x_f32(float 0x7FF0000000000000) ; (1.0, inf) = inf
  %k16 = fcmp oeq float %r15, 0x7FF0000000000000
  %r17 = call float @maximumnum_one_x_f32(float %r1)               ; (1.0, -0.0) = 1.0
  %k18 = fcmp oeq float %r17, 1.0
  %r19 = call float @maximumnum_one_x_f32(float 0.0)               ; (1.0, 0.0) = 1.0
  %k20 = fcmp oeq float %r19, 1.0
  %r21 = call float @maximumnum_negzero_x_f32(float %r2)           ; (-0.0, NaN) = -0.0
  %k22 = fcmp oeq float %r21, -0.0
  %q23 = fdiv float 1.0, %r21
  %k24 = fcmp oeq float %q23, 0xFFF0000000000000
  %r25 = call float @maximumnum_negzero_x_f32(float %r1)           ; (-0.0, -0.0) = -0.0
  %k26 = fcmp oeq float %r25, -0.0
  %q27 = fdiv float 1.0, %r25
  %k28 = fcmp oeq float %q27, 0xFFF0000000000000
  %r29 = call float @maximumnum_negzero_x_f32(float 0.0)           ; (-0.0, 0.0) = 0.0
  %k30 = fcmp oeq float %r29, 0.0
  %q31 = fdiv float 1.0, %r29
  %k32 = fcmp oeq float %q31, 0x7FF0000000000000
  %r33 = call float @maximumnum_negzero_x_f32(float -1.0)          ; (-0.0, -1.0) = -0.0
  %k34 = fcmp oeq float %r33, -0.0
  %q35 = fdiv float 1.0, %r33
  %k36 = fcmp oeq float %q35, 0xFFF0000000000000
  %r37 = call float @maximumnum_negzero_x_f32(float 2.0)           ; (-0.0, 2.0) = 2.0
  %k38 = fcmp oeq float %r37, 2.0
  %r39 = call float @maximumnum_negzero_x_f32(float 0xFFF0000000000000) ; (-0.0, -inf) = -0.0
  %k40 = fcmp oeq float %r39, -0.0
  %q41 = fdiv float 1.0, %r39
  %k42 = fcmp oeq float %q41, 0xFFF0000000000000
  %r43 = call float @maximumnum_negzero_x_f32(float 0x7FF0000000000000) ; (-0.0, inf) = inf
  %k44 = fcmp oeq float %r43, 0x7FF0000000000000
  %all1 = and i1 %k4, %k6
  %all2 = and i1 %all1, %k8
  %all3 = and i1 %all2, %k10
  %all4 = and i1 %all3, %k12
  %all5 = and i1 %all4, %k14
  %all6 = and i1 %all5, %k16
  %all7 = and i1 %all6, %k18
  %all8 = and i1 %all7, %k20
  %all9 = and i1 %all8, %k22
  %all10 = and i1 %all9, %k24
  %all11 = and i1 %all10, %k26
  %all12 = and i1 %all11, %k28
  %all13 = and i1 %all12, %k30
  %all14 = and i1 %all13, %k32
  %all15 = and i1 %all14, %k34
  %all16 = and i1 %all15, %k36
  %all17 = and i1 %all16, %k38
  %all18 = and i1 %all17, %k40
  %all19 = and i1 %all18, %k42
  %all20 = and i1 %all19, %k44
  %bad = xor i1 %all20, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

define i32 @main() {
  %b0 = call i32 @check_minimum_f64()
  %b1 = call i32 @check_minimum_f32()
  %s1 = shl i32 %b1, 1
  %b2 = call i32 @check_maximum_f64()
  %s2 = shl i32 %b2, 2
  %b3 = call i32 @check_maximum_f32()
  %s3 = shl i32 %b3, 3
  %b4 = call i32 @check_minimumnum_f64()
  %s4 = shl i32 %b4, 4
  %b5 = call i32 @check_minimumnum_f32()
  %s5 = shl i32 %b5, 5
  %b6 = call i32 @check_maximumnum_f64()
  %s6 = shl i32 %b6, 6
  %b7 = call i32 @check_maximumnum_f32()
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

declare double @llvm.minimum.f64(double, double)
declare float @llvm.minimum.f32(float, float)
declare double @llvm.maximum.f64(double, double)
declare float @llvm.maximum.f32(float, float)
declare double @llvm.minimumnum.f64(double, double)
declare float @llvm.minimumnum.f32(float, float)
declare double @llvm.maximumnum.f64(double, double)
declare float @llvm.maximumnum.f32(float, float)
