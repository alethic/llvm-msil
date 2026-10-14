; Multiplication with overflow check (Milestone 4): umul.with.overflow and
; smul.with.overflow on i64 and i128 (__builtin_mul_overflow), which the
; backend expands inline because MULO_I64 and MULO_I128 have no runtime
; helper (isel decision 10). The noinline helpers receive the operands, the
; product C gives (the low 64 or 128 bits, also on overflow) and the expected
; overflow flag as arguments, so llc cannot fold anything even at -O2; each
; returns 1 when the product or the flag differs. i128 values travel as
; 64-bit halves (lo, hi) and are rebuilt inside the helpers, so every
; signature stays scalar. Every check function covers one case class and sets
; one bit of the exit status (8 bits, D37): [exit 0] means every check passed
; and bit N names check N.
; RUN: %{msil_ll}

declare { i64, i1 } @llvm.umul.with.overflow.i64(i64, i64)
declare { i64, i1 } @llvm.smul.with.overflow.i64(i64, i64)
declare { i128, i1 } @llvm.umul.with.overflow.i128(i128, i128)
declare { i128, i1 } @llvm.smul.with.overflow.i128(i128, i128)

define internal i32 @umulo64(i64 %a, i64 %b, i64 %e, i32 %eo) noinline {
  %p = call { i64, i1 } @llvm.umul.with.overflow.i64(i64 %a, i64 %b)
  %v = extractvalue { i64, i1 } %p, 0
  %o = extractvalue { i64, i1 } %p, 1
  %bv = icmp ne i64 %v, %e
  %oz = zext i1 %o to i32
  %bo = icmp ne i32 %oz, %eo
  %bad = or i1 %bv, %bo
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal i32 @smulo64(i64 %a, i64 %b, i64 %e, i32 %eo) noinline {
  %p = call { i64, i1 } @llvm.smul.with.overflow.i64(i64 %a, i64 %b)
  %v = extractvalue { i64, i1 } %p, 0
  %o = extractvalue { i64, i1 } %p, 1
  %bv = icmp ne i64 %v, %e
  %oz = zext i1 %o to i32
  %bo = icmp ne i32 %oz, %eo
  %bad = or i1 %bv, %bo
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal i32 @umulo128(i64 %alo, i64 %ahi, i64 %blo, i64 %bhi, i64 %elo, i64 %ehi, i32 %eo) noinline {
  %a.l = zext i64 %alo to i128
  %a.h0 = zext i64 %ahi to i128
  %a.h = shl i128 %a.h0, 64
  %a = or i128 %a.h, %a.l
  %b.l = zext i64 %blo to i128
  %b.h0 = zext i64 %bhi to i128
  %b.h = shl i128 %b.h0, 64
  %b = or i128 %b.h, %b.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %p = call { i128, i1 } @llvm.umul.with.overflow.i128(i128 %a, i128 %b)
  %v = extractvalue { i128, i1 } %p, 0
  %o = extractvalue { i128, i1 } %p, 1
  %bv = icmp ne i128 %v, %e
  %oz = zext i1 %o to i32
  %bo = icmp ne i32 %oz, %eo
  %bad = or i1 %bv, %bo
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal i32 @smulo128(i64 %alo, i64 %ahi, i64 %blo, i64 %bhi, i64 %elo, i64 %ehi, i32 %eo) noinline {
  %a.l = zext i64 %alo to i128
  %a.h0 = zext i64 %ahi to i128
  %a.h = shl i128 %a.h0, 64
  %a = or i128 %a.h, %a.l
  %b.l = zext i64 %blo to i128
  %b.h0 = zext i64 %bhi to i128
  %b.h = shl i128 %b.h0, 64
  %b = or i128 %b.h, %b.l
  %e.l = zext i64 %elo to i128
  %e.h0 = zext i64 %ehi to i128
  %e.h = shl i128 %e.h0, 64
  %e = or i128 %e.h, %e.l
  %p = call { i128, i1 } @llvm.smul.with.overflow.i128(i128 %a, i128 %b)
  %v = extractvalue { i128, i1 } %p, 0
  %o = extractvalue { i128, i1 } %p, 1
  %bv = icmp ne i128 %v, %e
  %oz = zext i1 %o to i32
  %bo = icmp ne i32 %oz, %eo
  %bad = or i1 %bv, %bo
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 0: unsigned 64-bit products that fit.
define internal i32 @umulo64_fits() noinline {
  ; (2^32-1) * (2^32-1) = 0xfffffffe00000001
  %r0 = call i32 @umulo64(i64 4294967295, i64 4294967295, i64 -8589934591, i32 0)
  ; 0 * UINT64_MAX = 0
  %r1 = call i32 @umulo64(i64 0, i64 -1, i64 0, i32 0)
  ; 1 * UINT64_MAX = UINT64_MAX
  %r2 = call i32 @umulo64(i64 1, i64 -1, i64 -1, i32 0)
  ; 2^63 * 1 = 2^63
  %r3 = call i32 @umulo64(i64 -9223372036854775808, i64 1, i64 -9223372036854775808, i32 0)
  ; 3 * 0x5555555555555555 = UINT64_MAX
  %r4 = call i32 @umulo64(i64 3, i64 6148914691236517205, i64 -1, i32 0)
  ; 0x123456789 * 0xabcdef0 = 0x0c379aaaa375de70
  %r5 = call i32 @umulo64(i64 4886718345, i64 180150000, i64 880342309851750000, i32 0)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  ret i32 %o5
}

; Bit 1: unsigned 64-bit products that overflow (the low 64 bits remain).
define internal i32 @umulo64_overflows() noinline {
  ; 2^32 * 2^32 = 2^64 -> 0
  %r0 = call i32 @umulo64(i64 4294967296, i64 4294967296, i64 0, i32 1)
  ; UINT64_MAX * 2 -> 0xfffffffffffffffe
  %r1 = call i32 @umulo64(i64 -1, i64 2, i64 -2, i32 1)
  ; 2^63 * 2 -> 0
  %r2 = call i32 @umulo64(i64 -9223372036854775808, i64 2, i64 0, i32 1)
  ; UINT64_MAX * UINT64_MAX -> 1
  %r3 = call i32 @umulo64(i64 -1, i64 -1, i64 1, i32 1)
  ; 0x5555555555555556 * 3 = 2^64 + 2 -> 2
  %r4 = call i32 @umulo64(i64 6148914691236517206, i64 3, i64 2, i32 1)
  ; 0x123456789ab * 0xabcdef01 -> 0x79aaabcd65dd2eab
  %r5 = call i32 @umulo64(i64 1250999896491, i64 2882400001, i64 8767008523295731371, i32 1)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  ret i32 %o5
}

; Bit 2: signed 64-bit products that overflow.
define internal i32 @smulo64_overflows() noinline {
  ; INT64_MIN * -1 -> INT64_MIN
  %r0 = call i32 @smulo64(i64 -9223372036854775808, i64 -1, i64 -9223372036854775808, i32 1)
  ; -1 * INT64_MIN -> INT64_MIN
  %r1 = call i32 @smulo64(i64 -1, i64 -9223372036854775808, i64 -9223372036854775808, i32 1)
  ; 2^62 * 2 = 2^63 -> INT64_MIN
  %r2 = call i32 @smulo64(i64 4611686018427387904, i64 2, i64 -9223372036854775808, i32 1)
  ; INT64_MAX * 2 -> -2
  %r3 = call i32 @smulo64(i64 9223372036854775807, i64 2, i64 -2, i32 1)
  ; INT64_MIN * INT64_MIN = 2^126 -> 0
  %r4 = call i32 @smulo64(i64 -9223372036854775808, i64 -9223372036854775808, i64 0, i32 1)
  ; -2^32 * 2^32 = -2^64 -> 0
  %r5 = call i32 @smulo64(i64 -4294967296, i64 4294967296, i64 0, i32 1)
  ; 3037000500 * 3037000500 = 9223372037000250000 > INT64_MAX
  %r6 = call i32 @smulo64(i64 3037000500, i64 3037000500, i64 -9223372036709301616, i32 1)
  ; 0x123456789ab * -0xabcdef01 -> -0x79aaabcd65dd2eab
  %r7 = call i32 @smulo64(i64 1250999896491, i64 -2882400001, i64 -8767008523295731371, i32 1)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  %o6 = or i32 %o5, %r6
  %o7 = or i32 %o6, %r7
  ret i32 %o7
}

; Bit 3: signed 64-bit products that fit, several of them at the limits.
define internal i32 @smulo64_fits() noinline {
  ; 2^62 * -2 = -2^63 = INT64_MIN
  %r0 = call i32 @smulo64(i64 4611686018427387904, i64 -2, i64 -9223372036854775808, i32 0)
  ; -3 * 5 = -15
  %r1 = call i32 @smulo64(i64 -3, i64 5, i64 -15, i32 0)
  ; INT64_MIN * 1 = INT64_MIN
  %r2 = call i32 @smulo64(i64 -9223372036854775808, i64 1, i64 -9223372036854775808, i32 0)
  ; INT64_MAX * -1 = -INT64_MAX
  %r3 = call i32 @smulo64(i64 9223372036854775807, i64 -1, i64 -9223372036854775807, i32 0)
  ; -2^31 * 2^32 = INT64_MIN
  %r4 = call i32 @smulo64(i64 -2147483648, i64 4294967296, i64 -9223372036854775808, i32 0)
  ; -1 * -1 = 1
  %r5 = call i32 @smulo64(i64 -1, i64 -1, i64 1, i32 0)
  ; 3037000499 * 3037000499 = 9223372030926249001 <= INT64_MAX
  %r6 = call i32 @smulo64(i64 3037000499, i64 3037000499, i64 9223372030926249001, i32 0)
  ; -0x123456789 * 0xabcdef0 = -0x0c379aaaa375de70
  %r7 = call i32 @smulo64(i64 -4886718345, i64 180150000, i64 -880342309851750000, i32 0)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  %o6 = or i32 %o5, %r6
  %o7 = or i32 %o6, %r7
  ret i32 %o7
}

; Bit 4: unsigned 128-bit products that fit. Arguments: a.lo, a.hi, b.lo,
; b.hi, product.lo, product.hi, overflow.
define internal i32 @umulo128_fits() noinline {
  ; (2^64-1) * (2^64+1) = 2^128-1
  %r0 = call i32 @umulo128(i64 -1, i64 0, i64 1, i64 1, i64 -1, i64 -1, i32 0)
  ; 2^64 * (2^64-1) = 0xffffffffffffffff_0000000000000000
  %r1 = call i32 @umulo128(i64 0, i64 1, i64 -1, i64 0, i64 0, i64 -1, i32 0)
  ; 2^127 * 1 = 2^127
  %r2 = call i32 @umulo128(i64 0, i64 -9223372036854775808, i64 1, i64 0, i64 0, i64 -9223372036854775808, i32 0)
  ; 0 * (2^128-1) = 0
  %r3 = call i32 @umulo128(i64 0, i64 0, i64 -1, i64 -1, i64 0, i64 0, i32 0)
  ; 0x1_23456789abcdef01 * 0x0fedcba987654321
  ;   = 0x121fa00ad77d7422_335b54a7dd7e1221
  %r4 = call i32 @umulo128(i64 2541551405711093505, i64 1, i64 1147797409030816545, i64 0, i64 3700644598767292961, i64 1305938385386173474, i32 0)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  ret i32 %o4
}

; Bit 5: unsigned 128-bit products that overflow (the low 128 bits remain).
define internal i32 @umulo128_overflows() noinline {
  ; 2^64 * 2^64 = 2^128 -> 0
  %r0 = call i32 @umulo128(i64 0, i64 1, i64 0, i64 1, i64 0, i64 0, i32 1)
  ; (2^128-1) * 2 -> 2^128-2
  %r1 = call i32 @umulo128(i64 -1, i64 -1, i64 2, i64 0, i64 -2, i64 -1, i32 1)
  ; 2^127 * 2 -> 0
  %r2 = call i32 @umulo128(i64 0, i64 -9223372036854775808, i64 2, i64 0, i64 0, i64 0, i32 1)
  ; (2^64+1) * (2^64+1) = 2^128 + 2^65 + 1 -> 2^65 + 1
  %r3 = call i32 @umulo128(i64 1, i64 1, i64 1, i64 1, i64 1, i64 2, i32 1)
  ; (2^128-1) * (2^128-1) -> 1
  %r4 = call i32 @umulo128(i64 -1, i64 -1, i64 -1, i64 -1, i64 1, i64 0, i32 1)
  ; 0x123_456789abcdef0123 * 0x0fedcba987654321
  ;   -> 0x1fa00ad77d742235_88d7800b00ea4e83
  %r5 = call i32 @umulo128(i64 5001117282205630755, i64 291, i64 1147797409030816545, i64 0, i64 -8586253379810210173, i64 2278833332088480309, i32 1)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  ret i32 %o5
}

; Bit 6: signed 128-bit products that overflow.
define internal i32 @smulo128_overflows() noinline {
  ; INT128_MIN * -1 -> INT128_MIN
  %r0 = call i32 @smulo128(i64 0, i64 -9223372036854775808, i64 -1, i64 -1, i64 0, i64 -9223372036854775808, i32 1)
  ; -1 * INT128_MIN -> INT128_MIN
  %r1 = call i32 @smulo128(i64 -1, i64 -1, i64 0, i64 -9223372036854775808, i64 0, i64 -9223372036854775808, i32 1)
  ; 2^126 * 2 = 2^127 -> INT128_MIN
  %r2 = call i32 @smulo128(i64 0, i64 4611686018427387904, i64 2, i64 0, i64 0, i64 -9223372036854775808, i32 1)
  ; INT128_MAX * 2 -> -2
  %r3 = call i32 @smulo128(i64 -1, i64 9223372036854775807, i64 2, i64 0, i64 -2, i64 -1, i32 1)
  ; 2^64 * 2^63 = 2^127 -> INT128_MIN
  %r4 = call i32 @smulo128(i64 0, i64 1, i64 -9223372036854775808, i64 0, i64 0, i64 -9223372036854775808, i32 1)
  ; -2^64 * 2^64 = -2^128 -> 0
  %r5 = call i32 @smulo128(i64 0, i64 -1, i64 0, i64 1, i64 0, i64 0, i32 1)
  ; 0x123_456789abcdef0123 * -0x0fedcba987654321
  ;   -> 0xe05ff528828bddca_77287ff4ff15b17d
  %r6 = call i32 @smulo128(i64 5001117282205630755, i64 291, i64 -1147797409030816545, i64 -1, i64 8586253379810210173, i64 -2278833332088480310, i32 1)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  %o6 = or i32 %o5, %r6
  ret i32 %o6
}

; Bit 7: signed 128-bit products that fit, several of them at the limits.
define internal i32 @smulo128_fits() noinline {
  ; 2^126 * -2 = -2^127 = INT128_MIN
  %r0 = call i32 @smulo128(i64 0, i64 4611686018427387904, i64 -2, i64 -1, i64 0, i64 -9223372036854775808, i32 0)
  ; -3 * 5 = -15
  %r1 = call i32 @smulo128(i64 -3, i64 -1, i64 5, i64 0, i64 -15, i64 -1, i32 0)
  ; INT128_MIN * 1 = INT128_MIN
  %r2 = call i32 @smulo128(i64 0, i64 -9223372036854775808, i64 1, i64 0, i64 0, i64 -9223372036854775808, i32 0)
  ; INT128_MAX * -1 = -INT128_MAX = 0x80000000000000000000000000000001
  %r3 = call i32 @smulo128(i64 -1, i64 9223372036854775807, i64 -1, i64 -1, i64 1, i64 -9223372036854775808, i32 0)
  ; -2^63 * 2^64 = INT128_MIN
  %r4 = call i32 @smulo128(i64 -9223372036854775808, i64 -1, i64 0, i64 1, i64 0, i64 -9223372036854775808, i32 0)
  ; (2^64-1) * 2^63 = 0x7fffffffffffffff_8000000000000000
  %r5 = call i32 @smulo128(i64 -1, i64 0, i64 -9223372036854775808, i64 0, i64 -9223372036854775808, i64 9223372036854775807, i32 0)
  ; -1 * -1 = 1
  %r6 = call i32 @smulo128(i64 -1, i64 -1, i64 -1, i64 -1, i64 1, i64 0, i32 0)
  ; -0x1_23456789abcdef01 * 0x0fedcba987654321
  ;   = 0xede05ff528828bdd_cca4ab582281eddf
  %r7 = call i32 @smulo128(i64 -2541551405711093505, i64 -2, i64 1147797409030816545, i64 0, i64 -3700644598767292961, i64 -1305938385386173475, i32 0)
  %o1 = or i32 %r0, %r1
  %o2 = or i32 %o1, %r2
  %o3 = or i32 %o2, %r3
  %o4 = or i32 %o3, %r4
  %o5 = or i32 %o4, %r5
  %o6 = or i32 %o5, %r6
  %o7 = or i32 %o6, %r7
  ret i32 %o7
}

define i32 @main() {
  %c0 = call i32 @umulo64_fits()
  %c1 = call i32 @umulo64_overflows()
  %c2 = call i32 @smulo64_overflows()
  %c3 = call i32 @smulo64_fits()
  %c4 = call i32 @umulo128_fits()
  %c5 = call i32 @umulo128_overflows()
  %c6 = call i32 @smulo128_overflows()
  %c7 = call i32 @smulo128_fits()
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
