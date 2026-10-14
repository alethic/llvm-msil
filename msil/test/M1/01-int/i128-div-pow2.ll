; i128 sdiv and srem by constant powers of two, udiv and urem by 16, and exact
; sdiv and udiv by 10 (Milestone 4). With no runtime helper (__divti3,
; __modti3, __udivti3, __umodti3) the backend must expand these inline: sdiv
; and srem by a power of two into shifts, as LLVM's generic lowering does when
; integer division is not cheap (MSIL's isIntDivCheap is false above 64 bits),
; udiv and urem by a power of two into a shift and a mask, and an exact
; division into a shift and a multiply by the inverse (plan section 12, rows
; 'Plan 5.3 amended (D33 query hook), M4.b' and its correction). C rounds the
; quotient toward zero, so -7 / 4 is -1 and -7 % 4 is -3, never the -2 and 1
; of a plain arithmetic shift and mask. Divisors 2, 4, -4, 2^64 (the quotient
; comes from the high half) and 2^126, and an exact sdiv by 4; unsigned
; division and remainder by 16 of dividends with the top bit set and all ones;
; exact signed and unsigned division by 10 of multiples of 10 up to the ends of
; both ranges. The noinline helpers receive the dividend and the result C
; gives as 64-bit halves (lo, hi), so llc cannot fold anything even at -O2,
; and return 1 when the result differs. Every check function covers one case
; class and sets one bit of the exit status (8 bits, D37; bits 0 and 3 have two
; check functions each): [exit 0] means every check passed and bit N names
; group N.
; RUN: %{msil_ll}

; x / 4
define internal i32 @sdiv_4(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = sdiv i128 %x, 4
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x % 4
define internal i32 @srem_4(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = srem i128 %x, 4
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x / 2
define internal i32 @sdiv_2(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = sdiv i128 %x, 2
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x % 2
define internal i32 @srem_2(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = srem i128 %x, 2
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x / 2^64
define internal i32 @sdiv_2p64(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = sdiv i128 %x, 18446744073709551616
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x % 2^64
define internal i32 @srem_2p64(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = srem i128 %x, 18446744073709551616
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x / -4
define internal i32 @sdiv_m4(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = sdiv i128 %x, -4
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x % -4
define internal i32 @srem_m4(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = srem i128 %x, -4
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x / 2^126
define internal i32 @sdiv_2p126(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = sdiv i128 %x, 85070591730234615865843651857942052864
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x % 2^126
define internal i32 @srem_2p126(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = srem i128 %x, 85070591730234615865843651857942052864
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x / 4 (exact: x is a multiple of 4)
define internal i32 @sdiv_exact_4(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = sdiv exact i128 %x, 4
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x / 16 (unsigned)
define internal i32 @udiv_16(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = udiv i128 %x, 16
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x % 16 (unsigned)
define internal i32 @urem_16(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = urem i128 %x, 16
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x / 10 (exact: x is a multiple of 10)
define internal i32 @sdiv_exact_10(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = sdiv exact i128 %x, 10
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; x / 10 (unsigned, exact: x is a multiple of 10)
define internal i32 @udiv_exact_10(i64 %xlo, i64 %xhi, i64 %elo, i64 %ehi) noinline {
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %q = udiv exact i128 %x, 10
  %bad = icmp ne i128 %q, %e
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 0: non-negative dividends / 4.
define internal i32 @pos_div_4() noinline {
  ; 7 / 4 = 1
  %r0 = call i32 @sdiv_4(i64 7, i64 0, i64 1, i64 0)
  ; 8 / 4 = 2
  %r1 = call i32 @sdiv_4(i64 8, i64 0, i64 2, i64 0)
  ; 3 / 4 = 0
  %r2 = call i32 @sdiv_4(i64 3, i64 0, i64 0, i64 0)
  ; 0 / 4 = 0
  %r3 = call i32 @sdiv_4(i64 0, i64 0, i64 0, i64 0)
  ; (2^100 + 7) / 4 = (2^98 + 1)
  %r4 = call i32 @sdiv_4(i64 7, i64 68719476736, i64 1, i64 17179869184)
  ; INT128_MAX / 4 = (2^125 - 1)
  %r5 = call i32 @sdiv_4(i64 -1, i64 9223372036854775807, i64 -1, i64 2305843009213693951)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  ret i32 %o5
}

; Bit 0 too: negative dividends / 4 round toward zero.
define internal i32 @neg_div_4() noinline {
  ; -7 / 4 = -1
  %r0 = call i32 @sdiv_4(i64 -7, i64 -1, i64 -1, i64 -1)
  ; -8 / 4 = -2
  %r1 = call i32 @sdiv_4(i64 -8, i64 -1, i64 -2, i64 -1)
  ; -1 / 4 = 0
  %r2 = call i32 @sdiv_4(i64 -1, i64 -1, i64 0, i64 0)
  ; -3 / 4 = 0
  %r3 = call i32 @sdiv_4(i64 -3, i64 -1, i64 0, i64 0)
  ; -(2^100 + 7) / 4 = -(2^98 + 1)
  %r4 = call i32 @sdiv_4(i64 -7, i64 -68719476737, i64 -1, i64 -17179869185)
  ; INT128_MIN / 4 = -2^125
  %r5 = call i32 @sdiv_4(i64 0, i64 -9223372036854775808, i64 0, i64 -2305843009213693952)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  ret i32 %o5
}

; Bit 1: % 4 takes the sign of the dividend.
define internal i32 @rem_4() noinline {
  ; 7 % 4 = 3
  %r0 = call i32 @srem_4(i64 7, i64 0, i64 3, i64 0)
  ; -7 % 4 = -3
  %r1 = call i32 @srem_4(i64 -7, i64 -1, i64 -3, i64 -1)
  ; -8 % 4 = 0
  %r2 = call i32 @srem_4(i64 -8, i64 -1, i64 0, i64 0)
  ; -1 % 4 = -1
  %r3 = call i32 @srem_4(i64 -1, i64 -1, i64 -1, i64 -1)
  ; -(2^100 + 7) % 4 = -3
  %r4 = call i32 @srem_4(i64 -7, i64 -68719476737, i64 -3, i64 -1)
  ; INT128_MAX % 4 = 3
  %r5 = call i32 @srem_4(i64 -1, i64 9223372036854775807, i64 3, i64 0)
  ; INT128_MIN % 4 = 0
  %r6 = call i32 @srem_4(i64 0, i64 -9223372036854775808, i64 0, i64 0)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  %o6 = or i32 %o5, %r6
  ret i32 %o6
}

; Bit 2: / 2 and % 2.
define internal i32 @divrem_2() noinline {
  ; 7 / 2 = 3
  %r0 = call i32 @sdiv_2(i64 7, i64 0, i64 3, i64 0)
  ; -7 / 2 = -3
  %r1 = call i32 @sdiv_2(i64 -7, i64 -1, i64 -3, i64 -1)
  ; -1 / 2 = 0
  %r2 = call i32 @sdiv_2(i64 -1, i64 -1, i64 0, i64 0)
  ; INT128_MIN / 2 = -2^126
  %r3 = call i32 @sdiv_2(i64 0, i64 -9223372036854775808, i64 0, i64 -4611686018427387904)
  ; 7 % 2 = 1
  %r4 = call i32 @srem_2(i64 7, i64 0, i64 1, i64 0)
  ; -7 % 2 = -1
  %r5 = call i32 @srem_2(i64 -7, i64 -1, i64 -1, i64 -1)
  ; -1 % 2 = -1
  %r6 = call i32 @srem_2(i64 -1, i64 -1, i64 -1, i64 -1)
  ; INT128_MIN % 2 = 0
  %r7 = call i32 @srem_2(i64 0, i64 -9223372036854775808, i64 0, i64 0)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  %o6 = or i32 %o5, %r6
  %o7 = or i32 %o6, %r7
  ret i32 %o7
}

; Bit 3: / 2^64, where the quotient comes from the high half.
define internal i32 @div_2p64() noinline {
  ; -1 / 2^64 = 0
  %r0 = call i32 @sdiv_2p64(i64 -1, i64 -1, i64 0, i64 0)
  ; -(2^64 + 1) / 2^64 = -1
  %r1 = call i32 @sdiv_2p64(i64 -1, i64 -2, i64 -1, i64 -1)
  ; (5*2^64 + 3) / 2^64 = 5
  %r2 = call i32 @sdiv_2p64(i64 3, i64 5, i64 5, i64 0)
  ; -(5*2^64 + 3) / 2^64 = -5
  %r3 = call i32 @sdiv_2p64(i64 -3, i64 -6, i64 -5, i64 -1)
  ; -2^64 / 2^64 = -1
  %r4 = call i32 @sdiv_2p64(i64 0, i64 -1, i64 -1, i64 -1)
  ; INT128_MIN / 2^64 = -2^63
  %r5 = call i32 @sdiv_2p64(i64 0, i64 -9223372036854775808, i64 -9223372036854775808, i64 -1)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  ret i32 %o5
}

; Bit 3 too: % 2^64.
define internal i32 @rem_2p64() noinline {
  ; (5*2^64 + 3) % 2^64 = 3
  %r0 = call i32 @srem_2p64(i64 3, i64 5, i64 3, i64 0)
  ; -(5*2^64 + 3) % 2^64 = -3
  %r1 = call i32 @srem_2p64(i64 -3, i64 -6, i64 -3, i64 -1)
  ; -2^64 % 2^64 = 0
  %r2 = call i32 @srem_2p64(i64 0, i64 -1, i64 0, i64 0)
  ; -1 % 2^64 = -1
  %r3 = call i32 @srem_2p64(i64 -1, i64 -1, i64 -1, i64 -1)
  ; INT128_MIN % 2^64 = 0
  %r4 = call i32 @srem_2p64(i64 0, i64 -9223372036854775808, i64 0, i64 0)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  ret i32 %o4
}

; Bit 4: / -4 and % -4 (C: the remainder takes the sign of the dividend).
define internal i32 @divrem_m4() noinline {
  ; -7 / -4 = 1
  %r0 = call i32 @sdiv_m4(i64 -7, i64 -1, i64 1, i64 0)
  ; 7 / -4 = -1
  %r1 = call i32 @sdiv_m4(i64 7, i64 0, i64 -1, i64 -1)
  ; -8 / -4 = 2
  %r2 = call i32 @sdiv_m4(i64 -8, i64 -1, i64 2, i64 0)
  ; INT128_MIN / -4 = 2^125
  %r3 = call i32 @sdiv_m4(i64 0, i64 -9223372036854775808, i64 0, i64 2305843009213693952)
  ; 7 % -4 = 3
  %r4 = call i32 @srem_m4(i64 7, i64 0, i64 3, i64 0)
  ; -7 % -4 = -3
  %r5 = call i32 @srem_m4(i64 -7, i64 -1, i64 -3, i64 -1)
  ; -8 % -4 = 0
  %r6 = call i32 @srem_m4(i64 -8, i64 -1, i64 0, i64 0)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  %o6 = or i32 %o5, %r6
  ret i32 %o6
}

; Bit 5: / 2^126, % 2^126, and an exact / 4.
define internal i32 @misc() noinline {
  ; (2^126 + 5) / 2^126 = 1
  %r0 = call i32 @sdiv_2p126(i64 5, i64 4611686018427387904, i64 1, i64 0)
  ; -(2^126 + 5) / 2^126 = -1
  %r1 = call i32 @sdiv_2p126(i64 -5, i64 -4611686018427387905, i64 -1, i64 -1)
  ; -5 / 2^126 = 0
  %r2 = call i32 @sdiv_2p126(i64 -5, i64 -1, i64 0, i64 0)
  ; INT128_MIN / 2^126 = -2
  %r3 = call i32 @sdiv_2p126(i64 0, i64 -9223372036854775808, i64 -2, i64 -1)
  ; -(2^126 + 5) % 2^126 = -5
  %r4 = call i32 @srem_2p126(i64 -5, i64 -4611686018427387905, i64 -5, i64 -1)
  ; (2^126 + 5) % 2^126 = 5
  %r5 = call i32 @srem_2p126(i64 5, i64 4611686018427387904, i64 5, i64 0)
  ; -8 / 4 = -2
  %r6 = call i32 @sdiv_exact_4(i64 -8, i64 -1, i64 -2, i64 -1)
  ; 2^100 / 4 = 2^98
  %r7 = call i32 @sdiv_exact_4(i64 0, i64 68719476736, i64 0, i64 17179869184)
  ; -2^100 / 4 = -2^98
  %r8 = call i32 @sdiv_exact_4(i64 0, i64 -68719476736, i64 0, i64 -17179869184)
  ; INT128_MIN / 4 = -2^125
  %r9 = call i32 @sdiv_exact_4(i64 0, i64 -9223372036854775808, i64 0, i64 -2305843009213693952)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  %o6 = or i32 %o5, %r6
  %o7 = or i32 %o6, %r7
  %o8 = or i32 %o7, %r8
  %o9 = or i32 %o8, %r9
  ret i32 %o9
}

; Bit 6: unsigned / 16 and % 16 (a shift and a mask), top bit set and all ones
; included.
define internal i32 @divrem_u16() noinline {
  ; (2^127 + 0x35) / 16 = 2^123 + 3 (top bit set)
  %r0 = call i32 @udiv_16(i64 53, i64 -9223372036854775808, i64 3, i64 576460752303423488)
  ; (2^127 + 0x35) % 16 = 5
  %r1 = call i32 @urem_16(i64 53, i64 -9223372036854775808, i64 5, i64 0)
  ; (2^128 - 1) / 16 = 2^124 - 1 (all ones)
  %r2 = call i32 @udiv_16(i64 -1, i64 -1, i64 -1, i64 1152921504606846975)
  ; (2^128 - 1) % 16 = 15
  %r3 = call i32 @urem_16(i64 -1, i64 -1, i64 15, i64 0)
  ; (2^124 + 7) / 16 = 2^120
  %r4 = call i32 @udiv_16(i64 7, i64 1152921504606846976, i64 0, i64 72057594037927936)
  ; (2^124 + 7) % 16 = 7
  %r5 = call i32 @urem_16(i64 7, i64 1152921504606846976, i64 7, i64 0)
  ; (30*2^64 + 50) / 16 = 15*2^61 + 3 (bits move to the low half)
  %r6 = call i32 @udiv_16(i64 50, i64 30, i64 -2305843009213693949, i64 1)
  ; (30*2^64 + 50) % 16 = 2
  %r7 = call i32 @urem_16(i64 50, i64 30, i64 2, i64 0)
  ; 2^64 / 16 = 2^60
  %r8 = call i32 @udiv_16(i64 0, i64 1, i64 1152921504606846976, i64 0)
  ; 2^64 % 16 = 0
  %r9 = call i32 @urem_16(i64 0, i64 1, i64 0, i64 0)
  ; 15 / 16 = 0
  %r10 = call i32 @udiv_16(i64 15, i64 0, i64 0, i64 0)
  ; 15 % 16 = 15
  %r11 = call i32 @urem_16(i64 15, i64 0, i64 15, i64 0)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  %o6 = or i32 %o5, %r6
  %o7 = or i32 %o6, %r7
  %o8 = or i32 %o7, %r8
  %o9 = or i32 %o8, %r9
  %o10 = or i32 %o9, %r10
  %o11 = or i32 %o10, %r11
  ret i32 %o11
}

; Bit 7: exact / 10, signed and unsigned (a shift by one and a multiply by the
; inverse of 5), negative multiples and multiples with the top bit set included.
define internal i32 @div_exact_10() noinline {
  ; -30 / 10 = -3
  %r0 = call i32 @sdiv_exact_10(i64 -30, i64 -1, i64 -3, i64 -1)
  ; -10*(3*2^64 + 5) / 10 = -(3*2^64 + 5)
  %r1 = call i32 @sdiv_exact_10(i64 -50, i64 -31, i64 -5, i64 -4)
  ; 10*(3*2^64 + 5) / 10 = 3*2^64 + 5
  %r2 = call i32 @sdiv_exact_10(i64 50, i64 30, i64 5, i64 3)
  ; 10*(-2^123 + 7) / 10 = -2^123 + 7
  %r3 = call i32 @sdiv_exact_10(i64 70, i64 -5764607523034234880, i64 7, i64 -576460752303423488)
  ; 10*(2^122 - 1) / 10 = 2^122 - 1
  %r4 = call i32 @sdiv_exact_10(i64 -10, i64 2882303761517117439, i64 -1, i64 288230376151711743)
  ; (INT128_MIN + 8) / 10 = -17014118346046923173168730371588410572 (the dividend is the most negative multiple of 10)
  %r5 = call i32 @sdiv_exact_10(i64 8, i64 -9223372036854775808, i64 3689348814741910324, i64 -922337203685477581)
  ; (INT128_MAX - 7) / 10 = 17014118346046923173168730371588410572 (the dividend is the largest multiple of 10)
  %r6 = call i32 @sdiv_exact_10(i64 -8, i64 9223372036854775807, i64 -3689348814741910324, i64 922337203685477580)
  ; 0 / 10 = 0
  %r7 = call i32 @sdiv_exact_10(i64 0, i64 0, i64 0, i64 0)
  ; 10*(2^124 + 7) / 10 = 2^124 + 7 (the dividend 2^127 + 2^125 + 70 has the top bit set)
  %r8 = call i32 @udiv_exact_10(i64 70, i64 -6917529027641081856, i64 7, i64 1152921504606846976)
  ; (2^128 - 6) / 10 = 34028236692093846346337460743176821145 (the dividend is the largest unsigned multiple of 10)
  %r9 = call i32 @udiv_exact_10(i64 -6, i64 -1, i64 -7378697629483820647, i64 1844674407370955161)
  ; 10*(3*2^64 + 5) / 10 = 3*2^64 + 5
  %r10 = call i32 @udiv_exact_10(i64 50, i64 30, i64 5, i64 3)
  ; 10 / 10 = 1
  %r11 = call i32 @udiv_exact_10(i64 10, i64 0, i64 1, i64 0)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  %o6 = or i32 %o5, %r6
  %o7 = or i32 %o6, %r7
  %o8 = or i32 %o7, %r8
  %o9 = or i32 %o8, %r9
  %o10 = or i32 %o9, %r10
  %o11 = or i32 %o10, %r11
  ret i32 %o11
}

define i32 @main() {
  %c0a = call i32 @pos_div_4()
  %c0b = call i32 @neg_div_4()
  %c1 = call i32 @rem_4()
  %c2 = call i32 @divrem_2()
  %c3a = call i32 @div_2p64()
  %c3b = call i32 @rem_2p64()
  %c4 = call i32 @divrem_m4()
  %c5 = call i32 @misc()
  %c6 = call i32 @divrem_u16()
  %c7 = call i32 @div_exact_10()
  %c0 = or i32 %c0a, %c0b
  %c3 = or i32 %c3a, %c3b
  %s1 = shl i32 %c1, 1
  %s2 = shl i32 %c2, 2
  %s3 = shl i32 %c3, 3
  %s4 = shl i32 %c4, 4
  %s5 = shl i32 %c5, 5
  %s6 = shl i32 %c6, 6
  %s7 = shl i32 %c7, 7
  %m1 = or i32 %c0, %s1
  %m2 = or i32 %m1, %s2
  %m3 = or i32 %m2, %s3
  %m4 = or i32 %m3, %s4
  %m5 = or i32 %m4, %s5
  %m6 = or i32 %m5, %s6
  %m7 = or i32 %m6, %s7
  ret i32 %m7
}
