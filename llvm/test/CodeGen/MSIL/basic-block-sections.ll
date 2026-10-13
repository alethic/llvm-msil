; Basic block sections are ignored for MSIL: CIL methods have no sections, so
; the MSILTargetMachine constructor resets the option to None, silently like
; the exception model, whatever llc (--basic-block-sections=all or a profile
; file) or cc1 (-fbasic-block-sections=) passes (decision D8, amendment log
; 2026-10-13 'D8 amended'). It has to happen there: TargetPassConfig builds the
; machine pipeline from the option, and for a profile file adds passes that
; require it, so before the reset 'all' and a profile file that puts a block
; in a cluster both crashed the MSIL AsmPrinter on a null section. Each run
; exits 0 with no diagnostic and prints the same method as a run without the
; option. A profile file that names only the function never crashed, so the
; profiles below put the entry block in a cluster, in both formats. The basic
; block address map (--basic-block-address-map) is warned about by
; MSILPrepareIR instead (stack-protector-warning.ll).

; RUN: split-file %s %t
; DEFINE: %{check} = FileCheck %s --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not="LLVM ERROR" --implicit-check-not=warning: \
; DEFINE:   --implicit-check-not=error:

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/leaf.ll 2>&1 | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   --basic-block-sections=all < %t/leaf.ll 2>&1 | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   --basic-block-sections=%t/v0.txt < %t/leaf.ll 2>&1 | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   --basic-block-sections=%t/v1.txt < %t/leaf.ll 2>&1 | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/leaf.ll 2>&1 | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   --basic-block-sections=all < %t/leaf.ll 2>&1 | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   --basic-block-sections=%t/v0.txt < %t/leaf.ll 2>&1 | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   --basic-block-sections=%t/v1.txt < %t/leaf.ll 2>&1 | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs \
; RUN:   --basic-block-sections=all < %t/leaf.ll 2>&1 | %{check}

; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil --basic-block-sections=all \
; RUN:   < %t/leaf.ll -o %t/all.il && %ilasm-verify -output=%t/all.dll %t/all.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil --basic-block-sections=%t/v1.txt \
; RUN:   < %t/leaf.ll -o %t/v1.il && %ilasm-verify -output=%t/v1.dll %t/v1.il %}

; CHECK-LABEL: .method {{.*}}'f'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f'{{$}}
; CHECK-NOT:   .method

;--- leaf.ll
source_filename = "basic-block-sections.c"

define i32 @f(i32 %a) {
  ret i32 %a
}

;--- v0.txt
!f
!!0
;--- v1.txt
v1
f f
c 0
