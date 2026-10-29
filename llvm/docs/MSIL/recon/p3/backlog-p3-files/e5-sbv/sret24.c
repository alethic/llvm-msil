/* SysV: a 24-byte struct is MEMORY class, returned through a hidden pointer in RDI (and RAX). */
struct s24 { long long a, b, c; };
__attribute__((import_module("libnat24.so"), import_name("make24"))) void make24_sret(struct s24 *, long long);
int main(void) { struct s24 r = {0, 0, 0}; make24_sret(&r, 7); return (r.a == 7 && r.b == 14 && r.c == 21) ? 42 : 1; }
