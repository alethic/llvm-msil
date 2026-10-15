; Label addresses (blockaddress) outside an indirectbr (ImplementationPlan.md
; M8.a, section 12 row 'M8.a extended (BlockAddress)'; plan section 2
; 'Diagnostics leave well-formed output'). IndirectBrExpand (M6) rewrites the
; label addresses of a function that has an indirectbr into block numbers,
; including those stored in initializers, so a table of labels that a computed
; goto uses is ordinary data. A label address in a function without an
; indirectbr reaches instruction selection as ISD::BlockAddress: returned as
; an integer (ptrtoint) or as a pointer, and the GNU C 'return (unsigned long
; long)&&a;' (the Linux _THIS_IP_ idiom, as clang -O2 emits it). Each is one
; error naming its function (the text names a block address or a label),
; compilation goes on, llc exits with 1 and prints neither 'LLVM ERROR' nor a
; crash report, and the IL (taken from stdout) still assembles. In an
; initializer, such a label address is not the address of a global, which
; the data segment cannot relocate (M8.b, 'address of a non-global'): one
; error naming the global. Both triples, -O2 and -O0.

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: split-file %s %t
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/code.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=CODE
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/code.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=CODE
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/code.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=CODE
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/code.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=CODE
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/code.ll \
; RUN:   > %t/code.lnx.il
; RUN: FileCheck %s --check-prefix=CODE-IL --input-file=%t/code.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/code.ll \
; RUN:   > %t/code.win.o0.il
; RUN: FileCheck %s --check-prefix=CODE-IL --input-file=%t/code.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/code.lnx.dll %t/code.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/code.win.o0.dll %t/code.win.o0.il %}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/data.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DATA
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/data.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DATA

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/table.ll \
; RUN:   -o %t/table.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=TABLE --implicit-check-not=".field " --input-file=%t/table.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/table.ll \
; RUN:   -o %t/table.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=TABLE --implicit-check-not=".field " --input-file=%t/table.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/table.dll %t/table.lnx.il %}

;--- code.ll
source_filename = "blockaddress-code.c"

; CODE: {{^}}error: {{.*}}in function ba_i64 {{.*}}{{[Bb]lock ?[Aa]ddress|[Ll]abel}}
; CODE: {{^}}error: {{.*}}in function ba_ptr {{.*}}{{[Bb]lock ?[Aa]ddress|[Ll]abel}}
; CODE: {{^}}error: {{.*}}in function this_ip {{.*}}{{[Bb]lock ?[Aa]ddress|[Ll]abel}}

; CODE-IL-LABEL: .method {{.*}}'ba_i64'(
; CODE-IL:       {{^}}} // end of method 'ba_i64'{{$}}
; CODE-IL-LABEL: .method {{.*}}'ba_ptr'(
; CODE-IL:       {{^}}} // end of method 'ba_ptr'{{$}}
; CODE-IL-LABEL: .method {{.*}}'this_ip'(
; CODE-IL:       {{^}}} // end of method 'this_ip'{{$}}
; CODE-IL-LABEL: .method {{.*}}'after_ba'(
; CODE-IL-NEXT:  {{^\{$}}
; CODE-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; CODE-IL-NEXT:  {{^}}  ldarg.0{{$}}
; CODE-IL-NEXT:  {{^}}  ldarg.1{{$}}
; CODE-IL-NEXT:  {{^}}  add{{$}}
; CODE-IL-NEXT:  {{^}}  ret{{$}}
; CODE-IL-NEXT:  {{^}}} // end of method 'after_ba'{{$}}

define i64 @ba_i64() {
entry:
  br label %b

b:
  ret i64 ptrtoint (ptr blockaddress(@ba_i64, %b) to i64)
}

define ptr @ba_ptr() {
entry:
  br label %l

l:
  ret ptr blockaddress(@ba_ptr, %l)
}

; unsigned long long this_ip(void) { a: return (unsigned long long)&&a; }
define i64 @this_ip() {
entry:
  br label %a

a:
  ret i64 ptrtoint (ptr blockaddress(@this_ip, %a) to i64)
}

define i32 @after_ba(i32 %x, i32 %y) {
  %r = add i32 %x, %y
  ret i32 %r
}

;--- data.ll
; void *lbl = &&x;  (in a function without a computed goto)
source_filename = "blockaddress-data.c"

; DATA: {{^}}error: {{.*(lbl.*(non-global|[Bb]lock ?[Aa]ddress|label)|(non-global|[Bb]lock ?[Aa]ddress|label).*lbl)}}

@lbl = global ptr blockaddress(@h, %x), align 8

define i32 @h(i32 %v) {
entry:
  br label %x

x:
  ret i32 %v
}

;--- table.ll
; static void *tbl[] = {&&b, &&a}; goto *tbl[i]; IndirectBrExpand numbers a 1
; and b 2, so the table holds 2 and 1.
source_filename = "blockaddress-table.c"

@tbl = constant [2 x ptr] [ptr blockaddress(@dispatch, %b), ptr blockaddress(@dispatch, %a)], align 16

; TABLE-LABEL: {{^}}.method public static int32 'dispatch'(native int) cil managed{{$}}
; TABLE:       {{^}}  ldsfld native int 'tbl'{{$}}
; TABLE:       {{^}}} // end of method 'dispatch'{{$}}
define i32 @dispatch(i64 %i) {
entry:
  %p = getelementptr inbounds [2 x ptr], ptr @tbl, i64 0, i64 %i
  %t = load ptr, ptr %p, align 8
  indirectbr ptr %t, [label %a, label %b]

a:
  ret i32 1

b:
  ret i32 2
}

; TABLE-NOT:     {{^}}.method
; TABLE:        {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; TABLE:        {{^ *}}.pack 1{{( |$)}}
; TABLE:        {{^ *}}.size 16{{( |$)}}
; TABLE:        {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; TABLE-NEXT:   {{^}}  02 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00 ){{( |$)}}
; TABLE:        {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; TABLE:        {{^}}.field private static initonly native int '<msil>data'{{$}}
; TABLE:        {{^}}.field public static initonly native int 'tbl'{{( |$)}}
; TABLE:        {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; TABLE-NEXT:   {{^\{$}}
; TABLE-NEXT:   {{^}}  .maxstack {{[3-9]}}{{$}}
; TABLE-NEXT:   {{^}}  {{ldc\.i4(\.s)? 31( |$)}}
; TABLE-NEXT:   {{^}}  conv.i{{( |$)}}
; TABLE-NEXT:   {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; TABLE-NEXT:   {{^}}  {{ldc\.i4(\.s)? 15( |$)}}
; TABLE-NEXT:   {{^}}  conv.i{{( |$)}}
; TABLE-NEXT:   {{^}}  add{{( |$)}}
; TABLE-NEXT:   {{^}}  {{ldc\.i4(\.s)? -16( |$)}}
; TABLE-NEXT:   {{^}}  conv.i{{( |$)}}
; TABLE-NEXT:   {{^}}  and{{( |$)}}
; TABLE-NEXT:   {{^}}  stsfld native int '<msil>data'{{( |$)}}
; TABLE-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TABLE-NEXT:   {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; TABLE-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; TABLE-NEXT:   {{^}}  cpblk{{( |$)}}
; TABLE-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TABLE-NEXT:   {{^}}  stsfld native int 'tbl'{{( |$)}}
; TABLE-NEXT:   {{^}}  ret{{( |$)}}
; TABLE-NEXT:   {{^}}} // end of method '.cctor'{{$}}
; TABLE-NOT:     {{^}}.method
