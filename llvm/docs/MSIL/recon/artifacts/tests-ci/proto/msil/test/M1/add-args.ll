; Arithmetic on values the optimizer cannot see: llc does no IPO, so the
; noinline callee really executes even at -O2. Exit code 40 + 2 = 42.
; RUN: %{msil_ll}

define internal i32 @add(i32 %a, i32 %b) noinline {
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @main() {
  %r = call i32 @add(i32 40, i32 2)
  ret i32 %r
}
