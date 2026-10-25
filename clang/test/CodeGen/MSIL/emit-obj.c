// 'clang -cc1 -emit-obj' on an MSIL triple, also reached through the driver's
// '-S -Xclang -emit-obj', writes a CIL object from M19 (ImplementationPlan.md
// M19, 5.8.2 and 5.8.8): an ECMA-335 netmodule with the link-info record on
// its Module row. Until M19 these three commands were clean errors
// (emit-obj-unsupported.c, section 12 row 'fix-loop ownership:
// clang/test/CodeGen/MSIL/emit-obj-unsupported.c -> fix-6'); now each exits
// with status 0 and prints nothing.
//
// The objects that 'clang -c' and clang-cl '/c' write (5.10.5) are dumped here
// too: clang/test/Driver loads no MSIL lit configuration, so it has no
// 'llvm-cil-dump' feature, and the tool is not in CLANG_TEST_DEPS (section 12
// row 'M19.e amended', 2026-10-25). clang/test/Driver/msil-object.c keeps the
// '-###' job matrix, the silent compiles, determinism, llvm-nm and object-form
// text; llvm/test/CodeGen/MSIL/object-*.ll check object contents through llc.
//
// A codegen Error still never crashes or aborts: the fork's
// consumeError(CodeGenError) before err_fe_unable_to_interface_with_target in
// clang/lib/CodeGen/BackendUtil.cpp (section 12 row 'fix-loop ownership:
// clang/lib/CodeGen/BackendUtil.cpp -> fix-6') is reached with
// '-mllvm -force-new-pm-codegen': MSILTargetMachine does not override
// buildCodeGenPipeline, so runCodeGenPipeline gets TargetMachine's
// 'buildCodeGenPipeline is not overridden' Error, which an assertions build
// would abort on if it were left unconsumed.

// REQUIRES: msil-registered-target, llvm-cil-dump

// DEFINE: %{quiet} = FileCheck %s --check-prefix=QUIET --allow-empty --implicit-check-not="PLEASE submit" \
// DEFINE:   --implicit-check-not="LLVM ERROR" --implicit-check-not="Program aborted"
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-obj %s -o %t.lnx.o 2>&1 | %{quiet}
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-obj %s -o %t.win.o 2>&1 | %{quiet}
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -S -Xclang -emit-obj %s -o %t.drv.o 2>&1 | %{quiet}
// QUIET-NOT: {{error|warning}}:

// The driver's object compiles: x86_64 and aarch64 Linux, MSVC 19.40 (whose
// version the driver inserts before '-msil'), clang-cl /c, and aarch64 Windows
// with MSVC 19.40 and MinGW (D6). Each is silent.
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -c %s -o %t.c-lnx.o 2>&1 | %{quiet}
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -c %s -o %t.c-a64.o 2>&1 | %{quiet}
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -c %s -o %t.c-win.o 2>&1 \
// RUN:   | %{quiet}
// RUN: %clang_cl --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 /c /Fo%t.cl.obj -- %s 2>&1 \
// RUN:   | %{quiet}
// RUN: %clang --target=aarch64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -c %s -o %t.c-a64win.o 2>&1 \
// RUN:   | %{quiet}
// RUN: %clang --target=aarch64-w64-windows-gnu-msil -c %s -o %t.c-a64gnu.o 2>&1 | %{quiet}

// The envelope (5.8.2 O1, 5.8.6): a PE32+ DLL image of the triple's machine
// (0x8664 for x86_64, 0xAA64 for aarch64) with characteristics 0x2022
// (EXECUTABLE_IMAGE, LARGE_ADDRESS_AWARE, DLL), .text and the three empty
// marker sections, one COFF symbol per SYMTAB symbol (five here), CorFlags
// ILONLY and no entry point, although the object defines main.
// RUN: llvm-cil-dump --headers %t.lnx.o | FileCheck %s --check-prefixes=HDR,HDR-X64
// RUN: llvm-cil-dump --headers %t.win.o | FileCheck %s --check-prefixes=HDR,HDR-X64
// RUN: llvm-cil-dump --headers %t.drv.o | FileCheck %s --check-prefixes=HDR,HDR-X64
// RUN: llvm-cil-dump --headers %t.c-lnx.o | FileCheck %s --check-prefixes=HDR,HDR-X64
// RUN: llvm-cil-dump --headers %t.c-win.o | FileCheck %s --check-prefixes=HDR,HDR-X64
// RUN: llvm-cil-dump --headers %t.cl.obj | FileCheck %s --check-prefixes=HDR,HDR-X64
// RUN: llvm-cil-dump --headers %t.c-a64.o | FileCheck %s --check-prefixes=HDR,HDR-A64
// RUN: llvm-cil-dump --headers %t.c-a64win.o | FileCheck %s --check-prefixes=HDR,HDR-A64
// RUN: llvm-cil-dump --headers %t.c-a64gnu.o | FileCheck %s --check-prefixes=HDR,HDR-A64
// HDR-X64:  Format: PE32+ machine=0x8664 dll
// HDR-A64:  Format: PE32+ machine=0xAA64 dll
// HDR-NEXT: COFF: sections=4 timestamp=0x{{[0-9A-F]+}} characteristics=0x2022 symbols=5
// HDR-NEXT: Optional: {{.*}}
// HDR-NEXT: Section: .text rva=0x{{[0-9A-F]+}} {{.*}}
// HDR-NEXT: Section: .data rva=0x{{[0-9A-F]+}} vsize=0x0 raw=0x0 rawsize=0x0 flags=0xC0000040
// HDR-NEXT: Section: .rdata rva=0x{{[0-9A-F]+}} vsize=0x0 raw=0x0 rawsize=0x0 flags=0x40000040
// HDR-NEXT: Section: .bss rva=0x{{[0-9A-F]+}} vsize=0x0 raw=0x0 rawsize=0x0 flags=0xC0000080
// HDR-NEXT: CLI: flags=0x00000001 entrypoint=0x00000000 {{.*}}

// The Module row is named after the source file, never after -o or /Fo, with
// Generation, EncId and EncBaseId 0 (O3). A netmodule has no Assembly, File,
// ExportedType or ManifestResource row (O2), and its data travels in the
// record, not in FieldRVA rows (O5).
// RUN: llvm-cil-dump --tables %t.lnx.o | FileCheck %s --check-prefix=TBL
// RUN: llvm-cil-dump --tables %t.win.o | FileCheck %s --check-prefix=TBL
// RUN: llvm-cil-dump --tables %t.drv.o | FileCheck %s --check-prefix=TBL
// RUN: llvm-cil-dump --tables %t.c-lnx.o | FileCheck %s --check-prefix=TBL
// RUN: llvm-cil-dump --tables %t.cl.obj | FileCheck %s --check-prefix=TBL
// TBL:      Table: Module rows=1 rowsize={{[0-9]+}}
// TBL-NEXT:   Module[1]: Generation=0x0000 Name='emit-obj.c' Mvid={{.*}} EncId=0 EncBaseId=0
// TBL-NOT:  Table: FieldRVA
// TBL-NOT:  Table: Assembly rows
// TBL-NOT:  Table: File rows
// TBL-NOT:  Table: ExportedType rows
// TBL-NOT:  Table: ManifestResource rows

// The link-info record (5.8.8, 5.8.11's --link-info lines). The header has
// the target machine's triple (cc1's verbatim, the driver's with the MSVC
// version it adds), ABI version 1 and the triple's data model (sizeof void*,
// long, wchar_t, long double; clang writes no wchar_size flag for these
// defaults): 8,8,4,8 on Linux, 8,4,2,8 on Windows. SYMTAB lists the global
// variables in IR order (clang creates the constant and the initialized
// variable first, then the static and the tentative definition that main
// references), then main; FieldDefs and the MethodDef are numbered in that
// order. The unreferenced declaration f is not a symbol. Each defined variable
// has its own segment: the constant in a READONLY .rodata segment (8-aligned
// on windows-msvc, where clang aligns the array to 8), the initialized ones in
// .data with their bytes, the zero-initialized one in a ZERO .bss segment
// without bytes. clang-cl passes --dependent-lib=libcmt and oldnames, which
// become the LINKER_OPTIONS entries of !llvm.linker.options; cc1 and the GNU
// driver pass none. There are no relocations, comdats, constructors or
// call-site signatures here.
// RUN: llvm-cil-dump --link-info %t.lnx.o | FileCheck %s --check-prefixes=LI,LI-LNX,LI-A4
// RUN: llvm-cil-dump --link-info %t.win.o | FileCheck %s --check-prefixes=LI,LI-WIN,LI-A8
// RUN: llvm-cil-dump --link-info %t.drv.o | FileCheck %s --check-prefixes=LI,LI-LNX,LI-A4
// RUN: llvm-cil-dump --link-info %t.c-lnx.o | FileCheck %s --check-prefixes=LI,LI-LNX,LI-A4
// RUN: llvm-cil-dump --link-info %t.c-a64.o | FileCheck %s --check-prefixes=LI,LI-A64,LI-A4
// RUN: llvm-cil-dump --link-info %t.c-win.o | FileCheck %s --check-prefixes=LI,LI-WIN19,LI-A8
// RUN: llvm-cil-dump --link-info %t.cl.obj | FileCheck %s --check-prefixes=LI,LI-WIN19,LI-A8,LI-CL
// RUN: llvm-cil-dump --link-info %t.c-a64win.o | FileCheck %s --check-prefixes=LI,LI-A64WIN,LI-A8
// RUN: llvm-cil-dump --link-info %t.c-a64gnu.o | FileCheck %s --check-prefixes=LI,LI-A64GNU,LI-A4
// LI-LNX:      LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8{{$}}
// LI-A64:      LinkInfo v1.0 triple=aarch64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8{{$}}
// LI-WIN:      LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8{{$}}
// LI-WIN19:    LinkInfo v1.0 triple=x86_64-pc-windows-msvc19.40.0-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8{{$}}
// LI-A64WIN:   LinkInfo v1.0 triple=aarch64-pc-windows-msvc19.40.0-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8{{$}}
// LI-A64GNU:   LinkInfo v1.0 triple=aarch64-w64-windows-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8{{$}}
// LI-NEXT:     Symbol 0 DATA GLOBAL DEFAULT 'emit_obj_table' field=0x04000001 segment=0 offset=0 size=8{{$}}
// LI-NEXT:     Symbol 1 DATA GLOBAL DEFAULT 'emit_obj_value' field=0x04000002 segment=1 offset=0 size=4{{$}}
// LI-NEXT:     Symbol 2 DATA LOCAL DEFAULT 'emit_obj_local' field=0x04000003 segment=2 offset=0 size=4{{$}}
// LI-NEXT:     Symbol 3 DATA GLOBAL DEFAULT 'emit_obj_zero' field=0x04000004 segment=3 offset=0 size=4{{$}}
// LI-NEXT:     Symbol 4 FUNCTION GLOBAL DEFAULT 'main' method=0x06000001{{$}}
// LI-A4-NEXT:  Segment 0 '.rodata' align=4 size=8 flags=READONLY{{$}}
// LI-A8-NEXT:  Segment 0 '.rodata' align=8 size=8 flags=READONLY{{$}}
// LI-NEXT:     {{^}}  01 00 00 00 02 00 00 00{{$}}
// LI-NEXT:     Segment 1 '.data' align=4 size=4 flags=0{{$}}
// LI-NEXT:     {{^}}  2A 00 00 00{{$}}
// LI-NEXT:     Segment 2 '.data' align=4 size=4 flags=0{{$}}
// LI-NEXT:     {{^}}  07 00 00 00{{$}}
// LI-NEXT:     Segment 3 '.bss' align=4 size=4 flags=ZERO{{$}}
// LI-CL-NEXT:  LinkerOption LINKER_OPTION '/DEFAULTLIB:libcmt.lib'{{$}}
// LI-CL-NEXT:  LinkerOption LINKER_OPTION '/DEFAULTLIB:oldnames.lib'{{$}}
// LI-NOT:      {{.}}

// The new pass manager's codegen pipeline, which MSIL does not provide, is a
// clean error: the Error is consumed and nothing aborts.
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -mllvm -force-new-pm-codegen -emit-obj %s -o %t.npm.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NPM --implicit-check-not="PLEASE submit" --implicit-check-not="LLVM ERROR" \
// RUN:     --implicit-check-not="Program aborted"
// NPM: error: unable to interface with target machine

// The same translation unit compiles with -S (whole-program ILAsm text), so
// it has no undefined function: msil-object.c checks the undefined callee of a
// 'clang -c' object (llvm-nm, object-form text).
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S %s -o %t.il

int f(void);
int emit_obj_zero;
static int emit_obj_local = 7;
const int emit_obj_table[2] = {1, 2};
int emit_obj_value = 42;

int main(void) {
  return emit_obj_value + emit_obj_table[1] + emit_obj_local + emit_obj_zero;
}
