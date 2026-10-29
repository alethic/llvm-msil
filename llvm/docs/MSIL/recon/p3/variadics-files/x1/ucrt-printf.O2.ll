source_filename = "call_printf.c"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.51.36260-msil"
$printf = comdat any
$__local_stdio_printf_options = comdat any
$"??_C@_06IMOLFEOG@?$CFd?5?$CFf?6?$AA@" = comdat any
@"??_C@_06IMOLFEOG@?$CFd?5?$CFf?6?$AA@" = linkonce_odr unnamed_addr constant [7 x i8] c"%d %f\0A\00", comdat, align 4
@__local_stdio_printf_options._OptionsStorage = internal global i64 0, align 8
define noundef i32 @main() local_unnamed_addr #0 {
entry:
  %call = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @"??_C@_06IMOLFEOG@?$CFd?5?$CFf?6?$AA@", i32 noundef 42, double noundef 2.500000e+00)
  ret i32 0
}
define linkonce_odr i32 @printf(ptr noundef %_Format, ...) local_unnamed_addr #1 comdat {
entry:
  %_ArgList = alloca ptr, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %_ArgList) #6
  call void @llvm.va_start.p0(ptr nonnull %_ArgList)
  %0 = load ptr, ptr %_ArgList, align 8
  %call = call ptr @__acrt_iob_func(i32 noundef 1) #6
  %call.i = call ptr @__local_stdio_printf_options(), !inline_history !5
  %1 = load i64, ptr %call.i, align 8
  %call1.i = call i32 @__stdio_common_vfprintf(i64 noundef %1, ptr noundef %call, ptr noundef %_Format, ptr noundef null, ptr noundef %0) #6, !inline_history !5
  call void @llvm.va_end.p0(ptr nonnull %_ArgList)
  call void @llvm.lifetime.end.p0(ptr nonnull %_ArgList) #6
  ret i32 %call1.i
}
declare void @llvm.lifetime.start.p0(ptr captures(none)) #2
declare void @llvm.va_start.p0(ptr) #3
declare void @llvm.va_end.p0(ptr) #3
declare void @llvm.lifetime.end.p0(ptr captures(none)) #2
define linkonce_odr ptr @__local_stdio_printf_options() local_unnamed_addr #4 comdat {
entry:
  ret ptr @__local_stdio_printf_options._OptionsStorage
}
declare ptr @__acrt_iob_func(i32 noundef) local_unnamed_addr #5
declare i32 @__stdio_common_vfprintf(i64 noundef, ptr noundef, ptr noundef, ptr noundef, ptr noundef) local_unnamed_addr #5
