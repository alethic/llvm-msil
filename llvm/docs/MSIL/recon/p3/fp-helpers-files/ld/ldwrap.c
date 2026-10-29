/* What a helper archive could provide on triples whose native long double is wider than double:
   MSIL's long double is double, so each l function forwards to the double one. */
double sqrt(double); double exp(double); double strtod(const char *, char **);
long double sqrtl(long double x) { return sqrt(x); }
long double expl(long double x) { return exp(x); }
long double strtold(const char *s, char **e) { return strtod(s, e); }
