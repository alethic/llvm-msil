; Loops (Milestone 6): counted loops up and down, while loops whose test is
; at the top, do-while loops that run once, zero-trip loops, loop-carried
; values that swap (the PHI swap problem) or rotate (iterative Fibonacci),
; int32, native int, float32 and float64 accumulators and induction
; variables, i8 and i16 counters that wrap, unsigned bounds above 2^31,
; native int bounds that cross 2^32, a negative stride, loops with several
; exits and with 'continue', and calls inside a loop. Each noinline check
; function returns 1 when a result differs from the value in its comment
; (computed with C semantics); main ORs one bit per check into the exit
; status (8 bits, D37): [exit 0] means every check passed. (Plan M6.d calls
; this test loops.ll; the expected transcript is found by file stem, and
; loops.c already owns loops.expected.)
; RUN: %{msil_ll}

; s = s * 3 + i for i in [0, n) with int32 wraparound; a zero-trip guard.
define internal i32 @sum_up(i32 %n) noinline {
entry:
  %any = icmp sgt i32 %n, 0
  br i1 %any, label %loop, label %done

loop:
  %i = phi i32 [ 0, %entry ], [ %i1, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s1, %loop ]
  %m = mul i32 %s, 3
  %s1 = add i32 %m, %i
  %i1 = add i32 %i, 1
  %more = icmp slt i32 %i1, %n
  br i1 %more, label %loop, label %done

done:
  %r = phi i32 [ 0, %entry ], [ %s1, %loop ]
  ret i32 %r
}

; while (i * i <= n) i++; return i;  (the test is at the top of the loop; the
; latch is the last block, so the method ends with a backward 'br')
define internal i32 @first_square_above(i32 %n) noinline {
entry:
  br label %head

head:
  %i = phi i32 [ 0, %entry ], [ %i1, %body ]
  %sq = mul i32 %i, %i
  %le = icmp sle i32 %sq, %n
  br i1 %le, label %body, label %exit

exit:
  ret i32 %i

body:
  %i1 = add i32 %i, 1
  br label %head
}

; do { k++; n -= 5; } while (n > 0); return k;  (runs at least once; the loop
; is the last block and ends in a conditional branch whose other successor
; comes before it, so the method must end with an explicit 'br' and never
; fall off its end, passes-frame e3)
define internal i32 @do_once(i32 %n) noinline {
entry:
  br label %loop

exit:
  ret i32 %k1

loop:
  %k = phi i32 [ 0, %entry ], [ %k1, %loop ]
  %v = phi i32 [ %n, %entry ], [ %v1, %loop ]
  %k1 = add i32 %k, 1
  %v1 = sub i32 %v, 5
  %more = icmp sgt i32 %v1, 0
  br i1 %more, label %loop, label %exit
}

; p = 7; for (i = n; i != 0; i--) p = (p * 1000003) ^ i;  (native int)
define internal i64 @count_down(i64 %n) noinline {
entry:
  %zero = icmp eq i64 %n, 0
  br i1 %zero, label %exit, label %loop

loop:
  %i = phi i64 [ %n, %entry ], [ %i1, %loop ]
  %p = phi i64 [ 7, %entry ], [ %p2, %loop ]
  %p1 = mul i64 %p, 1000003
  %p2 = xor i64 %p1, %i
  %i1 = sub i64 %i, 1
  %more = icmp ne i64 %i1, 0
  br i1 %more, label %loop, label %exit

exit:
  %r = phi i64 [ 7, %entry ], [ %p2, %loop ]
  ret i64 %r
}

; for (i = start; i < end; i++) { k++; s += i & 255; } return k * 1000 + s;
; with signed native int bounds on both sides of 2^32.
define internal i64 @loop_i64(i64 %start, i64 %end) noinline {
entry:
  %any = icmp slt i64 %start, %end
  br i1 %any, label %loop, label %exit

loop:
  %i = phi i64 [ %start, %entry ], [ %i1, %loop ]
  %k = phi i64 [ 0, %entry ], [ %k1, %loop ]
  %s = phi i64 [ 0, %entry ], [ %s1, %loop ]
  %k1 = add i64 %k, 1
  %lo = and i64 %i, 255
  %s1 = add i64 %s, %lo
  %i1 = add i64 %i, 1
  %more = icmp slt i64 %i1, %end
  br i1 %more, label %loop, label %exit

exit:
  %kf = phi i64 [ 0, %entry ], [ %k1, %loop ]
  %sf = phi i64 [ 0, %entry ], [ %s1, %loop ]
  %km = mul i64 %kf, 1000
  %r = add i64 %km, %sf
  ret i64 %r
}

; a, b = 0, 1; n times: a, b = b, a + b; return a;  (the values rotate)
define internal i64 @fib64(i32 %n) noinline {
entry:
  %any = icmp sgt i32 %n, 0
  br i1 %any, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i1, %loop ]
  %a = phi i64 [ 0, %entry ], [ %b, %loop ]
  %b = phi i64 [ 1, %entry ], [ %ab, %loop ]
  %ab = add i64 %a, %b
  %i1 = add i32 %i, 1
  %more = icmp slt i32 %i1, %n
  br i1 %more, label %loop, label %exit

exit:
  %r = phi i64 [ 0, %entry ], [ %b, %loop ]
  ret i64 %r
}

; x, y = 1, 2; n times: x, y = y, x; return x * 10 + y;  (the PHI swap problem)
define internal i32 @swap_parity(i32 %n) noinline {
entry:
  br label %head

head:
  %i = phi i32 [ 0, %entry ], [ %i1, %body ]
  %x = phi i32 [ 1, %entry ], [ %y, %body ]
  %y = phi i32 [ 2, %entry ], [ %x, %body ]
  %more = icmp slt i32 %i, %n
  br i1 %more, label %body, label %exit

body:
  %i1 = add i32 %i, 1
  br label %head

exit:
  %x10 = mul i32 %x, 10
  %r = add i32 %x10, %y
  ret i32 %r
}

; unsigned char c = start; do { c++; k++; } while (c != 4); return k;
define internal i32 @wrap_i8(i8 %start) noinline {
entry:
  br label %loop

loop:
  %c = phi i8 [ %start, %entry ], [ %c1, %loop ]
  %k = phi i32 [ 0, %entry ], [ %k1, %loop ]
  %c1 = add i8 %c, 1
  %k1 = add i32 %k, 1
  %again = icmp ne i8 %c1, 4
  br i1 %again, label %loop, label %exit

exit:
  ret i32 %k1
}

; unsigned short c = start; do { c += 7; k++; } while (c >= 100);
; return (k << 16) | c;
define internal i32 @wrap_i16(i16 %start) noinline {
entry:
  br label %loop

loop:
  %c = phi i16 [ %start, %entry ], [ %c1, %loop ]
  %k = phi i32 [ 0, %entry ], [ %k1, %loop ]
  %c1 = add i16 %c, 7
  %k1 = add i32 %k, 1
  %again = icmp uge i16 %c1, 100
  br i1 %again, label %loop, label %exit

exit:
  %kh = shl i32 %k1, 16
  %cz = zext i16 %c1 to i32
  %r = or i32 %kh, %cz
  ret i32 %r
}

; for (unsigned i = lo; i < hi; i += step) k++;  (bounds above 2^31: a signed
; compare would stop after 7 iterations instead of 14)
define internal i32 @unsigned_bound(i32 %lo, i32 %hi, i32 %step) noinline {
entry:
  br label %head

head:
  %i = phi i32 [ %lo, %entry ], [ %i1, %body ]
  %k = phi i32 [ 0, %entry ], [ %k1, %body ]
  %lt = icmp ult i32 %i, %hi
  br i1 %lt, label %body, label %exit

body:
  %k1 = add i32 %k, 1
  %i1 = add i32 %i, %step
  br label %head

exit:
  ret i32 %k
}

; for (i = hi; i >= lo; i -= step) k++;  (negative stride, signed bound)
define internal i32 @neg_step(i32 %hi, i32 %lo, i32 %step) noinline {
entry:
  br label %head

head:
  %i = phi i32 [ %hi, %entry ], [ %i1, %body ]
  %k = phi i32 [ 0, %entry ], [ %k1, %body ]
  %ge = icmp sge i32 %i, %lo
  br i1 %ge, label %body, label %exit

body:
  %k1 = add i32 %k, 1
  %i1 = sub i32 %i, %step
  br label %head

exit:
  ret i32 %k
}

; for (i = 1;; i++) {
;   if (i % 13 == 0) return 1000 + i;
;   if (i * i > n) return i;
; }
define internal i32 @multi_exit(i32 %n) noinline {
entry:
  br label %loop

loop:
  %i = phi i32 [ 1, %entry ], [ %i1, %next ]
  %rem = srem i32 %i, 13
  %by13 = icmp eq i32 %rem, 0
  br i1 %by13, label %exit13, label %test2

test2:
  %sq = mul i32 %i, %i
  %big = icmp sgt i32 %sq, %n
  br i1 %big, label %exitsq, label %next

next:
  %i1 = add i32 %i, 1
  br label %loop

exit13:
  %r13 = add i32 %i, 1000
  ret i32 %r13

exitsq:
  ret i32 %i
}

; for (i = 0; i < n; i++) { if (i & 1) continue;
;   if (i % 3 == 0) { s ^= i * 5; continue; } s += i * i; }
define internal i32 @continue_odd(i32 %n) noinline {
entry:
  br label %head

head:
  %i = phi i32 [ 0, %entry ], [ %i1, %latch ]
  %s = phi i32 [ 0, %entry ], [ %s3, %latch ]
  %more = icmp slt i32 %i, %n
  br i1 %more, label %body, label %exit

body:
  %odd = and i32 %i, 1
  %isodd = icmp ne i32 %odd, 0
  br i1 %isodd, label %latch, label %even

even:
  %r3 = urem i32 %i, 3
  %by3 = icmp eq i32 %r3, 0
  br i1 %by3, label %xorit, label %addit

xorit:
  %i5 = mul i32 %i, 5
  %sx = xor i32 %s, %i5
  br label %latch

addit:
  %ii = mul i32 %i, %i
  %sa = add i32 %s, %ii
  br label %latch

latch:
  %s3 = phi i32 [ %s, %body ], [ %sx, %xorit ], [ %sa, %addit ]
  %i1 = add i32 %i, 1
  br label %head

exit:
  ret i32 %s
}

define internal i32 @step(i32 %s, i32 %i) noinline {
  %m = mul i32 %s, 5
  %a = add i32 %m, %i
  %r = xor i32 %a, 3
  ret i32 %r
}

; s = 1; for (i = 0; i < n; i++) s = step(s, i);  (a call result crosses the
; back edge)
define internal i32 @call_in_loop(i32 %n) noinline {
entry:
  br label %head

head:
  %i = phi i32 [ 0, %entry ], [ %i1, %body ]
  %s = phi i32 [ 1, %entry ], [ %s1, %body ]
  %more = icmp slt i32 %i, %n
  br i1 %more, label %body, label %exit

body:
  %s1 = call i32 @step(i32 %s, i32 %i)
  %i1 = add i32 %i, 1
  br label %head

exit:
  ret i32 %s
}

; double s = 0; for (i = 0; i < n; i++) s += i * 0.5;  (exact: 22.5 for n = 10)
define internal double @fsum(i32 %n) noinline {
entry:
  br label %head

head:
  %i = phi i32 [ 0, %entry ], [ %i1, %body ]
  %s = phi double [ 0.0, %entry ], [ %s1, %body ]
  %more = icmp slt i32 %i, %n
  br i1 %more, label %body, label %exit

body:
  %d = sitofp i32 %i to double
  %h = fmul double %d, 0.5
  %s1 = fadd double %s, %h
  %i1 = add i32 %i, 1
  br label %head

exit:
  ret double %s
}

; float s = 0; for (i = 0; i < n; i++) s += 0.25f * i;
; (exact: 11.25 for n = 10)
define internal float @fsum32(i32 %n) noinline {
entry:
  br label %head

head:
  %i = phi i32 [ 0, %entry ], [ %i1, %body ]
  %s = phi float [ 0.0, %entry ], [ %s1, %body ]
  %more = icmp slt i32 %i, %n
  br i1 %more, label %body, label %exit

body:
  %d = sitofp i32 %i to float
  %h = fmul float %d, 0.25
  %s1 = fadd float %s, %h
  %i1 = add i32 %i, 1
  br label %head

exit:
  ret float %s
}

; x = start; k = 0; while (x < limit) { x = x * 2; k++; }
; return k * 1000 + (x == x ? (int)x : 999);
; (a float64 induction variable and a float64 loop condition)
define internal i32 @fdouble(double %start, double %limit) noinline {
entry:
  br label %head

head:
  %x = phi double [ %start, %entry ], [ %x1, %body ]
  %k = phi i32 [ 0, %entry ], [ %k1, %body ]
  %lt = fcmp olt double %x, %limit
  br i1 %lt, label %body, label %exit

body:
  %x1 = fmul double %x, 2.0
  %k1 = add i32 %k, 1
  br label %head

exit:
  %km = mul i32 %k, 1000
  %ord = fcmp ord double %x, 0.0
  br i1 %ord, label %num, label %nan

num:
  %xi = fptosi double %x to i32
  %r = add i32 %km, %xi
  ret i32 %r

nan:
  %rn = add i32 %km, 999
  ret i32 %rn
}

; Bit 0: int32 loops: counted, zero-trip, test at the top, do-while.
define internal i32 @check_int32() noinline {
  %a = call i32 @sum_up(i32 25)               ; 1368754844 (wraps)
  %a1 = icmp ne i32 %a, 1368754844
  %b = call i32 @sum_up(i32 0)                ; 0 (zero trips)
  %b1 = icmp ne i32 %b, 0
  %c = call i32 @first_square_above(i32 1000) ; 32
  %c1 = icmp ne i32 %c, 32
  %d = call i32 @first_square_above(i32 0)    ; 1
  %d1 = icmp ne i32 %d, 1
  %e = call i32 @do_once(i32 0)               ; 1
  %e1 = icmp ne i32 %e, 1
  %f = call i32 @do_once(i32 12)              ; 3
  %f1 = icmp ne i32 %f, 3
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %o3 = or i1 %o2, %d1
  %o4 = or i1 %o3, %e1
  %o5 = or i1 %o4, %f1
  %r = zext i1 %o5 to i32
  ret i32 %r
}

; Bit 1: native int loops.
define internal i32 @check_native() noinline {
  %a = call i64 @count_down(i64 40)           ; -6614902455173642793
  %a1 = icmp ne i64 %a, -6614902455173642793
  %b = call i64 @count_down(i64 0)            ; 7
  %b1 = icmp ne i64 %b, 7
  %c = call i64 @loop_i64(i64 4294967293, i64 4294967300) ; 7 trips: 7768
  %c1 = icmp ne i64 %c, 7768
  %d = call i64 @loop_i64(i64 4294967300, i64 4294967293) ; 0
  %d1 = icmp ne i64 %d, 0
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %o3 = or i1 %o2, %d1
  %r = zext i1 %o3 to i32
  ret i32 %r
}

; Bit 2: loop-carried values that rotate or swap.
define internal i32 @check_phis() noinline {
  %a = call i64 @fib64(i32 90)                ; 2880067194370816120
  %a1 = icmp ne i64 %a, 2880067194370816120
  %b = call i64 @fib64(i32 1)                 ; 1
  %b1 = icmp ne i64 %b, 1
  %c = call i32 @swap_parity(i32 7)           ; 21
  %c1 = icmp ne i32 %c, 21
  %d = call i32 @swap_parity(i32 10)          ; 12
  %d1 = icmp ne i32 %d, 12
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %o3 = or i1 %o2, %d1
  %r = zext i1 %o3 to i32
  ret i32 %r
}

; Bit 3: i8 and i16 counters that wrap.
define internal i32 @check_narrow() noinline {
  %a = call i32 @wrap_i8(i8 -6)               ; from 250: 10 trips
  %a1 = icmp ne i32 %a, 10
  %b = call i32 @wrap_i8(i8 4)                ; all the way round: 256
  %b1 = icmp ne i32 %b, 256
  %c = call i32 @wrap_i16(i16 -536)           ; from 65000: 77 trips, ends at 3
  %c1 = icmp ne i32 %c, 5046275
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %r = zext i1 %o2 to i32
  ret i32 %r
}

; Bit 4: unsigned bounds above 2^31 and a negative stride.
define internal i32 @check_bounds() noinline {
  %a = call i32 @unsigned_bound(i32 268435456, i32 -268435456, i32 268435456) ; 14
  %a1 = icmp ne i32 %a, 14
  %b = call i32 @neg_step(i32 100, i32 -100, i32 7) ; 29
  %b1 = icmp ne i32 %b, 29
  %c = call i32 @neg_step(i32 -101, i32 -100, i32 7) ; 0
  %c1 = icmp ne i32 %c, 0
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %r = zext i1 %o2 to i32
  ret i32 %r
}

; Bit 5: several exits and 'continue'.
define internal i32 @check_exits() noinline {
  %a = call i32 @multi_exit(i32 100)          ; 11
  %a1 = icmp ne i32 %a, 11
  %b = call i32 @multi_exit(i32 500)          ; 1013
  %b1 = icmp ne i32 %b, 1013
  %c = call i32 @continue_odd(i32 30)         ; 2988
  %c1 = icmp ne i32 %c, 2988
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %r = zext i1 %o2 to i32
  ret i32 %r
}

; Bit 6: a call in the loop body.
define internal i32 @check_calls() noinline {
  %a = call i32 @call_in_loop(i32 12)         ; 277939459
  %a1 = icmp ne i32 %a, 277939459
  %b = call i32 @call_in_loop(i32 0)          ; 1
  %b1 = icmp ne i32 %b, 1
  %o1 = or i1 %a1, %b1
  %r = zext i1 %o1 to i32
  ret i32 %r
}

; Bit 7: floating-point accumulators, induction variable and loop condition.
define internal i32 @check_fp() noinline {
  %a = call double @fsum(i32 10)              ; 22.5
  %a1 = fcmp une double %a, 22.5
  %b = call float @fsum32(i32 10)             ; 11.25
  %b1 = fcmp une float %b, 11.25
  %c = call i32 @fdouble(double 0.5, double 100.0) ; 8 trips, x = 128: 8128
  %c1 = icmp ne i32 %c, 8128
  %d = call i32 @fdouble(double 0x7FF8000000000000, double 100.0) ; NaN: 0 trips: 999
  %d1 = icmp ne i32 %d, 999
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %o3 = or i1 %o2, %d1
  %r = zext i1 %o3 to i32
  ret i32 %r
}

define i32 @main() {
  %b0 = call i32 @check_int32()
  %b1 = call i32 @check_native()
  %b2 = call i32 @check_phis()
  %b3 = call i32 @check_narrow()
  %b4 = call i32 @check_bounds()
  %b5 = call i32 @check_exits()
  %b6 = call i32 @check_calls()
  %b7 = call i32 @check_fp()
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
