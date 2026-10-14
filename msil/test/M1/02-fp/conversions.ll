; Conversions between integers and floating point (Milestone 5): sitofp and
; uitofp from i1, i8, i16, i32 and i64 (signed only; unsigned 64-bit sources
; are in u64-to-fp-rounding.ll), fptosi and fptoui to i8, i16, i32 and i64,
; fpext and fptrunc. Only in-range values (out-of-range fptosi/fptoui is
; poison), including the unsigned ranges above INT32_MAX and INT64_MAX that
; conv.u4 / conv.u handle on every runtime (isel.md 3.2): 3e9 and 4e9 to i32,
; 2^63 and 1.8e19 to i64. Every result must be exactly the correctly rounded
; (int -> FP) or truncated (FP -> int) value. int64 -> float32 is also
; converted in the calling method, on a value from an identity function that
; the JIT inlines, where the .NET Framework JIT constant-folds conv.r4 through
; float64. Each noinline check function returns 1 when a result is wrong; main
; ORs one bit per check into the exit status (8 bits, D37; check_sitofp_i64
; and check_sitofp_i64_folded share bit 1): [exit 0] means every check passed.
; RUN: %{msil_ll}

define double @sitofp_i1_f64(i1 %x) noinline {
  %r = sitofp i1 %x to double
  ret double %r
}

define float @sitofp_i1_f32(i1 %x) noinline {
  %r = sitofp i1 %x to float
  ret float %r
}

define double @sitofp_i8_f64(i8 %x) noinline {
  %r = sitofp i8 %x to double
  ret double %r
}

define float @sitofp_i8_f32(i8 %x) noinline {
  %r = sitofp i8 %x to float
  ret float %r
}

define double @sitofp_i16_f64(i16 %x) noinline {
  %r = sitofp i16 %x to double
  ret double %r
}

define float @sitofp_i16_f32(i16 %x) noinline {
  %r = sitofp i16 %x to float
  ret float %r
}

define double @sitofp_i32_f64(i32 %x) noinline {
  %r = sitofp i32 %x to double
  ret double %r
}

define float @sitofp_i32_f32(i32 %x) noinline {
  %r = sitofp i32 %x to float
  ret float %r
}

define double @sitofp_i64_f64(i64 %x) noinline {
  %r = sitofp i64 %x to double
  ret double %r
}

define float @sitofp_i64_f32(i64 %x) noinline {
  %r = sitofp i64 %x to float
  ret float %r
}

define double @uitofp_i1_f64(i1 %x) noinline {
  %r = uitofp i1 %x to double
  ret double %r
}

define float @uitofp_i1_f32(i1 %x) noinline {
  %r = uitofp i1 %x to float
  ret float %r
}

define double @uitofp_i8_f64(i8 %x) noinline {
  %r = uitofp i8 %x to double
  ret double %r
}

define float @uitofp_i8_f32(i8 %x) noinline {
  %r = uitofp i8 %x to float
  ret float %r
}

define double @uitofp_i16_f64(i16 %x) noinline {
  %r = uitofp i16 %x to double
  ret double %r
}

define float @uitofp_i16_f32(i16 %x) noinline {
  %r = uitofp i16 %x to float
  ret float %r
}

define double @uitofp_i32_f64(i32 %x) noinline {
  %r = uitofp i32 %x to double
  ret double %r
}

define float @uitofp_i32_f32(i32 %x) noinline {
  %r = uitofp i32 %x to float
  ret float %r
}

define i8 @fptosi_f64_i8(double %x) noinline {
  %r = fptosi double %x to i8
  ret i8 %r
}

define i16 @fptosi_f64_i16(double %x) noinline {
  %r = fptosi double %x to i16
  ret i16 %r
}

define i32 @fptosi_f64_i32(double %x) noinline {
  %r = fptosi double %x to i32
  ret i32 %r
}

define i64 @fptosi_f64_i64(double %x) noinline {
  %r = fptosi double %x to i64
  ret i64 %r
}

define i8 @fptosi_f32_i8(float %x) noinline {
  %r = fptosi float %x to i8
  ret i8 %r
}

define i16 @fptosi_f32_i16(float %x) noinline {
  %r = fptosi float %x to i16
  ret i16 %r
}

define i32 @fptosi_f32_i32(float %x) noinline {
  %r = fptosi float %x to i32
  ret i32 %r
}

define i64 @fptosi_f32_i64(float %x) noinline {
  %r = fptosi float %x to i64
  ret i64 %r
}

define i8 @fptoui_f64_i8(double %x) noinline {
  %r = fptoui double %x to i8
  ret i8 %r
}

define i16 @fptoui_f64_i16(double %x) noinline {
  %r = fptoui double %x to i16
  ret i16 %r
}

define i32 @fptoui_f64_i32(double %x) noinline {
  %r = fptoui double %x to i32
  ret i32 %r
}

define i64 @fptoui_f64_i64(double %x) noinline {
  %r = fptoui double %x to i64
  ret i64 %r
}

define i8 @fptoui_f32_i8(float %x) noinline {
  %r = fptoui float %x to i8
  ret i8 %r
}

define i16 @fptoui_f32_i16(float %x) noinline {
  %r = fptoui float %x to i16
  ret i16 %r
}

define i32 @fptoui_f32_i32(float %x) noinline {
  %r = fptoui float %x to i32
  ret i32 %r
}

define i64 @fptoui_f32_i64(float %x) noinline {
  %r = fptoui float %x to i64
  ret i64 %r
}

define double @fpext_f32_f64(float %x) noinline {
  %r = fpext float %x to double
  ret double %r
}

define float @fptrunc_f64_f32(double %x) noinline {
  %r = fptrunc double %x to float
  ret float %r
}

define i64 @id_i64(i64 %x) noinline {
  ret i64 %x
}

; Bit 0: sitofp from int32 (conv.r8 / conv.r4, one rounding) and from
; i1, i8 and i16 (sign-extended first; true is -1.0).
define internal i32 @check_sitofp_i32() noinline {
  %r1 = call double @sitofp_i32_f64(i32 0)                         ; 0 -> 0.0
  %k2 = fcmp oeq double %r1, 0.0
  %q3 = fdiv double 1.0, %r1
  %k4 = fcmp ogt double %q3, 0.0
  %r5 = call double @sitofp_i32_f64(i32 -1)                        ; -1 -> -1.0
  %k6 = fcmp oeq double %r5, -1.0
  %r7 = call double @sitofp_i32_f64(i32 -2147483648)               ; -2147483648 -> -2147483648.0
  %k8 = fcmp oeq double %r7, -2147483648.0
  %r9 = call double @sitofp_i32_f64(i32 2147483647)                ; 2147483647 -> 2147483647.0
  %k10 = fcmp oeq double %r9, 2147483647.0
  %r11 = call double @sitofp_i32_f64(i32 16777217)                 ; 16777217 -> 16777217.0
  %k12 = fcmp oeq double %r11, 16777217.0
  %r13 = call double @sitofp_i32_f64(i32 16777219)                 ; 16777219 -> 16777219.0
  %k14 = fcmp oeq double %r13, 16777219.0
  %r15 = call double @sitofp_i32_f64(i32 -16777219)                ; -16777219 -> -16777219.0
  %k16 = fcmp oeq double %r15, -16777219.0
  %r17 = call double @sitofp_i32_f64(i32 123456789)                ; 123456789 -> 123456789.0
  %k18 = fcmp oeq double %r17, 123456789.0
  %r19 = call double @sitofp_i32_f64(i32 7)                        ; 7 -> 7.0
  %k20 = fcmp oeq double %r19, 7.0
  %r21 = call double @sitofp_i1_f64(i1 true)                       ; -1 -> -1.0
  %k22 = fcmp oeq double %r21, -1.0
  %r23 = call double @sitofp_i1_f64(i1 false)                      ; 0 -> 0.0
  %k24 = fcmp oeq double %r23, 0.0
  %q25 = fdiv double 1.0, %r23
  %k26 = fcmp ogt double %q25, 0.0
  %r27 = call double @sitofp_i8_f64(i8 -128)                       ; -128 -> -128.0
  %k28 = fcmp oeq double %r27, -128.0
  %r29 = call double @sitofp_i8_f64(i8 127)                        ; 127 -> 127.0
  %k30 = fcmp oeq double %r29, 127.0
  %r31 = call double @sitofp_i8_f64(i8 -1)                         ; -1 -> -1.0
  %k32 = fcmp oeq double %r31, -1.0
  %r33 = call double @sitofp_i16_f64(i16 -32768)                   ; -32768 -> -32768.0
  %k34 = fcmp oeq double %r33, -32768.0
  %r35 = call double @sitofp_i16_f64(i16 32767)                    ; 32767 -> 32767.0
  %k36 = fcmp oeq double %r35, 32767.0
  %r37 = call double @sitofp_i16_f64(i16 -300)                     ; -300 -> -300.0
  %k38 = fcmp oeq double %r37, -300.0
  %r39 = call float @sitofp_i32_f32(i32 0)                         ; 0 -> 0.0
  %k40 = fcmp oeq float %r39, 0.0
  %q41 = fdiv float 1.0, %r39
  %k42 = fcmp ogt float %q41, 0.0
  %r43 = call float @sitofp_i32_f32(i32 -1)                        ; -1 -> -1.0
  %k44 = fcmp oeq float %r43, -1.0
  %r45 = call float @sitofp_i32_f32(i32 -2147483648)               ; -2147483648 -> -2147483648.0
  %k46 = fcmp oeq float %r45, -2147483648.0
  %r47 = call float @sitofp_i32_f32(i32 2147483647)                ; 2147483647 -> 2147483648.0
  %k48 = fcmp oeq float %r47, 2147483648.0
  %r49 = call float @sitofp_i32_f32(i32 16777217)                  ; 16777217 -> 16777216.0
  %k50 = fcmp oeq float %r49, 16777216.0
  %r51 = call float @sitofp_i32_f32(i32 16777219)                  ; 16777219 -> 16777220.0
  %k52 = fcmp oeq float %r51, 16777220.0
  %r53 = call float @sitofp_i32_f32(i32 -16777219)                 ; -16777219 -> -16777220.0
  %k54 = fcmp oeq float %r53, -16777220.0
  %r55 = call float @sitofp_i32_f32(i32 123456789)                 ; 123456789 -> 123456792.0
  %k56 = fcmp oeq float %r55, 123456792.0
  %r57 = call float @sitofp_i32_f32(i32 7)                         ; 7 -> 7.0
  %k58 = fcmp oeq float %r57, 7.0
  %r59 = call float @sitofp_i1_f32(i1 true)                        ; -1 -> -1.0
  %k60 = fcmp oeq float %r59, -1.0
  %r61 = call float @sitofp_i1_f32(i1 false)                       ; 0 -> 0.0
  %k62 = fcmp oeq float %r61, 0.0
  %q63 = fdiv float 1.0, %r61
  %k64 = fcmp ogt float %q63, 0.0
  %r65 = call float @sitofp_i8_f32(i8 -128)                        ; -128 -> -128.0
  %k66 = fcmp oeq float %r65, -128.0
  %r67 = call float @sitofp_i8_f32(i8 127)                         ; 127 -> 127.0
  %k68 = fcmp oeq float %r67, 127.0
  %r69 = call float @sitofp_i8_f32(i8 -1)                          ; -1 -> -1.0
  %k70 = fcmp oeq float %r69, -1.0
  %r71 = call float @sitofp_i16_f32(i16 -32768)                    ; -32768 -> -32768.0
  %k72 = fcmp oeq float %r71, -32768.0
  %r73 = call float @sitofp_i16_f32(i16 32767)                     ; 32767 -> 32767.0
  %k74 = fcmp oeq float %r73, 32767.0
  %r75 = call float @sitofp_i16_f32(i16 -300)                      ; -300 -> -300.0
  %k76 = fcmp oeq float %r75, -300.0
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
  %bad = xor i1 %all37, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 1: sitofp from 64-bit values (conv.r8 / conv.r4 of a native int):
; one rounding, also to float32 (2^60 + 2^36 + 1 rounds up to
; 2^60 + 2^37; through float64 it would round to 2^60).
define internal i32 @check_sitofp_i64() noinline {
  %r1 = call double @sitofp_i64_f64(i64 0)                         ; 0 -> 0.0
  %k2 = fcmp oeq double %r1, 0.0
  %q3 = fdiv double 1.0, %r1
  %k4 = fcmp ogt double %q3, 0.0
  %r5 = call double @sitofp_i64_f64(i64 -1)                        ; -1 -> -1.0
  %k6 = fcmp oeq double %r5, -1.0
  %r7 = call double @sitofp_i64_f64(i64 -9223372036854775808)      ; -9223372036854775808 -> -9223372036854775808.0
  %k8 = fcmp oeq double %r7, 0xC3E0000000000000
  %r9 = call double @sitofp_i64_f64(i64 9223372036854775807)       ; 9223372036854775807 -> 9223372036854775808.0
  %k10 = fcmp oeq double %r9, 0x43E0000000000000
  %r11 = call double @sitofp_i64_f64(i64 9007199254740993)         ; 9007199254740993 -> 9007199254740992.0
  %k12 = fcmp oeq double %r11, 9007199254740992.0
  %r13 = call double @sitofp_i64_f64(i64 9007199254740995)         ; 9007199254740995 -> 9007199254740996.0
  %k14 = fcmp oeq double %r13, 9007199254740996.0
  %r15 = call double @sitofp_i64_f64(i64 1152921573326323713)      ; 1152921573326323713 -> 1152921573326323712.0
  %k16 = fcmp oeq double %r15, 0x43B0000010000000
  %r17 = call double @sitofp_i64_f64(i64 -1152921573326323713)     ; -1152921573326323713 -> -1152921573326323712.0
  %k18 = fcmp oeq double %r17, 0xC3B0000010000000
  %r19 = call double @sitofp_i64_f64(i64 1000000000000000)         ; 1000000000000000 -> 1000000000000000.0
  %k20 = fcmp oeq double %r19, 1000000000000000.0
  %r21 = call double @sitofp_i64_f64(i64 4294967295)               ; 4294967295 -> 4294967295.0
  %k22 = fcmp oeq double %r21, 4294967295.0
  %r23 = call double @sitofp_i64_f64(i64 -2147483649)              ; -2147483649 -> -2147483649.0
  %k24 = fcmp oeq double %r23, -2147483649.0
  %r25 = call double @sitofp_i64_f64(i64 549755813889)             ; 549755813889 -> 549755813889.0
  %k26 = fcmp oeq double %r25, 549755813889.0
  %r27 = call float @sitofp_i64_f32(i64 0)                         ; 0 -> 0.0
  %k28 = fcmp oeq float %r27, 0.0
  %q29 = fdiv float 1.0, %r27
  %k30 = fcmp ogt float %q29, 0.0
  %r31 = call float @sitofp_i64_f32(i64 -1)                        ; -1 -> -1.0
  %k32 = fcmp oeq float %r31, -1.0
  %r33 = call float @sitofp_i64_f32(i64 -9223372036854775808)      ; -9223372036854775808 -> -9223372036854775808.0
  %k34 = fcmp oeq float %r33, 0xC3E0000000000000
  %r35 = call float @sitofp_i64_f32(i64 9223372036854775807)       ; 9223372036854775807 -> 9223372036854775808.0
  %k36 = fcmp oeq float %r35, 0x43E0000000000000
  %r37 = call float @sitofp_i64_f32(i64 9007199254740993)          ; 9007199254740993 -> 9007199254740992.0
  %k38 = fcmp oeq float %r37, 9007199254740992.0
  %r39 = call float @sitofp_i64_f32(i64 9007199254740995)          ; 9007199254740995 -> 9007199254740992.0
  %k40 = fcmp oeq float %r39, 9007199254740992.0
  %r41 = call float @sitofp_i64_f32(i64 1152921573326323713)       ; 1152921573326323713 -> 1152921642045800448.0
  %k42 = fcmp oeq float %r41, 0x43B0000020000000
  %r43 = call float @sitofp_i64_f32(i64 -1152921573326323713)      ; -1152921573326323713 -> -1152921642045800448.0
  %k44 = fcmp oeq float %r43, 0xC3B0000020000000
  %r45 = call float @sitofp_i64_f32(i64 1000000000000000)          ; 1000000000000000 -> 999999986991104.0
  %k46 = fcmp oeq float %r45, 999999986991104.0
  %r47 = call float @sitofp_i64_f32(i64 4294967295)                ; 4294967295 -> 4294967296.0
  %k48 = fcmp oeq float %r47, 4294967296.0
  %r49 = call float @sitofp_i64_f32(i64 -2147483649)               ; -2147483649 -> -2147483648.0
  %k50 = fcmp oeq float %r49, -2147483648.0
  %r51 = call float @sitofp_i64_f32(i64 549755813889)              ; 549755813889 -> 549755813888.0
  %k52 = fcmp oeq float %r51, 549755813888.0
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

; Bit 1 (with check_sitofp_i64): sitofp from int64 to float32 in this
; method, on a value from the identity function @id_i64. The JIT inlines
; @id_i64, so conv.r4 sees a constant, and the .NET Framework 4.8 JIT
; folds conv.r4 of a constant int64 as (float32)(float64)x, which rounds
; twice. Each case rounds wrongly that way (2^60 + 2^36 + 1 would give
; 2^60), so it is correct only with the round-to-odd step before conv.r4
; (plan section 12, row 'M5.a extended (int64 to float32 under JIT
; constant folding)').
define internal i32 @check_sitofp_i64_folded() noinline {
  %r1 = call i64 @id_i64(i64 1152921573326323713)                  ; 2^60 + 2^36 + 1
  %r2 = sitofp i64 %r1 to float                                    ; -> 1152921642045800448.0 (0x5D800001)
  %k3 = fcmp oeq float %r2, 0x43B0000020000000
  %r4 = call i64 @id_i64(i64 -1152921573326323713)                 ; -(2^60 + 2^36 + 1)
  %r5 = sitofp i64 %r4 to float                                    ; -> -1152921642045800448.0 (0xDD800001)
  %k6 = fcmp oeq float %r5, 0xC3B0000020000000
  %r7 = call i64 @id_i64(i64 4611686293305294849)                  ; 2^62 + 2^38 + 1
  %r8 = sitofp i64 %r7 to float                                    ; -> 4611686568183201792.0 (0x5E800001)
  %k9 = fcmp oeq float %r8, 0x43D0000020000000
  %r10 = call i64 @id_i64(i64 -4611686293305294849)                ; -(2^62 + 2^38 + 1)
  %r11 = sitofp i64 %r10 to float                                  ; -> -4611686568183201792.0 (0xDE800001)
  %k12 = fcmp oeq float %r11, 0xC3D0000020000000
  %r13 = call i64 @id_i64(i64 9223371761976868863)                 ; 2^63 - 2^38 - 1
  %r14 = sitofp i64 %r13 to float                                  ; -> 9223371487098961920.0 (0x5EFFFFFF)
  %k15 = fcmp oeq float %r14, 0x43DFFFFFE0000000
  %r16 = call i64 @id_i64(i64 -9223371761976868863)                ; -(2^63 - 2^38 - 1)
  %r17 = sitofp i64 %r16 to float                                  ; -> -9223371487098961920.0 (0xDEFFFFFF)
  %k18 = fcmp oeq float %r17, 0xC3DFFFFFE0000000
  %all1 = and i1 %k3, %k6
  %all2 = and i1 %all1, %k9
  %all3 = and i1 %all2, %k12
  %all4 = and i1 %all3, %k15
  %all5 = and i1 %all4, %k18
  %bad = xor i1 %all5, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 2: uitofp from int32 (conv.r.un, exact, then conv.r4 for float32:
; one rounding) and from i1, i8 and i16 (zero-extended first).
define internal i32 @check_uitofp_i32() noinline {
  %r1 = call double @uitofp_i32_f64(i32 0)                         ; 0 -> 0.0
  %k2 = fcmp oeq double %r1, 0.0
  %q3 = fdiv double 1.0, %r1
  %k4 = fcmp ogt double %q3, 0.0
  %r5 = call double @uitofp_i32_f64(i32 1)                         ; 1 -> 1.0
  %k6 = fcmp oeq double %r5, 1.0
  %r7 = call double @uitofp_i32_f64(i32 -1)                        ; 4294967295 -> 4294967295.0
  %k8 = fcmp oeq double %r7, 4294967295.0
  %r9 = call double @uitofp_i32_f64(i32 -2147483648)               ; 2147483648 -> 2147483648.0
  %k10 = fcmp oeq double %r9, 2147483648.0
  %r11 = call double @uitofp_i32_f64(i32 -2147483519)              ; 2147483777 -> 2147483777.0
  %k12 = fcmp oeq double %r11, 2147483777.0
  %r13 = call double @uitofp_i32_f64(i32 2147483647)               ; 2147483647 -> 2147483647.0
  %k14 = fcmp oeq double %r13, 2147483647.0
  %r15 = call double @uitofp_i32_f64(i32 -129)                     ; 4294967167 -> 4294967167.0
  %k16 = fcmp oeq double %r15, 4294967167.0
  %r17 = call double @uitofp_i32_f64(i32 -128)                     ; 4294967168 -> 4294967168.0
  %k18 = fcmp oeq double %r17, 4294967168.0
  %r19 = call double @uitofp_i32_f64(i32 16777217)                 ; 16777217 -> 16777217.0
  %k20 = fcmp oeq double %r19, 16777217.0
  %r21 = call double @uitofp_i1_f64(i1 true)                       ; 1 -> 1.0
  %k22 = fcmp oeq double %r21, 1.0
  %r23 = call double @uitofp_i8_f64(i8 -1)                         ; 255 -> 255.0
  %k24 = fcmp oeq double %r23, 255.0
  %r25 = call double @uitofp_i8_f64(i8 -128)                       ; 128 -> 128.0
  %k26 = fcmp oeq double %r25, 128.0
  %r27 = call double @uitofp_i16_f64(i16 -1)                       ; 65535 -> 65535.0
  %k28 = fcmp oeq double %r27, 65535.0
  %r29 = call double @uitofp_i16_f64(i16 -32768)                   ; 32768 -> 32768.0
  %k30 = fcmp oeq double %r29, 32768.0
  %r31 = call float @uitofp_i32_f32(i32 0)                         ; 0 -> 0.0
  %k32 = fcmp oeq float %r31, 0.0
  %q33 = fdiv float 1.0, %r31
  %k34 = fcmp ogt float %q33, 0.0
  %r35 = call float @uitofp_i32_f32(i32 1)                         ; 1 -> 1.0
  %k36 = fcmp oeq float %r35, 1.0
  %r37 = call float @uitofp_i32_f32(i32 -1)                        ; 4294967295 -> 4294967296.0
  %k38 = fcmp oeq float %r37, 4294967296.0
  %r39 = call float @uitofp_i32_f32(i32 -2147483648)               ; 2147483648 -> 2147483648.0
  %k40 = fcmp oeq float %r39, 2147483648.0
  %r41 = call float @uitofp_i32_f32(i32 -2147483519)               ; 2147483777 -> 2147483904.0
  %k42 = fcmp oeq float %r41, 2147483904.0
  %r43 = call float @uitofp_i32_f32(i32 2147483647)                ; 2147483647 -> 2147483648.0
  %k44 = fcmp oeq float %r43, 2147483648.0
  %r45 = call float @uitofp_i32_f32(i32 -129)                      ; 4294967167 -> 4294967040.0
  %k46 = fcmp oeq float %r45, 4294967040.0
  %r47 = call float @uitofp_i32_f32(i32 -128)                      ; 4294967168 -> 4294967296.0
  %k48 = fcmp oeq float %r47, 4294967296.0
  %r49 = call float @uitofp_i32_f32(i32 16777217)                  ; 16777217 -> 16777216.0
  %k50 = fcmp oeq float %r49, 16777216.0
  %r51 = call float @uitofp_i1_f32(i1 true)                        ; 1 -> 1.0
  %k52 = fcmp oeq float %r51, 1.0
  %r53 = call float @uitofp_i8_f32(i8 -1)                          ; 255 -> 255.0
  %k54 = fcmp oeq float %r53, 255.0
  %r55 = call float @uitofp_i8_f32(i8 -128)                        ; 128 -> 128.0
  %k56 = fcmp oeq float %r55, 128.0
  %r57 = call float @uitofp_i16_f32(i16 -1)                        ; 65535 -> 65535.0
  %k58 = fcmp oeq float %r57, 65535.0
  %r59 = call float @uitofp_i16_f32(i16 -32768)                    ; 32768 -> 32768.0
  %k60 = fcmp oeq float %r59, 32768.0
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

; Bit 3: fptosi to int32, i16 and i8 rounds toward zero (in-range
; values only: out of range is poison).
define internal i32 @check_fptosi_i32() noinline {
  %r1 = call i32 @fptosi_f64_i32(double 0x4007333333333333)        ; 2.9 -> 2
  %k2 = icmp eq i32 %r1, 2
  %r3 = call i32 @fptosi_f64_i32(double 0xC007333333333333)        ; -2.9 -> -2
  %k4 = icmp eq i32 %r3, -2
  %r5 = call i32 @fptosi_f64_i32(double -0.0)                      ; -0.0 -> 0
  %k6 = icmp eq i32 %r5, 0
  %r7 = call i32 @fptosi_f64_i32(double 0x3FEFAE147AE147AE)        ; 0.99 -> 0
  %k8 = icmp eq i32 %r7, 0
  %r9 = call i32 @fptosi_f64_i32(double 0xBFEFAE147AE147AE)        ; -0.99 -> 0
  %k10 = icmp eq i32 %r9, 0
  %r11 = call i32 @fptosi_f64_i32(double 2147483520.0)             ; 2147483520.0 -> 2147483520
  %k12 = icmp eq i32 %r11, 2147483520
  %r13 = call i32 @fptosi_f64_i32(double -2147483648.0)            ; -2147483648.0 -> -2147483648
  %k14 = icmp eq i32 %r13, -2147483648
  %r15 = call i32 @fptosi_f64_i32(double 1000000000.5)             ; 1000000000.5 -> 1000000000
  %k16 = icmp eq i32 %r15, 1000000000
  %r17 = call i32 @fptosi_f64_i32(double 16777217.0)               ; 16777217.0 -> 16777217
  %k18 = icmp eq i32 %r17, 16777217
  %r19 = call i8 @fptosi_f64_i8(double 0xC060166666666666)         ; -128.7 -> -128
  %k20 = icmp eq i8 %r19, -128
  %r21 = call i8 @fptosi_f64_i8(double 0x405FF9999999999A)         ; 127.9 -> 127
  %k22 = icmp eq i8 %r21, 127
  %r23 = call i8 @fptosi_f64_i8(double -1.5)                       ; -1.5 -> -1
  %k24 = icmp eq i8 %r23, -1
  %r25 = call i16 @fptosi_f64_i16(double -32768.5)                 ; -32768.5 -> -32768
  %k26 = icmp eq i16 %r25, -32768
  %r27 = call i16 @fptosi_f64_i16(double 0x40DFFFFF5C28F5C3)       ; 32767.99 -> 32767
  %k28 = icmp eq i16 %r27, 32767
  %r29 = call i16 @fptosi_f64_i16(double 300.25)                   ; 300.25 -> 300
  %k30 = icmp eq i16 %r29, 300
  %r31 = call i32 @fptosi_f32_i32(float 0x4007333340000000)        ; 2.9 -> 2
  %k32 = icmp eq i32 %r31, 2
  %r33 = call i32 @fptosi_f32_i32(float 0xC007333340000000)        ; -2.9 -> -2
  %k34 = icmp eq i32 %r33, -2
  %r35 = call i32 @fptosi_f32_i32(float -0.0)                      ; -0.0 -> 0
  %k36 = icmp eq i32 %r35, 0
  %r37 = call i32 @fptosi_f32_i32(float 0x3FEFAE1480000000)        ; 0.99 -> 0
  %k38 = icmp eq i32 %r37, 0
  %r39 = call i32 @fptosi_f32_i32(float 0xBFEFAE1480000000)        ; -0.99 -> 0
  %k40 = icmp eq i32 %r39, 0
  %r41 = call i32 @fptosi_f32_i32(float 2147483520.0)              ; 2147483520.0 -> 2147483520
  %k42 = icmp eq i32 %r41, 2147483520
  %r43 = call i32 @fptosi_f32_i32(float -2147483648.0)             ; -2147483648.0 -> -2147483648
  %k44 = icmp eq i32 %r43, -2147483648
  %r45 = call i32 @fptosi_f32_i32(float 1000000000.0)              ; 1000000000.0 -> 1000000000
  %k46 = icmp eq i32 %r45, 1000000000
  %r47 = call i32 @fptosi_f32_i32(float 16777216.0)                ; 16777216.0 -> 16777216
  %k48 = icmp eq i32 %r47, 16777216
  %r49 = call i8 @fptosi_f32_i8(float 0xC060166660000000)          ; -128.7 -> -128
  %k50 = icmp eq i8 %r49, -128
  %r51 = call i8 @fptosi_f32_i8(float 0x405FF999A0000000)          ; 127.9 -> 127
  %k52 = icmp eq i8 %r51, 127
  %r53 = call i8 @fptosi_f32_i8(float -1.5)                        ; -1.5 -> -1
  %k54 = icmp eq i8 %r53, -1
  %r55 = call i16 @fptosi_f32_i16(float -32768.5)                  ; -32768.5 -> -32768
  %k56 = icmp eq i16 %r55, -32768
  %r57 = call i16 @fptosi_f32_i16(float 32767.990234375)           ; 32767.99 -> 32767
  %k58 = icmp eq i16 %r57, 32767
  %r59 = call i16 @fptosi_f32_i16(float 300.25)                    ; 300.25 -> 300
  %k60 = icmp eq i16 %r59, 300
  %r61 = call i32 @fptosi_f64_i32(double 2147483647.0)             ; 2147483647.0 -> 2147483647
  %k62 = icmp eq i32 %r61, 2147483647
  %r63 = call i32 @fptosi_f64_i32(double 0x41DFFFFFFFF9999A)       ; 2147483647.9 -> 2147483647
  %k64 = icmp eq i32 %r63, 2147483647
  %r65 = call i32 @fptosi_f64_i32(double 0xC1E00000001CCCCD)       ; -2147483648.9 -> -2147483648
  %k66 = icmp eq i32 %r65, -2147483648
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
  %bad = xor i1 %all32, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 4: fptosi to 64 bits (conv.i) rounds toward zero, from float64
; and float32, up to the int64 limits.
define internal i32 @check_fptosi_i64() noinline {
  %r1 = call i64 @fptosi_f64_i64(double 0x4007333333333333)        ; 2.9 -> 2
  %k2 = icmp eq i64 %r1, 2
  %r3 = call i64 @fptosi_f64_i64(double 0xC007333333333333)        ; -2.9 -> -2
  %k4 = icmp eq i64 %r3, -2
  %r5 = call i64 @fptosi_f64_i64(double -0.0)                      ; -0.0 -> 0
  %k6 = icmp eq i64 %r5, 0
  %r7 = call i64 @fptosi_f64_i64(double 0xC3E0000000000000)        ; -9223372036854775808.0 -> -9223372036854775808
  %k8 = icmp eq i64 %r7, -9223372036854775808
  %r9 = call i64 @fptosi_f64_i64(double 1099511627776.5)           ; 1099511627776.5 -> 1099511627776
  %k10 = icmp eq i64 %r9, 1099511627776
  %r11 = call i64 @fptosi_f64_i64(double 10000000000.0)            ; 10000000000.0 -> 10000000000
  %k12 = icmp eq i64 %r11, 10000000000
  %r13 = call i64 @fptosi_f64_i64(double -7.75)                    ; -7.75 -> -7
  %k14 = icmp eq i64 %r13, -7
  %r15 = call i64 @fptosi_f64_i64(double 0x40C81CD6C8B43958)       ; 12345.678 -> 12345
  %k16 = icmp eq i64 %r15, 12345
  %r17 = call i64 @fptosi_f32_i64(float 0x4007333340000000)        ; 2.9 -> 2
  %k18 = icmp eq i64 %r17, 2
  %r19 = call i64 @fptosi_f32_i64(float 0xC007333340000000)        ; -2.9 -> -2
  %k20 = icmp eq i64 %r19, -2
  %r21 = call i64 @fptosi_f32_i64(float -0.0)                      ; -0.0 -> 0
  %k22 = icmp eq i64 %r21, 0
  %r23 = call i64 @fptosi_f32_i64(float 0xC3E0000000000000)        ; -9223372036854775808.0 -> -9223372036854775808
  %k24 = icmp eq i64 %r23, -9223372036854775808
  %r25 = call i64 @fptosi_f32_i64(float 1099511627776.0)           ; 1099511627776.0 -> 1099511627776
  %k26 = icmp eq i64 %r25, 1099511627776
  %r27 = call i64 @fptosi_f32_i64(float 10000000000.0)             ; 10000000000.0 -> 10000000000
  %k28 = icmp eq i64 %r27, 10000000000
  %r29 = call i64 @fptosi_f32_i64(float -7.75)                     ; -7.75 -> -7
  %k30 = icmp eq i64 %r29, -7
  %r31 = call i64 @fptosi_f32_i64(float 12345.677734375)           ; 12345.678 -> 12345
  %k32 = icmp eq i64 %r31, 12345
  %r33 = call i64 @fptosi_f64_i64(double 0x43DFFFFFFFFFFFFF)       ; 9223372036854774784.0 -> 9223372036854774784
  %k34 = icmp eq i64 %r33, 9223372036854774784
  %r35 = call i64 @fptosi_f64_i64(double -1000000000000000.5)      ; -1000000000000000.5 -> -1000000000000000
  %k36 = icmp eq i64 %r35, -1000000000000000
  %r37 = call i64 @fptosi_f64_i64(double 4503599627370495.5)       ; 4503599627370495.5 -> 4503599627370495
  %k38 = icmp eq i64 %r37, 4503599627370495
  %r39 = call i64 @fptosi_f64_i64(double 9007199254740992.0)       ; 9007199254740992.0 -> 9007199254740992
  %k40 = icmp eq i64 %r39, 9007199254740992
  %r41 = call i64 @fptosi_f32_i64(float 0x43DFFFFFE0000000)        ; 9223371487098961920.0 -> 9223371487098961920
  %k42 = icmp eq i64 %r41, 9223371487098961920
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
  %bad = xor i1 %all20, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 5: fptoui to int32 (conv.u4), i16 and i8, including values
; above INT32_MAX (3e9, 4e9), which are in range for unsigned int.
define internal i32 @check_fptoui_i32() noinline {
  %r1 = call i32 @fptoui_f64_i32(double 3000000000.0)              ; 3000000000.0 -> 3000000000
  %k2 = icmp eq i32 %r1, -1294967296
  %r3 = call i32 @fptoui_f64_i32(double 4000000000.0)              ; 4000000000.0 -> 4000000000
  %k4 = icmp eq i32 %r3, -294967296
  %r5 = call i32 @fptoui_f64_i32(double 2147483648.0)              ; 2147483648.0 -> 2147483648
  %k6 = icmp eq i32 %r5, -2147483648
  %r7 = call i32 @fptoui_f64_i32(double 0x3FECCCCCCCCCCCCD)        ; 0.9 -> 0
  %k8 = icmp eq i32 %r7, 0
  %r9 = call i32 @fptoui_f64_i32(double 1.5)                       ; 1.5 -> 1
  %k10 = icmp eq i32 %r9, 1
  %r11 = call i32 @fptoui_f64_i32(double -0.0)                     ; -0.0 -> 0
  %k12 = icmp eq i32 %r11, 0
  %r13 = call i32 @fptoui_f64_i32(double 3900000000.0)             ; 3900000000.0 -> 3900000000
  %k14 = icmp eq i32 %r13, -394967296
  %r15 = call i32 @fptoui_f64_i32(double 3221225472.0)             ; 3221225472.0 -> 3221225472
  %k16 = icmp eq i32 %r15, -1073741824
  %r17 = call i8 @fptoui_f64_i8(double 0x406FFCCCCCCCCCCD)         ; 255.9 -> 255
  %k18 = icmp eq i8 %r17, -1
  %r19 = call i8 @fptoui_f64_i8(double 128.5)                      ; 128.5 -> 128
  %k20 = icmp eq i8 %r19, -128
  %r21 = call i8 @fptoui_f64_i8(double 0.5)                        ; 0.5 -> 0
  %k22 = icmp eq i8 %r21, 0
  %r23 = call i16 @fptoui_f64_i16(double 65535.5)                  ; 65535.5 -> 65535
  %k24 = icmp eq i16 %r23, -1
  %r25 = call i16 @fptoui_f64_i16(double 32768.0)                  ; 32768.0 -> 32768
  %k26 = icmp eq i16 %r25, -32768
  %r27 = call i16 @fptoui_f64_i16(double 40000.75)                 ; 40000.75 -> 40000
  %k28 = icmp eq i16 %r27, -25536
  %r29 = call i32 @fptoui_f32_i32(float 3000000000.0)              ; 3000000000.0 -> 3000000000
  %k30 = icmp eq i32 %r29, -1294967296
  %r31 = call i32 @fptoui_f32_i32(float 4000000000.0)              ; 4000000000.0 -> 4000000000
  %k32 = icmp eq i32 %r31, -294967296
  %r33 = call i32 @fptoui_f32_i32(float 2147483648.0)              ; 2147483648.0 -> 2147483648
  %k34 = icmp eq i32 %r33, -2147483648
  %r35 = call i32 @fptoui_f32_i32(float 0x3FECCCCCC0000000)        ; 0.9 -> 0
  %k36 = icmp eq i32 %r35, 0
  %r37 = call i32 @fptoui_f32_i32(float 1.5)                       ; 1.5 -> 1
  %k38 = icmp eq i32 %r37, 1
  %r39 = call i32 @fptoui_f32_i32(float -0.0)                      ; -0.0 -> 0
  %k40 = icmp eq i32 %r39, 0
  %r41 = call i32 @fptoui_f32_i32(float 3900000000.0)              ; 3900000000.0 -> 3900000000
  %k42 = icmp eq i32 %r41, -394967296
  %r43 = call i32 @fptoui_f32_i32(float 3221225472.0)              ; 3221225472.0 -> 3221225472
  %k44 = icmp eq i32 %r43, -1073741824
  %r45 = call i8 @fptoui_f32_i8(float 0x406FFCCCC0000000)          ; 255.9 -> 255
  %k46 = icmp eq i8 %r45, -1
  %r47 = call i8 @fptoui_f32_i8(float 128.5)                       ; 128.5 -> 128
  %k48 = icmp eq i8 %r47, -128
  %r49 = call i8 @fptoui_f32_i8(float 0.5)                         ; 0.5 -> 0
  %k50 = icmp eq i8 %r49, 0
  %r51 = call i16 @fptoui_f32_i16(float 65535.5)                   ; 65535.5 -> 65535
  %k52 = icmp eq i16 %r51, -1
  %r53 = call i16 @fptoui_f32_i16(float 32768.0)                   ; 32768.0 -> 32768
  %k54 = icmp eq i16 %r53, -32768
  %r55 = call i16 @fptoui_f32_i16(float 40000.75)                  ; 40000.75 -> 40000
  %k56 = icmp eq i16 %r55, -25536
  %r57 = call i32 @fptoui_f64_i32(double 4294967295.0)             ; 4294967295.0 -> 4294967295
  %k58 = icmp eq i32 %r57, -1
  %r59 = call i32 @fptoui_f64_i32(double 4294967295.5)             ; 4294967295.5 -> 4294967295
  %k60 = icmp eq i32 %r59, -1
  %r61 = call i32 @fptoui_f64_i32(double 2147483647.5)             ; 2147483647.5 -> 2147483647
  %k62 = icmp eq i32 %r61, 2147483647
  %r63 = call i32 @fptoui_f32_i32(float 4294967040.0)              ; 4294967040.0 -> 4294967040
  %k64 = icmp eq i32 %r63, -256
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

; Bit 6: fptoui to 64 bits (conv.u), including 2^63 and above (1e19,
; 1.8e19, the largest values below 2^64), which are in range for
; unsigned long long.
define internal i32 @check_fptoui_i64() noinline {
  %r1 = call i64 @fptoui_f64_i64(double 0x43E0000000000000)        ; 9223372036854775808.0 -> 9223372036854775808
  %k2 = icmp eq i64 %r1, -9223372036854775808
  %r3 = call i64 @fptoui_f64_i64(double 1.8e+19)                   ; 18000000000000000000.0 -> 18000000000000000000
  %k4 = icmp eq i64 %r3, -446744073709551616
  %r5 = call i64 @fptoui_f64_i64(double 1.0e+19)                   ; 10000000000000000000.0 -> 10000000000000000000
  %k6 = icmp eq i64 %r5, -8446744073709551616
  %r7 = call i64 @fptoui_f64_i64(double 0.5)                       ; 0.5 -> 0
  %k8 = icmp eq i64 %r7, 0
  %r9 = call i64 @fptoui_f64_i64(double 0x40C81CD6C8B43958)        ; 12345.678 -> 12345
  %k10 = icmp eq i64 %r9, 12345
  %r11 = call i64 @fptoui_f64_i64(double 0x43E8000000000000)       ; 13835058055282163712.0 -> 13835058055282163712
  %k12 = icmp eq i64 %r11, -4611686018427387904
  %r13 = call i64 @fptoui_f32_i64(float 0x43E0000000000000)        ; 9223372036854775808.0 -> 9223372036854775808
  %k14 = icmp eq i64 %r13, -9223372036854775808
  %r15 = call i64 @fptoui_f32_i64(float 0x43EF399B20000000)        ; 18000000404716257280.0 -> 18000000404716257280
  %k16 = icmp eq i64 %r15, -446743668993294336
  %r17 = call i64 @fptoui_f32_i64(float 0x43E158E460000000)        ; 9999999980506447872.0 -> 9999999980506447872
  %k18 = icmp eq i64 %r17, -8446744093203103744
  %r19 = call i64 @fptoui_f32_i64(float 0.5)                       ; 0.5 -> 0
  %k20 = icmp eq i64 %r19, 0
  %r21 = call i64 @fptoui_f32_i64(float 12345.677734375)           ; 12345.678 -> 12345
  %k22 = icmp eq i64 %r21, 12345
  %r23 = call i64 @fptoui_f32_i64(float 0x43E8000000000000)        ; 13835058055282163712.0 -> 13835058055282163712
  %k24 = icmp eq i64 %r23, -4611686018427387904
  %r25 = call i64 @fptoui_f64_i64(double 0x43EFFFFFFFFFFFFF)       ; 18446744073709549568.0 -> 18446744073709549568
  %k26 = icmp eq i64 %r25, -2048
  %r27 = call i64 @fptoui_f64_i64(double 0x43DFFFFFFFFFFFFF)       ; 9223372036854774784.0 -> 9223372036854774784
  %k28 = icmp eq i64 %r27, 9223372036854774784
  %r29 = call i64 @fptoui_f64_i64(double 9007199254740994.0)       ; 9007199254740994.0 -> 9007199254740994
  %k30 = icmp eq i64 %r29, 9007199254740994
  %r31 = call i64 @fptoui_f64_i64(double 0x43E0000000000001)       ; 9223372036854777856.0 -> 9223372036854777856
  %k32 = icmp eq i64 %r31, -9223372036854773760
  %r33 = call i64 @fptoui_f32_i64(float 0x43EFFFFFE0000000)        ; 18446742974197923840.0 -> 18446742974197923840
  %k34 = icmp eq i64 %r33, -1099511627776
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
  %bad = xor i1 %all16, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 7: fpext (exact) and fptrunc (round to nearest even, overflow to
; inf, gradual underflow, signed zeros, NaN).
define internal i32 @check_fp_fp() noinline {
  %r1 = call double @fpext_f32_f64(float 0x3FB99999A0000000)       ; 0.1 -> 0.10000000149011612
  %k2 = fcmp oeq double %r1, 0x3FB99999A0000000
  %r3 = call double @fpext_f32_f64(float 0x47EFFFFFE0000000)       ; 3.4028235e+38 -> 3.4028234663852886e+38
  %k4 = fcmp oeq double %r3, 0x47EFFFFFE0000000
  %r5 = call double @fpext_f32_f64(float 0x36A0000000000000)       ; 1e-45 -> 1.401298464324817e-45
  %k6 = fcmp oeq double %r5, 0x36A0000000000000
  %r7 = call double @fpext_f32_f64(float -0.0)                     ; -0.0 -> -0.0
  %k8 = fcmp oeq double %r7, -0.0
  %q9 = fdiv double 1.0, %r7
  %k10 = fcmp olt double %q9, 0.0
  %r11 = call double @fpext_f32_f64(float 0x7FF0000000000000)      ; inf -> inf
  %k12 = fcmp oeq double %r11, 0x7FF0000000000000
  %r13 = call double @fpext_f32_f64(float 0xFFF0000000000000)      ; -inf -> -inf
  %k14 = fcmp oeq double %r13, 0xFFF0000000000000
  %r15 = call double @fpext_f32_f64(float 0x7FF8000000000000)      ; NaN -> NaN
  %k16 = fcmp uno double %r15, 0.0
  %r17 = call double @fpext_f32_f64(float 0xB7A16C2000000000)      ; -1e-40 -> -9.99994610111476e-41
  %k18 = fcmp oeq double %r17, 0xB7A16C2000000000
  %r19 = call float @fptrunc_f64_f32(double 0x3FB999999999999A)    ; 0.1 -> 0.1
  %k20 = fcmp oeq float %r19, 0x3FB99999A0000000
  %r21 = call float @fptrunc_f64_f32(double 0x3FF0000010000000)    ; 1.0000000596046448 -> 1.0
  %k22 = fcmp oeq float %r21, 1.0
  %r23 = call float @fptrunc_f64_f32(double 0x3FF0000030000000)    ; 1.0000001788139343 -> 1.0000002
  %k24 = fcmp oeq float %r23, 0x3FF0000040000000
  %r25 = call float @fptrunc_f64_f32(double 0x3FF0000010000001)    ; 1.000000059604645 -> 1.0000001
  %k26 = fcmp oeq float %r25, 0x3FF0000020000000
  %r27 = call float @fptrunc_f64_f32(double 0x48078287F49C4A1D)    ; 1e+39 -> inf
  %k28 = fcmp oeq float %r27, 0x7FF0000000000000
  %r29 = call float @fptrunc_f64_f32(double 0xC8078287F49C4A1D)    ; -1e+39 -> -inf
  %k30 = fcmp oeq float %r29, 0xFFF0000000000000
  %r31 = call float @fptrunc_f64_f32(double 0x47EFFFFFE54DAFF8)    ; 3.4028235e+38 -> 3.4028235e+38
  %k32 = fcmp oeq float %r31, 0x47EFFFFFE0000000
  %r33 = call float @fptrunc_f64_f32(double 0x47EFFFFFF0000000)    ; 3.4028235677973366e+38 -> inf
  %k34 = fcmp oeq float %r33, 0x7FF0000000000000
  %r35 = call float @fptrunc_f64_f32(double 0x366244CE242C5561)    ; 1e-46 -> 0.0
  %k36 = fcmp oeq float %r35, 0.0
  %q37 = fdiv float 1.0, %r35
  %k38 = fcmp ogt float %q37, 0.0
  %r39 = call float @fptrunc_f64_f32(double 0xB66244CE242C5561)    ; -1e-46 -> -0.0
  %k40 = fcmp oeq float %r39, -0.0
  %q41 = fdiv float 1.0, %r39
  %k42 = fcmp olt float %q41, 0.0
  %r43 = call float @fptrunc_f64_f32(double 0x3696D601AD376AB9)    ; 1e-45 -> 1e-45
  %k44 = fcmp oeq float %r43, 0x36A0000000000000
  %r45 = call float @fptrunc_f64_f32(double 0x3690000000000000)    ; 7.006492321624085e-46 -> 0.0
  %k46 = fcmp oeq float %r45, 0.0
  %q47 = fdiv float 1.0, %r45
  %k48 = fcmp ogt float %q47, 0.0
  %r49 = call float @fptrunc_f64_f32(double 0x36A8000000000000)    ; 2.1019476964872256e-45 -> 3e-45
  %k50 = fcmp oeq float %r49, 0x36B0000000000000
  %r51 = call float @fptrunc_f64_f32(double 0x7FF8000000000000)    ; NaN -> NaN
  %k52 = fcmp uno float %r51, 0.0
  %r53 = call float @fptrunc_f64_f32(double -0.0)                  ; -0.0 -> -0.0
  %k54 = fcmp oeq float %r53, -0.0
  %q55 = fdiv float 1.0, %r53
  %k56 = fcmp olt float %q55, 0.0
  %r57 = call float @fptrunc_f64_f32(double 16777217.0)            ; 16777217.0 -> 16777216.0
  %k58 = fcmp oeq float %r57, 16777216.0
  %r59 = call float @fptrunc_f64_f32(double -16777219.0)           ; -16777219.0 -> -16777220.0
  %k60 = fcmp oeq float %r59, -16777220.0
  %r61 = call float @fptrunc_f64_f32(double 0x37A16C262777579C)    ; 1e-40 -> 1e-40
  %k62 = fcmp oeq float %r61, 0x37A16C2000000000
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
  %bad = xor i1 %all30, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

define i32 @main() {
  %b0 = call i32 @check_sitofp_i32()
  %b1 = call i32 @check_sitofp_i64()
  %b1.1 = call i32 @check_sitofp_i64_folded()
  %b1.or1 = or i32 %b1, %b1.1
  %s1 = shl i32 %b1.or1, 1
  %b2 = call i32 @check_uitofp_i32()
  %s2 = shl i32 %b2, 2
  %b3 = call i32 @check_fptosi_i32()
  %s3 = shl i32 %b3, 3
  %b4 = call i32 @check_fptosi_i64()
  %s4 = shl i32 %b4, 4
  %b5 = call i32 @check_fptoui_i32()
  %s5 = shl i32 %b5, 5
  %b6 = call i32 @check_fptoui_i64()
  %s6 = shl i32 %b6, 6
  %b7 = call i32 @check_fp_fp()
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
