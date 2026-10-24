# Getting started with the MSIL backend

This fork of llvm-project adds an experimental LLVM target, `MSIL`, that compiles C to IL-only
.NET assemblies (the design is in [Design.md](Design.md)). Phase 1 (the "first milestone" of
Design.md section 12) compiles one C file to ILAsm text with `clang -S`; `ilasm` turns the text
into an assembly, and `dotnet` (or .NET Framework 4.8) runs it. Since milestone M14 a program
can also have several files and archives: `clang -c` writes LLVM bitcode, and the linker
`msil-ld` links it into one ILAsm module, which `ilasm` assembles (section 3, "Programs of
several files"). Since M15 `msil-ld` binds the functions that no file defines to the native
libraries a native link of the same program reads, so a linked program calls the C library, the
Win32 API and native libraries of your own through P/Invoke (section 3, "Native libraries and
P/Invokes"). There are no CIL object files yet (M19), and `msil-ld` writes no assembly image of
its own yet (M20); since M17 the tools `llvm-cil-dump` and `llvm-cil-copy` read assemblies and
write them again without ilasm (section 6). What works and what does not is in
[Status.md](Status.md); how the work was planned and built is in
[ImplementationPlan.md](ImplementationPlan.md).

Like LLVM itself, the project ships compiler tools only (Design.md, scope line). The compiler
writes the `.il` file and nothing else: no `runtimeconfig.json`, no `.exe.config`, no project or
package files. Section 5 shows how to write the runtimeconfig yourself.

## 1. What you need

| For | Windows | Linux |
|---|---|---|
| Building | Visual Studio 2026 with the bundled clang-cl, lld-link and llvm-lib (any C++17 compiler works with the plain CMake route), CMake, Ninja, Python 3 | a C++17 compiler (clang or gcc), CMake, Ninja, Python 3 |
| Assembling | the .NET Framework ilasm, `%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe` (always present), or the NuGet `runtime.win-x64.Microsoft.NETCore.ILAsm` | the NuGet package `runtime.linux-x64.Microsoft.NETCore.ILAsm` (10.0.12 is what the tests use) |
| Running | .NET 8 or later; .NET Framework 4.8 for Windows triples | .NET 8 or later |
| Running the tests | everything above, plus a .NET SDK with the `Microsoft.NETCore.App.Ref` 8.x targeting pack (the .NET 8 SDK has it; it builds the `jitall` checker and the metadata oracles `mdump` and `mdcanon`, offline), the dotnet tool `dotnet-ilverify`, ildasm and PEVerify (Windows SDK, `NETFX 4.8 Tools`), the Visual Studio "C++ Clang tools" (their compiler-rt builtins and `ld.lld` serve the native oracles), and optionally WSL Ubuntu with python3 and curl or wget for the Linux legs (the legs' own .NET runtimes and Linux libraries come from the host setup of section 7, without root) | everything above, plus the .NET SDK (with the `Microsoft.NETCore.App.Ref` 8.x targeting pack), `dotnet-ilverify` and the NuGet `runtime.linux-x64.Microsoft.NETCore.ILDAsm` |

The NuGet ilasm and ildasm do not need a .NET project of yours. The repository has a
download-only project for them:

```sh
dotnet restore msil/utils/tools/msil-tools.proj -p:MsilToolsRid=linux-x64   # or win-x64
# ilasm: <NuGet global packages>/runtime.linux-x64.microsoft.netcore.ilasm/10.0.12/runtimes/linux-x64/native/ilasm
chmod +x ~/.nuget/packages/runtime.linux-x64.microsoft.netcore.il*/10.0.12/runtimes/linux-x64/native/il*
dotnet tool update --global dotnet-ilverify --version 10.0.12
```

NuGet does not keep the executable bit, hence the `chmod`. `dotnet nuget locals global-packages
--list` prints the packages folder when it is not `~/.nuget/packages`.

## 2. Configure and build

The MSIL target is experimental: it goes in `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD`, never in
`LLVM_TARGETS_TO_BUILD` (CMake fails if it is there). Keep the X86 target: clang passes x86
backend options for x86_64 triples, and the tests build every program natively as well (the
*native oracle*). The `msil/` directory (execution tests and their tools) is an LLVM external
project.

On any host, the cache file sets all of that (Release, assertions, clang and lld, X86,
experimental MSIL, the `msil/` project):

```sh
cmake -G Ninja -S llvm -B build -C msil/cmake/caches/MSIL.cmake
ninja -C build clang llc lld llvm-ar
```

`lld` also makes `msil-ld`, the linker that `clang` runs for MSIL triples, and `llvm-ar`
makes archives of bitcode (section 3); `clang` and `llc` alone are enough for `clang -S`.

Without the cache file, the essential options are:

```sh
cmake -G Ninja -S llvm -B build -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_ENABLE_PROJECTS='clang;lld' -DLLVM_TARGETS_TO_BUILD=X86 \
  -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MSIL \
  -DLLVM_EXTERNAL_PROJECTS=msil -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=$PWD/msil
```

On the Windows development host the PowerShell helpers in `msil/utils/build/` configure the same
tree with the Visual Studio clang-cl toolchain and serialize builds through a lock (see
[msil/utils/build/README.md](../../../msil/utils/build/README.md)):

```powershell
pwsh -NoProfile -File msil/utils/build/msil-configure.ps1 -Experimental MSIL
pwsh -NoProfile -File msil/utils/build/msil-build.ps1 -Standard
```

The second line builds everything the tests need: with `-Standard`, `msil-build.ps1` builds the
targets of `msil/utils/ci/build-targets.txt`, in the file's order, and then any target named
after the switch (`msil-build.ps1 -Standard llvm-dwarfdump`). That file is the one build-target
list of the gates and of CI (plan 5.6.11): `msil/utils/ci/build.ps1` builds it the same way,
and on Linux `bash msil/utils/ci/build.sh build` builds it with ninja (both then build jitall,
section 7). Its comments say why each target is there. Since milestone M17 it holds:

- the tools and lit helpers: `llc`, `opt`, `clang`, `FileCheck`, `not`, `count`, `split-file`,
  `llvm-config`, and `llvm-profdata`, which two upstream clang driver tests of the
  upstream-hooks check need (section 7);
- the unit tests `TargetParserTests`, `MCTests`, `MSILTests`, `CLITests` and `LLDAsLibAllTests`
  (`LLVM_BUILD_TESTS` is off, so unit tests are built only when named);
- `lld`, which also makes its copies `msil-ld`, `ld.lld`, `lld-link`, `ld64.lld` and `wasm-ld`;
- `llvm-ar` and its copies `llvm-ranlib`, `llvm-lib` and `llvm-dlltool`, each a target of its
  own that `ninja llvm-ar` does not build (without them lit would find the Visual Studio
  `llvm-lib` on `PATH`);
- `llvm-nm`, `llvm-objdump`, `llvm-readobj`, `llvm-as`, `llvm-bcanalyzer` and `llvm-lto2`, for
  the upstream tests of hooked upstream files and for the MSIL tests of the bitcode symbol table;
- `yaml2obj` and `llvm-mc` (M15), which make the native libraries of the `msil-ld` tests in
  `lld/test/MSIL`: ELF shared objects with symbol versions, and MinGW's long-format import
  members (`yaml2obj` also makes the images of the `llvm-cil-dump` and `llvm-cil-copy` tests);
- `llvm-cil-dump` and `llvm-cil-copy` (M17), the tools of the ECMA-335 library in
  `llvm/lib/CLI` (section 6).

A first build compiles about 3,700 steps; afterwards a change in `llvm/lib/Target/MSIL` rebuilds
the target and relinks in minutes. `llc --version` lists `msil - MSIL (.NET CIL as ILAsm text)`
when the target is in.

## 3. Compile a C program

Programs are C, in one file with `clang -S` or in several that `msil-ld` links (below). With
`clang -S` the only library functions a program may call are the standalone imports `puts`,
`putchar` and the C math library (Status.md lists them per triple) and functions declared with
`import_module` (optionally with `import_name` for the entry point; below). A program that
`msil-ld` links may also call the functions of the native libraries of the link, the C library
and the Win32 API among them, except variadic ones such as `printf` and those that take a
function pointer, such as `qsort` (M15; below, "Native libraries and P/Invokes"). Declare them
yourself or use clang's freestanding headers (`stdint.h`, `stddef.h`, `stdbool.h`, `limits.h`,
`float.h`). A C library's own headers work through the native triple's header search
(`--sysroot`, the `INCLUDE` of a Visual Studio developer prompt; the `hosted` leg of section 7
compiles glibc's and the UCRT's), but the examples of this guide are freestanding.

```c
// fib.c
int puts(const char *s);

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

int main(void) {
  char buf[16];
  char *p = buf + sizeof buf - 1;
  int v = fib(20);
  *p = 0;
  do {
    *--p = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  puts(p);          /* prints 6765 */
  return fib(10);   /* exit status 55 */
}
```

Compile it to ILAsm text with the triple of the platform the program will run on:

```sh
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S fib.c -o fib.il   # Linux (glibc)
clang --target=x86_64-pc-windows-msvc-msil   -O2 -ffreestanding -S fib.c -o fib.il   # Windows (UCRT)
```

- An MSIL triple is the native triple plus `-msil`. clang uses the native target's ABI and
  predefines (`__x86_64__`, `__linux__` or `_WIN64`, ...) and adds `__MSIL__`; `long double` is
  `double` on every MSIL triple (natively it is wider on Linux and x86_64 MinGW; Status.md
  section 3). With `-S` the P/Invoke library names come from the triple (`libc` and `libm.so.6`
  on glibc, `ucrtbase` on windows-msvc), and `msil-ld` takes them from the libraries it links
  (`libc.so.6`, `api-ms-win-crt-stdio-l1-1-0.dll`, ...): either way, build for the OS you run
  on.
- Every optimization level works (`-O0` to `-O3`, `-Os`, `-Oz`); the execution tests run at
  `-O0` and `-O2` in most legs, and the `more-levels` leg runs most of them at the other levels
  (C programs at all four, IR tests at `-O1` and `-O3`; section 7).
- `-ffreestanding` keeps the optimizer from introducing libc calls that `-S` cannot import
  (`msil-ld` binds those that LTO introduces, such as `puts` for `printf("...\n")`, natively).
  It also turns off `main`'s implicit `return 0` on Linux triples, so return explicitly.
- WebAssembly's import attributes import a function from a native library you name (milestone
  M13): `int beep(int) __attribute__((import_module("mylib"), import_name("real_beep")));`
  becomes `pinvokeimpl("mylib" as "real_beep" cdecl)`; without `import_name` the entry point is
  the C name. `import_name` alone makes no other function an import in `-S` and `llc` output:
  the function is still `not a Phase-1 import`. `msil-ld` keeps an `import_module` as given,
  unchecked, and looks the entry point of an `import_name` alone up in the native libraries of
  the link (below). On a Phase-1 import (`puts`, `putchar`, the C math functions)
  `import_name` alone renames the entry point imported from that function's Phase-1 library:
  `double cbrt(double) __attribute__((import_name("my_cbrt")));` becomes
  `pinvokeimpl("libm.so.6" as "my_cbrt" cdecl)` on glibc. An attribute written
  on a definition is ignored, with a warning when clang generates IR (`-S`, `-emit-llvm`; not
  with `-fsyntax-only`); one that a definition inherits from an earlier declaration is dropped
  without a diagnostic, as on WebAssembly (Status.md section 2).
- The assembly is named after the source file's stem (`fib`). `-mllvm -msil-assembly-name=<name>`
  sets another name. `-mllvm` passes backend options as it does for native targets (llc's
  `-print-after=`, `-debug-pass=Structure`, `-verify-machineinstrs`, ...; section 8).
- `-c` writes LLVM bitcode (below); without `-S` or `-c`, clang compiles and links through
  `msil-ld`. `-S` writes the same IL as before M14. Its cc1 line now has
  `-mrelocation-model static` instead of PIC and no `-funwind-tables=2`, so the IR of `-S
  -emit-llvm` lacks the `PIC Level` and `PIE Level` module flags (and `uwtable` where the
  native toolchain asked for unwind tables).
  `-fno-lto -c` stops with `CIL object files are not supported for MSIL target '...' yet; use
  -flto to write LLVM bitcode objects, or -S to write ILAsm text`, because CIL object files
  come in M19 (plan section 6); before M14 every `-c` and every link stopped there (before M13
  with `emitting object files is not yet supported for MSIL target '...'`). ILAsm text is not
  an assembler input: `-x assembler`, `.s` files, `-via-file-asm` and clang-cl's `/FA` and
  `/Fa` (which ask for an assemble step; an explicit `-flto` ignores these three, as upstream
  LTO does) stop with `assembling ILAsm text is not supported for MSIL target '...'; compile C
  or LLVM IR sources instead, or use -S to write ILAsm text`, and `-x assembler` and `.s` files
  with `-fno-integrated-as` stop with `there is no external assembler that can be used on this
  platform`.
- `llc` works on IR as usual: `clang --target=<triple> -O2 -ffreestanding -S -emit-llvm fib.c -o
  fib.ll`, then `llc fib.ll -o fib.il` (llc takes the triple from the module, or `-mtriple=`).

The output is one ILAsm module: an `.assembly extern mscorlib` reference, the assembly, one
`.method` per C function, P/Invoke declarations for the imports, the data segment and module
initializer (`<Module>::.cctor`) for global variables, and a generated `'<msil>Main'` entry point
that calls `main` (with `argc`/`argv` when `main` takes them).

### Programs of several files: `clang -c`, archives and `msil-ld`

From milestone M14, `clang -c` writes an LLVM bitcode object on every MSIL triple, `llvm-ar`
collects such objects into archives, and a link through clang runs `msil-ld`, which links the
bitcode with LTO and writes the whole program as one ILAsm module. `ilasm -exe` assembles it,
and the `.exe` runs on modern .NET and on .NET Framework 4.8 alike:

```sh
# main.c calls fib (fib.c), bump (bump.c) and puts; nothing calls unused.c.
clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -c main.c fib.c bump.c unused.c
llvm-ar rcs libutil.a bump.o unused.o        # the tree's build/bin/llvm-ar
clang --target=x86_64-pc-windows-msvc-msil -O2 main.o fib.o -L. -lutil \
  -Wl,--lto-emit-asm -o prog.il
ilasm -nologo -quiet -exe -pe64 -x64 -output=prog.exe prog.il
dotnet prog.exe       # with prog.runtimeconfig.json next to it (section 5)
prog.exe              # .NET Framework 4.8 (Windows triples)
```

- **Compiling.** `-c` runs `cc1 -emit-llvm-bc -flto=full` and writes bitcode to `<name>.o`, as
  if `-flto` were given (plan Q6; CIL objects replace it in M19). An explicit `-flto=...` or
  `-fno-lto` decides instead: `-flto=thin` writes ThinLTO bitcode, which `msil-ld` rejects
  until M20, and `-fno-lto -c` is the error above. `clang a.c b.c -Wl,--lto-emit-asm -o
  prog.il` compiles and links in one step.
- **Archives.** Use the tree's `llvm-ar` (`clang --emit-static-lib` runs it too): the Visual
  Studio `llvm-ar` is older and cannot index this LLVM's bitcode. A member is linked only when
  it defines a symbol that is still undefined (lld's rules), so `unused.o` above stays out (with
  its constructor). One exception, as in lld: before LTO, `msil-ld` extracts every bitcode
  member that defines a runtime library-call name of the target (`fmod`, `floor`, `memcpy`,
  ...), even when nothing references it. Such a member's constructors therefore run, and its
  other definitions and references join the link, which can give `duplicate symbol` or
  `undefined symbol` errors that a native static link without LTO would not give; native LTO
  through lld behaves the same. `-Wl,--whole-archive` links every member, and the objects
  between `-Wl,--start-lib` and `-Wl,--end-lib` behave as members of one archive. `-l<name>`
  searches the `-L` directories in order for `lib<name>.a` (on Windows triples also `<name>.lib`
  and DLL names; on Linux triples `lib<name>.so`); in the first directory that has one, an
  archive with bitcode members wins over a native library, and `-l:<file>` takes that exact
  file (the whole rule is under "Native libraries and P/Invokes" below).
- **Linking.** Every MSIL link runs `msil-ld`, the copy of `lld` next to clang
  (`build/bin/msil-ld`), never `ld`, `link.exe` or `lld-link`. `-fuse-ld=lld`, `-fuse-ld=ld` and
  `-fuse-ld=msil-ld` name it too, `--ld-path=<program>` and an absolute `-fuse-ld=<path>` run
  another one, and any other `-fuse-ld` value is `invalid linker name`. Without `-o` the output
  is `a.exe` on every MSIL triple. An MSIL triple that is not a Linux, windows-msvc or
  windows-gnu one (Darwin, FreeBSD, Cygwin, ...) ends with `unknown target triple '<triple>'`,
  also when the command only links, and never runs a native linker. `clang -###` shows the
  command: `msil-ld --target=<triple> [--sysroot=<dir>] -L<dir>... [--lto-O<n>] <objects,
  archives, -l and -Wl options in command-line order> <default libraries> -o <output>`
  (`-shared` adds `--shared`, which `msil-ld` rejects until M22; `-s` adds `--strip-all`, which
  it ignores).
- **`--lto-emit-asm` is required in M14.** `msil-ld` merges every bitcode input into one LTO
  module and, with `--lto-emit-asm`, writes that module's ILAsm text to `-o`. Writing the
  assembly image itself is M20's, so every other link stops with `msil-ld: error: ThinLTO,
  --lto-partitions and LTO without --lto-emit-asm need CIL object output, which this msil-ld
  does not have; use --lto-emit-asm (clang: -Wl,--lto-emit-asm) to write ILAsm text`. clang
  never adds `--lto-emit-asm` and never runs ilasm itself.
- **Optimization.** The link's `-O` is the LTO level (`-O0` gives `--lto-O0`, `-O1` and `-Og`
  `--lto-O1`, `-O2`, `-Os` and `-Oz` `--lto-O2`, `-O3` and `-Ofast` `--lto-O3`; without `-O`
  `msil-ld` uses `--lto-O2`), so give the link the level of the compiles. Only `main` and the
  `-u` symbols stay visible: every other function and variable becomes internal to the program
  (functions are `assembly` methods), so inlining and the other interprocedural optimizations
  work across files. On the link line, `-mllvm <option>` reaches the code generator inside
  `msil-ld` (as `--mllvm=<option>`), and `-save-temps` keeps LTO's bitcode after each stage
  (`<output>.0.0.preopt.bc`, `.0.2.internalize.bc`, `.0.4.opt.bc`, `.0.5.precodegen.bc`) and
  its symbol resolutions (`<output>.resolution.txt`). `.0.5.precodegen.bc` is written after
  `msil-ld` has checked the native bindings (below) but before it annotates the module with
  them, so it has no `msil-linked` flag, and it is not written when that check reports an
  error.
- **The assembly name** is the stem of `-o` (`prog`; never LTO's `ld-temp`), or
  `-Wl,--assembly-name=<name>` (the same as `-Wl,-soname,<name>`); `-mllvm
  -msil-assembly-name=<name>` on the link line overrides both. `msil-ld` hands the name to the
  backend in the module flag `msil-assembly-name` (D28 as amended for M14).
- **Symbols** resolve as in a static link by lld (plan 5.9.5, a port of lld/ELF): a strong
  definition beats a weak one and a common symbol, a common symbol beats a weak definition,
  common symbols of one name become one variable, an alias resolves as a definition of its
  kind, a COMDAT group (an inline function, an MSVC string literal, `__declspec(selectany)`
  data) keeps the first input's copy, and a weak undefined reference stays null and extracts
  no archive member. Two strong definitions are `duplicate symbol: <name>`, followed by one
  `>>> defined in <file>` line per definition (`-Wl,--allow-multiple-definition` keeps the
  first); a function and a variable of one name are `symbol type mismatch: <name>`; a
  thread-local and a plain variable of one name are `TLS attribute mismatch: <name>`.
  `-Wl,--warn-common` warns about common symbols.
- **Undefined symbols.** A function that no input defines binds to a native library of the
  link (below). A symbol that binds nowhere is `msil-ld: error: undefined symbol: <name>`,
  followed by `>>> referenced by <file>:(<function>)` lines. `msil-ld` reports it after LTO,
  so only the references that the optimized program still makes count, and a name that only
  the optimizer introduced is `>>> referenced by ld-temp.o`. A hidden reference is `undefined
  hidden symbol: <name>`, reported before LTO. (In M14, before native libraries, such a
  function could bind only through the Phase-1 import table of `-S`.)
- **Default libraries.** Unless `-nostdlib` or `-nodefaultlibs` is given, clang adds the C
  library of the triple (plan 5.10.4), as a native link does. `-nostartfiles` is accepted
  silently, because MSIL has no crt files (as are `-static-libgcc` on the Linux and MinGW
  triples and `-shared-libgcc` on the Linux ones; no libgcc is linked):

  | Triple | Default libraries |
  |---|---|
  | `*-linux-gnu-msil` | `-lc -lm` (with `-pthread`, `-lpthread` first) |
  | `*-linux-musl-msil` | `-lc` |
  | `*-windows-msvc-msil` | `-lucrt -lkernel32 -loldnames --ucrtbase-imports` (M15 added `--ucrtbase-imports`; below) |
  | `*-windows-gnu-msil` | `-lmsvcrt -ladvapi32 -lshell32 -luser32 -lkernel32` (with `-pthread`, `-lpthread` after `-lmsvcrt`, also with `-nolibc`); a `-lmsvcr*`, `-lucrt*` or `-lcrtdll*` of yours takes the place of `-lmsvcrt` |

  Since M15 `msil-ld` reads these libraries and binds the program's library calls in them
  (below); in M14 it only had to find them. A library that is not found is `unable to find
  library -l<name>`. On windows-msvc, clang passes each `%LIB%` directory of a Visual Studio
  developer prompt as `-L` (`msil-ld`, a GNU-style linker, does not read `LIB`), and otherwise
  the VC, UCRT and Windows SDK library directories it detects or that `/winsysroot`-style
  options name. A Windows host has no Linux C library, so for a Linux triple give clang
  `--sysroot=<dir>` of a Linux system: the E7 sysroot that `make-linux-sysroot.py` writes
  (section 7; `libc.so.6`, `libm.so.6` and the scripts `libc.so` and `libm.so`, which is what
  the test harness links with), the glibc sysroot of `make-glibc-sysroot.sh` (section 7;
  glibc's real headers and scripts, `libc_nonshared.a` included), or a Linux root. On
  windows-gnu `msil-ld` needs the mingw-w64 import libraries, from a `--sysroot` or `-L`
  directory. `-nodefaultlibs` leaves the libraries out, for a program that calls no native
  function (the test harness linked that way in M14). Header search is the native triple's
  too (Phase 2 needs no hosted headers, plan Q16): `--sysroot`, a developer prompt's
  `INCLUDE`, `-Xmicrosoft-windows-sys-root` or clang-cl's `/winsysroot`; the freestanding
  programs of this guide need none.
- **Stock-clang bitcode** (plan Q12). Bitcode that clang writes for the native triple
  (`clang --target=x86_64-pc-windows-msvc -flto -c`) links too: `msil-ld` compiles it for the
  MSIL triple with the rest and warns once per link, `<file>: linking bitcode for <native
  triple> as <msil triple>; stock-clang bitcode is a compatibility path`. Its limits (recon
  `p2/lld-flavor.md` 3.3): on windows-msvc compile it with `-fno-ms-volatile`, or its
  `volatile` accesses are atomics and the link stops with `runtime helper '__atomic_load_4' is
  not available for MSIL yet (phase 3)` (atomics are phase 3, plan D36); in stock bitcode for
  x86_64 Linux and MinGW `long double` is `x86_fp80`, which the backend rejects where code uses
  it, and a struct that contains one has another layout than in MSIL files, without a
  diagnostic; the file sees the native predefines (no `__MSIL__`, and `__ELF__` on Linux); and
  the native cost model vectorized it, which makes its IL larger (by about a quarter in the
  recon's corpus). Every input must have the link's architecture, OS and environment (the
  vendor and the MSVC version do not count), or it is `<file>: target triple <t> is
  incompatible with <t0>`; the link's triple is clang's `--target`, else the first bitcode
  input's with `-msil` appended to a native triple. That triple must be one of D6's (x86_64 or
  aarch64 on Linux, windows-msvc or windows-gnu): any other, such as that of stock bitcode for
  Darwin (`x86_64-apple-macosx`) or i686, stops the link before the checks of each input with
  `msil-ld: error: unsupported target triple '<codegen triple>' for MSIL`.
- **Other inputs.** A .NET assembly or a CIL object is `<file>: linking CIL object files and
  .NET assemblies is not supported yet` (M20-M22), and anything that is neither bitcode, an
  archive, a native library nor a GNU ld script (an `.il` file, a native object file) is
  `unknown file type: <file>`. Native libraries and linker scripts are inputs like any other
  since M15 (below).

`msil-ld`'s options in M15, with lld's spellings (pass them through clang as `-Wl,<option>`;
plan 5.9.3):

| Option | Meaning |
|---|---|
| `-o <file>` | the output; with `--lto-emit-asm` ILAsm text, which ilasm assembles with `-exe` |
| `--target=<triple>` | the code-generation triple (clang always passes it); one of D6's MSIL triples, else `unsupported target triple '<triple>' for MSIL` |
| `-L <dir>`, `-l <name>`, `-l:<file>`, `--sysroot=<dir>` | library search as above and under "Native libraries and P/Invokes" below; `-L=<dir>` is relative to `--sysroot` |
| `-Bstatic`, `-Bdynamic` | after `-Bstatic`, `-l` takes only static libraries (archives with bitcode members) and skips native ones; `-Bdynamic`, the default, takes both again (M15) |
| `--ucrtbase-imports` | a built-in import library, read where the option stands, that binds `memcpy`, `memset`, `memmove`, `memcmp`, `memchr`, `strchr`, `strrchr`, `strstr`, `wcschr`, `wcsrchr` and `wcsstr` to `ucrtbase.dll`; Windows targets only (M15) |
| `--print-imports` | print each native binding and the version floor of each ELF library (M15; below) |
| `--no-dependent-libraries`, `--dependent-libraries` | ignore, or read (the default), the linker options of the inputs: `#pragma comment(lib)`, `/DEFAULTLIB`, ... (M15) |
| `--whole-archive`, `--no-whole-archive`, `--start-lib`, `--end-lib` | as in lld |
| `-u <sym>`, `--undefined=<sym>` | a reference to `<sym>`: it extracts the archive member that defines it and stays visible after LTO |
| `-e <sym>`, `--entry=<sym>` | only `main` until M20: `--entry=<sym>: an entry point other than main is not supported yet` |
| `--lto-emit-asm` | write the program's ILAsm text to `-o` (required in M14) |
| `--lto-O<n>`, `--lto-CGO<n>`, `--save-temps`, `--disable-verify`, `-mllvm <option>`, `--mllvm=<option>` | LTO, as in lld; `--lto-partitions=<n>`, `--thinlto-jobs=<n>` and `--thinlto-cache-dir=<dir>` are accepted, but more than one partition, and ThinLTO, are M20's |
| `--assembly-name=<name>`, `-soname <name>` | the assembly name (default: the stem of `-o`) |
| `-Map=<file>`, `--print-map`, `-M` | the link map: the inputs in load order, native libraries included, then each defined symbol with its kind, binding and input, and each natively bound one (`NativeFunction`, `NativeData`) with its library |
| `--why-extract=<file>` | why each archive member was linked, in lld's format (`-` is standard output) |
| `--reproduce=<file>` | a tar of every input with a `response.txt` that links them again, as lld writes it |
| `--trace`, `--trace-symbol=<sym>`, `-y <sym>` | print each input file and extracted archive member, and each reference to and definition of `<sym>`, a native one as `<library>: shared definition of <sym>` |
| `--allow-multiple-definition`, `--warn-common`, `--fatal-warnings`, `--error-limit=<n>`, `--verbose`, `--color-diagnostics` | as in lld; `--verbose` also notes each skipped native archive and each ignored C runtime library request |
| `-s`, `--strip-all` | accepted and ignored |
| `-shared`, `--shared` | `-shared is not supported yet` (library assemblies are M22's) |
| `-r`, `--relocatable` | always `-r is not supported` |
| `--version`, `-v`, `--help` | lld's version, and this list |

Every other option is `unknown argument: <option>` until the milestone that adds it: for
example `--gc-sections` (M20) and `--reference=` (M21). The map of the example above
(`-Wl,-Map=prog.map`, from a developer prompt; the library paths are shortened), in which
`main` also prints through `puts`:

```text
msil-ld map of prog.il (x86_64-pc-windows-msvc-msil)
Inputs:
  main.o
  fib.o
  .\libutil.a(bump.o)
  <SDK>\lib\<ver>\ucrt\x64\ucrt.lib
  <SDK>\lib\<ver>\um\x64\kernel32.lib
  <VC>\lib\x64\oldnames.lib
  --ucrtbase-imports
Symbols:
  Kind            Binding Input                             Symbol
  DefinedFunction Global  main.o                            main
  DefinedData     Global  main.o                            counter
  DefinedData     Weak    main.o                            ??_C@_03NADLHBLG@fib?$AA@
  DefinedFunction Global  fib.o                             fib
  DefinedFunction Global  .\libutil.a(bump.o)               bump
  NativeFunction  Global  <SDK>\lib\<ver>\ucrt\x64\ucrt.lib puts
```

The default libraries are inputs even when nothing binds to them; `??_C@...` is the MSVC
string literal `"fib"`, a COMDAT.

### Native libraries and P/Invokes (M15)

A function that no input of the link defines becomes a P/Invoke of the native library that
defines it, found the way a native link of the same program finds it: the driver's default
libraries, the `-l` options and library files on the command line, and the libraries that the
program names with `#pragma comment(lib)`. `-Wl,--print-imports` shows each binding:

```sh
# Windows triple, from a Visual Studio x64 developer prompt (LIB):
clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -c main.c
clang --target=x86_64-pc-windows-msvc-msil -O2 main.o -Wl,--print-imports \
  -Wl,--lto-emit-asm -o prog.il
# Linux triple, on Windows or Linux: a sysroot with the C library (section 7).
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -c main.c
clang --target=x86_64-unknown-linux-gnu-msil -O2 main.o \
  --sysroot=C:/llvm-msil-scratch/linux-sysroot -Wl,--print-imports \
  -Wl,--lto-emit-asm -o prog.il
```

For a `main.c` that calls `puts`, `strlen`, `strdup` and `sqrt`, the two links print:

```text
puts -> api-ms-win-crt-stdio-l1-1-0.dll!puts [function, -]
sqrt -> api-ms-win-crt-math-l1-1-0.dll!sqrt [function, -]
strdup -> api-ms-win-crt-string-l1-1-0.dll!_strdup [function, -]
strlen -> api-ms-win-crt-string-l1-1-0.dll!strlen [function, -]
```

```text
puts -> libc.so.6!puts [function, GLIBC_2.2.5]
sqrt -> libm.so.6!sqrt [function, GLIBC_2.2.5]
strdup -> libc.so.6!strdup [function, GLIBC_2.2.5]
strlen -> libc.so.6!strlen [function, GLIBC_2.2.5]
libc.so.6: version floor GLIBC_2.2.5
libm.so.6: version floor GLIBC_2.2.5
```

and `prog.il` declares, for example on Windows,

```text
.method assembly hidebysig static pinvokeimpl("api-ms-win-crt-string-l1-1-0.dll" as "_strdup" cdecl) native int 'strdup'(native int) cil managed preservesig {}
```

- **What `msil-ld` reads.** The native libraries of the triple's object format, given on the
  command line or found by `-l`: on the Linux triples ELF shared objects (`.so`) and GNU ld
  scripts (glibc's `libc.so`); on the Windows triples Windows import libraries (`.lib`, and
  MinGW's `.dll.a`), `oldnames.lib`'s aliases and native DLLs; and archives of native objects.
  A library of the other format is an error, as in lld-link and ld.lld: `<path>: ELF file
  cannot be imported for target <triple>` on a Windows triple, `<path>: PE/COFF file cannot be
  imported for target <triple>` on a Linux one (section 8); `-l` passes over it. Their machine
  (and ELF class) must match the triple. Pass a library of your own as a file (`libnative.so`
  on Linux, `native.lib` or `native.dll` on Windows) or as `-L<dir> -lnative`.
- **`-l<name>`** searches the `-L` directories in order (the driver adds the triple's library
  directories after yours) for, on windows-msvc, `lib<name>.a`, `<name>.lib` and `<name>.dll`;
  on windows-gnu also `lib<name>.dll.a`, `<name>.dll.a`, `lib<name>.dll`; on Linux
  `lib<name>.a` and `lib<name>.so` (`<name>.dll` and `lib<name>.dll` count there only as .NET
  libraries, M22's, so `-lfoo` takes `libfoo.so` over a native `foo.dll`, as ld.lld does).
  The first directory that has an acceptable one wins, and in it an archive with bitcode
  members wins over an import library or a shared library (Design.md 5.2: static first, the
  opposite of GNU ld's default). An archive of native objects only is skipped
  (`-Wl,--verbose` notes `skipping native static archive <path> (no CIL or bitcode
  members)`), and so is a library of the other object format (`skipping <path>: ...`) and a
  .NET assembly, with a warning (`<path>: skipping the .NET assembly that -l<name> found; use
  --reference for .NET reference assemblies`). `-Wl,-Bstatic` makes the following `-l`
  options take static libraries only, until `-Wl,-Bdynamic`; `-l:<file>` takes that exact
  file of any kind (one of the other object format is the error above).
- **Linker scripts** are read as lld/ELF reads them (`GROUP`, `INPUT`, `AS_NEEDED`, `-l`): a
  name in a script is looked up under the sysroot when it starts with `=`, or when it is
  absolute and the script lies inside the sysroot (so glibc's `libc.so`, which names
  `/usr/lib/x86_64-linux-gnu/libc.so.6`, works under `--sysroot`); a relative name in the
  script's own directory, the current directory, then the `-L` directories. A missing one is
  lld/ELF's `unable to find <name>` error, and an input that leads back to a script still
  being read is `<script>:<line>: there is a cycle in linker script inputs` (section 8).
- **Which definition binds.** Definitions in the program and its archives first; then the
  native libraries in command-line order, where the first bindable definition wins. A
  P/Invoke can only reach what `dlsym` or `GetProcAddress` reaches: the default (`@@`) or
  unversioned version of an ELF symbol, never thread-local data or `GLIBC_PRIVATE`; a COFF
  import of the right kind. A program's own `malloc` (or any other name) wins over the
  library's for the program's code; native code keeps calling its own.
- **Module and entry names** are those a native link records (plan Q13): the `DT_SONAME`
  (`libc.so.6`), else the file name of a shared object; the DLL name of an import library
  exactly as it stores it (`KERNEL32.dll`, the API sets `api-ms-win-crt-*-l1-1-0.dll` of
  `ucrt.lib`); the file name of a DLL as `msil-ld` found it (`native.dll`), not the name in
  its export table; `#<n>` for an import by ordinal only (`htons` is `#9` of `WS2_32.dll`); the
  alias target of `oldnames.lib` (`strdup` is `_strdup`).
- **Default libraries.** On windows-msvc the driver's last library is `--ucrtbase-imports`,
  an import library built into `msil-ld` that binds the 11 memory and string functions that
  native programs take from vcruntime (`memcpy`, `memset`, `memmove`, `memcmp`, `memchr`,
  `strchr`, `strrchr`, `strstr`, `wcschr`, `wcsrchr`, `wcsstr`) to `ucrtbase.dll`, because
  `ucrt.lib` lacks them; `-lvcruntime` on your command line comes first and wins. On other
  targets the option is `--ucrtbase-imports needs a Windows target, not <triple>`.
- **Libraries the program names.** `#pragma comment(lib, "<name>")` (clang records
  `/DEFAULTLIB:<name>.lib` on Windows triples and `-l<name>` on Linux ones) and the other
  linker options in the bitcode are read when the file joins the link, an archive member's
  when it is extracted. Requests for the MSVC C runtime (`libcmt`, which clang names in every
  windows-msvc object, `msvcrt`, `ucrt`, `vcruntime`, ...) are ignored with a `--verbose`
  note, because the default libraries supply the C runtime; `/alternatename:` and
  `/failifmismatch:` work as in lld-link; any other option is ignored with the warning
  `ignoring linker option '<option>' in <file>`. `-Wl,--no-dependent-libraries` ignores them
  all.
- **Checked after LTO.** `msil-ld` binds what the program still calls after LTO, and reports
  there: an unbindable or unresolved function is a link error, with `>>> referenced by
  <file>:(<function>)` lines unless the code generator reports it (section 8 lists the texts),
  never a `DllNotFoundException` or `EntryPointNotFoundException` at run time. A call that the
  optimizer removed is not reported, and a library call that it introduced (`puts` for a
  `printf("...\n")`) is bound like any other.
- **What does not work yet** (Status.md section 2, "Native libraries and P/Invokes"): native
  variables (`environ`, `stderr`; M20), variadic native functions (`printf`), native functions
  that take or return a struct by value, an `__int128`, a vector or a `_Float16`, the native
  `long double` functions (`sqrtl`, `strtold`, ...) on x86_64 and aarch64 Linux and x86_64
  MinGW, `atexit`, and the native functions that take C function pointers (`qsort`, `bsearch`,
  `signal`, `pthread_create`, `CreateThread`, `_beginthreadex`, ...): all of them are link
  errors until phase 3 (native data until M20). A function address passed to any other native
  function (a library's own callback API, a Win32 callback) is not diagnosed and fails at run
  time until phase 3 (P3-B). For a call that passes no function of the program, such as
  `signal(SIGPIPE, SIG_IGN)`, declare the function with an explicit `import_module`, which
  `msil-ld` keeps as given.
- **Running.** The P/Invoke names only the library, so the library must be where the runtime's
  loader finds it: next to the program for a library of your own (.NET and .NET Framework load
  it from there; the test harness copies it there with `msil-run.py --dep`), the system's
  for the C library and the Win32 API. A P/Invoke has no symbol version: on Linux it binds the
  default version of the library that runs it, whatever version `msil-ld` saw (the version
  floor of `--print-imports` is the newest version the link bound in each library).
- **The linked text.** `msil-ld` annotates the module it compiles with the module flag
  `msil-linked`, the attributes `msil-import-module` and `msil-import-name` on each bound
  function and the named metadata `!msil.imports` for the library functions that code
  generation may call, and the backend prints P/Invokes from these alone, with `assembly`
  access (plan 5.10.13). `clang -S` and llc keep the standalone table and `public` P/Invokes.

## 4. Assemble with ilasm

```sh
ilasm -nologo -quiet -dll -pe64 -x64 -output=fib.dll fib.il      # x86_64 triples
ilasm -nologo -quiet -dll -pe64 -arm64 -output=fib.dll fib.il    # aarch64 triples
```

Always pass both `-pe64` and the machine: without them ilasm writes a 32-bit "AnyCPU" image that
a 32-bit runtime would load although the code assumes 64-bit pointers, and the Framework ilasm's
`-pe64` alone means Itanium. An arm64 image does not load on x64 (by design: the ABI is baked in
per triple). On Windows the Framework ilasm (`%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe`)
is the default the tests use; the NuGet ilasm 10.0.12 takes the same options and gives an
assembly that behaves the same.

For .NET Framework, build an `.exe` instead: `ilasm -nologo -quiet -exe -pe64 -x64
-output=fib.exe fib.il`. The `.exe` also runs on modern .NET (`dotnet fib.exe`, with a
`fib.runtimeconfig.json`), so one `.exe` serves every runtime; that is how the tests assemble
what `msil-ld` writes (section 3). `msil-ld` names the assembly after its own `-o`
(`prog.il` gives `prog`), so `-output=prog.exe` keeps the file and assembly names equal.

## 5. Write a runtimeconfig.json and run

`dotnet fib.dll` needs `fib.runtimeconfig.json` next to `fib.dll`; it names the shared framework
to run on. The compiler never writes it (Design.md section 8): like the C# compiler, it leaves
that to your build system, or to you. Without it `dotnet` stops with "The library
'hostpolicy.dll' (Linux: 'libhostpolicy.so') required to execute the application was not found"
and exit status `0x80008083` (131 on Linux, which keeps 8 bits, and in Git Bash).

```json
{
  "runtimeOptions": {
    "tfm": "net8.0",
    "framework": {
      "name": "Microsoft.NETCore.App",
      "version": "8.0.0"
    }
  }
}
```

Save that as `fib.runtimeconfig.json` (the stem of the `.dll` plus `.runtimeconfig.json`), then:

```sh
dotnet fib.dll; echo $?          # prints 6765, then 55
dotnet exec --runtimeconfig fib.runtimeconfig.json fib.dll   # same; the file may live anywhere
```

- `"version": "8.0.0"` runs on the newest installed 8.0.x. Add `"rollForward": "Major"` to fall
  back to a newer major version when .NET 8 is missing, or `"rollForward": "LatestMajor"` to
  always take the newest installed runtime. `tfm` is optional.
- The output (also `msil-ld`'s) references only `mscorlib`, so the same `.dll` runs on .NET 8, 9
  and 10, and the `.exe` on .NET Framework 4.8. A program that `msil-ld` linked against a
  native library of your own needs that library next to it when it runs (section 3, "Native
  libraries and P/Invokes").
- Arguments after the assembly reach `main`: `dotnet fib.dll a b` gives `argc` 3, and `argv[0]`
  is the path of the `.dll` (`Environment.GetCommandLineArgs()`).
- The exit status is `main`'s return value. Linux keeps only its low 8 bits, as for native
  programs. A trap (`__builtin_trap`, reaching `__builtin_unreachable`, or a call of a `weak`
  function that nothing defines) ends in an unhandled `System.NullReferenceException`.

.NET Framework 4.8 (Windows triples only) runs the `.exe` directly, with no configuration file:

```bat
fib.exe
echo %ERRORLEVEL%
```

On Linux, or in WSL, the same steps work with the Linux ilasm and a Linux triple:

```sh
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S fib.c -o fib.il
ilasm -nologo -quiet -dll -pe64 -x64 -output=fib.dll fib.il
cat > fib.runtimeconfig.json <<'EOF'
{ "runtimeOptions": { "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" },
                      "rollForward": "Major" } }
EOF
dotnet fib.dll; echo $?
```

From Windows, WSL can run the NuGet Linux ilasm in place from the Windows drive
(`/mnt/<drive>/...`): under WSL's default DrvFs mount, files on a Windows drive show as mode
0777. Only a distribution that mounts Windows drives with the `metadata` option and a
restrictive `fmask` (`/etc/wsl.conf`, `[automount] options`) makes the file non-executable; then
copy it into the Linux file system and `chmod +x` it. The test harness (`msil-run.py --stage`,
which `--param msil_wsl_ilasm` uses) always runs such a copy from a Linux temporary directory,
as a precaution.

## 6. Check an assembly

The test harness checks every assembly it builds. Its checkers also work on your own files:

```sh
# ILVerify with the allow-list, PEVerify (Windows), and a strict second pass over a
# neutralized ildasm/ilasm copy (Status.md, "Verification policy").
python msil/utils/msil-verify.py --ilverify ~/.dotnet/tools/ilverify \
  --runtime-dir "<dotnet>/shared/Microsoft.NETCore.App/8.0.<patch>" \
  --ildasm <ildasm> --ilasm <ilasm> [--peverify <PEVerify.exe>] fib.dll
# JIT-compile every method without running the program (needs the jitall build, section 7).
python msil/utils/msil-jitall.py fib.dll
```

Every module the backend writes has also passed `MSILStackCheck`, which recomputes the stack
depth of every instruction and the exact `.maxstack` and stops the compile on a mismatch.

### Read and rewrite an assembly: `llvm-cil-dump` and `llvm-cil-copy`

The ECMA-335 library of the fork (`llvm/lib/CLI`, M16 and M17; Status.md section 2) comes with
two tools (`ninja -C build llvm-cil-dump llvm-cil-copy`; both are on the build-target list).
`llvm-cil-dump` reads the PE images of .NET assemblies and netmodules, whoever wrote them, and
prints them in one or more modes, in this order whatever the order of the options:

```sh
llvm-cil-dump fib.exe                # = --canonical: canonical listing v1
llvm-cil-dump --headers fib.exe      # PE, COFF, CLI and metadata headers, streams, MVID
llvm-cil-dump --tables fib.exe       # every row of every metadata table, columns in ECMA order
llvm-cil-dump --link-info obj.o      # a CIL object's link-info record (M19 writes them)
llvm-cil-dump --canonical --headers a.exe b.dll   # several modes and files
```

- `--canonical` (the default mode) prints *canonical listing v1* (plan 5.7.10): the assembly's
  metadata and IL as text without anything that depends on how the image was laid out (tokens,
  heap offsets, RVAs, header forms, the MVID, time stamps), in UTF-8 with LF line ends, types
  and members sorted by name. Two images of one program list alike whichever assembler or
  writer laid them out, which is how the tests compare images (`%msil_canon`, section 7). Its
  options are those of `mdcanon` (section 7, "The metadata oracles"), which prints the same
  bytes:
  - `--order=sorted|decl` (table order with `decl`), `--forms=exact|canon` (short branches and
    the `.s` forms printed as their long forms), `--scopes=keep|strip` (`[*]` for every
    assembly scope and no `assemblyref` lines);
  - the six normalizations of plan 5.12.6, each of which removes one known difference between
    ilasm's images and the linker's: `--assembly-hash=ignore` (`hash=*`), `--params=drop` (no
    `.param` line for a parameter row with flags 0 and nothing attached),
    `--initlocals=ignore-without-locals` (no ` initlocals` on a body without locals),
    `--snan=quiet` (a float32 signaling NaN operand printed quieted, as ilasm stores it),
    `--pinvoke-module=strip` (`'*'` for the P/Invoke module, no `moduleref` lines) and
    `--pinvoke-access=ignore` (P/Invoke flags without the member access); each defaults to
    `keep`;
  - `--stats` appends the size of every table and heap and the image's, and `--stats-only`
    prints only those lines. Either one selects the canonical mode as `--canonical` does, also
    next to another mode: `--stats-only --headers` prints the statistics, then the headers, and
    `--stats --tables` the listing with its statistics, then the tables.
- `--headers` and `--tables` print the image as it is laid out, numbers in hexadecimal: the
  machine, the section table, the CLI header, the metadata root and its streams, the MVID, and
  each table's rows with tokens, heap strings, blobs and GUIDs.
- `--link-info` prints the link-info record of a CIL object (plan 5.8.11: one line per symbol,
  segment, relocation, COMDAT group, constructor, signature and linker option) and the symbol
  tables of a `-shared` library (M22). A file with neither is an error.

With several files each one's output starts with `File: <path>`. A file that cannot be read
prints `llvm-cil-dump: error: '<file>': <message>` on stderr and the tool goes on with the next
one; the exit status is 1 when any file failed. A malformed image is an error that names the
problem (`invalid CLI image: ...`, `invalid CLI metadata: ...`), never a crash, and no partial
listing is printed for it.

`llvm-cil-copy <input> -o <output>` reads an IL-only PE32+ image and writes it again through
the library: every row is rebuilt with `MetadataBuilder` (table order kept, tokens renumbered),
every method body re-encoded with its tokens rewritten, and the image written by the PE writer
that `msil-ld` will use (M20), in the envelope of the C# compiler's images. Duplicate TypeRef
and MemberRef rows, which ilasm never writes, become one row. The kind and the machine come
from the input: an image with an entry point is copied as an executable, one with an Assembly
row as a library, anything else as a netmodule, and the machine (x86_64 or ARM64) is kept. So a
DLL with an entry point (ilasm `-dll` of a program with `.entrypoint`, the shape of the lit
tests) becomes an executable, characteristics 0x0022 instead of 0x2022, and its canonical `pe`
line says `exe` where the input's says `dll`; compare a copy with an image that ilasm wrote
with `-exe` when the program has an entry point. A copy of a CIL object keeps its link-info
record as it is, so `--link-info` prints the same lines, but drops the COFF symbol view (plan
5.8.6): the copy has the one section `.text` and no symbol table, and `llvm-nm` lists nothing
in it. The copy lists exactly like its input and runs the same way:

```sh
clang --target=x86_64-pc-windows-msvc-msil -O2 -S hello.c -o hello.il
ilasm -nologo -quiet -exe -pe64 -x64 -output=hello.exe hello.il
llvm-cil-copy hello.exe -o hello-copy.exe
llvm-cil-dump hello.exe > a.canon && llvm-cil-dump hello-copy.exe > b.canon && diff a.canon b.canon
dotnet hello-copy.exe        # with hello-copy.runtimeconfig.json (section 5); prints and exits alike
```

Copying the same input twice gives the same bytes (the MVID and the time stamp are a hash of
the image). The tool refuses what the library's writer cannot rebuild, each time with
`llvm-cil-copy: error: '<input>': <message>`, exit status 1 and no output file (names print
raw, row numbers in decimal):

- a row of a table that the writer does not write (plan 5.7.6; `Constant`, `InterfaceImpl` and
  `GenericParam` among them): `unsupported table <Name> (<n> rows)`;
- FieldRVA data whose size the field's type does not give (a `native int` or pointer field):
  `the FieldRVA data of field '<name>' has no known size`, and FieldRVA data above 4 GiB in
  all: `the FieldRVA data is larger than 4 GiB`;
- an `ldstr` string that holds an unpaired UTF-16 surrogate, which the runtime accepts but
  `MetadataBuilder` cannot write: `unsupported user string at #US offset 0x<8X>: it holds a
  lone UTF-16 surrogate`;
- a class that extends, or is nested in, a later TypeDef row (valid ECMA-335; ilasm writes one
  when a derived class comes before its base): `type '<name>' (TypeDef row <r>) extends a later
  TypeDef, row <s>` and `type '<name>' (TypeDef row <r>) is nested in a later TypeDef, row <s>`;
- a first TypeDef row other than `<Module>` with flags 0 and no base type: `TypeDef row 1 is not
  the '<Module>' type that MetadataBuilder writes (flags 0, no base type)`;
- a method body that is not IL: `method '<name>' (MethodDef row <r>) has a body that is not
  IL`;
- an image that is not IL-only, `not an IL-only image (CorFlags 0x<8X>)`, or whose entry point
  is native code, `the entry point is native code (CorFlags 0x<8X>)`, or an entry point token
  that is not a MethodDef, `the entry point token 0x<8X> is not a MethodDef`;
- the accessors of an event, which are not copied: `MethodSemantics row <r> is not a
  property's (events are not copied)`;
- a PE32 image (only PE32+ images are written until phase 5), such as ilasm's x86 one:
  `cannot write PE image: machine 0x014C is not supported until phase 5`.

## 7. Run the tests

| Suite | Where | What |
|---|---|---|
| Unit tests | `llvm/unittests/Target/MSIL`, plus the MSIL cases of `TargetParserTests` and `MCTests`, and `llvm/unittests/CLI` (`CLITests`, all of it) | type mapping, name quoting, stack effects, target machine, streamer, the MC object hooks (`MSILMCObjectHooks.*`), the standalone import table and the linked import mode (`MSILImportsTest`, M15), triples and their compatibility, target selection; `CLITests` (18 suites; at least 170 tests, the M17 gate's floor): the ECMA-335 format headers, the metadata table schema and the link-info record (suites `CLIFormatTest`, `CLITableSchemaTest`, `CLIMSILLinkTest`, M13), the writer half of the ECMA-335 library (M16): blobs and compressed integers (`CLIBlobTest`), signatures (`CLISignatureTest`), custom-attribute blobs (`CLICustomAttributeTest`), the opcode table (`CLIOpcodesTest`), the IL builder with its short forms, branch relaxation and `.maxstack` (`CLIILBuilderTest`), the IL decoder and token rewriter (`CLIILDecoderTest`), method bodies (`CLIMethodBodyTest`), the metadata heaps (`CLIHeapsTest`) and `MetadataBuilder` (`CLIMetadataBuilderTest`), and its reader half and codecs (M17): the PE writer with its envelope per image kind, COFF symbol view and content ID (`CLIPEWriterTest`), the PE image reader (`CLIPEImageTest`), the metadata reader with every malformation and wide indexes (`CLIMetadataReaderTest`), a writer-reader round trip with canonical listings (`CLIRoundTripTest`), the link-info record's codec, checks, validation, COFF view and printer (`CLILinkInfoTest`) and the symbol-table attribute (`CLISymbolTableTest`) |
| llc and MIR tests | `llvm/test/CodeGen/MSIL` | the emitted IL, FileCheck'ed on both x86_64 triples (and the aarch64 ones where the ABI differs), and assembled with ilasm when it is available; the pass pipeline and the MIR at llc's stop points; the linker options in the bitcode symbol table of MSIL modules (`irsymtab-linker-options.ll`); the module flags `msil-assembly-name` and `msil-lto` that `msil-ld` sets (`lto-module-flags.ll`); the linked import mode, `msil-linked`, with its `assembly` P/Invokes and errors, against the standalone table (`imports-linked.ll`, M15) |
| clang tests | `clang/test/CodeGen/MSIL`, `clang/test/Preprocessor/init-msil.c`, `clang/test/Driver/msil-toolchain.c`, `msil-link.c`, `msil-lto.c` and `msil-header-search.c` | the clang wrapper, predefines, driver errors (the ELF-only section options among them), `-mllvm` options, `-cc1as`, the import attributes and `#pragma comment(lib)`, Design.md 12's literal command; the MSIL toolchain (M14): `-c` as bitcode, the `msil-ld` command line, the default libraries (with `--ucrtbase-imports` on windows-msvc since M15), `-fuse-ld` and `--ld-path=`, the LTO options and the cc1 lines (`-###` only) |
| `msil-ld` tests | `lld/test/MSIL` | 47 tests: which program names and `-flavor` values reach `msil-ld` (`dispatch.test`) and that `lld-link` and `-flavor link` still reach the COFF flavor (`dispatch-coff.test`), its options and their errors, symbol resolution in both input orders (weak, common, COMDAT, archives, `--whole-archive`, duplicates, undefined and mismatched symbols), the triple checks and the stock-bitcode warning, library search, the LTO options and `--lto-emit-asm` text, the map file, `--why-extract`, `--reproduce`, `--trace` and response files (M14); and the 21 `native-*.test` of M15: each native-library reader, the `-l` rules, scripts under `--sysroot`, binding and its order, the linker options of bitcode, `--ucrtbase-imports`, `--print-imports`, and every error of section 8 that names a native symbol. Inputs come from the tree's `llvm-as`, `llvm-ar`, `llvm-lib /def:` and `llvm-dlltool`, and for the native libraries `yaml2obj`, `llvm-mc` (the tests that need it require the X86 target), `ld.lld -shared` and `lld-link -dll`. In `check-msil` and the `net8` leg; the milestone gates also run them with `--param RUN_LLD_MAIN_TWICE=1` (`LLD_IN_TEST=2`: every link runs twice in one process, as for the other lld flavors; `dispatch-coff.test` is unsupported there, because the COFF flavor's `no input files` exits from the silenced first run) |
| ECMA-335 tool tests | `llvm/test/tools/llvm-cil-dump`, `llvm/test/tools/llvm-cil-copy` | 14 tests (M17; 8 of `llvm-cil-dump`, 6 of `llvm-cil-copy`) on `yaml2obj` copies of images that ilasm wrote from IL kept in each test: `llvm-cil-dump`'s `--headers` (PE32+, PE32, and a CIL object with its marker sections and COFF symbols), `--tables`, `--canonical` with every option and normalization, `--stats` and `--stats-only` alone and next to `--headers` and `--tables`, the details of canonical listing v1 that plan section 12's row '5.7.10 clarified (canonical listing v1 ...)' settles (`canonical-edges.test`: `.implements` and `.try` clauses in table order, sort keys on raw names, static properties and `.other`, what the listing leaves out, a `switch` without targets, a branch target before the code, data past a section's raw data, no `entrypoint` line for a native entry point, the limit of 64 type constructors across TypeSpecs, and the errors for an entry point that is not a MethodDef, an RVA outside every section and deeper types), `--link-info` on a hand-encoded record, on a library's symbol tables and on an image that carries both, several files and the errors of damaged images; `llvm-cil-copy` on an assembly with rows in every table the writer supports and on a CIL object (the same listing and record, the same bytes from two directories, and no COFF view in the copy), the kind and machine it takes from the input (`copy-kind.test`: a library, a DLL with an entry point that becomes an executable, an ARM64 image kept ARM64, and a PE32 image refused), duplicate TypeRef and MemberRef rows merged into one (`copy-duplicates.test`), and its refusals (unsupported tables, data of unknown size, a lone surrogate in a user string, a base type or enclosing type in a later row and the other refusals of section 6). `check-msil` builds both tools and runs these tests, and so does the `net8` leg. To run them alone (`build/bin/llvm-lit -sv llvm/test/tools/llvm-cil-dump`), build the tools first with `ninja -C build llvm-cil-dump llvm-cil-copy`: `check-llvm` and `check-llvm-tools-llvm-cil-dump` (or `-copy`) do not build them, and each directory's `lit.local.cfg` reports its tests UNSUPPORTED while they are missing |
| Execution tests | `msil/test/M1/<NN-area>/`, `msil/test/M2/<NN-area>/` | C and IR programs compiled at `-O0` and `-O2` (and in the `more-levels` leg at the other levels; see below), assembled, verified, force-JITted, run and compared with checked-in transcripts and with a native build (the *native oracle*, below); `M1/11-programs/` is the Design.md 12 program corpus. `M2/` (M14) holds programs of several files, archives and stock-clang bitcode that `msil/utils/msil-prog.py` builds through clang and `msil-ld`, programs that call native libraries (`M2/03-native/`, M15), the link-semantics matrix (`M2/08-semantics/`, Status.md section 2) and the compile-only hosted tests (`M2/09-hosted/`, M15; the `hosted` leg below) |
| Harness self-tests | `msil/test/harness/` | 75 tests: the runner, verifier and JIT checker themselves (among them the renumbering of ildasm's `I_<rva>` data labels in `msil-verify.py --roundtrip`, `legs/roundtrip-data-labels.test`, M17), `update-expected.py`, the E7 Linux oracle, the dependencies that `--dep` ships with a program (`dep-local.test`, `dep-wsl.test`, M15), the comparison of `il-snapshot.py` (`il-snapshot.test`, M15; below), the canonical listing of `%msil_canon` (`canon-listing.test`, M16, and from M17 on `llvm-cil-dump`) and its agreement with `mdcanon` (`canon-agree.test`, M17; below), and lit's configuration errors (bad `msil_*` parameters, and under `msil_require_verify=1` a missing verifier or lister, stop lit with exit status 2); `prog/` pins the expansions of `%{msil_prog}` and `%{msil_c}` (with `%{msil_native_libs}` and `%{msil_deps}` since M15) and the commands of `msil-prog.py` with stand-in tools |
| Upstream regression tests | the upstream lit tests and unit tests named in `msil/utils/upstream-hooks.txt` | the upstream behaviour of every upstream file the fork changes (the upstream-hooks check, below) |

```sh
ninja -C build check-msil                 # every MSIL lit test (lean dependencies)
ninja -C build check-msil-m1-11-programs  # one directory (check-msil-m1-<dir>, check-msil-m2-<dir>)
build/bin/llvm-lit -sv msil/test/M1/03-control llvm/test/CodeGen/MSIL/switch.ll   # llvm-lit.py on Windows
build/unittests/Target/MSIL/MSILTests     # after 'ninja -C build MSILTests'
```

The per-directory targets are created when CMake runs, so a directory added since the last
configure gets its target after the next one; `llvm-lit` with the path works at once.

On the Windows host, `msil/utils/build/msil-lit.ps1 -LitArgs "-sv <params>" <paths>` runs lit
under the shared build lock. Through `pwsh -File`, a value that starts with `-` and contains a
`:` (a drive path, as in `--param msil_ilasm=D:/...`) is read as `-Name:value` syntax and the
call fails with "Missing an argument for parameter 'LitArgs'": write `-LitArgs:"-sv ..."` or
start the value with a blank (`-LitArgs " -sv ..."`). Execution tests need clang with the MSIL
target, ilasm and a .NET runtime, and `msil/test/M2` also `msil-ld` (its archive tests the
tree's `llvm-ar`); without them they are UNSUPPORTED rather than failing. The compile-only
tests of `msil/test/M2/09-hosted` need only clang with the MSIL target, plus a glibc sysroot
(feature `msil-glibc-sysroot`) or the UCRT headers on `INCLUDE` (`msil-ucrt-headers`), and
`msil-ld` for the two that link.
`--param msil_pipeline=lto` builds every C test of `msil/test/M1` through `clang -c` and
`msil-ld` instead of `clang -S` (the `lto` leg below).

The execution suite takes lit parameters (all of them are in
[msil/test/README.md](../../../msil/test/README.md)). The milestone-1 gate runs it in these legs
(the names are those of `msil/utils/ci/lit-legs.ps1`). Every lit leg also passes
`--param msil_require_verify=1 --param msil_require_exec=1 --param msil_require_link=1`, which
turn a missing verifier, ildasm, jitall, mdcanon (M16), `llvm-cil-dump` (M17), PEVerify
(Windows), ilasm, runtime, MSIL-enabled clang, `msil-ld` or `llvm-ar` into an error instead of
skipped steps or UNSUPPORTED tests. *WSL* below stands for
`msil_runner=wsl msil_triple=x86_64-unknown-linux-gnu-msil msil_wsl_dotnet=private
msil_dotnet_runtime=10` (the private .NET 10 in WSL), *E7* for
`msil_native_oracle=required msil_linux_sysroot=<sysroot>` (the E7 Linux oracle; both are
described under "Native oracles and the WSL runtime" below), and the legs
marked `-Wsl` run only when `lit-legs.ps1` gets `-Wsl` (or names them with `-Leg`):

| Leg | Paths | Extra `--param`s |
|---|---|---|
| `net8` | `llvm/test/CodeGen/MSIL`, the clang MSIL tests (the driver tests included), `lld/test/MSIL`, `llvm/test/tools/llvm-cil-dump` and `llvm-cil-copy` (M17), `msil/test` (M1, M2 and the harness) | `msil_dotnet_runtime=8 msil_native_oracle=required msil_run_netfx=1` (.NET 8, the native oracle with the compiler-rt builtins, and .NET Framework 4.8; the native link and, since M15, the default libraries of the MSIL link need `LIB`: use a VS x64 developer prompt, or `msil-lit.ps1`, which enters that environment) |
| `net10` | `msil/test/M1` | `msil_dotnet_runtime=10` |
| `net8-tc0` | `msil/test/M1` | `msil_dotnet_runtime=8 msil_dotnet_env=DOTNET_TieredCompilation=0` (optimizing JIT) |
| `more-levels` | `msil/test/M1` | `msil_dotnet_runtime=8 msil_opt_levels=O1,O3,Os,Oz`: the four levels the other legs leave out (they run `-O0` and `-O2`); `.ll` tests that use `%{msil_ll}` run at `-O1` and `-O3` only, because llc has no `-Os` or `-Oz`. The three trap tests (`01-int/trap-call-before-div.ll`, `01-int/trap-call-before-rem.ll`, `03-control/trap.ll`) and `01-int/calls-arith-debug-info.test` have explicit RUN lines that pin `-O0` and `-O2` in every leg |
| `lto` | `msil/test/M1`, `msil/test/M2` | `msil_pipeline=lto msil_dotnet_runtime=8 msil_native_oracle=required`, and lit's `--filter-out=\.ll$` (M14): every C test of M1 as a one-file program through `clang -c`, `msil-ld --lto-emit-asm` and `ilasm -exe`, and M2, at `-O0` and `-O2` against the native oracle; M1's `.ll` tests are left out, because this pipeline runs them on llc text as `net8` does |
| `det` | `msil/test/M2/06-det` | `msil_dotnet_runtime=8` (M14): each program is built twice in two directories, and the bitcode objects, the archives and the `--lto-emit-asm` text must be equal byte for byte (`%{msil_det}`) |
| `wsl-net10` (`-Wsl`) | `msil/test/M1`, `msil/test/harness` | *WSL* and *E7* |
| `wsl-net10-tc0` (`-Wsl`) | `msil/test/M1` | *WSL* plus `msil_dotnet_env=DOTNET_TieredCompilation=0` |
| `wsl-ilasm` (`-Wsl`) | `msil/test/M1/11-programs` | *WSL* plus `msil_wsl_ilasm=<the NuGet linux-x64 ilasm>`: assembled by the Linux ilasm inside WSL |
| `wsl-quick` (`-Wsl`) | `msil/test/M1/11-programs`, `msil/test/harness`, `msil/test/M2` | *WSL* and *E7*: the short WSL leg that every Phase 2 milestone gate runs from M13 on (plan 5.12.10) |
| `hosted` (`-Wsl`, optional) | `msil/test/M2/09-hosted` | `msil_glibc_sysroot=<glibc sysroot>` (M15, plan Q16): the hosted compile-only tests (glibc's headers and its real `libc.so` scripts, the UCRT headers) with the glibc sysroot required, so a missing one is a configuration error and the leg cannot pass by skipping. The other legs run these tests only where the default glibc sysroot exists and, for the UCRT test, `INCLUDE` holds the UCRT headers. Marked `-Wsl` because its sysroot is made through WSL (below); the CI never runs it |
| `modern-ilasm` | `msil/test/M1/11-programs` | `msil_ilasm=<the NuGet win-x64 ilasm.exe>` |
| `roundtrip` | `msil/test/M1/11-programs` | `msil_roundtrip=1`: every assembly goes through ildasm and ilasm (Design.md 5.5) and the reassembled one runs |
| `expected` | `msil/test/M1`, `msil/test/M2` | not lit: `update-expected.py --check` (the Windows native oracle, with the compiler-rt builtins) |
| `expected-linux` (`-Wsl`) | `msil/test/M1`, `msil/test/M2` | not lit: `update-expected.py --check --os linux --linux-sysroot <sysroot>` (the E7 oracle, against `<stem>.linux.expected` where one exists, else `<stem>.expected`) |
| `utc` | six tests of `llvm/test/tools/UpdateTestChecks/update_llc_test_checks` | not lit: `msil/utils/run-utc-tests.py --llc <build>/bin/llc msil-basic x86-basic x86-mir-only x86-asm-mir-mixed x86-constant-pool asm-show-inst` runs their RUN lines (lit reports them UNSUPPORTED on Windows: `llvm/test/tools/UpdateTestChecks/lit.local.cfg` sets the `llc-binary` feature they require only when a file named exactly `llc` exists); the log is `build/lit-results/utc.log` |
| `all-levels` (nightly, `-Full`) | `msil/test` | `msil_opt_levels=O0,O1,O2,O3,Os,Oz` |

`<sysroot>` is `$MSIL_LINUX_SYSROOT`, else `C:\llvm-msil-scratch\linux-sysroot`, and `<glibc
sysroot>` is `$MSIL_GLIBC_SYSROOT`, else `C:\llvm-msil-scratch\glibc-sysroot`. Each lit leg
writes its lit arguments one per line (`--param=<name>=<value>`, `--xunit-xml-output=<file>`)
into the response file `build/lit-results/<leg>.params.rsp` and runs
`msil-lit.ps1 -LitArgs:"-sv @<file>"`; lit reads the file itself, so a parameter value may
contain blanks (a NuGet ilasm under a profile directory with a space, for example). The build
directory may not, because `msil-lit.ps1` splits `-LitArgs` at blanks; the leg then fails naming
it.

Expected transcripts (`<test>.expected`) are generated from a native build, never written by
hand, and CI checks that they are current:

```sh
python msil/utils/update-expected.py msil/test/M1/11-programs/fib.c   # write
python msil/utils/update-expected.py --check msil/test/M1 msil/test/M2   # check (Windows oracle)
python msil/utils/update-expected.py --check --os linux msil/test/M1 msil/test/M2   # (E7 oracle)
```

The script compiles C with the flags of `%{msil_cflags}` (`-std=gnu17 -ffreestanding
-Werror=return-type`) and runs the native builds in parallel (`-j`, default one per CPU). On
Windows its MSVC builds link the compiler-rt builtins (`--rtlib`, default `auto`, as
`msil_native_rtlib` below). `--os linux` uses the E7 oracle (`--linux-sysroot`, `--ld-lld`,
`--wsl-distro`); writing then creates `<stem>.linux.expected` only for a transcript that differs
from `<stem>.expected`, and removes one that no longer differs. No test of `msil/test/M1` or
`M2` needs one today. `--check` compares with `<stem>.<os>.expected` where it exists, else with
`<stem>.expected`, the file the test itself picks for that OS. An `M2` test (a `.test` file whose
RUN line is `%{msil_prog}`) is built natively by `msil-prog.py --mode native` from its
split-file parts and `REDEFINE:` lines, with the libraries of `%{msil_native_libs}` and, for
the run, the files of `%{msil_deps}` next to the program (M15); the RUN lines before the
first composite, which build a test's own native library, run first in lit's internal shell
with the oracle's tools. Tests that only pin a diagnostic or check determinism have no
transcript and are skipped.

How to add a test, what each status line means and how to reproduce a failure step by step is in
[msil/test/README.md](../../../msil/test/README.md).

### Native oracles and the WSL runtime

With `--param msil_native_oracle=1` or `required` (`required` in the `net8`, `lto`,
`wsl-net10` and `wsl-quick` legs), every execution test also builds its source for the native
triple (`x86_64-pc-windows-msvc` for `x86_64-pc-windows-msvc-msil`, and so on), runs it, and
compares its transcript with the same `.expected` file as the MSIL build's. A program of several
files (`msil/test/M2`) is built natively from the same sources and archives by
`msil-prog.py --mode native`: on Windows through clang with `-fuse-ld=lld`, so that archives
resolve by lld's rules as they do in `msil-ld`, and under WSL by the E7 recipe below. Both
builds link the test's native libraries (`%{msil_native_libs}`, M15), and both runs get the
files of `%{msil_deps}` (a test-built `native.dll` or `libnative.so`) next to the program; the
E7 link adds `-rpath $ORIGIN`, so the native program finds such a library there too.

- **Windows: compiler-rt.** A native MSVC build of an `__int128` test calls compiler-rt helpers
  (`__udivti3`, `__fixdfti`, ...) that no MSVC library has, and the fork builds no compiler-rt.
  `--param msil_native_rtlib=auto` (the default) links the newest
  `clang_rt.builtins-x86_64.lib` of the Visual Studio LLVM
  (`%VCINSTALLDIR%Tools\Llvm\x64\lib\clang\<version>\lib\windows`, from the "C++ Clang tools"
  component), else of the LLVM whose `clang-cl` or `lld-link` is on `PATH`; a path names
  another library and `none` turns it off. It applies to MSVC triples on a Windows host. When
  the library is missing, `msil_native_oracle=required` is a configuration error; lit prints
  `msil: native rtlib: <path>` when it found one.
- **Linux triple under WSL: the E7 oracle.** It needs no C compiler, no C headers and no root
  inside WSL (the distribution has none of the first two). The tree's clang compiles each test
  on Windows for `x86_64-unknown-linux-gnu` with `-fno-pic -c`; `ld.lld` (the tree's, else the
  one on `PATH`, else the Visual Studio LLVM's) links it as
  `ld.lld -dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed start.o <test>.o libm.so.6
  libc.so.6 libgcc_s.so.1`, where `start.o` is `msil/utils/linux-oracle/start.c` (a minimal
  `_start` that runs the program's `.init_array` constructors, then `exit(main(argc, argv))`,
  compiled once per lit run) and the libraries are the WSL distribution's own, copied into
  `<sysroot>` by `make-linux-sysroot.py`. `libgcc_s.so.1` provides the `__int128` helpers that
  `libc.so.6` does not export; `--as-needed` keeps it out of programs that do not call them.
  `msil-run.py --native --runner wsl` then runs the program inside WSL, with the limits and the
  private directory of every other run. `--param msil_linux_sysroot=<dir>` names the sysroot
  (default as `<sysroot>` above, or `/` on a Linux host); a named one without `libc.so.6` and
  `libm.so.6` is a configuration error. With `msil_runner=wsl` and the native oracle on, lit
  prints `msil: native oracle: E7 (<ld.lld>, sysroot <dir>)`. On the Linux CI job, which runs
  the Linux triple natively, the native oracle is simply clang and the system linker.
- **The private WSL runtime.** The WSL legs pass `msil_wsl_dotnet=private`: programs and jitall
  run on `$HOME/.dotnet-msil/dotnet` inside WSL, where `msil-wsl-dotnet.sh` installed
  Microsoft.NETCore.App 8.0.31 and 10.0.13 with Microsoft's `dotnet-install.sh`, so an
  unattended upgrade of the distribution's own dotnet packages never changes the runtime under
  test. lit prints `msil: WSL dotnet (private): <path> (WSL Ubuntu, Microsoft.NETCore.App
  <versions>)`; when `private` is named but not installed, the run stops with a configuration
  error that names the script. Without the parameter lit takes the private runtime when it is
  installed, else the distribution's `dotnet` on `PATH`, and says so. An absolute WSL path
  names any other dotnet host.

**Host setup.** The private runtimes and the E7 sysroot are made once per host, by idempotent
commands that need no root, change no system setting and write nothing inside the repository
(they take seconds once done; the first run downloads the two runtimes):

```powershell
wsl -d Ubuntu --cd <repository> --exec bash msil/utils/msil-wsl-dotnet.sh   # the private runtimes
python msil/utils/make-linux-sysroot.py --out C:/llvm-msil-scratch/linux-sysroot # the E7 sysroot
```

`make-linux-sysroot.py` copies `libc.so.6`, `libm.so.6`, `libgcc_s.so.1` and the dynamic loader
out of the distribution (symbolic links resolved, SHA-256 checked) into
`usr/lib/x86_64-linux-gnu/` and `lib64/`, writes the linker scripts `libc.so` and `libm.so` and
a stamp with the distribution's glibc version, rewrites only files that changed, and refuses a
directory inside the repository. From M15 `msil-ld` uses the same directory as `--sysroot`
(the harness's `%{msil_ldflags}` on Linux triples), and its two scripts are what lets the
driver's `-lc -lm` find `libc.so.6` and `libm.so.6`. `run-m1-gate.ps1` runs both commands, and
the third one below, as its `host-setup` step.

The optional `hosted` leg (M15) needs a glibc sysroot with headers, which a third command
makes, also without root (`apt-get download` of `libc6-dev`, `linux-libc-dev`, `libc6` and
`libgcc-s1` into `$MSIL_GLIBC_DEBS`, else `$HOME/.cache/msil/glibc-debs`, unpacked with
`dpkg-deb -x`; it downloads only the packages that are missing and copies nothing when the
stamp `msil-glibc-sysroot.stamp` lists the same packages):

```powershell
wsl -d Ubuntu --cd <repository> --exec bash msil/utils/make-glibc-sysroot.sh /mnt/c/llvm-msil-scratch/glibc-sysroot
```

The result holds glibc's own `usr/include`, `libc.so.6`, `libm.so.6`, `libc_nonshared.a` and
its real linker scripts `libc.so` and `libm.so`, with every symbolic link copied as a file (a
Windows drive keeps none). It replaces the directory only when it is empty or holds the stamp.
Every milestone gate from M15 runs it in its step G0, and `run-m1-gate.ps1`'s `host-setup` runs
it into `$MSIL_GLIBC_SYSROOT`, else `C:\llvm-msil-scratch\glibc-sysroot` (the `<glibc sysroot>`
of the `hosted` leg), so that leg needs no sysroot made beforehand.

### The milestone-1 gate and CI

```powershell
pwsh -NoProfile -File msil/utils/run-m1-gate.ps1                                # everything
pwsh -NoProfile -File msil/utils/run-m1-gate.ps1 -SkipConfigure -SkipBuild -Leg net10,roundtrip
pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -List                          # the leg names
```

`run-m1-gate.ps1` runs the scripts in `msil/utils/ci/` in the order the CI workflow
`.github/workflows/msil-ci.yml` runs them: `configure.ps1`, `build.ps1` (`msil-build.ps1
-Standard`, the targets of `build-targets.txt` of section 2, and jitall), `unit-tests.ps1`
(`TargetParserTests`, `MCTests` and `MSILTests` with the MSIL filter and all of `CLITests`, no
skipped test allowed), then, on the development host only, the `host-setup` step (the three
commands of the host setup above, as the rows `host-setup/wsl-dotnet`,
`host-setup/linux-sysroot` and `host-setup/glibc-sysroot`), then `lit-legs.ps1 -Wsl` (the legs
above) and `upstream-hooks.ps1` (the upstream-hooks check below, every leg; the gate checks that
each of them has a row). A failed `host-setup` row does not stop the legs; the WSL legs that
need what it makes then fail with a configuration error that names it. The gate's `-NoWsl`
leaves out `host-setup`, `-Wsl` and with it the six `-Wsl` legs (`hosted` among them), `-Full`
adds `all-levels`, `-Leg` runs only the named legs,
`-SkipUpstreamHooks` leaves out the upstream-hooks step, and `-SkipConfigure`, `-SkipBuild` and
`-SkipUnitTests` reuse what is there. Unless `-NoWsl`, a `ci-sh` row smoke-checks the Linux
job's scripts inside WSL (`bash -n` on `msil/utils/ci/*.sh`, `lit-legs.sh --list` against its
header, and a non-zero exit with FAIL rows against an empty build directory); the Linux job's
configure, build, unit-test and lit steps themselves run only in CI. A child script that exits
non-zero without a FAIL row, and every selected leg without a row, add FAIL rows. A failed
configure or build stops `host-setup`, the unit tests, the legs and the upstream-hooks step
(`ci-sh` needs no build and still runs); every leg runs even after another one failed. A lit leg
passes when lit exits 0 and at least one test passed, `expected` and `expected-linux` when their
script exits 0 and at least one test matched, `utc` when its script exits 0; the lit log, xunit
file and response file of a leg are `build/lit-results/<leg>.log`, `<leg>.xml` and
`<leg>.params.rsp`. The run ends with a summary table with one row per step or leg and exits 0
only when every row passed. A full run takes well over an hour.

From M12 on, each milestone's own gate runs a part of this in about 30 minutes (plan 5.12.10:
from M13 the build `msil-build.ps1 -Standard`, the unit tests with a floor of `CLITests` tests,
the `net8` leg, `wsl-quick`, the upstream-hooks `manifest` and `pins` legs, or all of the check
when the milestone changes an upstream file, and the milestone's own legs; M12's gate runs
`net8`, `net10`, `wsl-net10` instead of `wsl-quick`, `roundtrip`, `expected-linux` and the whole
upstream-hooks check; M14's adds `lto`, `det`, `lld/test/MSIL` with `LLD_IN_TEST=2` and
`update-expected.py --check msil/test/M2`; M15's adds `make-glibc-sysroot.sh` to its host setup,
`lld/test/MSIL` with `LLD_IN_TEST=2`, `lto` runs of `msil/test/M1/06-output` and
`08-fnptr/fnptr-to-libm.c` on both triples with the default libraries, `update-expected.py
--check` of the new M2 tests on both oracles, the `hosted` leg and the IL snapshot below; M16's
raises the `CLITests` floor to 110 and adds `build-mdtools.py --force`, `check-cil-defs.py`,
`cli-corpus-check.py --mode ilasm` and `canon-listing.test`, the metadata oracles below; M17's
raises the floor to 170 and adds the tests of `llvm-cil-dump` and `llvm-cil-copy`,
`canon-agree.test` and `cli-corpus-check.py --mode dumpers,copy,stats` over all of
`msil/test/M1`), and `run-m1-gate.ps1` runs in full after the commits of M12, M15, M18 and
M20.

The Windows CI job runs the same scripts, except that `configure.ps1 -Ci` configures with the
cache file instead of `msil-configure.ps1`, there is no `host-setup`, `lit-legs.ps1` runs
without `-Wsl` (a GitHub runner has no WSL), and `upstream-hooks.ps1` runs as `-NoCompile
-NoManifest -MinPass 250` whenever the build passed (also after failed unit tests or legs). The
Linux job runs their `.sh` twins, whose legs cover `x86_64-unknown-linux-gnu-msil` natively on
.NET 8 and .NET 10, including `more-levels`, `lto`, `det` and `utc` (`bash
msil/utils/ci/lit-legs.sh --list`); its `expected` leg is the native Linux oracle
(`update-expected.py --check` on Linux, against `<stem>.linux.expected` where one exists), and
its `--full` legs add `all-levels` and `upstream`, which builds and runs all of `ninja
check-llvm check-clang`. `lit-legs.sh --leg hosted` runs the optional `hosted` leg against the
host's own glibc (`msil_glibc_sysroot=${MSIL_GLIBC_SYSROOT:-/}`); no CI step names it. Both jobs
install .NET with `actions/setup-dotnet` pinned at v5.4.0
(`26b0ec14cb23fa6904739307f278c14f94c95bf1`). The workflow is committed with a manual trigger
only (`workflow_dispatch`, whose `full` input selects the full legs, as a nightly schedule
would); enabling push, pull-request or nightly runs is the repository owner's decision.

### The IL snapshot (`il-snapshot.py`)

The standalone import table is frozen (D14), and a change in the backend or the driver must
not change what `clang -S` and llc write. `msil/utils/il-snapshot.py` (M15) proves it: it
compiles every C and IR input under the given paths that `HEAD` tracks, at `-O0` and `-O2` for
`x86_64-pc-windows-msvc-msil` and `x86_64-unknown-linux-gnu-msil` and at `-O2` for
`x86_64-w64-windows-gnu-msil` and `x86_64-unknown-linux-musl-msil`, and keeps each output and
its diagnostics; a second snapshot is compared with a first one. Run it from the repository
root, writing outside the repository:

```powershell
# Before a change, with the build of HEAD:
python msil/utils/il-snapshot.py --build build --jobs 6 --out C:/llvm-msil-scratch/il-before `
  --exclude llvm/test/CodeGen/MSIL/imports-linked.ll msil/test/M1 llvm/test/CodeGen/MSIL
# After it, with the new build: exit 1 and the differences when anything changed.
python msil/utils/il-snapshot.py --build build --jobs 6 --out C:/llvm-msil-scratch/il-after `
  --compare C:/llvm-msil-scratch/il-before `
  --exclude llvm/test/CodeGen/MSIL/imports-linked.ll msil/test/M1 llvm/test/CodeGen/MSIL
```

- A `.c` file is compiled with `clang -S` and the flags of `%{msil_cflags}`, a `.ll` file
  with llc, and each `;--- <name>.ll` part of a split-file test separately, from its own
  directory under a relative name. The list of inputs comes from `git ls-tree` of `HEAD`
  (without the `--exclude` paths; a directory excludes its subtree), their contents from the
  working tree, so a test that a change adds is not compared.
- The output is `<out>/<triple>/<level>/<path>.il` and `.err`, the stderr with CRLF folded,
  the tool's path replaced by its name and a crash report cut to `[crash: stack dump
  omitted]`, then a status line (`[exit N]`, `[timeout]` or `[memory limit]`). Include and
  library variables (`INCLUDE`, `LIB`, `CPATH`, ...) are removed from the compilers'
  environment, so a snapshot taken in a developer prompt equals one taken outside it.
- `--out` is emptied first; it may not lie inside the repository or overlap `--compare`, and a
  non-empty one must hold the `il-snapshot.json` stamp that every snapshot writes, so no other
  directory is ever emptied. `--compare DIR` lists every file that differs, is missing or is
  extra, shows the first differences, and exits 1; usage errors exit 2. The harness self-test
  `msil/test/harness/il-snapshot.test` pins this comparison on the fixture trees of
  `msil/test/harness/Inputs/il-snapshot/`: exit 1 naming a changed `.il`, a changed `.err`, a
  missing and an extra file, and exit 2 for usage errors.
- M15's gate compares the 2,634 compiles of the 439 inputs (files and split-file parts) that
  the commands above select with a snapshot of M14's build; M18, M19 and M20 repeat the check
  against their own baselines (plan 5.12.4).

### The metadata oracles (`msil/utils/mdtools`)

The ECMA-335 library in `llvm/lib/CLI` (M13 and M16: blobs, signatures, custom attributes, the
opcode table, the IL builder and decoder, method bodies, the metadata heaps and
`MetadataBuilder`; M17: the PE writer, the reader, the link-info codecs and the dumper of
`llvm-cil-dump` and `llvm-cil-copy`, section 6) is checked from outside by two small C# tools
over System.Reflection.Metadata, which the repository keeps as source and builds offline (M16,
plan 5.7.11):

- `mdcanon` prints *canonical listing v1* (plan 5.7.10): the metadata and IL of an assembly or
  module as text without tokens, heap offsets, RVAs, header forms, the MVID or anything else
  that depends on how the image was laid out, in UTF-8 with LF line ends and members sorted by
  name. Two images of one program list alike whichever assembler or writer laid them out.
  `--order=decl` keeps table order, `--forms=canon` prints short branches and `.s` forms as their
  long forms, `--scopes=strip` hides assembly scopes, and the six normalizations of plan 5.12.6
  (`--assembly-hash=ignore`, `--params=drop`, `--initlocals=ignore-without-locals`,
  `--snan=quiet`, `--pinvoke-module=strip`, `--pinvoke-access=ignore`) remove one known
  difference each; `--stats` appends table and heap sizes, and `--out-dir DIR` writes
  `DIR/<file name>.canon` for many files in one process. `llvm-cil-dump --canonical` (M17,
  section 6) implements the same specification in C++ and must print the same bytes.
- `mdump` prints a raw listing of headers, tables and bodies for people (`--no-pe`,
  `--no-bodies`), the opcode table of `System.Reflection.Emit.OpCodes` (`--opcodes`), and the
  content ID (MVID and time stamp) that `BlobContentId.FromHash` makes of a hash
  (`--contentid <hex>`).

```powershell
# Build both into <build>/tools/msil/mdtools (offline; a no-op when up to date; --force rebuilds).
python msil/utils/mdtools/build-mdtools.py
# The canonical listing of an assembly, or of several into a directory.
dotnet exec --runtimeconfig build/tools/msil/mdtools/mdcanon.runtimeconfig.json `
  build/tools/msil/mdtools/mdcanon.dll fib.dll
dotnet build/tools/msil/mdtools/mdcanon.dll --params=drop `
  --out-dir C:/llvm-msil-scratch/canon a.dll b.dll
# CILOpcodes.def against System.Reflection.Emit.OpCodes: 218 of its 219 opcodes equal
# (OpCodes has no no.).
python msil/utils/mdtools/check-cil-defs.py
# clang -S of every program with an .expected file, assembled by the Framework ilasm and by the
# NuGet ilasm: the two listings of each must be equal (40 of 40 for 11-programs at -O0 and -O2).
python msil/utils/mdtools/cli-corpus-check.py --mode ilasm --levels O0,O2 msil/test/M1/11-programs
# M17: both dumpers on every program and its copy, the copies verified, force-JIT-compiled
# and run, and both dumpers on four framework assemblies (M17's gate step; about 10 minutes).
python msil/utils/mdtools/cli-corpus-check.py --mode dumpers,copy,stats --levels O0,O2 `
  --work C:/llvm-msil-scratch/cli-corpus msil/test/M1
```

`build-mdtools.py` compiles each tool with the newest installed SDK's C# compiler against the newest
`Microsoft.NETCore.App.Ref` 8.x reference assemblies (`--ref-major N` picks another major) and
writes `<tool>.runtimeconfig.json` with `rollForward` `Major`, so any runtime from .NET 8 on runs
them; `--out DIR` and `--build-dir DIR` choose where. `check-cil-defs.py` compares name, value,
operand kind, stack pops and pushes and flow, and needs the tools built first. `cli-corpus-check.py`
takes test directories or `.c` files, compiles with the tree's clang (`--clang`, default
`<build>/bin/clang`) for `x86_64-pc-windows-msvc-msil` (`--triple`) with the flags of
`%{msil_cflags}`, works in `--work` (default `C:\llvm-msil-scratch\cli-corpus` on Windows,
`$HOME/llvm-msil-scratch/cli-corpus` elsewhere, never inside the repository), dumps each set with
one `mdcanon --out-dir` run and prints the first differing lines of a program whose listings differ;
`--ilasm-framework` and `--ilasm-nuget` name the assemblers (the NuGet one at `MsilILAsmVersion` of
`msil/utils/tools/msil-tools.proj` by default). The run over `11-programs` takes 4-12 seconds on
the development host; over all of `msil/test/M1` (198 listings per assembler) about a minute and
a quarter.

M17 added three modes, which share one compile and one assembly of the inputs (each program
with `ilasm -exe` when it has an entry point, else `-dll`; the Framework ilasm on Windows, the
NuGet ilasm elsewhere) and one copy of each image with `llvm-cil-copy` (`--cil-dump` and
`--cil-copy` name the tools, by default those of `<build>/bin`):

- `dumpers`: `llvm-cil-dump --canonical` and `mdcanon` print the same bytes for every image and
  for its copy, once with the default options and once with the six normalizations and
  `--scopes=strip`; the first differing lines are printed.
- `copy`: every copy lists exactly like its image, and a second copy made from another
  directory is the same byte for byte. The copies of the programs of `11-programs` at `-O2`
  are then verified as `%msil_verify` verifies (`--ilverify`, `--peverify`,
  `--ildasm-framework` name the tools), force-JIT-compiled with `msil-jitall.py` as
  `%msil_jitall` does, under lit's limits, which completes the verification policy (D21;
  `--jitall DIR` names the jitall build, by default `<build>/tools/msil/jitall`, which the
  script builds or refreshes as lit does), checked with PEVerify `/md` (no message for a copy
  that its image does not also get), round-tripped through the NuGet ildasm and ilasm
  (`--ildasm-nuget`), and run with their `.expected` transcripts on .NET 8; `crc32`,
  `dispatch-vm`, `quicksort`, `string-table` and `struct-sim` also on .NET 10, the .NET
  Framework 4.8 and WSL .NET 10 (`--wsl-distro`, `--wsl-dotnet`; compiled again for the Linux
  triple, and those copies verified as the Windows ones are). The triple's OS must be the
  host's.
- `stats`: `--stats-only` of both dumpers agrees on the .NET Framework `mscorlib.dll` and the
  `System.Runtime.dll` and `System.Private.CoreLib.dll` of the newest installed .NET 8, and so
  do their `--canonical` listings, on the same three files and on the `System.Runtime.dll` of
  the newest installed `Microsoft.NETCore.App.Ref` 8.x reference pack (the one M21 binds
  against; skipped with a note when no pack is installed), so every row, signature and body of
  these files is decoded.

The modes `encoder` (M18), `object` and `gnu-tools` (M19) follow.

In lit, `%msil_canon` is the listing that tests compare: `llvm-cil-dump --canonical` from M17 on
(the tree's `llvm-cil-dump`, or `--param msil_cil_dump=PATH`; feature `msil-cil-dump`), and
`mdcanon` when there is no `llvm-cil-dump`, except under `--param msil_require_verify=1`, where
a missing or disabled `llvm-cil-dump` is a configuration error (lit exits with status 2).
`%msil_mdcanon` is `mdcanon` itself and `%msil_cil_dump` is `llvm-cil-dump`; lit builds the C#
tools on configuration as it builds jitall, and `--param msil_mdcanon=DIR` names another build
of them
([msil/test/README.md](../../../msil/test/README.md)). The harness self-test
`msil/test/harness/canon-listing.test` assembles two small modules with every construct of the
listing and checks every line of their default listings, of `--order=decl --forms=canon` and of
the five normalizations that an ilasm image exercises, plus `--scopes=strip` and the
`--stats`/`--stats-only` lines; `canon-agree.test` (M17) lists six harness images and a
module kept in the test with the canonical-listing details of plan section 12's row '5.7.10
clarified (canonical listing v1 ...)' that ilasm can write (`.implements` and `.try` clauses
out of name and text order, names whose raw and quoted orders differ, a static property with a
`.other` accessor, an event, a MethodImpl, a DeclSecurity row, a parameter's custom attribute
and a `switch` without targets) with both dumpers, with the default options, with
`--scopes=strip --order=decl --forms=canon --stats` and with the six normalizations, and
requires the same bytes; it also checks that module's default listing for that row's orders.
Both tests also run `%msil_canon --headers`, which `mdcanon` refuses, and check the listing and
then the headers, so they fail unless `%msil_canon` is `llvm-cil-dump --canonical`.

### The upstream-hooks check

The fork changes 55 upstream files today (its *hooks*): new diagnostics, MSIL branches in clang's
driver (among them the choice of the MSIL toolchain, `MSILToolChain`, since M14), code
generation, Sema and pragma handling, WebAssembly's import attributes widened to MSIL
triples, the MSIL object format and `IsMSIL` environment in the triple and MC layers with their
symbol, streamer and object-writer factories, `case` labels for it in exhaustive switches, the
lld flavor `msil`, CMake wiring of the fork's directories, a module-map entry for the fork's
X-macro headers, and so on.
`msil/utils/upstream-hooks.txt`, the manifest, names each of them with the upstream tests that
exercise the changed code, and `msil/utils/ci/upstream-hooks.ps1` (Windows only) runs those tests
and proves that the list is complete:

```powershell
pwsh -NoProfile -File msil/utils/ci/upstream-hooks.ps1                         # every leg
pwsh -NoProfile -File msil/utils/ci/upstream-hooks.ps1 -Leg manifest,pins
pwsh -NoProfile -File msil/utils/ci/upstream-hooks.ps1 -NoCompile -NoManifest -MinPass 250   # CI
pwsh -NoProfile -File msil/utils/ci/upstream-hooks.ps1 -SelfTest
python msil/utils/ci/check-upstream-hooks.py --check          # the manifest leg alone
python msil/utils/ci/check-upstream-hooks.py --list-lit       # the lit inputs, one per line
python msil/utils/ci/check-upstream-hooks.py --list-compile   # the compile leg's two files
python msil/utils/ci/check-action-pins.py                     # the pins leg alone
python msil/utils/ci/check-action-pins.py --self-test         # the pins checker's self-test
```

| Leg | What it checks | Time |
|---|---|---|
| `manifest` | `check-upstream-hooks.py --check`: every upstream file that differs from the upstream base (below) has a `hook` block, unless it is a file the fork added, on a `fork-only` line; a `fork-only` path must not exist in the upstream base (an upstream file the fork changes needs a `hook` block), and no block or `fork-only` line names an unchanged file (stale); every block says how it is tested (`lit`, `gtest`, `compile`, `utc` or `none <reason>`), every lit pattern matches a file, and every fork test exists; every `gtest` line names one of the gtest leg's binaries (below), whose list in `upstream-hooks.ps1` must equal `GTEST_BINARIES` of `check-upstream-hooks.py`; every name on a `utc` line is one of the `utc` leg's tests, which the check reads from `$utcTests` of `msil/utils/ci/lit-legs.ps1` and `utc_tests` of `msil/utils/ci/lit-legs.sh` and requires to be equal; every `compile` line names a compile-leg file or a header that one of them `#include`s directly, matched by include-path suffix such as `llvm/MC/MCContext.h`, and each compile-leg file has a block with a `compile` line (step 2 below); every exhaustive object-format switch in the whole monorepo, built or not (`case Triple::XCOFF:` or `case MCContext::IsXCOFF:`, with or without `llvm::`, or an unqualified `case XCOFF:` / `case IsXCOFF:`), has an MSIL case label in the same switch (comments and string literals do not count; a `Triple::MSIL` elsewhere in the file does not cover it); the two exemptions are checked per switch, not per file: in a file with a `switch-default` line each XCOFF case label must sit in a switch whose own body (not a nested switch) has a `default:` label or an MSIL case label, and a `switch-other` line exempts only its file's unqualified `case XCOFF:` / `case IsXCOFF:` labels, while its `Triple::XCOFF` and `MCContext::IsXCOFF` labels are checked as in any other file; a `switch-default` line whose file has no XCOFF switch, or a `switch-other` line whose file has no unqualified XCOFF label, is stale; a file whose braces do not match, or whose label sits outside a switch body (a macro), needs at least as many covering labels in code (MSIL case labels, plus `default:` labels in a `switch-default` file) as XCOFF labels that need cover instead (the unqualified labels of a `switch-other` file need none) | 11-20 s |
| `pins` | `check-action-pins.py`: every `uses: <action>@<sha> # <version>` of the `msil-*.yml` workflows under `.github` pins the action at the SHA and version that upstream's workflows use; quoted values are read as well (one pair of single or double quotes is stripped, in the MSIL and the upstream workflows alike); an action upstream never uses (`actions/setup-dotnet`) is printed without failing. Any other `uses:` line of an `msil-*.yml`, one that is neither such a pin nor a local `./...` action or reusable workflow nor a `docker://` image (no `@<ref>`, an unterminated quote, the value on the next line), fails, and so does an MSIL workflow with `uses:` lines but no pin. It also reads `.github/workflows/msil-ci.yml` as text: its windows job must have a step whose `run:` is exactly `pwsh -NoProfile -File msil/utils/ci/upstream-hooks.ps1 -NoCompile -NoManifest -MinPass 250` and whose `if:` is `${{ !cancelled() && steps.<id>.outcome == 'success' }}`, with `<id>` the `id:` of that job's Build step, and which comes after that step; the Build step is the one step of the windows job whose `run:` names `msil/utils/ci/build.ps1`, and the job must have exactly one. So each of these fails: a deleted step, a misspelled id, a Build step without an `id:` (or with a blank one), the step before the Build step or in another job, a changed `run:` line, a guard without `!cancelled()`, no `if:`, no Build step, and two or more Build steps | 1 s |
| `lit` | the upstream lit tests of the `lit` lines (327 today, of the `LLVM`, `Clang` and, from M13, `lld` suites; one of them is the fork test `lld/test/MSIL/dispatch.test`, the only runnable check of `lld/test/lit.cfg.py`'s feature map), in one `msil-lit.ps1` call (the list goes to lit through the response file `build/lit-results/upstream-hooks.rsp`, so it never meets the Windows command-line limit); `check-upstream-hooks.py --lit-verdict` passes lit's JSON report when every FAIL is allow-listed, no allow-listed test passed or went missing, nothing is UNRESOLVED, TIMEOUT or XPASS, every input ran, every lit input under a fork prefix PASSes, and at least `min-pass` (306) tests PASS (XFAIL does not count) | about 1 min |
| `gtest` | all of `TargetParserTests` (at least 700 tests) and `MCTests` (at least 40) through `msil-gtest.ps1 -NoSkip`, except the host-gated `HostTest` cases, `TargetRegistry.MSILLookupWithoutTarget` (it skips whenever MSIL is built) and the `Disassembler` test of a target that `llvm-config --targets-built` does not list, and from M13 all of `LLDAsLibAllTests` (at least 1: `AsLib.AllDrivers` runs every lld flavor in-process, `msil-ld` included). A `gtest` line of the manifest may name only these three binaries | 10-25 s |
| `compile` | the two hooked files the main build never compiles, `llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp` and `lldb/source/Plugins/Process/gdb-remote/GDBRemoteCommunicationClient.cpp`, in the private hooks tree (below): ninja builds the ARM object, and `msil-syntax.ps1` checks both files with `-Werror=switch`, so an object-format enumerator without a case fails | about 47 s |

Each leg writes a row of step `upstream-hooks`, every selected leg runs even after a failure,
and the exit status is 1 if a row failed. The logs, the response file and lit's reports are
`build/lit-results/upstream-hooks*.{log,rsp,json,xml}`. `-Leg` runs only the named legs;
`-NoManifest` and `-NoCompile` print a SKIP line for their leg and write no row; `-MinPass N`
overrides the manifest's `min-pass`. `-SelfTest` runs only the self-tests, as the four rows
`checker`, `pins-checker`, `gtest-filter` and `compile-mutation` of step
`upstream-hooks-self-test`:

- `checker`: `check-upstream-hooks.py --self-test` plants every defect the checks look for (an
  unlisted hook, a stale block, a `fork-only` line for a file of the base, a missing fork test,
  an unmatched glob, a `utc` name outside the leg's list, leg lists that differ, a leg list
  that does not parse or has an unterminated quote, a leg script without the list, `compile`
  lines outside the compile leg, on a `.cpp` file it does not compile and on headers that no
  compile-leg file `#include`s directly, switches without an MSIL case, bad allow lines, a merge
  in progress, failing and stale lit verdicts, a gtest leg whose binaries differ from
  `GTEST_BINARIES`, a file beside a fork prefix) in a temporary git repository; it also checks
  that a file under each of the Phase 2 fork prefixes needs no block and that lit's `lld` suite
  names map to `lld/test/...`. The switch defects are checked per switch: they include a file
  with two switches and one MSIL case, a file whose only MSIL mention is a comment, a
  `switch-default` file with a second XCOFF switch without `default:`, a `switch-other` file
  with a qualified XCOFF switch, and stale `switch-default` and `switch-other` lines.
- `pins-checker`: `check-action-pins.py --self-test` plants, in temporary `.github` trees,
  stale unquoted, single-quoted, double-quoted and comment-less pins, unparsable `uses:` lines,
  an MSIL workflow without a pin, and the wiring defects of the upstream-hooks step: a
  misspelled id in the guard, a missing or blank `id:` on the Build step, a deleted
  upstream-hooks step, one moved to another job or before the Build step, a changed `run:`
  line, a guard without `!cancelled()` or no guard, no Build step, and no `msil-ci.yml`; each of
  them must fail, while the clean tree (quoted pins, local actions, a reusable workflow, a
  `docker://` image and a correctly wired upstream-hooks step) must pass.
- `gtest-filter`: the `MCTests` filter is derived for four target lists.
- `compile-mutation`: copies of both compile-leg files without their MSIL case, in a temporary
  directory, must fail `-Werror=switch` with `not handled in switch` while the originals pass.

The self-test needs the hooks tree (`compile-mutation` fails without its `build.ninja`) and
writes nothing inside the repository.

**The upstream base.** The manifest leg compares the working tree (uncommitted and untracked
files included, so a milestone's own work counts) with, in this order: the commit in
`$env:MSIL_UPSTREAM_BASE`; `MERGE_HEAD` while a merge is in progress (the upstream commit being
merged, so the merge gate sees the fork's footprint on the new upstream); else the second parent
of the last first-parent commit whose subject is `[MSIL] Merge upstream llvm-project main at
<sha>`. Upstream merge commits therefore keep that subject. The leg prints the base and why it
chose it; finding none (a shallow clone, for example) is a failure, never a pass. Changes under
the fork's own directories (`llvm/lib/Target/MSIL/`, `llvm/test/CodeGen/MSIL/`,
`llvm/docs/MSIL/`, `llvm/unittests/Target/MSIL/`, `clang/test/CodeGen/MSIL/`, `msil/`,
`.github/msil/`, and the Phase 2 directories `lld/MSIL/`, `lld/test/MSIL/`,
`llvm/include/llvm/CLI/`, `llvm/lib/CLI/`, `llvm/unittests/CLI/`, `llvm/tools/llvm-cil-dump/`,
`llvm/test/tools/llvm-cil-dump/`, `llvm/tools/llvm-cil-copy/`, `llvm/test/tools/llvm-cil-copy/`
and `llvm/test/MC/MSIL/`; `FORK_PREFIXES` of `check-upstream-hooks.py`) are never hooks.

**Extending the manifest.** An upstream merge, or a milestone that changes another upstream
file, updates the manifest in the same commit (the format is documented in its header):

1. Run `upstream-hooks.ps1` on the uncommitted merge (or change). The manifest leg names every
   unlisted hook and every stale entry, and the lit and gtest legs show which upstream tests now
   fail.
2. For a new hooked file add a `hook <path>` block with `lit <path-or-glob>` lines for the
   upstream tests that reach the changed code, `gtest <exe> [<filter>]`, `compile`,
   `utc <test>...` or `none <reason>`, and a `fork <test>` line for each fork test that pins
   the MSIL side. A fork test that is the only runnable check of a hook also gets a `lit` line
   (`lld/test/MSIL/*.test` for the feature map of `lld/test/lit.cfg.py`), so that the lit leg
   runs it and requires it to PASS; a `fork` line only has to exist. A `utc` line names only
   tests that the `utc` leg runs: each name must be in `$utcTests` of
   `msil/utils/ci/lit-legs.ps1` and `utc_tests` of `msil/utils/ci/lit-legs.sh` (the two lists
   stay equal), so a test that the leg should run is added to both lists first.
   Drop the block of a file the fork no longer changes. `compile` is valid only on the compile
   leg's two translation units and on a file that one of them `#include`s directly
   (`llvm/include/llvm/MC/MCContext.h` and `llvm/include/llvm/TargetParser/Triple.h` today);
   the manifest leg rejects it anywhere else, and each of the two units needs a block with a
   `compile` line. The units are listed once, in
   `COMPILE_FILES` of `check-upstream-hooks.py`, which `upstream-hooks.ps1` reads through
   `check-upstream-hooks.py --list-compile`.
3. A file that the fork adds inside an upstream directory, one the upstream base does not
   have, gets a `fork-only <path>` line. `fork-only` is only for such files: an upstream file
   that the fork changes always needs a `hook` block, and the manifest leg rejects a
   `fork-only` line whose path exists in the base. A new exhaustive object-format switch gets
   an MSIL case. Otherwise a `switch-default <path>` line covers the XCOFF switches of its file
   that have their own `default:` label (every other XCOFF switch of that file still needs an
   MSIL case), and `switch-other <path> <reason>` covers the unqualified `case XCOFF:` /
   `case IsXCOFF:` labels of a file where `XCOFF` belongs to another enumeration (its
   `Triple::XCOFF` and `MCContext::IsXCOFF` labels still need MSIL cases). Drop the line when
   its file no longer has an XCOFF switch (`switch-default`) or an unqualified XCOFF label
   (`switch-other`); the leg reports it as stale.
4. Run the leg again until it passes, and check that `--list-lit` grew with the new tests.

**The allow-list.** An `allow <test> <reason>` line lets one lit input FAIL. It takes only an
environment failure of that single test file (a tool or runtime the build cannot provide), and
the reason names the tool; never a glob, a directory or a real FileCheck mismatch. An
allow-listed test that passes is stale and fails the leg. A real failure is fixed in the fork,
or the test is dropped from its `lit` line with a comment. The list is empty today; tests that
need tools this build lacks (`llvm-dwarfdump`, `llvm-objcopy`) are left out of the manifest
instead, and so, so far, are the two that need `llvm-mc`, which the build has had only since
M15 (the manifest's comments name them).

**The private hooks tree.** The compile leg works in `-HooksBuildDir`, else
`$env:MSIL_HOOKS_BUILD_DIR`, else `C:\llvm-msil-hooks-build`: projects clang and lldb, the ARM
target only, no tests and no optional lldb dependency. When the tree has no `build.ninja` the
leg configures it (about 4 min); it then builds the ARM object (about 8 min the first time,
5-13 min after an upstream merge) and checks both files (about 47 s on every run). A new tree
takes about 140 MB (the development host's tree is about 460 MB, because the recon also built
the lldb object there). The leg takes the tree's own exclusive build lock and runs cmake and
ninja there itself ([msil/utils/build/README.md](../../../msil/utils/build/README.md)); it
refuses the main build directory, never builds the lldb object (its order-only dependencies are
about 1,000 unrelated TUs), and never deletes or cleans the tree: for a broken cache run
`cmake --fresh` on it by hand or point `MSIL_HOOKS_BUILD_DIR` at a new directory. What stays
uncovered: the lldb file is only syntax-checked, and no ARM or lldb test runs.

**Where it runs.** `run-m1-gate.ps1` runs every leg (step 6, after the lit legs;
`-SkipUpstreamHooks` leaves it out). The Windows CI job runs `-NoCompile -NoManifest -MinPass
250`: its shallow checkout has no merge commit for the manifest leg, the hooks tree is a
development-host cost, and its build has no WebAssembly target, so the list's WebAssembly tests
are UNSUPPORTED there (the development host's gate enforces the manifest's `min-pass`). This
curated check runs on every gate run and every Windows CI run; the Linux job's `--full` leg `upstream` (`ninja
check-llvm check-clang`, hours) stays the complete upstream regression run, for nightly and
manual full CI runs.

## 8. When something goes wrong

| Message | Cause and fix |
|---|---|
| `CIL object files are not supported for MSIL target '...' yet; use -flto to write LLVM bitcode objects, or -S to write ILAsm text` (before M13: `emitting object files is not yet supported for MSIL target`) | `-fno-lto -c`: there are no CIL object files before M19. Drop `-fno-lto` (`-c` writes bitcode, which `msil-ld` links; section 3) or use `-S`. Before M14 every `-c` and every link stopped here |
| `msil-ld: error: ThinLTO, --lto-partitions and LTO without --lto-emit-asm need CIL object output, which this msil-ld does not have; use --lto-emit-asm (clang: -Wl,--lto-emit-asm) to write ILAsm text` | the link has no `-Wl,--lto-emit-asm`, or its inputs are ThinLTO bitcode (`-flto=thin`), or it asks for several LTO partitions (`-flto-partitions=`): until M20 `msil-ld` writes only the ILAsm text of the whole program. Link with `-Wl,--lto-emit-asm -o prog.il` and assemble with `ilasm -exe` (section 3). M13's stub said `linking is not implemented yet` for every link |
| `msil-ld: error: undefined symbol: <name>` | neither an input nor a native library of the link defines `<name>`. Add the library that defines it (`-l<name>`, a library file, the right `--sysroot`; `-Wl,--print-imports` shows what bound where). The `>>> referenced by <file>:(<function>)` lines name the live references after LTO, and `ld-temp.o` a call that the optimizer introduced. `undefined hidden symbol` is the same for a hidden reference. Before M15 every function that was not a Phase-1 import (D14) ended here |
| `msil-ld: error: unable to find library -l<name>` | no `-L` directory has the library: often one of the driver's default libraries (section 3). On Windows link from a Visual Studio developer prompt (`LIB`); for a Linux triple on Windows add `--sysroot=<a Linux sysroot>` (section 7's E7 or glibc sysroot), or `-nodefaultlibs` when the program calls no native function. After `-Wl,-Bstatic` only static libraries (archives with bitcode members) count; a native archive, a library of the other OS's object format (a native DLL or a COFF import library for a Linux triple) and a .NET assembly are skipped (`-Wl,--verbose` says so) |
| `msil-ld: error: unable to find library from dependent library specifier: <name>`, `>>> requested by <file>` | a `#pragma comment(lib, "<name>")` (or another linker option in the bitcode) names a library that no `-L` directory has: add its directory, or link with `-Wl,--no-dependent-libraries` |
| `msil-ld: error: native function 'qsort' (<module>) takes a C function pointer; passing functions to native code needs phase 3 thunks` | the program calls, or takes the address of, a native function of the callback list (`qsort`, `bsearch`, `signal`, `pthread_create`, `CreateThread`, ...; Status.md section 2), whose native code could call back into the program, which needs phase 3. The message names the symbol that glibc's headers make of the call: `signal` is `__sysv_signal` under `-std=c99`, `c11` or `c17`, and `ftw`, `nftw` and `scandir` are `ftw64`, `nftw64` and `scandir64` with `-D_FILE_OFFSET_BITS=64`. Write the function in C (a `qsort` of your own wins over the library's), or, when no function of the program is passed (`signal(SIGPIPE, SIG_IGN)`), declare it with an explicit `import_module` |
| `msil-ld: error: native function 'printf' (<module>) is variadic; calls of native variadic functions are not supported for MSIL yet (phase 3)` | a P/Invoke cannot pass C variadic arguments (phase 3). Call a non-variadic function (`puts`, `fputs`) instead |
| `msil-ld: error: native function '<name>' (<module>) takes or returns a value that a P/Invoke cannot pass natively yet (phase 3)` | the native function passes a struct by value, an `__int128`, a vector or a `_Float16`; wrap it in a native function that takes pointers. On x86_64 and aarch64 Linux and x86_64 MinGW it is also a native `long double` function (`sqrtl`, `expl`, `powl`, `strtold`, `nexttoward`, ..., and glibc's `_Float64x` and `_Float128` functions such as `sqrtf64x`, `strtof64x` and `sqrtf128`; with `>>> referenced by` lines), whose native `long double` is not the `double` that `long double` is in MSIL (Status.md section 3): call the `double` function (`sqrt`, `strtod`), which is the same function in an MSIL program. windows-msvc and `aarch64-w64-windows-gnu` bind them |
| `msil-ld: error: importing native data symbol '<name>' (<module>) is not supported yet` | the program uses a variable that a native library defines (`environ`, `stderr`, ...), or an `extern` variable with `import_module`; native data imports come with M20. `'<name>' in <module> is data, but <file> calls it as a function` is a function declaration that binds to native data (a call skips a COFF `DATA` or `CONST` import and a DLL's data export, and binds the function of a later library when there is one) |
| `msil-ld: error: symbol '<name>' is defined only in native object code (<archive>(<member>)); MSIL cannot link machine code` | the only definition is machine code in a native archive (glibc's `atexit` in `libc_nonshared.a`, a static library of native objects): build that code for the MSIL triple, or link a shared library or DLL of it |
| `msil-ld: error: symbol '<name>' in <module> has only the non-default version <name>@<ver>, which a P/Invoke cannot bind`, `... is thread-local data and cannot be imported` (for `errno` followed by `; use <errno.h>`), `... is <ver> (GLIBC_PRIVATE) and cannot be bound` | the library defines the name only in a form that `dlsym` does not return: a hidden version, thread-local data (`errno`: include `<errno.h>`, whose `errno` calls `__errno_location`; other thread-local data, such as `h_errno` or a library's own `__thread` variable, gets the text without the advice), or glibc's private interface |
| `msil-ld: error: <file>: ELF machine <EM_x> does not match target <triple>` (also `COFF machine` and `PE machine`) | a native library of another architecture (or ELF class) than the triple: an AArch64 or 32-bit library in an x86_64 link, say. Point `-L` or `--sysroot` at the triple's libraries |
| `msil-ld: error: <path>: ELF file cannot be imported for target <triple>`, `<path>: PE/COFF file cannot be imported for target <triple>` | a native library of the other OS's object format, given as a file, by `-l:<file>` or by a linker script: an ELF `.so` in a link for a Windows triple, a DLL or an import library in a link for a Linux triple. As lld-link and ld.lld do, `msil-ld` reads PE/COFF libraries for a Windows triple and ELF ones for a Linux triple (section 3): a P/Invoke of the other kind would fail to load at run time. Link against the triple's libraries; `-l<name>` passes over such a file (`-Wl,--verbose`: `skipping <path>: ...`) |
| `msil-ld: error: <script>:<line>: there is a cycle in linker script inputs` | an `INPUT` or `GROUP` name or a `-l<name>` of a GNU ld script (the `>>>` lines show it) leads back to a script that is still being read: a script that names itself, or two scripts that name each other, directly or through `-l`. That input is skipped; remove it. A script read again after it was read to its end (`-lc` given twice) is no cycle |
| `msil-ld: error: --ucrtbase-imports needs a Windows target, not <triple>` | `--ucrtbase-imports` (the driver passes it on windows-msvc) given for a Linux triple |
| `msil-ld: warning: <path>: skipping the .NET assembly that -l<name> found; use --reference for .NET reference assemblies` | `-l` found a managed `.dll`; `msil-ld` links .NET assemblies from M21 (references) and M22 (libraries) |
| `msil-ld: error: library call '<name>' was not resolved by msil-ld` | code generation calls a library function that no native library of the link defines: `floor`, `ceil` or `round` (`llvm.floor` and its kin at `-O2`), `sqrt` or `exp` under `-fno-math-errno`, ..., typically in a link with `-nodefaultlibs` or without the C or math library. Add that library (drop `-nodefaultlibs`, or add `-lm`, `-lc` or the library file) |
| `msil-ld: error: runtime helper '<name>' is not available for MSIL yet (phase 3)` | code generation needs a compiler-rt helper (those of the `runtime helper '__fixdfti'` row below) or an `__atomic_*` function, which atomic operations become (`_Atomic` variables, the `__atomic_*` builtins, and the `volatile` accesses of windows-msvc stock bitcode without `-fno-ms-volatile`, section 3). Both come with phase 3 (atomics: plan D36). A helper that clang itself calls (`__muldc3` for `_Complex` multiplication) is `undefined symbol: <name>` in a link, unless a native library of the link defines it |
| `msil-ld: error: undefined function '<name>' was not resolved by msil-ld` | the backend found, in `msil-ld`'s text, a function that the link did not bind, a backstop that `msil-ld`'s own errors normally precede; please report it with the inputs (`-Wl,--reproduce=<file>.tar`) |
| `msil-ld: error: <file>: target triple <t> is incompatible with <t0>` | the inputs were compiled for different MSIL triples (another architecture, OS or environment); compile every file for the triple of the link |
| `msil-ld: error: unsupported target triple '<codegen triple>' for MSIL` | the link's triple (`--target`, else the first bitcode input's with `-msil` appended) is not one of D6's: stock-clang bitcode for Darwin or i686, say (section 3). Compile the inputs for an x86_64 or aarch64 Linux, windows-msvc or windows-gnu triple |
| `msil-ld: warning: <file>: linking bitcode for <triple> as <triple>-msil; stock-clang bitcode is a compatibility path` | an input is bitcode for the native triple (plan Q12); it links, with the limits of section 3 (on windows-msvc compile it with `-fno-ms-volatile`) |
| `msil-ld: error: <file>: linking CIL object files and .NET assemblies is not supported yet` | a .NET assembly or a CIL object is an input of the link: `msil-ld` links CIL objects from M20, takes reference assemblies from M21 and libraries of this toolchain from M22 |
| `assembling ILAsm text is not supported for MSIL target '...'; compile C or LLVM IR sources instead, or use -S to write ILAsm text` | an assembler input (`-x assembler`, a `.s` or `.S` file) or an assemble step (`-via-file-asm`, clang-cl `/FA` or `/Fa`): ILAsm text goes to ilasm, not to clang (sections 3 and 4); use `-S` for the IL |
| `invalid linker name in argument '-fuse-ld=...'` | MSIL links run only `msil-ld`: use no `-fuse-ld`, `-fuse-ld=lld`, an absolute path, or `--ld-path=` (section 3) |
| `undefined function 'f' is not defined in this module and is not a Phase-1 import` | with `-S` and `llc`, every function must be defined in the same file, except `puts`, `putchar` and the C math library (Status.md) and functions declared with `import_module`, optionally with `import_name` for the entry point (section 3; `import_name` alone needs `msil-ld`). Functions of other files and of native libraries link with `msil-ld` (section 3) |
| `runtime helper '__fixdfti' is not available for MSIL yet (phase 3)` | the code needs a compiler-rt helper that the backend would call: conversions between floating point and `__int128` under strict floating point, `_Float16`/`__bf16` arithmetic and conversions (`__extendhfsf2`), `__bf16` parameters, arguments and return values (`__truncsfbf2`, `__extendbfsf2`) (Status.md section 3). `__int128` division and the ordinary conversions are expanded inline since M12, so a build from before M12 also names `__divti3`, `__udivti3`, `__floattidf`, ... here. Helpers that clang itself calls (`__muldc3` for `_Complex` multiplication, `__atomic_*` for atomics) are reported as undefined functions instead. In a program that `msil-ld` links, see the `msil-ld` rows above |
| `unknown target triple '...-msil'` | Darwin and i686 MSIL triples come in later phases; Windows MSIL triples name `msvc` or `gnu`. Any MSIL triple that is not a Linux, windows-msvc or windows-gnu one (Darwin, FreeBSD, Cygwin, ...) gets this error, also for a command that only links |
| `... is not supported for MSIL yet` | a Phase-1 limitation; Status.md lists them |
| `global variable @p_init in section ".CRT$XCU" is not supported for MSIL yet (phase 2 CIL objects)` | a variable with `__attribute__((section(...)))` or `__declspec(allocate(...))`; the section would be lost, and a constructor registered in `.init_array` or `.CRT$XCU` would never run. Use `__attribute__((constructor))`, or drop the section (Status.md section 3) |
| `alias @a is not supported for MSIL`, `ifunc @f is not supported for MSIL` | only aliases of a defined function (at offset 0) and of a variable are lowered (Status.md section 2); ifuncs are never supported |
| `inline assembly is not supported for the MSIL target '...'` (clang) or `inline assembly is not supported for MSIL` (llc) | only empty `asm` statements work: with no operands, or with inputs, read-write (`"+r"`), tied (`"0"`) and memory outputs. An output that is not tied to an input (`"=r"`) gets no value from an empty `asm` and is rejected, and so is a condition-flag output (`"=@ccz"`, `"+@ccz"`), which native code sets from the flags after the statement (Status.md section 2) |
| `the va_arg instruction is not supported for MSIL yet (phase 3 variadics)` | LLVM IR with a `va_arg` instruction (variadic functions are phase 3); before M12 one of a struct or array type crashed llc instead |
| `The library 'hostpolicy.dll' required to execute the application was not found` (Windows) or `The library 'libhostpolicy.so' required to execute the application was not found` (Linux), exit `0x80008083` or 131 | no `<name>.runtimeconfig.json` next to the `.dll` (section 5) |
| `You must install or update .NET to run this application` | the runtimeconfig names a runtime that is not installed; install it or add `"rollForward": "Major"` |
| `System.DllNotFoundException` naming `libc`, `libm.so.6`, `ucrtbase` or `msvcrt` (or, for `msil-ld`'s output, `libc.so.6`, `api-ms-win-crt-...dll`, ...) | the assembly was built for another OS than the one running it. For a native library of your own: it is not next to the program (section 3, "Native libraries and P/Invokes") |
| `System.BadImageFormatException` / `FileLoadException` at start-up | the image's machine (`-x64`, `-arm64`) does not match the process |
| `System.InvalidProgramException` | a backend bug: the JIT rejected a method. `msil-jitall.py` names it; please report it with the `.il` |
| `error: unsupported option '-fsplit-machine-functions' for target '...-msil'`, and the same for `-fbasic-block-sections=all` or `=list=`, `-fbasic-block-address-map` and `-fpartition-static-data-sections` | these options put functions, blocks or data in ELF sections, and CIL methods have no sections; drop the option (Status.md section 3). `-fbasic-block-sections=none` is accepted |
| `warning: basic block address map is not supported for MSIL; ignored` | `-fbasic-block-sections=labels` (a deprecated alias, which the driver passes on as `-fbasic-block-address-map`), or the address map requested through cc1 or llc; the output is the same as without it |

To look inside the backend, `llc -stop-after=<pass>` and `-stop-before=<pass>` print the machine
code (MIR) at every MSIL pass from instruction selection on: `msil-isel`, `msil-argument-move`,
`msil-set-mem-flags`, `msil-clean-code-after-trap`, the generic passes, then
`msil-nullify-dbg-value-lists`, `msil-replace-phys-regs`, `msil-optimize-live-intervals`,
`msil-reg-stackify`, `msil-reg-coloring`, `msil-explicit-locals`, `msil-reg-numbering` and
`msil-stack-check` (`llc -debug-pass=Arguments` prints the whole pipeline). Stopping works at
every one of them, and llc runs the machine verifier on the MIR it prints there. The MIR of
instruction selection reads back: `llc -run-pass=none x.mir` verifies it again, and
`llc -start-after=msil-isel x.mir -o x.il` finishes the compile with the same IL as the full
pipeline (`llvm/test/CodeGen/MSIL/stop-after-isel.ll`). Reading MIR back has three limits, all
from upstream code; none of them changes the IL of a normal compile:

- MIR with a floating-point infinity or NaN constant reads back at no stop point, `msil-isel`
  included. A `CONST_F32` or `CONST_F64` with such an immediate prints as `float +inf`,
  `double -inf` or `double +qnan`, and the MIR lexer rejects that spelling:
  `llc -run-pass=none` and `-start-after=msil-isel` exit 1 with
  `x.mir:<line>:<column>: expected a floating point literal`. This is an upstream MIR printer
  and parser mismatch that WebAssembly shows too; several floating-point tests of
  `msil/test/M1` hit it.
- For a function that takes block addresses (`blockaddress`, computed goto), the IL of
  `-start-after=msil-isel` lacks the `@tmpN: // Address of block that was removed by CodeGen`
  labels that the full pipeline prints; every other line of the IL is the same. Nothing refers
  to those labels: IndirectBrExpand, which runs before instruction selection, replaces each
  `blockaddress` with the block's number. The full pipeline still prints a label for every
  block whose address the input IR took, while the IR inside the MIR no longer takes any.
- MIR printed at `branch-folder` or any later stop point, for a function whose `switch` became
  a jump table, cannot be read back yet: `llc -run-pass=none` asserts
  `Cannot create an empty jump table!`, because BranchFolder empties the table, which the MSIL
  `switch` (`BR_TABLE_I32`) does not reference. This is an upstream MIR printer and parser
  issue that WebAssembly shows too.

A build from before milestone M11 aborts at `-stop-after=msil-isel` and
`-stop-before=msil-argument-move` with
`Bad machine code: Using an undefined physical register`. `-print-after-isel`,
`-print-after=<pass>`, `-debug-pass=Structure` and `-verify-machineinstrs` work in llc and,
through `-mllvm`, in clang.
