# Judge report: Phase 2 object format and binding, lens "implementation cost, risk and LLVM reuse" (key `judge-cost-risk`)

- Date: 2026-10-21. Checkout: branch `msil` at `1ea64fe9fc7c` (LLVM 24.0.0git, upstream merged 2026-10-19).
- Role: independent judge of the P2-A object-format and binding decision. Lens: lines and files to write, LLVM
  infrastructure reused, widely-included header edits and rebuilds on this 4C/8T host, how the work splits into
  small milestones with disjoint work packages, how testable each step is, and the risk of finding a dead end
  late. Preference: whatever gets a two-object program running soonest without painting later phases into a
  corner.
- Inputs read in full: [objfmt-container.md](objfmt-container.md), [objfmt-binding.md](objfmt-binding.md); the
  parts of [mc-streamer.md](mc-streamer.md), [metadata-writer.md](metadata-writer.md), [lld-flavor.md](lld-flavor.md),
  [driver-toolchain.md](driver-toolchain.md) and [tests-p2.md](tests-p2.md) that bear on cost and risk;
  Design.md (all), ImplementationPlan.md sections 1-5, 7 and the section 12 scope rows of 2026-10-14/15,
  FeatureIdeas.md, and the orchestrator's Phase-2 inputs file.
- Method: re-ran the experiments my verdict depends on (J1-J3), added four of my own (J4-J7), and checked every
  source line cited below in this checkout. Marks: **[V]** verified here, **[S]** read in source here,
  **[R]** taken from a recon report without re-running it, *est.* my estimate.
- Scratch: `C:\llvm-msil-scratch\p2\judge-cost-risk` (about 15 MB). Supporting files (32 KB, no binaries):
  [judge-cost-risk-files/](judge-cost-risk-files/). No tracked file was modified; no cmake or ninja ran (the
  ninja deps log was parsed read-only by a Python script); nothing was installed. D: had 17 GB free.

---

## 0. Verdict in brief

| Rank | Candidate (container + binding) | Score | One line |
|---|---|---|---|
| 1 | **B2: ECMA-335 netmodule objects + symbolic name binding + versioned link-info record** | **8** | Least format-specific code (est. 2.6-2.8k lines), zero required upstream hooks or widely-included header edits, one reader/writer library shared by objects, outputs, reference assemblies and P2-C inputs, independent payload oracles (ildasm, SRM, ilasm-built objects), no payload-format growth in P3/P4. Costs: LLVM object tools are blind to the objects; the link-info record is home-grown. |
| 2 | D1: ELF ET_REL (route D-ii, own writer) + token relocations + payload side sections | 6 | Best LLVM tool reuse (nm, ar index, objdump -r/-t, readobj, objcopy, yaml2obj) and standard C link semantics, for est. 3.4-4k lines and 6-9 low-conflict upstream hook files; but the CIL payload (signatures with value types, BCL members, later delegate types, custom attributes, generic instances) needs a private side format that grows every phase and converges on embedded metadata. |
| 3 | E3: bitcode objects + LTO only, as the *only* format | 4 | Zero container cost and already works; the right bring-up and the permanent `-flto` path, but as the sole format it puts whole-program codegen in every link and rules out ThinLTO and LTO partitions. |
| 4 | D+B: ELF (or COFF) container with an embedded ECMA-335 metadata section and token relocations (the MSVC `/clr` shape) | 4 | Gets both tool sets by paying for both writers plus two symbol spaces kept consistent; MSVC needed it for mixed native/managed objects, which an IL-only design does not have. |
| 5 | A1: wasm-style custom container + relocations (Design.md 5.1 as written) | 2 | Highest cost (est. 7-8k lines, about 30 shared hook sites) and no tool interoperability gain over D1. |
| 6 | B4: one assembly per object, forwarder facade + startup driver (no merge) | 2 | Zero object work, but not a static linker: a public class per symbol, non-`initonly` address fields, an explicit init driver and about 1 ms of startup per object. Relevant to P2-C at most. |

**Recommendation.** Stage it: **E3 now** (P2B-1: msil-ld links bitcode through LTO; `--lto-emit-asm` plus ilasm is
the interim assembler and oracle; keep it permanently for `-flto`), then **B2 for CIL objects**, with the module
initializer and `'<msil>Main'` synthesized by msil-ld from per-object data images and data-relocation tables, not
from per-object init methods (those fail D21 verification, J4). Keep **D1 as the documented fallback**: the MC
side (one MCInst per CIL instruction, code emitter, fixups, streamer) is identical for B2 and D1, so a later switch
costs only the object writer, lld's `ObjFile` and 6-9 ELF hook files.

---

## 1. What I re-checked, and where the recon reports need correcting

Full log: [judge-cost-risk-files/run-all.log](judge-cost-risk-files/run-all.log), produced by
[run-all.sh](judge-cost-risk-files/run-all.sh) in about one minute.

| # | Experiment | Result | Bears on |
|---|---|---|---|
| J1 | objfmt-container 6.2 re-run: CIL bodies in `.cil.text.*` ELF sections (x86_64 carrier assembled by this tree's clang), `e_machine` patched to 0xC11 and to EM_NONE, read by the VS-bundled LLVM 22.1.3 tools | [V] `llvm-readobj`: "elf64-unknown", "Machine: 0xC11"; `llvm-nm` lists `C common_var`, `D counter`, `U ext`, `t helper`, `T hid`, `W inl`, `w weak_undef`; `llvm-ar` builds a 9-entry armap over two objects; `llvm-objdump -r` lists the token and data relocations (type `Unknown`); `llvm-readobj --section-groups` shows the COMDAT group with its SHF_LINK_ORDER side section; `ld.lld -r`: "unsupported e_machine value: 3089" | D1's tool reuse is real |
| J2 | objfmt-binding E2 re-run: the two-object "zoo" (weak, weak undefined, COMDAT, common, alias, cross-object constructor priorities, function and data pointers across objects) as ilasm-built netmodule objects, linked by the 726-line C# prototype | [V] 37 token sites rewritten, constructors `ctor_b(100) ctor_a(200)`, prints `main` and `add3`, exit 63 on .NET Framework 4.8 (`.exe`) and on .NET 8. `llvm-nm`: "no symbols"; `llvm-ar` armap empty; `llvm-objdump -d` disassembles the IL as x86-64 (`xorb %dh,(%rcx)`) because the PE header says AMD64; `lld-link` and `ld.lld`: "unknown file type" | B2 works end to end; its tool gap is real but does not affect linking |
| J3 | Option E today | [V] `clang -c` for `x86_64-pc-windows-msvc-msil`: `err_drv_msil_no_object_output`; `-c -flto` writes bitcode (`42 43 c0 de`); a cc1 `-mlink-bitcode-file` link plus ilasm runs on Framework 4.8 (exit 42) | E3 bring-up costs nothing in the container |
| J4 | **New.** An `initonly` address field stored by a per-object init method that `.cctor` calls (mc-streamer 5.9, model 2) versus stored in `.cctor` itself ([perobj.il](judge-cost-risk-files/perobj.il), [cctor.il](judge-cost-risk-files/cctor.il)) | [V] both run (exit 1 = check passed) on Framework 4.8 and .NET 8; PEVerify 4.8 and ILVerify 10.0.12 both report "Cannot change initonly field outside its .ctor" (`[InitOnly]`) for the init-method variant and verify the `.cctor` variant cleanly. `InitOnly` is not in `msil/utils/ilverify-allowed.txt` (4 codes: Unverifiable, InitLocals, StackByRef, UnmanagedPointer), so model 2 fails every D21 gate unless the address fields stop being `initonly` | Data model: linker-synthesized initializer |
| J5 | **New.** Metadata tables in a Phase-1 object versus a hand-written Phase-3/4-shaped object ([p3shape.il](judge-cost-risk-files/p3shape.il): by-value P/Invoke struct (Design 2.3), Framework delegate thunk type (4.5), `[ThreadStatic]` shadow-stack field (4.2), `UnmanagedCallersOnly` thunk (4.5), setjmp filter plus typed catch (4.6), `Vector128<float32>` and a generic method (3.2)), counted by a 30-line SRM tool ([tblcount/](judge-cost-risk-files/tblcount/)) | [V] zoo `a.o`: 12 tables (Module, TypeRef, TypeDef, Field, MethodDef, Param, MemberRef, StandAloneSig, ModuleRef, ImplMap, AssemblyRef, ManifestResource). The P3/P4 shape: 16 tables, adding **CustomAttribute, ClassLayout, FieldLayout, TypeSpec and MethodSpec**, plus TypeDefs with runtime-managed methods, 12 TypeRefs, 7 MemberRefs and a TypeDefOrRef coded index inside a P/Invoke signature blob | What D1's side sections must re-encode over time |
| J6 | **New.** Header dependents in the shared build from a read-only parse of `.ninja_deps` (format v4) and `.ninja_log` (v7) ([deps.py](judge-cost-risk-files/deps.py)): 3,218 objects, 3.17 CPU-hours of compile | [V] `BinaryFormat/ELF.h` 51 objects, **2.4%** of compile CPU; `BinaryFormat/COFF.h` 58, 1.7%; `Object/ELFObjectFile.h` 15, 0.5%; `Object/ELF.h` 16, 0.6%; `Magic.h` 147, 6.0%; `Object/Binary.h` 158, 6.5%; `ObjectFile.h` 135, 5.3%; `SymbolicFile.h` 145, 6.0%; `MCSectionMSIL.h` 3, 0.1%; `MCContext.h` 609, 19.3%; `MCSymbol.h` 607, 19.0%; `MCObjectFileInfo.h` 104, 3.9%; `Triple.h` 2,104, 81.9% | Rebuild cost per candidate |
| J7 | **New.** Upstream churn of the hook files each candidate adds (commits since 2024-10-21 / since 2025-10-21) | [V] `ELF.h` 49/20, `ELFObjectFile.h` 11/4, `Object/ELF.cpp` 24/10, `ELFYAML.cpp` 38/14, `ELFDumper.cpp` 71/38; but commits that changed `EM_*` constant lines in `ELF.h`: **1** in two years; `case ELF::EM_` lines in `ELFObjectFile.h`: 1; `ECase(EM_` in `ELFYAML.cpp`: 1; `EM_` lines in `Object/ELF.cpp`: 2, in `ELFDumper.cpp`: 13. B2's optional hooks: `SymbolicFile.cpp` 2/1, `ObjectFile.cpp` 5/2, `Binary.h` 3/1 | Merge-conflict surface |

Corrections and cross-report conflicts:

1. **ELF.h is not a broad rebuild.** objfmt-container (its header-change list) says `BinaryFormat/ELF.h` is "directly
   included by 163 files ... a broad rebuild". In the tree that is actually built, an ELF.h edit recompiles 51
   objects, 2.4% of compile CPU, about 1-2 minutes of the 47-minute full rebuild (J6). mc-streamer's figure (ELF.h
   plus COFF.h = 92 objects, 3.6%) agrees. Once lld and the ELF tools join the gate build set, an ELF.h edit also
   recompiles lld/ELF (41 TUs, 229 s of CPU [R lld-flavor 5]) and llvm-readobj/llvm-objdump (24 TUs): est. 1-2 more
   minutes. Landing EM_CIL in the milestone that first builds those tools makes it a one-time cost.
2. **D1's upstream hooks are low-conflict.** The files churn, but the `EM_*` switch regions EM_CIL touches changed
   about once in two years (J7). The merge cost is real but small; it is not a reason to reject D1.
3. **mc-streamer's recommended data model (R5 and 5.9 model 2: per-object `<msil>init_data`/`<msil>init_relocs`
   methods) fails D21.** Storing an `initonly` address field outside `.cctor` is an `[InitOnly]` error in ILVerify
   and PEVerify (J4); objfmt-container found the same with `netmerge` (its 5.1). Model 2 works only if address
   fields drop `initonly` (the JIT-folding benefit is unverified, so the loss is unknown) or if msil-ld splices
   the bodies into one `.cctor`. objfmt-binding's prototype (a linker-generated `.cctor` from a data image and a
   relocation table, wasm-ld's model) verifies cleanly (objfmt-binding 4.5 [R]). The planners should pick model 1;
   the choice is independent of the container.
4. **lld needs neither an archive index nor an `llvm::object` reader for its own objects.** lld/ELF
   (`lld/ELF/Driver.cpp:2165`, "Scan all archive members rather than using the archive symbol index") and lld/wasm
   (`lld/wasm/Driver.cpp:279-305`, `getArchiveMembers` at `:246`) read every member themselves [S]. lld-flavor's R1
   ("a reader in llvm/lib/Object so that lld, llvm-ar's index, llvm-nm and llvm-objdump work") is a tooling wish,
   not a linker requirement; for B2 the lib/CLI `MetadataReader`, needed for reference assemblies anyway, is enough.
5. **objfmt-binding overstates option 1's cost when the container is ELF**: with D1, llvm-nm, llvm-ar and
   llvm-objdump support come free (J1). Only option A pays for them.
6. **B objects are not harmless to every LLVM tool**: `llvm-objdump -d` disassembles their IL as x86-64 (J2), as it
   already does for Phase-1 ilasm outputs. Cosmetic, worth a sentence in GettingStarted.md.

---

## 2. Pruning the container x binding matrix

Containers: A wasm-style custom, B ECMA-335 module (netmodule) envelope, C pure netmodules, D ELF/COFF
relocatable, E bitcode. Bindings: 1 relocations/token patching, 2 symbolic name-bound references, 3 LTO only,
4 no merge, 5 GOT-like table.

| | 1 relocations | 2 symbolic | 3 LTO only | 4 no merge | 5 GOT |
|---|---|---|---|---|---|
| A | **A1 (scored)** | needs names and signatures, i.e. metadata, so A grows into B: pruned | n/a | n/a | pruned |
| B | redundant: IL is self-describing and a decoder finds every token (objfmt-container 5.3): pruned | **B2 (scored)** | n/a | **B4 (scored)**, with assembly-shaped objects | pruned |
| C | as B | B2 without link info: no weak, COMDAT, common, constructor priorities or data relocations. Folded into B2 as its prototype step | n/a | as B4 | pruned |
| D-ELF | **D1 (scored)** | needs metadata inside the object: **D+B (scored)** | n/a | n/a | pruned |
| D-COFF | dominated by D-ELF: machine CEE (0xC0EE) is rejected by every LLVM tool and by link.exe/lib.exe (LNK1136); machine 0 makes link.exe and lld-link assume x64 and link CIL as code; no hidden visibility (objfmt-container 6.2, 6.8 [R]): pruned | | | | |
| E | n/a | n/a | **E3 (scored)** | n/a | n/a |

Binding 5 (GOT) is pruned everywhere: `calli` through a slot costs 3-5 times an inlined call (objfmt-binding E12
[R]), buys nothing inside one merged module (tokens are rewritten anyway), and Phase 1's `<msil>fn_*` fields are
already the per-function slots that function pointers need.

---

## 3. Work every CIL-object option shares (excluded from the comparison)

Needed whether objects are A, B or D, and mostly for E too. Sizes from the recon reports.

| Shared piece | Size | Source |
|---|---|---|
| ECMA-335 library (`BinaryFormat/CLI.h` and its `.def` files; `lib/CLI`: blobs, signatures, IL builder, heaps, MetadataBuilder, PEWriter, MetadataReader, dumper) | 8,500-9,500 lines including tests, 8 WPs, no widely-included header | metadata-writer 7 [R] |
| One MCInst per CIL instruction (expansion moved from `MSILInstPrinter` into `MSILMCInstLower`; `-S` unchanged) | about 400 lines changed, target-only rebuild | mc-streamer R1 [R] |
| `MSILMCCodeEmitter`, `MSILAsmBackend`, fixup kinds, `MCSymbolMSIL`, `MCMSILStreamer`, the `Triple::MSIL` cases in `TargetRegistry.cpp:93-96` and `MCAsmBackend.cpp:60-61` [S] | about 900-1,200 lines; lib/MC `.cpp` hooks only (`MCSymbol.h:45-49` has no format kind [S]; `createSymbolImpl` is in `MCContext.cpp:285` [S]) | mc-streamer 5, 6.2 [R] |
| Structured BCL operands; `SynthesizedBody` replaced by the IL builder | about 1,200 lines plus test churn | metadata-writer WP H [R] |
| lld/MSIL core (driver, symbol table, LTO, mark-live, map, writer skeleton, native libraries, BCL binder) | 6,000-7,000 lines; the P2B-1 subset about 2,500 | lld-flavor 6.1 [R] |
| Linker-synthesized `.cctor` (with a table-driven relocation loop) and `'<msil>Main'`; the `-S` one-object trailer built by the same code | port of `MSILAsmPrinter::emitModuleInitializer` (`MSILAsmPrinter.cpp:848`) and `emitEntryPoint` (`:1007`) [S] | lld-flavor 4.2, mc-streamer 5.10 [R] |
| Backend gaps: GlobalAlias lowering, `extern_weak` as weak undefined | small (backlog milestone M12) | backlog, objfmt-binding 6 [R] |

The container decision therefore moves a few thousand lines. It does not move the dominant cost, the ECMA-335
library, which msil-ld needs for its output and for reference binding in every option. This agrees with
objfmt-container's cross-cutting finding.

---

## 4. Format-specific cost per candidate

C++ lines without tests, *est.* unless marked. "Hook files" are upstream files the fork carries through every merge.

| | B2 netmodule + symbolic | D1 ELF (D-ii) + relocations | A1 wasm-style + relocations | D+B ELF + embedded metadata | E3 LTO only | B4 no merge |
|---|---|---|---|---|---|---|
| Object writer | 1,000-1,200: map `MCSymbolMSIL`s, sections and fixups to rows of the shared `MetadataBuilder` (provisional object-local tokens, as `WasmObjectWriter` applies provisional indices), netmodule through the shared `PEWriter` | 1,000 ELF serializer (compare `ELFObjectWriter.cpp`, 1,421 lines [V]) + 150 fixup-to-relocation mapping | 1,500 (compare `WasmObjectWriter.cpp`, 1,990 [V]) | 1,100 + 1,000 + 800 to keep CLR-token symbols and metadata rows consistent | 0 | 0 |
| C link semantics | link-info record (versioned: symbols, COMDATs, data image, data relocations, constructors, aliases, later TLS): 700 including encoder and decoder, modeled on wasm's `linking` subsections | free: ELF binding, visibility, SHT_GROUP, SHN_COMMON, STT_TLS, `.init_array.N` (J1) | part of the format | free (ELF) | IR linker | facade and driver |
| Payload beyond IL bytes (signatures, locals, value types, BCL members, P/Invoke module and entry; later delegate types, custom attributes, TypeSpec/MethodSpec, EH class tokens) | **0**: ordinary rows of the shared writer and reader (J5) | 600 writer + 600 lld reader for private side sections now; **+1,000-2,000 by P4** as J5's tables arrive, unless msil-ld synthesizes them | as D1 | 0 | n/a | 0 |
| Object reader | 0: the lib/CLI `MetadataReader` (needed for references) | 0: `llvm::object::ELFObjectFile` | 1,800 (compare `WasmObjectFile.cpp`, 2,376 [V]) | 0 | 0 | 0 |
| lld `ObjFile` and token/relocation processing | 900: rows to symbols, link info, an IL token walker over the shared opcode table (the C# prototype's object-specific part is about 340 lines on SRM, `Linker.cs` lines 1-138 and 606-707 [V]) | 1,000 (compare lld/wasm `ObjFile::parse` and `Relocations.cpp` [R lld-flavor 2.2]) | 1,200 | 1,400 | in P2B-1 (shared) | 500-1,000 facade and driver generator (prototype: 237 lines of Python [R]) |
| Object tools | ildasm and SRM read objects today; `llvm-cil-dump` (shared, metadata-writer WP F) reads them; optional `SymbolicFile` for llvm-nm and the llvm-ar index: 400 | free (J1); optional CIL disassembler for `objdump -d`: 600 | ObjectYAML, obj2yaml, yaml2obj, readobj, objdump, nm: about 2,000 (wasm: 1,410 + 442 + dumpers [R]) | free | `llvm-dis` | n/a |
| **Total format-specific** | **2,600-2,800** (+400 optional) | **3,350-3,950** now, +1-2k by P4 | **7,000-8,000** | **4,300-5,300** | **0** | **0.5-1k plus a codegen model change** |
| Upstream hook files | **0** required (optional `SymbolicFile`: `ObjectFile.cpp`, `SymbolicFile.cpp`, perhaps `Binary.h`) | 6-9 (BPF precedent `cfb51f54ba80`: 8 files, +33 lines [V]); low conflict (J7) | about 30 (wasm: 29 files [R]) | 6-9 | 0 | 0 |
| Widely-included header rebuild (J6) | none (optional `Binary.h`: 158 objects, 6.5%, about 3 min) | `ELF.h`: 51 objects, 2.4%, about 1-2 min (+1-2 min once lld/ELF and the ELF tools are built). Section groups, flags and link order must go through setters in `MCSectionMSIL.h` (3 objects), not through a new `MCContext::getMSILSection` signature (`MCContext.h:663`: 609 objects, 19.3%, about 10 min) | `Magic.h`/`Binary.h`/`ObjectFile.h`: about 160 objects, 6.5%, about 3 min | as D1 | none | none |
| Payload oracle independent of our code | ildasm, SRM (`mdcanon`), ilasm-assembled object-form text (J2) | none (llvm-readobj checks the ELF structure only) | none | ildasm on an extracted section (*est.*) | `llvm-dis` | ildasm |

---

## 5. Risk register: what could turn into a dead end late

### B2 (netmodule + symbolic + link info)

- **R-B1 coupling to the ECMA-335 library.** The object writer needs `MetadataBuilder` and `PEWriter` (metadata-writer
  WPs D and E). Mitigation: those WPs are on msil-ld's critical path anyway, because no option writes a PE without
  them; land the library contracts (WP A) first and do the MC refactor and the code emitter in parallel. If the
  library slips, P2-A's writer slips by the same amount as msil-ld's output; nothing that D1 would have let run
  earlier runs without that library either.
- **R-B2 home-grown link-info record.** Weak, COMDAT, common, aliases, the data image, data relocations and
  constructor priorities are prototyped (the zoo and the 93/93 corpus run, J2 [R objfmt-binding 4.4]); TLS, DIFF
  relocations across objects, COMDAT selection kinds other than "any", `__start_`/`__stop_` symbols and
  `--gc-sections` are not (objfmt-binding 12 [R]). wasm's `linking` section already encodes all of them (`Wasm.h`
  symbol and segment flags [R]), so this is design effort, not a feasibility risk. Version the record and put the
  triple in its header from day one.
- **R-B3 blind LLVM tools.** `llvm-nm` prints "no symbols", the `llvm-ar` index is empty and `llvm-objdump -d`
  misreads the IL (J2). Linking is unaffected (section 1, item 4). An MSIL `SymbolicFile` fixes `nm` and the index
  later if a user needs them.
- **R-B4 object validity is self-defined.** No verifier accepts objects (ILVerify and PEVerify refuse modules [R
  objfmt-container 4.1]). ildasm and SRM still parse them, which catches most writer bugs; outputs stay under D21.
- **R-B5 the merge must handle every table an object may contain.** Today 10-12 tables; the P3/P4 shape uses 16
  (J5). The same row-copy-and-remap code serves P2-C managed inputs and, at the end, C# netmodules, so this growth
  is shared work rather than B2-only work.
- Minor: duplicate `privatescope` names are renamed by an ildasm/ilasm round trip (cosmetic); the per-type limit of
  65,524 methods (metadata-writer 0, item 8 [R]) matters inside one object only for a TU with more functions than
  that.

### D1 (ELF + token relocations)

- **R-D1 payload side-format growth (the main late risk).** ELF carries C linkage well but nothing of CIL beyond the
  bytes. Every metadata kind codegen emits needs a private encoding, a relocation kind and an msil-ld decoder:
  signatures with value types (P3, Design 2.3; signature blobs hold compressed coded indices that cannot be
  patched in place [R objfmt-binding 3.1]), BCL members and types (P2-B), delegate types and custom attributes (P3
  thunks, `[ThreadStatic]`, `UnmanagedCallersOnly`), EH class tokens (P3 setjmp), TypeSpec and MethodSpec (P4 SIMD).
  J5 counts five new tables for that. The end state is MSVC's: a real metadata blob in a section (D+B), that is,
  B2's writer on top of D1's.
- **R-D2 fork-reserved `e_machine`.** It can collide with a future official value; `.note.msil` must be the real
  identification (objfmt-container 12 [R]).
- **R-D3 two symbol spaces.** ELF symbols drive resolution while metadata entities (BCL types and members,
  signatures, local types) need their own naming inside side sections; consistency bugs show up only at link time.
- **R-D4 two input readers in lld:** ELF for objects, ECMA-335 for references and P2-C managed inputs.
- **R-D5 header discipline.** Section groups and link order must reach `MCSectionMSIL` through setters, not through
  `MCContext::getMSILSection(StringRef, SectionKind)` (`MCContext.h:663`, `MCContext.cpp:961-972` [S]), or the
  edit costs about 10 minutes of rebuild (J6).

### A1, D+B, E3 as the sole format, B4

- **A1:** the costs of D1 plus a format, a reader, YAML support and dumpers of our own and about 30 hook sites; the
  same R-D1.
- **D+B:** both writers and two symbol spaces (MSVC's CLR-token symbols with TOKEN_DEF auxiliary records,
  objfmt-container 6.1 [R]); lld-link already rejects that shape ("should not refer to special section 0"), so the
  linker side gains no reuse.
- **E3 as the sole format:** whole-program codegen in every link (24.8 s for merge plus O2 codegen and 54.1 s with the
  full O2 pipeline for a 52-KLOC program, single-threaded [R objfmt-container 7]); ThinLTO and `--lto-partitions`
  produce one IL module per task that must be merged, which needs an object format (ThinLTO inputs fail today [R
  lld-flavor 0]); bitcode libraries are tied to the same or a newer LLVM and lose debug info on upgrade
  (`llvm/docs/DeveloperPolicy.md` [R]); Design.md 1 goal 3 ("at the CIL-object level otherwise") stays unmet.
- **B4:** changes the whole-program codegen model (a public class per global symbol, non-`initonly` address fields,
  an explicit two-phase init driver), costs about 0.9 ms (.NET 8) or 1.1 ms (Framework) of startup per object [R
  objfmt-binding E11], and lazy type initializers cannot honour constructor priorities. exports-multiasm recommends
  a different P2-C shape anyway (export class, accessors, two-phase initialization).

### Common to every option

- **Initializer model.** Per-object init methods fail D21 (J4). Use the linker-synthesized `.cctor` built from data
  images and relocation tables; it also keeps FeatureIdeas 6 and 7 open (per-instance and per-variant
  initializers).
- **`.cctor` size.** Straight-line relocation code grows with the program; use a relocation table and a loop
  (lld-flavor 4.2 [R]).
- The 0xFFFF member-list padding and the per-type method and field limits (metadata-writer [R]) apply to msil-ld's
  output in every option.

---

## 6. Schedule: milestones, critical path, work packages

The fastest runnable two-TU program comes from E3's P2B-1, whatever the container: the lld-flavor prototype (701
lines) already links bitcode, archives and DLL-exported native functions, and passes 93/93 of msil/test/M1 through
`--lto-emit-asm` plus ilasm [R lld-flavor 6.2]. A program from two **`clang -c` CIL objects without ilasm** needs,
in every CIL option, the ECMA-335 writer (output), the MC object writer (objects, and LTO output once LTO emits
objects) and lld's object input. The critical path below has the same length for B2 and D1; only the writer and
`ObjFile` boxes differ.

```text
P2-0  front-loaded hooks (container-independent for B2): lib/MC .cpp hooks and new MC headers, lld flavor
      registration stub, clang -c diagnostic text; optionally clang Attr.td import attributes (backlog; the only
      clang-wide rebuild). D1 adds here: ELF.h EM_CIL, ELFRelocs/MSIL.def, ELFObjectFile.h, Object/ELF.cpp,
      ELFYAML.cpp, ELFDumper.cpp (+1-2 min).
M12   backlog fixes (alias lowering, extern_weak, ...), in parallel with P2-0/P2B-1.
P2B-1 msil-ld on bitcode (E3): 9 disjoint WPs [R lld-flavor 6.2].    -> two-TU programs run (ilasm assembles)
L1    lib/CLI contracts (metadata-writer WP A), in parallel with P2B-1.
L2    lib/CLI WPs B, C, D, F, G in parallel  ||  P2-A1: one MCInst per CIL instruction, MSILMCCodeEmitter,
      MSILAsmBackend (llc -show-mc-encoding tests; -S byte-identical).
L3    lib/CLI WP E (PEWriter)  ||  link-info record contract (BinaryFormat header + encoder/decoder; B2 only).
P2-A2 MSILObjectWriter (B2: rows over lib/CLI; D1: ELF serializer + side sections), object-mode AsmPrinter
      (link info from MSILDataSegment instead of .cctor), clang -c enabled (Clang.cpp:5540-5543 [S]).
P2-B2 lld ObjFile + Writer (layout, token assignment, synthesized .cctor/Main, PE); LTO switches to object
      output.                                                       -> two clang -c objects run, no ilasm
```

Work-package split for B2 (disjoint files; every WP is target-local, lld-local or a new lib/CLI file):

| WP | Files | Depends on |
|---|---|---|
| A1 MC refactor | `MSILMCInstLower.{h,cpp}`, `MSILInstPrinter.{h,cpp}`, `MSILInstr*.td`, `MCTargetDesc/MSILMCExpr.{h,cpp}` | none |
| A2 encoder | new `MCTargetDesc/MSILMCCodeEmitter.cpp`, `MSILAsmBackend.cpp`, `MSILFixupKinds.h`; `MSILMCTargetDesc.cpp` | A1's records |
| A3 object-mode printer | `MSILAsmPrinter.{h,cpp}` (link info from `MSILDataSegment`), `MSILTargetObjectFile.{h,cpp}` (one section per method), `MSILTargetMachine.cpp` (`createMCStreamer`, `MSILTargetMachine.cpp:210-217` [S]) | P2-0 |
| A4 writer | new `MCTargetDesc/MSILObjectWriter.cpp` | lib/CLI D and E; link-info contract |
| LI link-info format | new `llvm/include/llvm/BinaryFormat/MSILLink.h` (and a `.def`), `llvm/lib/CLI/LinkInfo.cpp`, gtests | lib/CLI A |
| B1 object input | `lld/MSIL/InputFiles.cpp` (`ObjFile`), `InputChunks.{h,cpp}` | lib/CLI F, LI |
| B2 layout and synthesis | `lld/MSIL/OutputSegment.{h,cpp}`, `Synthetic.{h,cpp}` | lib/CLI C |
| B3 liveness | `lld/MSIL/Relocations.{h,cpp}`, `MarkLive.{h,cpp}` | B1 |
| B4 writer | `lld/MSIL/Writer.{h,cpp}` | lib/CLI E |
| T tests (separate agents) | `llvm/test/CodeGen/MSIL/obj-*.ll` (FileCheck over `llvm-cil-dump`), `lld/test/MSIL/**`, `msil/test/M2/**` | per milestone |

Two properties only B2 has; both shorten the P2-B critical path:

- **msil-ld's object input can be built and tested before the MC writer exists.** Objects assembled by ilasm from
  object-form text (how every object in J2 and in objfmt-binding's 93/93 corpus run was made) exercise B1-B4 with
  realistic C programs. D1's pre-writer vehicle is yaml2obj with hex CIL payloads (after EM_CIL reaches ELFYAML):
  good for focused lld tests, not for corpus runs.
- **The MC writer gets an exact external oracle**: `canon(clang -c x.c)` must equal `canon(ilasm(object-form text of
  x.c))` with the tests-p2 canonical dumper (`mdcanon`, later `llvm-cil-dump --canonical`), the same comparator
  tests-p2 proposes for outputs (its O3).

D1's compensating property: its writer does not wait for lib/CLI, and `llvm-readobj`, `llvm-objdump -r` and
`llvm-nm` FileCheck tests work from its first commit.

---

## 7. Testability per step

| Step | B2 | D1 |
|---|---|---|
| Encoder (common) | `llc -show-mc-encoding`; ilasm byte oracle (mc-streamer E3: 2,670 of 2,670 methods [R]) | same |
| Object writer | `llvm-cil-dump` FileCheck; ildasm opens every object; canonical equality with ilasm-built objects | `llvm-readobj`, `llvm-objdump -r` and `llvm-nm` FileCheck of the structure; the payload only through our own dumper |
| lld object input | ilasm-built `.il` objects as readable test inputs, or `llc -filetype=obj` | yaml2obj (hex payload) or `llc -filetype=obj` |
| Linked output | D21, ildasm/ilasm round trip, canonical O3/O4 (tests-p2) | same |
| Archives | `llvm-ar` (empty index; lld scans members) | `llvm-ar` with an index |

---

## 8. Scores (0-10 under this lens)

| Candidate | Score | Why |
|---|---|---|
| B2 | 8 | Cheapest format-specific code; zero required upstream hooks or header rebuilds; maximal sharing (one reader and one writer for objects, outputs, references and P2-C inputs); external payload oracles; a pre-writer test vehicle; no payload growth in P3/P4; the prototype ran the zoo and 93/93 corpus programs. Against: LLVM object tools are blind to it, the link-info record is ours to design and version, and the object writer depends on lib/CLI being ready. |
| D1 | 6 | Real LLVM reuse (J1) and standard C link semantics; few, low-conflict hooks (J7); small header cost (J6); writer independent of lib/CLI. Against: about 1k more lines now and a private side format that grows every phase (J5), two symbol spaces, two lld input readers, a fork-reserved machine number. |
| E3 (sole) | 4 | Zero container code and already working (J3); the right bring-up. As the only format: whole-program codegen in every link, no ThinLTO or partitions, version-locked libraries. |
| D+B | 4 | Both tool sets, at the cost of both writers and of keeping two symbol spaces consistent; no lld reuse (lld-link rejects the shape). |
| A1 | 2 | Most code and hooks, no interoperability, the same payload problem as D1. |
| B4 | 2 | No object work, but not a static linker; startup cost per object and a codegen model change. |

---

## 9. Recommendation and the decisions it needs

1. **Bring-up, now: E3.** Let `clang -c` for MSIL triples write bitcode until CIL objects land (or keep the error and
   document `-flto -c`; either is a small driver choice), and build P2B-1 (msil-ld on bitcode, LTO with
   `OverrideTriple`, `--lto-emit-asm` plus ilasm as the interim assembler). Keep this path permanently as `-flto`.
2. **CIL objects: B2.**
   - Objects are ECMA-335 **netmodules** written by `MSILObjectWriter` on the shared lib/CLI `MetadataBuilder` and
     `PEWriter`, with a content-hash MVID (metadata-writer 4.10).
   - Definitions are `<Module>` rows; TU-local symbols are `privatescope`; undefined functions, undefined data and
     function addresses are MemberRefs on placeholder TypeRefs in a placeholder AssemblyRef (one form for functions
     and data; the ImplMap-stub form only when the import module is explicit); BCL members keep a placeholder scope
     for msil-ld to bind (bcl-binding).
   - C link semantics, the object's data image, data relocations (absolute, function, DIFF), constructors with
     priorities, COMDATs, aliases and weak undefined symbols go into a **versioned link-info record** carried as a
     module-level custom attribute (ilasm can assemble it and it survives an ildasm/ilasm round trip [R
     objfmt-binding 4.2]), with the triple and the format version in its header.
   - msil-ld reads objects with the same `MetadataReader` it uses for reference assemblies, finds token sites by
     decoding IL with the shared opcode table, and **synthesizes `.cctor` and `'<msil>Main'`** (J4: no per-object
     init methods).
   - Defer an MSIL `SymbolicFile` (llvm-nm, llvm-ar index) until someone needs it.
3. **Keep D1 as the documented fallback.** The MC layer up to `recordRelocation` is identical (mc-streamer 5.8), so a
   later move to D1 costs only the writer, lld's `ObjFile` and the 6-9 ELF hook files (1-2 minutes of rebuild).
4. **Do not build A1, D+B or B4 for static linking.**

Decisions for the user, beyond those objfmt-container 11.3 and objfmt-binding list:

- Container: B2 (this judge) or D1 (objfmt-container). The trade is "one ECMA-335 code path, readable by .NET tools,
  no upstream hooks" against "LLVM object tools on objects and ELF's standard link semantics".
- Data model: the linker-synthesized initializer (this judge, objfmt-container and objfmt-binding) rather than
  mc-streamer's per-object init methods (J4).
- Design.md 5.1 wording if B2 is chosen: "CIL object files (`clang -c` default): ECMA-335 modules (netmodules) whose
  cross-object references are named metadata references, with a versioned link-info record for C link semantics"
  instead of "an MC-emitted container, modeled on the wasm object format".

---

## 10. What would change my mind

- **A requirement that LLVM object tools work on objects** (llvm-nm, an archive index that a build system inspects,
  `llvm-objdump -r`), or a plan to upstream the backend where reviewers expect an ELF-like object: then D1, or B2
  plus an MSIL `SymbolicFile` (about 400 lines and two or three `.cpp` hooks) if only `nm` and the index matter.
- **A P3 design in which objects never carry TypeDefs, custom attributes or generic instances** (msil-ld synthesizes
  every delegate type, thunk, `[ThreadStatic]` field and SIMD helper): D1's side format then stays small, R-D1
  mostly disappears, and D1's tool reuse would bring it level with B2 (7 and 7).
- **lib/CLI slipping or proving fragile** (for example `MetadataBuilder` finalization bugs that block objects as well
  as outputs): D1 decouples the object writer from it.
- **Evidence that MC cannot hand an ECMA-335 writer what it needs** (provisional tokens through `recordRelocation`,
  method boundaries from per-method sections). I expect none: `WasmObjectWriter` does this with provisional
  indices, and `getMSILSection` already uniques sections by name (`MCContext.cpp:961-972` [S]).
- **A measured IL-decoding cost far above the prototype's** (about 1 ms per KB of IL [R objfmt-binding E8]) would
  favour relocation records. I consider this very unlikely.

---

## 11. FeatureIdeas preclusion check (flagged only, not planned)

- Idea 1 (class placement): B2 and D1 both leave placement to msil-ld, which writes every output row. Neither
  precludes it.
- Idea 3 (typed pointers): B2 carries value types and pointer types natively; D1 needs side-format support (R-D1).
  Neither precludes it; D1 makes it more expensive.
- Ideas 4 and 5 (managed memory, escape analysis): need whole-program IR, which E3 keeps available next to either
  container.
- Ideas 6 and 7 (enclosed instance model, several triples): need linker-synthesized per-instance or per-variant
  initialization and a triple in each object. Both hold for B2 and D1 with the linker-synthesized initializer;
  **mc-streamer's per-object init methods would make 6 and 7 harder** (initializer code fixed in each object).
- C# netmodules in the same assembly (the end-of-project idea in the inputs file): B2 shares the merge path; D1 would
  add a second input kind.

---

## 12. Reproduction

```text
# layout under C:\llvm-msil-scratch\p2\judge-cost-risk (Git Bash, MSYS_NO_PATHCONV=1 set by the script)
d/         cil-elf.s, linkorder.s     from objfmt-container-files/elfcoff/
b2/tools   objfmt-binding-files/tools (cd tools/msilobj && dotnet build -o out)
b2/zoo     objfmt-binding-files/zoo/*.c
e/         a.c: int g = 1; int f(int x){return x+g;}    m.c: int f(int); int main(void){return f(41);}
initonly/  perobj.il, cctor.il        from judge-cost-risk-files/
tables/    p3shape.il, tblcount/      from judge-cost-risk-files/ (cd tblcount && dotnet build -o out)
hdr/       deps.py                    from judge-cost-risk-files/
bash run-all.sh > run-all.log 2>&1    # J1-J7, about one minute
```

Tools: this tree's `clang` and `llc` (`D:\llvm-msil\build\bin`), the VS-bundled LLVM 22.1.3 tools (`llvm-nm`,
`llvm-ar`, `llvm-readobj`, `llvm-objdump`, `ld.lld`, `lld-link`), Framework ilasm 4.8, PEVerify 4.8,
dotnet-ilverify 10.0.12, .NET SDK 10 (net8.0 and net10.0 tools, no packages), .NET 8 and Framework 4.8 runtimes,
git. Installs: none. Build trees: none. Disk: about 15 MB on C:; 32 KB of supporting files plus this report on D:.
