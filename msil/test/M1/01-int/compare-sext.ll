; Integer compares as 0/-1 values (Milestone 4): sext of the negated predicates
; ne, sle, sge, ule and uge, which ISel builds from two single-opcode compares
; ('ceq; ldc.i4.0; ceq' and the like, isel.md decision 2), on int32 and native
; int operands, widened (int32 compare to native int), narrowed (native int
; compare to int32) and used as masks. sext of a compare stays arithmetic (D34),
; so the -1 must come out of the compare itself. The helpers are noinline and
; return the value, so llc can neither fold them nor merge the sext into the
; caller's comparison even at -O2; the check functions compare each result with
; the value C gives (-(a OP b), in the comments) and return 1 when any differs.
; The int64 pairs include values that differ only in the high half and values
; whose signed and unsigned orders differ. main ORs one bit per check function
; into the exit status (8 bits, D37), so [exit 0] means every check passed and
; bit N names check N.
; RUN: %{msil_ll}

; -(a OP b) for int32 a and b, as int32.
define internal i32 @ne_i32(i32 %a, i32 %b) noinline {
  %c = icmp ne i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

define internal i32 @sle_i32(i32 %a, i32 %b) noinline {
  %c = icmp sle i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

define internal i32 @sge_i32(i32 %a, i32 %b) noinline {
  %c = icmp sge i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

define internal i32 @ule_i32(i32 %a, i32 %b) noinline {
  %c = icmp ule i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

define internal i32 @uge_i32(i32 %a, i32 %b) noinline {
  %c = icmp uge i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; -(a OP b) for int64 a and b, as int64.
define internal i64 @ne_i64(i64 %a, i64 %b) noinline {
  %c = icmp ne i64 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

define internal i64 @sle_i64(i64 %a, i64 %b) noinline {
  %c = icmp sle i64 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

define internal i64 @sge_i64(i64 %a, i64 %b) noinline {
  %c = icmp sge i64 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

define internal i64 @ule_i64(i64 %a, i64 %b) noinline {
  %c = icmp ule i64 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

define internal i64 @uge_i64(i64 %a, i64 %b) noinline {
  %c = icmp uge i64 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

; -(a OP b) for int32 a and b, as int64: the -1 must widen to all ones.
define internal i64 @ne_i32_to_i64(i32 %a, i32 %b) noinline {
  %c = icmp ne i32 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

define internal i64 @sle_i32_to_i64(i32 %a, i32 %b) noinline {
  %c = icmp sle i32 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

define internal i64 @sge_i32_to_i64(i32 %a, i32 %b) noinline {
  %c = icmp sge i32 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

define internal i64 @ule_i32_to_i64(i32 %a, i32 %b) noinline {
  %c = icmp ule i32 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

define internal i64 @uge_i32_to_i64(i32 %a, i32 %b) noinline {
  %c = icmp uge i32 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

; -(a OP b) for int64 a and b, as int32.
define internal i32 @ne_i64_to_i32(i64 %a, i64 %b) noinline {
  %c = icmp ne i64 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

define internal i32 @sle_i64_to_i32(i64 %a, i64 %b) noinline {
  %c = icmp sle i64 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

define internal i32 @sge_i64_to_i32(i64 %a, i64 %b) noinline {
  %c = icmp sge i64 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

define internal i32 @ule_i64_to_i32(i64 %a, i64 %b) noinline {
  %c = icmp ule i64 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

define internal i32 @uge_i64_to_i32(i64 %a, i64 %b) noinline {
  %c = icmp uge i64 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; x & -(a != b): x when a != b, else 0.
define internal i32 @and_ne(i32 %a, i32 %b, i32 %x) noinline {
  %c = icmp ne i32 %a, %b
  %m = sext i1 %c to i32
  %r = and i32 %m, %x
  ret i32 %r
}

; (x ^ m) - m with m = -(a >= b): -x when a >= b, else x.
define internal i32 @neg_sge(i32 %a, i32 %b, i32 %x) noinline {
  %c = icmp sge i32 %a, %b
  %m = sext i1 %c to i32
  %t = xor i32 %x, %m
  %r = sub i32 %t, %m
  ret i32 %r
}

; y + -(a <=u b): y - 1 when a <=u b, else y.
define internal i64 @add_ule(i64 %a, i64 %b, i64 %y) noinline {
  %c = icmp ule i64 %a, %b
  %m = sext i1 %c to i64
  %r = add i64 %y, %m
  ret i64 %r
}

; y | -(a >=u b): -1 when a >=u b, else y.
define internal i64 @or_uge(i64 %a, i64 %b, i64 %y) noinline {
  %c = icmp uge i64 %a, %b
  %m = sext i1 %c to i64
  %r = or i64 %y, %m
  ret i64 %r
}

; y & ~-(a <= b): 0 when a <= b, else y.
define internal i64 @andn_sle(i64 %a, i64 %b, i64 %y) noinline {
  %c = icmp sle i64 %a, %b
  %m = sext i1 %c to i64
  %n = xor i64 %m, -1
  %r = and i64 %n, %y
  ret i64 %r
}

; Bit 0: -(a != b), icmp ne, on int32 and int64.
define internal i32 @check_ne() noinline {
  %v0 = call i32 @ne_i32(i32 -1, i32 1)  ; -1
  %x0 = xor i32 %v0, -1
  %v1 = call i32 @ne_i32(i32 5, i32 5)  ; 0
  %x1 = xor i32 %v1, 0
  %v2 = call i32 @ne_i32(i32 -2147483648, i32 2147483647)  ; -1
  %x2 = xor i32 %v2, -1
  %v3 = call i32 @ne_i32(i32 2147483647, i32 -2147483648)  ; -1
  %x3 = xor i32 %v3, -1
  %v4 = call i64 @ne_i64(i64 -1, i64 1)  ; -1
  %x4 = xor i64 %v4, -1
  %v5 = call i64 @ne_i64(i64 5, i64 5)  ; 0
  %x5 = xor i64 %v5, 0
  %v6 = call i64 @ne_i64(i64 -9223372036854775808, i64 9223372036854775807)  ; -1
  %x6 = xor i64 %v6, -1
  %v7 = call i64 @ne_i64(i64 4294967301, i64 5)  ; -1
  %x7 = xor i64 %v7, -1
  %v8 = call i64 @ne_i64(i64 5, i64 4294967301)  ; -1
  %x8 = xor i64 %v8, -1
  %v9 = call i64 @ne_i64(i64 -4294967296, i64 4294967295)  ; -1
  %x9 = xor i64 %v9, -1
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %bad32 = icmp ne i32 %o3, 0
  %p1 = or i64 %x4, %x5
  %p2 = or i64 %p1, %x6
  %p3 = or i64 %p2, %x7
  %p4 = or i64 %p3, %x8
  %p5 = or i64 %p4, %x9
  %bad64 = icmp ne i64 %p5, 0
  %bad = or i1 %bad32, %bad64
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 1: -(a <= b), icmp sle, on int32 and int64.
define internal i32 @check_sle() noinline {
  %v0 = call i32 @sle_i32(i32 -1, i32 1)  ; -1
  %x0 = xor i32 %v0, -1
  %v1 = call i32 @sle_i32(i32 5, i32 5)  ; -1
  %x1 = xor i32 %v1, -1
  %v2 = call i32 @sle_i32(i32 -2147483648, i32 2147483647)  ; -1
  %x2 = xor i32 %v2, -1
  %v3 = call i32 @sle_i32(i32 2147483647, i32 -2147483648)  ; 0
  %x3 = xor i32 %v3, 0
  %v4 = call i64 @sle_i64(i64 -1, i64 1)  ; -1
  %x4 = xor i64 %v4, -1
  %v5 = call i64 @sle_i64(i64 5, i64 5)  ; -1
  %x5 = xor i64 %v5, -1
  %v6 = call i64 @sle_i64(i64 -9223372036854775808, i64 9223372036854775807)  ; -1
  %x6 = xor i64 %v6, -1
  %v7 = call i64 @sle_i64(i64 4294967301, i64 5)  ; 0
  %x7 = xor i64 %v7, 0
  %v8 = call i64 @sle_i64(i64 5, i64 4294967301)  ; -1
  %x8 = xor i64 %v8, -1
  %v9 = call i64 @sle_i64(i64 -4294967296, i64 4294967295)  ; -1
  %x9 = xor i64 %v9, -1
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %bad32 = icmp ne i32 %o3, 0
  %p1 = or i64 %x4, %x5
  %p2 = or i64 %p1, %x6
  %p3 = or i64 %p2, %x7
  %p4 = or i64 %p3, %x8
  %p5 = or i64 %p4, %x9
  %bad64 = icmp ne i64 %p5, 0
  %bad = or i1 %bad32, %bad64
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 2: -(a >= b), icmp sge, on int32 and int64.
define internal i32 @check_sge() noinline {
  %v0 = call i32 @sge_i32(i32 -1, i32 1)  ; 0
  %x0 = xor i32 %v0, 0
  %v1 = call i32 @sge_i32(i32 5, i32 5)  ; -1
  %x1 = xor i32 %v1, -1
  %v2 = call i32 @sge_i32(i32 -2147483648, i32 2147483647)  ; 0
  %x2 = xor i32 %v2, 0
  %v3 = call i32 @sge_i32(i32 2147483647, i32 -2147483648)  ; -1
  %x3 = xor i32 %v3, -1
  %v4 = call i64 @sge_i64(i64 -1, i64 1)  ; 0
  %x4 = xor i64 %v4, 0
  %v5 = call i64 @sge_i64(i64 5, i64 5)  ; -1
  %x5 = xor i64 %v5, -1
  %v6 = call i64 @sge_i64(i64 -9223372036854775808, i64 9223372036854775807)  ; 0
  %x6 = xor i64 %v6, 0
  %v7 = call i64 @sge_i64(i64 4294967301, i64 5)  ; -1
  %x7 = xor i64 %v7, -1
  %v8 = call i64 @sge_i64(i64 5, i64 4294967301)  ; 0
  %x8 = xor i64 %v8, 0
  %v9 = call i64 @sge_i64(i64 -4294967296, i64 4294967295)  ; 0
  %x9 = xor i64 %v9, 0
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %bad32 = icmp ne i32 %o3, 0
  %p1 = or i64 %x4, %x5
  %p2 = or i64 %p1, %x6
  %p3 = or i64 %p2, %x7
  %p4 = or i64 %p3, %x8
  %p5 = or i64 %p4, %x9
  %bad64 = icmp ne i64 %p5, 0
  %bad = or i1 %bad32, %bad64
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 3: -(a <=u b), icmp ule, on int32 and int64.
define internal i32 @check_ule() noinline {
  %v0 = call i32 @ule_i32(i32 -1, i32 1)  ; 0
  %x0 = xor i32 %v0, 0
  %v1 = call i32 @ule_i32(i32 5, i32 5)  ; -1
  %x1 = xor i32 %v1, -1
  %v2 = call i32 @ule_i32(i32 -2147483648, i32 2147483647)  ; 0
  %x2 = xor i32 %v2, 0
  %v3 = call i32 @ule_i32(i32 2147483647, i32 -2147483648)  ; -1
  %x3 = xor i32 %v3, -1
  %v4 = call i64 @ule_i64(i64 -1, i64 1)  ; 0
  %x4 = xor i64 %v4, 0
  %v5 = call i64 @ule_i64(i64 5, i64 5)  ; -1
  %x5 = xor i64 %v5, -1
  %v6 = call i64 @ule_i64(i64 -9223372036854775808, i64 9223372036854775807)  ; 0
  %x6 = xor i64 %v6, 0
  %v7 = call i64 @ule_i64(i64 4294967301, i64 5)  ; 0
  %x7 = xor i64 %v7, 0
  %v8 = call i64 @ule_i64(i64 5, i64 4294967301)  ; -1
  %x8 = xor i64 %v8, -1
  %v9 = call i64 @ule_i64(i64 -4294967296, i64 4294967295)  ; 0
  %x9 = xor i64 %v9, 0
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %bad32 = icmp ne i32 %o3, 0
  %p1 = or i64 %x4, %x5
  %p2 = or i64 %p1, %x6
  %p3 = or i64 %p2, %x7
  %p4 = or i64 %p3, %x8
  %p5 = or i64 %p4, %x9
  %bad64 = icmp ne i64 %p5, 0
  %bad = or i1 %bad32, %bad64
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 4: -(a >=u b), icmp uge, on int32 and int64.
define internal i32 @check_uge() noinline {
  %v0 = call i32 @uge_i32(i32 -1, i32 1)  ; -1
  %x0 = xor i32 %v0, -1
  %v1 = call i32 @uge_i32(i32 5, i32 5)  ; -1
  %x1 = xor i32 %v1, -1
  %v2 = call i32 @uge_i32(i32 -2147483648, i32 2147483647)  ; -1
  %x2 = xor i32 %v2, -1
  %v3 = call i32 @uge_i32(i32 2147483647, i32 -2147483648)  ; 0
  %x3 = xor i32 %v3, 0
  %v4 = call i64 @uge_i64(i64 -1, i64 1)  ; -1
  %x4 = xor i64 %v4, -1
  %v5 = call i64 @uge_i64(i64 5, i64 5)  ; -1
  %x5 = xor i64 %v5, -1
  %v6 = call i64 @uge_i64(i64 -9223372036854775808, i64 9223372036854775807)  ; -1
  %x6 = xor i64 %v6, -1
  %v7 = call i64 @uge_i64(i64 4294967301, i64 5)  ; -1
  %x7 = xor i64 %v7, -1
  %v8 = call i64 @uge_i64(i64 5, i64 4294967301)  ; 0
  %x8 = xor i64 %v8, 0
  %v9 = call i64 @uge_i64(i64 -4294967296, i64 4294967295)  ; -1
  %x9 = xor i64 %v9, -1
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %bad32 = icmp ne i32 %o3, 0
  %p1 = or i64 %x4, %x5
  %p2 = or i64 %p1, %x6
  %p3 = or i64 %p2, %x7
  %p4 = or i64 %p3, %x8
  %p5 = or i64 %p4, %x9
  %bad64 = icmp ne i64 %p5, 0
  %bad = or i1 %bad32, %bad64
  %r = zext i1 %bad to i32
  ret i32 %r
}

; Bit 5: int32 compares sign-extended to int64.
define internal i32 @check_widen() noinline {
  %v0 = call i64 @ne_i32_to_i64(i32 -1, i32 1)  ; -1
  %x0 = xor i64 %v0, -1
  %v1 = call i64 @ne_i32_to_i64(i32 5, i32 5)  ; 0
  %x1 = xor i64 %v1, 0
  %v2 = call i64 @ne_i32_to_i64(i32 2147483647, i32 -2147483648)  ; -1
  %x2 = xor i64 %v2, -1
  %v3 = call i64 @sle_i32_to_i64(i32 -1, i32 1)  ; -1
  %x3 = xor i64 %v3, -1
  %v4 = call i64 @sle_i32_to_i64(i32 5, i32 5)  ; -1
  %x4 = xor i64 %v4, -1
  %v5 = call i64 @sle_i32_to_i64(i32 2147483647, i32 -2147483648)  ; 0
  %x5 = xor i64 %v5, 0
  %v6 = call i64 @sge_i32_to_i64(i32 -1, i32 1)  ; 0
  %x6 = xor i64 %v6, 0
  %v7 = call i64 @sge_i32_to_i64(i32 5, i32 5)  ; -1
  %x7 = xor i64 %v7, -1
  %v8 = call i64 @sge_i32_to_i64(i32 2147483647, i32 -2147483648)  ; -1
  %x8 = xor i64 %v8, -1
  %v9 = call i64 @ule_i32_to_i64(i32 -1, i32 1)  ; 0
  %x9 = xor i64 %v9, 0
  %v10 = call i64 @ule_i32_to_i64(i32 5, i32 5)  ; -1
  %x10 = xor i64 %v10, -1
  %v11 = call i64 @ule_i32_to_i64(i32 2147483647, i32 -2147483648)  ; -1
  %x11 = xor i64 %v11, -1
  %v12 = call i64 @uge_i32_to_i64(i32 -1, i32 1)  ; -1
  %x12 = xor i64 %v12, -1
  %v13 = call i64 @uge_i32_to_i64(i32 5, i32 5)  ; -1
  %x13 = xor i64 %v13, -1
  %v14 = call i64 @uge_i32_to_i64(i32 2147483647, i32 -2147483648)  ; 0
  %x14 = xor i64 %v14, 0
  %p1 = or i64 %x0, %x1
  %p2 = or i64 %p1, %x2
  %p3 = or i64 %p2, %x3
  %p4 = or i64 %p3, %x4
  %p5 = or i64 %p4, %x5
  %p6 = or i64 %p5, %x6
  %p7 = or i64 %p6, %x7
  %p8 = or i64 %p7, %x8
  %p9 = or i64 %p8, %x9
  %p10 = or i64 %p9, %x10
  %p11 = or i64 %p10, %x11
  %p12 = or i64 %p11, %x12
  %p13 = or i64 %p12, %x13
  %p14 = or i64 %p13, %x14
  %bad64 = icmp ne i64 %p14, 0
  %r = zext i1 %bad64 to i32
  ret i32 %r
}

; Bit 6: int64 compares sign-extended to int32.
define internal i32 @check_narrow() noinline {
  %v0 = call i32 @ne_i64_to_i32(i64 5, i64 5)  ; 0
  %x0 = xor i32 %v0, 0
  %v1 = call i32 @ne_i64_to_i32(i64 4294967301, i64 5)  ; -1
  %x1 = xor i32 %v1, -1
  %v2 = call i32 @ne_i64_to_i32(i64 5, i64 4294967301)  ; -1
  %x2 = xor i32 %v2, -1
  %v3 = call i32 @ne_i64_to_i32(i64 -4294967296, i64 4294967295)  ; -1
  %x3 = xor i32 %v3, -1
  %v4 = call i32 @sle_i64_to_i32(i64 5, i64 5)  ; -1
  %x4 = xor i32 %v4, -1
  %v5 = call i32 @sle_i64_to_i32(i64 4294967301, i64 5)  ; 0
  %x5 = xor i32 %v5, 0
  %v6 = call i32 @sle_i64_to_i32(i64 5, i64 4294967301)  ; -1
  %x6 = xor i32 %v6, -1
  %v7 = call i32 @sle_i64_to_i32(i64 -4294967296, i64 4294967295)  ; -1
  %x7 = xor i32 %v7, -1
  %v8 = call i32 @sge_i64_to_i32(i64 5, i64 5)  ; -1
  %x8 = xor i32 %v8, -1
  %v9 = call i32 @sge_i64_to_i32(i64 4294967301, i64 5)  ; -1
  %x9 = xor i32 %v9, -1
  %v10 = call i32 @sge_i64_to_i32(i64 5, i64 4294967301)  ; 0
  %x10 = xor i32 %v10, 0
  %v11 = call i32 @sge_i64_to_i32(i64 -4294967296, i64 4294967295)  ; 0
  %x11 = xor i32 %v11, 0
  %v12 = call i32 @ule_i64_to_i32(i64 5, i64 5)  ; -1
  %x12 = xor i32 %v12, -1
  %v13 = call i32 @ule_i64_to_i32(i64 4294967301, i64 5)  ; 0
  %x13 = xor i32 %v13, 0
  %v14 = call i32 @ule_i64_to_i32(i64 5, i64 4294967301)  ; -1
  %x14 = xor i32 %v14, -1
  %v15 = call i32 @ule_i64_to_i32(i64 -4294967296, i64 4294967295)  ; 0
  %x15 = xor i32 %v15, 0
  %v16 = call i32 @uge_i64_to_i32(i64 5, i64 5)  ; -1
  %x16 = xor i32 %v16, -1
  %v17 = call i32 @uge_i64_to_i32(i64 4294967301, i64 5)  ; -1
  %x17 = xor i32 %v17, -1
  %v18 = call i32 @uge_i64_to_i32(i64 5, i64 4294967301)  ; 0
  %x18 = xor i32 %v18, 0
  %v19 = call i32 @uge_i64_to_i32(i64 -4294967296, i64 4294967295)  ; -1
  %x19 = xor i32 %v19, -1
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %o5 = or i32 %o4, %x5
  %o6 = or i32 %o5, %x6
  %o7 = or i32 %o6, %x7
  %o8 = or i32 %o7, %x8
  %o9 = or i32 %o8, %x9
  %o10 = or i32 %o9, %x10
  %o11 = or i32 %o10, %x11
  %o12 = or i32 %o11, %x12
  %o13 = or i32 %o12, %x13
  %o14 = or i32 %o13, %x14
  %o15 = or i32 %o14, %x15
  %o16 = or i32 %o15, %x16
  %o17 = or i32 %o16, %x17
  %o18 = or i32 %o17, %x18
  %o19 = or i32 %o18, %x19
  %bad32 = icmp ne i32 %o19, 0
  %r = zext i1 %bad32 to i32
  ret i32 %r
}

; Bit 7: the 0/-1 values as masks; x = 0x12345678, y = 0x123456789abcdef0.
define internal i32 @check_masks() noinline {
  %v0 = call i32 @and_ne(i32 3, i32 4, i32 305419896)  ; x
  %x0 = xor i32 %v0, 305419896
  %v1 = call i32 @and_ne(i32 4, i32 4, i32 305419896)  ; 0
  %x1 = xor i32 %v1, 0
  %v2 = call i32 @neg_sge(i32 -1, i32 1, i32 305419896)  ; x
  %x2 = xor i32 %v2, 305419896
  %v3 = call i32 @neg_sge(i32 7, i32 7, i32 305419896)  ; -x
  %x3 = xor i32 %v3, -305419896
  %v4 = call i32 @neg_sge(i32 1, i32 -1, i32 305419896)  ; -x
  %x4 = xor i32 %v4, -305419896
  %v5 = call i64 @add_ule(i64 1, i64 -1, i64 1311768467463790320)  ; y - 1
  %x5 = xor i64 %v5, 1311768467463790319
  %v6 = call i64 @add_ule(i64 -1, i64 1, i64 1311768467463790320)  ; y
  %x6 = xor i64 %v6, 1311768467463790320
  %v7 = call i64 @or_uge(i64 1, i64 -1, i64 1311768467463790320)  ; y
  %x7 = xor i64 %v7, 1311768467463790320
  %v8 = call i64 @or_uge(i64 -1, i64 1, i64 1311768467463790320)  ; -1
  %x8 = xor i64 %v8, -1
  %v9 = call i64 @andn_sle(i64 -1, i64 1, i64 1311768467463790320)  ; 0
  %x9 = xor i64 %v9, 0
  %v10 = call i64 @andn_sle(i64 1, i64 -1, i64 1311768467463790320)  ; y
  %x10 = xor i64 %v10, 1311768467463790320
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %bad32 = icmp ne i32 %o4, 0
  %p1 = or i64 %x5, %x6
  %p2 = or i64 %p1, %x7
  %p3 = or i64 %p2, %x8
  %p4 = or i64 %p3, %x9
  %p5 = or i64 %p4, %x10
  %bad64 = icmp ne i64 %p5, 0
  %bad = or i1 %bad32, %bad64
  %r = zext i1 %bad to i32
  ret i32 %r
}

define i32 @main() {
  %c0 = call i32 @check_ne()
  %c1 = call i32 @check_sle()
  %c2 = call i32 @check_sge()
  %c3 = call i32 @check_ule()
  %c4 = call i32 @check_uge()
  %c5 = call i32 @check_widen()
  %c6 = call i32 @check_narrow()
  %c7 = call i32 @check_masks()
  %s1 = shl i32 %c1, 1
  %s2 = shl i32 %c2, 2
  %s3 = shl i32 %c3, 3
  %s4 = shl i32 %c4, 4
  %s5 = shl i32 %c5, 5
  %s6 = shl i32 %c6, 6
  %s7 = shl i32 %c7, 7
  %m1 = or i32 %c0, %s1
  %m2 = or i32 %m1, %s2
  %m3 = or i32 %m2, %s3
  %m4 = or i32 %m3, %s4
  %m5 = or i32 %m4, %s5
  %m6 = or i32 %m5, %s6
  %m7 = or i32 %m6, %s7
  ret i32 %m7
}
