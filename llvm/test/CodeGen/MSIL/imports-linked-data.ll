; Native data imports in linked text (ImplementationPlan.md 5.10.12 and 5.10.13,
; Q15, M20; recon native-imports-files/data-win.il). In a module with the flag
; "msil-linked" (msil-ld's --lto-emit-asm text), an undefined variable that a
; native library binds as data comes as a kind-1 !msil.imports operand
; !{!"<symbol>", !"<module>", !"<entry>", i32 1}, or as a variable with the
; "msil-import-module"/"msil-import-name" attributes, and msil-ld adds kind-0
; operands for the two functions of the data-import helper. The backend:
; - keeps the variable's address field (D12), declared without initonly
;   ('.field public static native int', because a store outside .cctor into
;   an initonly field fails verification), and code reads it as usual;
; - declares the helper's two functions as linked P/Invokes (5.10.11):
;   kernel32.dll's LoadLibraryW and GetProcAddress on Windows triples
;   (DataImportHelper Kernel32), dlopen and dlsym as bound on ELF triples
;   (Dlfcn);
; - emits '<msil>dataimport'(string, string) : native int, shaped as
;   data-win.il: Marshal::StringToHGlobalUni + LoadLibraryW and
;   Marshal::StringToHGlobalAnsi + GetProcAddress (Kernel32), or
;   StringToHGlobalAnsi + dlopen(path, 1) and StringToHGlobalAnsi + dlsym
;   (Dlfcn);
; - emits '<msil>init_dataimports'(), which stores each address in its field
;   and throws EntryPointNotFoundException("data import '<entry>' not found
;   in <module>") for a null result, with module and entry verbatim;
; - calls it from <Module>::.cctor before the data relocations (here
;   p = &_iob + 48), never inlining the lookups into .cctor.
; Without "msil-linked" the same variable stays D26's error (standalone
; mode never imports data). x86_64 triples (D6), -O2 and -O0; every linked
; output compiles silently and assembles.

; DEFINE: %{llc} = llc -verify-machineinstrs -asm-verbose=false
; RUN: split-file %s %t
; RUN: cat %t/win-body.ll %t/linked.ll > %t/win.ll
; RUN: cat %t/lnx-body.ll %t/linked.ll > %t/lnx.ll

; Kernel32 on the Windows triples.
; DEFINE: %{win} = FileCheck %s --implicit-check-not="initonly native int '_iob'" \
; DEFINE:   --implicit-check-not="initonly native int 'test_data'" --implicit-check-not=dlopen
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/win.ll -o %t/win.msvc.il 2>&1 | count 0
; RUN: %{win} --check-prefixes=COMMON,WIN --input-file=%t/win.msvc.il
; RUN: FileCheck %s --check-prefix=WINH --input-file=%t/win.msvc.il
; RUN: FileCheck %s --check-prefix=WINIT --input-file=%t/win.msvc.il
; RUN: FileCheck %s --check-prefix=CCTOR --input-file=%t/win.msvc.il
; RUN: %{llc} -mtriple=x86_64-w64-windows-gnu-msil < %t/win.ll -o %t/win.mingw.il 2>&1 | count 0
; RUN: %{win} --check-prefixes=COMMON,WIN --input-file=%t/win.mingw.il
; RUN: FileCheck %s --check-prefix=WINH --input-file=%t/win.mingw.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/win.ll -o %t/win.o0.il 2>&1 | count 0
; RUN: %{win} --check-prefixes=COMMON,WIN --input-file=%t/win.o0.il
; RUN: FileCheck %s --check-prefix=WINIT --input-file=%t/win.o0.il
; RUN: FileCheck %s --check-prefix=CCTOR --input-file=%t/win.o0.il

; Dlfcn on the ELF triples.
; DEFINE: %{lnx} = FileCheck %s --implicit-check-not="initonly native int 'environ'" \
; DEFINE:   --implicit-check-not="initonly native int 'test_data'" --implicit-check-not=LoadLibraryW \
; DEFINE:   --implicit-check-not=StringToHGlobalUni
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/lnx.ll -o %t/lnx.gnu.il 2>&1 | count 0
; RUN: %{lnx} --check-prefixes=COMMON,LNX --input-file=%t/lnx.gnu.il
; RUN: FileCheck %s --check-prefix=LNXH --input-file=%t/lnx.gnu.il
; RUN: FileCheck %s --check-prefix=LNXIT --input-file=%t/lnx.gnu.il
; RUN: FileCheck %s --check-prefix=LCCTOR --input-file=%t/lnx.gnu.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-musl-msil < %t/lnx.ll -o %t/lnx.musl.il 2>&1 | count 0
; RUN: %{lnx} --check-prefixes=COMMON,LNX --input-file=%t/lnx.musl.il
; RUN: FileCheck %s --check-prefix=LNXH --input-file=%t/lnx.musl.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/lnx.ll -o %t/lnx.o0.il 2>&1 | count 0
; RUN: %{lnx} --check-prefixes=COMMON,LNX --input-file=%t/lnx.o0.il
; RUN: FileCheck %s --check-prefix=LNXIT --input-file=%t/lnx.o0.il

; Standalone: no data imports.
; DEFINE: %{sa} = FileCheck %s --check-prefix=SA --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=dataimport
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/win-body.ll -o /dev/null 2>&1 | %{sa}
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/lnx-body.ll -o /dev/null 2>&1 | %{sa}
; SA: {{^}}error: external global variable '{{_iob|environ}}' is not supported (phase 2 linker){{$}}

; RUN: %if ilasm %{ %ilasm-verify -output=%t/win.msvc.dll %t/win.msvc.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/win.o0.dll %t/win.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/lnx.gnu.dll %t/lnx.gnu.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/lnx.musl.dll %t/lnx.musl.il %}

;--- linked.ll
!llvm.module.flags = !{!0, !1}
!0 = !{i32 4, !"msil-linked", i32 1}
!1 = !{i32 4, !"msil-lto", i32 1}

;--- win-body.ll
; MinGW's msvcrt _iob (data-win.il), bound by msil-ld through a kind-1
; operand, and a test library's variable with import attributes.
source_filename = "ld-temp.o"

@_iob = external global [0 x i8], align 8
@test_data = external global i32, align 4 #0
@p = global ptr getelementptr (i8, ptr @_iob, i64 48), align 8

; COMMON-LABEL: {{^}}.method public static native int 'get_iob'() cil managed{{$}}
; WIN:          {{^}}  ldsfld native int '_iob'{{$}}
; LNX:          {{^}}  ldsfld native int 'environ'{{$}}
; COMMON:       {{^}}} // end of method 'get_iob'{{$}}
define ptr @get_iob() {
  ret ptr @_iob
}

; COMMON-LABEL: {{^}}.method public static int32 'get_test'() cil managed{{$}}
; COMMON:       {{^}}  ldsfld native int 'test_data'{{$}}
; COMMON:       {{^}}} // end of method 'get_test'{{$}}
define i32 @get_test() {
  %v = load i32, ptr @test_data, align 4
  ret i32 %v
}

define i32 @main() {
  ret i32 0
}

attributes #0 = { "msil-import-module"="testlib.dll" "msil-import-name"="test_data" }

!msil.imports = !{!10, !11, !12}
!10 = !{!"_iob", !"msvcrt.dll", !"_iob", i32 1}
!11 = !{!"LoadLibraryW", !"kernel32.dll", !"LoadLibraryW", i32 0}
!12 = !{!"GetProcAddress", !"kernel32.dll", !"GetProcAddress", i32 0}

; The address fields, without initonly; the helper's P/Invokes.
; COMMON-DAG: {{^}}.field public static native int 'test_data'{{$}}
; WIN-DAG:    {{^}}.field public static native int '_iob'{{$}}
; WIN-DAG:    {{^}}.method assembly hidebysig static pinvokeimpl("kernel32.dll" nomangle cdecl) native int 'LoadLibraryW'(native int) cil managed preservesig {}{{$}}
; WIN-DAG:    {{^}}.method assembly hidebysig static pinvokeimpl("kernel32.dll" nomangle cdecl) native int 'GetProcAddress'(native int, native int) cil managed preservesig {}{{$}}
; LNX-DAG:    {{^}}.field public static native int 'environ'{{$}}
; LNX-DAG:    {{^}}.method assembly hidebysig static pinvokeimpl("libc.so.6" nomangle cdecl) native int 'dlopen'(native int, int32) cil managed preservesig {}{{$}}
; LNX-DAG:    {{^}}.method assembly hidebysig static pinvokeimpl("libc.so.6" nomangle cdecl) native int 'dlsym'(native int, native int) cil managed preservesig {}{{$}}

; data-win.il's lookup.
; WINH-LABEL: {{^}}.method {{.*}}static native int '<msil>dataimport'(string, string) cil managed{{$}}
; WINH:       {{^}}  ldarg.0{{$}}
; WINH:       {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalUni(string){{$}}
; WINH:       {{^}}  call native int 'LoadLibraryW'(native int){{$}}
; WINH:       {{^}}  ldarg.1{{$}}
; WINH:       {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string){{$}}
; WINH:       {{^}}  call native int 'GetProcAddress'(native int, native int){{$}}
; WINH:       {{^}}  ret{{$}}
; WINH:       {{^}}} // end of method '<msil>dataimport'{{$}}

; One lookup per import, module and entry verbatim, with its null check.
; WINIT-LABEL: {{^}}.method {{.*}}static void '<msil>init_dataimports'() cil managed{{$}}
; WINIT-DAG:   {{^}}  ldstr "msvcrt.dll"{{$}}
; WINIT-DAG:   {{^}}  ldstr "_iob"{{$}}
; WINIT-DAG:   {{^}}  ldstr "data import {{\\?}}'_iob{{\\?}}' not found in msvcrt.dll"{{$}}
; WINIT-DAG:   {{^}}  stsfld native int '_iob'{{$}}
; WINIT-DAG:   {{^}}  ldstr "testlib.dll"{{$}}
; WINIT-DAG:   {{^}}  ldstr "test_data"{{$}}
; WINIT-DAG:   {{^}}  ldstr "data import {{\\?}}'test_data{{\\?}}' not found in testlib.dll"{{$}}
; WINIT-DAG:   {{^}}  stsfld native int 'test_data'{{$}}
; WINIT-DAG:   {{^}}  call native int '<msil>dataimport'(string, string){{$}}
; WINIT-DAG:   {{^}}  call native int '<msil>dataimport'(string, string){{$}}
; WINIT-DAG:   {{^}}  newobj instance void [mscorlib]System.EntryPointNotFoundException::.ctor(string){{$}}
; WINIT-DAG:   {{^}}  throw{{$}}
; WINIT:       {{^}}} // end of method '<msil>init_dataimports'{{$}}

; .cctor: the initializer before the relocation p = &_iob + 48; no lookup of
; its own.
; CCTOR-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CCTOR-NOT:   {{^}}  ldstr
; CCTOR:       {{^}}  call void '<msil>init_dataimports'(){{$}}
; CCTOR-NOT:   {{^}}  ldstr
; CCTOR:       {{^}}  ldsfld native int '_iob'{{$}}
; CCTOR-NEXT:  {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; CCTOR-NEXT:  {{^}}  add{{$}}
; CCTOR-NEXT:  {{^}}  stind.i{{$}}
; CCTOR-NOT:   {{^}}  ldstr
; CCTOR:       {{^}}  ret{{$}}
; CCTOR-NEXT:  {{^}}} // end of method '.cctor'{{$}}

;--- lnx-body.ll
; glibc's environ through a kind-1 operand, a test library's variable with
; import attributes.
source_filename = "ld-temp.o"

@environ = external global ptr, align 8
@test_data = external global i32, align 4 #0
@p = global ptr getelementptr (i8, ptr @environ, i64 48), align 8

define ptr @get_iob() {
  ret ptr @environ
}

define i32 @get_test() {
  %v = load i32, ptr @test_data, align 4
  ret i32 %v
}

define i32 @main() {
  ret i32 0
}

attributes #0 = { "msil-import-module"="libtest.so" "msil-import-name"="test_data" }

!msil.imports = !{!10, !11, !12}
!10 = !{!"environ", !"libc.so.6", !"environ", i32 1}
!11 = !{!"dlopen", !"libc.so.6", !"dlopen", i32 0}
!12 = !{!"dlsym", !"libc.so.6", !"dlsym", i32 0}

; dlopen(path, 1) and dlsym.
; LNXH-LABEL: {{^}}.method {{.*}}static native int '<msil>dataimport'(string, string) cil managed{{$}}
; LNXH:       {{^}}  ldarg.0{{$}}
; LNXH:       {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string){{$}}
; LNXH:       {{^}}  ldc.i4.1{{$}}
; LNXH:       {{^}}  call native int 'dlopen'(native int, int32){{$}}
; LNXH:       {{^}}  ldarg.1{{$}}
; LNXH:       {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string){{$}}
; LNXH:       {{^}}  call native int 'dlsym'(native int, native int){{$}}
; LNXH:       {{^}}  ret{{$}}
; LNXH:       {{^}}} // end of method '<msil>dataimport'{{$}}

; LNXIT-LABEL: {{^}}.method {{.*}}static void '<msil>init_dataimports'() cil managed{{$}}
; LNXIT-DAG:   {{^}}  ldstr "libc.so.6"{{$}}
; LNXIT-DAG:   {{^}}  ldstr "environ"{{$}}
; LNXIT-DAG:   {{^}}  ldstr "data import {{\\?}}'environ{{\\?}}' not found in libc.so.6"{{$}}
; LNXIT-DAG:   {{^}}  stsfld native int 'environ'{{$}}
; LNXIT-DAG:   {{^}}  ldstr "libtest.so"{{$}}
; LNXIT-DAG:   {{^}}  ldstr "test_data"{{$}}
; LNXIT-DAG:   {{^}}  ldstr "data import {{\\?}}'test_data{{\\?}}' not found in libtest.so"{{$}}
; LNXIT-DAG:   {{^}}  stsfld native int 'test_data'{{$}}
; LNXIT:       {{^}}} // end of method '<msil>init_dataimports'{{$}}

; LCCTOR-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; LCCTOR:       {{^}}  call void '<msil>init_dataimports'(){{$}}
; LCCTOR:       {{^}}  ldsfld native int 'environ'{{$}}
; LCCTOR-NEXT:  {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; LCCTOR-NEXT:  {{^}}  add{{$}}
; LCCTOR-NEXT:  {{^}}  stind.i{{$}}
; LCCTOR:       {{^}}} // end of method '.cctor'{{$}}
