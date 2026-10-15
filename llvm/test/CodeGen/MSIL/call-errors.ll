; Calls that MSIL cannot compile are clean errors (ImplementationPlan.md
; section 2 'Diagnostics leave well-formed output', M4.a, M4.c, M8.a, M8.c;
; decisions D14, D19; section 12 rows 'M4.a extended', 'Plan 5.3
; (MSILISelLowering.h) extended, M4.b', the two M4 fix-loop ownership rows of
; fix-1, the two M5 rows of fix-3, the M8 rows and the M9 row for this file).
; Each one is reported exactly once, naming the caller (%{diag} rejects every
; other 'error:' and 'warning:' line, so each expected error is checked; the
; errors of STRUCTMM and the end-of-module errors of EXTMULTI are matched as a
; group), compilation goes on, llc exits with 1 and prints neither
; 'LLVM ERROR' nor a crash report, no call is built for the rejected call (its
; result is undefined, so the call disappears from the IL), and the IL on
; stdout (llc deletes its -o file after an error) still assembles. Every part
; runs on both triples at -O2 and at least once at -O0 (the parts with
; two-value results on both triples).
;
; - LowerCall rejects a call of a variadic function ('calls to variadic
;   functions are not supported for MSIL yet', D19; varargs-call-error.ll has
;   more, and K&R calls are fixed-arity, function-bitcasts.ll), a direct call
;   whose call-site type still differs from the callee's after
;   MSILFixFunctionBitcasts ('mismatched call signature'; since M9 only
;   struct-typed mismatches, which the copied pass does not wrap; the others
;   call wrappers, function-bitcasts.ll), a nest or inalloca argument (byval
;   arguments are copied since M9, byval.ll), a musttail call, an unsupported
;   calling convention, a callee named 'llvm.*' that LLVM does not know
;   ("unknown intrinsic '<name>' is not supported for MSIL") and an
;   ExternalSymbol callee that is a runtime helper, whose name starts with
;   '__' ("runtime helper '<name>' is not available for MSIL yet (phase 3)",
;   D14 and M8.a; here the i128 to floating-point conversions __floattidf and
;   __floatuntisf, which return one value; libcall-policy.ll has the library
;   calls that are imports).
; - i128 division and remainder never reach LowerCall: the type legalizer
;   (ReplaceNodeResults) reports the same runtime helper text before any call
;   is built, because their libcalls return two values. Only a variable
;   divisor, or a non-exact division or a remainder by a constant that is not a
;   power of two, gets there; division and remainder by a power of two and
;   exact division by any constant expand inline (i128.ll).
; - A rejected call whose result needs more than one value (i128,
;   { i64, i64 }) is demoted to a frame slot around LowerCall (frames exist
;   since M7) and reported once. Since M9, MSILFixFunctionBitcasts erases
;   nothing (M4's copy erased these calls before ISel): LowerCall reports the
;   call once and builds no call node, and the demoted result is read from
;   its frame slot, so the caller has a static frame (a localloc). The IL
;   checks therefore reject a call or calli only at the start of an
;   instruction ('{{^}}  call'), which the 'call' inside 'localloc' does not
;   match.
; - A call of a definition whose body MSILPrepareIR deleted after a diagnostic
;   (here inline assembly) is erased too, so the only error is the real one.
;   The same holds for invokes, which share a landing pad with a PHI.
; - Aliases and ifuncs are errors of their own (D26); their callers compile,
;   with an undefined result, and add no error, also when the call-site
;   result needs two values.
; - A call of a function that is only declared is built. If the function is
;   not a Phase-1 import (D14; pinvoke.ll has the imports), the end of the
;   module reports "undefined function '<name>' is not defined in this module
;   and is not a Phase-1 import (puts, putchar, the C math library,
;   msil-import-module); Phase 1 has no linker" (M8.c) once per callee,
;   however many calls and callers it has, and declares no P/Invoke for it;
;   an available_externally definition is called like a declaration. A
;   declaration that is never called, or whose calls were all rejected, is
;   not an error.
; - A call of a declared function whose result needs more than one value
;   ({ double, double } from __muldc3 and <2 x float> from __divsc3, which
;   clang calls for C complex multiplication and division on
;   x86_64-unknown-linux-gnu-msil; i128; { i64, i64 }) has its result demoted
;   to a frame slot (frames exist since M7). None of these callees is an
;   import, so each is reported once with the same text (since M9
;   MSILFixFunctionBitcasts erases none of their calls): the errors are
;   matched as a group and counted, and only the IL of the other functions is
;   checked.
; - A byval argument of an import is copied like any byval argument (since
;   M9, byval.ll), and the import, whose aggregate parameter a P/Invoke cannot
;   pass, is reported once at the end of the module with M8.c's "imported
;   function '<name>' has a parameter or result that a P/Invoke cannot pass
;   natively (...)" and gets no P/Invoke declaration.
;
; Later owners (section 12): M8.d updated the EXTERN, AVAIL, EXTMULTI, LIBCALL,
; LIBCALLFP and DIAGCALLEE parts for the M8 texts; M9.c retired the M4 parts
; that M9 makes valid (mismatched scalar call types: wrappers in
; function-bitcasts.ll; byval at a call site: byval.ll), kept LowerCall's
; mismatch check as a backstop with struct-typed mismatches (STRUCTMM) and
; added the byval call of an import (BYVALIMP).

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: split-file %s %t

; Variadic callee.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/variadic.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VARIADIC
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/variadic.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VARIADIC
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/variadic.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VARIADIC
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/variadic.ll \
; RUN:   > %t/variadic.lnx.il
; RUN: FileCheck %s --check-prefix=VARIADIC-IL --input-file=%t/variadic.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/variadic.ll \
; RUN:   > %t/variadic.win.il
; RUN: FileCheck %s --check-prefix=VARIADIC-IL --input-file=%t/variadic.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/variadic.dll %t/variadic.win.il %}

; Variadic callees whose result needs two values.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/variadic-multi.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VMULTI
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/variadic-multi.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VMULTI
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/variadic-multi.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VMULTI
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/variadic-multi.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VMULTI
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/variadic-multi.ll \
; RUN:   > %t/variadic-multi.lnx.il
; RUN: FileCheck %s --check-prefix=VMULTI-IL --input-file=%t/variadic-multi.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/variadic-multi.ll \
; RUN:   > %t/variadic-multi.win.il
; RUN: FileCheck %s --check-prefix=VMULTI-IL --input-file=%t/variadic-multi.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/variadic-multi.dll %t/variadic-multi.win.il %}

; Runtime library calls.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/libcall.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LIBCALL
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/libcall.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LIBCALL
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/libcall.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LIBCALL
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/libcall.ll \
; RUN:   > %t/libcall.lnx.il
; RUN: FileCheck %s --check-prefix=LIBCALL-IL --input-file=%t/libcall.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/libcall.ll \
; RUN:   > %t/libcall.win.il
; RUN: FileCheck %s --check-prefix=LIBCALL-IL --input-file=%t/libcall.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/libcall.dll %t/libcall.lnx.il %}

; Runtime library calls that LowerCall sees as ExternalSymbol callees.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/libcall-fp.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LIBCALLFP
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/libcall-fp.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LIBCALLFP
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/libcall-fp.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LIBCALLFP
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/libcall-fp.ll \
; RUN:   > %t/libcall-fp.lnx.il
; RUN: FileCheck %s --check-prefix=LIBCALLFP-IL --input-file=%t/libcall-fp.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/libcall-fp.ll \
; RUN:   > %t/libcall-fp.win.il
; RUN: FileCheck %s --check-prefix=LIBCALLFP-IL --input-file=%t/libcall-fp.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/libcall-fp.dll %t/libcall-fp.win.il %}

; Call-site types that differ from the callee's in a struct type, which
; MSILFixFunctionBitcasts does not wrap (one of them with a two-value result).
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/struct-mismatch.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=STRUCTMM
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/struct-mismatch.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=STRUCTMM
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/struct-mismatch.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=STRUCTMM
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/struct-mismatch.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=STRUCTMM
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/struct-mismatch.ll \
; RUN:   > %t/struct-mismatch.lnx.il
; RUN: FileCheck %s --check-prefix=STRUCTMM-IL --input-file=%t/struct-mismatch.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/struct-mismatch.ll \
; RUN:   > %t/struct-mismatch.win.il
; RUN: FileCheck %s --check-prefix=STRUCTMM-IL --input-file=%t/struct-mismatch.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/struct-mismatch.dll %t/struct-mismatch.win.il %}

; nest and inalloca arguments.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/arg-attrs.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ARGATTR
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/arg-attrs.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ARGATTR
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/arg-attrs.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ARGATTR
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/arg-attrs.ll \
; RUN:   > %t/arg-attrs.lnx.il
; RUN: FileCheck %s --check-prefix=ARGATTR-IL --input-file=%t/arg-attrs.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/arg-attrs.ll \
; RUN:   > %t/arg-attrs.win.il
; RUN: FileCheck %s --check-prefix=ARGATTR-IL --input-file=%t/arg-attrs.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/arg-attrs.dll %t/arg-attrs.win.il %}

; musttail call.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/musttail.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MUSTTAIL
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/musttail.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MUSTTAIL
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/musttail.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MUSTTAIL
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/musttail.ll \
; RUN:   > %t/musttail.lnx.il
; RUN: FileCheck %s --check-prefix=MUSTTAIL-IL --input-file=%t/musttail.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/musttail.ll \
; RUN:   > %t/musttail.win.il
; RUN: FileCheck %s --check-prefix=MUSTTAIL-IL --input-file=%t/musttail.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/musttail.dll %t/musttail.win.il %}

; Unsupported calling conventions at the call site.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/callconv.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=CALLCONV
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/callconv.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=CALLCONV
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/callconv.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=CALLCONV
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/callconv.ll \
; RUN:   > %t/callconv.lnx.il
; RUN: FileCheck %s --check-prefix=CALLCONV-IL --input-file=%t/callconv.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/callconv.ll \
; RUN:   > %t/callconv.win.il
; RUN: FileCheck %s --check-prefix=CALLCONV-IL --input-file=%t/callconv.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/callconv.dll %t/callconv.win.il %}

; Unknown 'llvm.*' callee.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/unknown-intrinsic.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=UNKINTR
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/unknown-intrinsic.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=UNKINTR
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/unknown-intrinsic.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=UNKINTR
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/unknown-intrinsic.ll \
; RUN:   > %t/unknown-intrinsic.lnx.il
; RUN: FileCheck %s --check-prefix=UNKINTR-IL --input-file=%t/unknown-intrinsic.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/unknown-intrinsic.ll \
; RUN:   > %t/unknown-intrinsic.win.il
; RUN: FileCheck %s --check-prefix=UNKINTR-IL --input-file=%t/unknown-intrinsic.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/unknown-intrinsic.dll %t/unknown-intrinsic.win.il %}

; Callee whose body MSILPrepareIR deleted.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/diagnosed-callee.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DIAGCALLEE
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/diagnosed-callee.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DIAGCALLEE
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/diagnosed-callee.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DIAGCALLEE
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/diagnosed-callee.ll \
; RUN:   > %t/diagnosed-callee.lnx.il
; RUN: FileCheck %s --check-prefix=DIAGCALLEE-IL --input-file=%t/diagnosed-callee.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/diagnosed-callee.ll \
; RUN:   > %t/diagnosed-callee.win.il
; RUN: FileCheck %s --check-prefix=DIAGCALLEE-IL --input-file=%t/diagnosed-callee.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/diagnosed-callee.dll %t/diagnosed-callee.win.il %}

; Invokes of a variadic callee and of a callee whose body MSILPrepareIR deleted.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/invoke.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=INVOKE
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/invoke.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=INVOKE
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/invoke.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=INVOKE
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/invoke.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=INVOKE
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/invoke.ll \
; RUN:   > %t/invoke.lnx.il
; RUN: FileCheck %s --check-prefix=INVOKE-IL --input-file=%t/invoke.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/invoke.ll \
; RUN:   > %t/invoke.win.il
; RUN: FileCheck %s --check-prefix=INVOKE-IL --input-file=%t/invoke.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/invoke.dll %t/invoke.win.il %}

; Callers of a diagnosed alias and ifunc.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/alias-callers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ALIAS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/alias-callers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ALIAS
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/alias-callers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ALIAS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/alias-callers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ALIAS
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/alias-callers.ll \
; RUN:   > %t/alias-callers.lnx.il
; RUN: FileCheck %s --check-prefix=ALIAS-IL --input-file=%t/alias-callers.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/alias-callers.ll \
; RUN:   > %t/alias-callers.win.il
; RUN: FileCheck %s --check-prefix=ALIAS-IL --input-file=%t/alias-callers.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/alias-callers.dll %t/alias-callers.lnx.il %}

; Callee that is only declared and is not a Phase-1 import.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/extern.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=EXTERN
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/extern.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=EXTERN
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/extern.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=EXTERN

; Several calls of an available_externally definition and of a declaration,
; from two functions (the calls are built, so the IL does not assemble).
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/avail.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=AVAIL
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/avail.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=AVAIL
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/avail.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=AVAIL
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/avail.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=AVAIL
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/avail.ll \
; RUN:   > %t/avail.lnx.il
; RUN: FileCheck %s --check-prefix=AVAIL-IL --input-file=%t/avail.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/avail.ll \
; RUN:   > %t/avail.win.il
; RUN: FileCheck %s --check-prefix=AVAIL-IL --input-file=%t/avail.win.il

; Declared callees whose result needs two values.
; DEFINE: %{multi} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/extern-multi.ll -o /dev/null \
; RUN:   2> %t/extern-multi.lnx.err
; RUN: %{multi} --check-prefix=EXTMULTI --input-file=%t/extern-multi.lnx.err
; RUN: %{multi} --check-prefix=NEXTMULTI --input-file=%t/extern-multi.lnx.err
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/extern-multi.ll -o /dev/null \
; RUN:   2> %t/extern-multi.win.err
; RUN: %{multi} --check-prefix=EXTMULTI --input-file=%t/extern-multi.win.err
; RUN: %{multi} --check-prefix=NEXTMULTI --input-file=%t/extern-multi.win.err
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/extern-multi.ll -o /dev/null \
; RUN:   2> %t/extern-multi.lnx.o0.err
; RUN: %{multi} --check-prefix=EXTMULTI --input-file=%t/extern-multi.lnx.o0.err
; RUN: %{multi} --check-prefix=NEXTMULTI --input-file=%t/extern-multi.lnx.o0.err
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/extern-multi.ll -o /dev/null \
; RUN:   2> %t/extern-multi.win.o0.err
; RUN: %{multi} --check-prefix=EXTMULTI --input-file=%t/extern-multi.win.o0.err
; RUN: %{multi} --check-prefix=NEXTMULTI --input-file=%t/extern-multi.win.o0.err
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/extern-multi.ll \
; RUN:   > %t/extern-multi.lnx.il
; RUN: FileCheck %s --check-prefix=EXTMULTI-IL --implicit-check-not=pinvokeimpl --input-file=%t/extern-multi.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/extern-multi.ll \
; RUN:   > %t/extern-multi.win.il
; RUN: FileCheck %s --check-prefix=EXTMULTI-IL --implicit-check-not=pinvokeimpl --input-file=%t/extern-multi.win.il

; byval argument of an imported declaration (the call is built, and ext gets
; no P/Invoke, so the IL does not assemble).
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/byval-import.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BYVALIMP
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/byval-import.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BYVALIMP
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/byval-import.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BYVALIMP
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/byval-import.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BYVALIMP
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/byval-import.ll \
; RUN:   > %t/byval-import.lnx.il
; RUN: FileCheck %s --check-prefix=BYVALIMP-IL --implicit-check-not=pinvokeimpl --input-file=%t/byval-import.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/byval-import.ll \
; RUN:   > %t/byval-import.win.il
; RUN: FileCheck %s --check-prefix=BYVALIMP-IL --implicit-check-not=pinvokeimpl --input-file=%t/byval-import.win.il

;--- variadic.ll
; A variadic callee, called with and without variadic arguments, then a
; function that compiles normally.
; VARIADIC:     error: {{.*}}call_variadic{{.*}}calls to variadic functions are not supported for MSIL yet
; VARIADIC:     error: {{.*}}call_variadic_fixed_only{{.*}}calls to variadic functions are not supported for MSIL yet

; VARIADIC-IL-LABEL: .method {{.*}}'call_variadic'(
; VARIADIC-IL-NOT:   {{^}}  call
; VARIADIC-IL:       {{^}}  ret{{$}}
; VARIADIC-IL-NEXT:  {{^}}} // end of method 'call_variadic'{{$}}
; VARIADIC-IL-LABEL: .method {{.*}}'call_variadic_fixed_only'(
; VARIADIC-IL-NOT:   {{^}}  call
; VARIADIC-IL:       {{^}}  ret{{$}}
; VARIADIC-IL-NEXT:  {{^}}} // end of method 'call_variadic_fixed_only'{{$}}
; VARIADIC-IL-LABEL: .method {{.*}}'after_variadic'(
; VARIADIC-IL:       {{^}}  ldarg.0{{$}}
; VARIADIC-IL-NEXT:  {{^}}  ldarg.1{{$}}
; VARIADIC-IL-NEXT:  {{^}}  add{{$}}
; VARIADIC-IL-NEXT:  {{^}}  ret{{$}}
source_filename = "variadic.c"

declare i32 @vf(i32, ...)

define i32 @call_variadic(i32 %x) {
  %r = call i32 (i32, ...) @vf(i32 %x, i32 2, double 3.0)
  ret i32 %r
}

define void @call_variadic_fixed_only(i32 %x) {
  %r = call i32 (i32, ...) @vf(i32 %x)
  ret void
}

define i32 @after_variadic(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- variadic-multi.ll
; Calls of variadic functions returning i128 and { i64, i64 }; the callers
; return one half, so their own result is a single value. Each call is
; reported once and nothing is left to crash later.
; VMULTI:       error: {{.*}}call_variadic_i128{{.*}}calls to variadic functions are not supported for MSIL yet
; VMULTI:       error: {{.*}}call_variadic_pair{{.*}}calls to variadic functions are not supported for MSIL yet

; VMULTI-IL-LABEL: .method {{.*}}'call_variadic_i128'(
; VMULTI-IL-SAME:  int32) cil managed{{$}}
; VMULTI-IL-NOT:   {{^}}  call
; VMULTI-IL:       {{^}}  ret{{$}}
; VMULTI-IL-NEXT:  {{^}}} // end of method 'call_variadic_i128'{{$}}
; VMULTI-IL-LABEL: .method {{.*}}'call_variadic_pair'(
; VMULTI-IL-SAME:  int32) cil managed{{$}}
; VMULTI-IL-NOT:   {{^}}  call
; VMULTI-IL:       {{^}}  ret{{$}}
; VMULTI-IL-NEXT:  {{^}}} // end of method 'call_variadic_pair'{{$}}
; VMULTI-IL-LABEL: .method {{.*}}'after_variadic_multi'(
; VMULTI-IL:       {{^}}  ldarg.0{{$}}
; VMULTI-IL-NEXT:  {{^}}  ldarg.1{{$}}
; VMULTI-IL-NEXT:  {{^}}  add{{$}}
; VMULTI-IL-NEXT:  {{^}}  ret{{$}}
source_filename = "variadic-multi.c"

declare i128 @vf128(i32, ...)
declare { i64, i64 } @vfpair(i32, ...)

define i64 @call_variadic_i128(i32 %x) {
  %r = call i128 (i32, ...) @vf128(i32 %x, i32 1)
  %t = trunc i128 %r to i64
  ret i64 %t
}

define i64 @call_variadic_pair(i32 %x) {
  %r = call { i64, i64 } (i32, ...) @vfpair(i32 %x, i32 1)
  %e = extractvalue { i64, i64 } %r, 1
  ret i64 %e
}

define i64 @after_variadic_multi(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}

;--- libcall.ll
; i128 division and remainder by a variable, and by 10 (not a power of two),
; need __divti3, __udivti3, __modti3 and __umodti3. The type legalizer reports
; them (ReplaceNodeResults) before any call is built; no ExternalSymbol node
; reaches LowerCall.
; LIBCALL:      error: {{.*}}div128{{.*}}runtime helper '__divti3' is not available for MSIL yet (phase 3)
; LIBCALL:      error: {{.*}}urem128{{.*}}runtime helper '__umodti3' is not available for MSIL yet (phase 3)
; LIBCALL:      error: {{.*}}udiv128{{.*}}runtime helper '__udivti3' is not available for MSIL yet (phase 3)
; LIBCALL:      error: {{.*}}srem128{{.*}}runtime helper '__modti3' is not available for MSIL yet (phase 3)
; LIBCALL:      error: {{.*}}sdiv128_by_10{{.*}}runtime helper '__divti3' is not available for MSIL yet (phase 3)
; LIBCALL:      error: {{.*}}udiv128_by_10{{.*}}runtime helper '__udivti3' is not available for MSIL yet (phase 3)

; LIBCALL-IL-LABEL: .method {{.*}}'div128'(
; LIBCALL-IL-SAME:  native int, native int, native int, native int) cil managed{{$}}
; LIBCALL-IL-NOT:   {{^}}  call
; LIBCALL-IL:       {{^}}  ret{{$}}
; LIBCALL-IL-NEXT:  {{^}}} // end of method 'div128'{{$}}
; LIBCALL-IL-LABEL: .method {{.*}}'urem128'(
; LIBCALL-IL-NOT:   {{^}}  call
; LIBCALL-IL:       {{^}}  ret{{$}}
; LIBCALL-IL-NEXT:  {{^}}} // end of method 'urem128'{{$}}
; LIBCALL-IL-LABEL: .method {{.*}}'udiv128'(
; LIBCALL-IL-NOT:   {{^}}  call
; LIBCALL-IL:       {{^}}  ret{{$}}
; LIBCALL-IL-NEXT:  {{^}}} // end of method 'udiv128'{{$}}
; LIBCALL-IL-LABEL: .method {{.*}}'srem128'(
; LIBCALL-IL-NOT:   {{^}}  call
; LIBCALL-IL:       {{^}}  ret{{$}}
; LIBCALL-IL-NEXT:  {{^}}} // end of method 'srem128'{{$}}
; LIBCALL-IL-LABEL: .method {{.*}}'sdiv128_by_10'(
; LIBCALL-IL-SAME:  native int, native int) cil managed{{$}}
; LIBCALL-IL-NOT:   {{^}}  call
; LIBCALL-IL:       {{^}}  ret{{$}}
; LIBCALL-IL-NEXT:  {{^}}} // end of method 'sdiv128_by_10'{{$}}
; LIBCALL-IL-LABEL: .method {{.*}}'udiv128_by_10'(
; LIBCALL-IL-NOT:   {{^}}  call
; LIBCALL-IL:       {{^}}  ret{{$}}
; LIBCALL-IL-NEXT:  {{^}}} // end of method 'udiv128_by_10'{{$}}
; LIBCALL-IL-LABEL: .method {{.*}}'after_libcall'(
; LIBCALL-IL:       {{^}}  ldarg.0{{$}}
; LIBCALL-IL-NEXT:  {{^}}  ldarg.1{{$}}
; LIBCALL-IL-NEXT:  {{^}}  div{{$}}
; LIBCALL-IL-NEXT:  {{^}}  ret{{$}}
; LIBCALL-IL-NOT:   {{__u?(div|mod)ti3}}
source_filename = "libcall.c"

define i64 @div128(i128 %a, i128 %b) {
  %q = sdiv i128 %a, %b
  %t = trunc i128 %q to i64
  ret i64 %t
}

define i64 @urem128(i128 %a, i128 %b) {
  %q = urem i128 %a, %b
  %t = trunc i128 %q to i64
  ret i64 %t
}

define i64 @udiv128(i128 %a, i128 %b) {
  %q = udiv i128 %a, %b
  %t = trunc i128 %q to i64
  ret i64 %t
}

define i64 @srem128(i128 %a, i128 %b) {
  %q = srem i128 %a, %b
  %t = trunc i128 %q to i64
  ret i64 %t
}

define i64 @sdiv128_by_10(i128 %a) {
  %q = sdiv i128 %a, 10
  %t = trunc i128 %q to i64
  ret i64 %t
}

define i64 @udiv128_by_10(i128 %a) {
  %q = udiv i128 %a, 10
  %t = trunc i128 %q to i64
  ret i64 %t
}

define i64 @after_libcall(i64 %a, i64 %b) {
  %q = sdiv i64 %a, %b
  ret i64 %q
}

;--- libcall-fp.ll
; i128 to floating point is a runtime library call with one result, so it
; reaches LowerCall as an ExternalSymbol callee.
; LIBCALLFP:    error: {{.*}}sitofp128{{.*}}runtime helper '__floattidf' is not available for MSIL yet (phase 3)
; LIBCALLFP:    error: {{.*}}uitofp128{{.*}}runtime helper '__floatuntisf' is not available for MSIL yet (phase 3)

; LIBCALLFP-IL-LABEL: .method {{.*}}'sitofp128'(
; LIBCALLFP-IL-SAME:  native int, native int) cil managed{{$}}
; LIBCALLFP-IL-NOT:   {{^}}  call
; LIBCALLFP-IL:       {{^}}  ret{{$}}
; LIBCALLFP-IL-NEXT:  {{^}}} // end of method 'sitofp128'{{$}}
; LIBCALLFP-IL-LABEL: .method {{.*}}'uitofp128'(
; LIBCALLFP-IL-SAME:  native int, native int) cil managed{{$}}
; LIBCALLFP-IL-NOT:   {{^}}  call
; LIBCALLFP-IL:       {{^}}  ret{{$}}
; LIBCALLFP-IL-NEXT:  {{^}}} // end of method 'uitofp128'{{$}}
; LIBCALLFP-IL-LABEL: .method {{.*}}'after_libcall_fp'(
; LIBCALLFP-IL:       {{^}}  ldarg.0{{$}}
; LIBCALLFP-IL-NEXT:  {{^}}  ldarg.1{{$}}
; LIBCALLFP-IL-NEXT:  {{^}}  add{{$}}
; LIBCALLFP-IL-NEXT:  {{^}}  ret{{$}}
; LIBCALLFP-IL-NOT:   __float
source_filename = "libcall-fp.c"

define double @sitofp128(i128 %x) {
  %r = sitofp i128 %x to double
  ret double %r
}

define float @uitofp128(i128 %x) {
  %r = uitofp i128 %x to float
  ret float %r
}

define i64 @after_libcall_fp(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}

;--- struct-mismatch.ll
; MSILFixFunctionBitcasts (a copy of WebAssembly's) creates no wrapper when a
; parameter or the result differs in a struct type, so these calls keep their
; mismatched call-site types and LowerCall reports them: a { i32 } argument
; for an i32 parameter, and a { i32, i32 } result (two values: demoted to a
; frame slot first) for an i32 result. The errors are matched as a group: the
; result case may be reported before ISel. The matching call after them is
; compiled as usual.
; STRUCTMM-DAG: error: {{.*}}call_struct_param{{.*}}mismatched call signature
; STRUCTMM-DAG: error: {{.*}}call_struct_result{{.*}}mismatched call signature

; STRUCTMM-IL:       {{^}}.method public static int32 'callee'(int32) cil managed{{$}}
; STRUCTMM-IL-LABEL: .method {{.*}}'call_struct_param'(
; STRUCTMM-IL-NOT:   {{^}}  call
; STRUCTMM-IL:       {{^}}  ret{{$}}
; STRUCTMM-IL-NEXT:  {{^}}} // end of method 'call_struct_param'{{$}}
; STRUCTMM-IL-LABEL: .method {{.*}}'call_struct_result'(
; STRUCTMM-IL-NOT:   {{^}}  call
; STRUCTMM-IL:       {{^}}  ret{{$}}
; STRUCTMM-IL-NEXT:  {{^}}} // end of method 'call_struct_result'{{$}}
; STRUCTMM-IL-LABEL: .method {{.*}}'call_matching'(
; STRUCTMM-IL:       {{^}}  ldarg.0{{$}}
; STRUCTMM-IL-NEXT:  {{^}}  call int32 'callee'(int32){{$}}
; STRUCTMM-IL-NEXT:  {{^}}  ret{{$}}
; STRUCTMM-IL-NOT:   {{^}}.method {{.*}}_bitcast
source_filename = "struct-mismatch.c"

define i32 @callee(i32 %a) {
  ret i32 %a
}

define i32 @call_struct_param(i32 %x) {
  %s = insertvalue { i32 } poison, i32 %x, 0
  %r = call i32 @callee({ i32 } %s)
  ret i32 %r
}

define i32 @call_struct_result(i32 %x) {
  %r = call { i32, i32 } @callee(i32 %x)
  %e = extractvalue { i32, i32 } %r, 1
  ret i32 %e
}

define i32 @call_matching(i32 %x) {
  %r = call i32 @callee(i32 %x)
  ret i32 %r
}

;--- arg-attrs.ll
; nest and inalloca arguments at calls of declared functions.
; ARGATTR:      error: {{.*}}call_nest{{.*}}nest arguments are not supported for MSIL
; ARGATTR:      error: {{.*}}call_inalloca{{.*}}inalloca arguments are not supported for MSIL

; ARGATTR-IL-LABEL: .method {{.*}}'call_nest'(
; ARGATTR-IL-NOT:   {{^}}  call
; ARGATTR-IL:       {{^}}  ret{{$}}
; ARGATTR-IL-NEXT:  {{^}}} // end of method 'call_nest'{{$}}
; ARGATTR-IL-LABEL: .method {{.*}}'call_inalloca'(
; ARGATTR-IL-NOT:   {{^}}  call
; ARGATTR-IL:       {{^}}  ret{{$}}
; ARGATTR-IL-NEXT:  {{^}}} // end of method 'call_inalloca'{{$}}
; ARGATTR-IL-LABEL: .method {{.*}}'after_arg_attrs'(
; ARGATTR-IL:       {{^}}  ldarg.0{{$}}
; ARGATTR-IL-NEXT:  {{^}}  ldarg.1{{$}}
; ARGATTR-IL-NEXT:  {{^}}  add{{$}}
; ARGATTR-IL-NEXT:  {{^}}  ret{{$}}
source_filename = "arg-attrs.c"

declare i64 @n(ptr nest)
declare i64 @ia(ptr inalloca(i32))

define i64 @call_nest(ptr %p) {
  %r = call i64 @n(ptr nest %p)
  ret i64 %r
}

define i64 @call_inalloca(ptr %p) {
  %r = call i64 @ia(ptr inalloca(i32) %p)
  ret i64 %r
}

define i32 @after_arg_attrs(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- musttail.ll
; A musttail call (CIL 'tail.' is not used in Phase 1).
; MUSTTAIL:     error: {{.*}}call_musttail{{.*}}musttail calls are not supported for MSIL yet

; MUSTTAIL-IL:       {{^}}.method public static int32 'callee'(int32) cil managed{{$}}
; MUSTTAIL-IL-LABEL: .method {{.*}}'call_musttail'(
; MUSTTAIL-IL-NOT:   {{^}}  call
; MUSTTAIL-IL:       {{^}}  ret{{$}}
; MUSTTAIL-IL-NEXT:  {{^}}} // end of method 'call_musttail'{{$}}
; MUSTTAIL-IL-LABEL: .method {{.*}}'after_musttail'(
; MUSTTAIL-IL:       {{^}}  ldarg.0{{$}}
; MUSTTAIL-IL-NEXT:  {{^}}  ldarg.1{{$}}
; MUSTTAIL-IL-NEXT:  {{^}}  add{{$}}
; MUSTTAIL-IL-NEXT:  {{^}}  ret{{$}}
source_filename = "musttail.c"

define i32 @callee(i32 %a) {
  ret i32 %a
}

define i32 @call_musttail(i32 %x) {
  %r = musttail call i32 @callee(i32 %x)
  ret i32 %r
}

define i32 @after_musttail(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- callconv.ll
; Calls of declared functions with the x86_stdcallcc and ghccc conventions.
; CALLCONV:     error: {{.*}}call_stdcall{{.*}}calling convention is not supported for MSIL
; CALLCONV:     error: {{.*}}call_ghc{{.*}}calling convention is not supported for MSIL

; CALLCONV-IL-LABEL: .method {{.*}}'call_stdcall'(
; CALLCONV-IL-NOT:   {{^}}  call
; CALLCONV-IL:       {{^}}  ret{{$}}
; CALLCONV-IL-NEXT:  {{^}}} // end of method 'call_stdcall'{{$}}
; CALLCONV-IL-LABEL: .method {{.*}}'call_ghc'(
; CALLCONV-IL-NOT:   {{^}}  call
; CALLCONV-IL:       {{^}}  ret{{$}}
; CALLCONV-IL-NEXT:  {{^}}} // end of method 'call_ghc'{{$}}
; CALLCONV-IL-LABEL: .method {{.*}}'after_callconv'(
; CALLCONV-IL:       {{^}}  ldarg.0{{$}}
; CALLCONV-IL-NEXT:  {{^}}  ldarg.1{{$}}
; CALLCONV-IL-NEXT:  {{^}}  add{{$}}
; CALLCONV-IL-NEXT:  {{^}}  ret{{$}}
source_filename = "callconv.c"

declare x86_stdcallcc i32 @h(i32)
declare ghccc void @k(i32)

define i32 @call_stdcall(i32 %a) {
  %r = call x86_stdcallcc i32 @h(i32 %a)
  ret i32 %r
}

define void @call_ghc(i32 %a) {
  call ghccc void @k(i32 %a)
  ret void
}

define i32 @after_callconv(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- unknown-intrinsic.ll
; Declarations named 'llvm.*' that are not LLVM intrinsics pass the IR
; verifier, so their calls reach LowerCall as direct calls.
; UNKINTR:      error: {{.*}}call_unknown_intrinsic{{.*}}unknown intrinsic 'llvm.foo.bar' is not supported for MSIL
; UNKINTR:      error: {{.*}}call_unknown_intrinsic_void{{.*}}unknown intrinsic 'llvm.foo.baz' is not supported for MSIL

; UNKINTR-IL-LABEL: .method {{.*}}'call_unknown_intrinsic'(
; UNKINTR-IL-NOT:   {{^}}  call
; UNKINTR-IL:       {{^}}  ret{{$}}
; UNKINTR-IL-NEXT:  {{^}}} // end of method 'call_unknown_intrinsic'{{$}}
; UNKINTR-IL-LABEL: .method {{.*}}'call_unknown_intrinsic_void'(
; UNKINTR-IL-NOT:   {{^}}  call
; UNKINTR-IL:       {{^}}  ret{{$}}
; UNKINTR-IL-NEXT:  {{^}}} // end of method 'call_unknown_intrinsic_void'{{$}}
; UNKINTR-IL-LABEL: .method {{.*}}'after_unknown_intrinsic'(
; UNKINTR-IL:       {{^}}  ldarg.0{{$}}
; UNKINTR-IL-NEXT:  {{^}}  ldarg.1{{$}}
; UNKINTR-IL-NEXT:  {{^}}  add{{$}}
; UNKINTR-IL-NEXT:  {{^}}  ret{{$}}
; UNKINTR-IL-NOT:   llvm.foo
source_filename = "unknown-intrinsic.c"

declare i32 @llvm.foo.bar(i32)
declare void @llvm.foo.baz(i32)

define i32 @call_unknown_intrinsic(i32 %x) {
  %r = call i32 @llvm.foo.bar(i32 %x)
  ret i32 %r
}

define void @call_unknown_intrinsic_void(i32 %x) {
  call void @llvm.foo.baz(i32 %x)
  ret void
}

define i32 @after_unknown_intrinsic(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- diagnosed-callee.ll
; A definition with inline assembly, called from two other functions.
; MSILPrepareIR reports it, deletes its body and erases its calls, so this is
; the only error: %{diag} rejects any second 'error:' line, such as
; "undefined function 'has_asm' is not defined in this module ..." for the
; function that became a declaration, and the CHECK-NOT makes that explicit.
; DIAGCALLEE:     error: {{.*}}has_asm{{.*}}inline assembly is not supported for MSIL
; DIAGCALLEE-NOT: undefined function

; DIAGCALLEE-IL-LABEL: .method {{.*}}'calls_has_asm'(
; DIAGCALLEE-IL-NOT:   {{^}}  call
; DIAGCALLEE-IL:       {{^}}  ret{{$}}
; DIAGCALLEE-IL-NEXT:  {{^}}} // end of method 'calls_has_asm'{{$}}
; DIAGCALLEE-IL-LABEL: .method {{.*}}'drops_has_asm'(
; DIAGCALLEE-IL-NOT:   {{^}}  call
; DIAGCALLEE-IL:       {{^}}  ret{{$}}
; DIAGCALLEE-IL-NEXT:  {{^}}} // end of method 'drops_has_asm'{{$}}
; DIAGCALLEE-IL-LABEL: .method {{.*}}'after_diagnosed_callee'(
; DIAGCALLEE-IL:       {{^}}  ldarg.0{{$}}
; DIAGCALLEE-IL-NEXT:  {{^}}  ldarg.1{{$}}
; DIAGCALLEE-IL-NEXT:  {{^}}  add{{$}}
; DIAGCALLEE-IL-NEXT:  {{^}}  ret{{$}}
source_filename = "diagnosed-callee.c"

define i32 @has_asm(i32 %x) noinline {
  %r = call i32 asm "nop", "=r,r"(i32 %x)
  ret i32 %r
}

define i32 @calls_has_asm(i32 %x) {
  %r = call i32 @has_asm(i32 %x)
  %s = add i32 %r, 1
  ret i32 %s
}

define void @drops_has_asm(i32 %x) {
  %r = call i32 @has_asm(i32 %x)
  ret void
}

define i32 @after_diagnosed_callee(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- invoke.ll
; A function with a personality invokes a variadic function whose result
; needs two values, a definition whose body MSILPrepareIR deletes (inline
; assembly), and the variadic function again; all three share one landing
; pad, which has a PHI. The errors are the inline assembly and the two
; variadic calls, nothing crashes, and the rest of the module compiles.
; INVOKE:       error: {{.*}}has_asm{{.*}}inline assembly is not supported for MSIL
; INVOKE:       error: {{.*}}invokes{{.*}}calls to variadic functions are not supported for MSIL yet
; INVOKE:       error: {{.*}}invokes{{.*}}calls to variadic functions are not supported for MSIL yet

; INVOKE-IL-LABEL: .method {{.*}}'invokes'(
; INVOKE-IL-SAME:  int32) cil managed{{$}}
; INVOKE-IL-NOT:   {{^}}  call
; INVOKE-IL:       {{^}}  ret{{$}}
; INVOKE-IL-NEXT:  {{^}}} // end of method 'invokes'{{$}}
; INVOKE-IL-LABEL: .method {{.*}}'after_invoke'(
; INVOKE-IL:       {{^}}  ldarg.0{{$}}
; INVOKE-IL-NEXT:  {{^}}  ldarg.1{{$}}
; INVOKE-IL-NEXT:  {{^}}  add{{$}}
; INVOKE-IL-NEXT:  {{^}}  ret{{$}}
source_filename = "invoke.c"

declare i32 @__gxx_personality_v0(...)
declare i128 @vi(i32, ...)

define i32 @has_asm(i32 %x) noinline {
  %r = call i32 asm "nop", "=r,r"(i32 %x)
  ret i32 %r
}

define i64 @invokes(i32 %x) personality ptr @__gxx_personality_v0 {
entry:
  %a = invoke i128 (i32, ...) @vi(i32 %x, i32 1)
          to label %ok unwind label %lp
ok:
  %b = invoke i32 @has_asm(i32 %x)
          to label %ok2 unwind label %lp
ok2:
  %c = invoke i128 (i32, ...) @vi(i32 %b, i32 2)
          to label %done unwind label %lp
done:
  %t = trunc i128 %c to i64
  ret i64 %t
lp:
  %p = phi i64 [ 1, %entry ], [ 2, %ok ], [ 3, %ok2 ]
  %l = landingpad { ptr, i32 } cleanup
  ret i64 %p
}

define i32 @after_invoke(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- alias-callers.ll
; Calls of an alias whose result is used by an add, of an ifunc, and of an
; alias whose call-site result is i128 (truncated to i64; a function that
; returns i128 itself needs stores, M7). The errors are the alias and ifunc
; definitions, in module order; their callers call nothing and return an
; undefined value, an uninitialized local at -O2.
; ALIAS:        error: {{.*}}alias @alias_fn is not supported for MSIL
; ALIAS:        error: {{.*}}alias @alias128 is not supported for MSIL
; ALIAS:        error: {{.*}}ifunc @ifunc_fn is not supported for MSIL

; ALIAS-IL-LABEL: .method {{.*}}'calls_alias'(
; ALIAS-IL-SAME:  int32) cil managed{{$}}
; ALIAS-IL-NEXT:  {{^\{$}}
; ALIAS-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; ALIAS-IL-NEXT:  {{^}}  .locals (int32){{$}}
; ALIAS-IL-NEXT:  {{^}}  ldloc.0{{$}}
; ALIAS-IL-NEXT:  {{^}}  ret{{$}}
; ALIAS-IL-NEXT:  {{^}}} // end of method 'calls_alias'{{$}}
; ALIAS-IL-LABEL: .method {{.*}}'calls_ifunc'(
; ALIAS-IL-SAME:  int32) cil managed{{$}}
; ALIAS-IL-NEXT:  {{^\{$}}
; ALIAS-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; ALIAS-IL-NEXT:  {{^}}  .locals (int32){{$}}
; ALIAS-IL-NEXT:  {{^}}  ldloc.0{{$}}
; ALIAS-IL-NEXT:  {{^}}  ret{{$}}
; ALIAS-IL-NEXT:  {{^}}} // end of method 'calls_ifunc'{{$}}
; Since M9 the call is not erased before ISel: the i128 result is read back
; from its demoted frame slot (section 12, M4 fix-1 row: from M7 on the slot
; compiles; the frame is a localloc), and nothing is called.
; ALIAS-IL-LABEL: .method {{.*}}'calls_alias_i128'(
; ALIAS-IL-SAME:  native int) cil managed{{$}}
; ALIAS-IL-NOT:   {{^}}  call
; ALIAS-IL:       {{^}}  ret{{$}}
; ALIAS-IL-NEXT:  {{^}}} // end of method 'calls_alias_i128'{{$}}
; ALIAS-IL-LABEL: .method {{.*}}'after_alias'(
; ALIAS-IL:       {{^}}  ldarg.0{{$}}
; ALIAS-IL-NEXT:  {{^}}  ldarg.1{{$}}
; ALIAS-IL-NEXT:  {{^}}  add{{$}}
; ALIAS-IL-NEXT:  {{^}}  ret{{$}}
source_filename = "alias-callers.c"

define i32 @target(i32 %x) noinline {
  ret i32 %x
}

@alias_fn = alias i32 (i32), ptr @target
@alias128 = alias i128 (i64), ptr @target
@ifunc_fn = ifunc i32 (i32), ptr @resolver

define ptr @resolver() {
  ret ptr null
}

define i32 @calls_alias(i32 %x) {
  %r = call i32 @alias_fn(i32 %x)
  %s = add i32 %r, 1
  ret i32 %s
}

define i32 @calls_ifunc(i32 %x) {
  %r = call i32 @ifunc_fn(i32 %x)
  ret i32 %r
}

define i64 @calls_alias_i128(i64 %x) {
  %r = call i128 @alias128(i64 %x)
  %t = trunc i128 %r to i64
  ret i64 %t
}

define i32 @after_alias(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- extern.ll
; One error at the end of the module, naming the callee; the declaration that
; is never called is not mentioned.
; EXTERN:       {{^}}error: {{.*}}undefined function 'ext' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
source_filename = "extern.c"

declare i32 @ext(i32)
declare i32 @unused_decl(i32)

define i32 @call_ext(i32 %x) {
  %r = call i32 @ext(i32 %x)
  ret i32 %r
}

define i32 @after_ext(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}

;--- avail.ll
; f1 calls the available_externally definition ae twice and the declaration
; ext once, f2 calls ext twice. Neither callee is emitted, and each is
; reported once (ae comes first both in the module and in the order of first
; use) and gets no P/Invoke.
; AVAIL:        {{^}}error: {{.*}}undefined function 'ae' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; AVAIL:        {{^}}error: {{.*}}undefined function 'ext' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}

; AVAIL-IL-NOT:   {{^}}.method {{.*}}'ae'(
; AVAIL-IL-LABEL: .method {{.*}}'f1'(
; AVAIL-IL:       {{^}}  ldarg.0{{$}}
; AVAIL-IL-NEXT:  {{^}}  call int32 'ae'(int32){{$}}
; AVAIL-IL-NEXT:  {{^}}  call int32 'ext'(int32){{$}}
; AVAIL-IL-NEXT:  {{^}}  call int32 'ae'(int32){{$}}
; AVAIL-IL-NEXT:  {{^}}  ret{{$}}
; AVAIL-IL-LABEL: .method {{.*}}'f2'(
; AVAIL-IL:       {{^}}  ldarg.0{{$}}
; AVAIL-IL-NEXT:  {{^}}  call int32 'ext'(int32){{$}}
; AVAIL-IL-NEXT:  {{^}}  call int32 'ext'(int32){{$}}
; AVAIL-IL-NEXT:  {{^}}  ret{{$}}
; AVAIL-IL-NOT:   {{^}}.method {{.*}}'{{ae|ext}}'(
; AVAIL-IL-NOT:   pinvokeimpl
source_filename = "avail.c"

define available_externally i32 @ae(i32 %x) {
  ret i32 %x
}
declare i32 @ext(i32)

define i32 @f1(i32 %x) {
  %a = call i32 @ae(i32 %x)
  %b = call i32 @ext(i32 %a)
  %c = call i32 @ae(i32 %b)
  ret i32 %c
}

define i32 @f2(i32 %x) {
  %a = call i32 @ext(i32 %x)
  %b = call i32 @ext(i32 %a)
  ret i32 %b
}

;--- extern-multi.ll
; Calls of declared functions returning { double, double } (__muldc3) and
; <2 x float> (__divsc3), which clang -O2 emits for C complex multiplication
; and division on x86_64-unknown-linux-gnu-msil, i128 and { i64, i64 }. Each
; callee is called from two functions, with the result used and unused. None
; is an import: each is reported once, in the end-of-module text, and gets no
; P/Invoke; the errors are matched as a group and counted. Whether the calls
; are built does not matter here; the rest of the module compiles.
; EXTMULTI-DAG: {{^}}error: {{.*}}undefined function '__divsc3' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; EXTMULTI-DAG: {{^}}error: {{.*}}undefined function 'ext128' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; EXTMULTI-DAG: {{^}}error: {{.*}}undefined function '__muldc3' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; EXTMULTI-DAG: {{^}}error: {{.*}}undefined function 'extpair' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; NEXTMULTI-COUNT-4: {{^}}error:
; NEXTMULTI-NOT:     {{^}}error:

; EXTMULTI-IL-LABEL: .method {{.*}}'cdiv'(
; EXTMULTI-IL-SAME:  float32, float32, float32, float32) cil managed{{$}}
; EXTMULTI-IL-LABEL: .method {{.*}}'use128'(
; EXTMULTI-IL-SAME:  native int) cil managed{{$}}
; EXTMULTI-IL-LABEL: .method {{.*}}'cmul'(
; EXTMULTI-IL-SAME:  float64, float64, float64, float64) cil managed{{$}}
; EXTMULTI-IL-LABEL: .method {{.*}}'usepair'(
; EXTMULTI-IL-SAME:  native int) cil managed{{$}}
; EXTMULTI-IL-LABEL: .method {{.*}}'unused'(
; EXTMULTI-IL-SAME:  native int, float64, float32) cil managed{{$}}
; EXTMULTI-IL-LABEL: .method {{.*}}'after_extern_multi'(
; EXTMULTI-IL:       {{^}}  ldarg.0{{$}}
; EXTMULTI-IL-NEXT:  {{^}}  ldarg.1{{$}}
; EXTMULTI-IL-NEXT:  {{^}}  add{{$}}
; EXTMULTI-IL-NEXT:  {{^}}  ret{{$}}
; EXTMULTI-IL-NOT:   {{^}}.method
source_filename = "extern-multi.c"

declare { double, double } @__muldc3(double, double, double, double)
declare <2 x float> @__divsc3(float, float, float, float)
declare i128 @ext128(i64)
declare { i64, i64 } @extpair(i64)

define float @cdiv(float %a, float %b, float %c, float %d) {
  %r = call <2 x float> @__divsc3(float %a, float %b, float %c, float %d)
  %re = extractelement <2 x float> %r, i64 0
  %im = extractelement <2 x float> %r, i64 1
  %s = fadd float %re, %im
  ret float %s
}

define i64 @use128(i64 %x) {
  %r = call i128 @ext128(i64 %x)
  %t = trunc i128 %r to i64
  ret i64 %t
}

define double @cmul(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__muldc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  %im = extractvalue { double, double } %r, 1
  %s = fadd double %re, %im
  ret double %s
}

define i64 @usepair(i64 %x) {
  %r = call { i64, i64 } @extpair(i64 %x)
  %e = extractvalue { i64, i64 } %r, 1
  ret i64 %e
}

define void @unused(i64 %x, double %d, float %f) {
  %r1 = call { double, double } @__muldc3(double %d, double %d, double %d, double %d)
  %r2 = call <2 x float> @__divsc3(float %f, float %f, float %f, float %f)
  %r3 = call i128 @ext128(i64 %x)
  %r4 = call { i64, i64 } @extpair(i64 %x)
  ret void
}

define i32 @after_extern_multi(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- byval-import.ll
; A call of an import (D14: msil-import-module) with a byval aggregate. The
; caller copies the aggregate into its frame and calls ext with the copy's
; address, as for any byval argument (byval.ll); a P/Invoke cannot pass the
; aggregate, so ext is reported once at the end of the module (M8.c's text;
; pinvoke-errors.ll SIG has such declarations) and gets no P/Invoke.
; BYVALIMP:          {{^}}error: imported function 'ext' has a parameter or result that a P/Invoke cannot pass natively (aggregates, vectors, 128-bit integers and half are not supported for MSIL yet){{$}}

; BYVALIMP-IL-LABEL: .method {{.*}}'caller'(
; BYVALIMP-IL-SAME:  native int) cil managed{{$}}
; BYVALIMP-IL:       {{^}}  localloc{{$}}
; BYVALIMP-IL:       {{^}}  call native int 'ext'(native int){{$}}
; BYVALIMP-IL-NEXT:  {{^}}  ret{{$}}
source_filename = "byval-import.c"

%S = type { i64, i64, i64 }

declare i64 @ext(ptr byval(%S) align 8) "msil-import-module"="mylib"

define i64 @caller(ptr %p) {
  %r = call i64 @ext(ptr byval(%S) align 8 %p)
  ret i64 %r
}
