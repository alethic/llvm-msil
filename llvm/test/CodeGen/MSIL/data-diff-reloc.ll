; Symbol-difference relocations (ImplementationPlan.md M8.b, decision D12;
; recon asmprinter.md 7.1-7.2): the difference of two data addresses in an
; initializer (relative tables, as 'sub (ptrtoint A, ptrtoint B)', truncated
; to i32 or not, with a constant addend from a getelementptr on either side)
; is a constant of the layout, so MSILDataSegment::finalize() folds it into
; the image bytes once every global, the zero-initialized ones included, has
; its offset. Such slots never become .cctor relocations: the .cctor has no
; 'stind' at all here. Both triples, -O2 and -O0, and the module assembles.
;
; Differences that are not two data addresses are clean errors that name the
; global (exit code 1, no crash): a function address minus a data address,
; and the sum of two addresses ('unsupported constant expression').

; DEFINE: %{check} = FileCheck %s --implicit-check-not=".field " --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not=stind --implicit-check-not=ldftn
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/diffs.ll \
; RUN:   -o %t/diffs.lnx.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/diffs.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/diffs.ll \
; RUN:   -o %t/diffs.win.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/diffs.win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/diffs.ll \
; RUN:   -o %t/diffs.win.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/diffs.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/diffs.dll %t/diffs.lnx.il %}

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/fn-diff.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=FNDIFF
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/fn-diff.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=FNDIFF
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/sum.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=SUM
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/sum.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=SUM

;--- diffs.ll
; tgt0 +0, tgt1 +4, rel +8 (three i32: tgt0 - rel = -8, tgt1 - rel = -4,
; tgt1 - &rel[2] = -12), d64 +24 (tgt1 - tgt0 = 4), d64add +32
; ((char *)&tgt1 + 2 - tgt0 = 6), dzero +40 (zz - tgt0 = 52), dneg +48 (i32
; tgt0 - zz = -52); zz +52 is zero-initialized. The zero tail starts at
; Base + 52, which is only 4-aligned, so its initblk has the prefix
; 'unaligned. 4' (plan section 12, 'M8.c amended (alignment prefixes in
; <Module>::.cctor)').
source_filename = "data-diff-reloc.c"

@tgt0 = global i32 10, align 4
@tgt1 = global i32 11, align 4
@rel = constant [3 x i32] [
  i32 trunc (i64 sub (i64 ptrtoint (ptr @tgt0 to i64), i64 ptrtoint (ptr @rel to i64)) to i32),
  i32 trunc (i64 sub (i64 ptrtoint (ptr @tgt1 to i64), i64 ptrtoint (ptr @rel to i64)) to i32),
  i32 trunc (i64 sub (i64 ptrtoint (ptr @tgt1 to i64), i64 ptrtoint (ptr getelementptr inbounds ([3 x i32], ptr @rel, i64 0, i64 2) to i64)) to i32)
], align 4
@d64 = global i64 sub (i64 ptrtoint (ptr @tgt1 to i64), i64 ptrtoint (ptr @tgt0 to i64)), align 8
@d64add = global i64 sub (i64 ptrtoint (ptr getelementptr (i8, ptr @tgt1, i64 2) to i64), i64 ptrtoint (ptr @tgt0 to i64)), align 8
@dzero = global i64 sub (i64 ptrtoint (ptr @zz to i64), i64 ptrtoint (ptr @tgt0 to i64)), align 8
@dneg = global i32 trunc (i64 sub (i64 ptrtoint (ptr @tgt0 to i64), i64 ptrtoint (ptr @zz to i64)) to i32), align 4
@zz = global [4 x i8] zeroinitializer, align 4

; CHECK-LABEL: {{^}}.method public static int32 'rel1'() cil managed{{$}}
; CHECK:       {{^}}  ldsfld native int 'rel'{{$}}
; CHECK:       {{^}}  ldind.i4{{$}}
; CHECK:       {{^}}} // end of method 'rel1'{{$}}
define i32 @rel1() {
  %v = load i32, ptr getelementptr inbounds ([3 x i32], ptr @rel, i64 0, i64 1), align 4
  ret i32 %v
}

; CHECK-NOT:     {{^}}.method
; CHECK:        {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; CHECK:        {{^ *}}.pack 1{{( |$)}}
; CHECK:        {{^ *}}.size 52{{( |$)}}
; CHECK:        {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; CHECK-NEXT:   {{^}}  0A 00 00 00 0B 00 00 00 F8 FF FF FF FC FF FF FF{{( |$)}}
; CHECK-NEXT:   {{^}}  F4 FF FF FF 00 00 00 00 04 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  06 00 00 00 00 00 00 00 34 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  CC FF FF FF ){{( |$)}}
; CHECK:        {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; CHECK:        {{^}}.field private static initonly native int '<msil>data'{{$}}
; CHECK:        {{^}}.field public static initonly native int 'tgt0'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'tgt1'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'rel'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'd64'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'd64add'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'dzero'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'dneg'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'zz'{{( |$)}}
; CHECK:        {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK-NEXT:   {{^\{$}}
; CHECK-NEXT:   {{^}}  .maxstack {{[3-9]}}{{$}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 71( |$)}}
; CHECK-NEXT:   {{^}}  conv.i{{( |$)}}
; CHECK-NEXT:   {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 15( |$)}}
; CHECK-NEXT:   {{^}}  conv.i{{( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? -16( |$)}}
; CHECK-NEXT:   {{^}}  conv.i{{( |$)}}
; CHECK-NEXT:   {{^}}  and{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 52( |$)}}
; CHECK-NEXT:   {{^}}  cpblk{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 52( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )0( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; CHECK-NEXT:   {{^}}  unaligned. 4{{( |$)}}
; CHECK-NEXT:   {{^}}  initblk{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'tgt0'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'tgt1'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'rel'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 24( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'd64'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'd64add'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 40( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'dzero'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'dneg'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 52( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'zz'{{( |$)}}
; CHECK-NEXT:   {{^}}  ret{{( |$)}}
; CHECK-NEXT:   {{^}}} // end of method '.cctor'{{$}}
; CHECK-NOT:     {{^}}.method

;--- fn-diff.ll
; long d = (char *)fn - (char *)&tgt;
source_filename = "fn-diff.c"

; FNDIFF: {{^}}error: {{.*}}fdiff

@tgt = global i32 1, align 4
@fdiff = global i64 sub (i64 ptrtoint (ptr @fn to i64), i64 ptrtoint (ptr @tgt to i64)), align 8

define void @fn() {
  ret void
}

;--- sum.ll
; long s = (long)&tgt + (long)&tgt2;
source_filename = "sum.c"

; SUM: {{^}}error: {{.*(unsupported constant expression.*sum2|sum2.*unsupported constant expression)}}

@tgt = global i32 1, align 4
@tgt2 = global i32 2, align 4
@sum2 = global i64 add (i64 ptrtoint (ptr @tgt to i64), i64 ptrtoint (ptr @tgt2 to i64)), align 8
