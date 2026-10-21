/* Export-table stand-in for glibc libm.so.6. */
__declspec(dllexport) double sin(double x) { return x; }
__declspec(dllexport) double cos(double x) { return x; }
__declspec(dllexport) double sqrt(double x) { return x; }
__declspec(dllexport) double floor(double x) { return x; }
int _fltused = 0;
