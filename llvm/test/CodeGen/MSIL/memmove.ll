; memmove (ImplementationPlan.md decision D15, M7.a; recon isel.md 5.8 and
; 11.3; cil-runtime e19): cpblk has unspecified behaviour for overlapping
; ranges (ECMA-335 III.3.30), so every memmove that is not expanded inline is a
; call of [mscorlib]System.Buffer::MemoryCopy(void* source, void* destination,
; int64 destinationSizeInBytes, int64 sourceBytesToCopy), which handles
; overlap and exists on .NET Framework 4.8 and every .NET (NativeMemory is
; net6+ only). MSILISD::MEMMOVE takes (src, dst, len): the source address is
; pushed first, then the destination, then the native int length, which
; 'conv.i8; dup' turns into the two int64 arguments, so the record needs one
; extra stack slot (StackPeakExtra 1, .maxstack 4). An int32 length is
; zero-extended (conv.u) first. The call is a BCL member reference, never a
; P/Invoke or a library call of 'memmove'. PEVerify reports the native int
; arguments, which the D21 allow-list accepts; execution tests run it
; (msil/test/M1/05-memory). A short constant memmove may be expanded into
; loads followed by stores, which is correct for overlap; only the absence of
; a library call is checked for it.
;
; Every function is a single intrinsic on arguments, so the CIL is the same at
; -O0 and -O2.

; DEFINE: %{check} = FileCheck %s --implicit-check-not="'memmove'(" --implicit-check-not="'memcpy'(" \
; DEFINE:   --implicit-check-not=pinvokeimpl --implicit-check-not=cpblk --implicit-check-not=localloc \
; DEFINE:   --implicit-check-not=NativeMemory
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "memmove.c"

declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memmove.p0.p0.i32(ptr, ptr, i32, i1)

; CHECK-LABEL: .method {{.*}}'move_var'(
; CHECK-SAME:  native int, native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'move_var'{{$}}
define void @move_var(ptr %d, ptr %s, i64 %n) {
  call void @llvm.memmove.p0.p0.i64(ptr %d, ptr %s, i64 %n, i1 false)
  ret void
}

; Overlapping ranges inside one buffer: dst = p + 1, src = p.
; CHECK-LABEL: .method {{.*}}'move_overlap'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.1|ldc\.i4 1}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @move_overlap(ptr %p, i64 %n) {
  %d = getelementptr inbounds i8, ptr %p, i64 1
  call void @llvm.memmove.p0.p0.i64(ptr %d, ptr %p, i64 %n, i1 false)
  ret void
}

; A constant length too long to expand inline is still the call (never cpblk).
; CHECK-LABEL: .method {{.*}}'move_1000'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 1000{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @move_1000(ptr %d, ptr %s) {
  call void @llvm.memmove.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 1000, i1 false)
  ret void
}

; Above 4 GiB as well.
; CHECK-LABEL: .method {{.*}}'move_4gib'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 4294967296{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @move_4gib(ptr %d, ptr %s) {
  call void @llvm.memmove.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 4294967296, i1 false)
  ret void
}

; An int32 length is zero-extended to native int (conv.u), never
; sign-extended: a length of 2^31 or more stays positive.
; CHECK-LABEL: .method {{.*}}'move_var_i32len'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @move_var_i32len(ptr %d, ptr %s, i32 %n) {
  call void @llvm.memmove.p0.p0.i32(ptr %d, ptr %s, i32 %n, i1 false)
  ret void
}

; The call has no result and leaves the stack empty: a second memmove follows
; at depth 0, and the two calls stay in order.
; CHECK-LABEL: .method {{.*}}'move_twice'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @move_twice(ptr %a, ptr %b, i64 %n) {
  call void @llvm.memmove.p0.p0.i64(ptr %a, ptr %b, i64 %n, i1 false)
  call void @llvm.memmove.p0.p0.i64(ptr %b, ptr %a, i64 %n, i1 false)
  ret void
}

; A short constant memmove: loads before stores, or the call; no library call.
; CHECK-LABEL: .method {{.*}}'move_8'(
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'move_8'{{$}}
define void @move_8(ptr %d, ptr %s) {
  call void @llvm.memmove.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 8, i1 false)
  ret void
}

; A zero-length memmove is nothing.
; CHECK-LABEL: .method {{.*}}'move_0'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'move_0'{{$}}
define void @move_0(ptr %d, ptr %s) {
  call void @llvm.memmove.p0.p0.i64(ptr %d, ptr %s, i64 0, i1 false)
  ret void
}
