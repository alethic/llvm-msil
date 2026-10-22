; COMDAT, linkonce, weak and common definitions are ordinary definitions in
; the single-module world of Phase 1 (ImplementationPlan.md decision D26, M8.c
; 'every definition (any linkage, comdat ignored)'). MSVC string literals are
; linkonce_odr constants in a COMDAT named after the literal
; ('??_C@_05CJBACGMB@hello?$AA@' for "hello"): they get an ordinary public
; address field, single-quoted (D11: '?', '@' and '$' need no escape), and code
; that passes one to puts loads that field. linkonce, weak, weak_odr and common
; globals and a linkonce_odr function in a COMDAT are public; an internal
; global in a function's COMDAT is assembly; a common global is
; zero-initialized. Since M12 MSILPrepareIR drops a linkonce or linkonce_odr
; function that nothing uses (discardable-defs.ll), so 'inl' has a caller,
; 'call_inl'. No linkage or COMDAT text reaches the output, nothing is
; reported, both triples (the COMDATs are written as clang writes them for
; windows-msvc; the IR is the same for the Linux triple), -O2 and -O0, and the
; output assembles.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=".field " --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not=comdat --implicit-check-not=linkonce --implicit-check-not=weak_odr \
; DEFINE:   --implicit-check-not=selectany --implicit-check-not=".weak" --implicit-check-not=".section"
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.win.il 2>&1 \
; RUN:   | count 0
; RUN: %{check} --check-prefixes=CHECK,WIN --input-file=%t.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.lnx.il 2>&1 \
; RUN:   | count 0
; RUN: %{check} --check-prefixes=CHECK,LNX --input-file=%t.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,WIN --input-file=%t.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.dll %t.win.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.dll %t.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

source_filename = "msvc-literals.c"

$"??_C@_05CJBACGMB@hello?$AA@" = comdat any
$inl = comdat any
$wodr = comdat any

; The string +0, weakg +8, lo +12, wodr +16, grouped +20; common +24 is
; zero-initialized.
@"??_C@_05CJBACGMB@hello?$AA@" = linkonce_odr dso_local unnamed_addr constant [6 x i8] c"hello\00", comdat, align 1
@weakg = weak global i32 5, align 4
@lo = linkonce global i32 3, align 4
@wodr = weak_odr global i32 4, comdat, align 4
@grouped = internal global i32 6, comdat($inl), align 4
@common = common global i32 0, align 4

; puts("hello") with the MSVC literal.
; CHECK-LABEL: {{^}}.method public static int32 'use_str'() cil managed{{$}}
; CHECK:       {{^}}  ldsfld native int '??_C@_05CJBACGMB@hello?$AA@'{{$}}
; CHECK-NEXT:  {{^}}  call int32 'puts'(native int){{$}}
; CHECK:       {{^}}} // end of method 'use_str'{{$}}
define i32 @use_str() {
  %r = call i32 @puts(ptr @"??_C@_05CJBACGMB@hello?$AA@")
  ret i32 %r
}

; CHECK-LABEL: {{^}}.method public static int32 'inl'(int32) cil managed{{$}}
; CHECK:       {{^}}  ldsfld native int 'grouped'{{$}}
; CHECK:       {{^}}} // end of method 'inl'{{$}}
define linkonce_odr i32 @inl(i32 %x) comdat {
  %v = load i32, ptr @grouped, align 4
  %r = add i32 %x, %v
  ret i32 %r
}

; CHECK-LABEL: {{^}}.method public static int32 'sum_linkages'() cil managed{{$}}
; CHECK-DAG:   {{^}}  ldsfld native int 'weakg'{{$}}
; CHECK-DAG:   {{^}}  ldsfld native int 'lo'{{$}}
; CHECK-DAG:   {{^}}  ldsfld native int 'wodr'{{$}}
; CHECK-DAG:   {{^}}  ldsfld native int 'common'{{$}}
; CHECK:       {{^}}} // end of method 'sum_linkages'{{$}}
define i32 @sum_linkages() {
  %a = load i32, ptr @weakg, align 4
  %b = load i32, ptr @lo, align 4
  %c = load i32, ptr @wodr, align 4
  %d = load i32, ptr @common, align 4
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %s3 = add i32 %s2, %d
  ret i32 %s3
}

; CHECK-LABEL: {{^}}.method public static int32 'call_inl'(int32) cil managed{{$}}
; CHECK:       {{^}}  call int32 'inl'(int32){{$}}
; CHECK:       {{^}}} // end of method 'call_inl'{{$}}
define i32 @call_inl(i32 %x) {
  %r = call i32 @inl(i32 %x)
  ret i32 %r
}

declare i32 @puts(ptr)

; CHECK-NOT:   {{^}}.method {{(public|assembly) static}}
; LNX:         {{^}}.method public hidebysig static pinvokeimpl("libc" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; WIN:         {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; CHECK:        {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; CHECK:        {{^ *}}.pack 1{{( |$)}}
; CHECK:        {{^ *}}.size 24{{( |$)}}
; CHECK:        {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; CHECK-NEXT:   {{^}}  68 65 6C 6C 6F 00 00 00 05 00 00 00 03 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  04 00 00 00 06 00 00 00 ){{( |$)}}
; CHECK:        {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; CHECK:        {{^}}.field private static initonly native int '<msil>data'{{$}}
; CHECK:        {{^}}.field public static initonly native int '??_C@_05CJBACGMB@hello?$AA@'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'weakg'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'lo'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'wodr'{{( |$)}}
; CHECK:        {{^}}.field assembly static initonly native int 'grouped'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'common'{{( |$)}}
; CHECK:        {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK-NEXT:   {{^\{$}}
; CHECK-NEXT:   {{^}}  .maxstack {{[3-9]}}{{$}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 43( |$)}}
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
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 24( |$)}}
; CHECK-NEXT:   {{^}}  cpblk{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 24( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )0( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; CHECK-NEXT:   {{^}}  initblk{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '??_C@_05CJBACGMB@hello?$AA@'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'weakg'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 12( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'lo'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'wodr'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 20( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'grouped'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 24( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'common'{{( |$)}}
; CHECK-NEXT:   {{^}}  ret{{( |$)}}
; CHECK-NEXT:   {{^}}} // end of method '.cctor'{{$}}
; CHECK-NOT:   {{^}}.method
