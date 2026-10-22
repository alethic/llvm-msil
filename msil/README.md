# msil/: the MSIL project

Everything of the MSIL backend fork that is not part of LLVM or clang proper: the execution
test suite, its harness and verification tools, the CI and gate scripts, and the build helpers
of the development host. Like LLVM, the project ships compiler tools only; nothing here is a
runtime library that compiled programs need (generated assemblies depend only on the .NET base
class library and the native libraries they P/Invoke). It is built as an LLVM *external
project*, so no upstream CMake file changes:

```sh
# CI and other hosts: the cache file sets projects, targets and the external project.
cmake -G Ninja -S llvm -B build -C msil/cmake/caches/MSIL.cmake
# or, explicitly:
cmake -G Ninja -S llvm -B build -DLLVM_EXTERNAL_PROJECTS=msil -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=<repo>/msil ...
```

On the Windows development host use `msil/utils/build/msil-configure.ps1`, which passes the
same two variables (see [utils/build/README.md](utils/build/README.md)). The MSIL target itself
is experimental: it goes in `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD`, never in
`LLVM_TARGETS_TO_BUILD`. `MSIL.cmake` enables it once `llvm/lib/Target/MSIL` exists.

## Layout

| Path | Contents |
|---|---|
| `CMakeLists.txt` | the external project; adds `test/` when `LLVM_INCLUDE_TESTS` is on |
| `cmake/caches/MSIL.cmake` | CMake cache for CI: Release + assertions, clang and lld (`msil-ld` is lld's `msil` flavor), X86, experimental MSIL, this project |
| `.gitattributes` | LF line endings for everything here (expected transcripts are compared byte for byte) |
| `test/` | lit execution tests (`check-msil-exec`, per-directory `check-msil-*`, and `check-msil`, which also runs the MSIL lit tests outside `msil/`, `lld/test/MSIL` among them): `harness/` self-tests, `M1/` milestone-1 programs (`M1/11-programs/` is the Design.md section 12 corpus); see `test/README.md` |
| `utils/msil-run.py`, `utils/msil-verify.py`, `utils/ilverify-allowed.txt`, `utils/update-expected.py` | run a test program with a timeout and memory cap and compare its transcript (also a native Linux program inside WSL, `--native --runner wsl`); ILVerify/PEVerify with an allow-list (the verification policy, plan D21); regenerate or check expected transcripts from a native build (`--os linux`: the E7 Linux oracle and `<stem>.linux.expected`) |
| `utils/linux-oracle/` | `start.c`, the minimal `_start` of the E7 Linux native oracle: it runs the program's `.init_array` constructors, then `exit(main(argc, argv))`, so a native Linux build links on Windows with `ld.lld` and no C library start files |
| `utils/make-linux-sysroot.py` | copies `libc.so.6`, `libm.so.6`, `libgcc_s.so.1` and the dynamic loader out of the WSL distribution into a sysroot outside the repository (default `$MSIL_LINUX_SYSROOT`, else `C:\llvm-msil-scratch\linux-sysroot`) for the E7 oracle, and from M15 for `msil-ld --sysroot`; idempotent |
| `utils/msil-wsl-dotnet.sh` | run inside WSL: installs the private .NET runtimes 8.0.31 and 10.0.13 of the WSL test legs into `$HOME/.dotnet-msil` with Microsoft's `dotnet-install.sh` (no root, no system setting; `--print-path` prints the dotnet host) |
| `utils/jitall/`, `utils/msil-jitall.py` | force-JIT every method of an assembly without running it |
| `utils/tools/msil-tools.proj` | NuGet download of ilasm/ildasm for Linux CI |
| `utils/build/` | PowerShell build, lit, syntax-check and TableGen helpers with the shared build lock (`msil-build.ps1 -Standard` builds the targets of `utils/ci/build-targets.txt`) |
| `utils/ci/` | the CI job steps as `.ps1`/`.sh` scripts that `.github/workflows/msil-ci.yml` and the local gate both call: configure, build (the targets of `build-targets.txt`, the one build-target list of the gates and CI, plan 5.6.11), unit tests (`CLITests` among them) and lit legs (each lit leg passes its arguments through a response file, `build/lit-results/<leg>.params.rsp`), plus the upstream-hooks check (`upstream-hooks.ps1`, Windows only, with `check-upstream-hooks.py`, which checks the manifest and judges the lit results) and the action-pin check (`check-action-pins.py`: the `msil-*.yml` workflows pin every action as upstream's workflows do) |
| `utils/run-m1-gate.ps1` | the milestone-1 gate: runs every leg (the `utils/ci/` scripts in CI order, the upstream-hooks check included, after a `host-setup` step that runs `msil-wsl-dotnet.sh` and `make-linux-sysroot.py` for the WSL legs) and prints one summary row per leg |
| `utils/upstream-hooks.txt` | the manifest of hooked upstream files: every upstream file the fork changes, with the upstream lit tests, unit tests or compile coverage that exercise the change and the fork tests that pin the MSIL side; upstream merges keep it complete (`llvm/docs/MSIL/GettingStarted.md` section 7) |
| `utils/run-utc-tests.py` | runs `update_llc_test_checks.py` tests (`msil-basic`, ...) without lit, which skips them on Windows; the `utc` leg of the gate and CI runs six of them |
| `utils/plan-check.py` | ownership, inputs and commit-scope checks of `llvm/docs/MSIL/ImplementationPlan.md` |

How to build the compiler, compile and run a C program and run the tests:
[llvm/docs/MSIL/GettingStarted.md](../llvm/docs/MSIL/GettingStarted.md) (section 7 also covers the
native oracles and the WSL host setup). What Phase 1 supports,
its verification policy and the known runtime differences:
[llvm/docs/MSIL/Status.md](../llvm/docs/MSIL/Status.md). The design is in
[llvm/docs/MSIL/Design.md](../llvm/docs/MSIL/Design.md) and the implementation plan in
[llvm/docs/MSIL/ImplementationPlan.md](../llvm/docs/MSIL/ImplementationPlan.md).
