; msil64-unknown-dotnet, the portable target (ImplementationPlan.md 5.18,
; milestone P-1a): a C module compiles to IL and to a CIL object without any
; host triple underneath.
; - The data layout is the target's own (5.18.1 PT4):
;   'e-m:e-p:64:64-i64:64-i128:128-n8:16:32:64-S128', from opt and from llc
;   (the IR that -stop-after prints), and the triple needs no '-msil' suffix.
; - Pointers are native ints (D2): parameters, results, the address fields of
;   the globals and the pointer load through one of them.
; - Nothing is imported: no pinvokeimpl, no moduleref, no DllImport (the
;   portable target binds no native code, 5.18.6 PL4).
; - The width and byte-order guard (5.18.7 PE5 and PE6) is the first code of
;   <Module>::.cctor, before the image work of phase 1 (AllocHGlobal):
;     ldc.i4.m1; conv.i; conv.u8; ldc.i8 -1; bne.un FAIL_WIDTH
;     ldc.i4.1; call uint8[] System.BitConverter::GetBytes(int32);
;     ldc.i4.0; ldelem.u1; brfalse FAIL_ORDER
;   conv.i comes before conv.u8 (the sign extension is load-bearing: a conv.u
;   form would fire on 64-bit too). Each failure label loads T-P10's message
;   for the assembly (the source file's stem), constructs
;   System.PlatformNotSupportedException(string) and throws it; the guard
;   never calls Environment.FailFast or Environment.Exit. The branch
;   instructions may take their short forms and the constants any encoding.
; - A module with nothing in it still gets <Module>::.cctor, holding only the
;   guard (on portable outputs the .cctor always exists).
; - -filetype=obj writes the stubless AnyCPU PE32 envelope (5.18.7 PE1 and
;   PE3): machine I386 (0x014C), magic 0x10B, ImageBase 0x10000000,
;   COFF characteristics 0x2022, DllCharacteristics 0x8540, stack
;   0x400000/0x4000, heap 0x100000/0x1000, CorFlags ILONLY and entry point
;   0; its link-info record carries the normalized triple and the data model
;   8,8,4,8 (PT8). The guard is the linker's (msil-ld writes the image's
;   .cctor), so the object is not checked for it here.
; At -O0 and -O2; the text assembles.

; RUN: rm -rf %t && split-file %s %t

; The data layout.
; RUN: opt -mtriple=msil64-unknown-dotnet -S < %t/data.ll | FileCheck %s --check-prefix=DL
; RUN: llc -mtriple=msil64-unknown-dotnet -stop-after=finalize-isel -verify-machineinstrs < %t/data.ll \
; RUN:   -o %t/data.mir
; RUN: FileCheck %s --check-prefix=DLMIR --input-file=%t/data.mir
; DL:         {{^}}target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n8:16:32:64-S128"{{$}}
; DL-NEXT:    {{^}}target triple = "msil64-unknown-dotnet"{{$}}
; DLMIR:      {{^}}  target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n8:16:32:64-S128"{{$}}
; DLMIR-NEXT: {{^}}  target triple = "msil64-unknown-dotnet"{{$}}

; The text.
; DEFINE: %{o} =
; DEFINE: %{il} = llc -mtriple=msil64-unknown-dotnet %{o} -asm-verbose=false -verify-machineinstrs
; DEFINE: %{check} = FileCheck --implicit-check-not=pinvokeimpl --implicit-check-not=moduleref \
; DEFINE:   --implicit-check-not=DllImport --implicit-check-not=FailFast --implicit-check-not="Environment::Exit"
; REDEFINE: %{o} = -O2
; RUN: %{il} < %t/data.ll -o %t/data.o2.il 2>&1 | count 0
; REDEFINE: %{o} = -O0
; RUN: %{il} < %t/data.ll -o %t/data.o0.il 2>&1 | count 0
; RUN: %{il} < %t/empty.ll -o %t/empty.o0.il 2>&1 | count 0
; REDEFINE: %{o} = -O2
; RUN: %{il} < %t/empty.ll -o %t/empty.o2.il 2>&1 | count 0
; RUN: %{check} %s --check-prefixes=HDR,CODE,GUARD,DATA -DASM=portable --input-file=%t/data.o2.il
; RUN: %{check} %s --check-prefixes=HDR,CODE,GUARD,DATA -DASM=portable --input-file=%t/data.o0.il
; RUN: %{check} %s --check-prefixes=GUARD,WFAIL -DASM=portable --input-file=%t/data.o2.il
; RUN: %{check} %s --check-prefixes=GUARD,OFAIL -DASM=portable --input-file=%t/data.o2.il
; RUN: %{check} %s --check-prefixes=GUARD,WFAIL -DASM=portable --input-file=%t/data.o0.il
; RUN: %{check} %s --check-prefixes=GUARD,OFAIL -DASM=portable --input-file=%t/data.o0.il
; RUN: %{check} %s --check-prefixes=EHDR,GUARD,EMPTY -DASM=empty --input-file=%t/empty.o0.il
; RUN: %{check} %s --check-prefixes=GUARD,WFAIL -DASM=empty --input-file=%t/empty.o2.il
; RUN: %{check} %s --check-prefixes=GUARD,OFAIL -DASM=empty --input-file=%t/empty.o2.il

; One guard per output: one GetBytes call, two exceptions, both messages.
; RUN: grep -c "call uint8\[\] \[mscorlib\]System.BitConverter::GetBytes(int32)" %t/data.o2.il \
; RUN:   | FileCheck %s --check-prefix=ONE
; RUN: grep -c "newobj instance void \[mscorlib\]System.PlatformNotSupportedException::.ctor(string)" \
; RUN:   %t/data.o2.il | FileCheck %s --check-prefix=TWO
; RUN: grep -c "^.method .* .cctor() cil managed" %t/empty.o0.il | FileCheck %s --check-prefix=ONE
; RUN: grep -c "newobj instance void \[mscorlib\]System.PlatformNotSupportedException::.ctor(string)" \
; RUN:   %t/empty.o0.il | FileCheck %s --check-prefix=TWO
; ONE: {{^}}1{{$}}
; TWO: {{^}}2{{$}}

; RUN: %if ilasm %{ %ilasm-verify -output=%t/data.o2.dll %t/data.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/data.o0.dll %t/data.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/empty.o0.dll %t/empty.o0.il %}

; The object.
; RUN: %if llvm-cil-dump %{ llc -mtriple=msil64-unknown-dotnet -filetype=obj -verify-machineinstrs \
; RUN:   < %t/data.ll -o %t/data.o2.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=msil64-unknown-dotnet -O0 -filetype=obj -verify-machineinstrs \
; RUN:   < %t/data.ll -o %t/data.o0.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=msil64-unknown-dotnet-msil -filetype=obj -verify-machineinstrs \
; RUN:   < %t/data.ll -o %t/data.spelled.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --headers %t/data.o2.o | FileCheck %s --check-prefix=PE \
; RUN:   --match-full-lines --strict-whitespace %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --headers %t/data.o0.o | FileCheck %s --check-prefix=PE \
; RUN:   --match-full-lines --strict-whitespace %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/data.o2.o | FileCheck %s --check-prefix=LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/data.spelled.o | FileCheck %s --check-prefix=LINK %}
; RUN: %if llvm-cil-dump %{ cmp %t/data.o2.o %t/data.spelled.o %}
; PE:Format: PE32 machine=0x014C dll
; PE-NEXT:COFF: sections={{[0-9]+}} timestamp=0x{{([0-9A-F]{8})}} characteristics=0x2022 symbols={{[0-9]+}}
; PE-NEXT:Optional: imagebase=0x0000000010000000 sectionalign=0x2000 filealign=0x200 subsystem={{[0-9]+}} dllchars=0x8540 stack=0x400000,0x4000 heap=0x100000,0x1000
; PE-NEXT:Section: .text rva=0x00002000 vsize=0x{{[0-9A-F]+}} raw=0x{{[0-9A-F]+}} rawsize=0x{{[0-9A-F]+}} flags=0x60000020
; PE:CLI: flags=0x00000001 entrypoint=0x00000000 metadata=0x{{([0-9A-F]{8})}},0x{{[0-9A-F]+}} runtime=2.5
; LINK: {{^}}LinkInfo v1.0 triple=msil64-unknown-dotnet abi={{[0-9]+}} abiflags=0x{{[0-9A-F]+}} features=0x{{[0-9A-F]+}} datamodel=8,8,4,8{{$}}

; The header: the triple as given, the assembly named after the source file.
; HDR:       {{^}}// MSIL module generated by LLVM{{$}}
; HDR-NEXT:  {{^}}// source_filename = "portable.c"{{$}}
; HDR-NEXT:  {{^}}// target triple   = "msil64-unknown-dotnet"{{$}}
; HDR:       {{^}}.assembly 'portable' { .ver 0:0:0:0 }{{$}}
; EHDR:      {{^}}// source_filename = "empty.c"{{$}}
; EHDR-NEXT: {{^}}// target triple   = "msil64-unknown-dotnet"{{$}}
; EHDR:      {{^}}.assembly 'empty' { .ver 0:0:0:0 }{{$}}

; Pointers are native ints.
; CODE-LABEL: {{^}}.method public static native int 'distance'(native int, native int) cil managed{{$}}
; CODE:       {{^}}  sub{{$}}
; CODE:       {{^}}} // end of method 'distance'{{$}}
; CODE-LABEL: {{^}}.method public static native int 'advance'(native int) cil managed{{$}}
; CODE:       {{^}}  add{{$}}
; CODE:       {{^}}} // end of method 'advance'{{$}}
; CODE-LABEL: {{^}}.method public static int32 'main'() cil managed{{$}}
; CODE:       {{^}}  ldsfld native int 'slot'{{$}}
; CODE-NEXT:  {{^}}  ldind.i{{$}}
; CODE-NEXT:  {{^}}  ldind.i4{{$}}
; CODE:       {{^}}} // end of method 'main'{{$}}
; CODE-DAG:   {{^}}.field public static initonly native int 'counter'{{$}}
; CODE-DAG:   {{^}}.field public static initonly native int 'slot'{{$}}

; The guard, first in <Module>::.cctor.
; GUARD-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; GUARD-NEXT:  {{^\{$}}
; GUARD-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; GUARD-NEXT:  {{^}}  {{ldc\.i4\.m1|ldc\.i4(\.s)? -1}}{{$}}
; GUARD-NEXT:  {{^}}  conv.i{{$}}
; GUARD-NEXT:  {{^}}  conv.u8{{$}}
; GUARD-NEXT:  {{^}}  ldc.i8 {{-1|0xFFFFFFFFFFFFFFFF}}{{$}}
; GUARD-NEXT:  {{^}}  bne.un{{(\.s)?}} [[W:[^ :]+]]{{$}}
; GUARD-NEXT:  {{^}}  {{ldc\.i4(\.| |\.s )1}}{{$}}
; GUARD-NEXT:  {{^}}  call uint8[] [mscorlib]System.BitConverter::GetBytes(int32){{$}}
; GUARD-NEXT:  {{^}}  {{ldc\.i4(\.| |\.s )0}}{{$}}
; GUARD-NEXT:  {{^}}  ldelem.u1{{$}}
; GUARD-NEXT:  {{^}}  brfalse{{(\.s)?}} [[O:[^ :]+]]{{$}}
; Then phase 1's image work.
; DATA-NEXT:   {{^}}  {{ldc\.i4(\.s)? [0-9]+|ldc\.i4\.[0-8]}}{{$}}
; DATA-NEXT:   {{^}}  conv.i{{$}}
; DATA-NEXT:   {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{$}}
; DATA:        {{^}}} // end of method '.cctor'{{$}}
; An empty module: the guard and nothing else of the program's.
; EMPTY-NOT:   AllocHGlobal
; EMPTY:       {{^}}  ret{{$}}
; EMPTY:       {{^}}} // end of method '.cctor'{{$}}
; The failure blocks.
; WFAIL:       {{^}}[[W]]:{{$}}
; WFAIL-NEXT:  {{^}}  ldstr "[[ASM]] is built for msil64-unknown-dotnet and needs a 64-bit .NET process; this process is 32-bit"{{$}}
; WFAIL-NEXT:  {{^}}  newobj instance void [mscorlib]System.PlatformNotSupportedException::.ctor(string){{$}}
; WFAIL-NEXT:  {{^}}  throw{{$}}
; WFAIL:       {{^}}} // end of method '.cctor'{{$}}
; OFAIL:       {{^}}[[O]]:{{$}}
; OFAIL-NEXT:  {{^}}  ldstr "[[ASM]] is built for msil64-unknown-dotnet and needs a little-endian .NET process"{{$}}
; OFAIL-NEXT:  {{^}}  newobj instance void [mscorlib]System.PlatformNotSupportedException::.ctor(string){{$}}
; OFAIL-NEXT:  {{^}}  throw{{$}}
; OFAIL:       {{^}}} // end of method '.cctor'{{$}}

;--- data.ll
source_filename = "portable.c"

@counter = global i32 5, align 4
@slot = global ptr @counter, align 8

define i64 @distance(ptr %a, ptr %b) {
  %ia = ptrtoint ptr %a to i64
  %ib = ptrtoint ptr %b to i64
  %d = sub i64 %ia, %ib
  ret i64 %d
}

define ptr @advance(ptr %p) {
  %q = getelementptr inbounds i8, ptr %p, i64 24
  ret ptr %q
}

define i32 @main() {
  %p = load ptr, ptr @slot, align 8
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

;--- empty.ll
source_filename = "empty.c"
