// T-P3 from clang -S on msil64-unknown-dotnet (ImplementationPlan.md 5.18.6
// PL5, milestone P-1a, and its section 12 row on library calls without a
// declaration): standalone text imports nothing, so every function that the
// module does not define is an error with T-P3's text, whether the source
// declares it (ext) or the backend makes the library call itself from an
// intrinsic without a declaration (__builtin_sin with -fno-math-errno is
// llvm.sin, __builtin_ldexp and __builtin_ldexpf are llvm.ldexp). The host
// triples' 'is not a Phase-1 import' never appears.

// REQUIRES: msil-registered-target

// DEFINE: %{tp3} = --implicit-check-not="Phase-1 import" \
// DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not="LLVM ERROR"
// RUN: not %clang --target=msil64-unknown-dotnet -ffreestanding -O2 -fno-math-errno -S -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s %{tp3}
// RUN: not %clang --target=msil64-unknown-dotnet -ffreestanding -O0 -fno-math-errno -S -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s %{tp3}
// CHECK-DAG: {{^}}{{.*}}error: function 'sin' is not defined in this module: standalone text for msil64-unknown-dotnet imports nothing (link with msil-ld){{$}}
// CHECK-DAG: {{^}}{{.*}}error: function 'ldexp' is not defined in this module: standalone text for msil64-unknown-dotnet imports nothing (link with msil-ld){{$}}
// CHECK-DAG: {{^}}{{.*}}error: function 'ldexpf' is not defined in this module: standalone text for msil64-unknown-dotnet imports nothing (link with msil-ld){{$}}
// CHECK-DAG: {{^}}{{.*}}error: function 'ext' is not defined in this module: standalone text for msil64-unknown-dotnet imports nothing (link with msil-ld){{$}}

double ext(double);

double call_sin(double x) { return __builtin_sin(x); }
double call_ldexp(double x, int e) { return __builtin_ldexp(x, e); }
float call_ldexpf(float x, int e) { return __builtin_ldexpf(x, e); }
double call_ext(double x) { return ext(x); }
