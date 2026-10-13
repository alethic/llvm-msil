# MSIL recon: target selection and TargetMachine basics (`registry-tm`)

Status: recon report, 2026-10-12. Read-only investigation of the checkout at
`D:\llvm-msil` (branch `msil-milestone1`, HEAD `8da03434abcc`). No tracked file was
modified. Line numbers refer to that HEAD.

Scope: how a triple such as `x86_64-unknown-linux-gnu-msil` reaches an `msil`
target in every tool, CMake registration of an experimental `MSIL` target, the
DataLayout contract between clang and the backend, how the subtarget should treat
native CPU names and features, what TTI/TLI the target needs, and which
target-specific IR clang hands the backend at `-O2`.

Assumed from the triple work (owned by another recon report, names used below):
`Triple::MSIL` (new `ObjectFormatType`), `Triple::isOSBinFormatMSIL()`,
`Triple::getObjectFormatTypeName(MSIL) == "msil"`, and `parseFormat()` mapping an
environment component ending in `msil` to `Triple::MSIL`.

---

## 0. Summary of decisions proposed here

| Topic | Proposal |
|---|---|
| Target selection | One branch in `TargetRegistry::lookupTarget(const Triple&)`: if `TT.isOSBinFormatMSIL()`, return the target named `"msil"`. The MSIL target registers with an ArchMatchFn that never matches (the `arm64`/`amdgcn`/`bpf` precedent), so it never competes with X86/AArch64. In `lookupTarget(ArchName, Triple&)`, `-march=msil` keeps the arch and sets the object format to MSIL. `-march=<other>` with an MSIL triple is an error. |
| CMake | Add `MSIL` to `LLVM_ALL_EXPERIMENTAL_TARGETS` (llvm/CMakeLists.txt:588) and `LLVM_HAS_MSIL_TARGET` to `llvm/include/llvm/Config/Targets.h.cmake`. Build with `-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MSIL`. Everything else (Targets.def, AsmPrinters.def, components) is generated from the directory contents. |
| DataLayout | In this checkout, clang (`TargetInfo::resetDataLayout()`), the BackendUtil check and every `CodeGenTargetMachineImpl` all call `Triple::computeDataLayout()`. Make `computeDataLayout()` of an MSIL triple return the layout of the same triple with its native object format (ELF/COFF/MachO). MSILTargetMachine then needs no DataLayout code of its own. |
| Exception model | `Triple::getDefaultExceptionHandling()` returns `None` for MSIL. Without that change, an assert in `CodeGenTargetMachineImpl::initAsmInfo()` fires. |
| CPU/features | Milestone 1: `MSIL.td` defines one processor, `generic`, and **no** SubtargetFeatures. All native CPU names map to `generic` (for both CPU and TuneCPU), and the feature string is dropped before it reaches the generated MCSubtargetInfo. The native strings are kept on the subtarget for later SIMD mapping (Design.md 3.2). |
| TTI | `MSILTTIImpl : BasicTTIImplBase` with zero vector registers (turns off LoopVectorize and SLP), `shouldBuildRelLookupTables() == false`, and no partial or runtime unrolling. The reloc model is forced to `Static`. |
| TLI | Derived purely from the triple, so an MSIL triple gets the native TLI (glibc for `*-linux-gnu-msil`). Expected and wanted. Watch for libcalls the optimizer introduces (`bcmp`, `puts`, `putchar`, `ldexp`, `memset`, ...). |
| Object files | Milestone 1 rejects `-filetype=obj` with a clear error by overriding `createMCStreamer`. |

---

## 1. Target selection

### 1.1 How lookup works today

`llvm/include/llvm/MC/TargetRegistry.h`:
- `Target::ArchMatchFnTy = bool (*)(Triple::ArchType Arch)` (line 142). Each `Target`
  stores `ArchMatchFn` (line 243), `Name`, `ShortDesc`, `BackendName`, `HasJIT`.
- `TargetRegistry::RegisterTarget(Target&, Name, ShortDesc, BackendName, ArchMatchFn, HasJIT)`
  (decl line 760, impl `TargetRegistry.cpp:188-210`) pushes onto a singly linked list
  (`FirstTarget`). The list order is reverse registration order.
- Helper template `RegisterTarget<Triple::ArchType, HasJIT>` (lines 1059-1070) uses
  `getArchMatch(Arch) { return Arch == TargetArchType; }`.

`llvm/lib/MC/TargetRegistry.cpp`:
- `lookupTarget(const Triple &TT, std::string &Error)` (lines 161-186): fails if no
  targets are registered, otherwise `find_if(targets(), T.ArchMatchFn(TT.getArch()))`.
  If a **second** target also matches, it fails with `Cannot choose between targets
  "A" and "B"`. Only the arch is consulted. Object format, OS and vendor are ignored.
- `lookupTarget(StringRef ArchName, Triple &TheTriple, std::string &Error)` (lines
  124-159): when `ArchName` (`-march`) is non-empty, it finds the target **by name**,
  then `Triple::getArchTypeForLLVMName(ArchName)`; if that is a known arch it does
  `TheTriple.setArch(Type)`. Otherwise it delegates to the triple overload.

Consequences:
- MSIL **must not** register with `RegisterTarget<Triple::x86_64>` and the like. That
  would collide with X86 (`Cannot choose between targets`).
- It must not use `RegisterTarget<>` either, whose default `Triple::UnknownArch`
  means it would match every unknown-arch triple. Precedent for "named only, never
  matches an arch": `arm64`/`arm64_32` (`AArch64TargetInfo.cpp:39-44`), `amdgcn`
  (`AMDGPUTargetInfo.cpp:51-53`), `bpf` (`BPFTargetInfo.cpp:30-32`), all with
  `[](Triple::ArchType) { return false; }`.
- The old LLVM 2.x MSIL backend (`git show bd24404718a6^:llvm/lib/Target/MSIL/TargetInfo/MSILTargetInfo.cpp`)
  registered with a TripleMatchQuality of 1, so it was selectable only via `-march=msil`.
  The quality API no longer exists.

### 1.2 Proposed hook (Option A, minimal, .cpp only)

`llvm/lib/MC/TargetRegistry.cpp`. Replace the two `lookupTarget` bodies (lines 124-186)
with the following. Unchanged parts are elided with comments.

```cpp
/// The MSIL target registers under this name
/// (llvm/lib/Target/MSIL/TargetInfo/MSILTargetInfo.cpp). It never claims an
/// architecture; it is selected by the triple's object format or by -march=msil.
static constexpr StringLiteral MSILTargetName = "msil";

static const Target *findTargetByName(StringRef Name) {
  auto I = find_if(TargetRegistry::targets(),
                   [&](const Target &T) { return Name == T.getName(); });
  return I == TargetRegistry::targets().end() ? nullptr : &*I;
}

const Target *TargetRegistry::lookupTarget(StringRef ArchName,
                                           Triple &TheTriple,
                                           std::string &Error) {
  const Target *TheTarget = nullptr;
  if (!ArchName.empty()) {
    TheTarget = findTargetByName(ArchName);
    if (!TheTarget) {
      Error = ("invalid target '" + ArchName + "'.").str();
      return nullptr;
    }

    if (ArchName == MSILTargetName) {
      // -march=msil keeps the triple's architecture, OS and environment (they
      // select the C ABI) and only switches the object format to MSIL.
      if (!TheTriple.isOSBinFormatMSIL())
        TheTriple.setObjectFormat(Triple::MSIL);
      return TheTarget;
    }
    if (TheTriple.isOSBinFormatMSIL()) {
      Error = ("target '" + ArchName + "' cannot compile for the MSIL triple '" +
               TheTriple.str() + "'; use -march=msil or a non-MSIL triple")
                  .str();
      return nullptr;
    }

    // Adjust the triple to match (if known), otherwise stick with the
    // given triple.
    Triple::ArchType Type = Triple::getArchTypeForLLVMName(ArchName);
    if (Type != Triple::UnknownArch)
      TheTriple.setArch(Type);
  } else {
    // ... unchanged (delegates to lookupTarget(TheTriple, TempError)) ...
  }
  return TheTarget;
}

const Target *TargetRegistry::lookupTarget(const Triple &TT,
                                           std::string &Error) {
  // Provide special warning when no targets are initialized.
  if (targets().begin() == targets().end()) {
    Error = "Unable to find target for this triple (no targets are registered)";
    return nullptr;
  }

  // An MSIL triple (x86_64-unknown-linux-gnu-msil, ...) names a native
  // architecture only to select the C ABI. It is always compiled by the MSIL
  // target.
  if (TT.isOSBinFormatMSIL()) {
    if (const Target *T = findTargetByName(MSILTargetName))
      return T;
    Error = "No available targets are compatible with triple \"" + TT.str() +
            "\" (the MSIL target is not built; configure LLVM with "
            "-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MSIL)";
    return nullptr;
  }

  // ... unchanged arch matching (lines 168-185) ...
}
```

Notes:
- `setObjectFormat` (Triple.cpp:1701-1708) rebuilds the environment as
  `getEnvironmentTypeName(Env) + "-" + "msil"` and drops an environment version
  (`msvc19.33.0` becomes `msvc`). That only affects `-march=msil` on a versioned
  triple and is acceptable. `-mtriple=...-msil` keeps the version.
- In llc, the DataLayout callback (`llcdriver.cpp:632-664`) mutates `TheTriple` through
  this function and then returns `TheTriple.computeDataLayout(...)`. It also does
  `M->setTargetTriple(TheTriple)` (line 679), so the module, the TM and the
  DataLayout all see the MSIL triple.

**Option B (generic, if this is ever upstreamed):** add
`using TripleMatchFnTy = bool (*)(const Triple &)` plus a `TripleMatchFn` field to
`Target`, `TargetRegistry::RegisterTripleMatch(Target&, TripleMatchFnTy)`, and in
`lookupTarget(const Triple&)` first return the unique target whose `TripleMatchFn`
accepts the triple. MSIL would register
`[](const Triple &TT) { return TT.isOSBinFormatMSIL(); }`. This touches
`TargetRegistry.h`, a widely included header, and is more code. Option A is
recommended for the fork. The cost is that the generic `.cpp` hard-codes the
`"msil"` name.

### 1.3 MSIL target registration (TargetInfo)

```cpp
// llvm/lib/Target/MSIL/TargetInfo/MSILTargetInfo.h
#ifndef LLVM_LIB_TARGET_MSIL_TARGETINFO_MSILTARGETINFO_H
#define LLVM_LIB_TARGET_MSIL_TARGETINFO_MSILTARGETINFO_H
namespace llvm {
class Target;
Target &getTheMSILTarget();
} // namespace llvm
#endif

// llvm/lib/Target/MSIL/TargetInfo/MSILTargetInfo.cpp
#include "TargetInfo/MSILTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
using namespace llvm;

Target &llvm::getTheMSILTarget() {
  static Target TheMSILTarget;
  return TheMSILTarget;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMSILTargetInfo() {
  // Never claims an architecture (x86_64, aarch64, ... belong to their native
  // targets). TargetRegistry::lookupTarget selects it for triples whose object
  // format is MSIL, or by name with -march=msil.
  TargetRegistry::RegisterTarget(getTheMSILTarget(), "msil",
                                 "MSIL (.NET CIL as ILAsm text)", "MSIL",
                                 [](Triple::ArchType) { return false; },
                                 /*HasJIT=*/false);
}
```

The `BackendName` (`"MSIL"`) must match `def MSIL : Target` in `MSIL.td`.

**Required unittest change.** `llvm/unittests/MC/TargetRegistry.cpp:20-42`
(`TEST(TargetRegistry, TargetHasArchType)`) asserts that every registered target
name maps to a known arch via `Triple::getArchTypeForLLVMName`. Only `"cpp"` is
exempt. `"msil"` must stay `UnknownArch`, because if it mapped to an arch, `-march=msil`
would overwrite the triple's arch. So change that line to
`if (Name != "cpp" && Name != "msil")`, or the MCTests fail as soon as MSIL is built.

### 1.4 Every caller path that must reach the MSIL target

All of them call one of the two `lookupTarget` overloads, so the hook covers them
without per-caller changes. Verified call sites:

| Tool / path | Call site | Which overload | Notes |
|---|---|---|---|
| llc `-mtriple=...-msil` / module triple | `llvm/tools/llc/lib/llcdriver.cpp:644` (DataLayout callback), `:607` (`-mcpu=help` path) | `(getMArch(), TheTriple)` | `-march=msil` handled by the ArchName branch. |
| opt | `llvm/tools/opt/optdriver.cpp:518` (help path); module path via `codegen::createTargetMachineForTriple` (`optdriver.cpp:701`, defined `llvm/lib/CodeGen/CommandFlags.cpp:756-776`, lookup at `:762`) | `(getMArch(), TheTriple)` | opt only builds a TM if `ModuleTriple.getArch()` is known (`optdriver.cpp:696`). True for MSIL triples. |
| llvm-mc | `llvm/tools/llvm-mc/llvm-mc.cpp:272` | `(ArchName, TheTriple)` | M1 has no MCAsmParser, so assembling prints "this target does not support assembly parsing." (`llvm-mc.cpp:363`). Phase 2. |
| clang cc1 codegen | `clang/lib/CodeGen/BackendUtil.cpp:581` (`EmitAssemblyHelper::CreateTargetMachine`, lines 577-602) | `(Triple)` | The main path. The triple is the module's (`TheModule->getTargetTriple()`). |
| clang cc1 misc | `BackendUtil.cpp:1540` (dyndbg), `clang/tools/driver/cc1_main.cpp:119,137,175` (`--print-supported-cpus/-extensions`, `--print-enabled-extensions`), `clang/tools/driver/cc1as_main.cpp:438` (`-cc1as`), `clang/lib/Parse/ParseStmtAsm.cpp:517` (MS `__asm{}` blocks, x86 triples only; degrades to "target ASM parser unavailable"), `clang/lib/CodeGen/ObjectFilePCHContainerWriter.cpp:277` (`-gmodules`/obj PCH container: would need object emission) | `(Triple)` | `--print-supported-cpus` with an MSIL triple will list MSIL's processors (`generic`), not x86's. |
| clang driver | `clang/lib/Driver/ToolChain.cpp:672` (target prefix of program name, e.g. `x86_64-unknown-linux-gnu-msil-clang`) | `(Triple)` | Works automatically once registered. |
| LTO (lld, all flavors, and `msil-ld` later) | `llvm/lib/LTO/LTOBackend.cpp:580` (`initAndLookupTarget`); TM made by `createTargetMachine` at `LTOBackend.cpp:221-257` (CPU = `Conf.CPU`, features = `getDefaultSubtargetFeatures(TT)` + `Conf.MAttrs`) | `(Triple)` | lld itself never calls `lookupTarget`; it calls `InitializeAllTargets()` (`lld/wasm/Driver.cpp:90`, `lld/ELF/Driver.cpp:315`, `lld/COFF/Driver.cpp:1606`, `lld/MachO/Driver.cpp:782`) and goes through `lto::LTO`. |
| Legacy LTO / ThinLTO C API | `llvm/lib/LTO/LTOCodeGenerator.cpp:385`, `LTOModule.cpp:205`, `ThinLTOCodeGenerator.cpp:602` | `(Triple)` | |
| IR symbol table (bitcode writer with `-flto`, lld input) | `llvm/lib/Object/ModuleSymbolTable.cpp:80` | `(Triple)` | **Asserts `T && T->hasMCAsmParser()`** when the module has module-level inline asm. MSIL has no asm parser, so file-scope `asm()` must be rejected in clang (the Design.md wrapper does) and by `msil-ld` before irsymtab. |
| C API / JIT / other | `llvm/lib/Target/TargetMachineC.cpp:86`, `llvm/lib/ExecutionEngine/TargetSelect.cpp:69` (`HasJIT=false`), `llvm/lib/MC/MCDisassembler/Disassembler.cpp:51`, `llvm/lib/Frontend/OpenMP/OMPIRBuilder.cpp:7769` | `(Triple)` | N/A for M1. |

### 1.5 Collateral in TargetRegistry.cpp

`Target::createMCObjectStreamer` (TargetRegistry.cpp:44-99) switches on
`T.getObjectFormat()` with no `default`. Adding `Triple::MSIL` to the enum makes this
a `-Wswitch` warning, and an unhandled value falls through to a null `S`. Add:

```cpp
  case Triple::MSIL:
    // Phase 2 adds a CIL object format and an MSIL streamer constructor.
    // Milestone 1 never gets here: MSILTargetMachine::createMCStreamer rejects
    // CodeGenFileType::ObjectFile.
    reportFatalUsageError("MSIL object file emission is not implemented");
```

Other switches over `ObjectFormatType` that must gain a `case Triple::MSIL` (for
the triple/MC owners): `llvm/lib/MC/MCContext.cpp:85`, `llvm/lib/MC/MCObjectFileInfo.cpp`
(`initMCObjectFileInfo`, `getDwarfComdatSection` at :1233), `llvm/lib/MC/MCAsmBackend.cpp`,
`llvm/lib/Bitcode/Writer/BitcodeWriter.cpp:6062,6089`,
`llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp:2080`,
`clang/lib/CodeGen/BackendUtil.cpp:280`, `clang/lib/CodeGen/CGObjCMac.cpp:5942`,
`clang/lib/CodeGen/CodeGenModule.cpp:7509`, and `Triple.cpp` itself
(`getObjectFormatTypeName`, `parseFormat`).

---

## 2. CMake registration of an experimental target

### 2.1 What exists (verified)

- `llvm/CMakeLists.txt:564-586`: `LLVM_ALL_TARGETS` (core tier).
- `llvm/CMakeLists.txt:588-593`: `LLVM_ALL_EXPERIMENTAL_TARGETS` = `ARC CSKY M68k Xtensa`.
- `:598-602`: cache vars `LLVM_TARGETS_TO_BUILD` (default `all`) and
  `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD` (default empty).
- `:721-739`: `all` expansion, then `LLVM_TARGETS_TO_BUILD += LLVM_EXPERIMENTAL_TARGETS_TO_BUILD`.
- `:1150-1201`: for each target `t`:
  - Fatal error if `t` is neither in `LLVM_ALL_TARGETS` nor in
    `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD`. So `MSIL` must **not** go in
    `LLVM_TARGETS_TO_BUILD`. Comment at 1160-1162: experimental targets not in
    `LLVM_ALL_EXPERIMENTAL_TARGETS` are still allowed if passed via
    `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD`.
  - `LLVM_TARGET(t)` appended to `LLVM_ENUM_TARGETS`, and `LLVM_HAS_<T_UPPER>_TARGET=1`.
  - `LLVM_ASM_PRINTER(t)` if `file(GLOB "${td}/*AsmPrinter.cpp")` is non-empty.
  - `LLVM_ASM_PARSER(t)` if `${td}/AsmParser/CMakeLists.txt` exists.
  - `LLVM_DISASSEMBLER(t)` if `${td}/Disassembler/CMakeLists.txt` exists.
  - `LLVM_TARGETMCA(t)` if `${td}/MCA/CMakeLists.txt` exists.
  - `LLVM_EXEGESIS(t)` if `llvm/tools/llvm-exegesis/lib/${t}/CMakeLists.txt` exists.
- `:1207-1230`: `configure_file` of `AsmPrinters.def`, `AsmParsers.def`,
  `Disassemblers.def`, `Targets.def`, `TargetMCAs.def`, `TargetExegesis.def` into
  `${build}/include/llvm/Config/`. `:1320-1321`: `Targets.h.cmake` becomes `Targets.h`.
- `llvm/lib/Target/CMakeLists.txt:34-37`: `foreach(t ${LLVM_TARGETS_TO_BUILD}) add_subdirectory(${t})`.
  No change needed.
- `llvm/lib/CMakeLists.txt:63`: `add_llvm_component_group(all-targets LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD})`.
  Automatic.
- `llvm/include/llvm/Support/TargetSelect.h:23-53,64-178`: for each `LLVM_TARGET(X)`
  it declares and calls `LLVMInitialize##X##TargetInfo`, `LLVMInitialize##X##Target`,
  `LLVMInitialize##X##TargetMC`. For `LLVM_ASM_PRINTER(X)` it calls
  `LLVMInitialize##X##AsmPrinter`. All four must be defined or tools fail to link.
- Tools link targets through components: `clang/tools/driver/CMakeLists.txt:2`
  (`${LLVM_TARGETS_TO_BUILD}`), `llvm/tools/llc/CMakeLists.txt:2-5`
  (`AllTargets*`), `lld/wasm/CMakeLists.txt:22`. Initialization:
  `cc1_main.cpp:230-233`, `llcdriver.cpp:379-382`, `optdriver.cpp:416-417`.

### 2.2 Exact edits to tracked files

1. `llvm/CMakeLists.txt:588-593`:
   ```cmake
   set(LLVM_ALL_EXPERIMENTAL_TARGETS
     ARC
     CSKY
     M68k
     MSIL
     Xtensa
   )
   ```
   This is optional, but it gives the nicer "is experimental" error, and
   `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD=all` then includes MSIL.
2. `llvm/include/llvm/Config/Targets.h.cmake`, between Mips and MSP430:
   ```c
   /* Define if the MSIL target is built in */
   #cmakedefine01 LLVM_HAS_MSIL_TARGET
   ```
3. `llvm/unittests/MC/TargetRegistry.cpp:35`: exempt `"msil"` (see 1.3).

No change is needed in `llvm/lib/Target/CMakeLists.txt`, in the `.def.in` templates,
or in `llvm-config`.

### 2.3 Generated outputs once the directory exists

| Generated file | Entry | Trigger |
|---|---|---|
| `Targets.def` | `LLVM_TARGET(MSIL)` | `MSIL` in `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD` |
| `AsmPrinters.def` | `LLVM_ASM_PRINTER(MSIL)` | `llvm/lib/Target/MSIL/MSILAsmPrinter.cpp` exists **at configure time** (file GLOB). Re-run cmake after adding it. |
| `AsmParsers.def` | none in M1 | do **not** create `MSIL/AsmParser/CMakeLists.txt` until phase 2 |
| `Disassemblers.def` | none | no `MSIL/Disassembler/` |
| `TargetMCAs.def`, `TargetExegesis.def` | none | |
| `llvm/Config/Targets.h` | `LLVM_HAS_MSIL_TARGET 1` | edit 2 above |

Required entry points:
`LLVMInitializeMSILTargetInfo` (TargetInfo/MSILTargetInfo.cpp),
`LLVMInitializeMSILTarget` (MSILTargetMachine.cpp),
`LLVMInitializeMSILTargetMC` (MCTargetDesc/MSILMCTargetDesc.cpp),
`LLVMInitializeMSILAsmPrinter` (MSILAsmPrinter.cpp). Each one is
`extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void ...()`.

### 2.4 New CMake files (modeled on WebAssembly)

`llvm/lib/Target/MSIL/CMakeLists.txt`:
```cmake
add_llvm_component_group(MSIL)

set(LLVM_TARGET_DEFINITIONS MSIL.td)
tablegen(LLVM MSILGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM MSILGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM MSILGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM MSILGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM MSILGenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM MSILGenSubtargetInfo.inc -gen-subtarget)
add_public_tablegen_target(MSILCommonTableGen)

add_llvm_target(MSILCodeGen
  MSILAsmPrinter.cpp
  MSILSubtarget.cpp
  MSILTargetMachine.cpp
  MSILTargetObjectFile.cpp
  MSILTargetTransformInfo.cpp
  # ... ISel, ExplicitLocals, RegStackify, etc. (other reports)

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Analysis AsmPrinter CodeGen CodeGenTypes Core MC MSILDesc MSILInfo
  Scalar SelectionDAG Support Target TargetParser TransformUtils

  ADD_TO_COMPONENT
  MSIL
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
```
`TargetInfo/CMakeLists.txt`: `add_llvm_component_library(LLVMMSILInfo MSILTargetInfo.cpp LINK_COMPONENTS MC Support ADD_TO_COMPONENT MSIL)`.
WebAssembly's version also has `include_directories(...)` and `add_dependencies(... CommonTableGen)`
because it includes generated `.inc` files. MSIL's TargetInfo does not need either.
`MCTargetDesc/CMakeLists.txt`: `add_llvm_component_library(LLVMMSILDesc MSILMCAsmInfo.cpp MSILMCTargetDesc.cpp MSILInstPrinter.cpp ... LINK_COMPONENTS CodeGenTypes MC Support TargetParser MSILInfo ADD_TO_COMPONENT MSIL)`.
Do not depend on GlobalISel. WebAssembly links it (`WebAssembly/CMakeLists.txt`),
but MSIL M1 is SelectionDAG only.

llvm-config then reports the components `msil`, `msilcodegen`, `msildesc`,
`msilinfo` (lower-cased library suffixes, `llvm/cmake/modules/AddLLVM.cmake:994-1005`),
and `--targets-built` includes `MSIL` (`llvm/tools/llvm-config/CMakeLists.txt:81`).

lit: `llvm/test/lit.cfg.py:496-499` and `clang/test/lit.cfg.py:406` add the feature
`msil-registered-target` automatically. Add `llvm/test/CodeGen/MSIL/lit.local.cfg`:
```python
if not "MSIL" in config.root.targets:
    config.unsupported = True
```

### 2.5 Configure command for the dev build (Windows, from a vcvars64 shell)

```
cmake -G Ninja -S llvm -B build -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON ^
  -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_TARGETS_TO_BUILD="X86;AArch64" ^
  -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MSIL
```
Keep `X86` built. The clang driver passes x86 backend `cl::opt`s for x86 triples, for
example `-mllvm -x86-asm-syntax=intel` in clang-cl mode
(`clang/lib/Driver/ToolChains/Clang.cpp:2219,2227`; the option is defined in
`llvm/lib/Target/X86/MCTargetDesc/X86MCOptions.td:41`). Without the X86 backend
that becomes "Unknown command line argument". The MSIL driver should also skip
x86 backend `-mllvm` options for MSIL triples.

---

## 3. DataLayout consistency

### 3.1 Where the DataLayout comes from and where it is checked (this checkout)

- **clang TargetInfo:** `clang/lib/Basic/TargetInfo.cpp:225-227`:
  ```cpp
  void TargetInfo::resetDataLayout() {
    DataLayoutString = Triple.computeDataLayout(getABI());
  }
  ```
  Every X86 and AArch64 TargetInfo constructor calls the no-arg form
  (`clang/lib/Basic/Targets/X86.h:444,548,580,631,669,725,867,1045`;
  `AArch64.cpp:1352`). The module gets it in `clang/lib/CodeGen/ModuleBuilder.cpp:162`.
  (`WindowsX86_32TargetInfo` builds an unused `Layout` string at X86.h:572-580
  before calling `resetDataLayout()`. It is dead code, but do not copy it.)
- **IR input to clang (`-x ir`):** `BackendUtil.cpp:1265-1267` sets
  `M->getTargetTriple().computeDataLayout(TM->getTargetABIName(*M))` if the module DL
  is the default.
- **The clang check:** `clang/lib/CodeGen/BackendUtil.cpp:1561-1570`, after the
  pipelines run:
  ```cpp
  if (AsmHelper.TM) {
    std::string DLDesc = M->getDataLayout().getStringRepresentation();
    std::string TDesc = M->getTargetTriple().computeDataLayout(
        AsmHelper.TM->getTargetABIName(*M));
    if (DLDesc != TDesc)
      Diags.Report(diag::err_data_layout_mismatch) << DLDesc << TDesc;
  }
  ```
  It compares against `Triple::computeDataLayout`, not against `TM->createDataLayout()`.
  The diagnostic is `err_data_layout_mismatch`, "backend data layout '%0' does not
  match expected target description '%1'" (`DiagnosticFrontendKinds.td:511`).
- **The backend's hard check:** `llvm/lib/CodeGen/MachineFunction.cpp:249-256`:
  `report_fatal_error` if `!Target.isCompatibleDataLayout(getDataLayout())`, which
  is `DL == Candidate` (`TargetMachine.h:232-234`).
- **How a TargetMachine gets its DL:** `llvm/lib/CodeGen/CodeGenTargetMachineImpl.cpp:93-98`:
  ```cpp
  : TargetMachine(T, TT.computeDataLayout(Options.MCOptions.getABIName()), TT,
                  CPU, FS, Options) {
  ```
  `CodeGenTargetMachineImpl`'s constructor (`CodeGenTargetMachineImpl.h:26-29`) no
  longer takes a DataLayout string. Every in-tree target, WebAssembly included
  (`WebAssemblyTargetMachine.cpp:173`), just passes `TT`.
- `Triple::computeDataLayout` lives in `llvm/lib/TargetParser/TargetDataLayout.cpp:592`
  and dispatches on `getArch()`. x86 goes to `computeX86DataLayout` (:433-481) and
  AArch64 to `computeAArch64DataLayout` (:81-98). The mangling component comes from
  `getManglingComponent` (:14-24) and **depends on the object format**: `-m:o` for
  MachO, `-m:w`/`-m:x` for Windows COFF, `-m:e` otherwise. AArch64 also returns a
  different string for MachO and for COFF.

So one function produces all three DataLayouts (clang TargetInfo, the BackendUtil
expectation and the TM). They agree by construction **as long as clang's
TargetInfo keeps the MSIL triple** and nobody calls `resetDataLayout(StringRef)`.

### 3.2 The problem: object format changes the layout

Without a hook, an MSIL triple is "not COFF, not MachO", so it gets the ELF-style
layout. Experiment with clang 22.1.3, using `-elf` to emulate "a non-native object
format":

| Triple | Native layout (verified) | Same triple with a non-native format |
|---|---|---|
| `x86_64-unknown-linux-gnu` / `-musl` | `e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128` | same |
| `x86_64-pc-windows-msvc`, `x86_64-w64-windows-gnu` | `e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128` | `e-m:e-...` (mangling only) |
| `aarch64-unknown-linux-gnu` | `e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32` | same |
| `aarch64-pc-windows-msvc` | `e-m:w-p270:32:32-p271:32:32-p272:64:64-p:64:64-i32:32-i64:64-i128:128-n32:64-S128-Fn32` | ELF string (`i8:8:32-i16:16:32`, no `p:64:64`/`i32:32`) |
| `aarch64-apple-darwin` | `e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32` | ELF string |
| `i686-pc-windows-msvc` | `e-m:x-p:32:32-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32-a:0:32-S32` | `e-m:e-...` (drops stdcall `@N` mangling) |

Clang and backend would still agree without a hook, because they share the
function. But the layout would not be the native one, which breaks two things:
(a) Design.md's "Stock-clang compatibility". Bitcode from stock clang for
`x86_64-pc-windows-msvc` carries `e-m:w-...`, and `msil-ld` must codegen it with
MSILTargetMachine, which `MachineFunction.cpp:249` rejects unless the DLs are
identical. (b) The Design.md promise that the data model is exactly the native one.

### 3.3 Proposed hook

`llvm/include/llvm/TargetParser/Triple.h`, next to `getObjectFormat()` (line 539):
```cpp
  /// The object format implied by the architecture and OS alone (ELF, COFF,
  /// MachO, ...), i.e. what the triple would use had it not named one.
  LLVM_ABI ObjectFormatType getDefaultObjectFormat() const;
```
`llvm/lib/TargetParser/Triple.cpp`, after the static `getDefaultFormat` (line 951):
```cpp
Triple::ObjectFormatType Triple::getDefaultObjectFormat() const {
  return getDefaultFormat(*this);
}
```
`llvm/lib/TargetParser/TargetDataLayout.cpp:592`:
```cpp
std::string Triple::computeDataLayout(StringRef ABIName) const {
  // An MSIL triple (x86_64-unknown-linux-gnu-msil, x86_64-pc-windows-msvc-msil,
  // aarch64-apple-darwin-msil, ...) compiles C with the ABI of the native
  // platform it names, so its data layout is exactly that platform's. Compute
  // it as if the triple used its native object format. Only the parsed format
  // changes; arch, OS, environment (and its version) are kept.
  if (isOSBinFormatMSIL()) {
    Triple Native(*this);
    Native.ObjectFormat = getDefaultObjectFormat();
    if (Native.ObjectFormat != Triple::MSIL) // guard for a future msil64 arch
      return Native.computeDataLayout(ABIName);
  }
  switch (getArch()) {
  // ... unchanged ...
```
(`computeDataLayout` is a `Triple` member, so it may write the private
`ObjectFormat` of a local copy. The copy's `str()` still ends in `-msil`. Every
`compute*DataLayout` helper reads only parsed fields: arch, subarch, OS,
environment, object format.)

Result: for every MSIL triple, the layout equals the native one in the table above.
That holds in clang's TargetInfo, in the BackendUtil check, in MSILTargetMachine
(through `CodeGenTargetMachineImpl`), and in llc's DataLayout callback. MSILTargetMachine
has no DataLayout code of its own.

Consequences for the backend (for the AsmPrinter/ISel owners):
- The mangling mode is the native one. `Mangler` (via `TargetLoweringObjectFile`)
  therefore adds `_` to globals on Darwin (`m:o`) and on i686 Windows (`m:x`, plus
  `@N` for stdcall), and uses `L`/`.L` private prefixes. CIL method names, field
  names and P/Invoke `EntryPoint`s must be derived from the IR names
  (`GlobalValue::getName()`, minus a leading `\1`), **not** from `getSymbol(GV)`
  MCSymbol names. Otherwise `dlsym`/P/Invoke would look up `_puts` on macOS.
- `f80:128`, `p270/p271/p272` remain in the string and are harmless.
- `ABIName` is ignored by both the x86 and AArch64 layout functions, so
  `-target-abi`/`"target-abi"` module flags (`TargetMachine::getTargetABIName`,
  TargetMachine.cpp:337-341) cannot create a mismatch.
- The clang wrapper must not call `resetDataLayout(StringRef)`, even after mapping
  `long double` to `double`. The `f80:128` entry is irrelevant to `double`.

### 3.4 Exception-model assert (same area)

`CodeGenTargetMachineImpl::initAsmInfo()` (CodeGenTargetMachineImpl.cpp:83-85)
asserts `TmpAsmInfo->getExceptionHandlingType() == getTargetTriple().getDefaultExceptionHandling()`.
For `x86_64-*-msil`, `Triple::getDefaultExceptionHandling()` (Triple.cpp:2473-2521)
returns `DwarfCFI` (because of `isX86()` at :2496). For `*-windows-msvc-msil` it
returns DwarfCFI too, since it is not COFF. MSIL's MCAsmInfo must report `None`
(`MCAsmInfo.h:357` default), because otherwise the AsmPrinter adds DWARF CFI
handlers. Fix at the top of `Triple::getDefaultExceptionHandling()`:
```cpp
  // CIL has its own exception model; LLVM-level unwinding is not used.
  if (isOSBinFormatMSIL())
    return ExceptionHandling::None;
```
This also feeds `RuntimeLibcallsInfo` (`llvm/lib/IR/RuntimeLibcalls.cpp:36-38`) and
`TargetMachine::getExceptionModel()` (`TargetMachine.h:270-274`).

---

## 4. CPU and features

### 4.1 What clang passes for an x86_64 triple (verified with clang 22.1.3 `-###`)

`-triple x86_64-unknown-linux-gnu ... -mrelocation-model pic -pic-level 2 -pic-is-pie
-mframe-pointer=none -target-cpu x86-64 -tune-cpu generic`.
`-tune-cpu generic` comes from `AddX86TargetArgs` (`Clang.cpp:2244-2262`).
There is no `-target-feature`. The baseline features come from the CPU through
`X86TargetInfo::initFeatureMap` (`FeaturesX86_64`, `X86TargetParser.cpp:56,467`).
Every function gets:
```
"target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic"
```
`__attribute__((target("avx2")))` adds per-function
`"+avx,+avx2,+cmov,+crc32,+cx8,+fxsr,+mmx,+popcnt,+sse,+sse2,+sse3,+sse4.1,+sse4.2,+ssse3,+x87,+xsave"`.
BackendUtil passes `TargetOpts.CPU` and the joined `TargetOpts.Features` to
`createTargetMachine` (`BackendUtil.cpp:587-600`). The TM-level `MCSubtargetInfo`
(`CodeGenTargetMachineImpl.cpp:58-60`, `initAsmInfo`) therefore sees `x86-64` too.

### 4.2 How MCSubtargetInfo reacts (`llvm/lib/MC/MCSubtargetInfo.cpp`)

- `getFeatures()` (:179-237): **returns an empty bitset immediately if the target's
  `ProcDesc` or `ProcFeatures` table is empty** (:186). Otherwise:
  - An unknown `CPU` prints `'<cpu>' is not a recognized processor for this target (ignoring processor)` to `errs()` (:207).
  - An unknown `TuneCPU` that differs from `CPU` prints the same (:220).
  - Each unknown feature prints `'+x' is not a recognized feature for this target (ignoring feature)` via `ApplyFeatureFlag` (:69-94).
- `InitMCProcessorInfo()` (:239-249) calls `getSchedModelForCPU(TuneCPU)` whenever
  `TuneCPU` is non-empty. **That warns for an unknown TuneCPU even when the feature
  table is empty** (:449-463).
- `checkFeatures()` / `checkFeatureExpression()` (:329-360) are **fatal** on unknown
  feature names. They are only called on the target's own feature names.
- `Target::createMCSubtargetInfo` returns null if the feature string is malformed
  (`TargetRegistry.h:452-458`, `isValidFeatureListFormat`). Clang's strings are well formed.
- Generated code: `XGenSubtargetInfo(TT, CPU, TuneCPU, FS)` and `ParseSubtargetFeatures`
  both run `InitMCProcessorInfo`. With zero SubtargetFeatures, `ParseSubtargetFeatures`
  has an empty body (`SubtargetEmitter.cpp:2053-2056`), and `createXMCSubtargetInfoImpl`
  passes `{}` for the feature table (`SubtargetEmitter.cpp:2189-2192`). Both are
  supported shapes.

### 4.3 Other virtual-ISA targets (experiment: x86 `-O2` IR re-targeted, compiled with clang 22.1.3)

- **WebAssembly** (per-function subtarget from attributes, `WebAssemblyTargetMachine.cpp:209-229`;
  constructor passes `TuneCPU = CPU`, `WebAssemblySubtarget.cpp:55-56`): prints 2x each of
  `'+cmov' ... '+x87' is not a recognized feature` and 4x
  `'x86-64' is not a recognized processor`. The counts match: one subtarget, with
  `InitMCProcessorInfo` run twice (base constructor plus `ParseSubtargetFeatures`),
  and the CPU warned by both `getFeatures` and `getSchedModelForCPU(TuneCPU)`.
- **NVPTX**: silent. It has a single subtarget built from the TM's CPU/FS and ignores
  function attributes (`NVPTXTargetMachine.h:36-39`).
- **SPIRV**: not built in VS's clang. Same single-subtarget pattern
  (`SPIRVTargetMachine.h:32-35`), a single `Proc<"generic", []>` (`SPIRV.td:21-24`)
  and no SubtargetFeatures, so it is silent too.

### 4.4 Recommendation

**Milestone 1:**
- `MSIL.td`: `def : ProcessorModel<"generic", NoSchedModel, []>;` and **no**
  `SubtargetFeature`s.
- One normalization point used by both the MC layer and the CodeGen subtarget:
  `MSIL::getMSILCPU(CPU)` returns `"generic"` for every name except `"help"`. Pass the
  result as **both** CPU and TuneCPU. `MSIL::filterMSILFeatures(FS)` returns `""`.
  Together these make all the warnings above impossible, including the TuneCPU one.
- Keep a WebAssembly-style per-function subtarget map. Key it on the raw
  `target-cpu`+`target-features` strings, but build every subtarget from the
  normalized values, and store the raw strings on the subtarget (`NativeCPU`,
  `NativeFS`) for later. Inline compatibility (`BasicTTIImplBase::areInlineCompatible`,
  `BasicTTIImpl.h:397-420`) compares MSIL feature bits, which are all empty, so
  every function stays inline-compatible, as in a native baseline build.
- Do not read `"tune-cpu"`.

**Later (Design.md 3.2, SIMD):** add MSIL SubtargetFeatures whose names are the
native ones the mapping needs (`sse`, `sse2`, `ssse3`, `sse4.1`, `sse4.2`, `avx`,
`avx2`, `avx512f`, `popcnt`, `lzcnt`, `bmi`, `bmi2`, `aes`, `pclmul`, `fma`; and for
AArch64 `neon`, `aes`, `sha2`, `crc`, `dotprod`, `rdm`). Then make
`filterMSILFeatures` keep only names present in the generated table
(`MSILFeatureKVStorage.Features` from `GET_SUBTARGETINFO_MC_DESC`, or
`MCSubtargetInfo::getAllProcessorFeatures()`, `MCSubtargetInfo.h:315`). Unknown
native features (`cx8`, `x87`, `cmov`, `fxsr`, `mmx`, ...) keep being dropped
silently. Interpret same-named features per `TT.getArch()` (x86 `aes` maps to
`X86.Aes`; aarch64 `aes` maps to `Arm.Aes`). Model the .NET profile
(`-mdotnet=net48|net8.0`) as a TargetOptions field or module flag, not as a feature,
because the native clang TargetInfo validates `-target-feature` names.

---

## 5. TTI and TLI

### 5.1 Why TTI matters already in milestone 1

Clang runs the whole `-O2` middle end with the MSIL TM's TTI
(`TM->getTargetIRAnalysis()`, BackendUtil `RunOptimizationPipeline`). The
`CodeGenTargetMachineImpl` default is a plain `BasicTTIImpl`
(`CodeGenTargetMachineImpl.cpp:109-112`). Its defaults cause these problems:

| Default | Effect at -O2 | Evidence |
|---|---|---|
| `getNumberOfRegisters(vector class) == 8` (`TargetTransformInfoImpl.h:627`); `getRegisterBitWidth == 32` (`BasicTTIImpl.h:900-902`) | LoopVectorize and SLP stay enabled and produce small vectors (`<4 x i8>`, ...) | Gates at `LoopVectorize.cpp:8186-8189` and `SLPVectorizer.cpp:29618` key on `getNumberOfRegisters(getRegisterClassForType(true)) == 0` |
| `shouldBuildRelLookupTables()` is true when `TM.isPositionIndependent()`, the code model is small/kernel and the arch is 64-bit (`BasicTTIImpl.h:650-673`) | Clang passes PIC/PIE on Linux, so SimplifyCFG/RelLookupTableConverter produce `@llvm.load.relative.i64` and initializers like `trunc(sub(ptrtoint @str, ptrtoint @table))` | Seen in clang 22 x86_64-linux `-O2` output (`swstr()` in the experiment) |
| `getUnrollingPreferences`: partial/runtime unrolling only if the sched model's `LoopMicroOpBufferSize > 0` (`BasicTTIImpl.h:724-760`) | Off with `NoSchedModel` | Fine, but be explicit (Design.md risk 1: large methods hurt RyuJIT) |

### 5.2 Proposed MSILTTIImpl (milestone 1)

```cpp
// llvm/lib/Target/MSIL/MSILTargetTransformInfo.h
class MSILTTIImpl final : public BasicTTIImplBase<MSILTTIImpl> {
  using BaseT = BasicTTIImplBase<MSILTTIImpl>;
  using TTI = TargetTransformInfo;
  friend BaseT;

  const MSILSubtarget *ST;
  const MSILTargetLowering *TLI;
  const MSILSubtarget *getST() const { return ST; }
  const MSILTargetLowering *getTLI() const { return TLI; }

public:
  MSILTTIImpl(const MSILTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  // Milestone 1 has no vector types. Zero vector registers turn off
  // LoopVectorize (LoopVectorize.cpp:8186) and SLP (SLPVectorizer.cpp:29618).
  // Revisit with Vector128<T> (Design.md 3.2).
  unsigned getNumberOfRegisters(unsigned ClassID) const override {
    return ClassID == 1 ? 0 : BaseT::getNumberOfRegisters(ClassID);
  }
  TypeSize getRegisterBitWidth(TTI::RegisterKind K) const override {
    switch (K) {
    case TTI::RGK_Scalar:
      return TypeSize::getFixed(getDataLayout().getPointerSizeInBits(0));
    case TTI::RGK_FixedWidthVector:
      return TypeSize::getFixed(0);
    case TTI::RGK_ScalableVector:
      return TypeSize::getScalable(0);
    }
    llvm_unreachable("Unsupported register kind");
  }

  // Globals live in a run-time allocated segment; no PC-relative tables.
  bool shouldBuildRelLookupTables() const override { return false; }

  // Keep methods JIT-friendly (Design.md 10, risk 1).
  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE) const override {
    BaseT::getUnrollingPreferences(L, SE, UP, ORE);
    UP.Partial = UP.Runtime = false;
  }
  // Later: getPopcntSupport -> PSK_FastHardware when ctpop maps to
  // BitOperations.PopCount (net8 profile); IL-size-aware inline cost.
};
```
Keep `shouldBuildLookupTables()` at its default (`BasicTTIImpl.h:644-648`, true if
`BR_JT` or `BRIND` is legal or custom). Plain constant-array switch tables
(`@switch.table.sw`) are ordinary data and fine for CIL. Leave `getPopcntSupport`
at `PSK_Software` and `enableMemCmpExpansion` at none for M1.

### 5.3 TargetLibraryInfo is derived from the triple

- Clang: `llvm::driver::createTLII(TargetTriple, VecLib)` (`BackendUtil.cpp:962-964`)
  calls `new TargetLibraryInfoImpl(TargetTriple, ...)`
  (`llvm/lib/Frontend/Driver/CodeGenOptions.cpp:42-46`). Codegen:
  `TargetLibraryInfoImpl TLII(TM.getTargetTriple(), ...)` (`llvm/lib/Passes/RunCodeGen.cpp:38`).
- `TargetLibraryInfoImpl(const Triple&)` (`TargetLibraryInfo.cpp:909`) uses only
  arch/OS/environment predicates and never the object format (no `isOSBinFormat*`
  in that file). An MSIL triple therefore gets exactly the native TLI. For
  `x86_64-unknown-linux-gnu-msil` that means:
  - all of C99/POSIX is available, plus the glibc extras: `__strdup`,
    `__isoc99_scanf`, `_IO_getc`, `fopen64`/`stat64`, `memalign`, and the `*_unlocked`
    stdio functions (:605-690).
  - `bcmp` is available (`hasBcmp`, :56-64), so `memcmp(...)==0` becomes `bcmp`.
  - `IsErrnoFunctionCall` is true for GNU (:899-907).
  - int is 32-bit, and there are no `signext`/`zeroext` requirements on i32 (`initExtensionsForTriple`).
- Verified with clang 22 `-O2` on x86_64-linux-gnu: `memcmp(a,b,n)==0` becomes
  `@bcmp`, `printf("hello\n")` becomes `@puts`, `printf("%c",65)` becomes `@putchar`,
  a zeroing loop becomes `@llvm.memset`, `exp2((double)i)` becomes `@ldexp(1.0, i)`,
  and `sqrt` stays a call (math-errno). This matches the host-libc design (all are
  in glibc), but **the P/Invoke table must expect functions the source never names**.
- `-ffreestanding` implies `-fno-builtin`, so clang puts `"no-builtins"` on every
  function (`CGCall.cpp:2607-2609`). TLI honors that per function
  (`TargetLibraryInfo.h:302`). Even then, codegen emits libcalls for `llvm.memcpy`,
  `llvm.memset` and `llvm.memmove` unless MSIL lowers them to `cpblk`/`initblk`. It
  also emits `__divti3`, `__modti3`, `__udivti3` and `__umodti3` for i128. Clang
  itself emits `__mulsc3`/`__divsc3` for `_Complex` multiply and divide even when
  freestanding (seen in the experiment).
- Codegen libcall names come from `RuntimeLibcallsInfo(TT, ...)`
  (`llvm/lib/IR/RuntimeLibcalls.cpp:31-41`), which is triple-keyed TableGen. An
  `x86_64-*-msil` triple gets the X86 system library set (`isX86` predicates in
  `RuntimeLibcalls.td:3640-3690`). That is fine for M1. MSIL may later want its own
  set (for example routing i128 helpers to the runtime assembly).

---

## 6. Target-specific IR from plain portable C at -O2 on x86_64

Source: clang 22.1.3 `--target=x86_64-unknown-linux-gnu -O2 [-ffreestanding] -S -emit-llvm`
and `--target=x86_64-pc-windows-msvc`. Test files are in the scratchpad (section 10).
This used the x86 TTI. With MSILTTIImpl the vector and `load.relative` rows disappear.

| IR construct | Where it came from | M1 action |
|---|---|---|
| `"target-cpu"="x86-64"`, `"target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87"`, `"tune-cpu"="generic"` on every function | x86 TargetInfo, driver | Accept silently (section 4). |
| `"min-legal-vector-width"="N"` | Only for `isX86()` triples (`CodeGenFunction.cpp:538-540`) | Ignore. |
| `"stack-protector-buffer-size"="8"`, `"no-trapping-math"`, `"frame-pointer"`; module flags `wchar_size` (4 on Linux, 2 on Windows), `PIC Level`, `PIE Level`, `uwtable` (hosted), `MaxTLSAlign` (Windows COFF only) | Generic and x86 | Ignore. Reloc model forced Static. |
| `sspstrong`/`ssp` (only with `-fstack-protector*`; clang-cl `/GS` is on by default) | Driver | Either the MSIL driver disables it, or the generic StackProtector pass inserts `__stack_chk_guard`/`__stack_chk_fail` (or `__security_cookie` for MSVC). Prefer off for M1. |
| SysV ABI coercions: `<2 x float>` for `struct {float x,y;}` and `_Complex float` args/returns; `{double, i64}` returns; `byval`/`sret` with alignment; `signext`/`zeroext` on `char`/`short` params | `clang/lib/CodeGen/Targets/X86.cpp` | Must handle. `<2 x float>` must be legalized (split or scalarize) even with no vector types. |
| Win64: structs of size 1/2/4/8 coerced to `i64` etc.; larger ones by pointer (`dead_on_return`); `va_list` is `ptr` | X86.cpp WinX86_64 | Must handle. |
| `x86_fp80` / `llvm.fmuladd.f80` for `long double` | x86 TargetInfo | The MSIL wrapper maps long double to double. x86-64 SysV classification supports that (`X86.cpp:2016-2027`, `IEEEdouble` gives SSE). Reject `x86_fp80` in the backend (no sane libcalls exist for f80). |
| `fp128` (`__float128`), `half` (`_Float16`, enabled by SSE2), `bfloat` | x86 TargetInfo | Reject in M1 with a named diagnostic. |
| `i128` (`__int128`) | Generic | Legalizer expands. Division needs `__divti3` etc. (provider: runtime or libgcc). |
| `llvm.fmuladd.*` (default `-ffp-contract=on`), `llvm.smax/smin/umax/umin`, `llvm.abs`, `llvm.bswap`, `llvm.ctlz/cttz/ctpop`, `llvm.fshl/fshr` (rotates), `llvm.*.with.overflow`, `llvm.memcpy/memset/memmove`, `llvm.lifetime.*`, `llvm.assume`, `llvm.threadlocal.address`, `llvm.va_start/va_end/va_copy`, `llvm.stacksave/stackrestore` (VLAs in loops), `llvm.sqrt/fabs/floor/ceil/trunc/rint/round/copysign/minnum/maxnum`, `llvm.vector.reduce.*` | Generic, InstCombine, vectorizers | Must handle (the generic set). SelectionDAG expands most. `fmuladd` becomes FMUL+FADD unless `isFMAFasterThanFMulAndFAdd`. |
| `fastcc` on internal functions (`define internal fastcc i32 @fib`) | GlobalOpt | Must accept `CallingConv::Fast` (treat as C). |
| `@llvm.load.relative.i64` and `sub(ptrtoint,ptrtoint)` in initializers | RelLookupTableConverter (PIC + 64-bit) | Prevented by `Reloc::Static` and `shouldBuildRelLookupTables()=false`. |
| `@switch.table.*` private constant arrays | SimplifyCFG | Must handle (data segment). |
| `call i32 (i32, double, ...) @knr(...)` with `declare i32 @knr(...)` | `X86_64TargetCodeGenInfo::isNoProtoCallVariadic` (`X86.cpp:1565-1587`): unprototyped calls are variadic on x86-64 SysV | Must handle as fixed-arity (cf. `WebAssemblyFixFunctionBitcasts`). The clang MSIL wrapper should override `isNoProtoCallVariadic` to false and add `"no-prototype"` like `clang/lib/CodeGen/Targets/WebAssembly.cpp:123`. |
| x86-64 `va_arg` expanded inline over `%struct.__va_list_tag` (`gp_offset`/`fp_offset`/`overflow_arg_area`/`reg_save_area`) | X86.cpp | Plain IR. `va_start` lowering must fake an "exhausted" register area (Design.md 4.7). |
| `thread_local` globals | Generic | M1: reject or simple lowering (Design.md 4.4). |
| `llvm.x86.*` | **Not** produced by portable C. Produced by `<immintrin.h>` and `__builtin_ia32_*`: `_mm_min_ps` gives `llvm.x86.sse.min.ps`, `_mm_cvtps_epi32` gives `llvm.x86.sse2.cvtps2dq`, `_mm_packs_epi16` gives `llvm.x86.sse2.packsswb.128`, `__rdtsc` gives `llvm.x86.rdtsc`. Most intrinsics become generic IR: `_mm_add_ps` is `fadd <4 x float>`, `_mm_shuffle_ps` is `shufflevector`, `_mm_loadu_si128` is `load align 1`, `_mm_sqrt_ps` is `llvm.sqrt.v4f32`, `_mm_movemask_ps` is generic. | Reject in M1 with a diagnostic naming the intrinsic. Phase 4 maps them (Design.md 3.2). |
| `__builtin_cpu_supports` | `load @__cpu_model` (+ `__cpu_indicator_init`) | Reject in M1, later map to `IsSupported`. |
| `target("avx2")` functions | Per-function `target-features` | Ignored in M1 (no SIMD). |
| `ifunc` / `target_clones` | `TargetInfo::supportsIFunc()` (`TargetInfo.h:1572-1585`) is false for non-ELF x86 Linux, **but true for `aarch64-*-windows-*`** (:1575) | The clang wrapper must make it false for MSIL. |
| `dllimport`/`dllexport`, `llvm.linker.options` (`/DEFAULTLIB`) on Windows triples | Windows SDK headers, MSVC driver | Ignore in M1 (input for P/Invoke DLL resolution later). |
| x86 calling conventions: `x86_stdcallcc`/`fastcallcc`/`vectorcallcc`/`regcallcc`/`win64cc`/`x86_64_sysvcc`/`x86_intrcc` | Attributes in source | Treat `win64cc`/`x86_64_sysvcc` as C on their home OS. Reject the rest on 64-bit (stdcall/fastcall are already dropped by clang on x64). |
| Inline asm / module asm | Source | Rejected by the clang wrapper (Design.md 3). The backend should still emit a clean error. |

---

## 7. Proposed MSIL TargetMachine, subtarget and MC subtarget code

### 7.1 `MSIL.td` (subtarget part only)

```tablegen
include "llvm/Target/Target.td"

// Milestone 1: no SubtargetFeatures. With an empty feature table,
// MCSubtargetInfo ignores the native x86/AArch64 feature strings silently
// (MCSubtargetInfo.cpp:186). CPU and TuneCPU are normalized to "generic" in C++.
def : ProcessorModel<"generic", NoSchedModel, []>;

// ... registers / instructions (other reports) ...

def MSILInstrInfo : InstrInfo;
def MSILAsmWriter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
  int PassSubtarget = 1;
  int Variant = 0;
  bit isMCAsmWriter = 1;
}
def MSIL : Target {
  let InstructionSet = MSILInstrInfo;
  let AssemblyWriters = [MSILAsmWriter];
  // No AssemblyParsers in milestone 1.
}
```

### 7.2 MC subtarget creation (`MCTargetDesc/MSILMCTargetDesc.{h,cpp}`)

```cpp
// MSILMCTargetDesc.h
namespace llvm::MSIL {
/// Map a CPU name from the native toolchain ("x86-64", "znver4", "generic",
/// "apple-m1", ...) to an MSIL processor. Milestone 1 has only "generic".
StringRef getMSILCPU(StringRef NativeCPU);
/// Keep only the features MSIL defines. Milestone 1 defines none.
std::string filterMSILFeatures(StringRef NativeFS);
} // namespace llvm::MSIL

// MSILMCTargetDesc.cpp
#define GET_SUBTARGETINFO_MC_DESC
#include "MSILGenSubtargetInfo.inc"

StringRef MSIL::getMSILCPU(StringRef NativeCPU) {
  return NativeCPU == "help" ? NativeCPU : StringRef("generic");
}

std::string MSIL::filterMSILFeatures(StringRef NativeFS) {
  // Phase 4: keep entries whose name is in MSILFeatureKVStorage.Features.
  (void)NativeFS;
  return std::string();
}

static MCSubtargetInfo *createMSILMCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  StringRef MCPU = MSIL::getMSILCPU(CPU);
  return createMSILMCSubtargetInfoImpl(TT, MCPU, /*TuneCPU=*/MCPU,
                                       MSIL::filterMSILFeatures(FS));
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMSILTargetMC() {
  Target &T = getTheMSILTarget();
  RegisterMCAsmInfoFn X(T, createMSILMCAsmInfo);   // ExceptionsType = None
  TargetRegistry::RegisterMCInstrInfo(T, createMSILMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createMSILMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createMSILMCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createMSILMCInstPrinter);
  TargetRegistry::RegisterAsmTargetStreamer(T, createMSILAsmTargetStreamer);
  TargetRegistry::RegisterNullTargetStreamer(T, createMSILNullTargetStreamer);
  // No MCCodeEmitter / MCAsmBackend / object streamer in milestone 1.
}
```
(Pattern from `WebAssemblyMCTargetDesc.cpp:78-132`.) `-mcpu=help` prints nothing
while the feature table is empty, because `getFeatures` returns before handling
`help`. That is acceptable.

### 7.3 `MSILSubtarget`

```cpp
class MSILSubtarget final : public MSILGenSubtargetInfo {
  // The native CPU / feature string exactly as clang wrote them, e.g.
  // "x86-64" / "+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87". Not interpreted in
  // milestone 1; the input to the SIMD mapping later (Design.md 3.2).
  std::string NativeCPU;
  std::string NativeFS;
  // Declared after NativeCPU/NativeFS: initialized in declaration order.
  MSILInstrInfo InstrInfo;
  MSILFrameLowering FrameLowering;
  MSILTargetLowering TLInfo;
  MSILSelectionDAGInfo TSInfo;

  MSILSubtarget &initializeSubtargetDependencies() {
    ParseSubtargetFeatures(getCPU(), getTuneCPU(), getFeatureString());
    return *this;
  }

public:
  MSILSubtarget(const Triple &TT, StringRef CPU, StringRef FS,
                const TargetMachine &TM)
      : MSILGenSubtargetInfo(TT, MSIL::getMSILCPU(CPU),
                             /*TuneCPU=*/MSIL::getMSILCPU(CPU),
                             MSIL::filterMSILFeatures(FS)),
        NativeCPU(CPU), NativeFS(FS),
        InstrInfo(initializeSubtargetDependencies()), FrameLowering(),
        TLInfo(TM, *this) {}

  StringRef getNativeCPU() const { return NativeCPU; }
  StringRef getNativeFeatureString() const { return NativeFS; }
  // Generated by TableGen.
  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);
  // getInstrInfo/getTargetLowering/getFrameLowering/getRegisterInfo/
  // getSelectionDAGInfo overrides as in WebAssemblySubtarget.
};
```

### 7.4 `MSILTargetMachine`

```cpp
// MSILTargetMachine.h
class MSILTargetMachine final : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  mutable StringMap<std::unique_ptr<MSILSubtarget>> SubtargetMap;

public:
  MSILTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                    StringRef FS, const TargetOptions &Options,
                    std::optional<Reloc::Model> RM,
                    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                    bool JIT);
  ~MSILTargetMachine() override;

  const MSILSubtarget *getSubtargetImpl(StringRef CPU, StringRef FS) const;
  const MSILSubtarget *getSubtargetImpl(const Function &F) const override;

  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;
  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;
  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;
  bool usesPhysRegsForValues() const override { return false; }

  Expected<std::unique_ptr<MCStreamer>>
  createMCStreamer(raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
                   CodeGenFileType FileType, MCContext &Ctx) override;
};

// MSILTargetMachine.cpp
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMSILTarget() {
  RegisterTargetMachine<MSILTargetMachine> X(getTheMSILTarget());
  auto &PR = *PassRegistry::getPassRegistry();
  // initializeMSIL...Pass(PR) for the target's passes.
  (void)PR;
}

// CIL has no load-time relocation model: global addresses are computed at
// run time from the module's data-segment base (Design.md 4.1). Static also
// keeps the optimizer from building PC-relative lookup tables
// (BasicTTIImplBase::shouldBuildRelLookupTables, BasicTTIImpl.h:650).
static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model>) {
  return Reloc::Static;
}

// Code models are meaningless for CIL. Small keeps the generic x86-64
// "large data" logic (TargetMachine.cpp:69-125) inert.
static CodeModel::Model getEffectiveMSILCodeModel(std::optional<CodeModel::Model>) {
  return CodeModel::Small;
}

static void checkMSILTriple(const Triple &TT) {
  switch (TT.getArch()) {
  case Triple::x86_64:
  case Triple::aarch64:
    return;
  default: // i686 is "later" in Design.md 3; others are not planned.
    reportFatalUsageError(Twine("MSIL: unsupported architecture '") +
                          TT.getArchName() + "' in target triple '" +
                          TT.str() + "'");
  }
}

MSILTargetMachine::MSILTargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT)
    // DataLayout: CodeGenTargetMachineImpl computes
    // TT.computeDataLayout(ABIName), which for an MSIL triple is the native
    // layout of its arch/OS (TargetDataLayout.cpp hook, section 3.3).
    : CodeGenTargetMachineImpl(T, TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getEffectiveMSILCodeModel(CM), OL),
      TLOF(std::make_unique<MSILTargetObjectFile>()) {
  checkMSILTriple(TT);

  // Before initAsmInfo(), which copies Options.ExceptionModel into MCAsmInfo.
  if (this->Options.ExceptionModel != ExceptionHandling::Default &&
      this->Options.ExceptionModel != ExceptionHandling::None)
    reportFatalUsageError("MSIL: only -exception-model=none is supported");

  // A CIL method body may not fall off its end: lower 'unreachable' to a trap
  // (a 'throw' / call to abort), also after noreturn calls.
  this->Options.TrapUnreachable = true;
  this->Options.NoTrapAfterNoreturn = false;

  // Every function and global is emitted independently, as for WebAssembly.
  this->Options.FunctionSections = true;
  this->Options.DataSections = true;
  this->Options.UniqueSectionNames = true;

  initAsmInfo();

  // Like WebAssembly: no register allocation; tied operands are handled by
  // TwoAddressInstructionPass.
  setEnableTiedFastRegAlloc(false);
  // Do not call setRequiresStructuredCFG(true): CIL allows arbitrary branches.
}

MSILTargetMachine::~MSILTargetMachine() = default;

const MSILSubtarget *
MSILTargetMachine::getSubtargetImpl(StringRef CPU, StringRef FS) const {
  // Keyed on the native strings so that each subtarget remembers exactly what
  // clang asked for; all of them are built as MSIL "generic" in milestone 1.
  auto &I = SubtargetMap[CPU.str() + "|" + FS.str()];
  if (!I)
    I = std::make_unique<MSILSubtarget>(TargetTriple, CPU, FS, *this);
  return I.get();
}

const MSILSubtarget *
MSILTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");
  StringRef CPU = CPUAttr.isValid() ? CPUAttr.getValueAsString() : TargetCPU;
  StringRef FS = FSAttr.isValid() ? FSAttr.getValueAsString() : TargetFS;
  return getSubtargetImpl(CPU, FS);
}

TargetTransformInfo
MSILTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<MSILTTIImpl>(this, F));
}

Expected<std::unique_ptr<MCStreamer>>
MSILTargetMachine::createMCStreamer(raw_pwrite_stream &Out,
                                    raw_pwrite_stream *DwoOut,
                                    CodeGenFileType FileType, MCContext &Ctx) {
  if (FileType == CodeGenFileType::ObjectFile)
    return createStringError(
        "MSIL: object file emission is not implemented yet; use -S to emit "
        "ILAsm text and assemble it with ilasm");
  return CodeGenTargetMachineImpl::createMCStreamer(Out, DwoOut, FileType, Ctx);
}
```
`CodeGenTargetMachineImpl::addAsmPrinter` reports that error through
`Context.reportError` (`CodeGenTargetMachineImpl.cpp:147-152`). llc then prints
"target does not support generation of this file type" (`llcdriver.cpp:846`), and
clang prints `err_fe_unable_to_interface_with_target`.

Keep the legacy codegen pass manager: `shouldDefaultToNewPM()` stays false
(`TargetMachine.h:531`, selection in `llvm/lib/Passes/RunCodeGen.cpp:92-103`).
Implementing `buildCodeGenPipeline`/`registerPassBuilderCallbacks` (which
WebAssembly has) is not needed for M1.

---

## 8. Tests to gate this piece (milestone checkpoints)

1. **Triple-only, before any target exists** (`llvm/unittests/TargetParser/TripleTest.cpp`):
   for each pair (`x86_64-unknown-linux-gnu`, `x86_64-unknown-linux-musl`,
   `x86_64-pc-windows-msvc`, `x86_64-w64-windows-gnu`, `aarch64-unknown-linux-gnu`,
   `aarch64-pc-windows-msvc`, `aarch64-apple-darwin`, `i686-pc-windows-msvc`),
   `EXPECT_EQ(Triple(N + "-msil").computeDataLayout(), Triple(N).computeDataLayout())`.
   The expected strings are in 3.2. Also
   `EXPECT_EQ(Triple("x86_64-unknown-linux-gnu-msil").getDefaultExceptionHandling(), ExceptionHandling::None)`.
2. **Registry** (`llvm/unittests/MC/TargetRegistry.cpp`, with the `"msil"` exemption):
   `lookupTarget(Triple("x86_64-unknown-linux-gnu-msil"))->getName() == "msil"`;
   `lookupTarget(Triple("x86_64-unknown-linux-gnu"))` is still `x86-64` when X86 is built;
   `lookupTarget("msil", T = "x86_64-unknown-linux-gnu")` sets `T.isOSBinFormatMSIL()`;
   `lookupTarget("x86-64", T = "...-msil")` fails.
3. **llc lit** (`llvm/test/CodeGen/MSIL/target-selection.ll`, `REQUIRES: msil-registered-target`):
   - `llc -mtriple=x86_64-unknown-linux-gnu-msil < %s 2>&1 | FileCheck %s --implicit-check-not="not a recognized"`
     on IR carrying clang's x86 attributes (`"target-cpu"="x86-64"`,
     `"target-features"="+cmov,...,+avx2"`).
   - `llc -march=msil -mtriple=x86_64-unknown-linux-gnu` works the same.
   - `not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj` checks for the
     "object file emission is not implemented" message.
   - `llc --version | FileCheck` checks for a `msil` entry.
4. **clang** (`clang/test/CodeGen/MSIL/datalayout.c`):
   `%clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm` checks
   `target datalayout = "e-m:e-p270:32:32-...-S128"`. The Windows triple gives
   `e-m:w-...`. `-S` (through MSIL codegen) produces no
   `err_data_layout_mismatch` and no "not a recognized" warnings.
5. **TTI** (`llvm/test/Transforms/LoopVectorize/MSIL/novector.ll`):
   `opt -passes=loop-vectorize,slp-vectorizer -mtriple=x86_64-unknown-linux-gnu-msil`
   produces no vector types. `opt -O2` on the `swstr` pattern from section 6 produces
   no `llvm.load.relative`.

---

## 9. Cross-report dependencies, risks, open questions

- **Triple owner:** this report assumes `Triple::MSIL`, `isOSBinFormatMSIL()`,
  `getObjectFormatTypeName(MSIL)=="msil"` and `parseFormat` `.EndsWith("msil")`.
  Verified side effects:
  - Stock clang 22 rejects `x86_64-unknown-linux-gnu-msil` with "version '-msil' in
    target triple ... is invalid". The check is `clang/lib/Driver/Driver.cpp:1788-1799`,
    with `Triple::getEnvironmentVersionString` at `Triple.cpp:1459-1479`. It only
    strips a `-<format>` suffix when the format is known, so the new enum fixes it.
  - Stock clang **drops** `-msil` from `x86_64-pc-windows-msvc-msil`: the Win32
    branch of `Triple::normalize` (`Triple.cpp:1360-1383`) keeps a 5th component
    only for a known non-COFF format. `MSVCToolChain::ComputeEffectiveClangTriple`
    (`MSVC.cpp:991-1012`) then preserves the suffix (`msvc19.xx.y-msil`).
  - Add the `getDefaultObjectFormat()` helper of 3.3, and the
    `getDefaultExceptionHandling()` change of 3.4.
- **MC owner:** `MCContext.cpp:85` needs an environment for `Triple::MSIL` (a new
  `IsMSIL`, or deliberately reuse one). `MCObjectFileInfo` must not assert.
  `MSILMCAsmInfo` must keep `ExceptionsType == None`.
- **clang wrapper owner:** keep the MSIL triple in `TargetInfo::Triple` and never call
  `resetDataLayout(StringRef)`. Several native-TargetInfo decisions read the object
  format and silently change for MSIL triples:
  - `UserLabelPrefix` is `"_"` only for MachO (`TargetInfo.cpp:167`), so
    `__USER_LABEL_PREFIX__` differs from native Darwin.
  - x86 `MaxVectorAlign`/`MaxTLSAlign` depend on Windows COFF (`X86.h:176-178`).
  - `supportsIFunc()` (`TargetInfo.h:1572-1585`).
  - `Triple::supportsCOMDAT()` (`Triple.h:1282-1285`) is true for MSIL.
  - Darwin ARM selection uses `isOSBinFormatMachO` (`Targets.cpp:217`), and x86_64
    uses `isOSDarwin() || isOSBinFormatMachO()` (`Targets.cpp:634`).

  Also override `isNoProtoCallVariadic` and add `"no-prototype"` (section 6).
- **Driver owner:** the x86 `-mllvm` options (`-x86-asm-syntax`, align-branch) and
  clang-cl `/GS` (stack protector) should be suppressed for MSIL triples. Choose
  `-fno-stack-protector`, `-exception-model` none, and do not pass `-tune-cpu`
  (harmless either way after 4.4).
- **Risk:** the `"msil"` name is hard-coded in generic `TargetRegistry.cpp` (Option A).
  It is acceptable for a fork; Option B removes it.
- **Risk:** `ModuleSymbolTable.cpp:81` asserts on module asm for targets without an
  asm parser. Any path that lets file-scope `asm()` through for MSIL crashes
  asserts-enabled builds.
- **Risk:** `-march=msil` drops an environment version when it rewrites the triple
  (`setObjectFormat`).
- **Open:** should `x86` (i686) be accepted by `checkMSILTriple` now, as the
  `i686-pc-windows-msvc-msil` row of Design.md 3 suggests ("later")? Recommended: no,
  not in M1.
- **Open:** where do i128 and complex helper libcalls (`__divti3`, `__mulsc3`) come
  from on host-libc triples: the runtime assembly, or P/Invoke into libgcc_s /
  compiler-rt? This affects the RuntimeLibcalls set for MSIL.
- **Open:** if a target-independent `-mcpu`/`-mattr` story is wanted for MSIL itself
  (for example `-mattr=+vector128`), it needs a namespace that does not collide with
  native feature names, because clang validates `-target-feature` against the native
  TargetInfo.

---

## 10. Verified experiments (reproducible)

Scratch dir: `C:\Users\JEROME~1\AppData\Local\Temp\claude\D--llvm-msil\fa776849-ce33-4cee-bcc7-05382d063d3e\scratchpad\recon\registry-tm\`
(`portable.c`, `simd.c`, `libc.c`, `tiny.c`, `dl.c` and their `.ll` outputs).
Compiler: `C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\Llvm\x64\bin\clang.exe`,
clang 22.1.3. There is no build of this checkout, so these show stock-LLVM
behavior of the same code paths.

1. Native DataLayout strings for all eight Design.md triples (table in 3.2), plus the
   `-elf` variants showing the object-format dependence.
2. `clang -###` for x86_64-linux: `-target-cpu x86-64 -tune-cpu generic`,
   `-mrelocation-model pic -pic-level 2 -pic-is-pie`, and no `-target-feature`.
3. x86_64-linux and x86_64-windows `-O2` IR for portable C: the attributes, module
   flags and intrinsics listed in section 6, including `llvm.load.relative.i64` +
   `switch.table.swstr.rel`, `<2 x float>` ABI coercion, `x86_fp80`, `fastcc` on
   internal functions, and variadic K&R calls.
4. glibc TLI rewrites at `-O2`: `bcmp`, `puts`, `putchar`, `memset`, `ldexp`.
5. `<immintrin.h>`: which intrinsics stay `llvm.x86.*` and which become generic IR.
6. Backend reaction to x86 CPU/features: WebAssembly prints the "not a recognized
   processor/feature" warnings (2x per feature, 4x for the CPU). NVPTX is silent.
   SPIRV is not built in VS clang.
7. Stock clang's handling of `-msil` suffixes: the Linux and Darwin triples are
   rejected ("version ... invalid"), and the Windows triple loses the suffix.
