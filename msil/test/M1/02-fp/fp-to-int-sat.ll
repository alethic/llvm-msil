; Saturating conversions (Milestone 5): llvm.fptosi.sat and llvm.fptoui.sat to
; i32 and i64 from float32 and float64 (FP_TO_SINT_SAT / FP_TO_UINT_SAT are
; Expand: compares and selects around the plain conversion). NaN gives 0, a
; value above or below the range gives the type's maximum or minimum (0 for
; a negative value to an unsigned type, +-inf included), and an in-range value
; is truncated toward zero; the boundaries 2^31 - 1, 2^32 - 1, 2^63 - 1 and
; 2^64 - 1 are reached both exactly (when the source type holds them) and by
; saturation. The operands come from noinline identity functions (NaN from
; 0.0 / 0.0, plan 5.5). .NET 9 and later saturate conv.i4/conv.u4/conv.i8/
; conv.u8 natively, so a lost expansion shows only on .NET 8 and .NET
; Framework. Each noinline check function returns 1 when a result is wrong;
; main ORs one bit per check into the exit status (8 bits, D37): [exit 0]
; means every check passed.
; RUN: %{msil_ll}

; Identity functions: the operands of the conversions come from calls,
; so llc cannot fold them (the JIT may inline these and fold).
define double @id_f64(double %x) noinline {
  ret double %x
}

define float @id_f32(float %x) noinline {
  ret float %x
}

define double @fdiv_f64(double %a, double %b) noinline {
  %r = fdiv double %a, %b
  ret double %r
}

define float @fdiv_f32(float %a, float %b) noinline {
  %r = fdiv float %a, %b
  ret float %r
}

; Bit 0: llvm.fptosi.sat, float64 to i32 (signed: -2147483648 .. 2147483647).
define internal i32 @check_fptosi_sat_f64_i32() noinline {
  %r1 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %r2 = call i32 @llvm.fptosi.sat.i32.f64(double %r1)              ; NaN -> 0
  %k3 = icmp eq i32 %r2, 0
  %r4 = call double @id_f64(double 0x7FF0000000000000)
  %r5 = call i32 @llvm.fptosi.sat.i32.f64(double %r4)              ; inf -> 2147483647
  %k6 = icmp eq i32 %r5, 2147483647
  %r7 = call double @id_f64(double 0xFFF0000000000000)
  %r8 = call i32 @llvm.fptosi.sat.i32.f64(double %r7)              ; -inf -> -2147483648
  %k9 = icmp eq i32 %r8, -2147483648
  %r10 = call double @id_f64(double 1.0e+20)
  %r11 = call i32 @llvm.fptosi.sat.i32.f64(double %r10)            ; 1e+20 -> 2147483647
  %k12 = icmp eq i32 %r11, 2147483647
  %r13 = call double @id_f64(double -1.0e+20)
  %r14 = call i32 @llvm.fptosi.sat.i32.f64(double %r13)            ; -1e+20 -> -2147483648
  %k15 = icmp eq i32 %r14, -2147483648
  %r16 = call double @id_f64(double 0.0)
  %r17 = call i32 @llvm.fptosi.sat.i32.f64(double %r16)            ; 0.0 -> 0
  %k18 = icmp eq i32 %r17, 0
  %r19 = call double @id_f64(double -0.0)
  %r20 = call i32 @llvm.fptosi.sat.i32.f64(double %r19)            ; -0.0 -> 0
  %k21 = icmp eq i32 %r20, 0
  %r22 = call double @id_f64(double 0.75)
  %r23 = call i32 @llvm.fptosi.sat.i32.f64(double %r22)            ; 0.75 -> 0
  %k24 = icmp eq i32 %r23, 0
  %r25 = call double @id_f64(double -0.75)
  %r26 = call i32 @llvm.fptosi.sat.i32.f64(double %r25)            ; -0.75 -> 0
  %k27 = icmp eq i32 %r26, 0
  %r28 = call double @id_f64(double 0x405EF9999999999A)
  %r29 = call i32 @llvm.fptosi.sat.i32.f64(double %r28)            ; 123.9 -> 123
  %k30 = icmp eq i32 %r29, 123
  %r31 = call double @id_f64(double -5.5)
  %r32 = call i32 @llvm.fptosi.sat.i32.f64(double %r31)            ; -5.5 -> -5
  %k33 = icmp eq i32 %r32, -5
  %r34 = call double @id_f64(double 2147483647.0)
  %r35 = call i32 @llvm.fptosi.sat.i32.f64(double %r34)            ; 2147483647.0 -> 2147483647
  %k36 = icmp eq i32 %r35, 2147483647
  %r37 = call double @id_f64(double 2147483647.75)
  %r38 = call i32 @llvm.fptosi.sat.i32.f64(double %r37)            ; 2147483647.75 -> 2147483647
  %k39 = icmp eq i32 %r38, 2147483647
  %r40 = call double @id_f64(double 2147483648.0)
  %r41 = call i32 @llvm.fptosi.sat.i32.f64(double %r40)            ; 2147483648.0 -> 2147483647
  %k42 = icmp eq i32 %r41, 2147483647
  %r43 = call double @id_f64(double 2147483649.5)
  %r44 = call i32 @llvm.fptosi.sat.i32.f64(double %r43)            ; 2147483649.5 -> 2147483647
  %k45 = icmp eq i32 %r44, 2147483647
  %r46 = call double @id_f64(double -2147483648.0)
  %r47 = call i32 @llvm.fptosi.sat.i32.f64(double %r46)            ; -2147483648.0 -> -2147483648
  %k48 = icmp eq i32 %r47, -2147483648
  %r49 = call double @id_f64(double -2147483648.75)
  %r50 = call i32 @llvm.fptosi.sat.i32.f64(double %r49)            ; -2147483648.75 -> -2147483648
  %k51 = icmp eq i32 %r50, -2147483648
  %r52 = call double @id_f64(double -2147483649.0)
  %r53 = call i32 @llvm.fptosi.sat.i32.f64(double %r52)            ; -2147483649.0 -> -2147483648
  %k54 = icmp eq i32 %r53, -2147483648
  %r55 = call double @id_f64(double 4294967296.0)
  %r56 = call i32 @llvm.fptosi.sat.i32.f64(double %r55)            ; 4294967296.0 -> 2147483647
  %k57 = icmp eq i32 %r56, 2147483647
  %r58 = call double @id_f64(double -10000000000.0)
  %r59 = call i32 @llvm.fptosi.sat.i32.f64(double %r58)            ; -10000000000.0 -> -2147483648
  %k60 = icmp eq i32 %r59, -2147483648
  %all1 = and i1 %k3, %k6
  %all2 = and i1 %all1, %k9
  %all3 = and i1 %all2, %k12
  %all4 = and i1 %all3, %k15
  %all5 = and i1 %all4, %k18
  %all6 = and i1 %all5, %k21
  %all7 = and i1 %all6, %k24
  %all8 = and i1 %all7, %k27
  %all9 = and i1 %all8, %k30
  %all10 = and i1 %all9, %k33
  %all11 = and i1 %all10, %k36
  %all12 = and i1 %all11, %k39
  %all13 = and i1 %all12, %k42
  %all14 = and i1 %all13, %k45
  %all15 = and i1 %all14, %k48
  %all16 = and i1 %all15, %k51
  %all17 = and i1 %all16, %k54
  %all18 = and i1 %all17, %k57
  %all19 = and i1 %all18, %k60
  %bad = xor i1 %all19, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 1: llvm.fptosi.sat, float32 to i32 (signed: -2147483648 .. 2147483647).
define internal i32 @check_fptosi_sat_f32_i32() noinline {
  %r1 = call float @fdiv_f32(float 0.0, float 0.0)                 ; 0.0 / 0.0 = NaN
  %r2 = call i32 @llvm.fptosi.sat.i32.f32(float %r1)               ; NaN -> 0
  %k3 = icmp eq i32 %r2, 0
  %r4 = call float @id_f32(float 0x7FF0000000000000)
  %r5 = call i32 @llvm.fptosi.sat.i32.f32(float %r4)               ; inf -> 2147483647
  %k6 = icmp eq i32 %r5, 2147483647
  %r7 = call float @id_f32(float 0xFFF0000000000000)
  %r8 = call i32 @llvm.fptosi.sat.i32.f32(float %r7)               ; -inf -> -2147483648
  %k9 = icmp eq i32 %r8, -2147483648
  %r10 = call float @id_f32(float 0x4415AF1D80000000)
  %r11 = call i32 @llvm.fptosi.sat.i32.f32(float %r10)             ; 1e+20 -> 2147483647
  %k12 = icmp eq i32 %r11, 2147483647
  %r13 = call float @id_f32(float 0xC415AF1D80000000)
  %r14 = call i32 @llvm.fptosi.sat.i32.f32(float %r13)             ; -1e+20 -> -2147483648
  %k15 = icmp eq i32 %r14, -2147483648
  %r16 = call float @id_f32(float 0.0)
  %r17 = call i32 @llvm.fptosi.sat.i32.f32(float %r16)             ; 0.0 -> 0
  %k18 = icmp eq i32 %r17, 0
  %r19 = call float @id_f32(float -0.0)
  %r20 = call i32 @llvm.fptosi.sat.i32.f32(float %r19)             ; -0.0 -> 0
  %k21 = icmp eq i32 %r20, 0
  %r22 = call float @id_f32(float 0.75)
  %r23 = call i32 @llvm.fptosi.sat.i32.f32(float %r22)             ; 0.75 -> 0
  %k24 = icmp eq i32 %r23, 0
  %r25 = call float @id_f32(float -0.75)
  %r26 = call i32 @llvm.fptosi.sat.i32.f32(float %r25)             ; -0.75 -> 0
  %k27 = icmp eq i32 %r26, 0
  %r28 = call float @id_f32(float 0x405EF999A0000000)
  %r29 = call i32 @llvm.fptosi.sat.i32.f32(float %r28)             ; 123.9 -> 123
  %k30 = icmp eq i32 %r29, 123
  %r31 = call float @id_f32(float -5.5)
  %r32 = call i32 @llvm.fptosi.sat.i32.f32(float %r31)             ; -5.5 -> -5
  %k33 = icmp eq i32 %r32, -5
  %r34 = call float @id_f32(float 2147483520.0)
  %r35 = call i32 @llvm.fptosi.sat.i32.f32(float %r34)             ; 2147483520.0 -> 2147483520
  %k36 = icmp eq i32 %r35, 2147483520
  %r37 = call float @id_f32(float 2147483648.0)
  %r38 = call i32 @llvm.fptosi.sat.i32.f32(float %r37)             ; 2147483648.0 -> 2147483647
  %k39 = icmp eq i32 %r38, 2147483647
  %r40 = call float @id_f32(float -2147483648.0)
  %r41 = call i32 @llvm.fptosi.sat.i32.f32(float %r40)             ; -2147483648.0 -> -2147483648
  %k42 = icmp eq i32 %r41, -2147483648
  %r43 = call float @id_f32(float -2147483904.0)
  %r44 = call i32 @llvm.fptosi.sat.i32.f32(float %r43)             ; -2147483904.0 -> -2147483648
  %k45 = icmp eq i32 %r44, -2147483648
  %r46 = call float @id_f32(float 4294967296.0)
  %r47 = call i32 @llvm.fptosi.sat.i32.f32(float %r46)             ; 4294967296.0 -> 2147483647
  %k48 = icmp eq i32 %r47, 2147483647
  %r49 = call float @id_f32(float 3000000000.0)
  %r50 = call i32 @llvm.fptosi.sat.i32.f32(float %r49)             ; 3000000000.0 -> 2147483647
  %k51 = icmp eq i32 %r50, 2147483647
  %r52 = call float @id_f32(float -3000000000.0)
  %r53 = call i32 @llvm.fptosi.sat.i32.f32(float %r52)             ; -3000000000.0 -> -2147483648
  %k54 = icmp eq i32 %r53, -2147483648
  %all1 = and i1 %k3, %k6
  %all2 = and i1 %all1, %k9
  %all3 = and i1 %all2, %k12
  %all4 = and i1 %all3, %k15
  %all5 = and i1 %all4, %k18
  %all6 = and i1 %all5, %k21
  %all7 = and i1 %all6, %k24
  %all8 = and i1 %all7, %k27
  %all9 = and i1 %all8, %k30
  %all10 = and i1 %all9, %k33
  %all11 = and i1 %all10, %k36
  %all12 = and i1 %all11, %k39
  %all13 = and i1 %all12, %k42
  %all14 = and i1 %all13, %k45
  %all15 = and i1 %all14, %k48
  %all16 = and i1 %all15, %k51
  %all17 = and i1 %all16, %k54
  %bad = xor i1 %all17, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 2: llvm.fptosi.sat, float64 to i64 (signed: -9223372036854775808 .. 9223372036854775807).
define internal i32 @check_fptosi_sat_f64_i64() noinline {
  %r1 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %r2 = call i64 @llvm.fptosi.sat.i64.f64(double %r1)              ; NaN -> 0
  %k3 = icmp eq i64 %r2, 0
  %r4 = call double @id_f64(double 0x7FF0000000000000)
  %r5 = call i64 @llvm.fptosi.sat.i64.f64(double %r4)              ; inf -> 9223372036854775807
  %k6 = icmp eq i64 %r5, 9223372036854775807
  %r7 = call double @id_f64(double 0xFFF0000000000000)
  %r8 = call i64 @llvm.fptosi.sat.i64.f64(double %r7)              ; -inf -> -9223372036854775808
  %k9 = icmp eq i64 %r8, -9223372036854775808
  %r10 = call double @id_f64(double 1.0e+20)
  %r11 = call i64 @llvm.fptosi.sat.i64.f64(double %r10)            ; 1e+20 -> 9223372036854775807
  %k12 = icmp eq i64 %r11, 9223372036854775807
  %r13 = call double @id_f64(double -1.0e+20)
  %r14 = call i64 @llvm.fptosi.sat.i64.f64(double %r13)            ; -1e+20 -> -9223372036854775808
  %k15 = icmp eq i64 %r14, -9223372036854775808
  %r16 = call double @id_f64(double 0.0)
  %r17 = call i64 @llvm.fptosi.sat.i64.f64(double %r16)            ; 0.0 -> 0
  %k18 = icmp eq i64 %r17, 0
  %r19 = call double @id_f64(double -0.0)
  %r20 = call i64 @llvm.fptosi.sat.i64.f64(double %r19)            ; -0.0 -> 0
  %k21 = icmp eq i64 %r20, 0
  %r22 = call double @id_f64(double 0.75)
  %r23 = call i64 @llvm.fptosi.sat.i64.f64(double %r22)            ; 0.75 -> 0
  %k24 = icmp eq i64 %r23, 0
  %r25 = call double @id_f64(double -0.75)
  %r26 = call i64 @llvm.fptosi.sat.i64.f64(double %r25)            ; -0.75 -> 0
  %k27 = icmp eq i64 %r26, 0
  %r28 = call double @id_f64(double 0x405EF9999999999A)
  %r29 = call i64 @llvm.fptosi.sat.i64.f64(double %r28)            ; 123.9 -> 123
  %k30 = icmp eq i64 %r29, 123
  %r31 = call double @id_f64(double -5.5)
  %r32 = call i64 @llvm.fptosi.sat.i64.f64(double %r31)            ; -5.5 -> -5
  %k33 = icmp eq i64 %r32, -5
  %r34 = call double @id_f64(double 0x43DFFFFFFFFFFFFF)
  %r35 = call i64 @llvm.fptosi.sat.i64.f64(double %r34)            ; 9223372036854774784.0 -> 9223372036854774784
  %k36 = icmp eq i64 %r35, 9223372036854774784
  %r37 = call double @id_f64(double 0x43E0000000000000)
  %r38 = call i64 @llvm.fptosi.sat.i64.f64(double %r37)            ; 9223372036854775808.0 -> 9223372036854775807
  %k39 = icmp eq i64 %r38, 9223372036854775807
  %r40 = call double @id_f64(double 0xC3E0000000000000)
  %r41 = call i64 @llvm.fptosi.sat.i64.f64(double %r40)            ; -9223372036854775808.0 -> -9223372036854775808
  %k42 = icmp eq i64 %r41, -9223372036854775808
  %r43 = call double @id_f64(double 0xC3E0000000000001)
  %r44 = call i64 @llvm.fptosi.sat.i64.f64(double %r43)            ; -9223372036854777856.0 -> -9223372036854775808
  %k45 = icmp eq i64 %r44, -9223372036854775808
  %r46 = call double @id_f64(double 1.8e+19)
  %r47 = call i64 @llvm.fptosi.sat.i64.f64(double %r46)            ; 18000000000000000000.0 -> 9223372036854775807
  %k48 = icmp eq i64 %r47, 9223372036854775807
  %r49 = call double @id_f64(double -1.8e+19)
  %r50 = call i64 @llvm.fptosi.sat.i64.f64(double %r49)            ; -18000000000000000000.0 -> -9223372036854775808
  %k51 = icmp eq i64 %r50, -9223372036854775808
  %r52 = call double @id_f64(double 1.0e+18)
  %r53 = call i64 @llvm.fptosi.sat.i64.f64(double %r52)            ; 1000000000000000000.0 -> 1000000000000000000
  %k54 = icmp eq i64 %r53, 1000000000000000000
  %r55 = call double @id_f64(double -1.0e+18)
  %r56 = call i64 @llvm.fptosi.sat.i64.f64(double %r55)            ; -1000000000000000000.0 -> -1000000000000000000
  %k57 = icmp eq i64 %r56, -1000000000000000000
  %r58 = call double @id_f64(double 4294967296.5)
  %r59 = call i64 @llvm.fptosi.sat.i64.f64(double %r58)            ; 4294967296.5 -> 4294967296
  %k60 = icmp eq i64 %r59, 4294967296
  %all1 = and i1 %k3, %k6
  %all2 = and i1 %all1, %k9
  %all3 = and i1 %all2, %k12
  %all4 = and i1 %all3, %k15
  %all5 = and i1 %all4, %k18
  %all6 = and i1 %all5, %k21
  %all7 = and i1 %all6, %k24
  %all8 = and i1 %all7, %k27
  %all9 = and i1 %all8, %k30
  %all10 = and i1 %all9, %k33
  %all11 = and i1 %all10, %k36
  %all12 = and i1 %all11, %k39
  %all13 = and i1 %all12, %k42
  %all14 = and i1 %all13, %k45
  %all15 = and i1 %all14, %k48
  %all16 = and i1 %all15, %k51
  %all17 = and i1 %all16, %k54
  %all18 = and i1 %all17, %k57
  %all19 = and i1 %all18, %k60
  %bad = xor i1 %all19, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 3: llvm.fptosi.sat, float32 to i64 (signed: -9223372036854775808 .. 9223372036854775807).
define internal i32 @check_fptosi_sat_f32_i64() noinline {
  %r1 = call float @fdiv_f32(float 0.0, float 0.0)                 ; 0.0 / 0.0 = NaN
  %r2 = call i64 @llvm.fptosi.sat.i64.f32(float %r1)               ; NaN -> 0
  %k3 = icmp eq i64 %r2, 0
  %r4 = call float @id_f32(float 0x7FF0000000000000)
  %r5 = call i64 @llvm.fptosi.sat.i64.f32(float %r4)               ; inf -> 9223372036854775807
  %k6 = icmp eq i64 %r5, 9223372036854775807
  %r7 = call float @id_f32(float 0xFFF0000000000000)
  %r8 = call i64 @llvm.fptosi.sat.i64.f32(float %r7)               ; -inf -> -9223372036854775808
  %k9 = icmp eq i64 %r8, -9223372036854775808
  %r10 = call float @id_f32(float 0x4415AF1D80000000)
  %r11 = call i64 @llvm.fptosi.sat.i64.f32(float %r10)             ; 1e+20 -> 9223372036854775807
  %k12 = icmp eq i64 %r11, 9223372036854775807
  %r13 = call float @id_f32(float 0xC415AF1D80000000)
  %r14 = call i64 @llvm.fptosi.sat.i64.f32(float %r13)             ; -1e+20 -> -9223372036854775808
  %k15 = icmp eq i64 %r14, -9223372036854775808
  %r16 = call float @id_f32(float 0.0)
  %r17 = call i64 @llvm.fptosi.sat.i64.f32(float %r16)             ; 0.0 -> 0
  %k18 = icmp eq i64 %r17, 0
  %r19 = call float @id_f32(float -0.0)
  %r20 = call i64 @llvm.fptosi.sat.i64.f32(float %r19)             ; -0.0 -> 0
  %k21 = icmp eq i64 %r20, 0
  %r22 = call float @id_f32(float 0.75)
  %r23 = call i64 @llvm.fptosi.sat.i64.f32(float %r22)             ; 0.75 -> 0
  %k24 = icmp eq i64 %r23, 0
  %r25 = call float @id_f32(float -0.75)
  %r26 = call i64 @llvm.fptosi.sat.i64.f32(float %r25)             ; -0.75 -> 0
  %k27 = icmp eq i64 %r26, 0
  %r28 = call float @id_f32(float 0x405EF999A0000000)
  %r29 = call i64 @llvm.fptosi.sat.i64.f32(float %r28)             ; 123.9 -> 123
  %k30 = icmp eq i64 %r29, 123
  %r31 = call float @id_f32(float -5.5)
  %r32 = call i64 @llvm.fptosi.sat.i64.f32(float %r31)             ; -5.5 -> -5
  %k33 = icmp eq i64 %r32, -5
  %r34 = call float @id_f32(float 0x43DFFFFFE0000000)
  %r35 = call i64 @llvm.fptosi.sat.i64.f32(float %r34)             ; 9223371487098961920.0 -> 9223371487098961920
  %k36 = icmp eq i64 %r35, 9223371487098961920
  %r37 = call float @id_f32(float 0x43E0000000000000)
  %r38 = call i64 @llvm.fptosi.sat.i64.f32(float %r37)             ; 9223372036854775808.0 -> 9223372036854775807
  %k39 = icmp eq i64 %r38, 9223372036854775807
  %r40 = call float @id_f32(float 0xC3E0000000000000)
  %r41 = call i64 @llvm.fptosi.sat.i64.f32(float %r40)             ; -9223372036854775808.0 -> -9223372036854775808
  %k42 = icmp eq i64 %r41, -9223372036854775808
  %r43 = call float @id_f32(float 0xC3E0000020000000)
  %r44 = call i64 @llvm.fptosi.sat.i64.f32(float %r43)             ; -9223373136366403584.0 -> -9223372036854775808
  %k45 = icmp eq i64 %r44, -9223372036854775808
  %r46 = call float @id_f32(float 0x43EF399B20000000)
  %r47 = call i64 @llvm.fptosi.sat.i64.f32(float %r46)             ; 18000000404716257280.0 -> 9223372036854775807
  %k48 = icmp eq i64 %r47, 9223372036854775807
  %r49 = call float @id_f32(float 0xC3EF399B20000000)
  %r50 = call i64 @llvm.fptosi.sat.i64.f32(float %r49)             ; -18000000404716257280.0 -> -9223372036854775808
  %k51 = icmp eq i64 %r50, -9223372036854775808
  %r52 = call float @id_f32(float 0x43ABC16D60000000)
  %r53 = call i64 @llvm.fptosi.sat.i64.f32(float %r52)             ; 999999984306749440.0 -> 999999984306749440
  %k54 = icmp eq i64 %r53, 999999984306749440
  %r55 = call float @id_f32(float 0xC3ABC16D60000000)
  %r56 = call i64 @llvm.fptosi.sat.i64.f32(float %r55)             ; -999999984306749440.0 -> -999999984306749440
  %k57 = icmp eq i64 %r56, -999999984306749440
  %r58 = call float @id_f32(float 3000000000.0)
  %r59 = call i64 @llvm.fptosi.sat.i64.f32(float %r58)             ; 3000000000.0 -> 3000000000
  %k60 = icmp eq i64 %r59, 3000000000
  %all1 = and i1 %k3, %k6
  %all2 = and i1 %all1, %k9
  %all3 = and i1 %all2, %k12
  %all4 = and i1 %all3, %k15
  %all5 = and i1 %all4, %k18
  %all6 = and i1 %all5, %k21
  %all7 = and i1 %all6, %k24
  %all8 = and i1 %all7, %k27
  %all9 = and i1 %all8, %k30
  %all10 = and i1 %all9, %k33
  %all11 = and i1 %all10, %k36
  %all12 = and i1 %all11, %k39
  %all13 = and i1 %all12, %k42
  %all14 = and i1 %all13, %k45
  %all15 = and i1 %all14, %k48
  %all16 = and i1 %all15, %k51
  %all17 = and i1 %all16, %k54
  %all18 = and i1 %all17, %k57
  %all19 = and i1 %all18, %k60
  %bad = xor i1 %all19, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 4: llvm.fptoui.sat, float64 to i32 (unsigned: 0 .. 4294967295).
define internal i32 @check_fptoui_sat_f64_i32() noinline {
  %r1 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %r2 = call i32 @llvm.fptoui.sat.i32.f64(double %r1)              ; NaN -> 0
  %k3 = icmp eq i32 %r2, 0
  %r4 = call double @id_f64(double 0x7FF0000000000000)
  %r5 = call i32 @llvm.fptoui.sat.i32.f64(double %r4)              ; inf -> 4294967295
  %k6 = icmp eq i32 %r5, -1
  %r7 = call double @id_f64(double 0xFFF0000000000000)
  %r8 = call i32 @llvm.fptoui.sat.i32.f64(double %r7)              ; -inf -> 0
  %k9 = icmp eq i32 %r8, 0
  %r10 = call double @id_f64(double 1.0e+20)
  %r11 = call i32 @llvm.fptoui.sat.i32.f64(double %r10)            ; 1e+20 -> 4294967295
  %k12 = icmp eq i32 %r11, -1
  %r13 = call double @id_f64(double -1.0e+20)
  %r14 = call i32 @llvm.fptoui.sat.i32.f64(double %r13)            ; -1e+20 -> 0
  %k15 = icmp eq i32 %r14, 0
  %r16 = call double @id_f64(double 0.0)
  %r17 = call i32 @llvm.fptoui.sat.i32.f64(double %r16)            ; 0.0 -> 0
  %k18 = icmp eq i32 %r17, 0
  %r19 = call double @id_f64(double -0.0)
  %r20 = call i32 @llvm.fptoui.sat.i32.f64(double %r19)            ; -0.0 -> 0
  %k21 = icmp eq i32 %r20, 0
  %r22 = call double @id_f64(double 0.75)
  %r23 = call i32 @llvm.fptoui.sat.i32.f64(double %r22)            ; 0.75 -> 0
  %k24 = icmp eq i32 %r23, 0
  %r25 = call double @id_f64(double -0.75)
  %r26 = call i32 @llvm.fptoui.sat.i32.f64(double %r25)            ; -0.75 -> 0
  %k27 = icmp eq i32 %r26, 0
  %r28 = call double @id_f64(double 0x405EF9999999999A)
  %r29 = call i32 @llvm.fptoui.sat.i32.f64(double %r28)            ; 123.9 -> 123
  %k30 = icmp eq i32 %r29, 123
  %r31 = call double @id_f64(double -5.5)
  %r32 = call i32 @llvm.fptoui.sat.i32.f64(double %r31)            ; -5.5 -> 0
  %k33 = icmp eq i32 %r32, 0
  %r34 = call double @id_f64(double 4294967295.0)
  %r35 = call i32 @llvm.fptoui.sat.i32.f64(double %r34)            ; 4294967295.0 -> 4294967295
  %k36 = icmp eq i32 %r35, -1
  %r37 = call double @id_f64(double 4294967295.5)
  %r38 = call i32 @llvm.fptoui.sat.i32.f64(double %r37)            ; 4294967295.5 -> 4294967295
  %k39 = icmp eq i32 %r38, -1
  %r40 = call double @id_f64(double 4294967296.0)
  %r41 = call i32 @llvm.fptoui.sat.i32.f64(double %r40)            ; 4294967296.0 -> 4294967295
  %k42 = icmp eq i32 %r41, -1
  %r43 = call double @id_f64(double 3000000000.0)
  %r44 = call i32 @llvm.fptoui.sat.i32.f64(double %r43)            ; 3000000000.0 -> 3000000000
  %k45 = icmp eq i32 %r44, -1294967296
  %r46 = call double @id_f64(double 2147483648.0)
  %r47 = call i32 @llvm.fptoui.sat.i32.f64(double %r46)            ; 2147483648.0 -> 2147483648
  %k48 = icmp eq i32 %r47, -2147483648
  %r49 = call double @id_f64(double -1.0)
  %r50 = call i32 @llvm.fptoui.sat.i32.f64(double %r49)            ; -1.0 -> 0
  %k51 = icmp eq i32 %r50, 0
  %r52 = call double @id_f64(double 0xBFEFF7CED916872B)
  %r53 = call i32 @llvm.fptoui.sat.i32.f64(double %r52)            ; -0.999 -> 0
  %k54 = icmp eq i32 %r53, 0
  %r55 = call double @id_f64(double -2147483649.0)
  %r56 = call i32 @llvm.fptoui.sat.i32.f64(double %r55)            ; -2147483649.0 -> 0
  %k57 = icmp eq i32 %r56, 0
  %all1 = and i1 %k3, %k6
  %all2 = and i1 %all1, %k9
  %all3 = and i1 %all2, %k12
  %all4 = and i1 %all3, %k15
  %all5 = and i1 %all4, %k18
  %all6 = and i1 %all5, %k21
  %all7 = and i1 %all6, %k24
  %all8 = and i1 %all7, %k27
  %all9 = and i1 %all8, %k30
  %all10 = and i1 %all9, %k33
  %all11 = and i1 %all10, %k36
  %all12 = and i1 %all11, %k39
  %all13 = and i1 %all12, %k42
  %all14 = and i1 %all13, %k45
  %all15 = and i1 %all14, %k48
  %all16 = and i1 %all15, %k51
  %all17 = and i1 %all16, %k54
  %all18 = and i1 %all17, %k57
  %bad = xor i1 %all18, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 5: llvm.fptoui.sat, float32 to i32 (unsigned: 0 .. 4294967295).
define internal i32 @check_fptoui_sat_f32_i32() noinline {
  %r1 = call float @fdiv_f32(float 0.0, float 0.0)                 ; 0.0 / 0.0 = NaN
  %r2 = call i32 @llvm.fptoui.sat.i32.f32(float %r1)               ; NaN -> 0
  %k3 = icmp eq i32 %r2, 0
  %r4 = call float @id_f32(float 0x7FF0000000000000)
  %r5 = call i32 @llvm.fptoui.sat.i32.f32(float %r4)               ; inf -> 4294967295
  %k6 = icmp eq i32 %r5, -1
  %r7 = call float @id_f32(float 0xFFF0000000000000)
  %r8 = call i32 @llvm.fptoui.sat.i32.f32(float %r7)               ; -inf -> 0
  %k9 = icmp eq i32 %r8, 0
  %r10 = call float @id_f32(float 0x4415AF1D80000000)
  %r11 = call i32 @llvm.fptoui.sat.i32.f32(float %r10)             ; 1e+20 -> 4294967295
  %k12 = icmp eq i32 %r11, -1
  %r13 = call float @id_f32(float 0xC415AF1D80000000)
  %r14 = call i32 @llvm.fptoui.sat.i32.f32(float %r13)             ; -1e+20 -> 0
  %k15 = icmp eq i32 %r14, 0
  %r16 = call float @id_f32(float 0.0)
  %r17 = call i32 @llvm.fptoui.sat.i32.f32(float %r16)             ; 0.0 -> 0
  %k18 = icmp eq i32 %r17, 0
  %r19 = call float @id_f32(float -0.0)
  %r20 = call i32 @llvm.fptoui.sat.i32.f32(float %r19)             ; -0.0 -> 0
  %k21 = icmp eq i32 %r20, 0
  %r22 = call float @id_f32(float 0.75)
  %r23 = call i32 @llvm.fptoui.sat.i32.f32(float %r22)             ; 0.75 -> 0
  %k24 = icmp eq i32 %r23, 0
  %r25 = call float @id_f32(float -0.75)
  %r26 = call i32 @llvm.fptoui.sat.i32.f32(float %r25)             ; -0.75 -> 0
  %k27 = icmp eq i32 %r26, 0
  %r28 = call float @id_f32(float 0x405EF999A0000000)
  %r29 = call i32 @llvm.fptoui.sat.i32.f32(float %r28)             ; 123.9 -> 123
  %k30 = icmp eq i32 %r29, 123
  %r31 = call float @id_f32(float -5.5)
  %r32 = call i32 @llvm.fptoui.sat.i32.f32(float %r31)             ; -5.5 -> 0
  %k33 = icmp eq i32 %r32, 0
  %r34 = call float @id_f32(float 4294967040.0)
  %r35 = call i32 @llvm.fptoui.sat.i32.f32(float %r34)             ; 4294967040.0 -> 4294967040
  %k36 = icmp eq i32 %r35, -256
  %r37 = call float @id_f32(float 4294967296.0)
  %r38 = call i32 @llvm.fptoui.sat.i32.f32(float %r37)             ; 4294967296.0 -> 4294967295
  %k39 = icmp eq i32 %r38, -1
  %r40 = call float @id_f32(float 3000000000.0)
  %r41 = call i32 @llvm.fptoui.sat.i32.f32(float %r40)             ; 3000000000.0 -> 3000000000
  %k42 = icmp eq i32 %r41, -1294967296
  %r43 = call float @id_f32(float 2147483648.0)
  %r44 = call i32 @llvm.fptoui.sat.i32.f32(float %r43)             ; 2147483648.0 -> 2147483648
  %k45 = icmp eq i32 %r44, -2147483648
  %r46 = call float @id_f32(float -1.0)
  %r47 = call i32 @llvm.fptoui.sat.i32.f32(float %r46)             ; -1.0 -> 0
  %k48 = icmp eq i32 %r47, 0
  %r49 = call float @id_f32(float -3000000000.0)
  %r50 = call i32 @llvm.fptoui.sat.i32.f32(float %r49)             ; -3000000000.0 -> 0
  %k51 = icmp eq i32 %r50, 0
  %all1 = and i1 %k3, %k6
  %all2 = and i1 %all1, %k9
  %all3 = and i1 %all2, %k12
  %all4 = and i1 %all3, %k15
  %all5 = and i1 %all4, %k18
  %all6 = and i1 %all5, %k21
  %all7 = and i1 %all6, %k24
  %all8 = and i1 %all7, %k27
  %all9 = and i1 %all8, %k30
  %all10 = and i1 %all9, %k33
  %all11 = and i1 %all10, %k36
  %all12 = and i1 %all11, %k39
  %all13 = and i1 %all12, %k42
  %all14 = and i1 %all13, %k45
  %all15 = and i1 %all14, %k48
  %all16 = and i1 %all15, %k51
  %bad = xor i1 %all16, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 6: llvm.fptoui.sat, float64 to i64 (unsigned: 0 .. 18446744073709551615).
define internal i32 @check_fptoui_sat_f64_i64() noinline {
  %r1 = call double @fdiv_f64(double 0.0, double 0.0)              ; 0.0 / 0.0 = NaN
  %r2 = call i64 @llvm.fptoui.sat.i64.f64(double %r1)              ; NaN -> 0
  %k3 = icmp eq i64 %r2, 0
  %r4 = call double @id_f64(double 0x7FF0000000000000)
  %r5 = call i64 @llvm.fptoui.sat.i64.f64(double %r4)              ; inf -> 18446744073709551615
  %k6 = icmp eq i64 %r5, -1
  %r7 = call double @id_f64(double 0xFFF0000000000000)
  %r8 = call i64 @llvm.fptoui.sat.i64.f64(double %r7)              ; -inf -> 0
  %k9 = icmp eq i64 %r8, 0
  %r10 = call double @id_f64(double 1.0e+20)
  %r11 = call i64 @llvm.fptoui.sat.i64.f64(double %r10)            ; 1e+20 -> 18446744073709551615
  %k12 = icmp eq i64 %r11, -1
  %r13 = call double @id_f64(double -1.0e+20)
  %r14 = call i64 @llvm.fptoui.sat.i64.f64(double %r13)            ; -1e+20 -> 0
  %k15 = icmp eq i64 %r14, 0
  %r16 = call double @id_f64(double 0.0)
  %r17 = call i64 @llvm.fptoui.sat.i64.f64(double %r16)            ; 0.0 -> 0
  %k18 = icmp eq i64 %r17, 0
  %r19 = call double @id_f64(double -0.0)
  %r20 = call i64 @llvm.fptoui.sat.i64.f64(double %r19)            ; -0.0 -> 0
  %k21 = icmp eq i64 %r20, 0
  %r22 = call double @id_f64(double 0.75)
  %r23 = call i64 @llvm.fptoui.sat.i64.f64(double %r22)            ; 0.75 -> 0
  %k24 = icmp eq i64 %r23, 0
  %r25 = call double @id_f64(double -0.75)
  %r26 = call i64 @llvm.fptoui.sat.i64.f64(double %r25)            ; -0.75 -> 0
  %k27 = icmp eq i64 %r26, 0
  %r28 = call double @id_f64(double 0x405EF9999999999A)
  %r29 = call i64 @llvm.fptoui.sat.i64.f64(double %r28)            ; 123.9 -> 123
  %k30 = icmp eq i64 %r29, 123
  %r31 = call double @id_f64(double -5.5)
  %r32 = call i64 @llvm.fptoui.sat.i64.f64(double %r31)            ; -5.5 -> 0
  %k33 = icmp eq i64 %r32, 0
  %r34 = call double @id_f64(double 0x43EFFFFFFFFFFFFF)
  %r35 = call i64 @llvm.fptoui.sat.i64.f64(double %r34)            ; 18446744073709549568.0 -> 18446744073709549568
  %k36 = icmp eq i64 %r35, -2048
  %r37 = call double @id_f64(double 0x43F0000000000000)
  %r38 = call i64 @llvm.fptoui.sat.i64.f64(double %r37)            ; 18446744073709551616.0 -> 18446744073709551615
  %k39 = icmp eq i64 %r38, -1
  %r40 = call double @id_f64(double 0x43E0000000000000)
  %r41 = call i64 @llvm.fptoui.sat.i64.f64(double %r40)            ; 9223372036854775808.0 -> 9223372036854775808
  %k42 = icmp eq i64 %r41, -9223372036854775808
  %r43 = call double @id_f64(double 1.0e+19)
  %r44 = call i64 @llvm.fptoui.sat.i64.f64(double %r43)            ; 10000000000000000000.0 -> 10000000000000000000
  %k45 = icmp eq i64 %r44, -8446744073709551616
  %r46 = call double @id_f64(double -1.0)
  %r47 = call i64 @llvm.fptoui.sat.i64.f64(double %r46)            ; -1.0 -> 0
  %k48 = icmp eq i64 %r47, 0
  %r49 = call double @id_f64(double 0xC3E0000000000000)
  %r50 = call i64 @llvm.fptoui.sat.i64.f64(double %r49)            ; -9223372036854775808.0 -> 0
  %k51 = icmp eq i64 %r50, 0
  %r52 = call double @id_f64(double 3.0e+19)
  %r53 = call i64 @llvm.fptoui.sat.i64.f64(double %r52)            ; 30000000000000000000.0 -> 18446744073709551615
  %k54 = icmp eq i64 %r53, -1
  %all1 = and i1 %k3, %k6
  %all2 = and i1 %all1, %k9
  %all3 = and i1 %all2, %k12
  %all4 = and i1 %all3, %k15
  %all5 = and i1 %all4, %k18
  %all6 = and i1 %all5, %k21
  %all7 = and i1 %all6, %k24
  %all8 = and i1 %all7, %k27
  %all9 = and i1 %all8, %k30
  %all10 = and i1 %all9, %k33
  %all11 = and i1 %all10, %k36
  %all12 = and i1 %all11, %k39
  %all13 = and i1 %all12, %k42
  %all14 = and i1 %all13, %k45
  %all15 = and i1 %all14, %k48
  %all16 = and i1 %all15, %k51
  %all17 = and i1 %all16, %k54
  %bad = xor i1 %all17, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 7: llvm.fptoui.sat, float32 to i64 (unsigned: 0 .. 18446744073709551615).
define internal i32 @check_fptoui_sat_f32_i64() noinline {
  %r1 = call float @fdiv_f32(float 0.0, float 0.0)                 ; 0.0 / 0.0 = NaN
  %r2 = call i64 @llvm.fptoui.sat.i64.f32(float %r1)               ; NaN -> 0
  %k3 = icmp eq i64 %r2, 0
  %r4 = call float @id_f32(float 0x7FF0000000000000)
  %r5 = call i64 @llvm.fptoui.sat.i64.f32(float %r4)               ; inf -> 18446744073709551615
  %k6 = icmp eq i64 %r5, -1
  %r7 = call float @id_f32(float 0xFFF0000000000000)
  %r8 = call i64 @llvm.fptoui.sat.i64.f32(float %r7)               ; -inf -> 0
  %k9 = icmp eq i64 %r8, 0
  %r10 = call float @id_f32(float 0x4415AF1D80000000)
  %r11 = call i64 @llvm.fptoui.sat.i64.f32(float %r10)             ; 1e+20 -> 18446744073709551615
  %k12 = icmp eq i64 %r11, -1
  %r13 = call float @id_f32(float 0xC415AF1D80000000)
  %r14 = call i64 @llvm.fptoui.sat.i64.f32(float %r13)             ; -1e+20 -> 0
  %k15 = icmp eq i64 %r14, 0
  %r16 = call float @id_f32(float 0.0)
  %r17 = call i64 @llvm.fptoui.sat.i64.f32(float %r16)             ; 0.0 -> 0
  %k18 = icmp eq i64 %r17, 0
  %r19 = call float @id_f32(float -0.0)
  %r20 = call i64 @llvm.fptoui.sat.i64.f32(float %r19)             ; -0.0 -> 0
  %k21 = icmp eq i64 %r20, 0
  %r22 = call float @id_f32(float 0.75)
  %r23 = call i64 @llvm.fptoui.sat.i64.f32(float %r22)             ; 0.75 -> 0
  %k24 = icmp eq i64 %r23, 0
  %r25 = call float @id_f32(float -0.75)
  %r26 = call i64 @llvm.fptoui.sat.i64.f32(float %r25)             ; -0.75 -> 0
  %k27 = icmp eq i64 %r26, 0
  %r28 = call float @id_f32(float 0x405EF999A0000000)
  %r29 = call i64 @llvm.fptoui.sat.i64.f32(float %r28)             ; 123.9 -> 123
  %k30 = icmp eq i64 %r29, 123
  %r31 = call float @id_f32(float -5.5)
  %r32 = call i64 @llvm.fptoui.sat.i64.f32(float %r31)             ; -5.5 -> 0
  %k33 = icmp eq i64 %r32, 0
  %r34 = call float @id_f32(float 0x43EFFFFFE0000000)
  %r35 = call i64 @llvm.fptoui.sat.i64.f32(float %r34)             ; 18446742974197923840.0 -> 18446742974197923840
  %k36 = icmp eq i64 %r35, -1099511627776
  %r37 = call float @id_f32(float 0x43F0000000000000)
  %r38 = call i64 @llvm.fptoui.sat.i64.f32(float %r37)             ; 18446744073709551616.0 -> 18446744073709551615
  %k39 = icmp eq i64 %r38, -1
  %r40 = call float @id_f32(float 0x43E0000000000000)
  %r41 = call i64 @llvm.fptoui.sat.i64.f32(float %r40)             ; 9223372036854775808.0 -> 9223372036854775808
  %k42 = icmp eq i64 %r41, -9223372036854775808
  %r43 = call float @id_f32(float 0x43E158E460000000)
  %r44 = call i64 @llvm.fptoui.sat.i64.f32(float %r43)             ; 9999999980506447872.0 -> 9999999980506447872
  %k45 = icmp eq i64 %r44, -8446744093203103744
  %r46 = call float @id_f32(float -1.0)
  %r47 = call i64 @llvm.fptoui.sat.i64.f32(float %r46)             ; -1.0 -> 0
  %k48 = icmp eq i64 %r47, 0
  %r49 = call float @id_f32(float 0xC3E0000000000000)
  %r50 = call i64 @llvm.fptoui.sat.i64.f32(float %r49)             ; -9223372036854775808.0 -> 0
  %k51 = icmp eq i64 %r50, 0
  %r52 = call float @id_f32(float 0x43FA0556A0000000)
  %r53 = call i64 @llvm.fptoui.sat.i64.f32(float %r52)             ; 30000001041030971392.0 -> 18446744073709551615
  %k54 = icmp eq i64 %r53, -1
  %all1 = and i1 %k3, %k6
  %all2 = and i1 %all1, %k9
  %all3 = and i1 %all2, %k12
  %all4 = and i1 %all3, %k15
  %all5 = and i1 %all4, %k18
  %all6 = and i1 %all5, %k21
  %all7 = and i1 %all6, %k24
  %all8 = and i1 %all7, %k27
  %all9 = and i1 %all8, %k30
  %all10 = and i1 %all9, %k33
  %all11 = and i1 %all10, %k36
  %all12 = and i1 %all11, %k39
  %all13 = and i1 %all12, %k42
  %all14 = and i1 %all13, %k45
  %all15 = and i1 %all14, %k48
  %all16 = and i1 %all15, %k51
  %all17 = and i1 %all16, %k54
  %bad = xor i1 %all17, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

define i32 @main() {
  %b0 = call i32 @check_fptosi_sat_f64_i32()
  %b1 = call i32 @check_fptosi_sat_f32_i32()
  %s1 = shl i32 %b1, 1
  %b2 = call i32 @check_fptosi_sat_f64_i64()
  %s2 = shl i32 %b2, 2
  %b3 = call i32 @check_fptosi_sat_f32_i64()
  %s3 = shl i32 %b3, 3
  %b4 = call i32 @check_fptoui_sat_f64_i32()
  %s4 = shl i32 %b4, 4
  %b5 = call i32 @check_fptoui_sat_f32_i32()
  %s5 = shl i32 %b5, 5
  %b6 = call i32 @check_fptoui_sat_f64_i64()
  %s6 = shl i32 %b6, 6
  %b7 = call i32 @check_fptoui_sat_f32_i64()
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

declare i32 @llvm.fptosi.sat.i32.f64(double)
declare i32 @llvm.fptosi.sat.i32.f32(float)
declare i64 @llvm.fptosi.sat.i64.f64(double)
declare i64 @llvm.fptosi.sat.i64.f32(float)
declare i32 @llvm.fptoui.sat.i32.f64(double)
declare i32 @llvm.fptoui.sat.i32.f32(float)
declare i64 @llvm.fptoui.sat.i64.f64(double)
declare i64 @llvm.fptoui.sat.i64.f32(float)
