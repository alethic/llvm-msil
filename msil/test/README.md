# MSIL execution tests

This directory holds the end-to-end tests of the MSIL backend
(`llvm/docs/MSIL/Design.md`): C or LLVM IR is compiled for an MSIL triple,
assembled with ilasm, verified, force-JIT-compiled, run on .NET, and its
transcript is compared with a checked-in expected file. It is an LLVM external
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
  [--> native oracle]  the same source built natively must give the same transcript
  [--> .NET Framework] ilasm -exe and run on .NET Framework 4.8
```

jitall runs on the host with the local runner and inside WSL (on the WSL .NET
runtime the programs use) with `msil_runner=wsl`: the module initializer of a
linux-triple program calls libc and libm, which exist only there. ILVerify,
PEVerify, ildasm and ilasm always run on the host.

`%{msil_ll}` is the same for hand-written IR run through `llc` (`O0`..`O3`
only; `.ll` files carry no target triple or datalayout). `llc` does no IPO, so
values passed to `noinline` functions stay opaque even at `-O2`.

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
UNSUPPORTED. `msil-exec` is never required: clang with the MSIL target
arrives in later milestones.

## Running the tests

```text
pwsh -NoProfile -File msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" msil/test/harness
python build/bin/llvm-lit.py -sv msil/test/M1/00-smoke
ninja -C build check-msil-exec       (this suite)
ninja -C build check-msil-m1-02-fp   (one directory)
ninja -C build check-msil            (every MSIL test in the tree, lean dependencies)
```

Source paths work because `llvm-lit` maps `msil/test/lit.cfg.py` to the
generated `build/tools/msil/test/lit.site.cfg.py`. At configuration time
`lit.cfg.py` runs `msil/utils/jitall/build-jitall.py`, which builds jitall into
`build/tools/msil/jitall` when it is missing or out of date.

## Adding a test

1. Put `<name>.c` (or `.ll`) in the right `M1/<NN-area>/` directory: the
   lowest-numbered failing directory names the earliest broken capability.
   The first comment line says what the test covers, the second is the RUN line:

   ```c
   // Recursion and output through puts. Exit status fib(10) = 55.
   // RUN: %{msil_c}
   #include "msil_test.h"
   ```

   Use a per-level form (`%{msil_c_O2}`) only with a comment saying why: until
   frames exist (M7) every C test uses `%{msil_c_O2}` with the first-line
   comment '-O0 needs frames (M7)' (plan D32). Any other RUN line needs a
   comment too.
2. Generate the expected transcript from a native build with the just-built
   clang, review it, and run the directory:

   ```text
   python msil/utils/update-expected.py msil/test/M1/02-fp/<name>.c
   python msil/utils/update-expected.py --check msil/test/M1      (what CI checks)
   ```

   On Windows run it from a VS x64 developer prompt (the native link needs
   `LIB`). `.expected` files are never edited by hand; a per-OS override
   `<name>.windows.expected` / `<name>.linux.expected` is the only exception
   and carries a comment in the test explaining it.

Rules (plan 5.5, D32, D37): UB-free C11 that behaves the same natively at
every level; `main` always returns explicitly (`-ffreestanding` turns off its
implicit `return 0` on Linux triples, and `-Werror=return-type` makes falling
off it an error there; MSVCRT triples keep the implicit return); the exit
status is 0..255 (bitmasks use at most 8 bits, checksums are masked with
`& 0xff`); no `long`, `long double`, varargs, printing of addresses or of
floating-point values; no library call beyond `puts`, through `msil_test.h`.
Respect the capability table of plan 5.5: before M6 there are no branches, so
tests accumulate results with `MSIL_CHECK(acc, bit, cond)` and `return acc;`;
`MSIL_EXPECT(cond)` needs M6; the `put_*` helpers need M8.

## Parameters and features

| `--param` | Default | Meaning |
|---|---|---|
| `msil_triple` | host: `x86_64-pc-windows-msvc-msil` or `x86_64-unknown-linux-gnu-msil`; `x86_64-unknown-linux-gnu-msil` with `msil_runner=wsl` | triple of `%clang_msil` and `%llc_msil` |
| `msil_opt_levels` | `O0,O2` | levels `%{msil_c}` / `%{msil_ll}` run (`O0,O1,O2,O3,Os,Oz`) |
| `msil_dotnet_runtime` | `8` (`10` with `msil_runner=wsl`), else the newest | Microsoft.NETCore.App major the programs run on; a missing one is an error |
| `msil_dotnet_env` | empty | `VAR=VALUE[;VAR=VALUE...]` for programs and jitall, e.g. `DOTNET_TieredCompilation=0` (D38) |
| `msil_native_oracle` | `0` | `1`: also build natively and compare when possible; `required`: an error when it is not possible (CI) |
| `msil_run_netfx` | `0` | `1`: also `ilasm -exe` and run on .NET Framework 4.8 (Windows, local runner) |
| `msil_runner` | `local` | `wsl`: run programs in WSL (`wsl.exe -d Ubuntu`), on .NET 10 by default |
| `msil_require_verify` | `0` | `1` (every gate): a missing ILVerify, ildasm, jitall, (Windows) PEVerify, ilasm or .NET runtime of the runner is a configuration error, even when an empty override disabled it |
| `msil_ilasm`, `msil_ildasm`, `msil_dotnet`, `msil_ilverify`, `msil_peverify` | auto | tool path; empty disables the tool; `msil_ilasm=nuget` picks the newest NuGet `runtime.<rid>.Microsoft.NETCore.ILAsm` |
| `msil_jitall` | `<build>/tools/msil/jitall` | jitall directory; empty disables jitall |
| `msil_timeout`, `msil_mem_limit_mb` | `60`, `4096` | limits for every child process |
| `msil_wsl_distro` | `Ubuntu` | WSL distribution of `msil_runner=wsl` |

Tool search order: `--param msil_<tool>=PATH`, `$MSIL_<TOOL>`, the value CMake
found, well-known places (the .NET Framework ilasm, and the ildasm of the
Windows SDK's NETFX 4.x Tools next to PEVerify, on Windows), `PATH`, then the
NuGet global-packages folder (`dotnet nuget locals global-packages --list`)
for ilasm and ildasm (`runtime.<rid>.Microsoft.NETCore.ILAsm` / `.ILDAsm`),
`~/.dotnet/tools` for ILVerify and the Windows SDK for PEVerify.

Features: `msil-exec`, `msil-ilasm`, `msil-dotnet`, `dotnet-runtime-<N>` (of
the runner), `msil-ilverify`, `msil-peverify`, `msil-ildasm` (msil-verify.py
runs its neutralized pass), `msil-jitall` (jitall.dll is built and the runner
has a runtime), `msil-netfx`,
`msil-run-netfx`, `msil-wsl`, `msil-native-oracle`, `msil-registered-target`,
`msil-triple=<triple>`, and `msil-triple-windows` / `msil-triple-linux`. The
last two follow the effective triple, not the host: the WSL runner executes a
linux triple from Windows, so a Windows-only P/Invoke test says
`REQUIRES: msil-triple-windows`, not `system-windows`.

Substitutions: `%msil_triple`, `%clang_msil`, `%llc_msil`, `%clang_native`,
`%ilasm` (ilasm with `-nologo -quiet -pe64 -x64` under the limits; callers add
`-dll` or `-exe` and `-output=`), `%dotnet`, `%msil_exec` (msil-run.py),
`%msil_verify` (msil-verify.py, with `--ildasm`/`--ilasm` when ildasm was
found), `%msil_jitall` (msil-jitall.py, with `--runner wsl` under
`msil_runner=wsl`),
`%msil_inputs`, `%{msil_cflags}` (`-ffreestanding -Werror=return-type
-I msil/test/Inputs`), `%{msil_c}`, `%{msil_ll}`, `%{msil_c_O0}` ..
`%{msil_c_Oz}` and `%{msil_ll_O0}` .. `%{msil_ll_O3}`. Everything works in
lit's internal shell.

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
import libraries.

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
`build/tools/msil/test/<dir>/Output/<name>.c.tmp.<L>.{il,dll,out}`. Re-run one
step by copying its command line, for example
`python msil/utils/msil-run.py --show-stderr --dotnet ... --runtimeconfig
build/tools/msil/test/msil-net8.runtimeconfig.json <dll>` to see the
program's stderr, `python msil/utils/msil-jitall.py <dll>` to find the
method the JIT rejects, or `python msil/utils/msil-verify.py ... --keep-copy
<dir> <dll>` to see the neutralized copy that pass 2 of msil-verify.py
verified.
