/* Rust std's abort path on windows-msvc: core::intrinsics::abort is ud2, but
   std::process::abort -> __fastfail via asm!("int $$0x29", in("ecx") code) */
void die(unsigned c) { __asm__ volatile("int $0x29" :: "c"(c) : "memory"); __builtin_unreachable(); }
int main(void) { return 0; }
