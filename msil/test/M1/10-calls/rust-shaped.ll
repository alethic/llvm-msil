; The shape of rustc's IR (Milestone 12, backlog 2.1-2.3 and recon
; backlog-files/alias-rust/merge.ll, rustc 1.99.0 -O with its default
; -Z merge-functions=aliases), without a datalayout, a triple or the Rust
; runtime (the div-by-zero panic is an internal trap): an 'unnamed_addr alias'
; that rustc makes of two identical #[no_mangle] functions (@sum_b of @sum_a),
; called directly, through a function pointer in a global initializer and
; under its own name, which keeps a forwarding method; black_box's
; 'asm sideeffect "", "r,~{memory}"' on stack slots, which MSILPrepareIR
; lowers to the D36 fence, llvm.fake.use and llvm.sideeffect; an i128 udiv
; with a zero check (expanded inline since M12, no __udivti3) and
; llvm.fptosi.sat.i128.f64 (expanded inline, no __fixdfti), both returned as
; <16 x i8> as rustc returns u128; and the rest of merge.ll's module: an
; internal fastcc function with llvm.fshl, lifetime markers, and a vectorized
; loop with llvm.vector.reduce.add. The saturating conversion is checked in
; range, above and below the range, on infinity and on NaN (0). main prints
; every result as 16 hex digits per 64-bit half and returns a 7-bit checksum.
; The native oracle builds the same file (compiler-rt or libgcc_s provide the
; helpers there).
; RUN: %{msil_ll}

@sum_b = unnamed_addr alias i32 (ptr, i64), ptr @sum_a

; The alias in an initializer.
@sum_fn = internal global ptr @sum_b, align 8

@nums = private unnamed_addr constant [11 x i32] [i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11], align 4

; merge::priv_a
define internal fastcc noundef i64 @priv_a(i64 noundef %x) unnamed_addr #1 {
start:
  %_3 = mul i64 %x, -7046029254386353131
  %_0.i = tail call noundef i64 @llvm.fshl.i64(i64 %_3, i64 %_3, i64 17)
  %_0 = xor i64 %_0.i, 4660
  ret i64 %_0
}

; core::panicking::panic_const::panic_const_div_by_zero, without the runtime.
define internal void @panic_div_by_zero(ptr %loc) unnamed_addr #7 {
start:
  call void @llvm.trap()
  unreachable
}

define noundef <16 x i8> @div128(ptr noalias nofree noundef readonly align 16 captures(none) dereferenceable(16) %a, ptr noalias nofree noundef readonly align 16 captures(none) dereferenceable(16) %b) unnamed_addr #2 {
start:
  %0 = load i128, ptr %b, align 16, !noundef !1
  %_3 = icmp eq i128 %0, 0
  br i1 %_3, label %panic, label %bb1

bb1:                                              ; preds = %start
  %1 = load i128, ptr %a, align 16, !noundef !1
  %2 = udiv i128 %1, %0
  %3 = bitcast i128 %2 to <16 x i8>
  ret <16 x i8> %3

panic:                                            ; preds = %start
  tail call void @panic_div_by_zero(ptr null) #10
  unreachable
}

define noundef i64 @entry(i64 noundef %x) unnamed_addr #2 {
start:
  %_16 = alloca [8 x i8], align 8
  %v = alloca [16 x i8], align 4
  %_5 = alloca [8 x i8], align 8
  %_3 = alloca [8 x i8], align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %_3)
  store i64 %x, ptr %_3, align 8
  call void asm sideeffect "", "r,~{memory}"(ptr nonnull %_3) #11, !srcloc !0
  %0 = load i64, ptr %_3, align 8, !noundef !1
  %a = call fastcc noundef i64 @priv_a(i64 noundef %0) #12
  call void @llvm.lifetime.end.p0(ptr nonnull %_3)
  call void @llvm.lifetime.start.p0(ptr nonnull %_5)
  %_6 = add i64 %x, 1
  store i64 %_6, ptr %_5, align 8
  call void asm sideeffect "", "r,~{memory}"(ptr nonnull %_5) #11, !srcloc !0
  %1 = load i64, ptr %_5, align 8, !noundef !1
  %b = call fastcc noundef i64 @priv_a(i64 noundef %1) #12
  call void @llvm.lifetime.end.p0(ptr nonnull %_5)
  call void @llvm.lifetime.start.p0(ptr nonnull %v)
  store i32 1, ptr %v, align 4
  %2 = getelementptr inbounds nuw i8, ptr %v, i64 4
  store i32 2, ptr %2, align 4
  %3 = getelementptr inbounds nuw i8, ptr %v, i64 8
  store i32 3, ptr %3, align 4
  %4 = getelementptr inbounds nuw i8, ptr %v, i64 12
  store i32 4, ptr %4, align 4
  %_10 = call noundef i32 @sum_a(ptr noundef nonnull %v, i64 noundef 4) #12
  %_9 = zext i32 %_10 to i64
  call void @llvm.lifetime.start.p0(ptr nonnull %_16)
  store ptr %v, ptr %_16, align 8
  call void asm sideeffect "", "r,~{memory}"(ptr nonnull %_16) #11, !srcloc !0
  %5 = load ptr, ptr %_16, align 8, !noundef !1
  %_15 = call noundef i32 @sum_a(ptr noundef %5, i64 noundef 4) #12
  call void @llvm.lifetime.end.p0(ptr nonnull %_16)
  %_14 = zext i32 %_15 to i64
  %s = add nuw nsw i64 %_14, %_9
  %_20 = xor i64 %b, %a
  %_0 = xor i64 %_20, %s
  call void @llvm.lifetime.end.p0(ptr nonnull %v)
  ret i64 %_0
}

define noundef <16 x i8> @f2i128(double noundef %a) unnamed_addr #3 {
start:
  %0 = tail call i128 @llvm.fptosi.sat.i128.f64(double %a)
  %1 = bitcast i128 %0 to <16 x i8>
  ret <16 x i8> %1
}

define noundef i32 @sum_a(ptr nofree noundef readonly captures(none) %p, i64 noundef %n) unnamed_addr #4 {
start:
  %_93.not = icmp eq i64 %n, 0
  br i1 %_93.not, label %bb3, label %bb2.preheader

bb2.preheader:                                    ; preds = %start
  %min.iters.check = icmp ult i64 %n, 8
  br i1 %min.iters.check, label %bb2.preheader8, label %vector.ph

vector.ph:                                        ; preds = %bb2.preheader
  %n.vec = and i64 %n, -8
  br label %vector.body

vector.body:                                      ; preds = %vector.body, %vector.ph
  %index = phi i64 [ 0, %vector.ph ], [ %index.next, %vector.body ]
  %vec.phi = phi <4 x i32> [ zeroinitializer, %vector.ph ], [ %4, %vector.body ]
  %vec.phi6 = phi <4 x i32> [ zeroinitializer, %vector.ph ], [ %5, %vector.body ]
  %0 = getelementptr inbounds nuw [4 x i8], ptr %p, i64 %index
  %1 = getelementptr inbounds nuw i8, ptr %0, i64 16
  %wide.load = load <4 x i32>, ptr %0, align 4
  %wide.load7 = load <4 x i32>, ptr %1, align 4
  %2 = mul <4 x i32> %wide.load, splat (i32 3)
  %3 = mul <4 x i32> %wide.load7, splat (i32 3)
  %4 = add <4 x i32> %2, %vec.phi
  %5 = add <4 x i32> %3, %vec.phi6
  %index.next = add nuw i64 %index, 8
  %6 = icmp eq i64 %index.next, %n.vec
  br i1 %6, label %middle.block, label %vector.body

middle.block:                                     ; preds = %vector.body
  %bin.rdx = add <4 x i32> %5, %4
  %7 = tail call i32 @llvm.vector.reduce.add.v4i32(<4 x i32> %bin.rdx)
  %cmp.n = icmp eq i64 %n, %n.vec
  br i1 %cmp.n, label %bb3, label %bb2.preheader8

bb2.preheader8:                                   ; preds = %bb2.preheader, %middle.block
  %s.sroa.0.05.ph = phi i32 [ 0, %bb2.preheader ], [ %7, %middle.block ]
  %iter.sroa.0.04.ph = phi i64 [ 0, %bb2.preheader ], [ %n.vec, %middle.block ]
  br label %bb2

bb3:                                              ; preds = %bb2, %middle.block, %start
  %s.sroa.0.0.lcssa = phi i32 [ 0, %start ], [ %7, %middle.block ], [ %_5, %bb2 ]
  ret i32 %s.sroa.0.0.lcssa

bb2:                                              ; preds = %bb2.preheader8, %bb2
  %s.sroa.0.05 = phi i32 [ %_5, %bb2 ], [ %s.sroa.0.05.ph, %bb2.preheader8 ]
  %iter.sroa.0.04 = phi i64 [ %8, %bb2 ], [ %iter.sroa.0.04.ph, %bb2.preheader8 ]
  %8 = add nuw i64 %iter.sroa.0.04, 1
  %_8 = getelementptr inbounds nuw [4 x i8], ptr %p, i64 %iter.sroa.0.04
  %_7 = load i32, ptr %_8, align 4, !noundef !1
  %_6 = mul i32 %_7, 3
  %_5 = add i32 %_6, %s.sroa.0.05
  %exitcond.not = icmp eq i64 %8, %n
  br i1 %exitcond.not, label %bb3, label %bb2
}

declare i32 @puts(ptr)

; 0x and 16 lowercase hex digits.
define internal void @put_hex64(i64 %v) #12 {
entry:
  %buf = alloca [19 x i8], align 1
  store i8 48, ptr %buf, align 1
  %x = getelementptr inbounds i8, ptr %buf, i64 1
  store i8 120, ptr %x, align 1
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sh.n = sub i64 15, %i
  %sh = shl i64 %sh.n, 2
  %nib.s = lshr i64 %v, %sh
  %nib = and i64 %nib.s, 15
  %isdig = icmp ult i64 %nib, 10
  %c.d = add i64 %nib, 48
  %c.a = add i64 %nib, 87
  %c = select i1 %isdig, i64 %c.d, i64 %c.a
  %c8 = trunc i64 %c to i8
  %off = add i64 %i, 2
  %slot = getelementptr inbounds i8, ptr %buf, i64 %off
  store i8 %c8, ptr %slot, align 1
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, 16
  br i1 %done, label %exit, label %loop

exit:
  %end = getelementptr inbounds i8, ptr %buf, i64 18
  store i8 0, ptr %end, align 1
  %r = call i32 @puts(ptr %buf)
  ret void
}

; Prints the two halves of a u128 returned as <16 x i8>; returns their xor.
define internal i64 @put_u128(<16 x i8> %v) #12 {
entry:
  %w = bitcast <16 x i8> %v to i128
  %hi.w = lshr i128 %w, 64
  %hi = trunc i128 %hi.w to i64
  %lo = trunc i128 %w to i64
  call void @put_hex64(i64 %hi)
  call void @put_hex64(i64 %lo)
  %x = xor i64 %hi, %lo
  ret i64 %x
}

define i32 @main() {
start:
  %a = alloca i128, align 16
  %b = alloca i128, align 16
  ; entry(5) as merge.rs's caller would get it.
  %e = call i64 @entry(i64 5)
  call void @put_hex64(i64 %e)
  ; 10^30 + 7 by 97, and 2^128 - 1 by 2^64 + 1.
  store i128 1000000000000000000000000000007, ptr %a, align 16
  store i128 97, ptr %b, align 16
  %d1 = call <16 x i8> @div128(ptr %a, ptr %b)
  %x1 = call i64 @put_u128(<16 x i8> %d1)
  store i128 -1, ptr %a, align 16
  store i128 18446744073709551617, ptr %b, align 16
  %d2 = call <16 x i8> @div128(ptr %a, ptr %b)
  %x2 = call i64 @put_u128(<16 x i8> %d2)
  ; Saturating double -> i128: in range, above, below, +inf and NaN.
  %f1 = call <16 x i8> @f2i128(double -1.5e30)
  %y1 = call i64 @put_u128(<16 x i8> %f1)
  %f2 = call <16 x i8> @f2i128(double 1.0e40)
  %y2 = call i64 @put_u128(<16 x i8> %f2)
  %f3 = call <16 x i8> @f2i128(double -1.0e40)
  %y3 = call i64 @put_u128(<16 x i8> %f3)
  %f4 = call <16 x i8> @f2i128(double 0x7FF0000000000000)
  %y4 = call i64 @put_u128(<16 x i8> %f4)
  %f5 = call <16 x i8> @f2i128(double 0x7FF8000000000000)
  %y5 = call i64 @put_u128(<16 x i8> %f5)
  %f6 = call <16 x i8> @f2i128(double 0x47DFFFFFFFFFFFFF)
  %y6 = call i64 @put_u128(<16 x i8> %f6)
  ; The alias: by name, and through the pointer in @sum_fn.
  %s1 = call i32 @sum_b(ptr @nums, i64 11)
  %s1.w = zext i32 %s1 to i64
  call void @put_hex64(i64 %s1.w)
  %fp = load ptr, ptr @sum_fn, align 8
  %s2 = call i32 %fp(ptr @nums, i64 9)
  %s2.w = zext i32 %s2 to i64
  call void @put_hex64(i64 %s2.w)
  ; Checksum.
  %c0 = xor i64 %e, %x1
  %c1 = xor i64 %c0, %x2
  %c2 = xor i64 %c1, %y1
  %c3 = xor i64 %c2, %y2
  %c4 = xor i64 %c3, %y3
  %c5 = xor i64 %c4, %y4
  %c6 = xor i64 %c5, %y5
  %c7 = xor i64 %c6, %y6
  %c8 = add i64 %c7, %s1.w
  %c9 = add i64 %c8, %s2.w
  %h = lshr i64 %c9, 32
  %c10 = xor i64 %c9, %h
  %t = trunc i64 %c10 to i32
  %u = and i32 %t, 127
  ret i32 %u
}

declare i64 @llvm.fshl.i64(i64, i64, i64)
declare void @llvm.trap()
declare void @llvm.lifetime.start.p0(ptr captures(none))
declare void @llvm.lifetime.end.p0(ptr captures(none))
declare i128 @llvm.fptosi.sat.i128.f64(double)
declare i32 @llvm.vector.reduce.add.v4i32(<4 x i32>)

attributes #1 = { mustprogress nofree noinline norecurse nosync nounwind willreturn memory(none) }
attributes #2 = { noinline nounwind }
attributes #3 = { mustprogress nofree noinline norecurse nosync nounwind willreturn memory(none) }
attributes #4 = { nofree noinline norecurse nosync nounwind memory(argmem: read) }
attributes #7 = { cold noinline noreturn nounwind }
attributes #10 = { noinline noreturn nounwind }
attributes #11 = { nounwind }
attributes #12 = { noinline nounwind }

!0 = !{i64 1631086845479764}
!1 = !{}
