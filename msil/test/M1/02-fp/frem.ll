; frem (Milestone 5): the remainder of float32 and float64 values is C's fmod,
; exact, with the sign of the dividend (CIL rem, isel.md 3.2): huge
; quotients, signed zero results, NaN for inf rem x and x rem 0, x rem inf =
; x, subnormals, constant operands, and the remainder used in further
; arithmetic. Each noinline check function returns 1 when a result is wrong;
; main ORs one bit per check into the exit status (8 bits, D37): [exit 0]
; means every check passed.
; RUN: %{msil_ll}

define double @frem_f64(double %a, double %b) noinline {
  %r = frem double %a, %b
  ret double %r
}

define float @frem_f32(float %a, float %b) noinline {
  %r = frem float %a, %b
  ret float %r
}

define double @frem_expr_f64(double %a, double %b, double %c) noinline {
  %r = frem double %a, %b
  %s = fmul double %r, %c
  %t = fadd double %s, %r
  ret double %t
}

define double @frem_by_10_f64(double %a) noinline {
  %r = frem double %a, 10.0
  ret double %r
}

define double @frem_100_by_f64(double %b) noinline {
  %r = frem double 100.0, %b
  ret double %r
}

define float @frem_expr_f32(float %a, float %b, float %c) noinline {
  %r = frem float %a, %b
  %s = fmul float %r, %c
  %t = fadd float %s, %r
  ret float %t
}

define float @frem_by_10_f32(float %a) noinline {
  %r = frem float %a, 10.0
  ret float %r
}

define float @frem_100_by_f32(float %b) noinline {
  %r = frem float 100.0, %b
  ret float %r
}

define float @frem_widened(float %a, float %b) noinline {
  %x = fpext float %a to double
  %y = fpext float %b to double
  %r = frem double %x, %y
  %t = fptrunc double %r to float
  ret float %t
}

; Bit 0: float64 remainders take the sign of the dividend and are exact,
; also for huge quotients (1e300 rem 3, 2^1000 rem 3, 1e308 rem 1e-308).
define internal i32 @check_basic_f64() noinline {
  %r1 = call double @frem_f64(double 5.5, double 2.0)              ; 5.5 rem 2.0 = 1.5
  %k2 = fcmp oeq double %r1, 1.5
  %r3 = call double @frem_f64(double -5.5, double 2.0)             ; -5.5 rem 2.0 = -1.5
  %k4 = fcmp oeq double %r3, -1.5
  %r5 = call double @frem_f64(double 5.5, double -2.0)             ; 5.5 rem -2.0 = 1.5
  %k6 = fcmp oeq double %r5, 1.5
  %r7 = call double @frem_f64(double -5.5, double -2.0)            ; -5.5 rem -2.0 = -1.5
  %k8 = fcmp oeq double %r7, -1.5
  %r9 = call double @frem_f64(double 7.0, double 2.5)              ; 7.0 rem 2.5 = 2.0
  %k10 = fcmp oeq double %r9, 2.0
  %r11 = call double @frem_f64(double 0x3FB999999999999A, double 0x3F9EB851EB851EB8) ; 0.1 rem 0.03 = 0.010000000000000009
  %k12 = fcmp oeq double %r11, 0x3F847AE147AE1480
  %r13 = call double @frem_f64(double 0x7E37E43C8800759C, double 3.0) ; 1e+300 rem 3.0 = 0.0
  %k14 = fcmp oeq double %r13, 0.0
  %q15 = fdiv double 1.0, %r13
  %k16 = fcmp ogt double %q15, 0.0
  %r17 = call double @frem_f64(double 0x7E70000000000000, double 3.0) ; 1.0715086071862673e+301 rem 3.0 = 1.0
  %k18 = fcmp oeq double %r17, 1.0
  %r19 = call double @frem_f64(double 0x7FE1CCF385EBC8A0, double 0x000730D67819E8D2) ; 1e+308 rem 1e-308 = 3.498445546245627e-309
  %k20 = fcmp oeq double %r19, 0x00028401CF53D610
  %r21 = call double @frem_f64(double 123456789.0, double 0x3F50624DD2F1A9FC) ; 123456789.0 rem 0.001 = 0.0009999974300393183
  %k22 = fcmp oeq double %r21, 0x3F50624B1084C21C
  %r23 = call double @frem_f64(double 1.0, double 3.0)             ; 1.0 rem 3.0 = 1.0
  %k24 = fcmp oeq double %r23, 1.0
  %r25 = call double @frem_f64(double -9.75, double 0.5)           ; -9.75 rem 0.5 = -0.25
  %k26 = fcmp oeq double %r25, -0.25
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

; Bit 1: float32 remainders, exact (the result of two floats is a float).
define internal i32 @check_basic_f32() noinline {
  %r1 = call float @frem_f32(float 5.5, float 2.0)                 ; 5.5 rem 2.0 = 1.5
  %k2 = fcmp oeq float %r1, 1.5
  %r3 = call float @frem_f32(float -7.25, float 2.0)               ; -7.25 rem 2.0 = -1.25
  %k4 = fcmp oeq float %r3, -1.25
  %r5 = call float @frem_f32(float 7.25, float -2.0)               ; 7.25 rem -2.0 = 1.25
  %k6 = fcmp oeq float %r5, 1.25
  %r7 = call float @frem_f32(float 0x3FB99999A0000000, float 0x3F9EB851E0000000) ; 0.1 rem 0.03 = 0.0100000035
  %k8 = fcmp oeq float %r7, 0x3F847AE1C0000000
  %r9 = call float @frem_f32(float 0x46293E5940000000, float 3.0)  ; 1e+30 rem 3.0 = 0.0
  %k10 = fcmp oeq float %r9, 0.0
  %q11 = fdiv float 1.0, %r9
  %k12 = fcmp ogt float %q11, 0.0
  %r13 = call float @frem_f32(float 0x47EFFFFFE0000000, float 7.0) ; 3.4028235e+38 rem 7.0 = 0.0
  %k14 = fcmp oeq float %r13, 0.0
  %q15 = fdiv float 1.0, %r13
  %k16 = fcmp ogt float %q15, 0.0
  %r17 = call float @frem_f32(float 16777215.0, float 0.75)        ; 16777215.0 rem 0.75 = 0.0
  %k18 = fcmp oeq float %r17, 0.0
  %q19 = fdiv float 1.0, %r17
  %k20 = fcmp ogt float %q19, 0.0
  %r21 = call float @frem_f32(float 1.0, float 3.0)                ; 1.0 rem 3.0 = 1.0
  %k22 = fcmp oeq float %r21, 1.0
  %r23 = call float @frem_f32(float -9.75, float 0.5)              ; -9.75 rem 0.5 = -0.25
  %k24 = fcmp oeq float %r23, -0.25
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

; Bit 2: zero results keep the sign of the dividend: -0.0 rem 1 is -0.0,
; -5 rem 5 is -0.0, 5 rem -5 is +0.0, -0.0 rem inf is -0.0.
define internal i32 @check_zero_results() noinline {
  %r1 = call double @frem_f64(double -0.0, double 1.0)             ; -0.0 rem 1.0 = -0.0
  %k2 = fcmp oeq double %r1, -0.0
  %q3 = fdiv double 1.0, %r1
  %k4 = fcmp olt double %q3, 0.0
  %r5 = call double @frem_f64(double 0.0, double 1.0)              ; 0.0 rem 1.0 = 0.0
  %k6 = fcmp oeq double %r5, 0.0
  %q7 = fdiv double 1.0, %r5
  %k8 = fcmp ogt double %q7, 0.0
  %r9 = call double @frem_f64(double -5.0, double 5.0)             ; -5.0 rem 5.0 = -0.0
  %k10 = fcmp oeq double %r9, -0.0
  %q11 = fdiv double 1.0, %r9
  %k12 = fcmp olt double %q11, 0.0
  %r13 = call double @frem_f64(double 5.0, double 5.0)             ; 5.0 rem 5.0 = 0.0
  %k14 = fcmp oeq double %r13, 0.0
  %q15 = fdiv double 1.0, %r13
  %k16 = fcmp ogt double %q15, 0.0
  %r17 = call double @frem_f64(double 5.0, double -5.0)            ; 5.0 rem -5.0 = 0.0
  %k18 = fcmp oeq double %r17, 0.0
  %q19 = fdiv double 1.0, %r17
  %k20 = fcmp ogt double %q19, 0.0
  %r21 = call double @frem_f64(double -0.0, double 0x7FF0000000000000) ; -0.0 rem inf = -0.0
  %k22 = fcmp oeq double %r21, -0.0
  %q23 = fdiv double 1.0, %r21
  %k24 = fcmp olt double %q23, 0.0
  %r25 = call double @frem_f64(double -6.0, double 1.5)            ; -6.0 rem 1.5 = -0.0
  %k26 = fcmp oeq double %r25, -0.0
  %q27 = fdiv double 1.0, %r25
  %k28 = fcmp olt double %q27, 0.0
  %r29 = call double @frem_f64(double 0.0, double -3.0)            ; 0.0 rem -3.0 = 0.0
  %k30 = fcmp oeq double %r29, 0.0
  %q31 = fdiv double 1.0, %r29
  %k32 = fcmp ogt double %q31, 0.0
  %r33 = call float @frem_f32(float -0.0, float 1.0)               ; -0.0 rem 1.0 = -0.0
  %k34 = fcmp oeq float %r33, -0.0
  %q35 = fdiv float 1.0, %r33
  %k36 = fcmp olt float %q35, 0.0
  %r37 = call float @frem_f32(float 0.0, float 1.0)                ; 0.0 rem 1.0 = 0.0
  %k38 = fcmp oeq float %r37, 0.0
  %q39 = fdiv float 1.0, %r37
  %k40 = fcmp ogt float %q39, 0.0
  %r41 = call float @frem_f32(float -5.0, float 5.0)               ; -5.0 rem 5.0 = -0.0
  %k42 = fcmp oeq float %r41, -0.0
  %q43 = fdiv float 1.0, %r41
  %k44 = fcmp olt float %q43, 0.0
  %r45 = call float @frem_f32(float 5.0, float 5.0)                ; 5.0 rem 5.0 = 0.0
  %k46 = fcmp oeq float %r45, 0.0
  %q47 = fdiv float 1.0, %r45
  %k48 = fcmp ogt float %q47, 0.0
  %r49 = call float @frem_f32(float 5.0, float -5.0)               ; 5.0 rem -5.0 = 0.0
  %k50 = fcmp oeq float %r49, 0.0
  %q51 = fdiv float 1.0, %r49
  %k52 = fcmp ogt float %q51, 0.0
  %r53 = call float @frem_f32(float -0.0, float 0x7FF0000000000000) ; -0.0 rem inf = -0.0
  %k54 = fcmp oeq float %r53, -0.0
  %q55 = fdiv float 1.0, %r53
  %k56 = fcmp olt float %q55, 0.0
  %r57 = call float @frem_f32(float -6.0, float 1.5)               ; -6.0 rem 1.5 = -0.0
  %k58 = fcmp oeq float %r57, -0.0
  %q59 = fdiv float 1.0, %r57
  %k60 = fcmp olt float %q59, 0.0
  %r61 = call float @frem_f32(float 0.0, float -3.0)               ; 0.0 rem -3.0 = 0.0
  %k62 = fcmp oeq float %r61, 0.0
  %q63 = fdiv float 1.0, %r61
  %k64 = fcmp ogt float %q63, 0.0
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
  %bad = xor i1 %all31, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 3: float64 NaN results: inf rem x, x rem 0, and NaN operands.
define internal i32 @check_nan_f64() noinline {
  %r1 = call double @frem_f64(double 0x7FF0000000000000, double 2.0) ; inf rem 2.0 = NaN
  %k2 = fcmp uno double %r1, 0.0
  %r3 = call double @frem_f64(double 0xFFF0000000000000, double 2.0) ; -inf rem 2.0 = NaN
  %k4 = fcmp uno double %r3, 0.0
  %r5 = call double @frem_f64(double 5.0, double 0.0)              ; 5.0 rem 0.0 = NaN
  %k6 = fcmp uno double %r5, 0.0
  %r7 = call double @frem_f64(double 5.0, double -0.0)             ; 5.0 rem -0.0 = NaN
  %k8 = fcmp uno double %r7, 0.0
  %r9 = call double @frem_f64(double 0x7FF8000000000000, double 1.0) ; NaN rem 1.0 = NaN
  %k10 = fcmp uno double %r9, 0.0
  %r11 = call double @frem_f64(double 1.0, double 0x7FF8000000000000) ; 1.0 rem NaN = NaN
  %k12 = fcmp uno double %r11, 0.0
  %r13 = call double @frem_f64(double 0.0, double 0.0)             ; 0.0 rem 0.0 = NaN
  %k14 = fcmp uno double %r13, 0.0
  %r15 = call double @frem_f64(double 0x7FF0000000000000, double 0x7FF0000000000000) ; inf rem inf = NaN
  %k16 = fcmp uno double %r15, 0.0
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

; Bit 4: float32 NaN results: inf rem x, x rem 0, and NaN operands.
define internal i32 @check_nan_f32() noinline {
  %r1 = call float @frem_f32(float 0x7FF0000000000000, float 2.0)  ; inf rem 2.0 = NaN
  %k2 = fcmp uno float %r1, 0.0
  %r3 = call float @frem_f32(float 0xFFF0000000000000, float 2.0)  ; -inf rem 2.0 = NaN
  %k4 = fcmp uno float %r3, 0.0
  %r5 = call float @frem_f32(float 5.0, float 0.0)                 ; 5.0 rem 0.0 = NaN
  %k6 = fcmp uno float %r5, 0.0
  %r7 = call float @frem_f32(float 5.0, float -0.0)                ; 5.0 rem -0.0 = NaN
  %k8 = fcmp uno float %r7, 0.0
  %r9 = call float @frem_f32(float 0x7FF8000000000000, float 1.0)  ; NaN rem 1.0 = NaN
  %k10 = fcmp uno float %r9, 0.0
  %r11 = call float @frem_f32(float 1.0, float 0x7FF8000000000000) ; 1.0 rem NaN = NaN
  %k12 = fcmp uno float %r11, 0.0
  %r13 = call float @frem_f32(float 0.0, float 0.0)                ; 0.0 rem 0.0 = NaN
  %k14 = fcmp uno float %r13, 0.0
  %r15 = call float @frem_f32(float 0x7FF0000000000000, float 0x7FF0000000000000) ; inf rem inf = NaN
  %k16 = fcmp uno float %r15, 0.0
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

; Bit 5: x rem inf is x, and subnormal operands are exact.
define internal i32 @check_inf_divisor() noinline {
  %r1 = call double @frem_f64(double 5.0, double 0x7FF0000000000000) ; 5.0 rem inf = 5.0
  %k2 = fcmp oeq double %r1, 5.0
  %r3 = call double @frem_f64(double -5.0, double 0xFFF0000000000000) ; -5.0 rem -inf = -5.0
  %k4 = fcmp oeq double %r3, -5.0
  %r5 = call double @frem_f64(double -2.5, double 0x7FF0000000000000) ; -2.5 rem inf = -2.5
  %k6 = fcmp oeq double %r5, -2.5
  %r7 = call double @frem_f64(double 0x000FFFFFFFFFFFFF, double 0x0000000000000003) ; 2.225073858507201e-308 rem 1.5e-323 = 0.0
  %k8 = fcmp oeq double %r7, 0.0
  %q9 = fdiv double 1.0, %r7
  %k10 = fcmp ogt double %q9, 0.0
  %r11 = call double @frem_f64(double 1.0, double 0x000FFFFFFFFFFFFF) ; 1.0 rem 2.225073858507201e-308 = 8.487983164e-314
  %k12 = fcmp oeq double %r11, 0x0000000400000000
  %r13 = call double @frem_f64(double 0x0000000000000007, double 0x0000000000000002) ; 3.5e-323 rem 1e-323 = 5e-324
  %k14 = fcmp oeq double %r13, 0x0000000000000001
  %r15 = call float @frem_f32(float 5.0, float 0x7FF0000000000000) ; 5.0 rem inf = 5.0
  %k16 = fcmp oeq float %r15, 5.0
  %r17 = call float @frem_f32(float -5.0, float 0xFFF0000000000000) ; -5.0 rem -inf = -5.0
  %k18 = fcmp oeq float %r17, -5.0
  %r19 = call float @frem_f32(float -2.5, float 0x7FF0000000000000) ; -2.5 rem inf = -2.5
  %k20 = fcmp oeq float %r19, -2.5
  %r21 = call float @frem_f32(float 0x380FFFFFC0000000, float 0x36B8000000000000) ; 1.1754942e-38 rem 4e-45 = 1e-45
  %k22 = fcmp oeq float %r21, 0x36A0000000000000
  %r23 = call float @frem_f32(float 1.0, float 0x380FFFFFC0000000) ; 1.0 rem 1.1754942e-38 = 2.87e-42
  %k24 = fcmp oeq float %r23, 0x3750000000000000
  %r25 = call float @frem_f32(float 0x36CC000000000000, float 0x36B0000000000000) ; 1e-44 rem 3e-45 = 1e-45
  %k26 = fcmp oeq float %r25, 0x36A0000000000000
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

; Bit 6: the remainder feeds more arithmetic in the same method
; ((a rem b) * c + (a rem b)), and constant operands on either side.
define internal i32 @check_expr() noinline {
  %r1 = call double @frem_expr_f64(double 10.5, double 4.0, double 3.0) ; = 10.0
  %k2 = fcmp oeq double %r1, 10.0
  %r3 = call double @frem_expr_f64(double -7.0, double 3.0, double 0.5) ; = -1.5
  %k4 = fcmp oeq double %r3, -1.5
  %r5 = call double @frem_expr_f64(double 10000000000.0, double 7.0, double -1.0) ; = 0.0
  %k6 = fcmp oeq double %r5, 0.0
  %q7 = fdiv double 1.0, %r5
  %k8 = fcmp ogt double %q7, 0.0
  %r9 = call double @frem_by_10_f64(double 123.25)                 ; = 3.25
  %k10 = fcmp oeq double %r9, 3.25
  %r11 = call double @frem_by_10_f64(double -0.5)                  ; = -0.5
  %k12 = fcmp oeq double %r11, -0.5
  %r13 = call double @frem_by_10_f64(double -30.0)                 ; = -0.0
  %k14 = fcmp oeq double %r13, -0.0
  %q15 = fdiv double 1.0, %r13
  %k16 = fcmp olt double %q15, 0.0
  %r17 = call double @frem_100_by_f64(double 7.0)                  ; = 2.0
  %k18 = fcmp oeq double %r17, 2.0
  %r19 = call double @frem_100_by_f64(double -0.75)                ; = 0.25
  %k20 = fcmp oeq double %r19, 0.25
  %r21 = call double @frem_100_by_f64(double 0x3F50624DD2F1A9FC)   ; = 0.0009999999999979184
  %k22 = fcmp oeq double %r21, 0x3F50624DD2F1847C
  %r23 = call float @frem_expr_f32(float 10.5, float 4.0, float 3.0) ; = 10.0
  %k24 = fcmp oeq float %r23, 10.0
  %r25 = call float @frem_expr_f32(float -7.0, float 3.0, float 0.5) ; = -1.5
  %k26 = fcmp oeq float %r25, -1.5
  %r27 = call float @frem_expr_f32(float 10000000000.0, float 7.0, float -1.0) ; = 0.0
  %k28 = fcmp oeq float %r27, 0.0
  %q29 = fdiv float 1.0, %r27
  %k30 = fcmp ogt float %q29, 0.0
  %r31 = call float @frem_by_10_f32(float 123.25)                  ; = 3.25
  %k32 = fcmp oeq float %r31, 3.25
  %r33 = call float @frem_by_10_f32(float -0.5)                    ; = -0.5
  %k34 = fcmp oeq float %r33, -0.5
  %r35 = call float @frem_by_10_f32(float -30.0)                   ; = -0.0
  %k36 = fcmp oeq float %r35, -0.0
  %q37 = fdiv float 1.0, %r35
  %k38 = fcmp olt float %q37, 0.0
  %r39 = call float @frem_100_by_f32(float 7.0)                    ; = 2.0
  %k40 = fcmp oeq float %r39, 2.0
  %r41 = call float @frem_100_by_f32(float -0.75)                  ; = 0.25
  %k42 = fcmp oeq float %r41, 0.25
  %r43 = call float @frem_100_by_f32(float 0x3F50624DE0000000)     ; = 0.0009952503
  %k44 = fcmp oeq float %r43, 0x3F504E61E0000000
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

; Bit 7: float32 operands widened to float64, the remainder taken in
; float64 and narrowed again equal the float32 remainder.
define internal i32 @check_widened() noinline {
  %r1 = call float @frem_widened(float 5.5, float 2.0)             ; 5.5 rem 2.0 = 1.5
  %k2 = fcmp oeq float %r1, 1.5
  %r3 = call float @frem_widened(float 0x46293E5940000000, float 3.0) ; 1e+30 rem 3.0 = 0.0
  %k4 = fcmp oeq float %r3, 0.0
  %q5 = fdiv float 1.0, %r3
  %k6 = fcmp ogt float %q5, 0.0
  %r7 = call float @frem_widened(float -7.25, float 0x3FB99999A0000000) ; -7.25 rem 0.1 = -0.049999893
  %k8 = fcmp oeq float %r7, 0xBFA9999600000000
  %r9 = call float @frem_widened(float 0x47EFFFFFE0000000, float 0x3FD3333340000000) ; 3.4028235e+38 rem 0.3 = 0.13251364
  %k10 = fcmp oeq float %r9, 0x3FC0F63500000000
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %bad = xor i1 %all4, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

define i32 @main() {
  %b0 = call i32 @check_basic_f64()
  %b1 = call i32 @check_basic_f32()
  %s1 = shl i32 %b1, 1
  %b2 = call i32 @check_zero_results()
  %s2 = shl i32 %b2, 2
  %b3 = call i32 @check_nan_f64()
  %s3 = shl i32 %b3, 3
  %b4 = call i32 @check_nan_f32()
  %s4 = shl i32 %b4, 4
  %b5 = call i32 @check_inf_divisor()
  %s5 = shl i32 %b5, 5
  %b6 = call i32 @check_expr()
  %s6 = shl i32 %b6, 6
  %b7 = call i32 @check_widened()
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
