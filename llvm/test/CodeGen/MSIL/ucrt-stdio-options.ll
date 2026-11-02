; The UCRT stdio options in standalone whole-program text (ImplementationPlan.md
; 5.15.2 'startup options', M26; hosted-libc H3; review r1 finding 10).
; MSILAsmPrinter::fillTrailerUCRTStdio finds the definitions of
; __local_stdio_printf_options and __local_stdio_scanf_options (no parameter,
; a native int result: the UCRT's <stdio.h> inline functions) on
; *-windows-msvc-msil triples, and the module initializer ('.cctor', after
; the data segment's setup and before the constructors) ORs vcruntime's
; default bits into each options word, as the native CRT startup does:
;   call native int '<f>'(); dup; ldind.i8; ldc.i8 <bits>; or; stind.i8
; with PrintfBits 0x24 (36) and ScanfBits 0x2 by default (cli::StdioOptions).
; msil-ld's linked text passes the link's bits as the module flags
; "msil-ucrt-printf-bits" and "msil-ucrt-scanf-bits" (section 12 row
; '5.15.2's startup options amended (linked text)'), which then replace the
; defaults (flags.ll). A module that defines only one of the two functions
; gets only its half (printf-only.ll). Other triples, windows-gnu included,
; get no such initializer code, and a definition of another signature is not
; the UCRT's function (other.ll).

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{llc} = llc -verify-machineinstrs -asm-verbose=false
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/both.ll -o %t/both.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=BOTH --input-file=%t/both.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/both.ll -o %t/both.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=BOTH --input-file=%t/both.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/both.win.dll %t/both.win.il %}
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/both.ll -o %t/both.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NONE --input-file=%t/both.lnx.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/both.ll -o %t/both.lnx.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NONE --input-file=%t/both.lnx.o0.il
; RUN: %{llc} -mtriple=x86_64-w64-windows-gnu-msil < %t/both.ll -o %t/both.mingw.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NONE --input-file=%t/both.mingw.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/flags.ll -o %t/flags.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=FLAGS --input-file=%t/flags.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/printf-only.ll -o %t/printf-only.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=PONLY --input-file=%t/printf-only.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/other.ll -o %t/other.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NONE --input-file=%t/other.win.il

; The modules of the NONE runs: an initializer (the data segment's setup)
; without options code.
; NONE:     {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; NONE-NOT: {{^}}  ldind.i8{{$}}
; NONE-NOT: {{^}}  stind.i8{{$}}
; NONE:     {{^}}} // end of method '.cctor'{{$}}

;--- both.ll
source_filename = "both.c"

; BOTH:      {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; BOTH:      {{^}}  call native int '__local_stdio_printf_options'(){{$}}
; BOTH-NEXT: {{^}}  dup{{$}}
; BOTH-NEXT: {{^}}  ldind.i8{{$}}
; BOTH-NEXT: {{^}}  ldc.i8 36{{$}}
; BOTH-NEXT: {{^}}  or{{$}}
; BOTH-NEXT: {{^}}  stind.i8{{$}}
; BOTH-NEXT: {{^}}  call native int '__local_stdio_scanf_options'(){{$}}
; BOTH-NEXT: {{^}}  dup{{$}}
; BOTH-NEXT: {{^}}  ldind.i8{{$}}
; BOTH-NEXT: {{^}}  ldc.i8 2{{$}}
; BOTH-NEXT: {{^}}  or{{$}}
; BOTH-NEXT: {{^}}  stind.i8{{$}}
; BOTH:      {{^}}} // end of method '.cctor'{{$}}

$__local_stdio_printf_options = comdat any
$__local_stdio_scanf_options = comdat any
$_OptionsStorage.p = comdat any
$_OptionsStorage.s = comdat any

@_OptionsStorage.p = linkonce_odr global i64 0, comdat, align 8
@_OptionsStorage.s = linkonce_odr global i64 0, comdat, align 8

define linkonce_odr ptr @__local_stdio_printf_options() noinline comdat {
  ret ptr @_OptionsStorage.p
}

define linkonce_odr ptr @__local_stdio_scanf_options() noinline comdat {
  ret ptr @_OptionsStorage.s
}

define i32 @main() {
  %p = call ptr @__local_stdio_printf_options()
  %s = call ptr @__local_stdio_scanf_options()
  %pv = load i64, ptr %p, align 8
  %sv = load i64, ptr %s, align 8
  %o = or i64 %pv, %sv
  %r = trunc i64 %o to i32
  ret i32 %r
}

;--- flags.ll
source_filename = "flags.c"

; The link's bits replace the defaults (here those of a link with
; -liso_stdio_wide_specifiers: printf 0x24 without 0x4, scanf 0x2 without
; 0x2).
; FLAGS:      {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; FLAGS:      {{^}}  call native int '__local_stdio_printf_options'(){{$}}
; FLAGS-NEXT: {{^}}  dup{{$}}
; FLAGS-NEXT: {{^}}  ldind.i8{{$}}
; FLAGS-NEXT: {{^}}  ldc.i8 32{{$}}
; FLAGS-NEXT: {{^}}  or{{$}}
; FLAGS-NEXT: {{^}}  stind.i8{{$}}
; FLAGS-NEXT: {{^}}  call native int '__local_stdio_scanf_options'(){{$}}
; FLAGS-NEXT: {{^}}  dup{{$}}
; FLAGS-NEXT: {{^}}  ldind.i8{{$}}
; FLAGS-NEXT: {{^}}  ldc.i8 0{{$}}
; FLAGS-NEXT: {{^}}  or{{$}}
; FLAGS-NEXT: {{^}}  stind.i8{{$}}
; FLAGS:      {{^}}} // end of method '.cctor'{{$}}

@_OptionsStorage.p = linkonce_odr global i64 0, align 8
@_OptionsStorage.s = linkonce_odr global i64 0, align 8

define linkonce_odr ptr @__local_stdio_printf_options() noinline {
  ret ptr @_OptionsStorage.p
}

define linkonce_odr ptr @__local_stdio_scanf_options() noinline {
  ret ptr @_OptionsStorage.s
}

define i32 @main() {
  %p = call ptr @__local_stdio_printf_options()
  %s = call ptr @__local_stdio_scanf_options()
  ret i32 0
}

!llvm.module.flags = !{!0, !1}
!0 = !{i32 4, !"msil-ucrt-printf-bits", i64 32}
!1 = !{i32 4, !"msil-ucrt-scanf-bits", i64 0}

;--- printf-only.ll
source_filename = "printf-only.c"

; PONLY:      {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; PONLY:      {{^}}  call native int '__local_stdio_printf_options'(){{$}}
; PONLY-NEXT: {{^}}  dup{{$}}
; PONLY-NEXT: {{^}}  ldind.i8{{$}}
; PONLY-NEXT: {{^}}  ldc.i8 36{{$}}
; PONLY-NEXT: {{^}}  or{{$}}
; PONLY-NEXT: {{^}}  stind.i8{{$}}
; PONLY-NOT:  {{^}}  ldind.i8{{$}}
; PONLY:      {{^}}} // end of method '.cctor'{{$}}

@_OptionsStorage.p = linkonce_odr global i64 0, align 8

define linkonce_odr ptr @__local_stdio_printf_options() noinline {
  ret ptr @_OptionsStorage.p
}

define i32 @main() {
  %p = call ptr @__local_stdio_printf_options()
  ret i32 0
}

;--- other.ll
source_filename = "other.c"

; A printf function with an int32 result and a scanf function with a
; parameter: neither is the UCRT's definition.
@w = global i64 0, align 8

define i32 @__local_stdio_printf_options() noinline {
  ret i32 0
}

define ptr @__local_stdio_scanf_options(i32 %x) noinline {
  ret ptr @w
}

define i32 @main() {
  %p = call i32 @__local_stdio_printf_options()
  %s = call ptr @__local_stdio_scanf_options(i32 1)
  ret i32 %p
}
