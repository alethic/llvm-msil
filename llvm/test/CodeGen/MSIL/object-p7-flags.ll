; Matrix case p7-flags (ImplementationPlan.md 5.9.16; 5.8.8 SIGNATURES,
; SymbolFlags and LINKER_OPTIONS; 5.10.6; M19): what a CIL object must say
; about undefined functions and linker directives. The input is clang's IR
; (HEAD clang, -std=gnu17 -O2, datalayout and triple removed) of
; recon/p2/judge-c-fidelity-files/probes/p7.c as 5.9.16 defines the case:
; without the call ext_var("x", 4), whose variadic call is D19's error
; (object-errors.ll), and, on x86_64-pc-windows-msvc-msil only, with
; '__declspec(dllimport) int ext_dll(int);' called, so that DLLIMPORT is
; exercised; the ELF triple gets the same file without ext_dll:
;   #pragma comment(lib, "m")
;   int ext_proto(int, double);
;   int ext_noproto();
;   struct big { long a, b, c; };
;   struct big ext_sret(int);
;   int ext_byval(struct big);
;   extern int ext_data;
;   __declspec(dllimport) int ext_dll(int);          // windows-msvc only
;   int use(void) {
;     struct big s = ext_sret(1);
;     return ext_proto(1, 2.0) + ext_noproto(3) + ext_byval(s) + ext_data
;            + ext_dll(5);                           // windows-msvc only
;   }
; Expected (5.8.8): every undefined function is a FUNCTION UNDEFINED symbol
; and ext_data is DATA UNDEFINED; SIGNATURES has NO_PROTOTYPE on
; ext_noproto's MemberRef and SRET on ext_sret's (the returned aggregate:
; 12 bytes, align 4 on windows-msvc where long is 4 bytes; 24 bytes, align 8
; on linux-gnu), and on linux-gnu byval=0:24:8 on ext_byval's (windows-msvc
; passes the aggregate by reference, so its call has no byval and no entry);
; ext_dll is DLLIMPORT; LINKER_OPTIONS holds the IR's metadata as clang
; wrote it (5.10.6): '/DEFAULTLIB:m.lib' on windows-msvc, '-lm' on linux-gnu
; (an unquoted -l for MSIL triples whose OS is not Windows; section 12, M15's
; #pragma comment(lib) row). The SIGNATURES order is not fixed by 5.8.8, so
; the entries are matched in any order and counted; nothing else (no
; Segment, Reloc, Comdat or Init record) may stand between the symbols and
; the first entry, or between the last entry and the linker option. The
; objects also pass the object oracle (object-form text through ilasm, as in
; object-form-text.ll). Both triples at -O2, windows-msvc also at -O0.

; REQUIRES: llvm-cil-dump
; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{full} = --match-full-lines --strict-whitespace
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/p7-win.ll \
; RUN:   -o %t/win.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/win.o > %t/win.link
; RUN: FileCheck %s %{full} --check-prefixes=CHECK,WIN --input-file=%t/win.link
; RUN: FileCheck %s %{full} --check-prefix=WIN-SIGS --input-file=%t/win.link
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/p7-win.ll \
; RUN:   -o %t/win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/win.o0.o > %t/win.o0.link
; RUN: FileCheck %s %{full} --check-prefixes=CHECK,WIN --input-file=%t/win.o0.link
; RUN: FileCheck %s %{full} --check-prefix=WIN-SIGS --input-file=%t/win.o0.link
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/p7-lnx.ll \
; RUN:   -o %t/lnx.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/lnx.o > %t/lnx.link
; RUN: FileCheck %s %{full} --check-prefixes=CHECK,LNX --input-file=%t/lnx.link
; RUN: FileCheck %s %{full} --check-prefix=LNX-SIGS --input-file=%t/lnx.link

; The tokens of the SIGNATURES entries are the MemberRefs of the calls.
; RUN: llvm-cil-dump --tables %t/win.o | FileCheck %s %{full} --check-prefixes=TBL,TBL-WIN
; RUN: llvm-cil-dump --tables %t/lnx.o | FileCheck %s %{full} --check-prefix=TBL

; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{o} =
; DEFINE: %{oracle} = %ilasm-verify -output=%{o}.ilasm.o %{o}.il && \
; DEFINE:   %{canon} %{o}.o > %{o}.canon1 && %{canon} %{o}.ilasm.o > %{o}.canon2 && diff %{o}.canon1 %{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %{o}.o > %{o}.link1 && llvm-cil-dump --link-info %{o}.ilasm.o > %{o}.link2 && \
; DEFINE:   diff %{o}.link1 %{o}.link2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form -verify-machineinstrs < %t/p7-win.ll \
; RUN:   -o %t/win.il 2>&1 | count 0
; REDEFINE: %{o} = %t/win
; RUN: %if ilasm %{ %{oracle} %}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/p7-lnx.ll \
; RUN:   -o %t/lnx.il 2>&1 | count 0
; REDEFINE: %{o} = %t/lnx
; RUN: %if ilasm %{ %{oracle} %}

; WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; CHECK-NEXT:Symbol 0 DATA GLOBAL DEFAULT UNDEFINED 'ext_data'
; CHECK-NEXT:Symbol 1 FUNCTION GLOBAL DEFAULT 'use' method=0x06000001
; CHECK-NEXT:Symbol 2 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_sret'
; CHECK-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_proto'
; CHECK-NEXT:Symbol 4 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_noproto'
; CHECK-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT UNDEFINED 'ext_byval'
; WIN-NEXT:Symbol 6 FUNCTION GLOBAL DEFAULT UNDEFINED DLLIMPORT 'ext_dll'
; CHECK-NOT:{{.}}
; WIN-DAG:Signature 0x0A{{([0-9A-F]{6})}} 'ext_sret' SRET size=12 align=4
; LNX-DAG:Signature 0x0A{{([0-9A-F]{6})}} 'ext_sret' SRET size=24 align=8
; CHECK-DAG:Signature 0x0A{{([0-9A-F]{6})}} 'ext_noproto' NO_PROTOTYPE
; LNX-DAG:Signature 0x0A{{([0-9A-F]{6})}} 'ext_byval' byval=0:24:8
; CHECK-NOT:{{.}}
; WIN:LinkerOption LINKER_OPTION '/DEFAULTLIB:m.lib'
; LNX:LinkerOption LINKER_OPTION '-lm'
; CHECK-NOT:{{.}}

; WIN-SIGS-COUNT-2:{{^Signature .*}}
; WIN-SIGS-NOT:{{^Signature }}
; LNX-SIGS-COUNT-3:{{^Signature .*}}
; LNX-SIGS-NOT:{{^Signature }}

; TBL:Table: MemberRef rows={{[0-9]+}} rowsize={{[0-9]+}}
; TBL-DAG:  MemberRef[{{[0-9]+}}]: Class=0x01000001 Name='ext_sret' Signature=00 02 01 18 08
; TBL-DAG:  MemberRef[{{[0-9]+}}]: Class=0x01000001 Name='ext_proto' Signature=00 02 08 08 0D
; TBL-DAG:  MemberRef[{{[0-9]+}}]: Class=0x01000001 Name='ext_noproto' Signature=00 01 08 08
; TBL-DAG:  MemberRef[{{[0-9]+}}]: Class=0x01000001 Name='ext_byval' Signature=00 01 08 18
; TBL-DAG:  MemberRef[{{[0-9]+}}]: Class=0x01000001 Name='ext_data' Signature=06 18
; TBL-WIN-DAG:  MemberRef[{{[0-9]+}}]: Class=0x01000001 Name='ext_dll' Signature=00 01 08 08

;--- p7-win.ll
source_filename = "p7.c"

%struct.big = type { i32, i32, i32 }

@ext_data = external local_unnamed_addr global i32, align 4

define i32 @use() local_unnamed_addr #0 {
entry:
  %s = alloca %struct.big, align 4
  %byval-temp = alloca %struct.big, align 4
  call void @llvm.lifetime.start.p0(ptr nonnull %s) #4
  call void @ext_sret(ptr dead_on_unwind nonnull writable sret(%struct.big) align 4 %s, i32 noundef 1) #5
  %call = call i32 @ext_proto(i32 noundef 1, double noundef 2.000000e+00) #5
  %call1 = call i32 @ext_noproto(i32 noundef 3) #5
  %add = add i32 %call1, %call
  call void @llvm.lifetime.start.p0(ptr nonnull %byval-temp) #4
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 4 dereferenceable(12) %byval-temp, ptr noundef nonnull align 4 dereferenceable(12) %s, i64 12, i1 false)
  %call2 = call i32 @ext_byval(ptr nofreeobj noundef nonnull align 4 dead_on_return dereferenceable(12) %byval-temp) #5
  call void @llvm.lifetime.end.p0(ptr nonnull %byval-temp) #4
  %add3 = add i32 %add, %call2
  %0 = load i32, ptr @ext_data, align 4
  %add4 = add i32 %add3, %0
  %call5 = call i32 @ext_dll(i32 noundef 5) #5
  %add6 = add i32 %add4, %call5
  call void @llvm.lifetime.end.p0(ptr nonnull %s) #4
  ret i32 %add6
}

declare void @llvm.lifetime.start.p0(ptr captures(none)) #1

declare void @ext_sret(ptr dead_on_unwind writable sret(%struct.big) align 4, i32 noundef) local_unnamed_addr #2

declare i32 @ext_proto(i32 noundef, double noundef) local_unnamed_addr #2

declare i32 @ext_noproto(...) local_unnamed_addr #3

declare i32 @ext_byval(ptr nofreeobj noundef align 4 dead_on_return dereferenceable(12)) local_unnamed_addr #2

declare void @llvm.memcpy.p0.p0.i64(ptr noalias writeonly captures(none), ptr noalias readonly captures(none), i64, i1 immarg) #1

declare void @llvm.lifetime.end.p0(ptr captures(none)) #1

declare dllimport i32 @ext_dll(i32 noundef) local_unnamed_addr #2

attributes #0 = { nounwind "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { "no-builtins" "no-prototype" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nounwind }
attributes #5 = { nobuiltin nounwind "no-builtins" }

!llvm.linker.options = !{!0}
!0 = !{!"/DEFAULTLIB:m.lib"}

;--- p7-lnx.ll
source_filename = "p7.c"

%struct.big = type { i64, i64, i64 }

@ext_data = external local_unnamed_addr global i32, align 4

define i32 @use() local_unnamed_addr #0 {
entry:
  %s = alloca %struct.big, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %s) #4
  call void @ext_sret(ptr dead_on_unwind nonnull writable sret(%struct.big) align 8 %s, i32 noundef 1) #5
  %call = call i32 @ext_proto(i32 noundef 1, double noundef 2.000000e+00) #5
  %call1 = call i32 @ext_noproto(i32 noundef 3) #5
  %add = add nsw i32 %call1, %call
  %call2 = call i32 @ext_byval(ptr noundef nonnull byval(%struct.big) align 8 %s) #5
  %add3 = add nsw i32 %add, %call2
  %0 = load i32, ptr @ext_data, align 4
  %add4 = add nsw i32 %add3, %0
  call void @llvm.lifetime.end.p0(ptr nonnull %s) #4
  ret i32 %add4
}

declare void @llvm.lifetime.start.p0(ptr captures(none)) #1

declare void @ext_sret(ptr dead_on_unwind writable sret(%struct.big) align 8, i32 noundef) local_unnamed_addr #2

declare i32 @ext_proto(i32 noundef, double noundef) local_unnamed_addr #2

declare i32 @ext_noproto(...) local_unnamed_addr #3

declare i32 @ext_byval(ptr noundef byval(%struct.big) align 8) local_unnamed_addr #2

declare void @llvm.lifetime.end.p0(ptr captures(none)) #1

attributes #0 = { nounwind "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { "no-builtins" "no-prototype" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nounwind }
attributes #5 = { nobuiltin nounwind "no-builtins" }

!llvm.linker.options = !{!0}
!0 = !{!"-lm"}
