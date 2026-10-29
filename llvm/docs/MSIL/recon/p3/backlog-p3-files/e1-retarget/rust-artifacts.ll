define void @spin_hint() {
  call void @llvm.x86.sse2.pause()
  ret void
}
define void @fastfail(i32 %c) {
  call void asm sideeffect "int $$0x29", "{ecx},~{memory},~{dirflag},~{fpsr},~{flags}"(i32 %c)
  unreachable
}
declare void @llvm.x86.sse2.pause()
