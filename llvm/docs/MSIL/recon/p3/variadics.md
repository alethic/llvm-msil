# Phase 3 recon: variadic functions (P3-B)

Key: `variadics`. Scope: calls to native variadic functions (`printf`, `snprintf`, `open`, `ioctl`, `fcntl`,
`syslog`), calls to variadic functions defined in C (same object, other object, other assembly), variadic definitions,
`va_list`/`va_start`/`va_arg`/`va_copy`/`va_end`, and the v-variants. Base: branch `msil`, HEAD `cc4c79787d6a` (M23).
Supporting files: `llvm/docs/MSIL/recon/p3/variadics-files/` (sources, scripts, logs; no binaries). Scratch:
`C:\llvm-msil-scratch\p3\variadics\`. Every number below was measured on this host (Windows 11, shared and loaded;
WSL Ubuntu with the private .NET 8.0.31 and 10.0.13), on .NET Framework 4.8 (4.0.30319.42000), .NET 8.0.33 and
.NET 10.0.14 on Windows, and on .NET 8.0.31 and .NET 10.0.13 under WSL, unless a line says otherwise.

## 0. Summary

1. **Today** every variadic definition and every call of a variadic function is a compile-time error
   (`MSILPrepareIR.cpp:1075`, `MSILISelLoweringCall.cpp:402-422`). On `x86_64-pc-windows-msvc-msil` even `printf`
   is a *definition*: UCRT's `<stdio.h>` defines the whole printf/scanf family inline as `linkonce_odr` variadic
   functions that call `__stdio_common_v*` with a `va_list`, so the Windows triple reports `variadic function
   definitions are not supported for MSIL yet` at `stdio.h:950` for a plain `printf` call. A `va_list` parameter,
   clang's inline `va_arg`, `va_copy` and `va_end` already compile (M10 fix-6), and passing a `va_list` to native
   `vprintf` already links and runs. Phase 2 gap found on the way: `clang --target=x86_64-pc-windows-msvc-msil -O0
   -flto` of *any* file that includes `<stdio.h>` fails (`in function sprintf ... variadic function definitions are
   not supported`), because the inline UCRT definitions survive into msil-ld's LTO codegen; P3-B removes it.
2. **CLR measurements** (section 2):
   - The CLR `vararg` P/Invoke (`__arglist`) works on Windows (Framework 4.8, .NET 8, .NET 10) and throws
     `System.InvalidProgramException: Vararg calling convention not supported.` on Linux (.NET 8 and 10).
   - A fixed-arity P/Invoke per call site is **wrong on Win64 for a floating-point value in the first four argument
     positions** (the variadic callee reads the integer register: `42|0.000000|hello|-1.250000`), and **right when
     the double is passed as its 64-bit pattern in an integer parameter** (`2.500000|-1.250000|7|0.125000` on all
     three Windows runtimes).
   - On SysV x86-64 the CLR **does not set AL**. glibc's `sprintf` starts with `testb %al,%al; je` and skips saving
     XMM0-7 when AL is 0. Measured AL at the native entry of a fixed-arity P/Invoke: arbitrary per process, runtime,
     tier and call site (in the steady state mostly the low byte of a 16-byte-aligned address that changes with
     ASLR: 0x00, 0x10, 0x20, 0x30, 0x40, 0x70, ...; during warm-up 0x08). Result over six processes per
     configuration: `sprintf` with doubles printed `0.000000` for **2,000 of 2,000** calls in one .NET 8 process
     (run 6), another call site printed zeros in one .NET 10 TC=0 process (run 1), a probe call site read 0.0 in
     1,200 of 1,200 calls in three of 24 processes, and with TC=0 the first 8 of 2,000 `sprintf` calls were wrong in
     all 12 TC=0 processes. **Fixed-arity calls with floating-point variadic arguments cannot be used on SysV.**
     Integer and pointer variadic arguments are passed exactly like fixed ones on both ABIs.
   - The **v-variant route works everywhere**: a native `va_list` built over a buffer of 8-byte slots (Win64: the
     `char*` itself; SysV: `{gp_offset=48, fp_offset=176, overflow_arg_area=buffer, reg_save_area=0}`) given to
     `vsprintf` (glibc, msvcrt) and `__stdio_common_vsprintf` (ucrtbase) prints exactly the native result with 18
     mixed arguments on all five runtime configurations.
3. **Variadics defined in C:** LLVM's `ExpandVariadics` pass (`llvm/lib/Transforms/IPO/ExpandVariadics.cpp`, 1,169
   lines) does almost all of the work once it has an MSIL ABI: definitions become functions of their fixed
   parameters plus one `va_list` parameter, calls store the variadic arguments into a frame buffer and pass a
   `va_list` built over it, indirect calls of variadic type are expanded the same way, `va_copy`/`va_end` are
   expanded. A **prototype** (the pass copied into a private tool with ~150 lines of MSIL ABIs: SysV, Win64,
   AAPCS64; linked against the shared build's static libraries in 28 s, no tree rebuild) put in front of the
   shared `llc`/`msil-ld` ran a 13-case variadic program (`va.c`: c-testsuite 00140's `f1`, ints, doubles,
   `long long`, `size_t`, promoted `char`/`float`, 4-, 3-, 16-, 16- and 32-byte structs by value, `va_copy`,
   `va_list*`, two `va_start`s, recursion, zero variadic arguments, a function pointer to a variadic function, our
   variadic forwarding to native `vprintf`/`vsnprintf`, `printf` through a pointer, `snprintf` with ten doubles,
   `fprintf`), a variadic definition called from another object, the same from another *assembly* (`-shared`),
   c-testsuite 00140, and the fixed-arity natives (`open`, `fcntl` with two arities, `ioctl`, `syslog` via
   `vsyslog`, UCRT `_open`, msvcrt `sprintf` with doubles as integer bits). **Every output equals the native
   build's**, at `-O0` and `-O2`, on both x86_64 triples, on Framework 4.8, .NET 8 and .NET 10 (Windows) and
   .NET 8/10 with and without TC=0 (WSL). The c-testsuite tests that need `printf` were run through the same
   prototype: section 8.4.
4. **Recommended design** (section 6): Design.md 4.7's model, implemented with `ExpandVariadics` in `Lowering` mode
   behind a small hook (MSIL ABI classes keyed on `Triple::isOSBinFormatMSIL()`), run in `MSILPassConfig::addIRPasses`
   after `LowerInvoke`; native-layout `va_list` everywhere (no conversion at any boundary); the variadic buffer is a
   static alloca of the caller (no shadow stack, so no dependency on P3-A); calls of undefined variadic functions
   stay *expanded* in objects and **msil-ld** (not the target) binds them: to an MSIL definition directly, to a
   native function through a **per-C-runtime v-variant table** (glibc 44 pairs, msvcrt 64; UCRT needs none); a
   per-triple **fixed-arity list** in the backend (glibc's `open`/`fcntl`/`ioctl`/`syscall`/`execl`/..., UCRT's 20
   exported variadics) plus the `msil_fixed_arity` attribute turns calls of listed natives into per-call-site
   fixed-arity P/Invokes, with doubles as integer bits on Windows and a compile-time error for floating-point
   variadic arguments on SysV. The CLR `vararg` convention is not used on any OS.
5. **What it unblocks:** c-testsuite 00140 (the one `diag` entry of `libc-free.txt`) and the 68 tests that call
   `printf` (a second list compared with `<name>.c.expected`): through the prototype 69 of 70 left-out tests match
   on linux-gnu at `-O2` (00204 differs only where `long double` goes to glibc's `vprintf`) and every portable one
   matches its native build on windows-msvc once a non-inline `printf` exists for code that declares it without
   `<stdio.h>` (6 tests; UCRT exports none: decision V10); matrix row `variadic-def-xtu` (becomes `transcript:
   42`); Rust `std` on Linux without the fixed-arity shims of HANDOFF issue 15 (`open64`, `fcntl`, `syscall` are all
   on the fixed-arity list); the `printf` family on both triples, which every real C program of P3-B's "glibc and
   UCRT host-libc conformance" (zlib, Lua, SQLite) uses.

## 1. Today (Phase 2 as built)

### 1.1 What each case reports

Measured with the shared build (`x1/run.ps1`, sanitized log `x1/x1-diagnostics.txt`). "obj" is `clang -c`, "LTO" is
`clang -flto` with msil-ld writing the PE, "emit-asm" is `msil-ld --lto-emit-asm` (linked text).

| Case | linux-gnu obj | windows-msvc obj | LTO (both) |
|---|---|---|---|
| `printf("%d %f\n", 42, 2.5)` | `call_printf.c:2:5: error: calls to variadic functions are not supported for MSIL yet` | `<ucrt>\stdio.h:950:37: error: variadic function definitions are not supported for MSIL yet` (UCRT's inline `printf`) | linux: `msil-ld: error: <unknown>:0:0: in function main i32 (): calls to variadic functions are not supported for MSIL yet`; windows: `... in function printf i32 (ptr, ...): variadic function definitions ...` |
| `snprintf` | as `printf` | `stdio.h:1919:37` (inline `snprintf`) | as `printf` |
| `open`/`fcntl`/`ioctl`/`syslog` | 4 x `calls to variadic functions ...` | `_open`: 1 x `calls to variadic functions ...` | same texts from msil-ld |
| variadic definition, same TU | `def_same.c:2:5: error: variadic function definitions are not supported for MSIL yet` | same | `msil-ld: error: <unknown>:0:0: in function sum i32 (i32, ...): variadic function definitions ...` |
| definition in a.c, call in b.c (matrix `variadic-def-xtu`) | a.c: definition error; b.c: `calls to variadic functions ...` | same | definition error |
| `int my_vlog(const char *f, va_list ap) { return vprintf(f, ap); }` | compiles, links (vprintf binds to `libc.so.6`) | compiles, links (UCRT's inline `vprintf` -> `__stdio_common_vfprintf`) | compiles, links |
| `int (*pf)(const char*, ...) = printf; pf("hi %d\n", 1)` | call error plus `error: undefined variadic function 'printf' is not supported for MSIL yet (phase 3)` | definition error plus call error | call or definition error |
| `--lto-emit-asm` of a native variadic call or address | `msil-ld: error: native function 'printf' (libc.so.6) is variadic; calls of native variadic functions are not supported for MSIL yet (phase 3)` (pinned by `lld/test/MSIL/native-variadic.test`, also `(msvcrt.dll)`) | | |
| IR `va_arg` of a scalar | `the va_arg instruction is not supported for MSIL yet (phase 3 variadics)` (ISel) | same | |
| IR `va_arg` of a struct/array | same text from `MSILPrepareIR`, function body dropped (M12) | same | |

Where the texts come from (all verified in this checkout):

- `llvm/lib/Target/MSIL/MSILPrepareIR.cpp:1075-1079` `diagnoseDefinition`: `variadic function definitions are not
  supported for MSIL yet`; `:1063` `VAArgUnsupported`; `:1253-1262` the struct/array `VAArgInst` (M12, plan row
  2026-10-21 'D19 note'; `SelectionDAGBuilder::visitVAArg` crashes with 'Unknown type!' on aggregates, natively too).
  `:2105` notes that UCRT's `-O0` `<stdio.h>` defines `sprintf` and helpers in every module, which
  `dropUnreferencedDiscardableFunctions` drops when unreferenced.
- `llvm/lib/Target/MSIL/MSILISelLoweringCall.cpp:98-113` `matchesSignature` (a variadic `F` never matches),
  `:138-139` `LowerFormalArguments` backstop, `:402-422` `LowerCall`: linked mode gives 5.10.14's native text,
  otherwise `calls to variadic functions are not supported for MSIL yet`.
- `llvm/lib/Target/MSIL/MSILISelLowering.cpp:322-341`: `VAEND` Expand, `VACOPY` Custom (cpblk of 24 bytes SysV, 32
  bytes AAPCS64; `getNativeVAListStructSize` at `:45`) or Expand (`char*`), `VAARG` Custom -> `LowerUnsupported`
  (`:563`, `:649-650`, `:721-726`). This is M10 fix-6 (plan section 12 rows of 2026-10-15) and its test
  `llvm/test/CodeGen/MSIL/va-intrinsics.ll`.
- `llvm/lib/Target/MSIL/MSILAsmPrinter.cpp:1376-1380` (linked text, native variadic), `:1391-1396` (`undefined
  variadic function '<f>' is not supported for MSIL yet (phase 3)`), `:2126` (`getABIDescriptor`: "VARARG is never
  set (decision D19)"), `:2655-2662` (object mode, address-only reference, plan row 2026-10-25 M19).
- `llvm/lib/Target/MSIL/MSILMachineFunctionInfo.cpp:114-117` `computeSignatureVTs`: no parameter for the variable
  part.
- `lld/MSIL/Signatures.cpp:100-131` `checkNativeReferences` (the 5.10.14 variadic text), `:158-163` `reportMismatch`
  (`variadic functions are not supported yet: <sym>`), `llvm/lib/CLI/CallAdapter.cpp:123-125` `classifyCall`
  (VARARG on either side fails).
- The record already reserves what P3-B needs: `llvm/include/llvm/BinaryFormat/MSILLink.h:199-214` `CallFlags`
  (`VarArg = 0x2`, "Phase 2 writers never set VarArg"), `SignatureInfo::FixedParams` (`:313-322`; written only with
  VARARG, `llvm/lib/CLI/LinkInfo.cpp:604-605`, `:1029-1030`), `ToolchainABIVersion = 1` (`:51-53`, "phase 3's
  variadic buffer"), and libraries' `cli::LibraryFunctionFlags::Variadic = 0x1` (`llvm/include/llvm/CLI/SymbolTable.h:84`,
  set by `lld/MSIL/Exports.cpp:278-285` from the definition's VARARG, read by `lld/MSIL/AssemblyFiles.cpp:581-595`).

Tests that pin today's texts and change with P3-B: `llvm/test/CodeGen/MSIL/{varargs-call-error,va-intrinsics,
va-arg-aggregate,call-errors,add-prototypes,imports-linked,object-errors,unsupported-ir-errors}.ll`,
`lld/test/MSIL/{native-variadic,libcall,obj-pinvoke,obj-signatures}.test`, `msil/test/M2/08-semantics/
{variadic-def-xtu.test,matrix.txt:98}`, `msil/test/Programs/c-testsuite/libc-free.txt:146`,
`msil/test/harness/ctest-lists.test` with `Inputs/ctest-lists.py`.

### 1.2 What clang gives the backend

- **SysV x86-64** (`def_same.lnx.O2.ll`): `va_list` is `[1 x %struct.__va_list_tag]`, `%struct.__va_list_tag = {
  i32, i32, ptr, ptr }`; clang lowers `va_arg` inline: `icmp ult i32 %gp_offset, 41` -> `vaarg.in_reg` (read
  `reg_save_area + gp_offset`, add 8) or `vaarg.in_mem` (read `overflow_arg_area`, add 8). With `gp_offset = 48`
  and `fp_offset = 176` every read takes the overflow path.
- **Win64**: `va_list` is `ptr`; clang's `emitVoidPtrVAArg` reads 8-byte slots, indirectly for a type that is not
  1, 2, 4 or 8 bytes.
- **UCRT `printf`** (`x1/ucrt-printf.O2.ll`): `define linkonce_odr i32 @printf(ptr %_Format, ...) comdat` with
  `llvm.va_start`, a load of the `char*`, and `call i32 @__stdio_common_vfprintf(i64 %opts, ptr %stream, ptr
  %_Format, ptr null, ptr %va)`. At `-O0` clang also emits `sprintf`, `_snprintf`, `vsprintf`, `_vsnprintf`,
  `_vsprintf_l`, `_vsnprintf_l` in every module.
- **glibc `printf`**: `declare noundef i32 @printf(ptr noundef readonly captures(none), ...)`.
- **K&R calls** are non-variadic on every MSIL triple (fork hook in `clang/lib/CodeGen/CGCall.cpp:713-715`, plan
  row 2026-10-15 M9 fix-2), so a variadic call type in MSIL clang's IR is always a real variadic call. Stock-clang
  x86_64 SysV bitcode still types K&R calls as variadic (`isNoProtoCallVariadic`); direct ones are fixed by
  `MSILFixFunctionBitcasts` (`makeKnRCallsNonVariadic`, `MSILFixFunctionBitcasts.cpp:715-730`), indirect ones are
  ambiguous (section 6.7, risk).

## 2. Calls into native variadic functions: CLR measurements

Program `VaProbe.cs` (C# 5, compiled with the Framework `csc`, references only `mscorlib`, so one image runs on all
five configurations, like the toolchain's output), native probe `alprobe.s` (built with the tree's clang and
`ld.lld -shared`), logs `clr-windows.txt`, `clr-linux-run1.txt` ... `clr-linux-run6.txt`. Windows uses msvcrt.dll's
`sprintf`/`vsprintf` (real variadic exports) and ucrtbase's `__stdio_common_vsprintf`; Linux uses `libc.so.6`.
Expected `42|2.500000|hello|-1.250000` (`%d|%f|%s|%f`) and `2.500000|-1.250000|7|0.125000` (`%f|%f|%d|%f`).

### 2.1 The CLR vararg convention (`__arglist`, `pinvokeimpl ... vararg`)

| | Fx 4.8 | .NET 8 Win | .NET 10 Win | .NET 8 Linux | .NET 10 Linux |
|---|---|---|---|---|---|
| A1 `sprintf(b, "%d\|%f\|%s\|%f", __arglist(42, 2.5, s, -1.25))` | right | right | right | `InvalidProgramException: Vararg calling convention not supported.` | same |
| A2 doubles first | right | right | right | same exception | same |

Usable on Windows only; the toolchain must produce the same IL on every OS, so it is not used (Design.md 3.1's
"keep native vararg as a fallback" is withdrawn, section 10).

### 2.2 Fixed-arity P/Invoke of a variadic function

| | Fx 4.8 | .NET 8 Win | .NET 10 Win | Linux (.NET 8/10, default and TC=0) |
|---|---|---|---|---|
| B1 `sprintf(b, f, int 42, double 2.5, ptr, double -1.25)`: the first double is argument 4 | `42\|0.000000\|hello\|-1.250000` | same | same | right in 23 of 24 processes; `42\|0.000000\|hello\|0.000000` in .NET 10 TC=0 run 1 |
| B2 doubles in arguments 3 and 4 | `0.000000\|0.000000\|7\|0.125000` | same | same | as B4 |
| B3 Windows: the doubles as `long` (`BitConverter.DoubleToInt64Bits`) | right | right | right | n/a |
| B4 Linux: B2 x2000 | | | | wrong 2000/2000 (.NET 8, run 6); wrong 8/2000 in every TC=0 process (12 of 12); 0 otherwise |
| B5 Linux: `sprintf(b, "%f", 2.5)` x2000 | | | | 0 wrong in all runs |

Why: the Microsoft x64 convention passes a variadic floating-point value in **both** XMMn and the integer register,
and variadic callees spill RCX/RDX/R8/R9 to their home area and read from there; a P/Invoke with a `double` parameter
fills only XMMn. Passing the bits as `int64` puts them where the callee reads, and B3 shows it on every Windows
runtime. Arguments 5 and later are on the stack and identical either way (B1's last double is right).

SysV x86-64 passes variadic arguments exactly like fixed ones, plus AL = an upper bound on the vector registers used.
glibc 2.43's `sprintf` (`libc.so.6` of the E7 sysroot, `llvm-objdump` at 0x6a7e0):

```
6a7ef: movq %rdx,-0xa0(%rbp) ... movq %r9,-0x88(%rbp)
6a80b: testb %al,%al
6a80d: je 0x6a82f            ; skips the eight movaps %xmmN saves
```

AL probe (`alprobe.s`: `al_probe: movzbl %al,%eax; ret`, `al_probe_z` at a 256-byte-aligned address, `va_probe_z`/
`va_probe_nz` that return XMM0 only if AL != 0, like a gcc prologue):

Steady-state AL at `al_probe` (C1, 1,200 calls of four signatures; on .NET 8 the first four calls saw 0x08, and
`al_probe_z`'s first call 0x00) and, in parentheses, how many of 1,200 calls of the `va_probe` call site (C4) read
0.0:

| Run | .NET 8 | .NET 8 TC=0 | .NET 10 | .NET 10 TC=0 |
|---|---|---|---|---|
| 1 | 0x40 (1) | 0xb8 (1) | 0x10 (0) | 0x90 (**1200**; B1 printed `42\|0.000000\|hello\|0.000000`) |
| 2 | 0x30 (1) | 0x48 (1) | 0x40 (0) | 0xa0 (0) |
| 3 | 0x70 (1) | 0xd8 (1) | 0x10 (0) | 0x20 (0) |
| 4 | 0x10 (1) | 0x68 (1) | 0x10 (0) | 0x20 (0) |
| 5 | 0x20 (1) | 0x88 (**1200**) | 0x10 (0) | 0x50 (0) |
| 6 | **0x00** (**1200**; B4 `sprintf` wrong 2000/2000) | 0x18 (1) | 0x0c (0) | 0x40 (0) |

(The first probe build, with `al_probe` at 0x1248, read 0x48, 0x68, 0xe0 and 0x0a.) AL is whatever the JIT left in
RAX: per call site, per tier, and in the steady state often the low byte of an aligned address that ASLR moves
between processes, so AL = 0 for a given call site in roughly one process in sixteen. There
is no IL construct that sets AL (`calli unmanaged cdecl` is the same JIT path). **Conclusion:** on SysV a fixed-arity
call is correct only when no floating-point value is among the variadic arguments; for floating-point arguments the
only correct route is a v-variant. Integer and pointer variadic arguments are safe whatever AL holds: modern gcc and
clang prologues only test AL for zero (the old computed-jump prologue of gcc before 4.6 would misbehave with AL > 8;
not seen in glibc 2.43).

AAPCS64 (Linux) passes variadic arguments like fixed ones and its callees save all of x0-x7 and q0-q7, so fixed-arity
calls are correct there; Windows ARM64 passes variadic floating-point values in integer registers (the Win64 rule
again); Apple ARM64 passes every variadic argument on the stack, where no fixed-arity P/Invoke can put it. None of the
AArch64 statements was measured here (no ARM64 runtime on this host): **unverified**.

### 2.3 The v-variant route: a native `va_list` over an argument buffer

`BuildVaList` in `VaProbe.cs`: the arguments in 8-byte slots (ints widened, doubles as bits, pointers); Win64 passes
the buffer address as the `va_list`; SysV allocates the 24-byte tag `{u32 gp_offset = 48, u32 fp_offset = 176,
void *overflow_arg_area = buffer, void *reg_save_area = NULL}` and passes its address. The P/Invoke signature is
`(native int, native int, native int)` on both, because a SysV `va_list` parameter decays to a pointer to the tag
and the Win64 one is a `char*`.

| | Fx 4.8 | .NET 8 Win | .NET 10 Win | .NET 8 Linux | .NET 10 Linux |
|---|---|---|---|---|---|
| D1 `vsprintf(b, "%d\|%f\|%s\|%f", va)` | right | right | right | right | right |
| D2 18 arguments (ints, `long long` 2^40, a string, 11 doubles: past every register) | right | right | right | right | right |
| D3 Windows: ucrtbase `__stdio_common_vsprintf(0, b, 512, f, NULL, va)` | right | right | right | | |
| D3 Linux: a tag with `gp_offset = 16`, `fp_offset = 48` and a filled `reg_save_area` (a native `va_start`'s state) | | | | right | right |

(and the same with TieredCompilation=0, `clr-windows.txt`, `clr-linux-run*.txt`.)

### 2.4 Which native variadic functions have a v-variant

`scan.py` lists the functions that a C library's headers declare with `...` and its binary exports, and finds the
exported v-variant (`glibc-variadics.txt`, `ucrt-variadics.txt`, `msvcrt-variadics.txt`):

- **glibc 2.43** (the hosted sysroot's headers, `libc.so.6`): 71 exported variadic functions, 44 with a v-variant:
  `printf`, `fprintf`, `sprintf`, `snprintf`, `dprintf`, `asprintf`, `scanf`, `fscanf`, `sscanf`, the wide ones
  (`wprintf`, `fwprintf`, `swprintf`, `wscanf`, `fwscanf`, `swscanf`), `__isoc99_*scanf` and `__isoc23_*scanf` (the
  names `<stdio.h>` redirects `scanf` to under C99 and C23), the `_FORTIFY_SOURCE` entries (`__printf_chk`,
  `__fprintf_chk`, `__sprintf_chk`, `__snprintf_chk`, `__dprintf_chk`, `__asprintf_chk`, `__obstack_printf_chk`,
  `__syslog_chk`, the wide `_chk`), `syslog`, `err`, `errx`, `warn`, `warnx`, `obstack_printf`. Without one (27):
  `open`, `open64`, `openat`, `openat64`, `fcntl`, `fcntl64`, `ioctl`, `prctl`, `syscall`, `execl`, `execle`,
  `execlp`, `ulimit`, `sem_open`, `mq_open`, `semctl`, `ptrace`, `mremap`, `error`, `error_at_line`, `argp_error`,
  `argp_failure`, `strfmon`, `strfmon_l`, `__asprintf`, and the callback-taking `clone` and `makecontext`.
- **UCRT** (10.0.26100 headers, `ucrtbase.dll` exports): only **20** exported variadic functions, none with a
  v-variant, all taking integers and pointers only: `_open`, `_sopen`, `_wopen`, `_wsopen`, `_execl`, `_execle`,
  `_execlp`, `_execlpe`, `_wexecl*`, `_spawnl`, `_spawnle`, `_spawnlp`, `_spawnlpe`, `_wspawnl*`. Everything else
  variadic in the UCRT (the printf and scanf families, `_cprintf`, ...) is defined inline in the headers over
  `__stdio_common_v*`/`__conio_common_v*`, which take a `va_list`.
- **msvcrt.dll** (windows-gnu): 126 exported variadic functions, 64 with a v-variant (`printf`->`vprintf`,
  `sprintf`->`vsprintf`, `_snprintf`->`_vsnprintf`, `fprintf`->`vfprintf`, ...). The 62 without include the whole
  scanf family (msvcrt has no `vscanf`/`vsscanf`), which takes pointers only, and `_CrtDbgReport`.

## 3. Variadic functions defined in C and called from C

### 3.1 ExpandVariadics, as it is in this checkout

`llvm/lib/Transforms/IPO/ExpandVariadics.cpp` (1,169 lines; 31 commits since 2024-01, mostly API churn):

- `class VariadicABIInfo` (`:96-143`): `enableForTarget`, `vaListPassedInSSARegister`, `vaListType`,
  `vaListParameterType`, `initializeVaList(M, Ctx, Builder, AllocaInst *VaList, Value *Buffer)`, `slotInfo(DL, Type)`
  -> `{Align DataAlign, bool Indirect}`, `ignoreFunction(F)` (`:135`), `vaEndIsNop`/`vaCopyIsMemcpy` (always true).
  `VariadicABIInfo::create(const Triple &)` (`:1126`) switches on the architecture only (AMDGPU, wasm32 (testing
  only), NVPTX, SPIR-V): an MSIL triple (arch x86_64/aarch64) gets no ABI and the pass does nothing.
- `ExpandVariadics(Mode)`: `Lowering` rewrites the ABI. `runOnFunction` splits each variadic function into a
  fixed-arity replacement `F.valist(fixed..., va_list)` holding the body and a one-block wrapper; in `Lowering` mode
  the replacement takes the name, linkage, visibility and comdat (`ExternallyAccessible`), every direct call is
  rewritten by `expandCall`, and the wrapper is RAUW'd away (so `@fp = global ptr @sum` points at the new function).
  Declarations are rewritten as well (`expansionApplicableToFunction`, `:252`, returns true for them in `Lowering`).
  `runOnModule` (`:354`) then rewrites `va_start` in non-variadic functions into a store of the incoming `va_list`
  (`vaListPassedInSSARegister`) or a `va_copy` from it (struct `va_list`), `va_copy` into a memcpy of
  `vaListType`'s size and `va_end` into nothing, and in `Lowering` mode every indirect call of variadic type
  (`:415`) and every `va_arg` instruction (`expandVAArgInst`, `:935`).
- `expandCall` (`:642`): per variadic argument `slotInfo` decides the alignment and whether the value goes by
  pointer to a caller copy (`Indirect`); `byval` arguments are copied by value; the frame is a packed struct
  `<caller>.vararg` allocated **in the caller's entry block** with alignment `max(MaxFieldAlign, DL stack
  alignment)` (`:762-764`), and the `va_list` (for non-SSA ABIs an alloca of `vaListType`) is initialized by
  `initializeVaList`. `musttail` and `invoke` callers are `report_fatal_error("Cannot lower callbase instruction")`
  (`:653`) in `Lowering` mode.
- `expandVAArgInst` assumes a pointer cursor (`load ptr` from the `va_list`), which is right for `char*` ABIs and
  **wrong for SysV and AAPCS64**, whose `va_list` starts with two `i32` offsets.

### 3.2 The MSIL ABIs (prototype: `msil-expand-variadics.diff`)

| Triple | `vaListType` | in SSA? | `initializeVaList` | `slotInfo` |
|---|---|---|---|---|
| x86_64 linux-gnu/musl (SysV) | `[1 x {i32, i32, ptr, ptr}]` (24 bytes) | no; parameter `ptr` | store 48 at +0, 176 at +4, buffer at +8, null at +16 | align 8, 16 if the type's ABI alignment > 8; never indirect (clang already coerces aggregates of up to 16 bytes into eightbyte scalars and passes larger ones `byval`, which the pass copies by value) |
| aarch64 linux-gnu (AAPCS64) | `{ptr __stack, ptr __gr_top, ptr __vr_top, i32 __gr_offs, i32 __vr_offs}` (32 bytes) | no; `ptr` | buffer at +0, null at +8 and +16, 0 at +24 and +28 (offsets >= 0 mean "use the stack") | align 8 or 16; composites above 16 bytes already arrive as pointers (unverified: no ARM64 runtime) |
| x86_64 windows-msvc/-gnu (Win64) | `ptr` | yes | the buffer itself | align 8; indirect unless the size is 1, 2, 4 or 8 |
| aarch64 windows | `ptr` | yes | the buffer | align 8; indirect above 16 bytes (unverified) |
| aarch64 darwin (phase 4) | `ptr` | yes | the buffer | align 8; indirect above 16 bytes (unverified) |

All MSIL ABIs return `ignoreFunction(F) = F->hasFnAttribute("no-prototype") || F->hasFnAttribute("msil-fixed-arity")`:
prototype-less declarations keep their variadic type and their calls are made non-variadic by
`MSILFixFunctionBitcasts`, and the fixed-arity natives are handled before the pass (section 4).

Buffer layout, byte for byte, for `mixed("iuzldcfpsPFqoi", ...)` on SysV: each scalar in its own 8-byte slot (the
upper half of an `int` slot is not written, as natively), `struct small {short; char}` (coerced to `i32`) in one
slot, `struct pt {double, double}` (coerced to two `double`s) in two, `pair {i64, i64}` (`byval` here, because
clang's native classification found the six GPRs used up; coerced or `byval`, the bytes are the same) in two, `odd3`
(3 bytes, coerced to `i24`) in one, `struct foo` (32 bytes, `byval`) memcpy'd into four; the caller's `va_list` tag
points `overflow_arg_area` at slot 0. The IR the prototype makes for `sum(3, 10, 20, 12)`:

```llvm
%main.vararg = type <{ i32, [4 x i8], i32, [4 x i8], i32 }>
  %vararg_buffer = alloca %main.vararg, align 16          ; entry block
  %va_argument = alloca [1 x { i32, i32, ptr, ptr }], align 8
  store i32 10, ptr %vararg_buffer ...                     ; slots 0, 8, 16
  store i32 48, ptr %va_argument                           ; gp_offset
  store i32 176, ptr (%va_argument + 4)                    ; fp_offset
  store ptr %vararg_buffer, ptr (%va_argument + 8)         ; overflow_arg_area
  store ptr null, ptr (%va_argument + 16)                  ; reg_save_area
  %call = call i32 @sum(i32 3, ptr %va_argument)
define i32 @sum(i32 noundef %n, ptr %varargs)              ; va_start(ap) -> memcpy(ap, %varargs, 24)
```

and the IL `llc -O2 -msil-object-form` makes of it (`xtu_sum.lnx.il`, `xtu_main.lnx.il`): `sum` is `.method public
static int32 'sum'(int32, native int)`, opens with `ldc.i4.s 40 localloc` (clang's 16-aligned 24-byte `ap` plus
D13's 8 extra bytes), aligns the frame to 16, `cpblk`s the 24-byte tag (the `va_copy`), and loops over clang's two-way `va_arg` (`ldc.i4.s 40 bgt.un.s` to the
overflow path); the caller writes `ldc.i4.s 10 stind.i4` etc. into an 88-byte `localloc` frame and calls `int32
[msil.extern]'<msil>Symbols'::'sum'(int32, native int)`. On Win64 the caller passes the buffer address directly
(`ldloc.0 call int32 'sum'(int32, native int)`), and stack colouring makes all six variadic calls of `xtu_main`
share one 56-byte frame.

### 3.3 The prototype and what it ran

The tool (`msil-expand-variadics.cpp` = `ExpandVariadics.cpp` minus the pass registration, plus the MSIL ABIs, a
fixed-arity pre-step and a v-variant rename that stands in for msil-ld's link-time binding; diff in
`msil-expand-variadics.diff`, build script `proto-build.ps1`: clang-cl with the build's flags `-MD -UNDEBUG /EHs-c- /GR-
-std:c++17`, linked against `LLVMIRReader LLVMAsmParser LLVMBitReader LLVMCore LLVMRemarks LLVMBitstreamReader
LLVMBinaryFormat LLVMTargetParser LLVMSupport LLVMDemangle` of `D:\llvm-msil\build\lib`; 26 s compile + 2 s link, no
tree rebuild) runs between `clang -S -emit-llvm` and the shared `llc -filetype=obj`, then the shared clang driver
links with msil-ld (`proto-pipe.ps1`; native references with the tree's clang for `x86_64-pc-windows-msvc` and the
E7 oracle's `ld.lld` + `start.o` for Linux; `proto-runwsl.sh` runs the Linux images).

| Program | What it covers | Result (both triples, -O0 and -O2) |
|---|---|---|
| `va.c` | 00140's `f1`; `sum` of 0/3/10 ints; `avg` of 3/10 doubles; `mixed` of 14 kinds incl. 4-, 3-, 16-, 16- and 32-byte structs, `size_t`, `unsigned long` (32 bits on LLP64), promoted `char` and `float`; `va_list` function, forwarder, `va_copy`, `va_list*`, two `va_start`s, recursion, no variadic arguments, function pointer to a variadic definition; our variadic `my_printf` -> native `vprintf`, `my_snprintf` -> native `vsnprintf`; `printf`, `snprintf` with 10 doubles, `printf` through a pointer, `fprintf(stdout, ...)` | equal to native: Fx 4.8, .NET 8, .NET 10 (Windows); .NET 8, .NET 8 TC=0, .NET 10, .NET 10 TC=0 (WSL) |
| `xtu_sum.c` + `xtu_main.c` | definitions in one object (`sum`, `dsum` with doubles), calls and a function pointer in another (matrix `variadic-def-xtu`) | equal to native, same configurations |
| same, `xtu_sum.c` as `-shared` `vlib.dll` | exported variadic functions, called directly and through `fp = sum` from a program linked against the library | right on Fx 4.8 and .NET 8 (Windows) and .NET 8 (WSL) |
| `t00140.c` | c-testsuite 00140 unchanged | exit 0, same configurations |
| `natives.c` | linux: `open(path, flags, 0644)`, `fcntl(fd, F_GETFL)` and `fcntl(fd, F_DUPFD, 10)` (two arities of one native), `ioctl(FIONREAD)`, `syslog(LOG_DEBUG, "... %d %s %f", 42, "x", 2.5)` via `vsyslog`; windows: `_open(path, flags, mode)` | equal to native, same configurations |
| `winfp.c` | msvcrt.dll's real variadic `sprintf` (through `import_module`), fixed-arity, `"%f\|%f\|%d\|%f\|%f"` with the doubles as `i64` bits | `2.500000\|-1.250000\|7\|0.125000\|10000000000.000000` on Fx 4.8, .NET 8, .NET 10, -O0 and -O2 (objects) |
| `regs.c` | a SysV `va_list` in the state a native `va_start` leaves it (`gp_offset = 8`, `fp_offset = 48`, five ints and three doubles in `reg_save_area`, one int in the overflow area) read by our compiled `va_arg` | `regs 123456789` = native, all four WSL configurations |
| `emu.c` | the same lowering written by hand in C (no prototype tool), 4 cases incl. 12-, 24-, 16- and 1-byte structs and native `vsnprintf` with 14 arguments | equal to native through objects and LTO at -O0 and -O2 on all configurations, except Windows `-O0 -flto` (the Phase 2 gap of section 0) |

Cost (`bench.c`, 20 M calls of a `noinline` variadic `sum(4, i, 1, 2, 3)`, 1 M `snprintf("%d %f")`; one run each on a
loaded host, so +-30%):

| | variadic sum | `snprintf` |
|---|---|---|
| native Windows (clang -O2) | 167 ms | 745 ms |
| MSIL -O2 Fx 4.8 / .NET 8 / .NET 10 | 192 / 261 / 147 ms | 505 / 798 / 601 ms |
| native Linux | 372 ms | 1,578 ms |
| MSIL -O2 .NET 8 / .NET 10 (WSL) | 633 / 621 ms | 1,576 / 1,544 ms |

The SysV callee pays the 24-byte `va_list` copy and always fails clang's register test before reading the overflow
area (about 31 ns against 19 ns per call); `printf`-family calls run at native speed because the formatting is
native.

### 3.4 The `va_list` ABI: native layout, not WebAssembly's pointer

Design.md section 3 already decided "native `va_list` everywhere", and the measurements confirm it is the only
layout that needs no conversion in any direction:

- our variadic definitions read their arguments with clang's native `va_arg` lowering, which reads the buffer through
  the "registers exhausted" state (`va.c`, `xtu`);
- our `va_list` goes to a native `v*` function unchanged (`my_printf` -> `vprintf`, `emu.c` -> `vsnprintf`, UCRT's
  inline `printf` -> `__stdio_common_vfprintf`);
- a `va_list` made by native code (a native `va_start`, live `reg_save_area`) is read by our code (`regs.c`, and
  `VaProbe` D3 for glibc reading such a tag) - the case of a native library calling a C callback of type
  `void (*)(const char *fmt, va_list ap)` once P3-B's thunks exist.

WebAssembly's representation (the `va_list` is the buffer pointer, `ExpandVariadics`' `Wasm` ABI) would need a
conversion to a native tag before every native `v*` call and could not represent a native SysV `va_list` with a live
register save area at all. There is therefore one representation and no conversion helper.

### 3.5 The boundaries

- **A C variadic function called from native code** (its address passed to a native API that calls it with the
  native variadic convention, e.g. libxml2's `xmlGenericErrorFunc`): not supportable in general. The expanded method
  takes `(fixed..., va_list)`; a native-callable thunk has a fixed signature and cannot see the caller's variadic
  arguments. A possible later trick (not proposed for P3-B): a thunk with all six GPR and eight XMM parameters plus N
  stack-slot parameters that rebuilds a native tag (SysV) or an 8-byte-slot area (Win64), limited to N stack slots.
  P3-B should diagnose what it can see: a VARARG function passed to a parameter of a native function in 5.10.14's
  callback list (msil-ld) gets `variadic function '<f>' cannot be called from native code`; the thunk table entry of
  a VARARG function stays the native-callable thunk of its *expanded* signature, which is what MSIL callers in other
  assemblies call (`calli unmanaged cdecl (fixed..., native int)`, measured through `vlib.dll`'s `fp`), so
  cross-assembly function pointers keep working.
- **A C function taking `va_list` passed to native `vprintf`** and the reverse: no conversion (3.4).
- **Exported variadic functions** (section 5): the export method is `(fixed..., native int va_list)`, which a C#
  caller can call only by building a native `va_list` (like calling `vprintf`). A CLR-vararg overload is not
  portable (2.1). Friendlier overloads belong to the managed-calls goal (FeatureIdeas 3).

### 3.6 Edge cases the implementation must handle

1. **`musttail`** variadic forwarding (`musttail call i32 (i32, ...) @callee(i32 %a, ...)`) makes the pass
   `report_fatal_error` (measured: `LLVM ERROR: Cannot lower callbase instruction`). `LowerCall` reports `musttail
   calls are not supported for MSIL yet` (`MSILISelLoweringCall.cpp:336-337`), which runs too late: `MSILPrepareIR`
   must report `musttail` calls (at least of variadic type) and drop the body before the pass runs.
2. **`invoke`**: also fatal in `Lowering` mode; the pass must run after `createLowerInvokePass()`
   (`MSILTargetMachine.cpp:289`), which MSIL already runs.
3. **The IR `va_arg` instruction** (no clang output; other front ends and hand-written IR): on `char*` triples the
   pass's `expandVAArgInst` matches clang's Win64 lowering (8-byte slots, indirect above 8 bytes or for a size that
   is not a power of two) and can be accepted; on SysV and AAPCS64 it would miscompile, so `MSILPrepareIR` must
   report **every** `VAArgInst` there (today only struct and array ones; scalars reach ISel, which would now be after
   the pass) and drop the body. rustc lowers `va_arg` itself for x86_64 SysV, Win64 and AArch64 (its
   `rustc_codegen_llvm` `va_arg.rs`; not verified in this checkout), and Rust `std`
   defines no C-variadic functions (HANDOFF issue 15's analysis lists only `syscall`, `open64`, `fcntl` as variadic
   *declarations* on Linux, none on Windows).
4. **Prototype-less declarations** keep their variadic type (`ignoreFunction`).
5. **Stock-clang x86_64 SysV bitcode** types indirect K&R calls as variadic; the pass would expand them and a
   non-variadic callee would read garbage. Today such calls are errors; after P3-B they would be silent. Keep
   Phase 2's stock-bitcode warning and add the case to it (risk, section 11).
6. **Frame alignment:** `expandCall` aligns the buffer to the data layout's stack alignment (16, `S128`), so every
   function with a variadic call takes D13's realignment (`MaxAlign - 8` extra bytes and `add 15; and -16`). Harmless;
   an MSIL-only `MaxFieldAlign` would save 8 bytes and four instructions per such frame (optional hook detail).
7. **`-O0`:** the pass runs in codegen, so `-O0` (and UCRT's inline `sprintf` family that clang emits at `-O0`) is
   handled exactly like `-O2` (measured: all programs at `-O0`).
8. **COMDAT definitions** (UCRT's `printf` in every TU): the fixed-arity replacement keeps the comdat, linkage and
   name; every object defines the identical `printf(native int, native int)` (measured, `va.c` on Windows).
9. **`long double`** is `double` on MSIL triples: `printf("%Lf", x)` through glibc's `vprintf` reads a 16-byte x87
   value from a slot that holds 8 bytes of double. Natively correct, MSIL wrong, and invisible to the compiler (the
   format string decides). Same class as Status.md's `long double` limitation; a clang warning for `%L` in a
   format checked against a wide-`long double` MSIL triple would catch the literal cases (risk).

## 4. The `msil_fixed_arity` attribute and the v-variant table

### 4.1 What each is for, and where the decision is made

Three facts decide the split:

1. When `clang -c` compiles a call of an undefined variadic function, it cannot know whether the callee is a C
   definition in another object (common: `luaL_error`, CoreMark's `ee_printf`, any project's `log_printf`) or a
   native function. Only msil-ld knows.
2. A C definition in another object wants the *expanded* call (`(fixed..., va_list)`); a native function wants
   either a v-variant (which has exactly the expanded call's shape: `printf(fmt, va)` *is* `vprintf(fmt, va)`) or a
   fixed-arity call with the real argument types.
3. An expanded call cannot be turned back into a fixed-arity one without the argument types, which the object does
   not carry.

So: **expanded by default** (the C-definition case is direct and optimal, and the v-variant case is a rename in
msil-ld), and **fixed-arity by name at compile time** for the natives that have no v-variant, which are few, stable
and well known per C runtime:

- **v-variant table** (msil-ld, per C runtime of the link: glibc, musl, msvcrt; UCRT has none): maps a natively bound
  VARARG reference to the v-variant export of the native libraries (`printf` -> `vprintf`, `__isoc99_sscanf` ->
  `__isoc99_vsscanf`, `__printf_chk` -> `__vprintf_chk`, `syslog` -> `vsyslog`, ...; the 44 glibc and 64 msvcrt
  pairs of 2.4). The import keeps the symbol's name and gets the v-variant as its entry (5.10.11's `as "<entry>"`,
  already used for oldnames aliases), signature `(fixed..., native int)`. The lookup searches the **native** index
  only, so a program's own MSIL `vprintf` never captures `printf`'s binding.
- **Fixed-arity list** (backend, per triple's C runtime): glibc `open`, `open64`, `openat`, `openat64`, `fcntl`,
  `fcntl64`, `ioctl`, `prctl`, `syscall`, `execl`, `execle`, `execlp`, `ulimit`, `sem_open`, `mq_open`, `semctl`,
  `ptrace`, `mremap`, `error`, `error_at_line`, `argp_error`, `argp_failure`, `strfmon`, `strfmon_l`, `__asprintf`
  (`clone` and `makecontext` take callbacks: P3-B thunks decide them; `strfmon` exists to format doubles, so on SysV
  every real call of it is the floating-point error below, as is any `error`/`argp_*` call that passes a double); UCRT the 20 of 2.4; msvcrt its 62 without a
  v-variant. A call of such a *declaration* becomes the non-variadic call of all its arguments (the K&R mechanism:
  one MemberRef per call-site signature, one P/Invoke per IL signature in msil-ld, 5.10.11), with:
  - Windows triples: every `double` variadic argument bitcast to `i64` (B3, `winfp.c`);
  - SysV: a floating-point variadic argument is a compile-time error (2.2);
  - AAPCS64 Linux: unchanged (unverified).
- **`__attribute__((msil_fixed_arity))`**: the same treatment for a declaration the list does not name, typically a
  native library's variadic API without a v-variant (`curl_easy_setopt`, `sqlite3_config` from `libsqlite3.so`).
  A user adds it on a redeclaration (`int curl_easy_setopt(CURL *, CURLoption, ...)
  __attribute__((msil_fixed_arity));`) or in a forced-include header; the toolchain has no headers of its own (user
  decision), which is why Design.md's "declared in our headers" becomes "a built-in list plus the attribute".
  Clang sets the IR function attribute `"msil-fixed-arity"`, which the backend treats like a listed name.

### 4.2 Diagnostics when neither works

| Where | When | Text (proposal) |
|---|---|---|
| compile time (backend) | a fixed-arity (listed or attributed) native called with a floating-point variadic argument on SysV | `passing a floating-point value to native variadic function '<f>' is not supported for MSIL: it has no va_list variant, and .NET cannot set the vector-register count (AL) of the x86-64 System V variadic convention` |
| compile time | `musttail` call | `musttail calls are not supported for MSIL` (moved to `MSILPrepareIR`) |
| compile time | IR `va_arg` on SysV/AAPCS64 triples | `the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself` |
| msil-ld | VARARG reference bound natively, no v-variant in the table | `native function '<f>' (<module>) is variadic and has no va_list variant; declare it with __attribute__((msil_fixed_arity)) to call it with fixed arity` |
| msil-ld | the table names a v-variant that no native library of the link defines | `native function '<f>' (<module>) is variadic, and its va_list variant '<vf>' is not defined by the native libraries` |
| msil-ld | VARARG on one side only (MSIL definition and reference: C undefined behaviour) | 5.9.8's `function signature mismatch: <sym>` with `>>> defined as ... variadic` lines (replaces `variadic functions are not supported yet`) |
| msil-ld | a fixed-arity reference (list or attribute) bound to an MSIL VARARG definition | adapter that packs the arguments (types from the call's IL signature) and a warning, or an error in the first milestone (decision V6) |
| msil-ld | a VARARG function passed to a native callback parameter | `variadic function '<f>' cannot be called from native code` (with P3-B's thunks) |

## 5. Across objects and `-shared` libraries

- **Record.** The expanded definition's SIGNATURES entry and every reference to an expanded function set `VARARG`
  and `FixedParams` = the number of IL parameters before the `va_list` parameter (the `va_list` is IL parameter
  `FixedParams`; a hidden sret pointer counts). The backend knows a function was variadic through an attribute the
  pre-step adds before the pass (`"msil-variadic"`), which `deriveFixedArityReplacement`'s `copyAttributesFrom`
  keeps; `MSIL::getABIDescriptor` (`MSILAsmPrinter.cpp:2126`) reads it. Fixed-arity references set
  `NO_PROTOTYPE`-like behaviour; whether they get their own flag is decision V5.
- **Binding (msil-ld)**: VARARG reference + VARARG MSIL definition with equal `FixedParams` and IL signature ->
  direct (`classifyCall`, `llvm/lib/CLI/CallAdapter.cpp:123-125`, changes from "fail" to this rule); VARARG + native
  -> v-variant (4.1) or the error of 4.2; `checkNativeReferences` (`lld/MSIL/Signatures.cpp:126-131`) loses the
  blanket variadic error. Function addresses: a FnAddr reference to a native VARARG function binds to the
  v-variant's stub (measured through the rename: `pp = printf` on glibc), to the expanded method otherwise.
- **Libraries**: nothing new in the format. `lld/MSIL/Exports.cpp:278-285` already sets
  `LibraryFunctionFlags::Variadic` and the ABI descriptor from VARARG, `AssemblyFiles.cpp:581-595` reads them, and
  the cross-assembly call is an ordinary call of the export method with the expanded signature (measured: `vlib.dll`,
  direct and through `fp`). `cli::LibraryFunctionFlags::ABI`'s descriptor carries `FixedParams`.
- **ABI version**: `ToolchainABIVersion` (`MSILLink.h:51-53`) was reserved for "phase 3's variadic buffer", but no
  v1 object or library can contain a VARARG entry (writers never set it, `LinkInfo.cpp` writes FixedParams only with
  it), so the convention is fully identified by VARARG + FixedParams + the triple. Bumping would only make every
  Phase 2 library unusable with P3 programs (decision V4).
- **LTO**: the pass runs in `MSILPassConfig::addIRPasses`, which msil-ld's LTO codegen also runs (today's LTO errors
  come from the same `MSILPrepareIR`), so LTO needs no extra work for definitions and calls. In linked text
  (`--lto-emit-asm`), msil-ld's import analysis (`lld/MSIL/LTO.cpp`, before codegen) sees the still-variadic
  declaration and records the v-variant entry, which `MSILAsmPrinter`'s linked-mode import emission
  (`:1376-1380`, today the variadic error) then uses. LTO also makes cross-TU calls of C variadic definitions
  same-module calls.

## 6. Recommended design

### 6.1 Compile time (backend)

1. **Hook in `llvm/lib/Transforms/IPO/ExpandVariadics.cpp`**: in `VariadicABIInfo::create`, `if
   (T.isOSBinFormatMSIL())` return the MSIL ABI of section 3.2 (three small classes, ~150 lines including
   `ignoreFunction`). New `hook` entry in `msil/utils/upstream-hooks.txt` with `lit llvm/test/Transforms/
   ExpandVariadics/*.ll` and `lit llvm/test/CodeGen/WebAssembly/expand-variadic-call.ll` as the upstream
   coverage. Rebuild: one object (`ExpandVariadics.cpp` in `LLVMipo`) plus relinks.
2. **`MSILPrepareIR`**: stop reporting variadic definitions (`:1075-1079`); report every `VAArgInst` on struct
   `va_list` triples and every `musttail` call, dropping the body (3.6); otherwise unchanged.
3. **New `MSILVariadics.cpp`** (or a function in `MSILPrepareIR`): the fixed-arity list per triple and the pre-step
   (calls of listed or `"msil-fixed-arity"` declarations become per-call-site non-variadic calls; Win64 `double` ->
   `i64` bits; SysV floating-point argument -> 4.2's error), and the `"msil-variadic"` marking of every function the
   pass will expand.
4. **`MSILPassConfig::addIRPasses`** (`MSILTargetMachine.cpp:268-297`): `addPass(createExpandVariadicsPass(
   ExpandVariadicsMode::Lowering))` after `createLowerInvokePass()`/`createUnreachableBlockEliminationPass()` and
   before `createIndirectBrExpandPass`. Order with the MSIL passes: `MSILPrepareIR` (diagnose, mark),
   `MSILAddMissingPrototypes`, `MSILFixFunctionBitcasts` (K&R calls non-variadic), `MSILOptimizeReturned`,
   `LowerInvoke`, ExpandVariadics.
5. **Clean-ups after the pass**: no IR function is variadic any more except prototype-less and fixed-arity
   declarations, so `LowerCall`'s `IsVarArg` rejection (`MSILISelLoweringCall.cpp:402-422`), `LowerFormalArguments`'
   backstop (`:138-139`), `matchesSignature`'s early return (`:101`), `MSILAsmPrinter`'s three variadic errors
   (`:1376`, `:1394`, `:2660`) and `computeSignatureVTs`' comment (`MSILMachineFunctionInfo.cpp:114`) become
   backstops that exempt those two kinds (and may be reworded); `VAARG`'s ISel lowering stays for the `char*`
   triples' `va_arg` that the pass did not expand (none) and as a backstop.
6. **`getABIDescriptor`**: VARARG + FixedParams for `"msil-variadic"` functions; the fixed-arity references' flag
   (decision V5).
7. **Clang**: the `msil_fixed_arity` attribute (`Attr.td` record, `AttrDocs.td`, `SemaDeclAttr.cpp`: only on a
   variadic function declaration of an MSIL triple, `CodeGenModule.cpp`: the IR attribute). A new attribute record
   rebuilds 519-523 clang objects (~3,500 CPU-s, plan 5.6.10's measurement), so batch it with any other P3 clang
   attribute or defer it (decision V3).

### 6.2 Link time (msil-ld)

1. The v-variant table (`lld/MSIL/NativeLibraries.{h,cpp}`, next to `isNativeCallbackFunction`), keyed by the C
   runtime that the link's triple and native libraries identify (glibc and musl names on linux, msvcrt on
   windows-gnu, none on windows-msvc).
2. Binding rules of section 5 in `Signatures.cpp` (`checkNativeReferences`, `bindCall`, `reportMismatch`) and
   `llvm/lib/CLI/CallAdapter.cpp` (`classifyCall`), the texts of 4.2, and the v-variant entry in LTO's import
   analysis (`LTO.cpp`) for linked text.
3. Optional (decision V6): the packing adapter for a fixed-arity reference bound to an MSIL VARARG definition, built
   with `cli::ILBuilder` like 5.9.8's adapters: `localloc` a buffer, store each variadic argument per the triple's
   slot rule, build the `va_list` (SysV: a 24-byte tag), call the definition.

### 6.3 What this gives each case of the task

| Case | linux-gnu | windows-msvc |
|---|---|---|
| `printf`, `snprintf` | expanded call; msil-ld binds `printf` as `vprintf` (`snprintf` -> `vsnprintf`) | UCRT's inline definitions compile (expanded) and pass their `va_list` to `__stdio_common_v*` |
| `open`, `ioctl`, `fcntl` | fixed-arity list; per-call-site P/Invoke | `_open`: fixed-arity list |
| `syslog` | expanded; `vsyslog` | n/a |
| variadic definition, same object | expanded definition and call, direct | same |
| definition in another object | expanded on both sides, VARARG = VARARG, direct call | same |
| exported from a library | VARARG export, direct cross-assembly call | same |
| `va_list` passed to native `vprintf` | unchanged (native layout) | unchanged |
| function pointer to `printf` | expanded indirect calls; the address is `vprintf`'s stub | the address of the inline `printf` |
| `printf` declared without `<stdio.h>` | as `printf` | the helper archive's definition (V10) |

## 7. Decisions

**V1. Where variadic lowering happens.** Options: (a) `ExpandVariadics` (IR, `Lowering` mode) with MSIL ABIs;
(b) WebAssembly-style in SelectionDAG (`WebAssemblyISelLowering.cpp:1459-1531`: `LowerCall` stores the operands after
`NumFixedArgs` into a stack object and passes its address; `:1687-1693`: `LowerFormalArguments` adds the buffer
pointer as a virtual register; `:2219` `LowerVASTART` stores it; for MSIL `LowerVASTART` would build the native tag); (c) an MSIL-only copy of ExpandVariadics
(`MSILExpandVariadics.cpp`, D30 style). **Recommend (a).** Evidence: the prototype (a) ran every program of 3.3 with
~150 lines of ABI code and no change to the pass logic; it runs in codegen, so `-O0`, objects and LTO alike get it;
it handles `byval`, aggregates, indirect calls, `va_copy` and comdats; (b) would re-implement all of that after type
legalization, and the MSIL ISel today has no variadic code to start from; (c) duplicates 1,169 lines that changed 31
times since 2024. Needs user approval only as an upstream hook (one file, additions only).

**V2. Native variadic calls: v-variant, fixed-arity, or CLR vararg.** Options: (a) v-variants where they exist,
fixed-arity for the rest (FP as integer bits on Windows; FP an error on SysV), no CLR vararg; (b) CLR vararg on
Windows plus (a) on Linux (Design.md 3.1's fallback); (c) fixed-arity for everything (as the Rust experiment's
stand-ins did). **Recommend (a).** Evidence: 2.1 (vararg throws on Linux), 2.2 (fixed-arity FP wrong on SysV for a whole
call site in 3 of 24 processes and for the first 8 calls in all 12 TC=0 processes; on Win64 wrong without the
integer-bits rule, right with it), 2.3 (v-variants right everywhere).

**V3. How a program names a fixed-arity native that the built-in list lacks.** Options: (a) the clang attribute
`msil_fixed_arity` (IR attribute, survives into bitcode and LTO; one Attr.td record = ~8 minutes of clang rebuild);
(b) a backend option `-mllvm -msil-fixed-arity=<names>` (no clang change; must also reach LTO through
`-Wl,-mllvm,...`); (c) none: the built-in list only, msil-ld's error otherwise. **Recommend (a), batched with any
other P3 clang attribute change; (c) until then.** Needs the user (scope: a new user-facing attribute).

**V4. ToolchainABIVersion.** Options: (a) keep 1: VARARG + FixedParams identify the convention, no v1 file has
VARARG; (b) bump to 2 as plan 5.8.9 anticipated. **Recommend (a)**; amend 5.8.8's VARARG text (section 12 row).
Evidence: section 5; a bump makes every Phase 2 library input fail the ABI check for no incompatibility.

**V5. The record flag of a fixed-arity reference.** Options: (a) reuse `NO_PROTOTYPE` (no format change; msil-ld
cannot tell K&R from fixed-arity in its texts); (b) a new CallFlags bit `FIXED_ARITY 0x10` (record v1.1 minor, 5.8.9
allows flag bits; better texts, and the packing adapter of V6 can be limited to it). **Recommend (b).**

**V6. A fixed-arity reference bound to an MSIL variadic definition** (a project that defines its own `open`, or the
attribute on a function that turns out to be C). Options: (a) msil-ld packing adapter plus a warning; (b) an error
naming both files. **Recommend (b) in the P3-B milestone, (a) later if a program needs it.** It matters for the
portable triple, whose LLVM libc defines `open` and friends in C: that triple has an empty fixed-arity list, so it
never reaches this case.

**V7. Where the v-variant table lives.** Options: (a) msil-ld (link time, per C runtime); (b) the target (compile
time, Design.md 4.7's "a table in the target"). **Recommend (a).** Evidence: 4.1; with (b) a program that defines
its own `printf` in another object (embedded-style code) would have its calls silently bound to native `vprintf`.

**V8. IR `va_arg` on SysV/AAPCS64.** Options: (a) report it (clang never emits it; rustc lowers it itself); (b)
lower it in MSIL IR like clang's `X86_64ABIInfo::EmitVAArg`. **Recommend (a)** now; (b) when a front end needs it.

**V9. C variadic functions called from native code.** Options: (a) diagnose where visible (callback list), document
otherwise; (b) a capture thunk with N stack slots (3.5). **Recommend (a)** for P3-B.

**V10. Non-inline printf-family definitions on windows-msvc** (code that declares `printf` itself, objects or
bitcode compiled without UCRT's inline definitions; 6 of the 68 c-testsuite `printf` tests). Options: (a) a small C
file in the toolchain's sources (like compiler-rt) that defines the C standard printf/scanf names over the
documented `__stdio_common_v*` entry points and `__acrt_iob_func`, compiled into P3-A's static helper archive, from
which msil-ld takes a member only when the name is otherwise undefined; (b) compile UCRT's own header with
`_CRT_STDIO_INLINE` defined empty (measured: works, 179 functions) at toolchain build or link time, which needs the
Windows SDK headers there and redistributes code derived from them; (c) an error with a hint to include
`<stdio.h>`. **Recommend (a)**; it reuses whatever P3-A decides for the helper archive (inputs file section 4, "how
the static C helper archive reaches msil-ld"). The option bits are the header's: `0` for `printf`-like calls,
`_CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR` (2) for `snprintf`/`vsnprintf`. Needs the user only if the
licensing of (b) is considered.

## 8. Tests

### 8.1 New (`llvm/test/CodeGen/MSIL/`, FileCheck, both triples, -O0 and -O2, `%if ilasm` round trips)

- `variadic-def.ll`: expanded signature `int32 'f'(int32, native int)`, the `cpblk 24` (SysV) or the plain store
  (Win64), `va_copy`/`va_end`, two `va_start`s; the SIGNATURES `VARARG fixed=<n>` line in `--link-info` dumps.
- `variadic-call.ll`: buffer layout per triple (slot offsets of i32, i64, double, ptr, i128 (16-aligned on SysV,
  indirect on Win64), `byval` 32-byte struct, Win64 3-byte struct indirect), the tag stores 48/176, indirect calls,
  zero variadic arguments, a call inside a loop (one buffer).
- `variadic-fixed-arity.ll`: listed names and `"msil-fixed-arity"`: per-call-site MemberRefs, Win64 `double` ->
  `int64`, the SysV floating-point error.
- `variadic-errors.ll`: `musttail`, IR `va_arg` on SysV/AAPCS64, body dropped, no `LLVM ERROR`.
- Updates: `varargs-call-error.ll`, `va-intrinsics.ll`, `va-arg-aggregate.ll`, `call-errors.ll`, `add-prototypes.ll`,
  `imports-linked.ll`, `object-errors.ll`, `unsupported-ir-errors.ll`.

### 8.2 msil-ld (`lld/test/MSIL/`)

- `native-variadic.test` rewritten: `printf` binds as `pinvokeimpl("libc.so.6" as "vprintf")`, `msvcrt.dll`'s too;
  a variadic native without v-variant gives 4.2's text; a missing v-variant; addresses.
- `obj-variadic.test`: VARARG definition and reference across objects, FixedParams mismatch, one-sided VARARG.
- `shared-variadic.test`: the symbol table's `VARARG` flag and ABI descriptor of an export, a program against it.
- Updates: `libcall.test`, `obj-pinvoke.test`, `obj-signatures.test`.

### 8.3 Execution (`msil/test`)

- `msil/test/M2/08-semantics/variadic-def-xtu.test` becomes a `%{msil_prog}` transcript (`sum(3, 10, 20, 12) = 42`)
  on obj and lto, both OSes; `matrix.txt:98` -> `transcript`.
- A variadic corpus (e.g. `msil/test/M3/variadics/`): `va.c`, `regs.c` (linux), `natives.c`, `winfp.c`-style
  msvcrt via `import_module` (windows), with `.expected` from update-expected.py on both OSes and `msil_also_run`
  folding .NET 10 and TC=0 (the AL hazard is a TC=0/warm-up effect, 2.2).
- c-testsuite: `libc-free.txt:146` (00140) becomes a plain entry; a second list (its own `# rule:` and `# count:`)
  for the tests that print, compared with `<name>.c.expected` instead of `exit0.expected`
  (`msil_ctest_format.py`, `msil/test/harness/ctest-lists.test`), and a `-Wsl` twin of the `ctest` leg (plan row
  2026-10-28 'Linux-triple coverage').

### 8.4 What the prototype says about c-testsuite

The 70 tests of c-testsuite-5c7275656d75 that `libc-free.txt` leaves out (63 call only `printf`; 5 call `printf`
and other functions, one of them, 00204, also defines and calls variadic functions; 00025 calls `strlen`, 00040
`calloc`), each built through the prototype at `-O0` and `-O2` for both triples (`ctest/ctest2.ps1`) and run on
.NET 8 against `<name>.c.expected` (`ctest/run-win.sh`, `ctest/run-lnx.sh`; results `ctest/run-win.txt`,
`ctest/run-lnx.txt`, `ctest/build-failures.txt`):

| | linux-gnu -O0 | linux-gnu -O2 | windows-msvc -O0 | windows-msvc -O2 |
|---|---|---|---|---|
| equal to `.c.expected` | 68 | 69 | 60 | 61 |
| equal to the native build, not to `.c.expected` (the expected files assume LP64 and GCC bit-field layout: 00212, 00216, 00218) | | | 2 | 3 |
| `long double` printed with `%Lf` through glibc's `vprintf` (00204: 20 of 174 lines are `nan`; the other 154, all of its struct and `va_arg` cases, match) | 1 | 1 | | |
| VLA (00207, P3-A's shadow stack; at -O2 the VLA is optimized away) | 1 | | 1 | |
| `int printf(const char *, ...);` declared by the test, no `<stdio.h>`: `undefined symbol: printf` (00210, 00211, 00213, 00215, 00217, 00218 at -O0; at -O2 00218's only call became `puts`) | | | 6 | 5 |
| does not compile for Windows natively either (00214: `typedef unsigned long size_t`) | | | 1 | 1 |

So every test whose source is portable runs like its native build on both triples, except where `long double`
reaches glibc, once code that declares `printf` itself gets a definition on windows-msvc (below). 00204 passes on Windows (`long double` is `double` there natively). 00025 and 00040 need no variadics
and pass too (plan section 12 row 2026-10-28 already noted that M15 binds them).

**The UCRT finding behind the 6 Windows link errors.** `ucrtbase.dll` exports no `printf` (2.4); clang's native
default for `x86_64-pc-windows-msvc` is the static `libucrt.lib`, which does define `printf` (`lld-link /verbose`:
`libucrt.lib(debug.obj) for printf`), and a native `/MD` link needs `legacy_stdio_definitions.lib`, which is machine
code that msil-ld cannot link. A test of the remedy: `ctest/ucrt_stdio_defs.c` (`#define _CRT_STDIO_INLINE` then
`#include <stdio.h>`, which makes every inline definition of the UCRT header an external one: 179 functions) compiled
through the prototype into `libucrt_stdio_defs.a` and added to the six links (`ctest/helper.ps1`): 00210, 00211,
00213, 00215 and 00217 then match `.c.expected` and 00218 matches the native build, at `-O0` and `-O2`, on
Framework 4.8, .NET 8 and .NET 10. Decision V10 chooses how the toolchain provides such definitions.

## 9. Effect on the later goals

- **Portable triple `msil64-unknown-dotnet`:** needs its own `VariadicABIInfo` (any layout works because every
  variadic function is ours: the simplest is the `char*` one) and an empty fixed-arity list and v-variant table;
  LLVM libc's `printf`/`open` are C definitions that the expanded convention calls directly. Nothing here precludes
  it; choosing the `char*` ABI for it makes its `va_list` 8 bytes.
- **Managed calls from C:** a C-declared .NET method is never variadic in C; a .NET `params` method is a managed
  call, not a C variadic one. No interaction, except that the attribute machinery of V3 is the same kind of clang
  change (batch them if the timing allows).
- **Inline IL:** no interaction.
- **FeatureIdeas** (typed or delegate overloads of exports, FeatureIdeas 3): an exported variadic function's method
  takes a native `va_list`; a C#-friendly overload is that idea's job.
- **Final cleanup:** the texts proposed in 4.2 contain no plan or milestone names.

## 10. Design.md and plan updates this proposes

- Design.md 3.1, last bullet: drop "keep native vararg as a fallback for functions without a v-variant" (2.1); say
  that Windows triples call native variadic functions with fixed arity, floating-point values as integer bits.
- Design.md 4.7: the buffer is a static alloca in the caller's frame (no shadow stack); "a table in the target"
  becomes "msil-ld's table, per C runtime" (V7); "declared in our headers with `msil_fixed_arity`" becomes "a
  per-triple built-in list plus the `msil_fixed_arity` attribute" (no toolchain headers); on SysV a fixed-arity
  call cannot pass floating-point values (AL); UCRT needs no table, but code that declares `printf` without
  `<stdio.h>` needs non-inline definitions from the helper archive (V10).
- Plan 5.8.8: VARARG's meaning and FixedParams' definition (5), FIXED_ARITY (V5); 5.8.9: ABI version unchanged (V4);
  5.9.8's VARARG row; 5.10.14's variadic text; Status.md section 3's two variadic rows.

## 11. Risks

- **AL on SysV:** any route that ends in a fixed-arity call with a floating-point variadic value is silently wrong in
  some processes (2.2); the compile-time error must cover the list and the attribute, and msil-ld must never
  "fall back" to fixed arity for an expanded call.
- **Stock-clang SysV bitcode with indirect K&R calls** becomes silently wrong instead of an error (3.6 (5)).
- **`%Lf` through glibc** (3.6 (9)).
- **Native code calling a C variadic function** through a pointer it was given (3.5): wrong arguments, undiagnosed
  unless the callee is a known callback API.
- **The address of a fixed-arity native used as a variadic function pointer** (`int (*op)(const char *, int, ...) =
  open;`): indirect calls through it are expanded and would pass a `va_list` where `open` reads its mode. msil-ld
  should reject a FnAddr reference to a fixed-arity variadic native (or bind it to a synthesized expanded-to-fixed
  adapter); not measured.
- **Upstream merges** of `ExpandVariadics.cpp` (31 commits since 2024): the hook is additions at the end of the
  file, so conflicts are unlikely; the upstream-hook lit leg covers the pass's own tests.
- **AArch64 statements are unverified** (no ARM64 runtime here); the Linux AAPCS64 ABI class in the prototype was
  compiled but never run.
- **Host load:** the benchmark numbers are single runs on a loaded host.

## 12. Experiments index (all under `C:\llvm-msil-scratch\p3\variadics\`)

| Experiment | Files (`variadics-files/`) | Runtimes |
|---|---|---|
| Today's diagnostics | `x1/*.c`, `x1/run.ps1`, `x1/x1-diagnostics.txt`, `x1/ucrt-printf.O2.ll` | compile and link only |
| CLR vararg, fixed-arity, AL, v-variants | `VaProbe.cs`, `alprobe.s`, `clr-runwsl.sh`, `clr-windows.txt`, `clr-linux-run1..6.txt` | Fx 4.8, .NET 8/10 Windows (+TC=0), .NET 8/10 WSL (+TC=0), six Linux runs |
| Hand-written lowering through today's toolchain | `emu.c` | objects and LTO, -O0/-O2, all configurations |
| ExpandVariadics prototype | `msil-expand-variadics.diff`, `proto-build.ps1`, `proto-pipe.ps1`, `proto-runwsl.sh`, `va.c`, `regs.c`, `xtu_sum.c`, `xtu_main.c`, `natives.c`, `winfp.c`, `bench.c` | all configurations |
| Variadic tables | `scan.py`, `glibc-variadics.txt`, `ucrt-variadics.txt`, `msvcrt-variadics.txt` | n/a |
| c-testsuite's 70 left-out tests through the prototype; the UCRT helper archive | `ctest/ctest2.ps1`, `ctest/run-win.sh`, `ctest/run-lnx.sh`, `ctest/run-win.txt`, `ctest/run-lnx.txt`, `ctest/build-failures.txt`, `ctest/ucrt_stdio_defs.c`, `ctest/helper.ps1`, `ctest/excluded.txt` | .NET 8 (both OSes); the helper relinks on Fx 4.8, .NET 8, .NET 10 |
| Summary of the prototype and emulation runs | `proto-results.txt` | |

Installs: none. Private build: the prototype tool only (one TU against the shared build's static libraries, 28 s).
