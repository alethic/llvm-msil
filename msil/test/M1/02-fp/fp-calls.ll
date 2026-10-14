; Floating-point arguments and results through calls (Milestone 5): float32 and
; float64 parameters mixed with int32 and native int ones (12 arguments, in
; order), float32 values passed through unchanged (no widening in the
; signature), FP results passed on to further calls and dropped, and an exit
; status computed from FP values. Exit status 42: fptosi of a value built
; through the call chain; bit 7 is set when a check fails (8 bits, D37).
; RUN: %{msil_ll}

; Horner sum in base 16 of the twelve arguments, each converted to float64:
; with the digits 1..12 in order it is exactly 0x123456789ABC; any swapped,
; lost or truncated argument changes it.
define double @mix12(float %a, double %b, i32 %c, float %d, i64 %e, double %f,
                     float %g, i32 %h, double %i, i64 %j, float %k, double %l) noinline {
  %a1 = fpext float %a to double
  %c1 = sitofp i32 %c to double
  %d1 = fpext float %d to double
  %e1 = sitofp i64 %e to double
  %g1 = fpext float %g to double
  %h1 = sitofp i32 %h to double
  %j1 = sitofp i64 %j to double
  %k1 = fpext float %k to double
  %s1 = fmul double %a1, 16.0
  %s2 = fadd double %s1, %b
  %s3 = fmul double %s2, 16.0
  %s4 = fadd double %s3, %c1
  %s5 = fmul double %s4, 16.0
  %s6 = fadd double %s5, %d1
  %s7 = fmul double %s6, 16.0
  %s8 = fadd double %s7, %e1
  %s9 = fmul double %s8, 16.0
  %s10 = fadd double %s9, %f
  %s11 = fmul double %s10, 16.0
  %s12 = fadd double %s11, %g1
  %s13 = fmul double %s12, 16.0
  %s14 = fadd double %s13, %h1
  %s15 = fmul double %s14, 16.0
  %s16 = fadd double %s15, %i
  %s17 = fmul double %s16, 16.0
  %s18 = fadd double %s17, %j1
  %s19 = fmul double %s18, 16.0
  %s20 = fadd double %s19, %k1
  %s21 = fmul double %s20, 16.0
  %s22 = fadd double %s21, %l
  ret double %s22
}

; The same with float32 results: a + 2b + 4c + 8d in float32 arithmetic.
define float @mix4_f32(float %a, double %b, float %c, i32 %d) noinline {
  %b1 = fptrunc double %b to float
  %d1 = sitofp i32 %d to float
  %t1 = fmul float %b1, 2.0
  %t2 = fmul float %c, 4.0
  %t3 = fmul float %d1, 8.0
  %u1 = fadd float %a, %t1
  %u2 = fadd float %u1, %t2
  %u3 = fadd float %u2, %t3
  ret float %u3
}

define float @id_f32(float %x) noinline {
  ret float %x
}

define double @id_f64(double %x) noinline {
  ret double %x
}

define float @half_f32(float %x) noinline {
  %r = fmul float %x, 0.5
  ret float %r
}

define double @add_f64(double %a, double %b) noinline {
  %r = fadd double %a, %b
  ret double %r
}

; float32 -> float32 call -> float64 call: (x / 2) + y.
define double @chain(float %x, double %y) noinline {
  %h = call float @half_f32(float %x)
  %w = fpext float %h to double
  %r = call double @add_f64(double %w, double %y)
  ret double %r
}

; Results of calls passed straight to other calls, and one dropped.
define float @nested(float %x) noinline {
  %a = call float @id_f32(float %x)
  %b = call float @half_f32(float %a)
  %c = call float @half_f32(float %b)
  %unused = call double @id_f64(double 1.0)
  ret float %c
}

define internal i32 @check_args() noinline {
  %m = call double @mix12(float 1.0, double 2.0, i32 3, float 4.0, i64 5, double 6.0,
                          float 7.0, i32 8, double 9.0, i64 10, float 11.0, double 12.0)
  %k1 = fcmp oeq double %m, 20015998343868.0          ; 0x123456789ABC
  ; 0.1f + 2 * 0.25 + 4 * -1.5f + 8 * 3 = 18.6f
  %f = call float @mix4_f32(float 0x3FB99999A0000000, double 0.25, float -1.5, i32 3)
  %k2 = fcmp oeq float %f, 0x40329999A0000000
  %all = and i1 %k1, %k2
  %bad = xor i1 %all, true
  %r = zext i1 %bad to i32
  ret i32 %r
}

; float32 values come back with the same value (0.1f, the smallest
; subnormal, the largest float, -0.0, inf, NaN).
define internal i32 @check_identity() noinline {
  %a = call float @id_f32(float 0x3FB99999A0000000)
  %k1 = fcmp oeq float %a, 0x3FB99999A0000000
  %b = call float @id_f32(float 0x36A0000000000000)
  %k2 = fcmp oeq float %b, 0x36A0000000000000
  %c = call float @id_f32(float 0x47EFFFFFE0000000)
  %k3 = fcmp oeq float %c, 0x47EFFFFFE0000000
  %d = call float @id_f32(float -0.0)
  %q = fdiv float 1.0, %d
  %k4 = fcmp oeq float %q, 0xFFF0000000000000
  %e = call float @id_f32(float 0x7FF0000000000000)
  %k5 = fcmp oeq float %e, 0x7FF0000000000000
  %n = call float @id_f32(float 0x7FF8000000000000)
  %k6 = fcmp uno float %n, %n
  %x = call double @id_f64(double 0x3FB999999999999A)
  %k7 = fcmp oeq double %x, 0x3FB999999999999A
  %a1 = and i1 %k1, %k2
  %a2 = and i1 %a1, %k3
  %a3 = and i1 %a2, %k4
  %a4 = and i1 %a3, %k5
  %a5 = and i1 %a4, %k6
  %a6 = and i1 %a5, %k7
  %bad = xor i1 %a6, true
  %r = zext i1 %bad to i32
  ret i32 %r
}

define i32 @main() {
  %b0 = call i32 @check_args()
  %b1 = call i32 @check_identity()
  %bad = or i32 %b0, %b1
  %flag = shl i32 %bad, 7
  ; chain(83.5, 0.5) = 41.75 + 0.5 = 42.25; nested(170.0) = 42.5
  %c = call double @chain(float 83.5, double 0.5)
  %n = call float @nested(float 170.0)
  %w = fpext float %n to double
  %avg = fadd double %c, %w
  %h = fmul double %avg, 0.5                          ; 42.375
  %i = fptosi double %h to i32                        ; 42
  %low = and i32 %i, 127
  %r = or i32 %low, %flag
  ret i32 %r
}
