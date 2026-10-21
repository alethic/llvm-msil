# P2 recon: the clang driver for Phase 2 (`driver-toolchain`)

Status: recon report for Phase 2 planning (P2-B, and the `-c` part of P2-A), 2026-10-21. Base: branch `msil` at
`1ea64fe9fc7c`. Scratch: `C:\llvm-msil-scratch\p2\driver-toolchain\` (about 160 MB: a 110 MB prototype `clang.exe`,
a 30 MB glibc sysroot, small experiments). Supporting files (prototype sources and diffs, scripts, proposed tests,
CMake files, trimmed logs): [driver-toolchain-files/](driver-toolchain-files/).

Inputs read: Design.md (all; 3, 5, 6, 7, 8 and 11 closely), ImplementationPlan.md sections 1-5, 7 and the section 12
rows of 2026-10-14/15, Status.md, `msil/orchestration/P2/inputs.md`, recon `clang.md` (Phase-1 driver recon, its
section 5.6 sketched this toolchain), and the Phase 2 recon reports already present: `bcl-binding.md`,
`lld-flavor.md`, `mc-streamer.md`. Every file, line and API named below was read in this checkout; behaviour was
measured on this host (Windows 11, VS 2026 18.10 with MSVC 14.51.36231 and Windows SDK 10.0.26100, .NET Framework
4.8, .NET 8.0.31 and 10.0.12; WSL Ubuntu 26.04 with .NET 8 and 10). Anything not checked is marked *unverified*.

Installs: none on the system. In WSL, `apt-get download libc6-dev linux-libc-dev libc6 libgcc-s1` (Ubuntu 26.04,
glibc 2.43-2ubuntu2.4, linux-libc-dev 7.0.0-38.38; no root needed) fetched four `.deb` files into scratch, which were
unpacked with `dpkg-deb -x` into a sysroot directory; nothing was installed with `dpkg -i`/`apt install`. No tracked
file was changed; no `cmake`/`ninja` ran in `D:\llvm-msil\build` (the prototype clang was compiled and linked by hand
into scratch, section 11). D: had 18 GB free at the end.

## 0. Summary

**Findings**

1. **Phase 1 has no MSIL toolchain.** An MSIL triple gets its native toolchain from `Driver::getToolChain`
   (`clang/lib/Driver/Driver.cpp:6168-6395`: `toolchains::Linux`, `MSVCToolChain`, `MinGW`), and five MSIL hooks
   stop everything that would write an object or link: `ToolChain::getAssemble` and `ToolChain::getLink`
   (`clang/lib/Driver/ToolChain.cpp:736-745, 758-767`), `Clang::ConstructJob` for `AssembleJobAction`
   (`Clang.cpp:5540-5543`) and `ClangAs::ConstructJob` (`Clang.cpp:9340-9342`), all reporting
   `err_drv_msil_no_object_output` (`DiagnosticDriverKinds.td:162`). `-S`, `-emit-llvm` and `-flto -c` work; `-c`,
   every link (LTO included), `-fno-integrated-as -c` and `.s` inputs are errors (pinned by
   `clang/test/Driver/msil-toolchain.c`, 331 lines).
2. **The native toolchain is the right base, and a template mixin `MSILToolChain<NativeTC>` works.** A prototype
   (`MSIL.h` 90 lines, `MSIL.cpp` 260 lines, 4 small `Driver.cpp` edits, one `ToolChain.cpp` hunk removed; section 4)
   compiles against this checkout's headers (`Linux`, `MSVCToolChain`, `MinGW` are not `final`; the overridden
   virtuals are public or protected), and a scratch `clang.exe` built with it:
   - changes only two things in the `-S` cc1 line on every MSIL triple: `-mrelocation-model static` instead of
     `pic`/`-pic-level 2`/`-pic-is-pie`, and no `-funwind-tables=2` (header search, effective triple, MSVC version,
     predefines unchanged);
   - produces **byte-identical IL for all 372 Phase-1 corpus compiles** (93 C programs x 2 triples x -O0/-O2);
   - plans `msil-ld` link jobs on all MSIL triples, and with a stand-in `msil-ld` (the lld-flavor recon's prototype
     plus ilasm) a two-file program linked through the driver runs on .NET 8, .NET Framework 4.8 and WSL .NET 10;
   - passes the unchanged Phase-1 `msil-toolchain.c` except its LTO-link block (8 RUN commands, lines 159-184), which
     Phase 2 replaces.
3. **Output naming is constrained by the .NET host, not by the OS** (measured, section 4.7): `dotnet exec` and
   `dotnet <file>` refuse any file whose name does not end in `.dll` or `.exe` ("dotnet exec needs a managed .dll or
   .exe extension", exit 0x80008081) on Windows and Linux, .NET 8 and 10, so the native default `a.out` is unusable.
   The PE kind does not matter to modern .NET (EXE and DLL images both run under either extension), but .NET
   Framework starts only an EXE image (a DLL image named `.exe` fails with Win32 error 193). Hence: msil-ld writes
   an EXE image for every program and a DLL image for `-shared`, whatever the name; the driver's default image name
   for MSIL triples becomes `a.exe` on every OS.
4. **Default libraries need msil-ld features the glibc and Windows SDK layouts force** (section 4.5): glibc 2.43's
   `libc.so`, `libm.so` and even `libm.a` are GNU ld scripts; Design.md 5.2's static-first search finds glibc's
   native `libc.a` (2,233 ELF members) before `libc.so` (measured with the prototype msil-ld: 2,233 warnings and an
   "unknown file type" error); `atexit` is only in `libc_nonshared.a`; `stdin`/`stdout`/`environ` are data symbols.
   On windows-msvc, `ucrt.lib` maps the CRT to `api-ms-win-crt-*-l1-1-0.dll` (P/Invokes to those names run on .NET 8,
   10 and Framework, measured), but `memcpy`, `memchr`, `strchr`, `strstr`... are in `vcruntime.lib`
   (`VCRUNTIME140.dll`), while `ucrtbase.dll` exports both sets (2,484 names).
5. **Headers: hosted headers already work for both triples on this host, given a sysroot or VS detection.** All 29
   C standard headers plus 9 POSIX headers parse for `x86_64-unknown-linux-gnu-msil` with the glibc sysroot; all 29
   plus 6 Windows headers (`windows.h` included) parse for `x86_64-pc-windows-msvc-msil` except `tgmath.h`, which
   fails natively too. clang finds the VC/UCRT/SDK headers only inside a VS developer environment (`INCLUDE`) or with
   explicit `-Xmicrosoft-visualc-tools-root`/`-Xmicrosoft-windows-sdk-root`/`-Xmicrosoft-windows-sys-root`
   (`/vctoolsdir`, `/winsdkdir`, `/winsysroot`): VS 2026 is not detected through the Setup Configuration API on this
   host, by this fork's clang or by VS's own clang 22.1.3 (upstream behaviour, cause unverified). Hosted *codegen*
   hits Phase-3 limits: UCRT `stdio.h` at `-O0` emits its inline variadic `sprintf`/`_snprintf` definitions
   (a two-line MSILPrepareIR change removes them, verified with `opt -passes=globaldce`); glibc at `-O2` turns
   `getchar()` into `getc(stdin)`, a data import.
6. **CMake drives clang + msil-ld with a toolchain file and a small platform module** (section 7, tested in scratch
   for both triples): static library (llvm-ar), executable and `-shared` library built; the executable ran on .NET 8
   and Framework 4.8 (Windows triple) and on WSL .NET 10 (Linux triple), with the `runtimeconfig.json` written by
   CMake. Two traps: for windows-msvc triples clang predefines `_MSC_VER`, so CMake sets `CMAKE_C_SIMULATE_ID=MSVC`
   and its `Compiler/Clang.cmake` then defines no GNU rules and no `--target=` spelling unless the platform module
   loads them; and with `-flto` in the initial flags CMake cannot find its compiler-identification strings in
   bitcode objects (it succeeds through the linked `a.exe`).

**Recommendations**

- R1. Add `clang/lib/Driver/ToolChains/MSIL.{h,cpp}`: `tools::msil::Linker` and
  `template <class NativeTC> class MSILToolChain final : public NativeTC`, instantiated for `Linux`, `MSVCToolChain`
  and `MinGW` (Darwin later; `DarwinClang` is not `final` either), created by `createMSILToolChain()` from one line at
  the top of `Driver::getToolChain`. Keep everything that shapes the compile native; replace the linker, the
  assembler, the static-library tool, PIC/PIE and unwind defaults, the runtime library type and the sanitizer set
  (section 4.3).
- R2. The link job: `msil-ld --target=<triple> [--sysroot=] [--shared] -L... [LTO options] <inputs in order>
  <default libs> [<per-triple helper archive>] -o <out>`; no crt files, no dynamic linker, no `-pie`, no gold plugin.
  Do not call `tools::addLTOOptions` (it would add `-plugin <dir>/LLVMgold.dll`: its lld test at `CommonArgs.cpp:1078`
  recognises only `lld`, `lld-link` and `ld.lld`); map `-O<n>`, `-flto-jobs=`, `-flto-partitions=`, `-save-temps` and
  `-mllvm` to lld spellings, as HIP does for its device link (`HIPAMD.cpp:94-97`).
- R3. Default libraries (unless `-nostdlib`/`-nodefaultlibs`): glibc `-lc -lm` (the backend calls libm for
  `sqrt`/`fma`/`floor`, measured), musl `-lc`, MinGW `-lmsvcrt` (or the user's `-lucrt`/`-lmsvcr*`) plus `-ladvapi32
  -lshell32 -luser32 -lkernel32`, windows-msvc `-lucrt -lvcruntime -lkernel32` (native `/MD` parity) or the Phase-1
  `ucrtbase`-only set (user decision, section 9). msil-ld then needs: skipping archives that hold no CIL or bitcode
  member during `-l` search, a GNU ld script reader (GROUP/INPUT/AS_NEEDED, sysroot-relative), COFF and MinGW import
  library readers, and an address import for native data symbols (P2-C, native-imports).
- R4. References and the export class need no new driver option in P2-B: `-Wl,--reference=<path>` (or CMake's
  `LINKER:--reference=...`) and positional `.dll` inputs (already `TY_Object`, passed in order), and
  `-Wl,--export-class=<Ns.Class>` with `-shared` (and clang-cl `/LD`). The driver adds no reference by default
  (bcl-binding 5.10: msil-ld's built-in `mscorlib` table applies).
- R5. Default image name `a.exe` for MSIL triples (`Driver::getDefaultImageName`, `Driver.cpp:5460-5463`); never
  append an extension to `-o`; msil-ld names the assembly after the output stem and may warn when the output name
  ends in neither `.exe` nor `.dll`.
- R6. `-c` stays one `cc1 -emit-obj` job through the integrated assembler (`IsIntegratedAssemblerDefault()` true,
  `canCollapseAssembleAction`, `Driver.cpp:4880-4886`); P2-A only deletes `Clang.cpp:5540-5543` once the MSIL object
  streamer exists. `-fno-integrated-as -c` reports `err_no_external_assembler` from `MSILToolChain::buildAssembler`
  (as `MSVCToolChain` does), so both `ToolChain.cpp` hooks go away; `.s`/`.il` inputs and `-save-temps -c` keep a
  reworded clean error in `ClangAs` until an ILAsm parser exists (mc-streamer R7).
- R7. Phase 2 tests stay freestanding for execution (as Phase 1), plus `-###` driver tests with fake trees
  (`Inputs/debian_multiarch_tree`, `split-file` `/winsysroot` trees; four proposed files, three of them run green
  against the prototype, section 6) and an optional hosted compile-only leg gated on a glibc sysroot and on
  `INCLUDE`/VS detection. libc proper (variadic stdio, data imports, errno, atexit, setjmp) stays in Phase 3. Fix
  MSILPrepareIR to drop unreferenced discardable definitions before diagnosing them (unblocks UCRT `stdio.h` at
  `-O0`).
- R8. Keep codegen switches (P3-A) and the assembly name out of `cl::opt`s at link time: module flags or function
  attributes survive bitcode, so LTO needs no driver plumbing (`-mllvm` forwarding is a fallback, not the interface).

## 1. Method

| Step | What | Where |
|---|---|---|
| Read | Phase-1 driver hooks, `WebAssembly.{h,cpp}`, `Linux`, `MSVC`, `MinGW`, `Gnu` (GNU linker, static-lib tool), `CommonArgs.cpp` (LTO), `HIPAMD.cpp`, `Driver.cpp` (toolchain selection, naming, program search), `MSVCPaths.cpp`, lld's flavor dispatch | this checkout |
| Measure | include paths and cc1 lines per triple, `-print-*` options, PE kind x file name x host, API-set P/Invoke, CRT DLL and import-library exports, glibc sysroot contents | `logs/` |
| Prototype | `MSIL.{h,cpp}` + Driver/ToolChain hooks compiled with the shared build's exact flags and PCH, linked into a scratch `clang.exe` | `proto/`, `scripts/build-proto.py` |
| Validate | corpus IL identity (372 compiles), Phase-1 driver test, four proposed driver tests through real lit, end-to-end links with a stand-in `msil-ld`, CMake projects for both triples | `logs/`, `tests/`, `cmake/` |

## 2. How the driver handles MSIL triples today

### 2.1 Toolchain selection

`Driver::getToolChain` (`Driver.cpp:6168`) switches on the OS only; the object format plays no role:

| Triple | Toolchain (Driver.cpp) | cc1 `-triple` |
|---|---|---|
| `x86_64-unknown-linux-gnu-msil`, `*-linux-musl-msil`, `aarch64-*-linux-*-msil` | `toolchains::Linux` (6205-6227) | unchanged |
| `x86_64-pc-windows-msvc-msil` | `toolchains::MSVCToolChain` (6284-6293; `-fuse-ld=bfd` would pick CrossWindows) | `x86_64-pc-windows-msvc<MSVC version>-msil`: `19.51.36260` with a detected VS, else `19.33.0` |
| `x86_64-w64-windows-gnu-msil` | `toolchains::MinGW` (6274-6276) | unchanged; MinGW still passes `-exception-model=seh` (CodeGen drops it) |

### 2.2 The Phase-1 hooks

| Hook | Lines | Effect |
|---|---|---|
| `ToolChain::getAssemble` | `ToolChain.cpp:736-745` | the external-assembler route (`-fno-integrated-as -c`): `err_drv_msil_no_object_output`, no job |
| `ToolChain::getLink` | `ToolChain.cpp:758-767` | every link, LTO included: the same error, no job ("The driver selects the link tool before it builds the link's inputs") |
| `ToolChain::getLinkerWrapper` | `ToolChain.cpp:796-805` | returns null when `getLink()` does |
| `ToolChain::getSupportedSanitizers` | `ToolChain.cpp:1960-1963` | removes KCFI |
| `Clang::ConstructJob`, `AssembleJobAction` | `Clang.cpp:5540-5543` | `-c` (the collapsed compile+backend+assemble job): the error |
| `-fembed-bitcode` | `Clang.cpp:5715-5722` | `err_drv_unsupported_opt_for_target` |
| `-fms-volatile` default | `Clang.cpp:6418-6425` | off for MSIL |
| `/GS` default | `Clang.cpp:8975-8981` | off for MSIL |
| `ClangAs::ConstructJob` | `Clang.cpp:9340-9342` | `.s` inputs and `-save-temps -c`: the error |
| Diagnostic | `DiagnosticDriverKinds.td:162` | "emitting object files is not yet supported for MSIL target '%0'; use -S to produce ILAsm text, or -emit-llvm or -flto to produce bitcode" |

What works today: `-S` (ILAsm text; also with `-fno-integrated-as`), `-S -emit-llvm`, `-c -emit-llvm`, `-flto -c`
(`-emit-llvm-bc -flto=full`). `clang/test/Driver/msil-toolchain.c` pins all of it: `-S` cc1 lines per triple
(12-110), the object errors (112-150), the LTO-link errors (152-184), `-fno-integrated-as -S` (186-191), bitcode
output (193-208), KCFI (210-224), `-fembed-bitcode` (226-255), basic-block sections and friends (257-329).
`msil/utils/upstream-hooks.txt` lists `ToolChain.cpp` (204-211), `Clang.cpp` (213-223) and the diagnostic file
(127-129) as hooks covered by that test.

### 2.3 How clang finds headers today

| Triple | Header search (cc1 `-internal-isystem`/`-internal-externc-isystem`) | Measured on this host |
|---|---|---|
| linux-gnu-msil, no `--sysroot` | resource dir, `/usr/local/include`, `/include`, `/usr/include` (all missing on Windows) | only the resource dir exists: freestanding headers only |
| linux-gnu-msil, `--sysroot=S` | resource dir, `S/usr/include/x86_64-linux-gnu`, `S/usr/include` (multiarch from `Linux::getMultiarchTriple`, which ignores the `msil` suffix) | glibc sysroot works (section 5) |
| windows-msvc-msil, VS dev shell | `INCLUDE`/`EXTERNAL_INCLUDE` (`MSVC.cpp:866-874`): VC, ATLMFC, Auxiliary\VS, UCRT, um, shared, winrt, cppwinrt, NETFXSDK | works (`msil-env.ps1`) |
| windows-msvc-msil, no dev shell | VC detection (`MSVC.cpp:552-558`: command line, environment, Setup Configuration, registry); UCRT and SDK include dirs are added only when a VC toolchain was found (`MSVC.cpp:878`) | **fails**: falls back to `Microsoft Visual Studio 8/9/10` paths that do not exist (`-fms-compatibility-version=19.33`); the same with VS's clang 22.1.3, so upstream behaviour |
| windows-msvc-msil, `-Xmicrosoft-visualc-tools-root` + `-Xmicrosoft-windows-sdk-root` (or `-Xmicrosoft-windows-sys-root`, clang-cl `/vctoolsdir`, `/winsdkdir`, `/winsysroot`; `Options.td:9860-9868`) | VC include + atlmfc, UCRT, shared, um, winrt, cppwinrt | works; version `19.51.36260` from `cl.exe` |
| windows-gnu-msil | `<clang dir>/../x86_64-w64-mingw32/{include,usr/include}`, `<clang dir>/../include` | no MinGW installed: freestanding only |
| any, `-ffreestanding` | unchanged search; `__STDC_HOSTED__` 0, so clang's `stdint.h`, `limits.h`... do not `#include_next` | 9 freestanding headers parse on both triples with nothing installed |

`-ffreestanding` does not change header search (only `-nostdinc`/`-nostdlibinc` do). Phase-1 tests therefore pass
`-ffreestanding` and include only resource-dir headers and `msil/test/Inputs/msil_test.h`.

## 3. The models

### 3.1 WebAssembly (`clang/lib/Driver/ToolChains/WebAssembly.{h,cpp}`)

| Piece | Lines | What it does | For MSIL |
|---|---|---|---|
| `wasm::Linker::getLinkerPath` | 46-70 | absolute `-fuse-ld` path taken as is; `-fuse-ld=lld` means `wasm-ld`; `ld` means the default; anything else `err_drv_invalid_linker_name`; default `GetProgramPath(getDefaultLinker())` | same shape (prototype) |
| `wasm::Linker::ConstructJob` | 104-259 | `-m wasm32/64`, `-s` -> `--strip-all`, `-L`/`-u`, `AddFilePathLibArgs`, crt1 (`crt1-command.o`/`crt1-reactor.o`) unless `-nostdlib`/`-nostartfiles`, `-shared`, `AddLinkerInputs`, `-lc` and `AddRunTimeLibs` unless `-nostdlib`/`-nodefaultlibs`, `-o`, then optional `wasm-opt` | keep the skeleton; no crt file (startup is the generated `<msil>Main`, D27), no post-link optimizer |
| LTO | none | wasm-ld gets no `-O`/`-plugin-opt` from the driver; `addLTOOptions` is not called | MSIL passes `--lto-O<n>` etc. itself (section 4.8) |
| Constructor | 275-309 | program paths: the driver dir; file paths: `<sysroot>/lib/<multiarch>` (+ `llvm-lto/<version>` dirs with LTO) | MSIL keeps the native file paths instead |
| Defaults | 311-336 | `wasm-ld`, no PIC/PIE, `HasNativeLLVMSupport` true, `IsMathErrnoDefault` false, compiler-rt, libc++ | MSIL: PIC/PIE off, native LLVM support on, math-errno native |
| Header search | 511-546 | resource dir, `C_INCLUDE_DIRS`, `<sysroot>/include/<multiarch>`, `<sysroot>/include` | MSIL keeps the native toolchain's |

### 3.2 The native toolchains MSIL wraps

| Toolchain | Kept for MSIL (compile side) | Replaced (link side) |
|---|---|---|
| `Linux` (`Linux.h:19`, `Gnu.cpp` GNU linker 272-610) | sysroot (`computeSysRoot`), multiarch include dirs, GCC installation detection, file paths (`<S>/lib/x86_64-linux-gnu`, `<S>/usr/lib/x86_64-linux-gnu`, ...), predefines, `IsMathErrnoDefault` | `ld`, `-dynamic-linker`, `-pie`, crt1/Scrt1/crti/crtbegin(S)/crtend(S)/crtn (`Gnu.cpp:384-424, 577-595`), `-lgcc`/`-lgcc_s`, `addLTOOptions` (443) |
| `MSVCToolChain` (`MSVC.h:59`, linker `MSVC.cpp:65-200`) | VC/UCRT/SDK detection and include dirs, effective triple with the MSVC version, `-fms-compatibility-version`, clang-cl argument translation, the library-dir helpers `getSubDirectoryPath`, `getUniversalCRTLibraryPath`, `getWindowsSDKLibraryPath`, `useUniversalCRT` (public) | `link.exe`/`lld-link`, `-defaultlib:libcmt`/`msvcrt`/`oldnames` (from `--dependent-lib`), `-libpath:` (skipped when `LIB` is set: `MSVC.cpp:162-186`), no static-lib tool, forced PIC (`MSVC.cpp:601-613`) |
| `MinGW` (`MinGW.h:61`, linker `MinGW.cpp:107-395`) | sysroot/GCC detection and include dirs | `ld`/`ld.lld`, crt2/crtbegin/crtend, `-lmingw32 -lgcc -lgcc_eh -lmoldname -lmingwex -lmsvcrt` (`MinGW.cpp:61-104`), system libraries (`357-374`) |

## 4. MSILToolChain

### 4.1 Shape: template mixin or delegation

| | Template mixin `MSILToolChain<NativeTC> : NativeTC` | Delegation (`MSILToolChain : ToolChain` owning a native toolchain, like `CudaToolChain`'s `HostTC`) |
|---|---|---|
| Code | a dozen overrides (prototype: 90 + 260 lines) | forward header search, effective triple, `TranslateArgs`, `addClangTargetOptions`, `computeMSVCVersion`, copy `getFilePaths()`/`getProgramPaths()` (non-virtual member lists filled by constructors), replicate the OS switch to build the native toolchain |
| Fidelity | identical cc1 lines except the overridden defaults (measured, 4.2) | identical only as far as the forwards reach; every new native virtual must be forwarded |
| Upstream merges | native changes flow in; MSIL tests catch regressions | forward list drifts |
| Risk | a native override the MSIL toolchain should not inherit (MinGW's `getTool` returns gcc tools for preprocess/compile of non-clang inputs only; harmless) | more code |

Recommendation: the template (clang recon 5.6 proposed it; the prototype confirms it compiles and behaves). Explicit
instantiations in `MSIL.cpp` with `extern template` in `MSIL.h`, so `Driver.cpp` needs only the declaration.

### 4.2 What stays native (measured)

`logs/cc1-diff.log`: for `-S` at `-O0` and `-O2` on linux-gnu, windows-msvc, windows-gnu and aarch64-linux-musl
MSIL triples, the only cc1 differences between the shared build's clang and the prototype are `"-mrelocation-model"
"static"` instead of `"pic" "-pic-level" "2"` (and `"-pic-is-pie"` on Linux) and the missing `"-funwind-tables=2"`
(the resource-dir path differs only because the scratch clang lives elsewhere). `logs/corpus-diff.log`: 93 programs
of `msil/test/M1` x 2 triples x 2 levels = 372 compiles, **0 differing `.il` files**. Kept: header search, sysroot,
multiarch, MSVC detection and version, `-fms-compatibility-version`, `-fms-extensions`, predefines (including
`__MSIL__` from the TargetInfo wrapper and the absent `__ELF__`), `-fmath-errno` defaults, `-fno-use-cxa-atexit` on
Windows, MinGW's `-exception-model=seh`, the Phase-1 `Clang.cpp` defaults (`-fms-volatile` off, `/GS` off).

### 4.3 Overrides (prototype `proto/MSIL.h`)

| Override | Value | Why |
|---|---|---|
| `getDefaultLinker()` | `"msil-ld"` | the only linker for CIL objects and MSIL bitcode |
| `buildLinker()` | `new tools::msil::Linker(*this)` | section 4.4 |
| `buildAssembler()` | diagnose `err_no_external_assembler`, return null | reached only with `-fno-integrated-as`; there is no external CIL assembler (ilasm writes assemblies, not objects), the same answer `MSVCToolChain::buildAssembler` gives (`MSVC.cpp:578-583`) |
| `buildStaticLibTool()` | `tools::gnutools::StaticLibTool` (`llvm-ar rcsD`, `Gnu.cpp:227-270`) | `--emit-static-lib` on every MSIL triple; the native MSVC toolchain has none (`ToolChain::buildStaticLibTool` is `llvm_unreachable`) |
| `IsIntegratedAssemblerDefault()` | `true` (already the base value) | `-c` collapses to one `cc1 -emit-obj` (section 4.10) |
| `HasNativeLLVMSupport()` | `true` | `.bc`/`.ll` and bitcode `.o` inputs go to msil-ld as they are (MSVC's base answer is false) |
| `isPICDefault`, `isPIEDefault`, `isPICDefaultForced` | `false`, `false`, `false` | the backend forces `Reloc::Static` (D8); `MSVCToolChain` forces PIC on x64, which would make `-fno-pic` an ignored flag |
| `getDefaultUnwindTableLevel` | `None` | no native unwind tables in CIL |
| `getSupportedSanitizers` | `(Undefined & ~Vptr) | FloatDivideByZero | UnsignedIntegerOverflow | UnsignedShiftBase | ImplicitConversion | Nullability | LocalBounds` | only checks without a native runtime or native code layout; `-fsanitize=address` becomes `unsupported option ... for target` (measured), UBSan works in trap mode |
| `GetDefaultRuntimeLibType` (**not in the prototype; add**) | `RLT_CompilerRT` | Linux defaults to libgcc (`-print-libgcc-file-name` prints `libgcc.a` for the MSIL triple today); the helper archive of Design.md 6 is the compiler-rt-style one (4.11) |

Not overridden on purpose: `ComputeEffectiveClangTriple`, `AddClangSystemIncludeArgs`, `addClangTargetOptions`,
`TranslateArgs`, `IsMathErrnoDefault`, `GetExceptionModel` (MinGW SEH; CodeGen ignores it for MSIL),
`getDefaultDebugFormat` (DWARF for non-COFF in `MSVC.h:78`; Phase 4 decides on Portable PDB).

Construction: `createMSILToolChain(D, Triple, Args)` returns `MSILToolChain<Linux>` for `isOSLinux()`,
`<MSVCToolChain>` for `isWindowsMSVCEnvironment()`, `<MinGW>` for `isWindowsGNUEnvironment()`, else null (the native
switch then runs and the frontend rejects the triple, as D6 does today). The hook in `Driver::getToolChain`:

```cpp
  auto &TC = ToolChains[Target.str()];
  if (!TC && Target.isOSBinFormatMSIL())
    TC = toolchains::createMSILToolChain(*this, Target, Args);
  if (!TC) {
    switch (Target.getOS()) {
```

### 4.4 The link job

Prototype `msil::Linker::ConstructJob` (`proto/MSIL.cpp`), in order:

1. `--target=<normalized triple>` (`TC.getTriple().str()`, without the MSVC version; lld-flavor 2.4: msil-ld accepts
   inputs whose native-format triples match, so the driver states the codegen triple explicitly).
2. `--sysroot=<sysroot>` when set (msil-ld resolves the absolute paths inside glibc's ld scripts against it).
3. `--shared` for `-shared` (add clang-cl `/LD`/`/LDd`: the prototype names the output `a.dll` for `/LD` but does
   not pass `--shared` yet, measured). `-static` is `unsupported option '-static' for target` (no static C library
   exists for any MSIL triple; plan P5-A). `-pie`, `-no-pie`, `-rdynamic` are claimed silently. `-s` ->
   `--strip-all`.
4. `-L` and `-u` from the command line, then `TC.AddFilePathLibArgs` (the native toolchain's library directories),
   then on windows-msvc the `%LIB%` entries (developer prompt) or, with `/winsysroot`-style options or without `LIB`,
   the detected VC lib, UCRT lib and SDK `um` lib directories (msil-ld is a GNU-style flavor and does not read `LIB`).
5. LTO options when `isUsingLTO` (4.8).
6. `AddLinkerInputs`: objects, bitcode, archives, `.dll` inputs, `-l`, `-Wl`/`-Xlinker`, `-e`, in command-line order.
7. Unless `-nostdlib`/`-nodefaultlibs`: `-lpthread` for `-pthread` on Linux, the default libraries (4.5), then the
   per-triple helper archive if it exists (4.11).
8. `-o <output>`; response files `ResponseFileSupport::AtFileCurCP()` as wasm (MinGW uses `AtFileUTF8()` for its
   lld, `MinGW.cpp:391`; lld reads response files as UTF-8, so UTF-8 is the safer choice for non-ASCII paths,
   *unverified* by experiment).

Measured `-###` (prototype, `--sysroot` = the scratch glibc tree; Windows with `-Xmicrosoft-*` options):

```text
"msil-ld" "--target=x86_64-unknown-linux-gnu-msil" "--sysroot=$SR" "-L$SR/usr/lib/x86_64-linux-gnu" "-L$SR/usr/lib64"
  "-L$SR/usr/lib" "--lto-O2" "$TMP/a-*.o" "$TMP/b-*.o" "-lc" "-lm" "-o" "prog.exe"
"msil-ld" "--target=x86_64-pc-windows-msvc-msil" "-L$VC\lib\x64" "-L$SDK\Lib\10.0.26100.0\ucrt\x64"
  "-L$SDK\Lib\10.0.26100.0\um\x64" "--lto-O2" "$TMP/a-*.o" "$TMP/b-*.o" "-lucrt" "-lkernel32" "-o" "prog.exe"
"msil-ld" "--target=x86_64-w64-windows-gnu-msil" "-L<clang>/../x86_64-w64-mingw32/lib" ... "$TMP/a-*.o"
  "-lmsvcrt" "-ladvapi32" "-lshell32" "-luser32" "-lkernel32" "-o" "a.exe"
"msil-ld" "--target=x86_64-unknown-linux-gnu-msil" "--shared" "-L<dir>" "-u" "sym" "$TMP/x.o" "b.o" "lib.dll" "-lfoo"
  "--export-class=Contoso.Zlib.Native" "--reference=<dir>/refs" "-e" "start" "-lc" "-lm" "-o" "zlib.dll"
```

(The prototype's default-library list on windows-msvc is `-lucrt -lkernel32`; 4.5 explains why `-lvcruntime`
belongs there too.)

### 4.5 Default libraries per triple

| Triple | Native default (driver) | Proposed MSIL default | What msil-ld meets (measured) |
|---|---|---|---|
| `*-linux-gnu-msil` | crt files, `-lgcc --as-needed -lgcc_s --no-as-needed -lc ...` | `-lc -lm` | glibc 2.43 (Ubuntu 26.04): `libc.so` = `GROUP ( /usr/lib/x86_64-linux-gnu/libc.so.6 /usr/lib/x86_64-linux-gnu/libc_nonshared.a AS_NEEDED ( /lib64/ld-linux-x86-64.so.2 ) )`; `libm.so` = `GROUP ( .../libm.so.6 AS_NEEDED ( .../libmvec.so.1 ) )`; **`libm.a` is also a script** (`GROUP ( .../libm-2.43.a .../libmvec.a )`); `libc.a` (6.2 MB, 2,233 ELF members) comes first in a static-first search; `libc_nonshared.a` defines only `atexit`, `at_quick_exit`, `pthread_atfork`, `__pthread_atfork`, `__stack_chk_fail_local`; sonames `libc.so.6`, `libm.so.6`; dynamic symbols are versioned (`puts@@GLIBC_2.2.5`); `stdin`, `stdout`, `environ`, `optarg` are data (`D`/`B`/`V`) |
| `*-linux-musl-msil` | `-lc` | `-lc` | musl keeps libm in libc (no sysroot measured here) |
| `*-windows-msvc-msil` | `--dependent-lib=libcmt`/`msvcrt` + `oldnames` in the object; `link.exe` | `-lucrt -lvcruntime -lkernel32` (or `ucrtbase` only, decision 9.1) | `ucrt.lib` (SDK `ucrt\x64`): 1,392 archive members naming 14 `api-ms-win-crt-*-l1-1-0.dll` DLLs (math 293, multibyte 203, string 180, stdio 162, ...), e.g. `puts` -> `api-ms-win-crt-stdio-l1-1-0.dll`; **`memcpy`, `memset`, `memmove`, `memcmp`, `memchr`, `strchr`, `strrchr`, `strstr`, `longjmp` are not in `ucrt.lib` but in `vcruntime.lib`** (VC `lib\x64`; `VCRUNTIME140.dll`, a VC++ redistributable DLL); `ucrtbase.dll` (System32) exports 2,484 names, both sets included; `printf`/`sprintf` are exported by neither (UCRT's are header inline over `__stdio_common_vfprintf`); `atexit` only by `msvcrt.dll` |
| `*-windows-gnu-msil` | `-lmingw32 -lgcc -lgcc_eh -lmoldname -lmingwex -lmsvcrt -ladvapi32 -lshell32 -luser32 -lkernel32` | `-lmsvcrt` (or the user's `-lucrt`/`-lmsvcr*`/`-lcrtdll`, as `MinGW.cpp:91-100`) `-ladvapi32 -lshell32 -luser32 -lkernel32` | MinGW import archives (not installed here) |

Why `-lm` by default on glibc: the backend itself calls libm for operations that are instructions natively
(`__builtin_sqrt`/`floor`/`fma` with `-fno-math-errno` compile to `call float64 'sqrt'` etc. with
`pinvokeimpl("libm.so.6")`, measured), and msil-ld creates P/Invokes only for referenced symbols, so an unused
library costs nothing (unlike a native `DT_NEEDED`).

Why `kernel32` by default on Windows: native programs get it from the CRT's own `/DEFAULTLIB` directives; C code that
includes `windows.h` calls kernel32 freely.

Requirements this puts on msil-ld (P2-B/P2-C, native-imports): (a) during `-l` search skip an archive that has no
CIL or bitcode member (lld-flavor 2.3 states the rule; the prototype does not implement it: `-L<sysroot lib> -lc -lm`
gave 2,233 "neither an MSIL object file nor LLVM bitcode" warnings and `unknown file type: libm.a`,
`logs/lc-search.log`); (b) read GNU ld scripts (`GROUP`, `INPUT`, `AS_NEEDED`, `OUTPUT_FORMAT` ignored), with absolute
paths resolved under `--sysroot`, ignoring native static members such as `libc_nonshared.a`; (c) read COFF short
import libraries (`.lib`) and MinGW import archives; (d) native *data* symbols need an address import
(`dlsym`/`NativeLibrary.GetExport` at startup), because a P/Invoke cannot bind `stdout`. An alternative to (b) on
glibc is that the driver passes `-l:libc.so.6 -l:libm.so.6` (the sonames are part of the triple's ABI and need only
the runtime package), but users pass `-lm` themselves, so msil-ld needs (b) anyway.

`#pragma comment(lib, "user32")` on windows-msvc-msil produces `!llvm.linker.options = !{!"/DEFAULTLIB:user32.lib"}`
today, which the backend ignores silently (no handler in `llvm/lib/Target/MSIL`); on the Linux and MinGW MSIL triples
clang emits nothing. Recommend that P2-A objects carry the module's linker options and msil-ld honours
`/DEFAULTLIB:` (as lld-link) so Windows code that names its import libraries links without `-l`.

### 4.6 References and the export class

- The driver passes no reference by default; msil-ld's built-in `mscorlib` table covers the no-reference case
  (bcl-binding 5.10). There is no target-framework or core-library option (user decision 4).
- `-Wl,--reference=<path>` (file or directory) and positional `.dll` inputs: an unknown extension is `TY_Object`
  (`Driver.cpp:3253-3265`), so `lib.dll` reaches msil-ld in command-line order (measured). `-r` stays lld's
  `--relocatable` (clang forwards it). A dedicated driver spelling can come later; it would touch `Options.td`
  (`Options.inc` has 80 dependents, mc-streamer 6).
- `-shared` -> `--shared`; `-Wl,--export-class=Contoso.Zlib.Native` (Design.md 5.4). Nothing reaches cc1: visibility
  already decides what is exported, and there is no PIC.
- The assembly name: msil-ld's `--assembly-name`, default the output stem (lld-flavor 2.4), so `-o foo.dll` gives
  assembly `foo`, which the CLR binder needs to find `foo.dll` by name. The Phase-1 `-mllvm -msil-assembly-name` is a
  backend `cl::opt` (`MSILAsmPrinter.cpp:94`); under LTO it only works through forwarding (4.8).

### 4.7 Output naming and PE kind

Experiments (`scripts/naming-win.ps1`, `scripts/naming-wsl.sh`, `logs/naming-*.log`): one hello program assembled by
ilasm as an EXE image (`-exe`, Characteristics `0x0022`) and as a DLL image (`-dll`, `0x2022`), copied under four
names, run with `dotnet exec --runtimeconfig` and with `dotnet <file>` plus a `<stem>.runtimeconfig.json`:

| Image | Name | .NET 8 Windows | .NET 10 WSL (and .NET 8 WSL via `dotnet <file>`) | .NET Framework 4.8 (start the file) |
|---|---|---|---|---|
| EXE | `p.exe` | runs (exit 42) | runs | runs |
| EXE | `p.dll` | runs | runs | n/a |
| DLL | `p.exe` | runs | runs | **fails**: "not a valid application for this OS platform" (Win32 error 193) |
| DLL | `p.dll` | runs | runs | n/a |
| either | `a.out`, `prog` | **fails**: "dotnet exec needs a managed .dll or .exe extension" (0x80008081; `dotnet ./a.out`: "Could not execute because the specified command or file was not found") | **fails** the same way (exit 129 = 0x81) | n/a |

So: (1) msil-ld writes an EXE image (no `IMAGE_FILE_DLL`, an entry point) for every program and a DLL image for
`--shared`, independent of the name, like csc `/t:exe` vs `/t:library`; the program then runs on modern .NET under
either extension and on Framework as `.exe` (one output for every runtime, unlike Phase 1's separate `-dll` and
`-exe` ilasm runs). (2) The driver's default image name must not be `a.out`: `Driver::getDefaultImageName`
(`Driver.cpp:5460-5463`, triple-only, no arguments) returns `a.exe` for MSIL triples in the prototype (OS-independent,
which also suits FeatureIdeas 7). `-shared` without `-o` then also gives `a.exe`, as native `-shared` gives
`a.out`/`a.exe`; build systems always pass `-o`. (3) The driver never appends an extension to `-o` (clang-cl's
`/Fe` and `/LD` naming already add `.exe`/`.dll`, `Driver.cpp:5737-5750`). (4) msil-ld can warn when an output ends
in neither `.exe` nor `.dll`. (5) An apphost (native launcher) is the build system's business, like the
runtimeconfig.

Also measured: a P/Invoke to `api-ms-win-crt-stdio-l1-1-0` (`puts`) runs on .NET 8, .NET 10 and Framework 4.8 on
Windows 11, so import-library DLL names are usable as they are.

### 4.8 LTO

- Compile: unchanged (`-flto` -> `-emit-llvm-bc -flto=full|thin`, msil-toolchain.c 200-204).
- Link: `tools::addLTOOptions` must not be used: for a linker that is not named `lld`/`lld-link`/`ld.lld`
  (`CommonArgs.cpp:1078-1081`) it pushes `-plugin <clang dir>/../lib/LLVMgold.dll`, then `-plugin-opt=` options that
  lld/wasm (and the prototype msil-ld) do not accept. wasm does not call it either. The prototype's
  `addMSILLTOOptions`: `-O<n>` -> `--lto-O<n>` (via `getOptimizationLevel`), `-flto-jobs=N` -> `--thinlto-jobs=N`
  (`getLTOParallelism`), `-flto-partitions=N` -> `--lto-partitions=N`, `-save-temps` -> `--save-temps`, and each
  `-mllvm X` -> `--mllvm=X`, because the MSIL backend runs inside msil-ld under LTO (HIP forwards `-mllvm` to its
  device lld as `-plugin-opt=`, `HIPAMD.cpp:94-97`; native toolchains do not forward it).
- `Driver.cpp:3451-3455` reports `LTO requires -fuse-ld=lld` (`err_drv_lto_without_lld`) for every windows-msvc LTO
  link that does not say `-fuse-ld=lld`; the prototype exempts MSIL triples (one line). CMake hit exactly this
  before the exemption.
- Codegen switches (P3-A) should be module flags or function attributes so that LTO sees them without driver help;
  `-mllvm` forwarding is a debugging aid, not the interface. The same goes for anything a build system sets per TU.
- Stock-clang native-triple bitcode is msil-ld's business (lld-flavor 3.3); the driver passes `.bc`/`.o` inputs
  through (`HasNativeLLVMSupport`).

### 4.9 Finding and choosing the linker

- `GetProgramPath("msil-ld")` searches `-B` prefixes, then `<--target string>-msil-ld` and `msil-ld` in the program
  paths (the driver's directory: `Gnu.cpp:3053`, `MinGW.cpp:528`, `MSVC.cpp:534`) and `PATH`
  (`Driver.cpp:5930-5986`). lld's `parseProgname` picks the first `-`-separated component that names a flavor
  (`lld/Common/DriverDispatcher.cpp:31-38, 83-95`), so a copy named `x86_64-unknown-linux-gnu-msil-msil-ld` would run
  the ELF flavor ("gnu"); lld-flavor's optional msil-first rule in `parseProgname` removes the hazard.
- `-fuse-ld=lld`, `-fuse-ld=ld` and `-fuse-ld=msil-ld` select msil-ld; an absolute path is used as is; anything else
  (`bfd`, `gold`, `lld-link`) is `invalid linker name in argument` (measured). `--ld-path=` works as for every other
  toolchain. `CLANG_DEFAULT_LINKER` (`getPreferredLinker()`) is ignored for MSIL.
- Alternative: run the multi-flavor `lld` with `-flavor msil` first on the command line, as HIP runs `lld -flavor gnu`
  (`HIPAMD.cpp:58, 135`). On Windows lld copies its 67 MB executable for every alias (lld-flavor 1.4), so
  `lld -flavor msil` saves the `msil-ld.exe` copy; recommended only as a fallback when `msil-ld` is not found.

### 4.10 `-c` and the integrated assembler (P2-A)

- Today `clang -c` already plans one `cc1 -emit-obj` job (compile+backend+assemble collapsed:
  `canCollapseAssembleAction`, `Driver.cpp:4880-4886`, needs `useIntegratedAs()`, no `-save-temps`, no
  `-via-file-asm`, `/FA`, `/Fa`); only `Clang.cpp:5540-5543` stops it. With the MSIL MC object streamer, code emitter
  and asm backend of the mc-streamer recon (and the `Triple::MSIL` case in `MCAsmBackend::createObjectWriter`,
  `MCAsmBackend.cpp:60-61`), deleting those four lines makes `clang -c` write CIL objects; nothing else in the driver
  changes. `IsIntegratedAssemblerDefault()` stays true.
- `-fno-integrated-as -c`: `SelectTool` (`ToolChain.cpp:1298-1306`) uses `getAssemble()` -> `buildAssembler()` ->
  `err_no_external_assembler` (the MSIL override). With that override, both `ToolChain.cpp` hooks (getAssemble,
  getLink) and the `getLinkerWrapper` null check can go: the fork's diff in `ToolChain.cpp` shrinks to the KCFI line.
- Assembling ILAsm text (`-c x.s`, `-c x.il` with `-x assembler`, `-save-temps -c`, `-via-file-asm`, `/FA`): integrated
  `ClangAs` (`getClangAs()`), so `Clang.cpp:9340-9342` stays, reworded ("assembling ILAsm text is not supported for
  MSIL; compile C sources with -c, or use -S") until an ILAsm parser exists (mc-streamer R7). Making `-save-temps`
  work would need either the parser or a special case in `canCollapseAssembleAction` that keeps collapsing for MSIL and
  writes the `.il` as a side output; defer.
- `.il` given to a link is `TY_Object` today and goes to msil-ld (measured); msil-ld reports an unknown file type. A
  `LookupTypeForExtension("il")` override is optional.
- `-fembed-bitcode` stays an error unless the P2-A container gets a section for it.

### 4.11 Static libraries and the helper archive

- `--emit-static-lib` -> `llvm-ar rcsD <out> <objects>` on every MSIL triple (measured for linux-gnu and
  windows-msvc). CMake uses `CMAKE_AR` (`llvm-ar qc` + `llvm-ranlib`); lld/wasm-style linkers ignore the archive symbol
  index (lld-flavor 2.3), so `llvm-ar` needs no MSIL object reader to produce usable archives.
- The private C helpers of Design.md 6 (P3-A, "linked statically like compiler-rt builtins") have a ready place: the
  per-target runtime directory uses the full MSIL triple, so `--rtlib=compiler-rt -print-libgcc-file-name` prints
  `<resource>/lib/x86_64-unknown-linux-gnu-msil/libclang_rt.builtins.a` and
  `<resource>/lib/x86_64-pc-windows-msvc-msil/clang_rt.builtins.lib` (measured) and never a native archive. The
  prototype appends it after the default libraries when the file exists (and `AddFilePathLibArgs` then also adds that
  directory as `-L`), which answers inputs.md's open question "how the static C helper archive reaches msil-ld". One
  naming for all MSIL triples (`libclang_rt.builtins.a`, the archive format being the same) would need a
  `buildCompilerRTBasename` override; optional.

### 4.12 Smaller points

- clang-cl: `/c` gives `.obj` objects, `/Fe:` and `/LD` name the output, `-flto` links through msil-ld (measured).
  `/LD`/`/LDd` must map to `--shared` (prototype gap). Default `/MT` passes `-D_MT --dependent-lib=libcmt
  --dependent-lib=oldnames`, `/MD` adds `-D_DLL` and `msvcrt`: for MSIL the CRT is always the DLL one, so `/MD`
  semantics (`_DLL`, `_ACRTIMP` = dllimport) match reality better; the dependent-lib names would need a mapping
  (`msvcrt`/`libcmt` -> `ucrt`+`vcruntime`) if msil-ld honours `/DEFAULTLIB:` (4.5). Decide with P2-C.
- `-pthread`: `-lpthread` on Linux, as natively (the 2.43 sysroot still ships `libpthread.so.0` and `libpthread.a`; that glibc 2.34+ moved the functions into `libc.so.6` is *unverified* here).
- `-e <sym>` is forwarded by `AddLinkerInputs` (the prototype's first version passed it twice; fixed).
- `-mrelocation-model static` on every MSIL triple also means no `PIC Level`/`PIE Level` module flags; the backend
  forced static anyway (D8), and the corpus IL did not change.

### 4.13 Upstream diff and rebuild cost

| File | Change | Note |
|---|---|---|
| `clang/lib/Driver/ToolChains/MSIL.{h,cpp}` | new (fork-only) | about 100 + 300 lines with `/LD`, `GetDefaultRuntimeLibType`, vcruntime |
| `clang/lib/Driver/CMakeLists.txt` | add `ToolChains/MSIL.cpp` next to `ToolChains/WebAssembly.cpp` (line 96) | triggers a CMake regeneration |
| `clang/lib/Driver/Driver.cpp` | include; `createMSILToolChain` at the top of `getToolChain`; `!isOSBinFormatMSIL()` in the `err_drv_lto_without_lld` test; MSIL case in `getDefaultImageName` | `proto/Driver.cpp.diff` (4 hunks, 6 added lines) |
| `clang/lib/Driver/ToolChain.cpp` | remove the `getLink` hook (prototype) and the `getAssemble` hook (with P2-A) | `proto/ToolChain.cpp.diff` |
| `clang/lib/Driver/ToolChains/Clang.cpp` | P2-A: remove 5540-5543, reword 9340-9342 | |
| `clang/include/clang/Basic/DiagnosticDriverKinds.td` | P2-A: reword `err_drv_msil_no_object_output` | 84 dependents, 2.6% of compile CPU (mc-streamer 6) |

Measured with the shared build's flags and PCH: `MSIL.cpp` 2.0-2.4 s, `Driver.cpp` 8.8-14.0 s, `ToolChain.cpp`
5.1-6.1 s of compile, `clang.exe` link 2.8-3.2 s (lld-link). No widely-included header changes; the driver work needs
no front-loading into a "P2-0" milestone except the diagnostic text if P2-A rewords it there.
`msil/utils/upstream-hooks.txt` gains `hook clang/lib/Driver/Driver.cpp` (upstream tests: `cl-options.c` for
`err_drv_lto_without_lld`, `linux-ld-args.c`/`gcc_forward.c` for the default image name, `msvc-link.c`,
`mingw.cpp`/`linux-ld.c` for toolchain selection; pick the fast ones) and the new `fork` test lines; the
`ToolChain.cpp` block shrinks.

## 5. Headers and libc for Phase 2

### 5.1 A glibc sysroot on this host

WSL Ubuntu has `libc.so.6` but no headers and no C compiler (Status.md; installing `libc6-dev` needs the user's sudo
password). Without root: `apt-get download libc6-dev linux-libc-dev libc6 libgcc-s1` (6 MB of `.deb`s, about 2 s),
`dpkg-deb -x` into `/tmp`, rewrite absolute symlinks to sysroot-relative ones, `cp -rL` to
`C:\llvm-msil-scratch\p2\driver-toolchain\sysroot-glibc` (30 MB; `scripts/mk-sysroot.sh`). Ubuntu 26.04 is merged-usr:
everything is under `usr/` (`usr/lib/x86_64-linux-gnu`, `usr/lib64/ld-linux-x86-64.so.2`). Eight Linux netfilter
headers that differ only in case (`xt_CONNMARK.h`/`xt_connmark.h`, ...) collide on NTFS and lose one copy each;
irrelevant for C programs (a case-sensitive directory, `fsutil file setCaseSensitiveInfo`, would avoid it).
`clang --target=x86_64-unknown-linux-gnu-msil --sysroot=<that dir>` then searches
`<S>/usr/include/x86_64-linux-gnu` and `<S>/usr/include`, and links against `<S>/usr/lib/x86_64-linux-gnu`.
On the GitHub Linux runner (and any Linux host with `libc6-dev`) no sysroot is needed.

### 5.2 Header survey (`scripts/header-survey.sh`, `logs/header-survey.log`)

| Triple, configuration | Headers | Clean |
|---|---|---|
| linux-gnu-msil, glibc 2.43 sysroot | 29 C (`assert` ... `wctype`, including `threads.h`, `stdatomic.h`, `complex.h`, `fenv.h`, `tgmath.h`, `uchar.h`) + 9 POSIX (`unistd`, `pthread`, `fcntl`, `sys/stat`, `sys/types`, `sys/time`, `dirent`, `dlfcn`, `sys/mman`) | 38 / 38 (native triple: 38 / 38) |
| windows-msvc-msil, VC 14.51.36231 + SDK 10.0.26100 | 29 C + `windows.h`, `io.h`, `process.h`, `direct.h`, `conio.h`, `malloc.h` | 34 / 35: `tgmath.h` fails (`passing '_Complex float' to parameter of incompatible type '_Fcomplex'`), identically for the native triple |
| both, `-ffreestanding`, nothing installed | `stdint`, `stddef`, `stdarg`, `float`, `limits`, `stdbool`, `iso646`, `stdalign`, `stdnoreturn` | 9 / 9 |
| windows-msvc-msil, SDK root only (no VC) | `stdio.h` | `'stdio.h' file not found`: the UCRT/SDK include dirs need a VC toolchain (`MSVC.cpp:878`), and UCRT's `corecrt.h` includes `vcruntime.h` |

### 5.3 Hosted codegen (`scripts/hosted-probe.sh`, `logs/hosted-probe.log`)

`hello.c` (`stdio.h`, `string.h`, `math.h`; `puts`, `strlen`, `sqrt`) and `libcuse.c` (`malloc`, `strcpy`, `isalpha`,
`toupper`, `printf`, `fputs(stdout)`, `errno`, `getchar`, `free`) through the Phase-1 backend:

| Case | Result |
|---|---|
| hello, glibc, -O0 and -O2 | compiles; ran on WSL .NET 10 (`puts` -> `"libc"`, `sqrt` -> `"libm.so.6"`) |
| hello, UCRT, -O2 | compiles |
| hello, UCRT, -O0 | 3 errors: UCRT `stdio.h` defines `sprintf`, `_snprintf` (variadic) and helpers as `linkonce_odr` in every TU at -O0 (native clang does the same), and MSILPrepareIR diagnoses every surviving definition (`MSILPrepareIR.cpp:540-545`). After `opt -passes=globaldce` (8 definitions -> 1) the program compiles, assembles and runs on Framework 4.8 |
| libcuse, glibc | Phase-1 import errors for `malloc`, `free`, `strcpy`, `fputs`, `getchar`/`getc`, `toupper`/`__ctype_toupper_loc`, `__ctype_b_loc`, `__errno_location` (all msil-ld's in Phase 2), `printf` (variadic call: Phase 3) and the data symbols `stdout` and, at -O2, `stdin` (`getchar()` is `getc(stdin)` under `__USE_EXTERN_INLINES`): an address import, not a P/Invoke |
| libcuse, UCRT | the same functions (UCRT's `errno` is `_errno()`, `stdout` is `__acrt_iob_func(1)`: no data imports), `printf` is a variadic *definition* in the header over `__stdio_common_vfprintf` (Phase 3; once variadic definitions work, UCRT printf needs only that non-variadic import) |

### 5.4 Recommendation

- Phase 2 execution tests stay freestanding (`-ffreestanding`, prototypes for imported functions), exactly as Phase 1:
  they exercise objects, msil-ld and the PE writer without libc variables.
- Driver behaviour (header search, library directories, default libraries) is tested with `-###` and fake trees, so
  no host installation is needed (section 6).
- Optional hosted compile-only leg in `msil/test` (for example `M2-hosted/`), gated by lit features
  `msil-glibc-sysroot` (a `MSIL_GLIBC_SYSROOT` directory, made by a tracked copy of `mk-sysroot.sh`, or `/` on Linux
  CI) and `msil-ucrt-headers` (lit passes `INCLUDE` and `LIB` on Windows, `llvm/utils/lit/lit/TestingConfig.py:79-82`;
  the gate already runs inside `msil-env.ps1`): `-S` hosted programs that use only function imports, linked by
  msil-ld once P2-C resolves native libraries, and run.
- Backend follow-up for P2 (small, independent): MSILPrepareIR erases definitions that are
  `isDiscardableIfUnused()` and unreferenced before diagnosing them (unblocks UCRT `stdio.h` at -O0; legal because a
  `linkonce_odr` definition may be dropped by any TU that does not use it).
- libc proper stays in Phase 3: variadic definitions and calls (UCRT printf family, glibc printf via v-variants),
  native data imports (`stdin`, `stdout`, `stderr`, `environ`, `optarg`, `timezone`, ...), `errno`, `atexit` (glibc:
  only in `libc_nonshared.a`; UCRT: not exported), `setjmp`, threads.
- Users without a developer prompt use `-Xmicrosoft-windows-sys-root`/`/winsysroot` or the separate
  `-Xmicrosoft-visualc-tools-root`/`-Xmicrosoft-windows-sdk-root`; document in GettingStarted. Investigating why VS 2026
  is not found through the Setup Configuration API is an upstream issue (VS's own clang 22.1.3 behaves the same).

## 6. Driver tests to add

Three proposed files were run through real lit (`llvm/utils/lit/lit.py`, internal shell) against the prototype clang
with a scratch `lit.cfg.py` that maps `%clang`/`%clang_cl` and uses `FileCheck`, `not` and `split-file` from the shared
build; `Inputs` is a junction to `clang/test/Driver/Inputs`. All pass, both from a plain shell and inside the VS
developer environment (`INCLUDE`/`LIB` set). Full texts: [driver-toolchain-files/tests/](driver-toolchain-files/tests/).

1. **`clang/test/Driver/msil-link.c`** (P2-B, 21 RUN commands including the `split-file` setup): the link job per triple.
   - linux-gnu with `--sysroot=%S/Inputs/debian_multiarch_tree`: `msil-ld --target=... --sysroot=... -L<S>/usr/lib/x86_64-linux-gnu ... -lc -lm -o a.exe`,
     with `--implicit-check-not` for `crt1.o`, `crti.o`, `crtbegin`, `crtn.o`, `-dynamic-linker`, `"-pie"`, `-plugin`,
     `LLVMgold`, `ld.lld`, `"ld"`; musl (`-lc` only); aarch64.
   - windows-msvc with a `split-file` `/winsysroot` tree (`VC/Tools/MSVC/27.1828.18284/include/vcruntime.h`,
     `Windows Kits/10/Include/10.0.19041.0/ucrt/stdio.h`, the `cl-sysroot.cpp` pattern): `-L` VC lib, UCRT lib, SDK
     um lib, `-lucrt -lkernel32`, no `link.exe`/`lld-link`/`-defaultlib`, no `err_drv_lto_without_lld`; clang-cl with
     `/winsysroot` and `/Fe:prog.exe`.
   - MinGW default list; `-lucrt` replaces `-lmsvcrt`.
   - `-nostdlib`, `-nodefaultlibs`, `-pthread`.
   - `-shared` with `.o`, `.dll`, `-L`, `-l`, `-Wl,--export-class=`, `-Wl,--reference=`, `-e`, `-u` in order.
   - `-fuse-ld=lld` (Linux and MSVC), `-fuse-ld=bfd` error, `-static` error, `-fsanitize=address` error, UBSan trap
     mode accepted, `-pie -rdynamic` silent, `--emit-static-lib` -> `llvm-ar rcsD`.
   - Add when implemented: clang-cl `/LD` -> `--shared`; `-lvcruntime` (decision 9.1); `--ld-path=`.
2. **`clang/test/Driver/msil-lto.c`** (P2-B, 7 RUN commands): `-flto -O2` -> cc1 `-emit-llvm-bc -flto=full` and
   `--lto-O2`; `-flto=thin -flto-jobs=4 -O3` -> `--lto-O3 --thinlto-jobs=4`; `-mllvm -msil-assembly-name=foo` reaches
   cc1 and `--mllvm=`; `-flto-partitions=2 -save-temps` -> `--lto-partitions=2 --save-temps`; windows-msvc LTO
   without `-fuse-ld`; a bitcode `.o` input goes to msil-ld without a cc1 job. Every line has
   `--implicit-check-not=-plugin`/`LLVMgold`.
3. **`clang/test/Driver/msil-header-search.c`** (P2-B, 7 RUN commands including the setup): Linux sysroot multiarch include dirs and
   `-mrelocation-model static` without `-pic-is-pie`/`-funwind-tables`; windows-msvc `/winsysroot` include dirs with the
   versioned triple and no `-fms-volatile`; `-fno-pic` on windows-msvc stays static; `-ffreestanding -nostdlibinc`;
   `--rtlib=compiler-rt -print-libgcc-file-name` with a `split-file` resource dir; the helper archive after `-lc -lm`
   on the link line. (The `GetDefaultRuntimeLibType` override lets the last two drop `--rtlib=compiler-rt`.)
4. **`clang/test/Driver/msil-object.c`** (with P2-A; `tests/msil-object.c.proposed`, not runnable before P2-A): `-c`
   is one `cc1 -emit-obj` job on the four Linux/Windows triple families, no `-cc1as`/`as` job; default names `foo.o`
   and (clang-cl) `foo.obj`; a plain link compiles to objects and runs msil-ld without `-flto`;
   `-fno-integrated-as -c` -> `there is no external assembler that can be used on this platform`; `-c -x assembler`
   and `-save-temps=obj -c` -> the reworded MSIL error; `-S` unchanged.
5. **`clang/test/Driver/msil-toolchain.c`** (existing): P2-B removes the LTO-link block (lines 152-184: 8 RUN
   commands; verified: with them removed the file passes against the prototype) and points to `msil-lto.c`; P2-A moves
   the object block (112-150) into `msil-object.c`, inverted. Lines 12-110 and 186-329 stay valid. Its header comment
   ("Object files are not supported until phase 2") changes with them.

None of these needs `REQUIRES: msil-registered-target` (all `-###`, except one `-c -emit-llvm`/`-flto -c` compile in
`msil-lto.c`, which runs only the frontend). They need no host SDK, no sysroot and no `msil-ld` binary: the
expected program name is matched as `"{{[^"]*}}msil-ld{{(\.exe)?}}"`.

## 7. Build systems: CMake

Compiler tools only: the files below are a sketch for build-system integrators, tested in scratch and **not** proposed
for the repository. Full texts in [driver-toolchain-files/cmake/](driver-toolchain-files/cmake/); runs in
`logs/cmake-windows.log` and `logs/cmake-linux.log` (VS-bundled CMake 4.3.1-msvc1, Ninja 1.13.2; configure 4-5 s).

Toolchain file (`msil-x64-windows.cmake`; the Linux one differs in the triple and adds
`set(CMAKE_SYSROOT "$ENV{MSIL_GLIBC_SYSROOT}")`):

```cmake
set(CMAKE_SYSTEM_NAME MSIL)                  # Platform/MSIL.cmake, not Windows-Clang or Linux
set(CMAKE_SYSTEM_PROCESSOR x86_64)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/modules")
set(MSIL_TRIPLE x86_64-pc-windows-msvc-msil CACHE STRING "MSIL target triple")
set(CMAKE_C_COMPILER "${MSIL_LLVM_BIN}/clang.exe")
set(CMAKE_C_COMPILER_TARGET ${MSIL_TRIPLE})
set(CMAKE_AR "${MSIL_AR}" CACHE FILEPATH "llvm-ar")
set(CMAKE_C_FLAGS_INIT "--target=${MSIL_TRIPLE} -flto")   # -flto only until P2-A objects exist
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MSIL_LLVM_BIN MSIL_AR)
```

Platform module (`modules/Platform/MSIL.cmake`): `CMAKE_EXECUTABLE_SUFFIX .exe`; shared libraries `""` + `.dll`
(the library assembly is also what other programs link against: no import library); static libraries `lib` + `.a`;
`TARGET_SUPPORTS_SHARED_LIBS TRUE` (CMake's `Generic` platform turns shared libraries into static ones); no `-fPIC`,
no rpath, `-shared`. `modules/Platform/MSIL-Clang-C.cmake`: `include(Compiler/GNU)` + `__compiler_gnu(C)`,
`CMAKE_C_COMPILE_OPTIONS_TARGET "--target="`, `CMAKE_C_LINKER_WRAPPER_FLAG "-Xlinker" " "`, PIC/PIE options empty, IPO
`-flto`. The demo project: `add_library(util STATIC)`, `add_executable(app)` linking it,
`add_library(shlib SHARED)` with `target_link_options(shlib PRIVATE "-Wl,--export-class=Demo.Native")`, and
`file(GENERATE OUTPUT app.runtimeconfig.json ...)`: the build system writes the runtimeconfig, the compiler does not.

Results (with the prototype clang and the stand-in msil-ld):

| | windows-msvc-msil (VS dev shell) | linux-gnu-msil (glibc sysroot) |
|---|---|---|
| Compiler identification | Clang 24.0.0, `CMAKE_C_SIMULATE_ID MSVC`, frontend GNU | Clang 24.0.0, no simulate ID |
| Implicit link libraries/dirs (parsed from the msil-ld line of `clang -v`) | `ucrt;kernel32`; the five `LIB` directories | `c;m`; the three sysroot lib dirs |
| Build | 6 steps: 3 compiles, `llvm-ar qc` + `llvm-ranlib`, `shlib.dll` (`--shared --export-class=Demo.Native`), `app.exe` with `libutil.a` | the same |
| Run | `dotnet app.exe` (.NET 8, CMake's runtimeconfig): exit 42; `app.exe` on Framework 4.8: exit 42 | `wsl dotnet app.exe` (.NET 10): exit 42 |

Findings:

1. For windows-msvc triples clang predefines `_MSC_VER`, so CMake records `CMAKE_C_SIMULATE_ID=MSVC`, and
   `Modules/Compiler/Clang.cmake` (lines 18-24) then defines an empty `__compiler_clang` macro: no GNU rules and no
   `--target=` spelling, which normally come from `Platform/Windows-Clang.cmake`. Without `MSIL-Clang-C.cmake` the
   first configure compiled without `--target` and linked for the host. The module must therefore load the GNU rules
   itself (only when `CMAKE_C_SIMULATE_ID` is `MSVC`: for the Linux triple the run showed `-O3 -DNDEBUG` twice because
   `__compiler_gnu` ran twice).
2. Without the MSIL exemption in `Driver.cpp`, CMake's try-compile link failed with `LTO requires -fuse-ld=lld`.
3. With `-flto` in `CMAKE_C_FLAGS_INIT`, CMake's `-c` identification attempts produce bitcode in which it cannot find
   its `INFO:compiler[...]` strings ("The C compiler identification could not be found in CMakeCCompilerId.o"); the
   identification succeeds through the linked `a.exe` (ilasm writes the data image raw). P2-A objects keep data
   segments raw too, so identification by `-c` will work then; until then `--target` and `-flto` in the initial
   flags are what make it work.
4. References in CMake: `target_link_options(app PRIVATE "LINKER:--reference=${REFS}")` expands through
   `CMAKE_C_LINKER_WRAPPER_FLAG` to `-Xlinker --reference=...`.
5. CMake's IPO support (`CMAKE_INTERPROCEDURAL_OPTIMIZATION`) works through the module's `CMAKE_C_COMPILE_OPTIONS_IPO`
   once CIL objects exist; static libraries of bitcode need `llvm-ar`, which an LLVM toolchain release ships next to
   clang (CMake's `Clang-FindBinUtils` looks there).
6. MSBuild and other build systems would invoke `clang` the same way (compile with `--target`, link through the
   driver, write the runtimeconfig themselves); nothing in the compiler depends on CMake.

## 8. FeatureIdeas.md: what a driver choice could preclude (flagged, not planned)

- **Idea 7 (several triples in one assembly).** The toolchain is per triple and the link job states one `--target`;
  a multi-triple build would come from an offload-style driver mode (like `-arch x86_64 -arch arm64` or CUDA/HIP) with
  one compile per triple and one msil-ld run. Two choices keep that open: the default image name is triple-independent
  (`a.exe`), and per-triple default libraries must stay attributable to their triple (msil-ld would need library
  groups per variant); do not hard-code a single libc set into msil-ld itself.
- **Idea 1 (class placement).** `-Wl,--export-class=` now; a mapping file is another msil-ld option; an attribute is
  frontend work. Nothing in the driver precludes it.
- **Idea 2 (feature flags).** Keep `-mmsil-*`/`-fmsil-*` free for these; carry them as module flags so LTO and objects
  see them (R8).
- **Idea 6 (enclosed model).** An enclosed build links an in-enclosure libc, so it needs `-nostdlib` semantics or its
  own triple; `createMSILToolChain` is the single place where a future `msil64-unknown-dotnet` (Design.md 3, deferred)
  would get a toolchain that wraps no native one. Do not let the default libraries become msil-ld defaults.

## 9. Risks and open questions

1. **windows-msvc default C library set (decision for the user/planner).** (a) `-lucrt -lvcruntime -lkernel32` from
   the SDK and VC import libraries: native `/MD` parity, cross-host (works from Linux with an xwin-style
   `/winsysroot`), P/Invokes to `api-ms-win-crt-*-l1-1-0` and `VCRUNTIME140.dll` (the VC++ redistributable must be
   present at run time, as for native `/MD` programs). (b) `ucrtbase` only (Phase-1 D14 parity): one Windows 10+
   system DLL exports every CRT function the corpus needs, no redistributable, but no SDK import library describes it
   (msil-ld would read `ucrtbase.dll` from the host or a toolchain-shipped symbol list). (c) (a) for resolution plus a
   msil-ld rule that rewrites `api-ms-win-crt-*`/`VCRUNTIME140` imports of functions `ucrtbase` exports to `ucrtbase`.
   Recommended: (a), revisit with P2-C.
2. **VS detection.** Outside a developer prompt clang finds no VC toolchain on this host, so hosted windows-msvc
   compiles and the VC lib directory need `INCLUDE`/`LIB` or explicit options; UCRT/SDK lib dirs are found from the
   registry. Gates run inside `msil-env.ps1`; document the explicit options.
3. **glibc static-first search and ld scripts** (4.5): without (a) and (b) in msil-ld, `-lc -lm` fails on every real
   glibc sysroot. Owner: P2-B msil-ld (and native-imports).
4. **Data imports** (`stdin`, `stdout`, `environ`): unavoidable with hosted glibc headers at -O2. Owner: P2-C or
   native-imports; until then hosted tests use function-only libc.
5. **`-mllvm` forwarding** changes behaviour for options that should apply only at compile time (they run again in the
   link's codegen). HIP does the same; acceptable, and R8 keeps real switches out of `cl::opt`s.
6. **Response-file encoding** for non-ASCII paths (`AtFileCurCP` vs `AtFileUTF8`): *unverified*; test with a
   non-ASCII scratch path when msil-ld exists.
7. **clang-cl CRT flags** (`/MT` default `-D_MT --dependent-lib=libcmt`): harmless for codegen, but if msil-ld honours
   `/DEFAULTLIB:`, `libcmt`/`msvcrt`/`oldnames` need a mapping (4.12).
8. **Hosted UCRT at -O0** needs the MSILPrepareIR fix (5.4) before any hosted Windows test can use `stdio.h` at -O0.
9. **CMake `SIMULATE_ID MSVC`** is a build-system integration fact: worth one line in GettingStarted for integrators.

## 10. Work-package ideas (disjoint files)

| WP | Owns | Content |
|---|---|---|
| P2B-drv.a | `clang/lib/Driver/ToolChains/MSIL.{h,cpp}`, `clang/lib/Driver/CMakeLists.txt` | toolchain and linker of section 4 (from `proto/`, plus `/LD`, `GetDefaultRuntimeLibType`, the decided Windows library set, `AtFileUTF8` if verified) |
| P2B-drv.b | `clang/lib/Driver/Driver.cpp`, `clang/lib/Driver/ToolChain.cpp`, `msil/utils/upstream-hooks.txt` | the four Driver.cpp hunks, removal of the getLink/getAssemble hooks, hook-list entries |
| P2B-drv.t (separate agent) | `clang/test/Driver/msil-link.c`, `msil-lto.c`, `msil-header-search.c`, the LTO block of `msil-toolchain.c` | section 6, items 1-3 and 5 |
| P2A-drv (with the object writer) | `clang/lib/Driver/ToolChains/Clang.cpp`, `DiagnosticDriverKinds.td`, `clang/test/Driver/msil-object.c`, the object block of `msil-toolchain.c` | 4.10; section 6 item 4 |
| P2-prep (backend, small) | `llvm/lib/Target/MSIL/MSILPrepareIR.cpp` + a CodeGen/MSIL test | drop unreferenced discardable definitions before diagnosing (5.4) |
| P2-hosted (optional) | `msil/utils/make-glibc-sysroot.sh`, `msil/test/M2-hosted/**`, lit features in `msil/test/lit.cfg.py` | section 5.4 |
| docs | `llvm/docs/MSIL/GettingStarted.md`, `Status.md` | linking with clang, default libraries, naming, explicit VS options, the CMake facts of section 7 |

## 11. Reproduction

All in `C:\llvm-msil-scratch\p2\driver-toolchain\`; scripts in
[driver-toolchain-files/scripts/](driver-toolchain-files/scripts/), logs in
[driver-toolchain-files/logs/](driver-toolchain-files/logs/) (scrubbed with `scrub.py`).

```text
# glibc sysroot (WSL, no root): debs/ <- apt-get download ...; then
MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash /mnt/c/llvm-msil-scratch/p2/driver-toolchain/scripts/mk-sysroot.sh
# prototype clang: copy Driver.cpp/ToolChain.cpp to proto/, apply hooks, compile 3 TUs with the shared build's
# flags (compile_commands.json, its PCH read-only) and link clang.exe from the build's libraries (VS dev shell)
python scripts/patch-driver.py proto
pwsh -c ". D:\llvm-msil\msil\utils\build\msil-env.ps1; python scripts\build-proto.py"   # 3 TUs ~20 s, link ~3 s
mklink /J proto\lib\clang\24 D:\llvm-msil\build\lib\clang\24                            # resource dir
# experiments
bash scripts/cc1-diff.sh; bash scripts/corpus-diff.sh; bash scripts/gen-hdr-probes.sh; bash scripts/header-survey.sh
bash scripts/hosted-probe.sh; bash scripts/lc-search.sh
pwsh -File scripts/naming-win.ps1; wsl -- bash .../scripts/naming-wsl.sh
# stand-in msil-ld next to the prototype clang (prototype msil-ld of the lld-flavor recon + ilasm)
clang-cl /O2 /D_CRT_SECURE_NO_WARNINGS scripts/standin-launcher.c /Fe:proto/bin/msil-ld.exe; copy msil-ld-standin.py
bash scripts/e2e.sh
pwsh -File scripts/cmake-test.ps1; pwsh -File scripts/cmake-test-lnx.ps1
# proposed driver tests through lit (tests/lit.cfg.py; Inputs = junction to clang/test/Driver/Inputs)
python D:/llvm-msil/llvm/utils/lit/lit.py -v msil-link.c msil-lto.c msil-header-search.c
```
