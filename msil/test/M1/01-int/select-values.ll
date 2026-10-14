; select (Milestone 4, D34): a select that stays a select becomes a brtrue
; triangle with a PHI. Selects of int32 and native int values on integer
; conditions, selects of constants (most become arithmetic), signed and
; unsigned min and max (the llvm.smin/smax/umin/umax intrinsics and the
; compare+select idioms), clamps and select chains, i1 conditions that arrive
; as parameters and results, and float32/float64 selects next to integer ones
; on the same condition. FP values cannot be observed before M5 (FP compares
; and conversions), so the FP selects here are executed, verified and
; JIT-compiled but only the integer results are checked. Each noinline check
; function returns 1 when a result differs from the value C gives (in the
; comments); main ORs one bit per check into the exit status (8 bits, D37):
; [exit 0] means every check passed.
; RUN: %{msil_ll}

declare i32 @llvm.smin.i32(i32, i32)
declare i32 @llvm.smax.i32(i32, i32)
declare i32 @llvm.umin.i32(i32, i32)
declare i32 @llvm.umax.i32(i32, i32)
declare i64 @llvm.smin.i64(i64, i64)
declare i64 @llvm.smax.i64(i64, i64)
declare i64 @llvm.umin.i64(i64, i64)
declare i64 @llvm.umax.i64(i64, i64)
declare i32 @llvm.abs.i32(i32, i1)

define internal i32 @sel_i32(i32 %a, i32 %b, i32 %x, i32 %y) noinline {
  %c = icmp slt i32 %a, %b
  %s = select i1 %c, i32 %x, i32 %y
  ret i32 %s
}

define internal i32 @ten() noinline {
  ret i32 10
}

define internal i32 @twenty() noinline {
  ret i32 20
}

; Bit 0: select i32, both outcomes; operands and condition from call results.
define internal i32 @check_i32(i32 %one, i32 %two) noinline {
  %r0 = call i32 @sel_i32(i32 %one, i32 %two, i32 10, i32 20)  ; 10
  %r1 = call i32 @sel_i32(i32 %two, i32 %one, i32 10, i32 20)  ; 20
  %r2 = call i32 @sel_i32(i32 %two, i32 %two, i32 10, i32 20)  ; 20
  %t = call i32 @ten()
  %w = call i32 @twenty()
  %c3 = icmp ugt i32 %w, %t
  %r3 = select i1 %c3, i32 %t, i32 %w                          ; 10
  %c4 = icmp eq i32 %r3, %w
  %r4 = select i1 %c4, i32 %one, i32 %r1                       ; 20
  %x0 = xor i32 %r0, 10
  %x1 = xor i32 %r1, 20
  %x2 = xor i32 %r2, 20
  %x3 = xor i32 %r3, 10
  %x4 = xor i32 %r4, 20
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %bad = icmp ne i32 %o4, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define i64 @sel_i64(i64 %a, i64 %b, i64 %x, i64 %y) noinline {
  %c = icmp ult i64 %a, %b
  %s = select i1 %c, i64 %x, i64 %y
  ret i64 %s
}

; Bit 1: select native int values on an i64 compare and on an i32 compare.
define internal i32 @check_i64(i64 %big, i64 %low, i64 %x, i64 %y, i32 %m1, i32 %one) noinline {
  ; big = 0x100000000, low = 0xFFFFFFFF, x = 0x123456789, y = -0x987654321
  %r0 = call i64 @sel_i64(i64 %big, i64 %low, i64 %x, i64 %y)  ; y
  %r1 = call i64 @sel_i64(i64 %low, i64 %big, i64 %x, i64 %y)  ; x
  %c2 = icmp slt i32 %m1, %one
  %r2 = select i1 %c2, i64 %y, i64 %x                          ; y
  %c3 = icmp ult i32 %m1, %one
  %r3 = select i1 %c3, i64 %y, i64 %big                        ; big
  %x0 = xor i64 %r0, -40926266145
  %x1 = xor i64 %r1, 4886718345
  %x2 = xor i64 %r2, -40926266145
  %x3 = xor i64 %r3, 4294967296
  %o1 = or i64 %x0, %x1
  %o2 = or i64 %o1, %x2
  %o3 = or i64 %o2, %x3
  %bad = icmp ne i64 %o3, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 2: selects of constants, with a true and a false condition (DAGCombiner
; turns most of them into arithmetic; 42/17 may stay a select).
define internal i32 @check_consts(i32 %a, i32 %b) noinline {
  ; a = -5, b = 7
  %t = icmp slt i32 %a, %b
  %f = icmp sgt i32 %a, %b
  %s0 = select i1 %t, i32 5, i32 4         ; 5
  %s1 = select i1 %f, i32 5, i32 4         ; 4
  %s2 = select i1 %t, i32 -1, i32 0        ; -1
  %s3 = select i1 %f, i32 -1, i32 0        ; 0
  %s4 = select i1 %t, i32 0, i32 1         ; 0
  %s5 = select i1 %f, i32 8, i32 0         ; 0
  %s6 = select i1 %t, i32 8, i32 0         ; 8
  %s7 = select i1 %t, i32 42, i32 17       ; 42
  %s8 = select i1 %f, i32 42, i32 17       ; 17
  %l0 = select i1 %t, i64 4294967296, i64 7 ; 0x100000000
  %l1 = select i1 %f, i64 4294967296, i64 7 ; 7
  %l2 = select i1 %t, i64 -1, i64 0        ; -1
  %x0 = xor i32 %s0, 5
  %x1 = xor i32 %s1, 4
  %x2 = xor i32 %s2, -1
  %x6 = xor i32 %s6, 8
  %x7 = xor i32 %s7, 42
  %x8 = xor i32 %s8, 17
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %s3
  %o4 = or i32 %o3, %s4
  %o5 = or i32 %o4, %s5
  %o6 = or i32 %o5, %x6
  %o7 = or i32 %o6, %x7
  %o8 = or i32 %o7, %x8
  %y0 = xor i64 %l0, 4294967296
  %y1 = xor i64 %l1, 7
  %y2 = xor i64 %l2, -1
  %p1 = or i64 %y0, %y1
  %p2 = or i64 %p1, %y2
  %o8w = zext i32 %o8 to i64
  %all = or i64 %p2, %o8w
  %bad = icmp ne i64 %all, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 3: signed min and max of int32 (intrinsics and idioms), and abs.
define internal i32 @check_smin_smax(i32 %m1, i32 %one, i32 %min, i32 %max, i32 %five, i32 %m7) noinline {
  %r0 = call i32 @llvm.smin.i32(i32 %m1, i32 %one)     ; -1
  %r1 = call i32 @llvm.smax.i32(i32 %m1, i32 %one)     ; 1
  %r2 = call i32 @llvm.smin.i32(i32 %max, i32 %min)    ; INT_MIN
  %r3 = call i32 @llvm.smax.i32(i32 %min, i32 %max)    ; INT_MAX
  %r4 = call i32 @llvm.smin.i32(i32 %five, i32 %five)  ; 5
  %c5 = icmp slt i32 %one, %m1
  %r5 = select i1 %c5, i32 %one, i32 %m1               ; -1 (min idiom)
  %c6 = icmp sgt i32 %min, %five
  %r6 = select i1 %c6, i32 %min, i32 %five             ; 5 (max idiom)
  %r7 = call i32 @llvm.abs.i32(i32 %m7, i1 false)      ; 7
  %r8 = call i32 @llvm.abs.i32(i32 %min, i1 false)     ; INT_MIN
  %x0 = xor i32 %r0, -1
  %x1 = xor i32 %r1, 1
  %x2 = xor i32 %r2, -2147483648
  %x3 = xor i32 %r3, 2147483647
  %x4 = xor i32 %r4, 5
  %x5 = xor i32 %r5, -1
  %x6 = xor i32 %r6, 5
  %x7 = xor i32 %r7, 7
  %x8 = xor i32 %r8, -2147483648
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %o6 = or i32 %o5, %x6
  %o7 = or i32 %o6, %x7
  %o8 = or i32 %o7, %x8
  %bad = icmp ne i32 %o8, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 4: unsigned min and max of int32 (intrinsics and idioms).
define internal i32 @check_umin_umax(i32 %m1, i32 %one, i32 %min, i32 %max) noinline {
  %r0 = call i32 @llvm.umin.i32(i32 %m1, i32 %one)     ; 1
  %r1 = call i32 @llvm.umax.i32(i32 %m1, i32 %one)     ; -1
  %r2 = call i32 @llvm.umin.i32(i32 %min, i32 %max)    ; INT_MAX
  %r3 = call i32 @llvm.umax.i32(i32 %max, i32 %min)    ; INT_MIN
  %c4 = icmp ult i32 %m1, %one
  %r4 = select i1 %c4, i32 %m1, i32 %one               ; 1 (min idiom)
  %c5 = icmp ugt i32 %one, %min
  %r5 = select i1 %c5, i32 %one, i32 %min              ; INT_MIN (max idiom)
  %x0 = xor i32 %r0, 1
  %x1 = xor i32 %r1, -1
  %x2 = xor i32 %r2, 2147483647
  %x3 = xor i32 %r3, -2147483648
  %x4 = xor i32 %r4, 1
  %x5 = xor i32 %r5, -2147483648
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %bad = icmp ne i32 %o5, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 5: signed and unsigned min and max of native int.
define internal i32 @check_minmax64(i64 %m1, i64 %big, i64 %low, i64 %min, i64 %max) noinline {
  ; big = 0x100000000, low = 0xFFFFFFFF
  %r0 = call i64 @llvm.smin.i64(i64 %m1, i64 %big)     ; -1
  %r1 = call i64 @llvm.umin.i64(i64 %m1, i64 %big)     ; big
  %r2 = call i64 @llvm.smax.i64(i64 %low, i64 %big)    ; big
  %r3 = call i64 @llvm.umax.i64(i64 %min, i64 %max)    ; INT64_MIN
  %r4 = call i64 @llvm.smin.i64(i64 %max, i64 %min)    ; INT64_MIN
  %r5 = call i64 @llvm.smax.i64(i64 %m1, i64 %low)     ; low
  %c6 = icmp ugt i64 %low, %big
  %r6 = select i1 %c6, i64 %low, i64 %big              ; big (umax idiom)
  %x0 = xor i64 %r0, -1
  %x1 = xor i64 %r1, 4294967296
  %x2 = xor i64 %r2, 4294967296
  %x3 = xor i64 %r3, -9223372036854775808
  %x4 = xor i64 %r4, -9223372036854775808
  %x5 = xor i64 %r5, 4294967295
  %x6 = xor i64 %r6, 4294967296
  %o1 = or i64 %x0, %x1
  %o2 = or i64 %o1, %x2
  %o3 = or i64 %o2, %x3
  %o4 = or i64 %o3, %x4
  %o5 = or i64 %o4, %x5
  %o6 = or i64 %o5, %x6
  %bad = icmp ne i64 %o6, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal i32 @clamp(i32 %x, i32 %lo, i32 %hi) noinline {
  %c0 = icmp sgt i32 %x, %hi
  %m = select i1 %c0, i32 %hi, i32 %x
  %c1 = icmp slt i32 %m, %lo
  %r = select i1 %c1, i32 %lo, i32 %m
  ret i32 %r
}

define internal i32 @max4(i32 %a, i32 %b, i32 %c, i32 %d) noinline {
  %c0 = icmp sgt i32 %a, %b
  %m0 = select i1 %c0, i32 %a, i32 %b
  %c1 = icmp sgt i32 %m0, %c
  %m1 = select i1 %c1, i32 %m0, i32 %c
  %c2 = icmp sgt i32 %m1, %d
  %m2 = select i1 %c2, i32 %m1, i32 %d
  ret i32 %m2
}

define internal zeroext i1 @is_odd(i32 %v) noinline {
  %b = trunc i32 %v to i1
  ret i1 %b
}

define internal i64 @pick(i1 %b, i64 %x, i64 %y) noinline {
  %s = select i1 %b, i64 %x, i64 %y
  ret i64 %s
}

; Bit 6: select chains (clamp, max of four), a select result passed to a call,
; a condition that is a zeroext i1 result, and an i1 parameter without an
; attribute that arrives as 2 (low bit clear, so false).
define internal i32 @check_chains(i32 %m50, i32 %p50, i32 %three, i32 %two) noinline {
  %r0 = call i32 @clamp(i32 %m50, i32 -10, i32 10)     ; -10
  %r1 = call i32 @clamp(i32 %p50, i32 -10, i32 10)     ; 10
  %r2 = call i32 @clamp(i32 %three, i32 -10, i32 10)   ; 3
  %r3 = call i32 @max4(i32 %two, i32 %m50, i32 %p50, i32 %three) ; 50
  %r4 = call i32 @max4(i32 %p50, i32 %two, i32 %three, i32 %m50) ; 50
  %r5 = call i32 @max4(i32 %m50, i32 %m50, i32 %m50, i32 %two)   ; 2
  %odd = call zeroext i1 @is_odd(i32 %three)
  %s6 = select i1 %odd, i32 %m50, i32 %p50
  %r6 = call i32 @clamp(i32 %s6, i32 -20, i32 20)      ; -20
  %t2 = trunc i32 %two to i1
  %t3 = trunc i32 %three to i1
  %r7 = call i64 @pick(i1 %t2, i64 4294967296, i64 -2) ; -2
  %r8 = call i64 @pick(i1 %t3, i64 4294967296, i64 -2) ; 0x100000000
  %x0 = xor i32 %r0, -10
  %x1 = xor i32 %r1, 10
  %x2 = xor i32 %r2, 3
  %x3 = xor i32 %r3, 50
  %x4 = xor i32 %r4, 50
  %x5 = xor i32 %r5, 2
  %x6 = xor i32 %r6, -20
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %o6 = or i32 %o5, %x6
  %y7 = xor i64 %r7, -2
  %y8 = xor i64 %r8, 4294967296
  %p = or i64 %y7, %y8
  %ow = zext i32 %o6 to i64
  %all = or i64 %p, %ow
  %bad = icmp ne i64 %all, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define double @sel_f64(i1 zeroext %c, double %a, double %b) noinline {
  %s = select i1 %c, double %a, double %b
  ret double %s
}

define internal float @sel_f32(i32 %a, i32 %b, float %x, float %y) noinline {
  %c = icmp ult i32 %a, %b
  %s = select i1 %c, float %x, float %y
  ret float %s
}

define internal double @sel_f64_consts(i32 %a) noinline {
  %c = icmp eq i32 %a, 0
  %s = select i1 %c, double 1.5, double -0.0
  ret double %s
}

define internal i32 @fp_sink(double %d, float %f) noinline {
  ret i32 0
}

; Bit 7: float64, float32, native int and int32 selects on one condition; the
; integer results are checked, the FP ones flow into calls.
define internal i32 @check_mixed(i32 %a, i32 %b, double %d1, double %d2, float %f1, float %f2, i64 %l1, i64 %l2) noinline {
  ; a = 9, b = 4, l1 = 0x100000000 + 100, l2 = 0x100000000 + 200
  %c = icmp sgt i32 %a, %b
  %sd = select i1 %c, double %d1, double %d2
  %sf = select i1 %c, float %f1, float %f2
  %sl = select i1 %c, i64 %l1, i64 %l2               ; l1
  %si = select i1 %c, i32 %a, i32 %b                 ; 9
  %k0 = call i32 @fp_sink(double %sd, float %sf)     ; 0
  %e = call double @sel_f64(i1 zeroext %c, double %d2, double %sd)
  %g = call float @sel_f32(i32 %a, i32 %b, float %sf, float %f2)
  %h = call double @sel_f64_consts(i32 %b)
  %k1 = call i32 @fp_sink(double %e, float %g)       ; 0
  %k2 = call i32 @fp_sink(double %h, float 2.5)      ; 0
  %t = trunc i64 %sl to i32                          ; 100
  %u = lshr i64 %sl, 32                              ; 1
  %ut = trunc i64 %u to i32
  %x0 = xor i32 %t, 100
  %x1 = xor i32 %ut, 1
  %x2 = xor i32 %si, 9
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %k0
  %o4 = or i32 %o3, %k1
  %o5 = or i32 %o4, %k2
  %bad = icmp ne i32 %o5, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define i32 @main() {
  %f0 = call i32 @check_i32(i32 1, i32 2)
  %f1 = call i32 @check_i64(i64 4294967296, i64 4294967295, i64 4886718345, i64 -40926266145, i32 -1, i32 1)
  %f2 = call i32 @check_consts(i32 -5, i32 7)
  %f3 = call i32 @check_smin_smax(i32 -1, i32 1, i32 -2147483648, i32 2147483647, i32 5, i32 -7)
  %f4 = call i32 @check_umin_umax(i32 -1, i32 1, i32 -2147483648, i32 2147483647)
  %f5 = call i32 @check_minmax64(i64 -1, i64 4294967296, i64 4294967295, i64 -9223372036854775808, i64 9223372036854775807)
  %f6 = call i32 @check_chains(i32 -50, i32 50, i32 3, i32 2)
  %f7 = call i32 @check_mixed(i32 9, i32 4, double 1.25, double -3.5, float 0.5, float -8.0, i64 4294967396, i64 4294967496)
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
