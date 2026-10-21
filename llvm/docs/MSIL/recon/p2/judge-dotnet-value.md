# Judge report: object format and binding through the .NET-ecosystem and user-value lens (key `judge-dotnet-value`)

- Date: 2026-10-21. Checkout: branch `msil` at `1ea64fe9fc7c`.
- Role: independent judge of the Phase-2 object-format and binding decision. Lens: **.NET ecosystem and
  user value**. That means inspectability of objects and outputs with standard .NET tools, determinism,
  fit with the decided or outlined Phase-2..4 goals (Roslyn-style reference binding, `-shared` exports and
  assembly-to-assembly linking, P/Invoke resolution, .NET Framework 4.8 plus modern .NET, Portable PDB),
  keeping open the user-valued option of linking C#-built netmodules into the same assembly, and not
  precluding any FeatureIdeas.md idea. C semantics must not be compromised.
- Read in full: [objfmt-container.md](objfmt-container.md), [objfmt-binding.md](objfmt-binding.md).
  Read where they bear on the lens: [mc-streamer.md](mc-streamer.md) 5.8-5.10,
  [metadata-writer.md](metadata-writer.md), [lld-flavor.md](lld-flavor.md) 4 and 7,
  [bcl-binding.md](bcl-binding.md) 7, [exports-multiasm.md](exports-multiasm.md) 5,
  [driver-toolchain.md](driver-toolchain.md), [tests-p2.md](tests-p2.md) 3.9, [native-imports.md](native-imports.md);
  Design.md 1-8 and 11; ImplementationPlan.md section 7 (P2-A, P2-B, P2-C) and the section 12 rows of
  2026-10-14/15; FeatureIdeas.md; the Phase-2 inputs file (user decisions and the 2026-10-17 discussion).
- Scratch: `C:\llvm-msil-scratch\p2\judge-dotnet-value` (22 MB, 21 MB of it the ILSpy tool). Supporting
  files (sources, the reproduction script and the trimmed log, 28 KB, no binaries) are in
  [judge-dotnet-value-files/](judge-dotnet-value-files/).
- Installs: **ilspycmd 11.1.0.9782** (`dotnet tool install ilspycmd --tool-path <scratch>\tools`, not a
  global install). Nothing else. No tracked file was modified; no cmake or ninja ran. D: had 17 GB free.
- Tools used: Framework ilasm/ildasm 4.8 and PEVerify 4.8, NuGet ilasm/ildasm 10.0.12, dotnet-ilverify
  10.0.12, ilspycmd 11.1, csc from SDK 10.0.402, a 20-line System.Reflection.Metadata tool (`pdbdump`),
  VS-bundled LLVM 22.1.3 (`llvm-nm`, `llvm-ar`, `llvm-lib`, `llvm-objdump`, `llvm-readobj`, `lld-link`,
  `ld.lld`), GNU binutils 2.46 in WSL, .NET 8.0.32 / 10.0.13 / Framework 4.8, and the objfmt-binding
  recon's prototype linker `msilobj` and zoo objects (re-run, not modified).
- "Measured" means run here (section 2); "source" means read in this checkout with a line number;
  anything else is marked *unverified* or *estimate*.

---

## 0. Verdict

**Choose container B with binding 2:** a CIL object is an ECMA-335 **module** (netmodule envelope, no
Assembly row) whose definitions are ordinary rows, whose cross-object references are named metadata
references (MemberRefs on placeholder parents, or ImplMap stubs on ModuleRef `msil.unresolved`), and whose C
link semantics, data images, data relocations and constructor lists travel in a small versioned link-info
record. msil-ld binds by name, renumbers every token, lays out data and **synthesizes `<Module>::.cctor` and
`'<msil>Main'` itself**. Keep **E** (bitcode objects) as the bring-up path for `clang -c` and as the
permanent `-flto` path. Add one cheap thing the recons did not try: a **COFF symbol-table view** in each B
object, which makes `llvm-nm`, `llvm-ar`'s index and GNU `nm`/`ar` work with **zero LLVM changes** (J2).

| Candidate (container + binding) | Score under this lens |
|---|---|
| B + 2: ECMA-335 netmodule object, symbolic binding, link-info record, COFF symbol view | **8** |
| D + 2 hybrid: ELF ET_REL with an embedded ECMA-335 metadata section (MSVC `/clr` shape) | 5 |
| D + 1: ELF ET_REL, `EM_CIL`, token and data relocations, custom side sections (objfmt-container's pick) | 5 |
| E + 3: bitcode-only objects, LTO-only (as the *sole* format; as bring-up it is part of the verdict) | 3 |
| B + 4: every object stays an assembly, forwarder facade + startup driver (no merge) | 2 |
| A + 1: wasm-style custom container with relocations (Design.md 5.1 as written) | 2 |

Why, in one paragraph per point (evidence IDs refer to section 2):

1. **Objects become first-class .NET artifacts.** ILSpy decompiles a B object to C# and shows its
   placeholders by name (`<msil>Symbols.counter`, `<msil>FnAddr.maybe`, `[DllImport("msil.unresolved")]`)
   (J1, resolving objfmt-container's "ILSpy *unverified*"); both ildasms read it; an ildasm -> ilasm
   round trip of two objects relinks into a program that still exits 63 on .NET 8 and Framework 4.8 (J3).
   A D object is `BadImageFormatException` in ILSpy and "not a PE file" in ildasm (J8).
2. **One ECMA-335 reader/writer serves every input and output.** msil-ld must read reference assemblies
   (P2-B), toolchain libraries (P2-C) and, later, C# netmodules, and must write the PE; with B the object
   reader is that same reader, and the MC object writer is the same MetadataBuilder/PEWriter the
   metadata-writer recon plans as `llvm/lib/CLI` anyway. With D, msil-ld has two input families (ELF
   objects and ECMA-335 assemblies) and the MC writer emits a second encoding of the same payload.
3. **The payload of Phases 3-4 is ECMA-335 by nature.** Struct-by-value P/Invoke signatures (Design.md
   2.3), BCL calls whose signatures take `Int128`/`Half`/`Vector128<T>` (7.1, 3.2), `[ThreadStatic]` and
   `UnmanagedCallersOnly` custom attributes (4.2, 4.4, 4.5), the setjmp exception class token (4.6),
   typed pointers (FeatureIdeas 3) all need TypeDef/TypeRef/TypeSpec rows, custom-attribute rows and
   TypeDefOrRef coded indices inside signature blobs. Coded indices are compressed integers, so fixed-size
   ELF relocations cannot patch them (objfmt-binding 3.1; mc-streamer 5.8). D must therefore invent a
   symbolic signature/type/attribute encoding in side sections, which is a second, private metadata
   format. B has rows.
4. **C# interop stays open at the lowest cost.** `csc -target:module` with
   `[DllImport("msil.unresolved", CallingConvention = Cdecl, ExactSpelling = true)]` emits exactly the
   placeholder that B objects (and Phase 1's D14 shape) use: `pinvokeimpl("msil.unresolved" nomangle cdecl)`
   (J4). A C# module can therefore name C functions with no new convention, and msil-ld's object reader
   is already a netmodule reader. CoreCLR cannot load netmodules (0x8013101E, both recons), so merging in
   msil-ld would be the *only* way to run C# module code inside a C assembly on modern .NET.
5. **The LLVM-tool gap of B is smaller than reported.** `llvm-nm` "no symbols" and the empty `llvm-ar` index
   (objfmt-container 4.1) disappear when the object's PE envelope carries a COFF symbol table:
   `COFFObjectFile` reads the symbol table of any PE image whose `PointerToSymbolTable` is non-zero
   (`llvm/lib/Object/COFFObjectFile.cpp:984-985`) and `SymbolicFile` accepts `pecoff_executable`
   (`llvm/lib/Object/SymbolicFile.cpp:43`). Measured: `T`/`t`/`U`/`C` in `llvm-nm`, a 15-symbol armap,
   GNU `nm`/`ar` likewise, .NET readers and the prototype linker unaffected, native linkers still refuse
   the file (J2). This makes lld-flavor's R1 ("a reader in `llvm/lib/Object` plus a `file_magic`")
   unnecessary for B.
6. **Initialization belongs to msil-ld.** Objects must carry data and relocation tables, never init code:
   a per-object init method that stores an `initonly` address field runs (exit 49 on Framework and .NET 8)
   but ILVerify reports `[InitOnly] Cannot change initonly field outside its .ctor` and PEVerify the same,
   while the identical store inside `.cctor` is clean (J6). That rejects mc-streamer 5.9's recommended
   "per-object init methods" model unless the address fields stop being `initonly`, and it is what
   FeatureIdeas 6 (per-instance init) and 7 (per-variant init) need anyway.
7. **E stays.** It unblocks P2-B with no container work (lld-flavor's P2B-1 prototype linked 93/93) and is
   the `-flto` path; as the *only* format it fails this lens (version-tied bitcode libraries, whole-program
   codegen per link, no CIL object for C# or ilasm inputs).

D is the right answer to a different lens (C-toolchain fidelity: `llvm-readobj`, `objcopy`, section-level
`--gc-sections`, standard ELF encodings of weak/COMDAT/TLS for free). Under this lens it scores 5: it keeps
every FeatureIdea and C# linking possible, but makes objects opaque to every .NET tool and duplicates the
metadata model.

---

## 1. Criteria and weights

| # | Criterion | Weight | Container-dependent? |
|---|---|---|---|
| C1 | Objects readable by ildasm, ILSpy, System.Reflection.Metadata | medium (objects are intermediate, but they are the developer's window into `clang -c`, and the user asked about netmodule objects) | yes |
| C2 | Outputs readable, verifiable (D21) and deterministic | high | no: every option writes the output through the same PE writer |
| C3 | Fit with Roslyn-style reference binding (P2-B) | high | partly: how objects name BCL members |
| C4 | Fit with `-shared` exports and assembly-to-assembly linking (P2-C) | high | partly: reader sharing |
| C5 | P/Invoke resolution (native imports) | medium | partly: how objects carry import stubs and flags |
| C6 | .NET Framework 4.8 and modern .NET | high | no for outputs; objects are never loaded |
| C7 | Portable PDB (Phase 4) | medium | yes: per-object debug carrier |
| C8 | Payload expressiveness for Phase 3/4 .NET features | high | yes |
| C9 | C#-built netmodules linkable later | medium (user-valued, end-of-project) | yes |
| C10 | FeatureIdeas not precluded | gate | yes (data model) |
| C11 | C link semantics intact | gate | yes |
| C12 | C-developer tools on objects (`nm`, `ar`, `objdump`) | medium | yes |
| C13 | Upstream conflict surface (Design.md risk 5) | low under this lens | yes |

---

## 2. Experiments re-run or added (J0-J8)

All in `C:\llvm-msil-scratch\p2\judge-dotnet-value`, reproduced end to end by
[run.sh](judge-dotnet-value-files/run.sh); the log is [results.txt](judge-dotnet-value-files/results.txt).

| ID | What | Result |
|---|---|---|
| J0 | Re-ran objfmt-binding's E2: zoo objects `a.o`, `b.o` (ilasm-built B objects) through the prototype `msilobj link`, DLL and EXE | `main`, `add3`, **exit 63 on .NET 8, .NET 10 and Framework 4.8**, constructors `ctor_b(100) ctor_a(200)`: reproduced. Side finding: two links of the same inputs differ (494 bytes; also in two directories with the same output name) because the prototype writes `Guid.NewGuid()` as MVID (`objfmt-binding-files/tools/msilobj/Linker.cs:554`). That is a prototype artifact, not a format property: objfmt-container's `netmerge` hashes the inputs and was byte-identical. |
| J1 | `ilspycmd` 11.1 on the B object `a.o` (netmodule, no manifest) and on the linked `zoo.dll` | Both decompile to C#. In the object, undefined data shows as `*(int*)<msil>Symbols.counter`, the weak-undefined function address as `<msil>FnAddr.maybe`, undefined functions as `[DllImport("msil.unresolved", CallingConvention = CallingConvention.Cdecl)] public static extern int twice(int A_0);`, function pointers as `delegate*<int, int>`. |
| J2 | Appended a COFF symbol table to the PE envelope of `a.o`/`b.o` ([coffsym.py](judge-dotnet-value-files/coffsym.py): plain `EXTERNAL`/`STATIC` records, `Value` = size for common) | `a.o` 5,632 -> 6,045 bytes (19 records + 71-byte string table). `llvm-nm` 22.1.3: before "no symbols", after `T main`, `t helper`, `U twice`, `C common_c`, ... `llvm-ar rcs` + `llvm-nm --print-armap`: 15 defined globals indexed. GNU `nm`/`ar` 2.46 (WSL): same symbols and an index. ildasm 4.8 and 10.0.12: 9 methods, as before; ILSpy unchanged; the prototype linker extracts `b.o` lazily from the archive and the program exits 63. `lld-link`/`ld.lld`: "unknown file type"; `llvm-lib`: "not a COFF object, bitcode, archive, import library or resource file"; `lib.exe`: LNK1302 (unchanged). A first variant that used COFF weak externals for weak symbols put `.weak.*.default` names and the weak-undefined `maybe` into the index, so the view must use plain records (section 7.3). |
| J3 | ildasm 4.8 `/out` of both objects, ilasm 4.8 `/dll /pe64 /x64` back to objects, relink | exit 63 on .NET 8 and Framework 4.8: the link info (here a ManifestResource) and the placeholders survive the round trip. |
| J4 | `csc -target:module` (SDK 10.0.402, net48 `mscorlib` reference) of [cslib.cs](judge-dotnet-value-files/cslib.cs): a class calling a C function through `[DllImport("msil.unresolved", Cdecl, ExactSpelling)]` | `.method private hidebysig static pinvokeimpl("msil.unresolved" nomangle cdecl)`, `call int32 CsLib::twice(int32)`: the B placeholder shape, from a stock C# compiler. The prototype linker refuses the module ("no msil.link resource"): a managed-module input kind (classes with members) is future work in every option. |
| J5 | ilasm 10.0.12 `-debug=opt -det` on an object-shaped module with `.line` directives ([pdb-obj.il](judge-dotnet-value-files/pdb-obj.il)); SRM dump ([pdbdump](judge-dotnet-value-files/pdbdump/Program.cs)) | A Portable PDB (`BSJB`) next to the object; sequence points keyed by the object's own MethodDef row: `twice (0x06000001) IL_0000 -> obj.c:3:1`, `IL_0002 -> obj.c:4:1`. |
| J6 | [initonly.il](judge-dotnet-value-files/initonly.il): `'<msil>init_data$a.o'()` (called from `.cctor`) stores one `initonly` field, `.cctor` stores another | Runs, exit 49 on Framework 4.8 and .NET 8. ILVerify 10.0.12: `[InitOnly] ... <msil>init_data$a.o() ... Cannot change initonly field outside its .ctor`; PEVerify 4.8: the same error. The store in `.cctor` is not reported. |
| J7 | Typed link-semantics attributes on object rows ([link-attrs.il](judge-dotnet-value-files/link-attrs.il)): `[msil.link]MSIL.Link.WeakAttribute`, `ComdatAttribute(string)`, `CommonAttribute(int32,int32)`, module-level `ObjectAttribute(triple, version)` | ilasm 4.8 assembles; ILSpy renders `[Weak] public static int weakfn()`, `[Comdat("inl")]`, `[Common(4, 4)]`, `[module: Object("x86_64-pc-windows-msvc-msil", 1)]`; ildasm 10 prints the `.custom` lines. The attribute types live in a placeholder assembly that never exists; attributes are resolved only when reflected, and objects are never loaded. |
| J8 | objfmt-container's D object `cil-elf-em.o` (ELF, `e_machine` 0xC11) | `llvm-nm` reads it; GNU `nm`/`objdump -h` read it (`elf64-little`); ILSpy: `BadImageFormatException: Unknown file format`; ildasm 10: "not found or not a PE file". |

Also checked in source for the debug-info question (no recon covered it, section 5.7):
`MCObjectFileInfo::initMSILMCObjectFileInfo` defines no DWARF sections ("No DWARF, EH or TLS sections ...",
`llvm/lib/MC/MCObjectFileInfo.cpp:1180-1188`), `MSILMCAsmInfo` sets `SupportsDebugInformation = false`
(`llvm/lib/Target/MSIL/MCTargetDesc/MSILMCAsmInfo.cpp:59`), and `getRelocationResolver` returns
`{nullptr, nullptr}` for an ELF object whose architecture it does not list (`llvm/lib/Object/RelocationResolver.cpp:799-848`),
so `llvm-dwarfdump` could not apply relocations in a D object without one more shared hook.

---

## 3. Pruning the combination space

Containers: A wasm-style custom; B ECMA-335 module envelope plus link info; C pure netmodules plus naming
conventions; D ELF/COFF relocatable with CIL sections; E bitcode only. Bindings: 1 relocations/token
patching; 2 symbolic name-bound references, linker-synthesized initialization; 3 LTO-only; 4 no merge,
one assembly per object; 5 GOT-like table.

| | 1 relocations | 2 symbolic | 3 LTO-only | 4 no merge | 5 GOT |
|---|---|---|---|---|---|
| A | **scored** (A+1) | pruned: a custom container that carries named metadata references and signatures is B without the .NET tools | n/a | pruned: not loadable | pruned (as B+5) |
| B | pruned: IL is self-describing (opcode table finds every token), so token-site lists are redundant; data relocations are in the link info anyway | **scored** (B+2), recommended | n/a | **scored** (B+4) | pruned: `calli` is 3-5x an inlined call (objfmt-binding E12) and buys nothing inside one merged module |
| C | pruned: netmodules have no relocation records; adding them is B | pruned as a separate candidate: naming conventions alone carry no weak/COMDAT/common/TLS/priorities/data relocs; adding attributes makes it B (objfmt-container 5.2) | n/a | pruned (as B+4) | pruned |
| D | **scored** (D+1, objfmt-container's pick); COFF variant pruned (machine CEE rejected by every tool, machine 0 makes `link.exe`/`lld-link` link CIL as x64) | **scored** as the hybrid D+2 (ELF symbols for C linkage + embedded ECMA-335 metadata for the payload, IL tokens object-local; the MSVC `/clr` architecture, objfmt-container 6.1) | n/a | pruned: ELF cannot be loaded | pruned |
| E | n/a | n/a | **scored** (E+3) as the sole format | n/a | n/a |

---

## 4. Scored candidates

### 4.1 B + 2: ECMA-335 netmodule objects, symbolic binding, link-info record, COFF symbol view. Score 8

- **C1 objects readable:** yes, by ildasm 4.8/10, ILSpy (J1), SRM, and editable through an ildasm/ilasm
  round trip (J3). Link semantics are readable too if carried as typed attributes (J7), otherwise through
  `llvm-cil-dump` (metadata-writer WP F).
- **C3 Roslyn-style binding:** BCL members are MemberRefs on TypeRefs with an unbound or `[mscorlib]`
  scope; msil-ld rebinds the scope by searching `--reference` inputs exactly as bcl-binding 5 specifies.
  Nothing new to encode.
- **C4 P2-C:** the object reader and the toolchain-library reader are one CLI reader; the link-info record
  and P2-C's symbol-table attribute can share one encoder (both are versioned byte blobs on a custom
  attribute; exports-multiasm 5.3 shows such attributes survive ildasm/ilasm, J3/J7 here).
- **C5 P/Invoke:** explicit imports are already ImplMap rows (Phase-1 shape), undefined functions are
  stubs on `msil.unresolved` or MemberRefs; msil-ld rewrites the ModuleRef and flags (native-imports:
  ExactSpelling|CDecl). Per-undefined-symbol flags (vararg, no-prototype, byval/sret) go into the link info.
- **C7 Portable PDB:** a per-object Portable PDB keyed by the object's MethodDef rows exists today
  through ilasm (J5); msil-ld permutes MethodDebugInformation with the MethodDef map, the same remapping it
  does for metadata.
- **C8 payload:** native rows for value types, TypeSpecs, CAs, EH class tokens, generic instantiations.
  The prototype already re-encodes TypeDefOrRef coded indices in every signature (`IL.RewriteSig`,
  objfmt-binding 4.3 step 5).
- **C9 C# netmodules:** same placeholder convention (J4); the merger generalizes from `<Module>` members
  to classes with members.
- **C10/C11:** all of the zoo's semantics were prototyped (weak, weak undefined, COMDAT, common, alias,
  cross-object constructor priorities, string merging, archives; objfmt-binding 4.4, J0). Not prototyped:
  TLS, DIFF relocations across objects, COMDAT selection kinds other than `any`, `__start_`/`__stop_`,
  `--gc-sections`. All are records of the kind wasm's `linking` section already has; nothing in ECMA-335
  blocks them. No FeatureIdea is precluded, provided objects carry no init code (J6).
- **C12:** `nm`/`ar` through the COFF view (J2); `llvm-objdump -d`/`llvm-readobj` show only the PE
  envelope; disassembly is ildasm/ILSpy/`llvm-cil-dump`.
- **Weak spots:** the link-info record is a custom design that must reach ELF's coverage (risk, section
  7.5); Windows `lib.exe`/`llvm-lib` refuse B objects (LNK1302 / "not a COFF object"), the same as D, so
  `llvm-ar` is the archiver on every triple; codegen-independent linker test inputs come from ilasm text
  (an external tool) rather than `yaml2obj`.

### 4.2 D + 2 hybrid: ELF ET_REL with an embedded ECMA-335 metadata section. Score 5

ELF symbols, groups, visibility, TLS sections, `.init_array.N` and data RELA carry C linkage;
`.cil.text.<sym>` sections hold method bodies whose tokens are object-local metadata rows of a
`.msil.meta` section (an ECMA-335 metadata root, like MSVC's `.cormeta`); msil-ld remaps tokens by
decoding, as in B.

- Gets LLVM and GNU tools for symbols (J8) and a native payload (no second metadata format).
- But objects are still opaque to ildasm/ILSpy (they need a PE; SRM can read a raw metadata root only
  through code), there are **two symbol spaces** to keep consistent (ELF symbols and metadata rows; MSVC
  needed CLR-token symbols with aux records for this, objfmt-container 6.1), and section-level GC and
  COMDAT do not map onto rows of one shared metadata blob without extra bookkeeping. It also pays D's
  shared-file edits (ELF.h with 163 direct includers, `ELFObjectFile.h`, `Object/ELF.cpp`, `ELFYAML.cpp`,
  `ELFDumper.cpp`, plus `RelocationResolver.cpp` for `llvm-dwarfdump`) and a fork-reserved `e_machine`.
- It is the right fallback **if** the user ranks C-toolchain fidelity first: it avoids D+1's private
  metadata format.

### 4.3 D + 1: ELF ET_REL, `EM_CIL`, token and data relocations, custom side sections. Score 5

- **For:** every LLVM/GNU object tool works from day one (objfmt-container 6.2, J8); C linkage in standard
  ELF; deterministic by construction; section-level `--gc-sections`; `yaml2obj` ELF inputs for linker tests.
- **Against, under this lens:** objects are unreadable by every .NET tool (J8); the payload past Phase-1
  scalars needs a private encoding (section 0 point 3), so P2-A either designs a symbolic
  signature/type/attribute format now or reopens the object format in Phase 3; msil-ld reads ELF objects
  *and* ECMA-335 assemblies; C# netmodules become a second, rarely exercised input path; per-object debug
  info would be DWARF, which the MSIL MC environment does not have (`MCObjectFileInfo.cpp:1187`), which
  `llvm-dwarfdump` cannot relocate without a `RelocationResolver.cpp` case, and which msil-ld must convert
  to Portable PDB (local variables need a location convention like wasm's `DW_OP_WASM_location`); ELF
  files on Windows triples surprise Windows/.NET developers and `lib.exe`/`llvm-lib` refuse them
  (objfmt-container 6.2).
- objfmt-container's 2-3k-line estimate leaves out that payload encoding, and its table charges B for the
  ECMA-335 writer that metadata-writer plans for msil-ld regardless (section 6).

### 4.4 E + 3: bitcode-only objects, LTO-only, as the sole format. Score 3

- **For:** works today (`clang -c -flto`), deterministic, `llvm-ar`/`llvm-nm`/`llvm-lib` read it,
  whole-program IR is what FeatureIdeas 4 and 5 need, and the LTO codegen could write a Portable PDB
  directly.
- **Dealbreakers as the only format:** every link runs whole-program codegen (24.8 s for 52 KLOC, 54.1 s
  with the O2 pipeline, single-threaded; objfmt-container 7); static libraries ship as bitcode readable
  only by the same or newer LLVM, with debug metadata dropped on upgrade (`llvm/docs/DeveloperPolicy.md`,
  cited there); no CIL object exists for C# modules, ilasm-built helpers or prebuilt CIL archives; ThinLTO
  and LTO partitions need an object representation anyway (lld-flavor 3).
- As the bring-up path and the `-flto` path it is part of the verdict.

### 4.5 B + 4: every object stays an assembly (forwarder facade + startup driver). Score 2

The most CLR-native "linking" (the runtime binds at JIT time; objfmt-binding E10 ran on all three
runtimes with no change to any object), but for whole programs it ships one DLL per C file (about 1 ms of
startup each), a public class per global symbol, non-`initonly` address fields, an explicit init driver,
and dead weak/COMDAT losers. Deploying N assemblies for one C program is hostile to .NET users. Keep its
pieces (forwarders, two-phase init) for P2-C only, as exports-multiasm does.

### 4.6 A + 1: wasm-style custom container with relocations. Score 2

Neither .NET tools nor LLVM tools read it until 6-8k lines and about 30 shared hook sites are written
(objfmt-container 3); the same payload-encoding problem as D+1; no interoperability gain over B or D.

---

## 5. The lens, criterion by criterion

### 5.1 Inspectability (C1, C2, C12)

| Tool | B object | B object + COFF view | D object | E object | Linked output (all) |
|---|---|---|---|---|---|
| ildasm 4.8 / 10.0.12 | yes (J2, J3) | yes | no (J8) | no | yes |
| ILSpy 11.1 | yes, C# with placeholders (J1) | yes | no (J8) | no | yes (J1) |
| System.Reflection.Metadata | yes | yes | no | no | yes |
| ilasm round trip of the object | yes, relinks and runs (J3) | the COFF view is dropped, the object stays linkable | no | no | yes (D21 leg) |
| `llvm-nm`, `llvm-ar` index | "no symbols" / empty | yes (J2) | yes | yes | n/a |
| GNU `nm` / `ar` | | yes (J2) | yes (J8) | | |
| `llvm-readobj`/`objdump` symbol details, groups | PE headers only | COFF symbols only | yes | `llvm-dis` | PE headers |
| CIL disassembly | ildasm/ILSpy | ildasm/ILSpy | needs an MSIL `MCDisassembler` + `makeTriple` hook (`ELFObjectFile.h`) | after codegen only | ildasm/ILSpy |
| `lib.exe` / `llvm-lib` | LNK1302 / rejected | same | LNK1107 / rejected | LNK1107 / accepted | n/a |

Outputs are the same in every option, so C2 does not separate the candidates; it only requires that the
output writer be deterministic (metadata-writer 3.10 and 4.10: MVID and timestamp from
`BlobContentId.FromHash` of a SHA-256) and that objects not carry anything that leaks into outputs nondeterministically (J0's
prototype MVID is the counter-example to avoid). For B objects, also fix the Module row name (for example
the source file's base name, not the `-o` path), because Roslyn-style "module name = output file name"
would make `-o tmp123.o` change the object bytes.

### 5.2 Roslyn-style reference binding (C3)

The binding itself happens in msil-ld for every container (bcl-binding 5). What differs is how an object
names `Marshal::AllocHGlobal(native int)`: in B it is a TypeRef + MemberRef + signature blob, which is
exactly what the binder consumes and what a C# netmodule also contains (J4: csc emits the same row kinds
for its BCL calls); in D it is an undefined ELF symbol with an invented mangling plus a signature in a side
section, and those symbols must be kept out of archive extraction and "undefined symbol" diagnostics.
System.Type arguments in attribute blobs are assembly-qualified strings (bcl-binding, objfmt-binding 3.1);
in B they sit in CustomAttribute rows the binder rewrites, in D they need yet another side-section record.

### 5.3 Exports and assembly-to-assembly linking (C4)

exports-multiasm's design (export class, `'<msil>addr_'` accessors, two-phase initialization, symbol-table
attribute) is container-independent, but it requires msil-ld to read toolchain-built assemblies with the
CLI reader. With B, objects, libraries and references all go through that reader; with D, a second input
path. B also makes `msil-ld -r` (partial link into one object) trivial, because the merger's output before
`.cctor` synthesis *is* an object.

### 5.4 P/Invoke resolution (C5)

native-imports wants objects to carry, per undefined function, the signature and flags (vararg,
no-prototype, byval/sret size and alignment, explicit import module/name). B: the ImplMap/MemberRef row
plus link-info flags; the explicit-import case is Phase 1's ImplMap row unchanged. D: a symbol plus a
side-section record. Equivalent power; B reuses the rows msil-ld writes into the output.

### 5.5 Framework 4.8 and modern .NET (C6)

No option affects what the output loads on; that is decided by the references (bcl-binding) and the
envelope (metadata-writer: Roslyn-style PE32+, ILONLY, EXE for programs). B objects are never loaded; as
netmodules they cannot be run by mistake, and `dotnet exec` refuses a `.o` extension anyway
(driver-toolchain 0, measured there: "dotnet exec needs a managed .dll or .exe extension").

### 5.6 C# netmodules (C9)

- CoreCLR refuses multi-module assemblies (0x8013101E, objfmt-container 4.2, objfmt-binding E13), so a C#
  netmodule can only run inside a C program's assembly on modern .NET if msil-ld merges it.
- C# -> C: the placeholder is shared (J4). C -> C#: needs a naming rule (for example an asm label
  `"CsLib.cs_add"` or an attribute), out of Phase-2 scope.
- Cost: a general merger (TypeDefs with members, nested types, properties, events, generic parameters,
  MethodImpl, InterfaceImpl, CustomAttribute, Constant, FieldMarshal, DeclSecurity), estimated 1.5-2.5k
  lines of C++ in every option. With B it extends the object merger that every C link exercises; with D
  or A it is a separate input kind; with E-only it does not exist. I agree with objfmt-container 11.3
  item 4: keep it an end-of-project idea, but fix the `msil.unresolved` ImplMap convention now as the
  stable cross-language contract.

### 5.7 Portable PDB in Phase 4 (C7) - a gap in the recons

No recon looked at debug info. Facts: the MSIL MC environment has no DWARF sections and
`SupportsDebugInformation = false` (section 2). Options per container:

- **B:** each object carries a Portable PDB (embedded debug-directory entry or a companion stream) whose
  MethodDebugInformation rows parallel the object's MethodDef rows (J5); msil-ld permutes and merges
  Document, MethodDebugInformation, LocalScope, LocalVariable and CustomDebugInformation rows with the same
  token maps as metadata. Compile side: a Portable-PDB debug handler fed by MC line entries (a new
  target-local handler; size *estimate* 1-1.5k lines). The ilasm `-S` path already produces such PDBs as
  a test oracle (J5).
- **D:** DWARF in `.debug_*` sections needs DWARF sections in the MSIL MC environment (or D-i's ELF
  environment, which reopens D5), a `RelocationResolver.cpp` case for `llvm-dwarfdump`, and a DWARF ->
  Portable PDB converter in msil-ld with a local-variable location convention.
- **E:** LTO codegen in msil-ld writes the PDB directly.

None precludes Phase 4, but B keeps one debug format from compile to output. Planners should reserve the
per-object debug carrier in P2-A's object specification.

### 5.8 FeatureIdeas (C10)

| Idea | B + 2 | D + 1 | Note |
|---|---|---|---|
| 1 class placement | objects can already put methods into TypeDefs; msil-ld moves or merges classes | needs a class record per symbol | neither precludes |
| 3 typed pointers | `int32*`, `valuetype S*` are ordinary signature elements | needs the private signature format | binding must compare signatures by ABI, not by blob (objfmt-binding 12) |
| 4, 5 managed memory, escape analysis | through the E/LTO path | same | neither precludes |
| 6 enclosed instance | needs linker-synthesized per-instance init from data tables: objects must not carry init code (J6) | same | objfmt-container 10's "B makes idea 6 harder" assumed per-object `.cctor`s; objfmt-binding's B carries tables, so the concern does not apply |
| 7 several triples | triple in the link info; per-triple classes, segments, initializers in msil-ld | triple in `.note.msil` | neither precludes; keep per-variant state out of process-wide singletons (lld-flavor 7) |
| C# modules in the same assembly | cheapest | second input kind | 5.6 |

The only Phase-2 choice that would make an idea harder is a **data model with per-object init code**
(mc-streamer 5.9 option 2): it violates `initonly` verification (J6) and splits initialization in a way
ideas 6 and 7 must undo.

### 5.9 C semantics (C11)

Both B+2 and D+1 carry the full set; D gets them from ELF, B from a record that must be designed to the
same coverage. The objfmt-binding prototype shows the B record working for weak, weak undefined, COMDAT
`any`, common, aliases, constructor priorities across objects, string merging and archives on three
runtimes and both triples (J0 reproduces the core run). The remaining items (TLS, DIFF, COMDAT selection
kinds, start/stop symbols, gc) must be in the record's first version, with wasm's `linking` section
(`llvm/include/llvm/BinaryFormat/Wasm.h:204-264`, cited by objfmt-container 3.1) as the checklist.

---

## 6. Corrections and gaps in the recon reports

1. **objfmt-container 2/4.1/12, "llvm-nm prints 'no symbols', so the llvm-ar index of B objects is
   empty", and lld-flavor R1 "a reader in `llvm/lib/Object` plus a `file_magic`":** a COFF symbol table in
   the PE envelope gives both with no LLVM change (J2). An MSIL `SymbolicFile` is optional, not required.
2. **objfmt-container 12, "*unverified*: ILSpy reading B objects":** verified (J1).
3. **objfmt-container 2 (cost row) and 11.2:** B is charged "est. 3.5-5k (ECMA-335 writer)", but
   metadata-writer plans that library (`llvm/lib/CLI`, WP A-G) for msil-ld's output in every option, so
   its marginal cost for B is the object-specific part (placeholders, privatescope, link-info record,
   COFF view). D's 2-3k excludes the symbolic signature/type/attribute encoding that Phase 3 needs
   (section 0 point 3). On marginal cost B is not more expensive than D (*estimate*, not measured).
4. **objfmt-container 10, idea 6 "B makes it harder":** only true of per-object `.cctor` code, which
   objfmt-binding's B does not have.
5. **mc-streamer 5.9, recommended data model "per-object segment with per-object init methods":** fails
   ILVerify and PEVerify `[InitOnly]` (J6, and objfmt-container's netmerge finding), and its weak rule
   ("store the address field only if still zero") needs non-`initonly` fields. Prefer the wasm-ld model
   that objfmt-binding prototyped and that passes `msil-verify.py` (objfmt-binding 4.5).
6. **objfmt-binding E2/E8 prototype:** outputs are not deterministic (J0) because of `Guid.NewGuid()`;
   harmless for the recon's conclusions, but the C++ writer must hash content (metadata-writer 3.10, 4.10).
7. **Debug info:** not covered by any recon (section 5.7).
8. **objfmt-container 4.1 "A netmodule is the safer object envelope"**: agreed; and with the COFF view
   the native linkers still refuse it (J2), so adding symbols does not reintroduce the machine-0 hazard.

---

## 7. Recommendation for the planners (B + 2, with E)

### 7.1 Object shape

- PE32+ envelope (machine from the triple, as outputs), CLI header, ECMA-335 metadata, **no Assembly
  row** (a netmodule), content-hash MVID, Module name independent of the `-o` path.
- Definitions: global methods and `initonly native int` address fields of `<Module>` (Phase-1 names);
  global/weak/COMDAT definitions `public`, TU-locals `privatescope` (objfmt-binding 4.1).
- Undefined functions: MemberRef on `[msil.extern]'<msil>Symbols'`, or an ImplMap stub on ModuleRef
  `msil.unresolved` when the object wants the Phase-1/C# shape; explicit imports keep ImplMap with their
  real module. Undefined data: field MemberRef on `'<msil>Symbols'`. Function addresses: MemberRef on
  `'<msil>FnAddr'` (one linker-owned `<msil>fn_` field per function keeps Framework `ldftn` identity, M9).
- BCL members: MemberRef on a TypeRef with an unbound placeholder scope (bcl-binding's preference); `-S`
  keeps `[mscorlib]` for default-table members.
- **No `.cctor`, no `'<msil>Main'`, no `<msil>image` field in objects.** The data image, per-symbol
  layout, data relocations (ADDR, FNADDR, DIFF, TLS_OFFSET), constructors with priorities, aliases, weak
  undefined names, COMDAT groups with selection kinds, segment flags (STRINGS, TLS, RETAIN), section names
  for start/stop symbols, the triple, a format version and codegen-switch flags go into the link info.

### 7.2 Link-info carrier and presentation

- Carrier: a module-level custom attribute (survives ildasm/ilasm, objfmt-binding 4.2; exports-multiasm
  5.3) or a ManifestResource (survives, J3). Not an extra metadata stream (dropped by the round trip,
  exports-multiasm 5.3).
- Presentation (a planner choice): a compact binary record keyed by tokens (objfmt-binding 11.2) decoded
  by `llvm-cil-dump`, or typed per-row attributes for per-symbol semantics (`Weak`, `Comdat`, `Common`,
  `Hidden`, `Alias`) that ILSpy shows as C# attributes (J7), with only bulk data (image, relocations) in a
  blob. The second costs a CustomAttribute row per flagged symbol in objects only (outputs never carry
  them).
- Share the versioned encoder with P2-C's symbol-table attribute (exports-multiasm 5.2).

### 7.3 COFF symbol-table view

- Written by the object writer after the PE image: one plain record per global and local symbol
  (`EXTERNAL` defined, `STATIC` local, `EXTERNAL` undefined, common as undefined with `Value` = size);
  weak definitions as plain defined records and weak undefined ones as plain undefined records, because
  COFF weak externals pollute the archive index (J2's first variant). Put the data image in a data
  section so data symbols print as `D`.
- It is a **view** for `nm`/`ar`/build scripts (libtool reads `nm` output); msil-ld never reads it, the
  link info is authoritative, and an ildasm/ilasm round trip drops it harmlessly. About 150 lines.

### 7.4 Pipeline

- P2-B starts on E (lld-flavor P2B-1), with ilasm-built B objects from `-S` object-form text as msil-ld's
  first object inputs (the objfmt-binding route), before the MC object writer exists.
- The MC object writer (P2-A) uses the shared `llvm/lib/CLI` MetadataBuilder and PEWriter; tests compare
  `clang -c | msil-ld` with `clang -S | ilasm` (tests-p2 O3) and ildasm every object as a cheap check
  (tests-p2 3.9 B).
- LTO codegen in msil-ld emits an in-memory B object that goes through the same reader (wasm-ld model).

### 7.5 Risks to track

- The link-info record must reach ELF/wasm coverage before P2-A freezes it (TLS, DIFF, COMDAT kinds,
  start/stop, gc roots, `llvm.used`); version it from day one.
- Windows archivers: `llvm-ar` on every triple (same as D); document that `lib.exe` cannot archive CIL
  objects.
- Linker tests need object inputs: ilasm text (requires the gate's NuGet ilasm, Windows and Linux) or a
  small `yaml2cli`-style generator in `llvm-cil-dump`; D would have had `yaml2obj` for free.
- ECMA's exemption of `privatescope` from the unique-name rule is relied on (verified on all runtimes by
  objfmt-binding E1/E2); an ildasm round trip renames duplicates (`$PST...`), which J3 shows is harmless.
- Per-type member limits (65,524 methods, metadata-writer 3.6) apply to one object's `<Module>` too; the
  writer must spill like msil-ld.

---

## 8. Decisions for the user (from this lens)

1. Object container: **B + 2** (recommended here) vs D + 1 (objfmt-container's recommendation) vs the
   D + 2 hybrid. B: .NET-readable objects, one metadata model and reader, cheapest C# path, no shared
   LLVM header edits; D: LLVM/GNU object tools and standard ELF semantics.
2. If B: add the COFF symbol-table view (recommended) or an MSIL `SymbolicFile` in `llvm/lib/Object`
   (shared hooks) or neither.
3. Link-info presentation: compact blob only, or typed per-row attributes plus a bulk blob (J7).
4. Data model: msil-ld synthesizes `.cctor` from tables (recommended; verifier-clean) vs per-object init
   methods (mc-streamer; needs non-`initonly` address fields and an allow-list change).
5. Fix `pinvokeimpl("msil.unresolved" cdecl)` as the documented cross-language placeholder now; C#
   netmodule inputs stay an end-of-project idea.
6. Per-object debug carrier for Phase 4: Portable PDB rows (B-native) vs DWARF (D-native); reserve it in
   the P2-A object specification.

---

## 9. What would change my mind

- The user ranks C-developer object tooling (`llvm-readobj` symbol details, `objcopy`, ld-style section GC,
  `yaml2obj` test inputs) above .NET readability and code sharing: then the D + 2 hybrid, not D + 1.
- A worked design shows the B link-info record needs substantially more than wasm's `linking` section to
  cover TLS, COMDAT kinds and start/stop symbols, or a C++ prototype of it overruns badly.
- A tool in the build path (Linux GNU `ar`, CMake, libtool, sccache/ccache) mishandles PE-envelope objects
  with a COFF symbol table on some host (J2 covered llvm-ar/nm 22, GNU nm/ar 2.46 and the .NET readers
  only).
- Phase 4 settles on DWARF in objects for reasons outside this lens (for example DWARF-based debuggers on
  Linux reading CIL methods), which would favour an ELF container.
- The user drops C# netmodule linking and P2-C turns out not to need the CLI reader for inputs (it does,
  per exports-multiasm), removing the reader-sharing argument.

---

## 10. Reproduction

```text
# prerequisites: the objfmt-binding scratch (zoo objects, tools/msilobj built with `dotnet build -o out`)
#                and objfmt-container's elfcoff/cil-elf-em.o
dotnet tool install ilspycmd --tool-path C:\llvm-msil-scratch\p2\judge-dotnet-value\tools
cd C:\llvm-msil-scratch\p2\judge-dotnet-value\tools-src\pdbdump && dotnet build -c Release -o out
# copy judge-dotnet-value-files/{coffsym.py -> coffsym/, cslib.cs -> csmod/, link-attrs.il -> ca/obj.il,
#      pdb-obj.il -> pdb/obj.il, initonly.il -> initonly/init.il (+ an rc8.json runtimeconfig)}
bash run.sh > results.txt 2>&1        # J0-J8, Git Bash on the Windows host, WSL for GNU binutils
```

Files: [run.sh](judge-dotnet-value-files/run.sh), [results.txt](judge-dotnet-value-files/results.txt),
[coffsym.py](judge-dotnet-value-files/coffsym.py), [cslib.cs](judge-dotnet-value-files/cslib.cs),
[link-attrs.il](judge-dotnet-value-files/link-attrs.il), [pdb-obj.il](judge-dotnet-value-files/pdb-obj.il),
[initonly.il](judge-dotnet-value-files/initonly.il), [pdbdump/](judge-dotnet-value-files/pdbdump/).
