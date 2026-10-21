; MIR printed at the msil-isel stop point must pass the machine verifier that
; llc runs at the end of a pipeline that stops before code emission (upstream
; 663669080864; -disable-mir-output-verify opts out, and no RUN line here
; passes it or -verify-machineinstrs=0). Every MSIL instruction carries
; 'implicit-def dead $arguments' and ARGUMENT_<RC> reads 'implicit $arguments'
; (plan 5.4), and the ScheduleDAG scheduler may put an ARGUMENT after another
; instruction; the verifier then reports 'Using an undefined physical
; register' for the ARGUMENT, and the MIR parser, which verifies every
; function it reads, rejects the output. MSILDAGToDAGISel moves every ARGUMENT
; to the top of the entry block before it returns (section 12 row 'Plan 5.3
; and 5.4 extended', 2026-10-20), so -stop-after=msil-isel and
; -stop-before=msil-argument-move print the same verifier-clean MIR.
; MSILArgumentMove still runs and still repairs hand-written MIR
; (reg-argument.mir).
;
; sel and rotr_i64 are scheduled with an ARGUMENT after another instruction at
; -O0 and -O2. The checks require every ARGUMENT before the first other
; instruction, but pin neither the order among the ARGUMENTs nor the virtual
; register numbers (both follow the scheduler). sel_dbg is the -O2 IR that
; clang -g emits for 'int sel(_Bool c, int a, int b) { return c ? a : b; }'
; (x86_64-unknown-linux-gnu-msil), with the function renamed, its debug
; records and metadata kept (the file directory is /tmp) and the datalayout
; and triple removed. SelectionDAGISel puts the DBG_VALUE of each argument
; right after its ARGUMENT, so instruction selection printed them
; interleaved; the DBG_VALUEs stay where they are, so each argument keeps its
; DBG_VALUE and all of them follow the moved ARGUMENTs. That interleaved order
; has no other instruction before an ARGUMENT, so the verifier accepts it and
; sel_dbg fails only through its CHECK-NEXT lines. rotr_dbg is the same -O2
; IR for dbg.c's 'unsigned long long rotr(unsigned long long a, unsigned long
; long b) { return (a >> (b & 63)) | (a << ((-b) & 63)); }', renamed the same
; way (its metadata follows sel_dbg's): instruction selection put CONV_I4_I64
; between its two ARGUMENTs, so the verifier rejected the debug-info function
; on its own.
;
; The stop runs without -verify-machineinstrs exercise the verifier at the end
; of the pipeline, the two with it the verifier after instruction selection.
; Each -stop-after=msil-isel output, at -O0 and at -O2, is read back with
; -run-pass=none and compiled on to IL with -start-after=msil-isel at the same
; level; the IL must equal the full pipeline's at that level (the rest of the
; -O0 pipeline differs from -O2: it has no msil-optimize-live-intervals or
; msil-reg-coloring, and its IL has different .locals), so moving the
; ARGUMENTs changes no IL.
;
; The CANARY runs are controls, like the native run in function-splitting.ll:
; they show that the scheduler still places an ARGUMENT late in these inputs,
; so the checks above keep exercising the verifier failure. MSILDAGToDAGISel
; moves the ARGUMENTs inside its runOnMachineFunction, after SelectionDAGISel
; has printed the 'MachineFunction at end of ISel' dump of -debug-only=isel
; (assertions builds only), so only that dump shows the scheduler's order.
; Between its '# Machine code for function <f>:' and '# End machine code for
; function <f>.' lines, the CANARY lines require a CONST_, AND_ or CONV_
; instruction before an ARGUMENT_ in sel, rotr_i64 and rotr_dbg, and a
; DBG_VALUE before an ARGUMENT_ in sel_dbg. They match only those dump
; regions, which the MIR of the other runs (YAML, after the move) never
; contains, and only machine instructions ('%<n>:<class> = '), not the DAG
; nodes of the dump. If upstream stops misordering, a control fails and that
; input must be replaced by one that the scheduler still misorders.
;
; The LATE runs stop after and before every later MSIL pass that no other test
; stops at (msil-set-mem-flags and msil-explicit-locals are stop points of
; load-store-unaligned.ll, volatile.ll and implicit-def.ll), on both triples,
; at -O2 and, for the passes of the -O0 pipeline, at -O0. Each run exits 0
; only if the end-of-pipeline verifier accepts the MIR. llc silently ignores
; a stop at a registered pass that is not in the pipeline (it runs every pass
; and prints the MIR at the end), so each run also prints its pass list with
; -debug-pass=Structure (the MIR goes to /dev/null), and the LATE lines
; require the pass that -DLAST names to be the last MSIL pass, listed before
; 'MIR Printing Pass' with no MSIL pass after it. After -stop-after=X that is
; X; after -stop-before=X it is the MSIL pass before X at that level, and
; --implicit-check-not shows that X is not listed. A stop before X is not
; compared with a stop after the MSIL pass before it: generic passes run in
; between, so the MIR differs. The MIR of the LATE runs is not read back: from
; branch-folder on, a function with a jump table prints MIR that the MIR
; parser rejects (an upstream defect; section 12, 2026-10-20).

; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O0 -stop-after=msil-isel -o %t.lnx.O0.mir
; RUN: FileCheck %s --input-file=%t.lnx.O0.mir
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O2 -stop-after=msil-isel -o %t.lnx.O2.mir
; RUN: FileCheck %s --input-file=%t.lnx.O2.mir
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O0 -stop-after=msil-isel -o %t.win.O0.mir
; RUN: FileCheck %s --input-file=%t.win.O0.mir
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O2 -stop-after=msil-isel -o %t.win.O2.mir
; RUN: FileCheck %s --input-file=%t.win.O2.mir
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O0 -stop-before=msil-argument-move -o - \
; RUN:   | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O2 -stop-before=msil-argument-move -o - \
; RUN:   | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O0 -stop-before=msil-argument-move -o - \
; RUN:   | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O2 -stop-before=msil-argument-move -o - \
; RUN:   | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O2 -stop-after=msil-isel \
; RUN:   -verify-machineinstrs -o - | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O2 -stop-after=msil-isel \
; RUN:   -verify-machineinstrs -o - | FileCheck %s

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -run-pass=none %t.lnx.O0.mir -o /dev/null
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -run-pass=none %t.lnx.O2.mir -o /dev/null
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -run-pass=none %t.win.O0.mir -o /dev/null
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -run-pass=none %t.win.O2.mir -o /dev/null
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -start-after=msil-isel -verify-machineinstrs \
; RUN:   %t.lnx.O0.mir -o %t.lnx.O0.il
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs -o %t.lnx.O0.ref.il
; RUN: diff %t.lnx.O0.ref.il %t.lnx.O0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -start-after=msil-isel -verify-machineinstrs \
; RUN:   %t.win.O0.mir -o %t.win.O0.il
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs -o %t.win.O0.ref.il
; RUN: diff %t.win.O0.ref.il %t.win.O0.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -start-after=msil-isel -verify-machineinstrs \
; RUN:   %t.lnx.O2.mir -o %t.lnx.il
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O2 -verify-machineinstrs -o %t.lnx.ref.il
; RUN: diff %t.lnx.ref.il %t.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -start-after=msil-isel -verify-machineinstrs \
; RUN:   %t.win.O2.mir -o %t.win.il
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O2 -verify-machineinstrs -o %t.win.ref.il
; RUN: diff %t.win.ref.il %t.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.dll %t.win.il %}

; RUN: %if asserts %{ llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O0 -stop-after=msil-isel \
; RUN:   -debug-only=isel -o /dev/null 2>&1 | FileCheck %s --check-prefix=CANARY %}
; RUN: %if asserts %{ llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O2 -stop-after=msil-isel \
; RUN:   -debug-only=isel -o /dev/null 2>&1 | FileCheck %s --check-prefix=CANARY %}
; RUN: %if asserts %{ llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O0 -stop-after=msil-isel \
; RUN:   -debug-only=isel -o /dev/null 2>&1 | FileCheck %s --check-prefix=CANARY %}
; RUN: %if asserts %{ llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O2 -stop-after=msil-isel \
; RUN:   -debug-only=isel -o /dev/null 2>&1 | FileCheck %s --check-prefix=CANARY %}

; DEFINE: %{late-lnx} = llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -debug-pass=Structure \
; DEFINE:   -o /dev/null
; DEFINE: %{late-win} = llc < %s -mtriple=x86_64-pc-windows-msvc-msil -debug-pass=Structure \
; DEFINE:   -o /dev/null
; DEFINE: %{late-check} = 2>&1 | FileCheck %s --check-prefix=LATE
; RUN: %{late-lnx} -O2 -stop-after=msil-clean-code-after-trap %{late-check} \
; RUN:   -DLAST='MSIL Clean Code After Trap'
; RUN: %{late-win} -O2 -stop-after=msil-clean-code-after-trap %{late-check} \
; RUN:   -DLAST='MSIL Clean Code After Trap'
; RUN: %{late-lnx} -O2 -stop-before=msil-clean-code-after-trap %{late-check} \
; RUN:   -DLAST='MSIL Set Memory Flags' --implicit-check-not='MSIL Clean Code After Trap'
; RUN: %{late-win} -O2 -stop-before=msil-clean-code-after-trap %{late-check} \
; RUN:   -DLAST='MSIL Set Memory Flags' --implicit-check-not='MSIL Clean Code After Trap'
; RUN: %{late-lnx} -O2 -stop-after=msil-nullify-dbg-value-lists %{late-check} \
; RUN:   -DLAST='MSIL Nullify DBG_VALUE_LISTs'
; RUN: %{late-win} -O2 -stop-after=msil-nullify-dbg-value-lists %{late-check} \
; RUN:   -DLAST='MSIL Nullify DBG_VALUE_LISTs'
; RUN: %{late-lnx} -O2 -stop-before=msil-nullify-dbg-value-lists %{late-check} \
; RUN:   -DLAST='MSIL Clean Code After Trap' --implicit-check-not='MSIL Nullify DBG_VALUE_LISTs'
; RUN: %{late-win} -O2 -stop-before=msil-nullify-dbg-value-lists %{late-check} \
; RUN:   -DLAST='MSIL Clean Code After Trap' --implicit-check-not='MSIL Nullify DBG_VALUE_LISTs'
; RUN: %{late-lnx} -O2 -stop-after=msil-replace-phys-regs %{late-check} \
; RUN:   -DLAST='MSIL Replace Physical Registers'
; RUN: %{late-win} -O2 -stop-after=msil-replace-phys-regs %{late-check} \
; RUN:   -DLAST='MSIL Replace Physical Registers'
; RUN: %{late-lnx} -O2 -stop-before=msil-replace-phys-regs %{late-check} \
; RUN:   -DLAST='MSIL Nullify DBG_VALUE_LISTs' --implicit-check-not='MSIL Replace Physical Registers'
; RUN: %{late-win} -O2 -stop-before=msil-replace-phys-regs %{late-check} \
; RUN:   -DLAST='MSIL Nullify DBG_VALUE_LISTs' --implicit-check-not='MSIL Replace Physical Registers'
; RUN: %{late-lnx} -O2 -stop-after=msil-optimize-live-intervals %{late-check} \
; RUN:   -DLAST='MSIL Optimize Live Intervals'
; RUN: %{late-win} -O2 -stop-after=msil-optimize-live-intervals %{late-check} \
; RUN:   -DLAST='MSIL Optimize Live Intervals'
; RUN: %{late-lnx} -O2 -stop-before=msil-optimize-live-intervals %{late-check} \
; RUN:   -DLAST='MSIL Replace Physical Registers' --implicit-check-not='MSIL Optimize Live Intervals'
; RUN: %{late-win} -O2 -stop-before=msil-optimize-live-intervals %{late-check} \
; RUN:   -DLAST='MSIL Replace Physical Registers' --implicit-check-not='MSIL Optimize Live Intervals'
; RUN: %{late-lnx} -O2 -stop-after=msil-reg-stackify %{late-check} \
; RUN:   -DLAST='MSIL Register Stackify'
; RUN: %{late-win} -O2 -stop-after=msil-reg-stackify %{late-check} \
; RUN:   -DLAST='MSIL Register Stackify'
; RUN: %{late-lnx} -O2 -stop-before=msil-reg-stackify %{late-check} \
; RUN:   -DLAST='MSIL Optimize Live Intervals' --implicit-check-not='MSIL Register Stackify'
; RUN: %{late-win} -O2 -stop-before=msil-reg-stackify %{late-check} \
; RUN:   -DLAST='MSIL Optimize Live Intervals' --implicit-check-not='MSIL Register Stackify'
; RUN: %{late-lnx} -O2 -stop-after=msil-reg-coloring %{late-check} \
; RUN:   -DLAST='MSIL Register Coloring'
; RUN: %{late-win} -O2 -stop-after=msil-reg-coloring %{late-check} \
; RUN:   -DLAST='MSIL Register Coloring'
; RUN: %{late-lnx} -O2 -stop-before=msil-reg-coloring %{late-check} \
; RUN:   -DLAST='MSIL Register Stackify' --implicit-check-not='MSIL Register Coloring'
; RUN: %{late-win} -O2 -stop-before=msil-reg-coloring %{late-check} \
; RUN:   -DLAST='MSIL Register Stackify' --implicit-check-not='MSIL Register Coloring'
; RUN: %{late-lnx} -O2 -stop-after=msil-reg-numbering %{late-check} \
; RUN:   -DLAST='MSIL Register Numbering'
; RUN: %{late-win} -O2 -stop-after=msil-reg-numbering %{late-check} \
; RUN:   -DLAST='MSIL Register Numbering'
; RUN: %{late-lnx} -O2 -stop-before=msil-reg-numbering %{late-check} \
; RUN:   -DLAST='MSIL Explicit Locals' --implicit-check-not='MSIL Register Numbering'
; RUN: %{late-win} -O2 -stop-before=msil-reg-numbering %{late-check} \
; RUN:   -DLAST='MSIL Explicit Locals' --implicit-check-not='MSIL Register Numbering'
; RUN: %{late-lnx} -O2 -stop-after=msil-stack-check %{late-check} \
; RUN:   -DLAST='MSIL Stack Check'
; RUN: %{late-win} -O2 -stop-after=msil-stack-check %{late-check} \
; RUN:   -DLAST='MSIL Stack Check'
; RUN: %{late-lnx} -O2 -stop-before=msil-stack-check %{late-check} \
; RUN:   -DLAST='MSIL Register Numbering' --implicit-check-not='MSIL Stack Check'
; RUN: %{late-win} -O2 -stop-before=msil-stack-check %{late-check} \
; RUN:   -DLAST='MSIL Register Numbering' --implicit-check-not='MSIL Stack Check'

; The -O0 pipeline has no msil-optimize-live-intervals or msil-reg-coloring.
; RUN: %{late-lnx} -O0 -stop-after=msil-clean-code-after-trap %{late-check} \
; RUN:   -DLAST='MSIL Clean Code After Trap'
; RUN: %{late-win} -O0 -stop-after=msil-clean-code-after-trap %{late-check} \
; RUN:   -DLAST='MSIL Clean Code After Trap'
; RUN: %{late-lnx} -O0 -stop-before=msil-clean-code-after-trap %{late-check} \
; RUN:   -DLAST='MSIL Set Memory Flags' --implicit-check-not='MSIL Clean Code After Trap'
; RUN: %{late-win} -O0 -stop-before=msil-clean-code-after-trap %{late-check} \
; RUN:   -DLAST='MSIL Set Memory Flags' --implicit-check-not='MSIL Clean Code After Trap'
; RUN: %{late-lnx} -O0 -stop-after=msil-nullify-dbg-value-lists %{late-check} \
; RUN:   -DLAST='MSIL Nullify DBG_VALUE_LISTs'
; RUN: %{late-win} -O0 -stop-after=msil-nullify-dbg-value-lists %{late-check} \
; RUN:   -DLAST='MSIL Nullify DBG_VALUE_LISTs'
; RUN: %{late-lnx} -O0 -stop-before=msil-nullify-dbg-value-lists %{late-check} \
; RUN:   -DLAST='MSIL Clean Code After Trap' --implicit-check-not='MSIL Nullify DBG_VALUE_LISTs'
; RUN: %{late-win} -O0 -stop-before=msil-nullify-dbg-value-lists %{late-check} \
; RUN:   -DLAST='MSIL Clean Code After Trap' --implicit-check-not='MSIL Nullify DBG_VALUE_LISTs'
; RUN: %{late-lnx} -O0 -stop-after=msil-replace-phys-regs %{late-check} \
; RUN:   -DLAST='MSIL Replace Physical Registers'
; RUN: %{late-win} -O0 -stop-after=msil-replace-phys-regs %{late-check} \
; RUN:   -DLAST='MSIL Replace Physical Registers'
; RUN: %{late-lnx} -O0 -stop-before=msil-replace-phys-regs %{late-check} \
; RUN:   -DLAST='MSIL Nullify DBG_VALUE_LISTs' --implicit-check-not='MSIL Replace Physical Registers'
; RUN: %{late-win} -O0 -stop-before=msil-replace-phys-regs %{late-check} \
; RUN:   -DLAST='MSIL Nullify DBG_VALUE_LISTs' --implicit-check-not='MSIL Replace Physical Registers'
; RUN: %{late-lnx} -O0 -stop-after=msil-reg-stackify %{late-check} \
; RUN:   -DLAST='MSIL Register Stackify'
; RUN: %{late-win} -O0 -stop-after=msil-reg-stackify %{late-check} \
; RUN:   -DLAST='MSIL Register Stackify'
; RUN: %{late-lnx} -O0 -stop-before=msil-reg-stackify %{late-check} \
; RUN:   -DLAST='MSIL Replace Physical Registers' --implicit-check-not='MSIL Register Stackify'
; RUN: %{late-win} -O0 -stop-before=msil-reg-stackify %{late-check} \
; RUN:   -DLAST='MSIL Replace Physical Registers' --implicit-check-not='MSIL Register Stackify'
; RUN: %{late-lnx} -O0 -stop-after=msil-reg-numbering %{late-check} \
; RUN:   -DLAST='MSIL Register Numbering'
; RUN: %{late-win} -O0 -stop-after=msil-reg-numbering %{late-check} \
; RUN:   -DLAST='MSIL Register Numbering'
; RUN: %{late-lnx} -O0 -stop-before=msil-reg-numbering %{late-check} \
; RUN:   -DLAST='MSIL Explicit Locals' --implicit-check-not='MSIL Register Numbering'
; RUN: %{late-win} -O0 -stop-before=msil-reg-numbering %{late-check} \
; RUN:   -DLAST='MSIL Explicit Locals' --implicit-check-not='MSIL Register Numbering'
; RUN: %{late-lnx} -O0 -stop-after=msil-stack-check %{late-check} \
; RUN:   -DLAST='MSIL Stack Check'
; RUN: %{late-win} -O0 -stop-after=msil-stack-check %{late-check} \
; RUN:   -DLAST='MSIL Stack Check'
; RUN: %{late-lnx} -O0 -stop-before=msil-stack-check %{late-check} \
; RUN:   -DLAST='MSIL Register Numbering' --implicit-check-not='MSIL Stack Check'
; RUN: %{late-win} -O0 -stop-before=msil-stack-check %{late-check} \
; RUN:   -DLAST='MSIL Register Numbering' --implicit-check-not='MSIL Stack Check'

; LATE:     Pass Arguments:
; LATE:     {{^ +}}[[LAST]]{{$}}
; LATE-NOT: MSIL
; LATE:     {{^ +}}MIR Printing Pass{{$}}
; LATE-NOT: MSIL

; Before the fix ISel printed ARGUMENT_I32 0, CONST_I32 1, AND_I32,
; ARGUMENT_I32 2, ARGUMENT_I32 1 at -O0 and -O2.
; CHECK-LABEL: name: sel{{$}}
; CHECK:       bb.0
; CHECK-NEXT:    liveins: $arguments
; CHECK-NEXT:    {{  $}}
; CHECK-NEXT:    %{{[0-9]+}}:i32 = ARGUMENT_I32 {{[0-2]}}, implicit $arguments
; CHECK-NEXT:    %{{[0-9]+}}:i32 = ARGUMENT_I32 {{[0-2]}}, implicit $arguments
; CHECK-NEXT:    %{{[0-9]+}}:i32 = ARGUMENT_I32 {{[0-2]}}, implicit $arguments
; CHECK-NOT:     ARGUMENT_
; CHECK:         RETURN
; CANARY-LABEL: # Machine code for function sel:
; CANARY:       %{{[0-9]+}}:{{i32|i64}} = {{CONST|AND|CONV}}_
; CANARY:       %{{[0-9]+}}:{{i32|i64}} = ARGUMENT_
; CANARY:       # End machine code for function sel.
define i32 @sel(i1 %c, i32 %a, i32 %b) {
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}

; Before the fix: ARGUMENT_I64 1, CONV_I4_I64, ARGUMENT_I64 0.
; CHECK-LABEL: name: rotr_i64{{$}}
; CHECK:       bb.0
; CHECK-NEXT:    liveins: $arguments
; CHECK-NEXT:    {{  $}}
; CHECK-NEXT:    %{{[0-9]+}}:i64 = ARGUMENT_I64 {{[01]}}, implicit $arguments
; CHECK-NEXT:    %{{[0-9]+}}:i64 = ARGUMENT_I64 {{[01]}}, implicit $arguments
; CHECK-NOT:     ARGUMENT_
; CHECK:         RETURN
; CANARY-LABEL: # Machine code for function rotr_i64:
; CANARY:       %{{[0-9]+}}:{{i32|i64}} = {{CONST|AND|CONV}}_
; CANARY:       %{{[0-9]+}}:{{i32|i64}} = ARGUMENT_
; CANARY:       # End machine code for function rotr_i64.
define i64 @rotr_i64(i64 %a, i64 %b) {
  %r = call i64 @llvm.fshr.i64(i64 %a, i64 %a, i64 %b)
  ret i64 %r
}

declare i64 @llvm.fshr.i64(i64, i64, i64)

; Before the fix: ARGUMENT_I32 2, DBG_VALUE, ARGUMENT_I32 1, DBG_VALUE,
; ARGUMENT_I32 0, DBG_VALUE at -O0 and -O2. Each argument's DBG_VALUE names
; the register that its moved ARGUMENT defines.
; CHECK-LABEL: name: sel_dbg{{$}}
; CHECK:       bb.0
; CHECK-NEXT:    liveins: $arguments
; CHECK-NEXT:    {{  $}}
; CHECK-NEXT:    %[[REG1:[0-9]+]]:i32 = ARGUMENT_I32 {{[0-2]}}, implicit $arguments
; CHECK-NEXT:    %[[REG2:[0-9]+]]:i32 = ARGUMENT_I32 {{[0-2]}}, implicit $arguments
; CHECK-NEXT:    %[[REG3:[0-9]+]]:i32 = ARGUMENT_I32 {{[0-2]}}, implicit $arguments
; CHECK-NOT:     ARGUMENT_
; CHECK-DAG:     DBG_VALUE %[[REG1]], $noreg, !{{[0-9]+}}, !DIExpression(
; CHECK-DAG:     DBG_VALUE %[[REG2]], $noreg, !{{[0-9]+}}, !DIExpression(
; CHECK-DAG:     DBG_VALUE %[[REG3]], $noreg, !{{[0-9]+}}, !DIExpression(
; CHECK-NOT:     ARGUMENT_
; CHECK:         RETURN
; CANARY-LABEL: # Machine code for function sel_dbg:
; CANARY:       DBG_VALUE %{{[0-9]+}}:i32,
; CANARY:       %{{[0-9]+}}:i32 = ARGUMENT_
; CANARY:       # End machine code for function sel_dbg.
define noundef i32 @sel_dbg(i1 noundef zeroext %c, i32 noundef %a, i32 noundef %b) local_unnamed_addr #0 !dbg !14 {
entry:
    #dbg_value(i1 %c, !20, !DIExpression(DW_OP_LLVM_convert, 1, DW_ATE_unsigned, DW_OP_LLVM_convert, 8, DW_ATE_unsigned, DW_OP_stack_value), !23)
    #dbg_value(i32 %a, !21, !DIExpression(), !23)
    #dbg_value(i32 %b, !22, !DIExpression(), !23)
  %cond = select i1 %c, i32 %a, i32 %b, !dbg !24
  ret i32 %cond, !dbg !25
}

; Before the fix: ARGUMENT_I64 1, DBG_VALUE, CONV_I4_I64, ARGUMENT_I64 0,
; DBG_VALUE at -O0 and -O2; the verifier rejected the ARGUMENT after
; CONV_I4_I64. Each argument's DBG_VALUE stays where instruction selection put
; it, one before and one after CONV_I4_I64, so the CHECK-DAG lines do not pin
; their order.
; CHECK-LABEL: name: rotr_dbg{{$}}
; CHECK:       bb.0
; CHECK-NEXT:    liveins: $arguments
; CHECK-NEXT:    {{  $}}
; CHECK-NEXT:    %[[REG1:[0-9]+]]:i64 = ARGUMENT_I64 {{[01]}}, implicit $arguments
; CHECK-NEXT:    %[[REG2:[0-9]+]]:i64 = ARGUMENT_I64 {{[01]}}, implicit $arguments
; CHECK-NOT:     ARGUMENT_
; CHECK-DAG:     %{{[0-9]+}}:i32 = CONV_I4_I64 %{{[0-9]+}}, implicit-def dead $arguments
; CHECK-DAG:     DBG_VALUE %[[REG1]], $noreg, !{{[0-9]+}}, !DIExpression(
; CHECK-DAG:     DBG_VALUE %[[REG2]], $noreg, !{{[0-9]+}}, !DIExpression(
; CHECK-NOT:     ARGUMENT_
; CHECK:         RETURN
; CANARY-LABEL: # Machine code for function rotr_dbg:
; CANARY:       %{{[0-9]+}}:{{i32|i64}} = {{CONST|AND|CONV}}_
; CANARY:       %{{[0-9]+}}:{{i32|i64}} = ARGUMENT_
; CANARY:       # End machine code for function rotr_dbg.
define noundef i64 @rotr_dbg(i64 noundef %a, i64 noundef %b) local_unnamed_addr #0 !dbg !26 {
entry:
    #dbg_value(i64 %a, !31, !DIExpression(), !33)
    #dbg_value(i64 %b, !32, !DIExpression(), !33)
  %or = tail call i64 @llvm.fshr.i64(i64 %a, i64 %a, i64 %b), !dbg !34
  ret i64 %or, !dbg !35
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3, !4, !5, !6, !7}
!llvm.ident = !{!8}
!llvm.errno.tbaa = !{!9}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang version 24.0.0git", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "dbg.c", directory: "/tmp", checksumkind: CSK_MD5, checksum: "0a3d503cd1624898e3c4de8b49987121")
!2 = !{i32 7, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = !{i32 8, !"PIC Level", i32 2}
!5 = !{i32 7, !"PIE Level", i32 2}
!6 = !{i32 7, !"uwtable", i32 2}
!7 = !{i32 7, !"debug-info-assignment-tracking", i1 true}
!8 = !{!"clang version 24.0.0git"}
!9 = !{!10, !11, i64 0}
!10 = !{!"__libc_errno", !11, i64 0}
!11 = !{!"int", !12, i64 0}
!12 = !{!"omnipotent char", !13, i64 0}
!13 = !{!"Simple C/C++ TBAA"}
!14 = distinct !DISubprogram(name: "sel", scope: !1, file: !1, line: 1, type: !15, scopeLine: 1, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !19, keyInstructions: true)
!15 = !DISubroutineType(types: !16)
!16 = !{!17, !18, !17, !17}
!17 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!18 = !DIBasicType(name: "_Bool", size: 8, encoding: DW_ATE_boolean)
!19 = !{!20, !21, !22}
!20 = !DILocalVariable(name: "c", arg: 1, scope: !14, file: !1, line: 1, type: !18)
!21 = !DILocalVariable(name: "a", arg: 2, scope: !14, file: !1, line: 1, type: !17)
!22 = !DILocalVariable(name: "b", arg: 3, scope: !14, file: !1, line: 1, type: !17)
!23 = !DILocation(line: 0, scope: !14)
!24 = !DILocation(line: 1, column: 41, scope: !14, atomGroup: 2, atomRank: 1)
!25 = !DILocation(line: 1, column: 34, scope: !14, atomGroup: 1, atomRank: 1)
!26 = distinct !DISubprogram(name: "rotr", scope: !1, file: !1, line: 2, type: !27, scopeLine: 2, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !30, keyInstructions: true)
!27 = !DISubroutineType(types: !28)
!28 = !{!29, !29, !29}
!29 = !DIBasicType(name: "unsigned long long", size: 64, encoding: DW_ATE_unsigned)
!30 = !{!31, !32}
!31 = !DILocalVariable(name: "a", arg: 1, scope: !26, file: !1, line: 2, type: !29)
!32 = !DILocalVariable(name: "b", arg: 2, scope: !26, file: !1, line: 2, type: !29)
!33 = !DILocation(line: 0, scope: !26)
!34 = !DILocation(line: 2, column: 94, scope: !26, atomGroup: 1, atomRank: 2)
!35 = !DILocation(line: 2, column: 71, scope: !26, atomGroup: 1, atomRank: 1)
