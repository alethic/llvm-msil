int has_avx2(void) { return __builtin_cpu_supports("avx2"); }
int main(void) { return has_avx2() ? 1 : 0; }
