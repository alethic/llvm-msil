# MSIL recon: emitting a complete ILAsm module from the AsmPrinter (milestone 1)

- Key: `asmprinter`
- Date: 2026-10-12. Checkout: branch `msil-milestone1` @ `8da03434abcc`. Every API, path and line number below was checked against this tree. Anything not checked is marked *unverified*.
- Scope: Design.md 2.3, 4.1, 5.3, 5.5 and 12 (milestone 1: `-S` emits ILAsm, `ilasm` builds the PE). The report answers task questions 1–5 in sections 2–8. Section 9 has the complete verified example, and section 10 lists the experiments.
- Scratch experiments: `C:\Users\JEROME~1\AppData\Local\Temp\claude\D--llvm-msil\fa776849-ce33-4cee-bcc7-05382d063d3e\scratchpad\recon\asmprinter\`. They were assembled with `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe` (4.8.9221), disassembled with `ildasm.exe` from `NETFX 4.8 Tools\x64`, and run on .NET 8.0.31 and 10.0.12 (Windows) and .NET 10.0.12 in WSL Ubuntu (glibc 2.43).
- Sibling reports this one builds on: `passes-frame.md` (`.maxstack`, `.locals`, frame), `isel.md` (MIs, register classes, signature sources), `triple-objfmt.md` (`Triple::MSIL`, `MCContext::IsMSIL`, `MSILTargetObjectFile`), `registry-tm.md`, `wasm-inventory.md`. Points of disagreement are listed in section 11.

---

## 0. Summary and decisions

1. **File layout (verified end to end, section 9).** The printer writes the module in three phases:
   - **Header** (from `emitStartOfAsmFile`): `.assembly extern` and `.assembly`.
   - **Methods** (streamed per function): every C function is a **global method in `<Module>`**, written `.method public static <ret> 'name'(<params>) cil managed`.
   - **Trailer** (from `emitEndOfAsmFile`): P/Invoke declarations, the data image (`.class` sized blob type, `.data cil`, an RVA `.field … at`), one `initonly native int` address field per global, the module initializer `.cctor`, and the generated `.entrypoint` method.

   ilasm resolves forward references to global methods, global fields and data labels, so all module-level material can come *after* the methods. All calls bind to MethodDefs, with no MemberRefs (verified, e2/e6).
2. **The trailer must be computed at the end, not in `doInitialization`.** Two facts force this:
   - The legacy PM calls every contained pass's `doInitialization` before any module pass runs (LegacyPassManager.cpp:1501-1514). That means AsmPrinter's `doInitialization`/`emitStartOfAsmFile` run *before* FixFunctionBitcasts, AddMissingPrototypes, LowerGlobalDtors and the rest.
   - Functions are printed before `doFinalization` emits globals (AsmPrinter.cpp:2974 → 3214).

   Therefore code refers to globals **symbolically**, as `ldsfld native int 'table'`, never by segment offset. The layout is done in `doFinalization` → `emitGlobalVariable`/`emitEndOfAsmFile`.
3. **Always single-quote every symbol-derived identifier.** The quoted forms were verified for methods, fields, data labels and branch labels. Unquoted C names collide with ILAsm keywords: `add`, `div`, `rem`, `and`, `or`, `not`, `error`, `init`, `type`, `filter`, `record`, `assert`, `final`, `array`, `vector`, `string`, `float`, `value` and about 100 more all fail to assemble when unquoted (e2, keyword probe). Escapes:
   - `'` becomes `\'`.
   - A backslash becomes `\134`. Note that `\\` is **dropped** by ilasm (e6).
   - Control bytes become `\ooo` (octal).
   - Non-ASCII stays as raw UTF-8, **and the file must then start with a UTF-8 BOM**. Without the BOM, ilasm reads the file as ANSI and mangles the name. Octal escapes of UTF-8 bytes are mangled too (verified, `u1`/`u2`/`u3`).
4. **Labels.** Labels have the form `name:`, with names that follow ILAsm ID rules. A label right before `}` is accepted (verified). Dotted labels such as `.LBB0_1` and `L.BB0_1` are **rejected**. Set `MCAsmInfo::InternalSymbolPrefix = "@"`, so blocks print as `@BB0_1` and temporaries as `@func_end0`, and set `AllowAtInName = true`. `@` is a legal ILAsm ID start that no C identifier can contain (verified in `brtrue`, `blt`, `switch (...)`).
5. **MC integration: use the stock `MCAsmStreamer` (as NVPTX and WebAssembly do), with an `MSILMCAsmInfo` and AsmPrinter overrides that suppress every GNU directive.**
   - ILAsm directives are written by an `MSILTargetAsmStreamer` through `emitRawText`.
   - Instructions go through `MSILInstPrinter`.
   - Section 3 is the complete table of what the base AsmPrinter would emit and what neutralizes each item.
   - A custom `MCAsmBaseStreamer` subclass (the SystemZ HLASM precedent, registered with `TargetRegistry::RegisterAsmStreamer`) is a fallback if stray directives keep appearing (section 3.3).
6. **Signatures.**
   - Definitions, direct calls, `ldftn`, `calli` and P/Invoke declarations use the `MSIL::Signature` type from `isel.md` 8.1.
   - The **module-wide table lives in `MSILAsmPrinter`**: per `const Function*`, computed lazily with the same `computeSignatureVTs` that ISel uses; per external-symbol name for libcalls; and an interned set for `calli`.
   - The table reaches the InstPrinter through a target expression, `MSILMemberRefExpr : MCTargetExpr`, which carries `{IL name, const Signature*, kind, offset}`. The InstPrinter needs no side channel.
   - Undefined functions referenced from code *or data* are collected while lowering and printed as `pinvokeimpl` declarations at the end.
   - `MCSymbolMSIL` (the wasm `MCSymbolWasm` pattern) is deferred to phase 2, when an object writer needs signatures.
7. **Global data (Design 4.1).** One segment per module, allocated in `<Module>::.cctor`:
   - `AllocHGlobal(size+align-1)`, then align up.
   - `cpblk` from the RVA blob for the initialized prefix, then `initblk` for the zero tail.
   - Store each global's address into its field.
   - Apply relocations with straight-line `stind.i` (`ldftn` for functions).
   - Run `llvm.global_ctors` in priority order.

   **Initializer bytes come from `AsmPrinter::emitGlobalConstant`, captured by a ~60-line `MCStreamer` subclass** that records bytes and turns `MCValue{SymA,SymB,C}` into relocation records. It is installed by temporarily swapping the public `AsmPrinter::OutStreamer` (AsmPrinter.h:110). This reuses LLVM's complete constant lowering, including the new `ConstantByte` at AsmPrinter.cpp:4585, vectors, FP and large ints, with no NVPTX-style walker.
8. **Assembly references for the net8.0 profile (verified):**
   - `System.Object`, `ValueType`, `Environment` and `DisableRuntimeMarshallingAttribute` are in `System.Runtime 8.0.0.0`.
   - `Marshal` and `NativeMemory` are in `System.Runtime.InteropServices 8.0.0.0`.
   - Both have public key token `B0 3F 5F 7F 11 D5 0A 3A`.

   `DisableRuntimeMarshalling` plus a blittable `pinvokeimpl("libc" cdecl) int32 'puts'(native int)` works on .NET 8 and .NET 10 on Windows (`ucrtbase`), and on .NET 10 on Linux (`libc` and `libc.so.6`). In every case the `puts` buffer is flushed when `Main` returns, including when stdout is redirected to a file.
9. **The assembly name must be unique within the application directory. Use the output file stem.** With `dotnet x.dll` and no deps.json, every `*.dll` in the directory goes on the TPA. A second DLL whose *assembly* name collides silently runs instead of the requested file (reproduced: `dbg2.dll` ran `prog.dll`'s code). Use the `-msil-assembly-name` option, defaulting to `stem(source_filename)`. Lit tests that compile one source at `-O0` and `-O2` must use separate directories or separate names.
10. **ilasm invocation for the PE machine type:** `ilasm -dll -pe64 -x64` gives AMD64, PE32+, ILONLY (verified with `ildasm -headers`). Use `-arm64` for AArch64 triples. Omit `.module`; ilasm then names the module after `-output` (verified).

---

## 1. Sources read

| File (this checkout) | What matters |
|---|---|
| `llvm/lib/Target/NVPTX/NVPTXAsmPrinter.cpp` | The class is now inside the .cpp (125-372). `emitInstruction` 566-573, `lowerOperand` 581-626. `emitCallPrototype` 718-762 builds text and calls `emitRawText` (761). **`emitFunctionEntryLabel` 819-859**: emits globals lazily on the first function (`GlobalsEmitted`, 823-826), prints the header with `emitRawText`, then `"{\n"` (854). **`runOnMachineFunction` 861-870** prints `"}\n"` *after* the base, so end-of-function labels stay inside the body. `emitFunctionBodyStart` 872-885. `emitDeclarations` 1095-1155 (forward decls of callees). `doInitialization` 1173-1191 (reserves `getSymbol` for every GV). `emitGlobals` 1193-1252. `doFinalization` 1294-1320. `emitGlobalVariable` is a no-op (371). Byte walker: `AggBuffer` 127-212, `bufferLEByte` 1944-2043, `bufferAggregateConstant` 2045-2116, `lowerConstantForGV` 2193-2318. New-PM begin/pass/end wrappers 2541-2567. |
| `NVPTX/MCTargetDesc/NVPTXMCAsmInfo.cpp` 18-62, `.h` | `CommentString="//"`, `HasSingleParameterDotFile=false`, `HasFunctionAlignment=false`, `HasDotTypeDotSizeDirective=false`, visibility attrs `MCSA_Invalid`, `GlobalDirective="\t// .globl\t"` (a directive turned into a comment), `SupportsQuotedNames=false`, `UseIntegratedAssembler=false`. |
| `NVPTX/MCTargetDesc/NVPTXTargetStreamer.{h,cpp}` | Directive methods write to `formatted_raw_ostream &OS` directly. `changeSection` is overridden to print only DWARF sections. `emitRawBytes`/`emitValue` are overridden. |
| `NVPTX/MCTargetDesc/NVPTXMCTargetDesc.cpp` 63-98 | No MCAsmBackend and no MCCodeEmitter are registered; asm and null target streamers are. |
| `NVPTX/NVPTXMCExpr.{h,cpp}` | Precedent for an `MCTargetExpr` subclass whose `printImpl` prints target syntax (FP immediates). |
| `WebAssembly/WebAssemblyAsmPrinter.{h,cpp}` | `emitGlobalVariable` 192-268. `getOrCreateWasmSymbol` 270-331 (libcall signatures). **`emitDecls` 353-472** (`.functype` for every IR function, imports). `emitEndOfAsmFile` 474-528. `emitConstantPool` 692-696. `emitJumpTableInfo` 698-700 (no-op). **`emitFunctionBodyStart` 702-728** (signature + `.local`). `emitInstruction` 730-799 (ARGUMENT pseudos print nothing). New-PM wrappers 864-892. |
| `WebAssembly/MCTargetDesc/WebAssemblyTargetStreamer.{h,cpp}` | Abstract interface + Asm/Wasm/Null implementations. `.functype name sig` is printed from `MCSymbolWasm::getSignature()`. |
| `WebAssembly/MCTargetDesc/WebAssemblyInstPrinter.{h,cpp}` | `printOperand` prints expressions with `MAI.printExpr` (with a special case for `S_TYPEINDEX`, which prints a signature). FP immediates are printed as hex floats. |
| `WebAssembly/WebAssemblyMCInstLower.cpp` | `GetGlobalAddressSymbol` 71-109 computes the callee signature and attaches it to the `MCSymbolWasm`. `lowerTypeIndexOperand` 165-177. The `call_indirect` signature is built from the MI's register classes at 274-301. `MO_MachineBasicBlock` is `llvm_unreachable` (240-241). |
| `WebAssembly/WebAssemblyMachineFunctionInfo.cpp` 62-112 | `computeSignatureVTs` (results legalized; >1 result → sret pointer param; varargs → trailing pointer). |
| `SPIRV/SPIRVAsmPrinter.cpp` | Overrides `emitFunctionHeader` entirely (194-221, still sets `MF->setSection`). `emitFunctionEntryLabel() {}` (123). `emitGlobalVariable() {}` (129). Rejects module inline asm in `doInitialization` (957-969). Module sections are emitted lazily at the first function header (195-198). |
| `DirectX/DirectXAsmPrinter.cpp` | Not relevant (DXIL container, no text module). |
| `SystemZ/MCTargetDesc/SystemZHLASMAsmStreamer.{h,cpp}`, `SystemZMCTargetDesc.cpp` 205-217, 266-268 | **In-tree precedent for a fully custom text streamer**: `final : public MCAsmBaseStreamer`, registered with `TargetRegistry::RegisterAsmStreamer`. It overrides `changeSection`, `emitLabel`, `emitInstruction`, `emitRawTextImpl`, `emitValueImpl` and `finishImpl`. |
| `llvm/lib/CodeGen/AsmPrinter/AsmPrinter.cpp` | `doInitialization` 532-717 (`initSections` 547, `emitStartOfAsmFile` 566, `.file` 570-588, module asm 612-626, debug handlers only if `MAI.doesSupportDebugInformation()` 628-641, EH 646-705). `emitLinkage` 726-764. `emitGlobalVariable` 793-1000. `emitFunctionHeader` 1035-1173. `emitFunctionEntryLabel` 1177-1189. `needFuncLabels` 1971-1983. `emitFunctionBody` 2071-2592 (BB loop 2125-2424, `func_end` label 2517-2524, `emitJumpTableInfo` 2555). **`doFinalization` 2945-3225** (globals 2974-2975, visibility of declarations 2983-3013, aliases 3082-3096, ifuncs 3101, idents 3155, command lines 3160, stack section 3177, addrsig 3181-3191, `emitEndOfAsmFile` 3214, `finish` 3219). `emitSpecialLLVMGlobal` 3636-3704. `emitModuleIdents` 3794. `emitModuleCommandLines` 3808-3827. `emitGlobalConstantImpl` 4525-4657. `emitGlobalConstant` 4660-4678. `emitBasicBlockStart` 4841-4931. `emitVisibility` 4944-4963. |
| `llvm/include/llvm/CodeGen/AsmPrinter.h` | `OutStreamer` is a **public** `std::unique_ptr<MCStreamer>` (110). `runOnMachineFunction` = `SetupMachineFunction` + `emitFunctionBody` (466-470). `preprocessXXStructorList` 578 (public). `emitXXStructorList` 582. `emitStartOfAsmFile`/`emitEndOfAsmFile` 632/636. `emitFunctionBodyStart`/`End` 640/644. `emitLinkage` 971 (virtual). `getGVAlignment` 974 (public static). `emitFunctionHeader` 987 (private virtual, still overridable). |
| `llvm/lib/MC/MCAsmStreamer.cpp` | `setUseNamesOnTempLabels(true)` in the ctor (~214). `switchSection` 674-687 (target streamer `changeSection`, or `MAI->printSwitchToSection`). `emitLabel` 708-721 (`Symbol->print` + `LabelSuffix`). `emitSymbolAttribute` 867-946 (`.globl` uses `MAI->getGlobalDirective()`; `.hidden` etc. are printed unconditionally). `emitAlignmentDirective` 1609-1677 (`.p2align`). `emitInstruction` 2627-2661. `emitRawTextImpl` 2726-2730. `finishImpl` 2732-2757. |
| `llvm/lib/MC/MCStreamer.cpp` | `MCTargetStreamer::changeSection` 59-64. `emitValue` 70-76. `emitRawBytes` 78-88. `emitIntValue` → `emitBytes` 130-139. `emitZeros` → `emitFill` 202. `initSections` 397-399. `emitLabel` 401-420. `emitRawTextImpl` (fatal by default) 1283-1290. `changeSection` 1524-1535. |
| `llvm/include/llvm/MC/MCAsmInfo.h`, `llvm/lib/MC/MCAsmInfo.cpp` | `InternalSymbolPrefix` 159 (default `"L"`). `SupportsQuotedNames` 202. `HasDotTypeDotSizeDirective` 304. `HasSingleParameterDotFile` 308. `HasIdentDirective` 312. **`printSwitchToSection` is empty by default** (507-508). `isAcceptableChar` (MCAsmInfo.cpp:69-87: `@` only with `AllowAtInName`). `printExpr` 136-141. |
| `llvm/lib/MC/MCSymbol.cpp` 59-84 | `MCSymbol::print` quotes with **double** quotes, which ILAsm does not accept for identifiers. It is fatal if `!supportsNameQuoting()`. |
| `llvm/lib/MC/MCContext.cpp` 365-386, `llvm/lib/CodeGen/MachineBasicBlock.cpp` 62-92 | Block labels are `InternalSymbolPrefix + "BB<fn>_<bb>"`. Temp labels are `InternalSymbolPrefix + name`. |
| `llvm/include/llvm/MC/MCExpr.h` 464-486 | `MCTargetExpr` (`printImpl`, `evaluateAsRelocatableImpl`, `visitUsedExpr`, `findAssociatedFragment`). |
| `llvm/include/llvm/MC/TargetRegistry.h` 201-209, 946-965; `llvm/lib/MC/TargetRegistry.cpp` 101-118 | `RegisterAsmStreamer` / `RegisterAsmTargetStreamer` / `RegisterNullTargetStreamer`. The target streamer ctor receives the `MCInstPrinter*`. |
| `llvm/lib/CodeGen/CodeGenTargetMachineImpl.cpp` 177-197 | `-S` needs only an `MCInstPrinter`; a null `MCAsmBackend` is fine. |
| `llvm/lib/IR/LegacyPassManager.cpp` 1501-1514 | `doInitialization` of every contained pass/FPPassManager runs **before** the first module pass. |
| `llvm/include/llvm/IR/DataLayout.h` 292-325 | Native mangling prefixes (`_` for MachO and i686 Windows; `.L`/`L`/`$` private prefixes). |
| `git show bd24404718a6^:llvm/lib/Target/MSIL/MSILWriter.cpp` (copy in scratch `old_MSILWriter.cpp`) | The 2.x backend always single-quoted names (`getValueName` 230-243). It made each global its own RVA field with structured `.data` (1560-1629), built `argv` with `StringToHGlobalAnsi` (124-221), and printed P/Invoke declarations for all declarations (1631-1680). |

---

## 2. How the existing text backends do it (task question 1)

**NVPTX (closest analogue: a non-GNU text syntax through the stock MCAsmStreamer).**
- It keeps `MCAsmStreamer`. `NVPTXMCAsmInfo` turns off everything GNU-specific; `.globl` and `.weak` become comments through `GlobalDirective`/`WeakDirective`.
- It writes everything that is not an instruction or label as **raw text**: function headers (`emitFunctionEntryLabel`), the open brace, register declarations (`emitFunctionBodyStart` → target streamer), call prototypes and module-level globals (`emitGlobals` → one `emitRawText` of a buffered string).
- It closes the body in `runOnMachineFunction` after the base class, which keeps labels that `emitFunctionBody` adds after `emitFunctionBodyEnd` (e.g. `func_end`) inside the body. MSIL copies this.
- Globals are emitted once, at the first function's entry label, or in `doFinalization` if there are no functions. PTX needs declarations before use; ILAsm does not, so MSIL can put everything at the end (section 0.2).
- It builds its own byte image (`AggBuffer`), with symbol positions recorded for pointer slots. MSIL needs the same "bytes + relocations" output, but can get it from `emitGlobalConstant` (section 7).

**WebAssembly (structural template).**
- Signatures are attached to `MCSymbolWasm` (`setSignature`) whenever a function symbol is touched: at the definition (`emitFunctionBodyStart` 702-728), at call sites (`GetGlobalAddressSymbol`), for libcalls (`getOrCreateWasmSymbol` with the `RuntimeLibcallSignatures` table) and in `emitDecls` (every IR function).
- The asm target streamer prints `.functype name (params) -> (results)` from the symbol. `call_indirect` gets a temp symbol carrying a signature built from the MI's register classes.
- For MSIL, the signature is needed **at the call site text itself** (`call int32 'f'(int32)`), so it must be reachable from the InstPrinter. The proposal carries it in an `MCTargetExpr` (section 6.3) instead of a new `MCSymbol` subclass, which would need core MC changes (`MCContext::createSymbolImpl`, MCContext.cpp:279-311).

**SPIR-V.**
- Overrides `emitFunctionHeader` completely and still calls `MF->setSection(...)`. This is required because `emitFunctionBody` does `OutStreamer->switchSection(MF->getSection())` at AsmPrinter.cpp:2473.
- Makes `emitFunctionEntryLabel` and `emitGlobalVariable` no-ops, and diagnoses module inline asm.
- MSIL keeps the base `emitFunctionHeader` (NVPTX style), but borrows the module-asm rejection.

**SystemZ HLASM.** Shows that a target can replace `MCAsmStreamer` wholesale (`RegisterAsmStreamer`). Kept as the hardening option (3.3).

---

## 3. What the base AsmPrinter emits, and how MSIL suppresses it (task question 4)

### 3.1 Inventory (checked against AsmPrinter.cpp / MCAsmStreamer.cpp)

| Source of output | Would print | MSIL neutralization |
|---|---|---|
| `doInitialization` → `initSections` (547) | nothing (`switchSectionNoPrint`) | Needs a non-null `getTextSection()`: `IsMSIL` + `MCSectionMSIL` per `triple-objfmt.md`. |
| `.file` (570-588) | `.file "x.c"` | `HasSingleParameterDotFile = false`. |
| Module inline asm (612-626) | raw asm | Diagnose in `MSILAsmPrinter::doInitialization` (SPIR-V pattern); clang also rejects it (Design 3). |
| Debug/CodeView handlers (628-641) | `.loc`, `.cfi_sections`, DWARF sections | `SupportsDebugInformation = false`. With `-g`, `needFuncLabels` (1971-1983) still makes `emitFunctionBody` emit `@func_end0:` (2517-2524); it lands inside the method because `}` is printed after the base `runOnMachineFunction`. Verified OK (e2, e5). |
| EH (646-705) | `.cfi_*` | `ExceptionsType = None` (default) and `Triple::getDefaultExceptionHandling() == None` for MSIL (`triple-objfmt.md` 0.4). |
| Section switches (`switchSection`, MCAsmStreamer.cpp:674-687) | `.text`, `.data` … | The base `MCAsmInfo::printSwitchToSection` is empty (MCAsmInfo.h:507-508). `MSILTargetStreamer` does **not** override `changeSection`, because the default forwards to that empty function (MCStreamer.cpp:59-64). |
| `emitLinkage` (726-764) → `.globl`/`.weak` | `.globl foo` | Override `emitLinkage` as a no-op (it is virtual, AsmPrinter.h:971). Belt and braces: `GlobalDirective = "\t// .globl\t"`, `WeakDirective = "\t// .weak\t"` (NVPTX trick). |
| `emitVisibility` (4944) → `.hidden`/`.protected` | `.hidden foo` | `HiddenVisibilityAttr = HiddenDeclarationVisibilityAttr = ProtectedVisibilityAttr = ExportedVisibilityAttr = MCSA_Invalid`. |
| Function alignment (1061-1080) | `.p2align` | `HasFunctionAlignment = false`. |
| `.type`/`.size` (1082-1083, 2513-2537, 854, 994) | `.type f,@function`, `.size` | `HasDotTypeDotSizeDirective = false`. |
| Function entry label (1177-1189) | `f:` | Override `emitFunctionEntryLabel` to print the `.method` header instead. |
| Prefix/prologue data, `!func_sanitize` (1089-1119, 1171-1172) | `.byte`… | Diagnose and skip in `runOnMachineFunction` before calling the base. |
| Constant pool (1044) | `.section .rodata.cst8` + data | `emitConstantPool` override asserts empty (FP immediates are legal, `isel.md` 5.7). |
| MBB alignment (4867-4870) | `.p2align` | The default `PrefLoopAlignment` is `Align(1)`; do not call `setPrefLoopAlignment`. |
| MBB labels (4905-4915) | `@BB0_1:` / `// %bb.1:` | Valid ILAsm (`InternalSymbolPrefix = "@"`, `AllowAtInName = true`). |
| Jump tables (2555) | `.LJTI0_0:` + `.quad` | `emitJumpTableInfo` override is a no-op; `BR_TABLE` prints `switch (…)` + `br` (`isel.md` 11.2). |
| `emitGlobalVariable` (doFinalization 2974) | sections, labels, `.byte`/`.quad`/`.comm` | Overridden: captures bytes into the data segment (section 7). |
| Aliases / ifuncs (3082-3102) | `.set`, ifunc stubs | `emitGlobalAlias`/`emitGlobalIFunc` overrides diagnose "not supported" (M1). |
| `.ident` (3155) | `.ident "clang…"` | `HasIdentDirective = false` (default). |
| `llvm.commandline` (3160 → 3808) | section + bytes | `MSILTargetObjectFile::getSectionForCommandLines()` returns `nullptr` (default). |
| Stack section (3177) | `.section .note.GNU-stack` | `MCAsmInfo::getStackSection` returns `nullptr` by default (only `MCAsmInfoELF` overrides it). |
| `.addrsig` (3181-3191) | `.addrsig` (MCAsmStreamer prints it unconditionally) | The driver does not pass `-faddrsig` for MSIL (`clang.md`, Clang.cpp:8573-8580). Optionally force `TM.Options.EmitAddrsig = false` in `MSILTargetMachine`. |
| Weak refs (3127-3150) | `.weak_reference` | `WeakRefDirective = nullptr` (default). |
| `MCAsmStreamer::finishImpl` (2732-2757) | DWARF line label only with debug tables | None with debug off. |

### 3.2 `MSILMCAsmInfo` (proposed, complete)

```cpp
// MCTargetDesc/MSILMCAsmInfo.h
class MSILMCAsmInfo final : public MCAsmInfo {
public:
  explicit MSILMCAsmInfo(const Triple &TT, const MCTargetOptions &Options);
  ~MSILMCAsmInfo() override;
};

// MCTargetDesc/MSILMCAsmInfo.cpp
MSILMCAsmInfo::MSILMCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfo(Options) {
  CodePointerSize = CalleeSaveStackSlotSize = TT.isArch64Bit() ? 8 : 4;
  CommentString = "//";              // ILAsm supports // and /* */
  InternalSymbolPrefix = "@";        // @BB0_1, @func_end0: legal ILAsm IDs, never C identifiers
  AllowAtInName = true;              // so MCSymbol::print prints @BB0_1 unquoted
  SupportsQuotedNames = false;       // MCSymbol::print would emit "..." (invalid ILAsm): fail loudly
  HasSingleParameterDotFile = false; // no .file
  HasIdentDirective = false;         // no .ident (default)
  HasDotTypeDotSizeDirective = false;
  HasFunctionAlignment = false;
  HiddenVisibilityAttr = HiddenDeclarationVisibilityAttr = MCSA_Invalid;
  ProtectedVisibilityAttr = ExportedVisibilityAttr = MCSA_Invalid;
  GlobalDirective = "\t// .globl\t"; // never reached (emitLinkage is a no-op); harmless if it is
  WeakDirective = "\t// .weak\t";
  SupportsDebugInformation = false;  // M1; Portable PDB later (Design 10.7)
  ExceptionsType = ExceptionHandling::None;
  UseIntegratedAssembler = false;    // no object output in M1 (cf. NVPTX)
  // printSwitchToSection: inherit the empty base implementation (MCAsmInfo.h:507-508).
}
```

### 3.3 Hardening option: a custom streamer

If reviewers prefer a hard guarantee that nothing GNU-like reaches the `.il`, add `MSILAsmStreamer final : public MCAsmBaseStreamer` and register it with `TargetRegistry::RegisterAsmStreamer` (SystemZ: SystemZMCTargetDesc.cpp:205-217, 266-268).
- It prints only `emitInstruction`, `emitRawTextImpl`, comments and `emitLabel` (inside a method).
- It returns `true` from `emitSymbolAttribute` and makes `changeSection`, alignment and `emitAddrsig` no-ops.
- It calls `report_fatal_error` from `emitBytes`/`emitValueImpl`/`emitFill`/`emitCommonSymbol`.
- It must call `Context.setUseNamesOnTempLabels(true)`, as MCAsmStreamer.cpp:~214 and SystemZHLASMAsmStreamer.h:58 do; otherwise temp labels are unnamed.

This costs about 250 lines and must follow `MCStreamer` API churn. Not needed for M1, given the table above.

---

## 4. Structure of the emitted `.il` (task question 2)

Section 9 is the canonical verified example. The order, with its producers:

| # | Text | Producer |
|---|---|---|
| 0 | UTF-8 BOM, only if any global name is non-ASCII (`llvm::isASCII(StringRef)`, StringExtras.h:128) | `emitStartOfAsmFile` |
| 1 | `// source_filename`, `// target triple` comments | `emitStartOfAsmFile` |
| 2 | `.assembly extern System.Runtime {…}` and `.assembly extern System.Runtime.InteropServices {…}` (net8.0). A future net48 profile uses `.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }`. The fixed set is emitted unconditionally; ilasm needs an `.assembly extern` before the first `[name]` reference. | `emitStartOfAsmFile` |
| 3 | `.assembly 'name' { .custom … DisableRuntimeMarshallingAttribute::.ctor() = (01 00 00 00) .ver 0:0:0:0 }`. The attribute is emitted for net7+ profiles only (Design 5.3). No `.module`. | `emitStartOfAsmFile` |
| 4 | One `.method … { .maxstack N .locals (…) <body> }` per defined function, in IR order | per function (section 5) |
| 5 | `pinvokeimpl` declarations for every referenced undefined function | `emitEndOfAsmFile` |
| 6 | `.class private explicit ansi sealed '<msil>image_t' extends [System.Runtime]System.ValueType { .pack 1 .size <InitSize> }`, `.data cil '<msil>image_d' = bytearray ( … )`, `.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'`. Omitted when `InitSize == 0`. | `emitEndOfAsmFile` |
| 7 | `.field private static initonly native int '<msil>data'` (segment base) and one `.field public\|assembly static initonly native int '<name>'` per defined global | `emitEndOfAsmFile` |
| 8 | `.method private hidebysig specialname rtspecialname static void .cctor() cil managed { … }`. Emitted when there is data or there are ctors. | `emitEndOfAsmFile` |
| 9 | `.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed { .entrypoint … }`, when `main` is defined | `emitEndOfAsmFile` |

**Method headers.**
- `.method <access> static <ret> '<name>'(<p0>, <p1>, …) cil managed`.
  - `<access>` is `public` for non-local linkage and `assembly` for internal/private linkage.
  - Parameters are unnamed (`ldarg.N` addressing); ilasm accepts that (verified).
  - Types come from `MSIL::Signature` (`isel.md` 8.1): `int32`, `native int` (every 64-bit value on LP64/LLP64, `isel.md` 0.1), `float32`, `float64`. `void` if there is no result.
- Optional impl flags: `noinlining` for `noinline` at -O2, `aggressiveinlining` for `alwaysinline`. Note that clang marks everything `noinline` at -O0. Not required in M1.
- Body prologue: `.maxstack N` from `MSILFunctionInfo` (computed by `MSILStackCheck`, `passes-frame.md` 0.7), then `.locals (t0, t1, …)` **without `init`** (`passes-frame.md` 0.4). Omit `.locals` when there are none. Verified forms: `.maxstack 4` / `.locals (int32, int32)`.

**Where functions live.** They are global methods of `<Module>` for M1. Calls are unqualified (`call int32 'sum'(native int, int32)`) and bind to MethodDefs (verified, ildasm shows no MemberRefs). Phase 2's export class (Design 5.4) can either move the methods into a named class or add `public static` forwarders. The member-ref expression carries an optional owner type (section 6.3), so that change stays inside the printer.

**Identifiers (verified rules; implement `MSIL::printQuotedName`).**

```cpp
// MCTargetDesc/MSILMCTypeUtilities.cpp
void llvm::MSIL::printQuotedName(raw_ostream &OS, StringRef Name) {
  OS << '\'';
  for (unsigned char C : Name) {
    if (C == '\'')      OS << "\\'";
    else if (C == '\\') OS << "\\134";           // "\\\\" is dropped by ilasm (e6)
    else if (C < 0x20 || C == 0x7f)               // \t, \n, \001 all verified as octal
      OS << '\\' << char('0' + (C >> 6)) << char('0' + ((C >> 3) & 7)) << char('0' + (C & 7));
    else OS << C;                                  // >= 0x80: raw UTF-8, file needs a BOM
  }
  OS << '\'';
}
StringRef llvm::MSIL::getILName(const GlobalValue &GV, const TargetMachine &TM) {
  if (GV.hasName())
    return GlobalValue::dropLLVMManglingEscape(GV.getName()); // NOT getSymbol(): no '_' (m:o, m:x)
  return TM.getSymbol(&GV)->getName();                        // unnamed globals: __unnamed_N
}
```
- Use the IR name, not the mangled `MCSymbol`. Darwin (`m:o`) and i686 Windows (`m:x`) would otherwise get `_main`/`_puts`, which breaks both C# visibility and P/Invoke entry points (`triple-objfmt.md` 3b).
- Compiler-generated names use the `<msil>` prefix, which C cannot spell. Do not put `.` in generated *type* names: ilasm splits `'<msil>.data.t'` into namespace `'<msil>.data'` + name `t` (seen in the ildasm round trip).

**Instruction text** (what `MSILInstPrinter` emits; the opcode set belongs to `isel.md`):

| Kind | Text | Verified |
|---|---|---|
| Labels | `@BB1_2:` on its own line; operands print the MBB symbol name | yes (`blt`, `brtrue`, `switch`) |
| `switch` | `switch (@BB0_1, @BB0_2, @BB0_3)` then `br @BB0_1` | yes |
| Direct call | `call int32 'sum'(native int, int32)` (P/Invoke targets use the same syntax) | yes |
| Function address | `ldftn int32 'sum'(native int, int32)` | yes |
| Indirect call | `calli int32(native int, int32)` (managed, M1) | yes |
| Global address | `ldsfld native int 'table'` (+ `ldc.i4 off` / `add` if the folded offset ≠ 0) | yes |
| FP constants | `ldc.r8 float64(0x3FF8000000000000)`, `ldc.r4 float32(0x7FC00000)` (bit-exact for NaN payloads, −0, ±inf). `ldc.r8 (00 00 00 00 00 00 F0 7F)` also works. In verbose mode, append the decimal value as a `//` comment. | yes |
| 64-bit constants | `ldc.i8 0x8000000000000000`, `ldc.i4 -2147483648` | yes |
| Short forms | The printer picks `ldarg.0-3`, `ldarg.s`, `ldloc.0-3`, `ldloc.s`, `stloc.*`, `ldc.i4.m1`/`.0-.8`/`.s` from the index/immediate. ilasm does not shorten without `/OPTIMIZE`. | n/a |

---

## 5. Where each piece is produced (task question 3)

### 5.1 Hook map

| Hook (AsmPrinter.h line) | MSIL implementation |
|---|---|
| `doInitialization` (459) | Call the base first. Then diagnose `M.hasModuleInlineAsm()`, and set `Profile` (net8.0 / net48 from `-mdotnet=`; `registry-tm.md`). **Do not compute layout or signatures here**: the legacy PM runs this before IR codegen passes (LegacyPassManager.cpp:1501-1514). |
| `emitStartOfAsmFile` (632) | BOM?, header comments, `.assembly extern`, `.assembly`. Depends only on the triple, the profile, global *names* and the assembly name. |
| `runOnMachineFunction` (466, override) | Cache `Subtarget`/`MFI`. Reject prefix/prologue data. Call `AsmPrinter::runOnMachineFunction`, then `TS.emitMethodEnd()` (`}`) **after** the base, as NVPTX does at 861-870. |
| `emitFunctionHeader` (987) | **Keep the base.** It sets the section (required by AsmPrinter.cpp:2473) and runs handler hooks; everything it prints is neutralized (section 3). |
| `emitFunctionEntryLabel` (663) | `TS.emitMethodBegin({access, Signature, ILName, implflags})` prints `.method … cil managed` and `{`. Assert that `getSignature(F)` equals `MFI->getParams()/getResults()`. |
| `emitFunctionBodyStart` (640) | `TS.emitMaxStack(MFI->getMaxStack())`; `TS.emitLocals(MFI->getLocals())`. |
| `emitBasicBlockStart` (650) | Base (labels through `MCAsmStreamer::emitLabel`). |
| `emitInstruction` (656) | Like wasm 730-799: ARGUMENT/fence pseudos print nothing; otherwise `MSILMCInstLower::lower` → `EmitToStreamer`. |
| `emitFunctionBodyEnd` (644) | Nothing; `}` is in `runOnMachineFunction`. |
| `emitLinkage` (971) | No-op. |
| `emitConstantPool` (541) / `emitJumpTableInfo` (545) | assert-empty / no-op. |
| `emitGlobalVariable` (548) | Data capture (section 7). Handles the `llvm.*` specials itself. |
| `emitGlobalAlias` (1038) / `emitGlobalIFunc` (1029) | Diagnose (M1). |
| `emitEndOfAsmFile` (636) | P/Invoke declarations, data image, address fields, `.cctor`, `Main`. |
| New PM | `MSILAsmPrinterBeginPass`/`Pass`/`EndPass`, copied from WebAssemblyAsmPrinter.cpp:864-892. |

### 5.2 How non-MC text is emitted

All ILAsm directives go through `MSILTargetStreamer` (abstract, plus `MSILTargetAsmStreamer` and `MSILTargetNullStreamer`, wasm's three-way split). The asm implementation formats into a `SmallString` and calls **`getStreamer().emitRawText(...)`**, not the raw `OS`. That way any pending `AddComment`/`getCommentOS()` text is attached to the same line by `MCAsmStreamer::EmitEOL` instead of appearing one line late (`emitRawTextImpl`, MCAsmStreamer.cpp:2726-2730). A null target streamer must be registered for `-filetype=null`; otherwise `getTargetStreamer()` is null.

### 5.3 Signatures: where the table lives

**Why it is needed.** ILAsm needs a full signature at every `call`/`ldftn`/`calli`, on every method definition, and on every P/Invoke declaration. A mismatch between a call site and its definition means `MissingMethodException` at JIT time, so all of them must come from one function.

**Where it lives.** `MSILAsmPrinter` owns the table. In M1 only the printer, including `MSILMCInstLower`, consumes signatures; ISel computes its own per call (`isel.md` 8.2).

```cpp
// MSILAsmPrinter.h (member state)
SpecificBumpPtrAllocator<MSIL::Signature> SigAlloc;               // outlives every MCInst we print
DenseMap<const Function *, const MSIL::Signature *> FuncSigs;      // definitions, decls, ldftn targets
StringMap<const MSIL::Signature *> ExtSymSigs;                     // libcall/ExternalSymbol callees
std::set<MSIL::Signature> InternedCalliSigs;                       // (or FoldingSet) for calli
MapVector<const Function *, bool> ExternFuncs;                     // undefined callees -> P/Invoke decls

const MSIL::Signature &MSILAsmPrinter::getSignature(const Function &F) {
  auto [It, New] = FuncSigs.try_emplace(&F, nullptr);
  if (New) {
    SmallVector<MVT, 4> Params, Results;
    MSIL::computeSignatureVTs(F.getFunctionType(), &F, F, TM, Params, Results); // shared with ISel
    auto *S = new (SigAlloc.Allocate()) MSIL::Signature;
    for (MVT VT : Results) S->Results.push_back(MSIL::toValType(VT, PointerBits));
    for (MVT VT : Params)  S->Params.push_back(MSIL::toValType(VT, PointerBits));
    It->second = S;
  }
  return *It->second;
}
```

Sources of entries, matching `isel.md` 8.2:
- **Definitions**: `getSignature(F)`, asserted equal to `MFI` params/results.
- **Direct calls and `ldftn`**: `getSignature(*cast<Function>(MO.getGlobal()))`.
- **`calli`**: built from the MI. Defs become Results; explicit register uses minus the trailing callee become Params; the `cc` immediate becomes `CallConv`. The result is interned.
- **ExternalSymbol callees**: from the MI. Record `name → sig`, assert agreement across call sites, and emit a declaration at the end. No such callee is reachable in M1 (`isel.md` 7).
- **P/Invoke declarations**: `getSignature(F)` for each `F` in `ExternFuncs`. That set is filled by `MSILMCInstLower` when a call or `ldftn` names a declaration, and by the data collector when a relocation targets one.

**Phase 2.** Move the pointer onto an `MCSymbolMSIL` (`MCContext::createSymbolImpl`, MCContext.cpp:292-310) so the object writer can encode MemberRef/StandAloneSig tokens. ISel and the printer API do not change.

---

## 6. C++ sketches

### 6.1 Registration (`MCTargetDesc/MSILMCTargetDesc.cpp`)

```cpp
static MCAsmInfo *createMSILMCAsmInfo(const MCRegisterInfo &, const Triple &TT,
                                      const MCTargetOptions &Options) {
  return new MSILMCAsmInfo(TT, Options);
}
static MCInstPrinter *createMSILMCInstPrinter(const Triple &, unsigned SyntaxVariant,
                                              const MCAsmInfo &MAI, const MCInstrInfo &MII,
                                              const MCRegisterInfo &MRI) {
  return SyntaxVariant == 0 ? new MSILInstPrinter(MAI, MII, MRI) : nullptr;
}
static MCTargetStreamer *createMSILAsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &,
                                                     MCInstPrinter *) {
  return new MSILTargetAsmStreamer(S);
}
static MCTargetStreamer *createMSILNullTargetStreamer(MCStreamer &S) {
  return new MSILTargetNullStreamer(S);
}
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMSILTargetMC() {
  Target &T = getTheMSILTarget();
  RegisterMCAsmInfoFn X(T, createMSILMCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createMSILMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createMSILMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createMSILMCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createMSILMCInstPrinter);
  TargetRegistry::RegisterAsmTargetStreamer(T, createMSILAsmTargetStreamer);
  TargetRegistry::RegisterNullTargetStreamer(T, createMSILNullTargetStreamer);
  // No MCCodeEmitter/MCAsmBackend in M1: -filetype=obj fails cleanly (CodeGenTargetMachineImpl.cpp:201-209).
}
```

### 6.2 Target streamer (`MCTargetDesc/MSILTargetStreamer.{h,cpp}`)

```cpp
struct MSILMethodDecl {
  StringRef Name;                    // IL name (unquoted; the streamer quotes)
  const MSIL::Signature *Sig;
  bool Public = true;
  bool NoInlining = false, AggressiveInlining = false;
};

class MSILTargetStreamer : public MCTargetStreamer {
public:
  explicit MSILTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}
  virtual void emitFileHeader(bool BOM, StringRef SourceFile, StringRef Triple) = 0;
  virtual void emitAssemblyRefs(MSIL::Profile P) = 0;
  virtual void emitAssembly(StringRef Name, MSIL::Profile P) = 0;
  virtual void emitMethodBegin(const MSILMethodDecl &M) = 0;     // .method ... {
  virtual void emitEntryPoint() = 0;                             // .entrypoint
  virtual void emitMaxStack(unsigned N) = 0;                     // .maxstack N
  virtual void emitLocals(ArrayRef<MSIL::ValType> L, bool Init) = 0;
  virtual void emitMethodEnd() = 0;                              // }
  virtual void emitPInvoke(const MSILMethodDecl &M, StringRef Lib, StringRef Entry,
                           MSIL::CallConv CC) = 0;
  virtual void emitDataImage(StringRef TypeName, StringRef FieldName, StringRef Label,
                             ArrayRef<uint8_t> Bytes) = 0;
  virtual void emitAddressField(StringRef Name, bool Public) = 0;
  virtual void emitILLine(const Twine &Line) = 0;                // synthesized .cctor/Main bodies (M1)
};

class MSILTargetAsmStreamer final : public MSILTargetStreamer {
  void line(const Twine &T) { getStreamer().emitRawText(T); }
public:
  using MSILTargetStreamer::MSILTargetStreamer;
  void emitMethodBegin(const MSILMethodDecl &M) override {
    SmallString<128> S; raw_svector_ostream OS(S);
    OS << ".method " << (M.Public ? "public" : "assembly") << " static ";
    MSIL::printResult(OS, *M.Sig);           // "void" | "int32" | "native int" | ...
    OS << ' ';
    MSIL::printQuotedName(OS, M.Name);
    MSIL::printParamList(OS, *M.Sig);        // "(native int, int32)"
    OS << " cil managed";
    if (M.NoInlining) OS << " noinlining";
    if (M.AggressiveInlining) OS << " aggressiveinlining";
    OS << "\n{";
    line(OS.str());
  }
  void emitMaxStack(unsigned N) override { line("  .maxstack " + Twine(N)); }
  void emitLocals(ArrayRef<MSIL::ValType> L, bool Init) override {
    if (L.empty()) return;
    SmallString<64> S; raw_svector_ostream OS(S);
    OS << "  .locals " << (Init ? "init " : "") << '(';
    interleaveComma(L, OS, [&](MSIL::ValType T) { MSIL::printValType(OS, T); });
    OS << ')';
    line(OS.str());
  }
  void emitMethodEnd() override { line("}"); }
  void emitPInvoke(const MSILMethodDecl &M, StringRef Lib, StringRef Entry,
                   MSIL::CallConv) override {
    SmallString<128> S; raw_svector_ostream OS(S);
    OS << ".method public hidebysig static pinvokeimpl(\"" << Lib << '"';
    if (Entry != M.Name) OS << " as \"" << Entry << '"';
    OS << " cdecl) ";                                    // ignored on 64-bit; stdcall later (Design 3.1)
    MSIL::printResult(OS, *M.Sig); OS << ' ';
    MSIL::printQuotedName(OS, M.Name); MSIL::printParamList(OS, *M.Sig);
    OS << " cil managed preservesig {}";
    line(OS.str());
  }
  void emitDataImage(StringRef Ty, StringRef Fld, StringRef Lbl, ArrayRef<uint8_t> B) override {
    // .class private explicit ansi sealed 'Ty' extends [System.Runtime]System.ValueType { .pack 1 .size N }
    // .data cil 'Lbl' = bytearray ( 16 bytes per line ... )
    // .field private static valuetype 'Ty' 'Fld' at 'Lbl'
  }
  // emitFileHeader / emitAssemblyRefs / emitAssembly / emitAddressField / emitEntryPoint /
  // emitILLine: straightforward line() calls; exact text in section 9.
};
```

### 6.3 Member-reference expression (`MCTargetDesc/MSILMCExpr.{h,cpp}`)

```cpp
// Carries everything the InstPrinter needs to print a CIL token operand.
// MCExprs are bump-allocated and never destroyed: keep the members trivially destructible.
class MSILMemberRefExpr final : public MCTargetExpr {
public:
  enum Kind : uint8_t { Method, Field, StandAloneSig, BCLMember };
private:
  Kind K; StringRef Name; const MSIL::Signature *Sig; int64_t Offset; StringRef Owner;
  MSILMemberRefExpr(Kind K, StringRef N, const MSIL::Signature *S, int64_t Off, StringRef Own)
      : K(K), Name(N), Sig(S), Offset(Off), Owner(Own) {}
public:
  static const MSILMemberRefExpr *createMethod(StringRef ILName, const MSIL::Signature *S,
                                               MCContext &Ctx) {
    return new (Ctx) MSILMemberRefExpr(Method, Ctx.allocateString(ILName), S, 0, {});
  }
  static const MSILMemberRefExpr *createField(StringRef ILName, int64_t Off, MCContext &Ctx);
  static const MSILMemberRefExpr *createSig(const MSIL::Signature *S, MCContext &Ctx);
  // BCLMember: Name holds the preformatted "[System.Runtime]System.Math::Sqrt(float64)" text.

  void printImpl(raw_ostream &OS, const MCAsmInfo *) const override {
    switch (K) {
    case Method:                       // int32 'sum'(native int, int32)
      MSIL::printResult(OS, *Sig); OS << ' ';
      if (!Owner.empty()) { MSIL::printQuotedName(OS, Owner); OS << "::"; } // phase 2 export class
      MSIL::printQuotedName(OS, Name); MSIL::printParamList(OS, *Sig);
      return;
    case Field:                        // native int 'table'   (offset printed by the GLOBAL_ADDR case)
      OS << "native int "; MSIL::printQuotedName(OS, Name); return;
    case StandAloneSig:                // int32(native int, int32)
      MSIL::printResult(OS, *Sig); MSIL::printParamList(OS, *Sig); return;
    case BCLMember: OS << Name; return;
    }
  }
  bool evaluateAsRelocatableImpl(MCValue &, const MCAssembler *) const override { return false; }
  void visitUsedExpr(MCStreamer &) const override {}
  MCFragment *findAssociatedFragment() const override { return nullptr; }
  Kind getKind() const { return K; }
  int64_t getOffset() const { return Offset; }
};
```

`MSILInstPrinter::printOperand` stays generic: `MAI.printExpr(O, *Op.getExpr())` → `MCExpr::print` → `printImpl` for `MCExpr::Target` (MCExpr.cpp:58-59). MBB operands are plain `MCSymbolRefExpr`s and print `@BB1_2`. The `GLOBAL_ADDR` macro case in `printInst` appends `\n\tldc.i4\t<off>\n\tadd` when `getOffset() != 0`, using `ldc.i8 <off>; conv.i` if the offset is not `isInt<32>`. That is `isel.md`'s `StackPeakExtra = 1`.

### 6.4 MCInstLower operand cases (`MSILMCInstLower.cpp`)

```cpp
case MachineOperand::MO_GlobalAddress: {
  const GlobalValue *GV = MO.getGlobal();
  if (const auto *F = dyn_cast<Function>(GV)) {                       // CALL / FUNC_ADDR (ldftn)
    if (MO.getOffset()) report_fatal_error("function address with offset");
    if (F->isDeclaration() && !F->isIntrinsic()) Printer.noteExternFunction(*F);
    MCOp = MCOperand::createExpr(MSILMemberRefExpr::createMethod(
        MSIL::getILName(*F, Printer.TM), &Printer.getSignature(*F), Ctx));
  } else {                                                           // GLOBAL_ADDR
    MCOp = MCOperand::createExpr(MSILMemberRefExpr::createField(
        MSIL::getILName(*GV, Printer.TM), MO.getOffset(), Ctx));
  }
  break;
}
case MachineOperand::MO_MachineBasicBlock:                           // wasm makes this unreachable
  MCOp = MCOperand::createExpr(MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
  break;
case MachineOperand::MO_ExternalSymbol: {                            // libcall callee
  const MSIL::Signature &S = Printer.noteExternalSymbol(MO.getSymbolName(), sigFromMI(*MI));
  MCOp = MCOperand::createExpr(MSILMemberRefExpr::createMethod(MO.getSymbolName(), &S, Ctx));
  break;
}
// CALL_INDIRECT: the typeindex placeholder operand becomes createSig(&Printer.intern(sigFromMI(*MI)))
// (wasm MCInstLower.cpp:274-301; drop the trailing callee operand from Params).
```

### 6.5 AsmPrinter core (`MSILAsmPrinter.cpp`)

```cpp
bool MSILAsmPrinter::doInitialization(Module &M) {
  if (M.hasModuleInlineAsm())
    M.getContext().emitError("module-level inline assembly is not supported for MSIL");
  return AsmPrinter::doInitialization(M);   // calls emitStartOfAsmFile
}

void MSILAsmPrinter::emitStartOfAsmFile(Module &M) {
  bool BOM = any_of(M.global_values(), [](const GlobalValue &GV) { return !isASCII(GV.getName()); });
  auto &TS = getTargetStreamer();
  TS.emitFileHeader(BOM, M.getSourceFileName(), TM.getTargetTriple().str());
  TS.emitAssemblyRefs(Profile);
  TS.emitAssembly(getAssemblyName(M), Profile);   // cl::opt msil-assembly-name, default stem(source)
}

bool MSILAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<MSILSubtarget>();
  MFI = MF.getInfo<MSILFunctionInfo>();
  const Function &F = MF.getFunction();
  if (F.hasPrefixData() || F.hasPrologueData() || F.getMetadata(LLVMContext::MD_func_sanitize)) {
    F.getContext().diagnose(DiagnosticInfoUnsupported(F, "prefix/prologue data"));
    return false;
  }
  AsmPrinter::runOnMachineFunction(MF);       // header, entry label, body, @func_end (if -g)
  getTargetStreamer().emitMethodEnd();        // "}" after everything (NVPTXAsmPrinter.cpp:861-870)
  return false;
}

void MSILAsmPrinter::emitFunctionEntryLabel() {
  const Function &F = MF->getFunction();
  const MSIL::Signature &Sig = getSignature(F);
  assert(Sig.Params == MFI->getParamValTypes() && Sig.Results == MFI->getResultValTypes() &&
         "call-site and definition signatures diverged");
  getTargetStreamer().emitMethodBegin({MSIL::getILName(F, TM), &Sig, !F.hasLocalLinkage()});
}

void MSILAsmPrinter::emitFunctionBodyStart() {
  auto &TS = getTargetStreamer();
  TS.emitMaxStack(MFI->getMaxStack());
  TS.emitLocals(MFI->getLocalValTypes(), /*Init=*/false);
}

void MSILAsmPrinter::emitEndOfAsmFile(Module &M) {
  auto &TS = getTargetStreamer();
  for (const Function *F : ExternFuncs.keys()) {
    if (F->isVarArg()) {   // Design 4.7: needs v-variant / fixed-arity handling (phase 3)
      F->getContext().diagnose(DiagnosticInfoUnsupported(*F, "call to native variadic function"));
      continue;
    }
    TS.emitPInvoke({MSIL::getILName(*F, TM), &getSignature(*F)},
                   getImportModule(*F),   // fn attr "msil-import-module", else per-triple libc
                   getImportName(*F),     // fn attr "msil-import-name", else IL name
                   MSIL::CallConv::UnmanagedCdecl);
  }
  Data.finalize();                        // place zero-initialized globals after the image
  if (!Data.Image.empty())
    TS.emitDataImage("<msil>image_t", "<msil>image", "<msil>image_d", Data.Image);
  if (Data.TotalSize) TS.emitAddressField("<msil>data", /*Public=*/false);
  for (const auto &E : Data.Entries)
    TS.emitAddressField(MSIL::getILName(*E.GV, TM), !E.GV->hasLocalLinkage());
  if (Data.TotalSize || !Ctors.empty()) emitModuleInitializer();
  if (const Function *Main = M.getFunction("main"); Main && !Main->isDeclaration())
    emitEntryPoint(*Main);
}
```

Default import module for M1: `x86_64-*-linux-gnu-msil` → `"libc"`, `*-windows-msvc-msil` → `"ucrtbase"`. Both were verified, and `"libc.so.6"` was verified on glibc. Design 5.3 later takes `DT_SONAME`. Any undefined function, not only `puts`, becomes a P/Invoke into that library. This costs nothing extra over the hard-coded `puts` and also makes `-ffreestanding` `memcpy`/`strlen` calls work.

---

## 7. Global data (task question 5)

### 7.1 Recommendation for M1: capture `emitGlobalConstant` into bytes + relocation records

The options considered:

| Option | Fidelity | Code | Verdict |
|---|---|---|---|
| **Own walker** (NVPTX `AggBuffer`/`bufferLEByte`, 1944-2116) | Must replicate struct padding, vectors, i1/odd ints, FP formats, `ConstantDataSequential`, the new `ConstantByte` (AsmPrinter.cpp:4585), and `lowerConstant`'s expression folding | 250-400 lines | Fallback. |
| **Capture**: swap `OutStreamer` for a collector `MCStreamer` around `AsmPrinter::emitGlobalConstant(DL, Init)` | Exact: the code native targets use. Symbol slots arrive as `emitValue(MCExpr)`, already folded by `lowerConstant` into Sym+Off / SymA−SymB+Off | ~100 lines | **Recommended.** `OutStreamer` is a public `unique_ptr` (AsmPrinter.h:110). `emitGlobalConstantImpl` calls only `emitZeros`/`emitIntValue`/`emitBytes`/`emitValue`/`getCommentOS` on it (4525-4657). |
| Let the base `emitGlobalVariable` run into an "object-streamer-lite" (sections, labels, `.comm`) | Exact, and also handles `.comm`/BSS paths | Needs real BSS/data sections in the TLOF and a stateful streamer | Phase 2 shape (`MCMSILStreamer`); too much for M1. |

```cpp
// MSILDataSegment.h
struct MSILReloc { uint64_t Site; const GlobalValue *Target; int64_t Addend; uint8_t Size; };
struct MSILDiff  { uint64_t Site; const GlobalVariable *A, *B; int64_t Addend; uint8_t Size; };
struct MSILDataSegment {
  struct Entry { const GlobalVariable *GV; uint64_t Offset, Size; };
  SmallVector<uint8_t, 0> Image;                 // initialized prefix [0, InitSize)
  SmallVector<Entry, 16> Entries;                // all defined globals, final offsets
  SmallVector<std::tuple<const GlobalVariable *, uint64_t, Align>, 8> Zero;  // laid out by finalize()
  std::vector<MSILReloc> Relocs; std::vector<MSILDiff> Diffs;
  Align MaxAlign = Align(16);                    // >= malloc alignment
  uint64_t InitSize = 0, TotalSize = 0;
  void finalize();                               // place Zero after Image, fold Diffs into Image bytes
};

// The collector: an MCStreamer that appends to Image.
class MSILDataCollector final : public MCStreamer {
  MSILDataSegment &Seg;
  const DenseMap<const MCSymbol *, const GlobalValue *> &SymToGV;
  const GlobalVariable &Cur;
public:
  MSILDataCollector(MCContext &Ctx, MSILDataSegment &S,
                    const DenseMap<const MCSymbol *, const GlobalValue *> &M,
                    const GlobalVariable &GV)
      : MCStreamer(Ctx), Seg(S), SymToGV(M), Cur(GV) {}
  bool emitSymbolAttribute(MCSymbol *, MCSymbolAttr) override { return true; }
  void emitCommonSymbol(MCSymbol *, uint64_t, Align) override { llvm_unreachable("no .comm"); }
  void emitBytes(StringRef D) override { Seg.Image.append(D.begin(), D.end()); }
  void emitFill(const MCExpr &N, uint64_t V, SMLoc) override {
    int64_t Num;
    if (!N.evaluateAsAbsolute(Num)) return fail("non-constant fill");
    Seg.Image.append(Num, uint8_t(V));
  }
  void emitValueImpl(const MCExpr *E, unsigned Size, SMLoc) override {
    MCValue V;
    if (!E->evaluateAsRelocatable(V, nullptr)) return fail("unsupported constant expression");
    if (V.isAbsolute()) return emitIntValue(V.getConstant(), Size);   // -> emitBytes
    const GlobalValue *A = SymToGV.lookup(V.getAddSym());
    if (!A) return fail("address of a non-global (block address?)");
    uint64_t Site = Seg.Image.size();
    if (const MCSymbol *B = V.getSubSym()) {           // relative tables: both must be our data
      auto *GA = dyn_cast<GlobalVariable>(A); auto *GB = dyn_cast_or_null<GlobalVariable>(SymToGV.lookup(B));
      if (!GA || !GB) return fail("symbol difference across non-data symbols");
      Seg.Diffs.push_back({Site, GA, GB, V.getConstant(), uint8_t(Size)});
    } else {
      if (isa<Function>(A) && V.getConstant()) return fail("function address + offset");
      Seg.Relocs.push_back({Site, A, V.getConstant(), uint8_t(Size)});   // Size 8 or 4
    }
    Seg.Image.append(Size, 0);
  }
private:
  void fail(const Twine &Msg) { Cur.getContext().emitError("MSIL: " + Msg + " in initializer of '" + Cur.getName() + "'"); }
};

void MSILAsmPrinter::emitGlobalVariable(const GlobalVariable *GV) {
  StringRef N = GV->getName();
  if (N == "llvm.global_ctors") { preprocessXXStructorList(DL(), GV->getInitializer(), Ctors); return; }
  if (N == "llvm.global_dtors") { diagnose("global destructors (phase 3, Design 4.1)"); return; }
  if (N.starts_with("llvm.") || GV->getSection() == "llvm.metadata") return;   // llvm.used etc.
  if (GV->isDeclaration()) { if (!GV->use_empty()) diagnose("external global variable"); return; }
  if (GV->isThreadLocal()) { diagnose("thread_local (Design 4.4)"); return; }
  const DataLayout &DL = GV->getDataLayout();
  uint64_t Size = GV->getGlobalSize(DL);
  Align A = getGVAlignment(GV, DL);                  // AsmPrinter.h:974
  Data.MaxAlign = std::max(Data.MaxAlign, A);
  const Constant *Init = GV->getInitializer();
  if (Init->isNullValue() || isa<UndefValue>(Init)) { Data.Zero.push_back({GV, Size, A}); return; }
  Data.Image.resize(alignTo(Data.Image.size(), A), 0);
  uint64_t Off = Data.Image.size();
  std::unique_ptr<MCStreamer> C =
      std::make_unique<MSILDataCollector>(OutContext, Data, symToGV(), *GV);
  std::swap(OutStreamer, C);
  emitGlobalConstant(DL, Init);                      // AsmPrinter.cpp:4660
  std::swap(OutStreamer, C);
  assert(Data.Image.size() == Off + Size && "initializer size mismatch");
  Data.Entries.push_back({GV, Off, Size});
}
```

`symToGV()` is built once, lazily: `for (const GlobalValue &G : M.global_values()) Map[getSymbol(&G)] = &G;`. Those are the same `MCSymbol`s that `lowerConstant` uses.

### 7.2 Layout

- Initialized globals go first, in IR order, each aligned to `getGVAlignment`. Then `finalize()` places zero-initialized globals after `InitSize`.
- `MaxAlign` is at least 16. `TotalSize = alignTo(last end, 1)`.
- The image holds only `[0, InitSize)`, so large BSS arrays cost nothing in the PE. A 4 MB `bytearray` assembled in 0.6 s (e4).
- `.data cil` (read-only, in `.text`) is used because the image is only read, never written. `.data` without `cil` also works (e3).

### 7.3 Module initializer (text written through `TS.emitILLine`; exact output in section 9)

```
ldc.i4 <TotalSize + MaxAlign - 1>; conv.i
call native int [System.Runtime.InteropServices]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
ldc.i4.s <MaxAlign-1>; conv.i; add; ldc.i4.s <-MaxAlign>; conv.i; and
stsfld native int '<msil>data'
ldsfld '<msil>data'; ldsflda valuetype '<msil>image_t' '<msil>image'; ldc.i4 <InitSize>; cpblk
ldsfld '<msil>data'; ldc.i4 <InitSize>; add; ldc.i4.0; ldc.i4 <TotalSize-InitSize>; initblk   // if any
for each global: ldsfld '<msil>data'; [ldc.i4 off; add]; stsfld native int '<name>'
for each reloc:  ldsfld '<msil>data'; ldc.i4 site; add;
                 (ldsfld native int '<target>' [; ldc.i4 addend; add]) | (ldftn <sig> '<fn>');
                 stind.i            (Size 4: conv.i4; stind.i4)
for each ctor (preprocessXXStructorList order): call void '<ctor>'()
ret
```

Notes:
- `AllocHGlobal` plus manual alignment works on both the net8.0 and net48 profiles. `NativeMemory.AlignedAlloc` (System.Runtime.InteropServices 8.0.0.0, verified to exist) is net6+ only. The segment is never freed (process lifetime). `AllocHGlobal` does not zero memory, hence the `initblk`.
- In M1, function relocations use `ldftn`, a managed entry point valid for our own `calli`. The native-callable thunk table (Design 4.5) replaces it later. A relocation to an *undefined* function `ldftn`s its P/Invoke stub (add it to `ExternFuncs`).
- Straight-line relocations are fine for M1. For thousands of relocations (SQLite, Lua tables), phase 2 can switch data→data relocations to a `(site, target)` uint32 table in a second RVA blob plus a loop. Function relocations stay straight-line, or use a per-function address table indexed by the relocation entries.
- The `<Module>` `.cctor` runs before `Main` touches the module (verified: `main` read `names` and `op` correctly). Ctors run after all addresses and relocations are in place, so a ctor that touches globals is safe on the initializing thread.

### 7.4 Generated entry point

`'<msil>Main'(string[])` uses `Environment.GetCommandLineArgs()`, which includes argv[0] (the `.dll` path, verified). It builds `char **argv` with `Marshal.AllocHGlobal` + `Marshal.StringToHGlobalAnsi` (UTF-8 on Unix, ANSI code page on Windows), sets `argv[argc] = NULL`, and then calls `main(int32, native int)` and returns its value.
- With the wasm `FixFunctionBitcasts` port (`passes-frame.md`), an `int main(void)` becomes `__original_main` plus a `main(i32, ptr)` wrapper (WebAssemblyFixFunctionBitcasts.cpp:242-299). So `main` always has that shape, and the generator only needs to handle `(i32, ptr) -> i32`. Diagnose anything else.
- Returning from `Main` flushed the C stdio buffers in every tested configuration (ucrtbase on .NET 8/10 Windows, glibc on .NET 10 Linux, stdout redirected to a file). When the process crashed with an exception, buffered output was lost, which is native behavior.

---

## 8. Answers in one place

- **Q1 (study).** Section 2. NVPTX supplies the mechanics: raw-text headers, a brace closed after the base `runOnMachineFunction`, MCAsmInfo directive suppression, and a byte image with symbol slots. WebAssembly supplies the architecture: signature computation shared with ISel, a target streamer with Asm/Null variants, and the MCInstLower signature hooks.
- **Q2 (file structure).** Sections 4 and 9.
- **Q3 (who produces what, and the signature table).** Sections 5 and 6. The table lives in `MSILAsmPrinter` and is carried into MCInsts by `MSILMemberRefExpr`. P/Invoke declarations come from the collected undefined callees.
- **Q4 (MC settings).** Section 3: MCAsmInfo knobs plus AsmPrinter overrides, with the custom streamer as the fallback.
- **Q5 (data).** Section 7: capture `emitGlobalConstant` → image + relocations → `.data cil` + RVA field + `.cctor`.

---

## 9. Complete example (verified)

`prog.c` (scratch `prog.c`):

```c
int puts(const char *s);

int table[5] = {5, 3, 9, 1, 7};          /* global array            */
const char *names[2] = {"zero", "one"};  /* relocations to strings  */
int hits;                                /* zero-initialized (bss)  */

static int sum(const int *p, int n) {    /* function #2 (internal)  */
  int s = 0;
  for (int i = 0; i < n; ++i)
    s += p[i];
  return s;
}

int (*op)(const int *, int) = sum;       /* relocation to a function */

int main(int argc, char **argv) {        /* function #1             */
  ++hits;
  puts("hello from msil");               /* string literal + P/Invoke */
  puts(names[argc & 1]);
  return op(table, 5);                   /* exit code 25            */
}
```

The IR global order assumed for the layout is `@table`, `@.str` ("zero"), `@.str.1` ("one"), `@names`, `@op`, `@.str.2` ("hello from msil"), `@hits`, and functions `@main`, `@sum` (clang emits the deferred static `sum` last).
- Layout: `table` +0 (20 bytes), `.L.str` +20, `.L.str.1` +25, `names` +32 (align 16), `op` +48, `.L.str.2` +56, giving InitSize 72. Then `hits` +72 (bss), giving TotalSize 76. MaxAlign 16.
- The method bodies show the intended post-ExplicitLocals/RegStackify shape. Exact opcode choices belong to `isel.md`.

`prog.il` (scratch `prog.il`; identical to what the printer should produce, modulo verbose comments):

```
// Generated by LLVM MSIL backend (hand-written reference for milestone 1)
// source_filename = "prog.c"
// target triple   = "x86_64-unknown-linux-gnu-msil"
// assemble with:    ilasm -dll -pe64 -x64 prog.il -output=prog.dll

.assembly extern System.Runtime { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }
.assembly extern System.Runtime.InteropServices { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }
.assembly 'prog'
{
  .custom instance void [System.Runtime]System.Runtime.CompilerServices.DisableRuntimeMarshallingAttribute::.ctor() = ( 01 00 00 00 )
  .ver 0:0:0:0
}

// -- Begin function main
.method public static int32 'main'(int32, native int) cil managed
{
  .maxstack 3
  ldsfld native int 'hits'
  ldsfld native int 'hits'
  ldind.i4
  ldc.i4.1
  add
  stind.i4
  ldsfld native int '.L.str.2'
  call int32 'puts'(native int)
  pop
  ldsfld native int 'names'
  ldarg.0
  ldc.i4.1
  and
  conv.u
  ldc.i4.3
  shl
  add
  ldind.i
  call int32 'puts'(native int)
  pop
  ldsfld native int 'table'
  ldc.i4.5
  ldsfld native int 'op'
  ldind.i
  calli int32(native int, int32)
  ret
}
// -- End function

// -- Begin function sum
.method assembly static int32 'sum'(native int, int32) cil managed
{
  .maxstack 4
  .locals (int32, int32)
  ldc.i4.0
  stloc.0
  ldarg.1
  ldc.i4.1
  blt @BB1_3
  ldc.i4.0
  stloc.1
@BB1_2:
  ldloc.0
  ldarg.0
  ldloc.1
  conv.i
  ldc.i4.2
  shl
  add
  ldind.i4
  add
  stloc.0
  ldloc.1
  ldc.i4.1
  add
  dup
  stloc.1
  ldarg.1
  blt @BB1_2
@BB1_3:
  ldloc.0
  ret
}
// -- End function

// ---- undefined external functions: P/Invoke ----
.method public hidebysig static pinvokeimpl("libc" cdecl) int32 'puts'(native int) cil managed preservesig {}

// ---- data segment: 76 bytes (72 initialized + 4 zero), align 16 ----
.class private explicit ansi sealed '<msil>image_t' extends [System.Runtime]System.ValueType
{
  .pack 1
  .size 72
}
.data cil '<msil>image_d' = bytearray (
  05 00 00 00 03 00 00 00 09 00 00 00 01 00 00 00   // 0x0000
  07 00 00 00 7A 65 72 6F 00 6F 6E 65 00 00 00 00   // 0x0010
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   // 0x0020
  00 00 00 00 00 00 00 00 68 65 6C 6C 6F 20 66 72   // 0x0030
  6F 6D 20 6D 73 69 6C 00 )                         // 0x0040
.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'

.field private static initonly native int '<msil>data'
.field public static initonly native int 'table'      // +0
.field assembly static initonly native int '.L.str'   // +20 "zero"
.field assembly static initonly native int '.L.str.1' // +25 "one"
.field public static initonly native int 'names'      // +32
.field public static initonly native int 'op'         // +48
.field assembly static initonly native int '.L.str.2' // +56 "hello from msil"
.field public static initonly native int 'hits'       // +72 (zero-initialized)

// ---- module initializer ----
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 3
  ldc.i4 91                       // 76 + (16 - 1)
  conv.i
  call native int [System.Runtime.InteropServices]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  ldc.i4.s 15
  conv.i
  add
  ldc.i4.s -16
  conv.i
  and
  stsfld native int '<msil>data'
  ldsfld native int '<msil>data'  // copy initialized image
  ldsflda valuetype '<msil>image_t' '<msil>image'
  ldc.i4 72
  cpblk
  ldsfld native int '<msil>data'  // zero-fill [72, 76)
  ldc.i4 72
  add
  ldc.i4.0
  ldc.i4.4
  initblk
  ldsfld native int '<msil>data'  // symbol addresses
  stsfld native int 'table'
  ldsfld native int '<msil>data'
  ldc.i4.s 20
  add
  stsfld native int '.L.str'
  ldsfld native int '<msil>data'
  ldc.i4.s 25
  add
  stsfld native int '.L.str.1'
  ldsfld native int '<msil>data'
  ldc.i4.s 32
  add
  stsfld native int 'names'
  ldsfld native int '<msil>data'
  ldc.i4.s 48
  add
  stsfld native int 'op'
  ldsfld native int '<msil>data'
  ldc.i4.s 56
  add
  stsfld native int '.L.str.2'
  ldsfld native int '<msil>data'
  ldc.i4.s 72
  add
  stsfld native int 'hits'
  ldsfld native int '<msil>data'  // reloc @32: names[0] = .L.str
  ldc.i4.s 32
  add
  ldsfld native int '.L.str'
  stind.i
  ldsfld native int '<msil>data'  // reloc @40: names[1] = .L.str.1
  ldc.i4.s 40
  add
  ldsfld native int '.L.str.1'
  stind.i
  ldsfld native int '<msil>data'  // reloc @48: op = sum
  ldc.i4.s 48
  add
  ldftn int32 'sum'(native int, int32)
  stind.i
  ret                             // llvm.global_ctors calls go before this ret
}

// ---- entry point: Main(string[]) -> main(argc, argv) ----
.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 4
  .locals init (string[] cl, int32 argc, native int argv, int32 i)
  call string[] [System.Runtime]System.Environment::GetCommandLineArgs()
  stloc.0
  ldloc.0
  ldlen
  conv.i4
  stloc.1
  ldloc.1
  ldc.i4.1
  add
  conv.i
  ldc.i4.8
  mul
  call native int [System.Runtime.InteropServices]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stloc.2
  ldc.i4.0
  stloc.3
  br.s @L1
@L0:
  ldloc.2
  ldloc.3
  conv.i
  ldc.i4.8
  mul
  add
  ldloc.0
  ldloc.3
  ldelem.ref
  call native int [System.Runtime.InteropServices]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  stind.i
  ldloc.3
  ldc.i4.1
  add
  stloc.3
@L1:
  ldloc.3
  ldloc.1
  blt.s @L0
  ldloc.2                         // argv[argc] = NULL
  ldloc.1
  conv.i
  ldc.i4.8
  mul
  add
  ldc.i4.0
  conv.i
  stind.i
  ldloc.1
  ldloc.2
  call int32 'main'(int32, native int)
  ret
}
```

Results (each run in a directory containing only `prog.dll` + `prog.runtimeconfig.json`):

| Build | Runtime | Command | Output | Exit |
|---|---|---|---|---|
| `"libc"` | .NET 10.0.12, WSL Ubuntu (glibc 2.43) | `dotnet prog.dll > o.txt` / `dotnet prog.dll x > o.txt` | `hello from msil\none\n` / `hello from msil\nzero\n` | 25 / 25 |
| `"ucrtbase"` (same file, lib name changed) | .NET 8.0.31 and 10.0.12, Windows | same | same (`\r\n`) | 25 / 25 |
| ildasm → ilasm round trip of the Windows build | .NET 8 | `dotnet prog.dll x` | same | 25 |

The `runtimeconfig.json` used: `{ "runtimeOptions": { "tfm": "net8.0", "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" } } }` (net10.0/10.0.0 for the .NET 10 runs; WSL needed `DOTNET_ROLL_FORWARD=Major` for the 8.0 config).

---

## 10. Experiments (scratch `recon\asmprinter\`)

| File | What it shows |
|---|---|
| `e1_example.il` (+ `prog.dll` from it) | First full module: data blob + RVA field + `.cctor` relocations + `ldftn`/`calli` + `pinvokeimpl("ucrtbase")` under `DisableRuntimeMarshalling` + `.entrypoint` in a `-dll`. Runs on .NET 8/10, exit 25, flushes redirected stdout. A too-small `.maxstack` gave `InvalidProgramException`, and the crash lost the buffered `puts` output. |
| `e1_linux.il`, `e1_linux_so6.il` | `pinvokeimpl("libc")` and `pinvokeimpl("libc.so.6")` both work on .NET 10 / glibc 2.43 (WSL, no `libc.so` dev symlink present). |
| `e2_names.il` | Forward references to global methods and fields work. Quoted names work with a leading dot, dots, `$`, `@?``, a space and `\'`. A label right before `}` works. ildasm quotes only keywords and odd names, and calls bind to MethodDefs. |
| keyword probe (`kw.il` loop) | About 130 ILAsm keywords tested as unquoted method names. Common C names such as `add`, `div`, `rem`, `and`, `or`, `not`, `error`, `init`, `type`, `filter`, `record`, `assert`, `final`, `array`, `vector`, `string`, `float` and `value` fail. `puts`, `printf`, `malloc`, `abs`, `exit`, `min` and `max` pass. |
| `u1/u2/u3` | Non-ASCII names: correct only with a UTF-8 BOM. Without it, or with octal-escaped UTF-8, each byte becomes one Latin-1 char. |
| label probe (`lab.il` loop) | `LBB0_1`, `$LBB0_1`, `@LBB0_1`, `?LBB0_1`, `'.LBB0_1'` and `'add'` are OK. `` `LBB0_1 ``, `.LBB0_1`, `L.BB0_1` and unquoted `add` are rejected. |
| `e3_misc.il` | `ldc.r8 float64(0x…)`, `ldc.r4 float32(0x…)`, bytearray `ldc.r8`, `ldc.i8 0x8000000000000000`, `switch (…)`, argv-building Main (argv[0] = dll path, argv[argc] = NULL), an assembly name different from the file, `.data` without `cil`, and no `.module` (module = output file name). |
| `e4_big.il` | 4 MB `bytearray` RVA data: ilasm 0.6 s, read back correctly. |
| `e5_sw.il` | `switch (@BB0_1, …)` and the trailing label `@func_end0:` before `}`. |
| `e6_esc.il`, `e6b.il` | `\'`, `\t`, `\n`, `\001`, `\101` are correct. **`\\` is dropped**; `\134` and `\\\\` both give one backslash. |
| `dbg*.il` + `clean*/` | Assembly-name collision: with another `prog.dll` (same assembly name) in the directory, `dotnet dbg2.dll` silently executed the other file. In a clean directory it behaves correctly. |
| `final_win/`, `final_linux/`, `prog.il`, `prog_win.il` | Section 9 runs (Windows .NET 8/10, Linux .NET 10, ildasm/ilasm round trip). |
| `ildasm -headers` on `prog.dll` | `-pe64 -x64` gives Machine 0x8664, PE32+ (0x20b), `.corflags 0x1` ILONLY. `-x64` alone prints a warning and implies `-pe64`. |
| PowerShell + System.Reflection.Metadata over `packs\Microsoft.NETCore.App.Ref\8.0.31\ref\net8.0` | Defining reference assemblies: `System.Runtime` (Object, ValueType, String, Environment, AppDomain, RuntimeHelpers, DisableRuntimeMarshallingAttribute, TargetFrameworkAttribute), `System.Runtime.InteropServices` (Marshal, NativeMemory, UnmanagedCallersOnlyAttribute), `System.Console`. All 8.0.0.0, token b03f5f7f11d50a3a. |

---

## 11. Interactions with sibling reports

- **`isel.md` 8.3 prints unquoted names** (`call int32 fib(int32)`). This report requires **always quoting** (keyword evidence above). The change is local to `MSIL::printQuotedName`.
- **`isel.md` 5.7 `GLOBAL_ADDR`** prints `ldsfld '<data base>'; ldc.i4 <off(sym)+off>; add` and lists "one initonly field per global" as the alternative. **This report picks the per-global field** (`ldsfld native int 'g'` [+ `ldc.i4 off; add`]). Reason: the layout is only known in `doFinalization`, *after* all functions are printed (section 0.2), and symbolic fields also map one-to-one onto phase-2 relocations. The MI, its `StackPeakExtra = 1` and `isOffsetFoldingLegal` stay as `isel.md` proposes. Only the printer text changes.
- **`isel.md` 8.2 / 12 says a `-ffreestanding` call to `memcpy(…)` "must be diagnosed".** With the general "undefined function → P/Invoke into the default libc" rule (section 6.5) it simply works. Diagnose only variadic declarations (Design 4.7).
- **`passes-frame.md`:** `.maxstack` comes from `MSILStackCheck` via `MFI`. `.locals` is emitted without `init`, and `MFI` must expose CIL value types (not MVTs) for locals and params. `FixFunctionBitcasts` is kept, so `main` is always `(i32, ptr) -> i32`.
- **`triple-objfmt.md`:** `IsMSIL` + `MCSectionMSIL` + `MSILTargetObjectFile` provide `getTextSection()` (needed by `initSections`) and `SectionForGlobal` (needed by the base `emitFunctionHeader`). Its "override `printSwitchToSection`" is unnecessary but harmless: the base is already empty (MCAsmInfo.h:507-508). Its "MCTargetStreamer subclass overriding `changeSection`" is also unnecessary for the same reason. It agrees on using IR names instead of mangled `getSymbol()` names.
- **`registry-tm.md`:** `MSILMCAsmInfo` with `ExceptionsType = None` (agrees). It registers asm and null target streamers (agrees).

---

## 12. Test plan for the AsmPrinter work

1. `llvm/test/CodeGen/MSIL/asmprinter/*.ll` with FileCheck, one feature per test:
   - the header (`.assembly extern`, `.assembly`, no `.module`);
   - a method header for each signature shape;
   - `.maxstack`/`.locals`;
   - quoting (functions named `add`, `div`, `.L.x`, non-ASCII → BOM);
   - labels `@BB`;
   - P/Invoke declarations for undefined callees, including one referenced only from data;
   - the data image (bytes, BSS split, alignment);
   - relocations (data+addend, function, 4-byte);
   - ctors order;
   - the `Main` shape;
   - diagnostics (alias, ifunc, TLS, external global, dtors, variadic P/Invoke, module asm).
2. A **"no GNU directives" guard**: `CHECK-NOT: {{^[[:space:]]*\.(text|data|section|globl|p2align|type|size|file|ident|addrsig)}}` in every asmprinter test. Note that `.data cil` is a legal ILAsm directive and must be excluded from the pattern.
3. A lit feature `ilasm`, and a run line for each test of the form `ilasm -dll -pe64 -x64 %t.il -output=%t.dir/<unique>.dll`. Every emitted `.il` must assemble. Give each test its own output directory and a unique assembly name (section 0.9).
4. Runtime tests (Design 12 corpus) under `dotnet` with a generated runtimeconfig. Round-trip through `ildasm`+`ilasm` where ildasm is available (Windows), as Design 5.5 requires.

---

## 13. Risks and open questions

1. **Core ilasm (Linux CI) parity.** Every verification used .NET Framework ilasm 4.8. The Linux CI needs the NuGet package `runtime.linux-x64.Microsoft.NETCore.ILAsm` (via `Microsoft.NETCore.ILAsm`, e.g. 8.0.x) and `dotnet-ilverify` (8.0.x). Neither was downloaded. BOM handling, `\134`, the dropped `\\`, and the `-pe64 -x64` spelling are *unverified* on Core ilasm.
2. **`"libc"` resolution on .NET 8 Linux** is *unverified* (only .NET 10 was available in WSL). `"libc.so.6"` is the safer default for glibc triples and matches Design 5.3's `DT_SONAME` plan.
3. **Swapping `OutStreamer`** is unusual. If upstream reviewers object, the NVPTX-style walker (section 7.1) is the drop-in replacement with the same `MSILDataSegment` output.
4. **Large straight-line `.cctor`s** (many globals or relocations) will be JIT'd with MinOpts once. Acceptable for M1; switch to a table plus loop when Design 12 programs grow.
5. **RyuJIT folding of `static initonly native int` fields** in `<Module>` at Tier1 is *unverified*. It only affects performance.
6. **Assembly naming.** M1 uses a `cl::opt` plus the source stem. Should the driver pass the `-o` stem through a module flag instead? Proposed: the driver sets module flag `"msil.assembly-name"`; the backend falls back to the cl::opt, then to the source stem.
7. **`-g`** produces only comments and `@func_end` labels in M1. Portable PDB emission (Design 10.7) will need an AsmPrinterHandler, plus `.line`/sequence-point support in the text path (ILAsm `.line` directive, *unverified*).
8. **Export class (Design 5.4).** Global methods cannot be called from C# source. `-shared` (phase 2) needs either methods in a named class or forwarders. `MSILMemberRefExpr::Owner` exists so that this changes only the printer.
