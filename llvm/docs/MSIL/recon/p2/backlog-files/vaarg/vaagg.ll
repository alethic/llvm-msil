%struct.P = type { i32, i32 }
define i32 @get(ptr %ap) {
  %v = va_arg ptr %ap, %struct.P
  %a = extractvalue %struct.P %v, 0
  ret i32 %a
}
