# Recon: build environment (index of verified facts and the tested helper scripts)

Status: reconstructed 2026-10-12 by the plan reviser from the build-env recon's scripts, logs and the facts the
implementation plan quotes. The original build-env report was returned to the lead planner as chat text (the harness
refused subagent `.md` files). The tested scripts and logs are copied verbatim to
[artifacts/build-env/](artifacts/build-env/). Live copies of the same scripts are in `D:/llvm-msil/build/msil-scripts/`
until M0 moves them to `msil/utils/build/`. If the orchestrator still holds the original report text, it may append it
below, but it must keep this index.

## Host

- Windows 11 Pro, Intel i7-7700K (4 cores / 8 threads), 64 GB RAM. D: has about 34-36 GB free, which is not enough
  for a second build tree.
- Visual Studio 2026 Professional at `C:/Program Files/Microsoft Visual Studio/18/Professional`: MSVC 14.51, bundled
  clang-cl / lld-link / llvm-lib 22.1.3 (`VC/Tools/Llvm/x64/bin`), CMake 4.3.1 (on PATH only inside the dev environment),
  git-clang-format and clang-format 22.1.3.
- ninja 1.13.2 on PATH. Python 3.14.7 at `C:/Users/JeromeHaltom/AppData/Local/Python/pythoncore-3.14-64/python.exe`
  (pass it to CMake with forward slashes).
- .NET SDKs 6/8/9/10, .NET Framework 4.8 ilasm and PEVerify, modern ilasm/ildasm 10.0.12 in the NuGet global-packages
  folder `D:/packages/NuGet`, dotnet-ilverify 10.0.12, and WSL Ubuntu (stopped by default) with .NET 10.0.12 only.

## Configuration (as configured in `D:/llvm-msil/build`)

`msil-configure.ps1` (artifacts/build-env/msil-configure.ps1) runs CMake inside the VS x64 developer environment with:
Ninja, Release + assertions, projects `clang;lld`, targets `X86;WebAssembly` (MSIL is added later through
`-Experimental MSIL`, which sets `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD`; `LLVM_TARGETS_TO_BUILD=MSIL` is fatal),
`LLVM_OPTIMIZED_TABLEGEN=OFF` (ON with assertions triggers a nested native build), 8 compile jobs and 2 link jobs,
no PDBs, benchmarks or examples, `LLVM_INCLUDE_TESTS=ON` with `LLVM_BUILD_TESTS=OFF`, no zlib/zstd/libxml2/DIA/telemetry,
no static analyzer or ObjC rewriter, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, and clang-cl + lld-link + llvm-lib from the VS
bundle (`LLVM_ENABLE_LLD=ON`). The plan's section 4 has the equivalent raw command line.

Configure time: about 63 s configure plus 29 s generate on a fresh tree (`cfg-main.log`), about 34 s plus 14 s on a
reconfigure (`cfg-main2.log`).

## Build size and time

- `ninja -t commands` for the M1 target set (`llc opt clang FileCheck not count split-file llvm-config
  TargetParserTests MCTests`): 3,462 steps, of which 2,762 are C++ TUs and 242 are TableGen runs. The whole tree is
  4,817 steps. Estimated 1-2.5 h on this shared host.
- After M1, only the MSIL target and relinks are rebuilt (5-30 min).
- No build had been run when the recon finished: `build/bin` held only `llvm-lit` and the tree had no TableGen
  outputs. That is why M1 starts with a TableGen-only pre-build (plan M1.0) so implementers can syntax-check.

## Per-TU compile timing (`timing.csv`)

Two rounds of compiling five representative TUs with MSVC `cl` and with clang-cl, as an object (`obj`) and as a syntax
check only (`zs`, `/Zs`). clang-cl is as fast as or faster than MSVC, for example APFloat.cpp at 5.5 s versus 26.7 s for
the first round. A `/Zs` syntax check costs about as much as a compile, so `msil-syntax.ps1` is for single TUs, not
whole directories.

## Helper scripts (tested; artifacts/build-env/)

| Script | Purpose | Lock |
|---|---|---|
| `msil-env.ps1` | Dot-sourced. Enters the VS x64 dev shell (cached in `%TEMP%/msil-vsenv-x64.json`, about 0.1 s after the first 5 s), sets `$MsilRepo`, `$MsilBuild`, `$MsilPython`, `$MsilLlvmBin` | none |
| `msil-lock.ps1` | `Enter-MsilBuildLock` / shared lock on a file in the build dir. Exclusive for cmake/ninja, shared for lit and tool runs; waits up to `-WaitMinutes` | - |
| `msil-configure.ps1` | Configure (see above). `-Experimental MSIL`, `-Compiler msvc`, `-BuildDir`, `-Fresh` | exclusive |
| `msil-build.ps1` | `ninja -C <build> <targets>` under the exclusive lock. Drops to `-j 4` below 16 GB free RAM and `-j 2` below 6 GB, because other agents' .NET experiments have held 40 GB. `-KeepGoing` adds `-k 0` | exclusive |
| `msil-lit.ps1` | `llvm-lit` with source-tree paths under the shared lock | shared |
| `msil-run.ps1` | Run one built tool under the shared lock | shared |
| `msil-syntax.ps1` | Compile one TU with `/Zs` using its `compile_commands.json` entry (no outputs, no lock). Prints a hint naming the missing `*_gen`/`*TableGen` target when a generated `.inc` is missing | none |

`lock-test.ps1`, `shared-hold.ps1` and the `lockA*.log`/`r*.log`/`w1.log` logs (all in artifacts/build-env/) showed that a
second exclusive locker waits while a shared holder runs, and that shared holders do not block each other.

## Operational rules that follow

- Only the designated build agent runs cmake or ninja. Long builds run with `run_in_background`, and the agent waits for
  them: a background job dies with the agent's turn.
- Never `ninja check-llvm`, `check-clang` or `check-all` (they build every tool).
- `msil-syntax.ps1` needs TableGen outputs. Before the first build, the build agent must build the `*_gen` and
  `clang-tablegen-targets` targets (plan M1.0).


---

# Appendix: original build-env recon findings (restored from the planning workflow journal)

## Summary

I could not write the report file: the harness blocked it as a subagent report. Everything is below, and the tested PowerShell helpers are installed in D:\llvm-msil\build\msil-scripts\ (the build dir is gitignored by /build*): msil-env, msil-lock, msil-configure, msil-build, msil-lit, msil-run, msil-syntax (.ps1). They must stay in one directory because they dot-source each other through $PSScriptRoot. No tracked file was modified, nothing was downloaded, and no ninja build was run. Only read-only `ninja -t commands/targets` queries were run on the real tree.

CONFIGURE: succeeded. D:\llvm-msil\build is configured (Ninja, Release + LLVM_ENABLE_ASSERTIONS=ON) but not built.
- Timing: first configure 93 s (configure 62.9 s + generate 28.8 s); reconfigure 48.6 s. No CMake warnings in the real tree.
- One configuration fix: Python3_EXECUTABLE must use forward slashes (C:/Users/JeromeHaltom/AppData/Local/Python/pythoncore-3.14-64/python.exe). With backslashes, llvm/test/lit.site.cfg.py.in:19 pastes "C:\Users..." into a Python string literal and lit dies with a \U SyntaxError.
- Lit now starts, finds the unix tools in C:\Program Files\Git\usr\bin via the GitForWindows registry key, and stops at "Did not find FileCheck", as expected for an unbuilt tree.
- Options: LLVM_ENABLE_PROJECTS=clang;lld, LLVM_TARGETS_TO_BUILD=X86;WebAssembly, LLVM_EXPERIMENTAL_TARGETS_TO_BUILD empty (later: `msil-configure.ps1 -Experimental MSIL`), LLVM_OPTIMIZED_TABLEGEN=OFF, PARALLEL_COMPILE_JOBS=8, PARALLEL_LINK_JOBS=2, PDB/benchmarks/examples/telemetry/DIA/zlib/zstd/libxml2 OFF, LLVM_INCLUDE_TESTS=ON, LLVM_BUILD_TESTS=OFF, LLVM_APPEND_VC_REV=OFF, CLANG_ENABLE_STATIC_ANALYZER=OFF, CLANG_ENABLE_OBJC_REWRITER=OFF, CMAKE_EXPORT_COMPILE_COMMANDS=ON, PCH left at the default (on).
  - Why tablegen OFF: llvm/CMakeLists.txt:1008-1012 turns LLVM_OPTIMIZED_TABLEGEN plus assertions into a nested NATIVE tblgen build, and Release tblgen is already /O2.
  - Do not pass CLANG_ENABLE_ARCMT: it is deprecated (clang/CMakeLists.txt:516-519); CLANG_ENABLE_OBJC_REWRITER replaces it.

COMPILER: clang-cl 22.1.3 + lld-link + llvm-lib from VS 2026, all under C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\Llvm\x64\bin.
- Measured on 5 Support/TableGen TUs with PCH stripped, cl and clang-cl interleaved over two rounds: full compiles 30.1 s for clang-cl vs 62.9 s for cl (19.51.36260); /Zs 27.9 s vs 48.7 s.
- Minimums (CheckCompilerVersion.cmake:7-18,66-71): CLANG_MIN 5.0 and MSVC_MIN 19.28. The tree reports clang 22.1.3 simulating MSVC 19.51, so both pass.
- clang-cl also gives upstream's warning set (-Wcovered-switch-default, -Wsuggest-override, ...) and detects stale PCHs, which MSVC does not.

TARGETS:
- X86: required (native reference builds, x86 -mllvm options forwarded by the clang driver).
- WebAssembly: yes. It is the structural template; `llc -mtriple=wasm32 -debug-only=wasm-reg-stackify` gives side-by-side reference behavior for the ported passes. Cost is 65 TUs + 14 tblgen runs, about 3% of the build.
- AArch64: no for now. This disagrees with registry-tm.md 2.5. MSIL is selected by object format, clang's AArch64 TargetInfo is always compiled, the driver only passes -aarch64-* backend options for explicit -m[no-]global-merge (Clang.cpp:1673-1681), and native aarch64 output cannot run on this host. It would add about 10-15 min of build and is easy to add later.

ENVIRONMENT:
- PowerShell: Import-Module ...\Common7\Tools\Microsoft.VisualStudio.DevShell.dll, then Enter-VsDevShell -VsInstallPath <VS> -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'. Cold cost is about 4-5 s. msil-env.ps1 caches the result in %TEMP%\msil-vsenv-x64.json, bringing pwsh start plus environment to 1.3 s.
- Bash: inline cmd //c quoting fails; writing a .bat that calls vcvars64.bat and running `cmd //c <bat>` works (about 7.5 s). Simplest from Bash is `pwsh -NoProfile -File D:/llvm-msil/build/msil-scripts/<script>.ps1` with forward slashes, because Bash eats unquoted backslashes.
- The "'vswhere.exe' is not recognized" line is harmless: VsDevCmd.bat:171-186 runs vswhere by bare name, which the harness's NoDefaultCurrentDirectoryInExePath=1 blocks.
- Versions: VS 18.10.2, cmake 4.3.1-msvc1 (VS-bundled, only inside the dev env), ninja 1.13.2 (WinGet copy, first on PATH and the one CMake cached), Windows SDK 10.0.26100.0, Python 3.14.7.
- Not present: psutil (lit needs it only for --timeout), sccache, ccache, flock.
- Host: i7-7700K with 4 cores / 8 threads (not 8 cores), 64 GB RAM, D: is ReFS with 34-36 GB free (shrinking), C: has 810 GB free.

COST (exact step counts from `ninja -t commands`; time and disk are estimates):
- M1 set `llc opt clang lld FileCheck not count llvm-config split-file TargetParserTests MCTests`: 3,462 steps, 2,762 C++ TUs, 242 tablegen runs. Estimated 45-75 min on an idle machine and about 3.5-5 GB of disk.
- `all`: 4,817 steps, about 7-10 GB.
- Generated-headers bootstrap set (6.4 below): 559 steps, about 10 min. It is what makes /Zs syntax checks work on any TU that includes tablegen .inc files.
- Lit needs FileCheck, not and count (fatal if missing). clang tests also need clang and llvm-config, because clang/test/lit.cfg.py:410-416 derives the *-registered-target features from `llvm-config --targets-built`; without it, msil-registered-target tests are silently UNSUPPORTED.
- Never use check-llvm or check-clang: they build every tool.

CONCURRENCY: a reader/writer lock on D:\llvm-msil\build\.msil-build.lock, held as an open FileStream.
- Exclusive (ReadWrite + FileShare.Read) for ninja and cmake. Shared (Read + FileShare.Read) for lit and tool runs, because on Windows a running .exe cannot be relinked.
- The OS drops the lock when the holding process dies, so there are no stale locks.
- An orphan guard also waits for any ninja.exe or cmake.exe whose command line contains the build dir.
- msil-syntax takes no lock and writes nothing to the build tree.
- Background tasks are killed, whole process tree included, when an agent ends its turn (verified with TaskStop). Long builds therefore run with run_in_background and the agent must wait for them before finishing.
- msil-build.ps1 drops to -j 4 below 16 GB free RAM and -j 2 below 6 GB. Another agent's dotnet experiment was seen holding 40 GB.

OPEN ISSUE for M1 run-tests: x86_64-unknown-linux-gnu-msil with DllImport("libc") cannot resolve `puts` on this Windows host. Options are WSL (stopped, .NET there unverified), adding the windows-msvc-msil triple with ucrtbase, or gating those tests behind a lit feature.

## Verified experiments

- Enter-VsDevShell (DevShell.dll, -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64') in pwsh resolves cl 19.51.36260, clang-cl 22.1.3, lld-link 22.1.3, cmake 4.3.1-msvc1 and ninja 1.13.2 in 3.9-4.9 s. The cached msil-env.ps1 path takes 1.3 s including pwsh startup. vcvars64.bat run from a .bat via `cmd //c` from Git Bash works in about 7.5 s; inline `cmd //c '"..." && ...'` quoting fails.
- cl vs clang-cl, interleaved over 2 rounds on APInt.cpp, APFloat.cpp, CommandLine.cpp, TableGen/Record.cpp and ItaniumDemangle.cpp (PCH stripped): full compiles 62.9 s (cl) vs 30.1 s (clang-cl); /Zs 48.7 s vs 27.9 s. A cold single round gave 58.3 s vs 34.2 s.
- Main configure of D:\llvm-msil\build with clang-cl succeeded: 93 s (62.9 s configure + 28.8 s generate), no CMake warnings. Reconfigure took 48.6 s. Two parallel scratch configures (cl and clang-cl) took 167 s and 178 s; the cl one emitted a CMP194 dev warning.
- Python3_EXECUTABLE with backslashes produced 'SyntaxError: (unicode error) unicodeescape ... truncated \UXXXXXXXX' in build\test\lit.site.cfg.py line 32. Forward slashes fixed it; lit then ran its config, picked C:\Program Files\Git\usr\bin as lit tools, and stopped at 'Did not find FileCheck' (nothing built yet).
- `ninja -t commands` step counts on the real tree: M1 set 3,462 steps / 2,762 C++ TUs / 242 tblgen runs; llc 1,993; clang 3,254; lld 2,141; all 4,817; gen-headers set 559. WebAssembly is 65 TUs + 14 tblgen runs and X86 is 90 TUs + 21 tblgen runs in llc's closure. No .ninja_log or .ninja_deps was created.
- Lock experiments: an exclusive holder blocks other writers and the waiter prints the holder pid/what; taskkill /F of the holder frees the lock within 0.4 s (no stale lock); a waiter acquires 12.5 s after a 12 s hold; two shared holders overlap while an exclusive waiter waits 9.4 s until both release; the orphan guard waited for a raw ninja.exe on the dir and acquired after it exited (10.8 s); TaskStop of a background msil-build run killed all 6 processes (bash, pwsh, ninja, child) and freed the lock.
- Bash `mkdir` locking is atomic (a second mkdir is refused) but stays stale after a kill. flock is not present in Git Bash (MSYS 3.6.10).
- msil-syntax.ps1 on the real tree: APInt.cpp, MC/TargetRegistry.cpp and the headers TargetParser/Triple.h, MC/TargetRegistry.h, ADT/APInt.h and clang/Basic/TargetOptions.h pass (about 1.5-3 s each). TargetParser/Triple.cpp fails on the missing generated R600TargetParserDef.inc, with the hint. A deliberate error (s.sizee()) gives FAIL and rc 1. Three concurrent runs work. The build-tree file count is 665 before and after (no writes).
- VS-bundled llvm-mt.exe merges manifests (libxml2-enabled) and is what CMake uses as CMAKE_MT. ReFS does not block-clone `cmake -E copy` (3 x 100 MB copies used 301 MB). Unprivileged symlinks work on C: and D: (Developer Mode = 1). LongPathsEnabled = 1.
- Tools present: .NET Framework ilasm 4.8.9221.0 (not on PATH); ildasm in 'NETFX 4.8 Tools\x64'; dotnet SDKs 6.0.428/8.0.425/9.0.318/10.0.401 with runtimes up to 10.0.12; global tool dotnet-ilverify 10.0.12; Python 3.14.7 with PyYAML (no psutil, no pygments); Git 2.55.0 at C:\Program Files\Git, registered in HKLM\SOFTWARE\GitForWindows.

## Edits

- D:\llvm-msil\build (CMake cache, gitignored): Configured with the msil-configure.ps1 defaults (clang-cl/lld-link, Release+Asserts, X86;WebAssembly, clang;lld, compile_commands on). To add MSIL later, after llvm/lib/Target/MSIL exists and the LLVM_ALL_EXPERIMENTAL_TARGETS / Targets.h.cmake edits from registry-tm.md 2.4 are in, run: pwsh -NoProfile -File D:\llvm-msil\build\msil-scripts\msil-configure.ps1 -Experimental MSIL. That passes -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MSIL; experimental targets must not go in LLVM_TARGETS_TO_BUILD (llvm/CMakeLists.txt:1159-1173).
- llvm/test/CodeGen/MSIL/lit.local.cfg (future; suggestion only): ilasm is not on PATH. Locate it from an env var such as MSIL_ILASM, falling back to C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe, and add lit features 'ilasm'/'dotnet' so the run-tests are gated. dotnet and ilverify (dotnet-ilverify 10.0.12 global tool, already installed) are on PATH, and lit passes PATH through.

## New files

- D:\llvm-msil\build\msil-scripts\msil-env.ps1: Dot-sourced by the other scripts. Enters the VS 2026 x64 dev environment through Enter-VsDevShell and caches the changed variables (with PATH prefix and suffix) in %TEMP%\msil-vsenv-x64.json. Defines $MsilRepo, $MsilBuild, $MsilPython (forward slashes), $MsilLlvmBin, $MsilIlasm and $MsilIldasm.
- D:\llvm-msil\build\msil-scripts\msil-lock.ps1: Enter-MsilBuildLock -BuildDir X [-Shared] [-What s] [-WaitMinutes n]. Reader/writer lock via a FileStream on <BuildDir>\.msil-build.lock, plus the orphan ninja/cmake guard. Returns the stream; Dispose() it to release.
- D:\llvm-msil\build\msil-scripts\msil-configure.ps1: Locked cmake configure. Parameters: -Compiler clangcl|msvc, -Targets, -Experimental (MSIL later), -CompileJobs, -LinkJobs, -BuildDir, -Fresh.
- D:\llvm-msil\build\msil-scripts\msil-build.ps1: Locked `ninja -C <BuildDir> <targets>`, with -Jobs, -KeepGoing (-k 0), -WaitMinutes, and an automatic -j throttle when free RAM is low. Usage: pwsh -NoProfile -File .../msil-build.ps1 llc FileCheck not count llvm-config
- D:\llvm-msil\build\msil-scripts\msil-lit.ps1: Runs build\bin\llvm-lit.py on source-tree paths under the shared lock. Takes -LitArgs (default -sv) and -Jobs.
- D:\llvm-msil\build\msil-scripts\msil-run.ps1: Runs build\bin\<tool>.exe with verbatim pass-through arguments under the shared lock, e.g. msil-run.ps1 llc -mtriple=... x.ll -o -. Build dir override via $env:MSIL_BUILD_DIR.
- D:\llvm-msil\build\msil-scripts\msil-syntax.ps1: Lock-free single-file syntax check from compile_commands.json: drops /Fo /Fd /Yu /Yc /Fp, keeps the PCH /FI unless -NoPch (always dropped for headers), adds /Zs, uses a response file in %TEMP%. Headers and new .cpp files borrow a sibling lib TU's flags. -Obj does a real compile for timing. Exit 1 on any failure, with a hint when a tablegen .inc is missing.

## Risks

- Shared machine: another agent's dotnet experiment held ~40 GB of the 64 GB RAM (0.9 GB free), and a user process (Eggnog.Relations.Tests) used ~1.7 cores during the measurements. Build times can double; msil-build.ps1 throttles -j when RAM is low.
- The CPU is 4C/8T (i7-7700K), not 8 cores. The time estimates (M1 set 45-75 min idle) assume an effective parallelism of about 5 and an average of 4-5 s per TU; they are estimates, not measurements.
- Disk: D: free fell from 36 to 34 GB during the session because of other agents. An M1 build needs an estimated 3.5-5 GB. A second build tree belongs on C: (810 GB free) under a short path; the 115-char scratch path produced CMAKE_OBJECT_PATH_MAX warnings.
- Python3_EXECUTABLE with backslashes breaks every lit.site.cfg.py (\U escape SyntaxError). Always pass it with forward slashes, as msil-env.ps1 does.
- Background tool tasks are killed with their whole process tree when the agent ends its turn. An agent that starts a 1-hour build and finishes early aborts it; ninja recovers on the next run, but the work is lost.
- Running llc/clang/lit while ninja relinks those exes makes the link fail on Windows (file in use). Use msil-run.ps1/msil-lit.ps1 (shared lock), not raw invocations, while builds may be running.
- Syntax checks of TUs that include tablegen output fail until the gen-headers bootstrap or the first build has run (verified: llvm/lib/TargetParser/Triple.cpp needs R600TargetParserDef.inc). A concurrent tablegen rewrite can cause transient failures.
- Editing llvm/include/llvm/TargetParser/Triple.h (needed for the msil object format) is close to a full rebuild. Batch those edits.
- core.autocrlf=true (system gitconfig) gives a CRLF working tree; upstream recommends false on Windows (llvm/docs/GettingStarted.md:30). Some byte-exact upstream tests may fail; that is not an MSIL regression.
- VS updates change the toolset paths CMake recorded (14.51.36231, clang 22.1.3). Afterwards delete %TEMP%\msil-vsenv-x64.json and reconfigure with -Fresh.
- registry-tm.md 2.5 suggests building AArch64; this report recommends against it for now. The orchestrator should settle this so the configure isn't flip-flopped (every target change triggers relinks).

## Open questions

- M1 run-tests use x86_64-unknown-linux-gnu-msil with DllImport("libc") for puts, which cannot resolve under dotnet on Windows. Options: run them under WSL Ubuntu (stopped; whether a .NET runtime is installed there is unverified), add x86_64-pc-windows-msvc-msil with puts from ucrtbase, or gate stdout tests behind a lit feature.
- Should LLVM_USE_SYMLINKS=ON be adopted? Developer Mode is on and unprivileged symlinks work on C: and D: (verified). It would save about 0.8-1 GB (4 copies each of clang.exe and lld.exe); upstream Windows bots use copies.
- Should psutil be installed for lit --timeout? Package psutil==7.0.0 (abi3 win_amd64 wheel, expected to work on Python 3.14; unverified), via "C:/Users/JeromeHaltom/AppData/Local/Python/pythoncore-3.14-64/python.exe -m pip install psutil==7.0.0". Not needed for M1.
- Who runs the first ~1-hour M1 build, and when? After it, every agent can syntax-check any file and run lit. Until then only TUs without generated-header dependencies can be checked, or the 559-step gen-headers bootstrap has to be run first.
- The full markdown report could not be written to llvm/docs/MSIL/recon/build-env.md because the harness blocked subagent report files. The parent agent may want to write it from these fields.

