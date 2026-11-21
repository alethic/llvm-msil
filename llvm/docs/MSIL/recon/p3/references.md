# Phase 3 recon: real .NET references in C (`references`)

Status: the recon copy behind contract 5.21 and the R-a milestone of ImplementationPlan.md, added by R-a's plan commit;
it digests work of 2026-11-18 to 2026-11-20. Base: branch `msil` at `a15f3c958572`; every file and line cited below is
that commit's, and the lines move with later commits. Supporting files: [references-files/](references-files/), which
holds `zinit.il`, the hand-written IL of the runtime check in section 7.

What it digests, each with its verifiers' corrections applied in place: Claude's path plan for the first slice of real
references, R-a (section 1); the caret and keyword check (section 2); the K2 check (section 3); the value-types check,
with its questions VT-1 to VT-9 (section 4); the no-star check (section 5); the questions and answers of the user's
references walk of 2026-11-18/19 (section 6); the measurement `zinit.il` (section 7); and the file-and-line evidence
behind 5.21's citations (section 8). The plan and the checks are of 2026-11-19, and each had a verifier that re-checked
its facts in the tree at `a15f3c958572`. R-a's text cites the walk's questions as "references Q1" to "references Q5"
(section 6) and the value-type questions as "references VT-1" to "references VT-9" (section 4.10).

How to read the labels: "the user" marks only the user's own words and picks, quoted word for word, typing slips
included, with their date and time (CST, to the minute). Item prov-10 is the user's pick of 2026-11-04 on the review
page, "silent states only with my OK", which has a date and no time. Everything else is labelled as a check's reading, a
verifier's reading or Claude's (plan, proposal, recommendation, reading, decision or estimate); nothing a check or a
verifier says is decided. Sizes and times are estimates unless marked measured, most of them made for two CPUs; R-a's
plan text, approved on 2026-11-20 20:42 (section 6.14) with the user's change "The only change is you can use 4 CPUs.",
states its own for four (for R-a, 2.5 to 5 weeks at four CPUs, Claude's estimate).

Section numbers such as 3.3 or 6.10 are this copy's. 5.21 and its items (5.21.4 RS2), the plan's other sections (5.13,
5.20.3), section 12's rows and the decisions D8, D13 and D21 are ImplementationPlan.md's. The interop vision (sections 4
and 5) is an earlier design of far-future C/C++ and .NET interop, shown to the user from 2026-11-18 (section 6.2) and
not in the tree.

Contents:

0. Summary
1. The path plan: the first slice (R-a)
   - 1.1 The first slice
   - 1.2 The path plan's Sema rules and CodeGen
   - 1.3 Managed calls, the IR type and the back end
   - 1.4 The path plan's pass slot P15 and its backstops
   - 1.5 Measurements it proposed
   - 1.6 Dependencies and place
   - 1.7 Exception handling: EH-1's share
   - 1.8 Size
   - 1.9 Risks, and how each fails loudly
   - 1.10 What it asked of the user, and the answers
2. The caret and keyword check: how a reference is written
   - 2.1 The question and the spellings compared
   - 2.2 Which characters are free
   - 2.3 The caret today: parsed for Blocks, refused on MSIL
   - 2.4 Never Apple's block pointer
   - 2.5 Building the type: the pointer route or a type class of its own
   - 2.6 Fences, C++ and templates
   - 2.7 Blocks, Objective-C, tools and portability
   - 2.8 (K) a keyword, and (V) no mark
   - 2.9 Costs and upstream footprint (this check's estimates, from reading)
   - 2.10 Verdicts on the three spellings
   - 2.11 What followed
3. The K2 check: one clang type per .NET class
   - 3.1 Verdicts on K2
   - 3.2 The type of its own
   - 3.3 The K2 check's list of switches over type classes
   - 3.4 Sizeless, not scalar (5.21 RC3)
   - 3.5 The rules in C
   - 3.6 Jumps past a declaration (5.21 RS28)
   - 3.7 NULL
   - 3.8 CodeGen, the IR type and the ABI
   - 3.9 Spelling, C++, Objective-C and tools
   - 3.10 Costs and the upstream footprint
   - 3.11 Open choices, and the verifier's review
4. The value-types check: .NET structs and the questions VT-1 to VT-9
   - 4.1 The user's question, and the two versions checked
   - 4.2 Is DateTime blittable?
   - 4.3 The three forms of version 2, repaired
   - 4.4 Form B: a boxed value held as a reference (kind 3; R-a's "value-type form B")
   - 4.5 Form A: structs in C memory (kind 2)
   - 4.6 Form C: IL values (kind 1)
   - 4.7 Conversions between the forms
   - 4.8 Timing, order and costs
   - 4.9 Alternatives, fit and verdicts
   - 4.10 The questions VT-1 to VT-9
5. The no-star check: tickets and MC-1's star-spelled handles
   - 5.1 What was checked, and the outcome
   - 5.2 Tickets by value through MC-1
   - 5.3 MC-1's handles spelled with a star: the counts
   - 5.4 The counts R-a's text uses
   - 5.5 `intptr_t` as the untyped form, and the boxed exception
   - 5.6 When the star-spelled handles go
   - 5.7 Silent states
   - 5.8 Typing, ergonomics, fit and costs, in brief
6. The references walk: questions and answers
   - 6.1 Questions and answers at a glance
   - 6.2 The user's directions and questions, 2026-11-18
   - 6.3 The user's lean on strings, 2026-11-19 00:23-00:25
   - 6.4 Question 1 as first asked, and the directions it drew (2026-11-19 00:29-01:26)
   - 6.5 Q1: when real references come (answered "Okay.")
   - 6.6 Q2: how a real reference is written (first asking)
   - 6.7 Q2, asked again: K2 with a type form, or the caret (answered "Caret")
   - 6.8 Q3: one CLI_TYPE, two names (withdrawn)
   - 6.9 Q4: strings, following the user's lean (answered "Confirm")
   - 6.10 Q5: silent states that need the user's OK (answered "OK all six")
   - 6.11 Approval 1: the upstream footprint (answered "(a) Yes")
   - 6.12 Approval 2: Design.md's texts (answered "Approve")
   - 6.13 Approval 3: C only first, and where C++ comes (answered "(a) Yes, R-a2")
   - 6.14 What the walk changes later, and later picks that refine it
7. Measurements: zinit.il
   - 7.1 What the file tests
   - 7.2 How it was run
   - 7.3 Results
   - 7.4 What it does not cover
   - 7.5 How R-a relies on it
8. File-and-line evidence for 5.21's citations
   - 8.1 clang: parser and Sema
   - 8.2 clang: AST, serialization, mangling and libclang
   - 8.3 clang: CodeGen, target ABIs and ClangIR
   - 8.4 clang: driver, frontend, headers and diagnostics
   - 8.5 LLVM core
   - 8.6 LLVM's CLI headers and the MSIL target
   - 8.7 msil-ld (lld/MSIL)
   - 8.8 Harness files (R-a.i's Reads)
   - 8.9 Documents, tools and counts
   - 8.10 Corrections to the earlier evidence list

## 0. Summary

Outcomes, each with the section that records it:

- **When.** R-a, the first slice of real IL references (C only), comes next, before the cleanup round's milestones, EH-0
  with FX-1 to FX-3, and P-2: Claude's reading of the user's "Okay." (2026-11-19 02:33) to references Q1, whose
  recommended option was (A) (section 6.5). By the user's "(a) Stop and ask me" (2026-11-19 15:48), if R-a has not
  committed 4 weeks after its launch, the user is asked whether EH-0 and FX-1 to FX-3 go first (section 6.14).
- **Spelling.** A reference is written `T ^r`, `T` a `CLI_TYPE` struct: the user's "Caret" (2026-11-19 12:31), against
  Claude's recommendation of K2 with a type form (section 6.7; the checks of sections 2 and 3). It is built as a clang
  type of its own, which fails closed (Claude's reading of the option as framed), and the user approved its upstream
  footprint, about 40 to 45 more upstream files for C, with "(a) Yes" (2026-11-19 12:39; section 6.11).
- **Where references live.** Only where .NET tracks them, never in C memory, so they need no GCHandle; in R-a, local
  variables, the parameters and results of `static` functions whose address is never taken, and managed calls,
  everything else being a loud error (sections 1.1 and 6.5). Design.md's lines 27 and 125 take the texts the user
  approved with "Approve" (2026-11-19 12:49; section 6.12).
- **C first.** R-a is C only; C++ follows as R-a2, directly before P-2: "(a) Yes, R-a2" (2026-11-19 12:50) and "(b)
  Before P-2" (2026-11-20 11:50) (sections 6.13 and 6.14).
- **Strings.** No automatic conversion either way; C converts by calling the .NET API it chooses: the user's lean of
  2026-11-19 00:23-00:25, confirmed with "Confirm" (2026-11-19 12:34) (sections 6.3 and 6.9). MC-1's rules S1-S3 go in
  R-a2: "(a) Before P-2" (2026-11-20 12:04; section 6.14).
- **Silent states.** Six, OK'd with "OK all six" (2026-11-19 12:37) under prov-10 (section 6.10); one more with "(a) OK
  as undefined" (2026-11-20 17:04) (sections 5.7 and 6.14); and S1-S3's silent cases until R-a2, OK'd, as presented,
  with "(a) Before P-2" (2026-11-20 12:04; section 6.14). A `goto` or `switch` past a reference's declaration is
  refused, a loud rule (Claude's decision, section 3.6).
- **Tickets, star handles and value types.** Tickets stay raw GCHandle numbers, after the user's direction "That's just
  a raw GCHandle integer." (2026-11-19 01:26), held in an `intptr_t` (Claude's reading; sections 5.1 and 6.4). MC-1's
  star-spelled handles become `intptr_t` or `uintptr_t` in R-a2 by the user's "(b) Retire before P-2" (2026-11-20
  14:24), except boxed parameters and results (section 5.6). Value types are not in R-a; as R-a's governance row in
  section 12 records it (Claude's), the questions VT-1 to VT-9 come to the user after R-a's plan commit, VT-3 being
  settled by "Caret" (Claude's reading) and VT-6 re-cut first (section 4.10).
- **Measured.** `zinit.il` read every unwritten object local as null on .NET Framework 4.8 and .NET 8, 10 and 11, tiered
  compilation on and off, and ILVerify and PEVerify reported only the allowed InitLocals (section 7.3).

## 1. The path plan: the first slice (R-a)

Claude's plan of 2026-11-19 for the shortest way to real IL references in C (R-a's text counts it among the checks: the
path check), with its verifier's corrections applied (marked **Corrected**). The plan decided nothing; its numbers are
Claude's guesses and its recommendations Claude's. The verifier (its reading; nothing it says is decided) checked the
plan's 52 facts at `a15f3c958572`: 47 confirmed, 5 corrected, none refuted. Its verdict: "right direction and mostly
accurate", with six things to fix; real references need nothing that is still unbuilt, and R-a can come first. The
subsections follow the plan's own order: the first slice (1.1), the work (1.2 to 1.4), then the measurements, place,
exception-handling share, size and risks, and the questions it asked (1.5 to 1.10). The plan predates the choice of
spelling and writes a reference `String s`; its spelling-free parts read the same for R-a's `T ^` (5.21.1 RA1, 5.21.3).

It answers the user's question of 2026-11-18 20:50 (section 6.2), "It can be straight IL with managed references,
right?", and the end goal the user set (2026-11-19 01:26): "What matters is when we start actually using real referenes,
without GC handle."

**The model.** One new kind of value, a .NET reference, held only where .NET tracks it: IL locals, parameters, results
and the IL stack. It never goes into C memory, so it needs no GCHandle and no release, and it cannot leak. In IL it is
`object`; classes appear only inside stubs. Tickets stay as MC-1 has them, raw GCHandle numbers (`intptr_t` in the plan;
the user, 2026-11-19 01:26: "That's just a raw GCHandle integer."), made and used through GCHandle's own members with
MC-1's `gch` rule, only to keep an object in C memory.

### 1.1 The first slice

Included, C only:
- **The annotation** `CLI_TYPE("Ns.T")` (the attribute `cli_type`) on structs standing for .NET reference types:
  classes, interfaces, `string`, `object` and arrays, named in MC-1's TYPE grammar (5.20.2) and parsed by MC-1's parser
  (`llvm/include/llvm/CLI/ManagedSignature.h`).
- **Where references live:** local variables, at every optimization level; the IL stack; parameters and results of
  `static` functions whose address is never taken; managed calls, whose reference arguments, results and `this` pass
  straight through with one `castclass` each.
- **Strings:** a string result comes back as a reference; bytes and strings convert through ordinary managed calls.
  **Corrected:** the plan named UTF8Encoding, GetString, GetBytes and Marshal.Copy as built-in rows
  (`llvm/include/llvm/CLI/BCLMembers.def:168-175`), but the table has only `Encoding.GetString(uint8*, int32)` and the
  throwing UTF-8 and UTF-32 constructors; `GetBytes`, `GetByteCount` and `Marshal.Copy`, like the StringBuilder and
  Environment members of the plan's example, bind only with `--reference`.
- **Operations:** `NULL`; `==` and `!=` as identity (IL `ceq`); `!r`, `if (r)` and `?:`; implicit conversion to
  `System.Object`; explicit casts between classes, checked with `castclass`. A reference local without an initializer
  starts as NULL.

Left out, each a loud "not supported for MSIL yet" error with a test: C memory (struct and union fields, arrays and
VLAs, globals, static locals, `thread_local`, the heap and any pointer to a reference, `&r`, compound literals,
`_Atomic`, `volatile`, `sizeof`, asm operands, `cleanup`); casts to or from integers and other pointers, `<`, `>` and
arithmetic; external-linkage functions with references in their signature and `-shared` exports; the address of such a
function, function-pointer types with references and unprototyped functions; callbacks (P-3's delegates); C++ (R-a2);
varargs; functions that call setjmp (EH-2 decides); value types, boxed values and generics; .NET static fields as C
globals; exact classes in IL; `ldstr` literals, because a CIL object cannot carry user strings
(`MSILObjectWriter.cpp:352-360`; the verifier's note: only CIL objects refuse them, while whole-program output can carry
them and msil-ld maps them, `lld/MSIL/Synthetic.cpp:582-589`). No exception handler exists before EH-1. MC-1's handles
and its automatic C-string conversions do not change.

Later steps, in the plan's names: **R-a2**, C++ free functions, needed before P-2 can use references; **R-b**, external
functions, calls between files, exports, callbacks with references (after P-3), function pointers on the portable
triples, globals as .NET static fields, exact classes in IL; **R-c**, references across exception handlers and setjmp,
with EH-1 and EH-2.

### 1.2 The path plan's Sema rules and CodeGen

The rules (marked in the plan as mostly the same for every spelling):
- **Allowed:** locals, and parameters and results of `static` functions; assignment; `==`, `!=`, `!r`, `if (r)`, `&&`,
  `||`, `?:` and `NULL`; implicit conversion to the `System.Object` reference; explicit casts between CLI classes.
- **Refused (loud):** every kind of storage (fields, arrays, globals, static locals, `thread_local`, compound literals,
  `_Atomic` and `volatile`); `&r`; a pointer to a `CLI_TYPE` struct, which would claim to point at a reference and which
  MC-1 takes as a handle today (`clang/lib/Sema/SemaMSILManagedCall.cpp:133-146`); casts to integers or other pointers,
  `<`/`>`, and arithmetic; varargs, unprototyped functions, asm operands and `cleanup`; references in the signature of a
  function with external linkage, or of a function whose address is taken; C++, in R-a.
- **Files:** `clang/lib/Sema/SemaExpr.cpp`, `SemaCast.cpp` and `SemaType.cpp` (not hooked); `SemaDecl.cpp` and
  `SemaChecking.cpp` (hooked). Which of the rules clang already enforces depends on the spelling.
- **Put to the user as rules:** `==` is identity, a reference starts as NULL, `volatile` is refused (a `volatile` slot
  is one the -O0 promotion misses, risk 2), casts are checked.
- **CodeGen:** `ConvertType` maps the reference to the IR type (`clang/lib/CodeGen/CodeGenTypes.cpp`, not hooked;
  WebAssembly's precedent at :577-584); LLVM forbids `icmp` on it, so `==`, null tests, truth values and casts in
  `CGExprScalar.cpp` (not hooked) become calls of helper declarations and cast stubs; a reference is passed directly,
  never as an integer and never dropped.

Corrections and later findings:
- **Corrected: NULL at the start must come from clang.** The plan had clang store it (`CGDecl.cpp`, not hooked) "or the
  backend leaves that job to ldnull". The verifier: only clang keeps the promise. After SROA an unset reference is
  `undef` or `poison`, and LLVM folds it away. In
  `static String pick(String a, int c) { String s; if (c) s = a; return s; }` InstSimplify replaces the phi of `a` and
  `undef` with `a`, because an argument dominates every block, so the function returns `a` when `c` is 0, while -O0
  returns NULL: silent wrong code. `ldnull` on `IMPLICIT_DEF` stays only as the GC safety net. In R-a a
  zero-initialization step in Sema makes CodeGen store NULL at the declaration at every level (5.21.4 RS2, where Sema is
  Claude's choice; 5.21.5 RG3), and the back end's `ldnull` is a partial net (5.21.7 RB7). The K2 check's verifier later
  found a second way past the store, a `goto` or `switch` that jumps over the declaration; R-a refuses such jumps
  (5.21.4 RS28; section 3.6).
- **Corrected: the helpers cannot be internal declarations.** The plan called them "internal `__msil_ref_*` helper
  declarations". The verifier: a declaration must have external or extern_weak linkage, so they would be external
  declarations with reserved `__msil_` names, like MC-1's `__msil_mc.*`, and P1, LTO, T-M17's consistency check and the
  rule that they never reach an object's symbol table then need handling and tests. R-a has one such helper,
  `__msil_ref.eq` (5.21.6 RI6); casts call cast stubs (5.21.9 RM8).
- **A managed-call rule the user was not asked about:** the `CLI_TYPE` class must equal the signature's class or be
  `System.Object`. The verifier: under it, a FileStream passed to a Stream parameter needs an explicit cast (a castclass
  stub), and an Object passed to any class parameter compiles and is checked only at run time. Asked later, the user
  picked exact classes, "I think A is correct." (2026-11-20 11:44; 5.21.9 RM2), implicit conversion only to Object, "(a)
  Only to Object" (2026-11-20 10:45; 5.21.4 RS4), and checked casts, "Keep checked casts" (2026-11-20 10:19; 5.21.4
  RS8).
- **Corrected: a sizeless type gets no pointer or `&` fences free.** The plan expected a keyword type built on a
  sizeless type, like WebAssembly's `__externref_t`, to get the storage fences of C and C++ free. The verifier: pointers
  to sizeless types are legal, and WebAssembly refuses them with checks of its own
  (`clang/lib/Sema/SemaType.cpp:1879-1892`, `clang/lib/Sema/SemaExpr.cpp:15509`, both WebAssembly-only). R-a's type is
  sizeless (5.21.3 RC3, from the K2 check, section 3.4) and refuses pointers and `&` by rules of its own (5.21.4 RS10,
  RS12, RS13).
- **Corrected: the spelling reaches clang's CodeGen.** The plan said only clang's front half depends on the spelling;
  the verifier: say "only clang". Records are aggregates (`clang/lib/CodeGen/CodeGenFunction.cpp:279-287`), so only the
  no-star form needs a change there, builtin and block-pointer types being scalars already; debug info (CGDebugInfo) and
  the mangling for C's `overloadable` attribute differ too. The verifier also said that x86-64 SysV classifies an
  unknown builtin type as Memory (`clang/lib/CodeGen/Targets/X86.cpp:2007-2043`, then `getIndirectResult` at
  :2970-2978), so a keyword type would pass through C memory unless hooked. The K2 check's verifier corrected that
  (section 3.8): the MEMORY class sends a non-aggregate to `getIndirectResult`, which returns Direct (:2423-2442), so
  R-a needs no ABI hook (5.21.5 RG6), and a per-triple ABI test guards it (5.21.14 RV2).

### 1.3 Managed calls, the IR type and the back end

- **Managed calls:** new conversion codes `ref` and `this-ref` beside MC-1's
  (`llvm/include/llvm/CLI/ManagedSignature.h:1226-1242`), chosen in the classifiers of the fork-only
  `SemaMSILManagedCall.cpp` (:270-337) and written into `msil-managed-conv` by `CGMSILManagedCalls.cpp` (5.20.4 MR1). A
  C cast to another class calls a small cast stub, a new member kind `castclass` on MC-1's machinery, so the IR never
  names a class. In the stubs (`MSILManagedCalls.cpp`) a `ref` argument is `ldarg; castclass <class>` (none for
  `object`), replacing the handle path's null test, FromIntPtr, box, unbox and get_Target (:807-834, :1095-1097); a
  `ref` result is returned as it is, with no Alloc or ToIntPtr (:835-844, :1217-1232); `this-ref` is
  `ldarg.0; castclass <declaring type>`. msil-ld accepts the codes in its managed-call checks
  (`lld/MSIL/ManagedCalls.cpp:1899-1934`, :1967-2001) and in LTO's MR4 check, and `object` names no type for it
  (`lld/MSIL/SymbolTable.cpp:876-899`).
- **IR type** `target("msil.ref")`, registered in `getTargetTypeInfo` (`llvm/lib/IR/Type.cpp`, not hooked; beside
  WebAssembly's entries, :1104-1110) with HasZeroInit and CanBeLocal but not CanBeGlobal, so the IR verifier refuses
  reference globals; IR already forbids casts, `ptrtoint`, `inttoptr` and `icmp` on it. Its name maps to the shared tag
  `externref` (`ValueTypes.td:355`) in `MVT::getVT` (`llvm/lib/CodeGen/ValueTypes.cpp`, not hooked). **Corrected:** an
  unknown name ends in `llvm_unreachable` only when `HandleUnknown` is false; with it true, `getVT` returns `MVT::Other`
  (:325-327, :292-294); and :274 maps `MVT::externref` back to `wasm.externref`, the root of risk 13. SelectionDAG turns
  the tag's null into one node (`SelectionDAGBuilder.cpp:2046-2054`), selected as `ldnull`; the verifier's note: that
  node is `INTRINSIC_WO_CHAIN` with `Intrinsic::wasm_ref_null_extern`, so under L1 MSIL's selection matches a
  WebAssembly intrinsic id. Options: L0 (reuse `wasm.externref`, no core file), L1 (two files, Claude's recommendation,
  which R-a takes: 5.21.6 RI1, RI2), L2 (a tag of MSIL's own, four files). No intrinsic, and no address space: the host
  data layouts stay native, which keeps stock-clang LTO working (`llvm/docs/MSIL/Design.md:91`).
- **Back end:** today five value kinds, I32, NativeInt, I64, F32 and F64 (`MSILMCTypeUtilities.h:33-40`), and four
  register classes (`MSILRegisterInfo.td:58-62`). The plan adds `ValType::Object`, a register class `OBJ`, ARGUMENT,
  LOCAL, COPY, DROP, CONST_NULL (`ldnull`), SELECT, `ceq` and the branches on objects, and deliberately no
  `ldind.ref`/`stind.ref` patterns, so a memory access of a reference cannot be selected. In ExplicitLocals
  (`MSILExplicitLocals.cpp:89-155`) an undefined object value gets `ldnull` instead of being erased (:278-282). Frames
  do not change (`MSILFrameLowering.cpp:42-45`). `ValType::Object` is `ELEMENT_TYPE_OBJECT` in signatures and locals
  (`MSILObjectWriter.cpp:71-86`, :679-683; `MSILAsmPrinter.cpp:2105-2109`), which the CLI library already treats as a
  primitive (`llvm/lib/CLI/Signature.cpp:60`, :948-949); the text streamer keeps refusing class and value-type locals
  (`MSILTargetStreamer.cpp:197-212`), and every R-a shape prints in `-S` text.

### 1.4 The path plan's pass slot P15 and its backstops

The plan:
- **A required pass, `MSILPromoteReferences`,** in a new slot right after P1, promotes every reference stack slot with
  LLVM's `PromoteMemToReg`, at every optimization level, before the setjmp, variadic and (later) EH passes.
- **A check before instruction selection** refuses any reference slot, load or store that is left.
- **The slots are fixed** (5.13.6; confirmed: `addPassesToHandleExceptions` stays empty and `addISelPrepare` keeps only
  AtomicExpand), so the slot needs plan rows and a `pipeline.ll` update. The verifier's note: P10 and P11 are reserved
  and not in the code yet, and P12 runs only at -O1 and above (`MSILTargetMachine.cpp:285-353`).
- **Promotion is enough for R-a:** there are no exception pads until EH-1 (D8 sets the exception model to None, and
  slots P3/P4 lower invokes and delete the pads, `MSILTargetMachine.cpp:355-368`); functions that call setjmp are
  fenced; the C rules refuse every address.
- **MSILPrepareIR (P1)** lets `msil.ref` through; today it refuses every target extension type, in bodies and in globals
  (`MSILPrepareIR.cpp:142-148`, :165-167, :265-269, :2373-2385). It keeps backstops: external-linkage definitions,
  functions whose address is taken, variadic calls, and setjmp callers.
- **MSILFixFunctionBitcasts (P5):** a mismatch involving a reference must report an error, not convert. **Corrected:**
  the plan said its `convertValue` handles only integers and pointers (:507-527). It also converts any pair it can
  bit-cast, by its bits (:525-526); a target extension type cannot be bit-cast, so for a reference it does return null.
  In whole-program mode, though, `createWrapper` turns a type mismatch into a `_bitcast_invalid` wrapper whose body is
  `unreachable` (:370-377): a trap at run time with no compile-time error. "Report an error, not convert" must cover
  that path, not only the msil-lto table's `convertValue`.
- **setjmp** is fenced: M27's wrappers would carry the new kind (they copy IR types, `MSILLowerSjLj.cpp:617-633`; their
  result local is typed at `MSILAsmPrinter.cpp:2111-2117`), but EH-2 replaces them.
- **Four fences (risk 1):** Sema's refusals; the IR verifier (no CanBeGlobal, no casts or `icmp`); the check before
  ISel; no `ldind.ref`/`stind.ref` patterns, so the worst case is an internal error. A slot the promotion misses (a
  `volatile` reference, a `memcpy` from `-ftrivial-auto-var-init=pattern`, a new clang shape) is reported by the check
  (risk 2).
- **The verifier's addition:** an `il-snapshot --compare` step in R-a's gate, because the new slot must change no IL in
  programs without references.

R-a's text (5.21.7 RB10-RB12; Claude's design from this plan):
- **P15** is added by `MSILPassConfig::addISelPrepare` after AtomicExpand and before
  `TargetPassConfig::addISelPrepare()`, so after CodeGenPrepare and `addPassesToHandleExceptions` (code generation runs
  addIRPasses, CodeGenPrepare, addPassesToHandleExceptions, addISelPrepare, then ISel:
  `llvm/lib/CodeGen/TargetPassConfig.cpp:935-957`), where EH-1's preparation will run. Not right after P1, as the plan
  had it: EH preparation can demote reference PHIs into new slots after P1 (with a scoped non-funclet personality, as
  WebAssembly's, WinEHPrepare demotes only catchswitch PHIs, into stack slots:
  `llvm/lib/CodeGen/WinEHPrepare.cpp:186-190`, :923-965). WebAssembly's RefTypeMem2Local runs in the same hook
  (`WebAssemblyRefTypeMem2Local.cpp:66-90`; `WebAssemblyTargetMachine.cpp:298`).
- P15 runs at every level, in LTO and under `optnone`, and promotes only the static allocas of exactly the type that
  `isAllocaPromotable` accepts, and no other (LLVM issue 81575). An alloca or access left over is T-RB4; a slot an EH
  pad uses is T-RB8. Functions without reference slots are untouched (il-snapshot in R-a's gate, 5.21.14 RV29). ISel's
  LOAD and STORE report T-RB6 behind it (5.21.7 RB2).
- The plan's four P1 backstops are rules 3, 4, 6 and 8 of RB11's twelve (`MSILReferences.cpp`, called from hooks in
  `MSILPrepareIR.cpp`): not static (also when ThinLTO or `--lto-partitions` made the function external), address taken,
  variadic, and `returns_twice` callers. RB12 makes P5's mismatches T-RB5 on the `_bitcast_invalid` path in both modes
  and in the msil-lto table.

### 1.5 Measurements it proposed

The plan proposed (Claude's proposal) half a day of hand-written IL in a scratch folder: (1) whether object locals are
zeroed in methods without `init` (`llvm/docs/MSIL/Design.md:139`: `.locals` is never `init`): (a) read before written
after a callee dirtied the stack, (b) the same in the compiler's `localloc` frame shape, (c) held unwritten while a
callee runs `GC.Collect()`, many times, and (d) with a filter clause, on .NET Framework 4.8, .NET 8 and .NET 10 (tiered
compilation on and off) and WSL .NET 10; (2) ILVerify and PEVerify on the new shapes (D21: any new message is a defect
or a question for the user), expecting only the allow-list of `msil/utils/ilverify-allowed.txt` (Unverifiable,
InitLocals, StackByRef, UnmanagedPointer); (3) Framework 4.8 and both ilasms; (4) llvm-cil-dump and the C# canonical
listing printing `object` alike; (5) optionally, a string call's cost through handles against references. The nets
either way: R-a's IL never reads an object local before writing it, and methods with object locals would get
`localsinit` if (c) failed.

Measured on 2026-11-19 (section 7): `zinit.il`, which has the shapes of (a) and (c), read every unwritten object local
as null on .NET Framework 4.8 and .NET 8, 10 and 11, tiered compilation on and off, and ILVerify and PEVerify reported
only InitLocals, which D21's allow-list has. R-a's tests decide (b), (d) and the other new shapes (5.21.11;
section 7.4).

### 1.6 Dependencies and place

- **Needs** MC-1 (committed, a82fde953427), the two runtime checks (1.5) and the user's decisions; nothing else. **Does
  not need** EH-0, EH-1 or EH-2 (no pads before EH-1; setjmp callers fenced); P-2 (a client); M33 and M34 (every
  reference is `object` in IL); the cleanup round (no technical link); or M28: R-a refuses the address of any function
  with references in its signature, and on host triples a function pointer is a native-callable entry
  (`llvm/docs/MSIL/Design.md:156-158`), which cannot take an object, so the refusal stays there after M28; the portable
  triples can lift it in R-b. **Corrected:** Design.md:156 states M28's design (today an indirect call is a managed
  `calli`), and the fail-fast at :161 is narrower than the plan said: it covers calls through a pointer to another
  assembly's function, which take the native path, when the signature passes a value MSIL cannot pass to native code.
- **Depends on R-a:** P-2, if it is to use references (its OS layer, `DotnetFile`, is C++, 5.19.9, so it needs R-a2
  too); EH-1, whose typed catch and C SEH exception accessor can return a reference instead of a ticket; later, P-3's
  delegates with reference parameters (R-b), the fuller managed calls, inline IL operands, C++ and Qt.
- **Places**, in the order then (MC-1, EH-0, P-2, EH-1, EH-2, M28, M29, M30, P-3, M31, EH-3, M32, M33, M34, M35, P-4,
  with the cleanup round after MC-1): (A) before the cleanup round's milestones: real references about 3-5 weeks after
  the go, while the round (about 5-9 weeks if all twelve of its milestones run then, 3.5-5.5 if eight do), EH-0, the
  three fix steps (FX-1 to FX-3) and P-2 each move back by R-a's 2-4 weeks, and the four silent wrong-code states that
  EH-0 and the fix steps remove last that much longer; (B) right after the round, before EH-0 and P-2: 5-9 weeks later
  than (A), in the new layout; (C) after P-2, before EH-1: P-2 written on tickets and reworked later; (D) after Phase 3,
  where the earlier designs put it.
- **Claude's recommendation: (A)**, adding R-a2 before P-2 if the spelling makes C++ cheap. Weak points: the round the
  user asked for waits, EH-0's silent states last longer, R-a lands in the old layout. Claude's reading of
  FeatureIdeas.md item 4 (managed memory and refs, recorded from the user, to be evaluated at the very end;
  FeatureIdeas.md's opening paragraph and items 4 and 5): it puts C's own data on the GC heap, while a reference that
  never enters C memory needs neither managed C memory nor escape analysis, so item 4 is not this work.

The verifier's order review (its reading; nothing decided):
- The dependencies are right, and (A) is the earliest real place; it fits the user's words ("We dont care about anything
  except getting to that").
- **Corrected:** the plan's heading "If EH-1 runs first (places B, C or D)" was wrong. (B) and (C) also put R-a before
  EH-1, which comes after P-2 whichever of the round and EH-0 runs first; only (D) comes after EH-1. So (B) and (C)
  carry the same small EH-1 share as (A), and R-a needs no re-homing under them.
- **Missing options:** (A'), EH-0 and the three fix steps first, then R-a, then the round, following Claude's own
  recommendation to run EH-0 and the fix steps before the round (EH-0 is one or two work packages, so it could fill the
  time while answers were pending); and an early start of the spelling-free half (L0 or L1, the back end, the -O0 pass,
  stubs, msil-ld), tested from hand-written IR while clang waits; L0 needs no upstream OK.
- **Costs of (A) the plan left out:** the user's OK for the four silent wrong-code states lasting about 3-5 weeks
  longer, as the user's pick on the review page (2026-11-04, item prov-10) requires ("silent states only with my OK"),
  to be listed as a decision; re-pin rows for M28-M35, as MC-1 needed; an amendment to 5.13.5's sentence "No Phase 3
  milestone edits `Attr.td`, an IR intrinsic, ...". **A gain it left out:** the cleanup round's respelled managed-call
  declarations may take their signature from the C prototype, which can give object types only through `CLI_TYPE` types,
  so with R-a first they can use them from the start.

### 1.7 Exception handling: EH-1's share

If R-a runs first, EH-1 must at least stay loud: R-a's check refuses any reference slot that is left, so a reference
live into a handler fails loudly, and EH-1 adds a clear message and tests (small); or EH-1 re-homes such references as
IL locals (medium). EH-1 must also refuse references in SEH filters and `__finally` blocks, and may bind caught
exceptions as references. EH-2, which rebuilds setjmp without wrappers, lifts R-a's setjmp fence or keeps it; M28 keeps
refusing native-callable addresses of functions with references on host triples. Only under (D) would R-a itself re-home
the catchswitch PHIs that WinEHPrepare's WebAssembly mode moves into stack slots in C memory
(`WinEHPrepare.cpp:186-190`, :923-965), by WebAssembly's design (its commits 82f92e35c646 and d4cdb516eee4: frame
objects with a local stack ID), since promotion alone would re-create the demoted PHIs (LLVM issue 81575), and refuse
references in outlined `__except` filters and `__finally` blocks, which reach the parent's locals through one
escape-block slot in C memory.

**Corrected** (the verifier): "a reference live into a handler fails loudly" is too broad. Only references merged at a
catchswitch get stack slots (`WinEHPrepare.cpp:939-960`); the others stay IL locals and work. But the usual SEH shape, a
reference set inside `__try` and read in `__except`, creates such a merge, so a "loud refusal only" choice would refuse
ordinary SEH code once EH-1 lands, and the user should see that before picking. Section 12's EH input row records it:
EH-1 keeps such a reference loud (T-RB8; the usual `__try`/`__except` shape is then refused) or re-homes such slots as
IL locals, refuses references in outlined filters and `__finally` blocks, and brings the choice to the user; EH-2 lifts
or keeps the setjmp fence and says which.

### 1.8 Size

Claude's estimates; nothing measured.

| Piece | Lines, with tests | Time at two CPUs |
|---|---|---|
| The checks (scratch IL, not committed) | 150-400 | half a day |
| R-a's plan text: contract, milestone, rows | 800-1,500 | 1-3 days, with a check |
| clang, the part that is the same for every spelling | 1,000-2,000 | |
| clang, the spelling's part (C) | `String s`: 500-1,000, over 12-14 new shared files; keyword: 400-1,200, more with a type node; caret: 400-1,500 | |
| LLVM core | L1: about 10 lines in 2 files; L2: 30-60 in 4 | |
| Backend, including the -O0 pass | 1,500-2,500 | |
| Stubs, metadata writer, text | 200-400 | |
| msil-ld | 100-300 | |
| Tests (clang, llc, lld, execution, verification) | 5,000-12,000 | |
| **R-a in total** | **about 8,000-18,000** | **about 2-4 weeks** |
| R-a2 (C++ free functions) | keyword: 500-1,500; `String s`: 2,000-4,000 (18-22 clang files in all) | days to 2 weeks |
| EH-1's share | loud refusal: 50-200; re-homing: 600-1,500 | |

Yardsticks: MC-1, +40,524/-341 lines in 185 files (`git show --shortstat a82fde953427`), launched 2026-11-04, committed
2026-11-16; WebAssembly's reference-type commits 890146b19206 (47 files, +522/-17), 82f92e35c646 (13 files, +249/-7),
d4cdb516eee4 (5 files, +152) and 765f0d14fe14 (64 files, +679/-405); a full test run at two workers, 343 minutes; M24 to
MC-1 needed 4 to 20 gate rounds.

**Corrected** (the verifier, who found the sizes plausible): the stated total is below the sum of the table's own rows
(about 9,000-20,000 with the plan text) and leaves out the docs; the 2-week low end looks optimistic at two CPUs, since
MC-1 took 12 days on the whole machine and an edit to `Attr.td` or clang's type headers rebuilds hundreds of objects
(MC-1's estimate for the `Attr.td` class: about 520 clang objects, 3,500-4,000 CPU-seconds). About 27,600 of MC-1's
insertions are in test paths (the plan said about 26,000), and MC-1's last fix and commit-prep days ran on the new host
after the machine move of 2026-11-14, not "on the old host with all its CPUs". Plan work the table left out: the 5.13.5
amendment and the re-pin rows (above; the rows cover the CLITests floors of M28-M35, the hook contexts they cite in the
MSIL target and `lld/MSIL`, and the 5.13.6 table that M28 and M31 extend), the il-snapshot step, and Design.md:139 with
D13 if the `localsinit` fallback were needed. The keyword's clang cost was understated: WebAssembly's first commit for a
sizeless builtin reference type (890146b19206) changed about 30 clang files outside the tests, most not hooked here; the
plan's "at least 5 shared clang files" fits none of the three spellings.

### 1.9 Risks, and how each fails loudly

1. A reference reaches C memory through a missed rule: the four fences above, with a test for each rule.
2. The -O0 promotion misses a slot: the check before ISel reports it.
3. The JIT does not zero object locals never written: check 1 decides (measured above for plain locals); the fallback is
   `localsinit` on such methods; the GC stress test stays in the gate.
4. A new ILVerify or PEVerify message: the D21 gate fails, and the message goes to the user.
5. Optimizer passes meet the new type: IR forbids casts and compares, and SROA and GVN refuse to change it; MSIL has no
   vector registers (D8), so the SLP vectorizer, which crashed on WebAssembly's older form, builds none; anything else
   fails at ISel; tests at -O1 to -O3 and with LTO.
6. clang misses a CodeGen path: an assertion in an assertions build. The one silent case, `String s` on the portable
   triples, where an incomplete struct is an empty record that the ABI drops (`Targets/MSIL.cpp:64-90`), is guarded by
   an ABI test per triple and an assertion that a reference parameter is never dropped.
7. The wrong class at run time: `castclass` throws InvalidCastException.
8. EH-1 meets references: a demoted reference fails R-a's check, and references in SEH helpers are refused, until
   re-homing exists (with the correction above).
9. The pass contract: the new slot and check change 5.13.6; its rows and `pipeline.ll` pin it, and plan-check catches
   drift.
10. Upstream merges: the 2-4 core edits and the clang hooks get `upstream-hooks.txt` blocks with upstream lit tests, and
    the hooks check fails on drift.
11. Tools mishandle `object`: the leg of `msil-verify.py`'s pass 2 or of an oracle fails, and the tool gets fixed.
12. LTO consistency of the new codes and helper declarations: MR4's check (T-M17) is extended to cover them.
13. L1's shared tag: generic code may rebuild the IR type from the tag as `wasm.externref`; tests at every level cover
    it, and L2 removes the risk.
14. The spelling: `String s` has no clang precedent for a struct used as a scalar, and `String b = a;` reads like a
    struct copy; the caret needs a new type node or a hybrid; the keyword, a new keyword.
15. Schedule: R-a waits on the user's decisions, and the round and EH-0 wait on R-a.

### 1.10 What it asked of the user, and the answers

The plan listed as the user's to decide: R-a's place; the spelling; upstream files not hooked today (Design.md:316,
"Keep the diff to new directories plus registration hooks": design text, the verifier notes, not the user's words), with
L0, L1 or L2; new wording for Design.md:27 (the non-goal "GC references in C code") and :125 ("The C program never
touches the GC heap"); FeatureIdeas item 4; C only or C++ too; new verifier messages and `localsinit`; the four rules
above; EH-1's share; the cleanup round's open choices (the verifier: "answer", not "re-answer"); and, separately, MC-1's
pointer-spelled handles, where the plan's "rule that a type must tell the truth" is Claude's reading of the user's words
"it's storing data, which is lied about by the type" (2026-11-19 01:20), as the verifier noted.

The answers are recorded in section 6, and 5.21 and section 12 carry them. The place is references Q1, which offered
(A), (A'), (B), (C) and (D) with (A) as Claude's recommendation, answered "Okay." (2026-11-19 02:33; Claude's reading:
(A), R-a next; section 6.5), and bounded by the user's pick "(a) Stop and ask me" (2026-11-19 15:48; section 6.14). Item
6 of references Q5, "OK all six" (2026-11-19 12:37; section 6.10), is the silent-state OK the verifier asked for, within
that bound. The spelling is "Caret" (2026-11-19 12:31; section 6.7); the upstream files, the Design.md texts and C first
are approvals 1 to 3 (sections 6.11 to 6.13); R-a2's place and MC-1's star-spelled handles came with later picks
(section 6.14); the class rules are in 1.2.

## 2. The caret and keyword check: how a reference is written

The caret check, as R-a's text calls it: a read-only check by an agent on 2026-11-19 at HEAD `a15f3c958572`, with one
adversarial verifier; nothing was built, compiled or edited. It asked whether C can get a mark of its own for a .NET
reference, compared a caret, a keyword and no mark, and listed the free characters. The verifier re-checked its 52 facts
in the tree and against six public pages (50 confirmed, 2 corrected) and listed what it missed; its corrections are
applied in place and marked **Corrected**. Labels are kept: "this check's reading", "an adversarial verifier's reading",
"Claude's reading", "Claude's recommendation". The check measured two file counts with git (118 files by `git grep`, 55
by `git show --stat`); its other file counts are estimates from reading. Outcome: the user later picked "Caret"
(2026-11-19 12:31), the option of the caret built as a type of its own; R-a writes a reference `T ^r` and never builds
it as Apple's block pointer (2.11).

### 2.1 The question and the spellings compared

The check followed the user's objection to `*` for something that is not a pointer (2026-11-19 01:18, rephrased at
01:20; section 6.4) and a question, not a pick (01:22): "Is there any way to introduce a new magic character or type
modifier in C?" In every spelling the class is a `CLI_TYPE` struct with no body, and `*` keeps its C meaning, a real
pointer into C memory.

| Spelling | Written | How it is built |
|---|---|---|
| (C) caret | `String ^s = System_String_Concat(a, b);` | clang already parses `^` in every declarator, for Apple's Blocks: Sema and a new type only |
| (K) keyword | `__cli_ref(String) s = System_String_Concat(a, b);` | a type specifier built like `_Atomic(int)`: (C)'s type plus a keyword |
| (V) no mark | `String s = System_String_Concat(a, b);` | the annotated struct is the reference, as in C#: clang must treat a struct as one scalar value |
| attribute | `String CLI_REF s`, a macro over a type attribute | no new token, but another compiler ignores the attribute |

The check's framing, corrected by the verifier:
- **Tickets.** The check assumed typed tickets (a one-field struct, `String_h`, with `cli_hold`, `cli_get` and
  `cli_release` helpers) and offered "tickets only" and "no hurry". It started at 01:22; the user's directions of 01:26
  (not picks; section 6.4), "That's just a raw GCHandle integer." among them, put those parts out of date, and they are
  not carried here: tickets are raw GCHandle integers, which MC-1 already accepts as `intptr_t` handles
  (`clang/lib/Sema/SemaMSILManagedCall.cpp:133-146`), and class structs serve only the reference spelling.
- **Wording and the mark.** "A .NET object that it holds only for a moment" misdescribes references (a local can live
  for the whole function; "in local variables, parameters and return values" is accurate). With integer tickets a class
  struct has no use except as a reference, so under (C) and (K) the mark is required, not chosen: it buys truthfulness.
  Only for .NET value types would it choose: a boxed `Point ^` against a copy, `Point`. **Corrected:** the check's fit
  part read the 01:22 question as asking for such a mark; it was a question, not a request or a pick.
- **Scope.** The spelling touches only clang's front end; the IR type, the MSIL backend, the metadata writer and msil-ld
  are the same under all three. Timing was a separate question, not "after Phase 3" (references Q1, answered "Okay." on
  2026-11-19 02:33, which Claude read as its option (A): next, before the cleanup round's milestones; section 6.5).

### 2.2 Which characters are free

A declarator accepts `*` and `^` in every C and C++ mode; C++ adds `&` and `&&`, and OpenCL 2.0 or later the keyword
`pipe` (`isPtrOperatorToken`, `clang/lib/Parse/ParseDecl.cpp:6529-6553`). So `^` is the only character besides `*` that
clang already reads in a C declarator and, in this check's reading, the only new mark that costs nothing in the lexer
and parser; `%`, `@` and `$` collide with meanings they already have, and the backtick is free but unprecedented.
- **`^`:** an ordinary punctuator; `^=` is its own token, and `^^` one token only in C++26 reflection mode
  (`clang/lib/Lex/Lexer.cpp:4464-4476`; `clang/include/clang/Basic/TokenKinds.def:240-242`;
  `clang/include/clang/Basic/LangOptions.def:539`). Its declarator use is in 2.3; it is also C++/CLI's and C++/CX's
  handle (2.6).
- **`%`:** modulo and the digraphs `%>` (`}`), `%:` (`#`) and `%:%:` (`##`), on in C99 and C++ (Lexer.cpp:4321-4355);
  not a declarator token (`String %s` is a syntax error today); C++/CLI's tracking reference, a GC-aware `&`. Parser
  work, and in `vector<String%>` the `%>` lexes as `}`.
- **`@`:** a token only in Objective-C (`@interface`, `@"text"`; the parser's `tok::at` case,
  `clang/lib/Parse/ParseExpr.cpp:1548-1553`), elsewhere an unknown character (Lexer.cpp:4546-4571): lexer and parser
  work, and a clash if Objective-C ever met MSIL.
- **`$`:** a letter in identifiers by default, off only for assembler preprocessing (Lexer.cpp:4146-4156;
  LangOptions.def:79; `clang/include/clang/Options/Options.td:2422-2423`; `-pedantic` warns,
  `clang/include/clang/Basic/DiagnosticLexKinds.td:79-80`): `String$` is one name.
- **The backtick:** no lexer case, so an unknown token. **Corrected:** it lands in the ASCII default case at
  Lexer.cpp:4593-4596 (the check's :4588-4592 end the backslash and UCN case); Lexer.cpp:2400 takes `$`, `@` and the
  backtick only as raw-string prefixes. C++26 adds the three to the basic character set and C23 makes them single bytes
  in literals, neither with grammar (cppreference); the tree's UCN rules single them out
  (`clang/lib/Lex/LiteralSupport.cpp:733-735`). No precedent as a type mark.
- **Taken:** `&` (C++'s reference, and P-2 is C++), `~`, `!`, `?`, `:`, `.`, `#`, `\` and `'`. **Keywords instead:**
  `_BitInt` (TokenKinds.def:324), `_Atomic` (:341), `__ptrauth` (:357), `__ob_wrap` and `__ob_trap` (:360-361),
  `_Nonnull` (:618), `__funcref` (:624), `__ptr32` (:628): (K)'s route (2.8).

### 2.3 The caret today: parsed for Blocks, refused on MSIL

- **Parser.** `isPtrOperatorToken` returns true for `*` and `^` first (ParseDecl.cpp:6531).
  `Parser::ParseDeclaratorInternal` consumes `^` like `*`, reads the qualifiers after it and records a
  `DeclaratorChunk::BlockPointer` (:6669-6703); `Parser::MightBeDeclarator` (:2027) and C++ tentative parsing
  (`TryParsePtrOperatorSeq`, `clang/lib/Parse/ParseTentative.cpp:832`) accept it too. So `String ^s`,
  `String ^f(String ^a)`, `(String ^)o`, `typedef String ^StringRef;` and C++'s `std::function<void(String^)>` parse
  today. The Blocks specification calls this "function pointer style notation substituting `^` for `*`"
  (`clang/docs/BlockLanguageSpec.md:46-47`; **corrected:** the check quoted it without "function").
- **Sema on MSIL: two errors.** `GetFullTypeForDeclarator`'s `DeclaratorChunk::BlockPointer` case
  (`clang/lib/Sema/SemaType.cpp:4780-4797`) gives `err_blocks_disable`, "blocks support disabled - compile with -fblocks
  ...", unless `-fblocks` is on (:4782-4783; `clang/include/clang/Basic/DiagnosticSemaKinds.td:11326-11328`). With no
  early return, `Sema::BuildBlockPointerType` then adds `err_nonfunction_block_type`, "block pointer to non-function
  type is invalid" (SemaType.cpp:2871-2877, the diagnostic at :2875; DiagnosticSemaKinds.td:7545-7546). Its test is
  `isFunctionType()`, with no exception for dependent types, so `T ^p` in a C++ template is refused too. The first
  error's advice, compile with `-fblocks`, is wrong here.
- **When Blocks are on.** Apple's toolchains (`clang/lib/Driver/ToolChains/Darwin.h:258-262`); the GNU Objective-C
  runtime with the non-fragile ABI (`Clang::ConstructJob`, `clang/lib/Driver/ToolChains/Clang.cpp:7602-7606`); OpenCL
  2.0 (`clang/lib/Frontend/CompilerInvocation.cpp:4316-4317`). Otherwise they are off
  (`clang/include/clang/Driver/ToolChain.h:481-482`): the MSIL toolchain never overrides `IsBlocksDefault` (no match in
  `clang/lib/Driver/ToolChains/MSIL.cpp` or `MSIL.h`). The verifier adds that only Darwin.h overrides `IsBlocksDefault`,
  and Darwin MSIL triples are refused (ToolChains/MSIL.cpp:434-441).
- **Expressions.** Binary `^` is XOR; a unary `^` always starts a block literal, refused without `-fblocks`
  (ParseExpr.cpp:1554-1556; `Sema::ActOnBlockStmtExpr`, `clang/lib/Sema/SemaExpr.cpp:17385-17389`); C needs no
  expression form, because objects come from .NET constructors. C++26 reflection took `^^`; with Blocks on, clang splits
  it into two carets in a binary expression (ParseExpr.cpp:336-347). Nothing clashes with a declarator caret.

### 2.4 Never Apple's block pointer

The check called building `String ^` as `BlockPointerType` a fatal sub-idea, and the verifier confirmed it with one
correction:
- Its contract is a function pointee (`clang/include/clang/AST/TypeBase.h:3650-3652`;
  `clang/include/clang/Basic/TypeNodes.td:49`).
- In C a block pointer converts to `void *` with no cast (SemaExpr.cpp:10161-10171), and a `void *` to a block pointer
  with no cast (:10202-10207), both in `Sema::CheckAssignmentConstraints`: two silent escapes into C memory.
  **Corrected:** the check said an integer converts to a block pointer too. It does not: Sema classifies it
  `IntToBlockPointer` (:10192-10194), and `Sema::DiagnoseAssignmentResult` reports `err_int_to_block_pointer`
  (SemaExpr.cpp:18082-18085; DiagnosticSemaKinds.td:9456). Both silent escapes go through `void *`, in both directions,
  and the fatal sub-idea stands.
- CodeGen makes it a plain LLVM pointer (`CodeGenTypes::ConvertType`'s `Type::BlockPointer` case,
  `clang/lib/CodeGen/CodeGenTypes.cpp:741-751`), which a GC reference can never be.
- Debug info describes Apple's block literal, `__block_descriptor` and `FlagAppleBlock` (`CGDebugInfo::CreateType` for a
  `BlockPointerType`, `clang/lib/CodeGen/CGDebugInfo.cpp:1610-1644`).
- Itanium mangles it as a block, `U13block_pointer` and the pointee
  (`CXXNameMangler::mangleType(const BlockPointerType *)`, `clang/lib/AST/ItaniumMangle.cpp:4589-4592`).
- The analyzer treats block-pointer arguments as callbacks (`isCallback`,
  `clang/lib/StaticAnalyzer/Core/CallEvent.cpp:78-98`); the fork's build turns the analyzer off
  (`msil/cmake/caches/MSIL.cmake:35`). Sema treats block pointers as scalars with a scalar kind of their own and
  switches on it (TypeBase.h:9178-9190, :2836-2847; `clang/lib/Sema/Sema.cpp:889`; SemaExpr.cpp:7930-8107).
- About 118 files under `clang/` and `clang-tools-extra/` name block pointer types (this check's `git grep` count,
  without the bytecode interpreter's unrelated `Block`), each needing an exception. The verifier: a type-name pattern
  gives 110 files (107 without tests and docs), plain `BlockPointer` 140 (128 without tests, docs and unit tests), so
  "about 118" sits inside the range.

So only the parser's `^` is reused. The type also needs a marker of its own: an earlier draft that spelled the reference
`String *` could read the pointee's attribute alone, because the star was the reference, but the caret must tell
`String ^` from `String *`.

### 2.5 Building the type: the pointer route or a type class of its own

**(C-i) WebAssembly's `__funcref` route:** a plain pointer whose pointee sits in a new language address space (the .NET
heap), lowered to an IR target extension type, as `__funcref` is (`HandleWebAssemblyFuncrefAttr`,
SemaType.cpp:7409-7426; `QualType::isWebAssemblyFuncrefType`, `clang/lib/AST/Type.cpp:3178-3182`;
`clang/include/clang/Basic/AddressSpaces.h:80`; CodeGenTypes.cpp:641-642; the IR type `wasm.funcref`,
`llvm/lib/IR/Type.cpp:330-332`, the verifier's addition).
- **Free:** `==`, `!=`, `!`, `if (s)`, NULL, `?:`, parameters and results; `_Nonnull` and `_Nullable`
  (`Type::canHaveNullability`, Type.cpp:5340-5355); the "used before set" warning
  (`clang/lib/Analysis/UninitializedValues.cpp:58-66`); C++'s built-in operators; and an error for any implicit
  conversion to `void *` or another pointer, because the address spaces differ (`checkPointerTypesForAssignment`,
  SemaExpr.cpp:9610-9613, and :18020-18030).
- **No edit to the existing target maps:** a new language address space maps to target address space 0 unless listed
  (AddressSpaces.h:92-104; `clang/lib/Basic/Targets/X86.h:30-34`), which corrected an earlier design draft's worry about
  the X86 and AArch64 maps; native `-msil` triples use `MSILTargetInfo<Native>` (`clang/lib/Basic/Targets/MSIL.h:83-84`,
  the verifier's addition).
- **Needs:** printing as `String ^`, as `clang/lib/AST/TypePrinter.cpp:1922-1923` and :2788-2789 print `__funcref`; C++
  mangling, a vendor qualifier in ItaniumMangle.cpp (:2880-2969, where an unlisted address space reaches
  `llvm_unreachable` on targets without map mangling) and in `clang/lib/AST/MicrosoftMangle.cpp`; the fences (2.6). **It
  fails open:** every other pointer rule applies until a fence refuses it.

**(C-ii) A type class of its own,** beside `BlockPointerType` (TypeNodes.td:49), fails closed, like WebAssembly's
externref, but every switch over type classes needs a case, and `==`, `!`, `if`, NULL and `?:` need allow-rules. Scale:
`OverflowBehaviorType` (TypeNodes.td:114), commit `7f631bb523a3` of 2026-02-19, changed 82 files (+4,112/-179), 55 of
them outside `clang/docs` and `clang/test` (54 in clang, and lldb's `TypeSystemClang.cpp`). **Corrected:** the check
called roughly half of the 55 the plumbing every type class needs; the verifier counted about 22: AST nodes, TypeLoc,
serialization, importer, manglers, printer, CGDebugInfo, TreeTransform, libclang and lldb. On either route (this check's
reading), CodeGen gives `target("msil.ref")`, and `==`, truth tests and NULL go through helpers, because `icmp` is
illegal on such types; debug info would describe an object-typed local once Portable PDB exists (phase 4), and until
then MSIL writes no debug info.

**The pointer route leaks silently in C** (the verifier; the check had called missed fences "loud but late", as invalid
IR or a failed backend slot pass):
- (a) `c ? s : t` with two different classes gives `void *` in the reference address space with only a warning
  (`checkConditionalPointerCompatibility`, SemaExpr.cpp:8776-8784; `ext_typecheck_cond_incompatible_pointers` is a plain
  ExtWarn, DiagnosticSemaKinds.td:10018-10020), and C then converts that pointer to any class without a cast.
- (b) Assigning one class's reference to another is an error only by default:
  `ext_typecheck_convert_incompatible_pointer` is DefaultError, and `-Wno-incompatible-pointer-types` turns it off
  (DiagnosticSemaKinds.td:9302-9319).
- (c) An explicit cast between two classes is a silent pointer cast unless a castclass rule lands. (d) Passing a
  reference to a function with no prototype (C17 and earlier) behaves like varargs.
- If all classes share one IR type, (a) to (c) give no invalid IR, so they are silent. The fix: every type in the
  reference space must point to a .NET class (`void` included), and class mix-ups are hard errors. (V) refuses (a) to
  (c) through C's struct rules, and C++ refuses (a) and (b) itself.

**"Loud" needs assertions** (the verifier): builds without assertions skip the IR verifier (`Clang::ConstructJob`,
Clang.cpp:5857-5859), and the MSIL backend has no verifier of its own (no match under `llvm/lib/Target/MSIL`). The
fork's development and CI builds have assertions, a future release toolchain would not, so the fences belong in Sema.

**A second Sema edit** (the verifier): `DeclaratorLocFiller::VisitPointerTypeLoc` asserts that a pointer type came from
a `*` chunk (SemaType.cpp:6325-6328; the filler runs in `GetTypeSourceInfoForDeclarator`'s loop, :6508). The fork's
builds have assertions on (MSIL.cmake:7; `msil/utils/build/msil-configure.ps1:35`), so a pointer-like type built from a
`^` chunk fires that assert unless the filler is taught too. The check had counted "one Sema branch"; the fix is small
and in the same file, but it is a second edit.

### 2.6 Fences, C++ and templates

- **Free with (C-i):** implicit conversion to `void *` and other pointers; pointer arithmetic, `s[i]` and `s->x`,
  because the pointee is incomplete.
- **Hand-written:** explicit casts to integers, `void *` and other pointers (C allows casts across address spaces; only
  OpenCL checks them, in `CastOperation::checkAddressSpaceCast`, `clang/lib/Sema/SemaCast.cpp:2699-2715`); `&s` and
  `String ^*`, on WebAssembly's model (`Sema::CheckAddressOfOperand`, SemaExpr.cpp:15504-15511;
  `Sema::BuildPointerType`, SemaType.cpp:1878-1891); fields, globals and statics, arrays, compound literals; varargs,
  `_Atomic`, `sizeof`, ordering (`<`), `restrict` and `*s`; asm operands, the `cleanup` attribute, SEH blocks and
  coroutines; in C++, class members, `T&` and `T&&` (WebAssembly's model, `Sema::BuildReferenceType`,
  SemaType.cpp:1957-1961), lambda captures, `new`, `throw`, `catch` and `bit_cast`; function pointers with reference
  parameters on the host triples, as for every reference option.
- **C++/CLI** (Microsoft's documentation): a `^` handle acts as a pointer to a whole object, with no member pointers or
  arithmetic, kept up to date by the GC; `%` is the tracking reference, and `^` on a value type a boxed value
  (`Int32^`). Clang implements none of it: clang-cl lists `/clr` and `/ZW` as unsupported (Options.td:9920-9929, :9991),
  and clang notes that it does not support /CLR (`clang/lib/Sema/SemaTypeTraits.cpp:781-783`). The verifier adds that
  C++/CLI backs the fences: no handle in a native type (the fix is `gcroot<T^>`, built on GCHandle) and no global or
  static handles outside managed classes (error C3145), so with no managed classes in C its limits equal the proposed
  fences: locals, parameters and results only.
- **Templates** (P-2's OS layer is C++): function templates take `String ^`; `T ^` needs the dependent-pointee change.
  Class templates that store `T` (`std::vector`, `std::optional`, `std::pair`) fail loudly at the field and pointer
  fences, as in C++/CLI. With (C-i), a `T*` template also matches `String ^` as a pointer and its body then hits the
  fences; (C-ii) avoids that.
- **Corrected, the template carrier:** the check would carry a dependent `T ^` as a block pointer until `T` is known.
  That breaks deduction, since `DeduceTemplateArgumentsByTypeMatch`'s `Type::BlockPointer` case deduces only from a
  block-pointer argument (`clang/lib/Sema/SemaTemplateDeduction.cpp:2220-2224`): `f(s)` with a real reference fails,
  only `f<String>(s)` works, and function-template signatures would mangle as `U13block_pointer`. Under (C-i) the fix is
  to build `T ^` directly as "pointer to `T` in the reference space", safe only if `^` never means a block on MSIL (the
  fork does not refuse `-fblocks` on MSIL triples today); under (C-ii) it needs a deduction case in
  SemaTemplateDeduction.cpp, missing from the check's costs. The verifier judged the carrier fatal to that template
  sub-idea, not to (C).
- **References and overloads:** `String ^&` and `String ^&&` are refused at first (IL byrefs to locals would need a
  second IR type), so `std::move`, `std::swap` and `const T&` APIs cannot take references; C++/CLI allows `String^%`,
  and (V) has the same gap with `String&`. `f(String ^)` and `f(Stream ^)` overload as distinct types and need the
  vendor-qualifier mangling, free for (V). An implicit conversion to `Object ^` (a proposal at the time) and explicit
  casts compiled to `castclass` are new rules under every answer.
- **Virtual functions** (the verifier; the check did not raise it): Design.md section 4.5 (`llvm/docs/MSIL/Design.md`,
  around line 156) makes every function pointer on the host triples a native-callable entry point; if C++ vtable slots
  follow that rule, virtual functions with reference parameters are fenced on host triples too, under every spelling.
  P-2 keeps tickets in its classes under every answer.

### 2.7 Blocks, Objective-C, tools and portability

- **Blocks** stay as they are in the check's design: a function pointee is a block, a CLI class pointee is a reference,
  anything else is an error, with no grammar clash. If `-fblocks` were passed on MSIL, both meanings would coexist, told
  apart by the pointee, as Objective-C tells C pointers from object pointers. The verifier proposed refusing `-fblocks`
  on MSIL triples instead, so that `^` has one meaning there (its template fix, 2.6).
- **Objective-C.** The user asked (2026-11-19 09:46; a question, not a pick): "The obvious issue is.... how would the
  Objective-C to IL compiler work?" Claude's answer: Objective-C is not a project goal, but a reference `^` on a
  `CLI_TYPE` class can coexist with blocks by the pointee, while turning Blocks off on MSIL would rule Objective-C out.
  The later pick keeps Blocks for function pointees (Claude's reading, 2.11).
- **Tools.** The fork builds only clang and lld (MSIL.cmake:12; `llvm/docs/MSIL/GettingStarted.md:96`). A stock clangd
  shows at least two errors per declaration (inferred from SemaType.cpp, not run); a clangd built from the fork would
  understand (C), (K) and (V). A stock clang-format classes a `^` after a name as a binary operator
  (`clang/lib/Format/TokenAnnotator.cpp:2472-2476`, :3187-3199), so `String ^s` likely becomes `String ^ s` (not run),
  and it knows block declarators only as `(^name)` (:464-467, :495-496). **Corrected:** the check said no setting
  changes this; clang-format's `Macros` option (`clang/lib/Format/Format.cpp:1442`) can map a mark macro such as
  `CLI_REF` to `*`, though not a literal caret.
- **Portability.** Another compiler cannot parse a line containing `String ^s`; text inside `#ifdef __MSIL__` is fine,
  because other compilers skip it and `^` is a valid preprocessing token. **Corrected:** the check said no macro can
  hide a declarator character short of wrapping the whole type; an object-like macro can expand to `^`
  (`#define CLI_REF ^`, then `String CLI_REF s`). Only a literal caret is beyond tools and macros, so the trade is the
  same for (C) and (K): a macro that means something else on other compilers is a silent state, which needs the user's
  OK (the user's pick of 2026-11-04: "silent states only with my OK").
- **Truthfulness** (this check's reading): the type says "a reference to a String object", which is what the variable
  holds; `String ^b = a;` copies the reference, as a pointer copy does. `*` points into C memory and `^` refers into the
  .NET heap, as in C++/CLI, and `Point ^p` could later spell a boxed value type. (C) and (K) meet the user's "a special
  type that represents that it is compiled AS a reference" (2026-11-18 20:31) literally and pass "storing data, which is
  lied about by the type" on any reading; (V) meets the first through the class kind, not the spelling, and passes the
  second only on the C# reading. An agent's earlier design draft left C++/CLI syntax out, but (C) borrows only C++/CLI's
  glyph and meaning, not its object model.

### 2.8 (K) a keyword, and (V) no mark

**(K) `__cli_ref(String) s`** builds (C)'s type: one type, two spellings, so Sema, CodeGen, the fences and C++ cost the
same, plus the keyword. An agent's earlier design draft had proposed exactly `__cli_ref(T)`.
- **Parser and token:** modelled on `_Atomic(T)` (`ParseDeclarationSpecifiers`, ParseDecl.cpp:4653-4664;
  `ParseAtomicSpecifier`, :8249-8282, giving the specifier kind `TST_atomic`), plus the "could this start a type?"
  tests, C++ tentative parsing and the specifier kind's sites. The token is one line in TokenKinds.def, on every target,
  refused off MSIL as clang refuses `__funcref` off WebAssembly (ParseDecl.cpp:1029-1036). TokenKinds.h reaches nearly
  all of clang (`clang/include/clang/Basic/IdentifierTable.h:22`; `clang/include/clang/Lex/Token.h:17`), as does
  Specifiers.h (TypeBase.h:33): a rebuild of most of clang (not measured).
- **Files, corrected:** the check counted about eight spelling-only files, none hooked (TokenKinds.def, Specifiers.h,
  DeclSpec.h, DeclSpec.cpp, ParseDecl.cpp, ParseTentative.cpp, ParseExprCXX.cpp or ParseExpr.cpp,
  SemaTemplateVariadic.cpp); the verifier adds `clang/include/clang/Parse/Parser.h` for the new parse function, as
  `ParseAtomicSpecifier` is declared there (:2550): about nine. A cheaper route, clang's transform-type-trait table
  (`clang/include/clang/Basic/BuiltinTraits.td:330-333`, the `__add_pointer(T)` style), generates the token, the
  specifier kind and most parser cases, but its keywords are C++-only (TokenKinds.def:67-68), and a trait is a
  non-deduced context in templates.
- **Tools and portability:** clang-format's `TypenameMacros` option (Format.cpp:1562) can declare it a type; a guard
  macro that hides it on another compiler changes its meaning there (`#define __cli_ref(T) T *` makes it a C pointer), a
  silent state that needs the user's OK. Honest but long: `__cli_ref(System_IO_FileStream) fs`.
- **Cesium:** its documented extensions are `__cli_import(...)`, `__nint`, `__nuint` and `__CESIUM__`, with no syntax
  for object references, so none of the three spellings is Cesium's. (K) is in Cesium's keyword style, in one family
  with `__cli_import`, the form of the user's pick "(B) Cesium + signature" (2026-11-18 15:12).

**(V) `String s`** (from the H-star check, an earlier check of the starred spellings; section 6.4): clang has never
treated a struct as one value; CodeGen treats records as memory, and on the portable triples a struct with no fields
counts as empty and is dropped silently if a hook is missed (`clang/lib/CodeGen/ABIInfoImpl.cpp:285-303`, spot-checked).
In C++, `==` and truth tests on a class go to overload resolution (TypeBase.h:9222-9227, spot-checked). The "used before
set" warning (UninitializedValues.cpp:40-66) and `_Nullable` (Type.cpp:5340-5355) are lost, and `String b = a;` looks
like a copy but shares one object. It needs no token, type class or address space, prints and mangles as `String`, and
fails closed with most fences free.

### 2.9 Costs and upstream footprint (this check's estimates, from reading)

"Hooked" means listed in `msil/utils/upstream-hooks.txt`, which names 82 upstream files (clang's run from :265 to :747).
- **Before references:** (C) needs nothing; the check recommended an MSIL error in place of the Blocks advice, needing
  SemaType.cpp (not hooked: one new hook) and DiagnosticSemaKinds.td (hooked). (K) needs nothing (a "not yet" keyword
  would cost the keyword files and a rebuild). (V) needs two small refusals in SemaDecl.cpp (hooked) or the fork's own
  Sema file: a by-value class struct in a declaration that is not a definition, and objects with static or thread
  storage (`extern String g;`). The check's common ticket costs fell away with integer tickets (2.1).
- **The reference step, common to all three:** in LLVM core the IR type (`llvm/lib/IR/Type.cpp`), its value-type tag
  (`llvm/include/llvm/CodeGen/ValueTypes.td`, `llvm/lib/CodeGen/ValueTypes.cpp`) and, if WebAssembly's slot pass is
  ported, a stack ID (`llvm/include/llvm/CodeGen/TargetFrameLowering.h`), none hooked, so needing the user's OK under
  Design.md's fork-maintenance rule (`llvm/docs/MSIL/Design.md:316`: keep the diff to new directories plus registration
  hooks); the MSIL backend, the metadata writer and msil-ld; shared clang rules ("a reference local has no address";
  SEH, asm, coroutines; host-triple function pointers); CodeGen in CodeGenTypes.cpp and
  `clang/lib/CodeGen/CGExprScalar.cpp` (not hooked), `clang/lib/CodeGen/CGCall.cpp` and
  `clang/lib/CodeGen/Targets/X86.cpp` (hooked).
- **(C) by the pointer route:** about 20 to 24 clang files with C++, about 5 of them hooked today: AddressSpaces.h,
  TypeBase.h and Type.cpp; SemaType.cpp, SemaExpr.cpp, SemaCast.cpp, SemaDecl.cpp (hooked), SemaDeclAttr.cpp (hooked),
  SemaStmt.cpp, SemaStmtAsm.cpp, SemaInit.cpp, SemaOverload.cpp, SemaExprCXX.cpp and SemaCoroutine.cpp; TypePrinter.cpp,
  ItaniumMangle.cpp and MicrosoftMangle.cpp; and the common CodeGen files. It is WebAssembly's well-trodden path; a type
  class of its own costs far more (`OverflowBehaviorType`: 55 source files).
- **(K):** (C)'s list plus the keyword files, about 28 to 32 files in all (counted with eight keyword files, before the
  verifier's ninth); none of the keyword files is hooked. **(V):** 12 to 14 shared clang files for C and 18 to 22 with
  C++ (the H-star check's estimate; the verifier: the 12 to 14 count C-only shared clang files new to the hook list,
  plus 3 to 5 in LLVM core), new ground.
- **Rebuilds:** every route rebuilds most of clang at the reference step, because TypeBase.h includes AddressSpaces.h
  and Specifiers.h (:23, :33); only (K) also changes the token table, which IdentifierTable.h:22 and Token.h:17 pull in
  everywhere.

### 2.10 Verdicts on the three spellings

**This check's reading.** No fatal flaw in any of the three; the fatal sub-idea is Apple's block-pointer type for
`String ^` (2.4); `$`, `%` and `@` are ruled out (2.2). Lean: (C) for hand-written C (cheapest in clang, shortest to
read, already a loud error, C++/CLI's meaning); (K) if editors and other compilers must read these lines, or for a
`__cli_` keyword family, and addable later on the same type for the keyword's cost alone; (V) third (no new mark, but
the most new-ground clang work and the weakest on the user's truth test). (C)'s weak points: `^` shared with Blocks,
told apart by the pointee; unreadable to stock tools and other compilers; C++/CLI expectations it will not meet
(`gcnew`, `->`, `%`); failing open by the pointer route until each fence is written.

**An adversarial verifier's reading (not a decision).** The check's answer holds: the caret is cheapest, since clang
already reads `^` in every declaration and refuses it on these targets today (ParseDecl.cpp:6531; SemaType.cpp:2875); a
keyword costs more; a plain attribute costs least, but other compilers would silently ignore it. All 52 facts were
re-checked at `a15f3c958572` and against six public pages (three from Microsoft Learn, two from cppreference, Cesium's
extensions page): 50 confirmed, 2 corrected (2.4, 2.2); the other findings are applied above. Lean: (C) for the first
real-reference slice, with the fixes for the leaks, templates and assert, and an MSIL error in place of the Blocks
advice (optional, since today's two errors are loud; a new hook in SemaType.cpp, so it needs the user's OK under
Design.md's fork-maintenance rule); (K) later as a second spelling of the same type; (V) third. No fatal flaw in (C),
(K) or (V); confirmed fatal: Apple's block-pointer type for `String ^`, and the block-pointer carrier for templates,
fixable by building `T ^` directly.

### 2.11 What followed

- The user first picked the caret, "Let's go with C, the carrot." (2026-11-19 10:27), then preferred Claude's keyword
  shape K2, in which `typedef __cli_ref("System.String") String;` makes `String s` the reference: "Wait, hold on." and
  "Your simpler shape I like better." (10:29). On value types the user then said (10:40): "I guess that's the benefit of
  the ^ syntax. YOu can represent both." (Claude's reading: `DateTime d`, the value in C memory; `DateTime ^r`, a
  reference to a boxed copy).
- After the K2 and value-types checks (sections 3 and 4), Claude recommended K2 with a type form, `__cli_ref(DateTime)`,
  against the caret built as a type of its own. The user picked "Caret" (2026-11-19 12:31; section 6.7). Claude's
  readings recorded with the pick: the caret builds a type of its own, so it fails closed and class mix-ups are hard
  errors; the type is keyed by the folded .NET name from `CLI_TYPE`, so two C names for one class give one reference
  type (Claude's reading of the user's "The specifc names of the C structs or C prototypes and such don't matter, since
  the attributes specify them.", 2026-11-18 20:35; 5.21 RA6); and Blocks keep `^` for function pointees, so `^` means
  two things in Objective-C code.
- The user then approved the upstream footprint of the caret built as a type of its own, "(a) Yes" (2026-11-19 12:39;
  Claude's recommendation), over the cheaper pointer route, which fails open (section 6.11).
- In R-a's contract (5.21) this check shows in RA5 (never the block pointer; only the parser's `^` is reused), RC1, RC2
  (T-R4 replaces the block error and its `-fblocks` advice), RC5 (`DeclaratorLocFiller`) and RC7 (`-fblocks` off by
  default; function pointees stay blocks); in 5.21.4, where RS4, RS7, RS8 and RS18 close the leaks (b), (a), (c) and (d)
  of 2.5; and in R-a2's RN2 (a dependent `T ^` never a block pointer) and RN4 (`String ^&` and `String ^&&` refused for
  now, among Claude's additions).

## 3. The K2 check: one clang type per .NET class

K2 is the spelling `typedef __cli_ref("System.String") String;`, in which `String s` is a real .NET reference: a keyword
type specifier that takes the .NET name, with no struct and no `*` for classes. Claude offered it as not yet checked;
the user answered the spelling question (references Q2, section 6.6) with "Wait, hold on." then "Your simpler shape I
like better." (2026-11-19 10:29), revising "Let's go with C, the carrot." (10:27). The K2 check was a read-only check by
an agent on 2026-11-19 at HEAD `a15f3c958572` (nothing built, compiled or edited); an adversarial verifier re-checked it
at the same commit. Their leans, estimates and recommendations are "this check's reading" and "the verifier's reading",
never decisions; counts are estimates from reading unless marked measured.

After this check and the value-types check (section 4), Claude asked the spelling question again, recommending K2 with a
type form, and the user picked "Caret" (2026-11-19 12:31; section 6.7): a reference is `T ^r`, `T` a `CLI_TYPE` struct,
built as a type of its own (5.21 RA1, RA5). K2's spelling is not in R-a, whose parser is unchanged (5.21.13 RH6). What
5.21 and the R-a milestone take from this check, with the verifier's corrections: the type (RC3, "the K2 check,
adopted"; 3.2, 3.4), the switches over type classes (RC8, RH5; 3.3), the rules and NULL (5.21.4; 3.4-3.7), no ABI hook
(RG6), jumps refused (RS28), asm operands refused (RS29), the folded key (RT3) and the approved upstream count (RH2;
3.10).

The check and the verifier cited lines; the function named beside a line was looked up at HEAD `a15f3c958572` for this
copy. In this section, square brackets after a point name the R-a items or 5.21 subsections that carry it (RS in 5.21.4,
RG in 5.21.5, RI in 5.21.6; others as noted).

### 3.1 Verdicts on K2

**This check: "K2 works. No fatal flaw was found."** The spelling fits clang's shape for an argument-carrying type
keyword, `_BitInt(N)`; "one type per .NET name" fits clang's type system (a canonical type keyed by an interned name,
uniqued by pointer, serialized with the existing identifier properties); the rules mostly come free (the sizeless rules
refuse storage, OpenCL's `queue_t` is the precedent for NULL and `==`, AMDGPU's predicate for `if` on a non-scalar
type); the backend half is the path plan's (section 1). Fatal sub-ideas to avoid: (1) the class only as sugar over one
shared type, such as a `btf_type_tag`-style attribute (BTFTagAttributedType is NeverCanonical, TypeNodes.td:87), since
clang compares canonical types for assignment (SemaExpr.cpp:9931-9935), `?:` (:9288-9291), overloads and templates, so
`String s = stream;` would compile silently; (2) keying the type by the raw text without MC-1's normalization, which
makes two spellings of one class two types. Among its weak points: a typo in a class name compiles and is found only
where a stub or cast binds it. It also corrected two of Claude's first descriptions of K2: the cost is the keyword plus
a type of its own, not "the keyword option's cost" (costed on the pointer route, which K2 cannot use: it has no
pointee); the rules are about 10 refusals and about 8 allow sites in C, not "WebAssembly's two extra rules plus ==, NULL
and if".

**The verifier: "K2 stands."** Of 63 facts re-checked, 61 were confirmed, 2 corrected (the ABI and inline asm, 3.8) and
none refuted. It confirmed both fatal sub-ideas and extended the second to arrays (3.2). Its holes are applied below,
each marked as the verifier's. One smaller point needs no section: MC-1's TYPE text carries no assembly, so one name in
two assemblies is one C type, as in MC-1, and `castclass` catches a mismatch at run time.

### 3.2 The type of its own

- **This check's type:** a new type class, `CLIReferenceType`, always canonical like BuiltinType (TypeNodes.td:43-46),
  with one field: the .NET name, interned once in clang's identifier table. Uniqued by that name (a FoldingSet, like
  BitIntType's), two typedef names of one class are one type and two classes are two types, which meets by construction
  the user's direction "The specifc names of the C structs or C prototypes and such don't matter, since the attributes
  specify them." (2026-11-18 20:35; message 7, section 6.2). The name goes through MC-1's TYPE parser,
  `cli::parseManagedTypeName` (ManagedSignature.h:1023-1063), which trims, refuses a leading `class ` and requires full
  names (`System.String`, never `string`).
- **Corrected by the verifier (arrays):** that parser returns `string[]` and `class System.String[]` as two different
  values (its array branch, ManagedSignature.h:1027-1038; `ManagedSigParser::parseBaseType`, :853-859, :876-877), and
  likewise `object[]` and `class System.Object[]`, and spacing (`string []`), so one .NET type would be two C types:
  loud, but against message 7. The identity must be a canonical print of the parsed type with `class System.String` and
  `class System.Object` folded (MC-1 folds `class System.Object` in one rule, in `cli::checkManagedCallShape`,
  :1185-1188), or those forms refused. "MC-1's TYPE grammar exactly" also contradicted "no `valuetype`": the grammar
  accepts it (:1040-1046). R-a: RT2 (the grammar minus `valuetype`), RT3 (folded at every level; the key is the
  canonical listing text; a new `cli::parseCLITypeName`).
- **R-a's node** (RC3, Claude's design) is not always canonical: the canonical node is profiled by the key, interned as
  an `IdentifierInfo`, and its class slot holds one implicit, never-completed struct per key; sugared nodes carry the
  struct as written and desugar to it; `CLIReferenceTypeLoc` holds the caret and the class's TypeLoc.
- **Keying by a string** (this check) is cheap: TypeProperties.td serializes identifier-valued properties (:621, :914;
  PropertiesBase.td:128), not plain strings. Debug info needs one case, a forward-declared class named for the .NET
  type, as for WebAssembly's reference types (`CGDebugInfo::CreateType(const BuiltinType *)`, CGDebugInfo.cpp:1118-1125)
  [RG7].
- **Mangling** (this check): Itanium, a vendor type with a type argument, `u9__cli_refIN6System6StringEE`, the shape
  clang emits for dependent type traits (`CXXNameMangler::mangleType(const UnaryTransformType *)`,
  ItaniumMangle.cpp:4687-4705) and LLVM's demangler reads (ItaniumDemangle.h:4336-4352); Microsoft, an artificial struct
  in namespace `__clang`, as for OverflowBehaviorType (MicrosoftMangle.cpp:4028-4044); R-a needs none, since both
  manglers can report a type as unsupported, loudly (ItaniumMangle.cpp:1269-1276; `MicrosoftCXXNameMangler::Error`,
  MicrosoftMangle.cpp:579-593). Corrected by the verifier: if `/` and `.` both become nesting, `Ns.Outer/Inner` (a
  nested type) and `Ns.Outer.Inner` (a namespace) are both `N2Ns5Outer5InnerE`, so two overloads share one symbol and,
  across files, link silently; R-a2 needs an encoding that keeps them apart and covers arrays (5.21.16 RN6). R-a reports
  `cannot mangle this .NET reference type yet` through `err_ms_mangle_unsupported` ("cannot mangle this %0 yet",
  DiagnosticASTKinds.td:1092) in both manglers (RG10, Claude's decision).
- **Why not a pointer underneath** (WebAssembly's `__funcref` route): pointer rules inherit the caret verifier's silent
  holes (section 2.5: `?:` across two classes, class mixing that `-Wno-incompatible-pointer-types` turns off, silent
  class casts). This check's reading: a type of its own is worth its cost under the user's pick on the review page
  (2026-11-04, item prov-10), "silent states only with my OK".
- **A missing alternative** (the verifier): one implicit, never-completed record per .NET name, used by value with the
  no-star rules and no new type class, about 18-20 shared clang files for C (from the estimate of 12-14 for C and 18-22
  with C++ that the no-star check's costs repeat from the H-star check, section 2.9); it fails closed too, but is new
  ground, the portable ABI drops empty records silently (`MSILPortableABIInfo::classifyArgumentType`,
  Targets/MSIL.cpp:73-75), and modules and C++ are hard. So "clang needs a new kind of type" was too strong. RC3 keeps a
  type class and puts one such struct per key in its class slot.

### 3.3 The K2 check's list of switches over type classes

This check took its files from the footprint of HLSLInlineSpirvType, a canonical leaf type; the table keeps its order.
The functions are those that name HLSLInlineSpirvType at HEAD or hold the check's own lines ("the check's :n"); a new
class needs its case or method beside them. "Switch :a, case :b" is the switch over type classes and
HLSLInlineSpirvType's case in it. The last column is R-a's package (5.21.15) and item.

| File | Switches and visitors at HEAD | R-a |
|---|---|---|
| `clang/include/clang/Basic/TypeNodes.td` | the node list (HLSLInlineSpirvType :89) | R-a.a, RC3 |
| `clang/include/clang/AST/TypeBase.h` | the class (:7040), `Type::isHLSLInlineSpirvType` (:9029) | R-a.a |
| `clang/include/clang/AST/TypeLoc.h` | `HLSLInlineSpirvTypeLoc` (:1140) | R-a.a, `CLIReferenceTypeLoc` |
| `clang/include/clang/AST/TypeProperties.td` | `let Class = HLSLInlineSpirvType` (:714) | R-a.a |
| `clang/include/clang/AST/RecursiveASTVisitor.h` | `DEF_TRAVERSE_TYPE` (:1186), `DEF_TRAVERSE_TYPELOC` (:1535) | R-a.a |
| `clang/include/clang/AST/ASTNodeTraverser.h` | `VisitHLSLInlineSpirvType` (:459), the dumper | R-a.a |
| `clang/include/clang/AST/ASTContext.h` | the FoldingSet (:327), `getHLSLInlineSpirvType` (:2088) | R-a.a, the getter declared |
| `clang/lib/AST/ASTContext.cpp` | `ASTContext::getTypeInfoImpl` (switch :2089; the check's :2603-2611); `encodeTypeForFunctionPointerAuth` (switch :3412, case :3655); `ASTContext::getVariableArrayDecayedType` (:4344, :4385); the getter `ASTContext::getHLSLInlineSpirvType` (:5928-5949); `ASTContext::getObjCEncodingForTypeImpl` (:9454, :9792); `ASTContext::mergeTypes` (:12063, :12316); `getCommonNonSugarTypeNode` (:14470, :14489); `getCommonSugarTypeNode` (:14809, :14832) | R-a.b: the getter, type info, encodings (unreachable), the variably-modified walk, merging of equal canonical types only, common sugar |
| `clang/lib/AST/ASTImporter.cpp` | `ASTNodeImporter::VisitHLSLInlineSpirvType` (:2073) | R-a.b, import |
| `clang/lib/AST/ASTStructuralEquivalence.cpp` | `ASTStructuralEquivalence::isEquivalent` (switch :961, case :1264) | R-a.b, equal keys |
| `clang/lib/AST/ExprConstant.cpp` | `EvaluateBuiltinClassifyType` (switch :16514, case :16649) | R-a.b, `__builtin_classify_type` |
| `clang/lib/AST/Type.cpp` | `computeCachedProperties` (:5097, :5192); `LinkageComputer::computeTypeLinkageInfo` (:5213, :5297); `Type::canHaveNullability` (:5343, :5491) | R-a.b: linkage, and RC3's sizeless edit (3.4) |
| `clang/lib/AST/TypeLoc.cpp` | no HLSLInlineSpirvType site; `TypeLoc::getEndLoc` (switch :231, `default:` at :232; `BlockPointer` among the prefix types, :257-267) | R-a.b, "end location" (5.21.13's table) |
| `clang/lib/AST/TypePrinter.cpp` | `TypePrinter::canPrefixQualifiers` (switch :218, case :248); `TypePrinter::printHLSLInlineSpirvBefore` (:2246), `printHLSLInlineSpirvAfter` (:2288) | R-a.b, RC4's display name with aka text |
| `clang/lib/AST/ItaniumMangle.cpp` | `CXXNameMangler::mangleUnresolvedTypeOrSimpleId` (switch :2532, case :2561); `CXXNameMangler::mangleType(const HLSLInlineSpirvType *)` (:4819) | R-a.b, RG10 |
| `clang/lib/AST/MicrosoftMangle.cpp` | `MicrosoftCXXNameMangler::mangleType(const HLSLInlineSpirvType *, ...)` (:4023) | R-a.b, RG10 |
| `clang/include/clang/Serialization/TypeBitCodes.def` | `TYPE_BIT_CODE(HLSLInlineSpirv, HLSL_INLINE_SPIRV, 60)` (:69) | R-a.a, `TYPE_CLI_REFERENCE` appended |
| `clang/lib/Serialization/ASTReader.cpp` | `TypeLocReader::VisitHLSLInlineSpirvTypeLoc` (:7845) | R-a.b |
| `clang/lib/Serialization/ASTWriter.cpp` | `TypeLocWriter::VisitHLSLInlineSpirvTypeLoc` (:616) | R-a.b |
| `clang/lib/Sema/SemaType.cpp` | `TypeSpecLocFiller::VisitHLSLInlineSpirvTypeLoc` (:6032) | R-a.c; for the caret RC5 names `DeclaratorLocFiller::VisitCLIReferenceTypeLoc` and the chunk sites |
| `clang/lib/Sema/SemaExpr.cpp` | `captureVariablyModifiedType` (switch :4671, case :4695) | R-a.c; named by RH5 |
| `clang/lib/Sema/SemaLookup.cpp` | `addAssociatedClassesAndNamespaces` (switch :3159; the check's :3301-3308, where inline SPIR-V types "are treated as fundamental types") | R-a.b, the ADL case |
| `clang/lib/Sema/SemaTemplate.cpp` | `UnnamedLocalNoLinkageFinder::VisitHLSLInlineSpirvType` (:6594) | R-a.b, a visitor needed to link |
| `clang/lib/Sema/SemaTemplateDeduction.cpp` | `DeduceTemplateArgumentsByTypeMatch` (switch :1863; the check's :2553-2555); `MarkUsedTemplateParameters` for a `QualType` (:7000, switch :7012; the check's :7272-7295) | R-a.b; R-a2 adds the C++ rules |
| `clang/lib/Sema/TreeTransform.h` | `TreeTransform<Derived>::TransformHLSLInlineSpirvType` (:8016) | R-a.a |
| `clang/lib/CodeGen/CodeGenTypes.cpp` | `CodeGenTypes::ConvertType` (switch :396; the check's :797-803) | R-a.d, RG1 |
| `clang/lib/CodeGen/CodeGenFunction.cpp` | `CodeGenFunction::getEvaluationKind` (:238-296, switch :241; the check's :270-272, `TEK_Scalar`); `CodeGenFunction::EmitVariablyModifiedType` (switch :2577; the check's :2602, "never variably-modified", and :2692, a group of sugar types) | R-a.d, RG1, RG8 |
| `clang/lib/CodeGen/CGDebugInfo.cpp`, `.h` | `CGDebugInfo::CreateTypeNode` (switch :4415; the check's :4472-4481); `CGDebugInfo::CreateType(const HLSLInlineSpirvType *, ...)` (CGDebugInfo.cpp:4113; CGDebugInfo.h:225) | R-a.d (`.cpp`), R-a.a (`.h`), RG7 |
| `clang/lib/CodeGen/ItaniumCXXABI.cpp` | `ItaniumRTTIBuilder::BuildVTablePointer` (switch :3686; the check's :3711 and :3789); `ItaniumRTTIBuilder::BuildTypeInfo` (switch :4005; the check's :4095; the HLSL types at :4098-4100) | R-a.b, the RTTI cases |
| `clang/lib/CodeGen/QualTypeMapper.cpp` | `QualTypeMapper::convertTypeImpl` (switch :91; the check's :157-164, where the HLSL types report "Type not supported in ABI lowering") | R-a.b, unsupported |

Not built by the fork, so they can wait (this check): libclang (`clang/include/clang-c/Index.h`,
`clang/tools/libclang/CIndex.cpp`, `clang/tools/libclang/CXType.cpp`), ClangIR's three files and lldb's
`TypeSystemClang.cpp`. The check's CodeGen part names six of the switches above as CodeGen's difference from the caret's
pointer paths: `ConvertType`, the evaluation kind (a scalar value), the variably-modified walk, debug info, C++ type
info (ItaniumCXXABI.cpp) and QualTypeMapper.cpp.

**A missed case.** This check: a `-Wswitch` warning at build time, and a crash (loud) if it is ever reached. Corrected
by the verifier: the crash needs an assertions build; in a release build `llvm_unreachable` is undefined behaviour. The
fork builds with assertions (MSIL.cmake:7), but the verifier notes a release toolchain among the user's goals and
suggests making `-Wswitch` an error for clang at upstream merges. R-a (RH5, Claude's decision): gate step 30
syntax-checks R-a's 22 switch files with `-Werror=switch`; from R-a on, `check-upstream-hooks.py --check` (the manifest
leg of `upstream-hooks.ps1`, run by R-a's gate, `run-m1-gate.ps1` and the upstream-merge gate) fails a switch in
`clang/include`, `clang/lib` or `clang/tools` on `getTypeClass()` or `getTypeLocClass()`, or on a local initialized from
one, that has no `CLIReference` case, no `default:` of its own and no generated case for every class.

**Where R-a's text differs from the check's list:**

1. ClangIR is built (RH4: `CLANG_ENABLE_CIR=ON`, and `clang` links it) and reachable on MSIL triples with `-fclangir`,
   so its four exhaustive switches get the class (Claude's decision; two upstream files):
   `CIRGenFunction::getEvaluationKind` (switch :82, the HLSL case :113) and `CIRGenFunction::emitVariablyModifiedType`
   (:1708, :1713) in CIRGenFunction.cpp, `vTableClassNameForType` (:699, :781) and
   `CIRGenItaniumRTTIBuilder::buildTypeInfo` (:1188, :1284) in CIRGenItaniumCXXABI.cpp. CIRGenTypes.cpp is not changed:
   a reference reaching ClangIR gets the not-yet-implemented error of `CIRGenTypes::convertType`'s default case.
2. libclang is not a build target, but `ninja check-clang` builds it (c-index-test is a clang test dependency), and the
   `--full` upstream leg of lit-legs.sh runs that target. `CursorVisitor` declares a `Visit<Class>TypeLoc` method for
   every TypeLoc class through TypeLocNodes.def and CIndex.cpp defines each by hand (HLSLInlineSpirvType's at :1774), so
   CIndex.cpp gains `CursorVisitor::VisitCLIReferenceTypeLoc`, visiting the class's TypeLoc as
   `VisitBlockPointerTypeLoc` visits its pointee's (RH4, Claude's decision). Index.h and CXType.cpp are in none of
   5.21.15's lists; lldb's TypeSystemClang.cpp is neither built nor changed (RH6).
3. The caret brings sites a type-specifier spelling does not: SemaType.cpp's `DeclaratorLocFiller` gains
   `VisitCLIReferenceTypeLoc` (else its `VisitTypeLoc` reaches `llvm_unreachable`) and 18 `BlockPointer` chunk sites
   treat a chunk that built a reference as no pointer (RC5); TypeLoc.cpp's end location; common sugar takes the
   canonical node, never a merge of two structs (RC8).
4. Of the files the verifier said may also be needed (3.10), only JumpDiagnostics.cpp is changed (RS28); SemaStmtAsm.cpp
   (the asm rule is in CGStmt.cpp, RS29), DiagnosticASTKinds.td (RG10 reuses `err_ms_mangle_unsupported`) and Parser.h
   (RC1) are not.
5. Of the check's 31 files, 10 headers go to R-a.a, 2 to R-a.c, 3 to R-a.d and 16 to R-a.b (the table's last column),
   whose 19 files add the two ClangIR files and CIndex.cpp. RH5's 22 switch files (gate step 30) are R-a.b's 19 without
   CIndex.cpp, plus SemaExpr.cpp (`captureVariablyModifiedType`, which the check listed only as a file),
   CodeGenTypes.cpp, CodeGenFunction.cpp and CGDebugInfo.cpp; CIndex.cpp has gate steps 25 (its hook is there) and 29 (a
   syntax check), and only the `--full` leg links libclang.

### 3.4 Sizeless, not scalar (5.21 RC3)

This check's recommendation, adopted by R-a.

- **Two one-line generic edits come first**, since "sizeless" means builtin only today: `Type::isSizelessType()` just
  calls `isSizelessBuiltinType()` (Type.cpp:2789; the list at :2751-2771), and the storage test of
  `Sema::RequireCompleteTypeImpl` asks the same (SemaType.cpp:9779-9781); the verifier confirmed both. For C++ traits,
  `QualType::isTrivialType` (Type.cpp:2994), `isTriviallyCopyableTypeImpl` (:3061) and `QualType::isCXX11PODType`
  (:3387) should also say yes. R-a adds (RC3): `isSizelessBuiltinType()` does not learn the type, so braced initializers
  stay refused: `InitListChecker::CheckListElementTypes` checks sizeless builtins as scalars (SemaInit.cpp:1476-1480)
  and reports `err_illegal_initializer_type` for other non-scalar types (:1488-1493).
- **Then refused free:** fields and unions (`Sema::CheckFieldDecl`, SemaDecl.cpp:19710-19714); arrays
  (`Sema::BuildArrayType`, SemaType.cpp:2179-2181, :2193-2196); `sizeof` and `alignof`
  (`Sema::CheckUnaryExprOrTypeTraitOperand`, SemaExpr.cpp:4529-4539); globals, statics, `thread_local` and `extern`
  (`Sema::CheckVariableDeclarationType`, SemaDecl.cpp:9314-9318, which exempts WebAssembly references and HLSL types,
  not this type); `_Atomic` (`Sema::BuildAtomicType`, SemaType.cpp:10502-10503); `alignas`
  (`Sema::CheckAlignasUnderalignment`, SemaDeclAttr.cpp:5020); `new`, `throw`, `catch` and exception specifications
  (SemaExprCXX.cpp:2694, :1008-1010; SemaDeclCXX.cpp:17561-17563; SemaExceptionSpec.cpp:180-184); by-copy lambda
  captures (`captureInLambda`, SemaExpr.cpp:20076-20081); casts to and from integers, `void *` and pointers
  (`CastOperation::CheckCStyleCast`, SemaCast.cpp:3136-3184, :3191-3197); arithmetic, `<`, `>` and `switch` [RS14 and
  the list pinned after RS30].
- **Precedent:** a sizeless, non-scalar type already let into conditions, AMDGPU's feature predicate
  (`Sema::CheckBooleanCondition`, SemaExpr.cpp:21722-21723; `Sema::CheckLogicalOperands`, :14312-14315). The verifier
  adds that this precedent rewrites the predicate into a builtin call (:21722-21724), so `if (s)` needs a rewrite or a
  CodeGen bool conversion, not only an allow-rule; RS6 rewrites truth tests into `r != NULL` (Claude's decision).
- **Scalar is worse** (this check): it opens only C's condition contexts (SemaExpr.cpp:21735, :16699, :14318, :8665);
  `==` and NULL still need rules (OpenCL's `queue_t` needed its own, :10580-10585, :13683-13695), and in C++ even
  `if (s)` (SemaOverload.cpp:2600-2606; Sema.cpp:885-898); it needs a new scalar kind (TypeBase.h:2836-2847) with cases
  in `Type::getScalarTypeKind` (Type.cpp:2582-2621) and `Sema::PrepareScalarCast` (SemaExpr.cpp:7930-8110), hand-written
  storage refusals, and opens template value parameters (SemaTemplate.cpp:1390) and `__is_scalar`. It fails open. The
  phrase "scalar gives ==, if and NULL free" holds only for pointer-shaped types, which fail open (the check's
  correction of the question's framing).

### 3.5 The rules in C

**Allowed:**

- Locals [RS1]; parameters and results, which R-a limits to static functions whose address is never taken, the path
  plan's rule under every spelling [RS3].
- Assignment, initialization, arguments and returns of the same class: free (`Sema::CheckAssignmentConstraints`,
  SemaExpr.cpp:9931-9935; C initialization uses the same check through the `SK_CAssignment` step of
  `InitializationSequence::Perform`, SemaInit.cpp:8589-8597). Different classes are a hard error no flag disables
  (`err_typecheck_convert_incompatible` in `Sema::DiagnoseAssignmentResult`, SemaExpr.cpp:18159-18171) [RS4].
- `==` and `!=`, a rule in `Sema::CheckCompareOperands` (:13193): the same class, `System.Object` against any class (the
  other side converts), or a null pointer constant; two different classes are an error [RS5].
- `!`, `if`, `while`, `for`, `do`, `&&`, `||` and the `?:` condition: rules in `Sema::CheckBooleanCondition` (:21735),
  `Sema::CreateBuiltinUnaryOp` (:16699), `Sema::CheckLogicalOperands` (:14318) and `checkCondition` (:8665); true means
  not null; C++ adds the bool conversion (SemaOverload.cpp:2600-2606) and a cast kind (Sema.cpp:885-898) [RS6; 5.21.16
  RN3].
- `?:` operands: the same class is free (:9288-9291); with NULL or Object, a rule in `Sema::CheckConditionalOperands`
  (:9115), before :9286 [RS7].
- The implicit conversion to `System.Object`: in C a rule in `Sema::CheckAssignmentConstraints` (:9919), in C++ a new
  implicit-conversion kind [RS4, under the user's pick "(a) Only to Object" (2026-11-20 10:45)].
- Explicit casts between classes: a rule in `CastOperation::CheckCStyleCast` (near SemaCast.cpp:3116); CodeGen calls a
  `castclass` stub, which throws InvalidCastException on a wrong class (section 1.3); `(void)s` is free [RS8, under the
  user's pick "Keep checked casts" (2026-11-20 10:19)].
- Optional in the check, `bool b = s;` meaning `s != NULL`: R-a keeps `_Bool b = r;` an error [RS6].

**Refused by a new rule:**

- `String *` and `String **`: `Sema::BuildPointerType` (SemaType.cpp:1845; WebAssembly's rule at :1879-1886) [RS10,
  RS12].
- `&s`: `Sema::CheckAddressOfOperand` (SemaExpr.cpp:15269; WebAssembly's rule at :15505-15512) [RS13].
- Varargs: `Sema::isValidVarArgType` (:967; WebAssembly's rule at :988-991); calls to unprototyped functions that pass a
  reference (C17 and earlier), which behave like varargs; `va_arg(ap, String)`, which is checked for completeness only
  (`Sema::BuildVAArgExpr`, :17677-17680) [RS19].
- Captures by blocks, by reference in lambdas and in OpenMP regions: one rule in `isVariableCapturable` (:19773), which
  `Sema::tryCaptureVariable` runs for every capture kind (:20489-20490) [RS26].
- The `cleanup` attribute, which passes `&s` (SemaDeclAttr.cpp, hooked) [RS25]; `volatile`, which promotion cannot
  handle (the path plan's rule, section 1.2) [RS23]; `__builtin_bit_cast` with a reference [RS14].
- Added by the verifier: asm operands (3.8) [RS29] and jumps past the declaration (3.6) [RS28].
- Corrected by the verifier: compound literals `(String){...}` need no rule. The check listed them as checked for
  completeness only (`Sema::BuildCompoundLiteralExpr`, :7671-7675), but the type is not scalar, so SemaInit reports
  `err_illegal_initializer_type`, an error (SemaInit.cpp:1431-1493; DiagnosticSemaKinds.td:6872), for compound literals
  and braced initializers alike; C23's `String s = {};` is refused too, which the documentation should say [RS14].

**Free** (the sizeless rules, or clang as it is): fields, unions and bit-fields; arrays and VLAs; globals, statics,
`thread_local` and `extern`; `sizeof` and `alignof`; `_Atomic` and `alignas`; casts to and from integers, `void *` and
pointers (a null pointer constant excepted, 3.7); arithmetic and ordering. The check also put asm operands here; the
verifier corrected that (3.8). **Policy rules** (R-a's, under every spelling): references in external-linkage
signatures; function-pointer types and addresses of functions with references; functions that call setjmp; MSILPrepareIR
backstops for each (section 1.4) [RS15-RS17, RS22; 5.21.7]. **Lost:** `_Nullable` and `_Nonnull` (pointers only); the
used-before-set warning, unless UninitializedValues.cpp is extended (`isTrackedVar` tracks scalars, vectors and RVV
sizeless types only, :57-66).

**Counts** (this check's reading): in C about 10 refusal rules and about 8 allow-rule sites, mostly in SemaExpr.cpp,
SemaType.cpp and SemaCast.cpp; C++ adds the `String&` refusal (`Sema::BuildReferenceType`, SemaType.cpp:1898;
WebAssembly's rule at :1956-1960), the bool, null and Object conversions, and C++ casts. The verifier's changes: asm
operands +1, compound literals -1, a jump rule +1.

### 3.6 Jumps past a declaration (5.21 RS28)

- This check: a reference local without an initializer starts as NULL; clang must store the null at the declaration
  (CGDecl), because leaving it to the backend gives silent wrong code at -O1 and above (the path verifier, section 1.2).
- The verifier's hole: C lets `goto` or `switch` jump past a declaration. Clang checks jumps in C only for VLAs,
  `cleanup`, `__block`, ObjC ARC locals and non-trivial C structs (`GetDiagForGotoScopeDecl`,
  JumpDiagnostics.cpp:144-181; the protected-scope marking in `Sema::CheckVariableDeclarationType`,
  SemaDecl.cpp:9250-9253, and in `Sema::CheckCompleteVariableDeclaration`, :15196-15205); a jump past an initialized
  local is only an off-by-default `-Wc++-compat` warning (`IsCppCompatWarning`, JumpDiagnostics.cpp:911-916). The NULL
  store is then skipped: at -O0 the value reads as NULL (the `ldnull` safety net); at -O1 and above `phi(a, undef)`
  folds to `a`, the path verifier's own wrong-code example, reached by a `goto`.
- Its options: (a) refuse jumps into a reference's scope, which fails closed (precedent: ObjC ARC strong locals and
  non-trivial C structs, JumpDiagnostics.cpp:160-172; JumpDiagnostics.cpp is not hooked, SemaDecl.cpp is); (b) also null
  every reference slot at function entry and reword the promise. It recommended (a), the user's call under prov-10.
- Outcome: Claude's decision (the K2 check's verifier), shown with references Q5 (section 6.10), which the user answered
  "OK all six" (2026-11-19 12:37): the compiler refuses a `goto` or `switch` past a reference's declaration, a loud
  rule, not a silent state. RS28: clang's jump error with the note T-R23, also for computed `goto` and `asm goto`,
  hooked in `GetDiagForGotoScopeDecl` and the protected-scope marking of `Sema::CheckVariableDeclarationType`; jumps out
  and backward stay allowed, as the declaration stores NULL again. RG3: `CodeGenFunction::getAutoVarInitKind` answers
  `Uninitialized` for references, because a computed `goto` marks every local bypassed.

### 3.7 NULL

- **What NULL is:** `((void*)0)` in C and `__null` in C++ (`0` under MinGW and MSVC, which includes the windows-msvc
  triples) (`clang/lib/Headers/__stddef_null.h:19-26`); llvm-libc (c.dll) takes NULL from that header
  (`libc/include/llvm-libc-macros/null-macro.h:12`). C23 and C++ also write `nullptr`, and C++ `0`.
- **The rule:** an expression that `Expr::isNullPointerConstant` (Expr.cpp:4110) calls null converts to any reference
  type, as a null reference. Nothing else converts: not a `void *` variable, `1`, `(void*)1` or any integer variable. As
  for C pointers, every integer constant with value 0 counts (`s = 1-1;`, `s = '\0';`): C's own rule.
- **Precedent:** OpenCL's opaque, non-pointer `queue_t` gets exactly this, in assignment
  (`Sema::CheckSingleAssignmentConstraints`, SemaExpr.cpp:10580-10585) and in comparison (`Sema::CheckCompareOperands`,
  :13683-13695).
- **Where the rule goes:** in C, one site in `Sema::CheckSingleAssignmentConstraints` (:10399), beside `queue_t`'s
  rule, covering initialization (SemaInit.cpp:8589-8597), assignment, arguments and returns [RS4]; comparisons in
  `Sema::CheckCompareOperands` (:13193), where the null side converts to the other side's class [RS5]; `c ? s : NULL` in
  `Sema::CheckConditionalOperands` (:9115), before the null diagnostic at :9286 [RS7]; explicit `(String)0` and
  `(String)NULL` in the C cast check, as OpenCL turns `0` into `event_t` (`CastOperation::CheckCStyleCast`,
  SemaCast.cpp:3163-3178) [RS8]. In C++ (R-a2): a new conversion kind from a null pointer constant, which must also
  accept `nullptr`, since `queue_t`'s conversion in `IsStandardConversion` (SemaOverload.cpp:2685-2689) tests only for
  an integer 0, and the matching step in `PerformImplicitConversion` (SemaExprCXX.cpp) [5.21.16 RN3].
- **`!s` and `if (s)`** mean "is null" and "is not null", by the rules of 3.5 [RS6].
- **CodeGen:** the null value is the IR type's zero value, the type being registered with HasZeroInit [RI1, RI5];
  `CK_NullToPointer` will not do as it is, since `ScalarExprEmitter::VisitCastExpr` casts the destination to an LLVM
  pointer type (CGExprScalar.cpp:2929-2933), so it needs a branch, like OpenCL's null value for `CK_ZeroToOCLOpaqueType`
  (:3155-3159) [RG2]; `==` and the truth test become helper calls, because LLVM forbids `icmp` on target extension types
  (section 1.2) [RG4, RI6]; a local without an initializer starts as NULL, stored by clang at the declaration (CGDecl)
  [RS2, RG3].
- The verifier: right, including the `CK_NullToPointer` catch; but "starts as NULL" fails silently after a jump past the
  declaration (3.6).
- Notes (this check): `==` on two references is identity (IL `ceq`), and two different classes need one side to be
  `System.Object`, or a cast; in C++ `f(NULL)` with overloads `f(int)` and `f(String)` picks `int`, as with pointers.

### 3.8 CodeGen, the IR type and the ABI

- **The same as the path plan** (this check): `target("msil.ref")` in `llvm/lib/IR/Type.cpp`'s `getTargetTypeInfo` with
  HasZeroInit and CanBeLocal, without CanBeGlobal, its value-type tag in `llvm/lib/CodeGen/ValueTypes.cpp` [RI1, RI2],
  and the backend, -O0 pass, backstops, stubs (one `castclass`, no GCHandle) and helpers [5.21.6-5.21.10]. What differs
  is clang CodeGen only: the cases of 3.3, the evaluation kind being scalar [RG1]; class names read from the type itself
  (`"System.String"` against CIL `string`, `"System.Object"` against `object`, `"Ns.T"` against `class Ns.T`); no
  address space anywhere, the path plan's choice (section 1.3).
- **The ABI, corrected by the verifier (in K2's favour).** The check said x86-64 SysV (the Linux host triples)
  classifies an unrecognised type as MEMORY (`X86_64ABIInfo::classify`, X86.cpp:2010), so a reference would pass through
  a copy in C memory unless one central override in CGCall.cpp (hooked) were added, under every spelling. The
  classifications are right; the conclusion is not. `X86_64ABIInfo::classifyArgumentType` sends the MEMORY class to
  `X86_64ABIInfo::getIndirectResult` (X86.cpp:2970-2978), which returns Direct with the converted IR type for any type
  that is not an aggregate for the ABI (:2423-2442); results go the same way (`X86_64ABIInfo::classifyReturnType`,
  :2836-2837, then `X86_64ABIInfo::getIndirectReturnResult`, :2388-2402). Not an aggregate means a scalar evaluation
  kind (`CodeGen::isAggregateTypeForABI`, ABIInfoImpl.cpp:96-99), which the type has. Win64
  (`WinX86_64ABIInfo::classify`, X86.cpp:3748), AArch64's non-aggregate path (`AArch64ABIInfo::classifyArgumentType`,
  AArch64.cpp:390) and the portable triples (`MSILPortableABIInfo::classifyArgumentType` through `DefaultABIInfo`,
  Targets/MSIL.cpp:80) are Direct too. So no ABI hook is needed and CGCall.cpp leaves the hooked-file list; a per-triple
  ABI test stays as the guard (the path plan's risk 6, section 1.9). The caret built on pointers is likely still classed
  INTEGER and coerced (X86.cpp:2052, not checked in depth). R-a: RG6 (`CGCall.cpp` unchanged), RV2's `ref-abi.c` on all
  ten MSIL triples.
- **Asm operands, corrected by the verifier.** The check said they come free, because MSIL refuses inline asm in CodeGen
  (CGStmt.cpp:2915; CodeGenModule.cpp:8399). MSIL refuses only non-empty inline asm (`emitMSILAsmStmt`, CGStmt.cpp:2915)
  and module asm (`CodeGenModule::EmitTopLevelDecl`, CodeGenModule.cpp:8399). An empty GNU asm statement is accepted:
  without operands it becomes a fence and a call of `llvm.sideeffect`; with operands (`asm volatile("" : : "r"(v))`, or
  `"+r"(x)`) it takes the native path (`emitMSILAsmStmt`, CGStmt.cpp:2879-2913). Sema accepts any complete operand that
  is neither `void` nor `_BitInt` (`Sema::ActOnGCCAsmStmt`, SemaStmtAsm.cpp:472-503), and an `"m"` operand passes the
  reference's address, which fails only late, in the backend. A rule is needed, modelled on `_BitInt`'s refusal
  (SemaStmtAsm.cpp:481-485, not hooked) or in CGStmt.cpp's MSIL block (hooked). R-a: RS29, T-R24 from
  `emitMSILAsmStmt`, "yet" for the inline-IL goal; SemaStmtAsm.cpp unchanged (RH6).
- **Fork-only parts:** rows in MC-1's classifiers `classifyManagedParam`, `classifyManagedResult` and
  `classifyManagedThis` (SemaMSILManagedCall.cpp:270-355), next to the handle rule `isHandleType` (:133-146); the
  conversion codes (`enum class ManagedConv`, ManagedSignature.h:1226-1242); CGMSILManagedCalls.cpp [5.21.9].
- **Not needed under K2** (this check): the `cli_type` attribute and the refusal of a pointer to a `CLI_TYPE` struct, K2
  having no class structs. Under the caret both return: `cli_type` marks the class struct (5.21.2) and `String *` is
  refused (RS10); the edit of Attr.td is in the 5.13.5 exemption row.

### 3.9 Spelling, C++, Objective-C and tools

- **Spelling** (K2's own, not in R-a): a type specifier taking a string literal, modelled on `_BitInt(N)`
  (TokenKinds.def:324; ParseDecl.cpp:4387-4396); a real keyword costs 6 more upstream files for C and 9 with C++, which
  a macro over a type attribute (as clang predefines `__declspec(a)` for MinGW, Targets.cpp:89-91) saves, at the cost of
  editing Attr.td and AttrDocs.td. This check's reading: a real keyword. The verifier: right; Parser.h may join the
  list; "that corrects the caret check's list" was wrong framing (SemaDecl.cpp:6311, in
  `RebuildDeclaratorInCurrentInstantiation`, serves a type argument, not a string); with KEYALL,
  `__is_identifier(__cli_ref)` is 0 on every target of the fork's clang, so guard with `#ifdef __MSIL__`
  (Targets/MSIL.h:129, :209). Under the caret, `^` already parses (RC1).
- **C++** (R-a2's part; the same under every spelling): templates taking references by value work
  (SemaTemplateDeduction.cpp:2553-2555, :7272-7295); class templates storing `T` fail loudly at the field rule; `T&` and
  `T&&` are refused as WebAssembly does (SemaType.cpp:1956-1960), losing `const String&` parameters, `std::move`,
  `std::function` and `[&]` captures; IL managed byrefs are deep (clang's Address requires an LLVM pointer type,
  Address.h:204-206), a later idea in this check's reading. The verifier adds: R-a's rule keeps references out of member
  functions with external linkage and every virtual function until R-b, and llvm-libc's `File` reaches its platform
  layer through function pointers (`libc/src/__support/File/file.h:57-58`, :83, :154), so P-2 holds references in locals
  and internal helpers only. R-a: 5.21.16 RN1-RN9.
- **Objective-C and tools** (this check): K2 leaves `^` to Blocks; other compilers reject the lines loudly, and a
  fallback definition for them would silently change meaning, which prov-10 allows only with the user's OK. Under the
  caret, `^` means two things in Objective-C code (Claude's reading with the pick; RC2, RC7), and `<cli/cli.h>` stops
  with `#error` off MSIL triples (RT1).

### 3.10 Costs and the upstream footprint

- **This check's estimate:** the type of its own 21-30 files, the keyword 6 (C) or 9 (C++), rules in SemaCast.cpp and
  SemaInit.cpp, CodeGen in CGExprScalar.cpp and CGDecl.cpp, LLVM core's Type.cpp and ValueTypes.cpp, hooked
  SemaDecl.cpp, SemaDeclAttr.cpp, DiagnosticSemaKinds.td and CGCall.cpp (plus Sema.cpp for C++): about 33-43 unhooked
  upstream files for C and 39-49 with C++, plus 4-5 hooked; about 35-51 with the core, roughly 1.5 to 2 times the caret
  on the pointer route (20-24 clang files with C++, about 5 hooked, plus the same 2 core files; section 2.9). Measured
  proxies: HLSLAttributedResourceType's first commit (e00e9a3f8294), 26 files, +274/-2, 21 neither HLSL-specific nor
  libclang; HLSLInlineSpirvType, named by 38 non-test files at HEAD, 30 in the fork's clang build (without 3 ClangIR
  files, 3 libclang files, lldb and its own SPIR target); WebAssembly's first externref commit (890146b19206), 47 files,
  +522/-17, 34 clang files outside tests; OverflowBehaviorType (7f631bb523a3), 82 files, +4,112/-179, 55 outside docs
  and tests.
- **Corrected by the verifier:** the low end is too low. The 21 came from a first commit that lacked cases a working
  type needs (no CodeGenTypes.cpp, MicrosoftMangle.cpp or ExprConstant.cpp in e00e9a3f8294's file list); the complete
  leaf type HLSLInlineSpirvType touches 30 files in the fork's build; HLSLAttributedResourceType is now named by 48
  non-test files and OverflowBehaviorType by 54 (`git grep -l`, the verifier's count). So about 40-45 unhooked files for
  C (46-51 with C++), and more may be needed: JumpDiagnostics.cpp, SemaStmtAsm.cpp, DiagnosticASTKinds.td (Itanium's
  "cannot yet mangle" kinds, :1067-1075) and Parser.h. The check contradicted itself on the hooked count ("only 4-5 of
  them are hooked" against "plus 4-5 hooked"), and its "35-51 in all" did not match its parts (37-54). After the ABI
  correction the hooked files are SemaDecl.cpp, SemaDeclAttr.cpp and DiagnosticSemaKinds.td, plus Sema.cpp for C++, plus
  CGStmt.cpp if the asm rule goes there.
- **Approved and planned:** the user's "(a) Yes" (2026-11-19 12:39), for the caret built as a type of its own, and R-a's
  list of 52 upstream files (RH1, RH2) are in section 6.11.
- **Rebuild and schedule** (this check's estimates): TypeNodes.td and TypeBase.h rebuild most of clang (for scale,
  MC-1's Attr.td class was about 520 objects, 15-17 minutes at 4 jobs, in MC-1's Rebuild cost paragraph of
  ImplementationPlan.md), so header edits land first; K2's clang part about 1,000-2,500 lines plus tests, adding about
  3-6 working days to R-a's 2-4 weeks. R-a: every header edit is in R-a.a, built once by R-a.0 (R-a's Header changes);
  R-a's text now gives 2.5 to 5 weeks at four CPUs (Claude's estimate).

### 3.11 Open choices, and the verifier's review

The check's seven choices, with this check's recommendations: (1) the kind of type: sizeless and its own; (2) a real
keyword, not a predefined macro; (3) the names: MC-1's TYPE grammar; (4) implicit conversions only to `System.Object`;
(5) C++ `String&` refused for now; (6) a `CLI_TYPE` struct that names a class: an error; (7) no fallback for other
compilers. The verifier (its reading) found them mostly right: (1) should list the record-per-name route and be put as
fails closed against fails open; (2) goes with the upstream approval (6-9 files); (3) waits, since the check started at
10:30, before the user's question at 10:31 and observation at 10:40 on value types (2026-11-19; section 4.1), and reads
"MC-1's grammar without `valuetype`"; (4) splits off whether a prototype may say Object where the signature says class
T, checked only at run time; (5) gains the P-2 limit (3.9); (6) is moot until `CLI_TYPE` exists; (7) is prov-10's
default. Missing: a choice on jumps (3.6). Claude's to decide and record: refusing asm operands, folding the array
aliases, `-Wswitch` errors at merges.

Where each landed: the user picked "Caret" (2026-11-19 12:31), so (2) and (7) are moot; (1) is RC3; (3) is RT2 and RT3,
with no short names (Claude's decisions); (4) is the user's pick "(a) Only to Object" (2026-11-20 10:45), and the
split-off question is settled by the user's "I think A is correct." (2026-11-20 11:44): exact classes in managed-call
declarations, not `Object ^` for any class parameter (RM2, RM12); (5) is R-a2's refusal of `String&` for now, under the
user's "(a) Yes, R-a2" (2026-11-19 12:50; RN4); (6) does not arise, a class being a `CLI_TYPE` struct (RA1); the jump
choice is RS28, the asm refusal RS29, the fold RT3 and the switch rule RH5.

## 4. The value-types check: .NET structs and the questions VT-1 to VT-9

A read-only check by an agent, 2026-11-19, at HEAD `a15f3c958572`, then an adversarial verifier; both were done at
11:38, and nothing was compiled or run. The metadata facts come from dumps of the .NET 10.0.15 and Framework 4.8
assemblies installed on the check's machine, made with the tree's `llvm-cil-dump` (the dumps are not in the tree). Sizes
and times (at two CPUs) are the check's estimates, unmeasured. "This check's reading", "Claude's proposal" and "Claude's
recommendation" are the check's; "the verifier's reading" is the adversarial verifier's. Value types are not in R-a
(5.21.16): R-a refuses `valuetype` names with T-R1 (RT2), and value-type structs will be marked `valuetype`, never
recognised by a body (RT5).

### 4.1 The user's question, and the two versions checked

- The user (2026-11-19 10:31, a question, not a pick): "But it does raise the question of how structs work, like real
  structs, value types." Claude's first sketch, **version 1**: (1) an IL value, `__cli_value("Ns.V")`, for every
  value type, in IL locals only; (2) C-memory mirrors only for public-field structs; (3) boxed values as raw GCHandle
  tickets (MC-1's form).
- The user (2026-11-19 10:40, an observation, not a pick): "What about dealing with both at the same time? A DateTime
  can be stored in C memory. It's blittable? But it can also be held as a reference to a .NET struct on the heap."
  then "I guess that's the benefit of the ^ syntax. YOu can represent both." Claude's sketch after that, **version
  2**, which the check tested: mirrors widen to every struct with no references inside (opaque bytes when its fields
  are private), boxed values become real references, and IL values stay only for structs with references inside.
- **Spelling.** The user's pick then was **K2** ("Your simpler shape I like better.", 2026-11-19 10:29): a reference is
  a typedef of a keyword, `typedef __cli_ref("System.String") String;`, so a value type needs a second name for its
  boxed form; with **the caret**, `DateTime d` is the value and `DateTime ^r` a reference to a boxed copy. Asked again
  after the check (K2 with a type form `__cli_ref(DateTime)`, Claude's recommendation, or the caret built as a type of
  its own), the user answered "Caret" (2026-11-19 12:31; section 6.7). R-a writes a reference `T ^` (5.21 RA1); the K2
  spellings below are the check's.

### 4.2 Is DateTime blittable?

This check's reading, confirmed by the verifier with the corrections marked:
- Not to the P/Invoke marshaller, whose blittable types are the integer types, IntPtr/UIntPtr, Single, Double,
  one-dimensional arrays of blittable primitives and formatted structs of blittable types; it turns DateTime into an
  OLE DATE (a double). Yes to C# ("unmanaged": no field holds a reference; the verifier adds that C# also calls
  `ValueTuple<int,long>` unmanaged, despite its auto layout). In IL, `ldobj` and `stobj` copy any struct through an
  address; with no reference inside, the copy is exact and the GC never needs to see it. So a DateTime can live in C
  memory as 8 opaque bytes, meaningful only inside the running process (VT-7).
- CoreLib 10.0.15 marks DateTime auto layout (flags 0x00102101) with one uint64 `_dateData`, internal readonly (flags
  0x0023; the check said private, and the verifier corrected it); Framework 4.8's mscorlib, auto layout with a private
  uint64 `dateData` and no IsReadOnlyAttribute; the .NET 10 reference pack's System.Runtime.dll, only a private
  placeholder `int32 _dummyPrimitive`, marked sequential (0x00100109), so a size never comes from reference
  assemblies. (Web sources, read 2026-11-19: Microsoft's .NET pages on blittable types, marshalling and unmanaged and
  nullable types.)

### 4.3 The three forms of version 2, repaired

| Form (kind) | For | C holds | IL |
|---|---|---|---|
| A (kind 2): a struct in C memory | structs with no references inside (DateTime, Guid, Vector3) | a `CLI_TYPE` struct of opaque bytes, or of fields once the layout is proven | stubs copy with `ldobj`/`stobj` through the C address; `T *self` is the method's `this`, in place |
| B (kind 3): a boxed reference | any boxable struct | a reference to a boxed copy: `__cli_ref(DateTime)` or `__cli_ref("System.DateTime")` in the check; `DateTime ^r` since "Caret" | `box`, `unbox.any`, `unbox`; no GCHandle |
| C (kind 1): an IL value | structs with references inside (CancellationToken, `KeyValuePair<string,int>`) and spans | `__cli_value("Ns.V")`, in locals, parameters and results only | an IL local of that struct type, held by the runtime |

The check's four repairs of version 2 as sketched: (1) a layout proof (or field copies) for public-field mirrors;
(2) an explicit `opaque` marker in `CLI_TYPE`; (3) readonly or copy for `const T *self`; (4) refusals for a boxed
`Nullable<T>` and boxed ref structs. Two sub-ideas would fail silently, and both are avoided: raw byte copies of
public-field mirrors with no layout proof, and naming .NET types by a per-module number inside the IR. A guard: a
C-memory form for a struct holding references, or one never checked for references, would be a GC hole.

### 4.4 Form B: a boxed value held as a reference (kind 3; R-a's "value-type form B")

**What MC-1 has, kept as built.** 5.20.3 MS5's rows (ImplementationPlan.md:9503-9508): `boxed` (a handle in, then
`unbox.any V`; out, `box V`, then a new handle), `this-val` (`unbox V`, a pointer into the box) and a value type's
`.ctor` (`newobj`, then `box`). They are Q25 (a) of the portable decision brief, "opaque boxed handles" (:9516-9517);
the plan's row of 2026-11-02 records the brief's answers as the user's (2026-11-01), as recommended except the brief's
Q5 and Q15. Each dereference does FromIntPtr, box, unbox and get_Target (:9656-9660), and each result takes a GCHandle
slot that C must free (MH2). Users: MC-1's tests (a DateTime `.ctor`, `get_Ticks` through `this-val`, a boxed result;
:9890-9891), 14 declarations in 7 test files by the no-star check's verifier (section 5.4; not re-counted by this
check's verifier), and P-2's plan (clocks and roots: :9043-9044, :9075-9077, :10110-10111).

**What version 2 changes: a boxed value is a real reference** (Claude's proposal, this check's).
- A box is an ordinary heap object, so a reference to it is an ordinary reference: in the check's K2 spelling
  `__cli_ref(DateTime) r` (VT-3 (a)) or `typedef __cli_ref("System.DateTime") DateTimeBox;`; since "Caret",
  `DateTime ^r`, the form that R-a's text calls value-type form B (5.21.16).
- Stub rows with no handle: a `valuetype V` parameter from a reference is `unbox.any V` (null gives
  NullReferenceException, a wrong type InvalidCastException); a `valuetype V` result into a reference is `box V`;
  `this` is `unbox V`, so a mutating member changes the box, which every holder shares (C++/CLI's `V^`; the type says
  "reference", so it is honest); an `object` or interface parameter takes the reference straight through, with
  `castclass` where needed. `==` compares boxes by identity, as R-a's `==` does.
- Refused: `Nullable<T>`, since boxing gives a boxed T or null, never a boxed `Nullable<T>`, so a "boxed Nullable"
  type would lie; ref structs, which cannot be boxed.
- Cost (this check's estimate): about 1,200-3,000 lines with tests, 3-6 days, including the `.conv` pseudo-member
  (4.7); it needs R-a. The verifier's reading: tight, because `.conv` is a new member kind (MC-1 has only `.ctor`),
  touching Sema, IR, stubs and msil-ld, and B lifts R-a's "reference types only" rule for type names (RA1, RT2).

**What stays.** Tickets stay raw GCHandle integers, only to keep a reference or a box in C memory, through GCHandle's
own Alloc, get_Target and Free (MC-1's `gch` rows); the user (2026-11-19 01:26): "That's just a raw GCHandle integer."
and "No reason to color it as anything else." A struct with references inside is kept in C memory as a box (B) plus a
ticket. MC-1's `boxed` and `this-val` rows stay as built, new code would use A or B, and retiring them is VT-9; R-a's
text keeps boxed parameters and results spelled with the star until value-type form B, or until the step named by the
answer to references VT-9 (5.21 RM10, RN10). Version 1 kept boxed values as tickets only, against the user's end goal
(2026-11-19 01:26, quoted in section 6.4).

### 4.5 Form A: structs in C memory (kind 2)

Claude's proposal, this check's, in brief: a value type with no references inside that is not a ref struct, as msil-ld
decides at link time from the references (reference packs mark references with a `_dummy object` placeholder and give
unmanaged structs only `int32 _dummyPrimitive`: this check's inference, which the verifier supported; without
references the call is refused, as T-M8 does today). An explicit marker keeps a misspelled mirror from becoming silent
bytes: `CLI_TYPE("opaque valuetype System.DateTime")` on 8 aligned bytes, or
`CLI_TYPE("valuetype System.Numerics.Vector3")` on `{ float X, Y, Z; }`. Stubs copy with `ldobj`/`stobj` through the C
address; `T *self` is the method's `this`, in place; `const T *self` runs in place for readonly members, else on a
copy. Every stub checks `sizeof V` and each mirror field's offset against C's and throws on a mismatch, since
``ValueTuple`2`` is auto layout with public Item1 and Item2 in CoreLib 10.0.15 and Framework 4.8 while the reference
pack calls it sequential. msil-ld checks a value type (MK3) with no references, not byref-like, and a mirror's public
fields; clang adds rows `vin`, `vout`, `this-ptr`, `this-cptr`, `vref` (a struct by value matches no row today,
SemaMSILManagedCall.cpp:291-298, :327-334) and passes these structs indirectly through a clang hook in CGCall.cpp,
since host triples squeeze small structs into integers and a stub needs one IR parameter per C parameter
(MSILManagedCalls.cpp:168-174).

**The verifier's corrections to form A:**
- Run-time checks cannot go into objects today (MSILObjectWriter.cpp:352-360; the failed-check throw,
  `StubWriter::emitCheckFailure`, is text-only, MSILManagedCalls.cpp:953-962): the named exception needs a throw with
  no message, user strings in objects, or a helper that msil-ld makes, none of it in A's estimate.
- Repair 3 has no mechanism (readonly marks are visible only to msil-ld, through references): it needs a readonly marker
  on the declaration that msil-ld verifies, or a copy every time. P-2 binds against Framework 4.8
  (ImplementationPlan.md:9700-9703), which marks nothing readonly, so every const-self call copies, as a box and an
  unbox until stubs have typed locals, against the check's "no box" for P-2. Typed stub locals are a small piece of
  their own, not part of R-v, the step that would build form C (4.10; :9658-9660 lists a `valuetype GCHandle` local as a
  later optimization).
- "Only new stub rows" understates A: MK2's walker expects one member access per stub (ImplementationPlan.md:9709-9712),
  which the offset check's `ldflda` tokens break; msil-verify.py's pass 2 must neutralize the new constructs; A needs
  `cli_type`'s value-type forms (since "Caret" R-a adds `cli_type` for classes, 5.21 RT1-RT8, so A would extend it:
  Claude's reading). Byrefs are not free: MC-1's grammar refuses byrefs (T-M2, :9407-9408) and the modreq that C#
  in-parameters on virtual members carry.
- The ABI hook needs its own cache key: `CodeGenTypes::findOrInsertCGFunctionInfo` caches by the C function type
  (CGCall.cpp:1119-1141), so a shared entry could change a native boundary's ABI silently; precedent: `X86ABIAVXLevel`
  (:1111-1115, :1126-1128). In text on Framework every form-A stub fails to JIT (`IsReferenceOrContainsReferences` is
  .NET Core only), and text cannot see a hidden non-public field inside a mirror's padding.
- The check's IL precedent, `llvm/docs/MSIL/recon/p2/judge-cost-risk-files/p3shape.il`, holds for `ldobj` only: lines
  56-57 run `ldobj` through a native int, but the `stobj` at line 59 has no destination address (a stack underflow); the
  file was only assembled by ilasm to count metadata tables (run-all.sh:66), never run or verified. The binder's
  Field-row walk it cited (BCLBinder.cpp:1013-1044) is the enum-only `getEnumUnderlyingType`; the type-kind and
  field-flag queries are `BCLBinder::getTypeKind` (:1987-1992) and `BCLBinder::getFieldFlags` (:2017-2023).

### 4.6 Form C: IL values (kind 1)

Claude's proposal, this check's, in brief:
- **clang:** a value kind on K2's type node, keyed by the canonical .NET name (a sibling type class would double the
  plumbing), spelled `typedef __cli_value("System.Threading.CancellationToken") CancellationToken;` and fenced like a
  reference (no address, no storage outside locals, no `sizeof`, no `==`); a local starts as default(T) (`initobj`).
  Ref structs (`Span<T>`) are never boxed and never in C memory; Framework 4.8's mscorlib has no Span.
- **IR:** the check proposed `target("msil.value", H1, H2)`, H1:H2 a 64-bit hash of the type text, with a table and a
  loud collision check. The verifier's correction: no hash is needed; LangRef limits only the parameters
  (LangRef.md:4884-4886), and LLVM already matches type families by name prefix (Type.cpp:1049 `spirv.`, :1085 `dx.`;
  ValueTypes.cpp:315), so `target("msil.value.<canonical .NET name>")` carries the identity exactly and unifies across
  LTO, with no table and no collision check.
- **Backend:** a pass MSILDemoteValues, the opposite of R-a's promotion, puts each value in an IL local typed
  `valuetype <name>` (a special stack ID; `TargetStackID::WasmLocal` already exists, TargetFrameLowering.h:35;
  `ldloca`, `cpobj`, `initobj`), never in C memory. The check placed it after P1, where R-a's pass then sat; R-a's
  text adds P15 in `MSILPassConfig::addISelPrepare` instead (RB10).
- **The verifier's corrections:** "no MVT entry is needed" is a risk: an unmapped target type hits `llvm_unreachable`
  in `MVT::getVT` (ValueTypes.cpp:325-327), and BasicTTI's `computeTypeLegalizationCost` allows no unknown type
  (BasicTTIImpl.h:1034-1036), while such values live through the whole middle end; map it, as R-a maps `msil.ref` to
  `externref` (5.21 RI2), or test the optimizer. And the object writer does not type signatures from its five value
  kinds only: it also takes exact narrow primitive types for managed-call stubs (ExactParams, ExactReturn,
  MSILObjectWriter.cpp:88-129), though it has no class, value-type or byref types. Closed generics need the generics
  piece: msil-ld refuses a parent that is not a TypeRef (Synthetic.cpp:484-488) and MethodSpecs (:541-546), and MC-1's
  grammar refuses generics (ManagedSignature.h:741, :822, :909).

### 4.7 Conversions between the forms

Claude's proposal (this check's). Every conversion is explicit: a declaration whose prototype shows both types, or the
interop vision's `.conv` pseudo-member; a C assignment between two forms is always a compile error.

| From → to | IL in the stub | Spelled as |
|---|---|---|
| A → B (box) | `ldarg p; ldobj V; box V` | a `.conv` declaration (the check's example, in a managed-call spelling not in the tree: `__cli_import("System.DateTime::.conv") DateTimeBox dt_box(DateTime d);`), or any member whose CIL parameter is `object` or an interface |
| B → A (unbox) | `unbox.any V`, then `stobj` through the result pointer | `.conv` (`DateTime dt_unbox(DateTimeBox r);`); a member taking `valuetype V` also accepts the box directly |
| C → B | `ldarg pv; ldobj V; box V` | `.conv`, or an `object` parameter |
| B → C | `unbox.any V; stobj` into the local | `.conv`, or a member returning V |
| B ↔ ticket | `GCHandle.Alloc` + `ToIntPtr`; `FromIntPtr` + `get_Target` | ordinary managed calls with MC-1's `gch` rule |
| A → ticket in one call | the stub boxes for `Alloc(object)` | `GCHandle.Alloc` declared with a `DateTime` parameter |
| MC-1's boxed handle → B | `FromIntPtr` + `get_Target` | it already is a ticket of a box |

Ordinary managed calls are used where one exists, after the user's words on strings (2026-11-19 00:25): "you would
call the appropriate managed function"; boxing and unboxing have no .NET member, so `.conv` covers them. Boxing
copies: the C bytes and the box are separate values, never aliases, and the types say which is which. Boxing at a
declared `object` parameter is lossless (VT-4); the verifier adds that it allocates on every call.

### 4.8 Timing, order and costs

- **None of the three forms needs M33 or M34**: their value types are made by msil-ld and the trailer, never as locals
  of compiled C (ImplementationPlan.md:5552-5553 and :6855-6860, M33.c's text sink and M34.b's `TrailerThunks.cpp`
  item), as the verifier re-checked; this corrects Claude's first answer (10:31; section 6.6), which had called IL value
  locals M33 and M34's territory.
- **A** needs no references; with A, P-2's `time()`, which reads DateTime.UtcNow ticks through a boxed handle
  (ImplementationPlan.md:9043-9044), becomes `DateTime now = DateTime_get_UtcNow();` and
  `int64_t t = DateTime_get_Ticks(&now);`, with no box, GCHandle or release. **C** pays off only with the generics
  piece, and P-2, bound against Framework 4.8 (the portable decision brief's Q30 (a)), whose mscorlib has no Span, gains
  nothing from it.
- **Claude's recommended order** (this check's): 1. R-a, with B folded in; 2. the cleanup round's milestones; 3. EH-0
  with FX-1 to FX-3, whose silent wrong-code fixes "are not pushed back further"; 4. A; 5. P-2, written on A; 6. later,
  R-v with the generics piece. **The verifier's correction:** B in or right after R-a delays EH-0's four silent
  wrong-code fixes by B's 3-6 days on top of R-a's own delay, and B needs only R-a, so it can come after EH-0; A before
  P-2 pays off only if A brings typed stub locals. **R-a's text** leaves value types, form B included, out of R-a
  (5.21.16) and orders R-a, the round's milestones, EH-0 with FX-1 to FX-3, R-a2, P-2 (5.21's intro).

| Step (this check's estimates, unmeasured, at two CPUs) | Lines with tests | Time |
|---|---|---|
| B: boxed references | 1,200-3,000 | 3-6 days |
| A: structs in C memory | 4,500-10,000 | 1-2.5 weeks |
| C = R-v: IL values | 8,000-16,000 | 2-4 weeks |
| Generics piece (for most of C's users) | 3,000-7,000 | 1-2 weeks |
| Version 1 instead (C as the main form, field-copy mirrors, no B) | 11,000-23,000 | 3-6 weeks |

Measurements before planning (this check's list, about a day of scratch IL): zeroing of struct locals holding references
without `localsinit` (MSILTargetStreamer.cpp:202) on Framework 4.8, .NET 8 and .NET 10; ILVerify and PEVerify on the new
constructs (StackByRef is allowed, `msil/utils/ilverify-allowed.txt:45`, but its comment names only ldind/stind, so a
new construct is a question for the user under the plan's D21); the JIT folding the checks; the offsets of
`ValueTuple<int,long>` and `ValueTuple<int,int>`; misaligned `ldobj`; and B's `unbox.any`, `box`, `unbox`.

### 4.9 Alternatives, fit and verdicts

**Alternatives** (this check's list): version 2, repaired (recommended); version 1, better on exactness (no size table,
any layout, one value form) but with no DateTime in C memory except as Ticks or a box and a ticket, the heaviest first
step (R-v), and GCHandles kept for boxes; IL values with explicit field copies; mirrors only (option (a) of the interop
vision's decision on structs, as written; the smallest); field-by-field copies for mirrors (VT-2's fallback); raw copies
with no proof (fatal if taken literally); layouts read from metadata (refuted by the reference pack); projections such
as DateTime as {Ticks, Kind} (lossy); spans through the stub's (pointer, length) conversion (can coexist with C).
Claude's recommendation (this check's): version 2, repaired, with VT-2 (a), K2 with `__cli_ref(T)` for the boxed form
(since replaced by "Caret"), and the order of VT-5 (a).

**Fit with the user's words** (this check's reading): `DateTime *` points at real bytes, and B and C never use `*` (the
user, 2026-11-19 01:18: "* means its a pointer. String* is in fact not a pointer to a string strut."); only an unproven
raw mirror or a "boxed Nullable" (refused) could lie about its data (01:20: "it's storing data, which is lied about by
the type."); B removes the GCHandle from boxed values (the end goal, 01:26); form A is the user's "annotate structs that
represent .NET type references, or .NET structs" (2026-11-18 20:35), though two C structs with one `CLI_TYPE` stay two C
types (C's tag rule), which the check leans to accept. Under the user's pick on the review page (2026-11-04, item
prov-10), "silent states only with my OK", the one silent state left is VT-7's; VT-8's copy option would be a second.

**Verdicts.** This check's: version 2 is the better default once repaired, needing the least new machinery for the
commonest structs (stub rows only, no new clang type, no backend locals); version 1 is better only on exactness; no
fatal flaw in the repaired shape. The verifier's: the check holds up (of 49 repository facts, 45 confirmed and 4
corrected: line numbers, one field's access flag, one malformed precedent; all 6 web facts confirmed), with no fatal
flaw in the repaired version 2; VT-1 (a) stands with its repairs (run-time checks in objects, a mechanism for const self
with typed stub locals in A, B's timing, the IR name instead of a hash, VT-7's evidence, the IL precedent) and the order
adjusted. Choices it found missing: how a failed run-time check reports in an object; whether A includes typed stub
locals; how `const T *self` is decided. Reading the user's "Okay." (2026-11-19 02:33, to references Q1, section 6.5) as
"real references next" is Claude's reading.

### 4.10 The questions VT-1 to VT-9

Each question, its options, recommendation and reason are quoted from the check word for word, except that its second
person, which addressed the user, is put in the third person in square brackets, and the interop vision's ids are
replaced by bracketed descriptions; one `<` is escaped for Markdown. The verifier's review and the status are summaries.
Status, as R-a's governance row in section 12 records it (Claude's): the questions come to the user after R-a's plan
commit; VT-3 is settled by "Caret"; VT-6 is re-cut before it is asked; the others are open.

Terms: A, B, C, K2 and the caret are in 4.1 and 4.3. R-v is the check's name for the step that builds form C, since R-a2
is R-a's C++ step (5.21.16); the check's task had called that step 'R-a2'. "The round" is the cleanup round's
milestones; EH-0 stands for EH-0 with FX-1 to FX-3, which end four silent wrong-code states (section 12's silent-states
row, item 6). The interop vision, an earlier design not in the tree, had a decision on structs, and it put all of its
fuller managed calls (generics, byrefs and spans; structs, opaque values and pseudo-members such as `.conv`) after Phase
3, as needing M28, M33, M34 and the exception model. Q25 is in 4.4; prov-10 is the user's pick on the review page
(2026-11-04), "silent states only with my OK".

**VT-1: version 2 (repaired) or version 1.** What shape do .NET structs take in C?
- (a) Version 2, repaired: structs with no references live in C memory as bytes, or as fields once the layout is
  proven (A); a boxed copy is a K2 reference (B); structs with references and spans are IL values (C, later)
- (b) Version 1: every struct is an IL value; C-memory mirrors only for public-field structs, copied field by field;
  boxed values stay tickets
- (c) [The vision's decision on structs, option (a),] as written: mirrors and opaque values, boxed values as GCHandle
  tickets ([the user's] Q25 form), no IL values
- (d) Decide after R-a
- Recommendation (Claude's, this check's): (a). Why: It matches 'A DateTime can be stored in C memory' and the end
  goal (no GCHandle for boxes). The commonest BCL structs need only stub rows, with no new compiler type. Version 1's
  form stays for the structs that must have it. No fatal flaw once repaired.
- The verifier: agrees with (a), adding run-time checks in objects, a mechanism for const self, typed stub locals
  inside A and B's corrected timing; (b) stays the exact fallback; (c) keeps a GCHandle for every box.
- Status: open. R-a's text cites the list as "references VT-1 to VT-9" (5.21.16).

**VT-2: how public-field structs are copied.** How are public-field structs such as Vector3 copied between C memory
and .NET?
- (a) Raw byte copies, after each stub proves the runtime's size and every field offset equal C's (loud on mismatch;
  the JIT should fold the check away)
- (b) Field-by-field copies: any layout works, but stubs need value-type locals (from R-v) and there is no in-place
  'this'
- (c) No field mirrors at first: every struct is opaque bytes, and fields are read through property calls
- (d) Raw copies with no proof
- Recommendation (Claude's, this check's): (a) now, (b) as the fallback once R-v exists. Why: ValueTuple`2 is auto
  layout in both runtimes, and the reference pack even calls it sequential, so unproven raw copies (d) can misread
  fields silently. (a) keeps zero-copy and in-place 'this' for Vector3-like types and fails loudly on the rest. (c) is
  the safe minimum if [the user wants] fewer moving parts.
- The verifier: (a) is sound but not needed first, since P-2 needs only opaque structs (DateTime); it suggests A's
  first slice be opaque structs only, which is (c), with mirrors once stubs have typed locals, after which (b) and (a)
  can both exist. "(b) from R-v" is wrong: typed stub locals are a small piece of their own.
- Status: open.

**VT-3: how the boxed form is spelled.** Under K2, how is the boxed form of a struct written?
- (a) __cli_ref(DateTime) r; K2's keyword also takes a CLI_TYPE C type, so one declaration serves both forms
- (b) A second typedef per value type: typedef __cli_ref("System.DateTime") DateTimeBox;
- (c) Back to the caret everywhere: DateTime ^r and String ^s (this revises [the user's] K2 pick)
- (d) The caret for value types only, beside K2 for classes (two reference spellings)
- Recommendation (Claude's, this check's): (a). Why: It gives the caret's benefit ('You can represent both',
  [the user's] words) inside [the user's] K2 pick and one keyword family. It costs only a type operand in the parser,
  and the caret check already named this form. The caret does not spell IL values either.
- The verifier: (a) is reasonable, but K2's keyword would then take either a .NET name or a C type; (b) needs no
  compiler work; (c) and (d) revise the K2 pick.
- Status: settled without being asked. On the re-asked spelling question (K2 with a type form, Claude's
  recommendation, or the caret built as a type of its own), the user picked "Caret" (2026-11-19 12:31). The boxed form
  is therefore `DateTime ^r`, option (c)'s spelling, which R-a's text calls value-type form B (5.21.16); that this
  settles VT-3 is Claude's reading.

**VT-4: how box and unbox are written.** How are box and unbox written?
- (a) The .conv pseudo-member for standalone box and unbox, plus boxing where a declaration passes a struct to an
  object or interface parameter (visible in the prototype, as C# boxes)
- (b) .conv only: every box written at the call
- (c) Compiler builtins such as __cli_box and __cli_unbox
- Recommendation (Claude's, this check's): (a). Why: Ordinary managed calls where one exists, as [the user] wanted for
  strings; .conv only where no .NET member exists. Boxing is lossless, unlike string decoding, so the declared form
  hides nothing. Builtins add compiler surface.
- The verifier: agrees with (a); boxing at a declared object parameter allocates on every call, and it shows only
  where the signature is written (a signature derived from the prototype would never box).
- Status: open.

**VT-5: when A, B and C come.** When do the three forms come?
- (a) B inside or right after R-a; A after the round and EH-0, before P-2; C (R-v) with the generics piece, when a
  client needs it
- (b) A right after R-a, before the round
- (c) A and C right after R-a (C as the task's 'R-a2')
- (d) All of it in [the vision's fuller managed calls] after Phase 3 ([where the vision put them])
- Recommendation (Claude's, this check's): (a). Why: R-a stays first ([the user's] 'Okay.'). EH-0's silent wrong-code
  fixes are not delayed further. P-2 is written on A, so its clocks need no boxes or GCHandles. Most of C's users are
  generic. None of the three needs M33 or M34. 'R-a2' already means C++ free functions, hence the name R-v.
- The verifier: (a) needs fixing: B in or right after R-a delays EH-0's four silent wrong-code fixes by B's 3-6 days,
  so either ask for the same OK under prov-10 as R-a's delay, or put B after EH-0 (it needs only R-a); and A before
  P-2 pays off only if A brings typed stub locals.
- Status: open. R-a's text leaves all three forms out of R-a (5.21.16).

**VT-6: how the IL-value form is built.** When the IL-value form (C) comes, how is it built?
- (a) K2's node with a value kind, __cli_value("Ns.V"); values moved into typed IL locals before instruction
  selection; the keyword reserved in R-a's keyword edit
- (b) A separate sibling type class
- (c) No IL-value form: structs with references only boxed (B), spans only through the (pointer, length) stub
  conversion
- Recommendation (Claude's, this check's): (a). Why: One node keeps one set of type plumbing. IL locals avoid a
  per-register type table and leave struct optimization to the JIT. Reserving the keyword early saves a second near-full
  clang rebuild. (c) is the fallback if [the user wants] no IL values at all.
- The verifier: agrees with (a), with two changes: the .NET name in the IR type's name instead of a hash, and an MVT
  tag or an optimizer test, so cost queries cannot hit `llvm_unreachable`; reserve `__cli_value` in R-a's keyword
  edit only if VT-6 is answered before then.
- Status: re-cut before it is asked. A reviewer of R-a's draft read that (a) builds on K2's node, which "Caret"
  replaced; R-a builds the caret as a type class of its own (5.21 RA5) and its text adds no keyword, so (a)'s keyword
  edit does not exist (Claude's reading).

**VT-7: the opaque-bytes rule.** Silent state for [the user's] OK (prov-10): the bytes of an opaque struct mean nothing
outside the running process. Accept it as a documented rule?
- (a) OK, as a documented rule: writing those bytes to a file, or reading them in another process or runtime, is
  undefined; use Ticks, ToBinary or a string instead
- (b) Not OK: opaque structs stay out (only proven field mirrors, boxes and IL values)
- Recommendation (Claude's, this check's): (a). Why: No compiler or linker can see a memcpy or an fwrite. It is real,
  not theoretical: Framework 4.8 and .NET 10 lay out Decimal and DateTimeOffset differently. Refusing it gives up
  DateTime, Guid and Decimal in C memory.
- The verifier: the rule is needed, but its evidence is half wrong: Decimal's bytes are the same on both runtimes (both
  sequential; in little-endian order .NET 10's `_lo64` holds lo then mid, where Framework keeps its lo and mid
  fields); only DateTimeOffset differs (an int16 offset against an int32, both auto layout). The rule should also say
  that bytes .NET did not make, written by C or left uninitialized, are undefined except all zeros (the default
  value), and that comparing bytes is not .NET equality (DateTime's `==` ignores Kind).
- Status: open; it is not among the silent states of section 12's silent-states row.

**VT-8: mutating members on IL values.** For IL values (C), what happens when a member that may mutate is called
through a by-value 'this'? (This can wait until R-v.)
- (a) Refuse it unless the reference marks the member or the type readonly. Loud; Framework 4.8 carries no such
  marks, so there only static members and fields work
- (b) Run it on a copy: C's by-value meaning, but silent (List\<T>.Enumerator.MoveNext would never advance)
- (c) Design a by-reference 'this' form later (C++'s T& in the C++ phase)
- Recommendation (Claude's, this check's): (a) now, (c) later. Why: Prov-10 wants loud failure. (b) is a silent state
  that needs [the user's] OK. Form A already has in-place calls through a real C pointer.
- The verifier: agrees, and it can wait; (a) is a refusal in msil-ld at link time, with no change to the stub, and on
  Framework it refuses nearly every instance member, which matters little, since form C is mostly for .NET's Span.
- Status: open.

**VT-9: MC-1's boxed handles.** What happens to MC-1's boxed handle rows (boxed, this-val), [the user's] Q25 pick?
- (a) Keep them as built; new code uses A and B; reconsider at the end-of-project cleanup
- (b) Retire them once B exists (rewriting 14 declarations in 7 test files, and P-2's plan text)
- (c) Retire them now, in the cleanup round's respelling
- Recommendation (Claude's, this check's): (a). Why: They work, P-2's plan names them, and a ticket that holds a box is
  just a raw GCHandle integer, as [the user] put it. Retiring them changes [the user's] Q25 pick, so it waits for
  [the user's] word.
- The verifier: agrees with (a), with one caveat: the boxed rows also accept `void *` and pointers to incomplete
  structs (`isHandleType`, SemaMSILManagedCall.cpp:133-146), which the user's star direction calls a lie, while a
  ticket is `intptr_t` only; VT-9 should say whether "as built" narrows them to integer tickets or leaves that to the
  respelling.
- Status: open. Since the check, the user picked "(b) Retire before P-2" (2026-11-20 14:24) for MC-1's star-spelled
  handles: R-a2 makes them `intptr_t` or `uintptr_t`, except boxed parameters and results, which keep the star until
  value-type form B, or until the step named by the answer to references VT-9 (5.21 RM10, RN10). By RM10's reading
  (Claude's), those are the boxed parameters and the boxed results of members other than `.ctor`, where an integer
  type means an enum, so they cannot take an integer ticket; a value type's `.ctor` result already takes `intptr_t` and
  moves with the rest.

## 5. The no-star check: tickets and MC-1's star-spelled handles

A read-only check by an agent, 2026-11-19, at HEAD `a15f3c958572`, then an adversarial verifier the same day; nothing
was compiled. "This check's reading" and "the verifier's reading" mark their conclusions, and neither decided anything.
The counts come from the tree's 947 managed-call (`msil_method`) declarations as classified at that HEAD, with typedefs
resolved from each file as git shows it there; call sites were not counted. The check's counting script and the
verifier's independent recount are not in the tree. For this copy Claude re-ran both data sets, the strings count of
section 5.4 and the `git grep` of section 5.3: every count in sections 5.3 and 5.4 reproduces.

### 5.1 What was checked, and the outcome

The check tested Claude's reading of the user's star direction, which no one had reviewed yet:

- The user (2026-11-19 01:18, a direction): "* means its a pointer. String* is in fact not a pointer to a string
  strut.", rephrased by the user at 01:20, while the check ran (section 6.4).
- The design checked: a `*` always means a real pointer into C memory (`char *` for C text, `Vector3 *` for a copy of
  a .NET struct). A .NET class is a `CLI_TYPE` struct with any C name. A *reference* is that struct written without a
  star (then spelled `String s`; the user later picked the caret, `T ^r`: "Caret", 2026-11-19 12:31). A *ticket* keeps
  an object alive in C memory, either **typed**, a one-field struct passed by value
  (`typedef struct CLI_TYPE("handle System.String") { intptr_t id; } String_h;`), or **raw**, a plain `intptr_t`.
  `String *` has no meaning, so it is refused; today MC-1 would take it as a handle (`isHandleType`,
  `clang/lib/Sema/SemaMSILManagedCall.cpp:143`).
- By this check's reading, the star direction closes H-star, the references walk's earlier design, in which a ticket
  was written `String *` and `*h` read its object, and every other option that stars something that is not a pointer.
  No-star's reference half repeated H-star's value form, which the H-star check had covered; the caret, K2 and path
  checks in other sections of this copy carry the reference design that R-a builds.
- While the check ran, the user added directions and a question, not picks (01:23 and 01:26; section 6.4), among them
  "That's just a raw GCHandle integer."

Outcome:

- **No fatal flaw**, by this check's reading and the verifier's. The verifier checked the check's 52 facts: 47
  confirmed, 5 corrected, none refuted. The ABI claims hold on every triple for plain C structs, and the five web facts
  match their pages.
- **Tickets stay raw.** The verifier read the user's later words as settling the ticket half (directions, not picks):
  tickets stay MC-1's plain `intptr_t`, so typed tickets drop out unless the user reopens them, and with them the
  msil64/msil32 trap of section 5.2. 5.21 RA3 carries this: a ticket is a raw GCHandle number in an `intptr_t`, and R-a
  adds nothing for tickets.
- **The verifier's recommendation:** read the user's words as no-star with plain-number tickets; build no typed tickets
  and no msil64 ABI rule; add no work now to retire the star handles, and ask the user first (asked later, section 5.6).

### 5.2 Tickets by value through MC-1

How clang passes `struct { intptr_t id; }` (this check's reading; the verifier confirmed it for plain C structs):

- x86-64 Linux, glibc and musl (SysV): an `i64` in a register, both ways (`clang/lib/CodeGen/Targets/X86.cpp:2240-2251`,
  `:2697-2733`; arguments `:2987-2991`, results `:2841-2842`).
- x86-64 Windows, msvc and MinGW (Win64): an 8-byte record is coerced to an `i64`, both ways (`X86.cpp:3662-3670`,
  `:3783-3785`). AArch64 Linux and Windows: an `i64`, both ways (`clang/lib/CodeGen/Targets/AArch64.cpp:498-548`,
  `:614-625`). i686 Windows (phase 5): the argument is expanded to an `i32`; the result comes back in a register
  (`X86.cpp:951-957`, `:551`).
- msil64 and msil32: a parameter is a pointer to a copy made by the caller (`byval`), and a result comes back through
  a hidden pointer (`sret`): `MSILPortableABIInfo` (`clang/lib/CodeGen/Targets/MSIL.cpp:64-93`) hands records to
  `DefaultABIInfo` (`clang/lib/CodeGen/ABIInfoImpl.cpp:17-28`, `:47-52`).

What MC-1 does with it:

- Sema refuses it today: a struct is not a handle type (`isHandleType`, `SemaMSILManagedCall.cpp:137-146`), so it is
  T-M3, and adding tickets would be purely additive.
- **The portable trap.** A ticket parameter's IR is a pointer to the copy. The fit check accepts any pointer as a
  handle (`isManagedHandleType` and `fitsManagedConversion`, `llvm/include/llvm/CLI/ManagedCallIR.h:91-95`,
  `:150-156`), and `MSIL::parseManagedCall` reads only `signext` and `zeroext`
  (`llvm/lib/Target/MSIL/MSILManagedCalls.cpp:251-278`). The stub would use the copy's address as the GCHandle number,
  with no error at compile time. A result fails loudly: `sret` adds a parameter, so the count check reports invalid
  attributes (`MSILManagedCalls.cpp:164-174`; msil-ld does the same, `lld/MSIL/ManagedCalls.cpp:1888-1935`).

The verifier's corrections:

- **C++ classes passed by a plain pointer.** A C++ record that is not trivially copyable or destructible is passed as a
  plain pointer to a temporary, with no `byval` mark, on every triple (`X86.cpp:2246-2249`; `MSIL.cpp:70-72`), and that
  pointer fits `isManagedHandleType`. The check had presented the hidden-pointer problem as Windows-only, and its
  backstop, which refuses `byval` and `sret` pointers, would miss this case; only a Sema rule (a trivially copyable
  plain struct) stops it. Results stay loud.
- **Where such a backstop goes.** `isManagedHandleType` and `fitsManagedConversion` see only an IR type, never `byval`
  or `sret`. The check belongs where the function's attributes are visible: `MSIL::parseManagedCall`
  (`MSILManagedCalls.cpp:251-264`) and lld's `isValidManagedCall` (`lld/MSIL/ManagedCalls.cpp:1873-1936`), or a new
  shared helper that takes the function.

Both matter only if typed tickets come back. Raw tickets need none of this work: `intptr_t` is already a handle type and
a `native int` on every triple. Their one gap is boxed values (section 5.5).

### 5.3 MC-1's handles spelled with a star: the counts

Of the 947 declarations, 610 compile, 335 are error tests, and 2 have signatures that do not parse. A *place* is one of
MC-1's handle positions (5.20.3 MS5), as the check's script defines them: the `this` handle of an instance member other
than `.ctor`; an object, class, array or `string` result; an object, class or array parameter; a `string` parameter
whose C type is not a character pointer; a `GCHandle` parameter or result (`gch`); a non-integer C type at another
`valuetype` parameter or result (`boxed`); and a `.ctor`'s constructed object.

| Figure | This check | The verifier |
|---|---|---|
| Compiling declarations that use a handle | 273 of 610, at 340 places | the same |
| Spelled with a star | 268: 312 places through a pointer to an incomplete struct, through the typedefs `mc_handle` (241 places) and `dn_handle` (71); 21 places through `void *`, in 20 declarations in 3 test files | the same |
| Only `intptr_t` or `uintptr_t` | 5 declarations, "7 places" | 5 declarations at 5 places; the 7 `intptr_t`/`uintptr_t` places include 2 declarations that mix them with `void *` (`clang/test/CodeGen/MSIL/msil-method.c:207`, `clang/test/Sema/attr-msil-method.c:499`) |
| Where the handle sits | 135 declarations have a `this` handle, 128 a handle result, 65 a handle parameter; 171 pass or return a handle, not counting `this`; 102 have only a `this` handle | the same |
| Places by conversion row | `obj` 161, `this-obj` 61, `this-val` 44, `boxed` 38, `this-gch` 30, `gch` 6 | the same |
| Boxed values | 32 declarations pass or return one (38 places); 74 counting `this-val` | 14 declarations in 7 test files cannot become plain numbers: 20 places; 6 declarations have boxed parameters and 10 boxed results of members other than `.ctor`, 2 of them both. The other 18 are value-type `.ctor` results, which already take `intptr_t` (section 5.5) |
| GCHandle rows | 36 declarations, 24 of them `GCHandle::Free` | the same |
| Files | 32: `msil/test` holds 191 declarations, `clang/test/Sema` 36, `clang/test/CodeGen` 21, `lld/test` 12, `llvm/docs` 8, `clang/test/SemaCXX` 4, `AttrDocs.td` 1; 269 are C and 4 C++ | the same |
| Error tests | 52 have handle places, 35 of them spelled with a star | 51 and 36 by another reading: `int32[,]` is not a handle place (`clang/test/Sema/attr-msil-method.c:635`), and `const char *` at an object place counts as a star (`:702`, `:707`) |
| Lines naming `mc_handle` or `dn_handle` | 480 | 551 lines in 35 files; the 480 are those in the 32 declaration files. `lld/test/MSIL/managed-calls.test`, `managed-calls-lto.test` and `managed-calls-checks.test` add 71 lines (C prototypes in the comments of IR tests) |
| Lines defining them | about 37 | 37: 24 `mc_handle`, 13 `dn_handle`, all pointers to an opaque struct |

The 3 test files with `void *` handles are `clang/test/CodeGen/MSIL/msil-method.c`, `clang/test/Sema/attr-msil-method.c`
and `clang/test/SemaCXX/attr-msil-method.cpp`. An earlier rough count had put the declarations that pass or return a
handle at "roughly 230 to 245" of the 610. The check said the difference was only whether `this` is counted; the
verifier: that does not hold (171 without `this`, 273 with it), and 273 less the 36 GCHandle-row declarations is 237,
which may be what the rough counts left out.

### 5.4 The counts R-a's text uses

- **268 of the 273** compiling declarations that use a handle (as `this`, a parameter or the result) spell it with a
  star (5.21 RN10; section 12's retirement row). The check and the verifier agree.
- **303 with the error tests** (RN10; the retirement row): 268 + 35, by this check's definitions. By the verifier's
  reading of the error tests (36 of 51) it is 304.
- **The typedef names on 551 lines in 35 files** (RN10; the retirement row): the verifier's figure, which replaces the
  check's 480 lines in 32 files.
- **14 declarations in 7 test files** keep the star (RM10, RN10; the retirement row): the verifier's figure, which
  replaces the check's 32. The 7 files, from the verifier's recount data: `clang/test/CodeGen/MSIL/msil-method.c` (2
  declarations), `clang/test/Sema/attr-msil-method.c` (2), and in `msil/test/M3/13-managed-calls/`: `field-read.test`
  (2), `null-handles.test` (1), `static-value-members.test` (3), `text-checks.test` (1), `value-types.test` (3). RM10
  reads the count as covering boxed parameters and the boxed results of members other than `.ctor`; a value type's
  `.ctor` result moves with the rest (Claude's reading).
- **94 declarations** rewritten when MC-1's string rows S1-S3 go (RM13, RN10; the strings row's "94 of 947"): not this
  check's count, but the strings count shown with references Q4 (section 6.9). 94 of the 947 declarations pass a C
  string for a .NET `string` parameter: 84 of the 610 that compile, plus 10 error tests. The compiling ones hold 88 such
  parameters (68 UTF-8: `const char *` 64, `char *` 3, `const char8_t *` 1; `const wchar_t *` 9, `const char32_t *` 6,
  `const char16_t *` 5); the error tests add 10 `const char *`. This check left that cost out, and the verifier added
  it: the user's strings lean, then not a pick, implies removing S1-S3 (`utf8`, `utf16`, `utf32`) at some point, with
  their tests and docs. The user later confirmed the lean ("Confirm", 2026-11-19 12:34) and picked when they go: "(a)
  Before P-2" (2026-11-20 12:04).

### 5.5 `intptr_t` as the untyped form, and the boxed exception

MS5's handle type is `void *`, `intptr_t`, `uintptr_t` or a pointer to an incomplete struct type
(`llvm/docs/MSIL/ImplementationPlan.md:9484-9486`; `isHandleType`, `SemaMSILManagedCall.cpp:137-146`). Can `intptr_t`
carry every handle honestly? This check's reading, with the verifier's corrections:

- **Yes for the `obj`, `this-obj`, `this-val`, `gch` and `this-gch` rows:** it is an integer, and it says so.
- **Not for boxed parameters and results.** At a `valuetype` place other than `GCHandle`, an integer C type is the enum
  conversion: `classifyManagedParam` (`SemaMSILManagedCall.cpp:291-297`) and `classifyManagedResult` (`:327-333`);
  msil-ld then refuses it for a struct (T-M7, T-M8). `llvm/docs/MSIL/GettingStarted.md:1250-1252` tells users to declare
  a boxed struct parameter, result or field as `void *` or a pointer to an incomplete struct only. Boxed values need
  their own spelling, or must wait for the by-value .NET structs of the interop vision's fuller managed calls; R-a's
  text keeps their star until value-type form B (`DateTime ^r`) or the step named by the answer to references VT-9 (5.21
  RM10).
- **The verifier's correction:** the enum rule holds only for parameters and for results of members other than
  `.ctor`. A value type's `.ctor` result skips the enum row (`!IsCtor`, `:330`) and takes `intptr_t` as `boxed`
  (`:330-333`), and a value type's `this` takes any handle type as `this-val` (`classifyManagedThis`, `:349-355`);
  `GettingStarted.md:1252-1253` says both. So 18 of the check's 32 boxed declarations can move to `intptr_t`, and 14
  cannot.
- **It always needs a written signature**, since an integer carries no class to derive one from. All 947 declarations
  already write theirs.
- **It is loose.** `isIntPtrType` (`:113-120`) matches by canonical type, so a count passed by mistake can compile. The
  check said `long` and `long long` pass on "every 64-bit MSIL triple"; the verifier corrected where: `long` counts
  only where `intptr_t` is `long` (the Linux host triples and both portable triples; on msil32 that `long` is 32 bits),
  `long long` only where `intptr_t` is `long long` (the Windows and MinGW triples), and `size_t` and `ptrdiff_t` on
  every listed triple. On msil64, `int64_t` is `long long` and is not a handle type
  (`clang/lib/Basic/Targets/MSIL.h:182-186`).

### 5.6 When the star-spelled handles go

- **This check's options.** The cleanup round (the vision's first stage), which renames `msil_method` to `__cli_import`,
  could respell them as raw tickets, since MC-1 already takes `intptr_t`: about 37 typedef lines (such as
  `typedef intptr_t mc_handle;`) and the 21 `void *` places. Its costs: boxed values cannot move (the check said 32
  declarations; the verifier, 14), and expected test text changes (T-M3 prints the typedef's "aka" type, and CodeGen
  check lines would see `i64` instead of `ptr`). Or the interop vision's later stage that brings `cli_type` could retire
  them with a refusal in the fork's own Sema file and respelled tests; this check recommended that later stage, once
  boxed values have a spelling.
- **The verifier's corrections.** Retiring today's star handles is the check's own idea, not the user's; the user's "We
  dont care about anything except getting to that" argues against work off the path to references. The minimal step:
  when `CLI_TYPE` arrives, refuse a star on annotated class structs, and keep MC-1's untyped star handles
  (`struct mc_object *`, `void *`) as a recorded temporary workaround until references replace them; when they go was
  the user's call. And the check's claim that the user had kept the cleanup round to renames was wrong: a round that
  only respells is Claude's recommendation in the interop vision, not a pick of the user's; the user's pick there was
  "(B) Cesium + signature" (2026-11-18 15:12), Cesium's `__cli_import` with an optional signature. The verifier also
  corrected the order of the questions: Claude had recorded that the shortest path to real references is checked before
  the reference question is asked again, and the check had put that question after it.
- **What R-a does.** R-a takes the verifier's minimal step: `String *` and the class struct by value are refused (5.21
  RS10, RS11), since otherwise `isHandleType` takes a `String *` (one formed by `extern String g; &g`, for example), and
  MC-1's star handles stay in R-a as a recorded temporary workaround (RM10). The step that retires them is the user's
  pick "(b) Retire before P-2" (2026-11-20 14:24): in R-a2, directly before P-2, they become `intptr_t` or `uintptr_t`
  (RM10, RN10), except boxed parameters and results (section 5.4's 14 declarations), which keep the star until
  value-type form B or the step named by the answer to references VT-9. Until then new code, P-2 included, also writes a
  boxed parameter or result with the star (Claude's reading; section 12's `*` row).

### 5.7 Silent states

The check went through the references walk's list of silent states as it stood then, eight states, for both ticket
forms:

1. **A forgotten release leaks silently** (MH6), also past a `longjmp` or a .NET exception. Remains in both. Still
   there: `warn_unused_result`, CLI_AUTO (a release at the end of a scope through `cleanup`), RAII and the
   checked-handle mode. Lost: the analyzer's ownership checks, since `ownership_returns` and `ownership_takes` take
   only pointers (`clang/lib/Sema/SemaDeclAttr.cpp:1592-1598`, `:1630-1646`).
2. **Use after release**, and a reused slot that holds an object of the same type, go unnoticed (MH4). Remains in
   both.
3. **`cli_release(NULL)` does nothing.** This check: NULL cannot be passed in either form, and what a release does
   with `{0}` or `0` is the user's choice: ignore it, as `free` does (silent, so it needs the user's OK), or throw, as
   MC-1's `GCHandle::Free` of 0 does today (`GettingStarted.md:1262-1263`). The verifier: "NULL cannot be passed"
   holds only in C; in C++ `NULL` is an integer, so `cli_release(NULL)` and `intptr_t t = NULL` compile.
4. **`==` compares numbers, not objects.** Gone with typed tickets (it does not compile); remains with raw tickets.
5. **A `void *` becomes a handle silently.** This check: gone in both, since a struct has no conversion from `void *`
   and pointer to integer is an error by default (`clang/include/clang/Basic/DiagnosticSemaKinds.td:9236-9238`). The
   verifier: with raw tickets it is closed in C only by default, as a `DefaultError` warning that `-Wno-int-conversion`
   silences.
6. **A pinned view of a string** can be used after unpinning, or written through. Remains in both: a pin gives a real
   pointer.
7. **Handles and C pointers mix.** This check: mostly gone in both; `free(t)` and `cli_release` on C text do not
   compile, since integer to pointer is an error too (`DiagnosticSemaKinds.td:9256-9258`), and CLI_AUTO stays safe
   only with a typed cleanup function. The verifier: with raw tickets this too holds in C only by default; and a
   cleanup function taking `intptr_t *` accepts only locals of `intptr_t`'s own type (`long` or `ptrdiff_t` on LP64,
   `long long` on Windows), not any integer local as the check had said, because `Sema::ActOnCleanupAttr`
   (`SemaDeclAttr.cpp:9110-9135`) refuses an incompatible pointer (`Sema::IsAssignConvertCompatible`,
   `clang/include/clang/Sema/Sema.h:8213-8221`).
8. **H-star's `*h` on NULL or on an empty slot.** Gone: there is no `*h`. Reading a ticket's object comes back at the
   reference step, with the H-star verifier's lesson: `GCHandle.Alloc` of a null object makes a live handle whose slot
   holds null, so every helper that makes a ticket must turn null into 0.

New states:

- **Typed: the portable ABI trap** (section 5.2), an implementation trap rather than a language state; it went with
  typed tickets.
- **Raw: any integer counts as a ticket.** A count or a size passes at compile time; when the call runs, a garbage
  number crashes, or on .NET 8 and 10 can read as null (`GettingStarted.md:1264-1266`). MC-1's `intptr_t` handles
  have this today.
- **Typed: `.id` is open to anyone.** `(Stream_h){s.id}` makes a ticket of another class, visibly; the stub's
  `castclass` catches it where the CIL type names a class, not at `object` parameters or at release.

What became of the list:

- This check suggested that 5, 7 and 8 close, 4 stays with raw tickets, and 3 is still a choice. The verifier: 5 and 7
  close in C only by default.
- References Q5 then put six states to the user, rewritten for raw tickets, real references and the caret, under the
  user's pick on the review page (2026-11-04, item prov-10), "silent states only with my OK"; the user answered "OK all
  six" (2026-11-19 12:37). Section 12's silent-states row records the six, and section 6.10 lists them as asked; item 4
  carries the verifier's caveat (an error only by default in C, and `NULL` an integer in C++). `cli_release(NULL)` and
  `*h` had gone when the questions were re-cut after this check: a ticket is released through .NET's own
  `GCHandle.Free`, which throws on 0.
- The raw-ticket state is in the same row, by the user's pick "(a) OK as undefined" (2026-11-20 17:04): a nonzero
  number passed where a ticket is expected that was never a live ticket (never made by `GCHandle.Alloc` and
  `ToIntPtr`) is undefined, as a wild pointer is in C; 0 stays null (MH1); Status.md documents it beside item (2), and
  the compiler does not change. R-a.n writes items (1)-(5), item (6)'s time bound and this case into Status.md.

### 5.8 Typing, ergonomics, fit and costs, in brief

**Typing** (this check's reading). Typed tickets give one struct type per class: a `Stream_h` passed where a `String_h`
is expected is a compile error, and C has no cast between struct types (C11 6.5.4). Two C names for one class are two
types, since even C23's rule needs the same tag and tagless structs never match (`ASTContext::mergeTagDefinitions`,
`clang/lib/AST/ASTContext.cpp:11873-11893`); a "same `CLI_TYPE` and kind means one type" rule would be one hook in
record merging (`:12238-12240`). The verifier: one hook is not enough, because C's conditional operator compares record
types with `hasSameUnqualifiedType` (`Sema::CheckConditionalOperands`, `clang/lib/Sema/SemaExpr.cpp:9218-9221`), so
`c ? a : b` across two names still fails, loudly. Generic helpers (`cli_release`, `cli_same`, CLI_AUTO) fall back to
`.id` or to per-class `overloadable` declarations. On upcasts the check said that a derived ticket passed for a base
class is refused or left to the stub's `castclass`, which "fails loudly"; the verifier corrected this: a derived object
passes `castclass` for its base, and `castclass` fails only for an unrelated class. Raw tickets carry no class in the C
type: a mix-up shows when the call runs (`castclass` throws `InvalidCastException`), not at `object` parameters and not
at release; reading a ticket's object names its target at the call (`cli_get(String, t)`), and places that hold
objects keep written signatures until references exist.

**Ergonomics.** Typed `a == b` does not compile (C11 6.5.9); raw `a == b` compares slot numbers. Typed tickets have no
NULL (a null result is `{0}`, MH2); with raw tickets `0` works and `NULL` is an error by default in C. A copy shares one
slot in both forms and is released once. Both ticket forms keep the "used before set" warning, which tracks scalars and
records that have fields (`clang/lib/Analysis/UninitializedValues.cpp:40-68`). Both lose `_Nullable` and `_Nonnull`,
errors on non-pointers (`DiagnosticSemaKinds.td:13269-13270`); the verifier noted that in C++ a class marked with the
class-level `_Nullable` keyword accepts them. Clang's analyzer tracks integer handles only for Fuchsia's `zx_handle_t`
(`clang/lib/StaticAnalyzer/Checkers/FuchsiaHandleChecker.cpp:111`). The verifier's smaller corrections: "a small owning
class, move-only, where a copy calls `cli_dup`" contradicts itself; P-2's `DotnetFile` member working "at
[the vision's `cli_type` stage], in C++, today" is true only for raw tickets, since typed ones get T-M3 today; and
refusing C++ `String&` outright is a cost, not a simplification, because it also refuses `[&]` lambda captures of such a
local, range-for by reference, structured bindings and `std::forward` (5.21 RN4 refuses `String&` and lambda captures
for now, as approved).

**Fit with the user's words** (this check's reading). No-star meets the star direction exactly, and it reaches MC-1
itself (the 268 declarations). Typed tickets go against "The "ticket type" doesn't need to be a type." and "No reason
to color it as anything else."; raw tickets meet both. The check's side idea that completeness could decide a
`CLI_TYPE` struct's kind is unsafe, the verifier found: complete annotated structs also declare copies of .NET
structs, so with `CLI_TYPE("Ns.T")`, which has no value-type word, Sema cannot tell a boxed ticket of V from a copy of
V. Both are one `i64`, and a wrong guess would silently read a GCHandle number as struct data.

**Costs** (this check's estimates from reading the code, not measurements). Raw tickets need no new compiler work. Typed
tickets would need `cli_type` with a ticket kind (`Attr.td`, `AttrDocs.td`, `SemaDeclAttr.cpp`, all hooked), classifier,
shape and class checks in `SemaMSILManagedCall.cpp`, the portable ABI rule in `Targets/MSIL.cpp`, the loud IR check
(fork-only files), and refusals of `String *`, a class struct by value and static or extern class objects in
`SemaDecl.cpp` (hooked) or `SemaType.cpp` (not hooked). The reference step costs the same as H-star's value form. The
verifier added the missing cost of removing S1-S3 (section 5.4).

**Prior art** (fetched 2026-11-19; the verifier matched the five web facts to their pages). wit-bindgen's C bindings
wrap an imported resource's owning and borrowing handles each in a struct around one `int32_t __handle;` field,
released by `*_drop_own`: exactly the typed ticket. .NET's `GCHandle` is a struct with explicit conversions to and
from `IntPtr`; `GCHandle<T>` exists only from .NET 10, so stubs that must run on .NET Framework 4.8 and .NET 8 cannot
use it. Vulkan's non-dispatchable handle is a pointer to an incomplete struct with 64-bit pointers turned on, and a
plain `uint64_t` otherwise. In Win32, `HANDLE` is a void pointer and `HWND` a `HANDLE`, and `STRICT` makes the specific
handle types incompatible with each other.

## 6. The references walk: questions and answers

This section records the questions on real .NET references in C that were put to the user on 2026-11-18 and 2026-11-19,
one per message with a written briefing (not in the repository), and the user's answers. R-a's text (contract 5.21 and
the section 12 rows of R-a's plan commit) cites them as "references Q1" to "references Q5", by the briefing's final
numbers (6.1). Options are given as presented, shortened only where long. The checks are digested in sections 1 to 5;
`zinit.il`, the runtime check that Q1 asked for, is section 7. The labels below (direction, lean, question, observation,
pick) and the times come from the walk's own record, which is not in the repository either; "the record" below means
that record.

Terms, as the briefing used them: a *handle* (GCHandle) is a slot in a .NET table that keeps one object alive, C
holding the slot's number, which is how MC-1 gives C every object (plan 5.20.9, MH1-MH6); a *ticket* is a raw GCHandle
number in an `intptr_t`, used only to keep an object in C memory; an *IL reference* is what C# holds in a variable of
a class type, tracked by the garbage collector and never in C memory; *C memory* is globals, `malloc` memory, struct
fields, arrays and every local whose address is taken (at -O0, every local); *P-2* is the C library's OS layer, C++
code in llvm-libc and the first code written on managed calls; *messages 1 to 7* are the user's direction messages of
2026-11-18 (6.2), numbered as the briefing numbered them.

### 6.1 Questions and answers at a glance

The briefing first planned four questions (what `String *` and `String` mean; two names for one `CLI_TYPE`; strings;
silent states). After the user's answers to the first one it was re-cut (2026-11-19 02:26) into the five questions
below, which R-a's text cites by these numbers. All dates are 2026-11-19.

| Question | Presented | The user's answer, word for word | Answered | Outcome |
|---|---|---|---|---|
| Question 1 as first asked: what `String *` and `String` mean (6.4) | 00:29 | not picked: "I don't understand the question." (01:14), then the objection to `*` (01:18, 01:20) | - | re-cut into Q1 and Q2 |
| Q1: when real references come (6.5) | 02:27 | "Okay." | 02:33 | Claude's reading: option (A), R-a next |
| Q2: how a real reference is written (6.6) | after Q1 | "Let's go with C, the carrot."; then "Wait, hold on." and "Your simpler shape I like better." | 10:27; 10:29 | the caret, then K2; both superseded at 12:31 |
| Q2, asked again (6.7) | 11:38 | "Caret" | 12:31 | `T ^r`, built as a type of its own (Claude's reading) |
| Q3: one `CLI_TYPE`, two names (6.8) | 02:27; rewritten 10:27 | withdrawn | 10:29 | one type by construction |
| Q4: strings (6.9) | 12:31 | "Confirm" | 12:34 | the user's lean, with the optional convenience header |
| Q5: silent states (6.10) | 12:34 | "OK all six" | 12:37 | six silent states OK'd; one loud rule shown with them |
| Approval 1: the upstream footprint (6.11) | 12:37 | "(a) Yes" | 12:39 | about 40 to 45 more upstream files for C |
| Approval 2: Design.md's texts (6.12) | 12:39 | "Approve" | 12:49 | lines 27 and 125 as drafted |
| Approval 3: C only first (6.13) | 12:49 | "(a) Yes, R-a2" | 12:50 | C++ follows as R-a2, before P-2 |

### 6.2 The user's directions and questions, 2026-11-18

These came while the first part (presented at 19:57) of a far-future C/C++ and .NET interop design was being shown
to the user. The record labels each message below a direction, not a pick.

Messages 1 to 3 (20:31):

1. "A dotnet string arrives as a C string. I'm not sure of that. I think ti should arrive as a reference type, whatever
   that means. In C to IL that means it's just a plain reference. So, a special type that represents that it is compiled
   AS a reference. Changing it to a REAL C string would require an explicit operation."
2. "Basically we solve for .NET object references, and strings are just those."
3. "A C string however stays a C string compiled to IL. Memory, allocated, pointer to it."

Messages 4 to 7 (20:35):

4. "What I would do is use special syntax to annotate structs that represent .NET type references, or .NET structs. And
   those attributes can be used by the linker to help in resolution."
5. "Some sort of type mapping. For instance, a struct annnotated with a .NET type that it corresponds to."
6. "Yeah that's CLI_TYPE("Ns.T")"
7. "The specifc names of the C structs or C prototypes and such don't matter, since the attributes specify them."

Questions (not picks), with Claude's answers in brief:
- 20:50: "When you talk about handles, explain this to me. Why are the GCHandles?" then "Okay. So, when we start doing
  more advanced compilation, this might change, right? For instance, a C method that just calls .NET stuff, really has
  no need for shadow stacks and stuff. It can be straight IL with managed references, right?" (Yes: by a type route,
  a `CLI_TYPE` reference kept out of C memory, or an optimizer route, handles elided where they do not escape.)
- About 20:55: "Intersting. What might a reference type look like in C? Some language extension, or?" (Three
  spellings were sketched; the user did not choose.)

R-a's text quotes the end of message 1 and messages 2, 6 and 7 in the first of R-a's section 12 rows, and message 7 in
5.21 RA6 (one type per .NET class, keyed by the folded name: Claude's reading of it).

### 6.3 The user's lean on strings, 2026-11-19 00:23-00:25

A lean, not a pick (the record's label), against MC-1's automatic conversion of a C string argument (S1-S3):
- 00:23: "I guess I don't like the idea of a C string being automatically converted to a .NET string. I don't know why I
  don't like it. I think I don't like it because let me think about this. There's no guarantee that a C string is null
  terminated, is there?"
- 00:24: "There's also no guarantee about character formats."
- 00:25: "You don't need a special CLI_string function either, do you? Because once you get the ability to simply call
  managed functions, you would call the appropriate managed function to do the conversion of a C string to a .NET
  string."

Claude's readings, as recorded: under S1-S3 a missing terminator reads past the buffer and an embedded 0 truncates
silently; text in another encoding that is valid UTF-8 converts silently to different text; C can convert by calling
the .NET API of its choice, with no helpers in the compiler. Q4 (6.9) asked the user to confirm this.

### 6.4 Question 1 as first asked, and the directions it drew (2026-11-19 00:29-01:26)

The first form of question 1 (presented 00:29) came from the references design (ten agents: three researchers, each
with an adversarial verifier, a design, a review, a revised design and a second review; 194 facts, 174 confirmed, 20
corrected, none refuted) and the H-star check of Claude's candidate (50 facts, 47 confirmed, 3 corrected; no fatal
flaw). It asked what `String *` and `String` mean, that is, which spelling gets the ticket and which the plain
reference (a plain-words preface said so after the user's "I don't understand the question.", 01:14): **H-star**
(Claude's recommendation: `String *h` a handle, `String s` an IL reference once the reference step lands and a loud
error until then); **H-both, spelling 1** (the revised design's recommendation: `String *` the IL reference once the
reference step lands, a storable handle needing a kind word, `CLI_TYPE("handle System.String")`); **H-typed**
(`String *` a handle for good, nothing ever compiled as a reference); **H-later** (handles under a kind word now, the
plain pointer decided once the reference work is measured); not recommended, **H-ref** (C memory keeps one untyped
handle, so stored objects lose their .NET type) and **H-elide** (the optimizer turns handles that never leave their
function into references from -O1 up: FeatureIdeas item 5). A correction recorded with it, Claude's, of its own
examples: `typedef struct CLI_TYPE("System.String") String;` declares only the tag in C; the correct form is
`typedef struct CLI_TYPE("System.String") String String;`, as every example here and in 5.21 has it.

The user's messages on it (directions and questions, not picks, as recorded):
- 01:14: "I don't understand the question."
- 01:18: "I guess I still don't understand. How could the H-star option work?" then, after Claude's explanation, "That
  seems to be extending the C language meaning beyond what fits though. * means its a pointer. String* is in fact not a
  pointer to a string strut."
- 01:20: "Actually, let me rephrase that. I don't think it's extending the language as much as it's storing data, which
  is lied about by the type."
- 01:22: "Is there any way to introduce a new magic character or type modifier in C?" (Claude: `^`, a keyword, or a
  type attribute.)
- 01:23: "These are all ultimately just GC handles though, right? So why do we need typed GC handles?"
- 01:26: "Yeah, that's what I mean. The "ticket type" doesn't need to be a type. Right? That's just a raw GCHandle
  integer." and "No reason to color it as anything else." and "What matters is when we start actually using real
  referenes, without GC handle. Thats the end goal, right? We dont care about anything except getting to that."

What the briefing then recorded as settled by these words (directions, not picks; the wording is Claude's): `*` means
only a real pointer into C memory; a ticket is the raw GCHandle number, a plain `intptr_t`, untyped, made and used
through .NET's own GCHandle calls (the `GCHandle_*` entries, `llvm/include/llvm/CLI/BCLMembers.def:150-162`), which MC-1
already accepts as a handle (`isIntPtrType` and `isHandleType`, `clang/lib/Sema/SemaMSILManagedCall.cpp:113-120` and
:137-146); the end goal is real IL references with no GCHandle, so nothing is built on the way (no typed tickets, no
bridge of hidden tickets made and released by the compiler). H-star, H-both, H-typed and H-later each spelled a ticket
or a reference with `*` and were superseded; the question was re-cut (02:26) after the no-star check (section 5), the
caret and keyword check (section 2) and the path plan (section 1).

R-a's text quotes the third 01:26 message as R-a's goal (5.21's opening), "That's just a raw GCHandle integer." in 5.21
RA3 (`intptr_t` and the calls being Claude's reading there) and the 01:18-01:26 messages in section 12's `*` row;
5.21 RA2 ("`*` keeps C's meaning") rests on them.

### 6.5 Q1: when real references come (answered "Okay.")

Presented at 02:27, with the path plan's first slice (section 1.1).

**The first piece, R-a** (the path plan's; C only):
- Where references can live: local variables; parameters and results of `static` functions whose address is never
  taken; managed calls (arguments, results and `this`).
- What works on them: `==`, `!=`, `NULL`, `!r` and `if (r)`; casts checked at run time; a string result comes back as
  a reference.
- Everything else stays a loud "not supported for MSIL yet" error: any C memory (struct fields, arrays, globals, the
  heap, `&r`); exported functions, and calls between C files; function pointers and callbacks; C++, varargs, and
  functions that call setjmp.

**What it needs:** nothing unbuilt (MC-1 is enough; neither EH-0, EH-1, EH-2, M28, P-2, M33 or M34, nor the cleanup
round). Two runtime checks first, about half a day: whether .NET zeroes object locals when this compiler never asks it
to (`localsinit`), on Framework 4.8, .NET 8 and .NET 10, and whether ILVerify and PEVerify accept the new IL (Claude
ran both at about 02:33; section 7). Size (Claude's estimate, unmeasured, as presented): 2 to 4 weeks at two CPUs (the
low end optimistic), and 8,000 to 20,000 lines, mostly tests.

**Where it can go**, earliest first:
- **(A) Next, before the cleanup round's milestones** (Claude's recommendation). Real references about 3 to 5 weeks
  after the user's go. The cleanup round, EH-0 with its three small fix steps (FX-1 to FX-3: a race, a memset length,
  `%L` in formats built at run time) and P-2 each move back by R-a's length, and the four silent wrong-code states
  those steps remove last that much longer, which needs the user's OK under prov-10 (Q5, item 6). A gain: the cleanup
  round's `__cli_import` can use `CLI_TYPE` prototypes from day one.
- **(A') EH-0 and the three fix steps first (small), then R-a, then the cleanup round** (added by the path plan's
  verifier, section 1.6). The silent states end first, and references come a little later. EH-0 waits for the D8 rows;
  if they are not approved, only the fix steps run first.
- **(B) After the cleanup round, before EH-0 and P-2.** R-a lands once, in the new layout, 5 to 9 weeks later than (A).
- **(C) After P-2, before EH-1.** P-2 is written on tickets and reworked later.
- **(D) After Phase 3**, where the earlier designs put it, many months later.

Under (A) or (A'), the half that does not depend on the spelling could start before Q2 was answered (the backend's new
kind of value, the -O0 pass, the stubs and msil-ld; this early start is also the verifier's addition). EH-1 comes after
R-a under (A), (A'), (B) and (C), so it must keep a reference that reaches an exception handler a loud error, or move
it into an IL local; until then the usual SEH shape (a reference set inside `__try` and read in `__except`) is
refused. Approvals R-a needs, each asked later (6.11 to 6.13): edits to upstream files that are not hooked today (2 in
LLVM's core and at least 5 shared clang files, more for some spellings, against Design.md's risk 5, line 316); new
wording for Design.md lines 27 and 125; C only first, or C++ too (P-2 is C++); and the plan's amendments, which the
verifier added (5.13.5's rule that no Phase 3 milestone edits `Attr.td`, and re-pin rows for M28-M35).

**Answer:** "Okay." (2026-11-19 02:33). Claude's reading of the one-word answer, recorded as such: the user accepts the
recommended (A): R-a, the first slice of real IL references (C only), comes next, before the cleanup round's
milestones, EH-0 with FX-1 to FX-3, and P-2; had the user meant only "understood", the reading would be withdrawn.
The prov-10 OK (Q5) and the approvals (6.11 to 6.13) were asked separately; the plan's amendments came with R-a's
text (6.14). The record of this answer calls the silent-state item "item 9" of Q5, as Q5's list was numbered then; in
Q5 as rewritten and answered (6.10) it is item 6, which made the (A) placement explicit. R-a's text: 5.21 RA7 (the
slice in the form R-a's text gives it) and section 12's order row.

### 6.6 Q2: how a real reference is written (first asking)

Presented with the re-cut (02:27) and asked after Q1. As presented, every answer compiles to the same IL and needs the
same backend work; only clang's front end differs.
- **(C) A caret:** `String ^s = System_String_Concat(a, b);` (Claude's lean as presented). C++/CLI's own mark for a .NET
  reference: `*` points into C memory, `^` refers into the .NET heap. The cheapest: clang already reads `^` in every
  declaration for Apple's Blocks (`isPtrOperatorToken`, `clang/lib/Parse/ParseDecl.cpp:6531`) and refuses it on these
  targets today (`Sema::BuildBlockPointerType`, `clang/lib/Sema/SemaType.cpp:2875`). It must be built as a type of its
  own: Apple's block type would be wrong in many places, and about 118 clang files know it. Pointer-like, it gets C's
  rules for `==`, `if`, `NULL`, parameters and results; the compiler must forbid the rest, and mixing two classes must
  be a hard error. Other compilers reject it, loudly; stock formatters would space a literal `^` like XOR, which a macro
  mark, `String CLI_REF s`, avoids.
- **(K) A keyword:** `__cli_ref(String) s = ...;`. Built like `_Atomic(int)`, in one family with `__cli_import`; the
  same type as (C) plus a new keyword (a rebuild of most of clang, and about nine more files); the longest to type and
  the easiest for tools; it can be added later as a second spelling of (C).
- **(V) No mark:** `String s = ...;`. The annotated struct itself is the reference, as in C#; no new syntax. The most
  new compiler work (clang has never treated a struct as a single value, and C++ is hardest). `String b = a;` looks
  like a copy but shares one object. It passes the user's truth test (Claude's reading of the 01:20 message: a C type
  must tell the truth about the data stored in it) only if `String` is read the C# way.
- **(L) Decide later**, after the path is chosen.

Under (C) and (K), every reference carries the mark; it changes nothing at run time and makes the type say "reference".

Questions on it (not picks), 09:46: "So, do all of these require frontend work?" then "The obvious issue is.... how
would the Objective-C to IL compiler work?" Claude's answers: every spelling needs front-end work; a block pointer
always points to a function type, so a reference `^` on a `CLI_TYPE` class can coexist with Objective-C's blocks by
the pointee, though `^` would then mean two things for a reader. By the record of the pick, Claude's lean then moved
from (C) to (K).

The answers:
- 10:27: "Let's go with C, the carrot." Option (C): a real reference is written `String ^s`, `String` being the
  `CLI_TYPE`-annotated struct with no body. The record notes that (C) was the caret and keyword check's lean and
  Claude's first lean, but not Claude's lean at the time of the pick.
- The user then asked "Under these proposals, what is String defined as?" (the record gives no time), and Claude
  offered a simpler shape, K2, as not yet checked: `typedef __cli_ref("System.String") String;`, with
  `String s = System_String_Concat(a, b);` a real .NET reference. The keyword takes the .NET name directly; classes
  need no struct and no star; `CLI_TYPE` structs stay for real .NET structs and enums.
- 10:29: "Wait, hold on." then "Your simpler shape I like better." K2 replaced the caret pick of 10:27, to hold unless
  the K2 check found a fatal flaw; Q3 was withdrawn with it (6.8).
- 10:31 (a question, not a pick), on how real .NET structs work, quoted in section 4.1. Claude's first answer said real
  .NET values need value-type IL locals (M33's and M34's territory); corrected in 6.7.
- 10:40 (an observation, not a pick, as recorded), quoted in section 4.1, ending "I guess that's the benefit of the ^
  syntax. YOu can represent both." The value-types check was restarted with these words (section 4); the K2 check found
  no fatal flaw in K2 (section 3).

### 6.7 Q2, asked again: K2 with a type form, or the caret (answered "Caret")

Presented at 11:38, after the K2 and value-types checks, neither with a fatal flaw. What the checks found, as presented:
- **Both forms of a struct work.** A struct with no references inside (`DateTime`, `Guid`, `Vector3`) can live in C
  memory as its bytes, copied by IL's own instructions; a boxed copy is a real reference with no GCHandle. Structs with
  references inside (`CancellationToken`) can only be references or IL values in locals. None of this needs M33 or
  M34: Claude's earlier answer (10:31) was wrong there.
- **Both spellings can show both forms from one struct declaration:** the caret, `DateTime d;` and `DateTime ^r;`; K2
  with a type form, `DateTime d;` and `__cli_ref(DateTime) r;`, classes keeping
  `typedef __cli_ref("System.String") String;`.
- **How a reference is built is a separate choice from its spelling.** Built as a type of its own, it fails closed and
  mixing two classes is a hard error: about 40 to 45 untouched upstream files for C (46 to 51 with C++), for either
  spelling. Built on clang's pointer machinery, it needs about half the files but fails open, a class mix-up being
  only a warning that is an error by default; only the caret can be built this way.
- **Other differences:** `^` is shorter; `^` means a block in Objective-C code, so it would mean two things there,
  while K2 collides with nothing; under K2 a class needs no struct at all.

Claude's recommendation: keep K2, add the type form, built as a type of its own. The answers offered were "Keep K2" and
"Caret", the caret option framed as "The caret, built the same way".

**Answer:** "Caret" (2026-11-19 12:31), against Claude's recommendation. As the briefing recorded it: a reference is
written `T ^r` (classes: `String ^s`; value types: `DateTime d` for the value, `DateTime ^r` for a boxed copy), built as
a type of its own, keyed by the .NET name, so two C names for one class are one type (message 7); K2 is superseded.
Claude's readings, recorded with the pick: a type of its own, as the option was framed (fails closed; class mix-ups
are hard errors); keyed by the folded .NET name from `CLI_TYPE`, which answers Q3 by construction (the user can
object); Blocks keep `^` for function pointees, so `^` means two things in Objective-C code, accepted with this pick.
R-a's text: 5.21 RA1, RA5 and RA6, and section 12's row on `T ^r`, which also quotes the answers of 10:27, 10:29 and
10:40; the value-type forms are in neither R-a nor R-a2 (section 4).

### 6.8 Q3: one CLI_TYPE, two names (withdrawn)

Message 7 says names don't matter; C's own rule makes two differently named structs two types. Asked with the re-cut
(02:27), the options were **compatible** (Claude's proposal: one type per `CLI_TYPE`, at the cost of hooks in
`ASTContext::mergeTypes` (`clang/lib/AST/ASTContext.cpp:11937`) and `checkPointerTypesForAssignment`
(`clang/lib/Sema/SemaExpr.cpp:9581`), neither file hooked today) and **C's rule** (names matter; a cast mixes them).
Rewritten for the caret (10:27): with `typedef struct CLI_TYPE("System.String") String String;` in one header and
`typedef struct CLI_TYPE("System.String") NetStr NetStr;` in another, is `NetStr ^` the same type as `String ^`? (a) one
type (Claude's proposal); (b) C's rule. Withdrawn at 10:29: under K2, two typedef names of one `__cli_ref("Ns.T")` are
one type by construction, which is message 7. After "Caret", Claude's reading keeps it answered by construction, the
caret's type being keyed by the folded .NET name (5.21 RA6).

### 6.9 Q4: strings, following the user's lean (answered "Confirm")

Presented at 12:31 as a confirmation of the user's lean (6.3). Claude's proposal, from the user's three messages:
- No automatic conversion either way. A `string` parameter takes a .NET string; MC-1's rules S1-S3 go.
- No conversion helpers in the compiler, and no special string pseudo-member.
- C converts by calling the .NET API it chooses, and so picks both the end of the text and its encoding. In:
  `new UTF8Encoding(false, true)`, then `Encoding.GetString(uint8*, int32)`, both already in msil-ld's built-in table.
  Out: `GetByteCount`, `GetBytes` and `Marshal.Copy`, as MC-1's rule S4 already says for results.
- Optionally, a small header of C conveniences over those calls: library code, not compiler code.
- Without a signature, `const char *` then means bytes, as Cesium reads it.

Costs, as presented: 94 of the tree's 947 managed-call declarations are rewritten (84 that compile and 10 error tests,
with 88 string parameters); each call site gets more lines, and P-2 converts every file name itself; until references
exist, each intermediate object (the encoding, the string) is a ticket to release, and with references nothing is
released; the string-to-bytes members (`GetBytes`, `GetByteCount`, `Marshal.Copy`) bind only with `--reference` today,
since the built-in table has only the decoding ones (`Encoding_GetString`, `UTF8Encoding_ctor` and `UTF32Encoding_ctor`,
`llvm/include/llvm/CLI/BCLMembers.def:168-176` at HEAD; the briefing cited 168-175).

The answers offered were "Confirm" and "Keep the header out". **Answer:** "Confirm" (2026-11-19 12:34): the lean
confirmed, with the optional convenience header (also Claude's recommendation). When S1-S3 go was asked later, with
R-a's text (6.14). R-a's text: 5.21 RM3, and section 12's strings row, which quotes the 00:23 and 00:24 messages.

### 6.10 Q5: silent states that need the user's OK (answered "OK all six")

Presented at 12:34, rewritten for raw tickets, real references and the caret. The rule it applies is the user's pick on
the review page (2026-11-04, item prov-10), "silent states only with my OK". With real references most short-lived
objects need no ticket at all, so the states concern mainly objects kept in C memory:
1. **A forgotten ticket release leaks silently** (MC-1 rule MH6), also when a longjmp or a .NET exception skips the
   release. Aid: a checked-handle mode that lists leaks at exit, which comes after Phase 3.
2. **Using a ticket after its release is undefined** (MH4): usually a loud exception, but a recycled slot that holds an
   object of the same type goes unnoticed. Aid: the same checked mode, later.
3. **`==` on two tickets compares numbers, not objects.** Two tickets can name one object. With references, `==` is
   real identity.
4. **Tickets and pointers can mix.** In C, mixing an integer and a pointer is an error only by default
   (`-Wno-int-conversion` turns it off); in C++, `NULL` is an integer.
5. **A pinned view of a string** (its characters read in place, through .NET's pinning call) can be used after it is
   unpinned, or written through, which changes an immutable and possibly shared string.
6. **Under (A), EH-0's four silent wrong-code states last R-a's 2 to 4 weeks longer.** Saying no here means (A'): EH-0
   and its three fixes first, then R-a.

The briefing labels MH4 and MH6 Claude's decisions. Gone since the re-cut: typed handles, `*h`, and `cli_release(NULL)`
(a ticket is released through .NET's own `GCHandle.Free`, which throws on 0, loudly). Shown with the six, and loud
rather than silent: a `goto` or `switch` that jumps past a reference's declaration would skip its starting null silently
at -O1 and above, so the compiler refuses such jumps. The briefing says the K2 check found this; R-a's text records the
refusal as Claude's decision from the K2 check's verifier, shown with this question and not asked (5.21 RS28;
section 3.6).

Claude's recommendation: OK all six; 1 to 5 are C's own model for anything released by hand, as with `free`, with loud
aids where they exist, and 6 is the price of real references first, the user's priority. The answers offered were "OK
all six" and "Not 6: EH-0 first".

**Answer:** "OK all six" (2026-11-19 12:37). Item 6 makes Q1's (A) placement explicit (as the walk's record states it).
R-a's text: 5.21 RS28, section 12's silent-states row and its order row; the later time bound on item 6 and one more
silent state are in 6.14.

### 6.11 Approval 1: the upstream footprint (answered "(a) Yes")

Presented at 12:37, once Q1 to Q5 were answered, as the first of three approvals R-a needed before it starts. The fork
changes 82 upstream files today, each listed with its tests in `msil/utils/upstream-hooks.txt` (82 `hook` entries at
HEAD). Design.md's risk 5 (line 316) says: "Keep the diff to new directories plus registration hooks, as WebAssembly and
llvm-mos do." Building the caret as a type of its own puts a new kind of type into clang: its type system (AST,
serialization, printing, mangling, debug info), its rules (Sema) and its code generation, plus two files in LLVM's core.
The K2 check and its verifier estimated about 40 to 45 more upstream files for C, and 46 to 51 with C++, about half
again as many as today: an estimate from reading, with R-a's spec to list the exact files.
- **(a) Yes.** Each new hook is kept minimal and listed with its tests, as today's 82 are, and risk 5's sentence gains
  "and the .NET reference type's hooks in clang's type system". (Claude's recommendation.)
- **(b) The cheaper route instead:** the caret built on clang's pointer machinery, about 20 to 24 clang files plus the
  same 2 core files. It fails open: a missed rule compiles silently, and a class mix-up is only a warning that is an
  error by default.
- **(c) Not yet.** R-a waits, and real references with it.

Why (a), as presented: the end goal needs a type that clang itself checks, and failing closed is the user's prov-10
rule. Weak point: every upstream merge carries these hunks, in clang's type system, which changes often upstream. The
answers offered were "(a) Yes" and "(b) Pointer route".

**Answer:** "(a) Yes" (2026-11-19 12:39). The record states what it approves: R-a may change about 40-45 more upstream
files for C (46-51 with C++; 82 are hooked today) to build the caret as a type of its own, plus two files in LLVM's
core, each new hook minimal and listed with its tests. R-a's text lists 43 new hook blocks (52 upstream files with the
9 already hooked) and bounds them at 45 with the two core files counted, a 46th coming back to the user (Claude's
stricter reading; 5.21.13 RH1 and RH2, and section 12's footprint row). Risk 5's clause and the 5.13.5 exemption rest
on this approval (section 12).

### 6.12 Approval 2: Design.md's texts (answered "Approve")

Presented at 12:39, each text shown before and after. Line 27, a non-goal (Design.md section 1), before:

> - A managed object model, GC references in C code, or C++/CLI-style mixed assemblies. Two narrower features are later goals (user, 2026-10-22): ...

after:

> - A managed object model, GC references stored in C memory, or C++/CLI-style mixed assemblies. C code holds .NET objects as real references (`T ^r`, `T` a `CLI_TYPE` struct) only in local variables, parameters and results, where .NET tracks them, and as GCHandle tickets (plain integers) when C memory must keep them (user, 2026-11-19). Two narrower features are later goals (user, 2026-10-22): ... (the rest unchanged)

Line 125, the memory model (Design.md section 4), before:

> The C program never touches the GC heap. All C-visible memory is unmanaged and never moves, so pointers are plain `native int` and pinning is never needed.

after:

> C memory never holds a GC reference. All C-visible memory is unmanaged and never moves, so pointers are plain `native int`, and the compiler never pins anything. A .NET reference lives only where the runtime tracks it: in IL locals, parameters, results and the evaluation stack. A program that pins an object through .NET's own calls gets a pointer that is valid only while the object stays pinned (user, 2026-11-19).

Claude's recommendation: approve as written; the edits land with R-a's plan text. The answers offered were "Approve"
and "Change them". **Answer:** "Approve" (2026-11-19 12:49): both texts as drafted, word for word. R-a's plan commit
applies them, with approval 1's clause at the end of risk 5 (line 316; its place there is Claude's reading).

### 6.13 Approval 3: C only first, and where C++ comes (answered "(a) Yes, R-a2")

Presented at 12:49. R-a, as planned, is C only. P-2's OS layer is C++ (llvm-libc's `DotnetFile`, plan 5.19.9), and
under (A) P-2 comes after R-a and the cleanup round's milestones.
- **(a) R-a is C only; C++ follows as R-a2, before P-2** (Claude's recommendation). R-a2 adds references in C++ local
  variables and in parameters and results of internal C++ functions: templates by value, overloads, C++ name mangling.
  It refuses `String&`, lambda captures and class members for now. P-2 can then hold references in locals and
  internal helpers; its callbacks through function pointers and its virtual functions wait for R-b, so what P-2 keeps
  beyond a call stays a ticket.
- **(b) C only now; C++ later with R-b, after P-2.** P-2 is written on tickets throughout, and reworked later.
- **(c) C and C++ together in R-a.** One step, larger and later: about 46 to 51 upstream files instead of 40 to 45, and
  more weeks before the first real reference.

Why (a), as presented: it keeps the first real references small and early, the user's priority, and still lets P-2
use them. **Answer:** "(a) Yes, R-a2" (2026-11-19 12:50). R-a's text: 5.21 RA4 (C only), R-a2's outline (5.21.16 and
the milestone's R-a2 paragraph) and section 12's R-a2 rows; R-a2's exact place came later (6.14).

### 6.14 What the walk changes later, and later picks that refine it

As the briefing put it: R-b later takes exported and public functions, callbacks and C++ (approval 3 moved C++ locals
and internal functions to R-a2); tickets stay MC-1's raw integers, and a boxed .NET struct needs a spelling of its own,
in 14 test declarations in 7 files (section 5); the optimizer route (handles elided) stays at the very end, unless the
user moves it.

Later picks, asked with R-a's text and quoted in it by the user's words and times, refine answers above:
- "(a) Stop and ask me" (2026-11-19 15:48): if R-a has not committed 4 weeks after its launch, the user is asked
  whether EH-0 and FX-1 to FX-3 go first; this bounds Q5's item 6 at 4 weeks.
- "(b) Before P-2" (2026-11-20 11:50): R-a2 goes directly before P-2, after the cleanup round's milestones and EH-0
  with FX-1 to FX-3.
- "(a) Before P-2" (2026-11-20 12:04): S1-S3 (Q4) go in R-a2, directly before P-2; as presented, also the user's OK of
  their silent cases until then.
- "(b) Retire before P-2" (2026-11-20 14:24): MC-1's star-spelled handles become `intptr_t` or `uintptr_t` in R-a2;
  boxed parameters and results keep the star until value-type form B (`DateTime ^r`) or the step named by the answer
  to references VT-9 (section 4).
- "(a) OK as undefined" (2026-11-20 17:04): one more silent state with the user's OK, a ticket number that was never
  live, beside Q5's item 2 (section 5.7).

R-a's text itself, the plan's amendments included, was approved by the user on 2026-11-20 at 20:42 with one change:
"The only change is you can use 4 CPUs." The estimates quoted in this section are those presented on 2026-11-19.

## 7. Measurements: zinit.il

R-a's two runtime checks of 2026-11-19 (does the runtime zero object locals without `localsinit`, and what do ILVerify
and PEVerify say about them) ran on one hand-written file, kept as
[references-files/zinit.il](references-files/zinit.il). The copy is byte for byte (3,026 bytes, 127 lines); the file
holds no machine path or user name, so nothing in it was replaced. 5.21.11 cites its results, and R-a.k and R-a.m
read it.

### 7.1 What the file tests

The question is the first runtime check that the path plan proposed (section 1.5; the plan and its proposal are
Claude's): does the runtime zero the object-typed locals of a method whose header does not set `localsinit`? This
compiler never sets it. At HEAD `a15f3c958572`:

- Design.md section 4.2 ends its frame rule with "Frames are not zeroed: `.locals` is never `init`."
  (`llvm/docs/MSIL/Design.md:139`), and plan decision D13 says the same (`llvm/docs/MSIL/ImplementationPlan.md:178`).
- The text streamer writes `.locals (` under the comment "Never 'init' (decision D13)"
  (`MSILTargetAsmStreamer::emitLocals`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILTargetStreamer.cpp:202-204`), and the
  object writer writes the method bodies "never InitLocals (decision D13)" (`MSILObjectWriter::writeObject`,
  `llvm/lib/Target/MSIL/MCTargetDesc/MSILObjectWriter.cpp:787-788`).

The path plan expected, without measuring, that the JIT zeroes reference-typed locals whatever the flag says, because
the garbage collector must never see garbage. R-a holds its references in IL locals among other places (5.21.1 RA1),
so the expectation had to be measured.

The file is one hand-written IL assembly against `[mscorlib]` 4.0.0.0, with one class `P` and three methods:

- `Dirty()` (lines 11-57; `noinlining`; `.locals init`): ten locals, eight `object`, one `string` and one `object[]`.
  It stores one new `System.Object` in the eight object locals, `ldstr "dirty"` in the string and a four-element
  `newarr` in the array, then passes each local to `GC.KeepAlive`, so its frame holds ten live, non-null references.
- `Probe(bool collect)` (lines 59-92; `noinlining`; `.locals` without `init`, line 62): the same ten locals, never
  written. When `collect` is true it calls `GC.Collect()` first (line 65). It then reads every local with `ldloc` and
  `brtrue`, and returns 0 when all ten are null, 1 otherwise. It is the only method of the file without `init`.
- `Main()` (lines 94-126): while `i < 100000`, calls `Dirty()` and then `Probe(i % 1000 == 0)`, so each probe runs
  right after a frame full of references, from the same caller, and every 1,000th probe collects first. It adds up
  the results and returns 42 when every read was null, 1 when any was not.

The file's own comment states the contract ("Exit 42 = every read was null; 1 = a non-null read.") and that the
collection "would crash on a garbage reference". It cites `Design.md:139`, a line number of HEAD `a15f3c958572`.

Against the path plan's proposed tests (section 1.5), the file has the shapes of (a), an `object` local read before it
is written after a callee filled the stack, and (c), unwritten object locals held while `GC.Collect()` runs, many times.
It does not have (b), the compiler's frame shape (`localloc` at entry plus native int locals), or (d), a filter clause;
nor `ldnull`, `ceq` or `select` on objects, `castclass`, or object arguments and results.

### 7.2 How it was run

Claude ran it with direct commands on 2026-11-19 at about 02:33, in a scratch folder outside the tree, after the path
plan's verification had ended (its verifier's order review: "The two runtime checks can run now."). The path plan had
proposed hand-written IL files assembled with ilasm in a scratch folder. The run's record names the runtimes, the
verifiers and the results; it does not keep the command lines, the ilasm build or its options, or the operating system.
Beside the IL, three runtime configuration files (not committed) name the .NET versions:

```
rc8.0.0.json   {"runtimeOptions":{"tfm":"net8.0","framework":{"name":"Microsoft.NETCore.App","version":"8.0.0"},"rollForward":"LatestPatch"}}
rc10.0.0.json  {"runtimeOptions":{"tfm":"net10.0","framework":{"name":"Microsoft.NETCore.App","version":"10.0.0"},"rollForward":"LatestPatch"}}
rc11.0.0.json  {"runtimeOptions":{"tfm":"net11.0","framework":{"name":"Microsoft.NETCore.App","version":"11.0.0"},"rollForward":"LatestPatch"}}
```

Runtimes, as the record names them: .NET Framework 4.8, and .NET 8.0.34, 10.0.15 and 11.0.0, each with
TieredCompilation on and off. Verifiers: ILVerify 10.0.12, and PEVerify of the NETFX 4.8.1 Tools. The path
plan's list also had a WSL .NET 10 run; the record names none. .NET 11.0.0 was not on that list.

### 7.3 Results

| Check | Result |
|---|---|
| Zeroing | exit 42 ("every read null, no crash") on .NET Framework 4.8 and on .NET 8.0.34, 10.0.15 and 11.0.0, each with TieredCompilation on and off |
| ILVerify 10.0.12 | reports only InitLocals |
| PEVerify (NETFX 4.8.1 Tools) | reports only InitLocals |

InitLocals is allowed: plan decision D21's ILVerify allow-list names it (`llvm/docs/MSIL/ImplementationPlan.md:186`),
`msil/utils/ilverify-allowed.txt` has it under the comment "'.locals' are never 'init' (D13)." (lines 37-38), and
`msil-verify.py`'s first pass allows PEVerify's message for ".locals without 'init' (D13)"
(`msil/utils/msil-verify.py:315-319`). The file's IL brings no new verifier message.

5.21.11 records the result as plain object locals without `localsinit`, read before written, reading as null on
Framework 4.8 and .NET 8, 10 and 11, tiered on and off, with ILVerify and PEVerify reporting only the allowed
InitLocals. On that basis Design.md section 4.2's frame rule and D13 stand, and R-a's plan commit leaves Design.md:139
unchanged.

### 7.4 What it does not cover

- Claude's note of the run: not yet covered are `ceq` on two references, `castclass`, and object arguments and
  results; D21 checks each shape in the milestone that first emits it.
- 5.21.11's list of what is not measured: object locals beside the `localloc` frame, filter clauses, `ceq` and `select`
  on objects, and `castclass` on entry. R-a's tests decide them: `frames-mixed` and `catch-ref` in
  `msil/test/M3/14-references/` (RV28, which also has `gc-stress`), the stub tests, both `msil-verify.py` passes,
  jitall, PEVerify, and `msil/test/harness/verify-object-locals.test` (ILVerify and PEVerify on those shapes, RV28). In
  llc, `reference-promote.ll` has locals beside the `localloc` frame (RV23). Pass 2 of `msil-verify.py` verifies a copy
  whose `.locals (...)` are rewritten to `.locals init (...)` (`msil/utils/msil-verify.py:69-81`); the allowance for
  `.locals` without `init` belongs to pass 1.
- Until those tests pass, a `localsinit` fallback stays open (Claude's reading, 5.21.11). It would set `localsinit` on
  methods with object locals, which also zeroes their `localloc` frame, a cost the path plan says to measure. It would
  need edits of Design.md:139 and plan D13 (the path plan's verifier, section 1.8), and it would come back to the user.
- Struct locals that hold references (`CancellationToken`, `Span<byte>`): the value-types check (section 4.8) proposes
  extending this check to them, on Framework 4.8, .NET 8 and .NET 10 with tiering on and off, with `localloc` in the
  same method and under `GC.Collect`, and measuring the cost of `initobj`. That is not part of this measurement; value
  types are not in R-a (5.21.16).

### 7.5 How R-a relies on it

In 5.21 the measurement is a net, not C's guarantee: "C's guarantee is RS2 with RS28" (RB7). The path plan listed two
nets (section 1.5): R-a's IL never reads an object local before writing it (`ldnull` is written for undefined values),
and `localsinit` on methods with object locals if check (c) failed. For C's NULL start it gave two options, a store by
clang or the backend's `ldnull`. Its verifier corrected that (section 1.2, with the verifier's `pick` example): the NULL
start must be done in clang, because the backend's `ldnull` gives silent wrong code at -O1 and above; `ldnull` on
`IMPLICIT_DEF` stays only as the GC safety net.

5.21 applies the correction. RS2 adds a zero-initialization step in Sema (`TryDefaultInitialization`) and RG3 stores
NULL at the declaration; RS28 refuses jumps past a reference's declaration; RB7 turns an OBJ `IMPLICIT_DEF` into
`ldnull` in `explicitLocals`, a partial net only, because `isSafeToMove` skips undef operands; and stubs and ISel never
read an object local before writing it (5.21.11). Where an unwritten object local is read all the same, the measured
runtimes gave null.

## 8. File-and-line evidence for 5.21's citations

5.21 names files and functions, never line numbers (5.13's rule). This section is the evidence behind those names: for
each file-and-function citation of 5.21, and for the functions that the R-a milestone's Reads entries name in
parentheses, the place at HEAD `a15f3c958572`. **The lines are HEAD `a15f3c958572`'s and move with later commits; the
names are what R-a's text relies on.** In the Lines column a range given for a function or a class is its whole
definition (in C and C++, its ends found by matching braces); after a semicolon comes the cited part inside the first
range or, where a row names several symbols or places, the next one's lines, in the order named. "Cited by" names 5.21's
rule or section, or the work package whose Reads entry names the function ("R-a.c Reads"). Every row was re-checked at
that HEAD with `git show`, starting from an earlier list made for a draft of R-a's text; 8.10 says what changed.

### 8.1 clang: parser and Sema

| Cited by | File | Lines | Symbol: what is there |
|---|---|---|---|
| RC1 | clang/lib/Parse/ParseDecl.cpp | 6529-6555; 6531 | `isPtrOperatorToken`: `tok::caret` is a pointer operator |
| RC1 | clang/lib/Parse/ParseDecl.cpp | 6568-6759; 6674-6703 | `Parser::ParseDeclaratorInternal`: `*` and `^` share one path; `^` adds `DeclaratorChunk::getBlockPointer` with the parsed qualifiers |
| RC2, RS17; R-a.c Reads | clang/lib/Sema/SemaType.cpp | 4390-5825; 4780-4797 | `GetFullTypeForDeclarator`, its `DeclaratorChunk::BlockPointer` case: the `-fblocks` error (4782-4783), the block-pointer nullability inference (4786-4788), `BuildBlockPointerType` (4790) |
| RC2 | clang/lib/Sema/SemaType.cpp | 2871-2886 | `Sema::BuildBlockPointerType`: a non-function pointee is `err_nonfunction_block_type` (2874-2877) |
| RC3 | clang/lib/Sema/SemaType.cpp | 9750-9907; 9779-9781 | `Sema::RequireCompleteTypeImpl`: its sizeless test (`isSizelessBuiltinType`) |
| RC5; R-a.c Reads | clang/lib/Sema/SemaType.cpp | 6286-6405; 6402-6404 | `DeclaratorLocFiller`: `VisitTypeLoc` is `llvm_unreachable`; its `VisitBlockPointerTypeLoc` asserts a block-pointer chunk (6321-6324) |
| RC5 | clang/lib/Sema/SemaType.cpp | 457, 484, 518, 577, 642, 2942, 2994, 3095, 3745, 4020, 4320, 4448, 4516, 5568, 5911, 5995, 7757, 7787 | the 18 chunk sites: `maybeMovePastReturnType` 457, 484 (441-502); `distributeObjCPointerTypeAttr` 518 (509-561); `distributeObjCPointerTypeAttrFromDeclarator` 577 (565-620); `distributeFunctionTypeAttr` 642 (626-652); `inferARCWriteback` 2942, 2994 (2913-3007); `diagnoseRedundantReturnTypeQualifiers` 3095 (3060-3128); `warnAboutRedundantParens` 3745 (3691-3815); `classifyPointerDeclarator` 4020 (3998-4124); `hasOuterPointerLikeChunk` 4320 (4305-4327); `GetFullTypeForDeclarator` 4448 (the deduced-type check), 4516 (the nullability pointer count), 5568 (the C23 prototype rule); `transferARCOwnership` 5911 (5888-5937); `fillAtomicQualLoc` 5995 (5982-6004); `distributeNullabilityTypeAttr` 7757, 7787 (7735-7814) |
| RS10, RS17; R-a.c Reads | clang/lib/Sema/SemaType.cpp | 1845-1896; 1879-1892 | `Sema::BuildPointerType`: WebAssembly's refusal of pointers to reference types and tables |
| RN4 | clang/lib/Sema/SemaType.cpp | 1898-1971; 1956-1960 | `Sema::BuildReferenceType`: WebAssembly's refusal of references to reference types and tables |
| RA5 | clang/lib/Sema/SemaExpr.cpp | 9919-10322; 10161-10171 | `Sema::CheckAssignmentConstraints`: `// U^ -> void*`, a block pointer converts to `void *` as `Compatible` |
| RS4; R-a.c Reads | clang/lib/Sema/SemaExpr.cpp | 9919-10322; 9919-9935 | `Sema::CheckAssignmentConstraints`, its start: canonical unqualified types, and equal types are `CK_NoOp` (the overload that takes a `SourceLocation` is 9880-9892) |
| RS4; R-a.c Reads | clang/lib/Sema/SemaExpr.cpp | 10399-10635; 10580-10585 | `Sema::CheckSingleAssignmentConstraints`: OpenCL `queue_t`'s null rule (`CK_NullToPointer`) |
| RS4 | clang/lib/Sema/SemaInit.cpp | 7964-8949; 8596-8598 | `InitializationSequence::Perform`: its call of `CheckSingleAssignmentConstraints` |
| RS5; R-a.c Reads | clang/lib/Sema/SemaExpr.cpp | 13193-13699; 13683-13695 | `Sema::CheckCompareOperands`: the `queue_t` rules |
| RS6; R-a.c Reads | clang/lib/Sema/SemaExpr.cpp | 21712-21744; 21723-21724 | `Sema::CheckBooleanCondition`: AMDGPU's predicate is rewritten (`ExpandAMDGPUPredicateBuiltIn`) |
| RS6 | clang/lib/Sema/SemaExpr.cpp | 16559-16822; 16699 | `Sema::CreateBuiltinUnaryOp`: the scalar test of the operand of `!` |
| RS6 | clang/lib/Sema/SemaExpr.cpp | 14225-14351; 14313-14320 | `Sema::CheckLogicalOperands`: AMDGPU's predicate is rewritten, then the scalar test |
| RS6 | clang/lib/Sema/SemaExpr.cpp | 8653-8670 | `checkCondition`: the scalar test of a `?:` condition |
| RS7; R-a.c Reads | clang/lib/Sema/SemaExpr.cpp | 9115-9298; 9282-9286 | `Sema::CheckConditionalOperands`: the null-pointer-constant diagnostic (`DiagnoseConditionalForNull`) |
| RS13 | clang/lib/Sema/SemaExpr.cpp | 15269-15523; 15505-15512 | `Sema::CheckAddressOfOperand`: WebAssembly's reference refusal |
| RS19; R-a.c Reads | clang/lib/Sema/SemaExpr.cpp | 967-1026; 988-991; 1025 | `Sema::isValidVarArgType`: a WebAssembly reference is `VarArgKind::Invalid`; the `Undefined` fallback |
| RS19 | clang/lib/Sema/SemaExpr.cpp | 1073-1134; 1103-1104 | `Sema::DefaultVariadicArgumentPromotion`: an `Undefined` argument becomes a trap |
| RS26 | clang/lib/Sema/SemaExpr.cpp | 19773-19871 | `isVariableCapturable` |
| RH5; R-a.c Reads | clang/lib/Sema/SemaExpr.cpp | 4663-4783; 4671 | `captureVariablyModifiedType`: its switch over type classes |
| RS8 | clang/lib/Sema/SemaCast.cpp | 3046-3400; 3136 | `CastOperation::CheckCStyleCast`: its scalar-type test |
| RS2; R-a.c Reads | clang/lib/Sema/SemaInit.cpp | 5951-5987; 5982-5986 | `TryDefaultInitialization`: ARC's zero-initialization step |
| RT4; R-a.c Reads | clang/lib/Sema/SemaDecl.cpp | 2956-3084 | `mergeDeclAttribute` |
| RS11, RS28; R-a.c Reads | clang/lib/Sema/SemaDecl.cpp | 9049-9382; 9250-9253 | `Sema::CheckVariableDeclarationType`: the protected-scope marking (`setFunctionHasBranchProtectedScope`) |
| RS11 | clang/lib/Sema/SemaDecl.cpp | 10289-11524; 11313-11319 | `Sema::ActOnFunctionDeclarator`: MC-1's hook (`msil::checkMSILManagedCallDecl`) |
| RS17; R-a.c Reads | clang/lib/Sema/SemaDecl.cpp | 16154-16262; 16199-16200 | `Sema::CheckParameter`: the parameter gets its adjusted type (`getAdjustedParameterType`) |
| RS22; R-a.c Reads | clang/lib/Sema/SemaDecl.cpp | 16945-17462 | `Sema::ActOnFinishFunctionBody` |
| RT5 | clang/lib/Sema/SemaDecl.cpp | 19412-19478 | `Sema::ActOnTagFinishDefinition` |
| RT8 | clang/lib/Sema/SemaDeclAttr.cpp | 7623-8580; 7812-7813 | `ProcessDeclAttribute`: MC-1's `AT_MSILMethod` case |
| RS16 | clang/lib/Sema/SemaOverload.cpp | 11949-12009 | `checkAddressOfFunctionIsAvailable` |
| RN3 | clang/lib/Sema/SemaOverload.cpp | 2410-2784; 2600-2606; 2685-2689 | `IsStandardConversion`: the boolean rule; the `queue_t` rule |
| RN3 | clang/lib/Sema/Sema.cpp | 885-899 | `Sema::ScalarTypeToBooleanCastKind` |
| RN3, RN8 | clang/include/clang/Sema/Overload.h | 104 | `ImplicitConversionKind` (the text names only the file) |
| RN2 | clang/lib/Sema/SemaTemplateDeduction.cpp | 1605-2571; 2220-2224 | `DeduceTemplateArgumentsByTypeMatch`: its `BlockPointer` case deduces only from a block pointer |
| RS28; R-a.c Reads | clang/lib/Sema/JumpDiagnostics.cpp | 144-232 | `GetDiagForGotoScopeDecl` |
| RS11; R-a.c Reads | clang/lib/Sema/SemaMSILManagedCall.cpp | 137-146 | `isHandleType` |
| RM2; R-a.c Reads | clang/lib/Sema/SemaMSILManagedCall.cpp | 271-302 | `classifyManagedParam`; beside it, and not named by the text, `classifyManagedResult` (308-338) and `classifyManagedThis` (344-359) classify the result and `this` |

### 8.2 clang: AST, serialization, mangling and libclang

| Cited by | File | Lines | Symbol: what is there |
|---|---|---|---|
| RC3; R-a.a Reads | clang/include/clang/Basic/TypeNodes.td | 49; 89 | `def BlockPointerType`; `def HLSLInlineSpirvType` |
| R-a.a Reads | clang/include/clang/AST/TypeBase.h | 3653; 2490 | `class BlockPointerType`; the declaration of `Type::isSizelessType` (`isSizelessBuiltinType` at 2491) |
| RC3 | clang/lib/AST/Type.cpp | 2789; 2751-2771 | `Type::isSizelessType`, which returns `isSizelessBuiltinType()`; `Type::isSizelessBuiltinType` |
| R-a.b Reads | clang/lib/AST/Type.cpp | 5096-5199; 5212-5302 | `computeCachedProperties`; `LinkageComputer::computeTypeLinkageInfo` |
| R-a.a Reads | clang/include/clang/AST/ASTContext.h | 1734 | `getBlockPointerType` |
| R-a.b Reads | clang/lib/AST/ASTContext.cpp | 4146-4167; 2084-2660; 11937-12333 | `ASTContext::getBlockPointerType`; `ASTContext::getTypeInfoImpl`; `ASTContext::mergeTypes` |
| RA5; R-a.b Reads | clang/lib/AST/ItaniumMangle.cpp | 4589-4592 | `CXXNameMangler::mangleType(const BlockPointerType *)`: `U13block_pointer` (the dispatcher `mangleType(QualType)` is 3106-3211) |
| RN6; R-a.b Reads | clang/lib/AST/MicrosoftMangle.cpp | 4028-4044; 3872-3881 | `MicrosoftCXXNameMangler::mangleType(const OverflowBehaviorType *, ...)`; the `BlockPointerType` overload |
| R-a.b Reads | clang/lib/Serialization/ASTReader.cpp | 7549-7581; 7623 | `TypeLocReader`; its `VisitBlockPointerTypeLoc` |
| R-a.b Reads | clang/lib/Serialization/ASTWriter.cpp | 327-344; 382 | `TypeLocWriter`; its `VisitBlockPointerTypeLoc` |
| RH4; R-a.b Reads | clang/tools/libclang/CursorVisitor.h | 70-286; 70-71; 268-270 | `CursorVisitor`, a `TypeLocVisitor<CursorVisitor, bool>`; one `Visit<Class>TypeLoc` declaration per class through `TypeLocNodes.def` |
| RH4; R-a.b Reads | clang/tools/libclang/CIndex.cpp | 1722-1724; 1898-1901 | `CursorVisitor::VisitBlockPointerTypeLoc`, which visits the pointee's TypeLoc; `DEFAULT_TYPELOC_IMPL`, which hands a class to its parent's visitor |
| RH4 | clang/include/clang/AST/TypeLocVisitor.h | 53 | `TypeLocVisitor::VisitTypeLoc` returns `RetTy()`: it visits nothing |

### 8.3 clang: CodeGen, target ABIs and ClangIR

| Cited by | File | Lines | Symbol: what is there |
|---|---|---|---|
| RA5, RG1; R-a.d Reads | clang/lib/CodeGen/CodeGenTypes.cpp | 360-813; 396; 577-584; 741-752 | `CodeGenTypes::ConvertType`: its switch over type classes; the WebAssembly reference types' case (`WASM_REF_TYPE`); the `BlockPointer` case |
| RG1; R-a.d Reads | clang/lib/CodeGen/CodeGenFunction.cpp | 238-296 | `CodeGenFunction::getEvaluationKind` |
| RG12; R-a.d Reads | clang/lib/CodeGen/CodeGenFunction.cpp | 1486-1704; 1664-1684 | `CodeGenFunction::GenerateCode`: C++'s missing-return path, whose trap is emitted only at -O0 (1676-1678) |
| R-a.d Reads | clang/lib/CodeGen/CodeGenFunction.cpp | 2565-2724 | `CodeGenFunction::EmitVariablyModifiedType` |
| RG2, RG5; R-a.d Reads | clang/lib/CodeGen/CGExprScalar.cpp | 2629-3242; 2671; 2929-2934; 3155-3160 | `ScalarExprEmitter::VisitCastExpr`: `CK_BitCast`; `CK_NullToPointer`; OpenCL's `CK_ZeroToOCLOpaqueType` |
| RG3; R-a.d Reads | clang/lib/CodeGen/CGDecl.cpp | 2015-2154 | `CodeGenFunction::EmitAutoVarInit` |
| RG3; R-a.d Reads | clang/lib/CodeGen/CGDecl.cpp | 1878-1888 | `CodeGenFunction::getAutoVarInitKind` |
| RG3; R-a.d Reads | clang/lib/CodeGen/CGDecl.cpp | 1494-1798; 1652-1678 | `CodeGenFunction::EmitAutoVarAlloca`: the bypass rule (`Bypasses.IsBypassed` with `getAutoVarInitKind`) |
| RS29; R-a.d Reads | clang/lib/CodeGen/CGStmt.cpp | 2902-2918 | `emitMSILAsmStmt` (its comment from 2879; `err_msil_asm_unsupported` at 2915) |
| RG7; R-a.d Reads | clang/lib/CodeGen/CGDebugInfo.cpp | 4409-4506 | `CGDebugInfo::CreateTypeNode` |
| RG7 | clang/lib/CodeGen/CGDebugInfo.cpp | 909-1237; 1118-1125 | `CGDebugInfo::CreateType(const BuiltinType *)`: the WebAssembly reference types' case |
| RG9; R-a.d Reads | clang/lib/CodeGen/CGMSILManagedCalls.cpp | 149-174; 157-161 | `getMSILManagedCallTypeText`: the canonical unqualified type, printed as text |
| R-a.d Reads | clang/lib/CodeGen/CGMSILManagedCalls.cpp | 293-300; 696-722 | `declareMSILManagedCallWithForwarders`; `emitMSILManagedCallsMetadata` |
| RG6 | clang/lib/CodeGen/Targets/X86.cpp | 2941-3071; 2970-2978 | `X86_64ABIInfo::classifyArgumentType`: the MEMORY class goes to `getIndirectResult` |
| RG6 | clang/lib/CodeGen/Targets/X86.cpp | 2423-2485; 2433-2442 | `X86_64ABIInfo::getIndirectResult`: a non-aggregate is passed directly |
| RG6 | clang/lib/CodeGen/Targets/X86.cpp | 3581-3749; 3748 | `WinX86_64ABIInfo::classify`: the closing `getDirect()` |
| RG6 | clang/lib/CodeGen/Targets/AArch64.cpp | 372-553; 390 | `AArch64ABIInfo::classifyArgumentType`: the non-aggregate path |
| RG6 | clang/lib/CodeGen/Targets/MSIL.cpp | 64-81 | `MSILPortableABIInfo::classifyArgumentType` (corrected, 8.10) |
| RH4 | clang/lib/CodeGen/ItaniumCXXABI.cpp | 3672-3830; 3964-4147 | `ItaniumRTTIBuilder::BuildVTablePointer` and `ItaniumRTTIBuilder::BuildTypeInfo`, the switches whose cases RH4's ClangIR cases follow (`BlockPointer` at 3719 and 4020) |
| RH4; R-a.b Reads | clang/lib/CIR/CodeGen/CIRGenFunction.cpp | 79-137; 1698-1838 | `CIRGenFunction::getEvaluationKind` (switch at 82); `CIRGenFunction::emitVariablyModifiedType` (switch at 1708) |
| RH4 | clang/lib/CIR/CodeGen/CIRGenItaniumCXXABI.cpp | 688-786; 1158-1355 | `vTableClassNameForType` (switch at 699); `CIRGenItaniumRTTIBuilder::buildTypeInfo` (switch at 1188) |
| RH4; R-a.b Reads | clang/lib/CIR/CodeGen/CIRGenTypes.cpp | 306-684; 673-677 | `CIRGenTypes::convertType`: the default case, `errorNYI` with "processing of type" |

### 8.4 clang: driver, frontend, headers and diagnostics

| Cited by | File | Lines | Symbol: what is there |
|---|---|---|---|
| RC6 | clang/lib/Frontend/CompilerInvocation.cpp | 4065-4761; 4748 | `CompilerInvocation::ParseLangArgs`: HLSL on another target is `err_drv_hlsl_unsupported_target` |
| RC7 | clang/lib/Driver/ToolChains/Clang.cpp | 5258-8740; 7601-7611 | `Clang::ConstructJob`: the `-fblocks` default (`TC.IsBlocksDefault()`) |
| RI1 | clang/lib/Driver/ToolChains/Clang.cpp | 5258-8740; 5855-5859 | `Clang::ConstructJob`: `-disable-llvm-verifier` by default in builds without assertions |
| RI4 | clang/lib/Driver/ToolChains/Clang.cpp | 5258-8740; 8236-8237 | `Clang::ConstructJob`: `-fextend-variable-liveness=all` by default at -Og |
| R-a.a Checks | clang/lib/Basic/Targets/MSIL.h | 126-138; 207-219 | `MSILTargetInfo<Native>::getTargetDefines` (`__MSIL__` at 129) and `MSILPortableTargetInfo::getTargetDefines` (`__MSIL__` at 209) |
| RT1; R-a.a Reads | clang/lib/Headers/CMakeLists.txt | 393-403; 411-419 | `openmp_wrapper_files`; `llvm_libc_wrapper_files` |
| RT1; R-a.a Reads | clang/lib/Headers/CMakeLists.txt | 443-465; 480-485 | `copy_header_to_output_dir`; the copy loop over `files` and the wrapper lists (481-482) |
| RT1; R-a.a Reads | clang/lib/Headers/CMakeLists.txt | 659-662; 664-667; 857-861 | the install rules: `llvm_libc_wrapper_files` to `${header_install_dir}/llvm_libc_wrappers` and `openmp_wrapper_files` to `${header_install_dir}/openmp_wrappers` (component `clang-resource-headers`), and `openmp_wrapper_files` again (component `openmp-resource-headers`) |
| RT1 | clang/lib/Headers/CMakeLists.txt | 165-167; 335-357; 634-637 | `webassembly_files` (`wasm_simd128.h`); `files`, which holds it (354); `install(FILES ${files} ${generated_files} DESTINATION ${header_install_dir} COMPONENT clang-resource-headers)` |
| RT1; R-a.a Reads | clang/include/clang/Basic/Attr.td | 504-506; 2655-2677 | `TargetMSIL` (`T.isOSBinFormatMSIL()`); MC-1's `MSILMethod` record, with its fake `StringArgument<"Conversions", /*opt=*/1, /*fake=*/1>` (2666) |
| 5.21.12; R-a.a Reads | clang/include/clang/Basic/DiagnosticSemaKinds.td | 10877-10933; 10930-10933 | MC-1's block ("Managed calls from C and C++: msil_method and msil_catch."); its last entry, `err_msil_method_forwarder_clash` |
| RS4, RS27 | clang/include/clang/Basic/DiagnosticSemaKinds.td | 9147 | `err_typecheck_convert_incompatible` |
| RS19 | clang/include/clang/Basic/DiagnosticSemaKinds.td | 9933-9935 | `err_cannot_pass_to_vararg` |
| RS8 | clang/include/clang/Basic/DiagnosticSemaKinds.td | 9996-9997 | `err_typecheck_expect_scalar_operand` |
| 5.21.12 | clang/include/clang/Basic/DiagnosticCommonKinds.td | 299-301 | `err_msil_asm_unsupported` |
| RG10 | clang/include/clang/Basic/DiagnosticASTKinds.td | 1092 | `err_ms_mangle_unsupported` ("cannot mangle this %0 yet") |

### 8.5 LLVM core

| Cited by | File | Lines | Symbol: what is there |
|---|---|---|---|
| RI1; R-a.a Reads | llvm/lib/IR/Type.cpp | 1016-1111; 1105-1108 | `getTargetTypeInfo`: `wasm.funcref` and `wasm.externref` (`ptr` layout; HasZeroInit, CanBeGlobal, CanBeLocal) |
| RI1 | llvm/lib/IR/Verifier.cpp | 712-848; 826-828 | `Verifier::visitGlobalVariable`: a global may not hold a target type that cannot be global |
| RI2; R-a.a Reads | llvm/lib/CodeGen/ValueTypes.cpp | 289-340; 311-312 | `MVT::getVT`: `wasm.externref` is `MVT::externref` |
| RI2; R-a.a Reads | llvm/lib/CodeGen/ValueTypes.cpp | 258-282; 274 | `EVT::getTypeForEVT`: `MVT::externref` is `wasm.externref` |
| RI2 | llvm/lib/CodeGen/SelectionDAG/SelectionDAGBuilder.cpp | 1907-2110; 2046-2054 | `SelectionDAGBuilder::getValueImpl`: a null `externref` becomes `llvm.wasm.ref.null.extern` |
| RB10 | llvm/lib/CodeGen/TargetPassConfig.cpp | 937-958; 948-955 | `TargetPassConfig::addISelPasses`: `addIRPasses`, then `addCodeGenPrepare`, `addPassesToHandleExceptions` and `addISelPrepare` |
| RB10 | llvm/lib/CodeGen/TargetPassConfig.cpp | 826-848 | `TargetPassConfig::addISelPrepare` |
| RB10; R-a.f Reads | llvm/lib/Transforms/Utils/PromoteMemoryToRegister.cpp | 1236-1243; 66-103 | `PromoteMemToReg`; `isAllocaPromotable` |
| RB11 | llvm/lib/Transforms/Utils/FunctionImportUtils.cpp | 260-365; 318-327 | `FunctionImportGlobalProcessing::processGlobalForThinLTO`: a promoted local becomes external, with hidden visibility |
| RB11 | llvm/include/llvm/IR/GlobalValue.h | 573-579 | `GlobalValue::externalize` |
| RB11 | llvm/lib/LTO/LTOBackend.cpp | 520-573; 563-566 | `splitCodeGen`: its `splitModule` call |
| RI1, RI6, RG4 | llvm/docs/LangRef.md | 4862; 4874-4876; 4878-4882 | "Target Extension Type": the only constants are `zeroinitializer`, `undef` and `poison`, and other values come from target-specific intrinsics and functions; no `bitcast`, `ptrtoint`, `inttoptr` or `icmp` |
| RB10 | (outside the tree) | - | LLVM issue 81575, a public issue, not a file; not checked here |

### 8.6 LLVM's CLI headers and the MSIL target

| Cited by | File | Lines | Symbol: what is there |
|---|---|---|---|
| RT2; R-a.a Reads | llvm/include/llvm/CLI/ManagedSignature.h | 1023-1067 | `cli::parseManagedTypeName` |
| RT3; R-a.a Reads | llvm/include/llvm/CLI/ManagedSignature.h | 1162-1207; 1185-1195 | `cli::checkManagedCallShape`: `IsObject`, which takes `class System.Object` as `object`, serves only the delegate-constructor check |
| RM1; R-a.a Reads | llvm/include/llvm/CLI/ManagedSignature.h | 1226-1242 | `enum class cli::ManagedConv` |
| RM8; R-a.a Reads | llvm/include/llvm/CLI/ManagedSignature.h | 190; 200-215 | `ManagedCallUniqPostfix` (`".__uniq."`); `cli::getManagedCallUniqPostfix` |
| RM8; R-a.a Reads | llvm/include/llvm/CLI/ManagedSignature.h | 1101-1123 | `cli::checkManagedMemberName` (corrected, 8.10) |
| RM6; R-a.a Reads | llvm/include/llvm/CLI/ManagedCallIR.h | 91-95; 110-166 | `isManagedHandleType`; `fitsManagedConversion` (its `Obj` case at 150-151) |
| RI6 | llvm/include/llvm/CLI/TrailerDefs.h | 102-104 | `cli::isToolchainName` |
| RB1; R-a.a Reads | llvm/lib/Target/MSIL/MCTargetDesc/MSILMCTypeUtilities.h | 34-40 | `enum class MSIL::ValType`: I32, NativeInt, I64, F32, F64 |
| RB1; R-a.e Reads | llvm/lib/Target/MSIL/MCTargetDesc/MSILMCTypeUtilities.cpp | 34-47 | `MSIL::toValType` |
| RB1; R-a.g Reads | llvm/lib/Target/MSIL/MSILAsmPrinter.cpp | 239-253 | `MSIL::toTypeSig` |
| RB5 | llvm/lib/Target/MSIL/MSILAsmPrinter.cpp | 2524-3156; 2786-2802 | `MSILAsmPrinter::emitObjectEnd`: its symbol loop over functions |
| RB1; R-a.g Reads | llvm/lib/Target/MSIL/MCTargetDesc/MSILObjectWriter.cpp | 72-86 | `toTypeSig` |
| 5.21.16 | llvm/lib/Target/MSIL/MCTargetDesc/MSILObjectWriter.cpp | 329-374; 352-358 | `MSILObjectWriter::recordRelocation`: "a CIL object has no user strings" |
| 7.1 | llvm/lib/Target/MSIL/MCTargetDesc/MSILObjectWriter.cpp | 438-859; 787-788 | `MSILObjectWriter::writeObject`: the method bodies, "never InitLocals (decision D13)" |
| RI3; 7.1 | llvm/lib/Target/MSIL/MCTargetDesc/MSILTargetStreamer.cpp | 197-212; 202-204; 207 | `MSILTargetAsmStreamer::emitLocals`: `.locals (`, never `init`; a local of a class or value type is a fatal internal error |
| RB2; R-a.e Reads | llvm/lib/Target/MSIL/MSILISelLowering.cpp | 56-440; 76-79 | `MSILTargetLowering::MSILTargetLowering`: the register classes |
| RB2; R-a.e Reads | llvm/lib/Target/MSIL/MSILISelLowering.cpp | 865-879; 871-875 | `MSILTargetLowering::EmitInstrWithCustomInserter`: the `SELECT_*` cases |
| RB5; R-a.e Reads | llvm/lib/Target/MSIL/MSILISelLoweringCall.cpp | 293-650 | `MSILTargetLowering::LowerCall` |
| RB2, RV26 | llvm/lib/Target/MSIL/MSILRegisterInfo.td | 25-26; 59-62 | `MSILRegClass`; the four register classes `I32`, `I64`, `F32`, `F64` |
| RB3; R-a.a Reads | llvm/lib/Target/MSIL/MSILInstrInfo.td | 135-140; 153-195 | multiclass `ARGUMENT` (its four `defm` at 141-144); multiclass `LOCAL` (196-199) |
| RB3; R-a.a Reads | llvm/lib/Target/MSIL/MCTargetDesc/MSILMCTargetDesc.h | 225-235; 237-247; 249-259; 261-271 | `isArgument`; `isCopy`; `isTee`; `isScalarConst` |
| RB3; R-a.e Reads | llvm/lib/Target/MSIL/MSILUtilities.cpp | 137-151 | `MSIL::getCopyOpcodeForRegClass` |
| RB3; R-a.e Reads | llvm/lib/Target/MSIL/MSILInstrInfo.cpp | 94-188 | `getInvertedBranch` |
| RB3; R-a.e Reads | llvm/lib/Target/MSIL/MSILMCInstLower.cpp | 482-697; 494-529 | `MSILMCInstLower::lowerImpl`: the `ARG_GET_*` to `LOCAL_SET_*` cases |
| RB6 | llvm/lib/Target/MSIL/MSILMachineFunctionInfo.cpp | 126-133 | `parseMVT` |
| RB7; R-a.e Reads | llvm/lib/Target/MSIL/MSILExplicitLocals.cpp | 90-100; 104-114; 118-128; 132-142; 145-155 | `getDropOpcode`; `getLocalGetOpcode`; `getLocalSetOpcode`; `getLocalTeeOpcode`; `typeForRegClass` |
| RB7; R-a.e Reads | llvm/lib/Target/MSIL/MSILExplicitLocals.cpp | 215-477; 278-282; 447-459 | `explicitLocals`: its `IMPLICIT_DEF` handling; the definition of the locals |
| RB7; R-a.e Reads | llvm/lib/Target/MSIL/MSILRegStackify.cpp | 103-129 | `convertImplicitDefToConstZero` |
| RB7; R-a.e Reads | llvm/lib/Target/MSIL/MSILRegStackify.cpp | 297-402; 332 | `isSafeToMove`: undef operands are skipped |
| RB8 | llvm/lib/Target/MSIL/MSILRegColoring.cpp | 220-322; 282-287 | `regColoring`: a color is reused only within one register class |
| RB10; R-a.f Reads | llvm/lib/Target/MSIL/MSILTargetMachine.cpp | 370-375 | `MSILPassConfig::addISelPrepare`: AtomicExpand, then `TargetPassConfig::addISelPrepare()` |
| RB10; R-a.f Reads | llvm/lib/Target/MSIL/MSILTargetMachine.cpp | 286-353; 314-318 | `MSILPassConfig::addIRPasses`: P3 and P4 lower invokes and delete the landing pads ("addPassesToHandleExceptions adds nothing") |
| RB11; R-a.f Reads | llvm/lib/Target/MSIL/MSILPrepareIR.cpp | 267-269 | `isScalableOrTargetExtType` |
| RB11; R-a.f Reads | llvm/lib/Target/MSIL/MSILPrepareIR.cpp | 1065-1318 | `diagnoseDefinition` |
| RB11; R-a.f Reads | llvm/lib/Target/MSIL/MSILPrepareIR.cpp | 2238-2431; 2313 | `MSILPrepareIRLegacy::runOnModule`: its `lowerEmptyInlineAsm` call |
| RB11; R-a.f Reads | llvm/lib/Target/MSIL/MSILPrepareIR.cpp | 970-1054 | `lowerEmptyInlineAsm` |
| RB11; R-a.f Reads | llvm/lib/Target/MSIL/MSILManagedCalls.cpp | 382-412; 383-386 | `refuseNonCallUses`: an `llvm.used` or `llvm.compiler.used` listing "is not an address that the program takes" |
| RB12; R-a.f Reads | llvm/lib/Target/MSIL/MSILFixFunctionBitcasts.cpp | 288-386; 370-377 | `createWrapper`: a type mismatch gives a `_bitcast_invalid` wrapper whose body is `unreachable` |
| RB12; R-a.f Reads | llvm/lib/Target/MSIL/MSILFixFunctionBitcasts.cpp | 484-496; 507-528 | `createTrapWrapper`; `convertValue` |
| RM8; R-a.g Reads | llvm/lib/Target/MSIL/MSILManagedCalls.cpp | 107-280; 124 | `MSIL::parseManagedCall`: its `cli::checkManagedMemberName` call |
| RM8 | llvm/lib/Target/MSIL/MSILManagedCalls.cpp | 744-754 | `declaredType`, a member of `StubWriter` (class at 638) |
| RM7; R-a.g Reads | llvm/lib/Target/MSIL/MSILManagedCalls.cpp | 1119-1172; 1145-1147 | `StubWriter::emitAccess`: the virtual test, `callvirt` for `ThisObj` |
| RM7, RM12; R-a.g Reads | llvm/lib/Target/MSIL/MSILManagedCalls.cpp | 1260-1278 | `StubWriter::emitZero` (MX2) |
| RM3, RM13; R-a.g Reads | llvm/lib/Target/MSIL/MSILManagedCalls.cpp | 548-562; 967-990; 1018-1115 | `layoutLocals`; `StubWriter::emitLengthLoop` (corrected, 8.10); `StubWriter::emitArgument`, its `UTF8`, `UTF32` and `UTF16` cases at 1062-1094 (S2's `String_ctor_PChar` at 1091) |

### 8.7 msil-ld (lld/MSIL)

| Cited by | File | Lines | Symbol: what is there |
|---|---|---|---|
| RL1, RM8; R-a.h Reads | lld/MSIL/ManagedCalls.cpp | 1873-1936; 1876 | `isValidManagedCall`: its `checkManagedMemberName` call |
| RM8 | lld/MSIL/ManagedCalls.cpp | 734-741 | `isManagedCallStub`: a `__msil_mc.` method whose symbol is LOCAL |
| RL2; R-a.h Reads | lld/MSIL/ManagedCalls.cpp | 1958-2024 | `checkDeclaration` (MK3) |
| RL3; R-a.h Reads | lld/MSIL/ManagedCalls.cpp | 453-583; 585-728 | `scanStubSites`; `checkStub` |
| RL4; R-a.h Reads | lld/MSIL/ManagedCalls.cpp | 879-891; 893-927 | `lld::msil::checkReservedManagedCallNames`, for an object file and for a bitcode file |
| RL4; R-a.h Reads | lld/MSIL/LTO.cpp | 961-1383; 1085-1086; 1150 | `annotateImports`: managed calls are skipped; the `reportUnbound` call for functions (the one at 1273 is for data) |
| RL5; R-a.h Reads | lld/MSIL/Synthetic.cpp | 551-580 | `OutputModule::mapStandAloneSig` |
| RL5; R-a.h Reads | lld/MSIL/SymbolTable.cpp | 880-883; 885-899 | `signatureNamesType`; `namesType`, true for a type with modifiers, a CLASS, VALUETYPE or GENERICINST, a FNPTR whose signature names a type, or an operand that does |
| RL7; R-a.h Reads | lld/MSIL/Driver.cpp | 1462-1548; 1503; 1525-1526 | `readConfigs`: `--lto-partitions` (default 1); `--thinlto-cache-dir` and `--thinlto-jobs` |

### 8.8 Harness files (R-a.i's Reads)

| Cited by | File | Lines | Symbol: what is there |
|---|---|---|---|
| RH5; R-a.i Reads | msil/utils/ci/check-upstream-hooks.py | 536; 588; 628; 1151; 146-147 | `strip_code`; `_switch_bodies`; `switch_problems`, the XCOFF scan; `self_test`; `XCOFF_LABEL_RE` |
| R-a.i Reads | msil/utils/ci/lit-legs.ps1 | 360 | `$checkMsil` |
| R-a.i Reads | msil/utils/ci/lit-legs.sh | 184 | `check_msil` |
| R-a.i Reads | msil/utils/upstream-hooks.txt | 73-78; 79; 115-180 | the header's `allow` and `min-pass` entries; the min-pass comment, ending in `min-pass 383` (180); MC-1's blocks include `hook clang/lib/Sema/SemaDecl.cpp` (613) and `hook clang/lib/Sema/SemaDeclAttr.cpp` (675) |
| RH5; R-a.i Reads | msil/utils/ci/upstream-hooks.ps1 | 20 | the header's manifest leg (`check-upstream-hooks.py --check`) |
| RH5; R-a.i Reads | msil/utils/run-m1-gate.ps1 | 96-97 | the header's upstream-hooks step |

### 8.9 Documents, tools and counts

| Cited by | File | Lines | Symbol: what is there |
|---|---|---|---|
| RS12, 5.21.12 | llvm/docs/MSIL/Design.md | 123; 125 | section 4, "Memory model"; at HEAD its paragraph begins "The C program never touches the GC heap.", and R-a's plan commit replaces it with the approved text that begins "C memory never holds a GC reference." |
| 5.21.11; 7.1 | llvm/docs/MSIL/Design.md | 136; 139 | section 4.2, "The stack"; the frame rule ending "Frames are not zeroed: `.locals` is never `init`." (unchanged by R-a) |
| RS16 | llvm/docs/MSIL/Design.md | 152 | section 4.5, "Function pointers" |
| 5.21.11; 7 | llvm/docs/MSIL/ImplementationPlan.md | 178; 186; 7751 | decision D13 (frames; `.locals` never `init`); decision D21 (verification policy; ILVerify's allow-list names `InitLocals`); 5.17.16 |
| 5.21.11; 7.3 | msil/utils/ilverify-allowed.txt | 37-38 | "# '.locals' are never 'init' (D13)." and the InitLocals pattern |
| 5.21.11; 7.3 | msil/utils/msil-verify.py | 69-81; 315-319 | pass 2 verifies a copy rewritten to `.locals init (...)`; pass 1 allows PEVerify's message for ".locals without 'init' (D13)" |
| RN9 | libc/src/__support/File/file.h | 37; 57-58; 83-84 | `class File`; `WriteFunc` and `ReadFunc`; the function-pointer members `platform_write` and `platform_read` |

The counts that 5.21 and R-a's text give, checked at the same HEAD:

| What the text says | At HEAD |
|---|---|
| RC5: "The other 18 `BlockPointer` chunk sites of SemaType.cpp" | 20 mentions of `DeclaratorChunk::BlockPointer` in clang/lib/Sema/SemaType.cpp: the 18 sites of 8.1, RC2's own case (4780) and `DeclaratorLocFiller`'s assertion (6322) |
| RV18: `ManagedSignatureTest.cpp` 25 → 31, CLITests 488 → 494 | 25 `TEST` cases in llvm/unittests/CLI/ManagedSignatureTest.cpp; 488 in the 23 test files of `CLITests` |
| 5.21.14: "the eight host triples of `triples.c`" | clang/test/CodeGen/MSIL/triples.c accepts eight `-msil` host triples (RUN lines 10-25) |
| RV26: "four classes become five" | llvm/unittests/Target/MSIL/MSILStackEffectTest.cpp names the four classes I32, I64, F32 and F64 (26; 83-84) |
| RH2 and gate step 25: "82 + 45" | 82 `hook` lines in msil/utils/upstream-hooks.txt |

### 8.10 Corrections to the earlier evidence list

The earlier list gave, for each citation of a draft, the draft's form and the corrected place. Its corrections are kept
here, and the re-check confirms each corrected place at HEAD. Twelve of them moved a citation to another file: the lines
of RS4 to RS7 are in `clang/lib/Sema/SemaExpr.cpp`, not SemaInit.cpp (seven rows); RB3's `lowerImpl` cases are in
MSILMCInstLower.cpp, not MSILMCTargetDesc.h; RB7's `isSafeToMove` is in MSILRegStackify.cpp, not MSILExplicitLocals.cpp;
RB11 rule 10's `lowerEmptyInlineAsm` call is in MSILPrepareIR.cpp, not MSILManagedCalls.cpp; RL4's
`checkReservedManagedCallNames` is in `lld/MSIL/ManagedCalls.cpp` (879-891), not `lld/MSIL/LTO.cpp` (879-930); and the
"Target Extension Type" lines of RI6 are in `llvm/docs/LangRef.md`, not Clang.cpp. Twenty-one more corrected the symbol
or the claim at the same place; for example, RT3's `IsObject` serves only the delegate-constructor check.

This re-check's own corrections, where an earlier range ran past its definition or stopped inside it:

- `MSILPortableABIInfo::classifyArgumentType` is 64-81, not 64-94; 83-94 is `classifyReturnType`.
- `classifyManagedParam` is 271-302, not 271-359; 308-338 is `classifyManagedResult` and 344-359 `classifyManagedThis`.
- `signatureNamesType` is 880-883, not 876-899; 876-878 declare `namesType`, which is defined at 885-899.
- `StubWriter::emitLengthLoop` is 967-990, not 967-996; 992-996 is the comment of `emitThis`.
- `cli::checkManagedMemberName` is 1101-1123, not 1101-1127; 1125-1127 is `ManagedCallArg`.
- `checkStub` ends at 728, not 731; `checkDeclaration` at 2024, not 2025; `createWrapper` at 386 (377 ends its
  type-mismatch trap); `CodeGenFunction::GenerateCode`'s missing-return path at 1684, not 1683; and
  `processGlobalForThinLTO`'s promotion starts at 318, not 315, which is inside an assertion.
- Where the earlier list gave a shorter range under a function's name, the whole definition is given:
  `CodeGenFunction::getEvaluationKind` is 238-296 (the list gave 238-273), `CodeGenFunction::EmitAutoVarInit`
  2015-2154 (2015-2083), and `getManagedCallUniqPostfix` 200-215, with its constant at 190 (180-215).

Dropped, because 5.21 does not cite them: `CIRGenModule::getTargetCIRGenInfo`; SemaType.cpp:4734, a type test already
dropped from RC5; ImplementationPlan.md's own lines for its section 12 MC-1 gate rows, its section 4 rebuild
estimates, its section 7 and P-2's 5.19 items; and Design.md:27 and :316 (Design.md section 1's first non-goal and
section 10's risk 5), which the plan's section 12 rows and the Design.md texts change, not 5.21. SemaType.cpp:4786 is
now part of RC2's row.

Added, for citations made since that list: `getTargetDefines` in `clang/lib/Basic/Targets/MSIL.h`; the header lists,
copy loop and install rules of `clang/lib/Headers/CMakeLists.txt`; `CursorVisitor`, `VisitBlockPointerTypeLoc`,
`DEFAULT_TYPELOC_IMPL` and `TypeLocVisitor::VisitTypeLoc`; ItaniumCXXABI.cpp's two builders; the neighbours of
`classifyManagedParam`; the functions that the Reads entries of R-a.a to R-a.i name; the D13, D21, streamer, object
writer and `msil-verify.py` lines behind section 7; and the counts.
