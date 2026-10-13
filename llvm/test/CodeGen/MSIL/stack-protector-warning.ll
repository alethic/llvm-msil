; Security hardening and instrumentation that CIL cannot express are ignored
; with a warning, and the module still compiles: no error, exit code 0 and a
; complete ILAsm module.
; - MSILPrepareIR strips the stack-protector attributes (ssp, sspstrong,
;   sspreq) and safestack with a warning naming the function (decision D25,
;   ImplementationPlan.md M2.a item (5)).
; - A "cfguard" module flag (clang -mguard=cf, clang-cl /guard:cf, native
;   Windows bitcode) is reset by MSILAsmPrinter::doInitialization with a
;   warning, so that no WinCFGuard handler runs at the end of the module
;   (amendment log 2026-10-13, M2.d item (1) extended).
; - MSILPrepareIR strips the function entry instrumentation attributes
;   (patchable-function-entry, patchable-function-prefix, fentry-call,
;   mnop-mcount, mrecord-mcount; clang -fpatchable-function-entry, -pg
;   -mfentry) with a warning naming the function when they ask for
;   instrumentation, and erases pseudo probes (llvm.pseudoprobe calls and
;   !llvm.pseudo_probe_desc; clang -fpseudo-probe-for-profiling) with one
;   warning for the module (amendment log 2026-10-13, M2.a item (5) extended).
; - MSILPrepareIR strips the function padding attributes ("tail-pad-to-size",
;   "tail-pad-value"; the DynamicDebugging pass) with a warning naming the
;   function when it asks for padding, and turns off the call graph section
;   (--call-graph-section, clang -fexperimental-call-graph-section), the basic
;   block address map (--basic-block-address-map, clang
;   -fbasic-block-sections=labels) and !pcsections metadata with one warning
;   each for the module (amendment log 2026-10-13, M3 fix-loop rows for fix-5).
; The stack protector, control flow guard and entry instrumentation cases were
; written in M2, before function bodies could be compiled: their full llc runs
; compile declarations, and definitions are checked with
; -stop-after=msil-prepare-ir. The M3 cases compile leaf definitions through
; the whole pipeline and check the IL.

; RUN: split-file %s %t

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/protector.ll -o %t/lnx.il 2>%t/lnx.err
; RUN: FileCheck %s --check-prefix=WARN --input-file=%t/lnx.err --implicit-check-not=warning:
; RUN: FileCheck %s --check-prefix=NOWARN --input-file=%t/lnx.err
; RUN: FileCheck %s --check-prefix=IL --input-file=%t/lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/protector.ll -o %t/win.il 2>%t/win.err
; RUN: FileCheck %s --check-prefix=WARN --input-file=%t/win.err --implicit-check-not=warning:
; RUN: FileCheck %s --check-prefix=NOWARN --input-file=%t/win.err
; RUN: FileCheck %s --check-prefix=IL --input-file=%t/win.il

; WARN-DAG: warning: {{.*}}msil_sp_basic{{.*}}stack protector is not supported for MSIL; attribute ignored
; WARN-DAG: warning: {{.*}}msil_sp_strong{{.*}}stack protector is not supported for MSIL; attribute ignored
; WARN-DAG: warning: {{.*}}msil_sp_req{{.*}}stack protector is not supported for MSIL; attribute ignored
; WARN-DAG: warning: {{.*}}msil_safe_stack{{.*}}stack protector is not supported for MSIL; attribute ignored
; WARN-NOT: warning:

; No error, and no warning for the function without these attributes.
; NOWARN-NOT: {{error|LLVM ERROR|PLEASE submit|msil_unprotected}}

; IL: .assembly extern mscorlib
; IL: .assembly 'protector' { .ver 0:0:0:0 }

; The attributes are gone after MSILPrepareIR; the other attributes stay.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/protector.ll -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=IR --implicit-check-not=ssp --implicit-check-not=safestack
; IR: declare void @msil_sp_basic() #[[ATTRS:[0-9]+]]
; IR: declare void @msil_sp_strong() #[[ATTRS]]
; IR: declare void @msil_sp_req() #[[ATTRS]]
; IR: declare void @msil_safe_stack() #[[ATTRS]]
; IR: declare void @msil_unprotected() #[[ATTRS]]
; IR: attributes #[[ATTRS]] = { nounwind }

; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %t/protector.ll -o %t/protector.il \
; RUN:   && %ilasm-verify -output=%t/protector.dll %t/protector.il %}

; Control flow guard, "cfguard" = 2 (checks and tables) on both triples and
; 1 (tables only) on Windows: exactly one warning, exit code 0 and the module
; header. A function in llvm.used gives the guard handler a symbol to record.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/cfguard.ll -o %t/cfg-lnx.il 2>%t/cfg-lnx.err
; RUN: FileCheck %s --check-prefix=CFG --input-file=%t/cfg-lnx.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefixes=CFG-IL,CFG-IL-LNX --input-file=%t/cfg-lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/cfguard.ll -o %t/cfg-win.il 2>%t/cfg-win.err
; RUN: FileCheck %s --check-prefix=CFG --input-file=%t/cfg-win.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefixes=CFG-IL,CFG-IL-WIN --input-file=%t/cfg-win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/cfguard-table.ll -o %t/cfg-table.il 2>%t/cfg-table.err
; RUN: FileCheck %s --check-prefix=CFG --input-file=%t/cfg-table.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefixes=CFG-IL,CFG-IL-WIN --input-file=%t/cfg-table.il

; CFG: warning: {{.*}}control flow guard is not supported for MSIL; ignored

; CFG-IL:          // MSIL module generated by LLVM
; CFG-IL-NEXT:     // source_filename = "cfguard.c"
; CFG-IL-LNX-NEXT: // target triple   = "x86_64-unknown-linux-gnu-msil"
; CFG-IL-WIN-NEXT: // target triple   = "x86_64-pc-windows-msvc-msil"
; CFG-IL-NEXT:     .assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
; CFG-IL-NEXT:     .assembly 'cfguard' { .ver 0:0:0:0 }

; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t/cfguard.ll -o %t/cfguard.il \
; RUN:   && %ilasm-verify -output=%t/cfguard.dll %t/cfguard.il %}

; Function entry instrumentation and pseudo probes on definitions, up to
; MSILPrepareIR: one warning for each function whose attributes ask for
; instrumentation, none for "patchable-function-entry"="0", and one for the
; pseudo probes of the module. Afterwards the attributes, the probe calls and
; the descriptors are gone; the other attributes stay.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/entry-instr.ll -o %t/entry-lnx.mir \
; RUN:   2>%t/entry-lnx.err
; RUN: FileCheck %s --check-prefix=ENTRY-WARN --input-file=%t/entry-lnx.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=FOUR-WARNINGS --input-file=%t/entry-lnx.err
; RUN: FileCheck %s --check-prefix=ENTRY-IR --input-file=%t/entry-lnx.mir --implicit-check-not=patchable-function \
; RUN:   --implicit-check-not=fentry-call --implicit-check-not=mcount --implicit-check-not=llvm.pseudoprobe \
; RUN:   --implicit-check-not=llvm.pseudo_probe_desc
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=msil-prepare-ir < %t/entry-instr.ll -o %t/entry-win.mir \
; RUN:   2>%t/entry-win.err
; RUN: FileCheck %s --check-prefix=ENTRY-WARN --input-file=%t/entry-win.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=FOUR-WARNINGS --input-file=%t/entry-win.err
; RUN: FileCheck %s --check-prefix=ENTRY-IR --input-file=%t/entry-win.mir --implicit-check-not=patchable-function \
; RUN:   --implicit-check-not=fentry-call --implicit-check-not=mcount --implicit-check-not=llvm.pseudoprobe \
; RUN:   --implicit-check-not=llvm.pseudo_probe_desc

; ENTRY-WARN-DAG: warning: {{.*}}msil_patchable_entry{{.*}}function entry instrumentation is not supported for MSIL; attribute ignored
; ENTRY-WARN-DAG: warning: {{.*}}msil_patchable_prefix{{.*}}function entry instrumentation is not supported for MSIL; attribute ignored
; ENTRY-WARN-DAG: warning: {{.*}}msil_fentry{{.*}}function entry instrumentation is not supported for MSIL; attribute ignored
; ENTRY-WARN-DAG: warning: pseudo probes are not supported for MSIL; ignored
; ENTRY-WARN-NOT: warning:

; The four warnings above match distinct lines; there are no others.
; FOUR-WARNINGS-COUNT-4: warning:
; FOUR-WARNINGS-NOT:     warning:

; ENTRY-IR:      define void @msil_patchable_entry() #[[ENTRY_ATTRS:[0-9]+]]
; ENTRY-IR:      define void @msil_patchable_prefix() #[[ENTRY_ATTRS]]
; ENTRY-IR:      define void @msil_fentry() #[[ENTRY_ATTRS]]
; ENTRY-IR:      define void @msil_patchable_zero() #[[ENTRY_ATTRS]]
; ENTRY-IR:      define void @msil_probed() #[[ENTRY_ATTRS]]
; ENTRY-IR-NEXT:   ret void
; ENTRY-IR:      attributes #[[ENTRY_ATTRS]] = { nounwind }

; The whole pipeline on declarations with the same attributes and a pseudo
; probe descriptor: the same warnings, exit code 0 and the module header, with
; no pseudo probe directive or section in the output.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/entry-instr-decl.ll -o %t/entry-decl-lnx.il \
; RUN:   2>%t/entry-decl-lnx.err
; RUN: FileCheck %s --check-prefix=ENTRY-WARN --input-file=%t/entry-decl-lnx.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=FOUR-WARNINGS --input-file=%t/entry-decl-lnx.err
; RUN: FileCheck %s --check-prefixes=ENTRY-IL,ENTRY-IL-LNX --input-file=%t/entry-decl-lnx.il --implicit-check-not=pseudo
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/entry-instr-decl.ll -o %t/entry-decl-win.il \
; RUN:   2>%t/entry-decl-win.err
; RUN: FileCheck %s --check-prefix=ENTRY-WARN --input-file=%t/entry-decl-win.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=FOUR-WARNINGS --input-file=%t/entry-decl-win.err
; RUN: FileCheck %s --check-prefixes=ENTRY-IL,ENTRY-IL-WIN --input-file=%t/entry-decl-win.il --implicit-check-not=pseudo

; ENTRY-IL:          // MSIL module generated by LLVM
; ENTRY-IL-NEXT:     // source_filename = "entry-instr-decl.c"
; ENTRY-IL-LNX-NEXT: // target triple   = "x86_64-unknown-linux-gnu-msil"
; ENTRY-IL-WIN-NEXT: // target triple   = "x86_64-pc-windows-msvc-msil"
; ENTRY-IL-NEXT:     .assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
; ENTRY-IL-NEXT:     .assembly 'entry-instr-decl' { .ver 0:0:0:0 }

; RUN: %if ilasm %{ %ilasm-verify -output=%t/entry-instr-decl.dll %t/entry-decl-lnx.il %}

; Function padding: one warning for the function that asks for 64 bytes, none
; for "tail-pad-to-size"="0", exit code 0, and each method ends right after its
; 'ret', with no native fill directive or label.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/tail-pad.ll \
; RUN:   -o %t/tail-pad-lnx.il 2>%t/tail-pad-lnx.err
; RUN: FileCheck %s --check-prefix=TAILPAD --input-file=%t/tail-pad-lnx.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=TAILPAD-IL --input-file=%t/tail-pad-lnx.il --implicit-check-not=.zero \
; RUN:   --implicit-check-not=tail_pad --implicit-check-not=func_begin
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/tail-pad.ll \
; RUN:   -o %t/tail-pad-win.il 2>%t/tail-pad-win.err
; RUN: FileCheck %s --check-prefix=TAILPAD --input-file=%t/tail-pad-win.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=TAILPAD-IL --input-file=%t/tail-pad-win.il --implicit-check-not=.zero \
; RUN:   --implicit-check-not=tail_pad --implicit-check-not=func_begin
; RUN: %if ilasm %{ %ilasm-verify -output=%t/tail-pad.dll %t/tail-pad-win.il %}

; TAILPAD: warning: {{.*}}in function msil_tail_pad i32 (): function padding is not supported for MSIL; attribute ignored

; TAILPAD-IL-LABEL: {{^}}.method public static int32 'msil_tail_pad'() cil managed{{$}}
; TAILPAD-IL-NEXT:  {{^\{$}}
; TAILPAD-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; TAILPAD-IL-NEXT:  {{^}}  ldc.i4.7{{$}}
; TAILPAD-IL-NEXT:  {{^}}  ret{{$}}
; TAILPAD-IL-NEXT:  {{^}}} // end of method 'msil_tail_pad'{{$}}
; TAILPAD-IL-LABEL: {{^}}.method public static int32 'msil_tail_pad_zero'() cil managed{{$}}
; TAILPAD-IL-NEXT:  {{^\{$}}
; TAILPAD-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; TAILPAD-IL-NEXT:  {{^}}  ldc.i4.8{{$}}
; TAILPAD-IL-NEXT:  {{^}}  ret{{$}}
; TAILPAD-IL-NEXT:  {{^}}} // end of method 'msil_tail_pad_zero'{{$}}
; TAILPAD-IL-NOT:   {{.}}

; The call graph section and the basic block address map: one warning each,
; exit code 0, and the method alone, with none of the labels or sections they
; would add.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil --call-graph-section -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/cg-map.ll -o %t/cg-lnx.il 2>%t/cg-lnx.err
; RUN: FileCheck %s --check-prefix=CG --input-file=%t/cg-lnx.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=CG-MAP-IL --input-file=%t/cg-lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil --call-graph-section -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/cg-map.ll -o %t/cg-win.il 2>%t/cg-win.err
; RUN: FileCheck %s --check-prefix=CG --input-file=%t/cg-win.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=CG-MAP-IL --input-file=%t/cg-win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/cg.dll %t/cg-win.il %}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil --basic-block-address-map -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/cg-map.ll -o %t/map-lnx.il 2>%t/map-lnx.err
; RUN: FileCheck %s --check-prefix=MAP --input-file=%t/map-lnx.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=CG-MAP-IL --input-file=%t/map-lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil --basic-block-address-map -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/cg-map.ll -o %t/map-win.il 2>%t/map-win.err
; RUN: FileCheck %s --check-prefix=MAP --input-file=%t/map-win.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=CG-MAP-IL --input-file=%t/map-win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/map.dll %t/map-lnx.il %}

; CG: warning: call graph section is not supported for MSIL; ignored
; MAP: warning: basic block address map is not supported for MSIL; ignored

; CG-MAP-IL-LABEL: {{^}}.method public static int32 'msil_cg_map'(int32) cil managed{{$}}
; CG-MAP-IL-NEXT:  {{^\{$}}
; CG-MAP-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; CG-MAP-IL-NEXT:  {{^}}  ldarg.0{{$}}
; CG-MAP-IL-NEXT:  {{^}}  ret{{$}}
; CG-MAP-IL-NEXT:  {{^}}} // end of method 'msil_cg_map'{{$}}
; CG-MAP-IL-NOT:   {{.}}

; !pcsections on a function and on instructions (a value and a return): one
; warning for the module, exit code 0 at -O2 and -O0, the metadata gone after
; MSILPrepareIR, and methods with no PC section labels or sections.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/pcsections.ll \
; RUN:   -o %t/pcs-lnx.il 2>%t/pcs-lnx.err
; RUN: FileCheck %s --check-prefix=PCS --input-file=%t/pcs-lnx.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=PCS-IL --input-file=%t/pcs-lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/pcsections.ll \
; RUN:   -o %t/pcs-win.il 2>%t/pcs-win.err
; RUN: FileCheck %s --check-prefix=PCS --input-file=%t/pcs-win.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=PCS-IL --input-file=%t/pcs-win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/pcsections.ll \
; RUN:   -o %t/pcs-win-O0.il 2>%t/pcs-win-O0.err
; RUN: FileCheck %s --check-prefix=PCS --input-file=%t/pcs-win-O0.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --check-prefix=PCS-IL --input-file=%t/pcs-win-O0.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/pcsections.ll -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=PCS-IR --implicit-check-not=pcsections
; RUN: %if ilasm %{ %ilasm-verify -output=%t/pcs.dll %t/pcs-win.il %}

; PCS: warning: PC sections are not supported for MSIL; ignored

; PCS-IR:      define i32 @msil_pcs_function(i32 %a) {{\{$}}
; PCS-IR:      define i32 @msil_pcs_instruction() {{\{$}}
; PCS-IR-NEXT:   %v = freeze i32 7{{$}}
; PCS-IR:      define i32 @msil_pcs_ret(i32 %a) {{\{$}}
; PCS-IR-NEXT:   ret i32 %a{{$}}

; PCS-IL-LABEL: {{^}}.method public static int32 'msil_pcs_function'(int32) cil managed{{$}}
; PCS-IL-NEXT:  {{^\{$}}
; PCS-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; PCS-IL-NEXT:  {{^}}  ldarg.0{{$}}
; PCS-IL-NEXT:  {{^}}  ret{{$}}
; PCS-IL-NEXT:  {{^}}} // end of method 'msil_pcs_function'{{$}}
; PCS-IL-LABEL: {{^}}.method public static int32 'msil_pcs_instruction'() cil managed{{$}}
; PCS-IL-NEXT:  {{^\{$}}
; PCS-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; PCS-IL-NEXT:  {{^}}  ldc.i4.7{{$}}
; PCS-IL-NEXT:  {{^}}  ret{{$}}
; PCS-IL-NEXT:  {{^}}} // end of method 'msil_pcs_instruction'{{$}}
; PCS-IL-LABEL: {{^}}.method public static int32 'msil_pcs_ret'(int32) cil managed{{$}}
; PCS-IL-NEXT:  {{^\{$}}
; PCS-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; PCS-IL-NEXT:  {{^}}  ldarg.0{{$}}
; PCS-IL-NEXT:  {{^}}  ret{{$}}
; PCS-IL-NEXT:  {{^}}} // end of method 'msil_pcs_ret'{{$}}
; PCS-IL-NOT:   {{.}}

;--- protector.ll
source_filename = "protector.c"

declare void @msil_sp_basic() #0
declare void @msil_sp_strong() #1
declare void @msil_sp_req() #2
declare void @msil_safe_stack() #3
declare void @msil_unprotected() #4

attributes #0 = { nounwind ssp }
attributes #1 = { nounwind sspstrong }
attributes #2 = { nounwind sspreq }
attributes #3 = { nounwind safestack }
attributes #4 = { nounwind }

;--- cfguard.ll
source_filename = "cfguard.c"

@llvm.used = appending global [1 x ptr] [ptr @msil_cfg_fn], section "llvm.metadata"

declare void @msil_cfg_fn()

!llvm.module.flags = !{!0}
!0 = !{i32 2, !"cfguard", i32 2}

;--- cfguard-table.ll
source_filename = "cfguard.c"

@llvm.used = appending global [1 x ptr] [ptr @msil_cfg_fn], section "llvm.metadata"

declare void @msil_cfg_fn()

!llvm.module.flags = !{!0}
!0 = !{i32 2, !"cfguard", i32 1}

;--- entry-instr.ll
source_filename = "entry-instr.c"

define void @msil_patchable_entry() #0 {
  ret void
}

define void @msil_patchable_prefix() #1 {
  ret void
}

define void @msil_fentry() #2 {
  ret void
}

define void @msil_patchable_zero() #3 {
  ret void
}

define void @msil_probed() #4 {
  call void @llvm.pseudoprobe(i64 6028998432455395745, i64 1, i32 0, i64 -1)
  ret void
}

declare void @llvm.pseudoprobe(i64, i64, i32, i64)

attributes #0 = { nounwind "patchable-function-entry"="2" }
attributes #1 = { nounwind "patchable-function-prefix"="1" }
attributes #2 = { nounwind "fentry-call"="true" "mnop-mcount" "mrecord-mcount" }
attributes #3 = { nounwind "patchable-function-entry"="0" }
attributes #4 = { nounwind }

!llvm.pseudo_probe_desc = !{!0}
!0 = !{i64 6028998432455395745, i64 4294967295, !"msil_probed"}

;--- tail-pad.ll
source_filename = "tail-pad.c"

define i32 @msil_tail_pad() #0 {
  ret i32 7
}

define i32 @msil_tail_pad_zero() #1 {
  ret i32 8
}

attributes #0 = { nounwind "tail-pad-to-size"="64" "tail-pad-value"="144" }
attributes #1 = { nounwind "tail-pad-to-size"="0" }

;--- cg-map.ll
source_filename = "cg-map.c"

define i32 @msil_cg_map(i32 %a) {
  ret i32 %a
}

;--- pcsections.ll
source_filename = "pcs.c"

define i32 @msil_pcs_function(i32 %a) !pcsections !0 {
  ret i32 %a
}

define i32 @msil_pcs_instruction() {
  %v = freeze i32 7, !pcsections !0
  ret i32 %v
}

define i32 @msil_pcs_ret(i32 %a) {
  ret i32 %a, !pcsections !0
}

!0 = !{!"msil.pcs"}

;--- entry-instr-decl.ll
source_filename = "entry-instr-decl.c"

declare void @msil_patchable_entry() #0
declare void @msil_patchable_prefix() #1
declare void @msil_fentry() #2
declare void @msil_patchable_zero() #3

attributes #0 = { nounwind "patchable-function-entry"="2" }
attributes #1 = { nounwind "patchable-function-prefix"="1" }
attributes #2 = { nounwind "fentry-call"="true" "mnop-mcount" "mrecord-mcount" }
attributes #3 = { nounwind "patchable-function-entry"="0" }

!llvm.pseudo_probe_desc = !{!0}
!0 = !{i64 6028998432455395745, i64 4294967295, !"msil_probed"}
