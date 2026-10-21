/* p7.c: what the object must say about undefined functions and linker directives */
#pragma comment(lib, "m")
int ext_proto(int, double);
int ext_noproto();
int ext_var(const char *, ...);
struct big { long a, b, c; };
struct big ext_sret(int);
int ext_byval(struct big);
extern int ext_data;
int use(void) {
  struct big s = ext_sret(1);
  return ext_proto(1, 2.0) + ext_noproto(3) + ext_var("x", 4) + ext_byval(s) + ext_data;
}
