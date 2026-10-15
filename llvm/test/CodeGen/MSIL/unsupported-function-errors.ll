; User diagnostics for function definitions that M3 cannot compile
; (ImplementationPlan.md section 2, 'Diagnostics leave well-formed output';
; M3.c, M3.d): inalloca, preallocated and nest parameters; calling conventions
; other than C, fast, cold, preserve_most and preserve_all; naked functions;
; and methods with more than 65535 arguments or locals. Since M9 a byval
; parameter is the address of the caller's copy and is not diagnosed
; (byval.ll). Each problem is reported once, llc exits with 1 without
; 'LLVM ERROR' or a stack dump, and compilation goes on: a diagnosed parameter
; is lowered as the plain pointer it is ('native int', decision D2), a function
; with an unsupported convention is compiled like a C function, a naked
; function keeps the method header of its IR type and its unreachable body
; traps, and the well-formed function after them compiles as usual. llc deletes
; its -o file after an error, so the modules that are checked and assembled are
; taken from stdout.

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: split-file %s %t

; inalloca, preallocated and nest parameters (and undiagnosed byval and sret
; ones).
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/args.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ARGS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/args.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ARGS
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/args.ll > %t/args.lnx.il
; RUN: FileCheck %s --check-prefix=ARGS-IL --input-file=%t/args.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/args.ll > %t/args.win.il
; RUN: FileCheck %s --check-prefix=ARGS-IL --input-file=%t/args.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/args.dll %t/args.win.il %}

; Calling conventions.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/cc.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=CC
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/cc.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=CC
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/cc.ll > %t/cc.lnx.il
; RUN: FileCheck %s --check-prefix=CC-IL --input-file=%t/cc.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/cc.ll > %t/cc.win.il
; RUN: FileCheck %s --check-prefix=CC-IL --input-file=%t/cc.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/cc.dll %t/cc.win.il %}

; Naked functions.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/naked.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=NAKED
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/naked.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=NAKED
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/naked.ll > %t/naked.lnx.il
; RUN: FileCheck %s --check-prefix=NAKED-IL --input-file=%t/naked.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/naked.ll > %t/naked.win.il
; RUN: FileCheck %s --check-prefix=NAKED-IL --input-file=%t/naked.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/naked.dll %t/naked.lnx.il %}

; ldarg, starg, ldloc and stloc take 16-bit indices, and the backend keeps clear
; of index 0xFFFF, so a CIL method has at most 65535 arguments (indices 0 to
; 65534) and 65535 locals (indices 0 to 65534): a function with 65536
; parameters or 65536 locals is an error, and one with 65535 compiles silently.
; gen.py prints a function '@params' with N i32 parameters that returns the last
; one, followed by the well-formed '@ok'.
; RUN: %python %t/gen.py 65536 > %t/over.ll
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/over.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=OVER
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/over.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=OVER
; RUN: %python %t/gen.py 65535 > %t/limit.ll
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/limit.ll \
; RUN:   -o %t/limit.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=LIMIT --input-file=%t/limit.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/limit.ll \
; RUN:   -o %t/limit.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=LIMIT --input-file=%t/limit.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/limit.dll %t/limit.win.il %}

; genlocals.py prints MIR for a function '@locals' whose body is a chain of N
; i32 values, a constant and N - 1 copies of the value before, and returns the
; last one. llc starts at MSILExplicitLocals, after MSILRegStackify and
; MSILRegColoring, so no value is stackified or shares a local with another,
; and every value gets a CIL local of its own (as in explicit-locals.mir): the
; method has N locals. The IR section gives the method header its signature.
; RUN: %python %t/genlocals.py 65536 > %t/locals-over.mir
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -start-before=msil-explicit-locals -verify-machineinstrs \
; RUN:   %t/locals-over.mir -o /dev/null 2>&1 | %{diag} --check-prefix=LOCALS-OVER
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -start-before=msil-explicit-locals -verify-machineinstrs \
; RUN:   %t/locals-over.mir -o /dev/null 2>&1 | %{diag} --check-prefix=LOCALS-OVER
; RUN: %python %t/genlocals.py 65535 > %t/locals-limit.mir
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -start-before=msil-explicit-locals -asm-verbose=false \
; RUN:   -verify-machineinstrs %t/locals-limit.mir -o %t/locals-limit.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=LOCALS-LIMIT --input-file=%t/locals-limit.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -start-before=msil-explicit-locals -asm-verbose=false \
; RUN:   -verify-machineinstrs %t/locals-limit.mir -o %t/locals-limit.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=LOCALS-LIMIT --input-file=%t/locals-limit.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/locals-limit.dll %t/locals-limit.win.il %}

; The error names the function and prints its whole IR type.
; OVER:      error: {{.*}}in function params i32 (i32, i32, i32,
; OVER-SAME: , i32, i32): a CIL method has at most 65535 arguments and 65535 locals

; The last of 65535 arguments is argument 65534, read with the long form.
; LIMIT-LABEL: .method {{.*}}'params'(
; LIMIT-SAME:  int32, int32) cil managed{{$}}
; LIMIT-NEXT:  {{^\{$}}
; LIMIT-NEXT:  {{^}}  .maxstack 1{{$}}
; LIMIT-NEXT:  {{^}}  ldarg 65534{{$}}
; LIMIT-NEXT:  {{^}}  ret{{$}}
; LIMIT-LABEL: .method {{.*}}'ok'(
; LIMIT-NEXT:  {{^\{$}}
; LIMIT-NEXT:  {{^}}  .maxstack 1{{$}}
; LIMIT-NEXT:  {{^}}  ldc.i4.3{{$}}
; LIMIT-NEXT:  {{^}}  ret{{$}}

; LOCALS-OVER: error: {{.*}}in function locals i32 (): a CIL method has at most 65535 arguments and 65535 locals{{$}}

; 65535 int32 locals: value k is stored to local k and read back for the next
; copy, so the last value is local 65534, read with the long forms. (A regular
; expression cannot count 65535 types: its repeat counts stop at 255.)
; LOCALS-LIMIT-LABEL: {{^}}.method public static int32 'locals'() cil managed{{$}}
; LOCALS-LIMIT-NEXT:  {{^\{$}}
; LOCALS-LIMIT-NEXT:  {{^}}  .maxstack 1{{$}}
; LOCALS-LIMIT-NEXT:  {{^}}  .locals (int32{{(, int32)+}}){{$}}
; LOCALS-LIMIT-NEXT:  {{^}}  ldc.i4.0{{$}}
; LOCALS-LIMIT-NEXT:  {{^}}  stloc.0{{$}}
; LOCALS-LIMIT-NEXT:  {{^}}  ldloc.0{{$}}
; LOCALS-LIMIT-NEXT:  {{^}}  stloc.1{{$}}
; LOCALS-LIMIT:       {{^}}  ldloc 65533{{$}}
; LOCALS-LIMIT-NEXT:  {{^}}  stloc 65534{{$}}
; LOCALS-LIMIT-NEXT:  {{^}}  ldloc 65534{{$}}
; LOCALS-LIMIT-NEXT:  {{^}}  ret{{$}}
; LOCALS-LIMIT-NOT:   {{^ *[^/ ]}}
; LOCALS-LIMIT:       {{^}}} // end of method 'locals'{{$}}

;--- args.ll
; Each diagnosed parameter (inalloca, preallocated, nest) is reported once and
; becomes a 'native int' parameter that is read with ldarg like any pointer.
; Since M9 @byval and the byval parameter of @mixed are not diagnosed: the
; caller copies a byval argument (M9.a), and the parameter stays a plain
; 'native int', the address of that copy. SelectionDAG also sets the byval flag
; on inalloca and preallocated arguments, which keep their own errors and are
; not accepted as byval ones; %{diag} has --implicit-check-not=error:, so no
; other error may appear. sret is an ordinary pointer parameter and is not
; diagnosed either.

%struct.S = type { i32, i32 }

; ARGS-IL-LABEL: {{^}}.method public static native int 'byval'(native int) cil managed{{$}}
; ARGS-IL-NEXT:  {{^\{$}}
; ARGS-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; ARGS-IL-NEXT:  {{^}}  ldarg.0{{$}}
; ARGS-IL-NEXT:  {{^}}  ret{{$}}
define ptr @byval(ptr byval(%struct.S) %p) {
  ret ptr %p
}

; ARGS: error: {{.*}}in function inalloca ptr (i32, ptr): inalloca arguments are not supported for MSIL{{$}}
; ARGS-IL-LABEL: {{^}}.method public static native int 'inalloca'(int32, native int) cil managed{{$}}
; ARGS-IL-NEXT:  {{^\{$}}
; ARGS-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; ARGS-IL-NEXT:  {{^}}  ldarg.1{{$}}
; ARGS-IL-NEXT:  {{^}}  ret{{$}}
define ptr @inalloca(i32 %x, ptr inalloca(i32) %p) {
  ret ptr %p
}

; ARGS: error: {{.*}}in function preallocated ptr (ptr, i32): preallocated arguments are not supported for MSIL{{$}}
; ARGS-IL-LABEL: {{^}}.method public static native int 'preallocated'(native int, int32) cil managed{{$}}
; ARGS-IL-NEXT:  {{^\{$}}
; ARGS-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; ARGS-IL-NEXT:  {{^}}  ldarg.0{{$}}
; ARGS-IL-NEXT:  {{^}}  ret{{$}}
define ptr @preallocated(ptr preallocated(%struct.S) %p, i32 %x) {
  ret ptr %p
}

; ARGS: error: {{.*}}in function nest ptr (i32, ptr): nest arguments are not supported for MSIL{{$}}
; ARGS-IL-LABEL: {{^}}.method public static native int 'nest'(int32, native int) cil managed{{$}}
; ARGS-IL-NEXT:  {{^\{$}}
; ARGS-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; ARGS-IL-NEXT:  {{^}}  ldarg.1{{$}}
; ARGS-IL-NEXT:  {{^}}  ret{{$}}
define ptr @nest(i32 %x, ptr nest %n) {
  ret ptr %n
}

; One diagnosed (inalloca) parameter after an undiagnosed byval one, around an
; ordinary one.
; ARGS: error: {{.*}}in function mixed i32 (ptr, i32, ptr): inalloca arguments are not supported for MSIL{{$}}
; ARGS-IL-LABEL: {{^}}.method public static int32 'mixed'(native int, int32, native int) cil managed{{$}}
; ARGS-IL-NEXT:  {{^\{$}}
; ARGS-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; ARGS-IL-NEXT:  {{^}}  ldarg.1{{$}}
; ARGS-IL-NEXT:  {{^}}  ret{{$}}
define i32 @mixed(ptr byval(%struct.S) %p, i32 %x, ptr inalloca(i32) %q) {
  ret i32 %x
}

; ARGS-IL-LABEL: {{^}}.method public static void 'sret'(native int) cil managed{{$}}
; ARGS-IL-NEXT:  {{^\{$}}
; ARGS-IL-NEXT:  {{^}}  .maxstack 0{{$}}
; ARGS-IL-NEXT:  {{^}}  ret{{$}}
define void @sret(ptr sret(%struct.S) %r) {
  ret void
}

; ARGS-IL-LABEL: {{^}}.method public static int32 'ok'() cil managed{{$}}
; ARGS-IL-NEXT:  {{^\{$}}
; ARGS-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; ARGS-IL-NEXT:  {{^}}  ldc.i4.3{{$}}
; ARGS-IL-NEXT:  {{^}}  ret{{$}}
define i32 @ok() {
  ret i32 3
}

;--- cc.ll
; clang emits x86_vectorcallcc for __vectorcall on both triples, win64cc for
; __attribute__((ms_abi)) only on the Linux triple and x86_64_sysvcc for
; __attribute__((sysv_abi)) only on the Windows triple (on the other triple
; each is the default C convention, so the IR names no convention). A
; convention named in the IR is rejected on every triple, so both triples share
; the checks. The diagnostic comes once per definition, and the body is
; compiled as for a C function. fastcc, coldcc, preserve_mostcc and
; preserve_allcc are accepted: every call is a managed call with the arguments
; on the evaluation stack.

; CC: error: {{.*}}in function vectorcall i32 (i32): calling convention is not supported for MSIL{{$}}
; CC-IL-LABEL: {{^}}.method public static int32 'vectorcall'(int32) cil managed{{$}}
; CC-IL-NEXT:  {{^\{$}}
; CC-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; CC-IL-NEXT:  {{^}}  ldarg.0{{$}}
; CC-IL-NEXT:  {{^}}  ret{{$}}
define x86_vectorcallcc i32 @vectorcall(i32 %a) {
  ret i32 %a
}

; CC: error: {{.*}}in function ghc void (): calling convention is not supported for MSIL{{$}}
; CC-IL-LABEL: {{^}}.method public static void 'ghc'() cil managed{{$}}
; CC-IL-NEXT:  {{^\{$}}
; CC-IL-NEXT:  {{^}}  .maxstack 0{{$}}
; CC-IL-NEXT:  {{^}}  ret{{$}}
define ghccc void @ghc() {
  ret void
}

; CC: error: {{.*}}in function ms_abi i32 (i32): calling convention is not supported for MSIL{{$}}
; CC-IL-LABEL: {{^}}.method public static int32 'ms_abi'(int32) cil managed{{$}}
; CC-IL-NEXT:  {{^\{$}}
; CC-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; CC-IL-NEXT:  {{^}}  ldarg.0{{$}}
; CC-IL-NEXT:  {{^}}  ret{{$}}
define win64cc i32 @ms_abi(i32 %a) {
  ret i32 %a
}

; CC: error: {{.*}}in function sysv_abi i32 (i32): calling convention is not supported for MSIL{{$}}
; CC-IL-LABEL: {{^}}.method public static int32 'sysv_abi'(int32) cil managed{{$}}
; CC-IL-NEXT:  {{^\{$}}
; CC-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; CC-IL-NEXT:  {{^}}  ldarg.0{{$}}
; CC-IL-NEXT:  {{^}}  ret{{$}}
define x86_64_sysvcc i32 @sysv_abi(i32 %a) {
  ret i32 %a
}

; CC-IL-LABEL: {{^}}.method public static int32 'fast'(int32) cil managed{{$}}
; CC-IL-NEXT:  {{^\{$}}
; CC-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; CC-IL-NEXT:  {{^}}  ldarg.0{{$}}
; CC-IL-NEXT:  {{^}}  ret{{$}}
define fastcc i32 @fast(i32 %a) {
  ret i32 %a
}

; CC-IL-LABEL: {{^}}.method public static int32 'cold'(int32) cil managed{{$}}
define coldcc i32 @cold(i32 %a) {
  ret i32 %a
}

; CC-IL-LABEL: {{^}}.method public static int32 'preserve_most'(int32) cil managed{{$}}
define preserve_mostcc i32 @preserve_most(i32 %a) {
  ret i32 %a
}

; CC-IL-LABEL: {{^}}.method public static int32 'preserve_all'(int32) cil managed{{$}}
define preserve_allcc i32 @preserve_all(i32 %a) {
  ret i32 %a
}

; CC-IL-LABEL: {{^}}.method public static int32 'ok'() cil managed{{$}}
; CC-IL-NEXT:  {{^\{$}}
; CC-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; CC-IL-NEXT:  {{^}}  ldc.i4.3{{$}}
; CC-IL-NEXT:  {{^}}  ret{{$}}
define i32 @ok() {
  ret i32 3
}

;--- naked.ll
; clang's IR for '__attribute__((naked)) int naked(int a, char *p) {}' (with
; inline assembly in the body, clang itself reports the asm statement). A naked
; function only makes sense with inline assembly, so it is an error. Its
; header still has the parameters of its IR type, which callers use, and its
; body, 'unreachable', traps with ldnull/throw (TrapUnreachable).

; NAKED: error: {{.*}}in function naked i32 (i32, ptr): naked functions are not supported for MSIL{{$}}
; NAKED-IL-LABEL: {{^}}.method public static int32 'naked'(int32, native int) cil managed{{$}}
; NAKED-IL-NEXT:  {{^\{$}}
; NAKED-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; NAKED-IL-NEXT:  {{^}}  ldnull{{$}}
; NAKED-IL-NEXT:  {{^}}  throw{{$}}
; NAKED-IL-NOT:   {{^ *[^/ ]}}
; NAKED-IL:       {{^}}} // end of method 'naked'{{$}}
define i32 @naked(i32 noundef %0, ptr noundef %1) #0 {
entry:
  unreachable
}

; NAKED: error: {{.*}}in function naked_void void (): naked functions are not supported for MSIL{{$}}
; NAKED-IL-LABEL: {{^}}.method public static void 'naked_void'() cil managed{{$}}
; NAKED-IL-NEXT:  {{^\{$}}
; NAKED-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; NAKED-IL-NEXT:  {{^}}  ldnull{{$}}
; NAKED-IL-NEXT:  {{^}}  throw{{$}}
; NAKED-IL-NOT:   {{^ *[^/ ]}}
; NAKED-IL:       {{^}}} // end of method 'naked_void'{{$}}
define void @naked_void() #0 {
entry:
  unreachable
}

; NAKED-IL-LABEL: {{^}}.method public static int32 'ok'() cil managed{{$}}
; NAKED-IL-NEXT:  {{^\{$}}
; NAKED-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; NAKED-IL-NEXT:  {{^}}  ldc.i4.3{{$}}
; NAKED-IL-NEXT:  {{^}}  ret{{$}}
define i32 @ok() {
  ret i32 3
}

attributes #0 = { naked noinline nounwind optnone }

;--- gen.py
import sys

n = int(sys.argv[1])
params = ["i32"] * (n - 1) + ["i32 %last"]
print("define i32 @params(" + ", ".join(params) + ") {")
print("  ret i32 %last")
print("}")
print()
print("define i32 @ok() {")
print("  ret i32 3")
print("}")

;--- genlocals.py
import sys

n = int(sys.argv[1])
print("--- |")
print("  define i32 @locals() {")
print("    ret i32 0")
print("  }")
print("...")
print("---")
print("name:            locals")
print("tracksRegLiveness: true")
print("machineFunctionInfo:")
print("  params:  [ ]")
print("  results: [ i32 ]")
print("body: |")
print("  bb.0:")
print("    %0:i32 = CONST_I32 0, implicit-def dead $arguments")
for k in range(1, n):
    print("    %%%d:i32 = COPY_I32 %%%d, implicit-def dead $arguments" % (k, k - 1))
print("    RETURN %%%d, implicit-def dead $arguments" % (n - 1))
print("...")
