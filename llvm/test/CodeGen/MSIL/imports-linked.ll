; The import modes of the backend (ImplementationPlan.md 5.10.13, M15.b;
; decision D14 as amended by the section 12 row of 2026-10-21 for M15).
;
; - Linked text. msil-ld's import-annotation pass (MSILLinkHooksPass, 5.9.13)
;   sets the module flag "msil-linked" (i32 1, Override) on the merged LTO
;   module, puts "msil-import-module"/"msil-import-name" on every natively
;   bound declaration, and lists the library functions that the module neither
;   defines nor declares but that a native library binds in the named metadata
;   !msil.imports, one operand !{!"<symbol>", !"<module>", !"<entry>", i32 0}
;   each (kind 0: a function; kind 1, data, is M20's). In such a module the
;   backend never consults the frozen standalone table of D14: an undefined
;   function or a library call takes module and entry from its
;   "msil-import-module" attribute (entry: "msil-import-name", else the name),
;   else from its kind-0 !msil.imports operand, and anything else is an error,
;   "undefined function '<name>' was not resolved by msil-ld" or
;   "library call '<name>' was not resolved by msil-ld". A runtime helper
;   (a '__' name: compiler-rt's, or the __atomic_* functions that AtomicExpand
;   calls for atomic operations) keeps its phase-3 text, "runtime helper
;   '<name>' is not available for MSIL yet (phase 3)", even when
;   !msil.imports lists it, and a library call whose function the module
;   defines calls that definition. Each of these texts is a plain
;   'error: <text>' line (LLVMContext::emitError), whether LowerCall reports
;   it for a call or the AsmPrinter for a declaration.
;   Linked P/Invokes have 'assembly' access (5.10.11) and, from M18 on,
;   'nomangle':
;     .method assembly hidebysig static pinvokeimpl("<module>" [as "<entry>"] nomangle cdecl) <ret> '<name>'(<params>) cil managed preservesig {}
;   with 'as "<entry>"' only when the entry differs from the IL name. Module
;   and entry strings are copied verbatim, whatever the triple (Q13: an
;   ordinal entry '#9', a DLL name with its suffix, an ELF DT_SONAME). The
;   D11 byte order mark also covers the !msil.imports strings. A natively
;   bound function whose signature a P/Invoke cannot pass is one of 5.10.14's
;   'native function '<name>' (<module>) ...' errors: variadic, or a value
;   that a P/Invoke cannot pass natively (here an i128 result and a byval
;   aggregate parameter). An unbound extern_weak declaration keeps M12's
;   null-plus-trap lowering ('<msil>undef_<name>', 5.9.6 step 3), also when
;   D14 lists its name (puts): its address is 0 and its direct calls call the
;   trap stub, with no P/Invoke and no not-resolved error.
; - Standalone (clang -S, llc). A module without "msil-linked", one with only
;   M14's "msil-lto", and one whose "msil-linked" is not 1 print D14's
;   P/Invokes ('public'; "libc" and "libm.so.6" on linux-gnu, "ucrtbase" on
;   windows-msvc) and ignore any !msil.imports: the same IL byte for byte. An
;   extern_weak declaration that D14 lists stays D14's P/Invoke there (M12),
;   with its address in '<msil>fn_<name>' and no trap stub.
; - The float32 ldexp and frexp rule (5.10.13, M15.b item 4) depends only on
;   RuntimeLibcalls and the triple, in both modes: linux-gnu and linux-musl
;   call 'ldexpf' and 'frexpf'; windows-msvc (the MSVC C runtime has no
;   float32 functions) and windows-gnu (msvcrt.dll lacks ldexpf and frexpf)
;   promote to 'ldexp' and 'frexp' on float64 (conv.r8 before the call,
;   conv.r4 after it). In linked mode this decides which name must be bound:
;   a module that lists only the other pair of functions gets the
;   not-resolved error for the name the rule picks.
;
; Every error names its symbol (in errors.ll, where each symbol has one use,
; exactly once), llc exits with 1 and prints neither 'LLVM ERROR' nor a crash
; report; every module without an error compiles without any message and
; assembles. x86_64 triples (D6), -O2 and -O0.

; RUN: split-file %s %t
; DEFINE: %{llc} = llc -verify-machineinstrs -asm-verbose=false
; DEFINE: %{nocrash} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; The D14 module strings, with the quotes that delimit them: never in linked
; text.
; DEFINE: %{nod14} = --implicit-check-not='("libc"' --implicit-check-not='("ucrtbase"' \
; DEFINE:   --implicit-check-not='("msvcrt"' --implicit-check-not='public hidebysig static pinvokeimpl'

; Linked text: the same P/Invokes on every triple.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/linked.ll -o %t/linked.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=LINKED %{nod14} --input-file=%t/linked.lnx.il
; RUN: FileCheck %s --check-prefix=NLINKED --input-file=%t/linked.lnx.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/linked.ll -o %t/linked.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=LINKED %{nod14} --input-file=%t/linked.win.il
; RUN: FileCheck %s --check-prefix=NLINKED --input-file=%t/linked.win.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-musl-msil < %t/linked.ll -o %t/linked.musl.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=LINKED %{nod14} --input-file=%t/linked.musl.il
; RUN: %{llc} -mtriple=x86_64-w64-windows-gnu-msil < %t/linked.ll -o %t/linked.mingw.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=LINKED %{nod14} --input-file=%t/linked.mingw.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/linked.ll -o %t/linked.lnx.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=LINKED %{nod14} --input-file=%t/linked.lnx.o0.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/linked.ll -o %t/linked.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=LINKED %{nod14} --input-file=%t/linked.win.o0.il
; "msil-linked" alone, without M14's "msil-lto", is the same linked text.
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/linked-only.ll -o %t/linked-only.win.il 2>&1 | count 0
; RUN: diff %t/linked.win.il %t/linked-only.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/linked.lnx.dll %t/linked.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/linked.win.dll %t/linked.win.il %}

; Linked-mode errors: stderr, then the IL that llc still writes to stdout.
; The errors are matched as a group (ERR), every error line is counted
; (NERR), and a third run
; (%{eban}), whose only directive is the first error line, bans Phase 1's and
; M14's texts, a warning and the crash markers everywhere in the output
; (implicit negative checks do not look between or after the matches of a
; CHECK-DAG group).
; DEFINE: %{eban} = FileCheck %s --check-prefix=EBAN %{nocrash} --implicit-check-not=warning: \
; DEFINE:   --implicit-check-not="Phase-1 import" --implicit-check-not="Phase 1 has no linker" \
; DEFINE:   --implicit-check-not="undefined symbol" --implicit-check-not="imported function" \
; DEFINE:   --implicit-check-not=okfn
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/errors.ll -o /dev/null 2> %t/errors.lnx.err
; RUN: FileCheck %s --check-prefix=ERR %{nocrash} --input-file=%t/errors.lnx.err
; RUN: FileCheck %s --check-prefix=NERR --input-file=%t/errors.lnx.err
; RUN: %{eban} --input-file=%t/errors.lnx.err
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/errors.ll -o /dev/null 2> %t/errors.win.err
; RUN: FileCheck %s --check-prefix=ERR %{nocrash} --input-file=%t/errors.win.err
; RUN: FileCheck %s --check-prefix=NERR --input-file=%t/errors.win.err
; RUN: %{eban} --input-file=%t/errors.win.err
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/errors.ll -o /dev/null 2> %t/errors.win.o0.err
; RUN: FileCheck %s --check-prefix=ERR %{nocrash} --input-file=%t/errors.win.o0.err
; RUN: FileCheck %s --check-prefix=NERR --input-file=%t/errors.win.o0.err
; RUN: %{eban} --input-file=%t/errors.win.o0.err
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/errors.ll > %t/errors.lnx.il
; RUN: FileCheck %s --check-prefix=ERR-IL %{nod14} --implicit-check-not=pinvokeimpl --input-file=%t/errors.lnx.il
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/errors.ll > %t/errors.win.il
; RUN: FileCheck %s --check-prefix=ERR-IL %{nod14} --implicit-check-not=pinvokeimpl --input-file=%t/errors.win.il

; Standalone: no flag, only msil-lto, or msil-linked = 0, with !msil.imports
; and all: D14's IL, byte-identical.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/standalone.ll -o %t/sa.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=SA,SA-LNX --input-file=%t/sa.lnx.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/lto-only.ll -o %t/lto-only.lnx.il 2>&1 | count 0
; RUN: diff %t/sa.lnx.il %t/lto-only.lnx.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/linked-zero.ll -o %t/linked-zero.lnx.il 2>&1 | count 0
; RUN: diff %t/sa.lnx.il %t/linked-zero.lnx.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/standalone.ll -o %t/sa.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=SA,SA-WIN --input-file=%t/sa.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/lto-only.ll -o %t/lto-only.win.il 2>&1 | count 0
; RUN: diff %t/sa.win.il %t/lto-only.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/linked-zero.ll -o %t/linked-zero.win.il 2>&1 | count 0
; RUN: diff %t/sa.win.il %t/linked-zero.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/standalone.ll -o %t/sa.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=SA,SA-WIN --input-file=%t/sa.win.o0.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/lto-only.ll -o %t/lto-only.win.o0.il 2>&1 | count 0
; RUN: diff %t/sa.win.o0.il %t/lto-only.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/sa.lnx.dll %t/sa.lnx.il %}

; float32 ldexp and frexp, standalone, on the four triples.
; DEFINE: %{f32} = FileCheck %s --implicit-check-not=pinvokeimpl
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/f32.ll -o %t/f32.gnu.il 2>&1 | count 0
; RUN: %{f32} --check-prefixes=F32,F32-FLOAT,F32-PUB -DMLIB=libm.so.6 --input-file=%t/f32.gnu.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-musl-msil < %t/f32.ll -o %t/f32.musl.il 2>&1 | count 0
; RUN: %{f32} --check-prefixes=F32,F32-FLOAT,F32-PUB -DMLIB=libc --input-file=%t/f32.musl.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/f32.ll -o %t/f32.msvc.il 2>&1 | count 0
; RUN: %{f32} --check-prefixes=F32,F32-DOUBLE,F64-PUB -DMLIB=ucrtbase --input-file=%t/f32.msvc.il
; RUN: %{llc} -mtriple=x86_64-w64-windows-gnu-msil < %t/f32.ll -o %t/f32.mingw.il 2>&1 | count 0
; RUN: %{f32} --check-prefixes=F32,F32-DOUBLE,F64-PUB -DMLIB=msvcrt --input-file=%t/f32.mingw.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/f32.ll -o %t/f32.gnu.o0.il 2>&1 | count 0
; RUN: %{f32} --check-prefixes=F32,F32-FLOAT,F32-PUB -DMLIB=libm.so.6 --input-file=%t/f32.gnu.o0.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/f32.ll -o %t/f32.msvc.o0.il 2>&1 | count 0
; RUN: %{f32} --check-prefixes=F32,F32-DOUBLE,F64-PUB -DMLIB=ucrtbase --input-file=%t/f32.msvc.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/f32.gnu.dll %t/f32.gnu.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/f32.msvc.dll %t/f32.msvc.il %}

; float32 ldexp and frexp, linked: f32-float.ll binds only ldexpf and frexpf,
; f32-double.ll only ldexp and frexp. Each links where the rule picks the
; functions it binds and fails with the not-resolved text elsewhere.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/f32-float.ll -o %t/f32l.gnu.il 2>&1 | count 0
; RUN: %{f32} --check-prefixes=F32,F32-FLOAT,F32-LINKED --input-file=%t/f32l.gnu.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-musl-msil < %t/f32-float.ll -o %t/f32l.musl.il 2>&1 | count 0
; RUN: %{f32} --check-prefixes=F32,F32-FLOAT,F32-LINKED --input-file=%t/f32l.musl.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/f32-double.ll -o %t/f32l.msvc.il 2>&1 | count 0
; RUN: %{f32} --check-prefixes=F32,F32-DOUBLE,F64-LINKED --input-file=%t/f32l.msvc.il
; RUN: %{llc} -mtriple=x86_64-w64-windows-gnu-msil < %t/f32-double.ll -o %t/f32l.mingw.il 2>&1 | count 0
; RUN: %{f32} --check-prefixes=F32,F32-DOUBLE,F64-LINKED --input-file=%t/f32l.mingw.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/f32-double.ll -o %t/f32l.msvc.o0.il 2>&1 | count 0
; RUN: %{f32} --check-prefixes=F32,F32-DOUBLE,F64-LINKED --input-file=%t/f32l.msvc.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/f32l.gnu.dll %t/f32l.gnu.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/f32l.msvc.dll %t/f32l.msvc.il %}
; DEFINE: %{f32err} = FileCheck %s %{nocrash} --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/f32-double.ll -o /dev/null 2>&1 \
; RUN:   | %{f32err} --check-prefix=F32-NOFLOAT
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-musl-msil < %t/f32-double.ll -o /dev/null 2>&1 \
; RUN:   | %{f32err} --check-prefix=F32-NOFLOAT
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/f32-float.ll -o /dev/null 2>&1 \
; RUN:   | %{f32err} --check-prefix=F32-NODOUBLE
; RUN: not %{llc} -mtriple=x86_64-w64-windows-gnu-msil < %t/f32-float.ll -o /dev/null 2>&1 \
; RUN:   | %{f32err} --check-prefix=F32-NODOUBLE
; RUN: not %{llc} -mtriple=x86_64-w64-windows-gnu-msil -O0 < %t/f32-float.ll -o /dev/null 2>&1 \
; RUN:   | %{f32err} --check-prefix=F32-NODOUBLE

; The byte order mark for a non-ASCII !msil.imports string.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/bom.ll -o %t/bom.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=BOM --input-file=%t/bom.lnx.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/bom.ll -o %t/bom.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=BOM --input-file=%t/bom.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/bom.win.dll %t/bom.win.il %}

; An unbound extern_weak puts, which D14 lists. Linked text (weak-linked.ll)
; never consults D14 (5.10.13): llc is silent (WEAK-QUIET), the call goes to
; the trap stub, the address is null, and there is no P/Invoke, no 'puts' and
; no '<msil>fn_puts'. Standalone (weak-sa.ll, and weak-lto.ll with only
; msil-lto) keeps D14's public P/Invoke and no stub, byte for byte.
; DEFINE: %{weakl} = FileCheck %s --check-prefix=WEAKL --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not="'puts'" --implicit-check-not="<msil>fn_puts"
; DEFINE: %{weakquiet} = FileCheck %s --check-prefix=WEAK-QUIET --allow-empty
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/weak-linked.ll -o %t/weak-linked.lnx.il 2>&1 \
; RUN:   | %{weakquiet}
; RUN: %{weakl} --input-file=%t/weak-linked.lnx.il
; RUN: FileCheck %s --check-prefix=WEAKSTUB --input-file=%t/weak-linked.lnx.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/weak-linked.ll -o %t/weak-linked.win.il 2>&1 \
; RUN:   | %{weakquiet}
; RUN: %{weakl} --input-file=%t/weak-linked.win.il
; RUN: FileCheck %s --check-prefix=WEAKSTUB --input-file=%t/weak-linked.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/weak-linked.ll -o %t/weak-linked.win.o0.il 2>&1 \
; RUN:   | %{weakquiet}
; RUN: %{weakl} --input-file=%t/weak-linked.win.o0.il
; RUN: FileCheck %s --check-prefix=WEAKSTUB --input-file=%t/weak-linked.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/weak-linked.lnx.dll %t/weak-linked.lnx.il %}
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/weak-sa.ll -o %t/weak-sa.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=WEAKSA,WEAKSA-LNX --implicit-check-not="<msil>undef_puts" \
; RUN:   --input-file=%t/weak-sa.lnx.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/weak-lto.ll -o %t/weak-lto.lnx.il 2>&1 | count 0
; RUN: diff %t/weak-sa.lnx.il %t/weak-lto.lnx.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/weak-sa.ll -o %t/weak-sa.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=WEAKSA,WEAKSA-WIN --implicit-check-not="<msil>undef_puts" \
; RUN:   --input-file=%t/weak-sa.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/weak-lto.ll -o %t/weak-lto.win.il 2>&1 | count 0
; RUN: diff %t/weak-sa.win.il %t/weak-lto.win.il

;--- linked.ll
; A merged LTO module as msil-ld leaves it (hand-annotated): puts, strdup,
; htons and on_event bound natively (attributes), mylib_add an explicit C
; import_module, the library calls sqrt, pow and trunc bound through
; !msil.imports, fmax defined here although !msil.imports lists it, and cbrt
; listed but never called. maybe is weak and bound nowhere.
source_filename = "ld-temp.o"

@hello = private constant [6 x i8] c"hello\00"
@handler = global ptr @on_event, align 8

declare i32 @puts(ptr) #0
declare ptr @strdup(ptr) #1
declare zeroext i16 @htons(i16 zeroext) #2
declare i32 @mylib_add(i32, i32) #3
declare void @on_event(i32) #4
declare extern_weak i32 @maybe()
declare double @llvm.sqrt.f64(double)
declare double @llvm.pow.f64(double, double)
declare double @llvm.trunc.f64(double)
declare double @llvm.maxnum.f64(double, double)

; LINKED-LABEL: {{^}}.method public static int32 'say'() cil managed{{$}}
; LINKED:       {{^}}  call int32 'puts'(native int){{$}}
; LINKED:       {{^}}} // end of method 'say'{{$}}
define i32 @say() {
  %r = call i32 @puts(ptr @hello)
  ret i32 %r
}

; LINKED-LABEL: {{^}}.method public static native int 'dup'(native int) cil managed{{$}}
; LINKED:       {{^}}  call native int 'strdup'(native int){{$}}
; LINKED:       {{^}}} // end of method 'dup'{{$}}
define ptr @dup(ptr %s) {
  %r = call ptr @strdup(ptr %s)
  ret ptr %r
}

; LINKED-LABEL: {{^}}.method public static int32 'swap16'(int32) cil managed{{$}}
; LINKED:       {{^}}  call int32 'htons'(int32){{$}}
; LINKED:       {{^}}} // end of method 'swap16'{{$}}
define i32 @swap16(i32 %x) {
  %t = trunc i32 %x to i16
  %r = call zeroext i16 @htons(i16 zeroext %t)
  %e = zext i16 %r to i32
  ret i32 %e
}

; LINKED-LABEL: {{^}}.method public static int32 'custom'(int32, int32) cil managed{{$}}
; LINKED:       {{^}}  call int32 'mylib_add'(int32, int32){{$}}
; LINKED:       {{^}}} // end of method 'custom'{{$}}
define i32 @custom(i32 %a, i32 %b) {
  %r = call i32 @mylib_add(i32 %a, i32 %b)
  ret i32 %r
}

; LINKED-LABEL: {{^}}.method public static float64 'root'(float64) cil managed{{$}}
; LINKED:       {{^}}  call float64 'sqrt'(float64){{$}}
; LINKED:       {{^}}} // end of method 'root'{{$}}
define double @root(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

; LINKED-LABEL: {{^}}.method public static float64 'power'(float64, float64) cil managed{{$}}
; LINKED:       {{^}}  call float64 'pow'(float64, float64){{$}}
; LINKED:       {{^}}} // end of method 'power'{{$}}
define double @power(double %x, double %y) {
  %r = call double @llvm.pow.f64(double %x, double %y)
  ret double %r
}

; LINKED-LABEL: {{^}}.method public static float64 'chop'(float64) cil managed{{$}}
; LINKED:       {{^}}  call float64 'trunc'(float64){{$}}
; LINKED:       {{^}}} // end of method 'chop'{{$}}
define double @chop(double %x) {
  %r = call double @llvm.trunc.f64(double %x)
  ret double %r
}

; The library call fmax calls the definition below: no P/Invoke.
; LINKED-LABEL: {{^}}.method public static float64 'bigger'(float64, float64) cil managed{{$}}
; LINKED:       {{^}}  call float64 'fmax'(float64, float64){{$}}
; LINKED:       {{^}}} // end of method 'bigger'{{$}}
define double @bigger(double %a, double %b) {
  %r = call double @llvm.maxnum.f64(double %a, double %b)
  ret double %r
}

; LINKED-LABEL: {{^}}.method public static float64 'fmax'(float64, float64) cil managed{{$}}
define double @fmax(double %a, double %b) {
  %c = fcmp ogt double %a, %b
  %r = select i1 %c, double %a, double %b
  ret double %r
}

; The unbound weak reference: M12's trap stub, no error and no P/Invoke.
; LINKED-LABEL: {{^}}.method public static int32 'weak_call'() cil managed{{$}}
; LINKED:       {{^}}} // end of method 'weak_call'{{$}}
define i32 @weak_call() {
  %r = call i32 @maybe()
  ret i32 %r
}

; LINKED:     {{^}}.method {{.*}}'<msil>undef_maybe'(){{.*}}
; LINKED-DAG: {{^}}.method assembly hidebysig static pinvokeimpl("libc.so.6" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; LINKED-DAG: {{^}}.method assembly hidebysig static pinvokeimpl("api-ms-win-crt-string-l1-1-0.dll" as "_strdup" nomangle cdecl) native int 'strdup'(native int) cil managed preservesig {}{{$}}
; LINKED-DAG: {{^}}.method assembly hidebysig static pinvokeimpl("WS2_32.dll" as "#9" nomangle cdecl) int32 'htons'(int32) cil managed preservesig {}{{$}}
; LINKED-DAG: {{^}}.method assembly hidebysig static pinvokeimpl("native.dll" nomangle cdecl) int32 'mylib_add'(int32, int32) cil managed preservesig {}{{$}}
; LINKED-DAG: {{^}}.method assembly hidebysig static pinvokeimpl("libevents.so.2" nomangle cdecl) void 'on_event'(int32) cil managed preservesig {}{{$}}
; LINKED-DAG: {{^}}.method assembly hidebysig static pinvokeimpl("libm.so.6" nomangle cdecl) float64 'sqrt'(float64) cil managed preservesig {}{{$}}
; LINKED-DAG: {{^}}.method assembly hidebysig static pinvokeimpl("api-ms-win-crt-math-l1-1-0.dll" nomangle cdecl) float64 'pow'(float64, float64) cil managed preservesig {}{{$}}
; LINKED-DAG: {{^}}.method assembly hidebysig static pinvokeimpl("mymath.dll" as "my_trunc" nomangle cdecl) float64 'trunc'(float64) cil managed preservesig {}{{$}}

; Eight P/Invokes, none for fmax, cbrt or maybe.
; NLINKED-COUNT-8: {{^}}.method assembly hidebysig static pinvokeimpl(
; NLINKED-NOT:     pinvokeimpl
; NLINKED-NOT:     'cbrt'

!llvm.module.flags = !{!0, !1}
!0 = !{i32 4, !"msil-linked", i32 1}
!1 = !{i32 4, !"msil-lto", i32 1}

!msil.imports = !{!10, !11, !12, !13, !14}
!10 = !{!"sqrt", !"libm.so.6", !"sqrt", i32 0}
!11 = !{!"pow", !"api-ms-win-crt-math-l1-1-0.dll", !"pow", i32 0}
!12 = !{!"trunc", !"mymath.dll", !"my_trunc", i32 0}
!13 = !{!"fmax", !"libm.so.6", !"fmax", i32 0}
!14 = !{!"cbrt", !"libm.so.6", !"cbrt", i32 0}

attributes #0 = { "msil-import-module"="libc.so.6" "msil-import-name"="puts" }
attributes #1 = { "msil-import-module"="api-ms-win-crt-string-l1-1-0.dll" "msil-import-name"="_strdup" }
attributes #2 = { "msil-import-module"="WS2_32.dll" "msil-import-name"="#9" }
attributes #3 = { "msil-import-module"="native.dll" }
attributes #4 = { "msil-import-module"="libevents.so.2" "msil-import-name"="on_event" }

;--- linked-only.ll
; linked.ll with "msil-linked" and without "msil-lto".
source_filename = "ld-temp.o"

@hello = private constant [6 x i8] c"hello\00"
@handler = global ptr @on_event, align 8

declare i32 @puts(ptr) #0
declare ptr @strdup(ptr) #1
declare zeroext i16 @htons(i16 zeroext) #2
declare i32 @mylib_add(i32, i32) #3
declare void @on_event(i32) #4
declare extern_weak i32 @maybe()
declare double @llvm.sqrt.f64(double)
declare double @llvm.pow.f64(double, double)
declare double @llvm.trunc.f64(double)
declare double @llvm.maxnum.f64(double, double)

define i32 @say() {
  %r = call i32 @puts(ptr @hello)
  ret i32 %r
}

define ptr @dup(ptr %s) {
  %r = call ptr @strdup(ptr %s)
  ret ptr %r
}

define i32 @swap16(i32 %x) {
  %t = trunc i32 %x to i16
  %r = call zeroext i16 @htons(i16 zeroext %t)
  %e = zext i16 %r to i32
  ret i32 %e
}

define i32 @custom(i32 %a, i32 %b) {
  %r = call i32 @mylib_add(i32 %a, i32 %b)
  ret i32 %r
}

define double @root(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

define double @power(double %x, double %y) {
  %r = call double @llvm.pow.f64(double %x, double %y)
  ret double %r
}

define double @chop(double %x) {
  %r = call double @llvm.trunc.f64(double %x)
  ret double %r
}

define double @bigger(double %a, double %b) {
  %r = call double @llvm.maxnum.f64(double %a, double %b)
  ret double %r
}

define double @fmax(double %a, double %b) {
  %c = fcmp ogt double %a, %b
  %r = select i1 %c, double %a, double %b
  ret double %r
}

define i32 @weak_call() {
  %r = call i32 @maybe()
  ret i32 %r
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-linked", i32 1}

!msil.imports = !{!10, !11, !12, !13, !14}
!10 = !{!"sqrt", !"libm.so.6", !"sqrt", i32 0}
!11 = !{!"pow", !"api-ms-win-crt-math-l1-1-0.dll", !"pow", i32 0}
!12 = !{!"trunc", !"mymath.dll", !"my_trunc", i32 0}
!13 = !{!"fmax", !"libm.so.6", !"fmax", i32 0}
!14 = !{!"cbrt", !"libm.so.6", !"cbrt", i32 0}

attributes #0 = { "msil-import-module"="libc.so.6" "msil-import-name"="puts" }
attributes #1 = { "msil-import-module"="api-ms-win-crt-string-l1-1-0.dll" "msil-import-name"="_strdup" }
attributes #2 = { "msil-import-module"="WS2_32.dll" "msil-import-name"="#9" }
attributes #3 = { "msil-import-module"="native.dll" }
attributes #4 = { "msil-import-module"="libevents.so.2" "msil-import-name"="on_event" }

;--- errors.ll
; A linked module with every backend error of linked mode, and one bound
; import (okfn) that still gets its P/Invoke:
; - ext, called, and dataonly, only stored in tbl: undefined functions that
;   nothing binds;
; - puts, called without an attribute or an !msil.imports operand: D14 would
;   import it, linked mode does not;
; - the library call round, which nothing binds, and sqrt, whose only
;   operand is not of kind 0;
; - __extendhfsf2, the runtime helper of fpext from half, keeps its phase-3
;   text although !msil.imports gives it a module and an entry;
; - __atomic_fetch_add_4 and __atomic_load_4, the helpers that AtomicExpand
;   calls for an atomicrmw add and an atomic load of an i32 (atomics are
;   phase 3, D36), keep the same phase-3 text and are never 'not resolved by
;   msil-ld', __atomic_load_4 although !msil.imports lists it;
; - vimp, a natively bound variadic function, whose address is taken;
; - imp128 (an i128 result) and takebyval (a byval aggregate), natively bound
;   functions whose signatures a P/Invoke cannot pass, one called and one
;   only stored in tbl.
source_filename = "ld-temp.o"

%S = type { i64, i64 }

@msg = private constant [3 x i8] c"hi\00"
@tbl = global [3 x ptr] [ptr @dataonly, ptr @vimp, ptr @takebyval], align 8
@counter = global i32 0, align 4

declare i32 @ext(i32)
declare void @dataonly()
declare i32 @puts(ptr)
declare i32 @vimp(ptr, ...) #0
declare i128 @imp128(i64) #1
declare void @takebyval(ptr byval(%S)) #1
declare i32 @okfn(i32) #2
declare double @llvm.round.f64(double)
declare double @llvm.sqrt.f64(double)

; ERR-DAG: {{^}}error: undefined function 'ext' was not resolved by msil-ld{{$}}
; ERR-DAG: {{^}}error: undefined function 'dataonly' was not resolved by msil-ld{{$}}
; ERR-DAG: {{^}}error: undefined function 'puts' was not resolved by msil-ld{{$}}
; ERR-DAG: {{^}}error: library call 'round' was not resolved by msil-ld{{$}}
; ERR-DAG: {{^}}error: library call 'sqrt' was not resolved by msil-ld{{$}}
; ERR-DAG: {{^}}error: runtime helper '__extendhfsf2' is not available for MSIL yet (phase 3){{$}}
; ERR-DAG: {{^}}error: runtime helper '__atomic_fetch_add_4' is not available for MSIL yet (phase 3){{$}}
; ERR-DAG: {{^}}error: runtime helper '__atomic_load_4' is not available for MSIL yet (phase 3){{$}}
; ERR-DAG: {{^}}error: native function 'vimp' (libc.so.6) is variadic; calls of native variadic functions are not supported for MSIL yet (phase 3){{$}}
; ERR-DAG: {{^}}error: native function 'imp128' (libbig.so.1) takes or returns a value that a P/Invoke cannot pass natively yet (phase 3){{$}}
; ERR-DAG: {{^}}error: native function 'takebyval' (libbig.so.1) takes or returns a value that a P/Invoke cannot pass natively yet (phase 3){{$}}
; EBAN: {{^}}error:

; The eleven errors above, each reported once: eleven error lines and no
; twelfth (so no 'was not resolved by msil-ld' line for a runtime helper).
; NERR-COUNT-11: {{^}}error: {{.+}}
; NERR-NOT:      {{^}}error:

; The IL keeps every method; only okfn gets a P/Invoke.
; ERR-IL-LABEL: {{^}}.method public static int32 'use_okfn'(int32) cil managed{{$}}
; ERR-IL:       {{^}}  call int32 'okfn'(int32){{$}}
; ERR-IL:       {{^}}.method assembly hidebysig static pinvokeimpl("libok.so.1" nomangle cdecl) int32 'okfn'(int32) cil managed preservesig {}{{$}}

define i32 @use_ext(i32 %x) {
  %r = call i32 @ext(i32 %x)
  ret i32 %r
}

define i32 @use_puts() {
  %r = call i32 @puts(ptr @msg)
  ret i32 %r
}

define double @use_round(double %x) {
  %r = call double @llvm.round.f64(double %x)
  ret double %r
}

define double @use_sqrt(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

define float @use_half(i16 %b) {
  %h = bitcast i16 %b to half
  %f = fpext half %h to float
  ret float %f
}

define i64 @use_imp128(i64 %x) {
  %r = call i128 @imp128(i64 %x)
  %t = trunc i128 %r to i64
  ret i64 %t
}

define i32 @use_atomic_add() {
  %old = atomicrmw add ptr @counter, i32 2 seq_cst
  ret i32 %old
}

define i32 @use_atomic_load() {
  %v = load atomic i32, ptr @counter seq_cst, align 4
  ret i32 %v
}

define i32 @use_okfn(i32 %x) {
  %r = call i32 @okfn(i32 %x)
  ret i32 %r
}

!llvm.module.flags = !{!0, !1}
!0 = !{i32 4, !"msil-linked", i32 1}
!1 = !{i32 4, !"msil-lto", i32 1}

!msil.imports = !{!10, !11, !12}
!10 = !{!"__extendhfsf2", !"libgcc_s.so.1", !"__extendhfsf2", i32 0}
!11 = !{!"sqrt", !"libm.so.6", !"sqrt", i32 1}
!12 = !{!"__atomic_load_4", !"libatomic.so.1", !"__atomic_load_4", i32 0}

attributes #0 = { "msil-import-module"="libc.so.6" "msil-import-name"="vimp" }
attributes #1 = { "msil-import-module"="libbig.so.1" }
attributes #2 = { "msil-import-module"="libok.so.1" }

;--- standalone.ll
; No flag: D14 alone decides, and !msil.imports means nothing.
source_filename = "ld-temp.o"

@hello = private constant [6 x i8] c"hello\00"

declare i32 @puts(ptr)
declare double @llvm.sqrt.f64(double)

; SA-LABEL:  {{^}}.method public static int32 'say'() cil managed{{$}}
; SA:        {{^}}  call int32 'puts'(native int){{$}}
; SA-LABEL:  {{^}}.method public static float64 'root'(float64) cil managed{{$}}
; SA:        {{^}}  call float64 'sqrt'(float64){{$}}
; SA-LNX-DAG: {{^}}.method public hidebysig static pinvokeimpl("libc" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; SA-LNX-DAG: {{^}}.method public hidebysig static pinvokeimpl("libm.so.6" nomangle cdecl) float64 'sqrt'(float64) cil managed preservesig {}{{$}}
; SA-WIN-DAG: {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; SA-WIN-DAG: {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" nomangle cdecl) float64 'sqrt'(float64) cil managed preservesig {}{{$}}
; SA-NOT:    pinvokeimpl
; SA-NOT:    assembly hidebysig

define i32 @say() {
  %r = call i32 @puts(ptr @hello)
  ret i32 %r
}

define double @root(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

!msil.imports = !{!10, !11}
!10 = !{!"sqrt", !"api-ms-win-crt-math-l1-1-0.dll", !"sqrt", i32 0}
!11 = !{!"puts", !"libc.so.6", !"puts", i32 0}

;--- lto-only.ll
; standalone.ll with M14's msil-lto flag only.
source_filename = "ld-temp.o"

@hello = private constant [6 x i8] c"hello\00"

declare i32 @puts(ptr)
declare double @llvm.sqrt.f64(double)

define i32 @say() {
  %r = call i32 @puts(ptr @hello)
  ret i32 %r
}

define double @root(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-lto", i32 1}

!msil.imports = !{!10, !11}
!10 = !{!"sqrt", !"api-ms-win-crt-math-l1-1-0.dll", !"sqrt", i32 0}
!11 = !{!"puts", !"libc.so.6", !"puts", i32 0}

;--- linked-zero.ll
; standalone.ll with "msil-linked" = 0, which is not linked mode.
source_filename = "ld-temp.o"

@hello = private constant [6 x i8] c"hello\00"

declare i32 @puts(ptr)
declare double @llvm.sqrt.f64(double)

define i32 @say() {
  %r = call i32 @puts(ptr @hello)
  ret i32 %r
}

define double @root(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-linked", i32 0}

!msil.imports = !{!10, !11}
!10 = !{!"sqrt", !"api-ms-win-crt-math-l1-1-0.dll", !"sqrt", i32 0}
!11 = !{!"puts", !"libc.so.6", !"puts", i32 0}

;--- f32.ll
; float32 ldexp and frexp (llvm.ldexp, llvm.frexp), standalone.
source_filename = "f32.c"

declare float @llvm.ldexp.f32.i32(float, i32)
declare { float, i32 } @llvm.frexp.f32.i32(float)

; F32-LABEL:        {{^}}.method public static float32 'scale'(float32, int32) cil managed{{$}}
; F32-FLOAT-NOT:    conv.r8
; F32-FLOAT:        {{^}}  call float32 'ldexpf'(float32, int32){{$}}
; F32-DOUBLE:       {{^}}  conv.r8{{$}}
; F32-DOUBLE:       {{^}}  call float64 'ldexp'(float64, int32){{$}}
; F32-DOUBLE-NEXT:  {{^}}  conv.r4{{$}}
; F32:              {{^}}} // end of method 'scale'{{$}}
define float @scale(float %x, i32 %e) {
  %r = call float @llvm.ldexp.f32.i32(float %x, i32 %e)
  ret float %r
}

; F32-LABEL:        {{^}}.method public static float32 'mantissa'(float32) cil managed{{$}}
; F32:              {{^}}  localloc{{$}}
; F32-FLOAT-NOT:    conv.r8
; F32-FLOAT:        {{^}}  call float32 'frexpf'(float32, native int){{$}}
; F32-DOUBLE:       {{^}}  conv.r8{{$}}
; F32-DOUBLE:       {{^}}  call float64 'frexp'(float64, native int){{$}}
; F32-DOUBLE-NEXT:  {{^}}  conv.r4{{$}}
; F32:              {{^}}} // end of method 'mantissa'{{$}}
define float @mantissa(float %x) {
  %fe = call { float, i32 } @llvm.frexp.f32.i32(float %x)
  %m = extractvalue { float, i32 } %fe, 0
  ret float %m
}

; F32-LABEL:        {{^}}.method public static int32 'exponent'(float32) cil managed{{$}}
; F32-FLOAT:        {{^}}  call float32 'frexpf'(float32, native int){{$}}
; F32-DOUBLE:       {{^}}  call float64 'frexp'(float64, native int){{$}}
; F32:              {{^}}  ldind.i4{{$}}
; F32:              {{^}}} // end of method 'exponent'{{$}}
define i32 @exponent(float %x) {
  %fe = call { float, i32 } @llvm.frexp.f32.i32(float %x)
  %e = extractvalue { float, i32 } %fe, 1
  ret i32 %e
}

; F32-PUB-DAG:    {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float32 'ldexpf'(float32, int32) cil managed preservesig {}{{$}}
; F32-PUB-DAG:    {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float32 'frexpf'(float32, native int) cil managed preservesig {}{{$}}
; F64-PUB-DAG:    {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float64 'ldexp'(float64, int32) cil managed preservesig {}{{$}}
; F64-PUB-DAG:    {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float64 'frexp'(float64, native int) cil managed preservesig {}{{$}}
; F32-LINKED-DAG: {{^}}.method assembly hidebysig static pinvokeimpl("libm.so.6" nomangle cdecl) float32 'ldexpf'(float32, int32) cil managed preservesig {}{{$}}
; F32-LINKED-DAG: {{^}}.method assembly hidebysig static pinvokeimpl("libm.so.6" nomangle cdecl) float32 'frexpf'(float32, native int) cil managed preservesig {}{{$}}
; F64-LINKED-DAG: {{^}}.method assembly hidebysig static pinvokeimpl("api-ms-win-crt-math-l1-1-0.dll" nomangle cdecl) float64 'ldexp'(float64, int32) cil managed preservesig {}{{$}}
; F64-LINKED-DAG: {{^}}.method assembly hidebysig static pinvokeimpl("api-ms-win-crt-math-l1-1-0.dll" nomangle cdecl) float64 'frexp'(float64, native int) cil managed preservesig {}{{$}}

;--- f32-float.ll
; f32.ll linked, with only ldexpf and frexpf bound.
source_filename = "ld-temp.o"

declare float @llvm.ldexp.f32.i32(float, i32)
declare { float, i32 } @llvm.frexp.f32.i32(float)

define float @scale(float %x, i32 %e) {
  %r = call float @llvm.ldexp.f32.i32(float %x, i32 %e)
  ret float %r
}

define float @mantissa(float %x) {
  %fe = call { float, i32 } @llvm.frexp.f32.i32(float %x)
  %m = extractvalue { float, i32 } %fe, 0
  ret float %m
}

define i32 @exponent(float %x) {
  %fe = call { float, i32 } @llvm.frexp.f32.i32(float %x)
  %e = extractvalue { float, i32 } %fe, 1
  ret i32 %e
}

!llvm.module.flags = !{!0, !1}
!0 = !{i32 4, !"msil-linked", i32 1}
!1 = !{i32 4, !"msil-lto", i32 1}

!msil.imports = !{!10, !11}
!10 = !{!"ldexpf", !"libm.so.6", !"ldexpf", i32 0}
!11 = !{!"frexpf", !"libm.so.6", !"frexpf", i32 0}

; F32-NODOUBLE-DAG: {{^}}error: library call 'ldexp' was not resolved by msil-ld{{$}}
; F32-NODOUBLE-DAG: {{^}}error: library call 'frexp' was not resolved by msil-ld{{$}}

;--- f32-double.ll
; f32.ll linked, with only ldexp and frexp bound.
source_filename = "ld-temp.o"

declare float @llvm.ldexp.f32.i32(float, i32)
declare { float, i32 } @llvm.frexp.f32.i32(float)

define float @scale(float %x, i32 %e) {
  %r = call float @llvm.ldexp.f32.i32(float %x, i32 %e)
  ret float %r
}

define float @mantissa(float %x) {
  %fe = call { float, i32 } @llvm.frexp.f32.i32(float %x)
  %m = extractvalue { float, i32 } %fe, 0
  ret float %m
}

define i32 @exponent(float %x) {
  %fe = call { float, i32 } @llvm.frexp.f32.i32(float %x)
  %e = extractvalue { float, i32 } %fe, 1
  ret i32 %e
}

!llvm.module.flags = !{!0, !1}
!0 = !{i32 4, !"msil-linked", i32 1}
!1 = !{i32 4, !"msil-lto", i32 1}

!msil.imports = !{!10, !11}
!10 = !{!"ldexp", !"api-ms-win-crt-math-l1-1-0.dll", !"ldexp", i32 0}
!11 = !{!"frexp", !"api-ms-win-crt-math-l1-1-0.dll", !"frexp", i32 0}

; F32-NOFLOAT-DAG: {{^}}error: library call 'ldexpf' was not resolved by msil-ld{{$}}
; F32-NOFLOAT-DAG: {{^}}error: library call 'frexpf' was not resolved by msil-ld{{$}}

;--- bom.ll
; Every IR name is ASCII; the module and the entry of the one !msil.imports
; operand are 'cafe.so' and 'entree' with an acute accent (IR escapes
; \C3\A9).
source_filename = "ld-temp.o"

declare double @llvm.sqrt.f64(double)

; BOM-NOT:  {{.}}
; BOM:      {{^}}{{\xEF\xBB\xBF}}// MSIL module generated by LLVM{{$}}
; BOM:      {{^}}  call float64 'sqrt'(float64){{$}}
; BOM:      {{^}}.method assembly hidebysig static pinvokeimpl("caf{{\xC3\xA9}}.so" as "entr{{\xC3\xA9}}e" nomangle cdecl) float64 'sqrt'(float64) cil managed preservesig {}{{$}}

define double @root(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-linked", i32 1}

!msil.imports = !{!10}
!10 = !{!"sqrt", !"caf\C3\A9.so", !"entr\C3\A9e", i32 0}

;--- weak-linked.ll
; A linked module (both of msil-ld's flags) in which msil-ld bound nothing for
; the weak puts: no import attribute and no !msil.imports operand.
source_filename = "ld-temp.o"

@hello = private constant [6 x i8] c"hello\00"

declare extern_weak i32 @puts(ptr)

; WEAK-QUIET-NOT: undefined function 'puts' was not resolved by msil-ld
; WEAK-QUIET-NOT: {{.}}

; WEAKL-LABEL: {{^}}.method public static int32 'weak_say'() cil managed{{$}}
; WEAKL:       {{^}}  call int32 '<msil>undef_puts'(native int){{$}}
; WEAKL:       {{^}}} // end of method 'weak_say'{{$}}
define i32 @weak_say() {
  %r = call i32 @puts(ptr @hello)
  ret i32 %r
}

; The address is null: no '<msil>fn_puts' field is read.
; WEAKL-LABEL: {{^}}.method public static int32 'weak_present'() cil managed{{$}}
; WEAKL:       {{^}}} // end of method 'weak_present'{{$}}
define i1 @weak_present() {
  %c = icmp ne ptr @puts, null
  ret i1 %c
}

; WEAKSTUB: {{^}}.method assembly static int32 '<msil>undef_puts'(native int) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldnull[[:space:]]+throw[[:space:]]+\} // end of method '<msil>undef_puts'$}}

!llvm.module.flags = !{!0, !1}
!0 = !{i32 4, !"msil-linked", i32 1}
!1 = !{i32 4, !"msil-lto", i32 1}

;--- weak-sa.ll
; weak-linked.ll without module flags: D14 imports the weak puts.
source_filename = "ld-temp.o"

@hello = private constant [6 x i8] c"hello\00"

declare extern_weak i32 @puts(ptr)

; WEAKSA-LABEL: {{^}}.method public static int32 'weak_say'() cil managed{{$}}
; WEAKSA:       {{^}}  call int32 'puts'(native int){{$}}
; WEAKSA:       {{^}}} // end of method 'weak_say'{{$}}
define i32 @weak_say() {
  %r = call i32 @puts(ptr @hello)
  ret i32 %r
}

; WEAKSA-LABEL: {{^}}.method public static int32 'weak_present'() cil managed{{$}}
; WEAKSA:       {{^}}  ldsfld native int '<msil>fn_puts'{{$}}
; WEAKSA:       {{^}}} // end of method 'weak_present'{{$}}
; WEAKSA-LNX:   {{^}}.method public hidebysig static pinvokeimpl("libc" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; WEAKSA-WIN:   {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; WEAKSA-NOT:   pinvokeimpl
; WEAKSA-NOT:   assembly hidebysig
define i1 @weak_present() {
  %c = icmp ne ptr @puts, null
  ret i1 %c
}

;--- weak-lto.ll
; weak-sa.ll with M14's msil-lto flag only.
source_filename = "ld-temp.o"

@hello = private constant [6 x i8] c"hello\00"

declare extern_weak i32 @puts(ptr)

define i32 @weak_say() {
  %r = call i32 @puts(ptr @hello)
  ret i32 %r
}

define i1 @weak_present() {
  %c = icmp ne ptr @puts, null
  ret i1 %c
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-lto", i32 1}
