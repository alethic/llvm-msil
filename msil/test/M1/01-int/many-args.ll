; Calls with 12 arguments of mixed int32 and native int types (Milestone 4):
; argument order and indexing (ldarg.0-3 and ldarg.s), sign and full 64-bit
; width of every argument, deep evaluation stacks when every argument is
; itself a call result (.maxstack 12 and more), arguments forwarded in a
; different order, and a void call. Each noinline check function returns 1
; when a result differs from the value C gives; main ORs one bit per check
; into the exit status (8 bits, D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

; Sum of every argument times its own odd weight, in 64-bit arithmetic.
define internal i64 @hash12(i32 %a0, i64 %a1, i32 %a2, i64 %a3, i32 %a4, i64 %a5, i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11) noinline {
  %w0 = sext i32 %a0 to i64
  %w2 = sext i32 %a2 to i64
  %w4 = sext i32 %a4 to i64
  %w6 = sext i32 %a6 to i64
  %w8 = sext i32 %a8 to i64
  %w10 = sext i32 %a10 to i64
  %m0 = mul i64 %w0, 3
  %m1 = mul i64 %a1, 5
  %m2 = mul i64 %w2, 7
  %m3 = mul i64 %a3, 11
  %m4 = mul i64 %w4, 13
  %m5 = mul i64 %a5, 17
  %m6 = mul i64 %w6, 19
  %m7 = mul i64 %a7, 23
  %m8 = mul i64 %w8, 29
  %m9 = mul i64 %a9, 31
  %m10 = mul i64 %w10, 37
  %m11 = mul i64 %a11, 41
  %s1 = add i64 %m0, %m1
  %s2 = add i64 %s1, %m2
  %s3 = add i64 %s2, %m3
  %s4 = add i64 %s3, %m4
  %s5 = add i64 %s4, %m5
  %s6 = add i64 %s5, %m6
  %s7 = add i64 %s6, %m7
  %s8 = add i64 %s7, %m8
  %s9 = add i64 %s8, %m9
  %s10 = add i64 %s9, %m10
  %s11 = add i64 %s10, %m11
  ret i64 %s11
}

define internal i64 @xor12(i32 %a0, i64 %a1, i32 %a2, i64 %a3, i32 %a4, i64 %a5, i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11) noinline {
  %w0 = sext i32 %a0 to i64
  %w2 = sext i32 %a2 to i64
  %w4 = sext i32 %a4 to i64
  %w6 = sext i32 %a6 to i64
  %w8 = sext i32 %a8 to i64
  %w10 = sext i32 %a10 to i64
  %x1 = xor i64 %w0, %a1
  %x2 = xor i64 %x1, %w2
  %x3 = xor i64 %x2, %a3
  %x4 = xor i64 %x3, %w4
  %x5 = xor i64 %x4, %a5
  %x6 = xor i64 %x5, %w6
  %x7 = xor i64 %x6, %a7
  %x8 = xor i64 %x7, %w8
  %x9 = xor i64 %x8, %a9
  %x10 = xor i64 %x9, %w10
  %x11 = xor i64 %x10, %a11
  ret i64 %x11
}

define internal i32 @pick0(i32 %a0, i64 %a1, i32 %a2, i64 %a3, i32 %a4, i64 %a5, i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11) noinline {
  ret i32 %a0
}

define internal i64 @pick3(i32 %a0, i64 %a1, i32 %a2, i64 %a3, i32 %a4, i64 %a5, i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11) noinline {
  ret i64 %a3
}

define internal i32 @pick4(i32 %a0, i64 %a1, i32 %a2, i64 %a3, i32 %a4, i64 %a5, i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11) noinline {
  ret i32 %a4
}

define i64 @pick5(i32 %a0, i64 %a1, i32 %a2, i64 %a3, i32 %a4, i64 %a5, i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11) noinline {
  ret i64 %a5
}

define internal i32 @pick10(i32 %a0, i64 %a1, i32 %a2, i64 %a3, i32 %a4, i64 %a5, i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11) noinline {
  ret i32 %a10
}

define internal i64 @pick11(i32 %a0, i64 %a1, i32 %a2, i64 %a3, i32 %a4, i64 %a5, i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11) noinline {
  ret i64 %a11
}

; The number of negative arguments.
define internal i32 @negs12(i32 %a0, i64 %a1, i32 %a2, i64 %a3, i32 %a4, i64 %a5, i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11) noinline {
  %c0 = icmp slt i32 %a0, 0
  %z0 = zext i1 %c0 to i32
  %c1 = icmp slt i64 %a1, 0
  %z1 = zext i1 %c1 to i32
  %c2 = icmp slt i32 %a2, 0
  %z2 = zext i1 %c2 to i32
  %c3 = icmp slt i64 %a3, 0
  %z3 = zext i1 %c3 to i32
  %c4 = icmp slt i32 %a4, 0
  %z4 = zext i1 %c4 to i32
  %c5 = icmp slt i64 %a5, 0
  %z5 = zext i1 %c5 to i32
  %c6 = icmp slt i32 %a6, 0
  %z6 = zext i1 %c6 to i32
  %c7 = icmp slt i64 %a7, 0
  %z7 = zext i1 %c7 to i32
  %c8 = icmp slt i32 %a8, 0
  %z8 = zext i1 %c8 to i32
  %c9 = icmp slt i64 %a9, 0
  %z9 = zext i1 %c9 to i32
  %c10 = icmp slt i32 %a10, 0
  %z10 = zext i1 %c10 to i32
  %c11 = icmp slt i64 %a11, 0
  %z11 = zext i1 %c11 to i32
  %n1 = add i32 %z0, %z1
  %n2 = add i32 %n1, %z2
  %n3 = add i32 %n2, %z3
  %n4 = add i32 %n3, %z4
  %n5 = add i32 %n4, %z5
  %n6 = add i32 %n5, %z6
  %n7 = add i32 %n6, %z7
  %n8 = add i32 %n7, %z8
  %n9 = add i32 %n8, %z9
  %n10 = add i32 %n9, %z10
  %n11 = add i32 %n10, %z11
  ret i32 %n11
}

; Passes its arguments on, rotated by two positions (types stay in place).
define internal i64 @forward12(i32 %a0, i64 %a1, i32 %a2, i64 %a3, i32 %a4, i64 %a5, i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11) noinline {
  %r = call i64 @hash12(i32 %a2, i64 %a3, i32 %a4, i64 %a5, i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11, i32 %a0, i64 %a1)
  ret i64 %r
}

define internal void @void12(i32 %a0, i64 %a1, i32 %a2, i64 %a3, i32 %a4, i64 %a5, i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11) noinline {
  %d = add i32 %a0, %a10
  ret void
}

define internal i32 @id32(i32 %x) noinline {
  ret i32 %x
}

define internal i64 @id64(i64 %x) noinline {
  ret i64 %x
}

; Bit 0: twelve constant arguments.
define internal i32 @check_hash() noinline {
  %h = call i64 @hash12(i32 1, i64 4294967298, i32 -3, i64 -17179869188, i32 5, i64 9223372032559808518, i32 -7, i64 8, i32 2147483647, i64 -9, i32 -2147483648, i64 4503599627370507)
  %x = xor i64 %h, -9038724709830622923
  %bad = icmp ne i64 %x, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 1: the arguments read with ldarg.0 and ldarg.3.
define internal i32 @check_short() noinline {
  %p0 = call i32 @pick0(i32 1, i64 4294967298, i32 -3, i64 -17179869188, i32 5, i64 9223372032559808518, i32 -7, i64 8, i32 2147483647, i64 -9, i32 -2147483648, i64 4503599627370507)
  %p3 = call i64 @pick3(i32 1, i64 4294967298, i32 -3, i64 -17179869188, i32 5, i64 9223372032559808518, i32 -7, i64 8, i32 2147483647, i64 -9, i32 -2147483648, i64 4503599627370507)
  %x0 = xor i32 %p0, 1
  %y0 = zext i32 %x0 to i64
  %y3 = xor i64 %p3, -17179869188
  %o0 = or i64 %y0, %y3
  %bad = icmp ne i64 %o0, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 2: the arguments read with ldarg.s 4, 5, 10 and 11.
define internal i32 @check_long() noinline {
  %p4 = call i32 @pick4(i32 1, i64 4294967298, i32 -3, i64 -17179869188, i32 5, i64 9223372032559808518, i32 -7, i64 8, i32 2147483647, i64 -9, i32 -2147483648, i64 4503599627370507)
  %p5 = call i64 @pick5(i32 1, i64 4294967298, i32 -3, i64 -17179869188, i32 5, i64 9223372032559808518, i32 -7, i64 8, i32 2147483647, i64 -9, i32 -2147483648, i64 4503599627370507)
  %p10 = call i32 @pick10(i32 1, i64 4294967298, i32 -3, i64 -17179869188, i32 5, i64 9223372032559808518, i32 -7, i64 8, i32 2147483647, i64 -9, i32 -2147483648, i64 4503599627370507)
  %p11 = call i64 @pick11(i32 1, i64 4294967298, i32 -3, i64 -17179869188, i32 5, i64 9223372032559808518, i32 -7, i64 8, i32 2147483647, i64 -9, i32 -2147483648, i64 4503599627370507)
  %x4 = xor i32 %p4, 5
  %y4 = zext i32 %x4 to i64
  %y5 = xor i64 %p5, 9223372032559808518
  %x10 = xor i32 %p10, -2147483648
  %y10 = zext i32 %x10 to i64
  %y11 = xor i64 %p11, 4503599627370507
  %o0 = or i64 %y4, %y5
  %o1 = or i64 %o0, %y10
  %o2 = or i64 %o1, %y11
  %bad = icmp ne i64 %o2, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 3: every argument is the result of a call.
define internal i32 @check_nested() noinline {
  %v0 = call i32 @id32(i32 1)
  %v1 = call i64 @id64(i64 4294967298)
  %v2 = call i32 @id32(i32 -3)
  %v3 = call i64 @id64(i64 -17179869188)
  %v4 = call i32 @id32(i32 5)
  %v5 = call i64 @id64(i64 9223372032559808518)
  %v6 = call i32 @id32(i32 -7)
  %v7 = call i64 @id64(i64 8)
  %v8 = call i32 @id32(i32 2147483647)
  %v9 = call i64 @id64(i64 -9)
  %v10 = call i32 @id32(i32 -2147483648)
  %v11 = call i64 @id64(i64 4503599627370507)
  %h = call i64 @hash12(i32 %v0, i64 %v1, i32 %v2, i64 %v3, i32 %v4, i64 %v5, i32 %v6, i64 %v7, i32 %v8, i64 %v9, i32 %v10, i64 %v11)
  %x = xor i64 %h, -9038724709830622923
  %bad = icmp ne i64 %x, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 4: the same values used several times, and computed arguments.
define internal i32 @check_reuse() noinline {
  %v = call i32 @id32(i32 3)
  %q = call i64 @id64(i64 4294967297)
  %vw = sext i32 %v to i64
  %v1 = add i32 %v, 1
  %v9 = mul i32 %v, %v
  %q2 = add i64 %q, 1
  %vn = sub i32 0, %v
  %vnw = sext i32 %vn to i64
  %q3 = mul i64 %q, %vw
  %z = sub i32 %v, %v
  %h = call i64 @hash12(i32 %v, i64 %vw, i32 %v1, i64 %q, i32 %v9, i64 %q2, i32 %v, i64 %vnw, i32 %vn, i64 %q3, i32 %z, i64 %q)
  %x = xor i64 %h, 695784702201
  %bad = icmp ne i64 %x, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 5: five of the twelve arguments are negative.
define internal i32 @check_signs() noinline {
  %n = call i32 @negs12(i32 1, i64 4294967298, i32 -3, i64 -17179869188, i32 5, i64 9223372032559808518, i32 -7, i64 8, i32 2147483647, i64 -9, i32 -2147483648, i64 4503599627370507)
  %x = xor i32 %n, 5
  %y = zext i32 %x to i64
  %bad = icmp ne i64 %y, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 6: a 12-argument function passes its arguments on, rotated.
define internal i32 @check_forward() noinline {
  %h = call i64 @forward12(i32 1, i64 4294967298, i32 -3, i64 -17179869188, i32 5, i64 9223372032559808518, i32 -7, i64 8, i32 2147483647, i64 -9, i32 -2147483648, i64 4503599627370507)
  %x = xor i64 %h, -9083760426931453601
  %bad = icmp ne i64 %x, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 7: a void 12-argument call, then the xor of all twelve arguments.
define internal i32 @check_void() noinline {
  call void @void12(i32 1, i64 4294967298, i32 -3, i64 -17179869188, i32 5, i64 9223372032559808518, i32 -7, i64 8, i32 2147483647, i64 -9, i32 -2147483648, i64 4503599627370507)
  %h = call i64 @xor12(i32 1, i64 4294967298, i32 -3, i64 -17179869188, i32 5, i64 9223372032559808518, i32 -7, i64 8, i32 2147483647, i64 -9, i32 -2147483648, i64 4503599627370507)
  %x = xor i64 %h, -9218868411457601549
  %bad = icmp ne i64 %x, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define i32 @main() {
  %f0 = call i32 @check_hash()
  %f1 = call i32 @check_short()
  %f2 = call i32 @check_long()
  %f3 = call i32 @check_nested()
  %f4 = call i32 @check_reuse()
  %f5 = call i32 @check_signs()
  %f6 = call i32 @check_forward()
  %f7 = call i32 @check_void()
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
