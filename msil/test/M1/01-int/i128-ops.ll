; i128 arithmetic expanded inline (Milestone 4): add with carry, sub with
; borrow, compares, 128-bit and widening multiplies, and shifts by constant and
; by variable amounts (expandShiftParts, which needs SELECT, D34), with no
; runtime helper (__multi3, __ashlti3, __lshrti3, __ashrti3). Signatures stay
; scalar: the noinline check functions get the 64-bit halves as arguments, so
; llc cannot fold anything even at -O2, build the i128 values, and compare
; both halves of every result with the value C gives; each returns 1 when a
; result differs, and main ORs one bit per check into the exit status (8 bits,
; D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

; Bit 0: add carries from the low into the high half; adding 2^64.
define internal i32 @add(i64 %m1, i64 %zero, i64 %one, i64 %hmax, i64 %xlo, i64 %xhi, i64 %ylo, i64 %yhi) noinline {
  %a.l = zext i64 %m1 to i128
  %a.h0 = zext i64 %zero to i128
  %a.h = shl i128 %a.h0, 64
  %a = or i128 %a.h, %a.l           ; 2^64 - 1
  %b.l = zext i64 %one to i128
  %b.h0 = zext i64 %zero to i128
  %b.h = shl i128 %b.h0, 64
  %b = or i128 %b.h, %b.l           ; 1
  %c.l = zext i64 %m1 to i128
  %c.h0 = zext i64 %hmax to i128
  %c.h = shl i128 %c.h0, 64
  %c = or i128 %c.h, %c.l           ; 2^127 - 1
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l           ; 0x123_8000000000000001
  %y.l = zext i64 %ylo to i128
  %y.h0 = zext i64 %yhi to i128
  %y.h = shl i128 %y.h0, 64
  %y = or i128 %y.h, %y.l           ; 0x456_8000000000000002
  %r0 = add i128 %a, %b
  %r0.lo = trunc i128 %r0 to i64
  %r0.h0 = lshr i128 %r0, 64
  %r0.hi = trunc i128 %r0.h0 to i64
  %r0.xl = xor i64 %r0.lo, 0
  %r0.xh = xor i64 %r0.hi, 1
  %r1 = add i128 %c, %b
  %r1.lo = trunc i128 %r1 to i64
  %r1.h0 = lshr i128 %r1, 64
  %r1.hi = trunc i128 %r1.h0 to i64
  %r1.xl = xor i64 %r1.lo, 0
  %r1.xh = xor i64 %r1.hi, -9223372036854775808
  %r2 = add i128 %x, %y
  %r2.lo = trunc i128 %r2 to i64
  %r2.h0 = lshr i128 %r2, 64
  %r2.hi = trunc i128 %r2.h0 to i64
  %r2.xl = xor i64 %r2.lo, 3
  %r2.xh = xor i64 %r2.hi, 1402
  %r3 = add i128 %x, 18446744073709551616
  %r3.lo = trunc i128 %r3 to i64
  %r3.h0 = lshr i128 %r3, 64
  %r3.hi = trunc i128 %r3.h0 to i64
  %r3.xl = xor i64 %r3.lo, -9223372036854775807
  %r3.xh = xor i64 %r3.hi, 292
  %acc0 = or i64 %r0.xl, %r0.xh
  %acc1 = or i64 %acc0, %r1.xl
  %acc2 = or i64 %acc1, %r1.xh
  %acc3 = or i64 %acc2, %r2.xl
  %acc4 = or i64 %acc3, %r2.xh
  %acc5 = or i64 %acc4, %r3.xl
  %acc6 = or i64 %acc5, %r3.xh
  %bad = icmp ne i64 %acc6, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 1: sub borrows from the high half; neg; signed and unsigned compares
; and eq/ne, packed into one word.
define internal i32 @sub(i64 %zero, i64 %one, i64 %xlo, i64 %xhi, i64 %ylo, i64 %yhi) noinline {
  %a.l = zext i64 %zero to i128
  %a.h0 = zext i64 %one to i128
  %a.h = shl i128 %a.h0, 64
  %a = or i128 %a.h, %a.l           ; 2^64
  %b.l = zext i64 %one to i128
  %b.h0 = zext i64 %zero to i128
  %b.h = shl i128 %b.h0, 64
  %b = or i128 %b.h, %b.l           ; 1
  %x.l = zext i64 %xlo to i128
  %x.h0 = zext i64 %xhi to i128
  %x.h = shl i128 %x.h0, 64
  %x = or i128 %x.h, %x.l
  %y.l = zext i64 %ylo to i128
  %y.h0 = zext i64 %yhi to i128
  %y.h = shl i128 %y.h0, 64
  %y = or i128 %y.h, %y.l
  %z.l = zext i64 %xlo to i128
  %z.h0 = zext i64 %yhi to i128
  %z.h = shl i128 %z.h0, 64
  %z = or i128 %z.h, %z.l
  %r0 = sub i128 %a, %b
  %r0.lo = trunc i128 %r0 to i64
  %r0.h0 = lshr i128 %r0, 64
  %r0.hi = trunc i128 %r0.h0 to i64
  %r0.xl = xor i64 %r0.lo, -1
  %r0.xh = xor i64 %r0.hi, 0
  %r1 = sub i128 %x, %y
  %r1.lo = trunc i128 %r1 to i64
  %r1.h0 = lshr i128 %r1, 64
  %r1.hi = trunc i128 %r1.h0 to i64
  %r1.xl = xor i64 %r1.lo, -1
  %r1.xh = xor i64 %r1.hi, -820
  %r2 = sub i128 0, %x
  %r2.lo = trunc i128 %r2 to i64
  %r2.h0 = lshr i128 %r2, 64
  %r2.hi = trunc i128 %r2.h0 to i64
  %r2.xl = xor i64 %r2.lo, 9223372036854775807
  %r2.xh = xor i64 %r2.hi, -292
  %c0 = icmp ult i128 %x, %y    ; 1
  %z0 = zext i1 %c0 to i64
  %c1 = icmp slt i128 %r2, %x    ; 1
  %z1 = zext i1 %c1 to i64
  %w1 = shl i64 %z1, 1
  %c2 = icmp ugt i128 %r2, %x    ; 1
  %z2 = zext i1 %c2 to i64
  %w2 = shl i64 %z2, 2
  %c3 = icmp eq i128 %r1, -15107883396368122773505    ; 1
  %z3 = zext i1 %c3 to i64
  %w3 = shl i64 %z3, 3
  %c4 = icmp eq i128 %x, %y    ; 0
  %z4 = zext i1 %c4 to i64
  %w4 = shl i64 %z4, 4
  %c5 = icmp ne i128 %x, %z    ; 1
  %z5 = zext i1 %c5 to i64
  %w5 = shl i64 %z5, 5
  %c6 = icmp sgt i128 %x, %r2    ; 1
  %z6 = zext i1 %c6 to i64
  %w6 = shl i64 %z6, 6
  %c7 = icmp sle i128 %y, %x    ; 0
  %z7 = zext i1 %c7 to i64
  %w7 = shl i64 %z7, 7
  %p1 = or i64 %z0, %w1
  %p2 = or i64 %p1, %w2
  %p3 = or i64 %p2, %w3
  %p4 = or i64 %p3, %w4
  %p5 = or i64 %p4, %w5
  %p6 = or i64 %p5, %w6
  %p7 = or i64 %p6, %w7
  %xp = xor i64 %p7, 111
  %acc0 = or i64 %r0.xl, %r0.xh
  %acc1 = or i64 %acc0, %r1.xl
  %acc2 = or i64 %acc1, %r1.xh
  %acc3 = or i64 %acc2, %r2.xl
  %acc4 = or i64 %acc3, %r2.xh
  %acc5 = or i64 %acc4, %xp
  %bad = icmp ne i64 %acc5, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 2: 128 x 128 -> 128-bit mul (inline, never __multi3).
define internal i32 @mul(i64 %alo, i64 %ahi, i64 %blo, i64 %bhi) noinline {
  %a.l = zext i64 %alo to i128
  %a.h0 = zext i64 %ahi to i128
  %a.h = shl i128 %a.h0, 64
  %a = or i128 %a.h, %a.l           ; 0x0123456789ABCDEF_FEDCBA9876543210
  %b.l = zext i64 %blo to i128
  %b.h0 = zext i64 %bhi to i128
  %b.h = shl i128 %b.h0, 64
  %b = or i128 %b.h, %b.l           ; 0x3_8000000000000001
  %r0 = mul i128 %a, %b
  %r0.lo = trunc i128 %r0 to i64
  %r0.h0 = lshr i128 %r0, 64
  %r0.hi = trunc i128 %r0.h0 to i64
  %r0.xl = xor i64 %r0.lo, -81985529216486896
  %r0.xh = xor i64 %r0.hi, -204963823041217241
  %r1 = mul i128 %a, %a
  %r1.lo = trunc i128 %r1 to i64
  %r1.h0 = lshr i128 %r1, 64
  %r1.hi = trunc i128 %r1.h0 to i64
  %r1.xl = xor i64 %r1.lo, -2383410429355736832
  %r1.xh = xor i64 %r1.hi, 4767185238841324716
  %r2 = mul i128 %a, 7
  %r2.lo = trunc i128 %r2 to i64
  %r2.h0 = lshr i128 %r2, 64
  %r2.hi = trunc i128 %r2.h0 to i64
  %r2.xl = xor i64 %r2.lo, -573898704515408272
  %r2.xh = xor i64 %r2.hi, 573898704515408271
  %acc0 = or i64 %r0.xl, %r0.xh
  %acc1 = or i64 %acc0, %r1.xl
  %acc2 = or i64 %acc1, %r1.xh
  %acc3 = or i64 %acc2, %r2.xl
  %acc4 = or i64 %acc3, %r2.xh
  %bad = icmp ne i64 %acc4, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 3: 64 x 64 -> 128-bit products of zero- and sign-extended values.
define internal i32 @widemul(i64 %m1, i64 %m3, i64 %five, i64 %min) noinline {
  %u = zext i64 %m1 to i128
  %r0 = mul i128 %u, %u
  %r0.lo = trunc i128 %r0 to i64
  %r0.h0 = lshr i128 %r0, 64
  %r0.hi = trunc i128 %r0.h0 to i64
  %r0.xl = xor i64 %r0.lo, 1
  %r0.xh = xor i64 %r0.hi, -2
  %s3 = sext i64 %m3 to i128
  %s5 = sext i64 %five to i128
  %r1 = mul i128 %s3, %s5
  %r1.lo = trunc i128 %r1 to i64
  %r1.h0 = lshr i128 %r1, 64
  %r1.hi = trunc i128 %r1.h0 to i64
  %r1.xl = xor i64 %r1.lo, -15
  %r1.xh = xor i64 %r1.hi, -1
  %sm = sext i64 %min to i128
  %r2 = mul i128 %sm, %sm
  %r2.lo = trunc i128 %r2 to i64
  %r2.h0 = lshr i128 %r2, 64
  %r2.hi = trunc i128 %r2.h0 to i64
  %r2.xl = xor i64 %r2.lo, 0
  %r2.xh = xor i64 %r2.hi, 4611686018427387904
  %acc0 = or i64 %r0.xl, %r0.xh
  %acc1 = or i64 %acc0, %r1.xl
  %acc2 = or i64 %acc1, %r1.xh
  %acc3 = or i64 %acc2, %r2.xl
  %acc4 = or i64 %acc3, %r2.xh
  %bad = icmp ne i64 %acc4, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 4: shl, lshr and ashr by constant amounts across the halves.
define internal i32 @shift_const(i64 %vlo, i64 %vhi) noinline {
  %v.l = zext i64 %vlo to i128
  %v.h0 = zext i64 %vhi to i128
  %v.h = shl i128 %v.h0, 64
  %v = or i128 %v.h, %v.l           ; 0xFEDCBA9876543210_0123456789ABCDEF
  %r0 = shl i128 %v, 1
  %r0.lo = trunc i128 %r0 to i64
  %r0.h0 = lshr i128 %r0, 64
  %r0.hi = trunc i128 %r0.h0 to i64
  %r0.xl = xor i64 %r0.lo, 163971058432973790
  %r0.xh = xor i64 %r0.hi, -163971058432973792
  %r1 = shl i128 %v, 64
  %r1.lo = trunc i128 %r1 to i64
  %r1.h0 = lshr i128 %r1, 64
  %r1.hi = trunc i128 %r1.h0 to i64
  %r1.xl = xor i64 %r1.lo, 0
  %r1.xh = xor i64 %r1.hi, 81985529216486895
  %r2 = shl i128 %v, 65
  %r2.lo = trunc i128 %r2 to i64
  %r2.h0 = lshr i128 %r2, 64
  %r2.hi = trunc i128 %r2.h0 to i64
  %r2.xl = xor i64 %r2.lo, 0
  %r2.xh = xor i64 %r2.hi, 163971058432973790
  %r3 = shl i128 %v, 127
  %r3.lo = trunc i128 %r3 to i64
  %r3.h0 = lshr i128 %r3, 64
  %r3.hi = trunc i128 %r3.h0 to i64
  %r3.xl = xor i64 %r3.lo, 0
  %r3.xh = xor i64 %r3.hi, -9223372036854775808
  %r4 = lshr i128 %v, 63
  %r4.lo = trunc i128 %r4 to i64
  %r4.h0 = lshr i128 %r4, 64
  %r4.hi = trunc i128 %r4.h0 to i64
  %r4.xl = xor i64 %r4.lo, -163971058432973792
  %r4.xh = xor i64 %r4.hi, 1
  %r5 = lshr i128 %v, 64
  %r5.lo = trunc i128 %r5 to i64
  %r5.h0 = lshr i128 %r5, 64
  %r5.hi = trunc i128 %r5.h0 to i64
  %r5.xl = xor i64 %r5.lo, -81985529216486896
  %r5.xh = xor i64 %r5.hi, 0
  %r6 = lshr i128 %v, 100
  %r6.lo = trunc i128 %r6 to i64
  %r6.h0 = lshr i128 %r6, 64
  %r6.hi = trunc i128 %r6.h0 to i64
  %r6.xl = xor i64 %r6.lo, 267242409
  %r6.xh = xor i64 %r6.hi, 0
  %r7 = ashr i128 %v, 4
  %r7.lo = trunc i128 %r7 to i64
  %r7.h0 = lshr i128 %r7, 64
  %r7.hi = trunc i128 %r7.h0 to i64
  %r7.xl = xor i64 %r7.lo, 5124095576030430
  %r7.xh = xor i64 %r7.hi, -5124095576030431
  %r8 = ashr i128 %v, 64
  %r8.lo = trunc i128 %r8 to i64
  %r8.h0 = lshr i128 %r8, 64
  %r8.hi = trunc i128 %r8.h0 to i64
  %r8.xl = xor i64 %r8.lo, -81985529216486896
  %r8.xh = xor i64 %r8.hi, -1
  %r9 = ashr i128 %v, 127
  %r9.lo = trunc i128 %r9 to i64
  %r9.h0 = lshr i128 %r9, 64
  %r9.hi = trunc i128 %r9.h0 to i64
  %r9.xl = xor i64 %r9.lo, -1
  %r9.xh = xor i64 %r9.hi, -1
  %acc0 = or i64 %r0.xl, %r0.xh
  %acc1 = or i64 %acc0, %r1.xl
  %acc2 = or i64 %acc1, %r1.xh
  %acc3 = or i64 %acc2, %r2.xl
  %acc4 = or i64 %acc3, %r2.xh
  %acc5 = or i64 %acc4, %r3.xl
  %acc6 = or i64 %acc5, %r3.xh
  %acc7 = or i64 %acc6, %r4.xl
  %acc8 = or i64 %acc7, %r4.xh
  %acc9 = or i64 %acc8, %r5.xl
  %acc10 = or i64 %acc9, %r5.xh
  %acc11 = or i64 %acc10, %r6.xl
  %acc12 = or i64 %acc11, %r6.xh
  %acc13 = or i64 %acc12, %r7.xl
  %acc14 = or i64 %acc13, %r7.xh
  %acc15 = or i64 %acc14, %r8.xl
  %acc16 = or i64 %acc15, %r8.xh
  %acc17 = or i64 %acc16, %r9.xl
  %acc18 = or i64 %acc17, %r9.xh
  %bad = icmp ne i64 %acc18, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 5: shl by variable amounts 0, 1, 63, 64, 65 and 127 (the shift-parts
; expansion selects between the halves).
define internal i32 @shl_var(i64 %vlo, i64 %vhi, i32 %n0, i32 %n1, i32 %n63, i32 %n64, i32 %n65, i32 %n127) noinline {
  %v.l = zext i64 %vlo to i128
  %v.h0 = zext i64 %vhi to i128
  %v.h = shl i128 %v.h0, 64
  %v = or i128 %v.h, %v.l
  %z0 = zext i32 %n0 to i128
  %r0 = shl i128 %v, %z0
  %r0.lo = trunc i128 %r0 to i64
  %r0.h0 = lshr i128 %r0, 64
  %r0.hi = trunc i128 %r0.h0 to i64
  %r0.xl = xor i64 %r0.lo, 81985529216486895
  %r0.xh = xor i64 %r0.hi, -81985529216486896
  %z1 = zext i32 %n1 to i128
  %r1 = shl i128 %v, %z1
  %r1.lo = trunc i128 %r1 to i64
  %r1.h0 = lshr i128 %r1, 64
  %r1.hi = trunc i128 %r1.h0 to i64
  %r1.xl = xor i64 %r1.lo, 163971058432973790
  %r1.xh = xor i64 %r1.hi, -163971058432973792
  %z63 = zext i32 %n63 to i128
  %r63 = shl i128 %v, %z63
  %r63.lo = trunc i128 %r63 to i64
  %r63.h0 = lshr i128 %r63, 64
  %r63.hi = trunc i128 %r63.h0 to i64
  %r63.xl = xor i64 %r63.lo, -9223372036854775808
  %r63.xh = xor i64 %r63.hi, 40992764608243447
  %z64 = zext i32 %n64 to i128
  %r64 = shl i128 %v, %z64
  %r64.lo = trunc i128 %r64 to i64
  %r64.h0 = lshr i128 %r64, 64
  %r64.hi = trunc i128 %r64.h0 to i64
  %r64.xl = xor i64 %r64.lo, 0
  %r64.xh = xor i64 %r64.hi, 81985529216486895
  %z65 = zext i32 %n65 to i128
  %r65 = shl i128 %v, %z65
  %r65.lo = trunc i128 %r65 to i64
  %r65.h0 = lshr i128 %r65, 64
  %r65.hi = trunc i128 %r65.h0 to i64
  %r65.xl = xor i64 %r65.lo, 0
  %r65.xh = xor i64 %r65.hi, 163971058432973790
  %z127 = zext i32 %n127 to i128
  %r127 = shl i128 %v, %z127
  %r127.lo = trunc i128 %r127 to i64
  %r127.h0 = lshr i128 %r127, 64
  %r127.hi = trunc i128 %r127.h0 to i64
  %r127.xl = xor i64 %r127.lo, 0
  %r127.xh = xor i64 %r127.hi, -9223372036854775808
  %acc0 = or i64 %r0.xl, %r0.xh
  %acc1 = or i64 %acc0, %r1.xl
  %acc2 = or i64 %acc1, %r1.xh
  %acc3 = or i64 %acc2, %r63.xl
  %acc4 = or i64 %acc3, %r63.xh
  %acc5 = or i64 %acc4, %r64.xl
  %acc6 = or i64 %acc5, %r64.xh
  %acc7 = or i64 %acc6, %r65.xl
  %acc8 = or i64 %acc7, %r65.xh
  %acc9 = or i64 %acc8, %r127.xl
  %acc10 = or i64 %acc9, %r127.xh
  %bad = icmp ne i64 %acc10, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 6: lshr by variable amounts 0, 1, 63, 64, 65 and 127 (the shift-parts
; expansion selects between the halves).
define internal i32 @lshr_var(i64 %vlo, i64 %vhi, i32 %n0, i32 %n1, i32 %n63, i32 %n64, i32 %n65, i32 %n127) noinline {
  %v.l = zext i64 %vlo to i128
  %v.h0 = zext i64 %vhi to i128
  %v.h = shl i128 %v.h0, 64
  %v = or i128 %v.h, %v.l
  %z0 = zext i32 %n0 to i128
  %r0 = lshr i128 %v, %z0
  %r0.lo = trunc i128 %r0 to i64
  %r0.h0 = lshr i128 %r0, 64
  %r0.hi = trunc i128 %r0.h0 to i64
  %r0.xl = xor i64 %r0.lo, 81985529216486895
  %r0.xh = xor i64 %r0.hi, -81985529216486896
  %z1 = zext i32 %n1 to i128
  %r1 = lshr i128 %v, %z1
  %r1.lo = trunc i128 %r1 to i64
  %r1.h0 = lshr i128 %r1, 64
  %r1.hi = trunc i128 %r1.h0 to i64
  %r1.xl = xor i64 %r1.lo, 40992764608243447
  %r1.xh = xor i64 %r1.hi, 9182379272246532360
  %z63 = zext i32 %n63 to i128
  %r63 = lshr i128 %v, %z63
  %r63.lo = trunc i128 %r63 to i64
  %r63.h0 = lshr i128 %r63, 64
  %r63.hi = trunc i128 %r63.h0 to i64
  %r63.xl = xor i64 %r63.lo, -163971058432973792
  %r63.xh = xor i64 %r63.hi, 1
  %z64 = zext i32 %n64 to i128
  %r64 = lshr i128 %v, %z64
  %r64.lo = trunc i128 %r64 to i64
  %r64.h0 = lshr i128 %r64, 64
  %r64.hi = trunc i128 %r64.h0 to i64
  %r64.xl = xor i64 %r64.lo, -81985529216486896
  %r64.xh = xor i64 %r64.hi, 0
  %z65 = zext i32 %n65 to i128
  %r65 = lshr i128 %v, %z65
  %r65.lo = trunc i128 %r65 to i64
  %r65.h0 = lshr i128 %r65, 64
  %r65.hi = trunc i128 %r65.h0 to i64
  %r65.xl = xor i64 %r65.lo, 9182379272246532360
  %r65.xh = xor i64 %r65.hi, 0
  %z127 = zext i32 %n127 to i128
  %r127 = lshr i128 %v, %z127
  %r127.lo = trunc i128 %r127 to i64
  %r127.h0 = lshr i128 %r127, 64
  %r127.hi = trunc i128 %r127.h0 to i64
  %r127.xl = xor i64 %r127.lo, 1
  %r127.xh = xor i64 %r127.hi, 0
  %acc0 = or i64 %r0.xl, %r0.xh
  %acc1 = or i64 %acc0, %r1.xl
  %acc2 = or i64 %acc1, %r1.xh
  %acc3 = or i64 %acc2, %r63.xl
  %acc4 = or i64 %acc3, %r63.xh
  %acc5 = or i64 %acc4, %r64.xl
  %acc6 = or i64 %acc5, %r64.xh
  %acc7 = or i64 %acc6, %r65.xl
  %acc8 = or i64 %acc7, %r65.xh
  %acc9 = or i64 %acc8, %r127.xl
  %acc10 = or i64 %acc9, %r127.xh
  %bad = icmp ne i64 %acc10, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 7: ashr by variable amounts 0, 1, 63, 64, 65 and 127 (the shift-parts
; expansion selects between the halves).
define internal i32 @ashr_var(i64 %vlo, i64 %vhi, i32 %n0, i32 %n1, i32 %n63, i32 %n64, i32 %n65, i32 %n127) noinline {
  %v.l = zext i64 %vlo to i128
  %v.h0 = zext i64 %vhi to i128
  %v.h = shl i128 %v.h0, 64
  %v = or i128 %v.h, %v.l
  %z0 = zext i32 %n0 to i128
  %r0 = ashr i128 %v, %z0
  %r0.lo = trunc i128 %r0 to i64
  %r0.h0 = lshr i128 %r0, 64
  %r0.hi = trunc i128 %r0.h0 to i64
  %r0.xl = xor i64 %r0.lo, 81985529216486895
  %r0.xh = xor i64 %r0.hi, -81985529216486896
  %z1 = zext i32 %n1 to i128
  %r1 = ashr i128 %v, %z1
  %r1.lo = trunc i128 %r1 to i64
  %r1.h0 = lshr i128 %r1, 64
  %r1.hi = trunc i128 %r1.h0 to i64
  %r1.xl = xor i64 %r1.lo, 40992764608243447
  %r1.xh = xor i64 %r1.hi, -40992764608243448
  %z63 = zext i32 %n63 to i128
  %r63 = ashr i128 %v, %z63
  %r63.lo = trunc i128 %r63 to i64
  %r63.h0 = lshr i128 %r63, 64
  %r63.hi = trunc i128 %r63.h0 to i64
  %r63.xl = xor i64 %r63.lo, -163971058432973792
  %r63.xh = xor i64 %r63.hi, -1
  %z64 = zext i32 %n64 to i128
  %r64 = ashr i128 %v, %z64
  %r64.lo = trunc i128 %r64 to i64
  %r64.h0 = lshr i128 %r64, 64
  %r64.hi = trunc i128 %r64.h0 to i64
  %r64.xl = xor i64 %r64.lo, -81985529216486896
  %r64.xh = xor i64 %r64.hi, -1
  %z65 = zext i32 %n65 to i128
  %r65 = ashr i128 %v, %z65
  %r65.lo = trunc i128 %r65 to i64
  %r65.h0 = lshr i128 %r65, 64
  %r65.hi = trunc i128 %r65.h0 to i64
  %r65.xl = xor i64 %r65.lo, -40992764608243448
  %r65.xh = xor i64 %r65.hi, -1
  %z127 = zext i32 %n127 to i128
  %r127 = ashr i128 %v, %z127
  %r127.lo = trunc i128 %r127 to i64
  %r127.h0 = lshr i128 %r127, 64
  %r127.hi = trunc i128 %r127.h0 to i64
  %r127.xl = xor i64 %r127.lo, -1
  %r127.xh = xor i64 %r127.hi, -1
  %acc0 = or i64 %r0.xl, %r0.xh
  %acc1 = or i64 %acc0, %r1.xl
  %acc2 = or i64 %acc1, %r1.xh
  %acc3 = or i64 %acc2, %r63.xl
  %acc4 = or i64 %acc3, %r63.xh
  %acc5 = or i64 %acc4, %r64.xl
  %acc6 = or i64 %acc5, %r64.xh
  %acc7 = or i64 %acc6, %r65.xl
  %acc8 = or i64 %acc7, %r65.xh
  %acc9 = or i64 %acc8, %r127.xl
  %acc10 = or i64 %acc9, %r127.xh
  %bad = icmp ne i64 %acc10, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define i32 @main() {
  %f0 = call i32 @add(i64 -1, i64 0, i64 1, i64 9223372036854775807, i64 -9223372036854775807, i64 291, i64 -9223372036854775806, i64 1110)
  %f1 = call i32 @sub(i64 0, i64 1, i64 -9223372036854775807, i64 291, i64 -9223372036854775806, i64 1110)
  %f2 = call i32 @mul(i64 -81985529216486896, i64 81985529216486895, i64 -9223372036854775807, i64 3)
  %f3 = call i32 @widemul(i64 -1, i64 -3, i64 5, i64 -9223372036854775808)
  %f4 = call i32 @shift_const(i64 81985529216486895, i64 -81985529216486896)
  %f5 = call i32 @shl_var(i64 81985529216486895, i64 -81985529216486896, i32 0, i32 1, i32 63, i32 64, i32 65, i32 127)
  %f6 = call i32 @lshr_var(i64 81985529216486895, i64 -81985529216486896, i32 0, i32 1, i32 63, i32 64, i32 65, i32 127)
  %f7 = call i32 @ashr_var(i64 81985529216486895, i64 -81985529216486896, i32 0, i32 1, i32 63, i32 64, i32 65, i32 127)
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
