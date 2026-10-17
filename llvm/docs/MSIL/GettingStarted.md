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
| Running the tests | everything above, plus a .NET SDK (it builds the `jitall` checker), the dotnet tool `dotnet-ilverify`, ildasm and PEVerify (Windows SDK, `NETFX 4.8 Tools`), and optionally WSL Ubuntu with .NET 10 and python3 for the Linux leg | everything above, plus the .NET SDK, `dotnet-ilverify` and the NuGet `runtime.linux-x64.Microsoft.NETCore.ILDAsm` |

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
pwsh -NoProfile -File msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config MCTests MSILTests
```

The second line builds everything the tests need: the tools, the lit helpers and the unit tests
(`LLVM_BUILD_TESTS` is off, so unit tests are built only when named). A first build compiles
about 3,500 steps; afterwards a change in `llvm/lib/Target/MSIL` rebuilds the target and relinks
in minutes. `llc --version` lists `msil - MSIL (.NET CIL as ILAsm text)` when the target is in.

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
- Every optimization level works (`-O0` to `-O3`, `-Os`, `-Oz`); the tests run `-O0` and `-O2`.
- `-ffreestanding` keeps the optimizer from introducing libc calls that are not Phase-1 imports.
  It also turns off `main`'s implicit `return 0` on Linux triples, so return explicitly.
- The assembly is named after the source file's stem (`fib`). `-mllvm -msil-assembly-name=<name>`
  sets another name.
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
  programs. A trap (`__builtin_trap`, or reaching `__builtin_unreachable`) ends in an unhandled
  `System.NullReferenceException`.

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
| llc and MIR tests | `llvm/test/CodeGen/MSIL` | the emitted IL, FileCheck'ed on both x86_64 triples, and assembled with ilasm when it is available |
| clang tests | `clang/test/CodeGen/MSIL`, `clang/test/Preprocessor/init-msil.c`, `clang/test/Driver/msil-toolchain.c` | the clang wrapper, predefines, driver errors, Design.md 12's literal command |
| Execution tests | `msil/test/M1/<NN-area>/` | C and IR programs compiled at `-O0` and `-O2`, assembled, verified, force-JITted, run and compared with checked-in transcripts and with a native build; `11-programs/` is the Design.md 12 program corpus |
| Harness self-tests | `msil/test/harness/` | the runner, verifier and JIT checker themselves |

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
skipped steps or UNSUPPORTED tests:

| Leg | Paths | Extra `--param`s |
|---|---|---|
| `net8` | `llvm/test/CodeGen/MSIL`, the clang MSIL tests, `msil/test` | `msil_dotnet_runtime=8 msil_native_oracle=required msil_run_netfx=1` (.NET 8, the native oracle and .NET Framework 4.8; the native link needs `LIB`: use a VS x64 developer prompt, or `msil-lit.ps1`, which enters that environment) |
| `net10` | `msil/test/M1` | `msil_dotnet_runtime=10` |
| `net8-tc0` | `msil/test/M1` | `msil_dotnet_runtime=8 msil_dotnet_env=DOTNET_TieredCompilation=0` (optimizing JIT) |
| `wsl-net10` | `msil/test/M1`, `msil/test/harness` | `msil_runner=wsl msil_triple=x86_64-unknown-linux-gnu-msil msil_dotnet_runtime=10` |
| `wsl-net10-tc0` | `msil/test/M1` | the `wsl-net10` parameters plus `msil_dotnet_env=DOTNET_TieredCompilation=0` |
| `wsl-ilasm` | `msil/test/M1/11-programs` | the `wsl-net10` parameters plus `msil_wsl_ilasm=<the NuGet linux-x64 ilasm>`: assembled by the Linux ilasm inside WSL |
| `modern-ilasm` | `msil/test/M1/11-programs` | `msil_ilasm=<the NuGet win-x64 ilasm.exe>` |
| `roundtrip` | `msil/test/M1/11-programs` | `msil_roundtrip=1`: every assembly goes through ildasm and ilasm (Design.md 5.5) and the reassembled one runs |
| `expected` | `msil/test/M1` | not lit: `update-expected.py --check` |
| `all-levels` (nightly) | `msil/test` | `msil_opt_levels=O0,O1,O2,O3,Os,Oz` |

Expected transcripts (`<test>.expected`) are generated from a native build, never written by
hand, and CI checks that they are current:

```sh
python msil/utils/update-expected.py msil/test/M1/11-programs/fib.c   # write
python msil/utils/update-expected.py --check msil/test/M1             # check
```

How to add a test, what each status line means and how to reproduce a failure step by step is in
[msil/test/README.md](../../../msil/test/README.md).

### The milestone-1 gate and CI

```powershell
pwsh -NoProfile -File msil/utils/run-m1-gate.ps1                                # everything
pwsh -NoProfile -File msil/utils/run-m1-gate.ps1 -SkipConfigure -SkipBuild -Leg net10,roundtrip
pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -List                          # the leg names
```

`run-m1-gate.ps1` runs the scripts in `msil/utils/ci/` in the order the CI workflow
`.github/workflows/msil-ci.yml` runs them: `configure.ps1`, `build.ps1` (the tools, the unit
tests and jitall), `unit-tests.ps1` (`TargetParserTests`, `MCTests` and `MSILTests`, no skipped
test allowed) and `lit-legs.ps1 -Wsl` (the legs above). The gate's `-NoWsl` leaves out `-Wsl`
and with it the three WSL legs, `-Full` adds `all-levels`, `-Leg` runs only the named legs, and
`-SkipConfigure`, `-SkipBuild` and `-SkipUnitTests` reuse what is there. Unless `-NoWsl`, a
`ci-sh` row smoke-checks the Linux job's scripts inside WSL (`bash -n` on
`msil/utils/ci/*.sh`, `lit-legs.sh --list` against its header, and a non-zero exit with FAIL
rows against an empty build directory); the Linux job's configure, build, unit-test and lit
steps themselves run only in CI. A child script that exits non-zero without a FAIL row, and
every selected leg without a row, add FAIL rows. A failed configure or build stops the unit
tests and the legs (`ci-sh` needs no build and still runs); every leg runs even after another
one failed. A leg passes when lit exits 0 and at least one test passed; its lit log and xunit
file are `build/lit-results/<leg>.log` and `<leg>.xml`. The run ends with a summary table with
one row per step or leg and exits 0 only when every row passed. A full run takes an hour or
more.

The Windows CI job runs the same scripts, except that `configure.ps1 -Ci` configures with the
cache file instead of `msil-configure.ps1` and `lit-legs.ps1` runs without `-Wsl` (a GitHub
runner has no WSL); the Linux job runs their `.sh` twins, whose legs cover
`x86_64-unknown-linux-gnu-msil` natively on .NET 8 and .NET 10
(`bash msil/utils/ci/lit-legs.sh --list`). The workflow is committed with a manual trigger only
(`workflow_dispatch`); enabling push, pull-request or nightly runs is the repository owner's
decision.

## 8. When something goes wrong

| Message | Cause and fix |
|---|---|
| `emitting object files is not yet supported for MSIL target` | `-S` is missing; Phase 1 has no object files or linker |
| `undefined function 'f' is not defined in this module and is not a Phase-1 import` | every function must be defined in the same file, except `puts`, `putchar` and the C math library (Status.md) |
| `runtime helper '__divti3' is not available for MSIL yet (phase 3)` | the code needs a compiler-rt helper that the backend would call: `__int128` division and conversions to or from floating point, `_Float16` arithmetic (Status.md section 3). Helpers that clang itself calls (`__muldc3` for `_Complex` multiplication, `__atomic_*` for atomics) are reported as undefined functions instead |
| `unknown target triple '...-msil'` | Darwin and i686 MSIL triples come in later phases; Windows MSIL triples name `msvc` or `gnu` |
| `... is not supported for MSIL yet` | a Phase-1 limitation; Status.md lists them |
| `The library 'hostpolicy.dll' required to execute the application was not found` (Windows) or `The library 'libhostpolicy.so' required to execute the application was not found` (Linux), exit `0x80008083` or 131 | no `<name>.runtimeconfig.json` next to the `.dll` (section 5) |
| `You must install or update .NET to run this application` | the runtimeconfig names a runtime that is not installed; install it or add `"rollForward": "Major"` |
| `System.DllNotFoundException` naming `libc`, `libm.so.6`, `ucrtbase` or `msvcrt` | the assembly was built for another OS than the one running it |
| `System.BadImageFormatException` / `FileLoadException` at start-up | the image's machine (`-x64`, `-arm64`) does not match the process |
| `System.InvalidProgramException` | a backend bug: the JIT rejected a method. `msil-jitall.py` names it; please report it with the `.il` |
