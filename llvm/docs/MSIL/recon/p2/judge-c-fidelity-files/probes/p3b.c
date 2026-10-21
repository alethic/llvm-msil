/* p3b.c */
struct reg { int v; };
static const struct reg r2 __attribute__((used, section("myreg"))) = { 5 };
static const struct reg r3 __attribute__((used, section("myreg"))) = { 7 };
