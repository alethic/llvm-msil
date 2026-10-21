declare i32 @g(i32)
declare i32 @__gxx_personality_v0(...)
define i32 @h(i32 %x) personality ptr @__gxx_personality_v0 {
entry:
  %r = invoke i32 @g(i32 %x) to label %ok unwind label %lp
ok:
  ret i32 %r
lp:
  %l = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %l
}
