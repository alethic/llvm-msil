#pragma STDC FENV_ACCESS ON
#include <stdint.h>
int puts(const char*);
__attribute__((noinline)) double cvt(__int128 a){return (double)a;}
__attribute__((noinline)) float cvtu(unsigned __int128 a){return (float)a;}
static void hx(uint64_t v){char b[20];for(int i=0;i<16;i++)b[i]="0123456789abcdef"[(v>>(60-4*i))&15];b[16]=0;puts(b);}
int main(void){ volatile __int128 x = ((__int128)1<<100) + 12345; volatile unsigned __int128 y = ~(unsigned __int128)0;
 double d = cvt(x); float f = cvtu(y); uint64_t u; __builtin_memcpy(&u,&d,8); hx(u); uint32_t v; __builtin_memcpy(&v,&f,4); hx(v); return 0; }
