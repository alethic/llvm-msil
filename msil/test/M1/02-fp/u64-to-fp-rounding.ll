; uint64 -> float64 and float32 (Milestone 5, D17): every result must be the
; correctly rounded value on every runtime. conv.r.un rounds twice on .NET 8
; and .NET Framework (to float32 also on .NET 9; cil-runtime e05), so the
; backend never uses it for 64-bit inputs; the D17 sequence (halve with a
; sticky bit, one signed conversion, multiply by 1 or 2) rounds once, with no
; select and no frame. The cases tell single from double rounding:
; 2^63 + 2^39 + 1 (float32: 2^63 + 2^40, not 2^63), 2^63 + 2^10 + 1 (float64:
; 2^63 + 2^11, not 2^63), 2^64 - 1 and 2^53 + 1, plus halfway cases around 2^63
; and 2^64, values below 2^63, round trips through fptoui, and conversions of
; computed values. The float32 hard cases are also converted in the calling
; method, on a value from an identity function that the JIT inlines, where the
; .NET Framework JIT constant-folds conv.r4 through float64. Each noinline
; check function returns 1 when a result is wrong; main ORs one bit per check
; into the exit status (8 bits, D37; check_hard_f32 and check_hard_f32_folded
; share bit 1): [exit 0] means every check passed.
; RUN: %{msil_ll}

define double @u64_to_f64(i64 %x) noinline {
  %r = uitofp i64 %x to double
  ret double %r
}

define float @u64_to_f32(i64 %x) noinline {
  %r = uitofp i64 %x to float
  ret float %r
}

define i64 @f64_to_u64(double %x) noinline {
  %r = fptoui double %x to i64
  ret i64 %r
}

define i64 @f32_to_u64(float %x) noinline {
  %r = fptoui float %x to i64
  ret i64 %r
}

define double @u64_sum_to_f64(i64 %a, i64 %b) noinline {
  %x = add i64 %a, %b
  %r = uitofp i64 %x to double
  ret double %r
}

define double @u64_add_f64(i64 %a, i64 %b) noinline {
  %x = uitofp i64 %a to double
  %y = uitofp i64 %b to double
  %r = fadd double %x, %y
  ret double %r
}

define double @u32_zext_to_f64(i32 %a) noinline {
  %x = zext i32 %a to i64
  %r = uitofp i64 %x to double
  ret double %r
}

define float @u64_sum_to_f32(i64 %a, i64 %b) noinline {
  %x = add i64 %a, %b
  %r = uitofp i64 %x to float
  ret float %r
}

define float @u64_add_f32(i64 %a, i64 %b) noinline {
  %x = uitofp i64 %a to float
  %y = uitofp i64 %b to float
  %r = fadd float %x, %y
  ret float %r
}

define float @u32_zext_to_f32(i32 %a) noinline {
  %x = zext i32 %a to i64
  %r = uitofp i64 %x to float
  ret float %r
}

define i64 @id_i64(i64 %x) noinline {
  ret i64 %x
}

define double @u64_to_f32_to_f64(i64 %x) noinline {
  %f = uitofp i64 %x to float
  %r = fpext float %f to double
  ret double %r
}

; Bit 0: the hard cases to float64, where conv.r.un rounds twice on
; .NET 8 and .NET Framework.
define internal i32 @check_hard_f64() noinline {
  %r1 = call double @u64_to_f64(i64 -9223371487098961919)          ; 0x8000008000000001 -> 9223372586610589696.0 (2^63 + 2^39 + 1)
  %k2 = fcmp oeq double %r1, 0x43E0000010000000
  %r3 = call double @u64_to_f64(i64 -9223372036854774783)          ; 0x8000000000000401 -> 9223372036854777856.0 (2^63 + 2^10 + 1)
  %k4 = fcmp oeq double %r3, 0x43E0000000000001
  %r5 = call double @u64_to_f64(i64 -1)                            ; 0xFFFFFFFFFFFFFFFF -> 18446744073709551616.0 (2^64 - 1)
  %k6 = fcmp oeq double %r5, 0x43F0000000000000
  %r7 = call double @u64_to_f64(i64 9007199254740993)              ; 0x0020000000000001 -> 9007199254740992.0 (2^53 + 1)
  %k8 = fcmp oeq double %r7, 9007199254740992.0
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %bad = xor i1 %all3, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 1: the hard cases to float32, where conv.r.un rounds twice on
; .NET 8 and .NET Framework.
define internal i32 @check_hard_f32() noinline {
  %r1 = call float @u64_to_f32(i64 -9223371487098961919)           ; 0x8000008000000001 -> 9223373136366403584.0 (2^63 + 2^39 + 1)
  %k2 = fcmp oeq float %r1, 0x43E0000020000000
  %r3 = call float @u64_to_f32(i64 -9223372036854774783)           ; 0x8000000000000401 -> 9223372036854775808.0 (2^63 + 2^10 + 1)
  %k4 = fcmp oeq float %r3, 0x43E0000000000000
  %r5 = call float @u64_to_f32(i64 -1)                             ; 0xFFFFFFFFFFFFFFFF -> 18446744073709551616.0 (2^64 - 1)
  %k6 = fcmp oeq float %r5, 0x43F0000000000000
  %r7 = call float @u64_to_f32(i64 9007199254740993)               ; 0x0020000000000001 -> 9007199254740992.0 (2^53 + 1)
  %k8 = fcmp oeq float %r7, 9007199254740992.0
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %bad = xor i1 %all3, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 1 (with check_hard_f32): float32 cases converted in this method,
; on a value from the identity function @id_i64. The JIT inlines @id_i64,
; so conv.r4 sees a constant, and the .NET Framework 4.8 JIT folds conv.r4
; of a constant int64 as (float32)(float64)x, which rounds twice. Each
; case rounds wrongly that way, so it is correct only with the
; round-to-odd step before conv.r4 (plan section 12, row 'M5.a extended
; (int64 to float32 under JIT constant folding)').
define internal i32 @check_hard_f32_folded() noinline {
  %r1 = call i64 @id_i64(i64 -9223371487098961919)                 ; 0x8000008000000001
  %r2 = uitofp i64 %r1 to float                                    ; -> 9223373136366403584.0 (0x5F000001)
  %k3 = fcmp oeq float %r2, 0x43E0000020000000
  %r4 = call i64 @id_i64(i64 -549755813889)                        ; 0xFFFFFF7FFFFFFFFF
  %r5 = uitofp i64 %r4 to float                                    ; -> 18446742974197923840.0 (0x5F7FFFFF)
  %k6 = fcmp oeq float %r5, 0x43EFFFFFE0000000
  %r7 = call i64 @id_i64(i64 -9223370387587334145)                 ; 0x8000017FFFFFFFFF
  %r8 = uitofp i64 %r7 to float                                    ; -> 9223373136366403584.0 (0x5F000001)
  %k9 = fcmp oeq float %r8, 0x43E0000020000000
  %r10 = call i64 @id_i64(i64 1152921573326323713)                 ; 0x1000001000000001
  %r11 = uitofp i64 %r10 to float                                  ; -> 1152921642045800448.0 (0x5D800001)
  %k12 = fcmp oeq float %r11, 0x43B0000020000000
  %all1 = and i1 %k3, %k6
  %all2 = and i1 %all1, %k9
  %all3 = and i1 %all2, %k12
  %bad = xor i1 %all3, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 2: float64 rounding at and above 2^63 (one ulp is 2048 there):
; exact halfway cases round to even, one above rounds up.
define internal i32 @check_ties_f64() noinline {
  %r1 = call double @u64_to_f64(i64 -9223372036854775808)          ; 0x8000000000000000 -> 9223372036854775808.0
  %k2 = fcmp oeq double %r1, 0x43E0000000000000
  %r3 = call double @u64_to_f64(i64 -9223372036854775807)          ; 0x8000000000000001 -> 9223372036854775808.0
  %k4 = fcmp oeq double %r3, 0x43E0000000000000
  %r5 = call double @u64_to_f64(i64 -9223372036854774784)          ; 0x8000000000000400 -> 9223372036854775808.0
  %k6 = fcmp oeq double %r5, 0x43E0000000000000
  %r7 = call double @u64_to_f64(i64 -9223372036854772737)          ; 0x8000000000000BFF -> 9223372036854777856.0
  %k8 = fcmp oeq double %r7, 0x43E0000000000001
  %r9 = call double @u64_to_f64(i64 -9223372036854772736)          ; 0x8000000000000C00 -> 9223372036854779904.0
  %k10 = fcmp oeq double %r9, 0x43E0000000000002
  %r11 = call double @u64_to_f64(i64 -9223372036854772735)         ; 0x8000000000000C01 -> 9223372036854779904.0
  %k12 = fcmp oeq double %r11, 0x43E0000000000002
  %r13 = call double @u64_to_f64(i64 -2048)                        ; 0xFFFFFFFFFFFFF800 -> 18446744073709549568.0
  %k14 = fcmp oeq double %r13, 0x43EFFFFFFFFFFFFF
  %r15 = call double @u64_to_f64(i64 -1025)                        ; 0xFFFFFFFFFFFFFBFF -> 18446744073709549568.0
  %k16 = fcmp oeq double %r15, 0x43EFFFFFFFFFFFFF
  %r17 = call double @u64_to_f64(i64 -1024)                        ; 0xFFFFFFFFFFFFFC00 -> 18446744073709551616.0
  %k18 = fcmp oeq double %r17, 0x43F0000000000000
  %r19 = call double @u64_to_f64(i64 -4611686018427386368)         ; 0xC000000000000600 -> 13835058055282165760.0
  %k20 = fcmp oeq double %r19, 0x43E8000000000001
  %r21 = call double @u64_to_f64(i64 -7911603569559876113)         ; 0x9234567890ABCDEF -> 10535140504149676032.0
  %k22 = fcmp oeq double %r21, 0x43E2468ACF12157A
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

; Bit 3: float32 rounding at and above 2^63 (one ulp is 2^40 there):
; exact halfway cases round to even, one above rounds up.
define internal i32 @check_ties_f32() noinline {
  %r1 = call float @u64_to_f32(i64 -9223372036854775808)           ; 0x8000000000000000 -> 9223372036854775808.0
  %k2 = fcmp oeq float %r1, 0x43E0000000000000
  %r3 = call float @u64_to_f32(i64 -9223372036854775807)           ; 0x8000000000000001 -> 9223372036854775808.0
  %k4 = fcmp oeq float %r3, 0x43E0000000000000
  %r5 = call float @u64_to_f32(i64 -9223371487098961920)           ; 0x8000008000000000 -> 9223372036854775808.0
  %k6 = fcmp oeq float %r5, 0x43E0000000000000
  %r7 = call float @u64_to_f32(i64 -9223370387587334144)           ; 0x8000018000000000 -> 9223374235878031360.0
  %k8 = fcmp oeq float %r7, 0x43E0000040000000
  %r9 = call float @u64_to_f32(i64 -9223370387587334145)           ; 0x8000017FFFFFFFFF -> 9223373136366403584.0
  %k10 = fcmp oeq float %r9, 0x43E0000020000000
  %r11 = call float @u64_to_f32(i64 -549755813889)                 ; 0xFFFFFF7FFFFFFFFF -> 18446742974197923840.0
  %k12 = fcmp oeq float %r11, 0x43EFFFFFE0000000
  %r13 = call float @u64_to_f32(i64 -549755813888)                 ; 0xFFFFFF8000000000 -> 18446744073709551616.0
  %k14 = fcmp oeq float %r13, 0x43F0000000000000
  %r15 = call float @u64_to_f32(i64 -1099511627776)                ; 0xFFFFFF0000000000 -> 18446742974197923840.0
  %k16 = fcmp oeq float %r15, 0x43EFFFFFE0000000
  %r17 = call float @u64_to_f32(i64 -4611685743549480959)          ; 0xC000004000000001 -> 13835058055282163712.0
  %k18 = fcmp oeq float %r17, 0x43E8000000000000
  %r19 = call float @u64_to_f32(i64 -7911603569559876113)          ; 0x9234567890ABCDEF -> 10535139986326421504.0
  %k20 = fcmp oeq float %r19, 0x43E2468AC0000000
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

; Bit 4: values below 2^63, converted with one signed conversion:
; 0, 1, 2^63 - 1, and the float32 single-rounding case 2^60 + 2^36 + 1.
define internal i32 @check_low_range() noinline {
  %r1 = call double @u64_to_f64(i64 0)                             ; 0x0000000000000000 -> 0.0
  %k2 = fcmp oeq double %r1, 0.0
  %q3 = fdiv double 1.0, %r1
  %k4 = fcmp ogt double %q3, 0.0
  %r5 = call double @u64_to_f64(i64 1)                             ; 0x0000000000000001 -> 1.0
  %k6 = fcmp oeq double %r5, 1.0
  %r7 = call double @u64_to_f64(i64 9223372036854775807)           ; 0x7FFFFFFFFFFFFFFF -> 9223372036854775808.0
  %k8 = fcmp oeq double %r7, 0x43E0000000000000
  %r9 = call double @u64_to_f64(i64 1152921573326323713)           ; 0x1000001000000001 -> 1152921573326323712.0
  %k10 = fcmp oeq double %r9, 0x43B0000010000000
  %r11 = call double @u64_to_f64(i64 9223372036854775296)          ; 0x7FFFFFFFFFFFFE00 -> 9223372036854775808.0
  %k12 = fcmp oeq double %r11, 0x43E0000000000000
  %r13 = call double @u64_to_f64(i64 9223371487098961919)          ; 0x7FFFFF7FFFFFFFFF -> 9223371487098961920.0
  %k14 = fcmp oeq double %r13, 0x43DFFFFFE0000000
  %r15 = call double @u64_to_f64(i64 12345678901234567)            ; 0x002BDC545D6B4B87 -> 12345678901234568.0
  %k16 = fcmp oeq double %r15, 1.2345678901234568e+16
  %r17 = call double @u64_to_f64(i64 4294967295)                   ; 0x00000000FFFFFFFF -> 4294967295.0
  %k18 = fcmp oeq double %r17, 4294967295.0
  %r19 = call float @u64_to_f32(i64 0)                             ; 0x0000000000000000 -> 0.0
  %k20 = fcmp oeq float %r19, 0.0
  %q21 = fdiv float 1.0, %r19
  %k22 = fcmp ogt float %q21, 0.0
  %r23 = call float @u64_to_f32(i64 1)                             ; 0x0000000000000001 -> 1.0
  %k24 = fcmp oeq float %r23, 1.0
  %r25 = call float @u64_to_f32(i64 9223372036854775807)           ; 0x7FFFFFFFFFFFFFFF -> 9223372036854775808.0
  %k26 = fcmp oeq float %r25, 0x43E0000000000000
  %r27 = call float @u64_to_f32(i64 1152921573326323713)           ; 0x1000001000000001 -> 1152921642045800448.0
  %k28 = fcmp oeq float %r27, 0x43B0000020000000
  %r29 = call float @u64_to_f32(i64 9223372036854775296)           ; 0x7FFFFFFFFFFFFE00 -> 9223372036854775808.0
  %k30 = fcmp oeq float %r29, 0x43E0000000000000
  %r31 = call float @u64_to_f32(i64 9223371487098961919)           ; 0x7FFFFF7FFFFFFFFF -> 9223371487098961920.0
  %k32 = fcmp oeq float %r31, 0x43DFFFFFE0000000
  %r33 = call float @u64_to_f32(i64 12345678901234567)             ; 0x002BDC545D6B4B87 -> 12345678407663616.0
  %k34 = fcmp oeq float %r33, 1.2345678407663616e+16
  %r35 = call float @u64_to_f32(i64 4294967295)                    ; 0x00000000FFFFFFFF -> 4294967296.0
  %k36 = fcmp oeq float %r35, 4294967296.0
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
  %bad = xor i1 %all17, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 5: uitofp then fptoui returns the rounded value as an integer.
define internal i32 @check_round_trip() noinline {
  %r1 = call double @u64_to_f64(i64 -9223372036854774783)          ; 0x8000000000000401 -> 9223372036854777856.0
  %r2 = call i64 @f64_to_u64(double %r1)                           ; 0x8000000000000800
  %k3 = icmp eq i64 %r2, -9223372036854773760
  %r4 = call double @u64_to_f64(i64 -2048)                         ; 0xFFFFFFFFFFFFF800 -> 18446744073709549568.0
  %r5 = call i64 @f64_to_u64(double %r4)                           ; 0xFFFFFFFFFFFFF800
  %k6 = icmp eq i64 %r5, -2048
  %r7 = call double @u64_to_f64(i64 -1025)                         ; 0xFFFFFFFFFFFFFBFF -> 18446744073709549568.0
  %r8 = call i64 @f64_to_u64(double %r7)                           ; 0xFFFFFFFFFFFFF800
  %k9 = icmp eq i64 %r8, -2048
  %r10 = call double @u64_to_f64(i64 -9223371487098961919)         ; 0x8000008000000001 -> 9223372586610589696.0
  %r11 = call i64 @f64_to_u64(double %r10)                         ; 0x8000008000000000
  %k12 = icmp eq i64 %r11, -9223371487098961920
  %r13 = call double @u64_to_f64(i64 -9223372036854775808)         ; 0x8000000000000000 -> 9223372036854775808.0
  %r14 = call i64 @f64_to_u64(double %r13)                         ; 0x8000000000000000
  %k15 = icmp eq i64 %r14, -9223372036854775808
  %r16 = call double @u64_to_f64(i64 3735928559)                   ; 0x00000000DEADBEEF -> 3735928559.0
  %r17 = call i64 @f64_to_u64(double %r16)                         ; 0x00000000DEADBEEF
  %k18 = icmp eq i64 %r17, 3735928559
  %r19 = call float @u64_to_f32(i64 -9223371487098961919)          ; 0x8000008000000001 -> 9223373136366403584.0
  %r20 = call i64 @f32_to_u64(float %r19)                          ; 0x8000010000000000
  %k21 = icmp eq i64 %r20, -9223370937343148032
  %r22 = call float @u64_to_f32(i64 -1099511627776)                ; 0xFFFFFF0000000000 -> 18446742974197923840.0
  %r23 = call i64 @f32_to_u64(float %r22)                          ; 0xFFFFFF0000000000
  %k24 = icmp eq i64 %r23, -1099511627776
  %r25 = call float @u64_to_f32(i64 -549755813889)                 ; 0xFFFFFF7FFFFFFFFF -> 18446742974197923840.0
  %r26 = call i64 @f32_to_u64(float %r25)                          ; 0xFFFFFF0000000000
  %k27 = icmp eq i64 %r26, -1099511627776
  %r28 = call float @u64_to_f32(i64 -9223372036854774783)          ; 0x8000000000000401 -> 9223372036854775808.0
  %r29 = call i64 @f32_to_u64(float %r28)                          ; 0x8000000000000000
  %k30 = icmp eq i64 %r29, -9223372036854775808
  %r31 = call float @u64_to_f32(i64 -9223372036854775808)          ; 0x8000000000000000 -> 9223372036854775808.0
  %r32 = call i64 @f32_to_u64(float %r31)                          ; 0x8000000000000000
  %k33 = icmp eq i64 %r32, -9223372036854775808
  %r34 = call float @u64_to_f32(i64 3735928559)                    ; 0x00000000DEADBEEF -> 3735928576.0
  %r35 = call i64 @f32_to_u64(float %r34)                          ; 0x00000000DEADBF00
  %k36 = icmp eq i64 %r35, 3735928576
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
  %bad = xor i1 %all11, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 6: a converted value computed in the same method (a + b), two
; conversions in one expression, and a zero-extended 32-bit value.
define internal i32 @check_computed() noinline {
  %r1 = call double @u64_sum_to_f64(i64 9223372036854775807, i64 549755813890) ; 0x8000008000000001 -> 9223372586610589696.0
  %k2 = fcmp oeq double %r1, 0x43E0000010000000
  %r3 = call double @u64_sum_to_f64(i64 -9223372036854775808, i64 1025) ; 0x8000000000000401 -> 9223372036854777856.0
  %k4 = fcmp oeq double %r3, 0x43E0000000000001
  %r5 = call double @u64_add_f64(i64 -9223372036854774783, i64 -1) ; = 27670116110564327424.0
  %k6 = fcmp oeq double %r5, 0x43F8000000000000
  %r7 = call double @u64_add_f64(i64 -9223371487098961919, i64 3)  ; = 9223372586610589696.0
  %k8 = fcmp oeq double %r7, 0x43E0000010000000
  %r9 = call double @u32_zext_to_f64(i32 -1)                       ; 0xFFFFFFFF -> 4294967295.0
  %k10 = fcmp oeq double %r9, 4294967295.0
  %r11 = call double @u32_zext_to_f64(i32 -2147483647)             ; 0x80000001 -> 2147483649.0
  %k12 = fcmp oeq double %r11, 2147483649.0
  %r13 = call float @u64_sum_to_f32(i64 9223372036854775807, i64 549755813890) ; 0x8000008000000001 -> 9223373136366403584.0
  %k14 = fcmp oeq float %r13, 0x43E0000020000000
  %r15 = call float @u64_sum_to_f32(i64 -9223372036854775808, i64 1025) ; 0x8000000000000401 -> 9223372036854775808.0
  %k16 = fcmp oeq float %r15, 0x43E0000000000000
  %r17 = call float @u64_add_f32(i64 -9223372036854774783, i64 -1) ; = 27670116110564327424.0
  %k18 = fcmp oeq float %r17, 0x43F8000000000000
  %r19 = call float @u64_add_f32(i64 -9223371487098961919, i64 3)  ; = 9223373136366403584.0
  %k20 = fcmp oeq float %r19, 0x43E0000020000000
  %r21 = call float @u32_zext_to_f32(i32 -1)                       ; 0xFFFFFFFF -> 4294967296.0
  %k22 = fcmp oeq float %r21, 4294967296.0
  %r23 = call float @u32_zext_to_f32(i32 -2147483647)              ; 0x80000001 -> 2147483648.0
  %k24 = fcmp oeq float %r23, 2147483648.0
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

; Bit 7: float32 results widened to float64 keep the float32 value
; (a double-rounded or unrounded value would differ).
define internal i32 @check_f32_widened() noinline {
  %r1 = call double @u64_to_f32_to_f64(i64 -9223371487098961919)   ; 0x8000008000000001 -> 9223373136366403584.0
  %k2 = fcmp oeq double %r1, 0x43E0000020000000
  %r3 = call double @u64_to_f32_to_f64(i64 1152921573326323713)    ; 0x1000001000000001 -> 1152921642045800448.0
  %k4 = fcmp oeq double %r3, 0x43B0000020000000
  %r5 = call double @u64_to_f32_to_f64(i64 -1)                     ; 0xFFFFFFFFFFFFFFFF -> 18446744073709551616.0
  %k6 = fcmp oeq double %r5, 0x43F0000000000000
  %r7 = call double @u64_to_f32_to_f64(i64 -9223370387587334144)   ; 0x8000018000000000 -> 9223374235878031360.0
  %k8 = fcmp oeq double %r7, 0x43E0000040000000
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %bad = xor i1 %all3, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

define i32 @main() {
  %b0 = call i32 @check_hard_f64()
  %b1 = call i32 @check_hard_f32()
  %b1.1 = call i32 @check_hard_f32_folded()
  %b1.or1 = or i32 %b1, %b1.1
  %s1 = shl i32 %b1.or1, 1
  %b2 = call i32 @check_ties_f64()
  %s2 = shl i32 %b2, 2
  %b3 = call i32 @check_ties_f32()
  %s3 = shl i32 %b3, 3
  %b4 = call i32 @check_low_range()
  %s4 = shl i32 %b4, 4
  %b5 = call i32 @check_round_trip()
  %s5 = shl i32 %b5, 5
  %b6 = call i32 @check_computed()
  %s6 = shl i32 %b6, 6
  %b7 = call i32 @check_f32_widened()
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
