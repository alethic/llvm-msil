; Text output of a module that needs a runtime helper (ImplementationPlan.md
; 5.14.3 A9, 5.10.13, M25.c; plan section 6 M25, its shared texts T-A, T-B
; and T-C and its name classes N1-N3).
; Only msil-ld links the toolchain's per-triple helper archive
; clang_rt.builtins (the driver passes it after the default libraries, 5.14.3
; A1), so whole-program text (clang -S, llc, msil-ld --lto-emit-asm) of a
; module that needs one of its helpers is an error, in a standalone module
; (helpers.ll, no "msil-linked" module flag) and in a linked one
; (helpers-linked.ll, with the flag that msil-ld's LTO sets) alike:
; - T-A, "runtime helper '<name>' needs the helper archive clang_rt.builtins,
;   which only msil-ld links", for a library call whose name starts with '__'
;   (LowerCall: __extendhfsf2 for an fpext from half, and __powidf2 for
;   llvm.powi on linux-gnu) and for an undefined function of class N1 (a '__'
;   name of lto::LTO::getRuntimeLibcallSymbols, or one of the ten complex
;   helpers that clang's CodeGen calls by name and RuntimeLibcalls lacks,
;   __mulhc3, __mulsc3, __muldc3, __mulxc3, __multc3, __divhc3, __divsc3,
;   __divdc3, __divxc3 and __divtc3: here __muldc3, declared as a function
;   as clang declares it, __mulhc3 and __divhc3 in hc3.ll and the other
;   seven in cx.ll and cx-linked.ll) or N3 (a '__msil_' name that
;   cli::findTrailerDef does not know: __msil_example), unless the name has
;   compiler-rt's ti mode (T-C, below);
; - T-B, "runtime helper '<name>' is an atomic operation that only msil-ld
;   links", for the __atomic_* helpers (N2): __atomic_fetch_add_4, which
;   AtomicExpand calls for an atomicrmw add of an i32;
; - __msil_compiler_barrier, whose trailer definition cli::findTrailerDef
;   knows, is no error: the module defines it (trailer-defs.ll);
; - no other '__' name changes its text: __start_x is D14's undefined-function
;   error in the standalone module and "undefined function '__start_x' was not
;   resolved by msil-ld" in the linked one, never a helper text;
; - on windows-msvc llvm.powi calls pow with the exponent converted to
;   float64, a D14 import in the standalone module and bound by its
;   !msil.imports operand in the linked one: a P/Invoke and no error.
; Every error names its symbol exactly once (each symbol has one use): the
; errors are matched as a group (SA, LK), every error line is counted (NSA,
; NLK), and a third run, whose only directive is the first error line, bans
; the crash markers, a warning, any "(phase 3)" text and the names that must
; not be reported everywhere in the output (implicit negative checks do not
; look between or after the matches of a CHECK-DAG group). Only LowerCall's
; errors in a standalone module (f16_to_f32, powi; ti.ll's conversions) and
; ReplaceNodeResults' (i128.ll, i128-linked.ll) carry the location and
; function that DiagnosticInfoUnsupported prints; every other error, the
; AsmPrinter's and LowerCall's in a linked module, is a plain
; 'error: <text>' line (LLVMContext::emitError) and is pinned exactly.
; llc exits with 1; the IL that it still writes to stdout has no call of a
; library-call helper and no P/Invoke but pow's.
; In object form none of this is an error (5.8.10; D14's Phase 2 note):
; the objects of both modules (-filetype=obj) are the same and list every
; helper, the barrier included (msil-ld's '<trailer>' input defines it), as an
; UNDEFINED FUNCTION symbol for msil-ld to resolve, from the archive where it
; defines the name, and the object-form text calls each through
; [msil.extern]'<msil>Symbols'.
; T-C, "runtime helper '<name>' is not supported for MSIL: the helper archive
; clang_rt.builtins has no 128-bit integer helpers", since no archive serves
; compiler-rt's *ti* helpers (5.14.3 A5):
; - i128.ll and i128-linked.ll: ReplaceNodeResults reports it for an i128
;   node in every output mode, objects included: with -expand-div-rem-bits=128
;   an i128 sdiv, udiv, srem and urem reach instruction selection instead of
;   the inline expansion (i128-divrem.ll) and name __divti3, __udivti3,
;   __modti3 and __umodti3; with -expand-fp-convert-bits=128 fptosi and
;   fptoui to i128 name __fixdfti and __fixunssfti;
; - ti.ll and ti-linked.ll: in text output only, a library call or a declared
;   and called function whose name is an N1 name with compiler-rt's ti mode
;   gets it instead of T-A (plan section 12, the row 'M25.c (1) and (2) ...
;   amended (fix loop; review r2 finding 4)' of 2026-10-31): with
;   -expand-fp-convert-bits=128 sitofp i128 to double and uitofp i128 to
;   float stay library calls of __floattidf and __floatuntisf, which LowerCall
;   reports, and __divti3 is declared and called, which the AsmPrinter's check
;   of the undefined functions reports. Their objects (-filetype=obj) give no
;   diagnostic: each of the three is an UNDEFINED FUNCTION symbol, which
;   msil-ld reports with N1's undefined-symbol line, since no archive defines
;   it.
; Split modules, each with a linked twin (the "msil-linked" flag), whose
; errors all come from the AsmPrinter's check of the undefined functions:
; - clib.ll: the C library's own '__' entry points that a triple's
;   RuntimeLibcalls set lists are not N1 (plan section 12, the row 'Plan
;   5.14.2's N1 as built (M24.d (1))' of 2026-10-31), so each keeps D14's
;   undefined-function text, or "undefined function '<name>' was not
;   resolved by msil-ld" when linked, and no 'runtime helper' text appears:
;   __exp_finite and __powf_finite (both triples' sets list the '_finite'
;   functions), __memcpy_chk and __memset_chk (whose length and object-size
;   operands differ, so CodeGenPrepare cannot turn them into memcpy and
;   memset), __stack_chk_fail (only the Linux set lists it), __chkstk and
;   __security_check_cookie (only the windows-msvc set);
; - rtlib.ll: __extendhfsf2, declared and called, is N1 through the
;   RuntimeLibcalls lookup and gets T-A on both triples;
; - hc3.ll: __mulhc3 and __divhc3, which clang's CodeGen calls for
;   _Float16 _Complex multiplication and division when half arithmetic is
;   not promoted to float (on x86_64 with -fexcess-precision=16; hc3.ll has
;   clang's x86_64 Linux declarations, hc3-linked.ll its windows-msvc ones),
;   get T-A on both triples; no archive defines them (compiler-rt has no
;   mulhc3.c or divhc3.c);
; - cx.ll: the six complex helpers that no other llc test pins, __divdc3,
;   __mulsc3, __mulxc3, __divxc3, __multc3 and __divtc3, get T-A on both
;   triples (plan section 12, the row 'M25.f (2) and M25.i (1) extended (fix
;   loop; review r2 findings 6 and 8)' of 2026-10-31); cx-linked.ll also
;   calls __divsc3, which call-errors.ll (EXTMULTI) pins in a standalone
;   module only. The operands are double or float, since the name alone
;   decides the class (in C, __mulxc3 and __divxc3 take x87 long double and
;   __multc3 and __divtc3 binary128). One T-A line per name and the count of
;   the error lines make a name that falls back to D14's text, to "... was
;   not resolved by msil-ld" or to any other text fail. None of the ten
;   complex helpers has the ti mode, so T-C is banned here.
; Both x86_64 triples (D6), -O2 and -O0.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{llc} = llc -verify-machineinstrs -asm-verbose=false
; DEFINE: %{nocrash} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not="(phase 3)"
; DEFINE: %{ban} = FileCheck %s --check-prefix=BAN %{nocrash} --implicit-check-not=warning: \
; DEFINE:   --implicit-check-not=__msil_compiler_barrier --implicit-check-not="'pow'" \
; DEFINE:   --implicit-check-not="is not available for MSIL"
; DEFINE: %{il} = FileCheck %s --implicit-check-not=pinvokeimpl --implicit-check-not=__extendhfsf2 \
; DEFINE:   --implicit-check-not=__powidf2

; The standalone module: stderr, then the IL on stdout.
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/helpers.ll -o /dev/null 2> %t/sa.lnx.err
; RUN: FileCheck %s --check-prefixes=SA,SA-LNX %{nocrash} --input-file=%t/sa.lnx.err
; RUN: FileCheck %s --check-prefix=NSA-LNX --input-file=%t/sa.lnx.err
; RUN: %{ban} --implicit-check-not="was not resolved" --input-file=%t/sa.lnx.err
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/helpers.ll -o /dev/null 2> %t/sa.lnx.o0.err
; RUN: FileCheck %s --check-prefixes=SA,SA-LNX %{nocrash} --input-file=%t/sa.lnx.o0.err
; RUN: FileCheck %s --check-prefix=NSA-LNX --input-file=%t/sa.lnx.o0.err
; RUN: %{ban} --implicit-check-not="was not resolved" --input-file=%t/sa.lnx.o0.err
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/helpers.ll -o /dev/null 2> %t/sa.win.err
; RUN: FileCheck %s --check-prefix=SA %{nocrash} --input-file=%t/sa.win.err
; RUN: FileCheck %s --check-prefix=NSA-WIN --input-file=%t/sa.win.err
; RUN: %{ban} --implicit-check-not="was not resolved" --implicit-check-not=__powidf2 --input-file=%t/sa.win.err
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/helpers.ll -o /dev/null 2> %t/sa.win.o0.err
; RUN: FileCheck %s --check-prefix=SA %{nocrash} --input-file=%t/sa.win.o0.err
; RUN: FileCheck %s --check-prefix=NSA-WIN --input-file=%t/sa.win.o0.err
; RUN: %{ban} --implicit-check-not="was not resolved" --implicit-check-not=__powidf2 --input-file=%t/sa.win.o0.err
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/helpers.ll > %t/sa.lnx.il 2> /dev/null
; RUN: %{il} --check-prefixes=IL,IL-LNX --input-file=%t/sa.lnx.il
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/helpers.ll > %t/sa.win.il 2> /dev/null
; RUN: %{il} --check-prefixes=IL,IL-WIN,IL-SA-WIN --input-file=%t/sa.win.il

; The linked module.
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/helpers-linked.ll -o /dev/null 2> %t/lk.lnx.err
; RUN: FileCheck %s --check-prefixes=LK,LK-LNX %{nocrash} --input-file=%t/lk.lnx.err
; RUN: FileCheck %s --check-prefix=NLK-LNX --input-file=%t/lk.lnx.err
; RUN: %{ban} --implicit-check-not="Phase-1 import" --implicit-check-not="Phase 1 has no linker" \
; RUN:   --input-file=%t/lk.lnx.err
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/helpers-linked.ll -o /dev/null 2> %t/lk.lnx.o0.err
; RUN: FileCheck %s --check-prefixes=LK,LK-LNX %{nocrash} --input-file=%t/lk.lnx.o0.err
; RUN: FileCheck %s --check-prefix=NLK-LNX --input-file=%t/lk.lnx.o0.err
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/helpers-linked.ll -o /dev/null 2> %t/lk.win.err
; RUN: FileCheck %s --check-prefix=LK %{nocrash} --input-file=%t/lk.win.err
; RUN: FileCheck %s --check-prefix=NLK-WIN --input-file=%t/lk.win.err
; RUN: %{ban} --implicit-check-not="Phase-1 import" --implicit-check-not="Phase 1 has no linker" \
; RUN:   --implicit-check-not=__powidf2 --input-file=%t/lk.win.err
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/helpers-linked.ll -o /dev/null 2> %t/lk.win.o0.err
; RUN: FileCheck %s --check-prefix=LK %{nocrash} --input-file=%t/lk.win.o0.err
; RUN: FileCheck %s --check-prefix=NLK-WIN --input-file=%t/lk.win.o0.err
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/helpers-linked.ll > %t/lk.lnx.il 2> /dev/null
; RUN: %{il} --check-prefixes=IL,IL-LNX --input-file=%t/lk.lnx.il
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/helpers-linked.ll > %t/lk.win.il 2> /dev/null
; RUN: %{il} --check-prefixes=IL,IL-WIN,IL-LK-WIN --input-file=%t/lk.win.il

; Objects and object-form text: no message, the helpers UNDEFINED.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/helpers.ll \
; RUN:   -o %t/sa.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/helpers.ll \
; RUN:   -o %t/sa.win.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj -verify-machineinstrs < %t/helpers.ll \
; RUN:   -o %t/sa.win.o0.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/helpers-linked.ll \
; RUN:   -o %t/lk.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj -verify-machineinstrs < %t/helpers-linked.ll \
; RUN:   -o %t/lk.win.o0.o 2>&1 | count 0
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/sa.lnx.o \
; RUN:   | FileCheck %s --match-full-lines --strict-whitespace --check-prefixes=OBJ,OBJ-LNX %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/sa.win.o \
; RUN:   | FileCheck %s --match-full-lines --strict-whitespace --check-prefixes=OBJ,OBJ-WIN %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/sa.win.o0.o \
; RUN:   | FileCheck %s --match-full-lines --strict-whitespace --check-prefixes=OBJ,OBJ-WIN %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/lk.lnx.o \
; RUN:   | FileCheck %s --match-full-lines --strict-whitespace --check-prefixes=OBJ,OBJ-LNX %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/lk.win.o0.o \
; RUN:   | FileCheck %s --match-full-lines --strict-whitespace --check-prefixes=OBJ,OBJ-WIN %}
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form < %t/helpers.ll -o %t/sa.lnx.of.il 2>&1 \
; RUN:   | count 0
; RUN: FileCheck %s --check-prefixes=OF,OF-LNX --implicit-check-not=pinvokeimpl --input-file=%t/sa.lnx.of.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-object-form < %t/helpers-linked.ll \
; RUN:   -o %t/lk.win.of.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=OF,OF-WIN --implicit-check-not=pinvokeimpl --input-file=%t/lk.win.of.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/sa.lnx.of.dll %t/sa.lnx.of.il %}

; T-C for the i128 nodes of ReplaceNodeResults, in every output mode (text,
; objects and object-form text). The library calls and the declared function
; of ti.ll get it in text output only and give no diagnostic in objects; they
; run with the split modules below.
; DEFINE: %{tc} = FileCheck %s --check-prefix=TC %{nocrash} --implicit-check-not=error: --implicit-check-not=warning:
; DEFINE: %{tcllc} = llc -verify-machineinstrs -expand-div-rem-bits=128 -expand-fp-convert-bits=128
; RUN: not %{tcllc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/i128.ll -o /dev/null 2>&1 | %{tc}
; RUN: not %{tcllc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/i128.ll -o /dev/null 2>&1 | %{tc}
; RUN: not %{tcllc} -mtriple=x86_64-pc-windows-msvc-msil < %t/i128.ll -o /dev/null 2>&1 | %{tc}
; RUN: not %{tcllc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/i128.ll -o /dev/null 2>&1 | %{tc}
; RUN: not %{tcllc} -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj < %t/i128.ll -o %t/i128.lnx.o 2>&1 | %{tc}
; RUN: not %{tcllc} -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj < %t/i128.ll -o %t/i128.win.o 2>&1 \
; RUN:   | %{tc}
; RUN: not %{tcllc} -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form < %t/i128.ll -o /dev/null 2>&1 | %{tc}
; RUN: not %{tcllc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/i128-linked.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=TCLK %{nocrash} --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not %{tcllc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/i128-linked.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=TCLK %{nocrash} --implicit-check-not=error: --implicit-check-not=warning:

; The split modules: %{split} runs %{smod}.ll through %{sllc} (%{llc}, and
; %{tcllc} for the ti modules) on both triples at -O2 and -O0 and checks each
; output as above: its errors as a group (%{spfx}), every error line counted
; (N%{spfx}), and %{ban} with %{sbans} over all of it. %{sin} is the start of
; an --input-file option, completed by the output's suffix.
; DEFINE: %{sllc} = %{llc}
; DEFINE: %{smod} =
; DEFINE: %{spfx} =
; DEFINE: %{sbans} =
; DEFINE: %{sin} = --input-file=%t/%{smod}
; DEFINE: %{sgrp} = FileCheck %s --check-prefix=%{spfx} %{nocrash}
; DEFINE: %{scnt} = FileCheck %s --check-prefix=N%{spfx}
; DEFINE: %{sban} = %{ban} %{sbans}
; DEFINE: %{split} = not %{sllc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/%{smod}.ll -o /dev/null \
; DEFINE:     2> %t/%{smod}.lnx.err && \
; DEFINE:   %{sgrp} %{sin}.lnx.err && %{scnt} %{sin}.lnx.err && %{sban} %{sin}.lnx.err && \
; DEFINE:   not %{sllc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/%{smod}.ll -o /dev/null \
; DEFINE:     2> %t/%{smod}.lnx.o0.err && \
; DEFINE:   %{sgrp} %{sin}.lnx.o0.err && %{scnt} %{sin}.lnx.o0.err && %{sban} %{sin}.lnx.o0.err && \
; DEFINE:   not %{sllc} -mtriple=x86_64-pc-windows-msvc-msil < %t/%{smod}.ll -o /dev/null \
; DEFINE:     2> %t/%{smod}.win.err && \
; DEFINE:   %{sgrp} %{sin}.win.err && %{scnt} %{sin}.win.err && %{sban} %{sin}.win.err && \
; DEFINE:   not %{sllc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/%{smod}.ll -o /dev/null \
; DEFINE:     2> %t/%{smod}.win.o0.err && \
; DEFINE:   %{sgrp} %{sin}.win.o0.err && %{scnt} %{sin}.win.o0.err && %{sban} %{sin}.win.o0.err
; REDEFINE: %{smod} = clib
; REDEFINE: %{spfx} = CLIB
; REDEFINE: %{sbans} = --implicit-check-not="runtime helper" --implicit-check-not="was not resolved"
; RUN: %{split}
; REDEFINE: %{smod} = clib-linked
; REDEFINE: %{spfx} = CLIBLK
; REDEFINE: %{sbans} = --implicit-check-not="runtime helper" --implicit-check-not="Phase-1 import"
; RUN: %{split}
; REDEFINE: %{smod} = rtlib
; REDEFINE: %{spfx} = RTLIB
; REDEFINE: %{sbans} = --implicit-check-not="Phase-1 import" --implicit-check-not="was not resolved"
; RUN: %{split}
; REDEFINE: %{smod} = rtlib-linked
; RUN: %{split}
; REDEFINE: %{smod} = hc3
; REDEFINE: %{spfx} = HC3
; RUN: %{split}
; REDEFINE: %{smod} = hc3-linked
; RUN: %{split}
; REDEFINE: %{smod} = cx
; REDEFINE: %{spfx} = CX
; REDEFINE: %{sbans} = --implicit-check-not="Phase-1 import" --implicit-check-not="was not resolved" \
; REDEFINE:   --implicit-check-not="128-bit integer helpers"
; RUN: %{split}
; REDEFINE: %{smod} = cx-linked
; REDEFINE: %{spfx} = CXLK
; RUN: %{split}

; The ti modules, through %{tcllc}: T-C in text output, no T-A. Their objects,
; one per triple, give no diagnostic and list the three helpers as UNDEFINED.
; REDEFINE: %{sllc} = %{tcllc}
; REDEFINE: %{smod} = ti
; REDEFINE: %{spfx} = TI
; REDEFINE: %{sbans} = --implicit-check-not="needs the helper archive" --implicit-check-not="Phase-1 import" \
; REDEFINE:   --implicit-check-not="was not resolved"
; RUN: %{split}
; REDEFINE: %{smod} = ti-linked
; REDEFINE: %{spfx} = TILK
; RUN: %{split}
; RUN: %{tcllc} -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj < %t/ti.ll -o %t/ti.lnx.o 2>&1 | count 0
; RUN: %{tcllc} -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj < %t/ti.ll -o %t/ti.win.o0.o 2>&1 | count 0
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/ti.lnx.o | FileCheck %s --check-prefix=TIOBJ %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/ti.win.o0.o | FileCheck %s --check-prefix=TIOBJ %}

;--- helpers.ll
source_filename = "helpers.c"

; T-A for the two library calls (Linux: two, windows-msvc: one), T-A for
; __muldc3 and __msil_example, T-B for __atomic_fetch_add_4, D14's text for
; __start_x; the barrier is no error.
; SA-DAG:     {{^}}error: {{.*}}in function f16_to_f32 {{.*}}: runtime helper '__extendhfsf2' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; SA-DAG:     {{^}}error: runtime helper '__muldc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; SA-DAG:     {{^}}error: runtime helper '__atomic_fetch_add_4' is an atomic operation that only msil-ld links{{$}}
; SA-DAG:     {{^}}error: runtime helper '__msil_example' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; SA-DAG:     {{^}}error: undefined function '__start_x' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; SA-LNX-DAG: {{^}}error: {{.*}}in function powi {{.*}}: runtime helper '__powidf2' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; BAN:        {{^}}error:

; NSA-LNX-COUNT-6: {{^}}error: {{.+}}
; NSA-LNX-NOT:     {{^}}error:
; NSA-WIN-COUNT-5: {{^}}error: {{.+}}
; NSA-WIN-NOT:     {{^}}error:

; The IL of both modules: no call in f16_to_f32, none in powi on Linux and
; pow's on windows-msvc, the barrier's call and its trailer definition, and
; pow's P/Invoke (D14's in the standalone module, the !msil.imports binding
; in the linked one).
; IL-LABEL:  {{^}}.method public static float32 'f16_to_f32'(int32) cil managed{{$}}
; IL-NOT:    {{^}}  call
; IL:        {{^}}} // end of method 'f16_to_f32'{{$}}
; IL-LABEL:  {{^}}.method public static float64 'powi'(float64, int32) cil managed{{$}}
; IL-LNX-NOT: {{^}}  call
; IL-WIN:    {{^}}  call float64 'pow'(float64, float64){{$}}
; IL:        {{^}}} // end of method 'powi'{{$}}
; IL-LABEL:  {{^}}.method public static void 'barrier'() cil managed{{$}}
; IL:        {{^}}  call void '__msil_compiler_barrier'(){{$}}
; IL:        {{^}}} // end of method 'barrier'{{$}}
; IL-SA-WIN: {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" nomangle cdecl) float64 'pow'(float64, float64) cil managed preservesig {}{{$}}
; IL-LK-WIN: {{^}}.method assembly hidebysig static pinvokeimpl("api-ms-win-crt-math-l1-1-0.dll" nomangle cdecl) float64 'pow'(float64, float64) cil managed preservesig {}{{$}}
; IL:        {{^}}.method assembly static void '__msil_compiler_barrier'() cil managed noinlining{{$}}

; The objects of both modules: the helpers and the barrier are UNDEFINED.
; OBJ-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; OBJ-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; OBJ-NEXT:Symbol 0 DATA GLOBAL DEFAULT 'counter' field=0x04000001 segment=0 offset=0 size=4
; OBJ-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT UNDEFINED '__muldc3'
; OBJ-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT UNDEFINED '__msil_example'
; OBJ-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED '__msil_compiler_barrier'
; OBJ-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT UNDEFINED '__start_x'
; OBJ-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT 'f16_to_f32' method=0x06000001
; OBJ-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT 'cmul' method=0x06000002
; OBJ-NEXT:Symbol 7 FUNCTION GLOBAL DEFAULT 'powi' method=0x06000003
; OBJ-NEXT:Symbol 8 FUNCTION GLOBAL DEFAULT 'atomic_add' method=0x06000004
; OBJ-NEXT:Symbol 9 FUNCTION GLOBAL DEFAULT 'example' method=0x06000005
; OBJ-NEXT:Symbol 10 FUNCTION GLOBAL DEFAULT 'barrier' method=0x06000006
; OBJ-NEXT:Symbol 11 FUNCTION GLOBAL DEFAULT 'start' method=0x06000007
; OBJ-NEXT:Symbol 12 FUNCTION GLOBAL DEFAULT UNDEFINED '__atomic_fetch_add_4'
; OBJ-NEXT:Symbol 13 FUNCTION GLOBAL DEFAULT UNDEFINED '__extendhfsf2'
; OBJ-LNX-NEXT:Symbol 14 FUNCTION GLOBAL DEFAULT UNDEFINED '__powidf2'
; OBJ-WIN-NEXT:Symbol 14 FUNCTION GLOBAL DEFAULT UNDEFINED 'pow'
; OBJ-NEXT:Segment 0 '.bss' align=4 size=4 flags=ZERO
; OBJ-NEXT:Signature 0x0A000002 '__muldc3' NATIVE_INCOMPATIBLE
; OBJ-NOT:{{.}}

; The object-form text: each helper is a call of its '<msil>Symbols' member.
; OF-LABEL: {{^}}.method public static float32 'f16_to_f32'(int32) cil managed{{$}}
; OF:       {{^}}  call float32 [msil.extern]'<msil>Symbols'::'__extendhfsf2'(int32){{$}}
; OF-LABEL: {{^}}.method public static float64 'cmul'(float64, float64, float64, float64) cil managed{{$}}
; OF:       {{^}}  call void [msil.extern]'<msil>Symbols'::'__muldc3'(native int, float64, float64, float64, float64){{$}}
; OF-LABEL: {{^}}.method public static float64 'powi'(float64, int32) cil managed{{$}}
; OF-LNX:   {{^}}  call float64 [msil.extern]'<msil>Symbols'::'__powidf2'(float64, int32){{$}}
; OF-WIN:   {{^}}  call float64 [msil.extern]'<msil>Symbols'::'pow'(float64, float64){{$}}
; OF-LABEL: {{^}}.method public static int32 'atomic_add'() cil managed{{$}}
; OF:       {{^}}  call int32 [msil.extern]'<msil>Symbols'::'__atomic_fetch_add_4'(native int, int32, int32){{$}}
; OF-LABEL: {{^}}.method public static void 'example'(int32) cil managed{{$}}
; OF:       {{^}}  call void [msil.extern]'<msil>Symbols'::'__msil_example'(int32){{$}}
; OF-LABEL: {{^}}.method public static void 'barrier'() cil managed{{$}}
; OF:       {{^}}  call void [msil.extern]'<msil>Symbols'::'__msil_compiler_barrier'(){{$}}
; OF-LABEL: {{^}}.method public static void 'start'() cil managed{{$}}
; OF:       {{^}}  call void [msil.extern]'<msil>Symbols'::'__start_x'(){{$}}
; OF-NOT:   {{^}}.method {{.*}}'__msil_compiler_barrier'

@counter = global i32 0, align 4

; A double _Complex multiplication, as clang emits it for x86_64 Linux.
declare { double, double } @__muldc3(double, double, double, double)
declare void @__msil_example(i32)
declare void @__msil_compiler_barrier()
declare void @__start_x()
declare double @llvm.powi.f64.i32(double, i32)

; half arrives as its i16 bits (fp-expand.ll's bf16.ll).
define float @f16_to_f32(i16 %b) {
  %h = bitcast i16 %b to half
  %f = fpext half %h to float
  ret float %f
}

define double @cmul(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__muldc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  ret double %re
}

define double @powi(double %x, i32 %n) {
  %r = call double @llvm.powi.f64.i32(double %x, i32 %n)
  ret double %r
}

define i32 @atomic_add() {
  %old = atomicrmw add ptr @counter, i32 2 seq_cst
  ret i32 %old
}

define void @example(i32 %x) {
  call void @__msil_example(i32 %x)
  ret void
}

define void @barrier() {
  call void @__msil_compiler_barrier()
  ret void
}

define void @start() {
  call void @__start_x()
  ret void
}

;--- helpers-linked.ll
; helpers.ll after msil-ld's import-annotation pass: the "msil-linked" flag,
; and pow bound to windows-msvc's C runtime math DLL. __start_x is an
; ordinary undefined function that nothing bound.
source_filename = "ld-temp.o"

; LK-DAG:     {{^}}error: runtime helper '__extendhfsf2' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; LK-DAG:     {{^}}error: runtime helper '__muldc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; LK-DAG:     {{^}}error: runtime helper '__atomic_fetch_add_4' is an atomic operation that only msil-ld links{{$}}
; LK-DAG:     {{^}}error: runtime helper '__msil_example' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; LK-DAG:     {{^}}error: undefined function '__start_x' was not resolved by msil-ld{{$}}
; LK-LNX-DAG: {{^}}error: runtime helper '__powidf2' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}

; NLK-LNX-COUNT-6: {{^}}error: {{.+}}
; NLK-LNX-NOT:     {{^}}error:
; NLK-WIN-COUNT-5: {{^}}error: {{.+}}
; NLK-WIN-NOT:     {{^}}error:

@counter = global i32 0, align 4

declare { double, double } @__muldc3(double, double, double, double)
declare void @__msil_example(i32)
declare void @__msil_compiler_barrier()
declare void @__start_x()
declare double @llvm.powi.f64.i32(double, i32)

define float @f16_to_f32(i16 %b) {
  %h = bitcast i16 %b to half
  %f = fpext half %h to float
  ret float %f
}

define double @cmul(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__muldc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  ret double %re
}

define double @powi(double %x, i32 %n) {
  %r = call double @llvm.powi.f64.i32(double %x, i32 %n)
  ret double %r
}

define i32 @atomic_add() {
  %old = atomicrmw add ptr @counter, i32 2 seq_cst
  ret i32 %old
}

define void @example(i32 %x) {
  call void @__msil_example(i32 %x)
  ret void
}

define void @barrier() {
  call void @__msil_compiler_barrier()
  ret void
}

define void @start() {
  call void @__start_x()
  ret void
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-linked", i32 1}

!msil.imports = !{!10}
!10 = !{!"pow", !"api-ms-win-crt-math-l1-1-0.dll", !"pow", i32 0}

;--- i128.ll
source_filename = "i128.c"

; TC: {{^}}error: {{.*}}in function sdiv128 {{.*}}: runtime helper '__divti3' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}
; TC: {{^}}error: {{.*}}in function udiv128 {{.*}}: runtime helper '__udivti3' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}
; TC: {{^}}error: {{.*}}in function srem128 {{.*}}: runtime helper '__modti3' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}
; TC: {{^}}error: {{.*}}in function urem128 {{.*}}: runtime helper '__umodti3' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}
; TC: {{^}}error: {{.*}}in function fptosi128 {{.*}}: runtime helper '__fixdfti' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}
; TC: {{^}}error: {{.*}}in function fptoui128 {{.*}}: runtime helper '__fixunssfti' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}

define i128 @sdiv128(i128 %a, i128 %b) {
  %r = sdiv i128 %a, %b
  ret i128 %r
}

define i128 @udiv128(i128 %a, i128 %b) {
  %r = udiv i128 %a, %b
  ret i128 %r
}

define i128 @srem128(i128 %a, i128 %b) {
  %r = srem i128 %a, %b
  ret i128 %r
}

define i128 @urem128(i128 %a, i128 %b) {
  %r = urem i128 %a, %b
  ret i128 %r
}

define i128 @fptosi128(double %x) {
  %r = fptosi double %x to i128
  ret i128 %r
}

define i128 @fptoui128(float %x) {
  %r = fptoui float %x to i128
  ret i128 %r
}

;--- i128-linked.ll
source_filename = "ld-temp.o"

; TCLK: {{^}}error: {{.*}}in function sdiv128 {{.*}}: runtime helper '__divti3' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}
; TCLK: {{^}}error: {{.*}}in function fptoui128 {{.*}}: runtime helper '__fixunssfti' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}

define i128 @sdiv128(i128 %a, i128 %b) {
  %r = sdiv i128 %a, %b
  ret i128 %r
}

define i128 @fptoui128(float %x) {
  %r = fptoui float %x to i128
  ret i128 %r
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-linked", i32 1}

;--- ti.ll
; Calls of helpers with compiler-rt's ti mode that reach the AsmPrinter or
; LowerCall instead of ReplaceNodeResults: the two conversions from i128 stay
; library calls under -expand-fp-convert-bits=128 (LowerCall reports each with
; its function), and __divti3 is declared and called (the AsmPrinter reports
; it at the end of the module, a plain line). T-C for each, and nothing else.
source_filename = "ti.c"

; TI-DAG: {{^}}error: {{.*}}in function sitofp128 {{.*}}: runtime helper '__floattidf' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}
; TI-DAG: {{^}}error: {{.*}}in function uitofp128 {{.*}}: runtime helper '__floatuntisf' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}
; TI-DAG: {{^}}error: runtime helper '__divti3' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}

; NTI-COUNT-3: {{^}}error: {{.+}}
; NTI-NOT:     {{^}}error:

; The objects: each helper is an UNDEFINED FUNCTION symbol.
; TIOBJ-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT UNDEFINED '__floattidf'{{$}}
; TIOBJ-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT UNDEFINED '__floatuntisf'{{$}}
; TIOBJ-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT UNDEFINED '__divti3'{{$}}

declare i128 @__divti3(i128, i128)

define double @sitofp128(i128 %x) {
  %r = sitofp i128 %x to double
  ret double %r
}

define float @uitofp128(i128 %x) {
  %r = uitofp i128 %x to float
  ret float %r
}

define i128 @divti3(i128 %a, i128 %b) {
  %r = call i128 @__divti3(i128 %a, i128 %b)
  ret i128 %r
}

;--- ti-linked.ll
; ti.ll linked: the same three T-C texts, each a plain line.
source_filename = "ld-temp.o"

; TILK-DAG: {{^}}error: runtime helper '__floattidf' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}
; TILK-DAG: {{^}}error: runtime helper '__floatuntisf' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}
; TILK-DAG: {{^}}error: runtime helper '__divti3' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers{{$}}

; NTILK-COUNT-3: {{^}}error: {{.+}}
; NTILK-NOT:     {{^}}error:

declare i128 @__divti3(i128, i128)

define double @sitofp128(i128 %x) {
  %r = sitofp i128 %x to double
  ret double %r
}

define float @uitofp128(i128 %x) {
  %r = uitofp i128 %x to float
  ret float %r
}

define i128 @divti3(i128 %a, i128 %b) {
  %r = call i128 @__divti3(i128 %a, i128 %b)
  ret i128 %r
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-linked", i32 1}

;--- clib.ll
; The C library's '__' names of the two triples' RuntimeLibcalls sets,
; declared and called: D14's text for each, on both triples.
source_filename = "clib.c"

; CLIB-DAG: {{^}}error: undefined function '__exp_finite' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; CLIB-DAG: {{^}}error: undefined function '__powf_finite' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; CLIB-DAG: {{^}}error: undefined function '__memcpy_chk' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; CLIB-DAG: {{^}}error: undefined function '__memset_chk' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; CLIB-DAG: {{^}}error: undefined function '__stack_chk_fail' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; CLIB-DAG: {{^}}error: undefined function '__chkstk' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; CLIB-DAG: {{^}}error: undefined function '__security_check_cookie' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}

; NCLIB-COUNT-7: {{^}}error: {{.+}}
; NCLIB-NOT:     {{^}}error:

declare double @__exp_finite(double)
declare float @__powf_finite(float, float)
declare ptr @__memcpy_chk(ptr, ptr, i64, i64)
declare ptr @__memset_chk(ptr, i32, i64, i64)
declare void @__stack_chk_fail()
declare void @__chkstk()
declare void @__security_check_cookie(i64)

define double @exp_finite(double %x) {
  %r = call double @__exp_finite(double %x)
  ret double %r
}

define float @powf_finite(float %x, float %y) {
  %r = call float @__powf_finite(float %x, float %y)
  ret float %r
}

; The length and the object size differ.
define ptr @memcpy_chk(ptr %d, ptr %s, i64 %n, i64 %size) {
  %r = call ptr @__memcpy_chk(ptr %d, ptr %s, i64 %n, i64 %size)
  ret ptr %r
}

define ptr @memset_chk(ptr %d, i32 %c, i64 %n, i64 %size) {
  %r = call ptr @__memset_chk(ptr %d, i32 %c, i64 %n, i64 %size)
  ret ptr %r
}

define void @stack_chk_fail() {
  call void @__stack_chk_fail()
  ret void
}

define void @chkstk() {
  call void @__chkstk()
  ret void
}

define void @security_check_cookie(i64 %c) {
  call void @__security_check_cookie(i64 %c)
  ret void
}

;--- clib-linked.ll
; clib.ll linked: nothing binds these names, so each is not resolved.
source_filename = "ld-temp.o"

; CLIBLK-DAG: {{^}}error: undefined function '__exp_finite' was not resolved by msil-ld{{$}}
; CLIBLK-DAG: {{^}}error: undefined function '__powf_finite' was not resolved by msil-ld{{$}}
; CLIBLK-DAG: {{^}}error: undefined function '__memcpy_chk' was not resolved by msil-ld{{$}}
; CLIBLK-DAG: {{^}}error: undefined function '__memset_chk' was not resolved by msil-ld{{$}}
; CLIBLK-DAG: {{^}}error: undefined function '__stack_chk_fail' was not resolved by msil-ld{{$}}
; CLIBLK-DAG: {{^}}error: undefined function '__chkstk' was not resolved by msil-ld{{$}}
; CLIBLK-DAG: {{^}}error: undefined function '__security_check_cookie' was not resolved by msil-ld{{$}}

; NCLIBLK-COUNT-7: {{^}}error: {{.+}}
; NCLIBLK-NOT:     {{^}}error:

declare double @__exp_finite(double)
declare float @__powf_finite(float, float)
declare ptr @__memcpy_chk(ptr, ptr, i64, i64)
declare ptr @__memset_chk(ptr, i32, i64, i64)
declare void @__stack_chk_fail()
declare void @__chkstk()
declare void @__security_check_cookie(i64)

define double @exp_finite(double %x) {
  %r = call double @__exp_finite(double %x)
  ret double %r
}

define float @powf_finite(float %x, float %y) {
  %r = call float @__powf_finite(float %x, float %y)
  ret float %r
}

define ptr @memcpy_chk(ptr %d, ptr %s, i64 %n, i64 %size) {
  %r = call ptr @__memcpy_chk(ptr %d, ptr %s, i64 %n, i64 %size)
  ret ptr %r
}

define ptr @memset_chk(ptr %d, i32 %c, i64 %n, i64 %size) {
  %r = call ptr @__memset_chk(ptr %d, i32 %c, i64 %n, i64 %size)
  ret ptr %r
}

define void @stack_chk_fail() {
  call void @__stack_chk_fail()
  ret void
}

define void @chkstk() {
  call void @__chkstk()
  ret void
}

define void @security_check_cookie(i64 %c) {
  call void @__security_check_cookie(i64 %c)
  ret void
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-linked", i32 1}

;--- rtlib.ll
; A RuntimeLibcalls name that the module declares and calls itself (fpext
; from half arrives as its library call in fp-expand.ll and helpers.ll).
source_filename = "rtlib.c"

; RTLIB-DAG: {{^}}error: runtime helper '__extendhfsf2' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}

; NRTLIB-COUNT-1: {{^}}error: {{.+}}
; NRTLIB-NOT:     {{^}}error:

declare float @__extendhfsf2(i32)

define float @extendhfsf2(i32 %h) {
  %f = call float @__extendhfsf2(i32 %h)
  ret float %f
}

;--- rtlib-linked.ll
; rtlib.ll linked; the same error (RTLIB, NRTLIB).
source_filename = "ld-temp.o"

declare float @__extendhfsf2(i32)

define float @extendhfsf2(i32 %h) {
  %f = call float @__extendhfsf2(i32 %h)
  ret float %f
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-linked", i32 1}

;--- hc3.ll
; _Float16 _Complex multiplication and division with -fexcess-precision=16,
; as clang declares the helpers for x86_64 Linux; the arguments pass straight
; through, so no half arithmetic adds a library call.
source_filename = "hc3.c"

; HC3-DAG: {{^}}error: runtime helper '__mulhc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; HC3-DAG: {{^}}error: runtime helper '__divhc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}

; NHC3-COUNT-2: {{^}}error: {{.+}}
; NHC3-NOT:     {{^}}error:

declare <2 x half> @__mulhc3(half, half, half, half)
declare <2 x half> @__divhc3(half, half, half, half)

define <2 x half> @mulhc3(half %a, half %b, half %c, half %d) {
  %r = call <2 x half> @__mulhc3(half %a, half %b, half %c, half %d)
  ret <2 x half> %r
}

define <2 x half> @divhc3(half %a, half %b, half %c, half %d) {
  %r = call <2 x half> @__divhc3(half %a, half %b, half %c, half %d)
  ret <2 x half> %r
}

;--- hc3-linked.ll
; hc3.ll linked, with the declarations clang emits for windows-msvc (the
; result in an int32); the same errors (HC3, NHC3).
source_filename = "ld-temp.o"

declare i32 @__mulhc3(half, half, half, half)
declare i32 @__divhc3(half, half, half, half)

define i32 @mulhc3(half %a, half %b, half %c, half %d) {
  %r = call i32 @__mulhc3(half %a, half %b, half %c, half %d)
  ret i32 %r
}

define i32 @divhc3(half %a, half %b, half %c, half %d) {
  %r = call i32 @__divhc3(half %a, half %b, half %c, half %d)
  ret i32 %r
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-linked", i32 1}

;--- cx.ll
; The complex helpers that no other llc test pins, declared and called, as
; clang declares __divdc3 and __mulsc3 for x86_64 Linux; the x87 and binary128
; ones take double operands here, since the name alone decides the class.
; T-A for each, on both triples.
source_filename = "cx.c"

; CX-DAG: {{^}}error: runtime helper '__divdc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; CX-DAG: {{^}}error: runtime helper '__mulsc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; CX-DAG: {{^}}error: runtime helper '__mulxc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; CX-DAG: {{^}}error: runtime helper '__divxc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; CX-DAG: {{^}}error: runtime helper '__multc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; CX-DAG: {{^}}error: runtime helper '__divtc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}

; NCX-COUNT-6: {{^}}error: {{.+}}
; NCX-NOT:     {{^}}error:

declare { double, double } @__divdc3(double, double, double, double)
declare <2 x float> @__mulsc3(float, float, float, float)
declare { double, double } @__mulxc3(double, double, double, double)
declare { double, double } @__divxc3(double, double, double, double)
declare { double, double } @__multc3(double, double, double, double)
declare { double, double } @__divtc3(double, double, double, double)

define double @divdc3(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__divdc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  ret double %re
}

define float @mulsc3(float %a, float %b, float %c, float %d) {
  %r = call <2 x float> @__mulsc3(float %a, float %b, float %c, float %d)
  %re = extractelement <2 x float> %r, i64 0
  ret float %re
}

define double @mulxc3(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__mulxc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  ret double %re
}

define double @divxc3(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__divxc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  ret double %re
}

define double @multc3(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__multc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  ret double %re
}

define double @divtc3(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__divtc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  ret double %re
}

;--- cx-linked.ll
; cx.ll linked, and __divsc3 (call-errors.ll pins it in a standalone module
; only): T-A for each of the seven names.
source_filename = "ld-temp.o"

; CXLK-DAG: {{^}}error: runtime helper '__divdc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; CXLK-DAG: {{^}}error: runtime helper '__mulsc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; CXLK-DAG: {{^}}error: runtime helper '__divsc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; CXLK-DAG: {{^}}error: runtime helper '__mulxc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; CXLK-DAG: {{^}}error: runtime helper '__divxc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; CXLK-DAG: {{^}}error: runtime helper '__multc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}
; CXLK-DAG: {{^}}error: runtime helper '__divtc3' needs the helper archive clang_rt.builtins, which only msil-ld links{{$}}

; NCXLK-COUNT-7: {{^}}error: {{.+}}
; NCXLK-NOT:     {{^}}error:

declare { double, double } @__divdc3(double, double, double, double)
declare <2 x float> @__mulsc3(float, float, float, float)
declare <2 x float> @__divsc3(float, float, float, float)
declare { double, double } @__mulxc3(double, double, double, double)
declare { double, double } @__divxc3(double, double, double, double)
declare { double, double } @__multc3(double, double, double, double)
declare { double, double } @__divtc3(double, double, double, double)

define double @divdc3(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__divdc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  ret double %re
}

define float @mulsc3(float %a, float %b, float %c, float %d) {
  %r = call <2 x float> @__mulsc3(float %a, float %b, float %c, float %d)
  %re = extractelement <2 x float> %r, i64 0
  ret float %re
}

define float @divsc3(float %a, float %b, float %c, float %d) {
  %r = call <2 x float> @__divsc3(float %a, float %b, float %c, float %d)
  %re = extractelement <2 x float> %r, i64 0
  ret float %re
}

define double @mulxc3(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__mulxc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  ret double %re
}

define double @divxc3(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__divxc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  ret double %re
}

define double @multc3(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__multc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  ret double %re
}

define double @divtc3(double %a, double %b, double %c, double %d) {
  %r = call { double, double } @__divtc3(double %a, double %b, double %c, double %d)
  %re = extractvalue { double, double } %r, 0
  ret double %re
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-linked", i32 1}
