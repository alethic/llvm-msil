# msil/: the MSIL project

Everything of the MSIL backend fork that is not part of LLVM or clang proper: the execution
test suite, its harness and verification tools, the build helpers of the development host,
and (later) the runtime support assembly. It is built as an LLVM *external project*, so no
upstream CMake file changes:

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
| `cmake/caches/MSIL.cmake` | CMake cache for CI: Release + assertions, clang, X86, experimental MSIL, this project |
| `.gitattributes` | LF line endings for everything here (expected transcripts are compared byte for byte) |
| `test/` | lit execution tests (`check-msil-exec`, `check-msil`, per-directory `check-msil-*`): `harness/` self-tests, `M1/` milestone-1 programs; see `test/README.md` |
| `utils/msil-run.py`, `utils/msil-verify.py`, `utils/update-expected.py` | run a test program with a timeout and memory cap and compare its transcript; ILVerify/PEVerify with an allow-list; regenerate expected transcripts from a native build |
| `utils/jitall/`, `utils/msil-jitall.py` | force-JIT every method of an assembly without running it |
| `utils/tools/msil-tools.proj` | NuGet download of ilasm/ildasm for Linux CI |
| `utils/build/` | PowerShell build, lit, syntax-check and TableGen helpers with the shared build lock |
| `utils/plan-check.py` | ownership, inputs and commit-scope checks of `llvm/docs/MSIL/ImplementationPlan.md` |

The design is in [llvm/docs/MSIL/Design.md](../llvm/docs/MSIL/Design.md) and the
implementation plan in [llvm/docs/MSIL/ImplementationPlan.md](../llvm/docs/MSIL/ImplementationPlan.md).
