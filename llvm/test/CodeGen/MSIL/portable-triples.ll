; The portable triples' spelling rules in llc (ImplementationPlan.md 5.18.1
; PT5, milestone P-1a, and its section 12 row on environments and vendors):
; - An msil architecture goes only with the OS dotnet, the MSIL object format
;   and no environment component; the OS dotnet goes only with an msil
;   architecture. TargetRegistry::lookupTarget refuses every other mix with
;   the reason, so llc exits with 1 before compiling anything:
;   x86_64-unknown-dotnet, msil64-unknown-linux, msil64-unknown-dotnet-elf,
;   msil64-unknown-dotnet-gnu and msil64-unknown-dotnet-musl. The object
;   format suffix of msil64-unknown-dotnet-msil is not an environment and
;   stays accepted (portable-basic.ll).
; - Another vendor is accepted and treated as unknown: msil64-pc-dotnet
;   compiles, and the texts it forms name msil64-unknown-dotnet, among them
;   T-P10's guard messages (5.18.7 PE5 and PE6) at -O0 and -O2 and the
;   link-info record's triple key (5.18.1 PT8). The module header keeps the
;   triple as given.
; - A fourth component that names no environment ("unknown") is dropped by
;   normalization (section 12, P-1a): msil64-unknown-dotnet-unknown compiles
;   as msil64-unknown-dotnet, and no text, the module header and the guard
;   messages included, keeps 'dotnet-unknown'.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{nocrash} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"

; The refusals (PT5).
; RUN: not llc -mtriple=msil64-unknown-dotnet-gnu < %t/plain.ll -o %t/gnu.il 2>&1 \
; RUN:   | FileCheck %s --check-prefix=GNU %{nocrash}
; RUN: not llc -mtriple=msil64-unknown-dotnet-gnu -filetype=obj < %t/plain.ll -o %t/gnu.o 2>&1 \
; RUN:   | FileCheck %s --check-prefix=GNU %{nocrash}
; RUN: not llc -mtriple=msil64-unknown-dotnet-musl < %t/plain.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=MUSL %{nocrash}
; RUN: not llc -mtriple=msil32-unknown-dotnet-gnu < %t/plain.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=GNU32 %{nocrash}
; RUN: not llc -mtriple=x86_64-unknown-dotnet < %t/plain.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=X64 %{nocrash}
; RUN: not llc -mtriple=msil64-unknown-linux < %t/plain.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=LINUX %{nocrash}
; RUN: not llc -mtriple=msil64-unknown-dotnet-elf < %t/plain.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=ELF %{nocrash}
; GNU:   {{^}}{{.*}}error: unable to get target for 'msil64-unknown-dotnet-gnu': the architecture 'msil64' with the OS 'dotnet' takes no environment, not 'gnu' (msil64-unknown-dotnet){{$}}
; MUSL:  {{^}}{{.*}}error: unable to get target for 'msil64-unknown-dotnet-musl': the architecture 'msil64' with the OS 'dotnet' takes no environment, not 'musl' (msil64-unknown-dotnet){{$}}
; GNU32: {{^}}{{.*}}error: unable to get target for 'msil32-unknown-dotnet-gnu': the architecture 'msil32' with the OS 'dotnet' takes no environment, not 'gnu' (msil32-unknown-dotnet){{$}}
; X64:   {{^}}{{.*}}error: unable to get target for 'x86_64-unknown-dotnet': the OS 'dotnet' needs the architecture msil64 or msil32 (msil64-unknown-dotnet){{$}}
; LINUX: {{^}}{{.*}}error: unable to get target for 'msil64-unknown-linux': the architecture 'msil64' needs the OS 'dotnet' (msil64-unknown-dotnet){{$}}
; ELF:   {{^}}{{.*}}error: unable to get target for 'msil64-unknown-dotnet-elf': the architecture 'msil64' produces only MSIL, not the 'elf' object format{{$}}

; Another vendor: the guard messages and the record key name the unknown
; vendor.
; DEFINE: %{pc} = llc -mtriple=msil64-pc-dotnet -asm-verbose=false -verify-machineinstrs
; RUN: %{pc} -O2 < %t/plain.ll -o %t/pc.o2.il 2>&1 | count 0
; RUN: %{pc} -O0 < %t/plain.ll -o %t/pc.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=PC --input-file=%t/pc.o2.il --implicit-check-not="built for msil64-pc"
; RUN: FileCheck %s --check-prefix=PC --input-file=%t/pc.o0.il --implicit-check-not="built for msil64-pc"
; PC:      {{^}}// target triple   = "msil64-pc-dotnet"{{$}}
; PC:      {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; PC-DAG:  {{^}}  ldstr "plain is built for msil64-unknown-dotnet and needs a 64-bit .NET process; this process is 32-bit"{{$}}
; PC-DAG:  {{^}}  ldstr "plain is built for msil64-unknown-dotnet and needs a little-endian .NET process"{{$}}
; PC:      {{^}}} // end of method '.cctor'{{$}}
; RUN: %if llvm-cil-dump %{ %{pc} -filetype=obj < %t/plain.ll -o %t/pc.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/pc.o | FileCheck %s --check-prefix=PCLINK %}
; PCLINK: {{^}}LinkInfo v1.0 triple=msil64-unknown-dotnet abi={{[0-9]+}} abiflags=0x{{[0-9A-F]+}} features=0x{{[0-9A-F]+}} datamodel=8,8,4,8{{$}}

; An "unknown" environment: llc normalizes it away, so the module header
; and T-P10's guard messages name msil64-unknown-dotnet.
; DEFINE: %{unk} = llc -mtriple=msil64-unknown-dotnet-unknown -asm-verbose=false -verify-machineinstrs
; RUN: %{unk} -O2 < %t/plain.ll -o %t/unk.o2.il 2>&1 | count 0
; RUN: %{unk} -O0 < %t/plain.ll -o %t/unk.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=UNK --input-file=%t/unk.o2.il --implicit-check-not=dotnet-unknown
; RUN: FileCheck %s --check-prefix=UNK --input-file=%t/unk.o0.il --implicit-check-not=dotnet-unknown
; UNK:      {{^}}// target triple   = "msil64-unknown-dotnet"{{$}}
; UNK:      {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; UNK-DAG:  {{^}}  ldstr "plain is built for msil64-unknown-dotnet and needs a 64-bit .NET process; this process is 32-bit"{{$}}
; UNK-DAG:  {{^}}  ldstr "plain is built for msil64-unknown-dotnet and needs a little-endian .NET process"{{$}}
; UNK:      {{^}}} // end of method '.cctor'{{$}}
; RUN: %if llvm-cil-dump %{ %{unk} -filetype=obj < %t/plain.ll -o %t/unk.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/unk.o | FileCheck %s --check-prefix=PCLINK %}

;--- plain.ll
source_filename = "plain.c"

define i32 @plain(i32 %x) {
  %r = add i32 %x, 1
  ret i32 %r
}
