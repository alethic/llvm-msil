# MSIL build helpers (Windows host)

PowerShell 7 scripts that configure, build and test the llvm-msil tree on the Windows
development host, safely shared by several agents (or people) working in one checkout and
one build directory. They dot-source each other through `$PSScriptRoot`, so keep them
together in this directory. Run them with `pwsh -NoProfile -File <script> ...`; from Git
Bash use forward slashes (`pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 ...`).

Paths: the repository root is three levels above this directory; the build directory is
`<repo>/build` unless `MSIL_BUILD_DIR` is set (every script also takes `-BuildDir`, except
`msil-run.ps1`, which uses `MSIL_BUILD_DIR` only). Relative test and source paths are
relative to the repository root.

## Lock rules

`msil-lock.ps1` implements a reader/writer lock on `<build>/.msil-build.lock`, held as an open
file handle, so the operating system releases it the moment the holder exits or is killed
(no stale locks).

| Lock | Scripts | Why |
|---|---|---|
| exclusive | `msil-configure.ps1`, `msil-build.ps1` (cmake, ninja) | one writer per build directory |
| shared | `msil-lit.ps1`, `msil-run.ps1`, `msil-gtest.ps1`, `msil-expect-error.ps1` (when the program is in the build directory) | on Windows a running `.exe` cannot be relinked, so tools never run while ninja writes |
| none | `msil-syntax.ps1`, `msil-tblgen.ps1`, `msil-env.ps1` | they write nothing into the build tree |

A waiting script prints the current holder and waits (`-WaitMinutes`, default 240 for
builds and lit). The exclusive lock also waits for orphaned `ninja.exe`/`cmake.exe`
processes that still run on the build directory.

Only the designated build agent runs `msil-configure.ps1` and `msil-build.ps1`; everyone else
checks C++ with `msil-syntax.ps1` and TableGen with `msil-tblgen.ps1`. Long builds run in
the background (`run_in_background`) and the agent waits for them: **a background job dies
with the agent's turn**, whole process tree included. Never run `ninja check-llvm`,
`check-clang` or `check-all` (they build every tool).

## Scripts

### msil-env.ps1

Dot-source it: `. D:/llvm-msil/msil/utils/build/msil-env.ps1`. Enters the VS 2026 x64
developer environment (cached in `%TEMP%\msil-vsenv-x64.json`, about 0.1 s after the first
5 s; delete the file after a Visual Studio update) and sets `$MsilRepo`, `$MsilBuild`
(`MSIL_BUILD_DIR`), `$MsilPython` (`MSIL_PYTHON`, always with forward slashes because CMake
pastes it into `lit.site.cfg.py`), `$MsilLlvmBin`, `$MsilIlasm` and `$MsilIldasm`.

```powershell
. D:/llvm-msil/msil/utils/build/msil-env.ps1; & $MsilPython --version
```

### msil-lock.ps1

Dot-source it and call `Enter-MsilBuildLock -BuildDir <dir> [-Shared] [-What <text>]
[-WaitMinutes <n>]`; `Dispose()` the returned stream to release the lock.

```powershell
$lock = Enter-MsilBuildLock -BuildDir $MsilBuild -Shared -What 'my tool'; try { ... } finally { $lock.Dispose() }
```

### msil-configure.ps1 (build agent, exclusive)

Configures Ninja, Release + assertions, projects `clang;lld`, targets `X86;WebAssembly`,
clang-cl + lld-link + llvm-lib from the VS bundle, `LLVM_OPTIMIZED_TABLEGEN=OFF`,
`LLVM_BUILD_TESTS=OFF`, `compile_commands.json` on, and the `msil/` external project
(`-DLLVM_EXTERNAL_PROJECTS=msil -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=<repo>/msil`). Options:
`-Experimental MSIL` (from M2 on; never put MSIL in `-Targets`), `-NoMsilProject`, `-Fresh`,
`-BuildDir`, `-Compiler msvc`, `-Targets`, `-CompileJobs`, `-LinkJobs`.

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-configure.ps1 -Experimental MSIL
```

### msil-build.ps1 (build agent, exclusive)

`ninja -C <build> <targets>`. Drops to `-j 4` below 16 GB of free RAM and `-j 2` below 6 GB
unless `-Jobs` is given; `-KeepGoing` adds `-k 0`.

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config MCTests MSILTests
```

### msil-lit.ps1 (shared)

Runs `<build>/bin/llvm-lit.py` on source-tree paths; `-LitArgs` (default `-sv`) is split on
spaces, `-Jobs` adds `-j`.

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" msil/test/harness
```

### msil-run.ps1 (shared)

Runs `<build>/bin/<tool>.exe`, passing every further argument through verbatim.

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-run.ps1 llc -mtriple=x86_64-pc-windows-msvc-msil in.ll -o -
```

### msil-syntax.ps1 (no lock)

Compiles each file with `/Zs` (no output) using its own command from `compile_commands.json`;
headers and files without an entry borrow the flags of a TU in the same or a parent
directory, or of `-FlagsFrom <tu>`. `-ExtraInclude <dir,dir,...>` puts extra `/I` directories
first. `-NoPch` drops the PCH force-include (stricter). A `/Zs` check costs about as much as a
compile, so check single files, not directories. Needs the TableGen outputs the file includes:
before M1.0 only TUs without generated headers can be checked.

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-syntax.ps1 -FlagsFrom llvm/lib/Target/WebAssembly/WebAssemblyInstrInfo.cpp -ExtraInclude llvm/lib/Target/MSIL,build/lib/Target/MSIL,C:/scratch/tblgen llvm/lib/Target/MSIL/MSILInstrInfo.cpp
```

### msil-tblgen.ps1 (no lock)

Runs `<build>/bin/llvm-tblgen.exe` on `llvm/lib/Target/MSIL/MSIL.td` with the build's flags for
`-gen-asm-writer`, `-gen-dag-isel`, `-gen-instr-info`, `-gen-register-info`,
`-gen-sd-node-info` and `-gen-subtarget` (or the backends named by `-Gen a,b`), writing
`MSILGen*.inc` into `-Out` only. It refuses an `-Out` inside the repository or the build
directory, deletes a failed backend's old outputs, and exits 1 on any TableGen error.

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-tblgen.ps1 -Out C:/scratch/tblgen -Gen instr-info,dag-isel
```

### msil-gtest.ps1 (shared)

Runs a gtest binary with `--gtest_filter` and fails on a non-zero exit, on a missing or too
small `[  PASSED  ] N tests.` count (`-MinTests N`), and with `-NoSkip` on any `[  SKIPPED ]`
line: `GTEST_SKIP` exits 0, so a plain run cannot tell a skipped test from a passing one.

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-gtest.ps1 -Exe D:/llvm-msil/build/unittests/Target/MSIL/MSILTests.exe -Filter '*MSIL*' -NoSkip -MinTests 3
```

### msil-expect-error.ps1 (shared when the program is in the build directory)

Passes only if the program exits with exactly `-ExitCode` (default 1), its combined
stdout+stderr matches `-Pattern`, and the output has none of `PLEASE submit a bug report`,
`LLVM ERROR`, `Stack dump`, `Exception Code:`. On Windows `report_fatal_error` ends in
`abort()` (exit code 3), which plain `not` accepts as an ordinary failure. Everything after
`--` goes to the program verbatim; a bare tool name runs `<build>/bin/<tool>.exe`.

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-expect-error.ps1 -Pattern 'error: .*variadic' -- llc -mtriple=x86_64-pc-windows-msvc-msil vararg.ll -o NUL
```

## Plan checks

`msil/utils/plan-check.py` (Python standard library only) checks
`llvm/docs/MSIL/ImplementationPlan.md`: `ownership` (owned files disjoint inside every
milestone), `reads M<n>` (every input a work package reads exists), `scope M<n>` (every staged
path belongs to the milestone; run `git add -A` first) and `--self-test`. A plan it cannot
parse completely (a malformed milestone or work-package heading, a work package without its
one-line `- Owned files:` list, a wrapped list, a malformed fix-loop row) fails every command
with exit status 2 instead of dropping the work package from the checks.

```powershell
python D:/llvm-msil/msil/utils/plan-check.py ownership
```
