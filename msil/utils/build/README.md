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
| exclusive, on the private hooks tree | `msil/utils/ci/upstream-hooks.ps1` (its compile leg runs cmake and ninja there itself; `-SelfTest` takes the same lock shared) | the hooks tree has its own `.msil-build.lock`; the main build directory's lock is not involved (below) |

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
pastes it into `lit.site.cfg.py`), `$MsilLlvmBin`, `$MsilIlasm` and `$MsilIldasm`. The
developer environment's `LIB` and `INCLUDE` matter beyond the build since M15: clang passes
each `LIB` directory to `msil-ld`, which binds the windows-msvc default libraries (`ucrt.lib`,
`kernel32.lib`, `oldnames.lib`) from there, and lit's `msil-ucrt-headers` feature looks for
the UCRT headers in `INCLUDE`; `msil-lit.ps1` enters the environment itself.

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

`-Standard` (from M13) builds every target of `msil/utils/ci/build-targets.txt`, in the file's
order, then the targets named on the command line (one already on the list is not named twice);
the targets may then be left out. That file is the one build-target list (plan 5.6.11): gate
step G1 is `msil-build.ps1 -Standard`, `msil/utils/ci/build.ps1` runs the same command, and
`build.sh` reads the same file. It has one target per line, `#` comments and blank lines; a line
with two names is an error. Among its targets are `lld`, which also makes `msil-ld` (lld's
`msil` flavor, the linker clang runs for MSIL triples), and `llvm-ar` with its copies, which
`msil/test/M2` and `lld/test/MSIL` need (M14), and `yaml2obj` and `llvm-mc`, which make the
native libraries of `lld/test/MSIL`'s native-library tests (M15; about 5-8 minutes and 0.2 GB
the first time). Without `-Standard` the script builds exactly the targets it is given and
needs at least one, as before: after a change in `lld/MSIL` or in clang's MSIL toolchain, `lld
clang` relinks `msil-ld` and clang.

**The phase gate's build.** The Phase 2 exit (M23) builds exactly this list: its Build targets
line names the 29 targets of `build-targets.txt` in the file's order (`llc`, `opt`, `clang`,
`FileCheck`, `not`, `count`, `split-file`, `llvm-config`, `llvm-profdata`, `TargetParserTests`,
`MCTests`, `MSILTests`, `lld`, `llvm-ar`, `llvm-ranlib`, `llvm-lib`, `llvm-dlltool`, `llvm-nm`,
`llvm-objdump`, `llvm-readobj`, `llvm-as`, `llvm-bcanalyzer`, `llvm-lto2`, `CLITests`,
`LLDAsLibAllTests`, `yaml2obj`, `llvm-mc`, `llvm-cil-dump`, `llvm-cil-copy`), M17's list, to which
M18-M23 added nothing. So gate step G1 (`msil-build.ps1 -Standard`) and the build step of
`msil/utils/run-m1-gate.ps1` (`msil/utils/ci/build.ps1`, which also builds jitall) are the same
build; on a tree that is current it only confirms that. The exit corpus needs no target of its
own: `msil/utils/corpus/fetch.py` is Python, and `msil/utils/ci/det-manifest.py`, the
determinism row, uses `clang` and `llvm-ar` from the list.

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 -Standard
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 -Standard llvm-dwarfdump
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 lld clang
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llc FileCheck
```

### msil-lit.ps1 (shared)

Runs `<build>/bin/llvm-lit.py` on source-tree paths; `-LitArgs` (default `-sv`) is split on
spaces, `-Jobs` adds `-j`.

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" msil/test/harness
```

The gates run `lld/test/MSIL` a second time with `-LitArgs "-sv --param
RUN_LLD_MAIN_TWICE=1"`, which makes every link run twice in one process (`LLD_IN_TEST=2`), so
global state of `msil-ld` that is not reset between links (M15's native index, say) fails a
test; the phase gate of M23 does it together with `clang/test/Driver/msil-shared.c` (only
`dispatch-coff.test`, `UNSUPPORTED: main-run-twice`, may be unsupported there). No leg of
`msil/utils/ci/lit-legs.ps1` and no row of `run-m1-gate.ps1` runs this, so run it after any
change to `lld/MSIL`:

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param RUN_LLD_MAIN_TWICE=1 --param msil_require_verify=1" lld/test/MSIL clang/test/Driver/msil-shared.c
```

The execution tests read the exit corpus from a cache outside the repository; fetch it once
(`python msil/utils/corpus/fetch.py`, into `C:\llvm-msil-scratch\corpus-cache` unless
`MSIL_CORPUS_CACHE` names another) before running `msil/test/Programs` with `msil-lit.ps1`
(GettingStarted.md section 7, "The exit corpus"). The hosted tests (M15) run with `msil-lit.ps1 -LitArgs:"-sv --param
msil_glibc_sysroot=C:/llvm-msil-scratch/glibc-sysroot" msil/test/M2/09-hosted` (the colon after
`-LitArgs`, because the value holds a drive path; see `msil/test/README.md`), or as the `hosted`
leg of `msil/utils/ci/lit-legs.ps1`.

### msil-run.ps1 (shared)

Runs `<build>/bin/<tool>.exe`, passing every further argument through verbatim.

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-run.ps1 llc -mtriple=x86_64-pc-windows-msvc-msil in.ll -o -
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-run.ps1 msil-ld --version
```

### msil-syntax.ps1 (no lock)

Compiles each file with `/Zs` (no output) using its own command from `compile_commands.json`;
headers and files without an entry borrow the flags of a TU in the same or a parent
directory, or of `-FlagsFrom <tu>`. `-ExtraInclude <dir,dir,...>` puts extra `/I` directories
first. `-NoPch` drops the PCH force-include (stricter). `-ExtraFlags <flags>` appends compiler
flags after the TU's own (`/clang:-Werror=switch` turns an unhandled enumerator into a failure),
and `-BuildDir <dir>` reads another tree's `compile_commands.json` (the private hooks tree). A
`/Zs` check costs about as much as a compile, so check single files, not directories. Needs the TableGen outputs the file includes:
before M1.0 only TUs without generated headers can be checked.

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-syntax.ps1 -FlagsFrom llvm/lib/Target/WebAssembly/WebAssemblyInstrInfo.cpp -ExtraInclude llvm/lib/Target/MSIL,build/lib/Target/MSIL,C:/scratch/tblgen llvm/lib/Target/MSIL/MSILInstrInfo.cpp
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-syntax.ps1 lld/MSIL/SymbolTable.cpp
```

A source file that a CMake list gained since the last configure has no entry yet and borrows the
flags of a TU in its directory, as M14's new `lld/MSIL` files (and M15's `NativeLibraries.cpp`)
borrowed `Driver.cpp`'s before the gate configured them.

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

## The private hooks tree

The compile leg of `msil/utils/ci/upstream-hooks.ps1` (plan M11; llvm/docs/MSIL/GettingStarted.md
section 7) compiles two hooked upstream files that the main build never compiles,
`llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp` and
`lldb/source/Plugins/Process/gdb-remote/GDBRemoteCommunicationClient.cpp`, in a build tree of its
own: `-HooksBuildDir`, else `$env:MSIL_HOOKS_BUILD_DIR`, else `C:\llvm-msil-hooks-build`
(projects clang and lldb, the ARM target only, no tests, no optional lldb dependency, the
compilers and job limits of `msil-configure.ps1`).

- **Its own lock.** The leg takes the tree's exclusive lock (`Enter-MsilBuildLock -BuildDir
  <tree>` from `msil-lock.ps1`, the file `<tree>/.msil-build.lock`) and, while it holds it, runs
  cmake (only when the tree has no `build.ninja`) and ninja there directly. It never calls
  `msil-configure.ps1` or `msil-build.ps1`: they take the same lock themselves and would wait
  for their own parent. The main build directory's lock is not involved, and the leg refuses to
  use the main build directory as the hooks tree.
- **What it runs.** `ninja -C <tree>` on the ARMAsmParser.cpp object (it also generates the ARM
  `.inc` files), then `msil-syntax.ps1 -BuildDir <tree> -ExtraFlags /clang:-Werror=switch` on
  both files. The lldb object is never built with ninja: its order-only dependencies are about
  1,000 unrelated TUs (19 min).
- **Cost.** About 47 s per run; about 13 min and 140 MB for a new tree (4 min configure, 8 min
  for the ARM object); 5-13 min after an upstream merge. The development host's tree is about
  460 MB, because the recon also built the lldb object there.
- **Never deleted by scripts.** No script deletes or cleans the tree. For a broken CMake cache,
  run `cmake --fresh` on it by hand, or point `MSIL_HOOKS_BUILD_DIR` at a new directory.

To check one of the two files by hand against the tree:

```powershell
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-syntax.ps1 -BuildDir C:/llvm-msil-hooks-build -ExtraFlags /clang:-Werror=switch llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp
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
