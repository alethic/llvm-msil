# P2 recon: exports and assembly-to-assembly linking (`exports-multiasm`)

Status: recon report for Phase 2 planning (P2-C, Design.md 5.4), 2026-10-21. Base: branch `msil` at `1ea64fe9fc7c`.
Scratch: `C:\llvm-msil-scratch\p2\exports-multiasm\` (7.6 MB; no private LLVM build). Supporting files:
[exports-multiasm-files/](exports-multiasm-files/) (every experiment's IL, C, C# and Python sources, the reader tool,
`run-all.sh` and the logs `run-all.log` (Windows) and `run-linux.log` (WSL)). Installs: none (the ReadyToRun leg used
the `microsoft.netcore.app.crossgen2.win-x64` 8.0.32 package that was already in the NuGet cache; the reader tool is a
`net10.0` console app on the in-box System.Reflection.Metadata).

Inputs read: Design.md (all; 5, 6, 7, 8 and 11 in detail), ImplementationPlan.md sections 1-5, 7 and the section 12
rows of 2026-10-14/15, Status.md, `msil/orchestration/P2/inputs.md`, FeatureIdeas.md, recon `cil-runtime.md`,
`asmprinter.md` (skimmed), and the sibling P2 reports `bcl-binding.md` and `mc-streamer.md`. Every file, line and API
named below was checked in this checkout; runtime behaviour was measured on this host. Statements that rest on
documentation or memory rather than a check here are marked *unverified*.

Runtimes: Windows .NET 8.0.32, .NET 10.0.13 (`dotnet exec --runtimeconfig`, roll-forward picked these patches), .NET
Framework 4.8 (4.0.30319.42000, programs assembled as `.exe`); WSL Ubuntu .NET 8.0.31 and 10.0.13. Assembler: the
Framework ilasm (`-dll|-exe -pe64 -x64`), D31's default. Every result below holds on all of these unless a row says
otherwise.

## 0. Summary

**Findings**

1. **An export class is required, and it works.** A library's global methods are unreachable from another assembly: a
   `TypeRef` to another assembly's `<Module>` fails with `TypeLoadException: Could not load type '<Module>'` (E1). Public
   static methods of `.class public abstract sealed Contoso.Zlib.Native` are called through ordinary MemberRefs on all
   runtimes, from IL and from C# (net8 and net48 builds), and C#/ReadyToRun compilation of such a library works (E1, E6).
2. **Binding and versioning (no strong names).** The CLR finds a library only as `<assembly name>.dll` in the
   application directory: a library whose file stem differs from its assembly name is not found
   (`FileNotFoundException`; Framework: `FileLoadException 0x80131040` when the AssemblyRef names the file). CoreCLR
   accepts a library whose version is **greater than or equal** to the AssemblyRef's and fails with
   `FileNotFoundException` when it is lower (even by one revision); .NET Framework ignores versions of simple-named
   assemblies entirely. A public key token in the AssemblyRef is ignored by CoreCLR and makes Framework fail (E1).
3. **Missing members fail at JIT time of the caller, not at the call.** A missing method or a wrong signature gives
   `MissingMethodException`, a missing class `TypeLoadException`, a missing assembly `FileNotFoundException`, all
   thrown before the **first instruction of the calling method** runs, even when the call is on a path that never
   executes (E1 `cold`). A missing library referenced by the program's module initializer stops the program before
   `main` (`TypeInitializationException` wrapping `FileNotFoundException`, E7), which matches a native loader error.
4. **When a library's `<Module>::.cctor` runs.** On every runtime the CLR runs it when the JIT first resolves a token
   that refers into the library (a call, `ldftn`, `ldsfld`, `ldtoken`), i.e. before the referencing method starts and
   even if the reference is on a cold path or gets inlined; reflection `Invoke` also triggers it; `Assembly.Load` and
   `Assembly.GetType` do not (E4). ReadyToRun images (crossgen2) keep the same order (E4c, E6r2r). So **no per-export
   prologue and no explicit initialization from `'<msil>Main'` is needed for correctness**: every external use of a
   library is preceded by its initializer.
5. **Cycles break the Phase-1 initializer layout.** With two libraries that refer to each other's data, the Phase-1
   shape (address fields and allocation inside `<Module>::.cctor`) hands one of them a **null address** on every
   runtime (E4b v1). Splitting initialization into a dependency-free *phase 1* (a `beforefieldinit` class
   `'<msil>Data'` whose `.cctor` allocates the segment and sets the address fields) and a *phase 2*
   (`<Module>::.cctor`: imports, cross-assembly relocations, constructors) fixes it (E4b v2).
6. **Libraries that are never referenced are never initialized**, so constructor side effects of a linked-but-unused
   library would not happen. One `ldtoken [lib]Class` per linked library in the importer's `<Module>::.cctor`
   initializes them, depth-first and in IL order, before the importer's own initializer body and before `main`
   (E4c, also under ReadyToRun).
7. **Function-pointer identity across assemblies needs the exporter's address.** On .NET Framework an importer's
   `ldftn` on a MemberRef compiled after the target first ran through `calli` returns the native code, not the
   precode that the library's own `'<msil>fn_'` field holds: `&twice` in the program differs from `&twice` in the
   library (E2 `prog2c`, E2b; CoreCLR is stable). The importer must obtain the address from the exporter
   (`call [lib]Class::'<msil>addr_twice'()`), which Phase 3's native-callable thunks need anyway.
8. **Exported data through accessors costs nothing at run time.** Calling the library's `'<msil>addr_x'()` accessor at
   every access is as fast as a cached import field (200 M accesses: 62-306 ms either way, noise-dominated, on all
   runtimes, tiered and `TieredCompilation=0`): the JIT inlines the cross-assembly accessor (E7 bench). It is also
   cycle-safe, because the accessor only touches phase-1 state. So no GOT-like import table is needed.
9. **The symbol table attribute.** Measured for a library with 1,000 exported functions and 50 exported variables
   (base file 73,728 bytes, metadata 52,720): a self-contained binary table (names and CIL signatures) is 24,894
   bytes; one that refers to MethodDef/Property rows 3,126; the same as text 48,490; the **proposed by-name table**
   (functions implied by the export class, only data sizes/alignments and flagged functions listed) **1,119 bytes,
   +1,240 bytes of metadata (2.4%)**, and it survives an ildasm/ilasm round trip (E3). A manifest resource costs the
   same as the attribute; a custom `#MSIL` metadata stream loads on every runtime and in SRM, PEVerify, ILVerify and
   ildasm, but an ildasm/ilasm round trip drops it. Reading it back and refusing a triple mismatch takes about
   0.5 ms per 1,000 symbols with SRM.
10. **The per-export members dominate the size, not the table.** The proposed full shape (function + canonical
    address field + `'<msil>addr_'` accessor per function, property + accessor per variable) grows the 1,000-export
    library from 73,728 to 189,952 bytes, mostly because ilasm does not share string suffixes and `#Strings` crossed
    64 KB, which widens every string index (MethodDef rows 14 -> 16 bytes). A writer with suffix sharing (Roslyn's
    behaviour) would add about 15 bytes of names per function; about 60-70 bytes per export in all, comparable to an
    ELF `.dynsym` + `.dynstr` + hash entry. Publishing all 1,000 addresses eagerly with `ldftn` costs about 4 ms at
    library initialization; with Phase 3's delegate thunks it would cost 30-130 ms, so P3 must make them lazy.
11. **Global C state across assemblies is shared exactly when the C runtime is shared.** Two assemblies P/Invoking the
    same CRT (ucrtbase; glibc in WSL) share the heap (`malloc` in one, `free` in the other), `FILE *` streams and
    `errno` (E5). Two different CRTs in one process (msvcrt, as MinGW triples use, against ucrtbase) give
    `STATUS_HEAP_CORRUPTION` (0xC0000374) on a cross `free`, `AccessViolationException` on a cross `fputs`, and two
    `errno`s. Statically linked stateful helpers behave like a second CRT (one copy of the state per assembly). A
    P3 caveat found on the way: on CoreCLR any BCL work between a P/Invoke and the `errno` read (here
    `Console.WriteLine`) resets `errno` to 0; Framework kept it.
12. **Destructor order needs process-wide state.** Separate `AppDomain.ProcessExit` handlers run in subscription
    order, so a library's destructors run before the program's (wrong). One LIFO stack kept in an
    `AppDomain.SetData` slot, created under a lock on an interned string literal, gives the native order (program
    first) on every runtime without a support assembly (E5c).
13. **C# interop.** C# calls the export class directly (`nint` parameters, a data property, `delegate*<int,int>`
    through a returned function pointer), on .NET 8/10 and Framework (E6); a csc command line against the .NET 8 ref
    pack needs the `mscorlib` facade reference when the library binds to `[mscorlib]` (SDK projects include it).
    Calling a C# library from C works at the IL level with MemberRefs carrying the C# signature (`int64`, `int32*`
    fed with `native int`, E6b; PEVerify wants a `conv.i8`), but nothing names C# methods as C symbols, so it is out
    of scope for P2-C.

**Recommendations**

- R1. `-shared` output: assembly name = output file stem (or `-soname`), version from a new `--assembly-version`
  (default 0.0.0.0), no strong name. Every exported function is a `public static` method of the export class
  (`--export-class=NS.Name`, default: the assembly name), with its body there (no forwarders); hidden and internal
  functions stay `assembly` methods of `<Module>`.
- R2. Per exported function `f`: canonical address field `'<msil>fn_f'` in `'<msil>Data'` plus a public accessor
  `native int '<msil>addr_f'()`. Per exported variable `x`: a public accessor `'<msil>addr_x'()` returning its
  address and a public property `x` with that getter (C#: `Native.x`). Exported addresses are never public fields.
- R3. Two-phase initialization in every linked output (program or library): phase 1 = `'<msil>Data'::.cctor`
  (`beforefieldinit`, refers to no other assembly: allocation, image copy, zeroing, address fields, one `ldftn` per
  address-taken or exported function, intra-assembly relocations); phase 2 = `<Module>::.cctor` (one `ldtoken` per
  directly linked library in link order, cross-assembly relocations, then constructors in priority order).
- R4. Importers: a call to an exported function is `call <exact library signature> [lib]Class::f`; the address of an
  imported variable or function is `call native int [lib]Class::'<msil>addr_<name>'()` wherever code needs it (same
  5 bytes and stack effect as `ldsfld`, so a linker can patch it in place); data initializers that contain such
  addresses become phase-2 stores. Never `ldftn` on an imported function.
- R5. The symbol table is one assembly-level custom attribute `'<msil>SymbolTable'(uint8[])` on a private attribute
  class emitted into the library, in the by-name format of section 5.2, carrying the triple, the C runtime identity,
  the data-model bytes and a toolchain ABI version; msil-ld refuses any mismatch (section 5.4).
- R6. msil-ld treats linked libraries with `-Bsymbolic`/`-z defs` semantics: no interposition, no undefined symbols in
  `-shared` output, AssemblyRefs carrying the library's exact name and version, and a warning when an object also
  defines a symbol that a linked library exports.
- R7. Process-wide C state that must be shared across assemblies (the atexit/destructor list; later anything else a
  helper keeps) lives in an `AppDomain` data slot, never in a statically linked helper.
- R8. Tests: section 11 (library/program pairs on all runtimes, cycles, ordering, identity on Framework, C# consumer,
  ReadyToRun, triple/CRT mismatch refusal, missing library and member).

## 1. Method and experiment index

All experiments are hand-written ILAsm (or generated by small Python scripts) shaped like Phase-1 output, assembled
with the Framework ilasm, and run from one private directory per experiment. `common.sh` holds the assembler and runner
functions; `run-all.sh` rebuilds and reruns everything on Windows and writes `run-all.log`; the `lnx-run.sh` scripts
rerun the Linux legs (`run-linux.log`).

| Id | Directory | Question |
|---|---|---|
| E1 | `e1/` | Export class calls, a `<Module>` reference across assemblies, versioning, file-name binding, missing members |
| E2, E2b | `e2/`, `e2b/` | Exported data (accessor vs public field), `extern` data, function pointers and `ldftn` identity |
| E3 | `e3/`, `tools/symtab/` | Symbol-table encodings and sizes at 1,000 exports, reading back, triple check, resource and `#MSIL` stream alternatives, cost of publishing addresses |
| E4, E4b, E4c | `e4/`, `e4b/`, `e4c/` | When a library's module initializer runs; cyclic libraries; initialization order of linked libraries; ReadyToRun |
| E5, E5c | `e5/`, `e5c/` | Heap, stdio and errno across assemblies with one or two C runtimes; destructor order |
| E6, E6b, E6r2r | `e6/`, `e6b/`, `e6r2r/` | C# consumer of a C library; IL calling a C# library; ReadyToRun-published C# app with a C library |
| E7 | `e7/` | End-to-end prototype of the proposed P2-C shape: `zl.c` + `main.c` written by hand as msil-ld would link them, verification, accessor benchmark |

## 2. The Phase-1 baseline

What the backend emits today and what P2-C has to change (all in `llvm/lib/Target/MSIL/`):

- Every function is a global method of `<Module>`: `MSILAsmPrinter::emitFunctionEntryLabel`
  (`MSILAsmPrinter.cpp:1210-1232`) passes `Public = !F.hasLocalLinkage()` to `MSILTargetAsmStreamer::emitMethodBegin`
  (`MCTargetDesc/MSILTargetStreamer.cpp:78`, `public` or `assembly`). Visibility is ignored: a
  `__attribute__((visibility("hidden")))` function is `public` too (checked with `e0/lib.c`); that is harmless inside
  one assembly because `<Module>` is not reachable from outside (E1), but export selection needs visibility.
- Every global has an `initonly native int` address field in `<Module>` (`emitDataSegment`, `:809-837`;
  `MSILTargetAsmStreamer::emitAddressField`, `MSILTargetStreamer.cpp:191-199`), and code reads it with
  `ldsfld native int 'g'` (GLOBAL_ADDR, `MSILInstrInfo.td:223`, lowered in `MSILMCInstLower.cpp:235-249`).
- Address-taken functions get `'<msil>fn_<name>'` fields (`FuncFieldPrefix`, `MSILAsmPrinter.cpp:167`;
  `emitFunctionAddressFields`, `:839-846`) set once by `ldftn` (M9 row of plan section 12).
- `<Module>::.cctor` does everything in one method (`emitModuleInitializer`, `:848-1005`): allocation, image copy,
  zeroing, address fields, function addresses, relocations, constructors.
- An undefined global variable that is used is an error (`collectExternGlobals`, `:545-565`: "external global variable
  ... is not supported (phase 2 linker)"); undefined functions go through the D14 import table
  (`emitPInvokeDeclarations`, `:725-807`).
- One AssemblyRef (`emitAssemblyRefs`, `MSILTargetStreamer.cpp:57-62`) and `.assembly '<name>' { .ver 0:0:0:0 }`
  (`emitAssembly`, `:64-72`).

In clang's IR for the MSIL triples (`e0/vis.c`): `__declspec(dllexport)`/`dllimport` survive as IR DLL storage classes
on `x86_64-pc-windows-msvc-msil` and `x86_64-w64-windows-gnu-msil` (`define dllexport i32 @exported_fn`,
`declare dllimport i32 @imported_fn`, `@imported_data = external dllimport global i32`); on
`x86_64-unknown-linux-gnu-msil` `__declspec` needs `-fdeclspec` and the storage classes are dropped; `hidden`
visibility appears on every triple.

## 3. E1: the export class, loading, versioning and missing members

`e1/zlibx.il` is a library `zlibx` 1.2.3.4 whose `Contoso.Zlib.Native` has `add(int32, int32)` and `bump()` (which
reads C data through a `<Module>` address field and calls the hidden `<Module>` method `helper`); `e1/prog.il.in` is a
program whose test methods each print a line first and then call into the library.

| Case | Result (.NET 8, .NET 10, Framework 4.8; WSL .NET 8/10 gave the Windows CoreCLR result in every case it ran: all but `noasm`) |
|---|---|
| `call int32 [zlibx]Contoso.Zlib.Native::add(int32, int32)` | works; the library's `<Module>::.cctor` ran before the calling method's first line |
| `call int32 [zlibx]'<Module>'::helper(int32)` (a global method, even if `public`) | `TypeLoadException: Could not load type '<Module>' from assembly 'zlibx, ...'` |
| method not defined (`gone`) | `MissingMethodException: Method not found: 'Int32 Contoso.Zlib.Native.gone()'` |
| right name, wrong signature (`add(native int, int32)`) | `MissingMethodException: ... 'Int32 Contoso.Zlib.Native.add(IntPtr, Int32)'` |
| class not defined | `TypeLoadException: Could not load type 'Contoso.Zlib.Other' from assembly ...` |
| assembly not present | `FileNotFoundException` |
| missing method on a branch that never runs (`t_missing_cold(false)`) | `MissingMethodException` before the method's first line: the whole calling method fails |

In every failing case the line that the calling method prints first never appears: the exception is raised when the
caller is JIT-compiled.

Versioning (library `zlibx` 1.2.3.4, no public key; the AssemblyRef varies):

| AssemblyRef | .NET 8 / .NET 10 (Windows and Linux) | Framework 4.8 |
|---|---|---|
| `.ver 1:2:3:4` (equal) | works | works |
| `.ver 1:0:0:0` (lower) | works | works |
| no `.ver` (0.0.0.0) | works | works |
| `.ver 1:2:3:5` (higher revision) | `FileNotFoundException: Could not load file or assembly 'zlibx, Version=1.2.3.5 ...'` | works |
| `.ver 2:0:0:0` | `FileNotFoundException` | works |
| `.publickeytoken = (01 02 ... 08)`, `.ver 1:2:3:4` | works (token ignored) | `FileLoadException ... 0x80131040` |
| file `z.dll` holding assembly `zlibx`, AssemblyRef `zlibx` | `FileNotFoundException` | `FileNotFoundException` |
| file `z.dll` holding assembly `zlibx`, AssemblyRef `z` | `FileNotFoundException` | `FileLoadException 0x80131040` (manifest does not match) |

Consequences: the library's assembly name must equal its file stem; the AssemblyRef should carry the library's exact
version (CoreCLR then runs the program with that library or a newer one, the usual shared-library compatibility
direction); never emit a public key token for a toolchain-built library. Simple names are process-global (one
`zlib` per application directory, and a managed `z.dll` cannot sit next to a native `z.dll`).

## 4. E2: exported data, `extern` data and function pointers

### 4.1 Data

`e2/lib2.il` exports `counter` twice: as an accessor `'<msil>addr_counter'()` and as a public `initonly native int`
field on the export class, both holding the address of the variable in the library's unmanaged segment.
`e2/prog2.il` has its own address field `counter` set in its `.cctor` from the accessor (the C `extern int counter;`),
stores 40 through it and calls the library's `bump()`, which returns 41; the program's address equals the exported
field's. Pointers to library data are plain `native int` addresses into the library's segment: any assembly reads and
writes them, and they live forever (the segment is never freed, D12).

Both exposures trigger the library's initializer before use (E4 `field` row), but only an accessor stays correct in a
cycle (E4b) and only an accessor can later compute an address lazily (Phase 3 TLS: the address of a `thread_local`
differs per thread). C# reads a property whose getter is `'<msil>addr_counter'`: `int* c = (int*)Native.counter;`
(E6; Roslyn binds properties through MethodSemantics, so the getter's name does not matter).

The per-access cost of the accessor is nil after JIT inlining (`e7/bench.il`, 200 M reads of `*counter` from another
assembly, two rounds each, ms):

| Runtime | accessor per access | cached import field |
|---|---|---|
| .NET 8 tiered | 220-232 | 227-306 |
| .NET 10 tiered | 234-235 | 237-242 |
| Framework 4.8 | 80-149 | 93-137 |
| .NET 8 `TieredCompilation=0` | 79-99 | 62-114 |
| .NET 10 `TieredCompilation=0` | 99-101 | 76-93 |

(The spread is noise: the same configuration varies by 2x between rounds. Earlier runs gave the same picture:
221/228, 104/117, 90/97 ms.)

### 4.2 Function pointers

- `calli` on a pointer to a method of another assembly works on every runtime (managed `calli`, Phase 2's pointer
  representation), whether the pointer came from the importer's `ldftn` on a MemberRef or from the library.
- `ldftn` on a MemberRef triggers the library's initializer at JIT time like a call (E4 `ldftn`).
- **Identity** (`e2/prog2.il`, `e2/prog2c.il`, `e2b/ident*.il`):

| Comparison | .NET 8/10 (Windows, Linux) | Framework 4.8 |
|---|---|---|
| program's `ldftn [lib]twice` in its `.cctor` vs the library's `'<msil>fn_twice'` | equal | equal |
| program's `ldftn` in a method JIT-compiled after `twice` ran through direct `call`s | equal | equal |
| program's `ldftn` in a method JIT-compiled after `twice` first ran through `calli` | equal | **differ** |
| library's own `ldftn` in a late method, same condition | equal | **differ** |
| `ident2.il`: `.cctor` `ldftn` vs late `ldftn` after a `calli`, global and class methods | equal | **differ** (both) |

  This is M9's Framework finding (plan section 12, M9 row) reproduced, and it extends to MemberRefs across
  assemblies. The condition that matters is the first execution through `calli` (`ident.il`, where the target first
  runs through a direct call, stays equal). C requires `&twice` to be equal in every assembly, so an importer must
  never take the address of an imported function with its own `ldftn`; it must ask the exporter, whose single
  `ldftn` in phase 1 defines the address. Phase 3 needs the same rule for a stronger reason: a native-callable
  pointer is a thunk that only the exporter can own (Design.md 4.5).
- The addresses of **native** functions are a different case: each assembly has its own P/Invoke stub, so in Phase 2
  `&puts` differs between assemblies (two MethodDefs). Phase 3 can make them equal by using the native entry point
  (`NativeLibrary.GetExport`/`GetProcAddress`) as the address of a native function; *not tested*.

## 5. E3: the symbol-table custom attribute

### 5.1 Encodings measured

`e3/gen.py` generates a library with N = 1,000 exported functions (C-like names, average 15 bytes; 0-5 parameters of
`int32`/`native int`/`float64`) and D = 50 exported variables, in nine variants; `tools/symtab` (C#, SRM) prints the
metadata layout and reads the table back. Sizes in bytes:

| Variant | File | Metadata | `#~` | `#Strings` | `#Blob` | Table |
|---|---|---|---|---|---|---|
| `base`: export class, no table | 73,728 | 52,720 | 31,348 | 18,712 | 2,528 | - |
| `attr-a`: attribute, self-contained (kind, name, CIL signature; data size/align) | 98,816 | 77,736 | 31,408 | 18,752 | 27,444 | 24,894 |
| `attr-b`: attribute, kind + MethodDef/Property row numbers | 77,312 | 55,968 | 31,408 | 18,752 | 5,676 | 3,126 |
| `attr-t`: attribute, the same as UTF-8 text | 122,368 | 101,332 | 31,408 | 18,752 | 51,040 | 48,490 |
| **`attr-n`: attribute, by name (proposed)** | 75,264 | 53,960 | 31,408 | 18,752 | 3,668 | 1,119 |
| `res-a`: `attr-a`'s table as manifest resource `msil.symtab` | 98,816 | 52,748 | 31,364 | 18,724 | 2,528 | 24,894 (resource) |
| `fnfld`: `base` + public `'<msil>fn_f'` field per function, set by `ldftn` | 116,224 | 83,920 | 37,348 | 43,912 | 2,528 | - |
| `fnacc`: `base` + lazy `'<msil>addr_f'` accessor (Interlocked) per function | 209,920 | 136,712 | 62,884 | 71,160 | 2,536 | - |
| **`proposed`**: `attr-n` + `'<msil>fn_f'` field and `'<msil>addr_f'` accessor per function | 189,952 | 137,892 | 62,936 | 71,156 | 3,668 | 1,119 |

Observations:
- The table itself is cheap in the by-name form; the CIL signature and the name of every function are already in
  the MethodDef row, so repeating them (`attr-a`, `attr-t`) only duplicates metadata. Row references (`attr-b`) are
  smaller than `attr-a` but break when a tool reorders rows; names do not.
- ilasm does not share string suffixes, so `'<msil>addr_f'` and `'<msil>fn_f'` add the whole name twice; once
  `#Strings` passes 64 KB every string index becomes 4 bytes (MethodDef rows 14 -> 16 bytes, TypeDef 14 -> 18,
  Field 6 -> 8), which is why `#~` doubles in `fnacc` and `proposed`. msil-ld's own writer should share suffixes as
  Roslyn does (then `f` costs nothing next to `'<msil>addr_f'`) and give internal fields short names; that brings
  the cost to roughly 15 bytes of names plus a MethodDef row (14-16), a Field row (6-8), a 7-byte accessor body and
  11 bytes of phase-1 IL per function.

### 5.2 Proposed encoding (format 1)

Attribute: `.custom instance void '<msil>SymbolTable'::.ctor(uint8[])` on the Assembly row, where
`'<msil>SymbolTable'` is a private class of the library itself (`.class private auto ansi sealed beforefieldinit
extends System.Attribute`, one constructor calling `System.Attribute::.ctor()`), like Roslyn's embedded attributes; no
runtime support assembly is involved. Reflection instantiates it without error (`Assembly.GetCustomAttributes`, E3
`attrs`). Blob (ECMA-335 II.23.3): `01 00`, `uint32` length L, L table bytes, `00 00` (no named arguments).

Table, little-endian; `cu` = ECMA-335 II.23.2 compressed unsigned integer; `str` = `cu` length + UTF-8 bytes:

| Field | Encoding | Notes |
|---|---|---|
| magic | `4D 53 59 4D` ("MSYM") | |
| format version | `u8` = 1 | a reader refuses a higher major format |
| flags | `u8` | 0 |
| toolchain ABI version | `u16` | version of the internal calling convention (aggregates indirect, `sret` first, the Phase-3 variadic buffer layout); msil-ld refuses a mismatch |
| triple | `str` | e.g. `x86_64-pc-windows-msvc-msil` (environment version such as `msvc19.33.0` stripped) |
| C runtime identity | `str` | the P/Invoke library of the C runtime: `ucrtbase`, `msvcrt`, `libc` (glibc), `libc` + musl, `libSystem`; *proposed, not in the measured blob* |
| data model | 4 x `u8` | `sizeof(void *)`, `sizeof(long)`, `sizeof(wchar_t)`, `sizeof(long double)`; *proposed, not in the measured blob* |
| export classes | `cu` n, n x `str` | full names (`Contoso.Zlib.Native`) |
| data symbols | `cu` n, n x (`str` name, `cu` size, `u8` log2 alignment, `u8` flags) | flags: bit 0 const, bit 1 thread-local (P3), bit 2 weak |
| flagged functions | `cu` n, n x (`str` name, `u8` flags) | functions whose flags are not 0: bit 0 variadic (P3), bit 1 weak |

The functions are, by definition, the public static methods of the listed classes whose names do not start with
`<msil>`; their CIL signatures are their MethodDef signatures. Each data symbol `x` must have a property `x` in one of
the classes whose getter is `'<msil>addr_x'()`; each function `f` must have `'<msil>addr_f'()`. The two proposed
fields add about 15 bytes. E7's real table (`zl.dll`, one class, two variables) is 80 bytes, the whole blob 88.

### 5.3 Alternatives to the attribute

| Container | Size | Runtime | Tools | Round trip | Verdict |
|---|---|---|---|---|---|
| Assembly custom attribute (proposed) | payload + 8 + blob-length prefix; one CustomAttribute row (6-8 bytes), one TypeDef, one MethodDef, one MemberRef to `System.Attribute::.ctor` | loads everywhere | ildasm shows it as `.custom ... = ( bytes )`; reflection reads it | survives ildasm/ilasm (E3 `rt/`) | use it: only standard metadata, which msil-ld's CLI reader (bcl-binding.md section 7) reads anyway |
| Manifest resource `msil.symtab` | payload + 4; a ManifestResource row (12-14 bytes) and the CLI resources directory | loads everywhere | ildasm writes it as a file, `GetManifestResourceStream` reads it | survives ildasm/ilasm (ildasm writes the file, ilasm re-embeds it) | workable; needs the resources directory in msil-ld's PE writer and reader for nothing gained |
| Custom metadata stream `#MSIL` (Portable-PDB-like) | payload + 12-byte stream header | **loads on .NET 8, .NET 10 and Framework** (`e3/addstream.py` moved the metadata into a new section with a sixth stream; the E1 program ran unchanged) | SRM, PEVerify `/md` and `/il`, ILVerify and both ildasms accept it (same messages as without it) | **dropped** by ildasm/ilasm (verified) | not recommended: invisible to reflection and ildasm, lost by the Design.md 5.5 round trip |

### 5.4 Reading it back in msil-ld

1. Open the PE (`COFFObjectFile`), find the COR20 header and the metadata (the CLI reader proposed in
   bcl-binding.md 7.2-7.4). No CLI header: a native DLL (P/Invoke resolution, P2-B).
2. Find the CustomAttribute rows whose Parent is the Assembly row (HasCustomAttribute coded index, tag 14, row 1) and
   whose Type is a MethodDef `.ctor` owned by the TypeDef `<msil>SymbolTable` with an empty namespace; decode the
   blob. None: the assembly was not built by this toolchain; error "cannot link against 'X.dll': not built by this
   toolchain" (it may still serve as a BCL reference for P2-B).
3. Check, in this order, with one error each: format version; toolchain ABI version; triple key = arch, OS,
   environment without its version, object format (vendor ignored; `tools/symtab` implements this rule and refused
   `x86_64-unknown-linux-gnu-msil`, `x86_64-w64-windows-gnu-msil` and `aarch64-pc-windows-msvc-msil` against an
   `x86_64-pc-windows-msvc-msil` library); C runtime identity (E5 shows why msvcrt against ucrtbase must be refused);
   data-model bytes.
4. Enumerate the classes' methods and properties into the symbol map: name -> (class, kind, MethodDef signature
   blob, data size/alignment, flags). Missing accessors or properties: "malformed export table".
5. Also check the library's own AssemblyRefs against the link's references (bcl-binding.md 5.9).

Cost: the C# reader decodes and enumerates 1,050 symbols in 0.45-1.25 ms (200-iteration average, string allocation
included); `#MSIL` and resource variants are the same. Not a factor.

### 5.5 Cost of publishing every exported function's address

`e3/gentime.py`: time of the first call into a library with 1,000 exported `void()` functions, library load and
initializer included (three runs; the first run of a series on .NET 8 had a one-off cold-start outlier of about 1 s,
ignored):

| `<Module>::.cctor` per function | .NET 8 | .NET 10 | Framework 4.8 |
|---|---|---|---|
| nothing | 1.8-2.9 ms | 1.4-2.9 ms | 2.0-3.2 ms |
| `ldftn` + `stsfld` (Phase 2 shape) | 5.5-9.6 ms | 5.0-7.5 ms | 5.7-7.4 ms |
| `Action` delegate + `Marshal.GetFunctionPointerForDelegate`, kept alive (Phase 3 Framework thunk) | 31-52 ms | 28-46 ms | 37-137 ms |

So eager `ldftn` is fine for Phase 2 (about 4 us per export); Phase 3's default delegate thunks must be created lazily
inside `'<msil>addr_f'` (an `Interlocked.CompareExchange` on the field keeps one value; `fnacc` measures that body).

## 6. E4: module initializers across assemblies

### 6.1 When the CLR runs a library's `<Module>::.cctor`

`e4/lib4.il`'s initializer prints a line; `e4/prog4.il` runs one scenario per process; each scenario method prints
`entered` first.

| Program does | Library initializer runs |
|---|---|
| calls `pure()` (touches no library static) | before `s_pure entered` |
| calls `pure()`, `TieredCompilation=0` (the call is inlined) | before `s_pure entered` |
| calls `data()` (reads a `<Module>` field) | before `s_data entered` |
| `ldftn [lib]pure_print`, prints, then `calli` | before `s_ldftn entered` |
| `ldsfld [lib]Native::pub_g` (a public field) | before `s_field entered`; the field is already set |
| `ldtoken [lib]Native` + `Type.GetTypeFromHandle` | before `s_typeof entered` |
| `Assembly.Load("lib4")` | never |
| `Assembly.Load`, `GetType("L.Native")`, `GetMethod("pure")` | not yet; it runs inside `MethodBase.Invoke` |
| a method whose only call into the library is on a branch that is not taken | before `s_cold entered` |
| nothing | never |

Identical on .NET 8, .NET 10, Framework 4.8 and Linux .NET 8/10. The trigger is the JIT's resolution of a token into
the library (the first time any method referring to it is compiled), not assembly load and not the call itself. A
ReadyToRun-compiled program and libraries (crossgen2 8.0.32, `e4c/r2r/`, no method JIT-compiled according to
`DOTNET_JitDisasmSummary`) give the same order, and a ReadyToRun-published C# app with a crossgen2-compiled C library
saw the library's constructor before its first read (`e6r2r`). An exception in the initializer surfaces as
`TypeInitializationException: The type initializer for '<Module>' threw an exception` in the calling method, again at
its JIT time (observed in E1 when a first draft of the library had a `.maxstack` that was too small).

### 6.2 Cycles (E4b)

Libraries `a` and `b` each define a variable and initialize a pointer to the other's (`A.pb = &b_var`,
`B.pa = &a_var`, applied in each `<Module>::.cctor` through the other's accessor). The program calls `A`:

- **v1, Phase-1 layout** (address fields and allocation in `<Module>::.cctor`): `B: module initializer begins/ends`,
  then `A: ...`, then `A.peer == &b: ok`, **`B.peer == &a: WRONG (stale null address)`**. JIT-compiling A's
  initializer resolves B, so B's initializer runs first; it calls A's accessor, whose `<Module>` initialization is
  already in progress on this thread, so the CLR lets it read A's not-yet-set field (0).
- **v2, two phases** (allocation and address fields in a `beforefieldinit` class `'<msil>Data'`, relocations in
  `<Module>::.cctor`): `B: module initializer begins`, `B: data segment allocated`, `A: data segment allocated`, `B
  ... ends`, `A ... begins/ends`, both checks `ok`.

Same on every runtime. What a cycle can still observe: the other library's *phase 2* (its cross-assembly relocations
and constructors) may not have run when one library's constructors call into it; native cyclic libraries leave the
constructor order unspecified too, but they have all relocations applied first. Two threads entering a cycle from both
ends rely on the CLR's type-initialization deadlock breaking (ECMA-335 II.10.5.3.3); *not tested*.

### 6.3 Order, and libraries nobody calls (E4c)

`a` links `c`, `b` links nothing, the program links `a` and `b` (`e4c/gen.py`). Each `<Module>::.cctor` prints
`constructors run`.

| Program's `<Module>::.cctor` | Output |
|---|---|
| does not reference the libraries | `prog: module initializer begins/ends`, `main entered` (no library initializer ever runs) |
| `ldtoken [a]A.Native; pop; ldtoken [b]B.Native; pop` | `c`, `a`, `b`, `prog begins`, `prog ends`, `main entered` |
| the same in the order `b`, `a` | `b`, `c`, `a`, `prog ...`, `main entered` |

All initializers of directly linked libraries run before the program's initializer body and before `main`,
dependencies first (`c` before `a` because `a`'s initializer references `c`), siblings in IL order. ReadyToRun gives
the same output.

### 6.4 The rule (proposed)

1. Every linked output (program or library) has two initializers. **Phase 1** = `'<msil>Data'::.cctor`
   (`beforefieldinit`): allocation, image copy, zeroing, all address fields, the `'<msil>fn_'` fields (one `ldftn` per
   address-taken or exported function), relocations whose targets are in the same assembly. It must contain no
   token of another assembly (one would activate that assembly, whose phase 2 might call back). Phase-1 state is
   reached only through `'<msil>Data'` fields, so it runs before any use, cycles included.
2. **Phase 2** = `<Module>::.cctor`: one `ldtoken [lib]ExportClass` + `pop` per directly linked library, in link
   order; then cross-assembly relocations (`<site> = call [lib]Class::'<msil>addr_x'()`); then the constructors in
   priority order (D12's order). Keeping the body in a method `'<msil>init'` called from `<Module>::.cctor` lets
   FeatureIdeas 7 call it per variant later.
3. No prologue in exports and no explicit initialization from `'<msil>Main'`: the CLR runs the library's phase 2
   before the first method that refers to the library starts (6.1), and its phase 1 before the first data access.
4. Destructors and `atexit` (P3) are pushed onto the process-wide LIFO of 7.3, not onto per-assembly
   `ProcessExit` handlers.

Plan impact: Status.md section 4 item 4 and `msil/utils/msil-verify.py:42-46, 196-200, 351, 588` accept PEVerify's
"Cannot modify an imaged based (RVA) static" only at `ldsflda valuetype '<msil>image_t' '<msil>image'` in
`<Module>::.cctor`; if the image field moves into `'<msil>Data'` the listing shows
`'<msil>Data'::'<msil>image'` (E7's PEVerify run reports it in `<msil>Data::.cctor`). Simplest: keep the RVA field in
`<Module>` with `assembly` (not `private`) access so the text stays the same.

## 7. E5: C library state shared between assemblies

### 7.1 One C runtime

`e5/gen.py ucrtbase ucrtbase` (Windows) and `libc libc` (WSL glibc): the library and the program each P/Invoke the C
runtime themselves.

| Step | Windows ucrtbase (.NET 8/10, Framework) | Linux glibc (.NET 8/10) |
|---|---|---|
| library `malloc` + `strcpy`, program reads the string and calls its own `free` | works | works |
| library `fopen(..., "w")`, program `fputs` into that `FILE *`, library `fclose` | file contains the program's text | same |
| library makes `strtol` set `ERANGE`; program reads `*_errno()` / `*__errno_location()` immediately | 34 | 34 |
| program stores 7; library reads | 7 | 7 |
| after a `Console.WriteLine`, program reads | **0** on .NET 8/10, 7 on Framework | **0** |

Nothing breaks across assemblies: the native C runtime is loaded once per process, so its heap, streams and per-thread
`errno` are shared, as between native shared libraries. (`libc` and `libc.so.6` as P/Invoke names reach the same glibc
for heap and stdio.) The last row is a Phase-3 issue, not a cross-assembly one: CoreCLR's own native code clobbers the
thread's `errno` during BCL work, so generated code must read `errno` right after the P/Invoke that set it (or the
runtime's `Marshal.GetLastSystemError`-style support, *unverified*).

### 7.2 Two C runtimes, or statically linked helpers

`e5/gen.py msvcrt ucrtbase`: the library uses `msvcrt.dll` (what `*-windows-gnu-msil` imports today, D14) and the
program `ucrtbase.dll`:

| Step | Result on every runtime |
|---|---|
| program `free`s the library's `malloc` block | process killed, `0xC0000374` (STATUS_HEAP_CORRUPTION) |
| program `fputs` into the library's `FILE *` | `AccessViolationException` (fatal on .NET 8/10; unhandled on Framework) |
| `errno` | separate: the program reads 0 after the library set 34, the library still reads 34 after the program stored 7 |

Statically linking a C runtime or any stateful helper into each assembly has the same effect (one copy of the state
per assembly; the toolchain's own static helpers are hidden and per assembly by design, section 6 of Design.md).
Rules for P2-C: (a) refuse to link assemblies with different C runtime identities (5.4); (b) helpers linked
statically must be stateless or keep shared state in a process-wide BCL location; (c) helper symbols are always
hidden, never exported.

### 7.3 Destructor order (E5c)

`dlib` and `dprog` each have one destructor; `dprog`'s initializer references `dlib` first.

| Mechanism | Order at exit (all runtimes) |
|---|---|
| each assembly subscribes its own `AppDomain.ProcessExit` handler in its initializer | `dlib: destructors`, then `dprog: destructors` (subscription order: wrong) |
| each pushes an `Action` onto one `System.Collections.Stack` kept in `AppDomain.SetData("msil.atexit")`, created under `Monitor.Enter` on the interned literal `"msil.atexit"`; the creator subscribes the one handler that pops and runs them | `dprog: destructors`, then `dlib: destructors` (native order) |

This answers inputs.md's open question on atexit order across output assemblies for the ProcessExit path; the BCL
members used (`AppDomain.GetData`/`SetData`/`add_ProcessExit`, `Monitor`, `Stack`, `Action`) are in `mscorlib` on
Framework and forwarded on .NET (*the `[mscorlib]`-safe list of bcl-binding.md 2.4 should include them*).

## 8. E6: assemblies not built by this toolchain

### 8.1 C# calling a library (useful; works)

`e6/zlibc.il` (the proposed shape: attribute, `'<msil>Data'`, export class with functions, a data property and
`'<msil>addr_'` accessors) and `e6/use.cs`:

```csharp
Console.WriteLine(Native.add(2, 3));                         // 5
int* counter = (int*)Native.counter; *counter = 40;          // data property -> address
Console.WriteLine(Native.bump());                            // 41
delegate*<int, int> f = (delegate*<int, int>)Native.get_twice();
Console.WriteLine(f(21));                                    // 42
Native.length(Marshal.StringToHGlobalAnsi("hello, world"));  // 12
```

Built with the SDK's Roslyn `csc` against the .NET 8 reference pack (runs on .NET 8 and 10) and against the Framework
4.8 reference assemblies (runs on Framework): same output, the library's constructor first. Notes: C# sees
`abstract sealed` as a static class, `native int` as `nint` (`IntPtr` before C# 9), the `'<msil>...'` methods are not
nameable in C# (reflection finds them); a plain csc command line against the .NET 8 pack needs `-r:mscorlib.dll` (the
facade) because the library binds to `[mscorlib]` (CS0012 otherwise); an SDK project (`e6r2r/r2r.csproj`) needs
nothing. `dotnet publish -p:PublishReadyToRun=true` compiles the C library with crossgen2 as well (its CLI flags became
`0x4`, a ReadyToRun header appeared) and the program still saw `counter == 100`, i.e. the constructor ran first.

### 8.2 C calling a C# library (not required; what blocks it)

`e6b/cprog.il` calls `Contoso.Managed.Api` from `e6b/cslib.cs` the way msil-ld would have to: `Add(int64, int64)` and
`Sum(int32*, int32)` with `native int` arguments, `Twice(native int)`. It runs on every runtime (the JIT accepts
`native int` for `int64` and `int32*`); PEVerify reports `[found Native Int][expected Long]`, so a `conv.i8` would be
needed. Blockers: the C# assembly has no symbol table, so nothing maps C names to members (overloads, nested classes,
instance members); the C prototype does not determine the CIL signature (`long` vs `int64`, typed pointers, `bool`,
`char`); managed parameter types (`string`, arrays, `ref`, generics, structs without explicit layout) have no C
equivalent; and the C# assembly binds to a framework the link must accept (bcl-binding.md 5.9). A later mechanism
could be an explicit import declaration (an attribute such as `__attribute__((msil_import("asm", "Type", "Member")))`
or a linker map file) that names the member and lets msil-ld adapt scalar arguments. Not part of P2-C.

## 9. E7: end-to-end prototype of the proposed shape

`e7/zl.c` (library: exported `counter`, `pcounter = &counter`, internal `calls`, hidden `helper`, exported `twice`,
`bump`, `get_twice`, `apply(int (*)(int), int)`, a constructor that calls `puts` and sets `counter = 100`) and
`e7/main.c` (program: `extern int counter, *pcounter;`, `int *pc = &counter;`, `int (*ft)(int) = twice;`, a static
`add1` passed to `apply`) were translated by hand into `zl.il.in` and `main.il` exactly as R1-R5 describe:
`<msil>SymbolTable` (80-byte table built by `symtab.py`), `'<msil>Data'` phase 1, export class
`Contoso.Zl.Native` with bodies, properties and `'<msil>addr_'` accessors, `helper` and the constructor as `assembly`
methods of `<Module>`, phase 2 in `<Module>::.cctor`; the program's phase 2 does `ldtoken [zl]...`, stores
`&counter` and `&twice` through the library's accessors, and `main` reads `counter` through
`'<msil>addr_counter'()`.

`main` checks six C facts (constructor ran before `main`; `pc == &counter == pcounter`; `ft == get_twice() ==
&twice`; `ft(21) == 42`; `apply(add1, 41) == 42` (a program pointer called by the library); `*pc = 7; bump() == 8`)
and returns a bit mask: **0 on .NET 8, .NET 10 and Framework 4.8, tiered and `TieredCompilation=0`**. Without `zl.dll`
the program stops before `main` (6.1). The reader decodes `zl.dll`'s table into the 4 functions and 2 variables.

Verification (D21 tools, PEVerify from the Framework 4.8 SDK, ILVerify 10.0.12 with `-r zl.dll`): every message is in
an existing allow-list category or note: `StackByRef` with `[found Native Int]`, `Unverifiable` (cpblk/initblk),
`InitLocals`, ILVerify's `ImportCalli not implemented`, PEVerify's `0x8013129d` metadata warnings for global methods,
and the RVA-image note now located in `<msil>Data::.cctor` (6.4). Cross-assembly MemberRefs add no new category;
ILVerify and the neutralized second pass of `msil-verify.py` must be given the linked libraries as references.

## 10. Proposed P2-C design

### 10.1 What `-shared` produces

- **Identity.** Assembly name = stem of `-o` (or `-soname`/`--assembly-name`; warn when it differs from the file
  stem, which the CLR requires, E1); `--assembly-version=a.b.c.d` (default 0.0.0.0); no public key.
- **Export selection.** ELF-like triples: every definition with external linkage and default visibility
  (`-fvisibility=hidden` and `visibility("default")` work as natively). Windows triples: if any input defines a
  `dllexport` symbol, exactly the `dllexport` ones (link.exe); otherwise all default-visibility definitions (GNU ld's
  auto-export rule for PE DLLs). Symbols from archive members follow the same rule; `--exclude-libs` hides archive
  symbols. Names starting with `<msil>` and the reserved `.ctor`/`.cctor` are never exported.
- **Export class.** `--export-class=NS.Name` (repeatable later for FeatureIdeas 1; default: the assembly name as a
  class without namespace). `.class public abstract sealed auto ansi NS.Name extends [core]System.Object`, no `.cctor`.
  Exported function bodies are methods of this class (their MethodDef rows are simply owned by its TypeDef), so the
  library's own calls cost the same as today. Everything else stays in `<Module>` with `assembly` access.
- **Members per export.** Function `f`: method `f` (its normal signature) and `public static native int
  '<msil>addr_f'()` returning `'<msil>Data'::'<msil>fn_f'`. Variable `x`: `public hidebysig specialname static native
  int '<msil>addr_x'()` and `.property native int x() { .get ... '<msil>addr_x'() }`.
- **Initialization.** 6.4.
- **Symbol table.** 5.2, on the Assembly row; the attribute class is private and emitted into the library.
- **No entry point** even if `main` is defined (it is exported like any function).

### 10.2 Linking against a library

- **Inputs.** A `.dll` on the command line or found by `-l` (Design.md 5.2 order: `libfoo.a`, then a managed `foo.dll`
  *with* a symbol table, then a native library). A managed DLL without the attribute is not linkable as C (5.4 step 2).
- **Resolution.** Objects and archives first (Design.md 5.2.1), then assemblies in command-line order, then native
  libraries. A symbol exported by two linked assemblies binds to the first, with a warning naming both. An object that
  defines a symbol a linked library also exports wins for the importer, but the library keeps its own copy (no
  interposition, `-Bsymbolic` semantics): warn.
- **Code shapes** (all patchable in place, see 10.5):

| Reference to imported symbol | Emitted |
|---|---|
| call of function `f` | `call <the library's MethodDef signature> [lib]NS.Name::f` (MemberRef). The caller's CIL signature must equal it; otherwise an error, or a bitcast wrapper as MSILFixFunctionBitcasts makes |
| address of function `f` | `call native int [lib]NS.Name::'<msil>addr_f'()` (never `ldftn`, 4.2) |
| address of variable `x` (+ offset) | `call native int [lib]NS.Name::'<msil>addr_x'()` (then `ldc.i4 off; add` as today) |
| data initializer containing `&x` or `&f` | phase-2 store in `<Module>::.cctor` |
| the library itself | AssemblyRef with its exact name and version, no token; `ldtoken [lib]NS.Name; pop` in phase 2 |

- **Checks** (errors unless noted): triple key, C runtime identity, data model, ABI version, format version; unresolved
  symbol; signature mismatch; an imported variable whose declared type is larger than the exported size (warning, as
  ELF linkers warn on size changes); a variadic flag mismatch (P3); the library's AssemblyRefs versus the references
  (bcl-binding.md 5.9).
- **`-shared` outputs** must resolve every symbol (`-z defs` behaviour): an assembly has no global symbol scope, so a
  library cannot leave a symbol for its eventual program (plugins call host APIs through a host library instead).
- **Deployment** is not the linker's business, but the rules are: libraries next to the program (`dotnet` probes the
  application directory without a `deps.json`; Framework probes the application base); `-rpath` has no equivalent
  (warn and ignore).

### 10.3 Options

| Tool | Option | Meaning |
|---|---|---|
| msil-ld | `-shared` | library output (10.1) |
| msil-ld | `--export-class=<NS.Name>` | export class (default: assembly name) |
| msil-ld | `--assembly-name=<name>`, `-soname <name>` | assembly name (default: `-o` stem) |
| msil-ld | `--assembly-version=<a.b.c.d>` | version (default 0.0.0.0) |
| msil-ld | `--exclude-libs=<list>|ALL` | do not export archive members' symbols |
| msil-ld | `--export-all-symbols` | Windows triples: export default-visibility symbols even with `dllexport` present |
| clang driver | `-shared`, `-Wl,...`, `.dll` inputs, `-l` | passed through; a managed `.dll` input is a library to link, as Design.md section 8 says |

### 10.4 What P2-A and P2-B must provide

- A **symbolic address operation** in objects for every global and function address (GLOBAL_ADDR/FUNC_ADDR of a
  symbol that may be undefined), which msil-ld lowers to `ldsfld '<msil>Data'::x` (defined in the output) or
  `call [lib]...'<msil>addr_x'()` (imported). Both are 5 bytes (`7E <token>` and `28 <token>`) with the same stack
  effect (0 -> 1), so whichever P2-A container wins, the linker only rewrites the opcode byte and the token; IL sizes
  and branch offsets do not change. Calls are `28 <token>` for a MethodDef, a MemberRef or a P/Invoke stub alike.
- **Relocations as data** (or per-object initializer parts split into local and cross-assembly parts), so msil-ld can
  place relocations to imported symbols in phase 2 (mc-streamer.md R5 keeps per-object segments; that works as long
  as the linker, not the object, decides the phase).
- The **IR DLL storage class and visibility** of each definition in the object's symbol table (export selection).
- The **writer** (P2-B): suffix sharing in `#Strings`, Property/MethodSemantics tables, CustomAttribute on the
  Assembly row, method ownership by the export class (contiguous MethodList), and the CLI reader (bcl-binding.md 7)
  extended with Property, PropertyMap, MethodSemantics and the attribute blob.

### 10.5 Limitations to document

- No symbol interposition across assemblies; one copy per assembly of a symbol defined in both (warned).
- Cyclic libraries: phase 1 is always complete, phase 2 (cross-assembly relocations and constructors) of the library
  that was entered first may not have run when the other's constructors call it.
- In Phase 2 the address of a *native* function differs between assemblies (each has its own P/Invoke stub).
- A library whose member disappears fails at the caller's JIT time (`MissingMethodException`), not at link time of an
  already linked program; CoreCLR also refuses an older library version (`FileNotFoundException`).
- Two simple-named assemblies with one name cannot coexist in one application.

## 11. Tests for P2-C

Lit tests under `lld/test/MSIL/` (msil-ld) on small objects or IR, checked with the CLI dump of the reader WP
(`llvm-readobj` or a test tool) and, where `%if ilasm`, with ildasm text:

1. `-shared` layout: export class name and namespace, method ownership, `<Module>` keeps hidden/static functions,
   accessor and property per variable, accessor per function, no entry point, assembly name/version options.
2. Export selection: default visibility, `-fvisibility=hidden`, `dllexport`-only on Windows triples, auto-export
   without `dllexport`, `--exclude-libs`, reserved names.
3. The attribute: exact blob bytes for a small library (golden hex), round trip through the reader, ildasm/ilasm
   round trip keeps it.
4. Importer shapes: MemberRef with the library's signature, `'<msil>addr_'` calls for data and function addresses,
   phase-2 stores for initializers, `ldtoken` per library in link order, AssemblyRef name/version without token.
5. Negative: triple mismatch (each triple key part), C runtime mismatch (`windows-gnu` vs `windows-msvc`), ABI and
   format version, assembly without attribute, native DLL vs managed DLL detection, unresolved symbol in `-shared`
   output, signature mismatch, duplicate exports across libraries (warning), object-vs-library duplicate (warning),
   size mismatch (warning), every diagnostic without `LLVM ERROR`.

Execution tests (a new `msil/test/P2C/` tier, harness extended to build several assemblies into one private
directory), each at `-O0`/`-O2` on .NET 8 (tiered and `TieredCompilation=0`), .NET 10, Framework 4.8 and WSL .NET 10:

6. E7's pair compiled from `zl.c`/`main.c` by the real toolchain (calls, data, `extern` data, both relocation kinds,
   pointer identity both ways, constructor before `main`).
7. Framework identity: the `prog2c` sequence (target first called through `calli`, then `&f` taken in the program).
8. Cycles: E4b's pair, plus a three-library cycle.
9. Order: E4c's tree (dependency first, link order, unused-but-linked library initialized).
10. Missing library and missing member: expected exception names in the transcript (a run that is expected to fail).
11. Shared C state: heap/stdio/errno across a library and a program on the host CRT (E5 one-runtime rows; the errno
    read placed directly after the setter until P3 handles it).
12. C# consumer: a csc-built program against a toolchain library (net8 and net48 legs), as E6.
13. Optional host-only legs: crossgen2 ReadyToRun of program and libraries (E4c/E6r2r), D21 verification with the
    linked libraries passed to ILVerify and the neutralized pass.

## 12. FeatureIdeas: what these choices could preclude

- **1 (class placement).** Not precluded: the table lists classes and the importer binds `[lib]Class::member` per
  symbol, so exports may live in several classes; a mapping file or attribute only changes which TypeDef owns a
  method. Keep `'<msil>addr_'` accessors in the same class as their symbol.
- **3 (typed pointers).** Not precluded *if* importers copy the library's MethodDef signature into the MemberRef
  (10.2) instead of recomputing it from their own IR; `native int` and `T*` are interchangeable on the stack.
- **6 (enclosed instance model).** Exports would be instance members and accessors per instance; the format version
  and the per-symbol flags byte leave room for an "instance" kind. Process-wide state (7.3) conflicts with
  enclosure by definition; an enclosed variant would keep its own list.
- **7 (several triples in one assembly).** Not precluded if (a) the attribute may occur once per variant, each naming
  its own classes and triple (the reflection behaviour of several instances of an attribute without
  `AttributeUsage(AllowMultiple = true)` is *unverified*), (b) phase 1 is per variant (`'<msil>Data'` named per
  variant), and (c) phase 2 lives in a method (`'<msil>init'`) that a variant's own initializer can call instead of
  `<Module>::.cctor` ("run on first use of that variant"). A single `<Module>::.cctor` that initializes everything
  would preclude (c); 6.4 item 2 avoids that.
- **4/5 (managed memory, escape analysis).** Signatures with `ref`/`Span<T>` parameters are carried by the MethodDef
  signatures, not by the table; nothing in P2-C assumes scalar-only exports beyond the current codegen.

## 13. Risks and open questions

- **JIT-time activation is observed behaviour.** ECMA-335 II.10.5.3.1 only promises the module initializer "at, or
  sometime before, first access"; CoreCLR, Framework and crossgen2 agree today. Mono/Unity, NativeAOT and the .NET
  interpreter were *not tested*; the per-library `ldtoken` and the two phases keep correctness as long as a runtime
  initializes before first access.
- **Cross-thread cycles** rely on the CLR's deadlock breaking in type initialization; *not tested*.
- **errno clobbering on CoreCLR** (7.1) is a Phase-3 design input.
- **Name collisions:** a toolchain library named like a native DLL (`z.dll`) cannot be deployed next to it.
- **Size:** about 60-70 bytes per export with a suffix-sharing writer; libraries with tens of thousands of exports
  should be measured (index widening beyond 64 K rows of MethodDef doubles more columns).
- **Open:** default export class name (assembly name vs a fixed `Exports`); whether `-soname` should set the assembly
  name; whether exported `const` data needs a flag at all; how the driver spells `--export-class`
  (`-Wl,--export-class=` per Design.md 5.4 is assumed).

## 14. Work-package ideas (disjoint files)

1. **Two-phase initializer and export-class emission in msil-ld** (`lld/MSIL/` writer files for type layout and
   synthesized methods; the backend's `-S` text keeps the Phase-1 shape for a single TU).
2. **Export selection and `-shared` options** (`lld/MSIL/Options.td`, `lld/MSIL/Driver.cpp` parts, export table
   builder `lld/MSIL/Exports.{h,cpp}`).
3. **Symbol-table attribute writer and reader** (`llvm/include/llvm/BinaryFormat/MSILSymbolTable.h` +
   `llvm/lib/BinaryFormat/MSILSymbolTable.cpp`: encode/decode format 1 and the triple key; unit tests in
   `llvm/unittests/BinaryFormat/`), shared by msil-ld and a dump tool.
4. **Import resolution in msil-ld** (`lld/MSIL/InputAssembly.{h,cpp}`: open a library through the CLI reader, checks of
   5.4, symbol map; `lld/MSIL/Relocations` part that rewrites symbolic addresses to accessor calls and emits phase-2
   stores and `ldtoken`s).
5. **Driver** (`clang/lib/Driver/ToolChains/MSIL.cpp`: `-shared`, managed `.dll` inputs, `-l` lookup preferring a
   managed library with a table; driver tests in `clang/test/Driver/msil-*.c`).
6. **Tests**: lit tests (section 11 items 1-5) and the execution tier (items 6-13), including harness support for
   multi-assembly directories and expected-failure runs (`msil/utils/msil-run.py`, `msil/test/P2C/`), and
   `msil-verify.py` support for linked references and the moved RVA note.
7. **Docs**: Design.md 5.4 wording proposals (accessors as properties, two-phase initialization, no interposition,
   C runtime identity in the table) for the user, GettingStarted and Status updates.

## 15. Reproduction

```text
# Windows (Git Bash), from C:\llvm-msil-scratch\p2\exports-multiasm (copy exports-multiasm-files there first)
dotnet build -c Release -o tools/symtab/bin tools/symtab          # the reader (net10.0, in-box SRM)
python e3/gen.py <variant> 1000 50  (in e3/out-<variant>/), python e3/addstream.py, python e3/gentime.py 1000 (in e3/time/)
python e4b/gen.py <1|2> (in e4b/v<1|2>/), python e4c/gen.py, python e5/gen.py <libcrt> <progcrt> <dir>, python e5c/gen.py
bash run-all.sh > run-all.log                                       # rebuilds every IL experiment and runs .NET 8/10/Framework
# WSL legs
MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash /mnt/c/llvm-msil-scratch/p2/exports-multiasm/e1/lnx-run.sh   (also e4/, e4b/, e5/lnx-run.sh <dir>)
# ReadyToRun legs
crossgen2.exe (microsoft.netcore.app.crossgen2.win-x64 8.0.32, tools/) <in> -o r2r/<in> -r "<.NET 8.0.32 shared dir>/*.dll" -r "*.dll" --targetos windows --targetarch x64 -O
dotnet publish -c Release -o pub   (in e6r2r/, PublishReadyToRun=true)
```
