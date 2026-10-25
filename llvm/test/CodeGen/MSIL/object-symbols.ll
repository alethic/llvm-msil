; SYMTAB of a CIL object (ImplementationPlan.md 5.8.8 and 5.8.10; M19): one
; symbol per global, in this order: global variables in IR module order
; (definitions, commons and referenced declarations), then functions in IR
; module order (definitions and referenced declarations); unreferenced
; declarations and intrinsics are left out. Rows of 5.8.10:
; - external / internal and private definitions: GLOBAL / LOCAL;
; - weak, weak_odr, linkonce and linkonce_odr definitions: WEAK (no comdat
;   here, so no COMDATS entry);
; - common: COMMON with its size and alignment, a FieldDef and no segment;
; - available_externally: not emitted; its references make it UNDEFINED;
; - a declaration / an extern_weak declaration: UNDEFINED GLOBAL /
;   UNDEFINED WEAK, for variables (no D26 error in object mode) and functions;
; - hidden and protected visibility: VISIBILITY, on definitions and on a
;   declaration; dllexport / dllimport (dll.ll, windows-msvc): DLLEXPORT /
;   DLLIMPORT, the latter only on undefined symbols;
; - llvm.used: NO_STRIP, and RETAIN on the variable's segment;
;   llvm.compiler.used: nothing.
; Tokens: MethodDefs in emission order (definitions in IR order) and Fields
; in SYMTAB order of the defined variables, commons included (R6); one
; '.data' segment per defined non-common variable, in SYMTAB order.
; Calls and loads of local, WEAK and linkonce (COMDAT-able) definitions
; name the object's own rows (R1): use_all calls f_int, f_priv, f_linkonce
; and f_linkonce_odr through their MethodDefs on '<Module>' and loads g_int,
; g_priv, g_linkonce and g_linkonce_odr through their FieldDefs (R1 CANON
; lines), and the MemberRef table holds only the '<msil>Symbols'
; placeholders of the undefined symbols and the carrier's .ctor (ROWS), so
; no placeholder names a definition. object-comdat.ll checks the same for a
; call of a COMDAT member.
;
; The header's DataModel comes from MSILLink::setDataModel (5.8.9): sizeof
; void*, long, wchar_t and long double are 8,4,2,8 on Windows triples (msvc
; and MinGW, x86_64 and aarch64) and 8,8,4,8 on Linux triples without a
; 'wchar_size' module flag (clang omits it when it equals the triple's
; default), and the flag overrides wchar_t: 'wchar_size' 2 on
; x86_64-unknown-linux-gnu-msil gives 8,8,2,8. The triple is the target
; machine's normalized triple.

; REQUIRES: llvm-cil-dump
; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{rec} = FileCheck %s --match-full-lines --strict-whitespace

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/sym.ll \
; RUN:   -o %t/sym.lnx.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/sym.lnx.o | %{rec} --check-prefixes=CHECK,LNX
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/sym.ll \
; RUN:   -o %t/sym.win.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/sym.win.o | %{rec} --check-prefixes=CHECK,WIN
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -O0 -verify-machineinstrs < %t/sym.ll \
; RUN:   -o %t/sym.lnx.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/sym.lnx.o0.o | %{rec} --check-prefixes=CHECK,LNX

; The rows behind the symbols: the Fields and MethodDefs with R4's access
; (public for non-local linkage, assembly for internal and private), and no
; row at all for the undefined symbols but their placeholder MemberRefs.
; RUN: llvm-cil-dump --tables %t/sym.win.o | FileCheck %s --check-prefix=ROWS --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --tables %t/sym.lnx.o0.o | FileCheck %s --check-prefix=ROWS --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --canonical %t/sym.win.o | FileCheck %s --check-prefix=R1 --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --canonical %t/sym.lnx.o | FileCheck %s --check-prefix=R1 --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --canonical %t/sym.lnx.o0.o | FileCheck %s --check-prefix=R1 --match-full-lines --strict-whitespace

; The object oracle (5.12.6, as in object-form-text.ll): ilasm's object of
; the object-form text is canonically equal to llc's object, record and
; tokens included.
; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{o} =
; DEFINE: %{oracle} = %ilasm-verify -output=%{o}.ilasm.o %{o}.il && \
; DEFINE:   %{canon} %{o}.o > %{o}.canon1 && %{canon} %{o}.ilasm.o > %{o}.canon2 && diff %{o}.canon1 %{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %{o}.o > %{o}.link1 && llvm-cil-dump --link-info %{o}.ilasm.o > %{o}.link2 && \
; DEFINE:   diff %{o}.link1 %{o}.link2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form -verify-machineinstrs < %t/sym.ll -o %t/sym.win.il 2>&1 | count 0
; REDEFINE: %{o} = %t/sym.win
; RUN: %if ilasm %{ %{oracle} %}

; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/dll.ll \
; RUN:   -o %t/dll.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/dll.o | %{rec} --check-prefix=DLL

; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/dm.ll -o %t/dm.msvc.o
; RUN: llvm-cil-dump --link-info %t/dm.msvc.o | %{rec} --check-prefixes=DM,DM-MSVC
; RUN: llc -mtriple=x86_64-w64-windows-gnu-msil -filetype=obj -verify-machineinstrs < %t/dm.ll -o %t/dm.mingw.o
; RUN: llvm-cil-dump --link-info %t/dm.mingw.o | %{rec} --check-prefixes=DM,DM-MINGW
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/dm.ll -o %t/dm.gnu.o
; RUN: llvm-cil-dump --link-info %t/dm.gnu.o | %{rec} --check-prefixes=DM,DM-GNU
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/dm.ll -o %t/dm.a64.o
; RUN: llvm-cil-dump --link-info %t/dm.a64.o | %{rec} --check-prefixes=DM,DM-A64
; RUN: llc -mtriple=aarch64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/dm.ll -o %t/dm.a64msvc.o
; RUN: llvm-cil-dump --link-info %t/dm.a64msvc.o | %{rec} --check-prefixes=DM,DM-A64-MSVC
; RUN: llc -mtriple=aarch64-w64-windows-gnu-msil -filetype=obj -verify-machineinstrs < %t/dm.ll -o %t/dm.a64mingw.o
; RUN: llvm-cil-dump --link-info %t/dm.a64mingw.o | %{rec} --check-prefixes=DM,DM-A64-MINGW
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/dm-wchar2.ll -o %t/dm.wchar2.o
; RUN: llvm-cil-dump --link-info %t/dm.wchar2.o | %{rec} --check-prefixes=DM,DM-WCHAR2

; LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; CHECK-NEXT:Symbol 0 DATA GLOBAL DEFAULT 'g_ext' field=0x04000001 segment=0 offset=0 size=4
; CHECK-NEXT:Symbol 1 DATA LOCAL DEFAULT 'g_int' field=0x04000002 segment=1 offset=0 size=4
; CHECK-NEXT:Symbol 2 DATA LOCAL DEFAULT 'g_priv' field=0x04000003 segment=2 offset=0 size=4
; CHECK-NEXT:Symbol 3 DATA WEAK DEFAULT 'g_weak' field=0x04000004 segment=3 offset=0 size=4
; CHECK-NEXT:Symbol 4 DATA WEAK DEFAULT 'g_weak_odr' field=0x04000005 segment=4 offset=0 size=4
; CHECK-NEXT:Symbol 5 DATA WEAK DEFAULT 'g_linkonce' field=0x04000006 segment=5 offset=0 size=4
; CHECK-NEXT:Symbol 6 DATA WEAK DEFAULT 'g_linkonce_odr' field=0x04000007 segment=6 offset=0 size=4
; CHECK-NEXT:Symbol 7 DATA COMMON DEFAULT 'g_common' field=0x04000008 size=8 align=8
; CHECK-NEXT:Symbol 8 DATA GLOBAL HIDDEN 'g_hidden' field=0x04000009 segment=7 offset=0 size=4
; CHECK-NEXT:Symbol 9 DATA GLOBAL PROTECTED 'g_protected' field=0x0400000A segment=8 offset=0 size=4
; CHECK-NEXT:Symbol 10 DATA GLOBAL DEFAULT NO_STRIP 'g_used' field=0x0400000B segment=9 offset=0 size=4
; CHECK-NEXT:Symbol 11 DATA GLOBAL DEFAULT 'g_cused' field=0x0400000C segment=10 offset=0 size=4
; CHECK-NEXT:Symbol 12 DATA GLOBAL DEFAULT UNDEFINED 'g_avail'
; CHECK-NEXT:Symbol 13 DATA GLOBAL DEFAULT UNDEFINED 'g_decl'
; CHECK-NEXT:Symbol 14 DATA WEAK DEFAULT UNDEFINED 'g_extweak'
; CHECK-NEXT:Symbol 15 FUNCTION GLOBAL DEFAULT 'f_ext' method=0x06000001
; CHECK-NEXT:Symbol 16 FUNCTION LOCAL DEFAULT 'f_int' method=0x06000002
; CHECK-NEXT:Symbol 17 FUNCTION LOCAL DEFAULT 'f_priv' method=0x06000003
; CHECK-NEXT:Symbol 18 FUNCTION WEAK DEFAULT 'f_weak' method=0x06000004
; CHECK-NEXT:Symbol 19 FUNCTION WEAK DEFAULT 'f_weak_odr' method=0x06000005
; CHECK-NEXT:Symbol 20 FUNCTION WEAK DEFAULT 'f_linkonce' method=0x06000006
; CHECK-NEXT:Symbol 21 FUNCTION WEAK DEFAULT 'f_linkonce_odr' method=0x06000007
; CHECK-NEXT:Symbol 22 FUNCTION GLOBAL HIDDEN 'f_hidden' method=0x06000008
; CHECK-NEXT:Symbol 23 FUNCTION GLOBAL PROTECTED 'f_protected' method=0x06000009
; CHECK-NEXT:Symbol 24 FUNCTION GLOBAL DEFAULT NO_STRIP 'f_used' method=0x0600000A
; CHECK-NEXT:Symbol 25 FUNCTION GLOBAL DEFAULT 'f_cused' method=0x0600000B
; CHECK-NEXT:Symbol 26 FUNCTION GLOBAL DEFAULT UNDEFINED 'f_avail'
; CHECK-NEXT:Symbol 27 FUNCTION GLOBAL DEFAULT UNDEFINED 'f_decl'
; CHECK-NEXT:Symbol 28 FUNCTION WEAK DEFAULT UNDEFINED 'f_extweak'
; CHECK-NEXT:Symbol 29 FUNCTION GLOBAL HIDDEN UNDEFINED 'f_hdecl'
; CHECK-NEXT:Symbol 30 FUNCTION GLOBAL DEFAULT 'use_all' method=0x0600000C
; CHECK-NEXT:Segment 0 '.data' align=4 size=4 flags=0
; CHECK-NEXT:  01 00 00 00
; CHECK-NEXT:Segment 1 '.data' align=4 size=4 flags=0
; CHECK-NEXT:  02 00 00 00
; CHECK-NEXT:Segment 2 '.data' align=4 size=4 flags=0
; CHECK-NEXT:  03 00 00 00
; CHECK-NEXT:Segment 3 '.data' align=4 size=4 flags=0
; CHECK-NEXT:  04 00 00 00
; CHECK-NEXT:Segment 4 '.data' align=4 size=4 flags=0
; CHECK-NEXT:  05 00 00 00
; CHECK-NEXT:Segment 5 '.data' align=4 size=4 flags=0
; CHECK-NEXT:  06 00 00 00
; CHECK-NEXT:Segment 6 '.data' align=4 size=4 flags=0
; CHECK-NEXT:  07 00 00 00
; CHECK-NEXT:Segment 7 '.data' align=4 size=4 flags=0
; CHECK-NEXT:  08 00 00 00
; CHECK-NEXT:Segment 8 '.data' align=4 size=4 flags=0
; CHECK-NEXT:  09 00 00 00
; CHECK-NEXT:Segment 9 '.data' align=4 size=4 flags=RETAIN
; CHECK-NEXT:  0A 00 00 00
; CHECK-NEXT:Segment 10 '.data' align=4 size=4 flags=0
; CHECK-NEXT:  0B 00 00 00
; CHECK-NOT:{{.}}

; ROWS:Table: TypeRef rows=2 rowsize={{[0-9]+}}
; ROWS-NEXT:  TypeRef[1]: ResolutionScope=0x23000001 TypeName='<msil>Symbols' TypeNamespace=''
; ROWS-NEXT:  TypeRef[2]: ResolutionScope=0x23000003 TypeName='<msil>LinkInfo' TypeNamespace=''
; ROWS:Table: Field rows=12 rowsize={{[0-9]+}}
; ROWS-NEXT:  Field[1]: Flags=0x0036 Name='g_ext' Signature=06 18
; ROWS-NEXT:  Field[2]: Flags=0x0033 Name='g_int' Signature=06 18
; ROWS-NEXT:  Field[3]: Flags=0x0033 Name='g_priv' Signature=06 18
; ROWS-NEXT:  Field[4]: Flags=0x0036 Name='g_weak' Signature=06 18
; ROWS-NEXT:  Field[5]: Flags=0x0036 Name='g_weak_odr' Signature=06 18
; ROWS-NEXT:  Field[6]: Flags=0x0036 Name='g_linkonce' Signature=06 18
; ROWS-NEXT:  Field[7]: Flags=0x0036 Name='g_linkonce_odr' Signature=06 18
; ROWS-NEXT:  Field[8]: Flags=0x0036 Name='g_common' Signature=06 18
; ROWS-NEXT:  Field[9]: Flags=0x0036 Name='g_hidden' Signature=06 18
; ROWS-NEXT:  Field[10]: Flags=0x0036 Name='g_protected' Signature=06 18
; ROWS-NEXT:  Field[11]: Flags=0x0036 Name='g_used' Signature=06 18
; ROWS-NEXT:  Field[12]: Flags=0x0036 Name='g_cused' Signature=06 18
; ROWS-NEXT:Table: MethodDef rows=12 rowsize={{[0-9]+}}
; ROWS-NEXT:  MethodDef[1]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='f_ext' Signature=00 00 01 ParamList={{.*}}
; ROWS-NEXT:  MethodDef[2]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0013 Name='f_int' Signature=00 00 01 ParamList={{.*}}
; ROWS-NEXT:  MethodDef[3]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0013 Name='f_priv' Signature=00 00 01 ParamList={{.*}}
; ROWS-NEXT:  MethodDef[4]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='f_weak' Signature=00 00 01 ParamList={{.*}}
; ROWS-NEXT:  MethodDef[5]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='f_weak_odr' Signature=00 00 01 ParamList={{.*}}
; ROWS-NEXT:  MethodDef[6]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='f_linkonce' Signature=00 00 01 ParamList={{.*}}
; ROWS-NEXT:  MethodDef[7]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='f_linkonce_odr' Signature=00 00 01 ParamList={{.*}}
; ROWS-NEXT:  MethodDef[8]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='f_hidden' Signature=00 00 01 ParamList={{.*}}
; ROWS-NEXT:  MethodDef[9]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='f_protected' Signature=00 00 01 ParamList={{.*}}
; ROWS-NEXT:  MethodDef[10]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='f_used' Signature=00 00 01 ParamList={{.*}}
; ROWS-NEXT:  MethodDef[11]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='f_cused' Signature=00 00 01 ParamList={{.*}}
; ROWS-NEXT:  MethodDef[12]: RVA=0x{{([0-9A-F]{8})}} ImplFlags=0x0000 Flags=0x0016 Name='use_all' Signature=00 00 08 ParamList={{.*}}
; ROWS-NOT:  MethodDef[
; ROWS:Table: MemberRef rows=8 rowsize={{[0-9]+}}
; ROWS-NEXT:  MemberRef[1]: Class=0x01000001 Name='f_avail' Signature=00 00 01
; ROWS-NEXT:  MemberRef[2]: Class=0x01000001 Name='f_decl' Signature=00 00 01
; ROWS-NEXT:  MemberRef[3]: Class=0x01000001 Name='f_extweak' Signature=00 00 01
; ROWS-NEXT:  MemberRef[4]: Class=0x01000001 Name='f_hdecl' Signature=00 00 01
; ROWS-NEXT:  MemberRef[5]: Class=0x01000001 Name='g_avail' Signature=06 18
; ROWS-NEXT:  MemberRef[6]: Class=0x01000001 Name='g_decl' Signature=06 18
; ROWS-NEXT:  MemberRef[7]: Class=0x01000001 Name='g_extweak' Signature=06 18
; ROWS-NEXT:  MemberRef[8]: Class=0x01000002 Name='.ctor' Signature=20 01 01 1D 05
; ROWS-NEXT:Table: {{.*}}

; Rule R1 in use_all: its calls and loads of definitions name the MethodDef
; or FieldDef on '<Module>' (canonical listing v1 prints a MethodDef operand as
; '<sig> '<Module>'::'<name>'' and a FieldDef as 'native int
; '<Module>'::'<name>''; the MemberRef table above has no row of these
; names); the undefined symbols go through '<msil>Symbols'.
; R1:  .method 'use_all' int32() flags=0x0016 impl=0x0000
; R1-NOT:  .method
; R1:    L{{[0-9]+}}: call void() '<Module>'::'f_int'
; R1-NEXT:    L{{[0-9]+}}: call void() '<Module>'::'f_priv'
; R1-NEXT:    L{{[0-9]+}}: call void() '<Module>'::'f_linkonce'
; R1-NEXT:    L{{[0-9]+}}: call void() '<Module>'::'f_linkonce_odr'
; R1-NEXT:    L{{[0-9]+}}: call void() [msil.extern]<msil>Symbols::'f_avail'
; R1-NEXT:    L{{[0-9]+}}: call void() [msil.extern]<msil>Symbols::'f_decl'
; R1-NEXT:    L{{[0-9]+}}: call void() [msil.extern]<msil>Symbols::'f_extweak'
; R1-NEXT:    L{{[0-9]+}}: call void() [msil.extern]<msil>Symbols::'f_hdecl'
; R1-NOT:  .method
; R1-DAG:    L{{[0-9]+}}: ldsfld native int '<Module>'::'g_int'
; R1-DAG:    L{{[0-9]+}}: ldsfld native int '<Module>'::'g_priv'
; R1-DAG:    L{{[0-9]+}}: ldsfld native int '<Module>'::'g_linkonce'
; R1-DAG:    L{{[0-9]+}}: ldsfld native int '<Module>'::'g_linkonce_odr'
; R1-DAG:    L{{[0-9]+}}: ldsfld native int [msil.extern]<msil>Symbols::'g_avail'
; R1-DAG:    L{{[0-9]+}}: ldsfld native int [msil.extern]<msil>Symbols::'g_decl'
; R1-DAG:    L{{[0-9]+}}: ldsfld native int [msil.extern]<msil>Symbols::'g_extweak'
; R1-NOT:  .method
; R1:module .custom {{.*}}

; DLL:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; DLL-NEXT:Symbol 0 DATA GLOBAL DEFAULT DLLEXPORT 'd_export' field=0x04000001 segment=0 offset=0 size=4
; DLL-NEXT:Symbol 1 DATA GLOBAL DEFAULT UNDEFINED DLLIMPORT 'd_import'
; DLL-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT DLLEXPORT 'fd_export' method=0x06000001
; DLL-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED DLLIMPORT 'fd_import'
; DLL-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT 'use_dll' method=0x06000002
; DLL-NEXT:Segment 0 '.data' align=4 size=4 flags=0
; DLL-NEXT:  01 00 00 00
; DLL-NOT:{{.}}

; DM-MSVC:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; DM-MINGW:LinkInfo v1.0 triple=x86_64-w64-windows-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; DM-GNU:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; DM-A64:LinkInfo v1.0 triple=aarch64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; DM-A64-MSVC:LinkInfo v1.0 triple=aarch64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; DM-A64-MINGW:LinkInfo v1.0 triple=aarch64-w64-windows-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; DM-WCHAR2:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,2,8
; DM-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT 'f' method=0x06000001
; DM-NOT:{{.}}

;--- sym.ll
source_filename = "symbols.c"

@g_ext = global i32 1, align 4
@g_int = internal global i32 2, align 4
@g_priv = private global i32 3, align 4
@g_weak = weak global i32 4, align 4
@g_weak_odr = weak_odr global i32 5, align 4
@g_linkonce = linkonce global i32 6, align 4
@g_linkonce_odr = linkonce_odr global i32 7, align 4
@g_common = common global i64 0, align 8
@g_hidden = hidden global i32 8, align 4
@g_protected = protected global i32 9, align 4
@g_used = global i32 10, align 4
@g_cused = global i32 11, align 4
@g_avail = available_externally global i32 12, align 4
@g_decl = external global i32, align 4
@g_extweak = extern_weak global i32, align 4
@g_unref = external global i32, align 4
@llvm.used = appending global [2 x ptr] [ptr @g_used, ptr @f_used], section "llvm.metadata"
@llvm.compiler.used = appending global [2 x ptr] [ptr @g_cused, ptr @f_cused], section "llvm.metadata"

define void @f_ext() {
  ret void
}

define internal void @f_int() {
  ret void
}

define private void @f_priv() {
  ret void
}

define weak void @f_weak() {
  ret void
}

define weak_odr void @f_weak_odr() {
  ret void
}

define linkonce void @f_linkonce() {
  ret void
}

define linkonce_odr void @f_linkonce_odr() {
  ret void
}

define hidden void @f_hidden() {
  ret void
}

define protected void @f_protected() {
  ret void
}

define void @f_used() {
  ret void
}

define void @f_cused() {
  ret void
}

define available_externally void @f_avail() {
  ret void
}

declare void @f_decl()

declare extern_weak void @f_extweak()

declare hidden void @f_hdecl()

declare void @f_unref()

declare i32 @llvm.smax.i32(i32, i32)

; Every local, linkonce, available_externally and declared symbol is used
; here, so that none of them can be left out as unreferenced; the intrinsic
; is referenced too, but intrinsics are never symbols.
define i32 @use_all() {
  call void @f_int()
  call void @f_priv()
  call void @f_linkonce()
  call void @f_linkonce_odr()
  call void @f_avail()
  call void @f_decl()
  call void @f_extweak()
  call void @f_hdecl()
  %a = load i32, ptr @g_int, align 4
  %b = load i32, ptr @g_priv, align 4
  %c = load i32, ptr @g_linkonce, align 4
  %d = load i32, ptr @g_linkonce_odr, align 4
  %e = load i32, ptr @g_avail, align 4
  %f = load i32, ptr @g_decl, align 4
  %g = load i32, ptr @g_extweak, align 4
  %ab = add i32 %a, %b
  %cd = add i32 %c, %d
  %ef = add i32 %e, %f
  %abcd = add i32 %ab, %cd
  %efg = add i32 %ef, %g
  %r = call i32 @llvm.smax.i32(i32 %abcd, i32 %efg)
  ret i32 %r
}

;--- dll.ll
source_filename = "dll.c"

@d_export = dllexport global i32 1, align 4
@d_import = external dllimport global i32, align 4

define dllexport i32 @fd_export() {
  ret i32 0
}

declare dllimport void @fd_import()

define i32 @use_dll() {
  call void @fd_import()
  %v = load i32, ptr @d_import, align 4
  ret i32 %v
}

;--- dm.ll
source_filename = "dm.c"

define void @f() {
  ret void
}

;--- dm-wchar2.ll
source_filename = "dm.c"

define void @f() {
  ret void
}

!llvm.module.flags = !{!0}
!0 = !{i32 1, !"wchar_size", i32 2}
