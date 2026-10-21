; Struct arguments and results in the shapes clang emits for AAPCS64, the
; aarch64 twin of struct-args-sysv.ll and struct-args-win64.ll
; (ImplementationPlan.md M11.c and decisions D2, D6, D13; Design.md 3 'clang
; reuses its native TargetInfo and ABI lowering'). The input is the IR clang
; -O2 produces for aarch64-unknown-linux-gnu-msil from C functions that take
; and return structs; the callees and the callers were compiled as separate
; translation units, so every call stays, and the datalayout, the triple,
; attributes and TBAA metadata were removed. Upstream de7794fcf781 made clang
; pass a non-HFA aggregate argument of fewer than 8 bytes as an iN of its
; exact size (i8 ... i56, formerly i64) on every aarch64 MSIL triple, so the
; backend now sees odd-width integers on aarch64 too: an i8 to i32 is an
; int32 parameter and an i40 to i64 a native int (D2). [2 x i64] and
; [2 x ptr] are two native ints, the HFAs [2 x float] alignstack(8) and
; [4 x double] alignstack(8) are their float32 and float64 elements, a result
; that needs more than one value (a [2 x i64] or an HFA struct) is demoted to
; a frame slot whose address is the first argument (the method returns void),
; and a 24-byte struct is passed as a pointer to the caller's copy with the
; result through sret, both ordinary pointer parameters. The callers build
; each coerced value from memory with loads of the natural widths (ldind.u1,
; ldind.u2, ldind.i4, ldind.i, with 'unaligned. N' below the natural
; alignment) and combine the parts little-endian with shl and or. No int64 and
; no diagnostic anywhere. aarch64-aggregate-abi.c pins the clang side; this
; test keeps the backend side pinned if a later upstream merge changes clang
; again.
;
; SIG checks every method header; callers (and g3, g5) are exact at -O2 (O2)
; and checked for their calls at -O0 (O0). The same IR runs on the Linux and
; Windows aarch64 triples (Windows would drop the alignstack, which the
; backend ignores), and every module assembles as an ARM64 image ('-arm64'
; after the '-x64' of %ilasm-verify, as in aarch64-triples.ll). Execution
; stays x86_64-only (D6).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=SIG,O2
; RUN: llc -mtriple=aarch64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=SIG,O2
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=SIG,O0
; RUN: llc -mtriple=aarch64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=SIG,O0
; RUN: %if ilasm %{ llc -mtriple=aarch64-unknown-linux-gnu-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -arm64 -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=aarch64-pc-windows-msvc-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -arm64 -output=%t.o0.dll %t.o0.il %}

source_filename = "struct-args-aapcs64.c"

%struct.HFA2 = type { float, float }
%struct.HFA4 = type { double, double, double, double }
%struct.S24 = type { [6 x i32] }

@sink = global i32 0, align 4

; Callees: each takes and returns its aggregate in the AAPCS64 shape.
; struct { char a; }: i8, an int32 in IL (an i64 before de7794fcf781).
; SIG:       {{^}}.method public static int32 'f1'(int32) cil managed{{$}}
define i8 @f1(i8 %x.coerce) {
entry:
  %add = add i8 %x.coerce, 1
  ret i8 %add
}

; struct { short a; }: i16, an int32.
; SIG:       {{^}}.method public static int32 'f2'(int32) cil managed{{$}}
define i16 @f2(i16 %x.coerce) {
entry:
  %add = add i16 %x.coerce, 1
  ret i16 %add
}

; struct { char a, b, c; }: i24, an int32.
; SIG:       {{^}}.method public static int32 'f3'(int32) cil managed{{$}}
define i24 @f3(i24 %x.coerce) {
entry:
  %x.sroa.0.0.extract.trunc = trunc i24 %x.coerce to i8
  %x.sroa.4.0.extract.shift = lshr i24 %x.coerce, 16
  %x.sroa.4.0.extract.trunc = trunc nuw i24 %x.sroa.4.0.extract.shift to i8
  %add = add i8 %x.sroa.4.0.extract.trunc, %x.sroa.0.0.extract.trunc
  %retval.sroa.2.0.insert.ext = zext i8 %add to i24
  %retval.sroa.2.0.insert.shift = shl nuw nsw i24 %retval.sroa.2.0.insert.ext, 8
  %0 = and i24 %x.coerce, -65281
  %retval.sroa.0.0.insert.insert = or disjoint i24 %retval.sroa.2.0.insert.shift, %0
  ret i24 %retval.sroa.0.0.insert.insert
}

; struct { int a; }: i32.
; SIG:       {{^}}.method public static int32 'f4'(int32) cil managed{{$}}
define range(i32 -2147483647, -2147483648) i32 @f4(i32 %x.coerce) {
entry:
  %add = add nsw i32 %x.coerce, 1
  ret i32 %add
}

; struct { char a[5]; }: i40, a native int (an i40 to i56 is promoted to i64).
; SIG:       {{^}}.method public static native int 'f5'(native int) cil managed{{$}}
define i40 @f5(i40 %x.coerce) {
entry:
  %retval.sroa.3.0.insert.ext = and i40 %x.coerce, 255
  %retval.sroa.3.0.insert.shift = shl nuw i40 %retval.sroa.3.0.insert.ext, 32
  %retval.sroa.2.0.insert.ext = and i40 %x.coerce, 4294967040
  %retval.sroa.2.0.insert.insert = or disjoint i40 %retval.sroa.3.0.insert.shift, %retval.sroa.2.0.insert.ext
  %retval.sroa.0.0.insert.insert = or disjoint i40 %retval.sroa.2.0.insert.insert, %retval.sroa.3.0.insert.ext
  ret i40 %retval.sroa.0.0.insert.insert
}

; struct { short a[3]; }: i48, a native int.
; SIG:       {{^}}.method public static native int 'f6'(native int) cil managed{{$}}
define i48 @f6(i48 %x.coerce) {
entry:
  %x.sroa.0.0.extract.trunc = trunc i48 %x.coerce to i16
  %0 = lshr i48 %x.coerce, 32
  %conv3 = trunc nuw i48 %0 to i16
  %add = add i16 %conv3, %x.sroa.0.0.extract.trunc
  %retval.sroa.3.0.insert.ext = zext i16 %add to i48
  %retval.sroa.3.0.insert.shift = shl nuw i48 %retval.sroa.3.0.insert.ext, 32
  %1 = and i48 %x.coerce, 4294967295
  %retval.sroa.0.0.insert.insert = or disjoint i48 %retval.sroa.3.0.insert.shift, %1
  ret i48 %retval.sroa.0.0.insert.insert
}

; struct { char a[7]; }: i56, a native int.
; SIG:       {{^}}.method public static native int 'f7'(native int) cil managed{{$}}
define i56 @f7(i56 %x.coerce) {
entry:
  %x.sroa.2.0.extract.shift = lshr i56 %x.coerce, 8
  %retval.sroa.4.0.insert.ext = and i56 %x.sroa.2.0.extract.shift, 255
  %retval.sroa.4.0.insert.shift = shl nuw i56 %retval.sroa.4.0.insert.ext, 48
  %retval.sroa.3.0.insert.ext = and i56 %x.coerce, 281474976645120
  %retval.sroa.3.0.insert.insert = or disjoint i56 %retval.sroa.4.0.insert.shift, %retval.sroa.3.0.insert.ext
  %retval.sroa.2.0.insert.shift = shl nuw nsw i56 %retval.sroa.4.0.insert.ext, 8
  %retval.sroa.2.0.insert.insert = or disjoint i56 %retval.sroa.3.0.insert.insert, %retval.sroa.2.0.insert.shift
  %retval.sroa.0.0.insert.ext = and i56 %x.coerce, 255
  %retval.sroa.0.0.insert.insert = or disjoint i56 %retval.sroa.2.0.insert.insert, %retval.sroa.0.0.insert.ext
  ret i56 %retval.sroa.0.0.insert.insert
}

; struct { int a, b; }: i64, a native int (unchanged by de7794fcf781).
; SIG:       {{^}}.method public static native int 'f8'(native int) cil managed{{$}}
define i64 @f8(i64 %x.coerce) {
entry:
  %x.sroa.4.0.extract.shift = lshr i64 %x.coerce, 32
  %add = add i64 %x.sroa.4.0.extract.shift, %x.coerce
  %retval.sroa.2.0.insert.shift = and i64 %x.coerce, -4294967296
  %retval.sroa.0.0.insert.ext = and i64 %add, 4294967295
  %retval.sroa.0.0.insert.insert = or disjoint i64 %retval.sroa.0.0.insert.ext, %retval.sroa.2.0.insert.shift
  ret i64 %retval.sroa.0.0.insert.insert
}

; struct { long long a, b; }: [2 x i64], two native ints; the [2 x i64]
; result needs two values and is demoted to a hidden first pointer
; (CanLowerReturn: Outs.size() <= 1), so the method returns void.
; SIG:       {{^}}.method public static void 'f16'(native int, native int, native int) cil managed{{$}}
define [2 x i64] @f16([2 x i64] %x.coerce) {
entry:
  %x.coerce.fca.0.extract = extractvalue [2 x i64] %x.coerce, 0
  %x.coerce.fca.1.extract = extractvalue [2 x i64] %x.coerce, 1
  %add = add nsw i64 %x.coerce.fca.0.extract, %x.coerce.fca.1.extract
  %.fca.0.insert = insertvalue [2 x i64] poison, i64 %add, 0
  %.fca.1.insert = insertvalue [2 x i64] %.fca.0.insert, i64 %x.coerce.fca.1.extract, 1
  ret [2 x i64] %.fca.1.insert
}

; struct { char *a, *b; }: [2 x ptr] as an argument, [2 x i64] as a result.
; SIG:       {{^}}.method public static void 'fsp2'(native int, native int, native int) cil managed{{$}}
define [2 x i64] @fsp2([2 x ptr] %x.coerce) {
entry:
  %x.coerce.fca.0.extract = extractvalue [2 x ptr] %x.coerce, 0
  %x.coerce.fca.1.extract = extractvalue [2 x ptr] %x.coerce, 1
  %0 = ptrtoint ptr %x.coerce.fca.1.extract to i64
  %.fca.0.insert = insertvalue [2 x i64] poison, i64 %0, 0
  %1 = ptrtoint ptr %x.coerce.fca.0.extract to i64
  %.fca.1.insert = insertvalue [2 x i64] %.fca.0.insert, i64 %1, 1
  ret [2 x i64] %.fca.1.insert
}

; HFA struct { float a, b; }: [2 x float] alignstack(8) (alignstack only
; matters to the native ABI); the %struct.HFA2 result is demoted.
; SIG:       {{^}}.method public static void 'fh2'(native int, float32, float32) cil managed{{$}}
define %struct.HFA2 @fh2([2 x float] alignstack(8) %x.coerce) {
entry:
  %x.coerce.fca.0.extract = extractvalue [2 x float] %x.coerce, 0
  %x.coerce.fca.1.extract = extractvalue [2 x float] %x.coerce, 1
  %add = fadd float %x.coerce.fca.0.extract, %x.coerce.fca.1.extract
  %.fca.0.insert = insertvalue %struct.HFA2 poison, float %add, 0
  %.fca.1.insert = insertvalue %struct.HFA2 %.fca.0.insert, float %x.coerce.fca.1.extract, 1
  ret %struct.HFA2 %.fca.1.insert
}

; HFA struct { double a, b, c, d; }: [4 x double] alignstack(8), four
; float64s; the %struct.HFA4 result is demoted.
; SIG:       {{^}}.method public static void 'fh4'(native int, float64, float64, float64, float64) cil managed{{$}}
define %struct.HFA4 @fh4([4 x double] alignstack(8) %x.coerce) {
entry:
  %x.coerce.fca.0.extract = extractvalue [4 x double] %x.coerce, 0
  %x.coerce.fca.1.extract = extractvalue [4 x double] %x.coerce, 1
  %x.coerce.fca.2.extract = extractvalue [4 x double] %x.coerce, 2
  %x.coerce.fca.3.extract = extractvalue [4 x double] %x.coerce, 3
  %add = fadd double %x.coerce.fca.0.extract, %x.coerce.fca.3.extract
  %.fca.0.insert = insertvalue %struct.HFA4 poison, double %add, 0
  %.fca.1.insert = insertvalue %struct.HFA4 %.fca.0.insert, double %x.coerce.fca.1.extract, 1
  %.fca.2.insert = insertvalue %struct.HFA4 %.fca.1.insert, double %x.coerce.fca.2.extract, 2
  %.fca.3.insert = insertvalue %struct.HFA4 %.fca.2.insert, double %x.coerce.fca.3.extract, 3
  ret %struct.HFA4 %.fca.3.insert
}

; struct { int a[6]; } (24 bytes): indirect. The argument is a pointer to
; the caller's copy (dead_on_return) and the result goes through clang's sret
; pointer; both are ordinary pointer parameters, sret first.
; SIG:       {{^}}.method public static void 'fs24'(native int, native int) cil managed{{$}}
define void @fs24(ptr dead_on_unwind noalias nofree writable writeonly sret(%struct.S24) align 4 captures(none) initializes((0, 24)) %agg.result, ptr nofreeobj noundef align 4 captures(none) dead_on_return dereferenceable(24) %x) {
entry:
  %arrayidx = getelementptr inbounds nuw i8, ptr %x, i64 20
  %0 = load i32, ptr %arrayidx, align 4
  %1 = load i32, ptr %x, align 4
  %add = add nsw i32 %1, %0
  store i32 %add, ptr %x, align 4
  tail call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 4 dereferenceable(24) %agg.result, ptr noundef nonnull align 4 dereferenceable(24) %x, i64 24, i1 false)
  ret void
}

; void callees that store to a volatile global, so their calls stay at -O2;
; they read the field out of the coerced integer (an i24 in an int32 and an
; i40 in a native int, whose upper bits are not defined).
; SIG-LABEL: {{^}}.method public static void 'g3'(int32) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 3{{$}}
; O2-NEXT:   {{^}}  ldsfld native int 'sink'{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4.8{{$}}
; O2-NEXT:   {{^}}  shl{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 24{{$}}
; O2-NEXT:   {{^}}  shr{{$}}
; O2-NEXT:   {{^}}  volatile.{{$}}
; O2-NEXT:   {{^}}  stind.i4{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'g3'{{$}}
define void @g3(i24 %x.coerce) {
entry:
  %0 = ashr i24 %x.coerce, 16
  %conv = sext i24 %0 to i32
  store volatile i32 %conv, ptr @sink, align 4
  ret void
}

; SIG-LABEL: {{^}}.method public static void 'g5'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 3{{$}}
; O2-NEXT:   {{^}}  ldsfld native int 'sink'{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 24{{$}}
; O2-NEXT:   {{^}}  shl{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 56{{$}}
; O2-NEXT:   {{^}}  shr{{$}}
; O2-NEXT:   {{^}}  conv.i4{{$}}
; O2-NEXT:   {{^}}  volatile.{{$}}
; O2-NEXT:   {{^}}  stind.i4{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'g5'{{$}}
define void @g5(i40 %x.coerce) {
entry:
  %0 = ashr i40 %x.coerce, 32
  %conv = trunc nsw i40 %0 to i32
  store volatile i32 %conv, ptr @sink, align 4
  ret void
}

; Callers: each builds the coerced value from memory and passes it.
; i8: one ldind.u1.
; SIG-LABEL: {{^}}.method public static int32 'c1'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 1{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldind.u1{{$}}
; O2-NEXT:   {{^}}  call int32 'f1'(int32){{$}}
; O2-NEXT:   {{^}}  conv.i1{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'c1'{{$}}
; O0:        {{^}}  call int32 'f1'(int32){{$}}
define range(i32 -128, 128) i32 @c1(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load i8, ptr %p, align 1
  %call = tail call i8 @f1(i8 %0)
  %conv = sext i8 %call to i32
  ret i32 %conv
}

; i16: one ldind.u2.
; SIG-LABEL: {{^}}.method public static int32 'c2'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 1{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldind.u2{{$}}
; O2-NEXT:   {{^}}  call int32 'f2'(int32){{$}}
; O2-NEXT:   {{^}}  conv.i2{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'c2'{{$}}
; O0:        {{^}}  call int32 'f2'(int32){{$}}
define range(i32 -32768, 32768) i32 @c2(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load i16, ptr %p, align 2
  %call = tail call i16 @f2(i16 %0)
  %conv = sext i16 %call to i32
  ret i32 %conv
}

; i24 at align 1: an unaligned ldind.u2 and the third byte, shifted by 16,
; or-ed into an int32.
; SIG-LABEL: {{^}}.method public static int32 'c3'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 3{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  unaligned. 1{{$}}
; O2-NEXT:   {{^}}  ldind.u2{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 2{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.u1{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:   {{^}}  shl{{$}}
; O2-NEXT:   {{^}}  or{{$}}
; O2-NEXT:   {{^}}  call int32 'f3'(int32){{$}}
; O2-NEXT:   {{^}}  ldc.i4.8{{$}}
; O2-NEXT:   {{^}}  shr.un{{$}}
; O2-NEXT:   {{^}}  conv.i1{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'c3'{{$}}
; O0:        {{^}}  call int32 'f3'(int32){{$}}
define range(i32 -128, 128) i32 @c3(ptr nofree noundef readonly captures(none) %p) {
entry:
  %.coerce.0.copyload = load i24, ptr %p, align 1
  %call = tail call i24 @f3(i24 %.coerce.0.copyload)
  %ref.tmp.sroa.3.0.extract.shift = lshr i24 %call, 8
  %ref.tmp.sroa.3.0.extract.trunc = zext nneg i24 %ref.tmp.sroa.3.0.extract.shift to i32
  %sext = shl i32 %ref.tmp.sroa.3.0.extract.trunc, 24
  %conv = ashr exact i32 %sext, 24
  ret i32 %conv
}

; i32: one ldind.i4.
; SIG-LABEL: {{^}}.method public static int32 'c4'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 1{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldind.i4{{$}}
; O2-NEXT:   {{^}}  call int32 'f4'(int32){{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'c4'{{$}}
; O0:        {{^}}  call int32 'f4'(int32){{$}}
define i32 @c4(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load i32, ptr %p, align 4
  %call = tail call i32 @f4(i32 %0)
  ret i32 %call
}

; i40 at align 1: an unaligned ldind.i4 and the fifth byte, each widened to
; native int, the byte shifted by 32.
; SIG-LABEL: {{^}}.method public static int32 'c5'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 3{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  unaligned. 1{{$}}
; O2-NEXT:   {{^}}  ldind.i4{{$}}
; O2-NEXT:   {{^}}  conv.u{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 4{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.u1{{$}}
; O2-NEXT:   {{^}}  conv.u{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 32{{$}}
; O2-NEXT:   {{^}}  shl{{$}}
; O2-NEXT:   {{^}}  or{{$}}
; O2-NEXT:   {{^}}  call native int 'f5'(native int){{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 24{{$}}
; O2-NEXT:   {{^}}  shl{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 56{{$}}
; O2-NEXT:   {{^}}  shr{{$}}
; O2-NEXT:   {{^}}  conv.i4{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'c5'{{$}}
; O0:        {{^}}  call native int 'f5'(native int){{$}}
define range(i32 -128, 128) i32 @c5(ptr nofree noundef readonly captures(none) %p) {
entry:
  %coerce.dive.coerce.0.copyload = load i40, ptr %p, align 1
  %call = tail call i40 @f5(i40 %coerce.dive.coerce.0.copyload)
  %0 = ashr i40 %call, 32
  %conv = trunc nsw i40 %0 to i32
  ret i32 %conv
}

; i48 at align 2: an 'unaligned. 2' ldind.i4 and an ldind.u2 at +4.
; SIG-LABEL: {{^}}.method public static int32 'c6'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 3{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  unaligned. 2{{$}}
; O2-NEXT:   {{^}}  ldind.i4{{$}}
; O2-NEXT:   {{^}}  conv.u{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 4{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.u2{{$}}
; O2-NEXT:   {{^}}  conv.u{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 32{{$}}
; O2-NEXT:   {{^}}  shl{{$}}
; O2-NEXT:   {{^}}  or{{$}}
; O2-NEXT:   {{^}}  call native int 'f6'(native int){{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:   {{^}}  shl{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 48{{$}}
; O2-NEXT:   {{^}}  shr{{$}}
; O2-NEXT:   {{^}}  conv.i4{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'c6'{{$}}
; O0:        {{^}}  call native int 'f6'(native int){{$}}
define range(i32 -32768, 32768) i32 @c6(ptr nofree noundef readonly captures(none) %p) {
entry:
  %coerce.dive.coerce.0.copyload = load i48, ptr %p, align 2
  %call = tail call i48 @f6(i48 %coerce.dive.coerce.0.copyload)
  %0 = ashr i48 %call, 32
  %conv = trunc nsw i48 %0 to i32
  ret i32 %conv
}

; i56 at align 1: four bytes, then two and one byte combined as an int32,
; shifted by 32.
; SIG-LABEL: {{^}}.method public static int32 'c7'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 4{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  unaligned. 1{{$}}
; O2-NEXT:   {{^}}  ldind.i4{{$}}
; O2-NEXT:   {{^}}  conv.u{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 4{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  unaligned. 1{{$}}
; O2-NEXT:   {{^}}  ldind.u2{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 6{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.u1{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:   {{^}}  shl{{$}}
; O2-NEXT:   {{^}}  or{{$}}
; O2-NEXT:   {{^}}  conv.u{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 32{{$}}
; O2-NEXT:   {{^}}  shl{{$}}
; O2-NEXT:   {{^}}  or{{$}}
; O2-NEXT:   {{^}}  call native int 'f7'(native int){{$}}
; O2-NEXT:   {{^}}  ldc.i4.8{{$}}
; O2-NEXT:   {{^}}  shl{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 56{{$}}
; O2-NEXT:   {{^}}  shr{{$}}
; O2-NEXT:   {{^}}  conv.i4{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'c7'{{$}}
; O0:        {{^}}  call native int 'f7'(native int){{$}}
define range(i32 -128, 128) i32 @c7(ptr nofree noundef readonly captures(none) %p) {
entry:
  %coerce.dive.coerce.0.copyload = load i56, ptr %p, align 1
  %call = tail call i56 @f7(i56 %coerce.dive.coerce.0.copyload)
  %0 = ashr i56 %call, 48
  %conv = trunc nsw i56 %0 to i32
  ret i32 %conv
}

; i64 at align 4: one 'unaligned. 4' ldind.i.
; SIG-LABEL: {{^}}.method public static int32 'c8'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 1{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  unaligned. 4{{$}}
; O2-NEXT:   {{^}}  ldind.i{{$}}
; O2-NEXT:   {{^}}  call native int 'f8'(native int){{$}}
; O2-NEXT:   {{^}}  conv.i4{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'c8'{{$}}
; O0:        {{^}}  call native int 'f8'(native int){{$}}
define i32 @c8(ptr nofree noundef readonly captures(none) %p) {
entry:
  %0 = load i64, ptr %p, align 4
  %call = tail call i64 @f8(i64 %0)
  %ref.tmp.sroa.0.0.extract.trunc = trunc i64 %call to i32
  ret i32 %ref.tmp.sroa.0.0.extract.trunc
}

; [2 x i64]: two ldind.i; the result is demoted to a 16-byte frame slot
; (localloc, D13) whose address is the first argument.
; SIG-LABEL: {{^}}.method public static native int 'c16'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 4{{$}}
; O2-NEXT:   {{^}}  .locals (native int){{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:   {{^}}  localloc{{$}}
; O2-NEXT:   {{^}}  stloc.0{{$}}
; O2-NEXT:   {{^}}  ldloc.0{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldind.i{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 8{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.i{{$}}
; O2-NEXT:   {{^}}  call void 'f16'(native int, native int, native int){{$}}
; O2-NEXT:   {{^}}  ldloc.0{{$}}
; O2-NEXT:   {{^}}  ldind.i{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'c16'{{$}}
; O0:        {{^}}  call void 'f16'(native int, native int, native int){{$}}
define i64 @c16(ptr nofree noundef readonly captures(none) %p) {
entry:
  %.unpack = load i64, ptr %p, align 8
  %0 = insertvalue [2 x i64] poison, i64 %.unpack, 0
  %.elt1 = getelementptr inbounds nuw i8, ptr %p, i64 8
  %.unpack2 = load i64, ptr %.elt1, align 8
  %1 = insertvalue [2 x i64] %0, i64 %.unpack2, 1
  %call = tail call [2 x i64] @f16([2 x i64] %1)
  %call.fca.0.extract = extractvalue [2 x i64] %call, 0
  ret i64 %call.fca.0.extract
}

; [2 x ptr]: the same shape as [2 x i64].
; SIG-LABEL: {{^}}.method public static native int 'csp2'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 4{{$}}
; O2-NEXT:   {{^}}  .locals (native int){{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:   {{^}}  localloc{{$}}
; O2-NEXT:   {{^}}  stloc.0{{$}}
; O2-NEXT:   {{^}}  ldloc.0{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldind.i{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 8{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.i{{$}}
; O2-NEXT:   {{^}}  call void 'fsp2'(native int, native int, native int){{$}}
; O2-NEXT:   {{^}}  ldloc.0{{$}}
; O2-NEXT:   {{^}}  ldind.i{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'csp2'{{$}}
; O0:        {{^}}  call void 'fsp2'(native int, native int, native int){{$}}
define ptr @csp2(ptr nofree noundef readonly captures(none) %p) {
entry:
  %.unpack = load ptr, ptr %p, align 8
  %0 = insertvalue [2 x ptr] poison, ptr %.unpack, 0
  %.elt1 = getelementptr inbounds nuw i8, ptr %p, i64 8
  %.unpack2 = load ptr, ptr %.elt1, align 8
  %1 = insertvalue [2 x ptr] %0, ptr %.unpack2, 1
  %call = tail call [2 x i64] @fsp2([2 x ptr] %1)
  %call.fca.0.extract = extractvalue [2 x i64] %call, 0
  %2 = inttoptr i64 %call.fca.0.extract to ptr
  ret ptr %2
}

; [2 x float]: two ldind.r4; the HFA result is demoted to a frame slot.
; SIG-LABEL: {{^}}.method public static float32 'ch2'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 4{{$}}
; O2-NEXT:   {{^}}  .locals (native int){{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:   {{^}}  localloc{{$}}
; O2-NEXT:   {{^}}  stloc.0{{$}}
; O2-NEXT:   {{^}}  ldloc.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 8{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldind.r4{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 4{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.r4{{$}}
; O2-NEXT:   {{^}}  call void 'fh2'(native int, float32, float32){{$}}
; O2-NEXT:   {{^}}  ldloc.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 8{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.r4{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'ch2'{{$}}
; O0:        {{^}}  call void 'fh2'(native int, float32, float32){{$}}
define float @ch2(ptr nofree noundef readonly captures(none) %p) {
entry:
  %.unpack = load float, ptr %p, align 4
  %0 = insertvalue [2 x float] poison, float %.unpack, 0
  %.elt1 = getelementptr inbounds nuw i8, ptr %p, i64 4
  %.unpack2 = load float, ptr %.elt1, align 4
  %1 = insertvalue [2 x float] %0, float %.unpack2, 1
  %call = tail call %struct.HFA2 @fh2([2 x float] alignstack(8) %1)
  %2 = extractvalue %struct.HFA2 %call, 0
  ret float %2
}

; [4 x double]: four ldind.r8.
; SIG-LABEL: {{^}}.method public static float64 'ch4'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 6{{$}}
; O2-NEXT:   {{^}}  .locals (native int){{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 32{{$}}
; O2-NEXT:   {{^}}  localloc{{$}}
; O2-NEXT:   {{^}}  stloc.0{{$}}
; O2-NEXT:   {{^}}  ldloc.0{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldind.r8{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 8{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.r8{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 16{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.r8{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 24{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.r8{{$}}
; O2-NEXT:   {{^}}  call void 'fh4'(native int, float64, float64, float64, float64){{$}}
; O2-NEXT:   {{^}}  ldloc.0{{$}}
; O2-NEXT:   {{^}}  ldind.r8{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'ch4'{{$}}
; O0:        {{^}}  call void 'fh4'(native int, float64, float64, float64, float64){{$}}
define double @ch4(ptr nofree noundef readonly captures(none) %p) {
entry:
  %.unpack = load double, ptr %p, align 8
  %0 = insertvalue [4 x double] poison, double %.unpack, 0
  %.elt1 = getelementptr inbounds nuw i8, ptr %p, i64 8
  %.unpack2 = load double, ptr %.elt1, align 8
  %1 = insertvalue [4 x double] %0, double %.unpack2, 1
  %.elt3 = getelementptr inbounds nuw i8, ptr %p, i64 16
  %.unpack4 = load double, ptr %.elt3, align 8
  %2 = insertvalue [4 x double] %1, double %.unpack4, 2
  %.elt5 = getelementptr inbounds nuw i8, ptr %p, i64 24
  %.unpack6 = load double, ptr %.elt5, align 8
  %3 = insertvalue [4 x double] %2, double %.unpack6, 3
  %call = tail call %struct.HFA4 @fh4([4 x double] alignstack(8) %3)
  %4 = extractvalue %struct.HFA4 %call, 0
  ret double %4
}

; Indirect: a 48-byte frame holds the copy at FP + 0 and the sret slot at
; FP + 24; the caller copies the 24 bytes (cpblk), then passes the sret
; slot's address first and the copy's second.
; SIG-LABEL: {{^}}.method public static int32 'cs24'(native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 3{{$}}
; O2-NEXT:   {{^}}  .locals (native int){{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 48{{$}}
; O2-NEXT:   {{^}}  localloc{{$}}
; O2-NEXT:   {{^}}  stloc.0{{$}}
; O2-NEXT:   {{^}}  ldloc.0{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 24{{$}}
; O2-NEXT:   {{^}}  unaligned. 4{{$}}
; O2-NEXT:   {{^}}  cpblk{{$}}
; O2-NEXT:   {{^}}  ldloc.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 24{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldloc.0{{$}}
; O2-NEXT:   {{^}}  call void 'fs24'(native int, native int){{$}}
; O2-NEXT:   {{^}}  ldloc.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 24{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.i4{{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'cs24'{{$}}
; O0:        {{^}}  call void 'fs24'(native int, native int){{$}}
define i32 @cs24(ptr nofree noundef readonly captures(none) %p) {
entry:
  %ref.tmp = alloca %struct.S24, align 4
  %byval-temp = alloca %struct.S24, align 4
  call void @llvm.lifetime.start.p0(ptr nonnull %ref.tmp)
  call void @llvm.lifetime.start.p0(ptr nonnull %byval-temp)
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 4 dereferenceable(24) %byval-temp, ptr noundef nonnull align 4 dereferenceable(24) %p, i64 24, i1 false)
  call void @fs24(ptr dead_on_unwind nonnull writable sret(%struct.S24) align 4 %ref.tmp, ptr nofreeobj noundef nonnull align 4 dead_on_return dereferenceable(24) %byval-temp)
  call void @llvm.lifetime.end.p0(ptr nonnull %byval-temp)
  %0 = load i32, ptr %ref.tmp, align 4
  call void @llvm.lifetime.end.p0(ptr nonnull %ref.tmp)
  ret i32 %0
}

; The call sites of aarch64-aggregate-abi.c's call_small: the i24 is packed
; little-endian as ldind.u2 | ldind.u1 << 16, the i40 as ldind.i4 | ldind.u1 << 32.
; SIG-LABEL: {{^}}.method public static void 'call_small'(native int, native int) cil managed{{$}}
; O2-NEXT:   {{^\{$}}
; O2-NEXT:   {{^}}  .maxstack 3{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  unaligned. 1{{$}}
; O2-NEXT:   {{^}}  ldind.u2{{$}}
; O2-NEXT:   {{^}}  ldarg.0{{$}}
; O2-NEXT:   {{^}}  ldc.i4 2{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.u1{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 16{{$}}
; O2-NEXT:   {{^}}  shl{{$}}
; O2-NEXT:   {{^}}  or{{$}}
; O2-NEXT:   {{^}}  call void 'g3'(int32){{$}}
; O2-NEXT:   {{^}}  ldarg.1{{$}}
; O2-NEXT:   {{^}}  unaligned. 1{{$}}
; O2-NEXT:   {{^}}  ldind.i4{{$}}
; O2-NEXT:   {{^}}  conv.u{{$}}
; O2-NEXT:   {{^}}  ldarg.1{{$}}
; O2-NEXT:   {{^}}  ldc.i4 4{{$}}
; O2-NEXT:   {{^}}  conv.i{{$}}
; O2-NEXT:   {{^}}  add{{$}}
; O2-NEXT:   {{^}}  ldind.u1{{$}}
; O2-NEXT:   {{^}}  conv.u{{$}}
; O2-NEXT:   {{^}}  ldc.i4.s 32{{$}}
; O2-NEXT:   {{^}}  shl{{$}}
; O2-NEXT:   {{^}}  or{{$}}
; O2-NEXT:   {{^}}  call void 'g5'(native int){{$}}
; O2-NEXT:   {{^}}  ret{{$}}
; O2-NEXT:   {{^}}} // end of method 'call_small'{{$}}
; O0-DAG:    {{^}}  ldind.u2{{$}}
; O0-DAG:    {{^}}  ldind.u1{{$}}
; O0-DAG:    {{^}}  ldc.i4.s 16{{$}}
; O0-DAG:    {{^}}  shl{{$}}
; O0:        {{^}}  or{{$}}
; O0-NEXT:   {{^}}  call void 'g3'(int32){{$}}
; O0-DAG:    {{^}}  ldind.i4{{$}}
; O0-DAG:    {{^}}  ldind.u1{{$}}
; O0-DAG:    {{^}}  ldc.i4.s 32{{$}}
; O0-DAG:    {{^}}  shl{{$}}
; O0:        {{^}}  or{{$}}
; O0-NEXT:   {{^}}  call void 'g5'(native int){{$}}
define void @call_small(ptr nofree noundef readonly captures(none) %p, ptr nofree noundef readonly captures(none) %q) {
entry:
  %.coerce.0.copyload = load i24, ptr %p, align 1
  tail call void @g3(i24 %.coerce.0.copyload)
  %coerce.dive.coerce.0.copyload = load i40, ptr %q, align 1
  tail call void @g5(i40 %coerce.dive.coerce.0.copyload)
  ret void
}

declare void @llvm.memcpy.p0.p0.i64(ptr noalias writeonly captures(none), ptr noalias readonly captures(none), i64, i1 immarg)
declare void @llvm.lifetime.start.p0(ptr captures(none))
declare void @llvm.lifetime.end.p0(ptr captures(none))
