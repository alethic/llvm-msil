/* Small aggregates as arguments and returns. */
struct S1 { char a; };
struct S2 { short a; };
struct S3 { char a, b, c; };
struct S4 { int a; };
struct S5 { char a[5]; };
struct S6 { short a[3]; };
struct S7 { char a[7]; };
struct S8 { int a, b; };
struct S12 { int a, b, c; };
struct S16 { long long a, b; };
struct SIF { int a; float b; };
struct SCF { char a; float b; };
struct HFA2 { float a, b; };
struct HFA3 { float a, b, c; };
struct HFA4 { double a, b, c, d; };
struct HD1 { double a; };
struct HFAarr { float a[3]; };
typedef float v2f __attribute__((vector_size(8)));
typedef float v4f __attribute__((vector_size(16)));
struct HVA2 { v4f a, b; };
struct HVAs { v2f a; };
struct SP2 { void *a, *b; };
struct S24 { int a[6]; };

struct S1 f1(struct S1 x) { return x; }
struct S2 f2(struct S2 x) { return x; }
struct S3 f3(struct S3 x) { return x; }
struct S4 f4(struct S4 x) { return x; }
struct S5 f5(struct S5 x) { return x; }
struct S6 f6(struct S6 x) { return x; }
struct S7 f7(struct S7 x) { return x; }
struct S8 f8(struct S8 x) { return x; }
struct S12 f12(struct S12 x) { return x; }
struct S16 f16(struct S16 x) { return x; }
struct SIF fif(struct SIF x) { return x; }
struct SCF fcf(struct SCF x) { return x; }
struct HFA2 fh2(struct HFA2 x) { return x; }
struct HFA3 fh3(struct HFA3 x) { return x; }
struct HFA4 fh4(struct HFA4 x) { return x; }
struct HD1 fhd1(struct HD1 x) { return x; }
struct HFAarr fharr(struct HFAarr x) { return x; }
struct HVA2 fhva2(struct HVA2 x) { return x; }
struct HVAs fhvas(struct HVAs x) { return x; }
struct SP2 fsp2(struct SP2 x) { return x; }
struct S24 fs24(struct S24 x) { return x; }
