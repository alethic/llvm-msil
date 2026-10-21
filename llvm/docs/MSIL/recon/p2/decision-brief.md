# Phase 2 decision brief: object format, linker and plan

- Date: 2026-10-21. Checkout: branch `msil` at `1ea64fe9fc7c`. Key: `decision-brief` (the Phase 2 synthesizer).
- Built from 11 recon reports and 3 judge reports in this directory (index in section 7). No new experiment was
  run for this brief. Every number comes from a linked report. The source lines cited here were re-read in this
  checkout: `clang/lib/Driver/ToolChains/Clang.cpp:5540-5543`, `llvm/lib/MC/TargetRegistry.cpp:93-96`,
  `llvm/lib/MC/MCAsmBackend.cpp:60-61`, `MSILAsmPrinter.cpp:848/1007/1148`, `MSILPrepareIR.cpp:1102-1106`,
  `clang/lib/Parse/ParsePragma.cpp:475-480`, `llvm/lib/Object/COFFObjectFile.cpp:984-985`,
  `llvm/lib/Object/SymbolicFile.cpp:43`, `lld/ELF/Symbols.cpp:541-546` and `lld/Common/DriverDispatcher.cpp:31-38, 83-95`.
- Reading order: section 0 (decisions, about 4 minutes), section 1 (object format, about 4 minutes) and section 2
  (milestones, about 2 minutes). Sections 3-7 are reference material.

---

## 0. Decisions

### 0.1 The decision that shapes Phase 2

**Q1. CIL object format and binding.**

**Recommendation: B2.**
- A CIL object is an ECMA-335 **netmodule**: a PE32+ envelope with no Assembly row.
- Definitions are ordinary metadata rows.
- Cross-object references are named metadata references: MemberRefs on placeholder types, or ImplMap stubs on
  `msil.unresolved`.
- C link semantics, data images, data relocations and constructor lists travel in a **versioned link-info
  record**.
- msil-ld binds by name, renumbers every token, lays out one data segment and synthesizes `<Module>::.cctor` and
  `'<msil>Main'` itself.
- Bitcode (`-flto`, option E) is the bring-up path now and stays as the permanent `-flto` path.

**Runner-up: D1.** ELF `ET_REL` objects with a fork-reserved `EM_CIL`, token relocations (one kind per operand
class) and a call-site signature side table.

**Scores.** The three judges give B2 23 points and D1 19. Two of the three judges rank B2 first; the C-fidelity
judge ranks D1 first.

Section 1 has the table, the disagreement and the conditions attached to B2.

**Timing.** Q1 binds only from M18. Milestones M12-M17 are the same under B2 and D1. Only one contract work package
in M13 differs, and it can move (section 2). So if you want more evidence, the decision can wait until M18 is
planned without delaying anything.

### 0.2 Everything else planning needs

Planning assumes these recommendations unless you say otherwise. **Bold** rows contradict a recon report or a
Design.md sentence, so please read them.

**A. Object details (under B2)**

| # | Decision | Recommended | Alternatives | Why |
|---|---|---|---|---|
| Q2 | How `nm` and `ar` see objects | A plain COFF symbol table in each object's PE envelope (about 150 lines, no LLVM change) | An MSIL `SymbolicFile` in `llvm/lib/Object` (about 400 lines, 2-3 upstream `.cpp` hooks); neither | Measured: `llvm-nm`, the `llvm-ar` index and GNU `nm`/`ar` all work. ildasm, ILSpy and the linker are unaffected, and native linkers still refuse the file (judge-dotnet-value J2) |
| Q3 | Carrier for the link-info record | One versioned binary record in a module-level custom attribute | ManifestResource; an extra metadata stream (no); typed per-row attributes (later) | It survives an ildasm/ilasm round trip, which an extra stream does not (objfmt-binding E15, exports-multiasm E3) |
| Q4 | Data model | msil-ld synthesizes `.cctor` and `Main` from per-object data images and relocation tables | mc-streamer's per-object init methods | All three judges agree. Init methods fail ILVerify and PEVerify `[InitOnly]` (J4/J6), cannot concatenate a section across objects and cannot size common symbols (c-fidelity F2) |
| Q5 | What `clang -S` prints in Phase 2 | Phase 1's runnable module: the object plus a one-object self-link trailer from the same builder msil-ld uses, with D14 frozen. Object-form text sits behind a flag, for the ilasm oracle | Object form only | Keeps the Phase 1 contract and the tests-p2 oracle O3 |
| Q6 | `clang -c` before CIL objects land (M19) | Write bitcode | Keep the error and document `-c -flto` | Unblocks build systems and M14. It already works (`-c -flto`) |
| Q7 | C# netmodule inputs | An end-of-project idea. Fix `pinvokeimpl("msil.unresolved" nomangle cdecl)` now as the documented placeholder | A Phase 2 goal | `csc -target:module` already emits exactly this shape (judge-dotnet-value J4) |

**B. Linking and references**

| # | Decision | Recommended | Alternatives | Why |
|---|---|---|---|---|
| Q8 | Cross-object signature mismatch (K&R calls, wrong prototypes) | An adapter thunk plus a warning where native behaviour can be matched; otherwise a link error naming both objects | Always an error; a warning plus a trapping stub (wasm-ld) | Today a mismatch links silently and dies with InvalidProgramException at JIT time on every runtime (c-fidelity J4b). LTO and native give 41 |
| Q9 | Spelling of the reference option | `--reference=<file\|dir>` or `--reference <path>`, repeatable, with no short form. Positional managed `.dll` inputs are references too. No new clang option in P2 (use `-Wl,` and `.dll` inputs) | `--ref`; positional only | lld's `-r` means `--relocatable`, and `-R` collides in lld and in clang (bcl-binding 6) |
| Q10 | What msil-ld binds against with no references | A built-in table of `[mscorlib]`-safe members bound to mscorlib 4.0.0.0, switched off as soon as any `--reference` is given. Anything outside the table is an error asking for `--reference` | An error (Roslyn on .NET); embedded reference assemblies; auto-detecting installed packs | Keeps `clang foo.c -o foo.exe` running on Framework 4.8 and .NET 6/8/10, as Phase 1 output does (bcl-binding 5.10) |
| Q11 | BCL members the references lack | Members the default codegen needs are required (error). Optimization-only attributes are optional and dropped. API substitutions (FMA, Half, Int128) are optional, fall back to a helper and are reported under `--verbose` | Every request required | Binding stays Roslyn-like, and the P3 switches degrade safely |
| Q12 | Bitcode from stock clang | Supported, with a once-per-link warning and documented limits: `-fno-ms-volatile` on windows-msvc until P3, the `long double` layout on linux-gnu and MinGW, and 27% more IL | Best effort only | The lld-flavor prototype ran 91/93 programs from stock clang (with `-fno-ms-volatile`) and 89/89 on Linux |

**C. Native imports and the driver**

| # | Decision | Recommended | Alternatives | Why |
|---|---|---|---|---|
| **Q13** | **DllImport library names per triple** | **The name a native link records.** ELF: `DT_SONAME` (`libc.so.6`, `libm.so.6`, `libz.so.1`). Windows: the DLL file name verbatim, with `.dll` (`api-ms-win-crt-*-l1-1-0.dll`, `kernel32.dll`), and `#N` for ordinals. Always ExactSpelling\|CDecl | Design.md 5.3's bare names; Phase 1's `libc` | Measured: bare `m`/`z` and unversioned `.so` names fail without the `-dev` symlinks, and `libc` works only through a CoreCLR special case. Framework fails on a dotted name without `.dll`. Without `nomangle`, `GetModuleHandle` silently binds to `GetModuleHandleA` (native-imports 3) |
| **Q14** | **Default C libraries on windows-msvc** | **`ucrt.lib` (API-set names), `kernel32.lib` and `oldnames.lib`, plus a toolchain import list that binds the 11 mem/str/wcs functions to `ucrtbase.dll`** | `vcruntime.lib`, which adds a `VCRUNTIME140.dll` dependency (driver-toolchain's choice: parity with native `/MD`); `ucrtbase` only (Phase 1) | The two recons disagree. A clean .NET machine may lack the VC++ redistributable, and `ucrtbase.dll` exports all 11 functions (verified). You can revisit this in P2-C |
| Q15 | Native data imports (`stdout`, `environ`) in P2 | The existing address field, filled at startup by a private helper (NativeLibrary, or LoadLibrary/GetProcAddress on Framework) | A diagnostic until P3 | Verified on Framework, .NET 8 and .NET 10, on Windows and in WSL. glibc `-O2` turns `getchar` into `getc(stdin)` |
| Q16 | Hosted headers in Phase 2 | Not required. Execution tests stay freestanding, with shim headers for the exit programs. The driver keeps native header search (`--sysroot`, a VS developer shell or `-Xmicrosoft-*`), and an optional compile-only hosted leg covers glibc 2.43 (a 30 MB sysroot unpacked without root, on C:) and UCRT. libc behaviour is P3 | Hosted programs must run in P2 | All 38 C/POSIX headers parse on glibc and 34 of 35 on UCRT. Running hosted programs needs variadic `printf`, which is P3 (driver-toolchain 5) |
| Q17 | Sysroot input | Real sysroot files: `.so` files, GNU ld scripts and import libraries | `llvm-ifs` text stubs | `llvm-ifs` cannot represent glibc's versioned duplicates (measured) |
| Q18 | How msil-ld is hosted | A flavor inside `lld.exe` with an `msil-ld` alias, and `parseProgname` preferring `msil`. The alias copies cost about 0.4 GB on D:, which has 16 GB free | A standalone `msil-ld`; `lld -flavor msil` only | lld's lit config needs every flavor. Without the `msil` preference, `x86_64-unknown-linux-gnu-msil-ld` dispatches to the ELF flavor (measured) |
| Q19 | Output naming and PE kind | `a.exe` on every MSIL triple. Programs are EXE images; `-shared` gives a DLL image | `a.dll` on Linux triples | Framework starts only EXE images, and `dotnet` refuses names other than `.dll` and `.exe` (driver-toolchain 4.7) |
| Q20 | `-save-temps -c`, `/FA`, `/Fa` and `.il` inputs | Clean errors (there is no ILAsm parser in P2) | An MSIL special case; a 2k-line MCAsmParser | — |

**D. Exports and libraries (P2-C)**

| # | Decision | Recommended | Alternatives | Why |
|---|---|---|---|---|
| Q21 | Library semantics | CLR-style: `-Bsymbolic` (no interposition), `-z defs` (no undefined symbols in `-shared` output), and a warning for duplicate definitions | ELF-style interposition | An assembly has no global symbol scope (exports-multiasm 10.2) |
| Q22 | Export selection on Windows triples | `dllexport` symbols only when any exist, otherwise every default-visibility symbol (the GNU ld rule) | Always every default-visibility symbol | Matches Windows C code that uses ZEXPORT-style macros |
| Q23 | Default export class | The assembly name as a class without a namespace; `--export-class` overrides it | A fixed name | — |
| Q24 | Export surface | Per function `f`: the method plus a `'<msil>addr_f'()` accessor. Per variable `x`: the accessor plus a property `x`. Never public fields. No opt-out at first | Accessor only | Framework function-pointer identity needs the exporter's own value (E2b). The cost is 60-70 bytes per export |
| **Q25** | **Type of the symbol-table attribute** | **A private attribute class emitted into the library (AllowMultiple, for FeatureIdeas 7), sharing its codec with the object's link-info record** | The BCL `AssemblyMetadataAttribute` (backlog S5) | Both satisfy "no runtime support assembly". A private type cannot clash and is versioned with the toolchain |

**E. Codegen and the writer**

| # | Decision | Recommended | Alternatives | Why |
|---|---|---|---|---|
| Q26 | Shortest instruction forms in `-S` | Yes. The FileCheck tests are updated mechanically | Keep long forms and normalize in comparisons | IL is 16.5% smaller at `-O2` and 23.6% smaller at `-O0`, and text and binary become byte-identical through ilasm (metadata-writer E9) |
| Q27 | Identifier length | Keep the 1,023-byte limit everywhere in P2 | Lift it in the binary path | PEVerify `/md` flags longer names |
| Q28 | ECMA-335 library: name and place | One library holding both reader and writer, at `llvm/lib/CLI` (namespace `llvm::cli`), with constants in `BinaryFormat/CLI.h` | `llvm/lib/ECMA335` (avoids "CLI" being read as command line); inside the target; the reader in `lib/Object` (bcl-binding) | Objects, outputs, references and P2-C inputs share one schema |

**F. Tests and the Phase 2 exit**

| # | Decision | Recommended | Alternatives | Why |
|---|---|---|---|---|
| Q29 | Which programs define the Phase 2 exit | Nine multi-TU programs (zlib `Z_SOLO`, tinf+zlib, Monocypher, CoreMark, LZ4, BLAKE2, TweetNaCl, KISS FFT, jsmn) at `-O0`/`-O2`, on both triples and every runtime. Plus c-testsuite's 150 libc-free tests, the link-semantics matrix (the zoo and p1-p9 against native transcripts) and M1 through the object pipeline | A smaller set (zlib, Monocypher, CoreMark) | 9/9 already match native through the prototype path (tests-p2 E4) |
| Q30 | Where corpus sources come from | A pinned manifest fetched into a cache outside the repository by a gate step. This needs your OK for the network step; the CI licences of CoreMark and c-testsuite are unverified | Manual fetch. Never vendored | — |
| Q31 | Default test pipeline | `asm` until the P2 exit, then `obj`, keeping one `asm` oracle leg | Switch per milestone | — |
| Q32 | Gate budget | 20-30 minutes per milestone gate, 70-90 minutes for the phase-exit gate | Move legs to CI only | Measured leg costs (tests-p2 6) |
| Q33 | Linux oracle | Cross-linked native binaries with copies of WSL's `libc.so.6`/`libm.so.6` (no sudo) | Install clang in WSL (needs your sudo) | 93/93 M1 programs reproduced (tests-p2 E7) |

**G. Backlog and housekeeping**

| # | Decision | Recommended | Alternatives |
|---|---|---|---|
| Q34 | Backlog placement | A separate M12 "Backlog fixes" now (contents in section 2). The import attribute goes into M13. The S1-S8 texts go into the P2 plan. swiftcc moves to P4 or is dropped. Drop the test-count item | Fold everything into P2 milestones |
| Q35 | C spelling of the import attribute | Share WebAssembly's `import_module`/`import_name` (an `Attr.td` hook in M13, the only clang-wide rebuild of P2) | `msil_import_module`/`msil_import_name`; none |
| Q36 | i128 division and FP<->i128 conversions | Expand them inline (M12), and change Design.md 7.1's 128-bit row and plan P3-B | Helpers or Int128 in P3 |
| Q37 | Status.md's 14 Design.md texts and the S1-S13 consistency fixes (backlog 6-7) | Accept them all. Item 4 is amended per Q13 (backlog's draft says `libc`; Q13 says `DT_SONAME`) | Edit them case by case |
| Q38 | CI | Make `msil` the default branch: `msil-ci.yml` cannot be dispatched otherwise (404). Pin setup-dotnet to v5.4.0 `26b0ec14cb23fa6904739307f278c14f94c95bf1` | Keep `main` as the default and have no CI; v6.0.0 |
| Q39 | WSL .NET updates | The harness uses a private `dotnet-install.sh` runtime | `apt-mark hold` (needs sudo) |
| Q40 | Upstream reports | File the MIR FP-literal lexer regression (all targets, since `41c214f0b115`). The wasm-isel verifier and va_arg-aggregate crashes are optional | Drop them |

---

## 1. Object format and binding

### 1.1 The candidates as judged

Each judge scored 0-10 under one lens: [cost and risk](judge-cost-risk.md), [C fidelity](judge-c-fidelity.md)
and [.NET value](judge-dotnet-value.md). The candidates were pruned from the container x binding matrix. Every
judge pruned COFF containers: machine `0xC0EE` is rejected by every LLVM tool, by `link.exe` and by `lib.exe`, and
machine 0 is linked as x64 code by `link.exe` and `lld-link`. Every judge also pruned pure netmodules without a
record and the GOT binding (`calli` costs 3-5x an inlined call).

| Candidate | Container and binding | Cost | Fidelity | .NET | Sum | Key evidence | Dealbreakers named |
|---|---|---|---|---|---|---|---|
| **B2** | netmodule + symbolic references + link-info record | **8** | 7 | **8** | **23** | The zoo (weak, weak undefined, COMDAT, common, alias, cross-object constructor priorities, pointers in data) gives exit 63 on .NET 8, .NET 10 and Framework, and in WSL. 93/93 M1 programs pass through objects at `-O0`/`-O2`. ILSpy decompiles objects, and an ildasm-to-ilasm round trip still relinks. 2.6-2.8k lines of format-specific code, no upstream hooks | A record not specified field by field (the prototype got a weak alias wrong, J3, and weak vs common wrong, exit 51 vs native 1, J10); assembly-shaped objects |
| D1 | ELF `ET_REL` (`EM_CIL`, own writer) + token and data relocations + side tables | 6 | **8** | 5 | 19 | ELF with an unknown machine is read by `llvm-nm`, `llvm-ar` (with an index), `objdump -r`, `readobj` (groups, TLS, notes) and `objcopy`, and `ld.lld` refuses it. An `ELF.h` edit rebuilds 51 objects (2.4%). 3.4-4k lines now, plus 1-2k by P4 | A single `R_CIL_TOKEN32` kind (cannot tell `call f` from `&f`); no call-site signature table |
| D2 | ELF + an embedded ECMA-335 metadata section (the MSVC `/clr` shape) | 4 | 7 | 5 | 16 | `link.exe` merges and runs MSVC `/clr` objects (exit 154), and `lld-link` rejects them. 4.3-5.3k lines | Two symbol spaces to keep consistent |
| E3 | Bitcode only, LTO only, as the sole format | 4 | 5 | 3 | 12 | Works today. A 52-KLOC program needs 24.8 s (merge + O2 codegen) or 54.1 s (full O2) per link, single-threaded | Whole-program codegen in every link; no ThinLTO; version-locked libraries; per-TU errors surface at link time |
| A1 | A wasm-style custom container (Design.md 5.1 as written) | 2 | 6 | 2 | 10 | 7-8k lines and about 30 hook sites; wasm needed 10.4k lines outside lld | Dominated by D1 |
| B4 | One assembly per object, a forwarder facade and a startup driver | 2 | 2 | 2 | 6 | Runs on every runtime with no change. Startup costs about 0.9-1.1 ms per assembly | Errors at JIT time; not a static linker |

### 1.2 The recommendation and its conditions

**Adopt B2, together with E as the bring-up and `-flto` path.** Adopt it with the C-fidelity judge's contract.
That judge's own text says B2 then ties D1, and that the ilasm oracle then tips the balance to B
([judge-c-fidelity.md](judge-c-fidelity.md) 8).

1. **Specify link-info record v1 field by field against gABI and lld/ELF semantics** before P2-A freezes it. Use
   wasm's `linking` section as the checklist, and version the record from day one. It covers:
   - symbols: binding, visibility, common size and alignment, TLS, no-strip;
   - aliases, with binding and offset;
   - weak undefined symbols;
   - COMDAT groups with a selection kind (Any and NoDeduplicate; diagnose the others);
   - segments with section names and the flags STRINGS, TLS and RETAIN;
   - data relocations ADDR64/32, FNADDR, DIFF and TLS_OFFSET;
   - init and fini lists with priorities;
   - linker options;
   - a header with the triple, the format version and the codegen-switch and ABI flags.
2. **msil-ld's resolver is a port of `lld/ELF/Symbols.cpp`**: a common symbol yields only to a non-weak
   definition (`:541-546`), the largest common wins, a weak undefined does not extract, and duplicates are
   reported. lld/wasm, the driver skeleton, has no common symbols.
3. **Every reference to an undefined function is a MemberRef per (name, call-site signature).** The record carries
   its flags: no prototype, vararg, byval/sret size and alignment, dllimport, and an explicit import module and
   name. msil-ld checks every reference against the definition (Q8).
4. **Objects carry section names.** msil-ld concatenates same-named sections and synthesizes `__start_`/`__stop_`
   and the constructor and destructor tables per triple (ELF `.init_array*`, Windows `.CRT$XC*`).
5. **A link-semantics matrix**: the zoo plus c-fidelity's p1-p9 (later TLS and destructors), run through objects
   and through LTO on both triples. Each run must match a native transcript or stop with a named diagnostic, never
   with a JIT-time exception.
6. **A COFF symbol view (Q2)**, so that `nm` and `ar` see the objects.
7. **Netmodule shape.** No Assembly row, a content-hash MVID, and a Module name independent of the `-o` path.
   Objects carry no `.cctor`, no `Main` and no image field.
8. Reserve a per-object Portable PDB carrier for Phase 4, keyed by the object's MethodDef rows (ilasm already
   produces one, judge-dotnet-value J5).

### 1.3 Where the judges disagree, and the trade-off

- **The case for D1** (the C-fidelity judge): ELF's link vocabulary is externally specified (gABI), implemented by
  lld/ELF and inspectable with independent tools.
  - D1 also allows a **linkage-parity oracle** no other container has: compare the symbol, section and group
    tables of an MSIL object with a native ELF compile of the same IR.
  - B2's record re-specifies all of this privately, and the prototype already got two rules wrong.
- **The case for B2** (the cost-risk and .NET-value judges): D1 does not remove the private format; it moves it
  from the link semantics to the payload.
  - Phases 3 and 4 add metadata kinds: value types in P/Invoke signatures, delegate thunk types,
    `[ThreadStatic]`/`UnmanagedCallersOnly` attributes, EH class tokens, and `Vector128` TypeSpecs and
    MethodSpecs. J5 counted five new tables.
  - Under D1 each needs a private side-section encoding, because the coded indices in signature blobs are
    compressed and fixed-size relocations cannot patch them. That format converges on embedded metadata (D2).
    This is the C-fidelity judge's own condition for switching away from D1.
  - Under B2 they are ordinary rows. One reader and writer then serves objects, outputs, reference assemblies and
    P2-C inputs, ildasm and ilasm give an independent oracle for objects, and no upstream hook is needed.
- **The synthesizer's reading.** The link-semantics set is closed and has reference specifications to copy (gABI,
  lld/ELF and wasm's `linking` section). The payload set is open-ended and grows every phase. A private encoding
  of the closed set can be specified once and tested against native transcripts. A private encoding of the open
  set must be redesigned in every phase.
  - **What B2 gives up**: `llvm-readobj`, `objcopy` and `yaml2obj` on objects; the parity oracle; ELF semantics
    for free. Linker test inputs come from ilasm text rather than `yaml2obj`.
  - Both candidates keep two sources of truth. In B2 the record is keyed by row tokens; in D1 ELF symbols sit
    beside the side-table names for BCL members and types. msil-ld should check consistency either way.
- **Cost**: the critical path to "two `clang -c` objects run without ilasm" has the same length under B2 and D1
  (judge-cost-risk 6). The ECMA-335 library (8.5-9.5k lines with tests) dominates in every option, because
  msil-ld writes the PE and reads references whatever the container.

### 1.4 Agreed by every judge (no decision needed)

- **The binding model.** Bind by name and renumber every token. A B object needs no code relocations: IL is
  self-describing (about 36 tokens per KB), and a decoder finds every token. msil-ld lays out one data segment and
  synthesizes the module initializer (with a table-driven relocation loop) and `'<msil>Main'`. One trailer builder
  serves both msil-ld and the `-S` self-link.
- **No initialization code in objects**, so FeatureIdeas 6 and 7 stay open.
- **E** is the bring-up path and the permanent `-flto` path.
- **Archives** are built with `llvm-ar` on every triple. `lib.exe` and `llvm-lib` reject every CIL candidate.
- **Rejected**: A1, C without a record, B4 for static linking (its forwarders and two-phase init serve P2-C only),
  the GOT and COFF.

### 1.5 Your ideas of 2026-10-17: what the experiments showed

1. **".netmodule as the object format".** It works, and it is the recommendation, with a link-info record added.
   - The objects are netmodules built by ilasm from Phase 1 text. The zoo exits 63 on all three runtimes, and 93/93
     corpus programs pass through objects ([objfmt-binding.md](objfmt-binding.md) 4.4).
   - Netmodules alone carry no C link semantics: no weak, COMDAT, common, priorities or data relocations.
   - The CLR's own multi-module linking runs on Framework only. CoreCLR raises `0x8013101E` ("only single file
     assemblies are supported"), so msil-ld must merge the modules.
   - Your variant (B), an extra stream or section: an extra `#MSILLink` stream or `.msillnk` section is tolerated by
     both ildasms, by System.Reflection.Metadata and by every runtime, but an ildasm/ilasm round trip drops it. A
     module-level custom attribute or a ManifestResource survives the round trip, so the record goes there.
   - Your variant (C), pure netmodules with an ILMerge-like merger: a 440-line prototype merged 101 per-TU modules
     (4,302 methods, 24,977 tokens rewritten) in about 1 s, and the result matches the LTO build. But per-object
     `.cctor` code then fails ILVerify and PEVerify `[InitOnly]`, and C semantics need the record anyway, which
     turns C into B.
   - Your variant (D), ELF or COFF with CIL in sections: ELF works and is the runner-up. COFF does not (1.1).
   - The upside you named: `csc -target:module` with `[DllImport("msil.unresolved")]` emits exactly B's
     placeholder shape (Q7).
2. **"Reuse the C++/CLI object format?"** It was dissected ([objfmt-container.md](objfmt-container.md) 6.1).
   - Its shape: COFF, one `.text$mn` CIL body per method, `IMAGE_REL_AMD64_TOKEN` relocations against CLR-token
     symbols, and `.cormeta` holding full ECMA-335 metadata. `link.exe` merges it (the program exits 154), and
     `lld-link` rejects it.
   - Its architecture is candidate D2: two writers and two symbol spaces. MSVC needs that because its objects mix
     native and managed code; an IL-only design does not.
   - COFF itself is unsafe for CIL, as section 1.1 explains.
3. **"Why do we need relocations at all?"**
   - For code, we do not: tokens are row numbers, so every merge renumbers every token anyway. The linker finds
     the token sites by decoding the IL and learns their meaning from the object's own metadata. With ELF RELA,
     the same information would cost about 860 bytes of relocations per KB of IL.
   - For data, relocations stay: pointers in initialized data are applied by the synthesized `.cctor`, as in
     Phase 1.
   - Your option (2), symbolic objects, is the recommended binding.
   - Your option (3), LTO only, is the bring-up path but not the sole format: whole-program codegen in every link,
     no ThinLTO or partitions, and backend errors moved from `-c` to link time.
4. **"A real .NET assembly as the object, with a magic table."**
   - (a) MemberRefs on `[msil.extern]'<msil>Symbols'` are exactly B2's placeholders. Use the netmodule shape,
     though: an assembly-shaped object loads and runs until a placeholder is hit (DllNotFound or FileNotFound),
     while a netmodule refuses to load.
   - (b) No merge (patching ResolutionScope): runs on all three runtimes, but costs about 1 ms of startup per
     object, a public class per symbol and errors at JIT time. A TypeRef to another assembly's `<Module>` throws
     TypeLoadException, so P2-C needs an export class. Only P2-C uses its pieces.
   - (c) A GOT costs 3-5x an inlined call, and the `<msil>fn_` fields already are the slots: rejected.
5. **"Maybe a metadata table already fits."**
   - (1) ImplMap + ModuleRef `msil.unresolved` works with both ilasms, with SRM and on every runtime. It is one of
     B2's two placeholder forms and the C# contract (Q7).
   - (2) A MemberRef with a ModuleRef parent, plus File/ExportedType, works on Framework only.
   - (3) ExportedType forwarders work everywhere; they serve P2-C diagnostics only.
   - (4) ENCLog/ENCMap images are rejected by both runtimes.
   - (5) CustomAttribute and Portable PDB CDI rows are tolerated everywhere. The module-level custom attribute is
     the recommended record carrier.
   - As you expected, none of these avoids rebasing tokens on a merge.

---

## 2. Phase 2 milestone outline (recommended option B2)

The outline keeps the farm's style: one workflow run per milestone, parallel WPs on disjoint files, tests by
separate agents, a three-lens review, a gate and one commit. Sizes are taken from the recon reports (code + tests,
estimates).

| M | Title | Goal | Size, WPs, rebuild | Needs |
|---|---|---|---|---|
| M12 | Backlog fixes | Close the backend items that any linker path hits (detail below) | About 450 code + 1,000 test lines; 7 WPs; MSIL target + `CGStmt.cpp`, 15-30 min | Q34, Q36 |
| M13 | Phase-2 shared plumbing and contracts (the M1 of Phase 2) | Every upstream hook and shared contract Phase 2 needs, landed once (detail below) | About 1,800 + 600 lines; 7 WPs; one clang-wide rebuild if Q35 (otherwise about 5 min); first lld build 2.5-3 min; +0.4 GB on D: | Q28, Q35, Q18 |
| M14 | msil-ld links bitcode (P2B-1) + clang MSIL toolchain | Linker driver, inputs, lld/ELF symbol rules, LTO and the clang toolchain; output is IL text assembled by ilasm (detail below) | About 2,900 + tests; 9 WPs; lld/MSIL + 3 clang TUs | Q6, Q9 (spelling only), Q12, Q19 |
| M15 | Native libraries and P/Invoke resolution | Replace D14 in link mode with resolution from native libraries (detail below) | About 1,600 + 20 lit tests; 6 WPs | Q13-Q17 |
| M16 | ECMA-335 library: writer half | `llvm/lib/CLI` WPs B, C, D, G: blobs, signatures, custom attributes; opcodes, ILBuilder (labels, shortest forms, relaxation, maxstack), method bodies with EH; heaps and MetadataBuilder (ordering, 0xFFFF padding, sorting, tokens); C# oracle tools (`mdump`/`mdcanon`) | About 3,100 + 1,900 + 700; 4-5 WPs; new library only | Q28 |
| M17 | ECMA-335 library: reader, dumper, PE writer, link-info codec | WPs E, F and the codec (detail below) | About 3,300 + 1,200; 4 WPs; new library only | Q3, Q25 |
| M18 | One MCInst per CIL instruction + binary encoder (P2-A1) | A shared encoding for `-S` and objects (detail below) | About 1,600 + test churn; 5 WPs; target only | Q26, Q27; **Q1 from here on** |
| M19 | CIL objects (P2-A2) | `clang -c` writes B2 objects (detail below) | About 2,500 + tests; 6 WPs; target + `Clang.cpp` | Q1-Q5 |
| M20 | msil-ld links CIL objects and writes the PE (P2B-2) | Two `clang -c` objects run with no ilasm (detail below) | About 3,500 + tests; 7 WPs | Q8 |
| M21 | BCL reference binding (P2B-3) | Bind BCL members from `--reference` assemblies, Roslyn-style (detail below) | About 2,000 + tests; 5 WPs | Q9-Q11 |
| M22 | Libraries and assembly-to-assembly linking (P2-C) | `-shared` libraries and linking programs against them (detail below) | About 2,500 + tests; 6 WPs | Q21-Q25 |
| M23 | Phase-2 conformance and exit | The exit corpus and the final gate (detail below) | 6 WPs; phase gate 70-90 min | Q29-Q33, Q38 |

Milestone contents:

- **M12, Backlog fixes.**
  - One-module GlobalAlias lowering and `extern_weak` as null plus a trap stub, in whole-program mode only
    (object mode later emits symbols, c-fidelity F5).
  - Empty asm with operands (`black_box`).
  - Inline i128 division and conversions.
  - A `VAArgInst` diagnostic for aggregates.
  - A byval slot pool.
  - **A diagnostic for `section` on globals**: today a constructor registered through `.init_array` or `.CRT$XCU`
    silently does not run (exit 3 instead of 42, J6).
  - MSILPrepareIR drops unreferenced discardable definitions (UCRT `-O0` variadics).
  - Infrastructure: the `-std=gnu17` pin, compiler-rt in the Windows oracle, lit `@rsp` files, the cross-linked
    Linux oracle and a private WSL runtime.
- **M13, Phase-2 shared plumbing and contracts.**
  - clang `Attr.td` import attributes (Q35), the `DiagnosticDriverKinds.td` rewording, and the
    `#pragma comment(lib)` handler for MSIL triples (`ParsePragma.cpp:475-480`: dropped silently today, J12).
  - lib/MC `.cpp` hooks (`MCContext.cpp` createSymbolImpl, `TargetRegistry.cpp:93-96`, `MCAsmBackend.cpp:60-61`)
    with stub factories that report clean errors, and the new headers `MCSymbolMSIL.h`, `MCMSILStreamer.h` and
    `MCMSILObjectWriter.h`. No `MCSymbol.h` or `MCContext.h` edit is needed (measured).
  - `BinaryFormat/CLI.h`, `CLITables.def` and `CILOpcodes.def`, plus the lib/CLI skeleton (WP A).
  - The **link-info record v1 contract** (`BinaryFormat/MSILLink.h`, condition 1).
  - lld flavor hooks and an msil-ld stub, `Triple::isCompatibleWith(X, X-msil)`.
  - The gate builds lld, llvm-ar, llvm-nm, llvm-objdump and llvm-readobj.
- **M14, msil-ld links bitcode, and the clang MSIL toolchain.**
  - The lld/MSIL driver and options; bitcode and archive inputs with lazy members; resolution by lld/ELF rules.
  - LTO with `OverrideTriple`; the assembly name and codegen switches passed as module flags.
  - `-Map`, `--why-extract` and `--reproduce`.
  - `MSILToolChain<NativeTC>` plus the `Driver.cpp` hunks.
  - Output: `--lto-emit-asm` plus ilasm.
  - Harness: `msil_pipeline=lto`, `%{msil_prog}` and `msil-run.py --dep`; `msil/test/M2/00-02`.
  - Gate: two-TU programs, archives, and M1 at 93/93.
- **M15, Native libraries and P/Invoke resolution.**
  - Readers for ELF `.so` files with symbol versions, GNU ld scripts, COFF and MinGW import libraries, weak-alias
    objects and DLL export tables.
  - `-l` lookup that skips native-only archives, plus `-Bstatic` and `-Bdynamic`.
  - Naming per Q13; kind and version diagnostics.
  - Driver default libraries.
  - D14 frozen for `-S`/llc, and the f32 `ldexp`/`frexp` promotion decoupled from it.
- **M17, ECMA-335 library: reader, dumper, PE writer, link-info codec.**
  - PEWriter: the Roslyn envelope (PE32+, ILONLY, EXE or DLL) with a deterministic `BlobContentId`.
  - MetadataReader: reference packs, custom-attribute blobs, and Property/MethodSemantics.
  - `llvm-cil-dump --canonical`, which replaces `mdcanon` in oracle O3.
  - The link-info and P2-C symbol-table codec.
- **M18, One MCInst per CIL instruction, and the binary encoder.**
  - `MSILMCInstLower` expands every multi-opcode record. The printer and a table-driven `MSILMCCodeEmitter` read
    the same record.
  - `MSILAsmBackend` and one fixup kind per operand class.
  - Structured BCL operands from one `.def` list; `SynthesizedBody` replaced by the ILBuilder.
  - Gate: `-S` unchanged (or updated mechanically per Q26), and the encoder matches ilasm on 2,670 of 2,670 methods.
- **M19, CIL objects.**
  - An object-mode AsmPrinter emits the record from `MSILDataSegment`, ctors, FuncAddrs, aliases, weak undefined
    symbols, section names, call-site flags and linker options.
  - `MSILObjectWriter` writes the netmodule through MetadataBuilder and PEWriter, plus the COFF view.
  - Object-form text behind a flag. The `Clang.cpp:5540-5543` error is removed, and `-c` switches from bitcode to
    CIL objects.
  - Gate: `canon(clang -c)` equals `canon(ilasm(object text))`; ildasm opens every object; `llvm-nm` reads them.
- **M20, msil-ld links CIL objects and writes the PE.**
  - An `ObjFile` over the MetadataReader and the record, and an IL token walker.
  - Common symbols; signature checks with adapters.
  - Named-section concatenation, `__start_`/`__stop_`, and constructor and destructor tables.
  - The shared trailer builder, also adopted by `-S`, and a table-driven relocation loop.
  - Spilling members into extra classes past the per-type limits (65,524 methods).
  - The native data-import helper; `--gc-sections`; LTO emits in-memory objects (so ThinLTO and partitions work).
  - Gate: two `clang -c` objects run with no ilasm on every runtime; M1 runs with `msil_pipeline=obj`; O3 and O4
    hold; the link-semantics matrix matches native; D21 and the round trip pass on outputs.
- **M21, BCL reference binding.**
  - `--reference`, the core-library rule, identities from Assembly rows, and ambiguity errors.
  - Member lookup that copies modreqs and rewrites type names in custom-attribute blobs.
  - The no-reference table (Q10).
  - Test legs with no references and with net48, net8 and net10 references.
- **M22, Libraries and assembly-to-assembly linking.**
  - `-shared`, export selection and the export class; accessors and properties.
  - Two-phase initialization: a beforefieldinit `'<msil>Data'` class, then `<Module>::.cctor`, which runs
    `ldtoken` once per linked library.
  - The symbol-table attribute.
  - Importing toolchain libraries with triple, CRT and ABI checks.
  - Driver support for `-shared` and `/LD`; harness dependencies.
  - Gate: a library and a program, a cyclic pair, and a C# consumer.
- **M23, Phase-2 conformance and exit.**
  - The exit corpus by pinned manifest; the link-semantics matrix completed.
  - The default pipeline switches to `obj`, keeping an `asm` oracle leg.
  - Determinism and CI legs.
  - Updates to Status.md and GettingStarted.md.

M14-M17 do not depend on each other's files, so the orchestrator may run two of them back to back without waiting
for the object decision. M12 can start immediately.

### 2.1 How the outline changes if you pick D1

- **M13** adds the ELF hook batch: `EM_CIL` in `BinaryFormat/ELF.h`; `ELFRelocs/MSIL.def` with one relocation kind
  per operand class (METHOD, FNADDR, GADDR, SIG, LOCALSIG, TYPE, MEMBERREF, USERSTRING, later TLSOFF); and the
  switch cases in `ELFObjectFile.h`, `Object/ELF.cpp`, `ELFYAML.cpp` and `ELFDumper.cpp`. That is 6-9 upstream
  hook files and 1-2 more minutes of rebuild. Its contract becomes the side sections (a `.msil.refs` call-site
  signature and flags table indexed from the relocation addend, `.msil.sig`, `.note.msil`, and `st_other` dll
  bits) instead of `MSILLink.h`. There is no COFF view.
- **M17** drops the link-info codec and gains the side-section codec (about 1.2k lines).
- **M19** writes an ELF serializer and reproduces TargetLoweringObjectFileELF's section selection (per-function
  `.cil.text.<sym>` sections, COMDAT groups, `SHF_MERGE`, `.init_array.N`, `.tdata`/`.tbss`). There is no
  object-form text or ilasm oracle. The gate uses `llvm-readobj`/`llvm-nm`/`objdump -r` FileCheck tests plus the
  **linkage-parity oracle** against native ELF. The writer no longer needs MetadataBuilder, so M18-M19 may move
  ahead of M16-M17. An optional MSIL `MCDisassembler` plus a `makeTriple` hook would give `llvm-objdump -d`.
- **M20** reads objects through `llvm::object::ELFObjectFile`, processes relocations by kind and decodes the side
  tables, with test inputs from `yaml2obj`. msil-ld then has two input readers: ELF for objects, and CLI for
  references and P2-C (M21, M22).
- **After Phase 2**, every P3/P4 metadata kind needs a side-format extension (+1-2k lines by P4), or a format
  revision to D2. Phase 4 debug info needs DWARF sections in the MSIL MC environment, a `RelocationResolver.cpp`
  case and a DWARF-to-Portable-PDB converter in msil-ld.
- The critical path to "two `clang -c` objects without ilasm" keeps the same length (judge-cost-risk 6).

---

## 3. Design.md and plan section 7 changes the recommendation implies (not edited)

Design.md is the user's document. The plan is the orchestrator's. These are proposals.

**Design.md**

1. **2.1 table**, the row "Wasm object format with typed symbols and relocations, plus `wasm-ld`": change it to
   "lld/wasm: template for msil-ld's driver and LTO; symbol resolution follows lld/ELF; CIL objects are ECMA-335
   modules (5.1)".
2. **2.3, last bullet**: "...all emitted by the AsmPrinter and object writer" becomes "objects carry method
   definitions, data images and relocation tables; msil-ld synthesizes the module initializer, `<msil>Main`, the
   P/Invoke stubs and the export class (`-S` runs the same builder for a one-object self-link)".
3. **4.1, first two bullets**: "Each module gets one unmanaged data segment allocated in the module initializer"
   becomes "Each output assembly gets one unmanaged data segment, laid out by msil-ld from the objects' data
   (same-named sections concatenated, `__start_`/`__stop_` provided) and allocated by the linker-synthesized module
   initializer, which applies relocations from a table and runs constructors in priority order across objects".
4. **5.1, first bullet**: replace "an MC-emitted container, modeled on the wasm object format, holding method
   bodies in CIL, data segments, relocations, and a symbol table with typed signatures" with: "ECMA-335 modules
   (netmodules: no Assembly row) whose definitions are metadata rows and whose cross-object references are named
   metadata references (MemberRefs on placeholder types, ImplMap stubs on `msil.unresolved`), plus a versioned
   link-info record in a module-level custom attribute: C link semantics, data images, data relocations,
   constructor lists, section names and the triple. ildasm and ILSpy read them; a COFF symbol table in the
   envelope serves `nm` and `ar`."
5. **5.1, second bullet**: add "Until CIL objects exist, `clang -c` writes bitcode."
6. **5.2 step 1**: add "including the toolchain's static C helper archive, linked like compiler-rt builtins".
   **Step 2.1**: add "with lld/ELF's resolution rules (weak, common, COMDAT, aliases, lazy extraction)".
   **Step 2.2**: append backlog S7: "Any other reference assembly carries no such table and is used only to bind
   BCL types and members (7.1)."
7. **5.2 step 2.3**: add "Only default-version ELF symbols bind (P/Invoke resolves through `dlsym`); symbols that
   exist only in native static code are link errors unless the toolchain provides them; native data is imported
   through the global's address field."
8. **5.2 step 3**: "Lay out data segments, apply relocations, bind every BCL type ..." becomes "Lay out one data
   segment, synthesize the module initializer and `<msil>Main`, check every cross-object call signature (adapter
   or error), bind every BCL type and member by searching the `--reference` assemblies (a built-in `[mscorlib]`
   table when none is given), and write the PE."
9. **5.2, `-lfoo` lookup**: add "`libfoo.a` counts only if it holds CIL or bitcode members; native archives are
   skipped."
10. **5.3, second bullet**: replace the self-contradictory naming text with Q13's rule (native-imports 12).
    **Third bullet**: "`[SuppressGCTransition]` is emitted only under its own codegen switch and bound from the
    references" (backlog S9).
11. **5.4**:
    - "Exported data is exposed as address accessors" becomes "an accessor `'<msil>addr_x'()` and a property `x`;
      every exported function also gets an address accessor".
    - Add: two-phase initialization; `-Bsymbolic`/`-z defs` semantics; the default export class is the assembly
      name; the Windows export-selection rule.
    - The symbol-table attribute's type is a private class emitted into the library. It carries the triple, the C
      runtime identity, the data model and an ABI version (backlog S6).
12. **5.5, Phase 2**: "the MC object writer writes ECMA-335 module objects and msil-ld writes the PE, both through
    one ECMA-335 library. ilasm stays a test oracle: `clang -c` objects equal ilasm-assembled object-form text
    canonically, and outputs round-trip through ildasm and ilasm."
13. **7.1, "Assembly references" bullet**: insert backlog S8. "Without references, and in `-S` output, BCL members
    bind to `[mscorlib]`, limited to members it provides on both runtimes." Also name the option `--reference`.
14. **8**:
    - Add the bullet `llvm/lib/CLI`: an ECMA-335 reader and writer shared by the MC object writer and msil-ld.
    - `lld/MSIL`: "modeled on lld/wasm (driver, LTO) with lld/ELF's resolution rules, hosted in `lld` as
      `msil-ld`".
    - The driver bullet: `MSILToolChain` over the native toolchain, default C libraries per triple, `a.exe`, and
      managed `.dll` inputs as references or libraries.
    - Mark `clang/lib/CodeGen/Targets/MSIL.cpp` as "(planned)".
15. **10, risk 6**: "Object format and PE writer effort" becomes "the ECMA-335 library (about 9k lines with tests)
    is the dominant Phase 2 cost; the container choice moves a few thousand lines".
16. **11, phase 2 row**: "CIL object format" becomes "CIL objects as ECMA-335 modules with a link-info record".

**ImplementationPlan.md section 7**

17. **P2-A**: replace "a CIL object format modeled on the wasm object format (method bodies in CIL, data segments,
    relocations, typed symbol table)" and "Requires a batched broad-rebuild header commit (MCSymbol kind if needed,
    MCContext createSymbolImpl, BinaryFormat additions)" with:
    - Phase 2's first milestone (M13) batches lib/MC `.cpp` hooks, new MC and BinaryFormat headers, the link-info
      contract, the lld flavor hooks and clang `Attr.td`. `Attr.td` is the only clang-wide rebuild. No `MCSymbol.h`
      or `MCContext.h` edit is needed (measured).
    - P2-A makes every MCInst one CIL instruction, adds structured BCL operands, and writes netmodule objects through
      `llvm/lib/CLI`, with a versioned link-info record and a COFF symbol view.
    - Objects carry no `.cctor` or `Main`. `clang -c` writes bitcode until then.
18. **P2-B**:
    - "(-r)" becomes "(`--reference`)". Add S2's no-reference table and S3's helper archive.
    - Native resolution moves here from P2-C (S4).
    - Add the lld/ELF resolution rules, signature checks with adapters and the synthesized `.cctor`/`Main`.
    - Stock-clang bitcode is a documented compatibility path. msil-ld is hosted in `lld`.
19. **P2-C**: as S4/S5. Add two-phase initialization, accessors with properties and `-Bsymbolic` semantics.
20. **Plan sections 3 and 10** (consequential):
    - D12's "one segment per module" holds only for `-S`; outputs get one segment per assembly, laid out by msil-ld.
    - D14 is frozen as the standalone `-S`/llc policy.
    - D26's alias, `extern_weak` and external-global errors become object symbols in object mode.
    - Section 10 gains this brief's Q1-Q40.

---

## 4. Other places where the reports disagree, and how this brief resolved them

| Topic | Positions | Resolution here |
|---|---|---|
| Data model | mc-streamer R5: per-object segment plus init methods. Everyone else: linker-synthesized initializer | Linker-synthesized (Q4); measured `[InitOnly]` failures |
| windows-msvc C libraries | driver-toolchain: `-lucrt -lvcruntime -lkernel32`. native-imports: vcruntime subset bound to ucrtbase | native-imports (Q14); either choice is cheap to change in P2-C |
| glibc library name | backlog's Design 5.3 draft: `libc` for the C library. native-imports: `libc.so.6` | `DT_SONAME` uniformly (Q13); it also gives musl's own soname |
| Reader location | bcl-binding: `llvm/lib/Object/CLIMetadata`. metadata-writer: `lib/CLI` MetadataReader | One library (Q28) |
| Symbol-table attribute type | exports-multiasm: private class. backlog S5: BCL `AssemblyMetadataAttribute` or private | Private class with AllowMultiple (Q25) |
| Alias lowering | backlog 2.1: RAUW plus forwarding (M12). c-fidelity F5: correct only in whole-program mode | Whole-program mode in M12; object mode emits alias symbols (M19) |
| Object-format claims | objfmt-container: `ELF.h` is "a broad rebuild", and a single `R_CIL_TOKEN32` | Measured at 51 objects (2.4%); per-operand-class kinds needed (judges) |
| B's tool blindness | objfmt-container: `llvm-nm` "no symbols", empty index | The COFF view fixes it with no LLVM change (Q2) |
| objfmt-binding prototype | "aliases tested"; "a definition beats common" | Weak aliases fail (J3), and the common rule contradicts lld/ELF (J10): port lld/ELF rules |
| `#pragma comment(lib)` on linux-gnu-msil | native-imports: emits `-lws2_32` | Only with `-fms-extensions`; by default it is dropped silently (J12). Install the handler in M13 |

---

## 5. Risks to watch, and what would change the recommendation

- **The ECMA-335 library is the schedule risk in every option**, not the container. Land its contracts first
  (M13), and keep M16-M17 small and parallel.
- **Link-info record coverage.** The record must reach ELF/wasm coverage before M19 freezes it. TLS, DIFF, COMDAT
  kinds, start/stop symbols and gc roots are not prototyped yet. Mitigation: condition 1 plus the matrix (1.2).
- **Signature mismatches** must never reach the JIT (Q8). Decide the rule (exact blob or ABI-equivalent) before
  typed pointers or by-value structs arrive (FeatureIdeas 3).
- **The CLR's per-type limits** (65,524 methods; about 65,535 fields) and Roslyn's 0xFFFF-row bug apply to
  msil-ld's output: spill members into extra classes, and pad the tables.
- **Native `exit()` skips `AppDomain.ProcessExit` on modern .NET.** The P2 resolver must leave room for P3 to
  intercept `exit`/`atexit`.
- **Switch to D1** if any of these turns out true:
  - you need LLVM object tools beyond `nm`/`ar` on objects (`readobj` symbol details, `objcopy`, `yaml2obj`);
  - you plan to upstream the backend and reviewers expect ELF-like objects;
  - P3 is designed so that objects never carry TypeDefs, custom attributes or generic instances;
  - the lib/CLI library slips badly, since D1 decouples the object writer from it.
- **Switch to D2** if D1's side tables grow into a copy of the MemberRef, TypeRef and TypeSpec tables.

**FeatureIdeas (preclusion only).**
- Nothing recommended here precludes an idea.
- The rejected per-object init code would have made 6 and 7 harder.
- An assembly-level symbol table holding a single triple would preclude 7; Q25 avoids that.
- Storing final coded indices in object blobs would preclude 3: B2 uses rows, and D1 must use object-local type
  indices.
- Ideas 4 and 5 keep whole-program IR through `-flto`.

---

## 6. What this brief did not settle (planning-level, no user input needed)

- Whether an undefined function is always a MemberRef on `'<msil>Symbols'`, or an ImplMap stub when the import
  module is explicit. Both work, and the judges prefer one MemberRef form plus ImplMap for explicit imports.
- Where branch relaxation lives (MC or msil-ld's re-encode).
- Where the trailer builder lives: the MSIL target library, so that lld links it through `LLVM_TARGETS_TO_BUILD`,
  or lib/CLI.
- How link semantics are presented in objects: a compact record only (recommended for v1), or typed per-row
  attributes that ILSpy can show (judge-dotnet-value J7).
- `Triple::isCompatibleWith` versus filtering the IRMover warning, and whether `--stack-size` exists for `.exe`
  outputs.

---

## 7. Evidence index

| Report | What it settles |
|---|---|
| [objfmt-container.md](objfmt-container.md) | Containers A-E measured; MSVC `/clr` dissected; LTO cost; archivers |
| [objfmt-binding.md](objfmt-binding.md) | The symbolic binding model; zoo and 93/93 through a 726-line linker; ECMA mechanisms E13-E15 |
| [judge-cost-risk.md](judge-cost-risk.md) | Format-specific line counts; header rebuild shares (J6); hook churn (J7); `[InitOnly]` (J4); P3/P4 table growth (J5) |
| [judge-c-fidelity.md](judge-c-fidelity.md) | C link semantics R1-R22; probes J1-J12 against native; the D1 object contract (6.2) |
| [judge-dotnet-value.md](judge-dotnet-value.md) | ILSpy on objects; COFF view (J2); round trip (J3); csc placeholder (J4); per-object PDB (J5) |
| [mc-streamer.md](mc-streamer.md) | One MCInst per CIL instruction; fixups; encoder matches ilasm on 2,670/2,670; header costs |
| [metadata-writer.md](metadata-writer.md) | The `llvm/lib/CLI` design, WPs A-H; Roslyn envelope; CLR limits; determinism |
| [lld-flavor.md](lld-flavor.md) | Flavor hooks; the 701-line msil-ld prototype; LTO retargeting; build cost |
| [bcl-binding.md](bcl-binding.md) | Roslyn-style binding; the `[mscorlib]`-safe table; `--reference` |
| [native-imports.md](native-imports.md) | Native readers; DllImport naming; data imports; default libraries |
| [exports-multiasm.md](exports-multiasm.md) | The P2-C shape: export class, accessors, two-phase init, symbol-table attribute |
| [driver-toolchain.md](driver-toolchain.md) | `MSILToolChain<NativeTC>`; link job; hosted headers; CMake |
| [tests-p2.md](tests-p2.md) | Harness, oracles O1-O6, exit corpus, gates |
| [backlog.md](backlog.md) | M12 scope; the 14 Status.md texts; S1-S13; CI and WSL items |
