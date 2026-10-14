; int64 -> float32 of a value whose sign the backend knows, at magnitudes of
; 2^53 and more (Milestone 5; plan section 12, row 'M5.a extended (int64 to
; float32 under JIT constant folding)'): a non-negative value from lshr
; converted with uitofp, and a value with ten sign bits from ashr by 9
; converted with sitofp. Such values still need the round-to-odd step before
; conv.r4, because only an int64 with eleven or more sign bits is always
; exact in float64. Every case rounds wrongly when it is converted to float64
; first, which is what the .NET Framework 4.8 JIT does when it constant-folds
; conv.r4 of an int64 constant. The folded checks convert in the check
; function, on a value from the identity function @id_i64 that the JIT
; inlines (as in u64-to-fp-rounding.ll); the same cases also go through
; noinline helpers that take x as an argument. Each noinline check function
; returns 1 when a result is wrong; main ORs one bit per check into the exit
; status (8 bits, D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

define i64 @id_i64(i64 %x) noinline {
  ret i64 %x
}

define float @u64_half_to_f32(i64 %x) noinline {
  %h = lshr i64 %x, 1
  %r = uitofp i64 %h to float
  ret float %r
}

define float @s64_ashr9_to_f32(i64 %x) noinline {
  %h = ashr i64 %x, 9
  %r = sitofp i64 %h to float
  ret float %r
}

; Bit 0: uitofp (lshr x, 1) to float in this method, on x from the identity
; function @id_i64 (the JIT inlines it, so the shift and the conversion see a
; constant).
define internal i32 @check_lshr1_folded() noinline {
  %r1 = call i64 @id_i64(i64 2305843146652647426)                  ; 0x2000002000000002
  %h2 = lshr i64 %r1, 1                                            ; 1152921573326323713
  %f3 = uitofp i64 %h2 to float                                    ; -> 1152921642045800448.0 (0x5D800001), not 1152921504606846976.0
  %k4 = fcmp oeq float %f3, 0x43B0000020000000
  %r5 = call i64 @id_i64(i64 2305843146652647427)                  ; 0x2000002000000003
  %h6 = lshr i64 %r5, 1                                            ; 1152921573326323713
  %f7 = uitofp i64 %h6 to float                                    ; -> 1152921642045800448.0 (0x5D800001), not 1152921504606846976.0
  %k8 = fcmp oeq float %f7, 0x43B0000020000000
  %r9 = call i64 @id_i64(i64 -9223371487098961918)                 ; 0x8000008000000002
  %h10 = lshr i64 %r9, 1                                           ; 4611686293305294849
  %f11 = uitofp i64 %h10 to float                                  ; -> 4611686568183201792.0 (0x5E800001), not 4611686018427387904.0
  %k12 = fcmp oeq float %f11, 0x43D0000020000000
  %r13 = call i64 @id_i64(i64 -549755813889)                       ; 0xFFFFFF7FFFFFFFFF
  %h14 = lshr i64 %r13, 1                                          ; 9223371761976868863
  %f15 = uitofp i64 %h14 to float                                  ; -> 9223371487098961920.0 (0x5EFFFFFF), not 9223372036854775808.0
  %k16 = fcmp oeq float %f15, 0x43DFFFFFE0000000
  %all1 = and i1 %k4, %k8
  %all2 = and i1 %all1, %k12
  %all3 = and i1 %all2, %k16
  %bad = xor i1 %all3, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 1: sitofp (ashr x, 9) to float in this method, on x from @id_i64; the
; result has ten sign bits and a magnitude between 2^53 and 2^54.
define internal i32 @check_ashr9_folded() noinline {
  %r1 = call i64 @id_i64(i64 4611686293305295871)                  ; 0x40000040000003FF
  %h2 = ashr i64 %r1, 9                                            ; 9007199791611905
  %f3 = sitofp i64 %h2 to float                                    ; -> 9007200328482816.0 (0x5A000001), not 9007199254740992.0
  %k4 = fcmp oeq float %f3, 9007200328482816.0
  %r5 = call i64 @id_i64(i64 -4611686293305295353)                 ; 0xBFFFFFBFFFFFFE07
  %h6 = ashr i64 %r5, 9                                            ; -9007199791611905
  %f7 = sitofp i64 %h6 to float                                    ; -> -9007200328482816.0 (0xDA000001), not -9007199254740992.0
  %k8 = fcmp oeq float %f7, -9007200328482816.0
  %r9 = call i64 @id_i64(i64 9223371761976868863)                  ; 0x7FFFFFBFFFFFFFFF
  %h10 = ashr i64 %r9, 9                                           ; 18014397972611071
  %f11 = sitofp i64 %h10 to float                                  ; -> 18014397435740160.0 (0x5A7FFFFF), not 18014398509481984.0
  %k12 = fcmp oeq float %f11, 1.801439743574016e+16
  %r13 = call i64 @id_i64(i64 -9223371761976868345)                ; 0x8000004000000207
  %h14 = ashr i64 %r13, 9                                          ; -18014397972611071
  %f15 = sitofp i64 %h14 to float                                  ; -> -18014397435740160.0 (0xDA7FFFFF), not -18014398509481984.0
  %k16 = fcmp oeq float %f15, -1.801439743574016e+16
  %all1 = and i1 %k4, %k8
  %all2 = and i1 %all1, %k12
  %all3 = and i1 %all2, %k16
  %bad = xor i1 %all3, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 2: the cases of bit 0 through the helper @u64_half_to_f32.
define internal i32 @check_lshr1_helper() noinline {
  %r1 = call float @u64_half_to_f32(i64 2305843146652647426)       ; 0x2000002000000002 -> 1152921642045800448.0 (0x5D800001)
  %k2 = fcmp oeq float %r1, 0x43B0000020000000
  %r3 = call float @u64_half_to_f32(i64 2305843146652647427)       ; 0x2000002000000003 -> 1152921642045800448.0 (0x5D800001)
  %k4 = fcmp oeq float %r3, 0x43B0000020000000
  %r5 = call float @u64_half_to_f32(i64 -9223371487098961918)      ; 0x8000008000000002 -> 4611686568183201792.0 (0x5E800001)
  %k6 = fcmp oeq float %r5, 0x43D0000020000000
  %r7 = call float @u64_half_to_f32(i64 -549755813889)             ; 0xFFFFFF7FFFFFFFFF -> 9223371487098961920.0 (0x5EFFFFFF)
  %k8 = fcmp oeq float %r7, 0x43DFFFFFE0000000
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %bad = xor i1 %all3, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 3: the cases of bit 1 through the helper @s64_ashr9_to_f32.
define internal i32 @check_ashr9_helper() noinline {
  %r1 = call float @s64_ashr9_to_f32(i64 4611686293305295871)      ; 0x40000040000003FF -> 9007200328482816.0 (0x5A000001)
  %k2 = fcmp oeq float %r1, 9007200328482816.0
  %r3 = call float @s64_ashr9_to_f32(i64 -4611686293305295353)     ; 0xBFFFFFBFFFFFFE07 -> -9007200328482816.0 (0xDA000001)
  %k4 = fcmp oeq float %r3, -9007200328482816.0
  %r5 = call float @s64_ashr9_to_f32(i64 9223371761976868863)      ; 0x7FFFFFBFFFFFFFFF -> 18014397435740160.0 (0x5A7FFFFF)
  %k6 = fcmp oeq float %r5, 1.801439743574016e+16
  %r7 = call float @s64_ashr9_to_f32(i64 -9223371761976868345)     ; 0x8000004000000207 -> -18014397435740160.0 (0xDA7FFFFF)
  %k8 = fcmp oeq float %r7, -1.801439743574016e+16
  %all1 = and i1 %k2, %k4
  %all2 = and i1 %all1, %k6
  %all3 = and i1 %all2, %k8
  %bad = xor i1 %all3, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

; Bit 4: uitofp (lshr x, 10) to float in this method, on x from @id_i64: a
; non-negative value below 2^54.
define internal i32 @check_lshr10_folded() noinline {
  %r1 = call i64 @id_i64(i64 -9223371487098959873)                 ; 0x80000080000007FF
  %h2 = lshr i64 %r1, 10                                           ; 9007199791611905
  %f3 = uitofp i64 %h2 to float                                    ; -> 9007200328482816.0 (0x5A000001), not 9007199254740992.0
  %k4 = fcmp oeq float %f3, 9007200328482816.0
  %r5 = call i64 @id_i64(i64 -549755813889)                        ; 0xFFFFFF7FFFFFFFFF
  %h6 = lshr i64 %r5, 10                                           ; 18014397972611071
  %f7 = uitofp i64 %h6 to float                                    ; -> 18014397435740160.0 (0x5A7FFFFF), not 18014398509481984.0
  %k8 = fcmp oeq float %f7, 1.801439743574016e+16
  %r9 = call i64 @id_i64(i64 -9223369288075705339)                 ; 0x8000028000000405
  %h10 = lshr i64 %r9, 10                                          ; 9007201939095553
  %f11 = uitofp i64 %h10 to float                                  ; -> 9007202475966464.0 (0x5A000003), not 9007201402224640.0
  %k12 = fcmp oeq float %f11, 9007202475966464.0
  %all1 = and i1 %k4, %k8
  %all2 = and i1 %all1, %k12
  %bad = xor i1 %all2, true
  %ret = zext i1 %bad to i32
  ret i32 %ret
}

define i32 @main() {
  %b0 = call i32 @check_lshr1_folded()
  %b1 = call i32 @check_ashr9_folded()
  %s1 = shl i32 %b1, 1
  %b2 = call i32 @check_lshr1_helper()
  %s2 = shl i32 %b2, 2
  %b3 = call i32 @check_ashr9_helper()
  %s3 = shl i32 %b3, 3
  %b4 = call i32 @check_lshr10_folded()
  %s4 = shl i32 %b4, 4
  %m1 = or i32 %b0, %s1
  %m2 = or i32 %m1, %s2
  %m3 = or i32 %m2, %s3
  %m4 = or i32 %m3, %s4
  ret i32 %m4
}
