//===-- msil_longdouble.c - long double forwarders for MSIL ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// On MSIL triples `long double` is `double`. Where the native C library's
// `long double` is wider (x87 on the x86_64 Linux and MinGW triples, binary128
// on aarch64 Linux: the triples of msil-ld's hasWideNativeLongDouble), its `l`
// functions cannot take an MSIL value, so the helper archive clang_rt.builtins
// of those triples defines each function of msil-ld's table
// nativeLongDoubleFunctions that has a `double` counterpart as a forwarder to
// it: `sqrtl` calls `sqrt`, `strtold` calls `strtod`, `__finitel` calls
// `__finite`, `faddl` calls `fadd`, `cexpl` calls `cexp`. Archive members
// resolve before native libraries, so the native `l` function is never bound.
// With `long double` as `double`, each forwarder computes exactly what its C
// function means on MSIL. Where the counterpart differs from the name minus
// its `l`: `daddl`, `dsubl`, `dmull` and `ddivl` call glibc's `f32xaddf64`,
// `f32xsubf64`, `f32xmulf64` and `f32xdivf64`; `dfmal` calls `fma` and
// `dsqrtl` `sqrt`; `nexttowardl` calls `nextafter`; `pow10l` calls `exp10`
// (glibc exports `pow10` only to old binaries); `nexttoward` calls
// `nextafter`, and `nexttowardf` compares in `double`. The four f32x
// functions round once and set errno as native `daddl` does: ERANGE on
// overflow, on underflow to zero and on division by zero, EDOM on an invalid
// operation (C17 7.12.1, with glibc's math_errhandling including MATH_ERRNO),
// which one operation in `double` would not. musl and MinGW have neither
// `daddl` nor its counterpart, so there a call fails to link, naming the
// counterpart, as native code fails naming `daddl`. A forwarder whose
// counterpart MSIL cannot call yet (`cexp` and the other complex functions
// that return a structure) is written all the same: its link names the
// counterpart's own error. The functions of the table without a forwarder,
// and every _Float64x and _Float128 function, stay msil-ld's link error:
//
// not forwarded: fromfpl: the type of fromfp differs by C library
// not forwarded: fromfpxl: the type of fromfpx differs by C library
// not forwarded: ufromfpl: the type of ufromfp differs by C library
// not forwarded: ufromfpxl: the type of ufromfpx differs by C library
//
// C23 made fromfp, fromfpx, ufromfp and ufromfpx return the floating type
// where TS 18661-1 returned intmax_t or uintmax_t, and glibc 2.43 exports both
// (fromfp@@GLIBC_2.43 and fromfp@GLIBC_2.25), so the counterpart's type depends
// on the program's headers and on the C library it runs with.
//
// build-builtins.py compiles this file once per function: each function sits
// under `#if MSIL_MEMBER(<name>)`, and the member of <name> is compiled with
// -D"MSIL_MEMBER(n)=MSIL_ONLY_##n" -DMSIL_ONLY_<name>=1, so that it defines
// that function alone and references only its counterpart. Without these
// flags MSIL_MEMBER(<name>) is 1 and the file defines every function. As every
// toolchain-authored source of the archive, the file includes no system
// header: each function declares the native function it calls.
//
//===----------------------------------------------------------------------===//

#ifndef MSIL_MEMBER
#define MSIL_MEMBER(name) 1
#endif

typedef __SIZE_TYPE__ __msil_size_t;
typedef __WCHAR_TYPE__ __msil_wchar_t;
typedef struct __locale_struct *__msil_locale_t;

#if MSIL_MEMBER(__clog10l)
double _Complex __clog10(double _Complex);
long double _Complex __clog10l(long double _Complex z) { return __clog10(z); }
#endif

#if MSIL_MEMBER(__finitel)
int __finite(double);
int __finitel(long double x) { return __finite(x); }
#endif

#if MSIL_MEMBER(__fpclassifyl)
int __fpclassify(double);
int __fpclassifyl(long double x) { return __fpclassify(x); }
#endif

#if MSIL_MEMBER(__iseqsigl)
int __iseqsig(double, double);
int __iseqsigl(long double x, long double y) { return __iseqsig(x, y); }
#endif

#if MSIL_MEMBER(__isinfl)
int __isinf(double);
int __isinfl(long double x) { return __isinf(x); }
#endif

#if MSIL_MEMBER(__isnanl)
int __isnan(double);
int __isnanl(long double x) { return __isnan(x); }
#endif

#if MSIL_MEMBER(__issignalingl)
int __issignaling(double);
int __issignalingl(long double x) { return __issignaling(x); }
#endif

#if MSIL_MEMBER(__signbitl)
int __signbit(double);
int __signbitl(long double x) { return __signbit(x); }
#endif

#if MSIL_MEMBER(__strtold_internal)
double __strtod_internal(const char *, char **, int);
long double __strtold_internal(const char *s, char **end, int group) {
  return __strtod_internal(s, end, group);
}
#endif

#if MSIL_MEMBER(__strtold_l)
double __strtod_l(const char *, char **, __msil_locale_t);
long double __strtold_l(const char *s, char **end, __msil_locale_t loc) {
  return __strtod_l(s, end, loc);
}
#endif

#if MSIL_MEMBER(__wcstold_internal)
double __wcstod_internal(const __msil_wchar_t *, __msil_wchar_t **, int);
long double __wcstold_internal(const __msil_wchar_t *s, __msil_wchar_t **end,
                               int group) {
  return __wcstod_internal(s, end, group);
}
#endif

#if MSIL_MEMBER(__wcstold_l)
double __wcstod_l(const __msil_wchar_t *, __msil_wchar_t **, __msil_locale_t);
long double __wcstold_l(const __msil_wchar_t *s, __msil_wchar_t **end,
                        __msil_locale_t loc) {
  return __wcstod_l(s, end, loc);
}
#endif

#if MSIL_MEMBER(acoshl)
double acosh(double);
long double acoshl(long double x) { return acosh(x); }
#endif

#if MSIL_MEMBER(acosl)
double acos(double);
long double acosl(long double x) { return acos(x); }
#endif

#if MSIL_MEMBER(acospil)
double acospi(double);
long double acospil(long double x) { return acospi(x); }
#endif

#if MSIL_MEMBER(asinhl)
double asinh(double);
long double asinhl(long double x) { return asinh(x); }
#endif

#if MSIL_MEMBER(asinl)
double asin(double);
long double asinl(long double x) { return asin(x); }
#endif

#if MSIL_MEMBER(asinpil)
double asinpi(double);
long double asinpil(long double x) { return asinpi(x); }
#endif

#if MSIL_MEMBER(atan2l)
double atan2(double, double);
long double atan2l(long double x, long double y) { return atan2(x, y); }
#endif

#if MSIL_MEMBER(atan2pil)
double atan2pi(double, double);
long double atan2pil(long double x, long double y) { return atan2pi(x, y); }
#endif

#if MSIL_MEMBER(atanhl)
double atanh(double);
long double atanhl(long double x) { return atanh(x); }
#endif

#if MSIL_MEMBER(atanl)
double atan(double);
long double atanl(long double x) { return atan(x); }
#endif

#if MSIL_MEMBER(atanpil)
double atanpi(double);
long double atanpil(long double x) { return atanpi(x); }
#endif

#if MSIL_MEMBER(cabsl)
double cabs(double _Complex);
long double cabsl(long double _Complex z) { return cabs(z); }
#endif

#if MSIL_MEMBER(cacoshl)
double _Complex cacosh(double _Complex);
long double _Complex cacoshl(long double _Complex z) { return cacosh(z); }
#endif

#if MSIL_MEMBER(cacosl)
double _Complex cacos(double _Complex);
long double _Complex cacosl(long double _Complex z) { return cacos(z); }
#endif

#if MSIL_MEMBER(canonicalizel)
int canonicalize(double *, const double *);
int canonicalizel(long double *cx, const long double *x) {
  return canonicalize((double *)cx, (const double *)x);
}
#endif

#if MSIL_MEMBER(cargl)
double carg(double _Complex);
long double cargl(long double _Complex z) { return carg(z); }
#endif

#if MSIL_MEMBER(casinhl)
double _Complex casinh(double _Complex);
long double _Complex casinhl(long double _Complex z) { return casinh(z); }
#endif

#if MSIL_MEMBER(casinl)
double _Complex casin(double _Complex);
long double _Complex casinl(long double _Complex z) { return casin(z); }
#endif

#if MSIL_MEMBER(catanhl)
double _Complex catanh(double _Complex);
long double _Complex catanhl(long double _Complex z) { return catanh(z); }
#endif

#if MSIL_MEMBER(catanl)
double _Complex catan(double _Complex);
long double _Complex catanl(long double _Complex z) { return catan(z); }
#endif

#if MSIL_MEMBER(cbrtl)
double cbrt(double);
long double cbrtl(long double x) { return cbrt(x); }
#endif

#if MSIL_MEMBER(ccoshl)
double _Complex ccosh(double _Complex);
long double _Complex ccoshl(long double _Complex z) { return ccosh(z); }
#endif

#if MSIL_MEMBER(ccosl)
double _Complex ccos(double _Complex);
long double _Complex ccosl(long double _Complex z) { return ccos(z); }
#endif

#if MSIL_MEMBER(ceill)
double ceil(double);
long double ceill(long double x) { return ceil(x); }
#endif

#if MSIL_MEMBER(cexpl)
double _Complex cexp(double _Complex);
long double _Complex cexpl(long double _Complex z) { return cexp(z); }
#endif

#if MSIL_MEMBER(cimagl)
double cimag(double _Complex);
long double cimagl(long double _Complex z) { return cimag(z); }
#endif

#if MSIL_MEMBER(clog10l)
double _Complex clog10(double _Complex);
long double _Complex clog10l(long double _Complex z) { return clog10(z); }
#endif

#if MSIL_MEMBER(clogl)
double _Complex clog(double _Complex);
long double _Complex clogl(long double _Complex z) { return clog(z); }
#endif

#if MSIL_MEMBER(compoundnl)
double compoundn(double, long long);
long double compoundnl(long double x, long long n) { return compoundn(x, n); }
#endif

#if MSIL_MEMBER(conjl)
double _Complex conj(double _Complex);
long double _Complex conjl(long double _Complex z) { return conj(z); }
#endif

#if MSIL_MEMBER(copysignl)
double copysign(double, double);
long double copysignl(long double x, long double y) { return copysign(x, y); }
#endif

#if MSIL_MEMBER(coshl)
double cosh(double);
long double coshl(long double x) { return cosh(x); }
#endif

#if MSIL_MEMBER(cosl)
double cos(double);
long double cosl(long double x) { return cos(x); }
#endif

#if MSIL_MEMBER(cospil)
double cospi(double);
long double cospil(long double x) { return cospi(x); }
#endif

#if MSIL_MEMBER(cpowl)
double _Complex cpow(double _Complex, double _Complex);
long double _Complex cpowl(long double _Complex x, long double _Complex y) {
  return cpow(x, y);
}
#endif

#if MSIL_MEMBER(cprojl)
double _Complex cproj(double _Complex);
long double _Complex cprojl(long double _Complex z) { return cproj(z); }
#endif

#if MSIL_MEMBER(creall)
double creal(double _Complex);
long double creall(long double _Complex z) { return creal(z); }
#endif

#if MSIL_MEMBER(csinhl)
double _Complex csinh(double _Complex);
long double _Complex csinhl(long double _Complex z) { return csinh(z); }
#endif

#if MSIL_MEMBER(csinl)
double _Complex csin(double _Complex);
long double _Complex csinl(long double _Complex z) { return csin(z); }
#endif

#if MSIL_MEMBER(csqrtl)
double _Complex csqrt(double _Complex);
long double _Complex csqrtl(long double _Complex z) { return csqrt(z); }
#endif

#if MSIL_MEMBER(ctanhl)
double _Complex ctanh(double _Complex);
long double _Complex ctanhl(long double _Complex z) { return ctanh(z); }
#endif

#if MSIL_MEMBER(ctanl)
double _Complex ctan(double _Complex);
long double _Complex ctanl(long double _Complex z) { return ctan(z); }
#endif

#if MSIL_MEMBER(daddl)
double f32xaddf64(double, double);
double daddl(long double x, long double y) { return f32xaddf64(x, y); }
#endif

#if MSIL_MEMBER(ddivl)
double f32xdivf64(double, double);
double ddivl(long double x, long double y) { return f32xdivf64(x, y); }
#endif

#if MSIL_MEMBER(dfmal)
double fma(double, double, double);
double dfmal(long double x, long double y, long double z) {
  return fma(x, y, z);
}
#endif

#if MSIL_MEMBER(dmull)
double f32xmulf64(double, double);
double dmull(long double x, long double y) { return f32xmulf64(x, y); }
#endif

#if MSIL_MEMBER(dreml)
double drem(double, double);
long double dreml(long double x, long double y) { return drem(x, y); }
#endif

#if MSIL_MEMBER(dsqrtl)
double sqrt(double);
double dsqrtl(long double x) { return sqrt(x); }
#endif

#if MSIL_MEMBER(dsubl)
double f32xsubf64(double, double);
double dsubl(long double x, long double y) { return f32xsubf64(x, y); }
#endif

#if MSIL_MEMBER(erfcl)
double erfc(double);
long double erfcl(long double x) { return erfc(x); }
#endif

#if MSIL_MEMBER(erfl)
double erf(double);
long double erfl(long double x) { return erf(x); }
#endif

#if MSIL_MEMBER(exp10l)
double exp10(double);
long double exp10l(long double x) { return exp10(x); }
#endif

#if MSIL_MEMBER(exp10m1l)
double exp10m1(double);
long double exp10m1l(long double x) { return exp10m1(x); }
#endif

#if MSIL_MEMBER(exp2l)
double exp2(double);
long double exp2l(long double x) { return exp2(x); }
#endif

#if MSIL_MEMBER(exp2m1l)
double exp2m1(double);
long double exp2m1l(long double x) { return exp2m1(x); }
#endif

#if MSIL_MEMBER(expl)
double exp(double);
long double expl(long double x) { return exp(x); }
#endif

#if MSIL_MEMBER(expm1l)
double expm1(double);
long double expm1l(long double x) { return expm1(x); }
#endif

#if MSIL_MEMBER(fabsl)
double fabs(double);
long double fabsl(long double x) { return fabs(x); }
#endif

#if MSIL_MEMBER(faddl)
float fadd(double, double);
float faddl(long double x, long double y) { return fadd(x, y); }
#endif

#if MSIL_MEMBER(fdiml)
double fdim(double, double);
long double fdiml(long double x, long double y) { return fdim(x, y); }
#endif

#if MSIL_MEMBER(fdivl)
float fdiv(double, double);
float fdivl(long double x, long double y) { return fdiv(x, y); }
#endif

#if MSIL_MEMBER(ffmal)
float ffma(double, double, double);
float ffmal(long double x, long double y, long double z) {
  return ffma(x, y, z);
}
#endif

#if MSIL_MEMBER(finitel)
int finite(double);
int finitel(long double x) { return finite(x); }
#endif

#if MSIL_MEMBER(floorl)
double floor(double);
long double floorl(long double x) { return floor(x); }
#endif

#if MSIL_MEMBER(fmal)
double fma(double, double, double);
long double fmal(long double x, long double y, long double z) {
  return fma(x, y, z);
}
#endif

#if MSIL_MEMBER(fmaximum_mag_numl)
double fmaximum_mag_num(double, double);
long double fmaximum_mag_numl(long double x, long double y) {
  return fmaximum_mag_num(x, y);
}
#endif

#if MSIL_MEMBER(fmaximum_magl)
double fmaximum_mag(double, double);
long double fmaximum_magl(long double x, long double y) {
  return fmaximum_mag(x, y);
}
#endif

#if MSIL_MEMBER(fmaximum_numl)
double fmaximum_num(double, double);
long double fmaximum_numl(long double x, long double y) {
  return fmaximum_num(x, y);
}
#endif

#if MSIL_MEMBER(fmaximuml)
double fmaximum(double, double);
long double fmaximuml(long double x, long double y) { return fmaximum(x, y); }
#endif

#if MSIL_MEMBER(fmaxl)
double fmax(double, double);
long double fmaxl(long double x, long double y) { return fmax(x, y); }
#endif

#if MSIL_MEMBER(fmaxmagl)
double fmaxmag(double, double);
long double fmaxmagl(long double x, long double y) { return fmaxmag(x, y); }
#endif

#if MSIL_MEMBER(fminimum_mag_numl)
double fminimum_mag_num(double, double);
long double fminimum_mag_numl(long double x, long double y) {
  return fminimum_mag_num(x, y);
}
#endif

#if MSIL_MEMBER(fminimum_magl)
double fminimum_mag(double, double);
long double fminimum_magl(long double x, long double y) {
  return fminimum_mag(x, y);
}
#endif

#if MSIL_MEMBER(fminimum_numl)
double fminimum_num(double, double);
long double fminimum_numl(long double x, long double y) {
  return fminimum_num(x, y);
}
#endif

#if MSIL_MEMBER(fminimuml)
double fminimum(double, double);
long double fminimuml(long double x, long double y) { return fminimum(x, y); }
#endif

#if MSIL_MEMBER(fminl)
double fmin(double, double);
long double fminl(long double x, long double y) { return fmin(x, y); }
#endif

#if MSIL_MEMBER(fminmagl)
double fminmag(double, double);
long double fminmagl(long double x, long double y) { return fminmag(x, y); }
#endif

#if MSIL_MEMBER(fmodl)
double fmod(double, double);
long double fmodl(long double x, long double y) { return fmod(x, y); }
#endif

#if MSIL_MEMBER(fmull)
float fmul(double, double);
float fmull(long double x, long double y) { return fmul(x, y); }
#endif

#if MSIL_MEMBER(frexpl)
double frexp(double, int *);
long double frexpl(long double x, int *e) { return frexp(x, e); }
#endif

#if MSIL_MEMBER(fsqrtl)
float fsqrt(double);
float fsqrtl(long double x) { return fsqrt(x); }
#endif

#if MSIL_MEMBER(fsubl)
float fsub(double, double);
float fsubl(long double x, long double y) { return fsub(x, y); }
#endif

#if MSIL_MEMBER(gammal)
double gamma(double);
long double gammal(long double x) { return gamma(x); }
#endif

#if MSIL_MEMBER(getpayloadl)
double getpayload(const double *);
long double getpayloadl(const long double *x) {
  return getpayload((const double *)x);
}
#endif

#if MSIL_MEMBER(hypotl)
double hypot(double, double);
long double hypotl(long double x, long double y) { return hypot(x, y); }
#endif

#if MSIL_MEMBER(ilogbl)
int ilogb(double);
int ilogbl(long double x) { return ilogb(x); }
#endif

#if MSIL_MEMBER(isinfl)
int isinf(double);
int isinfl(long double x) { return isinf(x); }
#endif

#if MSIL_MEMBER(isnanl)
int isnan(double);
int isnanl(long double x) { return isnan(x); }
#endif

#if MSIL_MEMBER(j0l)
double j0(double);
long double j0l(long double x) { return j0(x); }
#endif

#if MSIL_MEMBER(j1l)
double j1(double);
long double j1l(long double x) { return j1(x); }
#endif

#if MSIL_MEMBER(jnl)
double jn(int, double);
long double jnl(int n, long double x) { return jn(n, x); }
#endif

#if MSIL_MEMBER(ldexpl)
double ldexp(double, int);
long double ldexpl(long double x, int e) { return ldexp(x, e); }
#endif

#if MSIL_MEMBER(lgammal)
double lgamma(double);
long double lgammal(long double x) { return lgamma(x); }
#endif

#if MSIL_MEMBER(lgammal_r)
double lgamma_r(double, int *);
long double lgammal_r(long double x, int *sign) { return lgamma_r(x, sign); }
#endif

#if MSIL_MEMBER(llogbl)
long llogb(double);
long llogbl(long double x) { return llogb(x); }
#endif

#if MSIL_MEMBER(llrintl)
long long llrint(double);
long long llrintl(long double x) { return llrint(x); }
#endif

#if MSIL_MEMBER(llroundl)
long long llround(double);
long long llroundl(long double x) { return llround(x); }
#endif

#if MSIL_MEMBER(log10l)
double log10(double);
long double log10l(long double x) { return log10(x); }
#endif

#if MSIL_MEMBER(log10p1l)
double log10p1(double);
long double log10p1l(long double x) { return log10p1(x); }
#endif

#if MSIL_MEMBER(log1pl)
double log1p(double);
long double log1pl(long double x) { return log1p(x); }
#endif

#if MSIL_MEMBER(log2l)
double log2(double);
long double log2l(long double x) { return log2(x); }
#endif

#if MSIL_MEMBER(log2p1l)
double log2p1(double);
long double log2p1l(long double x) { return log2p1(x); }
#endif

#if MSIL_MEMBER(logbl)
double logb(double);
long double logbl(long double x) { return logb(x); }
#endif

#if MSIL_MEMBER(logl)
double log(double);
long double logl(long double x) { return log(x); }
#endif

#if MSIL_MEMBER(logp1l)
double logp1(double);
long double logp1l(long double x) { return logp1(x); }
#endif

#if MSIL_MEMBER(lrintl)
long lrint(double);
long lrintl(long double x) { return lrint(x); }
#endif

#if MSIL_MEMBER(lroundl)
long lround(double);
long lroundl(long double x) { return lround(x); }
#endif

#if MSIL_MEMBER(modfl)
double modf(double, double *);
long double modfl(long double x, long double *ip) {
  return modf(x, (double *)ip);
}
#endif

#if MSIL_MEMBER(nanl)
double nan(const char *);
long double nanl(const char *tag) { return nan(tag); }
#endif

#if MSIL_MEMBER(nearbyintl)
double nearbyint(double);
long double nearbyintl(long double x) { return nearbyint(x); }
#endif

#if MSIL_MEMBER(nextafterl)
double nextafter(double, double);
long double nextafterl(long double x, long double y) { return nextafter(x, y); }
#endif

#if MSIL_MEMBER(nextdownl)
double nextdown(double);
long double nextdownl(long double x) { return nextdown(x); }
#endif

#if MSIL_MEMBER(nexttoward)
double nextafter(double, double);
double nexttoward(double x, long double y) { return nextafter(x, y); }
#endif

#if MSIL_MEMBER(nexttowardf)
float nextafterf(float, float);
float nexttowardf(float x, long double y) {
  // A NaN operand gives a NaN, as x + y does natively; then the
  // comparison is made in long double (here double), never in float:
  // y rounded to float may equal x although y does not.
  if (x != x || y != y)
    return (float)(x + y);
  if (x == y)
    return (float)y;
  return nextafterf(x, x < y ? __builtin_inff() : -__builtin_inff());
}
#endif

#if MSIL_MEMBER(nexttowardl)
double nextafter(double, double);
long double nexttowardl(long double x, long double y) {
  return nextafter(x, y);
}
#endif

#if MSIL_MEMBER(nextupl)
double nextup(double);
long double nextupl(long double x) { return nextup(x); }
#endif

#if MSIL_MEMBER(pow10l)
double exp10(double);
long double pow10l(long double x) { return exp10(x); }
#endif

#if MSIL_MEMBER(powl)
double pow(double, double);
long double powl(long double x, long double y) { return pow(x, y); }
#endif

#if MSIL_MEMBER(pownl)
double pown(double, long long);
long double pownl(long double x, long long n) { return pown(x, n); }
#endif

#if MSIL_MEMBER(powrl)
double powr(double, double);
long double powrl(long double x, long double y) { return powr(x, y); }
#endif

#if MSIL_MEMBER(qecvt)
char *ecvt(double, int, int *, int *);
char *qecvt(long double x, int ndigit, int *decpt, int *sign) {
  return ecvt(x, ndigit, decpt, sign);
}
#endif

#if MSIL_MEMBER(qecvt_r)
int ecvt_r(double, int, int *, int *, char *, __msil_size_t);
int qecvt_r(long double x, int ndigit, int *decpt, int *sign, char *buf,
            __msil_size_t len) {
  return ecvt_r(x, ndigit, decpt, sign, buf, len);
}
#endif

#if MSIL_MEMBER(qfcvt)
char *fcvt(double, int, int *, int *);
char *qfcvt(long double x, int ndigit, int *decpt, int *sign) {
  return fcvt(x, ndigit, decpt, sign);
}
#endif

#if MSIL_MEMBER(qfcvt_r)
int fcvt_r(double, int, int *, int *, char *, __msil_size_t);
int qfcvt_r(long double x, int ndigit, int *decpt, int *sign, char *buf,
            __msil_size_t len) {
  return fcvt_r(x, ndigit, decpt, sign, buf, len);
}
#endif

#if MSIL_MEMBER(qgcvt)
char *gcvt(double, int, char *);
char *qgcvt(long double x, int ndigit, char *buf) {
  return gcvt(x, ndigit, buf);
}
#endif

#if MSIL_MEMBER(remainderl)
double remainder(double, double);
long double remainderl(long double x, long double y) { return remainder(x, y); }
#endif

#if MSIL_MEMBER(remquol)
double remquo(double, double, int *);
long double remquol(long double x, long double y, int *quo) {
  return remquo(x, y, quo);
}
#endif

#if MSIL_MEMBER(rintl)
double rint(double);
long double rintl(long double x) { return rint(x); }
#endif

#if MSIL_MEMBER(rootnl)
double rootn(double, long long);
long double rootnl(long double x, long long n) { return rootn(x, n); }
#endif

#if MSIL_MEMBER(roundevenl)
double roundeven(double);
long double roundevenl(long double x) { return roundeven(x); }
#endif

#if MSIL_MEMBER(roundl)
double round(double);
long double roundl(long double x) { return round(x); }
#endif

#if MSIL_MEMBER(rsqrtl)
double rsqrt(double);
long double rsqrtl(long double x) { return rsqrt(x); }
#endif

#if MSIL_MEMBER(scalbl)
double scalb(double, double);
long double scalbl(long double x, long double y) { return scalb(x, y); }
#endif

#if MSIL_MEMBER(scalblnl)
double scalbln(double, long);
long double scalblnl(long double x, long n) { return scalbln(x, n); }
#endif

#if MSIL_MEMBER(scalbnl)
double scalbn(double, int);
long double scalbnl(long double x, int n) { return scalbn(x, n); }
#endif

#if MSIL_MEMBER(setpayloadl)
int setpayload(double *, double);
int setpayloadl(long double *res, long double pl) {
  return setpayload((double *)res, pl);
}
#endif

#if MSIL_MEMBER(setpayloadsigl)
int setpayloadsig(double *, double);
int setpayloadsigl(long double *res, long double pl) {
  return setpayloadsig((double *)res, pl);
}
#endif

#if MSIL_MEMBER(significandl)
double significand(double);
long double significandl(long double x) { return significand(x); }
#endif

#if MSIL_MEMBER(sincosl)
void sincos(double, double *, double *);
void sincosl(long double x, long double *s, long double *c) {
  sincos(x, (double *)s, (double *)c);
}
#endif

#if MSIL_MEMBER(sinhl)
double sinh(double);
long double sinhl(long double x) { return sinh(x); }
#endif

#if MSIL_MEMBER(sinl)
double sin(double);
long double sinl(long double x) { return sin(x); }
#endif

#if MSIL_MEMBER(sinpil)
double sinpi(double);
long double sinpil(long double x) { return sinpi(x); }
#endif

#if MSIL_MEMBER(sqrtl)
double sqrt(double);
long double sqrtl(long double x) { return sqrt(x); }
#endif

#if MSIL_MEMBER(strfroml)
int strfromd(char *, __msil_size_t, const char *, double);
int strfroml(char *str, __msil_size_t n, const char *format, long double x) {
  return strfromd(str, n, format, x);
}
#endif

#if MSIL_MEMBER(strtold)
double strtod(const char *, char **);
long double strtold(const char *s, char **end) { return strtod(s, end); }
#endif

#if MSIL_MEMBER(strtold_l)
double strtod_l(const char *, char **, __msil_locale_t);
long double strtold_l(const char *s, char **end, __msil_locale_t loc) {
  return strtod_l(s, end, loc);
}
#endif

#if MSIL_MEMBER(tanhl)
double tanh(double);
long double tanhl(long double x) { return tanh(x); }
#endif

#if MSIL_MEMBER(tanl)
double tan(double);
long double tanl(long double x) { return tan(x); }
#endif

#if MSIL_MEMBER(tanpil)
double tanpi(double);
long double tanpil(long double x) { return tanpi(x); }
#endif

#if MSIL_MEMBER(tgammal)
double tgamma(double);
long double tgammal(long double x) { return tgamma(x); }
#endif

#if MSIL_MEMBER(totalorderl)
int totalorder(const double *, const double *);
int totalorderl(const long double *x, const long double *y) {
  return totalorder((const double *)x, (const double *)y);
}
#endif

#if MSIL_MEMBER(totalordermagl)
int totalordermag(const double *, const double *);
int totalordermagl(const long double *x, const long double *y) {
  return totalordermag((const double *)x, (const double *)y);
}
#endif

#if MSIL_MEMBER(truncl)
double trunc(double);
long double truncl(long double x) { return trunc(x); }
#endif

#if MSIL_MEMBER(wcstold)
double wcstod(const __msil_wchar_t *, __msil_wchar_t **);
long double wcstold(const __msil_wchar_t *s, __msil_wchar_t **end) {
  return wcstod(s, end);
}
#endif

#if MSIL_MEMBER(wcstold_l)
double wcstod_l(const __msil_wchar_t *, __msil_wchar_t **, __msil_locale_t);
long double wcstold_l(const __msil_wchar_t *s, __msil_wchar_t **end,
                      __msil_locale_t loc) {
  return wcstod_l(s, end, loc);
}
#endif

#if MSIL_MEMBER(y0l)
double y0(double);
long double y0l(long double x) { return y0(x); }
#endif

#if MSIL_MEMBER(y1l)
double y1(double);
long double y1l(long double x) { return y1(x); }
#endif

#if MSIL_MEMBER(ynl)
double yn(int, double);
long double ynl(int n, long double x) { return yn(n, x); }
#endif
