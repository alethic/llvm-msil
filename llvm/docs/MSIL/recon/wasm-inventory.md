# MSIL recon: the WebAssembly target as a template (`wasm-inventory`)

Status: recon report, 2026-10-12. Read-only investigation of `D:\llvm-msil`, branch
`msil-milestone1`, HEAD `2829ef573b08`. No tracked file was modified. All line counts
are `wc -l` at that HEAD. Line numbers refer to that HEAD unless marked otherwise.

Scope (from the task):
1. Inventory every file in `llvm/lib/Target/WebAssembly` and classify it for MSIL
   milestone 1 (M1) as COPY, ADAPT, DROP or DEFER.
2. The TableGen design: the register/stack dual-form scheme, whether MSIL keeps it, the
   M1 register classes, the minimal `.td` file set and the M1 instruction list.
3. Copy-and-rename feasibility, with numbers.
4. The M1 directory layout for `llvm/lib/Target/MSIL` and a split into work packages
   that agents can own in parallel.

Related recon reports in this directory (read them together with this one; this
report does not repeat their detail):
- `passes-frame.md`: post-ISel pass pipeline, frame lowering with `localloc`,
  argument/local numbering, `.maxstack`, CIL stack rules (with experiments).
- `registry-tm.md`: target selection by object format, CMake registration,
  DataLayout, CPU/feature handling, TTI/TLI.
- `isel.md`: SelectionDAG lowering and the per-instruction CIL mapping, with run-time
  verification on .NET 8. It is the authority for instruction semantics. Sections 3.6 and
  3.7 here are the inventory-level baseline; where they differ, follow `isel.md`, except on
  the dual-form question below.
- `clang.md`: the clang side (not used here).

Where this report and those overlap, they agree, except:
- **Dual-form instructions: open conflict.** `isel.md` 10.1 keeps WebAssembly's
  register + `_S` stack scheme (`multiclass I<oops_r, iops_r, oops_s, iops_s, ...>`). This
  report recommends one record per instruction (section 3.3). The WP0 owner must pick one
  before any `.td` work starts, because every per-area `.td` file depends on the choice.
  The record names, operands, patterns and CIL text are the same either way.
- `isel.md` independently reaches the same `I64` = `native int` decision (its section 0,
  item 1), and so does section 3.4 here.
- `passes-frame.md` section 11 leaves the pointer/`int64` representation open. Section 3.4
  resolves it, with PEVerify evidence.
- `passes-frame.md` 6.2 writes `LOCALLOC` in WebAssembly's dual-form `I<...>` syntax with
  a `PTR` class. Under sections 3.3 and 3.4 it becomes
  `defm LOCALLOC : I<(outs I64:$dst), (ins I32:$size), [], "localloc", 0xFE0F>;`.

---

## 0. Summary

1. **Size.** `llvm/lib/Target/WebAssembly` has **123 files, 38,348 lines**: 66 `.cpp`
   (29,632), 31 `.h` (3,394), 18 `.td` (4,881), 6 `CMakeLists.txt` (190), 1 `.def` (78),
   1 `README.txt` (173).
2. **Classification for M1** (full table in 2.2):

   | Class | Files | Lines | Meaning |
   |---|---:|---:|---|
   | COPY | 14 | 3,694 | Rename only. Two of them (RegStackify, ExplicitLocals) also need small, local edits; they are marked COPY* in the table. |
   | ADAPT | 58 | 15,630 | Copy, then modify substantially. My estimate is about 7.8k lines after trimming (2.4). |
   | DEFER | 11 | 5,913 | Needed later: object writer, MC encoder, SIMD, atomics, SjLj, new pass manager, debug fixup, br_table defaults. |
   | DROP | 40 | 13,111 | Not needed for CIL: structured control flow, wasm EH, reference types, tables, GlobalISel, FastISel, asm parser, disassembler. |

   So M1 needs **72 of the 123 files** (COPY+ADAPT), about **11.5k lines** after
   trimming. With 2 Utils files folded into others and 5 new files, the M1 directory has
   **75 files and about 12k lines** (2.4, 5.1).
3. **Shared LLVM infrastructure.** A mechanical rename (WebAssembly/Wasm to MSIL)
   leaves **47 files with 752 lines** that reference infrastructure that does not exist
   for MSIL: `llvm/BinaryFormat/Wasm.h`, `wasm::ValType`/`wasm::WasmSignature`,
   `MCSymbolWasm`, `MCSectionWasm`, `MCAsmInfoWasm`, `TargetLoweringObjectFileWasm`,
   `MCWasmObjectTargetWriter`, `IntrinsicsWebAssembly.h`, `WasmEHPrepare`/`WasmEHInfo`.
   None of the 14 COPY files is among them. Section 4 has the per-file numbers.
4. **Recommendation: curated selective copy, not wholesale copy-then-strip.** Copy the
   14 COPY files verbatim (renamed), copy and trim the 58 ADAPT files, and never copy the
   51 DROP/DEFER files. Leave the DEFER files in `WebAssembly/` as the reference for
   phases 2 and 3. A wholesale copy would carry 19,024 dead lines (49.6%) and would not
   build until the same 47 files were fixed or deleted. Section 4.4 gives the argument.
5. **TableGen: drop the dual-form (register + `_S` stack) scheme.** WebAssembly needs the
   stack twins only for its MC layer: asm parser, disassembler, binary encoder and the
   canonical text printer. M1 has none of those (ilasm assembles, ildasm disassembles),
   and the phase-2 encoder can skip register operands, as `WebAssemblyMCCodeEmitter.cpp:143`
   already does. Use one record per instruction. Keep the WebAssembly record *names*
   (`ADD_I32`, `CONST_I64`, `LOCAL_GET_I32`, `BR_IF`, `CALL`, ...), so the COPY passes
   compile unchanged. Section 3.3. **`isel.md` 10.1 keeps the dual form instead.** That is
   an open decision for WP0 (trade-off table in 3.3).
6. **Register classes: `I32`, `I64`, `F32`, `F64`** (and `V128` later), plus the nominal
   registers `FP`, `VALUE_STACK` and `ARGUMENTS`. **Print every `I64` value as `native int`
   on 64-bit triples**, not as `int64`. PEVerify reports mixing `int64` and `native int`
   as a stack-type error ("found Long, expected Native Int"). With the all-`native int`
   scheme only the expected unverifiable-pointer errors remain (verified, section 8).
   Section 3.4.
7. **Minimal `.td` set for M1: 11 files.** `MSIL.td`, `MSILRegisterInfo.td`,
   `MSILInstrFormats.td`, `MSILInstrInfo.td`, then one file per area: `MSILInstrInteger.td`,
   `MSILInstrFloat.td`, `MSILInstrConv.td`, `MSILInstrMemory.td`, `MSILInstrControl.td`,
   `MSILInstrCall.td`, `MSILInstrFrame.td`. Each per-area file has one owner. The
   instruction list is in 3.6, and the `setOperationAction` table for the TargetLowering
   constructor is in 3.7.
8. **Work packages.** WP0 owns every shared hotspot: `CMakeLists.txt`, `MSIL.td`,
   `MSILInstrInfo.td`, `MSIL.h`, the pipeline in `MSILTargetMachine.cpp`, operand types
   and opcode predicates. WP0 lands first and pre-creates stub files for all planned
   sources. This is needed because LLVM's CMake rejects any `.cpp` file in the directory
   that is not listed (`llvm/cmake/modules/LLVMProcessSources.cmake:83-133`). After WP0,
   WP1 to WP6 own disjoint file sets and run in parallel. WP7 (module emission) and WP8
   (end-to-end suite) follow. Every package has a test gate and a fixed reviewer. Section 6.

---

## 1. Method

- Line counts come from `wc -l` on every file (`find llvm/lib/Target/WebAssembly -type f`).
- I read every `.td` file except the bodies of `WebAssemblyInstrSIMD.td` and
  `WebAssemblyInstrAtomics.td` (headers only: they are DEFER). I read the key C++ in full
  or in the relevant parts: TargetMachine, ISelLowering (constructor, call lowering,
  LowerOperation, custom inserters), ISelDAGToDAG (`Select`), MCInstLower, ExplicitLocals,
  RegNumbering, RegStackify (main loop), FrameLowering, RegisterInfo, MCTargetDesc,
  MCAsmInfo, InstPrinter, TargetStreamer, TargetInfo, Subtarget, MachineFunctionInfo,
  Utilities. For the smaller passes I read the header comment and the target references.
- Copy-and-rename experiment: copy the directory into the scratchpad, rename files and
  contents (`WebAssembly`/`WEBASSEMBLY`/`Wasm`/`WASM`/word-initial `wasm` to MSIL), then
  grep for includes and identifiers that no longer resolve (section 4.1). Nothing was built.
- CIL facts were checked with the local tools: ilasm (`C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe`),
  PEVerify (`C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\x64\PEVerify.exe`),
  and execution on .NET Framework 4.x x64 and .NET 8.0. CIL opcode values come from
  `System.Reflection.Emit.OpCodes` under PowerShell 7. The probes are in section 8; the
  sources are in the scratchpad under `recon/wasm-inventory/il/`.

---

## 2. File inventory

### 2.1 Totals

| Directory | Files | Lines |
|---|---:|---:|
| top level (`WebAssembly/`) | 86 | 31,049 |
| `MCTargetDesc/` | 15 | 2,513 |
| `AsmParser/` | 4 | 2,256 |
| `GISel/` | 9 | 1,815 |
| `Disassembler/` | 2 | 399 |
| `Utils/` | 4 | 223 |
| `TargetInfo/` | 3 | 93 |
| **total** | **123** | **38,348** |

The largest files: `WebAssemblyISelLowering.cpp` 4,091; `WebAssemblyCFGStackify.cpp` 2,728;
`WebAssemblyInstrSIMD.td` 1,994; `WebAssemblyLowerEmscriptenEHSjLj.cpp` 1,892;
`WebAssemblyFastISel.cpp` 1,889; `AsmParser/WebAssemblyAsmParser.cpp` 1,412;
`WebAssemblyRegStackify.cpp` 1,062.

### 2.2 Every file

Legend: **COPY** = rename only (COPY* = rename plus small local edits, listed in the note).
**ADAPT** = copy and modify substantially. **DROP** = not needed for CIL. **DEFER** = needed
in a later phase. Lines are `wc -l`.

| Path (under `llvm/lib/Target/WebAssembly/`) | Lines | Purpose | M1 | Note for MSIL |
|---|---:|---|---|---|
| `AsmParser/CMakeLists.txt` | 16 | Builds LLVMWebAssemblyAsmParser. | **DROP** | No llvm-mc parsing of .il; ilasm is the M1 assembler. |
| `AsmParser/WebAssemblyAsmParser.cpp` | 1412 | Wasm text assembler for llvm-mc (directives, operands, stack-form matching). | **DROP** | Not needed: ilasm assembles .il in M1; phase 2 writes objects directly. |
| `AsmParser/WebAssemblyAsmTypeCheck.cpp` | 722 | Operand-stack type checker for hand-written wasm asm. | **DROP** | Same as above. |
| `AsmParser/WebAssemblyAsmTypeCheck.h` | 106 | Header for the asm type checker (uses wasm::ValType). | **DROP** | Same as above. |
| `CMakeLists.txt` | 112 | Top-level target build: 11+3 tablegen invocations, CodeGen source list, subdirs. | **ADAPT** | New source list; keep only -gen-asm-writer, -gen-dag-isel, -gen-instr-info, -gen-register-info, -gen-sd-node-info, -gen-subtarget. |
| `Disassembler/CMakeLists.txt` | 14 | Builds LLVMWebAssemblyDisassembler. | **DROP** | ildasm exists. |
| `Disassembler/WebAssemblyDisassembler.cpp` | 385 | Binary wasm to MCInst decoder. | **DROP** | Not needed. |
| `GISel/WebAssemblyCallLowering.cpp` | 634 | GlobalISel call/argument lowering. | **DROP** | SelectionDAG only. |
| `GISel/WebAssemblyCallLowering.h` | 43 | GlobalISel call lowering header. | **DROP** | SelectionDAG only. |
| `GISel/WebAssemblyInstructionSelector.cpp` | 300 | GlobalISel instruction selector (WebAssemblyGenGlobalISel.inc). | **DROP** | SelectionDAG only. |
| `GISel/WebAssemblyLegalizerInfo.cpp` | 234 | GlobalISel legalizer rules. | **DROP** | SelectionDAG only. |
| `GISel/WebAssemblyLegalizerInfo.h` | 32 | GlobalISel legalizer header. | **DROP** | SelectionDAG only. |
| `GISel/WebAssemblyPostLegalizerCombiner.cpp` | 205 | GlobalISel post-legalizer combiner pass. | **DROP** | SelectionDAG only. |
| `GISel/WebAssemblyPreLegalizerCombiner.cpp` | 208 | GlobalISel pre-legalizer combiner pass. | **DROP** | SelectionDAG only. |
| `GISel/WebAssemblyRegisterBankInfo.cpp` | 118 | GlobalISel register bank info. | **DROP** | SelectionDAG only. |
| `GISel/WebAssemblyRegisterBankInfo.h` | 41 | GlobalISel register bank header. | **DROP** | SelectionDAG only. |
| `MCTargetDesc/CMakeLists.txt` | 20 | Builds LLVMWebAssemblyDesc. | **ADAPT** | Drop AsmBackend/CodeEmitter/ObjectWriter sources; add MSILMCExpr.cpp. |
| `MCTargetDesc/WebAssemblyAsmBackend.cpp` | 135 | MCAsmBackend: fixup application; creates the Wasm object writer. | **DEFER** | Phase 2 (CIL object format). |
| `MCTargetDesc/WebAssemblyFixupKinds.h` | 29 | Fixup kinds (LEB-encoded symbol references). | **DEFER** | Phase 2. |
| `MCTargetDesc/WebAssemblyInstPrinter.cpp` | 488 | Prints MCInsts: TableGen AsmWriter plus custom call_indirect, br_table, catch-list, type-list printers. | **ADAPT** | Rewrite custom parts for CIL: call/calli method refs, switch label list, float bit literals. |
| `MCTargetDesc/WebAssemblyInstPrinter.h` | 71 | InstPrinter class declaration. | **ADAPT** | Rename plus new print methods. |
| `MCTargetDesc/WebAssemblyMCAsmInfo.cpp` | 64 | Asm syntax properties; derives from MCAsmInfoWasm; @-specifiers. | **ADAPT** | Derive from plain MCAsmInfo: '//' comments, no .type/.size/.p2align, label prefix without a leading '.'. |
| `MCTargetDesc/WebAssemblyMCAsmInfo.h` | 44 | MCAsmInfo declaration. | **ADAPT** | As above. |
| `MCTargetDesc/WebAssemblyMCCodeEmitter.cpp` | 283 | Binary encoder: opcode prefixes plus LEB immediates; skips register operands (line 143). | **DEFER** | Phase 2 CIL byte encoder can reuse the operand loop. |
| `MCTargetDesc/WebAssemblyMCTargetDesc.cpp` | 134 | Registers MCAsmInfo, MCInstrInfo, MCRegInfo, InstPrinter, CodeEmitter, AsmBackend, SubtargetInfo, target streamers. | **ADAPT** | Register one Target; no CodeEmitter/AsmBackend/object target streamer in M1. |
| `MCTargetDesc/WebAssemblyMCTargetDesc.h` | 627 | OperandType enum, operand flags, opcode-class predicates (isArgument, isCopy, isTee, isCallDirect...), p2align table. | **ADAPT** | Keep predicates minus _S/ref/EH/atomic/SIMD cases; new operand types. |
| `MCTargetDesc/WebAssemblyMCTypeUtilities.cpp` | 122 | wasm::ValType and BlockType to/from strings; regclass to ValType. | **ADAPT** | Replace with CIL type names ('int32', 'native int', 'float32', 'float64'). |
| `MCTargetDesc/WebAssemblyMCTypeUtilities.h` | 79 | Declarations for the above (includes BinaryFormat/Wasm.h). | **ADAPT** | As above. |
| `MCTargetDesc/WebAssemblyTargetStreamer.cpp` | 137 | Asm/Wasm/Null target streamers: .functype .local .globaltype .import_* directives. | **ADAPT** | Asm and Null streamers that print ILAsm directives (.method header, .locals init, .maxstack, .field, .data, .assembly). |
| `MCTargetDesc/WebAssemblyTargetStreamer.h` | 115 | Target streamer interfaces keyed on MCSymbolWasm. | **ADAPT** | Re-key on plain MCSymbol plus MSIL signature objects. |
| `MCTargetDesc/WebAssemblyWasmObjectWriter.cpp` | 165 | MCWasmObjectTargetWriter: fixup to wasm relocation type. | **DEFER** | Phase 2. |
| `README.txt` | 173 | Wasm backend notes and TODOs. | **DROP** | Write a new MSIL README if wanted. |
| `TargetInfo/CMakeLists.txt` | 14 | Builds LLVMWebAssemblyInfo (depends on WebAssemblyCommonTableGen). | **ADAPT** | Rename; drop include_directories/add_dependencies because MSILTargetInfo.h no longer includes generated InstrInfo (registry-tm.md 2.4). |
| `TargetInfo/WebAssemblyTargetInfo.cpp` | 43 | Registers wasm32/wasm64 Targets by arch (`RegisterTarget<Triple::wasm32>`). | **ADAPT** | One Target 'msil'; arch-match function returns false; selection by object format in lib/MC/TargetRegistry.cpp. |
| `TargetInfo/WebAssemblyTargetInfo.h` | 36 | getTheWebAssemblyTarget32/64 and InstrMapping function declarations. | **ADAPT** | getTheMSILTarget(); no InstrMapping functions (single-form instructions). |
| `Utils/CMakeLists.txt` | 14 | Builds LLVMWebAssemblyUtils (shared by CodeGen and AsmParser). | **DROP** | No AsmParser, so fold Utils into CodeGen/MCTargetDesc. |
| `Utils/WasmAddressSpaces.h` | 44 | Wasm address spaces (var=1, funcref=20). | **DROP** | Not applicable. |
| `Utils/WebAssemblyTypeUtilities.cpp` | 99 | parseMVT, MVT to wasm::ValType, wasmSymbolSetType. | **ADAPT** | Move MVT helpers into MSILUtilities / MSILMCTypeUtilities. |
| `Utils/WebAssemblyTypeUtilities.h` | 66 | Reference-type predicates and declarations. | **ADAPT** | As above, without reference types. |
| `WebAssembly.h` | 381 | Pass classes, factory and initialize* declarations; TargetIndex enum. | **ADAPT** | Declare only M1 passes (keep both Legacy and NewPM classes of copied passes). |
| `WebAssembly.td` | 186 | Features, processors, includes, ptr_rc remap, Target definition. | **ADAPT** | New features (none or profile), processor 'generic', `RemapAllTargetPseudoPointerOperands<I64>`. |
| `WebAssemblyAddMissingPrototypes.cpp` | 167 | IR pass: gives 'no-prototype' declarations a real function type. | **COPY** | Needs the clang MSIL TargetCodeGenInfo to add 'no-prototype' (today only clang/lib/CodeGen/Targets/WebAssembly.cpp:122-123 does). |
| `WebAssemblyArgumentMove.cpp` | 108 | Moves ARGUMENT_* pseudos to the top of the entry block after scheduling. | **COPY** | Rename only. |
| `WebAssemblyAsmPrinter.cpp` | 892 | Module and function emission: .functype, globals, producers/target_features sections, emitInstruction. | **ADAPT** | Rewrite module emission for ILAsm; keep emitInstruction/MCInstLower plumbing. |
| `WebAssemblyAsmPrinter.h` | 107 | AsmPrinter class plus NewPM begin/end passes. | **ADAPT** | Keep legacy class only in M1. |
| `WebAssemblyCFGSort.cpp` | 416 | Topological block sort so loops/EH regions are contiguous. | **DROP** | CIL allows arbitrary branches (Design 2.2). |
| `WebAssemblyCFGStackify.cpp` | 2728 | Inserts BLOCK/LOOP/TRY markers; rewrites branch targets to depths. | **DROP** | Dropped by Design 2.2. |
| `WebAssemblyCleanCodeAfterTrap.cpp` | 96 | Erases instructions after an unreachable terminator. | **COPY** | Rename only. |
| `WebAssemblyCoalesceFeaturesAndStripAtomics.cpp` | 212 | Unions per-function features; strips atomics/TLS when not enabled. | **DROP** | Wasm-specific feature model. |
| `WebAssemblyCodeGenPassBuilder.cpp` | 350 | New-pass-manager codegen pipeline (buildCodeGenPipeline). | **DEFER** | Legacy PM is the default for clang and llc unless shouldDefaultToNewPM() (TargetMachine.h:531). |
| `WebAssemblyCombine.td` | 22 | GlobalISel combiner rules. | **DROP** | SelectionDAG only. |
| `WebAssemblyDebugFixup.cpp` | 173 | Rewrites DBG_VALUEs of stackified regs to operand-stack locations. | **DEFER** | Needed with debug info (Design risk 7). |
| `WebAssemblyDebugValueManager.cpp` | 453 | Keeps DBG_VALUEs consistent when defs are moved/cloned. | **COPY** | Compile dependency of RegStackify and ExplicitLocals. |
| `WebAssemblyDebugValueManager.h` | 57 | Header for the above. | **COPY** | Rename only. |
| `WebAssemblyExceptionInfo.cpp` | 252 | Wasm EH region analysis for CFGSort/CFGStackify. | **DROP** | CIL try/catch EH design is different. |
| `WebAssemblyExceptionInfo.h` | 209 | Header for the above. | **DROP** | As above. |
| `WebAssemblyExplicitLocals.cpp` | 517 | Turns non-stackified vregs into typed locals; inserts local.get/set/tee and drop. | **COPY** | COPY*: rename, delete V128/ref-type cases, and pick ARG_* vs LOCAL_* opcodes by index < NumParams (passes-frame.md 7). |
| `WebAssemblyFastISel.cpp` | 1889 | FastISel for -O0. | **DROP** | SelectionDAG at -O0 is fine for M1; -gen-fast-isel not needed. |
| `WebAssemblyFixBrTableDefaults.cpp` | 216 | Folds the jump-table range check into br_table's default target. | **DEFER** | M1 keeps LLVM's range check and prints 'switch (...)' + 'br' to the first-case label (passes-frame.md 3). Port later as an optimization: CIL 'switch' falls through when out of range. |
| `WebAssemblyFixFunctionBitcasts.cpp` | 313 | IR pass: wrappers for calls through mismatched function types; normalizes main(). | **COPY** | Rename only. |
| `WebAssemblyFixIrreducibleControlFlow.cpp` | 586 | Makes multi-entry loops single-entry. | **DROP** | Dropped by Design 2.2. |
| `WebAssemblyFrameLowering.cpp` | 422 | Prologue/epilogue via the __stack_pointer global. | **ADAPT** | Prologue becomes 'localloc' of the static frame size (Design 4.2); no epilogue in M1. |
| `WebAssemblyFrameLowering.h` | 81 | FrameLowering declaration plus opcode helpers. | **ADAPT** | As above. |
| `WebAssemblyGISel.td` | 84 | GlobalISel patterns and complex-operand equivalents. | **DROP** | SelectionDAG only. |
| `WebAssemblyISelDAGToDAG.cpp` | 671 | DAG selector: CALL split into CALL_PARAMS/CALL_RESULTS, address modes, EH/atomic/intrinsic cases. | **ADAPT** | Keep the CALL split; drop wasm intrinsics, atomics, address-offset ComplexPatterns. |
| `WebAssemblyISelLowering.cpp` | 4091 | TargetLowering: op actions, call/arg/return lowering, custom inserters, SIMD lowering, DAG combines. | **ADAPT** | Keep about 1.2k of 4.1k lines (estimate); rewritten ctor table (3.7); add SELECT inserter. |
| `WebAssemblyISelLowering.h` | 147 | TargetLowering class declaration. | **ADAPT** | As above. |
| `WebAssemblyInstrAtomics.td` | 598 | Atomic memory instructions. | **DEFER** | Phase 3 (Interlocked, volatile.). |
| `WebAssemblyInstrBulkMemory.td` | 111 | memory.copy/fill/init and MEMCPY/MEMSET pseudos. | **DROP** | CIL cpblk/initblk replace it (MSILInstrMemory.td). |
| `WebAssemblyInstrCall.td` | 102 | ADJCALLSTACK*, CALL_PARAMS/CALL_RESULTS, CALL, CALL_INDIRECT, tail and ref calls. | **ADAPT** | Keep ADJCALLSTACK*, CALL_PARAMS, CALL_RESULTS, CALL ('call'), CALL_INDIRECT ('calli'). |
| `WebAssemblyInstrControl.td` | 207 | br/br_if/br_unless/br_table, block markers, return, unreachable, EH. | **ADAPT** | Keep BR, BR_IF, BR_UNLESS, BR_TABLE_I32, RETURN, UNREACHABLE, DEBUG_UNREACHABLE, NOP. |
| `WebAssemblyInstrConv.td` | 240 | wrap/extend/trunc/convert/promote/demote/reinterpret. | **ADAPT** | CIL conv.* table (3.6); bitcasts go through memory in M1. |
| `WebAssemblyInstrFloat.td` | 127 | f32/f64 arithmetic, comparisons, select, rounding ops. | **ADAPT** | add/sub/mul/div/rem/neg and ceq/cgt/clt(.un); rounding and sqrt become calls (3.7). |
| `WebAssemblyInstrFormats.td` | 68 | WebAssemblyInst/NI classes and the I/NRI register+stack multiclasses. | **ADAPT** | Single-form MSILInst/NI/I (3.3). |
| `WebAssemblyInstrInfo.cpp` | 243 | TargetInstrInfo: copyPhysReg, branch analysis, remat, commute. | **ADAPT** | Keep; drop CATCHRET/EH and target-index debug hooks. |
| `WebAssemblyInstrInfo.h` | 77 | InstrInfo declaration. | **ADAPT** | Rename and trim. |
| `WebAssemblyInstrInfo.td` | 485 | Predicates, SDNodes, operand types, InstrMappings, ARGUMENT/LOCAL/GLOBAL/CONST, address patterns, includes. | **ADAPT** | Core of MSILInstrInfo.td (3.5). |
| `WebAssemblyInstrInteger.td` | 183 | i32/i64 arithmetic, logic, shifts, rotates, comparisons, bit counts, select, wide arithmetic. | **ADAPT** | add/sub/mul/div(.un)/rem(.un)/and/or/xor/shl/shr(.un) and ceq/cgt/clt(.un). |
| `WebAssemblyInstrMemory.td` | 209 | Loads/stores with offset+p2align operands for A32/A64; memory.size/grow. | **ADAPT** | ldind.*/stind.* with an address operand and a prefix-flags operand; CIL has no offset field. |
| `WebAssemblyInstrRef.td` | 80 | Reference types (ref.null, ref.func, typed select). | **DROP** | Not applicable. |
| `WebAssemblyInstrSIMD.td` | 1994 | SIMD128 instructions. | **DEFER** | Later: `Vector128<T>` (Design 3.2). |
| `WebAssemblyInstrTable.td` | 91 | table.get/set/grow/fill/size/copy. | **DROP** | Not applicable. |
| `WebAssemblyLateEHPrepare.cpp` | 439 | Late wasm EH CFG fixups. | **DROP** | Not applicable. |
| `WebAssemblyLowerBrUnless.cpp` | 224 | Lowers the BR_UNLESS pseudo to eqz + br_if. | **DROP** | CIL has 'brfalse'; BR_UNLESS becomes a real instruction. |
| `WebAssemblyLowerEmscriptenEHSjLj.cpp` | 1892 | IR pass lowering invoke/setjmp/longjmp to Emscripten or wasm-EH runtime calls. | **DEFER** | Starting point for setjmp/longjmp (Design 4.6, phase 3). |
| `WebAssemblyMCInstLower.cpp` | 397 | MachineInstr to MCInst: symbols with wasm signatures, type-index operands, reg-to-stack opcode switch. | **ADAPT** | Lower MBB operands to labels, symbols to MSIL method-ref exprs, arg/local split, TEE to dup+stloc. |
| `WebAssemblyMCInstLower.h` | 51 | MCInstLower declaration (uses wasm::ValType). | **ADAPT** | As above. |
| `WebAssemblyMCLowerPrePass.cpp` | 119 | Pre-collects symbol types needed before asm printing. | **DROP** | ILAsm resolves forward references; declarations can be emitted at module end. |
| `WebAssemblyMachineFunctionInfo.cpp` | 149 | computeSignatureVTs/computeLegalValueVTs, vreg to WAReg map, MIR YAML. | **ADAPT** | Keep signature computation; drop wasm::ValType/WasmSignature helpers. |
| `WebAssemblyMachineFunctionInfo.h` | 205 | Params/Results/Locals, VRegStackified bitvector, WARegs, frame-base vreg. | **ADAPT** | Rename; drop CFGStackified and wasm signature helpers. |
| `WebAssemblyMemIntrinsicResults.cpp` | 276 | Uses memcpy/memset return values to save locals. | **DROP** | memcpy/memset become cpblk/initblk (no result). |
| `WebAssemblyNullifyDebugValueLists.cpp` | 78 | Sets DBG_VALUE_LISTs undef (unsupported by DebugValueManager). | **COPY** | Rename only. |
| `WebAssemblyOptimizeLiveIntervals.cpp` | 141 | Splits and cleans live intervals before RegStackify (O1+). | **COPY** | Rename only. |
| `WebAssemblyOptimizeReturned.cpp` | 102 | IR pass exploiting 'returned' argument attributes (O1+). | **COPY** | Rename only. |
| `WebAssemblyPassRegistry.def` | 78 | New-PM pass name registry. | **DEFER** | Comes with the new-PM pipeline. |
| `WebAssemblyPeephole.cpp` | 211 | Late peepholes (memcpy results, fallthrough return). | **DROP** | Its FALLTHROUGH_RETURN rewrite is wrong for CIL (explicit 'ret' required) and it asserts END_FUNCTION from CFGStackify. A CIL peephole is a new pass. |
| `WebAssemblyReduceToAnyAllTrue.cpp` | 154 | Turns vector and/or reductions into any_true/all_true. | **DROP** | Wasm SIMD-specific. |
| `WebAssemblyRefTypeMem2Local.cpp` | 116 | Moves reference-type allocas to the wasm-var address space. | **DROP** | Not applicable. |
| `WebAssemblyRegColoring.cpp` | 355 | Merges non-interfering vregs to reduce the local count (O1+). | **COPY** | Rename only (fewer locals also helps RyuJIT). |
| `WebAssemblyRegNumbering.cpp` | 122 | Assigns final local numbers (arguments first) to vregs. | **COPY** | Rename only. |
| `WebAssemblyRegStackify.cpp` | 1062 | Reorders defs into single-use expression trees on the value stack; tees; rematerialization. | **COPY** | COPY*: rename, delete V128/ref cases, trapping-conversion list and __stack_pointer checks; add 'never stackify a LOCALLOC def' (passes-frame.md 5.5). |
| `WebAssemblyRegisterBanks.td` | 22 | GlobalISel register banks. | **DROP** | SelectionDAG only. |
| `WebAssemblyRegisterInfo.cpp` | 206 | eliminateFrameIndex, getFrameRegister, GISel regbank mapping (lines 152-206). | **ADAPT** | Keep eliminateFrameIndex without load/store offset folding (no CIL offset field); drop the GISel part. |
| `WebAssemblyRegisterInfo.h` | 57 | RegisterInfo declaration. | **ADAPT** | Rename and trim. |
| `WebAssemblyRegisterInfo.td` | 72 | Nominal registers and register classes. | **ADAPT** | See 3.4. |
| `WebAssemblyReplacePhysRegs.cpp` | 123 | Replaces SP/FP physregs with vregs after PEI. | **COPY** | Rename only. |
| `WebAssemblyRuntimeLibcallSignatures.cpp` | 980 | Hand table: RTLIB libcall to wasm signature (176 wasm:: references). | **ADAPT** | Rewrite on MVTs, or use RuntimeLibcallsInfo::getFunctionTy (RuntimeLibcalls.h:196). |
| `WebAssemblyRuntimeLibcallSignatures.h` | 39 | Declarations for the above. | **ADAPT** | As above. |
| `WebAssemblySelectionDAGInfo.cpp` | 89 | memcpy/memmove/memset to bulk-memory nodes. | **ADAPT** | memcpy to cpblk, memset to initblk, memmove to a call. |
| `WebAssemblySelectionDAGInfo.h` | 62 | SDNode enum (generated plus CALL/RET_CALL) and DAG info declaration. | **ADAPT** | Keep CALL; drop RET_CALL. |
| `WebAssemblySetP2AlignOperands.cpp` | 110 | Fills p2align immediates from MachineMemOperands. | **ADAPT** | Becomes MSILSetMemFlags: 'unaligned.'/'volatile.' prefix flags from MMOs. |
| `WebAssemblySortRegion.cpp` | 78 | Loop/exception region abstraction for CFGSort/CFGStackify. | **DROP** | Goes with CFGSort/CFGStackify. |
| `WebAssemblySortRegion.h` | 91 | Header for the above. | **DROP** | As above. |
| `WebAssemblySubtarget.cpp` | 97 | Subtarget constructor: feature parsing, GISel objects. | **ADAPT** | No GISel; ignore native CPU/feature strings in M1 (section 7). |
| `WebAssemblySubtarget.h` | 153 | Feature flags and accessors. | **ADAPT** | Profile flags only (net48/net8 later). |
| `WebAssemblyTargetMachine.cpp` | 556 | Target and pass registration, TargetPassConfig pipeline, MIR YAML. | **ADAPT** | M1 pipeline in section 5.2. |
| `WebAssemblyTargetMachine.h` | 84 | TargetMachine declaration (NewPM hooks, wasm cl::opts). | **ADAPT** | Drop NewPM hooks and EH/SjLj options. |
| `WebAssemblyTargetObjectFile.cpp` | 24 | Derives TargetLoweringObjectFileWasm. | **ADAPT** | Derive plain TargetLoweringObjectFile with one section (SPIRV precedent). |
| `WebAssemblyTargetObjectFile.h` | 30 | Declaration for the above. | **ADAPT** | As above. |
| `WebAssemblyTargetTransformInfo.cpp` | 632 | TTI costs (mostly SIMD) and unrolling preferences. | **ADAPT** | Minimal stub in M1; IL-size-aware inline cost later (Design risk 1). |
| `WebAssemblyTargetTransformInfo.h` | 126 | TTI declaration. | **ADAPT** | As above. |
| `WebAssemblyUtilities.cpp` | 217 | isChild, mayThrow, getCalleeOp, table symbols, copy opcode, TLS base. | **ADAPT** | Keep isChild, getCalleeOp, getCopyOpcodeForRegClass, canLowerReturn; drop tables/EH/TLS. |
| `WebAssemblyUtilities.h` | 104 | Declarations for the above plus the ABI enum. | **ADAPT** | As above. |

### 2.3 The GlobalISel footprint (all DROP)

This checkout's WebAssembly target carries a GlobalISel path next to SelectionDAG. MSIL uses
SelectionDAG only (Design 2), so none of this is copied:
- `GISel/`: 9 files, 1,815 lines.
- `WebAssemblyGISel.td` (84), `WebAssemblyRegisterBanks.td` (22), `WebAssemblyCombine.td` (22),
  and three tablegen invocations (`CMakeLists.txt:17-22`, `-gen-global-isel`,
  `-gen-global-isel-combiner` x2) plus `-gen-register-bank` (`CMakeLists.txt:12`).
- Hooks in files that MSIL does adapt (delete them while adapting):
  - `WebAssembly.h:18,20,109-131,352-353`: InstructionSelector factory, combiner passes.
  - `WebAssemblySubtarget.h:24-27,75-78,145-148` and `WebAssemblySubtarget.cpp:16-18,59-66,82-97`:
    CallLowering, Legalizer, RegBankInfo, InstSelector members.
  - `WebAssemblyTargetMachine.cpp:23-26` (includes), `83-85` (`initializeGlobalISel` and the
    combiners), `200` (`LLT::setUseExtended(true)`), `260-265` and `498-536` (six
    TargetPassConfig overrides).
  - `WebAssemblyRegisterInfo.cpp:15` (include) and `152-206` (`getRegClassForBank`,
    `getConstrainedRegClassForReg`).
  - `WebAssemblyCodeGenPassBuilder.cpp` (19 references, file DEFERred anyway).
  - `CMakeLists.txt:91` (`GlobalISel` link component).

### 2.4 How big M1 really is

| | Files | Source lines in WebAssembly | Expected lines in MSIL M1 |
|---|---:|---:|---:|
| COPY | 14 | 3,694 | about 3,650 (RegStackify and ExplicitLocals lose about 60 lines of V128/reference-type cases) |
| ADAPT | 58 | 15,630 | about 7,800 (my per-file estimate; the big cuts: ISelLowering 4,091 to about 1,300, RuntimeLibcallSignatures 980 to 100-300, TTI 632 to about 60, MCTargetDesc.h 627 to about 200) |
| New files | 5 | | about 600: `MSILISelLoweringCall.cpp` (split out of ISelLowering, counted there), `MSILStackCheck.cpp` (`passes-frame.md` 8, about 250), `MCTargetDesc/MSILMCExpr.{h,cpp}` (about 200), `MSILInstrFrame.td`; plus lit configs |
| **M1 total** | **75** (72 - 2 folded Utils files + 5 new) | | **about 12,000** |

The estimates are mine and unverified. Treat them as relative sizes.

---

## 3. TableGen design

### 3.1 How WebAssembly's `.td` files fit together

- `WebAssembly.td` (186): `include "llvm/Target/Target.td"` (line 19), 19 SubtargetFeatures
  (25-101), `include "WebAssemblyRegisterInfo.td"` and `"WebAssemblyRegisterBanks.td"`
  (111-112), `include "WebAssemblyInstrInfo.td"` (118), the pointer register class by
  HwMode (`WASM64`, `wasm_ptr_rc`, 120-124) and
  `defm : RemapAllTargetPseudoPointerOperands<wasm_ptr_rc>;` (126). Then processors (135-164),
  AsmParser/AsmWriter (170-180) and `def WebAssembly : Target` (182-186).
- `WebAssemblyInstrInfo.td` (485): feature predicates (18-104), target SDTypeProfiles and
  SDNodes (110-157), operand definitions (163-251), three `InstrMapping`s (267-300),
  `include "WebAssemblyInstrFormats.td"` (306), ARGUMENT/LOCAL/CONST multiclasses (312-429),
  global-address patterns (431-469), and 11 per-area includes (475-485).
- Per-area files and their `defm` counts (a proxy for size): Integer 42, Float 28, Conv 46,
  Memory 65, Control 35, Call 11, Atomics 133, SIMD 303, Ref 10, BulkMemory 11, Table 9.
- Two TableGen facilities in this checkout matter for MSIL:
  - **Pointer operand remapping is mandatory.** `llvm/include/llvm/Target/Target.td:909-914`:
    `ptr_rc` is a placeholder, and "an unremapped ptr_rc reaching the instruction-info emitter
    is an error". Every target has a `defm : RemapAllTargetPseudoPointerOperands<...>;` line
    (AArch64.td:43, ARM.td:47, BPF.td:17, ...). MSIL M1:
    `defm : RemapAllTargetPseudoPointerOperands<I64>;`.
  - **`-gen-sd-node-info` generates the ISD enum.** Every `SDNode<"WebAssemblyISD::X", ...>`
    in any included `.td` becomes an enum entry (`WebAssemblySelectionDAGInfo.h:20-21`,
    `GET_SDNODE_ENUM`). Nodes created and selected only in C++ (`CALL`, `RET_CALL`) are
    appended by hand after `GENERATED_OPCODE_END` (`WebAssemblySelectionDAGInfo.h:26-29`). So
    each MSIL area can define its own SDNodes in its own `.td` file without touching a shared
    C++ enum.

### 3.2 The register/stack dual-form scheme

What it is (`WebAssemblyInstrFormats.td`, `WebAssemblyInstrInfo.td`):
- `class WebAssemblyInst<bits<32> inst, string asmstr, bit stack, bit is64>` (InstrFormats.td:17)
  derives from `StackRel, RegisterRel, Wasm64Rel, Instruction`. The marker classes select
  records for the InstrMappings. The fields are `Inst` (the binary encoding), `StackBased`,
  `BaseName = NAME`, `IsWasm64`, `Wasm32Name` and `IsCanonical`.
- `class NI<...>` (33-40) sets the operand lists and the pattern, and
  `let Defs = [ARGUMENTS]` (39). That last line is what keeps ARGUMENT pseudos ordered at
  function entry.
- `multiclass I<oops_r, iops_r, oops_s, iops_s, pattern_r, asmstr_r, asmstr_s, inst, is64>`
  (55-62) defines **two** records per instruction:
  - `def ""`: the **register form**. It has virtual-register operands and the ISel pattern,
    is `isCodeGenOnly = 1` (58), and its asm string names the registers
    (`"i32.add\t$dst, $lhs, $rhs"`).
  - `def _S`: the **stack form**. It has no register operands (only immediates), no pattern,
    `StackBased = 1`, `BaseName = NAME` (60-61), and the real wasm text (`"i32.add"`).
- `multiclass NRI` (65-68) is used when there are no register operands: both forms share one
  operand list.
- InstrMappings (InstrInfo.td): `getStackOpcode` (268-274, row `BaseName`, column
  `StackBased` 0 to 1), `getRegisterOpcode` (281-287, 1 to 0), `getWasm64Opcode` (294-300,
  A32 to A64 memory forms). TargetInfo declares them (`WebAssemblyTargetInfo.h`) because both
  CodeGen and MC need them.

How it is used:
- Every MachineInstr pass (RegStackify, ExplicitLocals, ...) sees only register forms.
- `WebAssemblyMCInstLower::lower` (MCInstLower.cpp:218-365) lowers all operands. Then
  `removeRegisterOperands` (367-397) switches the opcode to `getStackOpcode(RegOpcode)` (386)
  and erases every register operand, unless `-wasm-keep-registers` is set (MCInstLower.cpp:46-51).
- The whole MC layer works on `_S` forms. The InstPrinter prints `_S` asm strings, the
  MCCodeEmitter encodes them, and the AsmParser matches them; register forms are
  `isCodeGenOnly`, so the matcher ignores them. The Disassembler decodes into `_S`, with
  `IsCanonical` breaking ties.
- C++ outside the AsmParser and Disassembler that names `_S` opcodes or the mappings:
  `MCTargetDesc.h` (99 lines, the predicate helpers list both forms), `AsmPrinter.cpp` (18,
  `CATCH*_S`), `InstPrinter.cpp` (16), `Utilities.cpp` (7, `CALL_S`, `THROW_S`, ...),
  `MCCodeEmitter.cpp` (5), TargetInfo (4), `MCInstLower.cpp` (1).
- Cost: every instruction exists twice in the generated tables, each `defm` has 9
  parameters, and every instruction needs two asm strings.

### 3.3 Recommendation: one record per instruction

Do not keep the dual-form scheme in M1:
- **Nothing in M1 consumes stack forms.** The M1 MC layer is text only: the TableGen AsmWriter
  and the InstPrinter. ilasm assembles and ildasm disassembles, so there is no AsmParser and
  no Disassembler. `registry-tm.md` 2.3 also says not to create `AsmParser/` before phase 2.
  The MCCodeEmitter comes in phase 2.
- **The phase-2 binary encoder does not need them either.** `WebAssemblyMCCodeEmitter.cpp:142-145`
  already loops over the MCInst and skips `MO.isReg()` operands. A CIL encoder can do the
  same on single-form MCInsts.
- **No textual reuse is lost.** Every asm string has to be rewritten for CIL anyway, so
  keeping the 9-parameter `I` multiclass saves nothing.
- **MI-level names stay the same** (`ADD_I32`, `CONST_I64`, `COPY_I32`, `TEE_I32`,
  `LOCAL_GET_I32`, `DROP_I32`, `BR_IF`, `CALL`, ...), so the COPY passes compile unchanged.

How MC lowering works without stack forms:
- MSILMCInstLower keeps every operand in its position. A register operand becomes a
  placeholder `MCOperand::createReg(MCRegister())`, or the WAReg number for a debug print
  mode. AsmStrings name only immediate and symbol operands (`"ldc.i4\t$imm"`), so the operand
  indices TableGen computes stay correct.
- The variadic instructions get hand-written printing in `MSILInstPrinter::printInst`.
  WebAssembly already special-cases `CALL_INDIRECT` (InstPrinter.cpp:54-87) and variadic
  operands (89-116). The MSIL cases:
  - `CALL`: the callee is the first non-register operand. Its index equals the number of
    defs because of `variadicOpsAreDefs`.
  - `CALL_INDIRECT`: the signature operand.
  - `RETURN`: print `ret` and ignore the operands.
  - `BR_TABLE_*`: the MBB operands print as `switch (L0, L1, ...)` and then `br Ldefault`.
- `MO_MachineBasicBlock` operands are lowered to `MCSymbolRefExpr`s of the MBB symbol.
  WebAssembly asserts instead (MCInstLower.cpp:239-241), because CFGStackify has already
  turned branch targets into depths.

`MSILInstrFormats.td` (owner WP0):

```tablegen
// One record per CIL instruction; no register/stack twins (recon wasm-inventory 3.3).
class MSILInst<bits<16> cilop, string asmstr> : Instruction {
  bits<16> Inst = cilop;   // CIL opcode, two-byte opcodes as 0xFExx; 0xFFFF = pseudo or multi-opcode
  int StackPeakExtra = 0;  // transient extra stack depth of multi-opcode expansions (passes-frame.md 8.2)
  let Namespace = "MSIL";
  let Pattern = [];
  let AsmString = asmstr;
}

class NI<dag oops, dag iops, list<dag> pattern, string asmstr = "",
         bits<16> cilop = 0xFFFF> : MSILInst<cilop, asmstr> {
  dag OutOperandList = oops;
  dag InOperandList = iops;
  let Pattern = pattern;
  let Defs = [ARGUMENTS];  // keeps ARGUMENT_* first in the entry block (WebAssemblyInstrFormats.td:39)
}

// Same name as WebAssembly's multiclass so per-area files read alike.
multiclass I<dag oops, dag iops, list<dag> pattern, string asmstr = "",
             bits<16> cilop = 0xFFFF> {
  def "" : NI<oops, iops, pattern, asmstr, cilop>;
}
```

Revisit only if phase 2 wants `llvm-mc` to parse `.il` text. `_S` twins can be added
mechanically then.

**Conflict with `isel.md` 10.1.** `isel.md` keeps the dual form: its `MSILInst` has
`StackBased`/`BaseName` and inherits `StackRel, RegisterRel`, and its `I` multiclass emits
`def ""` plus `def _S`. Its records are otherwise the same as here (same names, operands,
patterns and opcodes). The trade-off:

| | Single form (this report) | Dual form (`isel.md` 10.1) |
|---|---|---|
| Records per instruction | 1 | 2 |
| `defm` parameters | 5 | 8-9 |
| MCInstLower | Keep operand positions with register placeholders; 4 variadic opcodes printed by hand | Copy wasm's `removeRegisterOperands` + `getStackOpcode` (MCInstLower.cpp:367-397) |
| InstPrinter | AsmStrings name only immediates | `_S` AsmStrings; wasm's variadic printing code ports directly |
| Future `llvm-mc` `.il` parser or disassembler | Needs `_S` twins added later | Ready |
| Debug output with registers (`-wasm-keep-registers` style) | Needs an extra print mode | Free (register-form AsmStrings) |

Both work. The single form is less TableGen; the dual form is closer to upstream
WebAssembly and makes a later assembler cheaper. Pick one in WP0 (6.1) and apply it
everywhere.

### 3.4 Register classes for M1

`MSILRegisterInfo.td` (owner WP0):

```tablegen
class MSILReg<string n> : Register<n> { let Namespace = "MSIL"; }
class MSILRegClass<list<ValueType> regTypes, int alignment, dag regList>
    : RegisterClass<"MSIL", regTypes, alignment, regList>;

// Frame base: defined by the localloc prologue, replaced by a vreg after PEI
// (passes-frame.md 6.1-6.2). M1 has no SP.
def FP : MSILReg<"%FP">;

// Register classes need at least one register (same reason as
// WebAssemblyRegisterInfo.td:35-42).
def I32_0 : MSILReg<"%i32.0">;
def I64_0 : MSILReg<"%i64.0">;
def F32_0 : MSILReg<"%f32.0">;
def F64_0 : MSILReg<"%f64.0">;

def VALUE_STACK : MSILReg<"STACK">;      // RegStackify ordering token (WebAssemblyRegisterInfo.td:52)
def ARGUMENTS   : MSILReg<"ARGUMENTS">;  // pins ARGUMENT_* at entry (WebAssemblyRegisterInfo.td:57)

def I32 : MSILRegClass<[i32], 32, (add I32_0)>;
def I64 : MSILRegClass<[i64], 64, (add FP, I64_0)>;  // pointers live here
def F32 : MSILRegClass<[f32], 32, (add F32_0)>;
def F64 : MSILRegClass<[f64], 64, (add F64_0)>;
// Later (Design 3.2):
// def V128 : MSILRegClass<[v16i8, v8i16, v4i32, v2i64, v4f32, v2f64], 128, (add V128_0)>;
```

In `MSIL.td`, add `defm : RemapAllTargetPseudoPointerOperands<I64>;`. M1 supports 64-bit
triples only. The later `i686` triple would use a `RegClassByHwMode`, as
`WebAssembly.td:120-126` does.

**CIL type of each class.** One helper, for example
`StringRef MSIL::cilTypeName(const TargetRegisterClass *RC)`, used for `.locals`, method
signatures and `calli` signatures:

| Class | Locals, params, returns | Evaluation-stack type |
|---|---|---|
| `I32` | `int32` | int32 |
| `I64` | **`native int`** (64-bit triples) | native int |
| `F32` | `float32` | F |
| `F64` | `float64` | F |

**Why `I64` prints as `native int` rather than `int64`.** `passes-frame.md` section 11
leaves this open. The evidence is in section 8, probes `mix.il` and `nint.il`.
- SelectionDAG has no pointer MVT. A pointer and a `long` are both `i64` in MachineInstrs
  (Design 2.3), so a separate pointer register class cannot be built with SelectionDAG.
- If `I64` printed as `int64`, every pointer computation would mix `int64` and
  `native int`. For `ldc.i8 5; ldarg.0 /*native int*/; add`, PEVerify reports
  `[found Long][expected Native Int] Unexpected type on the stack`. That is a stack-type
  error, which Design 12's exit criterion forbids. An `int64` address in `ldind` gives
  `Expected ByRef [found Long]`.
- With every `I64` value typed `native int`, the same program has only
  `Expected ByRef on the stack [found Native Int]`, the expected unverifiable-pointer error.
  It returns the right result on .NET Framework 4 x64 and on .NET 8 (72 in both).
- What this means for instruction selection (all included in 3.6):
  - `CONST_I64` prints `ldc.i8 N` + `conv.i`.
  - Sign extension i32 to i64 is `conv.i`; zero extension is `conv.u`.
  - i64 loads and stores are `ldind.i` and `stind.i`.
  - fp to i64 is `conv.i` or `conv.u`.
  - i64 to fp: `conv.r4` and `conv.r8` accept native int; unsigned uses `conv.r.un`
    (verified on native int, `misc.il`).
  - Shifts take a native int value with an int32 amount (verified, `nint.il`). So
    `getScalarShiftAmountTy` returns `i32` for all types up to 64 bits. WebAssembly returns
    the value's own width (ISelLowering.cpp:493-505).
- Boundaries:
  - P/Invoke signatures declare 64-bit C integers as `native int`. Same size and same
    register class on every 64-bit native ABI.
  - A BCL call that requires `int64` gets an explicit `conv.i8` (for example
    `Interlocked.Add(ref long, long)`, phase 3).
  - C#-facing exports (Design 5.4) can convert in their wrappers.
- 32-bit triples (later): pointers become i32. CIL allows int32 with native int in binary
  operations, but an address must be native int. Revisit then.

This is a recommendation for the Design.md owner and the ISel package (WP3) to confirm.

### 3.5 Minimal `.td` file set for M1 (11 files)

| File | Owner | Contents | Derived from |
|---|---|---|---|
| `MSIL.td` | WP0 | `include "llvm/Target/Target.td"`, register info, instr info; `def MSILInstrInfo : InstrInfo;`; `def : ProcessorModel<"generic", NoSchedModel, []>;` and **no** SubtargetFeatures in M1 (`registry-tm.md` 4.4); `RemapAllTargetPseudoPointerOperands<I64>`; AsmWriter with `AsmWriterClassName = "InstPrinter"`, `PassSubtarget = 1`, `isMCAsmWriter = 1`; `def MSIL : Target` with no AssemblyParsers | `WebAssembly.td` |
| `MSILRegisterInfo.td` | WP0 | section 3.4 | `WebAssemblyRegisterInfo.td` |
| `MSILInstrFormats.td` | WP0 | section 3.3 | `WebAssemblyInstrFormats.td` |
| `MSILInstrInfo.td` | WP0 | Shared SDTypeProfiles and SDNodes (`callseq_start/end`, `MSILISD::ARGUMENT`, `RETURN`, `MSILDataAddr`, `MSILFuncAddr`), operand definitions, the ARGUMENT, COPY, TEE, LOCAL, ARG, DROP, CONST, `GLOBAL_ADDR` and `FUNC_ADDR` records (3.6.1), then `include` of the per-area files. No InstrMappings. | `WebAssemblyInstrInfo.td` |
| `MSILInstrInteger.td` | WP2 | 3.6.2 | `WebAssemblyInstrInteger.td` |
| `MSILInstrFloat.td` | WP2 | 3.6.3 | `WebAssemblyInstrFloat.td` |
| `MSILInstrConv.td` | WP2 | 3.6.4 | `WebAssemblyInstrConv.td` |
| `MSILInstrMemory.td` | WP3 | 3.6.5, including `CPBLK`/`INITBLK` (replaces `WebAssemblyInstrBulkMemory.td`) | `WebAssemblyInstrMemory.td` |
| `MSILInstrControl.td` | WP3 | 3.6.6, including the `BR_TABLE` SDNode and the `SELECT_*` pseudos | `WebAssemblyInstrControl.td` |
| `MSILInstrCall.td` | WP4 | 3.6.7 | `WebAssemblyInstrCall.td` |
| `MSILInstrFrame.td` | WP5 | 3.6.8, `LOCALLOC` and its SDNode | new (`passes-frame.md` 6.2) |

Later: `MSILInstrAtomics.td` (phase 3), `MSILInstrSIMD.td` (`V128`), `MSILInstrEH.td` (CIL
`try`/`leave`/`endfinally`, phase 3), and possibly `MSILInstrBCL.td` for BCL intrinsic calls
(3.6.3).

TableGen invocations in `CMakeLists.txt`: 6 (`-gen-asm-writer`, `-gen-dag-isel`,
`-gen-instr-info`, `-gen-register-info`, `-gen-sd-node-info`, `-gen-subtarget`), as in
`registry-tm.md` 2.4. Dropped compared with WebAssembly: `-gen-asm-matcher`,
`-gen-disassembler`, `-gen-fast-isel`, `-gen-emitter`, `-gen-register-bank`,
`-gen-global-isel` and both `-gen-global-isel-combiner` runs.

### 3.6 M1 instruction list

All CIL opcode values were checked against `System.Reflection.Emit.OpCodes` (section 8).
"Inst" is the value for the `Inst` field; a multi-opcode record keeps `0xFFFF` and its
expansion is listed. `RC` stands for each class the row applies to. Record names reuse
WebAssembly names wherever the semantics match.

`isel.md` (sections 5, 9 and 10) covers the same instructions in more depth, verified at
run time on .NET 8. It is the authority for semantics. These are the places where it goes
further than this baseline or settles something left open here:
- **Negated compares.** `isel.md` builds them at ISel from single-opcode MIs
  (`setne` becomes `CEQ(CEQ a, b), 0`), not through `setCondCodeAction(..., Expand)`. The
  printed code is the same.
- **Fused compare-and-branch in M1.** `brcond(setcc)` selects `beq`, `bne.un`,
  `blt[.un]`, ... (`isel.md` decision 5). Here that is listed as optional.
- **Variable-length `memcpy`/`memset`/`memmove`** call `NativeMemory.Copy`/`Fill`.
  Constant lengths up to 2^32-1 use `cpblk`/`initblk` (`isel.md` decision 8). `isel.md`
  verified that `cpblk`/`initblk` accept a native int size, which settles the question
  raised in 3.6.5.
- **`uitofp` i64 to f32 is Custom.** `isel.md` verified that `conv.r.un; conv.r4`
  double-rounds on .NET 8 and .NET Framework (fixed in .NET 10), which settles the question
  raised in 3.6.4.
- **BCL intrinsics on the net8.0 profile** for `ctlz`, `cttz`, `ctpop`, `bswap`, rotates,
  `fabs`, `fsqrt`, rounding, `fcopysign`, `fma`, min/max and bitcasts (`isel.md`
  decision 11). This report lists those as Expand, which is what the net48 profile does.

#### 3.6.1 Core (`MSILInstrInfo.td`, WP0)

| Record(s) | outs / ins | CIL text | Inst | Produced by |
|---|---|---|---|---|
| `ARGUMENT_i32/_i64/_f32/_f64` | `(outs RC:$res) (ins i32imm:$argno)` | none, erased by ExplicitLocals | pseudo | `(MSILargument timm:$argno)` from LowerFormalArguments. `hasSideEffects = 1, Defs = [], Uses = [ARGUMENTS]` (WebAssemblyInstrInfo.td:312-325) |
| `COPY_<RC>` | `(outs RC:$res) (ins RC:$src)` | none, coalesced by ExplicitLocals | pseudo | `copyPhysReg`, `getCopyOpcodeForRegClass` |
| `TEE_<RC>` | `(outs RC:$res, RC:$also) (ins RC:$src)` | none, becomes `LOCAL_TEE`/`ARG_TEE` | pseudo | RegStackify |
| `LOCAL_GET_<RC>` | `(outs RC:$res) (ins local_op:$local)` | `ldloc $local` | 0xFE0C | ExplicitLocals, local index >= NumParams, printed as index - NumParams (`passes-frame.md` 7) |
| `LOCAL_SET_<RC>` | `(outs) (ins local_op:$local, RC:$src)` | `stloc $local` | 0xFE0E | ExplicitLocals |
| `LOCAL_TEE_<RC>` | `(outs RC:$res) (ins local_op:$local, RC:$src)` | `dup` then `stloc $local`, `StackPeakExtra = 1` | 0xFFFF (0x25 + 0xFE0E) | ExplicitLocals |
| `ARG_GET_<RC>` (new) | `(outs RC:$res) (ins arg_op:$arg)` | `ldarg $arg` | 0xFE09 | ExplicitLocals, local index < NumParams |
| `ARG_SET_<RC>` (new) | `(outs) (ins arg_op:$arg, RC:$src)` | `starg $arg` | 0xFE0B | ExplicitLocals, when an argument vreg is redefined after coalescing or coloring |
| `ARG_TEE_<RC>` (new) | `(outs RC:$res) (ins arg_op:$arg, RC:$src)` | `dup` then `starg $arg`, `StackPeakExtra = 1` | 0xFFFF | ExplicitLocals |
| `DROP_<RC>` | `(outs) (ins RC:$src)` | `pop` | 0x26 | ExplicitLocals |
| `CONST_I32` | `(outs I32:$res) (ins i32imm_op:$imm)` | `ldc.i4 $imm` | 0x20 | `imm`. `isMoveImm, isAsCheapAsAMove, isReMaterializable` (WebAssemblyInstrInfo.td:412-429) |
| `CONST_I64` | `(outs I64:$res) (ins i64imm_op:$imm)` | `ldc.i8 $imm` then `conv.i` | 0xFFFF (0x21 + 0xD3) | `imm` |
| `CONST_F32` | `(outs F32:$res) (ins f32imm_op:$imm)` | `ldc.r4 float32(0xXXXXXXXX)` | 0x22 | `fpimm`. The bit-exact form keeps NaN payloads and -0.0 (ilasm accepts it, section 8) |
| `CONST_F64` | `(outs F64:$res) (ins f64imm_op:$imm)` | `ldc.r8 float64(0xXXXXXXXXXXXXXXXX)` | 0x23 | `fpimm` |
| `GLOBAL_ADDR` (new; name as in `isel.md` 10.4) | `(outs I64:$res) (ins data_op:$sym)` | `ldsfld native int <data base field>`, `ldc.i4 <offset>`, `add`; `StackPeakExtra = 1`. The AsmPrinter supplies the offset from its module layout (Design 4.1) | 0xFFFF | `(MSILDataAddr tglobaladdr:$sym)` from LowerGlobalAddress for non-functions |
| `FUNC_ADDR` (new) | `(outs I64:$res) (ins method_op:$fn)` | `ldftn <method-ref>` | 0xFE06 | `(MSILFuncAddr tglobaladdr:$fn)` for functions (Design 12: `ldftn` + `calli` in M1) |

Not ported:
- WebAssembly's `GLOBAL_GET_*`/`GLOBAL_SET_*` (InstrInfo.td:376-386). MSIL's equivalent
  would be `ldsfld`/`stsfld` for the phase-3 shadow-stack pointer and TLS block. M1 does not
  need them, because `FP` comes from `localloc` (`passes-frame.md` 6.1).
- The PIC and TLS `Wrapper` patterns (InstrInfo.td:431-469).

#### 3.6.2 Integer (`MSILInstrInteger.td`, WP2). `RC` is `I32` and `I64`

| Records | Pattern | CIL | Inst | Notes |
|---|---|---|---|---|
| `ADD_x`, `SUB_x`, `MUL_x` | `add`, `sub`, `mul` | `add`, `sub`, `mul` | 0x58, 0x59, 0x5A | `isCommutable` as in wasm |
| `DIV_S_x`, `DIV_U_x` | `sdiv`, `udiv` | `div`, `div.un` | 0x5B, 0x5C | `hasSideEffects = 1` (throws on zero), as WebAssemblyInstrInteger.td:55-60 |
| `REM_S_x`, `REM_U_x` | `srem`, `urem` | `rem`, `rem.un` | 0x5D, 0x5E | same |
| `AND_x`, `OR_x`, `XOR_x` | `and`, `or`, `xor` | `and`, `or`, `xor` | 0x5F, 0x60, 0x61 | |
| `SHL_x`, `SHR_S_x`, `SHR_U_x` | `shl`, `sra`, `srl` with an `I32` amount | `shl`, `shr`, `shr.un` | 0x62, 0x63, 0x64 | amount type i32 (3.4) |
| `NEG_x` (optional) | `(sub 0, $x)` | `neg` | 0x65 | |
| `EQ_x` | `seteq` | `ceq` | 0xFE01 | result `I32` |
| `GT_S_x`, `GT_U_x` | `setgt`, `setugt` | `cgt`, `cgt.un` | 0xFE02, 0xFE03 | |
| `LT_S_x`, `LT_U_x` | `setlt`, `setult` | `clt`, `clt.un` | 0xFE04, 0xFE05 | |

- `NE`, `GE` and `LE` (signed and unsigned) are not instructions. The legalizer inverts them
  (3.7).
- No `ROTL`/`ROTR`/`CLZ`/`CTZ`/`POPCNT`/`EQZ`/`SELECT`/wide-arithmetic records
  (WebAssemblyInstrInteger.td:69-70, 85-94, 117-183).
- RegStackify keeps the DIV/REM opcodes in its `query()` list
  (WebAssemblyRegStackify.cpp:188-195, 223-230) and drops the trapping-conversion entries
  there, because CIL conversions do not trap.

#### 3.6.3 Floating point (`MSILInstrFloat.td`, WP2). `RC` is `F32` and `F64`

| Records | Pattern | CIL | Inst | Notes |
|---|---|---|---|---|
| `ADD_x`, `SUB_x`, `MUL_x`, `DIV_x` | `fadd`, `fsub`, `fmul`, `fdiv` | `add`, `sub`, `mul`, `div` | 0x58-0x5B | |
| `REM_x` | `frem` | `rem` | 0x5D | Bit-identical to C `fmod` on .NET Framework 4 x64 and .NET 8 in 4 cases, including `1e300 rem 3` and a subnormal divisor (section 8). So `FREM` is Legal and needs no `fmod` libcall. |
| `NEG_x` | `fneg` | `neg` | 0x65 | |
| `EQ_x` | `setoeq`, plus `seteq` | `ceq` | 0xFE01 | |
| `GT_x` | `setogt`, plus `setgt` | `cgt` | 0xFE02 | |
| `LT_x` | `setolt`, plus `setlt` | `clt` | 0xFE04 | |
| `GT_U_x` | `setugt` | `cgt.un` | 0xFE03 | true when unordered |
| `LT_U_x` | `setult` | `clt.un` | 0xFE05 | true when unordered |

- The other FP condition codes come from legalizer inversion (3.7). I checked
  `TargetLowering::LegalizeSetCCCondCode` (TargetLowering.cpp:14249-14369): it tries a swap,
  then an inversion, then an inversion plus a swap, and it expands `SETO`/`SETUO` through
  `SETOEQ`. All of those are available with this set.
- `sqrt`, `abs`, `copysign`, rounding and `min`/`max` have no CIL opcode. Two options:
  - **M1 (recommended):** Expand them to libcalls, and have the AsmPrinter map known libm
    names to BCL methods (for example `call float64 [System.Runtime]System.Math::Sqrt(float64)`).
  - **Later:** "BCL intrinsic" records whose AsmString is that `call`. They are pure, so
    RegStackify can move them.

  Either way the printer needs the profile's core-library name: `[mscorlib]` for net48,
  `[System.Runtime]` for net8.0 (Design 7.1).
- Float32 precision: CIL computes on type F, and ECMA-335 allows more precision than float32
  in locals and on the stack. RyuJIT on x64 uses single-precision SSE for float32 operands,
  but I have not verified that. The strict fix would be `conv.r4` after each F32 operation.
  Section 7.

#### 3.6.4 Conversions (`MSILInstrConv.td`, WP2)

| Records | Pattern | CIL | Inst |
|---|---|---|---|
| `I32_WRAP_I64` | `trunc` I64 to I32 | `conv.i4` | 0x69 |
| `I64_EXTEND_S_I32` | `sext` | `conv.i` | 0xD3 |
| `I64_EXTEND_U_I32` | `zext`, `anyext` | `conv.u` | 0xE0 |
| `I32_EXTEND8_S_I32`, `I32_EXTEND16_S_I32` | `sext_inreg` i8, i16 | `conv.i1`, `conv.i2` | 0x67, 0x68 |
| `I64_EXTEND8_S_I64`, `I64_EXTEND16_S_I64`, `I64_EXTEND32_S_I64` | `sext_inreg` i8, i16, i32 | `conv.i1`/`conv.i2`/`conv.i4`, each followed by `conv.i` | 0xFFFF |
| `F32_CONVERT_S_I32`, `F32_CONVERT_S_I64` | `sint_to_fp` | `conv.r4` | 0x6B |
| `F64_CONVERT_S_I32`, `F64_CONVERT_S_I64` | `sint_to_fp` | `conv.r8` | 0x6C |
| `F64_CONVERT_U_I32`, `F64_CONVERT_U_I64` | `uint_to_fp` | `conv.r.un` (then `conv.r8`) | 0x76 |
| `F32_CONVERT_U_I32`, `F32_CONVERT_U_I64` | `uint_to_fp` | `conv.r.un` then `conv.r4` | 0xFFFF. For the i64 source this double-rounds on .NET 8 and .NET Framework (verified by `isel.md`), so make `uint_to_fp` i64 to f32 Custom (`isel.md` decision 9). The i32 source is exact. |
| `I32_TRUNC_S_F32`, `I32_TRUNC_S_F64` | `fp_to_sint` | `conv.i4` | 0x69 |
| `I32_TRUNC_U_F32`, `I32_TRUNC_U_F64` | `fp_to_uint` | `conv.u4` | 0x6D |
| `I64_TRUNC_S_F32`, `I64_TRUNC_S_F64` | `fp_to_sint` | `conv.i` | 0xD3 |
| `I64_TRUNC_U_F32`, `I64_TRUNC_U_F64` | `fp_to_uint` | `conv.u` | 0xE0 |
| `F64_PROMOTE_F32` | `fpextend` | `conv.r8` | 0x6C |
| `F32_DEMOTE_F64` | `fpround` | `conv.r4` | 0x6B |

- A CIL float-to-int conversion that is out of range gives an unspecified value and does not
  trap. That matches LLVM's poison. So MSIL needs neither the `FP_TO_*` custom-inserter
  pseudos (WebAssemblyInstrConv.td:111-136) nor the trapping variants.
- Bitcasts (the `REINTERPRET` records, Conv.td:225-240) have no CIL opcode. M1 expands them
  through a stack temporary (`LegalizeDAG.cpp:3453-3457`, `EmitStackConvert`).

#### 3.6.5 Memory (`MSILInstrMemory.td`, WP3)

Every load and store takes `memflags:$flags` (an immediate) and `I64:$addr`. The flags print
as the `unaligned. N` and `volatile.` prefixes. MSILSetMemFlags, the adaptation of
SetP2AlignOperands, fills them from the MachineMemOperand. `isel.md` 10.6 instead fills
them during ISel with a root-aware ComplexPattern. Either works; with the ISel approach,
`MSILSetMemFlags.cpp` drops out of the layout in 5.1. There is no offset operand: CIL has
none (`passes-frame.md` 11).

| Records | Pattern | CIL | Inst |
|---|---|---|---|
| `LOAD_I32` | `load` i32 | `ldind.i4` | 0x4A |
| `LOAD8_S_I32`, `LOAD8_U_I32` | `sextloadi8`; `zextloadi8` and `extloadi8` | `ldind.i1`, `ldind.u1` | 0x46, 0x47 |
| `LOAD16_S_I32`, `LOAD16_U_I32` | same for i16 | `ldind.i2`, `ldind.u2` | 0x48, 0x49 |
| `LOAD_I64` | `load` i64 | `ldind.i` | 0x4D |
| `LOAD_F32`, `LOAD_F64` | `load` | `ldind.r4`, `ldind.r8` | 0x4E, 0x4F |
| `STORE_I32`, `STORE8_I32`, `STORE16_I32` | `store`, `truncstorei8`, `truncstorei16` | `stind.i4`, `stind.i1`, `stind.i2` | 0x54, 0x52, 0x53 |
| `STORE_I64` | `store` i64 | `stind.i` | 0xDF |
| `STORE_F32`, `STORE_F64` | `store` | `stind.r4`, `stind.r8` | 0x56, 0x57 |
| `MEMCPY` | `(MSILcpblk I64:$dst, I64:$src, I64:$len)` from `EmitTargetCodeForMemcpy`, constant length up to 2^32-1 only | `cpblk` | 0xFE17 |
| `MEMSET` | `(MSILinitblk I64:$dst, I32:$val, I64:$len)`, constant length up to 2^32-1 only | `initblk` | 0xFE18 |

- Extending loads into i64 and truncating stores from i64 are Expanded (3.7). The CIL
  `ldind.*`/`stind.i1/i2/i4` opcodes work on int32.
- I recalled from ECMA-335 that the `cpblk`/`initblk` size operand is unsigned int32, but
  `isel.md` verified that a native int size works. Following `isel.md` decision 8, only
  constant lengths up to 2^32-1 use `cpblk`/`initblk`, so the `conv.ovf.u4.un` guard can go.
  Variable lengths and every `memmove` call `NativeMemory.Copy`/`Fill`.
- The probe `misc.il` assembled and ran correctly on both runtimes with `volatile.`,
  `unaligned. 1`, `stind.i`, `ldind.i`, `cpblk`, `initblk` and `conv.ovf.u4.un` (section 8).

#### 3.6.6 Control flow (`MSILInstrControl.td`, WP3)

| Records | Pattern | CIL | Inst | Notes |
|---|---|---|---|---|
| `BR` | `(br bb)` | `br $dst` | 0x38 | branch, terminator, barrier |
| `BR_IF` | `(brcond I32:$c, bb)` | `brtrue $dst` | 0x3A | |
| `BR_UNLESS` | `brcond (seteq x, 0)` and `brcond (xor bool, 1)` patterns, as WebAssemblyInstrControl.td:29-34 | `brfalse $dst` | 0x39 | A real instruction now. No LowerBrUnless pass. |
| `BR_TABLE_I32`, `BR_TABLE_I64` | `(MSILbr_table $index, bb...)`, variadic | `switch (L0, L1, ...)` then `br Ldefault` | 0x45 | The default target is the last MBB operand (WebAssemblyISelLowering.cpp:2192-2215). PEVerify accepts a native int index, and x64 compares it at full width (`passes-frame.md` 3). |
| `RETURN` | `(MSILreturn ...)`, variadic | `ret` | 0x2A | |
| `UNREACHABLE` | `(trap)` | `ldnull` then `throw`, `StackPeakExtra = 1` | 0xFFFF (0x14 + 0x7A) | Terminator and barrier. Keep `TrapUnreachable = true` (WebAssemblyTargetMachine.cpp:184-185), because CIL forbids falling off the end of a method (`passes-frame.md` 3). |
| `DEBUG_UNREACHABLE` | `(debugtrap)` | `break` | 0x01 | Not a terminator (WebAssemblyInstrControl.td:113-120 explains why) |
| `NOP` | | `nop` | 0x00 | |
| `SELECT_I32`, `SELECT_I64`, `SELECT_F32`, `SELECT_F64` | `(select I32:$c, RC:$t, RC:$f)` | none: `usesCustomInserter = 1`, and `EmitInstrWithCustomInserter` builds a diamond with `BR_IF` and a PHI | pseudo | CIL has no select. The legalizer cannot turn `ISD::SELECT` into control flow, and CodeGenPrepare's select-to-branch (CodeGenPrepare.cpp:7611-7660) runs only under optimization heuristics. |

Optional later: fused compare-and-branch (`beq`, `bne.un`, `blt`, ... 0x3B-0x44) through
patterns on `(brcond (setcc a, b, cc), bb)`.

#### 3.6.7 Calls (`MSILInstrCall.td`, WP4)

| Records | outs / ins | CIL | Inst | Notes |
|---|---|---|---|---|
| `ADJCALLSTACKDOWN`, `ADJCALLSTACKUP` | `(ins i32imm:$amt, i32imm:$amt2)` | none | pseudo | From `callseq_start`/`callseq_end`; erased by `eliminateCallFramePseudoInstr`. WebAssembly gives them `Uses`/`Defs = [SP32, SP64]` (InstrCall.td:19); MSIL has no SP in M1. |
| `CALL_PARAMS` | `(ins method_op:$callee, variable_ops)` | none | pseudo | `Select()` splits `MSILISD::CALL` into this and `CALL_RESULTS` (WebAssemblyISelDAGToDAG.cpp:425-466) |
| `CALL_RESULTS` | `(ins variable_ops)`, `variadicOpsAreDefs`, `usesCustomInserter` | none | pseudo | The custom inserter fuses the pair into `CALL` or `CALL_INDIRECT` (WebAssemblyISelLowering.cpp:786-911, `LowerCallResults`; drop the funcref, table and tail-call parts) |
| `CALL` | `(outs) (ins method_op:$callee, variable_ops)`, `variadicOpsAreDefs = 1`, `isCall` | `call <ret> <name>(<params>)` | 0x28 | |
| `CALL_INDIRECT` | `(outs) (ins sig_op:$sig, variable_ops)`; the callee pointer is the last use | `calli <ret>(<params>)` | 0x29 | WebAssembly already moves the callee pointer to the end of the arguments (ISelLowering.cpp:824-843), which is exactly CIL's `calli` operand order (verified, `probe.il`). No table operand. |

- Dropped: `RET_CALL*`, `CALL_REF*`, `RET_CALL_RESULTS`. Tail calls come later with the
  `tail.` prefix (0xFE14).
- **Method references** replace `MCSymbolWasm` signatures. Use a new
  `MCTargetExpr` subclass (`llvm/include/llvm/MC/MCExpr.h:464-488`), in
  `MCTargetDesc/MSILMCExpr.{h,cpp}`:
  - It holds the `MCSymbol` and the CIL signature (return type and parameter types).
  - It prints `int32 foo(int32, native int)`, quoting the name with ILAsm single quotes when
    needed.
  - MSILMCInstLower builds it in place of `WebAssemblyMCInstLower::GetGlobalAddressSymbol`
    (MCInstLower.cpp:71-107), using `computeSignatureVTs`.
  - A `calli` signature is built from the register classes of the defs and uses, as
    MCInstLower.cpp:274-300 does for the wasm type index.

#### 3.6.8 Frame (`MSILInstrFrame.td`, WP5)

| Record | outs / ins | CIL | Inst | Notes |
|---|---|---|---|---|
| `LOCALLOC` | `(outs I64:$dst) (ins I32:$size)` | `localloc` | 0xFE0F | `hasSideEffects = 1`, not rematerializable. RegStackify must never stackify its def: a non-empty stack at `localloc` throws `InvalidProgramException` (verified, `probe.il`). `DYNAMIC_STACKALLOC` also maps here (`passes-frame.md` 6.8). |

#### 3.6.9 Operand types and opcode predicates (`MCTargetDesc/MSILMCTargetDesc.h`, WP0)

- `enum OperandType`: `OPERAND_BASIC_BLOCK`, `OPERAND_LOCAL`, `OPERAND_ARG`,
  `OPERAND_I32IMM`, `OPERAND_I64IMM`, `OPERAND_F32IMM`, `OPERAND_F64IMM`,
  `OPERAND_METHOD`, `OPERAND_SIGNATURE`, `OPERAND_DATA`, `OPERAND_MEMFLAGS`.
  This replaces WebAssemblyMCTargetDesc.h:42-89.
- Keep these predicates from WebAssemblyMCTargetDesc.h:319-612, minus their `_S`, V128,
  reference-type, EH and atomic cases: `isScalarConst`, `isArgument`, `isCopy`, `isTee`,
  `isCallDirect`, `isCallIndirect`, `isBrTable`, `isLocalGet`, `isLocalSet`, `isLocalTee`.
  Add `isArgGet`, `isArgSet`, `isArgTee`.
- Drop `GetDefaultP2Align*` (152-289), `isMarker`, `isEndMarker`, `isTry`, `isCatch*` and the
  `WebAssemblyII` target flags (92-128). Keep `UnusedReg`/`getWARegStackId` (614-622) only if
  a register-printing debug mode is kept.

### 3.7 Operation-action table for the TargetLowering constructor

The constructor belongs to WP3, but WP0 should write it from this table so that the area
packages do not have to edit it. Only the scalar part of WebAssembly's constructor
(ISelLowering.cpp:43-447) carries over.

This is the minimal, profile-independent baseline. `isel.md` section 6 is the fuller,
verified proposal: BCL intrinsics on net8.0, Custom `uitofp` i64 to f32, Custom
`SHL_PARTS`/`SRA_PARTS`/`SRL_PARTS`, no libcalls for `memcpy`/`memset`/`memmove`, and
`MUL_I128` made unavailable. **Merge from `isel.md` section 6.**

```cpp
MSILTargetLowering::MSILTargetLowering(const TargetMachine &TM,
                                       const MSILSubtarget &STI)
    : TargetLowering(TM, STI), Subtarget(&STI) {
  const MVT PtrVT = MVT::i64; // M1: 64-bit triples only (3.4)

  setBooleanContents(ZeroOrOneBooleanContent);  // ceq/cgt/clt produce 0 or 1
  setSchedulingPreference(Sched::RegPressure);  // as WebAssembly
  addRegisterClass(MVT::i32, &MSIL::I32RegClass);
  addRegisterClass(MVT::i64, &MSIL::I64RegClass);
  addRegisterClass(MVT::f32, &MSIL::F32RegClass);
  addRegisterClass(MVT::f64, &MSIL::F64RegClass);
  computeRegisterProperties(Subtarget->getRegisterInfo());

  // Integer operations with no CIL opcode.
  for (MVT T : {MVT::i32, MVT::i64}) {
    setOperationAction({ISD::ROTL, ISD::ROTR, ISD::CTLZ, ISD::CTTZ, ISD::CTPOP,
                        ISD::BSWAP, ISD::BITREVERSE, ISD::ABS, ISD::SMIN,
                        ISD::SMAX, ISD::UMIN, ISD::UMAX, ISD::SMUL_LOHI,
                        ISD::UMUL_LOHI, ISD::MULHS, ISD::MULHU, ISD::SDIVREM,
                        ISD::UDIVREM, ISD::SHL_PARTS, ISD::SRA_PARTS,
                        ISD::SRL_PARTS, ISD::FSHL, ISD::FSHR, ISD::ADDC,
                        ISD::ADDE, ISD::SUBC, ISD::SUBE, ISD::SELECT_CC,
                        ISD::BR_CC},
                       T, Expand);
    // Only EQ, GT, UGT, LT and ULT are instructions; the others are inverted.
    setCondCodeAction({ISD::SETNE, ISD::SETGE, ISD::SETUGE, ISD::SETLE,
                       ISD::SETULE},
                      T, Expand);
  }
  // SELECT stays Legal: it matches the SELECT_* pseudos (custom inserter).

  // Floating point.
  for (MVT T : {MVT::f32, MVT::f64}) {
    setOperationAction(ISD::ConstantFP, T, Legal); // ldc.r4/ldc.r8, no constant pool
    setOperationAction(ISD::FREM, T, Legal);       // CIL 'rem' == fmod (section 8)
    setOperationAction({ISD::FSIN, ISD::FCOS, ISD::FSINCOS, ISD::FPOW, ISD::FMA,
                        ISD::FSQRT, ISD::FABS, ISD::FCOPYSIGN, ISD::FCEIL,
                        ISD::FFLOOR, ISD::FTRUNC, ISD::FRINT, ISD::FNEARBYINT,
                        ISD::FROUND, ISD::FROUNDEVEN, ISD::FMINNUM, ISD::FMAXNUM,
                        ISD::FMINIMUM, ISD::FMAXIMUM, ISD::FMINIMUMNUM,
                        ISD::FMAXIMUMNUM, ISD::FP16_TO_FP, ISD::FP_TO_FP16,
                        ISD::SELECT_CC, ISD::BR_CC},
                       T, Expand);
    // Only OEQ, OGT, OLT, UGT and ULT (plus the don't-care EQ, GT, LT) are
    // instructions.
    setCondCodeAction({ISD::SETONE, ISD::SETOGE, ISD::SETOLE, ISD::SETUEQ,
                       ISD::SETUGE, ISD::SETULE, ISD::SETUNE, ISD::SETO,
                       ISD::SETUO, ISD::SETNE, ISD::SETGE, ISD::SETLE},
                      T, Expand);
    setLoadExtAction(ISD::EXTLOAD, T, MVT::f16, Expand);
    setTruncStoreAction(T, MVT::f16, Expand);
  }
  // No reinterpret opcode: bitcasts go through a stack temporary.
  setOperationAction(ISD::BITCAST, {MVT::i32, MVT::i64, MVT::f32, MVT::f64},
                     Expand);

  // Memory.
  for (MVT T : MVT::integer_valuetypes())
    for (auto Ext : {ISD::EXTLOAD, ISD::ZEXTLOAD, ISD::SEXTLOAD})
      setLoadExtAction(Ext, T, MVT::i1, Promote);   // as WebAssembly
  for (MVT MemT : {MVT::i8, MVT::i16, MVT::i32}) {
    setLoadExtAction({ISD::EXTLOAD, ISD::ZEXTLOAD, ISD::SEXTLOAD}, MVT::i64,
                     MemT, Expand);                  // ldind.* push int32
    setTruncStoreAction(MVT::i64, MemT, Expand);     // stind.i1/i2/i4 take int32
  }
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);

  // Addresses, control flow, frame.
  setOperationAction({ISD::GlobalAddress, ISD::ExternalSymbol, ISD::JumpTable},
                     PtrVT, Custom);  // -> GLOBAL_ADDR / FUNC_ADDR / call targets
  setOperationAction({ISD::BlockAddress, ISD::GlobalTLSAddress}, PtrVT,
                     Custom);         // M1: diagnose with fail() like WebAssembly
  setOperationAction(ISD::BRIND, MVT::Other, Custom); // diagnose; IndirectBrExpand runs first
  setOperationAction(ISD::BR_JT, MVT::Other, Custom); // -> MSILISD::BR_TABLE
  setOperationAction(ISD::FrameIndex, PtrVT, Custom);
  setOperationAction(ISD::CopyToReg, MVT::Other, Custom); // FP handling, as WebAssembly
  setOperationAction(ISD::DYNAMIC_STACKALLOC, PtrVT, Custom); // passes-frame.md 6.8
  setOperationAction({ISD::STACKSAVE, ISD::STACKRESTORE}, MVT::Other, Custom);
  setOperationAction({ISD::TRAP, ISD::DEBUGTRAP}, MVT::Other, Legal);
  setOperationAction(ISD::BUILD_PAIR, MVT::i64, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);
  // SIGN_EXTEND_INREG from i8/i16/i32 stays Legal (conv.i1/conv.i2/conv.i4).

  // Variadics: the native va_list layout (Design 4.7). M1 may fail() here.
  setOperationAction({ISD::VASTART, ISD::VACOPY}, MVT::Other, Custom);
  setOperationAction({ISD::VAARG, ISD::VAEND}, MVT::Other, Expand);

  setMinimumJumpTableEntries(2);       // as WebAssembly; RyuJIT builds the table
  setMaxAtomicSizeInBitsSupported(0);  // M1: no atomics (phase 3)
}
```

Hooks that change from WebAssembly:
- `getScalarShiftAmountTy` returns `MVT::i32` up to 64 bits.
- `getSetCCResultType` returns `MVT::i32`.
- `isLegalAddressingMode` allows only a base register, because loads and stores have no
  offset field.
- `allowsMisalignedMemoryAccesses` returns true, with the `unaligned.` prefix.
- `CanLowerReturn` allows at most one return value (`passes-frame.md` 7).
- `createFastISel` is not overridden.
- `shouldExpandAtomicRMWInIR` is not needed in M1.

I checked these APIs against this checkout: the `ArrayRef` overloads of
`setOperationAction`/`setLoadExtAction`/`setCondCodeAction` (TargetLowering.h:2772, 2777,
2794, 2892, 2906), `BITCAST` expansion (LegalizeDAG.cpp:3453) and `FSQRT` to a libcall
(LegalizeDAG.cpp:4848). Whether every listed `Expand` has a generic expansion was **not**
built or tested. WP3 confirms by building.

---

## 4. Copy-and-rename feasibility

### 4.1 Experiment

In the scratchpad (`recon/wasm-inventory/MSIL/`): copy the directory, rename files
(`WebAssembly` and `Wasm` to `MSIL` in names), then rewrite contents:

```sh
sed -e 's/WebAssembly/MSIL/g' -e 's/WEBASSEMBLY/MSIL/g' \
    -e 's/Wasm/MSIL/g' -e 's/WASM/MSIL/g' -e 's/\bwasm/msil/g'
```

Then grep for what no longer resolves:

| Broken `#include` after rename | Lines |
|---|---:|
| `llvm/BinaryFormat/MSIL.h` (was `BinaryFormat/Wasm.h`) | 13 |
| `llvm/MC/MCSymbolMSIL.h` | 11 |
| `llvm/IR/IntrinsicsMSIL.h` (was the generated `IntrinsicsWebAssembly.h`) | 7 |
| `llvm/MC/MCSectionMSIL.h` | 5 |
| `llvm/CodeGen/MSILEHInfo.h` | 2 |
| `llvm/MC/MCMSILObjectWriter.h` | 2 |
| `llvm/BinaryFormat/MSILRelocs.def` | 1 |
| `llvm/CodeGen/MSILEHPrepare.h` | 1 |
| `llvm/MC/MCAsmInfoMSIL.h` | 1 |
| **total** | **43** |

| Broken identifiers after rename | Files | Lines |
|---|---:|---:|
| `msil::` (was `wasm::ValType`, `wasm::WasmSignature`, `WASM_*` constants) | 25 | 528 |
| `MCSymbolMSIL` | 18 | 102 |
| `Intrinsic::msil_*` | 6 | 42 |
| `ExceptionHandling::MSIL` | 6 | 15 |
| `MCSectionMSIL` | 5 | 15 |
| `getMSILSection`, `createMSILSignature`, `MCContext::IsMSIL` | 4 | 11 |
| `CallingConv::MSIL_*` (was `WASM_EmscriptenInvoke`) | 3 | 4 |
| `MCMSILObjectTargetWriter` | 2 | 4 |
| `MCAsmInfoMSIL` | 2 | 3 |
| `Triple::msil32/msil64` | 2 | 3 |
| `TargetLoweringObjectFileMSIL` | 2 | 2 |
| `createMSILEHPass`, `MSILEHPreparePass` | 2 | 2 |
| **any of the above** | **47** | **752** |

Per file, by M1 class:
- **ADAPT, 30 files:** RuntimeLibcallSignatures.cpp 176, AsmPrinter.cpp 61, TargetStreamer.h 35,
  ISelLowering.cpp 33, MCTypeUtilities.cpp 32, ISelDAGToDAG.cpp 24, MCInstLower.cpp 23,
  MCTypeUtilities.h 19, TargetStreamer.cpp 18, Utils/TypeUtilities.cpp 16, InstPrinter.cpp 13,
  Utilities.cpp 9, TargetMachine.cpp 7, Utils/TypeUtilities.h 4, TTI.cpp 4,
  RuntimeLibcallSignatures.h 4, Utilities.h 3, MachineFunctionInfo.h 3,
  MachineFunctionInfo.cpp 3, MCInstLower.h 3, AsmPrinter.h 3, TargetInfo.cpp 2,
  MCAsmInfo.h 2, TargetObjectFile.h 1, TargetObjectFile.cpp 1, InstrInfo.cpp 1,
  FrameLowering.cpp 1, MCTargetDesc.h 1, MCAsmInfo.cpp 1, InstPrinter.h 1.
- **DEFER, 5 files:** WasmObjectWriter.cpp 43, MCCodeEmitter.cpp 6, CodeGenPassBuilder.cpp 5,
  LowerEmscriptenEHSjLj.cpp 4, AsmBackend.cpp 2.
- **DROP, 12 files:** AsmTypeCheck.cpp 75, AsmParser.cpp 56, Disassembler.cpp 18,
  AsmTypeCheck.h 18, CFGStackify.cpp 9, FastISel.cpp 3, ReduceToAnyAllTrue.cpp 3,
  CoalesceFeaturesAndStripAtomics.cpp 2, LateEHPrepare.cpp 1, ExceptionInfo.cpp 1,
  GISel/InstructionSelector.cpp 1, GISel/CallLowering.cpp 1.
- **COPY, 0 files.** None of the 14 COPY files references shared wasm infrastructure. That is
  why they really are rename-only.

Side effects of the naive `sed`:
- `\bwasm` does not match inside `int_wasm_*`, because `_` is a word character. So the 84
  `int_wasm_*` lines in `.td` files keep pointing at the real wasm intrinsics. That is
  harmless, but they are all in DROP/DEFER `.td` files or in patterns that MSIL deletes.
- The file name `WebAssemblyWasmObjectWriter.cpp` becomes `MSILMSILObjectWriter.cpp`.
- `-wasm-*` cl::opts become `-msil-*`, which is fine.

### 4.2 The shared infrastructure WebAssembly depends on, and what MSIL uses instead

| WebAssembly dependency | Shared location | MSIL M1 replacement |
|---|---|---|
| `llvm/BinaryFormat/Wasm.h` (`wasm::ValType`, `WasmSignature`, `WASM_*`) | `llvm/include/llvm/BinaryFormat/Wasm.h` | MVT lists plus a small CIL signature type (in `MSILMCExpr.h` or `MSILMCTypeUtilities.h`). Keep `computeSignatureVTs`/`computeLegalValueVTs` (WebAssemblyMachineFunctionInfo.cpp:40-112); drop `valTypesFromMVTs`/`signatureFromMVTs` (114-127). |
| `MCSymbolWasm` (signature, symbol type, import/export names) | `llvm/include/llvm/MC/MCSymbolWasm.h`; created by `MCContext::createSymbolImpl` for `IsWasm` (MCContext.cpp:301-302) | A plain `MCSymbol`. The MSIL MCContext environment creates `MCSymbol`, as `IsSPIRV` does (MCContext.cpp:307-308). Signatures travel in `MSILMCExpr` (3.6.7) or in a side table owned by the AsmPrinter. |
| `MCSectionWasm`, `MCContext::getWasmSection` | `llvm/include/llvm/MC/MCSectionWasm.h`; `TargetLoweringObjectFileImpl.cpp:2280-2350` | One section, following the SPIRV precedent: `llvm/include/llvm/MC/MCSectionSPIRV.h`, `MCContext::getSPIRVSection` (MCContext.cpp:927), `MCObjectFileInfo::initSPIRVMCObjectFileInfo` (MCObjectFileInfo.cpp:967-970). |
| `MCAsmInfoWasm` | `llvm/include/llvm/MC/MCAsmInfoWasm.h`, `llvm/lib/MC/MCAsmInfoWasm.cpp` | Plain `MCAsmInfo`. Its `printSwitchToSection` prints nothing (MCAsmInfo.h:507-508), so no `.section` lines reach the `.il`. Also set `HasSingleParameterDotFile = false` (MCAsmInfo.h:308 defaults to true; AsmPrinter.cpp:570-585 would otherwise print `.file`), `HasDotTypeDotSizeDirective = false`, `HasFunctionAlignment = false`, `CommentString = "//"`, a private label prefix without a leading `.`, and `SupportsDebugInformation = false` (`passes-frame.md` 11). NVPTXMCAsmInfo.cpp:18-63 is the closest existing example of a non-GNU text syntax. |
| `TargetLoweringObjectFileWasm` | `llvm/lib/CodeGen/TargetLoweringObjectFileImpl.cpp` | A `TargetLoweringObjectFile` subclass that returns `TextSection` for everything, like `SPIRVTargetObjectFile` (`llvm/lib/Target/SPIRV/SPIRVTargetObjectFile.h`). |
| `MCWasmObjectTargetWriter`, `WasmObjectWriter`, `MCWasmStreamer` | `llvm/lib/MC/WasmObjectWriter.cpp`, `MCWasmObjectTargetWriter.cpp`, `MCWasmStreamer.cpp` | Nothing in M1. Register no AsmBackend and no CodeEmitter: `Target::createMCAsmBackend` then returns `nullptr` (TargetRegistry.h:480-486), which the assembly path tolerates (CodeGenTargetMachineImpl.cpp:189-194); NVPTX registers neither (NVPTXMCTargetDesc.cpp:79-98). Reject `-filetype=obj` (`registry-tm.md` 0). |
| `IntrinsicsWebAssembly.h` (`Intrinsic::wasm_*`, `int_wasm_*`) | generated from `llvm/include/llvm/IR/IntrinsicsWebAssembly.td` | None in M1. |
| `WasmEHPrepare`, `WasmEHFuncInfo`, `WasmException` | `llvm/lib/CodeGen/WasmEHPrepare.cpp`, `llvm/include/llvm/CodeGen/WasmEHInfo.h`, `llvm/lib/CodeGen/AsmPrinter/WasmException.cpp` | None (EH is phase 3). |
| `ExceptionHandling::Wasm`/`Emscripten`, `CallingConv::WASM_EmscriptenInvoke` | `TargetOptions`, `CallingConv.h` | None. The default exception model is None (`registry-tm.md` 3.4). |
| `RegisterTarget<Triple::wasm32>` | TargetRegistry.h:1060-1070 | One Target with an ArchMatchFn that never matches, selected by object format in `lookupTarget` (`registry-tm.md` 1.2). |
| `isWasm()`/`isOSBinFormatWasm()` branches in generic CodeGen: Analysis.cpp:607, AsmPrinter.cpp:2513, DwarfDebug.cpp:406 and 3356-3357, SelectionDAGBuilder.cpp:2048, TargetLoweringObjectFileImpl.cpp:2280-2350, LiveDebugValues/VarLocBasedImpl.cpp:316-319 | `llvm/lib/CodeGen` | They do not fire for MSIL; nothing to change. When Portable PDB arrives, note that the generic debug code supports `TargetIndex` locations only for wasm. |

The new MCContext environment is owned by the triple work, not by this directory:
`MCContext::Environment` (MCContext.h:89-98), the switches at MCContext.cpp:84-117 and
290-311 (and `cloneSymbol` after it), and MCObjectFileInfo.cpp:1210-1235 and 1237-1254.
`registry-tm.md` covers target selection and DataLayout.

### 4.3 Wholesale copy versus curated copy, in numbers

| | Wholesale copy, then strip | Curated selective copy |
|---|---|---|
| Files copied | 123 | 72 (14 COPY + 58 ADAPT) |
| Lines copied | 38,348 | 19,324, trimmed on copy to about 11.5k (2.4) |
| Dead in M1 (DROP+DEFER) | 51 files, 19,024 lines (49.6%) | 0 |
| Files that fail to compile right after the rename | 47 (752 lines) | 30 ADAPT files, edited anyway |
| TableGen | 18 `.td` files, 4,881 lines, dual-form, plus 3 GISel `.td` and 9 tablegen invocations | 11 `.td` files (10 derived from 1,879 source lines, 1 new), 6 tablegen invocations |
| First compiling commit | only after deleting or fixing the 47 files and the GISel/FastISel/AsmParser glue in CMake | the WP0 skeleton (section 6) |

### 4.4 Recommendation: curated selective copy

- **A wholesale copy cannot build as-is.** 47 files reference infrastructure that does not
  exist for MSIL. 17 of them are DROP or DEFER files that would be deleted anyway. So the
  "strip" step is mandatory before the first build, and it is the same work as curating.
- **Half the copied lines would be dead weight** (19,024 of 38,348). They would also be
  stale: they reference wasm features (tables, reference types, multivalue, EH) that the
  ADAPT files must stop referencing anyway.
- **The real reuse is already isolated.** The 14 COPY files (3,694 lines) are the passes that
  make a stack machine work: RegStackify, ExplicitLocals, RegColoring, RegNumbering,
  ReplacePhysRegs, OptimizeLiveIntervals, ArgumentMove, DebugValueManager, plus the IR
  passes. None of them touches shared wasm infrastructure, so copying them file by file costs
  nothing.
- **The DEFER files are better left as reference.** The object writer, encoder, SIMD,
  atomics, SjLj and the new-PM pipeline in `llvm/lib/Target/WebAssembly` keep receiving
  upstream fixes. Copy them when their phase starts.

Procedure for each work package:
1. Copy the file from the WebAssembly tree as of a recorded commit:
   `git show 2829ef573b08:llvm/lib/Target/WebAssembly/WebAssemblyX.cpp > llvm/lib/Target/MSIL/MSILX.cpp`.
2. Apply the `sed` from 4.1. Do not rename `#include "llvm/BinaryFormat/Wasm.h"` and the like:
   delete them.
3. Trim as the note column of 2.2 says.
4. Keep a provenance line in the file header, for example
   `/// Derived from llvm/lib/Target/WebAssembly/WebAssemblyRegStackify.cpp at d1e15b695381.`
   That keeps future upstream fixes mergeable (Design risk 5).
5. Do not copy a DEFER file before its phase.

---

## 5. Proposed `llvm/lib/Target/MSIL` for M1

### 5.1 Layout

```
llvm/lib/Target/MSIL/
  CMakeLists.txt                      WP0  6 tablegen invocations, LLVMMSILCodeGen source list
  MSIL.h                              WP0  pass factory and initialize* declarations (all M1 passes)
  MSIL.td                             WP0
  MSILRegisterInfo.td                 WP0
  MSILInstrFormats.td                 WP0
  MSILInstrInfo.td                    WP0  core records + includes
  MSILInstrInteger.td                 WP2
  MSILInstrFloat.td                   WP2
  MSILInstrConv.td                    WP2
  MSILInstrMemory.td                  WP3
  MSILInstrControl.td                 WP3
  MSILInstrCall.td                    WP4
  MSILInstrFrame.td                   WP5
  MSILTargetMachine.{h,cpp}           WP0  registration, MSILPassConfig (pipeline)
  MSILSubtarget.{h,cpp}               WP0
  MSILTargetTransformInfo.{h,cpp}     WP0  (registry-tm.md 5.2)
  MSILTargetObjectFile.{h,cpp}        WP7
  MSILISelLowering.{h,cpp}            WP3  ctor (3.7), LowerOperation dispatch, globals, BR_JT, SELECT inserter
  MSILISelLoweringCall.cpp            WP4  LowerCall, LowerFormalArguments, LowerReturn, CanLowerReturn,
                                           LowerVASTART/VACOPY, CALL_RESULTS custom inserter
  MSILISelDAGToDAG.cpp                WP3
  MSILSelectionDAGInfo.{h,cpp}        WP3  C++-only ISD nodes (CALL), memcpy/memset -> cpblk/initblk
  MSILInstrInfo.{h,cpp}               WP3  copyPhysReg, analyzeBranch/insertBranch (brtrue/brfalse)
  MSILRegisterInfo.{h,cpp}            WP5  reserved regs, eliminateFrameIndex, getFrameRegister
  MSILFrameLowering.{h,cpp}           WP5  localloc prologue
  MSILMachineFunctionInfo.{h,cpp}     WP1  params/results/locals, stackified bits, WARegs, MaxStack
  MSILUtilities.{h,cpp}               WP1  isChild, getCalleeOp, getCopyOpcodeForRegClass, canLowerReturn
  MSILRuntimeLibcallSignatures.{h,cpp} WP4
  MSILAsmPrinter.{h,cpp}              WP7  ILAsm module: assembly header, methods, data segment,
                                           module initializer, P/Invoke for puts, Main wrapper
  MSILMCInstLower.{h,cpp}             WP6
  // IR passes (COPY)
  MSILAddMissingPrototypes.cpp        WP4
  MSILFixFunctionBitcasts.cpp         WP4
  MSILOptimizeReturned.cpp            WP4
  // MI passes
  MSILArgumentMove.cpp                WP1  COPY
  MSILSetMemFlags.cpp                 WP3  from SetP2AlignOperands
  MSILCleanCodeAfterTrap.cpp          WP1  COPY
  MSILNullifyDebugValueLists.cpp      WP1  COPY
  MSILReplacePhysRegs.cpp             WP1  COPY
  MSILOptimizeLiveIntervals.cpp       WP1  COPY
  MSILRegStackify.cpp                 WP1  COPY*
  MSILRegColoring.cpp                 WP1  COPY
  MSILExplicitLocals.cpp              WP1  COPY*
  MSILRegNumbering.cpp                WP1  COPY
  MSILDebugValueManager.{h,cpp}       WP1  COPY
  MSILStackCheck.cpp                  WP1  new, .maxstack and CIL stack-rule verifier (passes-frame.md 8.3)
  TargetInfo/
    CMakeLists.txt                    WP0
    MSILTargetInfo.{h,cpp}            WP0  getTheMSILTarget(), LLVMInitializeMSILTargetInfo
  MCTargetDesc/
    CMakeLists.txt                    WP0
    MSILMCTargetDesc.{h,cpp}          WP0  registration; operand types; opcode predicates (3.6.9)
    MSILMCAsmInfo.{h,cpp}             WP6
    MSILInstPrinter.{h,cpp}           WP6
    MSILTargetStreamer.{h,cpp}        WP6  ILAsm directives (asm and null streamers)
    MSILMCTypeUtilities.{h,cpp}       WP6  CIL type names, signature printing
    MSILMCExpr.{h,cpp}                WP6  new, method-ref and calli-signature MCTargetExpr
```

That is 47 C++ files at the top level, 2 in `TargetInfo/` and 12 in `MCTargetDesc/` (61 in
all), plus 11 `.td` files and 3 `CMakeLists.txt`: 75 files. They come from the 72
COPY+ADAPT files, minus the 2 Utils type-utility files folded into others, plus 5 new files.
- **No** `AsmParser/`, `Disassembler/`, `GISel/` or `Utils/`. Without an AsmParser, Utils has
  no second client, so its two useful helpers move into `MSILUtilities` and
  `MSILMCTypeUtilities`.
- **No** `MSILPassRegistry.def` or `MSILCodeGenPassBuilder.cpp`: the legacy PM is the default
  for clang and llc (TargetMachine.h:531, `RunCodeGen.cpp:92-99`, `llcdriver.cpp:749-758`).
  Keep the new-PM `...Pass` classes that come with the COPY passes, so the new-PM pipeline is
  a later add-on.

Test layout:
- `llvm/test/CodeGen/MSIL/lit.local.cfg` (WP0), as in `registry-tm.md` 2.4.
- One directory or file prefix per package: `llvm/test/CodeGen/MSIL/{core,int,fp,conv,mem,control,call,frame,mc,asmprinter}/`.
- Runnable tests (llc, then ilasm, then dotnet) under `llvm/test/CodeGen/MSIL/run/`, with their
  own `lit.local.cfg` that detects `ilasm` and `dotnet`. That avoids editing the global
  `lit.cfg.py`.

### 5.2 M1 pass pipeline

Use exactly the order in `passes-frame.md` section 4. In terms of the WebAssembly
`TargetPassConfig` (WebAssemblyTargetMachine.cpp:295-491):
- `addIRPasses`: AddMissingPrototypes, FixFunctionBitcasts, OptimizeReturned (O1+),
  LowerInvoke + UnreachableBlockElim, IndirectBrExpand.
- `addISelPrepare`: AtomicExpand only.
- `addInstSelector`: ISel, ArgumentMove, SetMemFlags, CleanCodeAfterTrap.
- `addPostRegAlloc`: the same disables as WebAssembly (lines 401-413).
- `addPreEmitPass`: NullifyDebugValueLists, UnreachableMachineBlockElim, ReplacePhysRegs,
  OptimizeLiveIntervals (O1+), RegStackify, RegColoring (O1+), ExplicitLocals, RegNumbering.
- `addPreEmitPass2`: StackCheck.
- No GlobalISel overrides. `createTargetRegisterAllocator` returns `nullptr`, and
  `addRegAssignAndRewriteFast/Optimized` return false, as in WebAssembly (lines 254-258,
  286-288).

---

## 6. Work packages

### 6.1 Hotspots and the rules for them

| Hotspot | Why it is shared | Rule |
|---|---|---|
| `CMakeLists.txt` (3 files) | LLVM rejects any `.cpp` in the directory that is not listed (`llvm/cmake/modules/LLVMProcessSources.cmake:83-133`, `llvm_check_source_file_list`). `AsmPrinters.def` is generated from a file glob at configure time (`registry-tm.md` 2.3). | WP0 lists **every** M1 source from 5.1 up front and creates each as a compiling stub (an empty pass that returns false, a TargetLowering with only the constructor). Adding a file later goes through WP0. |
| `MSIL.td`, `MSILRegisterInfo.td`, `MSILInstrFormats.td`, `MSILInstrInfo.td` | Everything includes them | WP0 only. The per-area `.td` files exist from day 1 (empty or minimal) and are included from `MSILInstrInfo.td`. Each area defines its own SDNodes in its own file (`-gen-sd-node-info`, 3.1). |
| `MSIL.h`, `MSILTargetMachine.cpp` (pipeline, `LLVMInitializeMSILTarget`) | Every pass is declared and scheduled there | WP0 declares and schedules all M1 passes from 5.2 at once, against the stubs. Packages then fill in their own pass files without touching these two. |
| `MSILMCTargetDesc.h` (operand types, opcode predicates) | Used by every pass and by MC | WP0 writes 3.6.9 once. |
| `MSILISelLowering.h` and the constructor | All areas need operation actions and Lower* hooks | WP0 writes the class declaration with **all** M1 `Lower*` methods and the constructor table from 3.7. Then WP3 owns `MSILISelLowering.cpp` and WP4 owns `MSILISelLoweringCall.cpp`; a class can be implemented across `.cpp` files. `LowerOperation` and `EmitInstrWithCustomInserter` dispatch (WP3) only call the WP4 functions. |
| `MSILSelectionDAGInfo.h` (C++-only ISD enum) | `CALL` is created by WP4 and selected by WP3 | WP0 adds `MSILISD::CALL` (after `GENERATED_OPCODE_END`). |
| Tests | Many files | Each package owns its own test directory (5.1). WP0 owns `lit.local.cfg`. |

### 6.2 Packages

| WP | Scope | Files owned (all under `llvm/lib/Target/MSIL/` unless noted) | Depends on | Gate (tests that must pass before merge) | Reviewer |
|---|---|---|---|---|---|
| **WP0** Skeleton and glue | Build plumbing, TableGen core, registration, pipeline, stubs | 3 `CMakeLists.txt`, `MSIL.td`, `MSILRegisterInfo.td`, `MSILInstrFormats.td`, `MSILInstrInfo.td`, empty per-area `.td` files, `MSIL.h`, `MSILTargetMachine.*`, `MSILSubtarget.*`, `MSILTargetTransformInfo.*`, `TargetInfo/*`, `MCTargetDesc/MSILMCTargetDesc.*`, `MSILISelLowering.h`, `MSILSelectionDAGInfo.h`, stubs of every other file, `llvm/test/CodeGen/MSIL/lit.local.cfg`; outside: the `llvm/CMakeLists.txt:588` and `Targets.h.cmake` edits (`registry-tm.md` 2.2) | The triple, registry and MCContext work outside this directory | Configures and builds with `-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MSIL`; `llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=null` runs ISel on `define i32 @f() { ret i32 0 }`; `llc --version` lists `msil` | WP3 |
| **WP1** Stackification | The COPY passes and their support code | `MSILArgumentMove`, `MSILReplacePhysRegs`, `MSILOptimizeLiveIntervals`, `MSILRegStackify`, `MSILRegColoring`, `MSILExplicitLocals`, `MSILRegNumbering`, `MSILCleanCodeAfterTrap`, `MSILNullifyDebugValueLists`, `MSILDebugValueManager.*`, `MSILMachineFunctionInfo.*`, `MSILUtilities.*`, `MSILStackCheck.cpp` | WP0 | MIR tests with `llc -run-pass=msil-reg-stackify`, `msil-explicit-locals` and `msil-stack-check`, adapted from `llvm/test/CodeGen/WebAssembly/explicit-locals.mir` and the reg-stackify tests; the LOCALLOC rule; ARG versus LOCAL numbering | WP5 |
| **WP2** Arithmetic and conversions | Instruction records and patterns | `MSILInstrInteger.td`, `MSILInstrFloat.td`, `MSILInstrConv.td`; tests `llvm/test/CodeGen/MSIL/{int,fp,conv}/` (port `i32.ll`, `i64.ll`, `f32.ll`, `f64.ll`, `comparisons-*.ll`, `conv.ll`, `frem.ll` from `llvm/test/CodeGen/WebAssembly/`) | WP0; WP6 for printed output | FileCheck on `.il`; every output assembles with ilasm | WP3 |
| **WP3** ISel core | Lowering, selection, memory, control flow | `MSILISelLowering.cpp`, `MSILISelDAGToDAG.cpp`, `MSILSelectionDAGInfo.cpp`, `MSILInstrInfo.{h,cpp}`, `MSILInstrMemory.td`, `MSILInstrControl.td`, `MSILSetMemFlags.cpp`; tests `{mem,control}/` (load-store, select, switch, branches, globals) | WP0 | FileCheck; ilasm assembles; `-verify-machineinstrs` is clean | WP2 |
| **WP4** Calls and signatures | Call ABI and signature machinery | `MSILInstrCall.td`, `MSILISelLoweringCall.cpp`, `MSILRuntimeLibcallSignatures.*`, `MSILAddMissingPrototypes.cpp`, `MSILFixFunctionBitcasts.cpp`, `MSILOptimizeReturned.cpp`; tests `call/` (port `call.ll`, `call-indirect.ll`, `function-bitcasts*.ll`, `add-prototypes.ll`) | WP0; WP6 for `MSILMCExpr` | Direct, indirect (`ldftn` + `calli`) and K&R calls assemble and run | WP6 |
| **WP5** Frame | Prologue and frame indices | `MSILInstrFrame.td`, `MSILFrameLowering.*`, `MSILRegisterInfo.{h,cpp}`; tests `frame/` (port `userstack.ll`, `byval.ll` where applicable) | WP0, WP1 (LOCALLOC rule) | Allocas, byval and over-aligned locals run; the localloc stack rule holds | WP1 |
| **WP6** MC layer | Printing | `MCTargetDesc/MSILMCAsmInfo.*`, `MSILInstPrinter.*`, `MSILTargetStreamer.*`, `MSILMCTypeUtilities.*`, `MSILMCExpr.*`, `MSILMCInstLower.*`; tests `mc/` | WP0 | Every record in 3.6 prints text that ilasm accepts (a generated test that emits each opcode once) | WP4 |
| **WP7** Module emission | Whole-file ILAsm | `MSILAsmPrinter.*`, `MSILTargetObjectFile.*`; tests `asmprinter/` (assembly header, `.locals`, `.maxstack`, data segment, module initializer and relocations, P/Invoke `puts`, generated `Main`) | WP6, WP1 | `fib.c`-level IR runs under `dotnet` | WP4 |
| **WP8** End-to-end suite | Design 12 exit criteria | `llvm/test/CodeGen/MSIL/run/` (llc-level, `lit.local.cfg` detects ilasm and dotnet), later `clang/test/...` for the 25 C programs at -O0 and -O2, compared with native clang output; an ilverify or PEVerify step | WP7 and the clang driver work | The 25 programs pass on .NET 8 (and PEVerify on Windows) | WP7 |

### 6.3 Order and gates

1. **G0 (serial):** WP0 lands. From then on every package builds against the full skeleton.
2. **G1 (parallel):** WP1, WP2, WP3, WP4, WP5 and WP6. Each merges behind its own FileCheck
   and MIR tests, plus "ilasm accepts every `.il` the tests produce" once WP6 has a minimal
   printer. WP6 should land a minimal printer early, because WP2 to WP5 check printed output.
3. **G2:** WP7. `define i32 @main() { ret i32 42 }` becomes an `.il` that ilasm assembles and
   `dotnet` runs with exit code 42. Then fib, sieve and so on.
4. **G3:** WP8. The Design 12 exit criteria, at -O0 and -O2.

Every merge needs approval from the package's reviewer (6.2). The reviewer checks the
change against this report, `passes-frame.md` and `registry-tm.md`, and reruns the gate.

### 6.4 Prerequisites outside `llvm/lib/Target/MSIL`

These belong to other recon keys and other owners:
- `Triple::MSIL` and its parsing (`llvm/lib/TargetParser/Triple.cpp`).
- The MCContext environment and MCObjectFileInfo init (4.2).
- `TargetRegistry::lookupTarget` selecting the target by object format (`registry-tm.md` 1.2).
- `Triple::computeDataLayout` returning the native layout for MSIL triples
  (`registry-tm.md` 3.3; `CodeGenTargetMachineImpl.cpp:97` calls it).
- The default exception model None (`registry-tm.md` 3.4).
- The clang wrapper, including the `"no-prototype"` attribute that AddMissingPrototypes needs
  (clang/lib/CodeGen/Targets/WebAssembly.cpp:122-123 is the only place that sets it today).
- The clang driver must not pass `-faddrsig` for MSIL triples. AsmPrinter.cpp:3181-3191
  would print `.addrsig` otherwise; unverified whether the driver passes it for this object
  format.

---

## 7. Risks and open questions

1. **`I64` as `native int`** (3.4) needs sign-off; `isel.md` reaches the same conclusion
   independently. If it is rejected, every pointer use needs a conversion. A conversion that
   is not stackified lands in a local whose type comes from the register class, so the
   `int64` scheme is fragile with RegStackify.
2. **Native CPU and feature strings.** clang passes x86 CPU and feature strings for
   `x86_64-*-msil`. Unless MSILSubtarget drops them, MCSubtargetInfo prints
   "is not a recognized processor/feature" on every compile (MCSubtargetInfo.cpp:92, 207).
   `registry-tm.md` 4.4 proposes mapping everything to `generic`; this report agrees.
3. **Float32 precision** (3.6.3) is unverified; WP2 must test it on both runtimes.
   **u64 to f32 double rounding** (3.6.4) is real: `isel.md` verified it on .NET 8 and .NET
   Framework, so that conversion is Custom.
4. **`cpblk`/`initblk` size.** `isel.md` verified that a native int size works, and it
   sends variable lengths to `NativeMemory.Copy`/`Fill`. The `conv.ovf.u4.un` guard in
   3.6.5 is only needed if `cpblk` is also used for variable lengths.
5. **libm functions.** `isel.md` decision 11 maps `sqrt`, rounding and friends to BCL
   intrinsics on net8.0. On net48 they stay libcalls, and the AsmPrinter must map them to
   BCL methods or emit P/Invokes to libm (WP4/WP7).
   `RuntimeLibcallsInfo::getFunctionTy` (RuntimeLibcalls.h:196-198) can provide IR types for
   many libcalls; its coverage is unverified, so `MSILRuntimeLibcallSignatures` may still
   need a small hand table (i128 helpers such as `__multi3` and `__divti3`).
6. **Atomics.** `setMaxAtomicSizeInBitsSupported(0)` turns atomics into `__atomic_*`
   libcalls that M1 does not provide. That is fine for the M1 programs. The alternative for
   single-threaded M1 is to select atomic loads and stores as `volatile.` `ldind`/`stind`.
7. **i128.** `isel.md` decision 10 expands i128 multiplication and shifts inline. i128
   division still legalizes to compiler-rt libcalls (`__divti3`, `__udivti3`, `__modti3`,
   `__umodti3`), which MSIL does not have in M1. Keep i128 division out of the M1 test
   programs.
8. **Dual-form or single-form TableGen** (3.3) is an open conflict with `isel.md` 10.1.
   WP0 must decide before the per-area `.td` work starts.
9. **Upstream drift.** WebAssembly in this checkout already has the new pass manager
   classes and GlobalISel. Record the source commit in each copied file (4.4) so fixes can be
   merged later.

---

## 8. Experiments run (on this host)

Sources are in this session's scratchpad,
`<scratch>\recon\wasm-inventory\`.
`il/` holds the probes and `MSIL/` holds the renamed copy.

| Probe | What it tests | Result |
|---|---|---|
| Rename experiment | Copy plus `sed` (4.1), then grep for unresolved includes and identifiers | 43 broken include lines (9 headers); 752 lines in 47 files with broken identifiers; 0 in COPY files |
| `probe.il` / `probe2.il` (ilasm 4.0.30319) | Top-level global methods and `.field static`; `float64(0x...)`; `ldarg N`, `ldloc N`, `stloc N`; labels `LBB1_1:`; `switch (L1, L2)`; `.maxstack` after the code; `ldftn int32 sw(int32)`; `calli int32(int32)`; `.locals init (native int, float32, int32)`; `localloc` | All assembled. `probe.exe` runs `localloc` with an `int32` still on the stack and throws **InvalidProgramException** on .NET Framework 4 x64. `probe2.exe`, with the value moved to a local first, runs and returns -1 as expected. PEVerify reports only "Instruction cannot be verified" (calli/localloc). |
| `mix.il` | `int64` plus `native int`, and `int64` as an `ldind` address | Runs (15) on .NET Framework 4 x64. **PEVerify: `[found Long][expected Native Int] Unexpected type on the stack`** and `[found Long] Expected ByRef`. |
| `nint.il` | The same program with every 64-bit value typed `native int`; `shl` of native int by an int32 amount | Runs: 72 on .NET Framework 4 x64 and on .NET 8. PEVerify: only `[found Native Int] Expected ByRef` (expected unverifiable pointer use) and a probe artifact (`conv.u` of `ldloca`). |
| `frem.il` | CIL `rem` on float64 against Python `math.fmod` | Bit-identical in 4 of 4 cases (`1e300 rem 3`, `-5.5 rem 2`, `5.5 rem -2`, `0.1 rem 1e-310`) on .NET Framework 4 x64 and on .NET 8. |
| `misc.il` | `ldc.r4 float32(0x3F800000)`, `volatile.`, `unaligned. 1`, `stind.i`/`ldind.i` on native int, `conv.ovf.u4.un`, `cpblk`, `initblk`, `conv.r.un` of native int | Assembles. Correct result (4.575657225703391E+18, exit 1) on .NET Framework 4 x64 and .NET 8. PEVerify: only `Expected ByRef [found Native Int]` and localloc "cannot be verified". |
| `System.Reflection.Emit.OpCodes` (PowerShell 7) | Opcode values and stack behaviour of every CIL opcode used in 3.6 | Values as listed in 3.6 (for example `ldarg` 0xFE09, `stloc` 0xFE0E, `ceq` 0xFE01, `localloc` 0xFE0F, `cpblk` 0xFE17, `stind.i` 0xDF, `conv.u` 0xE0, `conv.r.un` 0x76). |

PEVerify caveat: it reports only the first error in each method, so a clean PEVerify run
does not prove the absence of later errors. The `mix.il` evidence is valid because the stack
type error is the first instruction it checks in `mixadd`.
