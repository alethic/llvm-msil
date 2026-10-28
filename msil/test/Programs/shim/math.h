/* Shim of <math.h> for the exit-corpus programs (both builds; -isystem
   ../shim): the prototypes they call. The MSIL build resolves them from the
   C math library (ucrtbase, libm.so.6), the native build from the C
   runtime. */
#ifndef SHIM_MATH_H
#define SHIM_MATH_H
double sin(double);
double cos(double);
double sqrt(double);
double floor(double);
double ceil(double);
double fabs(double);
double exp(double);
double log(double);
double pow(double, double);
double atan2(double, double);
double atan(double);
#endif
