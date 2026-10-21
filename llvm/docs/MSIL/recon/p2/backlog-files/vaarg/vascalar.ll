define i32 @get(ptr %ap) {
  %v = va_arg ptr %ap, i32
  ret i32 %v
}
