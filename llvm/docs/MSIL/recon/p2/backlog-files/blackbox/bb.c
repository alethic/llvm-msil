static inline void do_not_optimize_in(int v) { __asm__ volatile("" : : "r"(v) : "memory"); }
static inline void do_not_optimize_rm(int *p) { __asm__ volatile("" : : "r,m"(p) : "memory"); }
static inline void do_not_optimize_inout(int *v) { __asm__ volatile("" : "+r"(*v) : : "memory"); }
static inline void clobber(void) { __asm__ volatile("" : : : "memory"); }
int main(void) {
  int x = 41, y = 1;
  do_not_optimize_in(x);
  do_not_optimize_rm(&y);
  do_not_optimize_inout(&x);
  clobber();
  return x + y;
}
