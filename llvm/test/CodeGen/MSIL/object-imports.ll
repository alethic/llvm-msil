; Import-attributed declarations in a CIL object (ImplementationPlan.md 5.8.3
; rows and R7, 5.8.8 IMPORT and SIGNATURES, 5.8.10, 5.9.17's M19 list,
; 5.10.7; M19). The C spelling is __attribute__((import_module("m"),
; import_name("e"))), which clang turns into the IR attributes
; "msil-import-module" and "msil-import-name" on declarations (5.10.7); D14
; does not apply here.
; - A function with "msil-import-module" (tick: module and name; modonly:
;   module only, so the entry is the C name) gets one stub per IL signature
;   its calls use, 'public hidebysig static pinvokeimpl("<module>" as
;   "<entry>" nomangle cdecl) ... preservesig' with no body: MethodDef flags
;   0x2096 (Public|Static|HideBySig|PinvokeImpl), ImplFlags 0x0080
;   (PreserveSig), an ImplMap row with MappingFlags 0x0201 (NoMangle,
;   CallConvCdecl) on a ModuleRef that names the module verbatim; the symbol
;   is FUNCTION UNDEFINED EXPLICIT_IMPORT, and each call names the stub of
;   its own signature (R1, R7).
; - A function with "msil-import-name" only (my_labs, entry 'labs'), called
;   with two IL signatures: two stubs 'pinvokeimpl("msil.unresolved" as
;   "labs" nomangle cdecl)', ModuleRef 'msil.unresolved', no EXPLICIT_IMPORT.
; - An undefined, prototyped function whose address the object takes but
;   never calls gets one address-only stub with the declaration's IL
;   signature: on 'msil.unresolved' with the C name (addr_only) or the import
;   name (addr_named), and on its module with "msil-import-module"
;   (addr_mod, EXPLICIT_IMPORT); its address is the FnAddr placeholder. An
;   unprototyped declaration (noproto_addr) gets no stub.
; - External variables with the attributes are DATA UNDEFINED IMPORT with
;   ImportModule (empty without "msil-import-module") and ImportName (the
;   import name, else the C name); their uses are '<msil>Symbols' field
;   placeholders, and they get no row of their own.
; - LINKER_OPTIONS: one DEPENDENT_LIB entry per !llvm.dependent-libraries
;   string and one LINKER_OPTION entry per !llvm.linker.options node, each
;   option its own string (a library name with a blank stays one argument;
;   section 12, M15's #pragma comment(lib) row). 5.8.8 fixes no order
;   between the two kinds, so they are matched in any order and counted
;   (LOPTS), and nothing may stand between the symbols and the first of them
;   (no Segment, Reloc, Comdat, Init or Signature record: every function
;   here is prototyped and passes no aggregate).
; - stub-sigs.ll: SIGNATURES entries on stub MethodDefs, which 5.8.8 derives
;   from the IR calls (R3, R7) and, for an address-only stub, from the
;   declaration it is made from (a MethodDef without an entry has no sret or
;   byval parameter): a K&R ("no-prototype") declaration with
;   "msil-import-module" called with two arities gets two stubs, each with
;   NO_PROTOTYPE; an sret import gets SRET size=24 align=8 (the returned
;   aggregate, IL parameter 0); a byval import with "msil-import-name" only
;   gets byval=0:24:8; and the address-only stubs of an sret and a byval
;   declaration get the same entries. Each entry's token is its stub's
;   MethodDef row (STUB-TBL); 5.8.8 does not fix the order of the entries,
;   so they are matched in any order and counted (STUB-SIGS).
; Event order (5.3 as amended for M19; section 12, 2026-10-21; M19.b
; Provides): definitions in IR order, then the stubs on a module and on
; msil.unresolved in first-use order, then the address-only stubs in SYMTAB
; order; ModuleRefs follow their first stub. order.ll pins it with an input
; whose first-use order, SYMTAB order and grouping by module all differ: use
; calls mod_c (module libc2), then unres_b ("msil-import-name" only), then
; mod_a (module liba), against their declaration order, and ay takes &addr_y
; before ax takes &addr_x (plain prototyped declarations, never called). The
; stub MethodDefs after the three definitions are mod_c, unres_b, mod_a (the
; call stubs in first-use order, module and msil.unresolved stubs mixed),
; then addr_x, addr_y (the address-only stubs in SYMTAB order, not in the
; order their addresses are taken); the ModuleRefs are libc2,
; msil.unresolved, liba; the ImplMap rows follow the stubs; and the
; object-form text declares the stubs in the same order (ORD-TEXT).
; r3-stub.ll: two calls of an import-attributed declaration with one IL
; signature but different byval attributes share its one stub (R7: one stub
; per (name, call IL signature)), warn as R3 says ('calls to 'imp' pass
; incompatible arguments; the object records the first call's layout') and
; record the first call's layout, byval=0:24:8, under the stub's MethodDef.
; Both x86_64 triples at -O2, windows-msvc also at -O0.

; REQUIRES: llvm-cil-dump
; RUN: rm -rf %t && split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/imports.ll -o %t/lnx.o 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/imports.ll -o %t/win.o 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/imports.ll -o %t/win.o0.o 2>&1 \
; RUN:   | count 0
; RUN: llvm-cil-dump --link-info %t/lnx.o \
; RUN:   | FileCheck %s --check-prefixes=CHECK,LNX --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/win.o \
; RUN:   | FileCheck %s --check-prefixes=CHECK,WIN --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/win.o0.o \
; RUN:   | FileCheck %s --check-prefixes=CHECK,WIN --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/lnx.o | FileCheck %s --check-prefix=LOPTS --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/win.o | FileCheck %s --check-prefix=LOPTS --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/win.o0.o | FileCheck %s --check-prefix=LOPTS --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --tables %t/lnx.o | FileCheck %s --check-prefix=TBL --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --tables %t/win.o0.o | FileCheck %s --check-prefix=TBL --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --canonical %t/win.o | FileCheck %s --check-prefix=CANON --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --canonical %t/lnx.o | FileCheck %s --check-prefix=CANON --match-full-lines --strict-whitespace

; The object oracle (5.12.6, as in object-form-text.ll): ilasm's object of
; the object-form text is canonically equal to llc's object, record and
; tokens included.
; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{o} =
; DEFINE: %{oracle} = %ilasm-verify -output=%{o}.ilasm.o %{o}.il && \
; DEFINE:   %{canon} %{o}.o > %{o}.canon1 && %{canon} %{o}.ilasm.o > %{o}.canon2 && diff %{o}.canon1 %{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %{o}.o > %{o}.link1 && llvm-cil-dump --link-info %{o}.ilasm.o > %{o}.link2 && \
; DEFINE:   diff %{o}.link1 %{o}.link2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form -verify-machineinstrs < %t/imports.ll -o %t/win.il 2>&1 | count 0
; REDEFINE: %{o} = %t/win
; RUN: %if ilasm %{ %{oracle} %}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/imports.ll -o %t/lnx.il 2>&1 | count 0
; REDEFINE: %{o} = %t/lnx
; RUN: %if ilasm %{ %{oracle} %}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-object-form -verify-machineinstrs < %t/imports.ll -o %t/win.o0.il 2>&1 | count 0
; REDEFINE: %{o} = %t/win.o0
; RUN: %if ilasm %{ %{oracle} %}

; The same stubs in object-form text (5.8.4), in event order.
; RUN: FileCheck %s --check-prefix=TEXT --input-file=%t/win.il

; SIGNATURES on stub MethodDefs (stub-sigs.ll).
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/stub-sigs.ll \
; RUN:   -o %t/sigs.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/stub-sigs.ll \
; RUN:   -o %t/sigs.win.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/stub-sigs.ll \
; RUN:   -o %t/sigs.win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/sigs.lnx.o \
; RUN:   | FileCheck %s --check-prefixes=STUB,STUB-LNX --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/sigs.win.o \
; RUN:   | FileCheck %s --check-prefixes=STUB,STUB-WIN --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/sigs.win.o0.o \
; RUN:   | FileCheck %s --check-prefixes=STUB,STUB-WIN --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/sigs.lnx.o | FileCheck %s --check-prefix=STUB-SIGS --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/sigs.win.o | FileCheck %s --check-prefix=STUB-SIGS --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/sigs.win.o0.o | FileCheck %s --check-prefix=STUB-SIGS --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --tables %t/sigs.lnx.o | FileCheck %s --check-prefix=STUB-TBL --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --tables %t/sigs.win.o0.o | FileCheck %s --check-prefix=STUB-TBL --match-full-lines --strict-whitespace
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/stub-sigs.ll -o %t/sigs.lnx.il 2>&1 | count 0
; REDEFINE: %{o} = %t/sigs.lnx
; RUN: %if ilasm %{ %{oracle} %}

; The event order of the stubs (order.ll).
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/order.ll \
; RUN:   -o %t/order.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/order.ll \
; RUN:   -o %t/order.win.o 2>&1 | count 0
; RUN: llvm-cil-dump --tables %t/order.lnx.o | FileCheck %s --check-prefix=ORD-TBL --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --tables %t/order.win.o | FileCheck %s --check-prefix=ORD-TBL --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/order.lnx.o \
; RUN:   | FileCheck %s --check-prefixes=ORD,ORD-LNX --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/order.win.o \
; RUN:   | FileCheck %s --check-prefixes=ORD,ORD-WIN --match-full-lines --strict-whitespace
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/order.ll \
; RUN:   -o %t/order.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=ORD-TEXT --input-file=%t/order.lnx.il
; REDEFINE: %{o} = %t/order.lnx
; RUN: %if ilasm %{ %{oracle} %}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form -verify-machineinstrs < %t/order.ll \
; RUN:   -o %t/order.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=ORD-TEXT --input-file=%t/order.win.il
; REDEFINE: %{o} = %t/order.win
; RUN: %if ilasm %{ %{oracle} %}

; R3 on an import stub (r3-stub.ll).
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/r3-stub.ll \
; RUN:   -o %t/r3.lnx.o 2>&1 | FileCheck %s --check-prefix=R3-WARN --implicit-check-not=warning: --implicit-check-not=error:
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/r3-stub.ll \
; RUN:   -o %t/r3.win.o 2>&1 | FileCheck %s --check-prefix=R3-WARN --implicit-check-not=warning: --implicit-check-not=error:
; RUN: llvm-cil-dump --link-info %t/r3.lnx.o \
; RUN:   | FileCheck %s --check-prefixes=R3,R3-LNX --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/r3.win.o \
; RUN:   | FileCheck %s --check-prefixes=R3,R3-WIN --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --tables %t/r3.lnx.o | FileCheck %s --check-prefix=R3-TBL --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --tables %t/r3.win.o | FileCheck %s --check-prefix=R3-TBL --match-full-lines --strict-whitespace

; LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; CHECK-NEXT:Symbol 0 DATA GLOBAL DEFAULT UNDEFINED IMPORT 'my_counter' import_module='' import_name='native_counter'
; CHECK-NEXT:Symbol 1 DATA GLOBAL DEFAULT UNDEFINED IMPORT 'mod_data' import_module='libfoo.so' import_name='foo_data'
; CHECK-NEXT:Symbol 2 DATA GLOBAL DEFAULT UNDEFINED IMPORT 'bar_data' import_module='libbar' import_name='bar_data'
; CHECK-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED EXPLICIT_IMPORT 'tick'
; CHECK-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT UNDEFINED EXPLICIT_IMPORT 'modonly'
; CHECK-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT UNDEFINED 'my_labs'
; CHECK-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT UNDEFINED 'addr_only'
; CHECK-NEXT:Symbol 7 FUNCTION GLOBAL DEFAULT UNDEFINED 'addr_named'
; CHECK-NEXT:Symbol 8 FUNCTION GLOBAL DEFAULT UNDEFINED EXPLICIT_IMPORT 'addr_mod'
; CHECK-NEXT:Symbol 9 FUNCTION GLOBAL DEFAULT UNDEFINED 'noproto_addr'
; CHECK-NEXT:Symbol 10 FUNCTION GLOBAL DEFAULT 'use' method=0x06000001
; CHECK-NEXT:Symbol 11 FUNCTION GLOBAL DEFAULT 'a1' method=0x06000002
; CHECK-NEXT:Symbol 12 FUNCTION GLOBAL DEFAULT 'a2' method=0x06000003
; CHECK-NEXT:Symbol 13 FUNCTION GLOBAL DEFAULT 'a3' method=0x06000004
; CHECK-NEXT:Symbol 14 FUNCTION GLOBAL DEFAULT 'a4' method=0x06000005
; CHECK-NOT:{{.}}
; CHECK-DAG:LinkerOption DEPENDENT_LIB 'm'
; CHECK-DAG:LinkerOption LINKER_OPTION '/DEFAULTLIB:ws2_32.lib'
; CHECK-DAG:LinkerOption LINKER_OPTION '-lmy lib' '-lfoo'
; CHECK-NOT:{{.}}
; LOPTS-COUNT-3:{{^LinkerOption .*}}
; LOPTS-NOT:{{^LinkerOption }}

; The stubs follow the five definitions (MethodDefs 6-12); the ImplMap rows
; are sorted by the stub they forward; the ModuleRefs are kernel32.dll,
; mylib, msil.unresolved and libaddr, in first-stub order.
; TBL:Table: MethodDef rows=12 rowsize={{[0-9]+}}
; TBL-NEXT:  MethodDef[1]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='use' Signature=00 00 08 ParamList={{.*}}
; TBL-NEXT:  MethodDef[2]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='a1' Signature=00 00 18 ParamList={{.*}}
; TBL-NEXT:  MethodDef[3]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='a2' Signature=00 00 18 ParamList={{.*}}
; TBL-NEXT:  MethodDef[4]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='a3' Signature=00 00 18 ParamList={{.*}}
; TBL-NEXT:  MethodDef[5]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='a4' Signature=00 00 18 ParamList={{.*}}
; TBL-NEXT:  MethodDef[6]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='tick' Signature=00 00 08 ParamList={{.*}}
; TBL-NEXT:  MethodDef[7]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='modonly' Signature=00 01 08 08 ParamList={{.*}}
; TBL-NEXT:  MethodDef[8]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='my_labs' Signature=00 01 08 08 ParamList={{.*}}
; TBL-NEXT:  MethodDef[9]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='my_labs' Signature=00 01 18 18 ParamList={{.*}}
; TBL-NEXT:  MethodDef[10]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='addr_only' Signature=00 01 08 08 ParamList={{.*}}
; TBL-NEXT:  MethodDef[11]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='addr_named' Signature=00 01 08 08 ParamList={{.*}}
; TBL-NEXT:  MethodDef[12]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='addr_mod' Signature=00 01 08 08 ParamList={{.*}}
; TBL:Table: ModuleRef rows=4 rowsize={{[0-9]+}}
; TBL-NEXT:  ModuleRef[1]: Name='kernel32.dll'
; TBL-NEXT:  ModuleRef[2]: Name='mylib'
; TBL-NEXT:  ModuleRef[3]: Name='msil.unresolved'
; TBL-NEXT:  ModuleRef[4]: Name='libaddr'
; TBL-NEXT:Table: ImplMap rows=7 rowsize={{[0-9]+}}
; TBL-NEXT:  ImplMap[1]: MappingFlags=0x0201 MemberForwarded=0x06000006 ImportName='GetTickCount' ImportScope=0x1A000001
; TBL-NEXT:  ImplMap[2]: MappingFlags=0x0201 MemberForwarded=0x06000007 ImportName='modonly' ImportScope=0x1A000002
; TBL-NEXT:  ImplMap[3]: MappingFlags=0x0201 MemberForwarded=0x06000008 ImportName='labs' ImportScope=0x1A000003
; TBL-NEXT:  ImplMap[4]: MappingFlags=0x0201 MemberForwarded=0x06000009 ImportName='labs' ImportScope=0x1A000003
; TBL-NEXT:  ImplMap[5]: MappingFlags=0x0201 MemberForwarded=0x0600000A ImportName='addr_only' ImportScope=0x1A000003
; TBL-NEXT:  ImplMap[6]: MappingFlags=0x0201 MemberForwarded=0x0600000B ImportName='named_entry' ImportScope=0x1A000003
; TBL-NEXT:  ImplMap[7]: MappingFlags=0x0201 MemberForwarded=0x0600000C ImportName='addr_mod' ImportScope=0x1A000004

; CANON:module 'imports.c'
; CANON-NEXT:assemblyref 'msil.bcl' ver 0.0.0.0 flags=0x00000000 key=- culture=''
; CANON-NEXT:assemblyref 'msil.extern' ver 0.0.0.0 flags=0x00000000 key=- culture=''
; CANON-NEXT:assemblyref 'msil.link' ver 0.0.0.0 flags=0x00000000 key=- culture=''
; CANON-NEXT:moduleref 'kernel32.dll'
; CANON-NEXT:moduleref 'libaddr'
; CANON-NEXT:moduleref 'msil.unresolved'
; CANON-NEXT:moduleref 'mylib'
; CANON-EMPTY:
; CANON-NEXT:.class '<Module>' flags=0x00000000
; CANON-NEXT:  .method 'a1' native int() flags=0x0016 impl=0x0000
; CANON-NEXT:    .maxstack 1
; CANON-NEXT:    L0: ldsfld native int [msil.extern]<msil>FnAddr::'addr_only'
; CANON-NEXT:    L1: ret
; CANON-NEXT:  .method 'a2' native int() flags=0x0016 impl=0x0000
; CANON-NEXT:    .maxstack 1
; CANON-NEXT:    L0: ldsfld native int [msil.extern]<msil>FnAddr::'addr_named'
; CANON-NEXT:    L1: ret
; CANON-NEXT:  .method 'a3' native int() flags=0x0016 impl=0x0000
; CANON-NEXT:    .maxstack 1
; CANON-NEXT:    L0: ldsfld native int [msil.extern]<msil>FnAddr::'addr_mod'
; CANON-NEXT:    L1: ret
; CANON-NEXT:  .method 'a4' native int() flags=0x0016 impl=0x0000
; CANON-NEXT:    .maxstack 1
; CANON-NEXT:    L0: ldsfld native int [msil.extern]<msil>FnAddr::'noproto_addr'
; CANON-NEXT:    L1: ret
; CANON-NEXT:  .method 'addr_mod' int32(int32) flags=0x2096 impl=0x0080
; CANON-NEXT:    .pinvokeimpl 'libaddr' entry 'addr_mod' flags=0x0201
; CANON-NEXT:  .method 'addr_named' int32(int32) flags=0x2096 impl=0x0080
; CANON-NEXT:    .pinvokeimpl 'msil.unresolved' entry 'named_entry' flags=0x0201
; CANON-NEXT:  .method 'addr_only' int32(int32) flags=0x2096 impl=0x0080
; CANON-NEXT:    .pinvokeimpl 'msil.unresolved' entry 'addr_only' flags=0x0201
; CANON-NEXT:  .method 'modonly' int32(int32) flags=0x2096 impl=0x0080
; CANON-NEXT:    .pinvokeimpl 'mylib' entry 'modonly' flags=0x0201
; CANON-NEXT:  .method 'my_labs' int32(int32) flags=0x2096 impl=0x0080
; CANON-NEXT:    .pinvokeimpl 'msil.unresolved' entry 'labs' flags=0x0201
; CANON-NEXT:  .method 'my_labs' native int(native int) flags=0x2096 impl=0x0080
; CANON-NEXT:    .pinvokeimpl 'msil.unresolved' entry 'labs' flags=0x0201
; CANON-NEXT:  .method 'tick' int32() flags=0x2096 impl=0x0080
; CANON-NEXT:    .pinvokeimpl 'kernel32.dll' entry 'GetTickCount' flags=0x0201
; CANON-NEXT:  .method 'use' int32() flags=0x0016 impl=0x0000
; CANON-NOT:  .method
; CANON:    L{{[0-9]+}}: call int32() '<Module>'::'tick'
; CANON-NOT:  .method
; CANON:    L{{[0-9]+}}: call int32(int32) '<Module>'::'modonly'
; CANON-NOT:  .method
; CANON:    L{{[0-9]+}}: call int32(int32) '<Module>'::'my_labs'
; CANON-NOT:  .method
; CANON:    L{{[0-9]+}}: call native int(native int) '<Module>'::'my_labs'
; CANON-NOT:  .method
; CANON:    L{{[0-9]+}}: ldsfld native int [msil.extern]<msil>Symbols::'my_counter'
; CANON-NOT:  .method
; CANON:    L{{[0-9]+}}: ldsfld native int [msil.extern]<msil>Symbols::'mod_data'
; CANON-NOT:  .method
; CANON:    L{{[0-9]+}}: ldsfld native int [msil.extern]<msil>Symbols::'bar_data'
; CANON-NOT:  .method
; CANON:module .custom {{.*}}

; TEXT:      {{^}}.method public static native int 'a4'() cil managed{{$}}
; TEXT:      {{^}}.method public hidebysig static pinvokeimpl("kernel32.dll" as "GetTickCount" nomangle cdecl) int32 'tick'() cil managed preservesig {}{{$}}
; TEXT:      {{^}}.method public hidebysig static pinvokeimpl("mylib" as "modonly" nomangle cdecl) int32 'modonly'(int32) cil managed preservesig {}{{$}}
; TEXT:      {{^}}.method public hidebysig static pinvokeimpl("msil.unresolved" as "labs" nomangle cdecl) int32 'my_labs'(int32) cil managed preservesig {}{{$}}
; TEXT:      {{^}}.method public hidebysig static pinvokeimpl("msil.unresolved" as "labs" nomangle cdecl) native int 'my_labs'(native int) cil managed preservesig {}{{$}}
; TEXT:      {{^}}.method public hidebysig static pinvokeimpl("msil.unresolved" as "addr_only" nomangle cdecl) int32 'addr_only'(int32) cil managed preservesig {}{{$}}
; TEXT:      {{^}}.method public hidebysig static pinvokeimpl("msil.unresolved" as "named_entry" nomangle cdecl) int32 'addr_named'(int32) cil managed preservesig {}{{$}}
; TEXT:      {{^}}.method public hidebysig static pinvokeimpl("libaddr" as "addr_mod" nomangle cdecl) int32 'addr_mod'(int32) cil managed preservesig {}{{$}}
; TEXT-NOT:  noproto_addr'(
; TEXT:      {{^}}.module 'imports.c'{{$}}

; STUB-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; STUB-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; STUB-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT UNDEFINED EXPLICIT_IMPORT 'np_imp'
; STUB-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT UNDEFINED EXPLICIT_IMPORT 'sret_imp'
; STUB-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT UNDEFINED 'byval_named'
; STUB-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED EXPLICIT_IMPORT 'sret_addr'
; STUB-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT UNDEFINED 'byval_addr'
; STUB-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT 'use_sigs' method=0x06000001
; STUB-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT 'addr_sret' method=0x06000002
; STUB-NEXT:Symbol 7 FUNCTION GLOBAL DEFAULT 'addr_byval' method=0x06000003
; STUB-NOT:{{.}}
; STUB-DAG:Signature 0x06000004 'np_imp' NO_PROTOTYPE
; STUB-DAG:Signature 0x06000005 'np_imp' NO_PROTOTYPE
; STUB-DAG:Signature 0x06000006 'sret_imp' SRET size=24 align=8
; STUB-DAG:Signature 0x06000007 'byval_named' byval=0:24:8
; STUB-DAG:Signature 0x06000008 'sret_addr' SRET size=24 align=8
; STUB-DAG:Signature 0x06000009 'byval_addr' byval=0:24:8
; STUB-NOT:{{.}}
; STUB-SIGS-COUNT-6:{{^Signature .*}}
; STUB-SIGS-NOT:{{^Signature }}

; The stub MethodDefs 4-9 that the entries name: the K&R stubs with their
; two call signatures in first-use order, then the sret and byval stubs,
; then the address-only stubs with their declarations' signatures; each has
; its ImplMap row.
; STUB-TBL:Table: MethodDef rows=9 rowsize={{[0-9]+}}
; STUB-TBL-NEXT:  MethodDef[1]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='use_sigs' Signature=00 00 08 ParamList={{.*}}
; STUB-TBL-NEXT:  MethodDef[2]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='addr_sret' Signature=00 00 18 ParamList={{.*}}
; STUB-TBL-NEXT:  MethodDef[3]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='addr_byval' Signature=00 00 18 ParamList={{.*}}
; STUB-TBL-NEXT:  MethodDef[4]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='np_imp' Signature=00 01 08 08 ParamList={{.*}}
; STUB-TBL-NEXT:  MethodDef[5]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='np_imp' Signature=00 02 08 08 08 ParamList={{.*}}
; STUB-TBL-NEXT:  MethodDef[6]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='sret_imp' Signature=00 02 01 18 08 ParamList={{.*}}
; STUB-TBL-NEXT:  MethodDef[7]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='byval_named' Signature=00 01 08 18 ParamList={{.*}}
; STUB-TBL-NEXT:  MethodDef[8]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='sret_addr' Signature=00 02 01 18 08 ParamList={{.*}}
; STUB-TBL-NEXT:  MethodDef[9]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='byval_addr' Signature=00 01 08 18 ParamList={{.*}}
; STUB-TBL:Table: ModuleRef rows=4 rowsize={{[0-9]+}}
; STUB-TBL-NEXT:  ModuleRef[1]: Name='libnp'
; STUB-TBL-NEXT:  ModuleRef[2]: Name='libsret'
; STUB-TBL-NEXT:  ModuleRef[3]: Name='msil.unresolved'
; STUB-TBL-NEXT:  ModuleRef[4]: Name='libaddr'
; STUB-TBL-NEXT:Table: ImplMap rows=6 rowsize={{[0-9]+}}
; STUB-TBL-NEXT:  ImplMap[1]: MappingFlags=0x0201 MemberForwarded=0x06000004 ImportName='np_imp' ImportScope=0x1A000001
; STUB-TBL-NEXT:  ImplMap[2]: MappingFlags=0x0201 MemberForwarded=0x06000005 ImportName='np_imp' ImportScope=0x1A000001
; STUB-TBL-NEXT:  ImplMap[3]: MappingFlags=0x0201 MemberForwarded=0x06000006 ImportName='sret_imp' ImportScope=0x1A000002
; STUB-TBL-NEXT:  ImplMap[4]: MappingFlags=0x0201 MemberForwarded=0x06000007 ImportName='byval_entry' ImportScope=0x1A000003
; STUB-TBL-NEXT:  ImplMap[5]: MappingFlags=0x0201 MemberForwarded=0x06000008 ImportName='sret_addr' ImportScope=0x1A000004
; STUB-TBL-NEXT:  ImplMap[6]: MappingFlags=0x0201 MemberForwarded=0x06000009 ImportName='byval_addr' ImportScope=0x1A000003

; order.ll: the three definitions, then the call stubs in first-use order
; (mod_c, unres_b, mod_a), then the address-only stubs in SYMTAB order
; (addr_x, addr_y); the ModuleRefs in first-stub order; the ImplMap rows in
; stub order with their scopes.
; ORD-TBL:Table: MethodDef rows=8 rowsize={{[0-9]+}}
; ORD-TBL-NEXT:  MethodDef[1]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='use' Signature=00 00 08 ParamList={{.*}}
; ORD-TBL-NEXT:  MethodDef[2]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='ay' Signature=00 00 18 ParamList={{.*}}
; ORD-TBL-NEXT:  MethodDef[3]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='ax' Signature=00 00 18 ParamList={{.*}}
; ORD-TBL-NEXT:  MethodDef[4]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='mod_c' Signature=00 01 08 08 ParamList={{.*}}
; ORD-TBL-NEXT:  MethodDef[5]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='unres_b' Signature=00 01 08 08 ParamList={{.*}}
; ORD-TBL-NEXT:  MethodDef[6]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='mod_a' Signature=00 01 08 08 ParamList={{.*}}
; ORD-TBL-NEXT:  MethodDef[7]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='addr_x' Signature=00 01 08 08 ParamList={{.*}}
; ORD-TBL-NEXT:  MethodDef[8]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='addr_y' Signature=00 01 08 08 ParamList={{.*}}
; ORD-TBL:Table: ModuleRef rows=3 rowsize={{[0-9]+}}
; ORD-TBL-NEXT:  ModuleRef[1]: Name='libc2'
; ORD-TBL-NEXT:  ModuleRef[2]: Name='msil.unresolved'
; ORD-TBL-NEXT:  ModuleRef[3]: Name='liba'
; ORD-TBL-NEXT:Table: ImplMap rows=5 rowsize={{[0-9]+}}
; ORD-TBL-NEXT:  ImplMap[1]: MappingFlags=0x0201 MemberForwarded=0x06000004 ImportName='mod_c' ImportScope=0x1A000001
; ORD-TBL-NEXT:  ImplMap[2]: MappingFlags=0x0201 MemberForwarded=0x06000005 ImportName='bentry' ImportScope=0x1A000002
; ORD-TBL-NEXT:  ImplMap[3]: MappingFlags=0x0201 MemberForwarded=0x06000006 ImportName='mod_a' ImportScope=0x1A000003
; ORD-TBL-NEXT:  ImplMap[4]: MappingFlags=0x0201 MemberForwarded=0x06000007 ImportName='addr_x' ImportScope=0x1A000002
; ORD-TBL-NEXT:  ImplMap[5]: MappingFlags=0x0201 MemberForwarded=0x06000008 ImportName='addr_y' ImportScope=0x1A000002

; SYMTAB keeps IR module order (declarations first), unlike the stubs.
; ORD-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; ORD-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; ORD-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT UNDEFINED EXPLICIT_IMPORT 'mod_a'
; ORD-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT UNDEFINED 'unres_b'
; ORD-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT UNDEFINED EXPLICIT_IMPORT 'mod_c'
; ORD-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED 'addr_x'
; ORD-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT UNDEFINED 'addr_y'
; ORD-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT 'use' method=0x06000001
; ORD-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT 'ay' method=0x06000002
; ORD-NEXT:Symbol 7 FUNCTION GLOBAL DEFAULT 'ax' method=0x06000003
; ORD-NOT:{{.}}

; ORD-TEXT:      {{^}}.method public static native int 'ax'() cil managed{{$}}
; ORD-TEXT:      {{^}}.method public hidebysig static pinvokeimpl("libc2" as "mod_c" nomangle cdecl) int32 'mod_c'(int32) cil managed preservesig {}{{$}}
; ORD-TEXT-NEXT: {{^}}.method public hidebysig static pinvokeimpl("msil.unresolved" as "bentry" nomangle cdecl) int32 'unres_b'(int32) cil managed preservesig {}{{$}}
; ORD-TEXT-NEXT: {{^}}.method public hidebysig static pinvokeimpl("liba" as "mod_a" nomangle cdecl) int32 'mod_a'(int32) cil managed preservesig {}{{$}}
; ORD-TEXT-NEXT: {{^}}.method public hidebysig static pinvokeimpl("msil.unresolved" as "addr_x" nomangle cdecl) int32 'addr_x'(int32) cil managed preservesig {}{{$}}
; ORD-TEXT-NEXT: {{^}}.method public hidebysig static pinvokeimpl("msil.unresolved" as "addr_y" nomangle cdecl) int32 'addr_y'(int32) cil managed preservesig {}{{$}}
; ORD-TEXT:      {{^}}.module 'order.c'{{$}}

; r3-stub.ll: one stub for the two calls, the first call's layout under it.
; R3-WARN: {{^}}warning: {{.*}}calls to 'imp' pass incompatible arguments; the object records the first call's layout{{$}}
; R3-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; R3-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; R3-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT UNDEFINED EXPLICIT_IMPORT 'imp'
; R3-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT 'caller' method=0x06000001
; R3-NEXT:Signature 0x06000002 'imp' byval=0:24:8
; R3-NOT:{{.}}
; R3-TBL:Table: MethodDef rows=2 rowsize={{[0-9]+}}
; R3-TBL-NEXT:  MethodDef[1]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='caller' Signature=00 01 08 18 ParamList={{.*}}
; R3-TBL-NEXT:  MethodDef[2]: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='imp' Signature=00 01 08 18 ParamList={{.*}}
; R3-TBL:Table: ModuleRef rows=1 rowsize={{[0-9]+}}
; R3-TBL-NEXT:  ModuleRef[1]: Name='libx'
; R3-TBL-NEXT:Table: ImplMap rows=1 rowsize={{[0-9]+}}
; R3-TBL-NEXT:  ImplMap[1]: MappingFlags=0x0201 MemberForwarded=0x06000002 ImportName='imp' ImportScope=0x1A000001

;--- imports.ll
source_filename = "imports.c"

@my_counter = external global i32, align 4 #0
@mod_data = external global i32, align 4 #1
@bar_data = external global i32, align 4 #2

declare i32 @tick() #3
declare i32 @modonly(i32) #4
declare i32 @my_labs(i32) #5
declare i32 @addr_only(i32)
declare i32 @addr_named(i32) #6
declare i32 @addr_mod(i32) #7
declare i32 @noproto_addr(...) #8

define i32 @use() {
  %a = call i32 @tick()
  %b = call i32 @modonly(i32 1)
  %c = call i32 @my_labs(i32 -5)
  %d64 = call i64 @my_labs(i64 -6)
  %d = trunc i64 %d64 to i32
  %e = load i32, ptr @my_counter, align 4
  %f = load i32, ptr @mod_data, align 4
  %g = load i32, ptr @bar_data, align 4
  %ab = add i32 %a, %b
  %cd = add i32 %c, %d
  %ef = add i32 %e, %f
  %abcd = add i32 %ab, %cd
  %efg = add i32 %ef, %g
  %r = add i32 %abcd, %efg
  ret i32 %r
}

define ptr @a1() {
  ret ptr @addr_only
}

define ptr @a2() {
  ret ptr @addr_named
}

define ptr @a3() {
  ret ptr @addr_mod
}

define ptr @a4() {
  ret ptr @noproto_addr
}

attributes #0 = { "msil-import-name"="native_counter" }
attributes #1 = { "msil-import-module"="libfoo.so" "msil-import-name"="foo_data" }
attributes #2 = { "msil-import-module"="libbar" }
attributes #3 = { "msil-import-module"="kernel32.dll" "msil-import-name"="GetTickCount" }
attributes #4 = { "msil-import-module"="mylib" }
attributes #5 = { "msil-import-name"="labs" }
attributes #6 = { "msil-import-name"="named_entry" }
attributes #7 = { "msil-import-module"="libaddr" }
attributes #8 = { "no-prototype" }

!llvm.dependent-libraries = !{!0}
!llvm.linker.options = !{!1, !2}

!0 = !{!"m"}
!1 = !{!"/DEFAULTLIB:ws2_32.lib"}
!2 = !{!"-lmy lib", !"-lfoo"}

;--- stub-sigs.ll
source_filename = "import-sigs.c"

%struct.big = type { i64, i64, i64 }

declare i32 @np_imp(...) #0
declare void @sret_imp(ptr sret(%struct.big) align 8, i32) #1
declare i32 @byval_named(ptr byval(%struct.big) align 8) #2
declare void @sret_addr(ptr sret(%struct.big) align 8, i32) #3
declare i32 @byval_addr(ptr byval(%struct.big) align 8)

define i32 @use_sigs() {
  %tmp = alloca %struct.big, align 8
  %a = call i32 @np_imp(i32 1)
  %b = call i32 @np_imp(i32 1, i32 2)
  call void @sret_imp(ptr sret(%struct.big) align 8 %tmp, i32 3)
  %c = call i32 @byval_named(ptr byval(%struct.big) align 8 %tmp)
  %ab = add i32 %a, %b
  %r = add i32 %ab, %c
  ret i32 %r
}

define ptr @addr_sret() {
  ret ptr @sret_addr
}

define ptr @addr_byval() {
  ret ptr @byval_addr
}

attributes #0 = { "no-prototype" "msil-import-module"="libnp" }
attributes #1 = { "msil-import-module"="libsret" }
attributes #2 = { "msil-import-name"="byval_entry" }
attributes #3 = { "msil-import-module"="libaddr" }

;--- order.ll
source_filename = "order.c"

declare i32 @mod_a(i32) #0
declare i32 @unres_b(i32) #1
declare i32 @mod_c(i32) #2
declare i32 @addr_x(i32)
declare i32 @addr_y(i32)

define i32 @use() {
  %c = call i32 @mod_c(i32 3)
  %b = call i32 @unres_b(i32 2)
  %a = call i32 @mod_a(i32 1)
  %ab = add i32 %a, %b
  %r = add i32 %ab, %c
  ret i32 %r
}

define ptr @ay() {
  ret ptr @addr_y
}

define ptr @ax() {
  ret ptr @addr_x
}

attributes #0 = { "msil-import-module"="liba" }
attributes #1 = { "msil-import-name"="bentry" }
attributes #2 = { "msil-import-module"="libc2" }

;--- r3-stub.ll
source_filename = "r3-stub.c"

%struct.big = type { i64, i64, i64 }

declare i32 @imp(ptr) #0

define i32 @caller(ptr %p) {
  %tmp = alloca %struct.big, align 8
  %c = call i32 @imp(ptr byval(%struct.big) align 8 %tmp)
  %d = call i32 @imp(ptr %p)
  %r = add i32 %c, %d
  ret i32 %r
}

attributes #0 = { "msil-import-module"="libx" }
