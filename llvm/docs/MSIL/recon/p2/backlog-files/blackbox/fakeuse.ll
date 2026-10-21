define i32 @f(i32 %x) {
  %p = alloca i32
  store i32 %x, ptr %p
  fence syncscope("singlethread") seq_cst
  call void (...) @llvm.fake.use(ptr %p)
  %m = mul i32 %x, 7
  call void (...) @llvm.fake.use(i32 %m)
  ret i32 0
}
declare void @llvm.fake.use(...)
