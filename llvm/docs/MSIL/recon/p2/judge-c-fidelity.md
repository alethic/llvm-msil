# Judge report: the Phase 2 object format and binding, judged on C semantics fidelity (`judge-c-fidelity`)

- Key: `judge-c-fidelity`. Date: 2026-10-21. Checkout: branch `msil` at `1ea64fe9fc7c` (LLVM 24.0.0git).
- Role: an independent judge of the Phase 2 object-format and binding decision. **Lens: C semantics
  fidelity and correctness.** The question is how faithfully each candidate implements C and ELF/COFF-style
  linking, when errors surface (compile, link or JIT time), and whether one C program behaves the same
  through `-S` + ilasm, through objects + msil-ld, and through LTO.
- Inputs read in full: [objfmt-container.md](objfmt-container.md), [objfmt-binding.md](objfmt-binding.md).
  Read where they bear on the lens: [mc-streamer.md](mc-streamer.md) (5.8-5.10, R1-R7),
  [metadata-writer.md](metadata-writer.md), [lld-flavor.md](lld-flavor.md),
  [driver-toolchain.md](driver-toolchain.md), [native-imports.md](native-imports.md) 7-8,
  [backlog.md](backlog.md) 2.1, [tests-p2.md](tests-p2.md) (oracles O1-O6, E7),
  [exports-multiasm.md](exports-multiasm.md); Design.md 4-5 and 10-11; ImplementationPlan.md decisions D5, D12,
  D14, D21, D26, D27 and section 7; `msil/orchestration/P2/inputs.md`.
- Experiments J1-J12 (section 3) ran in `C:\llvm-msil-scratch\p2\judge-c-fidelity` (6.5 MB). Sources, scripts
  and logs are in [judge-c-fidelity-files/](judge-c-fidelity-files/) (41 KB). No tracked file was modified;
  no cmake or ninja ran; nothing was installed; the only repository writes are this report and its files
  directory. D: had 17 GB free at the end.
- Tools: the shared build's `clang`/`llc` (`D:\llvm-msil\build\bin`), the VS-bundled LLVM 22.1.3 tools
  (`clang` for the native Windows oracle, `lld-link`, `ld.lld`, `llvm-readobj`, `llvm-nm`, `llvm-ar`),
  Framework ilasm 4.8, .NET 8.0.32 / 10.0.13 / Framework 4.8 runtimes on Windows, WSL Ubuntu for the
  native Linux oracle (tests-p2's E7 method: this tree's clang, VS `ld.lld`, a copy of WSL's
  `libc.so.6` and the 20-line `start.c`), and the objfmt-binding C# prototype linker (`msilobj`,
  726 lines, rebuilt from `objfmt-binding-files/tools`).
- Claims are measured (J numbers), read in this checkout (file:line), or marked *unverified*.
- Candidate names: container letter (A wasm-style custom, B netmodule envelope + link record, C pure
  netmodules, D ELF/COFF relocatable, E bitcode only) plus binding number (1 relocations, 2 symbolic
  metadata references, 3 LTO only, 4 no merge, 5 GOT).

---

## 0. Verdict

1. **The binding model decides fidelity far more than the container does.** Every merging candidate
   needs the same four things to be faithful to C: (a) a resolver with lld/ELF's exact rules, (b) a
   linker-laid-out data segment with a linker-synthesized module initializer, (c) a call-site signature
   for every reference plus link-time signature checking with adapter thunks, and (d) section names
   carried through to the linker. The two leading containers (ELF D, netmodule B) can carry all of it.
   The measured fidelity bugs found here are in the binding layer, not in a container:
   - the option-2 prototype's resolver lets a weak definition beat a common symbol (exit 51); native
     lld/ELF, lld-link and the LTO path all give 1 (J10);
   - its link record has no binding for aliases, so a musl-style weak alias overridden by a strong
     definition is reported as a duplicate symbol in both link orders; native gives 144 (J3);
   - a cross-object signature mismatch links silently and fails at JIT time with
     InvalidProgramException on .NET 8, .NET 10 and Framework 4.8, while LTO and native agree (41, J4b);
     K&R calls give 23 natively and through LTO (an `f_bitcast` wrapper) but cannot be expressed in the
     object emulation at all (J4).
2. **Phase 1 already has a silent miscompile that Phase 2 must fix, whatever the container.** The
   `section` attribute is dropped without a diagnostic. A constructor registered through a named
   section (`.init_array` on Linux, `.CRT$XCU` with `#pragma section` on Windows) runs natively (exit 42
   on both oracles) and silently does not run under MSIL `-S` (exit 3 on .NET 8, .NET 10 and Framework,
   both triples; J6). `__start_`/`__stop_` registration fails with a clean error in `-S` and in LTO (J5).
   Objects must carry section names, and msil-ld must concatenate same-named sections across objects.
   **That rules out mc-streamer's per-object data segment (R5, section 5.9)**, which cannot make one
   section contiguous across objects, cannot size a common symbol by the largest declaration, and
   writes `initonly` fields outside `.cctor` (netmerge already measured the [InitOnly] verifier errors).
3. **Ranking under this lens** (section 5): **D1 = ELF ET_REL with token and data relocations (8/10)**,
   B2 = netmodule envelope with symbolic references and a link record (7), D2 = ELF with an embedded
   object-local ECMA-335 metadata section, the MSVC `/clr` shape (7), A1 = wasm-style custom container
   (6), E3 = bitcode-only as the sole format (5), objects-as-assemblies without a merge (2).
   - **D wins on correctness** because the C link semantics are the gABI's: specified, implemented
     by lld/ELF, and inspectable. J11 shows that an ELF object with an unassigned `e_machine` keeps weak
     aliases, TLS symbols, `.init_array.N`/`.fini_array.N`, named sections, `.deplibs` and an archive
     index readable by LLVM 22's tools, and that `ld.lld` refuses it.
   - D also enables a cheap **linkage-parity oracle** that no other container has: the symbol and
     section tables of an MSIL object can be compared with those of a native ELF object built from the
     same IR (section 6.5).
   - **B's real advantages are signatures and the ilasm oracle.** Undefined references carry their
     call-site signatures natively (MemberRef), and `-S` + ilasm can build the object. D must design a
     side table for call-site signatures, which the container draft omits.
4. **Fix the container draft before adopting D.** objfmt-container 6.6 lists one token relocation,
   `R_CIL_TOKEN32`, for callee, address field, BCL member, `calli` signature and string. Against a
   function symbol that cannot distinguish `call f` (MethodDef) from `&f` (the `<msil>fn_f` field, which
   carries M9's Framework pointer identity). D needs one relocation kind per operand class (the
   `R_MSIL_*` list of objfmt-binding section 5), a per-reference signature-and-flags table (section 6.2),
   and token relocations that always name a symbol, never a section plus addend.
5. **Keep E (bitcode) as the bring-up path and as the `-flto` path, not as the only format.** IR
   linking matched native on every construct the backend supports (J4, J4b, J7, J10), but `clang -c
   -flto` accepts translation units whose backend errors (alias, TLS, destructors, `__start_`) then
   surface only at link time (J2, J3, J5, J8, J9). The objects path and the LTO path will be two
   resolver implementations, so every link-semantics test must run through both and compare them
   with a native transcript.

---

## 1. The lens: what C fidelity requires of an object format

Each row is a C or linker semantic. Native mechanism, then what each leading candidate needs, then
the evidence.

| # | C / linker semantic | Native (ELF / COFF) | D1: ELF + relocations | B2: netmodule + record | E3: LTO only | Evidence |
|---|---|---|---|---|---|---|
| R1 | external vs `static`, equal static names in two TUs | STB_GLOBAL / STB_LOCAL | native | `public` / `privatescope` rows | internalize + rename | zoo J1 |
| R2 | weak definition overridden by a strong one | STB_WEAK | native | record `bind` | IRMover | zoo J1 |
| R3 | weak undefined: null, no archive extraction | undefined STB_WEAK | native | record `weak_undef` | backend error today (`extern_weak`) | zoo J1/J2 |
| R4 | COMDAT any (`linkonce_odr`, `selectany`, MSVC string literals) | SHT_GROUP / COMDAT sections | GRP_COMDAT (Any and NoDeduplicate) | record comdat key (any only) | IRMover, every kind | J7, zoo |
| R5 | aliases: function, data, weak, with offset | second symbol on the same section and value | native (J11) | alias map; **prototype has no binding** (J3) | backend error today (D26) | J3, J11 |
| R6 | common symbols: largest size and alignment; common vs weak | SHN_COMMON | native | record `common`; **prototype rule wrong** (J10) | IRMover correct (J10) | J10 |
| R7 | constructors with priorities across objects | `.init_array.N` / `.CRT$XCT` sorting | native | record ctors | `appending` merge | zoo J1 |
| R8 | destructors with priorities | `.fini_array.N` | native (J11) | record (not prototyped) | backend error today (J9) | J9, J11 |
| R9 | named sections: `__start_`/`__stop_`, section-registered constructors, COFF `$` grouping | section name and concatenation | native | record needs section names (absent) | single module; `section` ignored (J6), `__start_` error (J5) | J5, J6 |
| R10 | data relocations: pointers to data and functions in initializers, addends, unaligned sites | RELA | native | record relocs | single module | zoo J1 |
| R11 | symbol differences (relative tables, `MSILDiff`) across sections | PC-relative or ADD/SUB pairs | needs a PC-relative kind or a pair | record DIFF with two symbols (not prototyped) | folded in one module | MSILDataSegment.h:67-81 |
| R12 | string merging | SHF_MERGE\|SHF_STRINGS; MSVC COMDAT strings | native | `merge_strings` flag | ConstantMerge | objfmt-binding E3 |
| R13 | alignment, BSS | `sh_addralign`, SHT_NOBITS | native | per-symbol align | n/a | zoo |
| R14 | `thread_local` | STT_TLS, `.tdata`/`.tbss` | native (J11) | record TLS segment (not prototyped) | backend error today (J8) | J8, J11 |
| R15 | archives: lazy extraction, weak undefined and common do not extract | member `.symtab`; archive index | native; `llvm-ar` index works (J11) | member metadata + record; empty index | irsymtab | J11, objfmt-binding E4 |
| R16 | `-ffunction-sections`, `--gc-sections`, `used`/`retain` | sections, SHF_GNU_RETAIN | native | rows + no-strip flag | GlobalDCE | - |
| R17 | `#pragma comment(lib)`, `/DEFAULTLIB`, `/alternatename` | `.deplibs` (SHT_LLVM_DEPENDENT_LIBRARIES), SHT_LLVM_LINKER_OPTIONS | native (J11) | record | module metadata; **dropped by the frontend on linux-gnu-msil** (J12) | J12 |
| R18 | hidden visibility; dllexport/dllimport (P2-C, Windows triples) | STV_HIDDEN; COFF storage class | STV_HIDDEN; dll* need `st_other` processor bits | record flags | IR attributes | - |
| R19 | call-site signature of undefined functions; K&R and prototype mismatches; vararg/byval/sret flags (native-imports 7) | none (native ABI absorbs it) | **side table to design** (addend -> signature entry) | MemberRef signature native, flags in record | IR call types + MSILFixFunctionBitcasts | J4, J4b |
| R20 | function-pointer identity across objects and through aliases (M9) | addresses are unique | FNADDR relocation kind -> one `<msil>fn_` field per resolved function | `<msil>FnAddr` MemberRef | single module | zoo J1 (`table[1] == table[2]`) |
| R21 | refuse mixing triples, ABIs, codegen switches | `e_machine`, `e_flags`, notes | `.note.msil` | record header | module triple and flags | - |
| R22 | where errors surface | link | link (signature errors only with R19) | link | link, including per-TU backend errors (J2) | J2, J4b |

---

## 2. Method

- **Native oracles.** Each probe was linked natively on Windows (`clang --target=x86_64-pc-windows-msvc`
  + `lld-link`, VS developer environment, `native-win.ps1`) and on Linux (this tree's clang for
  `x86_64-unknown-linux-gnu`, VS `ld.lld`, `start.c` and a copy of WSL's `libc.so.6`, run in WSL;
  `run-native-linux.sh`).
- **MSIL paths:**
  - `-S` per TU with Phase-1 `clang -S`, plus ilasm;
  - LTO-style: `clang -c -flto` per TU, then `clang -S -x ir a.bc -Xclang -mlink-bitcode-file ...`
    (IR linking, then one codegen), then ilasm;
  - objects: the objfmt-binding prototype (`mkobj.sh` emulates `clang -c` for option-2 objects, then
    `msilobj link`).
  Every run used .NET 8, .NET 10 and Framework 4.8 (`run-all.sh`).
- **Container representation.** Native x86_64 ELF objects of the probes
  (`-ffunction-sections -fdata-sections`) with `e_machine` patched to 0xC11 (unassigned), read by
  LLVM 22's `llvm-readobj`, `llvm-nm` and `llvm-ar`, and offered to `ld.lld`. This answers whether ELF's
  vocabulary carries each C feature for a machine that LLVM does not know.
- One change to the prototype's emulation, for J3 only: `irinfo.py` did not strip
  `@x = weak dso_local alias ...` lines (its regex expects `dso_local alias` right after `=`), so a
  weak alias crashed `llc`. The one-line fix is `irinfo-weak-alias.diff`. The linker (`Linker.cs`) is
  unchanged.
- Exit status 127 under Git Bash means the process ended with an unhandled .NET exception
  (0xE0434352); the exception text is logged next to it.

---

## 3. Experiments and results

Programs are in `judge-c-fidelity-files/probes/`. The zoo is objfmt-binding's (`objfmt-binding-files/zoo/a.c`,
`b.c`) and J4b is its E16 pair (`sigmismatch/caller.c`, `callee.c`). Logs: `results-msil.txt`,
`results-native-linux.txt`, `results-native-win.txt`.

| # | Program | Native Windows | Native Linux | MSIL `-S` (one TU each) | MSIL LTO (`-c -flto` + IR link) | MSIL objects (option-2 prototype) |
|---|---|---|---|---|---|---|
| J1 | zoo: weak, weak undefined, COMDAT, common, alias, ctors with priorities, pointers in data, equal statics, shared import | 63 (objfmt-binding) | - | n/a | see J2 | **63** on .NET 8, .NET 10, Framework (reproduced) |
| J2 | same | | | | `-c -flto` succeeds; link: `alias @alias_twice is not supported`, `undefined function 'maybe'` (reproduced) | |
| J3 | `p1*`: weak function alias of a static function, overridden by a strong `hook` in another TU; data alias identity | 144; 114 without the override | 144 (both link orders); 114 | error: alias not supported | `-c -flto` ok; link: alias errors | **duplicate symbol: hook** (both link orders); 114 without the override |
| J4 | `p2*`: K&R calls: one argument too many, exact, float parameter promoted to double | 23 | 23 | caller: `undefined function 'f'` | **23** on all runtimes, both triples (`f_bitcast` wrapper; -O0 and -O2) | emulation stops: `mismatched call signature` (MSILISelLoweringCall.cpp:286-293) |
| J4b | prototype `int f(int,int)` vs definition `int f(int)` (objfmt-binding E16) | 41 | 41 | n/a | **41** on all runtimes | links silently, then **InvalidProgramException** at JIT on all runtimes |
| J5 | `p3*`: `__start_myreg`/`__stop_myreg` over sections from two TUs | n/a (COFF uses `$` groups) | 15 | p3a: `external global variable '__start_myreg'`; **p3b compiles, its `section` silently dropped** | same errors at link | not representable (record has no section names) |
| J6 | `p8`: constructor registered through `.init_array` (Linux) or `#pragma section(".CRT$XCU")` + `__declspec(allocate)` (Windows) | 42 | 42 | **3, no diagnostic** (both triples, all runtimes) | n/a (single TU) | not representable |
| J7 | `p4*`: `__declspec(selectany) int sel` in two TUs (IR `comdat any`) | 15 | - | ok per TU | 15 on all runtimes | 15 on all runtimes |
| J8 | `p5*`: `_Thread_local` defined in one TU, used from another | 12 | 12 | error: thread-local not supported yet | `-c -flto` ok; link error | not emulated (Phase 3) |
| J9 | `p6*`: constructors and destructors with priorities across TUs | ctors BCDM; native Windows faults in a destructor at exit (0xC0000005, not investigated) | ctors BCDM | error: global destructor not supported yet | `-c -flto` ok; link error | not emulated |
| J10 | `p9*`: `__attribute__((weak)) int cw = 5` vs common `int cw` (`-fcommon`) | **1** (lld-link, both orders) | **1** (ld.lld, both orders) | n/a | **1** (both orders) | **51** (both orders) |
| J11 | ELF vocabulary with `e_machine` = 0xC11 | | | | | see below |
| J12 | `#pragma comment(lib, "m")` | | native linux-gnu: `!llvm.dependent-libraries` | linux-gnu-msil: **nothing** (unknown pragma, warns only under `-Wunknown-pragmas`); with `-fms-extensions`: `!llvm.linker.options = !{!"-lws2_32"}`; windows-msvc-msil: `!llvm.linker.options` | | |

J11 details (from `results-msil.txt`): `llvm-readobj -h`: `elf64-unknown`, `Machine: 0xC11`. `llvm-nm`:
`W hook`, `t dummy_hook`, `D base_arr`, `D arr_alias`. Symbol table: `hook` is Weak Function in
`.text.dummy_hook` (the aliasee's section, value 0); `arr_alias` is Global Object in `.data.base_arr`.
`llvm-ar` index lists `hook`, `base_arr`, `arr_alias` (weak definitions are indexed, as natively).
Sections `myreg`, `.init_array.120`, `.init_array`, `.fini_array.120`, `.deplibs`, `.llvm_addrsig` are
listed. `tcount` has `Type: TLS (0x6)`. `ld.lld`: `unsupported e_machine value: 3089`.

What clang emits for these constructs on MSIL triples (read from `-emit-llvm` output):
- `@sel = weak_odr ... global i32 7, comdat` with `$sel = comdat any`;
- `@r2 = internal constant ..., section "myreg"` plus `@llvm.used`;
- `@p_init = internal global ptr @init_fn, section ".init_array"` (Linux) or `section ".CRT$XCU"`
  (Windows);
- `declare i32 @f(...)` called as `call i32 @f(i32 1, i32 2)`;
- `declare void @ext_sret(ptr sret(...), i32)`, `declare i32 @ext_byval(ptr ... dead_on_return
  dereferenceable(12))`, `declare i32 @ext_var(ptr, ...)` (`p7.c`).

Clang emits only `comdat any` for C: `Comdat::Largest` comes only from the MS C++ ABI
(`clang/lib/CodeGen/MicrosoftCXXABI.cpp:1967`), and `NoDeduplicate` only from instrumentation and
Internalize.

---

## 4. Findings

### F1. The resolver must be lld/ELF's, verbatim; a hand-written rule set already diverged

- J10: lld/ELF's `Symbol::shouldReplace` lets a common symbol yield only to a *non-weak* definition
  (`lld/ELF/Symbols.cpp:541-546`, `return !other.isWeak()`), and `resolve(const CommonSymbol &)`
  overwrites an existing weak definition (`:607-641`). lld-link agrees (1), and so does IRMover (LTO, 1).
- The prototype's `AddDefs` (`objfmt-binding-files/tools/msilobj/Linker.cs:169-171`) lets any definition
  beat a common symbol and ignores a common symbol that arrives after a weak definition. Result: 51
  in both orders, silently.
- The prototype is not the deliverable. The point is that the semantics live in the resolver, the
  resolver is container-independent, and only a port of a reference implementation is trustworthy.
  lld/wasm (the planned skeleton, lld-flavor) has no common symbols at all (WebAssembly does not
  support them). So msil-ld must take `lld/ELF/Symbols.cpp` as its specification:
  - weak, common and lazy resolution: `:541-556`, `:600-641`;
  - lazy extraction, where a weak undefined does not extract (`:652-680`);
  - duplicate reporting (`checkDuplicate`, `:600-605`).
  These rules must be pinned by tests that compare against native transcripts.
- With D the inputs *are* ELF symbols, so the port is literal. With B the record fields must first
  be mapped onto these states.

### F2. The data model: a linker-laid-out segment with a linker-synthesized `.cctor` is required

mc-streamer section 5.9 recommends a per-object segment with per-object `<msil>init_data` and
`<msil>init_relocs` methods (R5), and stores weak and linkonce address fields "only if the field is
still zero". Measured and read against C:

- **Named sections cannot be contiguous across objects.** Each object's data sits in its own
  `AllocHGlobal` block, so `__start_X..__stop_X` (J5) and a section-registered constructor table
  (J6) cannot span objects. A linker-laid-out segment concatenates same-named input sections, as
  lld/ELF and wasm-ld do.
- **Common symbols** must get the largest size and alignment of all declarations (lld/ELF `Symbols.cpp:618-627`).
  "First non-zero store wins" picks by initialization order, not by size: a buffer overflow when the
  sizes differ. Weak-vs-common (J10) has no correct init-order rule at all.
- **Verification.** Methods other than `.cctor` that store `initonly` fields trigger ILVerify
  [InitOnly] and PEVerify errors (objfmt-container 5.1, measured with netmerge). The D21 gate would
  need non-`initonly` address fields or an allow-list entry.
- **DIFF relocations** across objects become errors (mc-streamer itself says so); with one segment
  they fold at link time.
- **`--gc-sections`** cannot drop data inside an object's segment.

The linker model (objfmt-binding 4.3 steps 4-6; mc-streamer 5.9 model 1) has none of these problems.
The duplication worry ("`-S` would then need a second implementation of the same trailer") goes away
with mc-streamer's own idea: one trailer builder in a library, used both by the AsmPrinter's `-S`
self-link and by msil-ld. This also gives the strongest `-S` vs link equivalence: one implementation
of allocation, image copy, relocation application, constructor ordering and `<msil>Main`.

### F3. Call-site signatures, K&R calls and early diagnostics

- J4: native C calls through unprototyped declarations work (23). The LTO path matches because
  IRMover binds the call to the definition and `MSILFixFunctionBitcasts` inserts `f_bitcast(int32,
  int32)`, which calls `f(int32)` (seen in `p2-O0.il`).
- The object path has no definition in the caller's object. The backend rejects the call
  (`MSILISelLoweringCall.cpp:286-293`, "mismatched call signature", because the declaration type is
  `i32 (...)`). And if a prototype-ful mismatch reaches the linker (J4b), the program links and dies
  at JIT time with InvalidProgramException.
- So objects must carry, for every reference to an undefined function, the call-site signature and
  flags (no prototype, vararg, byval and sret sizes, `msil-import-*`, dllimport; native-imports 7
  lists the same flags for imports). msil-ld must compare that against the definition and emit an
  adapter like `MSILFixFunctionBitcasts` (drop or zero-fill arguments, convert returns), or report a
  link error that names both objects. It must never leave the mismatch to the JIT.
- B2 gets the signature for free: a MemberRef per (name, signature), as mc-streamer's ldexp example
  needs. D1 needs a side table (6.2). A no-merge design (option 4) moves the failure to
  MissingMethodException at JIT time: a dealbreaker.

### F4. Section names must reach the linker; Phase 1 silently drops them (J6)

- `MSILAsmPrinter::emitGlobalVariable` (`MSILAsmPrinter.cpp:1148-1195`) puts every definition into
  the one segment and never looks at `GV->getSection()`. The section-registered constructor idiom
  therefore compiles cleanly and does not run (3 instead of 42). The same happens for any
  `__attribute__((section))` data whose meaning comes from the linker.
- This is the only *silent* divergence found. Every other unsupported construct is a clean error
  (D26).
- Phase 2 must, in objects, carry section names (ELF: the section name itself, `$` included, for
  Windows grouping; B: a record field) and implement these per-triple section semantics in msil-ld:
  - ELF triples: `.init_array`/`.init_array.N`/`.fini_array*` input sections become constructor and
    destructor lists; same-named C-identifier sections are concatenated with `__start_`/`__stop_`
    symbols.
  - Windows triples: `.CRT$XC*`/`.CRT$XT*` are constructor and terminator tables sorted by the
    suffix after `$`, with COFF `$` grouping for other names.
- Until then, `-S` and the LTO path should diagnose a `section` attribute on a global (a small
  MSILPrepareIR check, a natural M12 item) instead of ignoring it.

### F5. Aliases and weak undefined symbols: mode-dependent lowering

- D26 makes aliases and unresolved `extern_weak` errors. backlog 2.1 plans a one-module lowering
  (RAUW every use with the aliasee, plus a forwarding definition). That is correct where the module
  is the whole program: `-S` self-link and post-IR-link LTO codegen.
- In object mode it is wrong for weak aliases. A strong definition elsewhere must win, also for uses
  inside the alias's own TU, as native gives 144 (J3). So object mode must emit the alias as a symbol
  (ELF: a second symbol at the aliasee's section and value, J11) and keep in-TU references to a weak
  alias pointing at the alias symbol.
- Likewise `extern_weak` lowers to null and a trap stub only in whole-program mode. In object mode
  it is an undefined weak symbol.
- The B2 record must carry the alias binding. Without it, the J3 duplicate-symbol error results
  (`irinfo.py:121-123` drops the linkage; `Linker.cs:176-180` makes every alias a strong global).

### F6. ELF's vocabulary covers C; the CIL-specific additions are few and must be specified now

- J11 plus `objfmt-container` 6.2 cover R1-R5, R7, R8, R12-R18, all under an unknown `e_machine`.
- ELF COMDAT supports Any and NoDeduplicate
  (`llvm/lib/CodeGen/TargetLoweringObjectFileImpl.cpp:580-583`), which is everything C produces.
  Wasm supports only Any (`:2279`). The processor-specific group flag bits `GRP_MASKPROC`
  (`llvm/include/llvm/BinaryFormat/ELF.h:1364`) can encode other COFF selection kinds for `EM_CIL` if
  C++ ever needs them.
- The additions D needs (section 6.2 gives the contract):
  1. Relocation kinds per operand class, not one `R_CIL_TOKEN32` (see verdict item 4).
  2. A signature-and-flags table for references (R19), indexed from the relocation addend.
  3. `st_other` processor bits for dllimport/dllexport (ELF.h already uses this space:
     `STO_AARCH64_VARIANT_PCS` `:444`, `STO_RISCV_VARIANT_CC` `:733`).
  4. A PC-relative or ADD/SUB-pair data relocation for symbol differences (R11).
  5. Codegen-switch flags in `.note.msil` next to the triple (R21).
- One risk is specific to route D-ii: the backend's own writer must reproduce what
  `TargetLoweringObjectFileELF` does for native ELF, namely unique per-symbol sections, COMDAT
  groups, SHF_MERGE detection via `getKindForGlobal`, explicit sections, `retain`,
  `.init_array.N`/`.fini_array.N`, and `.tdata`/`.tbss`. That is where a fidelity bug would hide.
  The mitigation is the linkage-parity oracle (6.5).

### F7. The B2 record has to re-specify ELF semantics, and the prototype shows what gets missed

What B2 does well, measured by objfmt-binding:
- the zoo and 93/93 of the M1 corpus, end to end on three runtimes;
- signatures native (F3);
- objects buildable from `-S` + ilasm, which gives msil-ld a test vehicle before the MC writer;
- C# netmodule inputs nearer.

What it gets wrong or lacks:
- measured: the alias binding (J3) and the weak-vs-common rule (J10);
- not prototyped: TLS, DIFF, section names, destructors, retain, linker options, COMDAT kinds and
  offset data aliases (objfmt-binding 12).

None of this is impossible, but each item is a private design with no external specification and no
independent tool to check it (`llvm-nm` prints "no symbols" for B objects; ildasm does not decode the
record). The object also has two sources of truth: metadata rows, and record entries keyed by token.
Under this lens that is the main reason B scores one point below D.

### F8. LTO-only: faithful linking, late diagnostics

- **Faithful where supported.** Where the backend supports the construct, IR linking gave native
  results on every probe (J4 23, J4b 41, J7 15, J10 1). It is the best-tested implementation of LLVM
  linkage there is.
- **Late diagnostics.** As the sole format it moves every backend diagnostic to link time: `clang -c
  -flto` succeeded for every TU of J2, J3, J5, J8 and J9, whose errors then appeared during the link.
  It also cannot mix prebuilt CIL objects, the static C helper archive (Design 6) would have to ship
  as bitcode, and ThinLTO and partitions need objects anyway (objfmt-container 7, lld-flavor).
- **Bring-up is still right.** As the bring-up path for P2-B it is right; the backend gaps it exposes
  (alias, `extern_weak`, destructors, TLS, sections) must be closed for the object path too.

### F9. No merge (option 4): errors at JIT time and wrong initialization order

- **Errors move to JIT time.** Missing members, wrong signatures and missing assemblies surface at
  JIT time of the caller: exports-multiasm item 3 (MissingMethodException, TypeLoadException,
  FileNotFoundException) and objfmt-binding E14.
- **Initialization order is not C's.** Lazy type initializers cannot order constructors across
  assemblies. A cycle reads a stale NULL address unless initialization is split into two phases
  (exports-multiasm E4b).
- **Link semantics are only partly honoured.** Losing weak and COMDAT copies stay as dead code,
  hidden symbols must become public types, and an importer's `ldftn` breaks Framework pointer
  identity (exports-multiasm E2b).
- **Verdict.** Acceptable only for P2-C library-to-library linking, as both recons say; a dealbreaker
  for static linking.

### F10. Object-format gates in the frontend are container-independent (J12)

- MSIL triples have object format MSIL, so clang code gated on `isOSBinFormatELF()` or
  `isOSBinFormatCOFF()` behaves differently from native. On `x86_64-unknown-linux-gnu-msil`,
  `#pragma comment(lib, ...)` is an unknown pragma: no handler is installed unless MicrosoftExt, ELF or
  AIX (`clang/lib/Parse/ParsePragma.cpp:475-480`). It is dropped silently by default, while native
  `x86_64-unknown-linux-gnu` emits `!llvm.dependent-libraries`
  (`clang/lib/CodeGen/CodeGenModule.cpp:3984-3995`).
- native-imports 7 reports `!{!"-lws2_32"}` for linux-gnu-msil; that holds only with
  `-fms-extensions` (re-measured, J12).
- The Phase 1 clang recon (`recon/clang.md:600`) judged the pragma "fine" for Phase 1. With a
  linker it is a silent loss. Choosing the ELF *container* (route D-ii keeps `Triple::MSIL`) does not
  change these gates. Phase 2 should install the handler for MSIL triples (lib kind only, as ELF
  does) and audit the other `isOSBinFormat*` gates that affect linkage. That audit is not done here.

### F11. Safety of the containers against misuse

- **D:** `ld.lld` refuses EM_CIL (J11) and objfmt-container measured the same for `lib.exe`
  and `llvm-lib`. An ELF object cannot be executed.
- **COFF machine 0 is a hazard:** `link.exe` and `lld-link` accept it as x64 and would link CIL
  bytes as machine code (objfmt-container 6.2). D-COFF is therefore pruned.
- **B:** a netmodule-shaped object refuses to load. An assembly-shaped one runs until a placeholder
  is hit (DllNotFound/FileNotFound at first use, objfmt-binding E14), so B must use netmodules.

### F12. One program through `-S`, objects and LTO

- **Today.** The three paths agree on everything tested *except* where one path errors (a clean
  diagnostic) and on J6, where `-S` is silently wrong.
- **What equivalence needs in Phase 2:**
  - the shared trailer builder (F2);
  - the resolver port (F1), since LTO resolution (lld + IRMover) and object resolution (msil-ld)
    must agree;
  - adapters (F3), which the LTO path has and the object path lacks;
  - section semantics in all three paths (F4);
  - tests that run each link-semantics program through all three paths and compare against a native
    transcript (6.5).
- tests-p2's O3 (canonical dump of `-c` + msil-ld equals that of `-S` + ilasm) and O4 (LTO PE equals
  ilasm of `--lto-emit-asm`) check codegen equivalence. They do not check link semantics, because a
  single TU exercises none.

---

## 5. Candidates (pruned to six) and scores under this lens

Pruned without scoring:
- **C without a record:** pure netmodules carry no C link semantics (objfmt-container 5.2).
- **Binding 5 (GOT):** a `calli` through a slot with a mismatched signature is undefined behaviour,
  not a diagnostic, and the slot adds nothing within one merged module (objfmt-binding 8).
- **D-COFF:** machine 0 is linked as x64 by `link.exe` and `lld-link`, machine CEE is rejected by
  every tool, there is no hidden visibility, and it brings Windows conventions to Linux triples
  (objfmt-container 6.2, 6.8).
- **A with binding 2:** a custom container with metadata names is B.
- **mc-streamer's per-object data model:** a data model, not a container; rejected by F2.

| Candidate | Score | One-line rationale |
|---|---|---|
| **D1: ELF ET_REL (EM_CIL, route D-ii) + relocations, with objfmt-binding's binding model** | **8** | gABI semantics with lld/ELF as the executable reference; independent tools; parity oracle; needs a designed call-site signature table and per-operand-class relocation kinds |
| B2: netmodule envelope + symbolic metadata references + link record | 7 | native signatures and an ilasm oracle; end-to-end evidence; but a private re-specification of ELF semantics, already wrong twice (J3, J10); no LLVM-tool visibility |
| D2: ELF ET_REL with an embedded object-local ECMA-335 metadata section (MSVC `/clr` shape), tokens by decoding, ELF relocations for data | 7 | ELF semantics plus native signatures; two symbol spaces (ELF symbols and metadata rows) must agree, as MSVC's CLR-token symbols do |
| A1: wasm-style custom container + relocations | 6 | can carry everything, but every encoding is bespoke and the wasm template lacks commons, finis, section-name semantics and COMDAT kinds; no tool to check it |
| E3: bitcode only, LTO only (sole format) | 5 | the most faithful resolution, but per-TU diagnostics move to link time; no mixing with CIL objects; version-locked libraries; needs objects for ThinLTO anyway |
| B4/C4: one assembly per object, forwarder facade, no merge | 2 | link errors become JIT-time exceptions; constructor priorities need a driver; cycles; dead losers; hidden made public |

Details:

- **D1, score 8.**
  - Gives standard, complete C link semantics (R1-R18 native in ELF), with lld/ELF's symbol code
    as the literal specification (F1). The linkage half of every object is checkable against native
    ELF objects (6.5), and `ld.lld` refuses the objects.
  - Weaknesses: the signature side table (R19) and the CIL-specific relocation kinds are new design,
    and the container draft gets the latter wrong (`R_CIL_TOKEN32` alone). D-ii must reproduce
    TLOF ELF's IR-to-section mapping. Objects cannot be built by ilasm, so the object writer is
    checked only at the linked-output level (tests-p2 O3) plus the parity oracle.
  - Dealbreaker if the single token relocation kind of objfmt-container 6.6 ships unchanged; none
    otherwise.
- **B2, score 7.**
  - Strengths: signatures, BCL references and future value types are native metadata (F3). The
    `-S` text *is* the object, so ilasm gives an independent assembler as oracle. It is the only
    candidate with end-to-end evidence (zoo, 93/93 corpus, archives, reference binding).
  - Weaknesses: every C semantic is a private record field (F7). Two of them were wrong in the
    prototype, and seven are unspecified. There are two sources of truth per object. LLVM tools are
    blind to objects without an MSIL `SymbolicFile`.
  - Dealbreaker only if the record is not specified field by field against gABI and lld semantics.
- **D2, score 7.**
  - The MSVC `/clr` precedent (objfmt-container 6.1) minus token relocations. ELF symbols, groups and
    sections carry C linkage, and a `.cil.meta` section holds a real object-local ECMA-335 metadata
    root (MemberRefs with call-site signatures, TypeRefs and TypeSpecs, BCL references).
  - It solves R19 natively and drops the ~860 B of RELA per KB of IL. The price is the
    MSVC-style binding of ELF symbols to metadata rows (by name, or a CLR-token symbol class) and a
    metadata builder inside the MC writer.
  - The metadata builder is needed anyway in llvm/lib for msil-ld (metadata-writer), so the cost is
    plumbing, not new technology. It is a legitimate refinement of D1 if the D1 side tables grow into
    a mini-metadata (section 8).
- **A1, score 6.** Its fidelity potential equals D1's, but nothing checks it. It needs ObjectYAML and
  readers before any test can inspect linkage (objfmt-container 3). Wasm's linking section has no
  common symbols, no fini functions, and COMDAT Any only (`TargetLoweringObjectFileImpl.cpp:2279`).
- **E3, score 5 as the sole format.** F8. As the P2-B bring-up path and the permanent `-flto` path it
  is the right choice (objfmt-container 11.1, lld-flavor P2B-1).
- **B4/C4, score 2.** F9.

---

## 6. Recommendation and fidelity contract

### 6.1 Decision

Adopt **D1**: ELF ET_REL for every MSIL triple, route D-ii (own `MSILObjectWriter`, D5 kept),
fork-reserved `e_machine` with `.note.msil` as the identification, with the binding model of
objfmt-binding:
- bind by name;
- renumber every token;
- one linker-laid-out data segment;
- a linker-synthesized `.cctor` and `<msil>Main` from a trailer builder shared with `-S`;
- link-time signature checks with adapter thunks.

Keep **E** as the bring-up path (`clang -c` writes bitcode until CIL objects land) and as `-flto`.
Keep B2's prototype objects (`-S` + ilasm) as a test vehicle for msil-ld's ECMA-335 reader and merge
code, which P2-C needs anyway.

If the user prefers B for ildasm-readable objects, B2 is acceptable provided the record is specified
against the same contract (6.2) and passes the same matrix (6.5).

### 6.2 What a CIL object must carry (P2-A contract; D encoding, B equivalent in brackets)

1. **Symbols** with binding (local, global, weak), common (size and alignment), visibility,
   type (function, object, TLS), section and value, and size.
   - Aliases are extra symbols on the aliasee's section and value; data aliases carry an offset.
   - Undefined weak symbols stay undefined STB_WEAK.
   - [B: per-row record entries, aliases with binding and offset.]
2. **Sections**:
   - one per function (`.cil.text.<sym>`) and one per global, with the IR's explicit section name
     when present, including `$` for Windows grouping;
   - COMDAT groups (Any, NoDeduplicate; diagnose other kinds);
   - SHF_MERGE|SHF_STRINGS for mergeable strings, SHF_GNU_RETAIN for `retain`, `.tdata`/`.tbss`;
   - `.init_array.N` and `.fini_array.N` for `llvm.global_ctors` and `llvm.global_dtors`;
   - `.deplibs` or SHT_LLVM_LINKER_OPTIONS for `llvm.dependent-libraries` and `llvm.linker.options`.
   - [B: a section-name field per data symbol, init and fini lists, a linker-options list.]
3. **Token relocations, one kind per operand class**, always against a symbol, never a section plus
   addend:
   - `METHOD` (call, ldftn, jmp target MethodDef);
   - `FNADDR` (the function's canonical `<msil>fn_` field: M9 identity, zoo J1);
   - `GADDR` (a data symbol's address field; when the definition turns out to be a function the
     linker may substitute its `FNADDR` field, which is what native `&sym` means);
   - `SIG` (a `calli` StandAloneSig) and `LOCALSIG` (the fat-header LocalVarSigTok);
   - `TYPE`, `MEMBERREF` (BCL members, bound by P2-B), `USERSTRING`, and later `TLSOFF`
     (an `ldc.i4` immediate).
   [B: decoded from IL; meaning from the object's own rows.]
4. **A reference table** (a per-object `.msil.refs` section). Each `METHOD`
   relocation's addend indexes an entry with:
   - the call-site signature (an ECMA-335 MethodRefSig that uses object-local type indices, never
     final coded indices; signatures hold compressed coded indices that cannot be patched,
     objfmt-binding 3.1);
   - flags: no-prototype, vararg, byval/sret size and alignment, dllimport;
   - explicit import module and name (`msil-import-*`).
   Definitions get the same entry in their per-method side section (objfmt-container 6.6
   `.msil.method.<sym>`).
   [B: MemberRef signature + record flags.]
5. **Data relocations**: `ADDR64`/`ADDR32` against data or function symbols with addend and
   unaligned sites, and a PC-relative or ADD/SUB pair for symbol differences. They are applied by the
   synthesized `.cctor`; differences fold at link time.
6. **`.note.msil`**: triple, format version, and the codegen-switch and ABI flags that must agree
   across objects. msil-ld refuses a mismatch with a message naming both objects.
7. **`st_other` processor bits** for dllexport/dllimport on Windows triples (P2-C export selection,
   exports-multiasm).

### 6.3 What msil-ld must do (P2-B)

- **Resolution:** port lld/ELF's rules (F1), including weak vs common, the largest common, lazy
  extraction (weak undefined and common references do not extract), and duplicates.
- **Signatures:** compare every reference entry with the definition. Emit adapters (a link-time
  `MSILFixFunctionBitcasts`: drop extra arguments, zero-fill missing ones, convert float
  promotions) with a warning, or give an error naming both objects.
- **Data and initialization:** a single segment laid out from input sections, with named sections
  concatenated in input order. Synthesize `__start_`/`__stop_`, the constructor and destructor
  lists (ELF `.init_array*`, Windows `.CRT$XC*`/`.CRT$XT*` sorted by suffix), and the `.cctor`,
  through the shared trailer builder.
- **Weak undefined:** null address and `<msil>fn_` fields, and a trap stub for direct calls.
- **Garbage collection:** `--gc-sections` over relocations, keeping `llvm.used` symbols as wasm-ld
  does. That is more conservative than lld/ELF, which may drop `used` data; keeping more is never
  wrong.

### 6.4 Backend and frontend items that are fidelity bugs today

- MSILPrepareIR should diagnose `section` on a global in `-S` and whole-program modes until the
  trailer builder honours it (F4; silent today, J6).
- Alias and `extern_weak` lowering must depend on the mode (F5): RAUW or forwarding and null-plus-trap
  in whole-program modes; symbols in object mode. This amends backlog 2.1 before M12 implements it.
- `#pragma comment(lib)` on linux-gnu-msil: install the pragma handler for MSIL triples
  (`ParsePragma.cpp:475-480`), lib kind only, as for ELF (F10).

### 6.5 Tests that guard fidelity

1. **Link-semantics matrix.** The zoo plus `p1`-`p9` of this report (and later TLS and destructors)
   as `msil/test/M2` multi-TU tests, on both triples. Each program has a native transcript from the
   Windows oracle and from tests-p2's E7 Linux oracle, and runs through the object path and the
   LTO path (and `-S` where it is one TU). Every path must equal the native transcript or stop with a
   named diagnostic, never with a JIT-time exception.
2. **Linkage parity (D only).** Compare `llvm-readobj --symbols --section-groups --sections` of
   `clang --target=<t>-msil -c` with the same IR compiled by
   `llc -mtriple=x86_64-unknown-linux-gnu -function-sections -data-sections`. Names, bindings, types,
   visibility, group membership, data section names and flags, and init/fini priorities must match;
   payload sections are excluded. This makes native ELF the oracle for the linkage half of every
   object.
3. **Signature tests:**
   - K&R calls with extra arguments, missing arguments and float promotion;
   - int vs pointer parameters;
   - an unprototyped call to a variadic definition;
   - a prototype mismatch (J4b).
   Each must give the native transcript through an adapter, or a link-time error.
4. tests-p2's O3 and O4 stay as the codegen-equivalence oracles. This matrix adds the
   link-semantics oracle they lack.

---

## 7. Where the recon reports should not be taken at face value

- **objfmt-container:**
  - 6.6's relocation list (`R_CIL_TOKEN32`, `R_CIL_LOCALSIG32`, `R_CIL_ADDR64/32`) cannot
    distinguish `call f` from `&f` against one symbol (verdict item 4).
  - 6.4 lists aliases as "not tried". They are now verified as plain ELF symbols (J11).
  - Its C-semantics evidence is representational (tools read the bits); no linker ran on those
    objects. Linking fidelity comes from the resolver (F1).
  - D-ii's "about 1,000 lines" writer must also reproduce TLOF ELF's section selection (F6).
- **objfmt-binding:**
  - Section 10 marks aliases "tested", but weak aliases fail (J3).
  - Its common rule "a definition beats common" contradicts lld/ELF, lld-link and IRMover (J10).
  - Section 6 names only aliases and `extern_weak` as LTO gaps; destructors, TLS and named sections
    also fail, and `section` is silently ignored (J5, J6, J8, J9).
  - Its recommendation of option 2 is sound on signatures and the ilasm oracle, but undervalues
    having an external specification for the record's semantics.
- **mc-streamer:** R5 and 5.9's per-object segment model should be rejected (F2). Its R3 fixups per
  operand class and its shared trailer builder are right, and both are adopted above.
- **native-imports 7:** the `-lws2_32` linker option on linux-gnu-msil appears only with
  `-fms-extensions`; the default drops the pragma silently (J12).
- **backlog 2.1:** the one-module alias lowering is correct only in whole-program modes; object mode
  needs symbols, and weak aliases must not be RAUW'd (F5).
- **lld-flavor:** lld/wasm is the right skeleton for the driver and the writer, but not for the
  symbol rules. Take `lld/ELF/Symbols.cpp` for those (F1).

---

## 8. What would change this verdict

- **If D1's side tables grow into a copy of ECMA-335 metadata.** If, once BCL members, value-type
  signatures (Design 2.3, FeatureIdeas 3) and delegate types for P3 thunks are designed, D1's
  `.msil.refs`/`.msil.sig` tables turn into an ad-hoc copy of the MemberRef, TypeRef, TypeSpec and
  StandAloneSig tables, then D2 (embed real metadata in the ELF object) or B2 becomes the better
  choice.
- **If the B2 record were specified and visible.** If it were specified field by field against
  gABI and lld semantics, passed the matrix of 6.5, and an MSIL `SymbolicFile` gave `llvm-nm` and
  `llvm-ar` visibility, B2 would tie with D1. Its ilasm oracle would then tip the balance.
- **If ELF needs per-token custom encodings.** A measured case where ELF relocations cannot express a
  CIL fix-up without such encodings (for example, relocations inside signature blobs) would favour
  B or D2.
- **If the user never needs CIL objects.** If prebuilt CIL objects are never needed, and link-time
  backend diagnostics are acceptable for the user's builds, E would rise to about 7. No recon
  supports that today: the helper archive, ThinLTO and P2-C all want objects.

---

## 9. FeatureIdeas (preclusion only)

- **Idea 7 (several triples in one assembly):** D1 objects stay per triple (`.note.msil`), and a
  linker-synthesized initializer can be generated per variant. Not precluded. The per-object-init
  model would not preclude it either; F2 rejects that model on C grounds.
- **Idea 3 (typed pointers, value types):** 6.2 item 4 requires signatures with object-local type
  indices, so type references can be renumbered later. A D1 design that stored final coded indices
  in blobs would preclude it.
- **Idea 6 (enclosed instance model):** it needs the linker to own layout and initialization, which
  D1 and B2 both give. The per-object init model would bake per-object allocation into objects.
- **Ideas 1, 4 and 5:** not affected. msil-ld emits every row anyway, and the LTO path stays.

---

## 10. Reproduction

Copy `judge-c-fidelity-files/probes/*.c` to `C:\llvm-msil-scratch\p2\judge-c-fidelity\probes\`. Add
`objfmt-binding-files/zoo/{a,b}.c` (to `x1\`) and `objfmt-binding-files/sigmismatch/*.c` (to `probes\`).
Copy `objfmt-binding-files/tools` to `tools\` and apply `irinfo-weak-alias.diff`. Then:

```text
cd tools/msilobj && dotnet build -c Release -o out        # option-2 prototype linker
x1/rc8.json, x1/rc10.json: { "runtimeOptions": { "tfm": "net8.0", "framework":
    { "name": "Microsoft.NETCore.App", "version": "8.0.0" } } }   (10.0.0 for rc10)
./run-all.sh                 # J1-J12, MSIL paths and ELF representation -> results-msil.txt
./run-native-linux.sh        # native Linux oracle (needs probes/native-linux/{libc.so.6,start.c}) -> results-native-linux.txt
powershell -File native-win.ps1   # native Windows oracle (VS environment) -> results-native-win.txt
```

- `libc.so.6` is a copy of WSL Ubuntu's (tests-p2 E7) and `start.c` is
  `tests-p2-files/linux-oracle/start.c`.
- Times: `run-all.sh` about 3 minutes and the native scripts under 1 minute each, on the shared
  host.
- Installs: none. Build trees: none.
