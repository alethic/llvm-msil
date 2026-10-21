# P2 recon: test strategy and gates for Phase 2 (`tests-p2`)

Recon for Phase 2 planning, 2026-10-21, on branch `msil` at `1ea64fe9fc7c` (M11). Inputs: Design.md (sections 5-8, 11),
ImplementationPlan.md sections 1-5, 7 and the section 12 scope rows of 2026-10-14/15, Status.md, the Phase-2 planning
inputs (`msil/orchestration/P2/inputs.md`, git-ignored), the Phase-1 recon reports, and the sibling P2 recon reports
that existed when this one was written (`lld-flavor.md`, `mc-streamer.md`, `metadata-writer.md`, `bcl-binding.md`;
the object-format report `objfmt-container` was not written yet, so everything here is stated for every P2-A
candidate). Experiments ran in `C:\llvm-msil-scratch\p2\tests-p2\` on the shared host (4C/8T, other agents active, so
timings carry noise; medians are given). Supporting files: [tests-p2-files/](tests-p2-files/).

Marks: **[V]** verified by an experiment in this recon, **[S]** read in this checkout's source, **[U]** unverified.

---

## 0. Summary

**Findings**

1. **The Phase-1 harness generalizes, but three of its assumptions are single-file, single-assembly ones.**
   `msil/test/lit.cfg.py` builds every execution test from one source through one composite (`c_pipeline`,
   `ll_pipeline`, lit.cfg.py:1002-1049 [S]); `msil-run.py` copies exactly one file (plus a runtimeconfig) into its
   private directory (`run_local`, msil-run.py:587-640; the copy is line 610 [S]), so a program that references another assembly fails with
   `[unhandled exception: System.IO.FileNotFoundException]` [V], and one that P/Invokes a test-built native library
   fails with `DllNotFoundException` [V]; `update-expected.py` recognizes only `%{msil_c...}`/`%{msil_ll...}` RUN lines
   (`ORACLE_RUN_RE`, update-expected.py:62 [S]). Everything else (verification policy, jitall, legs, WSL runner,
   transcript format, CI scripts) carries over unchanged.
2. **lit can express multi-TU tests today without new lit features.** A prototype (`tests-p2-files/litproto/`)
   adds `--param msil_pipeline=asm|lto|obj`, `%msil_ld`, `%msil_ar` and a composite `%{msil_prog}` parameterized by
   `%{msil_srcs}`/`%{msil_libs}`, which a test sets with lit's `REDEFINE:` directive; `split-file` holds the TUs in
   one test file, and lit's internal shell expands `*.o`. Two prototype tests (two TUs with cross-file data
   relocations and a function pointer; an archive whose unused member's duplicate definition and constructor must not
   take effect) pass [V]. With `msil_pipeline=lto` the whole `msil/test/M1` suite passes **136/136**, its 93 C tests
   built through the prototype link path (`clang -flto -c` + the lld-flavor recon's prototype
   `msil-ld --lto-emit-asm` + ilasm; the IR tests stay on llc in the prototype), in 297 s against 311 s for the
   Phase-1 path on the same host (8 workers, .NET 8, verification enforced) [V]: linking costs nothing measurable
   per test.
3. **Per-test cost is dominated by verification, not by compiling or linking** (E1, 40 program-levels, medians):
   clang -S 0.29 s, ilasm 0.08-0.10 s, msil-verify 3.21 s, jitall 0.78 s, run through msil-run.py 0.70 s; the
   prototype link path clang -flto -c 0.22 s + msil-ld 0.19 s + ilasm 0.10 s [V]. Phase 2's extra legs therefore cost
   about what the corresponding Phase-1 legs cost.
4. **An equivalence oracle needs a canonical, layout-independent dumper; ildasm alone is not enough.** A 500-line C#
   prototype over System.Reflection.Metadata (`tests-p2-files/mdcanon/mdcanon.cs`: tokens printed as the entities
   they name, branch targets as instruction labels, RVA fields as their bytes, members sorted) gives:
   - Framework ilasm 4.8 vs .NET ilasm 10.0.12 on the same `.il` (40 program-levels): bytes differ 40/40, canonical
     dumps equal 40/40 (ildasm listings also equal once MVID, image base and module lines are dropped) [V];
   - the metadata-writer recon's independent Python PE writer vs ilasm on the same program: the dump pinpoints every
     real difference (assembly hash algorithm, module name, `.maxstack`, `initlocals`, the Param rows ilasm adds
     for parameters, including `A_0` for a P/Invoke stub, and the float32 signaling-NaN constant ilasm quiets) [V];
   - moving one method to the end of the `.il`: ildasm diff 164 lines, canonical dump 0 lines [V];
   - flipping one IL opcode or one data byte: one line of difference each [V].
   PEVerify's metadata validation (`/md`, not run today: msil-verify.py:338 passes `/nologo /il` [S]) is not clean
   even for ilasm output (2 errors on the entry-point token plus one warning per global member) [V], so it can only
   be used **differentially** (msil-ld output may not have /md messages that ilasm's output of the same program lacks).
5. **Determinism** (E2) [V]: `clang -S` is byte-identical across runs and directories with relative paths (an
   absolute source path changes the `// source_filename` line); `clang -flto -c` bitcode embeds the working directory
   (a `DICompileUnit` with `emissionKind: NoDebug`), fixed by `-fdebug-compilation-dir=.`; both ilasms differ in 17
   bytes between runs (MVID, timestamp); .NET ilasm `-DET` is reproducible across runs and directories but records
   the `-output` path's directory part in the module name (an absolute `-output` changes the image); the prototype
   msil-ld's `--lto-emit-asm` text is identical across runs and directories.
6. **Exit-criteria programs: nine realistic multi-TU programs already pass end to end** through the prototype link
   path with a 7-member support archive (arena allocator, `memcmp`/`memset`/`memcpy`/`strlen` and `malloc` only on
   MSIL, puts-based output) and minimal hosted-header shims: jsmn, TweetNaCl, Monocypher, BLAKE2 (keyed KATs of 4
   variants), zlib (Z_SOLO, 12 deflate/inflate round trips), tinf + zlib (two archives with same-named members),
   LZ4 + LZ4 HC, KISS FFT (libm P/Invokes) and CoreMark (self-validating CRCs). On `x86_64-pc-windows-msvc-msil` every
   one matches its native build on .NET 8, .NET 10 and .NET Framework 4.8 at -O0 and -O2 with verification and
   jitall clean; on `x86_64-unknown-linux-gnu-msil` (-O2) every one matches its native Linux build under WSL .NET 10 (with export-table
   stand-ins for `libc.so.6`/`libm.so.6`, which the prototype cannot read) [V]. All nine take 125 s sequentially
   for one triple and level, native builds and three runtimes included. c-testsuite: 150 of its 220 tests need no
   libc; they pass 149/150 through `-S` (one variadic definition) and 150/150 through the link path [V].
7. **A native Linux oracle needs no compiler in WSL** (closes the M10 leftover "No Linux-native oracle"): the
   shared clang (`--target=x86_64-unknown-linux-gnu`), VS's `ld.lld`, copies of WSL's `libc.so.6`/`libm.so.6` and a
   20-line `_start` that runs `.init_array`, `main` and `exit` reproduce all **93/93** C `.expected` files of
   `msil/test/M1` [V]. The same toolchain cross-builds `libnative.so` for P/Invoke tests that run in WSL [V].
8. **Tool versions matter for archives:** the VS-bundled `llvm-ar` 22.1.3 refuses to index Monocypher's LLVM-24
   bitcode (`Unknown attribute kind (110)`) [V]. Gates must build the tree's `llvm-ar` (5 TUs, lld-flavor 5).

**Recommendations**

- R1. **Keep one test format and switch pipelines by parameter:** `--param msil_pipeline=asm|lto|obj`, default `asm`
  until the Phase-2 exit gate, `obj` after it, with an `asm` leg kept as the oracle (4.1). The 132 composite tests of
  M1 need no edit; 4 explicit-RUN tests and 23 driver/llc RUN lines that pin "no objects" do (section 7).
- R2. **Multi-TU tests:** new `msil/test/M2/<NN-area>/` directories (00-link ... 07-equiv), split-file sources,
  `REDEFINE: %{msil_srcs}` / `%{msil_libs}` / `%{msil_native_libs}`, one composite `%{msil_prog}` (4.2, 4.3).
- R3. **Oracles, in this order** (3.8): O1 native transcript (exists), O2 the Design.md 5.5 round trip on msil-ld
  output, O3 single-TU equivalence `canon(msil-ld PE) == canon(ilasm(clang -S))` with a short, named allow-list,
  O4 LTO equivalence `canon(msil-ld LTO PE) == canon(ilasm(msil-ld --lto-emit-asm))` for multi-TU programs, O5 the
  D21 verification on msil-ld output, O6 differential PEVerify `/md`.
- R4. **Dumpers:** a C++ `llvm-cil-dump` (metadata-writer recon) for FileCheck tests in `check-msil`, with a
  `--canonical` mode that replaces the C# prototype as the O3/O4 comparator; the C# `mdcanon` (built offline like
  jitall) until then. Object-file dumping depends on the P2-A format (3.9).
- R5. **msil-run.py `--dep FILE` (repeatable)**, copied into the private directory (and the WSL temp directory);
  jitall and ILVerify get the same list (`-r`). Needed by P2-C and by every native-import test.
- R6. **Exit criteria:** the nine programs of finding 6 (section 5), sources fetched by a pinned manifest into a
  cache outside the repository, never vendored; drivers, ports and `.expected` files are ours and live in
  `msil/test/Programs/`.
- R7. **Gates:** a fast milestone gate (about 25 min) and a full phase gate (about 75-90 min on this host; 6.2);
  once `obj` is the default, drop duplicated asm legs to keep the full run near today's 47 min plus 25 min.

---

## 1. Today's harness (inventory)

### 1.1 Files

| File | Lines | Role |
|---|---|---|
| `msil/test/lit.cfg.py` | 1088 | tool discovery, parameters, features, substitutions; composites `c_pipeline` (1002-1023), `ll_pipeline` (1026-1049), substitution list (1055-1088) |
| `msil/test/lit.site.cfg.py.in` | 28 | generated site config (paths, found tools) |
| `msil/test/CMakeLists.txt` | 96 | `check-msil-exec`, `check-msil-<dir>`, umbrella `check-msil`; line 92: "Phase 2 adds `${LLVM_MAIN_SRC_DIR}/test/MC/MSIL` and `lld/test/MSIL` here" |
| `msil/test/M1/lit.local.cfg` | 31 | UNSUPPORTED without `msil-exec`; fatal under `msil_require_exec=1` |
| `msil/test/Inputs/msil_test.h` | 109 | `MSIL_CHECK`, `MSIL_EXPECT`, `put_*` (puts only) |
| `msil/utils/msil-run.py` | 944 | private-directory run, timeout, memory cap, transcript, `--native`, `--netfx`, `--runner wsl`, `--tool` |
| `msil/utils/msil-verify.py` | 1003 | D21: ILVerify allow-list + PEVerify `/il`, neutralized second pass, `--roundtrip` |
| `msil/utils/msil-jitall.py`, `jitall/` | 363 + | force-JIT every method |
| `msil/utils/update-expected.py` | 280 | native oracle writes/checks `.expected`; only `%{msil_c...}`/`%{msil_ll...}` tests (line 62) |
| `msil/utils/ci/lit-legs.ps1` / `.sh` | 200 / 203 | the legs (table 1.3) |
| `msil/utils/run-m1-gate.ps1` | 280 | configure, build, unit tests, legs, upstream-hooks, ci-sh smoke; 23 rows, 47 min in M11 |
| `.github/workflows/msil-ci.yml` | | dispatch-only; Linux (ubuntu-24.04, .NET 8/10) and Windows (windows-2022, .NET 8/10/Framework) jobs call the same scripts |

### 1.2 The pipeline per test and level

`%{msil_c}` = for each level of `msil_opt_levels` (default `O0,O2`): `%clang_msil %{msil_cflags} -<L> -S` → `%ilasm -dll`
→ `%msil_verify` → `%msil_jitall` → `%msil_exec --expect %S/%{s:stem}`, plus `%if` legs: `msil-roundtrip` (ildasm,
ilasm, compare listings, run again), `msil-native-oracle` (native build and run against the same `.expected`),
`msil-run-netfx` (ilasm `-exe`, run on Framework). `%{msil_ll}` is the same with `%llc_msil`. Counts in M1: 93
`%{msil_c}` tests, 39 `%{msil_ll}`, 4 with explicit RUN lines (three trap `.ll` tests and
`01-int/calls-arith-debug-info.test`) [S].

### 1.3 Legs and their measured cost (M11 gate run of 2026-10-20, `build/lit-results/*.xml` and `.log`)

| Leg | Paths | Tests | lit wall | Sum of test times |
|---|---|---|---|---|
| net8 (+ native oracle required + netfx) | all MSIL lit dirs + msil/test | 327 | 513 s | 4006 s |
| net10 | M1 | 136 | 217 s | 1663 s |
| net8-tc0 | M1 | 136 | 232 s | 1745 s |
| more-levels (O1,O3,Os,Oz) | M1 | 136 | 390 s | 2926 s |
| wsl-net10 | M1 + harness | 195 | 402 s | 3115 s |
| wsl-net10-tc0 | M1 | 136 | 325 s | 2501 s |
| wsl-ilasm | 11-programs | 20 | 66 s | 400 s |
| modern-ilasm | 11-programs | 20 | 42 s | 241 s |
| roundtrip | 11-programs | 20 | 56 s | 321 s |
| expected, utc, upstream-hooks | | | about 3-5 min together | |

The whole `run-m1-gate.ps1` (incremental build included) took 47 min (HANDOFF, M11 row).

### 1.4 Gaps for Phase 2

| Gap | Where | Needed for |
|---|---|---|
| One source per test, one composite per level | lit.cfg.py:1002-1049 | multi-TU, archives, LTO mixes |
| Private directory holds only the program | msil-run.py:606-616 (`shutil.copy2(prog, copy)` at 610) | P2-C (assembly references), native-import tests with test-built libraries, `.NET Framework` probing |
| `update-expected.py` handles only single-source tests | update-expected.py:62, 183-200 | `.expected` for multi-TU tests |
| No linker, no archiver, no dumper substitutions | lit.cfg.py:1055-1088 | everything in section 3 |
| Native oracle builds one file, links `-lm` only | lit.cfg.py:989-990, 1014-1018 | multi-TU native builds with the same archives |
| PEVerify runs `/il` only | msil-verify.py:338 | metadata validation of a new writer |
| ILVerify and jitall load one assembly | msil-verify.py, jitall.cs | P2-C (references between test assemblies) |
| Linux native oracle missing locally | lit.cfg.py:846-851 (`runner != "local"`) | Linux-specific `.expected`, `-lm`/`DT_SONAME` tests |

---

## 2. Experiments

All under `C:\llvm-msil-scratch\p2\tests-p2\` (E-numbers are subdirectories). Scripts are in
[tests-p2-files/](tests-p2-files/). Nothing was installed; the only downloads are the corpus sources of 2.7.

### 2.1 E1: cost of each pipeline step

`scripts/time_steps.py` ran the 20 `11-programs` at -O0 and -O2 sequentially (40 runs; log `logs/e1-times.json`):

| Step | Median | Mean | Max |
|---|---|---|---|
| clang -S (MSIL) | 0.29 s | 0.29 | 0.51 |
| ilasm 4.8 | 0.10 | 0.12 | 0.33 |
| ilasm 10.0.12 | 0.08 | 0.10 | 0.37 |
| msil-verify.py (both passes) | 3.21 | 3.55 | 11.05 |
| msil-jitall.py | 0.78 | 0.80 | 1.20 |
| msil-run.py, .NET 8 | 0.70 | 0.75 | 1.46 |
| ildasm 10.0.12 | 0.06 | 0.09 | 0.50 |
| clang -flto -c | 0.22 | 0.22 | 0.51 |
| prototype msil-ld --lto-emit-asm | 0.19 | 0.24 | 1.39 |
| ilasm 4.8 of the msil-ld output | 0.10 | 0.12 | 0.38 |

Transcripts of both paths were equal 40/40; the IL text was never identical (0/40): LTO runs its own pipeline and
the msil-ld text has no verbose-asm comments. A bare `dotnet exec` of these programs takes 0.10-0.20 s; most of
msil-run.py's 0.7 s is Python start-up, the Job Object and the private copy.

### 2.2 E2: determinism (`scripts/det.sh`, program `dispatch-vm.c`, -O2)

| Step | Same directory twice | Other directory, relative paths | Absolute paths |
|---|---|---|---|
| `clang -S` | identical | identical | differs: `// source_filename = "<absolute path>"` |
| `clang -flto -c` | identical | **differs** (cwd in `!DIFile(... directory:)` of a `DICompileUnit` with `emissionKind: NoDebug`) | differs; `-ffile-prefix-map` alone did not fix it |
| `clang -flto -c -fdebug-compilation-dir=.` | identical | identical | |
| ilasm 4.8 | differs (17 bytes) | | |
| ilasm 10.0.12 | differs (17 bytes) | | |
| ilasm 10.0.12 `-DET` | identical | identical (same relative `-output`) | differs with an absolute `-output`: the module name keeps the path's directory part (seen as `.module 'fib.O2/new.dll'`) |
| prototype msil-ld `--lto-emit-asm` | identical | identical | |

Consequences for tests: determinism tests compare builds made from **relative paths in two different directories**
and pass `-fdebug-compilation-dir=.` (and, for objects with debug info later, `-ffile-prefix-map`); msil-ld must take
the module name from the basename of `-o` (Roslyn does; metadata-writer 4.10 hashes it into the MVID).

### 2.3 E3: comparing two writers of the same program

- **Framework ilasm vs .NET ilasm** (`mdcanon/compare_writers.py`, the 40 E1 assemblies): raw bytes differ 40/40;
  ildasm 10.0.12 listings equal 40/40 after dropping `// MVID`, `// Image base` and `.module`; canonical dumps
  equal 40/40 [V]. So one ilasm suffices as the reference side of an equivalence check.
- **An independent writer vs ilasm**: the metadata-writer recon's `cliwriter.py` hello program against ilasm of its
  `.il` twin (same name for both outputs). The canonical diff (`logs/mdcanon-python-writer-vs-ilasm.diff`, 40 lines)
  names six kinds of difference: `hash=Sha1` vs `None` in the Assembly row; `.maxstack 8` (the prototype's
  constant) vs the declared depth; `initlocals`; ilasm's Param rows (`.param 1 'args'`, and `'A_0'` for
  parameters without a name, including the P/Invoke stub's); the long method name (1000 vs 2000 bytes by
  construction); `ldc.r4 float32(0x7F800001)` vs ilasm's quieted `0x7FC00001` [V]. The ildasm diff
  (`logs/ildasm-python-writer-vs-ilasm.diff`, 56 lines) shows the same plus layout noise (`at I_00002228` vs
  `I_00002060`, `.imagebase`) and parameter names that ildasm invents (`A_0`).
- **Row order**: one method moved to the end of a `.il` gives 164 ildasm diff lines and 0 canonical diff lines
  (146 with `--order=decl`) [V]. A direct writer will not order rows exactly like ilasm (P/Invoke stubs, address
  fields and synthesized members are emitted by msil-ld, not in source order), so a sorted dump is required.
- **Sensitivity**: `ldc.i4.1` → `ldc.i4.2` in one method and one flipped data byte each give exactly one diff line [V].
- **Cost**: about 1.4 s per assembly per dump (process start plus JIT of SRM); 3,544 lines for `dispatch-vm`. A
  C++ dumper (or a batch mode) makes it negligible.
- **PEVerify /md**: on ilasm output of fib it reports `0x80131B21` and `0x80131B23` at the entry-point method token
  plus one `0x8013129D` warning per global field or method (26 for fib); the Python writer's hello adds `0x801312E6`
  (the 2000-byte name; metadata-writer 0 item 8 names it `VLDTR_E_TD_NAMETOOLONG`) [V]. The message texts are not
  printed on this host (only "Internal error: <hresult>"), so the names of the other codes are **[U]**.

### 2.4 E4: multi-TU programs with archives (exit-criteria candidates)

`corpus/corpus.py` builds each program for a triple and level:

```text
clang --target=<t>-msil -O2 -flto -c   each library source  -> llvm-ar rcs lib<x>.a     (one archive per library)
clang --target=<t>-msil -O2 -flto -c   support sources      -> libsup.a (7 members)
clang --target=<t>-msil -O2 -flto -c   driver sources
msil-ld --lto-emit-asm -o p.il <driver objs> -L. -l<x>... -lsup <native libs> -Map p.map --why-extract=why.txt
ilasm -dll -pe64 -x64; msil-verify.py; msil-jitall.py; msil-run.py on .NET 8, .NET 10 and (ilasm -exe) Framework 4.8
native: the same sources with --target=<t>, linked by lld-link (Windows) or ld.lld + the E7 oracle (Linux)
```

Common flags: `-ffreestanding -isystem harness/shim -Werror=implicit-function-declaration`; the shims
(`string.h`, `stdlib.h`, `stdio.h`, `math.h`, `assert.h`, 5-12 prototypes each) are used on both sides, so the
MSIL build needs no SDK headers (the MSIL driver does not find UCRT headers by itself, TestCorpus BLAKE2 row) and
the native build gets the real C runtime functions. Results, `x86_64-pc-windows-msvc-msil`, -O2
(`logs/corpus-results-windows-O2.json`):

| Program | Program TUs (+7 support) | Archives | Extracted members | IL text | Methods after LTO | Data segment | msil-ld | verify | Native, .NET 8, .NET 10, Framework |
|---|---|---|---|---|---|---|---|---|---|
| jsmn | 2 | sup | 1 | 22 KB | 4 | 5.6 KB | 0.12 s | 2.2 s | all equal |
| TweetNaCl | 3 | nacl, sup | 2 | 187 KB | 18 | 9 KB | 0.60 | 3.6 | all equal |
| Monocypher 4.0.2 | 3 | monocypher*, sup | 3 | 516 KB | 39 | 22 KB | 1.07 | 2.9 | all equal |
| BLAKE2 ref | 7 | b2, sup | 8 | 360 KB | 15 | 53 KB | 0.54 | 2.9 | all equal |
| zlib 1.3.2 Z_SOLO | 10 | z, sup | 10 | 336 KB | 23 | 17.1 MB (16 MB arena BSS) | 1.07 | 2.5 | all equal |
| tinf + zlib | 15 | z, tinf, sup | 11 | 275 KB | 27 | 16.9 MB | 0.76 | 2.4 | all equal |
| LZ4 1.10.0 (+HC) | 3 | lz4, sup | 4 | 283 KB | 10 | 884 KB | 1.38 | 2.4 | all equal |
| KISS FFT (double) | 3 | kiss, sup | 6 | 45 KB | 18 | 16.8 MB | 0.18 | 2.7 | all equal |
| CoreMark (200 iterations) | 7 | coremark, sup | 6 | 65 KB | 8 | 7 KB | 0.21 | 2.2 | all equal |

\* VS llvm-ar 22.1.3 could not index Monocypher's bitcode; that library went in as `--start-lib ... --end-lib`
(same lazy semantics). Every run exits 0; jitall clean everywhere; the P/Invokes are `puts` everywhere and
`cos`/`floor`/`sin`/`sqrt` for KISS FFT. -O0 and the Linux triple (9/9 each): section 5.2. These are LTO builds,
so they test the P2-B front half (archives, lazy extraction, symbol resolution, native imports) and codegen of the
merged module, not yet CIL objects or the PE writer; the same drivers and lit tests run unchanged in `obj` mode.

What the set exercises: archives with lazy extraction (the `why.txt` lists show e.g. `inflate.o` not pulled for
the tinf program), two archives with members of the same name (`adler32.o`, `crc32.o` in libz and libtinf; the
first native build of my own script overwrote one with the other, a reminder that archive tests need per-archive
object directories), cross-TU data relocations, structs passed by value across TUs (Monocypher's
`crypto_argon2(..., config, inputs, extras)`), an address-taken libc name defined in an archive (BLAKE2's
`memset` behind a volatile function pointer), a 16 MB zero-initialized data segment, C math P/Invokes from an
archive member, `unsigned long` differing between the triples (TweetNaCl), a 2-TU header-only library whose TUs
must agree on `JSMN_PARENT_LINKS` (jsmn), and CoreMark's own CRC validation.

c-testsuite (`corpus/ctest.py`): 150 of 220 tests call no libc function (68 need printf, 2 other libc), all with
empty expected output. -O2, Windows triple, .NET 8, 4 workers, no verify/jitall: Phase-1 path 149/150 in 51 s
(`00140.c` defines a variadic function), link path 150/150 in 40 s (LTO removes it) [V].

### 2.5 E5: several assemblies and native libraries at run time

`multiasm/`: `mylib.dll` (ilasm, public class `Contoso.Native` with `add`), `prog.dll` referencing it and
P/Invoking `add3` from `native` [V]:

| Case | Result |
|---|---|
| `msil-run.py` (private directory holds only prog.dll) | `[unhandled exception: System.IO.FileNotFoundException]` |
| prog.dll + mylib.dll + native.dll in one directory, .NET 8 | exit 42 |
| prog.exe (ilasm `-exe`) + mylib.dll + native.dll, .NET Framework 4.8 | exit 42 |
| WSL .NET 10, prog.dll + mylib.dll + `libnative.so` | exit 42 |
| WSL without `libnative.so` | `DllNotFoundException`, probing `<appdir>/native.so`, `<runtime dir>/native.so`, ... |

`native.dll` (2,048 bytes) was linked with `lld-link -dll -noentry -nodefaultlib` and `libnative.so` (1,968 bytes)
with VS's `ld.lld -shared -soname libnative.so`, both from objects of the shared clang (`--target=x86_64-pc-windows-msvc`
and `--target=x86_64-unknown-linux-gnu -fPIC`): no SDK library, no WSL compiler.

### 2.6 E6: the lit prototype

`litproto/lit.cfg.py.diff` (58 added lines against lit.cfg.py): parameter `msil_pipeline`, feature `msil-pipeline=<p>`,
`msil-ld`; `%msil_ld`, `%msil_ar`, `%{msil_ld_libs}`; `%{msil_prog}` with placeholders `%{msil_srcs}` (default `%s`)
and `%{msil_libs}` (default empty); `c_pipeline` gains the `lto` first step. Tests: `litproto/M2/` (two `.test`
files plus `.expected`). Measured (`msil_require_verify=1`, .NET 8, 8 workers, no native oracle): M2 2/2 pass;
M1 136/136 with `msil_pipeline=asm` in 311 s and with `msil_pipeline=lto` in 297 s (the `.ll` tests stay on llc in
the prototype) [V]. Points learned:
- lit's `REDEFINE:` requires the name to occur in exactly one existing pattern (`SubstDirective.adjust_substitutions`,
  `llvm/utils/lit/lit/TestRunner.py:1607-1648` [S]): placeholder names must not be substrings of other substitution
  names (`%{msil_libs}` vs `%{msil_ld_libs}` is fine).
- The composite `cd`s into a fresh per-level directory and compiles all sources with one `clang -c` (no `-o`),
  then links `*.o`; the internal shell expands the glob [V]. With the Phase-2 driver this becomes one
  `clang <srcs> -o x.dll`.
- `split-file` (already a `check-msil` dependency) keeps a multi-TU test in one file.

### 2.7 E7: a native Linux oracle without a compiler in WSL

`linux-oracle/start.c` (20 lines: `_start` aligns the stack and calls a C function that runs
`__init_array_start..__init_array_end`, then `exit(main(argc, argv))`), compiled by the shared clang for
`x86_64-unknown-linux-gnu`; each test linked by VS `ld.lld -dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed
start.o prog.o libm.so.6 libc.so.6` with the two libraries copied from the WSL distribution; run in WSL. All 93 C
programs of `msil/test/M1` reproduce their `.expected` (90/93 before `.init_array` was run: the three constructor
tests) [V]. Pitfall: Git Bash rewrites `/lib64/...` into a Windows path unless `MSYS_NO_PATHCONV=1` [V]. This gives
the WSL legs a native oracle (`msil_native_oracle` currently requires `msil_runner=local`, lit.cfg.py:846-847) and
lets update-expected.py write `<stem>.linux.expected` overrides from a real Linux run.

---

## 3. What Phase 2 adds, and how to test each part

Locations follow lld-flavor 5 and metadata-writer 6: FileCheck tests in `llvm/test/MC/MSIL`,
`llvm/test/tools/llvm-cil-dump` (or `llvm/test/Object/MSIL`), `lld/test/MSIL`, `clang/test/Driver/msil-toolchain.c`;
gtests in `llvm/unittests/CLI` and `llvm/unittests/Object`; execution tests in `msil/test/M2/` and
`msil/test/Programs/`.

### 3.1 Multiple translation units (P2-A objects, P2-B linking)

FileCheck (`lld/test/MSIL`, inputs from `llc -filetype=obj` or `clang -c`): symbol resolution order (objects, then
archives, then managed, then native: Design 5.2), duplicate and undefined diagnostics with their texts, `-Map`,
`--why-extract`, `--reproduce` listing, comdat/linkonce/weak selection, signature mismatch between a call and a
definition (wasm-ld's variant mechanism, lld-flavor P2B2.e).

Execution (`M2/00-link`): calls, function pointers and data relocations across TUs (prototype test); two TUs with a
`static` function of the same name (each `<Module>` private method must stay distinct); weak + strong definitions;
`inline`/`extern inline` and MSVC `??_C@` string literals in several TUs (linkonce merging, D26); tentative
definitions with `-fcommon`; constructors with priorities in different TUs (the run order must match native);
structs by value and `sret` across TUs; a K&R definition called from another TU without a prototype; the entry
point `main` in a non-first object; a TU whose only content is data. Each runs at O0/O2 against the native
transcript.

### 3.2 Archives (`M2/01-archive`)

Lazy extraction (prototype test), a member that only defines data, `--whole-archive`, `--start-lib/--end-lib`,
archives containing CIL objects and bitcode together (Design 5.1), two archives with same-named members (tinf/zlib
case), a cycle between two archives (does msil-ld rescan like lld's ELF or need groups? decide in P2-B), an archive
member that defines a libc name (`memset`) overriding a native import, and `-Bstatic`/`-Bdynamic` with an archive
and a native library of the same `-l` name. FileCheck: the archive symbol table with the tree's `llvm-ar`/`llvm-nm`
(they must understand CIL objects: Object/SymbolicFile support is part of P2-A whatever the format).

### 3.3 LTO (`M2/02-lto`)

Mixed bitcode and CIL objects; stock-clang bitcode (`--target=x86_64-pc-windows-msvc -fno-ms-volatile`, lld-flavor
3.3) plus MSIL objects; bitcode archives; `--lto-O0/1/2/3`; ThinLTO and `--lto-partitions` once LTO produces
objects (lld-flavor: ThinLTO needs the object path); the same program built non-LTO and LTO must give the same
transcript; O4 equivalence between `--lto-emit-asm` + ilasm and the LTO PE. A negative test: stock Linux bitcode
with `long double` struct members (x86_fp80) mixed with MSIL bitcode must be diagnosed or documented
(lld-flavor 3.3 found the layout mismatch).

### 3.4 `-shared` libraries and assembly-to-assembly linking (P2-C, `M2/04-shared`)

- Build `libz.dll` as a library assembly from the zlib archive (`-shared --export-class=Contoso.Zlib.Native`) and
  link the zlib driver against it; run with `msil-run.py --dep libz.dll` (R5) on every runtime.
- A C# consumer: a 20-line C# program compiled offline with csc against the library (the way build-jitall.py
  compiles jitall) calls `Contoso.Zlib.Native.adler32` directly: Design.md goal 5. Only the test harness uses C#.
- Exported data through accessors; the symbol-table custom attribute read back by a second link (FileCheck with
  `llvm-cil-dump --custom-attributes`); refusal to link a library built for the other triple (Design 5.4) as a
  FileCheck test; an internal function of the library must not be callable (no public method).
- Two libraries that both link a static archive (each gets its own copy and its own data segment).

### 3.5 Native P/Invoke resolution (`M2/03-native`, both triples)

| Case | Windows triple | Linux triple |
|---|---|---|
| libc function not in the D14 table (`strlen`, `abs`, `malloc`/`free`) | `pinvokeimpl("ucrtbase")` from the DLL or the UCRT import library | `pinvokeimpl("libc")` from `libc.so.6` (CoreCLR maps `libc`) |
| math function | `ucrtbase` | `libm.so.6` from its `DT_SONAME` (Status.md D14 row; E4's KISS FFT) |
| test-built native library via `-L%t -lnative` | `native.dll` (E5) | `libnative.so` cross-built (E5) |
| Win32 API | `kernel32` from `kernel32.lib` (Design 3.1) | n/a |
| undefined everywhere | link error naming the symbol | link error |
| `-Bstatic` with only a native library | link error | link error |

FileCheck (`lld/test/MSIL`): the `pinvokeimpl` module names in `--lto-emit-asm` output or in `llvm-cil-dump`
of the PE. Execution: each case runs; the native library travels with `--dep`. The Linux leg needs the sysroot
files (libc.so.6, libm.so.6) on the Windows host for msil-ld to read: the same copies E7 uses.

### 3.6 Reference binding (P2-B, bcl-binding recon)

Lit parameter `msil_references=none|net48|net8|net10|netstandard2.0|<path>` → `--reference` for msil-ld, feature
`msil-ref=<name>`. Paths on this host: `C:\Program Files (x86)\Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8`
(or the `microsoft.netframework.referenceassemblies.net48` NuGet package, which the Linux CI job can restore like
ilasm), `<dotnet>\packs\Microsoft.NETCore.App.Ref\<newest N.x>\ref\net<N>.0`.

| Leg | Runs on | Expectation |
|---|---|---|
| none (default) | .NET 8, .NET 10, Framework, WSL | as Phase 1 (built-in `[mscorlib]` table, bcl-binding 5.10) |
| net48 | same | O3-style check: `canon(--reference net48) == canon(no reference)` |
| net8 | .NET 8, .NET 10, WSL .NET 8/10 | runs; `assemblyref` rows `System.Runtime`, `System.Runtime.InteropServices`, `System.Threading` 8.0.0.0 |
| net8 on Framework | Framework | negative: `[unhandled exception: System.IO.FileNotFoundException]` (bcl-binding 0 item 3) |
| net10 on .NET 8 | .NET 8 | negative: the 10.0.0.0 reference does not load (CoreCLR requires version >= requested) [U] |

FileCheck (`lld/test/MSIL`): missing member / ambiguous type / two core libraries diagnostics on tiny hand-made
reference assemblies (bcl-binding-files/fakepack), and `llvm-cil-dump` of the AssemblyRef/TypeRef/MemberRef rows.

### 3.7 Deterministic output (`M2/06-det`)

A composite `%{msil_det}` builds the same program twice, in `%t/a` and `%t/b`, from relative paths with
`-fdebug-compilation-dir=.`, and `cmp`s every object, archive and the PE. Variations: LTO with
`--lto-partitions=4` / `--thinlto-jobs` (output must not depend on threads), input order kept the same (order
changes archive semantics). Unit test: the content-id vector (`SHA-256("abc")` → `bf1678ba-018f-4acf-8141-40de5dae2223`,
`0xA36103B0`, metadata-writer 3.10). Later (CI): the Linux job's PE of a corpus program must equal the Windows
job's for the same triple (both hosts produce the same bytes), by uploading the hash as an artifact.

### 3.8 The equivalence oracle

| Oracle | What is compared | Where it can be used | Status |
|---|---|---|---|
| O1 | transcript vs the native build | everything | exists |
| O2 | ildasm → ilasm → ildasm of msil-ld output (Design 5.5), listings equal, rerun | everything | exists for `-S` (`msil_roundtrip`); extend to `obj` |
| O3 | `canon(clang -c + msil-ld) == canon(clang -S + ilasm)` | single-TU programs: all of M1 | new |
| O4 | `canon(msil-ld LTO PE) == canon(ilasm(msil-ld --lto-emit-asm))` | multi-TU programs through LTO: M2, Programs | new; needs `--lto-emit-asm` kept after the PE writer exists |
| O5 | D21 verification and jitall of msil-ld output | everything | exists (unchanged policy) |
| O6 | PEVerify `/md` messages of msil-ld output ⊆ those of the ilasm build | single-TU programs | new (2.3) |

O3 allow-list (each entry is a normalization in the comparator, named and justified, like ilverify-allowed.txt):
- module name: both builds use the same output basename (no normalization needed);
- `hash=` of the Assembly row: whatever msil-ld chooses (Roslyn writes SHA1) vs ilasm's None;
- Param rows that ilasm synthesizes (`'A_0'`-style names, P/Invoke stub parameters) unless msil-ld writes them too;
- float32 constants with a signaling-NaN payload: ilasm quiets them, the direct writer must not (mc-streamer 0
  item 2; metadata-writer 0 item 8 shows the JIT quiets them anyway): compare with the quiet bit masked;
- encoding forms, only if the text printer and the encoder ever choose differently (`--forms=canon` maps `br.s`/`br`,
  `ldc.i4.s`/`ldc.i4`, `ldloc.s`/`ldloc` alike; mc-streamer R1/R4 and metadata-writer 0 item 7 recommend that they
  never differ, so the default is `--forms=exact`);
- AssemblyRef scopes only in the reference legs (`--scopes=strip`).
`.maxstack`, locals, EH clauses, data bytes, P/Invoke rows and every operand must match exactly.

O3 is the strongest single check of P2-A's encoder plus P2-B's writer: same codegen, two writers. It runs as part
of `%{msil_c}` and `%{msil_ll}` when `--param msil_equiv=1` (feature `msil-equiv`), so it covers all 132 composite
M1 tests at every level for the cost of two dumps per level.

### 3.9 Dumper tools

| Tool | Purpose | Notes |
|---|---|---|
| `llvm-cil-dump` (C++, metadata-writer 4.11/6) | FileCheck of PEs (headers, tables, signatures, IL with resolved operands, custom attributes, P/Invoke rows) in `check-msil`; needs no .NET | add `--canonical` (the mdcanon listing: sorted, token-free, branch labels, RVA bytes) so O3/O4 run without .NET start-up cost and on the Linux CI job without the C# tool |
| `mdcanon` (C#, prototype in `tests-p2-files/mdcanon/`) | O3/O4 comparator until `llvm-cil-dump --canonical` exists | build it offline like jitall (`build-jitall.py` generalized to build several tools); add a batch mode (one process, many files) |
| ildasm (10.0.12) | O2 round trip; second opinion for O3 | unsorted, layout noise (`I_<rva>` labels, `.imagebase`): never the primary comparator |
| object-format dumper | FileCheck of `clang -c` output | depends on P2-A's choice, see below |

Object-format dumper by candidate (objfmt-container recon decides the format):
- (A) wasm-style CIL container: llvm-readobj/obj2yaml/yaml2obj support for the new `Binary` kind
  (`MSILObjectFile`), modeled on `WasmObjectFile` + `WasmYAML`; llvm-nm and llvm-ar work through `SymbolicFile`.
  yaml2obj inputs make linker tests independent of codegen (lld/test/wasm does this).
- (B) assembly-shaped object (real metadata, placeholder rows, mc-streamer 5.8): `llvm-cil-dump` reads objects too,
  ildasm/ILVerify can open them (a cheap extra check: an object must ildasm cleanly); llvm-nm needs a `SymbolicFile`
  adapter for `llvm-ar`'s symbol table.
- (D) ELF/COFF relocatable with CIL in sections: llvm-readobj, llvm-objdump `-r`, llvm-nm and llvm-ar work as they
  are; `llvm-cil-dump --section` for the CIL payload.

### 3.10 Driver (`clang/test/Driver/msil-toolchain.c`)

`-###` checks of the msil-ld job: default libraries per triple (`-lc` and the UCRT/glibc search paths), positional
`.dll` inputs as references, `-Wl,--reference=` pass-through (bcl-binding 6.2), `-shared`, `-flto` (full and thin)
handing bitcode to msil-ld, no `runtimeconfig.json` or `.exe.config` written (Design 8: a test that lists the output
directory after `clang x.c -o x.dll`).

---

## 4. Harness design

### 4.1 lit parameters and features (additions)

| `--param` | Values | Feature(s) | Meaning |
|---|---|---|---|
| `msil_pipeline` | `asm` (default until the P2 exit gate), `lto`, `obj` | `msil-pipeline=<p>` | first steps of `%{msil_c}`/`%{msil_ll}`/`%{msil_prog}`: `-S`+ilasm, `-flto -c`+msil-ld, `-c`+msil-ld; `.ll` tests use `llc -filetype=obj` in `obj` and stay on llc text in `lto` |
| `msil_equiv` | 0/1 | `msil-equiv` | in `obj`: also build the `asm` way and compare canonical dumps (O3) and `/md` messages (O6) |
| `msil_ld`, `msil_ar`, `msil_cil_dump`, `msil_mdcanon` | paths | `msil-ld`, `msil-ar`, `msil-cil-dump`, `msil-mdcanon` | tools (defaults: `<build>/bin/...`; never the VS llvm-ar, finding 8) |
| `msil_references` | `none`, `net48`, `net8`, `net10`, `netstandard2.0`, path | `msil-ref=<name>` | `--reference` for every link (3.6) |
| `msil_linux_sysroot` | dir with `libc.so.6`, `libm.so.6` (copied from WSL by the config if absent) | `msil-linux-oracle`, `msil-linux-sysroot` | E7 oracle for WSL legs; msil-ld's native search for the Linux triple |
| `msil_corpus_dir` | dir | `msil-corpus` | enables `msil/test/Programs` (sources fetched by the manifest, R6) |
| `msil_require_link` | 0/1 | | like `msil_require_exec`: a gate leg fails instead of reporting M2 UNSUPPORTED |

Existing parameters keep their meaning; `msil_require_verify=1` additionally requires `mdcanon` or `llvm-cil-dump`
when `msil_equiv=1`.

### 4.2 Substitutions (additions)

| Substitution | Expands to |
|---|---|
| `%msil_ld` | msil-ld under msil-run.py `--tool` limits, plus `%{msil_ref_flags}` |
| `%{msil_ld_libs}` | the triple's default native search (`-L <System32> -lucrtbase` or the Linux sysroot + `-lc -lm`) until the driver supplies it |
| `%msil_ar`, `%msil_nm` | the tree's llvm-ar / llvm-nm |
| `%{msil_cc_c}` | `%clang_msil %{msil_cflags} -c` (+ `-flto` in `lto`) |
| `%{msil_srcs}`, `%{msil_libs}`, `%{msil_native_libs}` | per-test placeholders (`REDEFINE:`), defaults `%s`, empty, empty |
| `%{msil_prog}`, `%{msil_prog_O0}`..`_Oz` | multi-TU composite: fresh `%t.<L>.d`, compile `%{msil_srcs}`, link `*.o %{msil_libs}`, verify, jitall, run `--expect`, `%if` legs (native oracle with `%{msil_native_libs}`, netfx, roundtrip, equiv) |
| `%{msil_det}` | 3.7 |
| `%msil_canon` | the canonical dumper (`llvm-cil-dump --canonical` or mdcanon) |
| `%{msil_ref_flags}` | `--reference <dir>` per `msil_references` |
| `%msil_exec --dep F` | R5 |

### 4.3 Test layout and format

```text
msil/test/M2/lit.local.cfg       UNSUPPORTED without msil-exec and msil-ld (fatal under msil_require_link=1)
msil/test/M2/00-link/            multi-TU (3.1)
msil/test/M2/01-archive/         (3.2)
msil/test/M2/02-lto/             (3.3)
msil/test/M2/03-native/          (3.5)          REQUIRES per triple where needed
msil/test/M2/04-shared/          (3.4)          (P2-C)
msil/test/M2/05-refs/            (3.6)
msil/test/M2/06-det/             (3.7)
msil/test/M2/07-equiv/           explicit O3/O4 checks with an allow-list file, and comparator self-tests
msil/test/Programs/              exit-criteria drivers, ports, .expected (sources from msil_corpus_dir)
msil/test/harness/               new self-tests: --dep, mdcanon allow-list, %{msil_prog}, %{msil_det}
```

A test (from the prototype, `litproto/M2/00-link/two-tu.test`):

```text
# Two translation units: calls, a function pointer and data relocations across files.
# RUN: rm -rf %t && split-file %s %t
# REDEFINE: %{msil_srcs} = %t/main.c %t/util.c
# RUN: %{msil_prog}
#--- main.c
...
#--- util.c
...
```

`.expected` files stay generated: update-expected.py learns `%{msil_prog}` (run split-file into a temporary
directory, read the `REDEFINE:` values with `%t` replaced, build natively with `%{msil_native_libs}`), and its
`--target` for a Linux triple uses the E7 oracle under WSL.

### 4.4 Script changes

- **msil-run.py**: `--dep FILE` (repeatable) copied next to the program in the private directory (local, netfx and
  WSL runners; the WSL runner copies into its Linux temp directory); a `--dep-dir DIR` form for a library directory.
- **msil-verify.py**: pass dependencies to ILVerify (`-r`) so references between test assemblies resolve; add the
  differential `/md` check (O6) as `--md-baseline <ilasm-built.dll>`; `--roundtrip` unchanged.
- **msil-jitall.py / jitall.cs**: resolve dependencies from the program's directory (it already runs from a
  private copy; copy the deps there).
- **build-jitall.py** → build every C# harness tool (jitall, mdcanon, the C# consumer of 3.4) the same offline way.
- **lit-legs.ps1/.sh**: new legs (6.1); `build.ps1/.sh`: targets `lld` (or `msil-ld`), `llvm-ar`, `llvm-nm`,
  `llvm-cil-dump`, and the object dumper of the chosen format.

---

## 5. Exit-criteria programs

### 5.1 Recommendation

The Phase 2 exit gate should require the nine programs of 2.4 at -O0 and -O2, on both x86_64 triples, on every
runtime of the triple, built **as multi-TU programs with static archives** through the object path (and through
LTO as a second leg), each matching its native build, plus c-testsuite's 150 libc-free tests through the object
path. Program-specific checks: zlib's 12 round trips and checksums; CoreMark's built-in CRC validation (exit 0);
BLAKE2's keyed KATs for blake2b/s/bp/sp (0 mismatches of 4 × 256); Monocypher's self-checks plus digests;
TweetNaCl identical on both triples although `unsigned long` differs. P2-C adds two variants: zlib built as a
`-shared` library assembly used by the zlib driver and by a C# consumer, and KISS FFT with libm resolved from
`libm.so.6`'s export table on the Linux triple.

| Program | Licence | Source (pinned in this recon) | Harness tweak | Phase-2 features | Verdict |
|---|---|---|---|---|---|
| zlib 1.3.2 (Z_SOLO, 9 of 11 core files: `compress.c`/`uncompr.c` need default allocators and a hosted `NULL`) | zlib | github.com/madler/zlib tag v1.3.2, sha256 `b99a0b86...a495a1` | arena `zalloc`/`zfree` | 9-member archive, lazy extraction | required |
| tinf (2022-02-12, `57ffa1f1d5e3`) + zlib | zlib | github.com/jibsen/tinf | `-DNDEBUG`, assert shim | two archives, same-named members | required |
| Monocypher 4.0.2 (+ optional ed25519) | BSD-2-Clause or CC0-1.0 | release tarball, sha256 `38d07179...ae0879c` | none (own driver) | struct by value across TUs; needs the tree's llvm-ar | required |
| CoreMark (`1f483d5b8316`, 2025-05-01) | EEMBC acceptable-use agreement + Apache-2.0 (governs publishing scores; using it as a test is permitted [U]) | github.com/eembc/coremark | own `core_portme.{h,c}`, `-Dmain=coremark_main` for `core_main.c` only, `ee_printf` compiled out, `ITERATIONS=200` | 5-TU library + port, self-validation | required |
| LZ4 1.10.0 (`lz4.c` + `lz4hc.c`) | BSD-2-Clause (lib/) | tag v1.10.0, sha256 `53751290...75a2` | `LZ4_FREESTANDING`, `LZ4_USER_MEMORY_FUNCTIONS`, `LZ4_FORCE_SW_BITCOUNT`, static states | archive, memory callbacks defined by the driver | required |
| BLAKE2 reference (`ed1974ea8343`, 2023-02-12) | CC0, OpenSSL or Apache-2.0 | github.com/BLAKE2/BLAKE2 | KAT header read at build time | 6-member archive; `memset` from the support archive, address-taken | required |
| TweetNaCl 20140427 | public domain | tweetnacl.cr.yp.to, sha256 `02e65bc3...75c4` (.c) | deterministic `randombytes` TU | external callback TU, cross-triple data model | required |
| KISS FFT (`2a1f13371`, 2026-10-20) | BSD-3-Clause | github.com/mborgerding/kissfft | `-Dkiss_fft_scalar=double -ffp-contract=off`, malloc from the support archive | libm P/Invokes from an archive member | required |
| jsmn (`25647e692c79`, 2021-10-14) | MIT | github.com/zserge/jsmn | `JSMN_HEADER` split | 2 TUs that must agree on config macros | required (cheap) |
| c-testsuite (`5c7275656d75`) | harness MIT; tests from scc and tinycc, per-test licences [U] | github.com/c-testsuite/c-testsuite | none for the 150 libc-free tests | breadth through the object path | required (breadth) |
| Embench-IoT multi-file programs, TACLeBench multi-file kernels | GPL-3.0 mostly / mixed per benchmark | | board stubs, memcmp | multi-file self-verifying | later (licence review first) |
| SoftFloat 3e (353 files → archive), bzip2, Brotli decoder (122 KB dictionary), zstd decoder | BSD / bzip2 / MIT / BSD | | TestCorpus.md rows | big archives, big data images | Phase 2 stretch or Phase 3 |

### 5.2 Measured runs

| Run | Result | Wall, all nine sequentially |
|---|---|---|
| `x86_64-pc-windows-msvc-msil` -O2 | 9/9 equal to native on .NET 8, .NET 10 and Framework 4.8; verify and jitall clean | 125 s |
| `x86_64-pc-windows-msvc-msil` -O0 | 9/9 likewise; the `optnone` bodies survive LTO (jsmn 13 methods instead of 4, LZ4 1.35 MB of IL, msil-ld 7.6 s for LZ4) | 124 s |
| `x86_64-unknown-linux-gnu-msil` -O2 | 9/9 equal to the E7 native Linux build under WSL .NET 10; verify clean (jitall not run by the script on this triple) | 110 s |

The 125 s include compiling, archiving, linking, ilasm, verification, jitall, three runtimes and the native build
and run. Per program: MSIL compile 0.6-5.4 s (LZ4's `lz4hc.c` dominates), msil-ld 0.1-1.4 s (7.6 s for LZ4 at
-O0), verify 1.8-3.6 s (9 s for LZ4 at -O0; 14.5 s once for Monocypher under load), native build 1.3-9.7 s, each
run 0.4-1.4 s. The Linux runs used export-table stand-ins for `libc.so.6` and `libm.so.6` (`corpus/libc.c`,
`corpus/libm.c`, COFF DLLs named `libc.dll` and `libm.so.6.dll`) because the prototype msil-ld reads only COFF
DLLs; the P/Invoke module names came out as `libc` and `libm.so.6`, which is what Design 5.3 and D14 want.
Logs: `logs/corpus-results-{windows-O2,windows-O0,linux-O2}.json`. As lit tests (one test per program, two levels,
the runtimes of the leg) the set costs about 2 min of wall time per leg with 8 workers.

### 5.3 Sources: no vendoring

Do not vendor any of these sources (licences differ, sizes add up to about 21 MB unpacked, and the repository is
public). Proposal:
- `msil/utils/corpus/manifest.json`: per program the URL, the version or commit, the sha256 of the archive or
  the tree hash, and the licence name;
- `msil/utils/corpus/fetch.py --dir <cache>`: downloads (about 7 MB of archives and shallow clones, 21 MB unpacked)
  into a cache outside the repository (on this host under `C:\llvm-msil-scratch`, not on the nearly full `D:`),
  verifies hashes, works offline once filled; gates and CI pass `--param msil_corpus_dir=<cache>`; CI caches it
  keyed by the manifest's hash;
- `msil/test/Programs/<name>/`: our drivers, ports (CoreMark's `core_portme.*`), the support library and the shim
  headers (all written in this recon, `tests-p2-files/corpus/harness/`), `lit.local.cfg` with the source list and
  flags, and `.expected` files from update-expected.py;
- adding the download step to a gate or CI script needs a work package that owns it and the user's go-ahead
  (plan section 2 rule).

---

## 6. Gates

### 6.1 New legs

| Leg | Paths | Params | Estimate on this host |
|---|---|---|---|
| check-msil FileCheck (existing net8 leg grows) | + `llvm/test/MC/MSIL`, `llvm/test/tools/llvm-cil-dump`, `lld/test/MSIL` | | + 1-2 min |
| obj-net8 | M1, M2, Programs | `msil_pipeline=obj msil_equiv=1 msil_native_oracle=required msil_run_netfx=1` | 8-10 min (M1 obj ≈ M1 asm: 297 vs 311 s; + O3 dumps: about 1.5 min with a C++ dumper, 3 min with mdcanon) |
| obj-net10, obj-net8-tc0 | M1, M2, Programs | | 4-5 min each |
| obj-wsl-net10 (+ tc0) | M1, M2, Programs | `msil_runner=wsl msil_linux_sysroot=...` | 7-8 min each |
| lto-net8 | M2, Programs | `msil_pipeline=lto` | 3 min |
| refs-net8, refs-net48 | M1/11-programs, M2/05-refs | `msil_references=net8` / `net48` | 2 min each |
| det | M2/06-det, Programs (det variant) | | 2-3 min |
| ctest (c-testsuite) | Programs/c-testsuite | `msil_corpus_dir` | 2 min per level pair |
| more-levels-obj | M1 | `msil_pipeline=obj msil_opt_levels=O1,O3,Os,Oz` | 6-7 min (replaces asm more-levels once obj is default) |

### 6.2 Milestone and phase gates

- **Milestone gates** (P2-0 ... P2-C): build + unit tests + check-msil FileCheck + harness + the existing net8 leg +
  the milestone's own legs (for example obj-net8 restricted to M1/00-smoke..03-control while P2-A grows) + scope and
  format checks. About 20-30 min.
- **Phase exit gate**: every M11 leg (47 min today) plus the legs of 6.1 (about 40 min) = **about 85-90 min**. When
  `obj` becomes the default (end of P2-B), retire the duplicated asm legs (net10, net8-tc0, wsl-net10-tc0 and
  more-levels stay only for `obj`; net8-asm with the native oracle, roundtrip, modern-ilasm and wsl-ilasm stay as
  the asm oracle): **about 70-75 min**. Run it with `run_in_background` like run-m1-gate.ps1; a `run-p2-gate.ps1`
  (or `run-m1-gate.ps1 -Phase 2`) prints the same summary rows.
- **Both ilasms as oracles**: E3 shows that the Framework and .NET ilasms produce canonically equal assemblies
  (40/40), so O3 needs only one; use the pinned .NET ilasm 10.0.12, which also runs on the Linux CI job. The
  modern-ilasm and wsl-ilasm legs keep guarding the `-S` text.

### 6.3 CI

`msil-ci.yml` stays dispatch-only (user decision). Changes: build targets as in 4.4; the Linux job runs the obj
legs natively (its apt `clang` gives a real native oracle there) and restores the net48 reference package with
`msil-tools.proj` for the refs legs; the Windows job runs obj-net8 with netfx; both upload `lit-results/*.xml`;
the corpus cache (`actions/cache`, pinned like the other actions, check-action-pins.py) keyed by the manifest hash.
The runner build itself is unchanged in size: `lld` adds 27 TUs and about 30 s at -j4 (lld-flavor 5).

---

## 7. Phase-1 tests that change when the object path becomes the default

| Test | Count | Change |
|---|---|---|
| `clang/test/Driver/msil-toolchain.c` lines 114-190 | 21 `not %clang` RUN commands | pin `err_drv_msil_no_object_output` for `-c`, links, LTO links, `-save-temps=obj`, `-fno-integrated-as -c`, `-c -x assembler`; P2-B turns them into `-###` checks of the msil-ld job (`-c -x assembler` and `-save-temps -c` stay errors: no AsmParser, mc-streamer 7) |
| `llvm/test/CodeGen/MSIL/obj-unsupported.ll` | 5 RUN lines | `-filetype=obj` is an error today; replaced by P2-A's object tests |
| `clang/test/CodeGen/MSIL/cc1as-unsupported.s` | 4 RUN lines | stays (no assembly parser); P2-A must keep `-cc1as -filetype obj` a clean error once an `MCAsmBackend` exists (mc-streamer 0 item 5) |
| llc tests with D14 diagnostics ("not a Phase-1 import", "external global variable ... (phase 2 linker)") | 12 files, 58 matching lines | stay valid for `-S` if `-S` keeps D14 (recommended: `-S` text must assemble on its own); add `-filetype=obj` twins in which the same IR yields undefined symbols, and lld tests for the link errors |
| tests that check `[mscorlib]` / `.assembly extern mscorlib` | 27 files | stay (`-S` keeps the built-in table, bcl-binding 5.11) |
| tests with an ilasm round trip RUN line | 110 files | stay (Design 5.5) |
| `msil/test/M1` composite tests | 132 (93 C, 39 IR) | no edit: the pipeline parameter switches them; transcripts do not depend on the pipeline (136/136 under `lto`) |
| `msil/test/M1` explicit-RUN tests | 4 (`01-int/trap-call-before-div.ll`, `01-int/trap-call-before-rem.ll`, `03-control/trap.ll`, `01-int/calls-arith-debug-info.test`, which compiles `calls-arith.c` with `-g`) | add `obj` variants or mark `REQUIRES: msil-pipeline=asm` |
| `msil/test/harness` | 43 `.test` files (plus `legs/` and `pipeline/`) | unchanged (hand-written IL); new self-tests for R5 and the comparator |
| update-expected.py | | unchanged for M1; extended for `%{msil_prog}` |
| Status.md / README counts, GettingStarted.md section 5 (`-S` + ilasm as the only route) | | documentation milestone |

---

## 8. FeatureIdeas.md: what a Phase-2 test choice could preclude (flagged only)

- **Ideas 1 and 7 (class placement, several triples in one assembly):** the canonical dumper prints each member under
  its owning type, so placing functions in other classes or per-triple classes needs no comparator change. O3 assumes
  `-S` text and objects describe the same assembly structure; keep both from one MC event stream (mc-streamer R1),
  or O3 stops working when an idea changes only one of them.
- **Idea 6 (enclosed instance model):** transcripts and native oracles stay valid; the D12-specific allow-list
  entries (the RVA image `ldsflda`) would change. Nothing in this plan precludes it.
- **Idea 7:** the harness assumes one triple per lit run (`msil_triple`, `msil-triple-*` features, `<stem>.<os>.expected`).
  A multi-triple assembly would need a runner that executes one assembly on several hosts; nothing here blocks that.

## 9. Risks and open questions

1. **P2-A format undecided:** 3.9 lists the dumper for each candidate; if (A) is chosen, obj2yaml/yaml2obj support is
   a work package of its own (about the size of WasmYAML) and is what makes lld tests codegen-independent.
2. **`-S` and D14 in Phase 2:** recommended that `-S` keeps the Phase-1 import table and `-c` never diagnoses
   undefined symbols; needs a decision before the llc tests are split (section 7).
3. **O3 allow-list growth:** every entry hides a possible writer bug; require a comment and a self-test per entry
   (as ilverify-allowed.txt does).
4. **Linux native-library reading in tests:** msil-ld on Windows needs the Linux sysroot's `.so` files; the WSL copies
   of E7 are host-specific; CI's Linux job has them natively.
5. **Corpus downloads in gates:** a network dependency; the cache plus hash pinning makes it offline after the first
   fill, but the first fill and any manifest change need the user's OK (plan section 2).
6. **Host load:** timings here varied by up to 3x for single steps (verify 2.2-14.5 s) because other agents shared
   the host; gate estimates use medians.
7. **CoreMark licence** (EEMBC agreement): confirm that running it in CI without publishing scores is permitted [U].
8. **c-testsuite per-test licences** come from scc and tinycc [U]; run it from the fetched cache only.

## 10. Reproduction

```text
# E1  python scripts/time_steps.py <out> D:/llvm-msil/msil/test/M1/11-programs/*.c      (VS dev shell)
# E2  bash scripts/det.sh <empty dir>
# E3  python mdcanon/build.py; python mdcanon/compare_writers.py <E1 out>
#     dotnet exec --runtimeconfig bin/mdcanon.runtimeconfig.json bin/mdcanon.dll [--order=decl|--forms=canon|--scopes=strip|--stats] x.dll
# E4  python corpus/corpus.py [--triple windows|linux] [--level O0|O2] <out> [program ...]   (sources in <scratch>/e4-corpus/src)
#     python corpus/ctest.py
# E5  multiasm/: ilasm lib.il, prog.il; clang + lld-link / ld.lld for native.c (commands in 2.5)
# E6  copy msil/test and msil/utils to scratch, apply litproto/lit.cfg.py.diff, add litproto/M2, write a site config
#     pointing at D:/llvm-msil/build, then: llvm-lit --param msil_pipeline=lto --param msil_ld=<proto msil-ld> <scratch>/obj/test/M2
# E7  clang --target=x86_64-unknown-linux-gnu -ffreestanding -fno-pic -O2 -c linux-oracle/start.c
#     MSYS_NO_PATHCONV=1 ld.lld -o p.elf -dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed start.o p.o libm.so.6 libc.so.6
```

The prototype msil-ld is the lld-flavor recon's (`C:\llvm-msil-scratch\p2\lld-flavor\proto\bin\msil-ld.exe`, copied to
this recon's scratch `tools\`). Installs: none. Downloads: the corpus sources of 5.1 into scratch only. No tracked
file was modified; nothing was built in `D:\llvm-msil\build`.
