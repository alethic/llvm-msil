; Zero-initialized globals (ImplementationPlan.md M8.b and M8.c, decision D12;
; recon asmprinter.md 7.1-7.3): a global whose initializer is zero or undef
; (zeroinitializer, 0, null, undef; also a constant one) takes no bytes in the
; image: it is placed after the initialized globals, at its own alignment, in
; the zero tail that the .cctor clears with initblk; -0.0 is not zero and stays
; in the image. The image holds only the initialized prefix ('.size' and the
; bytearray), so a large zero-initialized array costs nothing in the assembly.
; The segment is aligned to MaxAlign, at least 16 and otherwise the largest
; alignment of a global (64 here): AllocHGlobal(TotalSize + MaxAlign - 1),
; then 'ldc.i4 MaxAlign-1; conv.i; add; ldc.i4 -MaxAlign; conv.i; and'. A
; module whose globals are all zero-initialized has no image type, no RVA
; field and no cpblk, only the initblk of its whole segment.
;
; The zero tail starts at Base + InitSize, and Base is aligned to MaxAlign, so
; when InitSize is not a multiple of the pointer size the initblk gets the
; prefix 'unaligned. N' on its own line, N = min(commonAlignment(MaxAlign,
; InitSize), 4) (plan section 12, 'M8.c amended (alignment prefixes in
; <Module>::.cctor)'; D24): 'unaligned. 4' at Base + 12 (mixed.ll) and
; Base + 4 (align64.ll), 'unaligned. 1' at Base + 1 (huge.ll). An initblk at
; Base itself (bss-only.ll) has no prefix, and the cpblk of the image never
; has one.
;
; A segment of 2 GiB or more cannot be allocated with the int32 size of the
; .cctor: "the data segment of <TotalSize> bytes is larger than MSIL supports
; (2 GiB)", once, exit code 1, no crash (toobig.ll).
;
; mixed.ll interleaves zero-initialized and initialized globals in IR order.
; The order of the address fields and of their stores among the globals is
; not pinned (only the layout is): the fields are matched as a group, the
; store of the first global (first in IR order and at offset 0) is matched
; after the initblk, and the store of every other global is matched with its
; own offset by its own FileCheck run (the offsets are unique among the .cctor
; constants). The zero-initialized globals are declared in order of
; non-increasing alignment, so that their placement does not depend on whether
; the zero tail keeps IR order or sorts by alignment.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=".field " --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not=int64
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/mixed.ll \
; RUN:   -o %t/mixed.lnx.il 2>&1 | count 0
; RUN: %{check} --check-prefix=MIXED --input-file=%t/mixed.lnx.il
; RUN: FileCheck %s --check-prefix=STORE-B --input-file=%t/mixed.lnx.il
; RUN: FileCheck %s --check-prefix=STORE-NZ --input-file=%t/mixed.lnx.il
; RUN: FileCheck %s --check-prefix=STORE-Z1 --input-file=%t/mixed.lnx.il
; RUN: FileCheck %s --check-prefix=STORE-U --input-file=%t/mixed.lnx.il
; RUN: FileCheck %s --check-prefix=STORE-NP --input-file=%t/mixed.lnx.il
; RUN: FileCheck %s --check-prefix=STORE-CZ --input-file=%t/mixed.lnx.il
; RUN: FileCheck %s --check-prefix=STORE-Z3 --input-file=%t/mixed.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/mixed.ll \
; RUN:   -o %t/mixed.win.il 2>&1 | count 0
; RUN: %{check} --check-prefix=MIXED --input-file=%t/mixed.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/mixed.dll %t/mixed.lnx.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/align64.ll \
; RUN:   -o %t/align64.lnx.il 2>&1 | count 0
; RUN: %{check} --check-prefix=A64 --input-file=%t/align64.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/align64.ll \
; RUN:   -o %t/align64.win.il 2>&1 | count 0
; RUN: %{check} --check-prefix=A64 --input-file=%t/align64.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/align64.dll %t/align64.win.il %}

; DEFINE: %{noimage} = %{check} --implicit-check-not="<msil>image" --implicit-check-not=cpblk \
; DEFINE:   --implicit-check-not=".data cil"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/bss-only.ll \
; RUN:   -o %t/bss-only.lnx.il 2>&1 | count 0
; RUN: %{noimage} --check-prefix=NOIMG --input-file=%t/bss-only.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/bss-only.ll \
; RUN:   -o %t/bss-only.win.il 2>&1 | count 0
; RUN: %{noimage} --check-prefix=NOIMG --input-file=%t/bss-only.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/bss-only.dll %t/bss-only.lnx.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/huge.ll \
; RUN:   -o %t/huge.lnx.il 2>&1 | count 0
; RUN: %{check} --check-prefix=HUGE --input-file=%t/huge.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/huge.ll \
; RUN:   -o %t/huge.win.il 2>&1 | count 0
; RUN: %{check} --check-prefix=HUGE --input-file=%t/huge.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/huge.dll %t/huge.win.il %}

; DEFINE: %{diag} = FileCheck %s --check-prefix=TOOBIG --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/toobig.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/toobig.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}

;--- mixed.ll
; Initialized: a +0, b +4, nz +8 (12 bytes). Zero tail: z1 +16 (400 bytes),
; u +416, np +424, cz +432, z3 +438; TotalSize 439, AllocHGlobal(454).
source_filename = "bss-mixed.c"

@a = global i32 1, align 4
@z1 = global [100 x i32] zeroinitializer, align 16
@b = global i8 2, align 1
@u = global i64 undef, align 8
@nz = global float -0.000000e+00, align 4
@np = global ptr null, align 8
@cz = internal constant [3 x i16] zeroinitializer, align 2
@z3 = global i8 0, align 1

; MIXED-LABEL: {{^}}.method public static int32 'read_z1'() cil managed{{$}}
; MIXED:       {{^}}  ldsfld native int 'z1'{{$}}
; MIXED:       {{^}}  ldind.i4{{$}}
; MIXED:       {{^}}} // end of method 'read_z1'{{$}}
define i32 @read_z1() {
  %v = load i32, ptr getelementptr inbounds ([100 x i32], ptr @z1, i64 0, i64 99), align 4
  ret i32 %v
}

; MIXED-NOT:     {{^}}.method
; MIXED:         {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; MIXED:         {{^ *}}.pack 1{{( |$)}}
; MIXED:         {{^ *}}.size 12{{( |$)}}
; MIXED:         {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; MIXED-NEXT:    {{^}}  01 00 00 00 02 00 00 00 00 00 00 80 ){{( |$)}}
; MIXED:         {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; MIXED:         {{^}}.field private static initonly native int '<msil>data'{{$}}
; MIXED-DAG:     {{^}}.field public static initonly native int 'a'{{( |$)}}
; MIXED-DAG:     {{^}}.field public static initonly native int 'z1'{{( |$)}}
; MIXED-DAG:     {{^}}.field public static initonly native int 'b'{{( |$)}}
; MIXED-DAG:     {{^}}.field public static initonly native int 'u'{{( |$)}}
; MIXED-DAG:     {{^}}.field public static initonly native int 'nz'{{( |$)}}
; MIXED-DAG:     {{^}}.field public static initonly native int 'np'{{( |$)}}
; MIXED-DAG:     {{^}}.field assembly static initonly native int 'cz'{{( |$)}}
; MIXED-DAG:     {{^}}.field public static initonly native int 'z3'{{( |$)}}
; MIXED:         {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; MIXED-NEXT:    {{^\{$}}
; MIXED-NEXT:    {{^}}  .maxstack {{[3-9]}}{{$}}
; MIXED-NEXT:    {{^}}  {{ldc\.i4(\.s)? 454( |$)}}
; MIXED-NEXT:    {{^}}  conv.i{{( |$)}}
; MIXED-NEXT:    {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; MIXED-NEXT:    {{^}}  {{ldc\.i4(\.s)? 15( |$)}}
; MIXED-NEXT:    {{^}}  conv.i{{( |$)}}
; MIXED-NEXT:    {{^}}  add{{( |$)}}
; MIXED-NEXT:    {{^}}  {{ldc\.i4(\.s)? -16( |$)}}
; MIXED-NEXT:    {{^}}  conv.i{{( |$)}}
; MIXED-NEXT:    {{^}}  and{{( |$)}}
; MIXED-NEXT:    {{^}}  stsfld native int '<msil>data'{{( |$)}}
; MIXED-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; MIXED-NEXT:    {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; MIXED-NEXT:    {{^}}  {{ldc\.i4(\.s)? 12( |$)}}
; MIXED-NEXT:    {{^}}  cpblk{{( |$)}}
; MIXED-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; MIXED-NEXT:    {{^}}  {{ldc\.i4(\.s)? 12( |$)}}
; MIXED-NEXT:    {{^}}  add{{( |$)}}
; MIXED-NEXT:    {{^}}  {{ldc\.i4(\.| |\.s )0( |$)}}
; MIXED-NEXT:    {{^}}  {{ldc\.i4(\.s)? 427( |$)}}
; MIXED-NEXT:    {{^}}  unaligned. 4{{( |$)}}
; MIXED-NEXT:    {{^}}  initblk{{( |$)}}
; MIXED-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; MIXED-NEXT:    {{^}}  stsfld native int 'a'{{( |$)}}
; MIXED-DAG:     {{^}}  stsfld native int 'z1'{{( |$)}}
; MIXED-DAG:     {{^}}  stsfld native int 'b'{{( |$)}}
; MIXED-DAG:     {{^}}  stsfld native int 'u'{{( |$)}}
; MIXED-DAG:     {{^}}  stsfld native int 'nz'{{( |$)}}
; MIXED-DAG:     {{^}}  stsfld native int 'np'{{( |$)}}
; MIXED-DAG:     {{^}}  stsfld native int 'cz'{{( |$)}}
; MIXED-DAG:     {{^}}  stsfld native int 'z3'{{( |$)}}
; MIXED:         {{^}}  ret{{( |$)}}
; MIXED-NEXT:    {{^}}} // end of method '.cctor'{{$}}
; MIXED-NOT:     {{^}}.method

; STORE-B:       .cctor()
; STORE-B:       {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; STORE-B-NEXT:  {{^}}  add{{( |$)}}
; STORE-B-NEXT:  {{^}}  stsfld native int 'b'{{( |$)}}
; STORE-NZ:      .cctor()
; STORE-NZ:      {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
; STORE-NZ-NEXT: {{^}}  add{{( |$)}}
; STORE-NZ-NEXT: {{^}}  stsfld native int 'nz'{{( |$)}}
; STORE-Z1:      .cctor()
; STORE-Z1:      {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; STORE-Z1-NEXT: {{^}}  add{{( |$)}}
; STORE-Z1-NEXT: {{^}}  stsfld native int 'z1'{{( |$)}}
; STORE-U:       .cctor()
; STORE-U:       {{^}}  {{ldc\.i4(\.s)? 416( |$)}}
; STORE-U-NEXT:  {{^}}  add{{( |$)}}
; STORE-U-NEXT:  {{^}}  stsfld native int 'u'{{( |$)}}
; STORE-NP:      .cctor()
; STORE-NP:      {{^}}  {{ldc\.i4(\.s)? 424( |$)}}
; STORE-NP-NEXT: {{^}}  add{{( |$)}}
; STORE-NP-NEXT: {{^}}  stsfld native int 'np'{{( |$)}}
; STORE-CZ:      .cctor()
; STORE-CZ:      {{^}}  {{ldc\.i4(\.s)? 432( |$)}}
; STORE-CZ-NEXT: {{^}}  add{{( |$)}}
; STORE-CZ-NEXT: {{^}}  stsfld native int 'cz'{{( |$)}}
; STORE-Z3:      .cctor()
; STORE-Z3:      {{^}}  {{ldc\.i4(\.s)? 438( |$)}}
; STORE-Z3-NEXT: {{^}}  add{{( |$)}}
; STORE-Z3-NEXT: {{^}}  stsfld native int 'z3'{{( |$)}}

;--- align64.ll
; MaxAlign 64: big +0 (4 initialized bytes), z +32 in the zero tail;
; TotalSize 35, AllocHGlobal(35 + 63).
source_filename = "bss-align64.c"

@big = global i32 1, align 64
@z = global [3 x i8] zeroinitializer, align 32

; A64-LABEL: {{^}}.method public static native int 'addr_z'() cil managed{{$}}
; A64:       {{^}}  ldsfld native int 'z'{{$}}
; A64:       {{^}}} // end of method 'addr_z'{{$}}
define ptr @addr_z() {
  ret ptr @z
}

; A64-NOT:      {{^}}.method
; A64:          {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; A64:          {{^ *}}.pack 1{{( |$)}}
; A64:          {{^ *}}.size 4{{( |$)}}
; A64:          {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; A64-NEXT:     {{^}}  01 00 00 00 ){{( |$)}}
; A64:          {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; A64:          {{^}}.field private static initonly native int '<msil>data'{{$}}
; A64:          {{^}}.field public static initonly native int 'big'{{( |$)}}
; A64:          {{^}}.field public static initonly native int 'z'{{( |$)}}
; A64:          {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; A64-NEXT:     {{^\{$}}
; A64-NEXT:     {{^}}  .maxstack {{[3-9]}}{{$}}
; A64-NEXT:     {{^}}  {{ldc\.i4(\.s)? 98( |$)}}
; A64-NEXT:     {{^}}  conv.i{{( |$)}}
; A64-NEXT:     {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; A64-NEXT:     {{^}}  {{ldc\.i4(\.s)? 63( |$)}}
; A64-NEXT:     {{^}}  conv.i{{( |$)}}
; A64-NEXT:     {{^}}  add{{( |$)}}
; A64-NEXT:     {{^}}  {{ldc\.i4(\.s)? -64( |$)}}
; A64-NEXT:     {{^}}  conv.i{{( |$)}}
; A64-NEXT:     {{^}}  and{{( |$)}}
; A64-NEXT:     {{^}}  stsfld native int '<msil>data'{{( |$)}}
; A64-NEXT:     {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; A64-NEXT:     {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; A64-NEXT:     {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; A64-NEXT:     {{^}}  cpblk{{( |$)}}
; A64-NEXT:     {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; A64-NEXT:     {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; A64-NEXT:     {{^}}  add{{( |$)}}
; A64-NEXT:     {{^}}  {{ldc\.i4(\.| |\.s )0( |$)}}
; A64-NEXT:     {{^}}  {{ldc\.i4(\.s)? 31( |$)}}
; A64-NEXT:     {{^}}  unaligned. 4{{( |$)}}
; A64-NEXT:     {{^}}  initblk{{( |$)}}
; A64-NEXT:     {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; A64-NEXT:     {{^}}  stsfld native int 'big'{{( |$)}}
; A64-NEXT:     {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; A64-NEXT:     {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; A64-NEXT:     {{^}}  add{{( |$)}}
; A64-NEXT:     {{^}}  stsfld native int 'z'{{( |$)}}
; A64-NEXT:     {{^}}  ret{{( |$)}}
; A64-NEXT:     {{^}}} // end of method '.cctor'{{$}}
; A64-NOT:      {{^}}.method

;--- bss-only.ll
; Only zero-initialized globals: z +0, w +40, u +48 (undef); TotalSize 56.
; The initblk starts at the segment base (InitSize 0): recon asmprinter.md 7.3
; writes the start as 'ldsfld '<msil>data'; ldc.i4 <InitSize>; add', and the
; address fields omit 'ldc.i4 0; add' at offset 0, so the start is matched as
; 'ldsfld' optionally followed by exactly 'ldc.i4 0' and 'add' (one regex over
; those lines), then 'ldc.i4.0', the size 56 and 'initblk', with no prefix (the
; base is 16-aligned).
source_filename = "bss-only.c"

@z = global [10 x i32] zeroinitializer, align 16
@w = internal global i8 0, align 1
@u = global ptr undef, align 8

; NOIMG-LABEL: {{^}}.method public static int32 'read_w'() cil managed{{$}}
; NOIMG:       {{^}}  ldsfld native int 'w'{{$}}
; NOIMG:       {{^}}} // end of method 'read_w'{{$}}
define i32 @read_w() {
  %v = load i8, ptr @w, align 1
  %r = zext i8 %v to i32
  ret i32 %r
}

; NOIMG-NOT:     {{^}}.method
; NOIMG:         {{^}}.field private static initonly native int '<msil>data'{{$}}
; NOIMG:         {{^}}.field public static initonly native int 'z'{{( |$)}}
; NOIMG:         {{^}}.field assembly static initonly native int 'w'{{( |$)}}
; NOIMG:         {{^}}.field public static initonly native int 'u'{{( |$)}}
; NOIMG:         {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; NOIMG-NEXT:    {{^\{$}}
; NOIMG-NEXT:    {{^}}  .maxstack {{[3-9]}}{{$}}
; NOIMG-NEXT:    {{^}}  {{ldc\.i4(\.s)? 71( |$)}}
; NOIMG-NEXT:    {{^}}  conv.i{{( |$)}}
; NOIMG-NEXT:    {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; NOIMG-NEXT:    {{^}}  {{ldc\.i4(\.s)? 15( |$)}}
; NOIMG-NEXT:    {{^}}  conv.i{{( |$)}}
; NOIMG-NEXT:    {{^}}  add{{( |$)}}
; NOIMG-NEXT:    {{^}}  {{ldc\.i4(\.s)? -16( |$)}}
; NOIMG-NEXT:    {{^}}  conv.i{{( |$)}}
; NOIMG-NEXT:    {{^}}  and{{( |$)}}
; NOIMG-NEXT:    {{^}}  stsfld native int '<msil>data'{{( |$)}}
; NOIMG-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; NOIMG-NEXT:    {{^}}  {{(ldc\.i4(\.| |\.s )0[[:space:]]+add[[:space:]]+)?ldc\.i4(\.| |\.s )0( |$)}}
; NOIMG-NEXT:    {{^}}  {{ldc\.i4(\.s)? 56( |$)}}
; NOIMG-NEXT:    {{^}}  initblk{{( |$)}}
; NOIMG-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; NOIMG-NEXT:    {{^}}  stsfld native int 'z'{{( |$)}}
; NOIMG-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; NOIMG-NEXT:    {{^}}  {{ldc\.i4(\.s)? 40( |$)}}
; NOIMG-NEXT:    {{^}}  add{{( |$)}}
; NOIMG-NEXT:    {{^}}  stsfld native int 'w'{{( |$)}}
; NOIMG-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; NOIMG-NEXT:    {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; NOIMG-NEXT:    {{^}}  add{{( |$)}}
; NOIMG-NEXT:    {{^}}  stsfld native int 'u'{{( |$)}}
; NOIMG-NEXT:    {{^}}  ret{{( |$)}}
; NOIMG-NEXT:    {{^}}} // end of method '.cctor'{{$}}
; NOIMG-NOT:     {{^}}.method

;--- huge.ll
; A 1 MiB zero-initialized array after one initialized byte: the image is one
; byte, huge is at +16, and the initblk clears 1048591 bytes.
source_filename = "bss-huge.c"

@one = global i8 1, align 1
@huge = global [1048576 x i8] zeroinitializer, align 16

; HUGE-LABEL: {{^}}.method public static int32 'read_huge'(native int) cil managed{{$}}
; HUGE:       {{^}}  ldsfld native int 'huge'{{$}}
; HUGE:       {{^}}  ldind.u1{{$}}
; HUGE:       {{^}}} // end of method 'read_huge'{{$}}
define i32 @read_huge(i64 %i) {
  %p = getelementptr inbounds [1048576 x i8], ptr @huge, i64 0, i64 %i
  %v = load i8, ptr %p, align 1
  %r = zext i8 %v to i32
  ret i32 %r
}

; HUGE-NOT:      {{^}}.method
; HUGE:         {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; HUGE:         {{^ *}}.pack 1{{( |$)}}
; HUGE:         {{^ *}}.size 1{{( |$)}}
; HUGE:         {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; HUGE-NEXT:    {{^}}  01 ){{( |$)}}
; HUGE:         {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; HUGE:         {{^}}.field private static initonly native int '<msil>data'{{$}}
; HUGE:         {{^}}.field public static initonly native int 'one'{{( |$)}}
; HUGE:         {{^}}.field public static initonly native int 'huge'{{( |$)}}
; HUGE:         {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; HUGE-NEXT:    {{^\{$}}
; HUGE-NEXT:    {{^}}  .maxstack {{[3-9]}}{{$}}
; HUGE-NEXT:    {{^}}  {{ldc\.i4(\.s)? 1048607( |$)}}
; HUGE-NEXT:    {{^}}  conv.i{{( |$)}}
; HUGE-NEXT:    {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; HUGE-NEXT:    {{^}}  {{ldc\.i4(\.s)? 15( |$)}}
; HUGE-NEXT:    {{^}}  conv.i{{( |$)}}
; HUGE-NEXT:    {{^}}  add{{( |$)}}
; HUGE-NEXT:    {{^}}  {{ldc\.i4(\.s)? -16( |$)}}
; HUGE-NEXT:    {{^}}  conv.i{{( |$)}}
; HUGE-NEXT:    {{^}}  and{{( |$)}}
; HUGE-NEXT:    {{^}}  stsfld native int '<msil>data'{{( |$)}}
; HUGE-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; HUGE-NEXT:    {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; HUGE-NEXT:    {{^}}  {{ldc\.i4(\.| |\.s )1( |$)}}
; HUGE-NEXT:    {{^}}  cpblk{{( |$)}}
; HUGE-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; HUGE-NEXT:    {{^}}  {{ldc\.i4(\.| |\.s )1( |$)}}
; HUGE-NEXT:    {{^}}  add{{( |$)}}
; HUGE-NEXT:    {{^}}  {{ldc\.i4(\.| |\.s )0( |$)}}
; HUGE-NEXT:    {{^}}  {{ldc\.i4(\.s)? 1048591( |$)}}
; HUGE-NEXT:    {{^}}  unaligned. 1{{( |$)}}
; HUGE-NEXT:    {{^}}  initblk{{( |$)}}
; HUGE-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; HUGE-NEXT:    {{^}}  stsfld native int 'one'{{( |$)}}
; HUGE-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; HUGE-NEXT:    {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; HUGE-NEXT:    {{^}}  add{{( |$)}}
; HUGE-NEXT:    {{^}}  stsfld native int 'huge'{{( |$)}}
; HUGE-NEXT:    {{^}}  ret{{( |$)}}
; HUGE-NEXT:    {{^}}} // end of method '.cctor'{{$}}
; HUGE-NOT:      {{^}}.method

;--- toobig.ll
; A 2 GiB zero-initialized array after one initialized byte: huge is at +16,
; so the segment has 16 + 2^31 = 2147483664 bytes.
source_filename = "bss-toobig.c"

; TOOBIG: {{^}}error: {{.*}}the data segment of 2147483664 bytes is larger than MSIL supports (2 GiB){{$}}

@one = global i8 1, align 1
@huge = global [2147483648 x i8] zeroinitializer, align 16

define ptr @addr_huge() {
  ret ptr @huge
}
