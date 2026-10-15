; Symbol differences in global initializers (Milestone 8, D12): an
; initializer 'sub (ptrtoint @x, ptrtoint @y)' between two globals of the data
; segment is folded into the data image, because the backend lays out the
; whole segment itself. Covered: 64-bit differences in both directions, a
; difference with an offset truncated to 32 bits, and a table of 32-bit
; offsets relative to the table itself (the shape of LLVM's relative lookup
; tables). main checks that every stored difference equals the distance
; between the addresses the code sees through the address fields, and reads
; the strings found through the relative table. C has no constant expression
; for a difference of two addresses, so the test is IR. Exit status: a
; bitmask of failed checks (D37), 0 when all pass.
; RUN: %{msil_ll}

@a = global [4 x i32] [i32 1, i32 2, i32 3, i32 4], align 4
@b = global [4 x i32] [i32 5, i32 6, i32 7, i32 8], align 4
@s1 = global [6 x i8] c"first\00", align 1
@s2 = global [7 x i8] c"second\00", align 1

@d_ba = global i64 sub (i64 ptrtoint (ptr @b to i64), i64 ptrtoint (ptr @a to i64)), align 8
@d_ab = global i64 sub (i64 ptrtoint (ptr @a to i64), i64 ptrtoint (ptr @b to i64)), align 8
@d_b8a = global i32 trunc (i64 sub (i64 ptrtoint (ptr getelementptr (i8, ptr @b, i64 8) to i64), i64 ptrtoint (ptr @a to i64)) to i32), align 4
@rel = global [2 x i32] [
  i32 trunc (i64 sub (i64 ptrtoint (ptr @s1 to i64), i64 ptrtoint (ptr @rel to i64)) to i32),
  i32 trunc (i64 sub (i64 ptrtoint (ptr @s2 to i64), i64 ptrtoint (ptr getelementptr (i8, ptr @rel, i64 4) to i64)) to i32)
], align 4

; Sets bit %bit of the result when %ok is false.
define internal i32 @bit(i1 %ok, i32 %bit) noinline {
  %fail = xor i1 %ok, true
  %f = zext i1 %fail to i32
  %r = shl i32 %f, %bit
  ret i32 %r
}

define i32 @main() {
  %pa = ptrtoint ptr @a to i64
  %pb = ptrtoint ptr @b to i64

  ; bit 0: &a + (&b - &a) == &b
  %dba = load i64, ptr @d_ba, align 8
  %x0 = add i64 %pa, %dba
  %ok0 = icmp eq i64 %x0, %pb
  %r0 = call i32 @bit(i1 %ok0, i32 0)

  ; bit 1: &b + (&a - &b) == &a
  %dab = load i64, ptr @d_ab, align 8
  %x1 = add i64 %pb, %dab
  %ok1 = icmp eq i64 %x1, %pa
  %r1 = call i32 @bit(i1 %ok1, i32 1)

  ; bit 2: &a + sext(trunc(&b + 8 - &a)) == &b + 8
  %d32 = load i32, ptr @d_b8a, align 4
  %d32x = sext i32 %d32 to i64
  %x2 = add i64 %pa, %d32x
  %pb8 = add i64 %pb, 8
  %ok2 = icmp eq i64 %x2, %pb8
  %r2 = call i32 @bit(i1 %ok2, i32 2)

  ; bit 3: the two 64-bit differences are opposite
  %neg = sub i64 0, %dab
  %ok3 = icmp eq i64 %dba, %neg
  %r3 = call i32 @bit(i1 %ok3, i32 3)

  ; bit 4: &rel + rel[0] == &s1, and the string there starts with 'f'
  %prel = ptrtoint ptr @rel to i64
  %o0 = load i32, ptr @rel, align 4
  %o0x = sext i32 %o0 to i64
  %t0 = add i64 %prel, %o0x
  %ps1 = ptrtoint ptr @s1 to i64
  %ok4a = icmp eq i64 %t0, %ps1
  %p0 = inttoptr i64 %t0 to ptr
  %c0 = load i8, ptr %p0, align 1
  %ok4b = icmp eq i8 %c0, 102
  %ok4 = and i1 %ok4a, %ok4b
  %r4 = call i32 @bit(i1 %ok4, i32 4)

  ; bit 5: &rel[1] + rel[1] == &s2, and the string there ends in 'd'
  %prel1 = getelementptr i8, ptr @rel, i64 4
  %o1 = load i32, ptr %prel1, align 4
  %o1x = sext i32 %o1 to i64
  %prel1i = ptrtoint ptr %prel1 to i64
  %t1 = add i64 %prel1i, %o1x
  %ps2 = ptrtoint ptr @s2 to i64
  %ok5a = icmp eq i64 %t1, %ps2
  %p1 = inttoptr i64 %t1 to ptr
  %p1e = getelementptr i8, ptr %p1, i64 5
  %c1 = load i8, ptr %p1e, align 1
  %ok5b = icmp eq i8 %c1, 100
  %ok5 = and i1 %ok5a, %ok5b
  %r5 = call i32 @bit(i1 %ok5, i32 5)

  ; bit 6: the arrays the differences point to hold their initial values
  %pb2 = getelementptr i8, ptr @b, i64 8
  %v = load i32, ptr %pb2, align 4
  %ok6 = icmp eq i32 %v, 7
  %r6 = call i32 @bit(i1 %ok6, i32 6)

  %m01 = or i32 %r0, %r1
  %m23 = or i32 %r2, %r3
  %m45 = or i32 %r4, %r5
  %m03 = or i32 %m01, %m23
  %m46 = or i32 %m45, %r6
  %m = or i32 %m03, %m46
  ret i32 %m
}
