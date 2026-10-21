/* p3a.c: section-based registration across objects with __start_/__stop_ (ELF) */
struct reg { int v; };
static const struct reg r1 __attribute__((used, section("myreg"))) = { 3 };
extern const struct reg __start_myreg[], __stop_myreg[];
int main(void) {
  int s = 0;
  for (const struct reg *p = __start_myreg; p < __stop_myreg; ++p) s += p->v;
  return s;               /* 15 */
}
