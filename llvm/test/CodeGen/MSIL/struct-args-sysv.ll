; Struct arguments and results in the shapes clang emits for x86-64 SysV
; (ImplementationPlan.md M9 goal and M9.a, decision D13; Design.md 3 'clang
; reuses its native TargetInfo and ABI lowering' and 'internal calls pass
; aggregates indirectly'; recon registry-tm.md 6, isel.md 2.12). The input is
; the IR clang -O2 produces for x86_64-unknown-linux-gnu-msil from C functions
; that take and return structs (the classification is clang's; the backend
; only sees the coerced scalars, byval pointers and sret pointers), so these
; are the shapes the 09-structs execution tests run on WSL. Each eightbyte is
; one parameter: INTEGER as native int (or int32 for a 4-byte tail), SSE as
; float64, or as two float32s for <2 x float>, which is split because there
; are no vector types (D8); an i24 is an int32. A result of two eightbytes is
; demoted to a frame slot whose address is the first argument (the method
; returns void); clang's sret is an ordinary first pointer parameter. A
; struct of more than 16 bytes is byval: the callee sees a plain pointer, and
; the caller copies the aggregate into a new object of its own static frame
; (localloc) and passes the copy's address (M9.a; the copy may be cpblk or
; loads and stores, both are accepted). No diagnostic anywhere.
;
; SIG checks every method header; the callers are exact at -O2 (CHECK where
; -O0 is the same, O2 otherwise) and checked for their calls at -O0 (O0).
; The same IR runs on both triples (struct-args-win64.ll has the Win64
; shapes).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=SIG,CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=SIG,CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=SIG,CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=SIG,CHECK,O0
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "struct-args-sysv.c"

%struct.Big = type { i64, i64, i64 }

; ---- callees: the signatures are the ABI shapes -------------------------------
; SIG:      {{^}}.method public static int32 'take_i2'(native int) cil managed{{$}}
define i32 @take_i2(i64 %s.coerce) {
entry:
  %s.sroa.0.0.extract.trunc = trunc i64 %s.coerce to i32
  %s.sroa.2.0.extract.shift = lshr i64 %s.coerce, 32
  %s.sroa.2.0.extract.trunc = trunc nuw i64 %s.sroa.2.0.extract.shift to i32
  %add = add nsw i32 %s.sroa.2.0.extract.trunc, %s.sroa.0.0.extract.trunc
  ret i32 %add
}

; SIG:      {{^}}.method public static int32 'take_i3'(native int, int32) cil managed{{$}}
define i32 @take_i3(i64 %s.coerce0, i32 %s.coerce1) {
entry:
  %s.sroa.0.0.extract.trunc = trunc i64 %s.coerce0 to i32
  %s.sroa.2.0.extract.shift = lshr i64 %s.coerce0, 32
  %s.sroa.2.0.extract.trunc = trunc nuw i64 %s.sroa.2.0.extract.shift to i32
  %add = add i32 %s.coerce1, %s.sroa.0.0.extract.trunc
  %add1 = add i32 %add, %s.sroa.2.0.extract.trunc
  ret i32 %add1
}

; SIG:      {{^}}.method public static native int 'take_l2'(native int, native int) cil managed{{$}}
define i64 @take_l2(i64 %s.coerce0, i64 %s.coerce1) {
entry:
  %add = add nsw i64 %s.coerce1, %s.coerce0
  ret i64 %add
}

; SIG:      {{^}}.method public static float64 'take_d2'(float64, float64) cil managed{{$}}
define double @take_d2(double %s.coerce0, double %s.coerce1) {
entry:
  %add = fadd double %s.coerce0, %s.coerce1
  ret double %add
}

; SIG:      {{^}}.method public static float32 'take_f2'(float32, float32) cil managed{{$}}
define float @take_f2(<2 x float> %s.coerce) {
entry:
  %s.sroa.0.0.vec.extract = extractelement <2 x float> %s.coerce, i64 0
  %s.sroa.0.4.vec.extract = extractelement <2 x float> %s.coerce, i64 1
  %add = fadd float %s.sroa.0.0.vec.extract, %s.sroa.0.4.vec.extract
  ret float %add
}

; SIG:      {{^}}.method public static float32 'take_f3'(float32, float32, float32) cil managed{{$}}
define float @take_f3(<2 x float> %s.coerce0, float %s.coerce1) {
entry:
  %s.sroa.0.0.vec.extract = extractelement <2 x float> %s.coerce0, i64 0
  %s.sroa.0.4.vec.extract = extractelement <2 x float> %s.coerce0, i64 1
  %add = fadd float %s.sroa.0.0.vec.extract, %s.sroa.0.4.vec.extract
  %add1 = fadd float %s.coerce1, %add
  ret float %add1
}

; SIG:      {{^}}.method public static float64 'take_dl'(float64, native int) cil managed{{$}}
define double @take_dl(double %s.coerce0, i64 %s.coerce1) {
entry:
  %conv = sitofp i64 %s.coerce1 to double
  %add = fadd double %s.coerce0, %conv
  ret double %add
}

; SIG:      {{^}}.method public static int32 'take_c3'(int32) cil managed{{$}}
define range(i32 -256, 255) i32 @take_c3(i24 %s.coerce) {
entry:
  %s.sroa.0.0.extract.trunc = zext i24 %s.coerce to i32
  %sext = shl i32 %s.sroa.0.0.extract.trunc, 24
  %conv = ashr exact i32 %sext, 24
  %0 = ashr i24 %s.coerce, 16
  %conv3 = sext i24 %0 to i32
  %add = add nsw i32 %conv, %conv3
  ret i32 %add
}

; A byval parameter is the pointer to the caller's copy: no diagnostic, an
; ordinary native int that the callee reads through.
; SIG:      {{^}}.method public static native int 'take_big'(native int) cil managed{{$}}
; SIG-NOT:  localloc
; SIG:      {{^}}} // end of method 'take_big'{{$}}
define i64 @take_big(ptr nofree noundef readonly byval(%struct.Big) align 8 captures(none) %s) {
entry:
  %0 = load i64, ptr %s, align 8
  %c = getelementptr inbounds nuw i8, ptr %s, i64 16
  %1 = load i64, ptr %c, align 8
  %add = add nsw i64 %1, %0
  ret i64 %add
}

; SIG:      {{^}}.method public static float32 'take_f1'(float32) cil managed{{$}}
define float @take_f1(float returned %s.coerce) {
entry:
  ret float %s.coerce
}

; SIG:      {{^}}.method public static native int 'ret_i2'(int32) cil managed{{$}}
define i64 @ret_i2(i32 noundef %a) {
entry:
  %add = add nsw i32 %a, 1
  %retval.sroa.2.0.insert.ext = zext i32 %add to i64
  %retval.sroa.2.0.insert.shift = shl nuw i64 %retval.sroa.2.0.insert.ext, 32
  %retval.sroa.0.0.insert.ext = zext i32 %a to i64
  %retval.sroa.0.0.insert.insert = or disjoint i64 %retval.sroa.2.0.insert.shift, %retval.sroa.0.0.insert.ext
  ret i64 %retval.sroa.0.0.insert.insert
}

; SIG:      {{^}}.method public static void 'ret_i3'(native int, int32) cil managed{{$}}
define { i64, i32 } @ret_i3(i32 noundef %a) {
entry:
  %add = add nsw i32 %a, 1
  %add2 = add nsw i32 %a, 2
  %retval.sroa.2.0.insert.ext = zext i32 %add to i64
  %retval.sroa.2.0.insert.shift = shl nuw i64 %retval.sroa.2.0.insert.ext, 32
  %retval.sroa.0.0.insert.ext = zext i32 %a to i64
  %retval.sroa.0.0.insert.insert = or disjoint i64 %retval.sroa.2.0.insert.shift, %retval.sroa.0.0.insert.ext
  %.fca.0.insert = insertvalue { i64, i32 } poison, i64 %retval.sroa.0.0.insert.insert, 0
  %.fca.1.insert = insertvalue { i64, i32 } %.fca.0.insert, i32 %add2, 1
  ret { i64, i32 } %.fca.1.insert
}

; SIG:      {{^}}.method public static void 'ret_l2'(native int, native int) cil managed{{$}}
define { i64, i64 } @ret_l2(i64 noundef %a) {
entry:
  %add = add nsw i64 %a, 1
  %.fca.0.insert = insertvalue { i64, i64 } poison, i64 %a, 0
  %.fca.1.insert = insertvalue { i64, i64 } %.fca.0.insert, i64 %add, 1
  ret { i64, i64 } %.fca.1.insert
}

; SIG:      {{^}}.method public static void 'ret_d2'(native int, float64) cil managed{{$}}
define { double, double } @ret_d2(double noundef %a) {
entry:
  %mul = fmul double %a, 2.000000e+00
  %.fca.0.insert = insertvalue { double, double } poison, double %a, 0
  %.fca.1.insert = insertvalue { double, double } %.fca.0.insert, double %mul, 1
  ret { double, double } %.fca.1.insert
}

; SIG:      {{^}}.method public static void 'ret_f2'(native int, float32) cil managed{{$}}
define <2 x float> @ret_f2(float noundef %a) {
entry:
  %retval.sroa.0.0.vec.insert = insertelement <2 x float> poison, float %a, i64 0
  %mul = fmul float %a, 2.000000e+00
  %retval.sroa.0.4.vec.insert = insertelement <2 x float> %retval.sroa.0.0.vec.insert, float %mul, i64 1
  ret <2 x float> %retval.sroa.0.4.vec.insert
}

; sret: a plain pointer parameter, first.
; SIG:      {{^}}.method public static void 'ret_big'(native int, native int) cil managed{{$}}
define void @ret_big(ptr dead_on_unwind noalias nofree writable writeonly sret(%struct.Big) align 8 captures(none) initializes((0, 24)) %agg.result, i64 noundef %a) {
entry:
  store i64 %a, ptr %agg.result, align 8
  %b = getelementptr inbounds nuw i8, ptr %agg.result, i64 8
  %add = add nsw i64 %a, 1
  store i64 %add, ptr %b, align 8
  %c = getelementptr inbounds nuw i8, ptr %agg.result, i64 16
  %add2 = add nsw i64 %a, 2
  store i64 %add2, ptr %c, align 8
  ret void
}

; ---- callers ------------------------------------------------------------------

; struct { int a, b; }: one INTEGER eightbyte, coerced to i64 (a native int),
; loaded with an 'unaligned. 4' ldind.i.
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

; struct { int a, b, c; }: two eightbytes, { i64, i32 }.
; CHECK-LABEL: .method {{.*}}'c_i3'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  call int32 'take_i3'(native int, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @c_i3(ptr nofree noundef readonly captures(none) %p) {
entry:
  %.coerce.sroa.0.0.copyload = load i64, ptr %p, align 4
  %.coerce.sroa.2.0..sroa_idx = getelementptr inbounds nuw i8, ptr %p, i64 8
  %.coerce.sroa.2.0.copyload = load i32, ptr %.coerce.sroa.2.0..sroa_idx, align 4
  %call = tail call i32 @take_i3(i64 %.coerce.sroa.0.0.copyload, i32 %.coerce.sroa.2.0.copyload)
  ret i32 %call
}

; struct { long long a, b; }: two INTEGER eightbytes, two native ints.
; CHECK-LABEL: .method {{.*}}'c_l2'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  call native int 'take_l2'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @c_l2(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load i64, ptr %p, align 8
  %1 = getelementptr inbounds nuw i8, ptr %p, i64 8
  %2 = load i64, ptr %1, align 8
  %call = tail call i64 @take_l2(i64 %0, i64 %2)
  ret i64 %call
}

; struct { double x, y; }: two SSE eightbytes, two float64s.
; CHECK-LABEL: .method {{.*}}'c_d2'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  call float64 'take_d2'(float64, float64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @c_d2(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load double, ptr %p, align 8
  %1 = getelementptr inbounds nuw i8, ptr %p, i64 8
  %2 = load double, ptr %1, align 8
  %call = tail call double @take_d2(double %0, double %2)
  ret double %call
}

; struct { float x, y; }: one SSE eightbyte, <2 x float>, which is split
; into two float32 arguments (no vector registers, D8).
; O0-LABEL: .method {{.*}}'c_f2'(
; O0:       {{^}}  call float32 'take_f2'(float32, float32){{$}}
; O2-LABEL: .method {{.*}}'c_f2'(
; O2-SAME:  native int) cil managed{{$}}
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 3{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldind.r4{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.4{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldind.r4{{$}}
; O2-NEXT:  {{^}}  call float32 'take_f2'(float32, float32){{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define float @c_f2(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load <2 x float>, ptr %p, align 4
  %call = tail call float @take_f2(<2 x float> %0)
  ret float %call
}

; struct { char c[3]; }: coerced to i24, an int32 argument (the i24 is
; loaded in two parts).
; CHECK-LABEL: .method {{.*}}'c_c3'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  call int32 'take_c3'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @c_c3(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load i24, ptr %p, align 1
  %call = tail call i32 @take_c3(i24 %0)
  ret i32 %call
}

; struct { float x, y, z; }: <2 x float> and float, three float32s.
; O0-LABEL: .method {{.*}}'c_f3'(
; O0:       {{^}}  call float32 'take_f3'(float32, float32, float32){{$}}
; O2-LABEL: .method {{.*}}'c_f3'(
; O2-SAME:  native int) cil managed{{$}}
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 4{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldind.r4{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.4{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldind.r4{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.8{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldind.r4{{$}}
; O2-NEXT:  {{^}}  call float32 'take_f3'(float32, float32, float32){{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define float @c_f3(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load <2 x float>, ptr %p, align 4
  %1 = getelementptr inbounds nuw i8, ptr %p, i64 8
  %2 = load float, ptr %1, align 4
  %call = tail call float @take_f3(<2 x float> %0, float %2)
  ret float %call
}

; struct { double d; long long l; }: an SSE and an INTEGER eightbyte.
; CHECK-LABEL: .method {{.*}}'c_dl'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  call float64 'take_dl'(float64, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @c_dl(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load double, ptr %p, align 8
  %1 = getelementptr inbounds nuw i8, ptr %p, i64 8
  %2 = load i64, ptr %1, align 8
  %call = tail call double @take_dl(double %0, i64 %2)
  ret double %call
}

; Returned in one register: a native int result.
; CHECK-LABEL: .method {{.*}}'r_i2'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call native int 'ret_i2'(int32){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @r_i2(i32 noundef %a) {
entry:
  %call = tail call i64 @ret_i2(i32 noundef %a)
  %b = lshr i64 %call, 32
  ret i64 %b
}

; More than 16 bytes: byval. The caller copies the 24 bytes at %p into a new
; 24-byte object of its static frame (D13: 32 bytes, the object at FP + 8)
; before the call, by cpblk or by loads and stores, and passes the address of
; the copy, never %p itself.
; CHECK-LABEL: .method {{.*}}'c_big'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4\.8[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call native int 'take_big'\(native int\)$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'c_big'{{$}}
define i64 @c_big(ptr nofree noundef readonly captures(none) %p) {
entry:
  %call = tail call i64 @take_big(ptr noundef byval(%struct.Big) align 8 %p)
  ret i64 %call
}

; The argument is a local aggregate (at FP + 24): the copy is a second
; object at FP + 0 in a 48-byte frame, filled from FP + 24, and FP is passed.
; CHECK-LABEL: .method {{.*}}'c_big_local'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 48{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+call native int 'take_big'\(native int\)$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @c_big_local(i64 noundef %x) {
entry:
  %b = alloca %struct.Big, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %b)
  store i64 %x, ptr %b, align 8
  %b1 = getelementptr inbounds nuw i8, ptr %b, i64 8
  %add = add nsw i64 %x, 1
  store i64 %add, ptr %b1, align 8
  %c = getelementptr inbounds nuw i8, ptr %b, i64 16
  %add2 = add nsw i64 %x, 2
  store i64 %add2, ptr %c, align 8
  %call = tail call i64 @take_big(ptr noundef nonnull byval(%struct.Big) align 8 %b)
  call void @llvm.lifetime.end.p0(ptr nonnull %b)
  ret i64 %call
}

; { i64, i64 } needs two values: the result is demoted to a 16-byte frame
; slot whose address is the first argument (CanLowerReturn: Outs.size() <= 1).
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
  %call = tail call { i64, i64 } @ret_l2(i64 noundef %a)
  %0 = extractvalue { i64, i64 } %call, 1
  ret i64 %0
}

; { double, double }: demoted the same way.
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
  %call = tail call { double, double } @ret_d2(double noundef %a)
  %0 = extractvalue { double, double } %call, 1
  ret double %0
}

; <2 x float>: two float32 values, demoted; the second one is at slot + 4.
; O0-LABEL: .method {{.*}}'r_f2'(
; O0:       {{^}}  call void 'ret_f2'(native int, float32){{$}}
; O2-LABEL: .method {{.*}}'r_f2'(
; O2-SAME:  float32) cil managed{{$}}
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 2{{$}}
; O2-NEXT:  {{^}}  .locals (native int){{$}}
; O2-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:  {{^}}  localloc{{$}}
; O2-NEXT:  {{^}}  stloc.0{{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.8{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  call void 'ret_f2'(native int, float32){{$}}
; O2-NEXT:  {{^}}  ldloc.0{{$}}
; O2-NEXT:  {{^}}  ldc.i4.8{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ldc.i4.4{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  or{{$}}
; O2-NEXT:  {{^}}  ldind.r4{{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define float @r_f2(float noundef %a) {
entry:
  %call = tail call <2 x float> @ret_f2(float noundef %a)
  %ref.tmp.sroa.0.4.vec.extract = extractelement <2 x float> %call, i64 1
  ret float %ref.tmp.sroa.0.4.vec.extract
}

; More than 16 bytes: clang's sret pointer to the caller's alloca is an
; ordinary first argument.
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

; { i64, i32 }: demoted; the i32 is at slot + 8.
; CHECK-LABEL: .method {{.*}}'r_i3'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'ret_i3'(native int, int32){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @r_i3(i32 noundef %a) {
entry:
  %call = tail call { i64, i32 } @ret_i3(i32 noundef %a)
  %call.fca.1.extract = extractvalue { i64, i32 } %call, 1
  ret i32 %call.fca.1.extract
}

; The sret temporary is copied to the destination afterwards.
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
