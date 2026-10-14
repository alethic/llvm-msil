; Selects of floating-point values (Milestone 5; plan section 12, row 'M5.d
; extended'): float32 and float64 selects on integer conditions, on i1
; parameters and on fcmp conditions (ordered and unordered, NaN operands
; included), minimum/maximum idioms and clamps, selects of FP constants
; (signed zeros included), and selects whose arms or result are computed.
; Every selected value is observed, with fcmp oeq against the expected value
; (NaN with fcmp uno, the sign of a zero through 1.0 / x) or with fptosi, so
; swapped arms or a lost value fail. Each noinline check function returns 1
; when a result is wrong; main ORs one bit per check into the exit status (8
; bits, D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

; Integer conditions.
define double @sel_icmp_f64(i32 %a, i32 %b, double %x, double %y) noinline {
  %c = icmp slt i32 %a, %b
  %s = select i1 %c, double %x, double %y
  ret double %s
}

define float @sel_icmp_f32(i64 %a, i64 %b, float %x, float %y) noinline {
  %c = icmp ult i64 %a, %b
  %s = select i1 %c, float %x, float %y
  ret float %s
}

define double @sel_bool_f64(i1 %c, double %x, double %y) noinline {
  %s = select i1 %c, double %x, double %y
  ret double %s
}

define float @sel_bool_f32(i1 %c, float %x, float %y) noinline {
  %s = select i1 %c, float %x, float %y
  ret float %s
}

; FP conditions, ordered and unordered.
define double @sel_olt_f64(double %a, double %b, double %x, double %y) noinline {
  %c = fcmp olt double %a, %b
  %s = select i1 %c, double %x, double %y
  ret double %s
}

define double @sel_ult_f64(double %a, double %b, double %x, double %y) noinline {
  %c = fcmp ult double %a, %b
  %s = select i1 %c, double %x, double %y
  ret double %s
}

define double @sel_one_f64(double %a, double %b, double %x, double %y) noinline {
  %c = fcmp one double %a, %b
  %s = select i1 %c, double %x, double %y
  ret double %s
}

define double @sel_ueq_f64(double %a, double %b, double %x, double %y) noinline {
  %c = fcmp ueq double %a, %b
  %s = select i1 %c, double %x, double %y
  ret double %s
}

define float @sel_oge_f32(float %a, float %b, float %x, float %y) noinline {
  %c = fcmp oge float %a, %b
  %s = select i1 %c, float %x, float %y
  ret float %s
}

define float @sel_ule_f32(float %a, float %b, float %x, float %y) noinline {
  %c = fcmp ule float %a, %b
  %s = select i1 %c, float %x, float %y
  ret float %s
}

define float @sel_uno_f32(float %a, float %b, float %x, float %y) noinline {
  %c = fcmp uno float %a, %b
  %s = select i1 %c, float %x, float %y
  ret float %s
}

define float @sel_une_f32(float %a, float %b, float %x, float %y) noinline {
  %c = fcmp une float %a, %b
  %s = select i1 %c, float %x, float %y
  ret float %s
}

; float32 values chosen by a float64 compare, and the reverse.
define float @sel_mixed_f32(double %a, double %b, float %x, float %y) noinline {
  %c = fcmp ogt double %a, %b
  %s = select i1 %c, float %x, float %y
  ret float %s
}

define double @sel_mixed_f64(float %a, float %b, double %x, double %y) noinline {
  %c = fcmp ugt float %a, %b
  %s = select i1 %c, double %x, double %y
  ret double %s
}

; Minimum and maximum idioms: (a < b) ? a : b and (a > b) ? a : b.
define double @min_olt_f64(double %a, double %b) noinline {
  %c = fcmp olt double %a, %b
  %s = select i1 %c, double %a, double %b
  ret double %s
}

define double @max_ugt_f64(double %a, double %b) noinline {
  %c = fcmp ugt double %a, %b
  %s = select i1 %c, double %a, double %b
  ret double %s
}

define float @min_ult_f32(float %a, float %b) noinline {
  %c = fcmp ult float %a, %b
  %s = select i1 %c, float %a, float %b
  ret float %s
}

define float @max_ogt_f32(float %a, float %b) noinline {
  %c = fcmp ogt float %a, %b
  %s = select i1 %c, float %a, float %b
  ret float %s
}

; Clamp: two selects in a row.
define double @clamp_f64(double %x, double %lo, double %hi) noinline {
  %c1 = fcmp olt double %x, %lo
  %s1 = select i1 %c1, double %lo, double %x
  %c2 = fcmp ogt double %s1, %hi
  %s2 = select i1 %c2, double %hi, double %s1
  ret double %s2
}

define float @clamp_f32(float %x, float %lo, float %hi) noinline {
  %c1 = fcmp olt float %x, %lo
  %s1 = select i1 %c1, float %lo, float %x
  %c2 = fcmp ogt float %s1, %hi
  %s2 = select i1 %c2, float %hi, float %s1
  ret float %s2
}

; Selects of constants (no constant pool: FP constants are immediates).
define double @sel_consts_f64(double %a, double %b) noinline {
  %c = fcmp olt double %a, %b
  %s = select i1 %c, double 1.5, double -2.25
  ret double %s
}

define float @sel_consts_f32(i32 %a) noinline {
  %c = icmp eq i32 %a, 0
  %s = select i1 %c, float 0x7FF0000000000000, float 0.5
  ret float %s
}

define double @sel_zeros_f64(i32 %a) noinline {
  %c = icmp ne i32 %a, 0
  %s = select i1 %c, double -0.0, double 0.0
  ret double %s
}

define float @sel_zeros_f32(float %a, float %b) noinline {
  %c = fcmp uge float %a, %b
  %s = select i1 %c, float 0.0, float -0.0
  ret float %s
}

; The selected value observed with fptosi in the same method.
define i32 @sel_to_int_f64(i32 %a, double %x, double %y) noinline {
  %c = icmp sgt i32 %a, 0
  %s = select i1 %c, double %x, double %y
  %i = fptosi double %s to i32
  ret i32 %i
}

define i32 @sel_to_int_f32(float %a, float %b, float %x, float %y) noinline {
  %c = fcmp olt float %a, %b
  %s = select i1 %c, float %x, float %y
  %i = fptosi float %s to i32
  ret i32 %i
}

; A select whose arms are computed, and whose result is computed with.
define double @sel_arith_f64(double %a, double %b) noinline {
  %c = fcmp ole double %a, %b
  %d = fsub double %b, %a
  %e = fsub double %a, %b
  %s = select i1 %c, double %d, double %e
  %r = fmul double %s, 2.0
  ret double %r
}

; Bit 0: float64 and float32 selects on integer compares and on i1
; parameters, both outcomes.
define internal i32 @check_icmp() noinline {
  %r1 = call double @sel_icmp_f64(i32 -1, i32 1, double 1.5, double -2.25) ; -> 1.5
  %k2 = fcmp oeq double %r1, 1.5
  %r3 = call double @sel_icmp_f64(i32 1, i32 -1, double 1.5, double -2.25) ; -> -2.25
  %k4 = fcmp oeq double %r3, -2.25
  %r5 = call double @sel_icmp_f64(i32 5, i32 5, double 1.5, double -2.25) ; -> -2.25
  %k6 = fcmp oeq double %r5, -2.25
  %r7 = call float @sel_icmp_f32(i64 1, i64 -9223372036854775808, float 0.75, float 10000000000.0) ; -> 0.75
  %k8 = fcmp oeq float %r7, 0.75
  %r9 = call float @sel_icmp_f32(i64 -9223372036854775808, i64 1, float 0.75, float 10000000000.0) ; -> 10000000000.0
  %k10 = fcmp oeq float %r9, 10000000000.0
  %r11 = call double @sel_bool_f64(i1 true, double 0x7E37E43C8800759C, double 0x81A56E1FC2F8F359) ; -> 1e+300
  %k12 = fcmp oeq double %r11, 0x7E37E43C8800759C
  %r13 = call float @sel_bool_f32(i1 true, float -3.5, float 7.0)  ; -> -3.5
  %k14 = fcmp oeq float %r13, -3.5
  %r15 = call double @sel_bool_f64(i1 false, double 0x7E37E43C8800759C, double 0x81A56E1FC2F8F359) ; -> -1e-300
  %k16 = fcmp oeq double %r15, 0x81A56E1FC2F8F359
  %r17 = call float @sel_bool_f32(i1 false, float -3.5, float 7.0) ; -> 7.0
  %k18 = fcmp oeq float %r17, 7.0
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %all6 = and i1 %all5, %k14
  %all7 = and i1 %all6, %k16
  %all8 = and i1 %all7, %k18
  %bad = xor i1 %all8, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 1: float64 selects on fcmp olt, ult, one and ueq, with ordered
; operands and with NaN on either side.
define internal i32 @check_fcmp_f64() noinline {
  %r1 = call double @sel_olt_f64(double 1.0, double 2.0, double 10.0, double 20.0) ; -> 10.0
  %k2 = fcmp oeq double %r1, 10.0
  %r3 = call double @sel_olt_f64(double 2.0, double 1.0, double 10.0, double 20.0) ; -> 20.0
  %k4 = fcmp oeq double %r3, 20.0
  %r5 = call double @sel_olt_f64(double 3.0, double 3.0, double 10.0, double 20.0) ; -> 20.0
  %k6 = fcmp oeq double %r5, 20.0
  %r7 = call double @sel_olt_f64(double 0x7FF8000000000000, double 1.0, double 10.0, double 20.0) ; -> 20.0
  %k8 = fcmp oeq double %r7, 20.0
  %r9 = call double @sel_olt_f64(double 1.0, double 0x7FF8000000000000, double 10.0, double 20.0) ; -> 20.0
  %k10 = fcmp oeq double %r9, 20.0
  %r11 = call double @sel_ult_f64(double 1.0, double 2.0, double 10.0, double 20.0) ; -> 10.0
  %k12 = fcmp oeq double %r11, 10.0
  %r13 = call double @sel_ult_f64(double 2.0, double 1.0, double 10.0, double 20.0) ; -> 20.0
  %k14 = fcmp oeq double %r13, 20.0
  %r15 = call double @sel_ult_f64(double 3.0, double 3.0, double 10.0, double 20.0) ; -> 20.0
  %k16 = fcmp oeq double %r15, 20.0
  %r17 = call double @sel_ult_f64(double 0x7FF8000000000000, double 1.0, double 10.0, double 20.0) ; -> 10.0
  %k18 = fcmp oeq double %r17, 10.0
  %r19 = call double @sel_ult_f64(double 1.0, double 0x7FF8000000000000, double 10.0, double 20.0) ; -> 10.0
  %k20 = fcmp oeq double %r19, 10.0
  %r21 = call double @sel_one_f64(double 1.0, double 2.0, double 10.0, double 20.0) ; -> 10.0
  %k22 = fcmp oeq double %r21, 10.0
  %r23 = call double @sel_one_f64(double 2.0, double 1.0, double 10.0, double 20.0) ; -> 10.0
  %k24 = fcmp oeq double %r23, 10.0
  %r25 = call double @sel_one_f64(double 3.0, double 3.0, double 10.0, double 20.0) ; -> 20.0
  %k26 = fcmp oeq double %r25, 20.0
  %r27 = call double @sel_one_f64(double 0x7FF8000000000000, double 1.0, double 10.0, double 20.0) ; -> 20.0
  %k28 = fcmp oeq double %r27, 20.0
  %r29 = call double @sel_one_f64(double 1.0, double 0x7FF8000000000000, double 10.0, double 20.0) ; -> 20.0
  %k30 = fcmp oeq double %r29, 20.0
  %r31 = call double @sel_ueq_f64(double 1.0, double 2.0, double 10.0, double 20.0) ; -> 20.0
  %k32 = fcmp oeq double %r31, 20.0
  %r33 = call double @sel_ueq_f64(double 2.0, double 1.0, double 10.0, double 20.0) ; -> 20.0
  %k34 = fcmp oeq double %r33, 20.0
  %r35 = call double @sel_ueq_f64(double 3.0, double 3.0, double 10.0, double 20.0) ; -> 10.0
  %k36 = fcmp oeq double %r35, 10.0
  %r37 = call double @sel_ueq_f64(double 0x7FF8000000000000, double 1.0, double 10.0, double 20.0) ; -> 10.0
  %k38 = fcmp oeq double %r37, 10.0
  %r39 = call double @sel_ueq_f64(double 1.0, double 0x7FF8000000000000, double 10.0, double 20.0) ; -> 10.0
  %k40 = fcmp oeq double %r39, 10.0
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
  %bad = xor i1 %all19, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 2: float32 selects on fcmp oge, ule, uno and une, with ordered
; operands and with NaN on either side.
define internal i32 @check_fcmp_f32() noinline {
  %r1 = call float @sel_oge_f32(float 1.0, float 2.0, float 0.25, float -8.0) ; -> -8.0
  %k2 = fcmp oeq float %r1, -8.0
  %r3 = call float @sel_oge_f32(float 2.0, float 1.0, float 0.25, float -8.0) ; -> 0.25
  %k4 = fcmp oeq float %r3, 0.25
  %r5 = call float @sel_oge_f32(float -0.0, float 0.0, float 0.25, float -8.0) ; -> 0.25
  %k6 = fcmp oeq float %r5, 0.25
  %r7 = call float @sel_oge_f32(float 0x7FF8000000000000, float 1.0, float 0.25, float -8.0) ; -> -8.0
  %k8 = fcmp oeq float %r7, -8.0
  %r9 = call float @sel_oge_f32(float 1.0, float 0x7FF8000000000000, float 0.25, float -8.0) ; -> -8.0
  %k10 = fcmp oeq float %r9, -8.0
  %r11 = call float @sel_ule_f32(float 1.0, float 2.0, float 0.25, float -8.0) ; -> 0.25
  %k12 = fcmp oeq float %r11, 0.25
  %r13 = call float @sel_ule_f32(float 2.0, float 1.0, float 0.25, float -8.0) ; -> -8.0
  %k14 = fcmp oeq float %r13, -8.0
  %r15 = call float @sel_ule_f32(float -0.0, float 0.0, float 0.25, float -8.0) ; -> 0.25
  %k16 = fcmp oeq float %r15, 0.25
  %r17 = call float @sel_ule_f32(float 0x7FF8000000000000, float 1.0, float 0.25, float -8.0) ; -> 0.25
  %k18 = fcmp oeq float %r17, 0.25
  %r19 = call float @sel_ule_f32(float 1.0, float 0x7FF8000000000000, float 0.25, float -8.0) ; -> 0.25
  %k20 = fcmp oeq float %r19, 0.25
  %r21 = call float @sel_uno_f32(float 1.0, float 2.0, float 0.25, float -8.0) ; -> -8.0
  %k22 = fcmp oeq float %r21, -8.0
  %r23 = call float @sel_uno_f32(float 2.0, float 1.0, float 0.25, float -8.0) ; -> -8.0
  %k24 = fcmp oeq float %r23, -8.0
  %r25 = call float @sel_uno_f32(float -0.0, float 0.0, float 0.25, float -8.0) ; -> -8.0
  %k26 = fcmp oeq float %r25, -8.0
  %r27 = call float @sel_uno_f32(float 0x7FF8000000000000, float 1.0, float 0.25, float -8.0) ; -> 0.25
  %k28 = fcmp oeq float %r27, 0.25
  %r29 = call float @sel_uno_f32(float 1.0, float 0x7FF8000000000000, float 0.25, float -8.0) ; -> 0.25
  %k30 = fcmp oeq float %r29, 0.25
  %r31 = call float @sel_une_f32(float 1.0, float 2.0, float 0.25, float -8.0) ; -> 0.25
  %k32 = fcmp oeq float %r31, 0.25
  %r33 = call float @sel_une_f32(float 2.0, float 1.0, float 0.25, float -8.0) ; -> 0.25
  %k34 = fcmp oeq float %r33, 0.25
  %r35 = call float @sel_une_f32(float -0.0, float 0.0, float 0.25, float -8.0) ; -> -8.0
  %k36 = fcmp oeq float %r35, -8.0
  %r37 = call float @sel_une_f32(float 0x7FF8000000000000, float 1.0, float 0.25, float -8.0) ; -> 0.25
  %k38 = fcmp oeq float %r37, 0.25
  %r39 = call float @sel_une_f32(float 1.0, float 0x7FF8000000000000, float 0.25, float -8.0) ; -> 0.25
  %k40 = fcmp oeq float %r39, 0.25
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
  %bad = xor i1 %all19, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 3: float32 values chosen by a float64 compare and float64 values
; chosen by a float32 compare (NaN included).
define internal i32 @check_mixed() noinline {
  %r1 = call float @sel_mixed_f32(double 2.0, double 1.0, float 1.25, float -1.25) ; -> 1.25
  %k2 = fcmp oeq float %r1, 1.25
  %r3 = call float @sel_mixed_f32(double 1.0, double 2.0, float 1.25, float -1.25) ; -> -1.25
  %k4 = fcmp oeq float %r3, -1.25
  %r5 = call float @sel_mixed_f32(double 0x7FF8000000000000, double 1.0, float 1.25, float -1.25) ; -> -1.25
  %k6 = fcmp oeq float %r5, -1.25
  %r7 = call double @sel_mixed_f64(float 2.0, float 1.0, double 0x3FB999999999999A, double 0x3FC999999999999A) ; -> 0.1
  %k8 = fcmp oeq double %r7, 0x3FB999999999999A
  %r9 = call double @sel_mixed_f64(float 1.0, float 2.0, double 0x3FB999999999999A, double 0x3FC999999999999A) ; -> 0.2
  %k10 = fcmp oeq double %r9, 0x3FC999999999999A
  %r11 = call double @sel_mixed_f64(float 0x7FF8000000000000, float 1.0, double 0x3FB999999999999A, double 0x3FC999999999999A) ; -> 0.1
  %k12 = fcmp oeq double %r11, 0x3FB999999999999A
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %bad = xor i1 %all5, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 4: minimum and maximum idioms and clamps, ordered and unordered:
; (a < b) ? a : b with a NaN gives b, (a > b, unordered) ? a : b with a
; NaN gives NaN; signed zeros pass through unchanged.
define internal i32 @check_minmax() noinline {
  %r1 = call double @min_olt_f64(double 1.0, double 2.0)           ; -> 1.0
  %k2 = fcmp oeq double %r1, 1.0
  %r3 = call double @max_ugt_f64(double 1.0, double 2.0)           ; -> 2.0
  %k4 = fcmp oeq double %r3, 2.0
  %r5 = call double @min_olt_f64(double 2.0, double 1.0)           ; -> 1.0
  %k6 = fcmp oeq double %r5, 1.0
  %r7 = call double @max_ugt_f64(double 2.0, double 1.0)           ; -> 2.0
  %k8 = fcmp oeq double %r7, 2.0
  %r9 = call double @min_olt_f64(double 0x7FF8000000000000, double 1.0) ; -> 1.0
  %k10 = fcmp oeq double %r9, 1.0
  %r11 = call double @max_ugt_f64(double 0x7FF8000000000000, double 1.0) ; -> NaN
  %k12 = fcmp uno double %r11, 0.0
  %r13 = call double @min_olt_f64(double 1.0, double 0x7FF8000000000000) ; -> NaN
  %k14 = fcmp uno double %r13, 0.0
  %r15 = call double @max_ugt_f64(double 1.0, double 0x7FF8000000000000) ; -> 1.0
  %k16 = fcmp oeq double %r15, 1.0
  %r17 = call double @min_olt_f64(double -0.0, double 0.0)         ; -> 0.0
  %k18 = fcmp oeq double %r17, 0.0
  %q19 = fdiv double 1.0, %r17
  %k20 = fcmp ogt double %q19, 0.0
  %r21 = call double @max_ugt_f64(double -0.0, double 0.0)         ; -> 0.0
  %k22 = fcmp oeq double %r21, 0.0
  %q23 = fdiv double 1.0, %r21
  %k24 = fcmp ogt double %q23, 0.0
  %r25 = call double @min_olt_f64(double 0.0, double -0.0)         ; -> -0.0
  %k26 = fcmp oeq double %r25, -0.0
  %q27 = fdiv double 1.0, %r25
  %k28 = fcmp olt double %q27, 0.0
  %r29 = call double @max_ugt_f64(double 0.0, double -0.0)         ; -> -0.0
  %k30 = fcmp oeq double %r29, -0.0
  %q31 = fdiv double 1.0, %r29
  %k32 = fcmp olt double %q31, 0.0
  %r33 = call double @min_olt_f64(double 0xFFF0000000000000, double 5.0) ; -> -inf
  %k34 = fcmp oeq double %r33, 0xFFF0000000000000
  %r35 = call double @max_ugt_f64(double 0xFFF0000000000000, double 5.0) ; -> 5.0
  %k36 = fcmp oeq double %r35, 5.0
  %r37 = call double @clamp_f64(double 5.0, double 0.0, double 10.0) ; -> 5.0
  %k38 = fcmp oeq double %r37, 5.0
  %r39 = call double @clamp_f64(double -5.0, double 0.0, double 10.0) ; -> 0.0
  %k40 = fcmp oeq double %r39, 0.0
  %q41 = fdiv double 1.0, %r39
  %k42 = fcmp ogt double %q41, 0.0
  %r43 = call double @clamp_f64(double 15.0, double 0.0, double 10.0) ; -> 10.0
  %k44 = fcmp oeq double %r43, 10.0
  %r45 = call double @clamp_f64(double 0x7FF8000000000000, double 0.0, double 10.0) ; -> NaN
  %k46 = fcmp uno double %r45, 0.0
  %r47 = call float @min_ult_f32(float 1.0, float 2.0)             ; -> 1.0
  %k48 = fcmp oeq float %r47, 1.0
  %r49 = call float @max_ogt_f32(float 1.0, float 2.0)             ; -> 2.0
  %k50 = fcmp oeq float %r49, 2.0
  %r51 = call float @min_ult_f32(float 2.0, float 1.0)             ; -> 1.0
  %k52 = fcmp oeq float %r51, 1.0
  %r53 = call float @max_ogt_f32(float 2.0, float 1.0)             ; -> 2.0
  %k54 = fcmp oeq float %r53, 2.0
  %r55 = call float @min_ult_f32(float 0x7FF8000000000000, float 1.0) ; -> NaN
  %k56 = fcmp uno float %r55, 0.0
  %r57 = call float @max_ogt_f32(float 0x7FF8000000000000, float 1.0) ; -> 1.0
  %k58 = fcmp oeq float %r57, 1.0
  %r59 = call float @min_ult_f32(float 1.0, float 0x7FF8000000000000) ; -> 1.0
  %k60 = fcmp oeq float %r59, 1.0
  %r61 = call float @max_ogt_f32(float 1.0, float 0x7FF8000000000000) ; -> NaN
  %k62 = fcmp uno float %r61, 0.0
  %r63 = call float @min_ult_f32(float -0.0, float 0.0)            ; -> 0.0
  %k64 = fcmp oeq float %r63, 0.0
  %q65 = fdiv float 1.0, %r63
  %k66 = fcmp ogt float %q65, 0.0
  %r67 = call float @max_ogt_f32(float -0.0, float 0.0)            ; -> 0.0
  %k68 = fcmp oeq float %r67, 0.0
  %q69 = fdiv float 1.0, %r67
  %k70 = fcmp ogt float %q69, 0.0
  %r71 = call float @min_ult_f32(float 0.0, float -0.0)            ; -> -0.0
  %k72 = fcmp oeq float %r71, -0.0
  %q73 = fdiv float 1.0, %r71
  %k74 = fcmp olt float %q73, 0.0
  %r75 = call float @max_ogt_f32(float 0.0, float -0.0)            ; -> -0.0
  %k76 = fcmp oeq float %r75, -0.0
  %q77 = fdiv float 1.0, %r75
  %k78 = fcmp olt float %q77, 0.0
  %r79 = call float @min_ult_f32(float 0xFFF0000000000000, float 5.0) ; -> -inf
  %k80 = fcmp oeq float %r79, 0xFFF0000000000000
  %r81 = call float @max_ogt_f32(float 0xFFF0000000000000, float 5.0) ; -> 5.0
  %k82 = fcmp oeq float %r81, 5.0
  %r83 = call float @clamp_f32(float 5.0, float 0.0, float 10.0)   ; -> 5.0
  %k84 = fcmp oeq float %r83, 5.0
  %r85 = call float @clamp_f32(float -5.0, float 0.0, float 10.0)  ; -> 0.0
  %k86 = fcmp oeq float %r85, 0.0
  %q87 = fdiv float 1.0, %r85
  %k88 = fcmp ogt float %q87, 0.0
  %r89 = call float @clamp_f32(float 15.0, float 0.0, float 10.0)  ; -> 10.0
  %k90 = fcmp oeq float %r89, 10.0
  %r91 = call float @clamp_f32(float 0x7FF8000000000000, float 0.0, float 10.0) ; -> NaN
  %k92 = fcmp uno float %r91, 0.0
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
  %bad = xor i1 %all45, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 5: selects between FP constants, signed zeros included (a swapped
; -0.0 / 0.0 select is visible through 1.0 / x).
define internal i32 @check_consts() noinline {
  %r1 = call double @sel_consts_f64(double 1.0, double 2.0)        ; -> 1.5
  %k2 = fcmp oeq double %r1, 1.5
  %r3 = call double @sel_consts_f64(double 2.0, double 1.0)        ; -> -2.25
  %k4 = fcmp oeq double %r3, -2.25
  %r5 = call double @sel_consts_f64(double 0x7FF8000000000000, double 2.0) ; -> -2.25
  %k6 = fcmp oeq double %r5, -2.25
  %r7 = call float @sel_consts_f32(i32 0)                          ; -> inf
  %k8 = fcmp oeq float %r7, 0x7FF0000000000000
  %r9 = call double @sel_zeros_f64(i32 0)                          ; -> 0.0
  %k10 = fcmp oeq double %r9, 0.0
  %q11 = fdiv double 1.0, %r9
  %k12 = fcmp ogt double %q11, 0.0
  %r13 = call float @sel_consts_f32(i32 7)                         ; -> 0.5
  %k14 = fcmp oeq float %r13, 0.5
  %r15 = call double @sel_zeros_f64(i32 7)                         ; -> -0.0
  %k16 = fcmp oeq double %r15, -0.0
  %q17 = fdiv double 1.0, %r15
  %k18 = fcmp olt double %q17, 0.0
  %r19 = call float @sel_zeros_f32(float 1.0, float 2.0)           ; -> -0.0
  %k20 = fcmp oeq float %r19, -0.0
  %q21 = fdiv float 1.0, %r19
  %k22 = fcmp olt float %q21, 0.0
  %r23 = call float @sel_zeros_f32(float 2.0, float 1.0)           ; -> 0.0
  %k24 = fcmp oeq float %r23, 0.0
  %q25 = fdiv float 1.0, %r23
  %k26 = fcmp ogt float %q25, 0.0
  %r27 = call float @sel_zeros_f32(float 0x7FF8000000000000, float 1.0) ; -> 0.0
  %k28 = fcmp oeq float %r27, 0.0
  %q29 = fdiv float 1.0, %r27
  %k30 = fcmp ogt float %q29, 0.0
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
  %bad = xor i1 %all14, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 6: selected values observed with fptosi in the same method.
define internal i32 @check_to_int() noinline {
  %r1 = call i32 @sel_to_int_f64(i32 1, double 0x401F99999999999A, double 0xC00999999999999A) ; 7
  %k2 = icmp eq i32 %r1, 7
  %r3 = call i32 @sel_to_int_f64(i32 0, double 0x401F99999999999A, double 0xC00999999999999A) ; -3
  %k4 = icmp eq i32 %r3, -3
  %r5 = call i32 @sel_to_int_f64(i32 -3, double 0x401F99999999999A, double 0xC00999999999999A) ; -3
  %k6 = icmp eq i32 %r5, -3
  %r7 = call i32 @sel_to_int_f32(float 1.0, float 2.0, float 100.5, float -42.75) ; 100
  %k8 = icmp eq i32 %r7, 100
  %r9 = call i32 @sel_to_int_f32(float 2.0, float 1.0, float 100.5, float -42.75) ; -42
  %k10 = icmp eq i32 %r9, -42
  %r11 = call i32 @sel_to_int_f32(float 0x7FF8000000000000, float 2.0, float 100.5, float -42.75) ; -42
  %k12 = icmp eq i32 %r11, -42
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %bad = xor i1 %all5, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 7: computed arms and a computed result: ((a <= b) ? b - a : a - b) * 2.
define internal i32 @check_arith() noinline {
  %r1 = call double @sel_arith_f64(double 1.0, double 4.5)         ; -> 7.0
  %k2 = fcmp oeq double %r1, 7.0
  %r3 = call double @sel_arith_f64(double 4.5, double 1.0)         ; -> 7.0
  %k4 = fcmp oeq double %r3, 7.0
  %r5 = call double @sel_arith_f64(double 0x7FF8000000000000, double 1.0) ; -> NaN
  %k6 = fcmp uno double %r5, 0.0
  %r7 = call double @sel_arith_f64(double -0.0, double 0.0)        ; -> 0.0
  %k8 = fcmp oeq double %r7, 0.0
  %q9 = fdiv double 1.0, %r7
  %k10 = fcmp ogt double %q9, 0.0
  %r11 = call double @sel_arith_f64(double 0x3FB999999999999A, double 0x3FD3333333333333) ; -> 0.39999999999999997
  %k12 = fcmp oeq double %r11, 0x3FD9999999999999
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %all4 = and i1 %all3, %k10
  %all5 = and i1 %all4, %k12
  %bad = xor i1 %all5, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

define i32 @main() {
  %b0 = call i32 @check_icmp()
  %b1 = call i32 @check_fcmp_f64()
  %s1 = shl i32 %b1, 1
  %b2 = call i32 @check_fcmp_f32()
  %s2 = shl i32 %b2, 2
  %b3 = call i32 @check_mixed()
  %s3 = shl i32 %b3, 3
  %b4 = call i32 @check_minmax()
  %s4 = shl i32 %b4, 4
  %b5 = call i32 @check_consts()
  %s5 = shl i32 %b5, 5
  %b6 = call i32 @check_to_int()
  %s6 = shl i32 %b6, 6
  %b7 = call i32 @check_arith()
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
