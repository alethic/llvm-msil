# MSIL Phase 2 recon: binding cross-object references (`objfmt-binding`)

Status: recon report for Phase 2 planning, 2026-10-21. Branch `msil` at `1ea64fe9fc7c`.
Experiments ran in `C:\llvm-msil-scratch\p2\objfmt-binding` (79 MB, all on C:). Small sources,
scripts and logs that back every claim are in [objfmt-binding-files/](objfmt-binding-files/).
Tools used: the shared build's `clang`/`llc` (`D:\llvm-msil\build\bin`), Framework ilasm 4.8, NuGet
ilasm/ildasm 10.0.12, NETFX 4.8 ildasm and PEVerify, dotnet-ilverify 10.0.12, .NET SDK 10 (C# tools
on System.Reflection.Metadata), .NET 8.0.31/10.0.12 runtimes, .NET Framework 4.8, WSL Ubuntu
(.NET 8 and 10), the VS-bundled `llvm-ar`/`llvm-nm`/`llvm-objdump`/`llvm-objcopy`. Nothing was
installed. "Measured" means run here; "source" means read in this checkout with a line number;
anything else is marked *unverified*.

## 0. The answer in brief

The question: how does a reference from one object to a function or global in another object
become a final metadata token or address, and where do C link semantics live?

1. **Tokens are row numbers, so every merge renumbers.** Whatever the object format, a linker
   that produces one module must translate every token in every IL body through a per-object
   old-to-new map. The only real choices are how the linker *finds* the token sites (relocation
   records, or decoding the IL opcode stream) and how it *learns* what each token means (a custom
   symbol table, or the object's own metadata). Measured on the M1 corpus: 1.8 (-O0) to 2.5 (-O2)
   token operands per 100 IL bytes; zero tokens inside signature blobs, EH clauses or user strings
   in today's C codegen (section 3).
2. **Phase-1 IL already contains no addresses.** Globals are read through `initonly native int`
   address fields, functions through `<msil>fn_<name>` fields, and pointer-valued initialized data
   is fixed up by IL in `<Module>::.cctor`. So an address "relocation" exists only inside
   initialized data; IL only ever needs token binding (section 2).
3. **Option 2, symbolic objects, works end to end** (prototype, section 4): an object is an
   ECMA-335 module (built here from Phase-1 `-S` output plus ilasm), cross-object references are
   MemberRefs on a placeholder TypeRef `[msil.extern]'<msil>Symbols'` or Phase-1-shaped ImplMap
   stubs on ModuleRef `msil.unresolved`, TU-local symbols are `privatescope`, and a small
   link-info record carries what metadata cannot (weak, COMDAT, common, aliases, data layout,
   data relocations, constructor priorities). A 726-line C# linker merges objects, binds by name,
   rewrites every token and generates `.cctor` and `'<msil>Main'`. Results: the two-object
   test program (weak, weak undefined, COMDAT, common, alias, constructors with priorities across
   objects, function pointers and data pointers across objects, string literals, static names
   duplicated across objects, a shared native import) prints the native transcript and exits 63
   on .NET 8, .NET 10 and .NET Framework 4.8 at -O0, -O1 and -O2 (windows-msvc), and on WSL
   .NET 8/10 (linux-gnu, -O1);
   all 93 C programs of `msil/test/M1` pass through objects and the linker at -O0 and -O2 on
   .NET 8 and Framework 4.8; archives extract members lazily; BCL references bind against the
   net8.0 or net48 reference assemblies with the same by-name mechanism.
4. **Option 1, wasm-ld-style relocations**, is the same linker with the token sites listed in the
   object instead of found by decoding; it costs a new container, reader, writer and tool support
   and makes objects unreadable by .NET tools, for no gain in binding power (section 5).
5. **Option 3, LTO-only**, already works with IR-level linking except for two backend gaps
   (aliases and `extern_weak` declarations, both errors today), but puts whole-program codegen in
   every link (about 100 ms of `llc` per corpus module versus about 1 ms per KB of IL for the
   prototype merge) and rules out parallel or partitioned codegen, prebuilt CIL objects and mixed
   inputs (section 6).
6. **Option 4, no merge**, works on all three runtimes with *no change to any object*: the link
   product is a facade assembly of ExportedType forwarders plus a startup driver. It costs one
   assembly per C file (about 1 ms of startup each, measured), a public class per global symbol,
   non-`initonly` address fields and an explicit initialization driver; it is the right model for
   P2-C library-to-library linking, not for whole programs (section 7).
7. **Option 5, a GOT filled at startup**, turns every cross-object call into `calli`: 3 to 5 times
   an inlined direct call (section 8).
8. **ECMA-335 mechanisms** (section 9): ImplMap + ModuleRef and placeholder MemberRefs are both
   good object forms and fail cleanly if an object is run unlinked; MemberRef with a ModuleRef
   parent plus File/ExportedType (netmodules) runs on Framework 4.8 only (CoreCLR: "only single
   file assemblies are supported", 0x8013101E); type forwarders work on Framework 4.8, .NET 8 and
   .NET 10; ENCLog/ENCMap make an image unloadable on both runtimes (precedent only);
   CustomAttribute rows, a ManifestResource, an extra PE section, CustomDebugInformation in an
   embedded Portable PDB and even an extra metadata stream `#MSIL` are all tolerated by both
   ildasms, System.Reflection.Metadata and both runtimes; `privatescope` allows duplicate
   TU-local names in one `<Module>`.

**Recommendation:** option 2. Objects are ECMA-335 modules produced by the P2-A MC object writer
(and, until it exists, by `-S` plus ilasm, which is how every object here was built). Every
reference is a named metadata reference; definitions are rows; the linker binds by name, checks
signatures, renumbers every token, lays out one data segment from the link-info record, applies
data relocations in a linker-generated `.cctor` (Phase 1's module initializer moves from the
AsmPrinter to `msil-ld`) and writes the PE. The same C++ ECMA-335 reader and writer then serve the
MC object writer, `msil-ld`'s object input, reference-assembly binding (P2-B) and managed inputs
(P2-C). Details in section 11. The binding model itself (bind by name, renumber every token,
linker-generated initialization from a data-relocation table) holds for any container; section
11.5 maps it onto the ELF container that [objfmt-container.md](objfmt-container.md) recommends.

## 1. Experiments

| # | Experiment | Files (under objfmt-binding-files/) | Result |
|---|---|---|---|
| E1 | Object shape through ilasm: module without manifest, `[msil.extern]` MemberRefs (method and field), ImplMap stub on `msil.unresolved`, two `privatescope` methods with one name | `zoo/a.o.il`, `zoo/a.o.ildasm.txt` | both ilasms accept it; ildasm shows `[msil.extern]'<msil>Symbols'::counter`, `helper$PST06000003` |
| E2 | Option 2 merge of the zoo program (two objects), -O0/-O1/-O2, windows-msvc | `zoo/`, `tools/`, `logs/zoo-all.log` | `main`, `add3`, exit 63 on .NET 8, .NET 10, Framework 4.8 (native: same) |
| E3 | Same, linux-gnu triple (string merging of `.str`), run in WSL | `logs/zoo-all.log` | exit 63 on WSL .NET 8 and .NET 10 |
| E4 | Archive `libbcd.a` (llvm-ar of three objects), lazy extraction, weak undefined does not extract | `logs/zoo-all.log` | only `b.o` extracted; exit 63 everywhere |
| E5 | M1 corpus (93 C programs) through objects and the linker | `tools/corpus.sh`, `logs/measurements.txt` | 93/93 at -O0 and -O2 on .NET 8 and Framework 4.8 |
| E6 | Verification and round trip of a linked output | section 4.5 | `msil-verify.py` OK; round trip differs only in the `.data` label |
| E7 | BCL references bound against reference assemblies | `tools/msilobj/Refs.cs`, `logs/zoo-all.log` | net8.0 pack: `System.Runtime` + `System.Runtime.InteropServices`, runs on .NET 8/10, not Framework; net48: `mscorlib`, runs on all three |
| E8 | Link throughput: 200 synthetic objects, 10,000 methods, 440 KB IL | `bench/scale-gen.py` | 0.38-0.58 s link phase, 1.2-1.5 s process |
| E9 | Option 3: IR-level link of the zoo (`-Xclang -mlink-bitcode-file`) | `logs/zoo-all.log` | errors for the alias and `extern_weak`; without them exit 70 (alias identity lost) on all three runtimes |
| E10 | Option 4: zoo as two unchanged assemblies + forwarder facade + driver | `nomerge-zoo/`, `tools/nomerge.py` | exit 63 on .NET 8, .NET 10, Framework 4.8 |
| E11 | Option 4 cost: 200 one-file assemblies vs one assembly | `bench/startup-*.py` | +0.9 ms (.NET 8) and +1.1 ms (Framework) per assembly |
| E12 | Option 5 cost: call vs `calli` through a startup-filled slot | `bench/got-Program.cs` | calli 3-5x an inlined call, 1.0-1.5x a non-inlined call |
| E13 | Netmodule linking (ModuleRef-parent MemberRef, File, ExportedType) | `ecma/multimodule/` | Framework: works (exit 81); .NET 8/10: FileLoadException 0x8013101E |
| E14 | Type forwarders; reference to another assembly's `<Module>` member; lazy `.cctor` cycle; running an unlinked object | `ecma/` | forwarders work everywhere; `[b]'<Module>'` is a TypeLoadException everywhere; cycle safe when own fields are set first; clean DllNotFound/FileNotFound |
| E15 | Carriers for link info: CA, resource, PE section, embedded PDB CDI, extra stream, EnC tables | `tools/msilobj/Carriers.cs`, `ecma/carriers-load.ps1`, `logs/ecma-all.log` | all but EnC load and run on Framework 4.8 and .NET 10; EnC: BadImageFormat on both |
| E16 | Cross-object signature mismatch | `sigmismatch/`, `logs/measurements.txt` | InvalidProgramException at JIT (native ignores the extra argument) |

The zoo program (`zoo/a.c`, `zoo/b.c`): `a.c` calls `add3`, `twice` and `alias_twice` (an alias of
`twice` in `b.c`) through a function-pointer table in initialized data, reads `counter` (defined in
`b.c`) through a pointer in initialized data, has a weak `weakfn` that `b.c` overrides with a strong
one, tests the weak undefined `maybe`, defines `common_c` as a common symbol (so does `b.c`), uses
the string literal `"hello"` (a `linkonce_odr` COMDAT on windows-msvc, a private `unnamed_addr`
`.str` on linux-gnu; `b.c` uses the same literal), has a `static` function `helper` and a `static`
variable `local_g` with the same names as `b.c`'s, calls `puts` (so does `b.c`) and registers a
constructor at priority 200 (`b.c`: 100). `b_extra` in `b.c` reads `common_c` and the COMDAT string
from code, so code references to losing copies are exercised. The native oracle (`clang
--target=x86_64-pc-windows-msvc -O1 -fcommon`, lld-link) prints `main`, `add3` and exits 63.

## 2. The starting point: how Phase 1 refers to things (source)

Every reference the backend emits is an `MSILMemberRefExpr`
(`llvm/lib/Target/MSIL/MCTargetDesc/MSILMCExpr.h:39-78`), kinds `Method` (IL name + `MSIL::Signature`),
`Field` (IL name + offset; the offset is printed as `ldc.i4 <off>; add`, not folded into a token),
`StandAloneSig` and `BCLMember` (text). So a Phase-1 MCInst already carries a *symbolic* reference:
a name and a signature, never a token number.

- Functions (`MSILMCInstLower.cpp:55-83`, `lowerFunctionOperand`): a call names the method; an
  undefined callee is recorded by `MSILAsmPrinter::noteExternFunction` (`MSILAsmPrinter.cpp:319`) and
  becomes a P/Invoke declaration in `emitPInvokeDeclarations` (`:725`) if it is a D14 import, an
  error otherwise. `FUNC_ADDR` reads the field `'<msil>fn_<IL name>'` (`noteFunctionAddress`, `:362`;
  prefix constant `FuncFieldPrefix`, `:167`), which `.cctor` sets once with `ldftn` (M9: two `ldftn`
  of one method can differ on Framework).
- Globals (`MSILMCInstLower.cpp:230-250`): `GLOBAL_ADDR` reads the global's address field; an
  undefined variable is recorded by `noteExternGlobal` (`MSILAsmPrinter.cpp:345`) and reported by
  `collectExternGlobals` (`:545`): "external global variable ... (phase 2 linker)".
- Data (`MSILDataSegment.h:92-149`): `Image` (initialized bytes), `Entries` (offset and size per
  global), `Zero` (bss), `Relocs` (`MSILReloc`, `:55-65`: site, target global or function,
  addend, width 4 or 8) and `Diffs` (`MSILDiff`, `:71-81`: A - B + addend, folded into the image by
  `finalize()` because both globals are in the one segment).
- `emitModuleInitializer` (`MSILAsmPrinter.cpp:848`) writes `<Module>::.cctor`: `AllocHGlobal`,
  `cpblk` from the RVA image, `initblk` for bss, one `stsfld` per address field, `ldftn`/`stsfld` per
  function-address field, one `stind.i` per relocation, then `llvm.global_ctors` in priority order;
  `emitEntryPoint` (`:1007`) writes `'<msil>Main'` (D27).
- Linkage: `emitGlobalVariable` (`:1148-1195`) puts every definition of any linkage into the
  segment ("decisions D12 and D26: comdats are ignored"); `emitGlobalAlias` (`:1197`) is an error;
  MSILPrepareIR replaces aliases by poison (D26).

Consequence: in a Phase-1 module a cross-TU reference can occur in exactly three places: (a) an IL
token operand naming a method, an address field or a function-address field; (b) a data relocation
(a global or a function, plus addend); (c) an entry of the constructor list. (a) needs token
binding only; (b) needs an address, which Phase 1 already produces at startup by IL in `.cctor`.

## 3. What any merge has to rewrite

### 3.1 Inventory of token-bearing places (ECMA-335 II.22-II.25)

| Place | What it holds | In today's C codegen | Rewrite |
|---|---|---|---|
| IL operands of `call`, `ldftn`, `jmp`, `newobj` | MethodDef `06`, MemberRef `0A`, MethodSpec `2B` | always | 4-byte in place |
| IL operands of `ldsfld`, `stsfld`, `ldsflda`, `ldfld`... | FieldDef `04`, MemberRef `0A` | always (address fields) | 4-byte in place |
| IL operand of `calli` | StandAloneSig `11` | function pointers | 4-byte in place |
| IL operands of `ldtoken`, `sizeof`, `initobj`, `box`, `castclass`... | TypeDef `02`, TypeRef `01`, TypeSpec `1B` | none today | 4-byte in place |
| IL operand of `ldstr` | #US heap offset `70` | none (0 over the corpus) | 4-byte in place, string re-added |
| Method header `LocalVarSigTok` | StandAloneSig | every method with locals | 4-byte in place |
| Signature blobs (MethodDef, MemberRef, Field, StandAloneSig) | TypeDefOrRef coded indices after `CLASS 0x12`, `VALUETYPE 0x11`, `CMOD_REQD/OPT` | none today (0 over the corpus) | **compressed integers (1, 2 or 4 bytes): re-encode, cannot patch** |
| EH clauses | ClassToken of a typed catch | none today; P3 setjmp/longjmp | 4-byte in place |
| CustomAttribute rows | Parent (HasCustomAttribute), Type (MethodDef/MemberRef) | none today; P3 `[ThreadStatic]`, `UnmanagedCallersOnly` | row cells, re-emitted |
| CustomAttribute *blobs* | no tokens, but `System.Type` arguments are assembly-qualified **strings** (II.23.3) | none today | under P2-B reference binding the assembly name inside the string must follow the bound reference |
| Other row cells (MemberRef.Class, ImplMap, FieldRVA, ClassLayout, NestedClass, MethodImpl) | row numbers, RVAs | ImplMap, FieldRVA, ClassLayout | re-emitted by the writer |
| Portable PDB (phase 4) | MethodDebugInformation is parallel to MethodDef; LocalScope.Method; CDI parents | none yet | permute with the MethodDef map |

### 3.2 Measured

- **M1 corpus** (`msilobj ilstats` over the 91 objects of `tools/corpus.sh`): -O0: 1338 methods,
  416,251 IL bytes, 7,546 token operands (1.8 per 100 IL bytes; tokens are 7.3% of the IL bytes):
  MethodDef 4,358, Field 2,859, MemberRef 165, StandAloneSig 164. -O2: 1002 methods, 314,296 IL
  bytes, 7,975 token operands (2.5 per 100 IL bytes; 10.1%): Field 3,921, MethodDef 3,720,
  StandAloneSig 183, MemberRef 151. Summed over all 93 corpus links: 0 coded indices in signatures,
  0 EH clauses, 0 user strings; 369 (-O0) and 320 (-O2) data relocations.
- **Zoo at -O1** (`zoo/zoo.dll.token-relocs.txt`, one line per token site with old and new token):
  37 sites. In `a.o` (linked first) 7 of 24 tokens changed (its rows mostly keep their numbers); in
  `b.o` 12 of 13 changed. A linker cannot skip the first object either: `a.o`'s call of its own weak
  `weakfn` (`06000001`) must become `b.o`'s strong one (`06000007`), and its ImplMap stub for `puts`
  moves.
- **What a wasm-style object would carry instead** (one relocation record per token site:
  type byte + ULEB offset + ULEB symbol index): about 28.5 KB for the -O0 corpus objects (6.9% of
  the IL size) and 30.3 KB at -O2 (9.6%).

### 3.3 Consequences

- Every merging option visits every token. Patching in place is possible for all IL and header
  tokens (always 4 bytes); signature blobs must be re-encoded because coded indices are compressed.
  Since `msil-ld` writes the whole PE anyway (Design.md 5.5), it emits fresh tables and heaps and
  never patches an existing image; method bodies are copied with their token operands overwritten.
- The IL opcode stream is self-describing: an operand-size table of ~220 opcodes finds every token
  (the prototype takes it from `System.Reflection.Emit.OpCodes`, `tools/msilobj/IL.cs`). Relocation
  records for IL tokens are therefore redundant once the object is valid metadata.
- Today's C code never puts a token inside a signature; struct-by-value P/Invoke signatures
  (Design.md 2.3), delegate types for P3-B thunks and typed struct pointers (FeatureIdeas 3) will.

## 4. Option 2: symbolic objects (prototype)

### 4.1 Object shape

Built for the prototype by `tools/mkobj.sh` (an emulation of `clang -c`): `clang -S -emit-llvm`,
`tools/irinfo.py prep` (undefined functions get the `msil-import-module`="msil.unresolved"
attribute, external variables become temporary zero definitions so the backend prints the code
shape, aliases are dropped for the Phase-1 backend), Phase-1 `llc`, `tools/objectify.py` (rewrites
the text into the object shape below and writes the link info), Framework ilasm. Excerpt of
`zoo/a.o.il` and its disassembly (`zoo/a.o.ildasm.txt`):

```text
.module 'a.o'                                         // no .assembly: an object is not an assembly
.assembly extern mscorlib { ... .ver 4:0:0:0 }        // BCL references as Phase 1 prints them
.assembly extern 'msil.extern' { .ver 0:0:0:0 }       // placeholder scope of undefined symbols
.module extern 'msil.unresolved'                      // placeholder module of ImplMap stubs
.mresource private 'msil.link' { }                    // link info (section 4.2)

.method public static int32 'weakfn'() cil managed { ... }          // weak: public + link info
.method privatescope static void 'ctor_a'() cil managed             // TU-local: privatescope
  ldsfld native int [msil.extern]'<msil>Symbols'::'counter'         // undefined data: MemberRef
  ldsfld native int 'va'                                            // own data: FieldDef
.method public static int32 'main'() cil managed
  call int32 'puts'(native int)                                     // D14 import: Phase-1 stub
  call int32 'helper'(int32)                                        // own local: MethodDef
  call int32 'b_extra'()                                            // undefined: ImplMap stub
  ldsfld native int [msil.extern]'<msil>FnAddr'::'maybe'            // function address: MemberRef
.method public hidebysig static pinvokeimpl("msil.unresolved" cdecl) int32 'b_extra'() ... {}
.field public static initonly native int 'table'                    // address fields of defined data
.field privatescope static initonly native int 'local_g'
```

Rules:
- Definitions are rows of `<Module>`: functions are global methods, defined globals are
  `initonly native int` address fields (Phase-1 names). Global and weak symbols are `public`;
  TU-local ones are `privatescope` (ECMA "compiler controlled": not referenceable by name, duplicate
  names allowed, E1/E2).
- References to the object's own definitions use their MethodDef/FieldDef tokens, even for weak and
  COMDAT definitions: the linker redirects the token when another object's copy wins (measured:
  "MethodDef -> other object's definition" and "FieldDef -> other object's address field").
- Undefined functions: either the Phase-1 P/Invoke shape with ModuleRef `msil.unresolved`, or a
  MemberRef on TypeRef `'<msil>Symbols'` in AssemblyRef `msil.extern`. The zoo uses both (`b_extra`,
  `add3`, `twice`, `alias_twice` as stubs; the weak `maybe` as MemberRef); the linker handles both.
- Undefined data: MemberRef `[msil.extern]'<msil>Symbols'::'name'` with field signature
  `native int` (ImplMap cannot describe fields: II.22.22 says MemberForwarded only ever indexes
  MethodDef).
- Function addresses: MemberRef `[msil.extern]'<msil>FnAddr'::'name'` (signature `native int`), for
  defined and undefined functions alike, so the linker can make one `<msil>fn_` field per function
  for the whole program (Framework `ldftn` identity, M9).
- No `.cctor`, no `'<msil>Main'`, no `<msil>image_t`/RVA field: the linker generates them.
- Objects are 2-6 KB ilasm PE images (`a.o` 5,632 bytes with 2.9 KB of JSON link info; a binary
  record would be a few hundred bytes).

### 4.2 Link info

What metadata cannot express, one record per object (JSON in the prototype, `zoo/a.link.json`):

| Field | Content | Used for |
|---|---|---|
| `image`, `init_size` | the object's initialized data bytes | layout |
| `symbols[]` | name, kind (func/data), bind (global/weak/local/common), comdat key, visibility, and for data offset/size/alignment/`merge_strings` | resolution, layout, string merging |
| `relocs[]` | site (offset in the object image), kind (`addr64`, `addr32`, `fnaddr64`), target name, addend, unaligned width | data relocations |
| `ctors[]` | function, priority (from `llvm.global_ctors`) | global constructor order |
| `aliases{}` | alias name -> target symbol | GlobalAlias |
| `weak_undef[]` | weak undefined names | null binding, no archive extraction |

Missing from the prototype and needed by a real record (section 11.2): a format version and the
triple, comdat selection kinds, symbol-difference relocations (`MSILDiff` across symbols), TLS
segments and offsets (P3), destructors (P3), section names for `__start_`/`__stop_` (wasm-ld
synthesizes them, `lld/wasm/Writer.cpp:544-555`), `llvm.used` retention for dead stripping.
The record can be carried by any of the carriers of E15 (section 9); for the `-S` text form a
module-level custom attribute is the most convenient, because ilasm assembles it and it survives an
ildasm/ilasm round trip (tested with `b.o`'s 2.9 KB record: 2,866-byte attribute value after
assembly and after the round trip). ilasm attaches `.custom` to the most recently declared item, so
the placeholder `.assembly extern 'msil.link'` must come before `.module` and the `.custom`
directly after it; with the reference declared later ilasm prints "Reference to undeclared extern
assembly ... Failed to autodetect" and silently drops the attribute.

### 4.3 The linker (`tools/msilobj/Linker.cs`)

1. **Inputs.** Objects and `ar` archives (GNU/COFF format, `Archive.Members`). An object is read
   with `PEReader`/`MetadataReader`: MethodDefs with a body are definitions, MethodDefs with an
   ImplMap are stubs (module `msil.unresolved` = undefined, any other module = an explicit native
   import), FieldDefs are address fields, MemberRefs on `[msil.extern]` are placeholders (kind
   from `MemberReference.GetKind()` and the type name).
2. **Resolution** (lld/wasm rules): strong beats weak; two strong definitions are an error; the
   first copy of a COMDAT wins; common plus common keeps the largest size and alignment; a
   definition beats common; an alias is a definition that resolves to its target. Archive members
   are extracted while a *non-weak* undefined reference names one of their globals (ELF semantics:
   a weak undefined reference does not extract). An unresolved name is a native import if the
   import table has it (prototype: `puts`, `putchar`, the D14 math functions, `--import name=lib`;
   msil-ld: native export tables and import libraries, Design.md 5.2 step 2.3), null if weak,
   otherwise "undefined symbol".
3. **Rows.** Kept methods (TU-local ones become `privatescope`), then native P/Invoke stubs (one per
   name, library and signature across all objects), then trap stubs (`ldnull; throw`) for called
   weak undefined functions (wasm-ld: `SymbolTable::createUndefinedStub`,
   `lld/wasm/SymbolTable.cpp:1159`), `.cctor` and `'<msil>Main'`. Fields: one address field per kept
   data symbol, one `<msil>fn_` field per function whose address is used (keyed by the *resolved*
   method, so an alias shares its target's field), `<msil>data`, `<msil>image`. Row numbers are
   fixed before any body is encoded; the prototype asserts that no row appears later.
4. **Data layout.** One segment: kept initialized symbols in object order, each at its alignment;
   then zero-initialized symbols; COMDAT losers and losing weak copies take no space;
   `merge_strings` symbols with equal bytes share one copy. Base alignment = largest alignment, at
   least 16 (as Phase 1).
5. **Bodies.** For every kept method: copy the IL bytes, decode the opcode stream, map every token
   operand through the object's map (table in 4.4), map the local signature, set
   `hasDynamicStackAllocation` when the body contains `localloc`, copy EH regions (catch-type tokens
   mapped). Signatures are re-encoded (`IL.RewriteSig` maps every TypeDefOrRef coded index).
6. **Generated code.** `.cctor` = Phase 1's module initializer, but for the whole program: allocate,
   copy the image, zero bss, set every address field, `ldftn` every function-address field, apply
   every data relocation of every object (a site inside a discarded COMDAT copy is dropped;
   misaligned sites get `unaligned.`), then call all constructors sorted by priority and then link
   order (a constructor that returns a value is followed by `pop`). `'<msil>Main'` is D27's, for
   `main(void)` or `main(int, char **)`.
7. **References.** BCL TypeRefs are copied as the object names them, or, with `--ref <dir>`, bound
   to the reference assembly that defines the type (`tools/msilobj/Refs.cs`, E7).
8. **Output.** `ManagedPEBuilder`, PE32+ AMD64, IL-only, DLL or EXE.

### 4.4 Results

Token bindings in the zoo link at -O1 (`logs/zoo-all.log`):

| Binding | Sites |
|---|---|
| FieldDef -> own address field | 23 |
| FieldDef -> other object's address field (the common and the COMDAT string that `a.o` won) | 2 |
| MethodDef -> own definition | 2 |
| MethodDef -> other object's definition (`a.o`'s weak `weakfn` -> `b.o`'s) | 1 |
| ImplMap stub (`msil.unresolved`) -> definition (`b_extra`) | 1 |
| MethodDef stub with an explicit module -> shared native import (`puts` from both objects) | 2 |
| MemberRef `<msil>Symbols` (data) -> address field (`counter`) | 2 |
| MemberRef `<msil>FnAddr` -> function-address field (`maybe`, never set: weak undefined) | 1 |
| StandAloneSig -> StandAloneSig | 3 |

Link statistics: 12 methods (9 from objects, 1 native import), 19 fields, a 104-byte segment (100
initialized), 351 IL bytes and 37 token sites rewritten, 24 signature blobs re-encoded (0 coded
indices inside), 6 data relocations, 1 COMDAT copy dropped, 1 weak definition overridden, 1
common merge, constructors `ctor_b(100) ctor_a(200)` although `ctor_a`'s object comes first. On
linux-gnu, the two `.str` copies of `"hello"` are merged instead (1 string merged, 0 COMDAT).

| Run | .NET 8 | .NET 10 | Framework 4.8 | WSL .NET 8/10 |
|---|---|---|---|---|
| zoo, windows-msvc, -O0/-O1/-O2 | 63 | 63 | 63 | |
| zoo + `libbcd.a` (lazy extraction), -O0/-O1/-O2 | 63 | 63 | 63 | |
| zoo, BCL bound to net8.0 references | 63 | 63 | 127 (FileNotFound `System.Runtime 8.0.0.0`, as D10 predicts) | |
| zoo, BCL bound to net48 references | 63 | 63 | 63 | |
| zoo, linux-gnu, -O1 | | | | 63 / 63 |
| zoo + `c.o` on the command line (defines `maybe`) + `libbcd.a` | 223 = (63 + 100000) & 255 | | 223 | |
| M1 corpus (93 programs), -O0 and -O2 | 93/93 | | 93/93 | |
| 200 synthetic objects (E8) | exit 0 | | | |

Every run printed `main` and `add3` where the program prints them. Link throughput (E8, C#
prototype on .NET 10, inputs already read): 0.38-0.58 s for 200 objects, 10,003 methods, 440,002 IL
bytes, 60,000 token sites and 1,600 data relocations (about 1 ms per KB of IL); 1.2-1.5 s for the
whole process.

### 4.5 Findings along the way

- **Duplicate TU-local names** work as `privatescope` rows (`helper` and `local_g` twice in one
  `<Module>`; .NET 8/10, Framework 4.8). The disassembly shows the second as `$MD$5$PST06000005` and
  `$FD$C$PST0400000C`, and an ildasm/ilasm round trip *renames* them to `$MD$5`/`$FD$C` (names read
  back with System.Reflection.Metadata); behaviour is unchanged. If readable names matter, msil-ld
  can suffix locals (for example `helper$b.o`) instead.
- **Verification**: the linked zoo passes `msil/utils/msil-verify.py` (ILVerify with the allow-list,
  PEVerify, and the neutralized second pass), provided the synthesized `<msil>image` and
  `<msil>data` fields keep Phase 1's `private` access: with `privatescope` the neutralizer did not
  recognize `ldsflda '<msil>image$PST...'` and reported `Cannot modify an imaged based (RVA) static`.
- **Round trip** (`msil-verify.py --roundtrip`): the reassembled listing differs from the original
  only in the label of the RVA data (`I_00002780` vs `I_00002060`), because ManagedPEBuilder and
  ilasm place the mapped field data differently. The Phase-2 round-trip leg must normalize `.data`
  labels.
- **Signature mismatch** (E16): binding by name with a different signature gives
  `InvalidProgramException` at JIT time; msil-ld must compare signatures when it binds and either
  diagnose or emit an adapter (wasm-ld: `reportFunctionSignatureMismatch` and `getFunctionVariant`,
  `lld/wasm/SymbolTable.cpp:378-398, 1046`). K&R calls and mismatched prototypes across TUs are
  common in old C, so an adapter (link-time MSILFixFunctionBitcasts) is worth planning.
- **ilasm and non-ASCII names**: without a BOM, Framework ilasm reads the object text as ANSI and
  `größe` became `grAAYe` in metadata (D11 already requires a BOM; the emulation forgot it once).
- **Unlinked objects** fail cleanly at first use (E14): `DllNotFoundException: Unable to load DLL
  'msil.unresolved'` for a stub, `FileNotFoundException: ... 'msil.extern, Version=0.0.0.0'` for a
  MemberRef, on Framework 4.8 and .NET 8.
- **ilverify cannot check objects**: it rejects a module without manifest (BadImageFormat) and, for
  an assembly-shaped object, reports `Failed to load assembly 'msil.extern'`. Objects are never
  verified; outputs are.
- **LLVM tools** see an ilasm-built object as `coff-x86-64` with sections `.text` and `.reloc`;
  `llvm-nm` prints "no symbols"; `llvm-ar rcs` archives it with an empty symbol index
  (`llvm-nm --print-armap` lists no symbols). Link-time lazy extraction does not need the index:
  lld reads each member's own symbols (`lld/wasm/Driver.cpp:246-300`; `ArchiveWriter.cpp:545`: "lld
  and mold ignore the archive symbol index"). `SymbolicFile::isSymbolicFile` already accepts
  `pecoff_executable` (`llvm/lib/Object/SymbolicFile.cpp`), so an MSIL `SymbolicFile` over the
  metadata reader would give `llvm-nm` and the archive index.

## 5. Option 1: wasm-ld-style link-time relocations

Mechanism: the object is a custom container (wasm-like sections, or ELF/COFF sections, option D of
the inputs file) holding IL bodies whose token operands are placeholders, a symbol table and
relocation records; the linker writes each final token at each listed site.

Relocation kinds an MSIL object needs (with the wasm analogue from
`llvm/include/llvm/BinaryFormat/WasmRelocs.def`):

| Kind | Site | Value | wasm analogue |
|---|---|---|---|
| `R_MSIL_METHOD_TOKEN` | IL operand of `call`/`ldftn`/`jmp` | MethodDef of the definition, ImplMap stub, or MemberRef (P2-C) | `R_WASM_FUNCTION_INDEX_LEB` |
| `R_MSIL_FIELD_TOKEN` | `ldsfld`/`stsfld`/`ldsflda` of an address field | the symbol's address field | `R_WASM_GLOBAL_INDEX_LEB` |
| `R_MSIL_FNADDR_TOKEN` | `ldsfld` of a function-address field | the function's `<msil>fn_` field | `R_WASM_TABLE_INDEX_SLEB` |
| `R_MSIL_SIG_TOKEN` | `calli` operand, `LocalVarSigTok` | StandAloneSig | `R_WASM_TYPE_INDEX_LEB` |
| `R_MSIL_TYPE_TOKEN` | `ldtoken`, `sizeof`, `initobj`, EH class token | TypeDef/TypeRef | `R_WASM_TYPE_INDEX_LEB` |
| `R_MSIL_BCL_TOKEN` | BCL member operand | MemberRef bound against references (P2-B) | none |
| `R_MSIL_USERSTRING` | `ldstr` | #US offset | none |
| (signature type references) | inside signature blobs | compressed coded index: the object should keep signatures as structured entries, not blobs | `R_WASM_TYPE_INDEX_LEB` |
| `R_MSIL_ADDR64`/`ADDR32` | initialized data | address of symbol + addend | `R_WASM_MEMORY_ADDR_I64`/`I32` |
| `R_MSIL_FNADDR64`/`FNADDR32` | initialized data | function pointer value | `R_WASM_TABLE_INDEX_I64`/`I32` |
| `R_MSIL_DIFF32`/`DIFF64` | initialized data | A - B + addend | `R_WASM_MEMORY_ADDR_LOCREL_I32` (A - site) |
| `R_MSIL_TLS_OFFSET` (P3) | initialized data or a TLS offset field | offset in the TLS template | `R_WASM_MEMORY_ADDR_TLS_SLEB` |

Comparison with option 2:
- Binding power is identical: the apply step is "token at site := map(symbol)" in both, and the data
  relocations and link semantics are the same table. The prototype's `--trace-relocs` output
  (`zoo/zoo.dll.token-relocs.txt`) is exactly the IL relocation list an option-1 object would
  carry (37 entries for the zoo; about 7-10% of IL size for the corpus, section 3.2).
- Option 1 needs a container format, an MC object writer for it, a reader in msil-ld, llvm-objdump
  and llvm-nm support, and still an ECMA-335 reader (references, P2-C inputs) and writer (output).
  Option 2 needs only the ECMA-335 reader and writer, and the object is readable by ildasm, ILSpy,
  System.Reflection.Metadata and `ilasm` round trips.
- Option 1's advantages: no IL decoding (cheap anyway), and with an ELF/COFF container llvm-ar,
  llvm-nm and COMDAT groups work unchanged. Option 2 gets the first two from an MSIL `SymbolicFile`.

## 6. Option 3: LTO only (no CIL object format)

Mechanism: `clang -c` emits bitcode; msil-ld links IR (lld's LTO driver), runs the backend once and
writes the assembly.

- Works today in principle: `clang --target=x86_64-pc-windows-msvc-msil -flto -c b.c` already
  writes LLVM bitcode, and `clang ... -S a.c -Xclang -mlink-bitcode-file -Xclang b.bc` links the two
  modules before codegen. On the zoo it fails with `error: alias @alias_twice is not supported for
  MSIL` and `error: undefined function 'maybe' ... Phase 1 has no linker` (an `extern_weak`
  declaration that nothing defines). With the alias replaced by a wrapper and the weak undefined by
  a null constant it runs and exits 70 on .NET 8, .NET 10 and Framework 4.8 (63 + 7: the wrapper
  has its own address, so alias identity is lost).
- So even LTO-only needs GlobalAlias lowering (also open issue 10, rustc mergefunc) and a lowering
  of unresolved `extern_weak` to null (with a trap stub for calls).
- Everything else is handled by the IR linker: weak/strong, COMDAT, common, `appending`
  `llvm.global_ctors` with priorities, visibility, alignment, data relocations, and ConstantMerge
  for strings. Phase 1's single-module code paths then apply unchanged.
- Costs: whole-program optimization and codegen in every link. Measured Phase-1 codegen alone
  (Release+assertions `llc -O2`): 12.4 s for the 91 corpus modules, about 97 ms of codegen per
  module of about 3.4 KB IL (roughly 28 ms per KB of IL), versus about 1 ms per KB of IL for the
  unoptimized prototype merge. No parallel or partitioned codegen without an object format to
  combine the partitions (lld's `--lto-partitions` and ThinLTO produce several objects). Libraries
  ship as bitcode, which newer LLVM reads but older LLVM does not. No prebuilt CIL objects and no
  mixing (the "compiler-rt-like C helper archive" of section 6 of Design.md would also be bitcode).
- Good as a fast path inside option 2 (`-flto` objects) and as an early milestone if the linker is
  built before the object writer; not sufficient alone.

## 7. Option 4: no merge (every object stays an assembly)

Prototype `tools/nomerge.py` (output in `nomerge-zoo/`): each object becomes an assembly in which
every global symbol `NAME` is a public class `'<sym>NAME'` holding the function `NAME` (or the
address field `NAME`) and its `'<msil>fn'` field; TU-local symbols live in a class `'<msil>OBJ'`
together with `init1` (allocate the segment, set own address fields and function-address fields)
and `init2` (apply data relocations). Every reference to a global symbol, even to the object's own,
goes through `[msil.extern]'<sym>NAME'`. The link product is:
- `msil.extern.dll`: `.class extern forwarder '<sym>NAME' { .assembly extern '<winner>' }` for every
  symbol (16 in the zoo). Resolution (weak, COMDAT, common) happens here: the forwarder points at
  the winning assembly, losers stay as dead types in theirs. An alias is a type whose method is
  `jmp` to the target and whose `<msil>fn` copies the target's.
- `zoo4.exe`: classes for what nobody defines (the P/Invoke for `puts`, the null `maybe`), and a
  `Main` that calls every `init1`, then every `init2`, then the constructors in global priority
  order, then `main`.

Result: exit 63 on .NET 8, .NET 10 and Framework 4.8 with `a.dll` and `b.dll` assembled once and
never touched again. This beats the inputs file's variant (b), which patches the ResolutionScope
cells of placeholder TypeRefs: the facade needs no change to any object at all.

Costs and constraints (measured where marked):
- One assembly per C file: about 0.9 ms (.NET 8) and 1.1 ms (Framework 4.8) of startup per
  assembly (E11: one assembly with 200 classes min 101 / 53 ms; 200 assemblies min 280 / 268 ms).
- Global methods cannot be referenced across assemblies: a MemberRef on `[b]'<Module>'` is
  `TypeLoadException: Could not load type '<Module>'` on both runtimes (E14), so every global
  symbol must live in a public class; per-symbol classes are what forwarders need (a forwarder
  is per type).
- Address fields are written by another type's method, so they cannot be `initonly`, which Phase
  1's fields are (whether tiered JIT folds `initonly` statics of `<Module>` into constants is
  *unverified* here).
- Initialization needs a driver: lazy type initializers cannot honour constructor priorities across
  assemblies. Relocations alone are cycle-safe if each initializer sets its own address fields
  before reading another assembly's (E14: two assemblies reading each other's fields in their
  `.cctor`s gave the right values on both runtimes).
- Fits P2-C (a library assembly linked against another, like a shared library) and Design.md 5.4;
  heavy for whole programs; COMDAT and weak losers remain as dead code.

## 8. Option 5: a GOT-like function-pointer table filled at startup

Every cross-object call becomes `ldsfld slot; calli`, the slot filled at startup by `ldftn` (a
linker-generated initializer) or by name (reflection). Measured with C# function pointers
(`bench/got-Program.cs`, 400 million calls of `(x << 1) + 1`, ranges over 3 rounds and 2 sessions on
a shared host): .NET 8 inlined direct call 189-302 ms, non-inlined direct call 503-1185 ms, `calli`
through the slot 787-1577 ms; Framework 4.8: 286-449, 854-1301 and 1010-1683 ms. So `calli` costs
3-5 times an inlined call (the JIT cannot inline through it) and 1.0-1.5 times a non-inlined call.
Within one merged module the slot buys nothing (tokens are rewritten anyway); between assemblies a
MemberRef call is already bound by the CLR. The useful form already exists: Phase 1's
`<msil>fn_<name>` fields are the per-function slots for *function pointers*, and P3-B's thunk table
(Design.md 4.5) fills them.

## 9. ECMA-335 mechanisms named in the inputs file

| Mechanism | Test | Result |
|---|---|---|
| ImplMap + ModuleRef (`pinvokeimpl("msil.unresolved")`) | E1, E2, E14 | Valid object form for undefined functions (the Phase-1 shape); both ilasms, both ildasms, SRM; merging maps the stub to the definition or to a real P/Invoke. Functions only (ImplMap cannot forward fields). Run unlinked: `DllNotFoundException` naming `msil.unresolved`. |
| MemberRef with a ModuleRef parent + File + ExportedType (multi-module assembly) | E13 (`ecma/multimodule/`) | Framework 4.8: works (global method of a netmodule via `[.module mod2.netmodule]::'G'`, its module `.cctor` ran; exit 81). .NET 8 and .NET 10: `FileLoadException: ... The module cannot be loaded because only single file assemblies are supported. (0x8013101E)`. Unusable as a runtime mechanism; the module name would also have to be known at compile time. |
| ExportedType type forwarders | E14 (`ecma/forwarder/`), E10 | Framework 4.8, .NET 8 and .NET 10: a call through `[msil.extern]'add3'::'add3'` reaches `b.dll` via the facade's forwarder (exit 104 = 1 + 3 + 100 from `b`'s type initializer). Basis of option 4. |
| ENCLog / ENCMap | E15 (`carriers enc`) | MetadataBuilder writes them with the uncompressed `#-` stream; loading is `BadImageFormatException` on Framework 4.8 and "Bad IL format" on .NET 10. Precedent only, as the inputs file says. |
| CustomAttribute rows | E15 (`attr`), 4.2 | `[msil.link]'<msil>'.Weak` on a method and `.Relocs(uint8[])` on a field: ildasm (both) and SRM read them, both runtimes load the assembly and run it (attributes are resolved only when reflected). A module-level attribute carries a 2.9 KB record through ilasm and an ildasm/ilasm round trip. |
| Portable PDB CustomDebugInformation | E15 (`pdb`) | Rows on a MethodDef, a FieldDef and the Module, kind GUID chosen by us, in an embedded Portable PDB (debug directory entry): read back with SRM; both ildasms and both runtimes accept the image. ilasm cannot write it, so the `-S` path could not carry it. |
| ManifestResource | E2 (all objects) | Used by the prototype; ilasm embeds a same-named file (`.mresource private 'msil.link' { }`). |
| Extra PE section | E15 (`section`) | `.msilnk` via a ManagedPEBuilder subclass: llvm-objdump lists it, `llvm-objcopy --dump-section` extracts it, both ildasms and runtimes accept it. |
| Extra metadata stream | E15 (`stream`) | `#MSIL` added after `#Blob` (stream data moved, `#Blob` shrunk): SRM ignores it, both ildasms and both runtimes accept the image, `ildasm /metadata=validate` reports only the usual global-member warning `0x8013129d`. |
| `privatescope` members | E1, E2 | Duplicate names in one `<Module>` work on all runtimes; referenced by token only. |

## 10. C link semantics per option

(2) is the prototype's behaviour (tested unless marked); (1) is the same with a symbol table; (3)
is IR linking; (4) is the facade prototype; (5) only changes how calls are made, so it inherits
the semantics of whichever option carries the definitions.

| Semantics | (1) wasm-style | (2) symbolic (prototype) | (3) LTO-only | (4) no merge | (5) GOT |
|---|---|---|---|---|---|
| Weak definitions | symbol flag `BINDING_WEAK` | link-info bind `weak`; a losing MethodDef/FieldDef token is redirected, its body or data dropped (tested) | IRMover | forwarder to the strong definition (tested) | as host |
| Weak undefined | `UNDEFINED`+weak; trap stub (wasm-ld `createUndefinedStub`) | address and `<msil>fn_` field stay 0; direct calls bind to a `ldnull; throw` stub; no archive extraction (tested) | **backend error today**; needs lowering to null + trap stub | the driver defines the null type (tested) | slot stays 0 |
| COMDAT, `linkonce_odr`, inline functions | `WASM_COMDAT_INFO` | link-info comdat key, first copy wins, losers' rows and data dropped, their tokens and relocations redirected (tested with the MSVC string literal); selection kinds other than `any` need the record | IRMover | forwarder to the first copy; losers remain as dead types | as host |
| `static inline` / TU-local | local symbols | `privatescope` duplicates (tested) | internalized per TU, renamed by IRMover | private class per assembly | n/a |
| Hidden vs default visibility | `VISIBILITY_HIDDEN` | link-info flag; matters only for the P2-C export class (every `<Module>` member is invisible outside the assembly, E14) | IR | needs a public class per symbol, so hidden symbols are still public types | n/a |
| Aliases (GlobalAlias, rustc mergefunc) | symbol pointing at the target's segment/function | link-info alias -> target; calls bind to the target's MethodDef, `<msil>FnAddr` of the alias is the target's field (tested: `table[1] == table[2]`); data aliases need target + offset | **backend error today** (D26) | alias type with `jmp` + shared `<msil>fn` (tested) | as host |
| Common symbols | `BINDING_COMMON` (wasm-ld `addCommon`) | link-info bind `common`; largest size and alignment; a definition wins (tested) | IRMover | forwarder to one copy (the prototype keeps the first) | n/a |
| Global constructors and priorities | `WASM_INIT_FUNCS` with priority | link-info list; one sort over all objects by priority, then link order (tested: `ctor_b(100)` before `ctor_a(200)` across objects) | `appending` merge | explicit driver (tested); lazy `.cctor`s cannot order them | as host |
| TLS templates (Phase 3) | `SEG_FLAG_TLS`, `R_WASM_MEMORY_ADDR_TLS_SLEB` | link-info TLS segment per object; linker concatenates templates; per-TLS-symbol offset field set by `.cctor` (like address fields; *not prototyped*) | single module | per-assembly TLS blocks; cross-assembly TLS symbols through accessors | n/a |
| Data alignment | segment info | per-symbol alignment, base aligned to the maximum, at least 16 (tested with 4/8-aligned data and packed structs) | single module | per assembly | n/a |
| Data relocations | `R_WASM_MEMORY_ADDR_*`, `TABLE_INDEX_*` | link-info table applied by the generated `.cctor`; sites in discarded copies dropped; unaligned sites get `unaligned.` (tested: corpus `packed-relocs.c`); symbol differences need a DIFF kind (*not prototyped*) | single module | per-assembly `init2` reading other assemblies' fields through forwarders (tested) | as host |
| String literals and merging | `SEG_FLAG_STRINGS` | `merge_strings` (private `unnamed_addr` C strings) deduplicated by content (tested on linux-gnu); MSVC literals are COMDATs (tested) | ConstantMerge | none across assemblies | n/a |
| Archives, lazy extraction | lld reads members' symbol tables | prototype reads member metadata (tested); `llvm-ar` index empty unless an MSIL `SymbolicFile` exists | bitcode archives via irsymtab | n/a (each member an assembly) | as host |
| LTO with mixed bitcode and CIL objects | wasm-ld model: LTO output is an object, merged with the rest | same: lld's `BitcodeCompiler` resolutions (`Prevailing`, `VisibleToRegularObj` when a CIL object references the symbol, `lld/wasm/LTO.cpp:137-170`), LTO output = one in-memory CIL object from the MC object writer (`lld/wasm/LTO.cpp:299` creates an object file from each LTO output buffer); *not prototyped* | only bitcode | bitcode compiled per assembly | as host |
| Cross-TU signature mismatch | wasm-ld warns and uses a trapping variant | InvalidProgramException today (tested); needs a check and adapter | IR bitcasts; MSILFixFunctionBitcasts already handles it within the module | MissingMethodException at JIT (*unverified*) | calli with the wrong signature: undefined |

## 11. Recommendation for Phase 2

### 11.1 Choose option 2

- Same binding power as option 1 with less new machinery; everything the linker needs to read is
  ECMA-335, which msil-ld must read anyway for reference assemblies (P2-B) and managed inputs
  (P2-C), and write anyway for its output.
- The object is inspectable with the tools the project already uses (ildasm, ilasm, SRM,
  ILVerify on outputs), and the existing text path produces it: `clang -S` (object form) plus
  ilasm is a working `clang -c` today (section 4.1), so msil-ld can be developed and tested before
  the MC object writer exists, and the MC writer can be checked against ilasm's objects.
- LTO plugs in as in wasm-ld; option 4's facade remains available for P2-C.
- Nothing here precludes the FeatureIdeas (evaluated only for preclusion): class placement (idea 1)
  becomes a link-time decision because the linker emits every row anyway; typed pointers (idea 3)
  need the linker to bind by name and check signatures by ABI rather than by exact blob, which the
  signature-mismatch handling needs anyway; the enclosed model (idea 6) needs the linker to generate
  initialization (per instance), which option 2 already does, and option 4 would make it harder;
  several triples per assembly (idea 7) needs the triple in the link info and per-triple symbol
  namespaces, classes and initializers in the linker.

### 11.2 P2-A: what the backend and MC object writer emit

- `MSILMemberRefExpr` operands map to rows of the object: defined function -> MethodDef; undefined
  function -> MemberRef on `[msil.extern]'<msil>Symbols'` (or an ImplMap stub when the library is
  known: `msil-import-module`, D14); defined global -> its FieldDef address field; undefined global
  -> MemberRef field; `FUNC_ADDR` -> MemberRef on `'<msil>FnAddr'`; BCL member -> MemberRef on the
  BCL TypeRef (scope as today, rebound by msil-ld); `calli` -> StandAloneSig. One placeholder form
  for functions (MemberRef) keeps the linker to one lookup; the prototype shows both work.
- TU-local functions and globals: `privatescope`. Global, weak and COMDAT definitions: `public`.
- `emitEndOfAsmFile` in object mode writes the link-info record instead of `.cctor`, `Main` and the
  data image: the data already sits in `MSILDataSegment` (`Image`, `Entries`, `Zero`, `Relocs`,
  `Diffs`), the constructors in `Ctors`, the function-address uses in `FuncAddrs`. GlobalAlias and
  `extern_weak` become records instead of errors (D26), common globals keep size and alignment, and
  `MSILDiff` across two globals becomes a DIFF relocation (the linker lays out per symbol).
- Proposed record (binary, versioned, wasm "linking" section style; keyed by metadata tokens so
  names are not duplicated): header (version, triple, codegen-switch flags that must agree across
  objects); symbols (token, kind, binding local/global/weak/common, visibility, undefined, TLS,
  no-strip, comdat index, data: segment, offset, size, alignment); segments (name or section,
  alignment, flags STRINGS/TLS/RETAIN, initialized bytes, zero size); data relocations (segment,
  offset, kind ADDR64/ADDR32/FNADDR64/FNADDR32/DIFF64/DIFF32/TLS_OFFSET, symbol, second symbol for
  DIFF, addend); init and fini functions (priority, symbol); comdats (name, selection kind,
  members); aliases (alias, target, offset). Carried as a module-level custom attribute
  `[msil.link]'<msil>'.Link(uint8[])` so the `-S` form stays ilasm-assemblable (or a
  ManifestResource; all carriers of E15 work).
- Whether `clang -S` without `-c` keeps printing Phase 1's whole-assembly form (so a single file
  still runs without msil-ld) or prints the object form is a planning decision; the prototype shows
  that the object form plus msil-ld reproduces Phase 1's results on the whole corpus.

### 11.3 P2-B: what msil-ld does (the prototype's steps, section 4.3)

Read objects (ECMA reader + link info), archives (lazy, by member metadata), bitcode (LTO driver as
in `lld/wasm/LTO.cpp`), references (`-r`, spelling open) and managed inputs (P2-C); resolve with
wasm-ld's rules (weak, COMDAT, common, aliases, lazy symbols, weak undefined stubs); check
signatures (diagnose or adapter); lay out one data segment; build the output rows (kept methods,
P/Invoke stubs from native export tables or import libraries, trap stubs, address and
function-address fields); copy bodies with every token remapped and signatures re-encoded;
generate `.cctor` (Phase 1's `emitModuleInitializer` logic, now program-wide) and `'<msil>Main'`
(D27); bind every BCL TypeRef by searching the references (prototype: 43 lines,
`tools/msilobj/Refs.cs`); write the PE. The C# prototype that does all of this except LTO and
references-by-member is 726 lines (`Linker.cs`) plus 127 (`IL.cs`).

### 11.4 Shared C++ components to plan

- An ECMA-335 metadata reader (tables, heaps, coded indices, method bodies) and writer (MetadataBuilder
  equivalent), for example in `llvm/lib/Object` / `llvm/lib/BinaryFormat` (LLVM has neither today;
  *location not decided*), used by the MC object writer, msil-ld, and optionally an
  `object::MSILObjectFile : SymbolicFile` for llvm-nm/llvm-ar/llvm-objdump.
- A CIL opcode table with operand kinds (already in TableGen for the backend; needed by the
  linker's token walker).

### 11.5 Relation to the container recon ([objfmt-container.md](objfmt-container.md))

That report recommends bitcode objects (its option E) for bring-up and ELF relocatable objects (its
D) as the CIL container, with a netmodule envelope plus a link stream (its B, this report's option
2) as the alternative. The binding model recommended here does not depend on that choice:

- In every container the linker binds by name, renumbers every token through per-object maps,
  lays out one data segment and generates `.cctor` and `'<msil>Main'`; section 3's inventory and
  measurements apply unchanged.
- With **B / option 2** the IL needs no relocation records (tokens found by decoding, meaning
  taken from the object's own metadata) and the C link semantics need the link-info record of
  11.2 (prototyped here).
- With **D (ELF)** the IL token operands need the relocation kinds of section 5 against ELF
  symbols, member names and signatures need a home outside metadata, and the link-info record maps
  onto standard ELF: bind `weak`/`common`/`local` -> `STB_WEAK`/`SHN_COMMON`/`STB_LOCAL`; comdat ->
  `SHT_GROUP`; visibility -> `STV_HIDDEN`; data symbols -> `STT_OBJECT` with section, value and
  size; `merge_strings` -> `SHF_MERGE|SHF_STRINGS`; constructor priorities -> `.init_array.N`;
  aliases -> a second symbol at the same section and value; TLS -> `.tdata`/`.tbss` and `STT_TLS`;
  data relocations -> `R_MSIL_ADDR64`/`FNADDR64`/... RELA entries.
- Option E (bitcode for `clang -c` at first) meets the two backend gaps measured in section 6:
  GlobalAlias lowering and `extern_weak` lowering are needed before bitcode-only links match native
  results.
- If D is chosen, objects built by `-S` plus ilasm (this prototype's route) are still useful as a
  test vehicle for msil-ld's ECMA reader and merge code, which P2-C needs for managed inputs anyway.

## 12. Risks, open questions, not tested

- **Not prototyped:** TLS, symbol-difference relocations across objects, COMDAT selection kinds
  other than `any`, data aliases with offsets, destructors and `atexit`, `__start_`/`__stop_`
  section symbols, dead stripping (`--gc-sections`), LTO inside msil-ld, P2-C exports, PDB merging,
  member-level (not only type-level) reference binding, signature-mismatch adapters.
- **Signature compatibility** rules for binding (exact blob vs ABI-equivalent) must be decided
  before typed pointers or struct-by-value signatures arrive.
- **Names:** duplicate `privatescope` names are legal but ildasm renames them on a round trip;
  linker-generated names (`<msil>…`) must keep Phase 1's collision checks
  (`MSILAsmPrinter::checkAddressFieldNames`, `MSILAsmPrinter.cpp:589`) at link time.
- **Custom attribute blobs with `System.Type` arguments** embed assembly names as strings; P2-B's
  binding must rewrite them with the reference it chose (*unverified* in practice: no such attribute
  is emitted yet).
- **Round-trip leg** of the test harness must normalize `.data` labels for msil-ld output.
- **Option 4 for P2-C**: lazy initialization order and constructor priorities across assemblies;
  `initonly` folding (*unverified*).
- **Performance of the C# prototype** is not the C++ linker's; the measured link times only show
  that merging is cheap relative to codegen.

## 13. Reproduce

All scripts assume the scratch layout `C:\llvm-msil-scratch\p2\objfmt-binding\{tools,zoo,...}` and
the shared build at `D:\llvm-msil\build`; copy `objfmt-binding-files/tools` to `<scratch>/tools`
and `objfmt-binding-files/zoo/*.c` to `<scratch>/zoo`.

```text
cd <scratch>/tools/msilobj && dotnet build -o out                  # the C# tools (net10.0, no packages)
cd <scratch>/zoo && ../tools/mkobj.sh x86_64-pc-windows-msvc-msil a.c stub maybe=memberref
                   ../tools/mkobj.sh x86_64-pc-windows-msvc-msil b.c
dotnet ../tools/msilobj/out/msilobj.dll link -o zoo.dll --trace-relocs a.o b.o
dotnet ../tools/msilobj/out/msilobj.dll link --exe -o zoo.exe a.o b.o
dotnet exec --runtimeconfig rc8.json zoo.dll ; ./zoo.exe           # main, add3, exit 63
<scratch>/tools/zoo-all.sh       # E2-E4, E7, E9, E10 -> logs/zoo-all.log
<scratch>/tools/ecma-all.sh      # E13-E15 -> logs/ecma-all.log
NETFX=1 <scratch>/tools/corpus.sh x86_64-pc-windows-msvc-msil -O2  # E5 (about 10 min)
dotnet ../tools/msilobj/out/msilobj.dll ilstats <objects>          # section 3.2
```

`rc8.json`/`rc10.json` are minimal runtimeconfigs (`Microsoft.NETCore.App` 8.0.0 / 10.0.0).
Installs: none (the net48 build of `bench/got.csproj` restored
`microsoft.netframework.referenceassemblies.net48` 1.0.3, already in the NuGet cache). Disk: 79 MB
on C:; D: had 18 GB free at the end.
