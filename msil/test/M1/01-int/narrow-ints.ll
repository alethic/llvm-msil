; i1, i8 and i16 values (Milestone 4): they travel as int32 in signatures, so
; a signext/zeroext parameter or result is extended by the side that the
; attribute names (the caller for parameters, the callee for results), and
; without an attribute the receiving side extends it itself; the inputs come
; from truncated int32 values with garbage high bits, so a missing extension
; changes the result. Also i8/i16 arithmetic that wraps, compares, shifts and
; div/rem after promotion to int32. main ORs one bit per noinline check
; function into the exit status (8 bits, D37): [exit 0] means all passed.
; RUN: %{msil_ll}

define internal i32 @sx8_attr(i8 signext %x) noinline {
  %e = sext i8 %x to i32
  ret i32 %e
}

define internal i32 @zx8_attr(i8 zeroext %x) noinline {
  %e = zext i8 %x to i32
  ret i32 %e
}

define internal i32 @sx16_attr(i16 signext %x) noinline {
  %e = sext i16 %x to i32
  ret i32 %e
}

define i32 @zx16_attr(i16 zeroext %x) noinline {
  %e = zext i16 %x to i32
  ret i32 %e
}

; Bit 0: signext/zeroext parameters, extended by the caller.
define internal i32 @args_attr(i32 %v8, i32 %v16) noinline {
  ; v8 = 0x1F0 (i8 0xF0), v16 = 0x18765 (i16 0x8765)
  %t8 = trunc i32 %v8 to i8
  %t16 = trunc i32 %v16 to i16
  %r0 = call i32 @sx8_attr(i8 signext %t8)      ; -16
  %r1 = call i32 @zx8_attr(i8 zeroext %t8)      ; 240
  %r2 = call i32 @sx16_attr(i16 signext %t16)   ; -30875
  %r3 = call i32 @zx16_attr(i16 zeroext %t16)   ; 34661
  %x0 = xor i32 %r0, -16
  %x1 = xor i32 %r1, 240
  %x2 = xor i32 %r2, -30875
  %x3 = xor i32 %r3, 34661
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %bad = icmp ne i32 %o3, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal i32 @sx8_plain(i8 %x) noinline {
  %e = sext i8 %x to i32
  ret i32 %e
}

define i32 @zx8_plain(i8 %x) noinline {
  %e = zext i8 %x to i32
  ret i32 %e
}

define internal i32 @sx16_plain(i16 %x) noinline {
  %e = sext i16 %x to i32
  ret i32 %e
}

define internal i32 @zx16_plain(i16 %x) noinline {
  %e = zext i16 %x to i32
  ret i32 %e
}

; Bit 1: parameters without an attribute, extended by the callee.
define internal i32 @args_plain(i32 %v8, i32 %v16) noinline {
  %t8 = trunc i32 %v8 to i8
  %t16 = trunc i32 %v16 to i16
  %r0 = call i32 @sx8_plain(i8 %t8)             ; -16
  %r1 = call i32 @zx8_plain(i8 %t8)             ; 240
  %r2 = call i32 @sx16_plain(i16 %t16)          ; -30875
  %r3 = call i32 @zx16_plain(i16 %t16)          ; 34661
  %x0 = xor i32 %r0, -16
  %x1 = xor i32 %r1, 240
  %x2 = xor i32 %r2, -30875
  %x3 = xor i32 %r3, 34661
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %bad = icmp ne i32 %o3, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal signext i8 @ret_s8(i32 %x) noinline {
  %t = trunc i32 %x to i8
  ret i8 %t
}

define internal zeroext i8 @ret_z8(i32 %x) noinline {
  %t = trunc i32 %x to i8
  ret i8 %t
}

define signext i16 @ret_s16(i32 %x) noinline {
  %t = trunc i32 %x to i16
  ret i16 %t
}

define internal zeroext i16 @ret_z16(i32 %x) noinline {
  %t = trunc i32 %x to i16
  ret i16 %t
}

; Bit 2: signext/zeroext results, extended by the callee.
define internal i32 @rets_attr(i32 %v8, i32 %v16) noinline {
  %r0 = call signext i8 @ret_s8(i32 %v8)
  %r1 = call zeroext i8 @ret_z8(i32 %v8)
  %r2 = call signext i16 @ret_s16(i32 %v16)
  %r3 = call zeroext i16 @ret_z16(i32 %v16)
  %e0 = sext i8 %r0 to i32                      ; -16
  %e1 = zext i8 %r1 to i32                      ; 240
  %e2 = sext i16 %r2 to i32                     ; -30875
  %e3 = zext i16 %r3 to i32                     ; 34661
  %x0 = xor i32 %e0, -16
  %x1 = xor i32 %e1, 240
  %x2 = xor i32 %e2, -30875
  %x3 = xor i32 %e3, 34661
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %bad = icmp ne i32 %o3, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal i8 @ret_8(i32 %x) noinline {
  %t = trunc i32 %x to i8
  ret i8 %t
}

define internal i16 @ret_16(i32 %x) noinline {
  %t = trunc i32 %x to i16
  ret i16 %t
}

; Bit 3: results without an attribute, extended by the caller.
define internal i32 @rets_plain(i32 %v8, i32 %v16) noinline {
  %r0 = call i8 @ret_8(i32 %v8)
  %r1 = call i16 @ret_16(i32 %v16)
  %e0 = sext i8 %r0 to i32                      ; -16
  %e1 = zext i8 %r0 to i32                      ; 240
  %e2 = sext i16 %r1 to i32                     ; -30875
  %e3 = zext i16 %r1 to i32                     ; 34661
  %x0 = xor i32 %e0, -16
  %x1 = xor i32 %e1, 240
  %x2 = xor i32 %e2, -30875
  %x3 = xor i32 %e3, 34661
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %bad = icmp ne i32 %o3, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal zeroext i1 @isneg(i32 %x) noinline {
  %c = icmp slt i32 %x, 0
  ret i1 %c
}

define internal i1 @odd_plain(i32 %x) noinline {
  %t = trunc i32 %x to i1
  ret i1 %t
}

define internal i32 @b2i_plain(i1 %b) noinline {
  %e = zext i1 %b to i32
  ret i32 %e
}

define internal i32 @b2i_zext(i1 zeroext %b) noinline {
  %e = zext i1 %b to i32
  ret i32 %e
}

define internal i32 @sel_plain(i1 %b, i32 %x, i32 %y) noinline {
  %s = select i1 %b, i32 %x, i32 %y
  ret i32 %s
}

; Bit 4: i1 parameters and results, with and without zeroext; a select on an
; i1 parameter that arrives as 2 (low bit clear) must take the false value.
define internal i32 @bools(i32 %two, i32 %three, i32 %m5) noinline {
  %t2 = trunc i32 %two to i1                    ; false
  %t3 = trunc i32 %three to i1                  ; true
  %r0 = call i32 @b2i_plain(i1 %t2)             ; 0
  %r1 = call i32 @b2i_plain(i1 %t3)             ; 1
  %r2 = call i32 @b2i_zext(i1 zeroext %t3)      ; 1
  %r3 = call i32 @sel_plain(i1 %t2, i32 10, i32 20) ; 20
  %r4 = call i32 @sel_plain(i1 %t3, i32 10, i32 20) ; 10
  %n0 = call zeroext i1 @isneg(i32 %m5)         ; true
  %n1 = call zeroext i1 @isneg(i32 %three)      ; false
  %d0 = call i1 @odd_plain(i32 %three)          ; true
  %d1 = call i1 @odd_plain(i32 %two)            ; false
  %r5 = zext i1 %n0 to i32                      ; 1
  %r6 = sext i1 %n0 to i32                      ; -1
  %r7 = zext i1 %n1 to i32                      ; 0
  %r8 = zext i1 %d0 to i32                      ; 1
  %r9 = zext i1 %d1 to i32                      ; 0
  %x1 = xor i32 %r1, 1
  %x2 = xor i32 %r2, 1
  %x3 = xor i32 %r3, 20
  %x4 = xor i32 %r4, 10
  %x5 = xor i32 %r5, 1
  %x6 = xor i32 %r6, -1
  %x8 = xor i32 %r8, 1
  %o1 = or i32 %r0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %o6 = or i32 %o5, %x6
  %o7 = or i32 %o6, %r7
  %o8 = or i32 %o7, %x8
  %o9 = or i32 %o8, %r9
  %bad = icmp ne i32 %o9, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 5: i8 and i16 add, sub and mul wrap at their width.
define internal i32 @arith(i8 %a, i8 %b, i8 %c, i8 %d, i16 %e, i16 %f) noinline {
  ; a = 127, b = 1, c = 16, d = -128, e = 32767, f = 300
  %s0 = add i8 %a, %b
  %s1 = mul i8 %c, %c
  %s2 = sub i8 0, %d
  %s3 = mul i8 %a, %a
  %s4 = sub i8 %b, %a
  %s5 = add i16 %e, 1
  %s6 = mul i16 %f, %f
  %s7 = mul i16 %e, %f
  %e0 = sext i8 %s0 to i32                      ; -128
  %e1 = sext i8 %s1 to i32                      ; 0
  %e2 = sext i8 %s2 to i32                      ; -128
  %e3 = sext i8 %s3 to i32                      ; 1
  %e4 = sext i8 %s4 to i32                      ; -126
  %e5 = zext i8 %s0 to i32                      ; 128
  %e6 = sext i16 %s5 to i32                     ; -32768
  %e7 = sext i16 %s6 to i32                     ; 24464
  %e8 = sext i16 %s7 to i32                     ; -300
  %e9 = zext i16 %s5 to i32                     ; 32768
  %x0 = xor i32 %e0, -128
  %x2 = xor i32 %e2, -128
  %x3 = xor i32 %e3, 1
  %x4 = xor i32 %e4, -126
  %x5 = xor i32 %e5, 128
  %x6 = xor i32 %e6, -32768
  %x7 = xor i32 %e7, 24464
  %x8 = xor i32 %e8, -300
  %x9 = xor i32 %e9, 32768
  %o1 = or i32 %x0, %e1
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

; Bit 6: i8 and i16 compares (signed and unsigned order differ for 0x80 and
; 0x8001) and shifts (lshr shifts in zeros at bit 7 or 15, ashr copies it).
define internal i32 @cmpshift(i8 %a, i8 %b, i16 %h, i8 %n) noinline {
  ; a = -128 (0x80), b = 127, h = 0x8001, n = 3
  %c0 = icmp slt i8 %a, %b                      ; 1
  %c1 = icmp ult i8 %a, %b                      ; 0
  %c2 = icmp sgt i8 %a, -1                      ; 0
  %c3 = icmp ugt i8 %a, 127                     ; 1
  %c4 = icmp eq i8 %a, -128                     ; 1
  %c5 = icmp slt i16 %h, 0                      ; 1
  %c6 = icmp ult i16 %h, -32768                 ; 0
  %c7 = icmp ugt i16 %h, -32768                 ; 1
  %z0 = zext i1 %c0 to i32
  %z1 = zext i1 %c1 to i32
  %z2 = zext i1 %c2 to i32
  %z3 = zext i1 %c3 to i32
  %z4 = zext i1 %c4 to i32
  %z5 = zext i1 %c5 to i32
  %z6 = zext i1 %c6 to i32
  %z7 = zext i1 %c7 to i32
  %w1 = shl i32 %z1, 1
  %w2 = shl i32 %z2, 2
  %w3 = shl i32 %z3, 3
  %w4 = shl i32 %z4, 4
  %w5 = shl i32 %z5, 5
  %w6 = shl i32 %z6, 6
  %w7 = shl i32 %z7, 7
  %p1 = or i32 %z0, %w1
  %p2 = or i32 %p1, %w2
  %p3 = or i32 %p2, %w3
  %p4 = or i32 %p3, %w4
  %p5 = or i32 %p4, %w5
  %p6 = or i32 %p5, %w6
  %p7 = or i32 %p6, %w7
  %xp = xor i32 %p7, 185                        ; 0b1011_1001
  %h0 = lshr i16 %h, 15
  %h1 = ashr i16 %h, 15
  %h2 = lshr i8 %a, 4
  %h3 = ashr i8 %a, 7
  %h4 = shl i16 %h, 1
  %h5 = shl i8 %b, 1
  %h6 = lshr i8 %a, %n
  %e0 = zext i16 %h0 to i32                     ; 1
  %e1 = sext i16 %h1 to i32                     ; -1
  %e2 = zext i8 %h2 to i32                      ; 8
  %e3 = sext i8 %h3 to i32                      ; -1
  %e4 = sext i16 %h4 to i32                     ; 2
  %e5 = sext i8 %h5 to i32                      ; -2
  %e6 = zext i8 %h6 to i32                      ; 16
  %x0 = xor i32 %e0, 1
  %x1 = xor i32 %e1, -1
  %x2 = xor i32 %e2, 8
  %x3 = xor i32 %e3, -1
  %x4 = xor i32 %e4, 2
  %x5 = xor i32 %e5, -2
  %x6 = xor i32 %e6, 16
  %o0 = or i32 %xp, %x0
  %o1 = or i32 %o0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %o6 = or i32 %o5, %x6
  %bad = icmp ne i32 %o6, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 7: i8 and i16 sdiv, srem, udiv and urem.
define internal i32 @divrem(i8 %a, i8 %b, i16 %c, i16 %d, i8 %m1) noinline {
  ; a = -100, b = 7, c = -30000, d = 7, m1 = -1
  %q0 = sdiv i8 %a, %b
  %q1 = srem i8 %a, %b
  %q2 = udiv i8 %a, %b
  %q3 = urem i8 %a, %b
  %q4 = sdiv i16 %c, %d
  %q5 = srem i16 %c, %d
  %q6 = udiv i16 %c, %d
  %q7 = urem i16 %c, %d
  %q8 = sdiv i8 %a, %m1
  %e0 = sext i8 %q0 to i32                      ; -14
  %e1 = sext i8 %q1 to i32                      ; -2
  %e2 = zext i8 %q2 to i32                      ; 22
  %e3 = zext i8 %q3 to i32                      ; 2
  %e4 = sext i16 %q4 to i32                     ; -4285
  %e5 = sext i16 %q5 to i32                     ; -5
  %e6 = zext i16 %q6 to i32                     ; 5076
  %e7 = zext i16 %q7 to i32                     ; 4
  %e8 = sext i8 %q8 to i32                      ; 100
  %x0 = xor i32 %e0, -14
  %x1 = xor i32 %e1, -2
  %x2 = xor i32 %e2, 22
  %x3 = xor i32 %e3, 2
  %x4 = xor i32 %e4, -4285
  %x5 = xor i32 %e5, -5
  %x6 = xor i32 %e6, 5076
  %x7 = xor i32 %e7, 4
  %x8 = xor i32 %e8, 100
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

define i32 @main() {
  %f0 = call i32 @args_attr(i32 496, i32 100197)
  %f1 = call i32 @args_plain(i32 496, i32 100197)
  %f2 = call i32 @rets_attr(i32 496, i32 100197)
  %f3 = call i32 @rets_plain(i32 496, i32 100197)
  %f4 = call i32 @bools(i32 2, i32 3, i32 -5)
  %f5 = call i32 @arith(i8 127, i8 1, i8 16, i8 -128, i16 32767, i16 300)
  %f6 = call i32 @cmpshift(i8 -128, i8 127, i16 -32767, i8 3)
  %f7 = call i32 @divrem(i8 -100, i8 7, i16 -30000, i16 7, i8 -1)
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
