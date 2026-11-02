# P-0 recon `libc`: measuring the llvm-libc port for the portable target

Read-only recon for the portable target (P-0). Date: 2026-11-01. Repository the repository, branch `msil`, HEAD
`e38f9a199bc9` (a docs-only commit over M25's `c3460446ac09`; no compiler or libc file changed). The tools were
`<build>/bin` (clang 24.0.0git, msil-ld, llvm-nm, llvm-cil-dump), used read-only, plus `ilverify` 10.0.12,
.NET Framework 4.8 (release 0x82405), .NET 10.0.14 on Windows, and WSL Ubuntu's private .NET 10.0.13. Nothing was
written in the repository except this file. Scratch, probes and transcripts are in `<scratch>/p0/libc/`.
A summary transcript is in `TRANSCRIPT.txt` there. An earlier attempt of this run was cut off at the link step; its
compile results were re-checked (see section 1.3) and reused.

## 0. Summary

- **stdio compiles.** printf_core and scanf_core compile for the dotnet approximation once the include chain takes
  llvm-libc's existing non-Linux branches: `hdr/errno_macros.h` picks
  `include/llvm-libc-macros/generic-error-number-macros.h`, and `src/__support/printf_core/error_mapper.h` picks
  `generic/error_mapper.h`. `vsnprintf`, `vsprintf`, `vasprintf`, `vsscanf` and `printf_core/float_impl.cpp` compile
  with 0 warnings. Every variadic stdio entry point (`snprintf`, `sprintf`, `sscanf`, `asprintf`, `printf`,
  `fprintf`, `scanf`, `fscanf`, the `_modular` variants) stops with `variadic function definitions are not supported
  for MSIL yet` (M26).
- **Candidate freestanding list: 546 of 561 compile.** The list covers string, strings, ctype, stdlib, math without
  f128/f16, inttypes, stdbit, search, wchar, time, locale, stdio, errno and setjmp. The 15 failures fall into 7
  classes (section 2). None of them is a backend defect, and only 4 of them need M26. Across all 894 sources tried
  (adding fenv, complex, wctype, f16, f128 and the baremetal variants), 835 compile.
- **Linked and run.** A `-shared` `c.dll` built by msil-ld holds 13 libc entry points plus the errno provider. It has
  **0 `pinvokeimpl` and 0 `moduleref`**. A program calls `strtod`, `strtol`, `qsort` with its own comparator (a
  cross-assembly managed `calli`), `sinf`, `exp`, `pow`, `sqrt`, the string functions, `vsnprintf` with
  `%d|%s|%.3f|%x`, `%e|%g|%.10a|%lld|%llu`, `%Lf|%zu|%-3s|%#o` and truncation, and errno (`ERANGE` through
  `__llvm_libc_errno`). It **exits 42 on .NET Framework 4.8, .NET 10 Windows, and WSL .NET 10 (tiered and TC=0)**.
  Function-pointer identity of `&strlen` holds between the program and `c.dll` on 4.8 and 10. ILVerify reports only
  errors on the allow-list.
- **Two new port defects, both measured.**
  - **(D1)** `__llvm_libc_errno` is never exported from `c.dll`. The declaration in `src/__support/libc_errno.h` sits
    inside `LIBC_NAMESPACE_DECL`, which is `[[gnu::visibility("hidden")]]`. `libc_errno.o`'s reference is therefore
    `GLOBAL HIDDEN UNDEFINED`, and msil-ld's ELF-style visibility merge hides the definition (`mayBeExported` in
    `lld/MSIL/Exports.cpp`). The `errno` macro cannot work across assemblies until that declaration has default
    visibility. One attribute fixes it (measured).
  - **(D2)** `vsscanf` needs `malloc`, `realloc` and `free` (scanf's float converter uses `CharVector`). P-1b ships no
    heap (`malloc` is OS-dependent and needs MC-1), so `vsscanf` cannot ship in P-1b, and `sscanf` cannot ship after
    M26 either until P-2 provides `malloc`.
- **Atomics and TLS in single-thread mode.** The only undefined `__atomic_*` names are
  `__atomic_load_8`/`__atomic_compare_exchange_8` (`rand`) and `__atomic_store_8` (`srand`). There are no `__sync_*`
  names. Raw `thread_local` appears only in `src/__support/HashTable/randomness.h`, which ignores the thread mode and
  makes `hcreate`, `hcreate_r` and `hsearch` fail. `memset_explicit` calls `__msil_compiler_barrier`, which the
  trailer defines.
- **Recommendations.**
  - errno: **`LIBC_ERRNO_MODE_EXTERNAL` from P-1b on**, with `OSUtil/dotnet` defining `__llvm_libc_errno`. P-1b
    backs it with one cell, and P-3 swaps the body for the per-thread C block without a mode change or an ABI change.
  - stdio for P-2: **the generic `File` class** (`src/__support/File/file.h`), with a new
    `src/__support/File/dotnet` platform half. Do not extend the baremetal cookie stdio.
  - P-1b can ship **about 510 entry points** whose closure is free of heap, atomics, exit, clock, File, environment
    and time zone (section 5.6).

## 1. Method

### 1.1 The approximation of `msil64-unknown-dotnet`

Response file: `<scratch>/p0/libc/flags.rsp`. Driver script: `cc.sh`.

- `--target=x86_64-unknown-linux-gnu-msil` (data model `8,8,4,8`, `long double` = 64-bit, `wchar_t` = `int`).
  `-U` removes every host macro the target predefines: `__ELF__`, `__linux__`/`__linux`/`__gnu_linux__`/`linux`,
  `__unix__`/`__unix`/`unix`, `__x86_64__`/`__amd64__`, `__SSE*`, `__MMX__`, `__FXSR__`, `__k8*`, `__SEG_FS/GS`,
  `__FLOAT128__` and `__SIZEOF_FLOAT128__`. It adds `-D__dotnet__=1 -D__msil64__=1`. Clang still predefines
  `__MSIL__`. The macro names are this probe's assumption; P-1a fixes the real ones.
- `-x c++ -std=c++17 -O2 -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fvisibility=hidden`, plus
  `-DLLVM_LIBC_FUNCTION_ATTR=__attribute__((visibility("default")))`, `-DLIBC_NAMESPACE=__llvm_libc_dotnet`,
  `-DLIBC_COPT_PUBLIC_PACKAGING` and `-DLIBC_FULL_BUILD`.
- Configuration:
  - `LIBC_THREAD_MODE_SINGLE` and `LIBC_ERRNO_MODE_EXTERNAL`.
  - `LIBC_MATH=(LIBC_MATH_NO_EXCEPT|LIBC_MATH_ASSUME_ROUND_NEAREST_ONLY)`. This keeps math errno, as
    state-homes-research recommends (`math_errhandling` = `MATH_ERRNO`).
  - `LIBC_COPT_PRINTF_DISABLE_FIXED_POINT`, `LIBC_ADD_NULL_CHECKS` and `LIBC_QSORT_IMPL=LIBC_QSORT_QUICK_SORT`.
  - Float printf and scanf stay enabled. Baremetal disables them; dotnet must not.
- `-nostdinc -isystem <clang resource include>`. The include order is: an overlay
  (`<scratch>/p0/libc/overlay`) before `libc`, then stub headers (`stubinc/`), then
  `libc/include`.
- Overlay contents (scratch models of P-1b files):
  - `overlay/src/__support/OSUtil/io.h` is a copy with a `__dotnet__` branch.
  - `overlay/src/__support/OSUtil/dotnet/{io.h,io.cpp,exit.cpp,errno.cpp}`: `write_to_stderr` and `internal::exit`
    are `__builtin_trap()`, and `errno.cpp` is the EXTERNAL provider.
  - `overlay/include/llvm-libc-types/fenv_t.h` is a copy with an `__MSIL__` branch.

Differences from a real P-1b build. None of them changes a conclusion.

- **No CMake and no hdrgen.** `stubinc/search.h` and `stubinc/inttypes.h` stand in for generated headers.
- **`NDEBUG` was not defined**, so `LIBC_ASSERT` is live. This adds references to `write_to_stderr` and
  `internal::exit`, which a `-DNDEBUG` build does not have (measured on `vsnprintf`, `qsort` and `vsscanf` in
  `obj/ndebug/`).
- **`-DLIBC_ENABLE_CONSTEXPR=1` was missing.** `src/__support/wctype/CMakeLists.txt` sets it. Without it,
  `wctype_conversion_utils.cpp` fails because `math::log` (`src/__support/math/log.h`) is not `constexpr`. This is a
  probe artifact.
- **The va_list ABI is still SysV.** The backend still applies the SysV x86-64 C ABI, so `va_list` is
  `__va_list_tag*`. The run tests build that structure by hand: every register slot is marked used and the
  arguments sit in the overflow area. M26 gives the portable triples `MSILCharPtr`. `va_arg` is lowered by clang
  either way, and `v*printf`'s IL signature stays `(native int, native int, native int, native int)`.

### 1.2 Commands

```sh
# compile one source (see cc.sh; runall.sh/runextra.sh run the lists 2-way parallel)
clang @flags.rsp -DLIBC_ERRNO_MODE=LIBC_ERRNO_MODE_EXTERNAL -c libc/src/<file>.cpp -o obj/all/<name>.o
python -I analyze.py        # pass/fail, failure classes, undefined symbols (llvm-nm -u)
python -I counts.py         # counts by category
python -I p1bset.py         # P-1b shippable closure
# link c.dll (run/objs-c2.txt lists the objects)
clang --target=x86_64-unknown-linux-gnu-msil -shared -nostdlib $(cat objs-c2.txt) \
  .../x86_64-unknown-linux-gnu-msil/libclang_rt.builtins.a -o c.dll -Wl,-Map=c.map
clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -DWITH_VSNPRINTF -c prog.c -o prog.o
clang --target=x86_64-unknown-linux-gnu-msil -nostdlib prog.o c.dll -o prog.exe
./prog.exe; dotnet prog.exe; wsl ... bash run/wslrun.sh      # each exits 42
llvm-cil-dump c.dll | grep -c -i pinvokeimpl                  # 0
ilverify c.dll -r "<.NET 10 shared dir>/*.dll"                # 236 errors, all in msil/utils/ilverify-allowed.txt
```

WSL note: `wsl -d Ubuntu -- bash -c '... $? ...'` expands `$?` and `$(...)` in the outer shell, which gives a false
`exit=0`. Run a script file instead (`run/wslrun.sh`).

### 1.3 Re-checks of the earlier attempt

- `results-all.txt` row `127 math/generic/remainderf128.cpp` was an interrupted compile. Rerun: an fp128 failure.
- The 7 `search.h` and `imaxdiv_t` failures ran before `stubinc/` existed (log time 16:53 against stubinc 16:54).
  Rerun: `hdestroy`, `hdestroy_r`, `insque`, `remque` and `imaxdiv` compile. `hcreate`, `hcreate_r` and `hsearch`
  then fail on the `thread_local` class.
- The extra lists (`list-extra.txt`: `__support`, baremetal and generic stdio) had never run. They ran now
  (`results-extra.txt`, `results-bm.txt`).

## 2. Measurement 1: stdio, printf_core, scanf_core and the include chain

### 2.1 What compiles

| source | result |
|---|---|
| `src/stdio/vsnprintf.cpp`, `vsprintf.cpp`, `vasprintf.cpp` (printf_core: `src/__support/printf_core/printf_main.h`, `parser.h`, `converter.h`, `float_dec_converter.h`, `float_hex_converter.h`, `int_converter.h`, `strerror_converter.h`, `writer.h`) | compile, 0 warnings |
| `src/__support/printf_core/float_impl.cpp` | compiles (no symbols in this configuration) |
| `src/stdio/vsscanf.cpp` (scanf_core: `src/stdio/scanf_core/scanf_main.h`, `parser.h`, `converter.h`, `float_converter.h`, `string_reader.h`) | compiles; undefined `malloc`/`realloc`/`free` (D2) |
| `src/stdio/snprintf.cpp`, `sprintf.cpp`, `sscanf.cpp`, `asprintf.cpp`, `*_modular.cpp`, `generic/{printf,fprintf,scanf,fscanf}.cpp`, `baremetal/{printf,fprintf,scanf,fscanf,printf_modular}.cpp` | `variadic function definitions are not supported for MSIL yet` (M26) |
| `src/stdio/generic/*.cpp` (File-class stdio: fopen, fread, fwrite, fseek, ungetc, setvbuf, ...) and `src/__support/File/file.cpp` | compile (the 4 variadic ones aside); undefined only the platform half (section 5.5) |
| `src/stdio/baremetal/*.cpp` with an `OSUtil/dotnet/io.h` that re-exports the baremetal cookie interface (`overlay2/`) | 31 of 36 compile; the 5 failures are variadic |
| `src/stdio/baremetal/*.cpp` with the trap-only `dotnet/io.h` | `__llvm_libc_stdio_*` and `ssize_t`/`off_t` undeclared: baremetal stdio requires the cookie interface in `OSUtil/<os>/io.h` |

### 2.2 The include chain: what a dotnet branch picks

- **`hdr/errno_macros.h`.** Without `__linux__` and `__APPLE__`, the `#else` takes
  `include/llvm-libc-macros/generic-error-number-macros.h`. That is enough for printf, scanf and strto*. The
  generic set has 36 names: `EPERM`..`EPIPE` 1-32, `EDOM` 33, `ERANGE` 34, `EOVERFLOW` 35, `EILSEQ` 84. It lacks
  `ENOSYS`, so `src/wchar/swprintf.cpp` fails with `use of undeclared identifier 'ENOSYS'`. It also lacks
  `ENOTSUP`, `ETIMEDOUT`, `EDEADLK`, `EWOULDBLOCK`, `ENAMETOOLONG` and the other names P-2 and P-3 need. Its
  `EOVERFLOW` 35 is Linux's `EDEADLK`. Linux's `EOVERFLOW` is 75, while 1-34 and `EILSEQ` 84 match Linux.
  `include/errno.h.def` (the public `<errno.h>`) has the same three-way branch. The numbering is visible ABI from
  the first `c.dll` (`strtod` sets `ERANGE`). Proposal in 5.3; question Q1.
- **`src/__support/printf_core/error_mapper.h`** takes `generic/error_mapper.h` (`internal_error_to_errno` maps to
  `EDOM`/`ERANGE` only). That is acceptable, and no dotnet file is needed.
- **`include/llvm-libc-macros/time-macros.h`** takes the `#else` (`CLOCKS_PER_SEC 1000000`), which has no
  `CLOCK_REALTIME`, so `src/time/time.cpp` fails. Its `#elif defined(__ELF__)` baremetal branch is skipped because
  the portable triples do not define `__ELF__`. A dotnet branch is needed by P-2 (clocks over `Stopwatch`/`DateTime`).
- **`src/__support/OSUtil/io.h` and `src/stdlib/abort_utils.h`** key baremetal on `defined(__ELF__)`. With no
  `__ELF__`, `io.h` includes nothing and `abort_utils.h` includes nothing, so `src/stdlib/abort.cpp` fails with
  `unknown type name 'abort'` (no `LLVM_LIBC_FUNCTION`). Each needs a dotnet branch. These are the only
  `__ELF__`-keyed selectors in `libc/src`, `libc/include` and `libc/hdr`: `abort_utils.h:18`, `OSUtil/io.h:31` and
  `time-macros.h:8`.
- **`include/llvm-libc-types/fenv_t.h`** ends in `#error "fenv_t not defined for your platform"`. The probe's
  `__MSIL__` branch (`unsigned int __fpc`, as the GPU branch has) lets all 16 fenv entry points compile.
- **`include/llvm-libc-types/__jmp_buf.h`** ends in `#error "__jmp_buf not available for your target
  architecture."`. On MSIL, setjmp/longjmp are the compiler family (M27's `MSILLowerSjLj`, `'MSJ1'` 16-byte prefix,
  plan "The `jmp_buf` prefix (any triple ...)"), so the library defines only the type. The dotnet branch must give an
  opaque array of at least 16 bytes. That layout is frozen ABI.
- **`include/llvm-libc-types/struct_tm.h`** adds `tm_gmtoff` and `tm_zone` only under `__linux__`. Decide this at
  P-1b (5.3, Q3): adding them later changes `sizeof(struct tm)`.
- **`include/llvm-libc-macros/signal-macros.h`** has no branch, so `src/__support/StringUtil/signal_to_string.cpp`
  fails (`SIGINT` undeclared) and `strsignal` cannot link. Signal numbers are ABI too.
- **`src/__support/macros/properties/architectures.h`** already has `LIBC_TARGET_ARCH_IS_VM`, under `__CLR_VER` or
  GPU, which suppresses the x86 arch macros. P-1b adds `LIBC_TARGET_ARCH_IS_MSIL` (keyed on the portable arch macro)
  and includes it in `IS_VM`. `src/__support/macros/properties/os.h` gets `LIBC_TARGET_OS_IS_DOTNET`.

## 3. Measurement 2: the candidate freestanding list

894 sources were compiled: 830 earlier, re-checked, plus 5 recovered by the rerun. `counts.py` groups them by
category. "Candidate" is the task's list: string, strings, ctype, stdlib, math without f128/f16, inttypes, stdbit,
search, wchar, time, locale, stdio, errno and setjmp.

| category | ok | fail | failures |
|---|---|---|---|
| string | 43 | 0 | |
| strings | 12 | 0 | |
| ctype | 30 | 0 | |
| inttypes | 6 | 0 | |
| stdbit | 70 | 0 | |
| math (generic, no f16/bf16/f128) | 243 | 0 | |
| search | 13 | 3 | hcreate, hcreate_r, hsearch |
| wchar | 56 | 2 | fputwc, swprintf |
| locale | 7 | 0 | |
| stdlib | 47 | 4 | `__cxa_thread_atexit_impl`, abort, environ_internal, strfromf128 |
| time | 14 | 1 | time |
| stdio (the 8 sprintf/sscanf family) | 4 | 4 | snprintf, sprintf, sscanf, asprintf |
| errno | 1 | 0 | |
| setjmp (`setjmp/wasm/sigsetjmp.cpp`, the only non-arch one) | 0 | 1 | sigsetjmp |
| **candidate total** | **546** | **15** | of 561 |
| extra: fenv 16/0, complex 16/0 (+f16 4/0, f128 0/4), wctype 16/0, math f16/bf16 187/0, math f128 39/38, stdlib/baremetal (malloc family) 6/0, time/baremetal 2/2, printf_core 1/0, OSUtil/dotnet overlay 2/0 | 289 | 44 | of 333 |

Failure classes of the candidate list:

1. **Variadic definitions (M26), 4:** `src/stdio/{snprintf,sprintf,sscanf,asprintf}.cpp`. This is the expected M26
   dependency. `swprintf` is also variadic.
2. **`thread_local` in single-thread mode, 3:** `src/search/{hcreate,hcreate_r,hsearch}.cpp` through
   `src/__support/HashTable/randomness.h` (`LIBC_INLINE_VAR thread_local HashState state`, `thread_local uint64_t
   counter`). These use raw `thread_local`, not `LIBC_THREAD_LOCAL` (`src/__support/macros/attributes.h`, empty in
   single-thread mode). The backend error is `thread-local variable ... is not supported for MSIL yet` (until M31).
   Fix: an upstream-able one-word change to `LIBC_THREAD_LOCAL`, or leave hsearch out until M31. `hsearch` would
   also need `getrandom` seeding, which is OS-dependent.
3. **No OS branch (OS-dependent, so absent in P-1b by decision), 5:**
   - `stdlib/abort.cpp`: `abort_utils.h` has no branch.
   - `stdlib/__cxa_thread_atexit_impl.cpp`: `#error "Unsupported OS for Thread Control Block"`.
   - `stdlib/environ_internal.cpp`: `app` from `config/app.h`, which has no branch.
   - `time/time.cpp`: `CLOCK_REALTIME` is missing.
   - `setjmp/wasm/sigsetjmp.cpp`: `__jmp_buf` `#error`. setjmp is a compiler family on MSIL, so no libc setjmp
     entry points ship at all.
4. **errno set too small, 1:** `wchar/swprintf.cpp` needs `ENOSYS`.
5. **Upstream header hygiene, 1:** `wchar/fputwc.cpp` uses `LIBC_ASSERT` without including
   `src/__support/libc_assert.h`. Linux builds get it transitively. It is OS-dependent anyway (it needs `File`), so
   it is absent in P-1b and fixed at P-2.
6. **fp128, 1:** `stdlib/strfromf128.cpp`, out of scope. As a side note, 39 `*f128` math sources compile through
   llvm-libc's software `fputil::Float128` (e.g. `ceilf128`, `sqrtf128`), and 38 fail on `float128`.
7. **Probe artifacts, fixed by rerun:** generated `search.h` and `inttypes.h`, which P-1b's hdrgen produces.

Undefined non-libc symbols across all 835 objects (`analyze.py`), with the number of objects that reference each:

- **Half-precision helpers:** `__extendhfsf2` 68 and `__truncsfhf2` 21, from the f16/bf16 sources. They are in
  compiler-rt builtins, which P-1a must build for msil64.
- **Heap:** `malloc` 8, `free` 2, `realloc` 2, `aligned_alloc` 1, `__llvm_libc_dotnet_delete_sized` 4 and
  `__llvm_libc_dotnet_delete_aligned` 3.
- **Exit and clock:** `__cxa_finalize` 1 (`exit`), and `__llvm_libc_timespec_get_utc`/`_active` (baremetal
  timespec_get).
- **Atomics:** `__atomic_load_8`, `__atomic_compare_exchange_8` and `__atomic_store_8` (section 4).
- **Other:** `__msil_compiler_barrier` 1 (`memset_explicit`; trailer-defined) and `__llvm_libc_errno` 1.

## 4. Measurement 3: link c.dll and run

### 4.1 The library

`run/objs-c2.txt` lists the inputs:

- Entry points: `stdlib/strtod`, `strtol`, `qsort`, `string/strlen`, `strcmp`, `strstr`, `memcpy`,
  `ctype/isalpha`, `math/generic/sinf`, `exp`, `pow`, `sqrt` and `stdio/vsnprintf`.
- Support objects: `errno/libc_errno.cpp`, `__support/StringUtil/error_to_string.cpp`, and the overlay's
  `OSUtil/dotnet/{io,exit,errno}.cpp`.
- `libclang_rt.builtins.a`.

The link takes 7 s and gives `c.dll`, 302,592 bytes. `llvm-cil-dump --link-info` prints
`SymbolTable v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 runtime='none' datamodel=8,8,4,8` and
`ExportClass 'c'`. The export class has exactly the 13 entry points plus `__llvm_libc_errno`, each with its
`'<msil>addr_*'` accessor. Every `LIBC_NAMESPACE` symbol stays private.

- **`pinvokeimpl`: 0. `moduleref`: 0.** The only assemblyref is `mscorlib`.
- **ILVerify:** `c.dll` has 236 errors (InitLocals 70, StackByRef 85, Unverifiable 81) and `prog.exe` has 8. Every
  one matches `msil/utils/ilverify-allowed.txt`; 0 fall outside the allow-list.

### 4.2 Defect D1: `__llvm_libc_errno` is not exported (measured)

In the first link, `__llvm_libc_errno` was missing from the export class, although the overlay defines it with
`visibility("default")` and its own object records `FUNCTION GLOBAL DEFAULT`.
`llvm-cil-dump --link-info obj/all/errno_libc_errno.cpp.o` shows `Symbol 2 FUNCTION GLOBAL HIDDEN UNDEFINED
'__llvm_libc_errno'`. The cause is in `src/__support/libc_errno.h`:
`namespace LIBC_NAMESPACE_DECL { extern "C" int *__llvm_libc_errno() noexcept; ... }`, where `LIBC_NAMESPACE_DECL` is
`[[gnu::visibility("hidden")]] LIBC_NAMESPACE` (`src/__support/macros/config.h`). msil-ld merges visibility as ELF
does and keeps the most restrictive value, so `mayBeExported` (`lld/MSIL/Exports.cpp`) refuses the symbol. SHARED
mode defines the function in `libc_errno.cpp` under the same hidden declaration, so it has the same defect.

Fix, measured: `extern "C" [[gnu::visibility("default")]] int *__llvm_libc_errno() noexcept;` in that header
(`overlay3/`). After the change the reference reads `GLOBAL DEFAULT UNDEFINED` and `c.dll` exports
`__llvm_libc_errno`. The program then reads `ERANGE` (34) after `strtod("1e999")`.

The same rule applies to every C-level variable `c.dll` must export. `LLVM_LIBC_VARIABLE` (`stdout`, `stdin`,
`stderr`) uses `LLVM_LIBC_VARIABLE_ATTR` (`src/__support/common.h`), so the dotnet build sets it to default
visibility alongside `LLVM_LIBC_FUNCTION_ATTR`. It is an upstream conflict surface (one line in `libc_errno.h`).

### 4.3 Programs run

| program | checks | .NET Framework 4.8 | .NET 10 (Windows) | WSL .NET 10 / TC=0 |
|---|---|---|---|---|
| `run/prog.c` | `qsort` with a program comparator (passed into `c.dll`, called back through `calli`), `strtod("2.5e1xyz")` with end pointer, `strtol("-0x1F",16)`, `sinf(0.5f)`, `exp(1.0)` bit-exact, `pow(2.5,2)`, `sqrt(2)`, strlen/strcmp/strstr/isalpha, errno `ERANGE` via `__llvm_libc_errno`, `vsnprintf("%d\|%s\|%.3f\|%x")` | 42 | 42 | 42 / 42 |
| `run/fmt.c` | `vsnprintf` `%e\|%g\|%.10a\|%lld\|%llu`, `%Lf\|%zu\|%-3s\|%#o`, truncation return value 8 with "trun" | 42 | 42 | (not run) |
| `run/fpid.c` + `fpid_lib.c` in `fpc/c.dll` | `&strlen` from two program functions and from `c.dll` compare equal; call through the library's pointer | 42 | 42 | (not run) |
| `run/scan/scan.c` + `scan/c.dll` (**probe-only bump heap**, not a proposal) | `vsscanf("  -42 3.25e2 word 0x1f", "%d %lf %15s %x%n")`: 4 conversions, values and `%n`=22 | 42 | 42 | (not run) |

The critic's P-1b function-pointer identity test (Design.md 4.5) is thus already shown at the probe level.

### 4.4 What fails now, and why

- **`snprintf` (definition):** `src/stdio/snprintf.cpp:25: variadic function definitions are not supported for
  MSIL yet`. **A caller of `snprintf`:** `snp.c:3: calls to variadic functions are not supported for MSIL yet`
  (Status.md, the `variadic-def-xtu` row). Both are M26 (P3-B). After M26 the cross-assembly VARARG call also needs
  5.15.2 rule 1 and `MSILCharPtr` for the portable triples (plan M26 Provides line). That is a one-line hook, as the
  critic noted. `ArgList` is `va_list`-based (`src/__support/arg_list.h`), so no libc change is needed.
- **`vsscanf` in `c.dll` without a heap:** `msil-ld: error: undefined symbol: malloc` (also `realloc` and `free`),
  referenced by `scanf_core::convert_float<StringReader>` and `CharVector::append`. The heap is OS-dependent
  (NativeMemory/AllocHGlobal through MC-1, or msil-ld's per-assembly helpers), so P-1b leaves out `vsscanf`, and
  `sscanf` lands only once both M26 and P-2's `malloc` exist.

## 5. Measurement 4: atomics and TLS in single-thread mode

`llvm-nm -u` over the 835 objects, plus the extra lists, plus the baremetal stdio objects:

- **`__atomic_*`:** `__atomic_load_8` and `__atomic_compare_exchange_8` in `stdlib/rand.cpp`, and
  `__atomic_store_8` in `stdlib/srand.cpp`. Both come from `cpp::Atomic<unsigned long> rand_next`
  (`src/stdlib/rand_util.h`), which is atomic regardless of the thread mode. Every atomic operation is a
  libatomic-interface call on MSIL until M30 (Status.md, the "Atomics" row), and on portable it is undefined until
  M30's inline lowering covers the portable triples or P-3's lock path. So `rand` and `srand` are absent from P-1b.
- **`__sync_*`:** none.
- **TLS:** no `__tls*`, `__emutls*` or `__cxa_thread*` references. `LIBC_THREAD_LOCAL` is empty in single-thread
  mode (`attributes.h`). Two exceptions:
  - The raw `thread_local` in `HashTable/randomness.h` is a compile error, covered in section 3.
  - `stdlib/__cxa_thread_atexit_impl.cpp` hits `#error`.
- **Fences:** `string/memset_explicit.cpp` uses `__msil_compiler_barrier`, a single-thread fence that the trailer
  defines (`MSILISelLowering::LowerATOMIC_FENCE`).
- **Mutex:** `src/__support/threads/mutex.h` gives a no-op `Mutex` in single-thread mode, so the `File` class
  compiles with no lock calls. `LIBC_THREAD_MODE_EXTERNAL`'s branch is an empty TODO, so P-3 needs a
  `LIBC_THREAD_MODE_PLATFORM` dotnet mutex (5.5).

## 6. Proposals

### 6.1 What `libc/config/dotnet` must contain (P-1b)

The layout mirrors `config/baremetal/`. `libc/CMakeLists.txt` lines 297-303 pick `config/${LIBC_TARGET_OS}` and
`config/${LIBC_TARGET_OS}/${LIBC_TARGET_ARCHITECTURE}`.

- **`config/dotnet/config.json`.** Start from `config/baremetal/config.json` and change it as follows:
  - `LIBC_CONF_ERRNO_MODE`: `LIBC_ERRNO_MODE_EXTERNAL` (6.4).
  - `LIBC_CONF_THREAD_MODE`: `LIBC_THREAD_MODE_SINGLE` until P-3 (P-3 sets `PLATFORM`).
  - printf: keep `LIBC_CONF_PRINTF_DISABLE_FIXED_POINT` and `LIBC_COPT_PRINTF_DISABLE_BITINT`. **Enable** float,
    index mode, write-int (`%n`), strerror (`%m`) and wide (`%lc`/`%ls`), which baremetal disables. Measured: float
    and strerror compile and run. Keep `..._USE_MEGA_LONG_DOUBLE_TABLE` false, because `long double` is `double`.
  - scanf: enable float and index mode, which baremetal disables.
  - qsort: `LIBC_QSORT_QUICK_SORT` (the default) or keep heap sort. Either is fine.
  - str_to_float: enable Eisel-Lemire and the Clinger fast path, which baremetal disables for size.
  - math: `LIBC_MATH_NO_EXCEPT | LIBC_MATH_ASSUME_ROUND_NEAREST_ONLY`. **Not** `LIBC_MATH_NO_ERRNO`, `SKIP_ACCURATE_PASS`,
    `SMALL_TABLES` or `INTERMEDIATE_COMP_IN_FLOAT`. ECMA-335 fixes round-to-nearest (I.12.1.3) and libm keeps
    `MATH_ERRNO` (state-homes-research item 6). Accuracy matters more than size on .NET.
  - `LIBC_ADD_NULL_CHECKS` true (traps loudly), `LIBC_CONF_CTYPE_SMALLER_ASCII` as baremetal, and
    `LIBC_CONF_ENABLE_STRONG_STACK_PROTECTOR` false.
- **`config/dotnet/msil64/entrypoints.txt`** (and `msil32/` in P-4). This is the P-1b list of section 6.6, a pure
  allow-list. Absent names give link errors, as decided.
- **`config/dotnet/msil64/headers.txt`.** Ship the headers whose entry points ship, plus the type-only headers
  (`assert.h`, `ctype.h`, `errno.h`, `fenv.h`, `float.h`, `inttypes.h`, `limits.h`, `locale.h`, `math.h`,
  `search.h`, `setjmp.h` (type only), `stdbit.h`, `stdint.h`, `stdio.h` (types, `v*printf`), `stdlib.h`,
  `string.h`, `strings.h`, `time.h`, `uchar.h`, `wchar.h`, `wctype.h`, `complex.h`).
- **`config/dotnet/app.h`?** Not needed in P-1b. `config/app.h` picks per OS, and dotnet needs args and environment
  only at P-2 (`environ_internal.cpp` references `app`). P-2 adds a `dotnet/app.h` filled by `'<msil>Main'` or by
  `Environment.GetCommandLineArgs`.
- **CMake:**
  - `cmake/modules/LLVMLibCArchitectures.cmake`: the `get_arch_and_system_from_triple` arch regexes gain
    `^msil64` → `msil64` and `^msil32` → `msil32`; `LIBC_TARGET_OS` `dotnet` → `LIBC_TARGET_OS_IS_DOTNET`; a
    `LIBC_TARGET_ARCHITECTURE_IS_MSIL` branch. Today `unknown`/`none` OS means baremetal, at lines 163-165, so the
    sys component must be `dotnet`.
  - `cmake/modules/LLVMLibCCompileOptionRules.cmake`: `-fvisibility=hidden` as GPU does, plus
    `-DLLVM_LIBC_FUNCTION_ATTR=[[gnu::visibility("default")]]` and the same for `LLVM_LIBC_VARIABLE_ATTR`.
  - A `-shared` link rule that produces `c.dll` (M22), and `-DNDEBUG` in release.
- **Source and header branches keyed on `LIBC_TARGET_OS_IS_DOTNET`** (an OS macro from the portable triple; P-1a
  names it):
  - `src/__support/macros/properties/os.h` defines `LIBC_TARGET_OS_IS_DOTNET`.
  - `src/__support/macros/properties/architectures.h` defines `LIBC_TARGET_ARCH_IS_MSIL` and adds it to
    `LIBC_TARGET_ARCH_IS_VM`.
  - `src/__support/OSUtil/io.h` gets a dotnet branch before `__ELF__`.
  - `src/stdlib/abort_utils.h` gets a dotnet branch, used by P-2.
  - `include/llvm-libc-macros/time-macros.h` gets a dotnet branch before `__ELF__`.
  - `hdr/errno_macros.h` and `include/errno.h.def` get a dotnet branch.
  - `include/llvm-libc-types/fenv_t.h`: `__MSIL__`/msil branch.
  - `include/llvm-libc-types/__jmp_buf.h`: msil branch with an opaque array of at least 16 bytes.
  - `include/llvm-libc-types/struct_tm.h`: Q3.
  - `include/llvm-libc-macros/signal-macros.h`: dotnet branch (Q4).
  - `src/__support/libc_errno.h`: default-visibility declaration (D1).
  - Upstream-able clean-ups: `src/__support/HashTable/randomness.h` → `LIBC_THREAD_LOCAL`; `src/wchar/fputwc.cpp` →
    include `libc_assert.h`.

### 6.2 What `libc/src/__support/OSUtil/dotnet` must contain (P-1b)

Keep the directory minimal. P-1b has no OS layer, by decision:

- **`io.h`**: declares `void write_to_stderr(cpp::string_view)`. For P-2 it also declares the stdio hooks of the
  chosen model (6.5).
- **`io.cpp`**: `write_to_stderr` is `__builtin_trap()` in P-1b. It is loud, and it is reached only from
  `LIBC_ASSERT` in debug builds. With `-DNDEBUG`, no shipped entry point references it (measured for `vsnprintf`).
  P-2 replaces it with the managed `Console.Error` path through MC-1.
- **`exit.cpp`**: `internal::exit` is `__builtin_trap()`. In P-1b it is referenced only by the absent `exit`,
  `_Exit` and `quick_exit`, and by debug asserts. P-2 makes it `Environment.Exit` through MC-1.
- **`errno.cpp`**: `extern "C" int *__llvm_libc_errno() noexcept`, with default visibility. P-1b backs it with one
  `int` cell. P-3 points it at the per-thread C block (named thread data slot). Measured in
  `overlay/src/__support/OSUtil/dotnet/errno.cpp`.
- **`CMakeLists.txt`**: `osutil` object library and errno provider target.
- **Not in P-1b:** `syscall.h` (dotnet has no syscalls; `OSUtil/syscall.h` must not be reached), and the
  baremetal cookie hooks.

### 6.3 The C ABI fixed at P-1b (frozen once `c.dll` ships)

These are visible to programs through headers, so P-1b fixes them even though state sharing starts only at P-3:

- errno numbers (Q1).
- `struct tm` (Q3).
- `jmp_buf` size, with an opaque array of at least 16 bytes.
- `fenv_t` (`unsigned int`).
- `wchar_t`: 32-bit signed, by decision.
- `mbstate_t` and `div_t`/`ldiv_t`/`imaxdiv_t`, which are layout-neutral.
- `time_t`: 64-bit on both widths (`time_t_64.h` is the non-Apple default). P-4 must keep it 64-bit on msil32.
- `struct timespec`: `time_t` plus `long`, which changes width on msil32. Note it for P-4.
- Signal numbers, if `strsignal` ships (Q4).
- `FILE` is opaque, so its layout stays private to `c.dll` until P-3 shares it.

### 6.4 The errno mode: EXTERNAL from P-1b

This resolves the critic's inconsistency in favour of EXTERNAL.

- **THREAD_LOCAL** needs C `thread_local` (M31), and it gives each `-static` copy its own errno, which breaks the
  shared-state decision. Rejected.
- **SHARED** puts `shared_errno` in `src/errno/libc_errno.cpp`: one cell per copy, no thread safety. It works for
  P-1b, but P-3 would have to switch modes, because errno must become per-thread and process-wide across `-static`
  copies. It also has defect D1.
- **EXTERNAL** makes `libc_errno.cpp` forward `Errno::operator=`/`operator int` to `__llvm_libc_errno()`, and the
  embedder (here `OSUtil/dotnet/errno.cpp`, part of the libc) defines it. Its public ABI (`errno` =
  `(*__llvm_libc_errno())`, `include/errno.h.def`) is the same in every phase. Only the body changes:
  - P-1b and P-2: one cell.
  - P-3: per-thread block.
  - P-3 `-static`: each private copy's `__llvm_libc_errno` finds the same per-thread block through the named data
    slot, so there is one errno per thread per process, as decided.

  Measured: it links and runs with the D1 fix.

**Recommendation: EXTERNAL from P-1b, with `__llvm_libc_errno` defined in `OSUtil/dotnet/errno.cpp` and exported
from `c.dll`.** The recommendation text's line "With errno SHARED, `shared_errno` is a per-copy global" should then
read "errno EXTERNAL: `OSUtil/dotnet` defines `__llvm_libc_errno`, one cell until P-3."

### 6.5 The stdio model for P-2: the generic `File` class

- **Baremetal cookie stdio** (`src/stdio/baremetal/`, `src/__support/OSUtil/baremetal/io.h`):
  - `FILE*` is a cookie owned by the embedder.
  - The embedder also defines `__llvm_libc_stdio_{read,write,seek,close}` and `__llvm_libc_std{in,out,err}_cookie`
    (extern "C").
  - The library does no buffering: `write_internal` calls the hook on every write.
  - Only the 3 standard streams exist. There is no `fopen`, `freopen`, `setvbuf` or `fflush` list.
  - Measured: 31 of 36 sources compile once `dotnet/io.h` declares the cookie interface.

  P-2 would still have to write the hooks (the OS layer), and then extend it with fopen and buffering, which is
  `File` reinvented.
- **Generic File** (`src/__support/File/file.h`, `file.cpp`, `src/stdio/generic/*.cpp`):
  - Buffered, with `_IOLBF`/`_IOFBF`/`_IONBF`.
  - `ungetc`, `fopen`/`freopen`/`fclose`/`fmemopen`/`fopencookie`, the file list for `flush_all_streams` at exit,
    and per-file locks.
  - Measured: the whole generic set and `file.cpp` compile (the variadic ones aside). Undefined are only the
    platform half (`openfile`, `LIBC_NAMESPACE::stdin`/`stdout`/`stderr`, `internal::exit`) and the heap.
  - The platform half is a `File` subclass with three or four static functions, as in
    `src/__support/File/linux/file.h` (`LinuxFile`: `linux_file_write`, `linux_file_read`, `linux_file_seek`,
    `linux_file_close`, `openfile`, `create_file_from_fd`) and `src/stdio/linux/stdout.cpp` (`static LinuxFile
    StdOut(...)`, `LLVM_LIBC_VARIABLE(FILE *, stdout)`).

**Recommendation: P-2 uses generic File.** P-2 adds the following:

- `src/__support/File/dotnet/{file.h,file.cpp}`: `DotnetFile : File` holding a GCHandle to a `System.IO.Stream`
  (FileStream; `Console.OpenStandardOutput()`/`Error`/`Input`). `dotnet_file_write/read/seek/close` call `Write`,
  `Read`, `Seek` and `Dispose` through MC-1, and `openfile` maps C modes to `FileMode`/`FileAccess`.
- `src/stdio/dotnet/{stdin,stdout,stderr}.cpp`.
- `src/stdio/dotnet/CMakeLists.txt`.

Its costs belong in P-2's estimate, as the critic noted. For P-3, three points:

- `File::mutex` (`file.h`, non-recursive) becomes the dotnet `LIBC_THREAD_MODE_PLATFORM` mutex. The upstream
  non-recursive-lock defect (file.h:158-175 per the recommendation) gets fixed then.
- The `File` objects and `File::list_all`/`list_lock` move into the process root for shared state.
- `File`'s function-pointer members are managed `ldftn` pointers into the creating copy. They stay valid process-wide
  while that assembly is loaded, as the default load context does. On Framework they are per AppDomain, which
  matches the per-domain root.

### 6.6 The P-1b entry-point set (measured closure)

`p1bset.py` takes the candidate objects that compile, excludes f16 and f128, and keeps those whose undefined set
contains none of: heap, atomics, exit path, clock, `File`/streams, environment, signal names, the hash-table global.
That leaves **515**: ctype 30, inttypes 6, locale 7, math 243, search 7 (`lfind`, `lsearch`, `tfind`, `twalk`,
`twalk_r`, `insque`, `remque`), stdbit 70, stdio 2 (`vsnprintf`, `vsprintf`), stdlib 38, string 40, strings 12,
time 13 and wchar 47.

**Recommendation: also remove the 5 time-zone-dependent names** (`localtime`, `localtime_r`, `ctime`, `ctime_r` and
`mktime`). `time_utils::localtime_internal` answers in UTC "until timezone database is implemented"
(`src/time/time_utils.h`), which is a silent wrong answer on a machine whose zone is not UTC. That leaves **about
510**. Add them in P-2 with `TimeZoneInfo.Local` through MC-1.

Blocked, with reasons (all measured):

- **heap:** `strdup`, `strndup`, `wcsdup`, `hdestroy`, `hdestroy_r`, `hsearch_r`, `tsearch`, `tdelete`,
  `tdestroy`, `atexit`, `at_quick_exit`, `vsscanf`, `vasprintf`.
- **atomics:** `rand`, `srand`.
- **exit path:** `exit`, `_Exit`, `quick_exit`.
- **environment:** `getenv`.
- **File:** `fgetwc`, `fgetws`, `fputws`, `getwc`, `getwchar`, `putwc`, `putwchar`, `ungetwc`.
- **signal names:** `strsignal`.
- **compile failures** (section 3).

Optional extras that compile with no OS: fenv 16 (they keep a software `fenv_t`, and `fesetround` to anything but
nearest must fail), complex 16, wctype 16, and f16/bf16 187 (these need compiler-rt's half helpers in msil64
builtins).

Program exit without `exit` in P-1b: `'<msil>Main'` returns `main`'s value (measured in `prog.exe`). No libc `exit`
is needed for a P-1b program, but atexit and `stdout` flushing don't exist before P-2.

### 6.7 C globals in the objects (input to the P-3 state contract)

`datasyms.py` lists the writable data the objects define. Each is a per-copy global today, and under P-3's
`-static` shared state it moves to the process root or the per-thread block:

- **Process root:**
  - `libc_errno` (an empty Errno object; harmless).
  - `atexit_callbacks` and `handler_list_mtx` (`stdlib/atexit.cpp`), and `at_quick_exit_callbacks`.
  - `File::list_all` and `File::list_lock`, and `stdin`/`stdout`/`stderr`.
  - `rand_next`, `freelist_heap` (if a libc heap is used), `locale`/`c_locale` (`locale/locale.cpp`), and the
    `setlocale` string.
- **Per-thread** (C allows static buffers, but per-thread is safer):
  - `strtok_str`, `BASE64_BUFFER` (`l64a`), `error_buffer` (`strerror`).
  - The static `tm` and buffers of `gmtime`, `localtime`, `asctime` and `ctime`.
  - The `internal_mbstate` statics of `mbrlen`, `mbrtowc`, `wcrtomb`, `mbstowcs`, `wcstombs`, `mbsrtowcs`,
    `wcsrtombs`, `mbsnrtowcs` and `wcsnrtombs`.

### 6.8 P-1b tests (beyond the recommendation's list)

- `c.dll` exports exactly the entry list plus `__llvm_libc_errno` (an `llvm-cil-dump` CHECK). There are 0
  `pinvokeimpl` and 0 `moduleref` (measured shape).
- The errno round trip across the assembly boundary, which pins D1.
- `qsort`/`bsearch`/`lfind` with program comparators, and `&f` identity (measured).
- `vsnprintf` conversions as in `fmt.c`. Before M26, a hand-built `va_list` is used only on the SysV host; on
  portable these tests wait for M26's `MSILCharPtr`, or use the `v*` forms after M26.
- A program that calls an absent name (`malloc`, `printf` before M26 and P-2) fails the link loudly (`undefined
  symbol`).
- ILVerify allow-list clean.

## 7. Risks

1. **D1 needs a change to an upstream file** (`libc_errno.h`) and the visibility attribute settings. Without them,
   `errno` silently cannot cross into programs: there is a link error on `__llvm_libc_errno`, but the cause is
   hidden.
2. **`sscanf` is later than assumed.** It needs M26 and P-2's heap. Any plan text that promises `sscanf` with M26 or
   at P-1b is wrong.
3. **The errno numbering is frozen at P-1b.** The generic set's `EOVERFLOW`=35 collides with Linux's `EDEADLK`.
   Choose before shipping.
4. **The `__ELF__`-keyed baremetal selectors** (3 files) mean the "not ELF" decision costs one branch each, plus
   `errno.h.def`, `struct_tm.h`, `signal-macros.h`, `fenv_t.h` and `__jmp_buf.h`. These are about 12 upstream
   conflict surfaces in libc. They are small, but they recur at every upstream merge (R21's merge-first rule).
5. **The probe ran on a SysV-ABI host triple.** The portable ABI (DefaultABIInfo-based, M26 `MSILCharPtr`) may
   change byval and sret shapes for libc functions that take or return structs (`div`, `ldiv`, `imaxdiv`,
   `localeconv`, `strfrom*`). Re-run section 4's programs on msil64 at P-1b.
6. **f16 math and fenv** pull in compiler-rt helpers and a software `fenv_t`. If P-1a's msil64 builtins lag, keep
   f16 out of P-1b.
7. **`hsearch` and `rand`** depend on M31 (TLS) and M30 (atomics) through upstream code that ignores single-thread
   mode. A one-word upstream fix (`LIBC_THREAD_LOCAL`) removes the first. The second remains an M30 dependency.
8. **Build time.** The earlier attempt's 894-source run at 2-way parallel took about 20 min of host time
   (`list.txt` 16:50 to the transcript at 17:09) while the post-commit run was active. The math sources dominate. A CMake libc build for two widths doubles that.

## 8. Questions for the user

- **Q1. errno numbering for the portable C ABI, frozen once `c.dll` ships.**
  - (a) Linux (asm-generic) numbering in full, in a dotnet `error-number-macros.h`. It equals llvm-libc's generic
    set except `EOVERFLOW` (75, not 35), and adds `ENOSYS`, `ETIMEDOUT`, `EDEADLK` and the rest.
  - (b) llvm-libc's generic set, appending the new names after 84.
  - (c) The Windows UCRT numbering.

  **Recommendation: (a).** It is familiar, it is a superset, and it matches what C code printing `errno` expects.
- **Q2. `localtime`, `localtime_r`, `ctime`, `ctime_r` and `mktime` in P-1b.** They answer in UTC until a time-zone
  layer exists.
  - (a) Absent until P-2.
  - (b) Ship them, UTC-only.

  **Recommendation: (a),** under the fail-loudly rule.
- **Q3. `struct tm` layout on the portable triples.**
  - (a) Include `tm_gmtoff` and `tm_zone`, as glibc and BSD do.
  - (b) ISO C fields only.

  **Recommendation: (a).** `strftime` `%z`/`%Z` need them after P-2, and adding them later breaks the ABI.
- **Q4. Signal numbers.** Ship `signal.h` macros at P-1b with Linux numbering (so `strsignal` links), or leave
  `strsignal` and `signal.h` out until a signal design exists? **Recommendation:** leave them out until the signal
  design, because there is no `raise` or `signal` on .NET yet. Fix the numbering then.

## 9. Artifacts (`<scratch>/p0/libc/`)

- **Inputs:** `flags.rsp`, `flags-bm.rsp`, `flags-errfix.rsp`, `cc.sh`, `ccbm.sh`, `runall.sh`, `runextra.sh`,
  `list.txt` (894), `list-extra.txt` (105), `list-bm.txt`.
- **Results:** `results-all.txt`, `results-extra.txt`, `results-bm.txt` and `TRANSCRIPT.txt`. Logs are next to each
  object under `obj/{all,extra,bm,rerun,ndebug,errfix,os}/`.
- **Scripts:** `analyze.py`, `counts.py`, `p1bset.py`, `datasyms.py`, `selectors.py`; `obj/undef-all.json`.
- **Overlays:**
  - `overlay/`: the P-1b models (`OSUtil/dotnet`, `io.h`, `fenv_t.h`).
  - `overlay2/`: the cookie `io.h`.
  - `overlay3/`: the D1 fix.
  - `stubinc/`: the hdrgen stand-ins.
- **Runs** (`run/`):
  - `c.dll`, `c.map`, `c.dump.txt`.
  - `prog.c`/`prog.exe`, `fmt.c`/`fmt.exe`, `fpid*.c` with `fpc/`, and `scan/` (probe-only heap).
  - `snp.c` (variadic call), `ilv-c.txt`, `ilv-prog.txt` and `wslrun.sh`.

## Verifier corrections

Adversarial check, 2026-11-01. Scratch: `<scratch>/p0/libc-verify/` (`defs.txt` from `llvm-nm
--defined-only` over `obj/all`, and `closure.py`, a transitive closure of the shipped objects through the support
objects that define their undefined symbols). Nothing else was written in the repository.

### Re-measured and confirmed

- `counts.py`: candidate 546/561, extra 289/333, all 835/894. `list.txt` covers every non-f16/bf16/f128 file in
  `src/math/generic` (243 of 243) and every top-level `.cpp` in string, strings, ctype, inttypes, stdbit, search,
  wchar, locale, stdlib and time. The other top-level stdio files (fmemopen, fopencookie, setbuf, setvbuf, fflush,
  flockfile, funlockfile, `*_modular`) are in `results-extra.txt`.
- `p1bset.py` gives 515, with the per-directory counts and blocked lists as reported. `closure.py` follows support
  objects too (`time_utils.o`, `error_to_string.o`, `__support/wchar/mbrtowc.o`). It adds no heap, atomics, File,
  environment or clock reference. The only extra references are `write_to_stderr` and `internal::exit`, from
  `vsnprintf`, `vsprintf`, `strfromd/f/l` (`src/stdlib/str_from_util.h`, a `LIBC_ASSERT`) and the time family through
  `time_utils.o` (`src/time/time_utils.cpp`, a `LIBC_ASSERT`). Section 5.6's "with `-DNDEBUG` no shipped entry point
  references them" was measured on only 3 objects. Source inspection now confirms it for the rest.
- `c.dll`: re-dumped. 0 `pinvokeimpl`, 0 `moduleref`, `assemblyref 'mscorlib'` only. The export class has exactly
  the 13 entry points plus `__llvm_libc_errno`. `vsnprintf` is `int32(native int x4)`. ILVerify: 236 errors (70
  InitLocals, 85 StackByRef, 81 Unverifiable), all classes on the allow-list.
- Runs repeated. `prog.exe`, `fmt.exe`, `fpc/fpid.exe` and `scan/scan.exe` exit 42 on .NET Framework 4.8 and .NET
  10 (Windows). `prog.exe` exits 42 on WSL .NET 10, tiered and with TC=0.
- D1 is confirmed in the source. `src/__support/macros/config.h` makes `LIBC_NAMESPACE_DECL`
  `[[gnu::visibility("hidden")]]` under clang. `src/__support/libc_errno.h` declares `__llvm_libc_errno` inside it.
  SHARED mode defines the function inside the same namespace in `src/errno/libc_errno.cpp`. `mayBeExported`
  (`lld/MSIL/Exports.cpp`) refuses non-default visibility. One citation is missing: the ELF-style merge itself is
  `mergeVisibility` in `lld/MSIL/Symbols.cpp`.
- The only `__ELF__` selectors in all of `libc/` (tests aside) are `OSUtil/io.h`, `stdlib/abort_utils.h` and
  `llvm-libc-macros/time-macros.h`. The generic errno set has 36 names, with `EOVERFLOW` 35. `fputwc.cpp` lacks the
  `libc_assert.h` include. `randomness.h` uses raw `thread_local`. `mutex.h`'s EXTERNAL branch is a TODO, and its
  PLATFORM branch includes a mutex only under `__linux__` or `__APPLE__`. `rand_next` is a `cpp::Atomic`. The only
  undefined `__atomic_*` names are the three reported, and there are no `__sync`/TLS names.

### Errors

1. **The report treats M26 as later than P-1b, against the binding program order.** The user's order is M26, then
   P-1a, then P-1b. Variadic definitions and calls therefore exist when P-1b ships, provided the msil64 variadic ABI
   is hooked: M26's `VariadicABIInfo::create` gives `MSILCharPtr` only to Windows triples (plan, "The ExpandVariadics
   hook"), so P-1a or P-1r must extend it to msil64.
   - The P-1b set must add `snprintf`, `sprintf`, `vsnprintf_modular`, `vsprintf_modular`, `snprintf_modular` and
     `sprintf_modular`. Their only blocker is variadics, and their closure is the same as `vsnprintf`'s.
   - `swprintf` joins them once the errno set has `ENOSYS` (Q1).
   - `asprintf` and `sscanf` stay out for the heap only (D2).
   - Section 0's "only 4 of them need M26" is 5 (`swprintf` is variadic too), and none of the 5 is blocked at P-1b.
     The finding should say "class 1 is resolved by M26, which precedes P-1b".
   - In 6.8, "on portable these tests wait for M26's `MSILCharPtr`" should read "need P-1a's msil64 variadic ABI
     hook". The plan's P-1b test "snprintf cross-assembly" stays: do not replace it with `v*` forms.
2. **The CMake triple parsing is misdescribed. "The sys component must be `dotnet`" does not work.**
   `get_arch_and_system_from_triple` in `libc/cmake/modules/LLVMLibCArchitectures.cmake` takes the system from index
   `triple_size - 2`. For the 3-component `msil64-unknown-dotnet` that is index 1, `unknown`, which then becomes
   `baremetal`. (`aarch64-none-elf` reaches baremetal the same way.) P-1b needs an explicit special case, like the
   GPU one there that reads component 2: `dotnet` → `LIBC_TARGET_OS dotnet`. The arch regexes fall to
   `else() return()`, which is a FATAL_ERROR, as reported.
3. **The fenv extras are not "a software `fenv_t`".** On an arch with no FEnvImpl, `src/__support/FPUtil/FEnvImpl.h`
   takes the dummy branch, whose own comment reads "All dummy functions silently succeed":
   - `feraiseexcept` returns 0 and records nothing, and `fetestexcept` always returns 0.
   - `fenv-macros.h` defines `FE_DIVBYZERO` and the other exception macros unconditionally.
   - `include/llvm-libc-macros/math-macros.h` advertises `math_errhandling` as `MATH_ERRNO|MATH_ERREXCEPT` for
     anything that is not GPU or ARM-without-FP.

   Shipping fenv as an "optional extra" breaks the fail-loudly rule, and the public `math.h` misstates exception
   support even without fenv. A msil branch in `math-macros.h` (drop ERREXCEPT) is a further header surface.
   Decide fenv explicitly, and do not list it as harmless.
4. **"`hsearch` would also need `getrandom` seeding" is wrong.** `randomness.h` uses `getrandom` only under the
   opt-in `LIBC_HASHTABLE_USE_GETRANDOM`. Otherwise the seed is fixed, so the `LIBC_THREAD_LOCAL` change alone
   unblocks `hcreate`, `hcreate_r` and `hsearch` in single-thread mode.
5. **Counting slips.**
   - The 15 candidate failures fall into 6 classes, not 7: class 7 ends with 0 after the rerun.
   - The 515 includes `locale/locale.cpp`, a support object that defines `locale` and `c_locale`, so there are 514
     entry points.
   - Section 3's "894 sources were compiled: 830 earlier ... plus 5" describes the 835 that compile, not the 894.
   - Risk 1 says errno "silently" fails and also that there is a link error. It is a link error with a hidden cause.
6. **`setjmp.h` "type only" is too little.** M27's `MSILLowerSjLj` intercepts calls to the declarations
   `setjmp`/`longjmp`/`sigsetjmp`/`siglongjmp`. So the portable `setjmp.h` must declare them, as functions that no
   library defines, or C programs cannot call setjmp at all.
   - `jmp_buf` "at least 16 bytes" is a weak freeze. The plan's prefix table reserves offsets 64-199 for the
     sigsetjmp mask on Linux triples, and fits glibc's 200 bytes.
   - Freezing 16 forecloses a later mask or other fields. Recommend a glibc-sized (200-byte) opaque `jmp_buf` and
     `sigjmp_buf` for msil64, and record the msil32 size in P-4.

### Missed sites (selectors that compile silently on dotnet with a wrong default)

- `src/__support/StringUtil/platform_errors.h` (keyed on `__linux__`, Fuchsia or Emscripten). Otherwise it takes
  `tables/minimal_platform_errors.h` (STDC only: 0, `EDOM`, `ERANGE`, `EILSEQ`). `strerror`, `strerror_r`, printf
  `%m` and `%#m` (`try_get_errno_name`), all in the P-1b set, print "Unknown error N" for every POSIX errno. Q1(a)
  needs a dotnet branch to `linux_platform_errors.h`.
- `src/time/time_utils.h` (keyed on `__linux__`). `gmtime_internal` and `localtime_internal` set
  `tm_gmtoff`/`tm_zone` only on Linux. `get_timezone_name()` returns `"UTC"` and `get_timezone_offset()` returns 0
  elsewhere. So Q3(a) needs branches here as well as in `struct_tm.h`.
- `strftime` and `strftime_l` ship in the 515. Their `%Z` always prints `UTC` and `%z` always prints `+0000` on a
  non-Linux branch, whatever the `struct tm`. This is the same silent-wrong-answer class as `localtime`, so Q2
  should cover them (drop them, or ship them with `%z`/`%Z` failing).
- `include/llvm-libc-macros/math-macros.h`: `math_errhandling` (error 3). Also P-1a must keep the dotnet toolchain's
  `ToolChain::IsMathErrnoDefault()` true. Otherwise clang defines `__NO_MATH_ERRNO__`, `math_errhandling` loses
  `MATH_ERRNO`, and that contradicts the recommended MATH_ERRNO configuration.
- The upstream-conflict count (risk 4, "about 12") should add `platform_errors.h`, `time_utils.h`, `math-macros.h`
  and the CMake system special case, which makes about 16.

### Gaps

- **ABI freeze list (6.3).** It omits `struct lconv` (`localeconv` ships in the 515) and `wint_t` (from the
  compiler's `__WINT_TYPE__`, so P-1a fixes it). `mbstate_t` is 8 bytes (`uint32_t`, two `uint8_t`) and joins the
  frozen list.
- **compiler-rt.** `c.dll` linked the host triple's `libclang_rt.builtins.a`, which was compiled with `__ELF__`. The
  "no `__ELF__`" condition was applied to libc only. compiler-rt's `int_lib.h` `#error` (critic MUST-FIX) and an
  msil64 builtins archive are untested here.
- **Exports under `-static` (P-3).** `LLVM_LIBC_FUNCTION_ATTR`/`LLVM_LIBC_VARIABLE_ATTR` set to default visibility
  make every libc entry point, and `__llvm_libc_errno`, exportable from any `-shared` assembly that links `libc.a`.
  Two such DLLs would both export `strlen`. msil-ld's dotnet rule family needs an exclude-libs-style rule for the
  `-static` mode. Record it as a P-3 input next to the EXTERNAL-errno proposal.
- **Trap stubs and the decisions.** The recommendation itself proposed `write_to_stderr`/`internal::exit` as
  `__builtin_trap`. They are reached only by `LIBC_ASSERT`, so a release `c.dll` (`-DNDEBUG`) should not contain
  them at all. Add a P-1b CHECK that the release `c.dll` has no `write_to_stderr`/`exit` bodies. That keeps "absent,
  link error" literally true for shipped builds.
- **The heap alternative "msil-ld's per-assembly helpers"** (4.4) would be an interim intrinsic OS layer before
  MC-1, which the decisions forbid. Keep the heap at P-2 (MC-1) only.
