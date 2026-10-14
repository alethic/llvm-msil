; float32 precision (Milestone 5): float arithmetic is rounded to float32 at
; every step, also inside one method where the values stay on the CIL
; evaluation stack (ECMA-335 allows a wider internal representation; LLVM's
; float operations do not, and the x64 JITs keep single precision, isel.md
; 3.2). Sums, products and quotients whose float64 or wider value differs from
; the float32 one, compared inside the method and after it returns; fmul and
; fadd are never fused; float32 overflow and underflow of intermediates. Each
; noinline check function returns 1 when a result is wrong; main ORs one bit
; per check into the exit status (8 bits, D37): [exit 0] means every check
; passed.
; RUN: %{msil_ll}

; a + b + c, rounded to float32 after each addition.
define float @sum3_f32(float %a, float %b, float %c) noinline {
  %s = fadd float %a, %b
  %t = fadd float %s, %c
  ret float %t
}

; The same sum compared with e inside the method, before it is returned.
define i32 @sum3_eq_f32(float %a, float %b, float %c, float %e) noinline {
  %s = fadd float %a, %b
  %t = fadd float %s, %c
  %k = fcmp oeq float %t, %e
  %r = zext i1 %k to i32
  ret i32 %r
}

; (((big + one) + one) + one) - big
define float @add3_sub_f32(float %big, float %one) noinline {
  %s1 = fadd float %big, %one
  %s2 = fadd float %s1, %one
  %s3 = fadd float %s2, %one
  %r = fsub float %s3, %big
  ret float %r
}

; a + b == c, compared inside the method.
define i32 @sum_eq_f32(float %a, float %b, float %c) noinline {
  %s = fadd float %a, %b
  %k = fcmp oeq float %s, %c
  %r = zext i1 %k to i32
  ret i32 %r
}

; fpext (a + b): the float32 sum, widened exactly.
define double @sum_widened_f32(float %a, float %b) noinline {
  %s = fadd float %a, %b
  %r = fpext float %s to double
  ret double %r
}

; a * b - c, two roundings (fmul and fsub are never fused).
define float @mul_sub_f32(float %a, float %b, float %c) noinline {
  %m = fmul float %a, %b
  %r = fsub float %m, %c
  ret float %r
}

define double @mul_sub_f64(double %a, double %b, double %c) noinline {
  %m = fmul double %a, %b
  %r = fsub double %m, %c
  ret double %r
}

; a * b + c, two roundings.
define float @mul_add_f32(float %a, float %b, float %c) noinline {
  %m = fmul float %a, %b
  %r = fadd float %m, %c
  ret float %r
}

define double @mul_add_f64(double %a, double %b, double %c) noinline {
  %m = fmul double %a, %b
  %r = fadd double %m, %c
  ret double %r
}

; (a / b) * b == a, compared inside the method.
define i32 @div_mul_eq_f32(float %a, float %b) noinline {
  %q = fdiv float %a, %b
  %m = fmul float %q, %b
  %k = fcmp oeq float %m, %a
  %r = zext i1 %k to i32
  ret i32 %r
}

define float @div_mul_f32(float %a, float %b) noinline {
  %q = fdiv float %a, %b
  %m = fmul float %q, %b
  ret float %m
}

; (a * b) / b: the product overflows or underflows float32 first.
define float @mul_div_f32(float %a, float %b) noinline {
  %m = fmul float %a, %b
  %r = fdiv float %m, %b
  ret float %r
}

define double @mul_div_f64(double %a, double %b) noinline {
  %m = fmul double %a, %b
  %r = fdiv double %m, %b
  ret double %r
}

; (a * b) * c, compared with zero inside the method.
define i32 @mul_mul_is_zero_f32(float %a, float %b, float %c) noinline {
  %m = fmul float %a, %b
  %n = fmul float %m, %c
  %k = fcmp oeq float %n, 0.0
  %r = zext i1 %k to i32
  ret i32 %r
}

; x added to itself ten times, one rounding per addition.
define float @sum10_f32(float %x) noinline {
  %s1 = fadd float %x, %x
  %s2 = fadd float %s1, %x
  %s3 = fadd float %s2, %x
  %s4 = fadd float %s3, %x
  %s5 = fadd float %s4, %x
  %s6 = fadd float %s5, %x
  %s7 = fadd float %s6, %x
  %s8 = fadd float %s7, %x
  %s9 = fadd float %s8, %x
  ret float %s9
}

define i32 @sum10_eq_f32(float %x, float %e) noinline {
  %s1 = fadd float %x, %x
  %s2 = fadd float %s1, %x
  %s3 = fadd float %s2, %x
  %s4 = fadd float %s3, %x
  %s5 = fadd float %s4, %x
  %s6 = fadd float %s5, %x
  %s7 = fadd float %s6, %x
  %s8 = fadd float %s7, %x
  %s9 = fadd float %s8, %x
  %k = fcmp oeq float %s9, %e
  %r = zext i1 %k to i32
  ret i32 %r
}

; Bit 0: 16777216 + 1 + 1 stays 16777216 in float32 (16777218 if the
; intermediate kept more precision); 1e8 + 1 + 1 + 1 - 1e8 is 0.
define internal i32 @check_sum() noinline {
  %r1 = call float @sum3_f32(float 16777216.0, float 1.0, float 1.0) ; = 16777216.0
  %k2 = fcmp oeq float %r1, 16777216.0
  %r3 = call i32 @sum3_eq_f32(float 16777216.0, float 1.0, float 1.0, float 16777216.0) ; 1
  %k4 = icmp eq i32 %r3, 1
  %r5 = call i32 @sum3_eq_f32(float 16777216.0, float 1.0, float 1.0, float 16777218.0) ; 0
  %k6 = icmp eq i32 %r5, 0
  %r7 = call float @add3_sub_f32(float 100000000.0, float 1.0)     ; = 0.0
  %k8 = fcmp oeq float %r7, 0.0
  %q9 = fdiv float 1.0, %r7
  %k10 = fcmp ogt float %q9, 0.0
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %bad = xor i1 %all4, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 1: 0.1f + 0.2f == 0.3f holds in float32 (both are 0x3E99999A),
; and the widened sum is that float32 value, not 0.1f + 0.2f in float64.
define internal i32 @check_tenths() noinline {
  %r1 = call i32 @sum_eq_f32(float 0x3FB99999A0000000, float 0x3FC99999A0000000, float 0x3FD3333340000000) ; 1
  %k2 = icmp eq i32 %r1, 1
  %r3 = call double @sum_widened_f32(float 0x3FB99999A0000000, float 0x3FC99999A0000000) ; = 0.3
  %k4 = fcmp oeq double %r3, 0x3FD3333340000000
  %r5 = call i32 @sum_eq_f32(float 0x3FD3333340000000, float 0x3FE6666660000000, float 1.0) ; 1
  %k6 = icmp eq i32 %r5, 1
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %bad = xor i1 %all2, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 2: float32 products round to float32: 4097 * 4097 - 16785408 is 0
; (16785409 is not a float32), and 3 * (1/3) rounds to 1.
define internal i32 @check_products() noinline {
  %r1 = call float @mul_sub_f32(float 4097.0, float 4097.0, float 16785408.0) ; = 0.0
  %k2 = fcmp oeq float %r1, 0.0
  %q3 = fdiv float 1.0, %r1
  %k4 = fcmp ogt float %q3, 0.0
  %r5 = call float @mul_sub_f32(float 0x3FD5555560000000, float 3.0, float 1.0) ; = 0.0
  %k6 = fcmp oeq float %r5, 0.0
  %q7 = fdiv float 1.0, %r5
  %k8 = fcmp ogt float %q7, 0.0
  %r9 = call float @mul_add_f32(float 10000000000.0, float 10000000000.0, float 0xC415AF1D80000000) ; = 0.0
  %k10 = fcmp oeq float %r9, 0.0
  %q11 = fdiv float 1.0, %r9
  %k12 = fcmp ogt float %q11, 0.0
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %bad = xor i1 %all5, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 3: a * b + c is two roundings, never one fused operation: with
; a = b = 1 + 2^-12 (float32) or 1 + 2^-27 (float64) and c = -(1 + 2^-11)
; or -(1 + 2^-26), the product's low bit is rounded away and the sum is
; 0 (a fused multiply-add would give 2^-24 or 2^-54).
define internal i32 @check_no_fma() noinline {
  %r1 = call float @mul_sub_f32(float 1.000244140625, float 1.000244140625, float 1.00048828125) ; = 0.0
  %k2 = fcmp oeq float %r1, 0.0
  %q3 = fdiv float 1.0, %r1
  %k4 = fcmp ogt float %q3, 0.0
  %r5 = call float @mul_add_f32(float 1.000244140625, float 1.000244140625, float -1.00048828125) ; = 0.0
  %k6 = fcmp oeq float %r5, 0.0
  %q7 = fdiv float 1.0, %r5
  %k8 = fcmp ogt float %q7, 0.0
  %r9 = call double @mul_sub_f64(double 0x3FF0000002000000, double 0x3FF0000002000000, double 0x3FF0000004000000) ; = 0.0
  %k10 = fcmp oeq double %r9, 0.0
  %q11 = fdiv double 1.0, %r9
  %k12 = fcmp ogt double %q11, 0.0
  %r13 = call double @mul_add_f64(double 0x3FF0000002000000, double 0x3FF0000002000000, double 0xBFF0000004000000) ; = 0.0
  %k14 = fcmp oeq double %r13, 0.0
  %q15 = fdiv double 1.0, %r13
  %k16 = fcmp ogt double %q15, 0.0
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

; Bit 4: (a / b) * b compared with a inside the method: in float32
; (1/49) * 49 and (7/25) * 25 are exact and (1/41) * 41 is not, the other
; way round in float64; (1/3) * 3 is 1 only if the product is rounded to
; float32 as well as the quotient.
define internal i32 @check_div() noinline {
  %r1 = call i32 @div_mul_eq_f32(float 1.0, float 49.0)            ; 1
  %k2 = icmp eq i32 %r1, 1
  %r3 = call float @div_mul_f32(float 1.0, float 49.0)             ; = 1.0
  %k4 = fcmp oeq float %r3, 1.0
  %r5 = call i32 @div_mul_eq_f32(float 7.0, float 25.0)            ; 1
  %k6 = icmp eq i32 %r5, 1
  %r7 = call float @div_mul_f32(float 7.0, float 25.0)             ; = 7.0
  %k8 = fcmp oeq float %r7, 7.0
  %r9 = call i32 @div_mul_eq_f32(float 1.0, float 41.0)            ; 0
  %k10 = icmp eq i32 %r9, 0
  %r11 = call float @div_mul_f32(float 1.0, float 41.0)            ; = 0.99999994
  %k12 = fcmp oeq float %r11, 0x3FEFFFFFE0000000
  %r13 = call i32 @div_mul_eq_f32(float 1.0, float 3.0)            ; 1
  %k14 = icmp eq i32 %r13, 1
  %r15 = call float @div_mul_f32(float 1.0, float 3.0)             ; = 1.0
  %k16 = fcmp oeq float %r15, 1.0
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

; Bit 5: an intermediate product that overflows float32 is inf, so
; (1e30 * 1e10) / 1e10 is inf in float32 (1e30 with a wider
; intermediate); float64 overflows at 1e308.
define internal i32 @check_overflow() noinline {
  %r1 = call float @mul_div_f32(float 0x46293E5940000000, float 10000000000.0) ; = inf
  %k2 = fcmp oeq float %r1, 0x7FF0000000000000
  %r3 = call float @mul_div_f32(float 0xC7EC363CC0000000, float 2.0) ; = -inf
  %k4 = fcmp oeq float %r3, 0xFFF0000000000000
  %r5 = call float @mul_div_f32(float 0x4415AF1D80000000, float 0x4415AF1D80000000) ; = inf
  %k6 = fcmp oeq float %r5, 0x7FF0000000000000
  %r7 = call double @mul_div_f64(double 0x7E37E43C8800759C, double 10000000000.0) ; = inf
  %k8 = fcmp oeq double %r7, 0x7FF0000000000000
  %r9 = call double @mul_div_f64(double 0x46293E5939A08CEA, double 10000000000.0) ; = 1e+30
  %k10 = fcmp oeq double %r9, 0x46293E5939A08CEA
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %bad = xor i1 %all4, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 6: an intermediate product below the float32 subnormal range is
; 0: (1e-30 * 1e-20) * 1e20 == 0 in float32 (1e-30 with a wider
; intermediate); (1e-30 * 1e-10) / 1e-10 is 9.99994643e-31, because the
; subnormal 1e-40 keeps fewer digits; (1e-40 * 1e-6) / 1e-6 is 0.
define internal i32 @check_underflow() noinline {
  %r1 = call i32 @mul_mul_is_zero_f32(float 0x39B4484C00000000, float 0x3BC79CA100000000, float 0x4415AF1D80000000) ; 1
  %k2 = icmp eq i32 %r1, 1
  %r3 = call i32 @mul_mul_is_zero_f32(float 0x39B4484C00000000, float 0x3DDB7CDFE0000000, float 10000000000.0) ; 0
  %k4 = icmp eq i32 %r3, 0
  %r5 = call i32 @mul_mul_is_zero_f32(float 0x380B38FB80000000, float 0.5, float 2.0) ; 0
  %k6 = icmp eq i32 %r5, 0
  %r7 = call float @mul_div_f32(float 0x39B4484C00000000, float 0x3DDB7CDFE0000000) ; = 9.999946e-31
  %k8 = fcmp oeq float %r7, 0x39B44844E0000000
  %r9 = call float @mul_div_f32(float 0x37A16C2000000000, float 0x3EB0C6F7A0000000) ; = 0.0
  %k10 = fcmp oeq float %r9, 0.0
  %q11 = fdiv float 1.0, %r9
  %k12 = fcmp ogt float %q11, 0.0
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %bad = xor i1 %all5, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 7: x added to itself ten times, rounded to float32 at each step:
; 0.1f gives 1.0000001192092896 (not the float64 sum), 0.7f and 1e-8f
; their own float32 sums.
define internal i32 @check_sum10() noinline {
  %r1 = call float @sum10_f32(float 0x3FB99999A0000000)            ; = 1.0000001
  %k2 = fcmp oeq float %r1, 0x3FF0000020000000
  %r3 = call i32 @sum10_eq_f32(float 0x3FB99999A0000000, float 0x3FF0000020000000) ; 1
  %k4 = icmp eq i32 %r3, 1
  %r5 = call float @sum10_f32(float 0x3FE6666660000000)            ; = 6.999999
  %k6 = fcmp oeq float %r5, 0x401BFFFFC0000000
  %r7 = call i32 @sum10_eq_f32(float 0x3FE6666660000000, float 0x401BFFFFC0000000) ; 1
  %k8 = icmp eq i32 %r7, 1
  %r9 = call float @sum10_f32(float 0x3E45798EE0000000)            ; = 1e-07
  %k10 = fcmp oeq float %r9, 0x3E7AD7F2A0000000
  %r11 = call i32 @sum10_eq_f32(float 0x3E45798EE0000000, float 0x3E7AD7F2A0000000) ; 1
  %k12 = icmp eq i32 %r11, 1
  %r13 = call float @sum10_f32(float 0x400A666660000000)           ; = 32.999996
  %k14 = fcmp oeq float %r13, 0x40407FFFE0000000
  %r15 = call i32 @sum10_eq_f32(float 0x400A666660000000, float 0x40407FFFE0000000) ; 1
  %k16 = icmp eq i32 %r15, 1
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
  %b0 = call i32 @check_sum()
  %b1 = call i32 @check_tenths()
  %s1 = shl i32 %b1, 1
  %b2 = call i32 @check_products()
  %s2 = shl i32 %b2, 2
  %b3 = call i32 @check_no_fma()
  %s3 = shl i32 %b3, 3
  %b4 = call i32 @check_div()
  %s4 = shl i32 %b4, 4
  %b5 = call i32 @check_overflow()
  %s5 = shl i32 %b5, 5
  %b6 = call i32 @check_underflow()
  %s6 = shl i32 %b6, 6
  %b7 = call i32 @check_sum10()
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
