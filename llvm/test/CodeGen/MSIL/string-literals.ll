; String literals and other private constants (ImplementationPlan.md M8,
; decisions D11 and D12, plan 5.1 and 5.5): clang's private unnamed_addr
; constants ('.str', '.str.1', ...) are ordinary globals of the data segment,
; with 'assembly' address fields named after the IR name (D11: never the
; Mangler's '.L' name), and code reaches them through those fields, also
; with a constant offset. Their bytes are copied verbatim, escapes, embedded
; and trailing NULs, UTF-8 and bytes above 0x7F included, and wide strings
; (wchar_t is 16 bits on Windows, 32 bits elsewhere) are little-endian
; elements. Pointers to them in data are relocations (an addend points into
; a string). Passing one to puts makes the puts P/Invoke ("libc" on Linux,
; "ucrtbase" on windows-msvc). A local array initialized from a constant
; (clang's private @__const global plus llvm.memcpy, the case that plan 5.5
; keeps out of the tests until M8) copies from the global's field with cpblk
; (decision D15). Both triples, -O2 and -O0, and every module assembles.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=".field " --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not=".L.str"
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/strings.ll \
; RUN:   -o %t/strings.lnx.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O2,LNX --input-file=%t/strings.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/strings.ll \
; RUN:   -o %t/strings.win.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O2,WIN --input-file=%t/strings.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/strings.ll \
; RUN:   -o %t/strings.lnx.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O0,LNX --input-file=%t/strings.lnx.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/strings.lnx.dll %t/strings.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/strings.win.dll %t/strings.win.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/local-array.ll \
; RUN:   -o %t/local-array.lnx.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=LOCAL,LOCAL-O2 --input-file=%t/local-array.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/local-array.ll \
; RUN:   -o %t/local-array.win.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=LOCAL,LOCAL-O2 --input-file=%t/local-array.win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/local-array.ll \
; RUN:   -o %t/local-array.win.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=LOCAL,LOCAL-O0 --input-file=%t/local-array.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/local-array.dll %t/local-array.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/local-array.o0.dll %t/local-array.win.o0.il %}

;--- strings.ll
; '.str' +0 "hello", '.str.1' +6 "a\nb\tc\"d\\e", '.str.2' +16 "caf\u00e9!"
; in UTF-8, '.str.3' +23 (bytes 00 FF 80 00), '.wstr' +28 (L"hi", 16-bit),
; '.ustr' +36 (L"hi", 32-bit), msg +48 (-> '.str'), msgs +64 (-> '.str.1',
; -> '.str.2' + 3). 80 bytes, no zero tail.
source_filename = "string-literals.c"

@.str = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@.str.1 = private unnamed_addr constant [10 x i8] c"a\0Ab\09c\22d\5Ce\00", align 1
@.str.2 = private unnamed_addr constant [7 x i8] c"caf\C3\A9!\00", align 1
@.str.3 = private unnamed_addr constant [4 x i8] c"\00\FF\80\00", align 1
@.wstr = private unnamed_addr constant [3 x i16] [i16 104, i16 105, i16 0], align 2
@.ustr = private unnamed_addr constant [3 x i32] [i32 104, i32 105, i32 0], align 4
@msg = global ptr @.str, align 8
@msgs = global [2 x ptr] [ptr @.str.1, ptr getelementptr inbounds ([7 x i8], ptr @.str.2, i64 0, i64 3)], align 16

; puts("hello")
; CHECK-LABEL: {{^}}.method public static int32 'say_hello'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int '.str'{{$}}
; O2-NEXT:     {{^}}  call int32 'puts'(native int){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int '.str'{{$}}
; O0:          {{^}}  call int32 'puts'(native int){{$}}
; CHECK:       {{^}}} // end of method 'say_hello'{{$}}
define i32 @say_hello() {
  %r = call i32 @puts(ptr @.str)
  ret i32 %r
}

; "hello" + 1
; CHECK-LABEL: {{^}}.method public static native int 'second_char'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int '.str'{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4(\.| |\.s )1$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int '.str'{{$}}
; CHECK:       {{^}}} // end of method 'second_char'{{$}}
define ptr @second_char() {
  ret ptr getelementptr inbounds ([6 x i8], ptr @.str, i64 0, i64 1)
}

; The first byte of the two-byte UTF-8 sequence.
; CHECK-LABEL: {{^}}.method public static int32 'utf8_lead'() cil managed{{$}}
; CHECK:       {{^}}  ldsfld native int '.str.2'{{$}}
; CHECK:       {{^}}  ldind.u1{{$}}
; CHECK:       {{^}}} // end of method 'utf8_lead'{{$}}
define i32 @utf8_lead() {
  %v = load i8, ptr getelementptr inbounds ([7 x i8], ptr @.str.2, i64 0, i64 3), align 1
  %r = zext i8 %v to i32
  ret i32 %r
}

declare i32 @puts(ptr)

; CHECK-NOT:   {{^}}.method {{(public|assembly) static}}
; LNX:         {{^}}.method public hidebysig static pinvokeimpl("libc" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; WIN:         {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; CHECK:        {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; CHECK:        {{^ *}}.pack 1{{( |$)}}
; CHECK:        {{^ *}}.size 80{{( |$)}}
; CHECK:        {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; CHECK-NEXT:   {{^}}  68 65 6C 6C 6F 00 61 0A 62 09 63 22 64 5C 65 00{{( |$)}}
; CHECK-NEXT:   {{^}}  63 61 66 C3 A9 21 00 00 FF 80 00 00 68 00 69 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 68 00 00 00 69 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ){{( |$)}}
; CHECK:        {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; CHECK:        {{^}}.field private static initonly native int '<msil>data'{{$}}
; CHECK:        {{^}}.field assembly static initonly native int '.str'{{( |$)}}
; CHECK:        {{^}}.field assembly static initonly native int '.str.1'{{( |$)}}
; CHECK:        {{^}}.field assembly static initonly native int '.str.2'{{( |$)}}
; CHECK:        {{^}}.field assembly static initonly native int '.str.3'{{( |$)}}
; CHECK:        {{^}}.field assembly static initonly native int '.wstr'{{( |$)}}
; CHECK:        {{^}}.field assembly static initonly native int '.ustr'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'msg'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'msgs'{{( |$)}}
; CHECK:        {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK-NEXT:   {{^\{$}}
; CHECK-NEXT:   {{^}}  .maxstack {{[3-9]}}{{$}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 95( |$)}}
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
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 80( |$)}}
; CHECK-NEXT:   {{^}}  cpblk{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '.str'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )6( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '.str.1'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '.str.2'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 23( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '.str.3'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 28( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '.wstr'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 36( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '.ustr'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'msg'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 64( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'msgs'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '.str'{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 64( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '.str.1'{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 72( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '.str.2'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )3( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ret{{( |$)}}
; CHECK-NEXT:   {{^}}} // end of method '.cctor'{{$}}
; CHECK-NOT:   {{^}}.method

;--- local-array.ll
; int local_array(long i) { int arr[256] = {1, 2, 3, 4}; return arr[i]; }
; The 1024 bytes of the constant are the whole image.
source_filename = "local-array.c"

@__const.local_array.arr = private unnamed_addr constant <{ [4 x i32], [252 x i32] }> <{ [4 x i32] [i32 1, i32 2, i32 3, i32 4], [252 x i32] zeroinitializer }>, align 16

; LOCAL-LABEL:  {{^}}.method public static int32 'local_array'(native int) cil managed{{$}}
; LOCAL:        {{^}}  localloc{{$}}
; LOCAL-O2:     {{^}}  ldsfld native int '__const.local_array.arr'{{$}}
; LOCAL-O2-NEXT: {{^}}  ldc.i4 1024{{$}}
; LOCAL-O2-NEXT: {{^}}  cpblk{{$}}
; LOCAL-O0:     {{^}}  ldsfld native int '__const.local_array.arr'{{$}}
; LOCAL-O0:     {{^}}  cpblk{{$}}
; LOCAL:        {{^}}  ldind.i4{{$}}
; LOCAL:        {{^}}} // end of method 'local_array'{{$}}
define i32 @local_array(i64 %i) {
  %arr = alloca [256 x i32], align 16
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %arr, ptr align 16 @__const.local_array.arr, i64 1024, i1 false)
  %p = getelementptr inbounds [256 x i32], ptr %arr, i64 0, i64 %i
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)

; LOCAL-NOT:    {{^}}.method
; LOCAL:        {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; LOCAL:        {{^ *}}.pack 1{{( |$)}}
; LOCAL:        {{^ *}}.size 1024{{( |$)}}
; LOCAL:        {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; LOCAL-NEXT:   {{^}}  01 00 00 00 02 00 00 00 03 00 00 00 04 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; LOCAL-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ){{( |$)}}
; LOCAL:        {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; LOCAL:        {{^}}.field private static initonly native int '<msil>data'{{$}}
; LOCAL:        {{^}}.field assembly static initonly native int '__const.local_array.arr'{{( |$)}}
; LOCAL:        {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; LOCAL-NEXT:   {{^\{$}}
; LOCAL-NEXT:   {{^}}  .maxstack {{[3-9]}}{{$}}
; LOCAL-NEXT:   {{^}}  {{ldc\.i4(\.s)? 1039( |$)}}
; LOCAL-NEXT:   {{^}}  conv.i{{( |$)}}
; LOCAL-NEXT:   {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; LOCAL-NEXT:   {{^}}  {{ldc\.i4(\.s)? 15( |$)}}
; LOCAL-NEXT:   {{^}}  conv.i{{( |$)}}
; LOCAL-NEXT:   {{^}}  add{{( |$)}}
; LOCAL-NEXT:   {{^}}  {{ldc\.i4(\.s)? -16( |$)}}
; LOCAL-NEXT:   {{^}}  conv.i{{( |$)}}
; LOCAL-NEXT:   {{^}}  and{{( |$)}}
; LOCAL-NEXT:   {{^}}  stsfld native int '<msil>data'{{( |$)}}
; LOCAL-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; LOCAL-NEXT:   {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; LOCAL-NEXT:   {{^}}  {{ldc\.i4(\.s)? 1024( |$)}}
; LOCAL-NEXT:   {{^}}  cpblk{{( |$)}}
; LOCAL-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; LOCAL-NEXT:   {{^}}  stsfld native int '__const.local_array.arr'{{( |$)}}
; LOCAL-NEXT:   {{^}}  ret{{( |$)}}
; LOCAL-NEXT:   {{^}}} // end of method '.cctor'{{$}}
; LOCAL-NOT:    {{^}}.method
