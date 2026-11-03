; The MSIL ABIs of ExpandVariadics (ImplementationPlan.md 5.15.2 'The
; ExpandVariadics hook', M26). VariadicABIInfo::create returns, for an MSIL
; triple, MSILSysV64 (x86_64 off Windows), MSILAAPCS64 (aarch64 off Windows)
; or MSILCharPtr (Windows triples), and MSILPortable for the portable
; msil64-unknown-dotnet and msil32-unknown-dotnet (5.18.2 PC5, P-1a), which
; it tests first:
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
; - Portable (msil64-unknown-dotnet, msil32-unknown-dotnet): va_list is the
;   buffer pointer itself (char*); 8-byte slots aligned to max(8, the type's
;   alignment), so an i128 has a 16-aligned slot of its own on msil64; every
;   aggregate (byval or first-class, a 1-byte one included) goes by pointer to
;   a caller copy, as does an integer wider than the widest clang passes
;   directly (5.18.2 PC2): 64 bits on msil32 (a _BitInt(128), which clang
;   passes byval(i128) there; msil32 has no __int128) and 128 bits on msil64
;   (a _BitInt(129) or _BitInt(200), which clang passes byval(i256) on both),
;   the rule of clang's MSILPortableABIInfo::EmitVAArg. A pointer takes a
;   whole 8-byte slot on msil32 too (4 bytes, then padding).
; - The portable caller copy of a byval argument is aligned to at least the
;   call site's byval alignment, with which the callee loads through the
;   slot's pointer (16 and 32 below, above the IR types' 8). The Windows
;   triples keep the IR type's alignment (8) for their copy.
; The buffer is the packed '<caller>.vararg' alloca aligned to max(field
; alignment, 16). Every ABI
; ignores functions with "no-prototype" or "msil-fixed-arity": their
; declarations stay variadic and their calls are left alone.

; REQUIRES: msil-registered-target
; DEFINE: %{t} =
; DEFINE: %{p} =
; DEFINE: %{last} = 5
; DEFINE: %{run} = opt -mtriple=%{t} -S --passes=expand-variadics --expand-variadics-override=lowering < %s \
; DEFINE:   | FileCheck %s --check-prefixes=CHECK,%{p} -DLAST=%{last}

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
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{p} = WIN,PORT,PORT64
; RUN: %{run}
; On msil32 the i128 of pass_mixed is the buffer's field 6 (after the 4-byte
; pointer's padding).
; REDEFINE: %{t} = msil32-unknown-dotnet
; REDEFINE: %{p} = WIN,PORT,PORT32
; REDEFINE: %{last} = 6
; RUN: %{run}

; The buffer layouts: 8-byte slots (the i32 gets 4 bytes of padding), the
; i128 a 16-aligned slot of its own on SysV, AAPCS64, ARM64 Windows and
; msil64 and a pointer on Win64 and msil32 (where the 4-byte pointer before it
; is padded to its 8-byte slot); the 24-byte byval structure by value except
; on the Windows and portable triples; the 12-byte structure by value except
; on Win64 and the portable triples; the 1-byte byval structure by value
; except on the portable triples; a byval i128 by pointer on msil32 and by
; value in a 16-aligned slot on msil64.
; SYSV-DAG:   %pass_mixed.vararg = type <{ i32, [4 x i8], i64, double, ptr, i128 }>
; AAPCS-DAG:  %pass_mixed.vararg = type <{ i32, [4 x i8], i64, double, ptr, i128 }>
; WINA64-DAG: %pass_mixed.vararg = type <{ i32, [4 x i8], i64, double, ptr, i128 }>
; WIN64-DAG:  %pass_mixed.vararg = type <{ i32, [4 x i8], i64, double, ptr, ptr }>
; PORT64-DAG: %pass_mixed.vararg = type <{ i32, [4 x i8], i64, double, ptr, i128 }>
; PORT32-DAG: %pass_mixed.vararg = type <{ i32, [4 x i8], i64, double, ptr, [4 x i8], ptr }>
; BYVALUE-DAG: %pass_byval.vararg = type <{ %struct.big }>
; WIN-DAG:    %pass_byval.vararg = type <{ ptr }>
; BYVALUE-DAG: %pass_s12.vararg = type <{ %struct.s12 }>
; WINA64-DAG: %pass_s12.vararg = type <{ %struct.s12 }>
; WIN64-DAG:  %pass_s12.vararg = type <{ ptr }>
; PORT-DAG:   %pass_s12.vararg = type <{ ptr }>
; BYVALUE-DAG: %pass_byval_s1.vararg = type <{ i32, [4 x i8], %struct.s1 }>
; WIN64-DAG:  %pass_byval_s1.vararg = type <{ i32, [4 x i8], %struct.s1 }>
; WINA64-DAG: %pass_byval_s1.vararg = type <{ i32, [4 x i8], %struct.s1 }>
; PORT-DAG:   %pass_byval_s1.vararg = type <{ i32, [4 x i8], ptr }>
; PORT64-DAG: %pass_byval_i128.vararg = type <{ i32, [12 x i8], i128 }>
; PORT32-DAG: %pass_byval_i128.vararg = type <{ i32, [4 x i8], ptr }>
; PORT-DAG:   %pass_byval_i256.vararg = type <{ i32, [4 x i8], ptr }>
; PORT-DAG:   %pass_i200.vararg = type <{ i32, [4 x i8], ptr }>
; The over-aligned byval structures (8-aligned IR types) after an i32: on SysV
; at offset 16 (byval align 16) and 32 (byval align 32); on Win64 and the
; portable triples both are indirect; on ARM64 Windows the 16-byte one is by
; value in an 8-byte slot and the 24-byte one indirect. AAPCS64 is not pinned:
; clang never passes an aggregate byval there.
; SYSV-DAG:   %pass_byval_al16.vararg = type <{ i32, [12 x i8], %struct.two }>
; SYSV-DAG:   %pass_byval_al32.vararg = type <{ i32, [28 x i8], %struct.big }>
; WIN64-DAG:  %pass_byval_al16.vararg = type <{ i32, [4 x i8], ptr }>
; WIN64-DAG:  %pass_byval_al32.vararg = type <{ i32, [4 x i8], ptr }>
; WINA64-DAG: %pass_byval_al16.vararg = type <{ i32, [4 x i8], %struct.two }>
; WINA64-DAG: %pass_byval_al32.vararg = type <{ i32, [4 x i8], ptr }>
; PORT-DAG:   %pass_byval_al16.vararg = type <{ i32, [4 x i8], ptr }>
; PORT-DAG:   %pass_byval_al32.vararg = type <{ i32, [4 x i8], ptr }>
; CHECK-DAG:  %pass_indirect.vararg = type <{ i32, [4 x i8], double }>
; CHECK-DAG:  %call_vdef.vararg = type <{ i32 }>

%struct.big = type { i64, i64, i64 }
%struct.s12 = type { i32, i32, i32 }
%struct.two = type { i64, i64 }
%struct.s1 = type { i8 }

; CHECK-LABEL: declare void @sink(i32, ptr)
declare void @sink(i32, ...)

; CHECK-LABEL: define void @pass_mixed(ptr %p)
; WIN64:       %IndirectAlloca = alloca i128, align 16
; PORT32:      %IndirectAlloca = alloca i128, align 16
; CHECK:       %vararg_buffer = alloca %pass_mixed.vararg, align 16
; SYSV:        %va_argument = alloca [1 x { i32, i32, ptr, ptr }]
; AAPCS:       %va_argument = alloca { ptr, ptr, ptr, i32, i32 }
; WIN64:       store i128 9, ptr %IndirectAlloca, align 16
; PORT32:      store i128 9, ptr %IndirectAlloca, align 16
; CHECK:       [[F0:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_mixed.vararg, ptr %vararg_buffer, i32 0, i32 0
; CHECK-NEXT:  store i32 7, ptr [[F0]]
; CHECK:       [[F2:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_mixed.vararg, ptr %vararg_buffer, i32 0, i32 2
; CHECK-NEXT:  store i64 8, ptr [[F2]]
; CHECK:       [[F3:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_mixed.vararg, ptr %vararg_buffer, i32 0, i32 3
; CHECK-NEXT:  store double 2.500000e+00, ptr [[F3]]
; CHECK:       [[F4:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_mixed.vararg, ptr %vararg_buffer, i32 0, i32 4
; CHECK-NEXT:  store ptr %p, ptr [[F4]]
; CHECK:       [[F5:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_mixed.vararg, ptr %vararg_buffer, i32 0, i32 [[LAST]]
; SYSV-NEXT:   store i128 9, ptr [[F5]], align 16
; AAPCS-NEXT:  store i128 9, ptr [[F5]], align 16
; WINA64-NEXT: store i128 9, ptr [[F5]], align 16
; PORT64-NEXT: store i128 9, ptr [[F5]], align 16
; WIN64-NEXT:  store ptr %IndirectAlloca, ptr [[F5]]
; PORT32-NEXT: store ptr %IndirectAlloca, ptr [[F5]]
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
; WIN64:       %IndirectAlloca = alloca %struct.two, align 8
; PORT:        %IndirectAlloca = alloca %struct.two, align 16
; SYSV:        %vararg_buffer = alloca %pass_byval_al16.vararg, align 16
; SYSV:        [[A0:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_al16.vararg, ptr %vararg_buffer, i32 0, i32 0
; SYSV-NEXT:   store i32 2, ptr [[A0]]
; SYSV:        [[A2:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_al16.vararg, ptr %vararg_buffer, i32 0, i32 2
; SYSV-NEXT:   call void @llvm.memcpy.p0.p0.i64(ptr [[A2]], ptr %s, i64 16, i1 false)
; SYSV:        call void @sink(i32 1, ptr %va_argument)
; WIN64:       call void @llvm.memcpy.p0.p0.i64(ptr %IndirectAlloca, ptr %s, i64 16, i1 false)
; PORT:        call void @llvm.memcpy.p0.p0.i64(ptr %IndirectAlloca, ptr %s, i64 16, i1 false)
; PORT:        [[P2:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_al16.vararg, ptr %vararg_buffer, i32 0, i32 2
; PORT-NEXT:   store ptr %IndirectAlloca, ptr [[P2]]
; WIN:         call void @sink(i32 1, ptr %vararg_buffer)
; CHECK:       ret void
define void @pass_byval_al16(ptr %s) {
  call void (i32, ...) @sink(i32 1, i32 2, ptr byval(%struct.two) align 16 %s)
  ret void
}

; CHECK-LABEL: define void @pass_byval_al32(ptr %s)
; WIN64:       %IndirectAlloca = alloca %struct.big, align 8
; WINA64:      %IndirectAlloca = alloca %struct.big, align 8
; PORT:        %IndirectAlloca = alloca %struct.big, align 32
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
; PORT:        %IndirectAlloca = alloca %struct.s12
; CHECK:       %vararg_buffer = alloca %pass_s12.vararg, align 16
; CHECK:       %v = load %struct.s12, ptr %s
; WIN64:       store %struct.s12 %v, ptr %IndirectAlloca
; WIN64:       store ptr %IndirectAlloca, ptr
; PORT:        store %struct.s12 %v, ptr %IndirectAlloca
; PORT:        store ptr %IndirectAlloca, ptr
; BYVALUE:     store %struct.s12 %v, ptr
; WINA64:      store %struct.s12 %v, ptr
; VALIST:      call void @sink(i32 1, ptr %va_argument)
; WIN:         call void @sink(i32 1, ptr %vararg_buffer)
define void @pass_s12(ptr %s) {
  %v = load %struct.s12, ptr %s, align 4
  call void (i32, ...) @sink(i32 1, %struct.s12 %v)
  ret void
}

; A 1-byte byval structure (clang's struct { char }): by value in an 8-byte
; slot, even on Win64 (1 is a power of 2), and by pointer to a caller copy on
; the portable triples, whatever the size.
; CHECK-LABEL: define void @pass_byval_s1(ptr %s)
; PORT:        %IndirectAlloca = alloca %struct.s1, align {{[0-9]+}}
; CHECK:       %vararg_buffer = alloca %pass_byval_s1.vararg, align 16
; PORT:        call void @llvm.memcpy.p0.p0.i64(ptr %IndirectAlloca, ptr %s, i64 1, i1 false)
; PORT:        [[S0:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_s1.vararg, ptr %vararg_buffer, i32 0, i32 0
; PORT-NEXT:   store i32 2, ptr [[S0]]
; PORT:        [[S2:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_s1.vararg, ptr %vararg_buffer, i32 0, i32 2
; PORT-NEXT:   store ptr %IndirectAlloca, ptr [[S2]]
; WIN64:       [[V2:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_s1.vararg, ptr %vararg_buffer, i32 0, i32 2
; WIN64-NEXT:  call void @llvm.memcpy.p0.p0.i64(ptr [[V2]], ptr %s, i64 1, i1 false)
; VALIST:      call void @sink(i32 1, ptr %va_argument)
; WIN:         call void @sink(i32 1, ptr %vararg_buffer)
; CHECK:       ret void
define void @pass_byval_s1(ptr %s) {
  call void (i32, ...) @sink(i32 1, i32 2, ptr byval(%struct.s1) align 1 %s)
  ret void
}

; A byval i128, clang's form of a _BitInt(128) argument on msil32 (wider than
; long long there): one 8-byte slot holding a pointer to a caller copy. On
; msil64, where clang passes a _BitInt(128) directly, a byval i128 is copied
; by value into a 16-aligned slot.
; CHECK-LABEL: define void @pass_byval_i128(ptr %s)
; PORT32:      %IndirectAlloca = alloca i128, align 16
; PORT:        %vararg_buffer = alloca %pass_byval_i128.vararg, align 16
; PORT32:      call void @llvm.memcpy.p0.p0.i64(ptr %IndirectAlloca, ptr %s, i64 16, i1 false)
; PORT:        [[I0:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_i128.vararg, ptr %vararg_buffer, i32 0, i32 0
; PORT-NEXT:   store i32 2, ptr [[I0]]
; PORT:        [[I2:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_i128.vararg, ptr %vararg_buffer, i32 0, i32 2
; PORT32-NEXT: store ptr %IndirectAlloca, ptr [[I2]]
; PORT64-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr [[I2]], ptr %s, i64 16, i1 false)
; PORT:        call void @sink(i32 1, ptr %vararg_buffer)
; CHECK:       ret void
define void @pass_byval_i128(ptr %s) {
  call void (i32, ...) @sink(i32 1, i32 2, ptr byval(i128) align 8 %s)
  ret void
}

; A byval i256, clang's form of an unsigned _BitInt(129) or a _BitInt(200)
; argument on both portable triples (wider than the 128 bits msil64 passes
; directly): one 8-byte slot holding a pointer to a caller copy, aligned to
; the call site's byval alignment, on msil64 as on msil32.
; CHECK-LABEL: define void @pass_byval_i256(ptr %s)
; PORT:        %IndirectAlloca = alloca i256, align 16
; PORT:        %vararg_buffer = alloca %pass_byval_i256.vararg, align 16
; PORT:        call void @llvm.memcpy.p0.p0.i64(ptr %IndirectAlloca, ptr %s, i64 32, i1 false)
; PORT:        [[J0:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_i256.vararg, ptr %vararg_buffer, i32 0, i32 0
; PORT-NEXT:   store i32 2, ptr [[J0]]
; PORT:        [[J2:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_byval_i256.vararg, ptr %vararg_buffer, i32 0, i32 2
; PORT-NEXT:   store ptr %IndirectAlloca, ptr [[J2]]
; PORT:        call void @sink(i32 1, ptr %vararg_buffer)
; CHECK:       ret void
define void @pass_byval_i256(ptr %s) {
  call void (i32, ...) @sink(i32 1, i32 2, ptr byval(i256) align 16 %s)
  ret void
}

; A first-class i200 takes the same path: a caller copy, its address in the
; slot (the i128 of pass_mixed stays direct on msil64).
; CHECK-LABEL: define void @pass_i200(i200 %v)
; PORT:        %IndirectAlloca = alloca i200, align 16
; PORT:        %vararg_buffer = alloca %pass_i200.vararg, align 16
; PORT:        store i200 %v, ptr %IndirectAlloca, align 16
; PORT:        [[K2:%.*]] = getelementptr inbounds{{( nuw)?}} %pass_i200.vararg, ptr %vararg_buffer, i32 0, i32 2
; PORT-NEXT:   store ptr %IndirectAlloca, ptr [[K2]]
; PORT:        call void @sink(i32 1, ptr %vararg_buffer)
; CHECK:       ret void
define void @pass_i200(i200 %v) {
  call void (i32, ...) @sink(i32 1, i32 2, i200 %v)
  ret void
}

; The callee's half of the portable indirect slot (va_arg on the char*
; va_list): the cursor is rounded up to 8, advanced past one pointer, the
; pointer is loaded from the slot and the i200 through it. The cursor
; advances by the pointer's size, 8 on msil64 and 4 on msil32, where the next
; va_arg's rounding to 8 skips the slot's padding, as the caller's frame pads
; it (pass_mixed's PORT32 layout).
; CHECK-LABEL: define i200 @va_i200(ptr %ap)
; PORT:        [[CUR:%.*]] = load ptr, ptr %ap
; PORT-NEXT:   [[UP:%.*]] = getelementptr inbounds i8, ptr [[CUR]], i{{32|64}} 7
; PORT-NEXT:   [[SLOT:%.*]] = call ptr @llvm.ptrmask.p0.i{{32|64}}(ptr [[UP]], i{{32|64}} -8)
; PORT64-NEXT: [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[SLOT]], i64 8
; PORT32-NEXT: [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[SLOT]], i32 4
; PORT-NEXT:   store ptr [[NEXT]], ptr %ap
; PORT-NEXT:   [[COPY:%.*]] = load ptr, ptr [[SLOT]], align 8
; PORT-NEXT:   %v = load i200, ptr [[COPY]], align 16
; PORT-NEXT:   ret i200 %v
define i200 @va_i200(ptr %ap) {
  %v = va_arg ptr %ap, i200
  ret i200 %v
}

; The direct contrast on msil64: an i128 is read by value from a 16-aligned
; 16-byte slot. On msil32 (wider than 64 bits) it is indirect, as the i200.
; CHECK-LABEL: define i128 @va_i128(ptr %ap)
; PORT:        [[CUR:%.*]] = load ptr, ptr %ap
; PORT64-NEXT: [[UP:%.*]] = getelementptr inbounds i8, ptr [[CUR]], i64 15
; PORT64-NEXT: [[SLOT:%.*]] = call ptr @llvm.ptrmask.p0.i64(ptr [[UP]], i64 -16)
; PORT64-NEXT: [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[SLOT]], i64 16
; PORT64-NEXT: store ptr [[NEXT]], ptr %ap
; PORT64-NEXT: %v = load i128, ptr [[SLOT]], align 16
; PORT32-NEXT: [[UP:%.*]] = getelementptr inbounds i8, ptr [[CUR]], i32 7
; PORT32-NEXT: [[SLOT:%.*]] = call ptr @llvm.ptrmask.p0.i32(ptr [[UP]], i32 -8)
; PORT32-NEXT: [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[SLOT]], i32 4
; PORT32-NEXT: store ptr [[NEXT]], ptr %ap
; PORT32-NEXT: [[COPY:%.*]] = load ptr, ptr [[SLOT]], align 8
; PORT32-NEXT: %v = load i128, ptr [[COPY]], align 16
; PORT-NEXT:   ret i128 %v
define i128 @va_i128(ptr %ap) {
  %v = va_arg ptr %ap, i128
  ret i128 %v
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
