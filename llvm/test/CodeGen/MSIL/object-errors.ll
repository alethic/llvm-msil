; Diagnostics of object mode (ImplementationPlan.md 5.8.8 COMDATS, 5.8.10 and
; M19.b (2); decisions D19 and D26 as amended for M19). Every error is a
; clean diagnostic with exit code 1, never 'LLVM ERROR' or a crash report
; (section 2).
; New in object mode:
; - an alias that is not a definition plus a constant offset (offset 0 for
;   functions): 'alias '<a>' must point to a definition plus a constant
;   offset for MSIL' (here a function alias 4 bytes into a function, and in
;   alias-kind.ll aliases whose kind differs from their aliasee's: a data
;   alias of a function and a function alias of a variable, since an
;   Aliasee is a defined symbol of the same kind, 5.8.8);
; - a comdat whose selection is not Any or NoDeduplicate: 'comdat '<c>' uses
;   selection kind '<k>', which MSIL does not support' (largest, exactmatch,
;   samesize; whole-program mode ignores comdats, D26, so the same module
;   compiles with -S);
; - a name that starts with '<msil>' (R5: only the scaffolding may use the
;   prefix), on a function and on a variable;
; - two symbols with one IL name (C8: names unique within SYMTAB): a
;   function and an '\01' (asm label) function of another signature give
;   '@"\01f" has the same IL name 'f' as @f' (dup-sig.ll; whole-program
;   output accepts them as two overloads of 'f'), and two variables give
;   '@"\01foo" has the same IL name 'foo' as @foo' (dup-global.ll); two
;   functions of one IL name and one signature (dup-func.ll) give only the
;   error that -S gives (quoting-globals.ll DUP-FUNC), 'function @"\01f" has
;   the same IL name 'f' and signature as @f', never C8's error in addition;
; - an llvm.global_ctors entry that names a declaration (ctor-decl.ll) or a
;   variable (ctor-var.ll): 'global constructor '<c>' is not a function that
;   this object defines, which an MSIL object requires' (5.8.8 INIT_FINI: a
;   defined FUNCTION of this object);
; - a library call whose name is a global variable's (lib-name.ll, exp10):
;   'library call '<f>' has the IL name of global variable @<g>';
; - a variable larger than a segment may be (huge.ll; 5.8.8 SEGMENTS: Size
;   below 2^31): 'global variable @<g> of <n> bytes is larger than MSIL
;   supports (2 GiB)';
; - O7's limit on the import strings of a stub (5.8.3): an
;   "msil-import-module" or "msil-import-name" of 1,100 bytes gives 'import
;   module of 1100 bytes is longer than the 1023 bytes an MSIL object allows'
;   (resp. 'import name ...') with -filetype=obj and with -msil-object-form
;   (long-import-module.ll, long-import-name.ll, long-import-name-mod.ll), and
;   a 1,023-byte module compiles (import-module-1023.ll).
; Kept from Phase 1 (D19 and D26): a call of a variadic function (p7.c's
; ext_var("x", 4), 5.9.16 row p7-flags), a variadic definition, a
; thread_local variable, a non-empty llvm.global_dtors, an ifunc and module
; asm; and O7's identifier limit of 1,023 bytes (Q27) on a defined function
; (long-name.ll), a called undefined function (long-callee.ll), a defined
; variable (long-global.ll) and a called alias (long-alias.ll). Also kept,
; with -S's texts: a constructor that takes arguments ('global constructor
; '<c>' takes arguments, which MSIL does not pass', ctor-args.ll), and the
; initializers that MSILDataSegment cannot express as a relocation in object
; mode either (5.8.8 DATA_RELOCS: FNADDR has addend 0, DIFF names DATA
; symbols): 'MSIL: function address + offset in initializer of '<g>''
; (reloc-fnoff.ll) and 'MSIL: symbol difference across non-data symbols in
; initializer of '<g>'' (reloc-fndiff.ll).
; An undefined, prototyped variadic function that the object references only
; by address (D19; 5.8.8 'No VARARG'; section 12, 2026-10-25, 'Plan 5.10.13
; clarified for object mode'): 'undefined variadic function '<f>' is not
; supported for MSIL yet (phase 3)', with no import stub, whether the address
; is taken in a data initializer (vaddr-data.ll) or in code (vaddr-code.ll),
; and also for a declaration with "msil-import-module" and
; "msil-import-name" (vaddr-import.ll). An unprototyped declaration used only
; by its address stays accepted (object-calls.ll addr.ll, object-imports.ll).
; A global or function that the backend diagnoses gives exactly that one
; error, never a follow-on error of the object writer about the row the
; diagnosed symbol does not get (section 2: the visible result is the
; diagnostic and exit code 1): an alignment above 2^30 (big-align.ll), prefix
; data (prefix.ll), a name or a section name that is not valid UTF-8
; (utf8-name.ll, utf8-section.ll; 5.8.8 'str'), O7's identifier and import
; string limits (never also the writer's 'cannot write CLI metadata'), and
; every other error above except the D19 and D26 errors kept from Phase 1
; (variadic call and definition, thread_local, llvm.global_dtors, ifunc and
; module asm, matched under %{nocrash}): each is matched under %{clean},
; which admits no second error or warning.
; accepted.ll: what object mode accepts and whole-program mode diagnoses: an
; external variable (D26), an undefined function outside the D14 table, a
; runtime helper (__powidf2 on linux-gnu; windows-msvc imports pow instead)
; and a library call outside the D14 table (exp10). It also holds a section
; on a variable: M12 diagnosed it in whole-program mode, but from M20
; whole-program mode lays the section out as a one-object link does, so for
; that construct only the object rows remain (symbol 'in_sec' and segment
; 'mysec'), and %{clean} on the WP runs keeps the error from coming back.
; -filetype=obj writes the object with no message; llc's assembly output and
; -filetype=null stay whole-program and keep their other errors, with the
; same texts as before this milestone.

; REQUIRES: llvm-cil-dump
; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{nocrash} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; DEFINE: %{clean} = %{nocrash} --implicit-check-not=error: --implicit-check-not=warning:

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/alias.ll \
; RUN:   -o %t/alias.o 2>&1 | FileCheck %s %{clean} --check-prefix=ALIAS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/alias.ll \
; RUN:   -o %t/alias.o 2>&1 | FileCheck %s %{clean} --check-prefix=ALIAS
; ALIAS: {{^}}error: {{.*}}alias 'fn_offset' must point to a definition plus a constant offset for MSIL{{$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/alias-kind.ll \
; RUN:   -o %t/alias-kind.o 2>&1 | FileCheck %s %{clean} --check-prefix=ALIAS-KIND
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/alias-kind.ll \
; RUN:   -o %t/alias-kind.o 2>&1 | FileCheck %s %{clean} --check-prefix=ALIAS-KIND
; ALIAS-KIND-DAG: {{^}}error: {{.*}}alias 'data_of_fn' must point to a definition plus a constant offset for MSIL{{$}}
; ALIAS-KIND-DAG: {{^}}error: {{.*}}alias 'fn_of_data' must point to a definition plus a constant offset for MSIL{{$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/vaddr-data.ll \
; RUN:   -o %t/vaddr-data.o 2>&1 | FileCheck %s %{clean} --check-prefix=VADDR -DFN=printf
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/vaddr-data.ll \
; RUN:   -o %t/vaddr-data.o 2>&1 | FileCheck %s %{clean} --check-prefix=VADDR -DFN=printf
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/vaddr-code.ll \
; RUN:   -o %t/vaddr-code.o 2>&1 | FileCheck %s %{clean} --check-prefix=VADDR -DFN=printf
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/vaddr-code.ll \
; RUN:   -o %t/vaddr-code.o 2>&1 | FileCheck %s %{clean} --check-prefix=VADDR -DFN=printf
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/vaddr-import.ll \
; RUN:   -o %t/vaddr-import.o 2>&1 | FileCheck %s %{clean} --check-prefix=VADDR -DFN=my_printf
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/vaddr-import.ll \
; RUN:   -o %t/vaddr-import.o 2>&1 | FileCheck %s %{clean} --check-prefix=VADDR -DFN=my_printf
; VADDR: {{^}}error: undefined variadic function '[[FN]]' is not supported for MSIL yet (phase 3){{$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/big-align.ll \
; RUN:   -o %t/big-align.o 2>&1 | FileCheck %s %{clean} --check-prefix=BIGALIGN
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/big-align.ll \
; RUN:   -o %t/big-align.o 2>&1 | FileCheck %s %{clean} --check-prefix=BIGALIGN
; BIGALIGN: {{^}}error: global variable @big: an alignment of 2147483648 bytes is larger than MSIL supports (2^30){{$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/prefix.ll \
; RUN:   -o %t/prefix.o 2>&1 | FileCheck %s %{clean} --check-prefix=PREFIX
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/prefix.ll \
; RUN:   -o %t/prefix.o 2>&1 | FileCheck %s %{clean} --check-prefix=PREFIX
; PREFIX: {{^}}error: {{.*}}in function f i32 (): prefix data, prologue data and function sanitizer metadata are not supported for MSIL{{$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/utf8-name.ll \
; RUN:   -o %t/utf8-name.o 2>&1 | FileCheck %s %{clean} --check-prefix=UTF8-NAME
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/utf8-name.ll \
; RUN:   -o %t/utf8-name.o 2>&1 | FileCheck %s %{clean} --check-prefix=UTF8-NAME
; UTF8-NAME: {{^}}error: the name of @"bad\FF" is not valid UTF-8 without NUL bytes, which an MSIL object requires{{$}}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/utf8-section.ll \
; RUN:   -o %t/utf8-section.o 2>&1 | FileCheck %s %{clean} --check-prefix=UTF8-SEC
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/utf8-section.ll \
; RUN:   -o %t/utf8-section.o 2>&1 | FileCheck %s %{clean} --check-prefix=UTF8-SEC
; UTF8-SEC: {{^}}error: the section name of @in_bad_sec is not valid UTF-8 without NUL bytes, which an MSIL object requires{{$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/comdats.ll \
; RUN:   -o %t/comdats.o 2>&1 | FileCheck %s %{clean} --check-prefix=COMDAT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/comdats.ll \
; RUN:   -o %t/comdats.o 2>&1 | FileCheck %s %{clean} --check-prefix=COMDAT
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/comdats.ll -o /dev/null 2>&1 \
; RUN:   | count 0
; COMDAT-DAG: {{^}}error: {{.*}}comdat 'cl' uses selection kind 'largest', which MSIL does not support{{$}}
; COMDAT-DAG: {{^}}error: {{.*}}comdat 'ce' uses selection kind 'exactmatch', which MSIL does not support{{$}}
; COMDAT-DAG: {{^}}error: {{.*}}comdat 'cs' uses selection kind 'samesize', which MSIL does not support{{$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/reserved.ll \
; RUN:   -o %t/reserved.o 2>&1 | FileCheck %s %{clean} --check-prefix=RESERVED
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/reserved.ll \
; RUN:   -o %t/reserved.o 2>&1 | FileCheck %s %{clean} --check-prefix=RESERVED
; RESERVED-DAG: {{^}}error: {{.*}}<msil>fbad{{.*$}}
; RESERVED-DAG: {{^}}error: {{.*}}<msil>gbad{{.*$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/varcall.ll \
; RUN:   -o %t/varcall.o 2>&1 | FileCheck %s %{nocrash} --check-prefix=VARCALL
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/varcall.ll \
; RUN:   -o %t/varcall.o 2>&1 | FileCheck %s %{nocrash} --check-prefix=VARCALL
; VARCALL: {{^}}error: {{.*}}use_var{{.*}}calls to variadic functions are not supported for MSIL yet{{$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/vardef.ll \
; RUN:   -o %t/vardef.o 2>&1 | FileCheck %s %{nocrash} --check-prefix=VARDEF
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/vardef.ll \
; RUN:   -o %t/vardef.o 2>&1 | FileCheck %s %{nocrash} --check-prefix=VARDEF
; VARDEF: {{^}}error: {{.*(vdef.*variadic function definitions are not supported for MSIL yet|variadic function definitions are not supported for MSIL yet.*vdef)}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/tls.ll \
; RUN:   -o %t/tls.o 2>&1 | FileCheck %s %{nocrash} --check-prefix=TLS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/tls.ll \
; RUN:   -o %t/tls.o 2>&1 | FileCheck %s %{nocrash} --check-prefix=TLS
; TLS: {{^}}error: {{.*}}tls_v

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/dtors.ll \
; RUN:   -o %t/dtors.o 2>&1 | FileCheck %s %{nocrash} --check-prefix=DTORS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/dtors.ll \
; RUN:   -o %t/dtors.o 2>&1 | FileCheck %s %{nocrash} --check-prefix=DTORS
; DTORS: {{^}}error: {{.*}}{{llvm\.global_dtors|dtor_fn}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/ifunc.ll \
; RUN:   -o %t/ifunc.o 2>&1 | FileCheck %s %{nocrash} --check-prefix=IFUNC
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/ifunc.ll \
; RUN:   -o %t/ifunc.o 2>&1 | FileCheck %s %{nocrash} --check-prefix=IFUNC
; IFUNC: {{^}}error: {{.*}}ifunc @ifn is not supported for MSIL{{$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/module-asm.ll \
; RUN:   -o %t/module-asm.o 2>&1 | FileCheck %s %{nocrash} --check-prefix=MODASM
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/module-asm.ll \
; RUN:   -o %t/module-asm.o 2>&1 | FileCheck %s %{nocrash} --check-prefix=MODASM
; MODASM: {{^}}error: {{.*}}module-level inline assembly is not supported for MSIL{{$}}

; DEFINE: %{o7} = %{clean} --implicit-check-not="cannot write CLI metadata"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/long-name.ll \
; RUN:   -o %t/long-name.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGNAME
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/long-name.ll \
; RUN:   -o %t/long-name.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGNAME
; LONGNAME: {{^}}error: {{.*}}in function {{a+}} i32 (): identifier of 1024 bytes is longer than the 1023 bytes ilasm accepts{{$}}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/long-callee.ll \
; RUN:   -o %t/long-callee.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGID
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/long-callee.ll \
; RUN:   -o %t/long-callee.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGID
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/long-global.ll \
; RUN:   -o %t/long-global.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGID
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/long-global.ll \
; RUN:   -o %t/long-global.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGID
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/long-alias.ll \
; RUN:   -o %t/long-alias.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGID
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/long-alias.ll \
; RUN:   -o %t/long-alias.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGID
; LONGID: {{^}}error: {{.*}}identifier of 1024 bytes is longer than the 1023 bytes ilasm accepts{{$}}

; O7 for the import strings of a stub, with -filetype=obj and with
; -msil-object-form.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/long-import-module.ll \
; RUN:   -o %t/long-import-module.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGMOD
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/long-import-module.ll \
; RUN:   -o %t/long-import-module.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGMOD
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/long-import-module.ll \
; RUN:   -o %t/long-import-module.il 2>&1 | FileCheck %s %{o7} --check-prefix=LONGMOD
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form -verify-machineinstrs < %t/long-import-module.ll \
; RUN:   -o %t/long-import-module.il 2>&1 | FileCheck %s %{o7} --check-prefix=LONGMOD
; LONGMOD: {{^}}error: {{.*}}import module of 1100 bytes is longer than the 1023 bytes an MSIL object allows{{$}}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/long-import-name.ll \
; RUN:   -o %t/long-import-name.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGENT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/long-import-name.ll \
; RUN:   -o %t/long-import-name.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGENT
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/long-import-name.ll \
; RUN:   -o %t/long-import-name.il 2>&1 | FileCheck %s %{o7} --check-prefix=LONGENT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form -verify-machineinstrs < %t/long-import-name.ll \
; RUN:   -o %t/long-import-name.il 2>&1 | FileCheck %s %{o7} --check-prefix=LONGENT
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/long-import-name-mod.ll \
; RUN:   -o %t/long-import-name-mod.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGENT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/long-import-name-mod.ll \
; RUN:   -o %t/long-import-name-mod.o 2>&1 | FileCheck %s %{o7} --check-prefix=LONGENT
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/long-import-name-mod.ll \
; RUN:   -o %t/long-import-name-mod.il 2>&1 | FileCheck %s %{o7} --check-prefix=LONGENT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form -verify-machineinstrs < %t/long-import-name-mod.ll \
; RUN:   -o %t/long-import-name-mod.il 2>&1 | FileCheck %s %{o7} --check-prefix=LONGENT
; LONGENT: {{^}}error: {{.*}}import name of 1100 bytes is longer than the 1023 bytes an MSIL object allows{{$}}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/import-module-1023.ll \
; RUN:   -o %t/import-module-1023.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/import-module-1023.ll \
; RUN:   -o %t/import-module-1023.win.o 2>&1 | count 0
; RUN: llvm-cil-dump --tables %t/import-module-1023.lnx.o | FileCheck %s --check-prefix=MOD1023 --match-full-lines
; RUN: llvm-cil-dump --tables %t/import-module-1023.win.o | FileCheck %s --check-prefix=MOD1023 --match-full-lines
; MOD1023:Table: ModuleRef rows=1 rowsize={{[0-9]+}}
; MOD1023-NEXT:  ModuleRef[1]: Name='{{m+}}'
; MOD1023-NEXT:Table: ImplMap rows=1 rowsize={{[0-9]+}}
; MOD1023-NEXT:  ImplMap[1]: MappingFlags=0x0201 MemberForwarded=0x06000002 ImportName='imp' ImportScope=0x1A000001

; Two symbols with one IL name. dup-func.ll gives -S's error only, dup-sig.ll
; and dup-global.ll C8's; whole-program output accepts dup-sig.ll.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/dup-func.ll \
; RUN:   -o %t/dup-func.o 2>&1 | FileCheck %s %{clean} --check-prefix=DUP-FUNC
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/dup-func.ll \
; RUN:   -o %t/dup-func.o 2>&1 | FileCheck %s %{clean} --check-prefix=DUP-FUNC
; DUP-FUNC: {{^}}error: function @"\01f" has the same IL name 'f' and signature as @f{{$}}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/dup-sig.ll \
; RUN:   -o %t/dup-sig.o 2>&1 | FileCheck %s %{clean} --check-prefix=DUP-SIG
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/dup-sig.ll \
; RUN:   -o %t/dup-sig.o 2>&1 | FileCheck %s %{clean} --check-prefix=DUP-SIG
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/dup-sig.ll -o /dev/null 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/dup-sig.ll -o /dev/null 2>&1 \
; RUN:   | count 0
; DUP-SIG: {{^}}error: @"\01f" has the same IL name 'f' as @f{{$}}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/dup-global.ll \
; RUN:   -o %t/dup-global.o 2>&1 | FileCheck %s %{clean} --check-prefix=DUP-GLOBAL
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/dup-global.ll \
; RUN:   -o %t/dup-global.o 2>&1 | FileCheck %s %{clean} --check-prefix=DUP-GLOBAL
; DUP-GLOBAL: {{^}}error: @"\01foo" has the same IL name 'foo' as @foo{{$}}

; Constructors that an object cannot record (5.8.8 INIT_FINI).
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/ctor-decl.ll \
; RUN:   -o %t/ctor-decl.o 2>&1 | FileCheck %s %{clean} --check-prefix=CTOR-NOTDEF -DCTOR=extinit
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/ctor-decl.ll \
; RUN:   -o %t/ctor-decl.o 2>&1 | FileCheck %s %{clean} --check-prefix=CTOR-NOTDEF -DCTOR=extinit
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/ctor-var.ll \
; RUN:   -o %t/ctor-var.o 2>&1 | FileCheck %s %{clean} --check-prefix=CTOR-NOTDEF -DCTOR=notfn
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/ctor-var.ll \
; RUN:   -o %t/ctor-var.o 2>&1 | FileCheck %s %{clean} --check-prefix=CTOR-NOTDEF -DCTOR=notfn
; CTOR-NOTDEF: {{^}}error: global constructor '[[CTOR]]' is not a function that this object defines, which an MSIL object requires{{$}}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/ctor-args.ll \
; RUN:   -o %t/ctor-args.o 2>&1 | FileCheck %s %{clean} --check-prefix=CTOR-ARGS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/ctor-args.ll \
; RUN:   -o %t/ctor-args.o 2>&1 | FileCheck %s %{clean} --check-prefix=CTOR-ARGS
; CTOR-ARGS: {{^}}error: global constructor 'ia' takes arguments, which MSIL does not pass{{$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/lib-name.ll \
; RUN:   -o %t/lib-name.o 2>&1 | FileCheck %s %{clean} --check-prefix=LIBNAME
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/lib-name.ll \
; RUN:   -o %t/lib-name.o 2>&1 | FileCheck %s %{clean} --check-prefix=LIBNAME
; LIBNAME: {{^}}error: library call 'exp10' has the IL name of global variable @exp10{{$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/huge.ll \
; RUN:   -o %t/huge.o 2>&1 | FileCheck %s %{clean} --check-prefix=HUGE
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/huge.ll \
; RUN:   -o %t/huge.o 2>&1 | FileCheck %s %{clean} --check-prefix=HUGE
; HUGE: {{^}}error: global variable @huge of 3000000000 bytes is larger than MSIL supports (2 GiB){{$}}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/reloc-fnoff.ll \
; RUN:   -o %t/reloc-fnoff.o 2>&1 | FileCheck %s %{clean} --check-prefix=RELOC-FNOFF
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/reloc-fnoff.ll \
; RUN:   -o %t/reloc-fnoff.o 2>&1 | FileCheck %s %{clean} --check-prefix=RELOC-FNOFF
; RELOC-FNOFF: {{^}}error: MSIL: function address + offset in initializer of 'fpoff'{{$}}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/reloc-fndiff.ll \
; RUN:   -o %t/reloc-fndiff.o 2>&1 | FileCheck %s %{clean} --check-prefix=RELOC-DIFF
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/reloc-fndiff.ll \
; RUN:   -o %t/reloc-fndiff.o 2>&1 | FileCheck %s %{clean} --check-prefix=RELOC-DIFF
; RELOC-DIFF: {{^}}error: MSIL: symbol difference across non-data symbols in initializer of 'rel'{{$}}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/accepted.ll \
; RUN:   -o %t/accepted.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/accepted.ll \
; RUN:   -o %t/accepted.win.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/accepted.lnx.o \
; RUN:   | FileCheck %s --check-prefixes=OBJ,OBJ-LNX --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/accepted.win.o \
; RUN:   | FileCheck %s --check-prefixes=OBJ,OBJ-WIN --match-full-lines --strict-whitespace
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/accepted.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s %{clean} --check-prefixes=WP,WP-LNX
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=null -verify-machineinstrs < %t/accepted.ll 2>&1 \
; RUN:   | FileCheck %s %{clean} --check-prefixes=WP,WP-LNX
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/accepted.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s %{clean} --check-prefix=WP
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=null -verify-machineinstrs < %t/accepted.ll 2>&1 \
; RUN:   | FileCheck %s %{clean} --check-prefix=WP

; OBJ-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; OBJ-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; OBJ-NEXT:Symbol 0 DATA GLOBAL DEFAULT UNDEFINED 'ext_v'
; OBJ-NEXT:Symbol 1 DATA GLOBAL DEFAULT 'in_sec' field=0x04000001 segment=0 offset=0 size=4
; OBJ-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_f'
; OBJ-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT 'use' method=0x06000001
; OBJ-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT 'helper' method=0x06000002
; OBJ-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT 'libcall' method=0x06000003
; OBJ-LNX-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT UNDEFINED '__powidf2'
; OBJ-WIN-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT UNDEFINED 'pow'
; OBJ-NEXT:Symbol 7 FUNCTION GLOBAL DEFAULT UNDEFINED 'exp10'
; OBJ-NEXT:Segment 0 'mysec' align=4 size=4 flags=0
; OBJ-NEXT:  01 00 00 00
; OBJ-NOT:{{.}}

; WP-DAG: {{^}}error: external global variable 'ext_v' is not supported (phase 2 linker){{$}}
; WP-DAG: {{^}}error: undefined function 'ext_f' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; WP-DAG: {{^}}error: {{.*}}in function libcall {{.*}}: library call 'exp10' is not a Phase-1 import{{$}}
; WP-LNX-DAG: {{^}}error: {{.*}}in function helper {{.*}}: runtime helper '__powidf2' is not available for MSIL yet (phase 3){{$}}

;--- alias.ll
source_filename = "alias-errors.c"

@fn_offset = alias i32 (i32), getelementptr (i8, ptr @target, i64 4)

define i32 @target(i32 %x) {
  ret i32 %x
}

;--- alias-kind.ll
source_filename = "alias-kind.c"

@g = global i32 5, align 4

@data_of_fn = alias i8, ptr @f
@fn_of_data = alias i32 (), ptr @g

define i32 @f() {
  ret i32 1
}

define ptr @use_data_alias() {
  ret ptr @data_of_fn
}

define ptr @use_fn_alias() {
  ret ptr @fn_of_data
}

;--- vaddr-data.ll
source_filename = "vaddr-data.c"

@pp = global ptr @printf, align 8

declare i32 @printf(ptr noundef, ...)

;--- vaddr-code.ll
source_filename = "vaddr-code.c"

declare i32 @printf(ptr noundef, ...)

define ptr @get_printf() {
  ret ptr @printf
}

;--- vaddr-import.ll
source_filename = "vaddr-import.c"

declare i32 @my_printf(ptr noundef, ...) #0

define ptr @get_my_printf() {
  ret ptr @my_printf
}

attributes #0 = { "msil-import-module"="libc" "msil-import-name"="printf" }

;--- big-align.ll
source_filename = "big-align.c"

@big = global i32 1, align 2147483648

define i32 @get() {
  %v = load i32, ptr @big, align 4
  ret i32 %v
}

;--- prefix.ll
source_filename = "prefix.c"

define i32 @f() prefix i32 123 {
  ret i32 1
}

define i32 @g() {
  %r = call i32 @f()
  ret i32 %r
}

;--- utf8-name.ll
source_filename = "utf8-name.c"

@"bad\FF" = global i32 3, align 4

define i32 @get() {
  %v = load i32, ptr @"bad\FF", align 4
  ret i32 %v
}

;--- utf8-section.ll
source_filename = "utf8-section.c"

@in_bad_sec = global i32 3, section "sec\FF", align 4

define i32 @get() {
  %v = load i32, ptr @in_bad_sec, align 4
  ret i32 %v
}

;--- comdats.ll
source_filename = "comdat-kinds.c"

$cl = comdat largest
$ce = comdat exactmatch
$cs = comdat samesize

@vl = global i32 1, comdat($cl), align 4
@ve = global i32 2, comdat($ce), align 4

define i32 @fs() comdat($cs) {
  ret i32 3
}

;--- reserved.ll
source_filename = "reserved.c"

@"<msil>gbad" = global i32 1, align 4

define i32 @"<msil>fbad"() {
  ret i32 0
}

;--- varcall.ll
source_filename = "p7.c"

@.str = private unnamed_addr constant [2 x i8] c"x\00", align 1

declare i32 @ext_var(ptr noundef, ...)

define i32 @use_var() {
  %r = call i32 (ptr, ...) @ext_var(ptr noundef @.str, i32 noundef 4)
  ret i32 %r
}

;--- vardef.ll
source_filename = "vardef.c"

define i32 @vdef(i32 %n, ...) {
  ret i32 %n
}

;--- tls.ll
source_filename = "tls.c"

@tls_v = thread_local global i32 5, align 4

define i32 @read_tls() {
  %p = call ptr @llvm.threadlocal.address.p0(ptr @tls_v)
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

declare ptr @llvm.threadlocal.address.p0(ptr)

;--- dtors.ll
source_filename = "dtors.c"

@llvm.global_dtors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @dtor_fn, ptr null }]

define internal void @dtor_fn() {
  ret void
}

;--- ifunc.ll
source_filename = "ifunc.c"

@ifn = ifunc i32 (i32), ptr @resolver

define ptr @resolver() {
  ret ptr null
}

define i32 @calls_ifunc(i32 %x) {
  %r = call i32 @ifn(i32 %x)
  ret i32 %r
}

;--- module-asm.ll
source_filename = "module-asm.c"

module asm ".globl msil_module_asm_sym"

define i32 @f() {
  ret i32 0
}

;--- long-name.ll
source_filename = "long-name.c"

define i32 @aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa() {
  ret i32 0
}

;--- long-callee.ll
source_filename = "long-callee.c"

declare i32 @bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb(i32)

define i32 @use() {
  %r = call i32 @bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb(i32 1)
  ret i32 %r
}

;--- long-global.ll
source_filename = "long-global.c"

@gggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggg = global i32 1, align 4

;--- long-alias.ll
source_filename = "long-alias.c"

@qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq = alias i32 (), ptr @f

define i32 @f() {
  ret i32 1
}

define i32 @use() {
  %r = call i32 @qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq()
  ret i32 %r
}

;--- long-import-module.ll
source_filename = "long-import-module.c"

declare i32 @imp(i32) #0

define i32 @use() {
  %r = call i32 @imp(i32 1)
  ret i32 %r
}

attributes #0 = { "msil-import-module"="mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm" }

;--- long-import-name.ll
source_filename = "long-import-name.c"

declare i32 @imp(i32) #0

define i32 @use() {
  %r = call i32 @imp(i32 1)
  ret i32 %r
}

attributes #0 = { "msil-import-name"="eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee" }

;--- long-import-name-mod.ll
source_filename = "long-import-name-mod.c"

declare i32 @imp(i32) #0

define i32 @use() {
  %r = call i32 @imp(i32 1)
  ret i32 %r
}

attributes #0 = { "msil-import-module"="libc" "msil-import-name"="eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee" }

;--- import-module-1023.ll
source_filename = "import-module-1023.c"

declare i32 @imp(i32) #0

define i32 @use() {
  %r = call i32 @imp(i32 1)
  ret i32 %r
}

attributes #0 = { "msil-import-module"="mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm" }

;--- dup-func.ll
; int f(void) { return 1; } int f2(void) __asm__("f"); int f2(void) { return 2; }
source_filename = "dup-func.c"

define i32 @f() {
  ret i32 1
}

define i32 @"\01f"() {
  ret i32 2
}

define i32 @g() {
  %a = call i32 @f()
  %b = call i32 @"\01f"()
  %c = add i32 %a, %b
  ret i32 %c
}

;--- dup-sig.ll
source_filename = "dup-sig.c"

define i32 @f() {
  ret i32 1
}

define i64 @"\01f"() {
  ret i64 2
}

;--- dup-global.ll
source_filename = "dup-global.c"

@foo = global i32 1, align 4
@"\01foo" = global i32 2, align 4

define i32 @g() {
  %a = load i32, ptr @foo, align 4
  %b = load i32, ptr @"\01foo", align 4
  %c = add i32 %a, %b
  ret i32 %c
}

;--- ctor-decl.ll
source_filename = "ctor-decl.c"

@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @extinit, ptr null }]

declare void @extinit()

;--- ctor-var.ll
source_filename = "ctor-var.c"

@notfn = global i32 0, align 4
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @notfn, ptr null }]

;--- ctor-args.ll
source_filename = "ctor-args.c"

@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @ia, ptr null }]

define void @ia(i32 %x) {
  ret void
}

;--- lib-name.ll
source_filename = "lib-name.c"

@exp10 = global i32 0, align 4

define double @use(double %x) {
  %e = call double @llvm.exp10.f64(double %x)
  ret double %e
}

declare double @llvm.exp10.f64(double)

;--- huge.ll
source_filename = "huge.c"

@huge = global [3000000000 x i8] zeroinitializer, align 1

define ptr @use() {
  ret ptr @huge
}

;--- reloc-fnoff.ll
source_filename = "reloc-fnoff.c"

@fpoff = global ptr getelementptr (i8, ptr @f, i64 4), align 8

define i32 @f() {
  ret i32 1
}

;--- reloc-fndiff.ll
source_filename = "reloc-fndiff.c"

@rel = global i32 trunc (i64 sub (i64 ptrtoint (ptr @f to i64), i64 ptrtoint (ptr @rel to i64)) to i32), align 4

define i32 @f() {
  ret i32 1
}

;--- accepted.ll
source_filename = "accepted.c"

@ext_v = external global i32, align 4
@in_sec = global i32 1, section "mysec", align 4

declare i32 @ext_f(i32)

define i32 @use() {
  %v = load i32, ptr @ext_v, align 4
  %c = call i32 @ext_f(i32 %v)
  %s = load i32, ptr @in_sec, align 4
  %r = add i32 %c, %s
  ret i32 %r
}

define double @helper(double %x, i32 %n) {
  %p = call double @llvm.powi.f64.i32(double %x, i32 %n)
  ret double %p
}

define double @libcall(double %x) {
  %e = call double @llvm.exp10.f64(double %x)
  ret double %e
}

declare double @llvm.powi.f64.i32(double, i32)
declare double @llvm.exp10.f64(double)
