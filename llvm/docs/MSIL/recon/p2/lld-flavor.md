# P2 recon: msil-ld as an lld flavor modeled on lld/wasm (key `lld-flavor`)

Date: 2026-10-20. Branch `msil` at `1ea64fe9fc7c`. Scratch: `C:\llvm-msil-scratch\p2\lld-flavor\`
(about 200 MB; nothing large was written to D:, which had 19 GB free at the end).
Supporting files: [lld-flavor-files/](lld-flavor-files/) (prototype sources, scripts, logs).
No tracked file was changed. No tool was installed (the VS-bundled `llvm-ar`/`llvm-nm`, the
.NET SDKs and WSL .NET 10 were already present). Every path and line number below was read
in this checkout; anything not verified is marked *unverified*.

## 0. Summary

- **Registration is small and verified.** A flavor needs 2 lines in `lld/Common/DriverDispatcher.cpp`
  (`getFlavor`), 1 enum value in `lld/include/lld/Common/Driver.h`, the driver entry
  `lld::msil::link(...)`, and CMake glue. A prototype `msil-ld` built in scratch dispatches by
  `argv[0]` (`msil-ld`, `x86_64-pc-windows-msvc-msil-ld`) and by `lld -flavor msil`.
  One hazard: a triple-prefixed name that contains `gnu` before `msil`
  (`x86_64-unknown-linux-gnu-msil-ld`) dispatches to the ELF flavor (measured), because
  `parseProgname` takes the first component that names a flavor (section 1.3).
- **lld/wasm is 12,435 lines in 31 files.** About a third carries over almost unchanged
  (driver skeleton, option table helpers, archives and lazy symbols, LTO, map file, mark-live,
  `--reproduce`, response files, the diagnostics). The rest is wasm-specific (memory, tables,
  globals, tags, PIC, LEB relocations, wasm sections) and is replaced by the CIL object reader
  and the metadata/PE writer (section 2.2 has the per-file COPY/ADAPT/DROP/DEFER table).
  lld/Common is reused unchanged except the dispatcher.
- **LTO works through the MSIL TargetMachine today, and retargeting stock bitcode works.**
  `lto::Config::OverrideTriple` (`llvm/include/llvm/LTO/Config.h:131`, applied in
  `llvm/lib/LTO/LTOBackend.cpp:575-588`) moves native-triple bitcode onto the `-msil` triple.
  DataLayout, `target-cpu` and `target-features` are byte-identical between `<native>` and
  `<native>-msil` on both x86_64 triples. The prototype linked stock-clang bitcode, MSIL-clang
  bitcode, a lazy archive member and P/Invokes resolved from `ucrtbase.dll`'s export table into
  one program that runs on .NET 8. It also ran the whole msil/test/M1 C corpus through
  msil-ld LTO: 93 of 93 programs pass at -O2 and at -O0.
- **Stock-clang bitcode has real gaps (measured on the 93-program corpus).**
  - windows-msvc: clang's default `-fms-volatile` makes `volatile` accesses atomic, and 55
    programs fail on Phase 1's missing atomics. With `-fno-ms-volatile`, 91 of 93 pass.
  - Every triple: the empty `asm volatile("" ::: "memory")` barrier and K&R calls fail.
  - x86_64 Linux: unprototyped calls are variadic. 89 of 93 compile, and all 89 pass under
    WSL .NET 10.
  - `long double` is `x86_fp80` in stock Linux and MinGW bitcode. This is a silent struct-layout
    mismatch risk when stock and MSIL bitcode are mixed.
  - Stock bitcode is vectorized with the X86 TTI, so its IL is 27% larger (202,856 against
    159,549 IL instructions over the corpus).
  - The IR linker warns "Linking two modules of different target triples" for every mix,
    because `Triple::isCompatibleWith` compares the object format
    (`llvm/lib/TargetParser/Triple.cpp:2340-2344`).
- **LTO codegen should produce the P2-A object, not feed the linker's internal model.** That
  is how lld/wasm does it (`lld/wasm/SymbolTable.cpp:112-117` parses the LTO result as an
  ordinary `ObjFile`). ThinLTO and `--lto-partitions` need it, because each produces one IL
  module per task (measured: ThinLTO inputs fail in Phase-1 codegen on cross-module globals).
  Until P2-A exists, `--lto-emit-asm` (ld.lld's spelling; `lld/ELF/LTO.cpp:169-172`) writes
  ILAsm text that ilasm turns into a runnable assembly.
- **Build cost is small.** All measured with the shared build's exact flags and PCH, outputs in
  scratch:
  - lldCommon is 13 TUs and about 13 s of CPU.
  - lld/wasm is 14 TUs and 63-88 s of CPU. All 27 take 22-31 s wall at -j4.
  - The other four flavors add 92 TUs (428 s of CPU, 108 s wall at -j4), plus 5 LLVM TUs
    (DTLTO, LibDriver, WindowsManifest).
  - Links take 1.4-3.4 s. A full `lld.exe` is 66.9 MB; an msil-only executable is 59.4 MB.
  - On Windows, `lld` copies its exe for every alias name (no symlinks), so a full `lld`
    with an `msil-ld` alias costs about 0.4 GB on D:.
  - lld's own lit config fails unless `ld.lld`, `lld-link`, `ld64.lld` and `wasm-ld` all exist
    (`llvm/utils/lit/lit/llvm/config.py:834-900`). That decides where msil-ld's tests live
    (section 5).
- **First runnable milestone (proven by the prototype):** "msil-ld links bitcode objects and
  archives, resolves native imports and writes IL" (`--lto-emit-asm`, then ilasm, then run).
  It needs neither the P2-A object format nor the PE writer, so P2-B can start in parallel
  with P2-A. The second milestone (two `clang -c` CIL objects into one assembly that runs, with
  no ilasm) depends on P2-A's reader and the metadata writer (section 6).

## 1. Flavor registration

### 1.1 How lld picks a flavor (verified)

- `lld/tools/lld/lld.cpp:69-73` declares the linked drivers (`LLD_HAS_DRIVER(coff|elf|mingw|macho|wasm)`).
  `lld_main` (75-115) calls `lld::unsafeLldMain(args, ..., LLD_ALL_DRIVERS, exitEarly)` or, under
  `LLD_IN_TEST`, `lldMain` several times. That is the re-entrancy test, so every flavor must reset
  its global state.
- `lld/include/lld/Common/Driver.h`:
  - `enum Flavor` at 16-23.
  - `LLD_HAS_DRIVER(name)` at 51-57 declares
    `bool lld::name::link(ArrayRef<const char *>, raw_ostream &, raw_ostream &, bool exitEarly, bool disableOutput)`.
  - `LLD_ALL_DRIVERS` at 61-67 is a braced `{Flavor, &link}` list.
- `lld/Common/DriverDispatcher.cpp`:
  - `getFlavor` (31-38) is a `StringSwitch` over names: `ld`/`ld.lld`/`gnu`, `wasm`/`ld-wasm`,
    `link`, `ld64`/`ld64.lld`/`darwin`.
  - `parseProgname` (83-95) splits the program name at `-` and returns the **first** component
    that `getFlavor` knows.
  - `parseFlavorWithoutMinGW` (97-125) honours `-flavor X` only as `argv[1]` and strips it,
    otherwise uses `argv[0]` without `.exe`.
  - `whichDriver` (139-150) returns a stub that fails **silently** when the flavor is not linked
    in.
  - `unsafeLldMain` (157-175) runs the driver and calls `CommonLinkerContext::destroy()`.
- `lld/tools/lld/CMakeLists.txt`:
  - `add_lld_tool(lld lld.cpp SUPPORT_PLUGINS GENERATE_DRIVER)` (6-11). `GENERATE_DRIVER` writes
    `build/tools/lld/tools/lld/lld-driver.cpp` with `main` calling `lld_main`.
  - `lld_target_link_libraries(lld PRIVATE lldCommon lldCOFF lldELF lldMachO lldMinGW lldWasm)`
    (27-35).
  - Alias names come from `LLD_SYMLINKS_TO_CREATE`, default `lld-link ld.lld ld64.lld wasm-ld`
    (37-44). In this Windows build they are **copies** (the `POST_BUILD` of `bin/lld.exe` in
    `build/build.ninja` runs `cmake -E copy` four times).
- `lld/CMakeLists.txt:198-215`: `add_subdirectory(Common)`, `tools/lld`, then one
  `add_subdirectory` per flavor.
- `lld/wasm/CMakeLists.txt` (42 lines):
  - `tablegen(LLVM Options.inc -gen-opt-parser-defs)` and `WasmOptionsTableGen`.
  - `add_lld_library(lldWasm ...)` with `LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD} BinaryFormat
    BitWriter Core Demangle LTO MC Object Option Passes ProfileData Support TargetParser`,
    `LINK_LIBS lldCommon`, and `DEPENDS WasmOptionsTableGen intrinsics_gen`.

### 1.2 The hook diff (applied to copies in scratch and built; [proto/flavor-dispatch.diff](lld-flavor-files/proto/flavor-dispatch.diff))

```diff
--- a/lld/include/lld/Common/Driver.h
   Wasm,    // -flavor wasm
+  MSIL,    // -flavor msil
--- a/lld/Common/DriverDispatcher.cpp
       .CasesLower({"wasm", "ld-wasm"}, Wasm)
+      .CasesLower({"msil", "ld-msil"}, MSIL)
-        " (WebAssembly) instead");
+        " (WebAssembly), msil-ld (MSIL) instead");
```

Hosting msil-ld inside the multi-flavor `lld` executable (recommended, 1.4) also needs these
upstream edits. They are not built in scratch, but each is the wasm line copied:

- `Driver.h` `LLD_ALL_DRIVERS`: add `{lld::MSIL, &lld::msil::link}`.
- `lld/tools/lld/lld.cpp`: add `LLD_HAS_DRIVER(msil)`, and `msil-ld` in the header comment.
- `lld/tools/lld/CMakeLists.txt`: add `lldMSIL` to `lld_target_link_libraries`, and `msil-ld`
  to the default `LLD_SYMLINKS_TO_CREATE`.
- `lld/CMakeLists.txt`: add `add_subdirectory(MSIL)`.
- `lld/unittests/AsLibAll/AllDrivers.cpp` and its CMakeLists.txt: `LLD_ALL_DRIVERS` names
  `lld::msil::link`, so the test needs `LLD_HAS_DRIVER(msil)` and `lldMSIL`, or it fails to link.
- `lld/test/lit.cfg.py:72-92`: optionally add `"MSIL": "msil"` to the `--targets-built` map,
  so `lld/test/MSIL/lit.local.cfg` can test `"msil" in config.available_features` as
  `lld/test/wasm/lit.local.cfg` does.
- msil/utils/upstream-hooks.txt: a hook entry per edited upstream file.
- msil/utils/ci/check-upstream-hooks.py: `lld/MSIL/` and `lld/test/MSIL/` as fork-only prefixes.

The upstream-hooks check (M11) requires both of the last two.

### 1.3 Measured dispatch behaviour (prototype `msil-ld.exe`, MSIL driver only)

| Invocation | Result |
|---|---|
| `msil-ld --version`, `lld -flavor msil --version`, `x86_64-msil-ld`, `x86_64-pc-windows-msvc-msil-ld` | MSIL driver, rc 0 |
| `x86_64-unknown-linux-gnu-msil-ld`, `x86_64-w64-windows-gnu-msil-ld` | **Gnu** flavor chosen (`gnu` comes before `msil`); not linked, so a silent rc 1 |
| `wasm-ld` (copy of the msil-only exe) | silent rc 1 (flavor not linked) |
| `lld --version` | "lld is a generic driver. Invoke ... msil-ld (MSIL) instead", rc 1 |

Consequences:
- The clang MSIL toolchain (driver-toolchain recon) must invoke plain `msil-ld`, or pass
  `-flavor msil` as `argv[1]`. A triple-prefixed linker name that contains `gnu` must never be
  generated or installed.
- Optional one-line hook: `parseProgname` returns MSIL when any component is `msil`.
- The `ld-msil` spelling (by analogy with `ld-wasm`) is reachable only through `-flavor`,
  because `parseProgname("ld-msil")` hits `ld` first. It can be dropped.

### 1.4 Inside `lld.exe` or a standalone `msil-ld.exe`?

| | Inside multi-flavor `lld` (alias `msil-ld`) | Standalone tool target (`lld/tools/msil-ld`, links lldCommon + lldMSIL only) |
|---|---|---|
| Upstream shape | the wasm precedent exactly | unusual (only unittests build driver subsets: `lld/unittests/AsLibELF`) |
| First build | +126 compile steps (ELF 41, MachO 31, COFF 18, wasm 14, Common 13, 5 LLVM, 3 tool) +7 tablegen, about 2.5 min wall at -j4 measured | +13 Common + about 18 lldMSIL TUs, under 1 min |
| Exe | 66.9 MB, plus one copy per alias (5 aliases, about 0.4 GB) | 59.4 MB (all LLVM targets linked, `${LLVM_TARGETS_TO_BUILD}`) |
| lit | `lld/test/lit.cfg.py` works (`use_lld()` needs `ld.lld`, `lld-link`, `ld64.lld`, `wasm-ld`) | `lld/test/*` cannot run: `use_lld(required=True)` is fatal when those are missing |
| Fork diff | the hooks of 1.2 | a new tools directory, plus the same Common hooks |

Recommendation: host msil-ld in `lld` (upstream-shaped, and lld's lit works). The extra 92 TUs
are a one-time 2 minutes. If D: space gets tight, configure `-DLLD_SYMLINKS_TO_CREATE="msil-ld;ld.lld;lld-link;ld64.lld;wasm-ld"`
(all needed for lit) and do not add more.

## 2. Inventory

### 2.1 lld/Common (1,297 lines of .cpp; 2,857 with CMake and headers): reused as is

| File | Lines | Purpose | msil-ld |
|---|---|---|---|
| Args.cpp | 92 | `getInteger`, `getStrings`, `getCGOptLevel`, `getZOptionValue`, `getFilenameWithoutExe` | COPY (unchanged) |
| CommonLinkerContext.cpp | 57 | global context; registers `codegen::RegisterCodeGenFlags` (line 32) for LTO | unchanged |
| DriverDispatcher.cpp | 202 | flavor dispatch (1.1) | HOOK (1.2) |
| DWARF.cpp | 103 | DWARF line tables for ELF/wasm diagnostics | unused by MSIL (compiled anyway) |
| ErrorHandler.cpp | 358 | `error`/`warn`/`fatal`, `--error-limit`, colours, `diagnosticHandler` for LTO, `checkError`, `CHECK` | unchanged |
| Filesystem.cpp | 157 | `tryCreateFile`, `openFile`, `openLTOOutputFile`, `unlinkAsync` | unchanged |
| Memory.cpp | 25 | `make<T>` arena | unchanged |
| Reproduce.cpp | 66 | `relativeToRoot`, `quote`, `toString(const Arg &)` for `--reproduce` | unchanged |
| Strings.cpp | 81 | `saveBuffer`, `parseHex`, glob `StringMatcher` | unchanged |
| TargetOptionsCommandFlags.cpp | 29 | TargetOptions/CPU/attrs from `-mllvm`-style codegen flags for LTO | unchanged |
| Timer.cpp | 72 | `ScopedTimer` | unchanged |
| Utils.cpp | 22 | misc | unchanged |
| Version.cpp | 33 | `getLLDVersion` (needs the generated `VCSVersion.inc`) | unchanged |
| include/lld/Common/*.h | | Driver.h (HOOK); BPSectionOrdererBase.{h,inc} (ELF/COFF/MachO profile ordering) unused | |

### 2.2 lld/wasm (12,435 lines in 31 files): classification for msil-ld

COPY means nearly verbatim (renames only). ADAPT means the same structure with wasm specifics
replaced. DROP means no MSIL equivalent. DEFER means a later phase.

| File | Lines | Purpose (verified) | Class | msil-ld notes |
|---|---|---|---|---|
| CMakeLists.txt | 42 | lldWasm library, Options tablegen | ADAPT | `lldMSIL`, `MSILOptionsTableGen`. LINK_COMPONENTS can stay `${LLVM_TARGETS_TO_BUILD}` (LTO only needs MSIL, but the shared `lld.exe` links every target anyway) |
| Config.h | 301 | `Config` (options) and `Ctx` (state, `WasmSym` linker-defined symbols), global `ctx` | ADAPT | Keep generic fields (entry, output, searchPaths, isStatic, LTO/ThinLTO, map, whyExtract, unresolvedSymbols, exports, gcSections, allowMultipleDefinition, trace). Drop memory/table/global-base/PIC/TLS/features. Add triple, references, export class, assembly name, output kind (exe/dll), machine from triple. `WasmSym` becomes `MSILSym`: `<msil>data`, image field, `.cctor`, `<msil>Main`. Keep `reset()` for `LLD_IN_TEST=2` |
| Driver.cpp | 1517 | `link()` 128-145, option parse plus response files 206-230, `addFile` 272-330, `-l` search 332-365, `createFiles` 367-403, `readConfigs` 496-683, `setConfigs` 685-743, `checkOptions` 745-791, `--reproduce` 793 and 1144-1170, `handleUndefined` 800-816, `handleLibcall` 818-825, `writeWhyExtract` 827-845, `demoteLazySymbols` 847-859, synthetic symbols 901-1024, stub libraries 1026-1142, `--wrap` 1178-1250, `linkerMain` 1282-1515 | ADAPT | Keep the flow and generic option handling (about 60%). Drop memory/table/TLS/PIC/stub/`.imports`. Add Design.md 5.2 search order (2.3), reference assemblies, triple selection, `main` as the LTO and GC root |
| InputChunks.cpp/.h | 617/393 | `InputChunk` (segments, functions, custom sections), `relocate` 110-176 (LEB/I32/I64 patching), `MergeInputChunk` string merging, `generateRelocationCode` 440+ (runtime data relocs for PIC) | ADAPT | `InputSegment` becomes the D12 data piece. `InputFunction` becomes `InputMethod` (IL body + 4-byte token relocs: no LEB padding, no `--compress-relocations`). `generateRelocationCode` becomes IL for the `.cctor` (4.3). Merge chunks DEFER (SHF_STRINGS-like merging). Custom sections DEFER (PDB, P4) |
| InputElement.h | 106 | `InputGlobal`, `InputTag`, `InputTable` | DROP | wasm globals, tags and tables have no MSIL input counterpart (address fields are synthesized) |
| InputFiles.cpp/.h | 920/206 | `InputFile` kinds Object/Shared/Bitcode/Stub; `createObjectFile` 87-105; `ObjFile::parse` 493-631; reloc value calc 109-230; `parseLazy` 403-417; `SharedFile::parse` 431-466; `StubFile` 781-814; `BitcodeFile` 851-918 (triple check `isWasm()` at 903-907) | ADAPT | `ObjFile` reads P2-A objects through an `llvm::object` reader. `BitcodeFile` is nearly COPY, with the triple check of 3.3. `SharedFile` becomes `NativeLibraryFile` (DLL export table, import library members, ELF `.so` dynsym + `DT_SONAME`). New: `ManagedAssemblyFile` (P2-C export table) and `ReferenceAssemblyFile` (BCL binding). `StubFile` DEFER (a text "symbol list" format for cross builds without the native library, Design.md 5.2 step 2.3) |
| LTO.cpp/.h | 311/62 | `createConfig` 43-94, `BitcodeCompiler` 98-121, `add` (symbol resolutions) 137-171, `compile` 206-309 | COPY + small ADAPT | `OverrideTriple`; `RelocModel` always Static; drop the shared-memory feature hack 66-69; `--lto-emit-asm`; assembly name (3.2). Section 3 |
| MapFile.cpp/.h | 157/19 | `-Map` / `--print-map` table of sections, segments, functions | ADAPT | Columns: RVA, size, token, output, input, symbol, for methods, image offsets of globals, P/Invokes |
| MarkLive.cpp/.h | 246/18 | `--gc-sections` from entry, exports, init functions (90-99) and retained segments | ADAPT | Roots: `main`/`--entry`, `-shared` exports, constructors, `llvm.used`/retain. Mark through relocations |
| Options.td | 328 | option table (`B`, `Eq`, `EEq`, `F`, `J`, `JJ` helper classes; `-r` is `--relocatable` at line 294) | ADAPT | 2.4 |
| OutputSections.cpp/.h | 297/138 | wasm sections (code, data, custom) with headers and relocs | DROP | The metadata/PE writer (4.1) replaces them |
| OutputSegment.cpp/.h | 87/60 | merged data segments (name, alignment, flags, startVA) | ADAPT | Segments of the one RVA image (`.rodata` < `.data` < `.bss`, as `Writer.cpp` 1173-1182 sorts) |
| Relocations.cpp/.h | 199/20 | `scanRelocations` 85+: undefined reporting (`reportUndefined` 47-75), GOT/import entries, table-index (function pointer) entries | ADAPT | Reports undefined symbols. Turns native-resolved functions into P/Invoke stubs (`ImportSection::addImport` analogue). Function-address relocs request a `<msil>fn_*` field (M9), the analogue of `R_WASM_TABLE_INDEX` going to `ElemSection`. BCL refs go to the binder |
| SymbolTable.cpp/.h | 1225/148 | `addFile` 26-79, `compileBitcodeFiles` 81-118, `insert`, `add{Defined,Undefined,Shared,Common}*`, `shouldReplace` 329-376 (duplicate error 372-374), `addLazy` 1001, comdats 1039, signature variants 1046 and 1178, `--wrap`, weak undefined stubs | ADAPT | Keep everything for Function/Data/Common/Lazy symbols. Drop Global/Table/Tag. Native and managed library definitions are **late-bound** (2.3). Keep signature-mismatch variants: wasm warns and replaces calls through a mismatched signature with an unreachable stub (1178-1225). msil-ld can do the same (`ldnull; throw`) or generate adapters |
| Symbols.cpp/.h | 442/641 | 17 symbol kinds (Symbols.h 50-66) with flags (weak, hidden, exported, no-strip, live, used-in-regular-obj) | ADAPT | Keep DefinedFunction/Data, UndefinedFunction/Data, Common, Lazy. SharedFunction/Data become NativeFunction (P/Invoke module + entry name). New ManagedFunction/ManagedData (P2-C). Drop Global/Table/Tag/Section kinds (Section DEFER for debug info) |
| SyntheticSections.cpp/.h | 1030/478 | type, import, function, table, memory, tag, global, export, start, elem, data-count, linking, name, producers, target-features, reloc, build-id sections | DROP (concepts ADAPT) | TypeSection becomes signature and `StandAloneSig` interning; ImportSection becomes P/Invoke `ImplMap`/`ModuleRef`; ExportSection becomes the export class (P2-C); ElemSection becomes function address fields; TargetFeaturesSection becomes the triple/ABI attribute (Design.md 5.4); BuildIdSection becomes a deterministic MVID; Linking/Reloc sections (`-r` output) DEFER |
| Writer.cpp/.h | 2057/18 | `Writer::run` 1874-2048: common symbols, output segments 1124-1193, `layoutMemory` 350-533, scanRelocations, synthetic init functions (`__wasm_init_memory` 1339, `__wasm_apply_data_relocs` 1623-1653, `__wasm_call_ctors` 1706-1733), `assignIndexes` 999, imports 795, exports 813, map file, `FileOutputBuffer` | ADAPT | Same skeleton. The synthesized functions map one to one onto the D12 `.cctor` parts (4.2). Section 4 |
| WriterUtils.cpp/.h | 274/78 | LEB128 and wasm type encoders | DROP | The metadata writer library has the ECMA-335 compressed-integer and signature encoders |

What carries over almost verbatim (about 3,000 lines of the 12,435):
- `link()` and the `linkerMain` flow;
- the `OptTable` subclass and response-file handling;
- archive member enumeration and lazy symbols;
- `BitcodeFile` and `BitcodeCompiler`;
- `--reproduce`, `--why-extract`, `--trace-symbol`, `--wrap`, `--error-limit`;
- the duplicate- and undefined-symbol diagnostics;
- the map-file and mark-live skeletons.

### 2.3 What wasm-ld does that msil-ld also needs (and where MSIL differs)

| Capability | wasm-ld today (verified) | msil-ld |
|---|---|---|
| Inputs | `identify_magic`: `archive`, `bitcode`, `wasm_object`, `#STUB` text (Driver.cpp 278-329); `--start-lib/--end-lib`, `--whole-archive` | P2-A objects, bitcode, archives of either; native libraries (`pecoff_executable` without a CLI header, import-library members, ELF `.so`); managed assemblies (PE with a CLI header and the 5.4 attribute, P2-C); reference assemblies |
| Archives | members listed with `Archive::children`; the archive symbol index is **not** used; every member's own symbol table goes in as `LazySymbol`s (`parseLazy`); a later undefined reference extracts (`SymbolTable::addLazy` 1001), so archive order does not matter; non-wasm/bitcode members are warned about (Driver.cpp ~303-306) | same. Additionally an archive with no CIL or bitcode member (glibc's ELF `libc.a`, an MSVC import library) is **not** a static library for `-l` purposes (see search order) |
| Bitcode | `BitcodeFile` → `lto::InputFile`; members renamed `"a.a(m.o at <offset>)"` for ThinLTO uniqueness (InputFiles.cpp 865-870); `doneLTO` forbids bitcode after LTO | same; triple check per 3.3 |
| Resolution | ELF semantics: the first definition wins; strong beats weak and common; shared (`.so`) symbols are ordinary entries in command-line order (`shouldReplace` 329-376) | Design.md 5.2: objects and archives first, then managed assemblies, then native libraries. So managed and native definitions are **late-bound**: they never preempt a definition and never block archive extraction, and only symbols still undefined after all inputs are loaded look them up. This differs from ld.lld's order-dependent `.so` semantics. It is what the prototype implements (`ctx.nativeExports` side table, Driver.cpp of the prototype) |
| `-l` search | per `-L` dir: `lib<n>.so` (unless `-Bstatic`), then `lib<n>.a`; `-l:<file>` exact (Driver.cpp 332-358) | Design.md 5.2 per dir: `lib<n>.a` (static, only if it has CIL/bitcode members), then managed `<n>.dll`, then native `lib<n>.so`/`<n>.dll`/`lib<n>.dylib`. Windows also needs `<n>.lib` (import library: `COFFImportFile` members give symbol → DLL, Design.md 3.1). **Static-first is the opposite of GNU ld's `-Bdynamic` preference**; write that into the docs. `-Bstatic` allows only archives; `-Bdynamic` restores the default |
| `--export`, `-shared` | `--export` forces a symbol defined and exported (`handleUndefined` + `forceExport`); `-shared` means PIC with `__wasm_call_ctors` as entry; `--export-dynamic` defaults on for `-shared` | `-shared` gives a library assembly: no `.entrypoint`, default-visibility functions public on the export class (P2-C). `--export`/`--export-if-defined`/`--export-all` keep their meaning. There is no PIC: the data base is always dynamic (`AllocHGlobal`), so `-pie`, `--experimental-pic`, `-Bsymbolic` are DROP |
| Entry | `--entry` (default `_start`) or `--no-entry` | default `main`, which the backend wraps as `'<msil>Main'` (D27); the linker sets the CLI header's `EntryPointToken`. `main` must be an LTO root (`VisibleToRegularObj`), or LTO internalizes it and GlobalDCE deletes it, leaving no entry point |
| `--gc-sections` | default on unless `-r` (Driver.cpp 552-553) | same (MarkLive) |
| Map file | `-Map`, `--print-map`/`-M` | same, MSIL columns |
| Response files | `cl::ExpandResponseFiles` with `--rsp-quoting` (Windows tokenizer on a Windows host) | COPY (verified with `@link.rsp` in the prototype) |
| Diagnostics | `duplicate symbol: X\n>>> defined in A\n>>> defined in B`; `<file>: undefined symbol: X`; `function signature mismatch: ...`; `unable to find library -lX`; `--error-limit`, `--fatal-warnings`, `--noinhibit-exec`, `--unresolved-symbols=`, `--allow-undefined[-file]` | COPY the texts (the prototype prints the first three in lld's format). `--unresolved-symbols=ignore-all` must not silently produce P/Invokes to an unknown module (Design.md 5.2 step 4) |
| `--reproduce` | tar with `response.txt`, `version.txt` and every input read through `readFile` | COPY. Verified: native DLLs and reference assemblies end up in the tar too (`ucrtbase.dll` did) |
| LTO options | `--lto-O`, `--lto-CGO`, `--lto-partitions`, `--lto-obj-path`, `--thinlto-*`, `--save-temps`, `--disable-verify`, `-mllvm` | COPY, plus `--lto-emit-asm` (ld.lld) |
| `-z stack-size` | wasm stack size | ADAPT later: PE `SizeOfStackReserve` for .NET Framework `.exe`, or a thread for `main` (Status.md section 6 item 7) |
| `--build-id` | build-id section | ADAPT: deterministic MVID |
| `-r` / `--relocatable` | partial link | DEFER (needs an object writer in lld). **`-r` therefore cannot be the reference option** |

### 2.4 Proposed option spellings (lld conventions, two dashes for new long options)

| Purpose | Spelling | Notes |
|---|---|---|
| Reference assembly for BCL binding | `--reference=<path>` / `--reference <path>`; a positional `.dll` whose CLI metadata has no 5.4 symbol-table attribute is also taken as a reference | lld's `-r` is `--relocatable` (`lld/wasm/Options.td:294`, `lld/ELF/Options.td:621`). csc's `-r:` cannot be copied. The positional form matches the plan's "managed .dll inputs become references" (Design.md section 8) |
| Reference search | `-L` dirs plus `-l:<file>` | no new option |
| Codegen triple | `--target=<triple>` (alias `-m`? lld's `-m` means emulation, so better not) | default: the first bitcode or object input's triple; a native triple means `<triple>-msil` |
| Assembly name | `--assembly-name=<name>`, default the stem of `-o` | today `-mllvm -msil-assembly-name=` (`MSILAsmPrinter.cpp:93`); see 3.2 |
| Export class | `--export-class=<Ns.Class>` | Design.md 5.4 |
| IL text output | `--lto-emit-asm` | ld.lld precedent (`lld/ELF/Options.td:643`, `LTO.cpp:169-172`, `Driver.cpp:3443`) |

The prototype's [Options.td](lld-flavor-files/proto/Options.td) compiles with `llvm-tblgen -gen-opt-parser-defs`
and the wasm helper classes.

## 3. LTO

### 3.1 How lld/wasm drives LTO (verified)

1. **Parse.** `BitcodeFile` holds an `lto::InputFile` (InputFiles.cpp 873).
   `createBitcodeSymbol` (827-849) puts its irsymtab symbols into the symbol table as defined or
   undefined functions and data, and comdat leaders go through `SymbolTable::addComdat`.
2. **Libcalls.** Before LTO, `lto::LTO::getRuntimeLibcallSymbols(TT)` (`llvm/lib/LTO/LTO.cpp:1506`)
   extracts lazy bitcode members that define runtime libcalls (`handleLibcall`, Driver.cpp
   1436-1440). `getLibFuncSymbols` reports which TLI functions are lazily available
   (`SymbolTable.cpp:89-108`, then `setBitcodeLibFuncs`).
3. **Configure.** `SymbolTable::compileBitcodeFiles` (81-118) creates `BitcodeCompiler`.
   - `createConfig` (LTO.cpp 43-94) sets the codegen-flag TargetOptions, `FunctionSections` and
     `DataSections`, the opt levels, CPU and attrs, and the relocation model.
   - The ThinLTO backend is in-process or index-writing (LTO.cpp 104-118).
4. **Resolve.** `BitcodeCompiler::add` (137-171) builds one `lto::SymbolResolution` per irsymtab
   symbol: `Prevailing` (this file's definition won), `VisibleToRegularObj` (used by a native
   object, no-strip or exported), `LinkerRedefined` (`--wrap`). It also undefines the prevailing
   bitcode symbol so the LTO object can define it.
5. **Compile.** `compile` (206-309) runs `ltoObj->run` with an `AddStreamFn` into memory buffers
   (and the ThinLTO cache). Each non-empty buffer goes through `createObjectFile(...)` and
   `ObjFile::parse(true)`, so **the LTO output is an ordinary object** (SymbolTable.cpp 112-117).

### 3.2 What the MSIL TargetMachine needs to be an LTO codegen target

- **Already works:** `lto::backend` (`LTOBackend.cpp:600-627`) looks the target up by the
  (overridden) module triple, creates the TM, runs the post-link opt pipeline with the MSIL TTI
  and calls `codegen` with `Conf.CGFileType`. With `CodeGenFileType::AssemblyFile` the existing
  AsmPrinter writes ILAsm. The prototype uses exactly this and it ran the corpus 93/93.
- **Object output is P2-A's job:** `MSILTargetMachine::createMCStreamer` returns a `StringError`
  for `ObjectFile` today (`llvm/lib/Target/MSIL/MSILTargetMachine.cpp:210-217`). Once P2-A's MC
  object streamer exists, `lto::Config::CGFileType` stays at its default (`ObjectFile`,
  `Config.h:70`) and msil-ld parses the result like any object. This is the only shape that
  supports ThinLTO and `--lto-partitions`, which produce one module per task, and the ThinLTO
  cache, which caches object buffers. Emitting "straight into msil-ld's output model" would
  bypass `lto::LTO::run`'s streams, caching and parallel codegen and need a non-upstream TM
  API. Not recommended.
- **Phase-1 whole-program assumptions must become "emit an undefined symbol" in object mode.**
  - D14: an undefined function that is not a Phase-1 import is an error.
  - D26: an external global variable is an error.
  - The per-module `<Module>::.cctor` (`MSILAsmPrinter.cpp:848-1005`) and the data image
    (`emitEndOfAsmFile` 488-530) cannot be merged as IL. In object mode the backend emits the
    ingredients (segments, data relocations, constructor list with priorities, function-address
    uses) and msil-ld synthesizes the one `.cctor` (4.2).
  - Measured: ThinLTO inputs through the prototype fail with
    `external global variable 'counter' is not supported (phase 2 linker)`, because each ThinLTO
    module is codegen'd separately.
- **Options the in-process linker must set without `cl::opt`:** the backend has exactly one,
  `-msil-assembly-name` (`MSILAsmPrinter.cpp:93`). LTO's combined module is named `ld-temp.o`,
  so without it the assembly is called `'ld-temp'` (measured). The prototype passes
  `-mllvm -msil-assembly-name=prog`. For the real linker: a module flag (`"msil-assembly-name"`,
  set by msil-ld through `lto::Config::PreCodeGenPassesHook`, like the import annotation below)
  or a `TargetOptions`/`MCTargetOptions` field. Later codegen switches (P3-A) need the same
  channel, and Design.md 7.1 says capability comes from the references, which only msil-ld sees.
- **Native imports in the IL-text milestone:** the backend honours `"msil-import-module"`
  (plan D14). msil-ld resolves undefined functions against native export tables and hands the
  result to codegen with a legacy `ModulePass` added through `lto::Config::PreCodeGenPassesHook`
  (`Config.h:65`; `codegen()` calls it at `llvm/lib/LTO/LTOBackend.cpp:504-505`, before `addPassesToEmitFile`, so it runs before MSILPrepareIR). The pass sets
  the attribute on each resolved declaration. The prototype resolved `strlen`, `atoi` and
  `toupper` from `ucrtbase.dll`, none of which is in the D14 table, and the program ran.
  `PreCodeGenModuleHook` cannot do this: it takes a `const Module &` (`Config.h:241-263`).
  In the object milestone this pass goes away, because native resolution then applies to the
  LTO object's undefined symbols like any other.
- **Libcall pre-extraction:** `getRuntimeLibcallSymbols(Triple)` builds
  `RTLIB::RuntimeLibcallsInfo(TT)` from the triple. For `x86_64-*-msil` that is the X86 set, not
  what `MSILSubtarget::initLibcallLoweringInfo` decides. The effect is harmless: more lazy
  bitcode members than necessary may be extracted. P3's C helper archive should be bitcode or
  CIL and is found this way. *Unverified* whether `RuntimeLibcallsInfo` should learn the MSIL
  triple.
- `EmitAddrsig`, `ExceptionModel` and the relocation model are forced by the MSIL TM (D8), so
  LTO's codegen flags cannot break them.

### 3.3 Retargeting stock-clang bitcode to the `-msil` triple

Mechanism (verified in the prototype):
- msil-ld picks the codegen triple: `--target`, else the first input's triple, with `-msil`
  appended when the triple is native.
- It accepts an input whose `getNativeFormatTriple()` has the same arch, OS and environment.
  This ignores the vendor and the MSVC version suffix, so `msvc19.33.0` and `msvc19.51.0` mix.
- It sets `lto::Config::OverrideTriple` to the `-msil` triple.

Measurements:
- DataLayout is byte-identical by construction (`Triple::computeDataLayout`,
  `llvm/lib/TargetParser/Triple.cpp:469-474`); checked:
  `e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128` on Windows,
  `e-m:e-...` on Linux.
- `"target-cpu"="x86-64"`, `"target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87"`,
  `"tune-cpu"="generic"` and the module flags are identical between stock and `-msil` clang.
- The MSIL subtarget accepts any feature string (D7).

| Corpus run (93 C programs of msil/test/M1; details in [logs/retarget-results.txt](lld-flavor-files/logs/retarget-results.txt)) | Result |
|---|---|
| stock `x86_64-pc-windows-msvc` -O2 bitcode → llc `-msil` → ilasm → .NET 8 | 36 pass, 57 compile errors |
| same with `-fno-ms-volatile`, -O2 and -O0 | 91 pass, 2 errors |
| stock `x86_64-unknown-linux-gnu` -O2 and -O0 → llc → ilasm → WSL .NET 10 | 89 compile, all 89 pass; 4 errors |
| MSIL-clang bitcode through the msil-ld prototype (LTO), -O2 and -O0 | 93 of 93 pass |
| stock windows `-fno-ms-volatile` bitcode through the msil-ld prototype | 91 pass, 2 errors |

The incompatibilities, each with its cause in clang:

1. **`-fms-volatile`** (windows-msvc x86/x86_64 default; the MSIL driver turns it off,
   `clang/lib/Driver/ToolChains/Clang.cpp:6417-6425`). `volatile` accesses become
   `load atomic volatile ... acquire` and `__atomic_load_N` errors in Phase 1. From P3 on they
   would compile, but with stronger semantics than MSIL clang's (CIL `volatile.` is already
   acquire/release). 55 of 93 programs.
2. **Empty asm barrier.** MSIL clang turns `asm volatile("" ::: "memory")` into a fence
   (`clang/lib/CodeGen/CGStmt.cpp:2765`). Stock IR keeps inline asm, which the backend rejects.
   A cheap backend fix (MSILPrepareIR accepting an empty asm string with a memory clobber as a
   compiler barrier) would make stock bitcode tolerant.
3. **K&R calls.** Stock clang does not add `"no-prototype"` (MSIL only:
   `CodeGenModule.cpp:3837-3839`), and on SysV x86_64 it calls unprototyped functions as
   variadic (`CGCall.cpp:715` skips this only for MSIL). This gives "mismatched call signature"
   and "calls to variadic functions are not supported".
4. **`long double`.** It is `x86_fp80` (16 bytes) in stock x86_64 Linux and MinGW bitcode and
   `double` under MSIL. Code that uses it fails in the backend (x86_fp80 is rejected). But a
   **struct layout** that contains it differs silently between stock and MSIL TUs of one link.
   windows-msvc is unaffected (`long double` is `double` natively).
5. **Predefines.** These are source-level and invisible to the linker: stock TUs see `__ELF__`
   on Linux and no `__MSIL__`, so headers that route variadic natives through MSIL-specific
   declarations (Design.md 4.7) take the native path.
6. **Code quality.** The pre-link pipeline runs with the native X86 TTI, so stock bitcode
   arrives vectorized: 3,221 vector-typed IR lines against 103 over the corpus. The MSIL
   legalizer scalarizes it again, and the resulting IL has 202,856 instruction lines against
   159,549 (+27%).
7. **IRMover warning.** Mixing stock and MSIL bitcode in one regular-LTO link prints
   `warning: Linking two modules of different target triples` once per input with the other
   triple (`llvm/lib/Linker/IRMover.cpp:1511-1517`). `Triple::isCompatibleWith`
   (`Triple.cpp:2304-2345`) compares `getObjectFormat()`. Fix options:
   - (a) an upstream hook in `Triple.cpp`: two triples whose `getNativeFormatTriple()` are
     compatible are compatible. This is semantically what Design.md section 3 says. It is not
     a widely-included header, but it needs a `TripleTest` case.
   - (b) msil-ld's diagnostic handler drops this one warning when it has already validated
     the inputs (string match, brittle).

   Recommend (a).

Recommendations:
- Accept stock bitcode as Design.md promises, but say in the docs that it is a compatibility
  path. msil-ld warns once per link when an input has a native triple.
- For windows-msvc, say that it needs `-fno-ms-volatile` until P3. For linux-gnu and MinGW,
  warn about `long double` layout.
- Also make the backend accept the empty asm barrier (item 2).

## 4. The PE/metadata output stage inside the flavor

### 4.1 Where it lives

- `lld/MSIL/Writer.cpp` keeps wasm's `Writer::run` skeleton (`lld/wasm/Writer.cpp:1874-2048`)
  and decides **what** goes into the assembly: layout, token assignment, synthesized members,
  relocation application. The byte-level ECMA-335 writer (tables, heaps, signature blobs, IL
  method headers, CLI header, PE envelope) is a library, per the `metadata-writer` recon.
  - lld/COFF's writer cannot be reused: `lld/COFF/Writer.cpp` (3,150 lines) is tied to
    `COFFLinkerContext` and lld's chunk model.
  - There is no CLI/`COR20`/`.cormeta` support anywhere in `llvm/lib/Object` or `lld/COFF`
    (grep, this checkout).
- msil-ld also needs an **ECMA-335 metadata reader** in every container option: for reference
  assemblies (BCL binding, P2-B) and for managed assemblies built by this toolchain (P2-C).
  - Put it in `llvm/lib/Object` as an `ObjectFile`/`SymbolicFile`, as `WasmObjectFile` is for
    lld/wasm, `llvm-nm`, `llvm-objdump` and `llvm-ar`.
  - `llvm-ar` builds archive symbol indexes only for formats `llvm::object` can read. lld does
    not need the index (it parses members itself), but `llvm-nm` and other tools do.

### 4.2 Data layout at link time (D12) and the synthesized members

Mapping of wasm-ld's steps onto D12:

| wasm-ld (Writer.cpp) | msil-ld |
|---|---|
| `allocateCommonSymbols` | tentative definitions go to `.bss` |
| `createOutputSegments` (1124-1193): group live input segments by name, sort `.rodata` < `.data` < `.tdata` < `.bss` | one RVA image: concatenated live `InputSegment`s, same order; TLS templates (`.tdata/.tbss`) become the P3 TLS block template |
| `layoutMemory` (350-533): stack, global base, segments at aligned VAs, `__data_end`, heap | offsets inside the image instead of VAs. Each segment is aligned to its alignment; `InitSize` is the end of the last initialized byte; `TotalSize`; `MaxAlign = max(16, largest alignment)` (as `MSILDataSegment`, `llvm/lib/Target/MSIL/MSILDataSegment.h`). No stack or heap placement (the shadow stack is a P3 `[ThreadStatic]` block) |
| `__wasm_init_memory` (1339): `memory.init` passive segments | `.cctor`: `AllocHGlobal(TotalSize + MaxAlign - 1)`, align, `cpblk` from the `FieldRVA` image, `initblk` for `.bss` (what `MSILAsmPrinter::emitModuleInitializer` 848-1005 emits per module today) |
| `__wasm_apply_data_relocs` (1623-1653) via `InputChunk::generateRelocationCode` (`InputChunks.cpp:440`) | the absolute address relocations inside data (pointer slots), applied in `.cctor` after allocation. The base is only known at run time, so these are always runtime relocations |
| relocation values folded at link time (`InputChunk::relocate` 110-176) | symbol differences (`MSILDiff`: A − B + addend, both in the image) and image offsets are link-time constants written into the image bytes |
| table entries for address-taken functions (`ElemSection`, `R_WASM_TABLE_INDEX_*`) | one `'<msil>fn_<name>'` field per address-taken function, set once by `ldftn` in `.cctor` (M9, Status.md section 6 item 8); data relocs to functions read that field |
| `__wasm_call_ctors` (1706-1733) from init functions sorted by priority (`calculateInitFunctions`) | constructors called last in `.cctor`, in priority order across all objects |
| `_start`/command export wrappers | `'<msil>Main'` (D27) from the object that defines `main`, or synthesized by msil-ld; the CLI header's `EntryPointToken` set by the linker |
| `ImportSection` | P/Invoke `MethodDef`s with `ImplMap` + `ModuleRef` for native-resolved functions |

Scaling note: Phase 1 emits straight-line IL per relocation (`MSILAsmPrinter.cpp:958-986`). For
whole programs (SQLite, Lua) msil-ld should write a relocation table (site offset, target
offset) into the RVA image and a short loop in `.cctor`. wasm-ld has this as a TODO
(`InputChunks.cpp:449`, "Encode the relocations in the data section and write a loop").
A huge straight-line `.cctor` risks the JIT's MinOpts fallback for big methods (Design.md
section 10, risk 1).

### 4.3 Relocations in code

CIL tokens are always 4 bytes, so applying a code relocation never changes a method's size.
There is no LEB padding and no `--compress-relocations` (`InputChunks.cpp:236-366` is DROP).
`InputMethod::writeTo` copies the IL body and patches each relocation site with the output
token. The relocation kinds the linker needs, whatever P2-A calls them:
- MethodDef or MemberRef of a callee: a defined function, a P/Invoke stub, or an export of a
  managed assembly (P2-C).
- FieldDef of a global's or function's address field. If globals are instead addressed as
  `<msil>data + offset`, the kind is an image offset in an `ldc.i4` immediate, as with
  `R_WASM_MEMORY_ADDR_SLEB`.
- `StandAloneSig` for `calli` and the local-variable signature in the fat method header,
  interned per output.
- TypeRef or MemberRef of a BCL member (`Marshal::AllocHGlobal`, `Buffer::MemoryCopy`,
  `Interlocked::MemoryBarrier`, later `Math`, attributes). Objects must keep these
  **symbolic**, with no assembly baked in, so msil-ld can bind them against the references
  (Design.md 7.1, bcl-binding recon).

### 4.4 What lld needs from P2-A (derived from what lld/wasm's InputFiles consumes)

| | Requirement |
|---|---|
| R1 | A reader in `llvm/lib/Object`, so that lld, `llvm-ar`'s index, `llvm-nm` and `llvm-objdump` work, plus a `file_magic` the driver can tell apart from native PE/COFF/ELF and from bitcode |
| R2 | A symbol table: name; kind (function/data, later section); binding (global/weak/local); visibility (default/hidden); defined or undefined; function signature (for P/Invoke stubs and cross-object signature checks, wasm `WasmSymbol::Signature`); data segment, offset and size; comdat index; no-strip/retain; import module and name (`msil-import-module`, like wasm's `import_module`) |
| R3 | Methods: the IL body, max stack, locals signature, relocations (offset, kind, symbol index, addend) |
| R4 | Data segments: bytes, size, alignment, flags (mergeable strings, TLS, zero-fill) and relocations (site, symbol, addend, width 4/8; absolute or difference) |
| R5 | Constructors with priorities; later destructors/atexit (P3) |
| R6 | A comdat table (`linkonce_odr` MSVC string literals `??_C@...` appear in many objects: verified duplicates resolve correctly in the prototype through LTO's prevailing logic) |
| R7 | The triple (or a triple/ABI record), so msil-ld can refuse an object for a different ABI, as Design.md 5.4 does for assemblies |
| R8 | Symbolic BCL references (4.3) |

How the flavor changes with the container choices of the object-format recons:
- **A (wasm-style container):** a new `WasmObjectFile`-like reader in `llvm/lib/Object`; lld/MSIL
  as tabled above.
- **B (netmodule/assembly envelope plus link info)** and **C (pure netmodules plus merger):** the
  object reader is the same ECMA-335 reader msil-ld needs for references anyway (4.1), which
  argues for B. `InputMethod` relocations become (token offset, old token → symbol) maps, and
  "token rebasing" is `relocate`.
- **D (ELF/COFF relocatable with CIL sections):** `ObjFile` wraps `ELFObjectFile`/`COFFObjectFile`.
  Weak, comdat and section flags come from the container, and llvm-ar/nm/objdump work at once.
  The lld side is closest to lld/ELF's InputSection model.
- **E (bitcode-only):** the P2B-1 milestone below *is* this option. It works for whole-program
  links (93/93). ThinLTO and LTO partitions are excluded (each task is a separate IL module).
  Every link runs full codegen, and libraries are tied to one LLVM bitcode version.

In every case the `SymbolTable`, `Driver`, `LTO`, `MarkLive`, `MapFile`, the native and managed
library inputs and the `Writer` skeleton are the same. Only `ObjFile` and the relocation
application differ.

## 5. Build cost and the gate

Measured with the shared build's exact compile commands (`D:\llvm-msil\build\compile_commands.json`,
clang-cl `/O2`, the LLVMCore PCH) in the VS dev shell, outputs redirected to scratch
([scripts/time_lld_tus.py](lld-flavor-files/scripts/time_lld_tus.py)). The ninja closure came
from parsing `build.ninja` without running ninja
([scripts/ninja_closure.py](lld-flavor-files/scripts/ninja_closure.py)). The host was shared
with other agents (load about 33% at the start).

| Step | TUs | CPU (sum) | Wall at -j4 |
|---|---|---|---|
| lldCommon | 13 | 13.1 s | |
| lldWasm (a proxy for lldMSIL's size) | 14 | 63-88 s (two runs) | 22.5-31.3 s for both rows |
| lldELF / lldCOFF / lldMachO / lldMinGW / lld.cpp | 41 / 18 / 31 / 1 / 1 | 229 / 96 / 101 / 1.8 / 0.4 s | 108 s |
| LLVM libraries `bin/lld.exe` needs that the shared build lacks (`LLVMDTLTO` 3, `LLVMLibDriver` 1, `LLVMWindowsManifest` 1) | 5 | 18.6 s | |
| tablegen (`Options.inc` ×6, `VCSVersion.inc`) | 7 | under 1 s | |
| link `msil-ld.exe` (lldCommon + prototype + all LLVM libs) | | | 3.4 s, 59.4 MB |
| link full `lld.exe` (all flavors) | | | 1.4 s, 66.9 MB |

- Closure of `bin/lld.exe` in the shared tree today: 126 compiles, 10 link/lib steps and 7
  custom commands. Every other dependency (all LLVM libraries, all three targets, LTO, Passes)
  is already built for clang and llc.
- Estimate for P2B gates: the first `msil-build.ps1 lld` is 2.5-3 minutes. After that, a change
  in lld/MSIL costs one or a few TUs (5-8 s each), a relink (2-4 s) and the alias copies.
- Disk on D:
  - objects 17 MB and their `.lib` files of similar size;
  - `lld.exe` plus copies for `lld-link`, `ld.lld`, `ld64.lld`, `wasm-ld`, `msil-ld` is about
    6 × 67 MB, about 0.4 GB.
  - A standalone `msil-ld` would be about 65 MB, but lld's lit config cannot run then (1.4).

Gate target names:
- Add `lld` to the gate's `msil-build.ps1` target set: today
  `llc opt clang FileCheck not count split-file llvm-config llvm-profdata TargetParserTests MCTests MSILTests`,
  plan line 1737.
- Add `llvm-ar` for archive tests. It is 5 TUs, and its closure shares `llvm-lib` with lld's.
  The VS-bundled `llvm-ar` 22.1.3 works too, but tests should use the tree's tools.
- Later, `llvm-nm` (3 TUs), `llvm-objdump` (11) or `llvm-readobj` (13) to inspect P2-A objects.
- `llvm-as`/`llvm-dis` (2 TUs each) are optional, because `opt -o x.bc` already writes bitcode.
- Never run `check-lld`. Its `LLD_TEST_DEPS` (`lld/test/CMakeLists.txt:42-83`) pull in dsymutil,
  llvm-lipo, llvm-otool and more.
- Run `msil-lit.ps1 lld/test/MSIL`. `build/bin/llvm-lit.py:33` already maps `lld/test/lit.cfg.py`
  to `build/tools/lld/test/lit.site.cfg.py`, which exists. *Unverified* until `lld` is built
  in the shared tree.

Where tests go:
- `lld/test/MSIL/`: linker behaviour checked with FileCheck (options, diagnostics, map,
  `--why-extract`, `--reproduce`, symbol resolution and IL from `--lto-emit-asm`). lit
  substitutions come from `use_lld()`, and `msil-ld` is found on the `PATH` that `use_lld` sets.
- `msil/test/`: execution tests (link, ilasm or direct PE, run on .NET 8/10/Framework, verify),
  because the harness (`msil/test/lit.cfg.py`, `msil-run.py`, the verification legs) lives
  there. That needs a new `%{msil_ld}` substitution and a multi-TU recipe.

## 6. Proposed `lld/MSIL` layout, work packages and the first runnable milestone

### 6.1 Layout (names mirror lld/wasm so diffs stay mechanical, as D30 does for the backend)

```text
lld/MSIL/CMakeLists.txt        lldMSIL, MSILOptionsTableGen
lld/MSIL/Options.td
lld/MSIL/Config.h              Config, Ctx, MSILSym
lld/MSIL/Driver.cpp            link(), LinkerDriver, readConfigs, createFiles, 5.2 search, --reproduce
lld/MSIL/InputFiles.{h,cpp}    InputFile, ObjFile (P2-A reader), BitcodeFile
lld/MSIL/NativeLibrary.{h,cpp} NativeLibraryFile: DLL exports, import libraries, ELF .so + DT_SONAME
lld/MSIL/AssemblyFiles.{h,cpp} ReferenceAssemblyFile (BCL), ManagedAssemblyFile (P2-C)
lld/MSIL/Symbols.{h,cpp}
lld/MSIL/SymbolTable.{h,cpp}
lld/MSIL/LTO.{h,cpp}           BitcodeCompiler (OverrideTriple, --lto-emit-asm, import annotation pass)
lld/MSIL/InputChunks.{h,cpp}   InputMethod, InputSegment
lld/MSIL/OutputSegment.{h,cpp}
lld/MSIL/Relocations.{h,cpp}
lld/MSIL/MarkLive.{h,cpp}
lld/MSIL/BCLBinder.{h,cpp}     binds symbolic BCL refs against references (bcl-binding recon)
lld/MSIL/Synthetic.{h,cpp}     .cctor, '<msil>Main', P/Invoke declarations, address fields, export class
lld/MSIL/Writer.{h,cpp}        Writer::run(): layout, tokens, relocate, call the metadata/PE writer
lld/MSIL/MapFile.{h,cpp}
lld/test/MSIL/                 lit tests (+ lit.local.cfg)
```

Estimated size: about 6,000-7,000 lines, against lld/wasm's 12,435. The P2B-1 subset is
about 2,500 lines plus tests. The prototype that proves P2B-1 is 701 lines in one file.

### 6.2 Milestone P2B-1 (first runnable): "msil-ld links bitcode, archives and native imports into IL that runs"

**Exit criterion (the prototype already meets it, see
[logs/prototype-scenarios.log](lld-flavor-files/logs/prototype-scenarios.log)):**

```text
clang --target=x86_64-pc-windows-msvc -fno-ms-volatile -O2 -flto -c a.c     # stock triple
clang --target=x86_64-pc-windows-msvc-msil -O2 -flto -c b.c h1.c h2.c
llvm-ar rcs lib/libhelp.a h1.o h2.o
msil-ld --lto-emit-asm -o prog.il a.o b.o -L lib -lhelp -L C:/Windows/System32 -lucrtbase \
        -Map prog.map --why-extract=why.txt --reproduce repro.tar
ilasm -dll -pe64 -x64 prog.il && dotnet exec --runtimeconfig net8.json prog.dll   # exit 7, 11 lines
```

The run covers:
- `h1.o` extracted lazily and `h2.o` not;
- cross-TU data with relocations (`names[]`);
- `strlen`, `atoi` and `toupper` as P/Invokes to `ucrtbase`, found in the DLL's export table;
- the diagnostics (duplicate, undefined, incompatible triple, missing `-l`);
- a response file.

The whole msil/test/M1 corpus through msil-ld passes 93 of 93 at -O2 and -O0.

Work packages (disjoint files):

| WP | Owns | Content |
|---|---|---|
| P2B1.a hooks and build glue | `lld/include/lld/Common/Driver.h`, `lld/Common/DriverDispatcher.cpp`, `lld/tools/lld/lld.cpp`, `lld/tools/lld/CMakeLists.txt`, `lld/CMakeLists.txt`, `lld/unittests/AsLibAll/AllDrivers.cpp`, `lld/unittests/AsLibAll/CMakeLists.txt`, `lld/test/lit.cfg.py` (feature map), `lld/MSIL/CMakeLists.txt`, `msil/utils/upstream-hooks.txt`, `msil/utils/ci/check-upstream-hooks.py` (fork-only prefixes) | 1.2; optionally the `parseProgname` msil-first rule |
| P2B1.b driver | `lld/MSIL/Driver.cpp`, `Options.td`, `Config.h` | options of 2.3/2.4, Design.md 5.2 `-l` search, `--reproduce`, `--why-extract`, `-Map` plumbing, triple selection, `main` as the root |
| P2B1.c inputs and symbols | `lld/MSIL/InputFiles.{h,cpp}`, `Symbols.{h,cpp}`, `SymbolTable.{h,cpp}` | bitcode and archive inputs, lazy symbols, comdats, weak/strong, duplicates, late binding (2.3) |
| P2B1.d native libraries | `lld/MSIL/NativeLibrary.{h,cpp}` | COFF DLL export directory (`COFFObjectFile::export_directories`, verified: 2,484 names from `ucrtbase.dll`), import libraries (`COFFImportFile`), ELF `.so` dynamic symbols + `DT_SONAME` (Design.md 5.3) (with the native-imports recon) |
| P2B1.e LTO | `lld/MSIL/LTO.{h,cpp}`, `lld/MSIL/MapFile.{h,cpp}` | 3.1/3.2: `OverrideTriple`, `--lto-emit-asm`, the import-annotation pass, assembly name |
| P2B1.f assembly-name/triple hooks | `llvm/lib/Target/MSIL/MSILAsmPrinter.cpp` (assembly name from a module flag), `llvm/lib/TargetParser/Triple.cpp` + `llvm/unittests/TargetParser/TripleTest.cpp` (3.3 item 7), optionally `MSILPrepareIR.cpp` (empty asm barrier) | small, independent |
| P2B1.t1 tests (separate agent) | `lld/test/MSIL/**` | FileCheck: dispatch, options, diagnostics texts, map, why-extract, reproduce listing, `pinvokeimpl` lines in `--lto-emit-asm` output |
| P2B1.t2 tests (separate agent) | `msil/test/M2-link/**`, `msil/test/lit.cfg.py` (`%{msil_ld}`) | multi-TU execution tests: two TUs, archive, stock bitcode with `-fno-ms-volatile`, native import from `ucrtbase`/`libc.so.6` (WSL) |
| P2B1.docs | `llvm/docs/MSIL/GettingStarted.md`, `Status.md` | |

Gate: `msil-build.ps1 ... lld llvm-ar`; `msil-lit.ps1 lld/test/MSIL msil/test/M2-link`; the M1
suite unchanged.

### 6.3 Milestone P2B-2: "two `clang -c` CIL objects link into one assembly that runs" (needs P2-A)

Depends on the P2-A object reader in `llvm/lib/Object` and on the metadata/PE writer library.

| WP | Owns | Content |
|---|---|---|
| P2B2.a object input | `lld/MSIL/InputFiles.cpp` (ObjFile), `InputChunks.{h,cpp}` | read methods, segments and relocations (4.4 R1-R8) |
| P2B2.b layout and synthesis | `lld/MSIL/OutputSegment.{h,cpp}`, `Synthetic.{h,cpp}` | 4.2: the image, `.cctor` (with a table-driven relocation loop), address fields, P/Invoke declarations, `<msil>Main` |
| P2B2.c relocation and liveness | `lld/MSIL/Relocations.{h,cpp}`, `MarkLive.{h,cpp}` | 4.3, `--gc-sections` |
| P2B2.d writer | `lld/MSIL/Writer.{h,cpp}` | token assignment, relocate, call the metadata writer, `FileOutputBuffer`; LTO switches to `CGFileType::ObjectFile` and the LTO object feeds back in |
| P2B2.e signature checks | `lld/MSIL/SymbolTable.cpp` (variants) | cross-object signature mismatch: wasm's variant mechanism, a trap stub plus a warning, or an adapter |
| tests | `lld/test/MSIL/**`, `msil/test/M2-objects/**` | ilasm/ildasm round trip as the oracle (Design.md 5.5): the output disassembles, reassembles and runs identically |

### 6.4 Later

- **P2B-3:** the BCL binder against `--reference` assemblies (bcl-binding recon); the clang
  driver's `MSILToolChain<NativeTC>` invoking `msil-ld` (driver-toolchain); ThinLTO through
  objects.
- **P2-C:** `ManagedAssemblyFile`, `-shared`, `--export-class`, the 5.4 symbol-table attribute.

## 7. FeatureIdeas.md: Phase 2 choices that would preclude an idea (not planned, only flagged)

- **Idea 1 (class placement).** Keep the owning TypeDef as a property of each defined function
  and data symbol in `Symbols.h` and `Synthetic`. Do not hard-code `<Module>` in the Writer.
  A mapping file belongs in msil-ld, which sees the whole program.
- **Idea 6 (enclosed instance).** Make the late-binding step (2.3) a policy, so a closed-world
  link can refuse native imports. If P2-A addresses globals as image offsets
  (`ldsfld <msil>data` + offset), idea 6a (per-instance memory) needs much less change than with
  one static address field per global.
- **Idea 7 (several triples in one assembly).** Do not make the triple, the data image, the
  P/Invoke table and the `.cctor` process-global singletons of `Ctx`. Keep them in a
  per-output-variant object that the Writer could instantiate more than once. Synthesize the
  initializer as a normal method called from `.cctor`, so it can later run lazily per variant.

None of the P2B-1 choices precludes any idea.

## 8. Risks

1. **Stock-bitcode fidelity** (3.3): `-fms-volatile`, `long double` layout on Linux/MinGW,
   K&R calls, the asm barrier, and 27% larger IL. Mitigate with warnings and docs, the
   `Triple::isCompatibleWith` hook and the backend barrier fix.
2. **Triple-prefixed linker names** dispatch to the ELF flavor (1.3).
3. **lld's lit config requires every lld flavor** (1.4), which forces the full `lld` build
   (+92 TUs, 0.4 GB with the Windows copies).
4. **The `.cctor` grows linearly** with relocations and constructors (4.2).
5. **Phase-1 whole-program assumptions in the backend** (D14, D26, the per-module `.cctor` and
   `Main`) must become object-mode behaviour before P2B-2 and ThinLTO.
6. **`lld/wasm` uses a global `ctx` and `symtab`.** lld/ELF has moved to an explicit `Ctx &`.
   Copying wasm's style keeps diffs mechanical, but every global needs a reset for
   `LLD_IN_TEST=2` (`lld/test/lit.cfg.py` `RUN_LLD_MAIN_TWICE`); the prototype resets its `Ctx`
   with placement new, as `lld/wasm/Driver.cpp:57-73` does.

## 9. Experiments (all in `C:\llvm-msil-scratch\p2\lld-flavor\`, scripts in [lld-flavor-files/scripts](lld-flavor-files/scripts/))

1. **Ninja closure** of `bin/lld.exe` and `lib/lldWasm.lib` + `lib/lldCommon.lib`, from
   `build.ninja`, read-only.
2. **Compile timing** of all 119 lld TUs with the shared build's flags and PCH, in the VS dev
   shell, outputs in scratch. Logs: [logs/time-common-wasm.log](lld-flavor-files/logs/time-common-wasm.log),
   [logs/time-other-flavors.log](lld-flavor-files/logs/time-other-flavors.log).
3. **Full `lld.exe` link** from scratch objects plus the 5 missing LLVM TUs
   ([scripts/link_full_lld.py](lld-flavor-files/scripts/link_full_lld.py)): 66.9 MB, 1.4 s;
   `-flavor wasm --version` works.
4. **msil-ld prototype** ([proto/](lld-flavor-files/proto/)):
   - modified copies of `Driver.h` and `DriverDispatcher.cpp`;
   - `Options.td`, and a 701-line `Driver.cpp` with an MSIL-only `lld.cpp` main;
   - linked against the shared build's static libraries
     ([proto/build.py](lld-flavor-files/proto/build.py)): compile 7.4 s for Driver.cpp, link
     3.4 s.
   - Scenarios (dispatch, link and run, diagnostics, response file, ThinLTO) are in
     [t1/run-scenarios.sh](lld-flavor-files/t1/run-scenarios.sh); the transcript is
     [logs/prototype-scenarios.log](lld-flavor-files/logs/prototype-scenarios.log).
5. **Retarget corpus**: 93 C programs of msil/test/M1. Runs:
   - stock windows-msvc with and without `-fno-ms-volatile`, at O2 and O0;
   - stock linux-gnu at O2 and O0, run in WSL .NET 10;
   - llc and msil-ld paths.

   Results: [logs/retarget-results.txt](lld-flavor-files/logs/retarget-results.txt).
6. **IR comparisons** of `fib.c` between stock and MSIL clang on both triples: DataLayout,
   target attributes and module flags.

Runtimes: .NET 8 (Windows, `dotnet exec --runtimeconfig`), .NET 10 (WSL Ubuntu). Assembler:
.NET Framework ilasm 4.8 (`-dll -pe64 -x64`).
