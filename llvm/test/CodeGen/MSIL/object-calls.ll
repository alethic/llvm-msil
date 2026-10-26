; Calls in a CIL object (ImplementationPlan.md 5.8.3 R1-R3 and R6, 5.8.8
; SIGNATURES, 5.8.10 'The prototype passes in object mode'; M19;
; judge-c-fidelity F3 and R19).
;
; knr12.ll and knr21.ll (matrix case knr-two-arities, 5.9.16): a K&R
; declaration 'int f();' ("no-prototype", type i32 (...)) called as f(1, 2)
; and as f(1). A call of an undefined function gets no wrapper, so each call
; signature becomes its own MemberRef on [msil.extern]'<msil>Symbols' (R3:
; identity is (parent, name, signature)) with a NO_PROTOTYPE SIGNATURES
; entry, whatever the call order: knr12.ll's main calls f(1, 2) first,
; knr21.ll's g calls f(1) first, and in each module the MemberRefs are
; numbered in first-appearance order (R6). MSILAddMissingPrototypes and
; MSILFixFunctionBitcasts change nothing here: the declaration keeps its
; variadic type and its attribute, the calls keep their types, and no
; '_bitcast' or '.fixed_sig' method appears (Phase 1 derived one prototype
; and could lose an argument).
; proto.ll: a prototyped declaration called with its own type and through
; another function type gets two MemberRefs, the call's signature each, and
; no wrapper and no SIGNATURES entry (it is prototyped).
; sysv.ll: a variadic-typed call of a "no-prototype" declaration (the IR that
; native SysV targets emit) becomes the non-variadic call of all of its
; arguments, a fixed-arity MemberRef with NO_PROTOTYPE, never VARARG and
; never D19's error.
; addr.ll: an unprototyped declaration used only by its address gives the
; FnAddr placeholder, an UNDEFINED symbol, no import stub (5.8.3) and no
; diagnostic (the 'cannot be derived' error becomes msil-ld's, 5.10.13); its
; -S twin keeps Phase 1's 'cannot be derived' error (next to D14's error for
; an undefined function).
; fnaddr.ll: R2, the address of a defined, an undefined and an alias
; function is always [msil.extern]'<msil>FnAddr'::'<name>' (field signature
; 06 18); the undefined function is also called, so it gets no stub; BCL
; members are MemberRefs on TypeRefs scoped to [msil.bcl]; a calli signature
; is a StandAloneSig.
; sigs.ll: SIGNATURES (5.8.8) for definitions from their own attributes
; (sret(T), byval(T): sizes and alignments, the byval's IL parameter index)
; and for '<msil>Symbols' MemberRefs from the IR calls (CallBase attributes
; plus the callee's "no-prototype"); two calls with one IL signature but
; different byval attributes share one MemberRef, warn (R3) and record the
; first call's layout.
; libcalls.ll, the object twins of the D14 llc tests (libcall-policy.ll,
; pinvoke-errors.ll): in object mode the D14 import table does not apply.
; Declared C functions (sqrt and puts, Phase-1 imports; malloc, an error in
; -S), the library calls of llvm.sqrt (sqrt again: one symbol, one
; MemberRef), llvm.exp10 (exp10), llvm.lround (lround) and llvm.powi
; (__powidf2 on linux-gnu, pow of the converted exponent on windows-msvc)
; are all UNDEFINED FUNCTION symbols reached through '<msil>Symbols', with
; no P/Invoke into a C library and no error; the symbols that code
; generation creates come last in SYMTAB, in first-reference order. abort,
; whose address an initializer stores but which nothing calls, is a
; FNADDR64 relocation and gets 5.8.3's address-only stub on msil.unresolved.
; atomics.ll, the object twin of D14's '__' helper rule (D14 Phase 2 note,
; D36, 5.8.10 'library call (any, __ helpers included)'): the __atomic_*
; helpers that AtomicExpand adds during code generation for an atomicrmw
; add and a seq_cst atomic load (__atomic_fetch_add_4, __atomic_load_4), the
; f16 helper __truncsfhf2 and putchar (a Phase-1 import in -S) are UNDEFINED
; FUNCTION symbols with '<msil>Symbols' calls, no P/Invoke and no
; diagnostic, while the same module stays D14's error in -S. h, whose result
; is half, has a SIGNATURES entry with NATIVE_INCOMPATIBLE (5.8.8 as the
; section 12 row of 2026-10-25 amends it); the __truncsfhf2 MemberRef, which
; code generation adds and no IR call names, has none.
; ae.ll: calls through another function type of an available_externally
; function and of an extern_weak declaration, both declarations for the
; linker (isDeclarationForLinker; 5.8.10: an available_externally function
; is not emitted and its references are UNDEFINED): each is a direct call of
; a [msil.extern]'<msil>Symbols' MemberRef with the call's signature, with no
; wrapper method, no 'mismatched call signature' error and no warning; SYMTAB
; marks 'ae' UNDEFINED and 'ew' WEAK UNDEFINED (M19.b (3), R3).
; defwrap.ll: a call through another function type of a defined function
; (f) and of a weak definition (w) keeps Phase 1's wrapper (5.8.10 'calls of
; definitions keep Phase 1's wrappers', M19.b (2a)): LOCAL FUNCTION symbols
; 'f_bitcast' and 'w_bitcast', whose methods call the MethodDefs of f and w
; (R1, a WEAK definition included), and no placeholder MemberRef.
; libdef.ll: a library call whose name the module defines (llvm.exp10 with
; exp10 defined) calls the MethodDef '<Module>'::'exp10' (R1), and SYMTAB has
; the defined exp10 only, with no UNDEFINED duplicate (C8: names unique
; within SYMTAB) and no placeholder MemberRef.
; ninc.ll: NATIVE_INCOMPATIBLE (5.8.8 as the section 12 row of 2026-10-25
; amends it) on the definitions and the '<msil>Symbols' MemberRefs whose IR
; function type has a result or a parameter that is not a native scalar: a
; { i64, i64 } result (returned through a hidden pointer), an i128 result and
; an i128 parameter. Their CIL signatures are those of ordinary pointer and
; int64 calls, so only the flag tells msil-ld that a P/Invoke cannot pass
; them. The native-scalar definition and call get no entry.
; SIGNATURES entries are matched in any order (5.8.8 fixes none) and
; counted, and nothing may stand between the symbols and the first entry.
;
; Both x86_64 triples at -O2, most also at -O0.

; REQUIRES: llvm-cil-dump
; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{full} = --match-full-lines --strict-whitespace
; DEFINE: %{nowrap} = --implicit-check-not=_bitcast --implicit-check-not=fixed_sig
; The object oracle (5.12.6, as in object-form-text.ll, for knr12, knr21,
; fnaddr and sigs): ilasm's object of
; the object-form text is canonically equal to llc's object, record and
; tokens included.
; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{o} =
; DEFINE: %{oracle} = %ilasm-verify -output=%{o}.ilasm.o %{o}.il && \
; DEFINE:   %{canon} %{o}.o > %{o}.canon1 && %{canon} %{o}.ilasm.o > %{o}.canon2 && diff %{o}.canon1 %{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %{o}.o > %{o}.link1 && llvm-cil-dump --link-info %{o}.ilasm.o > %{o}.link2 && \
; DEFINE:   diff %{o}.link1 %{o}.link2

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/knr12.ll \
; RUN:   -o %t/knr12.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/knr12.ll \
; RUN:   -o %t/knr12.win.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/knr12.ll \
; RUN:   -o %t/knr12.win.o0.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/knr21.ll \
; RUN:   -o %t/knr21.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/knr21.ll \
; RUN:   -o %t/knr21.win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/knr12.lnx.o | FileCheck %s %{full} --check-prefixes=KNR,KNR-LNX -DFN=main
; RUN: llvm-cil-dump --link-info %t/knr12.win.o | FileCheck %s %{full} --check-prefixes=KNR,KNR-WIN -DFN=main
; RUN: llvm-cil-dump --link-info %t/knr12.win.o0.o | FileCheck %s %{full} --check-prefixes=KNR,KNR-WIN -DFN=main
; RUN: llvm-cil-dump --link-info %t/knr21.lnx.o | FileCheck %s %{full} --check-prefixes=KNR,KNR-LNX -DFN=g
; RUN: llvm-cil-dump --link-info %t/knr21.win.o0.o | FileCheck %s %{full} --check-prefixes=KNR,KNR-WIN -DFN=g
; RUN: llvm-cil-dump --link-info %t/knr12.lnx.o | FileCheck %s %{full} --check-prefix=KNR-SIGS
; RUN: llvm-cil-dump --link-info %t/knr12.win.o | FileCheck %s %{full} --check-prefix=KNR-SIGS
; RUN: llvm-cil-dump --link-info %t/knr12.win.o0.o | FileCheck %s %{full} --check-prefix=KNR-SIGS
; RUN: llvm-cil-dump --link-info %t/knr21.lnx.o | FileCheck %s %{full} --check-prefix=KNR-SIGS
; RUN: llvm-cil-dump --link-info %t/knr21.win.o0.o | FileCheck %s %{full} --check-prefix=KNR-SIGS
; RUN: llvm-cil-dump --tables %t/knr12.lnx.o | FileCheck %s %{full} --check-prefixes=KNR12-TBL
; RUN: llvm-cil-dump --tables %t/knr12.win.o0.o | FileCheck %s %{full} --check-prefixes=KNR12-TBL
; RUN: llvm-cil-dump --tables %t/knr21.lnx.o | FileCheck %s %{full} --check-prefixes=KNR21-TBL
; RUN: llvm-cil-dump --tables %t/knr21.win.o0.o | FileCheck %s %{full} --check-prefixes=KNR21-TBL
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/knr12.ll -o %t/knr12.lnx.il 2>&1 | count 0
; REDEFINE: %{o} = %t/knr12.lnx
; RUN: %if ilasm %{ %{oracle} %}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-object-form -verify-machineinstrs < %t/knr21.ll -o %t/knr21.win.o0.il 2>&1 | count 0
; REDEFINE: %{o} = %t/knr21.win.o0
; RUN: %if ilasm %{ %{oracle} %}
; RUN: llvm-cil-dump --canonical %t/knr12.lnx.o \
; RUN:   | FileCheck %s %{full} %{nowrap} --implicit-check-not=.method --check-prefixes=KNR-CANON,KNR12-CANON -DFN=main
; RUN: llvm-cil-dump --canonical %t/knr12.win.o0.o \
; RUN:   | FileCheck %s %{full} %{nowrap} --implicit-check-not=.method --check-prefixes=KNR-CANON,KNR12-CANON -DFN=main
; RUN: llvm-cil-dump --canonical %t/knr21.lnx.o \
; RUN:   | FileCheck %s %{full} %{nowrap} --implicit-check-not=.method --check-prefixes=KNR-CANON,KNR21-CANON -DFN=g
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -stop-after=msil-fix-function-bitcasts \
; RUN:   < %t/knr12.ll -o - | FileCheck %s %{nowrap} --check-prefixes=KNR-IR,KNR12-IR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-object-form -stop-after=msil-fix-function-bitcasts \
; RUN:   < %t/knr21.ll -o - | FileCheck %s %{nowrap} --check-prefixes=KNR-IR,KNR21-IR

; KNR-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; KNR-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; KNR-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT '[[FN]]' method=0x06000001
; KNR-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT UNDEFINED 'f'
; KNR-NOT:{{.}}
; KNR-DAG:Signature 0x0A000001 'f' NO_PROTOTYPE
; KNR-DAG:Signature 0x0A000002 'f' NO_PROTOTYPE
; KNR-NOT:{{.}}
; KNR-SIGS-COUNT-2:{{^Signature .*}}
; KNR-SIGS-NOT:{{^Signature }}

; KNR12-TBL:Table: MemberRef rows=3 rowsize={{[0-9]+}}
; KNR12-TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='f' Signature=00 02 08 08 08
; KNR12-TBL-NEXT:  MemberRef[2]: Class=0x01000001 Name='f' Signature=00 01 08 08
; KNR12-TBL-NEXT:  MemberRef[3]: Class=0x01000002 Name='.ctor' Signature=20 01 01 1D 05
; KNR21-TBL:Table: MemberRef rows=3 rowsize={{[0-9]+}}
; KNR21-TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='f' Signature=00 01 08 08
; KNR21-TBL-NEXT:  MemberRef[2]: Class=0x01000001 Name='f' Signature=00 02 08 08 08
; KNR21-TBL-NEXT:  MemberRef[3]: Class=0x01000002 Name='.ctor' Signature=20 01 01 1D 05

; KNR-CANON:.class '<Module>' flags=0x00000000
; KNR-CANON-NEXT:  .method '[[FN]]' int32() flags=0x0016 impl=0x0000
; KNR12-CANON:    L{{[0-9]+}}: call int32(int32, int32) [msil.extern]<msil>Symbols::'f'
; KNR12-CANON:    L{{[0-9]+}}: call int32(int32) [msil.extern]<msil>Symbols::'f'
; KNR21-CANON:    L{{[0-9]+}}: call int32(int32) [msil.extern]<msil>Symbols::'f'
; KNR21-CANON:    L{{[0-9]+}}: call int32(int32, int32) [msil.extern]<msil>Symbols::'f'
; KNR-CANON:module .custom {{.*}}
; KNR-CANON-NOT:{{.}}

; KNR12-IR:  %call = tail call i32 @f(i32 noundef 1, i32 noundef 2)
; KNR12-IR:  %call1 = tail call i32 @f(i32 noundef 1)
; KNR21-IR:  %call = tail call i32 @f(i32 noundef 1)
; KNR21-IR:  %call1 = tail call i32 @f(i32 noundef 1, i32 noundef 2)
; KNR-IR:  declare i32 @f(...) #[[NP:[0-9]+]]
; KNR-IR:  attributes #[[NP]] = { {{.*}}"no-prototype"{{.*}} }

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/proto.ll \
; RUN:   -o %t/proto.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/proto.ll \
; RUN:   -o %t/proto.win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/proto.lnx.o | FileCheck %s %{full} --check-prefixes=PROTO,PROTO-LNX
; RUN: llvm-cil-dump --link-info %t/proto.win.o0.o | FileCheck %s %{full} --check-prefixes=PROTO,PROTO-WIN
; RUN: llvm-cil-dump --tables %t/proto.lnx.o | FileCheck %s %{full} --check-prefix=PROTO-TBL
; RUN: llvm-cil-dump --canonical %t/proto.win.o0.o \
; RUN:   | FileCheck %s %{full} %{nowrap} --implicit-check-not=.method --check-prefix=PROTO-CANON
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -stop-after=msil-fix-function-bitcasts \
; RUN:   < %t/proto.ll -o - | FileCheck %s %{nowrap} --check-prefix=PROTO-IR
; PROTO-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; PROTO-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; PROTO-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT UNDEFINED 'p'
; PROTO-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT 'caller' method=0x06000001
; PROTO-NOT:{{.}}
; PROTO-TBL:Table: MemberRef rows=3 rowsize={{[0-9]+}}
; PROTO-TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='p' Signature=00 01 08 08
; PROTO-TBL-NEXT:  MemberRef[2]: Class=0x01000001 Name='p' Signature=00 02 08 08 08
; PROTO-TBL-NEXT:  MemberRef[3]: Class=0x01000002 Name='.ctor' Signature=20 01 01 1D 05
; PROTO-CANON:  .method 'caller' int32() flags=0x0016 impl=0x0000
; PROTO-CANON:    L{{[0-9]+}}: call int32(int32) [msil.extern]<msil>Symbols::'p'
; PROTO-CANON:    L{{[0-9]+}}: call int32(int32, int32) [msil.extern]<msil>Symbols::'p'
; PROTO-IR:  declare i32 @p(i32)
; PROTO-IR:  %a = call i32 @p(i32 1)
; PROTO-IR:  %b = call i32 @p(i32 1, i32 2)

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/sysv.ll \
; RUN:   -o %t/sysv.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/sysv.ll \
; RUN:   -o %t/sysv.win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/sysv.lnx.o | FileCheck %s %{full} --check-prefixes=SYSV,SYSV-LNX
; RUN: llvm-cil-dump --link-info %t/sysv.win.o0.o | FileCheck %s %{full} --check-prefixes=SYSV,SYSV-WIN
; RUN: llvm-cil-dump --tables %t/sysv.lnx.o | FileCheck %s %{full} --check-prefix=SYSV-TBL
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -stop-after=msil-fix-function-bitcasts \
; RUN:   < %t/sysv.ll -o - | FileCheck %s %{nowrap} --check-prefix=SYSV-IR
; SYSV-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; SYSV-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; SYSV-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT UNDEFINED 'h'
; SYSV-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT 'caller' method=0x06000001
; SYSV-NEXT:Signature 0x0A000001 'h' NO_PROTOTYPE
; SYSV-NOT:{{.}}
; SYSV-TBL:Table: MemberRef rows=2 rowsize={{[0-9]+}}
; SYSV-TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='h' Signature=00 02 08 08 08
; SYSV-IR:  declare i32 @h(...) #[[NP:[0-9]+]]
; SYSV-IR:  %r = call i32 @h(i32 1, i32 2)
; SYSV-IR:  attributes #[[NP]] = { "no-prototype" }

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/addr.ll \
; RUN:   -o %t/addr.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/addr.ll \
; RUN:   -o %t/addr.win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/addr.lnx.o | FileCheck %s %{full} --check-prefixes=ADDR,ADDR-LNX
; RUN: llvm-cil-dump --link-info %t/addr.win.o0.o | FileCheck %s %{full} --check-prefixes=ADDR,ADDR-WIN
; RUN: llvm-cil-dump --canonical %t/addr.lnx.o | FileCheck %s %{full} --check-prefix=ADDR-CANON \
; RUN:   --implicit-check-not=.method --implicit-check-not=pinvokeimpl --implicit-check-not=moduleref
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/addr.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=ADDR-S --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; ADDR-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; ADDR-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; ADDR-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT UNDEFINED 'k'
; ADDR-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT 'getk' method=0x06000001
; ADDR-NOT:{{.}}
; ADDR-CANON:  .method 'getk' native int() flags=0x0016 impl=0x0000
; ADDR-CANON-NEXT:    .maxstack 1
; ADDR-CANON-NEXT:    L0: ldsfld native int [msil.extern]<msil>FnAddr::'k'
; ADDR-CANON-NEXT:    L1: ret
; ADDR-S: {{^}}error: the signature of prototype-less function 'k' cannot be derived: its address is used, but it is never called directly; declare it with a prototype{{$}}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/fnaddr.ll \
; RUN:   -o %t/fnaddr.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/fnaddr.ll \
; RUN:   -o %t/fnaddr.win.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/fnaddr.ll \
; RUN:   -o %t/fnaddr.win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/fnaddr.lnx.o | FileCheck %s %{full} --check-prefixes=FNA,FNA-LNX
; RUN: llvm-cil-dump --link-info %t/fnaddr.win.o | FileCheck %s %{full} --check-prefixes=FNA,FNA-WIN
; RUN: llvm-cil-dump --link-info %t/fnaddr.win.o0.o | FileCheck %s %{full} --check-prefixes=FNA,FNA-WIN
; RUN: llvm-cil-dump --tables %t/fnaddr.lnx.o | FileCheck %s %{full} --check-prefix=FNA-TBL
; RUN: llvm-cil-dump --tables %t/fnaddr.win.o0.o | FileCheck %s %{full} --check-prefix=FNA-TBL
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form -verify-machineinstrs < %t/fnaddr.ll -o %t/fnaddr.win.il 2>&1 | count 0
; REDEFINE: %{o} = %t/fnaddr.win
; RUN: %if ilasm %{ %{oracle} %}
; RUN: llvm-cil-dump --canonical %t/fnaddr.win.o | FileCheck %s %{full} --check-prefix=FNA-CANON \
; RUN:   --implicit-check-not=pinvokeimpl --implicit-check-not=mscorlib
; FNA-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; FNA-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; FNA-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT 'def' method=0x06000001
; FNA-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_fn'
; FNA-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT 'addr_def' method=0x06000002
; FNA-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT 'addr_ext' method=0x06000003
; FNA-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT 'addr_al' method=0x06000004
; FNA-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT 'call_ext' method=0x06000005
; FNA-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT 'indirect' method=0x06000006
; FNA-NEXT:Symbol 7 FUNCTION GLOBAL DEFAULT 'bcl' method=0x06000007
; FNA-NEXT:Symbol 8 FUNCTION GLOBAL DEFAULT ALIAS 'al' aliasee=0
; FNA-NOT:{{.}}
; FNA-TBL:Table: TypeRef rows=5 rowsize={{[0-9]+}}
; FNA-TBL-NEXT:  TypeRef[1]: ResolutionScope=0x23000001 TypeName='<msil>FnAddr' TypeNamespace=''
; FNA-TBL-NEXT:  TypeRef[2]: ResolutionScope=0x23000001 TypeName='<msil>Symbols' TypeNamespace=''
; FNA-TBL-NEXT:  TypeRef[3]: ResolutionScope=0x23000002 TypeName='Buffer' TypeNamespace='System'
; FNA-TBL-NEXT:  TypeRef[4]: ResolutionScope=0x23000002 TypeName='Interlocked' TypeNamespace='System.Threading'
; FNA-TBL-NEXT:  TypeRef[5]: ResolutionScope=0x23000003 TypeName='<msil>LinkInfo' TypeNamespace=''
; FNA-TBL:Table: MemberRef rows=7 rowsize={{[0-9]+}}
; FNA-TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='def' Signature=06 18
; FNA-TBL-NEXT:  MemberRef[2]: Class=0x01000001 Name='ext_fn' Signature=06 18
; FNA-TBL-NEXT:  MemberRef[3]: Class=0x01000001 Name='al' Signature=06 18
; FNA-TBL-NEXT:  MemberRef[4]: Class=0x01000002 Name='ext_fn' Signature=00 01 08 08
; FNA-TBL-NEXT:  MemberRef[5]: Class=0x01000003 Name='MemoryCopy' Signature=00 04 01 0F 01 0F 01 0A 0A
; FNA-TBL-NEXT:  MemberRef[6]: Class=0x01000004 Name='MemoryBarrier' Signature=00 00 01
; FNA-TBL-NEXT:  MemberRef[7]: Class=0x01000005 Name='.ctor' Signature=20 01 01 1D 05
; FNA-TBL:Table: StandAloneSig rows={{[0-9]+}} rowsize={{[0-9]+}}
; FNA-TBL:  StandAloneSig[{{[0-9]+}}]: Signature=00 01 08 08
; FNA-CANON:  .method 'addr_al' native int() flags=0x0016 impl=0x0000
; FNA-CANON-NEXT:    .maxstack 1
; FNA-CANON-NEXT:    L0: ldsfld native int [msil.extern]<msil>FnAddr::'al'
; FNA-CANON-NEXT:    L1: ret
; FNA-CANON-NEXT:  .method 'addr_def' native int() flags=0x0016 impl=0x0000
; FNA-CANON-NEXT:    .maxstack 1
; FNA-CANON-NEXT:    L0: ldsfld native int [msil.extern]<msil>FnAddr::'def'
; FNA-CANON-NEXT:    L1: ret
; FNA-CANON-NEXT:  .method 'addr_ext' native int() flags=0x0016 impl=0x0000
; FNA-CANON-NEXT:    .maxstack 1
; FNA-CANON-NEXT:    L0: ldsfld native int [msil.extern]<msil>FnAddr::'ext_fn'
; FNA-CANON-NEXT:    L1: ret
; FNA-CANON-NEXT:  .method 'bcl' void(native int, native int, native int) flags=0x0016 impl=0x0000
; FNA-CANON:    L{{[0-9]+}}: call void(void*, void*, int64, int64) [msil.bcl]System.Buffer::'MemoryCopy'
; FNA-CANON:    L{{[0-9]+}}: call void() [msil.bcl]System.Threading.Interlocked::'MemoryBarrier'
; FNA-CANON:  .method 'call_ext' int32(int32) flags=0x0016 impl=0x0000
; FNA-CANON:    L{{[0-9]+}}: call int32(int32) [msil.extern]<msil>Symbols::'ext_fn'
; FNA-CANON:  .method 'def' int32(int32) flags=0x0016 impl=0x0000
; FNA-CANON-NOT:  .method 'ext_fn'
; FNA-CANON:  .method 'indirect' int32(native int, int32) flags=0x0016 impl=0x0000
; FNA-CANON:    L{{[0-9]+}}: calli int32(int32)
; FNA-CANON-NOT:  .method

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/sigs.ll \
; RUN:   -o %t/sigs.lnx.o 2>&1 | FileCheck %s --check-prefix=R3-WARN --implicit-check-not=warning: --implicit-check-not=error:
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/sigs.ll \
; RUN:   -o %t/sigs.win.o 2>&1 | FileCheck %s --check-prefix=R3-WARN --implicit-check-not=warning: --implicit-check-not=error:
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -O0 -verify-machineinstrs < %t/sigs.ll \
; RUN:   -o %t/sigs.lnx.o0.o 2>&1 | FileCheck %s --check-prefix=R3-WARN --implicit-check-not=warning: --implicit-check-not=error:
; RUN: llvm-cil-dump --link-info %t/sigs.lnx.o | FileCheck %s %{full} --check-prefixes=SIG,SIG-LNX
; RUN: llvm-cil-dump --link-info %t/sigs.win.o | FileCheck %s %{full} --check-prefixes=SIG,SIG-WIN
; RUN: llvm-cil-dump --link-info %t/sigs.lnx.o0.o | FileCheck %s %{full} --check-prefixes=SIG,SIG-LNX
; RUN: llvm-cil-dump --link-info %t/sigs.lnx.o | FileCheck %s %{full} --check-prefix=SIG-SIGS
; RUN: llvm-cil-dump --link-info %t/sigs.win.o | FileCheck %s %{full} --check-prefix=SIG-SIGS
; RUN: llvm-cil-dump --link-info %t/sigs.lnx.o0.o | FileCheck %s %{full} --check-prefix=SIG-SIGS
; RUN: llvm-cil-dump --tables %t/sigs.lnx.o | FileCheck %s %{full} --check-prefix=SIG-TBL
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/sigs.ll -o %t/sigs.lnx.il 2>/dev/null
; REDEFINE: %{o} = %t/sigs.lnx
; RUN: %if ilasm %{ %{oracle} %}
; R3-WARN: {{^}}warning: {{.*}}calls to 'ext_r3' pass incompatible arguments; the object records the first call's layout{{$}}
; SIG-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; SIG-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; SIG-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT 'mk' method=0x06000001
; SIG-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT 'take' method=0x06000002
; SIG-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT 'both' method=0x06000003
; SIG-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_mk'
; SIG-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_take'
; SIG-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_np'
; SIG-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_r3'
; SIG-NEXT:Symbol 7 FUNCTION GLOBAL DEFAULT 'caller' method=0x06000004
; SIG-NOT:{{.}}
; SIG-DAG:Signature 0x06000001 'mk' SRET size=24 align=8
; SIG-DAG:Signature 0x06000002 'take' byval=0:24:8
; SIG-DAG:Signature 0x06000003 'both' SRET size=24 align=8 byval=2:24:8
; SIG-DAG:Signature 0x0A000001 'ext_mk' SRET size=24 align=8
; SIG-DAG:Signature 0x0A000002 'ext_take' byval=0:24:8
; SIG-DAG:Signature 0x0A000003 'ext_np' NO_PROTOTYPE
; SIG-DAG:Signature 0x0A000004 'ext_r3' byval=0:24:8
; SIG-NOT:{{.}}
; SIG-SIGS-COUNT-7:{{^Signature .*}}
; SIG-SIGS-NOT:{{^Signature }}
; SIG-TBL:Table: MemberRef rows=5 rowsize={{[0-9]+}}
; SIG-TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='ext_mk' Signature=00 02 01 18 08
; SIG-TBL-NEXT:  MemberRef[2]: Class=0x01000001 Name='ext_take' Signature=00 01 08 18
; SIG-TBL-NEXT:  MemberRef[3]: Class=0x01000001 Name='ext_np' Signature=00 01 08 08
; SIG-TBL-NEXT:  MemberRef[4]: Class=0x01000001 Name='ext_r3' Signature=00 01 08 18
; SIG-TBL-NEXT:  MemberRef[5]: Class=0x01000002 Name='.ctor' Signature=20 01 01 1D 05

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/libcalls.ll \
; RUN:   -o %t/libcalls.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/libcalls.ll \
; RUN:   -o %t/libcalls.win.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -O0 -verify-machineinstrs < %t/libcalls.ll \
; RUN:   -o %t/libcalls.lnx.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/libcalls.lnx.o | FileCheck %s %{full} --check-prefixes=LIB,LIB-LNX
; RUN: llvm-cil-dump --link-info %t/libcalls.win.o | FileCheck %s %{full} --check-prefixes=LIB,LIB-WIN
; RUN: llvm-cil-dump --link-info %t/libcalls.lnx.o0.o | FileCheck %s %{full} --check-prefixes=LIB,LIB-LNX
; DEFINE: %{libc} = FileCheck %s %{full} --implicit-check-not=pinvokeimpl --implicit-check-not=moduleref \
; DEFINE:   --implicit-check-not=ucrtbase --implicit-check-not=libm.so.6 --implicit-check-not="'libc'"
; RUN: llvm-cil-dump --canonical %t/libcalls.lnx.o | %{libc} --check-prefixes=LIBC,LIBC-LNX
; RUN: llvm-cil-dump --canonical %t/libcalls.win.o | %{libc} --check-prefixes=LIBC,LIBC-WIN
; LIB-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; LIB-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; LIB-NEXT:Symbol 0 DATA LOCAL DEFAULT 'msg' field=0x04000001 segment=0 offset=0 size=3
; LIB-NEXT:Symbol 1 DATA GLOBAL DEFAULT 'on_error' field=0x04000002 segment=1 offset=0 size=8
; LIB-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT UNDEFINED 'sqrt'
; LIB-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED 'puts'
; LIB-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT UNDEFINED 'malloc'
; LIB-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT UNDEFINED 'abort'
; LIB-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT 'sqrt_c' method=0x06000001
; LIB-NEXT:Symbol 7 FUNCTION GLOBAL DEFAULT 'sqrt_i' method=0x06000002
; LIB-NEXT:Symbol 8 FUNCTION GLOBAL DEFAULT 'e10' method=0x06000003
; LIB-NEXT:Symbol 9 FUNCTION GLOBAL DEFAULT 'powi' method=0x06000004
; LIB-NEXT:Symbol 10 FUNCTION GLOBAL DEFAULT 'lr' method=0x06000005
; LIB-NEXT:Symbol 11 FUNCTION GLOBAL DEFAULT 'hello' method=0x06000006
; LIB-NEXT:Symbol 12 FUNCTION GLOBAL DEFAULT 'alloc' method=0x06000007
; LIB-NEXT:Symbol 13 FUNCTION GLOBAL DEFAULT UNDEFINED 'exp10'
; LIB-LNX-NEXT:Symbol 14 FUNCTION GLOBAL DEFAULT UNDEFINED '__powidf2'
; LIB-WIN-NEXT:Symbol 14 FUNCTION GLOBAL DEFAULT UNDEFINED 'pow'
; LIB-NEXT:Symbol 15 FUNCTION GLOBAL DEFAULT UNDEFINED 'lround'
; LIB-NEXT:Segment 0 '.rodata' align=1 size=3 flags=STRINGS|READONLY
; LIB-NEXT:  68 69 00
; LIB-NEXT:Segment 1 '.data' align=8 size=8 flags=0
; LIB-NEXT:  00 00 00 00 00 00 00 00
; LIB-NEXT:Reloc FNADDR64 segment=1 offset=0 symbol=5 'abort' addend=0
; LIB-NOT:{{.}}
; LIBC:moduleref 'msil.unresolved'
; LIBC:  .method 'abort' void() flags=0x2096 impl=0x0080
; LIBC-NEXT:    .pinvokeimpl 'msil.unresolved' entry 'abort' flags=0x0201
; LIBC-NEXT:  .method 'alloc' native int(native int) flags=0x0016 impl=0x0000
; LIBC:    L{{[0-9]+}}: call native int(native int) [msil.extern]<msil>Symbols::'malloc'
; LIBC:  .method 'e10' float64(float64) flags=0x0016 impl=0x0000
; LIBC:    L{{[0-9]+}}: call float64(float64) [msil.extern]<msil>Symbols::'exp10'
; LIBC:  .method 'hello' int32() flags=0x0016 impl=0x0000
; LIBC:    L{{[0-9]+}}: call int32(native int) [msil.extern]<msil>Symbols::'puts'
; LIBC:  .method 'lr' native int(float64) flags=0x0016 impl=0x0000
; LIBC:    L{{[0-9]+}}: call native int(float64) [msil.extern]<msil>Symbols::'lround'
; LIBC:  .method 'powi' float64(float64, int32) flags=0x0016 impl=0x0000
; LIBC-LNX:    L{{[0-9]+}}: call float64(float64, int32) [msil.extern]<msil>Symbols::'__powidf2'
; LIBC-WIN:    L{{[0-9]+}}: call float64(float64, float64) [msil.extern]<msil>Symbols::'pow'
; LIBC:  .method 'sqrt_c' float64(float64) flags=0x0016 impl=0x0000
; LIBC:    L{{[0-9]+}}: call float64(float64) [msil.extern]<msil>Symbols::'sqrt'
; LIBC:  .method 'sqrt_i' float64(float64) flags=0x0016 impl=0x0000
; LIBC:    L{{[0-9]+}}: call float64(float64) [msil.extern]<msil>Symbols::'sqrt'
; LIBC-NOT:  .method

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/atomics.ll \
; RUN:   -o %t/atomics.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/atomics.ll \
; RUN:   -o %t/atomics.win.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/atomics.ll \
; RUN:   -o %t/atomics.win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/atomics.lnx.o | FileCheck %s %{full} --check-prefixes=ATOM,ATOM-LNX
; RUN: llvm-cil-dump --link-info %t/atomics.win.o | FileCheck %s %{full} --check-prefixes=ATOM,ATOM-WIN
; RUN: llvm-cil-dump --link-info %t/atomics.win.o0.o | FileCheck %s %{full} --check-prefixes=ATOM,ATOM-WIN
; RUN: llvm-cil-dump --canonical %t/atomics.lnx.o | %{libc} --check-prefix=ATOM-CANON
; RUN: llvm-cil-dump --canonical %t/atomics.win.o | %{libc} --check-prefix=ATOM-CANON
; RUN: llvm-cil-dump --canonical %t/atomics.win.o0.o | %{libc} --check-prefix=ATOM-CANON
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/atomics.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=ATOM-S --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; ATOM-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; ATOM-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; ATOM-NEXT:Symbol 0 DATA GLOBAL DEFAULT 'counter' field=0x04000001 segment=0 offset=0 size=4
; ATOM-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT 'inc' method=0x06000001
; ATOM-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT 'ld' method=0x06000002
; ATOM-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED 'putchar'
; ATOM-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT 'pc' method=0x06000003
; ATOM-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT 'h' method=0x06000004
; ATOM-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT UNDEFINED '__atomic_fetch_add_4'
; ATOM-NEXT:Symbol 7 FUNCTION GLOBAL DEFAULT UNDEFINED '__atomic_load_4'
; ATOM-NEXT:Symbol 8 FUNCTION GLOBAL DEFAULT UNDEFINED '__truncsfhf2'
; ATOM-NEXT:Segment 0 '.bss' align=4 size=4 flags=ZERO
; ATOM-NEXT:Signature 0x06000004 'h' NATIVE_INCOMPATIBLE
; ATOM-NOT:{{.}}
; ATOM-CANON:  .method 'inc' int32() flags=0x0016 impl=0x0000
; ATOM-CANON-NOT:  .method
; ATOM-CANON:    L{{[0-9]+}}: call int32(native int, int32, int32) [msil.extern]<msil>Symbols::'__atomic_fetch_add_4'
; ATOM-CANON:  .method 'ld' int32() flags=0x0016 impl=0x0000
; ATOM-CANON-NOT:  .method
; ATOM-CANON:    L{{[0-9]+}}: call int32(native int, int32) [msil.extern]<msil>Symbols::'__atomic_load_4'
; ATOM-CANON:  .method 'pc' int32() flags=0x0016 impl=0x0000
; ATOM-CANON-NOT:  .method
; ATOM-CANON:    L{{[0-9]+}}: call int32(int32) [msil.extern]<msil>Symbols::'putchar'
; ATOM-CANON-NOT:  .method
; ATOM-S-DAG: {{^}}error: undefined function '__atomic_fetch_add_4' is not defined in this module {{.*}}
; ATOM-S-DAG: {{^}}error: undefined function '__atomic_load_4' is not defined in this module {{.*}}
; ATOM-S-DAG: {{^}}error: {{.*}}runtime helper '__truncsfhf2' is not available for MSIL yet (phase 3){{$}}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/ae.ll \
; RUN:   -o %t/ae.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/ae.ll \
; RUN:   -o %t/ae.win.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/ae.ll \
; RUN:   -o %t/ae.win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/ae.lnx.o | FileCheck %s %{full} --check-prefixes=AE,AE-LNX
; RUN: llvm-cil-dump --link-info %t/ae.win.o | FileCheck %s %{full} --check-prefixes=AE,AE-WIN
; RUN: llvm-cil-dump --link-info %t/ae.win.o0.o | FileCheck %s %{full} --check-prefixes=AE,AE-WIN
; RUN: llvm-cil-dump --tables %t/ae.lnx.o | FileCheck %s %{full} --check-prefix=AE-TBL
; RUN: llvm-cil-dump --tables %t/ae.win.o0.o | FileCheck %s %{full} --check-prefix=AE-TBL
; RUN: llvm-cil-dump --canonical %t/ae.win.o \
; RUN:   | FileCheck %s %{full} %{nowrap} --implicit-check-not=.method --check-prefix=AE-CANON
; AE-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; AE-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; AE-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT UNDEFINED 'ae'
; AE-NEXT:Symbol 1 FUNCTION WEAK DEFAULT UNDEFINED 'ew'
; AE-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT 'caller' method=0x06000001
; AE-NOT:{{.}}
; AE-TBL:Table: TypeRef rows=2 rowsize={{[0-9]+}}
; AE-TBL-NEXT:  TypeRef[1]: ResolutionScope=0x23000001 TypeName='<msil>Symbols' TypeNamespace=''
; AE-TBL-NEXT:  TypeRef[2]: ResolutionScope=0x23000003 TypeName='<msil>LinkInfo' TypeNamespace=''
; AE-TBL:Table: MethodDef rows=1 rowsize={{[0-9]+}}
; AE-TBL-NEXT:  MethodDef[1]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='caller' Signature=00 00 08 ParamList={{.*}}
; AE-TBL:Table: MemberRef rows=3 rowsize={{[0-9]+}}
; AE-TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='ae' Signature=00 02 08 08 08
; AE-TBL-NEXT:  MemberRef[2]: Class=0x01000001 Name='ew' Signature=00 02 08 08 08
; AE-TBL-NEXT:  MemberRef[3]: Class=0x01000002 Name='.ctor' Signature=20 01 01 1D 05
; AE-CANON:  .method 'caller' int32() flags=0x0016 impl=0x0000
; AE-CANON:    L{{[0-9]+}}: call int32(int32, int32) [msil.extern]<msil>Symbols::'ae'
; AE-CANON:    L{{[0-9]+}}: call int32(int32, int32) [msil.extern]<msil>Symbols::'ew'

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/defwrap.ll \
; RUN:   -o %t/defwrap.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/defwrap.ll \
; RUN:   -o %t/defwrap.win.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/defwrap.ll \
; RUN:   -o %t/defwrap.win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/defwrap.lnx.o | FileCheck %s %{full} --check-prefixes=DEFW,DEFW-LNX
; RUN: llvm-cil-dump --link-info %t/defwrap.win.o | FileCheck %s %{full} --check-prefixes=DEFW,DEFW-WIN
; RUN: llvm-cil-dump --link-info %t/defwrap.win.o0.o | FileCheck %s %{full} --check-prefixes=DEFW,DEFW-WIN
; RUN: llvm-cil-dump --tables %t/defwrap.lnx.o | FileCheck %s %{full} --check-prefix=DEFW-TBL
; RUN: llvm-cil-dump --tables %t/defwrap.win.o0.o | FileCheck %s %{full} --check-prefix=DEFW-TBL
; RUN: llvm-cil-dump --canonical %t/defwrap.lnx.o | FileCheck %s %{full} --check-prefix=DEFW-CANON
; RUN: llvm-cil-dump --canonical %t/defwrap.win.o | FileCheck %s %{full} --check-prefix=DEFW-CANON
; DEFW-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; DEFW-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; DEFW-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT 'f' method=0x06000001
; DEFW-NEXT:Symbol 1 FUNCTION WEAK DEFAULT 'w' method=0x06000002
; DEFW-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT 'g' method=0x06000003
; DEFW-NEXT:Symbol 3 FUNCTION LOCAL DEFAULT 'f_bitcast' method=0x06000004
; DEFW-NEXT:Symbol 4 FUNCTION LOCAL DEFAULT 'w_bitcast' method=0x06000005
; DEFW-NOT:{{.}}
; DEFW-TBL:Table: TypeRef rows=1 rowsize={{[0-9]+}}
; DEFW-TBL-NEXT:  TypeRef[1]: ResolutionScope=0x23000003 TypeName='<msil>LinkInfo' TypeNamespace=''
; DEFW-TBL:Table: MemberRef rows=1 rowsize={{[0-9]+}}
; DEFW-TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='.ctor' Signature=20 01 01 1D 05
; DEFW-CANON:  .method 'f_bitcast' int32(int32) flags=0x0013 impl=0x0000
; DEFW-CANON-NOT:  .method
; DEFW-CANON:    L{{[0-9]+}}: call int32(int32, int32) '<Module>'::'f'
; DEFW-CANON:  .method 'g' int32() flags=0x0016 impl=0x0000
; DEFW-CANON-NOT:  .method
; DEFW-CANON:    L{{[0-9]+}}: call int32(int32) '<Module>'::'f_bitcast'
; DEFW-CANON-NOT:  .method
; DEFW-CANON:    L{{[0-9]+}}: call int32(int32, int32) '<Module>'::'w_bitcast'
; DEFW-CANON:  .method 'w' int32(int32) flags=0x0016 impl=0x0000
; DEFW-CANON:  .method 'w_bitcast' int32(int32, int32) flags=0x0013 impl=0x0000
; DEFW-CANON-NOT:  .method
; DEFW-CANON:    L{{[0-9]+}}: call int32(int32) '<Module>'::'w'
; DEFW-CANON-NOT:  .method

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/libdef.ll \
; RUN:   -o %t/libdef.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/libdef.ll \
; RUN:   -o %t/libdef.win.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/libdef.ll \
; RUN:   -o %t/libdef.win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/libdef.lnx.o | FileCheck %s %{full} --check-prefixes=LIBDEF,LIBDEF-LNX
; RUN: llvm-cil-dump --link-info %t/libdef.win.o | FileCheck %s %{full} --check-prefixes=LIBDEF,LIBDEF-WIN
; RUN: llvm-cil-dump --link-info %t/libdef.win.o0.o | FileCheck %s %{full} --check-prefixes=LIBDEF,LIBDEF-WIN
; RUN: llvm-cil-dump --tables %t/libdef.lnx.o | FileCheck %s %{full} --check-prefix=LIBDEF-TBL
; RUN: llvm-cil-dump --canonical %t/libdef.lnx.o | FileCheck %s %{full} --check-prefix=LIBDEF-CANON
; RUN: llvm-cil-dump --canonical %t/libdef.win.o0.o | FileCheck %s %{full} --check-prefix=LIBDEF-CANON
; LIBDEF-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; LIBDEF-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; LIBDEF-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT 'use' method=0x06000001
; LIBDEF-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT 'exp10' method=0x06000002
; LIBDEF-NOT:{{.}}
; LIBDEF-TBL:Table: MemberRef rows=1 rowsize={{[0-9]+}}
; LIBDEF-TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='.ctor' Signature=20 01 01 1D 05
; LIBDEF-CANON:  .method 'use' float64(float64) flags=0x0016 impl=0x0000
; LIBDEF-CANON-NOT:  .method
; LIBDEF-CANON:    L{{[0-9]+}}: call float64(float64) '<Module>'::'exp10'
; LIBDEF-CANON-NOT:  .method

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/ninc.ll \
; RUN:   -o %t/ninc.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/ninc.ll \
; RUN:   -o %t/ninc.win.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/ninc.ll \
; RUN:   -o %t/ninc.win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/ninc.lnx.o | FileCheck %s %{full} --check-prefixes=NINC,NINC-LNX
; RUN: llvm-cil-dump --link-info %t/ninc.win.o | FileCheck %s %{full} --check-prefixes=NINC,NINC-WIN
; RUN: llvm-cil-dump --link-info %t/ninc.win.o0.o | FileCheck %s %{full} --check-prefixes=NINC,NINC-WIN
; RUN: llvm-cil-dump --link-info %t/ninc.lnx.o | FileCheck %s %{full} --check-prefix=NINC-SIGS
; RUN: llvm-cil-dump --link-info %t/ninc.win.o | FileCheck %s %{full} --check-prefix=NINC-SIGS
; RUN: llvm-cil-dump --link-info %t/ninc.win.o0.o | FileCheck %s %{full} --check-prefix=NINC-SIGS
; RUN: llvm-cil-dump --tables %t/ninc.lnx.o | FileCheck %s %{full} --check-prefix=NINC-TBL
; RUN: llvm-cil-dump --tables %t/ninc.win.o0.o | FileCheck %s %{full} --check-prefix=NINC-TBL
; NINC-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; NINC-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; NINC-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT 'mk_pair' method=0x06000001
; NINC-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT 'id128' method=0x06000002
; NINC-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT 'lo128' method=0x06000003
; NINC-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_pair'
; NINC-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_lo'
; NINC-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_plain'
; NINC-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT 'caller' method=0x06000004
; NINC-NOT:{{.}}
; NINC-DAG:Signature 0x06000001 'mk_pair' NATIVE_INCOMPATIBLE
; NINC-DAG:Signature 0x06000002 'id128' NATIVE_INCOMPATIBLE
; NINC-DAG:Signature 0x06000003 'lo128' NATIVE_INCOMPATIBLE
; NINC-DAG:Signature 0x0A000001 'ext_pair' NATIVE_INCOMPATIBLE
; NINC-DAG:Signature 0x0A000002 'ext_lo' NATIVE_INCOMPATIBLE
; NINC-NOT:{{.}}
; NINC-SIGS-COUNT-5:{{^Signature .*}}
; NINC-SIGS-NOT:{{^Signature }}
; NINC-TBL:Table: MethodDef rows=4 rowsize={{[0-9]+}}
; NINC-TBL-NEXT:  MethodDef[1]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='mk_pair' Signature=00 03 01 18 18 18 ParamList={{.*}}
; NINC-TBL-NEXT:  MethodDef[2]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='id128' Signature=00 03 01 18 18 18 ParamList={{.*}}
; NINC-TBL-NEXT:  MethodDef[3]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='lo128' Signature=00 02 18 18 18 ParamList={{.*}}
; NINC-TBL-NEXT:  MethodDef[4]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='caller' Signature=00 01 18 18 ParamList={{.*}}
; NINC-TBL:Table: MemberRef rows=4 rowsize={{[0-9]+}}
; NINC-TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='ext_pair' Signature=00 02 01 18 08
; NINC-TBL-NEXT:  MemberRef[2]: Class=0x01000001 Name='ext_lo' Signature=00 02 18 18 18
; NINC-TBL-NEXT:  MemberRef[3]: Class=0x01000001 Name='ext_plain' Signature=00 01 18 18
; NINC-TBL-NEXT:  MemberRef[4]: Class=0x01000002 Name='.ctor' Signature=20 01 01 1D 05

;--- knr12.ll
source_filename = "knr-a.c"

define i32 @main() #0 {
entry:
  %call = tail call i32 @f(i32 noundef 1, i32 noundef 2) #2
  %tobool.not = icmp eq i32 %call, 0
  br i1 %tobool.not, label %if.then, label %cleanup

if.then:
  %call1 = tail call i32 @f(i32 noundef 1) #2
  br label %cleanup

cleanup:
  %retval.0 = phi i32 [ %call1, %if.then ], [ %call, %entry ]
  ret i32 %retval.0
}

declare i32 @f(...) #1

attributes #0 = { nounwind "no-builtins" }
attributes #1 = { "no-builtins" "no-prototype" }
attributes #2 = { nobuiltin nounwind "no-builtins" }

;--- knr21.ll
source_filename = "knr-b.c"

define i32 @g() #0 {
entry:
  %call = tail call i32 @f(i32 noundef 1) #2
  %tobool.not = icmp eq i32 %call, 0
  br i1 %tobool.not, label %if.then, label %cleanup

if.then:
  %call1 = tail call i32 @f(i32 noundef 1, i32 noundef 2) #2
  br label %cleanup

cleanup:
  %retval.0 = phi i32 [ %call1, %if.then ], [ %call, %entry ]
  ret i32 %retval.0
}

declare i32 @f(...) #1

attributes #0 = { nounwind "no-builtins" }
attributes #1 = { "no-builtins" "no-prototype" }
attributes #2 = { nobuiltin nounwind "no-builtins" }

;--- proto.ll
source_filename = "proto.c"

declare i32 @p(i32)

define i32 @caller() {
  %a = call i32 @p(i32 1)
  %b = call i32 @p(i32 1, i32 2)
  %r = add i32 %a, %b
  ret i32 %r
}

;--- sysv.ll
source_filename = "sysv.c"

declare i32 @h(...) #0

define i32 @caller() {
  %r = call i32 (i32, i32, ...) @h(i32 1, i32 2)
  ret i32 %r
}

attributes #0 = { "no-prototype" }

;--- addr.ll
source_filename = "addr.c"

declare i32 @k(...) #0

define ptr @getk() {
  ret ptr @k
}

attributes #0 = { "no-prototype" }

;--- fnaddr.ll
source_filename = "fnaddr.c"

@al = alias i32 (i32), ptr @def

define i32 @def(i32 %x) {
  ret i32 %x
}

declare i32 @ext_fn(i32)

define ptr @addr_def() {
  ret ptr @def
}

define ptr @addr_ext() {
  ret ptr @ext_fn
}

define ptr @addr_al() {
  ret ptr @al
}

define i32 @call_ext(i32 %v) {
  %r = call i32 @ext_fn(i32 %v)
  ret i32 %r
}

define i32 @indirect(ptr %p, i32 %v) {
  %r = call i32 %p(i32 %v)
  ret i32 %r
}

define void @bcl(ptr %d, ptr %s, i64 %n) {
  call void @llvm.memmove.p0.p0.i64(ptr %d, ptr %s, i64 %n, i1 false)
  fence seq_cst
  ret void
}

declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1)

;--- sigs.ll
source_filename = "sigs.c"

%struct.big = type { i64, i64, i64 }

define void @mk(ptr sret(%struct.big) align 8 %out, i32 %x) {
  store i32 %x, ptr %out, align 8
  ret void
}

define i32 @take(ptr byval(%struct.big) align 8 %s) {
  %v = load i32, ptr %s, align 8
  ret i32 %v
}

define void @both(ptr sret(%struct.big) align 8 %out, i32 %a, ptr byval(%struct.big) align 8 %s) {
  store i32 %a, ptr %out, align 8
  ret void
}

declare void @ext_mk(ptr sret(%struct.big) align 8, i32)

declare i32 @ext_take(ptr byval(%struct.big) align 8)

declare i32 @ext_np(...) #0

declare i32 @ext_r3(ptr)

define i32 @caller(ptr %p) {
  %tmp = alloca %struct.big, align 8
  call void @ext_mk(ptr sret(%struct.big) align 8 %tmp, i32 1)
  %a = call i32 @ext_take(ptr byval(%struct.big) align 8 %tmp)
  %b = call i32 @ext_np(i32 3)
  %c = call i32 @ext_r3(ptr byval(%struct.big) align 8 %tmp)
  %d = call i32 @ext_r3(ptr %p)
  %ab = add i32 %a, %b
  %cd = add i32 %c, %d
  %r = add i32 %ab, %cd
  ret i32 %r
}

attributes #0 = { "no-prototype" }

;--- libcalls.ll
source_filename = "libcalls.c"

@msg = private unnamed_addr constant [3 x i8] c"hi\00", align 1
@on_error = global ptr @abort, align 8

declare double @sqrt(double)

declare i32 @puts(ptr)

declare ptr @malloc(i64)

declare void @abort()

define double @sqrt_c(double %x) {
  %r = call double @sqrt(double %x)
  ret double %r
}

define double @sqrt_i(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

define double @e10(double %x) {
  %r = call double @llvm.exp10.f64(double %x)
  ret double %r
}

define double @powi(double %x, i32 %n) {
  %r = call double @llvm.powi.f64.i32(double %x, i32 %n)
  ret double %r
}

define i64 @lr(double %x) {
  %r = call i64 @llvm.lround.i64.f64(double %x)
  ret i64 %r
}

define i32 @hello() {
  %r = call i32 @puts(ptr @msg)
  ret i32 %r
}

define ptr @alloc(i64 %n) {
  %p = call ptr @malloc(i64 %n)
  ret ptr %p
}

declare double @llvm.sqrt.f64(double)
declare double @llvm.exp10.f64(double)
declare double @llvm.powi.f64.i32(double, i32)
declare i64 @llvm.lround.i64.f64(double)

;--- atomics.ll
source_filename = "atomics.c"

@counter = global i32 0, align 4

define i32 @inc() {
  %r = atomicrmw add ptr @counter, i32 1 seq_cst, align 4
  ret i32 %r
}

define i32 @ld() {
  %r = load atomic i32, ptr @counter seq_cst, align 4
  ret i32 %r
}

declare i32 @putchar(i32)

define i32 @pc() {
  %r = call i32 @putchar(i32 65)
  ret i32 %r
}

define half @h(float %x) {
  %r = fptrunc float %x to half
  ret half %r
}

;--- ae.ll
source_filename = "ae.c"

define available_externally i32 @ae(i32 %x) {
  ret i32 %x
}

declare extern_weak i32 @ew(i32)

define i32 @caller() {
  %a = call i32 @ae(i32 1, i32 2)
  %b = call i32 @ew(i32 1, i32 2)
  %r = add i32 %a, %b
  ret i32 %r
}

;--- defwrap.ll
source_filename = "defwrap.c"

define i32 @f(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

define weak i32 @w(i32 %a) {
  ret i32 %a
}

define i32 @g() {
  %x = call i32 @f(i32 1)
  %y = call i32 @w(i32 1, i32 2)
  %r = add i32 %x, %y
  ret i32 %r
}

;--- libdef.ll
source_filename = "libdef.c"

define double @use(double %x) {
  %e = call double @llvm.exp10.f64(double %x)
  ret double %e
}

define double @exp10(double %x) {
  ret double %x
}

declare double @llvm.exp10.f64(double)

;--- ninc.ll
source_filename = "ninc.c"

define { i64, i64 } @mk_pair(i64 %a, i64 %b) {
  %p0 = insertvalue { i64, i64 } poison, i64 %a, 0
  %p1 = insertvalue { i64, i64 } %p0, i64 %b, 1
  ret { i64, i64 } %p1
}

define i128 @id128(i128 %v) {
  ret i128 %v
}

define i64 @lo128(i128 %v) {
  %t = trunc i128 %v to i64
  ret i64 %t
}

declare { i64, i64 } @ext_pair(i32)

declare i64 @ext_lo(i128)

declare i64 @ext_plain(i64)

define i64 @caller(i64 %x) {
  %p = call { i64, i64 } @ext_pair(i32 1)
  %a = extractvalue { i64, i64 } %p, 0
  %w = zext i64 %x to i128
  %b = call i64 @ext_lo(i128 %w)
  %c = call i64 @ext_plain(i64 %x)
  %ab = add i64 %a, %b
  %r = add i64 %ab, %c
  ret i64 %r
}
