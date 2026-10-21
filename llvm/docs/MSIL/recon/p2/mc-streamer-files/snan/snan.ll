define i32 @bits(float %x) {
  %b = bitcast float %x to i32
  ret i32 %b
}
define i32 @main() {
  %r = call i32 @bits(float 0x7FF4000000000000)
  %s = and i32 %r, 4194304
  ret i32 %s
}
