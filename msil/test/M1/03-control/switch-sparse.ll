; Sparse switches (Milestone 6): case values too far apart for one jump
; table become a binary tree of compares, clusters of nearby values become
; jump tables (the CIL 'switch' on an int32 index, D35) inside that tree, and
; few destinations over a small range become bit tests (a shift of 1 by the
; case offset and a mask, in int32 or native int). int32 cases at INT_MIN,
; INT_MAX, -1000000 and 65537; a dense cluster with far-away cases around
; it; bit tests on int32 and native int ranges (cases 0..63, so the mask
; needs 64 bits; values whose low bits look like a case must still go to the
; default); and native int cases at 2^32, 2^32 + 1, 2^40, INT64_MIN and
; INT64_MAX. Every case value and its neighbours are tried. Each check
; folds the results with h = h * 31 + f(x) and returns 1 when the hash
; differs from the value C gives (in the comments); main ORs one bit per
; check into the exit status (8 bits, D37): [exit 0] means every check
; passed.
; RUN: %{msil_ll}

; sparse(x): nine far-apart cases; otherwise (x & 0xFFF) ^ 0xABC.
define internal i32 @sparse(i32 %x) noinline {
entry:
  switch i32 %x, label %def [
    i32 -2147483648, label %cmin
    i32 -1000000, label %cm1e6
    i32 -7, label %cm7
    i32 0, label %c0
    i32 3, label %c3
    i32 100, label %c100
    i32 1024, label %c1024
    i32 65537, label %c65537
    i32 2147483647, label %cmax
  ]

cmin:
  %rmin = xor i32 %x, 252645135
  br label %join

cm1e6:
  br label %join

cm7:
  %rm7 = mul i32 %x, %x
  br label %join

c0:
  br label %join

c3:
  %r3 = add i32 %x, 77
  br label %join

c100:
  br label %join

c1024:
  %r1024 = ashr i32 %x, 3
  br label %join

c65537:
  %r65537 = sub i32 %x, 65536
  br label %join

cmax:
  br label %join

def:
  %lo = and i32 %x, 4095
  %rd = xor i32 %lo, 2748
  br label %join

join:
  %r = phi i32 [ %rmin, %cmin ], [ 11, %cm1e6 ], [ %rm7, %cm7 ], [ 13, %c0 ],
               [ %r3, %c3 ], [ 15, %c100 ], [ %r1024, %c1024 ],
               [ %r65537, %c65537 ], [ 19, %cmax ], [ %rd, %def ]
  ret i32 %r
}

; mixed(x): a dense cluster 10..17 and the far cases -50, 1000 and 5000;
; otherwise 0.
define internal i32 @mixed(i32 %x) noinline {
entry:
  switch i32 %x, label %def [
    i32 10, label %c10
    i32 11, label %c11
    i32 12, label %c12
    i32 13, label %c13
    i32 14, label %c14
    i32 15, label %c15
    i32 16, label %c16
    i32 17, label %c17
    i32 1000, label %c1000
    i32 -50, label %cm50
    i32 5000, label %c5000
  ]

c10:
  %r10 = mul i32 %x, 3
  br label %join

c11:
  br label %join

c12:
  %r12 = xor i32 %x, 119
  br label %join

c13:
  br label %join

c14:
  %r14 = sub i32 %x, 40
  br label %join

c15:
  br label %join

c16:
  %r16 = shl i32 %x, 2
  br label %join

c17:
  br label %join

c1000:
  %r1000 = sdiv i32 %x, 10
  br label %join

cm50:
  %rm50 = mul i32 %x, %x
  br label %join

c5000:
  br label %join

def:
  br label %join

join:
  %r = phi i32 [ %r10, %c10 ], [ 21, %c11 ], [ %r12, %c12 ], [ 23, %c13 ],
               [ %r14, %c14 ], [ 25, %c15 ], [ %r16, %c16 ], [ 27, %c17 ],
               [ %r1000, %c1000 ], [ %rm50, %cm50 ], [ 5, %c5000 ], [ 0, %def ]
  ret i32 %r
}

; bittest(x): the squares 1..49 -> x * 2, the primes below 50 -> x + 100,
; otherwise -x (three destinations over the range 1..49).
define internal i32 @bittest(i32 %x) noinline {
entry:
  switch i32 %x, label %other [
    i32 1, label %square
    i32 4, label %square
    i32 9, label %square
    i32 16, label %square
    i32 25, label %square
    i32 36, label %square
    i32 49, label %square
    i32 2, label %prime
    i32 3, label %prime
    i32 5, label %prime
    i32 7, label %prime
    i32 11, label %prime
    i32 13, label %prime
    i32 17, label %prime
    i32 19, label %prime
    i32 23, label %prime
    i32 29, label %prime
    i32 31, label %prime
    i32 37, label %prime
    i32 41, label %prime
    i32 43, label %prime
    i32 47, label %prime
  ]

square:
  %rs = mul i32 %x, 2
  ret i32 %rs

prime:
  %rp = add i32 %x, 100
  ret i32 %rp

other:
  %ro = sub i32 0, %x
  ret i32 %ro
}

; bittest64(x): native int cases 0, 2, 5, 9, 33, 63 -> 1; 1, 40, 62 -> 2;
; otherwise 3 (the mask spans bits 0..63).
define internal i32 @bittest64(i64 %x) noinline {
entry:
  switch i64 %x, label %other [
    i64 0, label %a
    i64 2, label %a
    i64 5, label %a
    i64 9, label %a
    i64 33, label %a
    i64 63, label %a
    i64 1, label %b
    i64 40, label %b
    i64 62, label %b
  ]

a:
  ret i32 1

b:
  ret i32 2

other:
  ret i32 3
}

; sparse_i64(x): native int cases -1, 0, 7, 2^32, 2^32 + 1, 2^40, INT64_MAX
; and INT64_MIN; otherwise x >> 1 (arithmetic).
define internal i64 @sparse_i64(i64 %x) noinline {
entry:
  switch i64 %x, label %def [
    i64 -1, label %cm1
    i64 0, label %c0
    i64 7, label %c7
    i64 4294967296, label %c2p32
    i64 4294967297, label %c2p32p1
    i64 1099511627776, label %c2p40
    i64 9223372036854775807, label %cmax
    i64 -9223372036854775808, label %cmin
  ]

cm1:
  br label %join

c0:
  %r0 = add i64 %x, 102
  br label %join

c7:
  %r7 = mul i64 %x, 3
  br label %join

c2p32:
  %r2p32 = ashr i64 %x, 16
  br label %join

c2p32p1:
  br label %join

c2p40:
  %r2p40 = xor i64 %x, 65535
  br label %join

cmax:
  br label %join

cmin:
  %rmin = add i64 %x, 1
  br label %join

def:
  %rd = ashr i64 %x, 1
  br label %join

join:
  %r = phi i64 [ 101, %cm1 ], [ %r0, %c0 ], [ %r7, %c7 ], [ %r2p32, %c2p32 ],
               [ 105, %c2p32p1 ], [ %r2p40, %c2p40 ], [ 107, %cmax ],
               [ %rmin, %cmin ], [ %rd, %def ]
  ret i64 %r
}

define internal i32 @mix(i32 %h, i32 %f) noinline {
  %m = mul i32 %h, 31
  %r = add i32 %m, %f
  ret i32 %r
}

define internal i64 @mix64(i64 %h, i64 %f) noinline {
  %m = mul i64 %h, 31
  %r = add i64 %m, %f
  ret i64 %r
}

; Bit 0: sparse(x) for every case value and its neighbours:
; -2147483648, -2147483647, -1000001, -1000000, -999999, -8, -7, -6, -1, 0, 1,
; 2, 3, 4, 99, 100, 101, 1023, 1024, 1025, 65536, 65537, 65538, 2147483646,
; 2147483647.
define internal i32 @check_sparse() noinline {
  %hv0 = call i32 @sparse(i32 -2147483648)
  %h0 = call i32 @mix(i32 0, i32 %hv0)
  %hv1 = call i32 @sparse(i32 -2147483647)
  %h1 = call i32 @mix(i32 %h0, i32 %hv1)
  %hv2 = call i32 @sparse(i32 -1000001)
  %h2 = call i32 @mix(i32 %h1, i32 %hv2)
  %hv3 = call i32 @sparse(i32 -1000000)
  %h3 = call i32 @mix(i32 %h2, i32 %hv3)
  %hv4 = call i32 @sparse(i32 -999999)
  %h4 = call i32 @mix(i32 %h3, i32 %hv4)
  %hv5 = call i32 @sparse(i32 -8)
  %h5 = call i32 @mix(i32 %h4, i32 %hv5)
  %hv6 = call i32 @sparse(i32 -7)
  %h6 = call i32 @mix(i32 %h5, i32 %hv6)
  %hv7 = call i32 @sparse(i32 -6)
  %h7 = call i32 @mix(i32 %h6, i32 %hv7)
  %hv8 = call i32 @sparse(i32 -1)
  %h8 = call i32 @mix(i32 %h7, i32 %hv8)
  %hv9 = call i32 @sparse(i32 0)
  %h9 = call i32 @mix(i32 %h8, i32 %hv9)
  %hv10 = call i32 @sparse(i32 1)
  %h10 = call i32 @mix(i32 %h9, i32 %hv10)
  %hv11 = call i32 @sparse(i32 2)
  %h11 = call i32 @mix(i32 %h10, i32 %hv11)
  %hv12 = call i32 @sparse(i32 3)
  %h12 = call i32 @mix(i32 %h11, i32 %hv12)
  %hv13 = call i32 @sparse(i32 4)
  %h13 = call i32 @mix(i32 %h12, i32 %hv13)
  %hv14 = call i32 @sparse(i32 99)
  %h14 = call i32 @mix(i32 %h13, i32 %hv14)
  %hv15 = call i32 @sparse(i32 100)
  %h15 = call i32 @mix(i32 %h14, i32 %hv15)
  %hv16 = call i32 @sparse(i32 101)
  %h16 = call i32 @mix(i32 %h15, i32 %hv16)
  %hv17 = call i32 @sparse(i32 1023)
  %h17 = call i32 @mix(i32 %h16, i32 %hv17)
  %hv18 = call i32 @sparse(i32 1024)
  %h18 = call i32 @mix(i32 %h17, i32 %hv18)
  %hv19 = call i32 @sparse(i32 1025)
  %h19 = call i32 @mix(i32 %h18, i32 %hv19)
  %hv20 = call i32 @sparse(i32 65536)
  %h20 = call i32 @mix(i32 %h19, i32 %hv20)
  %hv21 = call i32 @sparse(i32 65537)
  %h21 = call i32 @mix(i32 %h20, i32 %hv21)
  %hv22 = call i32 @sparse(i32 65538)
  %h22 = call i32 @mix(i32 %h21, i32 %hv22)
  %hv23 = call i32 @sparse(i32 2147483646)
  %h23 = call i32 @mix(i32 %h22, i32 %hv23)
  %hv24 = call i32 @sparse(i32 2147483647)
  %h24 = call i32 @mix(i32 %h23, i32 %hv24)
  %c = icmp ne i32 %h24, 1901205134
  %r = zext i1 %c to i32
  ret i32 %r
}

; Bit 1: mixed(x) for x from 5 to 20 (around the cluster), then
; 999, 1000, 1001, 4999, 5000, 5001, -51, -50, -49.
define internal i32 @check_mixed() noinline {
entry:
  br label %loop

loop:
  %x = phi i32 [ 5, %entry ], [ %x1, %loop ]
  %h = phi i32 [ 0, %entry ], [ %h1, %loop ]
  %f = call i32 @mixed(i32 %x)
  %h1 = call i32 @mix(i32 %h, i32 %f)
  %x1 = add i32 %x, 1
  %more = icmp sle i32 %x1, 20
  br i1 %more, label %loop, label %done

done:
  %tv0 = call i32 @mixed(i32 999)
  %t0 = call i32 @mix(i32 %h1, i32 %tv0)
  %tv1 = call i32 @mixed(i32 1000)
  %t1 = call i32 @mix(i32 %t0, i32 %tv1)
  %tv2 = call i32 @mixed(i32 1001)
  %t2 = call i32 @mix(i32 %t1, i32 %tv2)
  %tv3 = call i32 @mixed(i32 4999)
  %t3 = call i32 @mix(i32 %t2, i32 %tv3)
  %tv4 = call i32 @mixed(i32 5000)
  %t4 = call i32 @mix(i32 %t3, i32 %tv4)
  %tv5 = call i32 @mixed(i32 5001)
  %t5 = call i32 @mix(i32 %t4, i32 %tv5)
  %tv6 = call i32 @mixed(i32 -51)
  %t6 = call i32 @mix(i32 %t5, i32 %tv6)
  %tv7 = call i32 @mixed(i32 -50)
  %t7 = call i32 @mix(i32 %t6, i32 %tv7)
  %tv8 = call i32 @mixed(i32 -49)
  %t8 = call i32 @mix(i32 %t7, i32 %tv8)
  %c = icmp ne i32 %t8, -966142626
  %r = zext i1 %c to i32
  ret i32 %r
}

; Bit 2: bittest(x) for x from -2 to 70.
define internal i32 @check_bittest() noinline {
entry:
  br label %loop

loop:
  %x = phi i32 [ -2, %entry ], [ %x1, %loop ]
  %h = phi i32 [ 0, %entry ], [ %h1, %loop ]
  %f = call i32 @bittest(i32 %x)
  %h1 = call i32 @mix(i32 %h, i32 %f)
  %x1 = add i32 %x, 1
  %more = icmp sle i32 %x1, 70
  br i1 %more, label %loop, label %done

done:
  %c = icmp ne i32 %h1, 59821166
  %r = zext i1 %c to i32
  ret i32 %r
}

; Bit 3: bittest64(x) for x from -3 to 70, then values whose low bits match a
; case: 4294967298, 4294967297, 8589934632,
; -9223372036854775808, -9223372036854775745, -64 (all the default).
define internal i32 @check_bittest64() noinline {
entry:
  br label %loop

loop:
  %x = phi i64 [ -3, %entry ], [ %x1, %loop ]
  %h = phi i32 [ 0, %entry ], [ %h1, %loop ]
  %f = call i32 @bittest64(i64 %x)
  %h1 = call i32 @mix(i32 %h, i32 %f)
  %x1 = add i64 %x, 1
  %more = icmp sle i64 %x1, 70
  br i1 %more, label %loop, label %done

done:
  %tv0 = call i32 @bittest64(i64 4294967298)
  %t0 = call i32 @mix(i32 %h1, i32 %tv0)
  %tv1 = call i32 @bittest64(i64 4294967297)
  %t1 = call i32 @mix(i32 %t0, i32 %tv1)
  %tv2 = call i32 @bittest64(i64 8589934632)
  %t2 = call i32 @mix(i32 %t1, i32 %tv2)
  %tv3 = call i32 @bittest64(i64 -9223372036854775808)
  %t3 = call i32 @mix(i32 %t2, i32 %tv3)
  %tv4 = call i32 @bittest64(i64 -9223372036854775745)
  %t4 = call i32 @mix(i32 %t3, i32 %tv4)
  %tv5 = call i32 @bittest64(i64 -64)
  %t5 = call i32 @mix(i32 %t4, i32 %tv5)
  %c = icmp ne i32 %t5, -583046941
  %r = zext i1 %c to i32
  ret i32 %r
}

; Bit 4: sparse_i64(x) for every case value and its neighbours:
; -1, 0, 7, 4294967296, 4294967297, 1099511627776, 9223372036854775807,
; -9223372036854775808, 1, 6, 8, -2, 4294967295, 4294967298, 1099511627777,
; 1099511627775, 9223372036854775806, -9223372036854775807, 4294967303,
; -4294967289.
define internal i32 @check_sparse_i64() noinline {
  %hv0 = call i64 @sparse_i64(i64 -1)
  %h0 = call i64 @mix64(i64 0, i64 %hv0)
  %hv1 = call i64 @sparse_i64(i64 0)
  %h1 = call i64 @mix64(i64 %h0, i64 %hv1)
  %hv2 = call i64 @sparse_i64(i64 7)
  %h2 = call i64 @mix64(i64 %h1, i64 %hv2)
  %hv3 = call i64 @sparse_i64(i64 4294967296)
  %h3 = call i64 @mix64(i64 %h2, i64 %hv3)
  %hv4 = call i64 @sparse_i64(i64 4294967297)
  %h4 = call i64 @mix64(i64 %h3, i64 %hv4)
  %hv5 = call i64 @sparse_i64(i64 1099511627776)
  %h5 = call i64 @mix64(i64 %h4, i64 %hv5)
  %hv6 = call i64 @sparse_i64(i64 9223372036854775807)
  %h6 = call i64 @mix64(i64 %h5, i64 %hv6)
  %hv7 = call i64 @sparse_i64(i64 -9223372036854775808)
  %h7 = call i64 @mix64(i64 %h6, i64 %hv7)
  %hv8 = call i64 @sparse_i64(i64 1)
  %h8 = call i64 @mix64(i64 %h7, i64 %hv8)
  %hv9 = call i64 @sparse_i64(i64 6)
  %h9 = call i64 @mix64(i64 %h8, i64 %hv9)
  %hv10 = call i64 @sparse_i64(i64 8)
  %h10 = call i64 @mix64(i64 %h9, i64 %hv10)
  %hv11 = call i64 @sparse_i64(i64 -2)
  %h11 = call i64 @mix64(i64 %h10, i64 %hv11)
  %hv12 = call i64 @sparse_i64(i64 4294967295)
  %h12 = call i64 @mix64(i64 %h11, i64 %hv12)
  %hv13 = call i64 @sparse_i64(i64 4294967298)
  %h13 = call i64 @mix64(i64 %h12, i64 %hv13)
  %hv14 = call i64 @sparse_i64(i64 1099511627777)
  %h14 = call i64 @mix64(i64 %h13, i64 %hv14)
  %hv15 = call i64 @sparse_i64(i64 1099511627775)
  %h15 = call i64 @mix64(i64 %h14, i64 %hv15)
  %hv16 = call i64 @sparse_i64(i64 9223372036854775806)
  %h16 = call i64 @mix64(i64 %h15, i64 %hv16)
  %hv17 = call i64 @sparse_i64(i64 -9223372036854775807)
  %h17 = call i64 @mix64(i64 %h16, i64 %hv17)
  %hv18 = call i64 @sparse_i64(i64 4294967303)
  %h18 = call i64 @mix64(i64 %h17, i64 %hv18)
  %hv19 = call i64 @sparse_i64(i64 -4294967289)
  %h19 = call i64 @mix64(i64 %h18, i64 %hv19)
  %c = icmp ne i64 %h19, 7361031143194027448
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @main() {
  %b0 = call i32 @check_sparse()
  %b1 = call i32 @check_mixed()
  %b2 = call i32 @check_bittest()
  %b3 = call i32 @check_bittest64()
  %b4 = call i32 @check_sparse_i64()
  %s1 = shl i32 %b1, 1
  %s2 = shl i32 %b2, 2
  %s3 = shl i32 %b3, 3
  %s4 = shl i32 %b4, 4
  %m1 = or i32 %b0, %s1
  %m2 = or i32 %m1, %s2
  %m3 = or i32 %m2, %s3
  %m4 = or i32 %m3, %s4
  ret i32 %m4
}
