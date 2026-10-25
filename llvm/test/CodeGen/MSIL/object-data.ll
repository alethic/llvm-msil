; Data in a CIL object (ImplementationPlan.md 5.8.8 SEGMENTS and DATA_RELOCS,
; 5.8.10; M19): one segment per defined non-common variable, in SYMTAB order
; (no per-module data image, D12 as amended), never a FieldRVA row.
; - Name: the IR section verbatim ('$' kept: '.CRT$XCU'), else '.rodata',
;   '.data' or '.bss' by getKindForGlobal.
; - Flags: STRINGS for an unnamed_addr C string (isMergeable1ByteCString),
;   READONLY for constants (with or without relocations), ZERO for a
;   zero-initialized variable (no bytes), RETAIN for llvm.used (which also
;   sets NO_STRIP on the symbol). An initializer that mixes zeros with undef
;   or poison (zu, zv and the internal zw) is zero-initialized too, as
;   getKindForGlobal's BSS rule has it: a '.bss' segment with ZERO and no
;   bytes, like zeroinitializer (object-coff-view.ll checks its 'B'/'b').
; - A section on a function is ignored (5.8.10: methods have no addresses):
;   text_fn compiles with no diagnostic, and no segment, record string,
;   listing line or object-form text names 'mytext'.
; - AlignLog2 from the variable's alignment (printed in bytes: 64 here); Size
;   its allocation size; relocation sites hold zero bytes.
; DATA_RELOCS, sorted by (segment, offset): ADDR64 (with addends 0, 2 and -4,
; to defined data and to an undefined variable, at an unaligned site),
; ADDR32 (the low half of an address), FNADDR64 and FNADDR32 (function
; pointers, addend 0, also in a named section), and DIFF of width 4 and 8:
; addr(Symbol) - addr(Symbol2) + addend, with a getelementptr on the
; minuend's side and the site's own symbol as subtrahend (a relative
; offset). Object mode leaves DIFFs unfolded (M19.b (4)): whole-program text
; folds them (data-diff-reloc.ll).
; Both x86_64 triples at -O2 and -O0.

; REQUIRES: llvm-cil-dump
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %s -o %t.lnx.o 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %s -o %t.win.o 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -O0 -verify-machineinstrs < %s -o %t.lnx.o0.o 2>&1 \
; RUN:   | count 0
; RUN: llvm-cil-dump --link-info %t.lnx.o \
; RUN:   | FileCheck %s --check-prefixes=CHECK,LNX --match-full-lines --strict-whitespace --implicit-check-not=mytext
; RUN: llvm-cil-dump --link-info %t.win.o \
; RUN:   | FileCheck %s --check-prefixes=CHECK,WIN --match-full-lines --strict-whitespace --implicit-check-not=mytext
; RUN: llvm-cil-dump --link-info %t.lnx.o0.o \
; RUN:   | FileCheck %s --check-prefixes=CHECK,LNX --match-full-lines --strict-whitespace --implicit-check-not=mytext

; No initialization code and no image: one address field per defined
; variable, assembly for the local ones (R4).
; RUN: llvm-cil-dump --canonical %t.win.o \
; RUN:   | FileCheck %s --check-prefix=CANON --match-full-lines --strict-whitespace --implicit-check-not=.cctor \
; RUN:       --implicit-check-not=".data " --implicit-check-not="<msil>image" --implicit-check-not="<msil>data" \
; RUN:       --implicit-check-not=mytext
; RUN: llvm-cil-dump --tables %t.lnx.o \
; RUN:   | FileCheck %s --check-prefix=TBL --implicit-check-not="Table: FieldRVA" --implicit-check-not="Table: ClassLayout"

; The object oracle (5.12.6, as in object-form-text.ll): ilasm's object of
; the object-form text is canonically equal to llc's object, record and
; tokens included.
; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{o} =
; DEFINE: %{oracle} = %ilasm-verify -output=%{o}.ilasm.o %{o}.il && \
; DEFINE:   %{canon} %{o}.o > %{o}.canon1 && %{canon} %{o}.ilasm.o > %{o}.canon2 && diff %{o}.canon1 %{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %{o}.o > %{o}.link1 && llvm-cil-dump --link-info %{o}.ilasm.o > %{o}.link2 && \
; DEFINE:   diff %{o}.link1 %{o}.link2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %s -o %t.lnx.il 2>&1 | count 0
; REDEFINE: %{o} = %t.lnx
; RUN: %if ilasm %{ %{oracle} %}

; The object-form text has text_fn and no 'mytext' either.
; RUN: FileCheck %s --check-prefix=TEXT --implicit-check-not=mytext --input-file=%t.lnx.il
; TEXT: {{^}}.method public static int32 'text_fn'(int32) cil managed{{$}}

; LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; CHECK-NEXT:Symbol 0 DATA LOCAL DEFAULT 'str' field=0x04000001 segment=0 offset=0 size=6
; CHECK-NEXT:Symbol 1 DATA GLOBAL DEFAULT 'ro' field=0x04000002 segment=1 offset=0 size=4
; CHECK-NEXT:Symbol 2 DATA GLOBAL DEFAULT 'zero' field=0x04000003 segment=2 offset=0 size=8
; CHECK-NEXT:Symbol 3 DATA GLOBAL DEFAULT 'al64' field=0x04000004 segment=3 offset=0 size=4
; CHECK-NEXT:Symbol 4 DATA LOCAL DEFAULT NO_STRIP 'kept' field=0x04000005 segment=4 offset=0 size=4
; CHECK-NEXT:Symbol 5 DATA GLOBAL DEFAULT 'ptrs' field=0x04000006 segment=5 offset=0 size=32
; CHECK-NEXT:Symbol 6 DATA GLOBAL DEFAULT 'ctab' field=0x04000007 segment=6 offset=0 size=16
; CHECK-NEXT:Symbol 7 DATA GLOBAL DEFAULT 'p32' field=0x04000008 segment=7 offset=0 size=12
; CHECK-NEXT:Symbol 8 DATA GLOBAL DEFAULT 'd4' field=0x04000009 segment=8 offset=0 size=4
; CHECK-NEXT:Symbol 9 DATA GLOBAL DEFAULT 'd8' field=0x0400000A segment=9 offset=0 size=8
; CHECK-NEXT:Symbol 10 DATA GLOBAL DEFAULT 'unal' field=0x0400000B segment=10 offset=0 size=9
; CHECK-NEXT:Symbol 11 DATA LOCAL DEFAULT NO_STRIP 'crt' field=0x0400000C segment=11 offset=0 size=8
; CHECK-NEXT:Symbol 12 DATA GLOBAL DEFAULT 'named' field=0x0400000D segment=12 offset=0 size=4
; CHECK-NEXT:Symbol 13 DATA GLOBAL DEFAULT UNDEFINED 'ext_data'
; CHECK-NEXT:Symbol 14 DATA GLOBAL DEFAULT 'zu' field=0x0400000E segment=13 offset=0 size=4
; CHECK-NEXT:Symbol 15 DATA GLOBAL DEFAULT 'zv' field=0x0400000F segment=14 offset=0 size=8
; CHECK-NEXT:Symbol 16 DATA LOCAL DEFAULT 'zw' field=0x04000010 segment=15 offset=0 size=4
; CHECK-NEXT:Symbol 17 FUNCTION GLOBAL DEFAULT 'fn' method=0x06000001
; CHECK-NEXT:Symbol 18 FUNCTION GLOBAL DEFAULT 'text_fn' method=0x06000002
; CHECK-NEXT:Segment 0 '.rodata' align=1 size=6 flags=STRINGS|READONLY
; CHECK-NEXT:  68 65 6C 6C 6F 00
; CHECK-NEXT:Segment 1 '.rodata' align=4 size=4 flags=READONLY
; CHECK-NEXT:  2A 00 00 00
; CHECK-NEXT:Segment 2 '.bss' align=8 size=8 flags=ZERO
; CHECK-NEXT:Segment 3 '.data' align=64 size=4 flags=0
; CHECK-NEXT:  01 00 00 00
; CHECK-NEXT:Segment 4 '.data' align=4 size=4 flags=RETAIN
; CHECK-NEXT:  03 00 00 00
; CHECK-NEXT:Segment 5 '.data' align=8 size=32 flags=0
; CHECK-NEXT:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
; CHECK-NEXT:Segment 6 '.rodata' align=8 size=16 flags=READONLY
; CHECK-NEXT:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
; CHECK-NEXT:Segment 7 '.data' align=4 size=12 flags=0
; CHECK-NEXT:  00 00 00 00 00 00 00 00 07 00 00 00
; CHECK-NEXT:Segment 8 '.data' align=4 size=4 flags=0
; CHECK-NEXT:  00 00 00 00
; CHECK-NEXT:Segment 9 '.data' align=8 size=8 flags=0
; CHECK-NEXT:  00 00 00 00 00 00 00 00
; CHECK-NEXT:Segment 10 '.data' align=1 size=9 flags=0
; CHECK-NEXT:  01 00 00 00 00 00 00 00 00
; CHECK-NEXT:Segment 11 '.CRT$XCU' align=8 size=8 flags=RETAIN
; CHECK-NEXT:  00 00 00 00 00 00 00 00
; CHECK-NEXT:Segment 12 'myreg' align=4 size=4 flags=0
; CHECK-NEXT:  05 00 00 00
; CHECK-NEXT:Segment 13 '.bss' align=1 size=4 flags=ZERO
; CHECK-NEXT:Segment 14 '.bss' align=4 size=8 flags=ZERO
; CHECK-NEXT:Segment 15 '.bss' align=2 size=4 flags=ZERO
; CHECK-NEXT:Reloc ADDR64 segment=5 offset=0 symbol=1 'ro' addend=0
; CHECK-NEXT:Reloc ADDR64 segment=5 offset=8 symbol=1 'ro' addend=2
; CHECK-NEXT:Reloc FNADDR64 segment=5 offset=16 symbol=17 'fn' addend=0
; CHECK-NEXT:Reloc ADDR64 segment=6 offset=0 symbol=0 'str' addend=0
; CHECK-NEXT:Reloc ADDR64 segment=6 offset=8 symbol=13 'ext_data' addend=0
; CHECK-NEXT:Reloc ADDR32 segment=7 offset=0 symbol=2 'zero' addend=0
; CHECK-NEXT:Reloc FNADDR32 segment=7 offset=4 symbol=17 'fn' addend=0
; CHECK-NEXT:Reloc DIFF width=4 segment=8 offset=0 symbol=1 'ro' symbol2=8 'd4' addend=0
; CHECK-NEXT:Reloc DIFF width=8 segment=9 offset=0 symbol=2 'zero' symbol2=1 'ro' addend=4
; CHECK-NEXT:Reloc ADDR64 segment=10 offset=1 symbol=3 'al64' addend=-4
; CHECK-NEXT:Reloc FNADDR64 segment=11 offset=0 symbol=17 'fn' addend=0
; CHECK-NOT:{{.}}

; CANON:.class '<Module>' flags=0x00000000
; CANON-NEXT:  .field 'al64' native int flags=0x0036
; CANON-NEXT:  .field 'crt' native int flags=0x0033
; CANON-NEXT:  .field 'ctab' native int flags=0x0036
; CANON-NEXT:  .field 'd4' native int flags=0x0036
; CANON-NEXT:  .field 'd8' native int flags=0x0036
; CANON-NEXT:  .field 'kept' native int flags=0x0033
; CANON-NEXT:  .field 'named' native int flags=0x0036
; CANON-NEXT:  .field 'p32' native int flags=0x0036
; CANON-NEXT:  .field 'ptrs' native int flags=0x0036
; CANON-NEXT:  .field 'ro' native int flags=0x0036
; CANON-NEXT:  .field 'str' native int flags=0x0033
; CANON-NEXT:  .field 'unal' native int flags=0x0036
; CANON-NEXT:  .field 'zero' native int flags=0x0036
; CANON-NEXT:  .field 'zu' native int flags=0x0036
; CANON-NEXT:  .field 'zv' native int flags=0x0036
; CANON-NEXT:  .field 'zw' native int flags=0x0033
; CANON-NEXT:  .method 'fn' int32(int32) flags=0x0016 impl=0x0000
; CANON-NEXT:    .maxstack 1
; CANON-NEXT:    L0: ldarg.0
; CANON-NEXT:    L1: ret
; CANON-NEXT:  .method 'text_fn' int32(int32) flags=0x0016 impl=0x0000
; CANON-NEXT:    .maxstack 1
; CANON-NEXT:    L0: ldarg.0
; CANON-NEXT:    L1: ret
; CANON-NEXT:module .custom {{.*}}
; CANON-NOT:{{.}}

; TBL: Table: Field rows=16

source_filename = "data.c"

@str = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@ro = constant i32 42, align 4
@zero = global [8 x i8] zeroinitializer, align 8
@al64 = global i32 1, align 64
@kept = internal global i32 3, align 4
@ptrs = global [4 x ptr] [ptr @ro, ptr getelementptr (i8, ptr @ro, i64 2), ptr @fn, ptr null], align 8
@ctab = constant [2 x ptr] [ptr @str, ptr @ext_data], align 8
@p32 = global <{ i32, i32, i32 }> <{ i32 trunc (i64 ptrtoint (ptr @zero to i64) to i32), i32 trunc (i64 ptrtoint (ptr @fn to i64) to i32), i32 7 }>, align 4
@d4 = global i32 trunc (i64 sub (i64 ptrtoint (ptr @ro to i64), i64 ptrtoint (ptr @d4 to i64)) to i32), align 4
@d8 = global i64 sub (i64 ptrtoint (ptr getelementptr (i8, ptr @zero, i64 4) to i64), i64 ptrtoint (ptr @ro to i64)), align 8
@unal = global <{ i8, ptr }> <{ i8 1, ptr getelementptr (i8, ptr @al64, i64 -4) }>, align 1
@crt = internal global ptr @fn, section ".CRT$XCU", align 8
@named = global i32 5, section "myreg", align 4
@ext_data = external global i32, align 4
@zu = global { i8, [3 x i8] } { i8 0, [3 x i8] undef }, align 1
@zv = global [2 x i32] [i32 0, i32 undef], align 4
@zw = internal global [2 x i16] [i16 poison, i16 0], align 2
@llvm.used = appending global [2 x ptr] [ptr @kept, ptr @crt], section "llvm.metadata"

define i32 @fn(i32 %x) {
  ret i32 %x
}

define i32 @text_fn(i32 %x) section "mytext" {
  ret i32 %x
}
