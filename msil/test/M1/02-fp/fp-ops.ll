; Floating-point arithmetic (Milestone 5): fadd, fsub, fmul, fdiv and fneg on
; float32 and float64 values, with exact IEEE-754 results: rounding to nearest
; even, signed zeros, infinities, NaN, subnormals (no flush to zero), constant
; operands on either side, and expression trees evaluated in one method. The
; operations run in noinline helpers; llc does no IPO, so they execute at -O2
; too. Each check compares a result with fcmp oeq against the exact value (NaN
; with fcmp uno; the sign of a zero through 1.0 / x, since fabs, copysign and
; bitcasts need frames until M7). Each noinline check function returns 1 when
; a result is wrong; main ORs one bit per check into the exit status (8 bits,
; D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

define double @fadd_f64(double %a, double %b) noinline {
  %r = fadd double %a, %b
  ret double %r
}

define double @fsub_f64(double %a, double %b) noinline {
  %r = fsub double %a, %b
  ret double %r
}

define double @fmul_f64(double %a, double %b) noinline {
  %r = fmul double %a, %b
  ret double %r
}

define double @fdiv_f64(double %a, double %b) noinline {
  %r = fdiv double %a, %b
  ret double %r
}

define float @fadd_f32(float %a, float %b) noinline {
  %r = fadd float %a, %b
  ret float %r
}

define float @fsub_f32(float %a, float %b) noinline {
  %r = fsub float %a, %b
  ret float %r
}

define float @fmul_f32(float %a, float %b) noinline {
  %r = fmul float %a, %b
  ret float %r
}

define float @fdiv_f32(float %a, float %b) noinline {
  %r = fdiv float %a, %b
  ret float %r
}

define double @fneg_f64(double %a) noinline {
  %r = fneg double %a
  ret double %r
}

define double @zero_minus_f64(double %a) noinline {
  %r = fsub double 0.0, %a
  ret double %r
}

define float @fneg_f32(float %a) noinline {
  %r = fneg float %a
  ret float %r
}

define float @zero_minus_f32(float %a) noinline {
  %r = fsub float 0.0, %a
  ret float %r
}

define double @add_one_f64(double %a) noinline {
  %r = fadd double %a, 1.0
  ret double %r
}

define double @ten_minus_f64(double %a) noinline {
  %r = fsub double 10.0, %a
  ret double %r
}

define double @mul_tenth_f64(double %a) noinline {
  %r = fmul double %a, 0x3FB999999999999A
  ret double %r
}

define double @div_three_f64(double %a) noinline {
  %r = fdiv double %a, 3.0
  ret double %r
}

define double @div_four_f64(double %a) noinline {
  %r = fdiv double %a, 4.0
  ret double %r
}

define double @recip_f64(double %a) noinline {
  %r = fdiv double 1.0, %a
  ret double %r
}

define float @add_one_f32(float %a) noinline {
  %r = fadd float %a, 1.0
  ret float %r
}

define float @ten_minus_f32(float %a) noinline {
  %r = fsub float 10.0, %a
  ret float %r
}

define float @mul_tenth_f32(float %a) noinline {
  %r = fmul float %a, 0x3FB99999A0000000
  ret float %r
}

define float @div_three_f32(float %a) noinline {
  %r = fdiv float %a, 3.0
  ret float %r
}

define float @div_four_f32(float %a) noinline {
  %r = fdiv float %a, 4.0
  ret float %r
}

define float @recip_f32(float %a) noinline {
  %r = fdiv float 1.0, %a
  ret float %r
}

define double @expr_f64(double %a, double %b, double %c, double %d) noinline {
  %m = fmul double %a, %b
  %s = fadd double %m, %c
  %t = fsub double %a, %d
  %r = fdiv double %s, %t
  ret double %r
}

define float @expr_f32(float %a, float %b, float %c, float %d) noinline {
  %m = fmul float %a, %b
  %s = fadd float %m, %c
  %t = fsub float %a, %d
  %r = fdiv float %s, %t
  ret float %r
}

; Bit 0: float64 add, sub, mul and div, rounded to nearest even.
define internal i32 @check_basic_f64() noinline {
  %r1 = call double @fadd_f64(double 1.5, double 2.25)             ; 1.5 + 2.25 = 3.75
  %k2 = fcmp oeq double %r1, 3.75
  %r3 = call double @fadd_f64(double 0x3FB999999999999A, double 0x3FC999999999999A) ; 0.1 + 0.2 = 0.30000000000000004
  %k4 = fcmp oeq double %r3, 0x3FD3333333333334
  %r5 = call double @fsub_f64(double 10.0, double 0.25)            ; 10.0 - 0.25 = 9.75
  %k6 = fcmp oeq double %r5, 9.75
  %r7 = call double @fsub_f64(double 0x3FD3333333333333, double 0x3FB999999999999A) ; 0.3 - 0.1 = 0.19999999999999998
  %k8 = fcmp oeq double %r7, 0x3FC9999999999999
  %r9 = call double @fsub_f64(double 2.5, double 7.75)             ; 2.5 - 7.75 = -5.25
  %k10 = fcmp oeq double %r9, -5.25
  %r11 = call double @fmul_f64(double -6.0, double 1.5)            ; -6.0 * 1.5 = -9.0
  %k12 = fcmp oeq double %r11, -9.0
  %r13 = call double @fmul_f64(double 3.0, double 0x3FB999999999999A) ; 3.0 * 0.1 = 0.30000000000000004
  %k14 = fcmp oeq double %r13, 0x3FD3333333333334
  %r15 = call double @fmul_f64(double 0x6974E718D7D7625A, double 0x0C06E93F5DA2824C) ; 1e+200 * 1e-250 = 1e-50
  %k16 = fcmp oeq double %r15, 0x358DEE7A4AD4B81F
  %r17 = call double @fdiv_f64(double 7.0, double 2.0)             ; 7.0 / 2.0 = 3.5
  %k18 = fcmp oeq double %r17, 3.5
  %r19 = call double @fdiv_f64(double 1.0, double 3.0)             ; 1.0 / 3.0 = 0.3333333333333333
  %k20 = fcmp oeq double %r19, 0x3FD5555555555555
  %r21 = call double @fdiv_f64(double -2.0, double 3.0)            ; -2.0 / 3.0 = -0.6666666666666666
  %k22 = fcmp oeq double %r21, 0xBFE5555555555555
  %r23 = call double @fdiv_f64(double 0x7E37E43C8800759C, double 0x3DDB7CDFD9D7BDBB) ; 1e+300 / 1e-10 = inf
  %k24 = fcmp oeq double %r23, 0x7FF0000000000000
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

; Bit 1: float32 add, sub, mul and div, rounded to float32.
define internal i32 @check_basic_f32() noinline {
  %r1 = call float @fadd_f32(float 1.5, float 2.25)                ; 1.5 + 2.25 = 3.75
  %k2 = fcmp oeq float %r1, 3.75
  %r3 = call float @fadd_f32(float 0x3FB99999A0000000, float 0x3FC99999A0000000) ; 0.1 + 0.2 = 0.3
  %k4 = fcmp oeq float %r3, 0x3FD3333340000000
  %r5 = call float @fsub_f32(float 10.0, float 0.25)               ; 10.0 - 0.25 = 9.75
  %k6 = fcmp oeq float %r5, 9.75
  %r7 = call float @fsub_f32(float 0x3FD3333340000000, float 0x3FB99999A0000000) ; 0.3 - 0.1 = 0.20000002
  %k8 = fcmp oeq float %r7, 0x3FC99999C0000000
  %r9 = call float @fmul_f32(float -6.0, float 1.5)                ; -6.0 * 1.5 = -9.0
  %k10 = fcmp oeq float %r9, -9.0
  %r11 = call float @fmul_f32(float 3.0, float 0x3FB99999A0000000) ; 3.0 * 0.1 = 0.3
  %k12 = fcmp oeq float %r11, 0x3FD3333340000000
  %r13 = call float @fmul_f32(float 4097.0, float 4097.0)          ; 4097.0 * 4097.0 = 16785408.0
  %k14 = fcmp oeq float %r13, 16785408.0
  %r15 = call float @fdiv_f32(float 7.0, float 2.0)                ; 7.0 / 2.0 = 3.5
  %k16 = fcmp oeq float %r15, 3.5
  %r17 = call float @fdiv_f32(float 1.0, float 3.0)                ; 1.0 / 3.0 = 0.33333334
  %k18 = fcmp oeq float %r17, 0x3FD5555560000000
  %r19 = call float @fdiv_f32(float -2.0, float 3.0)               ; -2.0 / 3.0 = -0.6666667
  %k20 = fcmp oeq float %r19, 0xBFE5555560000000
  %r21 = call float @fdiv_f32(float 16777215.0, float 16777216.0)  ; 16777215.0 / 16777216.0 = 0.99999994
  %k22 = fcmp oeq float %r21, 0x3FEFFFFFE0000000
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
  %bad = xor i1 %all10, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 2: fneg flips the sign of every value, zeros and infinities
; included (neg); 0.0 - x is not a negation (0.0 - 0.0 is +0.0).
define internal i32 @check_neg() noinline {
  %r1 = call double @fneg_f64(double 2.5)                          ; 2.5 -> -2.5
  %k2 = fcmp oeq double %r1, -2.5
  %r3 = call double @fneg_f64(double 0xBFB999999999999A)           ; -0.1 -> 0.1
  %k4 = fcmp oeq double %r3, 0x3FB999999999999A
  %r5 = call double @fneg_f64(double 0.0)                          ; 0.0 -> -0.0
  %k6 = fcmp oeq double %r5, -0.0
  %q7 = fdiv double 1.0, %r5
  %k8 = fcmp olt double %q7, 0.0
  %r9 = call double @fneg_f64(double -0.0)                         ; -0.0 -> 0.0
  %k10 = fcmp oeq double %r9, 0.0
  %q11 = fdiv double 1.0, %r9
  %k12 = fcmp ogt double %q11, 0.0
  %r13 = call double @fneg_f64(double 0x7FF0000000000000)          ; inf -> -inf
  %k14 = fcmp oeq double %r13, 0xFFF0000000000000
  %r15 = call double @fneg_f64(double 0x7FF8000000000000)          ; NaN -> NaN
  %k16 = fcmp uno double %r15, 0.0
  %r17 = call double @zero_minus_f64(double 0.0)                   ; 0.0 -> 0.0
  %k18 = fcmp oeq double %r17, 0.0
  %q19 = fdiv double 1.0, %r17
  %k20 = fcmp ogt double %q19, 0.0
  %r21 = call double @zero_minus_f64(double -0.0)                  ; -0.0 -> 0.0
  %k22 = fcmp oeq double %r21, 0.0
  %q23 = fdiv double 1.0, %r21
  %k24 = fcmp ogt double %q23, 0.0
  %r25 = call double @zero_minus_f64(double 1.25)                  ; 1.25 -> -1.25
  %k26 = fcmp oeq double %r25, -1.25
  %r27 = call float @fneg_f32(float 2.5)                           ; 2.5 -> -2.5
  %k28 = fcmp oeq float %r27, -2.5
  %r29 = call float @fneg_f32(float 0xBFB99999A0000000)            ; -0.1 -> 0.1
  %k30 = fcmp oeq float %r29, 0x3FB99999A0000000
  %r31 = call float @fneg_f32(float 0.0)                           ; 0.0 -> -0.0
  %k32 = fcmp oeq float %r31, -0.0
  %q33 = fdiv float 1.0, %r31
  %k34 = fcmp olt float %q33, 0.0
  %r35 = call float @fneg_f32(float -0.0)                          ; -0.0 -> 0.0
  %k36 = fcmp oeq float %r35, 0.0
  %q37 = fdiv float 1.0, %r35
  %k38 = fcmp ogt float %q37, 0.0
  %r39 = call float @fneg_f32(float 0x7FF0000000000000)            ; inf -> -inf
  %k40 = fcmp oeq float %r39, 0xFFF0000000000000
  %r41 = call float @fneg_f32(float 0x7FF8000000000000)            ; NaN -> NaN
  %k42 = fcmp uno float %r41, 0.0
  %r43 = call float @zero_minus_f32(float 0.0)                     ; 0.0 -> 0.0
  %k44 = fcmp oeq float %r43, 0.0
  %q45 = fdiv float 1.0, %r43
  %k46 = fcmp ogt float %q45, 0.0
  %r47 = call float @zero_minus_f32(float -0.0)                    ; -0.0 -> 0.0
  %k48 = fcmp oeq float %r47, 0.0
  %q49 = fdiv float 1.0, %r47
  %k50 = fcmp ogt float %q49, 0.0
  %r51 = call float @zero_minus_f32(float 1.25)                    ; 1.25 -> -1.25
  %k52 = fcmp oeq float %r51, -1.25
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
  %bad = xor i1 %all25, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 3: signed zeros: 0.0 * -1.0 and 1.0 / -inf are -0.0, x + -x is
; +0.0, -0.0 + -0.0 is -0.0; the sign is observed through 1.0 / x.
define internal i32 @check_signed_zero() noinline {
  %r1 = call double @fmul_f64(double 0.0, double -1.0)             ; 0.0 * -1.0 = -0.0
  %k2 = fcmp oeq double %r1, -0.0
  %q3 = fdiv double 1.0, %r1
  %k4 = fcmp olt double %q3, 0.0
  %r5 = call double @fmul_f64(double -0.0, double -1.0)            ; -0.0 * -1.0 = 0.0
  %k6 = fcmp oeq double %r5, 0.0
  %q7 = fdiv double 1.0, %r5
  %k8 = fcmp ogt double %q7, 0.0
  %r9 = call double @fdiv_f64(double 1.0, double 0xFFF0000000000000) ; 1.0 / -inf = -0.0
  %k10 = fcmp oeq double %r9, -0.0
  %q11 = fdiv double 1.0, %r9
  %k12 = fcmp olt double %q11, 0.0
  %r13 = call double @fdiv_f64(double -0.0, double 5.0)            ; -0.0 / 5.0 = -0.0
  %k14 = fcmp oeq double %r13, -0.0
  %q15 = fdiv double 1.0, %r13
  %k16 = fcmp olt double %q15, 0.0
  %r17 = call double @fadd_f64(double -0.0, double 0.0)            ; -0.0 + 0.0 = 0.0
  %k18 = fcmp oeq double %r17, 0.0
  %q19 = fdiv double 1.0, %r17
  %k20 = fcmp ogt double %q19, 0.0
  %r21 = call double @fadd_f64(double -0.0, double -0.0)           ; -0.0 + -0.0 = -0.0
  %k22 = fcmp oeq double %r21, -0.0
  %q23 = fdiv double 1.0, %r21
  %k24 = fcmp olt double %q23, 0.0
  %r25 = call double @fadd_f64(double 1.5, double -1.5)            ; 1.5 + -1.5 = 0.0
  %k26 = fcmp oeq double %r25, 0.0
  %q27 = fdiv double 1.0, %r25
  %k28 = fcmp ogt double %q27, 0.0
  %r29 = call double @fsub_f64(double -0.0, double 0.0)            ; -0.0 - 0.0 = -0.0
  %k30 = fcmp oeq double %r29, -0.0
  %q31 = fdiv double 1.0, %r29
  %k32 = fcmp olt double %q31, 0.0
  %r33 = call double @fsub_f64(double 0.0, double 0.0)             ; 0.0 - 0.0 = 0.0
  %k34 = fcmp oeq double %r33, 0.0
  %q35 = fdiv double 1.0, %r33
  %k36 = fcmp ogt double %q35, 0.0
  %r37 = call double @fsub_f64(double -0.0, double -0.0)           ; -0.0 - -0.0 = 0.0
  %k38 = fcmp oeq double %r37, 0.0
  %q39 = fdiv double 1.0, %r37
  %k40 = fcmp ogt double %q39, 0.0
  %r41 = call float @fmul_f32(float 0.0, float -1.0)               ; 0.0 * -1.0 = -0.0
  %k42 = fcmp oeq float %r41, -0.0
  %q43 = fdiv float 1.0, %r41
  %k44 = fcmp olt float %q43, 0.0
  %r45 = call float @fmul_f32(float -0.0, float -1.0)              ; -0.0 * -1.0 = 0.0
  %k46 = fcmp oeq float %r45, 0.0
  %q47 = fdiv float 1.0, %r45
  %k48 = fcmp ogt float %q47, 0.0
  %r49 = call float @fdiv_f32(float 1.0, float 0xFFF0000000000000) ; 1.0 / -inf = -0.0
  %k50 = fcmp oeq float %r49, -0.0
  %q51 = fdiv float 1.0, %r49
  %k52 = fcmp olt float %q51, 0.0
  %r53 = call float @fdiv_f32(float -0.0, float 5.0)               ; -0.0 / 5.0 = -0.0
  %k54 = fcmp oeq float %r53, -0.0
  %q55 = fdiv float 1.0, %r53
  %k56 = fcmp olt float %q55, 0.0
  %r57 = call float @fadd_f32(float -0.0, float 0.0)               ; -0.0 + 0.0 = 0.0
  %k58 = fcmp oeq float %r57, 0.0
  %q59 = fdiv float 1.0, %r57
  %k60 = fcmp ogt float %q59, 0.0
  %r61 = call float @fadd_f32(float -0.0, float -0.0)              ; -0.0 + -0.0 = -0.0
  %k62 = fcmp oeq float %r61, -0.0
  %q63 = fdiv float 1.0, %r61
  %k64 = fcmp olt float %q63, 0.0
  %r65 = call float @fadd_f32(float 1.5, float -1.5)               ; 1.5 + -1.5 = 0.0
  %k66 = fcmp oeq float %r65, 0.0
  %q67 = fdiv float 1.0, %r65
  %k68 = fcmp ogt float %q67, 0.0
  %r69 = call float @fsub_f32(float -0.0, float 0.0)               ; -0.0 - 0.0 = -0.0
  %k70 = fcmp oeq float %r69, -0.0
  %q71 = fdiv float 1.0, %r69
  %k72 = fcmp olt float %q71, 0.0
  %r73 = call float @fsub_f32(float 0.0, float 0.0)                ; 0.0 - 0.0 = 0.0
  %k74 = fcmp oeq float %r73, 0.0
  %q75 = fdiv float 1.0, %r73
  %k76 = fcmp ogt float %q75, 0.0
  %r77 = call float @fsub_f32(float -0.0, float -0.0)              ; -0.0 - -0.0 = 0.0
  %k78 = fcmp oeq float %r77, 0.0
  %q79 = fdiv float 1.0, %r77
  %k80 = fcmp ogt float %q79, 0.0
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
  %bad = xor i1 %all39, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 4: infinities and NaN: division by zero, overflow, invalid
; operations and NaN propagation (NaN is tested with fcmp uno).
define internal i32 @check_inf_nan() noinline {
  %r1 = call double @fdiv_f64(double 1.0, double 0.0)              ; 1.0 / 0.0 = inf
  %k2 = fcmp oeq double %r1, 0x7FF0000000000000
  %r3 = call double @fdiv_f64(double -1.0, double 0.0)             ; -1.0 / 0.0 = -inf
  %k4 = fcmp oeq double %r3, 0xFFF0000000000000
  %r5 = call double @fdiv_f64(double 1.0, double -0.0)             ; 1.0 / -0.0 = -inf
  %k6 = fcmp oeq double %r5, 0xFFF0000000000000
  %r7 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %k8 = fcmp uno double %r7, 0.0
  %r9 = call double @fsub_f64(double 0x7FF0000000000000, double 0x7FF0000000000000) ; inf - inf = NaN
  %k10 = fcmp uno double %r9, 0.0
  %r11 = call double @fmul_f64(double 0x7FF0000000000000, double 0.0) ; inf * 0.0 = NaN
  %k12 = fcmp uno double %r11, 0.0
  %r13 = call double @fadd_f64(double 0x7FF8000000000000, double 1.0) ; NaN + 1.0 = NaN
  %k14 = fcmp uno double %r13, 0.0
  %r15 = call double @fmul_f64(double -2.0, double 0x7FF8000000000000) ; -2.0 * NaN = NaN
  %k16 = fcmp uno double %r15, 0.0
  %r17 = call double @fadd_f64(double 0x7FF0000000000000, double 1.0) ; inf + 1.0 = inf
  %k18 = fcmp oeq double %r17, 0x7FF0000000000000
  %r19 = call double @fsub_f64(double 0xFFF0000000000000, double 0x46293E5939A08CEA) ; -inf - 1e+30 = -inf
  %k20 = fcmp oeq double %r19, 0xFFF0000000000000
  %r21 = call double @fmul_f64(double 0x7FE1CCF385EBC8A0, double 10.0) ; 1e+308 * 10.0 = inf
  %k22 = fcmp oeq double %r21, 0x7FF0000000000000
  %r23 = call double @fmul_f64(double 0xFFE1CCF385EBC8A0, double 10.0) ; -1e+308 * 10.0 = -inf
  %k24 = fcmp oeq double %r23, 0xFFF0000000000000
  %r25 = call double @fdiv_f64(double 5.0, double 0x7FF0000000000000) ; 5.0 / inf = 0.0
  %k26 = fcmp oeq double %r25, 0.0
  %q27 = fdiv double 1.0, %r25
  %k28 = fcmp ogt double %q27, 0.0
  %r29 = call float @fdiv_f32(float 1.0, float 0.0)                ; 1.0 / 0.0 = inf
  %k30 = fcmp oeq float %r29, 0x7FF0000000000000
  %r31 = call float @fdiv_f32(float -1.0, float 0.0)               ; -1.0 / 0.0 = -inf
  %k32 = fcmp oeq float %r31, 0xFFF0000000000000
  %r33 = call float @fdiv_f32(float 1.0, float -0.0)               ; 1.0 / -0.0 = -inf
  %k34 = fcmp oeq float %r33, 0xFFF0000000000000
  %r35 = call float @fdiv_f32(float 0.0, float 0.0)                ; 0.0 / 0.0 = NaN
  %k36 = fcmp uno float %r35, 0.0
  %r37 = call float @fsub_f32(float 0x7FF0000000000000, float 0x7FF0000000000000) ; inf - inf = NaN
  %k38 = fcmp uno float %r37, 0.0
  %r39 = call float @fmul_f32(float 0x7FF0000000000000, float 0.0) ; inf * 0.0 = NaN
  %k40 = fcmp uno float %r39, 0.0
  %r41 = call float @fadd_f32(float 0x7FF8000000000000, float 1.0) ; NaN + 1.0 = NaN
  %k42 = fcmp uno float %r41, 0.0
  %r43 = call float @fmul_f32(float -2.0, float 0x7FF8000000000000) ; -2.0 * NaN = NaN
  %k44 = fcmp uno float %r43, 0.0
  %r45 = call float @fadd_f32(float 0x7FF0000000000000, float 1.0) ; inf + 1.0 = inf
  %k46 = fcmp oeq float %r45, 0x7FF0000000000000
  %r47 = call float @fsub_f32(float 0xFFF0000000000000, float 0x46293E5940000000) ; -inf - 1e+30 = -inf
  %k48 = fcmp oeq float %r47, 0xFFF0000000000000
  %r49 = call float @fmul_f32(float 0x47EC363CC0000000, float 10.0) ; 3e+38 * 10.0 = inf
  %k50 = fcmp oeq float %r49, 0x7FF0000000000000
  %r51 = call float @fmul_f32(float 0xC7EC363CC0000000, float 10.0) ; -3e+38 * 10.0 = -inf
  %k52 = fcmp oeq float %r51, 0xFFF0000000000000
  %r53 = call float @fdiv_f32(float 5.0, float 0x7FF0000000000000) ; 5.0 / inf = 0.0
  %k54 = fcmp oeq float %r53, 0.0
  %q55 = fdiv float 1.0, %r53
  %k56 = fcmp ogt float %q55, 0.0
  %r57 = call float @fmul_f32(float 0x46293E5940000000, float 10000000000.0) ; 1e+30 * 10000000000.0 = inf
  %k58 = fcmp oeq float %r57, 0x7FF0000000000000
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
  %bad = xor i1 %all28, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 5: gradual underflow: subnormal results, rounding at the
; smallest subnormal (ties to even), and no flush to zero.
define internal i32 @check_subnormal() noinline {
  %r1 = call double @fdiv_f64(double 0x0010000000000000, double 2.0) ; 2.2250738585072014e-308 / 2.0 = 1.1125369292536007e-308
  %k2 = fcmp oeq double %r1, 0x0008000000000000
  %r3 = call double @fmul_f64(double 0x0000000000000001, double 0.5) ; 5e-324 * 0.5 = 0.0
  %k4 = fcmp oeq double %r3, 0.0
  %q5 = fdiv double 1.0, %r3
  %k6 = fcmp ogt double %q5, 0.0
  %r7 = call double @fmul_f64(double 0x0000000000000001, double -0.5) ; 5e-324 * -0.5 = -0.0
  %k8 = fcmp oeq double %r7, -0.0
  %q9 = fdiv double 1.0, %r7
  %k10 = fcmp olt double %q9, 0.0
  %r11 = call double @fmul_f64(double 0x0000000000000001, double 1.5) ; 5e-324 * 1.5 = 1e-323
  %k12 = fcmp oeq double %r11, 0x0000000000000002
  %r13 = call double @fadd_f64(double 0x0000000000000001, double 0x0000000000000001) ; 5e-324 + 5e-324 = 1e-323
  %k14 = fcmp oeq double %r13, 0x0000000000000002
  %r15 = call double @fadd_f64(double 0x000FFFFFFFFFFFFF, double 0x0000000000000001) ; 2.225073858507201e-308 + 5e-324 = 2.2250738585072014e-308
  %k16 = fcmp oeq double %r15, 0x0010000000000000
  %r17 = call double @fsub_f64(double 0x0010000000000000, double 0x0000000000000001) ; 2.2250738585072014e-308 - 5e-324 = 2.225073858507201e-308
  %k18 = fcmp oeq double %r17, 0x000FFFFFFFFFFFFF
  %r19 = call double @fmul_f64(double 0x0000000000000001, double 0x7E70000000000000) ; 5e-324 * 1.0715086071862673e+301 = 5.293955920339377e-23
  %k20 = fcmp oeq double %r19, 0x3B50000000000000
  %r21 = call double @fdiv_f64(double 0x01A56E1FC2F8F359, double 10000000000.0) ; 1e-300 / 10000000000.0 = 1e-310
  %k22 = fcmp oeq double %r21, 0x000012688B70E62B
  %r23 = call float @fdiv_f32(float 0x3810000000000000, float 2.0) ; 1.1754944e-38 / 2.0 = 5.877472e-39
  %k24 = fcmp oeq float %r23, 0x3800000000000000
  %r25 = call float @fmul_f32(float 0x36A0000000000000, float 0.5) ; 1e-45 * 0.5 = 0.0
  %k26 = fcmp oeq float %r25, 0.0
  %q27 = fdiv float 1.0, %r25
  %k28 = fcmp ogt float %q27, 0.0
  %r29 = call float @fmul_f32(float 0x36A0000000000000, float -0.5) ; 1e-45 * -0.5 = -0.0
  %k30 = fcmp oeq float %r29, -0.0
  %q31 = fdiv float 1.0, %r29
  %k32 = fcmp olt float %q31, 0.0
  %r33 = call float @fmul_f32(float 0x36A0000000000000, float 1.5) ; 1e-45 * 1.5 = 3e-45
  %k34 = fcmp oeq float %r33, 0x36B0000000000000
  %r35 = call float @fadd_f32(float 0x36A0000000000000, float 0x36A0000000000000) ; 1e-45 + 1e-45 = 3e-45
  %k36 = fcmp oeq float %r35, 0x36B0000000000000
  %r37 = call float @fadd_f32(float 0x380FFFFFC0000000, float 0x36A0000000000000) ; 1.1754942e-38 + 1e-45 = 1.1754944e-38
  %k38 = fcmp oeq float %r37, 0x3810000000000000
  %r39 = call float @fsub_f32(float 0x3810000000000000, float 0x36A0000000000000) ; 1.1754944e-38 - 1e-45 = 1.1754942e-38
  %k40 = fcmp oeq float %r39, 0x380FFFFFC0000000
  %r41 = call float @fmul_f32(float 0x36A0000000000000, float 0x4630000000000000) ; 1e-45 * 1.2676506e+30 = 1.7763568e-15
  %k42 = fcmp oeq float %r41, 0x3CE0000000000000
  %r43 = call float @fmul_f32(float 0x39B4484C00000000, float 0x3DDB7CDFE0000000) ; 1e-30 * 1e-10 = 1e-40
  %k44 = fcmp oeq float %r43, 0x37A16C2000000000
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
  %bad = xor i1 %all21, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 6: one constant operand, on the left or on the right (x + 1,
; 10 - x, x * 0.1, x / 3, x / 4, 1 / x).
define internal i32 @check_const_operands() noinline {
  %r1 = call double @add_one_f64(double 2.5)                       ; 2.5 -> 3.5
  %k2 = fcmp oeq double %r1, 3.5
  %r3 = call double @add_one_f64(double -1.0)                      ; -1.0 -> 0.0
  %k4 = fcmp oeq double %r3, 0.0
  %q5 = fdiv double 1.0, %r3
  %k6 = fcmp ogt double %q5, 0.0
  %r7 = call double @ten_minus_f64(double 0.75)                    ; 0.75 -> 9.25
  %k8 = fcmp oeq double %r7, 9.25
  %r9 = call double @ten_minus_f64(double 12.0)                    ; 12.0 -> -2.0
  %k10 = fcmp oeq double %r9, -2.0
  %r11 = call double @mul_tenth_f64(double 3.0)                    ; 3.0 -> 0.30000000000000004
  %k12 = fcmp oeq double %r11, 0x3FD3333333333334
  %r13 = call double @div_three_f64(double 1.0)                    ; 1.0 -> 0.3333333333333333
  %k14 = fcmp oeq double %r13, 0x3FD5555555555555
  %r15 = call double @div_three_f64(double -7.0)                   ; -7.0 -> -2.3333333333333335
  %k16 = fcmp oeq double %r15, 0xC002AAAAAAAAAAAB
  %r17 = call double @div_four_f64(double 7.0)                     ; 7.0 -> 1.75
  %k18 = fcmp oeq double %r17, 1.75
  %r19 = call double @recip_f64(double 8.0)                        ; 8.0 -> 0.125
  %k20 = fcmp oeq double %r19, 0.125
  %r21 = call double @recip_f64(double -0.0)                       ; -0.0 -> -inf
  %k22 = fcmp oeq double %r21, 0xFFF0000000000000
  %r23 = call double @recip_f64(double 3.0)                        ; 3.0 -> 0.3333333333333333
  %k24 = fcmp oeq double %r23, 0x3FD5555555555555
  %r25 = call float @add_one_f32(float 2.5)                        ; 2.5 -> 3.5
  %k26 = fcmp oeq float %r25, 3.5
  %r27 = call float @add_one_f32(float -1.0)                       ; -1.0 -> 0.0
  %k28 = fcmp oeq float %r27, 0.0
  %q29 = fdiv float 1.0, %r27
  %k30 = fcmp ogt float %q29, 0.0
  %r31 = call float @ten_minus_f32(float 0.75)                     ; 0.75 -> 9.25
  %k32 = fcmp oeq float %r31, 9.25
  %r33 = call float @ten_minus_f32(float 12.0)                     ; 12.0 -> -2.0
  %k34 = fcmp oeq float %r33, -2.0
  %r35 = call float @mul_tenth_f32(float 3.0)                      ; 3.0 -> 0.3
  %k36 = fcmp oeq float %r35, 0x3FD3333340000000
  %r37 = call float @div_three_f32(float 1.0)                      ; 1.0 -> 0.33333334
  %k38 = fcmp oeq float %r37, 0x3FD5555560000000
  %r39 = call float @div_three_f32(float -7.0)                     ; -7.0 -> -2.3333333
  %k40 = fcmp oeq float %r39, 0xC002AAAAA0000000
  %r41 = call float @div_four_f32(float 7.0)                       ; 7.0 -> 1.75
  %k42 = fcmp oeq float %r41, 1.75
  %r43 = call float @recip_f32(float 8.0)                          ; 8.0 -> 0.125
  %k44 = fcmp oeq float %r43, 0.125
  %r45 = call float @recip_f32(float -0.0)                         ; -0.0 -> -inf
  %k46 = fcmp oeq float %r45, 0xFFF0000000000000
  %r47 = call float @recip_f32(float 3.0)                          ; 3.0 -> 0.33333334
  %k48 = fcmp oeq float %r47, 0x3FD5555560000000
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
  %bad = xor i1 %all23, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 7: expression trees, (a * b + c) / (a - d) in one method; each
; step is rounded to the type (no fused or wider intermediate).
define internal i32 @check_expr() noinline {
  %r1 = call double @expr_f64(double 0x3FF199999999999A, double 0x400199999999999A, double 0x400A666666666666, double 0x3FD999999999999A) ; = 8.17142857142857
  %k2 = fcmp oeq double %r1, 0x402057C57C57C57C
  %r3 = call double @expr_f64(double 3.0, double 7.0, double -1.0, double 1.0) ; = 10.0
  %k4 = fcmp oeq double %r3, 10.0
  %r5 = call double @expr_f64(double 10000000000.0, double 10000000000.0, double 1.0, double 9999999999.0) ; = 1e+20
  %k6 = fcmp oeq double %r5, 1.0e+20
  %r7 = call double @expr_f64(double 0x3FB999999999999A, double 0x3FB999999999999A, double 0xBF847AE147AE147B, double 0x3FC999999999999A) ; = -1.734723475976807e-17
  %k8 = fcmp oeq double %r7, 0xBC74000000000000
  %r9 = call float @expr_f32(float 0x3FF19999A0000000, float 0x40019999A0000000, float 0x400A666660000000, float 0x3FD99999A0000000) ; = 8.171429
  %k10 = fcmp oeq float %r9, 0x402057C580000000
  %r11 = call float @expr_f32(float 3.0, float 7.0, float -1.0, float 1.0) ; = 10.0
  %k12 = fcmp oeq float %r11, 10.0
  %r13 = call float @expr_f32(float 4096.0, float 4097.0, float 1.0, float 4095.0) ; = 16781312.0
  %k14 = fcmp oeq float %r13, 16781312.0
  %r15 = call float @expr_f32(float 0x3FB99999A0000000, float 0x3FB99999A0000000, float 0xBF847AE140000000, float 0x3FC99999A0000000) ; = -9.313226e-09
  %k16 = fcmp oeq float %r15, 0xBE44000000000000
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %all6 = and i1 %all5, %k14
  %all7 = and i1 %all6, %k16
  %bad = xor i1 %all7, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

define i32 @main() {
  %b0 = call i32 @check_basic_f64()
  %b1 = call i32 @check_basic_f32()
  %s1 = shl i32 %b1, 1
  %b2 = call i32 @check_neg()
  %s2 = shl i32 %b2, 2
  %b3 = call i32 @check_signed_zero()
  %s3 = shl i32 %b3, 3
  %b4 = call i32 @check_inf_nan()
  %s4 = shl i32 %b4, 4
  %b5 = call i32 @check_subnormal()
  %s5 = shl i32 %b5, 5
  %b6 = call i32 @check_const_operands()
  %s6 = shl i32 %b6, 6
  %b7 = call i32 @check_expr()
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
