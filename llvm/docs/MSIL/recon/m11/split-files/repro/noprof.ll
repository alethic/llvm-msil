source_filename = "mfs.c"

define internal i32 @bar() noinline {
  ret i32 1
}

define internal i32 @baz() noinline {
  ret i32 2
}

define i32 @foo1(i1 zeroext %0) {
  br i1 %0, label %2, label %4

2:
  %3 = call i32 @bar()
  br label %6

4:
  %5 = call i32 @baz()
  br label %6

6:
  %r = phi i32 [ %3, %2 ], [ %5, %4 ]
  ret i32 %r
}

