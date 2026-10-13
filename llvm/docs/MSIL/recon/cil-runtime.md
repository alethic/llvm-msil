# Recon: CIL runtime behaviour (index of experiments and verified facts)

Status: reconstructed 2026-10-12 by the plan reviser from the cil-runtime recon's experiment sources and logs.
The original cil-runtime report was returned to the lead planner as chat text, because the harness refused
subagent `.md` files, so it never reached the repository. Every IL source, runner script and log that report relied
on is copied verbatim to [artifacts/cil-runtime/](artifacts/cil-runtime/). If the orchestrator still holds the
original report text, it may append that text below this index, but it must keep the index and the artifacts.

The facts below are restated from the logs in `artifacts/cil-runtime/` and nothing else. The runner was
`final_runs.sh`; `final_log_san.txt` is its log with user paths replaced by `$SCR`.

## Tools and runtimes the experiments used

| Tool | Path on this host |
|---|---|
| .NET Framework ilasm 4.8 ("fx-ilasm") | `C:/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe` |
| Modern ilasm 10.0.12 ("new-ilasm") | `D:/packages/NuGet/runtime.win-x64.microsoft.netcore.ilasm/10.0.12/runtimes/win-x64/native/ilasm.exe` |
| Runtimes | .NET 8.0.31, 9.0.20 and 10.0.12 (`dotnet <dll>` with `rc8.json` / `rc9.json` / `rc10.json` as `<name>.runtimeconfig.json`), and .NET Framework 4.8 (`ilasm -exe`, run directly) |
| WSL | Ubuntu with .NET 10.0.12 only (`linux_log.txt`) |
| ILVerify | `ilverify <dll> -r 'C:\Program Files\dotnet\shared\Microsoft.NETCore.App\10.0.12\*.dll'` (`ilv.sh`, summarized by `ilv.py` into `ilverify_summary.txt`) |
| jitall | `jitall.cs` (force-JIT every method body), built offline with `csc.rsp` (reference list of the Microsoft.NETCore.App.Ref 8.0.31 pack) |

Every experiment in `final_runs.sh` was assembled with both ilasm builds and run on .NET 8, 9, 10 and Framework 4.8.

## Experiment index

| Id | File | Question | Result |
|---|---|---|---|
| e01 | `e01_hello.il` | Global `<Module>` entry point in a DLL, mscorlib-only reference, int32 return value as the exit code | exit 42 on every runtime and with both ilasm builds |
| e01b | `e01b_bareref.il` | Bare `.assembly extern mscorlib {}` (the old backend's form) | works everywhere |
| e01c | `e01c_sysruntime.il` | Reference System.Runtime/System.Console instead of mscorlib | works on .NET 8/9/10; **fails on Framework 4.8** (FileNotFoundException System.Console 8.0.0.0), which is why D10 uses mscorlib |
| e02 | `e02_exit.il` | `Main(string[])`, exit code from args, `Environment.Version`, `GetCommandLineArgs()[0]` | exit 7 everywhere; `GetCommandLineArgs()[0]` is the DLL path (EXE path on Framework) |
| e03 | `e03_sigs.il` | Global vs class methods, scalar signatures, native int results | 9/9 everywhere; ILVerify: 2 x StackUnexpected (int32 where native int expected) |
| e04 | `e04_mem.il` | ldind/stind of every width, sign/zero extension, `unaligned.`/`volatile.`, localloc with and without localsinit, initblk/cpblk | 32/32 on .NET 8/9/10. **Framework 4.8: `.locals init` does not zero localloc memory** (31/32). Overlapping cpblk results differ between runtimes, so memmove never uses cpblk (D15) |
| e05 | `e05_conv.il` | All conversions, including `conv.r.un` and float-to-int out of range | **`conv.r.un` of uint64 double-rounds** on .NET 8, 9 and Framework (and on f64 on .NET 8 and Framework); correct on .NET 10. Out-of-range float-to-int: .NET 8 and Framework give x86 "indefinite" values (0x80000000...), .NET 9/10 saturate. Basis of D17 |
| e05b | `e05b_u64fp.il` | Correctly rounded uint64 to f64/f32 without `conv.r.un`: `if ((int64)v >= 0) r = (fp)v; else { t = (v >>u 1) \| (v & 1); r = (fp)t; r = r + r; }` | 6/6 on every runtime (hard cases 0x8000000000000401, 0x8000008000000001, 0x1000001000000001, 2^64-1). So `conv.r4`/`conv.r8` of a non-negative int64 round once on every runtime |
| e06 | `e06_cmp.il` | ceq/cgt/clt and `.un` forms, branches incl. `.un`, NaN semantics, `switch` | 43/43 everywhere |
| e07 | `e07_fnptr.il` | ldftn + managed calli, pointer tables in unmanaged memory, identity, stability across tier-up | 10/10 everywhere; ILVerify reports "ImportCalli not implemented" for every calli |
| e08 | `e08_pinvoke.il`, `e08b_noflush.il` | `pinvokeimpl` puts from ucrtbase/msvcrt/"ucrtbase", `as "entry"` renaming, stdout flush order | 4/4 everywhere |
| e08c | `e08c_modern.il` | DisableRuntimeMarshalling and SuppressGCTransition | work on .NET 8+ only |
| e08d | `e08d_negative.il` | Negative P/Invoke cases | a string parameter under DisableRuntimeMarshalling and a P/Invoke without `preservesig` both throw MarshalDirectiveException; `pinvokeimpl("libc")` on Windows throws DllNotFoundException |
| e08L | `e08L_linux.il` | P/Invoke on Linux: "libc" vs "libc.so.6" | `linux_log.txt`: both `pinvokeimpl("libc")` and `pinvokeimpl("libc.so.6" as "puts")` resolve on WSL .NET 10 (passes=2, exit 44). No libm P/Invoke was run. glibc ships the math functions in the separate `libm.so.6` (the `ldd` output in `linux_ilasm_log.txt` lists libm.so.6 and libc.so.6 separately), which is why plan D14 binds math imports to "libm.so.6" on linux-gnu |
| e09 | `e09_modinit.il` | `<Module>::.cctor` allocates the data segment with `Marshal.AllocHGlobal`, cpblk from `ldsflda` of an RVA field, relocations | 6/6 everywhere (basis of D12) |
| e09b | `e09b_rvapitfalls.il`, `e09b2_rvawrite.il` | The old backend's `&(label)` in `.data` and writes to RVA memory | rejected designs: RVA data is never written, `&(label)` never emitted (D12) |
| e09c | `e09c_lib.il`, `e09c_app.il` | Does `<Module>::.cctor` run before an exported method that touches no `<Module>` member? | consumer/library pair; relevant to phase 2 exports |
| e10 | `e10_main.il`, `e10L_main.il` | Generated `Main(string[])` building C argc/argv (NULL-terminated, unmanaged) and calling `int32 'main'(int32, native int)` | exit 43 with the expected argv on every runtime; native `exit(7)` through P/Invoke also works (basis of D27) |
| e11 | `e11_names.il`, `e11b_bom.il`, `e11c_utf16.il`, `e11d_octal.il` | Identifier quoting: dots, digits, `$ - @ ?`, spaces, non-ASCII, quotes/backslashes, keyword collisions | 23/23 everywhere; reflected names round-trip (basis of D11) |
| e12 | `e12_access.il`, `e12b_access.il`, `e12c_access.il` | Accessibility of private/compiler-controlled `<Module>` members used from a class | `private`/privatescope globals are **not** accessible from a class in the same module (MethodAccessException / FieldAccessException); `assembly` is. Basis of "never `private`" in plan 5.1 |
| e13 | `e13_i64ptr.il` | Mixing int64 and native int without conversions | the JIT accepts all seven cases silently; ILVerify and PEVerify flag them (StackUnexpected). Basis of D2 (never emit int64) |
| e14 | `e14_ilv.il` | Does ILVerify keep checking after an ignored error? | reports StackByRef and ImportCalli; see `ilverify_summary.txt` |
| e15 | `e15_broken.il` | Deliberately malformed bodies: what do jitall and ILVerify catch? | ILVerify catches all 12 kinds. jitall (.NET 8) catches .maxstack too small, underflow, extra value at ret, non-empty stack at localloc and falling off the end; it accepts join-depth/type mismatches. **.NET 10 JIT on a join with different stack depths (b3) grew past 1.2 GB in 1 s and was killed by the guard** (`jitall_e15.txt`). Basis of D20 and the timeout/memory caps in M0 |
| e16 | `e16_syntax.il` | ILAsm spellings: bit-exact FP constants, integer constants, `.data` forms, `tail.`, long branches, many locals | 22/22 everywhere |
| e17 | `e17_abort.il`, `e17b_void.il` | Exit codes of an unhandled exception, FailFast, native abort, void entry point | void entry point gives exit 0 |
| e18 | `e18_ldftn_pinvoke.il` | `ldftn` of a `pinvokeimpl` method + managed calli | 2/2 everywhere |
| e19 | `e19_memmove.il` | Overlapping copies: cpblk with a variable size vs `Buffer.MemoryCopy` | `Buffer::MemoryCopy(void*, void*, int64, int64)` gives memmove semantics on every runtime (D15) |
| e20 | `e20_align.il` | Alignment of localloc and AllocHGlobal results | OR of the low 6 bits is 48 for **variable-size** localloc on every runtime (16-byte aligned). The plan review later found constant-size localloc of 32 bytes or less only 8-byte aligned (`../artifacts/review-cil/align16.il`), see plan D13 |
| e21 | `e21_voidptr.il` | `void*`/`uint8*` in signatures interoperating with native int; `off` is an ILAsm keyword | 2/2 everywhere |
| e22 | `e22_arith.il` | Bitcasts through a local's address, shift-amount types, out-of-range shifts, division traps, `.locals` without init | not part of `final_runs.sh`; the source documents the cases, but no log was kept |

## ILVerify summary (`ilverify_summary.txt`)

- Allowed in Phase 1 (D21): `Unverifiable` ("Instruction cannot be verified"), `StackByRef` ("Expected ByRef on the
  stack", any ldind/stind through native int), `InitLocals`, `UnmanagedPointer`; "ImportCalli not implemented" is a
  note, not an error.
- Must stay fatal: StackUnexpected (type mismatch), PathStackDepth, PathStackUnexpected, StackOverflow/Underflow,
  ReturnEmpty, LocallocStackNotEmpty, MethodFallthrough (all seen in e15).
- ILVerify verifies each method only up to its first unverifiable instruction (e14).

## Hazards found

- **JIT hang on bad IL.** .NET 10 grows without bound on an inconsistent stack depth at a join (e15 b3). Every
  `dotnet` and jitall run must have a wall-clock timeout and a memory cap (plan M0.b/M0.c).
- **Silent acceptance.** The JIT accepts int64/native int mixing (e13) and several stack-type errors (e15), so
  execution alone does not prove IL correctness. Verification (D21) and a native oracle are needed.
- **Runtime-dependent conversions.** `conv.r.un` (e05) and out-of-range float-to-int differ between .NET 8, 9, 10 and
  Framework. FP gates run on .NET 8, .NET 10 and Framework.
- **Credentials.** The recon noticed plaintext feed credentials in `%APPDATA%/NuGet/NuGet.Config`. No agent may read or
  copy that file (plan section 2). The user has been told (plan section 10).


---

# Appendix: original cil-runtime recon report (restored from the planning workflow journal)

# MSIL recon: CLR toolchain and the exact ILAsm milestone 1 must emit (`cil-runtime`)

- Key: `cil-runtime`
- Date: 2026-10-12. Checkout: branch `msil-milestone1` @ `2829ef573b08`. Host: Windows 11 x64 and WSL Ubuntu 26.04 x64.
- Scope: Design.md sections 4, 5.3, 5.5, 7 and 12. Every construct milestone 1 needs was written by hand in ILAsm, assembled, and **run**. That is 31 experiment files, two ilasm builds, and five runtimes: .NET 8.0.31, 9.0.20 and 10.0.12 on Windows, .NET 10.0.12 on Linux, and .NET Framework 4.8.
- Scratch directory (`$SCR`): `C:\Users\JEROME~1\AppData\Local\Temp\claude\D--llvm-msil\fa776849-ce33-4cee-bcc7-05382d063d3e\scratchpad\recon\cil-runtime\`. All .il sources, harness scripts and raw logs are there; Appendix A lists them. The harness blocked writing this file into the repo from the subagent, so it is delivered as text.
- Nothing was downloaded, and no tracked repo file was modified.

Legend: **[V]** = verified by running it here. **[U]** = not verified.

---

## 0. Rules for the AsmPrinter (all [V] unless marked)

1. **Header.** Reference only `mscorlib`: `.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }`. The assembly then runs unchanged on .NET 8, 9 and 10 (Windows and Linux) and on Framework 4.8. Modern-only APIs need `System.Runtime` / `System.Runtime.InteropServices` with `.ver 8:0:0:0`. A `.ver 10:0:0:0` reference fails on .NET 8 with FileNotFoundException.
2. **Running.** `ilasm -dll` plus `prog.runtimeconfig.json` (section 3.2), then `dotnet prog.dll`. `.entrypoint` in a DLL works. The `int32` returned from the entrypoint is the exit code: 32 bits on Windows, the low 8 bits on Linux (300 → 44). `ilasm -exe` output runs both directly on Framework and via `dotnet prog.exe`.
3. **Put all C functions and globals in `<Module>`, with explicit access.** Use `assembly` for everything internal.
   - **Never `private`**: a private global is inaccessible from other types in the module (MethodAccessException / FieldAccessException from the export class).
   - **Never omit the keyword**: that gives `privatescope`, and ildasm prints `name$PST06xxxxxx`.
   - Exports are `public static` forwarders on one class.
4. **Quote every identifier** with single quotes, escaping `'` and `\` with a backslash.
   - `add`, `int32`, `value`, `method`, `native` and `off` collide with keywords (`off` as an unquoted parameter name is a syntax error).
   - Framework ilasm double-encodes non-ASCII identifiers: it reads the source as ANSI even with a UTF-8 BOM or as UTF-16. Modern ilasm stores correct UTF-8.
5. **Data segment** (Design 4.1) works exactly as designed: `<Module>::.cctor` does `Marshal.AllocHGlobal`, then `cpblk` from `ldsflda` of an RVA field (an explicit-size value type), `initblk` for bss, and `stind.i` relocations, and stores the base in a static `native int`.
   - RVA memory is **read-only**: writing to it is a fatal AccessViolationException on CoreCLR and Framework.
   - **Never** put `&(label)` in `.data`: CoreCLR then **refuses to load** the image.
6. **Pointers.** Use `native int`. RyuJIT accepts int64/native int mixing everywhere, but ilverify flags it, so emit `conv.i`/`conv.i8` at boundaries if ilverify output is going to matter (section 14).
7. **Loads and stores.** `ldind.u4` leaves an **int32**, so `ldind.u4; conv.i8` *sign*-extends; use `conv.u8` to zero-extend. ilasm accepts `ldind.u8` as an alias of `ldind.i8`. `unaligned.`/`volatile.` work in either order, and on `cpblk`/`initblk` too.
8. **Copies.** `cpblk` is not memmove: a constant-size overlapping copy produced garbage. Map `llvm.memmove` to `System.Buffer::MemoryCopy(void*,void*,int64,int64)` with args (src, dst, n, n), `llvm.memcpy` to `cpblk`, and `llvm.memset` to `initblk`.
9. **localloc.**
   - It needs only the size on the stack; anything else gives InvalidProgramException.
   - The result was 16-byte aligned in every run.
   - It is **not zeroed on Framework 4.8** even with `.locals init`, so never rely on zeroing.
10. **Conversions.** Do not use `conv.r.un` for **uint64 → f64/f32**: it double-rounds on .NET 8 and Framework (f64 and f32) and on .NET 9 (f32). Use the verified sticky-bit sequence (section 6). Out-of-range float→int results differ: x86 "indefinite" on 8/Framework, saturating on 9/10.
11. **Compares and branches.** The icmp/fcmp table (section 7) is verified, including NaN. `switch` falls through for any index ≥ N, negative ones included.
12. **Function pointers.** `ldftn` + `calli <managed sig>` works for module methods and for P/Invoke methods, also through int64. Pointer identity is stable across tier-up.
13. **P/Invoke.** Use `pinvokeimpl("<lib>" cdecl) <sig> cil managed preservesig {}`.
    - **`preservesig` is mandatory**; without it the call throws MarshalDirectiveException.
    - `"libc"` works on Linux and is DllNotFoundException on Windows, so the milestone-1 `puts` needs a per-triple library (`ucrtbase.dll` on MSVC).
    - CRT stdout is buffered when piped, flushed at exit, and reordered relative to System.Console, so the startup must not use Console.
14. **Startup.** A generated `'__msil_start'(string[])` builds argc and a NULL-terminated argv (AllocHGlobal + StringToHGlobalAnsi), then calls `int32 'main'(int32, native int)` and returns its value (section 11, verbatim).
15. **FP constants.** Print `ldc.r8 float64(0x...)` / `ldc.r4 float32(0x...)`, which is exact. The old writer's `ldc.r8 ( HEX )` is a bug (it parses as 2 bytes). Use `bytearray` for data; `.data char*("..")` is UTF-16.
16. **`.maxstack` is enforced by the JIT**: too small gives InvalidProgramException.
17. **Verification is weak for this IL** (section 13).
    - ilverify reports only the first error per method (usually `StackByRef` at the first pointer dereference) and cannot verify methods that contain `calli`.
    - The JIT accepts type errors silently.
    - **The .NET 10 JIT hangs and eats memory (40 GB seen) on an inconsistent stack depth at a join.**
    - Needed: a backend stack-type verifier plus a force-JIT lit step under time and memory limits.

---

## 1. Tool inventory

### 1.1 Present [V]

| Tool | Path | Version / notes |
|---|---|---|
| Framework ilasm | `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe` (and `Framework\`) | 4.8.9221.0 |
| **Modern ilasm (win-x64)** | `D:\packages\NuGet\runtime.win-x64.microsoft.netcore.ilasm\10.0.12\runtimes\win-x64\native\ilasm.exe` | 10.0.12. Already cached in the NuGet global packages folder, which is `D:\packages\NuGet` (set in `%APPDATA%\NuGet\NuGet.Config`), **not** `%USERPROFILE%\.nuget\packages`. Source recorded in .nupkg.metadata: api.nuget.org. |
| **Modern ildasm (win-x64)** | `D:\packages\NuGet\runtime.win-x64.microsoft.netcore.ildasm\10.0.12\runtimes\win-x64\native\ildasm.exe` | 10.0.12 |
| **Modern ilasm/ildasm (linux-x64)** | `D:\packages\NuGet\runtime.linux-x64.microsoft.netcore.il{asm,dasm}\10.0.12\runtimes\linux-x64\native\` | Runs in WSL after copying to a Linux dir and `chmod +x`. Needs libstdc++, libgcc_s and libc. |
| Framework ildasm, PEVerify, CorFlags | `C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\x64\` | ildasm 4.8.3928.0, PEVerify 4.0.30319.0 |
| ilverify | `%USERPROFILE%\.dotnet\tools\ilverify.exe` (global tool `dotnet-ilverify`) | 10.0.12-servicing.26422.108 |
| Microsoft.ILVerification (library) | `D:\packages\NuGet\microsoft.ilverification\8.0.0` | |
| Roslyn csc | `C:\Program Files\dotnet\sdk\10.0.401\Roslyn\bincore\csc.dll` | Used via `dotnet csc.dll -noconfig -nostdlib+ @rsp` with refs from `packs\Microsoft.NETCore.App.Ref\8.0.31\ref\net8.0`. No restore involved. |
| dotnet (Windows) | `C:\Program Files\dotnet` | SDKs 6.0.428, 8.0.425, 9.0.318, 10.0.401; runtimes 6.0.36, 8.0.31, 9.0.20, 10.0.12 |
| dotnet (WSL Ubuntu 26.04) | `/usr/lib/dotnet` | Ubuntu packages, SDK 10.0.112, runtime **10.0.12 only**. The `/usr/lib/x86_64-linux-gnu/libc.so` dev symlink is absent. |
| .NET Framework | 4.8 (CLR 4.0.30319.42000) | |

### 1.2 Packages for later approval

Nothing needs downloading on this machine. For CI and other hosts:

| Package | Version | Status |
|---|---|---|
| `runtime.win-x64.Microsoft.NETCore.ILAsm` / `.ILDAsm` | 10.0.12 | cached here |
| `runtime.linux-x64.Microsoft.NETCore.ILAsm` / `.ILDAsm` | 10.0.12 | cached here |
| `runtime.{linux-arm64,osx-arm64,win-arm64}.Microsoft.NETCore.ILAsm` | 10.0.12 | [U] |
| meta packages `Microsoft.NETCore.ILAsm` / `Microsoft.NETCore.ILDAsm` | 10.0.12 | [U] |
| `dotnet-ilverify` (tool) | 10.0.12 | installed here |
| `Microsoft.ILVerification` | 10.0.12 | [U]; 8.0.0 is cached |
| .NET 8 runtime for Linux (Ubuntu `dotnet-runtime-8.0` or dotnet-install `--runtime dotnet --version 8.0.x`) | 8.0.x | **not installed in WSL**, so .NET 8 on Linux is unverified |

### 1.3 ilasm flags [V]

- **Common to both**: /DLL /EXE /OUTPUT= /INCLUDE= /OPTIMIZE /FOLD /DEBUG /PDB /KEY /PE64 /X64 /ARM64 /32BITPREFERRED /NOLOGO /QUIET /SUBSYSTEM /FLAGS /ALIGNMENT /BASE /STACK /MDV /MSV /NOCORSTUB /STRIPRELOC /HIGHENTROPYVA /APPCONTAINER /CLOCK.
- **Modern only**: **/DET** (deterministic output) and **/ANAME=**.
- **Framework only**: /RESOURCE, /ENC and /ITANIUM.
- **/PE64 default**: Itanium on Framework, X64 on modern.
- **Default image**: PE32, Machine 0x14C, ILONLY ("AnyCPU", runs as 64-bit) for both. `-x64` gives PE32+ 0x8664. `-arm64` gives 0xAA64, which x64 rejects with FileLoadException (as Design 3 intends). `-32bitpreferred` is rejected by CoreCLR with BadImageFormatException.
- **`#include`** resolves relative to the **current directory**, so use `-include=<dir>`.

---

## 2. Harness (in $SCR)

- `run.sh <name> [flags]`: Framework ilasm `-dll`, then for v in 8 and 10, copy `rc$v.json` to `<name>.runtimeconfig.json` and run `dotnet <name>.dll`. `run2.sh` is the same with modern ilasm.
- `final_runs.sh`: both ilasms × net8/9/10, plus `ilasm -exe` on Framework. Its sanitized log is `final_log_san.txt`.
- `common.il`: the `#include`d checker (`'check.i8'`, `'check.r8'` bitwise, `'report'()` returning the fail count).
- `ilv.sh`/`ilv.py`: run ilverify with `-r "C:\Program Files\dotnet\shared\Microsoft.NETCore.App\10.0.12\*.dll"` and summarize. The default system module `mscorlib` works for mscorlib-referencing assemblies.
- `jitall.cs`: force-JIT every method, `<Module>` globals included, via `RuntimeHelpers.PrepareMethod`.
- Git Bash truncates exit codes; full 32-bit codes were taken from PowerShell `$LASTEXITCODE`.

---

## 3. Module skeleton, runtimeconfig, exit codes (task 2)

### 3.1 [V]

`e01_hello.il` assembled with Framework ilasm runs on .NET 8, 9 and 10 (Windows), .NET 10 (Linux), and Framework (as -exe), exiting 42:

```
.assembly extern mscorlib
{
  .publickeytoken = (B7 7A 5C 56 19 34 E0 89)
  .ver 4:0:0:0
}
.assembly e01_hello { .ver 0:0:0:0 }
.module e01_hello.dll

.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 8
  ldstr "hello from <Module>::main"
  call void [mscorlib]System.Console::WriteLine(string)
  ldc.i4.s 42
  ret
}
```

- CoreCLR's `mscorlib` facade type-forwards to `System.Private.CoreLib`.
- The old writer's bare `.assembly extern mscorlib {}` works everywhere (e01b).
- `System.Runtime`/`System.Console` 8.0 references (e01c) work on CoreCLR only.
- `.module` is optional, and the assembly name need not match the file name.

### 3.2 runtimeconfig.json [V]

```json
{
  "runtimeOptions": {
    "tfm": "net8.0",
    "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" }
  }
}
```

- **Missing file**: "The library 'hostpolicy.dll' required to execute the application was not found ... runtimeconfig.json was not found".
- **`tfm` is optional.**
- **Version binding**: `8.0.0` binds to the newest 8.0.x installed (8.0.31). `"rollForward":"LatestMajor"` picked 10.0.12.
- **Uninstalled version**: 11.0.0 gives "You must install or update .NET".
- **Driver**: should write this file next to the DLL, with version = the profile's major.

### 3.3 [V]

`ilasm -exe` output runs on Framework directly **and** via `dotnet x.exe` (with `x.runtimeconfig.json`). A single artifact can serve both.

### 3.4 [V]

`.ver 8:0:0:0` framework references work on 8, 9 and 10. `.ver 10:0:0:0` fails on 8 (FileNotFoundException for System.Runtime.InteropServices 10.0.0.0).

### 3.5 Exit codes [V]

| Path | Windows exit code |
|---|---|
| `return int32.Parse(args[0])` (e02) | -1, 256, 300 and -2147483648 pass through unchanged (net8 = net10) |
| unhandled exception | 0xE0434352 |
| `Environment.FailFast` | 0x80131623 |
| native `abort()` | 0xC0000409 |
| native `exit(7)` | 7 (stdio flushed) |
| `void` entrypoint | 0 |

- On Linux the exit code is truncated to 8 bits (300 → 44).
- `Environment.GetCommandLineArgs()[0]` is the `.dll` path under `dotnet x.dll`.

---

## 4. Methods, signatures, classes, accessibility (task 3) [V]

**Signatures and binding** (e03):
- Global methods with int32, int64, native int, float32, float64 and void signatures, and a mixed 5-parameter signature, all pass.
- An unqualified `call int32 'add_i32'(int32, int32)` inside a class binds to the **global** method, even when the class has a method of the same name.
- A quoted `'Quoted.Dotted.Cls'` splits into namespace `Quoted.Dotted` and name `Cls`.

The export class form that works:

```
.class public abstract sealed auto ansi Exports extends [mscorlib]System.Object
{ .method public static int32 'get_counter'() cil managed { ... } }
```

**Accessibility of global members** (e12, e12b, e12c; same on net8/9/10 and Framework):

| Global member accessibility | Used from the export class |
|---|---|
| `private` | **MethodAccessException / FieldAccessException** |
| none (privatescope) | works, but ildasm shows `$PST` |
| `assembly` | works |

- **PEVerify /md**: warns `0x8013129d` on every global member, and gives error `0x80131b21` for `.entrypoint` in a DLL. The latter is harmless for CoreCLR.
- **C# interop**: C# calls the export class directly (consumer.cs via csc prints `C#: counter=5 call_fp(5)=25 msg=seg-ok`). `native int` appears as `nint`.
- **Pointer types**: `void*`/`uint8*` parameters interoperate with native int values (e21); ilverify reports `UnmanagedPointer`.

---

## 5. Memory (task 3) [V]

e04_mem passes 32/32 on net8, 9 and 10 with both ilasms, and on Linux net10.

**Loads and stores**
- `stind.i1 0x180` then `ldind.i1` = -128 and `ldind.u1` = 128. The i2/u2 cases behave the same way.
- After `stind.i4 0x80000000`, both `ldind.i4; conv.i8` and **`ldind.u4; conv.i8` give 0xFFFFFFFF80000000**. `ldind.u4; conv.u8` gives 0x80000000.
- `stind.i8`/`ldind.i8`/`ldind.u8` work; memory is little-endian.
- `stind.i`/`ldind.i`, `stind.r4`/`ldind.r4` and `stind.r8`/`ldind.r8` work. `stind.r4` of a float64 0.1 stores 0x3DCCCCCD.

**Pointer arithmetic**
- native int + int32, native int − native int, and negative offsets all work.

**Prefixes**
- `unaligned. 1` on odd addresses for i4, i8 and r8, and `unaligned. 2` work.
- `volatile.` works, including `unaligned. 1 volatile.` and `volatile. unaligned. 1`.
- `unaligned. 1 cpblk` and `volatile. initblk` work.

**initblk / cpblk**
- `initblk(addr, val, size)`: the value is truncated to u8; the size may be int32 or native int.
- `cpblk(dest, src, size)` behaves as expected.

**localloc**
- Works with no `.locals`, and with `.locals init` (zeroed on CoreCLR; **not zeroed on Framework**).
- Results from localloc and AllocHGlobal were always 16-byte aligned (e20: Windows, Linux, Framework).
- localloc with another value on the stack gives InvalidProgramException (ilverify: `LocallocStackNotEmpty`).

**Stack sizes**
- The dotnet.exe main thread stack reserve is 1.5 MB; Linux `ulimit -s` is 8 MB.

### 5.1 Overlapping copies [V]

| Case | net8/9/10 | Framework 4.8 | memmove would give |
|---|---|---|---|
| `cpblk(p+1, p, 7)`, constant size | 0x0706040303020101 | 0x0606040403020101 | 0x0706050403020101 |
| `cpblk(p+1, p, n=1000)`, non-constant size | same as memmove | same as memmove | |
| `Buffer.MemoryCopy(p, p+1, 1000, 1000)` | same as memmove | same as memmove | |

### 5.2 RVA data [V]

- **Writes (e09b2):** writing through `ldsflda` of an RVA field gives a fatal AccessViolationException on CoreCLR (PE32 and PE32+) and on Framework. RVA data was page-aligned.
- **Address fixups (e09b):** `.data D_PTR = &(D_TARGET)` stops CoreCLR from loading the image, with both ilasms:
  - PE32: "The assembly architecture is not compatible with the current process architecture".
  - PE32+: "A dynamic link library (DLL) initialization routine failed. (0x8007045A)".
- **Framework is different:** it loads the image and applies the relocation, so the old writer's scheme only ever worked on Framework.

---

## 6. Conversions (task 3) [V]

| LLVM | IL | Result |
|---|---|---|
| sext_inreg / trunc i8, i16 | `conv.i1` / `conv.i2` | 0x1234F680 → -128 / sign-extended 0xF680 |
| zext i8, i16 | `conv.u1` / `conv.u2` | → 0x80 / 0xF680 |
| sext / zext i32→i64 | `conv.i8` / `conv.u8` | |
| trunc i64→i32 | `conv.i4` (or `conv.u4`; the stack type is int32 either way) | |
| int↔ptr | `conv.i` (sign-extend) / `conv.u` (zero-extend) | |
| sitofp | `conv.r8` / `conv.r4` | int64→f32 is **single-rounded** on every runtime (2^60+2^36+1 → 0x5D800001) |
| uitofp i32 | `conv.r.un` + `conv.r8`/`conv.r4` | correct |
| **uitofp i64 → f64** | not `conv.r.un` | 0x8000000000000401 gives 2^63 instead of 2^63+2^11 on **net8 and Framework**; correct on net9 and net10 |
| **uitofp i64 → f32** | not `conv.r.un; conv.r4` | double-rounds on **net8, net9 and Framework**; correct on net10 |
| fptosi | `conv.i4` / `conv.i8` | 2.9 → 2, -2.9 → -2 |
| fptoui | `conv.u4` / `conv.u8` | 3e9 and 1e19 → 0x8AC7230489E80000, correct on all |
| fpext / fptrunc | `conv.r8` / `conv.r4` | |
| bitcast | `ldarga`/`ldloca` + `ldind.<other type>` (e22), or `BitConverter.DoubleToInt64Bits`/`Int64BitsToDouble` | ilverify flags the ldind form; `SingleToInt32Bits` is .NET Core only [U] |

Correct uint64 → float sequence (e05b, 6/6 on net8/9/10 and Framework):

```
  ldarg.0  ldc.i8 0  blt.s NEG
  ldarg.0  conv.r8  ret
NEG:
  ldarg.0  ldc.i4.1  shr.un
  ldarg.0  ldc.i8 1  and  or
  conv.r8  dup  add  ret
// f32: conv.r4 ... dup add conv.r4
```

Out-of-range float→int (poison in LLVM):

| Input | net8 / Framework | net9 / net10 |
|---|---|---|
| `conv.i4` 3e9 | 0x80000000 | 0x7FFFFFFF |
| `conv.i4` NaN | 0x80000000 | 0 |
| `conv.i8` 1e19 | 0x8000000000000000 | 0x7FFFFFFFFFFFFFFF |
| `conv.u4` / `conv.u8` -1.0 | all ones | 0 |
| `conv.u1` 300.0 | 0x2C | 0x2C |

`llvm.fpto*i.sat` therefore needs explicit clamping (Expand).

Other arithmetic (e22):
- `div.un`, `rem.un` and float `rem` (fmod semantics) behave as expected.
- INT_MIN / -1 throws OverflowException; x / 0 throws DivideByZeroException.
- Shift counts are masked (int32 1 << 33 = 2).
- An int64 shift amount runs, but ilverify flags it, so make `getScalarShiftAmountTy` return i32.

---

## 7. Comparisons, branches, switch (task 3) [V]

e06 passes 43/43 everywhere. NaN was loaded as `ldc.r8 (00 00 00 00 00 00 F8 7F)`.

| Predicate | Value form | Branch form |
|---|---|---|
| eq | `ceq` | `beq` |
| ne / une | `ceq; ldc.i4.0; ceq` | `bne.un` |
| slt / ult, olt / ult(f) | `clt` / `clt.un` | `blt` / `blt.un` |
| sgt / ugt, ogt / ugt(f) | `cgt` / `cgt.un` | `bgt` / `bgt.un` |
| sle / ule | `cgt; ldc.i4.0; ceq` / `cgt.un; ldc.i4.0; ceq` | `ble` / `ble.un` |
| sge / uge | `clt; ldc.i4.0; ceq` / `clt.un; ldc.i4.0; ceq` | `bge` / `bge.un` |
| fcmp ole / ule | `cgt.un; ldc.i4.0; ceq` / `cgt; ldc.i4.0; ceq` | `ble` / `ble.un` |
| fcmp oge / uge | `clt.un; ldc.i4.0; ceq` / `clt; ldc.i4.0; ceq` | `bge` / `bge.un` |
| fcmp oeq | `ceq` | `beq` |
| ord / uno | `a a ceq; b b ceq; and` / then `ldc.i4.0; ceq` | |
| one / ueq | [derived, not run] `olt\|ogt` / `uno\|oeq` | |

- **Unsigned / unordered branches**: the `.un` branches are taken on NaN; the ordered ones are not.
- **Operand types**: int32, int64, native int and both float types all work.
- **`brtrue`**: taken on an int64 equal to 0x100000000.
- **switch**: `switch (L0, L1, L2, L3)` falls through for index 4 and for -1. Duplicate targets are allowed. The index must be int32.

---

## 8. Function pointers (task 3) [V]

e07 passes 10/10 and e18 passes 2/2.

- **calli shapes**: `ldftn int32 'sq'(int32)` … `calli int32(int32)` works, as do `int64(int64, float64, native int)`, `void()`, and recursive `calli` through a static field.
- **Tables**: `stind.i` of `ldftn` results into unmanaged tables works.
- **P/Invoke targets**: `ldftn` of a pinvokeimpl method + managed `calli` works.
- **Identity**: two `ldftn` of the same method are equal and different methods differ. After 400k calls and a sleep (tier-up) the value is unchanged and the old pointer still works.
- **Tail calls**: `tail. call` and `tail. calli` at depth 1e6 do not overflow.
- **Unmanaged calls**: `calli unmanaged cdecl native int(native int)` through `NativeLibrary.GetExport` works on net8/10.

---

## 9. P/Invoke (task 3) [V]

**Windows** declarations that run (e08):

```
.method public static pinvokeimpl("ucrtbase.dll" cdecl) int32 'puts'(native int s) cil managed preservesig {}
.method public static pinvokeimpl("msvcrt.dll" as "puts" cdecl) int32 'msvcrt_puts'(native int s) cil managed preservesig {}
.method public static pinvokeimpl("ucrtbase" as "puts" cdecl) int32 'puts_noext'(native int s) cil managed preservesig {}
.method public static pinvokeimpl("ucrtbase.dll" cdecl) native int 'strlen'(native int s) cil managed preservesig {}
.method public static pinvokeimpl("ucrtbase.dll" cdecl) float64 'sqrt'(float64 x) cil managed preservesig {}
```

**Linux** (e08L, .NET 10): `pinvokeimpl("libc" cdecl)` and `pinvokeimpl("libc.so.6" as "puts" cdecl)` both resolve, even without the libc.so dev symlink.

**Behaviour**
- **Strings from RVA data**: a C string can be passed directly as `ldsflda <rva field>; conv.u`.
- **`preservesig` is required**: without it the call throws MarshalDirectiveException.
- **"libc" on Windows**: DllNotFoundException.

**Modern-only attributes** (e08c, net8/9/10; on Framework the `System.Runtime 8.0.0.0` reference is not found):

```
.assembly e08c_modern
{ .custom instance void [System.Runtime]System.Runtime.CompilerServices.DisableRuntimeMarshallingAttribute::.ctor() = (01 00 00 00) }
.method public static pinvokeimpl("ucrtbase.dll" cdecl) int32 'abs'(int32 x) cil managed preservesig
{ .custom instance void [System.Runtime]System.Runtime.InteropServices.SuppressGCTransitionAttribute::.ctor() = (01 00 00 00) }
call void* [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeMemory::Alloc(native uint)
call native int [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeLibrary::Load(string)
```

- DisableRuntimeMarshalling really is active: a `string` parameter throws MarshalDirectiveException (e08d).
- Defining reference assemblies (net8 ref pack):
  - NativeMemory, NativeLibrary and UnmanagedCallersOnly are in System.Runtime.InteropServices.
  - DisableRuntimeMarshalling, SuppressGCTransition and ModuleInitializer are in System.Runtime.

**stdio** (e08, e08b), when stdout is a pipe or file:
- CRT `puts` output is buffered and appears after all Console output unless `fflush(NULL)` is called.
- It is flushed at normal exit on Windows (both CRTs) and on Linux.
- msvcrt has its own buffer, which `fflush` in ucrtbase does not touch.

---

## 10. Module initializer and data segment (task 3) [V]

e09 passes 6/6 on all runtimes, both ilasms, Framework and Linux:

```
.field static assembly native int '__msil_data_base'
.class private explicit ansi sealed '__msil_init_image_t' extends [mscorlib]System.ValueType { .pack 1 .size 32 }
.field static assembly valuetype '__msil_init_image_t' '__msil_init_image' at D_INIT
.data D_INIT = bytearray (
  05 00 00 00  00 00 00 00
  00 00 00 00  00 00 00 00
  00 00 00 00  00 00 00 00
  73 65 67 2D 6F 6B 00 00 )
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 4
  ldc.i4 64
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stsfld native int '__msil_data_base'
  ldsfld native int '__msil_data_base'
  ldsflda valuetype '__msil_init_image_t' '__msil_init_image'
  ldc.i4 32
  cpblk
  ldsfld native int '__msil_data_base'
  ldc.i4 32
  add
  ldc.i4.0
  ldc.i4 32
  initblk
  ldsfld native int '__msil_data_base'
  ldc.i4.8
  add
  ldsfld native int '__msil_data_base'
  ldc.i4 24
  add
  stind.i
  ldsfld native int '__msil_data_base'
  ldc.i4 16
  add
  ldftn int32 'sq'(int32)
  stind.i
  ldc.i4.1
  stsfld int32 '__msil_cctor_ran'
  ret
}
```

**Addressing**
- A global's address is `ldsfld base; ldc.i4 off; add`.
- `cpblk` takes the `&` from `ldsflda` directly.

**When the module `.cctor` runs** (e09c)
- It ran before the **app's main** was entered, triggered when main was JIT-compiled, even though the called export touches no `<Module>` member.
- It also runs before C# calls the exports, and before the entrypoint.

**Other data-segment facts**
- A 1 MiB bytearray assembles in about 0.2 s with either ilasm.
- `[ThreadStatic]` on a global field works: put `.custom instance void [mscorlib]System.ThreadStaticAttribute::.ctor() = (01 00 00 00)` right after the `.field` (e16).
- `.data` forms that work: `bytearray (..)`, `{ int32 (1), int32 (2), int32 (-3) }`, `int8 (7) [5]`, `int64 (..)` and `float64 (..)`.
- `int32 (1, 2, -3)` is a syntax error, and `char* ("hi")` produces UTF-16.

---

## 11. Generated startup (task 3) [V]

The exact method (from e10_main.il). It runs on all runtimes: 2 args → exit 43, argv[argc] == NULL holds, and native `exit(7)` → 7. On Linux (with "libc") argv is correct UTF-8.

```
.method public static int32 '__msil_start'(string[] args) cil managed
{
  .entrypoint
  .maxstack 4
  .locals init (int32 argc, native int argv, int32 i)
  ldarg.0
  ldlen
  conv.i4
  ldc.i4.1
  add
  stloc.0
  ldloc.0
  ldc.i4.1
  add
  conv.i
  sizeof native int
  mul
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stloc.1
  ldloc.1
  call string[] [mscorlib]System.Environment::GetCommandLineArgs()
  ldc.i4.0
  ldelem.ref
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  stind.i
  ldc.i4.0
  stloc.2
  br.s ACOND
ABODY:
  ldloc.1
  ldloc.2
  ldc.i4.1
  add
  conv.i
  sizeof native int
  mul
  add
  ldarg.0
  ldloc.2
  ldelem.ref
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  stind.i
  ldloc.2
  ldc.i4.1
  add
  stloc.2
ACOND:
  ldloc.2
  ldarg.0
  ldlen
  conv.i4
  blt.s ABODY
  ldloc.1
  ldloc.0
  conv.i
  sizeof native int
  mul
  add
  ldc.i4.0
  conv.u
  stind.i
  ldloc.0
  ldloc.1
  call int32 'main'(int32, native int)
  ret
}
```

On Windows, `StringToHGlobalAnsi` uses the ANSI code page, the same as a native MSVC main.

---

## 12. Identifiers and labels (task 3) [V]

e11 passes 23/23 with both ilasms on all runtimes.

**Names that work when quoted**
- Method names: `'foo.bar' '1digit' 'has$dollar' '.str' 'llvm.memcpy.p0.p0.i64' 'add' 'int32' 'value' 'method' 'native' 'a-b@c?d' 'with space' 'quote\'and\\back' '_ZN3foo3barEv' 'foo.cold.1' '__func__.main'`.
- Field names: `'g.field$1'` and `'int64'`.
- Parameters and locals: `'value'`, `'1p'`, `'add'` and `'tmp$1'`, including by-name `ldarg 'value'` / `stloc 'add'`.

**Labels**
- Work: `BB0_1:`, `L$4:`, `'$BB0_2':`, `'.LBB0_3':` and `'BB.5':`.
- A too-far `br.s` is an ilasm error ("too large for 1 byte pcrel"), so emit long branch forms.

**Unicode identifiers**

| Encoding | Framework ilasm | Modern ilasm (Windows and Linux) |
|---|---|---|
| UTF-8, UTF-8+BOM, UTF-16LE+BOM, octal escapes | double-encoded (`C3 83 C2 BC…`) | correct UTF-8 (UTF-8 without BOM, and octal escapes, were tested) |

---

## 13. Verification tools [V]

### 13.1 ilverify

- **Invocation**: `ilverify x.dll -r "C:\Program Files\dotnet\shared\Microsoft.NETCore.App\10.0.12\*.dll"`. The exit code is the error count.
- **Stops at the first error per method** (e14). With `-g StackByRef`, a later `StackUnexpected` is still not reported. It did continue after an `InitLocals` error.
- **Any `calli`** gives `Error []: ImportCalli not implemented`, which `-g` cannot suppress.
- **Expected errors on correct C IL**: `StackByRef` (ldind/stind through native int), `Unverifiable` (localloc, cpblk, initblk), `UnmanagedPointer`, `ExpectedNumericType` (ldsflda; conv.u), `InitLocals`.
- **Real bugs it catches** (e15, when they precede the first pointer access): StackOverflow (maxstack), StackUnderflow, PathStackDepth, PathStackUnexpected, ReturnEmpty, MethodFallthrough, LocallocStackNotEmpty, and StackUnexpected (int64 or F stored into an int32 local, int32+F, int64 returned as int32).
- **Mixing**: int64/native int mixing (e13) and int32 passed as a native int argument (e03) are also StackUnexpected.

### 13.2 JIT via jitall (each method under a 1 GB / 20 s guard)

| e15 case | net8 | net10 |
|---|---|---|
| b1 maxstack too small | InvalidProgramException | InvalidProgramException |
| b2 underflow | IPE | IPE |
| **b3 different stack depth at a join** | **accepted silently** | **killed by the guard at >1 GB within 1–2 s; an unguarded run reached a 40 GB working set** |
| b4 different types at a join | accepted | accepted |
| b5 int64 into int32 local | accepted | accepted |
| b6 extra item at ret | IPE | IPE |
| b7 float64 into int32 local | accepted | accepted |
| b8 localloc with a non-empty stack | IPE | IPE |
| b9 int32 + float64 | accepted | accepted |
| b10 int64 returned as int32 | accepted | accepted |
| b11 values on stack across a back edge | accepted | accepted |
| b12 fall off the end | IPE | IPE |

### 13.3 PEVerify

It also reports only the first error per method, plus the MD warnings from section 4.

### 13.4 Recommendation

1. A debug stack-type verifier in the backend, after ExplicitLocals/RegStackify. It should track {int32, int64, native int, F, &}, require consistent stack shapes at block boundaries, and compute or check `.maxstack`.
2. A `jitall` step in lit, under a timeout and memory limit.
3. ilverify as advisory only, with `-g StackByRef -g Unverifiable -g UnmanagedPointer -g InitLocals -g ExpectedNumericType`.

---

## 14. int64 vs native int [V]

e13 tests seven mixings of int64 and native int:
- int64 used as an ldind/stind address;
- `native int + int64`;
- `stloc` across the two types;
- argument passing in both directions;
- `calli` through an int64;
- returning an int64 from a native int method;
- `beq` int64 vs native int.

All run correctly on every runtime, Framework and Linux included, and ilverify flags all of them.

For the backend: pointers can stay i64 vregs (Design 2.3). The AsmPrinter then either types pointer slots as `native int` and inserts `conv.i`/`conv.i8` where needed (ilverify-clean), or uses int64 throughout (JIT-clean only).

---

## 15. Differences across runtimes and ilasms (task 5) [V]

| Area | .NET 8.0.31 | .NET 9.0.20 | .NET 10.0.12 | Framework 4.8 |
|---|---|---|---|---|
| `conv.r.un` u64→f64 | wrong | ok | ok | wrong |
| `conv.r.un; conv.r4` u64→f32 | wrong | wrong | ok | wrong |
| out-of-range float→int | indefinite | saturating | saturating | indefinite |
| localloc zeroed with localsinit | yes | yes | yes | **no** |
| constant overlapping cpblk | 0x0706040303020101 | same | same | 0x0606040403020101 |
| stack-depth mismatch at a join | accepted | (not run) | **JIT hang** | (not run) |

**Framework ilasm vs modern ilasm**
- Runtime behaviour was identical for every experiment.
- They differ in Unicode identifier handling, in flags (`/DET`, `/ANAME`), and in ildasm round-tripping.
- **Framework ildasm** prints `ldc.r8 inf`, which **Framework ilasm cannot re-assemble**.
- **Modern ildasm** prints `(00 00 00 00 00 00 F0 7F)`, which round-trips.
- The ildasm → ilasm → run round trip succeeded for e08, e09 and e11 with the Framework tools, and for e09, e11 and e16 with the modern tools.

**Still to verify**
- .NET 8 on Linux
- arm64
- `ilasm -optimize`
- `NativeMemory.AlignedAlloc`
- macOS and musl `"libc"` resolution
- quoted `.data` labels

---

## 16. The old LLVM MSIL backend (task 4)

**Where it lives**
- `git show bd24404718a6^:llvm/lib/Target/MSIL/MSILWriter.cpp` (1706 lines), plus `MSILWriter.h` and `README.TXT`.
- Removed on 2010-08-28 as "not maintained, buggy, no testcases".
- It was an IR-walking FunctionPass that used `createLowerSwitchPass()` (line 1700) instead of the CIL `switch`.

| Line | What it did | Verdict |
|---|---|---|
| 103–116 | `.assembly extern mscorlib {}`, fixed `.assembly MSIL {}` | the bare ref works [V] |
| 124–221 `printModuleStartup` | entrypoint with argv from `GetCommandLineArgs()` + `StringToHGlobalAnsi` into a localloc'd array; calls `$MSIL_Init`, then main | argv not NULL-terminated (**bug**) |
| 230–242 `getValueName` | single-quotes every name | right, but no escaping of `'` and `\` (**bug**) |
| 245–254 `getLabelName` | replaces `.` with `@` in labels | unnecessary [V] |
| 271–288 `getConvModopt` | `modopt(CallConvCdecl)` on every signature | wrong for managed calls |
| 337–362 | pointers as `void*`; structs/arrays as valuetypes | we use native int and memory aggregates |
| 448–476 `printConstLoad` | `ldc.r8 ( HEX )` | **bug**: parsed as 2 bytes [V] |
| 857–905 | ICMP_NE = `ceq; neg; not` (-1/0); LE/GE re-evaluate operands | **bug** |
| 908–999 | FCMP_ONE = `ceq; not` (always true) | **bug** |
| 1127–1143 | `conv.i4` on shift amounts | good |
| 652–658 | `stind.u*` → `stind.i*` | correct |
| 1051–1062 | localloc per alloca | we use one per frame; the stack must be empty [V] |
| 1245–1299 | one `.locals` per value; `.maxstack` = 2×max call operands, min 16 | unsafe heuristic |
| 1497–1611 | globals as RVA fields with typed `.data` and **`&(label)`** pointer initializers; dynamic initializers in `$MSIL_Init` | **fatal on CoreCLR** [V] |
| 1459–1476 | `.class value explicit ansi sealed 'T' { .pack 1 .size N }` | the same shape we use for blobs |
| 1631–1683 `printExternals` | every external → `pinvokeimpl("MSVCRT.DLL") <sig> preservesig {}` (no cdecl, no access); data imports via LoadLibrary/GetProcAddress | preservesig right [V]; missing access and cdecl |
| other | vararg via `System.ArgIterator`; invoke via `.try/catch [mscorlib]System.Exception` | replaced by Design 4.7 / 4.6 |

README TODO items: vector instructions, opaque types, using the CLI `switch`, and a linker to map external functions to DLLs.

---

## 17. Reference skeleton for milestone 1 (assembled from verified parts)

```
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly 'prog' { .ver 0:0:0:0 }
.module 'prog.dll'
.method assembly static pinvokeimpl("libc" cdecl) int32 'puts'(native int) cil managed preservesig {}
.field static assembly native int '__msil_data_base'
.class private explicit ansi sealed '__msil_data_t' extends [mscorlib]System.ValueType { .pack 1 .size N }
.field static assembly valuetype '__msil_data_t' '__msil_data_image' at D_0
.data D_0 = bytearray ( ... )
.method private hidebysig specialname rtspecialname static void .cctor() cil managed { /* AllocHGlobal; cpblk; initblk; stind.i relocs; global_ctors */ ret }
.method assembly static int32 'fib'(int32 'n') cil managed { .maxstack <exact> .locals (int32, int32) ... BB0_1: ... ret }
.method public static int32 '__msil_start'(string[] args) cil managed { .entrypoint ... call int32 'main'(int32, native int) ret }
.class public abstract sealed auto ansi 'Exports' extends [mscorlib]System.Object
{ .method public static int32 'fib'(int32 n) cil managed { .maxstack 1 ldarg.0 call int32 'fib'(int32) ret } }
```

Notes:
- `.locals` without `init` works and suits C.
- Prefer unquoted `D_<n>` data labels; quoted data labels are [U].

---

## Appendix A: files in $SCR

**Harness**
- `common.il` (checker)
- `run.sh` / `run2.sh` / `final_runs.sh`
- `ilv.sh` / `ilv.py` / `san.py`
- `rc8.json` / `rc9.json` / `rc10.json`
- `jitall.cs`, `consumer.cs`, `csc.rsp`
- `out/linux/runall.sh`, `out3/linux_ilasm.sh`
- `out/big/big.il` (generated 1 MiB data test)

**Experiments**

| File | Covers |
|---|---|
| e01_hello, e01b_bareref, e01c_sysruntime | module header and assembly references |
| e02_exit | exit codes |
| e03_sigs | signatures, classes |
| e04_mem | memory |
| e05_conv, e05b_u64fp | conversions |
| e06_cmp | comparisons, switch |
| e07_fnptr | function pointers |
| e08_pinvoke, e08b_noflush (e08 without fflush) | P/Invoke, stdio |
| e08c_modern | modern-only interop |
| e08d_negative | P/Invoke failure cases |
| e08L_linux | P/Invoke on Linux |
| e09_modinit | data segment |
| e09b_rvapitfalls, e09b2_rvawrite | `&(label)` and RVA writes |
| e09c_lib, e09c_app | module .cctor timing |
| e10_main, e10L_main (e10 with "libc") | startup |
| e11_names, e11b_bom (e11 with UTF-8 BOM), e11c_utf16 (e11 as UTF-16LE), e11d_octal | identifiers |
| e12_access, e12b_access, e12c_access | accessibility |
| e13_i64ptr | int64/native int mixing |
| e14_ilv | ilverify behaviour |
| e15_broken | malformed IL |
| e16_syntax | ILAsm syntax |
| e17_abort, e17b_void | abnormal exits |
| e18_ldftn_pinvoke | ldftn of P/Invoke methods |
| e19_memmove | overlapping copies |
| e20_align | alignment |
| e21_voidptr | `void*` types |
| e22_arith | arithmetic, bitcasts, shifts |

## Appendix B: logs in $SCR

| File | Contents |
|---|---|
| `final_log_san.txt` | Windows matrix: both ilasms × net8/9/10 + Framework |
| `linux_log.txt` | WSL .NET 10 run of the Windows-assembled DLLs |
| `linux_ilasm_log.txt` | Linux modern ilasm assembling and running |
| `ilverify_summary.txt` | ilverify summaries |
| `jitall_e15.txt` | guarded per-method force-JIT of e15 |
