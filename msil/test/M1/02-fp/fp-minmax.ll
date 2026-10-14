; llvm.minimum, llvm.maximum, llvm.minimumnum and llvm.maximumnum on float64
; and float32 (Milestone 5; plan section 12, row 'M5.a amended (minimum and
; maximum)'): compares and selects with no call and no memory. minimum and
; maximum return a NaN when either operand is a NaN; minimumnum and
; maximumnum return the other operand when exactly one is a NaN; all four
; order -0.0 below +0.0, so minimum(-0.0, +0.0) and minimum(+0.0, -0.0) are
; -0.0 and maximum gives +0.0. Before M7 there is no fabs, copysign or
; bitcast (plan 5.5): -0.0 is made as 0.0 * -1.0 and NaN as 0.0 / 0.0 in
; noinline functions, a NaN result is tested with fcmp uno, and the sign of a
; zero result through 1.0 / x == -inf or +inf. Each noinline check function
; returns 1 when a result is wrong; main ORs one bit per check into the exit
; status (8 bits, D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

define double @minimum_f64(double %a, double %b) noinline {
  %r = call double @llvm.minimum.f64(double %a, double %b)
  ret double %r
}

define float @minimum_f32(float %a, float %b) noinline {
  %r = call float @llvm.minimum.f32(float %a, float %b)
  ret float %r
}

define double @maximum_f64(double %a, double %b) noinline {
  %r = call double @llvm.maximum.f64(double %a, double %b)
  ret double %r
}

define float @maximum_f32(float %a, float %b) noinline {
  %r = call float @llvm.maximum.f32(float %a, float %b)
  ret float %r
}

define double @minimumnum_f64(double %a, double %b) noinline {
  %r = call double @llvm.minimumnum.f64(double %a, double %b)
  ret double %r
}

define float @minimumnum_f32(float %a, float %b) noinline {
  %r = call float @llvm.minimumnum.f32(float %a, float %b)
  ret float %r
}

define double @maximumnum_f64(double %a, double %b) noinline {
  %r = call double @llvm.maximumnum.f64(double %a, double %b)
  ret double %r
}

define float @maximumnum_f32(float %a, float %b) noinline {
  %r = call float @llvm.maximumnum.f32(float %a, float %b)
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

; Bit 0: llvm.minimum.f64 of ordinary operands, of zeros of either sign in
; either order (-0.0 orders below +0.0), and of NaN operands (a NaN operand
; gives a NaN).
define internal i32 @check_minimum_f64() noinline {
  %r1 = call double @fmul_f64(double 0.0, double -1.0)             ; 0.0 * -1.0 = -0.0
  %r2 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %r3 = call double @minimum_f64(double 1.5, double -2.25)         ; (1.5, -2.25) = -2.25
  %k4 = fcmp oeq double %r3, -2.25
  %r5 = call double @minimum_f64(double -2.25, double 1.5)         ; (-2.25, 1.5) = -2.25
  %k6 = fcmp oeq double %r5, -2.25
  %r7 = call double @minimum_f64(double 3.0, double 3.0)           ; (3.0, 3.0) = 3.0
  %k8 = fcmp oeq double %r7, 3.0
  %r9 = call double @minimum_f64(double 0xFFF0000000000000, double 7.0) ; (-inf, 7.0) = -inf
  %k10 = fcmp oeq double %r9, 0xFFF0000000000000
  %r11 = call double @minimum_f64(double 7.0, double 0x7FF0000000000000) ; (7.0, inf) = 7.0
  %k12 = fcmp oeq double %r11, 7.0
  %r13 = call double @minimum_f64(double 100.0, double 0.5)        ; (100.0, 0.5) = 0.5
  %k14 = fcmp oeq double %r13, 0.5
  %r15 = call double @minimum_f64(double 0.0, double -1.0)         ; (0.0, -1.0) = -1.0
  %k16 = fcmp oeq double %r15, -1.0
  %r17 = call double @minimum_f64(double -1.0, double 0.0)         ; (-1.0, 0.0) = -1.0
  %k18 = fcmp oeq double %r17, -1.0
  %r19 = call double @minimum_f64(double %r1, double 1.0)          ; (-0.0, 1.0) = -0.0
  %k20 = fcmp oeq double %r19, -0.0
  %q21 = fdiv double 1.0, %r19
  %k22 = fcmp oeq double %q21, 0xFFF0000000000000
  %r23 = call double @minimum_f64(double -1.0, double %r1)         ; (-1.0, -0.0) = -1.0
  %k24 = fcmp oeq double %r23, -1.0
  %r25 = call double @minimum_f64(double %r1, double 0.0)          ; (-0.0, 0.0) = -0.0
  %k26 = fcmp oeq double %r25, -0.0
  %q27 = fdiv double 1.0, %r25
  %k28 = fcmp oeq double %q27, 0xFFF0000000000000
  %r29 = call double @minimum_f64(double 0.0, double %r1)          ; (0.0, -0.0) = -0.0
  %k30 = fcmp oeq double %r29, -0.0
  %q31 = fdiv double 1.0, %r29
  %k32 = fcmp oeq double %q31, 0xFFF0000000000000
  %r33 = call double @minimum_f64(double %r1, double %r1)          ; (-0.0, -0.0) = -0.0
  %k34 = fcmp oeq double %r33, -0.0
  %q35 = fdiv double 1.0, %r33
  %k36 = fcmp oeq double %q35, 0xFFF0000000000000
  %r37 = call double @minimum_f64(double 0.0, double 0.0)          ; (0.0, 0.0) = 0.0
  %k38 = fcmp oeq double %r37, 0.0
  %q39 = fdiv double 1.0, %r37
  %k40 = fcmp oeq double %q39, 0x7FF0000000000000
  %r41 = call double @minimum_f64(double %r2, double 1.0)          ; (NaN, 1.0) = NaN
  %k42 = fcmp uno double %r41, 0.0
  %r43 = call double @minimum_f64(double 1.0, double %r2)          ; (1.0, NaN) = NaN
  %k44 = fcmp uno double %r43, 0.0
  %r45 = call double @minimum_f64(double %r2, double %r2)          ; (NaN, NaN) = NaN
  %k46 = fcmp uno double %r45, 0.0
  %r47 = call double @minimum_f64(double %r2, double %r1)          ; (NaN, -0.0) = NaN
  %k48 = fcmp uno double %r47, 0.0
  %r49 = call double @minimum_f64(double 0.0, double %r2)          ; (0.0, NaN) = NaN
  %k50 = fcmp uno double %r49, 0.0
  %r51 = call double @minimum_f64(double %r2, double 0xFFF0000000000000) ; (NaN, -inf) = NaN
  %k52 = fcmp uno double %r51, 0.0
  %r53 = call double @minimum_f64(double 0x7FF0000000000000, double %r2) ; (inf, NaN) = NaN
  %k54 = fcmp uno double %r53, 0.0
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
  %all23 = and i1 %all22, %k50
  %all24 = and i1 %all23, %k52
  %all25 = and i1 %all24, %k54
  %bad = xor i1 %all25, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 1: llvm.minimum.f32 of ordinary operands, of zeros of either sign in
; either order (-0.0 orders below +0.0), and of NaN operands (a NaN operand
; gives a NaN).
define internal i32 @check_minimum_f32() noinline {
  %r1 = call float @fmul_f32(float 0.0, float -1.0)                ; 0.0 * -1.0 = -0.0
  %r2 = call float @fdiv_f32(float 0.0, float 0.0)                 ; 0.0 / 0.0 = NaN
  %r3 = call float @minimum_f32(float 1.5, float -2.25)            ; (1.5, -2.25) = -2.25
  %k4 = fcmp oeq float %r3, -2.25
  %r5 = call float @minimum_f32(float -2.25, float 1.5)            ; (-2.25, 1.5) = -2.25
  %k6 = fcmp oeq float %r5, -2.25
  %r7 = call float @minimum_f32(float 3.0, float 3.0)              ; (3.0, 3.0) = 3.0
  %k8 = fcmp oeq float %r7, 3.0
  %r9 = call float @minimum_f32(float 0xFFF0000000000000, float 7.0) ; (-inf, 7.0) = -inf
  %k10 = fcmp oeq float %r9, 0xFFF0000000000000
  %r11 = call float @minimum_f32(float 7.0, float 0x7FF0000000000000) ; (7.0, inf) = 7.0
  %k12 = fcmp oeq float %r11, 7.0
  %r13 = call float @minimum_f32(float 100.0, float 0.5)           ; (100.0, 0.5) = 0.5
  %k14 = fcmp oeq float %r13, 0.5
  %r15 = call float @minimum_f32(float 0.0, float -1.0)            ; (0.0, -1.0) = -1.0
  %k16 = fcmp oeq float %r15, -1.0
  %r17 = call float @minimum_f32(float -1.0, float 0.0)            ; (-1.0, 0.0) = -1.0
  %k18 = fcmp oeq float %r17, -1.0
  %r19 = call float @minimum_f32(float %r1, float 1.0)             ; (-0.0, 1.0) = -0.0
  %k20 = fcmp oeq float %r19, -0.0
  %q21 = fdiv float 1.0, %r19
  %k22 = fcmp oeq float %q21, 0xFFF0000000000000
  %r23 = call float @minimum_f32(float -1.0, float %r1)            ; (-1.0, -0.0) = -1.0
  %k24 = fcmp oeq float %r23, -1.0
  %r25 = call float @minimum_f32(float %r1, float 0.0)             ; (-0.0, 0.0) = -0.0
  %k26 = fcmp oeq float %r25, -0.0
  %q27 = fdiv float 1.0, %r25
  %k28 = fcmp oeq float %q27, 0xFFF0000000000000
  %r29 = call float @minimum_f32(float 0.0, float %r1)             ; (0.0, -0.0) = -0.0
  %k30 = fcmp oeq float %r29, -0.0
  %q31 = fdiv float 1.0, %r29
  %k32 = fcmp oeq float %q31, 0xFFF0000000000000
  %r33 = call float @minimum_f32(float %r1, float %r1)             ; (-0.0, -0.0) = -0.0
  %k34 = fcmp oeq float %r33, -0.0
  %q35 = fdiv float 1.0, %r33
  %k36 = fcmp oeq float %q35, 0xFFF0000000000000
  %r37 = call float @minimum_f32(float 0.0, float 0.0)             ; (0.0, 0.0) = 0.0
  %k38 = fcmp oeq float %r37, 0.0
  %q39 = fdiv float 1.0, %r37
  %k40 = fcmp oeq float %q39, 0x7FF0000000000000
  %r41 = call float @minimum_f32(float %r2, float 1.0)             ; (NaN, 1.0) = NaN
  %k42 = fcmp uno float %r41, 0.0
  %r43 = call float @minimum_f32(float 1.0, float %r2)             ; (1.0, NaN) = NaN
  %k44 = fcmp uno float %r43, 0.0
  %r45 = call float @minimum_f32(float %r2, float %r2)             ; (NaN, NaN) = NaN
  %k46 = fcmp uno float %r45, 0.0
  %r47 = call float @minimum_f32(float %r2, float %r1)             ; (NaN, -0.0) = NaN
  %k48 = fcmp uno float %r47, 0.0
  %r49 = call float @minimum_f32(float 0.0, float %r2)             ; (0.0, NaN) = NaN
  %k50 = fcmp uno float %r49, 0.0
  %r51 = call float @minimum_f32(float %r2, float 0xFFF0000000000000) ; (NaN, -inf) = NaN
  %k52 = fcmp uno float %r51, 0.0
  %r53 = call float @minimum_f32(float 0x7FF0000000000000, float %r2) ; (inf, NaN) = NaN
  %k54 = fcmp uno float %r53, 0.0
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
  %all23 = and i1 %all22, %k50
  %all24 = and i1 %all23, %k52
  %all25 = and i1 %all24, %k54
  %bad = xor i1 %all25, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 2: llvm.maximum.f64 of ordinary operands, of zeros of either sign in
; either order (-0.0 orders below +0.0), and of NaN operands (a NaN operand
; gives a NaN).
define internal i32 @check_maximum_f64() noinline {
  %r1 = call double @fmul_f64(double 0.0, double -1.0)             ; 0.0 * -1.0 = -0.0
  %r2 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %r3 = call double @maximum_f64(double 1.5, double -2.25)         ; (1.5, -2.25) = 1.5
  %k4 = fcmp oeq double %r3, 1.5
  %r5 = call double @maximum_f64(double -2.25, double 1.5)         ; (-2.25, 1.5) = 1.5
  %k6 = fcmp oeq double %r5, 1.5
  %r7 = call double @maximum_f64(double 3.0, double 3.0)           ; (3.0, 3.0) = 3.0
  %k8 = fcmp oeq double %r7, 3.0
  %r9 = call double @maximum_f64(double 0xFFF0000000000000, double 7.0) ; (-inf, 7.0) = 7.0
  %k10 = fcmp oeq double %r9, 7.0
  %r11 = call double @maximum_f64(double 7.0, double 0x7FF0000000000000) ; (7.0, inf) = inf
  %k12 = fcmp oeq double %r11, 0x7FF0000000000000
  %r13 = call double @maximum_f64(double 100.0, double 0.5)        ; (100.0, 0.5) = 100.0
  %k14 = fcmp oeq double %r13, 100.0
  %r15 = call double @maximum_f64(double 0.0, double -1.0)         ; (0.0, -1.0) = 0.0
  %k16 = fcmp oeq double %r15, 0.0
  %q17 = fdiv double 1.0, %r15
  %k18 = fcmp oeq double %q17, 0x7FF0000000000000
  %r19 = call double @maximum_f64(double -1.0, double 0.0)         ; (-1.0, 0.0) = 0.0
  %k20 = fcmp oeq double %r19, 0.0
  %q21 = fdiv double 1.0, %r19
  %k22 = fcmp oeq double %q21, 0x7FF0000000000000
  %r23 = call double @maximum_f64(double %r1, double 1.0)          ; (-0.0, 1.0) = 1.0
  %k24 = fcmp oeq double %r23, 1.0
  %r25 = call double @maximum_f64(double -1.0, double %r1)         ; (-1.0, -0.0) = -0.0
  %k26 = fcmp oeq double %r25, -0.0
  %q27 = fdiv double 1.0, %r25
  %k28 = fcmp oeq double %q27, 0xFFF0000000000000
  %r29 = call double @maximum_f64(double %r1, double 0.0)          ; (-0.0, 0.0) = 0.0
  %k30 = fcmp oeq double %r29, 0.0
  %q31 = fdiv double 1.0, %r29
  %k32 = fcmp oeq double %q31, 0x7FF0000000000000
  %r33 = call double @maximum_f64(double 0.0, double %r1)          ; (0.0, -0.0) = 0.0
  %k34 = fcmp oeq double %r33, 0.0
  %q35 = fdiv double 1.0, %r33
  %k36 = fcmp oeq double %q35, 0x7FF0000000000000
  %r37 = call double @maximum_f64(double %r1, double %r1)          ; (-0.0, -0.0) = -0.0
  %k38 = fcmp oeq double %r37, -0.0
  %q39 = fdiv double 1.0, %r37
  %k40 = fcmp oeq double %q39, 0xFFF0000000000000
  %r41 = call double @maximum_f64(double 0.0, double 0.0)          ; (0.0, 0.0) = 0.0
  %k42 = fcmp oeq double %r41, 0.0
  %q43 = fdiv double 1.0, %r41
  %k44 = fcmp oeq double %q43, 0x7FF0000000000000
  %r45 = call double @maximum_f64(double %r2, double 1.0)          ; (NaN, 1.0) = NaN
  %k46 = fcmp uno double %r45, 0.0
  %r47 = call double @maximum_f64(double 1.0, double %r2)          ; (1.0, NaN) = NaN
  %k48 = fcmp uno double %r47, 0.0
  %r49 = call double @maximum_f64(double %r2, double %r2)          ; (NaN, NaN) = NaN
  %k50 = fcmp uno double %r49, 0.0
  %r51 = call double @maximum_f64(double %r2, double %r1)          ; (NaN, -0.0) = NaN
  %k52 = fcmp uno double %r51, 0.0
  %r53 = call double @maximum_f64(double 0.0, double %r2)          ; (0.0, NaN) = NaN
  %k54 = fcmp uno double %r53, 0.0
  %r55 = call double @maximum_f64(double %r2, double 0xFFF0000000000000) ; (NaN, -inf) = NaN
  %k56 = fcmp uno double %r55, 0.0
  %r57 = call double @maximum_f64(double 0x7FF0000000000000, double %r2) ; (inf, NaN) = NaN
  %k58 = fcmp uno double %r57, 0.0
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
  %all23 = and i1 %all22, %k50
  %all24 = and i1 %all23, %k52
  %all25 = and i1 %all24, %k54
  %all26 = and i1 %all25, %k56
  %all27 = and i1 %all26, %k58
  %bad = xor i1 %all27, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 3: llvm.maximum.f32 of ordinary operands, of zeros of either sign in
; either order (-0.0 orders below +0.0), and of NaN operands (a NaN operand
; gives a NaN).
define internal i32 @check_maximum_f32() noinline {
  %r1 = call float @fmul_f32(float 0.0, float -1.0)                ; 0.0 * -1.0 = -0.0
  %r2 = call float @fdiv_f32(float 0.0, float 0.0)                 ; 0.0 / 0.0 = NaN
  %r3 = call float @maximum_f32(float 1.5, float -2.25)            ; (1.5, -2.25) = 1.5
  %k4 = fcmp oeq float %r3, 1.5
  %r5 = call float @maximum_f32(float -2.25, float 1.5)            ; (-2.25, 1.5) = 1.5
  %k6 = fcmp oeq float %r5, 1.5
  %r7 = call float @maximum_f32(float 3.0, float 3.0)              ; (3.0, 3.0) = 3.0
  %k8 = fcmp oeq float %r7, 3.0
  %r9 = call float @maximum_f32(float 0xFFF0000000000000, float 7.0) ; (-inf, 7.0) = 7.0
  %k10 = fcmp oeq float %r9, 7.0
  %r11 = call float @maximum_f32(float 7.0, float 0x7FF0000000000000) ; (7.0, inf) = inf
  %k12 = fcmp oeq float %r11, 0x7FF0000000000000
  %r13 = call float @maximum_f32(float 100.0, float 0.5)           ; (100.0, 0.5) = 100.0
  %k14 = fcmp oeq float %r13, 100.0
  %r15 = call float @maximum_f32(float 0.0, float -1.0)            ; (0.0, -1.0) = 0.0
  %k16 = fcmp oeq float %r15, 0.0
  %q17 = fdiv float 1.0, %r15
  %k18 = fcmp oeq float %q17, 0x7FF0000000000000
  %r19 = call float @maximum_f32(float -1.0, float 0.0)            ; (-1.0, 0.0) = 0.0
  %k20 = fcmp oeq float %r19, 0.0
  %q21 = fdiv float 1.0, %r19
  %k22 = fcmp oeq float %q21, 0x7FF0000000000000
  %r23 = call float @maximum_f32(float %r1, float 1.0)             ; (-0.0, 1.0) = 1.0
  %k24 = fcmp oeq float %r23, 1.0
  %r25 = call float @maximum_f32(float -1.0, float %r1)            ; (-1.0, -0.0) = -0.0
  %k26 = fcmp oeq float %r25, -0.0
  %q27 = fdiv float 1.0, %r25
  %k28 = fcmp oeq float %q27, 0xFFF0000000000000
  %r29 = call float @maximum_f32(float %r1, float 0.0)             ; (-0.0, 0.0) = 0.0
  %k30 = fcmp oeq float %r29, 0.0
  %q31 = fdiv float 1.0, %r29
  %k32 = fcmp oeq float %q31, 0x7FF0000000000000
  %r33 = call float @maximum_f32(float 0.0, float %r1)             ; (0.0, -0.0) = 0.0
  %k34 = fcmp oeq float %r33, 0.0
  %q35 = fdiv float 1.0, %r33
  %k36 = fcmp oeq float %q35, 0x7FF0000000000000
  %r37 = call float @maximum_f32(float %r1, float %r1)             ; (-0.0, -0.0) = -0.0
  %k38 = fcmp oeq float %r37, -0.0
  %q39 = fdiv float 1.0, %r37
  %k40 = fcmp oeq float %q39, 0xFFF0000000000000
  %r41 = call float @maximum_f32(float 0.0, float 0.0)             ; (0.0, 0.0) = 0.0
  %k42 = fcmp oeq float %r41, 0.0
  %q43 = fdiv float 1.0, %r41
  %k44 = fcmp oeq float %q43, 0x7FF0000000000000
  %r45 = call float @maximum_f32(float %r2, float 1.0)             ; (NaN, 1.0) = NaN
  %k46 = fcmp uno float %r45, 0.0
  %r47 = call float @maximum_f32(float 1.0, float %r2)             ; (1.0, NaN) = NaN
  %k48 = fcmp uno float %r47, 0.0
  %r49 = call float @maximum_f32(float %r2, float %r2)             ; (NaN, NaN) = NaN
  %k50 = fcmp uno float %r49, 0.0
  %r51 = call float @maximum_f32(float %r2, float %r1)             ; (NaN, -0.0) = NaN
  %k52 = fcmp uno float %r51, 0.0
  %r53 = call float @maximum_f32(float 0.0, float %r2)             ; (0.0, NaN) = NaN
  %k54 = fcmp uno float %r53, 0.0
  %r55 = call float @maximum_f32(float %r2, float 0xFFF0000000000000) ; (NaN, -inf) = NaN
  %k56 = fcmp uno float %r55, 0.0
  %r57 = call float @maximum_f32(float 0x7FF0000000000000, float %r2) ; (inf, NaN) = NaN
  %k58 = fcmp uno float %r57, 0.0
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
  %all23 = and i1 %all22, %k50
  %all24 = and i1 %all23, %k52
  %all25 = and i1 %all24, %k54
  %all26 = and i1 %all25, %k56
  %all27 = and i1 %all26, %k58
  %bad = xor i1 %all27, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 4: llvm.minimumnum.f64 of ordinary operands, of zeros of either sign in
; either order (-0.0 orders below +0.0), and of NaN operands (one NaN operand
; gives the other operand, two give a NaN).
define internal i32 @check_minimumnum_f64() noinline {
  %r1 = call double @fmul_f64(double 0.0, double -1.0)             ; 0.0 * -1.0 = -0.0
  %r2 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %r3 = call double @minimumnum_f64(double 1.5, double -2.25)      ; (1.5, -2.25) = -2.25
  %k4 = fcmp oeq double %r3, -2.25
  %r5 = call double @minimumnum_f64(double -2.25, double 1.5)      ; (-2.25, 1.5) = -2.25
  %k6 = fcmp oeq double %r5, -2.25
  %r7 = call double @minimumnum_f64(double 3.0, double 3.0)        ; (3.0, 3.0) = 3.0
  %k8 = fcmp oeq double %r7, 3.0
  %r9 = call double @minimumnum_f64(double 0xFFF0000000000000, double 7.0) ; (-inf, 7.0) = -inf
  %k10 = fcmp oeq double %r9, 0xFFF0000000000000
  %r11 = call double @minimumnum_f64(double 7.0, double 0x7FF0000000000000) ; (7.0, inf) = 7.0
  %k12 = fcmp oeq double %r11, 7.0
  %r13 = call double @minimumnum_f64(double 100.0, double 0.5)     ; (100.0, 0.5) = 0.5
  %k14 = fcmp oeq double %r13, 0.5
  %r15 = call double @minimumnum_f64(double 0.0, double -1.0)      ; (0.0, -1.0) = -1.0
  %k16 = fcmp oeq double %r15, -1.0
  %r17 = call double @minimumnum_f64(double -1.0, double 0.0)      ; (-1.0, 0.0) = -1.0
  %k18 = fcmp oeq double %r17, -1.0
  %r19 = call double @minimumnum_f64(double %r1, double 1.0)       ; (-0.0, 1.0) = -0.0
  %k20 = fcmp oeq double %r19, -0.0
  %q21 = fdiv double 1.0, %r19
  %k22 = fcmp oeq double %q21, 0xFFF0000000000000
  %r23 = call double @minimumnum_f64(double -1.0, double %r1)      ; (-1.0, -0.0) = -1.0
  %k24 = fcmp oeq double %r23, -1.0
  %r25 = call double @minimumnum_f64(double %r1, double 0.0)       ; (-0.0, 0.0) = -0.0
  %k26 = fcmp oeq double %r25, -0.0
  %q27 = fdiv double 1.0, %r25
  %k28 = fcmp oeq double %q27, 0xFFF0000000000000
  %r29 = call double @minimumnum_f64(double 0.0, double %r1)       ; (0.0, -0.0) = -0.0
  %k30 = fcmp oeq double %r29, -0.0
  %q31 = fdiv double 1.0, %r29
  %k32 = fcmp oeq double %q31, 0xFFF0000000000000
  %r33 = call double @minimumnum_f64(double %r1, double %r1)       ; (-0.0, -0.0) = -0.0
  %k34 = fcmp oeq double %r33, -0.0
  %q35 = fdiv double 1.0, %r33
  %k36 = fcmp oeq double %q35, 0xFFF0000000000000
  %r37 = call double @minimumnum_f64(double 0.0, double 0.0)       ; (0.0, 0.0) = 0.0
  %k38 = fcmp oeq double %r37, 0.0
  %q39 = fdiv double 1.0, %r37
  %k40 = fcmp oeq double %q39, 0x7FF0000000000000
  %r41 = call double @minimumnum_f64(double %r2, double 1.0)       ; (NaN, 1.0) = 1.0
  %k42 = fcmp oeq double %r41, 1.0
  %r43 = call double @minimumnum_f64(double 1.0, double %r2)       ; (1.0, NaN) = 1.0
  %k44 = fcmp oeq double %r43, 1.0
  %r45 = call double @minimumnum_f64(double %r2, double %r2)       ; (NaN, NaN) = NaN
  %k46 = fcmp uno double %r45, 0.0
  %r47 = call double @minimumnum_f64(double %r2, double %r1)       ; (NaN, -0.0) = -0.0
  %k48 = fcmp oeq double %r47, -0.0
  %q49 = fdiv double 1.0, %r47
  %k50 = fcmp oeq double %q49, 0xFFF0000000000000
  %r51 = call double @minimumnum_f64(double 0.0, double %r2)       ; (0.0, NaN) = 0.0
  %k52 = fcmp oeq double %r51, 0.0
  %q53 = fdiv double 1.0, %r51
  %k54 = fcmp oeq double %q53, 0x7FF0000000000000
  %r55 = call double @minimumnum_f64(double %r2, double 0xFFF0000000000000) ; (NaN, -inf) = -inf
  %k56 = fcmp oeq double %r55, 0xFFF0000000000000
  %r57 = call double @minimumnum_f64(double 0x7FF0000000000000, double %r2) ; (inf, NaN) = inf
  %k58 = fcmp oeq double %r57, 0x7FF0000000000000
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
  %all23 = and i1 %all22, %k50
  %all24 = and i1 %all23, %k52
  %all25 = and i1 %all24, %k54
  %all26 = and i1 %all25, %k56
  %all27 = and i1 %all26, %k58
  %bad = xor i1 %all27, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 5: llvm.minimumnum.f32 of ordinary operands, of zeros of either sign in
; either order (-0.0 orders below +0.0), and of NaN operands (one NaN operand
; gives the other operand, two give a NaN).
define internal i32 @check_minimumnum_f32() noinline {
  %r1 = call float @fmul_f32(float 0.0, float -1.0)                ; 0.0 * -1.0 = -0.0
  %r2 = call float @fdiv_f32(float 0.0, float 0.0)                 ; 0.0 / 0.0 = NaN
  %r3 = call float @minimumnum_f32(float 1.5, float -2.25)         ; (1.5, -2.25) = -2.25
  %k4 = fcmp oeq float %r3, -2.25
  %r5 = call float @minimumnum_f32(float -2.25, float 1.5)         ; (-2.25, 1.5) = -2.25
  %k6 = fcmp oeq float %r5, -2.25
  %r7 = call float @minimumnum_f32(float 3.0, float 3.0)           ; (3.0, 3.0) = 3.0
  %k8 = fcmp oeq float %r7, 3.0
  %r9 = call float @minimumnum_f32(float 0xFFF0000000000000, float 7.0) ; (-inf, 7.0) = -inf
  %k10 = fcmp oeq float %r9, 0xFFF0000000000000
  %r11 = call float @minimumnum_f32(float 7.0, float 0x7FF0000000000000) ; (7.0, inf) = 7.0
  %k12 = fcmp oeq float %r11, 7.0
  %r13 = call float @minimumnum_f32(float 100.0, float 0.5)        ; (100.0, 0.5) = 0.5
  %k14 = fcmp oeq float %r13, 0.5
  %r15 = call float @minimumnum_f32(float 0.0, float -1.0)         ; (0.0, -1.0) = -1.0
  %k16 = fcmp oeq float %r15, -1.0
  %r17 = call float @minimumnum_f32(float -1.0, float 0.0)         ; (-1.0, 0.0) = -1.0
  %k18 = fcmp oeq float %r17, -1.0
  %r19 = call float @minimumnum_f32(float %r1, float 1.0)          ; (-0.0, 1.0) = -0.0
  %k20 = fcmp oeq float %r19, -0.0
  %q21 = fdiv float 1.0, %r19
  %k22 = fcmp oeq float %q21, 0xFFF0000000000000
  %r23 = call float @minimumnum_f32(float -1.0, float %r1)         ; (-1.0, -0.0) = -1.0
  %k24 = fcmp oeq float %r23, -1.0
  %r25 = call float @minimumnum_f32(float %r1, float 0.0)          ; (-0.0, 0.0) = -0.0
  %k26 = fcmp oeq float %r25, -0.0
  %q27 = fdiv float 1.0, %r25
  %k28 = fcmp oeq float %q27, 0xFFF0000000000000
  %r29 = call float @minimumnum_f32(float 0.0, float %r1)          ; (0.0, -0.0) = -0.0
  %k30 = fcmp oeq float %r29, -0.0
  %q31 = fdiv float 1.0, %r29
  %k32 = fcmp oeq float %q31, 0xFFF0000000000000
  %r33 = call float @minimumnum_f32(float %r1, float %r1)          ; (-0.0, -0.0) = -0.0
  %k34 = fcmp oeq float %r33, -0.0
  %q35 = fdiv float 1.0, %r33
  %k36 = fcmp oeq float %q35, 0xFFF0000000000000
  %r37 = call float @minimumnum_f32(float 0.0, float 0.0)          ; (0.0, 0.0) = 0.0
  %k38 = fcmp oeq float %r37, 0.0
  %q39 = fdiv float 1.0, %r37
  %k40 = fcmp oeq float %q39, 0x7FF0000000000000
  %r41 = call float @minimumnum_f32(float %r2, float 1.0)          ; (NaN, 1.0) = 1.0
  %k42 = fcmp oeq float %r41, 1.0
  %r43 = call float @minimumnum_f32(float 1.0, float %r2)          ; (1.0, NaN) = 1.0
  %k44 = fcmp oeq float %r43, 1.0
  %r45 = call float @minimumnum_f32(float %r2, float %r2)          ; (NaN, NaN) = NaN
  %k46 = fcmp uno float %r45, 0.0
  %r47 = call float @minimumnum_f32(float %r2, float %r1)          ; (NaN, -0.0) = -0.0
  %k48 = fcmp oeq float %r47, -0.0
  %q49 = fdiv float 1.0, %r47
  %k50 = fcmp oeq float %q49, 0xFFF0000000000000
  %r51 = call float @minimumnum_f32(float 0.0, float %r2)          ; (0.0, NaN) = 0.0
  %k52 = fcmp oeq float %r51, 0.0
  %q53 = fdiv float 1.0, %r51
  %k54 = fcmp oeq float %q53, 0x7FF0000000000000
  %r55 = call float @minimumnum_f32(float %r2, float 0xFFF0000000000000) ; (NaN, -inf) = -inf
  %k56 = fcmp oeq float %r55, 0xFFF0000000000000
  %r57 = call float @minimumnum_f32(float 0x7FF0000000000000, float %r2) ; (inf, NaN) = inf
  %k58 = fcmp oeq float %r57, 0x7FF0000000000000
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
  %all23 = and i1 %all22, %k50
  %all24 = and i1 %all23, %k52
  %all25 = and i1 %all24, %k54
  %all26 = and i1 %all25, %k56
  %all27 = and i1 %all26, %k58
  %bad = xor i1 %all27, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 6: llvm.maximumnum.f64 of ordinary operands, of zeros of either sign in
; either order (-0.0 orders below +0.0), and of NaN operands (one NaN operand
; gives the other operand, two give a NaN).
define internal i32 @check_maximumnum_f64() noinline {
  %r1 = call double @fmul_f64(double 0.0, double -1.0)             ; 0.0 * -1.0 = -0.0
  %r2 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %r3 = call double @maximumnum_f64(double 1.5, double -2.25)      ; (1.5, -2.25) = 1.5
  %k4 = fcmp oeq double %r3, 1.5
  %r5 = call double @maximumnum_f64(double -2.25, double 1.5)      ; (-2.25, 1.5) = 1.5
  %k6 = fcmp oeq double %r5, 1.5
  %r7 = call double @maximumnum_f64(double 3.0, double 3.0)        ; (3.0, 3.0) = 3.0
  %k8 = fcmp oeq double %r7, 3.0
  %r9 = call double @maximumnum_f64(double 0xFFF0000000000000, double 7.0) ; (-inf, 7.0) = 7.0
  %k10 = fcmp oeq double %r9, 7.0
  %r11 = call double @maximumnum_f64(double 7.0, double 0x7FF0000000000000) ; (7.0, inf) = inf
  %k12 = fcmp oeq double %r11, 0x7FF0000000000000
  %r13 = call double @maximumnum_f64(double 100.0, double 0.5)     ; (100.0, 0.5) = 100.0
  %k14 = fcmp oeq double %r13, 100.0
  %r15 = call double @maximumnum_f64(double 0.0, double -1.0)      ; (0.0, -1.0) = 0.0
  %k16 = fcmp oeq double %r15, 0.0
  %q17 = fdiv double 1.0, %r15
  %k18 = fcmp oeq double %q17, 0x7FF0000000000000
  %r19 = call double @maximumnum_f64(double -1.0, double 0.0)      ; (-1.0, 0.0) = 0.0
  %k20 = fcmp oeq double %r19, 0.0
  %q21 = fdiv double 1.0, %r19
  %k22 = fcmp oeq double %q21, 0x7FF0000000000000
  %r23 = call double @maximumnum_f64(double %r1, double 1.0)       ; (-0.0, 1.0) = 1.0
  %k24 = fcmp oeq double %r23, 1.0
  %r25 = call double @maximumnum_f64(double -1.0, double %r1)      ; (-1.0, -0.0) = -0.0
  %k26 = fcmp oeq double %r25, -0.0
  %q27 = fdiv double 1.0, %r25
  %k28 = fcmp oeq double %q27, 0xFFF0000000000000
  %r29 = call double @maximumnum_f64(double %r1, double 0.0)       ; (-0.0, 0.0) = 0.0
  %k30 = fcmp oeq double %r29, 0.0
  %q31 = fdiv double 1.0, %r29
  %k32 = fcmp oeq double %q31, 0x7FF0000000000000
  %r33 = call double @maximumnum_f64(double 0.0, double %r1)       ; (0.0, -0.0) = 0.0
  %k34 = fcmp oeq double %r33, 0.0
  %q35 = fdiv double 1.0, %r33
  %k36 = fcmp oeq double %q35, 0x7FF0000000000000
  %r37 = call double @maximumnum_f64(double %r1, double %r1)       ; (-0.0, -0.0) = -0.0
  %k38 = fcmp oeq double %r37, -0.0
  %q39 = fdiv double 1.0, %r37
  %k40 = fcmp oeq double %q39, 0xFFF0000000000000
  %r41 = call double @maximumnum_f64(double 0.0, double 0.0)       ; (0.0, 0.0) = 0.0
  %k42 = fcmp oeq double %r41, 0.0
  %q43 = fdiv double 1.0, %r41
  %k44 = fcmp oeq double %q43, 0x7FF0000000000000
  %r45 = call double @maximumnum_f64(double %r2, double 1.0)       ; (NaN, 1.0) = 1.0
  %k46 = fcmp oeq double %r45, 1.0
  %r47 = call double @maximumnum_f64(double 1.0, double %r2)       ; (1.0, NaN) = 1.0
  %k48 = fcmp oeq double %r47, 1.0
  %r49 = call double @maximumnum_f64(double %r2, double %r2)       ; (NaN, NaN) = NaN
  %k50 = fcmp uno double %r49, 0.0
  %r51 = call double @maximumnum_f64(double %r2, double %r1)       ; (NaN, -0.0) = -0.0
  %k52 = fcmp oeq double %r51, -0.0
  %q53 = fdiv double 1.0, %r51
  %k54 = fcmp oeq double %q53, 0xFFF0000000000000
  %r55 = call double @maximumnum_f64(double 0.0, double %r2)       ; (0.0, NaN) = 0.0
  %k56 = fcmp oeq double %r55, 0.0
  %q57 = fdiv double 1.0, %r55
  %k58 = fcmp oeq double %q57, 0x7FF0000000000000
  %r59 = call double @maximumnum_f64(double %r2, double 0xFFF0000000000000) ; (NaN, -inf) = -inf
  %k60 = fcmp oeq double %r59, 0xFFF0000000000000
  %r61 = call double @maximumnum_f64(double 0x7FF0000000000000, double %r2) ; (inf, NaN) = inf
  %k62 = fcmp oeq double %r61, 0x7FF0000000000000
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
  %all23 = and i1 %all22, %k50
  %all24 = and i1 %all23, %k52
  %all25 = and i1 %all24, %k54
  %all26 = and i1 %all25, %k56
  %all27 = and i1 %all26, %k58
  %all28 = and i1 %all27, %k60
  %all29 = and i1 %all28, %k62
  %bad = xor i1 %all29, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 7: llvm.maximumnum.f32 of ordinary operands, of zeros of either sign in
; either order (-0.0 orders below +0.0), and of NaN operands (one NaN operand
; gives the other operand, two give a NaN).
define internal i32 @check_maximumnum_f32() noinline {
  %r1 = call float @fmul_f32(float 0.0, float -1.0)                ; 0.0 * -1.0 = -0.0
  %r2 = call float @fdiv_f32(float 0.0, float 0.0)                 ; 0.0 / 0.0 = NaN
  %r3 = call float @maximumnum_f32(float 1.5, float -2.25)         ; (1.5, -2.25) = 1.5
  %k4 = fcmp oeq float %r3, 1.5
  %r5 = call float @maximumnum_f32(float -2.25, float 1.5)         ; (-2.25, 1.5) = 1.5
  %k6 = fcmp oeq float %r5, 1.5
  %r7 = call float @maximumnum_f32(float 3.0, float 3.0)           ; (3.0, 3.0) = 3.0
  %k8 = fcmp oeq float %r7, 3.0
  %r9 = call float @maximumnum_f32(float 0xFFF0000000000000, float 7.0) ; (-inf, 7.0) = 7.0
  %k10 = fcmp oeq float %r9, 7.0
  %r11 = call float @maximumnum_f32(float 7.0, float 0x7FF0000000000000) ; (7.0, inf) = inf
  %k12 = fcmp oeq float %r11, 0x7FF0000000000000
  %r13 = call float @maximumnum_f32(float 100.0, float 0.5)        ; (100.0, 0.5) = 100.0
  %k14 = fcmp oeq float %r13, 100.0
  %r15 = call float @maximumnum_f32(float 0.0, float -1.0)         ; (0.0, -1.0) = 0.0
  %k16 = fcmp oeq float %r15, 0.0
  %q17 = fdiv float 1.0, %r15
  %k18 = fcmp oeq float %q17, 0x7FF0000000000000
  %r19 = call float @maximumnum_f32(float -1.0, float 0.0)         ; (-1.0, 0.0) = 0.0
  %k20 = fcmp oeq float %r19, 0.0
  %q21 = fdiv float 1.0, %r19
  %k22 = fcmp oeq float %q21, 0x7FF0000000000000
  %r23 = call float @maximumnum_f32(float %r1, float 1.0)          ; (-0.0, 1.0) = 1.0
  %k24 = fcmp oeq float %r23, 1.0
  %r25 = call float @maximumnum_f32(float -1.0, float %r1)         ; (-1.0, -0.0) = -0.0
  %k26 = fcmp oeq float %r25, -0.0
  %q27 = fdiv float 1.0, %r25
  %k28 = fcmp oeq float %q27, 0xFFF0000000000000
  %r29 = call float @maximumnum_f32(float %r1, float 0.0)          ; (-0.0, 0.0) = 0.0
  %k30 = fcmp oeq float %r29, 0.0
  %q31 = fdiv float 1.0, %r29
  %k32 = fcmp oeq float %q31, 0x7FF0000000000000
  %r33 = call float @maximumnum_f32(float 0.0, float %r1)          ; (0.0, -0.0) = 0.0
  %k34 = fcmp oeq float %r33, 0.0
  %q35 = fdiv float 1.0, %r33
  %k36 = fcmp oeq float %q35, 0x7FF0000000000000
  %r37 = call float @maximumnum_f32(float %r1, float %r1)          ; (-0.0, -0.0) = -0.0
  %k38 = fcmp oeq float %r37, -0.0
  %q39 = fdiv float 1.0, %r37
  %k40 = fcmp oeq float %q39, 0xFFF0000000000000
  %r41 = call float @maximumnum_f32(float 0.0, float 0.0)          ; (0.0, 0.0) = 0.0
  %k42 = fcmp oeq float %r41, 0.0
  %q43 = fdiv float 1.0, %r41
  %k44 = fcmp oeq float %q43, 0x7FF0000000000000
  %r45 = call float @maximumnum_f32(float %r2, float 1.0)          ; (NaN, 1.0) = 1.0
  %k46 = fcmp oeq float %r45, 1.0
  %r47 = call float @maximumnum_f32(float 1.0, float %r2)          ; (1.0, NaN) = 1.0
  %k48 = fcmp oeq float %r47, 1.0
  %r49 = call float @maximumnum_f32(float %r2, float %r2)          ; (NaN, NaN) = NaN
  %k50 = fcmp uno float %r49, 0.0
  %r51 = call float @maximumnum_f32(float %r2, float %r1)          ; (NaN, -0.0) = -0.0
  %k52 = fcmp oeq float %r51, -0.0
  %q53 = fdiv float 1.0, %r51
  %k54 = fcmp oeq float %q53, 0xFFF0000000000000
  %r55 = call float @maximumnum_f32(float 0.0, float %r2)          ; (0.0, NaN) = 0.0
  %k56 = fcmp oeq float %r55, 0.0
  %q57 = fdiv float 1.0, %r55
  %k58 = fcmp oeq float %q57, 0x7FF0000000000000
  %r59 = call float @maximumnum_f32(float %r2, float 0xFFF0000000000000) ; (NaN, -inf) = -inf
  %k60 = fcmp oeq float %r59, 0xFFF0000000000000
  %r61 = call float @maximumnum_f32(float 0x7FF0000000000000, float %r2) ; (inf, NaN) = inf
  %k62 = fcmp oeq float %r61, 0x7FF0000000000000
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
  %all23 = and i1 %all22, %k50
  %all24 = and i1 %all23, %k52
  %all25 = and i1 %all24, %k54
  %all26 = and i1 %all25, %k56
  %all27 = and i1 %all26, %k58
  %all28 = and i1 %all27, %k60
  %all29 = and i1 %all28, %k62
  %bad = xor i1 %all29, true
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
