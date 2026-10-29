/* A native function that returns a struct through a hidden pointer (sret), called as a P/Invoke
   whose first parameter is that pointer: the x86-64 ABIs (Win64 and SysV) pass it in the first
   integer register (RCX / RDI) and return it in RAX, so the scalar P/Invoke is ABI-exact. */
typedef struct { long long quot, rem; } lldiv_t;
#ifdef _WIN32
#define LIBC "ucrtbase.dll"
#else
#define LIBC "libc.so.6"
#endif
__attribute__((import_module(LIBC), import_name("lldiv"))) void lldiv_sret(lldiv_t *, long long, long long);
int main(void) {
  lldiv_t r = {0, 0};
  lldiv_sret(&r, 1000000000007LL, 10);
  return (r.rem == 7 && r.quot == 100000000000LL) ? 42 : 1;
}
