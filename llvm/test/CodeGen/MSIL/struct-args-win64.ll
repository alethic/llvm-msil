; Struct arguments and results in the shapes clang emits for Win64
; (ImplementationPlan.md M9 goal, decision D13; Design.md 3 'clang reuses its
; native TargetInfo and ABI lowering', 3.1; recon registry-tm.md 6 'Win64:
; structs of size 1/2/4/8 coerced to i64 etc.; larger ones by pointer'). The
; input is the IR clang -O2 produces for x86_64-pc-windows-msvc-msil from C
; functions that take and return structs, so these are the shapes the
; 09-structs execution tests run on Windows. A struct of 1, 2, 4 or 8 bytes
; is passed and returned as an integer of that size (int32 or native int; a
; struct of two floats is an i64 taken apart through a frame slot, D16).
; Any other struct is passed as a pointer to a temporary copy that the caller
; makes in IR (an alloca in its static frame filled by memcpy, which is cpblk
; with 'unaligned. N' below 8-byte alignment, D15/D24), and returned through
; clang's sret pointer, an ordinary first parameter. There is no byval in
; this ABI, and no diagnostic anywhere.
;
; SIG checks every method header; the callers are exact at -O2 (CHECK where
; -O0 is the same, O2 otherwise) and checked for their copies and calls at
; -O0 (O0). The same IR runs on both triples (struct-args-sysv.ll has the
; SysV shapes).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=SIG,CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=SIG,CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=SIG,CHECK,O0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=SIG,CHECK,O0
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "struct-args-win64.c"

%struct.I3 = type { i32, i32, i32 }
%struct.L2 = type { i64, i64 }
%struct.D2 = type { double, double }
%struct.Big = type { i64, i64, i64 }
%struct.F3 = type { float, float, float }
%struct.DL = type { double, i64 }
%struct.C3 = type { [3 x i8] }

; ---- callees: the signatures are the ABI shapes -------------------------------
; 8 bytes: coerced to i64.
; SIG:      {{^}}.method public static int32 'take_i2'(native int) cil managed{{$}}
define i32 @take_i2(i64 %s.coerce) {
entry:
  %s.sroa.0.0.extract.trunc = trunc i64 %s.coerce to i32
  %s.sroa.2.0.extract.shift = lshr i64 %s.coerce, 32
  %s.sroa.2.0.extract.trunc = trunc nuw i64 %s.sroa.2.0.extract.shift to i32
  %add = add i32 %s.sroa.2.0.extract.trunc, %s.sroa.0.0.extract.trunc
  ret i32 %add
}

; 12 bytes: a pointer to the caller's temporary copy (dead_on_return).
; SIG:      {{^}}.method public static int32 'take_i3'(native int) cil managed{{$}}
define i32 @take_i3(ptr nofreeobj noundef readonly align 4 captures(none) dead_on_return dereferenceable(12) %s) {
entry:
  %0 = load i32, ptr %s, align 4
  %b = getelementptr inbounds nuw i8, ptr %s, i64 4
  %1 = load i32, ptr %b, align 4
  %add = add i32 %1, %0
  %c = getelementptr inbounds nuw i8, ptr %s, i64 8
  %2 = load i32, ptr %c, align 4
  %add1 = add i32 %add, %2
  ret i32 %add1
}

; SIG:      {{^}}.method public static native int 'take_l2'(native int) cil managed{{$}}
define i64 @take_l2(ptr nofreeobj noundef readonly align 8 captures(none) dead_on_return dereferenceable(16) %s) {
entry:
  %0 = load i64, ptr %s, align 8
  %b = getelementptr inbounds nuw i8, ptr %s, i64 8
  %1 = load i64, ptr %b, align 8
  %add = add i64 %1, %0
  ret i64 %add
}

; SIG:      {{^}}.method public static float64 'take_d2'(native int) cil managed{{$}}
define double @take_d2(ptr nofreeobj noundef readonly align 8 captures(none) dead_on_return dereferenceable(16) %s) {
entry:
  %0 = load double, ptr %s, align 8
  %y = getelementptr inbounds nuw i8, ptr %s, i64 8
  %1 = load double, ptr %y, align 8
  %add = fadd double %0, %1
  ret double %add
}

; Two floats in 8 bytes: an i64, taken apart with shifts and float/int
; bitcasts through a frame slot (D16).
; SIG:      {{^}}.method public static float32 'take_f2'(native int) cil managed{{$}}
define float @take_f2(i64 %s.coerce) {
entry:
  %s.sroa.0.0.extract.trunc = trunc i64 %s.coerce to i32
  %0 = bitcast i32 %s.sroa.0.0.extract.trunc to float
  %s.sroa.2.0.extract.shift = lshr i64 %s.coerce, 32
  %s.sroa.2.0.extract.trunc = trunc nuw i64 %s.sroa.2.0.extract.shift to i32
  %1 = bitcast i32 %s.sroa.2.0.extract.trunc to float
  %add = fadd float %0, %1
  ret float %add
}

; SIG:      {{^}}.method public static float32 'take_f3'(native int) cil managed{{$}}
define float @take_f3(ptr nofreeobj noundef readonly align 4 captures(none) dead_on_return dereferenceable(12) %s) {
entry:
  %0 = load float, ptr %s, align 4
  %y = getelementptr inbounds nuw i8, ptr %s, i64 4
  %1 = load float, ptr %y, align 4
  %add = fadd float %0, %1
  %z = getelementptr inbounds nuw i8, ptr %s, i64 8
  %2 = load float, ptr %z, align 4
  %add1 = fadd float %add, %2
  ret float %add1
}

; SIG:      {{^}}.method public static float64 'take_dl'(native int) cil managed{{$}}
define double @take_dl(ptr nofreeobj noundef readonly align 8 captures(none) dead_on_return dereferenceable(16) %s) {
entry:
  %0 = load double, ptr %s, align 8
  %l = getelementptr inbounds nuw i8, ptr %s, i64 8
  %1 = load i64, ptr %l, align 8
  %conv = sitofp i64 %1 to double
  %add = fadd double %0, %conv
  ret double %add
}

; 3 bytes is not a power of two: by pointer.
; SIG:      {{^}}.method public static int32 'take_c3'(native int) cil managed{{$}}
define range(i32 -256, 255) i32 @take_c3(ptr nofreeobj noundef readonly align 1 captures(none) dead_on_return dereferenceable(3) %s) {
entry:
  %0 = load i8, ptr %s, align 1
  %conv = sext i8 %0 to i32
  %arrayidx2 = getelementptr inbounds nuw i8, ptr %s, i64 2
  %1 = load i8, ptr %arrayidx2, align 1
  %conv3 = sext i8 %1 to i32
  %add = add nsw i32 %conv3, %conv
  ret i32 %add
}

; SIG:      {{^}}.method public static native int 'take_big'(native int) cil managed{{$}}
define i64 @take_big(ptr nofreeobj noundef readonly align 8 captures(none) dead_on_return dereferenceable(24) %s) {
entry:
  %0 = load i64, ptr %s, align 8
  %c = getelementptr inbounds nuw i8, ptr %s, i64 16
  %1 = load i64, ptr %c, align 8
  %add = add i64 %1, %0
  ret i64 %add
}

; struct { float f; }: coerced to i32.
; SIG:      {{^}}.method public static float32 'take_f1'(int32) cil managed{{$}}
define float @take_f1(i32 %s.coerce) {
entry:
  %0 = bitcast i32 %s.coerce to float
  ret float %0
}

; 8-byte results come back as an i64; larger ones through sret.
; SIG:      {{^}}.method public static native int 'ret_i2'(int32) cil managed{{$}}
define i64 @ret_i2(i32 noundef %a) {
entry:
  %add = add i32 %a, 1
  %retval.sroa.2.0.insert.ext = zext i32 %add to i64
  %retval.sroa.2.0.insert.shift = shl nuw i64 %retval.sroa.2.0.insert.ext, 32
  %retval.sroa.0.0.insert.ext = zext i32 %a to i64
  %retval.sroa.0.0.insert.insert = or disjoint i64 %retval.sroa.2.0.insert.shift, %retval.sroa.0.0.insert.ext
  ret i64 %retval.sroa.0.0.insert.insert
}

; SIG:      {{^}}.method public static void 'ret_i3'(native int, int32) cil managed{{$}}
define void @ret_i3(ptr dead_on_unwind noalias nofree writable writeonly sret(%struct.I3) align 4 captures(none) initializes((0, 12)) %agg.result, i32 noundef %a) {
entry:
  store i32 %a, ptr %agg.result, align 4
  %b = getelementptr inbounds nuw i8, ptr %agg.result, i64 4
  %add = add i32 %a, 1
  store i32 %add, ptr %b, align 4
  %c = getelementptr inbounds nuw i8, ptr %agg.result, i64 8
  %add2 = add i32 %a, 2
  store i32 %add2, ptr %c, align 4
  ret void
}

; SIG:      {{^}}.method public static void 'ret_l2'(native int, native int) cil managed{{$}}
define void @ret_l2(ptr dead_on_unwind noalias nofree writable writeonly sret(%struct.L2) align 8 captures(none) initializes((0, 16)) %agg.result, i64 noundef %a) {
entry:
  store i64 %a, ptr %agg.result, align 8
  %b = getelementptr inbounds nuw i8, ptr %agg.result, i64 8
  %add = add i64 %a, 1
  store i64 %add, ptr %b, align 8
  ret void
}

; SIG:      {{^}}.method public static void 'ret_d2'(native int, float64) cil managed{{$}}
define void @ret_d2(ptr dead_on_unwind noalias nofree writable writeonly sret(%struct.D2) align 8 captures(none) initializes((0, 16)) %agg.result, double noundef %a) {
entry:
  store double %a, ptr %agg.result, align 8
  %y = getelementptr inbounds nuw i8, ptr %agg.result, i64 8
  %mul = fmul double %a, 2.000000e+00
  store double %mul, ptr %y, align 8
  ret void
}

; SIG:      {{^}}.method public static native int 'ret_f2'(float32) cil managed{{$}}
define i64 @ret_f2(float noundef %a) {
entry:
  %mul = fmul float %a, 2.000000e+00
  %0 = bitcast float %a to i32
  %1 = bitcast float %mul to i32
  %retval.sroa.2.0.insert.ext = zext i32 %1 to i64
  %retval.sroa.2.0.insert.shift = shl nuw i64 %retval.sroa.2.0.insert.ext, 32
  %retval.sroa.0.0.insert.ext = zext i32 %0 to i64
  %retval.sroa.0.0.insert.insert = or disjoint i64 %retval.sroa.2.0.insert.shift, %retval.sroa.0.0.insert.ext
  ret i64 %retval.sroa.0.0.insert.insert
}

; SIG:      {{^}}.method public static void 'ret_big'(native int, native int) cil managed{{$}}
define void @ret_big(ptr dead_on_unwind noalias nofree writable writeonly sret(%struct.Big) align 8 captures(none) initializes((0, 24)) %agg.result, i64 noundef %a) {
entry:
  store i64 %a, ptr %agg.result, align 8
  %b = getelementptr inbounds nuw i8, ptr %agg.result, i64 8
  %add = add i64 %a, 1
  store i64 %add, ptr %b, align 8
  %c = getelementptr inbounds nuw i8, ptr %agg.result, i64 16
  %add2 = add i64 %a, 2
  store i64 %add2, ptr %c, align 8
  ret void
}

; ---- callers ------------------------------------------------------------------

; An 8-byte struct is loaded as one native int.
; CHECK-LABEL: .method {{.*}}'c_i2'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  call int32 'take_i2'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @c_i2(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load i64, ptr %p, align 4
  %call = tail call i32 @take_i2(i64 %0)
  ret i32 %call
}

; A 12-byte struct: clang copies it into a temporary (memcpy, here cpblk with
; 'unaligned. 4' for the 4-byte alignment) in the caller's static frame and
; passes the temporary's address.
; O0-LABEL: .method {{.*}}'c_i3'(
; O0:       {{^}}  localloc{{$}}
; O0:       {{^}}  unaligned. 4{{$}}
; O0-NEXT:  {{^}}  cpblk{{$}}
; O0:       {{^}}  call int32 'take_i3'(native int){{$}}
; O2-LABEL: .method {{.*}}'c_i3'(
; O2-SAME:  native int) cil managed{{$}}
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 3{{$}}
; O2-NEXT:  {{^}}  .locals (native int){{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:  {{^}}  localloc{{$}}
; O2-NEXT:  {{^}}  stloc.0{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.4{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 12{{$}}
; O2-NEXT:  {{^}}  unaligned. 4{{$}}
; O2-NEXT:  {{^}}  cpblk{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.4{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  call int32 'take_i3'(native int){{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define i32 @c_i3(ptr nofree noundef readonly captures(none) %p) {
entry:
  %byval-temp = alloca %struct.I3, align 4
  call void @llvm.lifetime.start.p0(ptr nonnull %byval-temp)
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 4 dereferenceable(12) %byval-temp, ptr noundef nonnull align 4 dereferenceable(12) %p, i64 12, i1 false)
  %call = call i32 @take_i3(ptr nofreeobj noundef nonnull align 4 dead_on_return dereferenceable(12) %byval-temp)
  call void @llvm.lifetime.end.p0(ptr nonnull %byval-temp)
  ret i32 %call
}

; 16 bytes: a 16-byte temporary copy (8-aligned: no prefix), passed by address.
; CHECK-LABEL: .method {{.*}}'c_l2'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call native int 'take_l2'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @c_l2(ptr nofree noundef readonly captures(none) %p) {
entry:
  %byval-temp = alloca %struct.L2, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %byval-temp)
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %byval-temp, ptr noundef nonnull align 8 dereferenceable(16) %p, i64 16, i1 false)
  %call = call i64 @take_l2(ptr nofreeobj noundef nonnull align 8 dead_on_return dereferenceable(16) %byval-temp)
  call void @llvm.lifetime.end.p0(ptr nonnull %byval-temp)
  ret i64 %call
}

; CHECK-LABEL: .method {{.*}}'c_d2'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call float64 'take_d2'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @c_d2(ptr nofree noundef readonly captures(none) %p) {
entry:
  %byval-temp = alloca %struct.D2, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %byval-temp)
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %byval-temp, ptr noundef nonnull align 8 dereferenceable(16) %p, i64 16, i1 false)
  %call = call double @take_d2(ptr nofreeobj noundef nonnull align 8 dead_on_return dereferenceable(16) %byval-temp)
  call void @llvm.lifetime.end.p0(ptr nonnull %byval-temp)
  ret double %call
}

; Two floats loaded as one native int.
; CHECK-LABEL: .method {{.*}}'c_f2'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  call float32 'take_f2'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @c_f2(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load i64, ptr %p, align 4
  %call = tail call float @take_f2(i64 %0)
  ret float %call
}

; O0-LABEL: .method {{.*}}'c_f3'(
; O0:       {{^}}  cpblk{{$}}
; O0:       {{^}}  call float32 'take_f3'(native int){{$}}
; O2-LABEL: .method {{.*}}'c_f3'(
; O2-SAME:  native int) cil managed{{$}}
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 3{{$}}
; O2-NEXT:  {{^}}  .locals (native int){{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:  {{^}}  localloc{{$}}
; O2-NEXT:  {{^}}  stloc.0{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.4{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 12{{$}}
; O2-NEXT:  {{^}}  unaligned. 4{{$}}
; O2-NEXT:  {{^}}  cpblk{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.4{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  call float32 'take_f3'(native int){{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define float @c_f3(ptr nofree noundef readonly captures(none) %p) {
entry:
  %byval-temp = alloca %struct.F3, align 4
  call void @llvm.lifetime.start.p0(ptr nonnull %byval-temp)
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 4 dereferenceable(12) %byval-temp, ptr noundef nonnull align 4 dereferenceable(12) %p, i64 12, i1 false)
  %call = call float @take_f3(ptr nofreeobj noundef nonnull align 4 dead_on_return dereferenceable(12) %byval-temp)
  call void @llvm.lifetime.end.p0(ptr nonnull %byval-temp)
  ret float %call
}

; CHECK-LABEL: .method {{.*}}'c_dl'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call float64 'take_dl'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @c_dl(ptr nofree noundef readonly captures(none) %p) {
entry:
  %byval-temp = alloca %struct.DL, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %byval-temp)
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %byval-temp, ptr noundef nonnull align 8 dereferenceable(16) %p, i64 16, i1 false)
  %call = call double @take_dl(ptr nofreeobj noundef nonnull align 8 dead_on_return dereferenceable(16) %byval-temp)
  call void @llvm.lifetime.end.p0(ptr nonnull %byval-temp)
  ret double %call
}

; A 3-byte temporary at FP + 13 (alignment 1: 'unaligned. 1').
; O0-LABEL: .method {{.*}}'c_c3'(
; O0:       {{^}}  unaligned. 1{{$}}
; O0-NEXT:  {{^}}  cpblk{{$}}
; O0:       {{^}}  call int32 'take_c3'(native int){{$}}
; O2-LABEL: .method {{.*}}'c_c3'(
; O2-SAME:  native int) cil managed{{$}}
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 3{{$}}
; O2-NEXT:  {{^}}  .locals (native int){{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:  {{^}}  localloc{{$}}
; O2-NEXT:  {{^}}  stloc.0{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 13{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.3{{$}}
; O2-NEXT:  {{^}}  unaligned. 1{{$}}
; O2-NEXT:  {{^}}  cpblk{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 13{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  call int32 'take_c3'(native int){{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define i32 @c_c3(ptr nofree noundef readonly captures(none) %p) {
entry:
  %byval-temp = alloca %struct.C3, align 1
  call void @llvm.lifetime.start.p0(ptr nonnull %byval-temp)
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 1 dereferenceable(3) %byval-temp, ptr noundef nonnull align 1 dereferenceable(3) %p, i64 3, i1 false)
  %call = call i32 @take_c3(ptr nofreeobj noundef nonnull align 1 dead_on_return dereferenceable(3) %byval-temp)
  call void @llvm.lifetime.end.p0(ptr nonnull %byval-temp)
  ret i32 %call
}

; struct { float f; }: loaded as an int32.
; CHECK-LABEL: .method {{.*}}'c_f1'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  call float32 'take_f1'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @c_f1(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load i32, ptr %p, align 4
  %call = tail call float @take_f1(i32 %0)
  ret float %call
}

; The second int of an 8-byte result is the high half of the native int.
; CHECK-LABEL: .method {{.*}}'r_i2'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call native int 'ret_i2'(int32){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @r_i2(i32 noundef %a) {
entry:
  %call = tail call i64 @ret_i2(i32 noundef %a)
  %ref.tmp.sroa.3.0.extract.shift = lshr i64 %call, 32
  %ref.tmp.sroa.3.0.extract.trunc = trunc nuw i64 %ref.tmp.sroa.3.0.extract.shift to i32
  ret i32 %ref.tmp.sroa.3.0.extract.trunc
}

; O0-LABEL: .method {{.*}}'c_big'(
; O0:       {{^}}  cpblk{{$}}
; O0:       {{^}}  call native int 'take_big'(native int){{$}}
; O2-LABEL: .method {{.*}}'c_big'(
; O2-SAME:  native int) cil managed{{$}}
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 3{{$}}
; O2-NEXT:  {{^}}  .locals (native int){{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; O2-NEXT:  {{^}}  localloc{{$}}
; O2-NEXT:  {{^}}  stloc.0{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.8{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 24{{$}}
; O2-NEXT:  {{^}}  cpblk{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.8{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  call native int 'take_big'(native int){{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define i64 @c_big(ptr nofree noundef readonly captures(none) %p) {
entry:
  %byval-temp = alloca %struct.Big, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %byval-temp)
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(24) %byval-temp, ptr noundef nonnull align 8 dereferenceable(24) %p, i64 24, i1 false)
  %call = call i64 @take_big(ptr nofreeobj noundef nonnull align 8 dead_on_return dereferenceable(24) %byval-temp)
  call void @llvm.lifetime.end.p0(ptr nonnull %byval-temp)
  ret i64 %call
}

; The temporary is filled directly (SROA removed the local).
; O0-LABEL: .method {{.*}}'c_big_local'(
; O0:       {{^}}  localloc{{$}}
; O0:       {{^}}  call native int 'take_big'(native int){{$}}
; O2-LABEL: .method {{.*}}'c_big_local'(
; O2-SAME:  native int) cil managed{{$}}
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 3{{$}}
; O2-NEXT:  {{^}}  .locals (native int){{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; O2-NEXT:  {{^}}  localloc{{$}}
; O2-NEXT:  {{^}}  stloc.0{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.8{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  stind.i{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.1{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  stind.i{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 24{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.2{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  stind.i{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.8{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  call native int 'take_big'(native int){{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define i64 @c_big_local(i64 noundef %x) {
entry:
  %byval-temp = alloca %struct.Big, align 8
  %add = add i64 %x, 1
  %add2 = add i64 %x, 2
  call void @llvm.lifetime.start.p0(ptr nonnull %byval-temp)
  store i64 %x, ptr %byval-temp, align 8
  %b.sroa.4.0.byval-temp.sroa_idx = getelementptr inbounds nuw i8, ptr %byval-temp, i64 8
  store i64 %add, ptr %b.sroa.4.0.byval-temp.sroa_idx, align 8
  %b.sroa.5.0.byval-temp.sroa_idx = getelementptr inbounds nuw i8, ptr %byval-temp, i64 16
  store i64 %add2, ptr %b.sroa.5.0.byval-temp.sroa_idx, align 8
  %call = call i64 @take_big(ptr nofreeobj noundef nonnull align 8 dead_on_return dereferenceable(24) %byval-temp)
  call void @llvm.lifetime.end.p0(ptr nonnull %byval-temp)
  ret i64 %call
}

; 16-byte results: clang's sret pointer to the caller's temporary, first.
; CHECK-LABEL: .method {{.*}}'r_l2'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'ret_l2'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @r_l2(i64 noundef %a) {
entry:
  %ref.tmp = alloca %struct.L2, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %ref.tmp)
  call void @ret_l2(ptr dead_on_unwind nonnull writable sret(%struct.L2) align 8 %ref.tmp, i64 noundef %a)
  %b = getelementptr inbounds nuw i8, ptr %ref.tmp, i64 8
  %0 = load i64, ptr %b, align 8
  call void @llvm.lifetime.end.p0(ptr nonnull %ref.tmp)
  ret i64 %0
}

; CHECK-LABEL: .method {{.*}}'r_d2'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'ret_d2'(native int, float64){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @r_d2(double noundef %a) {
entry:
  %ref.tmp = alloca %struct.D2, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %ref.tmp)
  call void @ret_d2(ptr dead_on_unwind nonnull writable sret(%struct.D2) align 8 %ref.tmp, double noundef %a)
  %y = getelementptr inbounds nuw i8, ptr %ref.tmp, i64 8
  %0 = load double, ptr %y, align 8
  call void @llvm.lifetime.end.p0(ptr nonnull %ref.tmp)
  ret double %0
}

; An 8-byte result of two floats: an i64, stored to a slot and reloaded as a
; float (D16).
; O0-LABEL: .method {{.*}}'r_f2'(
; O0:       {{^}}  call native int 'ret_f2'(float32){{$}}
; O2-LABEL: .method {{.*}}'r_f2'(
; O2-SAME:  float32) cil managed{{$}}
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 3{{$}}
; O2-NEXT:  {{^}}  .locals (native int){{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:  {{^}}  localloc{{$}}
; O2-NEXT:  {{^}}  stloc.0{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 12{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  call native int 'ret_f2'(float32){{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; O2-NEXT:  {{^}}  shr.un{{$}}
; O2-NEXT:  {{^}}  conv.i4{{$}}
; O2-NEXT:  {{^}}  stind.i4{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 12{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldind.r4{{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define float @r_f2(float noundef %a) {
entry:
  %call = tail call i64 @ret_f2(float noundef %a)
  %ref.tmp.sroa.3.0.extract.shift = lshr i64 %call, 32
  %ref.tmp.sroa.3.0.extract.trunc = trunc nuw i64 %ref.tmp.sroa.3.0.extract.shift to i32
  %0 = bitcast i32 %ref.tmp.sroa.3.0.extract.trunc to float
  ret float %0
}

; CHECK-LABEL: .method {{.*}}'r_big'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'ret_big'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 24{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @r_big(i64 noundef %a) {
entry:
  %ref.tmp = alloca %struct.Big, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %ref.tmp)
  call void @ret_big(ptr dead_on_unwind nonnull writable sret(%struct.Big) align 8 %ref.tmp, i64 noundef %a)
  %c = getelementptr inbounds nuw i8, ptr %ref.tmp, i64 16
  %0 = load i64, ptr %c, align 8
  call void @llvm.lifetime.end.p0(ptr nonnull %ref.tmp)
  ret i64 %0
}

; CHECK-LABEL: .method {{.*}}'r_i3'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.4{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'ret_i3'(native int, int32){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 12{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @r_i3(i32 noundef %a) {
entry:
  %ref.tmp = alloca %struct.I3, align 4
  call void @llvm.lifetime.start.p0(ptr nonnull %ref.tmp)
  call void @ret_i3(ptr dead_on_unwind nonnull writable sret(%struct.I3) align 4 %ref.tmp, i32 noundef %a)
  %c = getelementptr inbounds nuw i8, ptr %ref.tmp, i64 8
  %0 = load i32, ptr %c, align 4
  call void @llvm.lifetime.end.p0(ptr nonnull %ref.tmp)
  ret i32 %0
}

; O0-LABEL: .method {{.*}}'r_big_into'(
; O0:       {{^}}  call void 'ret_big'(native int, native int){{$}}
; O0:       {{^}}  cpblk{{$}}
; O2-LABEL: .method {{.*}}'r_big_into'(
; O2-SAME:  native int, native int) cil managed{{$}}
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 3{{$}}
; O2-NEXT:  {{^}}  .locals (native int){{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; O2-NEXT:  {{^}}  localloc{{$}}
; O2-NEXT:  {{^}}  stloc.0{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.8{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldarg.1{{$}}
; O2-NEXT:  {{^}}  call void 'ret_big'(native int, native int){{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.8{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 24{{$}}
; O2-NEXT:  {{^}}  cpblk{{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define void @r_big_into(ptr nofree noundef writeonly captures(none) initializes((0, 24)) %out, i64 noundef %a) {
entry:
  %tmp = alloca %struct.Big, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %tmp)
  call void @ret_big(ptr dead_on_unwind nonnull writable sret(%struct.Big) align 8 %tmp, i64 noundef %a)
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(24) %out, ptr noundef nonnull align 8 dereferenceable(24) %tmp, i64 24, i1 false)
  call void @llvm.lifetime.end.p0(ptr nonnull %tmp)
  ret void
}

declare void @llvm.memcpy.p0.p0.i64(ptr noalias writeonly captures(none), ptr noalias readonly captures(none), i64, i1 immarg)
declare void @llvm.lifetime.start.p0(ptr captures(none))
declare void @llvm.lifetime.end.p0(ptr captures(none))
