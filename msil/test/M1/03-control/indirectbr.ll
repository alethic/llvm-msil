; Computed goto (Milestone 6): indirectbr on blockaddress values, which the
; IndirectBrExpand pass turns into a switch on the block numbers before
; instruction selection (blockaddress constants become small integers, so no
; address is ever taken). A three-way dispatch chosen by selects, and a
; threaded-code interpreter whose next target is carried around a loop in a
; PHI. Each noinline check function returns 1 when a result differs from the
; value C gives (in the comments); main ORs one bit per check into the exit
; status (2 bits, D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

; dispatch(sel, v): goto *(sel == 0 ? &&a : sel == 1 ? &&b : &&c);
;   a: return v + 1;  b: return v * 2;  c: return v ^ 255;
define internal i32 @dispatch(i32 %sel, i32 %v) noinline {
entry:
  %is0 = icmp eq i32 %sel, 0
  %is1 = icmp eq i32 %sel, 1
  %bc = select i1 %is1, ptr blockaddress(@dispatch, %b), ptr blockaddress(@dispatch, %c)
  %addr = select i1 %is0, ptr blockaddress(@dispatch, %a), ptr %bc
  indirectbr ptr %addr, [label %a, label %b, label %c]

a:
  %ra = add i32 %v, 1
  ret i32 %ra

b:
  %rb = mul i32 %v, 2
  ret i32 %rb

c:
  %rc = xor i32 %v, 255
  ret i32 %rc
}

; threaded(n): acc = 1, k = 0; start at op_inc; each op updates acc, counts a
; step and jumps to the next op, chosen from acc, until n steps ran (k is the
; step number before the increment):
;   op_inc: acc += k + 3, next op_mul if acc is odd else op_xor
;   op_mul: acc *= 5, next op_xor
;   op_xor: acc ^= k << 4, next op_inc
define internal i32 @threaded(i32 %n) noinline {
entry:
  br label %dispatch

dispatch:
  %next = phi ptr [ blockaddress(@threaded, %op_inc), %entry ], [ %n_inc, %op_inc ], [ blockaddress(@threaded, %op_xor), %op_mul ], [ blockaddress(@threaded, %op_inc), %op_xor ]
  %acc = phi i32 [ 1, %entry ], [ %acc_inc, %op_inc ], [ %acc_mul, %op_mul ], [ %acc_xor, %op_xor ]
  %k = phi i32 [ 0, %entry ], [ %k1, %op_inc ], [ %k1, %op_mul ], [ %k1, %op_xor ]
  %done = icmp sge i32 %k, %n
  br i1 %done, label %exit, label %go

go:
  %k1 = add i32 %k, 1
  indirectbr ptr %next, [label %op_inc, label %op_mul, label %op_xor]

op_inc:
  %acc_k = add i32 %acc, %k
  %acc_inc = add i32 %acc_k, 3
  %lo = and i32 %acc_inc, 1
  %odd = icmp ne i32 %lo, 0
  %n_inc = select i1 %odd, ptr blockaddress(@threaded, %op_mul), ptr blockaddress(@threaded, %op_xor)
  br label %dispatch

op_mul:
  %acc_mul = mul i32 %acc, 5
  br label %dispatch

op_xor:
  %k16 = shl i32 %k, 4
  %acc_xor = xor i32 %acc, %k16
  br label %dispatch

exit:
  ret i32 %acc
}

; Bit 0: dispatch(0, 10) = 11, dispatch(1, 10) = 20, dispatch(2, 10) = 245,
; dispatch(-1, 10) = 245.
define internal i32 @check_dispatch() noinline {
  %a = call i32 @dispatch(i32 0, i32 10)
  %a1 = icmp ne i32 %a, 11
  %b = call i32 @dispatch(i32 1, i32 10)
  %b1 = icmp ne i32 %b, 20
  %c = call i32 @dispatch(i32 2, i32 10)
  %c1 = icmp ne i32 %c, 245
  %d = call i32 @dispatch(i32 -1, i32 10)
  %d1 = icmp ne i32 %d, 245
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %o3 = or i1 %o2, %d1
  %r = zext i1 %o3 to i32
  ret i32 %r
}

; Bit 1: threaded(0) = 1, threaded(1) = 4, threaded(25) = 269308
; (10 op_inc, 6 op_mul and 9 op_xor steps).
define internal i32 @check_threaded() noinline {
  %a = call i32 @threaded(i32 0)
  %a1 = icmp ne i32 %a, 1
  %b = call i32 @threaded(i32 1)
  %b1 = icmp ne i32 %b, 4
  %c = call i32 @threaded(i32 25)
  %c1 = icmp ne i32 %c, 269308
  %o1 = or i1 %a1, %b1
  %o2 = or i1 %o1, %c1
  %r = zext i1 %o2 to i32
  ret i32 %r
}

define i32 @main() {
  %b0 = call i32 @check_dispatch()
  %b1 = call i32 @check_threaded()
  %s1 = shl i32 %b1, 1
  %m = or i32 %b0, %s1
  ret i32 %m
}
