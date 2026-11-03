# Getting started with the MSIL backend

This fork of llvm-project adds an experimental LLVM target, `MSIL`, that compiles C to IL-only
.NET assemblies (the design is in [Design.md](Design.md)). Phase 2, complete since milestone
M23, gives it the native workflow: `clang -c` compiles each C file to a CIL object (an ECMA-335
netmodule with a link-info record and a COFF symbol table), the tree's `llvm-ar` collects
objects into archives, and a link through clang runs `msil-ld`, an lld flavor, which links
objects, archives, LLVM bitcode (with LTO), native libraries and library assemblies and writes
the program's assembly image itself (section 3). `msil-ld` binds the functions and variables
that no file defines to the native libraries a native link of the same program reads, so a
program calls the C library, the Win32 API and native libraries of your own through P/Invoke
(section 3, "Native libraries and P/Invokes"); it binds the .NET base class library against the
reference assemblies you give it with `-Wl,--reference=` (the .NET Framework 4.8 reference
assemblies or a .NET 8 or .NET 10 reference pack, as the C# compiler does), or without them
against a built-in `[mscorlib]` table (section 3, "Reference assemblies"); and `clang -shared`
(clang-cl `/LD`) links a library assembly, a DLL whose exported C functions are public static
methods of a class, which programs, other libraries and C# programs link against (section 3,
"Libraries"). One image runs on .NET 8, .NET 10 and, for Windows triples, .NET Framework 4.8,
unless it is bound to a .NET reference pack. Phase 3, from M24, adds the C features that need
the toolchain's runtime support: compiler-rt's helpers as an archive of CIL objects (M25;
section 3, "The helper archive"), and variadic functions with the C library's `printf` family,
VLAs and `alloca` outside loops (M26; section 3, "Variadic functions, `printf` and VLAs"). The
portable triple `msil64-unknown-dotnet` (P-1a) names no platform at all: its assemblies contain
no P/Invoke and run on every 64-bit .NET, for now as freestanding programs, until its own C
library `c.dll` arrives (section 3, "The portable target").

Phase 1's route stays available as the oracle that the tests compare with: `clang -S` writes the
ILAsm text of one file, which `ilasm` assembles (sections 3 and 4). Every instruction the
backend emits is one CIL instruction whose binary encoding, which `llc -show-mc-encoding`
shows, equals ilasm's (section 3, "Instruction encodings"), the tools `llvm-cil-dump` and
`llvm-cil-copy` read assemblies and write them again without ilasm (section 6), and section 3
also has a recipe for Rust bitcode ("Rust bitcode"). What works and what does not is in
[Status.md](Status.md), with the Phase 2 exit criteria and how they are met in its section 1;
how the work was planned and built is in [ImplementationPlan.md](ImplementationPlan.md).

Like LLVM itself, the project ships compiler tools only (Design.md, scope line). The compiler
and the linker write the `.il` file, the CIL object or the linked image and nothing else: no
`runtimeconfig.json`, no `.exe.config`, no apphost, no project or package files. Section 5
shows how to write the runtimeconfig yourself, and how to name an executable.

## 1. What you need

| For | Windows | Linux |
|---|---|---|
| Building | Visual Studio with the x64 C++ tools and its bundled clang-cl, lld-link and llvm-lib (the development host has Visual Studio 2026; any C++17 compiler works with the plain CMake route), CMake, Ninja, Python 3. The PowerShell helpers of `msil/utils/build` use an x64 developer environment that is already loaded (a developer prompt, or CI's msvc-dev-cmd), else the newest Visual Studio with the x64 C++ tools that vswhere finds, in any edition ([msil/utils/build/README.md](../../../msil/utils/build/README.md)) | a C++17 compiler (clang or gcc), CMake, Ninja, Python 3 |
| Assembling | the .NET Framework ilasm, `%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe` (always present), or the NuGet `runtime.win-x64.Microsoft.NETCore.ILAsm` | the NuGet package `runtime.linux-x64.Microsoft.NETCore.ILAsm` (10.0.12 is what the tests use) |
| Running | .NET 8 or later; .NET Framework 4.8 for Windows triples | .NET 8 or later |
| Running the tests | everything above, plus a .NET SDK with the `Microsoft.NETCore.App.Ref` 8.x targeting pack (the .NET 8 SDK has it; it builds the `jitall` checker and the metadata oracles `mdump` and `mdcanon`, offline), the dotnet tool `dotnet-ilverify`, ildasm and PEVerify (Windows SDK, `NETFX 4.8 Tools`), the Visual Studio "C++ Clang tools" (their compiler-rt builtins and `ld.lld` serve the native oracles), and optionally WSL Ubuntu with python3 and curl or wget for the Linux legs (the legs' own .NET runtimes and Linux libraries come from the host setup of section 7, without root); for the reference legs (M21) the .NET Framework 4.8 reference assemblies (the 4.8 targeting pack or Developer Pack, else the NuGet package below) and the `Microsoft.NETCore.App.Ref` 8.x and 10.x packs (the .NET 8 and .NET 10 SDKs) | everything above, plus the .NET SDK (with the `Microsoft.NETCore.App.Ref` 8.x targeting pack, and 10.x for the `refs-net10` leg), `dotnet-ilverify` and the NuGet `runtime.linux-x64.Microsoft.NETCore.ILDAsm`; the restore below also fetches the .NET Framework 4.8 reference assemblies for the `refs-net48` leg; since M25 the hosted tests' native oracle needs compiler-rt's builtins for the host's clang (the file that `clang -rtlib=compiler-rt -print-libgcc-file-name` names must exist, as a distribution's compiler-rt package provides it, section 7) |

The NuGet ilasm and ildasm do not need a .NET project of yours. The repository has a
download-only project for them:

```sh
dotnet restore msil/utils/tools/msil-tools.proj -p:MsilToolsRid=linux-x64   # or win-x64
# ilasm: <NuGet global packages>/runtime.linux-x64.microsoft.netcore.ilasm/10.0.12/runtimes/linux-x64/native/ilasm
chmod +x ~/.nuget/packages/runtime.linux-x64.microsoft.netcore.il*/10.0.12/runtimes/linux-x64/native/il*
dotnet tool update --global dotnet-ilverify --version 10.0.12
```

NuGet does not keep the executable bit, hence the `chmod`. `dotnet nuget locals global-packages
--list` prints the packages folder when it is not `~/.nuget/packages`. The same restore
downloads `Microsoft.NETFramework.ReferenceAssemblies.net48` (the version
`MsilNet48RefAssembliesVersion` of the project, 1.0.3), whose `build/.NETFramework/v4.8`
directory holds the .NET Framework 4.8 reference assemblies, on a host without the .NET
Framework too (section 3, "Reference assemblies").

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
python3 msil/utils/build/build-builtins.py      # the helper archives; python on Windows
```

`lld` also makes `msil-ld`, the linker that `clang` runs for MSIL triples, and `llvm-ar`
makes archives of CIL objects and bitcode (section 3); `clang` and `llc` alone are enough for
`clang -S`. The last line builds the helper archives (below, "The helper archives").

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
python msil/utils/build/build-builtins.py
```

The second line builds every target the tests need, and the third the helper archives: with
`-Standard`, `msil-build.ps1` builds the targets of `msil/utils/ci/build-targets.txt`, in the
file's order, and then any target named after the switch (`msil-build.ps1 -Standard
llvm-dwarfdump`). That file is the one build-target list of the gates and of CI (plan 5.6.11):
`msil/utils/ci/build.ps1` builds it the same way, and on Linux `bash msil/utils/ci/build.sh
build` builds it with ninja (both then build the helper archives and jitall, section 7). Its
comments say why each target is there. Since milestone M17 it holds the following (M18-M23 added
nothing; the phase gate builds the same list):

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
  the upstream tests of hooked upstream files and for the MSIL tests of the bitcode symbol table
  (`llvm-nm` and `llvm-ar` also read the COFF view of CIL objects, M19);
- `yaml2obj` and `llvm-mc` (M15), which make the native libraries of the `msil-ld` tests in
  `lld/test/MSIL`: ELF shared objects with symbol versions, and MinGW's long-format import
  members (`yaml2obj` also makes the images of the `llvm-cil-dump` and `llvm-cil-copy` tests,
  and from M18 the MC tests of `llvm/test/MC/MSIL` check that `llvm-mc` refuses MSIL assembly
  input cleanly);
- `llvm-cil-dump` and `llvm-cil-copy` (M17), the tools of the ECMA-335 library in
  `llvm/lib/CLI` (section 6).

**The helper archives (M25).** They are not a ninja target: `msil/utils/build/build-builtins.py`
compiles compiler-rt's builtins, as `msil/lib/builtins/sources.txt` lists them, with the tree's
clang into one archive of CIL objects per MSIL triple, `clang_rt.builtins`, which clang links
into every program that calls a compiler-runtime helper (section 3, "The helper archive"); since
M26 the windows-msvc archives also hold the UCRT's `printf` family for code that declares it
without `<stdio.h>` (section 3, "Variadic functions, `printf` and VLAs"). Each
archive is plan 5.14.3 A1's per-target file in the resource directory (what
`clang -print-resource-dir` prints): `<resource dir>/lib/<triple>/clang_rt.builtins.lib` for the
`*-windows-msvc-msil` triples and `<resource dir>/lib/<triple>/libclang_rt.builtins.a` for the
others, in the shared build `build/lib/clang/24/lib/<triple>/`. The script writes, checks and
removes only that file (`--out <dir>` writes `<dir>/<triple>/<that name>` instead). Once the
file exists, `clang --target=<triple> -print-libgcc-file-name` names it, and the driver links
it. While it does not, the old layout's native compiler-rt, if the resource directory has it
(`<resource dir>/lib/<os>/[lib]clang_rt.builtins-<arch>.<ext>`, from a compiler-rt build with
`LLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF`), is the file that clang names instead: the script
never touches such a file, and if clang still names it after a build, the script fails with a
usage error that names it. By default the script builds the two triples that the tests run,
`x86_64-pc-windows-msvc-msil` and `x86_64-unknown-linux-gnu-msil`; `--triple <triple>`
(repeatable) names others, and `--all` adds the x86_64 MinGW and musl triples and the aarch64
Linux and windows-msvc ones. Every member depends on clang and the backend, so run it after
every build: a stamp beside each archive makes it rebuild only an archive whose clang, sources
or command changed (about 15 seconds for the two default triples with `--jobs 4` on a quiet host,
nothing when all are current), and `--check` builds nothing and exits 1 when an archive is
missing or stale. `--list` prints each triple's members, `--check-list` checks `sources.txt`
against compiler-rt's `CMakeLists.txt` (after an upstream merge), `--build-dir <dir>` names the
build (by default `$MSIL_BUILD_DIR` when it is set and not empty, else `build`, as for
`msil-build.ps1`, `msil-lit.ps1` and the other build scripts; `--self-test`, `--list` and
`--check-list` use no build), and `--self-test` checks the script offline; it exits 1 for a
failed check or compile and 2 for a usage error. `msil/utils/ci/build.ps1` and `build.sh` run it
after their ninja step, so the gates and CI link current archives; `msil-build.ps1` does not
([msil/utils/build/README.md](../../../msil/utils/build/README.md)).

A first build compiles about 3,700 steps; afterwards a change in `llvm/lib/Target/MSIL` rebuilds
the target and relinks in minutes. `llc --version` lists `msil - MSIL (.NET CIL as ILAsm text)`
when the target is in.

## 3. Compile and link a program

Programs are C, in one file or in several. `clang -c` compiles each file to a CIL object, the
tree's `llvm-ar` collects objects into archives, and a link through clang runs `msil-ld`, which
writes the program's image (below, "Programs of several files"); `-Wl,--reference=` names the
reference assemblies that `msil-ld` binds the .NET base class library against (below,
"Reference assemblies"), and `-shared` links a library assembly instead of a program (below,
"Libraries"). A program that `msil-ld` links may call the functions of the native libraries of
the link, the C library and the Win32 API among them, except those that take a function
pointer, such as `qsort` (below, "Native libraries and P/Invokes"); since M26 that includes
variadic ones such as `printf` (below, "Variadic functions and `printf`"). Declare them yourself
or use clang's freestanding headers (`stdint.h`, `stddef.h`, `stdbool.h`, `limits.h`,
`float.h`). A C library's own headers work through the native triple's header search
(`--sysroot`, the `INCLUDE` of a Visual Studio developer prompt; the hosted tests of section 7
use glibc's and the UCRT's), but the examples of this guide are freestanding, except the
`printf` example.

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

Compile and link it for the platform the program will run on:

```sh
# Windows (UCRT), from a Visual Studio x64 developer prompt, whose LIB has the default libraries:
clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -c fib.c        # fib.o, a CIL object
clang --target=x86_64-pc-windows-msvc-msil -O2 fib.o -o fib.exe               # msil-ld writes fib.exe
fib.exe                  # .NET Framework 4.8: prints 6765, exit status 55
dotnet fib.exe           # .NET 8 or later, with fib.runtimeconfig.json next to it (section 5)

# Linux (glibc): the link reads a Linux C library; on Windows give it a sysroot (section 7).
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -c fib.c
clang --target=x86_64-unknown-linux-gnu-msil -O2 --sysroot=<sysroot> fib.o -o fib.exe
```

`clang --target=<triple> -O2 -ffreestanding fib.c -o fib.exe` compiles and links in one
command. The image references only `mscorlib`, so the one file runs on .NET 8, 9 and 10 and,
for a Windows triple, on .NET Framework 4.8; how to run it, and when an executable is named
`.dll` instead, is in section 5. `clang -S` writes the ILAsm text of the file instead, the route
of Phase 1 that the tests keep as an oracle (below, "One file to ILAsm text").

- An MSIL triple is the native triple plus `-msil`. clang uses the native target's ABI and
  predefines (`__x86_64__`, `__linux__` or `_WIN64`, ...) and adds `__MSIL__`, and since M25
  `__ELF__` on the Linux triples, as their native object format is ELF; no MSIL triple reports
  atomics wider than 8 bytes lock-free (`__atomic_always_lock_free(16, 0)` is false, and
  `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16` is undefined), with the native layouts. `long double` is
  `double` on every MSIL triple (natively it is wider on Linux and x86_64 MinGW; Status.md
  section 3). `msil-ld` takes the P/Invoke library names from the libraries it links
  (`libc.so.6`, `api-ms-win-crt-stdio-l1-1-0.dll`, ...), and `-S` from the triple (`libc` and
  `libm.so.6` on glibc, `ucrtbase` on windows-msvc): either way, build for the OS you run on.
- Every optimization level works (`-O0` to `-O3`, `-Os`, `-Oz`); the execution tests run at
  `-O0` and `-O2` in most legs, and the `more-levels` leg runs most of them at the other levels
  (C programs at all four, IR tests at `-O1` and `-O3`; section 7).
- `-ffreestanding` keeps the optimizer from introducing libc calls that `-S` cannot import
  (`msil-ld` binds those that LTO introduces, such as `puts` for `printf("...\n")`, natively).
  It also turns off `main`'s implicit `return 0` on Linux triples, so return explicitly.
- WebAssembly's import attributes import a function from a native library you name (milestone
  M13): `int beep(int) __attribute__((import_module("mylib"), import_name("real_beep")));`
  becomes `pinvokeimpl("mylib" as "real_beep" nomangle cdecl)` (`nomangle` since M18: the
  runtime binds exactly that name); without `import_name` the entry point is the C name.
  `import_name` alone makes no other function an import in `-S` and `llc` output: the function
  is still `not a Phase-1 import`. `msil-ld` keeps an `import_module` as given, unchecked, and
  looks the entry point of an `import_name` alone up in the native libraries of the link
  (below). On a Phase-1 import (`puts`, `putchar`, the C math functions) `import_name` alone
  renames the entry point imported from that function's Phase-1 library: `double cbrt(double)
  __attribute__((import_name("my_cbrt")));` becomes `pinvokeimpl("libm.so.6" as "my_cbrt"
  nomangle cdecl)` on glibc. An attribute written on a definition is ignored, with a warning
  when clang generates IR (`-S`, `-emit-llvm`; not with `-fsyntax-only`); one that a definition
  inherits from an earlier declaration is dropped without a diagnostic, as on WebAssembly
  (Status.md section 2).
- A linked program's assembly is named after the stem of `-o` (`fib`; below, "Programs of
  several files"), and `-S` text after the source file's stem, or
  `-mllvm -msil-assembly-name=<name>`. `-mllvm` passes backend options as it does for native
  targets (llc's `-print-after=`, `-debug-pass=Structure`, `-verify-machineinstrs`, ...;
  section 8).
- `-c` writes a CIL object (M19; below, "CIL objects"), and `-flto -c` LLVM bitcode for
  `msil-ld` (below); without `-S` or `-c`, clang compiles each source to a CIL object (to
  bitcode with `-flto`) and links through `msil-ld` (M20; in M14-M19 it compiled to bitcode).
  `-S` writes the same IL as before M14. Its cc1 line now has
  `-mrelocation-model static` instead of PIC and no `-funwind-tables=2`, so the IR of `-S
  -emit-llvm` lacks the `PIC Level` and `PIE Level` module flags (and `uwtable` where the
  native toolchain asked for unwind tables).
  In M14-M18 `-c` wrote bitcode as `-flto -c` does, and `-fno-lto -c` stopped with `CIL
  object files are not supported for MSIL target '...' yet; use -flto to write LLVM bitcode
  objects, or -S to write ILAsm text`; before M14 every `-c` and every link stopped there
  (before M13 with `emitting object files is not yet supported for MSIL target '...'`). ILAsm
  text is not an assembler input: `-x assembler`, `.s` and `.S` files, `-save-temps` with
  `-c` or, since M20, on a command that compiles and links without `-flto` (which would
  assemble the saved text), `-via-file-asm` and clang-cl's `/FA` and `/Fa` (which ask for an
  assemble step; an explicit `-flto` ignores the last three, as upstream LTO does) stop with
  `assembling ILAsm text is not supported for MSIL target '...'; compile C or LLVM IR sources
  instead, or use -S to write ILAsm text`, and `-fno-integrated-as` with `-c` or on such a
  command (also with `-x assembler` or a `.s` file) stops with `there is no external assembler
  that can be used on this platform`.
- `llc` works on IR as usual: `clang --target=<triple> -O2 -ffreestanding -S -emit-llvm fib.c -o
  fib.ll`, then `llc fib.ll -o fib.il` for text or `llc -filetype=obj fib.ll -o fib.o` for a CIL
  object (llc takes the triple from the module, or `-mtriple=`).

### One file to ILAsm text: `-S` and ilasm (the oracle route)

`clang -S` writes the IL of one C file as ILAsm text, a runnable module that `ilasm` assembles
(section 4). It is Phase 1's route and, since M23, the tests' oracle: the `asm-oracle` leg
builds every test of `msil/test/M1` this way (`modern-ilasm` and `wsl-ilasm` the programs of
`11-programs`, with the other two ilasms), and the equivalence oracle O3 compares each one-file image of `msil-ld` with ilasm's image of the same
file's text (section 7). With `-S` the only library functions a program may call are the
standalone imports `puts`, `putchar` and the C math library (Status.md lists them per triple),
frozen since Phase 2 (D14), and functions declared with `import_module` (optionally with
`import_name` for the entry point; above):

```sh
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S fib.c -o fib.il   # Linux (glibc)
clang --target=x86_64-pc-windows-msvc-msil   -O2 -ffreestanding -S fib.c -o fib.il   # Windows (UCRT)
ilasm -nologo -quiet -exe -pe64 -x64 -output=fib.exe fib.il                         # section 4
```

The output is one ILAsm module: an `.assembly extern mscorlib` reference, the assembly, one
`.method` per C function, P/Invoke declarations for the imports, the data segment and module
initializer (`<Module>::.cctor`) for global variables, and a generated `'<msil>Main'` entry point
that calls `main` (with `argc`/`argv` when `main` takes them). Instructions take their shortest
forms (M18, plan Q26): `ldc.i4.m1`, `ldc.i4.<n>` and `ldc.i4.s <n>` for small constants, and
`br.s`, `brtrue.s`, `blt.s`, ... for every branch whose target is in reach; each P/Invoke is
`pinvokeimpl("<library>" nomangle cdecl)`.

### Programs of several files: `clang -c`, archives and `msil-ld`

`clang -c` writes a CIL object on every MSIL triple (M19; below, "CIL objects"), `llvm-ar`
collects objects into archives, and a link through clang runs `msil-ld`, which links them and,
since milestone M20, writes the program's assembly image itself: an `.exe` that runs on modern
.NET and on .NET Framework 4.8 alike, with no ilasm:

```sh
# main.c calls fib (fib.c), bump (bump.c) and puts; nothing calls unused.c.
clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -c main.c fib.c bump.c unused.c
llvm-ar rcs libutil.a bump.o unused.o        # the tree's build/bin/llvm-ar
clang --target=x86_64-pc-windows-msvc-msil -O2 main.o fib.o -L. -lutil -o prog.exe
dotnet prog.exe       # with prog.runtimeconfig.json next to it (section 5)
prog.exe              # .NET Framework 4.8 (Windows triples)
```

`clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding main.c fib.c bump.c -o
prog.exe` compiles and links in one command. With `-flto`, `msil-ld` links LLVM bitcode with
LTO (M14) and still writes the image; `-Wl,--lto-emit-asm` instead writes the ILAsm text of the
whole program, which `ilasm -exe` assembles, as every link did in M14-M19:

```sh
clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -flto -c main.c fib.c bump.c unused.c
llvm-ar rcs libutil.a bump.o unused.o
clang --target=x86_64-pc-windows-msvc-msil -O2 -flto main.o fib.o -L. -lutil -o prog.exe
clang --target=x86_64-pc-windows-msvc-msil -O2 -flto main.o fib.o -L. -lutil \
  -Wl,--lto-emit-asm -o prog.il
ilasm -nologo -quiet -exe -pe64 -x64 -output=prog.exe prog.il
```

- **Compiling.** `-c` runs one `cc1 -emit-obj` job per source and writes a CIL object to
  `<name>.o` (clang-cl `/c`: `<name>.obj`). `-flto -c` runs `cc1 -emit-llvm-bc -flto=full`
  and writes bitcode (`-emit-llvm -c` too), and `-flto=thin` ThinLTO bitcode, which `msil-ld`
  links since M20. CIL objects and bitcode mix in one link and in one archive. In M14-M19
  `msil-ld` linked only bitcode (a CIL object was `<file>: linking CIL object files and .NET
  assemblies is not supported yet`), so a program of several files was compiled with `-flto
  -c`, and a command that compiles and links compiled to bitcode without `-flto` (plan Q6).
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
  archive with CIL or bitcode members wins over a native library, and `-l:<file>` takes that
  exact file (the whole rule is under "Native libraries and P/Invokes" below).
- **Linking.** Every MSIL link runs `msil-ld`, the copy of `lld` next to clang
  (`build/bin/msil-ld`), never `ld`, `link.exe` or `lld-link`. `-fuse-ld=lld`, `-fuse-ld=ld` and
  `-fuse-ld=msil-ld` name it too, `--ld-path=<program>` and an absolute `-fuse-ld=<path>` run
  another one, and any other `-fuse-ld` value is `invalid linker name`. Without `-o` the output
  is `a.exe` on every MSIL triple. An MSIL triple that is not a Linux, windows-msvc or
  windows-gnu one (Darwin, FreeBSD, Cygwin, ...) ends with `unknown target triple '<triple>'`,
  also when the command only links, and never runs a native linker. `clang -###` shows the
  command: `msil-ld --target=<triple> [--sysroot=<dir>] -L<dir>... [--lto-O<n>] <objects,
  archives, -l and -Wl options in command-line order> <default libraries> -o <output>`
  (`--lto-O<n>` and the `--mllvm=` options only when the link has `-flto`; `-shared`, and
  clang-cl's `/LD` and `/LDd`, add `--shared`, which links a library since M22 (below,
  "Libraries"; before M22 `msil-ld` rejected it); `-s` adds `--strip-all`, which it ignores).
- **The output.** `msil-ld` writes an IL-only PE32+ executable for the triple's machine, whose
  entry point `'<msil>Main'` calls `main` (Status.md section 2, "CIL object links and the
  image"). It is an EXE image whatever the name (a name that ends in neither `.exe` nor `.dll`
  gets the warning `<path>: the output name ends in neither .exe nor .dll; msil-ld writes an
  EXE image`), and without reference assemblies it references only `mscorlib`, so the one file
  runs under `dotnet exec` (with a runtimeconfig, section 5) and directly on .NET Framework
  4.8; with them it references what they define (M21; below, "Reference assemblies"), and
  since M23 a `.exe` name for an image bound to .NET's core library warns, because only .NET can
  start it (section 5, "Executable names"). `-Wl,--lto-emit-asm` writes the
  ILAsm text of an LTO link to `-o` instead; it takes bitcode only (`<file>: --lto-emit-asm
  cannot link CIL objects; link without --lto-emit-asm`), one LTO partition and no ThinLTO
  (`ThinLTO and --lto-partitions cannot write ILAsm text; link without --lto-emit-asm`). In
  M14-M19 every link needed it, and any other was `msil-ld: error: ThinLTO, --lto-partitions
  and LTO without --lto-emit-asm need CIL object output, which this msil-ld does not have; use
  --lto-emit-asm (clang: -Wl,--lto-emit-asm) to write ILAsm text`. clang never adds
  `--lto-emit-asm` and never runs ilasm itself.
- **Optimization.** CIL objects are optimized when they are compiled; the link does not
  optimize them again. With `-flto` on the link line, the link's `-O` is the LTO level (`-O0`
  gives `--lto-O0`, `-O1` and `-Og` `--lto-O1`, `-O2`, `-Os` and `-Oz` `--lto-O2`, `-O3` and
  `-Ofast` `--lto-O3`; without `-O` `msil-ld` uses `--lto-O2`), so give the link `-flto` and the
  level of the compiles; without `-flto` the driver passes no LTO options, and bitcode inputs
  are optimized at `--lto-O2`. Only `main`, the `-u` symbols and the symbols that CIL objects
  of the link reference stay visible: every other function and variable of the bitcode becomes
  internal to the program (functions are `assembly` methods), so inlining and the other
  interprocedural optimizations work across files. On the link line, `-mllvm <option>` reaches
  the code generator inside `msil-ld` (as `--mllvm=<option>`, with `-flto`), and `-save-temps`
  keeps LTO's bitcode after each stage (`<output>.0.0.preopt.bc`, `.0.2.internalize.bc`,
  `.0.4.opt.bc`, `.0.5.precodegen.bc`), its symbol resolutions (`<output>.resolution.txt`) and,
  since M20, the CIL objects that LTO writes (`<output>.lto.o`, `<output>.lto.<n>.o` for further
  `--lto-partitions`, `<output>.lto.<stem>.o` next to each ThinLTO input). Under
  `--lto-emit-asm`, `.0.5.precodegen.bc` is written after `msil-ld` has checked the native
  bindings (below) but before it annotates the module with them, so it has no `msil-linked`
  flag, and it is not written when that check reports an error.
- **The assembly name** is the stem of `-o` (`prog`; never LTO's `ld-temp`), or
  `-Wl,--assembly-name=<name>` (the same as `-Wl,-soname,<name>`); with `--lto-emit-asm`,
  `-mllvm -msil-assembly-name=<name>` on the link line overrides both. The image's Module row is
  named after the output file (`prog.exe`). `msil-ld` hands the name to the backend in the
  module flag `msil-assembly-name` (D28 as amended for M14).
- **Symbols** resolve as in a static link by lld (plan 5.9.5, a port of lld/ELF): a strong
  definition beats a weak one and a common symbol, a common symbol beats a weak definition,
  common symbols of one name become one variable of the largest size and alignment, an alias
  resolves as a definition of its kind, a COMDAT group (an inline function, an MSVC string
  literal, `__declspec(selectany)` data) keeps the first input's copy, and a weak undefined
  reference stays null and extracts no archive member. Two strong definitions are `duplicate
  symbol: <name>`, followed by one `>>> defined in <file>` line per definition
  (`-Wl,--allow-multiple-definition` keeps the first); a function and a variable of one name
  are `symbol type mismatch: <name>`; a thread-local and a plain variable of one name are `TLS
  attribute mismatch: <name>`. `-Wl,--warn-common` warns about common symbols. Since M20 a call
  whose signature differs from its definition's (a K&R call with too many or too few
  arguments, an `int` passed where the definition takes a `long long` or a pointer) goes
  through an adapter and warns, and integer against floating point is an error (below).
- **Undefined symbols.** A function that no input defines binds to a native library of the
  link (below). A symbol that binds nowhere is `msil-ld: error: undefined symbol: <name>`,
  followed by `>>> referenced by <file>:(<function>)` lines. Only the references that live code
  makes count: in a link of CIL objects those that `--gc-sections` keeps, and for bitcode those
  that the program still makes after LTO (a name that only the optimizer introduced is `>>>
  referenced by ld-temp.o`). A hidden reference is `undefined hidden symbol: <name>`. (In M14,
  before native libraries, such a function could bind only through the Phase-1 import table of
  `-S`.)
- **Default libraries.** Unless `-nostdlib` or `-nodefaultlibs` is given, clang adds the C
  library of the triple (plan 5.10.4), as a native link does, and since M25 the triple's helper
  archive `clang_rt.builtins` after it, when the file exists (below, "The helper archive").
  `-nostartfiles` is accepted silently, because MSIL has no crt files (as are `-static-libgcc`
  on the Linux and MinGW triples and `-shared-libgcc` on the Linux ones; no libgcc is linked):

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
  directory. `-nodefaultlibs` leaves the libraries and the helper archive out, for a program
  that calls no native function and no runtime helper (the test harness linked that way in M14);
  `-nolibc` (Linux and MinGW triples) drops only the C library and keeps the archive. Header
  search is the native triple's too (Phase 2 needs no hosted headers, plan Q16): `--sysroot`, a
  developer prompt's `INCLUDE`, `-Xmicrosoft-windows-sys-root` or clang-cl's `/winsysroot`; the
  freestanding programs of this guide need none.
- **Stock-clang bitcode** (plan Q12). Bitcode that clang writes for the native triple
  (`clang --target=x86_64-pc-windows-msvc -flto -c`) links too: `msil-ld` compiles it for the
  MSIL triple with the rest and warns once per link, `<file>: linking bitcode for <native
  triple> as <msil triple>; stock-clang bitcode is a compatibility path`. Its limits (recon
  `p2/lld-flavor.md` 3.3): on windows-msvc compile it with `-fno-ms-volatile`, or its
  `volatile` accesses are atomics and the link stops with `undefined symbol: __atomic_load_4`
  and the line `>>> an atomic operation (libatomic's interface)` (since M24; with
  `--lto-emit-asm` the error `runtime helper '__atomic_load_4' is an atomic operation that only
  msil-ld links`, before M25 `... is not available for MSIL yet (phase 3)`; atomics come in
  M30); in stock bitcode for x86_64 Linux and MinGW `long double` is `x86_fp80`, which the
  backend rejects where code uses it, and a struct that contains one has another layout than in
  MSIL files, without a diagnostic; the file sees the native predefines (no `__MSIL__`, and with
  `cx16` or on aarch64 16-byte atomics that are lock-free, unlike MSIL files); and the native
  cost model vectorized it, which makes its IL larger (by about a quarter in the recon's
  corpus). Every input must have the link's architecture, OS and environment (the vendor and the
  MSVC version do not count), or it is `<file>: target triple <t> is incompatible with <t0>`,
  and the same data model as the CIL objects (`<file>: data model 8,8,2,8 differs from 8,8,4,8
  in <f0>` for bitcode compiled with `-fshort-wchar`, found when `msil-ld` reads the object that
  LTO writes); the link's triple is clang's `--target`, else the first input's with `-msil`
  appended to a native triple. That triple must be one of D6's (x86_64 or aarch64 on Linux,
  windows-msvc or windows-gnu): any other, such as that of stock bitcode for Darwin
  (`x86_64-apple-macosx`) or i686, stops the link before the checks of each input with `msil-ld:
  error: unsupported target triple '<codegen triple>' for MSIL`.
- **Other inputs.** A .NET assembly is a reference assembly since M21 (below, "Reference
  assemblies"), unless this toolchain built it as a library: then it is a library to link
  against since M22 (below, "Libraries"; in M21 it was `<path>: linking .NET libraries built by
  this toolchain is not supported yet`, in M20 every .NET assembly was `<file>: .NET assembly
  inputs are not supported yet`, and in M14-M19 it and a CIL object were `<file>: linking CIL
  object files and .NET assemblies is not supported yet`). A netmodule that is not a CIL
  object is `<file>: not a CIL object (no link-info record)`. A native object file of the
  triple's object format (COFF on the Windows triples, ELF on the Linux ones) is read like a
  member of a native archive since M22: its definitions are machine code, so it adds nothing
  unless something references one of them, which is an error (below, "Rust bitcode"); before
  M22 it was `unknown file type: <file>`. Anything else (an `.il` file, a native object of another format)
  is `unknown file type: <file>`, since M22 followed by the format when `msil-ld` recognizes
  it (`(WebAssembly object)`). Native libraries and linker scripts are inputs like any other
  since M15 (below).

**Calls whose signatures differ** (plan Q8). `msil-ld` compares every call that it binds by name
with the definition (Status.md section 2 has the table). A K&R call with an extra or a missing
argument, or an `int` passed where the definition takes a `long long` or a pointer, gets an
adapter method that drops, zero-fills or converts the arguments and the result, and a warning;
a mismatch that native code cannot survive either (integer against floating point, `float`
against `double`, an `sret` result on one side) is an error:

```text
msil-ld: warning: function signature mismatch: add
>>> defined as int32(int32, int32) in callee.o
>>> called as int32(int32, int32, int32) in caller.o
>>> adapter: drops 1 extra argument
msil-ld: error: function signature mismatch: half
>>> defined as int32(int32) in fcallee.o
>>> called as float32(float32) in fcaller.o
>>> argument 1 is float32 at the call but int32 in the definition (different native register classes)
```

The shapes are IL value classes (`int32`, `native int`, `float32`, `float64`). LTO gives the
same warnings and errors without the ` in <file>` parts for a mismatch across files, at every
optimization level: `msil-ld`'s LTO applies the table before the optimizer, which would
otherwise rewrite some mismatched calls, and again in code generation (Status.md section 2,
"The link-semantics matrix"). Inside one file the compile step of `-flto` at `-O1` and above
may already fold a mismatched call, which then links without a diagnostic, as a call inside
one object does on the object path; a file compiled at `-O0`, and a mismatch that the
optimizer cannot fold (an `int` argument for a `long long` or pointer-sized parameter), get
the warning or error once at every level (`msil/test/M2/02-lto/intra-tu-mismatch.test`).
Declare functions with prototypes.

**Sections and `--gc-sections`.** Variables in a named section (`__attribute__((section("x")))`,
`__declspec(allocate("x"))`) are grouped in the image as a native link groups them, on the Linux
triples `__start_x` and `__stop_x` give the section's bounds when `x` is a C identifier, and a
function pointer in `.init_array` (Linux) or `.CRT$XCU` (Windows) runs as a constructor
(`clang -S` text does the same since M20). `--gc-sections`, the default, drops every function
and variable that nothing live reaches. What stays is what a native `--gc-sections` or
`/OPT:REF` link keeps: `used` and `retain` variables, constructors and constructor tables,
destructor tables and the other sections that `msil-ld` reports as unsupported
(`.preinit_array` among them; Status.md section 3), the sections that `__start_`/`__stop_`
name, every variable of a named section of a file in which a live variable
lies in the same section (as ld.lld keeps a whole input section), and, on the Windows triples,
every variable of a named section that is not a COMDAT (as lld-link does). So a table between
two markers, whose entries only pointer arithmetic reaches, keeps them. Under `-flto` LTO drops
the unreferenced entries before `msil-ld` sees them, as native LTO does: mark them
`__attribute__((used))` there. `-Wl,--print-gc-sections` lists what it drops, here
`fib_unused`, a function of `fib.c` that nothing calls:

```text
removing unused section fib.o:(fib_unused)
```

`-Wl,--no-gc-sections` keeps everything.

`msil-ld`'s options (M20), with lld's spellings (pass them through clang as `-Wl,<option>`;
plan 5.9.3):

| Option | Meaning |
|---|---|
| `-o <file>` | the output: an EXE image (with `-shared` a DLL image); with `--lto-emit-asm` ILAsm text, which ilasm assembles with `-exe` |
| `--target=<triple>` | the code-generation triple (clang always passes it); one of D6's MSIL triples, else `unsupported target triple '<triple>' for MSIL`. Without it the triple of the first CIL object or bitcode file; a link of libraries alone (M22) needs it: `no target triple: pass --target or a CIL object or bitcode file` |
| `-L <dir>`, `-l <name>`, `-l:<file>`, `--sysroot=<dir>` | library search as above and under "Native libraries and P/Invokes" below; `-L=<dir>` is relative to `--sysroot` |
| `-Bstatic`, `-Bdynamic` | after `-Bstatic`, `-l` takes only static libraries (archives with CIL or bitcode members) and skips native ones; `-Bdynamic`, the default, takes both again (M15) |
| `--ucrtbase-imports` | a built-in import library, read where the option stands, that binds `memcpy`, `memset`, `memmove`, `memcmp`, `memchr`, `strchr`, `strrchr`, `strstr`, `wcschr`, `wcsrchr` and `wcsstr` to `ucrtbase.dll`; Windows targets only (M15) |
| `--print-imports` | print each native binding, functions and data, and the version floor of each ELF library (M15; below) |
| `--reference=<path>`, `--reference <path>` | bind the .NET base class library against this reference assembly, or against every `*.dll` assembly of this directory (not recursively); repeatable, no short form (M21; below, "Reference assemblies") |
| `--print-bcl-table` | print the built-in `[mscorlib]` table that links without references bind through, one entry per line, and exit (M21) |
| `--no-dependent-libraries`, `--dependent-libraries` | ignore, or read (the default), the linker options of the inputs: `#pragma comment(lib)`, `/DEFAULTLIB`, ... (M15) |
| `--whole-archive`, `--no-whole-archive`, `--start-lib`, `--end-lib` | as in lld |
| `-u <sym>`, `--undefined=<sym>` | a reference to `<sym>`: it extracts the archive member that defines it, keeps it through `--gc-sections` and stays visible after LTO |
| `-e <sym>`, `--entry=<sym>` | the function that `'<msil>Main'` calls (default `main`): any function of a D27 shape, `int f(void)` or `int f(int, char **)`, since M20 (a `short`, `char` or `_Bool` result counts as `int`, and a parameter list of an `int` and a pointer-sized integer builds `argv` too); another signature is `unsupported signature for <sym>`. With `--lto-emit-asm` too: `msil-ld` names an entry other than `main` to code generation with the module flag `msil-entry`, the text's `'<msil>Main'` calls it, and another signature is the same `unsupported signature for <sym>`; in M14-M19 only `main`. With `-shared` it is ignored with the warning `--entry is ignored with -shared` (M22) |
| `--gc-sections`, `--no-gc-sections` | drop the functions and variables that nothing live reaches (the default), or keep them all (M20) |
| `--print-gc-sections` | print `removing unused section <file>:(<name>)` for each function and variable that `--gc-sections` drops (M20) |
| `--lto-emit-asm` | write the ILAsm text of an LTO link to `-o` instead of the image (bitcode inputs only, one partition, no ThinLTO) |
| `--lto-O<n>`, `--lto-CGO<n>`, `--lto-partitions=<n>`, `--thinlto-jobs=<n>`, `--thinlto-cache-dir=<dir>`, `--save-temps`, `--disable-verify`, `-mllvm <option>`, `--mllvm=<option>` | LTO, as in lld; more than one partition and ThinLTO work since M20 |
| `--assembly-name=<name>`, `-soname <name>` | the assembly name (default: the stem of `-o`); the CLR finds a library only by a file named after its assembly, so with `-shared` a name that differs from the output's stem is warned about (M22) |
| `--assembly-version=<a>[.<b>[.<c>[.<d>]]]` | the version of the Assembly row, one to four numbers 0-65535 (default 0.0.0.0), for every output (M22) |
| `-Map=<file>`, `--print-map`, `-M` | the link map: the inputs in load order, native libraries included, then each defined symbol with its kind, binding and input, and each natively bound one (`NativeFunction`, `NativeData`) with its library; for an image (M20) also its layout, methods, fields and P/Invokes (below) |
| `--why-extract=<file>` | why each archive member was linked, in lld's format (`-` is standard output) |
| `--reproduce=<file>` | a tar of every input with a `response.txt` that links them again, as lld writes it |
| `--trace`, `--trace-symbol=<sym>`, `-y <sym>` | print each input file and extracted archive member, and each reference to and definition of `<sym>`, a native one as `<library>: shared definition of <sym>` and a library export as `<library>: managed definition of <sym>` (M22) |
| `--allow-multiple-definition`, `--warn-common`, `--fatal-warnings`, `--error-limit=<n>`, `--verbose`, `--color-diagnostics` | as in lld; `--verbose` also notes each skipped native archive and native object, each ignored C runtime library request and each interface library |
| `-s`, `--strip-all` | accepted and ignored |
| `-shared`, `--shared` | write a library assembly, a DLL image without an entry point (M22; below, "Libraries"); in M14-M21 `-shared is not supported yet` |
| `--export-class=<Ns.Name>` | the class whose public static methods are the exports of a `-shared` library (default: the assembly name, whose last `.` separates the namespace) (M22) |
| `--export-all-symbols` | Windows triples: export every default-visibility definition of a `-shared` link, also when some input has `dllexport` definitions (M22) |
| `--exclude-libs=<archive>[,<archive>...]`, `--exclude-libs=ALL` | export nothing that the members of these archives (file names such as `libutil.a`, separated by `,` or `:`), or of every archive, define (M22, as lld/ELF) |
| `--interface-only` | with `-shared`: write the library's interface, whose methods are stubs, allowing undefined symbols; for cycles (M22) |
| `-r`, `--relocatable` | always `-r is not supported` |
| `--version`, `-v`, `--help` | lld's version, and this list |

Three hidden options serve the tests: `--msil-reloc-loop-threshold=<n>` (apply more than `<n>`
data relocations in the relocation loop of `<Module>::.cctor`, default 32, in the image and,
as a module flag, in the `--lto-emit-asm` text, where llc's `-msil-reloc-loop-threshold`
given through `-mllvm` takes precedence; `-S` uses llc's option),
`--msil-type-member-limit=<n>` (move the members past `<n>` methods or fields of `<Module>`
into spill classes, when `<n>` is below the real limits) and, since M21, the repeatable
`--print-bcl-binding=<request>` (print how the references bind `type:<Ns>.<Name>[/<Nested>]`,
`member:<BCLMemberId>[:drop|:substitute]` or `typename:<name>`, or, for
`attr:(<params>):<hex>`, rewrite the `System.Type` values of the custom-attribute blob
`<hex>` for a constructor `instance void .ctor(<params>)`, one `<request> -> <result>` line
each, for `attr:` the rewritten blob in uppercase hex, and exit; below). Every other option
is `unknown argument: <option>`. The map of the first example
above (`-Wl,-Map=prog.map`,
from a developer prompt; the library paths are shortened), in which `main` prints `fib`
through `puts`, increments `counter` with `bump` and returns `fib(10) + counter`, 57:

```text
msil-ld map of prog.exe (x86_64-pc-windows-msvc-msil)
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
  DefinedData     Global  main.o                            counter
  DefinedData     Weak    main.o                            ??_C@_03NADLHBLG@fib?$AA@
  DefinedFunction Global  main.o                            main
  DefinedFunction Global  fib.o                             fib
  DefinedFunction Global  fib.o                             fib_unused
  DefinedFunction Global  .\libutil.a(bump.o)               bump
  NativeFunction  Global  <SDK>\lib\<ver>\ucrt\x64\ucrt.lib puts
Image: 12 bytes (8 initialized), align 16
  Offset     Size Align Section / input / symbol
       0        4     4 .data
       0        4     4   main.o:segment 0
       0        4     4     counter
       4        4     4 .rodata
       4        4     4   main.o:segment 1
       4        4     4     ??_C@_03NADLHBLG@fib?$AA@
       8        4     4 .bss
       8        4     4   .\libutil.a(bump.o):segment 0
       8        4     4     calls
Methods:
  Token      Owner      Input               Symbol
  0x06000001 <Module>   main.o              main
  0x06000002 <Module>   fib.o               fib
  0x06000003 <Module>   .\libutil.a(bump.o) bump
  0x06000005 <Module>   <internal>          .cctor
  0x06000006 <Module>   <internal>          <msil>Main
Fields:
  Token      Owner      Offset   Symbol
  0x04000001 <Module>        0   counter
  0x04000002 <Module>        4   ??_C@_03NADLHBLG@fib?$AA@
  0x04000003 <Module>        8   calls
  0x04000004 <Module>        -   <msil>data
  0x04000005 <Module>        -   <msil>image
Imports:
  Token      Module!Entry                              Symbol
  0x06000004 api-ms-win-crt-stdio-l1-1-0.dll!puts      puts
```

The default libraries are inputs even when nothing binds to them; `??_C@...` is the MSVC
string literal `"fib"`, a COMDAT, and `calls` is `bump.c`'s `static` counter. `Symbols:` lists
`fib_unused`, which `--gc-sections` dropped from the image. `Image:` gives each output section,
the segments of its inputs and their variables, with offsets in the data image; `Methods:` and
`Fields:` give each row's token and owner type (a spill class in a very large program), and a
field's offset in the image (`-` for the fields that `.cctor` itself uses); `Imports:` gives
each P/Invoke's token, module and entry. `llvm-cil-dump --headers prog.exe` shows the image
(`Format: PE32+ machine=0x8664 exe`, `entrypoint=0x06000006`), and `llvm-cil-dump prog.exe`
its canonical listing (section 6).

### Native libraries and P/Invokes (M15)

A function that no input of the link defines becomes a P/Invoke of the native library that
defines it, found the way a native link of the same program finds it: the driver's default
libraries, the `-l` options and library files on the command line, and the libraries that the
program names with `#pragma comment(lib)`. `-Wl,--print-imports` shows each binding:

```sh
# Windows triple, from a Visual Studio x64 developer prompt (LIB):
clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -c main.c
clang --target=x86_64-pc-windows-msvc-msil -O2 main.o -Wl,--print-imports -o prog.exe
# Linux triple, on Windows or Linux: a sysroot with the C library (section 7).
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -c main.c
clang --target=x86_64-unknown-linux-gnu-msil -O2 main.o \
  --sysroot=C:/llvm-msil-scratch/linux-sysroot -Wl,--print-imports -o prog.exe
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

and the image has one P/Invoke stub per function, which `llvm-cil-dump prog.exe` lists, for
`strdup` on Windows, as `.method 'strdup' native int(native int) flags=0x2093 impl=0x0080` with
`.pinvokeimpl 'api-ms-win-crt-string-l1-1-0.dll' entry '_strdup' flags=0x0201`; the
`--lto-emit-asm` text of an LTO link declares the same stub as

```text
.method assembly hidebysig static pinvokeimpl("api-ms-win-crt-string-l1-1-0.dll" as "_strdup" nomangle cdecl) native int 'strdup'(native int) cil managed preservesig {}
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
  `lib<name>.a`, `<name>.dll`, `lib<name>.dll` and `lib<name>.so` (the two DLL names count
  there only as .NET libraries of this toolchain, so `-lfoo` takes `libfoo.so` over a native
  `foo.dll`, as ld.lld does). The first directory that has an acceptable one wins, and in it
  an archive with CIL or bitcode members wins, then (since M22) a library assembly that this
  toolchain built (below, "Libraries"), then an import library or a shared library (Design.md
  5.2: static first, the opposite of GNU ld's default). An archive of native objects only is
  skipped (`-Wl,--verbose` notes `skipping native static archive <path> (no CIL or bitcode
  members)`), and so is a library of the other object format (`skipping <path>: ...`) and a
  .NET assembly without this toolchain's symbol table, with a warning (`<path>: skipping the
  .NET assembly that -l<name> found; use --reference for .NET reference assemblies`).
  `-Wl,-Bstatic` makes the following `-l` options take static libraries only, until
  `-Wl,-Bdynamic`; `-l:<file>` takes that exact file of any kind (one of the other object
  format is the error above).
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
  `/DEFAULTLIB:<name>.lib` on Windows triples and `-l<name>` on Linux ones) and the other linker
  options in a CIL object's record or in bitcode are read when the file joins the link, an
  archive member's when it is extracted. Requests for the MSVC C runtime (`libcmt`, which clang
  names in every windows-msvc object, `msvcrt`, `ucrt`, `vcruntime`, ...) are ignored with a
  `--verbose` note, because the default libraries supply the C runtime; `/alternatename:` and
  `/failifmismatch:` work as in lld-link; any other option is ignored with the warning `ignoring
  linker option '<option>' in <file>`. `-Wl,--no-dependent-libraries` ignores them all.
- **Checked at link time.** `msil-ld` binds what live code calls (in a link of CIL objects
  what `--gc-sections` keeps, for bitcode what the program still calls after LTO), and reports
  there: an unbindable or unresolved function is a link error, with `>>> referenced by
  <file>:(<function>)` lines unless the code generator reports it (section 8 lists the texts),
  never a `DllNotFoundException` or `EntryPointNotFoundException` at run time. A call that the
  optimizer removed is not reported, and a library call that it introduced (`puts` for a
  `printf("...\n")`) is bound like any other.
- **Native variables** (M20, plan 5.10.12). A variable that the program declares `extern` and
  a native library defines as data (glibc's `environ`, `optind` and `stderr`, `msvcrt.dll`'s
  `_iob`, a variable of your own library, or an `extern` variable with the import attributes)
  is imported: its address field is filled when the program starts, before its data
  relocations and constructors run, through `LoadLibraryW` and `GetProcAddress` of
  `kernel32.dll` on the Windows triples and through `dlopen` and `dlsym`, which the link binds
  from the C library (glibc 2.34 and later), on the Linux ones. For a `main.c` that reads
  `environ` and `optind`, the Linux link above prints

  ```text
  dlopen -> libc.so.6!dlopen [function, GLIBC_2.34]
  dlsym -> libc.so.6!dlsym [function, GLIBC_2.34]
  environ -> libc.so.6!environ [data, GLIBC_2.2.5]
  optind -> libc.so.6!optind [data, GLIBC_2.2.5]
  puts -> libc.so.6!puts [function, GLIBC_2.2.5]
  libc.so.6: version floor GLIBC_2.34
  ```

  A library that is missing at run time, or that lacks the name, stops the program in its
  module initializer with `System.EntryPointNotFoundException: data import 'environ' not found
  in libc.so.6` (inside a `TypeInitializationException`). The loader looks the library up by
  its name alone: `dlopen` of a library of your own (`libnative.so`) does not search the
  program's directory, so put it on the loader's path (`LD_LIBRARY_PATH`); see Status.md
  section 3, "Limits". Without `dlopen` and `dlsym` in the link (`-nodefaultlibs` on Linux) the
  import is the link error `data symbol '<name>' in <module> cannot be imported: no reference
  defines System.Runtime.InteropServices.NativeLibrary and the native libraries define no
  dlopen/dlsym` (under `--lto-emit-asm` with reference assemblies, which that text never
  binds, `... cannot be imported: --lto-emit-asm text is never bound to the references, and
  the native libraries define no dlopen/dlsym`; "Reference assemblies" below). In M15-M19
  every native variable was `importing native data symbol '<name>' (<module>) is not supported
  yet`.
- **What does not work yet** (Status.md section 2, "Native libraries and P/Invokes"; variadic
  native functions such as `printf` bind since M26, below): native functions that take or
  return a struct by value, an
  `__int128`, a vector or a `_Float16`, `atexit`, and the native functions that take C function
  pointers (`qsort`, `bsearch`, `signal`, `pthread_create`, `CreateThread`, `_beginthreadex`,
  ...): all of them are link errors until phase 3. On x86_64 and aarch64 Linux and x86_64 MinGW
  the native `long double` functions without a `double` counterpart (`fromfpl` and its three
  kin, glibc's `_Float64x` and `_Float128` functions) are a permanent link error; since M25 the
  helper archive forwards the others (`sqrtl`, `strtold`, ...) to their `double` counterparts
  (below, "The helper archive"). A function address passed
  to any other native function (a library's own callback API, a Win32 callback) is not diagnosed
  and fails at run time until phase 3 (P3-B). For a call that passes no function of the program,
  such as `signal(SIGPIPE, SIG_IGN)`, declare the function with an explicit `import_module`,
  which `msil-ld` keeps as given.
- **Running.** The P/Invoke names only the library, so the library must be where the runtime's
  loader finds it: next to the program for a library of your own (.NET and .NET Framework load
  it from there; the test harness copies it there with `msil-run.py --dep`), the system's
  for the C library and the Win32 API. A P/Invoke has no symbol version: on Linux it binds the
  default version of the library that runs it, whatever version `msil-ld` saw (the version
  floor of `--print-imports` is the newest version the link bound in each library).
- **The linked text.** Under `--lto-emit-asm`, `msil-ld` annotates the module it compiles
  with the module flag `msil-linked`, the attributes `msil-import-module` and
  `msil-import-name` on each bound function and the named metadata `!msil.imports` for the
  library functions that code generation may call and, since M20, for native variables and the
  data-import helper, and the backend prints P/Invokes from these alone, with `assembly` access
  (plan 5.10.13). The stubs of `msil-ld`'s image are `assembly` too. `clang -S` and llc keep the
  standalone table and `public` P/Invokes.

### The helper archive: compiler-rt's builtins (M25)

As on a native target, some C operations compile to calls of compiler-runtime helpers:
`_Float16` arithmetic and conversions (`__extendhfsf2`, `__truncsfhf2`), the rounding conversions
to `__bf16` (`__truncsfbf2`, `__truncdfbf2`), C `_Complex` multiplication and division
(`__muldc3`, `__divdc3`, ...), `__builtin_powi` on the Linux triples (`__powidf2`; on
windows-msvc it calls the UCRT's `pow`, as native code does) and compiler-rt's 32- and 64-bit
integer and soft-float helpers. A native link takes them from compiler-rt or libgcc; an MSIL link
takes them from the triple's helper archive `clang_rt.builtins`, compiler-rt's builtins compiled
for the MSIL triple into CIL objects by `build-builtins.py` (section 2, "The helper archives").
clang passes the archive to `msil-ld` after the default libraries when the file exists, and
`msil-ld` extracts only the members that the program calls:

```c
// cmul.c
static double _Complex sq(double _Complex z) { return z * z; } /* calls __muldc3 */

int main(void) {
  volatile double re = 1.0, im = 2.0;
  double _Complex z = sq(__builtin_complex((double)re, (double)im));
  return (int)__imag__ z; /* (1+2i)^2 = -3+4i: exit status 4 */
}
```

```sh
clang --target=x86_64-unknown-linux-gnu-msil -print-libgcc-file-name
# <build>/lib/clang/24/lib/x86_64-unknown-linux-gnu-msil/libclang_rt.builtins.a
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -c cmul.c
clang --target=x86_64-unknown-linux-gnu-msil -O2 --sysroot=<sysroot> cmul.o -o cmul.exe \
  -Wl,--why-extract=-       # prints: cmul.o  <archive>(muldc3.c.o)  __muldc3
```

- **Where it is.** `build-builtins.py` writes, checks and removes only plan 5.14.3 A1's
  per-target file, `<resource dir>/lib/<triple>/clang_rt.builtins.lib` on the windows-msvc
  triples and `<resource dir>/lib/<triple>/libclang_rt.builtins.a` on the others, with the
  resource directory that `clang -print-resource-dir` prints (in the shared build
  `build/lib/clang/24/lib/<triple>/`); `--out <dir>` writes `<dir>/<triple>/<that name>`
  instead. Once that file exists, `-print-libgcc-file-name` names it, as above, which shows the
  file that clang links (compiler-rt is the MSIL toolchain's runtime library, so
  `-rtlib=compiler-rt` changes nothing). While it does not, a resource directory that has the
  old layout's native compiler-rt, from a compiler-rt build with
  `LLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF`, makes clang name that file instead
  (`<resource dir>/lib/<os>/[lib]clang_rt.builtins-<arch>.<ext>`); the script never touches
  such a file, and if clang still names it after a build, the script fails with a usage error
  that names it. The driver passes the file that `-print-libgcc-file-name` names right before
  `-o` when it exists; when it does not, the driver passes nothing, and a program that calls a
  helper fails to link (below). `-nostdlib` and `-nodefaultlibs` drop it with the C library,
  and `-nolibc` keeps it. A link that does not go through clang names the file like any
  archive.
- **What it holds.** compiler-rt's `GENERIC_SOURCES` and `BF16_SOURCES`, less what MSIL cannot
  use: the `*ti*` helpers of `__int128`, which MSIL expands inline, the fp128 and x87 sources,
  the sources that need a C library or an operating system (`emutls.c`, `clear_cache.c`, ...),
  and `atomic.c`, which no helper archive ever holds: the lock path of atomics is the
  platform's process-wide library, `libatomic.so.1` on the Linux triples and, from M30, a
  Windows shim over the MSVC STL's `msvcp140_atomic_wait.dll` (plan 5.14.3 A5 and 5.14.2 N-3;
  `sources.txt` gives the user's decisions R4 and R5 as the reason).
  `python msil/utils/build/build-builtins.py --list` prints every member of each triple;
  Status.md section 2, "The helper archive (M25)", has the details. The members are hidden, so
  each output that calls a helper links its own copy, a `-shared` library exports none, and two
  assemblies never share a helper's state.
- **`long double` functions.** Where the native `long double` is wider than MSIL's `double` (the
  Linux triples and x86_64 MinGW), the archive also forwards the C library's `long double`
  functions that have a `double` counterpart (`sqrtl` to `sqrt`, `strtold` to `strtod`,
  `nexttoward` to `nextafter`, ...), so such calls link and compute in `double`. The others,
  `fromfpl`, `fromfpxl`, `ufromfpl`, `ufromfpxl` and glibc's `_Float64x` and `_Float128`
  functions, are a link error, `native function 'fromfpl' (libm.so.6) takes or returns a native
  'long double', which is not supported for MSIL; the helper archive clang_rt.builtins forwards
  the 'long double' functions that have a 'double' counterpart`: call the `double` function
  instead (section 8). On windows-msvc `long double` is `double` natively, so the UCRT's `l`
  functions bind as before.
- **Text output.** `clang -S` and llc link no archive, so a module that calls a helper has no
  text: `runtime helper '__muldc3' needs the helper archive clang_rt.builtins, which only
  msil-ld links` (for an atomic operation `runtime helper '__atomic_load_4' is an atomic
  operation that only msil-ld links`). `msil-ld --lto-emit-asm` gives these texts only for the
  calls that code generation creates after LTO (the `_Float16` and `__bf16` conversions,
  `__powidf2`, atomics): it resolves a helper that the program itself calls, such as the
  `__muldc3` of `cmul.c`, before code generation, as an image link does, so with the archive
  (the driver's default) it stops at `msil-ld: error: <archive>(<member>): --lto-emit-asm cannot
  link CIL objects; link without --lto-emit-asm`, and without it at the undefined-symbol report
  below. Either way, compile such a program with `-c` and link it with clang. Until M25 both
  texts were `runtime helper '<name>' is not available for MSIL yet (phase 3)`, and `__muldc3`
  an undefined function of D14.
- **When the link fails.** Without the archive (not built, or `-nodefaultlibs`) a helper is
  `msil-ld: error: undefined symbol: __muldc3` with the line `>>> a compiler runtime helper: link
  the toolchain's helper archive clang_rt.builtins`: build the archive (section 2) or pass its
  path. While an archive of that name takes part, a helper that it leaves out (a `*ti*`
  function that a program calls by name, say) gets the line `>>> a compiler runtime helper that
  the toolchain's helper archive clang_rt.builtins does not define`. So do `__mulhc3` and
  `__divhc3`, which clang calls for `_Float16 _Complex` products and quotients when it does not
  promote half arithmetic to `float` (on the aarch64 triples, and on the x86_64 ones with
  `-fexcess-precision=16`) and which no archive defines: multiply and divide in
  `float _Complex` instead (on x86_64 the default `-fexcess-precision` does so). A helper never
  binds to a native library, even one that exports it (`libgcc_s.so.1`); before M25 the
  `_Complex` helpers did.

### Variadic functions, `printf` and VLAs (M26)

Since M26 C variadic functions work: a program may define its own, use `va_list`, `va_copy` and
the C library's `v*` functions, and call the C library's variadic functions, the `printf` and
`scanf` families among them. This example uses the triple's real `<stdio.h>`:

```c
// hello.c
#include <stdarg.h>
#include <stdio.h>

static int sum(int n, ...) {   /* a variadic definition */
  va_list ap;
  int s = 0;
  va_start(ap, n);
  while (n-- > 0)
    s += va_arg(ap, int);
  va_end(ap);
  return s;
}

int main(void) {
  printf("%s %d %.0f\n", "sum", sum(3, 1, 2, 3), 2.5);   /* prints: sum 6 2 */
  return 0;
}
```

```sh
# Windows (UCRT), from a Visual Studio x64 developer prompt (INCLUDE has the UCRT's headers,
# LIB the default libraries):
clang --target=x86_64-pc-windows-msvc-msil -O2 -c hello.c
clang --target=x86_64-pc-windows-msvc-msil -O2 hello.o -Wl,--print-imports -o hello.exe
hello.exe                # .NET Framework 4.8: prints sum 6 2

# Linux (glibc): the headers and the C library of a glibc sysroot (section 7,
# make-glibc-sysroot.sh), on Windows or Linux.
clang --target=x86_64-unknown-linux-gnu-msil -O2 --sysroot=<glibc sysroot> -c hello.c
clang --target=x86_64-unknown-linux-gnu-msil -O2 --sysroot=<glibc sysroot> hello.o \
  -Wl,--print-imports -o hello.exe
```

Both run at every level and print the same line, as the native builds do. The two links print:

```text
__acrt_iob_func -> api-ms-win-crt-stdio-l1-1-0.dll!__acrt_iob_func [function, -]
__stdio_common_vfprintf -> api-ms-win-crt-stdio-l1-1-0.dll!__stdio_common_vfprintf [function, -]
```

```text
printf -> libc.so.6!vprintf [function, GLIBC_2.2.5]
libc.so.6: version floor GLIBC_2.2.5
```

- **How a variadic call works.** The compiler turns every variadic definition into a method of
  its fixed parameters plus one `va_list` (a `native int`), and every variadic call into a call
  that passes a `va_list` over a buffer in the caller's frame, in the native layout of the
  triple (the System V structure on x86_64 Linux, a `char *` on Windows), so a `va_list`
  passes to and from native code unchanged. The CLR's `vararg` calling convention is never
  used. Status.md section 2, "Variadic functions and acyclic dynamic allocas (M26)", has the
  layouts.
- **Native variadic functions.** On Linux `msil-ld` binds `printf` to its `va_list` variant
  `vprintf` (the P/Invoke is `pinvokeimpl("libc.so.6" as "vprintf" ...) int32
  'printf'(native int, native int)`), and so for the C library's other variadic functions that
  have one (`fprintf` to `vfprintf`, `syslog` to `vsyslog`, `msvcrt.dll`'s on windows-gnu,
  ...). The few without a variant are called with fixed arity, with the signature of each call
  site: glibc's `open`, `fcntl`, `ioctl`, `execl`, `syscall` and the others of its list, and the
  UCRT's `_open`, `_execl`, `_spawnl` and their kin, also under the POSIX names that
  `oldnames.lib` maps to them (`open`, `execl`, `spawnl`, ...); so is a variadic function that you declare
  with an explicit `import_module`. Such a call cannot pass a `double` on the x86_64 Linux
  triples (a P/Invoke cannot set the register count that the System V convention passes in
  `AL`), nor a structure by value on any triple; both are compile errors. The address of a
  native variadic function (`int (*p)(const char *, ...) = printf;` on Linux) is a link error
  until M28.
- **The UCRT** exports no `printf`: its `<stdio.h>` defines the family as inline functions over
  `__stdio_common_vfprintf` and its kin, which the program compiles like its own variadic
  functions, so on windows-msvc `printf` is a method of the image. Code that declares `printf`
  itself, without `<stdio.h>`, links the helper archive's `printf` instead (section 2, "The
  helper archives": build them first); `msil-ld` ignores a request for
  `legacy_stdio_definitions`, the library native code would name. The native C runtime sets the
  stdio option bits at start-up, and so does the image's module initializer when the program
  defines the UCRT's `__local_stdio_printf_options` (as every file that includes `<stdio.h>`
  does): `%.0f` of 2.5 prints `2`, as natively, where without the bits the UCRT prints `3`. As
  natively, a link that names `legacy_stdio_float_rounding` (`-l`, `/DEFAULTLIB:`, `#pragma
  comment(lib)`, with or without `.lib`) clears the rounding bit, one that names
  `iso_stdio_wide_specifiers` clears the legacy wide-specifier bits of `printf` and `scanf`,
  and `legacy_stdio_wide_specifiers` names the default mode and changes nothing; `msil-ld`
  ignores all three as libraries (`--verbose` notes them). `msil-ld`'s `--lto-emit-asm` text
  gets the link's bits through the module flags `msil-ucrt-printf-bits` and
  `msil-ucrt-scanf-bits`; standalone whole-program text keeps the default bits.
- **`long double` formats.** `long double` is `double` on every MSIL triple, so on the triples
  where the native one is wider (x86_64 Linux and MinGW, aarch64 Linux) clang warns about `%Lf`
  and the other `L` floating conversions in `printf` formats, and rejects them in `scanf`
  formats (an error of `-Wformat`; `-Wno-error=format` makes it a warning): the C library
  would read or write a 16-byte value. Use `%f` with `double`.
- **Text output.** `clang -S` and llc keep D14's frozen standalone import table, so they cannot
  compile a call of `printf` (on windows-msvc of `__stdio_common_vfprintf`); compile with `-c`
  and link with clang, or ask `msil-ld` for `--lto-emit-asm` text, whose P/Invoke says `as
  "vprintf"`.
- **VLAs and `alloca`.** A variable-length array, or `alloca` with a run-time size, that no loop
  of its function repeats is a `localloc` in the middle of the method, whose memory lives until
  the function returns (natively a VLA ends with its scope, which no C program can tell apart).
  One inside a loop is
  the compile error `a dynamic stack allocation that can repeat within one call (a VLA or alloca
  in a loop) is not supported for MSIL yet` until M32; move the array out of the loop.
- **What stays an error** (section 8): a native variadic function that has neither a variant nor
  a place on the list (`native function '<f>' (<module>) is variadic and has neither a va_list
  variant nor an entry in the fixed-arity list`), a variant that no library of the link defines,
  a `musttail` call, and an IR `va_arg` instruction on the System V and AAPCS64 triples (clang
  never emits one for C). A C variadic function called through a pointer by native code or by
  another assembly fails fast from M28.
- **Declare C library functions with their prototypes** (include the header). A call through a
  prototype-less declaration such as `int printf();` is an ordinary fixed-signature call, not a
  variadic one: on Linux it binds `printf` itself with fixed arity (a `double` argument then may
  print garbage), on windows-msvc the link fails with `function signature mismatch: printf`.
  clang warns about such a declaration (Status.md section 2, "Boundaries").

### The portable target: `msil64-unknown-dotnet` (P-1a)

Every triple above names a native platform, and its assemblies call that platform's C library
through P/Invoke. The portable triple `msil64-unknown-dotnet` (P-1a) names none: its assemblies
contain no P/Invoke, no native library and no CPU, and the same image runs on every 64-bit
little-endian .NET, on Windows and Linux and as a .NET Framework 4.8 executable. It has its own
data model, the wasm targets' LP64 one: `long` and pointers 64 bits, `wchar_t` and `wint_t`
`int`, `char` signed, `long double` an IEEE `double`, `max_align_t` 8, and it defines
`__dotnet__` and `__msil64__` but no `__ELF__`, `__linux__`, `_WIN32` or `__x86_64__`
(Status.md section 2, "The portable target msil64-unknown-dotnet (P-1a)", has the whole
table). The triple needs no `-msil` suffix; `msil64-dotnet` is the same triple, and
`clang -print-target-triple` prints `msil64-unknown-dotnet`.

Its C library, llvm-libc as the shared library assembly `c.dll`, comes with P-1b. Until then a
portable program is freestanding: it includes only clang's own headers (`<stdint.h>`,
`<stddef.h>`, `<stdarg.h>`, `<limits.h>`, `<float.h>`, `<stdbool.h>`), compiles with
`-ffreestanding` and links with `-nolibc`, which drops the default `-lc` and keeps the helper
archive. It has no output channel either (stdio comes with P-2), so it reports through its exit
status:

```c
// wide.c: no #include of the C library; <stdint.h> and <stddef.h> are clang's own.
#include <stddef.h>
#include <stdint.h>

static int64_t sum(const int32_t *p, size_t n) {
  int64_t s = 0;
  for (size_t i = 0; i < n; ++i)
    s += (int64_t)p[i] << 32;   /* needs 64-bit arithmetic */
  return s;
}

int main(void) {
  static const int32_t v[] = {1, 2, 3, 4, 5, 6};
  return (int)(sum(v, sizeof v / sizeof v[0]) >> 32);   /* exit status 21 */
}
```

Build the portable helper archive once (it is not in `build-builtins.py`'s default set or in
`--all`), then compile, link and run:

```sh
python msil/utils/build/build-builtins.py --triple msil64-unknown-dotnet
clang --target=msil64-unknown-dotnet -print-libgcc-file-name
# <build>/lib/clang/24/lib/msil64-unknown-dotnet/libclang_rt.builtins.a
clang --target=msil64-unknown-dotnet -O2 -ffreestanding -c wide.c
clang --target=msil64-unknown-dotnet -O2 -nolibc wide.o -o wide.exe
wide.exe                  # Windows, .NET Framework 4.8 direct start: exit status 21
dotnet wide.exe           # .NET 8 or later with a runtimeconfig (section 5): exit status 21
```

The link runs `msil-ld --target=msil64-unknown-dotnet
-L<build>/lib/clang-runtimes/msil64-unknown-dotnet/lib/msil64-unknown-dotnet wide.o
<archive>` (the sysroot is `<InstalledDir>/../lib/clang-runtimes/<triple>` unless `--sysroot`
names another; before P-1b its directories need not exist). Without `-nolibc` the link stops at
`msil-ld: error: unable to find library -lc`, and a call of a C library function (`strlen`,
`malloc`) at `undefined symbol: <name>` followed by `>>> a C library function; link the
managed C library c.dll (-lc)`. `-nostdlib` drops the helper archive too.

`-shared` links a portable library assembly as on the host triples (section 3, "Libraries"); it
exports every default-visibility definition, and `-Wl,--export=<sym>`,
`-Wl,--export-if-defined=<sym>` and `-Wl,--export-list=<file>` export more, also hidden
symbols:

```sh
clang --target=msil64-unknown-dotnet -O2 -nolibc -shared twice.c -o twice.dll   # long long twice(long long x) { return 2 * x; }
clang --target=msil64-unknown-dotnet -O2 -nolibc usetwice.c twice.dll -o usetwice.exe
usetwice.exe              # main returns (int)twice(21): exit status 42
```

`-l<name>` looks in each directory for `<name>.dll`, then `lib<name>.dll`, then
`lib<name>.a`. What the portable triple refuses, each with a named error (Status.md has the
texts and how long each lasts): native libraries and import libraries, `import_module`,
`-static` (`'-static' is not supported for 'msil64-unknown-dotnet' yet: the managed C library
links only as the shared assembly c.dll`, until P-3), `-fshort-wchar`, `-mcpu=` and `-march=`,
`.fini_array`, `.ctors` and the other destructor and pre-initialization sections, a constructor
that takes arguments, and an external function in `-S` text (link with `msil-ld`). Atomics,
`thread_local`, `setjmp` and destructors arrive with the milestones that bring them on the host
triples or with P-2 and P-3.

**The 32-bit refusals.** The images are AnyCPU (PE32, machine I386, `ILONLY`), so a 32-bit .NET
process loads them too, where 64-bit arithmetic on `native int` would silently compute wrong
results. Every portable assembly therefore checks its process before anything else runs, in its
module initializer: in a 32-bit process (the 32-bit `dotnet.exe` under `C:\Program Files
(x86)\dotnet`, a .NET Framework start of a copy marked `32BITREQUIRED`) the program stops with

```
Unhandled exception. System.TypeInitializationException: The type initializer for '<Module>' threw an exception.
 ---> System.PlatformNotSupportedException: wide is built for msil64-unknown-dotnet and needs a 64-bit .NET process; this process is 32-bit
   at .cctor()
```

(the 32-bit .NET 10 host prints the `PlatformNotSupportedException` alone, without the
`TypeInitializationException` around it) and exit status 0xE0434352 (134 on Linux); a big-endian runtime gets `... needs a
little-endian .NET process`. A host that loads a portable library catches the
`TypeInitializationException`. The check also refuses, unmeasured, .NET on Windows on ARM64
without .NET Framework 4.8.1 (which may start an AnyCPU Framework executable under x86
emulation), 32-bit linux-arm and browser-wasm .NET and big-endian runtimes such as s390x.

The 32-bit triple `msil32-unknown-dotnet` is reserved for P-4: clang accepts it for
`-fsyntax-only` and `-E`, but compiling stops at `error: target 'msil32-unknown-dotnet' is not
supported yet: only msil64-unknown-dotnet is implemented`, and `msil-ld` refuses it the same
way. A non-msil CPU with OS `dotnet` (`x86_64-unknown-dotnet`), an msil CPU with another OS
(`msil64-unknown-linux`), an environment component (`msil64-unknown-dotnet-gnu`) and an object
format other than MSIL (`msil64-unknown-dotnet-elf`) are `unknown target triple` for clang, also
when it only links; `msil-ld` and `llc` give TargetRegistry's reason instead, such as
`unable to get target for 'msil64-unknown-linux': the architecture 'msil64' needs the OS
'dotnet' (msil64-unknown-dotnet)`.

The portable tests live in `msil/test/Portable` and run only for the portable triple (section 7,
`msil_triple`, and the `-Portable` legs).

### Reference assemblies: `--reference` and the base class library (M21)

The code that the backend and `msil-ld` generate uses a few types and members of the .NET base
class library (`Marshal::AllocHGlobal` for the data image, `Environment::GetCommandLineArgs`
for `argv`, `Buffer::MemoryCopy`, `Interlocked::MemoryBarrier`, ...). Since milestone M21
`msil-ld` binds each of them by searching reference assemblies, as the C# compiler does: give
it the .NET Framework 4.8 reference assemblies, or the reference pack of the .NET you target,
with `-Wl,--reference=<file or directory>` (repeatable) or as `.dll` inputs on the link line.
There is no target-framework option; the references decide. For this program:

```c
// hello.c
int puts(const char *s);
static const char *msgs[] = {"hello", "world"};
int main(int argc, char **argv) {
  puts(msgs[argc > 1]);
  return argc + 41;
}
```

```sh
# From a Visual Studio x64 developer prompt (LIB, for the default libraries, as above).
clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -c hello.c
# No references: the built-in [mscorlib] table, as every link before M21.
clang --target=x86_64-pc-windows-msvc-msil -O2 hello.o -o none.exe
# The .NET Framework 4.8 reference assemblies (on Linux: the NuGet package of section 1,
# <NuGet global packages>/microsoft.netframework.referenceassemblies.net48/1.0.3/build/.NETFramework/v4.8).
clang --target=x86_64-pc-windows-msvc-msil -O2 hello.o -o net48.exe \
  "-Wl,--reference=C:/Program Files (x86)/Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8"
# The .NET 8 reference pack (installed by the .NET 8 SDK): its ref/net8.0 directory, not the
# version directory 8.0.33 above it (below); the .NET 10 pack is .../10.0.<n>/ref/net10.0.
# Only .NET runs the result, so it is named .dll (section 5, "Executable names").
clang --target=x86_64-pc-windows-msvc-msil -O2 hello.o -o net8.dll \
  "-Wl,--reference=C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.33/ref/net8.0"
llvm-cil-dump net8.dll | grep ^assemblyref
```

`none.exe` and `net48.exe` reference `mscorlib` 4.0.0.0 alone (linked under one output name,
their canonical listings are equal) and run on .NET 8, .NET 10 and the .NET Framework.
`net8.dll`, an EXE image like the others, references the assemblies of the pack that define
what it uses, in the order of first use:

```text
assemblyref 'System.Runtime' ver 8.0.0.0 flags=0x00000000 key=B0 3F 5F 7F 11 D5 0A 3A culture=''
assemblyref 'System.Runtime.InteropServices' ver 8.0.0.0 flags=0x00000000 key=B0 3F 5F 7F 11 D5 0A 3A culture=''
```

(a program with a `memmove` of variable length or a fence adds `System.Threading`). It runs on
.NET 8 and later (`dotnet net8.dll` prints `hello` and exits 42, with `net8.runtimeconfig.json`,
section 5), and the .NET Framework refuses it with `System.IO.FileNotFoundException: Could not
load file or assembly 'System.Runtime, Version=8.0.0.0, ...'`. Named `net8.exe` it would be the
same image, with the warning `net8.exe: this executable is bound to System.Runtime 8.0.0.0, so
it runs only on .NET through 'dotnet' and cannot start on .NET Framework; .NET executables are
named .dll (for example -o net8.dll)` (M23). An image bound to the .NET 10 pack runs on .NET 10
and later only; .NET 8 stops it with `System.MissingMethodException: Entry point not found`.
For one image that runs everywhere, link without references or with the .NET Framework 4.8
reference assemblies.

- **What counts as a reference.** A directory gives every `*.dll` in it that is a .NET assembly,
  not recursively (the v4.8 directory's `Facades/` is not searched), and skips its other files;
  a file given by name that is not a .NET assembly is `<path>: not a .NET assembly`. A `.dll`
  on the link line (`clang ... hello.o <pack>/System.Runtime.dll
  <pack>/System.Runtime.InteropServices.dll -o net8.dll`) is a reference assembly as well,
  unless this toolchain built it as a library (M22; next subsection); the image is the same
  whichever way, and
  in whatever order, the same assemblies are given. Point `--reference` at reference
  assemblies, not at a runtime's own directory: `<dotnet>/shared/Microsoft.NETCore.App/<v>`
  binds to `System.Private.CoreLib` with the warning `<path>/System.Private.CoreLib.dll is not
  a reference assembly; binding 'System.Runtime.InteropServices.Marshal' to
  System.Private.CoreLib 8.0.0.0 [7cec85d7bea7798e] ties the output to one runtime build`.
- **A pack is its `ref/net<N>.0` directory.** Give `--reference` the pack's
  `ref/net8.0` (or `ref/net10.0`), not the version directory above it: `8.0.33/` holds only
  the directories `analyzers/`, `data/` and `ref/`, which `--reference` does not search, so it
  adds no assembly. A link with a `--reference` never falls back to the built-in table, even
  when the option adds nothing (an empty directory, or one of native DLLs only), so the
  version directory stops the link:

  ```sh
  clang --target=x86_64-pc-windows-msvc-msil -O2 hello.o -o net8.dll \
    "-Wl,--reference=C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.33"
  ```

  ```text
  msil-ld: error: the references contain no core library (an assembly that defines System.Object and references no other assembly); given: no references
  ```

- **One core library.** The references must hold exactly one assembly that defines
  `System.Object` and references no other assembly (`mscorlib` of the .NET Framework,
  `System.Runtime` of a .NET pack). A facade does not count, so a pack's `mscorlib.dll` alone
  fails, and so does a mix of the two worlds:

  ```text
  msil-ld: error: the references contain no core library (an assembly that defines System.Object and references no other assembly); given: mscorlib 4.0.0.0 [b77a5c561934e089] (facade, forwards System.Object to System.Runtime)
  msil-ld: error: the references mix two core libraries: mscorlib 4.0.0.0 [b77a5c561934e089] (C:/Program Files (x86)/Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8/mscorlib.dll) and System.Runtime 8.0.0.0 [b03f5f7f11d50a3a] (C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.33/ref/net8.0/System.Runtime.dll)
  ```

  The whole v4.8 directory and a pack's `ref/net8.0` together fail before that, with an error
  for each assembly that both hold: `Microsoft.CSharp.dll` is 4.0.0.0 in one and 8.0.0.0 in
  the other (`... have the same name but different versions or keys`), and `mscorlib.dll`,
  `Microsoft.VisualBasic.dll`, `System.Core.dll` and others have the same identity in both but
  other contents, which names the two files in the same order whatever the order of the
  options:

  ```text
  msil-ld: error: references C:/Program Files (x86)/Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8/mscorlib.dll and C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.33/ref/net8.0/mscorlib.dll have the same identity (mscorlib 4.0.0.0 [b77a5c561934e089]) but different contents
  ```

  Two copies of the same file only warn (`<path> has the same identity as <path>
  (<identity>); using the first`).

- **How it binds** (Status.md section 2, "Reference assemblies and the base class library", has
  every rule and message): a type binds to its one public definition, never to a facade's
  forwarder, whose chain `msil-ld` follows to the definition (`type '<Ns.Type>' is not defined
  in the references (<n> assemblies)` when no reference has it, as for `Marshal` with only
  `System.Runtime.dll` of the .NET 8 pack); a member by its full signature, return type
  included, with the definition's custom modifiers; each AssemblyRef carries the version and
  public key token of the defining assembly's own Assembly row.
- **Without references** `msil-ld` binds through a built-in table of the members that resolve
  through `[mscorlib]` on every runtime, and writes the image M20 wrote. `msil-ld
  --print-bcl-table`, which needs no input, prints it and exits:

  ```text
  type [mscorlib]System.ValueType
  type [mscorlib]System.Runtime.InteropServices.Marshal
  type [mscorlib]System.Environment
  type [mscorlib]System.Buffer
  type [mscorlib]System.Threading.Interlocked
  method native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  method native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  method string[] [mscorlib]System.Environment::GetCommandLineArgs()
  method void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64)
  method void [mscorlib]System.Threading.Interlocked::MemoryBarrier()
  method native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalUni(string)
  type [mscorlib]System.EntryPointNotFoundException
  method instance void [mscorlib]System.EntryPointNotFoundException::.ctor(string)
  type [mscorlib]System.Object
  type [mscorlib]System.Attribute
  method instance void [mscorlib]System.Attribute::.ctor()
  type [mscorlib]System.AttributeUsageAttribute
  type [mscorlib]System.AttributeTargets
  method instance void [mscorlib]System.AttributeUsageAttribute::.ctor(valuetype System.AttributeTargets)
  method instance void [mscorlib]System.AttributeUsageAttribute::set_AllowMultiple(bool)
  ```

  The last six (M22) are what the symbol-table attribute of a library (next subsection) needs.

  Anything else needs references: `.NET member '<signature>' is not in msil-ld's built-in
  mscorlib table; pass --reference=<reference assemblies>`. `python
  msil/utils/check-mscorlib-safe.py --msil-ld build/bin/msil-ld --out <dir>` proves the table
  by running code: it writes an ILAsm probe that takes the token of every entry through
  `[mscorlib]`, assembles it with ilasm and runs it on the .NET Framework 4.8, .NET 8, .NET 10
  and WSL .NET 10 (`--runtimes netfx,net8,net10,wsl-net10`, the default), and names every
  entry that fails with its exception; `--table <file>` checks a table of the same form
  instead, and `--self-test` checks the probe itself (`MathF::Sqrt` must fail on the .NET
  Framework).
- **Native variables** (above) use `NativeLibrary.Load` and `NativeLibrary.GetExport` when the
  references define them (the .NET 8 and 10 packs), instead of `kernel32.dll` or `dlopen`.
- **Text is never bound.** `clang -S`, llc and `-Wl,--lto-emit-asm` keep printing `[mscorlib]`;
  with `--lto-emit-asm`, `msil-ld` still reads the references and reports the core-library and
  identity errors above. The text imports native variables with `kernel32.dll` or `dlopen`
  even when the references define `NativeLibrary`, so on a Linux triple a `--lto-emit-asm`
  link with references and without `dlopen`/`dlsym` (`-nodefaultlibs`) is `data symbol
  '<name>' in <module> cannot be imported: --lto-emit-asm text is never bound to the
  references, and the native libraries define no dlopen/dlsym`. An LTO link that writes the
  image binds like any other.
- **Verifying such an image.** PEVerify cannot load an image bound to the .NET 8 or 10 pack, so
  check it with ILVerify against the pack it was bound to: `msil-verify.py --runtime-dir <pack>
  --skip-peverify "bound to .NET 8 references" ...` (section 6), as the `refs-net8` and
  `refs-net10` legs do (section 7).

### Libraries: `-shared`, `/LD` and linking against them (M22)

Since milestone M22, `clang -shared` (clang-cl `/LD` or `/LDd`) links a *library assembly*: a
DLL without an entry point whose exported functions are public static methods of one class,
the *export class*, and which programs, other libraries and C# programs link against (Status.md
section 2, "Libraries and assembly-to-assembly linking", has every rule and message). A library
and its program:

```c
// zl.c: the library
int counter = 5;                         /* exported data */
static int calls;                        /* stays inside the library */
int twice(int x) { calls++; return 2 * x; }
int bump(void) { return ++counter; }
__attribute__((constructor)) static void init(void) { counter = 100; }
```

```c
// main.c: the program
int puts(const char *s);
extern int counter;
int twice(int);
int bump(void);
int *pc = &counter;                      /* the library's address in the program's data */
int main(void) {
  if (counter == 100)                    /* the library's constructor ran before main */
    puts("library ready");
  *pc = 7;
  return bump() + twice(21);             /* 8 + 42 */
}
```

```sh
# From a Visual Studio x64 developer prompt (LIB), as above; for the Linux triple add
# --sysroot=<a Linux sysroot> (section 7) to both links.
clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -c zl.c main.c
clang --target=x86_64-pc-windows-msvc-msil -O2 -shared zl.o -o zl.dll
clang --target=x86_64-pc-windows-msvc-msil -O2 main.o zl.dll -o main.exe
main.exe                    # .NET Framework 4.8: prints 'library ready', exit status 50
dotnet main.exe             # with main.runtimeconfig.json (section 5); zl.dll needs none
```

`clang-cl /LD zl.c` compiles and links `zl.dll` in one command (`/LDd` alike; `/Fe` names the
output), and `-shared` without `-o` writes `a.exe`, which is a DLL image all the same.

- **What the library holds.** On the Linux triples every function and variable of default
  visibility is exported, as `ld.lld -shared` exports it (`-fvisibility=hidden` and
  `__attribute__((visibility("default")))` work as natively); on the Windows triples the
  `__declspec(dllexport)` ones when the inputs have any (`-Wl,--export-all-symbols` exports the
  others too), else every default-visibility one. `-Wl,--exclude-libs=libutil.a` (archive
  file names separated by `,` or `:`, or `ALL`) keeps the archives' definitions out. Here `counter`, `twice` and `bump` are the exports, while
  `calls`, `init` and every hidden function stay private. `llvm-cil-dump zl.dll` (section 6)
  shows the export class `zl` (`public abstract sealed`, flags 0x181) with the methods `bump`
  and `twice`, an accessor per export (`'<msil>addr_bump'`, `'<msil>addr_counter'`,
  `'<msil>addr_twice'`, each returning that address as a `native int`) and a property
  `counter` whose getter is `'<msil>addr_counter'`; the class `'<msil>Data'`, whose `.cctor`
  sets up the library's data (phase 1); and the symbol table, which `llvm-cil-dump
  --link-info zl.dll` prints:

  ```text
  SymbolTable v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 runtime='ucrt' datamodel=8,4,2,8
  ExportClass 'zl'
  Data 'counter' size=4 align=4
  ```

  `runtime` is the library's C runtime: the first of the link's `-l` options that names one,
  the driver's `-lucrt` here (`c` on Linux; `none` with `-nostdlib`). A program or library
  that links against it must use the same one, the same triple and the same data model.
- **The export class** is the assembly name, here `zl`, a class without namespace. Name it with
  `-Wl,--export-class=<Ns.Name>`: `--export-class=Contoso.Zlib.Native` gives the class `Native`
  in the namespace `Contoso.Zlib`, which C# imports with `using Contoso.Zlib;`. An assembly
  name with dots is split the same way (`zlib.core` is the class `core` in `zlib`).
  `-Wl,--assembly-version=1.2.3.4` sets the version (default 0.0.0.0), which programs that
  link against the library record.
- **Linking against it.** Give the library as an input (`zl.dll`), or with `-L<dir> -lzl`: `-l`
  takes a library that this toolchain built after a static library and before an import
  library or a native shared one, and on the Linux triples finds `zl.dll` or `libzl.dll`. A
  call of `twice` is a call of `[zl]zl::twice` with the library's signature (a call whose
  signature differs gets plan Q8's adapter and warning, or its error, as between objects), the
  address of `counter` is a call of its accessor, and `int *pc = &counter;` is stored when the
  program starts, after the library has set up its own data. Every symbol must resolve, in a
  library too (`undefined symbol: <name>`), and the program's own definition of a name that a
  library also exports wins for the program, with a warning, while the library keeps its own:
  assemblies share no global symbol scope (no interposition). `-Wl,-Map=<file>` lists the
  libraries under `Inputs:` and the symbols bound to their exports under `Symbols:`, as
  `ManagedFunction` and `ManagedData` rows with the library as the input.
- **Deploying.** Put the libraries next to the program: `dotnet` and the .NET Framework load
  them from the application directory. Keep the file name equal to the assembly name (`zl.dll`
  holds the assembly `zl`; `msil-ld` warns when `--assembly-name` makes them differ), and give
  the program a name of its own: a library input with the output's assembly name is an error
  (`main.exe` cannot link `main.dll`), because the CLR loads one assembly of a name. Every
  linked library initializes before `main`, the libraries it links first. A missing library
  stops the program before `main` with a `System.TypeInitializationException` around a
  `System.IO.FileNotFoundException` that names it, and a library rebuilt without a function
  that the program calls stops the calling method with `System.MissingMethodException`.
- **Cycles.** Libraries that need each other cannot both link first, because each must resolve
  its symbols. Link one of them as an *interface* first: `-Wl,--interface-only` writes the
  same export class, signatures and symbol table with stub bodies, and allows undefined
  symbols. For `a.c` and `b.c`, which reference each other's functions and data:

  ```sh
  clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -c a.c b.c main.c
  clang --target=x86_64-pc-windows-msvc-msil -O2 -shared -Wl,--interface-only b.o -o iface/b.dll
  clang --target=x86_64-pc-windows-msvc-msil -O2 -shared a.o iface/b.dll -o a.dll
  clang --target=x86_64-pc-windows-msvc-msil -O2 -shared b.o a.dll -o b.dll
  clang --target=x86_64-pc-windows-msvc-msil -O2 main.o a.dll b.dll -o main.exe
  ```

  Deploy `a.dll`, `b.dll` and `main.exe`; the interface (here in `iface/`) is never deployed
  (`-Wl,--verbose` reminds you when a link reads one). Each library sets up its data before
  any cross-assembly pointer or constructor of the cycle is applied, so `a`'s pointer into `b`
  and `b`'s into `a` are both right; which library's constructors run first is unspecified, as
  natively.
- **A C# consumer.** C# sees the export class as a static class: exported functions with their
  C signatures (pointers as `nint`), and a property per variable whose value is its address.
  Build the library with an export class C# can name:

  ```sh
  clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -shared \
    -Wl,--export-class=Contoso.Lib.Native zlibc.c -o zlibc.dll
  ```

  ```csharp
  // use.cs; zlibc.c defines int counter, int add(int, int) and int length(const char *).
  using System;
  using System.Runtime.InteropServices;
  using Contoso.Lib;

  unsafe static class Program
  {
      static int Main()
      {
          int* counter = (int*)Native.counter;        // a variable: its address
          Console.WriteLine("counter = " + *counter);
          Console.WriteLine("add(2, 3) = " + Native.add(2, 3));
          IntPtr s = Marshal.StringToHGlobalAnsi("hello");
          Console.WriteLine("length = " + Native.length(s));
          Marshal.FreeHGlobal(s);
          return 0;
      }
  }
  ```

  Compile it with a reference to `zlibc.dll` and `-unsafe` (csc `-r:zlibc.dll -unsafe`, or a
  reference to the DLL in a C# project), for .NET or for the .NET Framework 4.8, and run it
  with the library next to it. The test harness's compiler step does that with the SDK's csc:
  `python msil/utils/jitall/build-jitall.py --csharp use.cs --out use.exe --reference
  zlibc.dll --refs net8 --unsafe` (`--refs net48` for the .NET Framework; section 7). The
  library's constructors run before `Main`'s first statement.
- **What does not work** (Status.md section 2 lists the limits): `-Wl,--lto-emit-asm` with
  `-shared` or with a library input (`--lto-emit-asm cannot write a -shared library; ...`,
  `<file>: --lto-emit-asm cannot import from a .NET library; ...`); an EXE as a library input;
  thread-local exports (phase 3); interposition (one copy of a name per assembly). A native
  function's address differs between assemblies, because each has its own P/Invoke stub.

### Rust bitcode (M22)

rustc's bitcode for the native x86_64 targets compiles and links like stock-clang bitcode
("Programs of several files" above) once rustc's fat LTO puts the whole crate graph, `core` and
`alloc` included, into one bitcode file. For a `no_std` crate with `#[no_mangle] pub extern "C"
fn main(argc: i32, argv: *const *const u8) -> i32`, a `#[panic_handler]` and a
`#[global_allocator]` over the C library's `malloc` and `free`:

```sh
rustc --edition 2021 --crate-type staticlib -C panic=abort -C lto=fat -C codegen-units=1 \
  -C opt-level=2 --target x86_64-pc-windows-msvc --emit=llvm-bc,link app.rs   # writes app.bc
clang --target=x86_64-pc-windows-msvc-msil -O2 app.bc -o app.exe               # or msil-ld
```

(`--target x86_64-unknown-linux-gnu` and `clang --target=x86_64-unknown-linux-gnu-msil
--sysroot=<sysroot>` for Linux; with cargo, the same settings in `Cargo.toml` and `cargo rustc
--release --target <triple> -- --emit=llvm-bc,link`.) clang compiles the bitcode into a CIL
object with the warning `overriding the module target triple with <triple>-msil`
(`-Woverride-module`); with `-flto`, or given to `msil-ld` itself, it is linked by LTO with
`msil-ld`'s stock-bitcode warning instead. `core`, `alloc` and `no_std` crates such as
serde_json, hashbrown, miniz_oxide and sha2 run this way on .NET 8, the .NET Framework 4.8 and
WSL. M22 fixed what the experiment that found this recipe hit (Status.md section 2, "Rust
bitcode"): bitcode with the kernel code model (`x86_64-unknown-none`) under LTO, comment-only
`asm!` barriers (serde_json's float formatter), the native object file that
`-Clinker-plugin-lto` passes, and constructors that take `argc`, `argv` and `envp` on Linux
(std's `ARGV_INIT_ARRAY`). Atomics (`Arc`), thread-local storage, native variadic functions and
callbacks are phase 3, so `std` does not run yet without stand-ins for them.

### Instruction encodings: `-show-mc-encoding` and `-M object-form` (M18)

From milestone M18 every instruction that the backend emits is one CIL instruction, and a
binary encoder encodes it (Status.md section 2, "Binary CIL"). Two llc options show what the
CIL objects of M19 hold; they are llc's own options, which `clang -mllvm` does not accept, so
write the IR first:

```sh
clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -S -emit-llvm fib.c -o fib.ll
llc -show-mc-encoding fib.ll -o fib.enc.il    # each instruction's bytes and fixups as comments
llc -M object-form fib.ll -o fib.obj.il       # the operand spelling of CIL objects (plan 5.8.4)
```

With `-show-mc-encoding`, `MCAsmStreamer` writes the bytes after each instruction; a byte that
the object writer fills in is a letter, and the fixup that fills it follows on its own line:

```text
  ldarg.0                               // encoding: [0x02]
  ldc.i4.2                              // encoding: [0x18]
  blt.s @BB1_3                          // encoding: [0x32,A]
                                        //   fixup A - offset: 1, value: @BB1_3-1, kind: FK_PCRel_1
  ...
  call int32 'fib'(int32)               // encoding: [0x28,A,A,A,A]
                                        //   fixup A - offset: 1, value: int32 'fib'(int32), kind: fixup_cil_method_token
```

- A metadata token is four bytes that a `fixup_cil_<class>_token` fixup fills: `method`
  (`call`, `ldftn`), `field` (`ldsfld`, `stsfld`, `ldsflda`) and `sig` (`calli`); the value is
  the operand in Phase 1's spelling.
- A branch is a PC-relative fixup on its label: `FK_PCRel_1` on `<label>-1` for a short
  branch, `FK_PCRel_4` on `<label>-4` for a long one, the displacement from the end of the
  instruction. Entry i (from 0) of an N-way `switch` is an `FK_PCRel_4` on `<label>-4*(N-i)`,
  measured from the end of the `switch`.
- The branch forms are chosen per method before it is emitted (short where the target is in
  reach, else long), so the text and the bytes agree; switch entries are always 32-bit.
- The comments do not change the text for ilasm while no instruction has more than 191 fixups:
  `fib.enc.il` assembles to an image that lists exactly like that of `fib.il` (`llvm-cil-dump`,
  section 6). `MCAsmStreamer` prints fixup i of an instruction as `char('A' + i)`, so entry 191
  of a `switch` (the 192nd fixup) prints as a NUL byte, later ones also as LF (201) and CR
  (204), and ilasm cannot assemble the text of a `switch` of more than 191 targets ('syntax
  error' on its line; jump tables have up to 4096 entries, plan D35); plain `-S` text is not
  affected. `cli-corpus-check.py --mode encoder` (section 7, the `encoder-parity` leg) assembles
  such text for every program of the test corpus, whose largest `switch` has 68 targets, and
  compares the bytes of each method with the encoding comments.

`-M object-form` changes only the operands that name an undefined function or variable (a
member of `'<msil>Symbols'` in the assembly `msil.extern`), the address field of a function (a
field of `'<msil>FnAddr'`) or a member of the .NET base class library (the scope `[msil.bcl]`);
the functions and variables that the module defines print as before. `diff fib.il fib.obj.il`
shows only the call of `puts`, and a program that takes the address of a function `inc` and
has an `atomic_thread_fence` also shows the other two kinds:

```text
  call int32 [msil.extern]'<msil>Symbols'::'puts'(native int)
  ldsfld native int [msil.extern]'<msil>FnAddr'::'inc'
  call void [msil.bcl]System.Threading.Interlocked::MemoryBarrier()
```

Such text is not a runnable module: ilasm only warns that `msil.extern` and `msil.bcl` are
undeclared assemblies. `-msil-object-form` (M19, next subsection) adds the declarations and the
rest of the object. `llvm-mc -filetype=obj` and `clang -cc1as -filetype obj` stop with
`assembly parsing is not supported for MSIL`: ILAsm text is never an assembler input
(section 8), and CIL objects come from the code generator.

### CIL objects: `clang -c` and `llc -filetype=obj` (M19)

From milestone M19, `clang -c` writes a *CIL object* on every MSIL triple: an ECMA-335
netmodule whose methods hold the backend's binary CIL, with a *link-info record* that says
what the symbol table, sections and relocations of a native object say, and a COFF symbol
table for `nm`-style tools (Status.md section 2, "CIL objects"; plan 5.8). `msil-ld` links CIL
objects since M20 (above, "Programs of several files"), and a compile-and-link command compiles
to them; in M19 a program of several files was compiled with `-flto -c`.

```c
// main.c
int puts(const char *s);
int add3(int x);              /* defined in util.c */
extern int counter;           /* defined in util.c */
static const char msg[] = "hello";
int table[2] = {1, 2};

int main(void) {
  puts(msg);
  return add3(counter) + table[1];
}
```

```c
// util.c
int counter = 39;
int add3(int x) { return x + 3; }
```

```sh
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -c main.c util.c   # main.o, util.o
clang-cl --target=x86_64-pc-windows-msvc-msil /O2 /c main.c                         # main.obj
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S -emit-llvm main.c -o main.ll
llc -filetype=obj main.ll -o main.o                                                  # here the same bytes
```

- `-c` and clang-cl's `/c` run one `cc1 -emit-obj` job (no assembler, no bitcode, no link);
  `clang -###` shows it. `-flto -c`, `-flto=thin -c` and `-emit-llvm -c` still write bitcode,
  and `-fno-lto -c` writes an object as plain `-c` does.
- The object is a PE32+ netmodule (`llvm-cil-dump --headers`: `machine=0x8664` or `0xAA64`,
  `characteristics=0x2022`, `entrypoint=0x00000000`), so nothing can run it, and nothing in it
  runs when it is loaded: it has no assembly row, no `.cctor`, no `'<msil>Main'` and no data
  image. Its module is named after the source file (`module 'main.c'` in the canonical
  listing), never after `-o`, and two compiles in different directories, with different `-o`
  names, write the same bytes.
- `main.c` compiles although `add3` and `counter` are defined elsewhere: in an object they are
  undefined symbols, which `msil-ld` resolves. `clang -S` still compiles one file as a
  whole program, where they are errors (`undefined function 'add3' is not defined in this
  module and is not a Phase-1 import ...` and `external global variable 'counter' is not
  supported (phase 2 linker)`); Status.md section 2 has the table of what object mode accepts
  and `-S` rejects (weak and COMDAT definitions, common symbols, aliases, sections, linker
  options, K&R calls, ...).

`llvm-cil-dump --link-info` prints the record (section 6), one line per entry, here on the
Linux triple:

```text
$ llvm-cil-dump --link-info main.o
LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
Symbol 0 DATA GLOBAL DEFAULT 'table' field=0x04000001 segment=0 offset=0 size=8
Symbol 1 DATA LOCAL DEFAULT 'msg' field=0x04000002 segment=1 offset=0 size=6
Symbol 2 DATA GLOBAL DEFAULT UNDEFINED 'counter'
Symbol 3 FUNCTION GLOBAL DEFAULT 'main' method=0x06000001
Symbol 4 FUNCTION GLOBAL DEFAULT UNDEFINED 'puts'
Symbol 5 FUNCTION GLOBAL DEFAULT UNDEFINED 'add3'
Segment 0 '.data' align=4 size=8 flags=0
  01 00 00 00 02 00 00 00
Segment 1 '.rodata' align=1 size=6 flags=READONLY
  68 65 6C 6C 6F 00
```

The header gives the triple and the data model (the sizes of `void *`, `long`, `wchar_t` and
`long double`); each symbol its kind, binding, visibility and metadata row (each defined
variable is a `native int` field that `msil-ld` sets to the variable's address); each
segment the bytes of one variable. Relocations, COMDAT groups, constructors, call flags and
linker options follow as `Reloc`, `Comdat`, `Init`, `Signature` and `LinkerOption` lines when
the file has any. The record's format is version 1.0, frozen by M20's commit after `msil-ld`
had linked the link-semantics matrix with it (plan 5.8.9); M20 added the call flag
`NATIVE_INCOMPATIBLE` for a result or parameter that a P/Invoke cannot pass natively. On the
windows-msvc triple the record's triple carries the MSVC version that clang compiles for
(`-fms-compatibility-version=19.40` gives `x86_64-pc-windows-msvc19.40.0-msil`), and clang-cl
adds its default `/DEFAULTLIB:` options as `LinkerOption` lines.

Symbol tools read the COFF view as they read a native object:

```text
$ llvm-nm main.o util.o
main.o:
         U add3
         U counter
180002000 T main
180004000 r msg
         U puts
180004000 D table

util.o:
180002000 T add3
180004000 D counter
$ llvm-ar rcs libutil.a main.o util.o && llvm-nm --print-armap libutil.a
Archive map
add3 in util.o
counter in util.o
main in main.o
table in main.o
...
```

`T` is a function (also an alias of one), `D`, `R` and `B` data in a writable, read-only or
zero segment, `C` a common symbol, `U` an undefined one, weak or not, and lower case a local
(`static`) symbol; the archive index lists the defined global symbols and the commons.
`llvm-objdump -t`, `llvm-readobj --symbols` and GNU `nm` and `ar` read the view too; both
ildasms and System.Reflection.Metadata (`mdcanon`, section 7) read the metadata. Native
librarians and linkers refuse a CIL object, as they refuse other netmodules: `lib.exe` stops
with `LNK1302: only support linking safe .netmodules; unable to link pure .netmodule`,
`llvm-lib` with `not a COFF object, bitcode, archive, import library or resource file`, and
`lld-link` and `ld.lld` with `unknown file type`; link them with `msil-ld` (which in M19 still
refused them with `<file>: linking CIL object files and .NET assemblies is not supported yet`).

**Object-form text.** `-mllvm -msil-object-form` (llc: `-msil-object-form`) makes `-S` write
the object as ILAsm text instead of the runnable module (plan 5.8.4): a header comment, the
placeholder assemblies, the methods with `-M object-form`'s operands, the address fields,
the P/Invoke stubs of import-attributed functions, and last the module name and the record as
a custom attribute. ilasm turns it into an object that lists exactly like `clang -c`'s, record
included, but has no COFF view (`llvm-nm` prints `no symbols`), which is how the tests check
the object writer:

```sh
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S -mllvm -msil-object-form main.c -o main.obj.il
ilasm -nologo -quiet -dll -pe64 -x64 -output=main.ilasm.o main.obj.il
llvm-cil-dump --params=drop --initlocals=ignore-without-locals --snan=quiet main.o > a.canon
llvm-cil-dump --params=drop --initlocals=ignore-without-locals --snan=quiet main.ilasm.o > b.canon
diff a.canon b.canon       # no difference
```

```text
// MSIL object generated by LLVM
// source_filename = "main.c"
// target triple   = "x86_64-unknown-linux-gnu-msil"
.assembly extern 'msil.extern' { .ver 0:0:0:0 }
.assembly extern 'msil.bcl' { .ver 0:0:0:0 }
.assembly extern 'msil.link' { .ver 0:0:0:0 }

.method public static int32 'main'() cil managed
{
  .maxstack 3
  ldsfld native int 'msg'
  call int32 [msil.extern]'<msil>Symbols'::'puts'(native int)
  pop
  ldsfld native int [msil.extern]'<msil>Symbols'::'counter'
  ldind.i4
  call int32 [msil.extern]'<msil>Symbols'::'add3'(int32)
  ...
} // end of method 'main'
.field public static initonly native int 'table'
.field assembly static initonly native int 'msg'

.module 'main.c'
.custom instance void [msil.link]'<msil>LinkInfo'::.ctor(uint8[]) = ( 01 00 ... 00 00 )
```

The three normalizations ignore what ilasm adds or changes on its own (a parameter row per
parameter, `InitLocals` on bodies without locals, the quiet bit of float32 signaling NaNs;
plan 5.12.6). `msil-ld` never reads this text, and an object that went through ildasm and
ilasm loses its COFF view and is not guaranteed to link (plan 5.8.4). The `obj-text` leg
(section 7) runs this comparison, and more, over every C program of the test corpus.

## 4. Assemble with ilasm

This is the end of the `-S` route (section 3, "One file to ILAsm text"), and of the
`-Wl,--lto-emit-asm` text of an LTO link; a program that `msil-ld` links needs no ilasm. The
tests use ilasm only as an oracle: the `asm-oracle`, `modern-ilasm` and `wsl-ilasm` legs, the
reference side of the equivalence oracles O3 and O4, and the round trip (section 7).

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
`fib.runtimeconfig.json`), so one `.exe` serves every runtime; `msil-ld`'s own images are such
`.exe` files too (section 3). For the `--lto-emit-asm` text of an LTO link, which the tests
assemble that way with `--param msil_link_output=il`, `msil-ld` names the assembly after its
own `-o` (`prog.il` gives `prog`), so `-output=prog.exe` keeps the file and assembly names
equal.

## 5. Write a runtimeconfig.json and run

`dotnet fib.exe` (or `dotnet fib.dll`) needs `fib.runtimeconfig.json` next to the image; it
names the shared framework to run on. Neither the compiler nor `msil-ld` writes it (Design.md
section 8): like the C# compiler, they leave that to your build system, or to you. Without it
`dotnet` stops with "The library 'hostpolicy.dll' (Linux: 'libhostpolicy.so') required to execute
the application was not found" and exit status `0x80008083` (131 on Linux, which keeps 8 bits,
and in Git Bash).

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

Save that as `fib.runtimeconfig.json` (the stem of the image plus `.runtimeconfig.json`), then:

```sh
dotnet fib.exe; echo $?          # prints 6765, then 55
dotnet exec --runtimeconfig fib.runtimeconfig.json fib.exe   # same; the file may live anywhere
```

- `"version": "8.0.0"` runs on the newest installed 8.0.x. Add `"rollForward": "Major"` to fall
  back to a newer major version when .NET 8 is missing, or `"rollForward": "LatestMajor"` to
  always take the newest installed runtime. `tfm` is optional.
- Without reference assemblies (and with the .NET Framework 4.8 ones) the image references only
  `mscorlib`, so the same file runs on .NET 8, 9 and 10 and, for a Windows triple, on .NET
  Framework 4.8; so does ilasm's image of `-S` text. An image bound to the .NET 8 pack needs
  `"version": "8.0.0"` or later in its runtimeconfig, one bound to the .NET 10 pack `"10.0.0"`
  (section 3, "Reference assemblies"). The libraries a program links (M22, section 3) are
  `.dll` files next to it that need no runtimeconfig of their own. A program that `msil-ld`
  linked against a native library of your own needs that library next to it when it runs, and
  on Linux on the loader's path when the program imports the library's variables (section 3,
  "Native libraries and P/Invokes").
- Arguments after the image reach `main`: `dotnet fib.exe a b` gives `argc` 3, and `argv[0]` is
  the path of the image (`Environment.GetCommandLineArgs()`).
- The exit status is `main`'s return value. Linux keeps only its low 8 bits, as for native
  programs. A trap (`__builtin_trap`, reaching `__builtin_unreachable`, or a call of a `weak`
  function that nothing defines) ends in an unhandled `System.NullReferenceException`.

.NET Framework 4.8 (Windows triples only) runs the `.exe` directly, with no configuration file:

```bat
fib.exe
echo %ERRORLEVEL%
```

### Executable names

`msil-ld` writes an EXE image (with an entry point, characteristics of an executable) whatever
the output is called, and clang's default output is `a.exe` on every MSIL triple; the name is a
convention that says who starts the program:

- **An executable that can run on .NET Framework**, linked without reference assemblies or
  against the .NET Framework 4.8 reference assemblies, is `<name>.exe`: the .NET Framework
  starts it directly, and .NET runs it as `dotnet <name>.exe` with `<name>.runtimeconfig.json`.
- **An executable bound to a .NET reference pack** (`-Wl,--reference=<pack>/ref/net8.0`) runs
  only on .NET and is `<name>.dll`, run as `dotnet <name>.dll` with the
  `<name>.runtimeconfig.json` that the build system writes, as .NET names the programs that the
  C# compiler builds. The native apphost `<name>.exe` that the .NET SDK adds next to such a
  program is not the toolchain's job: `msil-ld` writes neither it nor the runtimeconfig.
- Since M23 `msil-ld` warns when a program bound to .NET's core library (`System.Runtime` of a
  pack, or `System.Private.CoreLib` of a runtime directory) or to `netstandard` 2.1 or later
  (the .NET Framework 4.8 has only `netstandard` 2.0, so only a 2.0 image runs on both) is
  named `.exe`, in any case:
  `msil-ld: warning: <path>: this executable is bound to <core library> <version>, so it runs
  only on .NET through 'dotnet' and cannot start on .NET Framework; .NET executables are named
  .dll (for example -o <stem>.dll)`. The image is the same either way; rename it (section 8). A
  library (`-shared`) is always `<name>.dll`.

On Linux, or in WSL, the same steps work with a Linux triple; on a Linux host clang finds the
system's C library itself:

```sh
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding fib.c -o fib.exe
cat > fib.runtimeconfig.json <<'EOF'
{ "runtimeOptions": { "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" },
                      "rollForward": "Major" } }
EOF
dotnet fib.exe; echo $?
# The oracle route: the Linux ilasm assembles the -S text of section 3.
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S fib.c -o fib.il
ilasm -nologo -quiet -exe -pe64 -x64 -output=fib.exe fib.il
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
For an image that `msil-ld` bound to a .NET 8 or 10 reference pack (M21), pass the pack as
`--runtime-dir` and `--skip-peverify "<reason>"`: PEVerify cannot load such an image, and
`msil-verify.py` then prints `PEVerify skipped: <reason>` and verifies with ILVerify alone.
For a program that links libraries (M22), pass them with `--dep <file>... --` or
`--dep-dir <directory>` (to `msil-jitall.py` too), and add `--also-verify-deps` to verify
each managed file of the `--dep-dir` directories with both passes as well (`msil-verify: also
verified a.dll, b.dll`).

### Read and rewrite an assembly: `llvm-cil-dump` and `llvm-cil-copy`

The ECMA-335 library of the fork (`llvm/lib/CLI`, M16 and M17; Status.md section 2) comes with
two tools (`ninja -C build llvm-cil-dump llvm-cil-copy`; both are on the build-target list).
`llvm-cil-dump` reads the PE images of .NET assemblies and netmodules, whoever wrote them, and
prints them in one or more modes, in this order whatever the order of the options:

```sh
llvm-cil-dump fib.exe                # = --canonical: canonical listing v1
llvm-cil-dump --headers fib.exe      # PE, COFF, CLI and metadata headers, streams, MVID
llvm-cil-dump --tables fib.exe       # every row of every metadata table, columns in ECMA order
llvm-cil-dump --link-info main.o     # a CIL object's link-info record (clang -c, section 3)
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

`llvm-cil-copy <input> -o <output>` reads an IL-only image and writes it again through
the library: every row is rebuilt with `MetadataBuilder` (table order kept, tokens renumbered),
every method body re-encoded with its tokens rewritten, and the image written by the PE writer
that `msil-ld` uses (M20), in the envelope of the C# compiler's images. Duplicate TypeRef
and MemberRef rows, which ilasm never writes, become one row. The kind and the machine come
from the input: an image with an entry point is copied as an executable, one with an Assembly
row as a library, anything else as a netmodule, and the machine (x86_64 or ARM64, or I386 for an
AnyCPU PE32 image, written in msil64-unknown-dotnet's stubless PE32 envelope since P-1a) is kept. So a
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
- a PE32 image that is not AnyCPU (machine I386 with `32BITREQUIRED` or `32BITPREFERRED`):
  `cannot write PE image: machine 0x014C is written only as an AnyCPU image (CorFlags ILONLY
  without 32BITREQUIRED and 32BITPREFERRED), not with CorFlags 0x<8X>`; ilasm's default AnyCPU
  PE32 image is copied, in msil64-unknown-dotnet's stubless PE32 envelope (P-1a).

## 7. Run the tests

| Suite | Where | What |
|---|---|---|
| Unit tests | `llvm/unittests/Target/MSIL`, plus the MSIL cases of `TargetParserTests` and `MCTests`, and `llvm/unittests/CLI` (`CLITests`, all of it) | type mapping, name quoting, stack effects, target machine, streamer (with both spellings of every operand class since M18), the MC object hooks (`MSILMCObjectHooks.*`), the standalone import table and the linked import mode (`MSILImportsTest`, M15), the binary CIL of M18 (42 tests: the 219 CIL records against `CILOpcodes.def` and the shortest forms against the IL builder, `MSILCILRecords`; the BCL list, `MSILBCLMembers`; the choice of branch forms, `MSILBranchForms`; the bytes, sizes and fixups of every record, `MSILMCCodeEmitter`; the fixup kinds and branch resolution of the asm backend, `MSILAsmBackend`), triples and their compatibility, target selection; `CLITests` (22 suites, 461 tests since P-1a, 454 at M26, P-1a's 7 new in `CLIPEWriterTest`, `CLITrailerTest` and `CLIImageLayoutTest`; the gates' floors: at least 461 tests since P-1a (441 before), at least 19 of `CLISymbolTableTest`, at least 5 of `CLITrailerDefsTest` and, since M26, at least 47 of `CLILinkInfoTest`, 23 of `CLICallAdapterTest` and 22 of `CLITrailerTest`): the ECMA-335 format headers, the metadata table schema and the link-info record (suites `CLIFormatTest`, `CLITableSchemaTest`, `CLIMSILLinkTest`, M13), the writer half of the ECMA-335 library (M16): blobs and compressed integers (`CLIBlobTest`), signatures (`CLISignatureTest`), custom-attribute blobs (`CLICustomAttributeTest`), the opcode table (`CLIOpcodesTest`), the IL builder with its short forms, branch relaxation and `.maxstack` (`CLIILBuilderTest`), the IL decoder and token rewriter (`CLIILDecoderTest`), method bodies (`CLIMethodBodyTest`), the metadata heaps (`CLIHeapsTest`) and `MetadataBuilder` (`CLIMetadataBuilderTest`), and its reader half and codecs (M17): the PE writer with its envelope per image kind, COFF symbol view and content ID (`CLIPEWriterTest`), the PE image reader (`CLIPEImageTest`), the metadata reader with every malformation and wide indexes (`CLIMetadataReaderTest`), a writer-reader round trip with canonical listings (`CLIRoundTripTest`), the link-info record's codec, checks, validation, COFF view and printer (`CLILinkInfoTest`) and the symbol-table attribute (`CLISymbolTableTest`, which M22 extended with the INTERFACE flag and the ABI descriptors of library functions), and the link model of M20: the data image, section classes and constructor order (`CLIImageLayoutTest`), the module initializer with the relocation loop, the data-import helper and `'<msil>Main'`, run in a small CIL interpreter (`CLITrailerTest`, to which M24 added the trailer definitions' place and flags), and the call adapters of plan 5.9.8 (`CLICallAdapterTest`); and since M24 the registry of trailer definitions, `TrailerDefs.def`: its rows, names, scopes and signatures and the compiler barrier's method through a recording sink (`CLITrailerDefsTest`, 10 tests) |
| llc and MIR tests | `llvm/test/CodeGen/MSIL` | the emitted IL, FileCheck'ed on both x86_64 triples (and the aarch64 ones where the ABI differs; since M18 in the shortest forms and with `nomangle`), and assembled with ilasm when it is available; the pass pipeline and the MIR at llc's stop points; the linker options in the bitcode symbol table of MSIL modules (`irsymtab-linker-options.ll`); the module flags `msil-assembly-name` and `msil-lto` that `msil-ld` sets (`lto-module-flags.ll`); the linked import mode, `msil-linked`, with its `assembly` P/Invokes and errors, against the standalone table (`imports-linked.ll`, M15); M20's whole-program sections (`sections-whole-program.ll`: section groups, `__start_`/`__stop_`, constructor-table entries in order with `llvm.global_ctors`, and the texts of the sections that stay errors, on both triples; `section-errors.ll`), the relocation loop at several thresholds (`reloc-loop-threshold.ll`), the call adapters of `msil-lto` with their warnings and errors (`function-bitcasts-lto.ll`) and the data imports of linked text (`imports-linked-data.ll`); M22's inline asm whose template holds only comments, which gives the IL of an empty template, and since M23 line and unterminated comments, which stay errors (`asm-comment-barrier.ll`); M23's constructors that take `argc`, `argv` and `envp` in whole-program text, on glibc and musl, and their error on the Windows triples (`ctor-envp-text.ll`); M24's trailer definition of the compiler barrier, defined once in whole-program text and an undefined symbol in objects, with its type-mismatch errors (`trailer-defs.ll`), `__bf16` parameters and results, and each lane of a `bfloat` vector, as `int32` (`bf16-signatures.ll`) and the strict conversions between floating point and `__int128` as their plain forms (`strict-i128.ll`); M25's texts for runtime helpers in standalone and linked text, `needs the helper archive clang_rt.builtins` for compiler-runtime and archive-only helpers, `is an atomic operation` for `__atomic_*` and `has no 128-bit integer helpers` for the `__int128` nodes, with the objects that list each helper as an undefined symbol (`helper-archive-errors.ll`), and the empty `llvm.clear_cache` (`clear-cache.ll`); and the CIL objects of `llc -filetype=obj` (M19: the 12 `object-*.ll` tests read them with `llvm-cil-dump --headers`, `--tables`, `--canonical` and `--link-info`, `llvm-nm` and `llvm-ar`: the envelope, every kind of symbol and the data model, aliases, data segments and relocations, COMDAT groups, constructors, calls with the K&R case in both orders, import stubs, the errors of object mode, the COFF view, the matrix case `p7-flags`, and the object-form text, which most of them also assemble with ilasm and compare with the object; they require the lit feature `llvm-cil-dump`, which the directory's `lit.local.cfg` sets when `<build>/bin` has the tool, so `check-llvm` and `check-llvm-codegen-msil`, which do not build it, report them UNSUPPORTED on a tree without it, and under `msil_require_verify=1` a missing tool is a configuration error); since M26 the variadic tests (`variadic-def.ll`, `variadic-call.ll` with the `va_list` slots per triple, `variadic-fixed-arity.ll` with a MemberRef per call site and FIXED_ARITY, `variadic-errors.ll`), `pipeline-knr-variadic.ll`, `dynamic-alloca.ll` (`LOCALLOC_I64`, realignment, deleted `stacksave`/`stackrestore`) and `ucrt-stdio-options.ll` (the UCRT stdio options' initializer in standalone whole-program text: the default bits on windows-msvc at -O0 and -O2, the bits of the `msil-ucrt-printf-bits` and `msil-ucrt-scanf-bits` module flags, nothing on linux-gnu or windows-gnu), with `llvm/test/Transforms/ExpandVariadics/msil-abi.ll` for the MSIL ABIs of ExpandVariadics; P-1a added `portable-basic.ll`, `portable-errors.ll`, `portable-libcalls.ll`, `portable-sections.ll`, `portable-triples.ll` and `portable-varargs.ll` for msil64-unknown-dotnet (and `msil-abi.ll`'s portable RUN line) |
| MC tests | `llvm/test/MC/MSIL` | 9 tests (M18) of `llc -show-mc-encoding` and `-M object-form` on both x86_64 triples: the bytes and fixups of every operand kind (`encoding-operands.ll`), branches across exactly 127 and 128 bytes forward and -128 and -129 backward (`encoding-branches.ll`), `switch` entries and the `br` after them (`encoding-switch.ll`), the `.cctor` and an `argc`/`argv` `'<msil>Main'` (`encoding-synthesized.ll`), the shortest forms of constants, offsets and indices (`shortest-forms.ll`), the BCL members in both spellings (`bcl-members.ll`), every operand class in both spellings (`object-form-operands.ll`), `nomangle` on four triples with the ImplMap flags 0x0201 read back with `llvm-cil-dump --tables` from ilasm's image (`pinvoke-nomangle.ll`), and the clean error of `llvm-mc` for MSIL input (`llvm-mc-unsupported.s`). The directory's `lit.local.cfg` loads `llvm/test/CodeGen/MSIL`'s for `ilasm` and `%ilasm-verify` and adds the feature `llvm-cil-dump`, whose absence under `msil_require_verify=1` is a configuration error. In `check-msil` and the `net8` leg |
| clang tests | `clang/test/CodeGen/MSIL`, `clang/test/Preprocessor/init-msil.c` and `init-msil-portable.c`, `clang/test/Driver/msil-toolchain.c`, `msil-dotnet.c`, `msil-link.c`, `msil-lto.c`, `msil-header-search.c`, `msil-object.c` and `msil-shared.c`, `clang/test/Sema/msil-long-double-format.c` | the `%L` format diagnostics per triple (M26: a warning for the `printf` family, an error for `scanf`, none on windows-msvc); unnamed over-aligned aggregates passed to `...` as `byval` with their alignment on the x86_64 SysV MSIL triples (`variadic-overaligned.c`, M26); the clang wrapper, predefines, driver errors (the ELF-only section options among them), `-mllvm` options, `-cc1as`, the import attributes and `#pragma comment(lib)`, Design.md 12's literal command, `__bf16` scalars and GNU vectors in signatures, which compile to the IL of their `_Float16` twins byte for byte (`bf16-signatures.c`, M24), the 64-bit lock-free cap (`atomic-lock-free.c`, M25: `__atomic_always_lock_free(16, 0)` false, 16-byte `_Atomic` operations as calls of `__atomic_load` and `__atomic_store`, native layouts, no `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16`; `init-msil.c` pins `__ELF__` on the Linux triples), `cc1 -emit-obj` and the headers and record of the objects of `-c` and clang-cl `/c` (`emit-obj.c`, M19; it requires the feature `llvm-cil-dump`, which the directory gets from `llvm/test/CodeGen/MSIL/lit.local.cfg`, and is UNSUPPORTED without the tool); the MSIL toolchain (M14): `-flto -c` as bitcode, the `msil-ld` command line, the default libraries (with `--ucrtbase-imports` on windows-msvc since M15, and since M25 the helper archive after them, with `-nostdlib`, `-nodefaultlibs` and `-nolibc`), `-fuse-ld` and `--ld-path=`, the LTO options (since M20 only with `-flto`) and the cc1 lines (`-###` only); `-c` and clang-cl `/c` as CIL objects (`msil-object.c`, M19; since M20 also the compiles of a command that links, `cc1 -emit-obj`): their cc1 job and errors, and, compiled for real, their bytes from two directories, `llvm-nm` letters and object-form text; `-shared` on the four x86_64 MSIL triples and clang-cl's `/LD` and `/LDd` as `msil-ld --shared`, with the output names and `-Wl,--export-class=` (`msil-shared.c`, M22, `-###` only) |
| `msil-ld` tests | `lld/test/MSIL` | 131 tests (by prefix 29 `obj-`, 23 `native-`, 17 `bcl-`, 16 `shared-`, 9 `portable-` and 37 others; P-1a added the nine `portable-` tests of msil64-unknown-dotnet's link rules (Status.md section 2, "The portable target msil64-unknown-dotnet (P-1a)"); M26 added `obj-variadic.test` (variadic definitions and references across objects, archives and LTO, and the `function signature mismatch` and fixed-arity errors), `shared-variadic.test` (variadic exports of a `-shared` library), `ucrt-stdio-options.test` (the stdio option bits through objects and LTO, and the `legacy_stdio_*` names) and `variadic-tables.test` (every `va_list` variant through `--print-imports`), and rewrote `native-variadic.test` (variants, the fixed-arity list and the link errors of native variadic functions); M25 added `helper-report.test`, the helper archive in the undefined-symbol report: both forms of the compiler-runtime line, the `_Complex` helpers that never bind natively and their signature error, and the private members of a `-shared` library, and changed `never-bind.test`, `libcall.test` and `strict-i128-lto.test` for the new texts, `native-aggregate.test` for the `long double` error and a forwarder member's precedence over `libm.so.6` in either order, and `trailer-defs.test` for a program's bitcode definition of the barrier in an LTO link; M24 added `never-bind.test`, the four name classes that never bind to a native library with their report lines and the `setjmp` family's error, `helper-mismatch.test` and `helper-mismatch-lto.test`, the signature errors of helper calls, `trailer-defs.test`, the `<trailer>` input, `lto-o0-discardable.test`, `-O0` LTO links that compile no unused copy of a header's inline function (such as the UCRT's `sprintf`), while a program that calls `sprintf` keeps the variadic-definition error, and `strict-i128-lto.test`, the strict conversions between floating point and `__int128` in LTO's code generation, which an image link and `--lto-emit-asm` compile inline on three triples without a compiler-rt helper): which program names and `-flavor` values reach `msil-ld` (`dispatch.test`) and that `lld-link` and `-flavor link` still reach the COFF flavor (`dispatch-coff.test`), its options and their errors, symbol resolution in both input orders (weak, common, COMDAT, archives, `--whole-archive`, duplicates, undefined and mismatched symbols), the triple checks and the stock-bitcode warning, library search, the LTO options and `--lto-emit-asm` text, the map file, `--why-extract`, `--reproduce`, `--trace` and response files (M14); and the 21 `native-*.test` of M15: each native-library reader, the `-l` rules, scripts under `--sysroot`, binding and its order, the linker options of bitcode, `--ucrtbase-imports`, `--print-imports`, and every error of section 8 that names a native symbol; the 25 `obj-*.test` of M20, which link CIL objects that `llc -filetype=obj` writes (and a few that ilasm assembles from object-form text edited by hand, for what llc cannot write) and read the image with `llvm-cil-dump --canonical`, `--headers` and `--tables`: rows, the entry point, `[mscorlib]` and names (`obj-basic`), every token class (`obj-tokens`), resolution in both orders with aliases and LOCAL renaming (`obj-resolve`), common symbols, COMDAT groups and archives, every signature text (`obj-signatures`), weak and undefined references, symbol type mismatches, sections, `__start_`/`__stop_` and constructors on both triples (`obj-sections`, `obj-ctors`, `obj-sections-unsupported`), `--gc-sections`, the relocation loop, spill classes (`obj-spill`, with `--msil-type-member-limit`), the map, the triple and data-model checks, linker options, `-e`, P/Invokes and their checks (`obj-pinvoke`), determinism, LTO with ThinLTO, partitions and `--save-temps` (`obj-lto`), and data imports with both helpers (`obj-data-import`); M20 updated the tests that pinned the texts it retired and deleted `lto-object-output-error.test`; and the 17 `bcl-*.test` of M21, which bind such objects against the synthetic reference assemblies of `Inputs/fakepack/` (ilasm sources with their `yaml2obj` images; `python lld/test/MSIL/Inputs/fakepack/gen.py [--ilasm PATH]` assembles each source with ilasm and writes its YAML, and with `--check --yaml2obj PATH --llvm-cil-dump PATH` also requires the `yaml2obj` copy of each to list like ilasm's image under `--tables` and `--headers`): `--reference` and positional references (and .NET assemblies in archives), identities (the same identity with other bytes an error), the core-library rule (three core libraries and references that add no assembly included), types (three definitions included), forwarders, members (with their calling conventions in the texts, and the base-type search: constructors, a nearer non-public member, and base types that are missing or defined twice), fields (looked up on the requested type only, through hand-written objects: a base type's public field never binds, and the requested type's non-public fields of that name are listed), custom modifiers, the AssemblyRef rows and their order, the built-in `[mscorlib]` table, request strengths through `--print-bcl-binding`, `System.Type` names and their rewriting in attribute blobs (`bcl-attribute`, through `attr:` requests), the `NativeLibrary` data-import helper, `--lto-emit-asm` (with its data-import error in a link with references) and `--reproduce`, plus `obj-ctors-null.test` (a null `.CRT$XCU` slot skipped, a null `.init_array` slot an error) and `start-stop-hidden.test` (hidden `__start_`/`__stop_` data references alike in all three link modes; `undefined.test` gained the `--lto-emit-asm` cases of hidden and protected `__start_`/`__stop_` references with and without their section); M21 updated the tests that pinned the texts it retired (`.NET assembly inputs are not supported yet`, `needs --reference`); and the 15 `shared-*.test` of M22, which build libraries with `msil-ld -shared` (and `--interface-only`) and link objects and libraries against them: the layout and the export class, export selection, the symbol table's golden bytes and `--link-info` lines, both initializers and an EXE's one, a library's `'<msil>Data'` class with spill classes and a native data import (`shared-data-class`), MemberRefs, the accessor rewrite and cross-assembly relocations, the checks of a library input against images that `python lld/test/MSIL/Inputs/shared/gen.py` patches (other triples, ABI versions, data models, table versions, C runtimes, malformed tables, a table without an export class), `-z defs`, signatures, duplicates (a library named like the output among them), interface libraries and a cyclic pair, `-l`, LTO, a library's AssemblyRefs against the fake pack and 1,000 exports, with `lto-code-model.test`, `native-object-inputs.test` (a referenced definition in a command-line native object stays the error when a library also defines the name, and an oldnames-style alias object binds through its alias), `obj-ctors-args.test` and `obj-ctors-main-args.test` (constructors that take arguments share `main`'s `argc` and `argv`) for the defects of the Rust experiment (Status.md section 2, "Rust bitcode"); M22 updated `options.test` (also a link of libraries alone without `--target`), `bcl-reference-option.test` (a library of the fake pack is now a library input, and `--print-bcl-table` has six more entries) and `obj-ctors-null.test`, so no test pins `-shared is not supported yet` or `linking .NET libraries built by this toolchain is not supported yet`; and M23's `output-name-netcore.test` (the warning for a `.exe` bound to .NET's core library, over images of `System.Runtime`, `System.Private.CoreLib` and `netstandard` and the fake pack's `mscorlib`) and `native-alternatename.test` (`/alternatename` over a definition in native machine code, an object or an archive member, in both orders and all three link modes), with a null `llvm.global_ctors` entry in `obj-ctors-null.test`, a library against bitcode of another `wchar_t` size in `shared-checks.test`, and the exact printed lines and option error in `shared-symbol-table.test`, `shared-interface.test` and `shared-zdefs.test`. Inputs come from the tree's `llc`, `llvm-as`, `opt -module-summary`, `llvm-ar`, `llvm-lib /def:` and `llvm-dlltool`, and for the native libraries `yaml2obj`, `llvm-mc` (the tests that need it require the X86 target), `ld.lld -shared` and `lld-link -dll`. In `check-msil` and the `net8` leg; the milestone gates also run them with `--param RUN_LLD_MAIN_TWICE=1` (`LLD_IN_TEST=2`: every link runs twice in one process, as for the other lld flavors; `dispatch-coff.test` is unsupported there, because the COFF flavor's `no input files` exits from the silenced first run) |
| ECMA-335 tool tests | `llvm/test/tools/llvm-cil-dump`, `llvm/test/tools/llvm-cil-copy` | 14 tests (M17; 8 of `llvm-cil-dump`, 6 of `llvm-cil-copy`) on `yaml2obj` copies of images that ilasm wrote from IL kept in each test: `llvm-cil-dump`'s `--headers` (PE32+, PE32, and a CIL object with its marker sections and COFF symbols), `--tables`, `--canonical` with every option and normalization, `--stats` and `--stats-only` alone and next to `--headers` and `--tables`, the details of canonical listing v1 that plan section 12's row '5.7.10 clarified (canonical listing v1 ...)' settles (`canonical-edges.test`: `.implements` and `.try` clauses in table order, sort keys on raw names, static properties and `.other`, what the listing leaves out, a `switch` without targets, a branch target before the code, data past a section's raw data, no `entrypoint` line for a native entry point, the limit of 64 type constructors across TypeSpecs, and the errors for an entry point that is not a MethodDef, an RVA outside every section and deeper types), `--link-info` on a hand-encoded record, on a library's symbol tables and on an image that carries both, several files and the errors of damaged images; `llvm-cil-copy` on an assembly with rows in every table the writer supports and on a CIL object (the same listing and record, the same bytes from two directories, and no COFF view in the copy), the kind and machine it takes from the input (`copy-kind.test`: a library, a DLL with an entry point that becomes an executable, an ARM64 image kept ARM64, an AnyCPU PE32 image copied in msil64-unknown-dotnet's stubless PE32 envelope since P-1a, and PE32 images with `32BITREQUIRED` or `32BITPREFERRED` refused), duplicate TypeRef and MemberRef rows merged into one (`copy-duplicates.test`), and its refusals (unsupported tables, data of unknown size, a lone surrogate in a user string, a base type or enclosing type in a later row and the other refusals of section 6). `check-msil` builds both tools and runs these tests, and so does the `net8` leg. To run them alone (`build/bin/llvm-lit -sv llvm/test/tools/llvm-cil-dump`), build the tools first with `ninja -C build llvm-cil-dump llvm-cil-copy`: `check-llvm` and `check-llvm-tools-llvm-cil-dump` (or `-copy`) do not build them, and each directory's `lit.local.cfg` reports its tests UNSUPPORTED while they are missing |
| Execution tests | `msil/test/M1/<NN-area>/`, `msil/test/M2/<NN-area>/`, `msil/test/M3/<NN-area>/`, `msil/test/Programs/` | C and IR programs compiled at `-O0` and `-O2` (and in the `more-levels` leg at the other levels; see below), linked by `msil-ld` from CIL objects (since M23 by default; with `-S` and ilasm under `--param msil_pipeline=asm`), verified, force-JITted, run and compared with checked-in transcripts and with a native build (the *native oracle*, below); `M1/11-programs/` is the Design.md 12 program corpus. `M2/` (M14) holds programs of several files, archives and stock-clang bitcode that `msil/utils/msil-prog.py` builds through clang and `msil-ld` (since M20 from CIL objects into `msil-ld`'s image, or through LTO with `--param msil_pipeline=lto`), the LTO images of `M2/02-lto/` (M20), programs that call native libraries and read their variables (`M2/03-native/`, M15 and M20), libraries built with `msil-ld -shared` and the programs, libraries and C# programs that link against them (`M2/04-shared/`, M22: a library and its program, cycles of two and three libraries through interface libraries, initialization order, shared C library state, a missing library and a missing member, determinism, the ildasm round trip, `-l` and a csc-compiled consumer), reference binding against the real reference packs (`M2/05-refs/`, M21: the built-in table proven on every runtime by `mscorlib-safe.test`, the AssemblyRef rows of each pack and where the images run, positional references, the core-library errors and the `NativeLibrary` data-import helper), determinism (`M2/06-det/`), the explicit equivalence checks O3 and O4 (`M2/07-equiv/`, M20), the link-semantics matrix (`M2/08-semantics/`, Status.md section 2) and the compile-only hosted tests (`M2/09-hosted/`, M15; the `hosted` leg below); since M23 the matrix's P3 rows and `matrix.txt` with `matrix-coverage.test`, `04-shared/struct-abi` and `05-refs/netcore-dll-name`, and `Programs/`: the nine Phase 2 exit programs, their determinism tests in `Programs/det/` and c-testsuite's 150 libc-free tests in `Programs/c-testsuite/`, built from the corpus cache (below, "The exit corpus"). `M3/` (M24) holds the execution tests of Phase 3's features, one directory per area, all of them hosted (below, "Hosted mode"): `10-hosted` (seven programs that call the C library through its real headers), `08-atomics/barrier-spin.test` (the compiler barrier at run time) and `03-helpers`: `bitint-abi.test` (`_BitInt(N)` across files, M24) and M25's ten tests of the helper archive (compiler-runtime helpers, `_Float16` and `__bf16` conversions, Annex G complex arithmetic, `__builtin_powi`, the `long double` forwarders, the archive's members and their privacy; msil/test/README.md lists them) |
| Harness self-tests | `msil/test/harness/` | 154 tests (115 at the Phase 2 exit; P-1a added `portable/lit-config.test`, lit's configuration for `msil_triple=msil64-unknown-dotnet` and its errors, `portable/x86-runner.test`, the 32-bit starts and `msil-run.py --runtime-major` and `--set-32bitreq`, `mscorlib-safe.test`, `check-mscorlib-safe.py`'s accessibility probe, `legs/portable-tag.test` and `legs/portable-tag-sh.test`, the `Portable` legs, `portable/clibrary-names.test`, `msil/utils/gen-clibrary-names.py --check` of `lld/MSIL/CLibraryNames.def` with its stale, extra and unsorted cases, and `portable/update-expected.test`, `portable/skip-lines.test` and `portable/corpus-o1.test`, the portable branches of `update-expected.py`, of the skip lines of `msil-equiv.py` and `msil-prog.py` and of `cli-corpus-check.py`'s O1 envelope check; M26 added `gtest-vsenv.test`, `msil-gtest.ps1` under lit trusting the developer environment that lit passes on, `linux-syntax-check.test`, `msil/utils/ci/linux-syntax-check.py` on a passing and a failing translation unit, and `legs/p3-tests-stop.test`, the legs' stop when `check-matrix.py --p3-tests` fails, and changed `ctest-lists.test` (`libc.txt` required) and the `legs/` tests for its legs; M25 added `linux-rtlib.test`, the native compiler-rt archive of E7h through `msil-prog.py --linux-rtlib`, `update-expected.py --linux-rtlib` and lit's `msil_linux_rtlib`, and lit's notes on the MSIL triple's helper archive (without it, a test whose program calls a runtime helper is a FAIL with `msil-ld`'s undefined-symbol report and its compiler-runtime line, never UNSUPPORTED), `build-builtins.test`, `build-builtins.py`'s self-test, lists, `--check` and usage errors, `legs/ci-build-builtins.test` and `legs/ci-build-builtins-sh.test`, the `builtins` row that `msil/utils/ci/build.ps1` and `build.sh` add after ninja (its PASS and FAIL rows, the `--build-dir` argument, exit status 1 after a FAIL row, and no such row after a failed ninja), and extended `det-manifest.test` (the helper archives in the manifest), `equiv/skip.test` (the per-test `skip O4` lines of `03-helpers`: O4 skipped for `complex-matrix.test` on both default triples and for `powi.test` on the Linux triple only, never for `bitint-abi.test`, and no O3 or O6 skip in `03-helpers` or O3, O4 or O6 skip in another `M3` directory), `linux-hosted-oracle.test` and `prog/update-expected.test` (a missing path is a usage error); M24 added `hosted-mode.test`, `windows-hosted-oracle.test` and `linux-hosted-oracle.test` for hosted mode and its oracles, `expected-rule.test`, `loud-composite.test`, `loud-failfast.test` and `netfx-skip.test` for `%{msil_expected_rule}`, `%{msil_prog_loud_<L>}`, `[failfast]` and `%{msil_netfx_skip}`, `limits-redefine.test`, `probe-cache.test` and `steps-equivalence.test` for `%{msil_limits}`, the probe cache and `msil-steps.py`, `update-expected-levels.test` for `update-expected.py --levels`, `update-expected-walk.test` for its skip of the harness self-tests and its single processing of nested and repeated arguments, and eleven tests in `legs/` for `-Describe`, the `-Exit` tag, `lit-retry.py` and the timeout retry, `calibrate.py`, `gate-select.py` and the `matrix-legs` row of `run-m1-gate.ps1 -Exit` (`exit-matrix-legs.test`); M23 added `default-pipeline.test`, `also-run.test`, `corpus-param.test`, `copts-expect.test`, `det-manifest.test` and `prog/update-expected-m23.test`, for the default `obj` pipeline, `msil_also_run` (with `%{msil_also_run_check}` for tests with explicit RUN lines), the corpus parameters, `%{msil_copts}` and `%{msil_expect}`, `det-manifest.py` and `update-expected.py --corpus-dir`, and in its fix loop `ctest-lists.test`, for c-testsuite's list errors and its discovery, `legs/ci-legs-corpus.test` and `legs/ci-legs-corpus-sh.test`, for the CI corpus paths of `lit-legs.ps1` and `lit-legs.sh`: the `ctest` leg's SKIP row under `MSIL_CORPUS_SKIP`, the corpus legs' parameters and the removed legs, and `corpus-fetch.test`, for `fetch.py --self-test`): the runner, verifier and JIT checker themselves (among them the renumbering of ildasm's `I_<rva>` data labels in `msil-verify.py --roundtrip`, `legs/roundtrip-data-labels.test`, M17, its acceptance of ilasm's quieted float32 signaling NaNs, `prog/roundtrip-snan.test`, M21, and of a library's `'<msil>SymbolTable'` class at another position, `prog/roundtrip-moved-class.test`, M22, and `--skip-peverify`, `skip-peverify.test`, M21), `update-expected.py`, the E7 Linux oracle, the dependencies that `--dep` ships with a program (`dep-local.test`, `dep-wsl.test`, M15), the comparison of `il-snapshot.py` (`il-snapshot.test`, M15; below), the canonical listing of `%msil_canon` (`canon-listing.test`, M16, and from M17 on `llvm-cil-dump`) and its agreement with `mdcanon` (`canon-agree.test`, M17; below), and lit's configuration errors (bad `msil_*` parameters, and under `msil_require_verify=1` a missing verifier or lister, stop lit with exit status 2); `prog/` pins the expansions of `%{msil_prog}` and `%{msil_c}` (with `%{msil_native_libs}` and `%{msil_deps}` since M15) and the commands of `msil-prog.py` with stand-in tools, since M20 `%{msil_prog}` under `asm` as the `obj` pipeline's and O4 under `lto`, and since M21 the `skip O6` lines (`o6-skip.test`); `ref-flags.test` (M21) checks that `msil_references` reaches the MSIL side of every link and never the native oracle; M22's `shlibs.test` and `prog/shlibs-commands.test` pin how `%{msil_shlibs}` builds libraries and interfaces before the program, in MSIL mode and natively on both OSes, and pass them to verification, jitall and the runs, `csc.test` pins `%msil_csc`, and `verify-data-cctor.test` the RVA note of a library's `'<msil>Data'::.cctor`; `pipeline-obj/` (M20) pins every expansion of the `obj` pipeline; `equiv/` (M20) checks `msil-equiv.py`, each entry of `equiv-allowed.txt` and O6 (below, "The equivalence oracles") |
| Upstream regression tests | the upstream lit tests and unit tests named in `msil/utils/upstream-hooks.txt` | the upstream behaviour of every upstream file the fork changes (the upstream-hooks check, below) |

```sh
ninja -C build check-msil                 # every MSIL lit test (lean dependencies)
ninja -C build check-msil-m1-11-programs  # one directory (check-msil-m1-<dir>, check-msil-m2-<dir>, check-msil-m3-<dir>)
build/bin/llvm-lit -sv msil/test/M1/03-control llvm/test/CodeGen/MSIL/switch.ll   # llvm-lit.py on Windows
build/bin/llvm-lit -sv msil/test/M3/10-hosted      # hosted tests (below, "Hosted mode")
build/bin/llvm-lit -sv msil/test/Programs/zlib     # an exit program, after fetch.py (below, "The exit corpus")
build/bin/llvm-lit -sv --param msil_pipeline=asm msil/test/M1/00-smoke   # the -S and ilasm route
build/unittests/Target/MSIL/MSILTests     # after 'ninja -C build MSILTests'
```

The per-directory targets are created when CMake runs, so a directory added since the last
configure gets its target after the next one; `llvm-lit` with the path works at once.

On the Windows host, `msil/utils/build/msil-lit.ps1 -LitArgs "-sv <params>" <paths>` runs lit
under the shared build lock. Through `pwsh -File`, a value that starts with `-` and contains a
`:` (a drive path, as in `--param msil_ilasm=D:/...`) is read as `-Name:value` syntax and the
call fails with "Missing an argument for parameter 'LitArgs'": write `-LitArgs:"-sv ..."` or
start the value with a blank (`-LitArgs " -sv ..."`).

**The default pipeline.** Since M23 the execution tests build through CIL objects unless told
otherwise (plan Q31): with no `--param msil_pipeline`, `%{msil_c}` compiles its one C file with
`clang -c` (`%{msil_ll}` its IR file with `llc -filetype=obj`), `%{msil_prog}` every source of a
program, and `msil-ld` links them into its image, which is verified, force-JIT-compiled and run
(`msil_pipeline=obj`, `msil_link_output=pe`). `--param msil_pipeline=asm` builds the one-file
tests with `clang -S` (or llc) and ilasm instead, the Phase 1 route that the `asm-oracle` leg
keeps, and `--param msil_pipeline=lto` builds every C test through `clang -flto -c` and
`msil-ld`'s LTO (`%{msil_ll}` stays on llc text there). `%{msil_prog}` uses objects under
`asm` too, because there is no text link. Execution tests therefore need clang with the MSIL
target, `msil-ld`, the tree's `llvm-ar` and a .NET runtime (and ilasm for the `asm` route and
the oracles that compare with it): without `msil-ld` or `llvm-ar` the default pipeline makes
them UNSUPPORTED with a note, while an explicit `msil_pipeline=obj` or `lto`, and
`msil_require_link=1`, make that a configuration error. The compile-only tests of
`msil/test/M2/09-hosted` need only clang with the MSIL target, plus a glibc sysroot (feature
`msil-glibc-sysroot`) or the UCRT headers on `INCLUDE` (`msil-ucrt-headers`), and `msil-ld` for
the two that link. `msil/test/Programs` also needs the exit corpus (below, "The exit corpus").

**More runtimes from one build.** `--param msil_also_run=<entry>[,<entry>...]` (M23) makes every
composite repeat jitall and the run of the image it already verified, once per entry, against
the same expected file: an entry `<major>` runs on Microsoft.NETCore.App `<major>.x` of the
runner (with its own runtimeconfig.json), and `<major>+tc0` adds `DOTNET_TieredCompilation=0`
(the optimizing JIT, plan D38). `--param msil_also_run=10,8+tc0` on a .NET 8 leg thus also runs
every program on .NET 10 and on .NET 8 without tiering; the transcripts are
`<test>.tmp.<L>.also-net<major>[-tc0].out`. An entry that is neither form, an entry named twice
and, under `msil_require_exec=1`, a runtime that the runner lacks are configuration errors. The
.NET Framework run stays `msil_run_netfx=1`. A test with explicit RUN lines gets the same runs by
REDEFINing `%{msil_also_run_image}` to its image and appending `%{msil_also_run_check}` after
its main FileCheck, as the three trap tests of `msil/test/M1` do (msil/test/README.md).

**.NET 10 by default.** Since M24 the local runner runs programs on .NET 10 when no
`--param msil_dotnet_runtime=<major>` names another runtime (.NET 8 until M23; WSL always ran
.NET 10), so a plain `llvm-lit` run tests the primary modern runtime. The legs that run on .NET
8 say so: `net8`, `lto`, `programs`, `programs-lto` and `lto-m1` through *Windows run* below,
and `more-levels`, `asm-oracle`, `det` and `refs-net8` with `msil_dotnet_runtime=8`; `ctest` and
the WSL legs `wsl-net10`, `lto-wsl`, `programs-wsl`, `programs-lto-wsl` and `ctest-wsl` run .NET 8
as an `msil_also_run` entry.

**Hosted mode (M24).** Phase 1 and 2 tests are freestanding: `%{msil_cflags}` has
`-ffreestanding`, and they call only the functions of D14's table or their own native
libraries. A directory whose `lit.local.cfg` (or that of a directory above it) holds the line
`config.msil_enable_hosted(config)` is hosted (plan 5.17.3; every directory of `msil/test/M3` is):
its tests compile with the triple's real C library headers (`%{msil_cflags}` without
`-ffreestanding`, plus `%{msil_hosted_cflags}`, which is `--sysroot=<glibc sysroot>` on the
Linux triple and empty on Windows, where the UCRT headers come from `INCLUDE`), and their native
oracle is the hosted one (`%{msil_oracle_flags}` is `--hosted`): on Windows a build against the
DLL C runtime (`-fms-runtime-lib=dll`, so the program exits through the same `ucrtbase.dll` as
the MSIL program), and for the Linux triple under WSL *E7h*, which links glibc's own `crt1.o`,
`crti.o` and `crtn.o` with `-lm -lc` from the glibc sysroot (`clang -fuse-ld=lld -nostartfiles
-nodefaultlibs`, a PIE) instead of E7's `start.c`, so `atexit` handlers and `.fini_array` run as
natively. Since M25 E7h also links native compiler-rt's builtins between `-lm` and `-lc` (E7h has
no libgcc), so the tests of `msil/test/M3/03-helpers` compare the helper archive with
compiler-rt's own results. `--param msil_linux_rtlib=auto|none|PATH` names that archive (feature
`msil-linux-rtlib`, substitution `%{msil_linux_rtlib}`): `auto`, the default, takes
`usr/lib/x86_64-linux-gnu/libclang_rt.builtins-x86_64.a` of the Linux sysroot on Windows
(`make-linux-sysroot.py --builtins` writes it, "Host setup" below), and on a Linux host the file
that `clang -rtlib=compiler-rt -print-libgcc-file-name` names for the native triple, asking the
tree's clang and then the clang on `PATH`. A hosted directory of the Linux triple whose native
oracle runs without it loses the native oracle, with a note, or under
`msil_native_oracle=required` stops lit with a configuration error that says how to build it.
lit also notes, never as a feature, whether the MSIL triple's helper archive exists. The feature
`msil-hosted` holds when the triple's headers exist (the glibc sysroot of
`--param msil_glibc_sysroot`, default `$MSIL_GLIBC_SYSROOT`, else
`C:\llvm-msil-scratch\glibc-sysroot`, for the Linux triple), and `msil-linux-hosted-oracle` when
that sysroot also has E7h's files and `ld.lld` is found. Without `msil-hosted` a hosted directory
is UNSUPPORTED. Under the WSL runner, whose native oracle for the Linux triple is E7, a hosted
directory without `msil-linux-hosted-oracle` only loses the native oracle, with a note, unless
`msil_native_oracle=required` makes it UNSUPPORTED too. `--param msil_require_hosted=1`, which
every lit leg passes, makes each UNSUPPORTED case a configuration error that names what is
missing. `update-expected.py` finds hosted directories by the same line (`gate-select.py
harness` counts every `lit.local.cfg` under `msil/test` as a harness file, so adding the line
selects step G3c). The Linux CI job runs hosted tests natively against the runner's own glibc
(`msil_glibc_sysroot=/`).

**Limits, probes and steps (M24).** Every tool command of the harness takes its limits from the
placeholder `%{msil_limits}`, `--timeout <msil_timeout> --mem-limit-mb <msil_mem_limit_mb>`
(60 s and 4096 MiB by default); a test of a large program REDEFINEs it, and the build steps of
`%msil_prog_build` take ten times the timeout (`--timeout-factor 10`). lit keeps the results of
the probes that start a process (tool versions, runtimes, the native oracle's tools) in a
cache, `--param msil_probe_cache=<file>` (default `<build>/tools/msil/test/probe-cache.json`;
an empty value turns it off), and prints `note: probe cache: hit` or `note: probe cache: miss
(<reason>)`. Each level of a composite is its build followed by one `%msil_steps` command:
`msil/utils/msil-steps.py` runs the remaining steps (verification, jitall, each run, the
oracles, FileCheck) in one Python process and their consecutive WSL runs through one
`wsl.exe`, printing `msil-steps: <step>: <command>` before each step, a command line that runs
by itself, and `msil-steps: <step> failed (exit status <n>)` for the first failure, whose exit
status it returns. lit's `output_limit` is raised so that the log keeps every step's line and
output. M24 measured the composites of `msil/test/M1/03-control` at 47.8 s instead of 76.9 s,
`msil/test/M2/00-link` at 28.8 s instead of 47.7 s and `M1/03-control` in WSL at 51.7 s
instead of 90.7 s.

The execution suite takes lit parameters (all of them are in
[msil/test/README.md](../../../msil/test/README.md)). The phase gate (`run-m1-gate.ps1`, below)
runs it in these legs, those of `msil/utils/ci/lit-legs.ps1` since M23 (plan 5.12.11, with
Phase 3's changes of plan 5.17.6 since M24). Every lit run also passes `--param
msil_require_verify=1 --param msil_require_exec=1 --param msil_require_link=1 --param
msil_require_hosted=1` (the last since M24), which turn a missing verifier, ildasm, jitall,
mdcanon (M16), `llvm-cil-dump` (M17), PEVerify (Windows), ilasm, runtime, MSIL-enabled clang,
`msil-ld`, `llvm-ar`, hosted header set or hosted oracle into an error instead of skipped steps or
UNSUPPORTED tests, and the corpus legs pass `--param msil_require_corpus=1`. *WSL* below stands
for `msil_runner=wsl msil_triple=x86_64-unknown-linux-gnu-msil msil_wsl_dotnet=private
msil_dotnet_runtime=10` (the private .NET 10 in WSL), *E7* for `msil_native_oracle=required
msil_linux_sysroot=<sysroot>` (the E7 Linux oracle; both are described under "Native oracles and
the WSL runtime" below), *E7h* for *E7* plus `msil_glibc_sysroot=<glibc sysroot>` (the glibc
headers and the E7h oracle of hosted mode, below; the WSL legs that run hosted tests take it),
*Windows run* for `msil_dotnet_runtime=8 msil_native_oracle=required msil_run_netfx=1` (.NET 8,
the native oracle with the compiler-rt builtins, and .NET Framework 4.8; the native link and the
default libraries of the MSIL link need `LIB`: use a VS x64 developer prompt, or `msil-lit.ps1`,
which enters that environment), and *corpus* for the parameters of "The exit corpus" below. The
legs marked `-Wsl` run only when `lit-legs.ps1` gets `-Wsl` (or names them with `-Leg`), the
`-Quick` ones only when `-Leg` names them (the steps of the milestone gates, below), the `-Full`
ones only with `-Full`, and the `-Exit` ones (M24) only with `-Exit`, which every post-commit run
and the Phase 3 exit gate pass (an `-Exit` leg whose name ends in `-wsl` also needs `-Wsl`). M24
tagged no leg `-Exit`; M26 tags the first, `ctest-lto`, M28 adds `gc-churn` and `gc-churn-wsl`,
and M30 `stress` and `stress-wsl`. The legs of the tag `Portable` (P-1a) run only with
`-Portable` (`lit-legs.sh --portable`, `run-m1-gate.ps1 -Portable`), which the post-commit runs
pass; they are never `-Exit` legs, so the Phase 3 exit gate does not run them, and each first
builds the portable helper archive (`build-builtins.py --triple msil64-unknown-dotnet`, logged
to `build/lit-results/<leg>.setup.log`; a failure fails the row without a lit run):

| Leg | Paths | Extra `--param`s |
|---|---|---|
| `net8` | `llvm/test/CodeGen/MSIL`, the clang MSIL tests (the driver tests included, and since M26 `clang/test/Sema/msil-long-double-format.c`), `llvm/test/Transforms/ExpandVariadics/msil-abi.ll` (M26), `lld/test/MSIL`, `llvm/test/tools/llvm-cil-dump` and `llvm-cil-copy`, `llvm/test/MC/MSIL`, and `msil/test` (M1, M2, M3 and the harness; lit's `--filter-out=MSIL :: Programs/`, because the corpus legs run `msil/test/Programs`) | `msil_pipeline=obj`, *Windows run*, `msil_equiv=1 msil_also_run=10,8+tc0 msil_scale_tests=1 msil_require_csc=1`: every test through objects on .NET 8, .NET 10, .NET 8 with `DOTNET_TieredCompilation=0` and .NET Framework 4.8, with the equivalence oracles O3 and O6 (below, "The equivalence oracles"), `00-link/spill-limits.test`, which besides this leg only the `-Quick` leg `scale` runs (65,600 functions and globals; its native build alone takes about two minutes), and the C# consumer of `04-shared` with csc required |
| `more-levels` | `msil/test/M1` | `msil_pipeline=obj msil_dotnet_runtime=8 msil_opt_levels=O1,O3,Os,Oz`: the four levels the other legs leave out (they run `-O0` and `-O2`); `.ll` tests that use `%{msil_ll}` run at `-O1` and `-O3` only, because llc has no `-Os` or `-Oz`. Only the three trap tests (`01-int/trap-call-before-div.ll`, `01-int/trap-call-before-rem.ll`, `03-control/trap.ll`) pin `-O0` and `-O2` in every leg: through CIL objects, or under `asm` through llc's text and ilasm. `01-int/calls-arith-debug-info.test` is a `%{msil_prog}` composite of `calls-arith.c` with `-g` (`%{msil_copts}` is `-g`, `%{msil_expect}` is `calls-arith`), so it runs at each leg's `msil_opt_levels` (here `-O1`, `-O3`, `-Os` and `-Oz`) and gets the composite's runs (`msil_also_run`'s runtimes, the native oracle, .NET Framework); under `asm` it adds `clang -S -g` and ilasm at `-O0` and `-O2` |
| `asm-oracle` | `msil/test/M1`, `msil/test/harness/pipeline` | `msil_pipeline=asm msil_dotnet_runtime=8 msil_native_oracle=required msil_roundtrip=1`: the Phase 1 route, `clang -S` (or llc) and the .NET Framework ilasm, against the native oracle, and every module through ildasm and ilasm (Design.md 5.5); the FileCheck directories do not depend on the pipeline, and `msil/test/M2` links objects under `asm` too |
| `modern-ilasm` | `msil/test/M1/11-programs` | `msil_pipeline=asm msil_ilasm=<the NuGet win-x64 ilasm.exe>` |
| `roundtrip` | `msil/test/M1/11-programs`, `msil/test/M2/07-equiv`, `msil/test/M2/04-shared/roundtrip-library.test` | `msil_pipeline=obj msil_roundtrip=1`: oracle O2 on `msil-ld`'s images, through ildasm and ilasm, and the reassembled image runs; `roundtrip-library` round-trips a library and runs a program linked against the copy |
| `expected` | `msil/test/M1`, `msil/test/M2`, `msil/test/M3` | not lit: `update-expected.py --check --levels O0,O2` (the Windows native oracle, with the compiler-rt builtins, at `-O0` and `-O2`; M23 checked `-O0` alone; the hosted tests of `msil/test/M3` against the DLL CRT oracle) |
| `utc` | six tests of `llvm/test/tools/UpdateTestChecks/update_llc_test_checks` | not lit: `msil/utils/run-utc-tests.py --llc <build>/bin/llc msil-basic x86-basic x86-mir-only x86-asm-mir-mixed x86-constant-pool asm-show-inst` runs their RUN lines (lit reports them UNSUPPORTED on Windows: `llvm/test/tools/UpdateTestChecks/lit.local.cfg` sets the `llc-binary` feature they require only when a file named exactly `llc` exists); the log is `build/lit-results/utc.log` |
| `obj-text` | `msil/test/M1`, then `msil/test/M1/11-programs` for `x86_64-unknown-linux-gnu-msil` | not lit, Windows only (M19): `build-mdtools.py`, then `cli-corpus-check.py --mode object --levels O0,O2` twice, with the tree's clang, `llvm-cil-dump`, `llvm-nm`, `llvm-ar` and `mdcanon`, under the shared build lock: every program's CIL object from `clang -c` against the .NET Framework ilasm's object of its object-form text (canonical listing with the three object-oracle normalizations, record included, and `--link-info`), the same bytes from two directories, `mdcanon` and both ildasms, the envelope, C1-C5 and C7, and the `llvm-nm` letters and `llvm-ar` index (below, "The metadata oracles"); it passes when both runs exit 0 and print their summary lines, and the log is `build/lit-results/obj-text.log`. Not in `lit-legs.sh`, because it needs the Framework ilasm |
| `lto` | `msil/test/M1/11-programs`, `msil/test/M2`, `msil/test/M3` | `msil_pipeline=lto`, *Windows run*, `msil_equiv=1 msil_also_run=10`, and lit's `--filter-out=\.ll$`: the programs, M2 and M3 through `clang -flto -c` and `msil-ld`'s LTO image on .NET 8, .NET 10 and .NET Framework 4.8, with oracle O4 (the `--no-gc-sections` LTO image against ilasm's image of the same link's `--lto-emit-asm` text) |
| `det` | `msil/test/M2/06-det`, `msil/test/Programs/det` | `msil_dotnet_runtime=8`, *corpus*: each program is built twice in two directories, and the CIL objects or bitcode, the archives and `msil-ld`'s image must be equal byte for byte (`%{msil_det}`); the nine exit programs too |
| `refs-net48` | `msil/test/M1/11-programs`, `msil/test/M2/00-link`, `msil/test/M2/05-refs` | `msil_pipeline=obj msil_references=net48 msil_equiv=1 msil_run_netfx=1` (M21): every MSIL link binds against the .NET Framework 4.8 reference assemblies (below), on .NET 8 and Framework 4.8, with O3 (which also allows `scopes` here) and O6 |
| `refs-net8` | the same, `msil/test/M2/04-shared` and the matrix's `p2c-bsymbolic`, `p2c-cycle` and `p2c-weak-def` | `msil_pipeline=obj msil_references=net8 msil_dotnet_runtime=8 msil_also_run=10`: bound to the newest `Microsoft.NETCore.App.Ref` 8.x pack, on .NET 8 and .NET 10; `msil-verify.py` runs ILVerify against the pack and skips PEVerify (`PEVerify skipped: bound to .NET 8 references`) |
| `refs-net10` | the paths of `refs-net48` | `msil_pipeline=obj msil_references=net10 msil_dotnet_runtime=10`: bound to the newest 10.x pack, on .NET 10, PEVerify skipped |
| `programs` | the nine program directories of `msil/test/Programs` and the hosted programs that exist (`$hostedPrograms`: `zlib-full` since M26; `lua` and `sqlite` when M27 and M30 add them) | `msil_pipeline=obj`, *Windows run*, `msil_also_run=10,10+tc0`, *corpus*: the exit programs through objects at `-O0` and `-O2` on .NET 8, .NET 10, .NET 10 with `DOTNET_TieredCompilation=0` (since M26) and .NET Framework 4.8 against the native build (the hosted programs against the DLL C runtime's) |
| `programs-lto` | the same | `msil_pipeline=lto`, *Windows run*, `msil_equiv=1 msil_also_run=10,10+tc0`, *corpus*: the same through LTO, with O4 |
| `ctest` | `msil/test/Programs/c-testsuite` | `msil_pipeline=obj msil_dotnet_runtime=10 msil_native_oracle=required msil_run_netfx=1 msil_also_run=8`, *corpus*: c-testsuite's two lists at `-O0` and `-O2` on .NET 10, .NET Framework 4.8 and .NET 8 (M23: .NET 8 alone): `libc-free.txt`'s 150 tests and, since M26, `libc.txt`'s 70 hosted ones (`MSIL :: Programs/c-testsuite/libc/<name>`), 220 in all. While `MSIL_CORPUS_SKIP` names `c-testsuite` the leg prints `ctest: skipped (c-testsuite is in MSIL_CORPUS_SKIP)`, runs no lit and is a SKIP row, never a pass |
| `ctest-lto` (`-Exit`) | `msil/test/Programs/c-testsuite` | `msil_pipeline=lto msil_dotnet_runtime=10 msil_native_oracle=required`, *corpus* (M26): both lists through LTO on .NET 10, in the post-commit runs and the Phase 3 exit gate; skipped like `ctest` while `MSIL_CORPUS_SKIP` names `c-testsuite` |
| `wsl-net10` (`-Wsl`) | `msil/test/M1`, `msil/test/M2`, `msil/test/M3`, `msil/test/harness` | `msil_pipeline=obj`, *WSL*, *E7h*, `msil_equiv=1 msil_also_run=8,10+tc0`: everything through objects for the Linux triple on WSL .NET 10, the private WSL .NET 8 and .NET 10 with `DOTNET_TieredCompilation=0`, with O3 (O6 needs PEVerify; ilasm and the dumper run on the Windows host) |
| `wsl-ilasm` (`-Wsl`) | `msil/test/M1/11-programs` | `msil_pipeline=asm`, *WSL*, `msil_wsl_ilasm=<the NuGet linux-x64 ilasm>`: assembled by the Linux ilasm inside WSL |
| `expected-linux` (`-Wsl`) | `msil/test/M1`, `msil/test/M2`, `msil/test/M3` | not lit: `update-expected.py --check --levels O0,O2 --os linux --linux-sysroot <sysroot> --glibc-sysroot <glibc sysroot>` (the E7 oracle, and E7h for the hosted tests, at `-O0` and `-O2`, against `<stem>.linux.expected` where one exists, else `<stem>.expected`) |
| `obj-text-wsl` (`-Wsl`) | `msil/test/M1`, then `msil/test/M1/11-programs` for `x86_64-unknown-linux-gnu-msil` | not lit (M19): `cli-corpus-check.py --mode gnu-tools --levels O0,O2` twice, the second time with `--triple x86_64-unknown-linux-gnu-msil`: GNU `nm` and `ar` inside WSL Ubuntu (binutils) read the COFF view of every program's CIL object as `llvm-nm` does, and the index of an archive of each level's objects as the records give it; it passes when both runs exit 0 and print their summary lines, and the log is `build/lit-results/obj-text-wsl.log`. Not in `lit-legs.sh` |
| `lto-wsl` (`-Wsl`) | `msil/test/M1/11-programs`, the `msil/test/M2` directories `00-link`, `01-archive`, `02-lto`, `03-native`, `04-shared`, `05-refs`, `07-equiv` and `08-semantics`, and `msil/test/M3` | `msil_pipeline=lto`, *WSL*, *E7h*, `msil_also_run=8`: the programs, the multi-file link tests (with the matrix rows that live in `00-link`), the archive, LTO, native-library, shared-library, reference and equivalence tests, the matrix and M3 through LTO for the Linux triple on WSL .NET 10 and .NET 8 (M24 added `01-archive`, `03-native`, `04-shared`, `05-refs`, `07-equiv` and M3); no O4 (O4 over these paths runs in `lto`, and for the Linux triple in the Linux CI job's `lto` leg) |
| `programs-wsl` (`-Wsl`) | the nine program directories and the hosted programs that exist | `msil_pipeline=obj`, *WSL*, *E7h*, `msil_also_run=8,10+tc0`, *corpus*: the exit programs through objects for the Linux triple on WSL .NET 10, .NET 8 and .NET 10 with `DOTNET_TieredCompilation=0` (since M26, which also added `msil_glibc_sysroot` for the hosted programs) |
| `programs-lto-wsl` (`-Wsl`) | the same | `msil_pipeline=lto`, *WSL*, *E7h*, `msil_equiv=1 msil_also_run=8,10+tc0`, *corpus*: the same through LTO, with O4 |
| `ctest-wsl` (`-Wsl`) | `msil/test/Programs/c-testsuite` | `msil_pipeline=obj`, *WSL*, *E7h*, `msil_also_run=8`, *corpus* (M24): c-testsuite for the Linux triple on WSL .NET 10 and .NET 8; skipped like `ctest` while `MSIL_CORPUS_SKIP` names `c-testsuite` |
| `det-wsl` (`-Wsl`) | `msil/test/M2/06-det`, `msil/test/Programs/det` | *WSL*, `msil_glibc_sysroot=<glibc sysroot>` (since M26, for `det/zlib-full.test`), *corpus*: the determinism builds for the Linux triple |
| `hosted` (`-Wsl`, optional) | `msil/test/M2/09-hosted` | `msil_glibc_sysroot=<glibc sysroot>` (M15, plan Q16): the hosted compile-only tests (glibc's headers and its real `libc.so` scripts, the UCRT headers) with the glibc sysroot required, so a missing one is a configuration error and the leg cannot pass by skipping. The other legs run these tests only where the default glibc sysroot exists and, for the UCRT test, `INCLUDE` holds the UCRT headers. Marked `-Wsl` because its sysroot is made through WSL (below); the CI never runs it |
| `net-core` (`-Quick`) | the paths of `net8` without `msil/test/Programs`, `msil/test/harness`, `msil/test/M3`, `00-link/spill-limits.test` and the tests of `$p3Tests` (one lit `--filter-out`; below) | `msil_pipeline=obj msil_dotnet_runtime=10 msil_run_netfx=1 msil_equiv=1 msil_require_csc=1` (M24): step G3 of the standard milestone gate (below), the FileCheck directories, M1 and M2 through objects on .NET 10 and .NET Framework 4.8, with O3 and O6 in every composite and the explicit O3 and O4 tests of `07-equiv`; no native oracle, no `msil_also_run`, no `msil_scale_tests` (7-8 quiet minutes) |
| `p3-full` (`-Quick`) | `msil/test/M3` and the tests of `$p3Tests` | `msil_pipeline=obj msil_dotnet_runtime=10 msil_native_oracle=required msil_run_netfx=1 msil_equiv=1 msil_also_run=10+tc0 msil_require_csc=1` (M24): G3b, every Phase 3 feature test on .NET 10, .NET 10 with `DOTNET_TieredCompilation=0` and .NET Framework 4.8 against the native oracle, the Windows half of the gate's first condition (2-4) |
| `harness` (`-Quick`) | `msil/test/harness` | `msil_pipeline=obj msil_dotnet_runtime=10 msil_native_oracle=required msil_run_netfx=1 msil_require_csc=1` (M24): G3c, which a gate runs only when `gate-select.py harness` prints `harness: run` (5-6) |
| `wsl-p3` (`-Quick`) | run 1: `msil/test/M3` and the tests of `$p3Tests`; run 2: `msil/test/M2/08-semantics` and `msil/test/M1/11-programs` without the tests of `$p3Tests` | `msil_pipeline=obj`, *WSL*, *E7h*, and for run 1 `msil_also_run=10+tc0` (M24): G4, two lit runs for the Linux triple on WSL .NET 10 (run 1 also with `DOTNET_TieredCompilation=0`); run 1 is the WSL half of the gate's first condition, and run 2 is the run that a gate over budget moves to the post-commit run first, after which the gate runs `-Leg wsl-p3 -Run 1` (4-6) |
| `scale` (`-Quick`) | `msil/test/M2/00-link/spill-limits.test` | `msil_pipeline=obj msil_dotnet_runtime=10 msil_scale_tests=1` (M24): the spill test at the real CLR limits on .NET 10, for the gates of milestones that change the trailer, the spill classes or a type's member limit |
| `programs-hosted` (`-Quick`) | the hosted programs that exist (`$hostedPrograms`; `msil/test/Programs/zlib-full` since M26) | `msil_pipeline=obj msil_opt_levels=O2 msil_dotnet_runtime=10 msil_native_oracle=required msil_run_netfx=1 msil_also_run=10+tc0`, *corpus* (M26): the hosted programs of the Phase 3 exit corpus at `-O2` on .NET 10, .NET 10 with `DOTNET_TieredCompilation=0` and .NET Framework 4.8 against the DLL C runtime's oracle, an exit check of the gates of M26, M27, M28 and M30 |
| `programs-hosted-wsl` (`-Quick`) | the same | `msil_pipeline=obj msil_opt_levels=O2`, *WSL*, *E7h*, `msil_also_run=10+tc0`, *corpus* (M26): the same for the Linux triple on WSL .NET 10, with and without tiering (`zlib-full`'s `fortify` test too) |
| `wsl-quick` (`-Quick`) | `msil/test/M1/11-programs`, `msil/test/harness`, `msil/test/M2` | `msil_pipeline=obj`, *WSL*, *E7*: the short WSL leg of the Phase 2 milestone gates (plan 5.12.10, G4); Phase 3's G4 is `wsl-p3` |
| `portable-smoke` (`Portable`) | `msil/test/Portable` | `msil_triple=msil64-unknown-dotnet msil_pipeline=obj msil_dotnet_runtime=10 msil_also_run=10+tc0 msil_run_netfx=1 msil_require_portable=1 msil_require_x86=1` (P-1a): the portable tests on .NET 10, with `DOTNET_TieredCompilation=0` and on a .NET Framework direct start, and `guard-x86`'s 32-bit starts, which must refuse the image |
| `portable-smoke-wsl` (`Portable`, `-Wsl`) | `msil/test/Portable` | `msil_triple=msil64-unknown-dotnet msil_pipeline=obj`, *WSL* with the portable triple in place of the Linux one, `msil_also_run=10+tc0 msil_require_portable=1` (P-1a): the portable tests on WSL .NET 10, with and without tiering; no native oracle, no E7h |
| `all-levels` (`-Full`) | `msil/test` | `msil_opt_levels=O0,O1,O2,O3,Os,Oz` |
| `lto-m1` (`-Full`) | `msil/test/M1` | `msil_pipeline=lto`, *Windows run*, lit's `--filter-out=\.ll$`: every C test of M1 as a one-file program through LTO (`lto` runs `11-programs` only) |
| `encoder-parity` (`-Full`) | `msil/test/M1` | not lit, Windows only (M18): `cli-corpus-check.py --mode encoder --levels O0,O2 --min-methods 2600` with the tree's clang, llc and `llvm-cil-dump`, under the shared build lock: the encoder's bytes of every method against the .NET Framework ilasm's image of the same text (below, "The metadata oracles"); it passes when the script exits 0 and prints its summary line, and the log is `build/lit-results/encoder-parity.log`. Since O3 in `net8` compares every M1 method's encoding with ilasm's anyway, it moved to `-Full` in M23. Not in `lit-legs.sh`, because it needs the Framework ilasm |

The leg set before M23, and where its legs went (`lit-legs.ps1 -Leg <old name>` fails and names
the leg that covers it now):

| Leg before M23 | Since M23 |
|---|---|
| `net8` (M1 through `-S` and ilasm, M2 and the FileCheck directories) | `net8`, through objects, with `msil_also_run=10,8+tc0`; M1 through `-S` and ilasm in `asm-oracle` |
| `obj` (M1 and M2 through objects, O3/O6, `msil_scale_tests=1`) | `net8` (`msil_equiv=1`, `msil_scale_tests=1`) |
| `net10` | `net8`'s `msil_also_run` entry `10` |
| `net8-tc0` | `net8`'s entry `8+tc0` |
| `wsl-net10` (M1 and the harness through `-S`) | `wsl-net10`, through objects, with M2 and `msil_also_run=8,10+tc0` |
| `wsl-net10-tc0` | `wsl-net10`'s entry `10+tc0` |
| `obj-wsl` (11-programs and M2 through objects, O3) | `wsl-net10` (`msil_equiv=1`) |
| `wsl-quick` | `wsl-quick`, now `-Quick`: only when `-Leg` names it |
| `encoder-parity` | `encoder-parity`, now `-Full` |
| `lto` (all of M1 and M2) | `lto` (11-programs and M2, with O4 and `msil_also_run=10`); all of M1 in `lto-m1` (`-Full`) |
| `det` (`06-det`) | `det` (with the exit programs' `det` tests) |
| `roundtrip` (11-programs and a library) | `roundtrip` (also `07-equiv`, under objects) |
| `refs-net8` | `refs-net8` (also `04-shared` and the library rows of the matrix, on .NET 10 too) |
| `more-levels`, `modern-ilasm`, `expected`, `utc`, `obj-text`, `refs-net48`, `refs-net10`, `wsl-ilasm`, `expected-linux`, `obj-text-wsl`, `hosted`, `all-levels` | unchanged, under `obj` where they run programs |
| (new) | `asm-oracle`, `programs`, `programs-lto`, `ctest`, `lto-wsl`, `programs-wsl`, `programs-lto-wsl`, `det-wsl` |

M24 (plan 5.17.6) kept every leg name and added `ctest-wsl` and the `-Quick` legs `net-core`,
`p3-full`, `harness`, `wsl-p3` and `scale`, the steps of the standard milestone gate (below). It
runs `msil/test/M3` inside `net8`, `lto`, `wsl-net10` and `lto-wsl` and checks its transcripts in
`expected` and `expected-linux`, widened `lto-wsl` to the `msil/test/M2` directories that link
programs, moved `ctest` to .NET 10 with .NET 8 as an `msil_also_run` entry, made both expected
legs compare at `-O0` and `-O2`, and passes `msil_require_hosted=1` in every lit run. M26
(plan 5.17.6) added the `-Quick` legs `programs-hosted` and `programs-hosted-wsl` and the
`-Exit` leg `ctest-lto`, gave the `programs` legs and their WSL twins the hosted programs and
`msil_also_run`'s `10+tc0`, gave `det-wsl` the glibc sysroot, made `ctest` and `ctest-wsl` run
c-testsuite's second list, and added the two new upstream-file tests to `net8`'s paths.

`$p3Tests` is the list of the Phase 3 feature tests outside `msil/test/M3`, the lines that
`python msil/test/M2/08-semantics/Inputs/check-matrix.py --p3-tests <repo>` prints: the tests of
the matrix lines that name a Phase 3 milestone (M24-M35, the lines that such a milestone flips
or adds), then the paths of `matrix.txt`'s `#! p3-tests:` lines (in M24 only
`msil/test/M1/02-fp/strict-i128.c`). `lit-legs.ps1` reads it
whenever it builds its legs (for `-Describe` and before running any leg, not for `-List`), and
when the checker fails it stops with the checker's message and runs nothing. The tests' full lit
names, escaped and anchored, form `$p3Out`, which joins the `--filter-out` of `net-core` and of
`wsl-p3`'s run 2, so that in a gate only `p3-full` and `wsl-p3`'s run 1 run them; an empty
`$p3Tests` gives no `$p3Out`. A leg of several lit runs (`wsl-p3`) passes only when every run
passes, and run k writes `build/lit-results/<leg>-run<k>.log`, `.xml` and `.params.rsp`. `-Run
<k>[,<k>...]` with exactly one `-Leg` runs only those runs of the leg (1-based) and names its row
`<leg> (run <k>)`; an index the leg does not have, `-Run` without exactly one `-Leg`, or `-Run` on
a leg without lit runs is a usage error (`lit-legs.ps1: usage error: ...` on stderr, exit status
2, nothing run). `-Describe` prints every leg as JSON and runs nothing (version 1 of plan
5.17.6: for each leg its name, tag, kind and lit runs, each with its paths, its lit arguments,
every parameter it passes, the `msil_require_*` ones included, and the derived pipeline, triple,
runner and `msil_also_run`; a NuGet tool prints as `@nuget:<rid>/<tool>`, and `$p3Tests` and
`$p3Out` appear resolved); `lit-legs.sh --describe` prints the Linux job's legs the same way. The
milestone gates' step G6 and the `matrix-legs` row of `run-m1-gate.ps1 -Exit` (below) hand it to
`check-matrix.py --legs`, which checks that some default, `-Wsl` or `-Exit` leg runs every line of
the link-semantics matrix on its pipelines and triples and, by the gate rule, that a `-Quick` leg
runs every test of a matrix line that names a Phase 3 milestone and every `#! p3-tests:` path on
both triples with `10+tc0`, the native oracle required and, for the Windows triple,
`msil_run_netfx=1` (`p3-full` and `wsl-p3`'s run 1 do).

`<sysroot>` is `$MSIL_LINUX_SYSROOT`, else `C:\llvm-msil-scratch\linux-sysroot`, and `<glibc
sysroot>` is `$MSIL_GLIBC_SYSROOT`, else `C:\llvm-msil-scratch\glibc-sysroot`. The reference
legs take their packs from lit, which finds them without downloading anything: `net48` is
`$MSIL_REF_NET48`, else the .NET Framework 4.8 reference directory (`%ProgramFiles(x86)%\Reference
Assemblies\Microsoft\Framework\.NETFramework\v4.8`), else `build/.NETFramework/v4.8` of the
NuGet package `microsoft.netframework.referenceassemblies.net48` that `msil-tools.proj` pins
(section 1; the Linux CI job restores it and exports `MSIL_REF_NET48`); `net8` and `net10` are
`$MSIL_REF_NET8` and `$MSIL_REF_NET10`, else `ref/net<N>.0` of the newest
`Microsoft.NETCore.App.Ref` `<N>.x` pack of the dotnet installation. lit prints `msil:
reference packs: ...`; a pack the leg names but lit cannot find is a configuration error, and
so are `msil_references` under `msil_pipeline=asm` (no link), `msil_run_netfx=1` with `net8`
or `net10` (such images do not load there), `net10` on a runtime older than .NET 10, and
`msil_pipeline=lto` with `msil_equiv=1` and a .NET pack (O4 would compare the never-bound
`--lto-emit-asm` text with an image whose data-import helper calls `NativeLibrary`). A
`msil_references` path that names a .NET pack
(`<dotnet>/packs/Microsoft.NETCore.App.Ref/<v>/ref/net<N>.0`, or its `System.Runtime.dll`) is
handled as `net<N>`: PEVerify skipped, ILVerify against the pack, and the same configuration
errors. Each lit leg
writes its lit arguments one per line (`--param=<name>=<value>`, `--xunit-xml-output=<file>`)
into the response file `build/lit-results/<leg>.params.rsp` and runs
`msil-lit.ps1 -LitArgs:"-sv @<file>"`; lit reads the file itself, so a parameter value may
contain blanks (a NuGet ilasm under a profile directory with a space, for example). The build
directory may not, because `msil-lit.ps1` splits `-LitArgs` at blanks; the leg then fails naming
it.

Expected transcripts (`<test>.expected`) are generated from a native build, never written by
hand (the one exception, `%{msil_expected_rule}`, is below; no test uses it yet), and CI checks
that they are current:

```sh
python msil/utils/update-expected.py msil/test/M1/11-programs/fib.c   # write
python msil/utils/update-expected.py --check --levels O0,O2 msil/test/M1 msil/test/M2 msil/test/M3   # Windows oracle
python msil/utils/update-expected.py --check --levels O0,O2 --os linux msil/test/M1 msil/test/M2 msil/test/M3   # E7, E7h
```

The script compiles C with the flags of `%{msil_cflags}` (`-std=gnu17 -ffreestanding
-Werror=return-type`) and runs the native builds in parallel (`-j`, default one per CPU). On
Windows its MSVC builds link the compiler-rt builtins (`--rtlib`, default `auto`, as
`msil_native_rtlib` below). `--os linux` uses the E7 oracle (`--linux-sysroot`, `--ld-lld`,
`--wsl-distro`); writing then creates `<stem>.linux.expected` only for a transcript that differs
from `<stem>.expected`, and removes one that no longer differs; three tests of
`msil/test/M2/08-semantics` have one (Status.md section 2, the matrix). `--check` compares with
`<stem>.<os>.expected` where it exists, else with `<stem>.expected`, the file the test itself
picks for that OS. An `M2` test (a `.test` file whose RUN line is `%{msil_prog}`) is built
natively by `msil-prog.py --mode native` from its split-file parts and `REDEFINE:` lines, with
the libraries of `%{msil_native_libs}` and, for the run, the files of `%{msil_deps}` next to
the program (M15), and since M22 with the shared libraries of `%{msil_shlibs}`, built natively
first (DLLs with `-fuse-ld=lld` on Windows, `-Bsymbolic` ELF shared objects for the E7 oracle)
and found next to the program; the RUN lines before the first composite, which build a test's
own native library, run first in lit's internal shell with the oracle's tools. Tests that only
pin a diagnostic or check determinism have no transcript and are skipped. Since M23 it also
builds the exit programs of `msil/test/Programs` natively from the corpus cache
(`--corpus-dir <dir>`, by default the cache lit uses, below): it puts the cache in place of
`%{msil_corpus}` in the `REDEFINE:` values, adds `%{msil_copts}` to the native compile flags and
writes or checks the file that `%{msil_expect}` names. It skips the `det` tests, which have no
transcript, and the c-testsuite directory (`skip (c-testsuite format: one test per name of
libc-free.txt): <dir>`), whose expected files its lit format writes when a test runs, from
c-testsuite's own `<name>.c.expected` followed by `[exit 0]` (since M24; before, the tests shared
one `exit0.expected`); in a directory whose `lit.local.cfg` sets `config.suffixes = [".test"]` it
reads only `.test` files, so the drivers and support files of `msil/test/Programs` are no tests.

Since M24 (plan 5.17.4, 5.17.5) the script also takes:

- **`--levels L[,L...]`** (it excludes `--opt`): every test is built and run at each level. With
  `--check` a test prints `ok: <test>` when every level matches its file, else one `MISMATCH:
  <test>` line and a diff for each level that differs, whose second file is `native (<os>,
  -<L>)`; writing needs the same transcript at every level. The expected legs and the milestone
  gates pass `--levels O0,O2`.
- **Hosted mode**, decided per test as lit decides it: a test is hosted when the `lit.local.cfg`
  of its directory or of one above it holds the line `config.msil_enable_hosted(config)`
  (`msil/test/M3`), and `--hosted` makes every test named on the command line hosted. A hosted
  test compiles without `-ffreestanding`, for a Linux target with `--sysroot=<glibc sysroot>`
  (`--glibc-sysroot`, default as `<glibc sysroot>` above), and takes the hosted oracle,
  `msil-prog.py --mode native --hosted`: the DLL C runtime for an MSVC target, E7h (below) with
  `--os linux` on a Windows host, the host clang's ordinary link on a Linux host. `REDEFINE:
  %{msil_oracle_flags} = --hosted` gives a freestanding test the hosted oracle alone (the P3 rows
  of `msil/test/M2/08-semantics` whose transcript needs glibc's exit path). Every other test keeps
  the oracles above.
- **Hand-written expected files.** `REDEFINE: %{msil_expected_rule} = <os>[,<os>]: <reason>`
  marks a hand-written expected file for each OS it names (`windows`, `linux`): for that OS the
  script builds, checks and writes nothing, and the file must exist (`rule: <file> (<reason>)`
  counts as a match, `MISSING: <file> (...)` fails). Plan 5.17.4 allows it only for R13's Windows
  destructor transcripts, for 16- and 24-byte atomics on Windows and for loud failures, the
  `%{msil_prog_loud_<L>}` tests, which have no native oracle and fail without a rule for the
  OS; [msil/test/README.md](../../../msil/test/README.md) lists every use, and none exists yet.
- **`REDEFINE: %{msil_limits} = --timeout S --mem-limit-mb N`** (either option alone): the test's
  native run gets S seconds, every step of its native build ten times that, and both the memory
  cap of N MiB, as in lit.

Since M24 it also skips the harness self-tests: a directory at or below the source tree's
`msil/test/harness`, named or reached by the walk, and a file named below it print `skip (harness
self-test: stand-in tools): <path>` and count as matches, because those tests use stand-in tools
and have no native oracle (the `harness` leg runs them); their copies under the build tree are
processed. A test or directory that several arguments reach (nested or repeated) is processed and
reported once.

How to add a test, what each status line means and how to reproduce a failure step by step is in
[msil/test/README.md](../../../msil/test/README.md).

### Native oracles and the WSL runtime

With `--param msil_native_oracle=1` or `required` (`required` in the legs that run programs:
`net8`, `asm-oracle`, `lto`, `programs`, `programs-lto`, `ctest`, the `-Quick` legs `p3-full`
and `harness`, and the WSL legs `wsl-net10`, `lto-wsl`, `programs-wsl`, `programs-lto-wsl`,
`ctest-wsl`, `wsl-p3` and `wsl-quick`; `net-core` runs no native oracle), every execution test also builds its source for
the native triple (`x86_64-pc-windows-msvc` for `x86_64-pc-windows-msvc-msil`, and so on), runs
it, and compares its transcript with the same `.expected` file as the MSIL build's. A program of
several files (`msil/test/M2`) is built natively from the same sources and archives by
`msil-prog.py --mode native`: on Windows through clang with `-fuse-ld=lld`, so that archives
resolve by lld's rules as they do in `msil-ld`, and under WSL by the E7 recipe below. Both
builds link the test's native libraries (`%{msil_native_libs}`, M15), and both runs get the
files of `%{msil_deps}` (a test-built `native.dll` or `libnative.so`) next to the program; the
E7 link adds `-rpath $ORIGIN`, so the native program finds such a library there too. Since M22
both builds also build the shared libraries of `%{msil_shlibs}` before the program: `msil-ld
-shared` in MSIL mode; natively on Windows a DLL with its import library, every compile with
`-fms-runtime-lib=dll` so that all of them share one C runtime, as MSIL assemblies do; and for
E7 a `lib<NAME>.so` linked with `-Bsymbolic` from `-fPIC` objects, which has the CLR's
no-interposition semantics. Both runs get them next to the program (`--dep-dir`).

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
- **Hosted tests: the DLL C runtime and the E7h oracle (M24, plan 5.17.3).** A hosted test
  (section 7's "Hosted mode" above; the tests of `msil/test/M3`) is built natively by
  `msil-prog.py --mode native --hosted`. For a Windows triple every native compile adds
  `-fms-runtime-lib=dll`, so the start-up code and the exit path are those of the DLL C runtime
  (`vcruntime140.dll` and the `ucrtbase.dll` that MSIL programs bind), not the static one's,
  whose exit differs; the compiler-rt builtins of `msil_native_rtlib` are linked as before. For
  the Linux triple under WSL it is E7h: the test compiles with `--target=x86_64-unknown-linux-gnu
  --sysroot=<glibc sysroot>`, and `clang -fuse-ld=lld -nostartfiles -nodefaultlibs` links it as a
  PIE with glibc's own `crt1.o`, `crti.o` and `crtn.o` and `-lm -lc`, where `-lc` is the
  sysroot's `libc.so` script (`libc.so.6`, `libc_nonshared.a` and the dynamic loader), without
  `crtbegin` or libgcc, so glibc's exit runs the `atexit` handlers before `.fini_array` as a
  native program's does; it runs under WSL as E7 does. Since M25 the link is `-lm <archive> -lc`,
  where `<archive>` is native compiler-rt's `libclang_rt.builtins-x86_64.a` of
  `msil_linux_rtlib` (`msil-prog.py --linux-rtlib`), so a hosted test that calls a runtime
  helper gets compiler-rt's results natively too, as the Windows build gets them from
  `msil_native_rtlib`; E7 keeps `libgcc_s.so.1`. The MSIL link keeps `msil_linux_sysroot`
  (hosted mode changes only the compiles), and E7 stays the oracle of `msil/test/M1`,
  `msil/test/M2` (apart from the tests that redefine `%{msil_oracle_flags}`) and c-testsuite's
  libc-free tests. On the Linux CI job hosted tests use the runner's own glibc
  (`msil_glibc_sysroot=/`) and the host clang's ordinary link, with the compiler-rt builtins
  that the host clang names (`msil_linux_rtlib=auto`).
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
(they take seconds once done, and under a minute more when `--builtins` rebuilds E7h's archive
after a build that changed clang; the first run downloads the two runtimes):

```powershell
wsl -d Ubuntu --cd <repository> --exec bash msil/utils/msil-wsl-dotnet.sh   # the private runtimes
python msil/utils/make-linux-sysroot.py --out C:/llvm-msil-scratch/linux-sysroot --builtins
```

`make-linux-sysroot.py` copies `libc.so.6`, `libm.so.6`, `libgcc_s.so.1` and the dynamic loader
out of the distribution (symbolic links resolved, SHA-256 checked) into
`usr/lib/x86_64-linux-gnu/` and `lib64/`, writes the linker scripts `libc.so` and `libm.so` and
a stamp with the distribution's glibc version, rewrites only files that changed, and refuses a
directory inside the repository. From M15 `msil-ld` uses the same directory as `--sysroot`
(the harness's `%{msil_ldflags}` on Linux triples), and its two scripts are what lets the
driver's `-lc -lm` find `libc.so.6` and `libm.so.6`. With `--builtins` (M25) it then runs
`build-builtins.py --native-oracle <out>/usr/lib/x86_64-linux-gnu`, which compiles compiler-rt's
builtins with the tree's clang for native `x86_64-unknown-linux-gnu` into
`libclang_rt.builtins-x86_64.a`, the archive that E7h links (the sources of the helper archive
plus the `__int128`, fp128, x87 and x86_64 ones, 165 members; skipped when its stamp is
current); `--clang <build>/bin/clang` names the build (without it `build-builtins.py` takes its
default, `$MSIL_BUILD_DIR` when it is set and not empty, else `build`) and `--jobs N` the
parallel compiles. `run-m1-gate.ps1` runs both commands and the third one below as its
`host-setup` step, after the build, the second with `--clang <build>/bin/clang.exe` for the
build directory it tests, so the step also builds E7h's native compiler-rt archive with that
build's clang (skipped when its stamp is current), as step G0 of every milestone gate from M25
does after the build.

The optional `hosted` leg (M15) and, since M24, hosted mode on the Linux triple (its headers and
the E7h oracle; the WSL legs marked *E7h*) need a glibc sysroot with headers, which a third
command makes, also without root (`apt-get download` of `libc6-dev`, `linux-libc-dev`, `libc6` and
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
of the `hosted` leg and of *E7h*), so those legs need no sysroot made beforehand. It already
holds the start files and `libc_nonshared.a` that E7h links (checked when M24 was planned).

### The exit corpus

The Phase 2 exit programs (`msil/test/Programs`, M23) build from third-party sources that the
repository never holds. `msil/utils/corpus/manifest.json` pins ten entries (zlib, tinf,
Monocypher, CoreMark, LZ4, BLAKE2, TweetNaCl, KISS FFT, jsmn, c-testsuite; versions and
licences in TestCorpus.md, "Phase 2 exit corpus") by the commit of a GitHub archive or the
SHA-256 of release files, and by the SHA-256 of the extracted tree. `fetch.py` (Python standard
library only) puts them into a cache outside the repository:

```sh
python msil/utils/corpus/fetch.py                         # download what the cache lacks, verify all
python msil/utils/corpus/fetch.py --check                 # offline: verify every tree, write the stamp
python msil/utils/corpus/fetch.py --skip coremark,c-testsuite   # leave out two entries (CI did until M24)
python msil/utils/corpus/fetch.py --list                  # names, trees, licences and URLs; no cache
python msil/utils/corpus/fetch.py --self-test             # offline self-test in a temporary directory
```

- **The cache** is `--cache <dir>`, else `$MSIL_CORPUS_CACHE` when it is not empty, else
  `C:\llvm-msil-scratch\corpus-cache` on Windows and `~/.cache/msil-corpus` elsewhere; a cache
  inside the repository is refused (exit status 2). Each entry is a tree `<cache>/<name>-<ref>/`
  (`zlib-1.3.2`, `tinf-57ffa1f1d5e3`, `c-testsuite-5c7275656d75`, ...). All ten take about 19 MB,
  of which 4.4 MB are downloaded.
- **What it trusts.** It fetches only over `https` (a redirect elsewhere is refused), checks an
  archive's SHA-256 before it extracts anything, refuses absolute, `..`, Windows-reserved and
  case-colliding member names, special files and links that leave the tree, turns a link inside
  the tree into a copy of its target, and moves a tree into place only when its SHA-256 matches.
  It deletes nothing but its own `<name>-<ref>` trees and temporary files, and holds a lock on
  the cache while it runs.
- **The stamp.** After every selected entry verified, it writes `<cache>/msil-corpus.stamp`, the
  SHA-256 of `manifest.json` and the tree hash of each selected entry; any failing run removes
  it. `--check` verifies without network and writes the stamp too. `--only` and `--skip` (comma
  lists, combinable; `--skip ""` skips nothing) select entries, and only those are fetched,
  verified and stamped. Each entry prints one line (`fetch.py: <name>-<ref>: verified`,
  `... skipped`, `... FAILED: <reason>`), and the last line is `fetch.py: <n> of <m> verified`.
  The exit status is 0, 1 when a check, hash, download or the manifest failed, and 2 on a
  usage error (an unknown entry name among them).
- **lit** reads the stamp. `--param msil_corpus_dir=<dir>` names the cache (default as above;
  an empty value disables the corpus, and so does a path with a blank); the feature
  `msil-corpus` holds when the stamp's manifest hash is that of the checked-in `manifest.json`,
  and `msil-corpus-<name>` for each tree the stamp lists whose directory exists;
  `%{msil_corpus}` is the cache with `/` separators. Without `msil-corpus`, `msil/test/Programs`
  is UNSUPPORTED with a note that names `fetch.py --cache <dir>`. Under `--param
  msil_require_corpus=1`, which the corpus legs pass, a missing or stale stamp is a
  configuration error, and so is an entry that the stamp lacks, unless `--param
  msil_corpus_skip=<name>[,<name>...]` names it: its tests are then UNSUPPORTED with a note. A
  `msil_corpus_skip` name that is not in the manifest is a configuration error.
- **The legs.** `programs`, `programs-lto`, `ctest` and `det` and their WSL twins (`ctest-wsl`
  since M24), and since M26 `ctest-lto`, `programs-hosted` and `programs-hosted-wsl`, pass
  `msil_corpus_dir` from `$MSIL_CORPUS_CACHE` (when it is set) and
  `msil_corpus_skip` from `$MSIL_CORPUS_SKIP`, with `msil_require_corpus=1`. While
  `MSIL_CORPUS_SKIP` names `c-testsuite`, the `ctest`, `ctest-wsl` and `ctest-lto` legs print
  `<leg>: skipped
  (c-testsuite is in MSIL_CORPUS_SKIP)`, run no lit and are SKIP rows. Since M24 the CI leaves
  the variable empty (below), so it is only a local opt-out; `run-m1-gate.ps1` clears it, so the
  phase gate always runs all ten entries.
- **c-testsuite's lists (M24, plan 5.17.14).** `msil/test/Programs/c-testsuite/libc-free.txt`
  names the 150 tests that call no C library function, and `libc.txt` (M26; required since,
  so a missing file is a configuration error) the other 70, which run hosted, with the triple's
  real headers and the hosted native oracle, as `MSIL :: Programs/c-testsuite/libc/<name>`. Both
  start with the header lines `# rule: <regex>` and `# count:
  <n>`, and each entry is `<name>`, `<name> native <triple>  # <reason>` (on that triple the native
  oracle's transcript is the expected file), `<name> diag [<triple>] <text>  # M<n> <what>` (a
  named diagnostic of a construct that milestone M<n> implements; no triple means every triple)
  or `<name> exclude <triple> long-double|not-portable  # <text>` (UNSUPPORTED on that triple,
  never XFAIL), where `<triple>` is `linux-gnu` or `windows-msvc`. A plain test's expected file is
  c-testsuite's own `<name>.c.expected` followed by `[exit 0]`, which the lit format writes before
  the test runs. M24's one modifier was `00140 diag error: variadic function definitions are not
  supported for MSIL yet  # M26 ...`; since M26 `00140` is a plain entry, no `diag` entry is left
  in `libc-free.txt`, and `libc.txt`'s modifiers are `00212`, `00216` and `00218 native
  windows-msvc` (their `.c.expected` assumes LP64 or GCC's layouts), `00204 exclude linux-gnu
  long-double`, `00214 exclude windows-msvc not-portable` and `00189 diag linux-gnu` (the address
  of `fprintf`, called through the pointer, is plan 5.15.2 rule 4's link-time error until M28's
  address adapter), so M26's target is 137 of its 140 (test, triple) pairs (Status.md section 1,
  "c-testsuite").
- **The hosted programs (M26, plan 5.17.15).** `msil/test/Programs/zlib-full` builds the
  cache's zlib 1.3.2 in full (all 15 library files, gzip file I/O included) with the triple's
  real headers and C library, against the hosted native oracle: `example.test` (zlib's
  `test/example.c`), `fortify.test` (the same with glibc's `-D_FORTIFY_SOURCE=2`, Linux triple
  only) and `minigzip.test` (`test/minigzip.c` on a generated 2 MiB input, whose gzip stream must
  equal the native one byte for byte); `Programs/det/zlib-full.test` builds it twice. It needs
  the corpus entry `zlib` and, like every hosted test, `msil-hosted`.

### The phase gate and CI

```powershell
pwsh -NoProfile -File msil/utils/run-m1-gate.ps1                                # everything but the -Exit legs
pwsh -NoProfile -File msil/utils/run-m1-gate.ps1 -CorpusCache C:/llvm-msil-scratch/corpus-cache -FetchCorpus
pwsh -NoProfile -File msil/utils/run-m1-gate.ps1 -SkipConfigure -SkipBuild -Exit -CorpusCache C:/llvm-msil-scratch/corpus-cache
pwsh -NoProfile -File msil/utils/run-m1-gate.ps1 -SkipConfigure -SkipBuild -Leg programs,roundtrip
pwsh -NoProfile -File msil/utils/run-m1-gate.ps1 -SkipConfigure -SkipBuild -Portable    # with the portable legs (P-1a)
pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -List                          # the leg names
pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Describe                      # every leg as JSON
```

`run-m1-gate.ps1`, the milestone-1 gate of M10, since M23 the phase gate of Phase 2 and since
M24, with `-Exit`, Phase 3's post-commit run (the third command above, since P-1a with
`-Portable`; below) and the run of its
exit gate, runs the scripts in `msil/utils/ci/` in the order the CI workflow
`.github/workflows/msil-ci.yml` runs them: `configure.ps1`, `build.ps1` (`msil-build.ps1
-Standard`, the targets of `build-targets.txt` of section 2, then the helper archives with
`build-builtins.py`, as the row `build/builtins`, and jitall), `unit-tests.ps1`
(`TargetParserTests`, `MCTests` and `MSILTests` with the MSIL filter and all of `CLITests`, no
skipped test allowed), then, on the development host only, the `host-setup` step (the three
commands of the host setup above, as the rows `host-setup/wsl-dotnet`, `host-setup/linux-sysroot`
and `host-setup/glibc-sysroot`), then the `corpus` step, `lit-legs.ps1 -Wsl` (every leg above
except the `-Quick`, `-Full` and `-Exit` ones), the `determinism` step, with `-Exit` the
`matrix-legs` step, `upstream-hooks.ps1` (the upstream-hooks check below, every leg; the gate
checks that each of them has a row) and `ci-sh`. The two M23 steps and M24's `matrix-legs`:

- **`corpus`.** `-CorpusCache <dir>` names the cache (default `$MSIL_CORPUS_CACHE`, else
  `C:\llvm-msil-scratch\corpus-cache`), which the legs get as `MSIL_CORPUS_CACHE`; the row
  `corpus/check` runs `fetch.py --cache <dir> --check` over all ten entries, never with `--skip`,
  and `-FetchCorpus` adds the row `corpus/fetch` before it, which downloads what the cache lacks.
  A failed `corpus` row does not stop the legs, whose corpus legs then fail with lit's
  configuration error.
- **`determinism`.** `msil/utils/ci/det-manifest.py build` twice, from the work directories
  `C:\llvm-msil-scratch\gates\det\a` and `...\b`: every C file of `msil/test/M1` compiled with
  `clang -c` for `x86_64-pc-windows-msvc-msil` and `x86_64-unknown-linux-gnu-msil` at `-O0` and
  `-O2` (relative paths, `-fdebug-compilation-dir=.`, and `-fms-compatibility-version=19.33` on
  the windows-msvc compiles, so the triple the objects carry does not depend on the host's
  MSVC), one `llvm-ar rcs` archive per triple and level, since M25 the helper archives of both
  triples (`build-builtins.py --out <work>/builtins`, listed as
  `<sha256>  <triple>/builtins/<archive>` without their stamps), and a manifest of sorted
  `<sha256>  <triple>/<level>/<file>` lines; then
  `det-manifest.py compare a b`, which fails on a differing entry, lists the entries found in
  one manifest only, and fails when none is common. `det-manifest.py --self-test` checks it
  offline.
- **`matrix-legs`** (`-Exit` only; M24, plan 5.17.11 and 5.17.12). `lit-legs.ps1 -Describe`
  writes `build/lit-results/legs.json`, and `check-matrix.py --legs <that file> <repo>` checks it
  as the milestone gates' G6 does (above, after the leg tables): a default, `-Wsl` or `-Exit` leg
  for every line of the matrix, pipeline and triple, and the gate rule. The row
  `matrix-legs/-` shows check-matrix's two summary lines, or its problem count and first
  problems. It needs no build, so it also runs after a failed configure or build.

A failed `host-setup` row does not stop the legs; the WSL legs that need what it makes then fail
with a configuration error that names it. The gate's `-NoWsl` leaves out `host-setup`, `-Wsl`
and with it the `-Wsl` legs and the `-Exit` legs whose name ends in `-wsl`, `-Full` adds the
`-Full` legs, `-Exit` adds the `-Exit` legs (none in M24) and the `matrix-legs` row, `-Leg` runs
only the named legs (the `corpus` and `determinism` rows run with it too, and `matrix-legs` with
`-Exit`; a removed leg's name fails, naming its successor), `-SkipUpstreamHooks` leaves out the
upstream-hooks step, and `-SkipConfigure`, `-SkipBuild` and `-SkipUnitTests` reuse what is
there. Unless `-NoWsl`, a `ci-sh` row smoke-checks the Linux job's scripts inside WSL (`bash -n`
on `msil/utils/ci/*.sh`, `lit-legs.sh --list` against its header, where `--full:` and `--exit:`
descriptions tag a leg, and a non-zero exit with FAIL rows against an empty build directory);
the Linux job's configure, build, unit-test and lit steps themselves run only in CI. A child
script that exits non-zero without a FAIL row, and every selected leg without a row, add FAIL
rows. A failed configure or build stops `host-setup`, the unit tests, the corpus and determinism
rows, the legs and the upstream-hooks step (`matrix-legs` and `ci-sh` need no build and still
run); every leg runs even after another one failed. A lit leg passes when each of its lit runs
exits 0 with at least one test passed, or passes the timeout retry (below), `expected` and
`expected-linux` when their script exits 0 and at least one test matched (`ok:` or `rule:`),
`utc` when its script exits 0, `encoder-parity` when its script exits 0 and printed its summary
line, `obj-text` and `obj-text-wsl` when each of their runs exits 0 and prints its summary line;
the lit log, xunit file and response file of a leg are `build/lit-results/<leg>.log`,
`<leg>.xml` and `<leg>.params.rsp`. The run ends with a summary table with one row per step or
leg and exits 0 only when every row passed (a SKIP row cannot occur, because the gate clears
`MSIL_CORPUS_SKIP`). Under the table it prints `==== phase gate: <n> row(s), <k> not PASS, <min>
min ====` (the line a post-commit run puts into the next milestone's commit body) and then lists
every row whose detail tells of the timeout retry again, with its whole detail, under `====
timeout retry: <n> row(s), <k> passed after the re-run ====`, or prints `==== timeout retry: none
====`. A full run on a current build tree takes
about three to six hours on the development host (4 cores, 8 threads), depending on its load:
M23's four runs took 228, 339, 184 and 275 minutes, which plan section 12 ('Q32 amended
(phase-exit gate time)') records as the phase gate's time in place of Q32's 70-90 minutes. The
Windows legs took 144, 217, 129 and 132 minutes (`net8` 61, 87, 52 and 44, `asm-oracle` 19, 23,
14 and 16, `lto` 11, 22, 17 and 19, `ctest` 9, 18, 7 and 11, `more-levels` 16, 16, 13 and 12,
every other leg under 9), the WSL legs 79, 117, 50 and 138 (`wsl-net10` 57, 103, 36 and 109,
`lto-wsl` 9, 6, 6 and 10, every other leg under 5; from the third run on, `lto-wsl` also ran
`00-link` and `programs-lto-wsl` O4), and configure, the build check, the unit tests and the
`corpus`, `determinism`, upstream-hooks and `ci-sh` rows about 5 together. So start it in the
background. Those times predate M24, which added `msil/test/M3` to four legs, `ctest-wsl` and
the wider `lto-wsl`, and made each composite cheaper (above, "Limits, probes and steps").

From M12 on, each milestone's own gate ran a part of this in about 30 minutes (plan 5.12.10:
from M13 the build `msil-build.ps1 -Standard`, the unit tests with a floor of `CLITests` tests,
the `net8` leg, `wsl-quick`, the upstream-hooks `manifest` and `pins` legs, or all of the check
when the milestone changed an upstream file, and the milestone's own legs, such as `lto` and
`det` in M14, the IL snapshot below in M15, M18, M19 and M20, `obj-text` in M19, the `obj` leg in
M20 and the reference legs in M21), and `run-m1-gate.ps1` ran in full after the commits of M12,
M15, M18 and M20. M23's gate is the phase gate itself: `fetch.py --self-test`, the fetch and
`--check` of all ten entries, the build, the `CLITests` floors (210 tests, at least 9 of
`CLISymbolTableTest`), the leg lists of both scripts, the harness self-tests and
`matrix-coverage.test`, `lld/test/MSIL` and `clang/test/Driver/msil-shared.c` with
`LLD_IN_TEST=2`, `cli-corpus-check.py --mode ilasm,dumpers,copy,stats` on `11-programs`, the IL
snapshot against M22's build, `run-m1-gate.ps1 -CorpusCache <cache>`, the workflow and pin
checks, and the format and scope checks. Its step 16 let a lit leg whose only failures were
tool timeouts under host load pass when each such test, three at most, passed one re-run by
hand, alone (plan section 12, 'M23 gate step 16 amended (a tool timeout under host load)');
M23's last run needed one, of `M1/09-structs/struct-byval.c` in `wsl-net10`. Since M24 the
legs make that re-run themselves.

**The timeout retry (M24, plan 5.17.9).** After a lit run of a leg that failed, `lit-legs.ps1`
and `lit-legs.sh` run `msil/utils/ci/lit-retry.py select <xunit>`, which prints the full names of
the timeout-only failed tests, those whose failure text after the last command lit's shell
executed holds a timeout marker (`did not finish within <T> s` from `msil-run.py`,
`msil-verify.py` or `msil-jitall.py`, a `[timeout]` status line, or `msil-prog.py`'s `<step>
failed (timeout after <T> s)`). When it exits 0 (every failure timeout-only, three at most), lit
exited 1 (failed tests only) and lit ran every test (no testcase in the xunit file has `<skipped
message="Skipped"/>`, lit's mark of a test it did not run), each listed test runs once more,
alone, with exactly the run's parameters and lit arguments, `-j1` and `--filter=^<escaped
name>$`, into `build/lit-results/<leg>-retry-<stem>.log` and `.xml` (`<stem>` is the full lit
name with every run of other characters than letters, digits, `.` and `-` written `_`). The row
then passes with a detail that starts `retried after timeout: <names> (<logs>)` when every re-run
passes, and fails when a re-run fails in any way (a hang that times out twice still fails:
`re-run after timeout failed: <name> (<log>; lit exit <n>)`), when more than three tests timed
out (`<n> tests timed out; re-run the leg on a quieter host`), or when any failure is not
timeout-only (no re-run then). A lit run that exited non-zero without an xunit file, or whose
xunit file lists no failed test (as after `a worker process crashed`, when lit's process pool
lost a worker), also fails the row and re-runs nothing, and so do a run for which `lit-retry.py
select` fails and a run whose timeout-only failures it listed with exit 0 although lit exited
otherwise than 1 (`lit reported an error, so no retry`: after an error in tests lit exits 2) or
did not run every test (`lit did not run <k> test(s), so no retry`: `--max-failures` and
`--max-time`, which `LIT_OPTS` can pass, stop lit early without an error, and it exits 1). Either
way lit writes every test it had not run as skipped, so a re-run must never pass such a run. A
gate step or post-commit row that failed only through a lit error, with no failed test in its
xunit file, is re-run once, whole and alone, and passes only if that re-run passes; neither
`lit-legs.ps1` nor `lit-legs.sh` re-runs a leg itself. The legs that are not lit (`expected`,
`expected-linux`, `utc`, `encoder-parity`, `obj-text`, `obj-text-wsl`) have no retry, lit's own
retries (`ALLOW_RETRIES`, `--max-retries-per-test`) are never used, and `msil_timeout` stays 60 s
in every leg; a test of a large program raises its limits with `REDEFINE: %{msil_limits} = ...`
instead, with a comment that gives the slowest command's quiet time.

**The standard milestone gate (M24, plan 5.17.7).** From M24 each Phase 3 milestone's gate runs
these steps, with logs under `C:\llvm-msil-scratch\milestones\M<n>\gate-<k>\`, besides the
milestone's own steps:

| Step | What | Quiet minutes |
|---|---|---|
| G0 | the host setup above (from M25 with the native builtins, `make-linux-sysroot.py --builtins`; from M30 with `libatomic`) | <0.5 |
| G1 | D:'s free space (reported only), `msil-build.ps1 -Standard`; from M25 the helper archives (`build-builtins.py`, then `--check` and `--check-list`) | not counted |
| G1b | from M26, in a milestone that changes `msil/lib/builtins/`: the helper archives of all six triples | not counted |
| G2 | the unit tests, with the floors that the milestone's section names | ~1 |
| G2c | `python msil/utils/ci/calibrate.py --label start` | 0.5 |
| G3 | `lit-legs.ps1 -Leg net-core` | 7-8 |
| G3b | `lit-legs.ps1 -Leg p3-full` | 2-4 |
| G3c | `lit-legs.ps1 -Leg harness`, only when `python msil/utils/ci/gate-select.py harness` prints `harness: run` | 5-6 |
| G4 | `lit-legs.ps1 -Wsl -Leg wsl-p3` (`-Run 1` once its run 2 has moved) | 4-6 |
| G4c | `python msil/utils/ci/calibrate.py --label end`, then `--combine` of both files | 0.5 |
| G5 | `upstream-hooks.ps1 -Leg manifest,pins`, or the whole script when the milestone edits an upstream file | 0.3 (3) |
| G6 | the milestone's test-presence check; `check-matrix.py --legs` on the output of `lit-legs.ps1 -Describe` | <0.5 |
| G7 | `update-expected.py --check --levels O0,O2` over the directories that `gate-select.py expected-dirs` prints, with the Windows oracle and with `--os linux` (E7, E7h where hosted) | 1-2 |
| G8, G9 | the format check; `plan-check.py scope M<n>` | ~1 |

Without G3c that is 17-23 quiet minutes. Two conditions hold in every gate: `p3-full` and
`wsl-p3`'s run 1 run every Phase 3 feature test (every test of `msil/test/M3` and of `$p3Tests`)
on .NET 10, .NET 10 with `DOTNET_TieredCompilation=0`, .NET Framework 4.8 and WSL .NET 10 with
TC=0, against the native oracle (G6 proves it), and `net-core` keeps the equivalence oracles;
verification runs in every lit leg. A gate measures its host's load instead of assuming a quiet
host: `calibrate.py` runs `msil/test/M1/03-control/loops.c` alone (`-j1`) with the fixed
parameters of `msil/utils/ci/calibration.json`, inside a Windows job object, writes
`build/lit-results/calibration-<label>.json` (or `--out <file>`) and prints `calibration
<label>: <s> s against <q> s quiet, factor <f>`, where the factor is the measured time over the
file's `quiet_seconds`; it warns when the test's measured CPU time differs from the file's
`test_cpu_seconds` by more than 30%, and exits 1 when the test does not pass. `calibrate.py
--combine <start.json> <end.json>` prints `gate load factor <F>`, F = max(1.0, (start factor +
end factor) / 2), and the gate report gives each step's raw minutes and its normalized minutes,
raw / F. The budget is 30 normalized minutes, the build not counted. When round 1 of a gate
fails above it, steps move to the post-commit run through plan section 12 rows, in this order:
the milestone's own steps marked movable in its section, then G3c when the milestone's harness
change only adds files, then G4's run 2; G3, G3b, G4's run 1, a step that an exit criterion names
and a step that failed never move, and a total still above 30 is only recorded. `gate-select.py`
(standard library only) decides the two optional steps from the change set, `git diff
--name-only <base>` (default `HEAD`) plus the untracked files: `harness [--base REV]` prints
`harness: run` and the changed harness files (`msil/utils/**` except the corpus manifest and
`upstream-hooks.txt`, `msil/test/lit.cfg.py`, `msil/test/lit.site.cfg.py.in`, every
`lit.local.cfg` and `.py` file under `msil/test`, and `msil/test/harness/**`), else `harness:
skip`; `expected-dirs [--base REV]` prints each directory under `msil/test` (outside `Inputs/`
and `Output/`) with an added or changed `.c`, `.ll`, `.test` or `.expected` file.

**The post-commit run (M24, plan 5.17.11).** After each Phase 3 milestone's commit,
`run-m1-gate.ps1 -SkipConfigure -SkipBuild -Exit -Portable -CorpusCache <cache>` (`-Portable`
since P-1a) runs in the background, not as a gate step: the unit tests, the host setup, the
corpus check, every default, `-Wsl`, `-Exit` and `-Portable` leg (so .NET 8 and every leg the
standard gate leaves out), determinism, `matrix-legs`,
upstream-hooks and `ci-sh`, about three to six hours on the loaded host, with the steps that the
milestone's gate moved. A failure becomes a fix work package of the next milestone, whose gate
cannot pass while the fix is open, and the run's `phase gate:` summary line goes into the next
milestone's commit body. The Phase 3 exit gate (M35) runs `run-m1-gate.ps1 -CorpusCache <cache>
-Exit` with configure and build.

The Windows CI job runs the same scripts, except that `configure.ps1 -Ci` configures with the
cache file instead of `msil-configure.ps1`, there is no `host-setup`, `lit-legs.ps1` runs
without `-Wsl` (a GitHub runner has no WSL), and `upstream-hooks.ps1` runs as `-NoCompile
-NoManifest -MinPass 250` whenever the build passed (also after failed unit tests or legs). The
Linux job runs their `.sh` twins, whose legs cover `x86_64-unknown-linux-gnu-msil` natively:
`net8` with `msil_also_run=10,8+tc0,10+tc0` (O3, but no O6, which needs PEVerify, and no
`msil_scale_tests`), `more-levels`, `asm-oracle` (the NuGet ilasm and ildasm), `lto`, `det`,
the reference legs `refs-net48` (with the NuGet reference assemblies that the job restores and
exports as `MSIL_REF_NET48`, and without a .NET Framework run), `refs-net8` and `refs-net10`,
`roundtrip`, `programs`, `programs-lto`, `ctest` (on .NET 10 and, as an `msil_also_run` entry,
.NET 8 since M24), `expected` (the native Linux oracle: `update-expected.py --check --levels
O0,O2` on Linux, against `<stem>.linux.expected` where one exists) and `utc` (`bash
msil/utils/ci/lit-legs.sh --list`); its `--full` legs add `all-levels`, `lto-m1` and `upstream`,
which builds and runs all of `ninja check-llvm check-clang`, and `lit-legs.sh --leg hosted` runs
the optional `hosted` leg against the host's own glibc; no CI step names it. Since M24 every
Linux leg passes `msil_glibc_sysroot=${MSIL_GLIBC_SYSROOT:-/}`, and `net8`, `lto` and `expected`
also take `msil/test/M3`, whose hosted tests use the runner's own glibc headers and libraries
and the host's ordinary link as their oracle. `lit-legs.sh --exit` adds the `--exit` legs and
`--describe` prints the JSON of `-Describe`; the CI passes neither, and neither job runs the
`-Quick` or `-Exit` legs (plan 5.17.17). `lit-legs.sh --leg <removed leg>` (`net10`,
`net8-tc0`, `net10-tc0`, `obj`) fails naming its successor.

Since M23 both jobs also handle the exit corpus and determinism:

- The workflow sets `MSIL_CORPUS_SKIP: ''` since M24 (R23, plan 5.17.17), so both jobs
  download and run every entry. Until M24 it was `coremark,c-testsuite`, because the CI licences
  of CoreMark and of c-testsuite's tests were unverified (plan section 10 item 11); CoreMark's
  tests were then UNSUPPORTED with a note and `ctest` a SKIP row. The variable stays for local
  opt-outs. Each job points `MSIL_CORPUS_CACHE` at a directory outside the checkout, restores it
  with `actions/cache` (pinned as upstream pins it, keyed on
  `hashFiles('msil/utils/corpus/manifest.json')` and a hash of the skip list, never the raw
  variable), and runs `fetch.py --cache "$MSIL_CORPUS_CACHE" --skip "$MSIL_CORPUS_SKIP"` and the
  same with `--check`.
- After the legs each job runs `det-manifest.py build --out build/lit-results/det-<os>.txt` and
  uploads it, and the `determinism` job (Ubuntu, after both) downloads the two manifests and
  runs `det-manifest.py compare`: the CIL objects and archives of `msil/test/M1`, and since M25
  the helper archives of both default triples, must be the same bytes on Linux and Windows.
  Linked images are not compared across hosts (neither runner can link the other OS's triple);
  `det` checks them within each host.

Both jobs install .NET with `actions/setup-dotnet` pinned at v5.4.0
(`26b0ec14cb23fa6904739307f278c14f94c95bf1`). The workflow is committed with a manual trigger
only (`workflow_dispatch`, whose `full` input selects the full legs, as a nightly schedule
would); enabling push, pull-request or nightly runs is the repository owner's decision. The
Linux job installs `ninja-build`, `clang` and `lld` from Ubuntu 24.04 (clang 18 with libstdc++
14) and, since M26, `libclang-rt-18-dev`, compiler-rt's builtins archive, which lit's
`msil_linux_rtlib=auto` finds for the hosted native oracle (Ubuntu ships it only as a Recommends
of `libclang-common-18-dev`, which `--no-install-recommends` leaves out).

Since M26 each milestone gate also runs, after its build, a check of the fork's changed C++
files with the Linux job's compiler, because neither the Windows build nor the Windows CI job
shows an error that only clang 18 with libstdc++ reports (the Linux job of the CI run on M24's
commit stopped at one):

```sh
python msil/utils/ci/linux-syntax-check.py --changed-from HEAD   # the gate's step
python msil/utils/ci/linux-syntax-check.py FILE...               # chosen files
python msil/utils/ci/linux-syntax-check.py --setup-only          # fetch the toolchain only
python msil/utils/ci/linux-syntax-check.py --check-toolchain     # exit 0 when it is ready
```

It compiles each changed or untracked `.cpp` under `llvm/`, `clang/`, `lld/` and `msil/`
(outside `test` and `Inputs` directories), and each FILE as given, with `-fsyntax-only` for
`x86_64-unknown-linux-gnu`, with the flags of the build's `compile_commands.json` translated to
the Linux job's, using Ubuntu 24.04's clang 18 and libstdc++ 14, which it fetches once from the
Ubuntu archive (SHA-256-checked, extracted without root) into `$MSIL_LINUX_CLANG18`, else
`C:/llvm-msil-scratch/linux-clang18` on Windows and `~/llvm-msil-scratch/linux-clang18`
elsewhere; on Windows the compiles run inside WSL. It prints `ok:`, `FAIL:` (with clang's
output) or `skip:` (no compile command) per file and a summary line, and exits 1 on a failure
and 2 on a setup error. The CI workflow has no such step, because its Linux job builds every
file with that compiler. `harness/linux-syntax-check.test` runs only where `--check-toolchain`
finds the toolchain (lit's feature `msil-linux-syntax-check`); lit never fetches it.

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
- `--normalize forms` (M18; only with `--compare`) compares the `.il` files of both snapshots
  after mapping the forms that M18 changed back to Phase 1's: short branches become long ones
  (`br.s` ... `blt.un.s` and `leave.s` lose their `.s`), `ldc.i4.m1`, `ldc.i4.<n>` and
  `ldc.i4.s <n>` become `ldc.i4 <n>`, and `nomangle cdecl` becomes `cdecl`, each only as a
  whole word. Every other byte, line ends included, is compared as it is, the `.err` files
  exactly, and a summary line counts the `.il` files that differ only in those forms. Compared
  that way with a snapshot of M17's build, M18's build gives equal IL and diagnostics for all
  2,712 compiles of the 452 inputs of `msil/test/M1` and `llvm/test/CodeGen/MSIL`, 1,861 of the
  `.il` files only after normalizing (M18's gate runs this comparison).
- `--llc-arg <arg>` (M20; repeatable) appends `<arg>` to every llc command and `-mllvm <arg>`
  to every `clang -S`, and `--skip-changed` (M20) leaves out, and lists, every input under the
  paths that `git status --porcelain --untracked-files=all` reports as changed against `HEAD`
  (both names of a rename), on both sides of `--compare`. M20's gate compares its build with a
  snapshot of M19's with `--llc-arg=-msil-reloc-loop-threshold=1000000000 --skip-changed`:
  with the relocation loop out of reach, every input that M20 did not change must give the same
  IL and diagnostics, so the shared trailer builder and the whole-program layout of named
  sections leave every other module's text as it was.

The FileCheck tests that pin IL text change with it, and `msil/utils/check-il-forms-diff.py`
(M18) checks that such a change is mechanical: it reads `git diff -U0 <rev>` (default `HEAD`)
of the working tree under each path and accepts a modified `.ll`, `.c`, `.s`, `.test`, `.mir`
or `.expected` file only when every changed line of each of its hunks is one of these:

- a removed line paired, in order, with an added one that is equal to it after the same
  `--normalize forms` rewrite. The rewrite applies to CHECK patterns too and, in escaped
  spelling, inside FileCheck's `{{...}}` regular expressions: `ldc\.i4\.s <n>`, `ldc\.i4\.m1`
  and `ldc\.i4\.<0-8>` become `ldc\.i4 <n>`; a `.s` or `\.s` suffix right after a branch
  mnemonic, or right after a group or `{{...}}` block that lists only branch mnemonics
  (`{{brtrue}}.s`, `(brfalse|brtrue)\.s`), is dropped; and an optional-suffix group `(\.s)?`
  is dropped, a whole `{{(\.s)?}}` block with it. Nothing else in a regular expression
  (labels, other mnemonics, operands) and nothing in a `[[...]]` variable is rewritten;
- a RUN or DEFINE line that differs from its old line only by removed
  `--implicit-check-not=<pattern>` arguments whose pattern names a short form (plain or as a
  regular expression, such as Phase 1's `{{^ +b[a-z.]+\.s }}`) or `nomangle`, the rest of the
  line unchanged. An argument that stood alone on the last continuation line may go with its
  line, the line before it then losing only its trailing ` \`;
- comment prose: a comment line with no lit keyword and no FileCheck directive. An executed
  comment line, which a RUN or DEFINE line of the file runs as program text, is not prose: a
  line that starts with a string that such a command quotes together with `%s`, as the
  `%{jtsize}` DEFINE of `llvm/test/CodeGen/MSIL/switch.ll` runs its `;PY ` lines as Python.
  It must pair one to one with a line that is equal to it after `--normalize forms` extended
  for program text: a quoted short branch mnemonic (`'br.s'`) becomes the long one and is then
  dropped with its comma when it equals the quoted literal right before it in the same list,
  so `('br', 'br.s', 'ret')` equals `('br', 'ret')`. A line whose program text is a comment of
  the program's language (`;PY # ...` for Python) stays prose;
- a removed `CHECK-NOT` line that names a short form or `nomangle`, which pinned the absence of
  what M18 now prints.

An added, deleted or untracked file fails, and so does any other hunk:

```powershell
python msil/utils/check-il-forms-diff.py --base HEAD llvm/test/CodeGen/MSIL clang/test/CodeGen/MSIL `
  lld/test/MSIL llvm/test/tools/UpdateTestChecks/update_llc_test_checks/Inputs
python msil/utils/check-il-forms-diff.py --self-test   # every rule, in a temporary git repository
```

It prints each failing hunk, one count line per path and a total, and exits 0, 1 when a change
fails, or 2 on a usage or git error.

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

M18 added the mode `encoder`, the `encoder-parity` leg of section 7 (`--llc` names llc, by
default `<build>/bin/llc`; `--min-methods N` fails a run that compares fewer methods):

```powershell
python msil/utils/mdtools/cli-corpus-check.py --mode encoder --levels O0,O2 --min-methods 2600 msil/test/M1
```

Each program is compiled at each level with `clang -S -emit-llvm` and `llc -show-mc-encoding`
(section 3, "Instruction encodings"), and the Framework ilasm assembles that same text. For
every method the script rebuilds the bytes from the encoding comments, resolving each
PC-relative fixup from the label offsets as the assembler would, and reads ilasm's body of the
method from the image (`llvm-cil-dump --headers --tables`, tiny and fat headers). The code bytes
must be equal, ilasm's token at each `fixup_cil_<class>_token` must be in a table of that class
and name the member or signature of the fixup's value, and `.maxstack` must be equal. A float32
signaling NaN that ilasm quieted counts apart as `snan-quiet` (Status.md section 3, "Limits").
Any difference, a method without encoding comments, a program that does not compile or
assemble, or fewer methods than `--min-methods` fails the mode. A `switch` of more than 191
targets, whose encoding comment ilasm cannot read (section 3), is found in the listing before it
is parsed or assembled and fails that program and level with a message that names the `switch`
and its target count, instead of ilasm's generic failure. With M18's build all 2,819
methods of the 198 modules (the 99 C programs at `-O0` and `-O2`) were equal, with 24,099 token
and 23,316 branch and `switch` fixups checked; the run takes a few minutes with `--jobs 4`.

M19 added the modes `object` and `gnu-tools`, the `obj-text` and `obj-text-wsl` legs of section
7, which check the CIL objects of `clang -c` (section 3, "CIL objects"); `--llvm-nm` and
`--llvm-ar` name the symbol tools, by default those of `<build>/bin`:

```powershell
python msil/utils/mdtools/cli-corpus-check.py --mode object --levels O0,O2 msil/test/M1
python msil/utils/mdtools/cli-corpus-check.py --mode object --levels O0,O2 `
  --triple x86_64-unknown-linux-gnu-msil msil/test/M1/11-programs
python msil/utils/mdtools/cli-corpus-check.py --mode gnu-tools --levels O0,O2 msil/test/M1
python msil/utils/mdtools/cli-corpus-check.py --mode gnu-tools --levels O0,O2 `
  --triple x86_64-unknown-linux-gnu-msil msil/test/M1/11-programs
```

- `object` (the object oracle of plan 5.12.6, `canon(clang -c) == canon(ilasm(object-form
  text))`) works in `<work>/object/<triple>/`. Each program is compiled at each level with
  `clang -c` twice, once with an absolute `-o` into `obj1/` and once from a directory of its
  own in `obj2/` with the relative `-o again.o`, and the two objects must be the same byte for
  byte; then with `-S -mllvm -msil-object-form`, which the .NET Framework ilasm (the NuGet
  ilasm off Windows) assembles into an object of its own. Each object is then checked: its
  `llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet`
  listing, which includes the record's bytes, and its unnormalized `--link-info` must equal
  those of ilasm's object (`canonical`, `link-info`); one `mdcanon --out-dir` batch must list
  every object as `llvm-cil-dump` does (`mdcanon`); the headers must show the triple's machine,
  a PE32+ DLL with characteristics 0x2022 and entry point 0, exactly the sections `.text`,
  `.data`, `.rdata` and `.bss` with the marker flags of plan 5.8.6, one COFF symbol per record
  symbol, the module named after the source file and no `initlocals` (`envelope`); the
  consistency rules C1-C5 and C7 of plan 5.8.5 must hold between `--tables` and `--link-info`,
  for the clang object and for ilasm's object, whose rows ilasm numbered (`consistency`);
  `llvm-nm --no-sort` must print one symbol per record symbol, in record order, with the
  letter of plan 5.8.6 (`llvm-nm`); the index of `llvm-ar rcs` of the object, read with
  `llvm-nm --print-armap`, must list exactly its defined non-local symbols and its commons
  (`llvm-ar`); and both ildasms (the Framework's with `-text -nobar -utf8`, the NuGet one
  with `-utf8`) must open it, exit 0, print no `error :` line and one `.method` per
  MethodDef row (`ildasm`). A failing check skips the later ones of its object, which count as
  failed, and a failure prints `object: FAIL <name> -<level> (<check>): <why>`. The summary
  line is `cli-corpus-check.py: object: <n> of <m> objects passed every check (<triple> at
  <levels>): <check> <ok>/<total>, ...`; over `msil/test/M1` it checks 198 objects (99
  programs at two levels).
- `gnu-tools` (judge-dotnet-value J2, now with the marker sections) compiles each program with
  `clang -c` into `<work>/gnu-tools/<triple>/obj/` and runs one bash script inside WSL
  (`--wsl-distro`, default Ubuntu; natively on Linux): `nm -p` on every object, then per level
  `ar rcs`, `ar t` and `nm -p -s` on an archive of that level's objects. GNU `nm` must print
  `llvm-nm --no-sort`'s letters and names in its order, `ar t` must list the members in
  order, and the archive index must list exactly the defined non-local symbols and the commons
  of each member, as its record gives them. It needs only binutils in the distribution.

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

### The equivalence oracles (`msil-equiv.py`, M20)

Since M20 the tests compare `msil-ld`'s images with ilasm's images of the text that the same
compiler writes (plan 5.12.6), canonically, so that the shared trailer builder, the image
layout and the token renumbering cannot drift apart from `-S`:

| Oracle | Comparison | Where |
|---|---|---|
| O2 | ildasm, ilasm and ildasm again of `msil-ld`'s image: the two listings are equal, and the reassembled image runs (Design.md 5.5); since M21 one difference is accepted, line for line: a float32 signaling NaN `ldc.r4` operand that ilasm quieted; since M22 also, for a library, the top-level class block that a `.custom` of the `.assembly` block names (the `'<msil>SymbolTable'` attribute class of every `-shared` output) at another position: ilasm defines a TypeDef at its first reference, and the `.assembly` block precedes every class, so the class comes back first. The block's own lines and every other line must still be equal | `msil_pipeline=obj msil_roundtrip=1`: since M23 the `roundtrip` leg (`11-programs`, `07-equiv` and `04-shared/roundtrip-library`) |
| O3 | `canon(clang -c + msil-ld --no-gc-sections) == canon(clang -S + ilasm -exe)`, one file | `%{msil_c}` and `%{msil_ll}` under `msil_pipeline=obj` with `msil_equiv=1` (since M23 the `net8`, `wsl-net10` and `refs-net48` legs; before, `obj` and `obj-wsl`), `msil/test/M2/07-equiv/o3-*.test` |
| O4 | `canon(msil-ld's LTO image, --no-gc-sections) == canon(ilasm -exe of the same link's --lto-emit-asm text)`, several files | `%{msil_prog}` under `msil_pipeline=lto` with `msil_equiv=1` (and the default `msil_link_output=pe`): the `lto`, `programs-lto` and `programs-lto-wsl` legs (not `lto-wsl`: O4 over its paths runs in `lto`, and for the Linux triple in the Linux CI job's `lto` leg), `msil/test/M2/07-equiv/o4-*.test` |
| O6 | the PEVerify `/md` messages of the image that runs, each token replaced by the row it names, are among those of ilasm's image of the `-S` text | with O3 (`msil-verify.py --md-baseline`), Windows only |

O3 and O4 compare an extra image that `msil-prog.py --equiv-out` links with
`-Wl,--no-gc-sections` from the same objects or bitcode, never the image that runs, because
`-S` and `--lto-emit-asm` keep every function and variable that code generation emitted, while
the default link drops the unreferenced ones. `canon` is `%msil_canon` (`llvm-cil-dump
--canonical`, section 6) with the normalizations that the comparison allows, which
`msil/utils/equiv-allowed.txt` names, one per line, `<name> <option>... # <justification>`:

| Entry | Option | Allowed in | Why |
|---|---|---|---|
| `assembly-hash` | `--assembly-hash=ignore` | O3, O4 | `msil-ld` writes the hash algorithm SHA1, as the C# compiler does; ilasm writes none |
| `ilasm-params` | `--params=drop` | O3, O4 | ilasm adds a parameter row per parameter; the compiler and `msil-ld` write none |
| `ilasm-initlocals` | `--initlocals=ignore-without-locals` | O3, O4 | ilasm sets `InitLocals` on small bodies without locals |
| `snan-quiet` | `--snan=quiet` | O3, O4 | ilasm quiets float32 signaling NaNs; the encoder and `msil-ld` keep their bits |
| `pinvoke-module` | `--pinvoke-module=strip` | O3 | `-S` binds P/Invokes through the D14 table, `msil-ld` through the native libraries |
| `pinvoke-access` | `--pinvoke-access=ignore` | O3 | `-S` prints standalone P/Invokes `public`, `msil-ld` writes `assembly` stubs |
| `scopes` | `--scopes=strip` | O3, O4, only when `msil_references` binds reference packs (M21) | the text names `[mscorlib]` |

A line `skip <oracle>[@<triple>][:<level>] <test path> <reason>` (oracle `O3` or `O4`, and
since M21 `O6`) names a test whose difference no normalization removes (a path that ends in
`/` covers a directory). For O3 and O4 the test then builds no equivalence image and
`msil-equiv.py` prints `<oracle> skipped: <reason>`; for O6, `msil-verify.py --md-baseline
<image> --equiv-skip <file> --test <path> --triple <t> --level <L>` (what the composites run)
prints `O6 skipped: <reason>` instead of comparing the PEVerify messages. Every other check of
the test still runs, and each line covers only its own oracle. A line that names an MSIL triple
(`@<triple>`) or a level (`:<level>`, one of `O0`, `O1`, `O2`, `O3`, `Os` and `Oz`) covers the
test only for that triple or level, for example `skip O3@x86_64-pc-windows-msvc-msil:O2
msil/test/M1/11-programs/crc32.c ...`; a line that names neither covers every triple and level.
msil/test/README.md lists every line of the file. Since M25 eight of them skip O4 for the tests
of `msil/test/M3/03-helpers/` that call helper-archive functions, which have no
`--lto-emit-asm` text (plan 5.14.3 A9; section 3, "The helper archive"), one line per test:
six for every triple, and those of `powi.test` and `longdouble.test` for
`x86_64-unknown-linux-gnu-msil` only (`skip O4@x86_64-unknown-linux-gnu-msil`), because on
windows-msvc these two call the UCRT, as native code does, and pass O4; `bitint-abi.test`,
which calls no helper, keeps O4. No line skips O3 or O6 there: every test of the directory is a
`%{msil_prog}`, and O3 and O6 run only for `%{msil_c}` and `%{msil_ll}`. In the reference legs
(`msil_references` other than `none`, M21) O3 and O4 also allow the entry `scopes`, because
`-S` and `--lto-emit-asm` text name `[mscorlib]` while the image names the assemblies of the
pack.

```powershell
# Compare two images with the O3 allowances (exit 0 equal, 1 different, 2 usage error).
python msil/utils/msil-equiv.py --canon "build/bin/llvm-cil-dump --canonical" `
  --allowed msil/utils/equiv-allowed.txt --oracle O3 --allow assembly-hash --allow ilasm-params `
  --allow ilasm-initlocals --allow snan-quiet --allow pinvoke-module --allow pinvoke-access `
  equiv/prog.exe asm/prog.exe
```

`msil-equiv.py` (standard library only) runs the `--canon` command on both files with the
options of every `--allow`ed entry, compares the two listings and prints the first 40
differing lines; an `--allow` that is not an entry of the file is a usage error. With
`--test <path>` it honours the file's `skip` lines for `--oracle` (default O3), matching
qualified lines against `--triple <t>` and `--level <L>` (which the composites pass, as they
pass `msil-prog.py --equiv-skip` its own `--triple` and `--level`), and an operand
that ends in `.il` is first assembled with `--ilasm "<command>" -exe`, which is how O4's text
side is built. In lit, `%msil_equiv` is that command with `--canon` and `--allowed` filled in,
and the composites add the allowances of their oracle. The self-tests in
`msil/test/harness/equiv/` build, for each entry, a pair of images that differ only in what
that entry removes and check that the comparison fails without the entry and passes with it
(every other entry allowed), plus an unreferenced function and variable that only the
`--no-gc-sections` image keeps (`no-gc-sections.test`), an unknown entry (`unknown-name.test`,
exit status 2) and the `skip` lines: O3, O4 and directory lines, lines qualified by a triple, a
level or both, and malformed qualifiers, which are usage errors (`skip.test`).

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
| `CIL object files are not supported for MSIL target '...' yet; use -flto to write LLVM bitcode objects, or -S to write ILAsm text` (before M13: `emitting object files is not yet supported for MSIL target`) | a build from M13 to M18, given `-fno-lto -c`: there were no CIL object files before M19 (since M19 `-c` and `-fno-lto -c` write them, section 3). Before M14 every `-c` and every link stopped here |
| `MSIL object file emission is not implemented; use -S (ILAsm text)` (llc) | `llc -filetype=obj` with a build from before M19, whose llc wrote ILAsm text only; since M19 it writes a CIL object (section 3, "CIL objects") |
| `LLVM ERROR: assembly parsing is not supported for MSIL` (`llvm-mc`), `error in backend: assembly parsing is not supported for MSIL` (`clang -cc1as`) | `llvm-mc` or `clang -cc1as` was given an MSIL triple: there is no MSIL assembly parser, because ILAsm text goes to ilasm and CIL objects come from the code generator. On Windows `llvm-mc` then also prints LLVM's bug-report banner and a stack dump, which mean nothing here. Before M18 `llvm-mc -filetype=obj` crashed and `cc1as -filetype obj` asserted instead |
| `msil-ld: error: ThinLTO, --lto-partitions and LTO without --lto-emit-asm need CIL object output, which this msil-ld does not have; use --lto-emit-asm (clang: -Wl,--lto-emit-asm) to write ILAsm text` | a build from M14 to M19, whose `msil-ld` wrote only the ILAsm text of the whole program: every link needed `-Wl,--lto-emit-asm` and `ilasm -exe`, and ThinLTO bitcode and several LTO partitions were not linked. Since M20 `msil-ld` writes the image itself (section 3). M13's stub said `linking is not implemented yet` for every link |
| `msil-ld: error: undefined symbol: <name>` | neither an input, a library assembly (M22) nor a native library of the link defines `<name>`; a `-shared` link must resolve every symbol too (link a cycle of libraries through an interface library, section 3, "Libraries"). Add the library that defines it (`-l<name>`, a library file, the right `--sysroot`; `-Wl,--print-imports` shows what bound where). The `>>> referenced by <file>:(<function>)` lines name the live references (for bitcode after LTO), and `ld-temp.o` a call that the optimizer introduced. `undefined hidden symbol` is the same for a hidden reference. Before M15 every function that was not a Phase-1 import (D14) ended here |
| `msil-ld: error: unable to find library -l<name>` | no `-L` directory has the library: often one of the driver's default libraries (section 3). On Windows link from a Visual Studio developer prompt (`LIB`); for a Linux triple on Windows add `--sysroot=<a Linux sysroot>` (section 7's E7 or glibc sysroot), or `-nodefaultlibs` when the program calls no native function and no runtime helper (it drops the helper archive too, section 3). After `-Wl,-Bstatic` only static libraries (archives with bitcode members) count; a native archive, a library of the other OS's object format (a native DLL or a COFF import library for a Linux triple) and a .NET assembly are skipped (`-Wl,--verbose` says so) |
| `msil-ld: error: unable to find library from dependent library specifier: <name>`, `>>> requested by <file>` | a `#pragma comment(lib, "<name>")` (or another linker option in the bitcode) names a library that no `-L` directory has: add its directory, or link with `-Wl,--no-dependent-libraries` |
| `msil-ld: error: native function 'qsort' (<module>) takes a C function pointer; passing C functions to native code is not supported for MSIL yet` (before M24: `... passing functions to native code needs phase 3 thunks`) | the program calls, or takes the address of, a native function of the callback list (53 names since M24: `qsort`, `bsearch`, `signal`, `pthread_create`, `CreateThread`, `FlsAlloc`, the registration functions behind `atexit` such as `__cxa_atexit` and `_crt_atexit`, ...; Status.md section 2), whose native code could call back into the program; native code cannot call a C function of an MSIL program before M28. The message names the symbol that glibc's headers make of the call: `signal` is `__sysv_signal` under `-std=c99`, `c11` or `c17`, and `ftw`, `nftw` and `scandir` are `ftw64`, `nftw64` and `scandir64` with `-D_FILE_OFFSET_BITS=64`. Write the function in C (a `qsort` of your own wins over the library's), or, when no function of the program is passed (`signal(SIGPIPE, SIG_IGN)`), declare it with an explicit `import_module` |
| `msil-ld: error: native function 'printf' (<module>) is variadic; calls of native variadic functions are not supported for MSIL yet (phase 3)` | a build from before M26, which could not call native variadic functions; since M26 `msil-ld` binds them to their `va_list` variants (`printf` to `vprintf`) or calls them with fixed arity (section 3, "Variadic functions, `printf` and VLAs"). The text without ` (phase 3)` remains a backstop for a variadic call that the compiler left variadic (a prototype-less or fixed-arity declaration), which should not happen with C from clang: please report it |
| `msil-ld: error: native function '<f>' (<module>) is variadic and has neither a va_list variant nor an entry in the fixed-arity list` | (M26) a native variadic function that `msil-ld`'s tables do not know (`clone`, a variadic function of your own native library, `printf` in a windows-gnu link against the UCRT): call a non-variadic function or its `v` function with a `va_list`, or declare it with an explicit `import_module`, which is called with fixed arity per call site |
| `msil-ld: error: native function '<f>' (<module>) is variadic, and its va_list variant '<vf>' is not defined by the native libraries` | (M26) the link's C library lacks the variant (an old or partial C library, a sysroot without it): link the library that defines `<vf>` |
| `msil-ld: error: the address of native variadic function '<f>' (<module>) is not supported for MSIL yet` | (M26) the program takes the address of a natively bound variadic function (`p = printf;` on Linux); call it directly, or wrap the call in a variadic function of your own, until M28 |
| `msil-ld: error: fixed-arity call of '<name>' in <file> binds to the variadic definition in <file2>, which MSIL cannot call with fixed arity` | (M26) a call of a name on the fixed-arity list (`open`, `_open`, ...) binds to a variadic definition in the program or a library assembly instead of the C library: rename the definition or define it with the fixed parameters |
| `passing a floating-point value to native variadic function '<f>' is not supported for MSIL: ...` or `passing a structure by value to native variadic function '<f>' is not supported for MSIL` | (M26) a call of a fixed-arity native (`ioctl`, `fcntl`, an explicit import, ...) passes a `double` on an x86_64 Linux triple, or a structure by value on any triple: pass a pointer, or an integer holding the value's bits |
| `a dynamic stack allocation that can repeat within one call (a VLA or alloca in a loop) is not supported for MSIL yet` | (M26) a VLA declared inside a loop, or `alloca` called in a loop: allocate outside the loop, or with `malloc`, until M32. Until M26 every VLA and `alloca` was `dynamic alloca / VLA ...` (`phase 3 shadow stack`) |
| `warning: the 'L' length modifier makes the C library read a 16-byte native 'long double', but 'long double' is an 8-byte 'double' on this MSIL target` (for `scanf` formats an error: `... store a 16-byte native 'long double' into an 8-byte 'double' ...`) | (M26) a `%Lf` (or another `L` floating conversion) on a triple whose native `long double` is wider than MSIL's (x86_64 Linux and MinGW, aarch64 Linux): use `%f` with `double` |
| `msil-ld: error: native function '<name>' (<module>) takes or returns a value that a P/Invoke cannot pass natively yet (phase 3)` | the native function passes a struct by value, an `__int128`, a vector or a `_Float16`; wrap it in a native function that takes pointers. Since M25 a call of `cexpl` or another `long double _Complex` function on x86_64 and aarch64 Linux and x86_64 MinGW gives it for the `double` function (`cexp`, which returns a structure), to which the helper archive forwards the call (section 3, "The helper archive"). Until M25 a native `long double` function (`sqrtl`, `strtold`, `nexttoward`, ...) was this error on those triples too; now the archive forwards it, or it is the next row's error |
| `msil-ld: error: native function '<name>' (<module>) takes or returns a native 'long double', which is not supported for MSIL; the helper archive clang_rt.builtins forwards the 'long double' functions that have a 'double' counterpart`, with `>>> referenced by` lines | (M25) on x86_64 and aarch64 Linux and x86_64 MinGW, whose native `long double` is x87 or binary128 and not the `double` that `long double` is in MSIL (Status.md section 3), a native `long double` function that no member of the helper archive defines: `fromfpl`, `fromfpxl`, `ufromfpl` and `ufromfpxl`, whose `double` functions' types differ by C library; glibc's `_Float64x` and `_Float128` functions (`sqrtf64x`, `strtof64x`, `sqrtf128`, ...); and every `long double` function (`sqrtl`, `strtold`, ...) in a link without the archive (not built, `-nodefaultlibs`, or an `msil-ld` command that names none). Call the `double` function (`sqrt`, `strtod`), which is the same function in an MSIL program, or build the archive (section 2) or pass its path. The text is final, not a phase-3 gap. windows-msvc and `aarch64-w64-windows-gnu`, where `long double` is `double` natively, bind the functions as any other |
| `msil-ld: error: importing native data symbol '<name>' (<module>) is not supported yet` | a build from M15 to M19: native variables are imported since M20 (section 3, "Native libraries and P/Invokes"). `'<name>' in <module> is data, but <file> calls it as a function` is a function declaration that binds to native data (a call skips a COFF `DATA` or `CONST` import and a DLL's data export, and binds the function of a later library when there is one) |
| `msil-ld: error: data symbol '<name>' in <module> cannot be imported: no reference defines System.Runtime.InteropServices.NativeLibrary and the native libraries define no dlopen/dlsym` | the program reads a variable of a native library, which a Linux link imports through `dlopen` and `dlsym`, but no library of the link defines them: link the C library (drop `-nodefaultlibs`; glibc 2.34 and later has them in `libc.so.6`, older ones in `libdl.so.2`, `-ldl`), or, since M21, link against reference assemblies that define `NativeLibrary` (the .NET 8 or 10 pack, section 3), whose `Load` and `GetExport` then import the variable |
| `msil-ld: error: data symbol '<name>' in <module> cannot be imported: --lto-emit-asm text is never bound to the references, and the native libraries define no dlopen/dlsym` | the same under `--lto-emit-asm` in a link with references: the text keeps M20's `dlopen`/`dlsym` helper even when the references define `NativeLibrary` (section 3, "Reference assemblies"). Link the C library, or write the image instead of text, whose helper then calls `NativeLibrary` |
| `System.TypeInitializationException` with `System.EntryPointNotFoundException: data import '<name>' not found in <module>` | at start-up, the module initializer could not load the library of a native variable, or the library lacks it. On Linux `dlopen` looks a library of your own up by its `DT_SONAME` on the loader's path only, not next to the program: set `LD_LIBRARY_PATH` (Status.md section 3, "Limits"). An image bound to the .NET 8 or 10 pack (M21) imports through `NativeLibrary` instead, and the inner exception is the `System.DllNotFoundException` or `System.EntryPointNotFoundException` of `NativeLibrary.Load` or `GetExport` |
| `msil-ld: error: symbol '<name>' is defined only in native object code (<archive>(<member>)); MSIL cannot link machine code` | the only definition is machine code in a native archive (glibc's `atexit` in `libc_nonshared.a`, a static library of native objects), or, since M22, the definition is in a native object file named on the command line (`(<file>)`), which counts even when a native library or a library assembly also defines the name, because a native linker always links such an object: build that code for the MSIL triple, or link a shared library or DLL of it instead. An oldnames-style alias object on the command line (`strdup` as a weak alias of `_strdup`) binds through its alias, as inside `oldnames.lib`. A `/alternatename:<name>=<other>` linker option does not redirect such a name (since M23 neither when the machine code is a native archive's member), because lld-link links that definition first |
| `msil-ld: error: symbol '<name>' in <module> has only the non-default version <name>@<ver>, which a P/Invoke cannot bind`, `... is thread-local data and cannot be imported` (for `errno` followed by `; use <errno.h>`), `... is <ver> (GLIBC_PRIVATE) and cannot be bound` | the library defines the name only in a form that `dlsym` does not return: a hidden version, thread-local data (`errno`: include `<errno.h>`, whose `errno` calls `__errno_location`; other thread-local data, such as `h_errno` or a library's own `__thread` variable, gets the text without the advice), or glibc's private interface |
| `msil-ld: error: <file>: ELF machine <EM_x> does not match target <triple>` (also `COFF machine` and `PE machine`) | a native library of another architecture (or ELF class) than the triple: an AArch64 or 32-bit library in an x86_64 link, say. Point `-L` or `--sysroot` at the triple's libraries |
| `msil-ld: error: <path>: ELF file cannot be imported for target <triple>`, `<path>: PE/COFF file cannot be imported for target <triple>` | a native library of the other OS's object format, given as a file, by `-l:<file>` or by a linker script: an ELF `.so` in a link for a Windows triple, a DLL or an import library in a link for a Linux triple. As lld-link and ld.lld do, `msil-ld` reads PE/COFF libraries for a Windows triple and ELF ones for a Linux triple (section 3): a P/Invoke of the other kind would fail to load at run time. Link against the triple's libraries; `-l<name>` passes over such a file (`-Wl,--verbose`: `skipping <path>: ...`) |
| `msil-ld: error: <script>:<line>: there is a cycle in linker script inputs` | an `INPUT` or `GROUP` name or a `-l<name>` of a GNU ld script (the `>>>` lines show it) leads back to a script that is still being read: a script that names itself, or two scripts that name each other, directly or through `-l`. That input is skipped; remove it. A script read again after it was read to its end (`-lc` given twice) is no cycle |
| `msil-ld: error: --ucrtbase-imports needs a Windows target, not <triple>` | `--ucrtbase-imports` (the driver passes it on windows-msvc) given for a Linux triple |
| `msil-ld: warning: <path>: skipping the .NET assembly that -l<name> found; use --reference for .NET reference assemblies` | `-l` found a managed `.dll` without this toolchain's symbol table. Give reference assemblies with `-Wl,--reference=<path>` or as link inputs (M21, section 3); `-l` takes only the libraries that this toolchain built (M22, section 3, "Libraries") |
| `msil-ld: error: library call '<name>' was not resolved by msil-ld` | code generation calls a library function that no native library of the link defines: `floor`, `ceil` or `round` (`llvm.floor` and its kin at `-O2`), `sqrt` or `exp` under `-fno-math-errno`, ..., typically in a link with `-nodefaultlibs` or without the C or math library. Add that library (drop `-nodefaultlibs`, or add `-lm`, `-lc` or the library file) |
| `msil-ld: error: undefined symbol: <name>` followed by `>>> a compiler runtime helper: link the toolchain's helper archive clang_rt.builtins`, `>>> a compiler runtime helper that the toolchain's helper archive clang_rt.builtins does not define` (M25), `>>> an atomic operation: link with -latomic, as a native build does`, `>>> an atomic operation (libatomic's interface)` or `>>> a toolchain helper: link the toolchain's helper archive clang_rt.builtins; if it is linked, the object needs a newer toolchain` | (M24, plan 5.14.2) a name that never binds to a native library, which no object or archive member defines. A compiler-runtime helper (`__extendhfsf2`, `__truncsfbf2`, `__muldc3`, the `__sync_*` functions, ...; Status.md section 2, "The helper archive (M25)") comes from the toolchain's helper archive `clang_rt.builtins`, which clang passes after the default libraries when the file exists (section 3, "The helper archive"). The first line means that no archive of that name took part: it is not built, `-nodefaultlibs` or `-nostdlib` dropped it, or an `msil-ld` command does not name it; build it (section 2; `-print-libgcc-file-name` names the file) or pass its path. The second (M25) means that the archive took part and has no such member, such as a `*ti*` helper of `__int128` that a program calls by name (MSIL expands those operations inline, and the archive leaves them out), or `__mulhc3` and `__divhc3`, which clang calls for `_Float16 _Complex` products and quotients that it does not promote to `float` (on the aarch64 triples, or with `-fexcess-precision=16`; section 3, "The helper archive"). Since M25 the ten `_Complex` helpers (`__muldc3`, `__divsc3`, `__mulhc3`, ...) are compiler-runtime helpers too; until then they were ordinary names, which a native library of the link (`libgcc_s.so.1`) could define, and before M24 a link for a Linux triple that included `libgcc_s.so.1` bound every helper there. An atomic operation is an `__atomic_*` call (`_Atomic` variables, the `__atomic_*` builtins, the `volatile` accesses of windows-msvc stock bitcode without `-fno-ms-volatile`, section 3): for a Linux triple link with `-latomic`, which binds it to `libatomic.so.1` as a native build does; for a Windows triple no native library may provide it (atomics: M30). A toolchain helper (`__msil_*`) is defined by `msil-ld`'s `<trailer>` input or the helper archive (M25's defines none); one that neither defines comes from an object of a newer toolchain. The class line comes after `undefined symbol:` and before the `>>> referenced by` lines; in an LTO image link a helper that code generation made is referenced by `<output>.lto.o` |
| `msil-ld: error: <file>: '<name>' cannot be bound to a native library: setjmp and longjmp are lowered by the compiler` | (M24) the program calls `setjmp`, `longjmp` or another name of the family (`_setjmp`, `__sigsetjmp`, `sigsetjmp`, `siglongjmp`, `_longjmp`, `__longjmp_chk`, `_setjmp3`, `_setjmpex`), which only the compiler can implement (M27); once per referencing file and name, under LTO for the bitcode file. Until M24 the Linux object pipeline bound these names to `libc.so.6` without a word, and such programs ran by accident. Only a definition in an object resolves the name; avoid the family until M27 (Status.md section 3) |
| `msil-ld: error: function signature mismatch: <name> (a toolchain or compiler runtime helper)`, `>>> defined as <signature> in <file>`, `>>> called as <signature> in <file>` | (M24) a call of a compiler-runtime helper (since M25 also of the `_Complex` helpers `__muldc3`, `__divdc3` and their kin), of an atomic or toolchain helper, or of a trailer definition (`<trailer>` in the `defined as` line; when the call and the definition are both bitcode, LTO's merged module reports both lines without a file), whose signature differs from the definition's. These functions get no adapter, because the toolchain owns them: a C function of your own with such a name (`__fixdfti` or `__muldc3`, say) and another prototype causes it, so rename it; anything else is a toolchain defect, so please report it with the inputs (`-Wl,--reproduce=<file>.tar`) |
| `msil-ld: error: runtime helper '<name>' needs the helper archive clang_rt.builtins, which only msil-ld links` or `msil-ld: error: runtime helper '<name>' is an atomic operation that only msil-ld links` (M24: `runtime helper '<name>' is not available for MSIL yet (phase 3)`) | only in a `--lto-emit-asm` link (since M24): LTO's code generation needs a helper of the helper archive (a call that code generation creates, such as a `_Float16` or `__bf16` conversion or `__powidf2`; a helper that the program itself calls, such as `__muldc3`, `msil-ld` resolves before code generation, as the `--lto-emit-asm cannot link CIL objects` row below says) or an `__atomic_*` function, which the whole program's text cannot call, because only a link that writes the image takes members from the archive. Link without `--lto-emit-asm`: that link takes the helper from the archive, or reports it as an undefined symbol of `<output>.lto.o` with its class line (the `undefined symbol` row above) |
| `msil-ld: error: undefined function '<name>' was not resolved by msil-ld` | the backend found, in `msil-ld`'s text, a function that the link did not bind, a backstop that `msil-ld`'s own errors normally precede; please report it with the inputs (`-Wl,--reproduce=<file>.tar`) |
| `msil-ld: error: <file>: target triple <t> is incompatible with <t0>` | the inputs were compiled for different MSIL triples (another architecture, OS or environment); compile every file for the triple of the link |
| `msil-ld: error: unsupported target triple '<codegen triple>' for MSIL` | the link's triple (`--target`, else the first bitcode input's with `-msil` appended) is not one of D6's: stock-clang bitcode for Darwin or i686, say (section 3). Compile the inputs for an x86_64 or aarch64 Linux, windows-msvc or windows-gnu triple |
| `msil-ld: error: no target triple: pass --target or a CIL object or bitcode file` | `msil-ld` run by hand on libraries alone (library assemblies and native libraries, with or without `--interface-only`), which give it no triple: pass `--target=<triple>`, as clang always does (M22) |
| `msil-ld: warning: <file>: linking bitcode for <triple> as <triple>-msil; stock-clang bitcode is a compatibility path` | an input is bitcode for the native triple (plan Q12); it links, with the limits of section 3 (on windows-msvc compile it with `-fno-ms-volatile`) |
| `msil-ld: error: <file>: .NET assembly inputs are not supported yet` | a build of M20, given a .NET assembly: since M21 it is a reference assembly (section 3, "Reference assemblies"). A build from M14 to M19 said `<file>: linking CIL object files and .NET assemblies is not supported yet`, also for the CIL objects of `clang -c`, which `msil-ld` links since M20 |
| `msil-ld: error: <path>: linking .NET libraries built by this toolchain is not supported yet` | a build of M21, given a library that this toolchain built: M22's `msil-ld` links it (section 3, "Libraries"). With `--reference` such a library is skipped with the warning `<path> is a library built by this toolchain; pass it as a link input instead` |
| `msil-ld: error: -shared is not supported yet` | a build from M14 to M21: library assemblies came with M22 (section 3, "Libraries") |
| `msil-ld: error: invalid --export-class value '<v>'`, `export class '<c>' would hold <n> methods; the CLR allows 65,524` | the export class (`--export-class`, else the assembly name) is empty after its last `.`, holds a `/`, is longer than 1,023 bytes, or is reserved: a namespace or name that starts with `<msil>` (the prefix of the toolchain's own names) or the value `<Module>`, which would repeat a class that `msil-ld` writes (`Ns.<Module>` is allowed); or the library exports more functions and variables than one CLR type holds (each function is two methods, each variable one): split it into several libraries |
| `msil-ld: error: invalid --assembly-version value '<v>'` | `--assembly-version` takes one to four numbers from 0 to 65535 separated by `.` |
| `msil-ld: warning: assembly name '<n>' differs from the output file name '<f>'; the CLR loads a library only as <n>.dll` | `--assembly-name` or `-soname` names a library other than its file: the CLR finds a library by its assembly name, so rename the file or drop the option |
| `msil-ld: warning: --entry is ignored with -shared` | a library has no entry point; `main` is exported like any other function |
| `msil-ld: error: --interface-only requires -shared` | an interface library is a library; add `-shared` |
| `msil-ld: error: --lto-emit-asm cannot write a -shared library; link without --lto-emit-asm`, `<file>: --lto-emit-asm cannot import from a .NET library; link without --lto-emit-asm` | ILAsm text has no form for a library's export class and initializers, or for calls into another library; let `msil-ld` write the image |
| `msil-ld: error: <file>: C runtime '<c>' differs from '<c0>' of this link`, `<file>: target triple <t> is incompatible with <t0>`, `<file>: toolchain ABI version <n> differs from <m> in <f0>`, `<file>: data model <m> differs from <m0> in <f0>` | a library built for another C runtime (the first of its link's `-l` options named `c`, `msvcrt`, `ucrt`, `ucrtbase`, `crtdll` or `msvcr*`; `none` matches any), triple, toolchain or data model than the link that uses it: rebuild one of them to match; two C runtimes in one process would not share their heap, streams or `errno` |
| `msil-ld: error: <file>: malformed export table: <what>` | the library's symbol table and its metadata disagree (`no export class`, `export class '<c>' is not defined`, `function '<f>' has no accessor '<msil>addr_<f>'`, `no symbol table`, ...): a damaged library or one that another tool rewrote; rebuild it with `clang -shared` |
| `msil-ld: error: <file>: library assembly '<n>' is also linked from <file>`, `<file>: library assembly '<n>' has the name of the output`, each with `; the CLR loads one assembly of a name` | two different files hold the assembly `<n>`, or the library has the output's assembly name, compared without case (`foo.exe` linked against `foo.dll`, or a library linked against an older build of itself), whose references the CLR would resolve to the output; an application can load only one assembly of a name, so rename one of them |
| `msil-ld: warning: symbol '<s>' is exported by <lib1> and <lib2>; using <lib1>`, `symbol '<s>' is defined in <file> and exported by <lib>; the output uses its own definition` | two libraries export one name, or the program defines a name that a library exports. Assemblies have no global symbol scope: the output binds the first library's (or its own) definition, while every library keeps calling its own (no interposition, as with `-Bsymbolic`) |
| `msil-ld: error: <file>: unsupported use of imported symbol '<s>' in '<m>' at IL offset <n>`, `symbol difference '<a>' - '<b>' in <file> refers to a symbol of <lib>` | code that uses a library symbol's address other than by loading it (hand-written object-form text), or data that subtracts the address of a library symbol, which no relocation across assemblies can express |
| `msil-ld: error: <lib> references <name> <version>, newer than the reference <version>`, `msil-ld: warning: <lib> references <name>, which is not among the references` | a library was linked against newer reference assemblies than the program (or against assemblies that the program's link does not have): link both against the same references. The warning also names a library built by this toolchain that a linked library links and the program's link lacks: a program linked against `a.dll` alone, when `a.dll` links `c.dll`, warns about `c`; give `c.dll` to the program's link too, as it must be deployed with it anyway. An AssemblyRef that names another library of the link, or the output itself (the other half of a cycle), is not checked |
| `System.TypeInitializationException` around `System.IO.FileNotFoundException: Could not load file or assembly '<lib>, ...'` at start-up | a library that the program links is not next to it, or (on .NET 8 and later) is an older build than the one the program was linked against: deploy the libraries with the program (section 3, "Libraries") |
| `System.MissingMethodException: Method not found: '... <Ns.Class>.<f>(...)'` in a program that links libraries | a library was rebuilt without the function, or with another signature, after the program was linked: relink the program against the deployed library |
| `error: global constructor '<c>' takes arguments, which MSIL does not pass`, `global constructor '<c>' takes envp, but no native library of the link defines 'environ'` | a constructor with parameters: on the Linux triples only glibc's `(int argc, char **argv, char **envp)` and its prefixes are passed (since M22; `envp` needs a C library that defines `environ`), on the Windows triples none, as the MSVC C runtime calls constructors without arguments |
| `LLVM ERROR: Target does not support the kernel CodeModel` from `msil-ld` | a build before M22, linking bitcode whose `"Code Model"` module flag is kernel (rustc for `x86_64-unknown-none`); since M22 LTO ignores the code model (section 3, "Rust bitcode") |
| `msil-ld: error: <path>: not a .NET assembly` | `--reference` names a file that is not a .NET assembly (a native DLL, a CIL object, a netmodule). A directory's other files are skipped silently |
| `msil-ld: error: the references contain no core library (an assembly that defines System.Object and references no other assembly); given: <list>` | the references hold no core library: only facades (a pack's `mscorlib.dll` or `netstandard.dll`, noted `(facade, forwards System.Object to <name>)`), or assemblies that need another. Give the whole pack directory, or its `System.Runtime.dll` (.NET) or `mscorlib.dll` (.NET Framework). `given: no references` means that the `--reference` options added no assembly at all: a directory without `.NET` `.dll` files, most often a pack's version directory (`.../Microsoft.NETCore.App.Ref/8.0.33`) instead of its `ref/net8.0` (section 3); a `--reference` never falls back to the built-in table |
| `msil-ld: error: the references mix two core libraries: <identity> (<path>) and <identity> (<path>)`, `the references mix <n> core libraries: <list>` | references of two worlds, such as the .NET Framework's `mscorlib.dll` and a .NET pack's `System.Runtime.dll`: link against one of them. Three or more core libraries give the second form, `<n>` in digits and `<list>` each as `<identity> (<path>)`, joined by `, ` with ` and ` before the last |
| `msil-ld: error: references <path> (<identity>) and <path> (<identity>) have the same name but different versions or keys` | two versions of one assembly, from two packs, say; `msil-ld` does not unify versions. Keep one. Two copies of the same file only warn (`<path> has the same identity as <path> (<identity>); using the first`) |
| `msil-ld: error: references <path> and <path> have the same identity (<identity>) but different contents` | two different files claim one identity, as the .NET Framework's `v4.8/mscorlib.dll` and a .NET pack's `mscorlib.dll` facade do (both `mscorlib 4.0.0.0 [b77a5c561934e089]`) when both directories are given: references of two worlds. Link against one of them; the error does not depend on the order of the references |
| `msil-ld: error: type '<Ns.Type>' is not defined in the references (<n> assemblies)`, `type '<Ns.Type>' is forwarded by <identity> to <assembly>, which is not among the references` | no reference defines a type the output needs, or only a facade forwards it: add the assembly that defines it, or the whole pack directory (`Marshal` is in `System.Runtime.InteropServices.dll` of the .NET packs, `Interlocked` in `System.Threading.dll`) |
| `msil-ld: error: type '<Ns.Type>' is defined in two references: <identity> (<path>) and <identity> (<path>)`, `type '<Ns.Type>' is defined in <n> references: <list>` | two references define the type, or three or more (the second form, with the list of the core-library error above); keep one, whatever the order (`msil-ld` binds definitions only, as Roslyn does) |
| `msil-ld: error: undefined .NET member '<signature>' (requested by <file>); '<Ns.Type>' is defined in <identity> (<path>) but has no such member; overloads present: <list>` | the defining assembly lacks a member with exactly that signature (return type, calling convention and parameters; both the request and the overloads print the convention before the return type, `instance`, `explicit`, `vararg`): references of an older or a different framework. Constructors and fields are looked up on the requested type only (the runtime resolves neither through a base type), and a public method of a base type does not bind when the requested type, or a type in between, has a non-public method with its name and signature (listed with ` (not public)`), which the runtime would resolve the reference to (Status.md section 2). `.NET member '<signature>' (requested by <file>) matches several members of '<Ns.Type>' in <identity> (<path>) when custom modifiers are ignored: <list>` is two candidates that differ only in custom modifiers |
| `msil-ld: error: .NET member '<signature>' is not in msil-ld's built-in mscorlib table; pass --reference=<reference assemblies>` | a link without references needs a member outside the built-in table (`msil-ld --print-bcl-table` lists it; section 3): link against reference assemblies |
| `msil-ld: warning: <path> is not a reference assembly; binding '<Ns.Type>' to <identity> ties the output to one runtime build` | `--reference` names a runtime's own directory or `System.Private.*` assembly: use the reference pack (`<dotnet>/packs/Microsoft.NETCore.App.Ref/<v>/ref/net<N>.0`) instead |
| `msil-ld: warning: '<member>' is obsolete in <identity>`, `msil-ld: error: '<member>' requires preview features` | the bound member carries `[Obsolete]`, or `[RequiresPreviewFeatures]` (on it, its type or its assembly), which `msil-ld` refuses |
| `msil-ld: warning: <archive>(<member>): .NET assemblies in archives are not linked; ignored` | an archive member is a .NET assembly: give reference assemblies with `--reference` or as link inputs |
| `System.IO.FileNotFoundException: Could not load file or assembly 'System.Runtime, Version=8.0.0.0, ...'` (.NET Framework), `System.MissingMethodException: Entry point not found in assembly` (.NET 8, for an image bound to the .NET 10 pack) | the image was linked against a .NET reference pack newer than the runtime, or against any .NET pack for the .NET Framework (section 3, "Reference assemblies"). Link against the .NET Framework 4.8 reference assemblies, or without references, for an image that runs everywhere |
| PEVerify: `[HRESULT 0x80070002] - The system cannot find the file specified.` after `.NET Framework Internal error: 0x8013129d` messages | PEVerify cannot load an image bound to a .NET 8 or later pack; verify it with ILVerify against the pack (`msil-verify.py --runtime-dir <pack> --skip-peverify "<reason>"`, section 6) |
| `msil-ld: error: <file>: not a CIL object (no link-info record)`, `<file>: malformed CIL object: <rule>: <detail>`, `<file>: invalid link-info record: ...` | the input is a netmodule that no MSIL compiler wrote (a C# netmodule, an object that went through ildasm and ilasm), or a damaged CIL object; rebuild it with `clang -c` or `llc -filetype=obj` |
| `msil-ld: error: <file>: reference to assembly '<n>' needs --reference, which this msil-ld does not support yet` | a build of M20, given a CIL object that names a .NET assembly other than the base class library (hand-written object-form text, say): it bound only the base class library, to `[mscorlib]`. Since M21 such a type binds by its name against the references |
| `msil-ld: warning: function signature mismatch: <f>`, `>>> defined as ... in <file>`, `>>> called as ... in <file>`, `>>> adapter: ...` | a call does not match its definition (a K&R call with an extra or a missing argument, an `int` where the definition takes a `long long` or a pointer), undefined behaviour in C that native x86-64 code survives: `msil-ld` calls the definition through an adapter that drops, zero-fills or converts as native code would. Give the function a prototype. Under LTO the warning has no file names (section 3) |
| `msil-ld: error: function signature mismatch: <f>` with a reason line such as `>>> argument 1 is float32 at the call but int32 in the definition (different native register classes)` | a call that no adapter can save: integer against floating point, `float` against `double`, an `sret` result on one side, `byval` parameters that differ; natively the callee reads another register or memory. Fix the declaration. Since M26 a variadic side matches only a variadic side with the same fixed parameters and IL signature, and the lines then say `(variadic, <n> fixed)`; until M26 any variadic side was `variadic functions are not supported yet: <f>` |
| `msil-ld: error: undefined symbol: __start_<sec>` (or `__stop_<sec>`) on a Windows triple | link.exe and lld-link define no `__start_`/`__stop_` symbols, so neither does `msil-ld` there. Do as native Windows code does: put marker variables in the sections `<sec>$a` and `<sec>$z`, which the grouping sorts around the entries in `<sec>$m`. `--gc-sections` keeps the entries, as link.exe and lld-link do, because a named section outside a COMDAT group is always kept on the Windows triples; under `-flto` mark every entry `__attribute__((used))`, because LTO drops the unreferenced ones first, as native LTO does. On the Linux triples the section's name must be a C identifier |
| `msil-ld: error: global destructors are not supported yet: <s> in <file>`, `section '<name>' in <file> is not supported`, `thread-local storage is not supported yet: <s> in <file>`, `constructor table entry at <section>+<off> in <file> is not a function address` | a section that MSIL cannot lay out: a destructor table (`.fini_array*`, `.CRT$XT*`), `.ctors`/`.dtors`, `.preinit_array*` (Linux triples; ld.so runs it before every `.init_array` entry, which MSIL cannot do yet) or another `.CRT$X*` section, thread-local data, or an entry of a constructor table that holds no function address (also a null one in `.init_array`; since M21 a null entry of `.CRT$XC*` is skipped on the Windows triples, as the MSVC CRT skips it). Destructors and thread-local data come with phase 3; use `__attribute__((constructor))` or `.init_array`/`.CRT$XCU` for constructors (Status.md section 3). `-S` and llc give the same texts with the source file's name |
| `msil-ld: error: relocation refers to a symbol in a discarded section: <s>` | a reference to a symbol of a COMDAT copy that lost to another input's copy and that the winning copy does not define, as in lld |
| `msil-ld: error: unsupported signature for <entry>` | the entry point (`main`, or `-e <sym>`) has none of D27's shapes, `int f(void)` and `int f(int, char **)`; a narrow integer result (`short`, `char`, `_Bool`) counts as `int`, and an `int` with a pointer-sized integer builds `argv` as `(int, char **)` does |
| `msil-ld: error: <file>: --lto-emit-asm cannot link CIL objects; link without --lto-emit-asm`, `ThinLTO and --lto-partitions cannot write ILAsm text; link without --lto-emit-asm` | `--lto-emit-asm` writes the text of one LTO module of bitcode inputs; drop it and let `msil-ld` write the image. Since M25 `<file>` is also a member of the helper archive, `<archive>(<member>)` (`clang_rt.builtins.lib(muldc3.c.o)`, say): `--lto-emit-asm` resolves the helpers that the program's bitcode calls (`__muldc3` of a complex product) before code generation, as an image link does, and the archive that clang passes defines them in CIL objects, so such a program has no text; compile it with `-c` and link it with clang (section 3, "The helper archive") |
| `msil-ld: warning: <path>: the output name ends in neither .exe nor .dll; msil-ld writes an EXE image` (with `-shared`: `a DLL image`) | the image is an executable (with `-shared` a library) whatever its name; name it `.exe` (a library `.dll`) |
| `msil-ld: warning: <path>: this executable is bound to System.Runtime 8.0.0.0, so it runs only on .NET through 'dotnet' and cannot start on .NET Framework; .NET executables are named .dll (for example -o <stem>.dll)` (also `System.Private.CoreLib`, `netstandard` 2.1, and other versions) | the program was linked against a .NET reference pack (or a runtime directory, or `netstandard` 2.1 or later, which the .NET Framework 4.8 lacks) and named `.exe` (M23): the .NET Framework cannot start it, and .NET names such executables `.dll`. Rename it as the warning says and run it as `dotnet <stem>.dll` with `<stem>.runtimeconfig.json` (section 5, "Executable names"); the image does not change. For an `.exe` that also runs on the .NET Framework, link without references or against the .NET Framework 4.8 reference assemblies |
| `msil-ld: error: <file>: data model <m> differs from <m0> in <f0>` | the inputs disagree on the sizes of `void *`, `long`, `wchar_t` or `long double`, for example bitcode compiled with `-fshort-wchar` (found through the object that LTO writes, `<output>.lto.o`): compile every file with the same options |
| `LNK1302: only support linking safe .netmodules; unable to link pure .netmodule` (`lib.exe`), `not a COFF object, bitcode, archive, import library or resource file` (`llvm-lib`), `unknown file type` (`lld-link`, `ld.lld`) | a CIL object (from `clang -c` since M19) given to a native librarian or linker, which cannot use a netmodule. Archive CIL objects with `llvm-ar` and link them with `msil-ld` (since M20) |
| `alias '<a>' must point to a definition plus a constant offset for MSIL` | `clang -c` or `llc -filetype=obj` (M19): a CIL object keeps an alias as a symbol only when it names a definition of the file, a function at offset 0 or a variable at a constant offset; an alias of a declaration, an offset into a function or an alias of an ifunc cannot be one (Status.md section 2, "CIL objects") |
| `comdat '<c>' uses selection kind '<k>', which MSIL does not support` | a CIL object (M19) carries COMDAT groups of the selections `any` (what C gives: MSVC string literals, `selectany` data) and `nodeduplicate` only; `largest`, `exactmatch` and `samesize` come from other front ends or hand-written IR |
| `symbol '<name>' starts with '<msil>', which MSIL reserves for the names it generates` | a CIL object (M19) cannot hold a symbol of that name: the prefix belongs to the object's placeholders and to what `msil-ld` generates. Rename the symbol |
| `warning: calls to '<f>' pass incompatible arguments; the object records the first call's layout` | two calls of the undefined function `<f>` with the same IL signature that pass their arguments differently (a struct by value, `byval`, or an `sret` result in one call and a plain pointer in the other), undefined behaviour in C (C11 6.5.2.2p9): the CIL object records the first call's layout for both. Declare `<f>` with a prototype |
| `MSIL: link-info token mismatch for '<name>'`, or another `MSIL: ...` error about a CIL object's rows | the object writer found that the record and the metadata it wrote disagree, and wrote no object: a backend bug. Please report it with the IR (`-S -emit-llvm`) |
| `assembling ILAsm text is not supported for MSIL target '...'; compile C or LLVM IR sources instead, or use -S to write ILAsm text` | an assembler input (`-x assembler`, a `.s` or `.S` file) or an assemble step (`-via-file-asm`, clang-cl `/FA` or `/Fa`): ILAsm text goes to ilasm, not to clang (sections 3 and 4); use `-S` for the IL |
| `invalid linker name in argument '-fuse-ld=...'` | MSIL links run only `msil-ld`: use no `-fuse-ld`, `-fuse-ld=lld`, an absolute path, or `--ld-path=` (section 3) |
| `undefined function 'f' is not defined in this module and is not a Phase-1 import` | with `-S` and `llc`, every function must be defined in the same file, except `puts`, `putchar` and the C math library (Status.md) and functions declared with `import_module`, optionally with `import_name` for the entry point (section 3; `import_name` alone needs `msil-ld`). Functions of other files and of native libraries link with `msil-ld` (section 3) |
| `runtime helper '__extendhfsf2' needs the helper archive clang_rt.builtins, which only msil-ld links` or `runtime helper '__atomic_load_4' is an atomic operation that only msil-ld links` (until M25 both `runtime helper '<name>' is not available for MSIL yet (phase 3)`, before M24 also for `'__fixdfti'`); `runtime helper '__divti3' is not supported for MSIL: the helper archive clang_rt.builtins has no 128-bit integer helpers` | (M25) text output (`clang -S`, llc; `msil-ld --lto-emit-asm` is the `msil-ld` row above) of code that calls a helper which only `msil-ld` links, from the helper archive (section 3, "The helper archive"): `_Float16` arithmetic and conversions (`__extendhfsf2`, `__truncsfhf2`, also to and from `__int128`), `__bf16` arithmetic and the rounding conversions from `float`, `double` and `__int128` to `__bf16` (`__truncsfbf2`, `__truncdfbf2`), C `_Complex` multiplication and division (`__muldc3`, `__divdc3`, ...; until M25 D14's undefined-function error), `__builtin_powi` on Linux triples (`__powidf2`), or an atomic operation (`__atomic_*`; atomics come in M30). Compile with `-c` and link with clang, whose link takes the helper from the archive (no archive defines `__mulhc3` and `__divhc3`, the `_Float16 _Complex` helpers: the `undefined symbol` row above). The third text is, in every output mode, objects included, an `__int128` division or remainder that llc's `-expand-div-rem-bits=128` leaves to instruction selection, or a conversion from floating point to `__int128` that `-expand-fp-convert-bits=128` leaves there (`__divti3`, `__fixdfti`, ...): drop the option. In text output it also names a call of one of compiler-rt's `*ti*` helpers of `__int128` (`__floattidf`, `__floatuntisf`, `__divti3`, `__multi3`, ...), which no archive defines, whether the module declares and calls it by name or such an option leaves a library call (a conversion from `__int128` to floating point under `-expand-fp-convert-bits=128`); in a CIL object that call is an undefined symbol, which `msil-ld` reports with the compiler-runtime line of the `undefined symbol` row above (with the archive on the link line, `... does not define`). `__int128` division and the ordinary conversions are expanded inline since M12, the strict (constrained) conversions between floating point and `__int128` since M24, and `__bf16` parameters, results and the extension to `float` need no helper since M24, so an older build also names `__divti3`, `__udivti3`, `__fixdfti`, `__floattidf`, `__extendbfsf2`, ... here. In a program that `msil-ld` links, see the `msil-ld` rows above |
| `library call '<name>' does not match the type of the function of that name in this module`, or `function '<name>' is declared as <declared IL signature> but the toolchain defines it as <trailer signature>` (for example `function '__msil_compiler_barrier' is declared as int32(int32) but the toolchain defines it as void()`) | whole-program text (`clang -S`, `llc`, `msil-ld --lto-emit-asm`): the module defines a function with the name of a library call that the backend makes (`sqrtf` for the `llvm.sqrt.f32` intrinsic, say) but with another type, so the call cannot use it. Since M24 each compiler barrier (a single-thread fence, `__atomic_signal_fence`, `asm volatile("" ::: "memory")`) is a call of `__msil_compiler_barrier`, which the module defines itself unless it defines a function of that name and type: a function of your own with that name and another type (the first message, in each function with a barrier), a call of a declaration of another type (the first message, in the calling function) or the address of such a declaration (the second message, which names both signatures) causes it. Rename the function of your own. In a program that `msil-ld` links into an image (from objects, or from the objects that LTO writes), a call of another type is the `function signature mismatch: <name> (a toolchain or compiler runtime helper)` row above, with `>>> defined as void() in <trailer>` (for a definition of your own, its signature and file), and a declaration whose address is only taken is not checked, as for any function |
| `unknown target triple '...-msil'` | Darwin and i686 MSIL triples come in later phases; Windows MSIL triples name `msvc` or `gnu`. Any MSIL triple that is not a Linux, windows-msvc or windows-gnu one (Darwin, FreeBSD, Cygwin, ...) gets this error, also for a command that only links |
| `... is not supported for MSIL yet` | a Phase-1 limitation; Status.md lists them |
| `global variable @p_init in section ".CRT$XCU" is not supported for MSIL yet (phase 2 CIL objects)` | a build from M12 to M19, whose `-S` and llc text rejected every section on a variable. Since M20 they lay named sections out as a one-object link does, constructors registered in `.init_array` or `.CRT$XCU` run, and only the sections of the `section '<name>' in <file> is not supported` row above stay errors |
| `alias @a is not supported for MSIL`, `ifunc @f is not supported for MSIL` | `-S` or llc text: only aliases of a defined function (at offset 0) and of a variable are lowered (Status.md section 2); ifuncs are never supported. A CIL object keeps aliases as symbols instead (the `alias '<a>' must point to ...` row above) |
| `inline assembly is not supported for the MSIL target '...'` (clang) or `inline assembly is not supported for MSIL` (llc) | only empty `asm` statements work: with no operands, or with inputs, read-write (`"+r"`), tied (`"0"`) and memory outputs. An output that is not tied to an input (`"=r"`) gets no value from an empty `asm` and is rejected, and so is a condition-flag output (`"=@ccz"`, `"+@ccz"`), which native code sets from the flags after the statement (Status.md section 2). Since M22 the backend (llc, `msil-ld`'s LTO, the bitcode that rustc writes) also takes a template of nothing but `/* */` comments as empty; clang still accepts only an empty template in C |
| `the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself` (until M26 on every triple `the va_arg instruction is not supported for MSIL yet (phase 3 variadics)`) | LLVM IR with a `va_arg` instruction on a System V or AAPCS64 triple (since M26 the Windows triples expand it); clang never emits one for C, so it comes from another front end or hand-written IR. One of a struct or array type gets the IR verifier's error of the next row instead; this text names it only in IR that skipped the verifier (`llc -disable-verify`), and before M12 such IR crashed llc |
| `va_arg with an aggregate type is not supported` (clang: `invalid LLVM IR input: va_arg with an aggregate type is not supported`; llc follows it with `input module cannot be verified`) | LLVM IR with a `va_arg` instruction of a struct or array type, which the IR verifier rejects on every target since upstream 74a120bff99f (merged at 1838cefa99c0): front ends expand such a `va_arg` themselves, as clang does for C |
| `The library 'hostpolicy.dll' required to execute the application was not found` (Windows) or `The library 'libhostpolicy.so' required to execute the application was not found` (Linux), exit `0x80008083` or 131 | no `<name>.runtimeconfig.json` next to the `.dll` (section 5) |
| `You must install or update .NET to run this application` | the runtimeconfig names a runtime that is not installed; install it or add `"rollForward": "Major"` |
| `System.DllNotFoundException` naming `libc`, `libm.so.6`, `ucrtbase` or `msvcrt` (or, for `msil-ld`'s output, `libc.so.6`, `api-ms-win-crt-...dll`, ...) | the assembly was built for another OS than the one running it. For a native library of your own: it is not next to the program (section 3, "Native libraries and P/Invokes") |
| `System.BadImageFormatException` / `FileLoadException` at start-up | the image's machine (`-x64`, `-arm64`) does not match the process |
| `System.InvalidProgramException` | a backend bug: the JIT rejected a method. `msil-jitall.py` names it; please report it with the `.il` |
| `error: unsupported option '-fsplit-machine-functions' for target '...-msil'`, and the same for `-fbasic-block-sections=all` or `=list=`, `-fbasic-block-address-map` and `-fpartition-static-data-sections` | these options put functions, blocks or data in ELF sections, and CIL methods have no sections; drop the option (Status.md section 3). `-fbasic-block-sections=none` is accepted |
| `warning: basic block address map is not supported for MSIL; ignored` | `-fbasic-block-sections=labels` (a deprecated alias, which the driver passes on as `-fbasic-block-address-map`), or the address map requested through cc1 or llc; the output is the same as without it |
| lit: `fatal: msil: msil_require_corpus=1, but the exit corpus is not usable: <reason>; run 'python msil/utils/corpus/fetch.py --cache <dir>'` | a corpus leg (or `--param msil_require_corpus=1`) found no usable cache: no `msil-corpus.stamp` (never fetched, or a failed run removed it), a stamp written for another `manifest.json` (stale after a manifest change), or a cache path with a blank. Run the command it names (section 7, "The exit corpus"); without the parameter lit only notes it and `msil/test/Programs` is UNSUPPORTED |
| lit: `fatal: msil: msil_require_corpus=1, but the stamp of <dir> lacks <names>; run '...' (or name the entries in --param msil_corpus_skip)` | the cache was fetched with `--only` or `--skip`, so the stamp lists only some entries: fetch the rest, or leave them out on purpose with `--param msil_corpus_skip=<names>` (the legs take it from `MSIL_CORPUS_SKIP`), which makes their tests UNSUPPORTED with a note |
| `fetch.py: <name>-<ref>: FAILED: <reason>`, `fetch.py: <n> of <m> verified` with `n` < `m` | a download, an archive hash, an unsafe member name or link, or the tree hash failed for that entry: nothing of it was moved into the cache and the stamp is gone. A tree edited in the cache fails `--check`; run `fetch.py` without `--check` to fetch it again. A cache inside the repository is refused (exit status 2) |
| lit: `fatal: msil: --param msil_also_run=<entry>: no Microsoft.NETCore.App <n>.x for the <runner> runner (installed: <list>); msil_require_exec=1 makes it a configuration error` | an extra runtime of `msil_also_run` is not installed for the runner (in WSL: the private runtimes of `msil-wsl-dotnet.sh`, section 7); install it, or drop the entry |
| `leg '<name>' was removed in M23 (plan 5.12.11): <leg> runs its tests and runtimes now` (`lit-legs.ps1 -Leg`, `lit-legs.sh --leg`) | the legs `obj`, `obj-wsl`, `net10`, `net8-tc0`, `wsl-net10-tc0` (and on Linux `net10-tc0`) were folded into `net8` and `wsl-net10` and their `msil_also_run` entries (section 7); run the leg it names |

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
