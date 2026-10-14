; Direct calls (Milestone 4): call chains that pass int32 and native int values
; down and results back up, dropped results (pop) and void calls, calls as
; arguments of calls, a result used twice, fastcc and coldcc callees, tail
; call markers (always plain calls), pointer arguments and results (native
; int), public and assembly callees, and an eight-deep chain. Each noinline
; check function returns 1 when a result differs from the value C gives (in
; the comments); main ORs one bit per check into the exit status (8 bits,
; D37): [exit 0] means every check passed.
; RUN: %{msil_ll}

define internal i64 @c4(i64 %v) noinline {
  %r = mul i64 %v, 3
  ret i64 %r
}

define i64 @c3(i32 %a, i64 %b) noinline {
  %w = sext i32 %a to i64
  %s = add i64 %w, %b
  %r = call i64 @c4(i64 %s)
  %t = sub i64 %r, 1
  ret i64 %t
}

define internal i32 @c2(i64 %v) noinline {
  %r = call i64 @c3(i32 -10, i64 %v)
  %h = lshr i64 %r, 32
  %l = trunc i64 %r to i32
  %ht = trunc i64 %h to i32
  %x = xor i32 %l, %ht
  ret i32 %x
}

define internal i32 @c1(i32 %x) noinline {
  %w = zext i32 %x to i64
  %b = or i64 %w, 4294967296
  %r = call i32 @c2(i64 %b)
  ret i32 %r
}

; Bit 0: main -> c1 -> c2 -> c3 -> c4 and back.
define internal i32 @check_chain(i32 %x) noinline {
  ; x = 0x10: c1 passes 0x100000010; c3 gets (-10, 0x100000010): s = 0x100000006,
  ; c4 gives 0x300000012, c3 returns 0x300000011, c2 returns 0x11 ^ 3 = 0x12
  %r = call i32 @c1(i32 %x)
  %bad = icmp ne i32 %r, 18
  %z = zext i1 %bad to i32
  ret i32 %z
}

define internal i32 @inc(i32 %x) noinline {
  %r = add i32 %x, 1
  ret i32 %r
}

define i64 @inc64(i64 %x) noinline {
  %r = add i64 %x, 4294967296
  ret i64 %r
}

define internal void @nothing() noinline {
  ret void
}

define internal void @consume(i32 %a, i64 %b) noinline {
  %u = add i32 %a, 1
  ret void
}

; Bit 1: results that are dropped (pop) and void calls between the
; computations that are checked.
define internal i32 @check_dropped(i32 %x, i64 %y) noinline {
  %d0 = call i32 @inc(i32 %x)
  call void @nothing()
  %a = call i32 @inc(i32 %x)                     ; x + 1
  %d1 = call i64 @inc64(i64 %y)
  call void @consume(i32 %a, i64 %y)
  %b = call i64 @inc64(i64 %y)                   ; y + 2^32
  call void @nothing()
  %d2 = call i32 @inc(i32 %a)
  %bt = trunc i64 %b to i32
  %bh = lshr i64 %b, 32
  %bht = trunc i64 %bh to i32
  ; x = 41, y = 0x200000007: a = 42, b = 0x300000007
  %x0 = xor i32 %a, 42
  %x1 = xor i32 %bt, 7
  %x2 = xor i32 %bht, 3
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %bad = icmp ne i32 %o2, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal i32 @sub3(i32 %a, i32 %b, i32 %c) noinline {
  %t = sub i32 %a, %b
  %r = sub i32 %t, %c
  ret i32 %r
}

define internal i32 @twice(i32 %x) noinline {
  %r = shl i32 %x, 1
  ret i32 %r
}

; Bit 2: calls as arguments of calls (argument order matters for sub3) and a
; call result used twice.
define internal i32 @check_nested(i32 %x) noinline {
  ; x = 5
  %r0 = call i32 @sub3(i32 %x, i32 %x, i32 %x)                    ; -5
  %t = call i32 @twice(i32 %x)                                    ; 10
  %i = call i32 @inc(i32 %x)                                      ; 6
  %r1 = call i32 @sub3(i32 %t, i32 %i, i32 %t)                    ; -6
  %r2 = call i32 @sub3(i32 %t, i32 %t, i32 %t)                    ; -10
  %u = call i32 @twice(i32 %x)
  %v = call i32 @inc(i32 %x)
  %r3 = call i32 @sub3(i32 %u, i32 1, i32 %v)                     ; 3
  %r4 = call i32 @sub3(i32 %r3, i32 %r1, i32 %r0)                 ; 14
  %x0 = xor i32 %r0, -5
  %x1 = xor i32 %r1, -6
  %x2 = xor i32 %r2, -10
  %x3 = xor i32 %r3, 3
  %x4 = xor i32 %r4, 14
  %o1 = or i32 %x0, %x1
  %o2 = or i32 %o1, %x2
  %o3 = or i32 %o2, %x3
  %o4 = or i32 %o3, %x4
  %bad = icmp ne i32 %o4, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal fastcc i64 @fast_mix(i32 %a, i64 %b) noinline {
  %w = sext i32 %a to i64
  %r = sub i64 %b, %w
  ret i64 %r
}

define internal coldcc i32 @cold_neg(i32 %a) noinline {
  %r = sub i32 0, %a
  ret i32 %r
}

; Bit 3: fastcc and coldcc callees.
define internal i32 @check_conv(i32 %a, i64 %b) noinline {
  ; a = -7, b = 0x100000000
  %r0 = call fastcc i64 @fast_mix(i32 %a, i64 %b)                ; 0x100000007
  %r1 = call coldcc i32 @cold_neg(i32 %a)                        ; 7
  %y0 = xor i64 %r0, 4294967303
  %x1 = xor i32 %r1, 7
  %y1 = zext i32 %x1 to i64
  %o = or i64 %y0, %y1
  %bad = icmp ne i64 %o, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal i32 @tail_inc(i32 %x) noinline {
  %r = tail call i32 @inc(i32 %x)
  ret i32 %r
}

define internal i64 @tail_twice(i64 %x) noinline {
  %t = trunc i64 %x to i32
  %r = notail call i32 @twice(i32 %t)
  %w = sext i32 %r to i64
  %s = tail call i64 @inc64(i64 %w)
  ret i64 %s
}

; Bit 4: tail and notail call markers.
define internal i32 @check_tail(i32 %x, i64 %y) noinline {
  ; x = 99, y = -4
  %r0 = call i32 @tail_inc(i32 %x)                               ; 100
  %r1 = call i64 @tail_twice(i64 %y)                             ; 2^32 - 8
  %x0 = xor i32 %r0, 100
  %y0 = zext i32 %x0 to i64
  %y1 = xor i64 %r1, 4294967288
  %o = or i64 %y0, %y1
  %bad = icmp ne i64 %o, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal ptr @ptr_add(ptr %p, i64 %off) noinline {
  %q = getelementptr i8, ptr %p, i64 %off
  ret ptr %q
}

define i64 @ptr_diff(ptr %a, ptr %b) noinline {
  %ai = ptrtoint ptr %a to i64
  %bi = ptrtoint ptr %b to i64
  %d = sub i64 %ai, %bi
  ret i64 %d
}

define internal i32 @ptr_lt(ptr %a, ptr %b) noinline {
  %c = icmp ult ptr %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Bit 5: pointers are native int arguments and results (no memory access).
define internal i32 @check_ptrs(i64 %base) noinline {
  ; base = 0x123400000000
  %p = inttoptr i64 %base to ptr
  %q = call ptr @ptr_add(ptr %p, i64 4294967304)                 ; base + 0x100000008
  %r = call ptr @ptr_add(ptr %q, i64 -16)                        ; base + 0xFFFFFFF8
  %d0 = call i64 @ptr_diff(ptr %q, ptr %p)                       ; 0x100000008
  %d1 = call i64 @ptr_diff(ptr %r, ptr %q)                       ; -16
  %l0 = call i32 @ptr_lt(ptr %p, ptr %r)                         ; 1
  %l1 = call i32 @ptr_lt(ptr %q, ptr %r)                         ; 0
  %ri = ptrtoint ptr %r to i64
  %y0 = xor i64 %d0, 4294967304
  %y1 = xor i64 %d1, -16
  %y2 = xor i64 %ri, 20018842566648
  %x3 = xor i32 %l0, 1
  %x4 = or i32 %x3, %l1
  %y3 = zext i32 %x4 to i64
  %o1 = or i64 %y0, %y1
  %o2 = or i64 %o1, %y2
  %o3 = or i64 %o2, %y3
  %bad = icmp ne i64 %o3, 0
  %z = zext i1 %bad to i32
  ret i32 %z
}

define i32 @seven() noinline {
  ret i32 7
}

define internal i64 @big() noinline {
  ret i64 -81985529216486896
}

define i32 @mix_pub(i32 %a, i64 %b) noinline {
  %t = trunc i64 %b to i32
  %r = xor i32 %a, %t
  ret i32 %r
}

; Bit 6: public and assembly callees without parameters, mixed in one
; expression.
define internal i32 @check_mixed() noinline {
  %s = call i32 @seven()                                         ; 7
  %b = call i64 @big()                                           ; 0xFEDCBA9876543210
  %m = call i32 @mix_pub(i32 %s, i64 %b)                         ; 0x76543217
  %n = call i32 @mix_pub(i32 %m, i64 %b)                         ; 7
  %x0 = xor i32 %m, 1985229335
  %x1 = xor i32 %n, 7
  %o = or i32 %x0, %x1
  %bad = icmp ne i32 %o, 0
  %r = zext i1 %bad to i32
  ret i32 %r
}

define internal i64 @d1(i64 %v) noinline {
  %r = add i64 %v, 1
  ret i64 %r
}

define internal i64 @d2(i64 %v) noinline {
  %a = shl i64 %v, 1
  %r = call i64 @d1(i64 %a)
  ret i64 %r
}

define internal i64 @d3(i64 %v) noinline {
  %a = add i64 %v, 3
  %r = call i64 @d2(i64 %a)
  ret i64 %r
}

define internal i64 @d4(i64 %v) noinline {
  %a = shl i64 %v, 1
  %r = call i64 @d3(i64 %a)
  ret i64 %r
}

define internal i64 @d5(i64 %v) noinline {
  %a = add i64 %v, 5
  %r = call i64 @d4(i64 %a)
  ret i64 %r
}

define internal i64 @d6(i64 %v) noinline {
  %a = shl i64 %v, 1
  %r = call i64 @d5(i64 %a)
  ret i64 %r
}

define internal i64 @d7(i64 %v) noinline {
  %a = add i64 %v, 7
  %r = call i64 @d6(i64 %a)
  ret i64 %r
}

define internal i64 @d8(i64 %v) noinline {
  %a = shl i64 %v, 1
  %r = call i64 @d7(i64 %a)
  %s = add i64 %r, %v
  ret i64 %s
}

; Bit 7: an eight-deep call chain.
define internal i32 @check_deep(i64 %v) noinline {
  ; v = 0x40000000: d8..d1 give ((((2v + 7) * 2 + 5) * 2 + 3) * 2 + 1) + v
  ;   = 17v + 83 = 0x440000053
  %r = call i64 @d8(i64 %v)
  %bad = icmp ne i64 %r, 18253611091
  %z = zext i1 %bad to i32
  ret i32 %z
}

define i32 @main() {
  %f0 = call i32 @check_chain(i32 16)
  %f1 = call i32 @check_dropped(i32 41, i64 8589934599)
  %f2 = call i32 @check_nested(i32 5)
  %f3 = call i32 @check_conv(i32 -7, i64 4294967296)
  %f4 = call i32 @check_tail(i32 99, i64 -4)
  %f5 = call i32 @check_ptrs(i64 20014547599360)
  %f6 = call i32 @check_mixed()
  %f7 = call i32 @check_deep(i64 1073741824)
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
