// CIL objects from the clang driver (ImplementationPlan.md M19, 5.8 and
// 5.10.5; Q1 = B2, Q6, Q20). From M19, -c and clang-cl /c write a CIL object:
// an ECMA-335 netmodule that carries the link-info record (5.8.2, 5.8.8) and
// a COFF symbol view (5.8.6). That is one 'cc1 -emit-obj' job through the
// integrated assembler (the MSIL MC object writer), with no assemble job, no
// bitcode and no link. MSILToolChain::getLTOMode returns LTOK_None for -c
// and /c without an explicit -flto, -flto= or -fno-lto, so -fno-lto -c
// writes an object too, while -flto -c and -emit-llvm -c still write LLVM
// bitcode. A compile-and-link keeps M14's bitcode default and plans msil-ld
// until M20, because msil-ld reads CIL objects only from M20 (5.10.5).
// There is no MSIL assembly parser, so ILAsm text cannot be assembled:
// -save-temps -c, -c -x assembler and clang-cl /FA and /Fa report
// err_drv_msil_ilasm_input, and with -fno-integrated-as there is no
// assembler at all (err_no_external_assembler). This file takes over the
// object block of msil-toolchain.c, inverted (5.12.13); -S stays there.
// clang/test/CodeGen/MSIL/emit-obj.c compiles with -c and /c too and dumps
// those objects (llvm-cil-dump --headers and --link-info): this directory
// loads no MSIL lit configuration, so it has no 'llvm-cil-dump' feature
// (section 12 row 'M19.e amended', 2026-10-25).

// The object compile is one cc1 job (no second cc1 job, no cc1as, no linker)
// and asks for neither bitcode nor LTO.
// DEFINE: %{obj} = --implicit-check-not=error: --implicit-check-not=warning: \
// DEFINE:   --implicit-check-not='"-cc1"' --implicit-check-not=-cc1as --implicit-check-not=-emit-llvm-bc \
// DEFINE:   --implicit-check-not=-flto= --implicit-check-not=-flto-unit --implicit-check-not=msil-ld

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX %{obj} %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-lto -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX %{obj} %s
// OBJ-LNX: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-emit-obj"
// OBJ-LNX-SAME: "-o" "{{[^"]*}}.o" "-x" "c" "{{[^"]*}}msil-object.c"

// RUN: %clang -### --target=aarch64-unknown-linux-gnu-msil -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-A64 %{obj} %s
// OBJ-A64: "-cc1" "-triple" "aarch64-unknown-linux-gnu-msil" "-emit-obj"
// OBJ-A64-SAME: "-o" "{{[^"]*}}.o"

// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-MINGW %{obj} %s
// OBJ-MINGW: "-cc1" "-triple" "x86_64-w64-windows-gnu-msil" "-emit-obj"
// OBJ-MINGW-SAME: "-o" "{{[^"]*}}.o"

// The MSVC toolchain inserts the MSVC version before the '-msil' suffix.
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-WIN %{obj} %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -fno-lto -c %s \
// RUN:   -o %t.o 2>&1 | FileCheck --check-prefix=OBJ-WIN %{obj} %s
// OBJ-WIN: "-cc1" "-triple" "x86_64-pc-windows-msvc19.40.0-msil" "-emit-obj"
// OBJ-WIN-SAME: "-o" "{{[^"]*}}.o"

// aarch64 Windows (D6): MSVC 19.40 and MinGW.
// RUN: %clang -### --target=aarch64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-A64-WIN %{obj} %s
// OBJ-A64-WIN: "-cc1" "-triple" "aarch64-pc-windows-msvc19.40.0-msil" "-emit-obj"
// OBJ-A64-WIN-SAME: "-o" "{{[^"]*}}.o"
// RUN: %clang -### --target=aarch64-w64-windows-gnu-msil -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-A64-MINGW %{obj} %s
// OBJ-A64-MINGW: "-cc1" "-triple" "aarch64-w64-windows-gnu-msil" "-emit-obj"
// OBJ-A64-MINGW-SAME: "-o" "{{[^"]*}}.o"

// clang-cl /c, and the default object names, which follow the native
// drivers: <stem>.o in GCC mode and <stem>.obj with clang-cl.
// RUN: %clang_cl -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 /c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-CL %{obj} %s
// OBJ-CL: "-cc1" "-triple" "x86_64-pc-windows-msvc19.40.0-msil" "-emit-obj"
// OBJ-CL-SAME: "-o" "msil-object.obj"
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=NAME-O %{obj} %s
// NAME-O: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-emit-obj"
// NAME-O-SAME: "-o" "msil-object.o"

// -flto -c (full or thin) and -emit-llvm -c still write LLVM bitcode, never
// a CIL object.
// DEFINE: %{bc} = --implicit-check-not=error: --implicit-check-not=-emit-obj --implicit-check-not=-cc1as \
// DEFINE:   --implicit-check-not=msil-ld
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=LTO-FULL %{bc} %s
// LTO-FULL: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-emit-llvm-bc" "-flto=full" "-flto-unit"
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -flto=thin -c %s \
// RUN:   -o %t.o 2>&1 | FileCheck --check-prefix=LTO-THIN %{bc} %s
// LTO-THIN: "-cc1" "-triple" "x86_64-pc-windows-msvc19.40.0-msil" "-emit-llvm-bc" "-flto=thin" "-flto-unit"
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -c -emit-llvm %s -o %t.bc 2>&1 \
// RUN:   | FileCheck --check-prefix=BC-LNX %{bc} --implicit-check-not=-flto= %s
// BC-LNX: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-emit-llvm-bc"
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -c -emit-llvm %s \
// RUN:   -o %t.bc 2>&1 | FileCheck --check-prefix=BC-WIN %{bc} --implicit-check-not=-flto= %s
// BC-WIN: "-cc1" "-triple" "x86_64-pc-windows-msvc19.40.0-msil" "-emit-llvm-bc"

// Assembling ILAsm text is not supported: -save-temps keeps the assemble
// step (the backend writes ILAsm text for it), and assembler inputs need one.
// Neither writes bitcode temporaries for LTO any more, and no object job is
// planned.
// DEFINE: %{asm_err} = --implicit-check-not="PLEASE submit" --implicit-check-not=-flto= \
// DEFINE:   --implicit-check-not=-emit-obj
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -save-temps -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=ILASM %{asm_err} %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -save-temps=obj -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=ILASM %{asm_err} %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -c -x assembler %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=ILASM %{asm_err} %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -c -x assembler-with-cpp %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=ILASM %{asm_err} %s
// ILASM: error: assembling ILAsm text is not supported for MSIL target 'x86_64-unknown-linux-gnu-msil'; compile C or LLVM IR sources instead, or use -S to write ILAsm text

// The options that keep the assemble step from being collapsed (clang-cl /FA
// and /Fa<file>, -via-file-asm) also turn the bitcode default off without
// -flto, -flto= or -fno-lto (5.10.5's getLTOMode rule, section 12 row of
// 2026-10-22), so the assemble step stays and reports the same error.
// DEFINE: %{no_bc} = --implicit-check-not="PLEASE submit" --implicit-check-not=-emit-llvm-bc \
// DEFINE:   --implicit-check-not=-emit-obj
// RUN: not %clang_cl -### --target=x86_64-pc-windows-msvc-msil /FA /c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ILASM-WIN %{no_bc} %s
// RUN: not %clang_cl -### --target=x86_64-pc-windows-msvc-msil /Fa%t.asm /c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ILASM-WIN %{no_bc} %s
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -via-file-asm -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=ILASM-WIN %{no_bc} %s
// ILASM-WIN: error: assembling ILAsm text is not supported for MSIL target 'x86_64-pc-windows-msvc{{[0-9.]*}}-msil'; compile C or LLVM IR sources instead, or use -S to write ILAsm text

// There is no external CIL assembler (MSILToolChain::buildAssembler, as
// MSVCToolChain::buildAssembler does): -fno-integrated-as -c is an error on
// every MSIL triple, and no GNU 'as' job is planned.
// DEFINE: %{no_as} = --implicit-check-not="PLEASE submit" --implicit-check-not='"as"' \
// DEFINE:   --implicit-check-not=-flto= --implicit-check-not=-emit-obj
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-integrated-as -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=NO-AS %{no_as} %s
// RUN: not %clang -### --target=x86_64-w64-windows-gnu-msil -fno-integrated-as -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=NO-AS %{no_as} %s
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -fno-integrated-as \
// RUN:   -c %s -o %t.o 2>&1 | FileCheck --check-prefix=NO-AS %{no_as} %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-integrated-as -c -x assembler %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=NO-AS %{no_as} %s
// NO-AS: error: there is no external assembler that can be used on this platform

// A link without -c still compiles to bitcode and plans msil-ld with LTO
// until M20 (msil-link.c and msil-lto.c check the link jobs), also with
// -fno-integrated-as, and never runs 'ld'.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=LINK --implicit-check-not=error: --implicit-check-not='"ld"' \
// RUN:     --implicit-check-not=-emit-obj %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-integrated-as %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=LINK --implicit-check-not=error: --implicit-check-not='"ld"' \
// RUN:     --implicit-check-not='"as"' --implicit-check-not=-emit-obj %s
// LINK: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-emit-llvm-bc" "-flto=full"
// LINK: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil"
// LINK-SAME: "-o" "{{[^"]*}}.dll"

// -S without -mllvm -msil-object-form still writes whole-program ILAsm text.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=TEXT --implicit-check-not=error: --implicit-check-not=-emit-obj \
// RUN:     --implicit-check-not=-flto= %s
// TEXT: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-S"

// The compiled objects (only where the MSIL backend is built). Each compile
// is silent. The object of x86_64 Linux does not depend on the output path
// or the working directory (O3, O4): three compiles into two directories give
// identical bytes. emit-obj.c checks the objects' headers and link-info
// records.
// RUN: rm -rf %t.dir && mkdir -p %t.dir/a %t.dir/b
// RUN: %if msil-registered-target %{ %clang --target=x86_64-unknown-linux-gnu-msil -c %s -o %t.dir/lnx.o 2>&1 \
// RUN:   | count 0 %}
// RUN: %if msil-registered-target %{ cd %t.dir/a && %clang --target=x86_64-unknown-linux-gnu-msil -c %s -o one.o %}
// RUN: %if msil-registered-target %{ cd %t.dir/b && %clang --target=x86_64-unknown-linux-gnu-msil -c %s -o two.o %}
// RUN: %if msil-registered-target %{ cmp %t.dir/lnx.o %t.dir/a/one.o %}
// RUN: %if msil-registered-target %{ cmp %t.dir/a/one.o %t.dir/b/two.o %}
// RUN: %if msil-registered-target %{ %clang --target=aarch64-unknown-linux-gnu-msil -c %s -o %t.dir/a64.o 2>&1 \
// RUN:   | count 0 %}
// RUN: %if msil-registered-target %{ %clang --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 \
// RUN:   -c %s -o %t.dir/win.o 2>&1 | count 0 %}
// RUN: %if msil-registered-target %{ %clang_cl --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 \
// RUN:   /c /Fo%t.dir/cl.obj -- %s 2>&1 | count 0 %}

// The COFF symbol view (5.8.6) gives llvm-nm the native letters: T for the
// function, R, D and B by the segment's flags, lowercase for the LOCAL
// symbol, U for the undefined function.
// RUN: %if msil-registered-target %{ llvm-nm %t.dir/lnx.o | FileCheck --check-prefix=NM %s %}
// NM:      {{^[0-9a-f]+}} T main{{$}}
// NM-NEXT: {{^ +}}U msil_ext{{$}}
// NM-NEXT: {{^[0-9a-f]+}} d msil_local{{$}}
// NM-NEXT: {{^[0-9a-f]+}} R msil_table{{$}}
// NM-NEXT: {{^[0-9a-f]+}} D msil_value{{$}}
// NM-NEXT: {{^[0-9a-f]+}} B msil_zero{{$}}

// Object-form text (5.8.4) is the same object as ILAsm: the three placeholder
// assembly references, the method (calling the undefined function through
// '<msil>Symbols'), one address field per defined variable (assembly access
// for the LOCAL one), then the Module row with the link-info record last.
// There is no assembly row, entry point, .cctor, data image, mscorlib
// reference or import stub.
// RUN: %if msil-registered-target %{ %clang --target=x86_64-unknown-linux-gnu-msil -S -mllvm -msil-object-form \
// RUN:   %s -o - | FileCheck --check-prefix=FORM --implicit-check-not=".assembly '" \
// RUN:     --implicit-check-not=.entrypoint --implicit-check-not=.cctor --implicit-check-not='<msil>image' \
// RUN:     --implicit-check-not='<msil>Main' --implicit-check-not=mscorlib --implicit-check-not=pinvokeimpl %s %}
// FORM:      // MSIL object generated by LLVM
// FORM-NEXT: // source_filename = "{{.*}}msil-object.c"
// FORM-NEXT: // target triple = "x86_64-unknown-linux-gnu-msil"
// FORM-NEXT: .assembly extern 'msil.extern' { .ver 0:0:0:0 }
// FORM-NEXT: .assembly extern 'msil.bcl' { .ver 0:0:0:0 }
// FORM-NEXT: .assembly extern 'msil.link' { .ver 0:0:0:0 }
// FORM:      .method public static int32 'main'() cil managed
// FORM:      call int32 [msil.extern]'<msil>Symbols'::'msil_ext'(int32)
// FORM:      } // end of method 'main'
// FORM:      .field public static initonly native int 'msil_table'
// FORM:      .field public static initonly native int 'msil_value'
// FORM:      .field assembly static initonly native int 'msil_local'
// FORM:      .field public static initonly native int 'msil_zero'
// FORM:      .module 'msil-object.c'
// FORM-NEXT: .custom instance void [msil.link]'<msil>LinkInfo'::.ctor(uint8[]) = ( 01 00

// Whole-program -S of the same file still rejects the undefined function
// (D14's standalone policy, frozen until the end of Phase 2).
// RUN: %if msil-registered-target %{ not %clang --target=x86_64-unknown-linux-gnu-msil -S %s -o %t.dir/lnx.il 2>&1 \
// RUN:   | FileCheck --check-prefix=WHOLE --implicit-check-not="PLEASE submit" %s %}
// WHOLE: error: undefined function 'msil_ext' is not defined in this module

int msil_zero;
static int msil_local = 7;
const int msil_table[2] = {1, 2};
int msil_value = 5;
int msil_ext(int);

int main(void) {
  return msil_ext(msil_value) + msil_table[1] + msil_local + msil_zero;
}
