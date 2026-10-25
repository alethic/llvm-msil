; The entry point (ImplementationPlan.md decision D27, M3.d; plan 5.1; recon
; asmprinter.md 7.4 and 9): when the module defines 'main', the module ends with
; a generated method '<msil>Main'(string[] args) carrying .entrypoint, which
; calls main and returns its result as the process exit code. 'main' keeps its
; name (no __original_main renaming, decision D18) and stays an ordinary public
; method. For 'int main(void)' the generated body is just the call and 'ret'.
; For 'int main(int, char **)' it builds a NULL-terminated argv from
; [mscorlib]System.Environment::GetCommandLineArgs() (argv[0] is the path of the
; assembly), with Marshal::AllocHGlobal for the array and
; Marshal::StringToHGlobalAnsi for each string, and calls
; main(int32, native int). Any other signature of main is a clean error
; ('unsupported signature for main', exit code 1, no crash; plan section 2). A
; module without a definition of main has no entry point. The generated
; methods use only mscorlib (decision D10), and the output assembles (ilasm
; -dll accepts an .entrypoint).

; RUN: split-file %s %t

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/void.ll \
; RUN:   | FileCheck %s --check-prefix=VOID
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/void.ll \
; RUN:   | FileCheck %s --check-prefix=VOID
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/void.ll \
; RUN:   | FileCheck %s --check-prefix=VOID
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/argv.ll \
; RUN:   | FileCheck %s --check-prefixes=ARGV,MAINO2 --implicit-check-not="[System."
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/argv.ll \
; RUN:   | FileCheck %s --check-prefixes=ARGV,MAINO2 --implicit-check-not="[System."
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/argv.ll \
; RUN:   | FileCheck %s --check-prefixes=ARGV,MAINO0 --implicit-check-not="[System."
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/argv.ll \
; RUN:   | FileCheck %s --check-prefixes=ARGV,MAINO0 --implicit-check-not="[System."
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/no-main.ll \
; RUN:   | FileCheck %s --check-prefix=NOMAIN --implicit-check-not=.entrypoint --implicit-check-not="<msil>Main"
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/no-main.ll \
; RUN:   | FileCheck %s --check-prefix=NOMAIN --implicit-check-not=.entrypoint --implicit-check-not="<msil>Main"

; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t/void.ll -o %t/void.il \
; RUN:   && %ilasm-verify -output=%t/void.dll %t/void.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %t/argv.ll -o %t/argv.il \
; RUN:   && %ilasm-verify -output=%t/argv.dll %t/argv.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/argv.ll -o %t/argv-o0.il \
; RUN:   && %ilasm-verify -output=%t/argv-o0.dll %t/argv-o0.il %}

; Every other shape of main is an error.
; DEFINE: %{bad} = FileCheck %s --check-prefix=BAD --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil < %t/bad-i64.ll -o /dev/null 2>&1 | %{bad}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil < %t/bad-i64.ll -o /dev/null 2>&1 | %{bad}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/bad-i64.ll -o /dev/null 2>&1 | %{bad}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil < %t/bad-void.ll -o /dev/null 2>&1 | %{bad}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil < %t/bad-float.ll -o /dev/null 2>&1 | %{bad}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil < %t/bad-argc-only.ll -o /dev/null 2>&1 | %{bad}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil < %t/bad-envp.ll -o /dev/null 2>&1 | %{bad}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil < %t/bad-swapped.ll -o /dev/null 2>&1 | %{bad}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil < %t/bad-i64-argc.ll -o /dev/null 2>&1 | %{bad}
; BAD: error: {{.*}}unsupported signature for main

;--- void.ll
; int main(void): '<msil>Main' calls it and returns its result.
; VOID-LABEL: .method {{.*}}'main'(
; VOID-SAME:  ) cil managed{{$}}
; VOID-NEXT:  {{^\{$}}
; VOID-NEXT:  {{^}}  .maxstack 1{{$}}
; VOID-NEXT:  {{^}}  ldc.i4.s 42{{$}}
; VOID-NEXT:  {{^}}  ret{{$}}
; VOID-NEXT:  {{^}}} // end of method 'main'{{$}}
; VOID-NOT:   .method
; VOID:       {{^}}.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed{{$}}
; VOID-NEXT:  {{^\{$}}
; VOID-NEXT:  {{^}}  .entrypoint{{$}}
; VOID-NEXT:  {{^}}  .maxstack 1{{$}}
; VOID-NEXT:  {{^}}  call int32 'main'(){{$}}
; VOID-NEXT:  {{^}}  ret{{$}}
; VOID-NEXT:  {{^}}} // end of method '<msil>Main'{{$}}
; VOID-NOT:   {{.entrypoint|.method}}
source_filename = "void.c"

define i32 @main() {
  ret i32 42
}

;--- argv.ll
; int main(int argc, char **argv): '<msil>Main' builds argv exactly as recon
; asmprinter.md 9 and plan M3.d specify, with an exact .maxstack and the
; locals (string[] cl, int32 argc, native int argv, int32 i), never 'init':
;   cl = Environment::GetCommandLineArgs(); argc = cl.Length;
;   argv = Marshal::AllocHGlobal((argc + 1) * 8);
;   for (i = 0; i < argc; ++i) argv[i] = Marshal::StringToHGlobalAnsi(cl[i]);
;   argv[argc] = NULL;
;   return main(argc, argv);
; Each slot is 8 bytes (a native int on the 64-bit triples). A body without
; the NULL store, or with 4-byte slots, still assembles, verifies and runs a
; main that only reads argc, so the whole body is checked line by line here;
; ret-argc.ll in msil/test/M1 runs it. main's own body is checked at -O2
; (MAINO2); at -O0 (MAINO0) only its end.
; ARGV:        {{^}}.method public static int32 'main'(int32, native int) cil managed{{$}}
; ARGV-NEXT:   {{^\{$}}
; ARGV-NEXT:   {{^}}  .maxstack 1{{$}}
; MAINO2-NEXT: {{^}}  ldarg.0{{$}}
; MAINO2-NEXT: {{^}}  ret{{$}}
; MAINO2-NEXT: {{^}}} // end of method 'main'{{$}}
; MAINO0:      {{^}}} // end of method 'main'{{$}}
; ARGV-NOT:    .method
; ARGV:        {{^}}.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed{{$}}
; ARGV-NEXT:   {{^\{$}}
; ARGV-NEXT:   {{^}}  .entrypoint{{$}}
; ARGV-NEXT:   {{^}}  .maxstack 3{{$}}
; ARGV-NEXT:   {{^}}  .locals (string[], int32, native int, int32){{$}}
;   cl = GetCommandLineArgs(); argc = cl.Length
; ARGV-NEXT:   {{^}}  call string[] [mscorlib]System.Environment::GetCommandLineArgs(){{$}}
; ARGV-NEXT:   {{^}}  stloc.0{{$}}
; ARGV-NEXT:   {{^}}  ldloc.0{{$}}
; ARGV-NEXT:   {{^}}  ldlen{{$}}
; ARGV-NEXT:   {{^}}  conv.i4{{$}}
; ARGV-NEXT:   {{^}}  stloc.1{{$}}
;   argv = AllocHGlobal((argc + 1) * 8)
; ARGV-NEXT:   {{^}}  ldloc.1{{$}}
; ARGV-NEXT:   {{^}}  ldc.i4.1{{$}}
; ARGV-NEXT:   {{^}}  add{{$}}
; ARGV-NEXT:   {{^}}  conv.i{{$}}
; ARGV-NEXT:   {{^}}  ldc.i4.8{{$}}
; ARGV-NEXT:   {{^}}  mul{{$}}
; ARGV-NEXT:   {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{$}}
; ARGV-NEXT:   {{^}}  stloc.2{{$}}
;   i = 0, then the loop test at the bottom (short branch forms, Q26)
; ARGV-NEXT:   {{^}}  ldc.i4.0{{$}}
; ARGV-NEXT:   {{^}}  stloc.3{{$}}
; ARGV-NEXT:   {{^}}  br.s [[COND:@[^ :]+]]{{$}}
;   *(argv + i * 8) = StringToHGlobalAnsi(cl[i])
; ARGV-NEXT:   {{^}}[[LOOP:@[^ :]+]]:{{$}}
; ARGV-NEXT:   {{^}}  ldloc.2{{$}}
; ARGV-NEXT:   {{^}}  ldloc.3{{$}}
; ARGV-NEXT:   {{^}}  conv.i{{$}}
; ARGV-NEXT:   {{^}}  ldc.i4.8{{$}}
; ARGV-NEXT:   {{^}}  mul{{$}}
; ARGV-NEXT:   {{^}}  add{{$}}
; ARGV-NEXT:   {{^}}  ldloc.0{{$}}
; ARGV-NEXT:   {{^}}  ldloc.3{{$}}
; ARGV-NEXT:   {{^}}  ldelem.ref{{$}}
; ARGV-NEXT:   {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string){{$}}
; ARGV-NEXT:   {{^}}  stind.i{{$}}
;   ++i; loop while i < argc
; ARGV-NEXT:   {{^}}  ldloc.3{{$}}
; ARGV-NEXT:   {{^}}  ldc.i4.1{{$}}
; ARGV-NEXT:   {{^}}  add{{$}}
; ARGV-NEXT:   {{^}}  stloc.3{{$}}
; ARGV-NEXT:   {{^}}[[COND]]:{{$}}
; ARGV-NEXT:   {{^}}  ldloc.3{{$}}
; ARGV-NEXT:   {{^}}  ldloc.1{{$}}
; ARGV-NEXT:   {{^}}  blt.s [[LOOP]]{{$}}
;   *(argv + argc * 8) = NULL
; ARGV-NEXT:   {{^}}  ldloc.2{{$}}
; ARGV-NEXT:   {{^}}  ldloc.1{{$}}
; ARGV-NEXT:   {{^}}  conv.i{{$}}
; ARGV-NEXT:   {{^}}  ldc.i4.8{{$}}
; ARGV-NEXT:   {{^}}  mul{{$}}
; ARGV-NEXT:   {{^}}  add{{$}}
; ARGV-NEXT:   {{^}}  ldc.i4.0{{$}}
; ARGV-NEXT:   {{^}}  conv.i{{$}}
; ARGV-NEXT:   {{^}}  stind.i{{$}}
;   return main(argc, argv)
; ARGV-NEXT:   {{^}}  ldloc.1{{$}}
; ARGV-NEXT:   {{^}}  ldloc.2{{$}}
; ARGV-NEXT:   {{^}}  call int32 'main'(int32, native int){{$}}
; ARGV-NEXT:   {{^}}  ret{{$}}
; ARGV-NEXT:   {{^}}} // end of method '<msil>Main'{{$}}
; ARGV-NOT:    {{.entrypoint|.method}}
source_filename = "argv.c"

define i32 @main(i32 %argc, ptr %argv) {
  ret i32 %argc
}

;--- no-main.ll
; NOMAIN: {{^}}.method public static int32 'not_main'() cil managed{{$}}
; NOMAIN: {{^}}} // end of method 'not_main'{{$}}
source_filename = "no-main.c"

define i32 @not_main() {
  ret i32 0
}

;--- bad-i64.ll
source_filename = "bad.c"

define i64 @main() {
  ret i64 0
}

;--- bad-void.ll
source_filename = "bad.c"

define void @main() {
  ret void
}

;--- bad-float.ll
source_filename = "bad.c"

define float @main() {
  ret float 0.0
}

;--- bad-argc-only.ll
source_filename = "bad.c"

define i32 @main(i32 %argc) {
  ret i32 %argc
}

;--- bad-envp.ll
; main(int, char **, char **) with an environment pointer.
source_filename = "bad.c"

define i32 @main(i32 %argc, ptr %argv, ptr %envp) {
  ret i32 %argc
}

;--- bad-swapped.ll
source_filename = "bad.c"

define i32 @main(ptr %argv, i32 %argc) {
  ret i32 %argc
}

;--- bad-i64-argc.ll
source_filename = "bad.c"

define i32 @main(i64 %argc, ptr %argv) {
  ret i32 0
}
