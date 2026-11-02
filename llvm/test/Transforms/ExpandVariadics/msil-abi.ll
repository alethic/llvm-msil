; The MSIL ABIs of ExpandVariadics (ImplementationPlan.md 5.15.2 'The
; ExpandVariadics hook', M26). VariadicABIInfo::create returns, for an MSIL
; triple, MSILSysV64 (x86_64 off Windows), MSILAAPCS64 (aarch64 off Windows)
; or MSILCharPtr (Windows triples):
; - SysV: va_list [1 x {i32, i32, ptr, ptr}] (24 bytes) passed by pointer,
;   gp_offset 48, fp_offset 176, overflow_arg_area = the buffer,
;   reg_save_area null; 8-byte slots, aligned to the C alignment at which
;   clang's x86-64 va_arg reads them: 8, or the type's alignment when that
;   exceeds 8 (16 for an i128), and for a byval argument at least its
;   call-site alignment (section 12 row '5.15.2's SysV `slotInfo` amended
;   (review r1, slot alignment)'); never indirect (a byval argument is copied
;   by value into the buffer).
; - AAPCS64: va_list {ptr, ptr, ptr, i32, i32} (32 bytes) passed by pointer,
;   __stack = the buffer, __gr_top/__vr_top null, __gr_offs/__vr_offs 0;
;   8-byte slots, 16 for types aligned above 8; never indirect.
; - Win64 (x86_64 windows-msvc and windows-gnu): va_list is the buffer
;   pointer itself (char*); 8-byte slots; a value whose size is not 1, 2, 4
;   or 8 goes by pointer to a caller copy.
; - ARM64 Windows: as Win64, indirect only above 16 bytes.
; The buffer is the packed '<caller>.vararg' alloca aligned to max(field
; alignment, 16). Every ABI
; ignores functions with "no-prototype" or "msil-fixed-arity": their
; declarations stay variadic and their calls are left alone.

; REQUIRES: msil-registered-target
; DEFINE: %{t} =
; DEFINE: %{p} =
; DEFINE: %{run} = opt -mtriple=%{t} -S --passes=expand-variadics --expand-variadics-override=lowering < %s \
; DEFINE:   | FileCheck %s --check-prefixes=CHECK,%{p}

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{p} = SYSV,VALIST,BYVALUE
; RUN: %{run}
; REDEFINE: %{t} = x86_64-unknown-linux-musl-msil
; RUN: %{run}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; REDEFINE: %{p} = AAPCS,VALIST,BYVALUE
; RUN: %{run}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{p} = WIN,WIN64
; RUN: %{run}
; REDEFINE: %{t} = x86_64-w64-windows-gnu-msil
; RUN: %{run}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; REDEFINE: %{p} = WIN,WINA64
; RUN: %{run}

; The buffer layouts: 8-byte slots (the i32 gets 4 bytes of padding), the
; i128 a 16-aligned slot of its own on SysV, AAPCS64 and ARM64 Windows and a
; pointer on Win64; the 24-byte byval structure by value except on the
; Windows triples; the 12-byte structure by value except on Win64.
; SYSV-DAG:   %pass_mixed.vararg = type <{ i32, [4 x i8], i64, double, ptr, i128 }>
; AAPCS-DAG:  %pass_mixed.vararg = type <{ i32, [4 x i8], i64, double, ptr, i128 }>
; WINA64-DAG: %pass_mixed.vararg = type <{ i32, [4 x i8], i64, double, ptr, i128 }>
; WIN64-DAG:  %pass_mixed.vararg = type <{ i32, [4 x i8], i64, double, ptr, ptr }>
; BYVALUE-DAG: %pass_byval.vararg = type <{ %struct.big }>
; WIN-DAG:    %pass_byval.vararg = type <{ ptr }>
; BYVALUE-DAG: %pass_s12.vararg = type <{ %struct.s12 }>
; WINA64-DAG: %pass_s12.vararg = type <{ %struct.s12 }>
; WIN64-DAG:  %pass_s12.vararg = type <{ ptr }>
; The over-aligned byval structures (8-aligned IR types) after an i32: on SysV
; at offset 16 (byval align 16) and 32 (byval align 32); on Win64 both are
; indirect; on ARM64 Windows the 16-byte one is by value in an 8-byte slot and
; the 24-byte one indirect. AAPCS64 is not pinned: clang never passes an
; aggregate byval there.
; SYSV-DAG:   %pass_byval_al16.vararg = type <{ i32, [12 x i8], %struct.two }>
; SYSV-DAG:   %pass_byval_al32.vararg = type <{ i32, [28 x i8], %struct.big }>
; WIN64-DAG:  %pass_byval_al16.vararg = type <{ i32, [4 x i8], ptr }>
; WIN64-DAG:  %pass_byval_al32.vararg = type <{ i32, [4 x i8], ptr }>
; WINA64-DAG: %pass_byval_al16.vararg = type <{ i32, [4 x i8], %struct.two }>
; WINA64-DAG: %pass_byval_al32.vararg = type <{ i32, [4 x i8], ptr }>
; CHECK-DAG:  %pass_indirect.vararg = type <{ i32, [4 x i8], double }>
; CHECK-DAG:  %call_vdef.vararg = type <{ i32 }>

%struct.big = type { i64, i64, i64 }
%struct.s12 = type { i32, i32, i32 }
%struct.two = type { i64, i64 }

; CHECK-LABEL: declare void @sink(i32, ptr)
declare void @sink(i32, ...)

; CHECK-LABEL: define void @pass_mixed(ptr %p)
; WIN64:       %IndirectAlloca = alloca i128, align 16
; CHECK:       %vararg_buffer = alloca %pass_mixed.vararg, align 16
; SYSV:        %va_argument = alloca [1 x { i32, i32, ptr, ptr }]
; AAPCS:       %va_argument = alloca { ptr, ptr, ptr, i32, i32 }
; WIN64:       store i128 9, ptr %IndirectAlloca, align 16
; CHECK:       [[F0:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_mixed.vararg, ptr %vararg_buffer, i32 0, i32 0
; CHECK-NEXT:  store i32 7, ptr [[F0]]
; CHECK:       [[F2:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_mixed.vararg, ptr %vararg_buffer, i32 0, i32 2
; CHECK-NEXT:  store i64 8, ptr [[F2]]
; CHECK:       [[F3:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_mixed.vararg, ptr %vararg_buffer, i32 0, i32 3
; CHECK-NEXT:  store double 2.500000e+00, ptr [[F3]]
; CHECK:       [[F4:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_mixed.vararg, ptr %vararg_buffer, i32 0, i32 4
; CHECK-NEXT:  store ptr %p, ptr [[F4]]
; CHECK:       [[F5:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_mixed.vararg, ptr %vararg_buffer, i32 0, i32 5
; SYSV-NEXT:   store i128 9, ptr [[F5]], align 16
; AAPCS-NEXT:  store i128 9, ptr [[F5]], align 16
; WINA64-NEXT: store i128 9, ptr [[F5]], align 16
; WIN64-NEXT:  store ptr %IndirectAlloca, ptr [[F5]]
; The SysV va_list: registers exhausted, the buffer as the overflow area.
; SYSV:        store i32 48, ptr %va_argument
; SYSV:        [[FP:%.*]] = getelementptr inbounds i8, ptr %va_argument, i64 4
; SYSV-NEXT:   store i32 176, ptr [[FP]]
; SYSV:        [[OV:%.*]] = getelementptr inbounds i8, ptr %va_argument, i64 8
; SYSV-NEXT:   store ptr %vararg_buffer, ptr [[OV]]
; SYSV:        [[RS:%.*]] = getelementptr inbounds i8, ptr %va_argument, i64 16
; SYSV-NEXT:   store ptr null, ptr [[RS]]
; The AAPCS64 va_list: __stack is the buffer, the register areas are empty.
; AAPCS:       store ptr %vararg_buffer, ptr %va_argument
; AAPCS:       [[GT:%.*]] = getelementptr inbounds i8, ptr %va_argument, i64 8
; AAPCS-NEXT:  store ptr null, ptr [[GT]]
; AAPCS:       [[VT:%.*]] = getelementptr inbounds i8, ptr %va_argument, i64 16
; AAPCS-NEXT:  store ptr null, ptr [[VT]]
; AAPCS:       [[GO:%.*]] = getelementptr inbounds i8, ptr %va_argument, i64 24
; AAPCS-NEXT:  store i32 0, ptr [[GO]]
; AAPCS:       [[VO:%.*]] = getelementptr inbounds i8, ptr %va_argument, i64 28
; AAPCS-NEXT:  store i32 0, ptr [[VO]]
; VALIST:      call void @sink(i32 5, ptr %va_argument)
; WIN:         call void @sink(i32 5, ptr %vararg_buffer)
; CHECK:       ret void
define void @pass_mixed(ptr %p) {
  call void (i32, ...) @sink(i32 5, i32 7, i64 8, double 2.5, ptr %p, i128 9)
  ret void
}

; A byval structure: copied into the buffer, or on the Windows triples into a
; caller copy whose address goes into the buffer.
; CHECK-LABEL: define void @pass_byval(ptr %s)
; WIN:         %IndirectAlloca = alloca %struct.big
; CHECK:       %vararg_buffer = alloca %pass_byval.vararg, align 16
; BYVALUE:     [[B0:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval.vararg, ptr %vararg_buffer, i32 0, i32 0
; BYVALUE-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr [[B0]], ptr %s, i64 24, i1 false)
; WIN:         call void @llvm.memcpy.p0.p0.i64(ptr %IndirectAlloca, ptr %s, i64 24, i1 false)
; WIN:         [[W0:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval.vararg, ptr %vararg_buffer, i32 0, i32 0
; WIN-NEXT:    store ptr %IndirectAlloca, ptr [[W0]]
; VALIST:      call void @sink(i32 1, ptr %va_argument)
; WIN:         call void @sink(i32 1, ptr %vararg_buffer)
define void @pass_byval(ptr %s) {
  call void (i32, ...) @sink(i32 1, ptr byval(%struct.big) align 8 %s)
  ret void
}

; Over-aligned byval structures (clang passes an unnamed aggregate whose C
; alignment exceeds 8 so on the SysV MSIL triples): the slot takes the
; call-site alignment, not the IR type's 8, and the buffer is aligned to at
; least the slot. The callee's va_arg reads the value 16- and 32-aligned.
; CHECK-LABEL: define void @pass_byval_al16(ptr %s)
; SYSV:        %vararg_buffer = alloca %pass_byval_al16.vararg, align 16
; SYSV:        [[A0:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_al16.vararg, ptr %vararg_buffer, i32 0, i32 0
; SYSV-NEXT:   store i32 2, ptr [[A0]]
; SYSV:        [[A2:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_al16.vararg, ptr %vararg_buffer, i32 0, i32 2
; SYSV-NEXT:   call void @llvm.memcpy.p0.p0.i64(ptr [[A2]], ptr %s, i64 16, i1 false)
; SYSV:        call void @sink(i32 1, ptr %va_argument)
; WIN64:       call void @llvm.memcpy.p0.p0.i64(ptr %IndirectAlloca, ptr %s, i64 16, i1 false)
; WIN:         call void @sink(i32 1, ptr %vararg_buffer)
; CHECK:       ret void
define void @pass_byval_al16(ptr %s) {
  call void (i32, ...) @sink(i32 1, i32 2, ptr byval(%struct.two) align 16 %s)
  ret void
}

; CHECK-LABEL: define void @pass_byval_al32(ptr %s)
; SYSV:        %vararg_buffer = alloca %pass_byval_al32.vararg, align 32
; SYSV:        [[B0:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_al32.vararg, ptr %vararg_buffer, i32 0, i32 0
; SYSV-NEXT:   store i32 2, ptr [[B0]]
; SYSV:        [[B2:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_al32.vararg, ptr %vararg_buffer, i32 0, i32 2
; SYSV-NEXT:   call void @llvm.memcpy.p0.p0.i64(ptr [[B2]], ptr %s, i64 24, i1 false)
; SYSV:        call void @sink(i32 1, ptr %va_argument)
; WIN:         call void @llvm.memcpy.p0.p0.i64(ptr %IndirectAlloca, ptr %s, i64 24, i1 false)
; WIN:         call void @sink(i32 1, ptr %vararg_buffer)
; CHECK:       ret void
define void @pass_byval_al32(ptr %s) {
  call void (i32, ...) @sink(i32 1, i32 2, ptr byval(%struct.big) align 32 %s)
  ret void
}

; A 12-byte structure passed as a first-class aggregate: by value, except on
; Win64 (12 is not 1, 2, 4 or 8).
; CHECK-LABEL: define void @pass_s12(ptr %s)
; WIN64:       %IndirectAlloca = alloca %struct.s12
; CHECK:       %vararg_buffer = alloca %pass_s12.vararg, align 16
; CHECK:       %v = load %struct.s12, ptr %s
; WIN64:       store %struct.s12 %v, ptr %IndirectAlloca
; WIN64:       store ptr %IndirectAlloca, ptr
; BYVALUE:     store %struct.s12 %v, ptr
; WINA64:      store %struct.s12 %v, ptr
; VALIST:      call void @sink(i32 1, ptr %va_argument)
; WIN:         call void @sink(i32 1, ptr %vararg_buffer)
define void @pass_s12(ptr %s) {
  %v = load %struct.s12, ptr %s, align 4
  call void (i32, ...) @sink(i32 1, %struct.s12 %v)
  ret void
}

; An indirect variadic call is expanded the same way.
; CHECK-LABEL: define void @pass_indirect(ptr %fp)
; CHECK:       %vararg_buffer = alloca %pass_indirect.vararg, align 16
; CHECK:       store i32 10, ptr
; CHECK:       store double 1.000000e+00, ptr
; VALIST:      call void %fp(i32 2, ptr %va_argument)
; WIN:         call void %fp(i32 2, ptr %vararg_buffer)
define void @pass_indirect(ptr %fp) {
  call void (i32, ...) %fp(i32 2, i32 10, double 1.0)
  ret void
}

; A variadic definition takes its va_list last; va_start copies the caller's
; va_list (SysV 24 bytes, AAPCS64 32) or stores the char* (Windows).
; CHECK-LABEL: define i32 @vdef(i32 %n, ptr %varargs)
; CHECK-NEXT:  %ap = alloca [32 x i8], align 16
; SYSV-NEXT:   call void @llvm.memcpy.p0.p0.i{{(32|64)}}(ptr %ap, ptr %varargs, i{{(32|64)}} 24, i1 false)
; AAPCS-NEXT:  call void @llvm.memcpy.p0.p0.i{{(32|64)}}(ptr %ap, ptr %varargs, i{{(32|64)}} 32, i1 false)
; WIN-NEXT:    store ptr %varargs, ptr %ap
; CHECK-NEXT:  ret i32 %n
define i32 @vdef(i32 %n, ...) {
  %ap = alloca [32 x i8], align 16
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %n
}

; CHECK-LABEL: define i32 @call_vdef()
; CHECK:       %vararg_buffer = alloca %call_vdef.vararg, align 16
; CHECK:       store i32 4, ptr
; VALIST:      %r = call i32 @vdef(i32 3, ptr %va_argument)
; WIN:         %r = call i32 @vdef(i32 3, ptr %vararg_buffer)
define i32 @call_vdef() {
  %r = call i32 (i32, ...) @vdef(i32 3, i32 4)
  ret i32 %r
}

; Ignored: a fixed-arity native (MSILPrepareVariadics already rewrote its call
; into a non-variadic one) and a prototype-less declaration.
; CHECK-LABEL: declare i32 @fixed(ptr, ...)
; CHECK-LABEL: declare void @knr(...)
; CHECK-LABEL: define void @call_ignored(ptr %p)
; CHECK-NEXT:  %r = call i32 @fixed(ptr %p, i32 7)
; CHECK-NEXT:  call void (...) @knr(i32 1)
; CHECK-NEXT:  ret void
declare i32 @fixed(ptr, ...) #0
declare void @knr(...) #1

define void @call_ignored(ptr %p) {
  %r = call i32 @fixed(ptr %p, i32 7) #2
  call void (...) @knr(i32 1)
  ret void
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)

attributes #0 = { "msil-fixed-arity" }
attributes #1 = { "no-prototype" }
attributes #2 = { "msil-fixed-arity"="1" }
