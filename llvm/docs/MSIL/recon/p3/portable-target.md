# P-0 recon "target": triple, clang target, driver and backend for P-1a

Read-only audit and design for P-1a (`msil64-unknown-dotnet` toolchain, no libc; `msil32-unknown-dotnet` parsed and
refused until P-4). Tree: the repository, branch `msil`, HEAD `e38f9a199bc9` ("Plan: M25 records, the portable program
order and CI defects"). That commit is doc/test-only on top of M25 `c3460446ac09`, so every source citation below is M25
source. Scratch: `<scratch>/p0/target/` (the earlier cut-off attempt left `.ninja_deps`/`.ninja_log` copies,
`deps.py`, `union.py`, `enclosing.py` and `sites.tsv`; all were re-checked here, `sites.tsv` was found incomplete by 9
lines and its enclosing-function heuristic mislabels one function, both corrected below).

Binding user decisions applied throughout (not reopened): option-A order; two triples, msil64 first, msil32 parsed but
refused until P-4, each refuses a process of the other width; `wchar_t` = 32-bit signed int on both; managed function
pointers on portable (M28 thunks off); `-static` a loud driver error until P-3; a dotnet-specific link-rule family that
is NOT ELF and no `__ELF__`; compiler-rt `int_lib.h` and llvm-libc get a portable branch; no interim OS layer; no
runtime support assembly; defaults run on .NET Framework 4.8 and modern .NET; plan contracts cite files and functions.

---

## 1. Triple

### 1.1 How MSIL is modeled today (findings)

- **Object format, not architecture.** `Triple::ObjectFormatType` has `MSIL` (`llvm/include/llvm/TargetParser/Triple.h`,
  enum `ObjectFormatType`); `parseFormat` (`llvm/lib/TargetParser/Triple.cpp`, static `parseFormat`) matches a trailing
  `"msil"` on the environment component (`.EndsWith("msil", Triple::MSIL)`), so `x86_64-unknown-linux-gnu-msil` is arch
  x86_64, OS linux, env `gnu-msil`, format MSIL. `Triple::normalize` picks up a 5th `msil` component
  (`x86_64-windows-msvc-msil`). `isOSBinFormatMSIL()` (Triple.h) is the only predicate.
- **`Triple::getNativeFormatTriple`** (Triple.cpp, directly after `getDefaultObjectFormat`) strips `-msil`/`msil` from the
  environment and rebuilds the triple; its object format becomes `getDefaultFormat` of the native arch/OS. On a triple
  whose arch/OS default format is itself MSIL it would return a triple that is still MSIL.
- **Consumers of the native triple** (all format-keyed, so arch `msil64` + format MSIL reaches every one of them):
  - `Triple::computeDataLayout` (`llvm/lib/TargetParser/TargetDataLayout.cpp`): `if (isOSBinFormatMSIL()) return
    getNativeFormatTriple().computeDataLayout(ABIName);` -> **infinite recursion** for a portable triple whose default
    format is MSIL, unless the arch switch is consulted first. This is the one real recursion hazard.
  - `Triple::isCompatibleWith` (Triple.cpp): already guarded ("A malformed triple whose native form is still MSIL ...
    takes the plain comparison below instead of recursing"), so portable vs host compares arches and is incompatible,
    which is the wanted answer.
  - `clang/lib/Basic/Targets/MSIL.h` `MSILTargetInfo<Native>` constructor (native COFF limits) and `getTargetDefines`
    (`__ELF__` when the native format is ELF); `clang/lib/Sema/SemaDeclAttr.cpp` aligned-attribute limit (native COFF ->
    8192). For portable, `getNativeFormatTriple()` returning the triple itself gives "not COFF, not ELF", i.e. no
    `__ELF__` and the generic alignment limit; both correct.
  - `TargetRegistry::lookupTarget(const Triple&, std::string&)` (`llvm/lib/MC/TargetRegistry.cpp`): any MSIL-format triple
    -> `findTargetByName("msil")`. `lookupTarget(StringRef ArchName, Triple&, ...)`: `-march=msil` switches the format to
    MSIL; another `-march` on an MSIL triple is an error.
  - `TargetMachine::shouldAssumeDSOLocal` (`llvm/lib/Target/TargetMachine.cpp`), `irsymtab::build` linker options
    (`llvm/lib/Object/IRSymtab.cpp`), `Triple::getDefaultExceptionHandling` and `Triple::getDefaultLongDoubleFormat`
    (Triple.cpp): format-keyed, correct for portable as they are.
- **Target registration.** `LLVMInitializeMSILTargetInfo` (`llvm/lib/Target/MSIL/TargetInfo/MSILTargetInfo.cpp`) registers
  `"msil"` with `ArchMatchFn = [](Triple::ArchType) { return false; }`: the target is only ever found through the format
  branch of `lookupTarget`.
- **DataLayout source of truth.** clang's `TargetInfo::resetDataLayout()` (`clang/lib/Basic/TargetInfo.cpp`) is
  `Triple.computeDataLayout(getABI())`; the backend's `CodeGenTargetMachineImpl` derives it from the same triple. One
  edit in `TargetDataLayout.cpp` feeds both.
- **OS names live in `TripleName.def`.** `parseOS` uses `TRIPLE_OS(Enum, Name, CMakeName)` rows with prefix matching
  (`llvm/include/llvm/TargetParser/TripleName.def`); the `OSType` enum itself is still hand-written in Triple.h. No existing
  OS name is a prefix of `dotnet` or vice versa (darwin, dragonfly, driverkit), so a row anywhere is safe.
- **Runtime libcalls are keyed on arch.** `llvm/include/llvm/IR/RuntimeLibcalls.td` selects a `SystemRuntimeLibrary` per
  arch predicate; the generated `RTLIB::RuntimeLibcallsInfo` system-library lookup in
  `build/include/llvm/IR/RuntimeLibcalls.inc` ends with `return {};` for an arch no predicate names. Today MSIL triples
  inherit x86/AArch64's set through their native arch. **A new arch msil32/msil64 gets no libcalls at all** (memcpy,
  memset, `__divti3`, ldexp, ...), so `RuntimeLibcalls.td` must gain an MSIL predicate and system library. Plan 5.13.5
  currently says "No Phase 3 milestone edits ... `RuntimeLibcalls.td`" (needs an amendment row).
- **Current behavior (measured, `<scratch>/p0/target/`, tools read-only):**
  - `clang --target=msil64-unknown-dotnet -c t.c` -> `error: unknown target triple 'msil64-unknown-dotnet'`.
  - `llc -mtriple=msil64-unknown-dotnet t.ll` -> `unable to get target for 'msil64-unknown-dotnet'` (arch and OS unknown,
    default format ELF).
  - `llc -mtriple=msil64-unknown-dotnet-msil t.ll` -> reaches `MSILPrepareIR`'s gate: `unsupported target triple
    'msil64-unknown-dotnet-msil' for MSIL (Phase 1 supports x86_64 and aarch64 with 64-bit pointers)`. The format-suffix
    path already builds a TargetMachine for an unknown arch; only the arch tables are missing.
  - `clang -print-target-triple --target=msil64-dotnet` -> `msil64-dotnet` (no component recognized, no normalization).

### 1.2 Proposal

Arch `msil32`, `msil64`; OS `dotnet`; vendor ignored (`unknown` canonical); no environment. `msil64-unknown-dotnet` gets
format MSIL by default.

| Item | Change |
|---|---|
| `Triple.h` `enum ArchType` | append `msil32` (ILP32 CIL), `msil64` (LP64 CIL) after `ve`; `LastArchType = msil64` |
| `Triple.h` `enum OSType` | append `DotNet` after `H2`; `LastOSType = DotNet` |
| `Triple.h` predicates | `bool isMSIL() const` (arch msil32 or msil64), `bool isOSDotNet() const`; doc of `getNativeFormatTriple`: "a portable MSIL triple (arch msil32/msil64) has no native platform and is returned unchanged" |
| `TripleName.def` | `TRIPLE_OS(DotNet, "dotnet", "Generic")` |
| `Triple.cpp` `Triple::parseArch` | `.Case("msil32", Triple::msil32)`, `.Case("msil64", Triple::msil64)` |
| `Triple.cpp` `Triple::getArchTypeName` / `getArchTypePrefix` | `"msil32"`/`"msil64"`; prefix `"msil"` (reserved for future `__builtin_msil_*`/inline-IL intrinsics) |
| `Triple.cpp` `getDefaultFormat` | `case msil32: case msil64: return Triple::MSIL;` (no OS test; an explicit `-elf`/`-coff` suffix still parses and is refused in `lookupTarget`) |
| `Triple.cpp` `getArchPointerBitWidth` | 32 / 64 (drives `isArch64Bit`, `MSILMCAsmInfo::CodePointerSize`, msil-ld `setDataModel` callers) |
| `Triple.cpp` `get32BitArchVariant` / `get64BitArchVariant` | msil64 <-> msil32 (so `-m32` on msil64 yields msil32, refused loudly until P-4) |
| `Triple.cpp` `getBigEndianArchVariant` / `getLittleEndianArchVariant` / `isLittleEndian` | none / self / true (clang's `TargetInfo` sets `BigEndian = !T.isLittleEndian()`) |
| `Triple.cpp` `getNativeFormatTriple` | `if (isMSIL()) return *this;` before stripping (also covers the redundant spelling `msil64-unknown-dotnet-msil`) |
| `Triple.cpp` exhaustive arch switches without `default:` | `getDefaultExceptionHandling`, `getDefaultLongDoubleFormat` (both already return early for MSIL format but need the case labels for `-Wswitch`) |
| `TargetDataLayout.cpp` `Triple::computeDataLayout` | guard becomes `if (isOSBinFormatMSIL() && !isMSIL())`; switch gains `case msil32: case msil64: return computeMSILDataLayout(*this);` |
| `TargetRegistry.cpp` `lookupTarget(const Triple&, ...)` | before the format branch: `if (TT.isMSIL() && !TT.isOSBinFormatMSIL())` -> error "the msil64 architecture only produces MSIL; drop the '-elf' object format" (no recursion: no native triple is consulted) |
| `MSILTargetInfo.cpp` | unchanged (`ArchMatchFn` stays false: matching msil arches there would hand an ELF-format msil triple to the MSIL target) |
| `RuntimeLibcalls.td` | `def IsMSIL : LibcallPredicate<[{TT.isMSIL()}]>;` and `MSILSystemLibrary : SystemRuntimeLibrary<isMSIL, ["compiler-rt", "libatomic", "libm", "libc"]>` with i128 helpers on msil64 only and no f80/f128 names (long double is IEEE double); `libatomic` stays listed so oversized atomics become `__atomic_*` calls that stay undefined (loud) on portable until P-3 |
| `ExpandVariadics.cpp` `VariadicABIInfo::create` | after M26's MSIL classes: `case msil32: case msil64:` (or the M26 MSIL pre-switch hook keyed on `T.isMSIL()`) -> `MSILPortable` char* class (section 2.3) |

Proposed data layouts (both little-endian, ELF-style mangling, which is what the Linux MSIL triples already use and what
the backend's `getILName` handles):

- msil64: `e-m:e-p:64:64-i64:64-i128:128-n8:16:32:64-S128`
- msil32: `e-m:e-p:32:32-i64:64-i128:128-n8:16:32:64-S128`

Rationale: CIL's `int64` is a stack type on both widths, so `i64:64` and `n...:64` are identical (struct layout of every
type except pointers and `long` is width-independent, which simplifies P-4 and the shared-state ABI of contract 5.19);
`i128:128` keeps compiler-rt's TI helpers consistent; no `f80`; `S128` matches the shadow-stack alignment and
`SuitableAlign = 128` below.

No recursion anywhere: `getDefaultFormat` decides MSIL from the arch alone; `getNativeFormatTriple` short-circuits;
`computeDataLayout` consults the arch switch before the native redirect; `isCompatibleWith` already stops at "native
still MSIL"; `lookupTarget` never asks for a native triple.

### 1.3 Every upstream file P-1a touches (target/clang/driver/backend scope)

LLVM: `llvm/include/llvm/TargetParser/Triple.h`, `llvm/include/llvm/TargetParser/TripleName.def` (new hook),
`llvm/lib/TargetParser/Triple.cpp`, `llvm/lib/TargetParser/TargetDataLayout.cpp`, `llvm/lib/MC/TargetRegistry.cpp`,
`llvm/include/llvm/IR/RuntimeLibcalls.td` (new hook; plan amendment), `llvm/lib/Transforms/IPO/ExpandVariadics.cpp`
(M26's hook block, P-1a adds a row), `llvm/unittests/TargetParser/TripleTest.cpp`, `llvm/unittests/MC/TargetRegistry.cpp`.

clang: `clang/lib/Basic/Targets.cpp` (`AllocateTarget`/`AllocateMSILTarget`), `clang/lib/CodeGen/CodeGenModule.cpp`
(`createTargetCodeGenInfo`), `clang/lib/CodeGen/TargetInfo.h` (one `createMSILPortableTargetCodeGenInfo` declaration) and
`clang/lib/CodeGen/CMakeLists.txt` (new `Targets/MSIL.cpp`, fork-only line) -- or, to avoid both, a file-static ABIInfo in
CodeGenModule.cpp (worse for MC-1/C++); `clang/include/clang/Basic/DiagnosticDriverKinds.td` (optional: a dedicated
portable `-static` message), new tests `clang/test/Preprocessor/init-msil-portable.c`, `clang/test/Driver/msil-dotnet.c`
(fork-only lines). Already fork files (no new hook): `clang/lib/Basic/Targets/MSIL.h`,
`clang/lib/Driver/ToolChains/MSIL.{h,cpp}`.

compiler-rt (decided by the user, plan amendment needed since 5.13.5 says "compiler-rt is not edited in Phase 3"):
`compiler-rt/lib/builtins/int_lib.h`: add `|| defined(__dotnet__)` to the first `COMPILER_RT_ALIAS` branch (the
`__attribute__((alias))` one, which the Linux MSIL triples already use; keying on `__MSIL__` would also capture the
Windows MSIL triples, which must keep the `_WIN32` branch).

Not touched (verified): `clang/include/clang/Basic/TargetInfo.h` (309 objects), `clang/include/clang/Driver/ToolChain.h`
(76), `llvm/include/llvm/MC/TargetRegistry.h` (80), `clang/lib/Driver/Driver.cpp` (its `getToolChain` already tries
`createMSILToolChain` first for any MSIL-format triple; `getDefaultImageName` already gives `a.exe`),
`clang/lib/Sema/SemaDeclAttr.cpp`, `llvm/lib/Object/IRSymtab.cpp`, `llvm/lib/Target/TargetMachine.cpp`,
`clang/lib/Parse/ParsePragma.cpp` (`#pragma comment(lib)` is accepted for any MSIL-format triple).

Exhaustive `case Triple::ve:` switches elsewhere that have `default:` and need nothing: `clang/lib/Basic/Targets.cpp`
`AllocateTarget`, `clang/lib/Driver/Driver.cpp` `Driver::getToolChain`, `clang/lib/Driver/ToolChains/CommonArgs.cpp`
`getLDMOption`/`getTargetFeatures`, `Gnu.cpp` `Assembler::ConstructJob`, `Linux.cpp` `getDynamicLinker`,
`clang/lib/Driver/ToolChains/Clang.cpp` `Clang::RenderTargetOptions`; `CodeGenModule.cpp` `createTargetCodeGenInfo` falls
to `DefaultTargetCodeGenInfo`, which P-1a replaces with an explicit case.

Audit item (likely no change, add a test): `llvm/lib/Analysis/TargetLibraryInfo.cpp` `initializeLibCalls` treats an
unknown OS as "everything available"; llvm-libc's freestanding set is narrower (no `puts`/`printf` before P-2), so a
`printf("x\n")`->`puts` fold produces a link error only where printf itself cannot link either. Revisit at P-2.

---

## 2. clang target

### 2.1 Today

`AllocateTarget` (`clang/lib/Basic/Targets.cpp`) sends every MSIL-format triple to `AllocateMSILTarget`, which wraps a
native TargetInfo (`LinuxTargetInfo<X86_64TargetInfo>`, `MicrosoftX86_64TargetInfo`, `MinGWX86_64TargetInfo` and the
AArch64 trio) in `MSILTargetInfo<Native>` (`clang/lib/Basic/Targets/MSIL.h`): long double -> double, inline atomics capped
at 64 bits, `__MSIL__` (+ `__ELF__` on ELF natives), no `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16`, global register variables
rejected, `-mlong-double-80/128` diagnosed. Measured host leakage (`clang --target=x86_64-unknown-linux-gnu-msil -dM -E`,
saved as `<scratch>/p0/target/host-linux-msil-macros.txt`): `__linux__ __gnu_linux__ __unix__ linux unix
__ELF__ __x86_64__ __amd64__ __k8__ __SSE__ __SSE2__ __MMX__ __SEG_FS __SEG_GS __code_model_small__`; the Windows MSIL
triple adds `_WIN32 _WIN64 _MSC_VER _M_X64 _M_AMD64` and a 16-bit unsigned `wint_t`. The libc probe
(`<scratch>/p0/libc/flags.rsp`) emulated portable by `-U`-ing exactly these and adding `-D__dotnet__=1
-D__msil64__=1`.

### 2.2 Standalone `MSILPortableTargetInfo` (in the fork header `clang/lib/Basic/Targets/MSIL.h`, header-only like today, so
no `clang/lib/Basic/CMakeLists.txt` edit)

`AllocateMSILTarget` gains, before its native dispatch: `if (Triple.isMSIL()) return Triple.isOSDotNet() ?
std::make_unique<MSILPortableTargetInfo>(Triple, Opts) : nullptr;` (so `msil64-unknown-linux` or `msil64-unknown-unknown`
is "unknown target triple"; `x86_64-unknown-dotnet` without `-msil` reaches the native dispatch and is unknown too).
Derives from `TargetInfo` directly (model: `WebAssemblyTargetInfo`, `clang/lib/Basic/Targets/WebAssembly.h`).

| Property | msil64 | msil32 | Note |
|---|---|---|---|
| data model | LP64: `PointerWidth/Align = LongWidth/Align = 64` | ILP32 (TargetInfo defaults) | user decision |
| `size_t`/`ptrdiff_t`/`intptr_t` | `unsigned long`/`long`/`long` | same spelling (`unsigned long`, 32-bit) | wasm's choice: identical C++ mangling across widths (Qt goal); question Q2 |
| `int64_t`/`intmax_t` | `long long` (TargetInfo default) | `long long` | same reason; Linux-style `long` would differ between widths |
| plain `char` | signed (default) | signed | `-funsigned-char` stays a user option |
| `wchar_t` / `wint_t` | `int` (SignedInt, default) / `int` | same | user decision; `adjust()` reports `-fshort-wchar` (`Opts.WCharSize == 2`) as `err_opt_not_valid_on_target`, as `-mlong-double-*` is today, because `c.dll`'s wide functions assume 4 |
| `char16_t`/`char32_t` | defaults | defaults | |
| long double | `LongDoubleWidth = LongDoubleAlign = 64`, `IEEEdouble` | same | reuse `useDoubleForLongDouble()`; keep the `-mlong-double-*` diagnostic in `adjust()` |
| `__float128`, `_Float16`, `__bf16` | no `__float128` (`HasFloat128 = false`); `HasFloat16`/`HasBFloat16` true (M25 converts through clang_rt.builtins) | same | |
| va_list | `CharPtrBuiltinVaList` | same | backend: `getNativeVAListStructSize` returns 0 -> pointer va_list, `VACOPY` Expand |
| `MaxAtomicInlineWidth` | 64 | 64 | CLR `Interlocked` has 4/8-byte CAS in both process widths |
| `MaxAtomicPromoteWidth` | 64 | 64 | frozen ABI item (alignment of `_Atomic` 16-byte types); host triples keep native 128 |
| `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1/2/4/8` | defined in `getTargetDefines` | same | matches the host MSIL triples after M25's cap; never `_16` |
| `__int128` | yes (`hasInt128Type()` default: pointer width >= 64) | no | user decision, falls out of the default rule |
| `_BitInt` | `hasBitIntType() = true`, max 128 | same | DefaultABIInfo passes > 128 indirectly |
| `SuitableAlign` (`__BIGGEST_ALIGNMENT__`) | 128 | 128 | malloc alignment of llvm-libc must follow; frozen ABI |
| `UserLabelPrefix` | `""` (default for non-MachO) | same | compiler-rt `SYMBOL_NAME` |
| C++ ABI | `GenericItanium` (default) | same | for the later C++/Qt goal |
| `HasUnalignedAccess` | true | true | CIL `unaligned.` prefix exists |
| CPU / features | only `generic`; `isValidCPUName`, `setCPU`; no features (M33 adds `-mmsil-*` switches) | same | |
| inline asm | `getGCCRegNames` empty, `validateAsmConstraint` false; CodeGen keeps `err_msil_asm_unsupported` (format-keyed) | same | |
| global register variables | rejected (as `MSILTargetInfo`) | same | |
| calling conventions | `checkCallingConvention`: only `CC_C` OK, others `CCCR_Warning` (ignored) | same | no stdcall/vectorcall on portable |
| `TLSSupported` | true; backend errors loudly until M31 | same | |

Predefines (`getTargetDefines`): `__MSIL__` (shared with host MSIL triples), `__msil64__` / `__msil32__` (arch, like
`__wasm32__`), `__dotnet__` (portable-only key for compiler-rt `int_lib.h`, llvm-libc's OS branch and user code), the four
`__GCC_HAVE_SYNC_COMPARE_AND_SWAP_N`. `InitPreprocessor` adds `_LP64/__LP64__` (msil64) or `_ILP32/__ILP32__` (msil32) by
itself. Never: `__ELF__`, `__linux__`, `__unix__`, `_WIN32`, `__x86_64__`, `__aarch64__`, `__SSE*`, `__SEG_*`,
`__code_model_*`. `PlatformName` stays `"unknown"`.

### 2.3 CodeGen ABI: `MSILPortableABIInfo` (custom, derived from `DefaultABIInfo`)

What the backend expects (audit):
- `MSILISelLoweringCall.cpp` `LowerFormalArguments`/`LowerCall`: `byval` arguments are copied by the caller into a frame
  slot (`MSILFunctionInfo::getByvalSlot`); `inalloca`/`preallocated` are errors; `sret` is recorded in the link-info
  record (`MSILAsmPrinter` call-site signature: `MSILLink::Sret`, `SretSize`, `SretAlignLog2`, `Byval` list);
  `MSILFixFunctionBitcasts.cpp` keeps `byval`/`sret`/alignment across signature fixes. None of this depends on SysV,
  Win64 or AAPCS; it is attribute-driven.
- ABI-flag dependencies on the native triple are only: `MSILTargetLowering::LowerCall` `passInt128ByPointer` for Win64
  libcalls (`TT.getArch() == x86_64 && TT.isOSWindows()`), `getNativeVAListStructSize` (struct vs pointer va_list), and the
  gate `isSupportedTarget` in `MSILPrepareIR.cpp` ("the arch selects the C ABI"). The x86-64 SysV coercions
  (`{i64,i64}` small-struct pairs) and Win64 indirection are produced by clang's native ABIInfo, not assumed by the
  backend.
- `DefaultABIInfo::EmitVAArg` (`clang/lib/CodeGen/ABIInfoImpl.cpp`) emits an IR `va_arg` instruction. 5.15.2 accepts IR
  `va_arg` on char* triples, but the robust and upstream-standard form is the WebAssembly one: clang lowers `va_arg`
  itself with `emitVoidPtrVAArg`, and ExpandVariadics lays out calls; both must use one rule.

Proposal (new `clang/lib/CodeGen/Targets/MSIL.cpp`, `createMSILPortableTargetCodeGenInfo`, chosen in
`createTargetCodeGenInfo` by `case llvm::Triple::msil32: case llvm::Triple::msil64:`):
1. Arguments: empty records ignored (as WebAssembly); other aggregates indirect `byval` at natural alignment
   (DefaultABIInfo); enums as their integer type; promotable integers `signext`/`zeroext`; `__int128` direct (i128);
   `_BitInt(N > 128)` indirect.
2. Returns: void ignored; empty records ignored; aggregates `sret` (DefaultABIInfo); scalars direct.
3. Varargs: `EmitVAArg` = `emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*IsIndirect=*/isAggregateTypeForABI(Ty) &&
   !isEmptyRecord, TypeInfo, /*SlotSize=*/8, /*AllowHigherAlign=*/true, Slot)`. Slot size 8 on both widths (doubles and
   int64 are common; one rule for P-4). Matching ExpandVariadics class `MSILPortable`: `vaListType` = `ptr` passed in SSA,
   `initializeVaList` = the buffer, `slotInfo` = align max(8, ABI align of the type), indirect for every `StructType` and
   `ArrayType` (clang passes aggregates `byval`, ExpandVariadics then stores a pointer to a caller copy, which
   `emitVoidPtrVAArg(IsIndirect=true)` dereferences). One unit test pins both sides (`clang/test/CodeGen/MSIL/
   portable-abi.c` plus `llvm/test/Transforms/ExpandVariadics/msil-abi.ll`'s new portable rows).
4. `setTargetAttributes`: nothing new (no-prototype marking and import attributes stay format-keyed in
   `CodeGenModule.cpp`).
5. `getDependentLibraryOption`: default (`-l<name>`), which msil-ld tokenizes GNU-style on dotnet.

Why not plain `DefaultABIInfo`: its IR `va_arg` relies on a backend path the plan calls a backstop, and its non-ignored
empty records change the C++ ABI the Qt goal will freeze. Every choice here is frozen once `c.dll` ships (struct return
of `div_t`/`ldiv_t`, `printf` varargs).

---

## 3. Driver

### 3.1 Today

`createMSILToolChain` (`clang/lib/Driver/ToolChains/MSIL.cpp`) instantiates `MSILToolChain<Linux|MSVCToolChain|MinGW>`
(native toolchain for headers/paths), else `UnsupportedMSILToolChain`. `msil::Linker::ConstructJob` passes
`--target=<triple>`, `--sysroot`, `--shared` for `-shared`, refuses `-static` with `err_drv_unsupported_opt_for_target`
("No MSIL triple has a static C library"), claims PIE/PIC/libgcc options per native OS, adds `-L`/`-u`, `AddFilePathLibArgs`,
MSVC library paths, LTO options, inputs, `addDefaultLibs` (Linux `-lc [-lm] [-lpthread]`; MSVC `-lucrt -lkernel32 -loldnames
--ucrtbase-imports`; MinGW `-lmsvcrt ...`) and `clang_rt.builtins` via `TC.getCompilerRT(Args, "builtins")` unless
`-nostdlib/-nodefaultlibs`. The M25 builtins live in the resource dir per target
(`build/lib/clang/24/lib/x86_64-unknown-linux-gnu-msil/libclang_rt.builtins.a`, `.../x86_64-pc-windows-msvc-msil/
clang_rt.builtins.lib`).

### 3.2 `DotNetToolChain` (standalone, `clang/lib/Driver/ToolChains/MSIL.{h,cpp}`, no new file)

`createMSILToolChain`: `if (Triple.isMSIL()) return Triple.isOSDotNet() ? std::make_unique<DotNetToolChain>(...) :
std::make_unique<UnsupportedMSILToolChain>(...);` first. `DotNetToolChain : public ToolChain` with the same overrides as
`MSILToolChain` (`getDefaultLinker() = "msil-ld"`, `HasNativeLLVMSupport`, PIC/PIE false, unwind tables none,
`getSupportedSanitizers` shared, `RLT_CompilerRT`, `buildLinker` -> `msil::Linker`, `buildAssembler` ->
`err_no_external_assembler`, `buildStaticLibTool` -> `gnutools::StaticLibTool`), plus:

- **Sysroot.** `computeSysRoot()`: `--sysroot` if given, else BareMetal's convention
  `<InstalledDir>/../lib/clang-runtimes/<triple>` (`clang/lib/Driver/ToolChains/BareMetal.cpp` `BareMetal::computeSysRoot`),
  which suits the "LLVM-style toolchain release" goal. Layout:
  ```
  <sysroot>/include/                      llvm-libc public headers (P-1b)
  <sysroot>/include/msil64-unknown-dotnet/ width-specific headers if any (llvm-libc's llvm-libc-types are mostly width-neutral)
  <sysroot>/lib/msil64-unknown-dotnet/c.dll    shared C library assembly (P-1b)
  <sysroot>/lib/msil64-unknown-dotnet/libc.a   static archive (built by P-1b, usable from P-3)
  ```
  `clang_rt.builtins` stays where M25 put the others, `<resource>/lib/msil64-unknown-dotnet/libclang_rt.builtins.a`
  (`-print-libgcc-file-name` and `getCompilerRT` unchanged), not in the sysroot (question Q3 if the user prefers the
  sysroot).
- **Headers.** `AddClangSystemIncludeArgs`: unless `-nostdinc`: resource `include/` (unless `-nobuiltininc`), then unless
  `-nostdlibinc`: `<sysroot>/include/<triple>` and `<sysroot>/include` (WebAssembly's order,
  `clang/lib/Driver/ToolChains/WebAssembly.cpp` `WebAssembly::AddClangSystemIncludeArgs`). C++ include paths: none until
  the C++ goal (a C++ compile works freestanding, which P-1b's llvm-libc build needs).
- **Library paths.** `getFilePaths()` gets `<sysroot>/lib/<triple>`, passed by `AddFilePathLibArgs` as `-L`.
- **Default libraries** (`addDefaultLibs` gets a dotnet branch): `-lc` unless `-nolibc`; no `-lm` (llvm-libc is one
  library), no `-lpthread` (`-pthread`/`-pthreads` claimed, add nothing; threads come in P-3 inside libc); then
  `clang_rt.builtins` as today. `-nostdlib`/`-nodefaultlibs` drop both. Until P-1b, `-lc` cannot resolve, so P-1a's link
  tests use `-nostdlib` (critic NOTE).
- **`-static`.** Portable branch of the refusal in `msil::Linker::ConstructJob`: a dedicated message, e.g.
  `err_drv_msil_portable_static`: "'-static' is not supported for 'msil64-unknown-dotnet' yet: the managed C library
  links only as the shared assembly c.dll" (new `DiagnosticDriverKinds.td` entry, 85 objects, inside the WP1 union), or the
  existing `err_drv_unsupported_opt_for_target` plus a note. Host triples keep today's message.
- **`-shared`.** `--shared` as today (M22 library assembly).
- **Claimed options.** PIE/PIC/rdynamic/nostartfiles as today; no `-static-libgcc`/`-shared-libgcc` claims (no libgcc on
  dotnet: they warn as unused, like on WebAssembly).
- **`msil32-unknown-dotnet`.** Same toolchain; compiles reach the backend's refusal; the link job is built and msil-ld
  refuses the triple (loud, before P-4).

### 3.3 msil-ld `-l` lookup (scoped to dotnet only, critic NOTE)

`getLibraryCandidates` (`lld/MSIL/Driver.cpp`) gets a dotnet branch before the Windows/ELF ones:
`{"<name>.dll", "lib<name>.dll", "lib<name>.a"}` -- shared-first (managed toolchain library), then the archive; never
`.so`, `.lib`, `.dll.a` (no native libraries on portable). `searchLibraryBaseName`'s static-first rule (Design.md 5.2)
stays for host triples; `-Bstatic` keeps "static only" everywhere. `isSupportedTarget` accepts `msil64` + `DotNet` only;
msil32 gets "msil32-unknown-dotnet is not supported yet" (loud).

---

## 4. Native-triple query sites (target, msil-ld, CLI, driver, clang)

Method: `grep -nE '\b(isOS[A-Za-z]+|isWindows[A-Za-z]+|isMusl|isGNUEnvironment|getArch|getOS|getEnvironment|
isArch(64|32)Bit|isX86|isAArch64|getDefaultWCharSize|isAndroid|isOSDarwin|isLittleEndian|getNativeFormatTriple|getArchName|
getOSName|getVendor|isPS|isOSCygMing|getProcessTriple)\('` minus `isOSBinFormatMSIL` and comment lines, plus a
`Triple::(x86_64|aarch64|...)` enum grep (`<scratch>/p0/target/sites2.txt`, 70 lines). Counts: target 21 lines
(20 real + one `DL.isLittleEndian()` assert) in 13 functions; lld/MSIL 46 lines in 30 functions (one host-only); llvm/lib/CLI
3 lines in 2 functions; `MSILLink.h` 1 function; driver `MSIL.cpp` 11 lines in 4 functions; `Targets/MSIL.h` 4 lines (the
critic's ~12/28/10/4 under-counted lld).

Most `isOSWindows()` tests in the target and msil-ld are really "COFF rules vs ELF rules". Proposal: one fork helper in
`llvm/include/llvm/CLI/ImageLayout.h` / `llvm/lib/CLI` (fork prefix, no upstream hook): extend `enum class SectionRules :
uint8_t { ELF, COFF }` with `DotNet` and add `SectionRules cli::getSectionRules(const Triple &)` plus small predicates
(`definesSectionBoundaries` = ELF or DotNet; `hasNativeLibraries` = not DotNet; `constructorsTakeArgs` = ELF only at P-1a).
Each site below then asks the helper instead of `isOSWindows()`. The exact DotNet rule content (constructor/destructor
order, start/stop symbols, export visibility) is the link-rules recon's; the column gives the P-1a behavior assumed here.

### 4.1 llvm/lib/Target/MSIL (13 functions)

| File : function | Query | Portable (msil64-unknown-dotnet) behavior |
|---|---|---|
| `MSILPrepareIR.cpp` : `isSupportedTarget` | arch x86_64/aarch64, 64-bit pointers | accept `TT.isMSIL() && TT.isOSDotNet() && arch msil64 && ptr 64`; msil32 -> its own loud message "msil32-unknown-dotnet is not supported yet (planned after Phase 3)"; update the error text ("Phase 1 supports ...") |
| `MSILPrepareIR.cpp` : `lowerExternWeak` | `!isOSWindows()` keeps `__start_/__stop_` | `definesSectionBoundaries(TT)`: kept on dotnet (user's family includes start/stop) |
| `MSILPrepareIR.cpp` : `hasMemoryAlternative` | `isAArch64()` for 'Q' | unchanged (inline asm is rejected in clang on every MSIL triple) |
| `MSILAsmPrinter.cpp` : `getDataImportFunctions` (sites.tsv's "getRuntimeHelperText" is a heuristic mislabel) | `isOSWindows()` LoadLibraryW vs dlopen | unreachable on dotnet: a native data import is diagnosed earlier ("native data imports are not available on msil64-unknown-dotnet") |
| `MSILAsmPrinter.cpp` : `getConstructorArgs` | Windows: no ctor args | dotnet: no ctor args at P-1a (no native `environ`; revisit when `c.dll` exports `environ` in P-2) |
| `MSILAsmPrinter.cpp` : `getCLibrarySoname` | musl vs glibc soname | unreachable on dotnet (only for the envp ctor's `environ` import) |
| `MSILAsmPrinter.cpp` : `getSectionRules` | COFF vs ELF | `cli::getSectionRules(TT)` -> `DotNet` |
| `MSILAsmPrinter.cpp` : `collectExternGlobals` | `StartStop = !isOSWindows()` | `definesSectionBoundaries(TT)` -> true |
| `MSILAsmPrinter.cpp` : `emitTrailer` | DataImportHelper Kernel32 vs Dlfcn | no DataImportHelper on dotnet (diagnosed earlier); the process-width guard (IntPtr.Size, BitConverter.IsLittleEndian, Environment.FailFast) is emitted here for portable outputs (rows per 5.13.7; loader recon) |
| `MSILAsmPrinter.cpp` : `emitObjectEnd` | `getDefaultWCharSize()`; `setDataModel` | unchanged (4; LongSize 8 via pointer size) |
| `MSILISelLowering.cpp` : `getNativeVAListStructSize` | Windows/Darwin-ARM64 char*, else 24/32 | `if (TT.isMSIL()) return 0;` first (else msil64 would fall to "24", SysV) |
| `MSILISelLowering.cpp` : `MSILTargetLowering` ctor | `isWindowsGNUEnvironment()` ldexpf/frexpf promotion | false on dotnet; availability comes from the new MSIL RuntimeLibcalls library |
| `MSILISelLoweringCall.cpp` : `LowerCall` | Win64 `passInt128ByPointer` | not on dotnet (i128 direct, as clang's portable ABI) |
| `MSILUtilities.cpp` : `getCRuntime` (5 lines) / `MSIL::getPInvokeModule` | libc/ucrt/msvcrt names | `getCRuntime` already returns `std::nullopt` for OS dotnet (no standalone import table); an explicit `import_module` on dotnet becomes a loud error unless the user allows opt-in P/Invokes (Q1) |
| `MCTargetDesc/MSILMCAsmInfo.cpp` : ctor | `isArch64Bit()` | correct once `getArchPointerBitWidth` knows msil64 (8) / msil32 (4) |
| `MSILDataSegment.cpp` : `addGlobal` | `DL.isLittleEndian()` assert | holds (layout `e`) |

D2 (pointers and 64-bit ints as CIL `native int`) stays for msil64; it is exactly why the 64-bit-process guard is needed.

### 4.2 lld/MSIL (30 functions, 46 lines)

| File : function | Portable behavior |
|---|---|
| `Driver.cpp` : `isSupportedTarget` (3) | accept msil64 + DotNet; msil32 loud |
| `Driver.cpp` : `getLibraryCandidates` (2) | dotnet branch, shared-first (3.3) |
| `Driver.cpp` : `hasTripleKey` (3), `AssemblyFiles.cpp` : `hasTripleKey` (2) | unchanged: arch/OS/env differ, so host and portable inputs never mix (loud) |
| `Driver.cpp` : `getDataModel` (2), `AssemblyFiles.cpp` : `ManagedLibraryFile::check` (2), `Exports.cpp` : `getRecordHeader` (2) | unchanged (`setDataModel`: msil64 LongSize 8, msil32 4; wchar 4) |
| `Driver.cpp` : `LinkerDriver::linkerMain` (`--ucrtbase-imports` needs Windows) | unchanged (refused on dotnet) |
| `Driver.cpp` : `getQuotingStyle` | host process, not the target: unchanged |
| `Exports.cpp` : `selectExports` (2) | dotnet: default-visibility definitions exported, no dllexport filter, COMDAT rule as the link-rules recon decides |
| `Exports.cpp` : `getTableTriple` (2) | unchanged (no env version) |
| `InputFiles.cpp` : `ObjFile::getLinkerOptions`, `BitcodeFile::getLinkerOptions` | GNU tokenization (not Windows) |
| `LTO.cpp` : `isSectionBoundary`, `isHiddenSectionBoundaryReference`, `BitcodeCompiler` ctor | `definesSectionBoundaries` |
| `LTO.cpp` : `findEnvpConstructor` | dotnet: no envp ctors at P-1a (as Windows) |
| `MarkLive.cpp` : `MarkLive::run` (2) | `definesSectionBoundaries`; rules `getSectionRules(tt)` |
| `OutputSegment.cpp` : `OutputImage::layout` | defines `__start_/__stop_` on dotnet |
| `SymbolTable.cpp` : `bindLateSymbols` (2) | `definesSectionBoundaries` |
| `Writer.cpp` : `Writer` ctor | `getSectionRules(tt)` |
| `NativeLibraries.cpp` : `mayBindNatively` | dotnet: false for every name (no P/Invoke binding; an undefined symbol is an undefined-symbol error naming the missing library, e.g. "link c.dll" for libc names) |
| `NativeLibraries.cpp` : `getNameClassLine` | dotnet atomic text: "an atomic operation (libatomic's interface): not available on msil64-unknown-dotnet before threads (P-3)" |
| `NativeLibraries.cpp` : `findDataImportFunctions`, `Relocations.cpp` : `getDataImportHelper` | unreachable on dotnet (no native data) |
| `NativeLibraries.cpp` : `hasWideNativeLongDouble` (2) | dotnet: false (today returns true for every non-Windows triple; the portable libc is built with long double = double, so no forwarders) |
| `NativeLibraries.cpp` : `isOtherFormat`, `getOtherFormatMessage`, `isCompatibleELF` (2), `isCompatibleCOFF` (2) | dotnet: every native library (ELF, PE/COFF, import library) is refused with "native library X cannot be linked for msil64-unknown-dotnet" |

### 4.3 llvm/lib/CLI and BinaryFormat

| Site | Portable behavior |
|---|---|
| `PEWriter.cpp` : `PEOptions::get` (arch switch; "not supported until phase 5" for PE32) | blocked on the loader recon: AnyCPU (`IMAGE_FILE_MACHINE_I386`, PE32, ILONLY, no 32BITREQUIRED/PREFERRED) is Roslyn's portable envelope and may need its mscoree stub and `.reloc` for Framework 4.8; PE32+ AMD64 would pin x64 |
| `TrailerDefs.cpp` : `cli::isOfferedIn` (`WindowsExecutable` scope) | unchanged (not offered on dotnet); a `Portable` scope for the width guard if it is a trailer definition |
| `MSILLink.h` : `setDataModel` (`isOSWindows() && !Cygwin` -> LongSize 4) | unchanged |
| `ImageLayout.h` : `enum class SectionRules` | gains `DotNet` + `getSectionRules(const Triple&)` (fork prefix; 20 objects, inside the WP1 union) |

### 4.4 Driver `clang/lib/Driver/ToolChains/MSIL.cpp` (4 functions, 11 lines)

`addDefaultLibs` (Linux/musl/MSVC/MinGW branches: 4 lines) -> dotnet branch first (3.2); `msil::Linker::ConstructJob`
(libgcc/nolibc claims and MSVC paths: 4 lines) -> skip on dotnet, portable `-static` message; `createMSILToolChain` (3
lines) -> `DotNetToolChain` first. `UnsupportedMSILToolChain` keeps catching `msil64-unknown-linux` etc.

### 4.5 `clang/lib/Basic/Targets/MSIL.h` (4 lines)

The `assert(isOSBinFormatMSIL())`, the native COFF limit (`NativeTriple.isX86() && isOSWindows() && isOSBinFormatCOFF()`)
and the `__ELF__` test are inside `MSILTargetInfo<Native>`, which portable never instantiates. No change; the new
`MSILPortableTargetInfo` sits beside it.

---

## 5. Rebuild cost (measured read-only)

Copies of `<build>/.ninja_deps` (31,726,528 bytes, version 4, 15,220 paths, 5,696 outputs with deps) and
`.ninja_log` (both dated 2026-11-01 09:37, identical size to the build's) in `<scratch>/p0/target/`; parsed by
`deps.py` / `union.py` (no ninja run in the build tree). "job-min" = sum of logged compile durations of the affected
objects (last build per object, measured under that build's own parallelism). Whole build: 3,332 objects, 184.9 job-min
(the 737 job-min of "other" log entries are CMake install/regen stamps, not compiles).

| Header / generated file | Objects | job-min |
|---|---:|---:|
| `llvm/TargetParser/Triple.h` | 2,477 | 158.7 |
| `llvm/IR/DataLayout.h` (not edited, reference) | 1,602 | 97.2 |
| `llvm/IR/RuntimeLibcalls.inc` / `RuntimeLibcalls.h` (from `RuntimeLibcalls.td`) | 598 | 32.6 |
| `clang/Basic/TargetInfo.h` (avoided) | 309 | 33.4 |
| `clang/Basic/DiagnosticDriverKinds.inc` | 85 | 4.8 |
| `llvm/MC/TargetRegistry.h` (not edited) | 80 | 6.7 |
| `clang/Driver/ToolChain.h` (avoided) | 76 | 3.7 |
| `clang/lib/CodeGen/TargetInfo.h` | 58 | 5.0 |
| `llvm/BinaryFormat/MSILLink.h` | 47 | — |
| `llvm/CLI/ImageLayout.h` | 20 | 1.8 |
| `llvm/CLI/TrailerDefs.h` / `Trailer.h` | 18 / 13 | 1.8 / 1.3 |
| `lib/Driver/ToolChains/MSIL.h` / `lib/Basic/Targets/MSIL.h` / `TripleName.def` | 2 / 1 / 1 | 0.2 / 0.1 / 0.0 |

**Union of every header WP1 would touch** (`union.py llvm/TargetParser/Triple.h llvm/IR/RuntimeLibcalls.inc
clang/Basic/DiagnosticDriverKinds.inc lib/CodeGen/TargetInfo.h llvm/CLI/ImageLayout.h llvm/BinaryFormat/MSILLink.h
llvm/CLI/TrailerDefs.h lib/Basic/Targets/MSIL.h lib/Driver/ToolChains/MSIL.h llvm/CLI/Trailer.h
llvm/TargetParser/TripleName.def`, output `union-wp1.txt`): **2,481 objects, 158.9 job-min** -- 74% of the objects and 86%
of the compile time of a full build; everything except 8 objects is already inside Triple.h's set. On the 4C/8T host at
`-j8` that is roughly 20-30 min wall plus relinks of every tool (clang.exe alone logs 3.5 s; links are cheap here); at
the 2-process cap of a concurrent post-commit run it is about 80 min. A second Triple.h edit later costs the same again.

Front-loading (P-1a WP1, "triple and headers", one owner, builds and runs its unit tests before any other WP starts):
1. All header-level edits at once: Triple.h (both arches, OS, `isMSIL`/`isOSDotNet`, doc), TripleName.def,
   RuntimeLibcalls.td, DiagnosticDriverKinds.td (portable `-static`), `clang/lib/CodeGen/TargetInfo.h` declaration +
   CMakeLists line (stub `Targets/MSIL.cpp`), `ImageLayout.h` `SectionRules::DotNet` + helper declaration, and the CLI
   trailer/BCL row declarations if the width guard needs new ones.
2. The .cpp parts that make the triple coherent end to end: Triple.cpp, TargetDataLayout.cpp, TargetRegistry.cpp, and the
   unit tests (`TripleTest.cpp`, `TargetRegistry.cpp` unittest).
3. upstream-hooks.txt blocks for every new upstream file (TripleName.def, RuntimeLibcalls.td, CodeGen CMakeLists/TargetInfo.h,
   DiagnosticDriverKinds.td, int_lib.h) in the same WP so the manifest leg stays green.
Later WPs (clang TargetInfo/ABI, driver toolchain, backend sites, msil-ld sites, builtins archive, tests) touch only .cpp
files and fork headers with 1-20 dependents. This is the "R21 bend" the recommendation asks the user about: one
mid-phase Triple.h edit, isolated in WP1.

---

## 6. Test legs and gate pieces for P-1a

### 6.1 In-tree tests (no harness change)

- `llvm/unittests/TargetParser/TripleTest.cpp`: parse/normalize (`msil64-dotnet` -> `msil64-unknown-dotnet`), default
  format MSIL, `isArch64Bit`/`isArch32Bit`, 32/64 variants, endianness, `getNativeFormatTriple() == *this`,
  `msil64-unknown-dotnet-msil` equivalence, `isCompatibleWith` (portable vs `x86_64-unknown-linux-gnu-msil` false, msil64
  vs msil64 true, msil64 vs msil32 false), `computeDataLayout` strings for both widths.
- `llvm/unittests/MC/TargetRegistry.cpp`: `lookupTarget("msil64-unknown-dotnet")` -> msil; `msil64-unknown-dotnet-elf` ->
  error; `-march=x86-64` on the portable triple -> the existing error.
- `llvm/test/CodeGen/MSIL/portable-*.ll` (fork prefix): msil64 compiles (pointer = native int, data layout, no import
  table, guard in the module initializer), msil32 refused, native data import refused, explicit import module refused
  (or allowed, per Q1), `__start_/__stop_` kept.
- `llvm/test/Transforms/ExpandVariadics/msil-abi.ll` (M26's fork test): portable rows.
- `clang/test/Preprocessor/init-msil-portable.c` (fork-only line): full `-dM` lists for both widths with `-check-prefix`,
  `--implicit-check-not` for every host macro of section 2.1.
- `clang/test/CodeGen/MSIL/portable-abi.c`: byval/sret/empty records/i128/`va_arg` IR for both widths; `-fshort-wchar` and
  `-mlong-double-80` errors.
- `clang/test/Driver/msil-dotnet.c` (fork-only line): link job (`--target=msil64-unknown-dotnet`, `--sysroot`,
  `-L<sysroot>/lib/<triple>`, `-lc`, builtins path), `-nostdlib`, `-nolibc`, `-shared`, `-static` error text, include order
  with `-###`, default sysroot, msil32 job, `msil64-unknown-linux` unknown.
- `lld/test/MSIL/portable-*.test`: target gate, shared-first `-l` on dotnet only (host candidate order unchanged), native
  library refused, host/portable `hasTripleKey` mismatch, undefined libc name with no `c.dll` reported loud.

### 6.2 `msil/test/lit.cfg.py` (5.17.2 additions)

`msil_triple` today must end in `-msil` and derives `native_triple` from it (`default_msil_triple` and the check after it).
P-1a:

| Parameter / derived | Feature | Meaning |
|---|---|---|
| `msil_triple=msil64-unknown-dotnet` accepted | `msil-triple=msil64-unknown-dotnet`, `msil-triple-portable`, `msil-triple-msil64` | no `native_triple`; `triple_os = "dotnet"`; native oracle unavailable unless `msil_portable_oracle` names one |
| `msil_portable_oracle` (`none` default, or a host triple) | `msil-native-oracle` only when set | freestanding tests may compare against the host native build; others use hand-written expected files (5.17.4 `%{msil_expected_rule}`) |
| (derived) `clang --target=msil64-unknown-dotnet -E -dM` succeeds | `msil-portable` | the portable toolchain is built (any suite can `REQUIRES:` it) |
| `msil_portable_sysroot` (path; default the build's sysroot from P-1b) | `msil-portable-sysroot` | `c.dll` and headers exist (P-1b onward) |
| (derived) `<resource>/lib/msil64-unknown-dotnet/libclang_rt.builtins.a` exists | `msil-portable-builtins` | |

Substitutions: `%msil_portable_triple`, `%clang_portable` (`clang --target=msil64-unknown-dotnet
[--sysroot=<msil_portable_sysroot>]`), `%llc_portable`, `%{msil_portable_cflags}` (placeholder; `-nostdlib` in P-1a).
Placeholder names avoid substrings of existing ones (5.12.3).

### 6.3 Legs and gate (5.17.6, 5.17.7, 5.17.11)

- New leg `portable-smoke`, tag `Exit` only (user decision: post-commit runs only; M27-M35 gates unchanged): paths
  `msil/test/P1/` (new directory: freestanding programs, `-nostdlib`, self-checking via exit code), parameters
  `msil_triple=msil64-unknown-dotnet msil_pipeline=obj msil_dotnet_runtime=10 msil_run_netfx=1`; run 2 under WSL
  (`$wslRuntime`). The property worth pinning: one image, built once, runs on Windows .NET 10, .NET Framework 4.8 and
  Linux .NET 10 (WSL) -- the "portable" claim -- plus the width guard's FailFast when forced into a 32-bit process.
- P-1a's own milestone gate (5.17.7 standard rows): build, upstream-hooks manifest + lit leg (new hook blocks), the
  unit tests of 6.1, `check-llvm-codegen-msil`/clang/lld MSIL suites, `build-builtins.py --triple msil64-unknown-dotnet
  --check` (add the triple to `ALL_TRIPLES` in `msil/utils/build/build-builtins.py`; `msil/lib/builtins/sources.txt` globs
  decide members; the `int_lib.h` branch is the precondition), `portable-smoke` once, and the host legs unchanged (host
  output must not move: shared-first `-l` and the `SectionRules` refactor are dotnet-only).

---

## 7. Plan amendments P-1a needs (beyond the milestone text)

1. 5.13.5: P-1a hook rows for Triple.h, TripleName.def, Triple.cpp, TargetDataLayout.cpp, TargetRegistry.cpp,
   RuntimeLibcalls.td, ExpandVariadics.cpp (portable row), Targets.cpp, CodeGenModule.cpp, CodeGen TargetInfo.h and
   CMakeLists.txt, DiagnosticDriverKinds.td, compiler-rt `int_lib.h`; and amend "No Phase 3 milestone edits ...
   `RuntimeLibcalls.td`" and "compiler-rt is not edited in Phase 3" (the user decided the int_lib.h branch; the
   RuntimeLibcalls edit is a consequence of a new arch and has no alternative short of not adding the arch).
2. 5.13.7 / B2: the width-guard BCL rows (`System.IntPtr::get_Size`, `System.BitConverter::IsLittleEndian`,
   `System.Environment::FailFast(string)`, FailFast moving earlier than M28's planned rows) need a `// M<n>:`-style heading
   row (critic NOTE).
3. 5.15.11 fail-loudly table: per-triple rows (native imports, data imports, `__atomic_*`, `-static`, msil32, TLS,
   setjmp, function pointers to native code) with the step that lifts each.
4. Frozen-ABI list (recommendation section 3 / contract 5.18): data layouts, `size_t`/`int64_t` spellings, `SuitableAlign`
   128, `MaxAtomicPromoteWidth` 64, the portable ABIInfo and va slot rule, wchar 4 signed, long double = double.

---

## 8. Risks

- **R1 recursion/regression in shared Triple paths.** `computeDataLayout` recurses for a portable triple unless guarded
  by arch; any other future `getNativeFormatTriple()` caller that assumes "native is not MSIL" must check. Mitigation:
  unit tests in WP1 and the doc on `getNativeFormatTriple`.
- **R2 RuntimeLibcalls gap.** Without an MSIL system library every libcall of a new arch is "unsupported" (generated
  lookup returns `{}`); symptoms range from crashes in legalization to silent expansions. Must land in WP1.
- **R3 upstream conflict surface grows**: Triple.h enums (appended at the end, lowest conflict), TripleName.def,
  RuntimeLibcalls.td, int_lib.h, CodeGen CMakeLists/TargetInfo.h. Each upstream merge re-checks them (upstream-hooks).
- **R4 full rebuild** (~159 job-min, 2,481 objects) in WP1, and again for any later Triple.h edit; schedule WP1 when no
  post-commit run shares the host.
- **R5 ABI freeze.** The portable ABIInfo, va slot rule, data layout and type spellings become permanent once `c.dll`
  ships (P-1b); the libc probe used the SysV host ABI and `-U`'d macros, so the real TargetInfo may surface new
  llvm-libc issues (long double, wchar, `size_t` = `unsigned long` on msil32 vs llvm-libc's assumptions).
- **R6 envelope dependency.** `PEOptions::get` cannot be written until the loader recon settles AnyCPU PE32 vs PE32+.
- **R7 host output drift.** The `SectionRules` refactor touches ~20 msil-ld/target sites that host triples use; a
  behavior-preserving refactor needs the host legs as its oracle.
- **R8 TargetLibraryInfo** treats OS dotnet as "everything available"; harmless before P-2, re-audit when stdio arrives.

## 9. Questions for the user

- **Q1 P/Invoke on the portable triple.** Options: (a) loud error for every native import, explicit
  `import_module` included (pure portability); (b) allow explicit `import_module`/`import_name` declarations as an opt-in
  that makes the output non-portable, while implicit binding stays off. Recommendation: (a) now; (b) can be added later
  without breaking anything.
- **Q2 type spellings.** `size_t`/`ptrdiff_t`/`intptr_t` as `(unsigned) long` and `int64_t` as `long long` on both widths
  (wasm's choice; same C++ mangling on msil32 and msil64), or Linux-style (`size_t` = `unsigned int` on ILP32, `int64_t` =
  `long` on LP64). Recommendation: wasm-style.
- **Q3 where `clang_rt.builtins` lives for portable.** Resource dir `<resource>/lib/msil64-unknown-dotnet/` like M25's
  host archives (recommended: `-print-libgcc-file-name` keeps working), or inside the sysroot next to `c.dll`.
- **Q4 plan-rule exemptions.** Approve P-1a edits to `RuntimeLibcalls.td` (needed by any new arch) and the `int_lib.h`
  branch (already decided in substance) against 5.13.5's "no RuntimeLibcalls.td" and "compiler-rt not edited in Phase 3",
  plus the Triple.h front-load (R21 bend). Recommendation: approve all three, isolated in WP1.

---

## Verifier corrections

Adversarial check, 2026-11-01, of this report against the tree at `e38f9a199bc9` (the working tree carries uncommitted
M26 edits; nothing was built, no ninja/cmake run). Probes re-run with `<build>/bin` read-only in
`<scratch>/p0/target/` (`t.c`, `t.ll`, new `v.c`); `union.py` re-run on the copied `.ninja_deps`.

### Confirmed (re-checked, no change)

- Recursion hazard: `Triple::computeDataLayout` (TargetDataLayout.cpp) redirects every MSIL-format triple to
  `getNativeFormatTriple()`, and `Triple::getNativeFormatTriple` (Triple.cpp) rebuilds `msil64-unknown-dotnet` from
  arch/vendor/OS, which with the proposed `getDefaultFormat` is MSIL again: infinite recursion unless guarded.
  `Triple::isCompatibleWith` already stops on "native still MSIL". Only these consumers of `getNativeFormatTriple` exist:
  Triple.cpp `isCompatibleWith`, TargetDataLayout.cpp, `clang/lib/Basic/Targets/MSIL.h` (two), `SemaDeclAttr.cpp`; lld
  has none.
- RuntimeLibcalls: the generated system-library lookup in `build/include/llvm/IR/RuntimeLibcalls.inc` ends in
  `return {};`; `isDefaultLibcallArch` (RuntimeLibcalls.td) names MIPS/LoongArch/VE/BPF/CSKY/ARC/M68k/Xtensa/SystemZ
  only, so a new arch gets no library. Plan 5.13.5's closing paragraph does forbid `RuntimeLibcalls.td` edits and says
  compiler-rt is not edited in Phase 3.
- Tool probes reproduce exactly: clang "unknown target triple 'msil64-unknown-dotnet'"; llc "unable to get target"; the
  `-msil` spelling reaches `MSILPrepareIR` `isSupportedTarget`'s Phase 1 text; `-print-target-triple --target=msil64-dotnet`
  prints `msil64-dotnet`. The host macro dump and the Windows MSIL triple's 16-bit unsigned `wint_t` are confirmed.
- `TargetRegistry::lookupTarget` format branch, `ArchMatchFn` always false, `Driver::getToolChain` trying
  `createMSILToolChain` before the OS switch, `getDefaultImageName` -> `a.exe`, `getNativeVAListStructSize` (would give
  24 for msil64), the `passInt128ByPointer` gate, the `isWindowsGNUEnvironment` ldexpf promotion, `getCRuntime` ->
  nullopt for a non-Linux non-Windows OS, `hasWideNativeLongDouble` true for every non-Windows triple, `setDataModel`,
  `getDefaultWCharSize` (4), the `PEOptions::get` arch switch, `cli::isOfferedIn`, the `getDataImportFunctions` mislabel,
  the `int_lib.h` first `COMPILER_RT_ALIAS` branch, the `lit.cfg.py` `-msil` requirement, `DefaultABIInfo` behaviour
  (aggregates and empty records indirect byval, IR `va_arg` via `EmitVAArgInstr`), ExpandVariadics byval + indirect
  handling (`IndirectAlloca` copy).
- Rebuild union re-run: 2,481 objects / 158.9 job-min; Triple.h 2,477; RuntimeLibcalls.inc 598; DiagnosticDriverKinds.inc
  85 (2 outside Triple.h); CodeGen TargetInfo.h 58; ImageLayout.h 20.

### Errors

1. **Shared-first `-l` is not a candidate-order change.** Section 3.3 and the summary say `getLibraryCandidates`
   (lld/MSIL/Driver.cpp) gets a dotnet branch `{<name>.dll, lib<name>.dll, lib<name>.a}`. In `searchLibraryBaseName` the
   `LibraryKind::Static` case returns immediately, while managed and native hits are only remembered until the directory
   is done. With that order, `-lc` in a directory holding both `c.dll` and `libc.a` (the proposed sysroot layout) still
   returns `libc.a`. Correction: shared-first needs a dotnet branch in `searchLibraryBaseName`'s per-directory selection
   (managed before static unless `-Bstatic`/`-static`), not only a new candidate list.
2. **`libc.a` fallback breaks "`-static` is a loud error until P-3".** With shared-first-then-archive, a plain `-lc` falls
   back to `libc.a` whenever a directory has the archive but no `c.dll`. `-l:libc.a`, or a direct `msil-ld -Bstatic -lc`,
   links it with no driver `-static` at all, which bypasses the driver-only refusal in `msil::Linker::ConstructJob`. That
   is the static, shared-state mode before its prerequisite (P-3), which the standing rule forbids. Correction: P-1b
   ships no `libc.a` in the portable sysroot until P-3, or msil-ld refuses the libc archive (or `-static`/`-Bstatic` on
   `*-unknown-dotnet`) loudly until P-3. State which.
3. **`x86_64-unknown-dotnet` is not "unknown target triple".** Section 2.2 says it "reaches the native dispatch and is
   unknown too". Once `dotnet` parses as an OS, `x86_64-unknown-dotnet` has default format ELF. `AllocateTarget`
   (clang/lib/Basic/Targets.cpp) then takes the x86_64 case, and its OS `default:` returns a generic `X86_64TargetInfo`
   (measured: `clang --target=x86_64-unknown-foo -c v.c` succeeds today). The driver picks a generic ELF toolchain, and
   the result is native x86-64 ELF code whose triple answers `isOSDotNet()`. Correction: either refuse a non-msil arch
   with OS dotnet (clang `AllocateTarget` and `lookupTarget`), or document it as an ordinary unknown-OS native triple.
   Add a test either way.
4. **`wint_t` is not a user decision.** The 2.2 table marks `wchar_t`/`wint_t` = `int` as "user decision", but the user
   decided only `wchar_t` (32-bit signed). `wint_t` signedness is a separate frozen ABI choice. Linux and Fuchsia
   (llvm-libc's main targets; `LinuxTargetInfo` and `FuchsiaTargetInfo` in clang/lib/Basic/Targets/OSTargets.h) use
   `unsigned int`, and llvm-libc's `WEOF` is `((wint_t)(0xffffffffu))` (libc/include/llvm-libc-macros/wchar-macros.h).
   Correction: put `wint_t` in the frozen-ABI list (section 7 item 4) with a recommendation (`unsigned int` matches
   llvm-libc's tested configurations), or ask the user.
5. **`_BitInt` threshold on msil32.** The table says DefaultABIInfo passes `_BitInt(N > 128)` indirectly on both widths.
   `DefaultABIInfo::classifyArgumentType` and `classifyReturnType` use `Int128Ty` only when `hasInt128Type()`, so on
   msil32 (no `__int128`) the threshold is 64. The ExpandVariadics `MSILPortable` class and the proposed tests must use
   the width-dependent threshold.
6. **i128 libcalls.** Section 1.2 and the summary say "i128 helpers on msil64 only" and give `__divti3` as an example of a
   needed libcall. The backend expands i128 division inline (`setMaxDivRemBitWidthSupported` + ExpandIRInsts, in the
   MSILISelLowering.cpp constructor) and shifts via `*_PARTS`. `msil/lib/builtins/sources.txt` excludes every `*ti*`
   source ("MSIL lowers 128-bit integer operations inline"). Listing the TI helpers as available is harmless only while
   the backend never emits them, since the archive does not provide them. Correction: say so, and pick the MSIL system
   library's i128 rows to match what the host triples effectively get.
7. **"Not touched (verified)" leaves out real edits.** `msil/utils/build/build-builtins.py` validates `--triple` with
   `t.endswith("-msil")` (argument parsing, `UsageError "... is not an MSIL triple"`), so adding the triple to
   `ALL_TRIPLES` alone fails `build-builtins.py --triple msil64-unknown-dotnet --check`. `msil/lib/builtins/sources.txt`
   lists `int_util.c` and the `-ftrapv` helpers (`absv*`, `addv*`, `mulv*`, `negv*`, `subv*`) for `*-linux-*` only. The
   portable archive therefore lacks `__compilerrt_abort_impl` and every `-ftrapv` helper unless their globs gain the
   dotnet triple (int_util.c compiles freestanding without `_WIN32`). Without `__ELF__`, `comparedf2.c` and
   `comparesf2.c` also drop the `__cmpdf2`/`__cmpsf2` aliases. That is harmless, but the member and symbol set then
   differs from the host archives', and any `--check` symbol comparison must expect it.

### Gaps

- **Harness and tooling scripts that require `-msil`:** `msil/utils/msil-prog.py` (raises "does not end in -msil"),
  `msil/utils/msil-equiv.py` (rejects a triple not ending in `-msil`), `msil/utils/update-expected.py` (appends `-msil` to
  `--target`), plus `build-builtins.py` above. Section 6 covers only `lit.cfg.py`.
- **msil-ld `getMSILTriple`** (lld/MSIL/Driver.cpp) appends `-msil` to any non-MSIL triple. That is correct for the
  normalized portable triple (default format MSIL), but a non-normalized `msil64-dotnet` parses with OS unknown and is
  then refused by `isSupportedTarget`. It is missing from section 4.2 and needs a test or normalization.
- **M26 sites are not inventoried.** P-1a lands after M26, and the working tree already carries M26 edits that add
  code keyed on the native triple that this inventory lacks:
  - `clang/lib/Sema/SemaChecking.cpp` `getMSILNativeLongDoubleSize` (returns 0 for dotnet, which is right);
  - the planned `MSILVariadics.cpp` fixed-arity list keyed on the triple's C runtime;
  - the Windows double-as-i64 rule;
  - the SysV floating-point error;
  - `MSILPrepareIR`'s "va_arg on SysV/AAPCS64 triples" test;
  - the ExpandVariadics hook, which keys on `isOSBinFormatMSIL()` first, so the portable row must sit inside that block
    before native classification (the report gives this only as an alternative).

  Re-run the site inventory at M26's commit.
- **Design.md 5.2 conflict.** Design.md 5.2's `-lfoo` paragraph says shared-first applies once the toolchain ships shared
  library assemblies, with no triple restriction. The report scopes it to dotnet (per the critic's NOTE) without listing
  the Design.md 5.2 amendment or a user question. Add one of the two.
- **Plan amendment list incomplete:** the Phase 3 summary in section 11 of ImplementationPlan.md (the paragraph saying "Phase 3
  needs none: no IR intrinsic, `RuntimeLibcalls.td` or `CallingConv.h` change") also needs an amendment beside 5.13.5.
- **`-S` pipeline on portable:** the recommendation lists "`-S` externals are an error", but the report does not say what
  an undefined external does in `-S` output on dotnet (`MSIL::getPInvokeModule` returns nullopt), or which message and
  test pin it.
- **TripleName.def constraint:** `llvm/utils/get_triple_system_name.py` also parses the file (via
  `cmake/Modules/GetTripleCMakeSystemName.cmake`), so the new row must be one macro per line with string literals. The
  default build does not use that script, so there is no CMake rebuild cost.
- **Gate vs user decision:** section 6.3 runs `portable-smoke` once in P-1a's own gate, but the user decided the portable
  leg runs in post-commit runs only (M27-M35 gates unchanged). Running it in the milestone that creates it is probably
  intended. State it as an explicit exception or move it to the post-commit run.
- **Q4 partly reopens a decision:** the user already decided the `int_lib.h` branch ("compiler-rt's int_lib.h and llvm-libc
  need a branch"), and the RuntimeLibcalls edit follows from the decided arch names. Only the Triple.h front-load (R21
  bend) is a real question. Record the first two as amendment rows instead of asking.
- **Minor:** the host-macro list in 2.1 leaves out the non-suffixed spellings `__x86_64`, `__amd64`, `__k8`, `__linux`,
  `__unix`, which the dump (and the libc probe's `-U` list) contain. `init-msil-portable.c`'s `--implicit-check-not` list
  must include them. The `.ninja_deps`/`.ninja_log` copies in scratch are dated 2026-11-01 16:49 (09:37 is the build's
  own time, not re-checked).
