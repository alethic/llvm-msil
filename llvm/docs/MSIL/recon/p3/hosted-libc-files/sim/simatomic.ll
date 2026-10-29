; hosted-libc recon: single-threaded stand-ins for the __atomic_* libcalls that
; MSIL's legalizer emits for atomic loads, stores and cmpxchg (P3-B atomics).
define i8 @__atomic_load_1(ptr %p, i32 %o) { %v = load volatile i8, ptr %p
  ret i8 %v }
define i16 @__atomic_load_2(ptr %p, i32 %o) { %v = load volatile i16, ptr %p
  ret i16 %v }
define i32 @__atomic_load_4(ptr %p, i32 %o) { %v = load volatile i32, ptr %p
  ret i32 %v }
define i64 @__atomic_load_8(ptr %p, i32 %o) { %v = load volatile i64, ptr %p
  ret i64 %v }
define void @__atomic_store_1(ptr %p, i8 %v, i32 %o) { store volatile i8 %v, ptr %p
  ret void }
define void @__atomic_store_2(ptr %p, i16 %v, i32 %o) { store volatile i16 %v, ptr %p
  ret void }
define void @__atomic_store_4(ptr %p, i32 %v, i32 %o) { store volatile i32 %v, ptr %p
  ret void }
define void @__atomic_store_8(ptr %p, i64 %v, i32 %o) { store volatile i64 %v, ptr %p
  ret void }
define i1 @__atomic_compare_exchange_4(ptr %p, ptr %e, i32 %d, i32 %s, i32 %f) {
  %c = load volatile i32, ptr %p
  %ev = load i32, ptr %e
  %eq = icmp eq i32 %c, %ev
  br i1 %eq, label %y, label %n
y:
  store volatile i32 %d, ptr %p
  ret i1 1
n:
  store i32 %c, ptr %e
  ret i1 0 }
define i1 @__atomic_compare_exchange_8(ptr %p, ptr %e, i64 %d, i32 %s, i32 %f) {
  %c = load volatile i64, ptr %p
  %ev = load i64, ptr %e
  %eq = icmp eq i64 %c, %ev
  br i1 %eq, label %y, label %n
y:
  store volatile i64 %d, ptr %p
  ret i1 1
n:
  store i64 %c, ptr %e
  ret i1 0 }
