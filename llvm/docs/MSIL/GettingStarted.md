# Getting started with the MSIL backend

This fork of llvm-project adds an experimental LLVM target, `MSIL`, that compiles C to IL-only
.NET assemblies (the design is in [Design.md](Design.md)). Phase 1 (the "first milestone" of
Design.md section 12) compiles one C file to ILAsm text with `clang -S`; `ilasm` turns the text
into an assembly, and `dotnet` (or .NET Framework 4.8) runs it. There are no object files and no
linker yet. What works and what does not is in [Status.md](Status.md); how the work was planned
and built is in [ImplementationPlan.md](ImplementationPlan.md).

Like LLVM itself, the project ships compiler tools only (Design.md, scope line). The compiler
writes the `.il` file and nothing else: no `runtimeconfig.json`, no `.exe.config`, no project or
package files. Section 5 shows how to write the runtimeconfig yourself.

## 1. What you need

| For | Windows | Linux |
|---|---|---|
| Building | Visual Studio 2026 with the bundled clang-cl, lld-link and llvm-lib (any C++17 compiler works with the plain CMake route), CMake, Ninja, Python 3 | a C++17 compiler (clang or gcc), CMake, Ninja, Python 3 |
| Assembling | the .NET Framework ilasm, `%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe` (always present), or the NuGet `runtime.win-x64.Microsoft.NETCore.ILAsm` | the NuGet package `runtime.linux-x64.Microsoft.NETCore.ILAsm` (10.0.12 is what the tests use) |
| Running | .NET 8 or later; .NET Framework 4.8 for Windows triples | .NET 8 or later |
| Running the tests | everything above, plus a .NET SDK (it builds the `jitall` checker), the dotnet tool `dotnet-ilverify`, ildasm and PEVerify (Windows SDK, `NETFX 4.8 Tools`), the Visual Studio "C++ Clang tools" (their compiler-rt builtins and `ld.lld` serve the native oracles), and optionally WSL Ubuntu with python3 and curl or wget for the Linux legs (the legs' own .NET runtimes and Linux libraries come from the host setup of section 7, without root) | everything above, plus the .NET SDK, `dotnet-ilverify` and the NuGet `runtime.linux-x64.Microsoft.NETCore.ILDAsm` |

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

On any host, the cache file sets all of that (Release, assertions, clang, X86, experimental MSIL,
the `msil/` project):

```sh
cmake -G Ninja -S llvm -B build -C msil/cmake/caches/MSIL.cmake
ninja -C build clang llc
```

Without the cache file, the essential options are:

```sh
cmake -G Ninja -S llvm -B build -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=X86 \
  -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MSIL \
  -DLLVM_EXTERNAL_PROJECTS=msil -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=$PWD/msil
```

On the Windows development host the PowerShell helpers in `msil/utils/build/` configure the same
tree with the Visual Studio clang-cl toolchain and serialize builds through a lock (see
[msil/utils/build/README.md](../../../msil/utils/build/README.md)):

```powershell
pwsh -NoProfile -File msil/utils/build/msil-configure.ps1 -Experimental MSIL
pwsh -NoProfile -File msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config llvm-profdata TargetParserTests MCTests MSILTests
```

The second line builds everything the tests need (the build targets of milestones M11 and M12,
which `msil/utils/ci/build.ps1` and `build.sh` also build): the tools, the lit helpers and the unit
tests (`LLVM_BUILD_TESTS` is off, so unit tests are built only when named). `llvm-profdata` is
there for two upstream clang driver tests of the upstream-hooks check (section 7), and the whole
of `TargetParserTests` and `MCTests` runs in that check too. A first build compiles about 3,500
steps; afterwards a change in `llvm/lib/Target/MSIL` rebuilds the target and relinks in minutes.
`llc --version` lists `msil - MSIL (.NET CIL as ILAsm text)` when the target is in.

## 3. Compile a C program

Phase 1 programs are freestanding C in one file. The only library functions they may call are
the Phase-1 imports: `puts`, `putchar` and the C math library (Status.md lists them per triple).
Declare them yourself or use clang's freestanding headers (`stdint.h`, `stddef.h`, `stdbool.h`,
`limits.h`, `float.h`); there is no sysroot or libc header set yet.

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
  section 3). The P/Invoke library names come from the triple (`libc` and `libm.so.6` on glibc,
  `ucrtbase` on windows-msvc), so build for the OS you run on.
- Every optimization level works (`-O0` to `-O3`, `-Os`, `-Oz`); the execution tests run at
  `-O0` and `-O2` in most legs, and the `more-levels` leg runs most of them at the other levels
  (C programs at all four, IR tests at `-O1` and `-O3`; section 7).
- `-ffreestanding` keeps the optimizer from introducing libc calls that are not Phase-1 imports.
  It also turns off `main`'s implicit `return 0` on Linux triples, so return explicitly.
- The assembly is named after the source file's stem (`fib`). `-mllvm -msil-assembly-name=<name>`
  sets another name. `-mllvm` passes backend options as it does for native targets (llc's
  `-print-after=`, `-debug-pass=Structure`, `-verify-machineinstrs`, ...; section 8).
- `-c`, or no `-S` at all, stops with `emitting object files is not yet supported for MSIL target
  '...'; use -S to produce ILAsm text, or -emit-llvm or -flto to produce bitcode`. Object files and
  `msil-ld` are phase 2.
- `llc` works on IR as usual: `clang --target=<triple> -O2 -ffreestanding -S -emit-llvm fib.c -o
  fib.ll`, then `llc fib.ll -o fib.il` (llc takes the triple from the module, or `-mtriple=`).

The output is one ILAsm module: an `.assembly extern mscorlib` reference, the assembly, one
`.method` per C function, P/Invoke declarations for the imports, the data segment and module
initializer (`<Module>::.cctor`) for global variables, and a generated `'<msil>Main'` entry point
that calls `main` (with `argc`/`argv` when `main` takes them).

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
`fib.runtimeconfig.json`).

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
- Phase-1 output references only `mscorlib`, so the same `.dll` runs on .NET 8, 9 and 10, and the
  `.exe` on .NET Framework 4.8.
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

## 7. Run the tests

| Suite | Where | What |
|---|---|---|
| Unit tests | `llvm/unittests/Target/MSIL`, plus the MSIL cases of `TargetParserTests` and `MCTests` | type mapping, name quoting, stack effects, target machine, streamer, triples, target selection |
| llc and MIR tests | `llvm/test/CodeGen/MSIL` | the emitted IL, FileCheck'ed on both x86_64 triples (and the aarch64 ones where the ABI differs), and assembled with ilasm when it is available; the pass pipeline and the MIR at llc's stop points |
| clang tests | `clang/test/CodeGen/MSIL`, `clang/test/Preprocessor/init-msil.c`, `clang/test/Driver/msil-toolchain.c` | the clang wrapper, predefines, driver errors (the ELF-only section options among them), `-mllvm` options, `-cc1as`, Design.md 12's literal command |
| Execution tests | `msil/test/M1/<NN-area>/` | C and IR programs compiled at `-O0` and `-O2` (and in the `more-levels` leg at the other levels; see below), assembled, verified, force-JITted, run and compared with checked-in transcripts and with a native build (the *native oracle*, below); `11-programs/` is the Design.md 12 program corpus |
| Harness self-tests | `msil/test/harness/` | the runner, verifier and JIT checker themselves, `update-expected.py`, the E7 Linux oracle, and lit's configuration errors (bad `msil_*` parameters stop lit with exit status 2) |
| Upstream regression tests | the upstream lit tests and unit tests named in `msil/utils/upstream-hooks.txt` | the upstream behaviour of every upstream file the fork changes (the upstream-hooks check, below) |

```sh
ninja -C build check-msil                 # every MSIL lit test (lean dependencies)
ninja -C build check-msil-m1-11-programs  # one execution directory (check-msil-m1-<dir>)
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
target, ilasm and a .NET runtime; without them they are UNSUPPORTED rather than failing.

The execution suite takes lit parameters (all of them are in
[msil/test/README.md](../../../msil/test/README.md)). The milestone-1 gate runs it in these legs
(the names are those of `msil/utils/ci/lit-legs.ps1`). Every lit leg also passes
`--param msil_require_verify=1 --param msil_require_exec=1`, which turn a missing verifier,
ildasm, jitall, PEVerify (Windows), ilasm, runtime or MSIL-enabled clang into an error instead of
skipped steps or UNSUPPORTED tests. *WSL* below stands for
`msil_runner=wsl msil_triple=x86_64-unknown-linux-gnu-msil msil_wsl_dotnet=private
msil_dotnet_runtime=10` (the private .NET 10 in WSL), *E7* for
`msil_native_oracle=required msil_linux_sysroot=<sysroot>` (the E7 Linux oracle; both are
described under "Native oracles and the WSL runtime" below), and the legs
marked `-Wsl` run only when `lit-legs.ps1` gets `-Wsl` (or names them with `-Leg`):

| Leg | Paths | Extra `--param`s |
|---|---|---|
| `net8` | `llvm/test/CodeGen/MSIL`, the clang MSIL tests, `msil/test` | `msil_dotnet_runtime=8 msil_native_oracle=required msil_run_netfx=1` (.NET 8, the native oracle with the compiler-rt builtins, and .NET Framework 4.8; the native link needs `LIB`: use a VS x64 developer prompt, or `msil-lit.ps1`, which enters that environment) |
| `net10` | `msil/test/M1` | `msil_dotnet_runtime=10` |
| `net8-tc0` | `msil/test/M1` | `msil_dotnet_runtime=8 msil_dotnet_env=DOTNET_TieredCompilation=0` (optimizing JIT) |
| `more-levels` | `msil/test/M1` | `msil_dotnet_runtime=8 msil_opt_levels=O1,O3,Os,Oz`: the four levels the other legs leave out (they run `-O0` and `-O2`); `.ll` tests that use `%{msil_ll}` run at `-O1` and `-O3` only, because llc has no `-Os` or `-Oz`. The three trap tests (`01-int/trap-call-before-div.ll`, `01-int/trap-call-before-rem.ll`, `03-control/trap.ll`) and `01-int/calls-arith-debug-info.test` have explicit RUN lines that pin `-O0` and `-O2` in every leg |
| `wsl-net10` (`-Wsl`) | `msil/test/M1`, `msil/test/harness` | *WSL* and *E7* |
| `wsl-net10-tc0` (`-Wsl`) | `msil/test/M1` | *WSL* plus `msil_dotnet_env=DOTNET_TieredCompilation=0` |
| `wsl-ilasm` (`-Wsl`) | `msil/test/M1/11-programs` | *WSL* plus `msil_wsl_ilasm=<the NuGet linux-x64 ilasm>`: assembled by the Linux ilasm inside WSL |
| `wsl-quick` (`-Wsl`) | `msil/test/M1/11-programs`, `msil/test/harness`, and `msil/test/M2` once it exists | *WSL* and *E7*: the short WSL leg that every Phase 2 milestone gate runs from M13 on (plan 5.12.10) |
| `modern-ilasm` | `msil/test/M1/11-programs` | `msil_ilasm=<the NuGet win-x64 ilasm.exe>` |
| `roundtrip` | `msil/test/M1/11-programs` | `msil_roundtrip=1`: every assembly goes through ildasm and ilasm (Design.md 5.5) and the reassembled one runs |
| `expected` | `msil/test/M1` | not lit: `update-expected.py --check` (the Windows native oracle, with the compiler-rt builtins) |
| `expected-linux` (`-Wsl`) | `msil/test/M1` | not lit: `update-expected.py --check --os linux --linux-sysroot <sysroot>` (the E7 oracle, against `<stem>.linux.expected` where one exists, else `<stem>.expected`) |
| `utc` | six tests of `llvm/test/tools/UpdateTestChecks/update_llc_test_checks` | not lit: `msil/utils/run-utc-tests.py --llc <build>/bin/llc msil-basic x86-basic x86-mir-only x86-asm-mir-mixed x86-constant-pool asm-show-inst` runs their RUN lines (lit reports them UNSUPPORTED on Windows: `llvm/test/tools/UpdateTestChecks/lit.local.cfg` sets the `llc-binary` feature they require only when a file named exactly `llc` exists); the log is `build/lit-results/utc.log` |
| `all-levels` (nightly, `-Full`) | `msil/test` | `msil_opt_levels=O0,O1,O2,O3,Os,Oz` |

`<sysroot>` is `$MSIL_LINUX_SYSROOT`, else `C:\llvm-msil-scratch\linux-sysroot`. Each lit leg
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
python msil/utils/update-expected.py --check msil/test/M1             # check (Windows oracle)
python msil/utils/update-expected.py --check --os linux msil/test/M1  # check (E7 Linux oracle)
```

The script compiles C with the flags of `%{msil_cflags}` (`-std=gnu17 -ffreestanding
-Werror=return-type`) and runs the native builds in parallel (`-j`, default one per CPU). On
Windows its MSVC builds link the compiler-rt builtins (`--rtlib`, default `auto`, as
`msil_native_rtlib` below). `--os linux` uses the E7 oracle (`--linux-sysroot`, `--ld-lld`,
`--wsl-distro`); writing then creates `<stem>.linux.expected` only for a transcript that differs
from `<stem>.expected`, and removes one that no longer differs. No test of `msil/test/M1` needs
one today. `--check` compares with `<stem>.<os>.expected` where it exists, else with
`<stem>.expected`, the file the test itself picks for that OS.

How to add a test, what each status line means and how to reproduce a failure step by step is in
[msil/test/README.md](../../../msil/test/README.md).

### Native oracles and the WSL runtime

With `--param msil_native_oracle=1` or `required` (`required` in the `net8`, `wsl-net10` and
`wsl-quick` legs), every execution test also builds its source for the native triple
(`x86_64-pc-windows-msvc` for `x86_64-pc-windows-msvc-msil`, and so on), runs it, and compares
its transcript with the same `.expected` file as the MSIL build's.

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
directory inside the repository. From M15 `msil-ld` uses the same directory as `--sysroot`.
`run-m1-gate.ps1` runs both as its `host-setup` step.

### The milestone-1 gate and CI

```powershell
pwsh -NoProfile -File msil/utils/run-m1-gate.ps1                                # everything
pwsh -NoProfile -File msil/utils/run-m1-gate.ps1 -SkipConfigure -SkipBuild -Leg net10,roundtrip
pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -List                          # the leg names
```

`run-m1-gate.ps1` runs the scripts in `msil/utils/ci/` in the order the CI workflow
`.github/workflows/msil-ci.yml` runs them: `configure.ps1`, `build.ps1` (the build targets of
section 2 and jitall), `unit-tests.ps1` (`TargetParserTests`, `MCTests` and `MSILTests`, no
skipped test allowed), then, on the development host only, the `host-setup` step (the two
commands of the host setup above, as the rows `host-setup/wsl-dotnet` and
`host-setup/linux-sysroot`), then `lit-legs.ps1 -Wsl` (the legs above) and `upstream-hooks.ps1`
(the upstream-hooks check below, every leg; the gate checks that each of them has a row). A
failed `host-setup` row does not stop the legs; the WSL legs that need what it makes then fail
with a configuration error that names it. The gate's `-NoWsl` leaves out `host-setup`, `-Wsl`
and with it the five `-Wsl` legs, `-Full` adds `all-levels`, `-Leg` runs only the named legs,
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
the `net8` leg, `wsl-quick`, the upstream-hooks `manifest` and `pins` legs, or all of the check
when the milestone changes an upstream file, and the milestone's own legs; M12's gate runs
`net8`, `net10`, `wsl-net10` instead of `wsl-quick`, `roundtrip`, `expected-linux` and the whole
upstream-hooks check), and `run-m1-gate.ps1` runs in full after the commits of M12, M15, M18
and M20.

The Windows CI job runs the same scripts, except that `configure.ps1 -Ci` configures with the
cache file instead of `msil-configure.ps1`, there is no `host-setup`, `lit-legs.ps1` runs
without `-Wsl` (a GitHub runner has no WSL), and `upstream-hooks.ps1` runs as `-NoCompile
-NoManifest -MinPass 250` whenever the build passed (also after failed unit tests or legs). The
Linux job runs their `.sh` twins, whose legs cover `x86_64-unknown-linux-gnu-msil` natively on
.NET 8 and .NET 10, including `more-levels` and `utc` (`bash msil/utils/ci/lit-legs.sh --list`);
its `expected` leg is the native Linux oracle (`update-expected.py --check` on Linux, against
`<stem>.linux.expected` where one exists), and its `--full` legs add `all-levels` and
`upstream`, which builds and runs all of `ninja check-llvm check-clang`. Both jobs install .NET
with `actions/setup-dotnet` pinned at v5.4.0
(`26b0ec14cb23fa6904739307f278c14f94c95bf1`). The workflow is committed with a manual trigger
only (`workflow_dispatch`, whose `full` input selects the full legs, as a nightly schedule
would); enabling push, pull-request or nightly runs is the repository owner's decision.

### The upstream-hooks check

The fork changes 36 upstream files today (its *hooks*): new diagnostics, MSIL branches in clang's
driver, code generation and Sema, the MSIL object format and `IsMSIL` environment in the triple
and MC layers, `case` labels for it in exhaustive switches, and so on.
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
| `manifest` | `check-upstream-hooks.py --check`: every upstream file that differs from the upstream base (below) has a `hook` block, unless it is a file the fork added, on a `fork-only` line; a `fork-only` path must not exist in the upstream base (an upstream file the fork changes needs a `hook` block), and no block or `fork-only` line names an unchanged file (stale); every block says how it is tested (`lit`, `gtest`, `compile`, `utc` or `none <reason>`), every lit pattern matches a file, and every fork test exists; every name on a `utc` line is one of the `utc` leg's tests, which the check reads from `$utcTests` of `msil/utils/ci/lit-legs.ps1` and `utc_tests` of `msil/utils/ci/lit-legs.sh` and requires to be equal; every `compile` line names a compile-leg file or a header that one of them `#include`s directly, matched by include-path suffix such as `llvm/MC/MCContext.h`, and each compile-leg file has a block with a `compile` line (step 2 below); every exhaustive object-format switch in the whole monorepo, built or not (`case Triple::XCOFF:` or `case MCContext::IsXCOFF:`, with or without `llvm::`, or an unqualified `case XCOFF:` / `case IsXCOFF:`), has an MSIL case label in the same switch (comments and string literals do not count; a `Triple::MSIL` elsewhere in the file does not cover it); the two exemptions are checked per switch, not per file: in a file with a `switch-default` line each XCOFF case label must sit in a switch whose own body (not a nested switch) has a `default:` label or an MSIL case label, and a `switch-other` line exempts only its file's unqualified `case XCOFF:` / `case IsXCOFF:` labels, while its `Triple::XCOFF` and `MCContext::IsXCOFF` labels are checked as in any other file; a `switch-default` line whose file has no XCOFF switch, or a `switch-other` line whose file has no unqualified XCOFF label, is stale; a file whose braces do not match, or whose label sits outside a switch body (a macro), needs at least as many covering labels in code (MSIL case labels, plus `default:` labels in a `switch-default` file) as XCOFF labels that need cover instead (the unqualified labels of a `switch-other` file need none) | 11-20 s |
| `pins` | `check-action-pins.py`: every `uses: <action>@<sha> # <version>` of the `msil-*.yml` workflows under `.github` pins the action at the SHA and version that upstream's workflows use; quoted values are read as well (one pair of single or double quotes is stripped, in the MSIL and the upstream workflows alike); an action upstream never uses (`actions/setup-dotnet`) is printed without failing. Any other `uses:` line of an `msil-*.yml`, one that is neither such a pin nor a local `./...` action or reusable workflow nor a `docker://` image (no `@<ref>`, an unterminated quote, the value on the next line), fails, and so does an MSIL workflow with `uses:` lines but no pin. It also reads `.github/workflows/msil-ci.yml` as text: its windows job must have a step whose `run:` is exactly `pwsh -NoProfile -File msil/utils/ci/upstream-hooks.ps1 -NoCompile -NoManifest -MinPass 250` and whose `if:` is `${{ !cancelled() && steps.<id>.outcome == 'success' }}`, with `<id>` the `id:` of that job's Build step, and which comes after that step; the Build step is the one step of the windows job whose `run:` names `msil/utils/ci/build.ps1`, and the job must have exactly one. So each of these fails: a deleted step, a misspelled id, a Build step without an `id:` (or with a blank one), the step before the Build step or in another job, a changed `run:` line, a guard without `!cancelled()`, no `if:`, no Build step, and two or more Build steps | 1 s |
| `lit` | the upstream lit tests of the `lit` lines (301 today), in one `msil-lit.ps1` call (the list goes to lit through the response file `build/lit-results/upstream-hooks.rsp`, so it never meets the Windows command-line limit); `check-upstream-hooks.py --lit-verdict` passes lit's JSON report when every FAIL is allow-listed, no allow-listed test passed or went missing, nothing is UNRESOLVED, TIMEOUT or XPASS, every input ran, and at least `min-pass` (280) tests PASS (XFAIL does not count) | about 1 min |
| `gtest` | all of `TargetParserTests` (at least 700 tests) and `MCTests` (at least 40) through `msil-gtest.ps1 -NoSkip`, except the host-gated `HostTest` cases, `TargetRegistry.MSILLookupWithoutTarget` (it skips whenever MSIL is built) and the `Disassembler` test of a target that `llvm-config --targets-built` does not list | 10-25 s |
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
  in progress, failing and stale lit verdicts) in a temporary git repository. The switch
  defects are checked per switch: they include a file with two switches and one MSIL case, a
  file whose only MSIL mention is a comment, a `switch-default` file with a second XCOFF switch
  without `default:`, a `switch-other` file with a qualified XCOFF switch, and stale
  `switch-default` and `switch-other` lines.
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
`.github/msil/`) are never hooks.

**Extending the manifest.** An upstream merge, or a milestone that changes another upstream
file, updates the manifest in the same commit (the format is documented in its header):

1. Run `upstream-hooks.ps1` on the uncommitted merge (or change). The manifest leg names every
   unlisted hook and every stale entry, and the lit and gtest legs show which upstream tests now
   fail.
2. For a new hooked file add a `hook <path>` block with `lit <path-or-glob>` lines for the
   upstream tests that reach the changed code, `gtest <exe> [<filter>]`, `compile`,
   `utc <test>...` or `none <reason>`, and a `fork <test>` line for each fork test that pins
   the MSIL side. A `utc` line names only tests that the `utc` leg runs: each name must be in
   `$utcTests` of `msil/utils/ci/lit-legs.ps1` and `utc_tests` of `msil/utils/ci/lit-legs.sh`
   (the two lists stay equal), so a test that the leg should run is added to both lists first.
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
need tools this build lacks (`llvm-readobj`, `llvm-mc`, `llvm-objdump`, ...) are left out of the
manifest instead.

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
| `emitting object files is not yet supported for MSIL target` | `-S` is missing; Phase 1 has no object files or linker |
| `undefined function 'f' is not defined in this module and is not a Phase-1 import` | every function must be defined in the same file, except `puts`, `putchar` and the C math library (Status.md) |
| `runtime helper '__fixdfti' is not available for MSIL yet (phase 3)` | the code needs a compiler-rt helper that the backend would call: conversions between floating point and `__int128` under strict floating point, `_Float16`/`__bf16` arithmetic and conversions (`__extendhfsf2`), `__bf16` parameters, arguments and return values (`__truncsfbf2`, `__extendbfsf2`) (Status.md section 3). `__int128` division and the ordinary conversions are expanded inline since M12, so a build from before M12 also names `__divti3`, `__udivti3`, `__floattidf`, ... here. Helpers that clang itself calls (`__muldc3` for `_Complex` multiplication, `__atomic_*` for atomics) are reported as undefined functions instead |
| `unknown target triple '...-msil'` | Darwin and i686 MSIL triples come in later phases; Windows MSIL triples name `msvc` or `gnu` |
| `... is not supported for MSIL yet` | a Phase-1 limitation; Status.md lists them |
| `global variable @p_init in section ".CRT$XCU" is not supported for MSIL yet (phase 2 CIL objects)` | a variable with `__attribute__((section(...)))` or `__declspec(allocate(...))`; the section would be lost, and a constructor registered in `.init_array` or `.CRT$XCU` would never run. Use `__attribute__((constructor))`, or drop the section (Status.md section 3) |
| `alias @a is not supported for MSIL`, `ifunc @f is not supported for MSIL` | only aliases of a defined function (at offset 0) and of a variable are lowered (Status.md section 2); ifuncs are never supported |
| `inline assembly is not supported for the MSIL target '...'` (clang) or `inline assembly is not supported for MSIL` (llc) | only empty `asm` statements work: with no operands, or with inputs, read-write (`"+r"`), tied (`"0"`) and memory outputs. An output that is not tied to an input (`"=r"`) gets no value from an empty `asm` and is rejected, and so is a condition-flag output (`"=@ccz"`, `"+@ccz"`), which native code sets from the flags after the statement (Status.md section 2) |
| `the va_arg instruction is not supported for MSIL yet (phase 3 variadics)` | LLVM IR with a `va_arg` instruction (variadic functions are phase 3); before M12 one of a struct or array type crashed llc instead |
| `The library 'hostpolicy.dll' required to execute the application was not found` (Windows) or `The library 'libhostpolicy.so' required to execute the application was not found` (Linux), exit `0x80008083` or 131 | no `<name>.runtimeconfig.json` next to the `.dll` (section 5) |
| `You must install or update .NET to run this application` | the runtimeconfig names a runtime that is not installed; install it or add `"rollForward": "Major"` |
| `System.DllNotFoundException` naming `libc`, `libm.so.6`, `ucrtbase` or `msvcrt` | the assembly was built for another OS than the one running it |
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
