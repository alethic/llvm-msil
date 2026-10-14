; Integer division and remainder (Milestone 4): div, div.un, rem and rem.un on
; int32 and native int, for every sign combination (quotients round toward
; zero, remainders take the dividend's sign), unsigned operands with the top
; bit set, 64-bit operands beyond 32 bits, divisors that are constants (powers
; of two, negative, and ones that DAGCombiner may rewrite), and div/rem pairs
; of the same operands. Only in-range values: no division by zero and no
; INT_MIN / -1 (undefined in C and LLVM; CIL throws). Each noinline check
; function returns 1 when a result differs from the value C gives (in the
; comments); main ORs one bit per check into the exit status (8 bits, D37):
; [exit 0] means every check passed.
; RUN: %{msil_ll}

; Bit 0: sdiv i32.
define internal i32 @sdiv32(i32 %p7, i32 %m7, i32 %p2, i32 %m2, i32 %min, i32 %max, i32 %m1, i32 %one, i32 %zero, i32 %m5, i32 %mil) noinline {
  %q0 = sdiv i32 %p7, %p2                        ; 3
  %q1 = sdiv i32 %m7, %p2                        ; -3
  %q2 = sdiv i32 %p7, %m2                        ; -3
  %q3 = sdiv i32 %m7, %m2                        ; 3
  %q4 = sdiv i32 %min, %p2                       ; -1073741824
  %q5 = sdiv i32 %max, %m1                       ; -2147483647
  %q6 = sdiv i32 %min, %one                      ; INT_MIN
  %q7 = sdiv i32 %zero, %m5                      ; 0
  %q8 = sdiv i32 %mil, %m7                       ; 1000000 / -7 = -142857
  %x0 = xor i32 %q0, 3
  %x1 = xor i32 %q1, -3
  %x2 = xor i32 %q2, -3
  %x3 = xor i32 %q3, 3
  %x4 = xor i32 %q4, -1073741824
  %x5 = xor i32 %q5, -2147483647
  %x6 = xor i32 %q6, -2147483648
  %x8 = xor i32 %q8, -142857
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %o6 = or i32 %o5, %x6
  %o7 = or i32 %o6, %q7
  %o8 = or i32 %o7, %x8
  %bad = icmp ne i32 %o8, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 1: srem i32.
define internal i32 @srem32(i32 %p7, i32 %m7, i32 %p2, i32 %m2, i32 %min, i32 %max, i32 %m1, i32 %p3, i32 %m10, i32 %p5) noinline {
  %r0 = srem i32 %p7, %p2                        ; 1
  %r1 = srem i32 %m7, %p2                        ; -1
  %r2 = srem i32 %p7, %m2                        ; 1
  %r3 = srem i32 %m7, %m2                        ; -1
  %r4 = srem i32 %min, %p3                       ; -2
  %r5 = srem i32 %max, %m10                      ; 7
  %r6 = srem i32 %m1, %p5                        ; -1
  %x0 = xor i32 %r0, 1
  %x1 = xor i32 %r1, -1
  %x2 = xor i32 %r2, 1
  %x3 = xor i32 %r3, -1
  %x4 = xor i32 %r4, -2
  %x5 = xor i32 %r5, 7
  %x6 = xor i32 %r6, -1
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

; Bit 2: udiv and urem i32 with the top bit set.
define internal i32 @udiv32(i32 %all, i32 %top, i32 %p3, i32 %p7, i32 %m10, i32 %m3) noinline {
  ; all = 0xFFFFFFFF, top = 0x80000000
  %q0 = udiv i32 %all, %p3                       ; 0x55555555
  %r0 = urem i32 %all, %p3                       ; 0
  %q1 = udiv i32 %top, %p7                       ; 306783378
  %r1 = urem i32 %top, %p7                       ; 2
  %q2 = udiv i32 %all, %top                      ; 1
  %r2 = urem i32 %all, %top                      ; 0x7FFFFFFF
  %q3 = udiv i32 %m10, %m3                       ; 0
  %r3 = urem i32 %m10, %m3                       ; 0xFFFFFFF6
  %x0 = xor i32 %q0, 1431655765
  %x1 = xor i32 %q1, 306783378
  %x2 = xor i32 %r1, 2
  %x3 = xor i32 %q2, 1
  %x4 = xor i32 %r2, 2147483647
  %x5 = xor i32 %r3, -10
  %o1 = or i32 %x0, %r0
  %o2 = or i32 %o1, %x1
  %o3 = or i32 %o2, %x2
  %o4 = or i32 %o3, %x3
  %o5 = or i32 %o4, %x4
  %o6 = or i32 %o5, %q3
  %o7 = or i32 %o6, %x5
  %bad = icmp ne i32 %o7, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 3: sdiv and srem i64 beyond 32 bits.
define internal i32 @sdiv64(i64 %x, i64 %nx, i64 %y, i64 %ny, i64 %p7, i64 %min, i64 %p2, i64 %p3) noinline {
  ; x = -1000000000000007, nx = -x, y = 0x100000003, ny = -y
  %q0 = sdiv i64 %x, %p7                         ; -142857142857143
  %r0 = srem i64 %x, %p7                         ; -6
  %q1 = sdiv i64 %x, %y                          ; -232830
  %r1 = srem i64 %x, %y                          ; -2763773837
  %q2 = sdiv i64 %nx, %ny                        ; -232830
  %r2 = srem i64 %nx, %ny                        ; 2763773837
  %q3 = sdiv i64 %min, %p2                       ; -2^62
  %r3 = srem i64 %min, %p3                       ; -2
  %x0 = xor i64 %q0, -142857142857143
  %x1 = xor i64 %r0, -6
  %x2 = xor i64 %q1, -232830
  %x3 = xor i64 %r1, -2763773837
  %x4 = xor i64 %q2, -232830
  %x5 = xor i64 %r2, 2763773837
  %x6 = xor i64 %q3, -4611686018427387904
  %x7 = xor i64 %r3, -2
  %o1 = or i64 %x0, %x1
  %o2 = or i64 %o1, %x2
  %o3 = or i64 %o2, %x3
  %o4 = or i64 %o3, %x4
  %o5 = or i64 %o4, %x5
  %o6 = or i64 %o5, %x6
  %o7 = or i64 %o6, %x7
  %bad = icmp ne i64 %o7, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 4: udiv and urem i64 with the top bit set.
define internal i32 @udiv64(i64 %all, i64 %top, i64 %p10, i64 %d33, i64 %d32) noinline {
  ; all = 2^64 - 1, top = 2^63, d33 = 0x100000001, d32 = 0xFFFFFFFF
  %q0 = udiv i64 %all, %p10                      ; 1844674407370955161
  %r0 = urem i64 %all, %p10                      ; 5
  %q1 = udiv i64 %all, %top                      ; 1
  %r1 = urem i64 %all, %top                      ; 2^63 - 1
  %q2 = udiv i64 %top, %d33                      ; 2147483647
  %r2 = urem i64 %top, %d33                      ; 2147483649
  %q3 = udiv i64 %all, %d32                      ; 0x100000001
  %r3 = urem i64 %all, %d32                      ; 0
  %x0 = xor i64 %q0, 1844674407370955161
  %x1 = xor i64 %r0, 5
  %x2 = xor i64 %q1, 1
  %x3 = xor i64 %r1, 9223372036854775807
  %x4 = xor i64 %q2, 2147483647
  %x5 = xor i64 %r2, 2147483649
  %x6 = xor i64 %q3, 4294967297
  %o1 = or i64 %x0, %x1
  %o2 = or i64 %o1, %x2
  %o3 = or i64 %o2, %x3
  %o4 = or i64 %o3, %x4
  %o5 = or i64 %o4, %x5
  %o6 = or i64 %o5, %x6
  %o7 = or i64 %o6, %r3
  %bad = icmp ne i64 %o7, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 5: i32 divisors that are constants.
define internal i32 @const32(i32 %v, i32 %w, i32 %min) noinline {
  ; v = -13, w = 2000000011
  %c0 = sdiv i32 %v, 8                           ; -1
  %c1 = srem i32 %v, 8                           ; -5
  %c2 = sdiv i32 %w, 10                          ; 200000001
  %c3 = srem i32 %w, 10                          ; 1
  %c4 = udiv i32 %v, 10                          ; 429496728
  %c5 = urem i32 %v, 7                           ; 5
  %c6 = sdiv i32 %w, -3                          ; -666666670
  %c7 = srem i32 %w, -3                          ; 1
  %c8 = sdiv i32 %v, -3                          ; 4
  %c9 = srem i32 %v, -3                          ; -1
  %c10 = udiv i32 %v, 16                         ; 268435455
  %c11 = sdiv i32 %v, -2147483648                ; 0
  %c12 = sdiv i32 %min, -2147483648              ; 1
  %c13 = srem i32 %v, 2147483647                 ; -13
  %x0 = xor i32 %c0, -1
  %x1 = xor i32 %c1, -5
  %x2 = xor i32 %c2, 200000001
  %x3 = xor i32 %c3, 1
  %x4 = xor i32 %c4, 429496728
  %x5 = xor i32 %c5, 5
  %x6 = xor i32 %c6, -666666670
  %x7 = xor i32 %c7, 1
  %x8 = xor i32 %c8, 4
  %x9 = xor i32 %c9, -1
  %x10 = xor i32 %c10, 268435455
  %x12 = xor i32 %c12, 1
  %x13 = xor i32 %c13, -13
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %o6 = or i32 %o5, %x6
  %o7 = or i32 %o6, %x7
  %o8 = or i32 %o7, %x8
  %o9 = or i32 %o8, %x9
  %o10 = or i32 %o9, %x10
  %o11 = or i32 %o10, %c11
  %o12 = or i32 %o11, %x12
  %o13 = or i32 %o12, %x13
  %bad = icmp ne i32 %o13, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 6: i64 divisors that are constants.
define internal i32 @const64(i64 %v, i64 %w) noinline {
  ; v = -1234567890123456789, w = 0xF000000000000001
  %c0 = sdiv i64 %v, 1000000007                  ; -1234567881
  %c1 = srem i64 %v, 1000000007                  ; -481481622
  %c2 = udiv i64 %w, 10                          ; 1729382256910270464
  %c3 = urem i64 %w, 10                          ; 1
  %c4 = srem i64 %v, 16                          ; -5
  %c5 = sdiv i64 %v, -4294967296                 ; 287445236
  %c6 = udiv i64 %v, 4294967296                  ; 4007522059
  %c7 = urem i64 %v, 4294967297                  ; 2469957601
  %x0 = xor i64 %c0, -1234567881
  %x1 = xor i64 %c1, -481481622
  %x2 = xor i64 %c2, 1729382256910270464
  %x3 = xor i64 %c3, 1
  %x4 = xor i64 %c4, -5
  %x5 = xor i64 %c5, 287445236
  %x6 = xor i64 %c6, 4007522059
  %x7 = xor i64 %c7, 2469957601
  %o1 = or i64 %x0, %x1
  %o2 = or i64 %o1, %x2
  %o3 = or i64 %o2, %x3
  %o4 = or i64 %o3, %x4
  %o5 = or i64 %o4, %x5
  %o6 = or i64 %o5, %x6
  %o7 = or i64 %o6, %x7
  %bad = icmp ne i64 %o7, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 7: div and rem of the same operands satisfy a == (a / b) * b + a % b,
; signed and unsigned, int32 and native int.
define internal i32 @pairs(i32 %a, i32 %b, i64 %c, i64 %d) noinline {
  ; a = -2000000001, b = 37, c = 0xFEDCBA9876543210, d = 0x76543210
  %q0 = sdiv i32 %a, %b
  %r0 = srem i32 %a, %b
  %m0 = mul i32 %q0, %b
  %s0 = add i32 %m0, %r0
  %q1 = udiv i32 %a, %b
  %r1 = urem i32 %a, %b
  %m1 = mul i32 %q1, %b
  %s1 = add i32 %m1, %r1
  %q2 = sdiv i64 %c, %d
  %r2 = srem i64 %c, %d
  %m2 = mul i64 %q2, %d
  %s2 = add i64 %m2, %r2
  %q3 = udiv i64 %c, %d
  %r3 = urem i64 %c, %d
  %m3 = mul i64 %q3, %d
  %s3 = add i64 %m3, %r3
  %x0 = xor i32 %s0, %a
  %x1 = xor i32 %s1, %a
  %y2 = xor i64 %s2, %c
  %y3 = xor i64 %s3, %c
  ; the remainders themselves: -2000000001 % 37 = -3, 2294967295 % 37 = 4,
  ; 0xFEDCBA9876543210 % 0x76543210 = -913322960 (signed), 105722272 (unsigned)
  %x4 = xor i32 %r0, -3
  %x5 = xor i32 %r1, 4
  %y6 = xor i64 %r2, -913322960
  %y7 = xor i64 %r3, 105722272
  %o0 = or i32 %x0, %x1
  %o1 = or i32 %o0, %x4
  %o2 = or i32 %o1, %x5
  %ow = zext i32 %o2 to i64
  %p0 = or i64 %y2, %y3
  %p1 = or i64 %p0, %y6
  %p2 = or i64 %p1, %y7
  %all = or i64 %p2, %ow
  %bad = icmp ne i64 %all, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define i32 @main() {
  %f0 = call i32 @sdiv32(i32 7, i32 -7, i32 2, i32 -2, i32 -2147483648, i32 2147483647, i32 -1, i32 1, i32 0, i32 -5, i32 1000000)
  %f1 = call i32 @srem32(i32 7, i32 -7, i32 2, i32 -2, i32 -2147483648, i32 2147483647, i32 -1, i32 3, i32 -10, i32 5)
  %f2 = call i32 @udiv32(i32 -1, i32 -2147483648, i32 3, i32 7, i32 -10, i32 -3)
  %f3 = call i32 @sdiv64(i64 -1000000000000007, i64 1000000000000007, i64 4294967299, i64 -4294967299, i64 7, i64 -9223372036854775808, i64 2, i64 3)
  %f4 = call i32 @udiv64(i64 -1, i64 -9223372036854775808, i64 10, i64 4294967297, i64 4294967295)
  %f5 = call i32 @const32(i32 -13, i32 2000000011, i32 -2147483648)
  %f6 = call i32 @const64(i64 -1234567890123456789, i64 -1152921504606846975)
  %f7 = call i32 @pairs(i32 -2000000001, i32 37, i64 -81985529216486896, i64 1985229328)
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
