; The IR va_arg instruction, aggregate types included (ImplementationPlan.md
; 5.15.2 'Compile time', M26; M12.a (4); decision D19 and the section 12 rows
; of 2026-10-21 for M12, 'D19 note', and of 2026-10-28 for M26; recon
; p2/backlog.md 3.1). Since the upstream merge at 1838cefa99c0 (upstream
; 74a120bff99f) the IR verifier rejects a va_arg of an aggregate type on every
; target: llc stops at its input verifier with 'va_arg with an aggregate type
; is not supported' for each such instruction, then 'input module cannot be
; verified', exits with 1 and prints neither 'LLVM ERROR' nor a crash report
; (the VERIFY runs). The other runs skip the verifier with -disable-verify.
; On the SysV and AAPCS64 triples MSILPrepareIR reports every va_arg
; instruction, aggregate or scalar, with 'the va_arg instruction is not
; supported for MSIL on this triple; clang lowers C's va_arg itself', naming
; its function, and deletes the function's body, as for any diagnosed
; definition: the function has no method, and a call of it is erased (its
; result is undefined) instead of becoming an 'undefined function' error; an
; ordinary function compiles as before. llc exits with 1 and prints neither
; 'LLVM ERROR' nor a crash report at -O0 and -O2, and the IL on stdout
; assembles. On the char* triples (windows-msvc) an IR va_arg is accepted and
; expanded with the module's other variadic code (va V8; M12's aggregate rule
; included), so the module compiles without a diagnostic and assembles (the
; WIN runs).

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=UNREACHABLE --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -disable-verify -verify-machineinstrs < %s -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -disable-verify -verify-machineinstrs < %s -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -disable-verify -verify-machineinstrs < %s -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -disable-verify -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.lnx.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.lnx.il --implicit-check-not="'get_struct'" \
; RUN:   --implicit-check-not="'get_array'" --implicit-check-not="'get_scalar'" --implicit-check-not="{{^ +call}}"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -disable-verify -asm-verbose=false -verify-machineinstrs \
; RUN:   < %s > %t.lnx.o0.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.lnx.o0.il --implicit-check-not="'get_struct'" \
; RUN:   --implicit-check-not="'get_array'" --implicit-check-not="'get_scalar'" --implicit-check-not="{{^ +call}}"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -disable-verify -stop-after=msil-prepare-ir < %s -o - \
; RUN:   2>/dev/null > %t.prep.ll
; RUN: FileCheck %s --check-prefix=IR --input-file=%t.prep.ll
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.dll %t.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o0.dll %t.lnx.o0.il %}

; The WIN runs: accepted and expanded on the char* triples.
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -disable-verify -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=WIN --input-file=%t.win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -disable-verify -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=WIN --input-file=%t.win.o0.il
; RUN: llc -mtriple=aarch64-pc-windows-msvc-msil -disable-verify -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.a64win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=WIN --input-file=%t.a64win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.dll %t.win.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

; The VERIFY runs compile the module as it is, which the IR verifier rejects
; before any MSIL pass runs.
; DEFINE: %{verify} = FileCheck %s --check-prefix=VERIFY --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=UNREACHABLE --implicit-check-not=error: \
; DEFINE:   --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{verify}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{verify}

source_filename = "va-arg-aggregate.c"

%struct.P = type { i32, i32 }

; CHECK: {{^}}error: {{.*}}in function get_struct {{.*}}: the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}
; CHECK: {{^}}error: {{.*}}in function get_array {{.*}}: the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}
; CHECK: {{^}}error: {{.*}}in function get_scalar {{.*}}: the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}

; VERIFY:      {{^}}va_arg with an aggregate type is not supported{{$}}
; VERIFY-NEXT: {{^}}  %v = va_arg ptr %ap, %struct.P{{$}}
; VERIFY-NEXT: {{^}}va_arg with an aggregate type is not supported{{$}}
; VERIFY-NEXT: {{^}}  %v = va_arg ptr %ap, [2 x i64]{{$}}
; VERIFY-NEXT: {{^}}llc: error: '<stdin>': input module cannot be verified{{$}}

; IR: declare i32 @get_struct(ptr)
; WIN: {{^}}.method public static int32 'get_struct'(native int) cil managed{{$}}
define i32 @get_struct(ptr %ap) {
  %v = va_arg ptr %ap, %struct.P
  %a = extractvalue %struct.P %v, 0
  ret i32 %a
}

; IR: declare i32 @get_array(ptr)
; WIN: {{^}}.method public static int32 'get_array'(native int) cil managed{{$}}
define i32 @get_array(ptr %ap) {
  %v = va_arg ptr %ap, [2 x i64]
  %a = extractvalue [2 x i64] %v, 1
  %t = trunc i64 %a to i32
  ret i32 %t
}

; IR: declare i32 @get_scalar(ptr)
; WIN: {{^}}.method public static int32 'get_scalar'(native int) cil managed{{$}}
define i32 @get_scalar(ptr %ap) {
  %v = va_arg ptr %ap, i32
  ret i32 %v
}

; IL-LABEL: {{^}}.method public static int32 'calls_get_struct'(native int) cil managed{{$}}
; WIN:      {{^}}.method public static int32 'calls_get_struct'(native int) cil managed{{$}}
; WIN:      {{^}}  call int32 'get_struct'(native int){{$}}
; IL:       {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'calls_get_struct'{{$}}
define i32 @calls_get_struct(ptr %ap) {
  %r = call i32 @get_struct(ptr %ap)
  %s = add i32 %r, 1
  ret i32 %s
}

; WIN:      {{^}}.method public static int32 'ordinary'(int32, int32) cil managed{{$}}
; IL-LABEL: {{^}}.method public static int32 'ordinary'(int32, int32) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 2{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldarg.1{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'ordinary'{{$}}
define i32 @ordinary(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}
