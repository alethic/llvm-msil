; ModuleID = 'merge.d7ea951b9ecad8af-cgu.0'
source_filename = "merge.d7ea951b9ecad8af-cgu.0"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

@alloc_468526ea73219c847ada80a24233013f = private unnamed_addr constant [9 x i8] c"merge.rs\00", align 1, !guid !0
@alloc_92f628b60e16d0287d9db324f600a0cf = private unnamed_addr constant <{ ptr, [16 x i8] }> <{ ptr @alloc_468526ea73219c847ada80a24233013f, [16 x i8] c"\08\00\00\00\00\00\00\00 \00\00\006\00\00\00" }>, align 8, !guid !1

@sum_b = unnamed_addr alias i32 (ptr, i64), ptr @sum_a

; __rustc::rust_begin_unwind
; Function Attrs: nofree norecurse noreturn nosync nounwind memory(none) uwtable
define hidden void @_RNvCsdtSMxSA38aw_7___rustc17rust_begin_unwind(ptr noalias nofree noundef readonly align 8 captures(none) dereferenceable(24) %_1) unnamed_addr #0 !guid !5 {
start:
  br label %bb1

bb1:                                              ; preds = %bb1, %start
  br label %bb1
}

; merge::priv_a
; Function Attrs: mustprogress nofree noinline norecurse nosync nounwind willreturn memory(none) uwtable
define internal fastcc noundef i64 @_RNvCsixjGQLskEMJ_5merge6priv_a(i64 noundef %x) unnamed_addr #1 personality ptr @__CxxFrameHandler3 !guid !6 {
start:
  %_3 = mul i64 %x, -7046029254386353131
  %_0.i = tail call noundef i64 @llvm.fshl.i64(i64 %_3, i64 %_3, i64 17)
  %_0 = xor i64 %_0.i, 4660
  ret i64 %_0
}

; Function Attrs: nounwind uwtable
define noundef <16 x i8> @div128(ptr noalias nofree noundef readonly align 16 captures(none) dead_on_return dereferenceable(16) %a, ptr noalias nofree noundef readonly align 16 captures(none) dead_on_return dereferenceable(16) %b) unnamed_addr #2 !guid !7 {
start:
  %0 = load i128, ptr %b, align 16, !noundef !8
  %_3 = icmp eq i128 %0, 0
  br i1 %_3, label %panic, label %bb1

bb1:                                              ; preds = %start
  %1 = load i128, ptr %a, align 16, !noundef !8
  %2 = udiv i128 %1, %0
  %3 = bitcast i128 %2 to <16 x i8>
  ret <16 x i8> %3

panic:                                            ; preds = %start
; call core::panicking::panic_const::panic_const_div_by_zero
  tail call void @_RNvNtNtCs9rw95XCWuke_4core9panicking11panic_const23panic_const_div_by_zero(ptr noalias nofree noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_92f628b60e16d0287d9db324f600a0cf) #10
  unreachable
}

; Function Attrs: nounwind uwtable
define noundef i64 @entry(i64 noundef %x) unnamed_addr #2 !guid !9 {
start:
  %_16 = alloca [8 x i8], align 8
  %v = alloca [16 x i8], align 4
  %_5 = alloca [8 x i8], align 8
  %_3 = alloca [8 x i8], align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %_3)
  store i64 %x, ptr %_3, align 8
  call void asm sideeffect "", "r,~{memory}"(ptr nonnull %_3) #11, !srcloc !10
  %0 = load i64, ptr %_3, align 8, !noundef !8
; call merge::priv_a
  %a = call fastcc noundef i64 @_RNvCsixjGQLskEMJ_5merge6priv_a(i64 noundef %0) #12
  call void @llvm.lifetime.end.p0(ptr nonnull %_3)
  call void @llvm.lifetime.start.p0(ptr nonnull %_5)
  %_6 = add i64 %x, 1
  store i64 %_6, ptr %_5, align 8
  call void asm sideeffect "", "r,~{memory}"(ptr nonnull %_5) #11, !srcloc !10
  %1 = load i64, ptr %_5, align 8, !noundef !8
; call merge::priv_a
  %b = call fastcc noundef i64 @_RNvCsixjGQLskEMJ_5merge6priv_a(i64 noundef %1) #12
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
  call void asm sideeffect "", "r,~{memory}"(ptr nonnull %_16) #11, !srcloc !10
  %5 = load ptr, ptr %_16, align 8, !noundef !8
  %_15 = call noundef i32 @sum_a(ptr noundef %5, i64 noundef 4) #12
  call void @llvm.lifetime.end.p0(ptr nonnull %_16)
  %_14 = zext i32 %_15 to i64
  %s = add nuw nsw i64 %_14, %_9
  %_20 = xor i64 %b, %a
  %_0 = xor i64 %_20, %s
  call void @llvm.lifetime.end.p0(ptr nonnull %v)
  ret i64 %_0
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define noundef <16 x i8> @f2i128(double noundef %a) unnamed_addr #3 !guid !11 {
start:
  %0 = tail call i128 @llvm.fptosi.sat.i128.f64(double %a)
  %1 = bitcast i128 %0 to <16 x i8>
  ret <16 x i8> %1
}

; Function Attrs: nofree noinline norecurse nosync nounwind memory(argmem: read) uwtable
define noundef i32 @sum_a(ptr nofree noundef readonly captures(none) %p, i64 noundef %n) unnamed_addr #4 !guid !12 {
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
  br i1 %6, label %middle.block, label %vector.body, !llvm.loop !13

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
  %_7 = load i32, ptr %_8, align 4, !noundef !8
  %_6 = mul i32 %_7, 3
  %_5 = add i32 %_6, %s.sroa.0.05
  %exitcond.not = icmp eq i64 %8, %n
  br i1 %exitcond.not, label %bb3, label %bb2, !llvm.loop !16
}

declare i32 @__CxxFrameHandler3(...) unnamed_addr #5

; Function Attrs: mustprogress nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.fshl.i64(i64, i64, i64) #6

; core::panicking::panic_const::panic_const_div_by_zero
; Function Attrs: cold noinline noreturn nounwind uwtable
declare void @_RNvNtNtCs9rw95XCWuke_4core9panicking11panic_const23panic_const_div_by_zero(ptr noalias nofree noundef readonly align 8 captures(address, read_provenance) dereferenceable(24)) unnamed_addr #7

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #8

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #8

; Function Attrs: mustprogress nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i128 @llvm.fptosi.sat.i128.f64(double) #6

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.vector.reduce.add.v4i32(<4 x i32>) #9

attributes #0 = { nofree norecurse noreturn nosync nounwind memory(none) uwtable "target-cpu"="x86-64" "target-features"="+cx16,+sse,+sse2,+sse3,+sahf" }
attributes #1 = { mustprogress nofree noinline norecurse nosync nounwind willreturn memory(none) uwtable "target-cpu"="x86-64" "target-features"="+cx16,+sse,+sse2,+sse3,+sahf" }
attributes #2 = { nounwind uwtable "target-cpu"="x86-64" "target-features"="+cx16,+sse,+sse2,+sse3,+sahf" }
attributes #3 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable "target-cpu"="x86-64" "target-features"="+cx16,+sse,+sse2,+sse3,+sahf" }
attributes #4 = { nofree noinline norecurse nosync nounwind memory(argmem: read) uwtable "target-cpu"="x86-64" "target-features"="+cx16,+sse,+sse2,+sse3,+sahf" }
attributes #5 = { "target-cpu"="x86-64" }
attributes #6 = { mustprogress nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #7 = { cold noinline noreturn nounwind uwtable "target-cpu"="x86-64" "target-features"="+cx16,+sse,+sse2,+sse3,+sahf" }
attributes #8 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #9 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #10 = { noinline noreturn nounwind }
attributes #11 = { nounwind }
attributes #12 = { noinline nounwind }

!llvm.module.flags = !{!2, !3}
!llvm.ident = !{!4}

!0 = !{i64 5042815465377601346}
!1 = !{i64 7586570886626885570}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"rustc version 1.99.0 (b940084d7 2026-09-28)"}
!5 = !{i64 877453575045248192}
!6 = !{i64 -2100763929168933912}
!7 = !{i64 -7780487720894029417}
!8 = !{}
!9 = !{i64 6910751087267889936}
!10 = !{i64 1631086845479764}
!11 = !{i64 1764603795362685826}
!12 = !{i64 -6557184791284519764}
!13 = distinct !{!13, !14, !15}
!14 = !{!"llvm.loop.isvectorized", i32 1}
!15 = !{!"llvm.loop.unroll.runtime.disable"}
!16 = distinct !{!16, !15, !14}
