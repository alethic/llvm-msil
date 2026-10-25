; Constructors in a CIL object (ImplementationPlan.md 5.8.8 INIT_FINI and
; 5.8.10; M19; judge-c-fidelity R7): llvm.global_ctors becomes one INIT
; entry per element, in array order, never sorted by priority (msil-ld sorts
; them with the other objects' entries and section slots, 5.9.9), each with
; its priority (0-65535), its function's symbol (a defined function of any
; binding) and no comdat. The object carries no initialization code at all
; (O5): no .cctor, and the constructors are ordinary methods that nothing in
; the object calls. A constructor may return a value (C allows
; '__attribute__((constructor)) int f(void)'; the caller pops it): its
; method keeps its int32 result. Two entries share priority 101 and keep
; their array order. The functions are defined in another order than the
; array, so symbol indices and entries differ.
; ctoralias.ll: an entry whose function is a function alias names the
; alias's own FUNCTION ALIAS symbol (5.8.8 INIT_FINI: a defined FUNCTION of
; this object, any binding; an alias is defined through its Aliasee), not
; its aliasee's.
; Both x86_64 triples at -O2, linux-gnu also at -O0.

; REQUIRES: llvm-cil-dump
; RUN: rm -rf %t && split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/ctors.ll -o %t/lnx.o 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/ctors.ll -o %t/win.o 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -O0 -verify-machineinstrs < %t/ctors.ll -o %t/lnx.o0.o 2>&1 \
; RUN:   | count 0
; RUN: llvm-cil-dump --link-info %t/lnx.o \
; RUN:   | FileCheck %s --check-prefixes=CHECK,LNX --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/win.o \
; RUN:   | FileCheck %s --check-prefixes=CHECK,WIN --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/lnx.o0.o \
; RUN:   | FileCheck %s --check-prefixes=CHECK,LNX --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --canonical %t/win.o \
; RUN:   | FileCheck %s --check-prefix=CANON --match-full-lines --strict-whitespace --implicit-check-not=.cctor \
; RUN:       --implicit-check-not=call --implicit-check-not=entrypoint

; The object oracle (5.12.6, as in object-form-text.ll): ilasm's object of
; the object-form text is canonically equal to llc's object, record and
; tokens included.
; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{o} =
; DEFINE: %{oracle} = %ilasm-verify -output=%{o}.ilasm.o %{o}.il && \
; DEFINE:   %{canon} %{o}.o > %{o}.canon1 && %{canon} %{o}.ilasm.o > %{o}.canon2 && diff %{o}.canon1 %{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %{o}.o > %{o}.link1 && llvm-cil-dump --link-info %{o}.ilasm.o > %{o}.link2 && \
; DEFINE:   diff %{o}.link1 %{o}.link2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/ctors.ll -o %t/lnx.il 2>&1 | count 0
; REDEFINE: %{o} = %t/lnx
; RUN: %if ilasm %{ %{oracle} %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/ctoralias.ll \
; RUN:   -o %t/ctoralias.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/ctoralias.ll \
; RUN:   -o %t/ctoralias.win.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/ctoralias.lnx.o \
; RUN:   | FileCheck %s --check-prefixes=CTA,CTA-LNX --match-full-lines --strict-whitespace
; RUN: llvm-cil-dump --link-info %t/ctoralias.win.o \
; RUN:   | FileCheck %s --check-prefixes=CTA,CTA-WIN --match-full-lines --strict-whitespace
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form -verify-machineinstrs < %t/ctoralias.ll \
; RUN:   -o %t/ctoralias.win.il 2>&1 | count 0
; REDEFINE: %{o} = %t/ctoralias.win
; RUN: %if ilasm %{ %{oracle} %}

; LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; CHECK-NEXT:Symbol 0 DATA GLOBAL DEFAULT 'order' field=0x04000001 segment=0 offset=0 size=4
; CHECK-NEXT:Symbol 1 FUNCTION LOCAL DEFAULT 'cdef' method=0x06000001
; CHECK-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT 'ret_val' method=0x06000002
; CHECK-NEXT:Symbol 3 FUNCTION WEAK DEFAULT 'c101b' method=0x06000003
; CHECK-NEXT:Symbol 4 FUNCTION LOCAL DEFAULT 'c300' method=0x06000004
; CHECK-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT 'c101' method=0x06000005
; CHECK-NEXT:Segment 0 '.bss' align=4 size=4 flags=ZERO
; CHECK-NEXT:Init priority=300 symbol=4 'c300'
; CHECK-NEXT:Init priority=101 symbol=5 'c101'
; CHECK-NEXT:Init priority=65535 symbol=1 'cdef'
; CHECK-NEXT:Init priority=101 symbol=3 'c101b'
; CHECK-NEXT:Init priority=200 symbol=2 'ret_val'
; CHECK-NOT:{{.}}

; CANON:.class '<Module>' flags=0x00000000
; CANON-NEXT:  .field 'order' native int flags=0x0036
; CANON-NEXT:  .method 'c101' void() flags=0x0016 impl=0x0000
; CANON:  .method 'c101b' void() flags=0x0016 impl=0x0000
; CANON:  .method 'c300' void() flags=0x0013 impl=0x0000
; CANON:  .method 'cdef' void() flags=0x0013 impl=0x0000
; CANON:  .method 'ret_val' int32() flags=0x0016 impl=0x0000
; CANON-NEXT:    .maxstack 1
; CANON-NEXT:    L0: ldc.i4.7
; CANON-NEXT:    L1: ret
; CANON-NEXT:module .custom {{.*}}
; CANON-NOT:{{.}}

; CTA-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; CTA-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; CTA-NEXT:Symbol 0 FUNCTION GLOBAL DEFAULT 'realinit' method=0x06000001
; CTA-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT ALIAS 'ia' aliasee=0
; CTA-NEXT:Init priority=65535 symbol=1 'ia'
; CTA-NOT:{{.}}

;--- ctors.ll
source_filename = "ctors.c"

@order = global i32 0, align 4
@llvm.global_ctors = appending global [5 x { i32, ptr, ptr }] [
  { i32, ptr, ptr } { i32 300, ptr @c300, ptr null },
  { i32, ptr, ptr } { i32 101, ptr @c101, ptr null },
  { i32, ptr, ptr } { i32 65535, ptr @cdef, ptr null },
  { i32, ptr, ptr } { i32 101, ptr @c101b, ptr null },
  { i32, ptr, ptr } { i32 200, ptr @ret_val, ptr null }
]

define internal void @cdef() {
  store i32 3, ptr @order, align 4
  ret void
}

define i32 @ret_val() {
  ret i32 7
}

define weak void @c101b() {
  store i32 2, ptr @order, align 4
  ret void
}

define internal void @c300() {
  store i32 4, ptr @order, align 4
  ret void
}

define void @c101() {
  store i32 1, ptr @order, align 4
  ret void
}

;--- ctoralias.ll
source_filename = "ctoralias.c"

@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @ia, ptr null }]

@ia = alias void (), ptr @realinit

define void @realinit() {
  ret void
}
