# MSIL backend: status of Phase 2

State after milestone M23 (2026-10-27, branch `msil`, with upstream llvm-project main at
`64f50ffd4f1c` merged): Phase 2 (milestones M12-M23) is complete. `clang -c` compiles C to CIL
objects, and `msil-ld`, an lld flavor, links them, archives of them, LLVM bitcode, native
libraries and library assemblies into IL-only .NET images that it writes itself, binding the
base class library against the reference assemblies it is given; `clang -S` plus `ilasm`, the
Phase 1 route, stays as a test oracle. The nine programs of the Phase 2 exit corpus, the
link-semantics matrix and every Phase 1 test pass through CIL objects on both x86_64 MSIL
triples and every runtime of each, the programs and the matrix through LTO too, and
c-testsuite's 150 libc-free tests pass through objects on .NET 8 (section 1, "Phase 2 exit
criteria"); what Phase 2 does not do, and what Phase 3 brings, is in section 3.

How it got there. Phase 1 (milestones M0-M10) is the "first milestone" of
[Design.md](Design.md) section 12: one C file compiled with `clang -S` to ILAsm text,
assembled with `ilasm`, run on .NET. M11 followed up the first upstream merge. M12, the first
milestone of Phase 2, fixed the backend items every Phase 2 path needs (aliases, weak
references, empty `asm` statements with operands, inline `__int128` division, shared frame slots
for structs passed by value) and gave the WSL legs a native Linux oracle and a private .NET
runtime. M13 landed the shared plumbing of Phase 2 without changing any IL: WebAssembly's import
attributes and `#pragma comment(lib)` on MSIL triples, the driver's new error text for object
files, the lld flavor `msil` with a stub `msil-ld`, linker options in the bitcode symbol table
of MSIL modules, and the first headers of the ECMA-335 library and of CIL objects. M14 made
programs of several files work: `clang -c` wrote LLVM bitcode (since M19 `clang -flto -c`
does), clang has an MSIL toolchain
that links with `msil-ld`, and `msil-ld` links bitcode files and archives by lld's rules, with
LTO, into the ILAsm text of the whole program, which `ilasm` assembles (section 2, "Programs of
several files"). M15 made `msil-ld` bind every function that no file of the program defines to
the native libraries that a native link of the same program reads (import libraries, DLLs, ELF
shared objects with symbol versions, GNU ld scripts), so linked programs call the C library, the
Win32 API and native libraries of their own through P/Invoke, and every reference that cannot
bind is a named link error (section 2, "Native libraries and P/Invokes"); `clang -S` and `llc`
keep the frozen standalone import table of D14. M16 added the writer half of the ECMA-335
library in `llvm/lib/CLI`, which builds metadata, IL and method bodies, and two C# tools that
check it from outside, among them `mdcanon`, the canonical listing that later milestones compare
assemblies with (section 2, "The ECMA-335 library and the metadata oracles"). M17 completed the
library for Phase 2: a PE writer with the envelope of the C# compiler's images (executables,
libraries and the netmodules that CIL objects will be), a reader of PE images and metadata, the
codecs of the link-info record that CIL objects will carry, and a dumper, with two tools:
`llvm-cil-dump`, whose canonical listing is now the one the tests compare (`%msil_canon`), and
`llvm-cil-copy`, which rewrites an ilasm image through the reader and the writer, so the first
images written without ilasm verify and run on .NET 8, .NET 10, .NET Framework 4.8 and WSL;
M16 and M17 changed no IL. M18 made the backend emit binary CIL: every instruction it emits is
one MC instruction of one CIL opcode, which the ILAsm printer prints and a table-driven encoder
encodes, byte for byte as ilasm does on every method of the test corpus, so the text and the
CIL objects of M19 come from one stream of instructions. The text changed in two ways only:
the shortest instruction forms, short branches included (plan Q26), and `nomangle` in every
P/Invoke (section 2, "Binary CIL"). M19 made `clang -c` (clang-cl `/c`) and `llc
-filetype=obj` write CIL objects: ECMA-335 netmodules that hold the backend's binary CIL, a
link-info record that says what the symbol table, sections and relocations of a native object
say, and a COFF symbol table that `llvm-nm`, `llvm-ar` and GNU `nm` and `ar` read (section 2,
"CIL objects"). M20 made `msil-ld` link CIL objects and write the assembly image itself: the
objects of `clang -c`, or a program compiled and linked by one clang command, become an IL-only
PE32+ executable that runs on .NET 8, .NET 10, .NET Framework 4.8 and WSL with no ilasm. LTO
writes CIL objects too, so ThinLTO and LTO partitions work; a call whose signature differs from
its definition's gets an adapter and a warning, or a named error; named sections,
`__start_`/`__stop_` symbols and constructor tables are laid out as a native link lays them
out, in `clang -S` and llc text as well; `--gc-sections` is the default; and variables of native
libraries are imported (section 2, "CIL object links and the image"). M20's commit freezes
version 1.0 of the link-info record. M21 made `msil-ld` bind every type and member of the .NET
base class library that its output uses by searching reference assemblies, as the C# compiler
does: given the .NET Framework 4.8 reference assemblies, an image references `mscorlib` as
before; given the .NET 8 or .NET 10 reference pack, it references `System.Runtime`,
`System.Runtime.InteropServices` and `System.Threading` of that version and runs on that .NET
and later, but not on the .NET Framework. A link without references binds through a built-in
`[mscorlib]` table and writes exactly the image M20 wrote (section 2, "Reference assemblies and
the base class library"). M22 made `msil-ld -shared` (clang `-shared`, clang-cl `/LD` and
`/LDd`) link library assemblies: DLL images without an entry point whose exported functions
are public static methods of an export class, with an address accessor per export and a
property per variable, a symbol table in a custom attribute, and data initialized in two
phases, so libraries whose data point into each other, in a cycle too, see complete data.
Programs and other libraries link against them with the CLR's semantics (no interposition,
every symbol resolved, duplicates warned), and C# programs call them (section 2, "Libraries
and assembly-to-assembly linking"). M22 also fixed four defects that an experiment with Rust
bitcode found (section 2, "Rust bitcode"). M23 closed Phase 2: CIL objects became the default
pipeline of the tests, with `-S` and ilasm in the `asm-oracle` leg; the exit corpus comes from a
pinned manifest into a cache outside the repository, never from the repository itself; the
link-semantics matrix is complete and listed line by line in `matrix.txt`; one verified image
runs on several runtimes (`msil_also_run`); and determinism is checked within each host and,
for objects and archives, across the two CI hosts. Its fix loop made `msil-ld` warn about a
`.exe` that only .NET can start and closed four follow-ups of M22 (section 2, "The Phase 2 exit
(M23)"). How to build and use it is in
[GettingStarted.md](GettingStarted.md); the decisions referred to as D1-D38 are in section 3 of
[ImplementationPlan.md](ImplementationPlan.md), and its section 12 records every later
amendment.

## 1. Phase 2 exit criteria

Plan Q29 defines what closes Phase 2, and M23 checks it. The phase gate, `run-m1-gate.ps1
-CorpusCache <cache>` (plan M23 gate step 16), runs every leg named below; the results quoted
here were measured on the development host while M23 was built, through private lit roots on
Windows and in WSL, and the gate repeated them. Its last run, on M23's final tree (2026-10-28,
275 minutes), passed every row but `wsl-net10`, whose one failure, a PEVerify timeout of
`M1/09-structs/struct-byval.c` under host load, passed when re-run alone with the leg's
parameters, as plan section 12's row 'M23 gate step 16 amended (a tool timeout under host
load)' allows. How to run the legs, the corpus and the gate is
in GettingStarted.md section 7.

| Q29 item | How Phase 2 meets it |
|---|---|
| Nine programs of several files (zlib `Z_SOLO`, tinf with zlib, Monocypher, CoreMark, LZ4, BLAKE2, TweetNaCl, KISS FFT, jsmn), built with static archives, at `-O0` and `-O2` on both triples and every runtime | `msil/test/Programs/<name>/<name>.test` builds each program with `%{msil_prog}` from the sources of the pinned exit corpus (below) and the support archive, and its transcript must equal the native build's at `-O0` and `-O2`: through CIL objects (legs `programs` and `programs-wsl`) and through LTO (`programs-lto` and `programs-lto-wsl`, both with the equivalence oracle O4), on `x86_64-pc-windows-msvc-msil` (.NET 8, .NET 10 through `msil_also_run`, .NET Framework 4.8; the Windows native oracle) and on `x86_64-unknown-linux-gnu-msil` (WSL .NET 10 and, through `msil_also_run`, the private WSL .NET 8; the E7 oracle). Every image is verified (D21) and force-JIT-compiled on each runtime. All nine transcripts end `[exit 0]` (each driver returns its error count, clamped to 255, except jsmn's, which has no self-check and returns 0), and each program has one `.expected` file for both oracles, TweetNaCl included. `msil/test/Programs/det/<name>.test` builds each program twice from two directories and compares the objects, archives and image byte for byte (legs `det` and `det-wsl`) |
| c-testsuite's 150 libc-free tests | `msil/test/Programs/c-testsuite/`: one lit test per name of `libc-free.txt`, the 150 of the pinned tree's 220 tests whose source calls no C library function by the recon's rule (recomputed from the cache on every lit run), built through CIL objects at `-O0` and `-O2` against the native build on `x86_64-pc-windows-msvc-msil` and .NET 8 (leg `ctest`; no leg runs them for `x86_64-unknown-linux-gnu-msil`, section 3): 149 exit 0 with no output, as natively; `00140`, whose variadic definition is a Phase 3 construct (P3-B), is the one `diag` entry and pins `variadic function definitions are not supported for MSIL yet` (LTO deletes the unused definition at `-O2`, objects keep it) |
| The link-semantics matrix: the zoo and the probes p1-p9 against native transcripts | the 55 lines of `msil/test/M2/08-semantics/matrix.txt` (below): one per row and variant of plan 5.9.16, with the rows that live in `00-link/` and `llvm/test/CodeGen/MSIL` and the P3 rows, and one per other test of `08-semantics` (`narrow-arg-mismatch`, `narrow-result-mismatch`, `section-bracket` and the two variants of `initarray-args`); `matrix-coverage.test` checks the list against the tests (`Inputs/check-matrix.py`). Every case prints its native transcript or stops with its named diagnostic on the pipelines and triples its line lists (objects and LTO for all but `section-bracket`, objects only, and `p7-flags`, an object check) and on five runtimes; none ends in a JIT-time exception, none is XFAIL, and no row is `untested` |
| Phase 1's tests through the object pipeline | objects are the default test pipeline since M23 (plan Q31): every test of `msil/test/M1` compiles with `clang -c` (an IR test with `llc -filetype=obj`) and runs `msil-ld`'s image, in `net8` (.NET 8, and through `msil_also_run` .NET 10 and .NET 8 with `DOTNET_TieredCompilation=0`; .NET Framework 4.8; the native oracle; the equivalence oracles O3 and O6), `more-levels` (`-O1`, `-O3`, `-Os`, `-Oz`) and `wsl-net10` (WSL .NET 10, and through `msil_also_run` WSL .NET 8 and .NET 10 with `DOTNET_TieredCompilation=0`); `asm-oracle`, `modern-ilasm` and `wsl-ilasm` keep `-S` and ilasm, and `roundtrip` takes `msil-ld`'s images through ildasm and ilasm (O2) |

### The exit programs

The sources are never part of the repository. `msil/utils/corpus/manifest.json` pins each
entry by its full commit (a GitHub archive at that commit) or by the SHA-256 of its release
files, and by the SHA-256 of its extracted tree; `python msil/utils/corpus/fetch.py` downloads
them into a cache outside the repository (`C:\llvm-msil-scratch\corpus-cache` on the
development host), and lit reads the cache (TestCorpus.md, "Phase 2 exit corpus", has the
versions and licences). Every program links its library as a static archive and a support
archive, `msil/test/Programs/support/` (an arena allocator and `memcmp`, `memset`, `memcpy`
and `strlen`, defined only for the MSIL build, which links no C library statically, and output
through `puts`), and both builds compile with the shim headers of
`msil/test/Programs/shim/` instead of a C library's.

| Program | Archives | What the driver checks |
|---|---|---|
| zlib 1.3.2 (`Z_SOLO`) | `libz.a`: the nine core files | 12 round trips: one 100,000-byte input deflated at levels 1, 6 and 9 with four strategies, each stream inflated and compared; Adler-32, CRC-32 and each stream's size and hash |
| tinf (`57ffa1f1d5e3`) with zlib | `libz.a` and `libtinf.a`, whose members `adler32.o` and `crc32.o` share names | zlib compresses, tinf decompresses the raw and the zlib-wrapped stream; both libraries' Adler-32 and CRC-32 agree; the link extracts each archive's members for its own names only |
| Monocypher 4.0.2 with its optional Ed25519 | `libmonocypher.a` | digests and self-checks of every primitive family: BLAKE2b (keyed too), SHA-512 and HMAC-SHA-512, ChaCha20 and XChaCha20, Poly1305, AEAD lock and unlock, X25519 key agreement, EdDSA and Ed25519 signatures (a changed message must fail), Argon2id (whose configuration and inputs cross objects as structs by value) |
| CoreMark (`1f483d5b8316`) | `libcoremark.a`: the five CoreMark sources (`main` renamed `coremark_main`); the port `core_portme.c` and the driver `coremark_driver.c` are the program's own objects | the performance run's seeds, 200 iterations: CoreMark's own validation of its list, matrix and state CRCs, which the port records at `portable_fini` (CoreMark's `main` always returns 0); then a run with other seeds of the same seed CRC (0x4003, 1, 0x66), which CoreMark must count as mismatches (3 in the transcript), or the driver exits 255 |
| LZ4 1.10.0 | `liblz4.a`: `lz4.c` and `lz4hc.c`, freestanding | block compression at four accelerations and HC at four levels, each stream decompressed and compared; `memcpy`, `memset` and `memmove` are the compiler's builtins (`cpblk`, `initblk`, `Buffer.MemoryCopy`) |
| BLAKE2 (`ed1974ea8343`) | `libb2.a`: the six reference sources | 4 x 256 keyed known-answer tests of `blake2b`, `blake2s`, `blake2bp` and `blake2sp`, with 0 mismatches each |
| TweetNaCl 20140427 | `libnacl.a`, whose `tweetnacl.c` calls back the driver's `randombytes` in another object | box and open, sign and check (and reject a forged message), hash, authenticate and scalar multiplication; its `u32` is `unsigned long`, 32 bits on windows-msvc and 64 on linux-gnu, and the transcript is the same on both triples |
| KISS FFT (`2a1f1337160e`), `double` | `libkiss.a` | complex FFTs of four mixed-radix sizes, forward and inverse, and real FFTs of the even ones; `cos`, `sin`, `floor` and `sqrt` bind to the C math library, `malloc` to the support archive; `-ffp-contract=off` keeps fused multiply-adds out of the native build |
| jsmn (`25647e692c79`) | the support archive only; the parser comes from another object of the program | five documents parsed (one unterminated) with `JSMN_PARENT_LINKS` and `JSMN_STRICT` in both objects, token counts and a hash of the tokens |

Measured while M23 was built: all nine programs and their `det` tests passed (18 of 18) through
objects and through LTO with O4, on Windows (.NET 8, .NET 10 through `msil_also_run`, .NET
Framework 4.8) and in WSL (.NET 10, and .NET 8 through `msil_also_run`), and the Windows and E7
oracles gave the same transcripts at `-O0` and `-O2`, so no program has a `.linux.expected`
file. One program at both levels took 52 s (CoreMark) to 114 s (LZ4) on one worker with every
runtime of the Windows leg; with 8 workers the nine programs and their `det` tests took about 4
minutes per leg on Windows and 3.5 (objects) to 6 (LTO, and about 3 more with O4) in WSL. The
cache holds 19 MB, of which `fetch.py` downloaded 4.4 MB in 16.5 s; `fetch.py --check` takes
about 2 s.

### c-testsuite

`msil/test/Programs/c-testsuite/libc-free.txt` lists the 150 tests by name, under a header with
the rule (the recon's regular expression of C library calls) and the count, and `lit.local.cfg`
recomputes the list from the cached tree and stops on any difference, or on a listed test whose
`.c.expected` file is not empty. Each name is one lit test of the directory's own test format
(`msil_ctest_format.py`): `%{msil_prog}` of the one source with `%{msil_cflags}` redefined to
the recon's `-std=gnu17 -w` (no `-ffreestanding`, because several tests rely on `main`'s
implicit `return 0`) and `exit0.expected` (`[exit 0]`) as the expected file, against the native
build. A `diag` entry is allowed only for a construct that Phase 3 implements (a variadic
definition, P3-B; dynamic `alloca`, P3-A); it builds with `%{msil_prog_fail_<L>}` at each level
and checks the text with FileCheck. There is one:

| Test | Phase 2 | Phase 3 item |
|---|---|---|
| `00140` | `variadic function definitions are not supported for MSIL yet` (through objects; under LTO at `-O2` the unused variadic function is deleted before code generation, so the entry also requires the object pipeline) | P3-B variadic definitions (D19) |

The other 149 passed on their first run, at `-O0` and `-O2` on Windows .NET 8, and a sample
(`00001`-`00009` and `00140`) in WSL. With 8 workers the directory took about 18 minutes on the
loaded development host.

### The link-semantics matrix

`msil/test/M2/08-semantics/matrix.txt` lists every row of plan 5.9.16 and each of its variants,
with its tests, its native result, its pipelines, its triples, its Phase 2 result and the
milestones that built it; `matrix-coverage.test` runs `Inputs/check-matrix.py`, which requires
every listed file to exist and every `.test` of the directory to be listed, a `diag:` result for
every `UB` or `link error` native column, a transcript case to run `%{msil_prog}` on both triples
unless its row names one triple and the test requires it or guards its composite with `%if msil-triple-<os>` (`initarray-args`), a FileCheck of the diagnostic for each
listed pipeline with `--implicit-check-not` for `LLVM ERROR` and `PLEASE submit`, `obj` and `lto`
for every row of M20, and a pinned diagnostic for every P3 row. The table is that file:
"the native transcript" means the program prints what its native build prints, at `-O0`, `-O2`
and every level of the leg, on .NET 8, .NET 10 and .NET Framework 4.8 for windows-msvc and WSL
.NET 10 and .NET 8 for linux-gnu; a diagnostic is the named error with exit status 1 at `-O0` and
`-O2`. `(P3)` marks a row whose transcript comes with Phase 3; its test pins today's diagnostic.
What each case links is in section 2, "The link-semantics matrix".

| Case | Native | Phase 2, on each listed pipeline | Pipelines | Triples |
|---|---|---|---|---|
| `zoo` | 63 | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `zoo-archive` | 63 | the native transcript; the weak undefined `maybe` does not extract `c.o` | obj, lto | linux-gnu, windows-msvc |
| `zoo-defined` | 223 | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `p1-weak-alias` | 144 | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `p1-weak-alias/reversed` | 144 | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `p1-weak-alias/kept` | 114 | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `p1-weak-alias/main` | 144 | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `p1-weak-alias/main-kept` | 114 | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `p2-knr` | 23 | the native transcript; the object link adapts with a warning | obj, lto | linux-gnu, windows-msvc |
| `p3-start-stop/linux` | 15 | the native transcript | obj, lto | linux-gnu |
| `p3-start-stop/windows` | link error (COFF linkers define no `__start_`/`__stop_`) | `undefined symbol: __start_myreg` | obj, lto | windows-msvc |
| `p4-selectany` | 15 | the native transcript | obj, lto | windows-msvc |
| `p5-tls` (P3) | 12 | `thread-local variable @tcount is not supported for MSIL yet` | obj, lto | linux-gnu, windows-msvc |
| `p6-ctors` | BCDM | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `p6-dtors` (P3) | BCDM, then the destructors at exit (the E7 oracle runs no `.fini_array`; Windows faults at exit) | `global destructor @d150 is not supported for MSIL yet` | obj, lto | linux-gnu, windows-msvc |
| `p7-flags` | n/a (object content) | object content: SIGNATURES, DLLIMPORT (windows-msvc), LINKER_OPTIONS (plan 5.8.8) | obj | linux-gnu, windows-msvc |
| `p8-section-ctor` | 42 | the native transcript | asm, obj, lto | linux-gnu, windows-msvc |
| `p9-weak-common` | 1 | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `p9-weak-common/reversed` | 1 | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `knr-two-arities` | 3 | the native transcript; the object has two MemberRefs with `NO_PROTOTYPE` and the missing-argument adapter with its warning | obj, lto | linux-gnu, windows-msvc |
| `sig-mismatch` | 41 | the native transcript; adapter and warning | obj, lto | linux-gnu, windows-msvc |
| `weak-def-sig-mismatch` | the strong definition's result | the native transcript; adapter and warning | obj, lto | linux-gnu, windows-msvc |
| `weak-def-sig-mismatch/double` | UB (an integer argument read as a double) | `function signature mismatch: f` | obj, lto | linux-gnu, windows-msvc |
| `int-ptr-adapter` | transcript | the native transcript; adapter and warning | obj, lto | linux-gnu, windows-msvc |
| `missing-arg-adapter` | transcript | the native transcript; adapter and warning | obj, lto | linux-gnu, windows-msvc |
| `float-int-mismatch` | UB (the callee reads a register the caller never set) | `function signature mismatch: half_of` | obj, lto | linux-gnu, windows-msvc |
| `narrow-arg-mismatch` | 200 and -56 | the native transcript; both pipelines in every leg, with their warnings | obj, lto | linux-gnu, windows-msvc |
| `narrow-result-mismatch` | 200 and 50000 | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `ctor-slot-order` | the slot's letter, then the constructor's (non-LTO native) | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `common-largest` | transcript | the native transcript; the largest common wins | obj, lto | linux-gnu, windows-msvc |
| `unaligned-reloc` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `gc-used-retain` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `section-bracket` | 82 (Linux), 235 (Windows) | the native transcript | obj | linux-gnu, windows-msvc |
| `initarray-args/linux` | glibc passes `argc`, `argv` and `envp` | the native transcript | obj, lto | linux-gnu |
| `initarray-args/windows` | UB (MSVC's CRT calls `.CRT$XC*` entries without arguments) | `global constructor '<c>' takes arguments, which MSIL does not pass` | obj, lto | windows-msvc |
| `type-mismatch` | UB (a function used as data) | `symbol type mismatch: value` | obj, lto | linux-gnu, windows-msvc |
| `duplicate-strong` | link error (duplicate symbol) | `duplicate symbol: shared` | obj, lto | linux-gnu, windows-msvc |
| `undefined` | link error (undefined symbol) | `undefined symbol: missing_fn` | obj, lto | linux-gnu, windows-msvc |
| `weak-undef-guarded` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `import-attrs` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `pragma-comment-lib` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `native-callback` (P3) | the sorted array | `native function 'qsort' (<module>) takes a C function pointer; passing functions to native code needs phase 3 thunks` | obj, lto | linux-gnu, windows-msvc |
| `static-same-name` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `inline-comdat` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `byval-sret-xtu` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `knr-def-xtu` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `main-not-first` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `data-only-tu` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `p2c-bsymbolic` | `-Bsymbolic` native libraries | the native transcript; each assembly calls its own definition, with `msil-ld`'s warning | obj, lto | linux-gnu, windows-msvc |
| `p2c-cycle` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `p2c-weak-def` | transcript | the native transcript | obj, lto | linux-gnu, windows-msvc |
| `fini-array` (P3) | `main`, then the `.fini_array` entry at exit (glibc) | `global destructors are not supported yet: p_fini in <file>` | obj, lto | linux-gnu |
| `crt-xt` (P3) | `main`, then the `.CRT$XT` entry at exit (MSVC CRT) | `global destructors are not supported yet: p_term in <file>` | obj, lto | windows-msvc |
| `atexit-order` (P3) | the handlers in the reverse order of registration | `undefined symbol: atexit` | obj, lto | linux-gnu, windows-msvc |
| `variadic-def-xtu` (P3) | 42 | `variadic function definitions are not supported for MSIL yet` | obj, lto | linux-gnu, windows-msvc |

The P3 rows cover everything plan 5.9.16 leaves to Phase 3: thread-local data (`p5-tls`),
destructors (`p6-dtors`, and the destructor tables `.fini_array` and `.CRT$XT*` of `fini-array`
and `crt-xt`), `atexit` (`atexit-order`: `atexit` binds to no native library, because glibc
defines it only in `libc_nonshared.a`, which the tests' E7 sysroot does not have, and the UCRT
does not export it, so the link stops with `undefined symbol: atexit` and its `>>> referenced
by` line on both triples), function
pointers passed to native code (`native-callback`) and a variadic definition called from
another object (`variadic-def-xtu`: `clang -c` reports the definition under `obj`, and a caller
alone gets `calls to variadic functions are not supported for MSIL yet`; `msil-ld`'s LTO reports
the definition under `lto`). The three cases whose native build has undefined behaviour,
`weak-def-sig-mismatch/double`, `float-int-mismatch` and `type-mismatch`, and the Windows half of
`initarray-args` stop with their named diagnostics on both pipelines. Measured while M23 was
built, through private lit roots, of the 53 tests (the 52 of `msil/test/M2` that `matrix.txt`
names, and `matrix-coverage.test`): on Windows (.NET 8, .NET 10 through `msil_also_run`, .NET
Framework 4.8) 51 passed through objects and 50 through LTO, in WSL (.NET 10, and .NET 8
through `msil_also_run`) 50 and 49; the rest were the cases of the other triple, UNSUPPORTED
there, and `section-bracket`, which runs through objects only. The one other test that
`matrix.txt` names, `llvm/test/CodeGen/MSIL/object-p7-flags.ll`, checks object content and runs
in `net8`.

### Design.md section 11, Phase 2: what was built and what proves it

| Phase 2 outcome (Design.md 11) | As built | Proven by (leg or tests) |
|---|---|---|
| CIL objects as ECMA-335 modules with a link-info record | `clang -c` (clang-cl `/c`) and `llc -filetype=obj` write netmodules with link-info record v1 (frozen at M20's commit) in a module-level custom attribute and a COFF symbol view for `nm` and `ar` (M19) | `obj-text` (each object of `msil/test/M1` lists like ilasm's object of its object-form text, is deterministic and agrees with its record), `obj-text-wsl` (GNU `nm` and `ar`), the 12 `object-*.ll` tests and the matrix row `p7-flags` in `net8` |
| `msil-ld` | lld's `msil` flavor (M13) with lld/ELF's resolution rules (M14), linking objects, archives, bitcode and library assemblies | the 111 tests of `lld/test/MSIL` in `net8` (and with `LLD_IN_TEST=2` in the gates), the matrix in every leg that runs `msil/test/M2` |
| Direct PE writer | `msil-ld` writes the IL-only PE32+ image through `llvm/lib/CLI` (M17, M20); ilasm is a test oracle only | every lit leg under the default `obj` pipeline; O3 and O6 in `net8`, O4 in `lto`, `programs-lto` and `programs-lto-wsl` (not in `lto-wsl`), the ildasm and ilasm round trip O2 in `roundtrip`; `cli-corpus-check.py --mode copy` |
| Multiple TUs | programs of several files through bitcode (M14) and objects (M20) | `msil/test/M2/00-link`, the matrix, the nine exit programs (`programs`, `programs-wsl`) |
| Archives | lazy extraction by lld's rules, `--whole-archive`, `--start-lib` (M14) | `msil/test/M2/01-archive`, `zoo-archive`, and every exit program (each links static archives; tinf's two archives share member names) |
| Bitcode and LTO | `-flto` bitcode with LTO (M14); ThinLTO, partitions and mixed links (M20) | `lto`, `lto-wsl`, `programs-lto`, `programs-lto-wsl`, `msil/test/M2/02-lto` |
| `-shared` exports | library assemblies with an export class, accessors and properties, a symbol table, two-phase initialization (M22) | `msil/test/M2/04-shared` (also bound to the .NET 8 pack and run on .NET 10 in `refs-net8`), the 15 `shared-*.test` of `lld/test/MSIL` |
| Assembly-to-assembly linking | programs and libraries link against libraries (late binding to exports, cross-assembly relocations, cycles through interface libraries), and C# programs call them (M22) | `msil/test/M2/04-shared` (a C# consumer compiled by csc, required in `net8`), the `p2c-*` rows of the matrix |
| P/Invoke resolution from native export tables | `msil-ld` binds undefined functions to the native libraries of the link (ELF shared objects and scripts, import libraries, DLLs) with the library names a native link records (M15), and imports native variables (M20) | `msil/test/M2/03-native`, the matrix rows `pragma-comment-lib` and `import-attrs`, the 23 `native-*.test` of `lld/test/MSIL`, KISS FFT's C math calls |
| BCL references bound from reference assemblies | `msil-ld --reference` binds every base class library member the output needs by searching reference assemblies; without them, a built-in `[mscorlib]` table (M21) | `refs-net48`, `refs-net8`, `refs-net10`, `msil/test/M2/05-refs` (`mscorlib-safe` on every runtime), the 17 `bcl-*.test` of `lld/test/MSIL` |

### Tests in the tree at the Phase 2 exit

| Suite | Tests |
|---|---|
| `llvm/test/CodeGen/MSIL` | 139: 131 llc tests (`.ll`), 7 MIR tests and `irsymtab-linker-options.ll` |
| `llvm/test/MC/MSIL` | 9 |
| `clang/test/CodeGen/MSIL`, the driver tests | 22, and `clang/test/Driver/msil-toolchain.c`, `msil-link.c`, `msil-lto.c`, `msil-header-search.c`, `msil-object.c`, `msil-shared.c` and `clang/test/Preprocessor/init-msil.c` |
| `lld/test/MSIL` | 111: 28 `obj-`, 23 `native-`, 17 `bcl-`, 15 `shared-` and 28 others |
| `llvm/test/tools/llvm-cil-dump`, `llvm-cil-copy` | 14 |
| `msil/test/M1` | 143: 99 C programs, 43 IR files and one test with `-g` |
| `msil/test/M2` | 127: `00-link` 12, `01-archive` 5, `02-lto` 9, `03-native` 15, `04-shared` 14, `05-refs` 12, `06-det` 3, `07-equiv` 5, `08-semantics` 47, `09-hosted` 5 |
| `msil/test/Programs` | 169: the nine programs, their nine `det` tests, `det/check-recipe.test` and the 150 tests of c-testsuite |
| `msil/test/harness` | 112: `prog/` 16, `legs/` 13, `pipeline/` 3, `pipeline-obj/` 5, `equiv/` 11 and 64 at the top level |
| Unit tests | `MSILTests`, the MSIL cases of `TargetParserTests` and `MCTests`, and `CLITests` (21 suites and 426 tests, of which the gates require at least 210, and at least 9 of `CLISymbolTableTest`) |

### The phase gate, CI and determinism

From M23 the legs of `msil/utils/ci/lit-legs.ps1` and `lit-legs.sh` are the set of plan 5.12.11
(GettingStarted.md section 7 lists them, and the old names with the legs that replaced them):
`net8` runs every MSIL FileCheck directory and `msil/test` through objects with `msil_also_run`,
the old `obj`, `obj-wsl`, `net10`, `net8-tc0` and `wsl-net10-tc0` legs are folded into `net8` and
`wsl-net10`, and `programs`, `programs-lto` and `det` (with their WSL twins) and `ctest` (Windows only) read the
exit corpus. `run-m1-gate.ps1` adds two rows of its own: `corpus` (`fetch.py --check` of all ten
entries before the legs, with `-FetchCorpus` a fetch first) and `determinism`
(`msil/utils/ci/det-manifest.py build` from two work directories and `compare`). It clears
`MSIL_CORPUS_SKIP`, so the phase gate always runs CoreMark and c-testsuite.

The CI workflow (`.github/workflows/msil-ci.yml`, still dispatched by hand only) caches the
corpus with `actions/cache` keyed on `manifest.json`, fills it with `fetch.py --cache` and checks
it with `--check`, both with `--skip` of the workflow's `MSIL_CORPUS_SKIP: coremark,c-testsuite`
(the CI licences of those two are unverified, plan section 10 item 11): their tests are
UNSUPPORTED there and the `ctest` leg is a SKIP row, never a pass. After the legs each job writes
a manifest of the SHA-256 of every CIL object and archive it builds from `msil/test/M1` (both
x86_64 MSIL triples, `-O0` and `-O2`), and the `determinism` job requires the Linux and Windows
manifests to be equal. Images are compared within each host only (`det`, `det-wsl` and the CI's
`det` leg), because neither CI host can link the other OS's triple (section 3).

### Phase 1 exit criteria (Design.md section 12)

Phase 1's criteria, as M10 and M11 met them and later milestones kept them. Since M23 the same
tests build through CIL objects by default, and the .NET 10 and `DOTNET_TieredCompilation=0` runs
named below are `msil_also_run` entries of `net8` and `wsl-net10`, which also run what the `obj` and
`obj-wsl` legs named below ran (the Phase 2 criteria above).

| Criterion | How Phase 1 meets it |
|---|---|
| About 25 small C programs compile at `-O0` and `-O2`, run, and match native clang's output, as a lit suite in CI | `msil/test/M1/11-programs/` has 20 programs (fib, sieve, quicksort over a global array, mergesort, n-queens, hanoi, matrix multiplication, crc32, a string table with relocations, a bytecode VM dispatching through a function-pointer table, fnv1a, string functions, bignum addition, life, fixed-point mandelbrot, collatz, a struct-heavy simulation, linked lists and binary trees in static pools, K&R code). With the struct and function-pointer programs of `08-fnptr/`, `09-structs/` and `10-calls/` and the other execution tests in `msil/test/M1/` (143 in all: 99 C programs, 43 IR files and one test that compiles a C program with `-g`), each test runs at `-O0` and `-O2` and its transcript (stdout and exit status) must equal the one a native clang build produces (on Windows linked with the compiler-rt builtins of the Visual Studio LLVM, for the `__int128` helpers a native build calls; for the Linux triple in WSL, a native Linux build linked on Windows and run in WSL, the E7 oracle of GettingStarted.md section 7); the gate also runs every C program of `msil/test/M1` at `-O1`, `-O3`, `-Os` and `-Oz` and every IR test that uses `%{msil_ll}` (40 of the 43) at `-O1` and `-O3` (the `more-levels` leg, on .NET 8); only the three trap tests `01-int/trap-call-before-div.ll`, `01-int/trap-call-before-rem.ll` and `03-control/trap.ll` pin `-O0` and `-O2` in every leg, through CIL objects or, under `asm`, through llc's text and ilasm. The `-g` test, `01-int/calls-arith-debug-info.test`, is a `%{msil_prog}` composite of `calls-arith.c` with `-g` (`%{msil_copts}` is `-g`, `%{msil_expect}` is `calls-arith`): it runs at each leg's `msil_opt_levels` (so `more-levels` runs it at `-O1`, `-O3`, `-Os` and `-Oz`) and gets the composite's runs (`msil_also_run`'s runtimes, the native oracle, .NET Framework), and under `asm` it adds `clang -S -g` and ilasm at `-O0` and `-O2`. The gate runs the suite on .NET 8 (tiered and with `DOTNET_TieredCompilation=0`), .NET 10, .NET Framework 4.8, and in WSL on a private .NET 10 for `x86_64-unknown-linux-gnu-msil` (tiered and optimizing JIT), assembles the programs with the Framework ilasm, the modern Windows ilasm and the Linux ilasm, and round-trips them through the modern ildasm and ilasm (Design.md 5.5). It also runs the upstream regression tests of the 55 upstream files the fork changes (327 lit tests, all of `TargetParserTests` and `MCTests`, and `LLDAsLibAllTests`; the upstream-hooks check of GettingStarted.md section 7). Since M18 it also requires that the backend's binary encoding of every method of the C programs of `msil/test/M1`, at `-O0` and `-O2`, equal what ilasm assembles from the same text (the `encoder-parity` leg; section 2, "Binary CIL"). Since M19 it also requires that the CIL object `clang -c` writes of each C program of `msil/test/M1` at `-O0` and `-O2`, and of each program of `11-programs` for the Linux triple, list exactly like ilasm's object of its object-form text, link-info record included, come out the same from two directories, open in `mdcanon` and both ildasms, agree with its record, and give `llvm-nm`, `llvm-ar` and, in WSL, GNU `nm` and `ar` the symbols of plan 5.8.6 (the `obj-text` and `obj-text-wsl` legs; section 2, "CIL objects"). Since M20 the `obj` leg runs every test of `msil/test/M1` again as a program that `msil-ld` links from CIL objects (`clang -c`, or `llc -filetype=obj` for the IR tests) into an image of its own, at `-O0` and `-O2`, on .NET 8 and .NET Framework 4.8 against the native oracle, with the equivalence oracles O3 (the image of the one-object link lists exactly like ilasm's image of the same file's `-S` text, up to the normalizations of section 2) and O6 (it gets no PEVerify metadata message that ilasm's image does not get), and the `obj-wsl` leg runs the programs of `11-programs` and `msil/test/M2` that way in WSL on .NET 10 (section 2, "CIL object links and the image"). Since M21 the legs `refs-net48`, `refs-net8` and `refs-net10` run `11-programs`, `msil/test/M2/00-link` and `msil/test/M2/05-refs` through CIL objects linked against a reference pack: the .NET Framework 4.8 reference assemblies on .NET 8 and .NET Framework 4.8 (with O3 up to the assembly scopes, and O6), the .NET 8 pack on .NET 8 and the .NET 10 pack on .NET 10 (ILVerify against the pack, PEVerify skipped), and `check-mscorlib-safe.py` runs every entry of `msil-ld`'s built-in `[mscorlib]` table on .NET Framework 4.8, .NET 8, .NET 10 and WSL .NET 10 (section 2, "Reference assemblies and the base class library"). Since M22 the legs that run `msil/test/M2` also run its `04-shared` directory and the library rows of the link-semantics matrix: programs linked against libraries that the test builds with `msil-ld -shared` (a library and its program, two- and three-library cycles, initialization order, shared C library state, a missing library and a missing member, and a C# program compiled by csc against a library), against native oracles built from the same sources as DLLs on Windows and as `-Bsymbolic` ELF shared objects in WSL, also with `DOTNET_TieredCompilation=0` and bound to the .NET 8 pack on .NET 10 (section 2, "Libraries and assembly-to-assembly linking"). CI (`.github/workflows/msil-ci.yml`) runs the same scripts as the local gate; it has a manual trigger only until the repository owner enables more |
| FileCheck tests on the emitted `.il` | 138 llc and MIR tests in `llvm/test/CodeGen/MSIL` (131 `.ll`, among them M23's `ctor-envp-text.ll` for constructors that take `argc`, `argv` and `envp` in whole-program text on the Linux triples (glibc and musl) and their error on the Windows triples, M22's `asm-comment-barrier.ll` for inline asm whose template holds only comments, `lto-module-flags.ll` for the module flags of `msil-ld`, `imports-linked.ll` for the linked import mode of M15, the 12 `object-*.ll` tests of M19, which write CIL objects with `llc -filetype=obj`, read them back with `llvm-cil-dump`, `llvm-nm` and `llvm-ar` and compare them with ilasm's objects of their object-form text, and M20's `sections-whole-program.ll`, `reloc-loop-threshold.ll`, `function-bitcasts-lto.ll` and `imports-linked-data.ll` for the named sections, the relocation loop, the call adapters of LTO and the data imports of linked text, and 7 `.mir`; both x86_64 triples, the aarch64 triples where the ABI differs, ilasm round trip where a module is complete; since M18 they pin the shortest forms and `nomangle`) and one test there of the bitcode symbol table (`irsymtab-linker-options.ll`), the 9 MC tests of `llvm/test/MC/MSIL` (M18: `llc -show-mc-encoding` for every operand kind, branches at the short-form boundaries, `switch` entries and the synthesized `.cctor` and `'<msil>Main'`, the shortest forms, the BCL members, every operand class in both spellings of `-M object-form`, `nomangle` on four triples with its ImplMap flags 0x0201 read back from ilasm's image, and the clean error of `llvm-mc -filetype=obj`), 22 clang tests in `clang/test/CodeGen/MSIL` (including `design-milestone1.c`, Design.md 12's literal command on fib, and, since M19, `emit-obj.c`, whose `cc1 -emit-obj` writes CIL objects and which reads the headers and record of the objects of `-c` and clang-cl `/c`, and `section-globals.c`, whose constructor registered in `.init_array` or `.CRT$XCU` compiles since M20 and runs from `.cctor`), the driver tests in `clang/test/Driver/msil-toolchain.c` and, for the MSIL toolchain of M14, `msil-link.c` (with M15's `--ucrtbase-imports` on windows-msvc), `msil-lto.c` and `msil-header-search.c`, and, for M19, `msil-object.c` (`-c` and clang-cl `/c` write CIL objects: their cc1 jobs and errors, and the bytes from two directories, `llvm-nm` letters and object-form text of what they write), and, for M22, `msil-shared.c` (`-shared` on the four x86_64 MSIL triples and clang-cl `/LD` and `/LDd` give `msil-ld` `--shared`), the 111 `msil-ld` tests in `lld/test/MSIL` (by prefix 28 `obj-`, 23 `native-`, 17 `bcl-`, 15 `shared-` and 28 others: `dispatch.test` of M13, 24 of the 25 of M14, ten of which M15 updated to its rules, the 21 `native-*.test` of M15, whose ELF shared objects, import libraries and DLLs `yaml2obj`, `llvm-mc`, `ld.lld`, `lld-link` and `llvm-lib` make, the 25 `obj-*.test` of M20, which link CIL objects of `llc -filetype=obj` and read the image with `llvm-cil-dump`, and M21's 17 `bcl-*.test`, which bind against the synthetic reference assemblies of `Inputs/fakepack/` (ilasm sources, their `yaml2obj` images and the generator `gen.py`), with `obj-ctors-null.test` and `start-stop-hidden.test` for two follow-ups of M20, and M22's 15 `shared-*.test`, which build libraries with `msil-ld -shared` and link programs and libraries against them, and `lto-code-model.test`, `native-object-inputs.test`, `obj-ctors-args.test` and `obj-ctors-main-args.test` for three of the four defects of the Rust experiment, and M23's `output-name-netcore.test` for the warning about a `.exe` bound to .NET's core library and `native-alternatename.test` for `/alternatename` over native machine code; `dispatch.test` and `dispatch-coff.test` check which program names reach `msil-ld` and that the other lld flavors, COFF's included, dispatch as before; all but `dispatch-coff.test` also run with `LLD_IN_TEST=2`), the 14 tests of the ECMA-335 tools `llvm-cil-dump` and `llvm-cil-copy` in `llvm/test/tools` (M17; their inputs are `yaml2obj` copies of ilasm images), and unit tests in `llvm/unittests/Target/MSIL` (`MSILImportsTest` pins the standalone import table and the linked import mode; M18's suites `MSILCILRecords`, `MSILBCLMembers`, `MSILBranchForms`, `MSILMCCodeEmitter` and `MSILAsmBackend`, 42 tests, pin the 219 CIL records against `CILOpcodes.def`, the BCL list, the choice of branch forms, the bytes and fixups of every record and the asm backend, and `MSILTargetStreamerTest` the streamer and both spellings of every operand class) and, for the ECMA-335 library (its headers and the link-info record of M13, its writer half of M16, its reader, PE writer, codecs and dumper of M17, and the link model of M20), `llvm/unittests/CLI` (`CLITests`, 21 suites; 426 tests, at least 210 by the floor of the gates from M20; since M22 `CLISymbolTableTest` has 19, at least 9 by a second floor, for the symbol table's INTERFACE flag and ABI descriptors) |
| `ilverify` reports no stack-shape errors | replaced by the verification policy of section 4 (decision D21, approved by the user 2026-10-12), enforced in every gate |

## 2. What works

### Toolchain and output

- `clang --target=<msil triple> -S` and `llc` write ILAsm text; `ilasm -dll -pe64 -x64` (the
  .NET Framework 4.8 ilasm or the NuGet ilasm 10.0.12) assembles it. One C file (one LLVM module)
  is one assembly, and so is a program of several files that `msil-ld` links (M14, below), whose
  image `msil-ld` writes itself since M20, without ilasm ("CIL object links and the image"
  below). Every optimization level works.
- Since M18 the text uses the shortest instruction forms, short branches (`br.s`, `blt.s`, ...)
  wherever the target is in reach included, and every P/Invoke is `pinvokeimpl("<library>"
  nomangle cdecl)`; `llc -show-mc-encoding` prints the binary encoding of every instruction
  ("Binary CIL" below).
- The module references only `mscorlib` (D10), so one assembly runs on .NET 8, 9 and 10 and, for
  Windows triples, on .NET Framework 4.8 (as an `.exe`). The gate runs .NET 8, 10 and Framework
  4.8; .NET 9 was only covered by the planning experiments. `msil-ld` writes such an image too
  when the link has no reference assemblies; with them (M21) it binds the base class library to
  the assemblies given ("Reference assemblies and the base class library" below).
- C functions become static methods of `<Module>`: `public` for external linkage, `assembly`
  otherwise. A generated `'<msil>Main'(string[])` is the entry point (a library that `msil-ld
  -shared` links has none, M22: "Libraries and assembly-to-assembly linking" below). It calls
  `main(void)`, or
  `main(int, char **)` with a NULL-terminated `argv` built from
  `Environment.GetCommandLineArgs()` (`argv[0]` is the `.dll` path under `dotnet x.dll`). Since
  M20 a narrow integer result (`short`, `char` or `_Bool main`) counts as `int` on every output,
  and a `main` of an `int` and a pointer-sized integer gets `argv` too; any other signature of
  `main` is an error (D27).
- The assembly is named after the source file's stem, or `-mllvm -msil-assembly-name=<name>`
  (D28); in a program that `msil-ld` links, after the output file (M14, below).
- `-c` (clang-cl `/c`) writes a CIL object since M19, and so do `llc -filetype=obj` and `clang
  -cc1 -emit-obj` ("CIL objects" below). `-flto -c` and `-emit-llvm -c` write LLVM bitcode.
  Without `-S` or `-c`, clang compiles each source to a CIL object (to bitcode with `-flto`) and
  links the program with `msil-ld` (M14, below); in M14-M19 such a command compiled to bitcode,
  because `msil-ld` linked CIL objects only from M20. Before M19 `-c` wrote bitcode as `-flto
  -c` does, `-fno-lto -c` stopped with the driver error `CIL object files are not supported for
  MSIL target '<triple>' yet; use -flto to write LLVM bitcode objects, or -S to write ILAsm
  text` (M13's text; before M14 every `-c` and every link stopped there), and `llc
  -filetype=obj` with `MSIL object file emission is not implemented; use -S (ILAsm text)`.
  Assembler input (`-x assembler`, `.s` and `.S` files) and an assemble step (`-save-temps` or
  `-save-temps=obj` with `-c` or, since M20, on a command that compiles and links without
  `-flto`, `-via-file-asm`, clang-cl's `/FA` and `/Fa`, which an explicit `-flto` ignores) stop
  with `assembling ILAsm text is not supported for MSIL target '<triple>'; compile C or LLVM IR
  sources instead, or use -S to write ILAsm text`, and `-fno-integrated-as` with `-c` (since M20
  also on such a command) with `there is no external assembler that can be used on this
  platform`. `llvm-mc -filetype=obj` and `clang -cc1as -filetype obj`, which crashed or asserted
  before M18, stop with `assembly parsing is not supported for MSIL`.
- `msil-ld` is lld's flavor `msil` (M13): `msil-ld` (a copy of `lld`), `lld -flavor msil` and
  every program name with an `msil` component (such as `x86_64-unknown-linux-gnu-msil-ld`)
  start it, and the other flavors (`ld.lld`, `lld-link`, `ld64.lld`, `wasm-ld`) dispatch as
  before. M13's stub, which linked nothing, is a linker since M14 (below).
- `#pragma comment(lib, "<name>")` (M13) is kept on every MSIL triple, as the native triple
  spells it in `!llvm.linker.options`: `-l<name>` on the Linux triples, `/DEFAULTLIB:<name>.lib`
  on the Windows ones. The bitcode symbol table of an MSIL module carries these options, as it
  does for COFF, and `msil-ld` links the libraries they name (M15, "Native libraries and
  P/Invokes" below); `-S` output does not use them. The Linux MSIL triples
  accept only the `lib` kind and ignore the others with clang's warning, as ELF triples do; the
  Windows MSIL triples accept every kind, as MSVC triples do. Before M13 the pragma was dropped
  silently on the Linux triples.
- Bitcode of an MSIL triple and bitcode of its native triple are compatible
  (`Triple::isCompatibleWith`, M13), so the IR linker links them without the warning
  `Linking two modules of different target triples`.
- Unsupported input ends in an error that names the function or feature, with exit status 1,
  never a crash.
- `-mllvm` passes backend options from clang as llc takes them, and llc's stop points
  (`-stop-after=`, `-stop-before=`) work at every MSIL pass from `msil-isel` on: llc stops
  there, and the MIR it prints passes the machine verifier. The MIR of instruction selection
  reads back and compiles on with `-start-after=msil-isel` to the same IL as the full pipeline
  (GettingStarted.md section 8). Reading MIR back has three limits, all from upstream code;
  none of them changes the IL of a normal compile:
  - MIR with a floating-point infinity or NaN constant reads back at no stop point,
    `msil-isel` included: `CONST_F32` and `CONST_F64` print it as `float +inf`,
    `double -inf` or `double +qnan`, which the MIR parser rejects
    (`expected a floating point literal`), an upstream MIR printer and parser mismatch that
    WebAssembly shows too.
  - For a function that takes block addresses (computed goto), the IL of
    `-start-after=msil-isel` lacks the `@tmpN: // Address of block that was removed by CodeGen`
    labels that the full pipeline prints; every other line is the same. IndirectBrExpand, which
    runs before instruction selection, replaces each `blockaddress` with the block's number, so
    the IR inside the MIR takes no block address any more, while the full pipeline still prints
    an unused label for each block whose address the input IR took.
  - MIR printed at `branch-folder` or a later stop point does not read back yet when a `switch`
    became a jump table (`llc -run-pass=none` asserts `Cannot create an empty jump table!`:
    BranchFolder empties the table the MSIL `switch` does not reference, an upstream MIR
    printer and parser issue that WebAssembly shows too).

### Programs of several files (M14)

How to compile, link, assemble and run such a program, and every option, is in
GettingStarted.md section 3 ("Programs of several files").

- **clang's MSIL toolchain.** Every Linux, windows-msvc and windows-gnu MSIL triple gets
  `MSILToolChain` over its native toolchain (Linux, MSVC or MinGW), which keeps the native
  header search, predefines and ABI; any other MSIL triple (Darwin, FreeBSD, Cygwin, ...) ends
  with `unknown target triple '<triple>'`, also for a command that only links, and never runs a
  native linker. `-flto -c` writes LLVM bitcode (`-emit-llvm-bc -flto=full`), as plain `-c`
  did until M19 (it writes CIL objects since then, "CIL objects" below) and every compile of a
  command that also links did until M20 (since M20 such a compile writes a CIL object too),
  and a link runs `msil-ld` (never a native linker) with the triple's C library (`-lc -lm`,
  `-lucrt -lkernel32 -loldnames --ucrtbase-imports`, ...), the LTO level of `-O` (since M20 only
  when the link has `-flto`) and the `-L`, `-l` and `-Wl` options in command-line order; the
  default output is `a.exe`, and `-nostartfiles` is
  accepted silently, because MSIL has no crt files. On the `-S` command line only
  `-mrelocation-model static` (instead of PIC) and the missing `-funwind-tables=2` change (on
  the aarch64 triples also two target
  features, `-fmv` no longer off and `+outline-atomics`, from the compiler-rt runtime default),
  and the IL is the same: 495 `-S` compiles of the 99 C programs of `msil/test/M1` (both
  x86_64 triples at `-O0` and `-O2`, `aarch64-unknown-linux-gnu-msil` at `-O2`) gave identical
  IL and diagnostics.
- **What `msil-ld` does.** It links LLVM bitcode files and archives of them (and since M20 CIL
  objects and archives of them, "CIL object links and the image" below), in command-line order,
  by lld's rules: archive members are lazy (also through `-l`, `--start-lib`/`--end-lib`;
  `--whole-archive` loads them all), and, as in lld, every member that defines a runtime
  library-call name of the target is extracted before LTO (a limit, below). Symbols resolve by a
  port of lld/ELF's resolver (plan 5.9.5) in either input order: strong, weak and common
  definitions, weak undefined references, aliases, and COMDAT groups (the first copy wins; the
  selection kinds `exactmatch`, `largest` and `samesize` are an error naming the group). LTO
  compiles the merged program, in which only `main` and the `-u` symbols stay visible, and
  `--lto-emit-asm` writes its ILAsm text, which `ilasm -exe` assembles into one image for .NET
  and .NET Framework; since M20 LTO writes CIL objects in memory instead, `msil-ld` writes the
  image, and `--lto-emit-asm` is optional (in M14-M19 every link needed it and ilasm, and any
  other LTO link, ThinLTO bitcode and `--lto-partitions` above 1 stopped with `ThinLTO,
  --lto-partitions and LTO without --lto-emit-asm need CIL object output, which this msil-ld
  does not have; use --lto-emit-asm (clang: -Wl,--lto-emit-asm) to write ILAsm text`). The
  assembly is named after the output (`--assembly-name` or `-soname` name another), through the
  module flag `msil-assembly-name`. A function that no input defines binds to a native library
  of the link (M15, "Native libraries and P/Invokes" below), and a symbol that binds nowhere is
  `undefined symbol: <name>`. (In M14 the backend bound such functions through the Phase-1
  import table, D14, as it still does for a module that has `msil-ld`'s module flag `msil-lto`
  without M15's `msil-linked`.) Bitcode from stock clang for the native triple links with one
  warning per link (plan Q12; its limits are in GettingStarted.md section 3); every input must
  have the link's architecture, OS and environment. The link's triple (`--target`, else the
  first bitcode input's with `-msil` appended) must be one of D6's, x86_64 or aarch64 on Linux,
  windows-msvc or windows-gnu: any other, such as that of stock bitcode for Darwin or i686,
  stops the link with `unsupported target triple '<codegen triple>' for MSIL`. Diagnostics use
  lld's texts (`duplicate symbol`, `undefined symbol`, `undefined hidden symbol`, `symbol type
  mismatch`, `TLS attribute mismatch`, `unable to find library`, ...), and `-Map`,
  `--why-extract`, `--reproduce`, `--trace` and `-y` work as in lld. The same inputs give the
  same output byte for byte (the `det` leg).
- **What `msil-ld` does not do yet.**
  - Call native variadic functions, native functions with aggregate parameters or results, and
    native functions that take C function pointers (Phase 3): "Native libraries and P/Invokes"
    below.
  - `-r`, which is never supported. Native data, entry points other than `main` and
    `--gc-sections` came with M20, reference assemblies (`--reference`) with M21, and `-shared`
    and the .NET libraries that this toolchain builds with M22 ("Libraries and
    assembly-to-assembly linking" below): in M14-M21 `-shared` was `-shared is not supported
    yet`, and in M21 such a library given as an input was `<path>: linking .NET libraries built
    by this toolchain is not supported yet` (every other .NET assembly is a reference assembly
    since M21; in M20 a .NET assembly was `<file>: .NET assembly inputs are not supported yet`,
    and in M14-M19 `<file>: linking CIL object files and .NET assemblies is not supported yet`,
    as was a CIL object).
  - Leave out an archive member that nothing references when it defines a runtime library-call
    name of the target (`fmod`, `floor`, `memcpy`, ...). As lld does, `msil-ld` extracts every
    such bitcode member, so its constructors run and its other definitions and references join
    the link, which can give `duplicate symbol` or `undefined symbol` errors that a native
    static link without LTO would not give. Native LTO through lld behaves the same (plan
    5.9.13; narrowing the names is left for phase 3).
- **LTO and argument promotion.** LTO makes every function but `main`, the `-u` symbols and
  (since M20) the symbols that CIL objects of the link reference internal, so the optimizer may
  change the parameters of the others.
  `MSILTTIImpl::areTypesABICompatible` keeps ArgumentPromotion from turning a pointer parameter
  into a by-value `half` or `bfloat` one (`_Float16`, `__bf16`), which can need the phase-3
  helpers of section 3, so a program such as `msil/test/M1/02-fp/i128-fp-conv.c` links through
  `msil-ld` at `-O2` as it compiles with `-S` (plan D33 as amended for M14).

### Native libraries and P/Invokes (M15)

How to link against native libraries, with examples, is in GettingStarted.md section 3
("Native libraries and P/Invokes"); plan 5.10 has the contract.

- **What `msil-ld` reads.** Every native library on the command line or found by `-l`, through
  `llvm::object`, in the object format of the triple's OS only. On the Linux triples: ELF
  shared objects (the `.dynsym` definitions with their symbol versions) and GNU ld scripts
  (`GROUP`, `INPUT`, `AS_NEEDED` and `-l` inside them, found as lld/ELF finds them, and an
  absolute path under `--sysroot` when the script lies inside the sysroot, as glibc's `libc.so`
  needs). On the Windows triples: COFF import libraries (short import members with lld-link's
  name types, and MinGW's long-format members), the weak-alias objects of `oldnames.lib` and
  the export tables of native DLLs. Also archives of native objects and, since M22, native
  object files given on the command line, whose definitions are only named in an error ("Rust
  bitcode" below). A library of the other format, given on the command line, by `-l:<file>`
  or by a script, is an error, `<path>: ELF file cannot be imported for target <triple>` on a
  Windows triple (lld-link rejects an ELF input) and `<path>: PE/COFF file cannot be imported
  for target <triple>` on a Linux one (ld.lld rejects a DLL and ignores COFF import members);
  `-l` passes over such a candidate (`--verbose`: `skipping ` and that text) and goes on with
  the next one. A file's machine and ELF class must match the triple (`<file>: ELF machine
  EM_AARCH64 does not match target <triple>`, and the COFF and PE forms). Every file is read
  through the driver, so `--reproduce` captures it.
- **Names (plan Q13, 5.10.11).** A P/Invoke names the module as a native link records it,
  verbatim: the ELF `DT_SONAME` (`libc.so.6`, `libm.so.6`), else the file name; for an import
  library the DLL name of the import member (`api-ms-win-crt-stdio-l1-1-0.dll` from `ucrt.lib`,
  `KERNEL32.dll`, `ucrtbase.dll`); for a DLL that `msil-ld` reads (on the command line, by `-l`
  or by `-l:`) the DLL's file name as found (`native.dll`, `Renamed.DLL`), never the name in
  its export directory, which differs for a DLL renamed after it was linked; never a bare
  `libc`. The entry is the export name: `#<n>`
  for an import by ordinal only (`htons` from `ws2_32.lib` is `WS2_32.dll` `#9`), the
  `EXPORTAS` name, the target of an oldnames alias (`strdup` is `_strdup`). The
  `--lto-emit-asm` text declares each bound function as
  `.method assembly hidebysig static pinvokeimpl("api-ms-win-crt-string-l1-1-0.dll" as
  "_strdup" nomangle cdecl) native int 'strdup'(native int) cil managed preservesig {}`:
  `assembly`, where `-S` prints its standalone imports `public`, and `nomangle` since M18
  (ImplMap flags 0x0201, Design.md 5.3's `ExactSpelling`: the runtime looks the entry up by
  exactly that name; before M18 it printed `cdecl` alone).
- **Bindability (5.10.10).** A P/Invoke binds what `dlsym` and `GetProcAddress` reach: an ELF
  default-version (`@@`) or unversioned definition that is neither thread-local nor
  `GLIBC_PRIVATE` (an IFUNC, such as glibc's `strlen`, is a function: the loader runs its
  resolver), or a COFF import or DLL export of the right kind (an export in a section that is
  not code is data). A call never binds a COFF `DATA` or `CONST` import (for lld-link such a
  member defines only `__imp_<name>`) or a DLL's data export: lookup goes on with the later
  libraries, as in lld-link, and `'<name>' in <module> is data, but <file> calls it as a
  function` is reported only when no later library binds the name. Among ELF libraries the
  first definition wins whatever its type, as with `ld.so`. A definition that cannot
  be bound is kept for the error that names it when nothing else binds the symbol: only a
  hidden version (`symbol 'f' in libfoo.so.1 has only the non-default version f@V1, which a
  P/Invoke cannot bind`), thread-local data (`symbol 'errno' in libc.so.6 is thread-local data
  and cannot be imported; use <errno.h>`, the advice for `errno` only: `h_errno` or a library's
  own `__thread` variable gets the text without it), a `GLIBC_PRIVATE` symbol, and machine
  code in a native archive (`symbol 'atexit' is defined only in native object code
  (<path>/libc_nonshared.a(atexit.oS)); MSIL cannot link machine code`).
- **Resolution order.** Objects and archive members first, by lld/ELF's rules (section 2);
  then the native libraries in command-line order, where the first bindable definition wins
  and two libraries that define one name are no error. A native definition is bound late: it
  never preempts a definition of the program and never extracts an archive member, so a
  program's own `malloc` replaces the C library's for the program's code only (native code
  keeps calling its own). A weak undefined symbol that nothing defines stays null (M12).
  Anything else is `undefined symbol: <name>`.
- **Checked after LTO.** Bitcode symbol tables carry neither the import attributes nor the
  library calls that the optimizer adds (`printf` -> `puts`), so `msil-ld` binds and reports
  on the merged module after LTO, before code generation, as lld/ELF and lld/wasm report
  undefined symbols after LTO. Every error is followed by `>>> referenced by
  <file>:(<function>)` lines (`>>> referenced by ld-temp.o` for a name that only the optimizer
  introduced), and only live references count: a call that LTO removed is not reported. A
  hidden, protected or discarded undefined symbol is still reported before LTO. `msil-ld`
  writes the result into the module, as the module flag `msil-linked`, the attributes
  `msil-import-module` and `msil-import-name` on each bound declaration and the named metadata
  `!msil.imports` for the library functions that code generation may call; in that linked
  mode the backend takes every P/Invoke from these annotations and never consults D14
  (5.10.13). A library call that nothing bound is `library call '<name>' was not resolved by
  msil-ld`, and a `__` runtime helper keeps its phase-3 error. A link of CIL objects (M20)
  binds and reports the same way at link time, for the references of live code only (the
  module's code generator already ran when `clang -c` wrote each object), and the checks of
  native callees that M15 made on the merged module after LTO (variadic, not passable natively,
  callbacks, a prototype-less function used only by address) run for every object, LTO's
  in-memory objects included ("CIL object links and the image" below).
- **Native data (M20, plan 5.10.12).** A variable that no file defines and that a native
  library defines as data (glibc's `environ`, `optind` and `stderr`, `msvcrt.dll`'s `_iob`, a
  variable of a library of your own, an `extern` variable with the import attributes) keeps its
  address field, which `'<msil>init_dataimports'()` fills when the program starts, from
  `<Module>::.cctor` before the data relocations and constructors, so initializers that take
  its address work. On the Windows triples it calls `LoadLibraryW` and `GetProcAddress` of
  `kernel32.dll`; on the Linux triples `dlopen(<module>, RTLD_LAZY)` and `dlsym`, which bind
  from the link's libraries like any import (glibc 2.34 and later has them in `libc.so.6`).
  Since M21, when the link's reference assemblies define
  `System.Runtime.InteropServices.NativeLibrary` (the .NET 8 and .NET 10 packs do, the .NET
  Framework's do not), the helper calls `NativeLibrary.Load(string)` and
  `NativeLibrary.GetExport(native int, string)` instead, on every triple, and binds no native
  function for it. `--print-imports` lists the variables as `[data, <version>]` and the
  helper's native functions. A library that cannot be loaded or lacks the name throws
  `System.EntryPointNotFoundException: data import '<name>' not found in <module>` from the
  module initializer (a `System.TypeInitializationException` for the program), as a native
  program fails to start; through `NativeLibrary` the exception is the one `Load` or
  `GetExport` throws (`System.DllNotFoundException` or `System.EntryPointNotFoundException`).
  A Linux link whose libraries define no `dlopen`/`dlsym` (`-nodefaultlibs`) and whose
  references do not define `NativeLibrary` stops with `data symbol '<name>' in <module> cannot
  be imported: no reference defines System.Runtime.InteropServices.NativeLibrary and the native
  libraries define no dlopen/dlsym`. The `--lto-emit-asm` text, which is never bound to
  references (M21), imports native data with M20's helper, so there a Linux link that has
  references, even ones that define `NativeLibrary`, and whose libraries define no
  `dlopen`/`dlsym` stops with `data symbol '<name>' in <module> cannot be imported:
  --lto-emit-asm text is never bound to the references, and the native libraries define no
  dlopen/dlsym`. In M15-M19 such a
  variable was `importing native data symbol '<name>' (<module>) is not supported yet`. A
  function that the program declares and calls but that the library defines as data stays
  `'<name>' in <module> is data, but <file> calls it as a function`.
- **Default libraries (Q14, plan 5.10.4).** The driver's default libraries now bind: `-lc -lm`
  on the Linux triples (from a `--sysroot`, such as the E7 sysroot of GettingStarted.md section
  7), `-lucrt -lkernel32 -loldnames --ucrtbase-imports` on windows-msvc (from `LIB` or the
  detected Windows SDK), `-lmsvcrt -ladvapi32 -lshell32 -luser32 -lkernel32` on windows-gnu
  (from the mingw-w64 import libraries of a `--sysroot` or `-L` directory).
  `--ucrtbase-imports` is an import library built into `msil-ld` (Windows targets only; on
  others `--ucrtbase-imports needs a Windows target, not <triple>`) that binds the 11
  functions native programs take from vcruntime, which `ucrt.lib` lacks: `memcpy`, `memset`,
  `memmove`, `memcmp`, `memchr`, `strchr`, `strrchr`, `strstr`, `wcschr`, `wcsrchr` and
  `wcsstr`, to `ucrtbase.dll`, which exports all of them. A `-lvcruntime` of yours comes
  earlier on the line and wins. `msil-ld` itself has no default libraries.
- **Libraries the program names (5.10.6).** `#pragma comment(lib, "<name>")` and the other
  linker options of bitcode inputs (`/DEFAULTLIB:`, `-l`, `-l:`, the dependent libraries of
  stock ELF bitcode, `/alternatename:`, `/failifmismatch:`) are read from each input when it is
  loaded, from an archive member when it is extracted, and each library is read once. A request
  for an MSVC C or C++ runtime library (`libcmt`, which clang names in every windows-msvc
  object, `msvcrt`, `ucrt`, `vcruntime`, ...) is ignored with a `--verbose` note, because the
  default libraries supply the C runtime; another option is ignored with a warning; a library
  that is not found is `unable to find library from dependent library specifier: <name>` with
  `>>> requested by <file>`. `--no-dependent-libraries` drops them all.
- **`-l` search (5.10.9).** The `-L` directories in order; the first that holds an acceptable
  candidate (`lib<n>.a`, `<n>.lib` and `<n>.dll` on windows-msvc; the MinGW names
  `lib<n>.dll.a` and `<n>.dll.a` too on windows-gnu; `lib<n>.a` and `lib<n>.so` on Linux, where
  `<n>.dll` and `lib<n>.dll` count only as .NET libraries of this toolchain, so `-lfoo` in a
  directory holding a native `foo.dll`, `foo.lib` and `libfoo.so` takes `libfoo.so`, as ld.lld
  does) wins, and inside it a static library (with bitcode members) wins, then (since M22) a
  library assembly that this toolchain built, then an import library or a shared one:
  Design.md 5.2's static-first rule, the opposite of GNU ld's. An archive of machine code
  only is skipped (`--verbose`: `skipping native static archive <path> (no CIL or bitcode
  members)`), and so is a .NET assembly without this toolchain's symbol table, with a warning
  that names `--reference`. `-Bstatic` accepts only static libraries until `-Bdynamic`, `-l:<file>`
  takes that file whatever its kind (one of the other object format is the error above), and
  an empty archive (glibc's `libpthread.a` and `libdl.a` since 2.34) is a static library that
  defines nothing, so the `-lpthread` of `-pthread` links against such a glibc.
- **Reporting.** `--print-imports` prints one line per P/Invoke of the output, sorted,
  `<symbol> -> <module>!<entry> [function|data, <version>|-]` (`puts -> libc.so.6!puts
  [function, GLIBC_2.2.5]`), the library functions that code generation calls (`floor`,
  `sqrt`, ...) included, then for each ELF module `<module>: version floor <version>`, the
  highest default version bound in it, those of the library functions that code generation
  calls included (`-` when none is versioned), also when the link fails. A P/Invoke itself
  has no version: at run time it binds the default version of the library that runs it.
  `-Map` lists the native libraries under `Inputs:` and the bound symbols under `Symbols:` as
  `NativeFunction` or `NativeData` rows with the library as the input, and
  `--trace-symbol=<sym>` prints `<library>: shared definition of <sym>`. Since M20 the map of an
  image also has an `Imports:` section with the token, module and entry of every P/Invoke.
- **Tests.** The 21 `native-*.test` of `lld/test/MSIL` cover each reader, each search rule,
  each rejected kind of definition and each error text of plan 5.10.14 on synthetic libraries
  (yaml2obj, llvm-mc, `ld.lld`, `lld-link` and `llvm-lib /def:` make them). The programs of
  `msil/test/M2/03-native/` call the host's C library through the default libraries (string,
  memory, conversion and `malloc` functions, glibc's IFUNCs and its versioned libm functions,
  `kernel32.lib`, `htons` by ordinal, an oldnames alias, the 11 functions of
  `--ucrtbase-imports`) and a `native.dll` or `libnative.so` that the test builds and ships
  next to the program, and run on .NET 8 and Framework 4.8 and in WSL on .NET 10 against the
  native oracles; `msil/test/M2/09-hosted/` links a hosted glibc program against glibc's real
  `libc.so` and `libm.so` scripts (the optional `hosted` leg, GettingStarted.md section 7).
  Since M20 these programs link through CIL objects (`%{msil_prog}` of the default pipeline,
  and `lto` through bitcode), and `data-import-testlib`, `data-import-glibc` (Linux) and
  `data-import-msvcrt` (Windows) read and write native variables.
- **Not done yet.**
  - Native variadic functions (`printf`, phase 3): `native function 'printf' (libc.so.6) is
    variadic; calls of native variadic functions are not supported for MSIL yet (phase 3)`.
  - Native functions that take or return a struct by value, an `__int128`, a vector or a
    `_Float16` (phase 3): `native function '<name>' (<module>) takes or returns a value that a
    P/Invoke cannot pass natively yet (phase 3)`.
  - Native `long double` functions on x86_64 and aarch64 Linux (glibc and musl) and x86_64
    MinGW (phase 3): `long double` is `double` in MSIL (section 3), but the native one there is
    the x87 80-bit or the IEEE binary128 format, which the native ABI passes in memory, in
    `ST(0)` or in Q registers. A live call of, or reference to, a function of plan 5.10.14's
    `long double` list that native lookup binds is therefore a link error after LTO, `native
    function 'sqrtl' (libm.so.6) takes or returns a value that a P/Invoke cannot pass natively
    yet (phase 3)`, with `>>> referenced by` lines. The list holds the C99 and C11 `<math.h>`
    and `<complex.h>` `l` functions (`sqrtl`, `expl`, `powl`, `fabsl`, `frexpl`, `ldexpl`,
    `modfl`, `nanl`, `nexttowardl`, `cabsl`, ...), `nexttoward` and `nexttowardf` (a `long
    double` parameter), `strtold`, `wcstold` and glibc's `l` extensions (`sincosl`, `exp10l`,
    `significandl`, ...). It also covers glibc's TS 18661-3 `_Float64x` and `_Float128`
    functions, every name in which `f64x` or `f128` is followed by the end of the name or by
    `_` (`sqrtf64x`, `expf64x`, `strtof64x`, `strtof64x_l`, `wcstof64x`, `f32addf64x`,
    `sqrtf128`, `strtof128`, `__isinff128`, ...): with clang, glibc's `<bits/floatn.h>` makes
    `_Float64x` the source's `long double` (`double` in MSIL) while the native function passes
    the x87 or binary128 value, and a P/Invoke has no form of the native `_Float128` on any
    triple. Such a name is never bound quietly; a declaration with an explicit `import_module`
    is exempt (unchecked). windows-msvc (x86_64 and aarch64) and `aarch64-w64-windows-gnu`,
    where the native `long double` is `double` too, bind them. Call the `double` function
    instead (`sqrt` for `sqrtl` or `sqrtf64x`, `strtod` for `strtold`): it is the same function
    in an MSIL program.
  - `atexit` (phase 3, which intercepts it): glibc defines it only in machine code
    (`libc_nonshared.a`) and the UCRT does not export it, so it binds nowhere.
  - Native functions that take a C function pointer (phase 3, P3-B's native-callable thunks):
    native code that calls a function pointer of the program would enter managed code without
    a reverse-P/Invoke transition. Every live call of, or reference to, a function of plan
    5.10.14's callback list that native lookup binds is therefore a link error after LTO,
    `native function 'qsort' (libc.so.6) takes a C function pointer; passing functions to
    native code needs phase 3 thunks`, also when the call passes no function
    (`signal(SIGPIPE, SIG_IGN)`). The list has 46 names: `CreateThread`, `__sysv_signal`,
    `_beginthread`, `_beginthreadex`, `_lfind`, `_lfind_s`, `_lsearch`, `_lsearch_s`,
    `_onexit`, `at_quick_exit`, `atexit`, `bsd_signal`, `bsearch`, `bsearch_s`, `call_once`,
    `ftw`, `ftw64`, `lfind`, `lsearch`, `nftw`, `nftw64`, `on_exit`, `pthread_atfork`,
    `pthread_create`, `pthread_key_create`, `pthread_once`, `qsort`, `qsort_r`, `qsort_s`,
    `scandir`, `scandir64`, `scandirat`, `scandirat64`, `sigaction`, `signal`, `sigset`,
    `ssignal`, `sysv_signal`, `tdelete`, `tdestroy`, `tfind`, `thrd_create`, `tsearch`,
    `tss_create`, `twalk`, `twalk_r` (a name counts by its entry too, so `lfind` through the
    alias of `oldnames.lib` is `_lfind`). It also names glibc's other spellings of these
    functions, which its headers select through an asm label (`__REDIRECT`), so that standard C
    code references them instead: `signal` is `__sysv_signal` without `__USE_MISC`
    (`-std=c99`, `-std=c11` or `-std=c17`, `-ansi`, or `_POSIX_C_SOURCE` alone), and `ftw`,
    `nftw`, `scandir` and `scandirat` are `ftw64`, `nftw64`, `scandir64` and `scandirat64` with
    `_FILE_OFFSET_BITS=64`; `bsd_signal`, `scandirat`, `sigset`, `ssignal` and `sysv_signal`
    take a handler or a callback as well. The error names the symbol the program references
    (`native function '__sysv_signal' (libc.so.6) takes a C function pointer; ...`). A
    declaration with an explicit `import_module` is exempt, unchecked, and is the workaround
    for a call that passes no function of the program.
  - **Known gap until P3-B:** a function address passed to any other native function, such
    as a native library's own callback API or a Win32 callback (a window procedure,
    `EnumWindows`, `SetConsoleCtrlHandler`), is not diagnosed: the program links, and fails at
    run time when native code calls the function.

### The link-semantics matrix (M14, M15, M20, M22 and M23)

`msil/test/M2/08-semantics/` holds the rows of plan 5.9.16, built from the recon's probe
programs. Each case either prints the transcript of the same program built natively (by clang
and lld for the native triple) or stops with a named diagnostic, at `-O0` and `-O2`, through
LTO (the `lto` pipeline, M14) and, since M20, through CIL objects (the `obj` pipeline, which
`%{msil_prog}` also uses under the default pipeline); none ends in an exception at JIT or load
time, and both pipelines compare with the same `.expected` file, so they agree with each other.
Since M23 the gate runs them on .NET 8, .NET 10 (through `msil_also_run`) and .NET Framework
4.8 (the `net8` leg through objects, `lto` through bitcode) and in WSL on .NET 10 and the
private .NET 8 for the Linux triple (`wsl-net10` through objects, `lto-wsl` through bitcode, which also
runs the matrix rows of `00-link/`; before M23 `obj` and `obj-wsl`, before M20 `wsl-quick`), and `matrix.txt` lists every case
(section 1, "The link-semantics matrix"); the native transcripts are the same on both operating systems except in two cases
whose program differs by triple, each with a `.linux.expected` file of its own: `section-bracket`
(only the Windows program also walks an MSVC `$` group) and `narrow-arg-mismatch` (only the
Linux program makes the `signed char` calls). `initarray-args` runs its program on the Linux
triples only, against `initarray-args.linux.expected`; its `.expected` file is the native
Windows build with both constructors compiled out, which no MSIL leg compares against. M14 wrote
20 of the 47 tests, M15 `pragma-comment-lib` and `native-callback`, M19 `knr-two-arities`, M20
fifteen, M22 the three library rows `p2c-*` (the libraries are built by `%{msil_shlibs}`,
natively as DLLs on Windows and as `-Bsymbolic` shared objects for the E7 oracle) and
`initarray-args`, and M23 the four P3 cases `fini-array`, `crt-xt`, `atexit-order` and
`variadic-def-xtu` and `matrix-coverage`, which checks `matrix.txt` against the tests.

| Case | What it links | Native | MSIL |
|---|---|---|---|
| `zoo` | two objects: a weak definition and the strong one that overrides it, a weak undefined function, COMDAT string literals (windows-msvc), common symbols, a function alias in a table, constructor priorities across objects, pointers to the other object in data, `static` names in both | 63 | 63 |
| `zoo-archive` | `zoo/a.c` with an archive of `b.c`, `c.c` and `d.c`: a weak undefined reference extracts no member | 63 | 63 |
| `zoo-defined` | `a.c`, `b.c`, `c.c` and the archive: the weak undefined function is defined | 223 | 223 |
| `p1-weak-alias` (5 tests) | a weak alias of a `static` function (musl's idiom) and a data alias, against a strong definition in either order, without one, and from a file without `main` | 144; 114 without the strong definition | the same |
| `p2-knr` | calls through declarations without a prototype across objects: an extra argument, a K&R `float` parameter | 23 | 23 (through objects with an adapter and the warning `function signature mismatch: f`) |
| `knr-two-arities` (M19) | a declaration without a prototype, `int f();`, called as `f(1, 2)` and, on a path that never runs, as `f(1)`, against the definition `int f(int a, int b)` in the other file | 3 | 3 (through objects the caller's object has both call signatures, and `f(1)` gets the missing-argument adapter and its warning) |
| `sig-mismatch` | a prototype `f(int, int)` against the definition `f(int)` | 41 | 41 (with an adapter and its warning) |
| `p3-start-stop` (M20) | `__start_myreg` and `__stop_myreg` over `used` records in the section `myreg` of two objects | Linux 15; Windows: link error | the same: on the Windows triples `undefined symbol: __start_myreg` and `__stop_myreg` (`p3-start-stop-windows`) |
| `p4-selectany` | `__declspec(selectany)` data in two objects (Windows triples) | 15 | 15 |
| `p5-tls` | a `_Thread_local` variable used across objects | 12 | `thread-local variable @tcount is not supported for MSIL yet` (phase 3; on the object path from `clang -c`) |
| `p6-ctors` | constructors with priorities in two objects | `BCDM` | `BCDM` |
| `p6-dtors` | destructors with priorities | no transcript (Windows faults at exit; the Linux oracle runs no `.fini_array`) | `global destructor @d150 is not supported for MSIL yet`, and the same for `@d120` (phase 3; on the object path from `clang -c`) |
| `p8-section-ctor` (M20) | a constructor registered by a pointer in `.init_array` (Linux) or `.CRT$XCU` (Windows, `#pragma section`) | 42 | 42, also through `clang -S` and ilasm |
| `ctor-slot-order` (M20) | a table entry and an `__attribute__((constructor))` function of equal key in one file, `main` in another | the entry's function, then the constructor | the same through objects and LTO |
| `p9-weak-common` (both orders) | a weak definition and a common symbol of one name: the common one wins | 1 | 1 |
| `common-largest` (M20) | tentative definitions of one name with different sizes in two files | the largest wins, with its alignment | the same |
| `unaligned-reloc` (M20) | pointers at unaligned offsets in packed initialized data, to the other file's variables (with addends), string literals and functions | the values through each pointer | the same |
| `int-ptr-adapter`, `missing-arg-adapter` (M20) | K&R calls that pass `0` for a pointer parameter, or one argument fewer than a definition that never reads it | 49; 35 | the same, each with an adapter and its warning |
| `float-int-mismatch` (M20) | a prototype `int half_of(float)` against the definition `int half_of(int)` | links, with undefined behaviour | `function signature mismatch: half_of` with the reason line (plan 5.9.8's error, on both pipelines at every level; below) |
| `weak-def-sig-mismatch` (M20) | a weak `int f(int)` called in its own file, and a strong `long long f(long long)` in the other, which wins | 22 | 22, through an adapter that converts the argument (`conv.i`) and the result (`conv.i4`), with its warning; with `double f(double)` instead, plan 5.9.8's error |
| `narrow-result-mismatch` (M20) | prototypes `int f(void)` and `int g(void)` against the definitions `unsigned char f(void)`, which returns 200, and `unsigned short g(void)`, which returns 50000, in the other file (a call that clears `eax` runs first, so the native caller reads no stale upper bits) | 200 and 50000 | the same, with no warning on either pipeline (`int32` on both sides in CIL, and the result zero-extended as the callee returns it) |
| `narrow-arg-mismatch` (M20) | prototypes `int ui(unsigned char)` and `long long ul(unsigned char)` and, on the Linux triples, `int si(signed char)` and `long long sl(signed char)`, called with 200 and -56, against definitions with `int` and `long long` parameters in the other file that return what they received | 200 and 200; on the Linux triples also -56 and -56 (`sl`'s result read through its low 32 bits, all that SysV defines; the Windows triples make no `signed char` call, since Win64 leaves a `char` argument's upper bits undefined) | the same on both pipelines: `ui` and `si` bind directly with no warning, `ul` and `sl` through plan 5.9.8's adapter (`conv.i`) with its warning; since M21 the test builds the program through objects and through LTO in every leg, on the leg's triple at `-O0` and `-O2`, checks each pipeline's warnings and requires the two runs to agree (MSILFixFunctionBitcasts widens a narrow argument by the call's extension attribute, as the object path's adapter does) |
| `type-mismatch` | a function and a variable of one name | links, with undefined behaviour | `symbol type mismatch: value` |
| `duplicate-strong` | two strong definitions | link error | `duplicate symbol: shared` |
| `undefined` | a function that nothing defines | link error | `undefined symbol: missing_fn` |
| `weak-undef-guarded` | functions and a variable declared weak and defined nowhere, used only when their address is not null | every address null, 4 | the same |
| `gc-used-retain` (M20) | `--gc-sections` against `used` and `retain` data that nothing references, and records found only through `__start_`/`__stop_` | the records and their sums | the same |
| `section-bracket` (M20) | the default `--gc-sections` link against entries that nothing references and that are neither `used` nor `retain`, reached only by pointer arithmetic from markers: markers around the entries of one named section in one file, and on the Windows triples an MSVC `$a`/`$m`/`$z` group over two files (`#pragma section`, `__declspec(allocate)`) | 82; on the Windows triples also 153 for the group | the same through objects (`obj` pipeline only: under `-flto` LTO drops the unreferenced entries before `msil-ld` sees them, as native LTO does, so the test is not run on the `lto` pipeline) |
| `pragma-comment-lib` (M15) | `#pragma comment(lib, "native")` in one of two files names a native library that the test builds (`native.dll` with its import library, or `libnative.so`); the link gets only its directory | a string from the library's data, 42, 20; exit status 12 | the same |
| `import-attrs` (M20) | against a test-built native library: `import_name` alone on a function whose C name differs from its entry and on a variable, and `import_module` with `import_name` on a variable | 17 | the same |
| `native-callback` (M15) | the C library's `qsort` with a comparator of the program | the sorted array | `native function 'qsort' (<module>) takes a C function pointer; passing functions to native code needs phase 3 thunks` (until P3-B), with the module `qsort` binds to: `api-ms-win-crt-utility-l1-1-0.dll` or `libc.so.6` |
| `p2c-bsymbolic` (M22) | a function `which` that both the program and its library define (Q21) | each calls its own (2 and 1), and the two addresses differ (Windows DLLs and the `-Bsymbolic` Linux library alike) | the same, with the warning `symbol 'which' is defined in <file> and exported by bsym.dll; the output uses its own definition` (`<file>` is the program's object under `obj`, `prog.exe.lto.o` under `lto`) |
| `p2c-cycle` (M22) | two libraries that call each other, point into each other's data and have a constructor each, linked through an interface library | both constructors ran before `main` and found the other library's static data complete (22 and 11); pointers and calls across the cycle work | the same |
| `p2c-weak-def` (M22) | a library's weak function and weak variable, both also defined strongly by the program, and a weak function that only the library defines | the program uses its definitions (2, 20), the library its weak ones (1, 10), and `plain(1)` binds to the library's (101) | the same, with the warning above for `hook` and `wvalue`; the symbol table flags the library's weak definitions |
| `initarray-args` (M22) | a function that takes `(int argc, char **argv, char **envp)` in an `.init_array.00099` slot, as Rust std's `ARGV_INIT_ARRAY` is, and a constructor that takes `argc` | on Linux both get argc 1, the slot's function `main`'s `argv` with a null `argv[argc]` and an `envp` that holds the run's `DOTNET_NOLOGO=1` | the same on the Linux triples, through objects and LTO; on the Windows triples, whose C runtime calls constructors without arguments, `global constructor 'count_args' takes arguments, which MSIL does not pass` |
| `fini-array` (M23, P3) | a function's address in a `used` slot of `.fini_array` (Linux triples), a destructor registered without `__attribute__((destructor))` | `main`'s line, then the entry's at exit (glibc; the E7 oracle's `_start` runs no `.fini_array`, so no `.expected` file records it) | `global destructors are not supported yet: p_fini in <file>` (the object, or the LTO object), until phase 3 |
| `crt-xt` (M23, P3) | a function's address in a `.CRT$XTU` slot (Windows triples, `#pragma section` and `__declspec(allocate)`), a terminator | `main`'s line, then the terminator's at exit (MSVC CRT) | `global destructors are not supported yet: p_term in <file>`, until phase 3 |
| `atexit-order` (M23, P3) | handlers registered with `atexit` in `main` | the handlers in the reverse order of registration | `undefined symbol: atexit` with `>>> referenced by <file>:(main)`, on both triples: glibc defines `atexit` only in `libc_nonshared.a` and the UCRT does not export it (phase 3 intercepts it) |
| `variadic-def-xtu` (M23, P3) | a variadic function defined in one object and called from another | 42 | through objects `clang -c` stops at the definition with `variadic function definitions are not supported for MSIL yet` (the caller alone gets `calls to variadic functions are not supported for MSIL yet`); through LTO `msil-ld` reports the definition; until phase 3 |

On the object path the diagnostics are `msil-ld`'s, whose `>>> referenced by
<file>:(<function>)` and `>>> defined in <file>` lines name the objects; the adapter warnings
and the signature error name the object of each side (`>>> defined as int32(int32) in p2b.o`,
`>>> called as int32(int32, int32) in p2a.o`). Under LTO, MSILFixFunctionBitcasts gives the
same warnings and errors without the file names, at every LTO level: `msil-ld`'s LTO applies
the call table before LTO optimization (at the post-link early extension points of the full and
ThinLTO pipelines, which only LTO invokes) as well as in code generation. So InstCombine never
rewrites a mismatched call across files before the table sees it (dropping extra arguments,
zero-filling missing ones, passing a `float` as the bits of an `int`), and code generation,
which then finds the calls adapted, reports nothing twice; `clang -S`, llc and the compile step
of `-flto` keep their behaviour, so a call inside one file that the compile step folds at `-O1`
and above links silently ("CIL object links and the image" below; `02-lto/intra-tu-mismatch`).
The adapter warnings of `p2-knr`, `sig-mismatch`, `knr-two-arities`, `missing-arg-adapter` and
`narrow-arg-mismatch` and the error of `float-int-mismatch` therefore come at LTO `-O2` as at
`-O0`, and the tests pin LTO's diagnostics, like the object path's, at `-O0` and `-O2`.

The plain multi-file cases of plan 5.9.16 are tests of `msil/test/M2/00-link/`
(`static-same-name`, `inline-comdat`, `byval-sret-xtu`, `knr-def-xtu`, `main-not-first`,
`data-only-tu`, with `two-tu`, `weak-strong`, `common`, `ctor-priority` and, since M20,
`reloc-loop` and `spill-limits`), and they print the native transcript, as do the archive tests
of `01-archive/`, the LTO tests of `02-lto/` and the native-library programs of `03-native/`.
Since M23 every row of plan 5.9.16 has a test: the rows of phase 3 (thread-local data,
destructors and destructor tables, `atexit`, functions passed to native code, a variadic
definition called from another object) pin the diagnostic that Phase 2 gives until phase 3
brings their transcripts; the library rows came with M22, and the programs of `04-shared/`
cover libraries further ("Libraries and assembly-to-assembly linking" below). Of `p7-flags`, M19
pins what the objects say (`llvm/test/CodeGen/MSIL/object-p7-flags.ll`: the call flags of
declarations without a prototype, with `sret` and with `byval` parameters, `dllimport`,
undefined data and the linker options), which `msil-ld` reads since M20.

### Triples (D6)

| Triple | clang | llc | Executed by the gate |
|---|---|---|---|
| `x86_64-pc-windows-msvc-msil` | yes | yes | Windows: .NET 8 (tiered and optimizing JIT), .NET 10, .NET Framework 4.8; programs that `msil-ld` links (M14) on .NET 8 and Framework 4.8, with their P/Invokes bound from the Windows SDK's and Visual C++'s import libraries (M15), and since M20 through CIL objects and `msil-ld`'s own image (the zoo also on .NET 10); since M22 library assemblies with the programs and libraries that link them, also on .NET 10 bound to the .NET 8 pack, against native DLLs; since M23 every test through CIL objects by default, also on .NET 10 and with `DOTNET_TieredCompilation=0` through `msil_also_run`, the nine exit programs and c-testsuite |
| `x86_64-unknown-linux-gnu-msil` | yes | yes | WSL Ubuntu: a private .NET 10 (tiered and optimizing JIT) against a native Linux build (the E7 oracle), also assembled by the Linux ilasm; programs that `msil-ld` links (M14), linked on Windows with the E7 sysroot's `libc.so.6` and `libm.so.6` as `--sysroot` (M15), and since M20 through CIL objects and `msil-ld`'s own image (`obj-wsl`; the zoo also on the private .NET 8); since M22 library assemblies against native `-Bsymbolic` shared objects, and constructors that take glibc's `argc`, `argv` and `envp`; since M23 every test of `msil/test/M1`, `M2` and the harness through CIL objects, also on the private .NET 8 and with `DOTNET_TieredCompilation=0` through `msil_also_run`, and the nine exit programs |
| `aarch64-*-msil` CIL objects and images | `-c` writes them (M19) | `-filetype=obj` | FileCheck only: `msil-ld` writes an image with the triple's machine (0xAA64; `lld/test/MSIL/obj-basic.test`), which the gate does not run |
| `x86_64-unknown-linux-musl-msil`, `x86_64-w64-windows-gnu-msil` | yes | yes | FileCheck and ilasm only |
| `aarch64-unknown-linux-gnu-msil`, `aarch64-unknown-linux-musl-msil`, `aarch64-pc-windows-msvc-msil`, `aarch64-w64-windows-gnu-msil` | yes (native AArch64 DataLayout and ABI) | yes | FileCheck and ilasm (`-pe64 -arm64`) only |
| `aarch64-apple-darwin*-msil` | `unknown target triple` (phase 4) | accepted, untested | no |
| `i686-*-msil`, `x86_64-*-gnux32-msil` | `unknown target triple` (i686: phase 5) | `unsupported target triple ... (Phase 1 supports x86_64 and aarch64 with 64-bit pointers)` | no |
| `x86_64-pc-win32-msil` (normalizes to `x86_64-pc-windows-msil`) | `unknown target triple`: Windows MSIL triples name `msvc` or `gnu` | accepted as windows-msvc | no |

clang uses the native target's ABI, data layout and predefines and adds `__MSIL__`; `long double`
is `double` on every MSIL triple, which differs from native on Linux and x86_64 MinGW (section 3).
Linux MSIL triples do not define `__ELF__` (section 6, item 5).

On the aarch64 triples clang classifies aggregates as AAPCS64 does natively. Since upstream
`de7794fcf781` (merged with `64f50ffd4f1c`), a non-HFA aggregate argument smaller than 8 bytes is
passed as an integer of its exact size (`i8` ... `i56`, formerly `i64`), so 1-4-byte structs are
`int32` IL parameters and 5-7-byte ones stay `native int`; results did not change. Both sides of
every call come from the same clang, no Phase-1 import takes an aggregate, and a native function
that takes or returns one is a link error (M15, section 2), so nothing can disagree.
`clang/test/CodeGen/MSIL/aarch64-aggregate-abi.c` and
`llvm/test/CodeGen/MSIL/struct-args-aapcs64.ll` pin these shapes on all four aarch64 triples and
assemble them with `ilasm -arm64`; execution stays x86_64-only (D6).

### C and LLVM IR

- Integers of every width: 32-bit values are `int32`; pointers and 64-bit integers are
  `native int` (D2; `int64` is never emitted). `__int128` arithmetic, shifts and compares are
  expanded inline, and since M12 so are `__int128` division and remainder by any divisor, the
  conversions between `__int128` and `float` or `double`, saturating ones included, and the
  signed and unsigned conversions from `__bf16` to `__int128` (of a `__bf16` read from memory or
  passed as a parameter; returning a `__bf16` still names a helper, section 3) (LLVM's
  ExpandIRInsts, as NVPTX and AMDGPU use it; plan section 12, M12 rows on Q36), so no
  compiler-rt helper is called. The expansion is large: a division or remainder by a variable
  is about 600-1,050 IL instructions per site (626-761 at `-O2` and 783-955 at `-O0` for a bare
  one; up to 1,053 in backlog recon 2.2's test functions), one by a constant that is not a
  power of two about 460-650 (exact division too, which no longer multiplies by the inverse),
  one by a power of two a few dozen, and a conversion to floating point 350-570. Phase 3 may
  replace it by one helper per assembly or by `Int128`/`UInt128` behind a switch (P3-B).
- `float` and `double`: arithmetic, remainder (`frem` is CIL `rem`), all comparisons NaN-exact,
  every integer conversion correctly rounded on every runtime (D17, and a round-to-odd step for
  int64 to float32), saturating conversions, `llvm.minimum`/`maximum`/`minimumnum`/`maximumnum`
  as compares and selects (`fmin` and `fmax` are math-library calls), and constrained (strict)
  operations as their plain forms. `fabs`, `copysign` and the `isnan` family use inline integer
  expansions.
- Control flow: branches, loops, `switch` (CIL `switch` for jump tables up to 4096 entries,
  compare chains otherwise; D35), computed `goto`, irreducible control flow, traps
  (`ldnull; throw`).
- Calls: direct calls, calls through function pointers (managed `calli`), K&R definitions and
  calls without a prototype (MSILAddMissingPrototypes), calls through a mismatched function type
  (MSILFixFunctionBitcasts wrappers), structs passed by value (copied by the caller) and returned
  through `sret`, on the SysV and Win64 ABIs (and the AArch64 ABIs on the aarch64 triples, FileCheck
  and ilasm only). The caller's copies share frame slots (M12): the K-th by-value struct argument
  of every call in a function is copied, before each call, into the function's K-th byval slot,
  which is as large and as aligned as the largest such argument, so ten calls of a 64-byte struct
  need 64 bytes of frame, not 640, and two by-value arguments of one call never share a slot.
  Only the SysV x86_64 triples pass structs `byval` (in memory); the Win64 and AArch64 ABIs pass
  large structs by reference to a copy that clang makes.
- Memory: loads and stores of every scalar width, `volatile` and unaligned accesses (`volatile.`
  and `unaligned.` prefixes), `memcpy`/`memset`/`memmove` (`cpblk`, `initblk`, and
  `System.Buffer::MemoryCopy` when the copy may overlap or the length is variable; D15). Static
  frames: one `localloc` per function for address-taken locals, local arrays and over-aligned
  objects (D13).
- Global variables: initialized and zero-initialized data, string literals, pointers to globals and
  functions in initializers (relocations), constructors (`__attribute__((constructor))`) in
  priority order, linkonce/weak/comdat definitions as ordinary ones (D12, D26). Everything lives
  in one unmanaged data segment that `<Module>::.cctor` allocates and fills. Since M20 the
  segment is laid out by `cli::layoutImage` from one segment per global, as a one-object link
  lays it out: variables in a named section (`__attribute__((section(...)))`,
  `__declspec(allocate(...))`) are grouped, `__start_`/`__stop_` symbols work on the Linux
  triples, and constructors registered in `.init_array` or `.CRT$XC*` run (section 2, "CIL
  object links and the image"). A module with more than 32 data relocations applies them in a
  loop over a relocation table instead of one store sequence each.
- Unused inline functions: a `linkonce` or `linkonce_odr` function definition that nothing uses
  outside its own body is dropped before it is compiled or diagnosed, and so are the local
  (`static`) functions and other such definitions that only dropped bodies used (M12). A header's
  inline functions that the program does not call therefore cost nothing: the UCRT `stdio.h`
  defines `sprintf` and its helpers inline, and at `-O0` clang emits them, with calls of
  functions that are not imports. Global variables are never dropped, and neither is a local
  function that nothing referenced in the first place.
- Function addresses: each function whose address is used gets one `'<msil>fn_<name>'` field that
  the module initializer sets with `ldftn`, so a function has a single address on every runtime
  (section 12, M9 row).
- Aliases (M12): `__attribute__((alias("f")))`, `static`, `weak` and array aliases, and the
  aliases that rustc makes of identical functions. Every use of an alias of a defined function,
  or of a global variable at any constant offset, becomes its aliasee, through chains of aliases
  and in code, initializers and the constructor list alike, so `&alias == &aliasee`.
  A function alias with external, `weak` or `weak_odr` linkage also gets a `public` forwarding
  method under its own name, which calls the aliasee (a plain `call`, never `jmp`, which the
  verifiers reject), so the name stays a method and `main` may be an alias. Other aliases (an
  offset into a function, a function-typed alias of a variable, an alias of an ifunc, a
  `thread_local` alias of a variable that is not thread-local, such as
  `extern __thread int a __attribute__((alias("g")))` on a plain `int g`) and ifuncs stay errors
  (section 3).
- Undefined weak symbols (M12): a `__attribute__((weak))` declaration that nothing defines is
  null, as a native static link makes it: `if (f)`, `&weak_var == 0` and initializers that take
  its address see null. A direct call of such a function runs a trap stub
  `'<msil>undef_<name>'` (`ldnull`, `throw`: an unhandled `System.NullReferenceException`,
  where a native call of address 0 crashes), and so does a constructor entry that names it. A
  weak declaration of a Phase-1 import (`puts`, a math function, an `"msil-import-module"`
  declaration) stays a P/Invoke, and so does, in `msil-ld`'s text, a weak declaration that a
  native library binds (M15); a weak thread-local declaration
  (`extern __thread int v __attribute__((weak))`) keeps the `thread_local` error (section 3).
- Aliases, undefined weak symbols, the dropped inline functions and the layout of named
  sections are rules for a whole program, which every `-S` and llc output is, and so is the
  `--lto-emit-asm` text of `msil-ld` (M14), which applies them after resolving the symbols of
  all inputs. CIL objects (M19) keep aliases and weak symbols as object symbols for `msil-ld`
  to resolve ("CIL objects" below; plan 5.8.10). In M12-M19 a section on a variable was an
  error of whole-program output (section 3).
- Fences: `atomic_thread_fence` calls `Interlocked.MemoryBarrier`; `atomic_signal_fence` and the
  empty barrier `asm volatile("" ::: "memory")` are compiler-only barriers (D36).
- Empty `asm` statements with operands (M12): the `DoNotOptimize` idioms of benchmark harnesses
  and Rust's `black_box`, such as `asm volatile("" : : "r"(v) : "memory")`, with inputs of any
  kind (`"r"`, `"m"`, `"r,m"`, `"g"`, `"i"`), read-write operands (`"+r"`, `"+m"`, `"+rm"`;
  `"+r"` on a `_Bool` and on a local register variable such as `register int x asm("eax")`
  included), outputs tied to an input (`"=r"(x) : "0"(y)`, also with alternatives, as `"=r,r"`
  with `"0,0"`) and memory outputs (`"=m"`). clang emits them exactly as for the native triple,
  and the backend turns each into the compiler barrier above plus a use of every input that
  keeps its computation alive (`llvm.fake.use`, for integer inputs of up to 64 bits, pointers,
  `float` and `double`). An `asm` with instructions, an output not tied to an input (`"=r"`,
  `"=rm"`), a condition-flag output (`"=@ccz"` on x86, `"=@cceq"` on AArch64; read-write
  `"+@ccz"` and tied ones too, because native code sets it from the flags after the statement),
  an output in memory tied to an input of another floating-point type (`"=rm"` on a `float`
  with a `double` `"0"`, which native x86 cannot compile either), `asm goto` and Microsoft
  `__asm` stay errors (section 3). Since M22 the backend also counts as empty a template that
  holds nothing but whitespace and complete `/* ... */` comments, operand references inside
  them included: Rust's optimization barriers write `asm!("/*{0}*/", inout(reg) x)`, IR
  `asm sideeffect "/*${0:k}*/", "=&r,0,..."` (serde_json's float formatter), which llc and
  `msil-ld`'s LTO now compile to the same IL as an empty template (the value passes through);
  any other text, an unterminated comment or an untied output keeps the error. clang's own
  check of C `asm` statements is unchanged: in C source only an empty template is accepted.
  Each accepted output holds what native x86 code leaves in it:
  - A register output tied to an input is that input: zero-extended for a `_Bool` `"+r"`, and
    converted between pointer and integer for a pointer tied to an integer of its width, as the
    native register holds it. A tie written with alternatives (`"=r,r"` with `"0,0"`) is read
    from the first alternative that has one.
  - An output that allows a register and memory, tied to an input (`"=rm"`, `"=g"` or `"=X"`
    with `"0"`, read-write `"+rm"`; also `"=r"` on a non-scalar such as `_Complex int`), which
    clang passes in memory, receives its input after the statement, as native code does when it
    takes the register. A narrower output gets the input's low bytes (a `char` output tied to an
    `int` input gets its low byte).
  - With alternatives that include memory (`"=r,m"` or `"=m,r"` with `"0,0"`, `"+r,m"`), native
    x86 takes the memory alternative, so the memory keeps its contents and the output its old
    value, as for a memory output tied to an input (`"=m"` with `"0"`). On the AArch64 triples
    `"Q"` is a memory alternative too: `"=r,Q"` with `"0,0"` keeps the memory's contents, as
    native AArch64 code does.
- Fixed-length generic vector types are scalarized by the legalizer (scalable vectors cannot be
  and are an error, section 3). `-g` is accepted, but no debug information is written.

### The standalone import table (D14)

`clang -S` and llc compile one module without a linker, so a function that is not defined in the
module can only be a P/Invoke from this table, the Phase-1 import table; any other undefined
function is a compile error. Every declaration is `public` and `pinvokeimpl("<library>" nomangle
cdecl) ... preservesig` (`nomangle` since M18) with a blittable scalar signature. The table is
used by `-S` and llc only, and it is frozen until the end of Phase 2 (D14 as amended): no entry
is added or removed. M15's gate requires the IL and diagnostics of every C and IR test input
that the previous commit tracks, on four triples, to equal those of that commit's build byte for
byte (`il-snapshot.py`, GettingStarted.md section 7); M18's gate requires the same up to the
shortest forms and `nomangle` (`--normalize forms`), and M19 and M20 repeat the check. The text
that `msil-ld` links never consults it: since M15 every function that no file defines binds to
the native libraries of the link ("Native libraries and P/Invokes" above), and the module flag
`msil-linked` tells the backend so. In M14 the table also decided what a linked program could
call, and a module that has M14's flag `msil-lto` without `msil-linked` still gets that
behaviour.

| Triple (x86_64 and aarch64) | `puts`, `putchar` | C math library |
|---|---|---|
| `*-linux-gnu-msil` | `libc` | `libm.so.6` (glibc keeps libm separate; CoreCLR maps only `libc`) |
| `*-linux-musl-msil` | `libc` | `libc` |
| `*-windows-msvc-msil` | `ucrtbase` | `ucrtbase`: every function below except `frexpf`, `ldexpf` and `hypotf`, which the UCRT implements inline in its headers (`frexpf` and `ldexpf` from `llvm.frexp`/`llvm.ldexp` are computed in double) |
| `*-windows-gnu-msil` | `msvcrt` | `msvcrt`: only `sqrt`, `floor`, `ceil`, `fmod`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `exp`, `log`, `log10`, `pow`, `ldexp`, `frexp`, `modf` and their `f` variants except `ldexpf` and `frexpf` (what `msvcrt.dll` exports) |

The math functions are `sqrt`, `cbrt`, `floor`, `ceil`, `trunc`, `round`, `rint`, `nearbyint`,
`fmod`, `remainder`, `fma`, `fmin`, `fmax`, `fdim`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`,
`atan2`, `sinh`, `cosh`, `tanh`, `exp`, `exp2`, `expm1`, `log`, `log2`, `log10`, `log1p`, `pow`,
`ldexp`, `frexp`, `modf`, `hypot` and their `f` variants. In LLVM IR, a function declaration with
the attribute `"msil-import-module"="<library>"` (and optionally `"msil-import-name"="<entry>"`)
is imported from that library.

Since M15 the float32 `llvm.ldexp` and `llvm.frexp` are computed in double when the target's
runtime library has no float32 version of the call (windows-msvc) or the triple is windows-gnu
(`msvcrt.dll` exports no `ldexpf` or `frexpf`), whatever this table says; the IL is the same as
before on every triple, and in `msil-ld`'s text the same rule decides whether `ldexpf` or `ldexp`
must bind.

In C, WebAssembly's import attributes write these IR attributes on the MSIL triples (M13):
`__attribute__((import_module("<library>")))` and `__attribute__((import_name("<entry>")))`, or
`[[clang::import_module("<library>")]]` and `[[clang::import_name("<entry>")]]`, on a function
declaration. `import_module` makes the declaration a P/Invoke of that library, and `import_name`
sets its entry point (default: the C name); `__has_attribute(import_module)` is 1. `import_name`
alone makes no other function an import in `-S` and `llc` output (the function is still
`not a Phase-1 import`), but on a Phase-1 import (`puts`, `putchar`, the math functions above)
it renames the entry point imported from that function's Phase-1 library: `cbrt` declared with
`import_name("my_cbrt")` becomes `pinvokeimpl("libm.so.6" as "my_cbrt" nomangle cdecl)` on
glibc. In a program that `msil-ld` links (M15), `import_module` is kept as given, unchecked, and
`import_name` alone makes `msil-ld` look the entry point up in the native libraries of the link
and take the module from the library that defines it. Bitcode symbol tables carry no import
attributes, so when two bitcode files declare one function with different attributes the
first declaration's attributes win, unchecked. CIL objects carry them (M19), so since M20 two
objects that declare one symbol with different ones stop the link with `import module mismatch
for symbol: <name>` or `import name mismatch for symbol: <name>`, followed by a `>>> defined as
<value> in <file>` line for each, as in wasm-ld. On an `extern` variable declaration they give
the variable the same attributes, which `msil-ld` imports as native data since M20 ("Native
libraries and P/Invokes" above; until then `importing native data symbol '<name>' (<module>)
is not supported yet`); an external variable is still an error in `-S` output. On a definition
they have no effect. For an attribute written on the definition itself clang warns `import module
cannot be applied to a function with a definition` (`import name ...`, `... a variable with a
definition`), and only when it generates IR, not with `-fsyntax-only`; an attribute that the
definition inherits from an earlier declaration is dropped without a diagnostic, as on
WebAssembly.

### The ECMA-335 library and the metadata oracles (M16, M17, M20)

`llvm/lib/CLI` (the LLVM component `CLI`, library `LLVMCLI`, namespace `llvm::cli`; plan 5.7) is
the one library that reads and writes ECMA-335 metadata for the Phase 2 tools: the backend's
binary CIL (M18) and CIL objects (M19), `msil-ld`'s output image (M20) and the tools
`llvm-cil-dump` and `llvm-cil-copy` (M17). M13 added its format headers
(`llvm/include/llvm/BinaryFormat/CLI.h`, `CLITables.def`, `CILOpcodes.def`), handles and the
metadata table schema; M16 added the writer half:

- **Blobs, signatures and custom attributes.** Compressed integers and a blob reader whose first
  error sticks; one signature model for types, methods, fields, properties and locals, which
  encodes, decodes (every error names its offset and cause), compares and hashes by content,
  remaps the TypeRefs it names, and prints the signature text of the canonical listing;
  custom-attribute blobs, with the `uint8[]` framing that carries a CIL object's link-info
  record (M17, M19).
- **IL.** The opcode table built from `CILOpcodes.def` (all 219 opcodes); an IL decoder and a
  token rewriter (`msil-ld`'s renumbering, M20); an IL builder that picks the shortest
  instruction forms, relaxes branches to a fixed point, computes `.maxstack` (with exception
  handlers and dead code) and rejects malformed bodies; and tiny and fat method bodies with
  small or fat exception sections.
- **Metadata.** The `#Strings`, `#Blob`, `#US` and `#GUID` heaps and `MetadataBuilder`, which
  numbers rows in the order later milestones rely on (plan 5.8.3, R6), groups members by their
  type, sorts the sorted tables, pads Field, MethodDef and Param past exactly 0xFFFF rows (a
  count that the runtimes reject), reports the CLR's limits as errors that name the entity
  (65,524 methods and 65,534 fields per type, names of at most 1,023 bytes, no NUL in a name, a
  method without a body), and writes the metadata root with bytes that depend only on the
  calls.

M17 completed it for Phase 2:

- **PE writer.** A PE32+ image (x86_64 and aarch64 machines) in the envelope of the C# compiler's
  images, as an executable, a library or a netmodule (a module without an Assembly row, the
  shape of a CIL object, M19): one `.text` section with the CLI header, the IL, the metadata,
  an optional debug directory and the mapped field data, and no import table, `.reloc` section
  or native entry stub. For CIL objects it adds marker sections and a COFF symbol table (plan
  5.8.6), so that symbol tools such as `llvm-nm` list them (M19). The MVID and the time
  stamp are a content ID, a SHA-256 hash of the whole file, so the same input always gives the
  same bytes.
- **Reader.** A PE image reader and a lazy, zero-copy metadata reader for all 45 tables, the
  heaps, method bodies, field data and the lookups that the linker will need (owners, nested
  types, custom attributes, P/Invoke maps, layouts, properties). Every index, offset and length
  of the input is checked: a malformed file is an error that names the problem, never a crash,
  and a large assembly such as `mscorlib.dll` is decoded only as far as it is used.
- **Link-info and symbol-table codecs.** The record that every CIL object carries in a custom
  attribute (M19; plan 5.8.8: symbols, segments, relocations, COMDAT groups, constructors,
  signatures and linker options, with its version rules and every consistency check), its
  validation against the object's metadata, the COFF symbol view built from it and its text
  form; and the symbol-table attribute of the libraries of M22.
- **Dumper.** Canonical listing v1 with the six normalizations of plan 5.12.6, and listings of
  the headers, of every table row and of the link-info record.

M20 added the link model that `msil-ld` and whole-program output share (section 2, "CIL object
links and the image"):

- **`ImageLayout.h`.** `cli::layoutImage`, the data image of plan 5.9.9 from the segments and
  common symbols of every input (named-section groups, priorities and `$` groups, the
  initialized and zero parts, shared C strings, the address slots of the relocation loop, and
  the errors for thread-local data and an image above 2 GiB), `cli::classifySection`, which
  sorts a section name into data, constructor table, destructor table, unsupported or
  thread-local under ELF or COFF rules, and the key and the stable order of the constructor
  list (`cli::getConstructorKey`, `cli::sortConstructors`).
- **`Trailer.h`.** `cli::buildTrailer`: `<Module>::.cctor` (the image copy, the zero part, the
  relocations as straight-line stores up to a threshold and through the table `'<msil>relocs'`
  and one loop above it, the constructors), the data-import helper and `'<msil>Main'`, built
  through a `cli::TrailerSink` that the AsmPrinter prints as text and `msil-ld` writes as
  metadata rows; below the threshold the methods are Phase 1's, instruction for instruction.
- **`CallAdapter.h`.** `cli::classifyCall`, the table of plan 5.9.8 over the IL value classes
  of a call and its definition, `cli::buildAdapter`, which builds an adapter's body, and
  `cli::printCallShape`, the `int32(int32, native int)` spelling of the diagnostics.

M16 and M17 changed no IL: the backend links the library from M18 on (its opcode table, IL
builder and signatures; "Binary CIL" below), `msil-ld` since M20. `CLITests` checks it in 21
suites (426 tests; the gates' floor is 210 since M20) whose expected bytes come from ECMA-335, the
recon's measurements and Python prototype, ilasm, and C# compiled in scratch: among them the IL
builder rebuilds ilasm's 371-byte `main` of the recon's sample module instruction by
instruction, the 0xFFFF padding at 0xFFFE, 0xFFFF and 0x10000 rows, every limit, R6 order, the
same bytes from two identical builds, content IDs checked against `System.Reflection.Metadata`,
a module's envelope against a netmodule of the C# compiler, a writer-reader round trip of every
table the writer supports, wide heap and table indexes, public key tokens, and every link-info
entry, flag, check and printed line. M20's three suites (`CLIImageLayoutTest`,
`CLITrailerTest`, `CLICallAdapterTest`, 73 tests written from the plan alone) check every rule
of the layout, rebuild Phase 1's layouts and module initializers of
`llvm/test/CodeGen/MSIL/module-initializer.ll` and `global-relocs.ll`, run the recorded
trailer methods in a small CIL interpreter over a simulated heap (every store against the
alignment it may assume, every site, field and imported address), and evaluate every adapter of
plan 5.9.8 in both directions with its `.maxstack`.

Two tools in `llvm/tools` use the library (GettingStarted.md section 6):

- `llvm-cil-dump` prints canonical listing v1 (`--canonical`, the default) with every option of
  `mdcanon`, the PE, CLI and metadata headers (`--headers`), every table row (`--tables`) and a
  CIL object's link-info record (`--link-info`). Its canonical listing is lit's `%msil_canon`
  from M17 on, and it must print the same bytes as `mdcanon` below: M17's gate compares the two
  on every C program of `msil/test/M1` at `-O0` and `-O2` and on its copy, with the default
  options and with every normalization on, and their statistics and full listings on the .NET
  Framework `mscorlib.dll` and .NET 8's `System.Runtime.dll` and `System.Private.CoreLib.dll`,
  the listings also on the `System.Runtime.dll` of the .NET 8 reference pack.
- `llvm-cil-copy` reads an IL-only image and writes it again with `MetadataBuilder` and the PE
  writer, renumbering every token: the end-to-end check of the library before `msil-ld` wrote
  images (M20). The copy takes its kind and machine from the input (a DLL with an entry point
  becomes an executable), has one row for duplicate TypeRef and MemberRef rows, and keeps a CIL
  object's link-info record but not its COFF symbol view; an input that `MetadataBuilder`
  cannot rebuild (a table outside its writer set, a class whose base type is a later row, a
  lone surrogate in a user string, events, ...) is a named error (GettingStarted.md section 6).
  The copy of every C program of `msil/test/M1` at `-O0` and `-O2` lists exactly
  like its ilasm image and is the same byte for byte when made twice, and the copies of the 20
  programs of `msil/test/M1/11-programs` at `-O2` pass the verification of section 4, get no
  PEVerify metadata message that the ilasm image does not get, survive the ildasm and ilasm
  round trip, and run with the expected transcript on .NET 8, five of them also on .NET 10,
  .NET Framework 4.8 and WSL .NET 10.

The C# tools in `msil/utils/mdtools/` (GettingStarted.md section 7) check the library and its
users from outside, over System.Reflection.Metadata:

- `mdcanon` prints *canonical listing v1* (plan 5.7.10): an assembly's metadata and IL as text
  without anything that depends on the image layout (tokens, heap offsets, RVAs, header forms,
  the MVID). It was lit's `%msil_canon` in M16; from M17 that is `llvm-cil-dump --canonical`,
  which prints the same bytes, and `mdcanon` stays the independent second implementation that
  `cli-corpus-check.py --mode dumpers` and `msil/test/harness/canon-agree.test` compare it
  with. The equivalence oracles of M19-M23 compare listings: an object against the ilasm build
  of its text (M19), `msil-ld`'s image against `-S` and ilasm (M20). Its six options of
  plan 5.12.6 remove exactly the differences those comparisons allow (the assembly hash
  algorithm, ilasm's parameter rows, ilasm's `InitLocals` on bodies without locals, ilasm's
  quieting of float32 signaling NaNs, the P/Invoke module names and access).
- `check-cil-defs.py` finds `CILOpcodes.def` equal to the 218 opcodes of
  `System.Reflection.Emit.OpCodes` plus `no.`, which that table lacks.
- `cli-corpus-check.py --mode ilasm` shows that the listing does not depend on the assembler:
  the 20 programs of `msil/test/M1/11-programs` at `-O0` and `-O2`, assembled by the Framework
  ilasm and by the NuGet ilasm 10.0.12, give 40 of 40 equal listings, although every pair of
  images differs.

### Binary CIL (M18)

From M18 the backend emits CIL instructions and nothing else: each instruction that the
AsmPrinter emits is one MC instruction of one CIL opcode, and the ILAsm printer and a binary
encoder both read it, so the text and the binary CIL of M19's objects are two encodings of one
stream (plan M18; how to see the encoding is in GettingStarted.md section 3).

- **One record per opcode.** `llvm/lib/Target/MSIL/MSILInstrCIL.td` holds one record
  `CIL_<Opcode>` for each of the 219 opcodes of `CILOpcodes.def`, with the opcode's value, its
  ILAsm text as the mnemonic and the operand its kind takes. The machine instructions of
  instruction selection keep their records but no longer reach the streamer: the lowering
  expands each into CIL instructions (a 64-bit constant into `ldc.i4` or `ldc.i8` and `conv.i`,
  a global address into `ldsfld` plus an offset and `add`, `memmove` into `conv.i8`, `dup` and
  a `call` of `Buffer::MemoryCopy`, a jump table into `switch` and `br`, a trap into `ldnull`
  and `throw`, memory flags into `unaligned.` and `volatile.` prefixes, ...). The module
  initializer `<Module>::.cctor` and `'<msil>Main'` are built with the library's IL builder, in
  Phase 1's instruction order and with Phase 1's `.maxstack` (since M20 by `cli::buildTrailer`
  of `llvm/lib/CLI`, which `msil-ld` uses too).
- **The shortest forms (plan Q26).** Phase 1 already printed the short forms of 32-bit
  constants and of argument and local indices. Since M18 branches use their short forms
  (`br.s`, `brtrue.s`, `blt.s`, ...) where the target is in reach, and 64-bit constants
  (`ldc.i4.m1` then `conv.i`), global address offsets and the `.cctor` and `'<msil>Main'`
  bodies use the shortest `ldc.i4` forms. The branch forms of a method are chosen once, before
  it is emitted: every branch starts short, and a branch whose displacement from the next
  instruction leaves -128..127 is widened to its long form, until nothing changes; `switch`
  entries stay 32-bit. `msil-ld` will never relax branches (plan 5.8), so the text and the
  bytes always agree.
- **`nomangle`.** Every P/Invoke, in `-S`, llc and `msil-ld`'s text alike, is
  `pinvokeimpl("<module>" [as "<entry>"] nomangle cdecl)`: ImplMap flags 0x0201, Design.md
  5.3's `ExactSpelling`, so the runtime binds exactly the entry name and never tries another
  spelling of it. The D14 table itself did not change.
- **The encoder.** `MSILMCCodeEmitter` encodes each instruction from the opcode table: one byte
  or `FE` and one byte, then the operand, little-endian, with floating-point constants as their
  exact bits (signaling NaNs included). A metadata token is four zero bytes and a fixup that
  names the operand's class (`fixup_cil_method_token`, `fixup_cil_field_token`,
  `fixup_cil_sig_token`, and the type, `tok` and string kinds for later milestones), which M19's
  object writer turns into a token; a branch is a PC-relative fixup on its label, and so is
  each `switch` entry. `llc -show-mc-encoding` prints the bytes and fixups of every instruction
  as comments, and the text with those comments still assembles with ilasm while no instruction
  has more than 191 fixups: `MCAsmStreamer` prints fixup i as `char('A' + i)`, so entry 191 of
  a `switch` (the 192nd fixup) prints as a NUL byte, later ones also as LF (201) and CR (204),
  and ilasm cannot assemble the text of a `switch` of more than 191 targets (jump tables have up
  to 4096 entries, D35); plain `-S` text is not affected. Since M19 the CIL object writer
  writes these bytes into CIL objects and fills each token from the object's own metadata
  rows ("CIL objects" below).
- **Operand classes.** Each token operand records what it names, from the IR alone: a function
  defined in the module, or a declaration with `import_module` or `import_name` (plan 5.10.7);
  any other function, library calls included; the address field `'<msil>fn_<name>'` of a
  function; a global variable defined in the module (`common` ones included), or another one;
  the signature of a `calli`; a member of the .NET base class library. They print as Phase 1's
  text (`int32 'f'(int32)`, `native int 'g'`, `native int '<msil>fn_f'`, `void
  [mscorlib]System.Threading.Interlocked::MemoryBarrier()`), and llc's `-M object-form` prints
  the spelling of M19's CIL objects instead (plan 5.8.4): `int32
  [msil.extern]'<msil>Symbols'::'f'(int32)` for an undefined function, `native int
  [msil.extern]'<msil>Symbols'::'x'` for an undefined variable, `native int
  [msil.extern]'<msil>FnAddr'::'f'` for a function address, and the scope `[msil.bcl]` for a
  BCL member. A function or variable defined in the module prints alike in both.
- **The BCL list.** `llvm/include/llvm/CLI/BCLMembers.def` is the one list of base class
  library types and members that generated code uses (plan 5.11.7): `System.ValueType` (the
  base of the data image's value type) and the five members of D10 with their owner types,
  `Marshal::AllocHGlobal`, `Marshal::StringToHGlobalAnsi`, `Environment::GetCommandLineArgs`,
  `Buffer::MemoryCopy` and `Interlocked::MemoryBarrier`, all required and all in `mscorlib`.
  Every BCL name the backend and `msil-ld` print or write comes from it, and M20-M22 add the
  members they need: M20 added `Marshal::StringToHGlobalUni` and the constructor
  `EntryPointNotFoundException(string)` with its type, for the data-import helper, and
  `System.Object`, the base type of spill classes, all in `mscorlib` too. M21's reference
  binder binds them by it, and M21 added `System.Runtime.InteropServices.NativeLibrary` with
  `Load(string)` and `GetExport(native int, string)`, which the data-import helper uses when
  the references define them; these three are not mscorlib-safe, so the built-in table of
  links without references leaves them out (section 2, "Reference assemblies and the base
  class library"). New entries go after the existing ones, so no id changes.
- **The encoder equals ilasm.** `cli-corpus-check.py --mode encoder` (the `encoder-parity` leg,
  GettingStarted.md section 7) compiles each of the 99 C programs of `msil/test/M1` at `-O0`
  and `-O2` with `llc -show-mc-encoding`, assembles the same text with the .NET Framework
  ilasm, rebuilds each method's bytes from the encoding comments and compares them with
  ilasm's: 2,819 of 2,819 method bodies of the 198 modules are equal, with the same
  `.maxstack`, and each of their 24,099 token fixups names the member that ilasm's token names;
  their 23,316 branch and `switch` fixups resolve to ilasm's displacements. A float32 signaling
  NaN constant, which ilasm quiets (section 3, "Limits"), counts apart as `snan-quiet`; the
  corpus has none. A `switch` of more than 191 targets (see "The encoder") is found in the
  listing before it is parsed or assembled and fails its program and level with a message that
  names the `switch` and its target count, instead of ilasm's generic failure; the corpus's
  largest `switch` has 68 targets.
- **The text did not change otherwise.** The IL snapshot of M18's build, compared with that of
  M17's with `il-snapshot.py --normalize forms` (GettingStarted.md section 7), has the same IL
  and diagnostics for all 2,712 compiles of the 452 inputs of `msil/test/M1` and
  `llvm/test/CodeGen/MSIL` on four triples; 1,861 of the `.il` files differ only in the
  shortest forms and `nomangle`. The FileCheck tests of `llvm/test/CodeGen/MSIL`,
  `clang/test/CodeGen/MSIL` and `lld/test/MSIL` and the `msil-basic` test of
  `update_llc_test_checks` (83 files) changed only to follow those forms (rewritten, also
  inside FileCheck's regular expressions, and the checks that pinned the absence of a short
  form or `nomangle` deleted) and in comments, which `check-il-forms-diff.py` checks.

### CIL objects (M19)

How to write and read them, with examples, is in GettingStarted.md section 3 ("CIL objects");
plan 5.8 is the contract.

- **What writes them.** `clang -c` and clang-cl `/c` (one `cc1 -emit-obj` job: no assembler,
  no bitcode, no link; `<name>.o`, with clang-cl `<name>.obj`), `clang -cc1 -emit-obj` and
  `llc -filetype=obj`, on every MSIL triple of D6: x86_64 and aarch64, Linux, windows-msvc and
  windows-gnu. `-flto -c`, `-flto=thin -c` and `-emit-llvm -c` still write bitcode.
- **What an object is** (plan 5.8.2-5.8.7). An ECMA-335 netmodule in the envelope of the C#
  compiler's images: a PE32+ image for the triple's machine (0x8664 or 0xAA64) with
  characteristics 0x2022 and one `.text` section that holds the CLI header, the method bodies
  and the metadata. It has no entry point, no Assembly row, no `.cctor`, no `'<msil>Main'`, no
  data image and no FieldRVA row, so it cannot run, and nothing in it runs when it is loaded.
  The Module row is named after the base name of the source file (`main.c`; `<msil>module`
  when the module has none), never after `-o`. Each function the file defines is a static
  method of `<Module>` with M18's binary CIL (`public`, or `assembly` for a `static`
  function; bodies never set `InitLocals`), and each variable it defines is a `static
  initonly native int` address field that the linker will set. A function or variable that
  the file does not define is a member of `[msil.extern]'<msil>Symbols'`, called or loaded
  with the call's own signature; the address of a function is a field of
  `[msil.extern]'<msil>FnAddr'`, so a function keeps one address in the whole program; and
  the types of the base class library sit in the unbound scope `[msil.bcl]`, which `msil-ld`
  binds (M21). An undefined function declared with `import_module` gets one P/Invoke stub on
  that module per signature it is called with (`import_name` alone: on the placeholder module
  `msil.unresolved`, for `msil-ld` to bind natively under the import name), and one with its
  declared signature when the object only takes its address. Rows come in a fixed order (plan
  5.8.3, R6), the one ilasm gives the same object written as text, so both number every token
  alike.
- **The link-info record v1** (plan 5.8.8) sits in a custom attribute of the Module row and
  says what the symbol table, sections and relocations of a native object say: the triple, the
  ABI version and the data model (the sizes of `void *`, `long`, `wchar_t` and `long double`:
  8,8,4,8 on the Linux triples, 8,4,2,8 on the Windows ones); one symbol per global, in a fixed
  order (variables, functions, aliases, then the library calls of code generation), with its
  kind, binding (GLOBAL, WEAK, LOCAL or COMMON), visibility, `dllimport` and `dllexport`, its
  `llvm.used`, alias and import flags and its metadata row; one data segment per variable the
  file defines, named by its `section` or else `.data`, `.rodata` or `.bss`, with its bytes,
  alignment and flags (C strings, read-only, zero, kept by `llvm.used`); the data relocations
  (the address of a variable or of a function in 64 or 32 bits, and the difference of two
  addresses); COMDAT groups; the constructors of `llvm.global_ctors` in array order with their
  priorities; what a call's IL signature does not say (no prototype, an `sret` result, `byval`
  parameters with their sizes); and the linker options of `#pragma comment(lib)` and
  `!llvm.dependent-libraries`. `llvm-cil-dump --link-info` prints it, one line per entry. M19
  wrote version 1.0 without freezing it. M20 added one call flag, `NATIVE_INCOMPATIBLE` (0x8),
  on the entry of a definition or call whose result or parameters are not native scalars (a
  struct returned in registers, an `__int128`, a vector, a `_Float16`), so that `msil-ld` can
  refuse to bind such a function natively, and M20's commit froze version 1.0, after `msil-ld`
  had linked every M20 row of the link-semantics matrix with it. From then on a field never
  changes its meaning or encoding within major version 1; a minor version may add kinds, flag
  bits and enum values, which an older `msil-ld` reports as `invalid link-info record:
  v1.<minor> uses <feature>, which this toolchain does not support` where it must understand
  them, and `msil-ld` mixes objects of every minor version of major 1 (plan 5.8.9).
- **The COFF symbol view** (plan 5.8.6). A COFF symbol table at the end of the file, one
  symbol per record symbol in record order, and three empty marker sections `.data`, `.rdata`
  and `.bss` let symbol tools read an object like a native one: `llvm-nm` prints `T` for a
  function (aliases included), `D`, `R` or `B` for a variable in a writable, read-only or zero
  segment, `C` for a common symbol and `U` for an undefined one, weak or not (never `W`), in
  lower case for a local symbol; `llvm-ar rcs` indexes the defined global symbols and the
  commons; GNU `nm` and `ar` agree (in WSL); `llvm-objdump -t` and `llvm-readobj --symbols`
  list the symbols. `msil-ld` never reads the view, and a copy by ilasm or `llvm-cil-copy`
  drops it.
- **What object mode changes** (plan 5.8.10). Whole-program output (`-S`, llc text and
  `msil-ld`'s `--lto-emit-asm` text) is the same as before byte for byte; in an object the
  rules that need the whole program become symbols for `msil-ld`:

  | C or IR | In a CIL object | In `-S` and llc text |
  |---|---|---|
  | a function the file does not define (another file's, `malloc`, a library call such as `memcpy`, a `__` helper) | an UNDEFINED symbol, called through `'<msil>Symbols'` | an error unless the D14 table imports it |
  | an `extern` variable the file does not define | an UNDEFINED data symbol; with `import_module` or `import_name`, IMPORT with both names | `external global variable ... (phase 2 linker)` |
  | a `weak` (or `linkonce`) definition, an MSVC string literal, `__declspec(selectany)` data | WEAK, in its COMDAT group if it has one (selection `any` or `nodeduplicate`) | an ordinary definition |
  | a `weak` declaration | UNDEFINED WEAK | null, and a trap stub for calls (M12) |
  | a `common` variable | COMMON, with its size and alignment | a zero-initialized definition |
  | an alias of a definition (of a function at offset 0, of a variable at a constant offset) | an ALIAS symbol with its own linkage and visibility | the aliasee, plus a forwarding method (M12) |
  | `section` on a variable | the name of its segment | since M20 laid out as a one-object link lays it out (an error in M12-M19) |
  | `llvm.used` | NO_STRIP, and its segment is kept (RETAIN) | nothing |
  | constructors | INIT entries in array order, with their priorities | calls in `<Module>::.cctor`, by priority |
  | `#pragma comment(lib)`, `!llvm.dependent-libraries` | LINKER_OPTIONS | ignored |
  | visibility, `dllimport`, `dllexport` | the symbol's flags | ignored |
  | a call of a declaration without a prototype (`int f();`) | one MemberRef per IL signature of its calls, each marked NO_PROTOTYPE, and no wrapper | Phase 1's prototype from the calls, and wrappers |

  Phase 1's two prototype passes stop at undefined functions in object mode: a declaration
  without a prototype keeps its type and attribute, and no call of an undefined function gets
  a wrapper, so a K&R function called as `f(1, 2)` and as `f(1)` has two MemberRefs, and
  `msil-ld` checks each call against the definition (M20). Phase 1 derived one prototype
  from the first call and could lose an argument (`knr-two-arities` in the matrix above). Two
  calls with one IL signature that pass their arguments differently (a `byval` struct or an
  `sret` result in one, a plain pointer in the other) give the warning `calls to '<f>' pass
  incompatible arguments; the object records the first call's layout`. Thread-local
  variables, `llvm.global_dtors`, ifuncs, module `asm` and variadic functions stay the errors
  of section 3. Object mode adds three errors: `alias '<a>' must
  point to a definition plus a constant offset for MSIL`, `comdat '<c>' uses selection kind
  '<k>', which MSIL does not support` (`largest`, `exactmatch`, `samesize`), and `symbol
  '<name>' starts with '<msil>', which MSIL reserves for the names it generates`.
- **Object-form text.** `clang -S -mllvm -msil-object-form` (llc: `-msil-object-form`)
  writes the object as ILAsm text instead of the runnable module: a header comment, the three
  placeholder `.assembly extern` lines, the methods with the operand spelling of `-M
  object-form` (M18), the fields and stubs, then `.module` and the record as a `.custom` blob.
  ilasm turns it into an object whose canonical listing (`llvm-cil-dump --canonical
  --params=drop --initlocals=ignore-without-locals --snan=quiet`, which ignores ilasm's
  parameter rows, its `InitLocals` on bodies without locals and its quieting of float32
  signaling NaNs) and record equal those of `clang -c`'s object; only the COFF view is
  missing. That comparison is the tests' oracle; `msil-ld` never reads the text.
- **Determinism.** The MVID and the time stamp are a hash of the file, and an object depends
  only on the IR and the options, so two compiles in different directories with different
  `-o` names write the same bytes.
- **Linking them.** `msil-ld` links CIL objects since M20 (next subsection), and a
  compile-and-link command compiles to them; until then it rejected them, `<file>: linking CIL
  object files and .NET assemblies is not supported yet`, so a compile-and-link command
  compiled to bitcode and a program of several files was compiled with `-flto -c`. An object
  that went through ildasm and ilasm is not guaranteed to link (plan 5.8.4). Native librarians
  and linkers refuse CIL objects as they refuse other netmodules: `lib.exe` with `LNK1302: only
  support linking safe .netmodules; unable to link pure .netmodule`, `llvm-lib` with `not a
  COFF object, bitcode, archive, import library or resource file`, and `lld-link` and `ld.lld`
  with `unknown file type`. `llvm-ar` and GNU `ar` archive them.
- **Reading them.** `llvm-cil-dump` (`--headers`, `--tables`, `--canonical`, `--link-info`),
  both ildasms, System.Reflection.Metadata (`mdcanon`), and the symbol tools above.
- **Tests.** The 12 `object-*.ll` tests of `llvm/test/CodeGen/MSIL` write objects with `llc
  -filetype=obj` and check them through `llvm-cil-dump`: the envelope (`object-envelope.ll`),
  every symbol row and the data model (`object-symbols.ll`), aliases, data segments and
  relocations, COMDAT groups, constructors, calls (the K&R case in both call orders), import
  stubs, the errors of object mode, the object-form text, the COFF view with `llvm-nm` and
  `llvm-ar`, and the matrix case `p7-flags`; most of them also compare the object with ilasm's
  object of its object-form text. They require the lit feature `llvm-cil-dump`, which the
  directory's `lit.local.cfg` sets when the build has that tool, so a tree without it reports
  them UNSUPPORTED (under a gate's `msil_require_verify=1` it is a configuration error).
  `clang/test/Driver/msil-object.c` checks `-c` and `/c`, their jobs and errors, that two
  compiles from different directories give the same bytes, and the `llvm-nm` letters and
  object-form text of what they write; `clang/test/CodeGen/MSIL/emit-obj.c`, which requires
  `llvm-cil-dump` too, checks `cc1 -emit-obj` and the headers and records of the objects that
  `-c` and `/c` write; and `msil/test/M2/08-semantics/knr-two-arities.test` ran through LTO
  until M20, which added its object row. The non-lit legs `obj-text` and `obj-text-wsl`
  (GettingStarted.md section 7) check every C program of `msil/test/M1` at `-O0` and `-O2`, and
  the programs of `11-programs` for the Linux triple too: the object of `clang -c` must list
  exactly like ilasm's object of its object-form text, record included, and have the same
  `--link-info` lines; two compiles from different directories must give the same bytes;
  `mdcanon` and both ildasms must open it; its envelope must be the one above; the consistency
  rules C1-C5 and C7 of plan 5.8.5 must hold between its metadata and its record, and between
  ilasm's object and the same record; and `llvm-nm`, the `llvm-ar` index and, in WSL, GNU `nm`
  and `ar` must list the symbols the record gives.

### CIL object links and the image (M20)

How to link CIL objects and read the map of an image, with examples, is in GettingStarted.md
section 3 ("Programs of several files"); plan 5.9 is the contract.

- **No ilasm.** `msil-ld` links CIL objects, archives of them, LLVM bitcode and native
  libraries in one link and writes the program's assembly itself: an IL-only PE32+ executable
  for the triple's machine in the envelope of the C# compiler's images (one `.text` section, no
  native code, characteristics 0x0022, a 4 MB stack reserve), whose entry point is
  `'<msil>Main'`. It is an EXE image whatever `-o` says (a name that ends in neither `.exe` nor
  `.dll` gets the warning `<path>: the output name ends in neither .exe nor .dll; msil-ld writes
  an EXE image`), so the one file runs under `dotnet exec` and directly on .NET Framework 4.8;
  since M22 `-shared` writes a DLL image instead (below).
  The Assembly row is named after the output's stem (or `--assembly-name`, `-soname`), with
  version 0.0.0.0 (since M22 `--assembly-version`), the hash algorithm SHA1 (0x8004) and no
  public key, and the Module row after
  the output file's name; the MVID and the time stamp are a hash of the image, so the same
  inputs give the same bytes (the `det` leg compares the objects, archives and images of two
  builds). clang's compile-and-link command compiles each source with `cc1 -emit-obj` and links
  the objects (`-flto` keeps bitcode and LTO). `--lto-emit-asm`, the ILAsm text of an LTO link,
  is optional and takes bitcode only: `<file>: --lto-emit-asm cannot link CIL objects; link
  without --lto-emit-asm`, `ThinLTO and --lto-partitions cannot write ILAsm text; link without
  --lto-emit-asm`.
- **Inputs (plan 5.9.4).** Each object is read with the ECMA-335 reader, and its link-info
  record is decoded and checked against its metadata (the consistency rules of plan 5.8.5); a
  file that breaks them is a named error (`<file>: malformed CIL object: <rule>: <detail>`,
  `<file>: not a CIL object (no link-info record)`, `<file>: invalid link-info record: ...`,
  `malformed method body in '<m>' at IL offset <n>`), never a crash. Every input must have the
  link's triple, toolchain ABI version and data model (`<file>: data model 8,8,2,8 differs from
  8,8,4,8 in <f0>`), the objects that LTO writes included, which is how a bitcode file compiled
  with another `wchar_t` size is found. Archive members are lazy, as for bitcode. A .NET
  assembly is, since M21, a reference assembly ("Reference assemblies and the base class
  library" below) and, since M22, a library input when this toolchain built it ("Libraries and
  assembly-to-assembly linking" below); in M20 it was `<file>: .NET assembly inputs are not
  supported yet`.
- **Symbols and tokens (plan 5.9.5, 5.9.7).** Object symbols resolve by M14's port of lld/ELF's
  rules: strong, weak and common definitions (common symbols of one name become the largest,
  with the largest alignment), aliases (of a function, or of a variable plus an offset), weak
  undefined references, and COMDAT groups, whose losing copies are discarded (a reference to a
  symbol of a discarded copy is `relocation refers to a symbol in a discarded section: <s>`).
  A LOCAL (`static`) symbol stays in its object, and a LOCAL method or field whose name another
  member already has is renamed `'<name><msil><n>'`. `msil-ld` decodes every method body and
  maps every token of it to a row of the output: definitions of any object, placeholders of
  `[msil.extern]` and import stubs bound to their definitions or P/Invokes, function addresses
  to one `'<msil>fn_<name>'` field per function, BCL members, local signatures, the classes of
  exception clauses and user strings. Bodies are copied byte for byte with only their tokens
  rewritten, so code size, branches, `.maxstack` and exception clauses never change.
- **Calls whose signatures differ (plan Q8, 5.9.8).** Each call that `msil-ld` binds by name to
  a definition (a call of `'<msil>Symbols'`, an import stub resolved to a definition, and the
  calls inside a losing weak or COMDAT copy, which now reach the winner) is compared with the
  definition through `cli::classifyCall`. Extra trailing arguments, missing ones (passed as
  zero), an `int32` where the definition takes a `native int` or the reverse (`conv.i`,
  `conv.i4`), and a result that one side has and the other does not, get an adapter
  `'<msil>adapt_<name>_<n>'` (one per definition, call signature and call flags) and a
  warning; integer against floating point, `float` against `double`, an `sret` result on one
  side only and differing `byval` parameters are errors, and a variadic side is `variadic
  functions are not supported yet: <f>`. The texts are wasm-ld's:

  ```text
  msil-ld: warning: function signature mismatch: add
  >>> defined as int32(int32, int32) in callee.o
  >>> called as int32(int32, int32, int32) in caller.o
  >>> adapter: drops 1 extra argument
  msil-ld: error: function signature mismatch: half
  >>> defined as int32(int32) in fcallee.o
  >>> called as float32(float32) in fcaller.o
  >>> argument 1 is float32 at the call but int32 in the definition (different native register classes)
  ```

  Calls through function pointers are not checked, as natively. Under LTO,
  MSILFixFunctionBitcasts applies the same table when `msil-ld` sets the module flag
  `msil-lto`, with the same warnings and errors without the ` in <file>` parts, for every
  mismatch across files at every LTO level: it runs before LTO optimization (the post-link
  early extension points) and again in code generation (plan 5.9.8 and D30 as amended for M20).
  Inside one file the compile step of `-flto` keeps its pre-link pipeline, so at `-O1` and above
  it may already fold a mismatched call (InstCombine when the argument has the size of the
  parameter, then the inliner), as `clang -c -O2` does on the object path, where `msil-ld` never
  sees a call inside one object; such a call links without a diagnostic on both pipelines. A
  file compiled at `-O0`, and a mismatch that the optimizer cannot fold (an `int` argument for
  a `long long` or pointer-sized parameter), get the warning or error exactly once at every LTO
  level (`msil/test/M2/02-lto/intra-tu-mismatch.test`). `clang -S` and `-c` keep Phase 1's
  silent wrappers inside one file. A direct call of an undefined weak function runs a trap stub
  `'<msil>undef_<name>'` (`ldnull`, `throw`), one per call signature.
- **The data image (plan 5.9.9).** `cli::layoutImage` lays out one data image for the program
  from the objects' segments, one per variable: `.data`, `.rodata` and `.bss` segments one by
  one, and every other section name as a group placed where its first segment is (on the Linux
  triples `.init_array.<N>` and `.fini_array.<N>` join their section, ordered by priority; on
  the Windows triples the part of the name before `$` names the group and the full names order
  its members, as in lld-link); initialized data first, then zero data and common symbols in
  input order; equal C-string segments share one copy. A program without named sections and
  common symbols gets Phase 1's layout. A difference of two addresses is folded into the image
  (`symbol difference '<a>' - '<b>' in <file> does not fit in a <w>-byte value` when it does
  not fit); the image is at most 2 GiB (`the data image of <n> bytes is larger than MSIL
  supports (2 GiB)`).
- **Named sections and constructor tables.** On the Linux triples an undefined
  `__start_<sec>` or `__stop_<sec>` is the address of the start or end of the output section
  `<sec>` when its name is a C identifier, as in ld.lld. On the Windows triples they stay
  undefined (`undefined symbol: __start_myreg`), as with link.exe and lld-link. The 8-byte
  entries of `.init_array*` (Linux) and `.CRT$XC*` (Windows) sections are constructors that run
  together with those of `__attribute__((constructor))`, in a native link's order: by priority
  (Linux) or section name (Windows; a constructor's priority gives the name clang's COFF
  lowering gives it), then in link order, and within one object its table entries before its
  constructors of equal key, which is what ld.lld and lld-link give a `clang -c` object. An
  entry that is not a function address is `constructor table entry at <section>+<off> in
  <file> is not a function address`. Since M22 a constructor on the Linux triples may take
  glibc's `(int argc, char **argv, char **envp)` or a prefix of it, and gets them ("Rust
  bitcode" below); any other constructor that takes arguments is `global constructor '<c>'
  takes arguments, which MSIL does not pass`. Since M21 a null entry (zero bytes, no
  relocation) of a
  `.CRT$XC*` table on the Windows triples is skipped, as the MSVC CRT's `_initterm` skips it,
  in `msil-ld`'s image and in whole-program text alike (`cli::skipsNullConstructorSlots`); on
  the Linux triples a null `.init_array` entry stays that error, because glibc calls every
  entry (in M20 it was the error on both). Since M23 a null function in `llvm.global_ctors`
  itself (IR such as `[@a, null, @b]`, or an element that is `zeroinitializer` as a whole) ends
  the list on every path, as LLVM's AsmPrinter ends it for every target: through objects and the
  LTO image `<Module>::.cctor` calls `a` only, as the `-S`, llc and `--lto-emit-asm` texts
  always did for a null function, where M22's object writer skipped the null entry and also
  called `b` (on a `zeroinitializer` element the text paths hit an assertion before M23);
  an entry that MSILPrepareIR replaced after its diagnostic is
  still skipped, and modules without a null entry are unchanged (`obj-ctors-null.test`). Since
  M21, too, a hidden or protected data
  reference to `__start_<sec>` or `__stop_<sec>` resolves alike in a link of CIL objects, an
  LTO link and `--lto-emit-asm` text, which in M20 alone reported `undefined hidden symbol`
  for it; when no input has the section, all three report `undefined hidden symbol:
  __start_<sec>`. Destructor tables (`.fini_array*`, `.CRT$XT*`) and
  destructors, `.ctors`/`.dtors`, `.preinit_array*` (Linux; ld.so runs it before every
  `.init_array` entry, a phase MSIL does not have yet) and the other `.CRT$X*` sections, and
  thread-local data, are named errors (destructors and thread-local data until phase 3), never
  dropped silently: `global destructors are not supported yet: <s> in <file>`, `section
  '<name>' in <file> is not supported` (for example `section '.preinit_array' in <file> is not
  supported`), `thread-local storage is not supported yet: <s> in <file>`.
- **`--gc-sections` (plan 5.9.11 as amended in section 12)**, the default, drops the methods
  and data segments that nothing live reaches. The roots are the entry point, the `-u`
  symbols, `used` and `retain` data (NO_STRIP symbols and RETAIN segments), every constructor
  and destructor, every segment of a section that `cli::classifySection` puts in the
  constructor-table, destructor-table or unsupported class (the image runs the first and
  reports the others; thread-local data is no such root), every segment of a section that a
  referenced `__start_`/`__stop_` symbol names (lld/ELF's `-z nostart-stop-gc`), and, on the
  Windows triples, every segment of an explicit section (any name but the default `.data`,
  `.rodata`, `.bss`, `.tdata` and `.tbss`) that belongs to no COMDAT group, as lld-link's
  `/OPT:REF` discards only COMDAT sections. Marking follows the tokens of live methods, the
  relocations of live segments and aliases, and a live segment of an explicit section keeps
  live every segment of its object with the same section name and COMDAT group (or none), as
  ld.lld keeps a whole input section (a CIL object has one segment per variable, a native
  object one input section per section name and group). So entries reached only by pointer
  arithmetic from a live marker in their section stay, as in a native link (`section-bracket`
  in the matrix above). Under `-flto`, LTO internalizes the entries that nothing references and
  drops them before `msil-ld` marks anything, as native LTO does (clang-cl `-flto` with
  lld-link gives the same result). An undefined symbol that only dead code references is not
  reported, as in lld. `--print-gc-sections` prints `removing unused section <file>:(<name>)`
  for each method and segment it drops, and `--no-gc-sections` keeps everything (the
  equivalence oracles link that way).
- **The module initializer (plan 5.9.10).** `<Module>::.cctor` comes from `cli::buildTrailer`,
  which `-S` uses too. It allocates and fills the data image and applies the data relocations:
  up to 32 of them (`--msil-reloc-loop-threshold=<n>`, hidden, for tests) as Phase 1's
  straight-line stores, and above that through a table `'<msil>relocs'` in the image's RVA data
  and one loop over it (unaligned stores, 4-byte sites, function addresses through 8-byte
  slots of the image), so a large initialized table costs a fixed amount of IL instead of
  several instructions per pointer. Native data imports run first, then the relocations, then the
  constructors. Since M22 a link with library inputs runs an `ldtoken` per library and the
  cross-assembly relocations between the relocations and the constructors, and a `-shared`
  library splits the steps over two initializers ("Libraries and assembly-to-assembly linking"
  below).
- **Calls of native functions (plan 5.10.11, 5.10.13, 5.10.14).** A function that binds to a
  native library becomes a P/Invoke stub, one per symbol and IL signature, `assembly static
  pinvokeimpl("<module>" as "<entry>" nomangle cdecl) ... preservesig` with the module string
  exactly as M15 finds it. The checks that M15 made on the merged module after LTO run in
  `msil-ld` for every object, LTO's in-memory objects included, with `>>> referenced by` lines,
  and report: a function of
  the callback list (`qsort`, `signal`, `pthread_create`, ...; explicit `import_module`
  exempt), a variadic call or definition, a value that a P/Invoke cannot pass natively (an
  `sret` result, a `byval` parameter, the record's NATIVE_INCOMPATIBLE flag, the native `long
  double` functions on Linux and MinGW), and a prototype-less function used only by address
  (`the signature of prototype-less function '<f>' cannot be derived: its address is used, but
  it is never called directly; declare it with a prototype`), each with M15's text.
  `--print-imports` lists the P/Invokes of the image.
- **Per-type limits (plan 5.9.12).** The runtimes load at most 65,524 methods and 65,534 fields
  per type. Past that (or past `--msil-type-member-limit=<n>`, hidden, for tests) the members
  move, in row order, from `<Module>` into classes `'<msil>Spill1'`, `'<msil>Spill2'`, ...
  (`private abstract sealed auto ansi`, extending `[mscorlib]System.Object`); `<Module>` keeps
  `.cctor`, `'<msil>Main'` and the image fields, the address fields that move lose `initonly`
  (`.cctor` stores them from outside their class), and the `'<msil>fn_'` fields become
  `assembly` instead of `private`. A generated program of 65,600 functions and 65,600
  variables at `-O0` (two spill classes) links in about 3 seconds and runs on .NET 8, .NET 10
  and Framework 4.8; `msil/test/M2/00-link/spill-limits.test` runs one in the `net8` leg
  (`msil_scale_tests=1`; the `obj` leg before M23). Since M21 its program is two files of 32,800 functions and 32,800
  variables each, because one CIL object cannot pass the per-type limits (section 3,
  "Limits"); M20's single file did not compile with `clang -c`.
- **The base class library.** Objects name BCL types in the unbound scope `[msil.bcl]`. In
  M20 `msil-ld` bound them to `[mscorlib]` 4.0.0.0 with the public key token
  `b77a5c561934e089`, as `-S` names them (D10), so the image runs on .NET 8, 10 and Framework
  4.8 alike, and a reference to any other assembly was `<file>: reference to assembly '<n>'
  needs --reference, which this msil-ld does not support yet`. Since M21 the reference binder
  of the next subsection binds them; a link without references still writes exactly M20's
  rows.
- **LTO (plan 5.9.13).** LTO writes in-memory CIL objects, which `msil-ld` links like any other,
  so ThinLTO bitcode (`-flto=thin`), `--lto-partitions=<n>` and a link that mixes bitcode with
  CIL objects (also in one archive) work, and `--save-temps` also keeps the objects
  (`<output>.lto.o`, `<output>.lto.<n>.o` for further partitions, `<output>.lto.<stem>.o` for
  ThinLTO). The constructor rule above applies to an LTO object as to a `clang -c` object, so
  a program of one file gives the order of a native link without LTO.
- **The map (plan 5.9.14).** For an image, `-Map` adds after `Inputs:` and `Symbols:` (which
  now list the objects and their definitions) the sections `Image:` (the layout: each output
  section, its inputs' segments and their symbols with offset, size and alignment),
  `Methods:` and `Fields:` (each row's token, owner type, input and symbol, and each field's
  image offset) and `Imports:` (each P/Invoke's token, module and entry).
- **Whole-program output (plan Q5, D26 as amended).** `clang -S`, llc and the `--lto-emit-asm`
  text are a one-object link of their module through the same `cli::layoutImage` and
  `cli::buildTrailer`: named sections, `__start_`/`__stop_` symbols (on the Windows triples
  D26's error stays) and constructor-table entries work there too, the texts above name the
  source file, and M12's `global variable @<name> in section "<section>" is not supported for
  MSIL yet (phase 2 CIL objects)` is gone. A module without named sections keeps its text byte
  for byte while it has at most 32 data relocations (`-msil-reloc-loop-threshold`): M20's gate
  compares the IL and diagnostics of every C and IR input of `msil/test/M1` and
  `llvm/test/CodeGen/MSIL` with those of M19's build at an unreachable threshold. Five programs
  of `msil/test/M1`, with 36 to 47 relocations, use the loop at the default threshold:
  `07-globals/reloc-global-ptrs.c` and `string-table.c`, `08-fnptr/dispatch-table.c`, and
  `11-programs/dispatch-vm.c` and `string-table.c`. Whole-program output keeps two Phase 1
  rules where a link differs: it shares no equal C strings, and an initializer that mixes zeros
  and `undef` (`{ i32 0, i32 undef }`) is initialized data there but zero data in an object.
- **The equivalence oracles (plan 5.12.6).** O3: the image of a one-object link with
  `--no-gc-sections` lists exactly like ilasm's image of the same file's `-S` text, allowing
  only the hash algorithm, ilasm's parameter rows, ilasm's `InitLocals` on bodies without
  locals, ilasm's quieted float32 signaling NaNs and the P/Invoke modules and access, which D14
  and M15 decide differently. O4: the image of an LTO link lists exactly like ilasm's image of
  the same link's `--lto-emit-asm` text, with the first four of those allowances. O6: PEVerify
  `/md` reports no metadata message for the image that it does not report for ilasm's.
  `msil/utils/equiv-allowed.txt` lists the allowances and the tests whose difference none of
  them removes (`skip <oracle>[@<triple>][:<level>] <test path> <reason>` lines,
  msil/test/README.md; a line that names a triple or a level covers the test only for that
  triple or level), and the legs with `msil_equiv=1` (since M23 `net8`, `wsl-net10`,
  `refs-net48`, `lto` and `programs-lto`; before, the `obj` legs), `07-equiv/` and the harness
  self-tests of `msil/test/harness/equiv/` run them. Equal `unnamed_addr` C strings are one such difference,
  since `msil-ld` shares them inside one object too while `-S` keeps one copy of each (plan
  5.9.9 rule 6, clarified in section 12 on 2026-10-25), so `11-programs/crc32.c`, whose `-O2`
  build on the Windows triple holds two equal strings, skips O3 there only (`skip
  O3@x86_64-pc-windows-msvc-msil:O2`); at `-O0` and on the Linux triple both sides are equal
  and O3 runs. Since M21 a `skip O6` line exists too, which `msil-verify.py --md-baseline
  --equiv-skip <file> --test <path>` honours as the O3 and O4 lines are honoured:
  `07-globals/alias.c` and `weak-undefined.c` skip O6, because the image has, by plan 5.8.10,
  the address fields of the object's ALIAS symbols and the zero fields of its UNDEFINED WEAK
  ones, which the `-S` build lacks, and on a host where PEVerify `/md` reports every
  `<Module>` field (`0x8013129D`) those fields' messages have no counterpart in the baseline
  (M20's post-commit `obj` leg failed both at `-O0`). Each line covers its own oracle only,
  and O6 runs unchanged for every other test. The ildasm and ilasm round trip of `msil-ld`'s
  images (O2, `msil_pipeline=obj msil_roundtrip=1`) accepts, since M21, one difference, line
  for line: a float32 signaling NaN `ldc.r4` operand that ilasm quieted (`(01 00 80 7F)` back
  as `(01 00 C0 7F)`, `07-equiv/o3-single.test`), which the image keeps exact. Since M22 it
  also accepts, for a library, the top-level class block that a `.custom` of the `.assembly`
  block names (the `'<msil>SymbolTable'` attribute class of every `-shared` output) at another
  position: ilasm defines a TypeDef at its first reference, and the `.assembly` block precedes
  every class, so the class comes back first. The block's own lines and every other line must
  still be equal, and an EXE, whose `.assembly` block names no class, round-trips strictly
  (plan 5.12.6; `prog/roundtrip-moved-class.test` of the harness).
- **Tests.** The 25 `obj-*.test` of M20 in `lld/test/MSIL` link CIL objects from `llc
  -filetype=obj` and read the image with `llvm-cil-dump`: rows, entry points and names, every
  token class, resolution in both orders, common symbols, COMDAT groups, archives, every
  signature text, weak and undefined references, sections and constructors on both triples,
  `--gc-sections`, the relocation loop, spill classes, the map, the triple and data-model
  checks, linker options, P/Invokes and their checks, determinism, LTO and data imports.
  `msil/test/M2` adds the object rows of the matrix above, `00-link/reloc-loop` and
  `spill-limits`, the LTO images of `02-lto/` (`lto-pe`, `thinlto`, `lto-partitions`,
  `mixed-objects-bitcode`) and its signature checks under LTO (`intra-tu-mismatch` for
  mismatches inside one file; `lto-call-attrs`, Linux triples, for calls of one definition
  with one IR call type, each decided by its own `signext`, `zeroext` or `byval` attribute in
  both link orders at `-O0` and `-O2`; `lto-same-type-mismatch` for `sret` and `byval`
  mismatches whose IR call type equals the definition's, which stop the link with plan 5.9.8's
  errors, each once, at `-O0` and `-O2`, as on the object path, the `byval` case on Linux
  triples only), the data imports of `03-native/`, `06-det/det-obj` and `det-archive`, and the
  explicit O3 and O4 checks of `07-equiv/`. The zoo (two `clang -c` objects) exits 63 on .NET 8,
  .NET 10, Framework 4.8 and WSL .NET 8 and 10, with no ilasm.

### Reference assemblies and the base class library (M21)

How to link against reference assemblies, with examples, is in GettingStarted.md section 3
("Reference assemblies"); plan 5.11 is the contract.

- **What binds.** `msil-ld` binds every type and member of the .NET base class library that its
  output uses (the BCL requests of the objects, `[msil.bcl]`, and of `msil-ld`'s own
  synthesized code: the module initializer, `'<msil>Main'`, the data-import helper, the base
  types of the data image and of spill classes) by searching the link's reference assemblies,
  as the C# compiler does. A TypeRef of an object scoped to any assembly other than the
  placeholders `msil.extern` and `msil.link` binds by its type name the same way (M20's `needs
  --reference` error is gone). There is no target-framework option: the references alone
  decide what the output references (D10 as amended for Phase 2).
- **Inputs (plan 5.11.1).** `--reference=<path>` or `--reference <path>` (repeatable; no short
  form, because lld's `-r` is `--relocatable` and `-R` is `--rpath`) names an assembly or a
  directory, whose `*.dll` files with CLI metadata count, not recursively; its other files are
  skipped silently, while a file named explicitly that is no .NET assembly is `<path>: not a
  .NET assembly`. A positional .NET assembly input (a PE image with CLI metadata and an
  Assembly row, such as a `.dll` of a pack) is a reference assembly too, in command-line
  order, unless it carries this toolchain's symbol-table attribute: then it is
  a library input, which the driver hands to `createManagedLibraryFile` ("Libraries and
  assembly-to-assembly linking" below; in M21 it was `<path>: linking .NET libraries built by
  this toolchain is not supported yet`); such a library given with
  `--reference` is skipped with the warning `<path> is a library built by this toolchain; pass
  it as a link input instead`. A .NET assembly inside an archive (a reference assembly or a
  toolchain library) is neither a link input nor a reference: it gives the warning
  `<archive>(<member>): .NET assemblies in archives are not linked; ignored`, with or without
  `--whole-archive`, and the link goes on without it. clang needs no option of its own:
  `-Wl,--reference=<path>` and positional `.dll` inputs reach `msil-ld` as they are. Every
  reference is read through the driver, so `--reproduce` keeps it.
- **The reference set (5.11.2).** Each assembly's identity is its Assembly row (name, version,
  culture) and the token of its public key; diagnostics print it as `<name> <a.b.c.d>
  [<token>|null]`, such as `System.Runtime 8.0.0.0 [b03f5f7f11d50a3a]`. The same path twice
  counts once. Two files with the same identity are one reference when their bytes are the
  same: the later one is skipped with the warning `<path> has the same identity as <path>
  (<identity>); using the first` (either gives the same output). When their contents differ
  they are the error `references <path> and <path> have the same identity (<identity>) but
  different contents`, the two paths in the order of their normalized paths, so that neither
  the outcome nor the text depends on the order of the references: the .NET Framework's
  `v4.8/mscorlib.dll` and the .NET 8 pack's `mscorlib.dll` facade are both `mscorlib 4.0.0.0
  [b77a5c561934e089]`. The same simple name with another version or key is an error (no
  unification, as in Roslyn). The set is indexed lazily: the reader,
  the Assembly row and the names of the public TypeDefs and of the ExportedType forwarders,
  and the member tables only of the types that bind, so a whole `msil-ld` run against the
  .NET 8 pack (163 assemblies) takes about 0.1 s.
- **The core library (5.11.3)** is the unique reference that defines a top-level
  `System.Object` without a base type and references no other assembly (Roslyn's rule). Every
  link with references needs exactly one, because every image has a module initializer and
  `'<msil>Main'`: none is `the references contain no core library (an assembly that defines
  System.Object and references no other assembly); given: <list>`. The list names, in the
  order of their identities (never the order of the command line), each reference that
  forwards or defines `System.Object`, with a note (`(facade, forwards System.Object to
  System.Runtime)`, `(defines System.Object but references mscorlib)`, `(defines System.Object
  with a base type)`); when none does, every reference without a note; and `no references`
  when the `--reference` options added no assembly at all, as for an empty directory, a
  directory of native DLLs or of toolchain libraries only, or a pack's version directory
  (`.../Microsoft.NETCore.App.Ref/8.0.<n>`, which holds no `.dll` itself) instead of its
  `ref/net8.0`. Two are `the references mix two core libraries: <identity> (<path>) and
  <identity> (<path>)`, as for the .NET Framework's `mscorlib.dll` next to a .NET pack's
  `System.Runtime.dll`, and three or more are `the references mix <n> core libraries:
  <list>`, `<n>` in digits and `<list>` each candidate as `<identity> (<path>)` in the order
  of their identities, joined by `, ` with ` and ` before the last. A facade never qualifies:
  it references the assemblies it forwards to.
- **Types (5.11.4)** bind to their definition only: the one public TypeDef of that full name
  (a nested type when every enclosing type is public or nested-public, through its enclosing
  TypeRef; generic types by their arity name, such as ``Gen`1``). Two definitions are `type '<Ns.Type>'
  is defined in two references: <identity> (<path>) and <identity> (<path>)`, in either
  reference order, and three or more `type '<Ns.Type>' is defined in <n> references: <list>`,
  with the list of the core-library error above. A forwarder is followed inside the set to
  the definition, and the AssemblyRef then names the defining assembly with the version of
  its own Assembly row, never
  the facade with its AssemblyRef's version (0.0.0.0 in the .NET 8 pack); a destination that
  is missing is `type '<Ns.Type>' is forwarded by <identity> to <assembly>, which is not among
  the references`, and a name nothing defines or forwards `type '<Ns.Type>' is not defined in
  the references (<n> assemblies)`. A definition in a `System.Private.*` assembly that is not
  a reference assembly (the shared runtime's `System.Private.CoreLib.dll`) binds with the
  warning `<path> is not a reference assembly; binding '<Ns.Type>' to <identity> ties the
  output to one runtime build`, once per assembly.
- **Members (5.11.4)** match by name, then calling convention (static or instance, vararg,
  generic), generic arity, parameter count, return type and parameter types; only public ones
  count. A method other than a constructor that the requested type lacks is searched for in
  its base types, nearest first, up to a type without a base type, and the MemberRef keeps
  the requested type as its parent (plan 5.11.4), where Roslyn would name the declaring type.
  Constructors are not inherited (ECMA-335 II.10.5.1): `.ctor`, `.cctor` and every attribute
  constructor are looked up on the requested type only. Fields, like constructors, are looked
  up on the requested type only, because the runtime does not resolve a field MemberRef
  through base types: `ldfld` or `ldsfld` that names a derived type for a field of its base
  type throws `MissingFieldException` on .NET Framework 4.8, .NET 8 and .NET 10. For a
  constructor or a field the undefined-member error below then lists only that type's own
  constructors or fields of that name. The runtime resolves a method MemberRef from its
  parent upwards to the first method of that name and signature, custom modifiers included,
  whatever its accessibility, and checks access afterwards (`MethodAccessException`), so a
  non-public method of the requested type, or of a type in between, with the public base
  match's name and signature, custom modifiers included, makes the request the
  undefined-member error, whose list names that method with ` (not public)`; one that
  differs from the match only in a custom modifier does not stop the binding. Every base type that a method request's search needs binds by
  the type rules above (a field request never binds a base type): one that the references do
  not define, define twice or forward to an assembly that is not among them gives that type's
  own error (`type '<Ns.Type>' is not defined in the references (<n> assemblies)`, `... is
  defined in two references: ...`, `... is forwarded by <identity> to <assembly>, which is
  not among the references`), whatever the reference order; for an optional request (below)
  a missing or forwarded-to-missing base type falls back as a missing member does, and two
  definitions are an error whatever the strength.
  Custom modifiers are ignored for matching and copied from the definition
  into the MemberRef, because a call without the definition's `modreq` is a
  `MissingMethodException` at run time; two members that differ only in modifiers are the
  error `.NET member '<signature>' (requested by <file>[ for <switch>]) matches several
  members of '<Ns.Type>' in <identity> (<path>) when custom modifiers are ignored: <list>`. A
  missing member is `undefined .NET member '<signature>' (requested by <file>[ for
  <switch>]); '<Ns.Type>' is defined in <identity> (<path>) but has no such member; overloads
  present: <list>` (the members of that name, joined by `; `, each with ` (not public)` when
  it is not public, or `none`). The request's `<signature>` and each member of the list print
  the calling convention before the return type, as ILAsm does (`[instance ][explicit
  ][vararg ]`), and a generic method's arity after its name (`M<1>`), so a request and a
  definition that differ only in their convention read differently: a request of a default
  `void Fake.G::V()` against a `static vararg void V()` gives `overloads present: vararg void
  V()`, and the arguments of a vararg request follow the sentinel (`vararg void
  Fake.G::W(..., int32)`). A member marked
  `[Obsolete]` binds with the warning `'<member>' is obsolete in <identity>`, and one that
  requires preview features (`[RequiresPreviewFeatures]` on the member, its type or its
  assembly) is the error `'<member>' requires preview features`. Every type of a bound
  signature binds by the same rules, transitively. Diagnostics of the binder print reference
  paths with `/` separators.
- **Emission (5.11.6).** One AssemblyRef per defining assembly that binds something, in the
  order of first use: name, version and culture of its Assembly row, Flags 0, the 8-byte
  public key token, no hash. Nothing is emitted for a reference that binds nothing, rows are
  deduplicated, and `System.Type` values in custom-attribute blobs are rewritten as the
  assembly-qualified name of the definition (`System.Threading.Interlocked, System.Threading,
  Version=8.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a`). The output depends
  only on the set of references, never on their order or on whether they come positionally
  or by `--reference`, file or directory (the images are equal byte for byte).
- **What the packs give** (the program of the recon's `p1prog.c`, which uses every BCL member
  of Phase 1):

  | References | AssemblyRefs | Runs on |
  |---|---|---|
  | none (built-in table, below) | `mscorlib` 4.0.0.0 `b77a5c561934e089` | .NET 8, .NET 10, .NET Framework 4.8, WSL |
  | the .NET Framework 4.8 reference assemblies (`Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8`, or the NuGet package `Microsoft.NETFramework.ReferenceAssemblies.net48`) | the same row: the canonical listing equals the link's without references | the same |
  | the .NET 8 pack (`<dotnet>/packs/Microsoft.NETCore.App.Ref/8.0.<n>/ref/net8.0`) | `System.Runtime`, `System.Runtime.InteropServices` (`Marshal`) and `System.Threading` (`Interlocked`) 8.0.0.0 `b03f5f7f11d50a3a`, in first-use order | .NET 8, .NET 10 and WSL; the .NET Framework stops with `System.IO.FileNotFoundException` for `System.Runtime` 8.0.0.0 |
  | the .NET 10 pack (`.../10.0.<n>/ref/net10.0`) | the same three at 10.0.0.0 | .NET 10; .NET 8 stops with `System.MissingMethodException` (`Entry point not found in assembly ...`, not a `FileNotFoundException`: the .NET 8 host cannot load the .NET 10 references of `<Module>` and reports the entry point missing), the .NET Framework with `System.IO.FileNotFoundException` |

  With the scopes stripped (`llvm-cil-dump --scopes=strip`), the listing of an image bound to
  the .NET 8 or 10 pack equals that of the link without references.
- **Without references (5.11.7, Q10)** `msil-ld` binds through its built-in `[mscorlib]`
  table, exactly when no `--reference` option and no positional reference `.dll` was given,
  whatever they contain. A `--reference` that adds no assembly (an empty directory, a
  directory of native DLLs or of toolchain libraries only, a pack's version directory instead
  of its `ref/net<N>.0`) turns the table off too: such a link fails with the core-library
  error above, `... given: no references`, and a `--print-bcl-binding` request reports that
  error instead of the `pass --reference` text below; it never binds to `mscorlib` without a
  word. It holds
  the mscorlib-safe entries of `llvm/include/llvm/CLI/BCLMembers.def`, bound to `mscorlib`
  4.0.0.0 `b77a5c561934e089` with flags 0, and its rows are exactly those M20 wrote, so every
  link without references gives M20's image byte for byte and O3 keeps holding. `msil-ld
  --print-bcl-table` prints its 20 entries in `.def` order (M21's 14:
  `type [mscorlib]System.ValueType`,
  the owner types `Marshal`, `Environment`, `Buffer` and `Interlocked`, Phase 1's five members
  `Marshal::AllocHGlobal(native int)`, `Marshal::StringToHGlobalAnsi(string)`,
  `Environment::GetCommandLineArgs()`, `Buffer::MemoryCopy(void*, void*, int64, int64)` and
  `Interlocked::MemoryBarrier()`, then M20's `Marshal::StringToHGlobalUni(string)`, the type
  `System.EntryPointNotFoundException` and its constructor `(string)`, and the type
  `System.Object`; then M22's six for the symbol-table attribute of libraries: the types
  `System.Attribute`, `System.AttributeUsageAttribute` and the value type
  `System.AttributeTargets`, `Attribute::.ctor()`, `AttributeUsageAttribute::.ctor(valuetype
  System.AttributeTargets)` and `AttributeUsageAttribute::set_AllowMultiple(bool)`) and exits.
  Anything else is `.NET member '<signature>' is not in msil-ld's
  built-in mscorlib table; pass --reference=<reference assemblies>`.
  `msil/utils/check-mscorlib-safe.py` proves every entry by running code: an ILAsm probe that
  takes the token of each entry through `[mscorlib]` must exit 42 on .NET Framework 4.8,
  .NET 8, .NET 10 and WSL .NET 10, and a failing entry is named with its exception
  (`TypeLoadException`, `MissingMethodException`); an entry joins the table only with a passing
  run. M21's three new entries, `NativeLibrary` with `NativeLibrary::Load(string)` and
  `GetExport(native int, string)`, are not mscorlib-safe and never in the table.
- **Text is never bound (5.11.8).** `clang -S`, llc and `--lto-emit-asm` text keep their
  `[mscorlib]` scopes and the one `.assembly extern mscorlib`, whatever the references; with
  `--lto-emit-asm`, `msil-ld` still reads the references and applies the identity and
  core-library checks, so a bad set fails there too, and binds nothing. Its native data
  imports therefore always use M20's helper (`kernel32.dll`, `dlopen`/`dlsym`), even with
  references that define `NativeLibrary`: a Linux link with references whose libraries define
  no `dlopen`/`dlsym` stops with `data symbol '<name>' in <module> cannot be imported:
  --lto-emit-asm text is never bound to the references, and the native libraries define no
  dlopen/dlsym` (without references the text of "Native libraries and P/Invokes" above), while
  the object and LTO PE links of the same inputs call `NativeLibrary`. An LTO link that
  writes the image binds as a link of CIL objects does. CIL objects and link-info record v1 do
  not change.
- **Request strengths (5.11.5, Q11).** Every request in Phase 2 is required: a member that the
  references lack is an error. The binder also carries the two optional strengths that phase
  3's codegen switches will use, *drop* (an optimization-only attribute is left out) and
  *substitute* (the call goes to a helper symbol instead), each reported under `--verbose` as
  `note: '<member>' is not defined by the references; <omitted|using helper '<symbol>'>
  (requested by <file> for <switch>)`. Phase 2 has no producer of them (record v1 keeps
  MANAGED_REQUESTS reserved), so they are tested through the hidden option
  `--print-bcl-binding=<request>` (`type:<Ns>.<Name>[/<Nested>]`,
  `member:<BCLMemberId>[:drop|:substitute]`, `typename:<name>`, and `attr:(<params>):<hex>`,
  which rewrites the `System.Type` values of the custom-attribute blob `<hex>` for a
  constructor `instance void .ctor(<params>)`, a rewriting that no Phase 2 input reaches),
  which prints `<request> -> <result>` lines (for `attr:` the rewritten blob in uppercase hex)
  and exits. With references that add no assembly a request prints the core-library error.
- **Native data through `NativeLibrary`** ("Native libraries and P/Invokes" above): the
  data-import helper uses `NativeLibrary.Load` and `GetExport` exactly when the references
  define both, so a program bound to the .NET 8 or 10 pack imports native variables without
  `kernel32.dll` or `dlopen`, on .NET 8, .NET 10 and WSL.
- **Not supported in objects yet:** a MemberRef whose parent is not a TypeRef, and a MethodSpec
  (a generic method instantiation, which C does not produce): `<file>: member reference
  '<name>' has a parent other than a type reference, which is not supported yet`, `<file>:
  token 0x<8X> names a generic method instantiation, which is not supported yet`.
- **Executable names (M23).** The image is an EXE image whatever its name, and the driver's
  default output stays `a.exe`, but a program bound to .NET's core library cannot start on
  the .NET Framework, and .NET names its executables `.dll`. So a link without `-shared` whose
  output name ends in `.exe` (in any case) and whose core library is `System.Runtime` (a .NET
  reference pack), `System.Private.CoreLib` (a runtime directory) or `netstandard` 2.1 or later
  warns `<path>: this executable is bound to <core library> <version>, so it runs only on .NET
  through 'dotnet' and cannot start on .NET Framework; .NET executables are named .dll (for
  example -o <stem>.dll)`, with `<stem>` the output path without `.exe`. A link without
  references (the built-in `[mscorlib]` table), with the .NET Framework reference assemblies or
  with `netstandard` 2.0 or earlier, whose images run on both, a `.dll` name and a `-shared`
  link do not warn. Images bound to `netstandard` 2.1 run only on .NET: the .NET Framework 4.8
  ships only `netstandard` 2.0.0.0 and stops such an `.exe` with a `FileNotFoundException` for
  `netstandard` 2.1.0.0 (`output-name-netcore.test` has a fake `netstandard` of each version).
  `msil-ld` still writes neither a runtimeconfig.json nor the native apphost `<name>.exe` that
  the .NET SDK adds: a program bound to a .NET pack and named `prog.dll` runs as `dotnet
  prog.dll` with the `prog.runtimeconfig.json` that the build system writes
  (`msil/test/M2/05-refs/netcore-dll-name`, which runs it that way on Windows and through the
  harness on both triples).
- **Verification.** PEVerify cannot load an image bound to .NET 8 or later reference
  assemblies (it stops with `The system cannot find the file specified` after metadata
  errors), so the `refs-net8` and `refs-net10` legs verify such images with ILVerify against
  the pack they were bound to and skip PEVerify, saying so (`msil-verify.py --skip-peverify
  "bound to .NET <n> references"`), as does a lit run whose `msil_references` path names a
  .NET pack (msil/test/README.md); the same programs get PEVerify in every other leg (section
  4).
- **Tests.** The 17 `bcl-*.test` of `lld/test/MSIL` bind CIL objects of `llc -filetype=obj`
  against synthetic reference assemblies, `Inputs/fakepack/` (15 ilasm sources, among them a
  core library made with ilasm's `.mscorlib` directive, minimal core libraries, a facade with
  nested forwarders, a fake `mscorlib`, a core library with an AssemblyRef, a base class in
  another assembly, a library with the toolchain's symbol table and hand-written objects with
  the field requests that llc never writes, from which `gen.py` builds 54 images, variants
  included, committed as `yaml2obj` YAML and checked against ilasm's images; no binary is
  committed): the options and the reference set, .NET assemblies in archives included
  (`bcl-reference-option`), identities, the same identity with the same and with other bytes
  included (`bcl-identity`), the core-library rule, three core libraries and references that
  add no assembly included (`bcl-corlib`), types (three definitions included), forwarders,
  members with their calling conventions in the texts and the base-type search (constructors,
  a nearer non-public member, and base types that are missing or defined twice included),
  fields, looked up on the requested type only (a base type's public field never binds, and
  the requested type's non-public fields of that name are listed), and modifiers
  (`bcl-types`, `bcl-forwarders`, `bcl-members`, `bcl-fields`, `bcl-modifiers`), the rows and
  their order (`bcl-emission`, `bcl-order`), the built-in table (`bcl-builtin`), request
  strengths (`bcl-strength`), `System.Type` names (`bcl-typename`) and their rewriting in
  attribute blobs (`bcl-attribute`, through `attr:` requests), the data-import helper
  (`bcl-dataimport`), `--lto-emit-asm`, its data-import error with references included
  (`bcl-lto-emit-asm`), and `--reproduce` (`bcl-reproduce`); every one also runs with
  `LLD_IN_TEST=2`. `msil/test/M2/05-refs/` (12
  tests) runs the real packs: `mscorlib-safe` (the probe on every runtime of the leg), `p1prog`
  (in every leg, with the AssemblyRefs that leg must give), `rows-none`, `rows-net8` and
  `rows-net10` (the rows, the runs and the negative runs of the table above),
  `net48-equals-none`, `positional-pack` (byte-identical to `--reference`), `facade-only` and
  `two-corlibs` (the core-library errors), `dataimport-nativelibrary`,
  `lto-text-mscorlib` and, since M23, `netcore-dll-name` (a program bound to the .NET 8 pack,
  named `prog.dll`, run with its runtimeconfig). The legs `refs-net48`, `refs-net8` and
  `refs-net10` run `11-programs`, `00-link` and `05-refs` bound to each pack, and since M23
  `refs-net8` also runs `04-shared` and the library rows of the matrix, on .NET 8 and .NET 10
  (GettingStarted.md section 7).

### Libraries and assembly-to-assembly linking (M22)

How to build a library, link programs and other libraries against it, deploy them and call one
from C#, with examples, is in GettingStarted.md section 3 ("Libraries"); plan 5.9.15 is the
contract and recon `p2/exports-multiasm.md` section 10 the design.

- **What `-shared` writes.** `msil-ld -shared` (clang's `-shared` and clang-cl's `/LD` and
  `/LDd` pass `--shared`) writes a *library assembly*: an IL-only PE32+ DLL image
  (characteristics 0x2022) with entry-point token 0 and no `'<msil>Main'`, also when an input
  defines `main`, which is exported like any function (D27 as amended for M22; `-e` with
  `-shared` warns `--entry is ignored with -shared`). Output names stay the driver's: `/LD
  foo.c` writes `foo.dll`, `-o` is used as given, and `-shared` without `-o` writes `a.exe`, a
  DLL image whatever its name (a name that ends in neither `.exe` nor `.dll` warns `<path>: the
  output name ends in neither .exe nor .dll; msil-ld writes a DLL image`). The Assembly row is
  named after the stem of `-o` (or `--assembly-name`, `-soname`; a name that differs from the
  stem warns `assembly name '<n>' differs from the output file name '<f>'; the CLR loads a
  library only as <n>.dll`), with no public key and the version of
  `--assembly-version=<a>[.<b>[.<c>[.<d>]]]` (parts 0-65535, default 0.0.0.0; it sets an
  EXE's version too; anything else is `invalid --assembly-version value '<v>'`).
- **Exports (plan Q22).** On the Linux triples every defined GLOBAL or WEAK function and
  variable of default or protected visibility, common symbols included, as `ld.lld -shared`
  exports them, so `-fvisibility=hidden` and `__attribute__((visibility("default")))` work as
  natively. On the Windows triples the `__declspec(dllexport)` definitions when any input has
  one, as link.exe does (`--export-all-symbols` exports every default-visibility definition
  anyway), and otherwise every default-visibility definition, as GNU ld's automatic export
  does. Never exported: names that start with `<msil>`, `.ctor` and `.cctor`, and the
  definitions of the members of the archives that `--exclude-libs=<archive>[,<archive>...]`
  names by file name (`libutil.a`; `ALL`: every archive), as in lld/ELF. A thread-local export is `thread-local storage is not
  supported yet: <s> in <file>` (phase 3). Exports are `--gc-sections` roots, and under LTO
  every bitcode definition that may be exported stays visible (irsymtab has no `dllexport`
  bit, so on the Windows triples the selection reads the flags of the objects that LTO writes).
- **The export class (Q23, Q24).** The exported functions are public static methods, with
  Param rows `A_<i>`, of one class, `public abstract sealed` (0x181) and extending
  `System.Object`, named by `--export-class=<Ns.Name>`, else by the assembly name parsed the
  same way: the last `.` separates the namespace (`zl` is a class without namespace,
  `Contoso.Zlib` the class `Zlib` in `Contoso`). An empty name, a `/`, more than 1,023 bytes,
  a namespace or name that starts with `<msil>` (D11's prefix for the toolchain's own names)
  and the value `<Module>` are `invalid --export-class value '<v>'`, for the default from the
  assembly name too, because such a class could repeat a TypeDef that `msil-ld` writes
  (`<Module>`, `'<msil>Data'`, `'<msil>SymbolTable'`, the spill classes), which ECMA-335
  forbids (`Ns.<Module>` is allowed), and a class of more than 65,524 methods is `export
  class '<c>' would hold <n> methods; the CLR allows 65,524`. After the functions come the
  accessors: per function `public static native int '<msil>addr_<f>'()`, which returns the
  function's one address (its `'<msil>fn_<f>'` field), and per variable `'<msil>addr_<x>'()`,
  which returns the variable's address, with a property `<x>` whose getter it is. A function
  alias is a forwarding method of its own name, and a data alias's accessor returns the
  aliasee's address plus the alias's offset. Hidden and `static` functions stay in `<Module>`,
  which no other assembly can name. C# therefore sees a static class with the C functions
  (pointers as `nint`) and an address property per variable.
- **Two-phase initialization (plan 5.9.10).** A library initializes in two module
  initializers. Phase 1 is the `.cctor` of the private `beforefieldinit` class `'<msil>Data'`,
  which holds `'<msil>data'`, the address fields, the `'<msil>fn_'` fields and the data-import
  fields: it imports native variables, allocates and fills the data image, sets the address
  fields and applies the library's own relocations, and names no other assembly. Phase 2 is
  `<Module>::.cctor`, left out when empty: an `ldtoken` and `pop` of each linked library's
  export class, in command-line order, used or not, so that every linked library initializes
  before the code that links it; the cross-assembly relocations, straight-line stores of
  accessor results; then the constructors. Phase-1 state is reached only through
  `'<msil>Data'` fields, so a library whose data point into another library's, in a cycle too,
  finds that data complete. An EXE with library inputs keeps one `<Module>::.cctor`, which runs
  the `ldtoken`s and the cross-assembly relocations after its own relocations and before its
  constructors, as Design.md 4.1 and 5.4 say since plan section 10 item 10 was resolved on
  2026-10-27 (no assembly links against a program, so nothing waits on its first phase). An EXE
  without library inputs, and every `-S` text, is byte for byte what M21 wrote. Known limit: in
  a library whose `'<msil>Data'` fields spill (more than 65,534 of them), the type initializers
  of `'<msil>Data'` and of a spill class depend on each other, so when two threads first use
  such a library at the same time one of them may read a spilled field before phase 1 has
  stored it (no test covers it; the fix is planned with phase 3's threads).
- **The symbol table (Q25).** A private attribute class `'<msil>SymbolTable'` (extending
  `System.Attribute`, AllowMultiple, constructor `(uint8[])`) carries the table on the Assembly
  row: format 1.0 (magic `MSYM`), flags (0x01 INTERFACE), the toolchain ABI version, the triple
  without its environment version, the C runtime identity, the data model of the inputs (the
  sizes of `void *`, `long`, `wchar_t` and `long double`), the export class, the data symbols
  with size, alignment and flags (const, weak), and the functions that carry flags (weak, and
  ABI followed by the definition's call descriptor: no prototype, the `sret` size and
  alignment, the `byval` parameters), which the signature checks of importers need. The codec
  in `llvm/lib/CLI` refuses another major version, unknown flags and trailing bytes, and M22's
  commit freezes the format. The C runtime identity is the first `-l` of the link, the driver's
  default libraries included, named `c`, `msvcrt`, `ucrt`, `ucrtbase`, `crtdll` or `msvcr*`,
  written as given, else `none` (as with `-nostdlib`), which matches any; a windows-msvc
  library that clang links gets `ucrt`, a Linux one `c`. `llvm-cil-dump --link-info lib.dll`
  prints the table: `SymbolTable v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 runtime='ucrt'
  datamodel=8,4,2,8`, then lines such as `ExportClass 'lib'`, `Data 'counter' size=4 align=4`
  and `Function ABI 'mk' SRET size=12 align=4` (a function that returns a struct
  through `sret`).
- **Linking against a library.** A library is a link input, positional (`zl.dll`) or found by
  `-l` as the managed kind of plan 5.10.9 ("Native libraries and P/Invokes" above); a .NET
  assembly without the symbol table stays a reference assembly (M21). Libraries are read in
  command-line order, the same file twice counts once, and two files of one assembly name are
  `<file>: library assembly '<n>' is also linked from <file>; the CLR loads one assembly of a
  name`. A library with the output's assembly name, compared without case (`foo.exe` linked
  against `foo.dll`, or a library linked against an older build of itself), is `<lib>: library
  assembly '<n>' has the name of the output; the CLR loads one assembly of a name`, because
  the CLR would resolve the library's references to the output. The link's triple comes from
  `--target` or the first CIL object or bitcode file, so a link of libraries alone without
  `--target` (clang always passes one) is `no target triple: pass --target or a CIL object or
  bitcode file`, with `--interface-only` too. A symbol table that does not decode, or has another format version, is an error when
  the library is read; once every input is loaded, each library is checked against the link,
  and its first failure is the error: the toolchain ABI version, the triple (`<file>: target triple <t> is incompatible with <t0>`; the
  vendor does not count), the data model, the C runtime (`<file>: C runtime '<c>' differs from
  '<c0>' of this link`; `none` matches anything, since windows-gnu may mix msvcrt and ucrt),
  the export table (`<file>: malformed export table: <what>`: no export class, a missing class,
  accessor or property, a symbol exported twice, no table), then the library's own AssemblyRefs against
  the link's references, or against `mscorlib` 4.0.0.0 without references (a newer version is
  an error, an absent name the warning `<lib> references <name>, which is not among the
  references`). That warning also names a library built by this toolchain that a linked library
  links and the program's link does not have: a program linked against `a.dll` alone, when
  `a.dll` links `c.dll`, warns about `c`, and giving `c.dll` to the program's link too silences
  it. An AssemblyRef that names, ignoring case, another library of the link or the output
  itself (the other half of a cycle) is not checked against the references. Since M23 a
  library is also checked against bitcode: when the link has no CIL object of its own, the
  model was the first bitcode file's pointer and `long` sizes only, so after LTO `msil-ld`
  compares each library's `wchar_t` and `long double` sizes with the first LTO object's and
  reports `<lib>: data model <m> differs from <m0> in <LTO object>`, where M22 linked such a
  library without a diagnostic (`shared-checks.test`). Undefined symbols bind late, as native ones do, but
  before them: to the first library in command-line order that exports the name, weak
  undefined references included (`--trace-symbol` prints `<lib>: managed definition of
  <s>`). A later exporter warns `symbol '<s>' is exported by <lib1> and <lib2>; using <lib1>`;
  a definition of the program that a library also exports wins for the program, with the
  warning `symbol '<s>' is defined in <file> and exported by <lib>; the output uses its own
  definition`, while the library keeps calling its own (no interposition, Q21); a function
  bound to data or the reverse is `symbol type mismatch: <s>`.
- **Calls, addresses and data.** A call of a library function is a `call` of the MemberRef
  `[lib]Ns.Class::f` with the library's signature, after the check of plan Q8 against the
  export's signature and call descriptor: a difference that an adapter can bridge gets an
  adapter `'<msil>adapt_<f>_<n>'` and the warning `function signature mismatch: <f>`, whose
  `>>> defined as` line ends in `in <lib>`, and the others are that error. The address of a
  library function or variable is a call of its accessor: the object's 5-byte `ldsfld` of an
  address field becomes a 5-byte `call` of `'<msil>addr_<s>'()`, so sizes, branches and
  `.maxstack` stay, and any other use is `<file>: unsupported use of imported symbol '<s>' in
  '<m>' at IL offset <n>`. Importers never take a library function's address with `ldftn`, so
  `&f` is the same in every assembly, on the .NET Framework too (`framework-identity`). A data
  relocation against a library symbol (`int *pc = &counter;`) is a cross-assembly relocation
  that phase 2 (an EXE's one initializer) applies, 8- and 4-byte sites, aligned or not; a
  difference of two addresses of which one is in a library is `symbol difference '<a>' -
  '<b>' in <file> refers to a symbol of <lib>`. Each library gets one AssemblyRef: its name, version and culture,
  flags 0, no key or token (a token makes the .NET Framework refuse such a library). `-Map`
  lists the libraries under `Inputs:`, and under `Symbols:` the symbols bound to their exports
  as `ManagedFunction` and `ManagedData` rows with the library as the input, after the
  natively bound symbols and before `__start_`/`__stop_`, by library in link order (plan
  5.9.14).
- **Every symbol resolved (Q21's `-z defs`).** An assembly has no global symbol scope, so a
  library, like a program, must resolve every symbol: `undefined symbol: <name>` stops a
  `-shared` link too, and a weak undefined symbol stays null. A cyclic pair therefore links
  through an *interface library*: `-shared --interface-only` writes the export class,
  accessors, properties, signatures, identity and symbol table of the real link of the same
  inputs, the table flagged INTERFACE, with stub bodies (`ldnull`, `throw`) and no data image,
  `'<msil>Data'` or initializer, and allows undefined symbols. Linking against an interface
  binds as against the real library (`--verbose`: `<file> is an interface library; deploy the
  library built from the same objects`); the real library is what gets deployed.
  `--interface-only` without `-shared` is `--interface-only requires -shared`.
- **No text.** `--lto-emit-asm` with `-shared` is `--lto-emit-asm cannot write a -shared
  library; link without --lto-emit-asm`, and with a library input `<file>: --lto-emit-asm
  cannot import from a .NET library; link without --lto-emit-asm`, so oracle O4 skips the tests
  that build or link libraries (the `skip O4` lines of msil/test/README.md).
- **Running.** The libraries go next to the program: `dotnet` and the .NET Framework load them
  from the application directory, and a library needs no runtimeconfig of its own. Every
  linked library initializes before `main`, dependencies first: the CLR runs a library's
  module initializer when a method that refers to the library is JIT-compiled, and the
  program's `ldtoken`s refer to each linked library before its constructors run (recon
  exports-multiasm 6.1; `init-order`). The C library state is shared, because every assembly
  calls the same native C runtime: `malloc` and `free`, a `FILE *` and `errno`, read right after
  the call that set it, work across assemblies (`c-state`; CoreCLR's own code may clobber a
  thread's `errno` later, a phase 3 item). C# programs call a library through its export class
  (`csharp-consumer`, compiled by csc for .NET 8 and the .NET Framework 4.8). A missing library
  stops the program before `main` with a `System.TypeInitializationException` around the
  `System.IO.FileNotFoundException` that names it, and a library that lost a function stops the
  method that calls it, when it is JIT-compiled, with `System.MissingMethodException`
  (`missing-library`, `missing-member`).
- **Known limits** (recon exports-multiasm 10.5 and 13; plan 5.9.15):
  - No symbol interposition across assemblies: a symbol that two assemblies define has one
    copy in each (warned), as with `-Bsymbolic`.
  - In a cycle, phase 1 of every library is complete before any constructor runs, but phase 2
    (the cross-assembly relocations and the constructors) of the library that was entered
    first may not have run when the other library's constructors call into it, and which
    constructor of a cycle runs first is unspecified, as natively. Two threads that enter a
    cycle from both ends rely on the CLR's deadlock breaking in type initialization (not
    tested).
  - The address of a *native* function differs between assemblies: each has its own P/Invoke
    stub.
  - A library whose member disappears fails at the caller's JIT time, not when the program is
    linked again; .NET 8 and later also refuse a library older than the version the program
    was linked against (`System.IO.FileNotFoundException`), which the .NET Framework accepts
    (section 5).
  - A library's assembly name must equal its file stem, and an application holds one assembly
    of a name: a managed `z.dll` cannot sit next to a native `z.dll`.
  - An imported variable whose declared type is larger than the exported size gets no warning,
    as ELF linkers give: link-info record v1 gives undefined data symbols no size (plan
    5.9.15).
  - Only `-shared` outputs are libraries: an EXE is never a library input, and only `-shared`
    outputs initialize in two phases.
  - That the CLR initializes a library before its first use at JIT time is observed
    behaviour: CoreCLR, the .NET Framework and crossgen2 (ReadyToRun) agree, and ECMA-335
    promises module initializers only "at, or sometime before, first access". Mono, NativeAOT
    and the interpreter were not tested.
- **Tests.** The 15 `shared-*.test` of `lld/test/MSIL` build libraries from objects of `llc
  -filetype=obj` and read them with `llvm-cil-dump`: the layout (`shared-layout`), export
  selection (`shared-export-selection`), the symbol table's golden bytes and printed lines
  (`shared-symbol-table`), both initializers and an EXE's one (`shared-two-phase`), a library's `'<msil>Data'` class
  with spill classes and a native data import (`shared-data-class`), MemberRefs,
  the accessor rewrite and cross-assembly relocations (`shared-import`), the identity checks
  on images that `Inputs/shared/gen.py` patches, a table without an export class among them
  (`shared-checks`), `-z defs` (`shared-zdefs`),
  signatures (`shared-signature`), duplicates and a library named like the output
  (`shared-duplicates`), interface libraries and a
  cyclic pair (`shared-interface`), `-l` (`shared-lsearch`), LTO (`shared-lto`), a library's
  AssemblyRefs against M21's fake pack (`shared-assemblyref`) and 1,000 exports
  (`shared-size`), each also with `LLD_IN_TEST=2`; `options.test` and
  `bcl-reference-option.test` pin the new options and built-in entries where M14's and M21's
  retired errors were (`options.test` also a link of libraries alone without `--target`). `clang/test/Driver/msil-shared.c` pins the driver and
  `CLISymbolTableTest` (19 tests) the INTERFACE flag and the ABI descriptors; since M23
  `shared-symbol-table.test`, `shared-interface.test` and `shared-zdefs.test` pin the exact
  printed lines (`Function ABI 'mk' SRET size=12 align=4`, ` flags=INTERFACE`) and option
  error (`msil-ld: error: --interface-only requires -shared`). The 14 tests of
  `msil/test/M2/04-shared/` (`lib-and-prog`, recon E7: calls, data, both relocation kinds,
  identity both ways, the library's constructor before `main` and a program function called by
  the library; `framework-identity`; `cycle-pair` and `cycle-three`, through interfaces;
  `init-order`, recon E4c, an unused library's initializer included; `c-state`;
  `archive-in-two-libs`, a copy of the same code in each library; `missing-library` and
  `missing-member`; `csharp-consumer`; `lib-det`, two builds byte for byte, libraries
  included; `roundtrip-library`, ildasm and ilasm keep the attribute and a program links
  against the copy, in the `roundtrip` leg and M22's gate (`msil_roundtrip=1`); `lsearch-managed`; and, since M23, `struct-abi`, a 40-byte struct returned through `sret` and passed by value across the assembly boundary and a K&R call that promotes a `float` to `double`) and the library rows of the matrix above run at `-O0`
  and `-O2` on .NET 8 (tiered and optimizing JIT), .NET 10, the .NET Framework 4.8 and WSL .NET
  10 and .NET 8, and bound to the .NET 8 pack on .NET 8 and .NET 10, against native builds of the same sources: DLLs with
  import libraries on Windows, ELF shared objects linked with `-Bsymbolic` and found through
  `-rpath $ORIGIN` for the E7 oracle. The harness builds the libraries from `%{msil_shlibs}`
  and verifies them with the program (msil/test/README.md); the tests that set it are
  UNSUPPORTED under `msil_pipeline=lto` with `msil_link_output=il`, because a library has no
  `--lto-emit-asm` text.

### Rust bitcode (M22)

An experiment of 2026-10-26 retargeted the bitcode of rustc 1.99 to the MSIL triples: `core`,
`alloc` and 13 crates.io crates, and `std` with stand-ins, ran on the .NET Framework 4.8, .NET 8
and WSL. It found four defects, which M22 fixed:

- **Code models under LTO.** rustc's bitcode for `x86_64-unknown-none` carries the module flag
  `"Code Model"` = kernel, which stopped `msil-ld`'s LTO with `LLVM ERROR: Target does not
  support the kernel CodeModel` and a stack dump, while llc and `clang -c` compiled the same
  module. The MSIL target has no code models (D8), so LTO now always configures the small one
  and links such bitcode (kernel or large code model) without a diagnostic, on the MSIL
  triples and as stock bitcode (`lto-code-model.test`).
- **Comment-only `asm` barriers** in LLVM IR count as empty ("C and LLVM IR" above;
  `asm-comment-barrier.ll`).
- **Native object files.** `msil-ld` stopped with `unknown file type: <path>` on a native
  object file on the command line, such as the empty `symbols.o` that rustc passes under
  `-Clinker-plugin-lto`. A COFF (Windows triples) or ELF (Linux triples) object is now read
  like a member of a native archive: a definition that nothing references is skipped
  (`--verbose`: `native object <file>: its definitions are machine code, which MSIL cannot
  link; a reference to one of them is an error`), a referenced one stays `symbol '<s>' is
  defined only in native object code (<file>); MSIL cannot link machine code`, also when a
  native library or a library assembly defines the same name, before or after the object (a
  native linker always links an object that the command line names, so no library binds the
  name instead), an oldnames-style alias object (`strdup` as a weak alias of `_strdup`) binds
  through its alias as it does inside `oldnames.lib`, and a file of a format that `msil-ld`
  recognizes but cannot link names it (`unknown file type: <file> (WebAssembly object)`)
  (`native-object-inputs.test`). A `/alternatename:<from>=<to>` linker option (Windows
  triples) does not redirect `<from>` when such an object defines it, and since M23 neither
  when a member of a native archive defines it in machine code, which lld-link extracts before
  it applies `/alternatename`: the reference stays `symbol '<from>' is defined only in native
  object code (<lib>(<member>)); MSIL cannot link machine code`, as without the option, in the
  object link, the LTO image and `--lto-emit-asm` alike (`native-alternatename.test`; in M22 the
  archive case redirected silently).
- **Constructor arguments.** glibc calls every `.init_array` entry as `void (int argc, char
  **argv, char **envp)`, and Rust std's `ARGV_INIT_ARRAY` reads its arguments that way. On the
  Linux triples a constructor (`__attribute__((constructor))`, `llvm.global_ctors` or an
  `.init_array` slot) whose parameters are those or a prefix of them gets the `argc` and `argv`
  that `'<msil>Main'` passes to `main` (built once, before the first such constructor) and, for
  `envp`, the process environment: the program's own import of libc's `environ`, else a data
  import of `environ` that `msil-ld` adds from the first native library that defines it
  (`global constructor '<c>' takes envp, but no native library of the link defines 'environ'`
  when none does). `clang -S`, llc and `--lto-emit-asm` text agree; they import `environ` from
  the C library (`libc.so.6`, on musl `libc.so`) through `dlopen` and `dlsym`. Link-info record
  v1 is unchanged: the number of arguments comes from the method's signature. Destructors and
  the Windows triples' `.CRT$XC*` entries take no arguments, and any other parameter list
  stays `global constructor '<c>' takes arguments, which MSIL does not pass`
  (`obj-ctors-args.test`, `obj-ctors-main-args.test`, `08-semantics/initarray-args`).

What runs (the recipe is in GettingStarted.md section 3, "Rust bitcode"): with `-C lto=fat`
rustc writes the bitcode of the whole crate graph, `core` and `alloc` included, which links like stock-clang
bitcode for the native triple. `core`, `alloc` and `no_std` crates such as serde_json,
hashbrown, miniz_oxide, sha2, crc32fast and libm ran, verified, with the native build's output,
through objects and through LTO, on both x86_64 triples. What needs phase 3: atomics (`Arc`,
hashbrown's default hasher), thread-local storage, calls of native variadic functions (`open64`,
`fcntl` and `syscall` in `std` on Linux), functions passed to native code (signal handlers), and
MSVC's `__fastfail` asm; architecture intrinsics (x86 SIMD paths, the `pause` of
`core::hint::spin_loop`) are errors until phase 4. `std` ran only with IR rewrites standing in
for the phase 3 items (single-threaded thread-locals, fixed-arity imports of the variadic
functions, and the like): printing, `Vec`, `HashMap`, the program's arguments and file reads
worked, threads did not. Phase 3 is what lets `std`, `Arc` and threads run without stand-ins.
With `-Clinker-plugin-lto` and no rustc LTO, the prebuilt `core` and `alloc` of the toolchain
are native code, which `msil-ld` reports as machine code.

### The Phase 2 exit (M23)

M23 changed the tests, the gate and CI more than the compiler (section 1 has the results):

- **Objects by default.** With no `--param msil_pipeline`, lit builds `%{msil_c}`,
  `%{msil_ll}` and `%{msil_prog}` through CIL objects and `msil-ld`'s image (`msil_pipeline=obj`,
  `msil_link_output=pe`); `asm` (`-S` or llc text and ilasm) is the `asm-oracle`, `modern-ilasm`
  and `wsl-ilasm` legs, and the reference side of O3 and O4. On a tree without `msil-ld` or the
  tree's `llvm-ar` the default only makes the execution tests UNSUPPORTED, with a note; an
  explicit `obj` or `lto`, and `msil_require_link=1`, make it a configuration error.
- **One build, several runtimes.** `--param msil_also_run=<major>[+tc0][,...]` repeats jitall
  and the run of every composite's verified image on each further .NET runtime (`+tc0` with
  `DOTNET_TieredCompilation=0`) against the same expected file, so the old `net10`, `net8-tc0`
  and `wsl-net10-tc0` legs became entries of `net8` (`10,8+tc0`) and `wsl-net10` (`8,10+tc0`).
  A test with explicit RUN lines REDEFINEs `%{msil_also_run_image}` to its image and appends
  `%{msil_also_run_check}` after its main FileCheck: one jitall, one run and one
  `FileCheck --match-full-lines` per entry, nothing without the parameter. The three trap tests of
  `msil/test/M1` use it in place of their own `DOTNET_TieredCompilation=0` and .NET 10 runs,
  so they follow each leg's folded runtimes.
- **The exit corpus** (`msil/utils/corpus/`): `manifest.json` pins ten entries,
  `fetch.py` fetches only over https, checks every hash before it extracts, extracts safely,
  refuses a cache inside the repository and writes a stamp that lit reads (parameters
  `msil_corpus_dir`, `msil_require_corpus` and `msil_corpus_skip`, features `msil-corpus` and
  `msil-corpus-<name>`). `msil/test/Programs/` holds the nine programs, their `det` tests and
  the c-testsuite format; `%{msil_copts}` gives a test's compile flags to both builds of
  `%{msil_prog}` and `%{msil_det}` and to every build of `%{msil_c}` on every pipeline (never
  to `%{msil_ll}`), and `%{msil_expect}` names another expected file.
- **Determinism manifests.** `msil/utils/ci/det-manifest.py build` compiles every C file of
  `msil/test/M1` with `clang -c` for both x86_64 MSIL triples at `-O0` and `-O2` from relative
  paths, archives each set with `llvm-ar`, and writes the sorted SHA-256 of every object and
  archive; `compare` fails on any difference. The gate compares two work directories of one
  host, the CI the two hosts.
- **Compiler and linker changes of the fix loop**, each with its tests: the warning about a
  `.exe` bound to .NET's core library ("Reference assemblies and the base class library"
  above), a null function in `llvm.global_ctors` ending the constructor list on every path
  ("CIL object links and the image"), `/alternatename` over machine code in a native archive
  ("Rust bitcode"), and the data model of a library checked against LTO's objects
  ("Libraries and assembly-to-assembly linking"). Outputs that none of them concerns are byte
  for byte M22's. M23 also pinned exact texts that M22's tests matched loosely, and added
  tests for constructors that take `argc`, `argv` and `envp` in whole-program text
  (`ctor-envp-text.ll`, glibc and musl), comment-only asm templates of line and block comments
  (`asm-comment-barrier.ll`) and structs across an assembly boundary (`04-shared/struct-abi`).

## 3. What Phase 2 does not do

Phase 2 is the linker phase; most of what it leaves out is Phase 3's (plan section 7, P3-A and
P3-B). The middle column says what a program meets today, the last one where it is planned.
The link-semantics matrix pins the Phase 2 diagnostic of each P3 row (section 1).

| Feature | Phase 2 | Planned |
|---|---|---|
| Any other external function (`malloc`, `strlen`, `exit`, ...) or external variable (`environ`, `stderr`, ...) that no file of the program defines | `-S` and llc: `... is not a Phase-1 import` and `external global variable ... (phase 2 linker)`; in a CIL object (M19) both are undefined symbols. `msil-ld` binds such functions to the native libraries of the link (M15, section 2) and imports such variables (M20; in M15-M19 `importing native data symbol '<name>' (<module>) is not supported yet`); a symbol that nothing defines is `undefined symbol: <name>`, and a `weak` declaration that nothing defines is null instead (section 2) | linked programs: done (functions M15, variables M20); `-S` and llc compile one module and keep the errors |
| Variadic functions: definitions and calls | error; in a program that `msil-ld` links, a call of a native variadic function (`printf`) is `native function 'printf' (<module>) is variadic; calls of native variadic functions are not supported for MSIL yet (phase 3)`. The matrix row `variadic-def-xtu` (a definition called from another object) and c-testsuite's `00140` pin the definition's error | phase 3 (Design.md 4.7) |
| The `va_arg` instruction in LLVM IR (other front ends, hand-written IR; clang expands C's `va_arg` itself) | error: `the va_arg instruction is not supported for MSIL yet (phase 3 variadics)`. One of a struct or array type is invalid IR on every target since upstream 74a120bff99f (merged at 1838cefa99c0): the IR verifier rejects it with `va_arg with an aggregate type is not supported` (clang: `invalid LLVM IR input: ...`; llc: then `input module cannot be verified`). Only in IR that skipped the verifier (`llc -disable-verify`) does it reach the backend, which reports it with the text above before instruction selection, which crashed on it (as it does natively), and gives its function no method (M12) | phase 3 (Design.md 4.7) |
| `thread_local` / `_Thread_local` | error: `thread-local variable @<name> is not supported for MSIL yet`, also for a `weak` declaration that nothing defines (which is not made null, unlike other weak declarations) | phase 3 (Design.md 4.4) |
| Dynamic `alloca`, VLAs, `stacksave`/`stackrestore`, `__builtin_frame_address` | error (`... phase 3 shadow stack`) | phase 3 (Design.md 4.2) |
| `setjmp`/`longjmp` | error | phase 3 (Design.md 4.6) |
| Atomics (`_Atomic`, `__atomic_*`, `__sync_*`) | error (only fences work) | phase 3 |
| Global destructors, `atexit` | error, also a destructor table (`.fini_array*`, `.CRT$XT*`) since M20 (`global destructors are not supported yet: <s> in <file>`). `atexit` binds to no native library: glibc defines it only in machine code (`symbol 'atexit' is defined only in native object code (.../libc_nonshared.a(atexit.oS)); MSIL cannot link machine code`), with a glibc sysroot that has `libc_nonshared.a`; with the E7 sysroot, whose `libc.so` script names no `libc_nonshared.a`, and on the Windows triples, whose UCRT does not export it, the link stops with `undefined symbol: atexit` (the matrix rows `p6-dtors`, `fini-array`, `crt-xt` and `atexit-order` pin these errors) | phase 3 (Design.md 4.1), which intercepts `atexit` |
| `long double` | is `double` (8 bytes, 53-bit mantissa) on every MSIL triple, not an error. Differs from native on x86_64 Linux (glibc and musl) and x86_64 MinGW (`x86_64-w64-windows-gnu`), where it is the 80-bit x87 format in 16 bytes, and on aarch64 Linux, where it is IEEE binary128 in 16 bytes; matches native on windows-msvc (x86_64 and aarch64) and `aarch64-w64-windows-gnu`. So where it differs, a native `long double` function (`sqrtl`, `expl`, `powl`, `strtold`, `nexttoward`, ...) that `msil-ld` binds is the link error `native function '<name>' (<module>) takes or returns a value that a P/Invoke cannot pass natively yet (phase 3)` (section 2, "Native libraries and P/Invokes"); windows-msvc and `aarch64-w64-windows-gnu` bind them | permanent (Design.md 3; section 6, item 14); calls of native `long double` functions: phase 3 |
| Conversions between floating point and `__int128` under strict floating point (constrained intrinsics: `-ffp-model=strict`, `#pragma STDC FENV_ACCESS ON`), conversions between `_Float16` and `__int128` and from `__int128` to `__bf16` (helpers `__extendhfsf2`, `__truncsfhf2`, `__extendbfsf2`), `_Float16`/`__bf16` arithmetic (for `__bf16` `__extendbfsf2` and `__truncsfbf2`), `__bf16` call arguments and return values even without arithmetic (`__extendbfsf2`; at `-O2` a `__bf16` parameter that is only returned or passed to another call compiles, and a `__bf16` parameter itself needs no helper, nor do `_Float16` ones), C `_Complex` multiplication and division | error naming the runtime helper (`__fixdfti`, `__floattidf`, `__extendhfsf2`, `__muldc3`, ...). `__int128` division, the plain conversions and those from `__bf16` to `__int128` are inline since M12 (section 2). Under `msil-ld`'s LTO, argument promotion never turns a pointer into a `_Float16` or `__bf16` parameter (section 2, "LTO and argument promotion") | phase 3: BCL types where available, helpers in the output assembly otherwise |
| `__float128`, `x86_fp80`, rounding-mode changes, floating-point environment access | error (CIL rounds to nearest and has no FP exceptions) | |
| Inline and module-level assembly, other than empty `asm` statements with or without operands (section 2; in LLVM IR since M22 also templates of nothing but `/* */` comments): instructions, outputs not tied to an input, condition-flag outputs (`"=@cc<cond>"`, `"+@cc<cond>"`, tied or not), an output in memory tied to an input of another floating-point type, `asm goto`, Microsoft `__asm`, file-scope `asm` | error: clang's `inline assembly is not supported for the MSIL target '...'` (`Microsoft-style inline assembly ...`, `file-scope assembly ...`), llc's `inline assembly is not supported for MSIL` | never (Design.md 3) |
| A section that MSIL cannot lay out: a destructor table (`.fini_array*`, `.CRT$XT*`), `.ctors`/`.dtors`, `.preinit_array*` (Linux triples), another `.CRT$X*` section (`.CRT$XI*`, `.CRT$XL*`, `.CRT$XP*`, ...), thread-local data (`.tdata`, `.tbss`); a constructor-table entry that is not a function address; `__start_`/`__stop_` on the Windows triples | error: `global destructors are not supported yet: <s> in <file>`, `section '<name>' in <file> is not supported` (`.preinit_array` too, which ld.so runs before every `.init_array` entry; `--gc-sections` keeps such segments for the report), `thread-local storage is not supported yet: <s> in <file>`, `constructor table entry at <section>+<off> in <file> is not a function address` (in `-S` and llc text `<file>` is the source file; since M21 a null entry of a `.CRT$XC*` table on the Windows triples is skipped instead, as the MSVC CRT's `_initterm` skips it, while a null `.init_array` entry stays the error). Every other section on a variable (`__attribute__((section(...)))`, `__declspec(allocate(...))`) is laid out since M20, in `-S` and llc text as in `msil-ld`'s image, and constructors registered in `.init_array` or `.CRT$XC*` run (section 2). On the Windows triples `__start_X` stays undefined, as with link.exe and lld-link, and in `-S` text it is D26's external-variable error. In M12-M19 `-S` and llc rejected every section on a variable, `global variable @<name> in section "<section>" is not supported for MSIL yet (phase 2 CIL objects)`, and before M12 they dropped it silently, so such a constructor never ran. A section on a function is ignored, and clang's own sectioned globals (`llvm.metadata`) are not affected | destructor tables: phase 3 (Design.md 4.1); `.preinit_array` (its entries run before every `.init_array` entry): a later milestone; `__start_`/`__stop_` on Windows: never (native COFF links do not define them) |
| Aliases other than those of section 2 (an offset into a function, a function-typed alias of a variable, an alias of an ifunc, a `thread_local` alias of a variable that is not thread-local) and ifuncs (from LLVM IR only: clang ignores `__attribute__((ifunc(...)))` on the MSIL triples with an `unknown attribute` warning) | error: `alias @<name> is not supported for MSIL`, `ifunc @<name> is not supported for MSIL`. A CIL object (M19) keeps every alias of a definition plus a constant offset (offset 0 for a function) as an alias symbol for `msil-ld`; any other alias there is `alias '<name>' must point to a definition plus a constant offset for MSIL` | ifuncs stay errors |
| Function pointers passed to native code (callbacks) | function pointer values are managed; no Phase-1 import takes a callback. In a program that `msil-ld` links, a native function of plan 5.10.14's callback list (`qsort`, `bsearch`, `signal`, `pthread_create`, `CreateThread`, ...; section 2) is a link error, `native function '<name>' (<module>) takes a C function pointer; passing functions to native code needs phase 3 thunks`. **Known gap:** a function address passed to any other native function (a native library's own callback API, a Win32 callback) is not diagnosed and fails at run time | phase 3 thunks (P3-B; Design.md 4.5) |
| Structs by value, `__int128`, vectors and `_Float16` in P/Invoke signatures | none of the standalone imports takes one; a natively bound function that does is `native function '<name>' (<module>) takes or returns a value that a P/Invoke cannot pass natively yet (phase 3)` | phase 3 |
| Exports (`-shared`), assembly-to-assembly linking | done in M22 (section 2, "Libraries and assembly-to-assembly linking"), with the known limits listed there; in M14-M21 `msil-ld` reported `-shared is not supported yet`, and in M21 a .NET library that this toolchain built, given as an input, `<path>: linking .NET libraries built by this toolchain is not supported yet` | done (P2-C, M22), Design.md 2.3 |
| BCL intrinsics (`BitOperations`, `Math`, `MathF`, `Vector128`, ...) | none: bit operations, `fabs` and `copysign` use inline expansions, math calls are P/Invokes (D16) | phase 3 codegen switches |
| The codegen switches for newer .NET APIs of Design.md 7.1 (`DisableRuntimeMarshalling`, `Math.FusedMultiplyAdd`, `UnmanagedCallersOnly` thunks, ...) | none exist; Phase-1 output already is what their defaults will give: code that runs on .NET Framework 4.8 and modern .NET alike. Since M21 `msil-ld`'s binder has the optional request strengths they will use (an attribute dropped, or a call substituted by a helper, when the references lack the member; section 2), with no producer yet | phase 3 (P3-A) |
| Architecture-specific intrinsics (`llvm.x86.*`, `llvm.aarch64.*` from `immintrin.h`, `arm_neon.h`) | error naming the intrinsic: `intrinsic '<name>' is not supported for MSIL` (`llvm.x86.sse42.crc32.32.32` from `_mm_crc32_u32`, `llvm.x86.rdtsc` from `__rdtsc`, ...). Intrinsics that clang lowers to generic IR (`_mm_add_ps`, `_mm_popcnt_u32`, ...) still compile | phase 4 SIMD (Design.md 3.2) |
| SVE and SME scalable vector types (`__SVInt32_t`, `svint32_t`, `__SVCount_t`; in IR `<vscale x N x T>`, `target("aarch64.svcount")`, `llvm.vscale`), reachable on the aarch64 MSIL triples with `-march=...+sve` or `__attribute__((target("sve")))` | error naming the type or intrinsic: `scalable vector type '<vscale x 4 x i32>' is not supported for MSIL` | phase 4 SIMD (Design.md 3.2) |
| Debug information (Portable PDB) | `-g` accepted, nothing emitted | phase 4 |
| Darwin and i686 triples | clang: `unknown target triple '<triple>'`, for a Darwin triple also when the command only links; `msil-ld`: `unsupported target triple '<codegen triple>' for MSIL` (stock-clang bitcode of such a triple, or such a `--target=`) | phases 4 and 5 |
| Stack protector (`-fstack-protector*`, `/GS`), control-flow guard, patchable function entries | ignored with a warning (clang-cl's implicit `/GS` is not passed) | |
| Basic block sections, late function splitting, basic block address map, static data partitioning (`-fbasic-block-sections=`, `-fsplit-machine-functions`, `-fbasic-block-address-map`, `-fpartition-static-data-sections`) | `-fsplit-machine-functions`, `-fbasic-block-sections=all` and `=list=`, `-fbasic-block-address-map` and `-fpartition-static-data-sections` are a driver error (`unsupported option '...' for target '...'`), because MSIL is not ELF; `-fbasic-block-sections=none` is accepted, and `-fbasic-block-sections=labels` (a deprecated alias) passes the driver as `-fbasic-block-address-map` and ends in the backend's warning `basic block address map is not supported for MSIL; ignored`. Passed to cc1 or llc directly (`-fsplit-machine-functions`, `-mllvm -function-splitting=`, `--basic-block-sections=`, ...), the options are ignored, the address map with that warning: the IL is the same as without them | never: CIL methods have no sections |

### Known gaps of the Phase 2 exit

- **Cross-host image determinism.** The CI's `determinism` job compares the CIL objects and
  archives that the Linux and the Windows runner build from `msil/test/M1`; no linked image is
  compared across hosts, because the Windows runner has no Linux sysroot and the Linux runner no
  Windows import libraries (plan section 10 item 12, its default kept). Images are compared
  within each host: `det` and `det-wsl` (`msil/test/M2/06-det` and the nine exit programs) and
  the CI's `det` leg.
- **CI licences of the exit corpus.** CoreMark comes under the EEMBC licence agreement, and each
  c-testsuite test carries the licence of its scc or tinycc original; whether GitHub's runners
  may download and run them is unverified (plan section 10 item 11). Until the user decides,
  `msil-ci.yml` sets `MSIL_CORPUS_SKIP: coremark,c-testsuite`, so CI neither fetches nor runs
  them (CoreMark's tests are UNSUPPORTED, the `ctest` leg is a SKIP row), and they run only in
  the phase gate on the development host, which clears the variable; there c-testsuite runs
  only for `x86_64-pc-windows-msvc-msil` (the `ctest` leg has no WSL twin), so no run covers it
  on the Linux triple. Emptying the variable is one line of the workflow.
- **No `untested` rows.** Every line of `matrix.txt` has a test; the P3 rows pin today's
  diagnostic, and their transcripts are phase 3's.
- **Callbacks into the program from native code** that are not on plan 5.10.14's callback
  list are not diagnosed (the "Function pointers passed to native code" row above).
- **The cost of c-testsuite.** Its 150 tests at two levels took about 18 minutes with 8 workers
  on the loaded development host while M23 was built, and 9, 18, 7 and 11 minutes in M23's four
  phase-gate runs, against the plan's estimate of 4 for the `ctest` leg.
- **The time of the phase gate.** Step 16 took 228, 339, 184 and 275 minutes in M23 (about three
  to six hours, depending on the host's load), against Q32's budget of 70-90 minutes; the cuts
  that lose no coverage would save about 10 minutes, so plan section 12's row 'Q32 amended
  (phase-exit gate time)' keeps every leg and records the measured time (GettingStarted.md
  section 7 has the times per leg).

### Limits

- An IL identifier is at most 1023 bytes, because both ilasms reject longer names; a longer C
  name is a compile error.
- `memset` with a variable length is limited to 4 GiB (`initblk` takes a 32-bit size).
- A P/Invoke records only the native library's name (M15): a library of your own must be
  where the runtime's loader finds it when the program runs, next to the program or on the
  system's search path, and on another system the same name may be another build of the
  library (a P/Invoke has no symbol version and binds the running library's default one).
- A native data import (M20, plan 5.10.12 as clarified in section 12) passes the library name
  unchanged to `dlopen(<module>, RTLD_LAZY)` (Linux) or `LoadLibraryW` (Windows), so a bare
  name such as `libnative.so` or `native.dll` is found only through the system loader's own
  search: on Linux `LD_LIBRARY_PATH`, the `ld.so` cache (`/etc/ld.so.cache`) and the default
  directories, never the program's directory (glibc's `dlopen` does not look there); on
  Windows the directory of the host executable (`dotnet.exe` under `dotnet exec`, the program
  itself under .NET Framework), the system directories, the current directory and `PATH`.
  This differs from a P/Invoke, which the runtime also probes in the application directory: on
  Linux, and under `dotnet exec` on Windows when the current directory is another, a library of
  your own next to the program serves its functions but not its variables, whose import stops
  the program with `data import '<name>' not found in <module>` (section 2), so put that
  library on the loader's path. The system's C library is always found. The test
  harness does this for the programs that ship test-built libraries: on Linux, locally and in
  WSL, `msil-run.py` puts the directory into which it copies the `--dep` and `--dep-dir` files
  first in `LD_LIBRARY_PATH` (the analogue of the native oracle's `-rpath '$ORIGIN'`).
  Retrying a failed load in the application directory is left to a later milestone.
- One CIL object holds at most the per-type member counts of the runtimes in `<Module>`
  (65,524 methods, P/Invoke stubs included, and 65,534 fields), because the object writer has
  no spill classes (plan 5.8.3; only `msil-ld`'s images spill, 5.9.12): a larger file fails to
  compile with `cannot write CLI metadata: type '<Module>' has <n> methods (limit 65524)`;
  split it into several files. `msil-ld`'s image moves members past the per-type limits into
  spill classes (section 2). Spilling in objects is a phase 4 (conformance) item.
- Object mode classifies a global as zero data by `isNullOrUndef`, which also accepts
  aggregates of zeros and `undef`s, while whole-program output keeps Phase 1's top-level test,
  so such a global (`{ i32 0, i32 undef }`, rare in C, in no program of `msil/test/M1`) lies in
  the zero part of a one-object link's image but in the initialized part of the `-S` text's.
  The program behaves the same.
- The relocation loop of `<Module>::.cctor` (plan 5.9.10) reads and writes 64-bit pointers
  (`ldind.i`, `stind.i`, 8 bytes wide), as every triple of D6 has 64-bit pointers; the 32-bit
  triples of phase 5 revisit it.
- `--gc-sections` (M20) keeps unreferenced data only when it is `used` or `retain`; lies in a
  constructor or destructor table, an unsupported section (which is then reported) or a section
  that a referenced `__start_`/`__stop_` symbol names; lies in the same named section (and
  COMDAT group) of the same object as a live variable, as ld.lld keeps a whole input section;
  or, on the Windows triples, lies in a named section outside a COMDAT group, which link.exe
  and lld-link always keep. So a table delimited by markers, in one section of one file or, on
  the Windows triples, in `<sec>$a`, `<sec>$m` and `<sec>$z` sections of several files, keeps
  the entries that only pointer arithmetic reaches, as a native link does. What ld.lld drops
  goes here too: on the Linux triples, the entries that one file puts in a named section need
  `__attribute__((used))` when nothing references that file's part of the section and no
  `__start_`/`__stop_` symbol names it. Under `-flto`, LTO drops unreferenced entries before
  `msil-ld` marks anything, as native LTO does, so there the entries of such a table need
  `__attribute__((used))` on every triple. An entry of `.init_array` or `.CRT$XC*` that holds no
  function address is an error; a null entry of `.CRT$XC*` is skipped since M21, as the MSVC
  CRT's `_initterm` skips it (in M20 it was the error too), while a null `.init_array` entry
  stays the error, because glibc calls every entry.
- An image bound to the .NET 8 or .NET 10 reference pack (M21) runs only on that .NET and
  later: CoreCLR loads a framework assembly only at the requested version or a newer one, and
  the .NET Framework has none of them (section 5). Give `msil-ld` the .NET Framework 4.8
  reference assemblies, or no references, for one image that runs everywhere.
- ilasm sets the quiet bit of float32 signaling-NaN constants (`ldc.r4`), with both ilasm versions;
  float64 constants are exact. The backend's encoder (M18) keeps the bits (`ldc.r4` of
  0x7FA00000 encodes as `22 00 00 A0 7F`), so the encoder parity check counts such a constant
  as `snan-quiet` rather than as a mismatch. CIL objects (M19) carry the encoder's bytes, and
  `msil-ld`'s images (M20) keep them, so their constants are exact; only an image that ilasm
  assembles from `-S` or `--lto-emit-asm` text has quieted ones, and the object and
  equivalence oracles compare with `--snan=quiet`.
- In `-S` and llc output, a prototype-less imported function whose address is used but which
  is never called directly cannot get a signature and is an error; whether the optimizer turned
  the indirect call into a direct one decides, so such code can fail at `-O0` and compile at
  `-O2`. Declare imports with a prototype (section 12, M9 row). A CIL object (M19) takes such
  an address through `'<msil>FnAddr'` without an error and has no stub for the function, and
  `msil-ld` reports a native binding of it (M20, plan 5.10.13): `the signature of
  prototype-less function '<f>' cannot be derived: its address is used, but it is never called
  directly; declare it with a prototype`.
- Libraries (M22) have the known limits listed in section 2, "Libraries and
  assembly-to-assembly linking": no interposition across assemblies, the order of phase 2 in a
  cycle, one P/Invoke stub (and so one native function address) per assembly, failures of a
  changed library at JIT time, one assembly per name and the name equal to the file stem, and
  no warning for an imported variable whose declared type is larger than its export, because
  link-info record v1 gives undefined data symbols no size.
- On Linux, as for native programs, only the low 8 bits of the exit status survive.

## 4. Verification policy

Design.md 12 asks that "`ilverify` reports no stack-shape errors". ILVerify alone cannot check that:
it stops at the first error of a method and cannot verify any method that contains `calli`. The
user approved this replacement on 2026-10-12 (decision D21, and its refinement in plan section 12,
rows 'D21 and M0.b item (7) refined' and the user confirmation after it). A module passes when all
of these are clean:

1. **MSILStackCheck**, the last machine pass of the backend, always on: it recomputes the stack
   depth before and after every instruction, checks that every branch and join agrees on it and
   that no block falls off the end of a method, and computes the exact `.maxstack`. A violation is
   an internal compiler error, never a module.
2. **jitall** (`msil/utils/msil-jitall.py`): every method is JIT-compiled with
   `RuntimeHelpers.PrepareMethod` on the runtime under test (in WSL for Linux triples), under a
   timeout and a memory cap. `main` does not run; `<Module>::.cctor` does, as it would before
   `main`.
3. **ILVerify** (`dotnet-ilverify` 10.0.12) with the allow-list `msil/utils/ilverify-allowed.txt`:
   `Unverifiable` (localloc, cpblk, initblk), `InitLocals` (`.locals` without `init`), `StackByRef`
   only as `[found Native Int] Expected ByRef on the stack.`, and `UnmanagedPointer`. Every other
   code fails.
4. **PEVerify** (Windows): every message fails except the unverifiable-by-design ones and exactly
   `[found Native Int][expected unmanaged pointer] Unexpected type on the stack.` (the native-int
   addresses passed to `System.Buffer::MemoryCopy`, D15). Two shapes that D12 and D2 always
   produce are notes only when the second pass runs: the RVA image copy in `<Module>::.cctor`
   and, since M20, the read of the relocation table `'<msil>relocs'` there (`Cannot modify an
   imaged based (RVA) static`, at exactly those `ldsflda`s; since M22 also in
   `'<msil>Data'::.cctor`, phase 1 of a library), and a function address used as a
   `native int` (`ldftn`).
5. **A neutralized second pass** (`msil-verify.py`): an allowed construct hides the errors
   behind it, so the assembly is disassembled with ildasm, every allowed construct is replaced
   by something verifiable with the same stack effect (`localloc`, `ldftn` and the `ldsflda` of
   the image and of the relocation table become `native int` zeros; `cpblk`, `initblk`, `ldind`,
   `stind` and `calli` become calls of checking stubs), reassembled, and verified again by
   ILVerify and PEVerify with no allowance but the `MemoryCopy` message.

Stack *types* are checked only by the verifiers (the JIT accepts most type errors silently), and
mixing of `native int` and `int64` only by PEVerify, so that check runs on Windows. Every gate
passes `--param msil_require_verify=1`, which makes a missing ILVerify, ildasm, jitall, PEVerify
(Windows), ilasm or runtime an error instead of a skipped step (D38), since M16 also a missing
`mdcanon`, and since M17 a missing `llvm-cil-dump`, the two canonical-listing tools of section 2
(also when an empty `--param msil_mdcanon=` or `msil_cil_dump=` switched one off). On top of
verification, every program runs and its transcript is compared with the native build's, under
Tier-0 and fully optimizing JIT. Since M20 the images that `msil-ld` writes are verified the
same way, and with `--param msil_equiv=1` (since M23 `net8` and `refs-net48`; before, the `obj`
legs) `msil-verify.py --md-baseline` adds
oracle O6: the metadata messages of PEVerify `/md` for the image that runs, with each token
replaced by the row it names, must be among those for ilasm's image of the same file's `-S`
text (on Linux, which has no PEVerify, the check is skipped with a note). Since M21 two tests
skip O6 by a `skip O6` line of `msil/utils/equiv-allowed.txt` (section 2, "CIL object links
and the image"), and an image bound to the .NET 8 or 10 reference pack is verified by ILVerify
against that pack with PEVerify left out, because PEVerify cannot load such an image
(`msil-verify.py --skip-peverify "bound to .NET <n> references"` prints `PEVerify skipped:
<reason>`, and O6 is skipped with it); this happens only in the `refs-net8` and `refs-net10`
legs (and in a lit run whose `msil_references` path names such a pack, which lit treats as
that pack), and in the `05-refs` tests that bind to those packs, and the same programs pass
PEVerify in every other leg. Since M22 a program that links libraries is verified with them
(`msil-verify.py --dep-dir <dir>`: ILVerify references and copies next to the program for
PEVerify), and each library is verified too, with both passes (`--also-verify-deps`).

### The oracles (plan 5.12.6)

Verification checks that an image is well-formed; the oracles check that it means what the C
program means. D21's verification is oracle O5; the others compare with a native build or with
what ilasm makes of the compiler's own text:

| Oracle | What it compares | Where it runs (since M23) |
|---|---|---|
| O1 | the program's transcript (stdout and exit status) against the native build of the same sources: on Windows clang with lld-link and the compiler-rt builtins, for the Linux triple the E7 oracle (built on Windows, run in WSL), on the Linux CI job the system's clang and linker | every composite (`%{msil_c}`, `%{msil_ll}`, `%{msil_prog}`) in every leg with `msil_native_oracle=required`, and `update-expected.py --check` (`expected`, `expected-linux`) |
| O2 | `msil-ld`'s image through ildasm, ilasm and ildasm again: the two listings equal, and the reassembled image runs (Design.md 5.5) | `roundtrip` (`11-programs`, `07-equiv` and a library under `obj`), `asm-oracle` for the `-S` route |
| O3 | the canonical listing of a one-object link with `--no-gc-sections` against ilasm's image of the same file's `-S` (or llc) text, with the allowances of `msil/utils/equiv-allowed.txt` | `%{msil_c}` and `%{msil_ll}` under `obj` with `msil_equiv=1`: `net8`, `wsl-net10`, `refs-net48` (with `scopes`) |
| O4 | the canonical listing of an LTO image with `--no-gc-sections` against ilasm's image of the same link's `--lto-emit-asm` text | `%{msil_prog}` under `lto` with `msil_equiv=1`: `lto`, `programs-lto`, `programs-lto-wsl` (not `lto-wsl`, whose paths `lto` covers for the Windows triple and the Linux CI job's `lto` for the Linux triple) |
| O5 | D21: MSILStackCheck, jitall, ILVerify with its allow-list and PEVerify, on the image and a neutralized copy | every composite, on every runtime of `msil_also_run` for jitall |
| O6 | the PEVerify `/md` messages of the image that runs, each a subset of those of ilasm's image (Windows only) | with O3 |
| object oracle | the canonical listing of a CIL object against ilasm's object of its object-form text, record included | `obj-text`, `llvm/test/CodeGen/MSIL/object-form-text.ll` |

`equiv-allowed.txt` lists every allowance with its reason and every test whose difference no
allowance removes (`skip` lines, msil/test/README.md); a skipped test runs every other check.

## 5. Runtime differences

What differs between the runtimes that Phases 1 and 2 target, and how the backend and `msil-ld`
deal with it:

| Behaviour | .NET 8 | .NET 10 | .NET Framework 4.8 | Backend and `msil-ld` |
|---|---|---|---|---|
| `conv.r.un` of a 64-bit value | rounds twice | correct | rounds twice | never emitted for 64-bit sources; unsigned 64-bit to FP uses a branch-free sequence of signed conversions (D17) |
| `conv.r4` of an int64 that the JIT constant-folds | correct | correct | folded as `(float)(double)x`, rounds twice | the value is first rounded to odd at bit 11, so both paths give the correctly rounded float |
| Floating point to integer out of range (C undefined behaviour) | x86 "indefinite" value | saturates | indefinite | programs must not depend on it; `llvm.fptosi.sat`/`fptoui.sat` are lowered explicitly |
| Alignment of a constant-size `localloc` of 32 bytes or less | only 8 for 16 and 32 bytes in Tier-0 code, and for 8 bytes in optimized code | same as .NET 8 | only 8 for 8 to 32 bytes | frames assume 8 and realign when an object needs more, over-allocating `MaxAlign - 8` bytes (D13) |
| `localloc` zeroed under `.locals init` | yes | yes | no | `.locals` is never `init`; C does not expect zeroed frames |
| Two `ldftn` of one function in different methods | equal | equal | can differ (precode vs. compiled code) | one address field per function, set once in `<Module>::.cctor` (M9) |
| Overlapping constant-size `cpblk` | not what `memmove` gives | same as .NET 8 | a third result | `cpblk` only for `memcpy`, where overlap is undefined; `memmove` uses `Buffer.MemoryCopy` |
| Inconsistent stack depth at a join | accepted | the JIT hung and grew past 40 GB | not measured | MSILStackCheck rejects such code at compile time |
| `Math.Min(-0.0, +0.0)` | -0.0 | -0.0 | +0.0 | `Math.Min`/`Max` are not used; min/max are compares and selects (D16) |
| Main-thread stack on Windows | 1.5 MB (the reserve of `dotnet.exe`) | same | 4 MB (the default of ilasm's 64-bit `.exe`, `ilasm -stack=` sets another, and of `msil-ld`'s images) | see below |
| `errno` read after the runtime's own native code ran (a `Console.WriteLine`, say) | 0 | 0 | the value the program set | read `errno` right after the call that set it; across assemblies it is shared like every other C library state (M22) |
| An image that references `System.Runtime` and its siblings at 8.0.0.0 (`msil-ld` with the .NET 8 pack, M21) | runs | runs | `System.IO.FileNotFoundException` for `System.Runtime` 8.0.0.0 at start-up | without references, or with the .NET Framework 4.8 reference assemblies, the image references only `mscorlib` 4.0.0.0, which all three load (the runtimes' `mscorlib` facades forward each Phase 1 type in one hop) |
| The same at 10.0.0.0 (the .NET 10 pack) | `System.MissingMethodException` (`Entry point not found in assembly`) at start-up: CoreCLR loads only the requested version or a newer one | runs | `System.IO.FileNotFoundException` | as above |
| How an executable starts | `dotnet <name>.dll` (or `.exe`) with `<name>.runtimeconfig.json`; the SDK adds a native apphost `<name>.exe` | same | `<name>.exe` directly, no configuration file | `msil-ld` writes an EXE image under any name and no runtimeconfig or apphost; since M23 it warns when a `.exe` is bound to .NET's core library or to `netstandard` 2.1 or later, which the .NET Framework cannot start (section 2, "Reference assemblies and the base class library") |
| A program run with an older build of a library than the one it was linked against (a lower `--assembly-version`; M22) | `System.IO.FileNotFoundException` | same | runs (no strong name, so the version is not checked) | the AssemblyRef carries the library's exact version and no public key token (a token would make the .NET Framework refuse the unsigned library; recon exports-multiasm 3), so a newer build of the library serves an older program everywhere |
| When a library's module initializers run (M22) | when the first method that refers to the library is JIT-compiled | same | same (also ReadyToRun) | the program's own `<Module>::.cctor` names every linked library with `ldtoken`, so all of them initialize before `main` |

- **Deep recursion.** `dotnet x.dll` on Windows runs `main` on the host's main thread, whose
  stack is 1.5 MB, while native Linux programs get 8 MB by default (`ulimit -s`, which the Linux
  `dotnet` host's main thread also uses), and a CIL frame (the `localloc` frame plus the JIT's own
  frame) is larger than the native one, at `-O0` especially: about 19,000 frames of an
  `-O0`-shaped recursion fill the .NET 8 main thread on Windows (plan M9.d, review experiment
  `recon/artifacts/review-cil/deep.il`). A stack overflow terminates the .NET process and cannot be
  caught, so a program that recurses deeply natively may fail under .NET.
- **Traps and undefined behaviour** surface as .NET exceptions where native code gets a signal: a
  trap is an unhandled `System.NullReferenceException`, integer division by zero a
  `System.DivideByZeroException`. On Windows an unhandled exception exits with `0xE0434352`.
- **Tiered compilation.** Short programs run almost entirely as Tier-0 code, which is why the
  gate repeats the suite with `DOTNET_TieredCompilation=0` (since M23 through `msil_also_run`,
  on the image already verified); no difference in results is known.
- **The Phase 2 exit found no new difference.** The nine exit programs and the link-semantics
  matrix print the same transcripts on .NET 8, .NET 10 and the .NET Framework 4.8 for
  windows-msvc and on WSL .NET 10 and .NET 8 for linux-gnu (c-testsuite runs on .NET 8), and
  the exit programs print the same on both triples, so none of them needs a per-OS or
  per-runtime expected file. TweetNaCl, whose
  `u32` is `unsigned long` (32 bits on windows-msvc, 64 on linux-gnu), matches on both, as its
  native builds do.

## 6. Design.md updates

The 14 Design.md updates this section proposed were applied to [Design.md](Design.md) on
2026-10-21, with the user's approval, together with the Phase 2 plan, the Phase 2 decisions
(recon `p2/decision-brief.md` section 3) and the consistency fixes of recon `p2/backlog.md`
section 7. Item 4 was amended: a `DllImport` names the library as a native link records it (the
ELF `DT_SONAME`, the exact Windows DLL file name), always `ExactSpelling` and `CDecl`, never a bare
name and never `libc` (Design.md 5.3). Where each item went, for the references above:

| Items | Design.md |
|---|---|
| 1, 2, 3, 4, 8, 13 | the "As built (Phase 1)" note at the end of section 12; item 4 also 5.3, item 8 also 4.5 |
| 5, 10, 11, 14 | section 3 (predefines, inline assembly, triple table, `long double`) |
| 6 | 4.2 |
| 7 | section 10, risk 9 |
| 9 | 2.3 |
| 12 | 5.5 |

### Phase 2 as-built items (15-23)

Items 15-22 were proposed here where Phase 2 as built differed from Design.md. Item 15
(two-phase initialization) was applied on 2026-10-27. Items 16-22 and a new item 23 (the
data-import helper of `msil-ld`) were applied to [Design.md](Design.md) on 2026-10-28, with the
user's approval, together with the Phase 3 plan and the Phase 3 decisions (recon
`p3/decision-brief.md`); items 16, 19 and 21 were amended as recon `p3/backlog-p3.md` section 3
says. Where each item went:

| Items | Design.md |
|---|---|
| 15 | 4.1, 5.4 |
| 16 | the "As built (Phase 2)" note after the section 11 table |
| 17, 18 | 5.1 |
| 19 | 5.2, step 1 |
| 20 | 4.1 |
| 21 | section 8 |
| 22 | 5.4 |
| 23 | 5.2, step 2.3, and the 7.1 table |
