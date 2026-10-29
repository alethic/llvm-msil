target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc-msil"
$__msil_popcount32 = comdat any
define linkonce_odr hidden i32 @__msil_popcount32(i32 %x) comdat { %r = call i32 @llvm.ctpop.i32(i32 %x) ret i32 %r }
declare i32 @llvm.ctpop.i32(i32)
declare i32 @f2(i32)
define i32 @main() { %a = call i32 @__msil_popcount32(i32 255) %b = call i32 @f2(i32 7) %c = add i32 %a, %b ret i32 %c }
