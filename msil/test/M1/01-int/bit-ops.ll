; Bit-manipulation intrinsics through LLVM's generic integer expansions
; (Milestone 4, D16: no BCL intrinsics): ctlz and cttz with is_zero_poison
; false, input 0 included (at -O2 CodeGenPrepare splits these into a zero
; test, 'ceq; brtrue', and a join block), also applied to a select result
; (review-r2 finding 7: the select's block then already has the zero test's
; successors when emitSelect splits it into its brtrue triangle), ctpop,
; bswap, and rotates and funnel shifts (fshl/fshr) by 0, 1, width-1 and
; width+1 (the amount is taken modulo the width). Each noinline check function
; receives its inputs as arguments, so llc cannot fold the operations even at
; -O2; it compares every result with the value C gives (in the comments) and
; returns 1 when any differs. main ORs one bit per group of check functions
; into the exit status (8 bits, D37), so [exit 0] means every check passed and
; bit N names group N.
; RUN: %{msil_ll}

declare i32 @llvm.ctlz.i32(i32, i1)
declare i64 @llvm.ctlz.i64(i64, i1)
declare i32 @llvm.cttz.i32(i32, i1)
declare i64 @llvm.cttz.i64(i64, i1)
declare i32 @llvm.ctpop.i32(i32)
declare i64 @llvm.ctpop.i64(i64)
declare i32 @llvm.bswap.i32(i32)
declare i64 @llvm.bswap.i64(i64)
declare i32 @llvm.fshl.i32(i32, i32, i32)
declare i32 @llvm.fshr.i32(i32, i32, i32)
declare i64 @llvm.fshl.i64(i64, i64, i64)
declare i64 @llvm.fshr.i64(i64, i64, i64)

; Bit 0: ctlz i32 (__builtin_clz, but defined for 0), with sel_ctlz32_check.
define internal i32 @ctlz32(i32 %zero, i32 %one, i32 %p16, i32 %m1, i32 %max, i32 %f) noinline {
  %a = call i32 @llvm.ctlz.i32(i32 %zero, i1 false)  ; 32
  %b = call i32 @llvm.ctlz.i32(i32 %one, i1 false)   ; 31
  %c = call i32 @llvm.ctlz.i32(i32 %p16, i1 false)   ; 0x10000 -> 15
  %d = call i32 @llvm.ctlz.i32(i32 %m1, i1 false)    ; 0
  %e = call i32 @llvm.ctlz.i32(i32 %max, i1 false)   ; 0x7fffffff -> 1
  %g = call i32 @llvm.ctlz.i32(i32 %f, i1 true)      ; 0x00f00000 -> 8
  %x0 = xor i32 %a, 32
  %x1 = xor i32 %b, 31
  %x2 = xor i32 %c, 15
  %x3 = xor i32 %d, 0
  %x4 = xor i32 %e, 1
  %x5 = xor i32 %g, 8
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %bad = icmp ne i32 %o5, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; ctlz i32 of a select between two arguments on an int32 condition. At -O2
; CodeGenPrepare splits the block at the zero test of the ctlz, so the select
; stays in a block that already ends in 'ceq; brtrue' with two successors when
; emitSelect splits it again (review-r2 finding 7).
define internal i32 @sel_ctlz32(i32 %c, i32 %x, i32 %y) noinline {
  %b = icmp ne i32 %c, 0
  %s = select i1 %b, i32 %x, i32 %y
  %z = call i32 @llvm.ctlz.i32(i32 %s, i1 false)
  ret i32 %z
}

; Bit 0 too: ctlz of a select, both arms of the select on the zero path (the
; selected value is 0) and on the non-zero path.
define internal i32 @sel_ctlz32_check(i32 %t, i32 %f, i32 %zero, i32 %p16, i32 %ff) noinline {
  %a = call i32 @sel_ctlz32(i32 %t, i32 %zero, i32 %p16)  ; true arm: 0 -> 32
  %b = call i32 @sel_ctlz32(i32 %f, i32 %zero, i32 %p16)  ; false arm: 0x10000 -> 15
  %c = call i32 @sel_ctlz32(i32 %t, i32 %p16, i32 %zero)  ; true arm: 0x10000 -> 15
  %d = call i32 @sel_ctlz32(i32 %f, i32 %p16, i32 %zero)  ; false arm: 0 -> 32
  %e = call i32 @sel_ctlz32(i32 %t, i32 %ff, i32 %p16)    ; true arm: 0xff -> 24
  %g = call i32 @sel_ctlz32(i32 %f, i32 %ff, i32 %p16)    ; false arm: 0x10000 -> 15
  %x0 = xor i32 %a, 32
  %x1 = xor i32 %b, 15
  %x2 = xor i32 %c, 15
  %x3 = xor i32 %d, 32
  %x4 = xor i32 %e, 24
  %x5 = xor i32 %g, 15
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %bad = icmp ne i32 %o5, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 1: cttz i32 (__builtin_ctz, but defined for 0).
define internal i32 @cttz32(i32 %zero, i32 %one, i32 %p16, i32 %min, i32 %m1, i32 %f) noinline {
  %a = call i32 @llvm.cttz.i32(i32 %zero, i1 false)  ; 32
  %b = call i32 @llvm.cttz.i32(i32 %one, i1 false)   ; 0
  %c = call i32 @llvm.cttz.i32(i32 %p16, i1 false)   ; 0x10000 -> 16
  %d = call i32 @llvm.cttz.i32(i32 %min, i1 false)   ; INT_MIN -> 31
  %e = call i32 @llvm.cttz.i32(i32 %m1, i1 false)    ; 0
  %g = call i32 @llvm.cttz.i32(i32 %f, i1 true)      ; 0x00f00000 -> 20
  %x0 = xor i32 %a, 32
  %x1 = xor i32 %b, 0
  %x2 = xor i32 %c, 16
  %x3 = xor i32 %d, 31
  %x4 = xor i32 %e, 0
  %x5 = xor i32 %g, 20
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %bad = icmp ne i32 %o5, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 2: ctlz i64 (__builtin_clzll, but defined for 0).
define internal i32 @ctlz64(i64 %zero, i64 %one, i64 %p40, i64 %m1, i64 %p32, i64 %lo32, i64 %max) noinline {
  %a = call i64 @llvm.ctlz.i64(i64 %zero, i1 false)  ; 64
  %b = call i64 @llvm.ctlz.i64(i64 %one, i1 false)   ; 63
  %c = call i64 @llvm.ctlz.i64(i64 %p40, i1 false)   ; 2^40 -> 23
  %d = call i64 @llvm.ctlz.i64(i64 %m1, i1 false)    ; 0
  %e = call i64 @llvm.ctlz.i64(i64 %p32, i1 false)   ; 2^32 -> 31
  %f = call i64 @llvm.ctlz.i64(i64 %lo32, i1 false)  ; 0xffffffff -> 32
  %g = call i64 @llvm.ctlz.i64(i64 %max, i1 true)    ; INT64_MAX -> 1
  %x0 = xor i64 %a, 64
  %x1 = xor i64 %b, 63
  %x2 = xor i64 %c, 23
  %x3 = xor i64 %d, 0
  %x4 = xor i64 %e, 31
  %x5 = xor i64 %f, 32
  %x6 = xor i64 %g, 1
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

; Bit 3: cttz i64 (__builtin_ctzll, but defined for 0), with sel_cttz64_check.
define internal i32 @cttz64(i64 %zero, i64 %one, i64 %p40, i64 %min, i64 %p32, i64 %lo32, i64 %top4) noinline {
  %a = call i64 @llvm.cttz.i64(i64 %zero, i1 false)  ; 64
  %b = call i64 @llvm.cttz.i64(i64 %one, i1 false)   ; 0
  %c = call i64 @llvm.cttz.i64(i64 %p40, i1 false)   ; 2^40 -> 40
  %d = call i64 @llvm.cttz.i64(i64 %min, i1 false)   ; INT64_MIN -> 63
  %e = call i64 @llvm.cttz.i64(i64 %p32, i1 false)   ; 2^32 -> 32
  %f = call i64 @llvm.cttz.i64(i64 %lo32, i1 false)  ; 0xffffffff -> 0
  %g = call i64 @llvm.cttz.i64(i64 %top4, i1 true)   ; 0xf000000000000000 -> 60
  %x0 = xor i64 %a, 64
  %x1 = xor i64 %b, 0
  %x2 = xor i64 %c, 40
  %x3 = xor i64 %d, 63
  %x4 = xor i64 %e, 32
  %x5 = xor i64 %f, 0
  %x6 = xor i64 %g, 60
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

; cttz i64 of a select between two native int arguments on a native int
; condition (unsigned c < 5), the native int variant of sel_ctlz32.
define internal i64 @sel_cttz64(i64 %c, i64 %x, i64 %y) noinline {
  %b = icmp ult i64 %c, 5
  %s = select i1 %b, i64 %x, i64 %y
  %z = call i64 @llvm.cttz.i64(i64 %s, i1 false)
  ret i64 %z
}

; Bit 3 too: cttz of a select, both arms of the select on the zero path (the
; selected value is 0) and on the non-zero path.
define internal i32 @sel_cttz64_check(i64 %t, i64 %f, i64 %zero, i64 %p40, i64 %eight) noinline {
  %a = call i64 @sel_cttz64(i64 %t, i64 %zero, i64 %p40)    ; true arm: 0 -> 64
  %b = call i64 @sel_cttz64(i64 %f, i64 %zero, i64 %p40)    ; false arm: 2^40 -> 40
  %c = call i64 @sel_cttz64(i64 %t, i64 %p40, i64 %zero)    ; true arm: 2^40 -> 40
  %d = call i64 @sel_cttz64(i64 %f, i64 %p40, i64 %zero)    ; false arm: 0 -> 64
  %e = call i64 @sel_cttz64(i64 %t, i64 %eight, i64 %p40)   ; true arm: 8 -> 3
  %g = call i64 @sel_cttz64(i64 %f, i64 %eight, i64 %p40)   ; false arm: 2^40 -> 40
  %x0 = xor i64 %a, 64
  %x1 = xor i64 %b, 40
  %x2 = xor i64 %c, 40
  %x3 = xor i64 %d, 64
  %x4 = xor i64 %e, 3
  %x5 = xor i64 %g, 40
  %o1 = or i64 %x0, %x1
  %o2 = or i64 %o1, %x2
  %o3 = or i64 %o2, %x3
  %o4 = or i64 %o3, %x4
  %o5 = or i64 %o4, %x5
  %bad = icmp ne i64 %o5, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 4: ctpop i32 and i64 (__builtin_popcount, __builtin_popcountll).
define internal i32 @ctpop(i32 %a0, i32 %a1, i32 %a2, i32 %a3, i64 %b0, i64 %b1, i64 %b2, i64 %b3, i64 %b4) noinline {
  %p0 = call i32 @llvm.ctpop.i32(i32 %a0)            ; 0 -> 0
  %p1 = call i32 @llvm.ctpop.i32(i32 %a1)            ; -1 -> 32
  %p2 = call i32 @llvm.ctpop.i32(i32 %a2)            ; 0xf0f0f0f1 -> 17
  %p3 = call i32 @llvm.ctpop.i32(i32 %a3)            ; INT_MIN -> 1
  %q0 = call i64 @llvm.ctpop.i64(i64 %b0)            ; 0 -> 0
  %q1 = call i64 @llvm.ctpop.i64(i64 %b1)            ; -1 -> 64
  %q2 = call i64 @llvm.ctpop.i64(i64 %b2)            ; 0x8000000000000001 -> 2
  %q3 = call i64 @llvm.ctpop.i64(i64 %b3)            ; 0x0123456789abcdef -> 32
  %q4 = call i64 @llvm.ctpop.i64(i64 %b4)            ; 0xffffffff00000000 -> 32
  %x0 = xor i32 %p0, 0
  %x1 = xor i32 %p1, 32
  %x2 = xor i32 %p2, 17
  %x3 = xor i32 %p3, 1
  %y0 = xor i64 %q0, 0
  %y1 = xor i64 %q1, 64
  %y2 = xor i64 %q2, 2
  %y3 = xor i64 %q3, 32
  %y4 = xor i64 %q4, 32
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %w1 = or i64 %y0, %y1
  %w2 = or i64 %w1, %y2
  %w3 = or i64 %w2, %y3
  %w4 = or i64 %w3, %y4
  %bad32 = icmp ne i32 %o3, 0
  %bad64 = icmp ne i64 %w4, 0
  %bad = or i1 %bad32, %bad64
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 5: bswap i32 and i64 (__builtin_bswap32, __builtin_bswap64).
define internal i32 @bswap(i32 %a0, i32 %a1, i32 %a2, i64 %b0, i64 %b1, i64 %b2, i64 %b3) noinline {
  %s0 = call i32 @llvm.bswap.i32(i32 %a0)            ; 0x11223344 -> 0x44332211
  %s1 = call i32 @llvm.bswap.i32(i32 %a1)            ; 0x80000001 -> 0x01000080
  %s2 = call i32 @llvm.bswap.i32(i32 %a2)            ; 0xffffff00 -> 0x00ffffff
  %t0 = call i64 @llvm.bswap.i64(i64 %b0)            ; 0x0102030405060708 -> 0x0807060504030201
  %t1 = call i64 @llvm.bswap.i64(i64 %b1)            ; 0xff00000000000080 -> 0x80000000000000ff
  %t2 = call i64 @llvm.bswap.i64(i64 %b2)            ; 0x00000000ffffffff -> 0xffffffff00000000
  %t3 = call i64 @llvm.bswap.i64(i64 %b3)            ; 0x8000000000000001 -> 0x0100000000000080
  %x0 = xor i32 %s0, 1144201745
  %x1 = xor i32 %s1, 16777344
  %x2 = xor i32 %s2, 16777215
  %y0 = xor i64 %t0, 578437695752307201
  %y1 = xor i64 %t1, -9223372036854775553
  %y2 = xor i64 %t2, -4294967296
  %y3 = xor i64 %t3, 72057594037928064
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %w1 = or i64 %y0, %y1
  %w2 = or i64 %w1, %y2
  %w3 = or i64 %w2, %y3
  %bad32 = icmp ne i32 %o2, 0
  %bad64 = icmp ne i64 %w3, 0
  %bad = or i1 %bad32, %bad64
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 6: i32 rotates, fshl/fshr(x, x, n), and funnel shifts of two different
; values, fshl/fshr(y, x, n), by n = 0, 1, 31 and 33 (33 acts as 1).
; x = 0x80000001, y = 0x12345678.
define internal i32 @fsh32(i32 %x, i32 %y, i32 %n0, i32 %n1, i32 %n31, i32 %n33) noinline {
  %l0 = call i32 @llvm.fshl.i32(i32 %x, i32 %x, i32 %n0)    ; 0x80000001
  %l1 = call i32 @llvm.fshl.i32(i32 %x, i32 %x, i32 %n1)    ; 0x00000003
  %l2 = call i32 @llvm.fshl.i32(i32 %x, i32 %x, i32 %n31)   ; 0xc0000000
  %l3 = call i32 @llvm.fshl.i32(i32 %x, i32 %x, i32 %n33)   ; 0x00000003
  %r0 = call i32 @llvm.fshr.i32(i32 %x, i32 %x, i32 %n0)    ; 0x80000001
  %r1 = call i32 @llvm.fshr.i32(i32 %x, i32 %x, i32 %n1)    ; 0xc0000000
  %r2 = call i32 @llvm.fshr.i32(i32 %x, i32 %x, i32 %n31)   ; 0x00000003
  %r3 = call i32 @llvm.fshr.i32(i32 %x, i32 %x, i32 %n33)   ; 0xc0000000
  %fl0 = call i32 @llvm.fshl.i32(i32 %y, i32 %x, i32 %n0)   ; 0x12345678
  %fl1 = call i32 @llvm.fshl.i32(i32 %y, i32 %x, i32 %n1)   ; 0x2468acf1
  %fl2 = call i32 @llvm.fshl.i32(i32 %y, i32 %x, i32 %n31)  ; 0x40000000
  %fl3 = call i32 @llvm.fshl.i32(i32 %y, i32 %x, i32 %n33)  ; 0x2468acf1
  %fr0 = call i32 @llvm.fshr.i32(i32 %y, i32 %x, i32 %n0)   ; 0x80000001
  %fr1 = call i32 @llvm.fshr.i32(i32 %y, i32 %x, i32 %n1)   ; 0x40000000
  %fr2 = call i32 @llvm.fshr.i32(i32 %y, i32 %x, i32 %n31)  ; 0x2468acf1
  %fr3 = call i32 @llvm.fshr.i32(i32 %y, i32 %x, i32 %n33)  ; 0x40000000
  %x0 = xor i32 %l0, -2147483647
  %x1 = xor i32 %l1, 3
  %x2 = xor i32 %l2, -1073741824
  %x3 = xor i32 %l3, 3
  %x4 = xor i32 %r0, -2147483647
  %x5 = xor i32 %r1, -1073741824
  %x6 = xor i32 %r2, 3
  %x7 = xor i32 %r3, -1073741824
  %x8 = xor i32 %fl0, 305419896
  %x9 = xor i32 %fl1, 610839793
  %x10 = xor i32 %fl2, 1073741824
  %x11 = xor i32 %fl3, 610839793
  %x12 = xor i32 %fr0, -2147483647
  %x13 = xor i32 %fr1, 1073741824
  %x14 = xor i32 %fr2, 610839793
  %x15 = xor i32 %fr3, 1073741824
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
  %o11 = or i32 %o10, %x11
  %o12 = or i32 %o11, %x12
  %o13 = or i32 %o12, %x13
  %o14 = or i32 %o13, %x14
  %o15 = or i32 %o14, %x15
  %bad = icmp ne i32 %o15, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 7: i64 rotates and funnel shifts by n = 0, 1, 63 and 65 (65 acts as 1).
; x = 0x8000000000000003, y = 0x0123456789abcdee.
define internal i32 @fsh64(i64 %x, i64 %y, i64 %n0, i64 %n1, i64 %n63, i64 %n65) noinline {
  %l0 = call i64 @llvm.fshl.i64(i64 %x, i64 %x, i64 %n0)    ; 0x8000000000000003
  %l1 = call i64 @llvm.fshl.i64(i64 %x, i64 %x, i64 %n1)    ; 0x0000000000000007
  %l2 = call i64 @llvm.fshl.i64(i64 %x, i64 %x, i64 %n63)   ; 0xc000000000000001
  %l3 = call i64 @llvm.fshl.i64(i64 %x, i64 %x, i64 %n65)   ; 0x0000000000000007
  %r0 = call i64 @llvm.fshr.i64(i64 %x, i64 %x, i64 %n0)    ; 0x8000000000000003
  %r1 = call i64 @llvm.fshr.i64(i64 %x, i64 %x, i64 %n1)    ; 0xc000000000000001
  %r2 = call i64 @llvm.fshr.i64(i64 %x, i64 %x, i64 %n63)   ; 0x0000000000000007
  %r3 = call i64 @llvm.fshr.i64(i64 %x, i64 %x, i64 %n65)   ; 0xc000000000000001
  %fl0 = call i64 @llvm.fshl.i64(i64 %y, i64 %x, i64 %n0)   ; 0x0123456789abcdee
  %fl1 = call i64 @llvm.fshl.i64(i64 %y, i64 %x, i64 %n1)   ; 0x02468acf13579bdd
  %fl2 = call i64 @llvm.fshl.i64(i64 %y, i64 %x, i64 %n63)  ; 0x4000000000000001
  %fl3 = call i64 @llvm.fshl.i64(i64 %y, i64 %x, i64 %n65)  ; 0x02468acf13579bdd
  %fr0 = call i64 @llvm.fshr.i64(i64 %y, i64 %x, i64 %n0)   ; 0x8000000000000003
  %fr1 = call i64 @llvm.fshr.i64(i64 %y, i64 %x, i64 %n1)   ; 0x4000000000000001
  %fr2 = call i64 @llvm.fshr.i64(i64 %y, i64 %x, i64 %n63)  ; 0x02468acf13579bdd
  %fr3 = call i64 @llvm.fshr.i64(i64 %y, i64 %x, i64 %n65)  ; 0x4000000000000001
  %x0 = xor i64 %l0, -9223372036854775805
  %x1 = xor i64 %l1, 7
  %x2 = xor i64 %l2, -4611686018427387903
  %x3 = xor i64 %l3, 7
  %x4 = xor i64 %r0, -9223372036854775805
  %x5 = xor i64 %r1, -4611686018427387903
  %x6 = xor i64 %r2, 7
  %x7 = xor i64 %r3, -4611686018427387903
  %x8 = xor i64 %fl0, 81985529216486894
  %x9 = xor i64 %fl1, 163971058432973789
  %x10 = xor i64 %fl2, 4611686018427387905
  %x11 = xor i64 %fl3, 163971058432973789
  %x12 = xor i64 %fr0, -9223372036854775805
  %x13 = xor i64 %fr1, 4611686018427387905
  %x14 = xor i64 %fr2, 163971058432973789
  %x15 = xor i64 %fr3, 4611686018427387905
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
  %o15 = or i64 %o14, %x15
  %bad = icmp ne i64 %o15, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define i32 @main() {
  %c0a = call i32 @ctlz32(i32 0, i32 1, i32 65536, i32 -1, i32 2147483647, i32 15728640)
  %c0b = call i32 @sel_ctlz32_check(i32 1, i32 0, i32 0, i32 65536, i32 255)
  %c0 = or i32 %c0a, %c0b
  %c1 = call i32 @cttz32(i32 0, i32 1, i32 65536, i32 -2147483648, i32 -1, i32 15728640)
  %c2 = call i32 @ctlz64(i64 0, i64 1, i64 1099511627776, i64 -1, i64 4294967296, i64 4294967295, i64 9223372036854775807)
  %c3a = call i32 @cttz64(i64 0, i64 1, i64 1099511627776, i64 -9223372036854775808, i64 4294967296, i64 4294967295, i64 -1152921504606846976)
  %c3b = call i32 @sel_cttz64_check(i64 1, i64 9, i64 0, i64 1099511627776, i64 8)
  %c3 = or i32 %c3a, %c3b
  %c4 = call i32 @ctpop(i32 0, i32 -1, i32 -252645135, i32 -2147483648, i64 0, i64 -1, i64 -9223372036854775807, i64 81985529216486895, i64 -4294967296)
  %c5 = call i32 @bswap(i32 287454020, i32 -2147483647, i32 -256, i64 72623859790382856, i64 -72057594037927808, i64 4294967295, i64 -9223372036854775807)
  %c6 = call i32 @fsh32(i32 -2147483647, i32 305419896, i32 0, i32 1, i32 31, i32 33)
  %c7 = call i32 @fsh64(i64 -9223372036854775805, i64 81985529216486894, i64 0, i64 1, i64 63, i64 65)
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
