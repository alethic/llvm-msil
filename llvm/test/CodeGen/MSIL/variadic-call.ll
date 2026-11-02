; Calls of variadic functions (ImplementationPlan.md 5.15.2, M26; decision D19
; as the section 12 row of 2026-10-28 amends it). ExpandVariadics (slot P7,
; Lowering mode, the MSIL ABIs of 5.13.5) packs a call's variadic arguments
; into '<caller>.vararg', a packed static alloca of the caller aligned to
; max(field alignment, 16), and passes a va_list pointer after the fixed
; arguments, so every call of a variadic function, direct or through a
; pointer, has the IL signature (<fixed>, native int):
; - x86_64-unknown-linux-gnu-msil (SysV): 8-byte slots, nothing indirect. A
;   slot is aligned to the C alignment at which clang's x86-64 va_arg reads
;   it: 8, or the type's alignment when that exceeds 8 (16 for an i128), and
;   for a byval argument at least its call-site alignment (clang passes an
;   unnamed aggregate whose C alignment exceeds 8 byval with that alignment,
;   so 'byval(%T) align 32' of an 8-aligned %T gets a 32-aligned slot; the
;   section 12 row '5.15.2's SysV `slotInfo` amended (review r1, slot
;   alignment)'). The va_list is a [1 x { i32, i32, ptr, ptr }] in
;   the caller's frame with gp_offset 48 and fp_offset 176 (every register
;   slot used up, so va_arg reads the overflow area), overflow_arg_area the
;   buffer and reg_save_area null. At -O2 the two offsets are stored as one
;   int64, 176 << 32 | 48 = 755914244144.
; - aarch64-unknown-linux-gnu-msil (AAPCS64): the va_list is a
;   { ptr, ptr, ptr, i32, i32 } with __stack the buffer, __gr_top and
;   __vr_top null and both offsets 0.
; - x86_64-pc-windows-msvc-msil (char*): 8-byte slots, a value that is not 1,
;   2, 4 or 8 bytes is passed indirectly (its slot holds the address of a
;   copy), and the va_list is the buffer's address itself.
; - aarch64-pc-windows-msvc-msil (char*): the va_list is the buffer's address
;   (the AArch64 slot rules stay unverified until an ARM64 runtime exists).
; The buffer and the va_list are static allocas, so a call in a loop
; allocates nothing per iteration: the method's only localloc is its frame's,
; before the loop. A call with no variadic argument still passes a va_list.
; Nothing is reported at -O0 and -O2, and the modules assemble.
;
; calls.ll: the whole-program IL (all triples) and the IR after P9 (IR-*).
; obj.ll: object mode (5.8.8, 5.14.7): a call of an undefined variadic
; function is a '<msil>Symbols' MemberRef with the expanded signature and a
; VARARG SIGNATURES entry (p7.c's ext_var("x", 4), 5.9.16 row p7-flags), and
; the record stays v1.0. vaddr-data.ll and vaddr-code.ll: an undefined
; variadic function used only by its address (in an initializer, in code)
; compiles in object mode since M26: its address-only stub has the expanded
; signature and a VARARG entry (whole-program mode still gives 'undefined
; variadic function '<f>' is not supported for MSIL yet' for the address of
; one on the fixed-arity list or of an explicit import: variadic-errors.ll's
; vaddr.ll). vaddr-import.ll: the same for a declaration with
; "msil-import-module" and "msil-import-name" compiles.
; alias.ll: calls through an alias and a weak alias of a variadic definition
; are expanded in object mode (the call names the alias, (int32, native
; int)) and in whole-program mode (the call names the aliasee), at -O0 and
; -O2 on both x86_64 triples, with nothing reported.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{p} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/calls.ll \
; DEFINE:   -o %t/calls.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,%{p} --input-file=%t/calls.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=pinvokeimpl --implicit-check-not="'take'(int32, int32"

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{p} = SYSV
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/calls.lnx.dll %t/calls.x86_64-unknown-linux-gnu-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/calls.lnx.o0.dll %t/calls.x86_64-unknown-linux-gnu-msil-O0.il %}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; REDEFINE: %{p} = PTRARG
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{p} = PTRARG
; REDEFINE: %{o} = -O0
; RUN: %{il}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/calls.win.o0.dll %t/calls.x86_64-pc-windows-msvc-msil-O0.il %}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/calls.win.dll %t/calls.x86_64-pc-windows-msvc-msil-O2.il %}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{il}

; DEFINE: %{ir} = llc -mtriple=%{t} %{o} -stop-after=msil-lower-dynamic-alloca < %t/calls.ll -o - \
; DEFINE:   | FileCheck %s --check-prefixes=IR,%{p}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{p} = IR-SYSV
; REDEFINE: %{o} = -O0
; RUN: %{ir}
; REDEFINE: %{o} = -O2
; RUN: %{ir}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; REDEFINE: %{p} = IR-AAPCS
; RUN: %{ir}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{p} = IR-WIN,IR-WIN64
; RUN: %{ir}
; REDEFINE: %{o} = -O0
; RUN: %{ir}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; REDEFINE: %{p} = IR-WIN
; RUN: %{ir}

; REQUIRES: llvm-cil-dump
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/varcall.ll \
; RUN:   -o %t/varcall.lnx.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/varcall.lnx.o \
; RUN:   | FileCheck %s --check-prefixes=VARCALL,VARCALL-LNX --implicit-check-not=FIXED_ARITY
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/varcall.ll \
; RUN:   -o %t/varcall.win.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/varcall.win.o \
; RUN:   | FileCheck %s --check-prefixes=VARCALL,VARCALL-WIN --implicit-check-not=FIXED_ARITY
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/varcall.ll \
; RUN:   -o %t/varcall.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=VARCALL-IL --input-file=%t/varcall.lnx.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/vaddr-data.ll \
; RUN:   -o %t/vaddr-data.lnx.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/vaddr-data.lnx.o | FileCheck %s --check-prefix=VADDR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/vaddr-data.ll \
; RUN:   -o %t/vaddr-data.win.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/vaddr-data.win.o | FileCheck %s --check-prefix=VADDR
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/vaddr-code.ll \
; RUN:   -o %t/vaddr-code.lnx.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/vaddr-code.lnx.o | FileCheck %s --check-prefix=VADDR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/vaddr-code.ll \
; RUN:   -o %t/vaddr-code.win.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/vaddr-code.win.o | FileCheck %s --check-prefix=VADDR
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/vaddr-import.ll \
; RUN:   -o %t/vaddr-import.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/vaddr-import.ll \
; RUN:   -o %t/vaddr-import.win.o 2>&1 | count 0

; DEFINE: %{alias} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %t/alias.ll \
; DEFINE:   -o %t/alias.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump %t/alias.%{t}%{o}.o | FileCheck %s --check-prefix=ALIAS-OBJ && \
; DEFINE:   llvm-cil-dump --link-info %t/alias.%{t}%{o}.o | FileCheck %s --check-prefix=ALIAS-LINK && \
; DEFINE:   llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/alias.ll \
; DEFINE:   -o %t/alias.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefix=ALIAS-IL --input-file=%t/alias.%{t}%{o}.il
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %{alias}
; REDEFINE: %{o} = -O2
; RUN: %{alias}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/alias.lnx.dll %t/alias.x86_64-unknown-linux-gnu-msil-O2.il %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{alias}
; REDEFINE: %{o} = -O0
; RUN: %{alias}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/alias.win.o0.dll %t/alias.x86_64-pc-windows-msvc-msil-O0.il %}

;--- calls.ll
source_filename = "calls.c"

@g = global i32 5

; The buffers (IR, after P9): one per caller, packed.
; IR-SYSV-DAG:  %call_mixed.vararg = type <{ i32, [4 x i8], i64, double, ptr, i128 }>
; IR-WIN64-DAG: %call_mixed.vararg = type <{ i32, [4 x i8], i64, double, ptr, ptr }>
; The over-aligned byval structures: offset 16 and 32 on SysV, a pointer to a
; caller copy on Win64.
; IR-SYSV-DAG:  %call_al16.vararg = type <{ i32, [12 x i8], %struct.two }>
; IR-SYSV-DAG:  %call_al32.vararg = type <{ i32, [28 x i8], %struct.big }>
; IR-WIN64-DAG: %call_al16.vararg = type <{ i32, [4 x i8], ptr }>
; IR-WIN64-DAG: %call_al32.vararg = type <{ i32, [4 x i8], ptr }>

%struct.two = type { i64, i64 }
%struct.big = type { i64, i64, i64 }

; CHECK:       {{^}}.method public static int32 'take'(int32, native int) cil managed{{$}}
; IR-LABEL:    define i32 @take(i32 %n, ptr %varargs)
define i32 @take(i32 %n, ...) {
  ret i32 %n
}

; int, long, double, pointer and __int128 variadic arguments.
; CHECK-LABEL: {{^}}.method public static int32 'call_mixed'() cil managed{{$}}
; SYSV:        {{^}}  ldc.i8 4612811918334230528{{$}}
; SYSV:        {{^  (ldc.i8 755914244144|ldc.i4 176)$}}
; PTRARG-NOT:  {{^  (ldc.i8 755914244144|ldc.i4 176)$}}
; CHECK:       {{^}}  call int32 'take'(int32, native int){{$}}
; CHECK:       {{^}}} // end of method 'call_mixed'{{$}}
; IR-LABEL:    define i32 @call_mixed()
; IR-SYSV:     alloca %call_mixed.vararg, align 16
; IR-SYSV:     alloca [1 x { i32, i32, ptr, ptr }]
; IR-SYSV:     store i32 7, ptr
; IR-SYSV:     store i64 8, ptr
; IR-SYSV:     store double 2.500000e+00, ptr
; IR-SYSV:     store ptr @g, ptr
; IR-SYSV:     store i128 9, ptr {{.*}}, align 16
; IR-SYSV:     store i32 48, ptr [[VA:%[^,]+]]
; IR-SYSV:     store i32 176, ptr
; IR-SYSV:     store ptr {{%[^,]+}}, ptr
; IR-SYSV:     store ptr null, ptr
; IR-SYSV:     call i32 @take(i32 6, ptr [[VA]])
; IR-AAPCS:    alloca { ptr, ptr, ptr, i32, i32 }
; IR-AAPCS:    store i32 7, ptr
; IR-AAPCS:    store ptr {{%[^,]+}}, ptr [[VA:%[^,]+]]
; IR-AAPCS:    store ptr null, ptr
; IR-AAPCS:    store ptr null, ptr
; IR-AAPCS:    store i32 0, ptr
; IR-AAPCS:    store i32 0, ptr
; IR-AAPCS:    call i32 @take(i32 6, ptr [[VA]])
; IR-WIN:      [[BUF:%[^ ]+]] = alloca %call_mixed.vararg, align 16
; IR-WIN64:    store i128 9, ptr [[TMP:%[^,]+]], align 16
; IR-WIN:      store i32 7, ptr
; IR-WIN64:    store ptr [[TMP]], ptr
; IR-WIN:      call i32 @take(i32 6, ptr [[BUF]])
define i32 @call_mixed() {
  %r = call i32 (i32, ...) @take(i32 6, i32 7, i64 8, double 2.5, ptr @g, i128 9)
  ret i32 %r
}

; Over-aligned byval structures whose IR types are 8-aligned: the SysV slot
; takes the call-site alignment and the buffer is aligned to at least it.
; CHECK-LABEL: {{^}}.method public static int32 'call_al16'(native int) cil managed{{$}}
; CHECK:       {{^}}  call int32 'take'(int32, native int){{$}}
; CHECK:       {{^}}} // end of method 'call_al16'{{$}}
; IR-LABEL:    define i32 @call_al16(ptr %s)
; IR-SYSV:     alloca %call_al16.vararg, align 16
; IR-SYSV:     store i32 2, ptr
; IR-SYSV:     getelementptr inbounds{{( nuw)?}} %call_al16.vararg, ptr {{%[^,]+}}, i32 0, i32 2
; IR-SYSV-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr {{%[^,]+}}, ptr %s, i64 16, i1 false)
; IR-SYSV:     call i32 @take(i32 1, ptr
define i32 @call_al16(ptr %s) {
  %r = call i32 (i32, ...) @take(i32 1, i32 2, ptr byval(%struct.two) align 16 %s)
  ret i32 %r
}

; CHECK-LABEL: {{^}}.method public static int32 'call_al32'(native int) cil managed{{$}}
; CHECK:       {{^}}  call int32 'take'(int32, native int){{$}}
; CHECK:       {{^}}} // end of method 'call_al32'{{$}}
; IR-LABEL:    define i32 @call_al32(ptr %s)
; IR-SYSV:     alloca %call_al32.vararg, align 32
; IR-SYSV:     store i32 2, ptr
; IR-SYSV:     getelementptr inbounds{{( nuw)?}} %call_al32.vararg, ptr {{%[^,]+}}, i32 0, i32 2
; IR-SYSV-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr {{%[^,]+}}, ptr %s, i64 24, i1 false)
; IR-SYSV:     call i32 @take(i32 1, ptr
define i32 @call_al32(ptr %s) {
  %r = call i32 (i32, ...) @take(i32 1, i32 2, ptr byval(%struct.big) align 32 %s)
  ret i32 %r
}

; No variadic argument: a va_list is still passed.
; CHECK-LABEL: {{^}}.method public static int32 'call_none'() cil managed{{$}}
; CHECK:       {{^}}  call int32 'take'(int32, native int){{$}}
; CHECK:       {{^}}} // end of method 'call_none'{{$}}
; IR-LABEL:    define i32 @call_none()
; IR:          call i32 @take(i32 0, ptr
define i32 @call_none() {
  %r = call i32 (i32, ...) @take(i32 0)
  ret i32 %r
}

; Through a function pointer: the calli has the expanded signature.
; CHECK-LABEL: {{^}}.method public static int32 'call_indirect'(native int) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32, native int){{$}}
; CHECK:       {{^}}} // end of method 'call_indirect'{{$}}
; IR-LABEL:    define i32 @call_indirect(ptr %fp)
; IR:          call i32 %fp(i32 2, ptr
define i32 @call_indirect(ptr %fp) {
  %r = call i32 (i32, ...) %fp(i32 2, i32 10, i32 20)
  ret i32 %r
}

; A call in a loop: the buffer is part of the static frame, allocated once
; before the loop.
; CHECK-LABEL: {{^}}.method public static int32 'call_loop'(int32) cil managed{{$}}
; CHECK:       {{^}}  localloc{{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^@[^ ]+:$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}  call int32 'take'(int32, native int){{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}} // end of method 'call_loop'{{$}}
; IR-LABEL:    define i32 @call_loop(i32 %n)
; IR-NEXT:     entry:
; IR-NEXT:     alloca
define i32 @call_loop(i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i1, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc1, %loop ]
  %v = call i32 (i32, ...) @take(i32 1, i32 %i)
  %acc1 = add i32 %acc, %v
  %i1 = add i32 %i, 1
  %c = icmp slt i32 %i1, %n
  br i1 %c, label %loop, label %done
done:
  ret i32 %acc1
}

; A variadic call whose callee returns double and takes a double first.
; CHECK-LABEL: {{^}}.method public static float64 'call_fp'(float64) cil managed{{$}}
; CHECK:       {{^}}  call float64 'takefp'(float64, native int){{$}}
; CHECK:       {{^}}} // end of method 'call_fp'{{$}}
define double @takefp(double %x, ...) {
  ret double %x
}

define double @call_fp(double %x) {
  %r = call double (double, ...) @takefp(double %x, double %x, i32 3)
  ret double %r
}

;--- varcall.ll
source_filename = "p7.c"

@.str = private unnamed_addr constant [2 x i8] c"x\00", align 1

declare i32 @ext_var(ptr noundef, ...)

define i32 @use_var() {
  %r = call i32 (ptr, ...) @ext_var(ptr noundef @.str, i32 noundef 4)
  ret i32 %r
}

; VARCALL-LNX: LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; VARCALL-WIN: LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; VARCALL:     Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_var'
; VARCALL:     Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT 'use_var' method=0x06000001
; VARCALL:     Signature 0x0A000001 'ext_var' VARARG fixed=1
; VARCALL-NOT: Signature

; VARCALL-IL:  {{^}}  call int32 [msil.extern]'<msil>Symbols'::'ext_var'(native int, native int){{$}}

;--- vaddr-data.ll
source_filename = "vaddr-data.c"

@pp = global ptr @printf, align 8

declare i32 @printf(ptr noundef, ...)

; VADDR:     Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT UNDEFINED 'printf'
; VADDR:     Signature 0x06{{[0-9A-F]+}} 'printf' VARARG fixed=1
; VADDR-NOT: Signature

;--- vaddr-code.ll
source_filename = "vaddr-code.c"

declare i32 @printf(ptr noundef, ...)

define ptr @get_printf() {
  ret ptr @printf
}

;--- vaddr-import.ll
source_filename = "vaddr-import.c"

declare i32 @my_printf(ptr noundef, ...) #0

define ptr @get_my_printf() {
  ret ptr @my_printf
}

attributes #0 = { "msil-import-module"="libc" "msil-import-name"="printf" }

;--- alias.ll
source_filename = "alias.c"

; Object mode: each call names the alias, a '<msil>Symbols' MemberRef with
; the expanded signature, and the aliases are ALIAS symbols of impl.
; ALIAS-OBJ:       {{^}}  .method 'call_sum' int32(int32) flags={{.*}}
; ALIAS-OBJ:       {{^}}    L{{[0-9]+}}: call int32(int32, native int) [msil.extern]<msil>Symbols::'sum'{{$}}
; ALIAS-OBJ:       {{^}}  .method 'call_wsum' int32(int32) flags={{.*}}
; ALIAS-OBJ:       {{^}}    L{{[0-9]+}}: call int32(int32, native int) [msil.extern]<msil>Symbols::'wsum'{{$}}
; ALIAS-OBJ:       {{^}}  .method 'impl' int32(int32, native int) flags={{.*}}
; ALIAS-LINK:      Symbol [[IMPL:[0-9]+]] FUNCTION GLOBAL DEFAULT 'impl' method=0x06{{[0-9A-F]+}}{{$}}
; ALIAS-LINK-DAG:  Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT ALIAS 'sum' aliasee=[[IMPL]]{{$}}
; ALIAS-LINK-DAG:  Symbol {{[0-9]+}} FUNCTION WEAK DEFAULT ALIAS 'wsum' aliasee=[[IMPL]]{{$}}
; ALIAS-LINK:      Signature 0x06{{[0-9A-F]+}} 'impl' VARARG fixed=1{{$}}

; Whole-program mode: each call is an expanded call of the aliasee.
; ALIAS-IL:        {{^}}.method public static int32 'impl'(int32, native int) cil managed{{$}}
; ALIAS-IL-LABEL:  .method public static int32 'call_sum'(int32) cil managed{{$}}
; ALIAS-IL:        {{^}}  call int32 'impl'(int32, native int){{$}}
; ALIAS-IL-NEXT:   {{^}}  ret{{$}}
; ALIAS-IL-NEXT:   {{^}}} // end of method 'call_sum'{{$}}
; ALIAS-IL-LABEL:  .method public static int32 'call_wsum'(int32) cil managed{{$}}
; ALIAS-IL:        {{^}}  call int32 'impl'(int32, native int){{$}}
; ALIAS-IL-NEXT:   {{^}}  ret{{$}}
; ALIAS-IL-NEXT:   {{^}}} // end of method 'call_wsum'{{$}}
; ALIAS-IL:        {{^}}.method public static int32 'sum'(int32, native int) cil managed{{$}}
; ALIAS-IL:        {{^}}.method public static int32 'wsum'(int32, native int) cil managed{{$}}

@sum = alias i32 (i32, ...), ptr @impl
@wsum = weak alias i32 (i32, ...), ptr @impl

define i32 @impl(i32 %n, ...) {
  ret i32 %n
}

define i32 @call_sum(i32 %x) {
  %r = call i32 (i32, ...) @sum(i32 %x, i32 2, double 3.0)
  ret i32 %r
}

define i32 @call_wsum(i32 %x) {
  %r = call i32 (i32, ...) @wsum(i32 %x, i64 4)
  ret i32 %r
}
