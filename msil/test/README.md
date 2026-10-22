# MSIL execution tests

This directory holds the end-to-end tests of the MSIL backend
(`llvm/docs/MSIL/Design.md`): C or LLVM IR is compiled for an MSIL triple,
assembled with ilasm, verified, force-JIT-compiled, run on .NET, and its
transcript is compared with a checked-in expected file. Since M14, programs
of several files, archives and stock-clang bitcode are also linked by
`msil-ld` (`M2/`, `%{msil_prog}`). It is an LLVM external
project (`-DLLVM_EXTERNAL_PROJECTS=msil -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=<repo>/msil`,
set by `msil/utils/build/msil-configure.ps1` and `msil/cmake/caches/MSIL.cmake`),
so it needs no upstream CMake change. Plan section 5.5 has the rules every test
follows.

## What runs

For every level of `--param msil_opt_levels` (default `O0,O2`), `%{msil_c}` runs:

```text
prog.c --clang --target=<triple> -S-->  %t.<L>.il  --ilasm -dll-->  %t.<L>.dll
  --> msil-verify.py   ILVerify with ilverify-allowed.txt, PEVerify on Windows,
                       then both again on a neutralized copy (ildasm + ilasm)
  --> msil-jitall.py   JIT-compile every method (if jitall exists); the entry
                       point does not run, but <Module>::.cctor (data segment
                       and C constructors) does, where the program would run
  --> msil-run.py      run from a private directory, under a timeout and a memory
                       cap, and compare the transcript with prog[.<os>].expected
  [--> round trip]     ildasm, ilasm and ildasm again (the .NET tools); both
                       listings must match, and the reassembled image runs
                       against the same .expected (msil_roundtrip=1)
  [--> native oracle]  the same source built natively must give the same transcript
  [--> .NET Framework] ilasm -exe and run on .NET Framework 4.8
```

The native oracle (`--param msil_native_oracle=1` or `required`) builds the
test for the native triple with the same clang and `%{msil_cflags}`: on
Windows linked by the MSVC linker with the compiler-rt builtins of the Visual
Studio LLVM (`msil_native_rtlib`, for the `__int128` helpers `__udivti3`,
`__fixdfti`, ... that a native build calls), on Linux by the system linker
with `-lm`, and with `msil_runner=wsl` by the *E7 Linux oracle*: compiled on
Windows for `x86_64-unknown-linux-gnu` (`-fno-pic -c`), linked by `ld.lld`
with `msil/utils/linux-oracle/start.c` against the distribution's
`libm.so.6`, `libc.so.6` and `libgcc_s.so.1` (copied into
`msil_linux_sysroot` by `msil/utils/make-linux-sysroot.py`; `--as-needed`), and
run inside WSL by `msil-run.py --native --runner wsl`. WSL needs no compiler,
headers or root for it. `llvm/docs/MSIL/GettingStarted.md` section 7 has the
details and the host setup.

jitall runs on the host with the local runner and inside WSL (on the WSL .NET
runtime the programs use: the private one of `msil/utils/msil-wsl-dotnet.sh`,
`$HOME/.dotnet-msil`, in every gate leg) with `msil_runner=wsl`: the module
initializer of a linux-triple program calls libc and libm, which exist only
there. ILVerify, PEVerify, ildasm and ilasm run on the host, except that
`--param msil_wsl_ilasm` makes `%ilasm` the Linux ilasm inside WSL (the
neutralized pass of msil-verify.py still reassembles with the host ilasm).

`%{msil_ll}` is the same for hand-written IR run through `llc` (`O0`..`O3`
only; `.ll` files carry no target triple or datalayout). `llc` does no IPO, so
values passed to `noinline` functions stay opaque even at `-O2`.

`%{msil_prog}` (M14) builds a program of several files with
`msil/utils/msil-prog.py` (`%msil_prog_build`) in a fresh `%t.<L>.d` per level,
then checks and runs `prog.exe` as `%{msil_c}` does:

```text
%{msil_srcs}        --clang -c (bitcode)-->                    obj/<stem>.o
%{msil_stock_srcs}  --clang --target=<native> -flto -c-->      stock/<stem>.o
%{msil_archives}    --clang -c, then llvm-ar rcs-->            ar/<NAME>/lib<NAME>.a
  --clang -<L> -nodefaultlibs <objects> -Lar/<NAME> -l<NAME> %{msil_libs}
     -Wl,--lto-emit-asm (clang runs msil-ld)-->  prog.il  --ilasm -exe-->  prog.exe
  --> msil-verify.py, msil-jitall.py, msil-run.py   as for %{msil_c}
  [--> round trip]      of prog.exe (msil_roundtrip=1)
  [--> native oracle]   msil-prog.py --mode native: the same sources and
                        archives built for the native triple (clang
                        -fuse-ld=lld; the E7 recipe in WSL)
  [--> .NET Framework]  the same prog.exe, run by the Framework
```

`prog.exe` is an EXE image, so the one file runs under `dotnet exec` and on the
.NET Framework. Stock sources are compiled by the tree's clang for the native
triple (with `-fno-ms-volatile` on windows-msvc), as stock-clang bitcode (plan
Q12); natively every source is an ordinary one. `%{msil_ldflags}` is
`-nodefaultlibs` in M14: the driver's default libraries only have to exist in
M14, and a Windows host has no Linux C library (M15 links them through
`--sysroot`).

`--param msil_pipeline` (feature `msil-pipeline=<p>`) picks how `%{msil_c}`
builds: `asm` (the default) is the Phase 1 flow above, and `lto` builds the one
source as `%{msil_prog}` does (the `lto` leg). `%{msil_ll}` stays on llc text
under both. `%{msil_prog}` uses `lto` under both in M14; the feature
`msil-prog-pipeline=<p>` names the pipeline that `%{msil_prog}` actually uses
(from M20 `obj` under `asm`), so a check that depends on how `%{msil_prog}`
builds (an LTO-only diagnostic, `--lto-emit-asm` text, a `--check-prefix=LTO`
block) is guarded by `%if msil-prog-pipeline=lto %{ ... %}` or
`REQUIRES: msil-prog-pipeline=lto`, never by `msil-pipeline=<p>`, which decides
only `%{msil_c}` and `%{msil_ll}`. `msil_pipeline=obj` and
`msil_link_output=pe` are configuration errors until M20.

Most gate and CI legs run the default levels `O0,O2`. The `more-levels` leg
runs `M1/` again with `--param msil_opt_levels=O1,O3,Os,Oz` on .NET 8 in every
gate run and in both CI jobs (the gate and the Windows job on
`x86_64-pc-windows-msvc-msil`, the Linux job on
`x86_64-unknown-linux-gnu-msil`), so every `%{msil_c}` test runs at all six
levels and every `%{msil_ll}` test at `O0`..`O3`. The `utc` leg is not part
of this suite: `msil/utils/run-utc-tests.py` runs six tests of
`llvm/test/tools/UpdateTestChecks/update_llc_test_checks` (`msil-basic`,
`x86-basic`, `x86-mir-only`, `x86-asm-mir-mixed`, `x86-constant-pool`,
`asm-show-inst`) without lit, which reports them UNSUPPORTED on Windows
(`llvm/test/tools/UpdateTestChecks/lit.local.cfg` sets the `llc-binary`
feature they require only when a file named exactly `llc` exists).

Every child process (ilasm, ildasm, ILVerify, PEVerify, jitall, dotnet, native
programs) runs under a wall-clock timeout (60 s) and a cap on committed memory
(4096 MiB): a Windows Job Object, or `RLIMIT_DATA` in a new session on POSIX.
The .NET 10 JIT was seen growing past 40 GB on bad IL
(`llvm/docs/MSIL/recon/cil-runtime.md`, e15).

What makes a test UNSUPPORTED: `M1/` needs the feature `msil-exec` (clang with
the MSIL target, ilasm and a .NET runtime); `harness/` needs `msil-ilasm` and
`msil-dotnet`. Without them `check-all` still passes. With
`--param msil_require_verify=1` (every gate) a missing ILVerify, ildasm,
jitall, PEVerify (Windows), ilasm or .NET runtime for the selected runner is a
configuration error instead, also when an empty `--param msil_<tool>=` or
`$MSIL_<TOOL>` switched it off, so a gate cannot pass with every harness test
UNSUPPORTED. `--param msil_require_exec=1` (every gate leg) does the same for
`msil-exec`: `M1/` without clang, the MSIL target, ilasm or a runtime is a
configuration error instead of UNSUPPORTED. `M2/` also needs the feature
`msil-ld` (the tree's `msil-ld`), and its archive tests `msil-ar` (the tree's
`llvm-ar`); `--param msil_require_link=1` (every gate leg) makes a missing
`msil-ld` or `llvm-ar` a configuration error, and so does
`msil_pipeline=lto`.

## Directories

`M1/` holds the milestone-1 tests (Design.md section 12), one directory per
capability in the order the milestones added them, so the lowest-numbered
failing directory names the earliest broken capability:

| Directory | Since | Covers |
|---|---|---|
| `00-smoke/` | M3 | return values, `argc`, leaf methods |
| `01-int/` | M4 | integer arithmetic of every width, `__int128` (its division inline since M12), compares, select, direct calls |
| `02-fp/` | M5 | `float`/`double` arithmetic, NaN compares, conversions and their rounding (to and from `__int128` since M12), min/max |
| `03-control/` | M6 | branches, loops, `switch` (dense, sparse, 64-bit), irreducible control flow, computed `goto`, traps |
| `04-frames/` | M7 | address-taken locals, local arrays, over-aligned frames, recursion with frames |
| `05-memory/` | M7 | loads and stores of every width, unaligned and volatile accesses, `memcpy`/`memset`/`memmove`, fences, bit builtins |
| `06-output/` | M8 | `puts`, `putchar` and the C math library through P/Invoke |
| `07-globals/` | M8 | globals, string tables, relocations, constructors, BSS; aliases and undefined weak symbols (M12) |
| `08-fnptr/` | M9 | function pointers, dispatch tables, callbacks, pointers to imports |
| `09-structs/` | M9 | structs by value and returned, bit-fields, unions, nested aggregates |
| `10-calls/` | M9 | K&R calls, many arguments, mutual and deep recursion; shared byval slots, empty `asm` with operands (`black_box`) and a Rust-shaped IR module (M12) |
| `11-programs/` | M10 | the Design.md section 12 program corpus: 20 complete programs (fib, sieve, quicksort, a string table with relocations, a function-pointer VM, ...) |

`M1/` has 143 tests: 99 C programs, 43 IR files (40 of them use
`%{msil_ll}`; the three trap tests pin their levels) and one `.test` that
compiles a C program with `-g`.

`M2/` (Phase 2, from M14) holds programs of several files that `msil-ld`
links (`%{msil_prog}`), in the tiers of plan 5.12.5; the numbers that are
missing belong to later milestones (`03-native/` M15, `04-shared/` M22,
`05-refs/` M21, `07-equiv/` M20, `09-hosted/` M15):

| Directory | Since | Covers |
|---|---|---|
| `00-link/` | M14 | two or more files: calls and data across them, `static` functions and variables of one name, weak and strong definitions, inline functions (COMDATs), common symbols, constructor priorities across files, structs by value and returned across files, K&R definitions across files, `main` in a later file, a file with data only |
| `01-archive/` | M14 | archives: lazy members, members with data only, members of one name in two archives, two archives that need each other (no group needed, as in lld), members that define `puts` and `fdim` and win over the Phase-1 imports |
| `02-lto/` | M14 | stock-clang bitcode for the native triple, also inside an archive, and `msil-ld`'s once-per-link warning about it |
| `06-det/` | M14 | determinism: `%{msil_det}` builds a program twice and compares the bitcode objects, the archive and the `--lto-emit-asm` text |
| `08-semantics/` | M14 | the link-semantics matrix of plan 5.9.16, LTO rows (`llvm/docs/MSIL/Status.md` section 2): native transcripts, or named diagnostics for duplicate, undefined and mismatched symbols, thread-local variables and destructors |

`M2/` has 38 tests (10, 5, 2, 1 and 20); 32 compare a transcript, five pin a
named diagnostic and `06-det/det-lto.test` checks determinism. Every
transcript is the same on Windows and on the E7 Linux oracle, so there is no
`.linux.expected` file. `08-semantics/p4-selectany.test` (`__declspec`)
requires a Windows triple.

`harness/` holds the self-tests of the runner, the verifier, jitall,
`update-expected.py` and lit's configuration. `harness/prog/` (M14) pins the
expansions of `%{msil_prog}`, `%{msil_c}` under both pipelines and
`%{msil_ldflags}`, and the commands of `msil-prog.py` (every leg and level,
archive specifications, `--det` catching a planted difference, the usage and
configuration errors, `update-expected.py` on `%{msil_prog}` tests) with
stand-ins for clang, `msil-ld`, `llvm-ar` and `ld.lld`, so it needs neither a
compiler nor a linker.

## Running the tests

```text
pwsh -NoProfile -File msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" msil/test/harness
python build/bin/llvm-lit.py -sv msil/test/M1/00-smoke
ninja -C build check-msil-exec       (this suite)
ninja -C build check-msil-m1-02-fp   (one directory; check-msil-m2-00-link, ...)
ninja -C build check-msil            (every MSIL test in the tree, lean dependencies)
python build/bin/llvm-lit.py -sv --param msil_pipeline=lto msil/test/M1/11-programs
```

`check-msil` runs this suite and the MSIL lit tests outside `msil/`:
`llvm/test/CodeGen/MSIL`, `clang/test/CodeGen/MSIL`,
`clang/test/Preprocessor/init-msil.c`, the driver tests
`clang/test/Driver/msil-toolchain.c`, `msil-link.c`, `msil-lto.c` and
`msil-header-search.c`, and, when lld is built, `lld/test/MSIL` (the
`msil-ld` tests). This suite depends on `lld` (which makes `msil-ld`),
`llvm-ar` and `llvm-nm` besides clang, llc and the lit tools, and
`check-msil` also on `llvm-as`, `llvm-bcanalyzer` and `llvm-lib`, which
build the inputs of `lld/test/MSIL`.

Source paths work because `llvm-lit` maps `msil/test/lit.cfg.py` to the
generated `build/tools/msil/test/lit.site.cfg.py`. At configuration time
`lit.cfg.py` runs `msil/utils/jitall/build-jitall.py`, which builds jitall into
`build/tools/msil/jitall` when it is missing or out of date. The
`check-msil-m1-<dir>` targets are created by CMake, so a new directory gets
its target at the next configure.

`pwsh -File` reads an argument that starts with `-` and contains a `:` as
`-Name:value`, so `msil-lit.ps1 -LitArgs "-sv --param msil_ilasm=D:/..."`
fails with "Missing an argument for parameter 'LitArgs'". Bind the value
with a colon (`-LitArgs:"-sv --param msil_ilasm=D:/..."`) or start it with a
blank (`-LitArgs " -sv --param ..."`); msil-lit.ps1 splits it at blanks.

The milestone-1 gate runs this suite in several legs, each a lit run with
`--param msil_require_verify=1` plus the parameters of its leg (.NET 8 with
the native oracle and .NET Framework 4.8, .NET 10, `TieredCompilation=0`,
the four extra optimization levels of `more-levels`, `lto`
(`msil_pipeline=lto`: M1's C tests and `M2/` through `msil-ld`, native
oracle required), `det` (`M2/06-det`), WSL with the linux triple on the
private runtime (`msil_wsl_dotnet=private`) and, in `wsl-net10` and
`wsl-quick`, the E7 oracle required, the Linux and the modern Windows ilasm,
the ildasm round trip), and three legs that are not lit runs: `expected`
(`update-expected.py --check` of `M1/` and `M2/`), `expected-linux`
(`update-expected.py --check --os linux`, the E7 oracle) and `utc`;
`llvm/docs/MSIL/GettingStarted.md` section 7 lists them. Each lit leg also
passes `--param msil_require_exec=1` and `--param msil_require_link=1`, and
a lit leg passes only when lit exits 0 and at least one test passed.
`lit-legs.ps1` writes a leg's lit arguments one per line into
`build/lit-results/<leg>.params.rsp` and passes `@<file>` to lit, so a
parameter value may contain blanks. `pwsh -NoProfile -File
msil/utils/run-m1-gate.ps1` runs every leg in order through the same
`msil/utils/ci/` scripts as `msil-ci.yml`, after a `host-setup` step that
installs the private WSL runtimes and the E7 sysroot (both idempotent, no
root), and prints one summary row per leg; `msil/utils/ci/lit-legs.ps1
-List` names the legs, and each leg leaves its lit log and xunit file in
`build/lit-results/<leg>.{log,xml}`.

## Adding a test

1. Put `<name>.c` (or `.ll`) in the right `M1/<NN-area>/` directory: the
   lowest-numbered failing directory names the earliest broken capability.
   The first comment line says what the test covers, the second is the RUN line:

   ```c
   // Recursion and output through puts. Exit status fib(10) = 55.
   // RUN: %{msil_c}
   #include "msil_test.h"
   ```

   Every C test uses `%{msil_c}` (since M7 added frames, plan D32). Use a
   per-level form (`%{msil_c_O2}`) only with a comment saying why; any other
   RUN line needs a comment too.
2. Generate the expected transcript from a native build with the just-built
   clang, review it, and run the directory:

   ```text
   python msil/utils/update-expected.py msil/test/M1/02-fp/<name>.c
   python msil/utils/update-expected.py --check msil/test/M1      (what CI checks)
   python msil/utils/update-expected.py --os linux msil/test/M1/02-fp/<name>.c
   python msil/utils/update-expected.py --check --os linux msil/test/M1
   ```

   On Windows run it from a VS x64 developer prompt (the native link needs
   `LIB`); its MSVC builds link the compiler-rt builtins (`--rtlib`, default
   `auto`, as `msil_native_rtlib`). `--os linux` is the E7 Linux oracle on a
   Windows host (it needs the host setup of GettingStarted.md section 7) and
   the host's own build on Linux. Written with `--os linux`,
   `<name>.linux.expected` appears only when the Linux transcript differs
   from `<name>.expected` (a stale one is removed); review it and say in the
   test why it differs. `-j N` sets the number of parallel native builds
   (default one per CPU). `.expected` files are never edited by hand; a
   `<name>.windows.expected` override is the only exception and carries a
   comment in the test explaining it.

Rules (plan 5.5, D14, D32, D37): UB-free GNU C17 (`%{msil_cflags}` pins
`-std=gnu17`, so a change of clang's default dialect cannot break the corpus)
that behaves the same natively at every level; `main` always returns
explicitly (`-ffreestanding` turns off its implicit `return 0` on Linux
triples, and `-Werror=return-type` makes falling off it an error there; MSVCRT
triples keep the implicit return); the exit status is 0..255 (bitmasks use at
most 8 bits, checksums are masked with `& 0xff`); no `long`, `long double`,
varargs, printing of addresses or of floating-point values; no library calls
other than the Phase-1 imports (D14: `puts`, `putchar` and the C math library;
`M1/06-output/` tests them), and output goes through `msil_test.h`. Every
Phase-1 capability exists since M9, so a new test may use all of them
(`MSIL_EXPECT(cond)`, the `put_*` helpers, frames, globals, function
pointers). The capability table of plan 5.5 only limited what each milestone's
tests could use; that is why tests in `01-int/` and `02-fp/` accumulate
results branch-free with `MSIL_CHECK(acc, bit, cond)` and `return acc;`.

A test of several files goes in `M2/<NN-area>/` as a `.test` file (plan
5.12.5): its sources are `split-file` parts, `REDEFINE:` lines name them, and
one RUN line is `%{msil_prog}`:

```text
# Archive members are linked only when they resolve an undefined symbol.
# REQUIRES: msil-ar
# RUN: rm -rf %t && split-file %s %t
# REDEFINE: %{msil_srcs} = %t/main.c
# REDEFINE: %{msil_archives} = help=%t/used.c,%t/unused.c
# RUN: %{msil_prog}
#--- main.c
#include "msil_test.h"
int helper(void);
int main(void) { put_i64(helper()); return 7; }
#--- used.c
int helper(void) { return 100; }
#--- unused.c
...
```

- `%{msil_srcs}` (default `%s`) are the MSIL sources, `%{msil_stock_srcs}` the
  stock-clang ones, `%{msil_archives}` the archives as
  `NAME=SRC[,SRC...][;NAME=...]` (each `lib<NAME>.a`, linked in that order
  after the objects as `-L<dir> -l<NAME>`; a `stock:SRC` member is compiled as
  a stock source) and `%{msil_libs}` further arguments of the MSIL link only.
  The values sit inside single quotes on the command line: no `'` in them,
  and no `,` or `;` in a path.
- A test that builds an archive says `REQUIRES: msil-ar`; one that needs a
  Windows triple (`__declspec`) says `REQUIRES: msil-triple-windows`.
- Common symbols use `__attribute__((common))`, not `-fcommon`, which the
  native oracle would not get, and no RUN line uses a plain `clang -c`: what
  `-c` writes changes in M19.
- A case that must stop with a named diagnostic runs
  `%if msil-prog-pipeline=lto %{ %{msil_prog_fail_O0} 2>&1 | FileCheck %s
  --check-prefix=LTO --implicit-check-not="LLVM ERROR"
  --implicit-check-not="PLEASE submit" %}` (and the same with `_O2`), and has
  no `.expected` file. A check of `msil-ld`'s own output (a warning, say) runs
  `%msil_prog_build` under the same guard.
- `update-expected.py` writes the `.expected` file as for `M1/`
  (`python msil/utils/update-expected.py msil/test/M2/00-link/<name>.test`):
  it splits the test into a temporary directory, reads the `REDEFINE:` lines
  and builds the program natively with `msil-prog.py --mode native` (`--ar`
  and `--split-file` name the tools; by default the ones next to `--clang`).
  Tests whose only composites are `%{msil_prog_fail_<L>}` or `%{msil_det}`
  have no transcript and are skipped.

## Parameters and features

| `--param` | Default | Meaning |
|---|---|---|
| `msil_triple` | host: `x86_64-pc-windows-msvc-msil` or `x86_64-unknown-linux-gnu-msil`; `x86_64-unknown-linux-gnu-msil` with `msil_runner=wsl` | triple of `%clang_msil` and `%llc_msil` |
| `msil_opt_levels` | `O0,O2` | levels `%{msil_c}` / `%{msil_ll}` run (`O0,O1,O2,O3,Os,Oz`; `%{msil_ll}` skips `Os` and `Oz`); the `more-levels` leg passes `O1,O3,Os,Oz` |
| `msil_dotnet_runtime` | `8` (`10` with `msil_runner=wsl`), else the newest | Microsoft.NETCore.App major the programs run on; a missing one is an error |
| `msil_dotnet_env` | empty | `VAR=VALUE[;VAR=VALUE...]` for programs and jitall, e.g. `DOTNET_TieredCompilation=0` (D38) |
| `msil_native_oracle` | `0` | `1`: also build natively and compare when possible; `required`: an error when it is not possible (CI). With `msil_runner=wsl` it is the E7 Linux oracle (feature `msil-linux-oracle`), for `x86_64-unknown-linux-gnu-msil` only |
| `msil_native_rtlib` | `auto` | the compiler-rt builtins the Windows native oracle links for MSVC triples (`__udivti3`, `__fixdfti`, ...): `auto` takes the newest `clang_rt.builtins-<arch>.lib` of the Visual Studio LLVM (`%VCINSTALLDIR%Tools\Llvm\x64\lib\clang\*\lib\windows`), else of the LLVM whose `clang-cl` or `lld-link` is on `PATH`; a path names one; empty or `none` disables it. Missing under `msil_native_oracle=required`, it is a configuration error |
| `msil_linux_sysroot` | `$MSIL_LINUX_SYSROOT`, else `C:\llvm-msil-scratch\linux-sysroot` if it exists (Windows), else `/` (Linux) | the Linux libraries of the E7 oracle, in the layout of `msil/utils/make-linux-sysroot.py`; a named directory without `usr/lib/x86_64-linux-gnu/libc.so.6` and `libm.so.6` is a configuration error; empty disables it |
| `msil_run_netfx` | `0` | `1`: also `ilasm -exe` and run on .NET Framework 4.8 (Windows, local runner) |
| `msil_runner` | `local` | `wsl`: run programs in WSL (`wsl.exe -d Ubuntu`), on .NET 10 by default |
| `msil_wsl_dotnet` | the private runtime if installed, else the distribution's `dotnet` (with a note) | with `msil_runner=wsl`: the dotnet host inside WSL that runs the programs and jitall. `private` is `$HOME/.dotnet-msil/dotnet`, which `msil/utils/msil-wsl-dotnet.sh` installs; when it is not installed that is a configuration error naming the script. An absolute WSL path names another host. Every WSL gate leg passes `private` |
| `msil_require_verify` | `0` | `1` (every gate): a missing ILVerify, ildasm, jitall, (Windows) PEVerify, ilasm or .NET runtime of the runner is a configuration error, even when an empty override disabled it |
| `msil_require_exec` | `0` | `1` (every gate leg): `M1/` without the feature `msil-exec` is a configuration error, not UNSUPPORTED |
| `msil_roundtrip` | `0` | `1`: `%{msil_c}` and `%{msil_ll}` also round-trip every assembly through the .NET ildasm and ilasm at the version msil/utils/tools/msil-tools.proj pins (MsilILAsmVersion) from the NuGet global-packages folder (a missing one is a configuration error naming the version) (`msil-verify.py --roundtrip`; the listings must match) and run the reassembled image against the same `.expected` (Design.md 5.5; not the Framework ildasm, which prints infinite constants as `inf`) |
| `msil_wsl_ilasm` | unset | with `msil_runner=wsl`: `%ilasm` runs this Linux ilasm (a Windows path) inside WSL through `msil-run.py --tool --runner wsl`, which stages an executable copy; `nuget` picks the newest NuGet `runtime.linux-x64.Microsoft.NETCore.ILAsm` |
| `msil_ilasm`, `msil_ildasm`, `msil_dotnet`, `msil_ilverify`, `msil_peverify` | auto | tool path; empty disables the tool; `msil_ilasm=nuget` picks the newest NuGet `runtime.<rid>.Microsoft.NETCore.ILAsm` |
| `msil_jitall` | `<build>/tools/msil/jitall` | jitall directory; empty disables jitall |
| `msil_timeout`, `msil_mem_limit_mb` | `60`, `4096` | limits for every child process |
| `msil_wsl_distro` | `Ubuntu` | WSL distribution of `msil_runner=wsl` |
| `msil_pipeline` | `asm` | how `%{msil_c}` builds (feature `msil-pipeline=<p>`): `asm` with `clang -S` and `ilasm -dll`, `lto` as a one-source `%{msil_prog}` through `msil-ld` (M14); `obj` is a configuration error until M20 |
| `msil_link_output` | `il` | what `msil-ld` writes for `%{msil_prog}` (feature `msil-link-output=<x>`): `il`, ILAsm text that `ilasm -exe` assembles; `pe` is a configuration error until M20 |
| `msil_ld`, `msil_ar`, `msil_nm` | `<build>/bin/msil-ld`, `llvm-ar`, `llvm-nm` | the tree's linker, archiver and symbol lister (also `$MSIL_LD`, `$MSIL_AR`, `$MSIL_NM`), never ones from `PATH`: the Visual Studio `llvm-ar` cannot index this LLVM's bitcode. A named path that does not exist is a configuration error; empty disables the tool |
| `msil_require_link` | `0` | `1` (every gate leg): a missing `msil-ld` or `llvm-ar` is a configuration error, and so is an `M2/` that cannot run; `msil_pipeline=lto` requires both as well |

Tool search order: `--param msil_<tool>=PATH`, `$MSIL_<TOOL>`, the value CMake
found, well-known places (the .NET Framework ilasm, and the ildasm of the
Windows SDK's NETFX 4.x Tools next to PEVerify, on Windows), `PATH`, then the
NuGet global-packages folder (`dotnet nuget locals global-packages --list`)
for ilasm and ildasm (`runtime.<rid>.Microsoft.NETCore.ILAsm` / `.ILDAsm`),
`~/.dotnet/tools` for ILVerify and the Windows SDK for PEVerify.

Features: `msil-exec`, `msil-ilasm`, `msil-dotnet`, `dotnet-runtime-<N>` (of
the runner), `msil-ld` and `msil-ar` (the tree's `msil-ld` and `llvm-ar` were
found), `msil-pipeline=<asm|lto>`, `msil-prog-pipeline=lto` (the pipeline
`%{msil_prog}` uses; `lto` under both pipelines in M14, see "What runs"),
`msil-link-output=il`, `msil-ilverify`, `msil-peverify`, `msil-ildasm`
(msil-verify.py runs its neutralized pass), `msil-jitall` (jitall.dll is
built and the runner has a runtime), `msil-netfx`,
`msil-run-netfx`, `msil-wsl`, `msil-wsl-ilasm`, `msil-roundtrip`,
`msil-native-oracle`, `msil-native-rtlib` (the compiler-rt builtins were
found), `msil-linux-sysroot` (a complete `msil_linux_sysroot`),
`msil-linux-oracle` (E7 can run: the sysroot with `libgcc_s.so.1`,
`ld.lld`, clang with the X86 target and a compiled `start.o`),
`msil-registered-target`,
`msil-triple=<triple>`, and `msil-triple-windows` / `msil-triple-linux`. The
last two follow the effective triple, not the host: the WSL runner executes a
linux triple from Windows, so a Windows-only P/Invoke test says
`REQUIRES: msil-triple-windows`, not `system-windows`.

Substitutions: `%msil_triple`, `%clang_msil`, `%llc_msil`, `%clang_native`,
`%ilasm` (ilasm with `-nologo -quiet -pe64 -x64` under the limits; callers add
`-dll` or `-exe` and `-output=`), `%dotnet`, `%msil_exec` (msil-run.py),
`%msil_verify` (msil-verify.py, with `--ildasm`/`--ilasm` when ildasm was
found), `%msil_jitall` (msil-jitall.py, with `--runner wsl` under
`msil_runner=wsl`), `%msil_roundtrip` (`msil-verify.py --roundtrip` with the
.NET ildasm and ilasm),
`%msil_inputs`, `%{msil_cflags}` (`-std=gnu17 -ffreestanding
-Werror=return-type -I msil/test/Inputs`), `%{msil_native_ldflags}` (`-lm`
when the native triple is not Windows: glibc keeps the C math library out of
libc and clang does not add `-lm` for C; `"-l<path>.lib"` of the compiler-rt
builtins for MSVC triples when `msil_native_rtlib` found them; else empty;
the native-oracle legs put it after the source, and `update-expected.py`
follows the same rule), the E7 pieces `%{msil_linux_cc}` (clang
`--target=x86_64-unknown-linux-gnu -fno-pic`), `%{msil_linux_ld}` (`ld.lld
-dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed <start.o>`),
`%{msil_linux_libs}` (the sysroot's `libm.so.6`, `libc.so.6` and
`libgcc_s.so.1`), `%{msil_linux_exec}` (`%msil_exec --native --runner wsl
--wsl-distro <distro> --expect-os linux`) and `%{msil_e7_args}` (`--clang
<clang> --linux-sysroot <sysroot> --wsl-distro <distro> --ld-lld <ld.lld>`:
lit's own E7 configuration as `update-expected.py --os linux` options, so a
test that runs `update-expected.py` uses the clang, sysroot, WSL distribution
and ld.lld that enabled `msil-linux-oracle` and not the script's defaults; an
absent sysroot or ld.lld is a `*-not-found` placeholder), `%ld_lld` and
`%lld_link` (the tree's linkers, else `PATH`, else the Visual Studio LLVM's),
`%{msil_c}`, `%{msil_ll}`,
`%{msil_c_O0}` .. `%{msil_c_Oz}` and `%{msil_ll_O0}` .. `%{msil_ll_O3}`.
For programs of several files (M14): `%msil_ld` (`msil-ld` under
`msil-run.py --tool` limits), `%msil_ar` and `%msil_nm` (the tree's `llvm-ar`
and `llvm-nm`), `%msil_prog_build` (`msil-prog.py` with the tools, the triple,
`%{msil_cflags}`, `%{msil_ldflags}` and `%{msil_native_ldflags}`; add
`--level`, `--workdir` and the inputs), the placeholders `%{msil_srcs}`
(default `%s`), `%{msil_stock_srcs}`, `%{msil_archives}` and `%{msil_libs}`
(default empty), `%{msil_ldflags}` (`-nodefaultlibs` in M14), the composites
`%{msil_prog}` (every level of `msil_opt_levels`) and `%{msil_prog_O0}` ..
`%{msil_prog_Oz}`, `%{msil_prog_fail_O0}` .. `%{msil_prog_fail_Oz}` (`not`
and the build of that level only, for named diagnostics) and `%{msil_det}`
(`msil-prog.py --det` at `-O2` in `%t.det.d`).
Everything works in lit's internal shell.

## Transcripts

`msil-run.py` writes the program's stdout, then one status line. Every run of
CRs before an LF (`\r+\n`) becomes a single LF, in program output on every
runner (local, `--native`, `--netfx`, WSL) and in `.expected` files alike,
both in `msil-run.py` (`--out`, `--expect`) and in `update-expected.py`
(written files and `--check`). The rule is idempotent, so `puts("a\r")`
gives the transcript `a\n` on Windows (text-mode stdout writes `a\r\r\n`)
and on Linux (`a\r\n`), and the `.expected` file survives being committed
with `eol=lf` (`msil/.gitattributes`). The status lines are:

| Status | Meaning |
|---|---|
| `[exit N]` | exit status N with 0 <= N <= 255 |
| `[exit N: outside 0..255]` | any other exit status: Linux would truncate it, so it never matches (D37); Windows exception codes print as `0xC0000005` |
| `[unhandled exception: T]` | the CLR reported an unhandled exception of type T (same on every OS) |
| `[signal N]` | killed by signal N (POSIX) |
| `[timeout]` | still running after the timeout |
| `[memory limit]` | hit the memory cap |

`[no newline at end of output]` precedes the status when stdout does not end
in a newline. `<stem>.<os>.expected` wins over `<stem>.expected`; `<os>` comes
from the target triple, because the triple decides `sizeof(long)` and the
import libraries (lit passes it as `--expect-os`; msil-run.py's own default
is `linux` under `--runner wsl`, else the host's OS).

A program that the dotnet host or CoreCLR does not start at all (a missing
framework, an invalid runtimeconfig.json, 'Failed to create CoreCLR') is a
configuration error (exit status 2) on every OS, never a status line: Linux
keeps only 8 bits of the host's HRESULT (150 for a missing framework) and
CoreCLR exits with 137 there. An out-of-memory start-up failure is
`[memory limit]`.

## Verification policy (plan D21)

`msil-verify.py` runs two passes, and an error in either fails the test.

1. The assembly itself. An ILVerify error passes only when an entry of
   `msil/utils/ilverify-allowed.txt` matches its code and its message
   (`CODE: MESSAGE`, both full-match regular expressions): `Unverifiable`
   (localloc, cpblk, initblk), `InitLocals` (`.locals` without init),
   `StackByRef` only as `[found Native Int] Expected ByRef on the stack.` (an
   int32, int64 or float64 address fails, also on Linux without PEVerify) and
   `UnmanagedPointer` (ldarg/ldloc of an argument or local declared `void*`;
   no Phase-1 construct produces it, D2, but D21 lists it for phase-3 typed
   signatures). On Windows PEVerify fails on any message except the
   unverifiable-by-design ones and exactly `[found Native Int][expected
   unmanaged pointer] Unexpected type on the stack.` (the native int addresses
   of `System.Buffer::MemoryCopy` for MEMMOVE, D15). ILVerify cannot verify
   `calli` ('ImportCalli not implemented' is a note). Two PEVerify reports
   come from shapes that D12 and D2 always produce, and both need the ildasm
   listing of pass 2 (plan section 12, 'D21 and M0.b item (7) refined'):
   `Cannot modify an imaged based (RVA) static` passes only where the listing
   shows `ldsflda valuetype '<msil>image_t' '<msil>image'` at the reported
   offset (the image copy in `<Module>::.cctor`; any other RVA field fails),
   and every PEVerify error in a method that contains `ldftn` (a function
   address used as a native int, which PEVerify types as a method pointer) is
   a note, because pass 2 verifies that method again. Without ildasm both
   fail.
2. A neutralized copy. ILVerify does not follow control flow out of a block
   with an error, and PEVerify stops at the first error of most kinds, so in
   pass 1 an allowed construct hides the errors behind it (a type error after
   the localloc frame or after the image ldsflda, say). msil-verify.py
   disassembles the assembly with ildasm, rewrites it so that nothing allowed
   is left (`.locals init`; localloc becomes a pop and a zero; `ldftn` and the
   image `ldsflda` become `ldc.i4.0; conv.i`, a native int as the code uses
   them; cpblk, initblk, ldind, stind and calli become calls of
   `'<msil-verify>...'` stubs that take every address as native int and check
   the sizes, values and arguments too, for example
   `cpblk(native int, native int, int32)`; short branches become long),
   reassembles it with ilasm in a private directory and runs ILVerify and
   PEVerify on it with no allowance except PEVerify's MemoryCopy message.
   Offsets in its messages refer to the copy: `--keep-copy DIR` keeps it.

MSILStackCheck (in the backend) and jitall check stack depths, `.maxstack`
and what the JIT rejects, but not stack types, which the JIT mostly accepts
silently (plan section 9); only the verifiers check those, and native
int/int64 mixing only PEVerify, so that check exists on Windows only.
`--param msil_require_verify=1` (every gate) turns a missing ILVerify, ildasm
or (Windows) PEVerify into a configuration error.

## Reproducing a failure

`lit -v` prints every command it ran with the substitutions expanded. The
intermediate files are next to the test in the build tree:
`build/tools/msil/test/<dir>/Output/<name>.c.tmp.<L>.{il,dll,out}`, and the
native oracle's next to them (`.native.exe` and `.native.out`; under the E7
oracle `.native.o`, `.native.elf` and `.native.out`). Re-run one step by
copying its command line, for example `python msil/utils/msil-run.py
--show-stderr --dotnet ... --runtimeconfig
build/tools/msil/test/msil-net8.runtimeconfig.json <dll>` to see the program's
stderr, `python msil/utils/msil-jitall.py <dll>` to find the method the JIT
rejects, or `python msil/utils/msil-verify.py ... --keep-copy <dir> <dll>` to
see the neutralized copy that pass 2 of msil-verify.py verified.

A `%{msil_prog}` test builds each level in its own directory,
`build/tools/msil/test/M2/<dir>/Output/<name>.test.tmp.<L>.d/`: `obj/` and
`stock/` hold the bitcode objects, `ar/<NAME>/` each archive and its members,
and `prog.il` and `prog.exe` are `msil-ld`'s text and ilasm's image; the
transcripts are `<name>.test.tmp.<L>.out` (and `.native.out`, `.netfx.out`,
`.rt.out`), and the native build is in `<name>.test.tmp.<L>.native.d/`.
`msil-prog.py` prints every command it runs (`msil-prog: <command>`), with
the sources relative to that directory, so a step can be re-run from there;
`clang -###` on the link line shows the `msil-ld` command, and
`-Wl,-Map=<file>`, `-Wl,--why-extract=<file>` or `-Wl,--trace` added to it
show what `msil-ld` linked and why. `python msil/utils/msil-prog.py --help`
describes every option.
