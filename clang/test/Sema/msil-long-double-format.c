// The 'long double' format checks of MSIL triples (ImplementationPlan.md
// 5.15.2 'long double formats', M26; decision-brief R15). 'long double' is an
// 8-byte 'double' on every MSIL triple, but the native C library reads and
// writes its own 'long double' for the 'L' length modifier. On the triples
// whose native 'long double' is wider than 'double' (x86_64 linux-gnu and
// linux-musl, x86_64 windows-gnu and aarch64 linux: 16 bytes each) the format
// checker reports, in the group Format (-Wformat):
// - printf family, 'L' before a A e E f F g G: a warning;
// - scanf family, 'L' before a floating conversion: a warning that is an error
//   by default (DefaultError) and a warning again with -Wno-error=format.
// The windows-msvc triples, whose native 'long double' is 'double', report
// nothing, and -Wno-format silences both on every triple.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fsyntax-only -verify=ld,err %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -fsyntax-only -verify=ld,err %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -fsyntax-only -verify=ld,err %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -fsyntax-only -verify=ld,err %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fsyntax-only -Wno-error=format -verify=ld,noerr %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -fsyntax-only -Wno-error=format -verify=ld,noerr %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fsyntax-only -verify=none %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -fsyntax-only -verify=none %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fsyntax-only -Wno-format -verify=none %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -fsyntax-only -Wno-format -verify=none %s

// none-no-diagnostics

typedef __SIZE_TYPE__ size_t;
typedef struct _FILE FILE;

int printf(const char *restrict, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *restrict, const char *restrict, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *restrict, const char *restrict, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *restrict, size_t, const char *restrict, ...) __attribute__((format(printf, 3, 4)));
int scanf(const char *restrict, ...) __attribute__((format(scanf, 1, 2)));
int fscanf(FILE *restrict, const char *restrict, ...) __attribute__((format(scanf, 2, 3)));
int sscanf(const char *restrict, const char *restrict, ...) __attribute__((format(scanf, 2, 3)));

void print(FILE *f, char *buf, long double ld, double d) {
  printf("%La", ld); // ld-warning {{the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target}}
  printf("%LA", ld); // ld-warning {{the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target}}
  printf("%Le", ld); // ld-warning {{the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target}}
  printf("%LE", ld); // ld-warning {{the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target}}
  printf("%Lf", ld); // ld-warning {{the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target}}
  printf("%LF", ld); // ld-warning {{the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target}}
  printf("%Lg", ld); // ld-warning {{the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target}}
  printf("%LG", ld); // ld-warning {{the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target}}
  printf("x = %10.3Lf\n", ld); // ld-warning {{the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target}}
  fprintf(f, "%Lg", ld); // ld-warning {{the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target}}
  sprintf(buf, "%Le", ld); // ld-warning {{the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target}}
  snprintf(buf, 64, "%Lf", ld); // ld-warning {{the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target}}

  // No 'L': nothing to report.
  printf("%f %e %g %a", d, d, d, d);
  printf("%lf %le %lg %la", d, d, d, d);
  printf("%F %E %G %A", d, d, d, d);
  printf("%%Lf");
}

void scan(FILE *f, const char *s, long double *pld, double *pd, float *pf) {
  scanf("%Lf", pld); // err-error {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}} noerr-warning {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}}
  scanf("%Le", pld); // err-error {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}} noerr-warning {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}}
  scanf("%Lg", pld); // err-error {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}} noerr-warning {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}}
  scanf("%La", pld); // err-error {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}} noerr-warning {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}}
  scanf("%LE", pld); // err-error {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}} noerr-warning {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}}
  fscanf(f, "%Lf", pld); // err-error {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}} noerr-warning {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}}
  sscanf(s, "%Lg", pld); // err-error {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}} noerr-warning {{the 'L' length modifier makes the C library store a 16-byte native 'long double' into an 8-byte 'double' on this MSIL target}}

  // No 'L': nothing to report.
  scanf("%lf %le %lg %la", pd, pd, pd, pd);
  scanf("%f %e %g %a", pf, pf, pf, pf);
  scanf("%%Lf");
}
