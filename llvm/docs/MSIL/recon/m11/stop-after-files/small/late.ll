define i64 @rotr_i64(i64 %a, i64 %b) {
  %r = call i64 @llvm.fshr.i64(i64 %a, i64 %a, i64 %b)
  ret i64 %r
}
define i32 @sel(i1 %c, i32 %a, i32 %b) {
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}
define i64 @mix(i32 %a, i64 %b) {
  %x = zext i32 %a to i64
  %y = mul i64 %x, 12345678901
  %z = add i64 %y, %b
  ret i64 %z
}
declare i64 @llvm.fshr.i64(i64, i64, i64)
