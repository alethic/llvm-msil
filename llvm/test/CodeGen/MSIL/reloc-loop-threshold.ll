; The relocation loop of <Module>::.cctor (ImplementationPlan.md 5.9.10, M20;
; plan 5.1 as amended by the section 12 row of 2026-10-21 for M20). With at
; most -msil-reloc-loop-threshold data relocations (default 32) the .cctor
; stores each relocation straight-line, Phase 1's sequence instruction for
; instruction, so the text does not change (global-relocs.ll pins it). Above
; the threshold it applies them from an RVA table instead:
; - '<msil>relocs' of type '<msil>relocs_t' (explicit layout, .pack 1), one
;   16-byte entry {u32 Site; u32 Kind; i64 Value} per relocation in site
;   order: Kind 0 stores '<msil>data' + Value (pointer width), 1 the same
;   truncated to 4 bytes, 2 the pointer read at '<msil>data' + Value (an
;   8-byte address slot), 3 the same truncated to 4 bytes. Relocations to
;   data in the image use kinds 0 and 1 with Value = offset + addend;
;   function pointers use kinds 2 and 3;
; - one 8-byte address slot per distinct (field, addend) pair of the
;   relocations without an image target, 8-aligned after the zero part
;   (5.9.9 rule 4), so TotalSize and AllocHGlobal's size grow; the
;   straight-line part fills each slot from its field after the function
;   address fields, so there is still exactly one ldftn per function (M9's
;   Framework function pointers);
; - a loop that applies the entries with 'unaligned. 1' stores (Design.md
;   risk 1: a long straight-line .cctor drops the JIT into MinOpts).
; seven.ll is global-relocs.ll's unaligned.ll (unaligned 8- and 4-byte sites,
; function pointers): straight-line at the default threshold and at 7, the
; loop at 6, 1 and 0. one.ll has a single relocation: straight-line at 1,
; the loop at 0. many.ll has 33 relocations (32 to data, one function
; pointer): the loop at the default threshold on both triples, straight-line
; at 33. thirtytwo.ll has exactly 32 relocations of mixed kinds (29 aligned
; 8-byte data pointers, an unaligned 8-byte site, a 4-byte site and a
; function pointer): straight-line at the default threshold on both triples,
; the loop at 31. Together they bracket the default at exactly 32 (exit
; criterion 1: at the default threshold only modules above 32 data
; relocations change), so a changed default, or a change in how
; relocations are counted, fails here; gate step 13 compares with the loop
; out of reach and cannot see either. Every output assembles, on both
; x86_64 triples.

; DEFINE: %{llc} = llc -verify-machineinstrs -asm-verbose=false
; RUN: split-file %s %t

; seven.ll, straight-line.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/seven.ll -o %t/seven.def.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=S7 --implicit-check-not="<msil>relocs" --input-file=%t/seven.def.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -msil-reloc-loop-threshold=7 < %t/seven.ll \
; RUN:   -o %t/seven.t7.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=S7 --implicit-check-not="<msil>relocs" --input-file=%t/seven.t7.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 -msil-reloc-loop-threshold=32 < %t/seven.ll \
; RUN:   -o %t/seven.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=S7 --implicit-check-not="<msil>relocs" --input-file=%t/seven.o0.il

; seven.ll, the loop: the table (T7), the .cctor (L7) and its stores (U7).
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -msil-reloc-loop-threshold=6 < %t/seven.ll \
; RUN:   -o %t/seven.t6.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=T7 --input-file=%t/seven.t6.il
; RUN: FileCheck %s --check-prefix=L7 --implicit-check-not=ldftn --input-file=%t/seven.t6.il
; RUN: FileCheck %s --check-prefix=U7 --input-file=%t/seven.t6.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -msil-reloc-loop-threshold=1 < %t/seven.ll \
; RUN:   -o %t/seven.t1.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=T7 --input-file=%t/seven.t1.il
; RUN: FileCheck %s --check-prefix=L7 --implicit-check-not=ldftn --input-file=%t/seven.t1.il
; RUN: FileCheck %s --check-prefix=U7 --input-file=%t/seven.t1.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-reloc-loop-threshold=0 < %t/seven.ll \
; RUN:   -o %t/seven.t0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=T7 --input-file=%t/seven.t0.il
; RUN: FileCheck %s --check-prefix=L7 --implicit-check-not=ldftn --input-file=%t/seven.t0.il
; RUN: FileCheck %s --check-prefix=U7 --input-file=%t/seven.t0.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -msil-reloc-loop-threshold=0 < %t/seven.ll \
; RUN:   -o %t/seven.t0.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=T7 --input-file=%t/seven.t0.lnx.il

; one.ll.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -msil-reloc-loop-threshold=1 < %t/one.ll \
; RUN:   -o %t/one.t1.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=S1 --implicit-check-not="<msil>relocs" --input-file=%t/one.t1.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -msil-reloc-loop-threshold=0 < %t/one.ll \
; RUN:   -o %t/one.t0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=T1 --input-file=%t/one.t0.il

; many.ll.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/many.ll -o %t/many.def.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=TC --implicit-check-not=ldftn --input-file=%t/many.def.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/many.ll -o %t/many.def.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=TC --implicit-check-not=ldftn --input-file=%t/many.def.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -msil-reloc-loop-threshold=33 < %t/many.ll \
; RUN:   -o %t/many.t33.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=SC --implicit-check-not="<msil>relocs" --input-file=%t/many.t33.il

; thirtytwo.ll: straight-line at the default threshold (no
; -msil-reloc-loop-threshold) on both triples, the loop at 31.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/thirtytwo.ll -o %t/t32.def.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=S32 --implicit-check-not="<msil>relocs" --input-file=%t/t32.def.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/thirtytwo.ll -o %t/t32.def.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=S32 --implicit-check-not="<msil>relocs" --input-file=%t/t32.def.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/thirtytwo.ll -o %t/t32.def.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=S32 --implicit-check-not="<msil>relocs" --input-file=%t/t32.def.win.o0.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -msil-reloc-loop-threshold=31 < %t/thirtytwo.ll \
; RUN:   -o %t/t32.t31.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=L32 --implicit-check-not=ldftn --input-file=%t/t32.t31.il

; RUN: %if ilasm %{ %ilasm-verify -output=%t/seven.def.dll %t/seven.def.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/seven.t6.dll %t/seven.t6.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/seven.t1.dll %t/seven.t1.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/seven.t0.dll %t/seven.t0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/one.t0.dll %t/one.t0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/many.def.dll %t/many.def.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/many.def.win.dll %t/many.def.win.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/many.t33.dll %t/many.t33.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/t32.def.dll %t/t32.def.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/t32.def.win.dll %t/t32.def.win.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/t32.t31.dll %t/t32.t31.il %}

;--- seven.ll
; Layout (MaxAlign 16): x +0; pk +16 with 8-byte sites at +17 (x), +26
; (x + 2), +36 (fn) and +48 (x); q4 +56 with 4-byte sites at +58 (x + 4),
; +63 (fn) and +68 (x); h +72; the zero tail zz +74; TotalSize 76. In loop
; mode the one slot, of (fn, 0), is at 80 and TotalSize is 88.
source_filename = "reloc-loop-threshold.c"

@x = global i32 11, align 4
@pk = global <{ i8, ptr, i8, ptr, i16, ptr, i32, ptr }> <{ i8 97, ptr @x, i8 98, ptr getelementptr (i8, ptr @x, i64 2), i16 7, ptr @fn, i32 9, ptr @x }>, align 16
@q4 = global <{ i16, i32, i8, i32, i8, i32 }> <{ i16 1, i32 trunc (i64 ptrtoint (ptr getelementptr (i8, ptr @x, i64 4) to i64) to i32), i8 2, i32 trunc (i64 ptrtoint (ptr @fn to i64) to i32), i8 3, i32 trunc (i64 ptrtoint (ptr @x to i64) to i32) }>, align 8
@h = global i16 5, align 2
@zz = global i16 0, align 2

define i32 @fn(i32 %v) {
  ret i32 %v
}

define i32 @main() {
  %p = load ptr, ptr getelementptr (i8, ptr @pk, i64 1), align 1
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

; Straight-line: the seven stores of global-relocs.ll, with their D24
; prefixes, right after the function address field.
; S7-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; S7:       {{^}}  {{ldc\.i4(\.s)? 91( |$)}}
; S7:       {{^}}  ldftn int32 'fn'(int32){{( |$)}}
; S7-NEXT:  {{^}}  stsfld native int '<msil>fn_fn'{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; S7-NEXT:  {{^}}  {{ldc\.i4(\.s)? 17( |$)}}
; S7-NEXT:  {{^}}  add{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int 'x'{{( |$)}}
; S7-NEXT:  {{^}}  unaligned. 1{{( |$)}}
; S7-NEXT:  {{^}}  stind.i{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; S7-NEXT:  {{^}}  {{ldc\.i4(\.s)? 26( |$)}}
; S7-NEXT:  {{^}}  add{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int 'x'{{( |$)}}
; S7-NEXT:  {{^}}  {{ldc\.i4(\.| |\.s )2( |$)}}
; S7-NEXT:  {{^}}  add{{( |$)}}
; S7-NEXT:  {{^}}  unaligned. 2{{( |$)}}
; S7-NEXT:  {{^}}  stind.i{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; S7-NEXT:  {{^}}  {{ldc\.i4(\.s)? 36( |$)}}
; S7-NEXT:  {{^}}  add{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int '<msil>fn_fn'{{( |$)}}
; S7-NEXT:  {{^}}  unaligned. 4{{( |$)}}
; S7-NEXT:  {{^}}  stind.i{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; S7-NEXT:  {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; S7-NEXT:  {{^}}  add{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int 'x'{{( |$)}}
; S7-NEXT:  {{^}}  stind.i{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; S7-NEXT:  {{^}}  {{ldc\.i4(\.s)? 58( |$)}}
; S7-NEXT:  {{^}}  add{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int 'x'{{( |$)}}
; S7-NEXT:  {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; S7-NEXT:  {{^}}  add{{( |$)}}
; S7-NEXT:  {{^}}  conv.i4{{( |$)}}
; S7-NEXT:  {{^}}  unaligned. 2{{( |$)}}
; S7-NEXT:  {{^}}  stind.i4{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; S7-NEXT:  {{^}}  {{ldc\.i4(\.s)? 63( |$)}}
; S7-NEXT:  {{^}}  add{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int '<msil>fn_fn'{{( |$)}}
; S7-NEXT:  {{^}}  conv.i4{{( |$)}}
; S7-NEXT:  {{^}}  unaligned. 1{{( |$)}}
; S7-NEXT:  {{^}}  stind.i4{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; S7-NEXT:  {{^}}  {{ldc\.i4(\.s)? 68( |$)}}
; S7-NEXT:  {{^}}  add{{( |$)}}
; S7-NEXT:  {{^}}  ldsfld native int 'x'{{( |$)}}
; S7-NEXT:  {{^}}  conv.i4{{( |$)}}
; S7-NEXT:  {{^}}  stind.i4{{( |$)}}
; S7-NEXT:  {{^}}  ret{{( |$)}}
; S7-NEXT:  {{^}}} // end of method '.cctor'{{$}}

; The table: (17, 0, 0), (26, 0, 2), (36, 2, 80), (48, 0, 0), (58, 1, 4),
; (63, 3, 80), (68, 1, 0).
; T7:      {{^}}.class private explicit ansi sealed '<msil>relocs_t' extends [mscorlib]System.ValueType{{$}}
; T7:      {{^ *}}.pack 1{{( |$)}}
; T7-NEXT: {{^ *}}.size 112{{( |$)}}
; T7:      {{^}}.data cil '{{[^']+}}' = bytearray ({{$}}
; T7-NEXT: {{^}}  11 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; T7-NEXT: {{^}}  1A 00 00 00 00 00 00 00 02 00 00 00 00 00 00 00{{( |$)}}
; T7-NEXT: {{^}}  24 00 00 00 02 00 00 00 50 00 00 00 00 00 00 00{{( |$)}}
; T7-NEXT: {{^}}  30 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; T7-NEXT: {{^}}  3A 00 00 00 01 00 00 00 04 00 00 00 00 00 00 00{{( |$)}}
; T7-NEXT: {{^}}  3F 00 00 00 03 00 00 00 50 00 00 00 00 00 00 00{{( |$)}}
; T7-NEXT: {{^}}  44 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 ){{( |$)}}
; T7:      {{^}}.field {{.*}}static valuetype '<msil>relocs_t' '<msil>relocs' at '{{[^']+}}'{{$}}

; The loop's .cctor: AllocHGlobal(88 + 15), the base, the image, the address
; and function fields, then the slot of fn at 80 from its field, then the
; loop over the table.
; L7-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; L7:       {{^}}  {{ldc\.i4(\.s)? 103( |$)}}
; L7-NEXT:  {{^}}  conv.i{{( |$)}}
; L7-NEXT:  {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; L7-NEXT:  {{^}}  {{ldc\.i4(\.s)? 15( |$)}}
; L7-NEXT:  {{^}}  conv.i{{( |$)}}
; L7-NEXT:  {{^}}  add{{( |$)}}
; L7-NEXT:  {{^}}  {{ldc\.i4(\.s)? -16( |$)}}
; L7-NEXT:  {{^}}  conv.i{{( |$)}}
; L7-NEXT:  {{^}}  and{{( |$)}}
; L7-NEXT:  {{^}}  stsfld native int '<msil>data'{{( |$)}}
; L7-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; L7-NEXT:  {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; L7-NEXT:  {{^}}  {{ldc\.i4(\.s)? 74( |$)}}
; L7-NEXT:  {{^}}  cpblk{{( |$)}}
; L7:       {{^}}  stsfld native int 'zz'{{( |$)}}
; L7-NEXT:  {{^}}  ldftn int32 'fn'(int32){{( |$)}}
; L7-NEXT:  {{^}}  stsfld native int '<msil>fn_fn'{{( |$)}}
; L7-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; L7-NEXT:  {{^}}  {{ldc\.i4(\.s)? 80( |$)}}
; L7-NEXT:  {{^}}  add{{( |$)}}
; L7-NEXT:  {{^}}  ldsfld native int '<msil>fn_fn'{{( |$)}}
; L7-NEXT:  {{^}}  stind.i{{( |$)}}
; L7:       {{^}}  ldsflda valuetype '<msil>relocs_t' '<msil>relocs'{{( |$)}}
; L7:       {{^}}  ret{{( |$)}}
; L7-NEXT:  {{^}}} // end of method '.cctor'{{$}}

; The loop stores 8 and 4 bytes, each through 'unaligned. 1'.
; U7-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; U7:       {{^}}  ldsflda valuetype '<msil>relocs_t' '<msil>relocs'{{( |$)}}
; U7-DAG:   {{^}}  unaligned. 1{{( |$)}}
; U7-DAG:   {{^}}  unaligned. 1{{( |$)}}
; U7-DAG:   {{^}}  stind.i{{( |$)}}
; U7-DAG:   {{^}}  stind.i4{{( |$)}}
; U7:       {{^}}} // end of method '.cctor'{{$}}

;--- one.ll
; x +0, p +8 = &x: one relocation, no slot (TotalSize 16).
source_filename = "reloc-loop-one.c"

@x = global i32 3, align 4
@p = global ptr @x, align 8

; S1-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; S1:       {{^}}  stsfld native int 'p'{{( |$)}}
; S1-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; S1-NEXT:  {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
; S1-NEXT:  {{^}}  add{{( |$)}}
; S1-NEXT:  {{^}}  ldsfld native int 'x'{{( |$)}}
; S1-NEXT:  {{^}}  stind.i{{( |$)}}
; S1-NEXT:  {{^}}  ret{{( |$)}}

; T1:      {{^}}.class private explicit ansi sealed '<msil>relocs_t' extends [mscorlib]System.ValueType{{$}}
; T1:      {{^ *}}.size 16{{( |$)}}
; T1:      {{^}}.data cil '{{[^']+}}' = bytearray ({{$}}
; T1-NEXT: {{^}}  08 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ){{( |$)}}
; T1:      {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; T1:      {{^}}  {{ldc\.i4(\.s)? 31( |$)}}
; T1-NEXT: {{^}}  conv.i{{( |$)}}
; T1:      {{^}}  ldsflda valuetype '<msil>relocs_t' '<msil>relocs'{{( |$)}}
; T1-NOT:  {{^}}  ldsfld native int 'x'{{( |$)}}
; T1:      {{^}}} // end of method '.cctor'{{$}}

;--- many.ll
; arr +0 (16 bytes), tbl +16: 32 pointers into arr (arr + 4 * (k % 4)) and,
; last, a pointer to f; 280 initialized bytes. Loop mode: 33 entries (32 of
; kind 0, the last of kind 2 through the slot at 280), TotalSize 288.
source_filename = "reloc-loop-many.c"

@arr = global [4 x i32] [i32 1, i32 2, i32 3, i32 4], align 16
@tbl = global [33 x ptr] [ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @f], align 16

define i32 @f() {
  ret i32 7
}

; TC:      {{^}}.class private explicit ansi sealed '<msil>relocs_t' extends [mscorlib]System.ValueType{{$}}
; TC:      {{^ *}}.size 528{{( |$)}}
; TC:      {{^}}.data cil '{{[^']+}}' = bytearray ({{$}}
; TC-NEXT: {{^}}  10 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  18 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  20 00 00 00 00 00 00 00 08 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  28 00 00 00 00 00 00 00 0C 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  30 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  38 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  40 00 00 00 00 00 00 00 08 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  48 00 00 00 00 00 00 00 0C 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  50 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  58 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  60 00 00 00 00 00 00 00 08 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  68 00 00 00 00 00 00 00 0C 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  70 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  78 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  80 00 00 00 00 00 00 00 08 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  88 00 00 00 00 00 00 00 0C 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  90 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  98 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  A0 00 00 00 00 00 00 00 08 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  A8 00 00 00 00 00 00 00 0C 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  B0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  B8 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  C0 00 00 00 00 00 00 00 08 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  C8 00 00 00 00 00 00 00 0C 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  D0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  D8 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  E0 00 00 00 00 00 00 00 08 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  E8 00 00 00 00 00 00 00 0C 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  F0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  F8 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  00 01 00 00 00 00 00 00 08 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  08 01 00 00 00 00 00 00 0C 00 00 00 00 00 00 00{{( |$)}}
; TC-NEXT: {{^}}  10 01 00 00 02 00 00 00 18 01 00 00 00 00 00 00 ){{( |$)}}
; TC:      {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; TC:      {{^}}  {{ldc\.i4(\.s)? 303( |$)}}
; TC-NEXT: {{^}}  conv.i{{( |$)}}
; TC:      {{^}}  ldftn int32 'f'(){{( |$)}}
; TC-NEXT: {{^}}  stsfld native int '<msil>fn_f'{{( |$)}}
; TC-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TC-NEXT: {{^}}  {{ldc\.i4(\.s)? 280( |$)}}
; TC-NEXT: {{^}}  add{{( |$)}}
; TC-NEXT: {{^}}  ldsfld native int '<msil>fn_f'{{( |$)}}
; TC-NEXT: {{^}}  stind.i{{( |$)}}
; TC:      {{^}}  ldsflda valuetype '<msil>relocs_t' '<msil>relocs'{{( |$)}}
; TC:      {{^}}} // end of method '.cctor'{{$}}

; Straight-line at 33: AllocHGlobal(280 + 15), the first and the last of
; the 33 stores, no slot.
; SC-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; SC:       {{^}}  {{ldc\.i4(\.s)? 295( |$)}}
; SC:       {{^}}  stsfld native int '<msil>fn_f'{{( |$)}}
; SC-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; SC-NEXT:  {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; SC-NEXT:  {{^}}  add{{( |$)}}
; SC-NEXT:  {{^}}  ldsfld native int 'arr'{{( |$)}}
; SC-NEXT:  {{^}}  stind.i{{( |$)}}
; SC:       {{^}}  {{ldc\.i4(\.s)? 272( |$)}}
; SC-NEXT:  {{^}}  add{{( |$)}}
; SC-NEXT:  {{^}}  ldsfld native int '<msil>fn_f'{{( |$)}}
; SC-NEXT:  {{^}}  stind.i{{( |$)}}
; SC-NEXT:  {{^}}  ret{{( |$)}}

;--- thirtytwo.ll
; Exactly 32 relocations: arr +0 (16 bytes); tbl +16, 29 aligned 8-byte
; pointers into arr (arr + 4 * (k % 4)); mix +248 with an unaligned 8-byte
; site at +249 (arr + 12), a 4-byte site at +260 (arr + 4) and a function
; pointer at +264; 272 initialized bytes. Loop mode: 32 entries (the last
; of kind 2 through the slot at 272), TotalSize 280.
source_filename = "reloc-loop-32.c"

@arr = global [4 x i32] [i32 1, i32 2, i32 3, i32 4], align 16
@tbl = global [29 x ptr] [ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr @arr], align 16
@mix = global <{ i8, ptr, i8, i16, i32, ptr }> <{ i8 1, ptr getelementptr (i8, ptr @arr, i64 12), i8 2, i16 3, i32 trunc (i64 ptrtoint (ptr getelementptr (i8, ptr @arr, i64 4) to i64) to i32), ptr @f }>, align 8

define i32 @f() {
  ret i32 7
}

; Straight-line: AllocHGlobal(272 + 15) without a slot, the first store,
; and the last three: the unaligned site, the 4-byte site and the function
; pointer, then ret.
; S32-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; S32:       {{^}}  {{ldc\.i4(\.s)? 287( |$)}}
; S32-NEXT:  {{^}}  conv.i{{( |$)}}
; S32:       {{^}}  ldftn int32 'f'(){{( |$)}}
; S32-NEXT:  {{^}}  stsfld native int '<msil>fn_f'{{( |$)}}
; S32-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; S32-NEXT:  {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; S32-NEXT:  {{^}}  add{{( |$)}}
; S32-NEXT:  {{^}}  ldsfld native int 'arr'{{( |$)}}
; S32-NEXT:  {{^}}  stind.i{{( |$)}}
; S32:       {{^}}  {{ldc\.i4(\.s)? 249( |$)}}
; S32-NEXT:  {{^}}  add{{( |$)}}
; S32-NEXT:  {{^}}  ldsfld native int 'arr'{{( |$)}}
; S32-NEXT:  {{^}}  {{ldc\.i4(\.s)? 12( |$)}}
; S32-NEXT:  {{^}}  add{{( |$)}}
; S32-NEXT:  {{^}}  unaligned. 1{{( |$)}}
; S32-NEXT:  {{^}}  stind.i{{( |$)}}
; S32-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; S32-NEXT:  {{^}}  {{ldc\.i4(\.s)? 260( |$)}}
; S32-NEXT:  {{^}}  add{{( |$)}}
; S32-NEXT:  {{^}}  ldsfld native int 'arr'{{( |$)}}
; S32-NEXT:  {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; S32-NEXT:  {{^}}  add{{( |$)}}
; S32-NEXT:  {{^}}  conv.i4{{( |$)}}
; S32-NEXT:  {{^}}  stind.i4{{( |$)}}
; S32-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; S32-NEXT:  {{^}}  {{ldc\.i4(\.s)? 264( |$)}}
; S32-NEXT:  {{^}}  add{{( |$)}}
; S32-NEXT:  {{^}}  ldsfld native int '<msil>fn_f'{{( |$)}}
; S32-NEXT:  {{^}}  stind.i{{( |$)}}
; S32-NEXT:  {{^}}  ret{{( |$)}}
; S32-NEXT:  {{^}}} // end of method '.cctor'{{$}}

; The loop at 31: 32 entries of 16 bytes, the last three (249, 0, 12),
; (260, 1, 4) and (264, 2, 272); AllocHGlobal(280 + 15), the slot of f at
; 272 from its field, then the loop.
; L32:      {{^}}.class private explicit ansi sealed '<msil>relocs_t' extends [mscorlib]System.ValueType{{$}}
; L32:      {{^ *}}.pack 1{{( |$)}}
; L32-NEXT: {{^ *}}.size 512{{( |$)}}
; L32:      {{^}}.data cil '{{[^']+}}' = bytearray ({{$}}
; L32-NEXT: {{^}}  10 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; L32:      {{^}}  F9 00 00 00 00 00 00 00 0C 00 00 00 00 00 00 00{{( |$)}}
; L32-NEXT: {{^}}  04 01 00 00 01 00 00 00 04 00 00 00 00 00 00 00{{( |$)}}
; L32-NEXT: {{^}}  08 01 00 00 02 00 00 00 10 01 00 00 00 00 00 00 ){{( |$)}}
; L32:      {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; L32:      {{^}}  {{ldc\.i4(\.s)? 295( |$)}}
; L32-NEXT: {{^}}  conv.i{{( |$)}}
; L32:      {{^}}  ldftn int32 'f'(){{( |$)}}
; L32-NEXT: {{^}}  stsfld native int '<msil>fn_f'{{( |$)}}
; L32-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; L32-NEXT: {{^}}  {{ldc\.i4(\.s)? 272( |$)}}
; L32-NEXT: {{^}}  add{{( |$)}}
; L32-NEXT: {{^}}  ldsfld native int '<msil>fn_f'{{( |$)}}
; L32-NEXT: {{^}}  stind.i{{( |$)}}
; L32:      {{^}}  ldsflda valuetype '<msil>relocs_t' '<msil>relocs'{{( |$)}}
; L32:      {{^}}} // end of method '.cctor'{{$}}
