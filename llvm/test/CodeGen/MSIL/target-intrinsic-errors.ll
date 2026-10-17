; Target intrinsics are diagnosed in Phase 1 (Design.md 3.2: without the
; codegen switches of phase 3, arch-specific intrinsics are rejected with an
; error naming the intrinsic; ImplementationPlan.md section 2, 'Diagnostics
; leave well-formed output'; M10 fix-loop row for fix-2). MSIL has no target
; intrinsics of its own, and instruction selection cannot select another
; target's (it used to stop with 'LLVM ERROR: Cannot select: intrinsic').
; MSILPrepareIR reports each intrinsic once in every definition that calls it,
; naming the intrinsic, and deletes the definition's body. llc exits with 1
; without 'LLVM ERROR' or a crash report; the intrinsic declarations that stay
; in the module get no P/Invoke and no 'not a Phase-1 import' error; the
; generic llvm.thread.pointer (__builtin_thread_pointer), which instruction
; selection cannot select either, is reported the same way; another generic
; intrinsic is not affected; and the functions without target intrinsics
; compile as usual. The intrinsics are part of the IR whichever backends are
; built (there is no AArch64 backend, decision D9), and the check does not
; depend on the triple: the llvm.aarch64.* call is diagnosed on the aarch64
; MSIL triples, and on the x86_64 ones too.
;
; llc deletes its -o file after an error, so the modules that are checked and
; assembled are taken from stdout.

; DEFINE: %{diag} = FileCheck %s --check-prefix=ERR --implicit-check-not=error: --implicit-check-not=warning: \
; DEFINE:   --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=aarch64-pc-windows-msvc-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}

; The IR that MSILPrepareIR leaves: the diagnosed definitions are
; declarations, and no target intrinsic is called any more.
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=msil-prepare-ir < %s -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=IR --implicit-check-not="call {{.*}}@llvm.x86." \
; RUN:     --implicit-check-not="call {{.*}}@llvm.aarch64." --implicit-check-not="call {{.*}}@llvm.thread.pointer"

; DEFINE: %{il} = FileCheck %s --check-prefix=IL --implicit-check-not=.method --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not="'llvm."
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s > %t.lnx.il
; RUN: %{il} --input-file=%t.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s > %t.win.o0.il
; RUN: %{il} --input-file=%t.win.o0.il
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s > %t.a64.il
; RUN: %{il} --input-file=%t.a64.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.dll %t.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

source_filename = "target-intrinsic-errors.c"

; IL: .assembly 'target-intrinsic-errors'

; A function without intrinsics before the diagnosed ones.
; IR: define i32 @before(i32 %a)
; IL: .method {{.*}}'before'(int32) cil managed{{$}}
define i32 @before(i32 %a) {
  %r = mul i32 %a, 3
  ret i32 %r
}

; _mm_crc32_u32 (immintrin.h, -msse4.2), called twice and reported once.
; ERR: error: {{.*}}in function crc32 i32 (i32, i32): intrinsic 'llvm.x86.sse42.crc32.32.32' is not supported for MSIL{{$}}
; IR: declare i32 @crc32(i32, i32)
define i32 @crc32(i32 %crc, i32 %v) {
  %c1 = call i32 @llvm.x86.sse42.crc32.32.32(i32 %crc, i32 %v)
  %c2 = call i32 @llvm.x86.sse42.crc32.32.32(i32 %c1, i32 %v)
  ret i32 %c2
}

; __rdtsc (ia32intrin.h).
; ERR: error: {{.*}}in function rdtsc i64 (): intrinsic 'llvm.x86.rdtsc' is not supported for MSIL{{$}}
; IR: declare i64 @rdtsc()
define i64 @rdtsc() {
  %t = call i64 @llvm.x86.rdtsc()
  ret i64 %t
}

; _mm_pause (immintrin.h) in a spin loop: a void call.
; ERR: error: {{.*}}in function spin void (ptr): intrinsic 'llvm.x86.sse2.pause' is not supported for MSIL{{$}}
; IR: declare void @spin(ptr)
define void @spin(ptr %flag) {
entry:
  br label %loop

loop:
  call void @llvm.x86.sse2.pause()
  %v = load volatile i32, ptr %flag, align 4
  %done = icmp ne i32 %v, 0
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; _mm_shuffle_epi8 (tmmintrin.h, -mssse3) in a function whose own parameters
; and result are vectors: the declaration that stays has a vector signature,
; which nothing compiles or prints.
; ERR: error: {{.*}}in function shuffle <16 x i8> (<16 x i8>, <16 x i8>): intrinsic 'llvm.x86.ssse3.pshuf.b.128' is not supported for MSIL{{$}}
; IR: declare <16 x i8> @shuffle(<16 x i8>, <16 x i8>)
define <16 x i8> @shuffle(<16 x i8> %a, <16 x i8> %mask) {
  %r = call <16 x i8> @llvm.x86.ssse3.pshuf.b.128(<16 x i8> %a, <16 x i8> %mask)
  ret <16 x i8> %r
}

; Two intrinsics in one definition: each is reported once, at its first call.
; ERR: error: {{.*}}in function timed i64 (i32, i32): intrinsic 'llvm.x86.rdtsc' is not supported for MSIL{{$}}
; ERR: error: {{.*}}in function timed i64 (i32, i32): intrinsic 'llvm.x86.sse42.crc32.32.32' is not supported for MSIL{{$}}
; IR: declare i64 @timed(i32, i32)
define i64 @timed(i32 %crc, i32 %v) {
  %t0 = call i64 @llvm.x86.rdtsc()
  %c = call i32 @llvm.x86.sse42.crc32.32.32(i32 %crc, i32 %v)
  %t1 = call i64 @llvm.x86.rdtsc()
  %d = sub i64 %t1, %t0
  %z = zext i32 %c to i64
  %r = add i64 %d, %z
  ret i64 %r
}

; __crc32w (arm_acle.h, +crc).
; ERR: error: {{.*}}in function crc32w i32 (i32, i32): intrinsic 'llvm.aarch64.crc32w' is not supported for MSIL{{$}}
; IR: declare i32 @crc32w(i32, i32)
define i32 @crc32w(i32 %crc, i32 %v) {
  %r = call i32 @llvm.aarch64.crc32w(i32 %crc, i32 %v)
  ret i32 %r
}

; __builtin_thread_pointer: a generic intrinsic, but CIL has no thread pointer,
; and instruction selection used to stop with 'LLVM ERROR: Cannot select:
; intrinsic %llvm.thread.pointer' on every MSIL triple. Called twice (an
; inlined accessor) and reported once.
; ERR: error: {{.*}}in function tp ptr (): thread pointer access ('llvm.thread.pointer') is not supported for MSIL yet (phase 3 TLS){{$}}
; IR: declare ptr @tp()
define ptr @tp() {
  %p = call ptr @llvm.thread.pointer.p0()
  %q = call ptr @llvm.thread.pointer.p0()
  %c = icmp eq ptr %p, %q
  %r = select i1 %c, ptr %p, ptr null
  ret ptr %r
}

; Another generic intrinsic is not a target intrinsic: it is lowered as usual.
; IR: define i32 @generic(i32 %a, i32 %b)
; IR: call i32 @llvm.umax.i32(i32 %a, i32 %b)
; IL: .method {{.*}}'generic'(int32, int32) cil managed{{$}}
; IL: } // end of method 'generic'{{$}}
define i32 @generic(i32 %a, i32 %b) {
  %r = call i32 @llvm.umax.i32(i32 %a, i32 %b)
  ret i32 %r
}

; The function after them compiles as usual.
; IR: define i32 @after(i32 %a)
; IL-LABEL: .method {{.*}}'after'(
; IL-SAME:  int32) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 2{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.1{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'after'{{$}}
define i32 @after(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

declare i32 @llvm.x86.sse42.crc32.32.32(i32, i32)
declare i64 @llvm.x86.rdtsc()
declare void @llvm.x86.sse2.pause()
declare <16 x i8> @llvm.x86.ssse3.pshuf.b.128(<16 x i8>, <16 x i8>)
declare i32 @llvm.aarch64.crc32w(i32, i32)
declare ptr @llvm.thread.pointer.p0()
declare i32 @llvm.umax.i32(i32, i32)
