; clang attaches the native CPU, tuning CPU and feature strings of the native
; ABI to every function it emits for an MSIL triple, and llc users pass -mcpu
; and -mattr. The MSIL target has one processor, 'generic', and no features
; (ImplementationPlan.md decision D7): every such string is accepted and
; ignored without a 'not a recognized processor/feature' warning, on the x86_64
; and aarch64 triples, for functions that are compiled to methods (M2 could
; only check declarations). llc's stderr is therefore empty, and the functions
; still compile and assemble.

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %s -o %t.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --input-file=%t.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %s -o %t.win.il 2>&1 | count 0
; RUN: FileCheck %s --input-file=%t.win.il
; RUN: llc -mtriple=x86_64-w64-windows-gnu-msil -verify-machineinstrs < %s -o %t.mingw.il 2>&1 | count 0
; RUN: FileCheck %s --input-file=%t.mingw.il
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -verify-machineinstrs < %s -o %t.a64.il 2>&1 | count 0
; RUN: FileCheck %s --input-file=%t.a64.il
; RUN: llc -mtriple=aarch64-pc-windows-msvc-msil -verify-machineinstrs < %s -o %t.a64win.il 2>&1 | count 0
; RUN: FileCheck %s --input-file=%t.a64win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %s -o %t.o0.il 2>&1 | count 0
; RUN: FileCheck %s --input-file=%t.o0.il

; Command-line CPUs and features, including ones of the other architecture.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -mcpu=x86-64-v3 -mattr=+avx2,+bmi2,-sse4a \
; RUN:   -verify-machineinstrs < %s -o %t.mcpu.il 2>&1 | count 0
; RUN: FileCheck %s --input-file=%t.mcpu.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -mcpu=skylake-avx512 -mattr=+avx512f \
; RUN:   -verify-machineinstrs < %s -o %t.mcpu2.il 2>&1 | count 0
; RUN: FileCheck %s --input-file=%t.mcpu2.il
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -mcpu=cortex-a72 -mattr=+neon,+crc \
; RUN:   -verify-machineinstrs < %s -o %t.mcpu3.il 2>&1 | count 0
; RUN: FileCheck %s --input-file=%t.mcpu3.il
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -mcpu=x86-64 -mattr=+sse2 \
; RUN:   -verify-machineinstrs < %s -o %t.mcpu4.il 2>&1 | count 0
; RUN: FileCheck %s --input-file=%t.mcpu4.il

; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.asm.il \
; RUN:   && %ilasm-verify -output=%t.asm.dll %t.asm.il %}
; RUN: %if ilasm %{ llc -mtriple=aarch64-pc-windows-msvc-msil < %s -o %t.asma64.il \
; RUN:   && %ilasm-verify -output=%t.asma64.dll %t.asma64.il %}

; CHECK-LABEL: .method {{.*}}'clang_x86_64_o2'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method 'clang_x86_64_o2'{{$}}
; CHECK-LABEL: .method {{.*}}'clang_x86_64_o0'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.7{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method 'clang_x86_64_o0'{{$}}
; CHECK-LABEL: .method {{.*}}'clang_skylake'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK:       {{^}}  ldc.r8 float64(0x3FF0000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-LABEL: .method {{.*}}'clang_aarch64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-LABEL: .method {{.*}}'clang_apple_m1'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method 'clang_apple_m1'{{$}}

source_filename = "cpu-features.c"

; What clang -O2 emits for x86_64 on Linux and Windows.
define dso_local i32 @clang_x86_64_o2(i32 noundef %x) local_unnamed_addr #0 {
  ret i32 %x
}

; What clang -O0 emits (noinline optnone).
define dso_local i32 @clang_x86_64_o0() #1 {
  ret i32 7
}

; -march=skylake-avx512.
define dso_local double @clang_skylake() local_unnamed_addr #2 {
  ret double 1.0
}

; What clang emits for aarch64.
define dso_local ptr @clang_aarch64(ptr noundef readnone returned %p) local_unnamed_addr #3 {
  ret ptr %p
}

; An Apple CPU with feature removals.
define dso_local void @clang_apple_m1() local_unnamed_addr #4 {
  ret void
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { nounwind "target-cpu"="skylake-avx512" "target-features"="+adx,+aes,+avx,+avx2,+avx512bw,+avx512cd,+avx512dq,+avx512f,+avx512vl,+bmi,+bmi2,+clflushopt,+clwb,+cmov,+crc32,+cx16,+cx8,+f16c,+fma,+fsgsbase,+fxsr,+invpcid,+lzcnt,+mmx,+movbe,+pclmul,+pku,+popcnt,+prfchw,+rdrnd,+rdseed,+sahf,+sse,+sse2,+sse3,+sse4.1,+sse4.2,+ssse3,+x87,+xsave,+xsavec,+xsaveopt,+xsaves,-amx-tile,-sha" "tune-cpu"="skylake-avx512" }
attributes #3 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+fp-armv8,+neon,+outline-atomics,+v8a,-fmv" }
attributes #4 = { nounwind "target-cpu"="apple-m1" "target-features"="+aes,+crc,+dotprod,+fp-armv8,+fp16fml,+fullfp16,+lse,+neon,+ras,+rcpc,+rdm,+sha2,+sha3,+v8.5a,+zcm,+zcz,-sve" }
