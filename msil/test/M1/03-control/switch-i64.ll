; Switches on native int values (Milestone 6, D35): the jump-table index
; is int32, because the CIL 'switch' on a native int fails ILVerify, so
; instruction selection range-checks the full 64-bit value (x - low against
; the table size) and only then truncates the index with conv.i4. A switch
; on 2^32 + 1 with cases 0..7 must take the default, also on runtimes where
; 'switch' itself would truncate; so must every value whose low 32 bits are
; a case value (2^32, 2^32 + 7, 0xFFFFFFFF00000003, 2^31 * k + k). Also cases
; at 2^40 (the subtraction of the low bound is 64-bit: 3 is not 2^40 + 3),
; switches on the sign and zero extension of an int32 (C's
; 'switch ((long long)i)'), cases that straddle 2^32, and a switch on a call
; result. Each check folds the results with h = h * 31 + f(x) and returns 1
; when the hash differs from the value C gives (in the comments); main ORs
; one bit per check into the exit status (8 bits, D37): [exit 0] means every
; check passed.
; RUN: %{msil_ll}

; dense64(x): cases 0..7; otherwise -1.
define internal i32 @dense64(i64 %x) noinline {
entry:
  %t = trunc i64 %x to i32
  switch i64 %x, label %def [
    i64 0, label %c0
    i64 1, label %c1
    i64 2, label %c2
    i64 3, label %c3
    i64 4, label %c4
    i64 5, label %c5
    i64 6, label %c6
    i64 7, label %c7
  ]

c0:
  %r0 = add i32 %t, 1000
  br label %join

c1:
  br label %join

c2:
  %r2 = mul i32 %t, %t
  br label %join

c3:
  br label %join

c4:
  %r4 = xor i32 %t, 85
  br label %join

c5:
  br label %join

c6:
  %r6 = sub i32 %t, 77
  br label %join

c7:
  br label %join

def:
  br label %join

join:
  %r = phi i32 [ %r0, %c0 ], [ 2, %c1 ], [ %r2, %c2 ], [ 4, %c3 ],
               [ %r4, %c4 ], [ 6, %c5 ], [ %r6, %c6 ], [ 8, %c7 ], [ -1, %def ]
  ret i32 %r
}

; offset64(x): cases 2^40 .. 2^40 + 7; otherwise -1.
define internal i32 @offset64(i64 %x) noinline {
entry:
  switch i64 %x, label %def [
    i64 1099511627776, label %c0
    i64 1099511627777, label %c1
    i64 1099511627778, label %c2
    i64 1099511627779, label %c3
    i64 1099511627780, label %c4
    i64 1099511627781, label %c5
    i64 1099511627782, label %c6
    i64 1099511627783, label %c7
  ]

c0:
  br label %join

c1:
  %l1 = and i64 %x, 255
  %r1 = trunc i64 %l1 to i32
  br label %join

c2:
  br label %join

c3:
  %h3 = ashr i64 %x, 40
  %t3 = trunc i64 %h3 to i32
  %r3 = add i32 %t3, 40
  br label %join

c4:
  br label %join

c5:
  %t5 = trunc i64 %x to i32
  %r5 = add i32 %t5, 6
  br label %join

c6:
  br label %join

c7:
  br label %join

def:
  br label %join

join:
  %r = phi i32 [ 1, %c0 ], [ %r1, %c1 ], [ 3, %c2 ], [ %r3, %c3 ],
               [ 5, %c4 ], [ %r5, %c5 ], [ 7, %c6 ], [ 8, %c7 ], [ -1, %def ]
  ret i32 %r
}

; sext_switch(y): switch ((long long)y), cases -4..3; otherwise 99.
define internal i32 @sext_switch(i32 %y) noinline {
entry:
  %x = sext i32 %y to i64
  switch i64 %x, label %def [
    i64 -4, label %cm4
    i64 -3, label %cm3
    i64 -2, label %cm2
    i64 -1, label %cm1
    i64 0, label %c0
    i64 1, label %c1
    i64 2, label %c2
    i64 3, label %c3
  ]

cm4:
  %rm4 = mul i32 %y, 10
  br label %join

cm3:
  br label %join

cm2:
  %rm2 = sub i32 %y, 3
  br label %join

cm1:
  br label %join

c0:
  %r0 = add i32 %y, 7
  br label %join

c1:
  br label %join

c2:
  %r2 = mul i32 %y, %y
  br label %join

c3:
  br label %join

def:
  br label %join

join:
  %r = phi i32 [ %rm4, %cm4 ], [ 11, %cm3 ], [ %rm2, %cm2 ], [ 13, %cm1 ],
               [ %r0, %c0 ], [ 15, %c1 ], [ %r2, %c2 ], [ 17, %c3 ], [ 99, %def ]
  ret i32 %r
}

; zext_switch(y): switch ((unsigned long long)(unsigned)y), cases
; 0xFFFFFFF8 .. 0xFFFFFFFF; otherwise 99.
define internal i32 @zext_switch(i32 %y) noinline {
entry:
  %x = zext i32 %y to i64
  switch i64 %x, label %def [
    i64 4294967288, label %c0
    i64 4294967289, label %c1
    i64 4294967290, label %c2
    i64 4294967291, label %c3
    i64 4294967292, label %c4
    i64 4294967293, label %c5
    i64 4294967294, label %c6
    i64 4294967295, label %c7
  ]

c0:
  br label %join

c1:
  %r1 = and i32 %y, 255
  br label %join

c2:
  br label %join

c3:
  %r3 = mul i32 %y, 2
  br label %join

c4:
  br label %join

c5:
  %r5 = lshr i32 %y, 28
  br label %join

c6:
  br label %join

c7:
  br label %join

def:
  br label %join

join:
  %r = phi i32 [ 21, %c0 ], [ %r1, %c1 ], [ 23, %c2 ], [ %r3, %c3 ],
               [ 25, %c4 ], [ %r5, %c5 ], [ 27, %c6 ], [ 28, %c7 ], [ 99, %def ]
  ret i32 %r
}

; boundary(x): cases 2^32 - 4 .. 2^32 + 3; otherwise -1.
define internal i32 @boundary(i64 %x) noinline {
entry:
  switch i64 %x, label %def [
    i64 4294967292, label %c0
    i64 4294967293, label %c1
    i64 4294967294, label %c2
    i64 4294967295, label %c3
    i64 4294967296, label %c4
    i64 4294967297, label %c5
    i64 4294967298, label %c6
    i64 4294967299, label %c7
  ]

c0:
  br label %join

c1:
  %l1 = and i64 %x, 4095
  %r1 = trunc i64 %l1 to i32
  br label %join

c2:
  br label %join

c3:
  %h3 = ashr i64 %x, 32
  %r3 = trunc i64 %h3 to i32
  br label %join

c4:
  br label %join

c5:
  %d5 = sub i64 %x, 4294967296
  %r5 = trunc i64 %d5 to i32
  br label %join

c6:
  br label %join

c7:
  br label %join

def:
  br label %join

join:
  %r = phi i32 [ 31, %c0 ], [ %r1, %c1 ], [ 33, %c2 ], [ %r3, %c3 ],
               [ 35, %c4 ], [ %r5, %c5 ], [ 37, %c6 ], [ 38, %c7 ], [ -1, %def ]
  ret i32 %r
}

define internal i32 @mix(i32 %h, i32 %f) noinline {
  %m = mul i32 %h, 31
  %r = add i32 %m, %f
  ret i32 %r
}

; Bit 0: dense64(x): the cases 0..7, then the default for
; 8, -1, 4294967296, 4294967297, 4294967303, -4294967293, -9223372036854775808,
; 9223372032559808514, 9223372036854775807, -8.
define internal i32 @check_dense64() noinline {
  %p0_v0 = call i32 @dense64(i64 0)
  %p0_0 = call i32 @mix(i32 0, i32 %p0_v0)
  %p0_v1 = call i32 @dense64(i64 1)
  %p0_1 = call i32 @mix(i32 %p0_0, i32 %p0_v1)
  %p0_v2 = call i32 @dense64(i64 2)
  %p0_2 = call i32 @mix(i32 %p0_1, i32 %p0_v2)
  %p0_v3 = call i32 @dense64(i64 3)
  %p0_3 = call i32 @mix(i32 %p0_2, i32 %p0_v3)
  %p0_v4 = call i32 @dense64(i64 4)
  %p0_4 = call i32 @mix(i32 %p0_3, i32 %p0_v4)
  %p0_v5 = call i32 @dense64(i64 5)
  %p0_5 = call i32 @mix(i32 %p0_4, i32 %p0_v5)
  %p0_v6 = call i32 @dense64(i64 6)
  %p0_6 = call i32 @mix(i32 %p0_5, i32 %p0_v6)
  %p0_v7 = call i32 @dense64(i64 7)
  %p0_7 = call i32 @mix(i32 %p0_6, i32 %p0_v7)
  %p0_v8 = call i32 @dense64(i64 8)
  %p0_8 = call i32 @mix(i32 %p0_7, i32 %p0_v8)
  %p0_v9 = call i32 @dense64(i64 -1)
  %p0_9 = call i32 @mix(i32 %p0_8, i32 %p0_v9)
  %p0_v10 = call i32 @dense64(i64 4294967296)
  %p0_10 = call i32 @mix(i32 %p0_9, i32 %p0_v10)
  %p0_v11 = call i32 @dense64(i64 4294967297)
  %p0_11 = call i32 @mix(i32 %p0_10, i32 %p0_v11)
  %p0_v12 = call i32 @dense64(i64 4294967303)
  %p0_12 = call i32 @mix(i32 %p0_11, i32 %p0_v12)
  %p0_v13 = call i32 @dense64(i64 -4294967293)
  %p0_13 = call i32 @mix(i32 %p0_12, i32 %p0_v13)
  %p0_v14 = call i32 @dense64(i64 -9223372036854775808)
  %p0_14 = call i32 @mix(i32 %p0_13, i32 %p0_v14)
  %p0_v15 = call i32 @dense64(i64 9223372032559808514)
  %p0_15 = call i32 @mix(i32 %p0_14, i32 %p0_v15)
  %p0_v16 = call i32 @dense64(i64 9223372036854775807)
  %p0_16 = call i32 @mix(i32 %p0_15, i32 %p0_v16)
  %p0_v17 = call i32 @dense64(i64 -8)
  %p0_17 = call i32 @mix(i32 %p0_16, i32 %p0_v17)
  %c0 = icmp ne i32 %p0_17, -722788354
  %r = zext i1 %c0 to i32
  ret i32 %r
}

; Bit 1: the case named by plan M6.d: 2^32 + 1 is not case 1 (dense64 gives
; -1, not 2), and 1 is.
define internal i32 @check_2p32p1() noinline {
  %a = call i32 @dense64(i64 4294967297)
  %a1 = icmp ne i32 %a, -1
  %b = call i32 @dense64(i64 1)
  %b1 = icmp ne i32 %b, 2
  %o = or i1 %a1, %b1
  %r = zext i1 %o to i32
  ret i32 %r
}

; Bit 2: offset64(x): the cases 2^40 .. 2^40 + 7, then the default for
; 3, 1103806595075, 1099511627775, 1099511627784, 0, -1099511627773.
define internal i32 @check_offset64() noinline {
  %p0_v0 = call i32 @offset64(i64 1099511627776)
  %p0_0 = call i32 @mix(i32 0, i32 %p0_v0)
  %p0_v1 = call i32 @offset64(i64 1099511627777)
  %p0_1 = call i32 @mix(i32 %p0_0, i32 %p0_v1)
  %p0_v2 = call i32 @offset64(i64 1099511627778)
  %p0_2 = call i32 @mix(i32 %p0_1, i32 %p0_v2)
  %p0_v3 = call i32 @offset64(i64 1099511627779)
  %p0_3 = call i32 @mix(i32 %p0_2, i32 %p0_v3)
  %p0_v4 = call i32 @offset64(i64 1099511627780)
  %p0_4 = call i32 @mix(i32 %p0_3, i32 %p0_v4)
  %p0_v5 = call i32 @offset64(i64 1099511627781)
  %p0_5 = call i32 @mix(i32 %p0_4, i32 %p0_v5)
  %p0_v6 = call i32 @offset64(i64 1099511627782)
  %p0_6 = call i32 @mix(i32 %p0_5, i32 %p0_v6)
  %p0_v7 = call i32 @offset64(i64 1099511627783)
  %p0_7 = call i32 @mix(i32 %p0_6, i32 %p0_v7)
  %p0_v8 = call i32 @offset64(i64 3)
  %p0_8 = call i32 @mix(i32 %p0_7, i32 %p0_v8)
  %p0_v9 = call i32 @offset64(i64 1103806595075)
  %p0_9 = call i32 @mix(i32 %p0_8, i32 %p0_v9)
  %p0_v10 = call i32 @offset64(i64 1099511627775)
  %p0_10 = call i32 @mix(i32 %p0_9, i32 %p0_v10)
  %p0_v11 = call i32 @offset64(i64 1099511627784)
  %p0_11 = call i32 @mix(i32 %p0_10, i32 %p0_v11)
  %p0_v12 = call i32 @offset64(i64 0)
  %p0_12 = call i32 @mix(i32 %p0_11, i32 %p0_v12)
  %p0_v13 = call i32 @offset64(i64 -1099511627773)
  %p0_13 = call i32 @mix(i32 %p0_12, i32 %p0_v13)
  %c0 = icmp ne i32 %p0_13, 1628243853
  %r = zext i1 %c0 to i32
  ret i32 %r
}

; Bit 3: sext_switch(y) for y in
; -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, -2147483648, 2147483647,
; -2147483645.
; and zext_switch(y) for y in
; -1, -8, -9, 7, 0, -5, -2, -2147483648.
define internal i32 @check_ext() noinline {
  %p0_v0 = call i32 @sext_switch(i32 -6)
  %p0_0 = call i32 @mix(i32 0, i32 %p0_v0)
  %p0_v1 = call i32 @sext_switch(i32 -5)
  %p0_1 = call i32 @mix(i32 %p0_0, i32 %p0_v1)
  %p0_v2 = call i32 @sext_switch(i32 -4)
  %p0_2 = call i32 @mix(i32 %p0_1, i32 %p0_v2)
  %p0_v3 = call i32 @sext_switch(i32 -3)
  %p0_3 = call i32 @mix(i32 %p0_2, i32 %p0_v3)
  %p0_v4 = call i32 @sext_switch(i32 -2)
  %p0_4 = call i32 @mix(i32 %p0_3, i32 %p0_v4)
  %p0_v5 = call i32 @sext_switch(i32 -1)
  %p0_5 = call i32 @mix(i32 %p0_4, i32 %p0_v5)
  %p0_v6 = call i32 @sext_switch(i32 0)
  %p0_6 = call i32 @mix(i32 %p0_5, i32 %p0_v6)
  %p0_v7 = call i32 @sext_switch(i32 1)
  %p0_7 = call i32 @mix(i32 %p0_6, i32 %p0_v7)
  %p0_v8 = call i32 @sext_switch(i32 2)
  %p0_8 = call i32 @mix(i32 %p0_7, i32 %p0_v8)
  %p0_v9 = call i32 @sext_switch(i32 3)
  %p0_9 = call i32 @mix(i32 %p0_8, i32 %p0_v9)
  %p0_v10 = call i32 @sext_switch(i32 4)
  %p0_10 = call i32 @mix(i32 %p0_9, i32 %p0_v10)
  %p0_v11 = call i32 @sext_switch(i32 5)
  %p0_11 = call i32 @mix(i32 %p0_10, i32 %p0_v11)
  %p0_v12 = call i32 @sext_switch(i32 -2147483648)
  %p0_12 = call i32 @mix(i32 %p0_11, i32 %p0_v12)
  %p0_v13 = call i32 @sext_switch(i32 2147483647)
  %p0_13 = call i32 @mix(i32 %p0_12, i32 %p0_v13)
  %p0_v14 = call i32 @sext_switch(i32 -2147483645)
  %p0_14 = call i32 @mix(i32 %p0_13, i32 %p0_v14)
  %c0 = icmp ne i32 %p0_14, -1034356567
  %p1_v0 = call i32 @zext_switch(i32 -1)
  %p1_0 = call i32 @mix(i32 0, i32 %p1_v0)
  %p1_v1 = call i32 @zext_switch(i32 -8)
  %p1_1 = call i32 @mix(i32 %p1_0, i32 %p1_v1)
  %p1_v2 = call i32 @zext_switch(i32 -9)
  %p1_2 = call i32 @mix(i32 %p1_1, i32 %p1_v2)
  %p1_v3 = call i32 @zext_switch(i32 7)
  %p1_3 = call i32 @mix(i32 %p1_2, i32 %p1_v3)
  %p1_v4 = call i32 @zext_switch(i32 0)
  %p1_4 = call i32 @mix(i32 %p1_3, i32 %p1_v4)
  %p1_v5 = call i32 @zext_switch(i32 -5)
  %p1_5 = call i32 @mix(i32 %p1_4, i32 %p1_v5)
  %p1_v6 = call i32 @zext_switch(i32 -2)
  %p1_6 = call i32 @mix(i32 %p1_5, i32 %p1_v6)
  %p1_v7 = call i32 @zext_switch(i32 -2147483648)
  %p1_7 = call i32 @mix(i32 %p1_6, i32 %p1_v7)
  %c1 = icmp ne i32 %p1_7, 1645445108
  %o1 = or i1 %c0, %c1
  %r = zext i1 %o1 to i32
  ret i32 %r
}

; Bit 4: boundary(x): the cases 2^32 - 4 .. 2^32 + 3, then the default for
; -4, -3, -2, -1, 0, 1, 2, 3, 8589934592, 4294967291, 4294967300.
define internal i32 @check_boundary() noinline {
  %p0_v0 = call i32 @boundary(i64 4294967292)
  %p0_0 = call i32 @mix(i32 0, i32 %p0_v0)
  %p0_v1 = call i32 @boundary(i64 4294967293)
  %p0_1 = call i32 @mix(i32 %p0_0, i32 %p0_v1)
  %p0_v2 = call i32 @boundary(i64 4294967294)
  %p0_2 = call i32 @mix(i32 %p0_1, i32 %p0_v2)
  %p0_v3 = call i32 @boundary(i64 4294967295)
  %p0_3 = call i32 @mix(i32 %p0_2, i32 %p0_v3)
  %p0_v4 = call i32 @boundary(i64 4294967296)
  %p0_4 = call i32 @mix(i32 %p0_3, i32 %p0_v4)
  %p0_v5 = call i32 @boundary(i64 4294967297)
  %p0_5 = call i32 @mix(i32 %p0_4, i32 %p0_v5)
  %p0_v6 = call i32 @boundary(i64 4294967298)
  %p0_6 = call i32 @mix(i32 %p0_5, i32 %p0_v6)
  %p0_v7 = call i32 @boundary(i64 4294967299)
  %p0_7 = call i32 @mix(i32 %p0_6, i32 %p0_v7)
  %p0_v8 = call i32 @boundary(i64 -4)
  %p0_8 = call i32 @mix(i32 %p0_7, i32 %p0_v8)
  %p0_v9 = call i32 @boundary(i64 -3)
  %p0_9 = call i32 @mix(i32 %p0_8, i32 %p0_v9)
  %p0_v10 = call i32 @boundary(i64 -2)
  %p0_10 = call i32 @mix(i32 %p0_9, i32 %p0_v10)
  %p0_v11 = call i32 @boundary(i64 -1)
  %p0_11 = call i32 @mix(i32 %p0_10, i32 %p0_v11)
  %p0_v12 = call i32 @boundary(i64 0)
  %p0_12 = call i32 @mix(i32 %p0_11, i32 %p0_v12)
  %p0_v13 = call i32 @boundary(i64 1)
  %p0_13 = call i32 @mix(i32 %p0_12, i32 %p0_v13)
  %p0_v14 = call i32 @boundary(i64 2)
  %p0_14 = call i32 @mix(i32 %p0_13, i32 %p0_v14)
  %p0_v15 = call i32 @boundary(i64 3)
  %p0_15 = call i32 @mix(i32 %p0_14, i32 %p0_v15)
  %p0_v16 = call i32 @boundary(i64 8589934592)
  %p0_16 = call i32 @mix(i32 %p0_15, i32 %p0_v16)
  %p0_v17 = call i32 @boundary(i64 4294967291)
  %p0_17 = call i32 @mix(i32 %p0_16, i32 %p0_v17)
  %p0_v18 = call i32 @boundary(i64 4294967300)
  %p0_18 = call i32 @mix(i32 %p0_17, i32 %p0_v18)
  %c0 = icmp ne i32 %p0_18, 706881539
  %r = zext i1 %c0 to i32
  ret i32 %r
}

; Bit 5: dense64(x) in a loop over x = 2^31 * k + k, k = 0..9: only x = 0
; is a case; the low 32 bits of x are k for even k.
define internal i32 @check_stride() noinline {
entry:
  br label %loop

loop:
  %k = phi i32 [ 0, %entry ], [ %k1, %loop ]
  %x = phi i64 [ 0, %entry ], [ %x1, %loop ]
  %h = phi i32 [ 0, %entry ], [ %h1, %loop ]
  %f = call i32 @dense64(i64 %x)
  %h1 = call i32 @mix(i32 %h, i32 %f)
  %x1 = add i64 %x, 2147483649
  %k1 = add i32 %k, 1
  %more = icmp slt i32 %k1, 10
  br i1 %more, label %loop, label %done

done:
  %c = icmp ne i32 %h1, 202547607
  %r = zext i1 %c to i32
  ret i32 %r
}

define internal i64 @shift_down(i64 %x) noinline {
  %r = ashr i64 %x, 8
  ret i64 %r
}

; switch_on_call(x, v): switch (shift_down(x)), cases 0..5; v is live across
; the switch and the call result feeds it directly.
define internal i32 @switch_on_call(i64 %x, i32 %v) noinline {
entry:
  %k = call i64 @shift_down(i64 %x)
  %kt = trunc i64 %k to i32
  switch i64 %k, label %def [
    i64 0, label %c0
    i64 1, label %c1
    i64 2, label %c2
    i64 3, label %c3
    i64 4, label %c4
    i64 5, label %c5
  ]

c0:
  %r0 = add i32 %v, %kt
  ret i32 %r0

c1:
  %r1 = mul i32 %v, 2
  ret i32 %r1

c2:
  ret i32 33

c3:
  %r3 = xor i32 %v, %kt
  ret i32 %r3

c4:
  ret i32 55

c5:
  %r5 = sub i32 %v, %kt
  ret i32 %r5

def:
  %rd = sub i32 %v, 1
  ret i32 %rd
}

; Bit 6: switch_on_call(x, 1000) for shift_down(x) = -2..8, 2^32 + 3 and
; 2^32 (x is shown):
; -422, -166, 90, 346, 602, 858, 1114, 1370, 1626, 1882, 2138, 1099511628545,
; 1099511627776.
define internal i32 @check_on_call() noinline {
  %v0 = call i32 @switch_on_call(i64 -422, i32 1000)
  %h0 = call i32 @mix(i32 0, i32 %v0)
  %v1 = call i32 @switch_on_call(i64 -166, i32 1000)
  %h1 = call i32 @mix(i32 %h0, i32 %v1)
  %v2 = call i32 @switch_on_call(i64 90, i32 1000)
  %h2 = call i32 @mix(i32 %h1, i32 %v2)
  %v3 = call i32 @switch_on_call(i64 346, i32 1000)
  %h3 = call i32 @mix(i32 %h2, i32 %v3)
  %v4 = call i32 @switch_on_call(i64 602, i32 1000)
  %h4 = call i32 @mix(i32 %h3, i32 %v4)
  %v5 = call i32 @switch_on_call(i64 858, i32 1000)
  %h5 = call i32 @mix(i32 %h4, i32 %v5)
  %v6 = call i32 @switch_on_call(i64 1114, i32 1000)
  %h6 = call i32 @mix(i32 %h5, i32 %v6)
  %v7 = call i32 @switch_on_call(i64 1370, i32 1000)
  %h7 = call i32 @mix(i32 %h6, i32 %v7)
  %v8 = call i32 @switch_on_call(i64 1626, i32 1000)
  %h8 = call i32 @mix(i32 %h7, i32 %v8)
  %v9 = call i32 @switch_on_call(i64 1882, i32 1000)
  %h9 = call i32 @mix(i32 %h8, i32 %v9)
  %v10 = call i32 @switch_on_call(i64 2138, i32 1000)
  %h10 = call i32 @mix(i32 %h9, i32 %v10)
  %v11 = call i32 @switch_on_call(i64 1099511628545, i32 1000)
  %h11 = call i32 @mix(i32 %h10, i32 %v11)
  %v12 = call i32 @switch_on_call(i64 1099511627776, i32 1000)
  %h12 = call i32 @mix(i32 %h11, i32 %v12)
  %c = icmp ne i32 %h12, 921009961
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @main() {
  %b0 = call i32 @check_dense64()
  %b1 = call i32 @check_2p32p1()
  %b2 = call i32 @check_offset64()
  %b3 = call i32 @check_ext()
  %b4 = call i32 @check_boundary()
  %b5 = call i32 @check_stride()
  %b6 = call i32 @check_on_call()
  %s1 = shl i32 %b1, 1
  %s2 = shl i32 %b2, 2
  %s3 = shl i32 %b3, 3
  %s4 = shl i32 %b4, 4
  %s5 = shl i32 %b5, 5
  %s6 = shl i32 %b6, 6
  %m1 = or i32 %b0, %s1
  %m2 = or i32 %m1, %s2
  %m3 = or i32 %m2, %s3
  %m4 = or i32 %m3, %s4
  %m5 = or i32 %m4, %s5
  %m6 = or i32 %m5, %s6
  ret i32 %m6
}
