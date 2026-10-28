# llvm-msil test corpus candidates

Researched 2026-10-15. The user asked for tiny, medium and large C programs to test on, including what people compile to WebAssembly, and then for other languages, since this is an LLVM backend. The research ran two workflows: `wf_5a535ab6-7f0` (C: 128 raw candidates; fact-check found 198 claims confirmed, 33 wrong and corrected, 5 unverifiable) and `wf_d6c21212-e17` (other languages).

**Full detail** (exact harness steps, blockers, test vectors, sources) for every pick is in `TestCorpus-c.json` and `TestCorpus-nonc.json` next to this file. Read the `blockers` field before wiring a project up.

**Earliest phase** uses the plan's capabilities:
- **Phase 1** (after M10): one translation unit, output only through `puts`, `putchar` and libm; no malloc, printf/varargs, setjmp, threads or atomics.
- **"Phase 1 with a harness tweak"**: a small driver supplies a static arena, local `strlen`/`memcmp`, stubs, and so on.
- **Phase 2**: several files and the linker.
- **Phase 3**: the host libc, varargs, setjmp, TLS and atomics.
- **Phase 4**: conformance at scale and SIMD.

Some research agents compiled candidates through this backend while M9 was in progress. The JSON `blockers` and `evidence` fields say which candidates only compiled and which also ran.

The next section, "Phase 2 exit corpus", records what Phase 2 adopted from this research (M23, 2026-10-27); the sections after it are the research of 2026-10-15 as it was, with the backend issues it surfaced marked where a later milestone resolved them.

## Phase 2 exit corpus

Nine of the picks below and c-testsuite became the exit corpus of Phase 2 (plan Q29 and Q30,
milestone M23). They are never copied into the repository: `msil/utils/corpus/manifest.json`
pins them, `python msil/utils/corpus/fetch.py` downloads them into a cache outside the
repository and verifies every hash (GettingStarted.md section 7, "The exit corpus"), and
`msil/test/Programs/` builds them from that cache. The table is the manifest:

| Entry | Version | Pinned by | Licence (`license_file`) | Tree in the cache |
|---|---|---|---|---|
| zlib | 1.3.2 (tag `v1.3.2`) | commit `da607da739fa6047df13e66a2af6b8bec7c2a498`, GitHub archive | Zlib (`LICENSE`) | `zlib-1.3.2` |
| tinf | `57ffa1f1d5e3` | commit `57ffa1f1d5e3dde19011b2127bd26d01689b694b` | Zlib (`LICENSE`) | `tinf-57ffa1f1d5e3` |
| Monocypher | 4.0.2 | release tarball, SHA-256 `38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c` | BSD-2-Clause OR CC0-1.0 (`LICENCE.md`) | `monocypher-4.0.2` |
| CoreMark | `1f483d5b8316` | commit `1f483d5b8316753a742cbf5590caf5bd0a4e4777` | CoreMark Acceptable Use Agreement (EEMBC) and Apache-2.0 (`LICENSE.md`) | `coremark-1f483d5b8316` |
| LZ4 | 1.10.0 (tag `v1.10.0`) | commit `ebb370ca83af193212df4dcbadcc5d87bc0de2f0` | BSD-2-Clause for `lib/` (`lib/LICENSE`) | `lz4-1.10.0` |
| BLAKE2 | `ed1974ea8343` | commit `ed1974ea83433eba7b2d95c5dcd9ac33cb847913` | CC0-1.0 OR OpenSSL OR Apache-2.0 (`COPYING`) | `blake2-ed1974ea8343` |
| TweetNaCl | 20140427 | the two files `tweetnacl.c` and `tweetnacl.h` of `tweetnacl.cr.yp.to/20140427/` | public domain (no file) | `tweetnacl-20140427` |
| KISS FFT | `2a1f1337160e` | commit `2a1f1337160e6e8dbc6182202d0bd7f624002197` | BSD-3-Clause (`COPYING`) | `kissfft-2a1f1337160e` |
| jsmn | `25647e692c79` | commit `25647e692c7906b96ffd2b05ca54c097948e879c` | MIT (`LICENSE`) | `jsmn-25647e692c79` |
| c-testsuite | `5c7275656d75` | commit `5c7275656d751de0e68b2d340a95b5681858ed07` | MIT for the runners and scripts; each test carries the licence of its scc or tinycc original (`LICENSE`, `tests/LICENSE`, `tests/single-exec/*.c.otags`) | `c-testsuite-5c7275656d75` |

Every entry is also pinned by `tree_sha256`, the SHA-256 of its extracted tree's sorted
`<sha256> <path>` lines, because GitHub archives are not byte-stable; the six abbreviated hashes
of the recon (tests-p2 5.1) match the full commits, and the zlib and LZ4 archives at the tag
commits give the same trees as the release tarballs the recon used. The ten trees take about 19
MB in the cache, of which 4.4 MB are downloaded.

**How each program is built and what it checks** (Status.md section 1 has the details): each
links its library as a static archive with a small support archive (an arena allocator,
`memcmp`, `memset`, `memcpy`, `strlen` and output through `puts`) and compiles against shim
headers instead of a C library's, with the same flags natively and for MSIL. zlib runs 12
deflate/inflate round trips; tinf decompresses zlib's streams (two archives with members of the
same names); Monocypher self-checks every primitive family; CoreMark validates the CRCs of its
performance run; LZ4 round-trips block and HC compression; BLAKE2 checks 4 x 256 keyed
known-answer tests; TweetNaCl boxes, signs, hashes and multiplies scalars with a
`randombytes` from another object; KISS FFT runs complex and real FFTs with C math calls; jsmn
parses five documents with the parser in another object. Every transcript ends `[exit 0]`.

**Results at the Phase 2 exit** (M23, measured while it was built): all nine programs match
their native builds at `-O0` and `-O2`, through CIL objects and through LTO, on
`x86_64-pc-windows-msvc-msil` (.NET 8, .NET 10, .NET Framework 4.8) and
`x86_64-unknown-linux-gnu-msil` (WSL .NET 10 and .NET 8), with one expected transcript per
program for both triples (TweetNaCl included), and each builds byte for byte the same from two
directories. Of c-testsuite's 220 tests, the 150 whose source calls no C library function (the
recon's rule; 68 need `printf` and 2 other C library functions) are listed in
`msil/test/Programs/c-testsuite/libc-free.txt`; 149 exit 0 through objects at `-O0` and `-O2`
on `x86_64-pc-windows-msvc-msil` and .NET 8 (the `ctest` leg; no leg runs them for the Linux
triple), as natively, and one is a `diag` entry:

| Test | Phase 2 | Phase 3 item |
|---|---|---|
| `00140` | `variadic function definitions are not supported for MSIL yet` (LTO deletes the unused variadic function at `-O2`; objects keep it) | P3-B, variadic definitions |

**CI.** Whether GitHub's runners may download and run CoreMark (EEMBC's licence agreement) and
c-testsuite (per-test licences) is still unverified (plan section 10 item 11). Until the user
decides, `.github/workflows/msil-ci.yml` sets `MSIL_CORPUS_SKIP: coremark,c-testsuite`, which
reaches `fetch.py --skip` and lit's `msil_corpus_skip`: CI fetches and runs the other eight
entries, CoreMark's tests are UNSUPPORTED and the `ctest` leg is skipped. The phase gate on the
development host (`run-m1-gate.ps1`) runs all ten.

## C

### Tiny (under about 3k lines)

| Project | Size | Earliest phase | Why | WebAssembly precedent |
|---|---|---|---|---|
| [CoreMark (EEMBC), using the wasm3/wasm-coremark minimal port](https://github.com/eembc/coremark) | About 2.2k lines. | Phase 1 with a harness tweak | It is the CPU benchmark wasm runtimes use most. | wasm3/wasm-coremark. |
| [Monocypher](https://github.com/LoupVaillant/Monocypher) | monocypher.c is 2,997 lines plus monocypher.h. | Phase 1 | Needs no libc or malloc. | ar-nelson/monocypher-wasm (deno.land/x/monocypher) compiles monocypher.c and wingo's walloc.c with 'clang -c' ... |
| [TweetNaCl](https://tweetnacl.cr.yp.to/) | tweetnacl.c is 809 lines and tweetnacl.h is 272 lines (mostly macros). | Phase 1 with a harness tweak | Needs no libc, and its field arithmetic is i64-heavy. | Weak. |
| [BLAKE2 reference implementations (ref/: blake2b, blake2s, blake2bp, bl...](https://github.com/BLAKE2/BLAKE2) | ref/blake2b-ref.c is 379 lines and blake2s-ref.c is similar. | Phase 1 with a harness tweak | The self-test already reports through puts, and the code exercises 64/32-bit rotates and little-endian loads. | Not verified: argon2-browser compiles the Argon2 reference C with Emscripten and bundles the BLAKE2b reference... |
| [jsmn](https://github.com/zserge/jsmn) | jsmn.h is 471 lines. | Phase 1 | Zero libc. | None found for the C code; |
| [yxml](https://dev.yorhel.nl/yxml) | yxml.c is about 1,060 lines, generated from the 313-line yxml.c.in by yxml-gen.pl and chec... | Phase 1 | Its large generated switch state machine stresses CIL 'switch' lowering and large CFGs. | None found. |
| [tinf (tiny inflate)](https://github.com/jibsen/tinf) | src/*.c total 1,055 lines: tinflate.c (636) plus tinfzlib.c, tinfgzip.c, adler32.c and crc... | Phase 1 with a harness tweak | Needs no libc or malloc, and covers Huffman tree building, bit-level reading and table lookups. | None found. |
| [LZ4 (lib/lz4.c block format)](https://github.com/lz4/lz4) | lib/lz4.c is 2,883 lines and lz4.h is 888 (HEAD 48bfbfe). | Phase 1 with a harness tweak | A hot integer loop with unaligned loads and stores through memcpy plus ctz/clz match-length counting. | wasm-lz4 (npm; |
| [LZAV](https://github.com/avaneev/lzav) | lzav.h is about 2,970 lines. | Phase 1 with a harness tweak | On clang it uses __builtin_ctz/ctzll/clz/clzll/expect/prefetch, which exercises cttz/ctlz/prefetch lowering that few other picks reach. | README.md:13 says the library is WebAssembly-compatible and runs at roughly half native speed against WASI lib... |
| [minimp3](https://github.com/lieff/minimp3) | minimp3.h is 1,865 lines. | Phase 1 with a harness tweak | Real audio DSP: Huffman decoding, the IMDCT, polyphase synthesis and saturating float-to-int16 conversion, a mix of FP and integer code with large constant tables. | bashi/minimp3-wasm (listed among the README's bindings) builds with 'clang --target=wasm32-unknown-unknown -no... |
| [KISS FFT](https://github.com/mborgerding/kissfft) | The core is about 830 lines: kiss_fft.c 424, kiss_fft.h 184, _kiss_fft_guts.h 189, kiss_ff... | Phase 1 with a harness tweak | FP-heavy complex butterflies with recursion through kf_work, and the FIXED_POINT=16/32 builds run the same algorithm in integer arithmetic. | Not verified: kissfft-wasm (npm, Emscripten), kissfft-js (an Emscripten transpile by Chris Cannam) and webfft'... |
| [Richards benchmark (JetStream / Sightglass / llvm-test-suite copies)](https://github.com/WebKit/JetStream/tree/main/wasm/richards) | The JetStream richards.c is 375 lines, the Sightglass copy 390 and the llvm-test-suite cop... | Phase 1 with a harness tweak | Struct- and pointer-heavy code with indirect calls through stored function pointers, an early check of ldftn/calli, with deterministic qpkt_count and hold_count results. | JetStream 2/3 richards-wasm (built with emcc); |
| [stb_sprintf](https://github.com/nothings/stb/blob/master/stb_sprintf.h) | stb_sprintf.h, 1,906 lines. | Phase 3 | The ideal first varargs test for Phase 3: variadic definitions plus va_list/va_arg over ints, long longs, doubles, pointers and strings, with no libc dependency and output that diffs byte-for-byte against native. | stb headers are common in Emscripten builds; |

### Medium (about 3k-60k lines)

| Project | Size | Earliest phase | Why | WebAssembly precedent |
|---|---|---|---|---|
| [zlib (Z_SOLO core)](https://github.com/madler/zlib) | The v1.3.2 core .c files total 7,703 lines: adler32 164, compress 99, crc32 983, deflate 2... | Phase 1 with a harness tweak | Probably the most-ported C library in wasm, and integer-heavy (bit I/O, hashing, Huffman trees, CRC tables). | JetStream 3 (released 2026-03) zlib-wasm, built with 'emcc -s USE_ZLIB=1' around a zpipe.c driver that uses st... |
| [bzip2 / libbz2](https://sourceware.org/bzip2/) | libbz2 1.0.8 is 4,383 lines in 7 .c files (blocksort 1,094, bzlib 1,572, compress 672, dec... | Phase 1 with a harness tweak | Block sorting with an explicit-stack quicksort and fallback sort over large arrays, plus Huffman coding, MTF and CRCs, all pointer- and index-heavy. | The Emscripten port -sUSE_BZIP2: tools/ports/bzip2.py builds exactly the 7 library sources (bzip2 1.0.6, with ... |
| [Zstandard single-file decoder (zstddeclib.c)](https://github.com/facebook/zstd/tree/dev/build/single_file_libs) | The generated zstddeclib.c is about 22,271 lines (29 files inlined, zstd.h included) at v1... | Phase 1 with a harness tweak | A modern codec that browsers run through wasm, already published as a single TU, covering 64-bit bit-stream reading, FSE/Huffman tables and wildcopy loops. | zstddec (npm, donmccurdy) runs create_single_file_decoder.sh and then 'emcc zstddeclib.c ... |
| [Brotli decoder (c/common + c/dec)](https://github.com/google/brotli) | About 8.8k lines of decoder code on master: c/common .c 1,064, c/dec .c 3,745 (decode.c 3,... | Phase 1 with a harness tweak | Its 122 KB constant dictionary and transform tables stress the data image (the RVA blob and .cctor copy) far more than typical programs do. | Upstream .github/workflows/build_test_wasm.yml ('Build/Test WASM', EM_VERSION 3.1.51, BUILD_TARGET wasm32). |
| [MD4C (Markdown parser)](https://github.com/mity/md4c) | About 10.8k lines in 6 src files: md4c.c 7,386, md4c.h 495, md4c-html.c 658, md4c-html.h 6... | Phase 1 with a harness tweak | A real parser with pointer-heavy line and span processing, mark stacks and output through function-pointer callbacks. | markdown-wasm (rsms; |
| [Expat (XML parser)](https://github.com/libexpat/libexpat) | 2.9.0 lib/ .c files: xmlparse.c 9,705, xmltok.c 1,596 and xmlrole.c 1,256, plus xmltok_imp... | Phase 3 | A widely embedded parser with a switch-based tokenizer, per-encoding tables of function pointers and a memory suite of function pointers. | @hpcc-js/wasm-expat (Expat 2.8.2 via emsdk; |
| [tree-sitter runtime + one generated grammar](https://github.com/tree-sitter/tree-sitter) | The runtime is about 20k lines in 34 files (estimate, not measured). | Phase 3 | Generated grammars are the best available stress test for huge switch statements (ts_lex) and very large static uint16 tables in the data image, and the GLR runtime is pointer-heavy (ref-counted subtrees, stack versions)... | web-tree-sitter (npm) built with Emscripten; |
| [wasm3 (WebAssembly interpreter)](https://github.com/wasm3/wasm3) | main: source/ has 49 .c/.h files and 36,367 lines, about 30.0k of them outside the m3_api_... | Phase 3 | Running WebAssembly inside .NET through this compiler is the most on-theme test there is. | Not verified: wasm3 is itself compiled to wasm and can run itself, and webassembly.sh runs it in the browser. |
| [Lua 5.4 (onelua.c single-TU build)](https://github.com/lua/lua) | 5.4.9's .c/.h files total 29,450 lines (excluding ltests and onelua.c). | Phase 3 | A real VM in one TU, with switch or computed-goto dispatch, setjmp-based errors, a GC over pointer-heavy object graphs and a deeply recursive parser. | wasmoon (npm, Lua 5.4): its build.sh runs emcc over the official lua/lua sources except lua.c and luac.c. |
| [Box2D v3 (C library and unit tests)](https://github.com/erincatto/box2d) | main: src 38,622 + include 5,726 lines (44.3k), plus test 10,151 and shared 2,867 (57.4k i... | Phase 3 | FP-heavy simulation with a built-in cross-platform determinism hash, the strongest FP-equivalence check on the list (it catches contraction, conversion and ordering differences). | Upstream build_emscripten.sh (present on main and at v3.1.1) builds the unit tests with Emscripten. |
| [jq](https://github.com/jqlang/jq) | About 28k lines in 44 src/*.c/.h files (estimate, not measured), plus vendored decNumber a... | Phase 3 | tests/jq.test holds 550 deterministic program/input/expected-output cases at jq-1.8.2 (596 on master), a strong golden-output corpus. | Not verified: jq-web (npm; |
| [libsodium (with its test/default golden suite)](https://github.com/jedisct1/libsodium) | src/libsodium has 143 .c files on stable (145 on master) plus 6 .S files, about 59k lines ... | Phase 3 | A long-running cross-runtime wasm benchmark whose every test output is byte-compared to a golden .exp file. | Sightglass benchmarks/libsodium builds one .wasm per test/default file (Zig 0.12.0, libsodium 1.0.20-RELEASE, ... |
| [Berkeley SoftFloat 3e (+ TestFloat 3e as an oracle)](https://github.com/ucb-bar/berkeley-softfloat-3) | master: 326 .c files directly in source/ (32,184 lines), plus 8086-SSE (27 files) and incl... | Phase 1 with a harness tweak | It needs only stdbool/stdint and 64-bit integers, so it is a heavy deterministic integer workload. | None found. |
| [musl libm (src/math)](https://git.musl-libc.org/cgit/musl/tree/src/math) | At v1.2.6 the top level of src/math has 232 .c and 10 .h files, 17,883 lines (counted on w... | Phase 1 with a harness tweak | Every wasi-sdk C program runs this code, and its dense bit-level FP (union punning, FORCE_EVAL volatile tricks, double-double evaluation, exact rounding) finds FP codegen bugs that simple programs miss. | wasi-libc's libc-top-half/musl has been a git subtree of upstream musl since April 2026, with VERSION 1.2.6; |

### Large

| Project | Size | Earliest phase | Why | WebAssembly precedent |
|---|---|---|---|---|
| [SQLite (amalgamation)](https://sqlite.org/amalgamation.html) | 3.54.0: sqlite3.c 271,072 lines, shell.c 39,899, sqlite3.h 14,447, sqlite3ext.h 745. | Phase 3 | The biggest real-world single TU, with a huge VDBE dispatch switch, function-pointer VFS and method tables, 64-bit integer and FP formatting and its own printf engine over va_list. | Emscripten's test_benchmark.py builds sqlite as C, and Sightglass lists a sqlite3 benchmark (as prebuilt .wat)... |
| [QuickJS (Bellard; the QuickJS-ng fork ships an amalgamation)](https://github.com/bellard/quickjs) | 2026-06-04 release: quickjs.c is 61,197 lines. | Phase 3 | A full language engine: deep recursion (parser and interpreter re-entry), alloca'd interpreter frames, switch or computed-goto dispatch, and 128-bit BigInt limbs. | QuickJS-ng v0.17.0 (2026-09-18) ships a quickjs-amalgam.zip asset (verified). |
| [DOOM via doomgeneric](https://github.com/ozkl/doomgeneric) | HEAD e257a09 (2026-10-11): doomgeneric/ has 95 .c files and 73,140 lines of .c+.h. | Phase 3 | The canonical 'it runs Doom' milestone: fixed-point integer math, recursive BSP traversal, function-pointer thinkers and big switches over a lot of global state. | diekmann/wasm-fizzbuzz builds id's linuxdoom-1.10 (not doomgeneric) for wasm32 with plain clang and no Emscrip... |
| [TCC (Tiny C Compiler)](https://repo.or.cz/tinycc.git) | Mirror HEAD 43c7708 (2026-10-03), VERSION 0.9.28rc: the top-level .c+.h files total 68,684... | Phase 3 | A compiler compiled by our compiler: huge tokenizer and codegen switches, a deeply recursive parser, pointer-heavy symbol tables, and object output that can be byte-compared with a native TCC. | TCC compiled to wasm32-freestanding with Zig to cross-compile RISC-V in the browser (lupyuen, 'TCC RISC-V Comp... |
| [Opus (libopus, fixed-point build)](https://github.com/xiph/opus) | HEAD 51bd2e6 (2026-10-01): celt, silk, src and include hold 316 .c/.h files and 91,239 lin... | Phase 3 | Fixed-point DSP (32x16 multiplies, saturating arithmetic, many small inline helpers) whose integer output should match a native build of the same configuration byte for byte. | Not verified: opus-decoder and ogg-opus-decoder (wasm-audio-decoders, Emscripten); |
| [Oniguruma](https://github.com/kkos/oniguruma) | HEAD f95747b: src/*.c+*.h total 92,484 lines (57 .c files, much of it Unicode tables). | Phase 3 | A backtracking regex VM with direct-threaded (computed goto) dispatch, a recursive pattern compiler and very large Unicode tables, run as wasm for syntax highlighting in VS Code (web) and Shiki. | vscode-oniguruma (Microsoft): deps/oniguruma is upstream kkos/oniguruma, built by scripts/build.sh with emcc p... |
| [Graphviz (C core: cgraph, cdt, common, dotgen, neatogen, gvc, core plu...](https://gitlab.com/graphviz/graphviz) | lib/ C is about 123k lines in 408 files excluding the C++ directories (not re-measured), p... | Phase 3 | FP-heavy layout (network simplex, spline routing) whose 'dot -Tplain' and SVG output is text that can be diffed against native, over pointer-heavy graphs with cdt dictionaries driven by function-pointer disciplines. | @hpcc-js/wasm-graphviz (Graphviz 16.1.0; |
| [FFmpeg (C-only decoder/demuxer subset, --disable-asm)](https://github.com/FFmpeg/FFmpeg) | About 1.4M lines of C plus 161k lines of NASM (Debian 9.0.2 counts, not re-measured). | Phase 3 | The largest real-world C codebase people ship as wasm, with integer DSP, bit readers and huge function-pointer registries for codecs and formats. | ffmpeg.wasm (@ffmpeg/core and @ffmpeg/core-mt; |
| [libxml2 (+ xmllint)](https://gitlab.gnome.org/GNOME/libxml2) | HEAD c43dc98 (2026-09-23, VERSION 2.16.0): meson.build lists 19 always-built and 20 option... | Phase 3 | A mature, heavily tested library with one of the best golden-output harnesses (runtest diffs against result/), covering recursive-descent parsing, the XPath evaluator (FP and recursion) and hash tables and dictionaries. | xmllint-wasm (Emscripten; |
| [CPython (WASI-style static build)](https://github.com/python/cpython) | About 658k lines of C (Debian 3.13 count, not re-measured) plus the Python standard librar... | Phase 4 | The capstone interpreter. | PEP 11 lists wasm32-unknown-wasip1 as Tier 2 (WASI SDK, Wasmtime) and wasm32-unknown-emscripten as Tier 3. |
| [PostgreSQL in single-user mode (as PGlite does it)](https://github.com/postgres/postgres) | About 950k lines of C plus 121k lines of SQL tests (Debian postgresql-17 counts, not re-me... | Phase 4 | The flagship example of 'people now run X in wasm', using sigsetjmp-based error handling (PG_TRY/ereport) at scale, with pg_regress as a huge deterministic golden-SQL corpus. | PGlite (ElectricSQL, Emscripten): its README says it runs Postgres in single-user mode with no fork, one user ... |

### Benchmark and conformance suites

| Project | Size | Earliest phase | Why | WebAssembly precedent |
|---|---|---|---|---|
| [GCC C torture tests (gcc.c-torture/execute)](https://gcc.gnu.org/git/?p=gcc.git;a=tree;f=gcc/testsuite/gcc.c-torture/execute) | 1,699 top-level execute/*.c files, plus ieee/ (78 .c) and builtins/ (59 tests, each a test... | Phase 1 with a harness tweak | The densest compiler-bug finder available: about 1,700 self-checking corner-case programs (integer promotion, bitfields, struct passing, switch lowering, overflow, FP edge cases), each run at every optimization level. | Strong. |
| [c-testsuite](https://github.com/c-testsuite/c-testsuite) | 220 .c files (219 also have .c.otags), from a few lines up to a few hundred each. | Phase 1 | A simple pass rule (exit 0 and byte-equal output) and libc-free majority make it an early conformance check of basic-to-intermediate language features that small compilers often get wrong. | Weak. |
| [Csmith (differential random testing)](https://github.com/csmith-project/csmith) | Unlimited. | Phase 1 with a harness tweak | An unlimited stream of UB-free programs whose checksum must match native clang. | Not verified: Emscripten's tests/fuzz shipped csmith_driver.py, creduce_tester.py and csmith.h, and LLVM bug 2... |
| [Sightglass benchmarks (C subset: 19 shootout kernels plus quicksort an...](https://github.com/bytecodealliance/sightglass/tree/main/benchmarks) | Each program is one .c file of about 50-800 lines plus sightglass.h. | Phase 1 with a harness tweak | Small, real kernels straight from the wasm runtime world, covering 64-bit crypto (ed25519, keccak, chacha, gimli), recursion (ackermann, fib2), switch dispatch, memory moves, sorting and function pointers (richards). | Built with wasi-sdk (Dockerfile.wasi-sdk); |
| [PolyBench/C 4.2.1](https://github.com/MatthiasJReisinger/PolyBenchC-4.2.1) | About 6.8k lines across 64 .c/.h files: 30 kernels plus utilities/polybench.c (about 550 l... | Phase 1 with a harness tweak | The most-used C suite in wasm performance research. | Not individually verified: 'Not So Fast' (Jangda et al., USENIX ATC 2019); |
| [TACLeBench](https://github.com/tacle/tacle-bench) | Master has 60 benchmarks: kernel 29, sequential 23, app 2, test 3, parallel 3. | Phase 1 | Its paper's abstract describes the programs as free of standard-library and OS dependencies, which makes this the best Phase 1 fit on the list. | None found. |
| [Embench-IoT](https://github.com/embench/embench-iot) | master src/ has 19 programs: aha-mont64, crc32, depthconv, edn, huffbench, matmult-int, md... | Phase 1 with a harness tweak | Self-verifying real embedded programs (main returns !correct) with a static heap and no printf. | None found. |
| [llvm-test-suite SingleSource/Benchmarks (C subset), then UnitTests/Mul...](https://github.com/llvm/llvm-test-suite/tree/main/SingleSource/Benchmarks) | 142 programs: 101 C (BenchmarkGame 8, CoyoteBench 3, Dhrystone 2, Linpack 1, McGill 4, Mis... | Phase 3 | About 100 C programs with reference_output files and an existing lit/fpcmp runner that handles FP tolerances, including the classic Dhrystone, Whetstone and LINPACK. | WarpDiff (Rao et al., 'Revealing Performance Issues in Server-side WebAssembly Runtimes via Differential Testi... |
| [Emscripten's own test corpus (C subset of test/core, test/benchmark, t...](https://github.com/emscripten-core/emscripten/tree/main/test) | test/core top level: 262 .c, 96 .cpp and 297 .out files (282 .c and 311 .out including pth... | Phase 3 | The tests target failure modes a wasm-shaped target shares (unaligned access, i64 handling, function-pointer casts, varargs, setjmp, struct ABI, printf corner cases), and the golden .out files plug straight into a diff r... | This is Emscripten's own CI corpus. |

**Dropped, with reasons (selection):** C++ projects and the C++ parts of mixed ones; SPEC (license); PHP (needs GCC global register variables and musttail), CRuby (its conservative GC scans the machine stack), Redis and BusyBox (fork, sockets, signals); Quake (32-bit only); and many smaller overlaps, such as cJSON vs jsmn, miniz vs zlib, BLAKE3 vs BLAKE2. The full list is in `TestCorpus-c.json` under `final.dropped`.

## Other languages (through LLVM IR or bitcode)

| Language | Verdict | How its IR gets in | Earliest phase | Picks |
|---|---|---|---|---|
| Rust | Good fit, and the best non-C source for Phase 1. | rustc --edition 2021 --crate-type staticlib -C panic=abort -C opt-level=2 -C lto=fat -C codegen-units=1 -C llvm-args=-mergefunc-use-aliases=false --emit=llvm-bc... | Phase 1 for no_std plus alloc. | Hand-written no_std smoke programs, rustc_codegen_gcc tests/run (plus rustc_codegen_cr..., libm crate (Rust port of musl libm), RustCrypto sha2 + chacha20 with ed25519-dalek/curv..., wasmi WebAssembly... |
| Zig | Good fit, and the strongest non-Rust candidate. | zig build-obj (or zig test with a custom --test-runner) -target x86_64-freestanding-none -mcpu=baseline -fsingle-threaded -OReleaseSmall (or ReleaseFast) -femit... | Phase 1 | Hand-written freestanding Zig smoke programs, Zig test/behavior with a custom single-threaded te..., Zig lib/compiler_rt and its tests, Zig std unit tests for OS-independent modules (std... |
| Fortran (Flang, with flang-rt for Phase ... | Good fit for computational kernels in Phase 1, and already verified: BLAS, the LAPACK DGESV chain and MINPACK all ran on .NET 8. | Stock flang 23.1.2: flang -c -emit-llvm -O2 --target=x86_64-pc-windows-msvc. | Phase 1 for kernels with a bind(c) driver and stubs. | Reference BLAS (Reference-LAPACK BLAS/SRC), Reference LAPACK, solver subsets now, whole librar..., fortran-lang/minpack (modern MINPACK), TSVC (netlib vectord) and LINPACK (linpackd) kerne..., LLVM te... |
| D (LDC -betterC) | Good fit for Phase 1. | ldc2 -betterC -mtriple=x86_64-pc-windows-msvc -O2 -output-bc -checkaction=halt (flags verified in the LDC source). | Phase 1 | Hand-written betterC programs, DMD testsuite runnable -betterC tests |
| Odin | Good fit for Phase 1. | odin build x.odin -file -target:freestanding_amd64_win64 (x86_64-pc-windows-msvc) or freestanding_amd64_sysv (x86_64-pc-none-gnu) -build-mode:llvm-ir -no-crt -n... | Phase 1 | examples/demo/demo.odin (freestanding subset), Odin tests/core (crypto, hash, math, compress, str... |
| C3 | Possible. | Build c3c against this fork (CMakeLists.txt allows LLVM 19-24). | Phase 1 (bare). | Hand-written bare C3 programs, c3c test/unit (stdlib unit tests) |
| Ada (GNAT LLVM) | Possible but costly. | Build gnat-llvm (its README clones current gcc master for the front end). | Phase 1 for the light runtime. | Hand-written Ada programs on the light runtime, ACATS executable tests (no-propagation subset) |
| Swift (Embedded Swift) | Possible after one backend change. | swiftc -enable-experimental-feature Embedded -wmo -target x86_64-unknown-linux-gnu -O -emit-bc. | Phase 1 plus swiftcc support (not scheduled). | Hand-written Embedded Swift programs, swift/test/embedded executable lit tests |
| Go (TinyGo) | Possible, but only with two TinyGo patches. | tinygo build -o out.bc with a custom target JSON (llvm-target x86_64 Linux or Windows, gc leaking, scheduler none, a custom build tag), then the fork's llc -mtr... | Phase 1 with the recover and main patches (leaking GC). | TinyGo testdata golden-output programs, tinygo test on pure stdlib packages |
| Nim | Possible through the C backend. | nim c --compileOnly --nimcache:dir --mm:arc --threads:off --exceptions:goto -d:useMalloc --panics:on --os:any x.nim. | Phase 1 with shims. | Hand-written Nim programs (goto exceptions, ARC, c..., Nim testament tests/ subset |
| Scheme (CHICKEN, Gambit) | Possible for Phase 3, and valuable as a stress test of Phase 3 features. | CHICKEN: chicken x.scm -output-file x.c, then the fork's clang for the msil triple (or bitcode plus llvm-link with libchicken built as bitcode), then llc. | Phase 3 | CHICKEN chicken-core tests, Gambit unit-tests (and r7rs-benchmarks programs) |
| Koka | Possible in Phase 3. | koka -c (C goes into .koka/), then the fork's clang or stock clang to bitcode, llvm-link with kklib built as bitcode, then llc -mtriple=...-msil. | Phase 3. | Koka test/bench programs, Koka test/ expected-output suite |
| Standard ML (MLton) | Possible but low priority (Phase 3/4). | mlton -codegen llvm with the defaults -chunk-tail-call false and -llvm-cc10 false. | Phase 3 | MLton regression suite |
| Pascal (Free Pascal LLVM codegen) | Poor fit without RTL work, and exceptions never work. | Build FPC with LLVM=1 on Linux or WSL. | Phase 3 for exception-free programs with a patched RTL. | FPC tests/ (tbs/webtbs subset) |
| V | Possible but low priority (Phase 3). | v -gc none -o out.c x.v for a Linux target, then the fork's clang for the msil triple, then llc. | Phase 3 | vlib/v/tests with -gc none |
| Julia (StaticCompiler.jl) | Possible as a narrow Phase-1 niche: type-stable, allocation-free numeric kernels only. | Get the LLVM module with static_llvm_module or compile_llvm_module and a StaticTarget for an x86_64 triple, write it as bitcode, then llc -mtriple=...-msil. | Phase 1 | StaticCompiler/StaticTools example kernels |
| Lean 4 | Poor fit. | lean -c out.c, then clang to bitcode with the Lean runtime. | Phase 4 at the earliest | - |
| Crystal | Poor fit. | Not recommended. | Never in practice | - |
| Pony | Possible later, low priority. | Not yet assessed. | Phase 3 at the earliest | - |
| Kotlin/Native | Poor fit. | Not recommended. | Never in practice | - |
| Haskell (GHC) | Poor fit. | Not recommended. | Never for -fllvm. | - |
| Roc | Unknown, worth re-checking. | Check whether the roc CLI can emit or keep the .bc. | Unknown (Phase 1 in principle) | - |
| Vala | Poor fit. | Not recommended. | Phase 3 | - |

## Backend issues the research surfaced (2026-10-15; how each was resolved)

- **Aliases:** rustc merges identical functions into IR aliases, and MSIL rejects every GlobalAlias. The workaround is `-C llvm-args=-mergefunc-use-aliases=false`; lowering aliases would fix it properly. *Resolved in M12:* whole-program output lowers aliases of defined functions and variables, and CIL objects keep them as alias symbols for `msil-ld` (M19).
- **i128 division:** u128/i128 division by a non-power-of-two, and float-to-i128 conversions, become `__udivti3`/`__fixunsdfti`, which we report as Phase-3 helpers. NVPTX and AMDGPU set `setMaxDivRemBitWidthSupported(64)` and `setMaxLargeFPConvertBitWidthSupported(64)` so that LLVM expands these inline (ExpandIRInsts). It could be done the same way here. *Resolved in M12:* expanded inline exactly that way (plan Q36).
- **`black_box`:** Rust's `black_box` becomes an empty `asm sideeffect` with inputs and a memory clobber, which MSILPrepareIR rejects. It could be accepted as a compiler barrier. *Resolved in M12:* empty `asm` statements with operands are accepted (and since M22 templates of nothing but `/* */` comments in IR).
- **Swift:** Embedded Swift uses the swiftcc calling convention, which `callingConvSupported` rejects. *Moved to phase 4 or dropped* (plan Q34).
- **Rust slice and str compares:** these need `memcmp`, which is not a Phase-1 import. Define it in the program, or import it explicitly. *Since M15* a program that `msil-ld` links binds `memcmp` to the C library like any other native function; only `clang -S` keeps the Phase-1 import table.
- **Never planned, and the cause of most "poor fit" verdicts:** exception unwinding (C++ exceptions, Rust panic=unwind) and GCs that scan the machine stack.
