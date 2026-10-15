; Relocations in initializers (ImplementationPlan.md M8.b and M8.c, decision
; D12; recon asmprinter.md 7.1 and 7.3): an initializer that holds the address
; of a global keeps zeros in its image bytes, and the .cctor stores the address
; after every address field is set, one relocation per pointer slot, in site
; order: 'ldsfld native int '<msil>data'; ldc.i4 <site>; add', then the value,
; then 'stind.i'. The value of a relocation to data is 'ldsfld native int
; '<target>'' plus 'ldc.i4 <addend>; add' when the addend is not zero
; (negative addends and one past the end included); the value of a relocation
; to a function is 'ldftn <signature>' of the function, internal functions
; included. A relocation of 4 bytes (an address truncated to i32) converts the
; value with 'conv.i4' and stores it with 'stind.i4'. Relocations sit inside
; arrays and structs (here after an i32 and its padding), may point to the
; global that holds them, and may point to a zero-initialized global.
; Functions used as values in code are 'ldftn <signature>' (FUNC_ADDR, M8.a).
; Both triples, -O2 and -O0, and the module assembles. Every relocation site
; of relocs.ll is naturally aligned (8 for stind.i, 4 for stind.i4), and so is
; its zero tail at 144, so its .cctor has no 'unaligned.' prefix at all.
;
; Alignment prefixes (plan section 12, 'M8.c amended (alignment prefixes in
; <Module>::.cctor)'; D24): the site of a relocation is Base + site with Base
; aligned to MaxAlign, so a stind.i whose site is not 8-aligned, and a
; stind.i4 whose site is not 4-aligned, gets the line 'unaligned. N'
; immediately before it (for stind.i4 after the conv.i4), N =
; min(commonAlignment(MaxAlign, site), 4); likewise the initblk of a zero
; tail that starts at a site that is not 8-aligned. unaligned.ll has packed
; structs with 8-byte relocations at an odd site (N = 1), at 2 and 4 modulo 8
; (N = 2 and 4) and at an aligned control site, 4-byte relocations at 2
; modulo 4 and at an odd site (N = 2 and 1) and at an aligned control site,
; data and function targets, and a zero tail at 2 modulo 4 (N = 2). The cpblk
; of the image never has a prefix.
;
; A function address plus an offset cannot be a relocation ('function address
; + offset', M8.b): a clean error that names the global, exit code 1, no crash.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=".field " --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not=int64 --implicit-check-not="&("
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/relocs.ll \
; RUN:   -o %t/lnx.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/relocs.ll \
; RUN:   -o %t/win.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/relocs.ll \
; RUN:   -o %t/lnx.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/relocs.ll \
; RUN:   -o %t/win.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/lnx.dll %t/lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/win.o0.dll %t/win.o0.il %}

; DEFINE: %{ucheck} = FileCheck %s --check-prefix=UNALIGNED --implicit-check-not=".field " \
; DEFINE:   --implicit-check-not=pinvokeimpl --implicit-check-not=int64 --implicit-check-not="&("
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/unaligned.ll \
; RUN:   -o %t/unaligned.lnx.il 2>&1 | count 0
; RUN: %{ucheck} --input-file=%t/unaligned.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/unaligned.ll \
; RUN:   -o %t/unaligned.win.il 2>&1 | count 0
; RUN: %{ucheck} --input-file=%t/unaligned.win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/unaligned.ll \
; RUN:   -o %t/unaligned.win.o0.il 2>&1 | count 0
; RUN: %{ucheck} --input-file=%t/unaligned.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/unaligned.lnx.dll %t/unaligned.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/unaligned.win.dll %t/unaligned.win.il %}

; DEFINE: %{diag} = FileCheck %s --check-prefix=FNOFF --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/fn-offset.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/fn-offset.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/fn-offset.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}

;--- relocs.ll
source_filename = "global-relocs.c"

; Layout: target +0, p0 +16, p8 +24, pneg +32, pend +40, fp +48, ifp +56,
; tbl +64 (three slots), mixed +88 (its pointer at 96), s4 +112, f4 +120,
; s4add +124, self +128, pz +136; zero +144 is the zero tail. 144 initialized
; bytes, TotalSize 148.
@target = global [4 x i32] [i32 1, i32 2, i32 3, i32 4], align 16
@p0 = global ptr @target, align 8
@p8 = global ptr getelementptr (i8, ptr @target, i64 8), align 8
@pneg = global ptr getelementptr (i8, ptr @target, i64 -4), align 8
@pend = global ptr getelementptr inbounds ([4 x i32], ptr @target, i64 1), align 8
@fp = global ptr @callee, align 8
@ifp = internal global ptr @internal_fn, align 8
@tbl = global [3 x ptr] [ptr @callee, ptr @target, ptr null], align 16
@mixed = global { i32, ptr, i16 } { i32 9, ptr @p0, i16 5 }, align 8
@s4 = global { i32, i32 } { i32 trunc (i64 ptrtoint (ptr @target to i64) to i32), i32 7 }, align 4
@f4 = global i32 trunc (i64 ptrtoint (ptr @callee to i64) to i32), align 4
@s4add = global i32 trunc (i64 ptrtoint (ptr getelementptr (i8, ptr @target, i64 12) to i64) to i32), align 4
@self = global ptr @self, align 8
@pz = global ptr @zero, align 8
@zero = global i32 0, align 4

; CHECK-LABEL: {{^}}.method public static int32 'callee'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'callee'{{$}}
define i32 @callee(i32 %x) {
  ret i32 %x
}

; CHECK-LABEL: {{^}}.method assembly static void 'internal_fn'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'internal_fn'{{$}}
define internal void @internal_fn() {
  ret void
}

; The address of a function in code.
; CHECK-LABEL: {{^}}.method public static native int 'get_fp'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldftn int32 'callee'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'get_fp'{{$}}
define ptr @get_fp() {
  ret ptr @callee
}

; CHECK-LABEL: {{^}}.method public static native int 'get_ifp'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldftn void 'internal_fn'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'get_ifp'{{$}}
define ptr @get_ifp() {
  ret ptr @internal_fn
}

; A pointer read from a relocated slot.
; CHECK-LABEL: {{^}}.method public static int32 'deref_p8'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'p8'{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'deref_p8'{{$}}
define i32 @deref_p8() {
  %p = load ptr, ptr @p8, align 8
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

; CHECK-NOT:   {{^}}.method {{(public|assembly) static}}
; CHECK:        {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; CHECK:        {{^ *}}.pack 1{{( |$)}}
; CHECK:        {{^ *}}.size 144{{( |$)}}
; CHECK:        {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; CHECK-NEXT:   {{^}}  01 00 00 00 02 00 00 00 03 00 00 00 04 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 00 00 00 00 09 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 00 00 00 00 05 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 07 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ){{( |$)}}
; CHECK:        {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; CHECK:        {{^}}.field private static initonly native int '<msil>data'{{$}}
; CHECK:        {{^}}.field public static initonly native int 'target'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'p0'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'p8'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'pneg'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'pend'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'fp'{{( |$)}}
; CHECK:        {{^}}.field assembly static initonly native int 'ifp'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'tbl'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'mixed'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 's4'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'f4'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 's4add'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'self'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'pz'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'zero'{{( |$)}}
; CHECK:        {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK-NEXT:   {{^\{$}}
; CHECK-NEXT:   {{^}}  .maxstack {{[3-9]}}{{$}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 163( |$)}}
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
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 144( |$)}}
; CHECK-NEXT:   {{^}}  cpblk{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 144( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )0( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; CHECK-NEXT:   {{^}}  initblk{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'target'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'p0'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 24( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'p8'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'pneg'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 40( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'pend'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'fp'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 56( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'ifp'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 64( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'tbl'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 88( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'mixed'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 112( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 's4'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 120( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'f4'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 124( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 's4add'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 128( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'self'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 136( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'pz'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 144( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'zero'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int 'target'{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 24( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int 'target'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int 'target'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? -4( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 40( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int 'target'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldftn int32 'callee'(int32){{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 56( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldftn void 'internal_fn'(){{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 64( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldftn int32 'callee'(int32){{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 72( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int 'target'{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 96( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int 'p0'{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 112( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int 'target'{{( |$)}}
; CHECK-NEXT:   {{^}}  conv.i4{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i4{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 120( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldftn int32 'callee'(int32){{( |$)}}
; CHECK-NEXT:   {{^}}  conv.i4{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i4{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 124( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int 'target'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 12( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  conv.i4{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i4{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 128( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int 'self'{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 136( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int 'zero'{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ret{{( |$)}}
; CHECK-NEXT:   {{^}}} // end of method '.cctor'{{$}}
; CHECK-NOT:   {{^}}.method

;--- unaligned.ll
; Layout (MaxAlign 16): x +0; pk +16, a packed struct whose pointers are at
; +17 (x), +26 (x + 2), +36 (fn) and +48 (x); q4 +56, a packed struct whose
; 32-bit addresses are at +58 (x + 4), +63 (fn) and +68 (x); h +72. 74
; initialized bytes; zz +74 is the zero tail, so TotalSize is 76 and
; AllocHGlobal gets 76 + 15 = 91.
source_filename = "global-relocs-unaligned.c"

@x = global i32 11, align 4
@pk = global <{ i8, ptr, i8, ptr, i16, ptr, i32, ptr }> <{ i8 97, ptr @x, i8 98, ptr getelementptr (i8, ptr @x, i64 2), i16 7, ptr @fn, i32 9, ptr @x }>, align 16
@q4 = global <{ i16, i32, i8, i32, i8, i32 }> <{ i16 1, i32 trunc (i64 ptrtoint (ptr getelementptr (i8, ptr @x, i64 4) to i64) to i32), i8 2, i32 trunc (i64 ptrtoint (ptr @fn to i64) to i32), i8 3, i32 trunc (i64 ptrtoint (ptr @x to i64) to i32) }>, align 8
@h = global i16 5, align 2
@zz = global i16 0, align 2

; UNALIGNED-LABEL: {{^}}.method public static int32 'fn'(int32) cil managed{{$}}
; UNALIGNED:       {{^}}} // end of method 'fn'{{$}}
define i32 @fn(i32 %v) {
  ret i32 %v
}

; UNALIGNED-NOT:    {{^}}.method
; UNALIGNED:        {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; UNALIGNED:        {{^ *}}.pack 1{{( |$)}}
; UNALIGNED:        {{^ *}}.size 74{{( |$)}}
; UNALIGNED:        {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; UNALIGNED-NEXT:   {{^}}  0B 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  61 00 00 00 00 00 00 00 00 62 00 00 00 00 00 00{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  00 00 07 00 00 00 00 00 00 00 00 00 09 00 00 00{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  00 00 00 00 00 00 00 00 01 00 00 00 00 00 02 00{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  00 00 00 03 00 00 00 00 05 00 ){{( |$)}}
; UNALIGNED:        {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; UNALIGNED:        {{^}}.field private static initonly native int '<msil>data'{{$}}
; UNALIGNED:        {{^}}.field public static initonly native int 'x'{{( |$)}}
; UNALIGNED:        {{^}}.field public static initonly native int 'pk'{{( |$)}}
; UNALIGNED:        {{^}}.field public static initonly native int 'q4'{{( |$)}}
; UNALIGNED:        {{^}}.field public static initonly native int 'h'{{( |$)}}
; UNALIGNED:        {{^}}.field public static initonly native int 'zz'{{( |$)}}
; UNALIGNED:        {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; UNALIGNED-NEXT:   {{^\{$}}
; UNALIGNED-NEXT:   {{^}}  .maxstack {{[3-9]}}{{$}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 91( |$)}}
; UNALIGNED-NEXT:   {{^}}  conv.i{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 15( |$)}}
; UNALIGNED-NEXT:   {{^}}  conv.i{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? -16( |$)}}
; UNALIGNED-NEXT:   {{^}}  conv.i{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  and{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stsfld native int '<msil>data'{{( |$)}}
;   the image: no prefix
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 74( |$)}}
; UNALIGNED-NEXT:   {{^}}  cpblk{{( |$)}}
;   the zero tail at 74 (2 modulo 4)
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 74( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )0( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )2( |$)}}
; UNALIGNED-NEXT:   {{^}}  unaligned. 2{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  initblk{{( |$)}}
;   the address fields: no prefix
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stsfld native int 'x'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stsfld native int 'pk'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 56( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stsfld native int 'q4'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 72( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stsfld native int 'h'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 74( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stsfld native int 'zz'{{( |$)}}
;   reloc @17 (odd): pk.p = x
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 17( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ldsfld native int 'x'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  unaligned. 1{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stind.i{{( |$)}}
;   reloc @26 (2 modulo 8): x + 2
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 26( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ldsfld native int 'x'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )2( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  unaligned. 2{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stind.i{{( |$)}}
;   reloc @36 (4 modulo 8): fn
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 36( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ldftn int32 'fn'(int32){{( |$)}}
; UNALIGNED-NEXT:   {{^}}  unaligned. 4{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stind.i{{( |$)}}
;   reloc @48 (8-aligned): no prefix
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ldsfld native int 'x'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stind.i{{( |$)}}
;   4-byte reloc @58 (2 modulo 4): x + 4
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 58( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ldsfld native int 'x'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  conv.i4{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  unaligned. 2{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stind.i4{{( |$)}}
;   4-byte reloc @63 (odd): fn
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 63( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ldftn int32 'fn'(int32){{( |$)}}
; UNALIGNED-NEXT:   {{^}}  conv.i4{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  unaligned. 1{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stind.i4{{( |$)}}
;   4-byte reloc @68 (4-aligned): no prefix
; UNALIGNED-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  {{ldc\.i4(\.s)? 68( |$)}}
; UNALIGNED-NEXT:   {{^}}  add{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ldsfld native int 'x'{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  conv.i4{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  stind.i4{{( |$)}}
; UNALIGNED-NEXT:   {{^}}  ret{{( |$)}}
; UNALIGNED-NEXT:   {{^}}} // end of method '.cctor'{{$}}
; UNALIGNED-NOT:    {{^}}.method

;--- fn-offset.ll
; void *bad = (char *)callee + 4;
source_filename = "fn-offset.c"

; FNOFF: {{^}}error: {{.*(function address \+ offset.*bad|bad.*function address \+ offset)}}

@bad = global ptr getelementptr (i8, ptr @callee, i64 4), align 8

define i32 @callee(i32 %x) {
  ret i32 %x
}
