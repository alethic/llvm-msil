; Object-form text (ImplementationPlan.md 5.8.1 and 5.8.4; M19): with
; -msil-object-form, llc's assembly output is the CIL object as ILAsm text
; (object mode, Q5), and ilasm turns that text into an object that is
; canonically equal to the one -filetype=obj writes (5.12.6's object oracle).
; Text shape (shape.ll): the header comment with the source file and triple;
; the three placeholder '.assembly extern' lines ('msil.extern', 'msil.bcl',
; 'msil.link', version 0:0:0:0) and no other assembly; the methods as in
; Phase 1 (R4 access) with placeholder operands ([msil.extern]'<msil>Symbols'
; calls and fields, [msil.extern]'<msil>FnAddr' addresses, [msil.bcl] BCL
; members); the address fields of the defined variables; the import stubs
; (on a module and on msil.unresolved, then the address-only stubs); and, as
; the last two lines, '.module' with the source file's base name and the
; record's carrier '.custom instance void [msil.link]'<msil>LinkInfo'::.ctor(
; uint8[]) = ( 01 00 <length> <record> 00 00 )', the record starting with
; 'MSLK' and version 1.0 (5.8.7, 5.8.8). No '.cctor', '<msil>Main',
; '.entrypoint', data image, '<msil>fn_' field or mscorlib reference.
; The oracle: under %if ilasm, the text is assembled with ilasm -dll -pe64
; -x64, and the canonical listings of the two objects, with 5.12.6's three
; object-oracle normalizations (--params=drop for the Param rows ilasm adds,
; --initlocals=ignore-without-locals for ilasm's InitLocals on bodies
; without locals, --snan=quiet for ilasm's quieting of float32 signaling NaN
; constants, which snan() has), must be equal; so must their unnormalized
; --link-info, record bytes and tokens included (R6: ilasm numbers the rows
; as the object writer does). shape.ll at -O2 and -O0 on both x86_64
; triples, and the zoo program of the object-format recon (C-derived IR of
; recon/p2/objfmt-binding-files/zoo/a.c, -fcommon, per triple) at -O2 and
; -O0.

; REQUIRES: llvm-cil-dump
; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{o} =
; DEFINE: %{oracle} = %ilasm-verify -output=%{o}.ilasm.o %{o}.il && \
; DEFINE:   %{canon} %{o}.o > %{o}.canon1 && %{canon} %{o}.ilasm.o > %{o}.canon2 && diff %{o}.canon1 %{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %{o}.o > %{o}.link1 && llvm-cil-dump --link-info %{o}.ilasm.o > %{o}.link2 && \
; DEFINE:   diff %{o}.link1 %{o}.link2

; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form -verify-machineinstrs < %t/shape.ll \
; RUN:   -o %t/shape.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=TEXT --input-file=%t/shape.win.il -DTRIPLE=x86_64-pc-windows-msvc-msil \
; RUN:   --implicit-check-not=.cctor --implicit-check-not=.entrypoint --implicit-check-not="<msil>Main" \
; RUN:   --implicit-check-not="<msil>image" --implicit-check-not="<msil>data" --implicit-check-not="<msil>fn_" \
; RUN:   --implicit-check-not=mscorlib --implicit-check-not=".class " --implicit-check-not=".data "
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/shape.ll \
; RUN:   -o %t/shape.win.o 2>&1 | count 0
; REDEFINE: %{o} = %t/shape.win
; RUN: %if ilasm %{ %{oracle} %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/shape.ll \
; RUN:   -o %t/shape.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=TEXT --input-file=%t/shape.lnx.il -DTRIPLE=x86_64-unknown-linux-gnu-msil
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/shape.ll \
; RUN:   -o %t/shape.lnx.o 2>&1 | count 0
; REDEFINE: %{o} = %t/shape.lnx
; RUN: %if ilasm %{ %{oracle} %}

; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-object-form -verify-machineinstrs < %t/shape.ll \
; RUN:   -o %t/shape.win.o0.il 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj -verify-machineinstrs < %t/shape.ll \
; RUN:   -o %t/shape.win.o0.o 2>&1 | count 0
; REDEFINE: %{o} = %t/shape.win.o0
; RUN: %if ilasm %{ %{oracle} %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -msil-object-form -verify-machineinstrs < %t/shape.ll \
; RUN:   -o %t/shape.lnx.o0.il 2>&1 | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -filetype=obj -verify-machineinstrs < %t/shape.ll \
; RUN:   -o %t/shape.lnx.o0.o 2>&1 | count 0
; REDEFINE: %{o} = %t/shape.lnx.o0
; RUN: %if ilasm %{ %{oracle} %}

; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form -verify-machineinstrs < %t/zoo-win.ll \
; RUN:   -o %t/zoo.win.il 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/zoo-win.ll \
; RUN:   -o %t/zoo.win.o 2>&1 | count 0
; REDEFINE: %{o} = %t/zoo.win
; RUN: %if ilasm %{ %{oracle} %}

; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-object-form -verify-machineinstrs < %t/zoo-win.ll \
; RUN:   -o %t/zoo.win.o0.il 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj -verify-machineinstrs < %t/zoo-win.ll \
; RUN:   -o %t/zoo.win.o0.o 2>&1 | count 0
; REDEFINE: %{o} = %t/zoo.win.o0
; RUN: %if ilasm %{ %{oracle} %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/zoo-lnx.ll \
; RUN:   -o %t/zoo.lnx.il 2>&1 | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/zoo-lnx.ll \
; RUN:   -o %t/zoo.lnx.o 2>&1 | count 0
; REDEFINE: %{o} = %t/zoo.lnx
; RUN: %if ilasm %{ %{oracle} %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -msil-object-form -verify-machineinstrs < %t/zoo-lnx.ll \
; RUN:   -o %t/zoo.lnx.o0.il 2>&1 | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -filetype=obj -verify-machineinstrs < %t/zoo-lnx.ll \
; RUN:   -o %t/zoo.lnx.o0.o 2>&1 | count 0
; REDEFINE: %{o} = %t/zoo.lnx.o0
; RUN: %if ilasm %{ %{oracle} %}

; TEXT:      {{^}}// MSIL object generated by LLVM{{$}}
; TEXT-NEXT: {{^}}// source_filename = "shape.c"{{$}}
; TEXT-NEXT: {{^}}// target triple   = "[[TRIPLE]]"{{$}}
; TEXT-NEXT: {{^}}.assembly extern 'msil.extern' { .ver 0:0:0:0 }{{$}}
; TEXT-NEXT: {{^}}.assembly extern 'msil.bcl' { .ver 0:0:0:0 }{{$}}
; TEXT-NEXT: {{^}}.assembly extern 'msil.link' { .ver 0:0:0:0 }{{$}}
; TEXT-NOT:  .assembly
; TEXT:      {{^}}.method public static int32 'twice'(int32) cil managed{{$}}
; TEXT:      {{^}}} // end of method 'twice'{{$}}
; TEXT:      {{^}}.method assembly static void 'init'() cil managed{{$}}
; TEXT:      {{^}}} // end of method 'init'{{$}}
; TEXT:      {{^}}.method public static int32 'main'() cil managed{{$}}
; TEXT-DAG:  {{^}}  call int32 [msil.extern]'<msil>Symbols'::'add3'(int32){{$}}
; TEXT-DAG:  {{^}}  ldsfld native int [msil.extern]'<msil>FnAddr'::'twice'{{$}}
; TEXT-DAG:  {{^}}  ldsfld native int [msil.extern]'<msil>Symbols'::'counter'{{$}}
; TEXT-DAG:  {{^}}  call void [msil.bcl]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; TEXT-DAG:  {{^}}  call int32 [msil.extern]'<msil>Symbols'::'knr'(int32, int32){{$}}
; TEXT-DAG:  {{^}}  call int32 'tick'(){{$}}
; TEXT-DAG:  {{^}}  call int32 'my_labs'(int32){{$}}
; TEXT-DAG:  {{^}}  ldsfld native int 'lo_data'{{$}}
; TEXT-DAG:  {{^}}  ldsfld native int 'hidden_count'{{$}}
; TEXT-DAG:  {{^}}  calli int32(int32){{$}}
; TEXT:      {{^}}} // end of method 'main'{{$}}
; TEXT:      {{^}}.method public static native int 'addr'() cil managed{{$}}
; TEXT:      {{^}}  ldsfld native int [msil.extern]'<msil>FnAddr'::'addr_only'{{$}}
; TEXT:      {{^}}.method public static float32 'snan'() cil managed{{$}}
; TEXT:      {{^}}  ldc.r4 float32(0x7FA00000){{$}}
; TEXT:      {{^}}.field public static initonly native int 'table'{{$}}
; TEXT:      {{^}}.field assembly static initonly native int 'hidden_count'{{$}}
; TEXT:      {{^}}.field public static initonly native int 'lo_data'{{$}}
; TEXT:      {{^}}.method public hidebysig static pinvokeimpl("kernel32.dll" as "GetTickCount" nomangle cdecl) int32 'tick'() cil managed preservesig {}{{$}}
; TEXT:      {{^}}.method public hidebysig static pinvokeimpl("msil.unresolved" as "labs" nomangle cdecl) int32 'my_labs'(int32) cil managed preservesig {}{{$}}
; TEXT:      {{^}}.method public hidebysig static pinvokeimpl("msil.unresolved" as "addr_only" nomangle cdecl) int32 'addr_only'(int32) cil managed preservesig {}{{$}}
; TEXT:      {{^}}.module 'shape.c'{{$}}
; TEXT-NEXT: {{^}}.custom instance void [msil.link]'<msil>LinkInfo'::.ctor(uint8[]) = ( 01 00 {{(([0-9A-Fa-f]{2} ){4})}}4D 53 4C 4B 01 00 {{([0-9A-Fa-f]{2} )+}}00 00 ){{$}}
; TEXT-NOT:  {{.}}

;--- shape.ll
source_filename = "shape.c"

$lo = comdat any

@table = global [2 x ptr] [ptr @twice, ptr @add3], align 8
@hidden_count = internal global i32 0, align 4
@lo_data = linkonce_odr global i32 9, comdat($lo), align 4
@counter = external global i32, align 4
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 200, ptr @init, ptr null }]

declare i32 @add3(i32)

declare i32 @knr(...) #0

declare i32 @tick() #1

declare i32 @my_labs(i32) #2

declare i32 @addr_only(i32)

define i32 @twice(i32 %x) {
  %r = shl i32 %x, 1
  ret i32 %r
}

define internal void @init() {
  store i32 1, ptr @hidden_count, align 4
  ret void
}

define i32 @main() {
  %buf = alloca [16 x i8], align 8
  %a = call i32 @add3(i32 1)
  %fp = load ptr, ptr @table, align 8
  %same = icmp eq ptr %fp, @twice
  %s = zext i1 %same to i32
  %r = call i32 %fp(i32 %a)
  %c = load i32, ptr @counter, align 4
  call void @llvm.memmove.p0.p0.i64(ptr %buf, ptr @table, i64 16, i1 false)
  %b = load i32, ptr %buf, align 8
  %k = call i32 (i32, i32, ...) @knr(i32 %r, i32 %c)
  %t = call i32 @tick()
  %l = call i32 @my_labs(i32 %t)
  %lo = load i32, ptr @lo_data, align 4
  %h = load i32, ptr @hidden_count, align 4
  %s1 = add i32 %s, %b
  %s2 = add i32 %s1, %k
  %s3 = add i32 %s2, %l
  %s4 = add i32 %s3, %lo
  %s5 = add i32 %s4, %h
  ret i32 %s5
}

define ptr @addr() {
  ret ptr @addr_only
}

define float @snan() {
  ret float 0x7FF4000000000000
}

declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1)

attributes #0 = { "no-prototype" }
attributes #1 = { "msil-import-module"="kernel32.dll" "msil-import-name"="GetTickCount" }
attributes #2 = { "msil-import-name"="labs" }

!llvm.dependent-libraries = !{!0}
!0 = !{!"m"}

;--- zoo-win.ll
source_filename = "a.c"

$"??_C@_05CJBACGMB@hello?$AA@" = comdat any

$"??_C@_04GHJNJNPO@main?$AA@" = comdat any

@table = local_unnamed_addr global [3 x ptr] [ptr @add3, ptr @twice, ptr @alias_twice], align 8
@counter = external global i32, align 4
@pcounter = local_unnamed_addr global ptr @counter, align 8
@"??_C@_05CJBACGMB@hello?$AA@" = linkonce_odr unnamed_addr constant [6 x i8] c"hello\00", comdat, align 4
@msg = local_unnamed_addr global ptr @"??_C@_05CJBACGMB@hello?$AA@", align 8
@va = internal global i32 10, align 4
@"??_C@_04GHJNJNPO@main?$AA@" = linkonce_odr unnamed_addr constant [5 x i8] c"main\00", comdat, align 4
@local_g = internal unnamed_addr global i32 7, align 4
@common_c = common local_unnamed_addr global i32 0, align 4
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 200, ptr @ctor_a, ptr null }]

declare i32 @add3(i32 noundef) #0

declare i32 @twice(i32 noundef) #0

declare i32 @alias_twice(i32 noundef) #0

define weak i32 @weakfn() local_unnamed_addr #1 {
entry:
  ret i32 1
}

define internal void @ctor_a() #2 {
entry:
  %0 = load volatile i32, ptr @va, align 4
  %1 = load i32, ptr @counter, align 4
  %add = add i32 %1, %0
  store i32 %add, ptr @counter, align 4
  ret void
}

define range(i32 0, 256) i32 @main() local_unnamed_addr #1 {
entry:
  %call = tail call i32 @puts(ptr noundef nonnull @"??_C@_04GHJNJNPO@main?$AA@") #4
  %0 = load i32, ptr @local_g, align 4
  %inc = add i32 %0, 1
  store i32 %inc, ptr @local_g, align 4
  %1 = load ptr, ptr @table, align 8
  %call1 = tail call i32 %1(i32 noundef 1) #4
  %2 = load ptr, ptr getelementptr inbounds nuw (i8, ptr @table, i64 8), align 8
  %call2 = tail call i32 %2(i32 noundef 2) #4
  %add3 = add i32 %call2, %call1
  %3 = load ptr, ptr getelementptr inbounds nuw (i8, ptr @table, i64 16), align 8
  %call4 = tail call i32 %3(i32 noundef 3) #4
  %add5 = add i32 %add3, %call4
  %4 = load ptr, ptr @pcounter, align 8
  %5 = load i32, ptr %4, align 4
  %add6 = add i32 %add5, %5
  %6 = load i32, ptr @local_g, align 4
  %call7 = tail call fastcc i32 @helper(i32 noundef %6) #5
  %add8 = add i32 %add6, %call7
  %call9 = tail call i32 @weakfn() #5
  %mul = mul i32 %call9, 1000
  %add10 = add i32 %add8, %mul
  %tobool.not = icmp eq ptr @maybe, null
  %add11 = add i32 %add10, 100000
  %spec.select = select i1 %tobool.not, i32 %add10, i32 %add11
  %7 = load i32, ptr @common_c, align 4
  %add12 = add i32 %7, 1
  store i32 %add12, ptr @common_c, align 4
  %add13 = add i32 %spec.select, %add12
  %8 = load ptr, ptr getelementptr inbounds nuw (i8, ptr @table, i64 8), align 8
  %9 = load ptr, ptr getelementptr inbounds nuw (i8, ptr @table, i64 16), align 8
  %cmp = icmp eq ptr %8, %9
  %cond = select i1 %cmp, i32 0, i32 7
  %add14 = add i32 %add13, %cond
  %10 = load ptr, ptr @msg, align 8
  %11 = load i8, ptr %10, align 1
  %conv = zext i8 %11 to i32
  %add15 = add i32 %add14, %conv
  %call16 = tail call i32 @b_extra() #4
  %add17 = add i32 %add15, %call16
  %and = and i32 %add17, 255
  ret i32 %and
}

declare i32 @puts(ptr noundef) local_unnamed_addr #0

define internal fastcc noundef i32 @helper(i32 noundef %x) unnamed_addr #3 {
entry:
  %add = add i32 %x, 100
  ret i32 %add
}

declare extern_weak i32 @maybe() #0

declare i32 @b_extra() local_unnamed_addr #0

attributes #0 = { "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nounwind "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { nofree norecurse nosync nounwind memory(readwrite, argmem: none, target_mem: none) "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { mustprogress nofree noinline norecurse nosync nounwind willreturn memory(none) "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nobuiltin nounwind "no-builtins" }
attributes #5 = { nobuiltin "no-builtins" }

;--- zoo-lnx.ll
source_filename = "a.c"

@table = local_unnamed_addr global [3 x ptr] [ptr @add3, ptr @twice, ptr @alias_twice], align 16
@counter = external global i32, align 4
@pcounter = local_unnamed_addr global ptr @counter, align 8
@.str = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@msg = local_unnamed_addr global ptr @.str, align 8
@va = internal global i32 10, align 4
@.str.1 = private unnamed_addr constant [5 x i8] c"main\00", align 1
@local_g = internal unnamed_addr global i32 7, align 4
@common_c = common local_unnamed_addr global i32 0, align 4
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 200, ptr @ctor_a, ptr null }]

declare i32 @add3(i32 noundef) #0

declare i32 @twice(i32 noundef) #0

declare i32 @alias_twice(i32 noundef) #0

define weak i32 @weakfn() local_unnamed_addr #1 {
entry:
  ret i32 1
}

define internal void @ctor_a() #2 {
entry:
  %0 = load volatile i32, ptr @va, align 4
  %1 = load i32, ptr @counter, align 4
  %add = add nsw i32 %1, %0
  store i32 %add, ptr @counter, align 4
  ret void
}

define range(i32 0, 256) i32 @main() local_unnamed_addr #1 {
entry:
  %call = tail call i32 @puts(ptr noundef nonnull @.str.1) #4
  %0 = load i32, ptr @local_g, align 4
  %inc = add nsw i32 %0, 1
  store i32 %inc, ptr @local_g, align 4
  %1 = load ptr, ptr @table, align 16
  %call1 = tail call i32 %1(i32 noundef 1) #4
  %2 = load ptr, ptr getelementptr inbounds nuw (i8, ptr @table, i64 8), align 8
  %call2 = tail call i32 %2(i32 noundef 2) #4
  %add3 = add nsw i32 %call2, %call1
  %3 = load ptr, ptr getelementptr inbounds nuw (i8, ptr @table, i64 16), align 16
  %call4 = tail call i32 %3(i32 noundef 3) #4
  %add5 = add nsw i32 %add3, %call4
  %4 = load ptr, ptr @pcounter, align 8
  %5 = load i32, ptr %4, align 4
  %add6 = add nsw i32 %add5, %5
  %6 = load i32, ptr @local_g, align 4
  %call7 = tail call fastcc i32 @helper(i32 noundef %6) #5
  %add8 = add nsw i32 %add6, %call7
  %call9 = tail call i32 @weakfn() #5
  %mul = mul nsw i32 %call9, 1000
  %add10 = add nsw i32 %add8, %mul
  %tobool.not = icmp eq ptr @maybe, null
  %add11 = add nsw i32 %add10, 100000
  %spec.select = select i1 %tobool.not, i32 %add10, i32 %add11
  %7 = load i32, ptr @common_c, align 4
  %add12 = add nsw i32 %7, 1
  store i32 %add12, ptr @common_c, align 4
  %add13 = add nsw i32 %spec.select, %add12
  %8 = load ptr, ptr getelementptr inbounds nuw (i8, ptr @table, i64 8), align 8
  %9 = load ptr, ptr getelementptr inbounds nuw (i8, ptr @table, i64 16), align 16
  %cmp = icmp eq ptr %8, %9
  %cond = select i1 %cmp, i32 0, i32 7
  %add14 = add nsw i32 %add13, %cond
  %10 = load ptr, ptr @msg, align 8
  %11 = load i8, ptr %10, align 1
  %conv = zext i8 %11 to i32
  %add15 = add i32 %add14, %conv
  %call16 = tail call i32 @b_extra() #4
  %add17 = add i32 %add15, %call16
  %and = and i32 %add17, 255
  ret i32 %and
}

declare i32 @puts(ptr noundef) local_unnamed_addr #0

define internal fastcc range(i32 -2147483547, -2147483648) i32 @helper(i32 noundef range(i32 -2147483647, -2147483648) %x) unnamed_addr #3 {
entry:
  %add = add nsw i32 %x, 100
  ret i32 %add
}

declare extern_weak i32 @maybe() #0

declare i32 @b_extra() local_unnamed_addr #0

attributes #0 = { "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nounwind "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { nofree norecurse nosync nounwind memory(readwrite, argmem: none, target_mem: none) "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { mustprogress nofree noinline norecurse nosync nounwind willreturn memory(none) "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nobuiltin nounwind "no-builtins" }
attributes #5 = { nobuiltin "no-builtins" }
