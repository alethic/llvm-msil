// Does localloc'd frame memory start zeroed? Our methods never set localsinit.
int puts(const char *);
__attribute__((noinline)) int fill(volatile int n) { volatile char a[256]; for (int i = 0; i < n; i++) a[i] = (char)0xAB; return a[n - 1]; }
__attribute__((noinline)) int peek(volatile int n) { volatile char a[256]; int s = 0; for (int i = 0; i < n; i++) s += (unsigned char)a[i]; return s; }
int main(void) { fill(256); int s = peek(256); puts(s ? "frame NOT zeroed (stale data visible)" : "frame zeroed"); return s ? 1 : 0; }
