; Dense switches (Milestone 6): jump tables, which become the CIL 'switch'
; opcode on an int32 index (D35). LLVM's range check before the 'switch'
; sends every out-of-range value to the default, so the 'br' after the
; 'switch' goes to the first jump-table entry, a dummy fall-through target
; that never runs (switch contract, plan section 12). Covered: cases 0..9
; with values below, inside and above the table; a negative range with a
; hole; a range that starts at 1000 (the index is x - 1000); several cases
; that share a destination (a jump table or bit tests); an i8 switch whose
; cases cross the signed boundary 127/-128; ranges at INT_MAX and INT_MIN,
; where x - low wraps for values outside the range; C-style fallthrough
; between case blocks; a default that is unreachable (no range check, so
; only in-range values are used); and a state machine that runs a switch in
; a loop. Every case computes a different function of its inputs (not only
; constants). hash_<f> runs f over a range in a loop and folds the results
; with h = h * 31 + f(x); each noinline check function returns 1 when a hash
; differs from the value C gives (in the comments); main ORs one bit per check
; into the exit status (8 bits, D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

; dense(x, v): x = 0..9 -> ten different functions of v; otherwise ~v.
define internal i32 @dense(i32 %x, i32 %v) noinline {
entry:
  switch i32 %x, label %def [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
    i32 5, label %c5
    i32 6, label %c6
    i32 7, label %c7
    i32 8, label %c8
    i32 9, label %c9
  ]

c0:
  %r0 = add i32 %v, 11
  br label %join

c1:
  %r1 = mul i32 %v, 3
  br label %join

c2:
  %r2 = xor i32 %v, 23130
  br label %join

c3:
  %r3 = sub i32 %v, 100
  br label %join

c4:
  %r4 = shl i32 %v, 3
  br label %join

c5:
  %r5 = lshr i32 %v, 2
  br label %join

c6:
  %r6 = mul i32 %v, %v
  br label %join

c7:
  %r7 = sub i32 0, %v
  br label %join

c8:
  %r8 = or i32 %v, 65536
  br label %join

c9:
  %r9 = and i32 %v, 255
  br label %join

def:
  %rd = xor i32 %v, -1
  br label %join

join:
  %r = phi i32 [ %r0, %c0 ], [ %r1, %c1 ], [ %r2, %c2 ], [ %r3, %c3 ],
               [ %r4, %c4 ], [ %r5, %c5 ], [ %r6, %c6 ], [ %r7, %c7 ],
               [ %r8, %c8 ], [ %r9, %c9 ], [ %rd, %def ]
  ret i32 %r
}

; h = 0; for (x = lo; x <= hi; x++) h = h * 31 + dense(x, v + x);
define internal i32 @hash_dense(i32 %lo, i32 %hi, i32 %v) noinline {
entry:
  br label %loop

loop:
  %x = phi i32 [ %lo, %entry ], [ %x1, %loop ]
  %h = phi i32 [ 0, %entry ], [ %h2, %loop ]
  %vx = add i32 %v, %x
  %f = call i32 @dense(i32 %x, i32 %vx)
  %h1 = mul i32 %h, 31
  %h2 = add i32 %h1, %f
  %x1 = add i32 %x, 1
  %more = icmp sle i32 %x1, %hi
  br i1 %more, label %loop, label %exit

exit:
  ret i32 %h2
}

; neg_range(x): cases -4..4 without 0; otherwise x * 2.
define internal i32 @neg_range(i32 %x) noinline {
entry:
  switch i32 %x, label %def [
    i32 -4, label %m4
    i32 -3, label %m3
    i32 -2, label %m2
    i32 -1, label %m1
    i32 1, label %p1
    i32 2, label %p2
    i32 3, label %p3
    i32 4, label %p4
  ]

m4:
  br label %join

m3:
  %rm3 = mul i32 %x, 7
  br label %join

m2:
  br label %join

m1:
  %rm1 = add i32 %x, 100
  br label %join

p1:
  %sq = mul i32 %x, %x
  %rp1 = add i32 %sq, 1
  br label %join

p2:
  br label %join

p3:
  %rp3 = sub i32 %x, 50
  br label %join

p4:
  br label %join

def:
  %rd = mul i32 %x, 2
  br label %join

join:
  %r = phi i32 [ 40, %m4 ], [ %rm3, %m3 ], [ 22, %m2 ], [ %rm1, %m1 ],
               [ %rp1, %p1 ], [ 77, %p2 ], [ %rp3, %p3 ], [ 1000, %p4 ],
               [ %rd, %def ]
  ret i32 %r
}

; offset_range(x): cases 1000..1007; otherwise -x.
define internal i32 @offset_range(i32 %x) noinline {
entry:
  switch i32 %x, label %def [
    i32 1000, label %c0
    i32 1001, label %c1
    i32 1002, label %c2
    i32 1003, label %c3
    i32 1004, label %c4
    i32 1005, label %c5
    i32 1006, label %c6
    i32 1007, label %c7
  ]

c0:
  %r0 = sub i32 %x, 1000
  br label %join

c1:
  %r1 = mul i32 %x, 2
  br label %join

c2:
  br label %join

c3:
  %r3 = xor i32 %x, 255
  br label %join

c4:
  %r4 = lshr i32 %x, 1
  br label %join

c5:
  %sq = mul i32 %x, %x
  %r5 = add i32 %x, %sq
  br label %join

c6:
  br label %join

c7:
  %r7 = sub i32 %x, 7
  br label %join

def:
  %rd = sub i32 0, %x
  br label %join

join:
  %r = phi i32 [ %r0, %c0 ], [ %r1, %c1 ], [ 5, %c2 ], [ %r3, %c3 ],
               [ %r4, %c4 ], [ %r5, %c5 ], [ 123456, %c6 ], [ %r7, %c7 ],
               [ %rd, %def ]
  ret i32 %r
}

; shared(x): x = 0..11 -> one of three blocks by x % 3; otherwise -1.
define internal i32 @shared(i32 %x) noinline {
entry:
  switch i32 %x, label %def [
    i32 0, label %a
    i32 1, label %b
    i32 2, label %c
    i32 3, label %a
    i32 4, label %b
    i32 5, label %c
    i32 6, label %a
    i32 7, label %b
    i32 8, label %c
    i32 9, label %a
    i32 10, label %b
    i32 11, label %c
  ]

a:
  %ra = mul i32 %x, 5
  br label %join

b:
  %rb = add i32 %x, 7
  br label %join

c:
  %rc = xor i32 %x, 9
  br label %join

def:
  br label %join

join:
  %r = phi i32 [ %ra, %a ], [ %rb, %b ], [ %rc, %c ], [ -1, %def ]
  ret i32 %r
}

; i8_switch(x): signed char cases 124..127 and -128..-125; otherwise 0.
define internal i32 @i8_switch(i8 %x) noinline {
entry:
  %w = sext i8 %x to i32
  switch i8 %x, label %def [
    i8 124, label %c124
    i8 125, label %c125
    i8 126, label %c126
    i8 127, label %c127
    i8 -128, label %cm128
    i8 -127, label %cm127
    i8 -126, label %cm126
    i8 -125, label %cm125
  ]

c124:
  br label %join

c125:
  %r125 = mul i32 %w, 2
  br label %join

c126:
  br label %join

c127:
  %r127 = add i32 %w, 1000
  br label %join

cm128:
  %rm128 = mul i32 %w, 3
  br label %join

cm127:
  br label %join

cm126:
  %rm126 = sub i32 %w, 1000
  br label %join

cm125:
  br label %join

def:
  br label %join

join:
  %r = phi i32 [ 1, %c124 ], [ %r125, %c125 ], [ 3, %c126 ], [ %r127, %c127 ],
               [ %rm128, %cm128 ], [ 6, %cm127 ], [ %rm126, %cm126 ],
               [ 8, %cm125 ], [ 0, %def ]
  ret i32 %r
}

; int_max_edge(x): cases INT_MAX - 7 .. INT_MAX; otherwise -1.
define internal i32 @int_max_edge(i32 %x) noinline {
entry:
  switch i32 %x, label %def [
    i32 2147483640, label %c0
    i32 2147483641, label %c1
    i32 2147483642, label %c2
    i32 2147483643, label %c3
    i32 2147483644, label %c4
    i32 2147483645, label %c5
    i32 2147483646, label %c6
    i32 2147483647, label %c7
  ]

c0:
  %r0 = xor i32 %x, 1
  br label %join

c1:
  br label %join

c2:
  %r2 = ashr i32 %x, 4
  br label %join

c3:
  br label %join

c4:
  %r4 = and i32 %x, 65535
  br label %join

c5:
  br label %join

c6:
  %r6 = sub i32 %x, 2147483647
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

; int_min_edge(x): cases INT_MIN .. INT_MIN + 7; otherwise -1.
define internal i32 @int_min_edge(i32 %x) noinline {
entry:
  switch i32 %x, label %def [
    i32 -2147483648, label %c0
    i32 -2147483647, label %c1
    i32 -2147483646, label %c2
    i32 -2147483645, label %c3
    i32 -2147483644, label %c4
    i32 -2147483643, label %c5
    i32 -2147483642, label %c6
    i32 -2147483641, label %c7
  ]

c0:
  br label %join

c1:
  %r1 = add i32 %x, 5
  br label %join

c2:
  br label %join

c3:
  %r3 = xor i32 %x, 127
  br label %join

c4:
  br label %join

c5:
  %r5 = lshr i32 %x, 28
  br label %join

c6:
  br label %join

c7:
  %r7 = sub i32 %x, -2147483648
  br label %join

def:
  br label %join

join:
  %r = phi i32 [ 1, %c0 ], [ %r1, %c1 ], [ 3, %c2 ], [ %r3, %c3 ],
               [ 5, %c4 ], [ %r5, %c5 ], [ 7, %c6 ], [ %r7, %c7 ], [ -1, %def ]
  ret i32 %r
}

; fallthrough(x, v): switch (x) { case 0: v *= 3; /* fall through */
; case 1: v += 5; /* fall through */ case 2: return v ^ 0x55;
; case 3: return v - 1; } return v;
define internal i32 @fallthrough(i32 %x, i32 %v) noinline {
entry:
  switch i32 %x, label %def [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
  ]

c0:
  %v0 = mul i32 %v, 3
  br label %c1

c1:
  %v1 = phi i32 [ %v, %entry ], [ %v0, %c0 ]
  %v1a = add i32 %v1, 5
  br label %c2

c2:
  %v2 = phi i32 [ %v, %entry ], [ %v1a, %c1 ]
  %r2 = xor i32 %v2, 85
  ret i32 %r2

c3:
  %r3 = sub i32 %v, 1
  ret i32 %r3

def:
  ret i32 %v
}

; unreachable_default(x): cases 0..5; the default is unreachable, so the
; jump table has no range check; only x in [0, 5] is ever passed.
define internal i32 @unreachable_default(i32 %x) noinline {
entry:
  switch i32 %x, label %def [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
    i32 5, label %c5
  ]

c0:
  %m0 = mul i32 %x, 9
  %r0 = add i32 %m0, 1
  ret i32 %r0

c1:
  ret i32 17

c2:
  %r2 = xor i32 %x, 51
  ret i32 %r2

c3:
  ret i32 3000

c4:
  %r4 = shl i32 %x, 5
  ret i32 %r4

c5:
  ret i32 55

def:
  unreachable
}

; machine(n): s = 0, acc = 1; n steps of a state machine (a switch on s in
; a loop; each state updates acc and picks the next state):
;   0: acc += 1, s = 2;
;   1: acc *= 3, s = 3;
;   2: acc ^= 5, s = 1;
;   3: acc -= 2, s = acc & 1 ? 0 : 4;
;   4: acc += acc >> 2 (arithmetic), s = 5;
;   5: acc = acc * 7 + 1, s = (unsigned)acc % 3 == 0 ? 1 : 0;
; returns acc ^ (s << 28).
define internal i32 @machine(i32 %n) noinline {
entry:
  br label %head

head:
  %i = phi i32 [ 0, %entry ], [ %i1, %latch ]
  %s = phi i32 [ 0, %entry ], [ %sn, %latch ]
  %acc = phi i32 [ 1, %entry ], [ %an, %latch ]
  %more = icmp slt i32 %i, %n
  br i1 %more, label %body, label %exit

body:
  switch i32 %s, label %latch [
    i32 0, label %s0
    i32 1, label %s1
    i32 2, label %s2
    i32 3, label %s3
    i32 4, label %s4
    i32 5, label %s5
  ]

s0:
  %a0 = add i32 %acc, 1
  br label %latch

s1:
  %a1 = mul i32 %acc, 3
  br label %latch

s2:
  %a2 = xor i32 %acc, 5
  br label %latch

s3:
  %a3 = sub i32 %acc, 2
  %odd = and i32 %a3, 1
  %isodd = icmp ne i32 %odd, 0
  br i1 %isodd, label %s3odd, label %latch

s3odd:
  br label %latch

s4:
  %q = ashr i32 %acc, 2
  %a4 = add i32 %acc, %q
  br label %latch

s5:
  %m = mul i32 %acc, 7
  %a5 = add i32 %m, 1
  %rem = urem i32 %a5, 3
  %by3 = icmp eq i32 %rem, 0
  br i1 %by3, label %s5one, label %latch

s5one:
  br label %latch

latch:
  %sn = phi i32 [ %s, %body ], [ 2, %s0 ], [ 3, %s1 ], [ 1, %s2 ],
                [ 4, %s3 ], [ 0, %s3odd ], [ 5, %s4 ], [ 0, %s5 ], [ 1, %s5one ]
  %an = phi i32 [ %acc, %body ], [ %a0, %s0 ], [ %a1, %s1 ], [ %a2, %s2 ],
                [ %a3, %s3 ], [ %a3, %s3odd ], [ %a4, %s4 ], [ %a5, %s5 ],
                [ %a5, %s5one ]
  %i1 = add i32 %i, 1
  br label %head

exit:
  %sh = shl i32 %s, 28
  %r = xor i32 %acc, %sh
  ret i32 %r
}

; h = h * 31 + f(x) for one more value.
define internal i32 @mix(i32 %h, i32 %f) noinline {
  %m = mul i32 %h, 31
  %r = add i32 %m, %f
  ret i32 %r
}

; Bit 0: cases 0..9; x from -3 to 13 (below, inside and above the table).
define internal i32 @check_dense() noinline {
  %h = call i32 @hash_dense(i32 -3, i32 13, i32 1000)
  %bad = icmp ne i32 %h, 1697309727
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 1: a negative range with a hole (x from -7 to 7) and a range at 1000
; (x from 995 to 1012).
define internal i32 @check_ranges() noinline {
entry:
  br label %loop1

loop1:
  %x = phi i32 [ -7, %entry ], [ %x1, %loop1 ]
  %h = phi i32 [ 0, %entry ], [ %h1, %loop1 ]
  %f = call i32 @neg_range(i32 %x)
  %h1 = call i32 @mix(i32 %h, i32 %f)
  %x1 = add i32 %x, 1
  %more = icmp sle i32 %x1, 7
  br i1 %more, label %loop1, label %loop2

loop2:
  %y = phi i32 [ 995, %loop1 ], [ %y1, %loop2 ]
  %g = phi i32 [ 0, %loop1 ], [ %g1, %loop2 ]
  %e = call i32 @offset_range(i32 %y)
  %g1 = call i32 @mix(i32 %g, i32 %e)
  %y1 = add i32 %y, 1
  %more2 = icmp sle i32 %y1, 1012
  br i1 %more2, label %loop2, label %done

done:
  %b1 = icmp ne i32 %h1, 1822709454
  %b2 = icmp ne i32 %g1, 1177012842
  %o = or i1 %b1, %b2
  %r = zext i1 %o to i32
  ret i32 %r
}

; Bit 2: shared destinations; x from -2 to 14.
define internal i32 @check_shared() noinline {
entry:
  br label %loop

loop:
  %x = phi i32 [ -2, %entry ], [ %x1, %loop ]
  %h = phi i32 [ 0, %entry ], [ %h1, %loop ]
  %f = call i32 @shared(i32 %x)
  %h1 = call i32 @mix(i32 %h, i32 %f)
  %x1 = add i32 %x, 1
  %more = icmp sle i32 %x1, 14
  br i1 %more, label %loop, label %done

done:
  %bad = icmp ne i32 %h1, -101204251
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 3: the i8 switch; x from 120 to 135 truncated to signed char
; (120..127, then -128..-121).
define internal i32 @check_i8() noinline {
entry:
  br label %loop

loop:
  %x = phi i32 [ 120, %entry ], [ %x1, %loop ]
  %h = phi i32 [ 0, %entry ], [ %h1, %loop ]
  %x8 = trunc i32 %x to i8
  %f = call i32 @i8_switch(i8 %x8)
  %h1 = call i32 @mix(i32 %h, i32 %f)
  %x1 = add i32 %x, 1
  %more = icmp sle i32 %x1, 135
  br i1 %more, label %loop, label %done

done:
  %bad = icmp ne i32 %h1, -271893423
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 4: ranges at INT_MAX (x = INT_MAX - 8, INT_MAX - 7, INT_MAX - 3,
; INT_MAX, INT_MIN, INT_MIN + 3, -1, 0) and at INT_MIN (x = INT_MIN,
; INT_MIN + 5, INT_MIN + 7, INT_MIN + 8, INT_MAX, INT_MAX - 1, -1, 0).
define internal i32 @check_edges() noinline {
  %a0 = call i32 @int_max_edge(i32 2147483639)
  %h0 = call i32 @mix(i32 0, i32 %a0)
  %a1 = call i32 @int_max_edge(i32 2147483640)
  %h1 = call i32 @mix(i32 %h0, i32 %a1)
  %a2 = call i32 @int_max_edge(i32 2147483644)
  %h2 = call i32 @mix(i32 %h1, i32 %a2)
  %a3 = call i32 @int_max_edge(i32 2147483647)
  %h3 = call i32 @mix(i32 %h2, i32 %a3)
  %a4 = call i32 @int_max_edge(i32 -2147483648)
  %h4 = call i32 @mix(i32 %h3, i32 %a4)
  %a5 = call i32 @int_max_edge(i32 -2147483645)
  %h5 = call i32 @mix(i32 %h4, i32 %a5)
  %a6 = call i32 @int_max_edge(i32 -1)
  %h6 = call i32 @mix(i32 %h5, i32 %a6)
  %a7 = call i32 @int_max_edge(i32 0)
  %h7 = call i32 @mix(i32 %h6, i32 %a7)
  %b0 = call i32 @int_min_edge(i32 -2147483648)
  %g0 = call i32 @mix(i32 0, i32 %b0)
  %b1 = call i32 @int_min_edge(i32 -2147483643)
  %g1 = call i32 @mix(i32 %g0, i32 %b1)
  %b2 = call i32 @int_min_edge(i32 -2147483641)
  %g2 = call i32 @mix(i32 %g1, i32 %b2)
  %b3 = call i32 @int_min_edge(i32 -2147483640)
  %g3 = call i32 @mix(i32 %g2, i32 %b3)
  %b4 = call i32 @int_min_edge(i32 2147483647)
  %g4 = call i32 @mix(i32 %g3, i32 %b4)
  %b5 = call i32 @int_min_edge(i32 2147483646)
  %g5 = call i32 @mix(i32 %g4, i32 %b5)
  %b6 = call i32 @int_min_edge(i32 -1)
  %g6 = call i32 @mix(i32 %g5, i32 %b6)
  %b7 = call i32 @int_min_edge(i32 0)
  %g7 = call i32 @mix(i32 %g6, i32 %b7)
  %c1 = icmp ne i32 %h7, 2014254502
  %c2 = icmp ne i32 %g7, 452354943
  %o = or i1 %c1, %c2
  %r = zext i1 %o to i32
  ret i32 %r
}

; Bit 5: fallthrough; x from -1 to 4 with v = 10 (10, 118, 90, 95, 9, 10).
define internal i32 @check_fallthrough() noinline {
entry:
  br label %loop

loop:
  %x = phi i32 [ -1, %entry ], [ %x1, %loop ]
  %h = phi i32 [ 0, %entry ], [ %h1, %loop ]
  %f = call i32 @fallthrough(i32 %x, i32 10)
  %h1 = call i32 @mix(i32 %h, i32 %f)
  %x1 = add i32 %x, 1
  %more = icmp sle i32 %x1, 4
  br i1 %more, label %loop, label %done

done:
  %bad = icmp ne i32 %h1, 398039762
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 6: unreachable default; x from 0 to 5 (1, 17, 49, 3000, 128, 55).
define internal i32 @check_unreachable_default() noinline {
entry:
  br label %loop

loop:
  %x = phi i32 [ 0, %entry ], [ %x1, %loop ]
  %h = phi i32 [ 0, %entry ], [ %h1, %loop ]
  %f = call i32 @unreachable_default(i32 %x)
  %h1 = call i32 @mix(i32 %h, i32 %f)
  %x1 = add i32 %x, 1
  %more = icmp sle i32 %x1, 5
  br i1 %more, label %loop, label %done

done:
  %bad = icmp ne i32 %h1, 48675790
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 7: the state machine: machine(37) = 536989012, machine(0) = 1,
; machine(1) = 536870914.
define internal i32 @check_machine() noinline {
  %a = call i32 @machine(i32 37)
  %a1 = icmp ne i32 %a, 536989012
  %b = call i32 @machine(i32 0)
  %b1 = icmp ne i32 %b, 1
  %c = call i32 @machine(i32 1)
  %c1 = icmp ne i32 %c, 536870914
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %r = zext i1 %o2 to i32
  ret i32 %r
}

define i32 @main() {
  %b0 = call i32 @check_dense()
  %b1 = call i32 @check_ranges()
  %b2 = call i32 @check_shared()
  %b3 = call i32 @check_i8()
  %b4 = call i32 @check_edges()
  %b5 = call i32 @check_fallthrough()
  %b6 = call i32 @check_unreachable_default()
  %b7 = call i32 @check_machine()
  %s1 = shl i32 %b1, 1
  %s2 = shl i32 %b2, 2
  %s3 = shl i32 %b3, 3
  %s4 = shl i32 %b4, 4
  %s5 = shl i32 %b5, 5
  %s6 = shl i32 %b6, 6
  %s7 = shl i32 %b7, 7
  %m1 = or i32 %b0, %s1
  %m2 = or i32 %m1, %s2
  %m3 = or i32 %m2, %s3
  %m4 = or i32 %m3, %s4
  %m5 = or i32 %m4, %s5
  %m6 = or i32 %m5, %s6
  %m7 = or i32 %m6, %s7
  ret i32 %m7
}
