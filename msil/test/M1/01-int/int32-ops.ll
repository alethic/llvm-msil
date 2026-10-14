; int32 arithmetic, bitwise operations, shifts and compares as values
; (Milestone 4). Each noinline check function receives its inputs as
; arguments, so llc cannot fold the operations even at -O2; it compares every
; result with the value C gives (in the comments) and returns 1 when any
; differs. main ORs one bit per check function into the exit status (8 bits,
; D37), so [exit 0] means every check passed and bit N names check N.
; RUN: %{msil_ll}

; Bit 0: add and sub wrap around; neg (sub 0, x); immediate operands.
define internal i32 @addsub(i32 %max, i32 %m2) noinline {
  %a = add i32 %max, 1                 ; INT_MIN
  %b = add i32 %max, %m2               ; 2147483645
  %c = sub i32 %m2, %max               ; 2147483647 (wraps)
  %d = sub i32 0, %max                 ; -2147483647
  %e = add i32 %max, -2147483648       ; -1
  %f = sub i32 %m2, -100               ; 98
  %g = add i32 %m2, %m2                ; -4
  %x0 = xor i32 %a, -2147483648
  %x1 = xor i32 %b, 2147483645
  %x2 = xor i32 %c, 2147483647
  %x3 = xor i32 %d, -2147483647
  %x4 = xor i32 %e, -1
  %x5 = xor i32 %f, 98
  %x6 = xor i32 %g, -4
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %o6 = or i32 %o5, %x6
  %bad = icmp ne i32 %o6, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 1: mul keeps the low 32 bits of the product.
define internal i32 @mul(i32 %a, i32 %b, i32 %c, i32 %d, i32 %big) noinline {
  %p0 = mul i32 %a, %b                 ; 74565 * 26505 = 1976345325
  %p1 = mul i32 %c, %c                 ; 65536 * 65536 = 0
  %p2 = mul i32 %d, %a                 ; -3 * 74565 = -223695
  %p3 = mul i32 %a, 1000               ; 74565000
  %p4 = mul i32 %d, %d                 ; 9
  %p5 = mul i32 %a, -1                 ; -74565
  %p6 = mul i32 %big, %big             ; 0x7654321^2 -> -677098943
  %p7 = mul i32 %big, -7               ; -868537831
  %x0 = xor i32 %p0, 1976345325
  %x1 = xor i32 %p1, 0
  %x2 = xor i32 %p2, -223695
  %x3 = xor i32 %p3, 74565000
  %x4 = xor i32 %p4, 9
  %x5 = xor i32 %p5, -74565
  %x6 = xor i32 %p6, -677098943
  %x7 = xor i32 %p7, -868537831
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %o6 = or i32 %o5, %x6
  %o7 = or i32 %o6, %x7
  %bad = icmp ne i32 %o7, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 2: and, or, xor, not (xor -1), with registers and immediates.
define internal i32 @bitwise(i32 %a, i32 %b) noinline {
  ; a = 0xF0F0F0F0 (-252645136), b = 0x0FF00FF0
  %r0 = and i32 %a, %b                 ; 0x00F000F0 = 15728880
  %r1 = or i32 %a, %b                  ; 0xFFF0FFF0 = -983056
  %r2 = xor i32 %a, %b                 ; 0xFF00FF00 = -16711936
  %r3 = xor i32 %a, -1                 ; 0x0F0F0F0F = 252645135
  %r4 = and i32 %a, 65280              ; 0xF000 = 61440
  %r5 = or i32 %a, 1                   ; -252645135
  %r6 = xor i32 %a, -2147483648        ; 0x70F0F0F0 = 1894838512
  %x0 = xor i32 %r0, 15728880
  %x1 = xor i32 %r1, -983056
  %x2 = xor i32 %r2, -16711936
  %x3 = xor i32 %r3, 252645135
  %x4 = xor i32 %r4, 61440
  %x5 = xor i32 %r5, -252645135
  %x6 = xor i32 %r6, 1894838512
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %o6 = or i32 %o5, %x6
  %bad = icmp ne i32 %o6, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 3: shl, lshr (shr.un) and ashr (shr) by constant amounts.
define internal i32 @shift_const(i32 %a, i32 %b) noinline {
  ; a = 0x80000001 (-2147483647), b = 0x12345678
  %s0 = shl i32 %a, 1                  ; 2
  %s1 = shl i32 %b, 4                  ; 0x23456780 = 591751040
  %s2 = shl i32 %a, 31                 ; INT_MIN
  %s3 = lshr i32 %a, 1                 ; 0x40000000 = 1073741824
  %s4 = lshr i32 %a, 31                ; 1
  %s5 = lshr i32 %b, 8                 ; 0x00123456 = 1193046
  %s6 = ashr i32 %a, 1                 ; 0xC0000000 = -1073741824
  %s7 = ashr i32 %a, 31                ; -1
  %s8 = ashr i32 %b, 28                ; 1
  %x0 = xor i32 %s0, 2
  %x1 = xor i32 %s1, 591751040
  %x2 = xor i32 %s2, -2147483648
  %x3 = xor i32 %s3, 1073741824
  %x4 = xor i32 %s4, 1
  %x5 = xor i32 %s5, 1193046
  %x6 = xor i32 %s6, -1073741824
  %x7 = xor i32 %s7, -1
  %x8 = xor i32 %s8, 1
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %o6 = or i32 %o5, %x6
  %o7 = or i32 %o6, %x7
  %o8 = or i32 %o7, %x8
  %bad = icmp ne i32 %o8, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 4: shifts by variable amounts (0, 1, 13, 31), and C's masked form
; 'x << (n & 31)' with n = 33.
define internal i32 @shift_var(i32 %a, i32 %b, i32 %n0, i32 %n1, i32 %n13, i32 %n31, i32 %n33) noinline {
  ; a = 0x80000001 (-2147483647), b = 0x12345678
  %s0 = shl i32 %a, %n0                ; a
  %s1 = shl i32 %a, %n1                ; 2
  %s2 = shl i32 %b, %n13               ; -1966145536
  %s3 = lshr i32 %b, %n13              ; 37282
  %s4 = lshr i32 %a, %n13              ; 262144
  %s5 = lshr i32 %a, %n31              ; 1
  %s6 = ashr i32 %a, %n13              ; -262144
  %s7 = ashr i32 %a, %n31              ; -1
  %s8 = ashr i32 %a, %n0               ; a
  %m = and i32 %n33, 31
  %s9 = shl i32 %b, %m                 ; 0x2468ACF0 = 610839792
  %x0 = xor i32 %s0, -2147483647
  %x1 = xor i32 %s1, 2
  %x2 = xor i32 %s2, -1966145536
  %x3 = xor i32 %s3, 37282
  %x4 = xor i32 %s4, 262144
  %x5 = xor i32 %s5, 1
  %x6 = xor i32 %s6, -262144
  %x7 = xor i32 %s7, -1
  %x8 = xor i32 %s8, -2147483647
  %x9 = xor i32 %s9, 610839792
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %o6 = or i32 %o5, %x6
  %o7 = or i32 %o6, %x7
  %o8 = or i32 %o7, %x8
  %o9 = or i32 %o8, %x9
  %bad = icmp ne i32 %o9, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 5: signed compares as values (clt, cgt and the negated forms built
; from 'ceq'), true and false outcomes, on operands of mixed sign and
; against immediates. Each result is one bit of a word compared at the end.
define internal i32 @cmp_signed(i32 %m1, i32 %one, i32 %min, i32 %max) noinline {
  %c0 = icmp slt i32 %m1, %one         ; 1
  %c1 = icmp sgt i32 %m1, %one         ; 0
  %c2 = icmp sle i32 %m1, %m1          ; 1
  %c3 = icmp sge i32 %min, %max        ; 0
  %c4 = icmp slt i32 %min, %max        ; 1
  %c5 = icmp sgt i32 %max, %min        ; 1
  %c6 = icmp sle i32 %one, %m1         ; 0
  %c7 = icmp sge i32 %m1, %min         ; 1
  %k0 = icmp slt i32 %m1, 0            ; 1
  %k1 = icmp sgt i32 %one, -1          ; 1
  %k2 = icmp sge i32 %min, -2147483647 ; 0
  %k3 = icmp sle i32 %max, 2147483646  ; 0
  %k4 = icmp sgt i32 %max, 2147483646  ; 1
  %z0 = zext i1 %c0 to i32
  %z1 = zext i1 %c1 to i32
  %z2 = zext i1 %c2 to i32
  %z3 = zext i1 %c3 to i32
  %z4 = zext i1 %c4 to i32
  %z5 = zext i1 %c5 to i32
  %z6 = zext i1 %c6 to i32
  %z7 = zext i1 %c7 to i32
  %z8 = zext i1 %k0 to i32
  %z9 = zext i1 %k1 to i32
  %z10 = zext i1 %k2 to i32
  %z11 = zext i1 %k3 to i32
  %z12 = zext i1 %k4 to i32
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
  ; 0b1_0011_1011_0101 = 0x13B5
  %bad = icmp ne i32 %o12, 5045
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 6: unsigned compares (clt.un, cgt.un and negated forms), eq and ne,
; and the compares against zero.
define internal i32 @cmp_unsigned(i32 %m1, i32 %one, i32 %min, i32 %max, i32 %min2) noinline {
  %c0 = icmp ult i32 %m1, %one         ; 0
  %c1 = icmp ugt i32 %m1, %one         ; 1
  %c2 = icmp ule i32 %one, %m1         ; 1
  %c3 = icmp uge i32 %max, %min        ; 0
  %c4 = icmp ult i32 %max, %min        ; 1
  %c5 = icmp ugt i32 %min, %max        ; 1
  %c6 = icmp eq i32 %min, %min2        ; 1
  %c7 = icmp ne i32 %min, %min2        ; 0
  %c8 = icmp eq i32 %m1, %one          ; 0
  %c9 = icmp ne i32 %m1, %one          ; 1
  %c10 = icmp ule i32 %min, %min2      ; 1
  %c11 = icmp uge i32 %one, %m1        ; 0
  %c12 = icmp ult i32 %one, 2          ; 1
  %c13 = icmp ugt i32 %m1, -2          ; 1
  %c14 = icmp eq i32 %one, 0           ; 0
  %c15 = icmp ne i32 %min, 0           ; 1
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
  %z14 = zext i1 %c14 to i32
  %z15 = zext i1 %c15 to i32
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
  %w14 = shl i32 %z14, 14
  %w15 = shl i32 %z15, 15
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
  %o14 = or i32 %o13, %w14
  %o15 = or i32 %o14, %w15
  ; 0b1011_0110_0111_0110 = 0xB676
  %bad = icmp ne i32 %o15, 46710
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 7: sext and zext of compares and selects of constants are used as
; numbers (they stay arithmetic, D34), together with general selects.
define internal i32 @cmp_ext(i32 %a, i32 %b) noinline {
  ; a = -5, b = 7
  %lt = icmp slt i32 %a, %b
  %gt = icmp sgt i32 %a, %b
  %ult = icmp ult i32 %a, %b
  %ne = icmp ne i32 %a, %b
  %eq = icmp eq i32 %a, %b
  %neg = icmp slt i32 %a, 0
  %s1 = sext i1 %lt to i32             ; -1
  %s2 = sext i1 %gt to i32             ; 0
  %z1 = zext i1 %ult to i32            ; 0
  %z2 = zext i1 %ne to i32             ; 1
  %v1 = select i1 %eq, i32 -1, i32 0   ; 0
  %v2 = select i1 %lt, i32 5, i32 4    ; 5
  %v3 = select i1 %gt, i32 42, i32 17  ; 17
  %v4 = select i1 %neg, i32 8, i32 0   ; 8
  %t1 = and i32 %s1, 1000              ; 1000
  %t2 = mul i32 %z1, 3                 ; 0
  %t3 = mul i32 %z2, 20                ; 20
  %u1 = add i32 %t1, %s2
  %u2 = add i32 %u1, %t2
  %u3 = add i32 %u2, %t3
  %u4 = add i32 %u3, %v1
  %u5 = add i32 %u4, %v2
  %u6 = add i32 %u5, %v3
  %u7 = add i32 %u6, %v4               ; 1050
  %a1 = add i32 %a, %s1                ; -6
  %x0 = xor i32 %u7, 1050
  %x1 = xor i32 %a1, -6
  %o = or i32 %x0, %x1
  %bad = icmp ne i32 %o, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define i32 @main() {
  %f0 = call i32 @addsub(i32 2147483647, i32 -2)
  %f1 = call i32 @mul(i32 74565, i32 26505, i32 65536, i32 -3, i32 124076833)
  %f2 = call i32 @bitwise(i32 -252645136, i32 267390960)
  %f3 = call i32 @shift_const(i32 -2147483647, i32 305419896)
  %f4 = call i32 @shift_var(i32 -2147483647, i32 305419896, i32 0, i32 1, i32 13, i32 31, i32 33)
  %f5 = call i32 @cmp_signed(i32 -1, i32 1, i32 -2147483648, i32 2147483647)
  %f6 = call i32 @cmp_unsigned(i32 -1, i32 1, i32 -2147483648, i32 2147483647, i32 -2147483648)
  %f7 = call i32 @cmp_ext(i32 -5, i32 7)
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
