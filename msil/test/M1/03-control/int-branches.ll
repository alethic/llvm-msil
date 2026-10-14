; Integer compare-and-branch (Milestone 6): every icmp predicate on int32 and
; native int operands, fused into one CIL branch (beq, bne.un, blt, blt.un,
; ble, ble.un, bgt, bgt.un, bge, bge.un) or not, in four block shapes per
; predicate: a diamond whose 'then' block comes first, a triangle whose
; branch target is the join block, a triangle whose 'then' block comes first,
; and a branch to two returns whose 'else' block comes first. Instruction
; selection inverts a condition whose true successor is the next block, and
; branch folding and tail duplication reverse or rewrite the others, so a
; wrong mapping or inverse shows up as a wrong bit. The operand pairs mix
; signs, and include native ints whose low 32 bits compare differently from
; the whole value (2^32 vs 1, 2^32 + 1 vs 1, 2^31 vs 2^31 - 1, 5 vs 2^32,
; -1 vs 2^32 - 1). Also compares with constants (and with zero: brtrue and
; brfalse), and branches on i1 arguments, call results, truncations, 'not',
; and on 'and'/'or' conditions that instruction selection splits into
; several branches.
; br_<pred>_<type>(a, b) returns 15 when the predicate holds (one bit per
; shape) and 0 otherwise; all_<type>(a, b) packs the ten results into 4-bit
; fields of a native int. Each noinline check function returns 1 when a result
; differs from the value C gives (the constants were computed from the
; operands); main ORs one bit per check into the exit status (7 bits, D37):
; [exit 0] means every check passed.
; RUN: %{msil_ll}

define internal i32 @br_eq_i32(i32 %a, i32 %b) noinline {
entry:
  %c0 = icmp eq i32 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp eq i32 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp eq i32 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp eq i32 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ne_i32(i32 %a, i32 %b) noinline {
entry:
  %c0 = icmp ne i32 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp ne i32 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp ne i32 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp ne i32 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_slt_i32(i32 %a, i32 %b) noinline {
entry:
  %c0 = icmp slt i32 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp slt i32 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp slt i32 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp slt i32 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_sle_i32(i32 %a, i32 %b) noinline {
entry:
  %c0 = icmp sle i32 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp sle i32 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp sle i32 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp sle i32 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_sgt_i32(i32 %a, i32 %b) noinline {
entry:
  %c0 = icmp sgt i32 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp sgt i32 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp sgt i32 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp sgt i32 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_sge_i32(i32 %a, i32 %b) noinline {
entry:
  %c0 = icmp sge i32 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp sge i32 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp sge i32 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp sge i32 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ult_i32(i32 %a, i32 %b) noinline {
entry:
  %c0 = icmp ult i32 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp ult i32 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp ult i32 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp ult i32 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ule_i32(i32 %a, i32 %b) noinline {
entry:
  %c0 = icmp ule i32 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp ule i32 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp ule i32 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp ule i32 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ugt_i32(i32 %a, i32 %b) noinline {
entry:
  %c0 = icmp ugt i32 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp ugt i32 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp ugt i32 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp ugt i32 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_uge_i32(i32 %a, i32 %b) noinline {
entry:
  %c0 = icmp uge i32 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp uge i32 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp uge i32 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp uge i32 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

; all_i32(a, b): the br_<pred>_i32 results in 4-bit fields, in the order
; eq, ne, slt, sle, sgt, sge, ult, ule, ugt, uge (bit 4 * k + shape).
define internal i64 @all_i32(i32 %a, i32 %b) noinline {
  %v0 = call i32 @br_eq_i32(i32 %a, i32 %b)
  %w0 = zext i32 %v0 to i64
  %v1 = call i32 @br_ne_i32(i32 %a, i32 %b)
  %w1 = zext i32 %v1 to i64
  %v2 = call i32 @br_slt_i32(i32 %a, i32 %b)
  %w2 = zext i32 %v2 to i64
  %v3 = call i32 @br_sle_i32(i32 %a, i32 %b)
  %w3 = zext i32 %v3 to i64
  %v4 = call i32 @br_sgt_i32(i32 %a, i32 %b)
  %w4 = zext i32 %v4 to i64
  %v5 = call i32 @br_sge_i32(i32 %a, i32 %b)
  %w5 = zext i32 %v5 to i64
  %v6 = call i32 @br_ult_i32(i32 %a, i32 %b)
  %w6 = zext i32 %v6 to i64
  %v7 = call i32 @br_ule_i32(i32 %a, i32 %b)
  %w7 = zext i32 %v7 to i64
  %v8 = call i32 @br_ugt_i32(i32 %a, i32 %b)
  %w8 = zext i32 %v8 to i64
  %v9 = call i32 @br_uge_i32(i32 %a, i32 %b)
  %w9 = zext i32 %v9 to i64
  %s1 = shl i64 %w1, 4
  %m1 = or i64 %w0, %s1
  %s2 = shl i64 %w2, 8
  %m2 = or i64 %m1, %s2
  %s3 = shl i64 %w3, 12
  %m3 = or i64 %m2, %s3
  %s4 = shl i64 %w4, 16
  %m4 = or i64 %m3, %s4
  %s5 = shl i64 %w5, 20
  %m5 = or i64 %m4, %s5
  %s6 = shl i64 %w6, 24
  %m6 = or i64 %m5, %s6
  %s7 = shl i64 %w7, 28
  %m7 = or i64 %m6, %s7
  %s8 = shl i64 %w8, 32
  %m8 = or i64 %m7, %s8
  %s9 = shl i64 %w9, 36
  %m9 = or i64 %m8, %s9
  ret i64 %m9
}

define internal i32 @br_eq_i64(i64 %a, i64 %b) noinline {
entry:
  %c0 = icmp eq i64 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp eq i64 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp eq i64 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp eq i64 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ne_i64(i64 %a, i64 %b) noinline {
entry:
  %c0 = icmp ne i64 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp ne i64 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp ne i64 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp ne i64 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_slt_i64(i64 %a, i64 %b) noinline {
entry:
  %c0 = icmp slt i64 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp slt i64 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp slt i64 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp slt i64 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_sle_i64(i64 %a, i64 %b) noinline {
entry:
  %c0 = icmp sle i64 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp sle i64 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp sle i64 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp sle i64 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_sgt_i64(i64 %a, i64 %b) noinline {
entry:
  %c0 = icmp sgt i64 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp sgt i64 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp sgt i64 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp sgt i64 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_sge_i64(i64 %a, i64 %b) noinline {
entry:
  %c0 = icmp sge i64 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp sge i64 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp sge i64 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp sge i64 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ult_i64(i64 %a, i64 %b) noinline {
entry:
  %c0 = icmp ult i64 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp ult i64 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp ult i64 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp ult i64 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ule_i64(i64 %a, i64 %b) noinline {
entry:
  %c0 = icmp ule i64 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp ule i64 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp ule i64 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp ule i64 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ugt_i64(i64 %a, i64 %b) noinline {
entry:
  %c0 = icmp ugt i64 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp ugt i64 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp ugt i64 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp ugt i64 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_uge_i64(i64 %a, i64 %b) noinline {
entry:
  %c0 = icmp uge i64 %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = icmp uge i64 %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = icmp uge i64 %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = icmp uge i64 %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

; all_i64(a, b): the br_<pred>_i64 results in 4-bit fields, in the order
; eq, ne, slt, sle, sgt, sge, ult, ule, ugt, uge (bit 4 * k + shape).
define internal i64 @all_i64(i64 %a, i64 %b) noinline {
  %v0 = call i32 @br_eq_i64(i64 %a, i64 %b)
  %w0 = zext i32 %v0 to i64
  %v1 = call i32 @br_ne_i64(i64 %a, i64 %b)
  %w1 = zext i32 %v1 to i64
  %v2 = call i32 @br_slt_i64(i64 %a, i64 %b)
  %w2 = zext i32 %v2 to i64
  %v3 = call i32 @br_sle_i64(i64 %a, i64 %b)
  %w3 = zext i32 %v3 to i64
  %v4 = call i32 @br_sgt_i64(i64 %a, i64 %b)
  %w4 = zext i32 %v4 to i64
  %v5 = call i32 @br_sge_i64(i64 %a, i64 %b)
  %w5 = zext i32 %v5 to i64
  %v6 = call i32 @br_ult_i64(i64 %a, i64 %b)
  %w6 = zext i32 %v6 to i64
  %v7 = call i32 @br_ule_i64(i64 %a, i64 %b)
  %w7 = zext i32 %v7 to i64
  %v8 = call i32 @br_ugt_i64(i64 %a, i64 %b)
  %w8 = zext i32 %v8 to i64
  %v9 = call i32 @br_uge_i64(i64 %a, i64 %b)
  %w9 = zext i32 %v9 to i64
  %s1 = shl i64 %w1, 4
  %m1 = or i64 %w0, %s1
  %s2 = shl i64 %w2, 8
  %m2 = or i64 %m1, %s2
  %s3 = shl i64 %w3, 12
  %m3 = or i64 %m2, %s3
  %s4 = shl i64 %w4, 16
  %m4 = or i64 %m3, %s4
  %s5 = shl i64 %w5, 20
  %m5 = or i64 %m4, %s5
  %s6 = shl i64 %w6, 24
  %m6 = or i64 %m5, %s6
  %s7 = shl i64 %w7, 28
  %m7 = or i64 %m6, %s7
  %s8 = shl i64 %w8, 32
  %m8 = or i64 %m7, %s8
  %s9 = shl i64 %w9, 36
  %m9 = or i64 %m8, %s9
  ret i64 %m9
}

; Bit 0: int32 pairs, same signs.
define internal i32 @check_i32_a() noinline {
  %m0 = call i64 @all_i32(i32 1, i32 2)
  %c0 = icmp ne i64 %m0, 4278255600
  %m1 = call i64 @all_i32(i32 2, i32 1)
  %c1 = icmp ne i64 %m1, 1095233372400
  %m2 = call i64 @all_i32(i32 7, i32 7)
  %c2 = icmp ne i64 %m2, 1034834472975
  %m3 = call i64 @all_i32(i32 -2, i32 -1)
  %c3 = icmp ne i64 %m3, 4278255600
  %o1 = or i1 %c0, %c1
  %o2 = or i1 %o1, %c2
  %o3 = or i1 %o2, %c3
  %r = zext i1 %o3 to i32
  ret i32 %r
}

; Bit 1: int32 pairs, mixed signs and the extremes.
define internal i32 @check_i32_b() noinline {
  %m0 = call i64 @all_i32(i32 -1, i32 1)
  %c0 = icmp ne i64 %m0, 1095216726000
  %m1 = call i64 @all_i32(i32 1, i32 -1)
  %c1 = icmp ne i64 %m1, 4294902000
  %m2 = call i64 @all_i32(i32 -2147483648, i32 2147483647)
  %c2 = icmp ne i64 %m2, 1095216726000
  %m3 = call i64 @all_i32(i32 2147483647, i32 -2147483648)
  %c3 = icmp ne i64 %m3, 4294902000
  %o1 = or i1 %c0, %c1
  %o2 = or i1 %o1, %c2
  %o3 = or i1 %o2, %c3
  %r = zext i1 %o3 to i32
  ret i32 %r
}

; Bit 2: native int pairs, same signs.
define internal i32 @check_i64_a() noinline {
  %m0 = call i64 @all_i64(i64 1, i64 2)
  %c0 = icmp ne i64 %m0, 4278255600
  %m1 = call i64 @all_i64(i64 8589934597, i64 8589934597)
  %c1 = icmp ne i64 %m1, 1034834472975
  %o1 = or i1 %c0, %c1
  %r = zext i1 %o1 to i32
  ret i32 %r
}

; Bit 3: native int pairs, mixed signs and the extremes.
define internal i32 @check_i64_b() noinline {
  %m0 = call i64 @all_i64(i64 -1, i64 1)
  %c0 = icmp ne i64 %m0, 1095216726000
  %m1 = call i64 @all_i64(i64 1, i64 -1)
  %c1 = icmp ne i64 %m1, 4294902000
  %m2 = call i64 @all_i64(i64 -9223372036854775808, i64 9223372036854775807)
  %c2 = icmp ne i64 %m2, 1095216726000
  %o1 = or i1 %c0, %c1
  %o2 = or i1 %o1, %c2
  %r = zext i1 %o2 to i32
  ret i32 %r
}

; Bit 4: native int pairs whose low 32 bits compare differently.
define internal i32 @check_i64_c() noinline {
  %m0 = call i64 @all_i64(i64 4294967296, i64 1)
  %c0 = icmp ne i64 %m0, 1095233372400
  %m1 = call i64 @all_i64(i64 4294967297, i64 1)
  %c1 = icmp ne i64 %m1, 1095233372400
  %m2 = call i64 @all_i64(i64 2147483648, i64 2147483647)
  %c2 = icmp ne i64 %m2, 1095233372400
  %m3 = call i64 @all_i64(i64 5, i64 4294967296)
  %c3 = icmp ne i64 %m3, 4278255600
  %m4 = call i64 @all_i64(i64 -1, i64 4294967295)
  %c4 = icmp ne i64 %m4, 1095216726000
  %o1 = or i1 %c0, %c1
  %o2 = or i1 %o1, %c2
  %o3 = or i1 %o2, %c3
  %o4 = or i1 %o3, %c4
  %r = zext i1 %o4 to i32
  ret i32 %r
}

; classify32(x): x == 0 -> 1; x < -100 -> 2; (unsigned)x > 0xFFFFFF00 -> 3;
; x > 1000 -> 4; (unsigned)x < 10 -> 5; otherwise 6.  (compares with constants)
define internal i32 @classify32(i32 %x) noinline {
entry:
  %z = icmp eq i32 %x, 0
  br i1 %z, label %r1, label %n1

n1:
  %neg = icmp slt i32 %x, -100
  br i1 %neg, label %r2, label %n2

n2:
  %hi = icmp ugt i32 %x, -256
  br i1 %hi, label %r3, label %n3

n3:
  %big = icmp sgt i32 %x, 1000
  br i1 %big, label %r4, label %n4

n4:
  %small = icmp ult i32 %x, 10
  br i1 %small, label %r5, label %r6

r1:
  ret i32 1

r2:
  ret i32 2

r3:
  ret i32 3

r4:
  ret i32 4

r5:
  ret i32 5

r6:
  ret i32 6
}

; classify64(x): x == 0 -> 1; x < -2^32 -> 2;
; (unsigned)x > 0xFFFFFFFF00000000 -> 3; x > 0xFFFFFFFF -> 4;
; (unsigned)x < 2^32 -> 5; otherwise 6.
define internal i32 @classify64(i64 %x) noinline {
entry:
  %z = icmp ne i64 %x, 0
  br i1 %z, label %n1, label %r1

n1:
  %neg = icmp slt i64 %x, -4294967296
  br i1 %neg, label %r2, label %n2

n2:
  %hi = icmp ugt i64 %x, -4294967296
  br i1 %hi, label %r3, label %n3

n3:
  %big = icmp sgt i64 %x, 4294967295
  br i1 %big, label %r4, label %n4

n4:
  %small = icmp ult i64 %x, 4294967296
  br i1 %small, label %r5, label %r6

r1:
  ret i32 1

r2:
  ret i32 2

r3:
  ret i32 3

r4:
  ret i32 4

r5:
  ret i32 5

r6:
  ret i32 6
}

; x in 0, -101, -100, -256, 1001, 1000, 9, 10, -300, 2147483647; 3-bit fields
define internal i32 @cmask32() noinline {
  %v0 = call i32 @classify32(i32 0)
  %v1 = call i32 @classify32(i32 -101)
  %v2 = call i32 @classify32(i32 -100)
  %v3 = call i32 @classify32(i32 -256)
  %v4 = call i32 @classify32(i32 1001)
  %v5 = call i32 @classify32(i32 1000)
  %v6 = call i32 @classify32(i32 9)
  %v7 = call i32 @classify32(i32 10)
  %v8 = call i32 @classify32(i32 -300)
  %v9 = call i32 @classify32(i32 2147483647)
  %s1 = shl i32 %v1, 3
  %m1 = or i32 %v0, %s1
  %s2 = shl i32 %v2, 6
  %m2 = or i32 %m1, %s2
  %s3 = shl i32 %v3, 9
  %m3 = or i32 %m2, %s3
  %s4 = shl i32 %v4, 12
  %m4 = or i32 %m3, %s4
  %s5 = shl i32 %v5, 15
  %m5 = or i32 %m4, %s5
  %s6 = shl i32 %v6, 18
  %m6 = or i32 %m5, %s6
  %s7 = shl i32 %v7, 21
  %m7 = or i32 %m6, %s7
  %s8 = shl i32 %v8, 24
  %m8 = or i32 %m7, %s8
  %s9 = shl i32 %v9, 27
  %m9 = or i32 %m8, %s9
  ret i32 %m9
}

; x in 0, -4294967297, -4294967296, -1, 4294967296, 4294967295, 5, -4294967295,
; -9223372036854775808, 9223372036854775807; 3-bit fields
define internal i32 @cmask64() noinline {
  %v0 = call i32 @classify64(i64 0)
  %v1 = call i32 @classify64(i64 -4294967297)
  %v2 = call i32 @classify64(i64 -4294967296)
  %v3 = call i32 @classify64(i64 -1)
  %v4 = call i32 @classify64(i64 4294967296)
  %v5 = call i32 @classify64(i64 4294967295)
  %v6 = call i32 @classify64(i64 5)
  %v7 = call i32 @classify64(i64 -4294967295)
  %v8 = call i32 @classify64(i64 -9223372036854775808)
  %v9 = call i32 @classify64(i64 9223372036854775807)
  %s1 = shl i32 %v1, 3
  %m1 = or i32 %v0, %s1
  %s2 = shl i32 %v2, 6
  %m2 = or i32 %m1, %s2
  %s3 = shl i32 %v3, 9
  %m3 = or i32 %m2, %s3
  %s4 = shl i32 %v4, 12
  %m4 = or i32 %m3, %s4
  %s5 = shl i32 %v5, 15
  %m5 = or i32 %m4, %s5
  %s6 = shl i32 %v6, 18
  %m6 = or i32 %m5, %s6
  %s7 = shl i32 %v7, 21
  %m7 = or i32 %m6, %s7
  %s8 = shl i32 %v8, 24
  %m8 = or i32 %m7, %s8
  %s9 = shl i32 %v9, 27
  %m9 = or i32 %m8, %s9
  ret i32 %m9
}

; Bit 5: compares with constants.
define internal i32 @check_const() noinline {
  %a = call i32 @cmask32()
  %a1 = icmp ne i32 %a, 584533201
  %b = call i32 @cmask64()
  %b1 = icmp ne i32 %b, 578209681
  %o = or i1 %a1, %b1
  %r = zext i1 %o to i32
  ret i32 %r
}

define internal i1 @is_neg(i32 %x) noinline {
  %c = icmp slt i32 %x, 0
  ret i1 %c
}

; misc(x, f): bit 0 = f (an i1 argument), bit 1 = x & 1 (trunc to i1),
; bit 2 = x > 0 && x < 100 ('and'), bit 3 = x < -5 || x > 50 (the 'select'
; form of a logical or), bit 4 = !(x == 7) ('xor' with true), bit 5 =
; is_neg(x) (an i1 call result).
define internal i32 @misc(i32 %x, i1 %f) noinline {
entry:
  br i1 %f, label %t0, label %j0

t0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %entry ]
  %lo = trunc i32 %x to i1
  br i1 %lo, label %t1, label %j1

t1:
  %r1a = or i32 %r0, 2
  br label %j1

j1:
  %r1 = phi i32 [ %r1a, %t1 ], [ %r0, %j0 ]
  %gt0 = icmp sgt i32 %x, 0
  %lt100 = icmp slt i32 %x, 100
  %in = and i1 %gt0, %lt100
  br i1 %in, label %t2, label %j2

t2:
  %r2a = or i32 %r1, 4
  br label %j2

j2:
  %r2 = phi i32 [ %r2a, %t2 ], [ %r1, %j1 ]
  %ltm5 = icmp slt i32 %x, -5
  %gt50 = icmp sgt i32 %x, 50
  %out = select i1 %ltm5, i1 true, i1 %gt50
  br i1 %out, label %t3, label %j3

t3:
  %r3a = or i32 %r2, 8
  br label %j3

j3:
  %r3 = phi i32 [ %r3a, %t3 ], [ %r2, %j2 ]
  %eq7 = icmp eq i32 %x, 7
  %ne7 = xor i1 %eq7, true
  br i1 %ne7, label %t4, label %j4

t4:
  %r4a = or i32 %r3, 16
  br label %j4

j4:
  %r4 = phi i32 [ %r4a, %t4 ], [ %r3, %j3 ]
  %n = call i1 @is_neg(i32 %x)
  br i1 %n, label %t5, label %j5

t5:
  %r5a = or i32 %r4, 32
  br label %j5

j5:
  %r5 = phi i32 [ %r5a, %t5 ], [ %r4, %j4 ]
  ret i32 %r5
}

; (x, f) in (0, false), (7, true), (-6, true), (51, false); 6-bit fields
define internal i32 @mmask1() noinline {
  %v0 = call i32 @misc(i32 0, i1 false)
  %v1 = call i32 @misc(i32 7, i1 true)
  %v2 = call i32 @misc(i32 -6, i1 true)
  %v3 = call i32 @misc(i32 51, i1 false)
  %s1 = shl i32 %v1, 6
  %m1 = or i32 %v0, %s1
  %s2 = shl i32 %v2, 12
  %m2 = or i32 %m1, %s2
  %s3 = shl i32 %v3, 18
  %m3 = or i32 %m2, %s3
  ret i32 %m3
}

; (x, f) in (99, true), (100, false), (-5, false), (1, true); 6-bit fields
define internal i32 @mmask2() noinline {
  %v0 = call i32 @misc(i32 99, i1 true)
  %v1 = call i32 @misc(i32 100, i1 false)
  %v2 = call i32 @misc(i32 -5, i1 false)
  %v3 = call i32 @misc(i32 1, i1 true)
  %s1 = shl i32 %v1, 6
  %m1 = or i32 %v0, %s1
  %s2 = shl i32 %v2, 12
  %m2 = or i32 %m1, %s2
  %s3 = shl i32 %v3, 18
  %m3 = or i32 %m2, %s3
  ret i32 %m3
}

; Bit 6: branches on i1 values, 'not', and 'and'/'or' conditions.
define internal i32 @check_misc() noinline {
  %a = call i32 @mmask1()
  %a1 = icmp ne i32 %a, 8098256
  %b = call i32 @mmask2()
  %b1 = icmp ne i32 %b, 6235679
  %o = or i1 %a1, %b1
  %r = zext i1 %o to i32
  ret i32 %r
}

define i32 @main() {
  %b0 = call i32 @check_i32_a()
  %b1 = call i32 @check_i32_b()
  %b2 = call i32 @check_i64_a()
  %b3 = call i32 @check_i64_b()
  %b4 = call i32 @check_i64_c()
  %b5 = call i32 @check_const()
  %b6 = call i32 @check_misc()
  %s1 = shl i32 %b1, 1
  %m1 = or i32 %b0, %s1
  %s2 = shl i32 %b2, 2
  %m2 = or i32 %m1, %s2
  %s3 = shl i32 %b3, 3
  %m3 = or i32 %m2, %s3
  %s4 = shl i32 %b4, 4
  %m4 = or i32 %m3, %s4
  %s5 = shl i32 %b5, 5
  %m5 = or i32 %m4, %s5
  %s6 = shl i32 %b6, 6
  %m6 = or i32 %m5, %s6
  ret i32 %m6
}
