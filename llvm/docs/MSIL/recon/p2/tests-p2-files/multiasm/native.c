#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int add3(int a, int b, int c) { return a + b + c; }
