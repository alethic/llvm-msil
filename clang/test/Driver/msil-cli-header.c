// <cli/cli.h> (ImplementationPlan.md 5.21.2 RT1 and 5.21.14 RV17, milestone
// R-a): a header among clang's own resource headers, found as <cli/cli.h> on
// every MSIL object-format triple (the eight host triples,
// msil64-unknown-dotnet and msil32-unknown-dotnet) with no option and no
// system headers (-nostdlibinc). It holds only CLI_TYPE(N), which expands to
// __attribute__((cli_type(N))) (the MACROS run): a file that includes it
// alone preprocesses to no tokens (EMPTY) and gets none of the C library's
// macros. It lives in the 'cli' folder of the resource include directory, so
// '#include <cli.h>' does not find it (the copy loop and install rule of
// clang/lib/Headers/CMakeLists.txt; R-a.0 checks the install itself). Off the
// MSIL triples it stops with its #error (OFF). The driver never turns Blocks
// on by default for an MSIL triple (RC7, the NOBLOCKS runs; Apple's toolchain
// does, the APPLE run), so '^' over a function type stays an error there
// unless -fblocks is given.

// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=x86_64-unknown-linux-musl-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=aarch64-unknown-linux-musl-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=x86_64-w64-windows-gnu-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=aarch64-pc-windows-msvc-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=aarch64-w64-windows-gnu-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=msil64-unknown-dotnet -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=msil32-unknown-dotnet -nostdlibinc -fsyntax-only -Xclang -verify %s

// RUN: %clang --target=x86_64-pc-windows-msvc-msil -nostdlibinc -E -dM %s | FileCheck %s --check-prefix=MACROS
// RUN: %clang --target=msil64-unknown-dotnet -nostdlibinc -E -dM %s | FileCheck %s --check-prefix=MACROS
// MACROS: #define CLI_TYPE(N) __attribute__((cli_type(N)))

// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -nostdlibinc -E -P -DONLY %s \
// RUN:   | FileCheck %s --check-prefix=EMPTY --allow-empty
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -nostdlibinc -E -P -DONLY %s \
// RUN:   | FileCheck %s --check-prefix=EMPTY --allow-empty
// EMPTY-NOT: {{[^[:space:]]}}

// RUN: not %clang --target=x86_64-unknown-linux-gnu -nostdlibinc -fsyntax-only -DONLY %s 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: not %clang --target=x86_64-pc-windows-msvc -nostdlibinc -fsyntax-only -DONLY %s 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: not %clang --target=aarch64-unknown-linux-gnu -nostdlibinc -fsyntax-only -DONLY %s 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: not %clang --target=wasm32-unknown-unknown -nostdlibinc -fsyntax-only -DONLY %s 2>&1 | FileCheck %s --check-prefix=OFF
// OFF: {{.*}}cli{{[/\\]}}cli.h:{{[0-9]+}}:{{[0-9]+}}: error: {{"?}}<cli/cli.h> is only available on the MSIL targets{{"?}}{{$}}

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -c %s 2>&1 | FileCheck %s --check-prefix=NOBLOCKS --implicit-check-not='"-fblocks"'
// RUN: %clang -### --target=x86_64-unknown-linux-musl-msil -c %s 2>&1 | FileCheck %s --check-prefix=NOBLOCKS --implicit-check-not='"-fblocks"'
// RUN: %clang -### --target=aarch64-unknown-linux-gnu-msil -c %s 2>&1 | FileCheck %s --check-prefix=NOBLOCKS --implicit-check-not='"-fblocks"'
// RUN: %clang -### --target=aarch64-unknown-linux-musl-msil -c %s 2>&1 | FileCheck %s --check-prefix=NOBLOCKS --implicit-check-not='"-fblocks"'
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -c %s 2>&1 | FileCheck %s --check-prefix=NOBLOCKS --implicit-check-not='"-fblocks"'
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -c %s 2>&1 | FileCheck %s --check-prefix=NOBLOCKS --implicit-check-not='"-fblocks"'
// RUN: %clang -### --target=aarch64-pc-windows-msvc-msil -c %s 2>&1 | FileCheck %s --check-prefix=NOBLOCKS --implicit-check-not='"-fblocks"'
// RUN: %clang -### --target=aarch64-w64-windows-gnu-msil -c %s 2>&1 | FileCheck %s --check-prefix=NOBLOCKS --implicit-check-not='"-fblocks"'
// RUN: %clang -### --target=msil64-unknown-dotnet -c %s 2>&1 | FileCheck %s --check-prefix=NOBLOCKS --implicit-check-not='"-fblocks"'
// RUN: %clang -### --target=msil32-unknown-dotnet -c %s 2>&1 | FileCheck %s --check-prefix=NOBLOCKS --implicit-check-not='"-fblocks"'
// NOBLOCKS: "-cc1" "-triple" "{{[^"]+}}"
// RUN: %clang -### --target=x86_64-apple-macosx10.15 -c %s 2>&1 | FileCheck %s --check-prefix=APPLE
// APPLE: "-cc1" {{.*}}"-fblocks"

#include <cli/cli.h>

#if defined(ONLY)

// <cli/cli.h> alone: no declarations and no C library macros.
#if defined(NULL) || defined(offsetof) || defined(bool) || defined(true) || defined(SIZE_MAX)
#error "<cli/cli.h> defines more than CLI_TYPE"
#endif

#else

// expected-no-diagnostics

#if !defined(CLI_TYPE)
#error "<cli/cli.h> defines CLI_TYPE"
#endif
#if !__has_include(<cli/cli.h>) || __has_include(<cli.h>)
#error "<cli/cli.h> is in the resource directory's 'cli' folder"
#endif

typedef struct CLI_TYPE("System.String") String String;
typedef struct CLI_TYPE("System.Object") Object Object;
struct CLI_TYPE("string[]") Strings;
_Static_assert(__builtin_types_compatible_p(String ^, struct String ^), "CLI_TYPE marks a class struct");

static Object ^upcast(String ^s) { return s; }
int use(void) { return upcast((String ^)0) == (Object ^)0; }

#endif
