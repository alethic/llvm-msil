# tests-p2 recon: supporting files

Evidence and prototypes for [../tests-p2.md](../tests-p2.md). Nothing here is part of the build or the test suite.
Upstream sources of the corpus programs are not included: `corpus/corpus.py` expects them in the recon's scratch
directory (section 5 of the report lists URLs, versions and hashes).

| Path | What |
|---|---|
| `mdcanon/mdcanon.cs`, `build.py` | canonical, layout-independent metadata/IL listing of an assembly (System.Reflection.Metadata), built offline with the SDK's csc against the net8.0 reference pack |
| `mdcanon/compare_writers.py` | E3: bytes, ildasm listings and canonical listings of the same `.il` through two ilasms |
| `scripts/time_steps.py` | E1: wall time of each pipeline step |
| `scripts/det.sh` | E2: determinism of clang -S, clang -flto -c, both ilasms and the prototype msil-ld |
| `corpus/corpus.py`, `corpus/ctest.py` | E4: the exit-criteria candidates as multi-TU programs with archives, and c-testsuite |
| `corpus/harness/` | drivers, the 7-member support library (`sup/`), hosted-header shims (`shim/`), the CoreMark port (`coremark/`) |
| `corpus/libc.c`, `corpus/libm.c` | export-table stand-ins for `libc.so.6` and `libm.so.6` (the prototype msil-ld reads only COFF DLLs) |
| `multiasm/` | E5: an assembly referencing another one and P/Invoking a test-built native library |
| `linux-oracle/start.c` | E7: the minimal `_start` for native Linux oracle builds linked on Windows |
| `litproto/lit.cfg.py.diff`, `litproto/M2/` | E6: `msil_pipeline`, `%msil_ld`, `%{msil_prog}` with `REDEFINE:` placeholders, two multi-TU tests |
| `logs/` | E1 times, E3 diffs and a sample canonical listing, E4 results (Windows -O2/-O0, Linux -O2) |
