; "zero-call-used-regs" (clang -fzero-call-used-regs=, which x86 and AArch64
; accept, and __attribute__((zero_call_used_regs(...)))) asks for registers to
; be zeroed before each return. A CIL method has no registers, and PEI's
; insertZeroCallUsedRegs would take the virtual registers that RETURN reads for
; physical ones and assert (Reg.isPhysical() for "all", 'Out-of-bounds Bit
; access' for "used" and "used-gpr"). MSILPrepareIR removes the attribute
; (amendment log 2026-10-13, review-r1 finding 3; M2.a item (5) extended, as
; for decision D25): with one warning for each compiled definition whose value
; is not "skip", and silently for "skip" and for declarations, which clang
; gives the -fzero-call-used-regs value too. The module still compiles: exit
; code 0, no error and no crash, at -O2 and -O0, and the method bodies have no
; zeroing code.

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.lnx.il 2>%t.lnx.err
; RUN: FileCheck %s --check-prefix=WARN --input-file=%t.lnx.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --input-file=%t.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.win.il 2>%t.win.err
; RUN: FileCheck %s --check-prefix=WARN --input-file=%t.win.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --input-file=%t.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s -o %t.lnx.o0.il \
; RUN:   2>%t.lnx.o0.err
; RUN: FileCheck %s --check-prefix=WARN --input-file=%t.lnx.o0.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --input-file=%t.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s -o %t.win.o0.il \
; RUN:   2>%t.win.o0.err
; RUN: FileCheck %s --check-prefix=WARN --input-file=%t.win.o0.err --implicit-check-not=warning: \
; RUN:   --implicit-check-not=error: --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: FileCheck %s --input-file=%t.win.o0.il

; One warning for each definition whose value is not "skip", in module order.
; WARN: warning: {{.*}}in function zcur_all i32 (i32): zero-call-used-regs is not supported for MSIL; attribute ignored
; WARN: warning: {{.*}}in function zcur_used i32 (i32): zero-call-used-regs is not supported for MSIL; attribute ignored
; WARN: warning: {{.*}}in function zcur_used_gpr i32 (i32): zero-call-used-regs is not supported for MSIL; attribute ignored

; After MSILPrepareIR the attribute is gone from every function, declarations
; included; the other attributes stay.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %s -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=IR --implicit-check-not=zero-call-used-regs
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=msil-prepare-ir < %s -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=IR --implicit-check-not=zero-call-used-regs
; IR: define i32 @zcur_all(i32 %x) #[[ATTRS:[0-9]+]]
; IR: define i32 @zcur_used(i32 %x) #[[ATTRS]]
; IR: define i32 @zcur_used_gpr(i32 %x) #[[ATTRS]]
; IR: define i32 @zcur_skip(i32 %x) #[[ATTRS]]
; IR: declare i32 @zcur_decl(i32) #[[ATTRS]]
; IR: attributes #[[ATTRS]] = { nounwind }

; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.il \
; RUN:   && %ilasm-verify -output=%t.dll %t.il %}

source_filename = "zcur.c"

; CHECK-LABEL: .method {{.*}}'zcur_all'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NOT:   {{^ *[^/ ]}}
; CHECK:       {{^}}} // end of method 'zcur_all'{{$}}
define i32 @zcur_all(i32 %x) #0 {
  ret i32 %x
}

; CHECK-LABEL: .method {{.*}}'zcur_used'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NOT:   {{^ *[^/ ]}}
; CHECK:       {{^}}} // end of method 'zcur_used'{{$}}
define i32 @zcur_used(i32 %x) #1 {
  ret i32 %x
}

; CHECK-LABEL: .method {{.*}}'zcur_used_gpr'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NOT:   {{^ *[^/ ]}}
; CHECK:       {{^}}} // end of method 'zcur_used_gpr'{{$}}
define i32 @zcur_used_gpr(i32 %x) #2 {
  ret i32 %x
}

; CHECK-LABEL: .method {{.*}}'zcur_skip'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NOT:   {{^ *[^/ ]}}
; CHECK:       {{^}}} // end of method 'zcur_skip'{{$}}
define i32 @zcur_skip(i32 %x) #3 {
  ret i32 %x
}

; A declaration is not compiled: no warning and no method.
; CHECK-NOT: zcur_decl
declare i32 @zcur_decl(i32) #0

attributes #0 = { nounwind "zero-call-used-regs"="all" }
attributes #1 = { nounwind "zero-call-used-regs"="used" }
attributes #2 = { nounwind "zero-call-used-regs"="used-gpr" }
attributes #3 = { nounwind "zero-call-used-regs"="skip" }
