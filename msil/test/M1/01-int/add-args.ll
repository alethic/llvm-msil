; Direct calls with arguments and results (Milestone 4): main passes constants
; to noinline helpers that add their int32 and native int arguments; llc does
; no IPO, so the additions run in the callees even at -O2. Exit status 42
; (add(40, 2)); bit 7 is set when a 64-bit sum is wrong. The helper is named
; 'add' on purpose: that is an ilasm keyword, so the call site must quote the
; name exactly as the method header does (D11).
; RUN: %{msil_ll}

define internal i32 @add(i32 %a, i32 %b) noinline {
  %r = add i32 %a, %b
  ret i32 %r
}

define i64 @add64(i64 %a, i64 %b) noinline {
  %r = add i64 %a, %b
  ret i64 %r
}

define internal i64 @add_mixed(i32 %a, i64 %b) noinline {
  %w = sext i32 %a to i64
  %r = add i64 %w, %b
  ret i64 %r
}

define i32 @main() {
  %s = call i32 @add(i32 40, i32 2)                                 ; 42
  ; 0x7FFFFFFF00000000 + 0x10000002A = 0x800000000000002A
  %w = call i64 @add64(i64 9223372032559808512, i64 4294967338)
  ; -1 + 0x100000000 = 0xFFFFFFFF
  %m = call i64 @add_mixed(i32 -1, i64 4294967296)
  %x0 = xor i64 %w, -9223372036854775766
  %x1 = xor i64 %m, 4294967295
  %e = or i64 %x0, %x1
  %bad = icmp ne i64 %e, 0
  %flag = zext i1 %bad to i32
  %f7 = shl i32 %flag, 7
  %low = and i32 %s, 127
  %r = or i32 %low, %f7
  ret i32 %r
}
