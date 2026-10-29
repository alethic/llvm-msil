# Phase 3 recon: host-libc conformance (zlib, Lua, SQLite)

- Key: `hosted-libc`. Phase 3 item: P3-B's "glibc and UCRT host-libc conformance (zlib, Lua, SQLite)". Date 2026-10-28.
  Checkout: branch `msil` at `cc4c79787d6a` (M23). Scratch: `C:\llvm-msil-scratch\p3\hosted-libc`.
- Supporting files: [hosted-libc-files/](hosted-libc-files/): the build and measurement scripts (`tools/`), the
  stand-ins that simulate Phase 3 features (`sim/`), the probes (`probes/`, `il/`), the Lua and SQL drivers (`data/`),
  the proposed corpus manifest entries and the script that verified them (`corpus/`), and the raw results
  (`results/`: every diagnostic summarized per program and triple, the static inventories, the probe outputs, the
  output comparison and the timing log). No binaries.
- Method: real hosted builds (real glibc 2.43 headers from `C:\llvm-msil-scratch\glibc-sysroot` with `--sysroot`;
  the UCRT 10.0.26100 and MSVC 14.51 headers from `msil-env.ps1`'s `INCLUDE`), `-O2`, linked by `msil-ld` through
  the clang driver with its default libraries. Every run is compared with a native build of the same sources
  (`clang --target=x86_64-pc-windows-msvc -fuse-ld=lld`; `clang --target=x86_64-unknown-linux-gnu` plus `ld.lld`
  against the same sysroot, run in WSL). Runtimes: .NET Framework 4.8, .NET 8 and .NET 10 on Windows; the private
  .NET 8.0.31 and 10.0.13 under WSL Ubuntu (glibc 2.43). Where today's toolchain stops, the missing Phase 3 feature
  was **simulated** (section 1.3) to find what lies behind it; every simulation is named where a result depends on it.
- The host was shared and heavily loaded throughout (other projects' builds). Wall times carry +-50 % noise and
  include the harness's copies and process launches; CPU times reported by the programs themselves are steadier.
- No tracked file was changed; nothing was built in `D:\llvm-msil\build`; no tool was installed. Downloads: Lua
  5.4.9 and its tests, SQLite 3.54.1 (amalgamation and autoconf tarball), Lua 5.5.1 and its tests (section 1.1).
  zlib 1.3.2 is the existing corpus entry, byte-identical to `C:\llvm-msil-scratch\corpus-cache\zlib-1.3.2`. The
  variadics recon's private `ExpandVariadics` tool (`C:\llvm-msil-scratch\p3\variadics\proto`) was reused, not
  rebuilt. A first attempt of this recon (stopped by the user) did most builds; this run reused them, re-ran every
  measurement quoted below, fixed one of its probes (`lasterr.c` had `"\n"`/`"\f"` escapes in its paths) and added
  the rest.

---

## 0. Summary

1. **Today, nothing hosted links.** All three programs stop at P3-B variadics on both triples: zlib in 4 of 17
   files (62 diagnostics on linux-gnu, 8 on windows-msvc), Lua in 25 of 34 files (378 / 213), SQLite in both of its
   two files (487 / 432). On `x86_64-pc-windows-msvc-msil` the dominant error is a **definition**: the UCRT headers
   define the whole `printf`/`scanf` family inline (`stdio.h:830` `fprintf`, `:950` `printf`, `:1764` `sprintf`,
   `:1919` `snprintf`, `:2240` `sscanf`), each a `linkonce_odr` variadic function that calls
   `__stdio_common_v*(options, stream, format, locale, va_list)`. Variadics mask the next layers: `--lto-emit-asm
   --error-limit=0` and the simulations below show them.
2. **Behind variadics** (measured with the variadics recon's `ExpandVariadics` prototype, section 1.3):
   - **zlib full** (all 15 library files with `gzio`, `example`, `minigzip`) needs **nothing else**. It links on both
     triples and matches the native build byte for byte on all five runtime configurations (`example`'s transcript;
     `minigzip` compressing 2 MB, and decompressing the native stream back to the input).
   - **Lua 5.4.9** needs, in addition: **setjmp/longjmp** (windows-msvc: `llvm.frameaddress` from
     `_setjmp(buf, frameaddress(0))` in `luaD_rawrunprotected`, then `undefined symbol: _setjmp`/`longjmp`; linux-gnu:
     **no diagnostic at all**, `msil-ld` binds glibc's `_setjmp`/`_longjmp` as P/Invokes), and a **callback**
     (`signal(SIGINT, laction)` on Windows, `sigaction` on Linux, from `lua.c`). With `signal`/`sigaction` stubbed, the
     Linux build passes **Lua's entire portable test suite** (`all.lua` with `_U=true`, `final OK !!!`) on WSL .NET 8
     and .NET 10, through native glibc `_setjmp`, which works by accident (the sjlj recon's finding, here on a whole
     interpreter). The same on Windows through vcruntime's `setjmp` dies (`exit 127` with no output on .NET 8/10, a
     segfault at the first coroutine yield on Framework). Without errors or yields (`prog1.lua`), Windows Lua matches
     native on all three runtimes **except one line**: section 5.3's rounding finding. Lua 5.5.1 behaves the same
     (its test suite passes on WSL .NET 8 under the same simulation).
   - **SQLite 3.54.1** (amalgamation and its shell) needs, in addition: **atomics** (relaxed `__atomic_load_n`/
     `__atomic_store_n` from `AtomicLoad`/`AtomicStore`, 98-110 sites, which `SQLITE_THREADSAFE=0` does **not**
     remove; 80 system fences on Linux, 82 single-thread fences on Windows, 2 `cmpxchg` on Windows), **callbacks**
     (`atexit`, `signal` x3 or `SetConsoleCtrlHandler`, and `pthread_create`/`_beginthreadex` x3 for worker threads
     that never start at the default `SQLITE_DEFAULT_WORKER_THREADS=0`), and three items no other recon names:
     **`wmain`** (the Windows shell's entry point; `msil-ld` knows only `main`), **SEH** (`__try`/`__except`, which
     SQLite enables by default for `_MSC_VER`: `frame escape ('llvm.localescape') is not supported for MSIL`;
     `-DSQLITE_OMIT_SEH` removes it), and the **address of a native fixed-arity variadic function** stored in a table
     (`aSyscall[]` holds `fcntl64` and `mremap`, called through `int(*)(int,int,...)`). With those simulated, the shell
     runs three SQL scripts (2,000-row table with indexes, printf/JSON/date functions, a savepoint rollback,
     `integrity_check`; 20,000 rows in WAL mode with a checkpoint and `VACUUM`; float formatting edge cases) and
     matches native on all five configurations (Windows modulo CR, caused only by the `wmain` stand-in).
3. **New findings** (none is in another P3 recon):
   - **UCRT stdio options (section 5.3).** The inline `printf` family passes `*__local_stdio_printf_options()`, a
     per-program variable that the native CRT startup (`__scrt_initialize_default_local_stdio_options`, vcruntime's
     `exe_common.inl:206-210`) sets to `0x24` (`_CRT_INTERNAL_PRINTF_LEGACY_WIDE_SPECIFIERS |
     _CRT_INTERNAL_PRINTF_STANDARD_ROUNDING`; scanf `0x2`). MSIL programs never run that code: the options stay `0`
     and every MSIL printf on windows-msvc rounds the **legacy** way (`%.0f` of 0.5 prints `1`, of 2.5 prints `3`;
     native `0` and `2`; Lua's `%5.2f` of 123456789.125 prints `...13` against native `...12`). Measured on .NET 8,
     .NET 10 and Framework; a constructor that sets the two words makes every output match.
   - **glibc data imports pin GLIBC_2.34 (section 6.4).** Every hosted Linux program imports `stdin`/`stdout`/
     `stderr` as data (M20), and with no reference that defines `NativeLibrary` the import helper P/Invokes
     `libc.so.6!dlopen`/`dlsym`, which exist in `libc.so.6` only from glibc 2.34 (`--print-imports`: `libc.so.6:
     version floor GLIBC_2.34`). `libdl.so.2!dlopen` binds on glibc 2.43 too (measured: `dlsym` searches libdl's
     `DT_NEEDED` libc), and on older glibc libdl defines it, so one fixed module name removes the floor.
   - `errno` through `__errno_location()`/`_errno()` and Win32 `GetLastError()` survive between P/Invokes (0
     mismatches in 300,000 iterations on every runtime, first call included).
   - `_FORTIFY_SOURCE=2`/`3` needs no `__builtin_va_arg_pack`: glibc 2.43's clang branch (`__fortify_use_clang`)
     defines `printf` & co. as `static` overloadable variadic functions over `__v*_chk`, the same shape as UCRT's
     inline printf; with expanded variadics the fortified program matches native on WSL.
4. **Ordering (section 8).** Variadics unblock zlib completely and are the first wall for all three; SjLj and one
   callback (`signal`) unblock Lua; atomics, callbacks (plus P3-A's `atexit`), `wmain`, `-DSQLITE_OMIT_SEH` and the
   fixed-arity address adapter unblock SQLite. Recommended exit checks: **zlib full** for the variadics milestone,
   **Lua** (its test suite) for the setjmp milestone, **SQLite** for the atomics/callbacks milestone, and all three
   on both triples, both levels, objects and LTO and every runtime for P3-B's host-libc exit. Embedding drivers that
   avoid `lua.c`'s `signal` and `shell.c`'s `signal`/`atexit`/`wmain` let Lua and SQLite enter one milestone earlier.
5. **Corpus additions (section 9):** `lua` 5.4.9 and `lua-tests` 5.4.9 (MIT) and `sqlite` 3.54.1 (public domain) as
   `manifest.json` v1 entries, verified end to end by the tree's own `fetch.py` against a scratch cache (3.5 MB in
   12 s); zlib reuses the existing `zlib` 1.3.2 entry. All three licences allow CI. Build cost per triple and level:
   zlib ~10 s CPU, Lua ~25-31 s, SQLite ~60-75 s (one TU, `sqlite3.c`, 47-49 s for `-flto -c` alone); runs 1-5 s
   each, the Lua suite 3.4-4.7 s CPU (native 2.0-3.0 s), SQLite ~1.1-1.3 s of startup and JIT per shell process.

---

## 1. Programs, sources and method

### 1.1 Sources (fetched into `C:\llvm-msil-scratch\p3\hosted-libc\src`, never into the repository)

| Program | Version | URL | SHA-256 of the download | Licence |
|---|---|---|---|---|
| zlib | 1.3.2 (commit `da607da7`) | the existing `manifest.json` entry `zlib` (GitHub archive) | tree `adf94363...` (existing entry) | Zlib |
| Lua | 5.4.9 | `https://www.lua.org/ftp/lua-5.4.9.tar.gz` | `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6` | MIT (`src/lua.h`, end of file) |
| Lua tests | 5.4.9 | `https://www.lua.org/tests/lua-5.4.9-tests.tar.gz` | `7d971845f545ffc09fbb3128a86b2c6524161c70d0fdf0154a16e8c00c343fca` | MIT (`all.lua`, end of file) |
| SQLite | 3.54.1 | `https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip` | `0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9` | public domain |
| SQLite (not used) | 3.54.1 | `https://www.sqlite.org/2026/sqlite-autoconf-3540100.tar.gz` | `926ef8d3eab93fbf6e06011901273eec076b25594a4e1cdadd50c987f4f6d64e` | public domain |
| Lua (check) | 5.5.1 | `https://www.lua.org/ftp/lua-5.5.1.tar.gz` | `1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce` | MIT |
| Lua tests (check) | 5.5.1 | `https://www.lua.org/tests/lua-5.5.1-tests.tar.gz` | `da07b543872dc0bb2ff12aabd0c248578d78df3eb6b67efdc537a46d455c7f31` | MIT |

lua.org lists 5.4.7, 5.4.8, 5.4.9, 5.5.0 and 5.5.1; sqlite.org's download page lists 3.54.1 as current.

### 1.2 Builds

`tools/hl.py build <prog> <win|lin>` compiles every file three ways (`-c` to a CIL object, `-flto -c` to bitcode,
`-flto -S` to IR for the inventory), then links objects and bitcode through the clang driver (`msil-ld`, default
libraries, `--print-imports`) and once more with `-Wl,--lto-emit-asm -Wl,--error-limit=0`, which binds natively
before code generation and so reports the binding layer (setjmp, callbacks, `main`) that the code-generation layer
(variadics) hides. `tools/diag.py` counts the diagnostics. Flags (beyond `--target`, `-O2`, `--sysroot` on Linux):

| Program | Files | windows-msvc | linux-gnu |
|---|---|---|---|
| zlib | 15 library files (`adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil`) + `test/example.c`, `test/minigzip.c` | `-D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE` | `-DZ_HAVE_UNISTD_H -D_LARGEFILE64_SOURCE=1` |
| Lua | 32 core and library files + `lua.c`, `luac.c` | `-DLUA_COMPAT_5_3` (luaconf.h selects `LUA_USE_WINDOWS`) | `-DLUA_COMPAT_5_3 -DLUA_USE_LINUX -std=gnu99`, `-lm -ldl` (the Lua makefile's) |
| SQLite | `sqlite3.c`, `shell.c` | defaults | defaults, `-lm -lpthread -ldl` |

### 1.3 The simulation of Phase 3 features ("peeling")

To see past each wall, the simulated builds (`tools/hl.py pbuild`, `tools/pv.sh`) run `clang -S -emit-llvm`, the
variadics recon's `ExpandVariadics` prototype (`msil-expand-variadics.exe`: LLVM's pass with MSIL ABIs, its built-in
fixed-arity list and v-variant table), then the shared `llc -O2 -filetype=obj`, and link the CIL objects with the
shared `msil-ld`. Stand-ins (none is a proposal; `sim/`):

| Stand-in | Simulates | Used by |
|---|---|---|
| `msil-expand-variadics.exe` (variadics recon) | P3-B variadics: definitions, calls, v-variants (`printf`->`vprintf`, `__isoc99_sscanf`->`__isoc99_vsscanf`), fixed-arity natives (`open`, `fcntl`, `ioctl`, `_open`, `_wopen`) | all |
| `sim.h` (`-include`): `signal`, `sigaction`, `atexit`, `SetConsoleCtrlHandler` -> do-nothing functions in `sim.c` | P3-B callbacks / P3-A `atexit` interception | Lua, SQLite |
| `sim.h`: `pthread_create`, `_beginthreadex` -> return an error | worker-thread creation (never reached at the default `SQLITE_DEFAULT_WORKER_THREADS=0`) | SQLite |
| `simatomic.ll`: `__atomic_load_N`/`__atomic_store_N`/`__atomic_compare_exchange_N` as single-threaded volatile accesses | P3-B atomics (relaxed, single-threaded program) | SQLite |
| `fix.py vaaddr` + `sim-va.c`: `ptr @fcntl64`/`@mremap` in a global initializer -> an adapter `msil_sim_va_fcntl64(int, int, ...)` that reads one variadic slot and calls the fixed-arity native | an adapter for the address of a fixed-arity native (decision H7) | SQLite (Linux) |
| `-DSQLITE_OMIT_SEH` | no SEH (decision H5) | SQLite (Windows) |
| `-Dmain=main` (skips `shell.c`'s `#define main utf8_main` and its `wmain`) | `wmain` support (decision H4) | SQLite (Windows) |
| Linux: none (glibc's `_setjmp`/`_longjmp` bind natively today); Windows: `LUAI_THROW`=exit (`MSIL_SIM_LUA_ABORT`) or vcruntime `__intrinsic_setjmp(buf, 0)`/`longjmp` (`MSIL_SIM_LUA_NATIVE_SJLJ`) | P3-B setjmp/longjmp | Lua |

Runs: `tools/run.sh <rt> <image> [args]` copies the image and its data into a fresh directory and runs it under
`dotnet exec --runtimeconfig` (`net8`, `net10`), directly (`fx`, Framework), in WSL with the private runtimes (`wsl8`,
`wsl10`) or natively (`natwin`, `natlin`); `tools/measure.sh` runs every case and `results/compare.txt` holds the
comparison.

---

## 2. Today: what the Phase 2 toolchain reports

### 2.1 First layer: `clang -c` (CIL objects)

Exact texts, as `-fno-caret-diagnostics` prints them (source locations shortened):

| Program / triple | Files failing | Diagnostics |
|---|---|---|
| zlib / linux-gnu | 4 of 17 (`gzlib gzwrite example minigzip`) | 61 `calls to variadic functions are not supported for MSIL yet` (example.c 47, gzlib.c 8, minigzip.c 6) + 1 `variadic function definitions are not supported for MSIL yet` (`gzprintf`, gzwrite.c) |
| zlib / windows-msvc | 4 of 17 | 5 definitions (4 at UCRT `stdio.h`: `fprintf`, `printf`, `snprintf` in example, gzlib, minigzip; 1 `gzprintf`) + 3 calls (gzlib.c 2, example.c 1: `_wopen`, `open`, `gzprintf`) |
| Lua / linux-gnu | 25 of 34 | 373 calls (luac.c 166, lstrlib.c 46, lauxlib.c 19, liolib.c 19, lua.c 16, loadlib.c 13, lparser.c 11, lvm.c 11, ...) + 5 definitions (`luaG_runerror`, `luaL_error`, `luaO_pushfstring`, `lua_gc`, `lua_pushfstring`) |
| Lua / windows-msvc | 25 of 34 | 198 calls + 14 definitions (9 at `stdio.h:830/950/1764`, 5 Lua's) + 1 `ldo.c: error: llvm.frameaddress is not supported for MSIL yet (phase 3 shadow stack)` |
| SQLite / linux-gnu | 2 of 2 | 442 calls (shell.c 417, sqlite3.c 25) + 43 definitions (shell.c 23, sqlite3.c 20) + `error: undefined variadic function 'fcntl64' is not supported for MSIL yet (phase 3)` and the same for `'mremap'` (`MSILAsmPrinter.cpp:1394`) |
| SQLite / windows-msvc | 2 of 2 | 370 calls + 48 definitions (4 at `stdio.h:830/950/1919/2240`) + 7 `sqlite3.c:70447:20: error: frame escape ('llvm.localescape') is not supported for MSIL` and 7 `error: frame escape ('llvm.eh.recoverfp') is not supported for MSIL` (in `?filt$0@0@sqlite3WalFindFrame@@` and the other WAL filters) |

The texts come from `MSILPrepareIR.cpp:1075-1079` (definitions), `MSILISelLoweringCall.cpp:421` (calls; linked
text gets `:410-417`'s `native function '<f>' (<module>) is variadic; ...`), `MSILISelLowering.cpp:603-607`
(`FRAMEADDR`) and `MSILPrepareIR.cpp:1175-1179` (frame escape). The call error is per call site; the definition error
per function, and for UCRT's inline functions once per file that uses them.

### 2.2 Second layer: `msil-ld` (bitcode/LTO, and `--lto-emit-asm --error-limit=0`)

| Program / triple | LTO image link (stops in LTO code generation) | `--lto-emit-asm`, error limit 0 (stops at native binding) |
|---|---|---|
| zlib / both | the same variadic texts with `in function main`, `gz_open`, `gzopen`, `gzprintf`, `printf`, `fprintf`, `snprintf` | (not run) |
| Lua / linux-gnu | 32 calls + 8 definitions, then `too many errors emitted` | 162 x `native function 'printf' (libc.so.6) is variadic; calls of native variadic functions are not supported for MSIL yet (phase 3)` (luac), 5 `fprintf`, 5 `snprintf`, 1 `sprintf`; `native function 'sigaction' (libc.so.6) takes a C function pointer; passing functions to native code needs phase 3 thunks` (lua) |
| Lua / windows-msvc | 13 definitions + 2 x `in function luaD_rawrunprotected ...: llvm.frameaddress is not supported for MSIL yet (phase 3 shadow stack)` | `undefined symbol: _setjmp`, `undefined symbol: longjmp` (lua and luac: vcruntime is no default library), `native function 'signal' (api-ms-win-crt-runtime-l1-1-0.dll) takes a C function pointer; ...` |
| SQLite / linux-gnu | 20 definitions, `too many errors` | `native function 'signal' (libc.so.6) takes a C function pointer; ...` (2), `symbol 'atexit' is defined only in native object code (.../libc_nonshared.a(atexit.oS)); MSIL cannot link machine code` |
| SQLite / windows-msvc | 4 definitions (`fprintf`, `printf`, `snprintf`, `sscanf`) | `undefined symbol: main` (`>>> referenced by the entry point`): the shell defines `wmain` |

Linux Lua never mentions setjmp: `_setjmp` and `_longjmp` bind to `libc.so.6` silently (section 4.2). Status.md
section 3 lists setjmp/longjmp as "error"; that is true only of windows-msvc (sjlj recon, its section 1).

### 2.3 Third layer and beyond (only visible through the simulation)

| Program / triple | After simulated variadics | What it took to link and run |
|---|---|---|
| zlib / both | links (0.4-0.6 s) | nothing |
| Lua / linux-gnu | `native function 'sigaction' ... needs phase 3 thunks` | `sigaction`/`signal` stubbed; native `_setjmp` |
| Lua / windows-msvc | `llvm.frameaddress` (ldo.c) | `signal` stubbed; setjmp replaced (abort, or vcruntime's with frame 0) |
| SQLite / linux-gnu | `native function 'pthread_create' (libc.so.6) takes a C function pointer; ...`; `the signature of prototype-less function 'fcntl64' cannot be derived: its address is used, but it is never called directly; declare it with a prototype` (and `'mremap'`; `lld/MSIL/Signatures.cpp:139`); `undefined symbol: __atomic_load_1`, `_2`, `_4`, `__atomic_store_1`, `_2`, `_4`, `_8` | callbacks stubbed, the fcntl64/mremap adapters, `simatomic.ll` |
| SQLite / windows-msvc | `llc`: `in function sqlite3WalFindFrame ...: frame escape ('llvm.localescape') is not supported for MSIL` (and 6 more WAL functions and their filters); then `undefined symbol: main` | `-DSQLITE_OMIT_SEH`, `-Dmain=main`, callbacks and `_beginthreadex` stubbed, `simatomic.ll` |

`msil-ld` binds `__atomic_*` to nothing (no `libatomic` on the line) and reports them as undefined symbols without a
phase 3 text: `clang -c` compiles atomics to these libcalls silently (`setMaxAtomicSizeInBitsSupported(0)`; the
atomics-threads recon, its section 0 item 1).

---

## 3. Static inventory of Phase 3 constructs

`tools/inv.py` reads the `-flto -S` IR of every file of a program (post-`-O2`, before code generation) and counts
each construct, independent of which diagnostic fires first. Full listings: `results/inventory/*.txt`.

| Construct | zlib lin | zlib win | Lua lin | Lua win | SQLite lin | SQLite win |
|---|---|---|---|---|---|---|
| Definitions / external functions | 139 / 29 | 143 / 30 | 662 / 91 | 663 / 97 | 2015 / 110 | 2063 / 151 |
| Variadic definitions (program's own) | 1 (`gzprintf`) | 1 + 3 UCRT inline | 5 | 5 + 3 UCRT inline | 43 | 44 + 4 UCRT inline (`fprintf printf snprintf sscanf`; the 44th, `sqlite3_fprintf`, is a Windows-only shell function) |
| Calls of own variadic functions | 1 | 56 (UCRT inline included) | 203 | 408 | 2,067 | 2,099 |
| Calls of native variadic functions | 60: `fprintf` 45, `printf` 7, `fcntl` 4, `snprintf` 3, `open` 1 | 2: `open`, `_wopen` | 190: `printf` 161, `snprintf` 15, `fprintf` 13, `sprintf` 1 | 0 (all through UCRT's inline functions) | 38: `fprintf` 25, `ioctl` 6, `__isoc99_sscanf` 4, `snprintf`, `printf`, `open64` | 0 |
| Indirect variadic calls | 0 | 0 | 0 | 0 | 24 (`osFcntl`, `osOpen` via `aSyscall[]`) | 0 |
| setjmp / longjmp calls | - | - | `_setjmp` 1, `_longjmp` 1 | `_setjmp` 1 (2-argument, `llvm.frameaddress`), `longjmp` 1 | - | - |
| Own functions passed to native code | - | - | `sigaction` (`laction`) | `signal(laction)` | `atexit(abnormalExit)`, `signal` x3, `pthread_create` x3 | `atexit`, `SetConsoleCtrlHandler`, `_beginthreadex` x3 |
| Native function addresses in data | - | - | - | - | 22 (`aSyscall[]`: `fcntl64`, `mremap`, `read`, `pread64`, `mmap64`, ...) | 47 (`aSyscall[]`: `CreateFileW`, `GetLastError`, `HeapAlloc`, ...) |
| Native data imports | `stdin stdout stderr` | none | `stdin stdout stderr` | none | `stdin stdout stderr` | none |
| Atomics | - | - | - | - | 69 store, 29 load, 80 `fence` (system) | 70 store, 29 load, 2 `cmpxchg`, 82 `fence syncscope("singlethread")` |
| Thread-local, dynamic alloca, inline asm, global dtors | 0 | 0 | 0 | 0 | 0 (1 `atexit` call) | 0 (1 `atexit` call) |
| Struct by value to/from native, `long double` functions | 0 | 0 | 0 | 0 | 0 | 0 |
| `i128` (`__uint128_t` multiply in the float-to-decimal code) | 0 | 0 | 0 | 0 | 18 lines | 18 lines |
| SEH intrinsics | - | - | - | - | - | `llvm.localescape` 7, `llvm.localrecover` 7, `llvm.eh.recoverfp` 7, `llvm.eh.exceptioncode` 7 |
| External functions by group | stdio 12, posix-io 7, string 5, alloc 3 | stdio 11 (`__acrt_iob_func`, `__stdio_common_vfprintf`, `__stdio_common_vsprintf`), posix-io 7 | stdio 30, math 18, string 12, ctype/locale 7 (`__ctype_b_loc`, `__ctype_tolower_loc`, `__ctype_toupper_loc`, `__errno_location`, ...), time 6, dl 4, `__uflow`, `bcmp` | stdio 29, math 17, ctype 15, string 14, time 6 (`_time64`, `_localtime64`, `_gmtime64`, ...), Win32 3, dl 3 | posix-io 33, stdio 20, string 15, threads 10, process 8, dl 4 | Win32 70, stdio 20, string 15, posix-io 11 |

With `SQLITE_THREADSAFE=0` (and `SQLITE_OMIT_LOAD_EXTENSION`), `sqlite3.c` alone (`inv/sqlite3-ts0-*.txt`) loses its
three `pthread_create`/`_beginthreadex` callbacks, the pthread/CRITICAL_SECTION calls, every fence and both
`cmpxchg`, but keeps **81-82 relaxed atomic stores and 29 loads** (`AtomicLoad`/`AtomicStore`, `sqlite3.c:15113-15120`,
chosen by `__has_extension(c_atomic)`, not by thread safety, and not overridable with `-D`), and `shell.c` keeps its
`signal`/`atexit`/`wmain`.

Every LLVM intrinsic in the three programs outside the SEH ones (`llvm.fshl`, `llvm.umulh.i64`, `llvm.scmp`,
`llvm.bswap`, saturating and min/max ones, `llvm.fmuladd`, `llvm.floor`, ...) already compiles (the simulated builds
went through the shared `llc`).

---

## 4. Runs behind the simulation

### 4.1 Results (outputs compared with the native build; `results/compare.txt`)

| Case | net8 | net10 | fx | wsl8 | wsl10 |
|---|---|---|---|---|---|
| zlib `example` (all API tests, gzio included) | = native | = native | = native | = native | = native |
| zlib `minigzip < in.txt` (2 MB, compress) | = native (520,133 bytes) | = | = | = native | = |
| zlib `minigzip -d` of the native stream | = input | = | = | = input | = |
| Lua `prog1.lua` (no errors: formatting, `string.pack`, `utf8`, `os.date`/`os.time`, `io.lines`, `table.sort` with a Lua comparator, math) | 1 line differs (5.3) | 1 line | 1 line | = native | = native |
| Lua `prog2.lua` (`pcall`/`error`, `xpcall`, coroutines) | exit 127, no output (vcruntime setjmp) | exit 127 | segfault after 4 lines | = native (glibc setjmp) | = native |
| Lua 5.4.9 test suite (`all.lua`, `_U=true`) | - | - | - | `final OK !!!` | `final OK !!!` |
| Lua 5.5.1 test suite | - | - | - | `final OK !!!` | - |
| SQLite `t1.sql` (2,000 rows, index, printf/JSON/date/unicode functions, savepoint rollback, integrity check) | = native mod CR | = mod CR | = mod CR | = native | = native |
| SQLite `t2.sql` (WAL, 20,000 rows, checkpoint, VACUUM) | = mod CR | = mod CR | = mod CR | = native | = native |
| SQLite `t3.sql` (float formatting: `%!.15g`, `%.20f`, 1991/2000.0, ...) | = mod CR | = mod CR | = mod CR | = native | = native |

"mod CR": the Windows MSIL shell writes CRLF and the native shell LF because the `-Dmain=main` stand-in skips
`wmain`, which calls `_setmode(_fileno(stdout), _O_BINARY)` (`shell.c:39013`); with CR removed every output is
identical. The zlib gzip streams differ between Windows and Linux only in the OS byte of the header, natively too.

The Lua suite's output differs from native only in the random seeds it prints (time- and address-based), one
randomized line (`testing short-circuit optimizations (0)` vs `(1)`) and the timings. It exercises errors, `pcall`
in metamethods, coroutines (`coroutine.lua`, `cstack.lua`'s "stack overflow detection", 250,037 levels of
recursion in message handling), `goto`, GC (`gc.lua`, `gengc.lua`), files and dates (`files.lua`), `string.format`
and `%a`, `string.pack`, `utf8`, and the to-be-closed variables of 5.4.

### 4.2 setjmp on the Linux triple works by accident

Lua's `LUAI_THROW`/`LUAI_TRY` (`ldo.c`) use `_longjmp`/`_setjmp` under `LUA_USE_POSIX`; `msil-ld` binds both to
glibc (`_setjmp -> libc.so.6!_setjmp [function, GLIBC_2.2.5]`), and the native `longjmp` restores a stack pointer and
return address inside the P/Invoke stub of the `_setjmp` call, skipping every managed frame in between. It passed
the whole suite on .NET 8 and .NET 10, but it is not a design: the runtime's frame chain and GC mode are left
inconsistent between the two transitions, and on Windows the same idea fails at once (`exit 127` on .NET 8/10, a
segfault on Framework). Two consequences for Phase 3: (1) `msil-ld` must refuse native setjmp-family bindings (the
sjlj recon's section 1 recommendation), or Linux hosted tests can pass without the real lowering; (2) Lua is the
natural exit check of the setjmp work: one `setjmp` function (`luaD_rawrunprotected`, one indirect call), one
`longjmp` (`luaD_throw`), exercised by every error and every C-level yield.

### 4.3 Timings

Builds on the loaded host (`results/*` and the first attempt's `results.json`, CPU-bound, sums over files):

| Program | Files | `-flto -c` total (largest file) | simulated pipeline (`-S`, expand, `llc`) | object link | failing LTO link today |
|---|---|---|---|---|---|
| zlib | 17 | 10 s (1 s) | 16-20 s | 0.4-0.6 s | 6-11 s to the error |
| Lua 5.4.9 | 34 | 25-31 s (2-3 s) | 60-104 s | 0.5-2.1 s | 14-31 s |
| SQLite | 2 | 56-60 s (`sqlite3.c` 47-49 s) | 92-99 s (`sqlite3.c` 74-79 s) | 0.7-1.8 s | 28-50 s |

`sqlite3.c` is one translation unit, so it is the critical path of any parallel gate (~60-75 s for `clang -c` per
triple and level on this host; its object is 1.4-1.6 MB with about 2,000 methods, far from the 65,524-method limit
of one CIL object). Images: zlib `example` 84 KB, Lua 274-278 KB (native 561 KB), SQLite shell 1.69-1.73 MB (native
1.62-1.74 MB).

Runs, timed inside WSL on its own file system (`tools/wsltime.sh`; the `/mnt/c` runs of `measure.sh` add 2-60 s of
`wsl.exe` start and 9p I/O under load):

| Case | native | .NET 8 | .NET 10 |
|---|---|---|---|
| Lua 5.4.9 suite, CPU by `os.clock` | 1.98-3.03 s | 3.42-4.14 s | 3.52-4.70 s |
| Lua 5.5.1 suite, CPU | 4.03 s | 4.70 s | - |
| SQLite `t2.sql`, wall | 0.06-0.07 s | 2.46-3.31 s | 2.26-3.24 s |
| SQLite `t1.sql`, wall | 0.01-0.02 s | 2.01-2.75 s | - |
| SQLite `SELECT 1;`, wall (startup + JIT) | - | 1.06-1.28 s | - |

On Windows the harness wall times were 1.5-8 s for zlib and Lua cases and 3-15 s for SQLite under the same load
(`results/measure.log`). The SQLite times are dominated by JIT compilation of a 1.7 MB image (about 1.1 s before
the first query); this is the per-test cost to budget, not a correctness issue.

---

## 5. The UCRT (windows-msvc)

### 5.1 The inline printf family and `__stdio_common_v*`

UCRT's `<stdio.h>` declares `_CRT_STDIO_INLINE int __CRTDECL printf(...)` and the rest of the family as
`__inline` definitions (`stdio.h:830` `fprintf`, `:950` `printf`, `:1764` `sprintf`, `:1919` `snprintf`, `:2240`
`sscanf`, plus the `_l`, `_s` and `v` variants they call). clang emits each as a `linkonce_odr` (comdat) variadic
function in every file that uses it. Before and after the variadics expansion (zlib `example`):

```llvm
; clang -O2: a variadic definition (today: "variadic function definitions are not supported for MSIL yet")
define linkonce_odr i32 @fprintf(ptr noundef %_Stream, ptr noundef %_Format, ...) comdat {
  call void @llvm.va_start.p0(ptr nonnull %_ArgList)
  %0 = load ptr, ptr %_ArgList, align 8
  %call.i = call ptr @__local_stdio_printf_options()
  %1 = load i64, ptr %call.i, align 8
  %call1.i = call i32 @__stdio_common_vfprintf(i64 %1, ptr %_Stream, ptr %_Format, ptr null, ptr %0)
; after ExpandVariadics (MSIL Win64 ABI): the va_list is the caller's buffer of 8-byte slots
define linkonce_odr i32 @fprintf(ptr noundef %_Stream, ptr noundef %_Format, ptr %varargs) comdat {
  store ptr %varargs, ptr %_ArgList, align 8
  ...
  %call1.i = call i32 @__stdio_common_vfprintf(i64 %1, ptr %_Stream, ptr %_Format, ptr null, ptr %0)
```

So on windows-msvc a `printf` call is a call of an MSIL-defined variadic function, never a native variadic call;
the native boundary is `__stdio_common_vfprintf(unsigned __int64 options, FILE*, const char*, _locale_t, va_list)`,
a fixed-arity UCRT export whose `va_list` is a `char*` to 8-byte slots that our caller built. The variadics recon
measured that buffer with 18 mixed arguments; here it carried every format of zlib, Lua (`string.format` with `%g`,
`%.14g`, `%a`, `%e`, `%5.2f`, `%d`, `%x`, `%o`, `%c`, `%q`) and SQLite's shell, on all three Windows runtimes. No
v-variant table is needed for the UCRT (variadics recon V7: "UCRT needs none"). Imports a hosted Windows program
gets for stdio: `__acrt_iob_func` (for `stdin`/`stdout`/`stderr`, a function, so **no data import on the UCRT**),
`__stdio_common_vfprintf`, `__stdio_common_vsprintf`, `__stdio_common_vsscanf`, all from
`api-ms-win-crt-stdio-l1-1-0.dll`; `errno` is `*_errno()`.

### 5.2 `printf` without `<stdio.h>` and `legacy_stdio_definitions`

The UCRT exports no `printf` (only `__stdio_common_*`). Code that declares `int printf(const char *, ...);` itself
(`probes/noinc.c`), or defines `_NO_CRT_STDIO_INLINE`, gets `msil-ld: error: undefined symbol: printf` behind the
variadics expansion. Natively, `legacy_stdio_definitions.lib` supplies the out-of-line definitions; it holds 223
machine-code definitions, and `msil-ld -llegacy_stdio_definitions --verbose` says `skipping native static archive
...\legacy_stdio_definitions.lib (no CIL or bitcode members)` and then `unable to find library
-llegacy_stdio_definitions`. The variadics recon's decision V10 (a C file in P3-A's static helper archive that
defines the standard names over `__stdio_common_v*` and `__acrt_iob_func`) covers the names; this recon adds that
`msil-ld` should then treat `legacy_stdio_definitions` (by `-l` or `#pragma comment(lib)`/`/DEFAULTLIB:`) like the
MSVC runtime libraries it already ignores (`libcmt`, `msvcrt`, `ucrt`, `vcruntime`: a `--verbose` note), because
the helper archive provides what it would. `legacy_stdio_float_rounding.obj` and `legacy_stdio_wide_specifiers.lib`
are the opt-outs of section 5.3's two bits.

### 5.3 Finding: the per-program printf/scanf options are never initialized

`corecrt_stdio_config.h:89-103` defines `__local_stdio_printf_options()` and `__local_stdio_scanf_options()` as
`__declspec(noinline) __inline` functions returning the address of a `static unsigned __int64 _OptionsStorage`: one
variable per program (merged comdat), read by every inline printf/scanf call (`_CRT_INTERNAL_LOCAL_PRINTF_OPTIONS`).
The native CRT startup sets it before any C initializer runs: `exe_common.inl:206-210` calls
`__scrt_initialize_default_local_stdio_options()`, which ORs in the defaults unless `legacy_stdio_float_rounding.obj`
or `legacy_stdio_wide_specifiers.lib` is linked. MSIL programs have no CRT startup (`'<msil>Main'` calls `main`),
so the word stays 0. Measured (`probes/crtw.c`, `results/probes.txt`):

| | native (lld-link, UCRT) | MSIL net8 | MSIL net10 | MSIL fx | MSIL + constructor (`probes/crtw2.c`), net8 and fx |
|---|---|---|---|---|---|
| `*__local_stdio_printf_options()` | `0x24` | `0x0` | `0x0` | `0x0` | `0x24` |
| `*__local_stdio_scanf_options()` | `0x2` | `0x0` | `0x0` | `0x0` | `0x2` |
| `%.0f` of 0.5 / 2.5, `%.1f` of 0.25 | `0` / `2`, `0.2` | `1` / `3`, `0.3` | same | same | `0` / `2`, `0.2` |

`0x24` = `_CRT_INTERNAL_PRINTF_LEGACY_WIDE_SPECIFIERS (1<<2) | _CRT_INTERNAL_PRINTF_STANDARD_ROUNDING (1<<5)`
(`corecrt_stdio_config.h:115-125`); scanf `0x2` = `_CRT_INTERNAL_SCANF_LEGACY_WIDE_SPECIFIERS`. Without
`STANDARD_ROUNDING` the UCRT rounds exactly representable halves away from zero (the pre-Windows-10-2004 behaviour);
Lua's `string.format("%5.2f", 123456789.125)` prints `123456789.13` on all three Windows runtimes against native
`123456789.12` (`results/compare.txt`), the only difference in `prog1.lua`. The wide-specifier bit changes `%s`/`%S`
in the wide functions. The fix is a few instructions at startup (decision H3); the constructor used for the
measurement:

```c
__attribute__((constructor)) static void msil_default_local_stdio_options(void) {
  *__local_stdio_printf_options() |= _CRT_INTERNAL_PRINTF_LEGACY_WIDE_SPECIFIERS | _CRT_INTERNAL_PRINTF_STANDARD_ROUNDING;
  *__local_stdio_scanf_options() |= _CRT_INTERNAL_SCANF_LEGACY_WIDE_SPECIFIERS;
}
```

SQLite is not affected (its own `sqlite3_str_vappendf` formats floats); zlib prints no floats.

### 5.4 Other UCRT state that the host's startup owns

From the same probe: `_environ`, `getenv`, `setlocale(LC_ALL, NULL)` (`C`), `_fmode` (16384, `_O_TEXT`),
`_isatty`, `_fileno(stdout)`, `rand` after `srand(7)` (61) and `%ls`/`%S`/`%hs` match native. `__argc`/`__argv`
reflect the host process, not the program: native `1`/set; MSIL under `dotnet exec` `__argc=5` (dotnet's own
command line) with `__argv=NULL`; under Framework `0`/`NULL`; `_get_pgmptr` fast-fails (`0xC0000409`) because
`_pgmptr` is NULL. The backlog-p3 recon records the same family (its items on `GetCommandLineW` and
`program_invocation_name`); none of the three programs reads them.

### 5.5 `wmain` (SQLite's shell)

`shell.c:38979-39019`: on `_WIN32` without MinGW, the shell renames `main` to `utf8_main` and defines
`int wmain(int argc, wchar_t **wargv)`, which converts the arguments to UTF-8, sets the console to VT mode and
`stdout` to binary, then calls `utf8_main`. `msil-ld`'s entry point is `--entry`, default `main`
(`lld/MSIL/Writer.cpp:284-316`, `getEntryPoint`: `int32 main()` or `int32 main(int32, native int)` with an `argv` of
`char*`), so the link stops with `undefined symbol: main`. `-Wl,--entry=wmain` would pass the signature check but
build a `char**` for a `wchar_t**` parameter. `lld-link` picks `wmainCRTStartup` when `wmain` is defined and `main`
is not; the MSIL equivalent is a `wchar_t*` argv (UTF-16 copies of `Environment.GetCommandLineArgs()` through
`Marshal.StringToHGlobalUni`, both in every runtime's mscorlib) (decision H4).

### 5.6 SEH (`__try`/`__except`)

SQLite enables `SQLITE_USE_SEH` for `_MSC_VER` unless `SQLITE_OMIT_SEH` (`sqlite3.c:15228-15235`, the test at `:15231`) to catch
`EXCEPTION_IN_PAGE_ERROR` from the memory-mapped `-shm` file in WAL code (7 functions). clang-cl semantics on the
MSIL Windows triple turn `__try`/`__except` into `llvm.localescape` in the parent and a filter funclet with
`llvm.eh.recoverfp`/`llvm.localrecover`, which `MSILPrepareIR` rejects with a text that does not name SEH. The
exceptions SQLite filters are hardware exceptions, which CoreCLR does not deliver to managed handlers, so a CIL
mapping of `__except` would not even serve this case. `-DSQLITE_OMIT_SEH` compiles the same code without SEH, and the
measured runs (WAL included) used it (decision H5). The Windows inventory also lists `__C_specific_handler` and
`RaiseException` imports from the SEH code.

### 5.7 Win32 calls from C

SQLite's Windows VFS calls 70 kernel32 functions, 47 of them through function pointers stored in `aSyscall[]`
(M20's native function addresses: one P/Invoke stub per function, its address in the table, `calli` through it);
they work as measured. `GetLastError()` after a failing `CreateFileW` is preserved between the two P/Invokes
(`probes/lasterr2.c`: the first call and 300,000 iterations, plus `SetLastError`/`GetLastError` pairs and UCRT
`errno` after `_open`; 0 mismatches on .NET 8, .NET 10, Framework and natively). The CLR could clobber the value
only if this thread blocked in a GC suspension between the two calls, which needs managed allocation by some thread
(not measured; the callbacks recon's GC-churn leg could run `lasterr2.c`).

---

## 6. glibc (linux-gnu)

### 6.1 Headers

glibc 2.43's headers compile unchanged for `x86_64-unknown-linux-gnu-msil`: no diagnostic other than variadics came
from a header in any of the three programs (`bits/floatn.h`'s `_Float128`, `__int128` in headers, `__attribute__`
uses, `__extern_always_inline` functions, `__REDIRECT` asm labels all pass). glibc's `getc_unlocked` macro reads
`FILE`'s `_IO_read_ptr`/`_IO_read_end` inline and calls `__uflow` (Lua's `l_getc`): MSIL code reads the native
`FILE` layout directly, which works because the MSIL triple's data layout is the native one. `isalpha` and friends
read `*__ctype_b_loc()`, `toupper` `*__ctype_toupper_loc()`; `__isoc99_sscanf` is `sscanf` under C99 (SQLite's
shell), bound by the prototype's v-variant table to `__isoc99_vsscanf`.

### 6.2 `_FORTIFY_SOURCE` and `__builtin_va_arg_pack`

clang implements no `__builtin_va_arg_pack`. glibc 2.43 (`sys/cdefs.h:205-219`, `bits/stdio2.h:120-146`) has a
clang branch, `__fortify_use_clang` for clang >= 5: `printf`, `fprintf`, `sprintf`, `snprintf` (and `dprintf`, ...)
become `static __attribute__((overloadable))` variadic functions with a `pass_object_size` (level 2) or
`pass_dynamic_object_size` (level 3) first argument, mangled (`_ZL6printfPKcU17pass_object_size1z`), that call
`__builtin___vprintf_chk` & co. with a `va_list`. That is the UCRT shape again: today `variadic function definitions
are not supported for MSIL yet` at `bits/stdio2.h:37`, `:75`, `:123`, `:135`; behind the variadics expansion
`probes/fort.c` (printf, fprintf, snprintf, sprintf into a 16-byte buffer, strcpy/strcat, a fortified `memcpy`,
`read`, `realpath`) links with `__vprintf_chk`, `__vfprintf_chk`, `__vsnprintf_chk`, `__vsprintf_chk` (GLIBC_2.3.4),
`__strcat_chk`, `__read_chk` (GLIBC_2.4) and matches native on WSL .NET 8 at levels 2 and 3. Older glibc headers
without the clang branch fall back to macros (`#define printf(...) __printf_chk (...)`, `stdio2.h:145-148` in this
version's `#elif`), which call the native variadic `__printf_chk` family: the variadics recon's v-variant table
already lists those entries. glibc's `longjmp` becomes `__longjmp_chk` under fortify (sjlj recon, section 1).

### 6.3 Symbol versions

A P/Invoke records no version and binds the library's default version at run time (Status.md section 2); `msil-ld
--print-imports` reports the highest default version per library as the "version floor" (`libm.so.6: version floor
GLIBC_2.38` for Lua: `fmod@@GLIBC_2.38`, `exp`/`log`/`pow@@GLIBC_2.29` of the 2.43 libm). These floors are
informational: the names exist in every glibc, so an image built against 2.43 still binds `fmod` on an older glibc.
`memcpy@@GLIBC_2.14` never appears: `llvm.memcpy` is `cpblk` and a call of `memcpy` binds by name.

### 6.4 Finding: data imports and `dlopen` pin glibc 2.34

The three programs import `stdin`, `stdout` and `stderr` as data (`stdout -> libc.so.6!stdout [data, GLIBC_2.2.5]`);
`probes/crtl.c` also read `environ`, `optarg`/`optind` (getopt), `timezone`/`daylight`/`tzname` and
`program_invocation_short_name`, all correct (M20 works; `program_invocation_short_name` is `dotnet`, natively the
program's name). `'<msil>init_dataimports'` resolves them with `NativeLibrary` when a reference defines it, else
with `dlopen`/`dlsym` that `findDataImportFunctions` (`lld/MSIL/NativeLibraries.cpp:177-201`) binds from the link's
libraries: on a glibc >= 2.34 sysroot that is `libc.so.6` (`dlopen@@GLIBC_2.34`), and every hosted program gets
`libc.so.6: version floor GLIBC_2.34`. Unlike `fmod`, the name `dlopen` does not exist in `libc.so.6` before 2.34
(it lived in `libdl.so.2`), so such an image would stop at startup on Ubuntu 20.04 (2.31), Debian 11 (2.31) or
RHEL 8 (2.28) with `EntryPointNotFoundException` in the module initializer (inferred from glibc's history; no older
glibc was available to run). Measured (`il/dl.il`, WSL glibc 2.43, .NET 8 and 10): a P/Invoke of
`libdl.so.2!dlopen` **binds** (libdl.so.2 itself defines only `__libdl_version_placeholder` since 2.34, and `dlsym` on
its handle finds `dlopen` in its `DT_NEEDED` `libc.so.6`), as does `libc.so.6!dlopen`. Measured too (`il/nl.il`,
`il/nl2.il`): `NativeLibrary` through `[mscorlib]` throws `TypeLoadException` on .NET 8/10 (the facade does not
forward it), and through `[System.Runtime.InteropServices]4.1.0.0` it binds on both (decision H6).

### 6.5 `errno`

`errno` is `(*__errno_location ())`; after a failing `open` and `close(-1)` the value was right in the first call
and in 300,000 iterations (`probes/errno2.c`, 0 mismatches on WSL .NET 8/10 and natively). A program that declares
`extern int errno;` itself gets Phase 2's `symbol 'errno' in libc.so.6 is thread-local data and cannot be imported;
use <errno.h>`.

---

## 7. Gap catalogue by category

Counts are per program for linux-gnu / windows-msvc; "first wall" is the layer where the diagnostic appears today.

| Category | zlib | Lua | SQLite | Today's diagnostic | Owner (recon) |
|---|---|---|---|---|---|
| Variadic calls of native functions (printf family, `open`, `fcntl`, `ioctl`, `__isoc99_sscanf`) | 60 / 2 | 190 / 0 | 38 / 0 | `calls to variadic functions are not supported for MSIL yet`; linked text `native function 'printf' (libc.so.6) is variadic; ...` | P3-B variadics (v-variants, fixed-arity list) |
| Variadic definitions (own, UCRT inline printf, glibc fortify wrappers) | 1 / 4 | 5 / 8 | 43 / 48 | `variadic function definitions are not supported for MSIL yet` (UCRT at `stdio.h:830/950/1764/1919/2240`) | P3-B variadics |
| Indirect variadic calls and native variadic addresses in data | 0 | 0 | 24 calls; `fcntl64`, `mremap` in `aSyscall[]` (Linux) | `undefined variadic function 'fcntl64' is not supported for MSIL yet (phase 3)`; behind expansion `the signature of prototype-less function 'fcntl64' cannot be derived ...` | P3-B variadics + decision H7 |
| setjmp/longjmp | 0 | 1+1 | 0 | Windows `llvm.frameaddress is not supported for MSIL yet (phase 3 shadow stack)`, then `undefined symbol: _setjmp`/`longjmp`; Linux **none** (bound natively) | P3-B SjLj (sjlj recon E1) |
| Function pointers to native code | 0 | 1 (`signal`/`sigaction`) | 7 / 5 (`atexit`, `signal` x3 / `SetConsoleCtrlHandler`, `pthread_create` x3 / `_beginthreadex` x3) | `native function 'signal' (...) takes a C function pointer; passing functions to native code needs phase 3 thunks`; `atexit`: `defined only in native object code` (glibc) / `undefined symbol: atexit` (UCRT) | P3-B callbacks (CB8 for signals), P3-A lifecycle (`atexit`) |
| Native function addresses (non-variadic) in data | 0 | 0 | 22 / 47 | none: works (M20) | - |
| Data imports | `stdin stdout stderr` / none | same | same | none: works (M20); GLIBC_2.34 floor | decision H6 |
| `errno`, `GetLastError` | `__errno_location`/`_errno` | same | same + `GetLastError` | none: works (measured) | - |
| Struct by value across P/Invoke | 0 | 0 | 0 | (`div_t`/`lldiv_t` elsewhere: `takes or returns a value that a P/Invoke cannot pass natively yet (phase 3)`) | backlog-p3 BL2 |
| `FILE*` and stdio | 12 / 11 functions | 30 / 29 | 20 / 20 | none beyond variadics; `getc_unlocked` reads `FILE` inline (works) | - |
| time.h | - | `time clock mktime localtime_r gmtime_r strftime difftime` / `_time64 _localtime64 _gmtime64 _mktime64 _difftime64 clock` | `gettimeofday localtime time nanosleep` / `GetSystemTimeAsFileTime ...` | none: works (`os.date`, `os.time`, SQLite date functions match) | - |
| Locale | - | `setlocale localeconv strcoll` | - | none: works (`C`, as natively) | - |
| Signals | - | `sigaction` / `signal` | `signal` x3 / `SetConsoleCtrlHandler` | callback texts above | callbacks CB8 |
| Threads | - | - | pthread mutexes (work), `pthread_create`/`_beginthreadex` callbacks (worker threads, unused by default) | callback text | callbacks; `SQLITE_THREADSAFE=0` removes them |
| TLS | 0 | 0 | 0 | - | - |
| Atomics | 0 | 0 | 98 / 101 (+80 / 82 fences, 2 `cmpxchg` on Windows) | none at compile time; `undefined symbol: __atomic_load_4` etc. at link | P3-B atomics |
| `long double` | 0 | 0 | 0 | - | - |
| alloca/VLA, `stacksave` | 0 | 0 (Windows' `frameaddress` is setjmp's) | 0 | - | - |
| Inline asm | 0 | 0 | 0 | - | - |
| Intrinsics | all compile | all compile | all compile; SEH ones on Windows | `frame escape ('llvm.localescape') is not supported for MSIL` | decision H5 |
| Header-level | none (UCRT inline printf counted above) | none | none | - | - |
| Entry point | `main` | `main` | `wmain` on Windows | `undefined symbol: main` | decision H4 |
| UCRT startup state | (no floats printed) | `%f` rounding differs | (own printf) | none: silent output difference | decision H3 |

---

## 8. Ordering Phase 3 by what each feature unblocks

### 8.1 Feature x program matrix (both triples unless marked)

| Feature (milestone it belongs to) | zlib full | Lua 5.4.9 (`lua.c`) | Lua, embedding driver | SQLite shell | SQLite, embedding driver |
|---|---|---|---|---|---|
| Variadics: definitions, calls, v-variants, fixed-arity list (P3-B) | **required, sufficient** | required | required | required | required |
| UCRT stdio options at startup (H3) | - | win: output fidelity | win | - | - |
| setjmp/longjmp lowering (P3-B) | - | required (every error, every C yield) | required | - | - |
| Native-callable thunks / `signal` (P3-B callbacks, CB8) | - | `signal`/`sigaction` in `lua.c` | not needed | `signal` x3 / `SetConsoleCtrlHandler`; `pthread_create`/`_beginthreadex` (links only) | `pthread_create`/`_beginthreadex` unless `SQLITE_THREADSAFE=0` |
| `atexit` interception (P3-A lifecycle) | - | - | - | required | - |
| Atomics (P3-B) | - | - | - | required (also with `THREADSAFE=0`) | required |
| Address adapter for fixed-arity natives (H7) | - | - | - | lin: `fcntl64`, `mremap` | lin |
| `wmain` (H4) | - | - | - | win (or `-Dmain=main` and a CRLF-tolerant comparison) | - |
| SEH | - | - | - | win: `-DSQLITE_OMIT_SEH` (H5) | win: same |
| `dlopen` module for data imports (H6) | lin: portability only | lin | lin | lin | lin |

### 8.2 Recommended exit checks

1. **Variadics milestone: zlib full.** Measured to need nothing else on either triple; `example` exercises gzio
   (`gzprintf` is a variadic definition, `gzopen` a fixed-arity `open`/`_wopen`) and `minigzip` stdio and the data
   imports. Add `-D_FORTIFY_SOURCE=2` builds of zlib on Linux to cover glibc's clang fortify wrappers.
2. **setjmp milestone: Lua.** With an embedding driver (`luaL_newstate`, `luaL_openlibs`, `luaL_dofile`, no
   `signal`) it needs only variadics and SjLj; with `lua.c` it also needs the `signal` callback. Run `prog1.lua`- and
   `prog2.lua`-style drivers byte for byte against native, and the test suite (`all.lua`, `_U=true`), checking
   `final OK !!!` and the exit status (its seeds and times vary). Keep `msil-ld`'s refusal of native setjmp bindings
   in the same milestone, or the Linux runs prove nothing.
3. **Atomics/callbacks milestone: SQLite.** An embedding driver (`sqlite3_open`, `sqlite3_exec` of fixed scripts,
   `SQLITE_THREADSAFE=0`) needs variadics, atomics, `-DSQLITE_OMIT_SEH` on Windows and H7 on Linux; the shell adds
   callbacks, `atexit` and `wmain`.
4. **P3-B host-libc exit: all three** on both triples, `-O0` and `-O2`, objects and LTO, every runtime, the shell
   and `lua.c` themselves, plus the Lua suite on both triples.

---

## 9. Exit-corpus additions

### 9.1 `manifest.json` entries (verified)

Appended to `msil/utils/corpus/manifest.json` (format v1, `fetch.py`'s header). Verified by importing the tree's
`fetch.py` unchanged and calling `fetch.main(["--cache", <scratch>], manifest_path=<proposal>)`
(`corpus/runfetch.py`): `3 of 3 verified (downloaded 3.5 MB in 12.2 s)`, `--check` passes offline, and the
extracted trees equal the ones every measurement used (`diff -r`). zlib needs no entry: `zlib` 1.3.2 already holds
the full sources (Phase 2 built only Z_SOLO's nine files from it).

```json
{
  "name": "lua",
  "license": "MIT",
  "license_file": "src/lua.h",
  "url": "https://www.lua.org/ftp/lua-5.4.9.tar.gz",
  "ref": "5.4.9",
  "archive_sha256": "2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6",
  "tree_sha256": "5ae2103cee75f96abee333a09a559db72c44f8e8d5d626020a27904857689d60",
  "strip": 1
},
{
  "name": "lua-tests",
  "license": "MIT",
  "license_file": "all.lua",
  "url": "https://www.lua.org/tests/lua-5.4.9-tests.tar.gz",
  "ref": "5.4.9",
  "archive_sha256": "7d971845f545ffc09fbb3128a86b2c6524161c70d0fdf0154a16e8c00c343fca",
  "tree_sha256": "3bf782c4ae25eeab5e4e739720b685b23ae62f14f0b4a635999858f4fba765e0",
  "strip": 1
},
{
  "name": "sqlite",
  "license": "public domain",
  "license_file": null,
  "url": "https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip",
  "ref": "3.54.1",
  "archive_sha256": "0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9",
  "tree_sha256": "4c2d538f44a67d2a8befa009f9e6bbd93e2d2d9c1becc6b97fd9594bef502470",
  "strip": 1
}
```

Alternative Lua pin (`corpus/manifest-lua55.json`, tree hashes computed by the same script): `lua` 5.5.1
(`archive_sha256` `1c4b4068...`, `tree_sha256` `d0b292dad4c23f26da09cdf066e15e14c5baf350c0bee26dc93c23d9f4bf7101`)
and `lua-tests` 5.5.1 (`da07b543...`, `8c534f95952afe338cb224c7625b21630ae48159bc352e4252b9f63dc9d65186`).

Notes: `fetch.py` keeps regular files only, so the test tarball's empty `libs/P1/` directory is not created; it
is used only by `attrib.lua`'s `require` tests inside `if not _port`, which `_U=true` skips. sqlite.org's URL
carries the release year (`/2026/`); a later pin changes the path.

### 9.2 Tests (shape; tests-gates-p3 section 4.7 fixes the legs)

`msil/test/Programs/<zlib-full|lua|sqlite>/` with drivers, built as archives plus program like the Phase 2 programs
but with real headers (no `../shim`):

| Test | Build | Run and compare |
|---|---|---|
| `zlib-full` | 15 library files in `libz.a`; `example.c`, `minigzip.c`; Linux also `-D_FORTIFY_SOURCE=2` | `example` transcript; `minigzip` compress of a generated 2 MB input compared with the native stream, `-d` of the native stream compared with the input |
| `lua` | 32 files in `liblua.a`; `lua.c` (and/or an embedding driver); flags of 1.2 | `prog1.lua`/`prog2.lua` (`hosted-libc-files/data/`) byte for byte; `all.lua` with `-e_U=true` from a copy of the `lua-tests` tree: `final OK !!!` and exit 0 |
| `sqlite` | `sqlite3.c` (Windows: `-DSQLITE_OMIT_SEH`), `shell.c` (or a driver); Linux `-lm -lpthread -ldl` | `t1.sql`, `t2.sql`, `t3.sql` (`data/`) on stdin with a database file in the private directory, byte for byte |

Cost per triple and level, from 4.3: zlib ~10 s compile + ~5 s of runs; Lua ~30 s compile (parallel) + 3-5 s per
suite run; SQLite ~60-75 s for `sqlite3.c` (serial) + ~3 s per shell run. For the phase gate (2 triples x 2 levels
x objects and LTO, 5 runtime configurations), on the order of 15-25 CPU-minutes of compilation and 10-15 minutes of
runs, with `sqlite3.c` the longest single job; tests-gates-p3's `%{msil_limits}` override suits SQLite. Licences
(Zlib, MIT, public domain) allow CI, unlike CoreMark and c-testsuite.

---

## 10. Decisions

**H1. The Phase 3 host-libc programs and their pins.** Options: (a) zlib 1.3.2 full (existing entry), Lua 5.4.9 with
its tests, SQLite 3.54.1 amalgamation with its shell; (b) as (a) with Lua 5.5.1; (c) add more hosted programs now.
**Recommend (a).** Evidence: every measurement above used exactly these trees; 5.4.9 is the newest 5.4 and was run on
both triples, 5.5.1 passed its suite on WSL .NET 8 under the same simulation (diagnostics: 376 calls, 6
definitions, the same categories), so either works; P3-B's outline names these three.

**H2. Which program closes which milestone.** Options: (a) section 8.2 (zlib for variadics, Lua for setjmp, SQLite
for atomics/callbacks, all three for the host-libc exit), with embedding drivers so a program can close a milestone
before the callbacks/lifecycle work; (b) all three only at the end of P3-B. **Recommend (a).** Evidence: 8.1's
matrix; zlib needs variadics only (measured), Lua's driver variadics and SjLj only, SQLite's driver variadics,
atomics, H5 and H7 only.

**H3. UCRT local printf/scanf options at startup (windows-msvc).** Options: (a) `msil-ld` emits, in
`<Module>::.cctor` before the constructors, `*__local_stdio_printf_options() |= 0x24` and
`*__local_stdio_scanf_options() |= 0x2` when the link defines those functions (IL generated by the toolchain, no
library needed); (b) a C constructor in P3-A's static helper archive that references them weakly; (c) leave the
legacy behaviour and document it. **Recommend (a)**, on windows-msvc triples only, with `legacy_stdio_float_rounding`
/ `legacy_stdio_wide_specifiers` named by `-l`, `/DEFAULTLIB:` or `#pragma comment(lib)` clearing the corresponding
bit (that is what those objects do natively). Evidence: 5.3 (0x24/0x2 natively, 0 in MSIL on all three runtimes, the
constructor makes every output match). (b) depends on how the helper archive reaches `msil-ld`, an open question;
(c) leaves a silent output difference in every program that prints floats.

**H4. `wmain` on windows-msvc.** Options: (a) `'<msil>Main'` calls `wmain(int, wchar_t**)` (and `wmain(int,
wchar_t**, wchar_t**)`) with UTF-16 copies of the arguments when the program defines `wmain` and not `main`, as
lld-link chooses `wmainCRTStartup`; (b) unsupported (users rename). **Recommend (a)**; small (`Writer::getEntryPoint`
plus the trailer's argv loop with `Marshal.StringToHGlobalUni`). Evidence: 5.5; SQLite's shell needs it, and its
`wmain` is what sets binary mode, so without it the outputs differ. `WinMain`/`wWinMain` stay for later.

**H5. SEH (`__try`/`__except`/`__finally`) on windows-msvc.** Options: (a) keep it an error, with a clang diagnostic
that names SEH at the `__try` (today's `frame escape ('llvm.localescape')` names neither SEH nor the source construct
in the filter's error), and build SQLite with `-DSQLITE_OMIT_SEH`; (b) lower `__try`/`__finally` and `__except` for
software exceptions to CIL handlers. **Recommend (a)** for Phase 3. Evidence: 5.6; SQLite filters hardware exceptions,
which CoreCLR does not deliver to managed filters, so (b) would not serve the one real user found.

**H6. The module of `dlopen`/`dlsym` for glibc data imports.** Options: (a) as today, from the link's libraries
(`libc.so.6`, floor GLIBC_2.34); (b) always `libdl.so.2` on glibc triples (`libc.so` on musl); (c) on Linux triples
without references, `NativeLibrary` through `[System.Runtime.InteropServices]4.1.0.0`. **Recommend (b).** Evidence:
6.4: `libdl.so.2!dlopen` binds on glibc 2.43 (measured) and is libdl's own definition before 2.34; (a) fails at
startup on glibc < 2.34 (inferred); (c) works on .NET 8/10 (measured) but binds a BCL member that no given reference
defines, against the Roslyn rule.

**H7. Taking the address of a native function on the fixed-arity list.** Options: (a) `msil-ld` gives such an
address an adapter with the expanded signature `(fixed..., va_list)` that reads the entry's variadic slots (fcntl:
one `native int`; open/open64/openat: one `int`; ioctl: one pointer; mremap: one pointer) and calls the fixed-arity
P/Invoke, so indirect expanded calls through the pointer work; (b) an error naming the function. **Recommend (a)**,
as an addition to the variadics recon's fixed-arity list (each entry records its slot count and kinds). Evidence:
SQLite's Unix VFS stores `fcntl64` and `mremap` in `aSyscall[]` and calls them through `int(*)(int,int,...)` (24
indirect variadic calls); today's errors (2.1, 2.3); the simulated adapter (`sim/sim-va.c`, one slot read
unconditionally, as the native ABI would leave garbage there too) ran every SQLite script. The portable triple has
an empty fixed-arity list and never needs it.

**H8. SQLite configuration in the corpus.** Options: (a) defaults (`SQLITE_THREADSAFE=1`) plus `-DSQLITE_OMIT_SEH` on
Windows for the shell; (b) `SQLITE_THREADSAFE=0` everywhere. **Recommend (a) for the shell, (b) for an earlier
driver test.** Evidence: section 3: `THREADSAFE=0` removes only the worker-thread callbacks (never started at the
default `SQLITE_DEFAULT_WORKER_THREADS=0`), fences and 2 `cmpxchg`; the 110 relaxed atomics stay; pthread mutexes and
CRITICAL_SECTIONs already work through P/Invoke (all measured runs used the threadsafe build).

**H9. Lua test-suite scope.** Options: (a) `all.lua` with `_U=true` (portable mode) on both triples; (b) a selected
subset. **Recommend (a).** Evidence: it passed on WSL .NET 8 and .NET 10 (5.4.9) and .NET 8 (5.5.1) in 3.4-4.7 s CPU;
the parts it skips in portable mode (C libraries in `libs/`, `ltests`) need native shared objects or a debug build of
Lua. On Windows it runs once SjLj exists.

---

## 11. Effect on the later goals

- **Portable triple** (`msil64-unknown-dotnet`, LLVM libc over `System.*`): the three programs are natural
  conformance checks for it too (stdio, time, locale, `errno`, file I/O, `setjmp`), and the corpus entries carry
  over. H3 and H4 are UCRT-specific, H6 glibc-specific; none constrains it. H7 does not arise there (no fixed-arity
  natives). Its libc will define `printf` in C, so the variadics design keeps a C definition preempting native
  binding (variadics V7) and the portable triple reuses the same expansion.
- **Managed calls from C, inline IL:** unaffected; SQLite's `aSyscall[]` pattern (native addresses in a table,
  called through pointers) is the shape a managed-call binder's function addresses will also take.
- **FeatureIdeas:** nothing here precludes one; the `FILE`-layout observation (6.1) is a reminder that C code reads
  native structures inline, which any managed-memory idea must keep working.

## 12. Risks

- **The simulation is not the implementation.** The variadics prototype, the stub callbacks and single-threaded
  atomics stand in for P3-B; the in-tree pass may differ in the fixed-arity list, v-variant names and `-O0`/LTO
  behaviour (LTO was not run behind the simulation: `msil-ld`'s LTO code generation does not run the prototype).
- **Native setjmp on Linux hides the need for SjLj** (4.2). If `msil-ld` keeps binding `_setjmp`, Lua's Linux tests
  pass without the lowering.
- **Silent Windows output differences** remain until H3: any program printing floats with `%f`/`%e`/`%g` at a tie.
- **glibc floor**: until H6, hosted Linux images need glibc >= 2.34 (Ubuntu 22.04+, Debian 12+, RHEL 9+).
- **Run cost**: SQLite's JIT start (~1.1-1.3 s per process) multiplies with every shell invocation and runtime leg;
  `sqlite3.c` (60-75 s per compile) dominates gate wall time.
- **WAL and `mmap`**: SQLite maps the `-shm` file (`mmap64`/`MapViewOfFile`) and the tests passed, but the
  `EXCEPTION_IN_PAGE_ERROR` path that SEH guards natively is unguarded with `-DSQLITE_OMIT_SEH` (as in MinGW builds).
- **GC suspension between two P/Invokes** could clobber `GetLastError`/`errno` in multithreaded managed hosts (not
  measured, 5.7).

## 13. Experiments index (`C:\llvm-msil-scratch\p3\hosted-libc\`)

| Experiment | Where | Result |
|---|---|---|
| Today's builds of zlib, Lua, SQLite, both triples (objects, LTO, `--lto-emit-asm`) | `build/<prog>-default/<win|lin>-O2/`, summaries `results/diag/*.txt` | sections 2.1-2.2 |
| Static inventories | `inv/`, `results/inventory/*.txt` | section 3 |
| Simulated builds | `build/zlib-default/*-O2-proto`, `build/lua-default/*-O2-sim*`, `build/sqlite-default/*-O2-sim*` | section 2.3 |
| Runs on 5 runtime configurations + native | `m2/`, `tools/measure.sh`, `results/compare.txt`, `results/measure.log` | section 4.1 |
| Lua prog2 with vcruntime setjmp on Windows | `out2/` | exit 127 (.NET 8/10), segfault (fx) |
| Timings inside WSL | `tools/wsltime.sh` | section 4.3 |
| UCRT startup state, the options constructor | `probe/crtw.c`, `probe2/crtw2.c`, `results/probes.txt` | section 5.3-5.4 |
| `printf` without `<stdio.h>`, `legacy_stdio_definitions` | `legacy/noinc.c` | section 5.2 |
| glibc startup state, data imports | `probe/crtl.c` | section 6.4 |
| `GetLastError`/UCRT `errno`, glibc `errno` | `probe2/lasterr2.c`, `probe2/errno2.c` | sections 5.7, 6.5 |
| 128-bit multiply (SQLite's float formatting) | `probe/mul128.c` | equal on both triples |
| `_FORTIFY_SOURCE=2/3` | `fort/fort.c` | section 6.2 |
| `dlopen` module, `NativeLibrary` via mscorlib and System.Runtime.InteropServices | `il/dl.il`, `il/nl.il`, `il/nl2.il` | section 6.4 |
| Corpus entries through `fetch.py` | `corpus/` | section 9.1 |
| Lua 5.5.1 | `src55/`, `build/lua55-default/`, `build/native/lua55/` | suite passes on WSL .NET 8 |
