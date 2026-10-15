; P/Invoke targets referenced only by address (ImplementationPlan.md decision
; D14 'referenced from code or data', M8.c; recon asmprinter.md 7.3 'A
; relocation to an undefined function ldftns its P/Invoke stub'; plan section
; 12, 'Phase-1 function addresses'): an undefined import that is never called
; but whose address is stored in an initializer, or taken in code, still gets
; its P/Invoke declaration and an address field '<msil>fn_<name>' (after the
; data-segment block and its empty line, one per function in module order,
; then an empty line). The .cctor sets each field once with 'ldftn
; <signature>' of that declaration, after the stores of the global address
; fields and before the relocations; a .cctor relocation for data, and
; FUNC_ADDR in code, load the field ('ldftn' appears nowhere else). An import
; with "msil-import-name" is referenced under its IR name, its field too; only
; the declaration names the entry point. Exactly one declaration per import (a
; FileCheck run counts them), nothing is reported, both triples (the libraries
; of D14), -O2 and -O0, and the output assembles.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=".field " --implicit-check-not=int64 \
; DEFINE:   --implicit-check-not=ldftn
; DEFINE: %{count} = FileCheck %s --check-prefix=NPINV
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.lnx.il 2>&1 \
; RUN:   | count 0
; RUN: %{check} -DCLIB=libc -DMLIB=libm.so.6 --input-file=%t.lnx.il
; RUN: %{count} --input-file=%t.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.win.il 2>&1 \
; RUN:   | count 0
; RUN: %{check} -DCLIB=ucrtbase -DMLIB=ucrtbase --input-file=%t.win.il
; RUN: %{count} --input-file=%t.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o0.il 2>&1 | count 0
; RUN: %{check} -DCLIB=libc -DMLIB=libm.so.6 --input-file=%t.lnx.o0.il
; RUN: %{count} --input-file=%t.lnx.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.dll %t.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.dll %t.win.il %}

source_filename = "pinvoke-from-data.c"

declare i32 @puts(ptr)
declare double @sqrt(double)
declare i32 @putchar(i32)
declare i64 @renamed(i64) #0

; first +0, handlers +16 (puts, sqrt), cb +32 (renamed).
@first = global i32 1, align 4
@handlers = global [2 x ptr] [ptr @puts, ptr @sqrt], align 16
@cb = global ptr @renamed, align 8

; CHECK-LABEL: {{^}}.method public static native int 'get_putchar'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_putchar'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'get_putchar'{{$}}
define ptr @get_putchar() {
  ret ptr @putchar
}

; CHECK-NOT:   {{^}}.method {{(public|assembly) static}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[CLIB]]" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" cdecl) float64 'sqrt'(float64) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[CLIB]]" cdecl) int32 'putchar'(int32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("mylib" as "real_name" cdecl) native int 'renamed'(native int) cil managed preservesig {}{{$}}
; CHECK:        {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; CHECK:        {{^ *}}.pack 1{{( |$)}}
; CHECK:        {{^ *}}.size 40{{( |$)}}
; CHECK:        {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; CHECK-NEXT:   {{^}}  01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:   {{^}}  00 00 00 00 00 00 00 00 ){{( |$)}}
; CHECK:        {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; CHECK:        {{^}}.field private static initonly native int '<msil>data'{{$}}
; CHECK:        {{^}}.field public static initonly native int 'first'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'handlers'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'cb'{{( |$)}}
; CHECK-EMPTY:
; CHECK-NEXT:   {{^}}.field private static initonly native int '<msil>fn_puts'{{$}}
; CHECK-NEXT:   {{^}}.field private static initonly native int '<msil>fn_sqrt'{{$}}
; CHECK-NEXT:   {{^}}.field private static initonly native int '<msil>fn_putchar'{{$}}
; CHECK-NEXT:   {{^}}.field private static initonly native int '<msil>fn_renamed'{{$}}
; CHECK-EMPTY:
; CHECK-NEXT:   {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK-NEXT:   {{^\{$}}
; CHECK-NEXT:   {{^}}  .maxstack {{[3-9]}}{{$}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 55( |$)}}
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
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 40( |$)}}
; CHECK-NEXT:   {{^}}  cpblk{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'first'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'handlers'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'cb'{{( |$)}}
;   the function address fields, in module order: each ldftn names the
;   P/Invoke declaration
; CHECK-NEXT:   {{^}}  ldftn int32 'puts'(native int){{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '<msil>fn_puts'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldftn float64 'sqrt'(float64){{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '<msil>fn_sqrt'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldftn int32 'putchar'(int32){{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '<msil>fn_putchar'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldftn native int 'renamed'(native int){{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '<msil>fn_renamed'{{( |$)}}
;   the relocations, in site order
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>fn_puts'{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 24( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>fn_sqrt'{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>fn_renamed'{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  ret{{( |$)}}
; CHECK-NEXT:   {{^}}} // end of method '.cctor'{{$}}
; CHECK-NOT:   {{^}}.method

; NPINV-COUNT-4: pinvokeimpl
; NPINV-NOT:     pinvokeimpl

attributes #0 = { "msil-import-module"="mylib" "msil-import-name"="real_name" }
