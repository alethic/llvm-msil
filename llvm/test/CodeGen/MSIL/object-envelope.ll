; The envelope of a CIL object (ImplementationPlan.md 5.8.2, O1-O7, and 5.8.6;
; M19): llc -filetype=obj writes an ECMA-335 netmodule, never a runnable
; image.
; - O1: a PE32+ image with COFF Machine 0x8664 (x86_64) or 0xAA64 (aarch64)
;   and Characteristics 0x2022 (EXECUTABLE_IMAGE, LARGE_ADDRESS_AWARE, DLL);
;   '.text' plus the COFF view's three marker sections '.data' (0xC0000040),
;   '.rdata' (0x40000040) and '.bss' (0xC0000080), each with VirtualSize,
;   SizeOfRawData and PointerToRawData 0 at the next section-aligned RVA, so
;   SizeOfHeaders grows to 0x400; CorFlags ILONLY and EntryPointToken 0; one
;   COFF symbol per SYMTAB entry (5.8.6). Subsystem 3 is ilasm's for the same
;   netmodule, which the object oracle (5.12.6) compares canonically.
; - O2: no Assembly, File, ExportedType or ManifestResource row.
; - O3: the Module row's Name is the base name of the IR source_filename
;   ('dir/sub/a.c' gives 'a.c'; an empty one gives '<msil>module'), never a
;   name derived from -o; Generation, EncId and EncBaseId are 0.
; - O4: the bytes depend only on the IR and the options: two runs in two
;   directories, with different output names and input paths, are
;   byte-identical (cmp). So is a second compile in the same process: llc
;   -compile-twice (as llvm/test/MC/WebAssembly/compile-twice.ll does for
;   wasm) runs the pass manager and the object writer twice and fails with
;   'Running the pass manager twice changed the output.' if the second
;   object differs, for example by a doubled .method or .field row; a.ll has
;   a public and an internal global and function, so a doubled row of
;   either access would show, and each twice-compiled object equals the
;   single-run object of its triple (cmp), on both x86_64 triples.
; - O5: no .cctor, no '<msil>Main', no entry point, no '<msil>image_t',
;   '<msil>image' or '<msil>data' member, no FieldRVA row and no ClassLayout,
;   although the module defines main, globals with initializers and a
;   constructor (whole-program output has all of them).
; - O6: every member is owned by '<Module>', the only TypeDef.
; - O7: no method body sets InitLocals (D13); the identifier limit is
;   object-errors.ll's.
; - 5.8.3's scaffolding: the AssemblyRefs 'msil.extern', 'msil.bcl' and
;   'msil.link' (0.0.0.0, no key), and the record's carrier, TypeRef
;   [msil.link]'<msil>LinkInfo' with its .ctor(uint8[]) MemberRef (signature
;   20 01 01 1D 05) last and one CustomAttribute on the Module row, whose
;   value is 01 00, the record's length, the record ('MSLK', version 1.0) and
;   00 00 (5.8.7).
; Both x86_64 triples at -O2 and -O0, and aarch64 on Linux and on Windows
; (aarch64-pc-windows-msvc-msil: machine 0xAA64 and the Windows data model
; 8,4,2,8; D6 and exit criterion 1). The -filetype=null lines
; (from obj-unsupported.ll, which pinned the Phase 1 error this milestone
; removes) show that -filetype=null still runs the whole-program pipeline
; without output.

; REQUIRES: llvm-cil-dump
; RUN: rm -rf %t && split-file %s %t && mkdir %t/d1 %t/d2

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/a.ll \
; RUN:   -o %t/lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/a.ll \
; RUN:   -o %t/win.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/a.ll \
; RUN:   -o %t/win.o0.o 2>&1 | count 0
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/a.ll \
; RUN:   -o %t/a64.o 2>&1 | count 0
; RUN: llc -mtriple=aarch64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/a.ll \
; RUN:   -o %t/a64win.o 2>&1 | count 0

; DEFINE: %{hdr} = FileCheck %s --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --headers %t/lnx.o | %{hdr} --check-prefixes=HDR,HDR-X64
; RUN: llvm-cil-dump --headers %t/win.o | %{hdr} --check-prefixes=HDR,HDR-X64
; RUN: llvm-cil-dump --headers %t/win.o0.o | %{hdr} --check-prefixes=HDR,HDR-X64
; RUN: llvm-cil-dump --headers %t/a64.o | %{hdr} --check-prefixes=HDR,HDR-A64
; RUN: llvm-cil-dump --headers %t/a64win.o | %{hdr} --check-prefixes=HDR,HDR-A64

; HDR-X64:Format: PE32+ machine=0x8664 dll
; HDR-X64-NEXT:COFF: sections=4 timestamp=0x{{([0-9A-F]{8})}} characteristics=0x2022 symbols=4
; HDR-X64-NEXT:Optional: imagebase=0x{{([0-9A-F]{16})}} sectionalign=0x2000 filealign=0x200 subsystem=3 dllchars=0x{{([0-9A-F]{4})}} stack=0x{{[0-9A-F]+}},0x{{[0-9A-F]+}} heap=0x{{[0-9A-F]+}},0x{{[0-9A-F]+}}
; HDR-X64-NEXT:Section: .text rva=0x00002000 vsize=0x{{[0-9A-F]+}} raw=0x400 rawsize=0x{{[0-9A-F]+}} flags=0x60000020
; HDR-X64-NEXT:Section: .data rva=0x00004000 vsize=0x0 raw=0x0 rawsize=0x0 flags=0xC0000040
; HDR-X64-NEXT:Section: .rdata rva=0x00004000 vsize=0x0 raw=0x0 rawsize=0x0 flags=0x40000040
; HDR-X64-NEXT:Section: .bss rva=0x00004000 vsize=0x0 raw=0x0 rawsize=0x0 flags=0xC0000080
; HDR-A64:Format: PE32+ machine=0xAA64 dll
; HDR-A64-NEXT:COFF: sections=4 timestamp=0x{{([0-9A-F]{8})}} characteristics=0x2022 symbols=4
; HDR-A64-NEXT:Optional: imagebase=0x{{([0-9A-F]{16})}} sectionalign=0x{{[0-9A-F]+}} filealign=0x{{[0-9A-F]+}} subsystem=3 dllchars=0x{{([0-9A-F]{4})}} stack=0x{{[0-9A-F]+}},0x{{[0-9A-F]+}} heap=0x{{[0-9A-F]+}},0x{{[0-9A-F]+}}
; HDR-A64-NEXT:Section: .text rva=0x{{([0-9A-F]{8})}} vsize=0x{{[0-9A-F]+}} raw=0x400 rawsize=0x{{[0-9A-F]+}} flags=0x60000020
; HDR-A64-NEXT:Section: .data rva=[[MARK:0x[0-9A-F]{8}]] vsize=0x0 raw=0x0 rawsize=0x0 flags=0xC0000040
; HDR-A64-NEXT:Section: .rdata rva=[[MARK]] vsize=0x0 raw=0x0 rawsize=0x0 flags=0x40000040
; HDR-A64-NEXT:Section: .bss rva=[[MARK]] vsize=0x0 raw=0x0 rawsize=0x0 flags=0xC0000080
; HDR-NEXT:CLI: flags=0x00000001 entrypoint=0x00000000 metadata=0x{{([0-9A-F]{8})}},0x{{[0-9A-F]+}} runtime=2.5
; HDR-NEXT:Metadata: version='v4.0.30319' heapsizes=0x{{([0-9A-F]{2})}} valid=0x{{([0-9A-F]{16})}} sorted=0x{{([0-9A-F]{16})}}
; HDR:Mvid: {{([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})}}
; HDR-NOT:{{.}}

; The tables: no Assembly, File, ExportedType, ManifestResource, FieldRVA,
; ClassLayout, ImplMap or ModuleRef row, one TypeDef, and the carrier's
; TypeRef and MemberRef as the only rows of their tables here (nothing else
; is referenced).
; DEFINE: %{tables} = FileCheck %s --check-prefix=TBL --match-full-lines --strict-whitespace \
; DEFINE:   --implicit-check-not="Table: Assembly rows" --implicit-check-not="Table: File " \
; DEFINE:   --implicit-check-not="Table: ExportedType " --implicit-check-not="Table: ManifestResource " \
; DEFINE:   --implicit-check-not="Table: FieldRVA " --implicit-check-not="Table: ClassLayout " \
; DEFINE:   --implicit-check-not="Table: ImplMap " --implicit-check-not="Table: ModuleRef "
; RUN: llvm-cil-dump --tables %t/lnx.o | %{tables}
; RUN: llvm-cil-dump --tables %t/win.o0.o | %{tables}
; RUN: llvm-cil-dump --tables %t/a64.o | %{tables}
; TBL:Table: Module rows=1 rowsize={{[0-9]+}}
; TBL-NEXT:  Module[1]: Generation=0x0000 Name='a.c' Mvid={{[{][0-9A-Fa-f]{8}(-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}[}]}} EncId=0 EncBaseId=0
; TBL-NEXT:Table: TypeRef rows=1 rowsize={{[0-9]+}}
; TBL-NEXT:  TypeRef[1]: ResolutionScope=0x23000003 TypeName='<msil>LinkInfo' TypeNamespace=''
; TBL-NEXT:Table: TypeDef rows=1 rowsize={{[0-9]+}}
; TBL-NEXT:  TypeDef[1]: Flags=0x00000000 TypeName='<Module>' TypeNamespace='' Extends=0 FieldList=0x04000001 MethodList=0x06000001
; TBL-NEXT:Table: Field rows=2 rowsize={{[0-9]+}}
; TBL-NEXT:  Field[1]: Flags=0x0036 Name='counter' Signature=06 18
; TBL-NEXT:  Field[2]: Flags=0x0033 Name='hidden' Signature=06 18
; TBL-NEXT:Table: MethodDef rows=2 rowsize={{[0-9]+}}
; TBL-NEXT:  MethodDef[1]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0013 Name='init' Signature=00 00 01 ParamList={{.*}}
; TBL-NEXT:  MethodDef[2]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='main' Signature=00 00 08 ParamList={{.*}}
; TBL:Table: MemberRef rows=1 rowsize={{[0-9]+}}
; TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='.ctor' Signature=20 01 01 1D 05
; TBL-NEXT:Table: CustomAttribute rows=1 rowsize={{[0-9]+}}
; TBL-NEXT:  CustomAttribute[1]: Parent=0x00000001 Type=0x0A000001 Value=01 00 {{(([0-9A-F]{2} ){4})}}4D 53 4C 4B 01 00 {{([0-9A-F]{2} )+}}00 00
; TBL:Table: AssemblyRef rows=3 rowsize={{[0-9]+}}
; TBL-NEXT:  AssemblyRef[1]: MajorVersion=0x0000 MinorVersion=0x0000 BuildNumber=0x0000 RevisionNumber=0x0000 Flags=0x00000000 PublicKeyOrToken=- Name='msil.extern' Culture='' HashValue=-
; TBL-NEXT:  AssemblyRef[2]: MajorVersion=0x0000 MinorVersion=0x0000 BuildNumber=0x0000 RevisionNumber=0x0000 Flags=0x00000000 PublicKeyOrToken=- Name='msil.bcl' Culture='' HashValue=-
; TBL-NEXT:  AssemblyRef[3]: MajorVersion=0x0000 MinorVersion=0x0000 BuildNumber=0x0000 RevisionNumber=0x0000 Flags=0x00000000 PublicKeyOrToken=- Name='msil.link' Culture='' HashValue=-
; TBL-NOT:Table:

; The canonical listing: no entry point, no assembly line, the members of
; '<Module>' only, no body with InitLocals (O7; the listing prints
; '.maxstack <n> initlocals' for one), and the record as the Module row's
; attribute.
; DEFINE: %{canon} = FileCheck %s --check-prefix=CANON --match-full-lines --strict-whitespace \
; DEFINE:   --implicit-check-not=initlocals \
; DEFINE:   --implicit-check-not=.cctor --implicit-check-not="<msil>Main" --implicit-check-not=entrypoint \
; DEFINE:   --implicit-check-not="<msil>image" --implicit-check-not="<msil>data" --implicit-check-not="<msil>fn_" \
; DEFINE:   --implicit-check-not=".data " --implicit-check-not=".class '<msil>"
; RUN: llvm-cil-dump --canonical %t/lnx.o | %{canon} --check-prefixes=CANON-X64
; RUN: llvm-cil-dump --canonical %t/win.o | %{canon} --check-prefixes=CANON-X64
; RUN: llvm-cil-dump --canonical %t/win.o0.o | %{canon} --check-prefixes=CANON-X64
; RUN: llvm-cil-dump --canonical %t/a64.o | %{canon} --check-prefixes=CANON-A64
; CANON-X64:pe PE32+ machine=0x8664 dll subsystem=3
; CANON-A64:pe PE32+ machine=0xAA64 dll subsystem=3
; CANON-NEXT:cor flags=0x00000001 runtime=2.5 metadata-version=v4.0.30319
; CANON-NEXT:module 'a.c'
; CANON-NEXT:assemblyref 'msil.bcl' ver 0.0.0.0 flags=0x00000000 key=- culture=''
; CANON-NEXT:assemblyref 'msil.extern' ver 0.0.0.0 flags=0x00000000 key=- culture=''
; CANON-NEXT:assemblyref 'msil.link' ver 0.0.0.0 flags=0x00000000 key=- culture=''
; CANON-EMPTY:
; CANON-NEXT:.class '<Module>' flags=0x00000000
; CANON-NEXT:  .field 'counter' native int flags=0x0036
; CANON-NEXT:  .field 'hidden' native int flags=0x0033
; CANON-NEXT:  .method 'init' void() flags=0x0013 impl=0x0000
; CANON:  .method 'main' int32() flags=0x0016 impl=0x0000
; CANON:module .custom instance void(uint8[]) [msil.link]<msil>LinkInfo::'.ctor' = 01 00 {{(([0-9A-F]{2} ){4})}}4D 53 4C 4B 01 00 {{([0-9A-F]{2} )+}}00 00
; CANON-NOT:{{.}}

; The record of this module (5.8.8): the header with the target machine's
; triple and data model, four symbols, a segment per variable and the INIT
; entry.
; DEFINE: %{rec} = FileCheck %s --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/lnx.o | %{rec} --check-prefixes=REC,REC-LNX
; RUN: llvm-cil-dump --link-info %t/win.o | %{rec} --check-prefixes=REC,REC-WIN
; RUN: llvm-cil-dump --link-info %t/win.o0.o | %{rec} --check-prefixes=REC,REC-WIN
; RUN: llvm-cil-dump --link-info %t/a64.o | %{rec} --check-prefixes=REC,REC-A64
; RUN: llvm-cil-dump --link-info %t/a64win.o | %{rec} --check-prefixes=REC,REC-A64-WIN
; REC-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; REC-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; REC-A64:LinkInfo v1.0 triple=aarch64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; REC-A64-WIN:LinkInfo v1.0 triple=aarch64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; REC-NEXT:Symbol 0 DATA GLOBAL DEFAULT 'counter' field=0x04000001 segment=0 offset=0 size=4
; REC-NEXT:Symbol 1 DATA LOCAL DEFAULT 'hidden' field=0x04000002 segment=1 offset=0 size=4
; REC-NEXT:Symbol 2 FUNCTION LOCAL DEFAULT 'init' method=0x06000001
; REC-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT 'main' method=0x06000002
; REC-NEXT:Segment 0 '.data' align=4 size=4 flags=0
; REC-NEXT:  05 00 00 00
; REC-NEXT:Segment 1 '.data' align=4 size=4 flags=0
; REC-NEXT:  07 00 00 00
; REC-NEXT:Init priority=65535 symbol=2 'init'
; REC-NOT:{{.}}

; O3 and O4: the same IR compiled in two directories, from two input paths
; and into two output names, gives the same bytes, and the module is still
; 'a.c'. A module with an empty source_filename is '<msil>module'.
; RUN: cp %t/a.ll %t/d2/copy.ll
; RUN: cd %t/d1 && llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs ../a.ll -o first.o
; RUN: cd %t/d2 && llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs copy.ll -o %t/d2/second.o
; RUN: cmp %t/d1/first.o %t/d2/second.o
; RUN: cmp %t/win.o %t/d2/second.o
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/empty-name.ll \
; RUN:   -o %t/d1/empty.o 2>&1 | count 0
; RUN: llvm-cil-dump --canonical %t/d1/empty.o | FileCheck %s --check-prefix=NONAME --match-full-lines
; NONAME:module '<msil>module'

; O4 within one process: -compile-twice must reproduce the single-run object
; of the same triple and options (lnx.o and win.o above) byte for byte.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -compile-twice -verify-machineinstrs < %t/a.ll \
; RUN:   -o %t/twice.lnx.o 2>&1 | count 0
; RUN: cmp %t/lnx.o %t/twice.lnx.o
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -compile-twice -verify-machineinstrs < %t/a.ll \
; RUN:   -o %t/twice.win.o 2>&1 | count 0
; RUN: cmp %t/win.o %t/twice.win.o

; -filetype=null stays whole-program and prints nothing (from obj-unsupported.ll).
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=null -verify-machineinstrs < %t/a.ll 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=null -verify-machineinstrs < %t/a.ll 2>&1 \
; RUN:   | count 0

;--- a.ll
source_filename = "dir/sub/a.c"

@counter = global i32 5, align 4
@hidden = internal global i32 7, align 4
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @init, ptr null }]

define internal void @init() {
  store i32 6, ptr @counter, align 4
  ret void
}

define i32 @main() {
  %v = load i32, ptr @counter, align 4
  %h = load i32, ptr @hidden, align 4
  %s = add i32 %v, %h
  ret i32 %s
}

;--- empty-name.ll
source_filename = ""

define i32 @f(i32 %x) {
  ret i32 %x
}
