; Nested loops (Milestone 6): triangular two-level nests, a search that
; leaves both loops at once, a three-level nest with a condition in the
; innermost loop, 'break' in an inner loop next to 'continue' in both, a
; float32 inner loop inside a native int outer loop, a four-level nest with
; 'continue' in the second level, a 'while' loop (Euclid's algorithm) inside
; a two-level 'for' nest, and a digit-sum 'while' inside two 'for' loops.
; Each noinline check function returns 1 when a result differs from the
; value C gives (in the comments); main ORs one bit per check into the exit
; status (8 bits, D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

; for (i = 0; i < n; i++) for (j = 0; j <= i; j++) s += (i * j) ^ j;
define internal i32 @tri_sum(i32 %n) noinline {
entry:
  br label %outer

outer:
  %i = phi i32 [ 0, %entry ], [ %i1, %outer.latch ]
  %s = phi i32 [ 0, %entry ], [ %s.inner, %outer.latch ]
  %more.i = icmp slt i32 %i, %n
  br i1 %more.i, label %inner, label %exit

inner:
  %j = phi i32 [ 0, %outer ], [ %j1, %inner ]
  %t = phi i32 [ %s, %outer ], [ %t1, %inner ]
  %ij = mul i32 %i, %j
  %x = xor i32 %ij, %j
  %t1 = add i32 %t, %x
  %j1 = add i32 %j, 1
  %more.j = icmp sle i32 %j1, %i
  br i1 %more.j, label %inner, label %outer.latch

outer.latch:
  %s.inner = phi i32 [ %t1, %inner ]
  %i1 = add i32 %i, 1
  br label %outer

exit:
  ret i32 %s
}

; for (i = 1; i <= lim; i++) for (j = i; j <= lim; j++)
;   if (i * j == t) return i * 1000 + j;
; return -1;
define internal i32 @find_pair(i32 %t, i32 %lim) noinline {
entry:
  br label %outer

outer:
  %i = phi i32 [ 1, %entry ], [ %i1, %outer.latch ]
  %more.i = icmp sle i32 %i, %lim
  br i1 %more.i, label %inner, label %notfound

inner:
  %j = phi i32 [ %i, %outer ], [ %j1, %inner.latch ]
  %more.j = icmp sle i32 %j, %lim
  br i1 %more.j, label %body, label %outer.latch

body:
  %p = mul i32 %i, %j
  %hit = icmp eq i32 %p, %t
  br i1 %hit, label %found, label %inner.latch

inner.latch:
  %j1 = add i32 %j, 1
  br label %inner

outer.latch:
  %i1 = add i32 %i, 1
  br label %outer

found:
  %i1000 = mul i32 %i, 1000
  %r = add i32 %i1000, %j
  ret i32 %r

notfound:
  ret i32 -1
}

; count (i, j, k) with 0 <= i <= j <= k < n and i + j + k == n.
define internal i32 @count_triples(i32 %n) noinline {
entry:
  br label %li

li:
  %i = phi i32 [ 0, %entry ], [ %i1, %li.latch ]
  %c = phi i32 [ 0, %entry ], [ %c.j, %li.latch ]
  %more.i = icmp slt i32 %i, %n
  br i1 %more.i, label %lj, label %exit

lj:
  %j = phi i32 [ %i, %li ], [ %j1, %lj.latch ]
  %cj = phi i32 [ %c, %li ], [ %c.k, %lj.latch ]
  %more.j = icmp slt i32 %j, %n
  br i1 %more.j, label %lk, label %li.latch

lk:
  %k = phi i32 [ %j, %lj ], [ %k1, %lk.latch ]
  %ck = phi i32 [ %cj, %lj ], [ %ck1, %lk.latch ]
  %more.k = icmp slt i32 %k, %n
  br i1 %more.k, label %lk.body, label %lj.latch

lk.body:
  %ij = add i32 %i, %j
  %ijk = add i32 %ij, %k
  %eq = icmp eq i32 %ijk, %n
  br i1 %eq, label %lk.count, label %lk.latch

lk.count:
  %cinc = add i32 %ck, 1
  br label %lk.latch

lk.latch:
  %ck1 = phi i32 [ %ck, %lk.body ], [ %cinc, %lk.count ]
  %k1 = add i32 %k, 1
  br label %lk

lj.latch:
  %c.k = phi i32 [ %ck, %lk ]
  %j1 = add i32 %j, 1
  br label %lj

li.latch:
  %c.j = phi i32 [ %cj, %lj ]
  %i1 = add i32 %i, 1
  br label %li

exit:
  ret i32 %c
}

; for (i = 0; i < n; i++) { if (i % 4 == 3) continue;
;   for (j = 0; j < n; j++) { if (j > i) break;
;     if ((i + j) % 3 == 0) continue; s += i * j; } }
define internal i32 @break_continue(i32 %n) noinline {
entry:
  br label %outer

outer:
  %i = phi i32 [ 0, %entry ], [ %i1, %outer.latch ]
  %s = phi i32 [ 0, %entry ], [ %s.out, %outer.latch ]
  %more.i = icmp slt i32 %i, %n
  br i1 %more.i, label %outer.body, label %exit

outer.body:
  %r4 = srem i32 %i, 4
  %skip = icmp eq i32 %r4, 3
  br i1 %skip, label %outer.latch, label %inner

inner:
  %j = phi i32 [ 0, %outer.body ], [ %j1, %inner.latch ]
  %t = phi i32 [ %s, %outer.body ], [ %t1, %inner.latch ]
  %more.j = icmp slt i32 %j, %n
  br i1 %more.j, label %inner.body, label %outer.latch

inner.body:
  %gt = icmp sgt i32 %j, %i
  br i1 %gt, label %outer.latch, label %inner.test

inner.test:
  %ij = add i32 %i, %j
  %r3 = srem i32 %ij, 3
  %c3 = icmp eq i32 %r3, 0
  br i1 %c3, label %inner.latch, label %inner.add

inner.add:
  %p = mul i32 %i, %j
  %ta = add i32 %t, %p
  br label %inner.latch

inner.latch:
  %t1 = phi i32 [ %t, %inner.test ], [ %ta, %inner.add ]
  %j1 = add i32 %j, 1
  br label %inner

outer.latch:
  %s.out = phi i32 [ %s, %outer.body ], [ %t, %inner ], [ %t, %inner.body ]
  %i1 = add i32 %i, 1
  br label %outer

exit:
  ret i32 %s
}

; long long t = 0; for (i = 0; i < n; i++) { float f = 0;
;   for (j = 0; j < i; j++) f += 0.5f; t += (long long)(f * 2) * i; }
define internal i64 @nest_mixed(i64 %n) noinline {
entry:
  br label %outer

outer:
  %i = phi i64 [ 0, %entry ], [ %i1, %outer.latch ]
  %t = phi i64 [ 0, %entry ], [ %t1, %outer.latch ]
  %more.i = icmp slt i64 %i, %n
  br i1 %more.i, label %inner, label %exit

inner:
  %j = phi i64 [ 0, %outer ], [ %j1, %inner.body ]
  %f = phi float [ 0.0, %outer ], [ %f1, %inner.body ]
  %more.j = icmp slt i64 %j, %i
  br i1 %more.j, label %inner.body, label %outer.latch

inner.body:
  %f1 = fadd float %f, 0.5
  %j1 = add i64 %j, 1
  br label %inner

outer.latch:
  %f2 = fmul float %f, 2.0
  %fi = fptosi float %f2 to i64
  %p = mul i64 %fi, %i
  %t1 = add i64 %t, %p
  %i1 = add i64 %i, 1
  br label %outer

exit:
  ret i64 %t
}

; count (a, b, c, d) in [0, n)^4 with a < b ('continue' in the second loop),
; c != d and a + b + c + d odd.
define internal i32 @deep4(i32 %n) noinline {
entry:
  br label %la

la:
  %a = phi i32 [ 0, %entry ], [ %a1, %la.latch ]
  %cnt = phi i32 [ 0, %entry ], [ %cnt.b, %la.latch ]
  %more.a = icmp slt i32 %a, %n
  br i1 %more.a, label %lb, label %exit

lb:
  %b = phi i32 [ 0, %la ], [ %b1, %lb.latch ]
  %cb = phi i32 [ %cnt, %la ], [ %cb1, %lb.latch ]
  %more.b = icmp slt i32 %b, %n
  br i1 %more.b, label %lb.body, label %la.latch

lb.body:
  %ab = icmp slt i32 %a, %b
  br i1 %ab, label %lc, label %lb.latch

lc:
  %c = phi i32 [ 0, %lb.body ], [ %c1, %lc.latch ]
  %cc = phi i32 [ %cb, %lb.body ], [ %cc1, %lc.latch ]
  %more.c = icmp slt i32 %c, %n
  br i1 %more.c, label %ld, label %lb.latch

ld:
  %d = phi i32 [ 0, %lc ], [ %d1, %ld.latch ]
  %cd = phi i32 [ %cc, %lc ], [ %cd1, %ld.latch ]
  %more.d = icmp slt i32 %d, %n
  br i1 %more.d, label %ld.body, label %lc.latch

ld.body:
  %ne = icmp ne i32 %c, %d
  br i1 %ne, label %ld.parity, label %ld.latch

ld.parity:
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %s3 = add i32 %s2, %d
  %odd = and i32 %s3, 1
  %isodd = icmp ne i32 %odd, 0
  br i1 %isodd, label %ld.count, label %ld.latch

ld.count:
  %cdi = add i32 %cd, 1
  br label %ld.latch

ld.latch:
  %cd1 = phi i32 [ %cd, %ld.body ], [ %cd, %ld.parity ], [ %cdi, %ld.count ]
  %d1 = add i32 %d, 1
  br label %ld

lc.latch:
  %cc1 = phi i32 [ %cd, %ld ]
  %c1 = add i32 %c, 1
  br label %lc

lb.latch:
  %cb1 = phi i32 [ %cb, %lb.body ], [ %cc, %lc ]
  %b1 = add i32 %b, 1
  br label %lb

la.latch:
  %cnt.b = phi i32 [ %cb, %lb ]
  %a1 = add i32 %a, 1
  br label %la

exit:
  ret i32 %cnt
}

; for (i = 1; i <= n; i++) for (j = 1; j <= n; j++) {
;   unsigned a = i, b = j;
;   while (b) { unsigned t = a % b; a = b; b = t; }
;   s += a; }
define internal i32 @gcd_sum(i32 %n) noinline {
entry:
  br label %li

li:
  %i = phi i32 [ 1, %entry ], [ %i1, %li.latch ]
  %s = phi i32 [ 0, %entry ], [ %s.j, %li.latch ]
  %more.i = icmp sle i32 %i, %n
  br i1 %more.i, label %lj, label %exit

lj:
  %j = phi i32 [ 1, %li ], [ %j1, %gcd.done ]
  %sj = phi i32 [ %s, %li ], [ %sj1, %gcd.done ]
  %more.j = icmp sle i32 %j, %n
  br i1 %more.j, label %gcd, label %li.latch

gcd:
  %ga = phi i32 [ %i, %lj ], [ %gb, %gcd.body ]
  %gb = phi i32 [ %j, %lj ], [ %gt, %gcd.body ]
  %nz = icmp ne i32 %gb, 0
  br i1 %nz, label %gcd.body, label %gcd.done

gcd.body:
  %gt = urem i32 %ga, %gb
  br label %gcd

gcd.done:
  %sj1 = add i32 %sj, %ga
  %j1 = add i32 %j, 1
  br label %lj

li.latch:
  %s.j = phi i32 [ %sj, %lj ]
  %i1 = add i32 %i, 1
  br label %li

exit:
  ret i32 %s
}

; for (i = 1; i < n; i++) for (j = 1; j < n; j++)
;   for (v = i * j; v; v /= 10) s += v % 10;
define internal i32 @digit_sums(i32 %n) noinline {
entry:
  br label %li

li:
  %i = phi i32 [ 1, %entry ], [ %i1, %li.latch ]
  %s = phi i32 [ 0, %entry ], [ %s.j, %li.latch ]
  %more.i = icmp slt i32 %i, %n
  br i1 %more.i, label %lj, label %exit

lj:
  %j = phi i32 [ 1, %li ], [ %j1, %digits.done ]
  %sj = phi i32 [ %s, %li ], [ %sv, %digits.done ]
  %more.j = icmp slt i32 %j, %n
  br i1 %more.j, label %lj.body, label %li.latch

lj.body:
  %p = mul i32 %i, %j
  br label %digits

digits:
  %v = phi i32 [ %p, %lj.body ], [ %v1, %digits.body ]
  %sv = phi i32 [ %sj, %lj.body ], [ %sv1, %digits.body ]
  %nz = icmp ne i32 %v, 0
  br i1 %nz, label %digits.body, label %digits.done

digits.body:
  %d = urem i32 %v, 10
  %sv1 = add i32 %sv, %d
  %v1 = udiv i32 %v, 10
  br label %digits

digits.done:
  %j1 = add i32 %j, 1
  br label %lj

li.latch:
  %s.j = phi i32 [ %sj, %lj ]
  %i1 = add i32 %i, 1
  br label %li

exit:
  ret i32 %s
}

; Bit 0: tri_sum(30) = 98885, tri_sum(0) = 0.
define internal i32 @check_tri() noinline {
  %a = call i32 @tri_sum(i32 30)
  %a1 = icmp ne i32 %a, 98885
  %b = call i32 @tri_sum(i32 0)
  %b1 = icmp ne i32 %b, 0
  %o = or i1 %a1, %b1
  %r = zext i1 %o to i32
  ret i32 %r
}

; Bit 1: find_pair(391, 50) = 17023, find_pair(997, 50) = -1,
; find_pair(2500, 50) = 50050 (the last iteration of both loops).
define internal i32 @check_find() noinline {
  %a = call i32 @find_pair(i32 391, i32 50)
  %a1 = icmp ne i32 %a, 17023
  %b = call i32 @find_pair(i32 997, i32 50)
  %b1 = icmp ne i32 %b, -1
  %c = call i32 @find_pair(i32 2500, i32 50)
  %c1 = icmp ne i32 %c, 50050
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %r = zext i1 %o2 to i32
  ret i32 %r
}

; Bit 2: count_triples(12) = 18, count_triples(0) = 0.
define internal i32 @check_triples() noinline {
  %a = call i32 @count_triples(i32 12)
  %a1 = icmp ne i32 %a, 18
  %b = call i32 @count_triples(i32 0)
  %b1 = icmp ne i32 %b, 0
  %o = or i1 %a1, %b1
  %r = zext i1 %o to i32
  ret i32 %r
}

; Bit 3: break_continue(15) = 3395.
define internal i32 @check_break_continue() noinline {
  %a = call i32 @break_continue(i32 15)
  %a1 = icmp ne i32 %a, 3395
  %r = zext i1 %a1 to i32
  ret i32 %r
}

; Bit 4: nest_mixed(20) = 2470 (the sum of i * i for i < 20).
define internal i32 @check_mixed() noinline {
  %a = call i64 @nest_mixed(i64 20)
  %a1 = icmp ne i64 %a, 2470
  %r = zext i1 %a1 to i32
  ret i32 %r
}

; Bit 5: deep4(5) = 96.
define internal i32 @check_deep4() noinline {
  %a = call i32 @deep4(i32 5)
  %a1 = icmp ne i32 %a, 96
  %r = zext i1 %a1 to i32
  ret i32 %r
}

; Bit 6: gcd_sum(12) = 288.
define internal i32 @check_gcd() noinline {
  %a = call i32 @gcd_sum(i32 12)
  %a1 = icmp ne i32 %a, 288
  %r = zext i1 %a1 to i32
  ret i32 %r
}

; Bit 7: digit_sums(25) = 5220.
define internal i32 @check_digits() noinline {
  %a = call i32 @digit_sums(i32 25)
  %a1 = icmp ne i32 %a, 5220
  %r = zext i1 %a1 to i32
  ret i32 %r
}

define i32 @main() {
  %b0 = call i32 @check_tri()
  %b1 = call i32 @check_find()
  %b2 = call i32 @check_triples()
  %b3 = call i32 @check_break_continue()
  %b4 = call i32 @check_mixed()
  %b5 = call i32 @check_deep4()
  %b6 = call i32 @check_gcd()
  %b7 = call i32 @check_digits()
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
