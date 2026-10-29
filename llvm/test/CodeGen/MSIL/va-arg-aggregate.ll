; A va_arg instruction of an aggregate type is a clean error (ImplementationPlan.md
; M12.a (4); decision D19 and the section 12 row of 2026-10-21 for M12, 'D19
; note'; recon p2/backlog.md 3.1). Since the upstream merge at 1838cefa99c0
; (upstream 74a120bff99f) the IR verifier rejects such IR on every target:
; llc stops at its input verifier with 'va_arg with an aggregate type is not
; supported' for each such instruction, then 'input module cannot be
; verified', exits with 1 and prints neither 'LLVM ERROR' nor a crash report
; (the VERIFY runs, on both triples). MSILPrepareIR's diagnosis is the
; backstop for IR that skipped the verifier, which the other runs reach with
; -disable-verify: SelectionDAGBuilder::visitVAArg crashed on a struct or
; array type ('Unknown type!', UNREACHABLE in ValueTypes.cpp, for native
; x86-64 as well), so MSILPrepareIR reports such an instruction with the
; instruction selector's text, 'the va_arg instruction is not supported for
; MSIL yet (phase 3 variadics)' (va-intrinsics.ll pins it for scalars),
; naming its function, and deletes the function's body, as for any diagnosed
; definition: the function has no method, and a call of it is erased (its
; result is undefined) instead of becoming an 'undefined function' error. A
; scalar va_arg is still reported by instruction selection, after the
; MSILPrepareIR errors, and compiles to an undefined value (zero); an
; ordinary function compiles as before. llc exits with 1 and prints neither
; 'LLVM ERROR' nor a crash report, on both triples at -O0 and -O2, and the IL
; on stdout assembles.

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=UNREACHABLE --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -disable-verify -verify-machineinstrs < %s -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -disable-verify -verify-machineinstrs < %s -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -disable-verify -verify-machineinstrs < %s -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -disable-verify -verify-machineinstrs < %s -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -disable-verify -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.lnx.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.lnx.il --implicit-check-not="'get_struct'" \
; RUN:   --implicit-check-not="'get_array'" --implicit-check-not="{{^ +call}}"
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -disable-verify -asm-verbose=false -verify-machineinstrs \
; RUN:   < %s > %t.win.o0.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.win.o0.il --implicit-check-not="'get_struct'" \
; RUN:   --implicit-check-not="'get_array'" --implicit-check-not="{{^ +call}}"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -disable-verify -stop-after=msil-prepare-ir < %s -o - \
; RUN:   2>/dev/null > %t.prep.ll
; RUN: FileCheck %s --check-prefix=IR --input-file=%t.prep.ll
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.dll %t.lnx.il %}
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

; CHECK: {{^}}error: {{.*}}in function get_struct {{.*}}: the va_arg instruction is not supported for MSIL yet (phase 3 variadics){{$}}
; CHECK: {{^}}error: {{.*}}in function get_array {{.*}}: the va_arg instruction is not supported for MSIL yet (phase 3 variadics){{$}}
; CHECK: {{^}}error: {{.*}}in function get_scalar {{.*}}: the va_arg instruction is not supported for MSIL yet (phase 3 variadics){{$}}

; VERIFY:      {{^}}va_arg with an aggregate type is not supported{{$}}
; VERIFY-NEXT: {{^}}  %v = va_arg ptr %ap, %struct.P{{$}}
; VERIFY-NEXT: {{^}}va_arg with an aggregate type is not supported{{$}}
; VERIFY-NEXT: {{^}}  %v = va_arg ptr %ap, [2 x i64]{{$}}
; VERIFY-NEXT: {{^}}llc: error: '<stdin>': input module cannot be verified{{$}}

; IR: declare i32 @get_struct(ptr)
define i32 @get_struct(ptr %ap) {
  %v = va_arg ptr %ap, %struct.P
  %a = extractvalue %struct.P %v, 0
  ret i32 %a
}

; IR: declare i32 @get_array(ptr)
define i32 @get_array(ptr %ap) {
  %v = va_arg ptr %ap, [2 x i64]
  %a = extractvalue [2 x i64] %v, 1
  %t = trunc i64 %a to i32
  ret i32 %t
}

; IR-LABEL: define i32 @get_scalar(ptr %ap) {
; IR-NEXT:  %v = va_arg ptr %ap, i32
; IL-LABEL: {{^}}.method public static int32 'get_scalar'(native int) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 1{{$}}
; IL-NEXT:  {{^}}  ldc.i4.0{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'get_scalar'{{$}}
define i32 @get_scalar(ptr %ap) {
  %v = va_arg ptr %ap, i32
  ret i32 %v
}

; IL-LABEL: {{^}}.method public static int32 'calls_get_struct'(native int) cil managed{{$}}
; IL:       {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'calls_get_struct'{{$}}
define i32 @calls_get_struct(ptr %ap) {
  %r = call i32 @get_struct(ptr %ap)
  %s = add i32 %r, 1
  ret i32 %s
}

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
