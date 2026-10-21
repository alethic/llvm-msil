int puts(const char *);
int never_used(int x) { puts("must not be extracted"); return x; }
int duplicate_me(int x) { return x; }
