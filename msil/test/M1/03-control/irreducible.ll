; Irreducible control flow (Milestone 6): loops with more than one entry
; block, which C produces with 'goto' into a loop and which have no
; structured form. The CIL has no structure requirement, so they must compile
; as they are (no block may be left with values on the evaluation stack,
; passes-frame e2): a Collatz state machine whose two loop headers handle odd
; and even values, a 'goto' into the middle of a loop whose loop-carried
; value starts as an argument, a three-block cycle entered at any of its
; blocks, and a two-headed loop over a float64 and a native int. Each
; noinline check function returns 1 when a result differs from the value C
; gives (in the comments); main ORs one bit per check into the exit status
; (8 bits, D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

; collatz_sm(start, cap): the Collatz sequence of start as a state machine:
; block %odd handles odd x (x = 3x + 1), block %even handles even x (x /= 2),
; and the entry jumps straight to either. Returns the number of steps until
; x == 1 (reached in %even), or 100000 + steps when cap steps ran first.
define internal i32 @collatz_sm(i32 %start, i32 %cap) noinline {
entry:
  %lo = and i32 %start, 1
  %isodd = icmp ne i32 %lo, 0
  br i1 %isodd, label %odd, label %even

odd:
  %xo = phi i32 [ %start, %entry ], [ %xo1, %odd.next ], [ %xe1, %even.next ]
  %ko = phi i32 [ 0, %entry ], [ %ko1, %odd.next ], [ %ke1, %even.next ]
  %x3 = mul i32 %xo, 3
  %xo1 = add i32 %x3, 1
  %ko1 = add i32 %ko, 1
  %capo = icmp sge i32 %ko1, %cap
  br i1 %capo, label %capped, label %odd.next

odd.next:
  %lo1 = and i32 %xo1, 1
  %odd1 = icmp ne i32 %lo1, 0
  br i1 %odd1, label %odd, label %even

even:
  %xe = phi i32 [ %start, %entry ], [ %xo1, %odd.next ], [ %xe1, %even.next ]
  %ke = phi i32 [ 0, %entry ], [ %ko1, %odd.next ], [ %ke1, %even.next ]
  %xe1 = lshr i32 %xe, 1
  %ke1 = add i32 %ke, 1
  %one = icmp eq i32 %xe1, 1
  br i1 %one, label %done, label %even.cap

even.cap:
  %cape = icmp sge i32 %ke1, %cap
  br i1 %cape, label %capped, label %even.next

even.next:
  %lo2 = and i32 %xe1, 1
  %odd2 = icmp ne i32 %lo2, 0
  br i1 %odd2, label %odd, label %even

capped:
  %kc = phi i32 [ %ko1, %odd ], [ %ke1, %even.cap ]
  %rc = add i32 %kc, 100000
  ret i32 %rc

done:
  ret i32 %ke1
}

; goto_middle(n, flag): x = n; k = 0; if (flag) goto middle;
;   top: x = x * 5 + 3;  middle: x ^= k; if (++k < n) goto top;  return x;
define internal i32 @goto_middle(i32 %n, i32 %flag) noinline {
entry:
  %f = icmp ne i32 %flag, 0
  br i1 %f, label %middle, label %top

top:
  %xt = phi i32 [ %n, %entry ], [ %xm1, %middle ]
  %kt = phi i32 [ 0, %entry ], [ %km1, %middle ]
  %x5 = mul i32 %xt, 5
  %x53 = add i32 %x5, 3
  br label %middle

middle:
  %xm = phi i32 [ %n, %entry ], [ %x53, %top ]
  %km = phi i32 [ 0, %entry ], [ %kt, %top ]
  %xm1 = xor i32 %xm, %km
  %km1 = add i32 %km, 1
  %more = icmp slt i32 %km1, %n
  br i1 %more, label %top, label %exit

exit:
  ret i32 %xm1
}

; three_cycle(n, sel): acc = 7; enter block A, B or C by sel (0, 1, other);
;   A: acc = acc * 3 + 1, then B;
;   B: acc ^= acc >> 3 (arithmetic), then C if acc is odd, else A;
;   C: acc += 12345, then A;
; every block counts a step and returns acc after n steps.
define internal i32 @three_cycle(i32 %n, i32 %sel) noinline {
entry:
  %s0 = icmp eq i32 %sel, 0
  br i1 %s0, label %A, label %entry.1

entry.1:
  %s1 = icmp eq i32 %sel, 1
  br i1 %s1, label %B, label %C

A:
  %accA = phi i32 [ 7, %entry ], [ %accB1, %B.next ], [ %accC1, %C.next ]
  %kA = phi i32 [ 0, %entry ], [ %kB1, %B.next ], [ %kC1, %C.next ]
  %m3 = mul i32 %accA, 3
  %accA1 = add i32 %m3, 1
  %kA1 = add i32 %kA, 1
  %endA = icmp sge i32 %kA1, %n
  br i1 %endA, label %exit, label %B

B:
  %accB = phi i32 [ 7, %entry.1 ], [ %accA1, %A ]
  %kB = phi i32 [ 0, %entry.1 ], [ %kA1, %A ]
  %sh = ashr i32 %accB, 3
  %accB1 = xor i32 %accB, %sh
  %kB1 = add i32 %kB, 1
  %endB = icmp sge i32 %kB1, %n
  br i1 %endB, label %exit, label %B.next

B.next:
  %lo = and i32 %accB1, 1
  %isodd = icmp ne i32 %lo, 0
  br i1 %isodd, label %C, label %A

C:
  %accC = phi i32 [ 7, %entry.1 ], [ %accB1, %B.next ]
  %kC = phi i32 [ 0, %entry.1 ], [ %kB1, %B.next ]
  %accC1 = add i32 %accC, 12345
  %kC1 = add i32 %kC, 1
  %endC = icmp sge i32 %kC1, %n
  br i1 %endC, label %exit, label %C.next

C.next:
  br label %A

exit:
  %r = phi i32 [ %accA1, %A ], [ %accB1, %B ], [ %accC1, %C ]
  ret i32 %r
}

; fp_irr(x0, n, flag, *): d = x0, v = 1, k = 0; enter P (flag == 0) or Q;
;   P: d *= 1.5; v = v * 3 + k; then Q;
;   Q: d -= 0.25; v ^= v << 7; then P if d < 100, else Q;
; each block counts a step; after n steps returns (int)(d * 1024) when
; 'want_d' is set, else the low 32 bits of v ^ (v >> 32).
define internal i32 @fp_irr(double %x0, i32 %n, i32 %flag, i1 %want_d) noinline {
entry:
  %f = icmp ne i32 %flag, 0
  br i1 %f, label %Q, label %P

P:
  %dP = phi double [ %x0, %entry ], [ %dQ1, %Q.next ]
  %vP = phi i64 [ 1, %entry ], [ %vQ1, %Q.next ]
  %kP = phi i32 [ 0, %entry ], [ %kQ1, %Q.next ]
  %dP1 = fmul double %dP, 1.5
  %v3 = mul i64 %vP, 3
  %kw = sext i32 %kP to i64
  %vP1 = add i64 %v3, %kw
  %kP1 = add i32 %kP, 1
  %endP = icmp sge i32 %kP1, %n
  br i1 %endP, label %exit, label %Q

Q:
  %dQ = phi double [ %x0, %entry ], [ %dP1, %P ], [ %dQ1, %Q.next ]
  %vQ = phi i64 [ 1, %entry ], [ %vP1, %P ], [ %vQ1, %Q.next ]
  %kQ = phi i32 [ 0, %entry ], [ %kP1, %P ], [ %kQ1, %Q.next ]
  %dQ1 = fsub double %dQ, 0.25
  %vs = shl i64 %vQ, 7
  %vQ1 = xor i64 %vQ, %vs
  %kQ1 = add i32 %kQ, 1
  %endQ = icmp sge i32 %kQ1, %n
  br i1 %endQ, label %exit, label %Q.next

Q.next:
  %small = fcmp olt double %dQ1, 100.0
  br i1 %small, label %P, label %Q

exit:
  %d = phi double [ %dP1, %P ], [ %dQ1, %Q ]
  %v = phi i64 [ %vP1, %P ], [ %vQ1, %Q ]
  br i1 %want_d, label %ret.d, label %ret.v

ret.d:
  %d1024 = fmul double %d, 1024.0
  %di = fptosi double %d1024 to i32
  ret i32 %di

ret.v:
  %hi = lshr i64 %v, 32
  %fold = xor i64 %v, %hi
  %vi = trunc i64 %fold to i32
  ret i32 %vi
}

; Bit 0: collatz_sm(27, 1000) = 111, collatz_sm(96, 1000) = 12,
; collatz_sm(27, 50) = 100050 (capped), collatz_sm(1, 1000) = 3,
; collatz_sm(7, 1000) = 16.
define internal i32 @check_collatz() noinline {
  %a = call i32 @collatz_sm(i32 27, i32 1000)
  %a1 = icmp ne i32 %a, 111
  %b = call i32 @collatz_sm(i32 96, i32 1000)
  %b1 = icmp ne i32 %b, 12
  %c = call i32 @collatz_sm(i32 27, i32 50)
  %c1 = icmp ne i32 %c, 100050
  %d = call i32 @collatz_sm(i32 1, i32 1000)
  %d1 = icmp ne i32 %d, 3
  %e = call i32 @collatz_sm(i32 7, i32 1000)
  %e1 = icmp ne i32 %e, 16
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %o3 = or i1 %o2, %d1
  %o4 = or i1 %o3, %e1
  %r = zext i1 %o4 to i32
  ret i32 %r
}

; Bit 1: goto_middle(10, 0) = 105533221, goto_middle(10, 1) = 20480644,
; goto_middle(1, 0) = 8, goto_middle(1, 1) = 1, goto_middle(17, 1) = 200992801.
define internal i32 @check_goto_middle() noinline {
  %a = call i32 @goto_middle(i32 10, i32 0)
  %a1 = icmp ne i32 %a, 105533221
  %b = call i32 @goto_middle(i32 10, i32 1)
  %b1 = icmp ne i32 %b, 20480644
  %c = call i32 @goto_middle(i32 1, i32 0)
  %c1 = icmp ne i32 %c, 8
  %d = call i32 @goto_middle(i32 1, i32 1)
  %d1 = icmp ne i32 %d, 1
  %e = call i32 @goto_middle(i32 17, i32 1)
  %e1 = icmp ne i32 %e, 200992801
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %o3 = or i1 %o2, %d1
  %o4 = or i1 %o3, %e1
  %r = zext i1 %o4 to i32
  ret i32 %r
}

; Bit 2: three_cycle(20, 0) = 452894, three_cycle(20, 1) = 41397650,
; three_cycle(20, 2) = 124192951, three_cycle(1, 2) = 12352,
; three_cycle(33, 1) = -1966284129.
define internal i32 @check_three_cycle() noinline {
  %a = call i32 @three_cycle(i32 20, i32 0)
  %a1 = icmp ne i32 %a, 452894
  %b = call i32 @three_cycle(i32 20, i32 1)
  %b1 = icmp ne i32 %b, 41397650
  %c = call i32 @three_cycle(i32 20, i32 2)
  %c1 = icmp ne i32 %c, 124192951
  %d = call i32 @three_cycle(i32 1, i32 2)
  %d1 = icmp ne i32 %d, 12352
  %e = call i32 @three_cycle(i32 33, i32 1)
  %e1 = icmp ne i32 %e, -1966284129
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %o3 = or i1 %o2, %d1
  %o4 = or i1 %o3, %e1
  %r = zext i1 %o4 to i32
  ret i32 %r
}

; Bit 3: fp_irr: (x0, n, flag) = (1.0, 12, 0) gives d = 6.1953125 and
; v = 3227916344497471; (1.0, 12, 1) gives d = 3.59765625 and
; v = 3253290799678763; (2.0, 1, 1) gives d = 1.75 and v = 129.
; (int)(d * 1024): 6344, 3684, 1792; (int)(v ^ (v >> 32)): -685808902,
; -897790990, 129.
define internal i32 @check_fp_irr() noinline {
  %a = call i32 @fp_irr(double 1.0, i32 12, i32 0, i1 true)
  %a1 = icmp ne i32 %a, 6344
  %b = call i32 @fp_irr(double 1.0, i32 12, i32 0, i1 false)
  %b1 = icmp ne i32 %b, -685808902
  %c = call i32 @fp_irr(double 1.0, i32 12, i32 1, i1 true)
  %c1 = icmp ne i32 %c, 3684
  %d = call i32 @fp_irr(double 1.0, i32 12, i32 1, i1 false)
  %d1 = icmp ne i32 %d, -897790990
  %e = call i32 @fp_irr(double 2.0, i32 1, i32 1, i1 true)
  %e1 = icmp ne i32 %e, 1792
  %f = call i32 @fp_irr(double 2.0, i32 1, i32 1, i1 false)
  %f1 = icmp ne i32 %f, 129
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %o3 = or i1 %o2, %d1
  %o4 = or i1 %o3, %e1
  %o5 = or i1 %o4, %f1
  %r = zext i1 %o5 to i32
  ret i32 %r
}

define i32 @main() {
  %b0 = call i32 @check_collatz()
  %b1 = call i32 @check_goto_middle()
  %b2 = call i32 @check_three_cycle()
  %b3 = call i32 @check_fp_irr()
  %s1 = shl i32 %b1, 1
  %s2 = shl i32 %b2, 2
  %s3 = shl i32 %b3, 3
  %m1 = or i32 %b0, %s1
  %m2 = or i32 %m1, %s2
  %m3 = or i32 %m2, %s3
  ret i32 %m3
}
