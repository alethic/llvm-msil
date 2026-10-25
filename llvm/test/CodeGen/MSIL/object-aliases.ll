; Aliases in a CIL object (ImplementationPlan.md 5.8.3 R1/R2, 5.8.8 and
; 5.8.10; M19; judge-c-fidelity F5 and probe p1a.c). In object mode an alias
; whose aliasee is a definition plus a constant offset (offset 0 for
; functions) is an ALIAS symbol with the alias's own linkage and visibility,
; after the functions in SYMTAB, in IR order: Aliasee is the defined symbol,
; and a data alias also carries its offset into the aliasee and its size. It
; has no metadata row, and nothing is lowered: no forwarding method and no
; rewritten uses (M12's whole-program lowering does not apply).
; - R1: every reference to an alias goes through a placeholder MemberRef on
;   [msil.extern]'<msil>Symbols' (a call of a function alias, a load of a
;   data alias), never through the aliasee's own row, so that a strong
;   definition elsewhere still overrides a weak alias for uses in its own TU
;   (p1a.c's 144).
; - R2: the address of a function alias is the FnAddr placeholder
;   [msil.extern]'<msil>FnAddr'::'<alias>', in code and as a FNADDR64
;   relocation in data; a pointer to a data alias is an ADDR64 relocation on
;   the alias symbol.
; - A data alias whose aliasee is another data alias plus an offset
;   (tail_next, 4 bytes past arr_tail) resolves to the base definition with
;   the summed offset (aliasee=0 offset=12): an Aliasee is a defined
;   non-alias symbol (5.8.8).
; - SYMTAB order (5.8.8): aliases in IR order come after the functions, and
;   the symbols that code generation creates (lib's library call exp10)
;   come after the aliases.
; Aliases whose kind differs from their aliasee's are object-errors.ll's.
; Both x86_64 triples at -O2 and -O0.

; REQUIRES: llvm-cil-dump
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %s -o %t.lnx.o 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %s -o %t.win.o 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %s -o %t.win.o0.o 2>&1 \
; RUN:   | count 0
; RUN: llvm-cil-dump --link-info %t.lnx.o \
; RUN:   | FileCheck %s --check-prefixes=CHECK,LNX --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t.win.o \
; RUN:   | FileCheck %s --check-prefixes=CHECK,WIN --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t.win.o0.o \
; RUN:   | FileCheck %s --check-prefixes=CHECK,WIN --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --canonical %t.lnx.o | FileCheck %s --check-prefix=CANON --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --canonical %t.win.o0.o | FileCheck %s --check-prefix=CANON --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --tables %t.win.o | FileCheck %s --check-prefix=TBL --match-full-lines --strict-whitespace

; The object oracle (5.12.6, as in object-form-text.ll): ilasm's object of
; the object-form text is canonically equal to llc's object, record and
; tokens included.
; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{o} =
; DEFINE: %{oracle} = %ilasm-verify -output=%{o}.ilasm.o %{o}.il && \
; DEFINE:   %{canon} %{o}.o > %{o}.canon1 && %{canon} %{o}.ilasm.o > %{o}.canon2 && diff %{o}.canon1 %{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %{o}.o > %{o}.link1 && llvm-cil-dump --link-info %{o}.ilasm.o > %{o}.link2 && \
; DEFINE:   diff %{o}.link1 %{o}.link2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %s -o %t.lnx.il 2>&1 | count 0
; REDEFINE: %{o} = %t.lnx
; RUN: %if ilasm %{ %{oracle} %}

; LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; CHECK-NEXT:Symbol 0 DATA GLOBAL DEFAULT 'base_arr' field=0x04000001 segment=0 offset=0 size=16
; CHECK-NEXT:Symbol 1 DATA GLOBAL DEFAULT 'pa' field=0x04000002 segment=1 offset=0 size=8
; CHECK-NEXT:Symbol 2 DATA GLOBAL DEFAULT 'fp' field=0x04000003 segment=2 offset=0 size=8
; CHECK-NEXT:Symbol 3 FUNCTION LOCAL DEFAULT 'dummy_hook' method=0x06000001
; CHECK-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT 'impl' method=0x06000002
; CHECK-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT 'main' method=0x06000003
; CHECK-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT 'get_tail' method=0x06000004
; CHECK-NEXT:Symbol 7 FUNCTION GLOBAL DEFAULT 'addr_hook' method=0x06000005
; CHECK-NEXT:Symbol 8 FUNCTION GLOBAL DEFAULT 'lib' method=0x06000006
; CHECK-NEXT:Symbol 9 DATA GLOBAL DEFAULT ALIAS 'arr_alias' aliasee=0 offset=0 size=16
; CHECK-NEXT:Symbol 10 DATA GLOBAL HIDDEN ALIAS 'arr_tail' aliasee=0 offset=8 size=4
; CHECK-NEXT:Symbol 11 DATA WEAK DEFAULT ALIAS 'weak_elem' aliasee=0 offset=4 size=4
; CHECK-NEXT:Symbol 12 DATA GLOBAL DEFAULT ALIAS 'tail_next' aliasee=0 offset=12 size=4
; CHECK-NEXT:Symbol 13 FUNCTION WEAK DEFAULT ALIAS 'hook' aliasee=3
; CHECK-NEXT:Symbol 14 FUNCTION GLOBAL DEFAULT ALIAS 'strong_alias' aliasee=4
; CHECK-NEXT:Symbol 15 FUNCTION LOCAL DEFAULT ALIAS 'local_alias' aliasee=4
; CHECK-NEXT:Symbol 16 FUNCTION GLOBAL DEFAULT UNDEFINED 'exp10'
; CHECK-NEXT:Segment 0 '.data' align=4 size=16 flags=0
; CHECK-NEXT:  01 00 00 00 02 00 00 00 03 00 00 00 04 00 00 00
; CHECK-NEXT:Segment 1 '.data' align=8 size=8 flags=0
; CHECK-NEXT:  00 00 00 00 00 00 00 00
; CHECK-NEXT:Segment 2 '.data' align=8 size=8 flags=0
; CHECK-NEXT:  00 00 00 00 00 00 00 00
; CHECK-NEXT:Reloc ADDR64 segment=1 offset=0 symbol=9 'arr_alias' addend=0
; CHECK-NEXT:Reloc FNADDR64 segment=2 offset=0 symbol=13 'hook' addend=0
; CHECK-NOT:{{.}}

; Six methods and no forwarder: the alias names occur only as placeholder
; members, through '<msil>Symbols' for calls and loads and through
; '<msil>FnAddr' for the address.
; CANON:.class '<Module>' flags=0x00000000
; CANON-NEXT:  .field 'base_arr' native int flags=0x0036
; CANON-NEXT:  .field 'fp' native int flags=0x0036
; CANON-NEXT:  .field 'pa' native int flags=0x0036
; CANON-NEXT:  .method 'addr_hook' native int() flags=0x0016 impl=0x0000
; CANON-NEXT:    .maxstack 1
; CANON-NEXT:    L0: ldsfld native int [msil.extern]<msil>FnAddr::'hook'
; CANON-NEXT:    L1: ret
; CANON-NEXT:  .method 'dummy_hook' int32() flags=0x0013 impl=0x0000
; CANON:  .method 'get_tail' int32() flags=0x0016 impl=0x0000
; CANON-NEXT:    .maxstack {{[0-9]+}}
; CANON-NEXT:    L0: ldsfld native int [msil.extern]<msil>Symbols::'arr_tail'
; CANON-NEXT:    L1: ldind.i4
; CANON-NEXT:    L2: ret
; CANON-NEXT:  .method 'impl' int32() flags=0x0016 impl=0x0000
; CANON:  .method 'lib' float64(float64) flags=0x0016 impl=0x0000
; CANON-NOT:  .method
; CANON:    L{{[0-9]+}}: call float64(float64) [msil.extern]<msil>Symbols::'exp10'
; CANON:  .method 'main' int32() flags=0x0016 impl=0x0000
; CANON-NOT:  .method
; CANON:    L{{[0-9]+}}: call int32() [msil.extern]<msil>Symbols::'hook'
; CANON-NOT:  .method
; CANON:    L{{[0-9]+}}: call int32() [msil.extern]<msil>Symbols::'strong_alias'
; CANON-NOT:  .method
; CANON:    L{{[0-9]+}}: call int32() [msil.extern]<msil>Symbols::'local_alias'
; CANON-NOT:  .method
; CANON:module .custom {{.*}}

; The placeholder MemberRefs, in first-appearance order (R6: main's calls,
; get_tail's load, addr_hook's FnAddr, then lib's library call), each with
; its call-site or field signature.
; TBL:Table: TypeRef rows=3 rowsize={{[0-9]+}}
; TBL-NEXT:  TypeRef[1]: ResolutionScope=0x23000001 TypeName='<msil>Symbols' TypeNamespace=''
; TBL-NEXT:  TypeRef[2]: ResolutionScope=0x23000001 TypeName='<msil>FnAddr' TypeNamespace=''
; TBL-NEXT:  TypeRef[3]: ResolutionScope=0x23000003 TypeName='<msil>LinkInfo' TypeNamespace=''
; TBL:Table: MemberRef rows=7 rowsize={{[0-9]+}}
; TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='hook' Signature=00 00 08
; TBL-NEXT:  MemberRef[2]: Class=0x01000001 Name='strong_alias' Signature=00 00 08
; TBL-NEXT:  MemberRef[3]: Class=0x01000001 Name='local_alias' Signature=00 00 08
; TBL-NEXT:  MemberRef[4]: Class=0x01000001 Name='arr_tail' Signature=06 18
; TBL-NEXT:  MemberRef[5]: Class=0x01000002 Name='hook' Signature=06 18
; TBL-NEXT:  MemberRef[6]: Class=0x01000001 Name='exp10' Signature=00 01 0D 0D
; TBL-NEXT:  MemberRef[7]: Class=0x01000003 Name='.ctor' Signature=20 01 01 1D 05

source_filename = "aliases.c"

@base_arr = global [4 x i32] [i32 1, i32 2, i32 3, i32 4], align 4
@pa = global ptr @arr_alias, align 8
@fp = global ptr @hook, align 8

@arr_alias = alias [4 x i32], ptr @base_arr
@arr_tail = hidden alias i32, getelementptr (i8, ptr @base_arr, i64 8)
@weak_elem = weak alias i32, getelementptr (i8, ptr @base_arr, i64 4)
@tail_next = alias i32, getelementptr (i8, ptr @arr_tail, i64 4)
@hook = weak alias i32 (), ptr @dummy_hook
@strong_alias = alias i32 (), ptr @impl
@local_alias = internal alias i32 (), ptr @impl

define internal i32 @dummy_hook() {
  ret i32 1
}

define i32 @impl() {
  ret i32 4
}

define i32 @main() {
  %h = call i32 @hook()
  %s = call i32 @strong_alias()
  %l = call i32 @local_alias()
  %hs = add i32 %h, %s
  %r = add i32 %hs, %l
  ret i32 %r
}

define i32 @get_tail() {
  %t = load i32, ptr @arr_tail, align 4
  ret i32 %t
}

define ptr @addr_hook() {
  ret ptr @hook
}

define double @lib(double %x) {
  %r = call double @llvm.exp10.f64(double %x)
  ret double %r
}

declare double @llvm.exp10.f64(double)
