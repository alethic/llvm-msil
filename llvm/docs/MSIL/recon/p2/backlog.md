# Recon: Phase 2 backlog triage (`backlog`)

- Key: `backlog`. Date: 2026-10-21. Checkout: branch `msil` at `1ea64fe9fc7c` (LLVM 24.0.0git).
- Question: which backlog items (HANDOFF open issues 10 and 11, the M11 leftovers, the 14 proposed
  Design.md updates of Status.md section 6, and the open questions of `msil/orchestration/P2/inputs.md`
  section 2) are still true at HEAD, how big each is, where each belongs (a Phase 2 milestone, a small
  standalone milestone, Phase 3/4, the user, or nowhere), and which of them the Phase 2 object/linker
  choice changes. Also: are Design.md sections 5 and 7 and plan section 7 (P2-A/B/C) consistent with
  the 2026-10-14/15 scope decisions?
- Method: every item was reproduced with the shared build's tools (`D:\llvm-msil\build\bin`: clang,
  llc, split-file; no build was run), with the rustc already on this host (1.99.0, LLVM 23.1.1), the
  .NET runtimes (8.0.32, 10.0.13, Framework 4.8; WSL .NET 10.0.13), Framework ilasm 4.8, ILVerify
  10.0.12 and PEVerify through `msil/utils/msil-verify.py`, and read-only `gh api` queries. Sources,
  scripts and a trimmed log of every experiment are in [`backlog-files/`](backlog-files/)
  (`results.txt` is the log; 48 KB in all). Every path and line number below was checked in this
  checkout; anything not checked is marked *unverified*.
- Scratch: `C:\llvm-msil-scratch\p2\backlog\` (about 25 MB: two tiny C# projects, IL, binaries). No
  tracked file was modified, no cmake or ninja ran, nothing was installed (rustc, gh, binutils in WSL
  were already present). D: has 19 GB free.

---

## 0. Summary

1. **Every backend item of open issue 10 is still true at HEAD**, and three of them are cheap: with
   real rustc 1.99 output, llc reports `alias @sum_b is not supported for MSIL`, `inline assembly is
   not supported for MSIL` (three `black_box` calls) and `runtime helper '__udivti3'` / `'__fixdfti'`
   (section 2). A simulation of the proposed lowerings on that module (alias -> forwarding
   definition, empty asm -> compiler fence + `llvm.fake.use` + `llvm.sideeffect`, i128 expansion by
   llc options) **runs and returns the native Rust result (88) on .NET 8, .NET 10 and Framework 4.8**
   and passes the D21 verification policy.
2. **i128 division and FP<->i128 conversion work today behind two hidden llc options.**
   `-mllvm -expand-div-rem-bits=64 -mllvm -expand-fp-convert-bits=64` (what
   `setMaxDivRemBitWidthSupported(64)` / `setMaxLargeFPConvertBitWidthSupported(64)` would set) makes a
   2,000-iteration test of udiv/urem/sdiv/srem and all eight conversions match native clang +
   compiler-rt on .NET 8, .NET 10, Framework 4.8 (Windows triple, -O0 and -O2) and WSL .NET 10 (Linux
   triple), verifier-clean. Cost: 600-1,050 IL instructions per inlined division site, about 3x slower
   than native compiler-rt and 2x slower than .NET 8's `UInt128`. Strict (constrained) conversions and
   `f16`<->i128 stay helpers. `_BitInt(200)` division already works today (widths above 128 are always
   expanded). This changes Design.md 7.1's 128-bit row and plan P3-B (section 2.2).
3. **Recommended: a small standalone milestone M12 before P2-A** (runs while the user decides the
   object format; no widely-included header): i128 expansion, GlobalAlias lowering for one module,
   the `black_box` asm forms (IR and C), a `va_arg`-of-aggregate diagnostic (still a crash), byval
   slot reuse (10 byval call sites give a 640-byte frame instead of 64), plus test-infra fixes.
   About 350 lines of code, 900 of tests; MSIL target + `CGStmt.cpp` rebuild (section 9).
4. **The C spelling of the import attributes is a clang `Attr.td` edit** (clang-wide rebuild), so it
   belongs in P2's first, header-batch milestone; the attribute stays useful after msil-ld resolves
   natives (overrides, cross-compiles, renamed entries, wasm-port source compatibility). swiftcc is
   a WebAssembly-style ~80-line port with no Swift toolchain here: Phase 4 or drop.
5. **Test infrastructure:** a **Linux-native oracle needs no sudo**: the fork's clang compiles for
   `x86_64-unknown-linux-gnu` on Windows and WSL's preinstalled binutils `ld` links it with a
   5-instruction `_start` against `libc.so.6`/`libm.so.6`; four corpus programs match their
   `.expected` files (section 3.3). `kr-legacy.c` still compiles only because clang defaults to gnu17
   (`__STDC_VERSION__ 201710L` at HEAD); pin `-std=gnu17` in `%{msil_cflags}`. lit already reads
   `@response` files, which fixes `lit-legs.ps1`'s blank limit. Status.md's test counts are exact at
   HEAD (20/136/111/19) and the README's stale phrases are gone: drop that item.
6. **`msil-ci.yml` cannot even be dispatched today:** GitHub answers `HTTP 404: workflow msil-ci.yml
   not found on the default branch` because the default branch is `main` (the pure upstream mirror).
   Running CI needs a user decision about the default branch, not only the SHA pin. The pin itself
   can be filled in: `actions/setup-dotnet@v5` is `26b0ec14cb23fa6904739307f278c14f94c95bf1`
   (v5.4.0); v6.0.0 is `a98b56852c35b8e3190ac28c8c2271da59106c68`. Plan section 8 still says `v4.x`.
7. **The MIR FP read-back failure is an upstream regression in every target**, not an MSIL or wasm
   quirk: since upstream `41c214f0b115` (2026-05-07, the new IR float literal syntax) the MIR printer
   writes `double +inf` through `ConstantFP::printAsOperand` but `MILexer` was never taught the new
   literals; x86 GlobalISel `G_FCONSTANT double +inf` fails the same way. The wasm `-stop-after`
   verifier defect is still present upstream. Both: upstream reports (the user's call), drop from P2.
8. **New evidence for the P3 exit/atexit question:** a native `exit(3)` P/Invoked from managed code
   runs native atexit handlers but **not** `AppDomain.ProcessExit` on .NET 8 (Windows) and .NET 10
   (WSL); Framework 4.8 ran ProcessExit. And `atexit` itself is **not exported** by `ucrtbase.dll` or
   glibc's `libc.so.6`. So `exit`/`atexit` must be toolchain-implemented once managed destructors
   exist (P3-A); P2's native resolution may bind `exit` natively until then (section 5).
9. **Status.md section 6 has 14 proposals, not 13** (HANDOFF and inputs.md say 13). All 14 still
   hold at HEAD (re-measured: `dotnet prog.dll` exits `0x80008083`, `ilasm -dll` writes PE32 machine
   `0x014C`, `__ELF__` absent, `long double` table exact). Recommendation: accept all 14, with item 4
   amended (5.3's library-name rule is self-contradictory) and items 1-4, 8, 13 collected into one
   "As built" note at the end of Design.md section 12; exact texts in section 6.
10. **Consistency:** the 2026-10-14/15 edits landed, but eight sentences still need work (section 7):
    plan P2-B's `(-r)` contradicts the decision that the spelling is open; nothing says what msil-ld
    binds with no references or how it tells BCL references from toolchain-built assemblies; the
    symbol-table "custom attribute" (Design 5.4, plan P2-C) has no type that satisfies "no runtime
    support assembly"; `[SuppressGCTransition]` is not phrased as a switch; P2-B and P2-C both claim
    managed-assembly and native resolution; the helper archive has no P2 home.

| # | Item | True at HEAD? | Size | Home | P2 object/linker choice changes it? |
|---|---|---|---|---|---|
| 10.1 | GlobalAlias (rustc mergefunc, C `alias`) | yes (rustc and C) | ~80 code + ~300 test | **M12** (one-module lowering); P2-A object symbols; P2-C export names | **yes** |
| 10.2 | i128 div / FP<->i128 inline | yes; options prove the fix | 2 lines (+60 optional outlining) + ~250 test | **M12** | indirectly (helpers later, COMDAT dedupe) |
| 10.3 | `black_box` empty asm with operands | yes (IR and C) | ~60 backend + ~30 clang + ~200 test | **M12** | no |
| 10.4 | swiftcc | yes | ~80 + ~200 test | **Phase 4** (or drop) | no (signatures) |
| 10.5 | C spelling of `msil-import-module/name` | yes (no attribute) | ~90 + ~150 test, clang-wide rebuild | **P2 header milestone** (Attr.td batch) | **yes** (where the import lives) |
| 10.6 | one frame object per byval call site | yes (640 vs 64 bytes) | ~40 + ~80 test | **M12** (or P4-A) | no |
| 11.1 | `va_arg` of aggregate crashes | yes (MSIL and native x86) | ~15 + ~30 test | **M12** | no |
| 11.2 | msil-ci.yml never run, SHA pin | yes; not dispatchable from `main` | 2 lines | **user** (default branch, enable); pin in next infra WP | no (P2 adds CI legs) |
| 11.3 | no Linux-native oracle | yes, but no sudo needed | ~100 infra | **P2 test-infra WP** (or M12.f) | no |
| 11.4 | `kr-legacy.c` vs C23 | latent (gnu17 default) | 1 line | **M12.f** / next infra WP | no |
| 11.5 | lit-legs.ps1 blanks | yes | ~15 | P2 infra WP (low) | no |
| 11.6 | test counts in docs | counts are exact today | 0 | **drop** (standing rule) | no |
| 11.7 | Status.md 6 Design updates | 14 items, all hold | doc only | **user** (Design.md is theirs) | items 4, 12 interact |
| M11 | MIR FP inf/NaN read-back | yes, upstream regression | upstream ~20 lines | **user** (upstream report) / drop | no |
| M11 | wasm `-stop-after=wasm-isel` verifier | yes (wasm only) | upstream | **user** / drop | no |
| M11 | WSL unattended upgrades | yes (enabled, active) | ~20 harness or 1 apt command | **user**, or harness-private runtime in P2 infra | no |

---

## 1. Environment notes

- `D:\llvm-msil\build\bin` has clang, llc, opt, FileCheck, split-file, not, count, llvm-profdata;
  registered targets: msil, wasm32/64, x86/x86-64. No lld binary.
- rustc 1.99.0 (`b940084d7`, LLVM 23.1.1) with only the `x86_64-pc-windows-msvc` target is on PATH
  (rustup). Its LLVM 23 IR text parses in this LLVM 24 tree.
- The native oracle for MSVC triples needs compiler-rt for i128 helpers: the fork's build has no
  `lib/clang`, so the experiments linked the VS-bundled
  `VC\Tools\Llvm\x64\lib\clang\22\lib\windows\clang_rt.builtins-x86_64.lib` (the reason
  `msil/test/M1/04-frames/int128-return.c:18-19` avoids division).
- PowerShell splits a native argument like `-output=merge-run.exe` at the dot; quote it
  (`'-output=x.exe'`). Two assemblies with the same name in one directory collide under
  `dotnet x.dll` (hostpolicy puts every app-local assembly on the TPA list), the reason for D28's
  private directories; one experiment hit it.

---

## 2. Backend follow-ups (open issue 10)

### 2.1 GlobalAlias lowering

**Still true.** `MSILPrepareIR::runOnModule` reports and erases every alias
(`llvm/lib/Target/MSIL/MSILPrepareIR.cpp:1102-1106`, documented at lines 67-69); the AsmPrinter
backstop is `MSILAsmPrinter::emitGlobalAlias` (`MSILAsmPrinter.cpp:1197-1202`) and the data-segment
backstop `MSILDataSegment.cpp:273-274` (`fail("address of an alias or ifunc")`). D26 makes aliases an
error. Reproduced three ways (`backlog-files/alias-rust/`, `results.txt` E1):

- hand-written IR (`alias.ll`: an internal function alias, an external one, a data alias with offset);
- C: `__attribute__((alias("impl")))`, `static` alias, `weak` alias and an array alias
  (`calias.c`): 4 errors on `x86_64-pc-windows-msvc-msil`. musl's `weak_alias`, glibc's
  `strong_alias` and many portable libraries use this, so a P5 static libc needs it too;
- rustc: `merge.rs` (two identical `#[no_mangle] pub extern "C"` functions) gives
  `@sum_b = unnamed_addr alias i32 (ptr, i64), ptr @sum_a` (rustc's default
  `-Z merge-functions=aliases`; the internal twins `priv_a`/`priv_b` were merged by RAUW, no alias).

**Proposed one-module lowering (M12, all in MSILPrepareIR, about 80 lines).** For each `GlobalAlias`
with an aliasee object (`GA.getAliaseeObject()`, chains included) and a constant offset:

1. Replace every use of the alias by the aliasee (`getelementptr i8, ptr @aliasee, i64 off` for an
   offset). This keeps `&alias == &aliasee` (the M9 `'<msil>fn_<name>'` field of the aliasee is the
   only address). A call whose type differs from the aliasee's goes through
   MSILFixFunctionBitcasts, which runs after MSILPrepareIR (`MSILTargetMachine.cpp:229-239`).
2. If the alias is a function alias with non-local linkage, define a forwarding function under the
   alias's name (same type, linkage, visibility): `ldarg.0 ... ldarg.n; call 'aliasee'; ret`. Nothing
   references it after step 1; it exists so the name is a `public` method (an entry point named
   `main` that is an alias keeps working, and P2-C can export the name). Measured in the Rust
   experiment: the forwarding method is verifier-clean. A `jmp 'sum_a'(...)` body also runs on all
   three runtimes, but ILVerify and PEVerify report `[Unverifiable] Instruction cannot be verified`,
   so the plain call is better (the JIT inlines it).
3. Erase the alias. External data aliases need no name in a one-module world (D26 rejects external
   globals anyway); `weak` aliases are definitions, as D26 treats weak definitions.
4. ifuncs stay an error (they need a resolver call at startup; Phase 3 at the earliest).

**What the Phase 2 choice changes.** Object formats with a symbol table (wasm-style A, ELF D) carry an
alias as a second symbol on the same function or data offset ("one method, two names",
`objfmt-container.md` 6.4), so the linker binds other objects' references to the aliasee's token and
weak aliases resolve with ordinary weak semantics; step 2's forwarding definition is then needed only
for exported names (P2-C: one public method per exported name on the export class; exported data
aliases get an accessor). Metadata-only objects (netmodule B/C, objects-as-assemblies) have no second
name for a method and need the forwarding stub or a side table for every external alias. The M12
lowering is still required in every option, because the bring-up path (bitcode/LTO, option E) and
`-S` code-generate one module. Size estimate for the P2-A part: an alias symbol kind in the object
writer and reader (tens of lines in A/D).

### 2.2 i128 division and FP<->i128 conversions inline

**Still true, and the fix is proven.** MSIL never calls `setMaxDivRemBitWidthSupported` or
`setMaxLargeFPConvertBitWidthSupported` (the constructor ends at `MSILISelLowering.cpp:387-389` with
`setMaxAtomicSizeInBitsSupported(0)`), so the defaults of 128 (`TargetLoweringBase.cpp:750-752`) leave
65-128-bit division and conversions to the type legalizer, which reports compiler-rt helpers as
Phase-3 errors. NVPTX (`NVPTXISelLowering.cpp:1158-1159`) and AMDGPU
(`AMDGPUISelLowering.cpp:617-618`) set both to 64. The expansion is `ExpandIRInsts`
(`llvm/lib/CodeGen/ExpandIRInsts.cpp`), added for every target by `TargetPassConfig::addISelPasses`
(`TargetPassConfig.cpp:943`) before `addIRPasses`; its hidden options `-expand-div-rem-bits` and
`-expand-fp-convert-bits` (lines 69-78, read at 1281-1288) override the target value, which is how the
fix was tested without a rebuild.

Measured (`backlog-files/i128/`, `results.txt` E2):

| Test | Result |
|---|---|
| `i128.c`: 2,000 iterations of udiv/urem/sdiv/srem with divisors of every width, `(i128)` and `(u128)` conversions to and from float and double; native = this tree's clang -O2 + VS `clang_rt.builtins` | identical output and exit 30 on .NET 8, .NET 10, Framework 4.8 at -O2 and -O0 (`x86_64-pc-windows-msvc-msil`), and on WSL .NET 10 (`x86_64-unknown-linux-gnu-msil`); `msil-verify.py` (D21) clean |
| without the options | 8 errors `runtime helper '__udivti3' ... (phase 3)` and so on |
| `fp-expand.ll` split inputs (`i128`, `i128-strict`, `i128-sat`) | `i128`: 8 errors -> 2 (`fptosi/fptoui_f16_i128` now name `__extendhfsf2`); `i128-sat`: 4 -> 0; `i128-strict`: 8 -> 8 (constrained intrinsics are not expanded) |
| `call-errors.ll` `libcall.ll`/`libcall-fp.ll`, `libcall-policy.ll` `helpers.ll` | every `__divti3`/`__udivti3`/`__modti3`/`__umodti3`/`__floattidf`/`__floatuntisf` error disappears; `lround`/`exp10` stay |
| `bitint.c`: `unsigned _BitInt(200)` division and conversion | already compiles and matches native **without** the options (widths above 128 are always expanded) |
| IL size (`i128.c` -O2) | 881 (udiv), 974 (urem), 958 (sdiv), 1,053 (srem), 582 (u128->double), 573 (i128->float), 311, 159 IL instructions per function |
| Speed (`bench.c`, 3,000,000 u128 divisions) | native compiler-rt ~300 ms; MSIL .NET 8 1,023 ms, .NET 10 968 ms, Framework 1,013 ms (whole process); C# `UInt128 /` on .NET 8: 428-482 ms in-process |

The Rust module's `div128` and `f2i128` (`llvm.fptosi.sat.i128.f64`) compile with the options too.

**Recommendation (M12):** two lines in the `MSILTargetLowering` constructor plus test updates:
`call-errors.ll` (LIBCALL and LIBCALLFP prefixes, lines 428-436, 469, 516-517), `fp-expand.ll`
(I128 and I128SAT, lines 1244-1252, 1495-1498; I128S unchanged), `libcall-policy.ll` (HELPER lines
266-267 and the `--implicit-check-not` list at 76-77), comments in `i128.ll`, `i128-div-pow2.ll`,
`int128-return.c`; one new execution test. The MSVC native oracle cannot link compiler-rt helpers
today, so the execution test either links the VS `clang_rt.builtins-x86_64.lib` (a `lit.cfg.py`
change, M12.f) or is a self-checking `.ll` with constant results. Optional refinement (~60 lines):
outline each operation kind into one internal function per module in
`MSILPrepareIR::doInitialization` (which runs before ExpandIRInsts' `runOnFunction`, legacy PM order),
so the 600-1,050-instruction expansion exists once per module instead of once per site; methods with
many sites otherwise grow toward RyuJIT's MinOpts size limit (*unverified* threshold).

**Design consequence (user's document):** Design.md 7.1's "128-bit integers" row and section 6's
"`Int128`/`UInt128` for i128 division where available", and plan P3-B's "i128 division ... through
Int128/UInt128 ... and through compiler-rt-style helpers otherwise", become: expanded inline by LLVM on
both runtimes, no switch needed; P3 may replace the expansion with a private helper (one copy per
assembly; compiling compiler-rt's own C would use 64-bit `div`) or with `Int128`/`UInt128` behind a
switch, for size and speed. Strict conversions and `f16` stay with P3's helpers. Proposed 7.1 row in
section 7.

**P2 interaction:** none for correctness. A later helper-based replacement interacts with how the
static helper archive reaches msil-ld and with COMDAT (an ELF or wasm container can deduplicate
`linkonce_odr` helpers across objects; metadata-only objects would carry one private copy each).

### 2.3 The empty `asm sideeffect` of Rust's `black_box`

**Still true, in IR and in C.** MSILPrepareIR rejects every inline-asm call
(`MSILPrepareIR.cpp:581-584`, "inline assembly is not supported for MSIL"). clang's
`emitMSILAsmStmt` (`clang/lib/CodeGen/CGStmt.cpp:2739-2762`) accepts only an empty GNU asm with no
outputs, inputs or labels (it becomes `fence syncscope("singlethread") seq_cst` +
`llvm.sideeffect`, D36); anything else is `err_msil_asm_unsupported`. Reproduced
(`backlog-files/blackbox/`):

- rustc: `call void asm sideeffect "", "r,~{memory}"(ptr nonnull %_3)` three times in `entry`;
- C idioms (Google Benchmark's `DoNotOptimize` and friends, `bb.c`): `asm volatile("" : : "r"(v) :
  "memory")`, `"r,m"(p)`, `"+r"(*v)` are three clang errors on the MSIL triple; natively they are
  `"r,~{memory},..."`, `"r|m,~{memory},..."` and the tied form `"=r,0,~{memory},..."`.

**Proposed lowering (M12).** In MSILPrepareIR, accept a call (not `callbr`) of an `InlineAsm` whose
string is empty after trimming, whose constraints (`InlineAsm::ParseConstraints`) are inputs of any
register/memory/immediate kind (`r`, `m`, `r|m`, `*m`, `g`, `i`, `n`, `X`), outputs only when tied to
an input (`=r,0`, clang's `+r`), and any clobbers. Replace it by the D36 compiler fence,
`call void (...) @llvm.fake.use(<input>)` per non-immediate input, `call void @llvm.sideeffect()`
(keeps otherwise empty loops), and replace each tied output by its input. `llvm.fake.use` already
works in the MSIL backend: `fakeuse.ll` compiles verifier-clean at -O0 (the value is pushed and
popped) and -O2 (stored to a reused local), and MSILPassConfig already disables
`RemoveLoadsIntoFakeUses` (`MSILTargetMachine.cpp:312`). The Rust module with this lowering runs (88)
on all three runtimes. For C, `emitMSILAsmStmt` keeps its translation for the operand-free case and
lets an empty asm with operands through as IR asm (the wrapped native TargetInfo validates the
constraints), which MSILPrepareIR then lowers; the optimizer keeps treating the asm as opaque, which is
the point of `black_box`.

Caveat: after LLVM, RyuJIT may still delete a computation whose only use is a dead local store
(*unverified*). If the gate shows that, lower each input instead to a `volatile` store into one
internal 8-byte "black hole" global (opaque to the JIT). Size: ~60 lines MSILPrepareIR, ~30 lines
CGStmt.cpp (the existing hook function, no new hook site), ~200 lines of tests
(`clang/test/CodeGen/MSIL/inline-asm.c` and `asm-barrier.c` change). Not affected by Phase 2.
Design.md section 3's inline-asm sentence (Status item 10) then reads "empty asm statements, with or
without operands".

### 2.4 swiftcc (Embedded Swift)

**Still true:** `callingConvSupported` (`MSILISelLoweringCall.cpp:101-106`) accepts C, Fast, Cold,
PreserveMost and PreserveAll; `swift.ll` gives `calling convention is not supported for MSIL` for
both the definition and the call. WebAssembly supports `swiftcc`/`swifttailcc` by adding dummy
`swiftself`/`swifterror` (and `swiftasync`) parameters so caller and callee signatures always match:
`WebAssemblyISelLowering.cpp:1278` (accepted conventions), `1381-1449` (LowerCall), `1643` on
(LowerFormalArguments) and `WebAssemblyMachineFunctionInfo.cpp:88-102` (signatures); wasm does not
`supportSwiftError()`, so `swifterror` goes through memory. MSIL already copied the swift exemption in
`MSILFixFunctionBitcasts.cpp:280-284`. A port is ~80 lines in three places (`callingConvSupported`,
LowerCall/LowerFormalArguments, `computeSignatureVTs`) and ~200 lines of tests. No Swift toolchain on
this host (a Windows Swift install is >1 GB). **Home: Phase 4 (other-language conformance) or drop
until someone asks.** P2: no interaction (signatures come from one function).

### 2.5 A C spelling for `msil-import-module` / `msil-import-name`

**Still true:** the backend reads the IR attributes in `MSIL::getPInvokeModule` /
`getPInvokeEntry` (`MSILUtilities.cpp:251-308`) and `MSILAsmPrinter.cpp:471`; clang has no MSIL
attribute (`clang/include/clang/Basic/Attr.td` has none; there is no `clang/lib/CodeGen/Targets/MSIL.cpp`,
MSIL reuses the native `TargetCodeGenInfo`). WebAssembly's `import_module`/`import_name`
(`Attr.td:2625-2637`, `TargetSpecificAttr<TargetWebAssembly>`, `TargetWebAssembly` is arch-based at
`Attr.td:494`) cannot apply because MSIL triples have x86_64/aarch64 arches. `TargetSpec` has an
`ObjectFormats` list, so `def TargetMSIL : TargetSpec { let ObjectFormats = ["MSIL"]; }` works
(D3 added `Triple::MSIL`).

Two spellings:
- **(a) share `import_module`/`import_name`** with WebAssembly through `ParseKind` (as the five
  `interrupt` attributes do, `Attr.td:1066-2308`): wasm-port sources (Emscripten-style code, the
  wasm CoreMark port) compile unchanged; costs a two-line edit of the WebAssembly records, i.e. a new
  upstream hook in `Attr.td` (not hooked today; `msil/utils/upstream-hooks.txt` lists
  `SemaDeclAttr.cpp`).
- **(b) new `msil_import_module`/`msil_import_name`** (`Clang<"...">` spellings): append-only to
  `Attr.td`/`AttrDocs.td`, no upstream record touched.

Either way: Sema handler (~30 lines, WebAssembly's lives in SemaWasm.cpp), mapping to the IR
attributes in CodeGenModule under `isOSBinFormatMSIL()` (~20 lines), tests (~150). `Attr.td`
regenerates `AttrList.inc`/`AttrClasses.inc`, included by most of clang, so this is a clang-wide
rebuild: **put it in P2's first (header-batch) milestone**, as plan section 7 already prescribes for
`Attr.td` edits; Design 4.7's `msil_fixed_arity` (P3) could be declared in the same edit.

**P2 changes the role, not the need.** Once msil-ld resolves undefined functions against native
export tables (D14's table goes away), the attribute becomes an override: a library chosen by name,
cross-compiles without the native library, and an entry name that differs from the C name. Its
representation depends on the object choice: wasm-style A has import-module/name fields on undefined
symbols; ELF D needs a side section or symbol attribute; metadata objects (B) write the ImplMap row
directly (the Phase-1 shape, `objfmt-container.md` table at line 375).

### 2.6 One frame object per byval call site

**Still true.** `MSILTargetLowering::LowerCall` creates a new stack object for every byval argument of
every call (`MSILISelLoweringCall.cpp:343-360`, `MFI.CreateStackObject(...)` then an inline memcpy).
`byval.c`: ten calls of `take(struct S)` (64-byte struct) give `ldc.i4 640` before `localloc` on
`x86_64-unknown-linux-gnu-msil`; native x86-64 reuses its outgoing-argument area. Windows and AArch64
triples pass such structs by reference (clang makes the copy), so only the SysV x86-64 triples
(linux-gnu, linux-musl) are affected. StackColoring ("Merge disjoint stack slots") runs only at -O1
and above, and LowerCall emits no lifetime markers, so nothing merges the copies.

Fix (~40 lines + ~80 test lines): keep a per-function pool of byval slots in `MSILFunctionInfo`; the
k-th byval argument of a call reuses pool slot k, enlarging it with `MFI.setObjectSize` /
`setObjectAlignment` (the copy is dead when the call returns: C ends a byval parameter's lifetime at
callee return). Works at -O0 too, unlike lifetime markers. **Home: M12** (cheap) or P4-A with the
frame-model revisit. Status.md 3 "Limits" line 178-179 goes away. Not affected by Phase 2.

---

## 3. M10 leftovers (open issue 11)

### 3.1 `va_arg` on an aggregate type

**Still crashes**, in MSIL and native x86-64 alike: `vaagg.ll` (`va_arg ptr %ap, %struct.P`) ->
`Unknown type!` / `UNREACHABLE executed at llvm/lib/CodeGen/ValueTypes.cpp:254`, llc exit 132
(0xC000001D), in `SelectionDAGBuilder::visitVAArg` before any MSIL hook. A scalar `va_arg` is already
clean (`the va_arg instruction is not supported for MSIL yet (phase 3 variadics)`,
`MSILISelLowering.cpp:540-542`). The plan row of 2026-10-17 (line 2109) defers the diagnostic to P3,
but the crash violates Phase 1's "never a crash" rule (Status.md 2) and the fix is ~15 lines: in
MSILPrepareIR, diagnose every `VAArgInst` with the existing message and delete the body, as for
variadic definitions. **Home: M12.** Upstream bug report (native llc crashes too): the user's call.

### 3.2 `msil-ci.yml` never run, and the setup-dotnet pin

**Still true, and blocked by more than the pin.** `.github/workflows/msil-ci.yml:114` and `:221` read
`uses: actions/setup-dotnet@v5 # TODO(user): pin SHA ...`; the trigger is `workflow_dispatch` only
(line 66). Read-only checks on 2026-10-21:

- `gh run list -R <owner>/llvm-msil --workflow msil-ci.yml` -> `HTTP 404: workflow msil-ci.yml not
  found on the default branch`. The default branch is `main` (`gh api repos/<owner>/llvm-msil`), the
  user's pure upstream mirror, and GitHub only dispatches workflows that exist on the default branch.
  The repository has 3 workflow runs in total, all dependabot jobs on `main`.
- `actions/setup-dotnet` tags: `v5` = `v5.4.0` (2026-06-26) =
  `26b0ec14cb23fa6904739307f278c14f94c95bf1`; `v6.0.0` (2026-07-16) =
  `a98b56852c35b8e3190ac28c8c2271da59106c68`. `check-action-pins.py` reports setup-dotnet as "not
  pinned upstream" without failing, so either pin passes the gate.
- Plan section 8 says "actions/setup-dotnet (tag v4.x, SHA to be pinned by the user)": stale (the
  workflow uses v5).

**Home: the user** (make `msil` the default branch or accept that CI cannot run; repository
settings; `disable-upstream-workflows.sh`), and the two-line pin in whichever P2 infra WP owns
`msil-ci.yml` (P2 adds msil-ld legs to it anyway). Proposed pin line:
`uses: actions/setup-dotnet@26b0ec14cb23fa6904739307f278c14f94c95bf1 # v5.4.0`.

### 3.3 No Linux-native oracle

**Still true that WSL has no C compiler** (`cc`, `gcc`, `clang`, `zig`, `tcc` absent; no
`/usr/include/stdio.h`, no `crt1.o`; `sudo -n` asks for a password), **but no sudo is needed.** WSL
has binutils 2.46 (`ld`) and `libc.so.6`/`libm.so.6`, and the fork's clang has the X86 backend.
Prototype (`backlog-files/linux-oracle/`): compile on Windows with
`clang --target=x86_64-unknown-linux-gnu -O2 -ffreestanding -fno-pic -c`, link in WSL with
`ld -dynamic-linker /lib64/ld-linux-x86-64.so.2 start.o prog.o libc.so.6 libm.so.6`, where `start.c`
is a naked `_start` (argc/argv from the stack, `call main`, `call exit`). `fib`, `crc32`,
`mandelbrot-fixed` and `kr-legacy` from `msil/test/M1/11-programs` produce exactly their `.expected`
transcripts. Limits: no `__libc_start_main` (no `envp`, no `init_array` of the program), fine for
the freestanding corpus; P3's glibc conformance runs (stdio through `printf`, `getenv`, ...) want a
real toolchain (`sudo apt install clang` or an unprivileged LLVM/zig tarball in the WSL home).

**Home:** a P2 test-infra WP (P2 adds multi-TU tests where LP64/LLP64 can differ): `update-expected.py`
and `lit.cfg.py`'s `msil-native-oracle` leg for `msil_runner=wsl` (~100 lines). Not the user.

### 3.4 `kr-legacy.c` and a C23 default

**Latent, still true.** `%{msil_cflags}` is `-ffreestanding -Werror=return-type -I<Inputs>`
(`msil/test/lit.cfg.py:1068`), and the C pipeline (`c_pipeline`, lines 1002-1023) passes it to both
the MSIL compile and the native oracle. HEAD's clang defaults to gnu17 (`__STDC_VERSION__ 201710L` on
both x86_64 MSIL triples; `clang/lib/Basic/LangStandards.cpp:110`). With `-std=c23`, `kr-legacy.c`
fails (`kr-legacy.c:82: unknown type name 'pp'` and four more errors). Fix: add `-std=gnu17` to
`%{msil_cflags}` (one line; it pins the whole corpus against an upstream default change, which an
upstream merge would otherwise surface as dozens of failures at once). **Home: M12.f** or the next WP
that owns `lit.cfg.py`.

### 3.5 `lit-legs.ps1` and arguments with blanks

**Still true.** `msil-lit.ps1:31` splits `-LitArgs` at whitespace; `lit-legs.ps1:129-131` therefore
throws `lit argument with a blank` (for example a NuGet ilasm path under a profile with a space).
lit's parser accepts response files (`llvm/utils/lit/lit/cl_arguments.py:79`,
`fromfile_prefix_chars="@"`), and `upstream-hooks.ps1:187-200` already passes inputs through
`build/lit-results/upstream-hooks.rsp`. Fix (~15 lines): write the `--param` pairs one per line into
an `.rsp` file and pass `@file`. **Home: P2 infra WP**, low priority (this host and the CI runners have
no such path).

### 3.6 Test counts in docs

**Already exact; drop the item.** At HEAD: `msil/test/M1/11-programs` 20 programs; `msil/test/M1`
93 `.c` + 42 `.ll` + 1 `.test` = 136; `llvm/test/CodeGen/MSIL` 104 `.ll` + 7 `.mir` = 111;
`clang/test/CodeGen/MSIL` 19 tests: all as Status.md 1 says. `msil/test/README.md` no longer contains
"more than 110" or "about 100" (only "20 complete programs", correct). Keep it as a standing rule for
every milestone that adds tests. The "36 upstream files" / "301 lit tests" of GettingStarted.md
(lines 337, 360) drift with upstream merges and stay a merge-workflow step (HANDOFF 0b).

### 3.7 Status.md's proposed Design.md updates

Reviewed in section 6. Note: Status.md section 6 lists **14** items (item 14, `long double`, was added
after HANDOFF and inputs.md said "13").

---

## 4. M11 leftovers

### 4.1 MIR read-back of FP infinity/NaN constants

**Still true, and the cause is an upstream regression in every target.** `inf.ll` at
`-stop-after=msil-isel` prints `CONST_F64 double +inf` / `CONST_F32 float +qnan`; reading it back
(`-run-pass=none`) fails with `expected a floating point literal` (`MIParser.cpp:2185-2195` accepts
only `FloatingPointLiteral` and `HexLiteral` tokens). wasm32 fails identically. So does **x86**
GlobalISel MIR: `%0:_(s64) = G_FCONSTANT double +inf` fails, while the hex form
`double 0x7FF0000000000000` parses and is printed back as `+inf` (`backlog-files/mir/gi.mir`). Root
cause: `MachineOperand::print` prints `MO_FPImmediate` through `ConstantFP::printAsOperand`
(`MachineOperand.cpp:963-964`); upstream `8d9299cd7af5` (2026-04-27, "[AsmParser] Revamp how
floating-point literals work in LLVM IR", `LLLexer.cpp:1275`/`1316` `HexFPSpecial
[-+](inf|qnan|s?nan(0x...))`) and `41c214f0b115` (2026-05-07, "[AsmWriter] Change the output syntax of
floating-point literals") changed the IR syntax, and `MILexer.cpp` was never updated (no `qnan` in its
history). Upstream fix: ~20 lines in `MILexer::lexFloatingPointLiteral`/`maybeLexNumericalLiteral` plus a
MIR test. **Home: the user** (an upstream report or patch under their account); the fork should not
add a hook for it. Drop from P2.

### 4.2 WebAssembly `-stop-after=wasm-isel` verifier defect

**Still true upstream, fixed for MSIL by M11.** `late.ll` (`select`): wasm32 `-stop-after=wasm-isel`
-> `*** Bad machine code: Using an undefined physical register ***` (exit 132); MSIL
`-stop-after=msil-isel` is clean. Cause and sweep (144 of 343 wasm tests) are in
`recon/m11/stop-after.md` sections 1 and 6. **Home: the user** (upstream report) / drop.

### 4.3 WSL unattended upgrades

**Still true:** `unattended-upgrades` is enabled and active, `APT::Periodic::Unattended-Upgrade "1"`,
no held packages; .NET 10 is the apt package `dotnet-runtime-10.0 10.0.13`, and lit finds the WSL
runtime as whatever `command -v dotnet` returns (`msil/test/lit.cfg.py:453-478`). Options:
(a) the user runs `sudo apt-mark hold dotnet-host-10.0 dotnet-hostfxr-10.0 dotnet-runtime-10.0
dotnet-sdk-10.0` or disables unattended-upgrades (a system setting: **the user**); (b) no system
change: the harness gets a `msil_wsl_dotnet=<path>` parameter and the gate uses a private runtime
installed with `dotnet-install.sh --install-dir ~/.dotnet-msil` (no sudo; ~20 lines in `lit.cfg.py`
plus a setup step). Recommendation: (b) in the P2 infra WP, and tell the user about (a).

---

## 5. Open questions for phase planning (inputs.md section 2)

| Question | Home | Phase 2 dependency and new evidence |
|---|---|---|
| What msil-ld binds against with no references; the option's spelling | **P2-B** (user decision) | Answered in `p2/bcl-binding.md` 0.6-0.7: a built-in table of `[mscorlib]`-safe members when no reference is given (keeps `clang foo.c -o foo.dll` running everywhere), `--reference=<file\|dir>`, no short form (`-r` is `--relocatable`). Needs Design 7.1 text (section 7, S8) |
| Default switch settings on Linux/Darwin triples; target features (`-mavx2`) vs SIMD switches | **P3-A** (switch framework), **P4** (SIMD) | Linux/Darwin triples never run on Framework, so their defaults could assume modern .NET, except that Design 7.2 says not to preclude Mono/Unity (old Mono lacks `UnmanagedCallersOnly`, *unverified*): a user decision for P3. D7 keeps native CPU/feature strings on the subtarget for this. P2-B must provide the channel that carries codegen options through LTO (module flags or TargetOptions; `p2/lld-flavor.md` notes `-msil-assembly-name` needs it) |
| How the static C helper archive reaches msil-ld | **P2-B** (mechanism), P3 (content) | Like compiler-rt: the driver passes an archive from the resource directory and msil-ld extracts members for undefined helper symbols. With item 10.2 in M12 nothing needs it before P3. COMDAT-capable objects dedupe helpers |
| atexit ordering across output assemblies; C `exit` through the host libc bypassing ProcessExit | **P3-A** (with P2-C for ordering) | **Measured** (`backlog-files/exit/`): P/Invoked native `exit(3)` runs native atexit callbacks but **not** `AppDomain.ProcessExit` on .NET 8 (Windows) and .NET 10 (WSL); Framework 4.8 ran ProcessExit and the native callback's output did not appear. `atexit` is not exported by `ucrtbase.dll` (it has `_crt_atexit`; `msvcrt.dll` does export `atexit`) or glibc `libc.so.6` (it has `__cxa_atexit`). So `exit`, `atexit`, `quick_exit`, `at_quick_exit` must be implemented by the toolchain (intercepted like setjmp, Design 4.6) once managed destructors or atexit exist (P3-A). Until then P2-B may bind `exit` to the native libc; P2-B's resolver must expect libc API names that are not exported symbols |
| Per-thread `[ThreadStatic]` buffers: first-use allocation, never freed | **P3-A** | none. A private holder class with a finalizer per thread is one way to free them (*unverified*, a P3 recon experiment) |
| setjmp/longjmp across assemblies: the exception carries jmp_buf and value | **P3-B** (with P2-C) | `System.Exception.Data` exists on Framework and modern .NET; no shared exception type is needed |
| libm to Math/MathF | **P3-A** switch; **user** decision later | none (D14 imports stay until then; Design 6 says undecided) |
| Architecture-specific intrinsics | scalar fallbacks **P3-A**; vectors and intrinsic classes **P4** | needs P2-B reference binding: in the .NET 8 reference pack `Sse42` is defined in `System.Runtime.Intrinsics.dll` (checked) |
| Object container for P2-A (netmodule, COFF, ELF, symbolic, LTO-only) | **P2-A** (user decision) | `p2/objfmt-container.md` recommends bitcode/LTO (E) for bring-up and ELF ET_REL (D) as the container. Backlog items it changes: aliases (2.1), the import attribute (2.5), helper dedupe (2.2) |

---

## 6. Status.md section 6: the 14 proposed Design.md updates

All 14 still hold at HEAD. Verdicts and exact texts follow. Because section 12 is the original
milestone specification, a single "As built (Phase 1)" note at its end (for items 1, 2, 3, 4, 8, 13)
is cleaner than rewriting the spec; the inline alternatives are given too.

**Proposed note appended to Design.md section 12 (covers items 1-4, 8, 13):**

> **As built (Phase 1, 2026-10-20).** Phase 1 met these criteria with the following differences, all
> recorded in ImplementationPlan.md and Status.md:
> - The commands are `ilasm -dll -pe64 -x64 prog.il` (`-pe64 -arm64` for aarch64 triples; plain
>   `ilasm -dll` writes a 32-bit AnyCPU image), and `dotnet prog.dll` needs a `prog.runtimeconfig.json`
>   next to the assembly, written by the user or the build system as section 8 says (or
>   `dotnet exec --runtimeconfig <file> prog.dll`; GettingStarted.md section 5). Without it the host
>   stops with exit status `0x80008083`.
> - Instead of one hard-coded P/Invoke there is a fixed import table (plan D14): `puts` and `putchar`
>   from the C library and the C math library, per triple (`libm.so.6` on glibc, because glibc keeps
>   libm separate and CoreCLR maps only the bare name `libc`; on Windows only the functions the C
>   runtime DLL exports under that name), plus any declaration with the `msil-import-module` IR
>   attribute. Every other undefined function is a compile error until `msil-ld` (5.2).
> - Each address-taken function has one `native int` address field that the module initializer sets
>   with `ldftn` once; code and relocations read it, because .NET Framework can give two `ldftn` of
>   one method different values (4.5).
> - The generated `Main` calls `main(void)` or `main(int, char **)`; `argv` is NULL-terminated and
>   built from `Environment.GetCommandLineArgs()`, so `argv[0]` is the assembly path under
>   `dotnet x.dll`; any other `main` signature is an error (plan D27).
> - Verification is the policy of plan D21 (approved 2026-10-12): MSILStackCheck, force-JIT of every
>   method, ILVerify with an allow-list and PEVerify on Windows, clean on the assembly and on a
>   neutralized ildasm/ilasm copy (Status.md section 4). `ilverify` alone cannot check stack shapes:
>   it stops at the first error of a method and cannot verify `calli`.

| # | Proposal | Re-checked | Verdict | Exact text for Design.md |
|---|---|---|---|---|
| 1 | Section 12: `dotnet prog.dll` needs a runtimeconfig | yes: `backlog-files/design12/prog.c`, Framework ilasm; `dotnet prog.dll` -> "The library 'hostpolicy.dll' required to execute the application was not found", `0x80008083`; with `--runtimeconfig` or `prog.runtimeconfig.json`: exit 42 | **accept** | the "As built" note; or inline, section 12 line 309: "`clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S prog.c -o prog.il`, then `ilasm -dll -pe64 -x64 prog.il`, then `dotnet prog.dll` with a `prog.runtimeconfig.json` written by the user or the build system (section 8), with a correct exit code and output." |
| 2 | Section 12: ilasm options | yes: `ilasm -dll prog.il` -> machine `0x014C`, magic `0x010B` (PE32) | **accept** (merged into 1) | as in 1. `clang/test/CodeGen/MSIL/design-milestone1.c:3` quotes `ilasm -dll prog.il` in a comment only; update the comment with Design.md |
| 3 | Section 12: verification criterion | D21 approved (plan section 10 item 1) | **accept** | "As built" note; or replace the third exit criterion by: "- Verification (plan D21): MSILStackCheck, force-JIT of every method, ILVerify with an allow-list and PEVerify on Windows are clean on the assembly and on a neutralized ildasm/ilasm copy (Status.md section 4)." |
| 4 | Section 12 "one hard-coded P/Invoke"; 5.2, 5.3 | yes: D14 table in `MSILUtilities.cpp:251-308`; exports re-checked (msvcrt has no `hypot`/`cbrt`/`round`; ucrtbase no `frexpf`/`ldexpf`/`hypotf`) | **accept, amended**: section 12 via the note; 5.2 needs no change (it describes msil-ld); 5.3's library-name bullet is self-contradictory ("`z`, not `libz.so.1`" and then "sonames ... taken from `DT_SONAME`") and Phase 1's `libm.so.6` follows the second half | 5.3 second bullet, proposed (final policy from the `native-imports` recon): "Library names come from the library that resolved the symbol: on ELF its `DT_SONAME` (`libz.so.1`, glibc's `libm.so.6`), because the unversioned `libz.so` exists only with development packages, except the C library, which is emitted as `libc` (CoreCLR maps that name to the platform C library); on Windows the DLL named by the import library (`kernel32`, `ucrtbase`). A bare name (`z`), which the runtime probes as `libz.so`, `z.dll` or `libz.dylib`, is used only when no library file was read." |
| 5 | Section 3: `__ELF__` | yes: native `x86_64-unknown-linux-gnu` defines it, every `-msil` triple does not | **accept** | Section 3, first "What this means" bullet: replace "predefines (`__x86_64__`, `__linux__`, `_WIN32`, ...) and target features are exactly the native ones by construction" by "predefines (`__x86_64__`, `__linux__`, `_WIN32`, ...) and target features are the native ones by construction, plus `__MSIL__` and minus `__ELF__` on Linux triples (clang defines `__ELF__` only for the ELF object format; revisit with glibc headers in phase 3, plan section 10 item 9)". Not affected by an ELF object container: `__ELF__` follows `Triple::getObjectFormat()`, which stays `MSIL` |
| 6 | Section 4.2: localloc alignment | yes: D13, `MSILFrameLowering::getLocallocAlign` (`MSILFrameLowering.cpp:65`) | **accept** | 4.2 second bullet: "Remaining static allocas: a single `localloc` at function entry, sized for the whole frame. A constant-size `localloc` of 32 bytes or less can be only 8-byte aligned (.NET Framework 4.8 for 8 to 32 bytes; .NET 8 and .NET 10 for 16 and 32 bytes in Tier-0 code and for 8 bytes in optimized code), so frames assume 8-byte alignment, and a frame holding an object aligned above 8 allocates `MaxAlign - 8` extra bytes and aligns its base up (plan D13). Frames are not zeroed: `.locals` is never `init`." |
| 7 | Section 10: main-thread stack | Status 5 table (1.5 MB `dotnet.exe`, 4 MB ilasm `.exe`, ~19,000 `-O0` frames) | **accept** | new risk 9: "**Stack depth.** `dotnet x.dll` on Windows runs `main` on the host's main thread, whose stack is about 1.5 MB (an ilasm-built `.exe` on .NET Framework: 4 MB; native Linux programs: 8 MB by default), and a CIL frame is larger than the native one, at `-O0` especially: about 19,000 `-O0`-shaped frames overflow it, and a .NET stack overflow ends the process. The generated `Main` could later run `main` on a thread with a configurable stack size." P2 note: msil-ld writes the PE, so a `--stack-size` for `.exe` outputs (SizeOfStackReserve, honoured by Framework's `.exe` launch) is a cheap P2-B option |
| 8 | Section 12 and 4.5: one `ldftn` field per function | yes: `MSILAsmPrinter.cpp:164-167`, `FuncFieldPrefix = "<msil>fn_"` | **accept** | the note; plus 4.5, new bullet after "Representation": "**Identity.** On .NET Framework, two `ldftn` of one method in different methods can give different values (precode versus compiled code), so every address-taken function has one `native int` address field that the module initializer sets once; code and relocations read the field, and it is the slot the thunk table above fills later (plan section 12, M9 row)." |
| 9 | Section 2.3: every 64-bit integer is `native int` | yes: D2 | **accept** | 2.3 first bullet, append: "On 64-bit triples every 64-bit integer is `native int` as well, and `int64` is never emitted (plan D2): PEVerify rejects mixing the two, and one CIL type per register class keeps the stack types exact." |
| 10 | Section 3: the inline-asm exception | yes: `CGStmt.cpp:2739-2762`, D36 | **accept** (amend wording if M12's `black_box` lands) | Section 3: "a thin wrapper adds `__MSIL__`, rejects inline assembly except an empty asm statement such as `asm volatile("" ::: "memory")`, which becomes a compiler-only fence (plan D36), and maps `long double` to `double`." After M12: "... except empty asm statements, with or without operands (`asm volatile("" : : "r"(x) : "memory")`, Rust's `black_box`), which become compiler-only barriers that keep their operands alive ..." |
| 11 | Section 3 table: Darwin later | yes: `unknown target triple` (Status 2) | **accept** | rows: "`aarch64-apple-darwin-msil` \| libSystem \| LP64 (phase 4)" and "`i686-pc-windows-msvc-msil` \| UCRT \| ILP32 (phase 5)" |
| 12 | Section 5.5: two ilasm limits | yes: `MSIL::MaxILNameBytes = 1023` (`MSILUtilities.h:48`); sNaN quieting per Status 3 | **accept**, noting that `-S` keeps the limits in phase 2 | 5.5 first bullet: "**Milestone 1:** `-S` emits ILAsm-compatible text, and `ilasm` produces the assembly. There are no objects or linker yet. Two ilasm limits apply to this path: identifiers of at most 1023 bytes (a longer C name is a compile error) and float32 signaling-NaN constants, which come out quiet. Phase 2's PE writer has neither; `-S` output keeps them." |
| 13 | Section 12: generated `Main` contract | yes: D27 | **accept** | the note; or section 12 bullet: "A generated `Main` that calls `main(void)` or `main(int, char **)` with a NULL-terminated `argv` from `Environment.GetCommandLineArgs()`; any other `main` signature is an error (plan D27)." |
| 14 | Section 3: `long double` | yes: native mantissa/size 64/16 on x86_64 linux-gnu, linux-musl and w64-windows-gnu, 113/16 on aarch64 linux-gnu, 53/8 on both windows-msvc and aarch64 w64-windows-gnu; every `-msil` triple 53/8 | **accept** | Section 3 bullet: "`long double` is `double` everywhere. On x86-64 Linux and MinGW (80-bit x87 format) and on AArch64 Linux (IEEE binary128) that differs from native, so `long double` interop on those triples is a documented limitation; on windows-msvc and AArch64 windows-gnu `long double` is natively `double`." |

Home of the whole set: **the user** (Design.md is the user's document; agents do not edit it). Items
4 (5.3) and 12 interact with P2-B and P2-A decisions; the others are independent of Phase 2.

---

## 7. Consistency of Design.md 5 and 7 and plan 7 (P2-A/B/C) with the 2026-10-14/15 decisions

The scope edits of commit `c035b99c6ca1` landed (Design.md 2.3, 5.2 step 3, 5.6, 6, 7.1, 8, 11; plan
D10, D16, sections 7 and 10). `-mdotnet`, MsilC.Runtime, NuGet packaging and "profile" appear only in
historical rows (checked with grep over Design.md, the plan, Status.md and GettingStarted.md). These
sentences still need updating (S1-S8 follow from the decisions; S9-S13 are other staleness found
while checking):

| # | Where | Current sentence | Problem | Replacement |
|---|---|---|---|---|
| S1 | Plan 7 P2-B | "msil-ld is given reference assemblies (-r) and binds every BCL type and member ..." | the 2026-10-14 row leaves the spelling to P2-B because lld's `-r` is `--relocatable` | "msil-ld is given reference assemblies (the option's spelling is decided in P2-B: lld's `-r` means `--relocatable`; recon `p2/bcl-binding.md` 6 proposes `--reference=<file\|directory>` with no short form) and binds every BCL type and member ..." |
| S2 | Plan 7 P2-B (new sentence after the binding sentence) | - | decision (b) and goal 1: nothing says what happens without references | "With no references, msil-ld binds the members that the default code generation uses to `[mscorlib]` 4.0.0.0, as Phase 1 does, from a built-in list of members that `mscorlib` provides on .NET Framework 4.8 and modern .NET alike (recon `p2/bcl-binding.md` 5.10); a member outside that list needs a reference. (Pending the P2-B decision.)" |
| S3 | Plan 7 P2-B (new sentence) | - | no runtime support assembly: helpers are "linked statically by msil-ld" (Design 6), but no P2 milestone owns the mechanism | "msil-ld also links the toolchain's static C helper archive the way lld links compiler-rt builtins (the driver passes it; members are pulled in for undefined helper symbols) and keeps the IL helpers that the backend emits private to the output; the archive's content comes with P3." |
| S4 | Plan 7 P2-B vs P2-C | P2-B: "resolves symbols to objects, managed assemblies and native libraries"; P2-C: "linking against managed assemblies built by this toolchain, and P/Invoke resolution from native export tables / import libraries (DT_SONAME, kernel32.lib style mapping)" | both milestones claim managed-assembly and native resolution | P2-B: "... resolves symbols to objects, archives and native libraries (P/Invoke resolution from native export tables and import libraries: `DT_SONAME`, kernel32.lib-style mapping) ..."; P2-C: "-shared libraries with a public export class (--export-class), the symbol table (names, kinds, signatures, the triple) in metadata, and linking against managed assemblies built by this toolchain." |
| S5 | Plan 7 P2-C | "the symbol-table custom attribute carrying names, kinds, signatures and the triple" | an attribute needs a type; under "no runtime support assembly" it cannot come from a shared toolchain assembly | "the symbol table carried by an attribute whose type is a BCL type (for example `System.Reflection.AssemblyMetadataAttribute`, which `p2/metadata-writer.md` already emits for the triple) or a type defined privately in the output assembly, never one from a support assembly (section 12, 2026-10-15), attached so that one assembly could later hold several triples (FeatureIdeas idea 7)" |
| S6 | Design 5.4, second bullet | "An assembly-level custom attribute carries the symbol table (name, kind, signature) and the triple." | same as S5; "assembly-level" with one triple would also preclude FeatureIdeas idea 7 | "A custom attribute carries the symbol table (name, kind, signature) and the triple. Its type is a BCL attribute such as `System.Reflection.AssemblyMetadataAttribute`, or one defined privately in the assembly, so no support assembly is needed (section 6)." (rest of the bullet unchanged) |
| S7 | Design 5.2 step 2.2 | "**Referenced managed assemblies** built by this toolchain: read their export table (5.4). A match becomes a `call` into that assembly's export class." | Roslyn-style references give msil-ld one kind of `.dll` input; nothing says how BCL references and toolchain-built libraries are told apart | append: "Any other reference assembly (the BCL, or another .NET library) carries no such table and is used only to bind BCL types and members (7.1)." |
| S8 | Design 7.1, "Assembly references" bullet, after "Phase 1 hard-codes `[mscorlib]`, which .NET's `mscorlib` facade makes run everywhere." | - | decision (b) leaves the no-reference case and `-S` output undefined; Roslyn on .NET has no default reference at all (CS0518), which would break goal 1 | insert: "Without references, and in `-S` output, which no linker sees, BCL members bind to `[mscorlib]` as in phase 1, limited to members that `mscorlib` provides on both runtimes, so `clang foo.c -o foo.dll` keeps running everywhere." (pending the P2-B decision; see S2) |
| S9 | Design 5.3, third bullet | "`[SuppressGCTransition]` is available as an opt-in attribute on modern .NET." | decision (c): newer attributes come only through individual switches plus capability from the references | "`[SuppressGCTransition]` (modern .NET) is emitted only under its own codegen switch and is bound from the references like any attribute (7.1)." |
| S10 | Design 5.3, second bullet | "Library names are emitted without prefixes or suffixes ... Versioned sonames ... are taken from the native library's `DT_SONAME` ..." | self-contradictory (Status item 4) | section 6, item 4 |
| S11 | Design 5.1, first bullet; 2.1 table row "Wasm object format ..."; plan 7 P2-A "a CIL object format modeled on the wasm object format" | as written | not a scope conflict, but pending the P2-A container decision (`p2/objfmt-container.md` recommends E then D) | after the decision, for D: "**CIL object files** (`clang -c` default): ELF relocatable objects whose sections hold CIL method bodies, signatures and data images (ECMA-335 encoded), with ELF's symbol table, relocations, COMDAT groups and the triple in a note; LLVM's archiver and object tools read them." Plan P2-A should also add clang `Attr.td` (section 2.5) to its batched header commit |
| S12 | Design 7.1 table, "128-bit integers" row; section 6 "`Int128`/`UInt128` for i128 division where available"; plan 7 P3-B "i128 division and f16 through Int128/UInt128 ..." | as written | changes if M12 lands the i128 expansion (section 2.2) | row: "\| 128-bit integers \| Expanded inline by LLVM (legalizer and ExpandIRInsts), division and floating-point conversions included; `Int128`/`UInt128` only behind a switch \| Expanded inline, the same code \|"; section 6: drop "and `Int128`/`UInt128` for i128 division"; P3-B: "f16 through System.Half where the codegen switches and references allow (P3-A) ..., with helpers for C complex and the remaining compiler-rt calls (strict FP<->i128 conversions, f16 and bfloat conversions, ...); i128 division and plain FP<->i128 conversions are expanded inline since M12, and P3 may replace that expansion by one private helper per assembly or `Int128`/`UInt128` behind a switch for size and speed." |
| S13 | Plan 8 | "actions/setup-dotnet (tag v4.x, SHA to be pinned by the user)" | stale: the workflow uses `@v5` | "actions/setup-dotnet (tag v5; v5 = v5.4.0 = `26b0ec14cb23fa6904739307f278c14f94c95bf1` on 2026-10-21, to be pinned with the user's approval)" |

Also found outside sections 5 and 7: Design 8 lists `clang/lib/CodeGen/Targets/MSIL.cpp` ("ABI lowering,
plus attaching C-level signatures ...") and `clang/lib/Driver/ToolChains/MSIL.{h,cpp}`. Neither exists;
Phase 1 reuses the native `TargetCodeGenInfo` and has only `clang/lib/Basic/Targets/MSIL.h`. The
driver files are P2-B work; the CodeGen file arrives only if 2.3's by-value P/Invoke signatures need it
(P2-C). Suggested: "(planned)" after both entries. Design 4.1's "Destructors and `atexit` handlers run
at process exit (`AppDomain.ProcessExit`)" is incomplete given section 5's measurement (a native
`exit()` skips ProcessExit on modern .NET): a P3-A item, noted here so the P3 plan picks it up.

---

## 8. What the Phase 2 object/linker choice changes

| Item | Changed by the choice? | How |
|---|---|---|
| GlobalAlias (2.1) | **yes** | A/D carry alias symbols (free weak-alias semantics); B/C/assembly-objects need forwarding stubs or a side table; P2-C exports need a method per exported name in every option; the one-module lowering (M12) is needed for E and `-S` in every option |
| Import attributes (2.5) | **yes** (representation) | wasm-style import fields (A), a side section or symbol attribute (D), the ImplMap row itself (B); in every option they become overrides of msil-ld's native resolution |
| i128 helpers (2.2) | indirectly | per-site expansion needs nothing; a later helper needs the static helper archive (S3) and is deduplicated by COMDAT in A/D, duplicated per object in B/C |
| byval frames (2.6), black_box (2.3), swiftcc (2.4), va_arg (3.1) | no | backend-only |
| Main-thread stack (Status 7) | slightly | msil-ld writes the PE, so a stack-reserve option for `.exe` becomes possible |
| ilasm limits (Status 12) | yes | gone for objects and msil-ld output; kept for `-S` |
| CI (3.2), oracle (3.3) | no | P2 adds legs; same scripts |

---

## 9. Recommended packaging

**M12 "Backlog fixes" (before P2-A; can run while the user decides the object format).** No
widely-included header; rebuild = MSIL target TUs (MSILPrepareIR, MSILISelLowering,
MSILISelLoweringCall, and the TUs including `MSILMachineFunctionInfo.h`) + `clang/lib/CodeGen/CGStmt.cpp`
+ relinks of llc, opt, clang, MSILTests (est. 15-30 min). Work packages with disjoint files:

| WP | Files | Content |
|---|---|---|
| M12.a (implement) | `llvm/lib/Target/MSIL/MSILPrepareIR.cpp` | alias lowering (2.1), empty-asm-with-operands lowering (2.3), `VAArgInst` diagnostic (3.1), optional i128 outlining (2.2) |
| M12.b (implement) | `MSILISelLowering.cpp`, `MSILISelLoweringCall.cpp`, `MSILMachineFunctionInfo.{h,cpp}` | `setMaxDivRemBitWidthSupported(64)`, `setMaxLargeFPConvertBitWidthSupported(64)`; byval slot pool (2.6) |
| M12.c (implement) | `clang/lib/CodeGen/CGStmt.cpp` (existing hook function) | empty GNU asm with operands passes through as IR asm |
| M12.d (test) | `llvm/test/CodeGen/MSIL/`: new `alias.ll`, `asm-operands.ll`, `i128-divrem.ll`, `va-arg-aggregate.ll`, `byval-slot-reuse.ll`; updated `call-errors.ll`, `fp-expand.ll`, `libcall-policy.ll`, `i128.ll` | llc/FileCheck + ilasm round trip |
| M12.e (test) | `msil/test/M1/` (an i128 division/conversion program, a Rust-shaped `.ll` with alias + black_box, a C alias program), `clang/test/CodeGen/MSIL/inline-asm.c`, `asm-barrier.c` | execution vs native oracle, both x86_64 triples |
| M12.f (infra) | `msil/test/lit.cfg.py`, `msil/utils/update-expected.py`, `msil/utils/ci/lit-legs.ps1`, optionally `.github/workflows/msil-ci.yml` | `-std=gnu17` pin; native oracle links `clang_rt.builtins` on Windows for i128 tests; rsp for lit params; (optional) WSL Linux oracle and private WSL runtime; setup-dotnet pin if the user approves |
| M12.g (docs) | `llvm/docs/MSIL/Status.md`, `GettingStarted.md`, `msil/test/README.md` | i128, aliases, asm operands, byval limit removed; counts |

Size: ~350 lines of code, ~900 of tests, ~100 of docs. Review focus: alias address identity and
weak/`main` aliases; the asm-constraint acceptance rule; that strict and f16 i128 conversions still
diagnose; byval slot reuse with two byval arguments in one call.

**P2 header milestone (first P2 milestone):** add the clang `Attr.td` edit for the import attributes
(2.5) to the batched header commit, so clang is rebuilt once.

**P2-B / P2-C / P2 infra:** S1-S8 text; the native resolver's exceptions (libc API functions that are
not exported symbols; `exit`/`atexit` to be intercepted from P3); Linux oracle and WSL runtime pin if
not in M12.f; the lit-legs rsp fix.

**Phase 3/4:** swiftcc (P4 or drop); exit/atexit interception and the ProcessExit gap (P3-A);
helpers replacing the i128 expansion if size or speed matter (P3).

**Needs the user:** the 14 Design.md texts (section 6) and S6-S12; CI default branch and enabling;
the SHA pin choice (v5.4.0 or v6.0.0); WSL package hold or unattended-upgrades; upstream reports
(MIR FP literals, wasm-isel verifier, `va_arg` aggregate crash); the import attribute's spelling.

**Drop:** test counts (exact today; standing rule).

---

## 10. FeatureIdeas: nothing recommended here precludes an idea

- Idea 1 (class placement) and idea 6 (enclosed instance model): alias forwarding methods and the
  asm lowering are ordinary methods and IR; i128 expansion is plain IL.
- Idea 5 (escape analysis for frames): byval slot reuse works on frame objects before layout and is
  compatible with any later frame model.
- Idea 7 (several triples in one assembly): an assembly-level symbol-table attribute holding one
  triple would preclude it; S5/S6 phrase it so the triple can be per export class (or repeated:
  `AssemblyMetadataAttribute` allows multiple instances).

---

## 11. Experiments index

All under `C:\llvm-msil-scratch\p2\backlog\`; sources in [`backlog-files/`](backlog-files/), outputs in
`backlog-files/results.txt`. `cmp.ps1` (in `backlog-files/tools/`) builds the native oracle (this
tree's clang + VS `clang_rt.builtins`), the MSIL assembly (Framework ilasm `-exe -pe64 -x64`) and runs
.NET 8, .NET 10 (`dotnet exec --runtimeconfig rc8.json/rc10.json`) and Framework.

| Id | What | Result |
|---|---|---|
| E1 | `alias-rust/alias.ll`, `calias.c`, rustc `merge.rs` -> `merge.ll` (rustc 1.99 `--crate-type staticlib -C panic=abort -C opt-level=2 -C codegen-units=1 --emit=llvm-ir`), retargeted | alias errors in all three; `lower_rust.py` lowering + i128 options -> runs 88 on .NET 8/10/Framework = native `native.rs`; D21 clean; `jmp` stub runs but is unverifiable |
| E2 | `i128/i128.c`, `bench.c`, `bitint.c`, `UInt128Bench.cs`; split `fp-expand.ll`, `call-errors.ll`, `libcall-policy.ll` | section 2.2 table |
| E3 | `blackbox/fakeuse.ll`, `bb.c`; `swift/swift.ll` | fake.use clean at -O0/-O2; C idioms 3 errors; swiftcc rejected |
| E4 | `byval/byval.c` | 640-byte frame for 10 calls (SysV), none on msvc/aarch64 |
| E5 | `vaarg/vaagg.ll`, `vascalar.ll` | aggregate: UNREACHABLE (MSIL and native x86-64); scalar: clean diagnostic |
| E6 | `linux-oracle/start.c`, `link-run.sh` | 4 corpus programs match `.expected` with WSL `ld`, no sudo |
| E7 | `mir/inf.ll`, `gi.mir`, `late.ll` | MIR FP literals fail on MSIL, wasm32 and x86 GlobalISel; wasm-isel verifier defect present, MSIL clean |
| E8 | `design12/prog.c` | `ilasm -dll` -> PE32 0x14C; `dotnet prog.dll` -> 0x80008083; with runtimeconfig -> 42 |
| E9 | `exit/Program.cs`, `exit.csproj`, `run-wsl.sh` | native `exit()` skips ProcessExit on .NET 8 and WSL .NET 10; Framework ran ProcessExit; `atexit` not exported by ucrtbase or glibc libc.so.6 |
| - | `tools/wslcheck.sh`, `wslcheck2.sh`; `llvm-readobj --coff-exports` on System32 `msvcrt.dll`/`ucrtbase.dll`; `gh api` (setup-dotnet tags, fork repo, runs) | sections 3.2, 3.3, 4.3, 5 |
