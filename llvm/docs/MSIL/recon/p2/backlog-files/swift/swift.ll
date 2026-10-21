define swiftcc i32 @add(i32 %a, i32 %b, ptr swiftself %s) {
  %r = add i32 %a, %b
  ret i32 %r
}
define i32 @main() {
  %r = call swiftcc i32 @add(i32 40, i32 2, ptr swiftself null)
  ret i32 %r
}
