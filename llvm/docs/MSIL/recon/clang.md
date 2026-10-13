# Recon: clang support for MSIL triples (milestone 1)

Key: `clang`. Author: recon agent. Date: 2026-10-12. Checkout: `msil-milestone1` @ `8da03434abcc`.

Scope: clang frontend `TargetInfo`, inline-asm rejection, CodeGen object-format
dependencies, the driver, and tests, for `<native triple>-msil` triples
(llvm/docs/MSIL/Design.md section 3). Every path and line number below was read in this
checkout. Items marked **(unverified)** were not checked. "Observed" means I ran the
command with the stock clang 22.1.3 that ships with VS 2026
(`C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\Llvm\x64\bin\clang.exe`)
on the corresponding native triple. No fork build exists yet.

---

## 0. Summary and ordered work list

Milestone-1 changes, in order. Each depends only on the LLVM-side triple work in section 1.

| # | File | Change | Why |
|---|------|--------|-----|
| 1 | `clang/lib/Basic/Targets/MSIL.h` (new, header only) | `template <class Native> class MSILTargetInfo final : public Native`. The constructor and `adjust()` set long double to double. `getTargetDefines()` adds `__MSIL__`. `adjust()` rejects `-mlong-double-80/128`. | Design.md 3 "thin wrapper" |
| 2 | `clang/lib/Basic/Targets.cpp:111` | At the top of `AllocateTarget`: `if (Triple.isOSBinFormatMSIL()) return AllocateMSILTarget(Triple, Opts);`, plus a static `AllocateMSILTarget` that mirrors the native dispatch for the supported arch/OS pairs only | Wrap the native TargetInfo |
| 3 | `clang/include/clang/Basic/DiagnosticCommonKinds.td:297` | New `err_msil_asm_unsupported` | Inline-asm error that names the target |
| 4 | `clang/lib/CodeGen/CGStmt.cpp:2739` | `CodeGenFunction::EmitAsmStmt`: on MSIL, report the error. Exception: the empty `asm("" ::: "memory")` barrier becomes a single-thread fence. | Reject GNU and MS inline asm, only in code that is actually emitted |
| 5 | `clang/lib/CodeGen/CodeGenModule.cpp:8198` | `case Decl::FileScopeAsm`: on MSIL, report the error and skip `appendModuleInlineAsm` | Reject file-scope asm. It also avoids the `ModuleSymbolTable` assert that registry-tm.md mentions. |
| 6 | `clang/lib/Parse/ParseStmtAsm.cpp:512` | `ParseMicrosoftAsmStatement`: on MSIL, skip the MC-based parse and build an opaque `MSAsmStmt` | Without this, MS asm would go through `lookupTarget` to the MSIL target, which has no asm parser |
| 7 | `clang/lib/CodeGen/CodeGenModule.cpp:1718` | `if (getTriple().isOSLinux())` becomes `if (getTriple().isOSLinux() && getTriple().isOSBinFormatELF())` | **Must fix** for `aarch64-*-linux-*-msil`: line 1722 `assert(isOSBinFormatELF())` fires in every compile with assertions enabled |
| 8 | `BackendUtil.cpp:280`, `CodeGenModule.cpp:7509`, `CGObjCMac.cpp:5942`, `SemaChecking.cpp:3213` | Add `Triple::MSIL` to the object-format switches and to the CFString unsupported-format list | `-Wswitch` warnings, and `llvm_unreachable` on `__builtin___CFStringMakeConstantString` |
| 9 | `clang/lib/Driver/ToolChains/Clang.cpp:5499` and `:9261` | When MSIL would emit an object file, report `err_drv_msil_no_object_output` (new driver diagnostic) | `-c` and linking fail early with a clear message until phase 2 |
| 10 | `clang/include/clang/Basic/TargetInfo.h:1572` (optional) | `supportsIFunc()` returns false for MSIL | `ifunc` is accepted today on aarch64-windows |
| 11 | Tests (section 6) | Preprocessor predefine diff, driver `-###`, CodeGen long double, CodeGen inline asm | |

**The driver needs no toolchain change for `-S`.** `x86_64-unknown-linux-gnu-msil` resolves to
`toolchains::Linux` and `x86_64-pc-windows-msvc-msil` resolves to `toolchains::MSVCToolChain`
(Driver.cpp:6195-6217, 6274-6283). Both pass the triple through with the `-msil` suffix
intact (MSVC.cpp:1003-1010 keeps it explicitly). None of the object-format checks in the
driver blocks `-S` (section 5.3). Do **not** change `IsIntegratedAssemblerDefault`
(section 5.5).

---

## 1. Assumed LLVM-side API (owned by the triple/registry agent; see registry-tm.md)

clang code below assumes:

```cpp
// llvm/include/llvm/TargetParser/Triple.h
enum ObjectFormatType { UnknownObjectFormat, COFF, DXContainer, ELF, GOFF, MachO,
                        MSIL, SPIRV, Wasm, XCOFF };   // MSIL added
bool isOSBinFormatMSIL() const { return getObjectFormat() == Triple::MSIL; }
// getObjectFormatTypeName(MSIL) == "msil"; parseFormat(): EndsWith("msil") -> MSIL
```

clang also depends on these LLVM-side choices:

- **`Triple::normalize` keeps `-msil`.** The Windows branch (Triple.cpp:1359-1383) already
  keeps a non-COFF suffix as the 5th component (`x86_64-pc-windows-msvc-msil`). Linux
  keeps `gnu-msil`. Observed with `-elf` today: `--target=x86_64-pc-windows-msvc-elf`
  yields `-triple x86_64-pc-windows-msvc19.51.36260-elf`.
- **`Triple::getEnvironmentVersionString()` strips `-msil`** (Triple.cpp:1459-1479 does it
  for any known format). The driver's check at Driver.cpp:1789-1800 then accepts
  `gnu-msil` and `msvc19.xx-msil`.
- **`TargetRegistry::lookupTarget` returns the MSIL target for an MSIL triple and never
  falls back to X86 or AArch64.** If it fell back, `-S` would silently produce x86 assembly.
- **`Triple::computeDataLayout()` equals the native layout** (registry-tm.md section 3.3).
  clang's `TargetInfo::resetDataLayout()` (TargetInfo.cpp:225-226) and the BackendUtil
  check (BackendUtil.cpp:1561-1570) both call it, so clang needs no data-layout code.
- **`Triple::getDefaultLongDoubleFormat()`** (Triple.cpp:2552) should return
  `LongDoubleFormat::IEEEdouble` for MSIL. `RuntimeLibcalls.cpp:41` uses it to choose
  libcalls. `Module::getLongDoubleFormat()` (Module.cpp:685-693) falls back to it when the
  module has no `"long-double-type"` flag. clang emits that flag (CodeGenModule.cpp:1475-1493)
  only when long double is referenced. Suggested first line:
  `if (isOSBinFormatMSIL()) return LongDoubleFormat::IEEEdouble;`
- **`Triple::hasDefaultEmulatedTLS()` stays false for MSIL.** If it were true, the driver
  would pass `-femulated-tls` and LLVM would rewrite TLS to `__emutls_get_address`,
  bypassing Design.md 4.4.
- **`Triple::supportsCOMDAT()`** (Triple.h:1282) is true for MSIL unless changed. clang
  follows it (`CodeGenModule::supportsCOMDAT`, CodeGenModule.cpp:6516). On
  `*-windows-msvc-msil`, C code produces comdats (section 4.4), so the backend must
  either handle them or this must return false for MSIL.

---

## 2. Frontend TargetInfo

### 2.1 What AllocateTarget does today

- `clang/lib/Basic/Targets.cpp:111` `AllocateTarget(const llvm::Triple&, const TargetOptions&)`
  is a switch on `Triple.getArch()`, then on OS and environment. It returns
  `std::make_unique<OSWrapper<ArchTargetInfo>>` or a concrete class such as
  `MicrosoftX86_64TargetInfo`. Relevant cases:
  - x86_64: Targets.cpp:633-698. `isOSDarwin() || isOSBinFormatMachO()` gives `DarwinX86_64TargetInfo`. Linux gives `LinuxTargetInfo<X86_64TargetInfo>` (Android and OHOS have their own classes). Win32 gives `MinGWX86_64TargetInfo` for GNU, `CygwinX86_64TargetInfo` for Cygnus, and `MicrosoftX86_64TargetInfo` for MSVC or any other environment.
  - aarch64: Targets.cpp:145-195. Linux gives `LinuxTargetInfo<AArch64leTargetInfo>`. Win32 gives `MinGWARM64TargetInfo` or `MicrosoftARM64TargetInfo`. Darwin gives `DarwinAArch64TargetInfo`.
  - x86: Targets.cpp:574-631. Win32 MSVC gives `MicrosoftX86_32TargetInfo`.
  - wasm32/wasm64 return nullptr unless `isOSBinFormatWasm()` (Targets.cpp:733-737, 762-766), so `wasm32-...-msil` is rejected automatically.
- `AllocateTarget` is also called by `Targets/NVPTX.cpp:85` and `Targets/SPIR.h:102` (host
  target for offloading) and by `clang/lib/CIR/Dialect/Transforms/TargetLowering/LowerModule.cpp:95`.
  Hooking `AllocateTarget` itself therefore covers every caller.
- `TargetInfo::CreateTargetInfo` (Targets.cpp:840-926) runs, in order: `AllocateTarget`,
  `setCPU`, `initFeatureMap`, `handleTargetFeatures`, `validateTarget`. Later
  `CompilerInstance::createTarget` calls `adjust()` (CompilerInstance.cpp:168).
- OS wrappers (`clang/lib/Basic/Targets/OSTargets.h`) are templates.
  `OSTargetInfo<TgtInfo>` (lines 20-34) overrides `getTargetDefines` to call
  `TgtInfo::getTargetDefines` and then `getOSDefines`. `LinuxTargetInfo` (377-443) sets
  `HasFloat128 = true` for x86 and x86_64 (lines 428-431). `WindowsTargetInfo` (904-918)
  sets a 16-bit `wchar_t` and `UseMicrosoftManglingForC`.

### 2.2 Options considered

| Option | Verdict |
|---|---|
| **A. A template wrapper around "whatever AllocateTarget returned"** | Not possible as stated. The returned object's dynamic type is known only at run time, and a C++ template needs the static type. Turning the whole switch into `template<template<class> class W>` would touch about 150 lines and fail to compile for `final` classes (`AMDGPUTargetInfo`, `SPIRV64AMDGCNTargetInfo`, `SPIRV64IntelTargetInfo`). |
| **B. A delegating `TargetInfo` that owns the native one and forwards everything** | Rejected. `TargetInfo.h` has 128 `virtual` methods, and many properties are protected data members read directly (`LongDoubleWidth`, `HasFloat128`, `TLSSupported`, `DataLayoutString`, ...). A virtual that upstream adds later would silently fall back to the base default. |
| **C. Base-class hooks keyed on `getTriple().isOSBinFormatMSIL()`** (long double in `TargetInfo::adjust`, `__MSIL__` in InitPreprocessor.cpp next to `__ELF__`) | Works, with no duplication. The MSIL logic is spread over 2-3 shared files, and `adjust()` runs after `handleTargetFeatures` (section 2.4). Kept as the fallback (2.6). |
| **D. (Recommended) `MSILTargetInfo<Native>` template applied in a small MSIL-only dispatch** | Follows the existing OS-wrapper pattern. All TargetInfo behavior for MSIL sits in one class, which is the natural place for later overrides (calling conventions, builtins, `supportsIFunc`). Only the supported pairs are instantiated (about 9 classes), and every other `*-msil` triple gets `err_target_unknown_triple`. The cost is mirroring roughly 30 lines of native dispatch. A predefine-diff test (6.1) catches drift. |

### 2.3 Recommended implementation

New file `clang/lib/Basic/Targets/MSIL.h`. It is header only, so CMake needs no change.

```cpp
//===--- MSIL.h - MSIL (CIL) object-format TargetInfo wrapper ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A "<native triple>-msil" target is the native platform and ABI, emitted as
// CIL (llvm/docs/MSIL/Design.md, section 3). MSILTargetInfo keeps the native
// TargetInfo unchanged except that:
//  * __MSIL__ is predefined, and
//  * long double is IEEE double (with double's size and alignment).
// Inline assembly is rejected in CodeGen (CGStmt.cpp, CodeGenModule.cpp), not
// here, so that unused static inline functions in system headers that contain
// asm (clang's intrin.h, glibc's bits/fenv.h) still compile.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_MSIL_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_MSIL_H

#include "Targets.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/Twine.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>

namespace clang {
namespace targets {

template <typename NativeTargetInfo>
class LLVM_LIBRARY_VISIBILITY MSILTargetInfo final : public NativeTargetInfo {
  // CIL has no extended-precision type. This mirrors what TargetInfo::adjust()
  // does for -mlong-double-64 (TargetInfo.cpp:540-544).
  void useDoubleForLongDouble() {
    this->LongDoubleWidth = this->DoubleWidth;
    this->LongDoubleAlign = this->DoubleAlign;
    this->LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  }

public:
  MSILTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : NativeTargetInfo(Triple, Opts) {
    assert(Triple.isOSBinFormatMSIL() && "not an MSIL triple");
    // Set before handleTargetFeatures(): X86.cpp:510-511 disables long double
    // under -mno-x87 only while the format is x87DoubleExtended.
    useDoubleForLongDouble();
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    NativeTargetInfo::getTargetDefines(Opts, Builder);
    Builder.defineMacro("__MSIL__");
  }

  void adjust(DiagnosticsEngine &Diags, LangOptions &Opts,
              const TargetInfo *Aux) override {
    NativeTargetInfo::adjust(Diags, Opts, Aux);
    // -mlong-double-80/-128 cannot be honored. TargetInfo::adjust() has just
    // applied them (TargetInfo.cpp:540-563), so diagnose and undo.
    if (Opts.LongDoubleSize && Opts.LongDoubleSize != this->DoubleWidth)
      Diags.Report(diag::err_opt_not_valid_on_target)
          << ("-mlong-double-" + llvm::Twine(Opts.LongDoubleSize)).str();
    useDoubleForLongDouble();
  }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_MSIL_H
```

`clang/lib/Basic/Targets.cpp`:

```diff
 #include "Targets/M68k.h"
 #include "Targets/MSP430.h"
+#include "Targets/MSIL.h"
 #include "Targets/Mips.h"
@@
 //===----------------------------------------------------------------------===//
 // Driver code
 //===----------------------------------------------------------------------===//
 
+/// <native triple>-msil (Design.md section 3): the native TargetInfo, wrapped.
+/// This mirrors the native dispatch in AllocateTarget below, restricted to the
+/// platforms Design.md supports. Keep the two in sync.
+/// clang/test/Preprocessor/init-msil.c diffs native and -msil predefines to
+/// catch drift.
+static std::unique_ptr<TargetInfo>
+AllocateMSILTarget(const llvm::Triple &Triple, const TargetOptions &Opts) {
+  const llvm::Triple::OSType OS = Triple.getOS();
+  const bool IsMinGW = OS == llvm::Triple::Win32 &&
+                       Triple.getEnvironment() == llvm::Triple::GNU;
+  const bool IsMSVC = Triple.isWindowsMSVCEnvironment(); // msvc or no env
+  const bool IsPlainLinux = OS == llvm::Triple::Linux && !Triple.isAndroid() &&
+                            !Triple.isOHOSFamily() && !Triple.isX32();
+  switch (Triple.getArch()) {
+  case llvm::Triple::x86_64:
+    if (Triple.isOSDarwin()) // Phase 4. The driver's Darwin path is untested.
+      return std::make_unique<MSILTargetInfo<DarwinX86_64TargetInfo>>(Triple,
+                                                                      Opts);
+    if (IsPlainLinux) // glibc and musl
+      return std::make_unique<
+          MSILTargetInfo<LinuxTargetInfo<X86_64TargetInfo>>>(Triple, Opts);
+    if (IsMinGW)
+      return std::make_unique<MSILTargetInfo<MinGWX86_64TargetInfo>>(Triple,
+                                                                     Opts);
+    if (IsMSVC)
+      return std::make_unique<MSILTargetInfo<MicrosoftX86_64TargetInfo>>(
+          Triple, Opts);
+    return nullptr;
+  case llvm::Triple::aarch64:
+    if (Triple.isOSDarwin())
+      return std::make_unique<MSILTargetInfo<DarwinAArch64TargetInfo>>(Triple,
+                                                                       Opts);
+    if (IsPlainLinux)
+      return std::make_unique<
+          MSILTargetInfo<LinuxTargetInfo<AArch64leTargetInfo>>>(Triple, Opts);
+    if (IsMinGW)
+      return std::make_unique<MSILTargetInfo<MinGWARM64TargetInfo>>(Triple,
+                                                                    Opts);
+    if (IsMSVC && !Triple.isWindowsArm64EC())
+      return std::make_unique<MSILTargetInfo<MicrosoftARM64TargetInfo>>(Triple,
+                                                                        Opts);
+    return nullptr;
+  case llvm::Triple::x86: // i686-pc-windows-msvc-msil (later)
+    if (IsMSVC)
+      return std::make_unique<MSILTargetInfo<MicrosoftX86_32TargetInfo>>(
+          Triple, Opts);
+    return nullptr;
+  default:
+    return nullptr; // reported as err_target_unknown_triple
+  }
+}
+
 std::unique_ptr<TargetInfo> AllocateTarget(const llvm::Triple &Triple,
                                            const TargetOptions &Opts) {
+  if (Triple.isOSBinFormatMSIL())
+    return AllocateMSILTarget(Triple, Opts);
+
   llvm::Triple::OSType os = Triple.getOS();
```

Notes:
- None of the wrapped classes is `final`. Their `getTargetDefines` and `adjust` are plain
  `override`s that each call their base. I checked `X86.h:425-428`, `X86.h:834-837`,
  `X86.cpp:1899-1910` (MicrosoftX86_64), and every OS wrapper.
- The wrapper is outermost, so OS defines come first and `__MSIL__` last. `adjust()`
  runs after all native adjustments.

### 2.4 Exactly what long double becomes

| Native class | Native LongDouble (Width/Align/Format), source | MSIL |
|---|---|---|
| `LinuxTargetInfo<X86_64TargetInfo>` | 128/128/x87DoubleExtended (X86.h:713-714 and the X86TargetInfo constructor at X86.h:171) | 64/64/IEEEdouble |
| `MinGWX86_64TargetInfo` | 128/128/x87 (X86.h:979-981) | 64/64/IEEEdouble |
| `MicrosoftX86_64TargetInfo` | 64/64/IEEEdouble already (X86.h:941-942) | unchanged |
| `MicrosoftX86_32TargetInfo` | 64/64/IEEEdouble already (X86.h:591-592) | unchanged |
| `LinuxTargetInfo<AArch64leTargetInfo>` | 128/128/IEEEquad (AArch64.cpp:165-166) | 64/64/IEEEdouble |
| `MicrosoftARM64TargetInfo`, `DarwinAArch64TargetInfo` | 64/64/IEEEdouble already (AArch64.cpp:1796-1797, 1886-1887) | unchanged |

What changes as a consequence (observed with stock clang using `-mlong-double-64`, which
sets the same fields):
- Predefines: all 18 `__LDBL_*` macros and `__SIZEOF_LONG_DOUBLE__` (16 becomes 8).
  Nothing else changes. `__DECIMAL_DIG__` is defined as `__LDBL_DECIMAL_DIG__`, so its
  text is identical. `__BIGGEST_ALIGNMENT__` uses `SuitableAlign`, which stays 128 on
  x86_64 and AArch64 (X86.h:717, AArch64.cpp:165).
- `alignof(max_align_t)` on `*-linux-*-msil` drops from 16 to 8.
  `clang/lib/Headers/__stddef_max_align_t.h:18-24` aligns its member to
  `__alignof__(long double)`. This is an ABI difference from native glibc for structs
  that embed `max_align_t` (rare). Document it next to the existing long double
  limitation.
- `TargetInfo::getNewAlign()` (TargetInfo.h:760-762) falls to 8 when `NewAlign` is unset.
  This affects C++ only.
- On x86_64 the module flag `!"long-double-type", !"double"` is emitted whenever long
  double is used (CodeGenModule.cpp:1475-1493). Stock-clang bitcode for
  `x86_64-unknown-linux-gnu` says `"x86_fp80"`, and the flag has `Error` behavior
  (Module.cpp:695-698). IR-linking stock bitcode that uses long double with MSIL bitcode
  therefore fails cleanly. Mention this under Design.md 3 "stock-clang compatibility".

### 2.5 ABI verification: nothing breaks for x86_64 or AArch64

`createTargetCodeGenInfo` (CodeGenModule.cpp:120-334) selects the ABI by arch and OS
only. The ABI code already handles an IEEE-double long double because
`-mlong-double-64` exists:

- `X86_64ABIInfo::classify`, scalar case (clang/lib/CodeGen/Targets/X86.cpp:2016-2027):
  `IEEEquad` gives SSE/SSEUp, `x87DoubleExtended` gives X87/X87Up, and **`IEEEdouble`
  gives `Current = SSE`**. The `X87` and `ComplexX87` result types (`getX86_FP80Ty`,
  X86.cpp:2858-2870) are reached only from those classes, so `x86_fp80` never appears.
- `_Complex long double` (X86.cpp:2150-2158): `IEEEdouble` gives `Lo = Hi = SSE`, i.e.
  two doubles in XMM registers.
- `isX86VectorTypeForVectorCall` (X86.cpp:50-56) excludes only x87 long double.
- Win64 `WinX86_64ABIInfo::classify` (X86.cpp:3651-3660): the MinGW x87 indirect path
  runs only when the format is x87. With double it falls through to direct passing.
  MinGW-msil therefore passes long double like MSVC.
- `va_arg(ap, long double)` on SysV goes through `X86_64ABIInfo::EmitVAArg`, which uses
  the classification above (SSE). It reads `fp_offset` from the `__va_list_tag`.
  Observed on stock clang:
  `clang -cc1 -triple x86_64-unknown-linux-gnu -mlong-double-64 -O0 -emit-llvm`
  gives `%fits_in_fp = icmp ule i32 %fp_offset, 160`, then a `load double` from
  `reg_save_area + fp_offset`, or from `overflow_arg_area` with an 8-byte step. With
  `-O1`: `define dso_local noundef double @add(double noundef %a, double noundef %b)`,
  `define dso_local { double, double } @cadd(double, double, double, double)`,
  `@g = global double 1.0, align 8`, and `sizeof` returns 8 with `_Alignof` 8.
  Design.md 4.7 (a native `va_list` over a buffer) is therefore consistent: callee and
  callers agree that long double is an SSE-class 8-byte value.
- AArch64 Linux (observed, `-mlong-double-64`): parameters are `double`. `_Complex long
  double` is an HFA `[2 x double] alignstack(8)`. `va_arg` uses the `vr_offs` path with a
  16-byte step. All consistent.
- Other `long double` consumers in CodeGen read the format and do not assume x87:
  CGBuiltin.cpp:238-242, 2787, 4205; CGExprScalar.cpp:3585; CGAtomic.cpp:872;
  CGExprComplex.cpp:814 and 1080 (type-driven).

### 2.6 Fallback (option C), for reference

If reviewers reject the mirrored dispatch, the same behavior fits in about 10 lines and
needs no new class:

```diff
--- clang/lib/Basic/TargetInfo.cpp  (TargetInfo::adjust, after the LongDoubleSize block, ~line 563)
+  if (getTriple().isOSBinFormatMSIL()) {
+    if (Opts.LongDoubleSize && Opts.LongDoubleSize != DoubleWidth)
+      Diags.Report(diag::err_opt_not_valid_on_target)
+          << ("-mlong-double-" + Twine(Opts.LongDoubleSize)).str();
+    LongDoubleWidth = DoubleWidth;
+    LongDoubleAlign = DoubleAlign;
+    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
+  }
--- clang/lib/Frontend/InitPreprocessor.cpp  (next to __ELF__, line 1526)
+  if (TI.getTriple().isOSBinFormatMSIL())
+    Builder.defineMacro("__MSIL__");
```

Caveat: under option C, `handleTargetFeatures` still sees x87 long double, so
`-mno-x87` on x86 sets `HasLongDouble = false` (X86.cpp:510-511). Option C would also
have to reject non-MSIL-capable arches in `CreateTargetInfo`.

### 2.7 Native behaviors that change only because a native constructor tests the object format

These follow from "native TargetInfo, MSIL triple". None matters for milestone 1.

| Where | Native | `-msil` |
|---|---|---|
| InitPreprocessor.cpp:1526-1528 `__ELF__` | defined on Linux | **not defined**. Open question: I recommend leaving it undefined, since code that tests `__ELF__` usually wants ELF sections, symver, or visibility tricks. |
| Driver Clang.cpp:8582-8588 `-fdwarf2-cfi-asm`, which drives `__GCC_HAVE_DWARF2_CFI_ASM` | ELF and MachO only | not passed (correct, no CFI) |
| X86.h:175-178 `MaxVectorAlign`/`MaxTLSAlign = 8192*8` | Windows COFF | 0 (no cap). Harmless. |
| X86.h:571-578 (`WindowsX86_32TargetInfo`) `UserLabelPrefix = "_"` | i686 Windows COFF | `""`. With registry-tm's native data layout (`m:x`), the LLVM Mangler still adds `_`, so `__USER_LABEL_PREFIX__` would disagree with emitted names on i686. Revisit with the i686 triple. The `Layout` string at X86.h:573-576 is dead code: `resetDataLayout()` with no arguments ignores it. |
| TargetInfo.cpp:167 `UserLabelPrefix = "_"` | MachO | `""`. Matters for Darwin-msil only (phase 4). |
| TargetInfo.h:1572-1585 `supportsIFunc()` | true on Linux ELF (non-musl), MachO, aarch64 Windows | true only on aarch64 Windows, through the OS check. Recommend returning false for MSIL (work-list item 10). |

### 2.8 Deliberately not changed in milestone 1 (decide later)

- `HasFloat128` (LinuxTargetInfo, OSTargets.h:428-431; MinGW X86.h:981) defines
  `__FLOAT128__` and `__SIZEOF_FLOAT128__`, and `__float128` lowers to `fp128`. The
  backend must soften `fp128` or reject it. Alternative: set `this->HasFloat128 = false`
  in the wrapper constructor, which gives an early Sema error "__float128 is not
  supported on this target". That changes two predefines.
  **(unverified)** Whether glibc's `bits/floatn.h` enables `_Float128` under clang
  (`__GNUC_PREREQ(4,3)` is false with clang's `__GNUC__ 4.2`).
- `HasFloat16` and `HasBFloat16` (X86 with SSE2): `half` and `bfloat` IR need
  promotion/libcalls in the backend.
- Calling conventions: `X86_64TargetInfo::checkCallingConvention` (X86.h:753-771) and
  `WindowsX86_64TargetInfo` (X86.h:910-930) accept `vectorcall`, `regcall`,
  `preserve_*`, `swift`, `ms_abi`, and `sysv_abi`, so the IR may carry non-`ccc`
  conventions. On Windows MSVC, C names also change: `__vectorcall` gives `foo@@N`
  (Mangle.cpp:102-126 through `shouldUseMicrosoftCCforMangling`). A later wrapper override
  can return `CCCR_Warning` for everything except `CC_C`, plus the default `CC_Win64` or
  `CC_X86_64SysV`. i686 must keep stdcall, cdecl, and fastcall for P/Invoke (Design.md 3.1).

---

## 3. Inline assembly rejection

### 3.1 Candidate hooks

| Hook | Mechanism | Assessment |
|---|---|---|
| Parser, by setting `LangOpts.GNUAsm = false` (what `-fno-gnu-inline-asm` does) | ParseStmtAsm.cpp:695-699 and Parser.cpp:856-859 emit `err_gnu_inline_asm_disabled` for any non-empty GNU asm, including file-scope asm. `__has_extension(gnu_asm)` then returns 0 (Features.def:372). | **Rejected.** It fires on every parsed asm, including unused static inline functions in system headers. Observed: `clang --target=x86_64-pc-windows-msvc -fno-gnu-inline-asm -ffreestanding -fsyntax-only` on `#include <intrin.h>` + `<immintrin.h>` gives **18 errors**, all inside clang's own `intrin.h` (lines 247, 264, 281, ...: `__movsb`, `__stosd`, `__readmsr`, `__halt`, `__inbyte`, ...). Those functions are unconditional for x86 (intrin.h:245-333). The same unit compiled normally contains **0** `asm` calls in the IR because nothing uses them. glibc's x86 `bits/fenv.h` extern-inline helpers (under `__USE_EXTERN_INLINES` at `-O2`) and old `bits/select.h` are the same kind of case (from memory, **unverified**). The message also does not name MSIL. |
| Sema (`ActOnGCCAsmStmt`, `ActOnMSAsmStmt`, `ActOnFileScopeAsmDecl`, SemaStmtAsm.cpp and SemaDecl.cpp:21403) | Same problem: runs at parse time for every function body. | Rejected for the same reason. |
| **CodeGen** (`CodeGenFunction::EmitAsmStmt`, CGStmt.cpp:2739; `CodeGenModule::EmitTopLevelDecl` `case Decl::FileScopeAsm`, CodeGenModule.cpp:8198) | Diagnoses only what is emitted. One place covers `GCCAsmStmt` and `MSAsmStmt` (CGStmt.cpp:166-167 dispatches both to `EmitAsmStmt`). Uses a dedicated diagnostic that names the target. | **Recommended.** Precedent for asm-target handling in CodeGen: `EmitHipStdParUnsupportedAsm` (CGStmt.cpp:2705-2719). |

How other targets handle it: no clang target rejects inline asm in the frontend. Every
in-tree target accepts it, and the backend diagnoses bad asm through
`DiagnosticInfoInlineAsm` and `!srcloc`. The nearest precedents are
`-fno-gnu-inline-asm` (Parser), `err_msasm_unsupported_arch` (Parser, for MS asm on
non-x86, ParseStmtAsm.cpp:513-514), and HIP stdpar (CodeGen).

MS asm needs one Parser change. With `-fasm-blocks` (the default when
`-fms-extensions` is on, Options.td:3660-3661, so for `*-windows-msvc-msil`),
`ParseMicrosoftAsmStatement` calls `llvm::TargetRegistry::lookupTarget(TheTriple)`
(ParseStmtAsm.cpp:517) because the triple is x86. That returns the MSIL target, which has
no MC asm parser and is configured with x86 CPU and feature strings. The parse must
therefore skip MC for MSIL and leave the diagnostic to CodeGen.

### 3.2 Recommended implementation

Diagnostic (Common, so Parse, CodeGen and Basic can all use it), in
`clang/include/clang/Basic/DiagnosticCommonKinds.td`, inside the existing
`let CategoryName = "Inline Assembly Issue"` block at line 297:

```diff
 // Sema && Frontend
 let CategoryName = "Inline Assembly Issue" in {
+def err_msil_asm_unsupported : Error<
+  "%select{inline assembly|Microsoft-style inline assembly|file-scope assembly}0 "
+  "is not supported for the MSIL target '%1'">;
 def err_asm_invalid_type_in_input : Error<
```

`clang/lib/CodeGen/CGStmt.cpp`, before line 2739:

```diff
+/// MSIL has no machine code to put assembly into (Design.md section 3).
+/// The only form accepted is the empty GNU barrier idiom
+/// `asm volatile("" ::: "memory")`, which becomes a compiler-only fence.
+/// Any other GNU or MS asm statement is an error. Only emitted functions
+/// reach here, so unused asm in system headers (intrin.h) is fine.
+static void emitMSILAsmStmt(CodeGenFunction &CGF, const AsmStmt &S) {
+  if (const auto *GS = dyn_cast<GCCAsmStmt>(&S);
+      GS && GS->getNumOutputs() == 0 && GS->getNumInputs() == 0 &&
+      GS->getNumLabels() == 0 && StringRef(GS->getAsmString()).trim().empty()) {
+    for (unsigned I = 0, E = GS->getNumClobbers(); I != E; ++I)
+      if (GS->getClobber(I) == "memory") {
+        CGF.Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent,
+                                llvm::SyncScope::SingleThread);
+        break;
+      }
+    return;
+  }
+  CGF.CGM.getDiags().Report(S.getAsmLoc(), diag::err_msil_asm_unsupported)
+      << (isa<MSAsmStmt>(S) ? 1 : 0) << CGF.getTarget().getTriple().str();
+}
+
 void CodeGenFunction::EmitAsmStmt(const AsmStmt &S) {
+  if (getTarget().getTriple().isOSBinFormatMSIL())
+    return emitMSILAsmStmt(*this, S);
+
   // Pop all cleanup blocks at the end of the asm statement.
```

APIs verified in this checkout: `GCCAsmStmt::getAsmString()` returns `std::string`
(Stmt.h:3489). `AsmStmt::getClobber(unsigned)` returns `std::string` (Stmt.h:3381).
`GCCAsmStmt::getNumLabels()` is at Stmt.h:3608 and `AsmStmt::getAsmLoc()` at Stmt.h:3319.
`llvm::SyncScope::SingleThread` is already used at CGBuiltin.cpp:5510.

`clang/lib/CodeGen/CodeGenModule.cpp`, `case Decl::FileScopeAsm` (line 8198):

```diff
     auto *AD = cast<FileScopeAsmDecl>(D);
+    // MSIL: no module-level assembly (Design.md section 3). Never append any,
+    // because ModuleSymbolTable asserts an MC asm parser exists when module
+    // asm is present (see registry-tm.md).
+    if (getTriple().isOSBinFormatMSIL()) {
+      if (!StringRef(AD->getAsmString()).trim().empty())
+        getDiags().Report(AD->getAsmLoc(), diag::err_msil_asm_unsupported)
+            << 2 << getTriple().str();
+      break;
+    }
 
     const TargetOptions &TargetOpts = getTarget().getTargetOpts();
```

(`FileScopeAsmDecl::getAsmString()` returns `std::string` (Decl.h:4759), and
`getAsmLoc()` is at Decl.h:4748.)

`clang/lib/Parse/ParseStmtAsm.cpp:511-519`:

```diff
   const llvm::Triple &TheTriple = Actions.Context.getTargetInfo().getTriple();
   const llvm::Target *TheTarget = nullptr;
-  if (!TheTriple.isX86()) {
+  if (TheTriple.isOSBinFormatMSIL()) {
+    // No machine assembler for MSIL. Build an opaque MSAsmStmt; CodeGen
+    // reports it only if the enclosing function is emitted.
+  } else if (!TheTriple.isX86()) {
     Diag(AsmLoc, diag::err_msasm_unsupported_arch) << TheTriple.getArchName();
```

When `TheTarget` is null, the existing code returns `EmptyStmt()`, an `MSAsmStmt` with an
empty string (ParseStmtAsm.cpp:525-534). `emitMSILAsmStmt` reports every `MSAsmStmt`.

Not affected, and must keep working:
- Asm labels: `int f(void) __asm__("g");`. glibc's `__REDIRECT` uses them heavily. They
  are `AsmLabelAttr`, not asm statements, and produce `\01g`.
- `__has_extension(gnu_asm)` stays 1 because `GNUAsm` is untouched. `immintrin.h:516`
  therefore still defines the `_MSC_VER` HLE helpers, which are harmless unless used.

Also rejected by this design, because each contains an asm statement:
`__attribute__((naked))` bodies, which Sema requires to be asm-only, and `asm goto`.

Not covered, and rare (optional): global register variables
(`register long sp asm("rsp")` at file scope) lower to `llvm.read_register` /
`llvm.write_register`. The backend should reject those intrinsics.

Safety net (backend owner): bitcode from stock clang can still contain `call asm` and
`module asm` (Design.md 3, "stock-clang compatibility"). The MSIL AsmPrinter or ISel
must report inline asm through `LLVMContext::diagnose(DiagnosticInfoInlineAsm(...))`.
clang maps the `!srcloc` metadata it attaches back to a source location.

Later refinement (phase 3, optional): glibc x86 `bits/fenv.h` extern-inline functions
contain asm, and at `-O2` they are emitted `available_externally` when called. In
`CodeGenModule::shouldEmitFunction` (CodeGenModule.cpp:5060-5092), MSIL could return
false when the body contains an `AsmStmt`. The call would then bind to the real libc
symbol and become a P/Invoke instead of an error:

```cpp
  // After the NoInlineAttr check (line 5092):
  if (getTriple().isOSBinFormatMSIL() && containsAsmStmt(F->getBody()))
    return false; // use the out-of-line (native) definition instead
```

---

## 4. CodeGen

### 4.1 ABI selection

`createTargetCodeGenInfo` (CodeGenModule.cpp:120):
- `x86_64` (line 277): Win32 or UEFI gives `createWinX86_64TargetCodeGenInfo` (line 286).
  Anything else gives `createX86_64TargetCodeGenInfo` (SysV). For
  `x86_64-unknown-linux-gnu-msil` that is SysV. For `x86_64-pc-windows-msvc-msil` and
  `x86_64-w64-windows-gnu-msil` it is Win64. `WinX86_64ABIInfo` detects MinGW from the
  environment, not the format.
- `aarch64`: Windows gives `createWindowsAArch64TargetCodeGenInfo(..., Win64)` (line 157).
  Linux gives AAPCS. Darwin gives DarwinPCS when `getABI() == "darwinpcs"`.
- `x86` (i686 later): Win32 gives `createWinX86_32TargetCodeGenInfo`.
- Nothing in `clang/lib/CodeGen/Targets/X86.cpp` tests the object format. In
  `Targets/AArch64.cpp` the only test is line 658-659 (`aarch64_32 && MachO`).
- `shouldUseLLVMABILowering` and `getLLVMABITargetInfo` (CodeGenModule.cpp:340-450) run
  only under `-fexperimental-abi-lowering`. `Opts.IsMachO` (line 436) would be wrong for
  Darwin-msil there. That is not milestone-1 scope.
- Design.md 8 lists a later `clang/lib/CodeGen/Targets/MSIL.cpp` for attaching C
  signatures to external declarations. Because native ABI selection is arch-keyed and
  `TargetCodeGenInfo` has many virtuals, add that metadata from a `CodeGenModule` hook
  keyed on `isOSBinFormatMSIL()` (for example where declarations get attributes, in
  `SetFunctionAttributes`). Do not replace or wrap the native TargetCodeGenInfo.

### 4.2 Every object-format query in clang/lib/CodeGen, Sema, AST (plus Basic, Frontend, Parse)

`grep -rn "isOSBinFormat\|getObjectFormat()"` finds 84 hits in these directories. AST
has **none**.

| Location | Code | Effect for `*-msil` |
|---|---|---|
| CodeGen/BackendUtil.cpp:280-297 | `asanUseGlobalsGC` switch over `ObjectFormatType`, no default | **Add `case Triple::MSIL:`** next to Wasm/DXContainer/SPIRV, returning false. Otherwise `-Wswitch` warns. |
| CodeGen/CGBlocks.cpp:2880 | COFF: dllimport/dllexport for blocks runtime symbols | Only with `-fblocks`. Not taken. |
| CodeGen/CGCXX.cpp:163 | COFF: avoid weak destructor aliases | C++ only |
| CodeGen/CGDebugInfo.cpp:2944 | COFF: skip vtable debug info for imported vtables | C++ only |
| CodeGen/CGDeclCXX.cpp:640, 652-653 | ELF or MS-ABI: COMDAT key in llvm.used. ELF/Wasm: init function joins the comdat. | C++ only. On MSIL the init function gets no comdat. |
| CodeGen/CGObjC.cpp:2211; CGObjCGNU.cpp:183, 954, 1052, 1069, 1143, 1263, 1471, 1526, 1576, 1605, 1643, 1739, 2561; CGObjCMac.cpp:718, 1988, 1999, 3825, 3928, 3949, 5083, 5942, 6599, 7069, 7313, 7316, 7372, 7398, 7522, 7764, 7792, 7809, 7907, 7996, 8013, 8271, 8638, 8715, 8748, 8753 | Objective-C runtime sections and linkage | ObjC only. **CGObjCMac.cpp:5942-5963 is a switch with no default: add `case llvm::Triple::MSIL:` to the `report_fatal_error` group.** |
| CodeGen/CGVTables.cpp:1365 | non-COFF vtable handling | C++ only |
| CodeGen/CodeGenModule.cpp:209, 219 | PPC64 ELF ABI selection (with an assert) | ppc only. Not an MSIL arch. |
| CodeGen/CodeGenModule.cpp:436 | `Opts.IsMachO` for the experimental AArch64 LLVMABI | Only with `-fexperimental-abi-lowering` |
| CodeGen/CodeGenModule.cpp:849 | XCOFF: no alias to a common symbol | No |
| CodeGen/CodeGenModule.cpp:1700 | AArch64 ELF: `ptrauth-elf-got` / `ptrauth-init-fini*` module flags | Not emitted for MSIL (fine) |
| **CodeGen/CodeGenModule.cpp:1718-1722** | `if (getTriple().isOSLinux()) { ...; assert(getTriple().isOSBinFormatELF()); ... "aarch64-elf-pauthabi-platform"/"-version" flags }` | **Bug for `aarch64-*-linux-*-msil`: the assert fires in every compile with assertions enabled.** In release builds it emits ELF-only module flags. Fix: `if (getTriple().isOSLinux() && getTriple().isOSBinFormatELF())`. |
| CodeGen/CodeGenModule.cpp:1765 | ARM EABI build attributes | arm only |
| CodeGen/CodeGenModule.cpp:1909 | ELF: `direct-access-external-data` module setting | Not set (fine) |
| CodeGen/CodeGenModule.cpp:2353, 2361, 2365 | `shouldAssumeDSOLocal`: COFF treats everything as local, only ELF does the PIC/PIE analysis, others return false | No `dso_local` anywhere on MSIL. Harmless to the backend, which should use `isDeclaration()`. |
| CodeGen/CodeGenModule.cpp:3893 | `addUsedOrCompilerUsedGlobal`: ELF goes to `llvm.compiler.used`, others to `llvm.used` | `__attribute__((used))` lands in `llvm.used`. Backend must keep those globals. |
| CodeGen/CodeGenModule.cpp:3949 | `AddDependentLib`: ELF writes `llvm.dependent-libraries`, others go through `getDependentLibraryOption` into `llvm.linker.options` | `#pragma comment(lib, "x")` on Windows-msil becomes `!{"/DEFAULTLIB:x.lib"}`. Useful later for msil-ld. The milestone-1 backend must ignore `llvm.linker.options`. |
| CodeGen/CodeGenModule.cpp:4039 | Module link-library metadata (`IsELF`) | Clang modules only |
| CodeGen/CodeGenModule.cpp:7396, 7409, 7475, 7480 | CFString class linkage and string section | `__builtin___CFStringMakeConstantString` only |
| CodeGen/CodeGenModule.cpp:7509-7522 | CFString section switch: `UnknownObjectFormat` is unreachable, DX/GOFF/SPIRV/XCOFF are `llvm_unreachable("unimplemented")` | **Add `case llvm::Triple::MSIL:` to the unimplemented group**, and see SemaChecking below. |
| CodeGen/CoverageMappingGen.cpp:2518 | `getInstrProfSectionName(SK, ObjectFormat)` | Coverage only. The LLVM side must handle MSIL in `llvm/lib/ProfileData/InstrProf.cpp` **(unverified)**. |
| CodeGen/ItaniumCXXABI.cpp:2757-2758, 4588-4589 | ELF/Wasm comdat for guard variables and RTTI | C++ only |
| CodeGen/ObjectFilePCHContainerWriter.cpp:285, 312, 315 | PCH-in-object section names | `-gmodules` only. Uses the `__clangast` section name. |
| CodeGen/Targets/AArch64.cpp:658-659 | arm64_32 MachO vector legality | No |
| CodeGen/Targets/PPC.cpp:640 | PPC32 SVR4 | No |
| Sema/SemaChecking.cpp:2346-2357, used at 3213-3215 | `CheckBuiltinTargetNotInUnsupported(..., {GOFF, XCOFF})` for `__builtin___CFStringMakeConstantString` | **Add `llvm::Triple::MSIL`** so the builtin gets a Sema error instead of hitting the CodeGen `llvm_unreachable`. |
| Sema/SemaDeclAttr.cpp:4862 | COFF caps `aligned(N)` at 8192 | Windows-msil allows up to `Sema::MaximumAlignment`. Harmless. |
| Parse/ParsePragma.cpp:476, 604, 3273 | `#pragma comment` handler installed for MicrosoftExt, ELF, or AIX. ELF ignores every kind except `lib`. | Windows-msil (`-fms-extensions`): handled, all kinds. Linux-msil: no handler, so the pragma is unknown (`-Wunknown-pragmas`). Fine. |
| Frontend/InitPreprocessor.cpp:1526-1528 | `__ELF__` | Not defined (section 2.7) |
| Basic/TargetInfo.cpp:167 | MachO `UserLabelPrefix` | Section 2.7 |
| Basic/Targets/X86.h:177, 572 | COFF-only `MaxVectorAlign`/`MaxTLSAlign` and i686 `"_"` prefix | Section 2.7 |
| Basic/Targets.cpp:217, 634, 736, 765 | Dispatch | Bypassed by `AllocateMSILTarget` |
| include/clang/Basic/TargetInfo.h:1573, 1582 | `supportsIFunc` | Section 2.7, item 10 |
| include/clang/Basic/Attr.td:525-530 | `TargetELF`, `TargetELFOrMachO` specs (`ObjectFormats = [...]`) | Defined but not used by any attribute in this checkout. ClangAttrEmitter maps names to `llvm::Triple::<Name>`, so adding the enum needs nothing there. |

### 4.3 dllimport, comdats, TLS, sections, mangling, `__declspec`

All of these test the OS or environment, not the object format, so `-msil` keeps the
native behavior:
- **dllimport/dllexport**: `Triple::hasDLLImportExport()` is `isOSWindows() || isPS()`
  (Triple.h:1329). The attributes are `TargetSpecificAttr<TargetHasDLLImportExport>`
  (Attr.td:4574-4615). `shouldDLLImportComdatSymbols()` tests the environment. On
  `*-windows-*-msil`, SDK functions come out as `declare dllimport` (observed:
  `declare dllimport i32 @GetCurrentProcessId()`), which marks P/Invoke candidates
  (Design.md 2.3). On Linux-msil, `dllimport` is ignored with a warning. Data imports
  (`dllimport` globals) are the backend's problem: CIL cannot P/Invoke data.
- **Comdats**: `supportsCOMDAT()` is triple-level (section 1). **In C on
  `*-windows-msvc-msil`, comdats and MS-mangled names are routine.** String literals
  are mangled by the MS ABI (MicrosoftMangle.cpp:572-574 returns true), and
  CodeGenModule.cpp:7657-7661 plus 7623 make them `linkonce_odr ... comdat` named
  `??_C@_0...`. UCRT's `printf` is an inline wrapper emitted as `define linkonce_odr i32
  @printf(ptr, ...) comdat` that calls `__stdio_common_vfprintf(i64, ptr, ptr, ptr, ptr)`.
  `__local_stdio_printf_options` holds an `internal global` static. All of this was
  observed with stock clang, `--target=x86_64-pc-windows-msvc -O2`, on
  `#include <windows.h>` + `<stdio.h>`. Backend implications: handle `linkonce_odr`,
  `comdat any`, and names containing `?@$` (quote ILAsm identifiers). Design.md 4.7 also
  benefits: UCRT stdio already funnels through non-variadic `__stdio_common_v*` entry
  points that take a `char*` `va_list`.
- **TLS**: `TLSSupported` comes from the native class. `hasDefaultEmulatedTLS()` is false
  (section 1). `__thread`, `_Thread_local`, and `__declspec(thread)` become
  `thread_local` globals for the backend.
- **Section names**: `__attribute__((section))` is validated only on Darwin
  (SemaDeclAttr.cpp:3508-3511). C++ static-init functions get `.text.startup` on Linux
  (`getStaticInitSectionSpecifier`, OSTargets.h:438-440, used at CGDeclCXX.cpp:445).
  MS `#pragma section` / `__declspec(allocate)` set sections, and the common MSVC C
  "constructor" idiom (`.CRT$XCU`) will not run under MSIL. Document it. The backend must
  ignore or handle `section` on functions and globals.
- **Mangling**: AST (Mangle.cpp:90-127) tests `isWasm()` and
  `shouldUseMicrosoftCCforMangling()` (OS-keyed). Plain C names are unchanged. On Windows,
  `__vectorcall` becomes `foo@@N` and `__regcall` becomes `__regcall3__foo`; on 64-bit,
  `__stdcall` and `__fastcall` are ignored (CCCR_Ignore, X86.h:912-915).
- **`__declspec`**: the keyword is enabled by `-fms-extensions` or `-fdeclspec`, both of
  which key on the driver and OS. MinGW defines a `__declspec(a)` macro
  (Targets.cpp:82-105, OS-keyed).

### 4.4 What the backend will see from clang by default (FYI, for the backend agents)

Observed function attributes:
- `x86_64-unknown-linux-gnu` and `x86_64-pc-windows-msvc`: `"target-cpu"="x86-64"
  "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic"`.
- `aarch64-unknown-linux-gnu`: `"target-cpu"="generic"
  "target-features"="+fp-armv8,+neon,+v8a,-fmv" "frame-pointer"="non-leaf-no-reserve"`.

The MSIL subtarget must accept these CPU and feature names. Otherwise
`MCSubtargetInfo` prints "not a recognized processor/feature" warnings on every compile.
The default `-S` output is verbose asm (integrated-as default), so the MSIL
`MCAsmInfo::CommentString` must be `//`. Disable `.ident` and `.file`. Ignore
`uwtable`, `frame-pointer`, `stack-protector-buffer-size`, `min-legal-vector-width`,
`"stack-probe-size"` (Win64), `llvm.linker.options`, and the module flags `PIC
Level`/`PIE Level`/`uwtable`/`frame-pointer`. `-O2` passes `-vectorize-loops
-vectorize-slp`, so the MSIL TTI should report no vector registers for milestone 1, or
vectors will appear.

---

## 5. Driver

### 5.1 ToolChain selection (`Driver::getToolChain`, Driver.cpp:6158)

| `--target=` | Normalized triple (cc1 `-triple`) | ToolChain |
|---|---|---|
| `x86_64-unknown-linux-gnu-msil` | `x86_64-unknown-linux-gnu-msil` | `toolchains::Linux` (Driver.cpp:6195-6216; not Hexagon, Mips, PPC, VE, OHOS, WALI, or LFI) |
| `x86_64-unknown-linux-musl-msil` | same | `toolchains::Linux` |
| `x86_64-pc-windows-msvc-msil` | `x86_64-pc-windows-msvc<MSVT>-msil`. MSVT is the detected MSVC version: `19.51.36260` under vcvars, otherwise the default `19.33.0` (MSVC.cpp:969-1012). | `toolchains::MSVCToolChain` (Driver.cpp:6274-6282; `-fuse-ld=bfd` would pick CrossWindows) |
| `x86_64-w64-windows-gnu-msil` | same | `toolchains::MinGW` (Driver.cpp:6264-6266) |
| `aarch64-apple-darwin-msil` | (phase 4) | `toolchains::DarwinClang` (6170-6178). Note: `computeTargetTriple`'s `-arch` handling at Driver.cpp:655 is MachO-gated and is skipped. |

Observed evidence that the format suffix survives: stock clang
`-### --target=x86_64-pc-windows-msvc-elf` gives `"-triple"
"x86_64-pc-windows-msvc19.51.36260-elf"`.

### 5.2 cc1 arguments for `clang --target=<msil> -O2 -ffreestanding -S prog.c -o prog.il`

Observed for the native triples. The MSIL triple should differ only as marked.

Linux (`toolchains::Linux`):
```
"-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-O2" "-S" "-disable-free"
"-clear-ast-before-backend" "-disable-llvm-verifier" "-discard-value-names"
"-main-file-name" "prog.c" "-mrelocation-model" "pic" "-pic-level" "2" "-pic-is-pie"
"-mframe-pointer=none" "-fmath-errno" "-ffp-contract=on" "-fno-rounding-math"
"-mconstructor-aliases" "-ffreestanding" "-target-cpu" "x86-64" "-tune-cpu" "generic"
"-debugger-tuning=gdb" ... "-resource-dir" ... "-internal-isystem" "<res>/include"
"-internal-isystem" "/usr/local/include" "-internal-externc-isystem" "/include"
"-internal-externc-isystem" "/usr/include" ... "-fgnuc-version=4.2.1" ...
"-vectorize-loops" "-vectorize-slp" "-o" "prog.il" "-x" "c" "prog.c"
```
- The native command also has `"-faddrsig"`. **MSIL does not get it**: Clang.cpp:8573-8580
  requires ELF or COFF.
- No `-fhalf-no-semantic-interposition` (ELF-only, Clang.cpp:5999).
- No `-fdwarf2-cfi-asm` (ELF/MachO, Clang.cpp:8582-8588).
- PIC/PIE come from `Linux::isPIEDefault` (Linux.cpp:948-951, `CLANG_DEFAULT_PIE_ON_LINUX`
  defaults to ON, clang/CMakeLists.txt:299).
- `-mframe-pointer=none` at `-O2` and `all` at `-O0` (CommonArgs.cpp:131-144).
- Without `-ffreestanding`, `-funwind-tables=2` is added (Generic_GCC
  `getDefaultUnwindTableLevel`, Gnu.cpp:3086-3105).
- Header search: multiarch is computed from arch and environment only
  (`Linux::getMultiarchTriple`, Linux.cpp:79-84 gives `x86_64-linux-gnu`), so a glibc
  `--sysroot` works unchanged. Per-target runtime dirs use the full triple string
  (`ToolChain::getTargetSubDirPath`, ToolChain.cpp:1111-1123), e.g.
  `lib/clang/<v>/lib/x86_64-unknown-linux-gnu-msil/`, which keeps MSIL runtime libraries
  separate (phase 3).

Windows (`toolchains::MSVCToolChain`): `-triple x86_64-pc-windows-msvc19.33.0-msil` (or
the vcvars version), `-mrelocation-model pic -pic-level 2` (forced,
MSVC.cpp:601-613), `-mframe-pointer=none`, `-relaxed-aliasing`, `-fms-volatile`,
`-funwind-tables=2` (MSVC.cpp:586-599, when not freestanding), `-target-cpu x86-64
-tune-cpu generic`, `-fno-use-cxa-atexit -fms-extensions -fms-compatibility
-fms-compatibility-version=19.33`, `-fdelayed-template-parsing`, and the VC, UCRT, and
Windows SDK `-internal-isystem` paths (observed under vcvars). No `-faddrsig`. The
default debug format becomes DWARF instead of CodeView (`MSVC.h:78` keys on COFF).
`--dependent-lib=libcmt/oldnames` appears only with `-fms-runtime-lib=` in GCC-driver
mode (Clang.cpp:7691-7693) or in clang-cl mode.

### 5.3 Does anything choke on the new format?

I read every object-format query under `clang/lib/Driver` (Driver.cpp: 655, 1793, 1909,
2187, 4650, 6257-6259, 6370-6376; ToolChain.cpp: 1495, 1509; Clang.cpp: 620, 1967, 2590,
2630, 2861-2882, 4650, 4845, 5032, 5081, 5999, 6383, 6427, 6457, 6492-6494, 6582-6641,
8530, 8574-8592, 8703-8705, 9542; CommonArgs.cpp: 152, 175, 1003, 1017, 1351, 1405, 1472,
2117, 2537, 2831, 3059, 3214; MSVC.cpp: 579, 588; MSVC.h: 78; XRayArgs.cpp: 41). Each is
either MachO/ELF/COFF-specific tuning that is skipped for MSIL, or an
`err_drv_unsupported_opt_for_target` for an explicit flag (`-gsframe`,
`-fbasic-block-address-map`, `-ffat-lto-objects`, `-frecord-command-line`,
`-fsplit-machine-functions`, `-Wa,--crel`, `-fsymbol-partition=`). **None affects a plain
`-S` compile.** Other checks:
- Driver.cpp:1789-1800 (environment-version validation) works, given the
  `getEnvironmentVersionString` behavior in section 1.
- `MSVCToolChain::buildAssembler` (MSVC.cpp:578-583) reports `err_no_external_assembler`.
  That path runs only with `-fno-integrated-as -c`.
- Linking: `-c`, or a plain `clang foo.c`, collapses Compile+Backend+Assemble into one cc1
  job with `-emit-obj` (Clang.cpp:5499-5500). The MSIL backend cannot write objects, so
  today this ends in BackendUtil.cpp:1237 `err_fe_unable_to_interface_with_target`
  ("unable to interface with target machine"), which is unhelpful. Hence change 5.4.

### 5.4 Minimal milestone-1 driver change

`-S` needs nothing. For a clean `-c`/link error, add in
`clang/include/clang/Basic/DiagnosticDriverKinds.td`:

```
def err_drv_msil_no_object_output : Error<
  "emitting object files is not yet supported for MSIL target '%0'; use -S to "
  "produce ILAsm text, or -emit-llvm or -flto to produce bitcode">;
```

`clang/lib/Driver/ToolChains/Clang.cpp`, in `Clang::ConstructJob` (`Triple`, `TripleStr`,
and `D` are locals at lines 5212-5218):

```diff
   } else if (isa<AssembleJobAction>(JA)) {
+    if (Triple.isOSBinFormatMSIL())
+      D.Diag(diag::err_drv_msil_no_object_output) << TripleStr;
     CmdArgs.push_back("-emit-obj");
```

and at the top of `ClangAs::ConstructJob` (line 9261, after `TripleStr` and `D` are
declared at 9270-9272). This covers `-save-temps -c` and `.s` inputs:

```diff
   const auto &D = getToolChain().getDriver();
+  if (Triple.isOSBinFormatMSIL())
+    D.Diag(diag::err_drv_msil_no_object_output) << TripleStr;
```

Driver errors raised while building jobs stop execution, because
`Driver::ExecuteCompilation` returns 1 when `Diags.hasErrorOccurred()`
(Driver.cpp:2425-2427). `-###` prints and returns 1 (Driver.cpp:2420-2423), so
`not %clang -### -c` tests work. `-c -emit-llvm` and `-flto -c` produce `TY_LLVM_BC` /
`TY_LTO_BC` through the Compile/Backend branch (`-emit-llvm-bc`), not
`AssembleJobAction`, so they still work.

### 5.5 Should `IsIntegratedAssemblerDefault` change? No.

`Generic_GCC::IsIntegratedAssemblerDefault` returns true for x86_64 and aarch64
(Gnu.cpp:3128-3137). The base `ToolChain` returns true (ToolChain.h:485). If MSIL made it
false:
- `-c` would build a separate assemble job. On Linux that runs GNU `as` on IL text
  (`gnutools::Assembler`). MSVC gives `err_no_external_assembler`. Both are worse than
  the explicit diagnostic in 5.4.
- `-S` would become non-verbose (`-fno-verbose-asm`, Clang.cpp:6306-6309).

Keep the default and use the 5.4 diagnostic. In phase 2 the MSIL toolchain's
`buildAssembler()` should return nullptr with a diagnostic, like MSVC.cpp:578-583.

### 5.6 Sketch: `clang/lib/Driver/ToolChains/MSIL.{h,cpp}` (phase 2, not now)

Header search, sysroot, multiarch, MSVC/SDK detection, and the C++ stdlib must stay the
native toolchain's (Design.md 3 "headers come from that platform's sysroot"). Only the
tools and a few defaults change. The smallest design is a mixin over the native
toolchain. `Linux`, `MSVCToolChain`, `MinGW`, and `DarwinClang` are not `final`, and all
take `(const Driver&, const llvm::Triple&, const ArgList&)`.

```cpp
// MSIL.h
namespace clang::driver {
namespace tools::msil {
class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  explicit Linker(const ToolChain &TC) : Tool("msil::Linker", "msil-ld", TC) {}
  bool isLinkJob() const override { return true; }
  bool hasIntegratedCPP() const override { return false; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &Args,
                    const char *LinkingOutput) const override;
  // msil-ld -o out.{exe,dll} --triple=<T> -L<native lib dirs from
  // getFilePaths()> <objs/.bc/.a> -l... -lc (default) --runtime=MsilC.Runtime.dll
  // [-shared --export-class=...] [--reference=<managed .dll inputs>]
  // [-mdotnet=net48|net8.0 -> --profile=...]
};
} // namespace tools::msil

namespace toolchains {
template <class NativeTC>
class LLVM_LIBRARY_VISIBILITY MSILToolChain final : public NativeTC {
public:
  using NativeTC::NativeTC;
  bool IsIntegratedAssemblerDefault() const override { return true; }
  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &) const override { return false; }
  bool isPICDefaultForced() const override { return false; }
  ToolChain::UnwindTableLevel
  getDefaultUnwindTableLevel(const llvm::opt::ArgList &) const override {
    return ToolChain::UnwindTableLevel::None;
  }
  bool HasNativeLLVMSupport() const override { return true; } // .bc to msil-ld
  const char *getDefaultLinker() const override { return "msil-ld"; }
  void addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args,
                             Action::OffloadKind K) const override; // -mdotnet=
protected:
  Tool *buildLinker() const override { return new tools::msil::Linker(*this); }
  Tool *buildAssembler() const override; // diag: no external assembler for MSIL
};
} // namespace toolchains
} // namespace clang::driver
```

`Driver::getToolChain` (Driver.cpp:6162), first statement inside `if (!TC)`:

```cpp
if (Target.isOSBinFormatMSIL()) {
  if (Target.isOSLinux())
    TC = std::make_unique<toolchains::MSILToolChain<toolchains::Linux>>(*this, Target, Args);
  else if (Target.isWindowsGNUEnvironment())
    TC = std::make_unique<toolchains::MSILToolChain<toolchains::MinGW>>(*this, Target, Args);
  else if (Target.isWindowsMSVCEnvironment())
    TC = std::make_unique<toolchains::MSILToolChain<toolchains::MSVCToolChain>>(*this, Target, Args);
  // Darwin: phase 4
  else Diag(diag::err_drv_msil_unsupported_triple) << Target.str();
}
```

Also: `useFramePointerForTargetByDefault` (CommonArgs.cpp:85) should return false for
MSIL, as it does for wasm. An optional stop-gap before msil-ld exists: `ilasm` can
assemble several `.il` files into one assembly, so the MSIL toolchain could drive
`ilasm` as assembler plus linker. That needs module-unique names for `static` functions.
Not recommended for milestone 1.

---

## 6. Tests to add for milestone 1

The front-end tests need only the clang TargetInfo, not the MSIL backend: `-emit-llvm`
creates no TargetMachine (BackendUtil.cpp:1260-1267). They need **no `REQUIRES`**.
Anything that runs `-S` through the backend needs `REQUIRES: msil-registered-target`.
That feature comes from `llvm-config --targets-built` (clang/test/lit.cfg.py:402-414),
which includes experimental targets. `-dM` output is sorted (PrintPreprocessedOutput.cpp:1119),
so use `-DAG` or `-NOT` checks.

### 6.1 `clang/test/Preprocessor/init-msil.c`

```c
// Native-by-construction guard: -msil predefines must equal native, except
// for __MSIL__, long double, and __ELF__.
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "__SIZEOF_LONG_DOUBLE__" -e "#define __ELF__ " > %t.lnx.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "__SIZEOF_LONG_DOUBLE__" -e "#define __MSIL__ " > %t.lnx.msil
// RUN: diff %t.lnx.native %t.lnx.msil
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-pc-windows-msvc -fms-extensions < /dev/null \
// RUN:   > %t.win.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-pc-windows-msvc-msil -fms-extensions < /dev/null \
// RUN:   | grep -v "#define __MSIL__ " > %t.win.msil
// RUN: diff %t.win.native %t.win.msil
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-gnu < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "__SIZEOF_LONG_DOUBLE__" -e "#define __ELF__ " > %t.a64.native
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=aarch64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | grep -v -e "__LDBL_" -e "__SIZEOF_LONG_DOUBLE__" -e "#define __MSIL__ " > %t.a64.msil
// RUN: diff %t.a64.native %t.a64.msil

// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu-msil < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefixes=MSIL,LNX %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-pc-windows-msvc-msil -fms-extensions < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefixes=MSIL,WIN %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-linux-gnu < /dev/null \
// RUN:   | FileCheck -match-full-lines --check-prefix=NATIVE %s
// MSIL-DAG: #define __MSIL__ 1
// MSIL-DAG: #define __SIZEOF_LONG_DOUBLE__ 8
// MSIL-DAG: #define __LDBL_MANT_DIG__ 53
// MSIL-DAG: #define __x86_64__ 1
// LNX-DAG: #define __linux__ 1
// LNX-NOT: #define __ELF__ 1
// WIN-DAG: #define _WIN64 1
// WIN-DAG: #define _M_X64 100
// NATIVE-NOT: #define __MSIL__
```

(`grep` and `diff` in lit RUN lines are used widely in clang/test. `diff` is a lit
built-in.)

### 6.2 `clang/test/Driver/msil-toolchain.c`

```c
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=LNX %s
// LNX: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil"
// LNX-SAME: "-S"
// LNX-SAME: "-target-cpu" "x86-64"
// LNX-NOT: "-faddrsig"
// LNX-NOT: "-fdwarf2-cfi-asm"

// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN %s
// WIN: "-cc1" "-triple" "x86_64-pc-windows-msvc19.40.0-msil"
// WIN-SAME: "-S"

// RUN: %clang -### --target=aarch64-unknown-linux-gnu-msil -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=A64 %s
// A64: "-cc1" "-triple" "aarch64-unknown-linux-gnu-msil"

// Object files are not supported until phase 2.
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ %s
// OBJ: error: emitting object files is not yet supported for MSIL target 'x86_64-unknown-linux-gnu-msil'

// Bitcode output still works.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -c -emit-llvm %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BC %s
// BC: "-emit-llvm-bc"
// BC-NOT: error:
```

Check the `-SAME` ordering against the first real build. The observed native order is
`-cc1 -triple T -O2 -S ... -target-cpu`.

### 6.3 `clang/test/CodeGen/MSIL/long-double.c`

```c
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes=CHECK,SYSV --implicit-check-not=x86_fp80 %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes=CHECK,WIN --implicit-check-not=x86_fp80 %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes=CHECK,A64 --implicit-check-not=fp128 %s
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -mlong-double-80 -emit-llvm-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD80 %s
// RUN: not %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -mlong-double-128 -emit-llvm-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD128 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -mlong-double-64 -emit-llvm-only %s
// BAD80: error: option '-mlong-double-80' cannot be specified on this target
// BAD128: error: option '-mlong-double-128' cannot be specified on this target

_Static_assert(sizeof(long double) == 8, "");
_Static_assert(_Alignof(long double) == 8, "");

long double g = 1.0L;
// CHECK: @g = {{.*}}global double 1.000000e+00, align 8

long double add(long double a, long double b) { return a + b; }
// SYSV: define{{.*}} double @add(double noundef %a, double noundef %b)
// A64:  define{{.*}} double @add(double noundef %a, double noundef %b)
// CHECK: fadd double

_Complex long double cadd(_Complex long double a, _Complex long double b) { return a + b; }
// SYSV: define{{.*}} { double, double } @cadd(double noundef {{.*}}, double noundef {{.*}}, double noundef {{.*}}, double noundef {{.*}})

long double va(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  long double r = __builtin_va_arg(ap, long double);
  __builtin_va_end(ap);
  return r;
}
// SYSV-LABEL: define{{.*}} double @va(
// SYSV: %fp_offset = load i32
// SYSV: icmp ule i32 %fp_offset, 160
// A64-LABEL: define{{.*}} double @va(
// A64: %vr_offs = load i32

// CHECK: !{i32 1, !"long-double-type", !"double"}
```

(MinGW is used for `WIN` because its native long double is x87. That proves the wrapper
also overrides a Windows class.)

### 6.4 `clang/test/CodeGen/MSIL/inline-asm.c`

```c
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm-only -verify %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fsyntax-only -verify=syntax %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - %s -DNO_ERRORS \
// RUN:   | FileCheck %s
// syntax-no-diagnostics

// Not emitted, so not diagnosed (the intrin.h pattern).
static inline int unused_helper(int x) { __asm__("bswap %0" : "+r"(x)); return x; }

// Asm labels are not inline asm.
int renamed(void) __asm__("real_name");
int call_renamed(void) { return renamed(); }
// CHECK: declare{{.*}} @real_name()

// The empty compiler-barrier idiom is accepted.
void barrier(void) { __asm__ __volatile__("" ::: "memory"); }
// CHECK-LABEL: define{{.*}} void @barrier()
// CHECK: fence syncscope("singlethread") seq_cst

#ifndef NO_ERRORS
void f(void) {
  __asm__ volatile("nop"); // expected-error {{inline assembly is not supported for the MSIL target 'x86_64-unknown-linux-gnu-msil'}}
}
int g(int x) {
  __asm__("" : "+r"(x)); // expected-error {{inline assembly is not supported for the MSIL target}}
  return x;
}
__asm__(".globl foo"); // expected-error {{file-scope assembly is not supported for the MSIL target}}
#endif
```

`clang/test/CodeGen/MSIL/ms-inline-asm.c`:

```c
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -fasm-blocks -emit-llvm-only -verify %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -fasm-blocks -fsyntax-only -verify=syntax %s
// syntax-no-diagnostics
void f(void) {
  __asm { nop } // expected-error {{Microsoft-style inline assembly is not supported for the MSIL target}}
}
```

`clang/test/CodeGen/MSIL/system-headers.c` (guards the decision in section 3.1):

```c
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -fms-compatibility \
// RUN:   -fms-compatibility-version=19.40 -ffreestanding -emit-llvm-only -Werror %s
#include <intrin.h>
#include <immintrin.h>
int main(void) { return 0; }
```

### 6.5 `clang/test/CodeGen/MSIL/triples.c` (unsupported pairs, the aarch64 assert, ifunc)

```c
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o - %s | FileCheck --check-prefix=A64 %s
// A64-NOT: aarch64-elf-pauthabi
// RUN: not %clang_cc1 -triple powerpc64le-unknown-linux-gnu-msil -fsyntax-only %s 2>&1 | FileCheck --check-prefix=BAD %s
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-android-msil -fsyntax-only %s 2>&1 | FileCheck --check-prefix=BAD %s
// BAD: error: unknown target triple
int x;
```

### 6.6 End to end, owned by the backend agents

`clang/test/CodeGen/MSIL/fib-S.c` with `// REQUIRES: msil-registered-target`, running
`%clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S %s -o - | FileCheck %s`.
The run lit suite with `ilasm` and `dotnet` belongs to the milestone-1 harness, not
clang/test.

---

## 7. Cross-team notes (backend, LLVM triple, and runtime agents)

1. **`lookupTarget` must not fall back to the native backend** for `*-msil` (section 1).
2. **Subtarget strings:** accept `x86-64`, `generic`, and
   `+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87` (x86_64). Accept `generic` and
   `+fp-armv8,+neon,+v8a,-fmv` (aarch64). Do this silently: no "not a recognized
   processor/feature" warnings (section 4.4).
3. **Asm printer:** `-S` is verbose by default. Comments must use `//`, and there must
   be no `.ident`, `.file`, or CFI.
4. **Inline asm safety net** for stock-clang bitcode (section 3.2).
5. **Ignore in milestone 1:** `llvm.linker.options`, `uwtable`, `frame-pointer`,
   `stack-probe-size`, `section` on C++ init functions, and the PIC/PIE module flags.
6. **Windows-msvc C emits `linkonce_odr` plus `comdat any`** for string literals and UCRT
   inline functions (printf). Names contain `?@$`. External SDK functions are
   `declare dllimport`.
7. **`Triple::getDefaultLongDoubleFormat()`** returns IEEEdouble for MSIL (section 1).
8. **Vectorization:** at `-O2` the driver enables both vectorizers. The milestone-1 MSIL
   TTI should report zero vector registers.
9. **The `"long-double-type"="double"` module flag** conflicts with stock x86_64-linux
   bitcode that uses long double (section 2.4). Expected; msil-ld should report it
   clearly.

---

## 8. Open questions and risks

Open questions for the lead:
1. Should `__ELF__` stay undefined on `*-linux-*-msil`? (Recommended: yes, see 2.7.)
2. Should the empty `asm volatile("" ::: "memory")` barrier be accepted as a
   single-thread fence? (Recommended: yes. It is common in portable C. Everything else
   is an error.)
3. `__float128`, `_Float16`, `__bf16`: keep native and let the backend reject them,
   or disable in the wrapper (section 2.8)?
4. Restrict calling conventions in the wrapper (vectorcall, regcall, ms_abi, ...)?
   (Section 2.8.)
5. Darwin-msil is in `AllocateMSILTarget` for symmetry, but the MachO-keyed paths are
   untested: `UserLabelPrefix`, the driver's `-arch`, and DarwinClang's SDK and
   deployment-target handling. Gate it off until phase 4?

Risks:
- **Dispatch drift:** `AllocateMSILTarget` mirrors part of `AllocateTarget`. Mitigated by
  the predefine-diff test (6.1). Rebase conflicts are unlikely because the new code is
  separate from the native switch.
- **aarch64-linux-msil assert** (CodeGenModule.cpp:1722) until item 7 lands.
- **`max_align_t` alignment** changes on Linux-msil (16 to 8): an ABI difference from
  native.
- **glibc extern-inline asm at `-O2`** (bits/fenv.h): a CodeGen error if called, until
  the `shouldEmitFunction` refinement (3.2). **(unverified against a real sysroot)**
- **Native constructors keyed on COFF/MachO** (2.7) cause small differences on Windows
  i686 and Darwin.
- **`-Wswitch` fallout** from the new enum value: clang has 3 sites (4.2). The LLVM sites
  are listed in registry-tm.md 1.5.

---

## 9. Experiments run (scratch: `%TEMP%\claude\...\scratchpad\recon\clang\`)

All used stock clang 22.1.3 (VS 2026 bundled) with native triples as stand-ins.

| Experiment | Command (abridged) | Result |
|---|---|---|
| SysV ABI with double long double | `clang -cc1 -triple x86_64-unknown-linux-gnu -mlong-double-64 -O1 -emit-llvm ld.c` | `double` parameters and return, `{double,double}` complex in SSE, `va_arg` through `fp_offset` (`icmp ult ... 161` at -O1, `icmp ule ... 160` at -O0), sizeof/alignof 8/8, no `x86_fp80` |
| AAPCS64 with double long double | `-triple aarch64-unknown-linux-gnu -mlong-double-64` | `double` parameters, HFA `[2 x double]`, `va_arg` through `vr_offs` |
| Predefine delta | `-E -dM` with and without `-mlong-double-64` for x86_64-linux and aarch64-linux | Only the 18 `__LDBL_*` macros and `__SIZEOF_LONG_DOUBLE__` differ. `-gnu-elf` vs plain: identical. |
| Parse-time rejection breaks headers | `--target=x86_64-pc-windows-msvc -fno-gnu-inline-asm -fsyntax-only` on `#include <intrin.h>` + `<immintrin.h>` | 18 errors in clang's intrin.h. The normal compile has 0 `asm` in the IR. |
| windows.h | under vcvars: `-fno-gnu-inline-asm -fsyntax-only` on `<windows.h>` + `<stdio.h>` | 0 errors (the x64 SDK has no inline asm). `-O2 -emit-llvm`: 0 inline asm, 6 comdat lines, `??_C@...` string literal, `linkonce_odr @printf ... comdat` calling `__stdio_common_vfprintf`, `declare dllimport @GetCurrentProcessId` |
| Driver cc1 lines | `-### --target=x86_64-unknown-linux-gnu -O2 -ffreestanding -S`; `-### --target=x86_64-pc-windows-msvc ...` | As in section 5.2 |
| Format suffix kept by the MSVC toolchain | `-### --target=x86_64-pc-windows-msvc-elf -S` (under vcvars) | `-triple x86_64-pc-windows-msvc19.51.36260-elf`, plus ELF-only `-fhalf-no-semantic-interposition`/`-fdwarf2-cfi-asm`, which confirms those flags key on the format |
| Default function attributes | `-S -emit-llvm` for x86_64-linux, aarch64-linux, x86_64-msvc | As in section 4.4 |
