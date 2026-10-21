int impl(int x) { return x + 1; }
int pub_alias(int x) __attribute__((alias("impl")));
static int priv_alias(int x) __attribute__((alias("impl")));
__attribute__((weak, alias("impl"))) int weak_alias(int);
int table[4] = {1, 2, 3, 4};
extern int table_alias[4] __attribute__((alias("table")));
int main(void) { return pub_alias(1) + priv_alias(2) + weak_alias(3) + table_alias[3] - 13; }
