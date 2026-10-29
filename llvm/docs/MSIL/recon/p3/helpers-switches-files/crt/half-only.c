#include <stdint.h>
int puts(const char *);
static char buf[64];
static void hx(uint32_t v) { for (int i = 0; i < 8; ++i) buf[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15]; buf[8] = 0; puts(buf); }
volatile float f = 1.5f; volatile _Float16 h;
int main(void) { h = (_Float16)f; uint16_t u; __builtin_memcpy(&u, (const void *)&h, 2); hx(u); float g = (float)h * 2.0f; uint32_t w; __builtin_memcpy(&w, &g, 4); hx(w); return 0; }
