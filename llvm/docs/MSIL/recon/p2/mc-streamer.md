# P2 recon: the MC layer for CIL objects (key `mc-streamer`)

Status: Phase 2 recon report, 2026-10-20. Checkout: branch `msil` at `1ea64fe9fc7c` (upstream
`64f50ffd4f1c` merged). Scratch: `C:\llvm-msil-scratch\p2\mc-streamer`. Supporting files (sources,
scripts, trimmed outputs): [mc-streamer-files/](mc-streamer-files/). Every LLVM file, line and API
below was read in this checkout; anything not checked is marked *unverified*. No tracked file was
modified, no build was run in `D:\llvm-msil\build` (only the read-only `ninja -t deps` and
`ninja -t inputs` queries), and no private LLVM build was made.

Scope: P2-A of [ImplementationPlan.md](../../ImplementationPlan.md) section 7, from today's text-only
AsmPrinter to `clang -c`: the Phase-1 MC code, WebAssembly's object path as the model, the MSIL MC
object design for the two leading object-format candidates, the headers that must change (measured),
and whether Phase 2 needs an ILAsm parser.

## 0. Summary

**Findings**

1. **Phase-1 MC is text only, and four shortcuts stand between it and an object path.** The target
   registers MCAsmInfo, instr/reg/subtarget info, the instruction printer and the asm/null target
   streamers, but no MCCodeEmitter, MCAsmBackend or object target streamer
   (`MSILMCTargetDesc.cpp:108-131`). The shortcuts: (a) the module initializer, the entry point and
   section comments are raw text lines (`MSILTargetStreamer::emitILLine`, used at
   `MSILAsmPrinter.cpp:267, 269, 745, 806, 814, 836, 845, 1000-1002, 1117-1118`); (b) one MCInst can
   print several CIL opcodes: the 12 records whose opcode field is `0xFFFF` (TEE, CONST_I64,
   GLOBAL_ADDR with an offset, MEMMOVE, TRAP), `BR_TABLE_I32` (`switch` + `br`) and the memory
   records with `unaligned.`/`volatile.` prefixes; and the short forms (`ldarg.0`, `ldloc.s`, `ldc.i4.s`) are
   chosen inside `MSILInstPrinter` (`getShortForm`, `MSILInstPrinter.cpp:77-184`); (c) BCL member
   references are literal text (`[mscorlib]...` in asm strings of MEMMOVE/MEMORY_BARRIER and in
   `SynthesizedBody` lines); (d) `MSILMemberRefExpr::evaluateAsRelocatableImpl` returns false
   (`MSILMCExpr.h:92-95`), so MCAssembler could not turn a member reference into a fixup.
2. **CIL encoding is simple and is now validated end to end.** All 175 TableGen records that carry a
   CIL opcode (`MSILInst::Inst`, `MSILInstrFormats.td:28`) match ECMA-335 as published by
   `System.Reflection.Emit.OpCodes` (0 mismatches). A 200-line Python prototype of the code emitter
   (one ILAsm line = one CIL instruction, 4-byte token placeholders, branch and switch offsets
   resolved like PC-relative MC fixups) reproduces the IL bytes that ilasm produced for **2,670 of
   2,670 methods** (848,670 bytes, 22,938 token slots, 21,758 branches, 164 switch targets) of the
   186 modules built from `msil/test/M1` at -O0/-O2. It also shows one ilasm defect the integrated
   encoder removes: ilasm writes float32 `0x7FA00000` (signaling NaN) as `0x7FE00000`.
3. **WebAssembly's object path transfers almost one to one.** `MCSymbolWasm` carries the signature
   and symbol type (`MCSymbolWasm.h:17-31`), the code emitter emits padded placeholders plus fixups
   (`WebAssemblyMCCodeEmitter.cpp:237-275`), the asm backend records a relocation for every
   unresolved fixup (`WebAssemblyAsmBackend.cpp:100-105` `applyFixup`), the target object writer maps
   (fixup kind, symbol type, specifier) to a relocation type (`WebAssemblyWasmObjectWriter.cpp:66`
   `getRelocType`), each function lives in its own section (`WasmObjectWriter.cpp:1599`, forced by
   `WebAssemblyTargetMachine.cpp:190-192`), and the wasm target object streamer is mostly no-ops
   because the writer reads the symbols. For MSIL every fixup is a 4-byte metadata token or an
   intra-method branch; branches are resolved by MCAssembler (same-section rule,
   `MCObjectWriter.cpp:44-51`), tokens become relocations (candidate A) or rows of the object's own
   metadata (candidate B). Everything up to MCAssembler is common to both candidates; only the
   object writer differs.
4. **No existing widely-included header must change for P2-A.** In this checkout `MCSymbol` has no
   object-format kind (`MCSymbol.h:45-49`: Regular/Equated/Common), `MCContext::createSymbolImpl`
   is in the `.cpp`, M1 already added `MCContext::IsMSIL`, `MCSectionMSIL` and
   `MCObjectFileInfo::initMSILMCObjectFileInfo`. P2-A needs new headers (`MCSymbolMSIL.h`,
   `MCMSILStreamer.h`, `MCMSILObjectWriter.h`, `BinaryFormat/MSIL.h`: zero dependents) and `.cpp`
   edits in `llvm/lib/MC` (MCContext.cpp, MCAsmBackend.cpp, TargetRegistry.cpp). Measured with the
   ninja deps log (2,691 objects, 3.00 CPU-hours of compile): the optional Phase-2 header set
   (Magic.h, Binary.h, ObjectFile.h, MCSectionMSIL.h, clang driver diagnostics, Options, Types.def)
   recompiles **245 objects, 9.3% of compile CPU, about 4-5 minutes**; for comparison Triple.h
   (M1) is 86.5% (about 41 min) and MCContext.h or MCSymbol.h alone about 20% (about 10 min).
5. **No ILAsm parser is needed in Phase 2.** IR to object covers `clang -c`, `llc -filetype=obj`, LTO
   and msil-ld. The parser would only serve `-save-temps -c`, `/FA`/`/Fa`, `-via-file-asm` (the
   driver then splits codegen and assembling) and hand-written `.il` inputs; those stay clean errors
   (section 7). One crash to fix on the way: `clang -cc1as -filetype obj` with an MSIL triple hits
   `assert(MAB && "Unable to create asm backend!")` (`cc1as_main.cpp:600`) today, and once P2-A
   registers an asm backend it would reach `llvm_unreachable("unexpected object format")` in
   `MCAsmBackend::createObjectWriter` (`MCAsmBackend.cpp:60-61`) unless the `Triple::MSIL` case is
   added there.

**Recommendations**

- R1. **One event stream, one MCInst per CIL instruction.** The AsmPrinter emits only MCInsts,
  labels and structured `MSILTargetStreamer` calls (no IL text); `MSILMCInstLower` expands every
  multi-opcode record and every short form into MC-level CIL opcodes (about 30 new `isCodeGenOnly`
  records), so `MSILInstPrinter` prints and `MSILMCCodeEmitter` encodes the same record. `-S` text
  stays byte-identical to today's (plan 5.1 contract) and `-c` is the same stream through another
  streamer.
- R2. Follow the wasm file layout: `MCSymbolMSIL` and a small `MCMSILStreamer` in `llvm/lib/MC`,
  `Triple::MSIL` cases in `TargetRegistry.cpp` and `MCAsmBackend.cpp`, and the object writer in the
  target, reached through a factory method on an `MCMSILObjectTargetWriter` (so lib/MC never links a
  target and lld can link the same writer code through `${LLVM_TARGETS_TO_BUILD}`).
- R3. Fixups: one 4-byte token fixup kind per ECMA operand class (method, field, sig, type, token,
  string) plus generic `FK_Data_4` PC-relative fixups for branches and switch entries; the
  specifier on the `MCSymbolRefExpr` says which reference (definition, function-address field, BCL
  member, signature).
- R4. Keep long branches in P2-A (text and object identical); add branch relaxation later
  (83.5% of the corpus' branches fit a short form, 54 KB or 6.4% of the IL).
- R5. Keep D12's per-module data segment as a **per-object** segment with per-object init methods,
  so the object needs no data relocations, only IL token fixups (section 5.9); this choice is
  shared by both object-format candidates and fits FeatureIdeas 6 and 7.
- R6. Front-load no LLVM MC header in P2-A. Put the cheap shared edits of the whole phase into one
  first milestone "P2-0" (section 6.3), measured at about 4-5 min of rebuild plus relinks
  (links are 7 s for clang).
- R7. No MSIL `MCAsmParser` in Phase 2; test the encoder with `llc -show-mc-encoding` and an
  ilasm-oracle comparator (the `ilcensus` + `encode-check.py` method of section 4).

## 1. Sources read and method

- Phase-1 MC code: `llvm/lib/Target/MSIL/MCTargetDesc/*` (all 12 files), `MSILAsmPrinter.{h,cpp}`,
  `MSILMCInstLower.{h,cpp}`, `MSILDataSegment.h`, `MSILTargetObjectFile.{h,cpp}`,
  `MSILTargetMachine.cpp`, `MSILInstr*.td`; M1 hooks in `llvm/lib/MC` and `llvm/include/llvm/MC`;
  clang driver hooks (`ToolChain.cpp`, `Clang.cpp`, `DiagnosticDriverKinds.td`).
- WebAssembly: `MCTargetDesc/WebAssembly{MCCodeEmitter,AsmBackend,WasmObjectWriter,TargetStreamer,
  MCTargetDesc}.cpp`, `WebAssemblyFixupKinds.h`, `WebAssemblyMCInstLower.cpp`,
  `WebAssemblyTargetMachine.cpp`, `MCSymbolWasm.h`, `MCWasmStreamer.{h,cpp}`,
  `MCWasmObjectWriter.h`, `WasmObjectWriter.cpp`, `BinaryFormat/Wasm.h`, `lld/wasm/{Driver,InputFiles}.cpp`.
- Generic MC: `MCAssembler::evaluateFixup` (`MCAssembler.cpp:144-206`), `MCAsmBackend.h`,
  `MCObjectWriter.{h,cpp}`, `MCObjectStreamer.{h,cpp}`, `MCFixup.h`, `MCValue.h`,
  `CodeGenTargetMachineImpl.cpp:164-215`, `cc1as_main.cpp:585-610`, `AsmPrinterInlineAsm.cpp:73-95`.
- Plan inputs: Design.md (sections 5-8, 11), ImplementationPlan.md sections 1-5, 7 and the
  2026-10-14/15 rows of section 12, Status.md, `msil/orchestration/P2/inputs.md`, recon
  `asmprinter.md`, `triple-objfmt.md`, `registry-tm.md`, `wasm-inventory.md`, FeatureIdeas.md.
- Experiments: section 4 (E1-E10). Tools installed: none (the .NET 8 SDK, Python, the Framework
  and NuGet ilasm/ildasm were already present).

## 2. The Phase-1 MC code as built

### 2.1 Registration and entry points

| Piece | Where | Phase-1 state |
|---|---|---|
| Target MC registration | `MCTargetDesc/MSILMCTargetDesc.cpp:108-131` | MCAsmInfo, MCInstrInfo, MCRegisterInfo, MCSubtargetInfo, MCInstPrinter, asm target streamer (`MSILTargetAsmStreamer`), null target streamer. No `RegisterMCCodeEmitter`, `RegisterMCAsmBackend`, `RegisterObjectTargetStreamer` (comment at lines 17-18 and `MSILMCTargetDesc.h:43-44`). |
| MCAsmInfo | `MSILMCAsmInfo.cpp:24-67` | `CommentString "//"`, `InternalSymbolPrefix "@"` (block labels `@BB0_1`), `SupportsQuotedNames=false`, no `.type/.size`, no debug info, `UseIntegratedAssembler = false` (line 63). |
| Object streamer request | `MSILTargetMachine.cpp:209-218` | `createMCStreamer` returns `StringError("MSIL object file emission is not implemented; use -S (ILAsm text)")` for `CodeGenFileType::ObjectFile`. |
| lib/MC object streamer | `TargetRegistry.cpp:93-96` | `case Triple::MSIL: reportFatalUsageError("MSIL object file emission is not implemented")`. |
| lib/MC object writer | `MCAsmBackend.cpp:31-63, 65-86` | no `Triple::MSIL` case: `default: llvm_unreachable("unexpected object format")`. |
| Symbols | `MCContext.cpp:315-318` | `IsMSIL` creates a plain `MCSymbol` (comment: a later phase may add `MCSymbolMSIL`). |
| Sections | `MCSectionMSIL.h:28-38`, `MCContext.cpp:961-971`, `MCObjectFileInfo.cpp:1180-1187` | header-only `MCSectionMSIL(Name, SectionKind)`, uniqued by name, null begin symbol; `.text`, `.data`, `.bss`, `.rodata`. `MSILTargetObjectFile` (`MSILTargetObjectFile.cpp:21-42`) maps every global to one of the four by kind. |
| Asm parser | `AsmParser.cpp:808-812` | `reportFatalUsageError("assembly parsing is not supported for MSIL")`; pinned by `clang/test/CodeGen/MSIL/cc1as-unsupported.s`. |
| clang | `Clang.cpp:5541-5543` (AssembleJobAction in cc1), `Clang.cpp:9340-9342` (ClangAs), `ToolChain.cpp:736-765` (`getAssemble`, `getLink`), `DiagnosticDriverKinds.td:163-165` | `err_drv_msil_no_object_output` for `-c`, `-save-temps -c`, `.s` inputs, `-fno-integrated-as -c` and every link (probed, E9). |

### 2.2 How each output construct is produced today

ILAsm resolves forward references, so the printer emits functions in module order and the trailer
last (`emitEndOfAsmFile`, `MSILAsmPrinter.cpp:488-530`).

| Construct | Producer | Channel |
|---|---|---|
| Header comments, `.assembly extern mscorlib`, `.assembly 'name'` | `emitStartOfAsmFile` (462-486) | target streamer `emitFileHeader`, `emitAssemblyRefs`, `emitAssembly` (`MSILTargetStreamer.cpp:38-72`) |
| Method header `.method <public/assembly> static <ret> 'name'(<params>) cil managed` + `{` | `emitFunctionEntryLabel` (1210-1232) replaces the entry label | `emitMethodBegin(MSILMethodDecl{Name, Sig, Public})` (`MSILTargetStreamer.cpp:74-87`). The function's MCSymbol is never emitted. |
| `.maxstack`, `.locals (...)` | `emitFunctionBodyStart` (1234-1250); max stack from MSILStackCheck | `emitMaxStack`, `emitLocals` (`MSILTargetStreamer.cpp:91-106`) |
| Instructions | `emitInstruction` (1252-1265) -> `MSILMCInstLower::lower` -> `EmitToStreamer` | MCInst through `MCAsmStreamer` and `MSILInstPrinter::printInst` (197-249) |
| `}` | `emitJumpTableInfo` (1136-1146), the last per-function hook | `emitMethodEnd` (`MSILTargetStreamer.cpp:108-115`) |
| Call targets, `ldftn`, `ldsfld` fields, `calli` signatures | `MSILMCInstLower.cpp:54-170` | `MSILMemberRefExpr` (Method, Field, StandAloneSig, BCLMember; `MSILMCExpr.h:39-104`) printed by `MCAsmInfo::printExpr` |
| Signatures | `MSILAsmPrinter::getSignature` (284-311, from `computeSignatureVTs`), `internSignature` (313-317), `noteExternalSymbol` (327-343) | owned by the printer (`SpecificBumpPtrAllocator<MSIL::Signature> SigAlloc`, `std::set` for calli), pointed to by the expressions |
| P/Invoke declarations | `emitPInvokeDeclarations` (725-807), D14 table in `MSILUtilities` | `emitPInvoke` (`MSILTargetStreamer.cpp:117-139`) |
| Data image, `<msil>image_t`, RVA field | `MSILDataSegment` (collector streamer over `AsmPrinter::emitGlobalConstant`, `MSILDataSegment.h`), `emitDataSegment` (809-837) | `emitDataImage` (`MSILTargetStreamer.cpp:141-189`) |
| Address fields `'g'`, `'<msil>data'`, `'<msil>fn_f'` | `emitDataSegment`, `emitFunctionAddressFields` (839-846) | `emitAddressField` (`MSILTargetStreamer.cpp:191-202`) |
| `<Module>::.cctor` | `emitModuleInitializer` (848-1005) builds a `SynthesizedBody` (208-271: text lines with stack effects, computed `.maxstack`) | header and every line through `emitILLine` |
| `'<msil>Main'(string[])` | `emitEntryPoint` (1007-1122) | same: `emitILLine` |
| Section comments, blank lines | `emitPInvokeDeclarations`, `emitDataSegment` | `emitILLine`, `OutStreamer->AddComment` |

### 2.3 What an object path cannot reuse as is

1. **Raw text.** `emitILLine` (and `MCStreamer::emitRawText` behind it) has no meaning for an
   `MCObjectStreamer` (`MCStreamer::emitRawTextImpl` is a fatal error, `MCStreamer.cpp:1283-1289`).
   The `.cctor` and `Main` bodies, their headers (`private hidebysig specialname rtspecialname`,
   `string[] args`), and Main's `.locals (string[], int32, native int, int32)` exist only as text.
2. **Multi-opcode records** (from the TableGen records, `mc-streamer-files/tablegen-opcodes.txt`):

   | Record | Asm string | Where |
   |---|---|---|
   | `LOCAL_TEE_*`, `ARG_TEE_*` (8) | `dup` + `stloc $local` / `starg $arg` | `MSILInstrInfo.td:174-177` |
   | `CONST_I64` | `ldc.i8 $imm` + `conv.i` (printer prints `ldc.i4` when it fits) | `MSILInstrInfo.td:201` |
   | `GLOBAL_ADDR` | `ldsfld $addr`, plus `ldc.i4 off; add` printed by `printGlobalAddrOffset` (`MSILInstPrinter.cpp:358-372`) | `MSILInstrInfo.td:223` |
   | `MEMMOVE` | `conv.i8` + `dup` + `call void [mscorlib]System.Buffer::MemoryCopy(...)` | `MSILInstrMemory.td:148-151` |
   | `TRAP` | `ldnull` + `throw` | `MSILInstrControl.td:183` |
   | `BR_TABLE_I32` | `switch (...)` + `br <last>` (`printBrTable`, 251-286) | `MSILInstrControl.td:149` |
   | loads, stores, `CPBLK`, `INITBLK` | `${flags}` prints `unaligned. N` / `volatile.` prefix lines (`printMemFlagsOperand`, 430-444) | `MSILInstrMemory.td:62, 79, 128-133` |

3. **Short forms in the printer.** `LOCAL_GET_*` carries `Inst = 0xFE0C` (`ldloc`) but prints
   `ldloc.0-3`, `ldloc.s` or `ldloc` depending on the immediate; the same for args and `ldc.i4`
   (`getShortForm`/`printShortForm`, `MSILInstPrinter.cpp:77-184`). An encoder would have to repeat
   that choice exactly, or the text and the bytes diverge.
4. **BCL references as text.** `MEMORY_BARRIER` has opcode `0x28` but its callee is only in the asm
   string (`MSILInstrMemory.td:165-167`); `MEMMOVE` likewise; `AllocHGlobal`, `StringToHGlobalAnsi`
   and `GetCommandLineArgs` are string constants in `MSILAsmPrinter.cpp` (171-173, 1058-1089);
   `<msil>image_t` extends `[mscorlib]System.ValueType` (`MSILTargetStreamer.cpp:151`). P2-B binds
   every BCL member by searching reference assemblies, so the object needs them symbolically.
5. **Member-reference expressions are not relocatable.** `MSILMemberRefExpr` holds an IL name and a
   signature pointer, `evaluateAsRelocatableImpl` returns false and `visitUsedExpr` does nothing
   (`MSILMCExpr.h:92-97`); MCAssembler would report "expected relocatable expression"
   (`MCAssembler.cpp:158-161`).
6. **Signature lifetime.** Signatures live in the AsmPrinter; an object writer runs inside
   `OutStreamer->finish()` during `AsmPrinter::doFinalization`, so it works, but the natural owner
   is the target streamer (as isel.md 724 suggested), which lives exactly as long as the streamer.
7. **Function symbols are never defined.** The method header replaces the entry label; an object
   writer needs each method's symbol defined in its code section.

## 3. WebAssembly's object path (the model)

### 3.1 Pieces

| Piece | Where (this checkout) | Role |
|---|---|---|
| `MCSymbolWasm` | `llvm/include/llvm/MC/MCSymbolWasm.h:17-31` | symbol type (function, data, global, table, tag, section), weak/hidden/comdat, import module/name, export name, `wasm::WasmSignature *Signature`, data size expr. Trivially destructible (asserted in `MCContext.cpp:287-296`). |
| Signatures | `MCContext::createWasmSignature` (`MCContext.h:533`, `MCContext.cpp:499`), `SpecificBumpPtrAllocator<wasm::WasmSignature>` (`MCContext.h:155`) | MCContext owns them because the wasm asm parser creates signatures too. |
| AsmPrinter typing | `WebAssemblyAsmPrinter.cpp:244, 286-328, 441-451, 710-725` | sets type, signature, import module/name on the symbol, then calls the target streamer (`emitFunctionType`, `emitLocal`). |
| `call_indirect` type | `WebAssemblyMCInstLower.cpp:172-176` | a temp symbol (`createTempSymbol("typeindex")`) carries the signature; operand `MCSymbolRefExpr(Sym, WebAssembly::S_TYPEINDEX)`. |
| Code emitter | `WebAssemblyMCCodeEmitter.cpp:110-281` | opcode from TableGen `getBinaryCodeForInstr` (prefix + LEB); immediates by `OperandType`; every `MCExpr` operand: `MCFixup::create(OS.tell() - Start, Expr, Kind)` then a padded 5- or 10-byte LEB placeholder (237-275). |
| Fixup kinds | `WebAssemblyFixupKinds.h` | 4 target kinds from `FirstTargetFixupKind`: sleb128_i32/i64, uleb128_i32/i64. |
| Asm backend | `WebAssemblyAsmBackend.cpp:100-124` | `applyFixup`: `if (!IsResolved) Asm->getWriter().recordRelocation(F, Fixup, Target, Value)`, then ORs the value into the padded bytes; `getFixupKindInfo` table; `createObjectTargetWriter`. |
| Target object writer | `WebAssemblyWasmObjectWriter.cpp:66-160` `getRelocType` | specifier first (GOT, TBREL, MBREL, TLSREL, TYPEINDEX, FUNCINDEX), else (fixup kind, symbol type) -> `R_WASM_*`; FK_Data_4/8 in data sections -> `MEMORY_ADDR_I32`, `TABLE_INDEX_I32`, `FUNCTION_OFFSET_I32`, `SECTION_OFFSET_I32`. |
| Generic writer | `llvm/lib/MC/WasmObjectWriter.cpp` (1,990 lines) | reads symbols, signatures, sections; one function per section (1595-1600 "function sections must contain one function each"); body size prefix written by the writer (`writeCodeSection`, 1034-1057); `reloc.CODE`/`reloc.DATA` and the `linking` section (symbol table, segment info, init funcs = `llvm.global_ctors`, comdats). |
| Object streamer | `MCWasmStreamer.{h,cpp}` (66 + 177 lines), created by `TargetRegistry.cpp:73-76` | `emitSymbolAttribute`, `emitCommonSymbol`, `changeSection`, `emitLabel`, `finishImpl`. |
| Target streamers | `WebAssemblyTargetStreamer.h` | asm streamer prints `.functype`, `.local`, `.import_module`; the **wasm** streamer is no-ops except `emitLocal`, which writes the locals' bytes into the code stream; the writer reads everything else from the symbols. |
| Function sections | `WebAssemblyTargetMachine.cpp:188-192` | `FunctionSections = DataSections = UniqueSectionNames = true`. |
| clang | `ToolChain::IsIntegratedAssemblerDefault()` is true (`ToolChain.h:485`); the WebAssembly toolchain does not override it; `canCollapseAssembleAction` (`Driver.cpp:4880-4886`) collapses compile+backend+assemble into one `cc1 -emit-obj` unless `-save-temps`, `-via-file-asm`, `/FA`, `/Fa` or `-fno-integrated-as` | measured: `clang --target=wasm32 -c -###` runs one cc1 with `-emit-obj` (E8). The MSIL triple already gets the same single job; only the `Clang.cpp:5541-5543` diagnostic stops it. |

### 3.2 How a fixup becomes a relocation (generic MC, this checkout)

`MCObjectStreamer::emitInstruction` encodes into the current fragment and keeps the fixups. At
`MCAssembler::Finish` each fixup goes through `evaluateFixup` (`MCAssembler.cpp:144-206`): the
expression must `evaluateAsRelocatable` to an `MCValue` (symbol A, symbol B, constant, specifier);
`MCAsmBackend::evaluateFixup` may decide (`MCAsmBackend.h:116-119`); otherwise a PC-relative fixup
is resolved when the target is in the same section (`MCObjectWriter::isSymbolRefDifferenceFullyResolvedImpl`,
`MCObjectWriter.cpp:44-51`) and an absolute one only when the value is absolute. Then
`MCAsmBackend::applyFixup(F, Fixup, Target, Data, Value, IsResolved)` patches bytes and, for
unresolved fixups, calls `MCObjectWriter::recordRelocation(F, Fixup, Target, FixedValue)`.

E8 shows the result on a small C file (`mc-streamer-files/wasm/`): calls to an undefined function ->
`R_WASM_FUNCTION_INDEX_LEB`; `i32.const &global` -> `R_WASM_MEMORY_ADDR_LEB` (+addend);
`call_indirect` -> `R_WASM_TYPE_INDEX_LEB` + `R_WASM_TABLE_NUMBER_LEB`; a pointer in data ->
`R_WASM_MEMORY_ADDR_I32`; a function pointer in data -> `R_WASM_TABLE_INDEX_I32`; the `linking`
section's symbol table lists the undefined function and data symbols.

## 4. Experiments and measurements

All under `C:\llvm-msil-scratch\p2\mc-streamer`; scripts and outputs in `mc-streamer-files/`.

| # | Experiment | Result |
|---|---|---|
| E1 | `llvm-tblgen --dump-json` of `MSIL.td`; every record with `Inst != 0xFFFF` checked against `System.Reflection.Emit.OpCodes` (`inst-table.py`, `crosscheck.py`) | 207 records; 175 carry a CIL opcode, **0 mismatches**; 74 distinct opcodes; 12 printable records have `0xFFFF` (the multi-opcode ones of 2.3); 20 never print. |
| E2 | 93 C programs of `msil/test/M1` x {O0, O2}, `x86_64-pc-windows-msvc-msil`, built clang -> Framework ilasm (`-dll -pe64 -x64`) -> `ilcensus` (C#, System.Reflection.Metadata) | 186 modules, 2,670 bodies, 848,670 IL bytes. Token operands: `call`->MethodDef 8,374, `call`->MemberRef 329 (BCL only), `calli`->StandAloneSig 347, `ldftn`->MethodDef 257, `ldsfld`->FieldDef 10,806, `stsfld`->FieldDef 2,707, `ldsflda`->FieldDef 118. No TypeRef/TypeSpec/string tokens in IL. BCL members: `Marshal::AllocHGlobal` 178, `Environment::GetCommandLineArgs` 56, `Marshal::StringToHGlobalAnsi` 56, `Interlocked::MemoryBarrier` 21, `Buffer::MemoryCopy` 18. Opcodes outside the TableGen set (synthesized code): `ldftn`, `stsfld`, `ldsflda`, `ldlen`, `ldelem.ref`, `conv.i8`, plus `dup`, `ldnull`, `throw`, `unaligned.`, `volatile.` from multi-opcode records. Full table: `census.txt`. |
| E3 | `encode-check.py`: one ILAsm line = one instruction; operand encodings by ECMA operand type; 4-byte zero token slots; `InlineBrTarget` = target - (fixup offset + 4); `switch` entry = target - end of the whole instruction; compare with ilasm's bytes, tokens compared by kind and name | **2,670 / 2,670 methods byte-identical** (22,938 token slots agree by kind and name). Confirms: long branches only (ilasm never shortens), switch base = end of instruction, `ldc.r4/r8` bit patterns, `ldarg.s`/`ldloc.s` 1-byte and `ldloc`/`stloc` 2-byte indices, `unaligned.` 1-byte operand. |
| E4 | `snan.ll` (`float 0x7FF4000000000000`), llc -> ilasm, compare | `ldc.r4 float32(0x7FA00000)` is written by ilasm as `0x7FE00000` (quiet bit set; Status.md "Limits" says both ilasms do it); the direct encoding keeps `0x7FA00000`. |
| E5 | method header kinds | Both ilasms (Framework 4.8 and 10.0.12) write **fat** headers for every body, even `ldc.i4.s 42; ret` with `.maxstack 1` and no locals. A PE writer may use tiny headers (685 of the 2,670 bodies have no locals); an ilasm comparison must compare max stack, locals and code, not headers. |
| E6 | `branch-estimate.py`: long branches whose offset fits int8 | 18,158 of 21,758 (83.5%); about 54 KB of 849 KB (6.4%). |
| E7 | `privatescope/pst3.il`: two global `privatescope static int32 'helper'(int32)` with identical name and signature in `<Module>`, referenced as `'helper$PST06000001'` / `'helper$PST06000002'` | Both ilasms assemble it (plain names fail with "unresolved global member ref"); runs on .NET Framework 4.8 and .NET 8, exit 33 (= 11 + 22). C `static` functions from different objects can be merged into one `<Module>` without renaming. |
| E8 | `clang --target=wasm32 -O1 -c w.c`, `wasmdump.py` | one cc1 `-emit-obj` job; 10 code relocations, 2 data relocations, symbol table (section 3.2). |
| E9 | driver probes with the built clang | `-c`, `-save-temps -c`, `-c x.s`, `-fno-integrated-as -c`: `err_drv_msil_no_object_output`; `-###` shows the single `-emit-obj` job; `clang -cc1as -triple x86_64-pc-windows-msvc-msil -filetype obj` **asserts** at `cc1as_main.cpp:600` (`MAB && "Unable to create asm backend!"`), exit 132. |
| E10 | `ninja -C D:/llvm-msil/build -t deps` (122 MB dump, 2,691 objects) + `.ninja_log` durations (`header-impact.py`, `header-union.py`) | section 6. |

## 5. Design: the MSIL MC object layer

### 5.1 Principles

1. **One event stream.** The AsmPrinter emits MCInsts, labels and structured
   `MSILTargetStreamer` calls only. `MSILTargetAsmStreamer` turns them into today's text;
   `MSILTargetObjectStreamer` records them for the writer; the null streamer drops them. Comments
   become `emitComment` (no-op except in text).
2. **One MCInst = one CIL instruction.** Expansion happens once, in `MSILMCInstLower`; the printer
   and the code emitter are table-driven from the same TableGen record, so they cannot disagree.
   MachineInstr-level records, `StackPeakExtra` and MSILStackCheck do not change.
3. **Symbols carry the semantics** (`MCSymbolMSIL`): kind, signature, binding, import module/name,
   BCL member descriptor. Expressions are `MCSymbolRefExpr` with an MSIL specifier, as wasm does.
4. **Only the writer is format-specific.** Method sections, fixups, branch resolution, symbol
   binding and the method/field records are the same for both object-format candidates.

### 5.2 Component list

New files (none is included by existing code, so none forces a rebuild):

| File | Contents | Size (estimate) |
|---|---|---|
| `llvm/include/llvm/BinaryFormat/MSIL.h` (+ optional `MSILRelocs.def`) | ECMA-335 element types (`ELEMENT_TYPE_I4`, `I`, `I8`, `R4`, `R8`, `VOID`, `STRING`, `SZARRAY`, `PTR`, `VALUETYPE`, ...), `msil::Signature` (call convention, return and parameter **type nodes**, not a closed enum: typed pointers and value types must fit later), relocation kinds (candidate A), object format constants. Shared by MC, the target and lld (as `wasm::WasmSignature` is). | 250 |
| `llvm/include/llvm/MC/MCSymbolMSIL.h` | symbol subclass (5.3) | 120 |
| `llvm/include/llvm/MC/MCMSILStreamer.h`, `llvm/lib/MC/MCMSILStreamer.cpp` | `MCObjectStreamer` subclass (its constructor is protected, `MCObjectStreamer.h:68-75`), the `MCWasmStreamer` shape: `emitSymbolAttribute`, `emitCommonSymbol`, `changeSection`, `emitLabel`, `finishImpl`; factory `createMSILStreamer` | 200 |
| `llvm/include/llvm/MC/MCMSILObjectWriter.h` | `class MCMSILObjectTargetWriter : public MCObjectTargetWriter { Triple::ObjectFormatType getFormat() const override { return Triple::MSIL; } virtual std::unique_ptr<MCObjectWriter> createObjectWriter(raw_pwrite_stream &OS) = 0; }` so lib/MC dispatches without linking the target | 60 |
| `MCTargetDesc/MSILMCCodeEmitter.cpp` | 5.4 | 250 |
| `MCTargetDesc/MSILAsmBackend.cpp`, `MSILFixupKinds.h` | 5.5 | 200 |
| `MCTargetDesc/MSILTargetObjectStreamer.cpp` (or in `MSILTargetStreamer.cpp`) | 5.6 | 300 |
| `MCTargetDesc/MSILObjectWriter.cpp` (+ a metadata builder for candidate B) | 5.8; candidate A about 1,200 lines; candidate B shares an ECMA-335 metadata/PE builder with msil-ld (about 2,500 lines, written once) | 1,200-2,500 |
| `MSILInstrCIL.td` | MC-level CIL records (5.3) | 200 |

Changed files: `MCContext.cpp:315-318` (create `MCSymbolMSIL`), `TargetRegistry.cpp:93-96`
(`S = createMSILStreamer(...)`), `MCAsmBackend.cpp` (two `case Triple::MSIL:` calling the factory),
`llvm/lib/MC/CMakeLists.txt`; in the target `MSILMCTargetDesc.cpp` (register emitter, backend,
object target streamer), `MSILTargetMachine.cpp` (drop the ObjectFile error), `MSILMCAsmInfo.cpp`
(`UseIntegratedAssembler` can become true; it only affects inline asm, which MSILPrepareIR rejects,
and section-name uniquing in TLOF code MSIL does not use), `MSILAsmPrinter`, `MSILMCInstLower`,
`MSILInstPrinter`, `MSILMCExpr` (replaced or reduced), `MSILTargetStreamer`, `MSILTargetObjectFile`,
`MSILInstrInfo.td`/`MSILInstrFormats.td`, `MCTargetDesc/CMakeLists.txt` (+ `MSILGenMCCodeEmitter.inc`
via `-gen-emitter`, as WebAssembly does). In clang: `Clang.cpp:5541-5543` and `9340-9342`,
`ToolChain.cpp:736-765`, `DiagnosticDriverKinds.td:163-165` (section 7).

The alternative to the lib/MC edits is to construct the streamer and writer inside the existing
`MSILTargetMachine::createMCStreamer` override. It touches no lib/MC file, but `cc1as`, `llvm-mc` and
any other caller of `MCAsmBackend::createObjectWriter` would still reach `llvm_unreachable` once an
asm backend is registered (E9), so the `MCAsmBackend.cpp` case is needed anyway; follow the wasm
layout.

### 5.3 Symbols, signatures and MC-level CIL instructions

```cpp
// llvm/include/llvm/MC/MCSymbolMSIL.h (new). Must stay trivially destructible
// (static_assert list in MCContext::createSymbolImpl, MCContext.cpp:287-296).
namespace llvm {
namespace msil { struct Signature; struct MemberRef; } // BinaryFormat/MSIL.h
class MCSymbolMSIL : public MCSymbol {
public:
  enum class Kind : uint8_t {
    Unknown,
    Method,        // a C function: defined (has a method section) or undefined
    Data,          // a C global: its address field (D12); size/align on the symbol
    FuncAddrField, // '<msil>fn_<f>': the address of function f (M9); owned by the linker
    Signature,     // a temp symbol carrying a calli stand-alone signature (wasm "typeindex")
    ExternalMember // a BCL type/member reference, bound by msil-ld (P2-B)
  };
private:
  Kind K = Kind::Unknown;
  bool IsWeak = false, IsLinkOnce = false, IsHidden = false;
  const msil::Signature *Sig = nullptr;          // Method, Signature
  const msil::MemberRef *Member = nullptr;       // ExternalMember
  const MCSymbolMSIL *FuncTarget = nullptr;      // FuncAddrField
  std::optional<StringRef> ImportModule, ImportName; // "msil-import-module"/"-name"
  const MCExpr *Size = nullptr;                  // Data
public:
  MCSymbolMSIL(const MCSymbolTableEntry *Name, bool IsTemporary)
      : MCSymbol(Name, IsTemporary) {}
  // getters/setters as in MCSymbolWasm
};
} // namespace llvm
```

- **Names.** Symbols are named by `MSIL::getILName` (IR names, decision D11), not by the Mangler,
  so the symbol table, the text and the metadata spell one name. Local (internal) symbols may
  collide across objects; E7 shows the linker can keep both as `privatescope` members.
- **Signature storage.** A `SpecificBumpPtrAllocator<msil::Signature>` and an interning set in
  `MSILTargetStreamer` (lifetime of the streamer, which outlives `MCAssembler::Finish`). No
  `MCContext::createMSILSignature`: that would edit `MCContext.h` (607 dependents, section 6) and
  is only needed by an asm parser.
- **One name, several signatures.** Only library calls can do this today (`noteExternalSymbol`,
  `MSILAsmPrinter.cpp:327-343`, for example `ldexp` with an int32 and a native int exponent). The
  object needs one symbol per (name, signature): keep the first under the name and give later ones
  a reserved suffix with `ImportName` set to the real name. Rare; decide in P2-A.
- **MC-level CIL instructions.** `MSILMCInstLower` lowers each MachineInstr to one or more MCInsts
  whose records are exactly one CIL instruction:

| MachineInstr | MCInsts emitted |
|---|---|
| `ARG_GET_* i` | `LDARG_0..3`, `LDARG_S i` (i < 256), `LDARG i` |
| `ARG_SET_* i`, `ARG_TEE_* i` | (`DUP`), `STARG_S i` / `STARG i` |
| `LOCAL_GET/SET/TEE_* i` | `LDLOC_0..3` / `_S` / long; (`DUP`), `STLOC_0..3` / `_S` / long |
| `CONST_I32 v` | `LDC_I4_M1`, `LDC_I4_0..8`, `LDC_I4_S`, `LDC_I4` |
| `CONST_I64 v` | `LDC_I4 v` or `LDC_I8 v`, `CONV_I` |
| `GLOBAL_ADDR g+off` | `LDSFLD <g>`, then `LDC_I4 off` (or `LDC_I8 off`, `CONV_I`), `ADD` |
| `FUNC_ADDR f` | `LDSFLD <fn-addr field of f>` |
| memory ops with memflags | (`UNALIGNED n`), (`VOLATILE`), the access |
| `BR_TABLE_I32` | `SWITCH t1..tn`, `BR <last>` |
| `TRAP` | `LDNULL`, `THROW` |
| `MEMMOVE` | `CONV_I8`, `DUP`, `CALL <Buffer::MemoryCopy>` |
| `MEMORY_BARRIER` | `CALL <Interlocked::MemoryBarrier>` |
| `CALL`, `CALL_INDIRECT` | `CALL <method>`, `CALLI <sig symbol>` |
| everything else (already 1:1) | itself |

  New records (`isCodeGenOnly`, no patterns): the short forms, `DUP`, `LDNULL`, `THROW`, `CONV_I8`,
  `UNALIGNED`, `VOLATILE`, `SWITCH`, and for synthesized code `LDFTN`, `STSFLD`, `LDSFLDA`,
  `LDLEN`, `LDELEM_REF` (about 30). Add a `CILOperand` field to `MSILInst` (None, Var8, Var16, I8,
  I32, I64, R32, R64, Token, BrTarget32, Switch; future BrTarget8) exported through TSFlags, so
  printing, encoding and a later disassembler share one operand table. The `-S` text does not
  change (it already prints these lines); the shortcuts of `MSILInstPrinter` (`getShortForm`,
  `printBrTable`'s trailing `br`, `printGlobalAddrOffset`, `printMemFlagsOperand`) move into
  MCInstLower.
- **Synthesized methods** (`.cctor`, `<msil>Main`, the per-object init methods of 5.9) become
  `SynthesizedBody` objects that build MCInsts and temp-symbol labels instead of strings, keep the
  stack-depth bookkeeping, and go through `EmitToStreamer` like any function.
- **BCL references** come from a closed table `MSIL::BCLMember` (namespace, type, name, signature):
  `Marshal.AllocHGlobal`, `Marshal.StringToHGlobalAnsi`, `Environment.GetCommandLineArgs`,
  `Buffer.MemoryCopy`, `Interlocked.MemoryBarrier`, and the type `System.ValueType`; each becomes an
  `ExternalMember` symbol. The text printer adds the `[mscorlib]` scope (D10); the object records the
  member unbound and msil-ld binds it (P2-B). Phase 3 extends the table (ThreadStatic,
  ProcessExit, Interlocked, Half, ...).

### 5.4 MSILMCCodeEmitter

`encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB, SmallVectorImpl<MCFixup> &Fixups,
const MCSubtargetInfo &STI) const` (signature as in `WebAssemblyMCCodeEmitter.cpp:110-112`):

1. Opcode: `Inst` (`getBinaryCodeForInstr`, `MSILGenMCCodeEmitter.inc`): one byte, or `0xFE` then
   one byte.
2. Operands by `CILOperand`:

| Operand | Bytes | MC operand |
|---|---|---|
| Var8 / I8 (`ldarg.s`, `ldloc.s`, `ldc.i4.s`, `unaligned.`) | 1 | Imm |
| Var16 (`ldarg`, `ldloc`, `starg`, `stloc`) | 2 LE | Imm |
| I32 / I64 | 4 / 8 LE | Imm |
| R32 / R64 | 4 / 8 LE, exact bit pattern | SFPImm / DFPImm (keeps signaling NaNs, unlike ilasm, E4) |
| Token | 4 zero bytes + fixup `fixup_cil_<class>_token` at the operand offset | `MCSymbolRefExpr(MCSymbolMSIL, MSIL::Specifier)` |
| BrTarget32 | 4 zero bytes + `FK_Data_4` PC-relative fixup on `Target + (-4)` | block label |
| Switch | `u32 N`, then entry i (0-based) as above with constant `-4 * (N - i)` | N block labels |

MCAssembler computes a PC-relative value as target - (fragment offset + fixup offset)
(`MCAssembler.cpp:175-176`), so the constants give "relative to the next instruction" for branches
and "relative to the end of the switch" for switch entries, exactly what E3 checked against ilasm.
Branch targets are always in the method's own section, so they always resolve; a target in another
section is an internal error reported by the writer.

### 5.5 MSILAsmBackend and fixup kinds

```cpp
namespace llvm::MSIL {
enum Fixups {
  fixup_cil_method_token = FirstTargetFixupKind, // InlineMethod: call, ldftn (later newobj, jmp)
  fixup_cil_field_token,  // InlineField: ldsfld, stsfld, ldsflda
  fixup_cil_sig_token,    // InlineSig: calli
  fixup_cil_type_token,   // InlineType: later (ldobj/stobj/sizeof/initobj, value-type frames, SIMD)
  fixup_cil_tok_token,    // InlineTok: later (ldtoken)
  fixup_cil_string_token, // InlineString: later (ldstr)
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
enum Specifier : uint16_t { S_None, S_FuncAddrField, S_ExternalMember, S_Sig };
}
```

- `getFixupKindInfo`: every target kind is `{name, 0, 32, 0}`.
- `applyFixup(F, Fixup, Target, Data, Value, IsResolved)`: unresolved -> `recordRelocation` and
  leave zeros; resolved (branches) -> write `int32` little-endian. Token fixups are never resolved
  by MCAssembler (a symbol value is not absolute, `MCAssembler.cpp:195`), so no `evaluateFixup`
  override is needed.
- `mayNeedRelaxation` false in P2-A. Later: emit `br.s`/`b<cc>.s` and relax to the long form
  (`relaxInstruction`); text keeps long forms (it cannot know offsets), so the round-trip
  comparison then normalizes branch forms (E6: 6.4% of IL).
- `writeNopData`: `nop` (0x00); never needed (no code alignment).
- `createObjectTargetWriter`: `MSILObjectTargetWriter` (`MCMSILObjectTargetWriter`, format
  `Triple::MSIL`).

### 5.6 Streamers and method sections

`MSILTargetStreamer` grows from text helpers to structured declarations (names indicative):

| Call | Text streamer | Object streamer |
|---|---|---|
| `emitModuleHeader(Info)` (source file, triple, assembly name, BOM) | today's header and `.assembly` lines | module record (triple, name); no assembly identity in an object |
| `emitAssemblyRefs()` | `.assembly extern mscorlib` (D10) | nothing (references are unbound until P2-B) |
| `emitMethodBegin(Decl)` with `Decl = {MCSymbolMSIL *Sym, Signature, Access (public, assembly, private, privatescope), Flags (hidebysig, specialname, rtspecialname, entry point), ParamNames}` | method header + `{` | `switchSection(getMSILSection(".text.<n>", Text))` (unique per method; `getMSILSection` uniques by name, `MCContext.cpp:961-971`), `emitLabel(Sym)`, open a method record |
| `emitMaxStack(N)`, `emitLocals(Types)` | lines | stored in the method record (the writer builds the header and the local signature; E5) |
| `emitMethodEnd(Decl)` | `}` line | close the record; code size = section size |
| `emitPInvoke(Decl, Lib, Entry)` | `pinvokeimpl` line | candidate A: import attributes on the undefined symbol; candidate B: MethodDef + ImplMap stub |
| `emitDataImage(...)`, `emitAddressField(Sym, Access)` | today's lines | image bytes and data symbols |
| `emitComment(Twine)` | comment line | nothing |

The method header is emitted from `emitFunctionEntryLabel` as today; `AsmPrinter::emitFunctionHeader`
switches to the TLOF section first, and the target streamer switches again to the method section,
which also covers the synthesized methods emitted from `emitEndOfAsmFile`. In text mode section
switches print nothing (`MSILMCAsmInfo` keeps the empty `printSwitchToSection`).

### 5.7 AsmPrinter changes in one place

- Create symbols with `OutContext.getOrCreateSymbol(MSIL::getILName(GV))` and type them
  (`Method`, `Data`) when first seen: definitions in `emitFunctionEntryLabel` /
  `emitGlobalVariable`, references in `MSILMCInstLower`. The data collector's `SymToGV` map
  (`MSILAsmPrinter.cpp:532-543`) keeps using `AsmPrinter::getSymbol` for initializer symbols and
  maps them to these symbols.
- Linkage goes on the symbol (`IsWeak`, `IsLinkOnce`, `IsHidden`, local/global) instead of
  `emitLinkage` text (which stays empty, `MSILAsmPrinter.cpp:1124-1127`); comdats and linkonce stop
  being "ordinary definitions" (D26) in object mode, since msil-ld must deduplicate them across
  objects (MSVC string literals `??_C@...` are linkonce_odr in every TU that uses them).
- `emitModuleInitializer`, `emitEntryPoint`, `emitPInvokeDeclarations`,
  `emitFunctionAddressFields`: split into what the object contains and what the linker (or, for
  `-S`, the self-link trailer of 5.10) produces; see 5.9.

### 5.8 Fixups and relocations for the two leading candidates

Candidate A is the wasm-style container (Design.md 5.1, inputs.md option A). Candidate B is the
symbolic, metadata-based object (inputs.md options B/(2) and the "real assembly as object" variants:
the object is a valid ECMA-335 module, unresolved functions are MethodDef + ImplMap stubs on a
reserved module or MemberRefs on a placeholder parent). The MC side up to `recordRelocation` is the
same; the writer decides.

| CIL operand | Expression (symbol kind, specifier) | Fixup | A: relocation in the object, linker action | B: what the object writer creates, linker action |
|---|---|---|---|---|
| `call f` / `ldftn f`, f defined here | Method, S_None | method_token | `R_MSIL_METHOD_TOKEN(sym)`; linker writes the MethodDef token | MethodDef row, token written in place; linker remaps (row renumbering) |
| `call f`, f undefined C function | Method (undefined), S_None | method_token | same, undefined symbol (+ ImportModule/Name); linker: definition, managed export (P2-C) or P/Invoke | MethodDef stub + ImplMap(`msil.unresolved` or the attribute's module); linker replaces the stub by the definition or fixes the module name (the D14 shape) |
| `call <BCL member>` | ExternalMember, S_ExternalMember | method_token | `R_MSIL_MEMBER_TOKEN(index into a member-ref table)`; linker binds by searching references (P2-B) | MemberRef on a TypeRef whose scope is a placeholder AssemblyRef; linker rebinds the scope |
| `ldsfld/stsfld/ldsflda 'g'` (address field) | Data, S_None | field_token | `R_MSIL_FIELD_TOKEN(sym)`; defined or undefined global | FieldDef (defined) or MemberRef(field) on a placeholder parent (undefined) |
| `ldsfld '<msil>fn_f'` | FuncAddrField(f), S_FuncAddrField | field_token | `R_MSIL_FUNC_ADDR_TOKEN(sym f)`; the linker creates one field per address-taken function and sets it with one `ldftn` (the wasm table-index model, `R_WASM_TABLE_INDEX_*`) | MemberRef to `'<msil>fn_f'` on the placeholder parent; same linker action |
| `calli <sig>` | Signature temp symbol, S_Sig | sig_token | `R_MSIL_SIG_TOKEN(index into the object's signature table)` (as `R_WASM_TYPE_INDEX_LEB`) | StandAloneSig row |
| `.locals` | method record | none | local types in the method record | StandAloneSig row in the method header |
| `br`, `b<cc>`, `switch` | block label | FK_Data_4, PC-relative | resolved by MCAssembler; never a relocation | same |
| later: InlineType/Tok/String | type/member/string symbols | type/tok/string_token | `R_MSIL_TYPE_TOKEN`, ... (value types in P/Invoke signatures, Design.md 2.3) | TypeRef/TypeDef/TypeSpec/UserString rows |

Two consequences to carry into the object-format decision:

- **Type tokens inside signatures.** Struct-by-value P/Invoke signatures (Design.md 2.3), typed
  pointers (FeatureIdeas 3) and `Vector128<T>` (Design.md 3.2) put TypeDefOrRef coded indices
  inside signature blobs. In A that means relocations inside blobs (or structured signatures that
  the linker serializes); in B they are ordinary rows. This is why `msil::Signature` should hold
  type nodes, not the Phase-1 five-value `ValType` enum.
- **Candidate D** (ELF or COFF relocatable with CIL in sections) is, at the MC level, candidate A
  with an ELF/COFF-shaped writer. It cannot reuse LLVM's ELF/COFF writers, which require
  `MCSectionELF`/`MCSymbolELF` and an ELF/COFF MC environment, while M1 made MSIL triples `IsMSIL`;
  it would need its own writer and machine/relocation numbers (COFF.h + ELF.h: 92 dependents, E10).

### 5.9 Data and initialization: what the object carries

Two models, independent of A/B:

1. **Linker layout (wasm model).** Each global is emitted into its own data section through the
   stock `AsmPrinter::emitGlobalVariable`; pointers in initializers become `FK_Data_8`/`FK_Data_4`
   relocations (`R_MSIL_DATA_ADDR64/32`, `R_MSIL_DATA_FUNC_ADDR64`, differences); msil-ld lays out
   one segment and synthesizes the whole `.cctor` (allocation, copy, address fields, relocations,
   ctors), as wasm-ld synthesizes `__wasm_apply_data_relocs` and `__wasm_call_ctors`. `-S` would then
   need a second implementation of the same trailer in the AsmPrinter, or a shared one.
2. **Per-object segment (recommended).** The backend keeps `MSILDataSegment` (D12) per object and
   emits two internal methods per object: `<msil>init_data` (allocate, copy the RVA image, zero the
   rest, set this object's address fields) and `<msil>init_relocs` (store the relocated addresses,
   reading other objects' address fields through ordinary field tokens). The object lists them, and
   the `llvm.global_ctors` entries with priorities, in an init table (wasm's `WASM_INIT_FUNCS`
   subsection, `Wasm.h:205`). msil-ld only emits a `.cctor` that calls every `init_data`, then every
   `init_relocs`, then the ctors in priority order, plus `Main` and the function-address fields.
   - The object then has **no data relocations at all**, only IL token fixups; symbol differences
     stay resolved inside the object (`MSILDiff`); a difference across objects is an error (rare in C).
   - Weak and linkonce globals: msil-ld keeps one address field per name; the defining objects'
     `init_data` store weak/linkonce fields only if the field is still zero, strong ones
     unconditionally, so the strong definition wins in any call order and readers (all after
     `init_data`) see one value. Duplicate bytes stay in each object's segment (dead data).
   - Costs: one `AllocHGlobal` per object, dead bytes for discarded weak/linkonce data, no
     `--gc-sections` for data inside a segment.
   - It matches FeatureIdeas 6 (per-instance memory) and 7 (per-triple segments and initializers)
     better than one linker-laid-out segment.

### 5.10 `-S` and `-c` from one source of truth

- The text and the object are two encodings of the same AsmPrinter events (5.1); with one MCInst
  per CIL instruction the printer and the emitter read the same record.
- `-S` keeps today's contract: a runnable single-module ILAsm file that ilasm assembles (plan 5.1;
  Design.md 5.5 keeps ilasm as the oracle). It is "the object plus a self-link": the same object
  content followed by the trailer that msil-ld would add for a one-object link (`.cctor` calling the
  init methods and ctors, function-address fields, P/Invoke declarations from the D14 table).
  Implement that trailer once, as an MC-level builder in the MSIL target library that emits
  MCInsts; the AsmPrinter calls it for `-S`, and msil-ld (which links the target libraries through
  `${LLVM_TARGETS_TO_BUILD}`, `lld/wasm/CMakeLists.txt:21-22`, experimental targets included,
  `llvm/CMakeLists.txt:737-739`) calls it and encodes with the same `MSILMCCodeEmitter`.
- Candidate B adds an even stronger property: the object itself is a module, so `-S` text of the
  object (with placeholder references) assembles with ilasm into an equivalent object, and ildasm
  and System.Reflection.Metadata read objects directly.
- Tests:
  1. `llc -show-mc-encoding` FileCheck tests as soon as the code emitter and asm backend are
     registered (`CodeGenTargetMachineImpl.cpp:184-194` creates the emitter for `-S` when
     `ShowMCEncoding` is set), before any writer exists.
  2. An oracle leg: for each execution test, compare `-S | ilasm` with `-c | msil-ld` using an
     `ilcensus`-style comparator (bodies byte-identical with tokens compared by kind and name, max
     stack, locals, metadata by name). E3 shows this is exact today; known differences to allow:
     float32 signaling NaNs (E4), header kind (E5), later short branches.
  3. Optional: an `MCDisassembler` for CIL (table-driven from the same records) for
     `llvm-mc --disassemble` round trips; `llvm-objdump` support needs the Object-library edits of
     section 6.
- Limits that move: the 1023-byte identifier limit and the sNaN rewrite are ilasm's, so the object
  path does not have them; whether the CLR limits member name length is *unverified*.

## 6. Headers that must change, and the front-loaded milestone

### 6.1 Measured dependents

Method: `ninja -C D:/llvm-msil/build -t deps` (deps log of the last builds: 2,691 objects of the
targets this tree builds, `llc opt clang FileCheck ... MCTests MSILTests TargetParserTests`) and the
per-object durations in `.ninja_log` (3.00 CPU-hours of compile in total). "Minutes" scales a
header's share of compile CPU to the measured 47-minute full rebuild; links are negligible
(`clang.exe` 7.2 s, `opt.exe` 3.8 s, `llc.exe` 2.2 s). Tools not built here (lld, llvm-ar, llvm-nm,
llvm-objdump, llvm-readobj, llvm-mc) are not counted: adding all of them adds only **158 new
objects** (`ninja -t inputs`), because their libraries are already built.

| Header | Objects | Share of compile CPU | Est. minutes | Needed by |
|---|---|---|---|---|
| `TargetParser/Triple.h` (M1, for scale) | 2,102 | 86.5% | 41 | nothing in Phase 2 |
| `MC/MCSection.h`, `MC/MCFixup.h` | 653 each | ~22% | ~10 | nothing (target fixup kinds start at `FirstTargetFixupKind`) |
| `MC/MCExpr.h` | 625 | ~21% | ~10 | nothing (specifiers are target enums) |
| `MC/MCContext.h` | 607 | 20.3% | 10 | only a `createMSILSignature` (avoid; 5.3) |
| `MC/MCSymbol.h` | 605 | 20.0% | 9 | nothing (no format kind enum, `MCSymbol.h:45-49`) |
| `MC/MCAsmInfo.h` | 144 | 6.0% | 3 | nothing |
| `MC/MCObjectFileInfo.h` | 104 | ~4% | ~2 | nothing (methods get sections through `getMSILSection`) |
| `MC/TargetRegistry.h` | 69 | ~3% | ~1 | nothing (the `Triple::MSIL` case is in the .cpp) |
| `MC/MCAsmBackend.h`, `MCObjectWriter.h`, `MCCodeEmitter.h`, `MCAssembler.h`, `MCObjectStreamer.h` | 20-40 | ~1% | <1 | nothing |
| `MC/MCSectionMSIL.h` | 3 | 0.1% | 0 | optional: a COMDAT group or method flag |
| `BinaryFormat/Magic.h` | 147 | ~6% | ~3 | P2-B: `file_magic::msil_object` for lld input detection, llvm-ar symbol tables, llvm-nm (candidate A; candidate B is a PE, already `pecoff_executable`) |
| `Object/Binary.h`, `Object/ObjectFile.h` (+ `SymbolicFile.h` 145) | 158, 135 | ~7% | ~3 | P2-B: `ID_MSIL`, `createMSILObjectFile` (candidate A, if objects are `ObjectFile`s) |
| `BinaryFormat/COFF.h` + `ELF.h` | 92 | 3.6% | 2 | candidate D only |
| clang `DiagnosticDriverKinds.inc` (+ `Enums.inc`) | 84 | 2.6% | 1 | P2-A: reword or replace `err_drv_msil_no_object_output` (no new diagnostic **group**: `DiagnosticGroups.inc` has 665 dependents, 37.4%, 18 min) |
| clang `Options.inc` | 80 | 2.3% | 1 | only if the driver gets new options (P2-B/C) |
| clang `Driver/Types.def` | 76 | 1.9% | 1 | only if managed `.dll` inputs get their own type (Design.md 8) |
| target headers (`MSILMCTargetDesc.h`, `MSILGenInstrInfo.inc`) | 25 | 1.0% | <1 | P2-A (the whole MSIL target: 62 objects, 5.8 CPU-minutes) |

Full numbers: `mc-streamer-files/header-impact.txt` and `header-union.txt`.

### 6.2 What P2-A actually needs

- **No edit of an existing widely-included header.** New headers only: `MCSymbolMSIL.h`,
  `MCMSILStreamer.h`, `MCMSILObjectWriter.h`, `BinaryFormat/MSIL.h` (+ `.def`); they have no
  dependents until the new code includes them.
- **`.cpp` edits in lib/MC** (recompile one file each, relink): `MCContext.cpp` (createSymbolImpl),
  `TargetRegistry.cpp` (streamer), `MCAsmBackend.cpp` (both writer switches), plus
  `llvm/lib/MC/CMakeLists.txt`. These are fork hooks like M1's; the upstream-hooks check of M11.d
  should learn them.
- **clang**: `Clang.cpp`, `ToolChain.cpp` (.cpp) and one `DiagnosticDriverKinds.td` text change (84
  objects).
- The plan's P2-A text ("Requires a batched broad-rebuild header commit (MCSymbol kind if needed,
  MCContext createSymbolImpl, BinaryFormat additions)") overestimates: none of the three forces a
  broad rebuild in this checkout.

### 6.3 Proposed front-loaded milestone "P2-0" (the M1 analogue, about 5 minutes)

Batch every shared edit Phase 2 is expected to need into the first milestone, so later milestones
only rebuild the MSIL target, lib/MC `.cpp` files and new tools:

1. lib/MC hooks and new MC/BinaryFormat headers (above), with stub factories that report a clean
   "not implemented yet" error until P2-A lands the writer (so `cc1as -filetype obj` and llvm-mc stop
   asserting, E9).
2. If the object format is candidate A: `BinaryFormat/Magic.h` + `Magic.cpp`, `Object/Binary.h`,
   `Object/ObjectFile.h` (+ `SymbolicFile.cpp` dispatch) for `file_magic::msil_object` and an
   `MSILObjectFile` stub, so llvm-ar/llvm-nm/lld can recognize objects (159 objects).
3. clang: the driver diagnostic texts for `-c` (now allowed), `-save-temps`/`/FA`/`-via-file-asm`
   (still errors, section 7), and the P2-B link messages; `Options.td`/`Types.def` only if P2-B/C
   planning already knows the options and input types.
4. lld: flavor registration (`lld/include/lld/Common/Driver.h`, `lld/tools/lld/lld.cpp`) and the
   CMake for `lld/MSIL` with a stub driver. lld is not built today, so this is new compile, not
   rebuild.
5. The `lit`/gate wiring to build `lld`, `llvm-ar`, `llvm-nm`, `llvm-mc` (+158 objects).

Measured cost of items 1-3 with every optional header: 245 objects, 0.28 CPU-hours, **9.3% of
compile CPU, about 4-5 minutes**, plus relinks. Avoid in all of Phase 2: `MCContext.h`,
`MCSymbol.h`, `MCSection.h`, `MCExpr.h`, `MCFixup.h` (each about 10 minutes), clang diagnostic
groups (18 minutes), IR headers and `Triple.h`.

## 7. The AsmParser question

**Where it stands.** `AsmParser.cpp:808-812` reports "assembly parsing is not supported for MSIL"
(a usage error, pinned by `clang/test/CodeGen/MSIL/cc1as-unsupported.s` for `-filetype asm` and
`null`). The driver never assembles for MSIL (E9). `-filetype obj` through `cc1as` asserts today
(`cc1as_main.cpp:600`) and would hit `MCAsmBackend.cpp:60-61` once a backend exists; the
`Triple::MSIL` case of 6.3 item 1 fixes both.

**Who would need a parser.**

| Path | Needs `.il` -> `.o`? | Phase 2 answer |
|---|---|---|
| `clang -c foo.c`, `llc -filetype=obj`, LTO in msil-ld | no (IR -> object) | integrated, no parser |
| `clang -save-temps -c`, clang-cl `/FA` or `/Fa`, `-via-file-asm` | yes: the driver refuses to collapse the assemble step (`Driver.cpp:4880-4886`) and runs `cc1as` on the `.il` | keep a clean error ("-save-temps cannot assemble ILAsm for MSIL; ...") in `Clang.cpp:9340-9342`, or special-case MSIL in `canCollapseAssembleAction` so cc1 writes the object and the `.il` is a side output; decide in P2-A |
| `clang -c foo.il` / `foo.s`, `llvm-mc -filetype=obj` | yes | clean error; hand-written IL is not a C-toolchain need (Design.md 6 puts helpers in IL generated by the toolchain or in C) |
| MC-layer tests (`llvm/test/MC/WebAssembly` style) | convenient | use `.ll`/`.mir` -> `llc -filetype=obj` -> dumper, `-show-mc-encoding`, and the ilasm oracle (5.10) |
| inline assembly | no | MSILPrepareIR rejects every inline asm (`MSILPrepareIR.cpp:580-584`); `AsmPrinter::emitInlineAsm` never runs; LTO's irsymtab needs a parser only for module asm (registry-tm.md 1.4), which clang rejects for MSIL |

**Recommendation.** IR -> object is enough for Phase 2. Do not build an MSIL `MCAsmParser`; keep the
`IsMSIL` hook; make the `-save-temps`/`/FA`/`.il` paths clean errors with a clear message. If a
parser is ever wanted (llvm-mc tests, a `.il` input type, `-save-temps`), it is about the size of
WebAssembly's (1,412 + 722 lines) for the subset the backend prints, and candidate B makes it least
useful because ilasm can already assemble an object's text. Revisit after Phase 4.

## 8. Work-package ideas for P2-A (disjoint files)

| WP | Files | Content |
|---|---|---|
| a. lib/MC hooks | `MCContext.cpp`, `TargetRegistry.cpp`, `MCAsmBackend.cpp`, `lib/MC/CMakeLists.txt`, new `MCSymbolMSIL.h`, `MCMSILStreamer.{h,cpp}`, `MCMSILObjectWriter.h` | 5.2, 5.3; unit tests in `llvm/unittests/MC` |
| b. BinaryFormat | new `BinaryFormat/MSIL.h` (+ `.def`, `lib/BinaryFormat/MSIL.cpp` for signature blobs) | element types, signatures, relocation kinds, constants |
| c. CIL records and lowering | `MSILInstrFormats.td` (`CILOperand`), new `MSILInstrCIL.td`, `MSILInstrInfo.td`, `MSILMCInstLower.{h,cpp}`, `MSILInstPrinter.{h,cpp}`, `MSILMCExpr.{h,cpp}` | 5.3 table; `-S` output unchanged (all existing llc/clang tests must pass untouched) |
| d. Encoder and backend | new `MSILMCCodeEmitter.cpp`, `MSILAsmBackend.cpp`, `MSILFixupKinds.h`; `MSILMCTargetDesc.{h,cpp}`, `MCTargetDesc/CMakeLists.txt` | 5.4, 5.5; `-show-mc-encoding` tests |
| e. Streamers and AsmPrinter | `MSILTargetStreamer.{h,cpp}`, new `MSILTargetObjectStreamer.cpp`, `MSILAsmPrinter.{h,cpp}`, `MSILTargetObjectFile.{h,cpp}`, `MSILTargetMachine.cpp`, `MSILMCAsmInfo.cpp` | 5.6, 5.7, 5.9 |
| f. Object writer | new `MSILObjectWriter.cpp` (+ metadata builder for B) | 5.8 |
| g. Driver | `clang/lib/Driver/ToolChains/Clang.cpp`, `ToolChain.cpp`, `DiagnosticDriverKinds.td` | `-c` allowed; section 7 errors |
| h. Tests and tools | `llvm/test/CodeGen/MSIL/*encoding*.ll`, `llvm/test/MC/MSIL/`, the comparator (a C# tool next to `msil-jitall`, or a Python+SRM script), lit legs | 5.10 |

Dependencies: c before d (records), a+b before d/e/f, e before f's end-to-end tests. c can land
first on its own (pure refactor with unchanged text), which de-risks the rest.

## 9. Risks and open questions

1. **Object format choice drives the writer only** (A, B, D); decide it before WP f, not before
   a-e. Data model (5.9) is the other cross-cutting choice: it decides whether msil-ld lays out data.
2. **Signature model.** The five-value `MSIL::ValType` cannot express value types, typed pointers,
   `string[]` (Main) or `Vector128<T>`; moving to `msil::Signature` with type nodes in P2-A avoids a
   second migration in P3/P4 (FeatureIdeas 3 is not precluded either way).
3. **Function-address fields.** M9 made one `'<msil>fn_f'` field per function for pointer equality
   on .NET Framework; with several objects only the linker can keep it unique (5.8 `S_FuncAddrField`).
4. **Weak/linkonce semantics across objects** (5.9) need msil-ld tests with two objects; the
   "store if zero" rule is a proposal, not yet tried.
5. **One name, several signatures** for library calls (5.3).
6. **Static functions across objects**: E7 shows `privatescope` works on both runtimes; ILAsm text
   needs the `$PST<token>` suffix for such references, which matters only for a merged module's
   text (ildasm output), not for `-S` of one TU. That ECMA-335 exempts privatescope members from the
   duplicate name+signature rule is from memory; the experiment confirms the behaviour.
7. **Class placement (FeatureIdeas 1)**: do not hard-code `<Module>` as the only owner in the object;
   the method record should allow an owning type later.
8. **Verification policy (D21)** must run on msil-ld output as on ilasm output; the comparator of
   5.10 is the cheapest guard against drift between `-S` and `-c`.
9. **Fork maintenance**: the lib/MC `.cpp` hooks (three files) join the upstream-hooks list of M11.d.

## 10. Reproduction

```text
# E1: TableGen records vs ECMA opcodes
D:\llvm-msil\build\bin\llvm-tblgen.exe --dump-json -I llvm/include -I llvm/lib/Target/MSIL llvm/lib/Target/MSIL/MSIL.td -o msil-records.json
dotnet ilcensus.dll --opcodes > ecma-opcodes.txt        # ilcensus: mc-streamer-files/ilcensus (net8.0)
python inst-table.py msil-records.json ; python crosscheck.py
# E2/E3: corpus, census, encoder
for each msil/test/M1/**/*.c and O in O0 O2:
  clang --target=x86_64-pc-windows-msvc-msil -ffreestanding -Werror=return-type -I msil/test/Inputs -$O -S x.c -o x.il
  C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe -nologo -quiet -dll -pe64 -x64 -output=x.dll x.il
  dotnet ilcensus.dll --bodies x.dll > bodies/x.txt
dotnet ilcensus.dll corpus/*.dll > census.txt
python encode-check.py ecma-opcodes.txt corpus bodies    # {'methods': 2670, ..., 'identical': 2670}
# E4: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 snan.ll -o snan.il, ilasm, encode-check on it
# E7: ilasm -exe pst3.il (both ilasms); run on .NET Framework and with dotnet exec --runtimeconfig (net8)
# E8: clang --target=wasm32 -O1 -c w.c -o w.o ; python wasmdump.py w.o
# E10: ninja -C D:/llvm-msil/build -t deps > deps.txt ; python header-impact.py deps.txt D:/llvm-msil/build/.ninja_log <headers>
#      python header-union.py deps.txt D:/llvm-msil/build/.ninja_log name=h1,h2 ...
#      ninja -C D:/llvm-msil/build -t inputs lld   (and llvm-ar, llvm-nm, llvm-objdump, llvm-readobj, llvm-mc)
```

Scratch cost: about 140 MB on C: (the deps dump is 122 MB), nothing large on D:.
