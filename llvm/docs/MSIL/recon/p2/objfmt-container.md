# Recon: the container of a CIL object file (Phase 2, P2-A)

- Key: `objfmt-container`. Date: 2026-10-20/21. Checkout: branch `msil` at `1ea64fe9fc7c` (LLVM 24.0.0git).
- Question: what bytes is a CIL object file (`clang -c` output), and what does each container choice
  cost and buy? Options (task letters): **(A)** a wasm-style custom container (Design.md 5.1 as
  written); **(B)** a .NET module envelope (netmodule or assembly PE) plus a link stream or PE
  section; **(C)** pure netmodules plus naming conventions and an ILMerge-like merger; **(D)** ELF or
  COFF relocatable objects whose sections hold CIL; **(E)** bitcode-only objects (LTO-only).
- Method: small experiments for every option (section 13 lists them; sources, scripts and trimmed
  logs are in [`objfmt-container-files/`](objfmt-container-files/)), plus reading the LLVM sources
  and git history that a new format touches. Every path and line number below was checked in this
  checkout; claims that were not checked are marked *unverified*.
- Scratch: `C:\llvm-msil-scratch\p2\objfmt-container\` (about 60 MB; nothing written to D: except
  this report and its supporting files, about 310 KB). No tracked file was modified, no cmake or
  ninja ran, nothing was installed.
- Tools used: this tree's `clang`/`llc` (`D:\llvm-msil\build`), VS-bundled LLVM 22.1.3 tools
  (`llvm-nm`, `llvm-ar`, `llvm-lib`, `llvm-objdump`, `llvm-readobj`, `llvm-objcopy`, `llvm-size`,
  `ld.lld`, `lld-link`), MSVC 14.51 (`cl /clr`, `link.exe`, `lib.exe`), .NET Framework ilasm 4.8 and
  ildasm 4.8, NuGet ilasm/ildasm 10.0.12, dotnet-ilverify 10.0.12, PEVerify 4.8, .NET 8.0.32 /
  10.0.13 / Framework 4.8 runtimes, two small C# tools written for this recon on
  System.Reflection.Metadata (`mdinfo`, `netmerge`, section 13).
- Vocabulary (shared with the sibling recon `objfmt-binding`): the **container** is the file
  envelope (headers, sections or streams, string tables). The **payload** is the compiled program:
  CIL method bodies, signatures, data images. The **link information** is what a linker needs
  beyond the payload: the symbol table with C linkage (binding, visibility, COMDAT, common, TLS,
  constructor priority, alignment) and the fix-up sites (relocations, or tokens found by decoding).
  **Binding** (how a reference becomes a final token or address: patching, symbolic names,
  placeholder rows) is `objfmt-binding`'s subject; this report only says which bindings each
  container can carry.

---

## 0. Summary

1. **The container decision is mostly a tooling and plumbing decision, not a payload decision.**
   Whatever the container, the payload is ECMA-335 shaped (method bodies with tiny/fat headers,
   signature blobs, explicit-layout value types for data images and by-value P/Invoke structs), and
   msil-ld needs a full ECMA-335 **writer** (it writes the PE, Design.md 5.5) and **reader**
   (reference assemblies for P2-B binding, managed libraries for P2-C) in every option. The options
   differ in who carries the link information and which existing tools understand the file.

2. **(B) works as an envelope, measured on every reader and runtime.** An ilasm-built assembly or
   netmodule with an extra metadata stream `#MSILLink` (a sibling of `#~`) or an extra PE section
   `.msillnk` is read unchanged by ildasm 4.8 and 10.0.12 and System.Reflection.Metadata, and the
   assembly variants load and run on .NET 8, .NET 10 and .NET Framework 4.8 (exit 43 in every case,
   also when the original metadata copy is overwritten, so the loader reads the relocated root with
   the extra stream). ILVerify and PEVerify report exactly the baseline Phase-1 messages on the
   assembly variants and refuse netmodules ("BadImageFormatException", "expected to contain an
   assembly manifest"). LLVM tools see only PE headers: `llvm-nm` prints "no symbols", so an
   `llvm-ar` index of B objects is empty.

3. **CoreCLR cannot load netmodules at all.** A multi-module assembly (`.file 'two.netmodule'`) runs
   on .NET Framework 4.8 (exit 43) and fails on .NET 8 and .NET 10 with "only single file
   assemblies are supported (0x8013101E)". Any netmodule-based object therefore needs a real merge
   into one module for modern .NET; (C) is not "link by doing nothing".

4. **A merger is small for the Phase-1 shape and fast.** `netmerge` (440 lines of C# on
   System.Reflection.Metadata, `objfmt-container-files/tools/netmerge/Program.cs`) merges
   ilasm-built netmodules into one deterministic assembly: it resolves placeholders (an ImplMap stub
   to the ModuleRef `msil.unresolved`, or a MemberRef whose parent is that ModuleRef), renames
   colliding internal names, rewrites every token in every IL body and every TypeDefOrRef in every
   signature, copies RVA data and chains the inputs' `.cctor`s. On the 100-TU synthetic program
   (52,501 lines of C, 4,302 methods) it merged 101 netmodules, rewrote 24,977 tokens and resolved
   100 placeholders in about 1 s including .NET start-up; the result exits 140 on .NET 8, .NET 10
   and Framework 4.8, the same as the LTO-built program. **One real problem surfaced:** a
   per-object `.cctor` that becomes an ordinary method `'<msil>cctor$N'` writes `initonly` fields
   outside a type initializer, which ILVerify ([InitOnly]) and PEVerify reject; per-object
   initializers must be spliced into the single `.cctor`, or (better) the linker must synthesize the
   module initializer from data and relocation tables, as A, D and E naturally do.

5. **CIL needs no relocation records for code.** IL is self-describing: every opcode's operand size
   is fixed (`switch` carries its count), so a linker finds every token by decoding. Measured density
   on Phase-1 output: 19-68 token operands per KB of IL (mean 36/KB over the 20 corpus programs;
   about one per 13 instructions). In ELF that is about 860 bytes of RELA records per KB of IL, in
   COFF about 360 bytes; B carries none. Data relocations (addresses inside initialized data) are
   needed in every container.

6. **(D) gives almost all tooling for free, even with an unknown machine number.** CIL method
   bodies in per-function sections of an ELF relocatable object (assembled by clang's integrated
   assembler, then `e_machine` patched to an unassigned value or EM_NONE) are read by `llvm-nm`,
   `llvm-ar` (complete index), `llvm-objdump -h/-r/-t`, `llvm-readobj` (section groups, notes),
   `llvm-objcopy` and `llvm-size`; only relocation names print as `Unknown` and `-d` needs a CIL
   disassembler. COMDAT groups, weak and undefined-weak symbols, hidden visibility, common symbols,
   TLS sections, `.init_array.N` priorities and SHF_LINK_ORDER side sections all survive. `ld.lld`
   refuses the unknown machine ("unsupported e_machine value"), which is the safety we want. Adding
   an ELF machine to LLVM took 6-9 files and 33-339 lines in three precedents (BPF, CSKY,
   LoongArch).

7. **MSVC `/clr` is a real "D with embedded metadata" precedent, and LLVM already knows its
   pieces.** A `cl /clr /c` object is COFF (machine AMD64) with one `.text$mn` section per method
   holding a complete CIL method body, `IMAGE_REL_AMD64_TOKEN` relocations against symbols of storage
   class `IMAGE_SYM_CLASS_CLR_TOKEN` named after object-local tokens (`06000007`, `0A00000E`), and a
   `.cormeta` section holding a full ECMA-335 metadata root in uncompressed `#-` form, where
   references to functions and variables of other objects are MemberRefs on the object's own
   `<Module>` TypeDef. `link.exe` merges the metadata and patches the tokens (the linked program
   runs, exit 154). LLVM's `COFF.h` has the CLR token class, aux record and TOKEN relocations, and
   `llvm-objdump`/`llvm-readobj` print them; `lld-link` rejects the object ("0A00000D should not
   refer to special section 0", `lld/COFF/InputFiles.cpp:898`).

8. **(A) costs the most LLVM code for no interoperability gain.** The wasm object format needed
   about 10,400 lines outside lld/wasm (BinaryFormat, MC writer and streamer, Object reader,
   ObjectYAML, obj2yaml, llvm-readobj, llvm-objdump, ObjCopy) plus hooks in 29 shared Object/tool
   files (149 lines), and has had 236-291 commits per core file since 2016-2017. GOFF (3,758 lines,
   18 files) and DXContainer (8,266 lines, 30 files) show the same shape. Every shared hook is one
   more conflict site for the fork's upstream merges (Design.md risk 5).

9. **(E) works today and is the fastest bring-up, but cannot be the only format.** `clang -c -flto`
   already writes bitcode for MSIL triples, LLVM 22's `llvm-nm`/`llvm-ar` read it (weak included),
   and a cc1 `-mlink-bitcode-file` link followed by codegen runs (exit 41). The price is
   whole-program codegen at every link: 24.8 s for the 52-KLOC synthetic program (merge 2.7 s +
   single-threaded O2 codegen), 54.1 s with the full O2 IR pipeline, against about 0.2 s of codegen
   per TU in a separate compile and about 1 s to merge 101 compiled modules. Parallel or cached LTO
   (partitions, ThinLTO) produces per-partition code that must be merged, so it needs an object
   representation anyway.

10. **Windows archivers accept none of the candidates as-is.** `lib.exe` rejects ELF (LNK1107),
    bitcode (LNK1107), our netmodules ("LNK1302: only support linking safe .netmodules; unable to
    link pure .netmodule") and a COFF object with machine CEE (LNK1136); `llvm-lib` accepts only
    COFF and bitcode (machine 0 is "unknown machine: 0"). `llvm-ar` accepts everything. A COFF object
    with machine 0 is the one dangerous variant: `link.exe` and `lld-link` default to x64 and would
    link CIL bytes as machine code.

**Recommendation** (section 11): use **(E)** as the Phase-2 bring-up path and keep it as the
`-flto` path; make the CIL object container **(D) ELF ET_REL** for every MSIL triple, written by a
custom `MSILObjectWriter` (keeping D5's `MCContext::IsMSIL`), with a fork-reserved `e_machine`, a
`.note.msil` carrying the triple, one section per method and per global, ECMA-335-encoded payload
(method bodies and signature blobs) and standard ELF link information. (B) is the credible
alternative if readable-by-ildasm objects or C# netmodule inputs matter more than LLVM tooling; (A)
is not recommended; (C) is a prototype step of (B). Section 11.3 lists the user decisions.

---

## 1. What any CIL object container must carry

Derived from what Phase 1 emits today (`MSILAsmPrinter::emitEndOfAsmFile`,
`llvm/lib/Target/MSIL/MSILAsmPrinter.cpp:488-529`: P/Invoke declarations, data segment, function
address fields, module initializer, entry point) and from C link semantics (Design.md 5):

| Need | Phase-1 form (one module) | What an object needs in addition |
|---|---|---|
| Method bodies | `.method` + IL text, printed through `MSILTargetStreamer` (`MCTargetDesc/MSILTargetStreamer.h:47-81`) and `MSILInstPrinter` | A binary CIL encoder (no `MCCodeEmitter` exists: `MSILMCAsmInfo.cpp:63` sets `UseIntegratedAssembler = false`, and `TargetRegistry.cpp:93-96` makes object emission a fatal usage error) |
| Signatures | IL text from `MSIL::Signature` | ECMA-335 blobs (MethodDefSig, FieldSig, LocalVarSig, StandAloneMethodSig) able to name value types (Design.md 2.3 by-value P/Invoke structs; FeatureIdeas idea 3) |
| Calls, field and member references | `call`, `ldsfld`, `ldftn` resolved by ilasm inside one module | Object-local references to symbols defined elsewhere, and to BCL members bound by msil-ld (P2-B) |
| Globals | One data segment per module, `<msil>image_t` + RVA blob + one `initonly native int` address field per global (D12) | Per-global ranges (for `--gc-sections` and COMDAT), alignment, BSS, TLS templates (Design.md 4.4) |
| Data relocations | Straight-line `stind` in `<Module>::.cctor` (D12) | Relocation records against symbols of other objects |
| Constructors | Called from `.cctor` in priority order | Priority per constructor, merged across objects |
| C linkage | linkonce/weak/comdat treated as ordinary definitions (D26) | weak, COMDAT/linkonce, hidden visibility, common, aliases, undefined weak |
| Identity | `.assembly` name (D28), triple only in a comment | The full triple (refuse mixing glibc/UCRT or LP64/LLP64 objects, Design.md 5.4) and a format version |

Requirements that follow for every option:

- **Function-level granularity** is the norm for this kind of target: `WebAssemblyTargetMachine`
  forces `FunctionSections`, `DataSections` and `UniqueSectionNames`
  (`llvm/lib/Target/WebAssembly/WebAssemblyTargetMachine.cpp:190-192`); `MSILTargetMachine` sets
  none of them today.
- **The module initializer should be synthesized by msil-ld**, not concatenated from objects:
  `initonly` address fields (D12) may only be stored in the `.cctor` (finding 4 above, verified with
  ILVerify and PEVerify), and keeping them `initonly` lets the optimizing JIT treat them as
  constants once `<Module>` is initialized (*unverified for this IL*). This favours containers
  whose objects carry data plus relocation records (A, D, E) over objects that carry initializer
  code (B, C).

---

## 2. The options at a glance

Measured facts in plain text; estimates are marked "est.". Line counts are C++ without tests.

| | (A) wasm-style custom | (B) netmodule/PE + link stream | (C) pure netmodules | (D) ELF relocatable | (E) bitcode only |
|---|---|---|---|---|---|
| Bytes | `\0cil`-style magic, LEB-sized sections (sketch 3.4) | PE32+ + CLI header + ECMA-335 metadata + `#MSILLink` stream (4.5) | PE32+ netmodule | ELF64 ET_REL, `EM_CIL`, `.cil.text.*`, `.rela.*` (6.6) | LLVM bitcode (`BC C0 DE`) |
| New LLVM format code | est. 6-8k lines (wasm: about 10.4k outside lld) | est. 3.5-5k (ECMA-335 writer in llvm/lib, shared with msil-ld) + link stream 0.6k | as B minus link stream | EM_CIL: 6-9 files, 33-339 lines (precedents); object writer est. 1k | none |
| Shared upstream files touched | about 30 (wasm: 29 Object/tool files, 149 lines) | 0 if LLVM tools stay blind; est. 5-8 to teach them | same as B | 6-9 | 0 |
| llvm-nm / llvm-ar index | after the work | "no symbols" / empty index (measured) | same | yes, with an unknown EM (measured) | yes, LLVM 22 too (measured) |
| llvm-objdump / readobj | after the work | PE headers only (measured) | same | yes; reloc names `Unknown` until a `.def`; `-d` needs a CIL disassembler | `llvm-dis` |
| ildasm / SRM / ILSpy | no | yes, with extra stream or section (measured; ILSpy *inferred*, it reads through SRM) | yes | no | no |
| ILVerify / PEVerify | no | assembly variant yes (baseline messages), netmodule refused (measured) | refused | no | no |
| C link semantics | designed in (wasm `linking` section has them all) | must be designed into the link stream | none without custom attributes | native ELF (groups, weak, hidden, common, TLS, `.init_array.N`, SHF_LINK_ORDER; measured) | IR linkage |
| Fix-up records | relocations (LEB, compact) | none for code (tokens by decoding); data relocs in the stream | same | RELA, 24 B per token (est. +40% object size vs B) | n/a |
| Determinism | yes (writer is ours) | needs a content-hash MVID (ilasm only with `-det`, measured) | same | yes (measured) | yes (measured) |
| `--gc-sections` unit | symbol | metadata row; data needs a layout table | row | section (with associated side sections) | IR GlobalDCE |
| Link time | merge only | merge only: 101 modules in about 1 s (measured) | same | merge only | whole-program codegen: 24.8 s for 52 KLOC (measured) |
| Windows archivers | llvm-ar only | llvm-ar only (`lib.exe` LNK1302, `llvm-lib` rejects; measured) | same | llvm-ar only (`lib.exe` LNK1107, `llvm-lib` rejects; measured) | llvm-ar, llvm-lib (measured) |

---

## 3. Option A: a wasm-style custom container

### 3.1 What the wasm object format holds

`llvm/include/llvm/BinaryFormat/Wasm.h` (582 lines) defines the standard sections
(`WASM_SEC_TYPE` ... `WASM_SEC_TAG`, lines 37-51) and the custom `linking` section that makes an
object linkable: `WASM_SEGMENT_INFO`, `WASM_INIT_FUNCS`, `WASM_COMDAT_INFO`, `WASM_SYMBOL_TABLE`
(lines 204-207), COMDAT kinds (221-226), symbol kinds (231-236), segment flags `STRINGS`, `TLS`,
`RETAIN` (240-242) and symbol flags (250-264): binding global/weak/local/common, visibility hidden,
undefined, exported, explicit name, no-strip, TLS, absolute. Relocations live in `reloc.CODE` /
`reloc.DATA` custom sections with LEB-encoded offsets; `WasmRelocs.def` has 30 types. In other words
wasm re-implemented ELF's link model inside its own executable format, because the wasm binary
format *is* the target's executable format and existing wasm tools already read it.

The CIL analogue of that choice is not a new custom format but the target's own executable format,
ECMA-335 metadata plus a linking stream, which is option B. A new custom format (A as Design.md 5.1
reads) gets neither the .NET tools (they read only PE/ECMA-335) nor the LLVM tools (they read only
formats they were taught).

Reference experiment (`objfmt-container-files/wasm/`): a 5-line C file compiled by this tree's clang
to a wasm32 object (471 bytes) is read by LLVM 22's `llvm-nm` (`U counter`, `U ext`, `D table`,
`W twice`), `llvm-objdump -h` (TYPE, IMPORT, FUNCTION, DATACOUNT, CODE, DATA, `linking`,
`reloc.CODE`, `reloc.DATA`, `producers`, `target_features`) and `llvm-objdump -r`
(`R_WASM_MEMORY_ADDR_LEB counter+0`, `R_WASM_FUNCTION_INDEX_LEB ext+0`,
`R_WASM_MEMORY_ADDR_I32 counter+0`). That is the end state option A must rebuild for CIL.

### 3.2 What a new object format touches (measured in this checkout)

Inventory script `objfmt-container-files/precedent/inventory.py` (git-tracked files outside tests,
unittests, `llvm/lib/Target` and docs); output in `inventory.txt`, `shared-tools.txt`,
`history.txt`.

| Format | Dedicated files | Dedicated lines | Largest pieces |
|---|---|---|---|
| wasm (whole) | 68 | 22,841 | lld/wasm 31 files / 12,435; `WasmObjectFile.cpp` 2,376; `WasmObjectWriter.cpp` 1,990; ObjectYAML 1,410; ObjCopy 448; `wasm2yaml.cpp` 442; `WasmDumper.cpp` 255 |
| wasm without lld | 37 | about 10,400 | as above |
| XCOFF | 35 | 10,895 | MC 2,188; Object 1,667; ObjectYAML 1,305; llvm-readobj 1,256; llvm-objdump 787 |
| DXContainer | 30 | 8,266 | headers 3,472; ObjectYAML 1,768; Object 1,128; MC 941 |
| GOFF | 18 | 3,758 | headers 1,393; MC 1,124; Object 917 (no readobj/objdump dumper yet) |
| SPIR-V | 6 | 912 | MC writer 53 lines; no Object reader: LLVM tools cannot read SPIR-V objects |

Shared Object/tool files that mention the format (outside its own files): wasm 29 files / 149
lines; DXContainer 22 / 71; GOFF 15 / 67; XCOFF 31 / 308. For wasm they are, with the hook sites
checked in this tree:

- `llvm/lib/BinaryFormat/Magic.cpp:59-60` (`"\0asm"` -> `file_magic::wasm_object`) and `Magic.h:54`;
- `llvm/include/llvm/Object/Binary.h:74,139` (`ID_Wasm`, `isWasm()`),
  `ObjectFile.h:46,403-404` (`createWasmObjectFile`);
- `llvm/lib/Object/ObjectFactory.cpp:61,164-165,225`, `SymbolicFile.cpp:46` (this is what gives
  `llvm-ar` an index: `ArchiveWriter.cpp:536` asks `SymbolicFile::isSymbolicFile`),
  `IRObjectFile.cpp:96`, `Object.cpp:125-126` (C API), `SymbolSize.cpp`, `RelocationResolver.cpp`
  (56 lines);
- `llvm/tools/llvm-nm/llvm-nm.cpp:1079` (`getSymbolNMTypeChar(WasmObjectFile &)`), `:1174-1175`,
  `:1870-1871`; `llvm-readobj.cpp:353-354`; `llvm-objdump.cpp` (22 lines);
  `obj2yaml.cpp:58-59`; `ObjectYAML.cpp:67-69` (`!WASM`); `yaml2obj.cpp:48-49`;
- `llvm/lib/ObjCopy/ObjCopy.cpp` and `ConfigManager.cpp`.

`llvm-ar.cpp` itself has no wasm code: it gets an index for any format that `SymbolicFile`
accepts.

Bring-up history (`history.txt`): wasm objdump support `2c6f75ddc51f` (17 files, +546), readobj
`6d76b7b455cd` (10, +165), MC skeleton `18eafb6c688f` (35, +1,307), YAML and format work
`d3d84fdda185` (35, +2,442), first wasm-ld `c94d393ad52b` (61, +5,791); DXContainer reader
`4070aa01561c` (6, +224) and MC `3adc908b2685` (14, +393); GOFF reader `5b2423183cb3` (10, +1,543)
and writer `3adc2a0b84d0` (14, +551). Churn since creation: `WasmObjectWriter.cpp` 291 commits,
`WasmObjectFile.cpp` 236, `Wasm.h` 147, `lld/wasm/Driver.cpp` 329; DXContainer's reader 49 since
2022, GOFF's 28 since 2023.

### 3.3 MC, Object and tool work for an MSIL format A

- **Front-loaded headers (P2-A first milestone):** `llvm/include/llvm/BinaryFormat/MSIL.h` (new),
  `Magic.h` (`file_magic::msil_object`), `Object/Binary.h` (`ID_MSIL`), `Object/ObjectFile.h`,
  `MCSectionMSIL.h` (grows segment flags, COMDAT, alignment, like `MCSectionWasm.h`, 88 lines), a new
  `MCSymbolMSIL.h` (signature, kind, flags, like `MCSymbolWasm.h`, 158 lines; `MCContext`
  `createSymbolImpl` must create it, triple-objfmt.md 2 notes `MCSymbol.h` itself needs no change).
- **MC:** `MSILObjectWriter.cpp` (the analogue of `WasmObjectWriter.cpp`, 1,990 lines; est. 1,500
  for CIL), `MCMSILObjectWriter.h`, an object streamer (`MCWasmStreamer.cpp` is 177), the `Triple::MSIL`
  case of `createMCObjectStreamer` (`llvm/lib/MC/TargetRegistry.cpp:93-96`), and in the target
  `MSILAsmBackend`, `MSILMCCodeEmitter` (the CIL binary encoder) and `MSILObjectTargetWriter`
  (WebAssembly's three are 135 + 283 + 165 lines).
- **Object and tools:** `MSILObjectFile.cpp` (est. 1,500-2,000), the 29-ish hook sites above, an
  `MSILDumper` for llvm-readobj (wasm 255), `MSILDump` for llvm-objdump (wasm 65 + 22 hook lines),
  llvm-nm type characters, and ObjectYAML + obj2yaml + yaml2obj (wasm 1,410 + 442 + hooks), which
  every reader/writer test in upstream style needs.
- **lld/MSIL input side:** like lld/wasm `InputFiles.cpp` (920), `InputChunks.cpp` (617),
  `Relocations.cpp` (199), `MarkLive.cpp` (246), reading through `MSILObjectFile`.

Estimate: 6,000-8,000 lines of new LLVM code before msil-ld, plus about 30 shared hook sites that
every upstream merge carries. The fork's upstream-hooks manifest (36 hook blocks at M11) would
roughly double.

### 3.4 Byte layout sketch (if A were chosen)

```
00 63 69 6C   magic "\0cil";  01 00 00 00   version 1
then sections: id (u8), size (uleb128), payload
  1 SIGNATURE  count, ECMA-335 signature blobs (method, field, locals, calli)
  2 TYPE       explicit-layout value types (by-value P/Invoke structs, FeatureIdeas 3)
  3 IMPORT     undefined functions, variables, BCL members: kind, name, signature index
  4 FUNCTION   defined methods: signature index, flags
  5 CODE       ECMA-335 method bodies (tiny/fat header, IL, EH sections), token operands zero
  6 DATA       segments: name, alignment, flags (BSS, TLS, STRINGS), bytes
  0 "linking"  symbol table (binding, visibility, undefined, TLS, no-strip), segment info,
               init funcs (priority, symbol), COMDAT info  -- wasm's subsections 5-8
  0 "reloc.CODE" / "reloc.DATA"  (type u8, offset uleb, symbol uleb, addend sleb)
  0 "target"   triple, producer
```

### 3.5 Interop, determinism, archives, sections, incremental builds

- Interop: only the LLVM tools that are taught the format; no .NET tool reads it; native linkers
  reject it ("unknown file type").
- Determinism: by construction (the wasm writer writes no timestamp).
- Archives: an `llvm-ar` index after the `SymbolicFile` hook; msil-ld can also scan members, as
  lld/ELF and lld/wasm do (3.6).
- `-ffunction-sections`/`--gc-sections`: per symbol, as wasm-ld's `MarkLive.cpp`.
- Incremental builds: codegen in the compile step; the link only merges.

### 3.6 Archive indexes are optional for an lld-style linker

lld/ELF scans every archive member's own symbol table instead of the archive index
(`lld/ELF/Driver.cpp:2164-2183`, comment "Scan all archive members rather than using the archive
symbol index"), and lld/wasm does the same (`lld/wasm/Driver.cpp:279-309`). An empty `llvm-ar`
index (B, C) therefore costs nothing inside msil-ld; it matters for `llvm-nm --print-armap` and for
other consumers.

---

## 4. Option B: a .NET module envelope plus a link stream or section

### 4.1 Experiment

Sources and logs: `objfmt-container-files/envelope/`. `two.c` (a global, a static helper, `twice`,
`main`) was compiled by this tree's clang (`-O1 -S`, `x86_64-pc-windows-msvc-msil`) to `two.il`.
Variants, all assembled by ilasm 4.8 `-dll -pe64 -x64` (or `-exe` for Framework runs):

- `two.dll` (assembly), `two.netmodule` (the same without `.assembly`), `obj.netmodule` (an
  object-like module: `main` also calls `ext1`, a body-less MethodDef with an ImplMap row naming
  ModuleRef `msil.unresolved`, and `ext2` through `call int32 [.module 'msil.unresolved']::'ext2'`,
  a MemberRef whose parent is that ModuleRef), and `obj.dll` (the same as an assembly);
- `*_stream`: `pepatch.py stream` copies the metadata root into a new section `.msilmd`, appends a
  sixth stream `#MSILLink` (40 bytes: magic `MSILLINK`, version, symbol names) and repoints the CLI
  header's MetaData directory (`two.dll`: root at RVA 0x20F8, 0x290 bytes, streams `#~` 0x120,
  `#Strings` 0xC0, `#US` 0x8, `#GUID` 0x10, `#Blob` 0x2C; patched: RVA 0x6000, 0x2CC bytes,
  `#MSILLink` at root offset 0x2A4);
- `*_stream_z`: the same with the original metadata copy in `.text` overwritten by 0xCC;
- `*_section`: `pepatch.py section` adds a PE section `.msillnk` (`IMAGE_SCN_CNT_INITIALIZED_DATA |
  MEM_READ`) holding the same 40 bytes, metadata untouched.

Results (`checks-results.txt`, `runtime-results.txt`):

| Reader / runtime | assembly | + `#MSILLink` | + `.msillnk` | netmodule (any variant) | object-like (`obj.*`) |
|---|---|---|---|---|---|
| ildasm 4.8 (`/text`) | 5 methods | 5 | 5 | 5 | 6 |
| ildasm 10.0.12 | 5 | 5 | 5 | 5 | 6 |
| System.Reflection.Metadata | reads; extra stream invisible to `MetadataReader`, visible to a manual stream-header walk | same | reads | reads (`isAssembly=False`) | reads (ImplMap=1, ModuleRef=1) |
| ILVerify 10.0.12 | baseline (StackByRef, Unverifiable) | same | same | BadImageFormatException | `obj.dll`: "Assembly or module not found: msil.unresolved" |
| PEVerify 4.8 `/il` | baseline (RVA static, StackByRef) | same | same | "expected to contain an assembly manifest" | `obj.dll`: "Unable to resolve token" |
| .NET 8 / .NET 10 run | exit 43 | 43 (also `_z`) | 43 | `dotnet exec` refuses the extension | n/a |
| .NET Framework 4.8 run (`.exe`) | 43 | 43 (also `_z`) | 43 | n/a | n/a |
| llvm-readobj / objdump -h | COFF-x86-64, sections listed | + `.msilmd` | + `.msillnk` | same | same |
| llvm-nm | "no symbols" | same | same | same | same |
| llvm-ar index | empty | | | empty | empty |
| ld.lld / lld-link | "unknown file type" | | | | |

Conclusions:

- **The CLR and every .NET reader tolerate an unknown metadata stream and an unknown PE section**
  (observed on all three runtimes and in ildasm and SRM; this recon did not find a guarantee for
  extra streams in ECMA-335 II.24.2, so treat it as observed behaviour).
- **Objects cannot be verified as objects.** Verifiers either refuse modules or stop at the
  placeholders. Verification stays a property of the linked output (as D21 already has it).
- **A netmodule is the safer object envelope than an assembly**: it cannot be run by mistake. An
  assembly-shaped object with ImplMap placeholders would load and fail only when a placeholder is
  called (`DllNotFoundException` for `msil.unresolved`, *unverified*).

### 4.2 Netmodules on CoreCLR

`multi/main.il` (`.file 'two.netmodule'`, `call int32 [.module 'two.netmodule']::'twice'(int32)`)
runs on .NET Framework 4.8 (exit 43); on .NET 8 and .NET 10 it fails: "Could not load file or
assembly 'two.netmodule'. The module cannot be loaded because only single file assemblies are
supported. (0x8013101E)". The multi-module linking mechanism of inputs.md 2 (MemberRef with a
ModuleRef parent plus File/ExportedType) is Framework-only, as the orchestrator assumed.

### 4.3 Which placeholder shapes ilasm can express

| Shape for an undefined C symbol | ilasm 4.8 / 10.0.12 | Readers |
|---|---|---|
| Body-less MethodDef + ImplMap to ModuleRef `msil.unresolved` (the Phase-1 P/Invoke shape, produced by `llc` from `"msil-import-module"="msil.unresolved"`) | yes | yes |
| MemberRef with parent ModuleRef `msil.unresolved` (method or field) | yes (`[.module 'msil.unresolved']::'x'`) | yes |
| Call to an undefined global method `'twice'(int32)` | no: "unresolved global member ref 'twice'" | n/a |
| MemberRef on the module's own `<Module>` (the MSVC `/clr` shape, 6.1) | no: same error | yes (SRM reads MSVC's `.cormeta`) |

So an ilasm-produced object (useful as a test oracle) can use the first two shapes only; the MSVC
shape needs our own writer.

### 4.4 Determinism

ilasm 10.0.12 is deterministic only with `-det` (two runs in separate directories: identical with
`-det`, different without); Framework ilasm 4.8 has no such switch. The module name is part of the
image (the output file name), so the object's MVID must be derived from content, as Roslyn's
`/deterministic` does; `netmerge` does this and was byte-identical across runs.

### 4.5 Byte layout of a B object

```
"MZ" DOS stub, e_lfanew -> "PE\0\0"
COFF header: Machine AMD64 (or ARM64 per triple), Characteristics DLL|EXECUTABLE_IMAGE
PE32+ optional header, data directory 14 (CLR runtime header) -> IMAGE_COR20_HEADER
  (cb 72, runtime 2.5, MetaData RVA/size, Flags ILONLY, EntryPoint 0)
.text: CLI header, IL method bodies, RVA data, metadata root:
  42 53 4A 42  01 00 01 00  00 00 00 00  0C 00 00 00  "v4.0.30319\0\0"  00 00  06 00
  stream headers (offset u32, size u32, ASCIIZ name padded to 4):
    "#~" "#Strings" "#US" "#GUID" "#Blob"
    A4 02 00 00 28 00 00 00 23 4D 53 49 4C 4C 69 6E 6B 00 00 00   "#MSILLink" (from the experiment)
#MSILLink payload (proposal): "MSILLINK", version u32, triple,
  symbols  {name (#Strings offset), token (MethodDef | Field | ImplMap stub | MemberRef),
            flags: weak/local/hidden/common/TLS/no-strip, COMDAT index, alignment}
  data     {address-field token, image offset, size, alignment, flags}
  relocs   {image offset, kind (ADDR64/ADDR32/FUNC), target token, addend}
  inits    {priority u32, MethodDef token}, COMDAT table
```

### 4.6 Work and cost

- **Writer in MC.** The backend's object writer must produce ECMA-335 tables, heaps, signatures and
  a PE. msil-ld needs exactly this writer for its output, so it should live in `llvm/lib` (a new
  ECMA-335 library under `llvm/lib/Object` or `llvm/lib/BinaryFormat`), est. 3,500-5,000 lines
  (tables and coded indexes, four heaps, blob encoders, method body encoder, PE/CLI headers; SRM's
  `MetadataBuilder` + encoders + `ManagedPEBuilder` are the reference, *size unverified*).
- **Link stream.** Same information as wasm's `linking` + `reloc.*` sections, est. 600 lines for the
  writer and reader.
- **Reader for LLVM tools (optional).** Without it `llvm-nm` and the `llvm-ar` index see nothing. With
  it: CLI detection in `ObjectFactory.cpp` (PE magic is `file_magic::pecoff_executable`, read by
  `COFFObjectFile` today), a `Binary` ID, `SymbolicFile`, `llvm-nm` type characters: est. 5-8 shared
  files plus the ECMA-335 reader, which msil-ld also needs for reference assemblies (P2-B).
- **Linker input side: a metadata merger** instead of relocation processing (section 5).
- **Test oracle bonus.** The object's payload is what `-S` prints today, so ilasm/ildasm keep working
  on objects (Design.md 5.5). They drop the link stream, so the oracle covers payload only.

---

## 5. Option C: pure netmodules and an ILMerge-like merger

### 5.1 The `netmerge` prototype

`objfmt-container-files/tools/netmerge/Program.cs` (440 lines, 393 without comments and blank
lines; `netmerge.csproj` targets net8.0 and needs no packages). It merges netmodules whose code is
global methods and fields of `<Module>`:

1. symbol table: public MethodDefs without ImplMap and public fields, by name (a duplicate is an
   error);
2. plan rows: stubs (ImplMap to `msil.unresolved`) alias their definition; duplicate real P/Invokes
   (same library, entry and name) are shared; non-public names that collide get `$<input>`; each
   input's `.cctor` becomes `'<msil>cctor$<input>'`; every `<Module>` maps to row 1, other TypeDefs
   (Phase 1 has only memberless layout types) follow;
3. emit Module, Assembly, TypeDefs (+ ClassLayout), Fields (+ FieldRVA data copied, size from the
   field type's ClassLayout), MethodDefs (+ Params, ImplMaps) with rewritten bodies, and a new
   `.cctor` that calls the inputs' initializers in input order;
4. rewrite every token operand (decode IL, `MapToken`), every TypeDefOrRefOrSpec inside every
   signature blob (`RewriteSig`: field, locals and method signatures including `FNPTR`, arrays,
   `GENERICINST`, custom modifiers), deduplicate AssemblyRefs, TypeRefs and MemberRefs, resolve
   MemberRefs on `msil.unresolved` (or on `<Module>`, the MSVC shape) to definitions with a
   signature check;
5. write the PE with `ManagedPEBuilder` (ILONLY, AMD64, MVID from a SHA-256 of the inputs).

Results (`merge/merge-results.txt`, `lto/lto-results.txt`):

| Test | Result |
|---|---|
| `m1.netmodule` (main; `twice` as an ImplMap stub; `counter` as a field MemberRef on `msil.unresolved`; static `helper`) + `m2.netmodule` (`twice`, `counter`, `table`, its own static `helper`) | 7 methods, 7 fields, 21 tokens rewritten, 2 signature TypeRefs rewritten, 2 placeholders resolved, 3 renames; exit 66 on .NET 8, .NET 10 and Framework 4.8 (expected 66) |
| Determinism | two merges in separate directories are byte-identical |
| Missing definition | "netmerge: error: undefined symbol 'twice' referenced by m1.netmodule" |
| 101 per-TU netmodules of the synthetic program (`main` through `llc` with `"msil-import-module"="msil.unresolved"` on the 100 `tuN_entry` declarations; per-TU `.il` with the `.assembly` line removed; ilasm 4.8, 12.2 s for all) | 4,302 methods, 400 fields, 24,977 tokens rewritten, 100 placeholders resolved, 297 renames; 0.94-1.29 s per run including .NET start-up; exit 140 on .NET 8, .NET 10 and Framework 4.8, the same as the LTO build |
| ILVerify / PEVerify of the merged output | the Phase-1 messages plus new **[InitOnly] "Cannot change initonly field outside its .ctor"** in every `'<msil>cctor$N'` |

### 5.2 What the prototype teaches

- **Per-object module initializers do not merge cleanly.** Calling them from one `.cctor` breaks the
  `initonly` rule; the fix is to splice their straight-line bodies into one `.cctor` (with ordering:
  every allocation and address-field store before any cross-object relocation, then constructors in
  global priority order), or to let the linker generate the initializer from data layout and
  relocation tables. The second is what Phase 1's `MSILAsmPrinter::emitModuleInitializer`
  (`MSILAsmPrinter.cpp:848`) already does per module; moving it into msil-ld is natural for A, D
  and E.
- **The merge itself is mechanical** once a metadata reader and writer exist: row remapping per
  table, IL token rewriting by decoding, signature blob rewriting, deduplication of references.
  System.Reflection.Metadata did the heavy lifting here; in C++ the merger is est. 1,500-2,500
  lines on top of an ECMA-335 reader and writer.
- **Pure netmodules carry no C link semantics.** Weak, COMDAT/linkonce, hidden vs exported,
  common, aliases, constructor priorities, TLS templates, data alignment and data relocations
  against other objects have no ECMA-335 row. They can be encoded as custom attributes on the
  MethodDef/Field rows (with a fake AssemblyRef such as `msil.link`), which ilasm can express, but
  that is a link stream inside the tables, i.e. option B with a different encoding.
- **Naming conventions instead of a symbol table** (static functions renamed per object, globals by
  name) work for the Phase-1 subset, as the 101-module run shows.

### 5.3 No relocation records for code

`mdinfo il` (`tools/mdinfo/Program.cs`) decodes every IL body of the 20 programs of
`msil/test/M1/11-programs` (-O2) and of the synthetic program (`lto/token-density.txt`):

| | methods | IL bytes | instructions | token operands | per KB of IL |
|---|---|---|---|---|---|
| 20 corpus programs | 238 | 99,683 | 46,643 | 3,491 (Field 2,755, MethodDef 710, MemberRef 20) | 35.9 (range 18.5-68.4) |
| synthetic 100-TU program | 4,103 | 1,310,723 | 659,426 | 24,682 (Field 16,680, MethodDef 8,001, MemberRef 1) | 19.3 |

Every one is a 4-byte site that an A/D linker patches through a relocation record and a B/C linker
finds by decoding. Field tokens dominate because every global access is `ldsfld` of an address field
(D12).

---

## 6. Option D: ELF or COFF relocatable objects holding CIL

### 6.1 The MSVC `/clr` precedent, dissected

`objfmt-container-files/clr/` (`a.cpp`, `b.cpp`, `clr-results.txt`). `cl /nologo /clr /c /O2 a.cpp
b.cpp` (MSVC 14.51; `/clr:netcore` failed here for lack of a .NET reference setup and was not
pursued) gives COFF objects, machine `IMAGE_FILE_MACHINE_AMD64`:

- **One `.text$mn` section per method, each a complete CIL method body**, e.g. `03 30 02 00 04 00 00
  00 00 00 00 00 02 17 58 2A` (fat header, maxstack 2, code size 4, no locals; `ldarg.0; ldc.i4.1;
  add; ret` = `helper`). Token operands are zero and carry an **`IMAGE_REL_AMD64_TOKEN`** relocation.
- **Token symbols:** storage class **`IMAGE_SYM_CLASS_CLR_TOKEN` (0x6B)**, named after the
  object-local token: `06000007` (a MethodDef, defined in its `.text$mn` section, with an aux record
  of type `IMAGE_AUX_SYMBOL_TYPE_TOKEN_DEF` tying it to the COFF symbol `?add1@@$$J0YAHH@Z`) and
  `0A00000E` (a MemberRef, undefined). Ordinary COFF symbols (`add1`, `main`, undefined `counter`,
  `?twice@@$$J0YAHH@Z`) drive symbol resolution and archive extraction.
- **`.cormeta` (0x888 bytes) is a complete ECMA-335 metadata root** (`BSJB`, `v4.0.30319`, streams
  `#-` (the uncompressed, ENC-style table stream), `#Strings`, `#US`, `#GUID`, `#Blob`; module name
  `a.obj`). It has a MethodPtr table, MethodDefs with RVA 0, and **references to the other object's
  `twice` and `counter` as MemberRefs whose parent is the object's own `<Module>` TypeDef**
  (`0A00000E twice parent=TypeDef <Module>`, `0A00000A counter parent=TypeDef <Module>`).
- Mixed-mode extras that an IL-only design would not need: `.nep` native entry points, `__mep@` /
  `__m2mep@` / `__unep@` fields with `.rdata$ilfixup` relocations, `.drectve`, `.chks64`.
- `link.exe a.obj b.obj` merges the metadata (`twice` becomes MethodDef 0x06000002 of `ab.exe`) and
  the program runs: exit 154 = `add1(twice(20))`.
- `lld-link a.obj b.obj`: "a.obj: 0A00000D should not refer to special section 0"
  (`lld/COFF/InputFiles.cpp:898`). `llvm-lib` builds an archive with a full index of the COFF
  symbols.
- LLVM already models these pieces: `IMAGE_SYM_CLASS_CLR_TOKEN = 107`
  (`llvm/include/llvm/BinaryFormat/COFF.h:251`), `IMAGE_AUX_SYMBOL_TYPE_TOKEN_DEF` (`:283`),
  `IMAGE_REL_I386_TOKEN`, `IMAGE_REL_AMD64_TOKEN`, `IMAGE_REL_ARM_TOKEN`, `IMAGE_REL_ARM64_TOKEN`
  (`:355`, `:374`, `:386`, `:413`), `COFFSymbolRef::isCLRToken` (`llvm/include/llvm/Object/COFF.h:439`),
  and `llvm-readobj` prints `CLRToken` symbols and `AuxCLRToken` records
  (`llvm/tools/llvm-readobj/COFFDumper.cpp:535`, `:1818-1823`). `IMAGE_FILE_MACHINE_CEE` (0xC0EE) is
  in the Windows SDK's `winnt.h` (10.0.26100.0, line 19835) but not in LLVM's `COFF.h`
  (`COFF.h:96-122`).

What the precedent shows: a production C/C++-to-CIL toolchain chose a native relocatable container
for symbol resolution, COMDAT and archives, and embedded real ECMA-335 metadata for the payload;
its linker merges metadata and patches tokens. That is D with B's payload. It also shows the cost of
two symbol spaces (COFF symbols and metadata tokens) kept consistent through CLR-token symbols.

### 6.2 Experiment: CIL in ELF and COFF objects

`objfmt-container-files/elfcoff/` (`cil-elf.s`, `cil-coff.s`, `linkorder.s`, `elfcoff-results.txt`,
`lib-results.txt`). Hand-written assembly assembled by this tree's clang with
`--target=x86_64-unknown-linux-gnu` / `x86_64-pc-windows-msvc` (x86_64 only as a carrier):
`.cil.text.<f>` sections holding tiny-format CIL bodies; `call`/`ldsfld` tokens as 4-byte
relocations against `helper`, `counter` and the undefined `ext`; `inl` in a COMDAT group (weak);
`hid` hidden; `counter`/`table` in `.data` with 64-bit data relocations (`&counter`, `&twice`);
`tlsvar` in `.tdata`; `common_var` as `.comm`; an undefined weak reference; a constructor in
`.init_array.101`; a `.msil.sig` side section. Then `e_machine` was patched to 0xC11 (unassigned)
and to 0 (EM_NONE), and the COFF machine to 0xC0EE (CEE) and 0 (UNKNOWN).

| Tool | ELF x86_64 | ELF 0xC11 | ELF EM_NONE | COFF AMD64 | COFF CEE 0xC0EE | COFF 0 |
|---|---|---|---|---|---|---|
| llvm-readobj -h | elf64-x86-64 | elf64-unknown, Machine 0xC11 | EM_NONE | COFF-x86-64 | "not recognized as a valid object file" | COFF-<unknown arch> |
| llvm-nm | 10 symbols: C, D, U, t, T, W, w | same | same | same (+ `.weak.*.default.*`) | rejected | same as AMD64 |
| llvm-objdump -r | R_X86_64_32/64 | same offsets, type `Unknown` | `Unknown` | IMAGE_REL_AMD64_* | rejected | `Unknown` |
| section groups, notes | yes | yes | yes | COMDAT | rejected | COMDAT |
| llvm-ar index | 7 symbols | 7 | 7 | yes | empty | yes |
| llvm-objcopy, llvm-size | ok | ok | ok | | | |
| llvm-objdump -d | x86 disassembly (meaningless) | "can't find target" | same | | | |
| ld.lld -r | ok | "unsupported e_machine value: 3089" | "...: 0" | | | |
| lld-link | | | | accepted (fails only on the undefined `ext`) | "unknown file type" | "x64 is assumed", accepted (fails only on `ext`) |
| link.exe / lib.exe | | | | accepted (LNK2001 `ext`, LNK2017 on the 32-bit token reloc) / ok | LNK1136 / LNK1136 | "defaulting to X64", accepted (LNK2001 `ext`) / ok |
| llvm-lib | rejects ELF | rejects | rejects | ok | rejects | "unknown machine: 0" |

`linkorder.s` adds a per-method side section `.msil.method.<f>` with `SHF_LINK_ORDER` pointing at
`.cil.text.<f>` (so `--gc-sections` drops it with the body), places both in the COMDAT group of a
linkonce function, and adds a `.note.msil` (owner `MSIL`) carrying the triple. Two assemblies of
each source are byte-identical; COFF `TimeDateStamp` is 0.

Conclusions: an ELF container with its own machine number gets every generic LLVM tool for free
and is refused by native linkers. COFF needs `Magic.cpp` to recognise the machine before any LLVM
tool reads it (`Magic.cpp:245` only accepts 0x64 0x86 / 0x64 0xAA in that branch; machine 0 is
accepted at `:56-57`), and machine 0 is unsafe (`link.exe`/`lld-link` would link CIL as x64).

### 6.3 What a CIL machine number requires

Precedent commits (`history.txt`): BPF `cfb51f54ba80` (8 files, +33: `ELF.h`, `ELFRelocs/BPF.def`,
`ELFObjectFile.h`, `Object/ELF.cpp`, `ELFYAML.cpp`, `llvm-objdump.cpp`, `ELFDumper.cpp`, the target's
writer; BPF used **EM_NONE** until the official value 247 existed), CSKY `69f2c79f2ad2` (6 files,
+113), LoongArch `e53e6ec6ef74` (9 files, +339, most of it tests). For CIL in this tree the switch
sites are `llvm/include/llvm/BinaryFormat/ELF.h` (EM constant and relocation enum include),
`ELFRelocs/MSIL.def` (new), `llvm/lib/Object/ELF.cpp:137` and `:245` (relocation names, relative
relocation type), `llvm/include/llvm/Object/ELFObjectFile.h:1320`, `:1347` (format name) and
`:1417`/`:1438` (`getArch`), `llvm/tools/llvm-readobj/ELFDumper.cpp:1360` (machine names),
`llvm/lib/ObjectYAML/ELFYAML.cpp:355` (YAML enum). `llvm-objdump -d` additionally needs
`ELFObjectFileBase::makeTriple` to produce a triple whose object format is MSIL, because
`TargetRegistry::lookupTarget` picks the MSIL target by object format
(`llvm/lib/MC/TargetRegistry.cpp:155-159`), and an MSIL `MCDisassembler`.

The COFF route with `IMAGE_FILE_MACHINE_CEE` needs the enum value in `COFF.h`, a `Magic.cpp` case,
`COFFObjectFile` `getArch`/`getFileFormatName`/relocation names, `COFFYAML`, `COFFDumper` names and
`llvm-lib`'s machine check: est. 6-8 files.

### 6.4 C link semantics that ELF provides (present in `cil-elf.o` and `linkorder.o`, except aliases, not tried)

| C / IR feature | ELF mechanism | msil-ld action |
|---|---|---|
| external / static | STB_GLOBAL / STB_LOCAL | public method or `assembly` method |
| `__attribute__((weak))`, undefined weak | STB_WEAK, undefined STB_WEAK | weak resolution; null address field |
| linkonce_odr / inline / `selectany` | SHT_GROUP COMDAT (`.group`, signature `inl`) | keep one group |
| `-fvisibility=hidden` | STV_HIDDEN | not exported from a `-shared` assembly (Design.md 5.4) |
| tentative definitions (`-fcommon`) | SHN_COMMON with alignment | allocate in the data segment |
| `thread_local` | `.tdata`/`.tbss`, STT_TLS | TLS template block (Design.md 4.4) |
| constructors and priorities | `.init_array.<prio>` | calls in `.cctor` in priority order |
| per-function metadata | SHF_LINK_ORDER side section | signature, locals, EH tables kept with the body |
| alignment, BSS, read-only data | `sh_addralign`, SHT_NOBITS, section flags | data layout |
| aliases | two symbols on one section offset | one method, two names |
| `-ffunction-sections`/`-fdata-sections`, `--gc-sections` | one section per symbol | mark-live over relocations |

### 6.5 MC integration: two routes

The MSIL MC environment today is `MCContext::IsMSIL` (`llvm/lib/MC/MCContext.cpp:116-117`, D5) with
the four logical `MCSectionMSIL` sections of `MCObjectFileInfo::initMSILMCObjectFileInfo`
(`llvm/lib/MC/MCObjectFileInfo.cpp:1180-1186`), and `MSILTargetObjectFile` derives directly from
`TargetLoweringObjectFile` (`llvm/lib/Target/MSIL/MSILTargetObjectFile.h:27`).

- **D-i, reuse MC's ELF writer.** Make the MC environment ELF for MSIL triples
  (`Env = IsELF`, `initELFMCObjectFileInfo`), derive `MSILTargetObjectFile` from
  `TargetLoweringObjectFileELF`, create an ELF streamer in `createMCObjectStreamer`
  (`TargetRegistry.cpp:93-96`), and add `MSILELFObjectWriter` (an `MCELFObjectTargetWriter` with
  `EM_CIL`), `MSILAsmBackend` and `MSILMCCodeEmitter` (BPF's three are 85 + 123 + 186 lines;
  AMDGPU's ELF writer is 101). Smallest new code, but it revisits D5 and every generic
  AsmPrinter/TLOF path that keys on the MC environment (triple-objfmt.md section 3 would need a new
  pass), and `MCSymbolELF` has no slot for signatures (side sections carry them anyway).
- **D-ii, keep D5 and write ELF bytes in our own writer.** An `MSILObjectWriter : MCObjectWriter`
  serialises `MCSectionMSIL` fragments, fixups and symbols as ELF64 ET_REL (string tables, symbol
  table with locals first, section headers, RELA, groups, notes) using `BinaryFormat/ELF.h`; est.
  1,000 lines (`ELFObjectWriter.cpp` is 1,421 for the general case). `MCSectionMSIL.h` grows flags,
  group and link-order fields (a P2-A header change). This is wasm's structure (a format-specific
  writer) with a standard output format, so all tools still work.

Recommended: D-ii (no MC environment change, writer fully under our control, and the same writer
structure as option A, so the A-vs-D decision only changes the serialisation).

### 6.6 Byte layout of a D (ELF) object

```
ELF64, ELFDATA2LSB, ET_REL, e_machine = EM_CIL (fork-reserved), e_flags = format version
.cil.text.<sym>      PROGBITS, ALLOC|EXECINSTR (+GROUP), align 4: one ECMA-335 method body
                     (tiny/fat header, IL, EH data sections); token operands 0
.rela.cil.text.<sym> R_CIL_TOKEN32 (callee, address field, BCL member, calli signature, string),
                     R_CIL_LOCALSIG32 (fat header LocalVarSigTok)
.msil.method.<sym>   LINK_ORDER -> .cil.text.<sym>: MethodDefSig blob, flags, parameter info
.msil.sig            shared signature and type blobs (calli, BCL member references, value types)
.data.<g> .rodata.<g> .bss.<g> .tdata.<g> .tbss.<g>
.rela.data.<g>       R_CIL_ADDR64 / R_CIL_ADDR32 (address of a global or function + addend)
.init_array.<prio>   R_CIL_ADDR64 to the constructor
.group               COMDAT groups
.note.msil           owner "MSIL": NT 1 = triple, NT 2 = format version and producer
.symtab / .strtab    STT_FUNC/OBJECT/TLS, STB_LOCAL/GLOBAL/WEAK, STV_HIDDEN, SHN_COMMON,
                     SHN_UNDEF for externals and BCL members (naming: objfmt-binding)
```

### 6.7 lld/ELF and lld/COFF as msil-ld?

Not viable as the linker: their output side is ELF or PE-native (lld/ELF 35,888 lines, lld/COFF
21,557), and both reject the inputs (6.2). AMDGPU shows the cheapest lld/ELF reuse (an
`Arch/AMDGPU.cpp` of 220 lines) only because AMDGPU's output is ELF. msil-ld stays an lld flavor
modelled on lld/wasm (12,065 lines), reading ELF inputs through `llvm::object::ELFObjectFile`,
which costs nothing.

### 6.8 ELF or COFF?

ELF: one neutral container for every MSIL triple (as wasm uses one container on every OS), richer
semantics (visibility, typed symbols, TLS, priorities, SHF_LINK_ORDER), no Magic.cpp change, and
`llvm-ar` indexes it today. COFF: the CLR precedent and existing TOKEN/CLR-token vocabulary, but no
hidden visibility, weak definitions emulated through weak externals (`.weak.<sym>.default.<sym>`
in the `nm` output above), Windows-specific conventions (`.CRT$XC*`, `.tls$`) on Linux triples,
and a Magic.cpp change before any tool reads a CEE object. ELF is the better fit; borrow the CLR
precedent's ideas (token relocations, embedded ECMA-335 encodings), not its container.

---

## 7. Option E: bitcode-only objects

`objfmt-container-files/lto/` (`a.c`, `b.c`, `gen.py`, `time.sh`, `lto-results.txt`).

- **Works today.** `clang -c` for an MSIL triple stops with `err_drv_msil_no_object_output`
  ("use -S ... or -emit-llvm or -flto", `clang/include/clang/Basic/DiagnosticDriverKinds.td:162-164`,
  raised at `clang/lib/Driver/ToolChain.cpp:741,763` and `ToolChains/Clang.cpp:5543,9342`), but
  `clang -c -flto` writes bitcode, deterministically.
- **Tools.** LLVM 22's `llvm-nm` reads this tree's bitcode (`U counter`, `T main`, `W twice`);
  `llvm-ar rcs` builds a correct index; `llvm-lib` accepts it, `lib.exe` does not (LNK1107).
- **Linking.** A cc1 link (`-x ir a.o -Xclang -mlink-bitcode-file -Xclang b.o -S`) then ilasm gives
  a program that runs (exit 41). msil-ld would do the same through `lto::LTO`.
- **Link time, measured** on the synthetic program (100 TUs x 40 functions + `main`, 52,501 lines,
  4,103 methods; serial, 4C/8T host):

| Step | Time |
|---|---|
| 101 x `clang -O2 -c -flto` (frontend + IR optimisation) | 56.3 s |
| 100 x `clang -O2 -S` (frontend + IR optimisation + MSIL codegen) | 75.0 s (codegen about 0.19 s per TU, about 25% of a compile) |
| LTO link: merge only | 2.7 s |
| LTO link: merge + O2 codegen once (no IR re-optimisation) | 24.8 s |
| LTO link: merge + full O2 IR pipeline + codegen | 54.1 s |
| Object-style link of the same program (101 netmodules, `netmerge`) | about 1 s |

  Every edit of one file costs a full program codegen in E (25-54 s here, single-threaded), against
  about 0.75 s to recompile one TU plus a link that only merges. Codegen parallelism inside LTO
  (`--lto-partitions`, ThinLTO backends) produces one code unit per partition that must then be
  merged, which brings back an object representation, on disk or in memory.
- **Library compatibility.** "The current LLVM version supports loading any bitcode since version
  3.0" and "Debug metadata is special in that it is currently dropped during upgrades"
  (`llvm/docs/DeveloperPolicy.md:706`, `:713`): bitcode static libraries bind consumers to the same
  or a newer LLVM and lose debug info across versions. (LLVM 22's `llvm-nm` happened to read LLVM
  24git bitcode here; older readers are not promised.)
- **Precedent.** "Offload toolchains use full LTO by default" (`AMDGPUToolChain::getDefaultLTOMode`,
  `clang/lib/Driver/ToolChains/AMDGPU.cpp:1349-1352`); HIP links device bitcode through lld with
  `-plugin-opt` options (`HIPAMD.cpp:57-69`). AMDGPU still has a relocatable ELF format for the
  non-LTO path.
- **Size.** Bitcode is about 40 KB per TU of the synthetic program, 2.3 times the ilasm module
  (17 KB) and a quarter of the ILAsm text (164 KB).

---

## 8. Sizes per translation unit (synthetic program, 20 TUs measured)

| Representation | Bytes per TU | Note |
|---|---|---|
| ILAsm text (`-S`) | 163,881 | today's output |
| LLVM bitcode (`-c -flto`) | 40,186 | E |
| ilasm-built PE module | 17,408 | B/C, without a link stream (no relocation records needed) |
| ELF estimate | about 1.46 x PE | IL 13,741 B + 249 tokens x 24 B RELA + symbols, strings, one section header per method |
| COFF estimate | about 1.12 x PE | 10-byte relocations, 18-byte symbols |

---

## 9. Archives, sections, incremental builds, determinism

| | A | B / C | D (ELF) | E |
|---|---|---|---|---|
| `llvm-ar` index | after `SymbolicFile` support | empty (measured) | yes (measured) | yes (measured) |
| msil-ld archive scan | members' symbol tables (lld style) | members' metadata + link stream | members' `.symtab` | IR symbol tables |
| Windows `lib.exe` / `llvm-lib` | no / no | LNK1302 / no | LNK1107 / no | LNK1107 / yes |
| GC unit | symbol | MethodDef/Field row; data needs the layout table | section + associated side sections | IR global |
| COMDAT | `linking` COMDAT info | link stream | SHT_GROUP | IR comdat |
| Incremental rebuild of one TU | recompile + merge-only link | same | same | recompile + full LTO codegen |
| Determinism | by construction | needs content-hash MVID | by construction | by construction |

---

## 10. FeatureIdeas interactions (evaluated only for preclusion)

- **Idea 7 (several triples in one assembly):** every option can record the triple (A target
  section, B link stream or attribute, D `.note.msil`, E the module triple). None precludes it. The
  linker-synthesized initializer of A/D/E maps directly onto "its own initializer per variant".
- **Idea 3 (typed pointers) and Design.md 2.3 (structs by value in P/Invoke):** the payload needs
  value types in signatures. B has TypeDefs natively; A and D must carry ECMA-335 signature blobs
  plus a type table. A wasm-style type section limited to primitive value types would preclude
  them, so whichever container is chosen must use ECMA-335 signature encoding.
- **Idea 6 (enclosed instance model):** globals become per-instance offsets. Containers whose
  objects carry data and relocation tables (A, D, E) let msil-ld generate per-instance
  initialisation; B/C objects that carry `.cctor` code would need that code rewritten. B makes idea
  6 harder, not impossible.
- **Ideas 4 and 5 (managed memory, escape analysis):** need whole-program IR, i.e. the LTO path,
  which E provides and which stays available next to any CIL container.
- **Idea 1 (class placement):** A/D let msil-ld place methods; B objects already contain classes, so
  the merger must move members between classes. Neither precludes it.
- **C# netmodules in the same assembly (inputs.md 2):** needs a general ECMA-335 merger (classes
  with members, generics, properties, events, custom attributes, nested types) in every option.
  `lib.exe`'s message ("only support linking safe .netmodules") suggests Microsoft's tools support
  this for verifiable modules (*inferred from the message, not tested*). B shares that merger's
  core with its own object input; A and D would add it as a separate input kind.

---

## 11. Recommendation

1. **Bring-up: E now.** Let `clang -c` for MSIL triples write bitcode (drop
   `err_drv_msil_no_object_output` for `-c`, or make `-c` imply `-flto` until CIL objects land),
   and start msil-ld as an lld/wasm-shaped flavor that links bitcode, extracts archive members and
   writes the PE. P2-B's hard parts (ECMA-335/PE writer, reference-assembly binding, P/Invoke
   resolution, `.cctor` synthesis moved out of `MSILAsmPrinter::emitEndOfAsmFile`) then start
   without waiting for an object format. Keep this path permanently for `-flto`.
2. **CIL objects: D, ELF ET_REL, route D-ii.** One container for all MSIL triples; a fork-reserved
   `e_machine` (BPF's EM_NONE period is the precedent for living without an official value);
   `.note.msil` with triple and version; `.cil.text.<sym>` per method and per-global data
   sections (always, as WebAssembly forces function and data sections); ECMA-335 method bodies and
   signature blobs as payload; ELF symbols, groups, visibility, TLS and `.init_array.N` as link
   information; token and data relocations. LLVM-side cost about 2,000-3,000 lines (machine number
   plumbing, `MSILAsmBackend`, `MSILMCCodeEmitter`, `MSILObjectWriter`, side-section emission,
   optional `MSILDisassembler`), against 6,000-8,000 for A; tools work from day one.
3. **Keep B as the documented alternative**, chosen only if ildasm-readable objects and a shared
   C# netmodule merge path outweigh the LLVM tools; if chosen, use netmodules (not assemblies),
   content-hash MVIDs, a `#MSILLink` stream, and linker-synthesized initialisers rather than
   per-object `.cctor` code.
4. **Do not build A**; **treat C as a prototype of B** (the `netmerge` result).
5. **Archives** are created with `llvm-ar` (GNU format) for every MSIL triple, Windows included;
   msil-ld scans members as lld/ELF and lld/wasm do.

### 11.1 Proposed changes (for the orchestrator; Design.md is the user's document)

- Design.md 5.1: "CIL object files (`clang -c` default): ELF relocatable objects (`EM_CIL`) whose
  sections hold ECMA-335 method bodies, signatures and data, with ELF symbols, groups and
  relocations as link information" instead of "an MC-emitted container, modeled on the wasm object
  format". Section 2.1's table row "Wasm object format ... template for a CIL object format" would
  then read "template for the object writer and msil-ld".
- ImplementationPlan.md section 7 P2-A: the header batch is `BinaryFormat/ELF.h` (+
  `ELFRelocs/MSIL.def`), `MCSectionMSIL.h` (flags, group, link order), and the `Object/ELF.cpp`,
  `ELFObjectFile.h`, `ELFYAML.cpp`, `ELFDumper.cpp` switch cases; no new `file_magic`, `Binary` ID or
  ObjectYAML format.
- Status.md section 3 row "Object files ...": `-c -flto` already works; say so.

### 11.2 Work-package ideas (disjoint files)

1. **P2-A0 machine number** (header batch): `llvm/include/llvm/BinaryFormat/ELF.h`,
   `llvm/include/llvm/BinaryFormat/ELFRelocs/MSIL.def` (new), `llvm/lib/Object/ELF.cpp`,
   `llvm/include/llvm/Object/ELFObjectFile.h`, `llvm/lib/ObjectYAML/ELFYAML.cpp`,
   `llvm/tools/llvm-readobj/ELFDumper.cpp`, tests under `llvm/test/tools/llvm-readobj/ELF/`.
2. **P2-A1 CIL encoder**: `llvm/lib/Target/MSIL/MCTargetDesc/MSILMCCodeEmitter.cpp`,
   `MSILFixupKinds.h`, `MSILAsmBackend.cpp` (all new), registration in `MSILMCTargetDesc.cpp`.
3. **P2-A2 object writer**: `MSILObjectWriter.cpp` (new, ELF serialiser),
   `llvm/include/llvm/MC/MCSectionMSIL.h`, `llvm/lib/MC/TargetRegistry.cpp` (object streamer case).
4. **P2-A3 object-mode target streamer and sections**: `MSILTargetStreamer.{h,cpp}` (binary
   implementation of `emitMethodBegin`, `emitLocals`, `emitPInvoke`, `emitDataImage`, ...),
   `MSILTargetObjectFile.{h,cpp}` (unique sections), `MSILTargetMachine.cpp` (function/data
   sections on).
5. **P2-A4 disassembler** (optional): `llvm/lib/Target/MSIL/Disassembler/` and the
   `ELFObjectFileBase::makeTriple` hook.
6. **P2-B0 msil-ld on bitcode**: `lld/MSIL/` driver, LTO, archives, PE/ECMA-335 writer (or
   `llvm/lib/Object/ECMA335*` if shared), initializer synthesis.
7. **P2-B1 object input**: `lld/MSIL/InputFiles.cpp`, `Relocations.cpp`, `MarkLive.cpp`
   (ELF via `llvm::object`).
8. **Driver**: `clang/lib/Driver/ToolChain.cpp`, `ToolChains/Clang.cpp`, the diagnostic in
   `DiagnosticDriverKinds.td`, a new `ToolChains/MSIL.cpp` invoking msil-ld and `llvm-ar`.


### 11.3 Decisions that need the user

1. **Object container:** D (ELF relocatable, recommended), B (netmodule plus `#MSILLink`), or A
   (a custom wasm-style format, Design.md 5.1 as written). D gets every LLVM tool and C link
   semantics for the least code; B keeps objects readable by ildasm and shares a merge path with C#
   netmodules; A has neither advantage.
2. **Machine number (if D):** a fork-reserved `e_machine` value with `.note.msil` as the real
   identification (recommended), or EM_NONE plus the note (BPF's choice before 2016; any EM_NONE
   file then looks like a candidate).
3. **Interim `clang -c`:** write bitcode (option E) until CIL objects land (recommended), or keep
   the current error until then.
4. **C# netmodule inputs to msil-ld:** a Phase-2 goal, or an idea for the end (recommended: the
   end, with the FeatureIdeas). Only B makes it cheaper, and only partly.

---

## 12. Risks and open questions

- **Machine number.** A fork-reserved `e_machine` can collide with a future official assignment;
  an official one needs the gABI registry (BPF went from EM_NONE to 247 in 2016). Mitigation: the
  `.note.msil` is the real identification; the number only keeps native linkers away.
- **ELF files on Windows** surprise users and Windows tools (`lib.exe`, `llvm-lib` reject them,
  measured); the driver must use `llvm-ar`. The same is true of every candidate except E with
  `llvm-lib`.
- **The ECMA-335 writer and reader are the dominant Phase-2 cost in every option**; the container
  decision moves a few thousand lines, not tens of thousands.
- **Unknown-stream tolerance (B)** is observed behaviour of .NET 8/10/Framework 4.8, ildasm and
  SRM; no ECMA-335 guarantee was found.
- **D-ii vs D-i** (own ELF writer vs MC's ELF environment) is a P2-A planning choice; D-i would
  reopen D5 and needs a fresh audit of object-format-keyed code (triple-objfmt.md 3).
- **Binding inside the container** (tokens as relocations against symbols vs placeholder rows;
  how BCL members and signatures are named in an object) belongs to `objfmt-binding`; nothing in
  D or B constrains it beyond what 6.6 and 4.5 sketch.
- **Relocation volume.** About 36 token relocations per KB of IL; ELF RELA makes objects about 40%
  larger than a module image (estimate). Acceptable, but REL (16 bytes) or a compact custom
  relocation section would halve it.
- *Unverified:* ILSpy reading B objects (it reads through SRM, which does); CoreCLR's behaviour
  when an assembly-shaped object's placeholder is called; the C++ size of an ECMA-335
  reader/writer.

---

## 13. Experiments and reproduction

All under `C:\llvm-msil-scratch\p2\objfmt-container\`; sources, scripts and trimmed logs copied to
[`objfmt-container-files/`](objfmt-container-files/). Run Windows tools from Git Bash with
`MSYS_NO_PATHCONV=1`.

| Dir | What | Files |
|---|---|---|
| `precedent/` | format inventory and git history | `inventory.py`, `inventory.txt`, `shared-tools.txt`, `history.txt` |
| `wasm/` | wasm32 object read by LLVM 22 tools | `two.c`, `wasm-results.txt` |
| `envelope/` | B: ilasm variants, `pepatch.py stream|section|info`, readers, runtimes, multi-module assembly, ilasm placeholder shapes, determinism | `two.c`, `two.il`, `obj_mod.il`, `obj_asm.il`, `multi-main.il`, `undef.il`, `msvcshape.il`, `pepatch.py`, `checks.sh`, `checks-results.txt`, `runtime-results.txt` |
| `merge/` | C: `netmerge` on two modules | `m1.c`, `m2.c`, `m1.il`, `m2.il`, `merge-results.txt` |
| `tools/` | `mdinfo` (PE/metadata/IL token dump) and `netmerge` (C# on System.Reflection.Metadata, net8.0, no packages; `nuget.config` clears package sources) | `mdinfo/Program.cs`, `netmerge/Program.cs`, `*.csproj` |
| `clr/` | MSVC `/clr` objects | `a.cpp`, `b.cpp`, `clr-results.txt` |
| `elfcoff/` | D: CIL in ELF and COFF, machine variants, archivers and linkers | `cil-elf.s`, `cil-coff.s`, `linkorder.s`, `link.ps1`, `lib.ps1`, `lib2.ps1`, `elfcoff-results.txt`, `lib-results.txt` |
| `lto/` | E: bitcode objects, cc1 link, timings, token density, 101-module merge | `a.c`, `b.c`, `gen.py`, `time.sh`, `lto-results.txt`, `token-density.txt` |

Key commands:

```
# B: assemble, patch, run
ilasm -nologo -quiet -dll -pe64 -x64 two.il -output=two.dll
python pepatch.py stream two.dll two_stream.dll ; python pepatch.py section two.dll two_section.dll
dotnet exec --runtimeconfig net8.json two_stream.dll          # exit 43
# C: merge
dotnet build -c Release tools/netmerge ; netmerge merged.dll m1.netmodule m2.netmodule
# D: CIL in ELF, unknown machine
clang --target=x86_64-unknown-linux-gnu -c cil-elf.s -o cil-elf.o   # then patch e_machine at offset 18
llvm-nm cil-elf-em.o ; llvm-ar rcs lib.a cil-elf-em.o ; llvm-nm --print-armap lib.a
# E: bitcode objects and an LTO-style link
clang --target=x86_64-pc-windows-msvc-msil -c -flto -O2 a.c -o a.o
clang --target=x86_64-pc-windows-msvc-msil -S -O2 -x ir a.o -Xclang -mlink-bitcode-file -Xclang b.o -o ab.il
python gen.py 100 40 && ./time.sh
```

Installs: none. Build trees: none (the C# tools build in seconds with the installed .NET 8 SDK).
Disk: about 60 MB on C:, about 310 KB on D: (this report and its supporting files).
