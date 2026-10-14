; 64-bit integer arithmetic on native int (D2): wrap-around and carries across
; bit 32, products, bitwise operations with constants that need ldc.i8, shifts
; by constant and variable amounts, signed and unsigned compares whose answer
; a 32-bit compare would get wrong, and the i32 <-> i64 conversions (conv.i,
; conv.u, conv.i4). Each noinline check function gets its inputs as
; arguments, so llc cannot fold them even at -O2, and returns 1 when a result
; differs from the value C gives (in the comments); main ORs one bit per check
; into the exit status (8 bits, D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

; Bit 0: add and sub, carries and borrows across bit 32, INT64_MAX + 1.
define internal i32 @addsub(i64 %a, i64 %b, i64 %c, i64 %d) noinline {
  ; a = 0xFFFFFFFF, b = INT64_MAX, c = -0x100000000, d = 0x123456789
  %r0 = add i64 %a, 1                  ; 0x100000000
  %r1 = add i64 %b, 1                  ; INT64_MIN
  %r2 = add i64 %a, %c                 ; -1
  %r3 = sub i64 %c, %a                 ; -0x1FFFFFFFF
  %r4 = sub i64 0, %d                  ; -0x123456789
  %r5 = add i64 %d, 4294967296         ; 0x223456789
  %r6 = add i64 %d, -1                 ; 0x123456788
  %r7 = sub i64 %d, 4294967295         ; 0x2345678A
  %r8 = add i64 %a, %a                 ; 0x1FFFFFFFE
  %x0 = xor i64 %r0, 4294967296
  %x1 = xor i64 %r1, -9223372036854775808
  %x2 = xor i64 %r2, -1
  %x3 = xor i64 %r3, -8589934591
  %x4 = xor i64 %r4, -4886718345
  %x5 = xor i64 %r5, 9181685641
  %x6 = xor i64 %r6, 4886718344
  %x7 = xor i64 %r7, 591751050
  %x8 = xor i64 %r8, 8589934590
  %o1 = or i64 %x0, %x1
  %o2 = or i64 %o1, %x2
  %o3 = or i64 %o2, %x3
  %o4 = or i64 %o3, %x4
  %o5 = or i64 %o4, %x5
  %o6 = or i64 %o5, %x6
  %o7 = or i64 %o6, %x7
  %o8 = or i64 %o7, %x8
  %bad = icmp ne i64 %o8, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 1: mul keeps the low 64 bits of the product.
define internal i32 @mul(i64 %a, i64 %b, i64 %c, i64 %d) noinline {
  ; a = 0x100000001, b = -3, c = 0x100000000, d = 0x123456789ABCDEF
  %p0 = mul i64 %a, %a                 ; 0x200000001
  %p1 = mul i64 %c, %c                 ; 0
  %p2 = mul i64 %a, %b                 ; -12884901891
  %p3 = mul i64 %d, 65537              ; 5083102811422117359
  %p4 = mul i64 %d, %d                 ; -2547381487788710623
  %p5 = mul i64 %d, -1                 ; -81985529216486895
  %p6 = mul i64 %a, 4294967297         ; 0x200000001
  %x0 = xor i64 %p0, 8589934593
  %x1 = xor i64 %p1, 0
  %x2 = xor i64 %p2, -12884901891
  %x3 = xor i64 %p3, 5083102811422117359
  %x4 = xor i64 %p4, -2547381487788710623
  %x5 = xor i64 %p5, -81985529216486895
  %x6 = xor i64 %p6, 8589934593
  %o1 = or i64 %x0, %x1
  %o2 = or i64 %o1, %x2
  %o3 = or i64 %o2, %x3
  %o4 = or i64 %o3, %x4
  %o5 = or i64 %o4, %x5
  %o6 = or i64 %o5, %x6
  %bad = icmp ne i64 %o6, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 2: and, or, xor, not with registers and 64-bit immediates.
define internal i32 @bitwise(i64 %a, i64 %b, i64 %c) noinline {
  ; a = 0xF0F0F0F0F0F0F0F0, b = 0x0FF00FF00FF00FF0, c = 0x123456789ABCDEFF
  %r0 = and i64 %a, %b                 ; 0x00F000F000F000F0
  %r1 = or i64 %a, %b                  ; 0xFFF0FFF0FFF0FFF0
  %r2 = xor i64 %a, %b                 ; 0xFF00FF00FF00FF00
  %r3 = xor i64 %a, -1                 ; 0x0F0F0F0F0F0F0F0F
  %r4 = and i64 %a, 4294967295         ; 0xF0F0F0F0
  %r5 = and i64 %a, -4294967296        ; 0xF0F0F0F000000000
  %r6 = or i64 %b, -9223372036854775808 ; 0x8FF00FF00FF00FF0
  %r7 = xor i64 %a, 4294967295         ; 0xF0F0F0F00F0F0F0F
  %r8 = and i64 %c, -16                ; 0x123456789ABCDEF0
  %r9 = or i64 %b, 15                  ; 0x0FF00FF00FF00FFF
  %x0 = xor i64 %r0, 67555025218437360
  %x1 = xor i64 %r1, -4222189076152336
  %x2 = xor i64 %r2, -71777214294589696
  %x3 = xor i64 %r3, 1085102592571150095
  %x4 = xor i64 %r4, 4042322160
  %x5 = xor i64 %r5, -1085102596613472256
  %x6 = xor i64 %r6, -8074936608141340688
  %x7 = xor i64 %r7, -1085102596360827121
  %x8 = xor i64 %r8, 1311768467463790320
  %x9 = xor i64 %r9, 1148435428713435135
  %o1 = or i64 %x0, %x1
  %o2 = or i64 %o1, %x2
  %o3 = or i64 %o2, %x3
  %o4 = or i64 %o3, %x4
  %o5 = or i64 %o4, %x5
  %o6 = or i64 %o5, %x6
  %o7 = or i64 %o6, %x7
  %o8 = or i64 %o7, %x8
  %o9 = or i64 %o8, %x9
  %bad = icmp ne i64 %o9, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 3: shl, lshr and ashr by constant amounts, including 32 and 63.
define internal i32 @shift_const(i64 %a, i64 %b) noinline {
  ; a = 0x8000000000000001, b = 0x123456789ABCDEF0
  %s0 = shl i64 %a, 1                  ; 2
  %s1 = shl i64 %b, 4                  ; 0x23456789ABCDEF00
  %s2 = shl i64 %b, 32                 ; 0x9ABCDEF000000000
  %s3 = shl i64 %a, 63                 ; INT64_MIN
  %s4 = lshr i64 %a, 1                 ; 0x4000000000000000
  %s5 = lshr i64 %a, 63                ; 1
  %s6 = lshr i64 %a, 32                ; 0x80000000
  %s7 = lshr i64 %b, 36                ; 0x1234567
  %s8 = ashr i64 %a, 1                 ; 0xC000000000000000
  %s9 = ashr i64 %a, 63                ; -1
  %s10 = ashr i64 %a, 32               ; 0xFFFFFFFF80000000
  %s11 = ashr i64 %b, 60               ; 1
  %x0 = xor i64 %s0, 2
  %x1 = xor i64 %s1, 2541551405711093504
  %x2 = xor i64 %s2, -7296712173873528832
  %x3 = xor i64 %s3, -9223372036854775808
  %x4 = xor i64 %s4, 4611686018427387904
  %x5 = xor i64 %s5, 1
  %x6 = xor i64 %s6, 2147483648
  %x7 = xor i64 %s7, 19088743
  %x8 = xor i64 %s8, -4611686018427387904
  %x9 = xor i64 %s9, -1
  %x10 = xor i64 %s10, -2147483648
  %x11 = xor i64 %s11, 1
  %o1 = or i64 %x0, %x1
  %o2 = or i64 %o1, %x2
  %o3 = or i64 %o2, %x3
  %o4 = or i64 %o3, %x4
  %o5 = or i64 %o4, %x5
  %o6 = or i64 %o5, %x6
  %o7 = or i64 %o6, %x7
  %o8 = or i64 %o7, %x8
  %o9 = or i64 %o8, %x9
  %o10 = or i64 %o9, %x10
  %o11 = or i64 %o10, %x11
  %bad = icmp ne i64 %o11, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 4: shifts by variable i64 amounts (the amount becomes int32).
define internal i32 @shift_var(i64 %a, i64 %b, i64 %n0, i64 %n1, i64 %n31, i64 %n32, i64 %n33, i64 %n63) noinline {
  ; a = 0x8000000000000001, b = 0x123456789ABCDEF0
  %s0 = shl i64 %b, %n32               ; 0x9ABCDEF000000000
  %s1 = shl i64 %a, %n63               ; INT64_MIN
  %s2 = lshr i64 %a, %n33              ; 0x40000000
  %s3 = lshr i64 %b, %n31              ; 610839793
  %s4 = ashr i64 %a, %n32              ; 0xFFFFFFFF80000000
  %s5 = ashr i64 %a, %n63              ; -1
  %s6 = shl i64 %b, %n0                ; b
  %s7 = lshr i64 %a, %n1               ; 0x4000000000000000
  %s8 = ashr i64 %b, %n33              ; 152709948
  %x0 = xor i64 %s0, -7296712173873528832
  %x1 = xor i64 %s1, -9223372036854775808
  %x2 = xor i64 %s2, 1073741824
  %x3 = xor i64 %s3, 610839793
  %x4 = xor i64 %s4, -2147483648
  %x5 = xor i64 %s5, -1
  %x6 = xor i64 %s6, 1311768467463790320
  %x7 = xor i64 %s7, 4611686018427387904
  %x8 = xor i64 %s8, 152709948
  %o1 = or i64 %x0, %x1
  %o2 = or i64 %o1, %x2
  %o3 = or i64 %o2, %x3
  %o4 = or i64 %o3, %x4
  %o5 = or i64 %o4, %x5
  %o6 = or i64 %o5, %x6
  %o7 = or i64 %o6, %x7
  %o8 = or i64 %o7, %x8
  %bad = icmp ne i64 %o8, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 5: signed compares (big = 0x100000000 and low = 0xFFFFFFFF: compared
; as int32 they would order the other way).
define internal i32 @cmp_signed(i64 %m1, i64 %one, i64 %min, i64 %max, i64 %big, i64 %low) noinline {
  %c0 = icmp slt i64 %m1, %one         ; 1
  %c1 = icmp sgt i64 %m1, %one         ; 0
  %c2 = icmp slt i64 %min, %max        ; 1
  %c3 = icmp sge i64 %min, %max        ; 0
  %c4 = icmp sgt i64 %big, %low        ; 1
  %c5 = icmp slt i64 %big, %low        ; 0
  %c6 = icmp sle i64 %low, %big        ; 1
  %c7 = icmp sge i64 %max, %min        ; 1
  %c8 = icmp slt i64 %m1, 0            ; 1
  %c9 = icmp sgt i64 %big, 4294967295  ; 1
  %c10 = icmp sle i64 %min, -9223372036854775807 ; 1
  %c11 = icmp sgt i64 %low, -1         ; 1
  %z0 = zext i1 %c0 to i32
  %z1 = zext i1 %c1 to i32
  %z2 = zext i1 %c2 to i32
  %z3 = zext i1 %c3 to i32
  %z4 = zext i1 %c4 to i32
  %z5 = zext i1 %c5 to i32
  %z6 = zext i1 %c6 to i32
  %z7 = zext i1 %c7 to i32
  %z8 = zext i1 %c8 to i32
  %z9 = zext i1 %c9 to i32
  %z10 = zext i1 %c10 to i32
  %z11 = zext i1 %c11 to i32
  %w1 = shl i32 %z1, 1
  %w2 = shl i32 %z2, 2
  %w3 = shl i32 %z3, 3
  %w4 = shl i32 %z4, 4
  %w5 = shl i32 %z5, 5
  %w6 = shl i32 %z6, 6
  %w7 = shl i32 %z7, 7
  %w8 = shl i32 %z8, 8
  %w9 = shl i32 %z9, 9
  %w10 = shl i32 %z10, 10
  %w11 = shl i32 %z11, 11
  %o1 = or i32 %z0, %w1
  %o2 = or i32 %o1, %w2
  %o3 = or i32 %o2, %w3
  %o4 = or i32 %o3, %w4
  %o5 = or i32 %o4, %w5
  %o6 = or i32 %o5, %w6
  %o7 = or i32 %o6, %w7
  %o8 = or i32 %o7, %w8
  %o9 = or i32 %o8, %w9
  %o10 = or i32 %o9, %w10
  %o11 = or i32 %o10, %w11
  ; 0b1111_1101_0101 = 0xFD5
  %bad = icmp ne i32 %o11, 4053
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 6: unsigned compares, eq and ne on values whose low 32 bits are equal.
define internal i32 @cmp_unsigned(i64 %m1, i64 %one, i64 %min, i64 %max, i64 %big, i64 %low, i64 %x5a, i64 %x5b) noinline {
  ; x5a = 0x100000005, x5b = 0x200000005
  %c0 = icmp ult i64 %m1, %one         ; 0
  %c1 = icmp ugt i64 %m1, %one         ; 1
  %c2 = icmp ult i64 %max, %min        ; 1
  %c3 = icmp uge i64 %max, %min        ; 0
  %c4 = icmp ugt i64 %big, %low        ; 1
  %c5 = icmp ult i64 %low, %big        ; 1
  %c6 = icmp eq i64 %x5a, %x5b         ; 0
  %c7 = icmp ne i64 %x5a, %x5b         ; 1
  %c8 = icmp eq i64 %big, 4294967296   ; 1
  %c9 = icmp ne i64 %low, 4294967295   ; 0
  %c10 = icmp ule i64 %min, %max       ; 0
  %c11 = icmp ugt i64 %m1, -2          ; 1
  %c12 = icmp eq i64 %low, -1          ; 0
  %c13 = icmp ult i64 %x5a, %x5b       ; 1
  %z0 = zext i1 %c0 to i32
  %z1 = zext i1 %c1 to i32
  %z2 = zext i1 %c2 to i32
  %z3 = zext i1 %c3 to i32
  %z4 = zext i1 %c4 to i32
  %z5 = zext i1 %c5 to i32
  %z6 = zext i1 %c6 to i32
  %z7 = zext i1 %c7 to i32
  %z8 = zext i1 %c8 to i32
  %z9 = zext i1 %c9 to i32
  %z10 = zext i1 %c10 to i32
  %z11 = zext i1 %c11 to i32
  %z12 = zext i1 %c12 to i32
  %z13 = zext i1 %c13 to i32
  %w1 = shl i32 %z1, 1
  %w2 = shl i32 %z2, 2
  %w3 = shl i32 %z3, 3
  %w4 = shl i32 %z4, 4
  %w5 = shl i32 %z5, 5
  %w6 = shl i32 %z6, 6
  %w7 = shl i32 %z7, 7
  %w8 = shl i32 %z8, 8
  %w9 = shl i32 %z9, 9
  %w10 = shl i32 %z10, 10
  %w11 = shl i32 %z11, 11
  %w12 = shl i32 %z12, 12
  %w13 = shl i32 %z13, 13
  %o1 = or i32 %z0, %w1
  %o2 = or i32 %o1, %w2
  %o3 = or i32 %o2, %w3
  %o4 = or i32 %o3, %w4
  %o5 = or i32 %o4, %w5
  %o6 = or i32 %o5, %w6
  %o7 = or i32 %o6, %w7
  %o8 = or i32 %o7, %w8
  %o9 = or i32 %o8, %w9
  %o10 = or i32 %o9, %w10
  %o11 = or i32 %o10, %w11
  %o12 = or i32 %o11, %w12
  %o13 = or i32 %o12, %w13
  ; 0b10_1001_1011_0110 = 0x29B6
  %bad = icmp ne i32 %o13, 10678
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 7: sext/zext i32 -> i64 (conv.i / conv.u), trunc i64 -> i32 (conv.i4),
; sign and zero extension in a 64-bit register from 8, 16 and 32 bits, and
; native int + sext/zext of an int32.
define internal i32 @conv(i32 %m1, i32 %min, i64 %big, i64 %v) noinline {
  ; m1 = -1, min = INT_MIN, big = 0x180000000, v = 0x123456789ABCDEF0
  %e0 = sext i32 %m1 to i64            ; -1
  %e1 = sext i32 %min to i64           ; 0xFFFFFFFF80000000
  %e2 = zext i32 %m1 to i64            ; 0xFFFFFFFF
  %e3 = zext i32 %min to i64           ; 0x80000000
  %e4 = add i64 %e2, 1                 ; 0x100000000
  %t8 = trunc i64 %v to i8
  %t16 = trunc i64 %v to i16
  %t32 = trunc i64 %v to i32
  %e5 = sext i8 %t8 to i64             ; -16
  %e6 = sext i16 %t16 to i64           ; -8464
  %e7 = sext i32 %t32 to i64           ; 0xFFFFFFFF9ABCDEF0
  %e8 = zext i8 %t8 to i64             ; 240
  %e9 = zext i16 %t16 to i64           ; 57072
  %e10 = zext i32 %t32 to i64          ; 0x9ABCDEF0
  %e11 = add i64 %big, %e0             ; 0x17FFFFFFF
  %e12 = add i64 %big, %e2             ; 0x27FFFFFFF
  %gt = icmp ugt i64 %v, %big
  %e13 = zext i1 %gt to i64            ; 1
  %tb = trunc i64 %big to i32          ; INT_MIN
  %x0 = xor i64 %e0, -1
  %x1 = xor i64 %e1, -2147483648
  %x2 = xor i64 %e2, 4294967295
  %x3 = xor i64 %e3, 2147483648
  %x4 = xor i64 %e4, 4294967296
  %x5 = xor i64 %e5, -16
  %x6 = xor i64 %e6, -8464
  %x7 = xor i64 %e7, -1698898192
  %x8 = xor i64 %e8, 240
  %x9 = xor i64 %e9, 57072
  %x10 = xor i64 %e10, 2596069104
  %x11 = xor i64 %e11, 6442450943
  %x12 = xor i64 %e12, 10737418239
  %x13 = xor i64 %e13, 1
  %xt = xor i32 %tb, -2147483648
  %x14 = zext i32 %xt to i64
  %o1 = or i64 %x0, %x1
  %o2 = or i64 %o1, %x2
  %o3 = or i64 %o2, %x3
  %o4 = or i64 %o3, %x4
  %o5 = or i64 %o4, %x5
  %o6 = or i64 %o5, %x6
  %o7 = or i64 %o6, %x7
  %o8 = or i64 %o7, %x8
  %o9 = or i64 %o8, %x9
  %o10 = or i64 %o9, %x10
  %o11 = or i64 %o10, %x11
  %o12 = or i64 %o11, %x12
  %o13 = or i64 %o12, %x13
  %o14 = or i64 %o13, %x14
  %bad = icmp ne i64 %o14, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define i32 @main() {
  %f0 = call i32 @addsub(i64 4294967295, i64 9223372036854775807, i64 -4294967296, i64 4886718345)
  %f1 = call i32 @mul(i64 4294967297, i64 -3, i64 4294967296, i64 81985529216486895)
  %f2 = call i32 @bitwise(i64 -1085102592571150096, i64 1148435428713435120, i64 1311768467463790335)
  %f3 = call i32 @shift_const(i64 -9223372036854775807, i64 1311768467463790320)
  %f4 = call i32 @shift_var(i64 -9223372036854775807, i64 1311768467463790320, i64 0, i64 1, i64 31, i64 32, i64 33, i64 63)
  %f5 = call i32 @cmp_signed(i64 -1, i64 1, i64 -9223372036854775808, i64 9223372036854775807, i64 4294967296, i64 4294967295)
  %f6 = call i32 @cmp_unsigned(i64 -1, i64 1, i64 -9223372036854775808, i64 9223372036854775807, i64 4294967296, i64 4294967295, i64 4294967301, i64 8589934597)
  %f7 = call i32 @conv(i32 -1, i32 -2147483648, i64 6442450944, i64 1311768467463790320)
  %b1 = shl i32 %f1, 1
  %b2 = shl i32 %f2, 2
  %b3 = shl i32 %f3, 3
  %b4 = shl i32 %f4, 4
  %b5 = shl i32 %f5, 5
  %b6 = shl i32 %f6, 6
  %b7 = shl i32 %f7, 7
  %acc1 = or i32 %f0, %b1
  %acc2 = or i32 %acc1, %b2
  %acc3 = or i32 %acc2, %b3
  %acc4 = or i32 %acc3, %b4
  %acc5 = or i32 %acc4, %b5
  %acc6 = or i32 %acc5, %b6
  %acc7 = or i32 %acc6, %b7
  ret i32 %acc7
}
