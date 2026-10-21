@g = global [4 x i32] [i32 1, i32 2, i32 3, i32 4]
@g2 = alias i32, getelementptr (i8, ptr @g, i64 8)

define internal i32 @impl(i32 %x) {
  %r = add i32 %x, 1
  ret i32 %r
}
@inc_alias = internal alias i32 (i32), ptr @impl
@inc_pub = alias i32 (i32), ptr @impl

define i32 @main() {
  %a = call i32 @inc_alias(i32 1)
  %b = call i32 @inc_pub(i32 %a)
  %v = load i32, ptr @g2
  %s = add i32 %b, %v
  %p = icmp eq ptr @inc_pub, @impl
  %z = zext i1 %p to i32
  %t = add i32 %s, %z
  ret i32 %t
}
