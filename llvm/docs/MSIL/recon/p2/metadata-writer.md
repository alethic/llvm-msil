# Recon P2: the ECMA-335 metadata, CIL body and IL-only PE writer (`metadata-writer`)

- Key: `metadata-writer`. Date: 2026-10-20. Checkout: branch `msil` at `1ea64fe9fc7c`.
- Scope: Phase 2's "direct PE writer" (Design.md 5.5): writing ECMA-335 metadata, CIL method bodies and the
  IL-only PE image from C++ inside LLVM, with ilasm kept as a test oracle. Covers what LLVM already has,
  a library design (tables, heaps, signatures, bodies, PE envelope, determinism), the CIL binary encoder,
  experiments, a file layout, sizes, tests and work packages.
- Scratch: `C:\llvm-msil-scratch\p2\metadata-writer\` (prototype, tools, outputs, ilasm measurements).
  Supporting files (sources, drafts and trimmed logs, no binaries) are in
  [metadata-writer-files/](metadata-writer-files/).
- Nothing was installed or downloaded. No tracked file was modified. No build was run in
  `D:\llvm-msil\build`; the only builds were two small `dotnet build`s in scratch (the C# dumper).
- Legend: **[V]** verified here by running it; **[S]** read from the source of this checkout (path:line);
  **[E]** from ECMA-335 6th edition (June 2012) as typed into the prototype and then cross-checked by an
  experiment where the tag says so; **[U]** not verified.

---

## 0. Summary and recommendations

1. **Nothing in LLVM knows CLI metadata.** A grep for `COR20`, `cormeta`, `BSJB`, `#Strings`,
   `CLR_RUNTIME_HEADER` finds only the PE data-directory index `COFF::CLR_RUNTIME_HEADER`
   (`llvm/include/llvm/BinaryFormat/COFF.h:645`), its YAML key `ClrRuntimeHeader`
   (`llvm/lib/ObjectYAML/COFFYAML.cpp:518-519`) and its llvm-readobj label (`llvm/tools/llvm-readobj/COFFDumper.cpp:803`)
   [S]. What helps is the PE plumbing: the on-disk structs in `llvm/include/llvm/Object/COFF.h`
   (`dos_header` :58, `coff_file_header` :80, `pe32_header` :109, `pe32plus_header` :145, `data_directory`
   :177, `debug_directory` :182, `coff_section` :449, `coff_base_reloc_block_header` :792), `COFFObjectFile`
   (reads our PE: [V] with llvm-readobj 22.1.3), yaml2obj COFF (can build test PEs with a CLR data directory),
   `StringTableBuilder` (ELF kind = the #Strings layout with tail merging), `ConvertUTF`, `SHA256`, `SHA1`,
   `EndianStream`, `FileOutputBuffer`. lld/COFF's writer is the wrong shape to reuse (chunks, symbol tables),
   but its DOS stub and its "write zeros, hash the file, patch the timestamp" pattern (`writeBuildId`,
   `lld/COFF/Writer.cpp:2668-2717`) are the model.
2. **A prototype writer works on every runtime.** `metadata-writer-files/py/cliwriter.py` (700 lines of
   Python, stdlib only) writes heaps, all 14 tables Phase 1 needs plus CustomAttribute, #~, the metadata root,
   tiny and fat bodies with filter EH clauses, branch relaxation, and the PE envelope, byte by byte. Its
   Phase-1-shaped hello program (FieldRVA image copied by `<Module>::.cctor`, P/Invoke `puts`, `ldstr`,
   `calli` through an `ldftn` address field, an exception filter, `switch`, a long branch, a 2000-byte method
   name, an assembly custom attribute) runs on .NET 8 and .NET 10 (Windows, tiered and `TieredCompilation=0`),
   .NET 8 and .NET 10 on Linux (WSL), and .NET Framework 4.8 (as `.exe`) [V]. Its `main` IL is byte-identical
   to ilasm's for the same `.il` (371 bytes); it survives ildasm -> ilasm round trips with both toolsets [V].
3. **Use Roslyn's envelope, not ilasm's.** For the 64-bit triples: PE32+, machine from the triple (AMD64 or
   ARM64), `ILONLY` only, **no mscoree import, no entry stub, no `.reloc`**. Roslyn writes exactly this for
   `/platform:x64` and it runs as a `.exe` directly on .NET Framework 4.8 [V]; ilasm `-pe64 -x64` adds a
   16-byte IAT, a `_CorDllMain`/`_CorExeMain` import, a `mov rax,[imm64]; jmp rax` stub and a DIR64 base
   relocation that nothing needs [V]. The mscoree stub stays only for PE32 (i686 triples, phase 5).
4. **Determinism:** hash the image with a zero MVID and a zero timestamp using SHA-256, then patch the MVID
   (first 16 bytes, version-4 and RFC-4122 bits set) and the COFF timestamp (next 4 bytes, high bit set). This
   is exactly `BlobContentId.FromHash` (Roslyn `/deterministic`), checked bit for bit [V]. Two runs give
   byte-identical files [V].
5. **The library** (section 4): constants and schemas in `llvm/include/llvm/BinaryFormat/CLI.h` with two X-macro
   files `CLITables.def` and `CILOpcodes.def` (drafts generated and verified in this recon), and a new
   target-independent component (working name `llvm/lib/CLI`, namespace `llvm::cli`) with a
   `MetadataBuilder`, signature and custom-attribute encoders, an `ILBuilder` (labels, relaxation, EH, tiny/fat
   headers), a `PEWriter`, and a `MetadataReader` that the bcl-binding work needs anyway; plus a dumper tool.
   About 8,000-9,500 lines including tests, in 8 work packages of which 5 run in parallel after a small
   header-contract package. No widely-included header changes: the whole library is additive.
6. **The CIL encoder:** the opcode table (`CILOpcodes.def`, 219 entries) matches System.Reflection.Emit.OpCodes
   and System.Reflection.Metadata.ILOpCode for all 218 opcodes .NET knows (ECMA's `no.` 0xFE19 is in no .NET
   tool), and re-encoding an ilasm-assembled method that uses every opcode gives identical bytes with both
   ilasms [V]. All 99 CIL opcode values written in `llvm/lib/Target/MSIL/MSILInstr*.td` agree with ECMA-335 [V].
   One expansion function (MCInst -> CIL instructions) should feed both the text printer and the binary
   encoder; the encoding itself lives in the library, so msil-ld can synthesize and re-encode bodies too.
7. **Shortest forms matter:** Phase 1 prints long branches and long `ldc.i4` for 64-bit constants and address
   offsets. On the 20 Phase-1 corpus programs, short branches alone make the IL 4.4% smaller at -O2 (8.6% at
   -O0); short branches plus the shortest `ldc.i4` forms make it **16.5% smaller at -O2 and 23.6% at -O0** [V].
   IL size drives the JIT's inlining decisions [U], so the encoder should choose the shortest forms, and the
   text printer should print the same ones so ilasm stays an exact oracle.
8. **New CLR facts that constrain Phase 2** [V], on .NET 8, .NET 10 and Framework 4.8 alike:
   - a type can hold at most **65,524 methods** and about **65,535 fields** (65,525 methods or 65,536 fields
     fail to load: Framework "Type '<Module>' ... contains more methods than the current implementation allows" /
     "Internal limitation: too many fields", CoreCLR "Entry point not found"). Today every C function is a
     method of `<Module>` and every global and address-taken function an address field there (D12, M9), so a
     large linked program can exceed this; msil-ld must spill into further holder classes.
   - with **exactly 65,535 rows** in Field (and by the same rule MethodDef, Param, Event, Property), a trailing
     empty member list needs the value 65,536 in a 2-byte column. System.Reflection.Metadata's MetadataBuilder
     (Roslyn's writer) silently writes 0 and the runtimes reject the file ("File is corrupt", 0x8013110E). Our
     writer must pad such a table to 65,536 rows.
   - **RVA field data is read-only on Windows** (.NET 8, .NET 10, Framework) even in a writable `.sdata`
     section; on Linux CoreCLR it is writable only in a writable section. D12's copy into `AllocHGlobal` memory
     stays necessary; RVA data goes in `.text`, 8-aligned (Roslyn does the same).
   - **float32 signaling-NaN constants are quieted by every runtime's JIT**, not only by ilasm: `ldc.r4` with
     the exact bytes `01 00 80 7F` stores `0x7FC00001` on .NET 8, .NET 10 and Framework 4.8, while the same bits
     moved through `ldind.r4`/`stind.r4` stay `0x7F800001`. Status.md's "This lasts until the phase-2 PE writer"
     (section 3, Limits) is therefore only half true.
   - method names longer than 1023 bytes run everywhere (a 2000-byte name works), but PEVerify `/md` reports
     `VLDTR_E_TD_NAMETOOLONG` (0x801312E6) for them, so the 1023-byte limit should stay while PEVerify is a gate leg.

---

## 1. What exists in this checkout

### 1.1 PE/COFF formats and readers

| Piece | Where | Use for the CLI writer |
|---|---|---|
| Data directory index 14 `CLR_RUNTIME_HEADER` | `llvm/include/llvm/BinaryFormat/COFF.h:630-648` [S] | the CLI header's directory slot |
| Machine types (AMD64 0x8664, ARM64 0xAA64, I386 0x14C) | `COFF.h:93-105` [S] | COFF header |
| `Characteristics` (`IMAGE_FILE_EXECUTABLE_IMAGE` 0x2, `LARGE_ADDRESS_AWARE` 0x20, `32BIT_MACHINE` 0x100, `DLL` 0x2000) | `COFF.h:138-170` [S] | COFF header |
| `SectionCharacteristics` (`CNT_CODE` 0x20 :303, `CNT_INITIALIZED_DATA` 0x40 :304, `MEM_EXECUTE` :335, `MEM_WRITE` :337) | `COFF.h:298-338` [S] | section headers |
| `DLLCharacteristics`, `WindowsSubsystem` (`CUI` = 3 :654) | `COFF.h:650-693` [S] | optional header |
| `DebugType` (`CODEVIEW` 2 :720, `REPRO` 16 :734) | `COFF.h:717-736` [S] | debug directory (Phase 4 PDB, deterministic marker) |
| `BaseRelocationType` (`HIGHLOW` 3 :742, `DIR64` 10 :748) | `COFF.h:738-749` [S] | only for the PE32 stub |
| On-disk little-endian structs `dos_header`, `coff_file_header`, `pe32_header`, `pe32plus_header`, `data_directory`, `debug_directory`, `coff_section`, `coff_import_directory_table_entry` :564, `coff_base_reloc_block_header` | `llvm/include/llvm/Object/COFF.h:58-800` [S] | the writer can fill these directly, as lld does |
| `COFFObjectFile` (`getDataDirectory` :1155, `getRvaPtr` :1201, `getRvaAndSizeAsBytes` :1207) | `llvm/include/llvm/Object/COFF.h` [S] | the reader locates the CLI header and metadata through it; it parses the prototype's PE, imports and base relocations correctly [V] (VS-bundled llvm-readobj 22.1.3, `--file-headers --sections --coff-imports --coff-basereloc`) |
| yaml2obj COFF: optional header and every data directory including `ClrRuntimeHeader`, raw `SectionData` | `llvm/lib/ObjectYAML/COFFYAML.cpp:472-520` (`MappingTraits<COFFYAML::PEHeader>`), `COFFEmitter.cpp:235-470` [S] | LLVM-style binary test inputs for the reader and dumper (a PE with a hand-made metadata blob in `.text`) |
| llvm-readobj COFF dumper | `llvm/tools/llvm-readobj/COFFDumper.cpp:803` prints only "CLRRuntimeHeader" RVA/size [S] | a `--cli` option could be added, but a new tool avoids upstream edits (section 4.1) |

There is **no** IMAGE_COR20_HEADER struct, no metadata root, stream, table, heap, signature or IL code in
llvm, clang or lld. The only CLI-related symbols elsewhere are COFF object-file CLR tokens
(`AuxiliaryCLRToken`, `COFF.h:504`; `coff_aux_clr_token`, `Object/COFF.h:540`), which belong to MSVC `/clr`
objects and are irrelevant here. The Phase-1 `MCSectionMSIL` (`llvm/include/llvm/MC/MCSectionMSIL.h`) is a
logical bucket only.

### 1.2 lld/COFF's PE writer

`lld/COFF/Writer.cpp` (3150 lines) [S]:
- `dosProgram` and its static asserts (:70-80): the 56-byte "This program cannot be run in DOS mode" stub.
  ilasm and Roslyn use the classic 128-byte header+stub (e_lfanew = 0x80); either works.
- `Writer::assignAddresses` (:1759): headers size = DOS stub + "PE\0\0" + COFF header + optional header + data
  directories + section table, aligned to the file alignment; section RVAs aligned to the section alignment.
- `Writer::writeHeader<PEHeaderTy>` (:1834-2056): fills `dos_header`, `coff_file_header`, `pe32(plus)_header`
  and `data_directory[16]`, then each section header. Sets linker version 14.0 because 0.0 breaks Authenticode
  validation on Windows 7 (comment and assignment at :1910-1916).
- `Writer::writeBuildId` (:2668-2717): writes everything with a zero timestamp, hashes the output with
  `xxh3_64bits`, and patches the COFF `TimeDateStamp` (and debug-directory stamps) for `/Brepro`.

The writer is built around `Chunk`/`OutputSection`/symbol tables for COFF linking; msil-ld needs one code
section and a fixed handful of headers, so copy the patterns (and possibly the DOS stub), not the classes.

### 1.3 Support pieces

| Need | LLVM API | Notes |
|---|---|---|
| #Strings: NUL-terminated UTF-8, offset 0 = empty, tail merging (Roslyn merges suffixes) | `StringTableBuilder(StringTableBuilder::ELF)` (`llvm/include/llvm/MC/StringTableBuilder.h:25-93`) [S] | ELF kind reserves the leading NUL and tail-merges in `finalize()`; offsets are known only after finalize, which fits a finalize-then-serialize builder. Lives in LLVMMC. |
| #US: UTF-16LE | `convertUTF8ToUTF16String` (`llvm/include/llvm/Support/ConvertUTF.h:346`) [S] | plus the trailing-byte rule (4.4) |
| Deterministic MVID | `SHA256` (`llvm/include/llvm/Support/SHA256.h:34-57`) [S] | 32-byte digest; 20 bytes are used |
| Public key token of an AssemblyRef | `SHA1` | token = last 8 bytes of SHA-1(public key), reversed: gives `b03f5f7f11d50a3a` for System.Runtime 8.0 and `b77a5c561934e089` for mscorlib [V] |
| Little-endian output | `support::endian::Writer` (`EndianStream.h`), as `SPIRVObjectWriter` does (`llvm/lib/MC/SPIRVObjectWriter.cpp`) [S] | |
| Output file | `FileOutputBuffer` (lld's `Writer::openFile`, `lld/COFF/Writer.cpp:2058`) [S] | |

### 1.4 Precedents for a new binary format in LLVM

- **DXContainer** (DirectX): `BinaryFormat/DXContainer.h` (1044 lines) + `DXContainerConstants.def` (430),
  `Object/DXContainer.{h,cpp}` reader (1830), `ObjectYAML/DXContainer{YAML,Emitter}` (2400), MC writer pieces,
  `obj2yaml/dxcontainer2yaml.cpp`, `unittests/Object/DXContainerTest.cpp` (1687): 7,799 lines in total [S]. The
  closest analog for "constants in BinaryFormat, reader in Object, YAML for tests".
- **SPIR-V**: `llvm/lib/MC/SPIRVObjectWriter.cpp` (53 lines) + `MCSPIRVStreamer.cpp` (26): a target whose
  AsmPrinter produces almost everything and whose object writer only concatenates [S].
- **PDB** (`llvm/lib/DebugInfo/PDB`, `CodeView`, `MSF`): a Microsoft format with builder classes
  (`PDBFileBuilder`, `DbiStreamBuilder`) used by lld-link and by `llvm-pdbutil` (dump and yaml2pdb); a model for a
  builder/reader library shared by a linker and a dumper.

### 1.5 What the MSIL target has that the binary path will reuse or must change

- Every TableGen record already carries its CIL opcode: `class MSILInst<bits<16> cilop, ...>`, `bits<16> Inst =
  cilop` (`llvm/lib/Target/MSIL/MSILInstrFormats.td:25-46`), `0xFFFF` for pseudos and multi-opcode records [S].
  `check_td_opcodes.py` compared all 99 values against the ECMA table: **0 problems** [V]. There is no
  `-gen-emitter` TableGen run yet (`llvm/lib/Target/MSIL/CMakeLists.txt:5-10`) [S].
- The short forms (`ldarg.0-3`/`.s`, `ldloc`, `stloc`, `starg.s`, `ldc.i4.m1/.0-8/.s`) are chosen in C++ by
  `MSILInstPrinter` (`MCTargetDesc/MSILInstPrinter.cpp:56-184`: `ShortFormKind`, `getShortForm` :77,
  `printShortForm` :145); branches always print the long forms (:226-232); `CONST_I64` prints `ldc.i4 N` (never
  shortened) or `ldc.i8 N`, then `conv.i` (:172-179); `GLOBAL_ADDR` prints its offset as a long `ldc.i4`
  (`printGlobalAddrOffset`, :358-372) [S].
- Records that print several opcodes: `TRAP` "ldnull; throw" (`MSILInstrControl.td:183`), `LOCAL_TEE`/`ARG_TEE`
  "dup; stloc/starg" (`MSILInstrInfo.td:175-177`), `CONST_I64` (:203), `GLOBAL_ADDR`, `BR_TABLE_I32`
  ("switch (...); br"), `MEMMOVE` "conv.i8; dup; call Buffer::MemoryCopy" (`MSILInstrMemory.td:148-151`), and
  every memory record whose memflags operand prints `unaligned.`/`volatile.` prefix lines [S].
- **Text-only pieces that binary output cannot use as they are** [S]:
  - BCL members are preformatted text: `MSILMemberRefExpr::BCLMember` (`MCTargetDesc/MSILMCExpr.h`, "printed
    verbatim"), the asm strings of `MEMMOVE` and `MEMORY_BARRIER` (`MSILInstrMemory.td:150-167`), and
    `AllocHGlobal`, `GetCommandLineArgs`, `StringToHGlobalAnsi` in `MSILAsmPrinter.cpp:171-173, 1059, 1074,
    1087`; `extends [mscorlib]System.ValueType` in `MSILTargetStreamer.cpp:151`.
  - The module initializer and `'<msil>Main'` are built as text lines by `SynthesizedBody`
    (`MSILAsmPrinter.cpp:205-271`, class at :208, used at :869 and :1033) and written with `MSILTargetStreamer::emitILLine`.
  - `MSIL::Signature`/`MSIL::ValType` (`MCTargetDesc/MSILMCTypeUtilities.h`) know only int32, native int,
    int64, float32, float64; BCL signatures also need `void`, `string`, `string[]`, `void*`, `int64`.

  All three must become structured (a CIL builder API, structured member references with a general type
  signature) before a binary writer can emit them, and that is also what P2-B's reference binding needs.
- `MSILTargetMachine::createMCStreamer` refuses `ObjectFile` (`MSILTargetMachine.cpp:209-218`) [S].

---

## 2. What ilasm emits for Phase-1 programs (measured)

Phase-1 programs from `msil/test/M1/11-programs` compiled with `build\bin\clang.exe
--target=x86_64-pc-windows-msvc-msil -ffreestanding -O2 -S` and assembled with Framework ilasm 4.8 `-dll -pe64
-x64` (D31), dumped with the System.Reflection.Metadata dumper `metadata-writer-files/mdump/Program.cs` [V]
(`ilasm_measure.log`):

| Program | File | .text | Metadata | IL code (bodies) | Tables (rows) |
|---|---|---|---|---|---|
| fib | 6,656 | 0x142A | 0x528 (1,320 B) | 3,436 B (7 fat, largest 2,200) | Module 1, TypeRef 2, TypeDef 2, Field 18, MethodDef 8, Param 7, MemberRef 1, ClassLayout 1, StandAloneSig 5, ModuleRef 1, ImplMap 1, FieldRVA 1, Assembly 1, AssemblyRef 1 |
| quicksort | 7,168 | 0x16EA | 0x474 | 4,309 B (4 fat) | Field 17, MethodDef 5, Param 4, StandAloneSig 2, rest as above |
| string-table | 10,240 | 0x239A | 0x8C4 | 5,679 B (5 fat) | Field 48, MethodDef 6, Param 5, StandAloneSig 3 |
| dispatch-vm | 12,800 | 0x2DAA | 0xAD0 | 7,243 B (24 fat) | Field 56, MethodDef 25, Param 23, StandAloneSig 15 |

Every Phase-1 module uses exactly these **14 tables**; all heaps are tiny (#Strings 0x1F8-0x583, #Blob
0x58-0xA4, #GUID 16 bytes), so every index is 2 bytes.

ilasm's choices, all accepted by every runtime, and what our writer should do instead:

| Item | ilasm (Framework 4.8 and modern 10.0.12 identical except MVID/timestamp/module name) | Roslyn / our writer |
|---|---|---|
| Method headers | fat (12 bytes) for every body, `InitLocals` set on bodies without `.locals` (header byte 0x13), e.g. `'<msil>Main'` (6 bytes of code) | tiny (1 byte) when code < 64 bytes, maxstack <= 8, no locals, no EH; never `InitLocals` (D13) |
| Param rows | one per parameter, named `A_0`, `A_1`, ... when unnamed | none (optional); names only where useful (P2-C exports) |
| Assembly HashAlgId | 0 | 0x8004 (SHA1) like Roslyn, or 0; irrelevant without a strong name |
| `Sorted` bit vector | 0x000016003325FA00 (also flags EventMap and PropertyMap) | 0x000016003301FA00 = exactly ECMA's sorted tables (Roslyn, SRM) |
| Token order | TypeRefs and MemberRefs in order of first appearance in the source | any deterministic order |
| `.data cil` image | in `.text`, right after the CLI header | in `.text`, 8-aligned, after the bodies (Roslyn puts mapped data at the end of `.text`, 8-aligned, `rva 0x27B0/0x27C8` in a C# test [V]) |
| Envelope (`-pe64 -x64`) | PE32+, AMD64, image base 0x140000000 (DLL too), section align 0x2000, file align 0x200, linker 11.0, OS/subsystem 4.0, CUI, DllCharacteristics 0x8540, stack 0x400000/0x4000, heap 0x100000/0x2000; IAT (16 B) at 0x2000, CLI header at 0x2010, import of mscoree `_CorDllMain`/`_CorExeMain`, entry stub `48 A1 <imm64 IAT> FF E0`, `.reloc` with one DIR64 entry for the stub | same values except: DLL image base 0x180000000 (Roslyn), no IAT/import/stub/`.reloc`, CLI header at 0x2000 |

Modern ilasm `-det` derives MVID and timestamp from a hash, but not with Roslyn's scheme (its MVID has no
version-4 bits, timestamp 0x9BA6E2F4/0x369E3280 in two runs) [V].

---

## 3. Experiments

All runs on this host: Windows .NET 8.0.32 and 10.0.13 (`dotnet exec --runtimeconfig`, which binds the newest
patch), .NET Framework 4.8 (release 0x82405) for `.exe` files, WSL Ubuntu .NET 8.0.31 and 10.0.13. Exit codes
in the logs are the low 8 bits (Git Bash and Linux). `run_all.sh` reproduces the whole matrix; `runs.log` is its
output.

### 3.1 The byte-by-byte writer (E1)

`py/cliwriter.py` (layers mirror the proposed C++ library): compressed integers (II.23.2), heaps with dedupe and
the #US trailing byte (II.24.2.4), the full II.22 schema of 45 tables and 13 coded indexes, 2/4-byte index sizing
(II.24.2.6), sorting of the sorted tables, #~ and the metadata root (II.24.2.1), signature encoders (II.23.2),
an IL assembler with labels and branch relaxation, tiny/fat headers with small/fat EH sections (II.25.4), and the
PE envelope (II.25) with options: PE32/PE32+, DLL/EXE, CorFlags, mscoree stub, RVA data in `.text` or in a
writable `.sdata`, deterministic id.

`py/hello.py` builds a module shaped like Phase-1 output: `<Module>` with 4 fields (`'<msil>image'` with FieldRVA
over a `.pack 1 .size 72` value type `'<msil>image_t'`, `'<msil>data'`, `msg`, `'<msil>fn_square'`) and 7 methods
(`main`, `square`, `thrower`, a 2000-byte name, P/Invoke `puts` via ImplMap + ModuleRef `ucrtbase`/`libc`,
`.cctor` doing `AllocHGlobal` + `cpblk` from `ldsflda` + `ldftn`, `'<msil>Main'(string[])` entry point), three
MemberRefs, two StandAloneSigs (locals; `calli int32(int32)`), an `AssemblyMetadataAttribute("msil.triple",
"<triple>")` custom attribute on the Assembly row. `main` returns a bit mask: 1 calli, 2 exception filter,
4 short-branch loop, 8 switch, 16 long branch over 200 `nop`s, 32 call of the 2000-byte name, 64 float32 sNaN
constant stored exactly.

Results (`runs.log`):

| Variant | .NET 8 | .NET 8 TC=0 | .NET 10 | Framework 4.8 | Linux .NET 8 / 10 |
|---|---|---|---|---|---|
| `hello.dll`, Roslyn envelope (PE32+ AMD64, ILONLY, no import/stub/reloc) | 63 | 63 | 63 | n/a (DLL) | 63 / 63 (`--os linux`, ModuleRef `libc`) |
| `hello.exe`, same envelope | 63 | 63 | 63 | **63** | |
| `hellostub.exe`, ilasm envelope (mscoree import + x64 stub + `.reloc`) | 63 | 63 | 63 | 63 | |
| fat EH section forced (`41 1C 00 00` + 24-byte clause) | 63 | 63 | 63 | 63 | |
| 70,000 extra fields on two classes (4-byte Field, TypeDef.FieldList and #Strings indexes, #Strings 0x3793AA) | 63 | 63 | 63 | 63 | |

63 = every check except the float32 sNaN one (3.4). The 2000-byte method name was called successfully on every
runtime. The output also passes PEVerify `/md /il` apart from the expected items: the 0x8013129D
`VLDTR_E_FMD_GLOBALITEM` warning on every global member (ilasm's output gets the same), 0x801312E6
`VLDTR_E_TD_NAMETOOLONG` for the 2000-byte name, "Cannot modify an imaged based (RVA) static" in `.cctor` and
"Instruction cannot be verified" for `calli` (D21's allowed messages); ILVerify reports only `ImportCalli not
implemented` and `cpblk` unverifiable [V].

### 3.2 Comparison with ilasm (E2)

`py/gen_hello_il.py` writes the same module as ILAsm text (with a 1000-byte name, since both ilasms reject names
over 1023 bytes). Diff of the two dumps: `hello-python-vs-ilasm.diff` [V]:
- `main`'s 371 IL bytes are **identical**, tokens included; `.cctor`'s 55 bytes differ only in the
  `AllocHGlobal` MemberRef token (ilasm numbers MemberRefs by first appearance: 0x0A000003 vs 0x0A000001).
- The other differences are the ilasm choices of section 2 (Param rows, fat headers with InitLocals, sorted mask,
  envelope, TypeRef order, hash algorithm).
- ildasm (Framework 4.8 and 10.0.12) disassembles our output and both ilasms reassemble it into assemblies that
  run (exit 63) [V]; the reassembled `ldc.r4` loses the sNaN (see 3.4), so a round-trip oracle must normalize
  float32 NaN constants.

### 3.3 PE envelope variants (E3)

| CorFlags / machine | Framework 4.8 exe | .NET 8 / .NET 10 (x64 host) |
|---|---|---|
| PE32 I386, ILONLY ("AnyCPU"), with stub | runs (64-bit process) | runs |
| PE32 I386, ILONLY, **no** stub/import | runs | runs |
| PE32 I386, ILONLY + 32BITREQUIRED | runs (32-bit process) | FileLoadException (.NET 10: same) |
| PE32 I386, ILONLY + 32BITREQUIRED + 32BITPREFERRED | runs | BadImageFormatException |
| PE32+ AMD64, ILONLY + 32BITREQUIRED | BadImageFormatException | FileLoadException / BadImageFormatException |
| C# `csc -platform:x64` exe (no import, no stub, no reloc) | runs | (C# exe, not run) |

So the mscoree import and entry stub are not needed by any runtime on this Windows 11 host, for PE32 or PE32+
[V]. Roslyn keeps them only for PE32 (`t-x86`, `t-anycpu`: IAT 8 bytes, `.reloc` HIGHLOW), and ilasm writes them
always. Recommendation: PE32+ never; PE32 (i686, phase 5) keeps them for compatibility with what every other
toolchain emits [U: older Windows loaders not tested].

Roslyn header values per platform [V] (`csc -noconfig -deterministic`): x64 and arm64 exe: PE32+, characteristics
0x22, image base 0x140000000 (library 0x180000000), stack 0x400000/0x4000, CorFlags ILONLY; x86: PE32, 0x102,
image base 0x400000, stack 0x100000/0x1000, ILONLY|32BITREQUIRED; anycpu: PE32, 0x22, ILONLY. All: section
alignment 0x2000, file alignment 0x200, OS and subsystem version 4.0, CUI, DllCharacteristics 0x8540, linker
48.0, a Debug directory with a REPRO entry, and a `.rsrc` version resource.

### 3.4 Float32 signaling NaN (E4)

With the IL bytes `22 01 00 80 7F` (verified in the dump), the value stored by `stind.r4` is `0x7FC00001` on
.NET 8 (tiered and TC=0), .NET 10 and Framework 4.8; storing the int32 bits and moving them through
`ldind.r4`/`stind.r4` keeps `0x7F800001` everywhere [V] (`--snan-probe`; 252 = quieted, 248 = exact). Both ilasms
also quiet the constant when assembling, including ildasm's `ldc.r4 (01 00 80 7F)` byte syntax [V]. The binary
writer fixes the IL bytes, but exact float32 sNaN constants need codegen to materialize them through integer
bits. (float64 sNaN constants are kept by ilasm [V]; their JIT behaviour was not tested [U].)

### 3.5 RVA data writability (E5)

Writing one byte through `ldsflda` of the image field: AccessViolationException on .NET 8, .NET 10 and
Framework 4.8 on Windows whether the data is in `.text` (0x60000020) or in a `.sdata` section marked
0xC0000040 (read/write); on Linux .NET 8/10 it is fatal in `.text` (exit 134) but **succeeds** in `.sdata` [V].
Phase 1's e09b2 saw the Windows half. D12 (copy the image into `AllocHGlobal` memory) therefore stays; keep the
image in `.text`. Read-only, relocation-free data could later be read in place from the mapped image, which
never moves [U: not tested as an optimization].

### 3.6 Limits (E6)

- Methods per type: 65,524 on `<Module>` run; 65,525 fail on every runtime (binary search 65,512 / 65,517 /
  65,519 / 65,521 / 65,523 / 65,524 pass, 65,525+ fail) [V]. All extra methods shared one body RVA, which the
  runtimes accept.
- Fields per type: 65,534 (SRM-built) run; 65,536 fail ("Internal limitation: too many fields"); 70,000 split
  35,000 + 35,000 over two classes run [V]. 65,535 itself cannot be tested without the list-end problem below.
- The list-end problem: with exactly 65,535 Field rows, a type after the last field owner needs FieldList =
  65,536, which a 2-byte column (2 bytes because rows < 2^16, II.24.2.6) cannot hold. SRM's MetadataBuilder +
  ManagedPEBuilder (`mdump --srm-fields 65535`) writes 0; the runtimes report "File is corrupt" (0x8013110E;
  .NET 10 "Entry point not found") [V]. The prototype's writer raised an exception at the same boundary. Rule
  for our writer: if Field, MethodDef, Param, Event or Property would have exactly 0xFFFF rows and an owner's list
  ends at the table end, append one inert row (a private static field, a `ret` method, a sequence-0 Param).
- Names: CoreCLR and Framework run a 2000-byte method name; PEVerify `/md` flags it (0x801312E6). Type names
  were not tested [U] (CoreCLR has a 1024-character class-name limit in places).

### 3.7 Opcode table (E7)

`py/check_opcodes.py` [V] (`check_opcodes.log`):
1. `cilopcodes.py` (typed from ECMA-335 III) vs System.Reflection.Emit.OpCodes (218 opcodes + 8 reserved
   `prefix1-7`/`prefixref`) and the System.Reflection.Metadata `ILOpCode` enum (218 values): identical names,
   values and operand kinds; only ECMA's `no.` (0xFE19) is absent from both, and both ilasms reject it
   ("syntax error at token 'no'").
2. An `.il` method using all 218 opcodes with operands, assembled by both ilasms, decodes with our table into the
   same 219-instruction sequence (500 bytes) with the same immediates, except ilasm's quieted float32 sNaN.
3. Re-encoding the decoded list with the prototype's `IL` class (no relaxation) gives **identical bytes**.
`check_td_opcodes.py`: the 99 opcode values in `MSILInstr*.td` match ECMA-335 [V].
`gen_opcodes_def.py` turns the verified table plus OpCodes' stack behaviour and flow control into the draft
`CILOpcodes.def`.

### 3.8 Table schema (E8)

`py/verify_schema.py` computes the row size of every table from the II.22 schema and the II.24.2.6 index rules,
and compares it with System.Reflection.Metadata's `GetTableRowSize` on .NET 8 `System.Runtime.dll` and
`mscorlib.dll` (reference pack 8.0.31), `System.Private.CoreLib.dll`, Framework 4.8 reference `mscorlib.dll`,
`System.dll`, `System.Core.dll` and the prototype's output: **34 of 45 tables covered, all row sizes match**,
including 4-byte heap indexes (HeapSizes 0x05) and 4-byte coded indexes [V] (`verify_schema.log`). The 11
uncovered tables (the Ptr tables, ENCLog/ENCMap, Assembly(Ref)Processor/OS) are never emitted by current
compilers. `gen_tables_def.py` writes the draft `CLITables.def` from that schema.

Real reference assemblies, for the reader's scale [V]: .NET 8 reference `System.Runtime.dll` 828,712 bytes, 891
TypeDefs, 11,108 MethodDefs, 15,805 Params, 8,392 CustomAttributes, #Strings 0x24486, HeapSizes 0x05; .NET 8
reference `mscorlib.dll` is a facade (1 TypeDef, 1,256 ExportedType forwarders, 34 AssemblyRefs); Framework
reference `mscorlib.dll` 2.7 MB, 2,993 TypeDefs, 24,237 MethodDefs, 30 tables.

### 3.9 Encoding size (E9)

`py/relax_measure.py` decodes every body ilasm wrote for the 20 corpus programs (-O2: 218 bodies, -O0: 359),
turns branch targets into labels and re-encodes with relaxation [V] (`relax_measure.log`):

| | -O2 | -O0 |
|---|---|---|
| IL code as Phase 1 prints it | 99,683 B | 123,775 B |
| short branches | 95,291 B (-4.4%) | 113,173 B (-8.6%) |
| short branches + shortest `ldc.i4` (`CONST_I64`, `GLOBAL_ADDR` offsets) | 83,270 B (**-16.5%**) | 94,601 B (**-23.6%**) |
| headers: all fat vs tiny where allowed | 2,616 -> 2,198 B | 4,308 -> 4,088 B |

### 3.10 Determinism (E10)

`mdump --contentid` runs `BlobContentId.FromHash(SHA-256("abc"))`: GUID `bf1678ba-018f-4acf-8141-40de5dae2223`,
stamp 0xA36103B0; the prototype's formula gives the same bytes `BA 78 16 BF 8F 01 CF 4A 81 41 40 DE 5D AE 22 23`
[V]. Rebuilding `hello.dll` twice gives byte-identical files [V]. The module name (the output file name) is part
of the hashed content, as in Roslyn.

---

## 4. Library design

### 4.1 Layering and file layout

```text
llvm/include/llvm/BinaryFormat/CLI.h          constants, enums, on-disk structs, compressed ints (header-only)
llvm/include/llvm/BinaryFormat/CLITables.def  II.22 schema: CLI_TABLE / CLI_COLUMN / CLI_CODED_INDEX (draft in -files)
llvm/include/llvm/BinaryFormat/CILOpcodes.def III opcodes: CIL_OPCODE(Enum, Text, Value, Operand, Pop, Push, Flow) (draft in -files)

llvm/include/llvm/CLI/  and  llvm/lib/CLI/     component "CLI" (LLVMCLI), namespace llvm::cli
  Opcodes.{h,cpp}          OpcodeInfo table from CILOpcodes.def, lookup by value and by text
  Blob.{h,cpp}             BlobBuilder, compressed int/blob/SerString writers and readers
  Signature.{h,cpp}        TypeSig model; method/field/local/property/typespec/methodspec signature encoder and decoder
  CustomAttribute.{h,cpp}  CA blob encoder/decoder (prolog 0x0001, fixed args, named args)
  Heaps.{h,cpp}            #Strings (StringTableBuilder ELF), #US, #Blob, #GUID builders
  MetadataBuilder.{h,cpp}  rows with handles, finalize (ordering, padding, sorting, token map), #~ and root
  ILBuilder.{h,cpp}        instructions with labels and entity references, relaxation, maxstack check
  MethodBody.{h,cpp}       tiny/fat headers, EH sections (small/fat), alignment
  PEWriter.{h,cpp}         envelope, CLI header, sections, optional mscoree stub, debug dir, deterministic id
  MetadataReader.{h,cpp}   PE -> CLI header -> root -> streams -> tables/heaps; row accessors
  Dump.{h,cpp}             text dump shared by the tool and tests
llvm/tools/llvm-cil-dump/                     dumper (picked up by add_llvm_implicit_projects: no hook)
llvm/unittests/CLI/                           gtests
```

- Upstream-file edits: one `add_subdirectory(CLI)` line in `llvm/lib/CMakeLists.txt` (explicit list :6-57) and
  one in `llvm/unittests/CMakeLists.txt` (explicit list :31-76) [S]. New tools need no edit
  (`add_llvm_implicit_projects`, `llvm/tools/CMakeLists.txt`; `llvm/cmake/modules/AddLLVM.cmake:2030`) [S].
  BinaryFormat gets new headers only (no `.cpp`, so `LLVMBinaryFormat` does not change and nothing relinks).
  Zero-hook alternative: put the component under `llvm/lib/Target/MSIL/CLI/` and its tests in `MSILTests`; it
  works (lld already links target libraries) but files a format library under a target.
- Dependencies: Support, BinaryFormat, MC (StringTableBuilder), Object (COFFObjectFile for the reader).
- Naming: "CLI" is ECMA-335's name (Common Language Infrastructure) but reads as "command-line interface" in
  LLVM; `ECMA335` or `DotNet` are alternatives (user decision 3 in the structured result).
- Consumers: msil-ld (final PE, synthesized `.cctor`/`Main`/export forwarders, reading reference assemblies and
  managed inputs), the MSIL target (binary instruction encoding, and the object writer if the object format is
  metadata-based), the dumper, tests.

### 4.2 `BinaryFormat/CLI.h` contents

`namespace llvm::CLI` (like `llvm::COFF`, `llvm::wasm`, `llvm::dxbc`):
- `enum class TableId : uint8_t` from `CLITables.def` (0x00 Module ... 0x2C GenericParamConstraint); later
  0x30-0x37 for Portable PDB (Phase 4). `enum class CodedIndexKind`; `constexpr uint32_t makeToken(TableId,
  uint32_t Row)`; token type 0x70 for #US.
- `enum class OperandKind` (InlineNone, ShortInlineVar, InlineVar, ShortInlineI, InlineI, InlineI8,
  ShortInlineR, InlineR, ShortInlineBrTarget, InlineBrTarget, InlineMethod, InlineField, InlineType, InlineTok,
  InlineSig, InlineString, InlineSwitch) and `enum class Opcode : uint16_t` from `CILOpcodes.def`.
- Element types (VOID 0x01 ... I 0x18, U 0x19, FNPTR 0x1B, OBJECT 0x1C, SZARRAY 0x1D, MVAR 0x1E, CMOD_REQD 0x1F,
  CMOD_OPT 0x20, SENTINEL 0x41, PINNED 0x45); calling-convention bytes (DEFAULT 0, C 1, STDCALL 2, THISCALL 3,
  FASTCALL 4, VARARG 5, UNMANAGED 9, FIELD 6, LOCAL_SIG 7, PROPERTY 8, GENERICINST 0x0A; GENERIC 0x10, HASTHIS
  0x20, EXPLICITTHIS 0x40).
- Attribute flag enums: TypeAttributes, FieldAttributes, MethodAttributes, MethodImplAttributes,
  ParamAttributes, PInvokeAttributes (NoMangle 0x1, CharSet bits, SupportsLastError 0x40, CallConvWinapi 0x100,
  CallConvCdecl 0x200, CallConvStdcall 0x300, ThisCall 0x400, FastCall 0x500), AssemblyFlags (PublicKey 0x1,
  Retargetable 0x100, processor-architecture bits 0x70), AssemblyHashAlgorithm (None 0, SHA1 0x8004).
- `COMIMAGE_FLAGS_*` (ILONLY 0x1, 32BITREQUIRED 0x2, IL_LIBRARY 0x4, STRONGNAMESIGNED 0x8, NATIVE_ENTRYPOINT 0x10,
  TRACKDEBUGDATA 0x10000, 32BITPREFERRED 0x20000); `struct Cor20Header` (72 bytes, little-endian fields, its own
  `{RVA, Size}` pair, since BinaryFormat cannot use `object::data_directory`).
- Method header bits (TinyFormat 0x2, FatFormat 0x3, MoreSects 0x8, InitLocals 0x10, fat size 3 dwords in bits
  12-15); EH section kinds (EHTable 0x1, OptILTable 0x2, FatFormat 0x40, MoreSects 0x80); clause flags
  (Exception 0, Filter 1, Finally 2, Fault 4).
- `inline unsigned encodeCompressedUInt(uint32_t, uint8_t Out[4])`, `decodeCompressedUInt`, signed variant.
- Metadata root constants: signature 0x424A5342 ("BSJB"), version 1.1, version string `v4.0.30319`, #~ version
  2.0, HeapSizes bits (#Strings 0x01, #GUID 0x02, #Blob 0x04), `SortedTablesMask = 0x000016003301FA00`.

The flag values above are the ones the prototype wrote and the runtimes and PEVerify accepted [V]; the
authoritative cross-check for the rest is `CorHdr.h` in the .NET Framework SDK
(`C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8\Include\um\`, present on this host; Microsoft-licensed, so
compare values in a scratch script, never copy text).

### 4.3 Table set for Phase 2 (and what not to preclude)

| Table | Needed by | Phase |
|---|---|---|
| Module, Assembly, AssemblyRef, TypeRef, TypeDef, Field, MethodDef, MemberRef, ClassLayout, StandAloneSig, ModuleRef, ImplMap, FieldRVA | everything Phase 1 emits today (section 2) | P2-A/B |
| CustomAttribute | P2-C symbol-table attribute (Design 5.4), `DisableRuntimeMarshalling`, `ThreadStatic`, `UnmanagedCallersOnly`, `SuppressGCTransition` (P3) | P2-C, P3 |
| FieldLayout | explicit-layout value types for struct-by-value P/Invoke signatures (Design 2.3) | P2/P3 |
| Param | parameter names on exported methods (C# callers see them); optional elsewhere | P2-C |
| NestedClass | helper types nested in an export class or per-triple classes (FeatureIdeas 1, 7) | later |
| TypeSpec, MethodSpec | generic BCL types and methods: `Vector128<T>`, `Unsafe`, `MemoryMarshal`, `Span<T>` (Design 3.2, FeatureIdeas 4/5) | P3/P4 |
| Constant | literal fields, default values (unlikely) | optional |
| GenericParam, GenericParamConstraint, InterfaceImpl, MethodImpl, Event(Map), Property(Map), MethodSemantics | only if output ever defines generic or interface-implementing types (FeatureIdeas 6) | not planned |
| File, ExportedType, ManifestResource | multi-module assemblies, type forwarders, embedded resources (an alternative home for the P2-C symbol table) | reader: yes (forwarders in reference facades); writer: optional |

Nothing in the design should assume `<Module>`: methods and fields go on any TypeDef with any namespace (export
class, per-triple classes of FeatureIdeas 7, class placement of FeatureIdeas 1, spill classes of 3.6), and the
signature model is general (pointers, byrefs, value types, generic instances) so FeatureIdeas 3-6 stay open.
The writer must also accept a machine/CorFlags choice independent of the triple (FeatureIdeas 7's AnyCPU
assembly of i686 + x86_64 code).

### 4.4 Heaps and index sizes

- **#Strings**: offset 0 is the empty string; NUL-terminated UTF-8; reject names containing NUL; tail merging is
  allowed (Roslyn does it; `StringTableBuilder(ELF)` gives it). Index width 4 iff the heap size >= 0x10000
  (HeapSizes 0x01; checked on the 70,000-field variant [V]).
- **#US**: offset 0 = 0x00; each entry = compressed byte length, UTF-16LE, one trailing byte that is 1 iff some code
  unit has a non-zero high byte or a low byte in 0x01-0x08, 0x0E-0x1F, 0x27, 0x2D, 0x7F [E]. `ldstr` tokens are
  0x70000000 | offset, so the heap is limited to 2^24 bytes.
- **#Blob**: offset 0 = 0x00; compressed length + bytes; dedupe identical blobs. Width 4 iff size >= 0x10000.
- **#GUID**: 16-byte entries, 1-based index; entry 1 is the MVID (its file offset is what the deterministic
  patch needs).
- Every stream size and the #~ stream are padded to 4 bytes; streams in the order #~, #Strings, #US, #GUID,
  #Blob (ilasm and Roslyn).
- **Simple index** into table T: 2 bytes iff rows(T) < 2^16. **Coded index** with n tag bits: 2 bytes iff the
  largest row count among its tables < 2^(16-n) (II.24.2.6) [E, V by 3.8]. `CustomAttributeType` has 3 tag bits
  and only MethodDef (tag 2) and MemberRef (tag 3).
- Real assemblies need the 4-byte forms (HeapSizes 0x05 in every large reference assembly); the reader must
  handle them all. Rarely seen #~ details for the reader: the `#-` uncompressed stream (ENC; Ptr tables), the
  HeapSizes 0x40 "extra data" dword after the row counts, and Portable PDB's `#Pdb` stream (Phase 4) [E/U].

### 4.5 Rows, order, tokens

The builder must collect first and assign row numbers at `finalize()`, because method bodies need tokens and
II.22 imposes orders the emitter does not produce naturally:
- TypeDef row 1 is `<Module>`; an enclosing class precedes the classes it encloses (II.22 introduction).
- Field and MethodDef rows are runs owned by consecutive TypeDefs (`FieldList`/`MethodList` point to the first
  row of each run; an empty run points at the next owner's first row, or rows + 1 at the end); Param rows are runs
  owned by MethodDefs (`ParamList`). The **0xFFFF padding rule** of 3.6 applies here.
- Sorted tables, sorted at finalize: ClassLayout (Parent), Constant (Parent), CustomAttribute (Parent),
  DeclSecurity (Parent), FieldLayout (Field), FieldMarshal (Parent), FieldRVA (Field), GenericParam (Owner,
  Number), GenericParamConstraint (Owner), ImplMap (MemberForwarded), InterfaceImpl (Class, Interface),
  MethodImpl (Class), MethodSemantics (Association), NestedClass (NestedClass); keys compare as encoded coded
  indexes. GenericParam is referenced by other tables, so sorting it needs a remap.
- TypeRef, MemberRef, TypeSpec, MethodSpec, StandAloneSig, ModuleRef and AssemblyRef rows are deduplicated by
  content and may take any order; derive every order from input order (never pointer or hash order) so output is
  deterministic.
- Per-type limits (3.6): msil-ld must count methods and fields per type and spill into further holder classes
  (`<Module>` keeps the module initializer).
- Rows are limited to 2^24 per table (token row bits).

API shape (a sketch; names are proposals):

```cpp
namespace llvm::cli {
// Handles before finalize; tokens after.
struct TypeDefHandle { uint32_t Id; };  struct FieldHandle { uint32_t Id; };  ...
class MetadataBuilder {
public:
  StringRef/handles:  BlobRef addBlob(ArrayRef<uint8_t>); UserStringRef addUserString(StringRef UTF8);
  AssemblyRefHandle addAssemblyRef(StringRef Name, VersionTuple V, ArrayRef<uint8_t> KeyOrToken, uint32_t Flags);
  TypeRefHandle addTypeRef(ResolutionScope Scope, StringRef Namespace, StringRef Name);      // deduplicated
  TypeDefHandle addTypeDef(uint32_t Flags, StringRef Namespace, StringRef Name, TypeDefOrRef Extends,
                           std::optional<TypeDefHandle> EnclosingType = std::nullopt);
  FieldHandle addField(TypeDefHandle Owner, uint16_t Flags, StringRef Name, BlobRef Sig);
  MethodHandle addMethod(TypeDefHandle Owner, uint16_t Flags, uint16_t ImplFlags, StringRef Name, BlobRef Sig);
  void addParam(MethodHandle M, uint16_t Seq, uint16_t Flags, StringRef Name);
  MemberRefHandle addMemberRef(MemberRefParent Parent, StringRef Name, BlobRef Sig);      // deduplicated
  StandAloneSigHandle addStandAloneSig(BlobRef Sig);                                       // deduplicated
  ModuleRefHandle addModuleRef(StringRef Name);
  void addImplMap(MethodHandle M, uint16_t Flags, StringRef ImportName, ModuleRefHandle Scope);
  void addClassLayout(TypeDefHandle T, uint16_t Pack, uint32_t Size);
  void addFieldLayout(FieldHandle F, uint32_t Offset);
  void addFieldRVA(FieldHandle F, uint32_t MappedDataOffset);    // RVA known after PE layout
  void setMethodBody(MethodHandle M, uint32_t ILStreamOffset);  // RVA known after PE layout
  void addCustomAttribute(HasCustomAttribute Parent, CustomAttributeType Ctor, BlobRef Value);
  TypeSpecHandle addTypeSpec(BlobRef Sig);  MethodSpecHandle addMethodSpec(MethodDefOrRef M, BlobRef Inst);
  void setAssembly(StringRef Name, VersionTuple V, uint32_t HashAlg, ...); void setModule(StringRef FileName);

  Error finalize();                       // orders, pads, sorts, sizes; tokens become valid
  uint32_t getToken(EntityRef) const;     // for IL encoding after finalize
  size_t getSerializedSize() const;       // independent of RVA values (fixed-width columns)
  void serialize(raw_ostream &OS, uint32_t ILStreamRVA, uint32_t MappedDataRVA,
                 uint64_t *MvidOffset) const;
};
}
```

Because MethodDef.RVA and FieldRVA.RVA are fixed 4-byte columns, the metadata size does not depend on them: lay out
`.text` with the known sizes (CLI header, IL stream, mapped data, metadata), then serialize once with the final
RVAs. (The prototype serializes twice for simplicity and asserts that the size did not change.)

### 4.6 Signatures and blobs

Bytes the runtimes accepted in E1 [V] (they double as unit-test vectors):

| Blob | Bytes |
|---|---|
| `int32 main()` | `00 00 08` |
| `int32 square(int32)` | `00 01 08 08` |
| `int32 puts(native int)` (P/Invoke: plain DEFAULT signature; the native convention is in ImplMap flags 0x0200) | `00 01 08 18` |
| `int32 '<msil>Main'(string[])` | `00 01 08 1D 0E` |
| `void .cctor()` | `00 00 01` |
| `native int AllocHGlobal(native int)` | `00 01 18 18` |
| `instance void AssemblyMetadataAttribute::.ctor(string, string)` | `20 02 01 0E 0E` |
| field `valuetype '<msil>image_t'` (TypeDef row 2, TypeDefOrRefOrSpecEncoded = 2<<2 / 0) | `06 11 08` |
| field `native int` | `06 18` |
| calli `int32(int32)` (managed StandAloneSig) | `00 01 08 08` |
| locals `(int32, native int, int32, int32)` | `07 04 08 18 08 08` |
| custom attribute `("msil.triple", "x86_64-pc-windows-msvc-msil")` | `01 00 0B 6D 73 ... 6C 1B 78 38 ... 6C 00 00` (prolog, two SerStrings, NumNamed = 0) |
| AssemblyRef mscorlib public key token | `B7 7A 5C 56 19 34 E0 89` (8-byte blob, Flags 0) |

Still to support (from II.23.2, not exercised here): `calli unmanaged cdecl` (calling convention byte 0x01;
Design 4.5's native function pointers), 0x09 `unmanaged` with `modopt(CallConv*)` (C# function pointers; needed
for `SuppressGCTransition` on calli) [E/U], VARARG with SENTINEL (native varargs on Windows, Design 3.1 fallback),
generic instances (0x15) for TypeSpec and MethodSpec (0x0A), PTR/BYREF/PINNED locals, custom modifiers.

### 4.7 Method bodies

- Tiny header `(CodeSize << 2) | 2` when code < 64 bytes, maxstack <= 8, no locals, no EH; any alignment
  (tiny bodies at odd RVAs ran [V]).
- Fat header (12 bytes, 4-byte aligned): flags/size word `0x3 | MoreSects 0x8 | InitLocals 0x10 | (3 << 12)`,
  MaxStack u16, CodeSize u32, LocalVarSigTok u32. Never InitLocals (D13).
- EH data after the code, 4-byte aligned: small section `01, DataSize = 12n+4 (<= 255), 00 00`, clauses
  `u16 Flags, u16 TryOffset, u8 TryLength, u16 HandlerOffset, u8 HandlerLength, u32 ClassToken/FilterOffset`;
  fat section `41` + 24-bit DataSize (24n+4) and six u32 per clause. Small only if every offset <= 0xFFFF and
  every length <= 0xFF and n <= 20. Both formats ran with a filter clause [V].
- Rules the ILBuilder must enforce for Phase 3's SjLj filters (Design 4.6) [E]: nested regions list the inner
  clause first; a filter block runs from FilterOffset to HandlerOffset and ends with `endfilter`; the handler
  immediately follows; try and handler blocks are left with `leave`; `localloc` is not allowed inside a handler,
  filter or finally.
- MaxStack: for compiled functions it comes from MSILStackCheck (D20, recorded in MSILFunctionInfo); for
  synthesized bodies the ILBuilder computes it from the Pop/Push columns of `CILOpcodes.def`, the caller giving
  the arity of `call`/`calli`/`newobj`/`ret` (Varpop/Varpush). That replaces `SynthesizedBody`'s hand-written
  stack effects.
- Several MethodDefs may share one body RVA (used in 3.6) [V]; identical-body folding is possible later.

### 4.8 FieldRVA data (D12's image)

Mapped data in `.text`, 8-aligned (16 if Phase 3 wants in-place SIMD constants), after the IL stream, as Roslyn
does; one value type with `ClassLayout` (pack 1, size = image size) per blob, as Phase 1 does. Roslyn uses a
`.pack 8` type for 8-aligned element data (`__StaticArrayInitTypeSize=40_Align=8`, nested in
`<PrivateImplementationDetails>`) [V], which matters only for `RuntimeHelpers.CreateSpan`; the `cpblk` copy does
not care. The image stays read-only and is copied at startup (3.5).

### 4.9 PE envelope per triple

| Output | Machine / optional header | CorFlags | Extras |
|---|---|---|---|
| x86_64 triples | AMD64, PE32+ | ILONLY | none (no IAT, import, stub, `.reloc`) |
| aarch64 triples | ARM64, PE32+ | ILONLY | none (Roslyn arm64 has the same shape [V]; not run on ARM64 [U]) |
| i686 triples (phase 5) | I386, PE32, `32BIT_MACHINE` | ILONLY + 32BITREQUIRED | mscoree import + `FF 25 <abs32>` stub + `.reloc` HIGHLOW |
| future AnyCPU (FeatureIdeas 7) | I386, PE32 | ILONLY | as Roslyn anycpu |

Common: DOS stub (128-byte classic or lld's), linker version 14.0 (lld) or 48.0 (Roslyn), OS/subsystem 4.0,
subsystem CUI, DllCharacteristics 0x8540, section alignment 0x2000, file alignment 0x200, image base 0x140000000
for programs and 0x180000000 for libraries (PE32: 0x400000/0x10000000), stack reserve 0x400000/commit 0x4000
(PE32+); a `--stack` option would give Framework programs a bigger main thread (Status.md 5).
- **EXE vs DLL**: a program with `main` is written as an EXE image (no `IMAGE_FILE_DLL`) with the entry-point
  token in the CLI header: it runs directly on Framework and via `dotnet prog.exe` [V] (cil-runtime 3.3: also
  `dotnet` with an `.exe`); `-shared` writes a DLL with entry token 0. A DLL with an entry token (Phase 1's
  ilasm `-dll`) also runs under `dotnet` [V], but PEVerify `/md` reports 0x80131B21 for it (cil-runtime 4).
- **Strong name absent**: Assembly.PublicKey = 0, Flags 0, no STRONGNAMESIGNED, StrongNameSignature directory 0.
- Optional later: a Debug directory (CodeView entry for the Portable PDB in Phase 4, a REPRO entry for
  deterministic builds), a `.rsrc` version resource (`llvm/Object/WindowsResource.h` already builds `.rsrc` for
  lld-link [S, not used here]), and the CLI Resources directory (managed resources).

### 4.10 Determinism

Write the whole image with MVID = 0 and timestamp = 0 (and debug-directory stamps = 0), SHA-256 the bytes, set
MVID = digest[0..15] with `byte7 = (byte7 & 0x0F) | 0x40` and `byte8 = (byte8 & 0x3F) | 0x80`, timestamp =
LE32(digest[16..19]) | 0x80000000 (`BlobContentId.FromHash`, 3.10). The Phase 4 PDB id would use the same id.
No paths, times or host data in the output; module name = output file name.

### 4.11 The reader

msil-ld must read reference assemblies (P2-B binding, the `bcl-binding` recon) and managed inputs (P2-C), and the
dumper and tests need one too. It shares `CLITables.def`, the index-size logic (one `MetadataSizes`-like class
used by both sides), the coded-index tables and the signature model with the writer. Scope: PE -> CLI header (via
`COFFObjectFile`), root, streams (`#~` and `#-`), heaps, row accessors for all 45 tables, signature decoder,
custom-attribute decoder, method-body decoder (for the dumper and for linker-side relaxation, if wanted). Size
1,500-2,000 lines. Big inputs: Framework `mscorlib.dll` is 2.7 MB with 24,237 MethodDefs, so accessors must decode
lazily from the mapped file (no up-front row objects).

---

## 5. The CIL binary encoder

### 5.1 Opcode table and operand kinds

`CILOpcodes.def` (draft, 219 entries, generated from the verified table): one-byte opcodes 0x00-0xE0 with holes
(0x24, 0x77-0x78, 0xA6-0xB2, 0xBB-0xC1, 0xC4-0xC5, 0xC7-0xCF), two-byte `FE 00`-`FE 1E` (no FE08, FE10, FE1B);
0xFE is the two-byte prefix and 0xF8-0xFF are reserved prefixes [E, V by 3.7]. Operand encodings: ShortInlineVar
u8, InlineVar u16, ShortInlineI i8 (u8 for `unaligned.` 1/2/4 and `no.`), InlineI i32, InlineI8 i64,
ShortInlineR/InlineR raw IEEE bits, tokens u32, ShortInlineBrTarget i8 and InlineBrTarget i32 relative to the
**next** instruction, InlineSwitch u32 count + count x i32 relative to the **end of the whole switch**.

### 5.2 Forms

- Index and constant short forms: as `MSILInstPrinter::printShortForm` (ldarg.0-3, ldarg.s < 256, ldarg; starg.s,
  starg; ldloc/stloc .0-3/.s/long; ldc.i4.m1/.0-8/.s/long); add `ldarga.s`/`ldloca.s` if they are ever used.
- Branch relaxation: start every branch short, widen those whose displacement leaves [-128, 127], repeat until
  nothing changes (widening only grows code, so it terminates); `leave` too. Branch targets never leave a method,
  so relaxation is per method. Verified on E1's 371-byte `main` (short loops, a long branch over 200 bytes) and on
  the whole corpus (3.9).
- Prefixes (`unaligned.`, `volatile.`, `tail.`, `constrained.`, `readonly.`) are separate instructions that the
  next instruction's operand list does not include.

### 5.3 Where it lives

Recommendation:
1. **Encoding primitives and the method assembler are in the library** (`llvm::cli::ILBuilder`,
   `MethodBody`): msil-ld needs them anyway to synthesize the module initializer (merged data segments,
   relocations, constructors in priority order), `'<msil>Main'`, export forwarders and helpers, and the reader's
   decoder pairs with them.
2. **One expansion layer in the MSIL target**, `MSIL::expandToCIL(const MCInst &, SmallVectorImpl<CILInstr> &)`
   in `MCTargetDesc`, turns a record into its CIL sequence (TEE -> dup + stloc, CONST_I64 -> ldc + conv.i,
   GLOBAL_ADDR -> ldsfld [+ ldc + add], MEMMOVE -> conv.i8 + dup + call <BCL member>, TRAP, BR_TABLE_I32 ->
   switch + br, memflags -> prefixes) with the shortest index/constant forms. `MSILInstPrinter` prints that list
   (replacing `printShortForm`, `printGlobalAddrOffset` and the multi-line asm strings) and `MSILMCCodeEmitter`
   encodes it, so `-S` and objects cannot diverge. Token operands become fixups whose kind follows the
   object-format decision (the `mc-streamer`, `objfmt-*` recons).
3. **Branch relaxation**: either through MC (an `MSILAsmBackend` with `mayNeedRelaxation`/`relaxInstruction`,
   with a short/long flag operand on branch MCInsts or `_S` records, X86-style) or by keeping long branches in
   objects and letting msil-ld re-encode bodies with the ILBuilder while it patches tokens (it decodes them
   anyway). The second keeps MC simpler and gives ilasm-parity text only if the printer relaxes the same way;
   the first is the conventional LLVM shape. Both are cheap; pick with the object-format decision.
4. Method headers (tiny/fat), maxstack and EH tables are best written by whoever writes the final PE (msil-ld),
   from per-function records in the object; an object writer that already emits real method bodies (a
   metadata-shaped object format) uses the same library calls.
5. The same expansion should print the shortest forms in `-S` text too (3.9), which changes the expected text of
   many FileCheck tests (mechanical with `update_llc_test_checks.py`; user decision 1).

### 5.4 How to keep it checked

- Unit test: every `CILOpcodes.def` entry encodes to its value (one or two bytes) and operand size; values unique;
  two-byte entries all start with 0xFE.
- Lit test (`REQUIRES: ilasm`): the generated all-opcodes `.il` (`check_opcodes.py` builds it) assembled by ilasm
  and decoded by the library must give the expected opcode sequence; known ilasm deviation: float32 sNaN.
- Target unit test: every MSIL MC opcode with a `cilop` other than 0xFFFF maps to a `CILOpcodes.def` entry whose
  text equals the record's mnemonic (what `check_td_opcodes.py` did).
- Parity (`msil/test`): for each M1 program, the IL bytes of each method from `-S` + ilasm equal those from the
  binary path after token normalization (exact once the printer prints the encoder's forms).

---

## 6. Testing plan

| Level | What | Where |
|---|---|---|
| gtest | compressed ints (II.23.2 examples: 0x03 -> `03`, 0x7F -> `7F`, 0x80 -> `80 80`, 0x2E57 -> `AE 57`, 0x3FFF -> `BF FF`, 0x4000 -> `C0 00 40 00`, 0x1FFFFFFF -> `DF FF FF FF`) [E] | `llvm/unittests/CLI/BlobTest.cpp` |
| gtest | heaps: dedupe, tail merging, #US trailing byte cases, padding; index widths at 0xFFFF/0x10000 heap bytes | `HeapsTest.cpp` |
| gtest | table sizing at the boundaries: rows 0xFFFF/0x10000 (simple), 2^(16-n)-1 / 2^(16-n) (coded), the list-end padding rule | `MetadataBuilderTest.cpp` |
| gtest | sorted tables, TypeDef order with nesting, member runs, determinism of token order | same |
| gtest | signature vectors of 4.6; CA blob vector | `SignatureTest.cpp` |
| gtest | method headers (code 63/64, maxstack 8/9, locals, EH), EH small/fat (20/21 clauses, offset 0xFFFF/0x10000, length 0xFF/0x100), relaxation (+127/+128, -128/-129, switch) | `ILBuilderTest.cpp`, `MethodBodyTest.cpp` |
| gtest | content id vector: SHA-256("abc") -> `bf1678ba-018f-4acf-8141-40de5dae2223`, 0xA36103B0 | `PEWriterTest.cpp` |
| gtest | builder -> serialize -> reader round trip of every table the writer emits | `RoundTripTest.cpp` |
| lit | `llvm-cil-dump` on yaml2obj COFF inputs carrying metadata bytes (generated once from the writer or ilasm, then checked in as YAML); `COFFObjectFile`/llvm-readobj on writer output (`--file-headers`: no import table, CLR directory) | `llvm/test/tools/llvm-cil-dump/` |
| external (msil/test, `REQUIRES: dotnet`) | run on .NET 8/10 (and WSL) and Framework (exe); PEVerify `/md /il` with the allowed list (+ `VLDTR_E_FMD_GLOBALITEM` warnings); ILVerify; ildasm -> ilasm round trip with NaN normalization; parity with the `-S` + ilasm path over the M1 corpus using an SRM dumper | a C# dumper like `mdump` built the way `msil/utils/jitall/build-jitall.py` builds jitall |

The prototype and its scripts can serve as an executable specification during review: any disagreement between the
C++ library and `cliwriter.py` on the same input is a bug in one of them.

---

## 7. Size and work packages

Size estimates (C++, LLVM style; the Python prototype is 700 lines for the writer half):

| WP | Files (disjoint) | Lines (code + tests) | Depends on |
|---|---|---|---|
| A. Contracts | `BinaryFormat/CLI.h`, `CLITables.def`, `CILOpcodes.def`; `lib/CLI/CMakeLists.txt` skeleton; the two CMake one-liners; `unittests/CLI/CMakeLists.txt`, `CLIFormatTest.cpp` | 900 + 250 | none (lands first; writes declarations first, protocol 2a) |
| B. Blobs, signatures, custom attributes | `CLI/Blob.*`, `CLI/Signature.*`, `CLI/CustomAttribute.*`, their tests | 800 + 500 | A |
| C. IL builder and method bodies | `CLI/Opcodes.*`, `CLI/ILBuilder.*`, `CLI/MethodBody.*`, tests, the all-opcodes ilasm lit test | 900 + 700 | A |
| D. Heaps and MetadataBuilder | `CLI/Heaps.*`, `CLI/MetadataBuilder.*`, tests | 1,400 + 700 | A (uses B's blob writer through an interface; stub until B lands) |
| E. PE writer | `CLI/PEWriter.*`, tests | 600 + 400 | A; integrates C and D at the end |
| F. Reader and dumper | `CLI/MetadataReader.*`, `CLI/Dump.*`, `tools/llvm-cil-dump/`, lit tests with yaml2obj inputs | 2,000 + 800 | A (+ B's decoder) |
| G. External oracle tooling | `msil/utils/mdump/` (SRM dumper, built like jitall), parity and PEVerify scripts, msil/test lit wiring | 700 | none (C# only); uses E/F outputs |
| H. MSIL target integration | `expandToCIL` shared by `MSILInstPrinter` and a new `MSILMCCodeEmitter`; structured BCL member references; `SynthesizedBody` replaced by the ILBuilder; test updates | 1,200 + test churn | C, and the P2-A object-format decision |

Total about 8,500-9,500 lines with tests, comparable to the DXContainer stack (7,799). Order: A alone; then B, C, D,
F, G in parallel (5 agents, disjoint files); then E; then H inside P2-A. Build cost: everything is new files plus
two CMake list lines, so a milestone rebuilds only the new library, its tests and the tools that link it, plus a
CMake reconfigure; no broad rebuild. H touches MSIL target files only.

---

## 8. Risks and open questions

1. **Per-type limits** (65,524 methods, about 65,535 fields) are new constraints on D12/M9's "everything on
   `<Module>`" layout once msil-ld links whole programs: spill classes, or fewer fields (globals addressed as
   segment base + link-time offset need no per-global field; only exported data needs accessors). The object
   format and binding recons decide which.
2. **The 0xFFFF list-end edge** exists in Roslyn's own writer; ours must pad (cheap, but easy to forget; give it a
   unit test).
3. **float32 sNaN constants** cannot be made exact by the writer alone (3.4).
4. **Name lengths**: lifting ilasm's 1023-byte limit is possible for the runtimes but PEVerify `/md` objects;
   keep the limit while PEVerify is a gate leg, or treat `VLDTR_E_TD_NAMETOOLONG` as allowed (user decision 2).
5. **Text/binary drift** if the printer and the encoder keep separate expansion code; mitigated by section 5.3's
   single expansion and the parity test.
6. **ilasm as oracle** has quirks that tests must normalize: fat headers, InitLocals, `A_n` Param rows, token
   order, sorted mask, sNaN quieting, mscoree stub.
7. Not verified here [U]: ARM64 execution; PE32 on 32-bit Windows; Mono/Unity (Design 7.2) acceptance of the
   Roslyn-style envelope without the mscoree stub; float64 sNaN JIT behaviour; type-name length limits; the
   `0x09 unmanaged` calling convention on Framework (it is a .NET 5+ addition [U]).

---

## 9. FeatureIdeas.md interactions

Nothing proposed here precludes an idea, provided the library stays general:
- 1 (class placement) and 7 (several triples per assembly): methods and fields on arbitrary TypeDefs with
  namespaces and nesting, several type initializers, machine/CorFlags independent of the triple (AnyCPU PE32 for
  i686 + x86_64 variants). The spill classes of 3.6 are the same mechanism.
- 3 (typed pointers), 4 (managed memory, refs, Span), 5 (stackalloc/Span), 6 (instance model): a general signature
  model (PTR, BYREF, value types, generic instances, HASTHIS) and the Param, TypeSpec, MethodSpec and
  CustomAttribute tables; Event/Property/InterfaceImpl can be added when needed.
- Choosing ilasm's always-long branch forms in text would not preclude anything, but it would cost the IL size
  of 3.9.

---

## 10. Supporting files and reproduction

`llvm/docs/MSIL/recon/p2/metadata-writer-files/`:

| File | What |
|---|---|
| `py/cliwriter.py` | the prototype writer (heaps, tables, signatures, IL assembler, bodies, PE envelope, deterministic id) |
| `py/cilopcodes.py` | ECMA-335 III opcode table |
| `py/hello.py` | the E1 program and its variants (`--exe --stub --pe32 --req32 --pref32 --sdata --rvawrite --fat-eh --snan-probe --many-fields --split-fields --many-methods --longlen --os linux`) |
| `py/gen_hello_il.py` | the same module as ILAsm text (E2) |
| `py/check_opcodes.py`, `py/check_td_opcodes.py` | E7 |
| `py/verify_schema.py` | E8 |
| `py/relax_measure.py`, `py/summarize.py` | E9 and section 2 |
| `py/gen_opcodes_def.py`, `py/gen_tables_def.py` | generate the two `.def` drafts |
| `CILOpcodes.def`, `CLITables.def` | drafts for `llvm/include/llvm/BinaryFormat/` |
| `dotnet-opcodes.tsv` | System.Reflection.Emit.OpCodes and ILOpCode dump |
| `mdump/Program.cs`, `mdump/mdump.csproj` | C# dumper (System.Reflection.Metadata, .NET 8): `mdump <pe> [--no-pe] [--no-bodies]`, `--opcodes`, `--contentid <hex>`, `--srm-fields <n> <out>` (Roslyn's MetadataBuilder at the 65,535 boundary) |
| `run_all.sh`, `run-linux.sh` | reproduce the runtime matrix (Windows and WSL) |
| `runs.log`, `check_opcodes.log`, `check_td_opcodes.log`, `verify_schema.log`, `relax_measure.log`, `ilasm_measure.log` | outputs |
| `hello-python.mdump.txt`, `hello-ilasm.mdump.txt`, `hello-python-vs-ilasm.diff` | E2 |

Reproduce (Git Bash, from a scratch copy of the files folder laid out as `py/`, `tools/mdump/`):

```text
cd <scratch>/tools/mdump && dotnet build -c Release -o ../bin/mdump
python <scratch>/py/hello.py -o hello.dll && dotnet exec --runtimeconfig rc8.json hello.dll   # exit 63
bash <scratch>/run_all.sh <scratch>
python <scratch>/py/check_opcodes.py dotnet-opcodes.tsv <work> tools/bin/mdump/mdump.dll <Framework ilasm> <modern ilasm>
python <scratch>/py/check_td_opcodes.py D:/llvm-msil/llvm/lib/Target/MSIL
```

Installs: none. Tools used: Framework ilasm/ildasm 4.8, NuGet-cache ilasm/ildasm 10.0.12, PEVerify 4.8, ilverify
10.0.12, csc from SDK 10.0.401, dumpbin 14.51, VS-bundled llvm-readobj 22.1.3, `build\bin\clang.exe` of this
checkout, Python 3.14, .NET SDK 8 for the dumper.
