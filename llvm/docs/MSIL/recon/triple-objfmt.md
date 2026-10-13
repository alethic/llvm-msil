# Recon: `Triple::ObjectFormatType::MSIL` ("msil") and the MC layer

Key: `triple-objfmt`. Status: recon only. No tracked file was modified. Every claim
below was checked against this checkout (branch `msil-milestone1`, LLVM 24.0.0git) unless
it is marked *unverified*. Line numbers refer to the unmodified tree.

How this was verified: I compiled scratch copies of `Triple.h`, `Triple.cpp` and
`TargetDataLayout.cpp`, with and without the proposed patch, together with the few
Support/TargetParser sources they need. I used clang-cl 22.1.3 from VS 2026, no CMake, no
tablegen and no ninja (section 9). The proposed MC-layer edits were syntax-checked with
`-Wswitch`. The proposed new `TripleTest` cases run in a small gtest stand-in: **175
checks, 0 failures**.

---

## 0. Summary and decisions

1. **The suffix parses with one line.** `parseFormat()` (Triple.cpp:721) matches the object
   format with `EndsWith` on the environment component. Adding `.EndsWith("msil", Triple::MSIL)`
   makes all eight design triples parse as `{native arch, vendor, OS, environment} + MSIL`.
   `normalize()` then maps each of them to itself. Arch, OS and environment are exactly the
   native ones, so `isOSWindows()`, `isOSDarwin()`, `isMusl()`, `isOSGlibc()`,
   `isWindowsMSVCEnvironment()`, `getDefaultWCharSize()` and the rest keep their native
   answers. Only the `isOSBinFormat*()` predicates change.
2. **Today, without the patch,** stock LLVM silently treats `...-msil` as the native
   format (ELF/COFF/MachO), and `normalize()` **drops `-msil` from every Windows triple**
   (verified). The patch fixes both.
3. **Data layout: MSIL triples get exactly the native layout.** This includes the mangling
   component (`m:e`/`m:w`/`m:x`/`m:o`). It needs one line in `Triple::computeDataLayout`
   (TargetDataLayout.cpp:592). Without it, Windows and Darwin triples get ELF-style layouts
   (verified, section 6).
4. **Three more Triple behaviors are required:** `getDefaultExceptionHandling()` returns
   `None` for MSIL (otherwise the assert at CodeGenTargetMachineImpl.cpp:83-85 fires or DWARF
   CFI is emitted), plus the helpers `getDefaultObjectFormat()` and `getNativeFormatTriple()`.
5. **MC layer: add `MCContext::IsMSIL` plus a header-only `MCSectionMSIL`**, the same
   pattern as DXContainer. In this checkout `MCSection` and `MCSymbol` have no per-format
   kind enum or virtuals, so neither `MCSection.h` nor `MCSymbol.h` changes. Section-switch
   printing is the virtual `MCAsmInfo::printSwitchToSection`. The TLOF is target-local
   (`MSILTargetObjectFile : TargetLoweringObjectFile`, like SPIR-V, NVPTX and DXIL).
6. **Front-loaded header commit: `Triple.h`, `MCContext.h`, `MCObjectFileInfo.h`** (one
   private method; it could be avoided), plus the new `MCSectionMSIL.h`. **No other widely
   included header has to change** for the object format. `TargetRegistry.h`, `MCSection.h`,
   `MCSymbol.h`, `MCAsmInfo.h`, `TargetMachine.h`, `AsmPrinter.h` and the TLOF headers stay
   as they are (section 5). The same commit must carry the `.cpp` switch cases. In
   particular `MCContext.cpp` must include `MCSectionMSIL.h`, or it **fails to compile**
   (verified).
7. **Backend selection** is a `.cpp`-only branch in `TargetRegistry::lookupTarget(const Triple&)`.
   It is consistent with `registry-tm.md` and included in section 7.6.
8. **Generic code needs a few must-fix edits beyond the switches** (section 3a):
   `TargetMachine::shouldAssumeDSOLocal` ends in an assert that MSIL fails
   (TargetMachine.cpp:266) and is reached from generic DAGCombine. Clang's
   `__builtin___CFStringMakeConstantString` gate (SemaChecking.cpp:3213) needs MSIL in its
   unsupported list.

---

## 1. Triple parsing and normalization

### 1.1 How it works (verified in source)

- `Triple::Triple(std::string&&)` (Triple.cpp:1052-1081) does
  `split(Components, '-', /*MaxSplit*/ 3)`, so everything after the OS stays in
  `Components[3]`: `"gnu-msil"`, `"msvc19.40.0-msil"`, or `"msil"`.
  - `Environment = parseEnvironment(Components[3])` is a `StartsWith` match against
    `TRIPLE_ENV` names (TripleName.def:111-163, in `.def` order). `"gnu-msil"` matches
    `gnu`, `"msvc-msil"` matches `msvc`, `"musl-msil"` matches `musl`, and `"msil"` matches
    nothing (`UnknownEnvironment`). No environment, OS or vendor name is a prefix of
    `"msil"`, and no existing format suffix ends in `msil` (checked).
  - `ObjectFormat = parseFormat(Components[3])` (Triple.cpp:721-733) is an `EndsWith` chain.
    If it finds nothing, `getDefaultFormat(*this)` (Triple.cpp:951-1046) picks the format
    from arch and OS: COFF for Win32/UEFI on x86/arm/aarch64, MachO for Darwin OSes, ELF
    otherwise.
  - Note: `"dxcontainer"` is **not** in `parseFormat()`. DXContainer only comes from the
    `dxil` arch default. Precedent: a format does not have to be both parseable and a
    default.
- `normalize()` (Triple.cpp:1192-1421) splits on every `-`. It parses `Components[4]` as the
  format when present. If `Components[3]` is not a known environment, it tries it as a
  format (lines 1265-1272), which is what makes `aarch64-apple-darwin-msil` work. Special
  case for `OS == Win32` (lines 1360-1384): it does `Components.resize(4)` first. If the
  environment is unknown it writes `"msvc"`, or the format name when the format is neither
  unknown nor COFF. It re-appends `Components[4]` only when the format is **known** and not
  COFF. That is why, unpatched, `x86_64-pc-windows-msvc-msil` normalizes to
  `x86_64-pc-windows-msvc`.
- `getEnvironmentVersionString()` (Triple.cpp:1459-1480) strips `-<format name>` only when
  the format is known. Unpatched, it returns `"-msil"`, which makes stock clang's driver
  reject the triple (`err_drv_triple_version_invalid`, Driver.cpp:1786-1800).
- `setEnvironment` and `setObjectFormat` (Triple.cpp:1692-1708) build `"<env>-<format>"` or
  just `"<format>"`. `setArch`, `setVendor` and `setOSName` keep `getOSAndEnvironmentName()`,
  so the suffix survives them (verified: `get32BitArchVariant()` gives
  `i386-unknown-linux-gnu-msil`, and `setOSName("linux5.10")` gives
  `i386-unknown-linux5.10-gnu-msil`).

### 1.2 Results for the design triples (verified by running the patched code)

Each row is identical for `Triple(S)` and `Triple(Triple::normalize(S))`. `normalize(S) == S`
for every row.

| Triple | arch / vendor / OS / env | fmt today -> patched | `getEnvironmentVersionString()` patched | `getNativeFormatTriple()` |
|---|---|---|---|---|
| `x86_64-unknown-linux-gnu-msil` | x86_64 / unknown / linux / gnu | elf -> **msil** | `""` (today `"-msil"`) | `x86_64-unknown-linux-gnu` |
| `aarch64-unknown-linux-gnu-msil` | aarch64 / unknown / linux / gnu | elf -> **msil** | `""` | `aarch64-unknown-linux-gnu` |
| `x86_64-unknown-linux-musl-msil` | x86_64 / unknown / linux / musl | elf -> **msil** | `""` | `x86_64-unknown-linux-musl` |
| `aarch64-unknown-linux-musl-msil` | aarch64 / unknown / linux / musl | elf -> **msil** | `""` | `aarch64-unknown-linux-musl` |
| `x86_64-pc-windows-msvc-msil` | x86_64 / pc / windows / msvc | coff -> **msil** | `""` | `x86_64-pc-windows-msvc` |
| `aarch64-pc-windows-msvc-msil` | aarch64 / pc / windows / msvc | coff -> **msil** | `""` | `aarch64-pc-windows-msvc` |
| `x86_64-w64-windows-gnu-msil` | x86_64 / **unknown** (w64 is not a VendorType, same as native) / windows / gnu | coff -> **msil** | `""` | `x86_64-w64-windows-gnu` |
| `aarch64-apple-darwin-msil` | aarch64 / apple / darwin / **unknown** | macho -> **msil** | `"msil"` | `aarch64-apple-darwin` |
| `i686-pc-windows-msvc-msil` | x86 / pc / windows / msvc | coff -> **msil** | `""` | `i686-pc-windows-msvc` |

`normalize()` today vs patched for the Windows rows: `x86_64-pc-windows-msvc`,
`aarch64-pc-windows-msvc`, `x86_64-w64-windows-gnu`, `i686-pc-windows-msvc` (**suffix
dropped**) -> the input, unchanged.

Other spellings (patched, verified):
- `x86_64-pc-win32-msil` normalizes to `x86_64-pc-windows-msil`. "msvc" is not inserted.
  This follows the existing `-elf`/`-macho` precedent in `TEST(TripleTest, NormalizeWindows)`
  (TripleTest.cpp:3501-3513). `isWindowsMSVCEnvironment()` is still true (unknown env).
- `x86_64-w64-mingw32-msil` normalizes to `x86_64-w64-windows-gnu-msil`.
- `x86_64-linux-gnu-msil` normalizes to `x86_64-unknown-linux-gnu-msil`.
- `x86_64-pc-windows-msvc19.40.0-msil` (the spelling `MSVCToolChain::ComputeEffectiveClangTriple`
  produces, MSVC.cpp:1003-1010, which already preserves a format suffix) has environment
  version `19.40.0` and native triple `x86_64-pc-windows-msvc19.40.0`.
- `arm64-apple-macosx14.0.0-msil` (what `Darwin::ComputeEffectiveClangTriple` produces via
  `setOSName`, Darwin.cpp:1361) normalizes to itself, with OS version 14.0.0.
- `x86_64-pc-windows-msvcmsil` (glued) parses as MSVC+MSIL, like the existing
  `mipsel-windows-msvccoff` (TripleTest.cpp:3527).

### 1.3 Pitfalls

- **P1. Darwin has no environment component.** In `aarch64-apple-darwin-msil`, `"msil"` sits
  in the environment slot. You get `getEnvironment() == UnknownEnvironment` but
  `hasEnvironment() == true`, `getEnvironmentName() == "msil"` and
  `getEnvironmentVersionString() == "msil"` (the version still parses as empty).
  Consequences:
  - Clang's env-version check (Driver.cpp:1790-1797) passes only because it compares
    against `getObjectFormatTypeName()`.
  - `ToolChain::getTripleWithoutOSVersion()` (ToolChain.cpp:1100-1106) rebuilds the triple
    from the env *type* name and returns `aarch64-apple-darwin-unknown`: `-msil` is lost and
    `unknown` is inserted. For `*-gnu-msil` it returns `...-gnu`, which also loses `-msil`.
    The driver owner must not use it for MSIL runtime directory names.
  - The enum constructor with `UnknownEnvironment` spells the triple
    `aarch64-apple-darwin-unknown-msil`. It parses correctly but is not the canonical string.
- **P2. `normalize(CanonicalForm::FIVE_IDENT)` breaks Darwin-MSIL.** It yields
  `aarch64-apple-darwin-msil-unknown`, whose environment component `"msil-unknown"` no longer
  ends in `msil`, so it reparses as MachO. `THREE_IDENT` and `FOUR_IDENT` drop the format
  for every triple, by design. Only offload code uses these forms today (OffloadBundler.cpp:141-144,
  clang TargetID.cpp:229, AMDGPU), so this is low risk. Do not use them for MSIL.
- **P3. Unpatched tools mis-handle these triples.** Stock clang rejects them through the
  `"-msil"` environment version. Stock `llc` would silently compile them as native ELF/COFF.
  Do not let MSIL bitcode reach unpatched tools.
- **P4. LTO and IR linking.** `Triple::isCompatibleWith(native, msil)` is false for non-Apple
  vendors, so IRMover warns "different target triples" (IRMover.cpp:1511-1517).
  `Triple::merge` for vendor Apple keeps the triple with the newer OS version: merging a
  source `aarch64-apple-macosx14.0.0` into a destination `aarch64-apple-darwin-msil` returns
  `aarch64-apple-macosx14.0.0`, and the MSIL format is **lost** (verified). IRMover sets
  `DstM.setTargetTriple(SrcTriple.merge(DstTriple))` (IRMover.cpp:1519). Recommendation for
  phase 2: `msil-ld` rewrites every input module's triple to the MSIL triple (after checking
  `getNativeFormatTriple()` compatibility) before IRMover runs. If a Triple-level fix is
  preferred instead (it is `.cpp` only, so it does not need to be front-loaded):
  ```cpp
  std::string Triple::merge(const Triple &Other) const {
    if (isOSBinFormatMSIL() != Other.isOSBinFormatMSIL()) {
      Triple M(getNativeFormatTriple().merge(Other.getNativeFormatTriple()));
      M.setObjectFormat(Triple::MSIL);
      return M.str();
    }
    ... existing body ...
  ```
  (*unverified*; not compiled.)
- **P5. Re-setting the default format changes the spelling.** `setObjectFormat(getDefaultObjectFormat())`
  produces `aarch64-apple-darwin-macho` or `...-gnu-elf`, not the bare native string. Use
  `getNativeFormatTriple()` when the exact native spelling matters (sysroot or target
  directory names, handing the triple to native tools).
- **P6. `supportsCOMDAT()` is true for every MSIL triple,** including Darwin, where the native
  answer (MachO) is false. Clang may therefore emit comdats for `*-apple-darwin-msil`. The
  backend must accept and ignore them, or deduplicate them in `msil-ld`.
- **P7. `isAppleMachO()` is false for `aarch64-apple-darwin-msil`.** Its users are clang
  `__MACH__` (still defined through `isOSDarwin()`, OSTargets.cpp:53),
  InitHeaderSearch.cpp:242 (only for `UnknownOS`), and Targets.cpp:140-148 (`isOSDarwin`
  is tested first). No impact.

---

## 2. Precedent commits and the checklist they imply

| Commit | What | Files touched |
|---|---|---|
| `a03ae73c2932` (2019-03) | XCOFF format | clang BackendUtil.cpp, clang CodeGenModule.cpp, lldb GDBRemoteCommunicationClient.cpp, ADT/Triple.h, MC/MCObjectFileInfo.h, MCContext.cpp, MCObjectFileInfo.cpp, MCParser/AsmParser.cpp, Support/Triple.cpp, ARM/AsmParser/ARMAsmParser.cpp, unittests TripleTest.cpp |
| `b3aece05313e` (2020-06) | GOFF format + z/OS | clang BackendUtil.cpp, clang CGObjCMac.cpp, clang CodeGenModule.cpp, ADT/Triple.h, Support/TargetRegistry.h, Bitcode/Writer/BitcodeWriter.cpp, MC/MCObjectFileInfo.cpp, Support/Triple.cpp, Instrumentation/AddressSanitizer.cpp, TripleTest.cpp |
| `a8cfa4b9bda3` (2021-07) | GOFF asm output (MC env, section, symbol) | TargetLoweringObjectFileImpl.h/.cpp, MCContext.h/.cpp, MCObjectFileInfo.h/.cpp, MCSection.h, **MCSectionGOFF.h (new)**, MCSymbol.h, MCSymbolGOFF.h (new), MCParser/AsmParser.cpp, ARMAsmParser.cpp, SystemZTargetMachine.cpp, a test |
| `9130e471fe82` (2022-03) | DXContainer format | ADT/Triple.h, MC/MCContext.h, MC/TargetRegistry.h, BitcodeWriter.cpp, MCContext.cpp, MCObjectFileInfo.cpp, MCParser/AsmParser.cpp, Support/Triple.cpp, ARMAsmParser.cpp, AddressSanitizer.cpp, TripleTest.cpp |
| `6c69427e880b` (2022-04) | SPIR-V format + MC | ADT/Triple.h, MCContext.h, MCObjectFileInfo.h, MCSPIRVObjectWriter.h (new), MCSPIRVStreamer.h (new), MCSection.h, **MCSectionSPIRV.h (new)**, TargetRegistry.h, BitcodeWriter.cpp, MC/CMakeLists.txt, MCAsmBackend.cpp, MCContext.cpp, MCObjectFileInfo.cpp, MCParser/AsmParser.cpp, MCSPIRVStreamer.cpp, SPIRVObjectWriter.cpp, Support/Triple.cpp, ARMAsmParser.cpp, SPIRV target files, AddressSanitizer.cpp, TripleTest.cpp |
| `3adc908b2685` (2022-06) | DXContainer MC support | MCContext.h, MCDXContainerStreamer.h (new), MCDXContainerWriter.h (new), MCObjectFileInfo.h, MCSection.h, **MCSectionDXContainer.h (new)**, TargetRegistry.h, MC/CMakeLists.txt, MCAsmBackend.cpp, MCContext.cpp, MCDXContainerStreamer.cpp, MCDXContainerWriter.cpp, MCObjectFileInfo.cpp, MCSectionDXContainer.cpp |

(Found with `git log -S 'DXContainer,' -- llvm/include/llvm/ADT/Triple.h llvm/include/llvm/TargetParser/Triple.h`
and the same for `SPIRV,`, `GOFF,` and `XCOFF,`. `f09cf34d0062` (2022-12) only moved
Triple to TargetParser.)

How this checkout differs from those commits:
- The `createMCObjectStreamer` switch moved from `TargetRegistry.h` to
  `llvm/lib/MC/TargetRegistry.cpp:49`. **`TargetRegistry.h` no longer needs a change.**
- `MCSection.h` has no `SectionVariant` enum and no virtuals (MCSection.h:566-700).
  Printing goes through `MCAsmInfo::printSwitchToSection` (MCAsmInfo.h:507).
  **`MCSection.h` no longer needs a change.**
- `MCSymbol` has no per-format kind; its `Kind` enum is Regular/Equated/Common
  (MCSymbol.h:45-49). **`MCSymbol.h` does not change**, even if a later `MCSymbolMSIL` is added.
- `MCSectionDXContainer.cpp` no longer exists. Section classes can be header-only.

**Checklist for MSIL** (each item is detailed in sections 3 and 7):
Triple.h · Triple.cpp · TargetDataLayout.cpp · TripleTest.cpp · MCContext.h · MCContext.cpp ·
MCSectionMSIL.h (new) · MCObjectFileInfo.h · MCObjectFileInfo.cpp · TargetRegistry.cpp (streamer
switch + lookup) · MCParser/AsmParser.cpp · ARMAsmParser.cpp · BitcodeWriter.cpp ·
AddressSanitizer.cpp · TargetMachine.cpp · clang BackendUtil.cpp · clang CGObjCMac.cpp ·
clang CodeGenModule.cpp · clang SemaChecking.cpp · lldb GDBRemoteCommunicationClient.cpp (only if lldb is built).

---

## 3. Classification of every object-format use

Search used: `grep -rn "isOSBinFormat\|getObjectFormat()\|getObjectFileType()\|ObjectFormatType\|isAppleMachO\|supportsCOMDAT" llvm clang lld lldb bolt mlir flang polly`.
Counts excluding tests:
- llvm: isOSBinFormatELF 136, COFF 86, MachO 103, Wasm 6, XCOFF 25, GOFF 7, getObjectFormat 74.
- clang: ELF 66, COFF 44, MachO 56, getObjectFormat 13.
- **lld: 0.**
- 182 llvm sites are inside `llvm/lib/Target/<Arch>/` (AArch64 57, ARM 48, X86 27, RISCV 17,
  ...). They are **(c) irrelevant**: once `lookupTarget` selects the MSIL target, those
  backends never see an MSIL triple. The exceptions are ARMAsmParser's `-Wswitch` (below)
  and target code clang reuses through `TargetInfo`, which is listed under clang.

**Important: MSVC `cl.exe` does not warn on missing enum cases by default** (C4061/C4062 are
off), so a missed case compiles silently. In `MCContext::MCContext` that leaves `Env`
uninitialized. Use this checklist, or build the commit once with clang-cl, which has
`-Wswitch` on by default.

### 3a. Must handle MSIL, or it breaks

Verified with clang-cl `-Wswitch` against the patched headers (scratch `check_mc.bat`). The
**unpatched** `.cpp` warns at exactly these lines:

| Site | Construct | Failure without a case | Fix |
|---|---|---|---|
| llvm/lib/TargetParser/Triple.cpp:419 `getObjectFormatTypeName` | switch, `llvm_unreachable` after | unreachable, UB | `case MSIL: return "msil";` |
| llvm/lib/MC/MCContext.cpp:85 ctor | switch over `Triple::ObjectFormatType`, no default | `Env` uninitialized | `case Triple::MSIL: Env = IsMSIL;` |
| llvm/lib/MC/MCContext.cpp:292 `createSymbolImpl` | switch over Env | falls to the generic MCSymbol (OK), but -Wswitch | `case IsMSIL: return new MCSymbol` |
| llvm/lib/MC/MCContext.cpp (whole TU) | `SpecificBumpPtrAllocator<MCSectionMSIL>` member | **compile error**: incomplete type in `~SpecificBumpPtrAllocator` (verified) | `#include "llvm/MC/MCSectionMSIL.h"` |
| llvm/lib/MC/MCObjectFileInfo.cpp:1203 `initMCObjectFileInfo` | switch over Env | no sections, null TextSection, crash in `MCStreamer::initSections` | call `initMSILMCObjectFileInfo` |
| llvm/lib/MC/MCObjectFileInfo.cpp:1233 `getDwarfComdatSection` | switch, unreachable after | UB if reached (DWARF type units) | add to the `report_fatal_error` group |
| llvm/lib/MC/TargetRegistry.cpp:49 `createMCObjectStreamer` | switch, no default | `S == nullptr`, then a null dereference at line 95 | `reportFatalUsageError` (phase 2: `createMSILStreamer`) |
| llvm/lib/MC/MCParser/AsmParser.cpp:781 ctor | switch over Env | null `PlatformParser` dereference (inline or module asm) | `report_fatal_error` |
| llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp:6531 `parsePrefix` | switch over Env | -Wswitch only (ARM-only path) | add to the `llvm_unreachable` group |
| llvm/lib/Bitcode/Writer/BitcodeWriter.cpp:6062 `getSectionNameForBitcode` and :6089 `getSectionNameForCommandline` | switch, unreachable after | UB with `-fembed-bitcode` | `case Triple::MSIL: llvm_unreachable("MSIL is not yet implemented");` (DXContainer precedent) |
| llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp:2080 `getGlobalMetadataSection` | switch, unreachable after | UB with `-fsanitize=address` | add to the `report_fatal_error` group |
| llvm/lib/TargetParser/Triple.cpp:2473 `getDefaultExceptionHandling` | if-chain | returns DwarfCFI (x86/AArch64), which trips the **assert** at CodeGenTargetMachineImpl.cpp:83-85 unless MSIL's MCAsmInfo reports DwarfCFI, which would make AsmPrinter emit `.cfi_*` text | `if (isOSBinFormatMSIL()) return ExceptionHandling::None;` |
| llvm/lib/TargetParser/TargetDataLayout.cpp:592 `computeDataLayout` | per-arch helpers keyed on format | non-native layout (section 6) | redirect to the native triple |
| llvm/lib/Target/TargetMachine.cpp:214-268 `shouldAssumeDSOLocal` | if-chain ending in `assert(ELF \|\| Wasm \|\| XCOFF)` (:266) | **assert** for any non-`dso_local` GV. Reached from generic `TargetLowering::isOffsetFoldingLegal` (TargetLowering.cpp:647, called by DAGCombine), `getTLSModel`, GlobalMerge.cpp:700 | add an MSIL branch (7.7) |
| clang/lib/CodeGen/BackendUtil.cpp:280 `asanUseGlobalsGC` | switch | -Wswitch; falls through to `return false` | add to the Wasm/SPIRV `break` group |
| clang/lib/CodeGen/CGObjCMac.cpp:5942 `CGObjCCommonMac::GetSectionName` | switch, unreachable after | UB (ObjC only) | add to the `report_fatal_error` group |
| clang/lib/CodeGen/CodeGenModule.cpp:7509 (in `GetAddrOfConstantCFString`, :7344) | switch | `llvm_unreachable("unimplemented")` if grouped with DXContainer | add to the `llvm_unreachable("unimplemented")` group **and** gate in Sema (next row) |
| clang/lib/Sema/SemaChecking.cpp:3213 | `{GOFF, XCOFF}` unsupported list for `__builtin___CFStringMakeConstantString` | the builtin reaches CodeGen and hits the unreachable above | add `llvm::Triple::MSIL` to the list |
| lldb/source/Plugins/Process/gdb-remote/GDBRemoteCommunicationClient.cpp:2424 | switch | -Wswitch (only if lldb is built) | add to the "not supported" group |

Switches that already have a `default` (no change needed for milestone 1):
MCAsmBackend.cpp:35 and :69 (`createObjectWriter`/`createDwoObjectWriter`; phase 2 adds
`case Triple::MSIL`), MasmParser.cpp:988, ARMISelLowering.cpp:3672, ARMAsmBackend.cpp:1382,
JITLink.cpp:566, EHFrameSupport.cpp:665, Orc/LLJIT.cpp:1179, Orc/Debugging/DebuggerSupport.cpp:30,
lldb ObjCLanguageRuntime.cpp:44.

### 3b. Behavior changes to note (code that assumes Windows means COFF or Darwin means MachO)

LLVM (generic code reachable with an MSIL triple):
- **Mangler and DataLayout.** Native layouts keep `m:o` (Darwin) and `m:x` (i686 Windows), so
  `Mangler::getNameWithPrefix` (Mangler.cpp:120-176) adds `_`. On i686 it also adds `@N` for
  stdcall and fastcall, and private labels use `L`/`.L`. The MSIL backend must take CIL names
  and P/Invoke entry points from `GlobalValue::getName()` (minus a leading `\1`). Options:
  do not use `getSymbol()` names, or override `MSILTargetObjectFile::getNameWithPrefix`
  (virtual, TargetLoweringObjectFile.h:153). `registry-tm.md` makes the same point.
- **`TargetMachine::isLargeDataSize` / `isLargeGlobalValue`** (TargetMachine.cpp:67-95): the
  non-ELF path depends only on the code model. Harmless.
- **AsmPrinter** (AsmPrinter.cpp): every format test is `isOSBinFormatELF/COFF/MachO/XCOFF`,
  so MSIL skips all format-specific emission. Checked: :546, 557, 592, 784, 1181, 2350,
  2461, 2697, 2722, 2769, 2785, 2990, 3023, 3041, 3159, 3194, 3542, 4066, 4732, 5081, 5183.
  Of those, only `emitGlobalIFunc` (:2785) errors, with "IFuncs are not supported on this
  platform". The COFF-only helpers at :5257/:5322 assert COFF but are called only under
  COFF conditions.
  - :632: CodeView is created for `isOSWindows()` when `MAI.doesSupportDebugInformation()`
    and `llvm.dbg.cu` exist. For Windows-MSIL with `-g`, keep MSIL `MCAsmInfo::SupportsDebugInformation = false`
    (the default) in milestone 1, or override `createDwarfDebug`.
  - :4721 `GetCPISymbol` checks `isWindowsMSVCEnvironment()`, but the COFF cast at :4732 is
    guarded by `isOSBinFormatCOFF()`. Safe.
- **TargetPassConfig.cpp:744** adds `createWindowsSecureHotPatchingPass()` for `isOSWindows()`.
  That pass is inert unless hot-patch attributes or options are present.
- **AtomicExpandPass.cpp:2151**: on Windows x86_64, i128 atomic libcalls use a `<2 x i64>`
  argument. This is the native ABI, so it is correct for P/Invoke.
- **RuntimeLibcalls** (`RuntimeLibcalls.td`) choose libcall sets by arch and OS (`isX86`,
  `isOSWindows`, `isOSDarwin`); only ARM uses `IsOSBinFormatMachO`. MSIL triples get the X86
  or AArch64 libcall set. The ISel owner should decide whether MSIL needs its own
  `SystemRuntimeLibrary`. That would be a `.td` change and a broad rebuild (section 5).
- **TargetLibraryInfo** is OS-based (TargetLibraryInfo.cpp), so MSIL gets native libc
  availability, as intended.
- **Instrumentation and profiling** (InstrProfiling, SanitizerCoverage, GCOV, MemProf,
  EmbedBitcodePass:30): section names are chosen by format. For MSIL they fall to the
  "common" names, or a fatal error for EmbedBitcode. Not milestone-1 work.
- **IR Verifier.cpp:1922** (COFF comdat check) and **Globals.cpp:455** (`canIncreaseAlignment`
  treats unknown formats as non-ELF): harmless.
- **LTO and IRSymtab** (`IRSymtab.cpp:144-296`): COFF-specific handling (`__imp_`, dllimport)
  is off for Windows-MSIL bitcode archives. Revisit for `msil-ld` in phase 2.

Clang (it reuses the native TargetInfo; `clang.md` owns the wrapper):
- `AllocateTarget` (Targets.cpp) selects by arch and OS, so every MSIL triple gets the native
  `TargetInfo` class. The arm/thumb `isOSBinFormatMachO` checks (:217, :634) do not apply to
  x86/aarch64.
- **`UserLabelPrefix` versus the DataLayout global prefix (hard assert).**
  - TargetInfo.cpp:167 sets `"_"` only for MachO, and X86.h:571-578 sets `"_"` only for
    i686 Windows COFF.
  - With the native layout (`m:o`, `m:x`, global prefix `_`), clang's assert at
    Mangle.cpp:266-276, `(UserLabelPrefix.empty() && !GlobalPrefix) || UserLabelPrefix[0] == GlobalPrefix`,
    **fires in +Asserts builds** on every asm label for `aarch64-apple-darwin-msil` and
    `i686-pc-windows-msvc-msil`. Darwin SDK headers use many asm labels
    (`__DARWIN_INODE64` and similar).
  - It does not affect milestone 1 (Linux, `m:e`, prefix `""`).
  - Fix, for when those triples are enabled:
    - TargetInfo.cpp:167: `UserLabelPrefix = Triple.getNativeFormatTriple().isOSBinFormatMachO() ? "_" : "";`
    - X86.h:572: `bool IsWinCOFF = getTriple().isOSWindows() && getTriple().getNativeFormatTriple().isOSBinFormatCOFF();`
      (X86.h:177 has a second `IsWinCOFF`, used for `MaxTLSAlign`; give it the same edit.)
- **`__ELF__`** (InitPreprocessor.cpp:1527) is not defined for `*-linux-*-msil`. `clang.md`
  recommends leaving it undefined.
- **Sema max alignment**: SemaDeclAttr.cpp:4862 caps alignment at 8192 for COFF only, so
  Windows-MSIL allows larger alignments than native. Cosmetic.
- **`supportsIFunc()`** (TargetInfo.h:1572-1585) is false for every MSIL triple, which is
  what we want.
- **dso_local**: clang's static `shouldAssumeDSOLocal` (CodeGenModule.cpp:2321ff; the
  format tests are at :2353-2366) returns true
  for every COFF GV and false for every non-ELF/COFF/MachO GV. MSIL therefore gets no
  `dso_local` at all. Harmless; the backend decides (7.7).
- **`dllimport`/`dllexport`**: `Triple::hasDLLImportExport()` is OS-based (Triple.h:1329),
  so Windows-MSIL accepts `__declspec(dllimport/dllexport)` and emits the DLL storage
  classes. That is useful as a P/Invoke hint. On Linux-MSIL they are ignored, as on native
  Linux.
- **C calling-convention mangling** (stdcall `\01_foo@8` on i686) is OS-based
  (`UseMicrosoftManglingForC`, OSTargets.h:896/917, X86.h:628/992), so it is unchanged.
- **`#pragma comment(lib, ...)`** (CodeGenModule.cpp:3949): ELF uses `llvm.dependent-libraries`.
  Otherwise clang emits `llvm.linker.options` (`/DEFAULTLIB:` on Windows, `-l` elsewhere).
  `msil-ld` could use these as library hints in phase 2. ParsePragma.cpp:3273 accepts
  non-lib `#pragma comment` kinds on Linux-MSIL, which native ELF would ignore.
- **`addUsedOrCompilerUsedGlobal`** (CodeGenModule.cpp:3893): ELF uses `llvm.compiler.used`;
  MSIL uses `llvm.used`. The AsmPrinter must tolerate both.
- **Darwin static-init section** (`getStaticInitSectionSpecifier`) and MSVC
  `__declspec(allocate(".CRT$XCU"))` produce MachO or COFF section names on globals. The MSIL
  TLOF's `getExplicitSectionGlobal` must accept any name and not assert.
- **Driver**: `Driver::getToolChain` (Driver.cpp:6254-6262) sends `Win32` with an unknown
  environment to `Generic_GCC` or `Generic_ELF` by format. MSIL needs its own branch, and
  `clang.md` covers the driver. `ToolChain::getTripleWithoutOSVersion` drops `-msil` (P1).
  `MSVCToolChain::ComputeEffectiveClangTriple` keeps it (verified spelling).

### 3c. Irrelevant

All 182 sites in `llvm/lib/Target/<Arch>/` (see the note at the top of this section), plus:
ExecutionEngine/JITLink/Orc (JIT only); `llvm-jitlink`, `llvm-objdump`, `llvm-ml`, `dsymutil`;
`CGObjCGNU.cpp` and `CGObjCMac.cpp` beyond the switch above (ObjC); `ObjectFilePCHContainerWriter.cpp`
(PCH containers; it would only matter if PCH were built *for* an MSIL triple);
`CrossTranslationUnit.cpp`; CIR; `clang/include/clang/Basic/Attr.td` `TargetELF` and
`TargetELFOrMachO` (defined but unused); lld (0 sites); `cmake/Modules/GetTripleCMakeSystemName.cmake`
and `llvm/utils/get_triple_system_name.py`. That script scans components by prefix: `msil`
matches no OS or environment name, so `x86_64-unknown-linux-gnu-msil` still maps to `Linux`.

---

## 4. MC layer for a text-only (`-S`) MSIL target

### 4.1 What is mandatory

- `MCContext::MCContext` (MCContext.cpp:85-118) has to map `Triple::MSIL` to some `Env`.
  There is no generic environment.
- `MCObjectFileInfo::initMCObjectFileInfo` (MCObjectFileInfo.cpp:1181-1229) must set at
  least `TextSection`, because `MCStreamer::initSections` does
  `switchSectionNoPrint(getTextSection())` (MCStreamer.cpp:397-399).
- `CodeGenTargetMachineImpl::createMCStreamer` (CodeGenTargetMachineImpl.cpp:165-225) needs,
  for `AssemblyFile`, only an `MCInstPrinter`. `createMCAsmBackend` may return null. For
  `ObjectFile` it fails cleanly with "createMCCodeEmitter failed" when no code emitter is
  registered, so `clang -c` errors out in milestone 1 instead of crashing.
- `MCAsmStreamer::switchSection` (MCAsmStreamer.cpp:675-687) calls either the target
  streamer's `changeSection` or `MAI->printSwitchToSection`. Both are virtual and
  target-local, so MSIL can print nothing, or an ILAsm comment.
- `MCStreamer::changeSection` (MCStreamer.cpp:1524-1534) only touches a section's begin
  symbol if it exists. A `nullptr` begin symbol (as in DXContainer and SPIR-V) is fine for
  text output.

### 4.2 Options considered

| Option | Header cost | Verdict |
|---|---|---|
| A. Map `Triple::MSIL` to an existing `Env` (for example `IsELF`) and let the MSIL TLOF return ELF sections | none | **Rejected.** The triple says MSIL while MC says ELF. `initELFMCObjectFileInfo` builds about 50 ELF and DWARF sections, every symbol becomes `MCSymbolELF`, and phase 2 would have to change it anyway, costing a second full rebuild. |
| B. Map it to `IsSPIRV` (a single anonymous section) | none | Rejected. Misleading, and AsmParser reports a SPIR-V error. |
| **C. `IsMSIL` + header-only `MCSectionMSIL` + `MCContext::getMSILSection(Name, Kind)`** (the DXContainer pattern) | MCContext.h (+6 lines), MCObjectFileInfo.h (+1 line, optional) | **Recommended.** Correct now, and it leaves room for phase 2: `MCMSILStreamer`, `MCMSILObjectWriter` (a `Triple::MSIL` case in `MCAsmBackend::createObjectWriter` and `TargetRegistry.cpp:49`) and `MCSymbolMSIL` (MCContext.cpp `createSymbolImpl` plus a new header). None of those later steps touches a widely included header. |

### 4.3 Target-local pieces (no shared header changes)

- `MSILTargetObjectFile final : public TargetLoweringObjectFile` (SPIR-V/DXIL/NVPTX
  precedent; SPIRVTargetObjectFile.h, DirectXTargetMachine.cpp:95-109,
  NVPTXTargetObjectFile.h). It must implement the pure virtuals `getExplicitSectionGlobal`
  (TargetLoweringObjectFile.h:174-176) and `SelectSectionForGlobal` (:316-318). Suggested
  mapping: text goes to `TextSection`, BSS kinds to `BSSSection`, read-only kinds to
  `ReadOnlySection`, everything else to `DataSection`. `getExplicitSectionGlobal` ignores
  the requested name and uses the same mapping. Override `getSectionForConstant` to return
  `ReadOnlySection`. Optionally override `getNameWithPrefix` (3b, Mangler).
  In this checkout the hook is spelled
  `TargetLoweringObjectFile::initialize(MCContext&, const TargetMachine&, const Module&)`,
  lower-case and taking a `Module` (TargetLoweringObjectFile.cpp:41-56). It already calls
  `initMCObjectFileInfo`, which reaches the `IsMSIL` case.
- `MSILMCAsmInfo : MCAsmInfo` (not `MCAsmInfoELF`):
  - Leave `ExceptionsType = None` (the default, MCAsmInfo.h:357), which matches the patched
    `Triple::getDefaultExceptionHandling()`.
  - Leave `SupportsDebugInformation` false.
  - Override `printSwitchToSection` to emit nothing.
  - Leave `UsesCFIWithoutEH` false, so `uwtable` functions get no `.cfi_*`.
- An `MCTargetStreamer` subclass for ILAsm directives, overriding `changeSection`.
- `AsmPrinter` hooks are virtual (AsmPrinter.h): `emitFunctionHeader` :987, `emitLinkage`
  :971, `emitGlobalVariable` :548, `emitXXStructorList` :582 (needed because
  `StaticCtorSection` is null), `emitStartOfAsmFile`/`emitEndOfAsmFile` :632/:636, and
  `doInitialization`/`doFinalization`. **`AsmPrinter.h` should not need a change.** The MSIL
  AsmPrinter should diagnose module-level inline asm before the base `doInitialization`
  creates an AsmParser (AsmPrinter.cpp:611ff), which would hit the `IsMSIL`
  `report_fatal_error`.

---

## 5. Widely included headers that must change (front-load in one commit)

**Required for the object format (all in the first commit):**

| Header | Change | Fan-out (direct includers in llvm+clang+lld) |
|---|---|---|
| `llvm/include/llvm/TargetParser/Triple.h` | enum value `MSIL`; `isOSBinFormatMSIL()`; `getDefaultObjectFormat()`; `getNativeFormatTriple()` | 419 direct, and it is included by `IR/Module.h` (779), clang `TargetInfo.h` and `LangOptions.h`. **Effectively a full rebuild.** |
| `llvm/include/llvm/MC/MCContext.h` | `IsMSIL`, fwd-decl `MCSectionMSIL`, `MSILAllocator`, `MSILUniquingMap`, `getMSILSection()` | 347 direct |
| `llvm/include/llvm/MC/MCObjectFileInfo.h` | private `initMSILMCObjectFileInfo(const Triple&)` (style parity; it could be avoided by inlining in the `.cpp` switch) | 64 direct, but included by `Target/TargetLoweringObjectFile.h` (so all of CodeGen and the targets) |
| `llvm/include/llvm/MC/MCSectionMSIL.h` | **new**, header-only | no rebuild cost |

**Verified as not needed:** `MC/TargetRegistry.h` (the lookup hook goes in `.cpp`, and MSIL
registers with a never-matching `ArchMatchFn`), `MC/MCSection.h`, `MC/MCSymbol.h`,
`MC/MCAsmInfo.h`, `MC/MCStreamer.h` (use an `MCTargetStreamer` subclass), `Target/TargetMachine.h`,
`Target/TargetLoweringObjectFile.h`, `CodeGen/TargetLoweringObjectFileImpl.h`,
`CodeGen/AsmPrinter.h`, `TargetParser/TripleName.def`.

**Other expensive edits other recon keys may need.** Batch them into the same early commit
if the design calls for them, because each one also forces a broad rebuild:
- `llvm/include/llvm/IR/Intrinsics.td` (+ a new `IntrinsicsMSIL.td`), for any `llvm.msil.*`
  intrinsics. `IntrinsicEnums.inc` is included through `IR/Intrinsics.h`, which reaches most
  of LLVM.
- `llvm/include/llvm/IR/CallingConv.h`, for new calling-convention IDs.
- `llvm/include/llvm/IR/RuntimeLibcalls.td`, for an MSIL `SystemRuntimeLibrary`.
  `RuntimeLibcalls.inc` reaches `TargetLowering.h`.
- `clang/include/clang/Basic/Attr.td`, for `msil_fixed_arity` (Design 4.7). It regenerates
  `Attrs.inc` and friends, so all of clang rebuilds.
- `clang/include/clang/Basic/Diagnostic*Kinds.td`, for new diagnostics such as the
  inline-asm rejection.
- clang `TargetInfo.h`, if the clang wrapper needs new virtual hooks (see `clang.md`).
- Adding `MSIL` to `LLVM_ALL_TARGETS` or `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD` regenerates
  `llvm/Config/Targets.def` and friends. Those reach tools only (`TargetSelect.h`), so this
  is moderate.

---

## 6. Data layout

### 6.1 Where it is computed (verified)

- `Triple::computeDataLayout(StringRef ABIName)` lives in
  `llvm/lib/TargetParser/TargetDataLayout.cpp:592` and dispatches on `getArch()`.
- Callers that all have to agree:
  - clang `TargetInfo::resetDataLayout()` (TargetInfo.cpp:225-227).
  - clang BackendUtil.cpp:1265-1267 (fills in a default layout) and :1563-1570 (the
    `err_data_layout_mismatch` check, against `computeDataLayout`).
  - `CodeGenTargetMachineImpl` constructor (CodeGenTargetMachineImpl.cpp:97).
- Because all of them go through `Triple`, a single change is consistent everywhere.
- The mangling component comes from `getManglingComponent()` (TargetDataLayout.cpp:14-24):
  GOFF gives `m:l`; MachO gives `m:o`; `isOSWindowsOrUEFI() && isOSBinFormatCOFF()` gives
  `m:x` on x86 and `m:w` otherwise; XCOFF gives `m:a`; everything else gives `m:e`.
- `computeAArch64DataLayout` (:81-97) picks **whole strings** by `isOSBinFormatMachO()` and
  `isOSBinFormatCOFF()`. `computeX86DataLayout` (:433-481) uses `getManglingComponent` plus
  OS checks. **So the mangling component, and for AArch64 the whole string, depend on the
  object format.**

### 6.2 Layout strings (verified by running the code)

| Triple | Native layout (= patched MSIL layout) | MSIL layout **without** the redirect |
|---|---|---|
| x86_64 linux (gnu or musl) | `e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128` | same |
| x86_64 windows-msvc and windows-gnu | `e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128` | `e-m:e-...` (mangling differs) |
| aarch64 linux (gnu or musl) | `e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32` | same |
| aarch64 darwin | `e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32` | the aarch64 **ELF** string (`m:e`, adds `i8:8:32-i16:16:32`) |
| aarch64 windows-msvc | `e-m:w-p270:32:32-p271:32:32-p272:64:64-p:64:64-i32:32-i64:64-i128:128-n32:64-S128-Fn32` | the aarch64 **ELF** string |
| i686 windows-msvc | `e-m:x-p:32:32-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32-a:0:32-S32` | `e-m:e-...` (mangling differs) |

### 6.3 Proposal

At the top of `Triple::computeDataLayout`, return
`getNativeFormatTriple().computeDataLayout(ABIName)` for MSIL. This is the diff in 7.3; it
was verified to give byte-identical native layouts for all rows above.

`registry-tm.md` proposes a variant: copy `*this` and set the private `ObjectFormat` to
`getDefaultObjectFormat()`. It gives the same result and avoids re-parsing the string.
Either is fine; both need `getDefaultObjectFormat()`. Keep `getNativeFormatTriple()` in
`Triple.h` regardless, because clang and `msil-ld` need the native *spelling*.

Consequences:
- Bitcode from stock clang for the native triple has an identical layout, so there is no
  IRMover "different data layouts" warning (IRMover.cpp:1503-1509).
- Clang's `UserLabelPrefix` must follow the native format on Darwin and i686 Windows (the
  assert in 3b).
- The backend must not rely on `Mangler` names (3b).
- `f80:128` and `p270`-`p272` are harmless.
- On AArch64, `Fn32` lets LLVM assume function addresses are 4-byte aligned (for example,
  folding `(uintptr_t)&f & 3` to 0). CLR code addresses and stubs on ARM64 are 4-byte
  aligned (*unverified for .NET Framework delegate thunks*).

Optional (`.cpp` only): make `Triple::getDefaultLongDoubleFormat()` (Triple.cpp:2552ff)
return `IEEEdouble` for MSIL, to match "long double is double everywhere" (Design 3). Only
modules without a `long-double-type` flag see the difference; clang sets that flag whenever
long double is referenced (CodeGenModule.cpp:1476-1493). `RuntimeLibcallsInfo`
(RuntimeLibcalls.cpp:41) reads the Triple default directly. Coordinate with `clang.md`
(*unverified*; not compiled).

---

## 7. Proposed code changes

Sections 7.1 to 7.6 are the exact diffs I compiled (7.1 to 7.3 also ran). 7.7 onward were not
compiled, because those TUs need tablegen'd headers.

### 7.1 `llvm/include/llvm/TargetParser/Triple.h`
```diff
@@ -426,6 +426,7 @@ enum ObjectFormatType
     ELF,
     GOFF,
     MachO,
+    MSIL,
     SPIRV,
     Wasm,
     XCOFF,
@@ -538,6 +539,16 @@
   /// Get the object format for this triple.
   ObjectFormatType getObjectFormat() const { return ObjectFormat; }
 
+  /// Get the object format implied by the architecture and OS alone, i.e. the
+  /// format this triple would have if it did not name one explicitly.
+  LLVM_ABI ObjectFormatType getDefaultObjectFormat() const;
+
+  /// If this triple's object format is MSIL, return the native platform triple
+  /// it is derived from: the same string with the "msil" format suffix removed,
+  /// so the object format is the platform default (ELF, COFF or MachO).
+  /// Otherwise return *this unchanged.
+  LLVM_ABI Triple getNativeFormatTriple() const;
+
@@ -886,6 +897,10 @@
     return getObjectFormat() == Triple::DXContainer;
   }
 
+  /// Tests whether the target emits MSIL (CIL) for a .NET assembly. The rest
+  /// of the triple names the native platform whose C ABI the code follows.
+  bool isOSBinFormatMSIL() const { return getObjectFormat() == Triple::MSIL; }
+
```
The enum value is inserted in alphabetical order, as upstream did for DXContainer and SPIRV.
No code persists `ObjectFormatType` numerically: bitcode, PCMs and lldb store the triple
string (grep for `ObjectFormatType` found no tables or serialization). Appending after
`XCOFF` would work equally well.

### 7.2 `llvm/lib/TargetParser/Triple.cpp`
```diff
@@ -427,6 +427,8 @@ StringRef Triple::getObjectFormatTypeName(ObjectFormatType Kind) {
     return "goff";
   case MachO:
     return "macho";
+  case MSIL:
+    return "msil";
   case Wasm:
@@ -727,6 +729,7 @@ static Triple::ObjectFormatType parseFormat(StringRef EnvironmentName) {
       .EndsWith("elf", Triple::ELF)
       .EndsWith("goff", Triple::GOFF)
       .EndsWith("macho", Triple::MachO)
+      .EndsWith("msil", Triple::MSIL)
       .EndsWith("wasm", Triple::Wasm)
@@ -1045,6 +1048,23 @@  (right after static getDefaultFormat())
   llvm_unreachable("unknown architecture");
 }
 
+Triple::ObjectFormatType Triple::getDefaultObjectFormat() const {
+  return getDefaultFormat(*this);
+}
+
+Triple Triple::getNativeFormatTriple() const {
+  if (!isOSBinFormatMSIL())
+    return *this;
+  // parseFormat() matched a trailing "msil" on the environment component,
+  // e.g. "gnu-msil", "msvc19.40.0-msil", or just "msil" (no environment).
+  StringRef Env = getEnvironmentName();
+  if (!Env.consume_back("-msil"))
+    Env.consume_back("msil");
+  if (Env.empty())
+    return Triple(getArchName(), getVendorName(), getOSName());
+  return Triple(getArchName(), getVendorName(), getOSName(), Env);
+}
+
@@ -2471,6 +2491,10 @@
 ExceptionHandling Triple::getDefaultExceptionHandling() const {
+  // MSIL has no native unwinder; managed exceptions are emitted explicitly.
+  if (isOSBinFormatMSIL())
+    return ExceptionHandling::None;
+
   if (isOSBinFormatCOFF()) {
```
`getDefaultFormat()` stays as it is: MSIL is never a default. `normalize()` needs **no
change**, because it already preserves a known non-COFF format on Windows (lines 1378-1384).

### 7.3 `llvm/lib/TargetParser/TargetDataLayout.cpp`
```diff
@@ -590,6 +590,12 @@
 std::string Triple::computeDataLayout(StringRef ABIName) const {
+  // MSIL code keeps the exact data layout (including the symbol mangling
+  // component) of the native platform it targets, so C struct layout and
+  // bitcode produced by a stock clang for that platform match.
+  if (isOSBinFormatMSIL())
+    return getNativeFormatTriple().computeDataLayout(ABIName);
+
   switch (getArch()) {
```

### 7.4 `llvm/unittests/TargetParser/TripleTest.cpp`

Add a new test, for example after `TEST(TripleTest, FileFormat)` (TripleTest.cpp:3181-3290).
It ran green through the gtest stand-in:

```cpp
TEST(TripleTest, MSILObjectFormat) {
  struct {
    const char *Str;
    Triple::ArchType Arch;
    Triple::VendorType Vendor;
    Triple::OSType OS;
    Triple::EnvironmentType Env;
    const char *Native;
  } Cases[] = {
      {"x86_64-unknown-linux-gnu-msil", Triple::x86_64, Triple::UnknownVendor,
       Triple::Linux, Triple::GNU, "x86_64-unknown-linux-gnu"},
      {"aarch64-unknown-linux-gnu-msil", Triple::aarch64,
       Triple::UnknownVendor, Triple::Linux, Triple::GNU,
       "aarch64-unknown-linux-gnu"},
      {"x86_64-unknown-linux-musl-msil", Triple::x86_64, Triple::UnknownVendor,
       Triple::Linux, Triple::Musl, "x86_64-unknown-linux-musl"},
      {"aarch64-unknown-linux-musl-msil", Triple::aarch64,
       Triple::UnknownVendor, Triple::Linux, Triple::Musl,
       "aarch64-unknown-linux-musl"},
      {"x86_64-pc-windows-msvc-msil", Triple::x86_64, Triple::PC,
       Triple::Win32, Triple::MSVC, "x86_64-pc-windows-msvc"},
      {"aarch64-pc-windows-msvc-msil", Triple::aarch64, Triple::PC,
       Triple::Win32, Triple::MSVC, "aarch64-pc-windows-msvc"},
      {"x86_64-w64-windows-gnu-msil", Triple::x86_64, Triple::UnknownVendor,
       Triple::Win32, Triple::GNU, "x86_64-w64-windows-gnu"},
      {"aarch64-apple-darwin-msil", Triple::aarch64, Triple::Apple,
       Triple::Darwin, Triple::UnknownEnvironment, "aarch64-apple-darwin"},
      {"i686-pc-windows-msvc-msil", Triple::x86, Triple::PC, Triple::Win32,
       Triple::MSVC, "i686-pc-windows-msvc"},
  };
  for (const auto &C : Cases) {
    Triple T(C.Str);
    EXPECT_EQ(C.Arch, T.getArch()) << C.Str;
    EXPECT_EQ(C.Vendor, T.getVendor()) << C.Str;
    EXPECT_EQ(C.OS, T.getOS()) << C.Str;
    EXPECT_EQ(C.Env, T.getEnvironment()) << C.Str;
    EXPECT_EQ(Triple::MSIL, T.getObjectFormat()) << C.Str;
    EXPECT_TRUE(T.isOSBinFormatMSIL()) << C.Str;
    EXPECT_FALSE(T.isOSBinFormatELF() || T.isOSBinFormatCOFF() ||
                 T.isOSBinFormatMachO())
        << C.Str;
    // These spellings are already canonical; normalize() must keep "-msil"
    // (it used to drop it on Windows triples).
    EXPECT_EQ(C.Str, Triple::normalize(C.Str)) << C.Str;
    // The native triple is the same string without the format suffix.
    Triple N = T.getNativeFormatTriple();
    EXPECT_EQ(C.Native, N.str()) << C.Str;
    EXPECT_EQ(T.getDefaultObjectFormat(), N.getObjectFormat()) << C.Str;
    EXPECT_FALSE(N.isOSBinFormatMSIL()) << C.Str;
    // MSIL keeps the native data layout, including the mangling mode.
    EXPECT_EQ(N.computeDataLayout(), T.computeDataLayout()) << C.Str;
    EXPECT_EQ(ExceptionHandling::None, T.getDefaultExceptionHandling())
        << C.Str;
    EXPECT_TRUE(T.getEnvironmentVersion().empty()) << C.Str;
  }

  EXPECT_EQ("msil", Triple::getObjectFormatTypeName(Triple::MSIL));

  EXPECT_EQ("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-"
            "n8:16:32:64-S128",
            Triple("x86_64-unknown-linux-gnu-msil").computeDataLayout());
  EXPECT_EQ("e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-"
            "n8:16:32:64-S128",
            Triple("x86_64-pc-windows-msvc-msil").computeDataLayout());
  EXPECT_EQ("e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-"
            "i128:128-n32:64-S128-Fn32",
            Triple("aarch64-unknown-linux-gnu-msil").computeDataLayout());
  EXPECT_EQ("e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-"
            "S128-Fn32",
            Triple("aarch64-apple-darwin-msil").computeDataLayout());
  EXPECT_EQ("e-m:w-p270:32:32-p271:32:32-p272:64:64-p:64:64-i32:32-i64:64-"
            "i128:128-n32:64-S128-Fn32",
            Triple("aarch64-pc-windows-msvc-msil").computeDataLayout());
  EXPECT_EQ("e-m:x-p:32:32-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-"
            "f80:128-n8:16:32-a:0:32-S32",
            Triple("i686-pc-windows-msvc-msil").computeDataLayout());

  // Darwin has no environment component: "msil" sits in the environment slot.
  Triple D("aarch64-apple-darwin-msil");
  EXPECT_TRUE(D.isOSDarwin());
  EXPECT_TRUE(D.isMacOSX());
  EXPECT_FALSE(D.isAppleMachO());
  EXPECT_TRUE(D.hasEnvironment());
  EXPECT_EQ("msil", D.getEnvironmentName());
  EXPECT_EQ("msil", D.getEnvironmentVersionString());
  EXPECT_EQ("arm64-apple-macosx14.0.0-msil",
            Triple::normalize("arm64-apple-macosx14.0.0-msil"));
  EXPECT_EQ(VersionTuple(14, 0, 0),
            Triple("arm64-apple-macosx14.0.0-msil").getOSVersion());

  // Other spellings users and the driver produce.
  EXPECT_EQ("x86_64-pc-windows-msil", Triple::normalize("x86_64-pc-win32-msil"));
  EXPECT_EQ("x86_64-w64-windows-gnu-msil",
            Triple::normalize("x86_64-w64-mingw32-msil"));
  EXPECT_EQ("x86_64-unknown-linux-gnu-msil",
            Triple::normalize("x86_64-linux-gnu-msil"));
  Triple V("x86_64-pc-windows-msvc19.40.0-msil"); // MSVCToolChain spelling
  EXPECT_EQ(Triple::MSIL, V.getObjectFormat());
  EXPECT_EQ(VersionTuple(19, 40, 0), V.getEnvironmentVersion());
  EXPECT_EQ("x86_64-pc-windows-msvc19.40.0", V.getNativeFormatTriple().str());

  // Mutators keep or add the suffix.
  Triple M("aarch64-apple-darwin");
  M.setObjectFormat(Triple::MSIL);
  EXPECT_EQ("aarch64-apple-darwin-msil", M.str());
  Triple W("x86_64-pc-windows-msvc");
  W.setObjectFormat(Triple::MSIL);
  EXPECT_EQ("x86_64-pc-windows-msvc-msil", W.str());
  W.setEnvironment(Triple::GNU);
  EXPECT_EQ("x86_64-pc-windows-gnu-msil", W.str());
  EXPECT_EQ("i386-unknown-linux-gnu-msil",
            Triple("x86_64-unknown-linux-gnu-msil").get32BitArchVariant().str());
  Triple E(Triple::x86_64, Triple::NoSubArch, Triple::UnknownVendor,
           Triple::Linux, Triple::GNU, Triple::MSIL);
  EXPECT_EQ("x86_64-unknown-linux-gnu-msil", E.str());

  // Non-MSIL triples are returned unchanged.
  EXPECT_EQ("x86_64-unknown-linux-gnu",
            Triple("x86_64-unknown-linux-gnu").getNativeFormatTriple().str());
  EXPECT_EQ(Triple::ELF,
            Triple("x86_64-unknown-linux-gnu-msil").getDefaultObjectFormat());
  EXPECT_EQ(Triple::COFF,
            Triple("x86_64-pc-windows-msvc-msil").getDefaultObjectFormat());
  EXPECT_EQ(Triple::MachO,
            Triple("aarch64-apple-darwin-msil").getDefaultObjectFormat());

  // A native and an MSIL triple are not compatible (IRMover warns), and
  // merge() keeps the destination for non-Apple vendors.
  EXPECT_FALSE(Triple("x86_64-unknown-linux-gnu")
                   .isCompatibleWith(Triple("x86_64-unknown-linux-gnu-msil")));
  EXPECT_EQ("x86_64-unknown-linux-gnu-msil",
            Triple("x86_64-unknown-linux-gnu")
                .merge(Triple("x86_64-unknown-linux-gnu-msil")));
}
```

Add these lines to existing tests (also verified):
```cpp
// TEST(TripleTest, FileFormat), after the `T.setObjectFormat(Triple::SPIRV)` block (~:3281):
  T.setObjectFormat(Triple::MSIL);
  EXPECT_EQ(Triple::MSIL, T.getObjectFormat());
  EXPECT_EQ("msil", Triple::getObjectFormatTypeName(T.getObjectFormat()));
  EXPECT_EQ(Triple::MSIL, Triple("x86_64-unknown-linux-gnu-msil").getObjectFormat());
  EXPECT_EQ(Triple::MSIL, Triple("x86_64-pc-windows-msvc-msil").getObjectFormat());
  EXPECT_EQ(Triple::MSIL, Triple("aarch64-apple-darwin-msil").getObjectFormat());
  Triple MSVCMSILNormalized(Triple::normalize("x86_64-pc-windows-msvc-msil"));
  EXPECT_EQ(Triple::MSIL, MSVCMSILNormalized.getObjectFormat());

// TEST(TripleTest, DefaultExceptionHandling) (:3316):
  EXPECT_EQ(ExceptionHandling::None,
            Triple("x86_64-unknown-linux-gnu-msil").getDefaultExceptionHandling());
  EXPECT_EQ(ExceptionHandling::None,
            Triple("x86_64-pc-windows-msvc-msil").getDefaultExceptionHandling());
  EXPECT_EQ(ExceptionHandling::None,
            Triple("i686-pc-windows-msvc-msil").getDefaultExceptionHandling());
  EXPECT_EQ(ExceptionHandling::None,
            Triple("aarch64-apple-darwin-msil").getDefaultExceptionHandling());

// TEST(TripleTest, NormalizeWindows) (:3475):
  EXPECT_EQ("x86_64-pc-windows-msvc-msil",
            Triple::normalize("x86_64-pc-windows-msvc-msil"));
  EXPECT_EQ("i686-pc-windows-msvc-msil", Triple::normalize("i686-pc-windows-msvc-msil"));
  EXPECT_EQ("x86_64-w64-windows-gnu-msil",
            Triple::normalize("x86_64-w64-windows-gnu-msil"));
  EXPECT_EQ("x86_64-pc-windows-msil", Triple::normalize("x86_64-pc-win32-msil"));
  EXPECT_EQ("x86_64-w64-windows-gnu-msil", Triple::normalize("x86_64-w64-mingw32-msil"));

// TEST(TripleTest, isCompatibleWith) Cases[] (:3784):
      {"x86_64-unknown-linux-gnu", "x86_64-unknown-linux-gnu-msil", false},
      {"x86_64-pc-windows-msvc", "x86_64-pc-windows-msvc-msil", false},
```

### 7.5 MC headers

New file `llvm/include/llvm/MC/MCSectionMSIL.h` (verified to compile):
```cpp
//===- MCSectionMSIL.h - MSIL (.NET CIL) Machine Code Sections --*- C++ -*-===//
// ... standard LLVM license header ...
// MSIL output is an ECMA-335 assembly, which has no sections in the ELF/COFF
// sense; these sections are logical buckets (method bodies, initialized data,
// zero-initialized data, read-only data) that the MSIL AsmPrinter and, later,
// the MSIL object writer use to group what they emit.
#ifndef LLVM_MC_MCSECTIONMSIL_H
#define LLVM_MC_MCSECTIONMSIL_H

#include "llvm/MC/MCSection.h"
#include "llvm/MC/SectionKind.h"

namespace llvm {

class MCSymbol;

class MCSectionMSIL final : public MCSection {
  friend class MCContext;

  SectionKind Kind;

  MCSectionMSIL(StringRef Name, SectionKind K, MCSymbol *Begin)
      : MCSection(Name, K.isText(), /*IsBss=*/K.isBSS(), Begin), Kind(K) {}

public:
  SectionKind getKind() const { return Kind; }
};

} // end namespace llvm

#endif // LLVM_MC_MCSECTIONMSIL_H
```

`llvm/include/llvm/MC/MCContext.h`:
```diff
@@ -56,6 +56,7 @@
 class MCSectionDXContainer;
 class MCSectionELF;
 class MCSectionMachO;
+class MCSectionMSIL;
 class MCSectionSPIRV;
@@ -94,7 +95,8 @@ enum Environment
     IsSPIRV,
     IsWasm,
     IsXCOFF,
-    IsDXContainer
+    IsDXContainer,
+    IsMSIL
   };
@@ -143,6 +145,7 @@
   SpecificBumpPtrAllocator<MCSectionMachO> MachOAllocator;
+  SpecificBumpPtrAllocator<MCSectionMSIL> MSILAllocator;
   SpecificBumpPtrAllocator<MCSectionGOFF> GOFFAllocator;
@@ -319,6 +322,7 @@
   StringMap<MCSectionDXContainer *> DXCUniquingMap;
+  StringMap<MCSectionMSIL *> MSILUniquingMap;
   StringMap<bool> RelSecNames;
@@ -653,6 +657,10 @@
   LLVM_ABI MCSectionDXContainer *getDXContainerSection(StringRef Section,
                                                        SectionKind K);
 
+  /// Get (or create) the MSIL section with the given name. MSIL sections are
+  /// logical groupings inside a .NET assembly (code, data, bss, rodata).
+  LLVM_ABI MCSectionMSIL *getMSILSection(StringRef Section, SectionKind K);
+
```

`llvm/include/llvm/MC/MCObjectFileInfo.h` (:461):
```diff
   void initDXContainerObjectFileInfo(const Triple &T);
+  void initMSILMCObjectFileInfo(const Triple &T);
```

### 7.6 MC sources (all passed `-fsyntax-only -Wswitch` with no warnings)

`llvm/lib/MC/MCContext.cpp`:
```diff
@@ -27,6 +27,7 @@
 #include "llvm/MC/MCSectionGOFF.h"
+#include "llvm/MC/MCSectionMSIL.h"
 #include "llvm/MC/MCSectionMachO.h"
@@ -112,6 +113,9 @@  (constructor switch)
   case Triple::SPIRV:
     Env = IsSPIRV;
     break;
+  case Triple::MSIL:
+    Env = IsMSIL;
+    break;
@@ -151,6 +155,7 @@  (reset)
   MachOAllocator.DestroyAll();
+  MSILAllocator.DestroyAll();
@@ -180,6 +185,7 @@
   DXCUniquingMap.clear();
+  MSILUniquingMap.clear();
@@ -306,6 +312,10 @@  (createSymbolImpl)
   case MCContext::IsSPIRV:
     return new (Name, *this) MCSymbol(Name, IsTemporary);
+  case MCContext::IsMSIL:
+    // Phase 2 may introduce MCSymbolMSIL (typed signatures, like
+    // MCSymbolWasm); that only touches this switch and a new header.
+    return new (Name, *this) MCSymbol(Name, IsTemporary);
   }
@@ -948,6 +958,18 @@  (after getDXContainerSection)
+MCSectionMSIL *MCContext::getMSILSection(StringRef Section, SectionKind K) {
+  auto [It, Inserted] = MSILUniquingMap.try_emplace(Section);
+  if (!Inserted)
+    return It->second;
+  // The section keeps a StringRef to its name; use the map's copy. No begin
+  // symbol for now (like DXContainer/SPIR-V); the phase-2 object writer can
+  // add one with createRenamableSymbol() as getWasmSection() does.
+  StringRef Name = It->first();
+  It->second = new (MSILAllocator.Allocate()) MCSectionMSIL(Name, K, nullptr);
+  return It->second;
+}
```

`llvm/lib/MC/MCObjectFileInfo.cpp`:
```diff
@@ -21,6 +21,7 @@
 #include "llvm/MC/MCSectionGOFF.h"
+#include "llvm/MC/MCSectionMSIL.h"
@@ -1171,6 +1172,18 @@  (before initDXContainerObjectFileInfo)
+void MCObjectFileInfo::initMSILMCObjectFileInfo(const Triple &T) {
+  // Logical sections of a .NET assembly. The MSIL target's
+  // TargetLoweringObjectFile maps every global to one of these.
+  TextSection = Ctx->getMSILSection(".text", SectionKind::getText());
+  DataSection = Ctx->getMSILSection(".data", SectionKind::getData());
+  BSSSection = Ctx->getMSILSection(".bss", SectionKind::getBSS());
+  ReadOnlySection =
+      Ctx->getMSILSection(".rodata", SectionKind::getReadOnly());
+  // No DWARF, EH or TLS sections: debug info will be Portable PDB, EH is
+  // managed, and thread_local is lowered to [ThreadStatic] storage.
+}
@@ -1225,6 +1238,9 @@  (initMCObjectFileInfo switch)
   case MCContext::IsDXContainer:
     initDXContainerObjectFileInfo(TheTriple);
     break;
+  case MCContext::IsMSIL:
+    initMSILMCObjectFileInfo(TheTriple);
+    break;
@@ -1243,6 +1259,7 @@  (getDwarfComdatSection)
   case Triple::DXContainer:
+  case Triple::MSIL:
   case Triple::UnknownObjectFormat:
```

`llvm/lib/MC/TargetRegistry.cpp` (the streamer switch, plus backend selection consistent
with `registry-tm.md`, which also covers `-march=msil` in the `ArchName` overload):
```diff
@@ -90,6 +90,11 @@  (createMCObjectStreamer)
     S = createDXContainerStreamer(Ctx, std::move(TAB), std::move(OW),
                                   std::move(Emitter));
     break;
+  case Triple::MSIL:
+    // Phase 2: createMSILStreamer(...). Unreachable in milestone 1 because the
+    // MSIL target registers no MCCodeEmitter, so CodeGenTargetMachineImpl::
+    // createMCStreamer fails earlier with "createMCCodeEmitter failed".
+    reportFatalUsageError("MSIL object file emission is not implemented yet");
   }
@@ -165,6 +170,22 @@  (lookupTarget(const Triple &TT, std::string &Error))
     return nullptr;
   }
+
+  // An MSIL triple keeps its native architecture (x86_64, aarch64, ...) but is
+  // always compiled by the MSIL target. That target registers an ArchMatchFn
+  // that never matches, so it is only ever selected here, by object format.
+  if (TT.isOSBinFormatMSIL()) {
+    auto I = find_if(targets(), [](const Target &T) {
+      return StringRef(T.getName()) == "msil";
+    });
+    if (I == targets().end()) {
+      Error = "No available targets are compatible with triple \"" + TT.str() +
+              "\" (the MSIL target is not built)";
+      return nullptr;
+    }
+    return &*I;
+  }
+
   Triple::ArchType Arch = TT.getArch();
```
The MSIL target then registers with
`TargetRegistry::RegisterTarget(getTheMSILTarget(), "msil", "MSIL (.NET CIL)", "MSIL", [](Triple::ArchType) { return false; })`.
Do **not** use `RegisterTarget<Triple::UnknownArch>`: it would match `unknown-*` triples.

`llvm/lib/MC/MCParser/AsmParser.cpp` (:805):
```diff
   case MCContext::IsDXContainer:
     report_fatal_error("DXContainer is not supported yet");
     break;
+  case MCContext::IsMSIL:
+    // Reached for module-level or inline asm in IR; clang rejects inline asm
+    // for MSIL, and the MSIL AsmPrinter should diagnose module asm first.
+    report_fatal_error("assembly parsing is not supported for MSIL");
+    break;
   }
```

### 7.7 Other must-fix sites (not compiled; they need tablegen'd headers)

`llvm/lib/Target/TargetMachine.cpp`, in `shouldAssumeDSOLocal`, before the MachO block (:260):
```diff
   if (TT.isOSBinFormatGOFF())
     return true;
 
+  // MSIL: everything defined in the module lives in the same assembly and
+  // there is no GOT/PLT; declarations may be P/Invokes or other assemblies.
+  if (TT.isOSBinFormatMSIL())
+    return !GV->isDeclarationForLinker();
+
   if (TT.isOSBinFormatMachO()) {
```
(The MSIL ISel may still override `isOffsetFoldingLegal`, as WebAssembly does at
WebAssemblyISelLowering.cpp:1058, but the assert must go regardless.)

`llvm/lib/Bitcode/Writer/BitcodeWriter.cpp`, in both `getSectionNameForBitcode` (:6061) and
`getSectionNameForCommandline` (:6088):
```diff
   case Triple::DXContainer:
     llvm_unreachable("DXContainer is not yet implemented");
     break;
+  case Triple::MSIL:
+    llvm_unreachable("MSIL is not yet implemented");
+    break;
   }
```
`llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp:2080`:
`case Triple::DXContainer:` followed by `+  case Triple::MSIL:` (falls into
`report_fatal_error`).

`llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp:6547`:
`case MCContext::IsDXContainer:` followed by `+  case MCContext::IsMSIL:` (falls into
`llvm_unreachable`).

`clang/lib/CodeGen/BackendUtil.cpp:291`: `case Triple::DXContainer:` followed by
`+  case Triple::MSIL:` (the `break` group).

`clang/lib/CodeGen/CGObjCMac.cpp:5960`: `case llvm::Triple::DXContainer:` followed by
`+  case llvm::Triple::MSIL:` (the `report_fatal_error` group).

`clang/lib/CodeGen/CodeGenModule.cpp:7512`: `case llvm::Triple::DXContainer:` followed by
`+  case llvm::Triple::MSIL:` (the `llvm_unreachable("unimplemented")` group).

`clang/lib/Sema/SemaChecking.cpp:3215`: `{llvm::Triple::GOFF, llvm::Triple::XCOFF}` becomes
`{llvm::Triple::GOFF, llvm::Triple::XCOFF, llvm::Triple::MSIL}`.

`lldb/.../GDBRemoteCommunicationClient.cpp:2438`: `case llvm::Triple::DXContainer:`
followed by `+        case llvm::Triple::MSIL:`.

Phase 2 (not now): `MCAsmBackend.cpp:35`/`:69` gets `case Triple::MSIL:` returning an
`MCMSILObjectWriter`; `TargetRegistry.cpp:49` calls `createMSILStreamer`; `MCContext.cpp:292`
optionally returns `MCSymbolMSIL`; `Object/Binary.h` gets an `ID_MSIL` (a moderately wide
header); and possibly the `Triple::merge`/`isCompatibleWith` change in P4.

---

## 8. Suggested commit plan for this area

1. **"[MSIL] Add the msil object format to Triple and MC"** (the one expensive rebuild):
   - Triple.h/.cpp (7.1, 7.2), TargetDataLayout.cpp (7.3), TripleTest.cpp (7.4).
   - MCSectionMSIL.h, MCContext.h/.cpp, MCObjectFileInfo.h/.cpp, TargetRegistry.cpp
     (streamer case; lookup branch optional here), AsmParser.cpp (7.5, 7.6).
   - Every switch case in 7.7, plus TargetMachine.cpp.
   - The other broad-rebuild headers from section 5 that the plan already knows it needs
     (intrinsics, `Attr.td`, diagnostics).
   - Test: `TargetParserTests --gtest_filter=TripleTest.*`. The existing suite should pass
     unchanged: no existing test string ends in `msil` (grep found no `msil` anywhere in
     llvm/test, clang/test or the unittests).
2. The MSIL target skeleton and the `lookupTarget` branch (`.cpp` only, cheap).
3. Clang wrapper and driver (`clang.md`). The `UserLabelPrefix` fixes (3b) can wait for the
   Darwin and i686 triples.

---

## 9. Verification log and how to reproduce

Scratch directory (session-local):
`C:\Users\JEROME~1\AppData\Local\Temp\claude\D--llvm-msil\fa776849-ce33-4cee-bcc7-05382d063d3e\scratchpad\recon\triple-objfmt\`.

- `cfg/llvm/Config/{llvm-config,abi-breaking,config}.h`: generated with `sed` from the
  `.cmake` templates (`#cmakedefine01` becomes 0, version 24.0.0).
  `cfg/llvm/TargetParser/{AMDGPU,R600}TargetParserDef.inc` are stubs that only supply
  `NUM_FEATURES` and `R600_NUM_FEATURES`.
- `build.bat` compiles Triple.cpp, TargetDataLayout.cpp, Support/{StringRef,VersionTuple,Twine,
  raw_ostream,NativeFormatting,SmallVector,MemAlloc,StringMap}.cpp,
  TargetParser/ARMTargetParser{,Common}.cpp and `main.cpp` with clang-cl
  `/std:c++17 /MD /O1`. It links with `link /FORCE:UNRESOLVED`; only error-path symbols
  stay unresolved. It builds twice: `out/base` (tree as-is) and `out/patched`
  (`-I patched/inc` first).
- `out/base.txt` and `out/patched.txt` hold the full dumps behind section 1.2 and 6.2.
  `build_nodl.bat` builds patched Triple with the original TargetDataLayout.cpp, which gives
  the right-hand column of 6.2.
- `check_mc.bat`: `-fsyntax-only -Wswitch` of the patched MC sources (clean) and of the
  original sources against the patched headers. That run produced the warning list in 3a and
  the `MCContext.cpp` incomplete-type error.
- `run_shim.bat` + `shim_test.cpp` + `msil_triple_test.inc` + `existing_test_additions.inc`:
  the section 7.4 tests through a 30-line gtest stand-in. Result: 175 checks, 0 failures.
- Unified diffs of every verified edit are in `d_*.diff`.

Nothing was downloaded. No CMake, ninja or tablegen was run, and no tracked file was
modified.

---

## 10. Open questions and risks

1. **Name of the helper.** `getNativeFormatTriple()` (mine) versus a field-copy inside
   `computeDataLayout` (`registry-tm.md`). Both need `getDefaultObjectFormat()`. Settle on
   one name before the header commit; renaming later means another full rebuild.
2. **Windows `normalize("x86_64-pc-win32-msil")`** gives `x86_64-pc-windows-msil` (no
   `msvc`). This is consistent with `-elf`/`-macho`. If the driver wants `...-msvc-msil`,
   it must set the environment itself.
3. **Long double default in Triple** for MSIL (6.3, optional). Needs agreement with `clang.md`.
4. **`supportsCOMDAT()`** stays true for MSIL (Darwin-MSIL differs from native). The backend
   and `msil-ld` must accept comdats.
5. **`Fn32` on AArch64** (function-pointer alignment assumption): confirm with .NET
   Framework delegate thunks (phase 3).
6. **The Darwin no-environment spelling** shows up in tools that treat `hasEnvironment()` as
   "has a real environment" (P1). Grep any new driver code for `getEnvironmentName()` and
   `hasEnvironment()`.
