; Floating-point compare-and-branch with NaN operands (Milestone 6): every
; ordered and unordered fcmp predicate on float32 and float64 feeding a
; branch, in the four block shapes of int-branches.ll (a diamond and a
; triangle whose 'then' block comes first, a triangle whose branch target is
; the join block, two returns whose 'else' block comes first). The fused CIL
; branches are NaN-sensitive: blt is OLT but blt.un is ULT, bne.un is UNE,
; and the inverse of an ordered predicate is the unordered one with the
; opposite sense (OLT -> UGE, not OGE), so a branch that instruction
; selection or branch analysis inverts with the wrong table takes the wrong
; edge for NaN operands only. ONE, UEQ, ORD and UNO have no single opcode.
; Operand pairs include quiet NaN, negative NaN, a signaling-NaN bit pattern,
; NaN on either side and both, signed zeros and infinities; NaN and the
; infinities also come from arithmetic at run time (0/0, inf - inf, 1/0).
; Also 'and'/'or' of compares, which instruction selection may split into
; two branches and invert, loops whose exit test is a floating-point compare,
; and compares with constants. br_<pred>_<type>(a, b) returns 15 when the
; predicate holds (one bit per shape) and 0 otherwise; all_<type>(a, b) packs
; the fourteen results into 4-bit fields. Each noinline check function returns
; 1 when a result differs from the value C gives (the constants were computed
; from the operands with IEEE semantics); main ORs one bit per check into the
; exit status (8 bits, D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

define internal i32 @br_oeq_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp oeq double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp oeq double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp oeq double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp oeq double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ogt_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp ogt double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ogt double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ogt double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ogt double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_oge_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp oge double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp oge double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp oge double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp oge double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_olt_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp olt double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp olt double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp olt double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp olt double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ole_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp ole double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ole double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ole double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ole double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_one_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp one double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp one double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp one double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp one double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ord_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp ord double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ord double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ord double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ord double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ueq_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp ueq double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ueq double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ueq double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ueq double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ugt_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp ugt double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ugt double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ugt double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ugt double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_uge_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp uge double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp uge double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp uge double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp uge double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ult_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp ult double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ult double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ult double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ult double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ule_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp ule double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ule double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ule double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ule double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_une_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp une double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp une double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp une double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp une double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_uno_f64(double %a, double %b) noinline {
entry:
  %c0 = fcmp uno double %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp uno double %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp uno double %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp uno double %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

; all_f64(a, b): the br_<pred>_f64 results in 4-bit fields, in the order
; oeq, ogt, oge, olt, ole, one, ord, ueq, ugt, uge, ult, ule, une, uno.
define internal i64 @all_f64(double %a, double %b) noinline {
  %v0 = call i32 @br_oeq_f64(double %a, double %b)
  %w0 = zext i32 %v0 to i64
  %v1 = call i32 @br_ogt_f64(double %a, double %b)
  %w1 = zext i32 %v1 to i64
  %v2 = call i32 @br_oge_f64(double %a, double %b)
  %w2 = zext i32 %v2 to i64
  %v3 = call i32 @br_olt_f64(double %a, double %b)
  %w3 = zext i32 %v3 to i64
  %v4 = call i32 @br_ole_f64(double %a, double %b)
  %w4 = zext i32 %v4 to i64
  %v5 = call i32 @br_one_f64(double %a, double %b)
  %w5 = zext i32 %v5 to i64
  %v6 = call i32 @br_ord_f64(double %a, double %b)
  %w6 = zext i32 %v6 to i64
  %v7 = call i32 @br_ueq_f64(double %a, double %b)
  %w7 = zext i32 %v7 to i64
  %v8 = call i32 @br_ugt_f64(double %a, double %b)
  %w8 = zext i32 %v8 to i64
  %v9 = call i32 @br_uge_f64(double %a, double %b)
  %w9 = zext i32 %v9 to i64
  %v10 = call i32 @br_ult_f64(double %a, double %b)
  %w10 = zext i32 %v10 to i64
  %v11 = call i32 @br_ule_f64(double %a, double %b)
  %w11 = zext i32 %v11 to i64
  %v12 = call i32 @br_une_f64(double %a, double %b)
  %w12 = zext i32 %v12 to i64
  %v13 = call i32 @br_uno_f64(double %a, double %b)
  %w13 = zext i32 %v13 to i64
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
  %s10 = shl i64 %w10, 40
  %m10 = or i64 %m9, %s10
  %s11 = shl i64 %w11, 44
  %m11 = or i64 %m10, %s11
  %s12 = shl i64 %w12, 48
  %m12 = or i64 %m11, %s12
  %s13 = shl i64 %w13, 52
  %m13 = or i64 %m12, %s13
  ret i64 %m13
}

define internal i32 @br_oeq_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp oeq float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp oeq float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp oeq float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp oeq float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ogt_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp ogt float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ogt float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ogt float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ogt float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_oge_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp oge float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp oge float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp oge float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp oge float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_olt_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp olt float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp olt float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp olt float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp olt float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ole_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp ole float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ole float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ole float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ole float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_one_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp one float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp one float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp one float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp one float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ord_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp ord float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ord float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ord float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ord float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ueq_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp ueq float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ueq float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ueq float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ueq float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ugt_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp ugt float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ugt float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ugt float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ugt float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_uge_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp uge float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp uge float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp uge float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp uge float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ult_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp ult float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ult float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ult float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ult float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_ule_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp ule float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp ule float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp ule float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp ule float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_une_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp une float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp une float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp une float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp une float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

define internal i32 @br_uno_f32(float %a, float %b) noinline {
entry:
  %c0 = fcmp uno float %a, %b
  br i1 %c0, label %t0, label %f0

t0:
  br label %j0

f0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %f0 ]
  %c1 = fcmp uno float %a, %b
  br i1 %c1, label %j1, label %f1

f1:
  br label %j1

j1:
  %r1 = phi i32 [ 2, %j0 ], [ 0, %f1 ]
  %r01 = or i32 %r0, %r1
  %c2 = fcmp uno float %a, %b
  br i1 %c2, label %t2, label %j2

t2:
  br label %j2

j2:
  %r2 = phi i32 [ 4, %t2 ], [ 0, %j1 ]
  %r012 = or i32 %r01, %r2
  %c3 = fcmp uno float %a, %b
  br i1 %c3, label %t3, label %e3

e3:
  ret i32 %r012

t3:
  %r3 = or i32 %r012, 8
  ret i32 %r3
}

; all_f32(a, b): the br_<pred>_f32 results in 4-bit fields, in the order
; oeq, ogt, oge, olt, ole, one, ord, ueq, ugt, uge, ult, ule, une, uno.
define internal i64 @all_f32(float %a, float %b) noinline {
  %v0 = call i32 @br_oeq_f32(float %a, float %b)
  %w0 = zext i32 %v0 to i64
  %v1 = call i32 @br_ogt_f32(float %a, float %b)
  %w1 = zext i32 %v1 to i64
  %v2 = call i32 @br_oge_f32(float %a, float %b)
  %w2 = zext i32 %v2 to i64
  %v3 = call i32 @br_olt_f32(float %a, float %b)
  %w3 = zext i32 %v3 to i64
  %v4 = call i32 @br_ole_f32(float %a, float %b)
  %w4 = zext i32 %v4 to i64
  %v5 = call i32 @br_one_f32(float %a, float %b)
  %w5 = zext i32 %v5 to i64
  %v6 = call i32 @br_ord_f32(float %a, float %b)
  %w6 = zext i32 %v6 to i64
  %v7 = call i32 @br_ueq_f32(float %a, float %b)
  %w7 = zext i32 %v7 to i64
  %v8 = call i32 @br_ugt_f32(float %a, float %b)
  %w8 = zext i32 %v8 to i64
  %v9 = call i32 @br_uge_f32(float %a, float %b)
  %w9 = zext i32 %v9 to i64
  %v10 = call i32 @br_ult_f32(float %a, float %b)
  %w10 = zext i32 %v10 to i64
  %v11 = call i32 @br_ule_f32(float %a, float %b)
  %w11 = zext i32 %v11 to i64
  %v12 = call i32 @br_une_f32(float %a, float %b)
  %w12 = zext i32 %v12 to i64
  %v13 = call i32 @br_uno_f32(float %a, float %b)
  %w13 = zext i32 %v13 to i64
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
  %s10 = shl i64 %w10, 40
  %m10 = or i64 %m9, %s10
  %s11 = shl i64 %w11, 44
  %m11 = or i64 %m10, %s11
  %s12 = shl i64 %w12, 48
  %m12 = or i64 %m11, %s12
  %s13 = shl i64 %w13, 52
  %m13 = or i64 %m12, %s13
  ret i64 %m13
}

; Bit 0: float64, operands without NaN.
define internal i32 @check_f64_ordered() noinline {
  %m0 = call i64 @all_f64(double 1.0, double 2.0)
  %c0 = icmp ne i64 %m0, 4502500384174080
  %m1 = call i64 @all_f64(double 2.0, double 1.0)
  %c1 = icmp ne i64 %m1, 4223220134711280
  %m2 = call i64 @all_f64(double 1.5, double 1.5)
  %c2 = icmp ne i64 %m2, 264917861994255
  %m3 = call i64 @all_f64(double -0.0, double 0.0)
  %c3 = icmp ne i64 %m3, 264917861994255
  %m4 = call i64 @all_f64(double 0xFFF0000000000000, double 0x7FF0000000000000)
  %c4 = icmp ne i64 %m4, 4502500384174080
  %m5 = call i64 @all_f64(double 0x7FF0000000000000, double 0x7FF0000000000000)
  %c5 = icmp ne i64 %m5, 264917861994255
  %o1 = or i1 %c0, %c1
  %o2 = or i1 %o1, %c2
  %o3 = or i1 %o2, %c3
  %o4 = or i1 %o3, %c4
  %o5 = or i1 %o4, %c5
  %r = zext i1 %o5 to i32
  ret i32 %r
}

; Bit 1: float64, NaN operands.
define internal i32 @check_f64_nan() noinline {
  %m0 = call i64 @all_f64(double 0x7FF8000000000000, double 1.0)
  %c0 = icmp ne i64 %m0, 72057593769492480
  %m1 = call i64 @all_f64(double 1.0, double 0xFFF8000000000000)
  %c1 = icmp ne i64 %m1, 72057593769492480
  %m2 = call i64 @all_f64(double 0x7FF8000000000000, double 0x7FF8000000000000)
  %c2 = icmp ne i64 %m2, 72057593769492480
  %m3 = call i64 @all_f64(double 0x7FF4000000000000, double 0.0)
  %c3 = icmp ne i64 %m3, 72057593769492480
  %o1 = or i1 %c0, %c1
  %o2 = or i1 %o1, %c2
  %o3 = or i1 %o2, %c3
  %r = zext i1 %o3 to i32
  ret i32 %r
}

; Bit 2: float32, operands without NaN.
define internal i32 @check_f32_ordered() noinline {
  %m0 = call i64 @all_f32(float 1.0, float 2.0)
  %c0 = icmp ne i64 %m0, 4502500384174080
  %m1 = call i64 @all_f32(float 2.0, float 1.0)
  %c1 = icmp ne i64 %m1, 4223220134711280
  %m2 = call i64 @all_f32(float 1.5, float 1.5)
  %c2 = icmp ne i64 %m2, 264917861994255
  %m3 = call i64 @all_f32(float -0.0, float 0.0)
  %c3 = icmp ne i64 %m3, 264917861994255
  %m4 = call i64 @all_f32(float 0xFFF0000000000000, float 0x7FF0000000000000)
  %c4 = icmp ne i64 %m4, 4502500384174080
  %m5 = call i64 @all_f32(float 0x7FF0000000000000, float 0x7FF0000000000000)
  %c5 = icmp ne i64 %m5, 264917861994255
  %o1 = or i1 %c0, %c1
  %o2 = or i1 %o1, %c2
  %o3 = or i1 %o2, %c3
  %o4 = or i1 %o3, %c4
  %o5 = or i1 %o4, %c5
  %r = zext i1 %o5 to i32
  ret i32 %r
}

; Bit 3: float32, NaN operands.
define internal i32 @check_f32_nan() noinline {
  %m0 = call i64 @all_f32(float 0x7FF8000000000000, float 1.0)
  %c0 = icmp ne i64 %m0, 72057593769492480
  %m1 = call i64 @all_f32(float 1.0, float 0xFFF8000000000000)
  %c1 = icmp ne i64 %m1, 72057593769492480
  %m2 = call i64 @all_f32(float 0x7FF8000000000000, float 0x7FF8000000000000)
  %c2 = icmp ne i64 %m2, 72057593769492480
  %m3 = call i64 @all_f32(float 0x7FF4000000000000, float 0.0)
  %c3 = icmp ne i64 %m3, 72057593769492480
  %o1 = or i1 %c0, %c1
  %o2 = or i1 %o1, %c2
  %o3 = or i1 %o2, %c3
  %r = zext i1 %o3 to i32
  ret i32 %r
}

; combo_f64(a, b, c): five conditions made of two or three compares joined
; by 'and', 'or' and the select forms of logical and/or (one bit each, see
; the comments); instruction selection may split each into two branches and
; invert one of them.
define internal i32 @combo_f64(double %a, double %b, double %c) noinline {
entry:
  ; bit 0: (a olt b) and (b olt c)
  %x0 = fcmp olt double %a, %b
  %y0 = fcmp olt double %b, %c
  %c0 = and i1 %x0, %y0
  br i1 %c0, label %t0, label %j0

t0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %entry ]
  ; bit 1: (a uno b) or (b oeq c)
  %x1 = fcmp uno double %a, %b
  %y1 = fcmp oeq double %b, %c
  %c1 = or i1 %x1, %y1
  br i1 %c1, label %t1, label %j1

t1:
  %r1a = or i32 %r0, 2
  br label %j1

j1:
  %r1 = phi i32 [ %r1a, %t1 ], [ %r0, %j0 ]
  ; bit 2: (a une b) logical-and (a oge c); the false block comes first
  %x2 = fcmp une double %a, %b
  %y2 = fcmp oge double %a, %c
  %c2 = select i1 %x2, i1 %y2, i1 false
  %r2a = or i32 %r1, 4
  br i1 %c2, label %j2, label %f2

f2:
  br label %j2

j2:
  %r2 = phi i32 [ %r2a, %j1 ], [ %r1, %f2 ]
  ; bit 3: (a ogt b) logical-or (b ule c)
  %x3 = fcmp ogt double %a, %b
  %y3 = fcmp ule double %b, %c
  %c3 = select i1 %x3, i1 true, i1 %y3
  br i1 %c3, label %t3, label %j3

t3:
  %r3a = or i32 %r2, 8
  br label %j3

j3:
  %r3 = phi i32 [ %r3a, %t3 ], [ %r2, %j2 ]
  ; bit 4: ((a olt b) and (b ogt c)) or (a ueq c)
  %x4 = fcmp olt double %a, %b
  %y4 = fcmp ogt double %b, %c
  %z4 = fcmp ueq double %a, %c
  %a4 = and i1 %x4, %y4
  %c4 = or i1 %a4, %z4
  br i1 %c4, label %t4, label %e4

e4:
  ret i32 %r3

t4:
  %r4 = or i32 %r3, 16
  ret i32 %r4
}

define internal i32 @check_combo_f64() noinline {
  %v0 = call i32 @combo_f64(double 1.0, double 2.0, double 3.0)
  %c0 = icmp ne i32 %v0, 9
  %v1 = call i32 @combo_f64(double 3.0, double 2.0, double 1.0)
  %c1 = icmp ne i32 %v1, 12
  %v2 = call i32 @combo_f64(double 0x7FF8000000000000, double 2.0, double 3.0)
  %c2 = icmp ne i32 %v2, 26
  %v3 = call i32 @combo_f64(double 1.0, double 0x7FF8000000000000, double 3.0)
  %c3 = icmp ne i32 %v3, 10
  %v4 = call i32 @combo_f64(double 1.0, double 2.0, double 0x7FF8000000000000)
  %c4 = icmp ne i32 %v4, 24
  %v5 = call i32 @combo_f64(double 2.0, double 2.0, double 2.0)
  %c5 = icmp ne i32 %v5, 26
  %v6 = call i32 @combo_f64(double -0.0, double 0.0, double -0.0)
  %c6 = icmp ne i32 %v6, 26
  %v7 = call i32 @combo_f64(double 0x7FF0000000000000, double 0x7FF8000000000000, double 0x7FF0000000000000)
  %c7 = icmp ne i32 %v7, 30
  %v8 = call i32 @combo_f64(double 1.0, double 3.0, double 2.0)
  %c8 = icmp ne i32 %v8, 16
  %v9 = call i32 @combo_f64(double 2.0, double 1.0, double 2.0)
  %c9 = icmp ne i32 %v9, 28
  %o1 = or i1 %c0, %c1
  %o2 = or i1 %o1, %c2
  %o3 = or i1 %o2, %c3
  %o4 = or i1 %o3, %c4
  %o5 = or i1 %o4, %c5
  %o6 = or i1 %o5, %c6
  %o7 = or i1 %o6, %c7
  %o8 = or i1 %o7, %c8
  %o9 = or i1 %o8, %c9
  %r = zext i1 %o9 to i32
  ret i32 %r
}

; combo_f32(a, b, c): five conditions made of two or three compares joined
; by 'and', 'or' and the select forms of logical and/or (one bit each, see
; the comments); instruction selection may split each into two branches and
; invert one of them.
define internal i32 @combo_f32(float %a, float %b, float %c) noinline {
entry:
  ; bit 0: (a olt b) and (b olt c)
  %x0 = fcmp olt float %a, %b
  %y0 = fcmp olt float %b, %c
  %c0 = and i1 %x0, %y0
  br i1 %c0, label %t0, label %j0

t0:
  br label %j0

j0:
  %r0 = phi i32 [ 1, %t0 ], [ 0, %entry ]
  ; bit 1: (a uno b) or (b oeq c)
  %x1 = fcmp uno float %a, %b
  %y1 = fcmp oeq float %b, %c
  %c1 = or i1 %x1, %y1
  br i1 %c1, label %t1, label %j1

t1:
  %r1a = or i32 %r0, 2
  br label %j1

j1:
  %r1 = phi i32 [ %r1a, %t1 ], [ %r0, %j0 ]
  ; bit 2: (a une b) logical-and (a oge c); the false block comes first
  %x2 = fcmp une float %a, %b
  %y2 = fcmp oge float %a, %c
  %c2 = select i1 %x2, i1 %y2, i1 false
  %r2a = or i32 %r1, 4
  br i1 %c2, label %j2, label %f2

f2:
  br label %j2

j2:
  %r2 = phi i32 [ %r2a, %j1 ], [ %r1, %f2 ]
  ; bit 3: (a ogt b) logical-or (b ule c)
  %x3 = fcmp ogt float %a, %b
  %y3 = fcmp ule float %b, %c
  %c3 = select i1 %x3, i1 true, i1 %y3
  br i1 %c3, label %t3, label %j3

t3:
  %r3a = or i32 %r2, 8
  br label %j3

j3:
  %r3 = phi i32 [ %r3a, %t3 ], [ %r2, %j2 ]
  ; bit 4: ((a olt b) and (b ogt c)) or (a ueq c)
  %x4 = fcmp olt float %a, %b
  %y4 = fcmp ogt float %b, %c
  %z4 = fcmp ueq float %a, %c
  %a4 = and i1 %x4, %y4
  %c4 = or i1 %a4, %z4
  br i1 %c4, label %t4, label %e4

e4:
  ret i32 %r3

t4:
  %r4 = or i32 %r3, 16
  ret i32 %r4
}

define internal i32 @check_combo_f32() noinline {
  %v0 = call i32 @combo_f32(float 1.0, float 2.0, float 3.0)
  %c0 = icmp ne i32 %v0, 9
  %v1 = call i32 @combo_f32(float 3.0, float 2.0, float 1.0)
  %c1 = icmp ne i32 %v1, 12
  %v2 = call i32 @combo_f32(float 0x7FF8000000000000, float 2.0, float 3.0)
  %c2 = icmp ne i32 %v2, 26
  %v3 = call i32 @combo_f32(float 1.0, float 0x7FF8000000000000, float 3.0)
  %c3 = icmp ne i32 %v3, 10
  %v4 = call i32 @combo_f32(float 1.0, float 2.0, float 0x7FF8000000000000)
  %c4 = icmp ne i32 %v4, 24
  %v5 = call i32 @combo_f32(float 2.0, float 2.0, float 2.0)
  %c5 = icmp ne i32 %v5, 26
  %v6 = call i32 @combo_f32(float -0.0, float 0.0, float -0.0)
  %c6 = icmp ne i32 %v6, 26
  %v7 = call i32 @combo_f32(float 0x7FF0000000000000, float 0x7FF8000000000000, float 0x7FF0000000000000)
  %c7 = icmp ne i32 %v7, 30
  %v8 = call i32 @combo_f32(float 1.0, float 3.0, float 2.0)
  %c8 = icmp ne i32 %v8, 16
  %v9 = call i32 @combo_f32(float 2.0, float 1.0, float 2.0)
  %c9 = icmp ne i32 %v9, 28
  %o1 = or i1 %c0, %c1
  %o2 = or i1 %o1, %c2
  %o3 = or i1 %o2, %c3
  %o4 = or i1 %o3, %c4
  %o5 = or i1 %o4, %c5
  %o6 = or i1 %o5, %c6
  %o7 = or i1 %o6, %c7
  %o8 = or i1 %o7, %c8
  %o9 = or i1 %o8, %c9
  %r = zext i1 %o9 to i32
  ret i32 %r
}

; Bit 4: 'and'/'or' conditions.
define internal i32 @check_combos() noinline {
  %a = call i32 @check_combo_f64()
  %b = call i32 @check_combo_f32()
  %r = or i32 %a, %b
  ret i32 %r
}

; loop_ult_f64(start, limit, step): x = start; k = 0; do { x += step; k++; }
; while fcmp ult x, limit, at most 10 times; returns k.
define internal i32 @loop_ult_f64(double %start, double %limit, double %step) noinline {
entry:
  br label %loop

loop:
  %x = phi double [ %start, %entry ], [ %x1, %test ]
  %k = phi i32 [ 0, %entry ], [ %k1, %test ]
  %x1 = fadd double %x, %step
  %k1 = add i32 %k, 1
  %cap = icmp eq i32 %k1, 10
  br i1 %cap, label %exit, label %test

test:
  %c = fcmp ult double %x1, %limit
  br i1 %c, label %loop, label %exit

exit:
  ret i32 %k1
}

; loop_oge_f32(start, limit, step): x = start; k = 0; do { x += step; k++; }
; until x >= limit (exits on an ordered >=), at most 10 times; returns k.
define internal i32 @loop_oge_f32(float %start, float %limit, float %step) noinline {
entry:
  br label %loop

loop:
  %x = phi float [ %start, %entry ], [ %x1, %test ]
  %k = phi i32 [ 0, %entry ], [ %k1, %test ]
  %x1 = fadd float %x, %step
  %k1 = add i32 %k, 1
  %cap = icmp eq i32 %k1, 10
  br i1 %cap, label %exit, label %test

test:
  %c = fcmp oge float %x1, %limit
  br i1 %c, label %exit, label %loop

exit:
  ret i32 %k1
}

; loop_une_f64(start, limit, step): x = start; k = 0; do { x += step; k++; }
; while fcmp une x, limit, at most 10 times; returns k.
define internal i32 @loop_une_f64(double %start, double %limit, double %step) noinline {
entry:
  br label %loop

loop:
  %x = phi double [ %start, %entry ], [ %x1, %test ]
  %k = phi i32 [ 0, %entry ], [ %k1, %test ]
  %x1 = fadd double %x, %step
  %k1 = add i32 %k, 1
  %cap = icmp eq i32 %k1, 10
  br i1 %cap, label %exit, label %test

test:
  %c = fcmp une double %x1, %limit
  br i1 %c, label %loop, label %exit

exit:
  ret i32 %k1
}

; loop_one_f32(start, limit, step): x = start; k = 0; do { x += step; k++; }
; while fcmp one x, limit, at most 10 times; returns k.
define internal i32 @loop_one_f32(float %start, float %limit, float %step) noinline {
entry:
  br label %loop

loop:
  %x = phi float [ %start, %entry ], [ %x1, %test ]
  %k = phi i32 [ 0, %entry ], [ %k1, %test ]
  %x1 = fadd float %x, %step
  %k1 = add i32 %k, 1
  %cap = icmp eq i32 %k1, 10
  br i1 %cap, label %exit, label %test

test:
  %c = fcmp one float %x1, %limit
  br i1 %c, label %loop, label %exit

exit:
  ret i32 %k1
}

; loop_ult_f32(start, limit, step): x = start; k = 0; do { x += step; k++; }
; while fcmp ult x, limit, at most 10 times; returns k.
define internal i32 @loop_ult_f32(float %start, float %limit, float %step) noinline {
entry:
  br label %loop

loop:
  %x = phi float [ %start, %entry ], [ %x1, %test ]
  %k = phi i32 [ 0, %entry ], [ %k1, %test ]
  %x1 = fadd float %x, %step
  %k1 = add i32 %k, 1
  %cap = icmp eq i32 %k1, 10
  br i1 %cap, label %exit, label %test

test:
  %c = fcmp ult float %x1, %limit
  br i1 %c, label %loop, label %exit

exit:
  ret i32 %k1
}

; Bit 5: loops whose exit test is a floating-point compare (NaN never
; satisfies an ordered predicate and always satisfies an unordered one).
define internal i32 @check_loops() noinline {
  %v0 = call i32 @loop_ult_f64(double 0.0, double 4.0, double 1.0)
  %c0 = icmp ne i32 %v0, 4
  %v1 = call i32 @loop_ult_f64(double 0.0, double 0x7FF8000000000000, double 1.0)
  %c1 = icmp ne i32 %v1, 10
  %v2 = call i32 @loop_ult_f64(double 0x7FF8000000000000, double 4.0, double 1.0)
  %c2 = icmp ne i32 %v2, 10
  %v3 = call i32 @loop_oge_f32(float 0.0, float 4.0, float 1.0)
  %c3 = icmp ne i32 %v3, 4
  %v4 = call i32 @loop_oge_f32(float 0.0, float 0x7FF8000000000000, float 1.0)
  %c4 = icmp ne i32 %v4, 10
  %v5 = call i32 @loop_une_f64(double 0.0, double 3.0, double 0.5)
  %c5 = icmp ne i32 %v5, 6
  %v6 = call i32 @loop_une_f64(double 0.0, double 0x7FF8000000000000, double 0.5)
  %c6 = icmp ne i32 %v6, 10
  %v7 = call i32 @loop_one_f32(float 0.0, float 3.0, float 1.0)
  %c7 = icmp ne i32 %v7, 3
  %v8 = call i32 @loop_one_f32(float 0.0, float 0x7FF8000000000000, float 1.0)
  %c8 = icmp ne i32 %v8, 1
  %v9 = call i32 @loop_ult_f32(float 0xFFF0000000000000, float 4.0, float 1.0)
  %c9 = icmp ne i32 %v9, 10
  %o1 = or i1 %c0, %c1
  %o2 = or i1 %o1, %c2
  %o3 = or i1 %o2, %c3
  %o4 = or i1 %o3, %c4
  %o5 = or i1 %o4, %c5
  %o6 = or i1 %o5, %c6
  %o7 = or i1 %o6, %c7
  %o8 = or i1 %o7, %c8
  %o9 = or i1 %o8, %c9
  %r = zext i1 %o9 to i32
  ret i32 %r
}

define internal double @div_f64(double %a, double %b) noinline {
  %r = fdiv double %a, %b
  ret double %r
}

define internal double @sub_f64(double %a, double %b) noinline {
  %r = fsub double %a, %b
  ret double %r
}

define internal float @trunc_f64(double %a) noinline {
  %r = fptrunc double %a to float
  ret float %r
}

; Bit 6: NaN and infinities computed at run time (0/0, inf - inf, 1/0, -1/0,
; and float32 NaN and infinity from fptrunc).
define internal i32 @check_runtime() noinline {
  %nan = call double @div_f64(double 0.0, double 0.0)
  %inf = call double @div_f64(double 1.0, double 0.0)
  %ninf = call double @div_f64(double -1.0, double 0.0)
  %nan2 = call double @sub_f64(double %inf, double %inf)
  %fnan = call float @trunc_f64(double %nan)
  %finf = call float @trunc_f64(double %inf)
  %m0 = call i64 @all_f64(double %nan, double 1.0)
  %c0 = icmp ne i64 %m0, 72057593769492480
  %m1 = call i64 @all_f64(double 1.0, double %nan)
  %c1 = icmp ne i64 %m1, 72057593769492480
  %m2 = call i64 @all_f64(double %nan, double %nan2)
  %c2 = icmp ne i64 %m2, 72057593769492480
  %m3 = call i64 @all_f64(double %inf, double %ninf)
  %c3 = icmp ne i64 %m3, 4223220134711280
  %m4 = call i64 @all_f64(double %inf, double 0x7E37E43C8800759C)
  %c4 = icmp ne i64 %m4, 4223220134711280
  %m5 = call i64 @all_f64(double %nan2, double %inf)
  %c5 = icmp ne i64 %m5, 72057593769492480
  %m6 = call i64 @all_f64(double %ninf, double %ninf)
  %c6 = icmp ne i64 %m6, 264917861994255
  %m7 = call i64 @all_f32(float %fnan, float 1.0)
  %c7 = icmp ne i64 %m7, 72057593769492480
  %m8 = call i64 @all_f32(float %finf, float %fnan)
  %c8 = icmp ne i64 %m8, 72057593769492480
  %m9 = call i64 @all_f32(float %finf, float 0x4630000000000000)
  %c9 = icmp ne i64 %m9, 4223220134711280
  %o1 = or i1 %c0, %c1
  %o2 = or i1 %o1, %c2
  %o3 = or i1 %o2, %c3
  %o4 = or i1 %o3, %c4
  %o5 = or i1 %o4, %c5
  %o6 = or i1 %o5, %c6
  %o7 = or i1 %o6, %c7
  %o8 = or i1 %o7, %c8
  %o9 = or i1 %o8, %c9
  %r = zext i1 %o9 to i32
  ret i32 %r
}

; fclass_f64(x): x < -2^100 -> 1; x < 0 -> 2; x > 2^100 -> 3; x == 0 -> 4;
; NaN -> 5; otherwise 6.  (compares with constants)
define internal i32 @fclass_f64(double %x) noinline {
entry:
  %neg = fcmp olt double %x, 0.0
  br i1 %neg, label %negative, label %n1

negative:
  %huge = fcmp olt double %x, 0xC630000000000000
  br i1 %huge, label %r1, label %r2

n1:
  %big = fcmp ogt double %x, 0x4630000000000000
  br i1 %big, label %r3, label %n2

n2:
  %zero = fcmp oeq double %x, 0.0
  br i1 %zero, label %r4, label %n3

n3:
  %nan = fcmp uno double %x, %x
  br i1 %nan, label %r5, label %r6

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

define internal i32 @fclass_mask_f64() noinline {
  %v0 = call i32 @fclass_f64(double 0xFFF0000000000000)
  %v1 = call i32 @fclass_f64(double -1.0)
  %v2 = call i32 @fclass_f64(double 0x7FF0000000000000)
  %v3 = call i32 @fclass_f64(double 0.0)
  %v4 = call i32 @fclass_f64(double -0.0)
  %v5 = call i32 @fclass_f64(double 0x7FF8000000000000)
  %v6 = call i32 @fclass_f64(double 1.0)
  %v7 = call i32 @fclass_f64(double 0x4640000000000000)
  %v8 = call i32 @fclass_f64(double 0xC640000000000000)
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
  ret i32 %m8
}

; fclass_f32(x): x < -2^100 -> 1; x < 0 -> 2; x > 2^100 -> 3; x == 0 -> 4;
; NaN -> 5; otherwise 6.  (compares with constants)
define internal i32 @fclass_f32(float %x) noinline {
entry:
  %neg = fcmp olt float %x, 0.0
  br i1 %neg, label %negative, label %n1

negative:
  %huge = fcmp olt float %x, 0xC630000000000000
  br i1 %huge, label %r1, label %r2

n1:
  %big = fcmp ogt float %x, 0x4630000000000000
  br i1 %big, label %r3, label %n2

n2:
  %zero = fcmp oeq float %x, 0.0
  br i1 %zero, label %r4, label %n3

n3:
  %nan = fcmp uno float %x, %x
  br i1 %nan, label %r5, label %r6

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

define internal i32 @fclass_mask_f32() noinline {
  %v0 = call i32 @fclass_f32(float 0xFFF0000000000000)
  %v1 = call i32 @fclass_f32(float -1.0)
  %v2 = call i32 @fclass_f32(float 0x7FF0000000000000)
  %v3 = call i32 @fclass_f32(float 0.0)
  %v4 = call i32 @fclass_f32(float -0.0)
  %v5 = call i32 @fclass_f32(float 0x7FF8000000000000)
  %v6 = call i32 @fclass_f32(float 1.0)
  %v7 = call i32 @fclass_f32(float 0x4640000000000000)
  %v8 = call i32 @fclass_f32(float 0xC640000000000000)
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
  ret i32 %m8
}

; Bit 7: compares with constants; inputs -inf, -1, inf, 0, -0, NaN, 1, 2^101 and
; -2^101 in 3-bit fields.
define internal i32 @check_const() noinline {
  %a = call i32 @fclass_mask_f64()
  %a1 = icmp ne i32 %a, 24824017
  %b = call i32 @fclass_mask_f32()
  %b1 = icmp ne i32 %b, 24824017
  %o = or i1 %a1, %b1
  %r = zext i1 %o to i32
  ret i32 %r
}

define i32 @main() {
  %b0 = call i32 @check_f64_ordered()
  %b1 = call i32 @check_f64_nan()
  %b2 = call i32 @check_f32_ordered()
  %b3 = call i32 @check_f32_nan()
  %b4 = call i32 @check_combos()
  %b5 = call i32 @check_loops()
  %b6 = call i32 @check_runtime()
  %b7 = call i32 @check_const()
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
  %s7 = shl i32 %b7, 7
  %m7 = or i32 %m6, %s7
  ret i32 %m7
}
