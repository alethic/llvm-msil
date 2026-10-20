# MSIL recon: SelectionDAG instruction selection and lowering (`isel`)

Scope: Design.md section 12, milestone 1 ("fib.c runs on dotnet"). This report covers four things:

- How the WebAssembly target in this checkout lowers and selects code.
- The CIL each LLVM operation should become. The CIL semantics were checked by running IL on .NET 8, .NET 10 and .NET Framework 4.8.
- A complete `setOperationAction` proposal.
- The call-signature mechanism, the MachineInstr catalogue with stack effects, and TableGen and C++ snippets for the lowering hooks.

Read Design.md first. This report assumes its decisions: a WebAssembly-shaped SelectionDAG target, native triple plus `-msil`, i64 pointers on LP64/LLP64, and ILAsm text from `-S`.

Related sibling reports, which agree with this one where they overlap:
- `passes-frame.md` covers the pass pipeline, the frame (`FP` fake physreg plus `localloc`), ExplicitLocals and `.maxstack`.
- `registry-tm.md` covers target selection, the DataLayout and the TargetMachine.

Scratch experiments for this report are in `<scratch>\recon\isel\`:
- `cilsem.il`, `cilsem2.il`: CIL semantics probes.
- `bclprobe.il`, `nuintprobe.il`, `fence.il`: BCL member resolution on .NET 8.
- `mixver.il`, `mixsem.il`: int32 / native int mixing, under PEVerify and at run time.

They were built with `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe`. The `.dll` builds ran with `dotnet` (net8.0 and net10.0 runtimeconfig) and the `.exe` builds on .NET Framework 4.8. PEVerify is `C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\PEVerify.exe`. ILVerify is not installed. To get it later, download the NuGet global tool `dotnet-ilverify` (for example 8.0.x or 9.0.x) or the library `Microsoft.ILVerification`. Nothing was downloaded for this report.

---

## 0. Decisions proposed in this report (TL;DR)

1. **Four register classes, as in wasm minus vectors and references.** `I32` is CIL `int32`, `I64` is CIL **`native int`**, `F32` is `float32` and `F64` is `float64`.
   - Every 64-bit vreg is `native int`, never `int64`, on the 64-bit triples.
   - Reason: PEVerify reports `native int`/`int64` mixing as a type error ("Unexpected type on the stack"), while `native int` mixed with `int32` verifies cleanly (section 3.3).
   - A pointer and an `i64` are the same MVT in the DAG, so one consistent CIL type for the 64-bit class is the only way to avoid conversions that ISel cannot place.
2. **One MachineInstr = one CIL opcode, with few exceptions.** Multi-opcode results are built at ISel from single-opcode MIs. For example, `setne` becomes `(CEQ (CEQ a b) (CONST_I32 0))`, which prints as `ceq; ldc.i4.0; ceq`.
   - The remaining "macro" MIs, such as `CONST_I64`, `GLOBAL_ADDR`, `TRAP`, `BR_TABLE` and BCL calls with type adapters, are listed with their peak stack depth (section 9).
3. **The stack order is the MI operand order.** Register uses are pushed in operand order and the last use is on top. That is exactly CIL's order for every opcode used here, including `stind` (addr, value), `cpblk` (dst, src, len) and `calli` (args, then ftn).
4. **Calls copy wasm.** `MSILISD::CALL` is split by `Select` into `CALL_PARAMS`/`CALL_RESULTS` and recombined by a custom inserter into `CALL` or `CALL_INDIRECT`. The callee goes last for `CALL_INDIRECT`, as for `calli`.
   - Signatures follow the wasm rules:
     - Definitions use `MSILFunctionInfo` Params/Results.
     - Direct calls and `ldftn` use `computeSignatureVTs` on the callee's IR type.
     - `calli` and external-symbol calls use the call MI's operand register classes.
   - Section 8 covers the details.
5. **Compares and branches.**
   - `setcc` maps to `ceq`/`cgt`/`cgt.un`/`clt`/`clt.un`, plus `ldc.i4.0; ceq` for the negated forms.
   - FP `SETO`/`SETUO`/`SETONE`/`SETUEQ` are Expand.
   - `brcond (setcc)` is fused into one CIL branch (`beq`, `bne.un`, `blt[.un]`, `ble[.un]`, `bgt[.un]`, `bge[.un]`). All 10 integer and all 10 non-trivial FP predicates map to exactly one branch (verified, including NaN). The branch-inverse table differs for FP and integer (section 11.7).
6. **No CIL `select`.** `SELECT_{I32,I64,F32,F64}` are `usesCustomInserter` pseudos expanded into a branch triangle plus PHI. `SELECT_CC` and `BR_CC` are Expand.
7. **Memory.**
   - `ldind.*`/`stind.*` take a single address operand: there is no offset field and no addressing modes.
   - Alignment and volatility come from the MemSDNode through a root-aware ComplexPattern into a `memflags` immediate. The printer turns it into the `unaligned. N` / `volatile.` prefixes.
   - `i64` extending loads and truncating stores are Expand, as are `f64`←`f32` and f16.
8. **memcpy, memset and memmove never become libcalls.**

   | Operation | Constant length ≤ 2^32−1 | Variable length |
   |---|---|---|
   | memcpy | `cpblk` | `NativeMemory.Copy` |
   | memset | `initblk` | `NativeMemory.Fill` |
   | memmove | `NativeMemory.Copy` | `NativeMemory.Copy` |

   This goes through `MSILSelectionDAGInfo`. The DAG node operand order matches the BCL parameter order.
9. **Conversions.** `uitofp i64→f32` is Custom because `conv.r.un; conv.r4` double-rounds on .NET 8 and .NET Framework (verified, fixed in .NET 10). It falls back to LLVM's `__floatundisf` expansion. Every other int↔fp conversion is one or two CIL opcodes.
10. **Libcalls.**
    - Make `MUL_I128` (and `__mulo*`) unavailable through `MSILSubtarget::initLibcallLoweringInfo`, so i128 multiply and i64 overflow multiply expand inline.
    - Make `SHL_PARTS`/`SRA_PARTS`/`SRL_PARTS` Custom with `expandShiftParts`, so i128 shifts never call `__ashlti3`.
    - The remaining helpers a program can need are listed in section 7.2. None are reachable from the milestone-1 test programs.
11. **BCL intrinsics (net8.0 profile).**
    - Mapped to BCL calls that RyuJIT treats as intrinsics: `ctlz`, `cttz`, `ctpop`, `bswap`, `rotl`, `rotr`, `fabs`, `fsqrt`, the rounding ops, `fcopysign`, `fma`, `fminimum`/`fmaximum` (`Math.Min`/`Max`, IEEE semantics verified on .NET 8), `fminnum`/`fmaxnum` (`double.MinNumber`/`MaxNumber`) and the bitcasts (`BitConverter`).
    - Every referenced member was resolved on .NET 8, with its reference-assembly name (section 5.9).
    - The net48 profile uses Expand or LibCall instead.
12. **Required target settings.**
    - `TrapUnreachable = true` and `NoTrapAfterNoreturn = false`: CIL may not fall off the end of a method.
    - `TRAP` prints `ldnull; throw`.
    - `usesPhysRegsForValues() == false`.
    - `MSILFixFunctionBitcasts` must run, because a direct `call` token must match the callee's definition exactly.

---

## 1. Sources read

All paths are relative to `llvm/lib/Target/WebAssembly/` unless stated otherwise. Line numbers are from this checkout.

| File | What matters for MSIL |
|---|---|
| `WebAssemblyISelLowering.cpp` | Constructor 43-447. `getScalarShiftAmountTy` 493-511. `LowerFPToInt` 516-607 (a diamond-building custom inserter, the template for SELECT). `LowerMemcpy`/`LowerMemset` 611-784. `LowerCallResults` 786-911. `EmitInstrWithCustomInserter` 913-957. `isLegalAddressingMode` 1007-1023. `allowsMisalignedMemoryAccesses` 1025-1037. `isIntDivCheap` 1039-1044. `isOffsetFoldingLegal` 1058-1063. `getSetCCResultType` 1065-1080. `fail` 1259. `callingConvSupported` 1266-1278. `LowerCall` 1280-1588. `CanLowerReturn` 1590-1596. `LowerReturn` 1598-1626. `LowerFormalArguments` 1628-1712. `LowerOperation` 1752-1835. `LowerCopyToReg` 2008-2032. `LowerFrameIndex` 2034-2038. `LowerRETURNADDR`/`LowerFRAMEADDR` 2040-2071. `LowerGlobalAddress` 2124-2168. `LowerExternalSymbol` 2170-2180. `LowerJumpTable` 2182-2190. `LowerBR_JT` 2192-2216. `LowerVASTART` 2218-2230. |
| `WebAssemblyISelLowering.h` | The hook list (lines 24-137). |
| `WebAssemblyISelDAGToDAG.cpp` | `Select` 204-475. CALL split 425-467. `SelectAddrOperands` 531-589. `SelectAtomicAddrOperands` with `WantsRoot` 624-644. |
| `WebAssemblySelectionDAGInfo.{h,cpp}` | `WebAssemblyISD::CALL`/`RET_CALL` are hand-written enum values after `GENERATED_OPCODE_END` (h:23-28), because they have variadic results. Every other node comes from `-gen-sd-node-info` (`CMakeLists.txt`: `WebAssemblyGenSDNodeInfo.inc`). memcpy/memset hooks are at cpp:41-90. |
| `WebAssemblyInstrInfo.td` | SDNodes 110-157. `ARGUMENT_*` 312-325. COPY/TEE/LOCAL_*/DROP 329-410. CONST 412-429. Wrapper patterns 431-469. |
| `WebAssemblyInstrFormats.td` | The `I` multiclass (register form plus `_S` stack form) 55-62. |
| `WebAssemblyInstrCall.td` | ADJCALLSTACK 19-24. CALL_PARAMS/CALL_RESULTS 36-49. CALL 54-58. CALL_INDIRECT 60-67. |
| `WebAssemblyInstrControl.td` | BR_IF/BR_UNLESS/BR 14-27. brcond patterns 29-34. BR_TABLE 48-63. RETURN 98-100. UNREACHABLE 108-120. |
| `WebAssemblyInstrMemory.td` | AddrOps ComplexPatterns 33-34. Loads 37-119. Stores 122-187. |
| `WebAssemblyInstrInteger.td` | Binary and compare multiclasses 14-45. Defs 49-94. Shift-mask patterns 97-115. Select 117-138. |
| `WebAssemblyInstrFloat.td` | 47-127. |
| `WebAssemblyInstrConv.td` | 15-240. |
| `WebAssemblyInstrBulkMemory.td` | MEMCPY/MEMSET SDNodes 1-80. |
| `WebAssemblyMachineFunctionInfo.{h,cpp}` | Params/Results/Locals h:33-35. VarargVreg h:51. stackify bits h:120-137. `computeLegalValueVTs` cpp:40-60. `computeSignatureVTs` cpp:62-112. `signatureFromMVTs` cpp:120-127. |
| `WebAssemblyMCInstLower.cpp` | `GetGlobalAddressSymbol` (signature from the IR FunctionType) 70-109. `lowerTypeIndexOperand` 165-178. `CALL_INDIRECT` type from the operand register classes 274-301. |
| `WebAssemblyRegisterInfo.cpp` | `eliminateFrameIndex` 52-136. |
| `WebAssemblyRegStackify.cpp` | Trapping-op exemptions 185-247. Remat rule `isAsCheapAsAMove && isTriviallyReMaterializable` 268-270. |
| `WebAssemblyTargetMachine.cpp` | `TrapUnreachable = true`, `NoTrapAfterNoreturn = false` (184-185). |
| `WebAssemblyTargetMachine.h` | `usesPhysRegsForValues() == false` (63). |
| `llvm/lib/CodeGen/SelectionDAG/LegalizeIntegerTypes.cpp` | `ExpandIntRes_MUL` 4560 (libcall only if available, else `forceExpandMultiply`). `ExpandIntRes_Shift` 5077-5180 (SHL_PARTS if Legal or Custom, else libcall, else inline). `ExpandIntRes_SDIV` (always a libcall). |
| `llvm/lib/CodeGen/SelectionDAG/LegalizeDAG.cpp` | Custom→Expand fall-through 1352-1392. `[SU]INT_TO_FP` actions are keyed on the operand type (LegalizeOp). FRAMEADDR/RETURNADDR "lie about being legal" (1118-1130). `ExpandLegalINT_TO_FP` has the `__floatundisf` algorithm for unsigned i64→f32. |
| `llvm/lib/CodeGen/SelectionDAG/TargetLowering.cpp` | `makeLibCall` (225: fatal error if the impl is Unsupported). `expandShiftParts`. `expandFMINNUM_FMAXNUM` (no NaN-safe expansion; falls back to a libcall). `forceExpandWideMUL` 12833. `expandUINT_TO_FP`. |
| `llvm/lib/CodeGen/TargetLoweringBase.cpp` | `initActions`: the defaults. Everything is Legal except the long Expand lists. In this checkout, **`CTLZ_ZERO_POISON`/`CTTZ_ZERO_POISON` replace the old `*_ZERO_UNDEF`**. FMIN/FMAX and all rounding ops default to Expand. LRINT etc. default to LibCall. |
| `llvm/include/llvm/IR/RuntimeLibcalls.td` | Libcall availability is triple-keyed (`isX86` at 126, `X86SystemLibrary` 3689, `WasmSystemLibrary` 3870). `setTargetRuntimeLibcallSets` takes the **first** matching `SystemRuntimeLibrary`, and the records are ordered by def name. |
| `llvm/include/llvm/CodeGen/TargetSubtargetInfo.h:170` | `initLibcallLoweringInfo(LibcallLoweringInfo&)`, the per-subtarget libcall override (MSP430 example: `MSP430Subtarget.cpp:72`). |
| `git show bd24404718a6^:llvm/lib/Target/MSIL/MSILWriter.cpp` | The old 2.x backend. Its `fcmp` lowering (lines 905-994) is wrong: `ceq; not` yields −1/−2, not 0/1. Do not reuse its opcode sequences. Its type mapping (pointer = `void*`, i1 = `bool`) is not used here. |

---

## 2. How WebAssembly does it (verified in source)

### 2.1 Register model

- `addRegisterClass` for i32/i64/f32/f64 (ctor 62-65).
- Virtual registers survive to the end of codegen, and `usesPhysRegsForValues() == false`.
- Two fake physregs order things: `ARGUMENTS` (used by every `ARGUMENT_*`) and `VALUE_STACK` (block markers).
- `setBooleanContents(ZeroOrOneBooleanContent)` (53). `getSetCCResultType` is `i32` for scalars (1065-1080).

### 2.2 Formal arguments (`ARGUMENT_*`)

`LowerFormalArguments` (1628-1712):
1. Calls `MF.getRegInfo().addLiveIn(WebAssembly::ARGUMENTS)`.
2. For each `ISD::InputArg In`, produces `DAG.getNode(WebAssemblyISD::ARGUMENT, DL, In.VT, getTargetConstant(index))` if `In.Used`, else `UNDEF`.
3. Records `MFI->addParam(In.VT)` for every argument, used or not.
4. For a vararg function, appends one more `ARGUMENT` (the buffer pointer) into a vreg saved as `VarargBufferVreg`, and calls `addParam(PtrVT)`.
5. Finally records the results from `computeSignatureVTs` and asserts that the params match.

ISel turns the node into `ARGUMENT_<vt> %res, <argno>`, defined with `hasSideEffects = 1, Uses = [ARGUMENTS]` (InstrInfo.td:312-325). `WebAssemblyArgumentMove` hoists them to the top of the entry block. ExplicitLocals later maps the vreg to the parameter index and deletes the MI.

### 2.3 Returns

- `LowerReturn` builds `WebAssemblyISD::RETURN(chain, vals...)` (1598-1626).
- `CanLowerReturn` limits the return values to 1 without multivalue (1590-1596). Anything else, such as `{i64,i64}` or `i128` returns, is demoted to a hidden sret pointer by SelectionDAGBuilder.
- `computeSignatureVTs` mirrors that demotion: it drops the results and pushes `PtrVT` to the front of Params (MachineFunctionInfo.cpp:72-81).
- `RETURN` has `variable_ops` and pattern `[(WebAssemblyreturn)]` (InstrControl.td:98-100).

### 2.4 Calls

- `LowerCall` (1280-1588) does five things:
  - Checks the calling convention and handles tail calls.
  - Copies byval args into a new caller stack object with `getMemcpy`, then passes the frame index (1398-1413).
  - Lays out vararg values in a caller stack buffer (1460-1506).
  - Wraps a GlobalAddress callee in `Wrapper(TargetGlobalAddress)` (1508-1518).
  - Emits `WebAssemblyISD::CALL(chain, callee, fixed args..., [vararg buf])` with result VTs `Ins..., Other` (1579-1587).
- `Select` (ISelDAGToDAG.cpp:425-467) splits CALL into two glued MachineNodes:
  - `CALL_PARAMS` (variadic uses; callee first).
  - `CALL_RESULTS` (variadic defs; `usesCustomInserter`).

  It strips the Wrapper when the callee is a Function or an ExternalSymbol, which gives a direct call. Otherwise the wrapped address becomes a register, which gives an indirect call.
- `LowerCallResults` (786-911) merges the two into `CALL` or `CALL_INDIRECT`.
  - For indirect calls it **moves the function pointer to the end** of the operand list (824-844), because `call_indirect` pops the callee last. This matches CIL `calli`.
  - Its operands are the defs, then (indirect only) a type-index placeholder and a table, then the uses.
- `CALL`/`CALL_INDIRECT` are `variadicOpsAreDefs = 1` (InstrCall.td:54-67).
- `ADJCALLSTACKDOWN/UP` exist only so `TargetInstrInfo` has call-frame opcodes. Wasm never emits `CALLSEQ_START`/`CALLSEQ_END` in `LowerCall` (grep finds none).

### 2.5 Call signatures

- **Definitions:** `WebAssemblyFunctionInfo::Params/Results` (MVTs), filled in LowerFormalArguments.
- **Direct calls and function-address constants:** `WebAssemblyMCInstLower::GetGlobalAddressSymbol` (70-109) runs `computeSignatureVTs(FuncTy, F, CurrentFunc, TM, Params, Results)` on the callee's IR `FunctionType`. It then calls `signatureFromMVTs` and attaches the result to the `MCSymbolWasm` (`setSignature`).
  - `computeSignatureVTs` (cpp:62-112) legalizes each IR param/return through `computeLegalValueVTs`: `getNumRegisters` × `getRegisterType`. So an i8 param becomes i32 and an i128 param becomes two i64.
- **Indirect calls:** `MCInstLower::lower` (274-301) builds the type from the call MI's register classes: defs become Returns, explicit register uses become Params, minus the trailing callee.
- **External symbols** (libcalls): `WebAssemblyAsmPrinter::getOrCreateWasmSymbol` (AsmPrinter.cpp:270+) uses a table, `WebAssemblyRuntimeLibcallSignatures.cpp`. Wasm needs it because object files must type undefined symbols.
- **Bitcast calls:** `WebAssemblyFixFunctionBitcasts` makes every direct call's type equal the callee's declared type, so the IR-derived and MI-derived signatures agree. `AddMissingPrototypes` does the same for K&R declarations.

### 2.6 Frame indices

- `setOperationAction(ISD::FrameIndex, MVT::i32/i64, Custom)` (377-378). `LowerFrameIndex` returns `getTargetFrameIndex` (2034-2038).
- The `TargetFrameIndex` becomes an MO_FrameIndex operand of whatever MI uses it (a load/store address, an `add`, a call operand).
- `CopyToReg` cannot take an FI, so `LowerCopyToReg` inserts a `COPY_I32/I64` MachineNode (2008-2032).
- `eliminateFrameIndex` (RegisterInfo.cpp:52-136) does one of three things:
  - Folds SP + offset into the load/store `off` operand.
  - Folds it into an `add`'s constant.
  - Emits `CONST + ADD` from the frame register.
- `PreprocessISelDAG` hoists wasm-local allocas (ISelDAGToDAG.cpp:108-118). That is irrelevant for MSIL.

### 2.7 GlobalAddress and ExternalSymbol wrappers

- `LowerGlobalAddress` (2124-2168) returns `Wrapper(TargetGlobalAddress)`, or `ADD(Wrapper(__memory_base), WrapperREL(sym))` in PIC mode.
- `LowerExternalSymbol` returns `Wrapper(TargetExternalSymbol)` (2170-2180).
- Patterns select `Wrapper tglobaladdr` → `CONST_I32/I64 sym` (non-PIC) or `GLOBAL_GET` (PIC) (InstrInfo.td:431-469).
- `SelectAddrOperands` folds `Wrapper(tga)` and `add` constants into the load/store `offset` immediate (531-589).
- `isOffsetFoldingLegal` is false for functions (1058-1063).

### 2.8 `br_table` / switch

- `setOperationAction(ISD::BR_JT, Other, Custom)` (397) and `setMinimumJumpTableEntries(2)` (446).
- `LowerBR_JT` (2192-2216) builds `BR_TABLE(chain, index, MBB_0..MBB_{n-1}, MBB_0)`. The last MBB is a dummy default. It is never taken, because SelectionDAGBuilder already emitted a range check. `WebAssemblyFixBrTableDefaults` later removes the range check and installs the real default.
- `LowerJumpTable` returns a bare `TargetJumpTable` (2182-2190).
- `BR_TABLE_I32/I64` (InstrControl.td:48-63) are `isTerminator, isBarrier, isNotDuplicable`.

### 2.9 select

- Wasm has a real `select` (`SELECT_I32/I64/F32/F64`, Integer.td:117-138 and Float.td:106-127), with patterns that fold `setne c, 0` and swap the arms for `seteq c, 0`.
- `SELECT_CC` and `BR_CC` are Expand (382-384).

### 2.10 setcc

- Integer: `ComparisonInt` defs produce `i32` (Integer.td:36-45, 73-83). `EQZ` is the `x == 0` form.
- FP: `ComparisonFP` for OEQ/UNE/OLT/OLE/OGT/OGE (Float.td:84-90), plus "don't care" patterns (93-104). `SETO`, `SETUO`, `SETUEQ`, `SETONE`, `SETULT`, `SETULE`, `SETUGT` and `SETUGE` are Expand (ctor 135-137).

### 2.11 Varargs

- Caller: the varargs go into a static stack object (FI) in the caller. Each slot uses `AllocateStack(size, max(OrigAlign, ABITypeAlign))`, so i32 takes 4 bytes (1460-1506). A pointer to the buffer becomes an extra trailing argument.
- Callee: LowerFormalArguments saves that argument in `VarargBufferVreg`. `LowerVASTART` stores the vreg into the `va_list` (2218-2230).
- `VAARG`, `VACOPY` and `VAEND` are Expand (124-126).

### 2.12 byval and sret

- **byval:** copy into a caller stack object and pass its FI (1398-1413). The callee sees a plain pointer.
- **sret:** nothing special. The IR `ptr sret` param is just a pointer. Multi-value returns are demoted by `CanLowerReturn` returning false.

### 2.13 i128 and illegal types

- Wasm relies on the type legalizer. It marks `SHL_PARTS`/`SRA_PARTS`/`SRL_PARTS`, `ADDC`/`ADDE`/`SUBC`/`SUBE`, `[SU]MUL_LOHI`, `MULH[SU]` and `[SU]DIVREM` as Expand (174-183).
- Expand on `*_PARTS` means a libcall `__ashlti3` etc. whenever compiler-rt provides one. `WasmCompilerRt` does (RuntimeLibcalls.td:3861-3865).
- With `wide-arithmetic`, i128 ADD/SUB and i64 `*MUL_LOHI` are Custom (185-191).

### 2.14 Unaligned and volatile accesses

- `allowsMisalignedMemoryAccesses` returns true and Fast (1025-1037).
- Each load/store MI has a `P2Align` operand, which ISel sets to 0. `WebAssemblySetP2AlignOperands` rewrites it from the MachineMemOperand.
- Volatile needs nothing in wasm. RegStackify orders volatile accesses through `hasOrderedMemoryRef`.

### 2.15 memcpy and memset

- `WebAssemblySelectionDAGInfo::EmitTargetCodeFor{Memcpy,Memmove,Memset}` (cpp:41-90) returns `WebAssemblyISD::MEMCPY/MEMSET` when bulk-memory is available, else `SDValue()`, which means a libcall.
- The custom inserters `LowerMemcpy`/`LowerMemset` (611-784) wrap `memory.copy`/`memory.fill` in a zero-length check, because those trap on out-of-bounds addresses even when the length is 0.

### 2.16 Other wasm details MSIL inherits

- `TrapUnreachable = true; NoTrapAfterNoreturn = false` (TargetMachine.cpp:184-185).
- RegStackify exempts DIV/REM and the trapping fp→int conversions from "side effects" when stackifying (185-247). Those MIs are `hasSideEffects = 1` only so that nothing speculates them.
- RegStackify rematerializes `isAsCheapAsAMove && isTriviallyReMaterializable` defs (268-270). This is why constants and address MIs must carry both flags.

---

## 3. CIL semantics checked by experiment

Unless noted, every row was identical on .NET 8 (tiered and `DOTNET_TieredCompilation=0`), .NET 10 and .NET Framework 4.8 x64.

### 3.1 Integer and shift

| Probe | Result | Consequence |
|---|---|---|
| `shl` int32 by 32 / 33 | 1 / 2: masked like hardware. Constant-folded shifts are also masked. | ECMA-335 III.3.58 says "unspecified". LLVM's result is poison, so this is compatible. Keep the explicit `& (bw-1)` masks that LLVM inserts. Do **not** port wasm's mask-dropping patterns (Integer.td:97-115). |
| `shr.un` int32 −8 by 32 | −8 (masked) | as above |
| `shl` native int by 64 / 65 | 1 / 2 | as above |
| `shl` native int by a native int amount | works | amounts can be int32 or native int; we always use int32 |
| `div` / `rem` INT_MIN by −1 | `System.OverflowException` (both) | C and LLVM: UB. Fine. Keep `hasSideEffects = 1` on DIV/REM so nothing speculates them. |
| `conv.i1(0x1FF)` = −1, `conv.u2(−1)` = 65535 | sign- and zero-extend from 8/16 bits | `sext_inreg` → `conv.i1`/`conv.i2`; `and 0xff` → `conv.u1` |
| `conv.i(int32 −1)` = −1, `conv.u(int32 −1)` = 0xFFFFFFFF, `conv.u8(int32 −1)` = 0xFFFFFFFF | | `sext` → `conv.i`, `zext`/`anyext` → `conv.u` |
| `conv.i4(native int 0x1_80000000)` = INT_MIN | truncation | `trunc` → `conv.i4` |
| native int `+`/`&`/`ceq` with an int32 operand (constant or variable) | the int32 is **sign-extended**: `0x100000000 + (int32)-1 = 0xFFFFFFFF`, `0x123456789 & (int32)-16 = 0x123456780` | `OP_I64_I32` MIs mean `a op sext(b)` |
| `switch` with a native int index 0x1_00000001 | falls through (full-width unsigned compare) | an i64 jump-table index needs no `conv` |

### 3.2 Floating point

| Probe | Result | Consequence |
|---|---|---|
| `clt`/`cgt`/`ceq` with a NaN operand | 0 | ordered: OLT/OGT/OEQ |
| `clt.un`/`cgt.un` with a NaN operand | 1 | unordered: ULT/UGT |
| `blt`/`bge`/`ble`/`bgt`/`beq` with NaN | not taken | OLT, OGE, OLE, OGT, OEQ |
| `blt.un`/`bge.un`/`ble.un`/`bgt.un`/`bne.un` with NaN | taken | ULT, UGE, ULE, UGT, UNE |
| `conv.r.un` u64 max | 2^64 (correct) | `uitofp i64→f64` = `conv.r.un` |
| `conv.r.un; conv.r4` of `0x8000008000000001` | **2^63 (wrong; correct is 2^63+2^40)** on .NET 8 and Framework. **Correct on .NET 10.** | `uitofp i64→f32` must be Custom: fall back to LLVM's `__floatundisf` expansion |
| `conv.r4` of int64 `0x4000004000000001` | correctly rounded (cvtsi2ss) | `sitofp i64→f32` = `conv.r4` |
| `conv.r.un` of int32 −1 | 4294967295.0 | `uitofp i32→f64` = `conv.r.un`; →f32 = `conv.r.un; conv.r4` (exact then a single rounding) |
| `conv.u8` / `conv.u` of 1.8e19 (f64 and f32) | 18000000000000000000 (correct) | `fptoui →i64` = `conv.u` |
| `conv.u4` of 4e9 | 4000000000 | `fptoui →i32` = `conv.u4` |
| `conv.i4` of 3e9 | INT_MIN on .NET 8; **INT_MAX (saturating) on .NET 10** | LLVM: poison. Fine. `fptosi.sat` stays Expand. |
| `rem` float64: (5.5,2)=1.5; (−5.5,2)=−1.5; (−0.0,1)=−0.0; (inf,2)=NaN; (5,inf)=5; (5,0)=NaN; (1e300,3)=0 (matches Python `math.fmod`) | C `fmod` semantics | `frem` is **Legal** as CIL `rem` (no libcall) |
| `neg 0.0` | −0.0 | `fneg` = `neg` |
| `float32 add` 16777216f + 1f | 16777216 (single precision kept on x64) | no `conv.r4` after each op is needed on 64-bit runtimes (ECMA would allow wider precision; see risks) |
| `Math.Min(-0.0, 0.0)` = −0, `Math.Min(0.0, -0.0)` = −0, `Math.Min(NaN, 1)` = NaN, `Math.Max(-0.0, 0.0)` = +0 | .NET 8/10: IEEE-754-2019 minimum/maximum. **Framework: `Math.Min(-0.0, 0.0)` = +0** | `fminimum`/`fmaximum` → `Math.Min`/`Max` on the net8 profile only |

### 3.3 Verifier and JIT structure (PEVerify)

| Probe | Result |
|---|---|
| `native int + int64`; `stloc` of an int64 into a native int local | JIT accepts. **PEVerify: "Unexpected type on the stack"** ([found Native Int][expected Long]). |
| native int with int32: `add`, `shl`, `cgt.un`, `ceq`, `blt.un`; returning an int32 from a native-int method | **"All Classes and Methods Verified."** |
| `ldind`/`stind`/`cpblk`/`calli` through a native int | "Instruction cannot be verified". This is expected (unverifiable pointer use). |
| stack non-empty at a join (the in-stack select, below) | verified (only an initlocals complaint) |
| `localloc` with another value under the size | `InvalidProgramException` |
| `.maxstack 1` with depth 2 | `InvalidProgramException`, so `.maxstack` must be exact or an over-estimate |

### 3.4 Calls, memory and BCL

| Probe | Result |
|---|---|
| `ldftn` + `calli int32(int32)` (managed) | works; `ldftn` of the same method twice compares equal |
| `localloc` result alignment (sizes 3, 24) | 16-byte aligned (also measured by passes-frame) |
| `cpblk`/`initblk` with a **native int** size; `stind.i4` with a native int value | work; `stind.i4` stores the low 32 bits |
| In-stack select `a b c; brtrue L; stloc t; pop; ldloc t; br D; L: pop; D:` | works (alternative to the triangle, section 11.6) |
| `Interlocked.CompareExchange(int32&, …)` called with a native int pointer | works (phase-3 atomics) |

BCL member resolution on .NET 8 is in section 5.9.

---

## 4. Value typing model

| LLVM MVT | Register class | CIL stack type | CIL local/param type | Notes |
|---|---|---|---|---|
| i1, i8, i16 | (promoted to i32 by the type legalizer) | int32 | n/a | values carry LLVM's normal anyext/sext/zext discipline |
| i32 | `I32` | int32 | `int32` | |
| i64 | `I64` | **native int** | `native int` | pointers *and* `long long`/LP64 `long` |
| f32 | `F32` | F | `float32` | |
| f64 | `F64` | F | `float64` | |
| i128 | expanded to 2 × i64 | | | |
| vectors | scalarized (no vector register classes in M1) | | | Design 3.2 later |
| f16/bf16/f80/f128 | not legal | | | f16 is softened (libcalls, section 7.2); f80 must not occur (the wrapper maps long double to double, see registry-tm); f128 → reject in M1 |

Consequences:
- **Constants.** `ldc.i8` pushes `int64`, so `CONST_I64` prints as:
  - `ldc.i4 c; conv.i` when c fits in int32;
  - `ldc.i4 c; conv.u` when c fits in uint32;
  - otherwise `ldc.i8 c; conv.i`.

  The JIT treats `conv.i` on 64-bit as a no-op.
- **Mixed-width MIs.** An int32 may stand directly as an operand of a native-int binary op or compare, and is sign-extended (verified in 3.1 and 3.3). That allows mixed-width MIs (`ADD_I64_I32`, `AND_I64_I32`, …) for `x + sext(y)` and `x + imm32`. passes-frame needs these (`ADD_PTR_I32`/`AND_PTR_I32` in its §6.3; this report names them `ADD_I64_I32`/`AND_I64_I32`).
- **BCL calls typed `int64`** (`DoubleToInt64Bits`, `Int64BitsToDouble`) get `conv.i` after or `conv.i8` before, inside the macro MI. Prefer `native int`/`native uint` overloads where they exist (all verified on .NET 8, section 5.9).
- **Signatures.** In M1, i64 params and returns print as `native int`.
  - That is fine for internal calls, P/Invoke (`native int` and `int64` are both blittable 8-byte values on 64-bit) and `calli`.
  - Phase 2 exported APIs that want `int64` in C# signatures can print `int64` and add `conv.i` after `ldarg` / `conv.i8` before `ret`.
- **ILP32 (i686, later).** The rule generalizes to "the integer class whose width equals the pointer width is `native int`", but then `I32` becomes `native int`. Left open (section 14).

---

## 5. LLVM operation → CIL mapping (milestone 1)

Notation: `[a b]` is the stack before, top on the right. Every ISel output pops its operands in MI operand order.

### 5.1 Integer arithmetic (I32 and I64 unless noted)

| LLVM ISD | CIL | Notes |
|---|---|---|
| ADD, SUB, MUL | `add`, `sub`, `mul` | no `.ovf` forms |
| SDIV, UDIV, SREM, UREM | `div`, `div.un`, `rem`, `rem.un` | they throw on 0 and on INT_MIN/−1 (UB in LLVM); `hasSideEffects = 1` |
| AND, OR, XOR | `and`, `or`, `xor` | |
| `sub 0, x` / `xor x, -1` | `neg` / `not` | patterns on `ineg`/`not` (TargetSelectionDAG.td:1369-1371) |
| SHL, SRA, SRL | `shl`, `shr`, `shr.un` | the amount is **always i32** (`getScalarShiftAmountTy` returns i32). For counts ≥ width CIL is "unspecified" and LLVM gives poison. Keep LLVM's masks. |
| ROTL/ROTR i32 | `call uint32 [System.Runtime]System.Numerics.BitOperations::RotateLeft(uint32, int32)` / `RotateRight` | net8 profile; Expand on net48 |
| ROTL/ROTR i64 | `call native uint …RotateLeft(native uint, int32)` | the value is under the amount, so the `native uint` overload avoids a conversion (verified .NET 8) |
| CTLZ i32 / i64 | `call int32 …BitOperations::LeadingZeroCount(uint32)`; i64: `…(native uint)` then `conv.i` | returns 32/64 for 0, as ISD::CTLZ requires; `CTLZ_ZERO_POISON` stays Expand (→ CTLZ) |
| CTTZ | `TrailingZeroCount(uint32)` / `(native uint)` + `conv.i` | |
| CTPOP | `PopCount(uint32)` / `(native uint)` + `conv.i` | |
| BSWAP | `call uint32 [System.Memory]System.Buffers.Binary.BinaryPrimitives::ReverseEndianness(uint32)` / `(native uint)` | i16 bswap is promoted (→ i32 bswap + srl) |
| `sext_inreg` i32 from i8 / i16 | `conv.i1` / `conv.i2` | |
| `sext_inreg` i64 from i8 / i16 / i32 | `conv.i1; conv.i` / `conv.i2; conv.i` / `conv.i4; conv.i` | two single-opcode MIs |
| `and x, 0xff` / `0xffff` (i32) | `conv.u1` / `conv.u2` | optional peephole patterns |
| `and x, 0xffffffff` (i64) | `conv.u4; conv.u` | optional |
| ABS, SMIN, SMAX, UMIN, UMAX, SCMP, UCMP | Expand | ABS uses sra/xor/sub; min/max become select diamonds |
| MULHS, MULHU, SMUL_LOHI, UMUL_LOHI, SDIVREM, UDIVREM | Expand | with `isIntDivCheap() == true`, divide-by-constant is left to RyuJIT, which does the magic-number division |
| SADDO/UADDO/SSUBO/USUBO/SMULO/UMULO, `*_CARRY`, ADDC…SUBE | Expand (defaults) | |

### 5.2 Comparisons as values (`setcc` → i32 0/1)

| Condition | Integer (I32, I64) | FP (F32, F64) |
|---|---|---|
| EQ / OEQ | `ceq` | `ceq` |
| NE / UNE | `ceq; ldc.i4.0; ceq` | `ceq; ldc.i4.0; ceq` |
| SLT / OLT | `clt` | `clt` |
| ULT | `clt.un` | `clt.un` |
| SGT / OGT | `cgt` | `cgt` |
| UGT | `cgt.un` | `cgt.un` |
| SLE / OLE | `cgt; ldc.i4.0; ceq` | `cgt.un; ldc.i4.0; ceq` (= !UGT) |
| ULE | `cgt.un; ldc.i4.0; ceq` | `cgt; ldc.i4.0; ceq` (= !OGT) |
| SGE / OGE | `clt; ldc.i4.0; ceq` | `clt.un; ldc.i4.0; ceq` (= !ULT) |
| UGE | `clt.un; ldc.i4.0; ceq` | `clt; ldc.i4.0; ceq` (= !OLT) |
| SETO, SETUO, SETONE, SETUEQ | n/a | **Expand** (`setCondCodeAction`). The legalizer builds them from OEQ/UNE on (a,a), (b,b) and OLT/OGT. |
| "don't care" EQ/NE/LT/GT/LE/GE on FP | n/a | map to OEQ, UNE, OLT, OGT, OLE, OGE |
| `x == 0` / `x != 0` | `ldc.i4.0; ceq` / `ldc.i4.0; cgt.un` | the `cgt.un` form saves one opcode |

The "`…; ldc.i4.0; ceq`" forms are built at ISel as `(CEQ_I32 (CMP a b) (CONST_I32 0))`, so each MI is one opcode. A constant `CONST_I32 0` that MachineCSE shares is rematerialized by RegStackify.

### 5.3 Branches (`brcond`)

| Pattern | CIL |
|---|---|
| `brcond c` | `brtrue L` |
| `brcond (seteq c, 0)`, `brcond (xor bool, 1)` | `brfalse L` |
| `brcond (setcc a, b, cc)`, integer | `beq` (EQ), `bne.un` (NE), `blt`/`blt.un`, `ble`/`ble.un`, `bgt`/`bgt.un`, `bge`/`bge.un` (signed / unsigned) |
| `brcond (setcc a, b, cc)`, FP | `beq` (OEQ), `bne.un` (UNE), `blt` (OLT), `blt.un` (ULT), `ble` (OLE), `ble.un` (ULE), `bgt` (OGT), `bgt.un` (UGT), `bge` (OGE), `bge.un` (UGE). Verified with NaN. |
| `br` | `br L` |
| BR_JT | `switch (L0 … Ln-1)` then `br Ldefault` (macro MI). The index may be native int (verified). |
| RETURN | `ret` |
| TRAP | `ldnull; throw` (terminator, barrier) |
| DEBUGTRAP | `call void [System.Runtime]System.Diagnostics.Debugger::Break()` (not a terminator, like wasm's `DEBUG_UNREACHABLE`) |
| `unreachable` (IR) | becomes TRAP through `TrapUnreachable` |

### 5.4 Conversions

| LLVM | CIL |
|---|---|
| trunc i64→i32 | `conv.i4` |
| sext i32→i64 | `conv.i` |
| zext / anyext i32→i64 | `conv.u` |
| fpext f32→f64 | `conv.r8` |
| fptrunc f64→f32 | `conv.r4` |
| sitofp i32→f32 / f64 | `conv.r4` / `conv.r8` |
| sitofp i64→f32 / f64 | `conv.r4` / `conv.r8` (correctly rounded, verified) |
| uitofp i32→f64 | `conv.r.un` (`; conv.r8` optional) |
| uitofp i32→f32 | `conv.r.un; conv.r4` (exact u32→f64, then a single rounding) |
| uitofp i64→f64 | `conv.r.un` |
| uitofp i64→f32 | **Custom → `SDValue()`**, so the legalizer uses the `__floatundisf` select form (LegalizeDAG `ExpandLegalINT_TO_FP`), built on `conv.r4` from i64 |
| fptosi f→i32 / i64 | `conv.i4` / `conv.i` |
| fptoui f→i32 / i64 | `conv.u4` / `conv.u` (correct for values ≥ 2^63, verified) |
| fptosi.sat / fptoui.sat | Expand (compare/select). .NET 9+ conversions saturate, but net8 does not. |
| bitcast f32↔i32 | `call int32 [System.Runtime]System.BitConverter::SingleToInt32Bits(float32)` / `Int32BitsToSingle(int32)` |
| bitcast f64↔i64 | `call int64 …DoubleToInt64Bits(float64); conv.i` / `conv.i8; call float64 …Int64BitsToDouble(int64)` |
| bitcast on net48 | Expand (stack-temp store and load, through the frame). `SingleToInt32Bits` does not exist on Framework. |
| fp16 (`FP16_TO_FP`, `FP_TO_FP16`) | Expand → libcalls `__extendhfsf2`/`__truncsfhf2`/`__truncdfhf2` (section 7.2) |

### 5.5 Loads and stores

The address is a single native int operand. There is no offset immediate: offsets are explicit `add`s. `unaligned.` and `volatile.` prefixes come from the `memflags` operand (section 10.6).

| LLVM | CIL |
|---|---|
| load i32; sextload i8/i16 → i32; zextload or extload i8/i16 → i32 | `ldind.i4`; `ldind.i1` / `ldind.i2`; `ldind.u1` / `ldind.u2` |
| load i64 | `ldind.i` |
| load f32 / f64 | `ldind.r4` / `ldind.r8` |
| ext loads into i64 (from i8/i16/i32) | **Expand** → i32 extload + `conv.i`/`conv.u` |
| extload f32 → f64 | Expand → `ldind.r4` + `conv.r8` |
| i1 extloads | Promote (as wasm) |
| store i32; truncstore i8/i16 from i32 | `stind.i4`; `stind.i1`; `stind.i2` |
| store i64 | `stind.i` |
| store f32 / f64 | `stind.r4` / `stind.r8` |
| truncstore from i64 | **Expand** → `conv.i4` then the i32 store (`stind.i4` would accept a native int, but keep it explicit) |
| truncstore f64 → f32 | Expand |
| unaligned access | `unaligned. N` (N = min(align, 4), and only when align < access size) |
| volatile access | `volatile.` (CLI volatile is acquire/release, which is stronger than C requires and fine) |

ECMA allows both prefixes together, in either order.

### 5.6 Floating-point arithmetic

| LLVM | CIL (net8 profile) | net48 profile |
|---|---|---|
| FADD/FSUB/FMUL/FDIV | `add`/`sub`/`mul`/`div` | same |
| FREM | `rem` (fmod semantics, verified) | same |
| FNEG | `neg` | same |
| FABS | `call float64 [System.Runtime]System.Math::Abs(float64)` / `Math::Abs(float32)` | same (these exist in mscorlib) |
| FSQRT | `Math::Sqrt(float64)`; f32: `System.MathF::Sqrt(float32)` | f32: `conv.r8; Math::Sqrt; conv.r4` (correctly rounded) |
| FCEIL/FFLOOR/FTRUNC | `Math::Ceiling`/`Floor`/`Truncate`; f32 via `MathF` | f32 via f64 (exact) |
| FRINT/FNEARBYINT/FROUNDEVEN | `Math::Round(float64)` (ties to even); `MathF::Round(float32)` | |
| FROUND (away from zero) | `ldc.i4.1; call Math::Round(float64, valuetype System.MidpointRounding)` | |
| FCOPYSIGN | `Math::CopySign(float64, float64)` | Expand (bitcast + and/or, through the stack temp) |
| FMA | `Math::FusedMultiplyAdd` | LibCall `fma` |
| FMINIMUM/FMAXIMUM | `Math::Min`/`Max` (IEEE semantics on .NET Core 3.0+, verified on .NET 8) | Expand (compare/select) |
| FMINNUM/FMAXNUM | `System.Double::MinNumber`/`MaxNumber` (and `System.Single::`) | LibCall `fmin`/`fmax` |
| FMINIMUMNUM/FMAXIMUMNUM | Expand | Expand |
| FSIN, FCOS, FPOW, FEXP*, FLOG*, … | LibCall in M1. Phase 3: `System.Math` (CoreCLR calls the C runtime's own `sin` etc.) | LibCall |
| FPOWI, FLDEXP, FFREXP, LROUND… | defaults (Expand/LibCall) | |

### 5.7 Constants, addresses, frames

| LLVM | CIL |
|---|---|
| `Constant i32` | `ldc.i4 c` (the printer picks `ldc.i4.0`…`.8`, `.m1`, `.s`) |
| `Constant i64` | `ldc.i4 c; conv.i` / `ldc.i4 c; conv.u` / `ldc.i8 c; conv.i` |
| `ConstantFP f32` / `f64` | `ldc.r4` / `ldc.r8`. Legal, so there is no constant pool. The printer emits bit-exact forms such as `float64(0x…)` for NaN/−0. |
| GlobalAddress (data), Design 4.1 | `GLOBAL_ADDR sym+off`, printed as `ldsfld native int '<Module>'::'<data base>'; ldc.i4 <off(sym)+off>; add`. The offset folds in because `isOffsetFoldingLegal` is true for data. Make the field `initonly`: RyuJIT is expected to fold initialized static readonly fields at Tier1 (unverified). Alternative: one `initonly` field per global (`ldsfld native int 'g'`). |
| GlobalAddress (function) | `ldftn <sig> <name>` (FUNC_ADDR). No offsets (`isOffsetFoldingLegal` is false). |
| ExternalSymbol | only as a call target in M1 (`EXT_SYM` wrapper, stripped at the call) |
| FrameIndex | `TargetFrameIndex`. `eliminateFrameIndex` (passes-frame §6.4) rewrites it to `FP` or `FP + int32`. |
| GlobalTLSAddress | `fail()` in M1 (Design 4.4 TLS block is phase 3) |
| BlockAddress / BRIND | removed earlier by `IndirectBrExpandPass` (wasm-style `enableIndirectBrExpand`); otherwise `fail()` |

### 5.8 Calls, memory blocks, varargs

| LLVM | CIL |
|---|---|
| direct call | `call <ret> <name>(<params>)`; the args are pushed in order |
| indirect call | args…, ftn; `calli <ret>(<params>)` (managed, matching `ldftn` pointers in M1) |
| memcpy, constant n ≤ 2^32−1 | `[dst src n] [unaligned. N] [volatile.] cpblk` (n as `ldc.i4`) |
| memcpy, variable n | `[src dst n] call void [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeMemory::Copy(void*, void*, native uint)` (memmove semantics) |
| memmove | the NativeMemory.Copy form, always (`cpblk` with overlap is unspecified in ECMA) |
| memset, constant n | `[dst val n] initblk` |
| memset, variable n | `[dst n val] call void …NativeMemory::Fill(void*, native uint, uint8)` |
| memcpy/memset on net48 | `Buffer::MemoryCopy(void*, void*, int64, int64)` (`conv.i8; dup` duplicates the length); variable memset → runtime helper (phase 3) |
| `va_start` | ABI-specific stores into the native `va_list` (Design 4.7; section 11.4) |
| `va_copy` | Custom: memcpy of `sizeof(va_list)` (24 SysV x86-64, 32 AAPCS64, 8 for `char*` ABIs) |
| `va_arg` / `va_end` | Expand. Clang lowers `va_arg` itself for x86-64 and AArch64, so ISD::VAARG rarely appears. |

### 5.9 BCL members verified on .NET 8 (assembly name matters)

The resolution was tested by building IL with each candidate `[assembly]` and running it on net8.0 (`bclprobe.il`, `nuintprobe.il`, `fence.il`). The printer needs this table for `.assembly extern`.

| Type | Resolves via (net8) | Does **not** resolve via |
|---|---|---|
| `System.Math`, `System.MathF`, `System.BitConverter`, `System.Double`, `System.Single`, `System.Environment`, `System.Buffer` | `[System.Runtime]` (also `[mscorlib]`, `[netstandard]`) | |
| `System.Numerics.BitOperations` | `[System.Runtime]` | `[mscorlib]` |
| `System.Buffers.Binary.BinaryPrimitives` | `[System.Memory]` (also `[mscorlib]`, `[netstandard]`) | `[System.Runtime]` |
| `System.Runtime.InteropServices.NativeMemory` | `[System.Runtime.InteropServices]` | `[System.Runtime]`, `[mscorlib]` |
| `System.Threading.Interlocked` | `[System.Threading]`, `[mscorlib]` | `[System.Runtime]` |

Overloads verified to exist on .NET 8:
- `BitOperations.LeadingZeroCount(uint32/uint64/native uint)`, `TrailingZeroCount(uint64/native uint)`, `PopCount(uint64/native uint)`, `RotateLeft/RotateRight(native uint, int32)`.
- `BinaryPrimitives.ReverseEndianness(uint32/uint64/native uint)`.
- `NativeMemory.Copy(void*, void*, native uint)`, `NativeMemory.Fill(void*, native uint, uint8)`.
- `Math.CopySign`, `Math.FusedMultiplyAdd`, `MathF.Sqrt`, `MathF.Round(float32)`, `Math.Round(float64, MidpointRounding)`, `Math.Abs(float32)`, `Double.MinNumber/MaxNumber`, `Single.MaxNumber`, `BitConverter.SingleToInt32Bits`.

---

## 6. `setOperationAction` proposal (constructor)

`ST.hasModernBCL()` stands for the `-mdotnet` profile query, whatever registry-tm settles on (a TargetOptions field or module flag; registry-tm §"CPU/features" recommends that over a SubtargetFeature).

```cpp
MSILTargetLowering::MSILTargetLowering(const TargetMachine &TM,
                                       const MSILSubtarget &STI)
    : TargetLowering(TM, STI), Subtarget(&STI) {
  const MVT PtrVT = MVT::i64;            // milestone 1: LP64/LLP64 triples only
  const bool Modern = STI.hasModernBCL(); // net8.0 (default) vs net48

  setBooleanContents(ZeroOrOneBooleanContent);   // ceq/cgt/clt push 0/1
  setSchedulingPreference(Sched::RegPressure);
  // No stack-pointer register: DYNAMIC_STACKALLOC/STACKSAVE/STACKRESTORE are Custom.

  addRegisterClass(MVT::i32, &MSIL::I32RegClass);
  addRegisterClass(MVT::i64, &MSIL::I64RegClass); // CIL native int
  addRegisterClass(MVT::f32, &MSIL::F32RegClass);
  addRegisterClass(MVT::f64, &MSIL::F64RegClass);
  computeRegisterProperties(STI.getRegisterInfo());

  // --- addresses, symbols, frames ---------------------------------------
  setOperationAction({ISD::GlobalAddress, ISD::ExternalSymbol, ISD::JumpTable,
                      ISD::FrameIndex, ISD::GlobalTLSAddress,
                      ISD::BlockAddress}, PtrVT, Custom);
  setOperationAction(ISD::BRIND, MVT::Other, Custom);        // fail()
  setOperationAction(ISD::BR_JT, MVT::Other, Custom);        // -> MSILISD::BR_TABLE
  setOperationAction(ISD::CopyToReg, MVT::Other, Custom);    // FI -> COPY_I64
  setOperationAction(ISD::DYNAMIC_STACKALLOC, PtrVT, Custom); // passes-frame 6.8
  setOperationAction({ISD::STACKSAVE, ISD::STACKRESTORE}, MVT::Other, Custom);
  // FRAMEADDR/RETURNADDR: LegalizeDAG treats "Legal" as Custom (1118-1130);
  // LowerOperation must handle them (return 0 / the frame base for depth 0).

  // --- control flow -----------------------------------------------------
  for (MVT T : {MVT::i32, MVT::i64, MVT::f32, MVT::f64}) {
    setOperationAction({ISD::BR_CC, ISD::SELECT_CC}, T, Expand);
    setOperationAction(ISD::SELECT, T, Legal);  // SELECT_* pseudo, custom inserter
  }
  setOperationAction({ISD::TRAP, ISD::DEBUGTRAP}, MVT::Other, Legal);
  // No target intrinsics in M1: leave INTRINSIC_* at the default. Wasm makes
  // them Custom only for its own llvm.wasm.* intrinsics (ctor 437-439).
  setOperationAction(ISD::CLEAR_CACHE, MVT::Other, Custom);  // fail()

  // --- varargs (Design 4.7) ----------------------------------------------
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VACOPY, MVT::Other, Custom);  // memcpy sizeof(va_list)
  setOperationAction({ISD::VAARG, ISD::VAEND}, MVT::Other, Expand);

  // --- integer ------------------------------------------------------------
  for (MVT T : {MVT::i32, MVT::i64}) {
    setOperationAction({ISD::SMUL_LOHI, ISD::UMUL_LOHI, ISD::MULHS, ISD::MULHU,
                        ISD::SDIVREM, ISD::UDIVREM}, T, Expand);
    setOperationAction({ISD::SMIN, ISD::SMAX, ISD::UMIN, ISD::UMAX, ISD::ABS},
                       T, Expand);                       // (default already)
    setOperationAction({ISD::CTLZ, ISD::CTTZ, ISD::CTPOP, ISD::BSWAP,
                        ISD::ROTL, ISD::ROTR}, T, Modern ? Legal : Expand);
    setOperationAction({ISD::CTLZ_ZERO_POISON, ISD::CTTZ_ZERO_POISON}, T,
                       Expand);                          // -> CTLZ/CTTZ
    setOperationAction({ISD::FSHL, ISD::FSHR, ISD::BITREVERSE}, T, Expand);
  }
  // i128 shifts stay inline; no __ashlti3/__lshrti3/__ashrti3.
  setOperationAction({ISD::SHL_PARTS, ISD::SRA_PARTS, ISD::SRL_PARTS},
                     MVT::i64, Custom);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);
  // i8/i16/i32 SIGN_EXTEND_INREG: Legal (default), patterns conv.i1/.i2/.i4.

  // --- int <-> fp conversions ---------------------------------------------
  setOperationAction(ISD::UINT_TO_FP, MVT::i64, Custom); // f32 result -> Expand
  for (MVT T : {MVT::i32, MVT::i64})
    setOperationAction({ISD::FP_TO_SINT_SAT, ISD::FP_TO_UINT_SAT}, T, Expand);
  if (!Modern)
    setOperationAction(ISD::BITCAST, {MVT::i32, MVT::i64, MVT::f32, MVT::f64},
                       Expand);                          // stack temp

  // --- floating point -------------------------------------------------------
  for (MVT T : {MVT::f32, MVT::f64}) {
    setOperationAction(ISD::ConstantFP, T, Legal);
    setCondCodeAction({ISD::SETO, ISD::SETUO, ISD::SETONE, ISD::SETUEQ}, T,
                      Expand);
    setOperationAction({ISD::FREM, ISD::FNEG, ISD::FABS, ISD::FSQRT,
                        ISD::FCEIL, ISD::FFLOOR, ISD::FTRUNC, ISD::FRINT,
                        ISD::FNEARBYINT, ISD::FROUNDEVEN, ISD::FROUND},
                       T, Legal);
    setOperationAction({ISD::FCOPYSIGN, ISD::FMINIMUM, ISD::FMAXIMUM}, T,
                       Modern ? Legal : Expand);
    setOperationAction(ISD::FMA, T, Modern ? Legal : LibCall);
    setOperationAction({ISD::FMINNUM, ISD::FMAXNUM}, T,
                       Modern ? Legal : LibCall);
    setOperationAction({ISD::FMINIMUMNUM, ISD::FMAXIMUMNUM, ISD::FMINNUM_IEEE,
                        ISD::FMAXNUM_IEEE, ISD::FCANONICALIZE, ISD::FMAD},
                       T, Expand);
    setOperationAction({ISD::FSIN, ISD::FCOS, ISD::FPOW, ISD::FSINCOS}, T,
                       Expand);                          // -> libm libcalls in M1
    setOperationAction({ISD::FP16_TO_FP, ISD::FP_TO_FP16}, T, Expand);
    setLoadExtAction(ISD::EXTLOAD, T, MVT::f16, Expand);
    setTruncStoreAction(T, MVT::f16, Expand);
  }

  // --- loads/stores -----------------------------------------------------------
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);
  for (MVT T : MVT::integer_valuetypes())
    setLoadExtAction({ISD::EXTLOAD, ISD::ZEXTLOAD, ISD::SEXTLOAD}, T, MVT::i1,
                     Promote);
  setLoadExtAction({ISD::EXTLOAD, ISD::ZEXTLOAD, ISD::SEXTLOAD}, MVT::i64,
                   {MVT::i8, MVT::i16, MVT::i32}, Expand);
  setTruncStoreAction(MVT::i64, MVT::i8, Expand);
  setTruncStoreAction(MVT::i64, MVT::i16, Expand);
  setTruncStoreAction(MVT::i64, MVT::i32, Expand);

  // --- atomics (M1 minimal; phase 3 does Interlocked) ----------------------------
  setMaxAtomicSizeInBitsSupported(64);
  setMinCmpXchgSizeInBits(32);

  // --- memcpy heuristics: small copies as ld/st, rest cpblk/initblk ---------
  MaxStoresPerMemcpy = MaxStoresPerMemcpyOptSize = 4;
  MaxStoresPerMemmove = MaxStoresPerMemmoveOptSize = 4;
  MaxStoresPerMemset = MaxStoresPerMemsetOptSize = 4;
}
```

Other hook overrides:

| Hook | MSIL value | Why |
|---|---|---|
| `getScalarShiftAmountTy` | `MVT::i32` | CIL shift amounts are int32 (or native int). Unlike wasm (493-511), the amount type does not follow the value width. |
| `getSetCCResultType` | `i32` | `ceq`/`cgt`/`clt` push int32 |
| `isLegalAddressingMode` | only `[reg]`: `!AM.BaseGV && AM.BaseOffs == 0 && AM.Scale == 0` | `ldind`/`stind` have no offset or scale. LSR then prefers pointer increments. |
| `allowsMisalignedMemoryAccesses` | true, `*Fast = 1` | the `unaligned.` prefix; the JIT ignores it on x64/arm64 |
| `isIntDivCheap` | true | RyuJIT does magic-number division itself, so the IL stays smaller |
| `isOffsetFoldingLegal(GA)` | `!GV->getValueType()->isFunctionTy()` | data offsets fold into `GLOBAL_ADDR` for free |
| `isCheapToSpeculateCtlz/Cttz` | `hasModernBCL()` | |
| `CanLowerReturn` | `Outs.size() <= 1` | |
| `shouldExpandAtomicRMWInIR` | `CmpXChg` (phase 3) | `Interlocked.CompareExchange` |
| `createFastISel` | not overridden in M1 | SelectionDAG also runs at -O0 |

TargetMachine settings (also in passes-frame §4.1): `Options.TrapUnreachable = true; Options.NoTrapAfterNoreturn = false; usesPhysRegsForValues() == false`. Enable `IndirectBrExpand` (wasm: `WebAssemblySubtarget::enableIndirectBrExpand()`).

---

## 7. Libcalls and runtime helpers

### 7.1 Availability plumbing (this checkout)

- `RuntimeLibcalls.td` selects the libcall set from the **triple**. An MSIL triple keeps its arch, so `TT.isX86()` / `isAArch64` match and MSIL inherits `X86SystemLibrary`/`AArch64SystemLibrary`.
- `setTargetRuntimeLibcallSets` returns at the **first** matching `SystemRuntimeLibrary`, and the records are iterated in def-name order (`std::map` in `RecordKeeper`, `Record.h:2012`). So an `MSILSystemLibrary` would sort after `AArch64SystemLibrary`.
- If MSIL later gets its own set, add `(not IsMSIL)` to the X86 and AArch64 predicates (`IsMSIL : LibcallPredicate<[{TT.isOSBinFormatMSIL()}]>`) rather than relying on names. For M1 the native set is acceptable (registry-tm agrees).
- Per-target overrides go in `MSILSubtarget::initLibcallLoweringInfo` (TargetSubtargetInfo.h:170):

```cpp
void MSILSubtarget::initLibcallLoweringInfo(LibcallLoweringInfo &Info) const {
  // Force inline expansion (no runtime helper needed):
  Info.setLibcallImpl(RTLIB::MUL_I128, RTLIB::Unsupported);   // forceExpandMultiply
  Info.setLibcallImpl(RTLIB::MULO_I64, RTLIB::Unsupported);
  Info.setLibcallImpl(RTLIB::MULO_I128, RTLIB::Unsupported);
  // (SHL/SRL/SRA_I128 are unreachable because *_PARTS is Custom.)
}
```

`ExpandIntRes_MUL` uses `__multi3` only if it is available, else `forceExpandMultiply` (LegalizeIntegerTypes.cpp:4560-4590). `forceExpandWideMUL` likewise checks `MUL_I128` (TargetLowering.cpp:12833-12856). `MULO_I64`/`MULO_I128` are the libcalls behind `__mulodi4`/`__muloti4` (RuntimeLibcalls.td:1875, 1879), looked up via `RTLIB::getMULO` (LegalizeIntegerTypes.cpp:5333).

### 7.2 Helpers that cannot be expanded inline

None of these are produced by the milestone-1 corpus (scalar i32/i64/f32/f64, no i128, no `_Float16`, `-ffreestanding`). `makeLibCall` aborts with "unsupported library call operation" only if an impl is Unsupported. Available impls become calls to external symbols. In M1 the printer must diagnose any external symbol other than the hard-coded `puts`.

| Libcall | Source construct | Provide via (phase 2/3) |
|---|---|---|
| `__divti3`, `__udivti3`, `__modti3`, `__umodti3` | i128 `/`, `%` | compiler-rt builtins compiled for MSIL (static archive), or C# in `MsilC.Runtime` (`Int128` exists on .NET 7+) |
| `__fixdfti`, `__fixsfti`, `__fixunsdfti`, `__fixunssfti`, `__floattidf`, `__floattisf`, `__floatuntidf`, `__floatuntisf` | i128 ↔ fp | same |
| `__extendhfsf2`, `__truncsfhf2`, `__truncdfhf2` (and bf16) | `_Float16`, `__fp16` | same, or `System.Half` |
| `__powisf2`, `__powidf2` | `llvm.powi` (fast-math) | same |
| `__mulsc3`, `__muldc3`, `__divsc3`, `__divdc3` | C `_Complex` multiply/divide (emitted by clang, not by ISel) | same |
| `fmod` | never: FREM is Legal (`rem`) | n/a |
| `memcpy`/`memset`/`memmove` | never: `MSILSelectionDAGInfo` always returns a node | n/a |
| `sin`, `cos`, `pow`, `fmin`/`fmax` (net48), `fma` (net48), `lround`… | libm | host libc P/Invoke (phase 3) or `System.Math` |
| f128 soft-float (`__addtf3`, …) | `__float128` | reject in M1 |

---

## 8. Calls and signatures for MSIL

### 8.1 Representation

```cpp
// MCTargetDesc/MSILTypeUtilities.h (new)
namespace llvm::MSIL {
enum class ValType : uint8_t { I32, NativeInt, I64, F32, F64 }; // I64 only for ILP32 later
enum class CallConv : uint8_t { Managed = 0, UnmanagedCdecl = 1,
                                UnmanagedStdcall = 2 };
struct Signature {
  SmallVector<ValType, 1> Results;   // CIL: 0 or 1
  SmallVector<ValType, 4> Params;
  CallConv CC = CallConv::Managed;
  bool operator==(const Signature &) const = default;
};
ValType toValType(MVT VT, unsigned PtrBits);           // i32, i64->NativeInt, f32, f64
ValType regClassToValType(unsigned RCID, unsigned PtrBits);
void printValType(raw_ostream &OS, ValType T);         // int32, native int, ...
// "int32 '<Module>'::f(int32, native int)"  or  "int32(native int)" for calli
void printSignature(raw_ostream &OS, const Signature &S, StringRef Callee = {});
} // namespace llvm::MSIL
```

### 8.2 Where each signature comes from (same split as wasm)

| Use | Source | Code to copy |
|---|---|---|
| `.method` header of a definition | `MSILFunctionInfo::getParams()/getResults()`, recorded in `LowerFormalArguments` (`addParam(In.VT)` for every In, plus a trailing ptr for varargs) and from `computeSignatureVTs` (results) | wasm ISelLowering.cpp:1645-1711 |
| `call` to a GlobalAddress callee (defined, or a declaration such as `puts`) | `computeSignatureVTs(F->getFunctionType(), F, CurFn, TM, Params, Results)` | wasm MachineFunctionInfo.cpp:62-112 (drop the Swift part). The `canLowerReturn` demotion must use `Results.size() <= 1`. |
| `ldftn` (FUNC_ADDR) | same as a direct call | |
| `calli` (CALL_INDIRECT) | the MI: defs → Results; explicit register uses → Params, **minus the last (callee)**; the `cc` immediate → CC | wasm MCInstLower.cpp:274-301 |
| `call` to an ExternalSymbol | the MI, as for `calli` but without the callee pop. Record `(name → Signature)` in an AsmPrinter-level `StringMap`, assert that all call sites agree, and emit the declarations at end of module. | replaces wasm's `RuntimeLibcallSignatures` table, which MSIL does not need for text output |
| P/Invoke declaration (M1: only `puts`) | the IR declaration's type → `(native int) → int32`; the printer adds `pinvokeimpl("libc" cdecl)` | |

Invariants and checks:
- **Direct-call consistency.** `MSILFixFunctionBitcasts` and `MSILAddMissingPrototypes` (wasm passes, Design 2.1) must run, so `CB->getFunctionType() == F->getFunctionType()` for every direct call. `LowerCall` should diagnose violations instead of emitting a `call` whose token does not match the method def. That would fail at JIT time with `MissingMethodException`, or with stack imbalance.
- **i8/i16/i1 params and returns** are int32 in signatures, because `computeLegalValueVTs` gives the register type. Sign/zero extension follows the IR `signext`/`zeroext` attributes at the call site (SelectionDAGBuilder `getCopyToParts`) and `AssertSext`/`AssertZext` in the callee. This is consistent because both sides are our code. P/Invoke exactness for narrow types is a phase-3 concern (Design 2.3 metadata).
- **Phase 2 (object files).** Add `MCSymbolMSIL` with `setSignature(const MSIL::Signature*)` and allocate the signatures in the target streamer, the way `MCContext::createWasmSignature` works for wasm. The ISel side does not change.

### 8.3 Text forms the printer emits

```
call      int32 fib(int32)                                // global method in <Module>
call      int32 puts(native int)                          // pinvokeimpl method def
ldftn     int32 cmp(native int, native int)
calli     int32(native int, native int)                   // managed (M1)
calli     unmanaged cdecl int32(native int, native int)   // phase 2+ (Design 4.5)
```

---

## 9. MachineInstr catalogue and stack effects

Rules RegStackify and `.maxstack` rely on:
- After ExplicitLocals, **every explicit register use pops and every explicit register def pushes**. passes-frame §5.1 has the code evidence. Implicit operands do not count, and neither do immediate, symbol or MBB operands.
- An MI pops its uses **in operand order**: the last use is on top.
- At most one explicit def per MI, except `TEE_*` before ExplicitLocals, which becomes `LOCAL_TEE`.
- **Peak** is the maximum stack height reached while the MI's CIL executes, measured from the base *below* its operands. For a single opcode it is `max(pops, pushes)`. For macros, `StackPeakExtra = Peak − max(pops, pushes)` is the per-opcode TableGen field that passes-frame §0.7 proposes. `.maxstack = max over MIs of (depthBefore − pops + Peak)`.

| MI (register form) | CIL printed | Pops | Pushes | Peak | Flags / notes |
|---|---|---|---|---|---|
| `ARGUMENT_{I32,I64,F32,F64} $res, argno` | (none; ExplicitLocals → `ldarg`) | 0 | 1 | 1 | `hasSideEffects`, `Uses=[ARGUMENTS]` |
| `COPY_*` | (none; → `ldloc/stloc`) | 1 | 1 | 1 | `isAsCheapAsAMove`, codegen-only |
| `TEE_*` (2 defs) | → `LOCAL_TEE` | 1 | (1) | 2 | pre-ExplicitLocals only |
| `LOCAL_GET_*` | `ldloc N` / `ldarg N` | 0 | 1 | 1 | the arg/local index split is in passes-frame §7 |
| `LOCAL_SET_*` | `stloc N` / `starg N` | 1 | 0 | 1 | |
| `LOCAL_TEE_*` | `dup; stloc N` | 1 | 1 | **2** | macro (+1) |
| `DROP_*` | `pop` | 1 | 0 | 1 | |
| `CONST_I32 imm` | `ldc.i4[.n/.s] imm` | 0 | 1 | 1 | `isMoveImm, isAsCheapAsAMove, isReMaterializable` |
| `CONST_I64 imm` | `ldc.i4 c; conv.i` / `ldc.i4 c; conv.u` / `ldc.i8 c; conv.i` | 0 | 1 | 1 | macro (+0); remat |
| `CONST_F32` / `CONST_F64` | `ldc.r4` / `ldc.r8` | 0 | 1 | 1 | remat |
| `GLOBAL_ADDR sym+off` | `ldsfld native int <base>; ldc.i4 off; add` | 0 | 1 | **2** | macro (+1); remat, `mayLoad=0` (the base is invariant) |
| `FUNC_ADDR fn` | `ldftn <sig> fn` | 0 | 1 | 1 | remat |
| `LOCALLOC $res, $size` (frame and dynamic alloca) | `localloc` | 1 | 1 | 1 | **must be a tree root** (RegStackify rule, passes-frame §5.5) |
| `ADD/SUB/MUL/AND/OR/XOR_{I32,I64}` | `add`, `sub`, `mul`, `and`, `or`, `xor` | 2 | 1 | 2 | commutative where applicable |
| `ADD/SUB/AND/OR/XOR_I64_I32` | same opcodes (int32 operand sign-extended) | 2 | 1 | 2 | `x op sext(y)` and `x op imm32` |
| `DIV_S/DIV_U/REM_S/REM_U_{I32,I64}` | `div`, `div.un`, `rem`, `rem.un` | 2 | 1 | 2 | `hasSideEffects=1`, RegStackify exemption as in wasm 188-243 |
| `SHL/SHR_S/SHR_U_{I32,I64}` (the amount is I32) | `shl`, `shr`, `shr.un` | 2 | 1 | 2 | |
| `NEG_*`, `NOT_*` | `neg`, `not` | 1 | 1 | 1 | |
| `CEQ/CGT/CGT_UN/CLT/CLT_UN_{I32,I64,F32,F64}` | `ceq`, `cgt`, `cgt.un`, `clt`, `clt.un` | 2 | 1 (I32) | 2 | negated forms are built at ISel |
| `CONV_I4_I64` | `conv.i4` | 1 | 1 | 1 | trunc |
| `CONV_I_I32` / `CONV_U_I32` | `conv.i` / `conv.u` | 1 | 1 | 1 | sext / zext and anyext |
| `CONV_I1/I2/U1/U2_I32`, `CONV_I1/I2_I64` (→ I32) | `conv.i1`, `conv.i2`, `conv.u1`, `conv.u2` | 1 | 1 | 1 | |
| `CONV_R4_*`, `CONV_R8_*` (from I32/I64/F32/F64) | `conv.r4`, `conv.r8` | 1 | 1 | 1 | |
| `CONV_R_UN_{I32,I64}` (→ F64) | `conv.r.un` | 1 | 1 | 1 | |
| `CONV_I4/U4/I/U_{F32,F64}` | `conv.i4`, `conv.u4`, `conv.i`, `conv.u` | 1 | 1 | 1 | no side effects (CIL non-ovf conversions do not trap) |
| `LDIND_{I1,U1,I2,U2,I4,I,R4,R8} flags, addr` | `[unaligned. N] [volatile.] ldind.*` | 1 | 1 | 1 | `mayLoad` |
| `STIND_{I1,I2,I4,I,R4,R8} flags, addr, val` | `[prefixes] stind.*` | 2 | 0 | 2 | `mayStore` |
| `CPBLK flags, dst, src, len(I32)` | `[prefixes] cpblk` | 3 | 0 | 3 | `mayLoad, mayStore` |
| `INITBLK flags, dst, val(I32), len(I32)` | `[prefixes] initblk` | 3 | 0 | 3 | `mayStore` |
| `MEMMOVE src, dst, len(I64)` | `call void NativeMemory::Copy(void*, void*, native uint)` | 3 | 0 | 3 | `mayLoad, mayStore`; net48: `conv.i8; dup; call Buffer::MemoryCopy(...)`, Peak 4 |
| `MEMFILL dst, len(I64), val(I32)` | `call void NativeMemory::Fill(void*, native uint, uint8)` | 3 | 0 | 3 | `mayStore` |
| `ABS/SQRT/CEIL/FLOOR/TRUNC/NEAREST_{F32,F64}` | `call float64 Math::Abs(float64)` etc. | 1 | 1 | 1 | pure: not `isCall`, no side effects |
| `ROUND_{F32,F64}` | `ldc.i4.1; call Math::Round(x, MidpointRounding)` | 1 | 1 | **2** | macro (+1) |
| `COPYSIGN/MIN/MAX/MINNUM/MAXNUM_{F32,F64}` | `call Math::CopySign`, `Math::Min`, `Math::Max`, `Double::MinNumber`, … | 2 | 1 | 2 | pure |
| `FMA_{F32,F64}` | `call Math::FusedMultiplyAdd` (`MathF` for f32) | 3 | 1 | 3 | pure |
| `ADD/SUB/MUL/DIV/REM_{F32,F64}`, `NEG_F*` | `add`, `sub`, `mul`, `div`, `rem`, `neg` | 2 / 1 | 1 | 2 / 1 | |
| `CLZ/CTZ/POPCNT_I32` | `call int32 BitOperations::…(uint32)` | 1 | 1 | 1 | pure |
| `CLZ/CTZ/POPCNT_I64` | `call int32 BitOperations::…(native uint); conv.i` | 1 | 1 | 1 | macro (+0) |
| `BSWAP_I32` / `BSWAP_I64` | `call uint32/native uint BinaryPrimitives::ReverseEndianness(…)` | 1 | 1 | 1 | |
| `ROTL/ROTR_I32` / `_I64` | `call uint32/native uint BitOperations::RotateLeft/Right(…, int32)` | 2 | 1 | 2 | |
| `BITCAST_I32_F32` / `_F32_I32` | `call BitConverter::SingleToInt32Bits` / `Int32BitsToSingle` | 1 | 1 | 1 | |
| `BITCAST_I64_F64` / `_F64_I64` | `call DoubleToInt64Bits; conv.i` / `conv.i8; call Int64BitsToDouble` | 1 | 1 | 1 | macro (+0) |
| `SELECT_{I32,I64,F32,F64} t, f, c` | never printed (custom inserter → triangle + PHI) | 3 | 1 | — | `usesCustomInserter` |
| `BR bb` | `br L` | 0 | 0 | 0 | |
| `BR_IF bb, c` / `BR_UNLESS bb, c` | `brtrue L` / `brfalse L` | 1 | 0 | 1 | |
| `B{EQ,NE_UN,LT,LT_UN,LE,LE_UN,GT,GT_UN,GE,GE_UN}_{I32,I64,F32,F64} bb, a, b` | `beq`, `bne.un`, `blt`, … | 2 | 0 | 2 | `isBranch`, `isTerminator` |
| `BR_TABLE_{I32,I64} idx, bbs…, default` | `switch (L…); br Ldefault` | 1 | 0 | 1 | `isBarrier`, `isNotDuplicable` |
| `RETURN [val]` | `ret` | 0/1 | 0 | 0/1 | `variable_ops` |
| `TRAP` | `ldnull; throw` | 0 | 0 | **1** | macro (+1); terminator, barrier, `isTrap` |
| `DEBUGTRAP` | `call void Debugger::Break()` | 0 | 0 | 0 | |
| `CALL_PARAMS` / `CALL_RESULTS` | (never printed) | — | — | — | combined by the custom inserter |
| `CALL [res], callee, args…` | `call <sig>` | #args | 0/1 | max(#args, 1) | `isCall`, `variadicOpsAreDefs` |
| `CALL_INDIRECT [res], cc, args…, ftn` | `calli <sig>` | #args+1 | 0/1 | #args+1 | `isCall`, `variadicOpsAreDefs` |
| `ADJCALLSTACKDOWN/UP` | (none) | 0 | 0 | 0 | only for `TargetInstrInfo` |
| `FENCE` (optional, M1) | `call void [System.Threading]System.Threading.Interlocked::MemoryBarrier()` | 0 | 0 | 0 | `hasSideEffects` |

`.maxstack` algorithm (implemented by passes-frame's `MSILStackCheck`; sketched here for the per-opcode data it needs):

```cpp
unsigned computeMaxStack(const MachineFunction &MF, const MSILInstrInfo &TII) {
  unsigned Max = 0;
  for (const MachineBasicBlock &MBB : MF) {
    unsigned Depth = 0; // RegStackify invariant: empty at block entry
    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || MI.isMetaInstruction()) continue;
      unsigned Pops = 0, Pushes = 0;
      for (const MachineOperand &MO : MI.explicit_operands())
        if (MO.isReg()) (MO.isDef() ? Pushes : Pops)++;
      unsigned Peak = std::max(Pops, Pushes) + TII.getStackPeakExtra(MI.getOpcode());
      assert(Depth >= Pops && "stack underflow");
      Max = std::max(Max, Depth - Pops + Peak);
      Depth = Depth - Pops + Pushes;
    }
    assert(Depth == 0 && "value left on the stack at block end");
  }
  return Max;
}
```

---

## 10. TableGen snippets

### 10.1 Formats and registers (copy wasm, rename, add the CIL encoding)

```tablegen
// MSILInstrFormats.td
class MSILInst<bits<16> opc, string asmstr, bit stack> : StackRel, RegisterRel,
                                                          Instruction {
  bits<16> Inst = opc;          // CIL opcode: 0x00xx or 0xFExx; 0xFFFF = pseudo/macro
  bit StackBased = stack;
  string BaseName = NAME;
  int StackPeakExtra = 0;       // section 9; consumed by MSILStackCheck
  let Namespace = "MSIL";
  let Pattern = [];
  let AsmString = asmstr;
  let TSFlags{3-0} = StackPeakExtra; // or a getNamedOperand-style table
}
class NI<dag oops, dag iops, list<dag> pattern, bit stack, string asmstr = "",
         bits<16> opc = 0xFFFF> : MSILInst<opc, asmstr, stack> {
  dag OutOperandList = oops;
  dag InOperandList  = iops;
  let Pattern        = pattern;
  let Defs           = [ARGUMENTS];
}
multiclass I<dag oops_r, dag iops_r, dag oops_s, dag iops_s,
             list<dag> pattern_r, string asmstr_r = "", string asmstr_s = "",
             bits<16> opc = 0xFFFF> {
  let isCodeGenOnly = 1 in
  def "" : NI<oops_r, iops_r, pattern_r, false, asmstr_r, opc>;
  let BaseName = NAME in
  def _S : NI<oops_s, iops_s, [], true, asmstr_s, opc>;
}
multiclass NRI<dag oops, dag iops, list<dag> pattern, string asmstr = "",
               bits<16> opc = 0xFFFF> {
  defm "" : I<oops, iops, oops, iops, pattern, asmstr, asmstr, opc>;
}

// MSILRegisterInfo.td (FP comes from passes-frame 6.2)
def I32_0 : MSILReg<"%i32.0">;   def I64_0 : MSILReg<"%i64.0">;
def F32_0 : MSILReg<"%f32.0">;   def F64_0 : MSILReg<"%f64.0">;
def VALUE_STACK : MSILReg<"STACK">;
def ARGUMENTS   : MSILReg<"ARGUMENTS">;
def I32 : MSILRegClass<[i32], 32, (add I32_0)>;
def I64 : MSILRegClass<[i64], 64, (add FP, I64_0)>;   // CIL native int
def F32 : MSILRegClass<[f32], 32, (add F32_0)>;
def F64 : MSILRegClass<[f64], 64, (add F64_0)>;
```

### 10.2 SDNodes

`-gen-sd-node-info` generates `MSILISD::*` from these. `MSILISD::CALL` is hand-written after `GENERATED_OPCODE_END`, as `WebAssemblySelectionDAGInfo.h:23-28` does, because it has variadic results.

```tablegen
def SDT_MSILArgument : SDTypeProfile<1, 1, [SDTCisVT<1, i32>]>;
def SDT_MSILReturn   : SDTypeProfile<0, -1, []>;
def SDT_MSILWrapper  : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;
def SDT_MSILBrTable  : SDTypeProfile<0, -1, [SDTCisInt<0>]>;
// (dst, src, len:i32, memflags:timm)
def SDT_MSILCpBlk    : SDTypeProfile<0, 4, [SDTCisPtrTy<0>, SDTCisPtrTy<1>,
                                            SDTCisVT<2, i32>, SDTCisVT<3, i32>]>;
// (dst, val:i32, len:i32, memflags:timm)
def SDT_MSILInitBlk  : SDTypeProfile<0, 4, [SDTCisPtrTy<0>, SDTCisVT<1, i32>,
                                            SDTCisVT<2, i32>, SDTCisVT<3, i32>]>;
// (src, dst, len:ptr) -- NativeMemory.Copy order
def SDT_MSILMemMove  : SDTypeProfile<0, 3, [SDTCisPtrTy<0>, SDTCisPtrTy<1>,
                                            SDTCisPtrTy<2>]>;
// (dst, len:ptr, val:i32) -- NativeMemory.Fill order
def SDT_MSILMemFill  : SDTypeProfile<0, 3, [SDTCisPtrTy<0>, SDTCisPtrTy<1>,
                                            SDTCisVT<2, i32>]>;

def MSILargument : SDNode<"MSILISD::ARGUMENT", SDT_MSILArgument>;
def MSILreturn   : SDNode<"MSILISD::RETURN", SDT_MSILReturn,
                          [SDNPHasChain, SDNPVariadic]>;
def MSILDataAddr : SDNode<"MSILISD::DATA_ADDR", SDT_MSILWrapper>;
def MSILFuncAddr : SDNode<"MSILISD::FUNC_ADDR", SDT_MSILWrapper>;
def MSILExtSym   : SDNode<"MSILISD::EXT_SYM",   SDT_MSILWrapper>; // call targets
def MSILbr_table : SDNode<"MSILISD::BR_TABLE", SDT_MSILBrTable,
                          [SDNPHasChain, SDNPVariadic]>;
def MSILcpblk    : SDNode<"MSILISD::CPBLK", SDT_MSILCpBlk,
                          [SDNPHasChain, SDNPMayLoad, SDNPMayStore]>;
def MSILinitblk  : SDNode<"MSILISD::INITBLK", SDT_MSILInitBlk,
                          [SDNPHasChain, SDNPMayStore]>;
def MSILmemmove  : SDNode<"MSILISD::MEMMOVE", SDT_MSILMemMove,
                          [SDNPHasChain, SDNPMayLoad, SDNPMayStore]>;
def MSILmemfill  : SDNode<"MSILISD::MEMFILL", SDT_MSILMemFill,
                          [SDNPHasChain, SDNPMayStore]>;
// MSILISD::LOCALLOC: see passes-frame 6.8.
```

### 10.3 Operands

```tablegen
let OperandNamespace = "MSIL" in {
let OperandType = "OPERAND_BASIC_BLOCK" in def bb_op : Operand<OtherVT>;
let OperandType = "OPERAND_I32IMM" in def i32imm_op : Operand<i32>;
let OperandType = "OPERAND_I64IMM" in def i64imm_op : Operand<i64>;
let OperandType = "OPERAND_F32IMM" in def f32imm_op : Operand<f32>;
let OperandType = "OPERAND_F64IMM" in def f64imm_op : Operand<f64>;
let OperandType = "OPERAND_GLOBAL"   in def global_op   : Operand<i64>;
let OperandType = "OPERAND_FUNCTION" in def function_op : Operand<i64>;
let OperandType = "OPERAND_CALLCONV" in def callconv_op : Operand<i32>;
let OperandType = "OPERAND_MEMFLAGS" in
def memflags_op : Operand<i32> { let PrintMethod = "printMemFlagsPrefix"; }
let OperandType = "OPERAND_BRLIST" in
def brlist : Operand<i32> { let PrintMethod = "printBrList"; }
}
// (load/store root) -> (memflags, addr)
def MemOps : ComplexPattern<i64, 2, "SelectMemOperands"> { let WantsRoot = 1; }
def bool_node : PatLeaf<(i32 I32:$cond), [{
  return CurDAG->computeKnownBits(Op).countMinLeadingZeros() == 31;
}]>;
def imm32s : ImmLeaf<i64, [{ return isInt<32>(Imm); }]>;
def imm32u : ImmLeaf<i64, [{ return isUInt<32>(Imm) && !isInt<32>(Imm); }]>;
```

### 10.4 Arguments, constants, addresses

```tablegen
multiclass ARGUMENT<MSILRegClass rc, ValueType vt> {
  let hasSideEffects = 1, isCodeGenOnly = 1, Defs = []<Register>,
      Uses = [ARGUMENTS] in
  defm ARGUMENT_#vt : I<(outs rc:$res), (ins i32imm:$argno),
                        (outs), (ins i32imm:$argno),
                        [(set (vt rc:$res), (MSILargument timm:$argno))],
                        "ldarg\t$res, $argno", "ldarg\t$argno">;
}
defm "" : ARGUMENT<I32, i32>;  defm "" : ARGUMENT<I64, i64>;
defm "" : ARGUMENT<F32, f32>;  defm "" : ARGUMENT<F64, f64>;

let isMoveImm = 1, isAsCheapAsAMove = 1, isReMaterializable = 1 in {
defm CONST_I32 : I<(outs I32:$res), (ins i32imm_op:$imm), (outs), (ins i32imm_op:$imm),
                   [(set I32:$res, imm:$imm)], "ldc.i4\t$res, $imm", "ldc.i4\t$imm", 0x0020>;
// Macro: printer emits ldc.i4/ldc.i8 + conv.i/conv.u (section 4).
defm CONST_I64 : I<(outs I64:$res), (ins i64imm_op:$imm), (outs), (ins i64imm_op:$imm),
                   [(set I64:$res, imm:$imm)], "ldc.nint\t$res, $imm", "ldc.nint\t$imm">;
defm CONST_F32 : I<(outs F32:$res), (ins f32imm_op:$imm), (outs), (ins f32imm_op:$imm),
                   [(set F32:$res, fpimm:$imm)], "ldc.r4\t$res, $imm", "ldc.r4\t$imm", 0x0022>;
defm CONST_F64 : I<(outs F64:$res), (ins f64imm_op:$imm), (outs), (ins f64imm_op:$imm),
                   [(set F64:$res, fpimm:$imm)], "ldc.r8\t$res, $imm", "ldc.r8\t$imm", 0x0023>;
}
let isAsCheapAsAMove = 1, isReMaterializable = 1 in {
let StackPeakExtra = 1 in
defm GLOBAL_ADDR : I<(outs I64:$res), (ins global_op:$sym), (outs), (ins global_op:$sym),
                     [(set I64:$res, (MSILDataAddr tglobaladdr:$sym))],
                     "global.addr\t$res, $sym", "global.addr\t$sym">;
defm FUNC_ADDR   : I<(outs I64:$res), (ins function_op:$fn), (outs), (ins function_op:$fn),
                     [(set I64:$res, (MSILFuncAddr tglobaladdr:$fn))],
                     "ldftn\t$res, $fn", "ldftn\t$fn", 0xFE06>;
}
```

`MSILInstrInfo::isReMaterializableImpl` must list `CONST_*`, `GLOBAL_ADDR` and `FUNC_ADDR`, as wasm does at InstrInfo.cpp:50-61.

### 10.5 Integer, compare, conversion

```tablegen
multiclass BinaryInt<SDPatternOperator node, string name, bits<16> opc> {
  defm _I32 : I<(outs I32:$dst), (ins I32:$lhs, I32:$rhs), (outs), (ins),
                [(set I32:$dst, (node I32:$lhs, I32:$rhs))],
                !strconcat(name, "\t$dst, $lhs, $rhs"), name, opc>;
  defm _I64 : I<(outs I64:$dst), (ins I64:$lhs, I64:$rhs), (outs), (ins),
                [(set I64:$dst, (node I64:$lhs, I64:$rhs))],
                !strconcat(name, "\t$dst, $lhs, $rhs"), name, opc>;
  // native int op sext(int32): same opcode, int32 operand (verified sign-extension).
  defm _I64_I32 : I<(outs I64:$dst), (ins I64:$lhs, I32:$rhs), (outs), (ins),
                    [(set I64:$dst, (node I64:$lhs, (sext I32:$rhs)))],
                    !strconcat(name, "\t$dst, $lhs, $rhs"), name, opc>;
}
let isCommutable = 1 in {
defm ADD : BinaryInt<add, "add", 0x58>;  defm MUL : BinaryInt<mul, "mul", 0x5A>;
defm AND : BinaryInt<and, "and", 0x5F>;  defm OR  : BinaryInt<or,  "or",  0x60>;
defm XOR : BinaryInt<xor, "xor", 0x61>;
}
defm SUB : BinaryInt<sub, "sub", 0x59>;
let hasSideEffects = 1 in {
defm DIV_S : BinaryInt<sdiv, "div", 0x5B>;    defm DIV_U : BinaryInt<udiv, "div.un", 0x5C>;
defm REM_S : BinaryInt<srem, "rem", 0x5D>;    defm REM_U : BinaryInt<urem, "rem.un", 0x5E>;
}
// x op imm32 -> mixed form with an int32 constant (no conv.i)
def : Pat<(add I64:$a, imm32s:$c), (ADD_I64_I32 $a, (CONST_I32 (trunc_imm $c)))>;
def : Pat<(and I64:$a, imm32s:$c), (AND_I64_I32 $a, (CONST_I32 (trunc_imm $c)))>;
// trunc_imm: an SDNodeXForm producing an i32 TargetConstant.

multiclass ShiftInt<SDPatternOperator node, string name, bits<16> opc> {
  defm _I32 : I<(outs I32:$dst), (ins I32:$lhs, I32:$amt), (outs), (ins),
                [(set I32:$dst, (node I32:$lhs, I32:$amt))], "", name, opc>;
  defm _I64 : I<(outs I64:$dst), (ins I64:$lhs, I32:$amt), (outs), (ins),
                [(set I64:$dst, (node I64:$lhs, I32:$amt))], "", name, opc>;
}
defm SHL   : ShiftInt<shl, "shl", 0x62>;
defm SHR_S : ShiftInt<sra, "shr", 0x63>;
defm SHR_U : ShiftInt<srl, "shr.un", 0x64>;
// Do NOT add wasm's (shl x, (and y, 31)) -> SHL patterns: CIL leaves >= width unspecified.

multiclass Unary<SDPatternOperator node, MSILRegClass rc, string name, bits<16> opc> {
  defm "" : I<(outs rc:$dst), (ins rc:$src), (outs), (ins),
              [(set rc:$dst, (node rc:$src))], "", name, opc>;
}
defm NEG_I32 : Unary<ineg, I32, "neg", 0x65>;  defm NEG_I64 : Unary<ineg, I64, "neg", 0x65>;
defm NOT_I32 : Unary<not,  I32, "not", 0x66>;  defm NOT_I64 : Unary<not,  I64, "not", 0x66>;

// Compares: no patterns on the defs; patterns below choose them.
multiclass Cmp<string name, bits<16> opc> {
  foreach rc = [I32, I64, F32, F64] in
  defm _#rc : I<(outs I32:$dst), (ins rc:$lhs, rc:$rhs), (outs), (ins), [],
                !strconcat(name, "\t$dst, $lhs, $rhs"), name, opc>;
}
let isCommutable = 1 in defm CEQ : Cmp<"ceq", 0xFE01>;
defm CGT : Cmp<"cgt", 0xFE02>;     defm CGT_UN : Cmp<"cgt.un", 0xFE03>;
defm CLT : Cmp<"clt", 0xFE04>;     defm CLT_UN : Cmp<"clt.un", 0xFE05>;

class Not<dag d> : Pat<d, (CEQ_I32 d, (CONST_I32 0))>; // illustrative only
foreach rc = [I32, I64] in {
  def : Pat<(setcc rc:$a, rc:$b, SETEQ),  (!cast<NI>("CEQ_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETNE),  (CEQ_I32 (!cast<NI>("CEQ_"#rc) $a, $b), (CONST_I32 0))>;
  def : Pat<(setcc rc:$a, rc:$b, SETLT),  (!cast<NI>("CLT_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETULT), (!cast<NI>("CLT_UN_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETGT),  (!cast<NI>("CGT_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETUGT), (!cast<NI>("CGT_UN_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETLE),  (CEQ_I32 (!cast<NI>("CGT_"#rc) $a, $b), (CONST_I32 0))>;
  def : Pat<(setcc rc:$a, rc:$b, SETULE), (CEQ_I32 (!cast<NI>("CGT_UN_"#rc) $a, $b), (CONST_I32 0))>;
  def : Pat<(setcc rc:$a, rc:$b, SETGE),  (CEQ_I32 (!cast<NI>("CLT_"#rc) $a, $b), (CONST_I32 0))>;
  def : Pat<(setcc rc:$a, rc:$b, SETUGE), (CEQ_I32 (!cast<NI>("CLT_UN_"#rc) $a, $b), (CONST_I32 0))>;
}
let AddedComplexity = 10 in
def : Pat<(setne I32:$a, 0), (CGT_UN_I32 $a, (CONST_I32 0))>;
foreach rc = [F32, F64] in {
  def : Pat<(setcc rc:$a, rc:$b, SETOEQ), (!cast<NI>("CEQ_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETUNE), (CEQ_I32 (!cast<NI>("CEQ_"#rc) $a, $b), (CONST_I32 0))>;
  def : Pat<(setcc rc:$a, rc:$b, SETOLT), (!cast<NI>("CLT_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETULT), (!cast<NI>("CLT_UN_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETOGT), (!cast<NI>("CGT_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETUGT), (!cast<NI>("CGT_UN_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETOLE), (CEQ_I32 (!cast<NI>("CGT_UN_"#rc) $a, $b), (CONST_I32 0))>;
  def : Pat<(setcc rc:$a, rc:$b, SETULE), (CEQ_I32 (!cast<NI>("CGT_"#rc) $a, $b), (CONST_I32 0))>;
  def : Pat<(setcc rc:$a, rc:$b, SETOGE), (CEQ_I32 (!cast<NI>("CLT_UN_"#rc) $a, $b), (CONST_I32 0))>;
  def : Pat<(setcc rc:$a, rc:$b, SETUGE), (CEQ_I32 (!cast<NI>("CLT_"#rc) $a, $b), (CONST_I32 0))>;
  // don't-care forms -> ordered/unordered equivalents
  def : Pat<(setcc rc:$a, rc:$b, SETEQ), (!cast<NI>("CEQ_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETNE), (CEQ_I32 (!cast<NI>("CEQ_"#rc) $a, $b), (CONST_I32 0))>;
  def : Pat<(setcc rc:$a, rc:$b, SETLT), (!cast<NI>("CLT_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETGT), (!cast<NI>("CGT_"#rc) $a, $b)>;
  def : Pat<(setcc rc:$a, rc:$b, SETLE), (CEQ_I32 (!cast<NI>("CGT_UN_"#rc) $a, $b), (CONST_I32 0))>;
  def : Pat<(setcc rc:$a, rc:$b, SETGE), (CEQ_I32 (!cast<NI>("CLT_UN_"#rc) $a, $b), (CONST_I32 0))>;
}

// Conversions (single opcode each)
class ConvI<string n, MSILRegClass d, MSILRegClass s, list<dag> p, bits<16> opc>;
defm CONV_I4_I64 : I<(outs I32:$d), (ins I64:$s), (outs), (ins), [(set I32:$d, (trunc I64:$s))], "", "conv.i4", 0x69>;
defm CONV_I_I32  : I<(outs I64:$d), (ins I32:$s), (outs), (ins), [(set I64:$d, (sext I32:$s))], "", "conv.i", 0xD3>;
defm CONV_U_I32  : I<(outs I64:$d), (ins I32:$s), (outs), (ins), [(set I64:$d, (zext I32:$s))], "", "conv.u", 0xE0>;
def : Pat<(i64 (anyext I32:$s)), (CONV_U_I32 $s)>;
defm CONV_I1_I32 : I<(outs I32:$d), (ins I32:$s), (outs), (ins), [(set I32:$d, (sext_inreg I32:$s, i8))],  "", "conv.i1", 0x67>;
defm CONV_I2_I32 : I<(outs I32:$d), (ins I32:$s), (outs), (ins), [(set I32:$d, (sext_inreg I32:$s, i16))], "", "conv.i2", 0x68>;
defm CONV_U1_I32 : I<(outs I32:$d), (ins I32:$s), (outs), (ins), [(set I32:$d, (and I32:$s, 255))],   "", "conv.u1", 0xD2>;
defm CONV_U2_I32 : I<(outs I32:$d), (ins I32:$s), (outs), (ins), [(set I32:$d, (and I32:$s, 65535))], "", "conv.u2", 0xD1>;
defm CONV_I1_I64 : I<(outs I32:$d), (ins I64:$s), (outs), (ins), [], "", "conv.i1", 0x67>;
defm CONV_I2_I64 : I<(outs I32:$d), (ins I64:$s), (outs), (ins), [], "", "conv.i2", 0x68>;
def : Pat<(sext_inreg I64:$s, i8),  (CONV_I_I32 (CONV_I1_I64 $s))>;
def : Pat<(sext_inreg I64:$s, i16), (CONV_I_I32 (CONV_I2_I64 $s))>;
def : Pat<(sext_inreg I64:$s, i32), (CONV_I_I32 (CONV_I4_I64 $s))>;
def : Pat<(and I64:$s, 0xffffffff), (CONV_U_I32 (CONV_I4_I64 $s))>;
// FP <-> FP / int (names: CONV_<cilop>_<srcclass>)
defm CONV_R8_F32 : I<(outs F64:$d), (ins F32:$s), (outs), (ins), [(set F64:$d, (fpextend F32:$s))], "", "conv.r8", 0x6C>;
defm CONV_R4_F64 : I<(outs F32:$d), (ins F64:$s), (outs), (ins), [(set F32:$d, (fpround F64:$s))],  "", "conv.r4", 0x6B>;
defm CONV_R4_I32 : I<(outs F32:$d), (ins I32:$s), (outs), (ins), [(set F32:$d, (sint_to_fp I32:$s))], "", "conv.r4", 0x6B>;
defm CONV_R8_I32 : I<(outs F64:$d), (ins I32:$s), (outs), (ins), [(set F64:$d, (sint_to_fp I32:$s))], "", "conv.r8", 0x6C>;
defm CONV_R4_I64 : I<(outs F32:$d), (ins I64:$s), (outs), (ins), [(set F32:$d, (sint_to_fp I64:$s))], "", "conv.r4", 0x6B>;
defm CONV_R8_I64 : I<(outs F64:$d), (ins I64:$s), (outs), (ins), [(set F64:$d, (sint_to_fp I64:$s))], "", "conv.r8", 0x6C>;
defm CONV_R_UN_I32 : I<(outs F64:$d), (ins I32:$s), (outs), (ins), [(set F64:$d, (uint_to_fp I32:$s))], "", "conv.r.un", 0x76>;
defm CONV_R_UN_I64 : I<(outs F64:$d), (ins I64:$s), (outs), (ins), [(set F64:$d, (uint_to_fp I64:$s))], "", "conv.r.un", 0x76>;
def : Pat<(f32 (uint_to_fp I32:$s)), (CONV_R4_F64 (CONV_R_UN_I32 $s))>;
// (f32 (uint_to_fp I64)) intentionally has no pattern: it is Custom -> Expand.
foreach src = [F32, F64] in {
  defm CONV_I4_#src : I<(outs I32:$d), (ins src:$s), (outs), (ins), [(set I32:$d, (fp_to_sint src:$s))], "", "conv.i4", 0x69>;
  defm CONV_U4_#src : I<(outs I32:$d), (ins src:$s), (outs), (ins), [(set I32:$d, (fp_to_uint src:$s))], "", "conv.u4", 0x6D>;
  defm CONV_I_#src  : I<(outs I64:$d), (ins src:$s), (outs), (ins), [(set I64:$d, (fp_to_sint src:$s))], "", "conv.i", 0xD3>;
  defm CONV_U_#src  : I<(outs I64:$d), (ins src:$s), (outs), (ins), [(set I64:$d, (fp_to_uint src:$s))], "", "conv.u", 0xE0>;
}
```

The `class ConvI`/`class Not` lines are illustrative placeholders. Delete them or flesh them out. The CIL opcode bytes shown are from ECMA-335 Partition III (from memory). The MC-encoding agent should check them against the spec in phase 2. They do not matter for text output.

### 10.6 Memory

```tablegen
multiclass Load<MSILRegClass rc, string name, bits<16> opc> {
  let mayLoad = 1, UseNamedOperandTable = 1 in
  defm "" : I<(outs rc:$dst), (ins memflags_op:$flags, I64:$addr),
              (outs), (ins memflags_op:$flags),
              [], !strconcat(name, "\t$dst, $addr$flags"),
              !strconcat("${flags}", name), opc>;
}
defm LDIND_I1 : Load<I32, "ldind.i1", 0x46>;  defm LDIND_U1 : Load<I32, "ldind.u1", 0x47>;
defm LDIND_I2 : Load<I32, "ldind.i2", 0x48>;  defm LDIND_U2 : Load<I32, "ldind.u2", 0x49>;
defm LDIND_I4 : Load<I32, "ldind.i4", 0x4A>;  defm LDIND_I  : Load<I64, "ldind.i",  0x4D>;
defm LDIND_R4 : Load<F32, "ldind.r4", 0x4E>;  defm LDIND_R8 : Load<F64, "ldind.r8", 0x4F>;

class LdPat<ValueType vt, PatFrag kind, NI inst>
  : Pat<(vt (kind (MemOps memflags_op:$f, I64:$a))), (inst $f, $a)>;
def : LdPat<i32, load, LDIND_I4>;          def : LdPat<i64, load, LDIND_I>;
def : LdPat<f32, load, LDIND_R4>;          def : LdPat<f64, load, LDIND_R8>;
def : LdPat<i32, sextloadi8,  LDIND_I1>;   def : LdPat<i32, sextloadi16, LDIND_I2>;
def : LdPat<i32, zextloadi8,  LDIND_U1>;   def : LdPat<i32, zextloadi16, LDIND_U2>;
def : LdPat<i32, extloadi8,   LDIND_U1>;   def : LdPat<i32, extloadi16,  LDIND_U2>;

multiclass Store<MSILRegClass rc, string name, bits<16> opc> {
  let mayStore = 1, UseNamedOperandTable = 1 in
  defm "" : I<(outs), (ins memflags_op:$flags, I64:$addr, rc:$val),
              (outs), (ins memflags_op:$flags),
              [], !strconcat(name, "\t$addr, $val$flags"),
              !strconcat("${flags}", name), opc>;
}
defm STIND_I1 : Store<I32, "stind.i1", 0x52>;  defm STIND_I2 : Store<I32, "stind.i2", 0x53>;
defm STIND_I4 : Store<I32, "stind.i4", 0x54>;  defm STIND_I  : Store<I64, "stind.i",  0xDF>;
defm STIND_R4 : Store<F32, "stind.r4", 0x56>;  defm STIND_R8 : Store<F64, "stind.r8", 0x57>;

class StPat<ValueType vt, MSILRegClass rc, PatFrag kind, NI inst>
  : Pat<(kind (vt rc:$v), (MemOps memflags_op:$f, I64:$a)), (inst $f, $a, $v)>;
def : StPat<i32, I32, store, STIND_I4>;    def : StPat<i64, I64, store, STIND_I>;
def : StPat<f32, F32, store, STIND_R4>;    def : StPat<f64, F64, store, STIND_R8>;
def : StPat<i32, I32, truncstorei8,  STIND_I1>;
def : StPat<i32, I32, truncstorei16, STIND_I2>;

let mayLoad = 1, mayStore = 1 in
defm CPBLK : I<(outs), (ins memflags_op:$flags, I64:$dst, I64:$src, I32:$len),
               (outs), (ins memflags_op:$flags),
               [(MSILcpblk I64:$dst, I64:$src, I32:$len, (i32 timm:$flags))],
               "cpblk\t$dst, $src, $len$flags", "${flags}cpblk", 0xFE17>;
let mayStore = 1 in
defm INITBLK : I<(outs), (ins memflags_op:$flags, I64:$dst, I32:$val, I32:$len),
                 (outs), (ins memflags_op:$flags),
                 [(MSILinitblk I64:$dst, I32:$val, I32:$len, (i32 timm:$flags))],
                 "initblk\t$dst, $val, $len$flags", "${flags}initblk", 0xFE18>;
let mayLoad = 1, mayStore = 1 in
defm MEMMOVE : I<(outs), (ins I64:$src, I64:$dst, I64:$len), (outs), (ins),
                 [(MSILmemmove I64:$src, I64:$dst, I64:$len)],
                 "memmove\t$src, $dst, $len",
                 "call\tvoid [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeMemory::Copy(void*, void*, native uint)">;
let mayStore = 1 in
defm MEMFILL : I<(outs), (ins I64:$dst, I64:$len, I32:$val), (outs), (ins),
                 [(MSILmemfill I64:$dst, I64:$len, I32:$val)],
                 "memfill\t$dst, $len, $val",
                 "call\tvoid [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeMemory::Fill(void*, native uint, uint8)">;
```

The `memflags` encoding is `MSIL::MemFlags`:
- bits 0-2 hold the unaligned value N ∈ {0, 1, 2, 4} (0 = no prefix);
- bit 3 is volatile.

`printMemFlagsPrefix` prints `unaligned. N ` and/or `volatile. `.

BCL method references in AsmStrings are only for illustration. The printer should own one table (enum → profile-specific member reference, section 5.9), so the MIs carry no assembly names.

### 10.7 Control flow

```tablegen
let isBranch = 1, isTerminator = 1, hasCtrlDep = 1 in {
defm BR_IF     : I<(outs), (ins bb_op:$dst, I32:$cond), (outs), (ins bb_op:$dst),
                   [(brcond I32:$cond, bb:$dst)], "brtrue\t$dst, $cond", "brtrue\t$dst", 0x3A>;
defm BR_UNLESS : I<(outs), (ins bb_op:$dst, I32:$cond), (outs), (ins bb_op:$dst),
                   [], "brfalse\t$dst, $cond", "brfalse\t$dst", 0x39>;
let isBarrier = 1 in
defm BR : NRI<(outs), (ins bb_op:$dst), [(br bb:$dst)], "br\t$dst", 0x38>;
multiclass BCC<string name, bits<16> opc> {
  foreach rc = [I32, I64, F32, F64] in
  defm _#rc : I<(outs), (ins bb_op:$dst, rc:$lhs, rc:$rhs), (outs), (ins bb_op:$dst),
                [], !strconcat(name, "\t$dst, $lhs, $rhs"), !strconcat(name, "\t$dst"), opc>;
}
defm BEQ : BCC<"beq", 0x3B>;      defm BNE_UN : BCC<"bne.un", 0x40>;
defm BGE : BCC<"bge", 0x3C>;      defm BGE_UN : BCC<"bge.un", 0x41>;
defm BGT : BCC<"bgt", 0x3D>;      defm BGT_UN : BCC<"bgt.un", 0x42>;
defm BLE : BCC<"ble", 0x3E>;      defm BLE_UN : BCC<"ble.un", 0x43>;
defm BLT : BCC<"blt", 0x3F>;      defm BLT_UN : BCC<"blt.un", 0x44>;
}
def : Pat<(brcond (i32 (setne I32:$c, 0)), bb:$d), (BR_IF bb_op:$d, I32:$c)>;
def : Pat<(brcond (i32 (seteq I32:$c, 0)), bb:$d), (BR_UNLESS bb_op:$d, I32:$c)>;
def : Pat<(brcond (i32 (xor bool_node:$c, (i32 1))), bb:$d), (BR_UNLESS bb_op:$d, I32:$c)>;

multiclass BrCC<CondCode cc, string inst, list<MSILRegClass> rcs> {
  foreach rc = rcs in
  def : Pat<(brcond (i32 (setcc rc:$a, rc:$b, cc)), bb:$d),
            (!cast<NI>(inst # "_" # rc) bb_op:$d, rc:$a, rc:$b)>;
}
// integer
defm : BrCC<SETEQ, "BEQ", [I32, I64]>;    defm : BrCC<SETNE,  "BNE_UN", [I32, I64]>;
defm : BrCC<SETLT, "BLT", [I32, I64]>;    defm : BrCC<SETULT, "BLT_UN", [I32, I64]>;
defm : BrCC<SETLE, "BLE", [I32, I64]>;    defm : BrCC<SETULE, "BLE_UN", [I32, I64]>;
defm : BrCC<SETGT, "BGT", [I32, I64]>;    defm : BrCC<SETUGT, "BGT_UN", [I32, I64]>;
defm : BrCC<SETGE, "BGE", [I32, I64]>;    defm : BrCC<SETUGE, "BGE_UN", [I32, I64]>;
// floating point (ECMA: bge = !clt.un, ble = !cgt.un, bge.un = !clt, ble.un = !cgt)
defm : BrCC<SETOEQ, "BEQ", [F32, F64]>;   defm : BrCC<SETUNE, "BNE_UN", [F32, F64]>;
defm : BrCC<SETOLT, "BLT", [F32, F64]>;   defm : BrCC<SETULT, "BLT_UN", [F32, F64]>;
defm : BrCC<SETOLE, "BLE", [F32, F64]>;   defm : BrCC<SETULE, "BLE_UN", [F32, F64]>;
defm : BrCC<SETOGT, "BGT", [F32, F64]>;   defm : BrCC<SETUGT, "BGT_UN", [F32, F64]>;
defm : BrCC<SETOGE, "BGE", [F32, F64]>;   defm : BrCC<SETUGE, "BGE_UN", [F32, F64]>;
defm : BrCC<SETEQ,  "BEQ", [F32, F64]>;   defm : BrCC<SETNE,  "BNE_UN", [F32, F64]>;
defm : BrCC<SETLT,  "BLT", [F32, F64]>;   defm : BrCC<SETLE,  "BLE",    [F32, F64]>;
defm : BrCC<SETGT,  "BGT", [F32, F64]>;   defm : BrCC<SETGE,  "BGE",    [F32, F64]>;

let isTerminator = 1, hasCtrlDep = 1, isBarrier = 1, isNotDuplicable = 1 in {
defm BR_TABLE_I32 : I<(outs), (ins I32:$index, variable_ops), (outs), (ins brlist:$brl),
                      [(MSILbr_table I32:$index)], "switch\t$index", "switch\t$brl", 0x45>;
defm BR_TABLE_I64 : I<(outs), (ins I64:$index, variable_ops), (outs), (ins brlist:$brl),
                      [(MSILbr_table I64:$index)], "switch\t$index", "switch\t$brl", 0x45>;
}
let hasCtrlDep = 1, isBarrier = 1, isTerminator = 1 in {
let isReturn = 1 in
defm RETURN : I<(outs), (ins variable_ops), (outs), (ins), [(MSILreturn)], "ret", "ret", 0x2A>;
let isTrap = 1, StackPeakExtra = 1 in
defm TRAP : NRI<(outs), (ins), [(trap)], "ldnull\n\tthrow">;
}
let isTrap = 1, hasSideEffects = 1 in
defm DEBUGTRAP : NRI<(outs), (ins), [(debugtrap)],
                     "call\tvoid [System.Runtime]System.Diagnostics.Debugger::Break()">;

let usesCustomInserter = 1, hasSideEffects = 0, isCodeGenOnly = 1 in
foreach rc = [I32, I64, F32, F64] in
defm SELECT_#rc : I<(outs rc:$dst), (ins rc:$t, rc:$f, I32:$c), (outs), (ins),
                    [(set rc:$dst, (select I32:$c, rc:$t, rc:$f))]>;
```

### 10.8 Calls (copy of wasm InstrCall.td with the table and type-index operands removed)

```tablegen
let isCodeGenOnly = 1 in {
defm ADJCALLSTACKDOWN : NRI<(outs), (ins i32imm:$amt, i32imm:$amt2), []>;
defm ADJCALLSTACKUP   : NRI<(outs), (ins i32imm:$amt, i32imm:$amt2), []>;
}
let isCall = 1 in {
let isPseudo = 1 in
defm CALL_PARAMS : I<(outs), (ins function_op:$callee, variable_ops),
                     (outs), (ins function_op:$callee), [],
                     "call_params\t$callee", "call_params\t$callee">;
let variadicOpsAreDefs = 1, usesCustomInserter = 1, isPseudo = 1 in
defm CALL_RESULTS : I<(outs), (ins variable_ops), (outs), (ins), [],
                      "call_results", "call_results">;
let variadicOpsAreDefs = 1 in
defm CALL : I<(outs), (ins function_op:$callee, variable_ops),
              (outs), (ins function_op:$callee), [], "call", "call\t$callee", 0x28>;
let variadicOpsAreDefs = 1 in
defm CALL_INDIRECT : I<(outs), (ins callconv_op:$cc, variable_ops),
                       (outs), (ins callconv_op:$cc), [], "calli\t$cc", "calli\t$cc", 0x29>;
}
```

---

## 11. C++ snippets for the lowering hooks

### 11.1 Calls, arguments, returns

```cpp
static void fail(const SDLoc &DL, SelectionDAG &DAG, const char *Msg) {
  MachineFunction &MF = DAG.getMachineFunction();
  DAG.getContext()->diagnose(
      DiagnosticInfoUnsupported(MF.getFunction(), Msg, DL.getDebugLoc()));
}

static bool callingConvSupported(CallingConv::ID CC) {
  return CC == CallingConv::C || CC == CallingConv::Fast ||
         CC == CallingConv::Cold || CC == CallingConv::PreserveMost ||
         CC == CallingConv::PreserveAll;
}

// Native-ABI vararg slot layout (Design 4.7). SysV x86-64 overflow area,
// AAPCS64 __stack, Win64 and Apple-arm64 char* va_list: each slot is
// alignTo(size, 8), aligned to max(8, align).
static uint64_t layoutVarargSlot(uint64_t &Offset, uint64_t Size, Align A) {
  A = std::max(A, Align(8));
  Offset = alignTo(Offset, A);
  uint64_t Slot = Offset;
  Offset += alignTo(Size, 8);
  return Slot;
}

SDValue MSILTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc DL = CLI.DL;
  SDValue Chain = CLI.Chain, Callee = CLI.Callee;
  MachineFunction &MF = DAG.getMachineFunction();
  const DataLayout &Layout = MF.getDataLayout();
  MVT PtrVT = getPointerTy(Layout);
  LLVMContext &Ctx = *DAG.getContext();

  if (!callingConvSupported(CLI.CallConv))
    fail(DL, DAG, "MSIL does not support this calling convention yet");
  if (CLI.IsPatchPoint)
    fail(DL, DAG, "MSIL does not support patchpoints");
  if (CLI.IsTailCall) { // M1: no `tail.` prefix
    if (CLI.CB && CLI.CB->isMustTailCall())
      fail(DL, DAG, "MSIL does not support musttail yet");
    CLI.IsTailCall = false;
  }
  // Direct calls must match the callee's definition exactly (FixFunctionBitcasts).
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    if (auto *F = dyn_cast<Function>(G->getGlobal()->stripPointerCastsAndAliases()))
      if (CLI.CB && CLI.CB->getFunctionType() != F->getFunctionType())
        fail(DL, DAG, "call type differs from callee type; "
                      "MSILFixFunctionBitcasts did not run");

  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  unsigned NumFixedArgs = 0;
  for (unsigned I = 0; I < Outs.size(); ++I) {
    const ISD::OutputArg &Out = Outs[I];
    if (Out.Flags.isNest() || Out.Flags.isInAlloca() ||
        Out.Flags.isInConsecutiveRegs() || Out.Flags.isInConsecutiveRegsLast() ||
        Out.Flags.isSwiftSelf() || Out.Flags.isSwiftError())
      fail(DL, DAG, "MSIL does not support this argument attribute");
    // Fixed byval: copy into a caller frame object, pass its address (wasm 1398-1413).
    if (!Out.Flags.isVarArg() && Out.Flags.isByVal() &&
        Out.Flags.getByValSize() != 0) {
      Align A = Out.Flags.getNonZeroByValAlign();
      int FI = MF.getFrameInfo().CreateStackObject(Out.Flags.getByValSize(), A,
                                                   /*isSS=*/false);
      SDValue FINode = DAG.getFrameIndex(FI, PtrVT);
      Chain = DAG.getMemcpy(Chain, DL, FINode, OutVals[I],
                            DAG.getConstant(Out.Flags.getByValSize(), DL, MVT::i32),
                            A, A, /*isVolatile=*/false, /*AlwaysInline=*/false,
                            /*CI=*/nullptr, std::nullopt, MachinePointerInfo(),
                            MachinePointerInfo());
      OutVals[I] = FINode;
    }
    NumFixedArgs += !Out.Flags.isVarArg();
  }

  // Varargs: one caller-frame buffer in native overflow-area layout.
  SDValue VarargBuf;
  if (CLI.IsVarArg) {
    SmallVector<std::pair<unsigned, uint64_t>, 8> Slots; // (OutIdx, offset)
    uint64_t Size = 0;
    for (unsigned I = NumFixedArgs; I < Outs.size(); ++I) {
      const ISD::OutputArg &Out = Outs[I];
      if (Out.Flags.isByVal()) // SysV MEMORY-class structs: bytes go *inline*
        Slots.push_back({I, layoutVarargSlot(Size, Out.Flags.getByValSize(),
                                             Out.Flags.getNonZeroByValAlign())});
      else {
        Type *Ty = OutVals[I].getValueType().getTypeForEVT(Ctx);
        Slots.push_back({I, layoutVarargSlot(Size, Layout.getTypeAllocSize(Ty),
                                             std::max(Out.Flags.getNonZeroOrigAlign(),
                                                      Layout.getABITypeAlign(Ty)))});
      }
    }
    if (Size) {
      int FI = MF.getFrameInfo().CreateStackObject(Size, Align(16), false);
      VarargBuf = DAG.getFrameIndex(FI, PtrVT);
      SmallVector<SDValue, 8> Chains;
      for (auto [I, Off] : Slots) {
        SDValue Addr = DAG.getNode(ISD::ADD, DL, PtrVT, VarargBuf,
                                   DAG.getConstant(Off, DL, PtrVT));
        if (Outs[I].Flags.isByVal())
          Chains.push_back(DAG.getMemcpy(
              Chain, DL, Addr, OutVals[I],
              DAG.getConstant(Outs[I].Flags.getByValSize(), DL, MVT::i32),
              Align(8), Outs[I].Flags.getNonZeroByValAlign(), false, false,
              nullptr, std::nullopt, MachinePointerInfo(), MachinePointerInfo()));
        else
          Chains.push_back(DAG.getStore(
              Chain, DL, OutVals[I], Addr,
              MachinePointerInfo::getFixedStack(MF, FI, Off)));
      }
      Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chains);
    } else {
      VarargBuf = DAG.getIntPtrConstant(0, DL);
    }
  }

  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getNode(MSILISD::FUNC_ADDR, DL, PtrVT,
                         DAG.getTargetGlobalAddress(G->getGlobal(), DL, PtrVT,
                                                    G->getOffset()));
  // ExternalSymbol callees were already wrapped by LowerExternalSymbol.

  SmallVector<SDValue, 16> Ops{Chain, Callee};
  Ops.append(OutVals.begin(),
             CLI.IsVarArg ? OutVals.begin() + NumFixedArgs : OutVals.end());
  if (CLI.IsVarArg)
    Ops.push_back(VarargBuf);

  SmallVector<EVT, 2> InTys;
  for (const ISD::InputArg &In : CLI.Ins)
    InTys.push_back(In.VT);
  InTys.push_back(MVT::Other);
  SDValue Res = DAG.getNode(MSILISD::CALL, DL, DAG.getVTList(InTys), Ops);
  for (unsigned I = 0; I < CLI.Ins.size(); ++I)
    InVals.push_back(Res.getValue(I));
  return Res.getValue(CLI.Ins.size());
}
```

`LowerFormalArguments`, `LowerReturn` and `CanLowerReturn` are wasm's (1590-1712) with three changes: the Swift and EmscriptenInvoke parts dropped, `MSILISD::*` substituted, and `CanLowerReturn` returning `Outs.size() <= 1`. Keep the `MF.getRegInfo().addLiveIn(MSIL::ARGUMENTS)`, the per-In `addParam`, the vararg `VarargBufferVreg`, and the final `computeSignatureVTs` assert.

### 11.2 Addresses, jump tables, misc

```cpp
SDValue MSILTargetLowering::LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const auto *GA = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = GA->getGlobal();
  EVT VT = Op.getValueType();
  if (GV->isThreadLocal()) {
    fail(DL, DAG, "thread-local storage is not supported yet");
    return DAG.getUNDEF(VT);
  }
  unsigned Opc = GV->getValueType()->isFunctionTy() ? MSILISD::FUNC_ADDR
                                                    : MSILISD::DATA_ADDR;
  return DAG.getNode(Opc, DL, VT,
                     DAG.getTargetGlobalAddress(GV, DL, VT, GA->getOffset()));
}

SDValue MSILTargetLowering::LowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const {
  const auto *ES = cast<ExternalSymbolSDNode>(Op);
  EVT VT = Op.getValueType();
  return DAG.getNode(MSILISD::EXT_SYM, SDLoc(Op), VT,
                     DAG.getTargetExternalSymbol(ES->getSymbol(), VT));
}

SDValue MSILTargetLowering::LowerBR_JT(SDValue Op, SelectionDAG &DAG) const {
  // Identical to wasm 2192-2216: index, all case MBBs, then MBBs[0] as a
  // never-taken default (LLVM's range check precedes BR_JT). The printer emits
  // `switch (...)` followed by `br <last operand>`.
  SDLoc DL(Op);
  const auto *JT = cast<JumpTableSDNode>(Op.getOperand(1));
  const auto &MBBs = DAG.getMachineFunction().getJumpTableInfo()
                         ->getJumpTables()[JT->getIndex()].MBBs;
  SmallVector<SDValue, 16> Ops{Op.getOperand(0), Op.getOperand(2)};
  for (MachineBasicBlock *MBB : MBBs)
    Ops.push_back(DAG.getBasicBlock(MBB));
  Ops.push_back(DAG.getBasicBlock(MBBs.front()));
  return DAG.getNode(MSILISD::BR_TABLE, DL, MVT::Other, Ops);
}

SDValue MSILTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  switch (Op.getOpcode()) {
  case ISD::FrameIndex:
    return DAG.getTargetFrameIndex(cast<FrameIndexSDNode>(Op)->getIndex(),
                                   Op.getValueType());
  case ISD::GlobalAddress:     return LowerGlobalAddress(Op, DAG);
  case ISD::ExternalSymbol:    return LowerExternalSymbol(Op, DAG);
  case ISD::JumpTable: {
    const auto *JT = cast<JumpTableSDNode>(Op);
    return DAG.getTargetJumpTable(JT->getIndex(), Op.getValueType());
  }
  case ISD::BR_JT:             return LowerBR_JT(Op, DAG);
  case ISD::CopyToReg:         return LowerCopyToReg(Op, DAG); // wasm 2008-2032, COPY_I64
  case ISD::VASTART:           return LowerVASTART(Op, DAG);
  case ISD::VACOPY:            return LowerVACOPY(Op, DAG);
  case ISD::SHL_PARTS: case ISD::SRA_PARTS: case ISD::SRL_PARTS: {
    SDValue Lo, Hi;
    expandShiftParts(Op.getNode(), Lo, Hi, DAG);
    return DAG.getMergeValues({Lo, Hi}, DL);
  }
  case ISD::UINT_TO_FP:
    // conv.r.un;conv.r4 double-rounds u64->f32 on .NET 8 / Framework.
    // SDValue() => LegalizeDAG Expand => ExpandLegalINT_TO_FP (__floatundisf).
    if (Op.getValueType() == MVT::f32)
      return SDValue();
    return Op;                       // i64 -> f64: conv.r.un is exact-rounded
  case ISD::DYNAMIC_STACKALLOC:    return LowerDYNAMIC_STACKALLOC(Op, DAG); // passes-frame 6.8
  case ISD::STACKSAVE:             return DAG.getMergeValues(
                                       {DAG.getConstant(0, DL, MVT::i64), Op.getOperand(0)}, DL);
  case ISD::STACKRESTORE:          return Op.getOperand(0);
  case ISD::RETURNADDR:            return DAG.getConstant(0, DL, Op.getValueType());
  case ISD::FRAMEADDR:             return DAG.getConstant(0, DL, Op.getValueType()); // M1
  case ISD::GlobalTLSAddress:
  case ISD::BlockAddress:
  case ISD::BRIND:
  case ISD::CLEAR_CACHE:
    fail(DL, DAG, "unsupported operation for MSIL");
    return Op.getOpcode() == ISD::BRIND || Op.getOpcode() == ISD::CLEAR_CACHE
               ? Op.getOperand(0) : DAG.getUNDEF(Op.getValueType());
  default:
    llvm_unreachable("unimplemented MSIL operation lowering");
  }
}
```

### 11.3 SelectionDAGInfo (memcpy, memset, memmove never become libcalls)

```cpp
static unsigned encodeMemFlags(Align A, unsigned NaturalSize, bool Volatile) {
  unsigned F = 0;
  if (A.value() < NaturalSize)
    F |= std::min<unsigned>(A.value(), 4);   // unaligned. 1/2/4
  if (Volatile) F |= MSIL::MF_Volatile;      // 8
  return F;
}

SDValue MSILSelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align DstAlign, Align SrcAlign, bool IsVolatile,
    bool AlwaysInline, MachinePointerInfo, MachinePointerInfo) const {
  if (auto *C = dyn_cast<ConstantSDNode>(Size); C && isUInt<32>(C->getZExtValue()))
    return DAG.getNode(MSILISD::CPBLK, DL, MVT::Other, Chain, Dst, Src,
                       DAG.getConstant(C->getZExtValue(), DL, MVT::i32),
                       DAG.getTargetConstant(
                           encodeMemFlags(std::min(DstAlign, SrcAlign), 8,
                                          IsVolatile), DL, MVT::i32));
  // Variable length: NativeMemory.Copy(src, dst, len) - memmove semantics.
  return DAG.getNode(MSILISD::MEMMOVE, DL, MVT::Other, Chain, Src, Dst,
                     DAG.getZExtOrTrunc(Size, DL, MVT::i64));
}

SDValue MSILSelectionDAGInfo::EmitTargetCodeForMemmove(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align, Align, bool, MachinePointerInfo,
    MachinePointerInfo) const {
  return DAG.getNode(MSILISD::MEMMOVE, DL, MVT::Other, Chain, Src, Dst,
                     DAG.getZExtOrTrunc(Size, DL, MVT::i64));
}

SDValue MSILSelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Val,
    SDValue Size, Align A, bool IsVolatile, bool AlwaysInline,
    MachinePointerInfo) const {
  SDValue V = DAG.getAnyExtOrTrunc(Val, DL, MVT::i32);
  if (auto *C = dyn_cast<ConstantSDNode>(Size); C && isUInt<32>(C->getZExtValue()))
    return DAG.getNode(MSILISD::INITBLK, DL, MVT::Other, Chain, Dst, V,
                       DAG.getConstant(C->getZExtValue(), DL, MVT::i32),
                       DAG.getTargetConstant(encodeMemFlags(A, 8, IsVolatile),
                                             DL, MVT::i32));
  return DAG.getNode(MSILISD::MEMFILL, DL, MVT::Other, Chain, Dst,
                     DAG.getZExtOrTrunc(Size, DL, MVT::i64), V);
}
```

`getMemcpy` tries the inline load/store expansion (`MaxStoresPerMemcpy`) before it calls this hook, and only calls a libcall if the hook returns `SDValue()`, which these hooks never do. On the net48 profile, `MEMMOVE` prints `conv.i8; dup; call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64)` (Peak 4). `MEMFILL` needs a runtime helper (phase 3).

### 11.4 `va_start` and `va_copy` (Design 4.7)

```cpp
SDValue MSILTargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MachineFunction &MF = DAG.getMachineFunction();
  auto *MFI = MF.getInfo<MSILFunctionInfo>();
  SDValue Chain = Op.getOperand(0), VaList = Op.getOperand(1);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  SDValue Buf = DAG.getCopyFromReg(DAG.getEntryNode(), DL,
                                   MFI->getVarargBufferVreg(), MVT::i64);
  const Triple &TT = Subtarget->getTargetTriple();
  auto At = [&](unsigned Off) {
    return DAG.getMemBasePlusOffset(VaList, TypeSize::getFixed(Off), DL);
  };
  if (TT.getArch() == Triple::x86_64 && !TT.isOSWindows()) {
    // __va_list_tag { i32 gp_offset; i32 fp_offset; ptr overflow; ptr save; }
    SDValue S0 = DAG.getStore(Chain, DL, DAG.getConstant(48, DL, MVT::i32), At(0),
                              MachinePointerInfo(SV));            // GPRs exhausted
    SDValue S1 = DAG.getStore(Chain, DL, DAG.getConstant(176, DL, MVT::i32), At(4),
                              MachinePointerInfo(SV, 4));         // XMMs exhausted
    SDValue S2 = DAG.getStore(Chain, DL, Buf, At(8), MachinePointerInfo(SV, 8));
    SDValue S3 = DAG.getStore(Chain, DL, DAG.getConstant(0, DL, MVT::i64), At(16),
                              MachinePointerInfo(SV, 16));
    return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, S0, S1, S2, S3);
  }
  if (TT.getArch() == Triple::aarch64 && !TT.isOSDarwin() && !TT.isOSWindows()) {
    // { ptr __stack; ptr __gr_top; ptr __vr_top; i32 __gr_offs; i32 __vr_offs; }
    // offs >= 0 means "read from __stack".
    SDValue S0 = DAG.getStore(Chain, DL, Buf, At(0), MachinePointerInfo(SV));
    SDValue Z64 = DAG.getConstant(0, DL, MVT::i64), Z32 = DAG.getConstant(0, DL, MVT::i32);
    SDValue S1 = DAG.getStore(Chain, DL, Z64, At(8),  MachinePointerInfo(SV, 8));
    SDValue S2 = DAG.getStore(Chain, DL, Z64, At(16), MachinePointerInfo(SV, 16));
    SDValue S3 = DAG.getStore(Chain, DL, Z32, At(24), MachinePointerInfo(SV, 24));
    SDValue S4 = DAG.getStore(Chain, DL, Z32, At(28), MachinePointerInfo(SV, 28));
    return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, S0, S1, S2, S3, S4);
  }
  // char* va_list: Windows x64/arm64, Apple arm64.
  return DAG.getStore(Chain, DL, Buf, VaList, MachinePointerInfo(SV));
}
// LowerVACOPY: getMemcpy(Chain, Dst, Src, size{24|32|8}, Align(8), ...) by the same triple test.
```

Both layouts are verified against clang's own `va_arg` lowering in this checkout:
- **SysV x86-64** (`clang/lib/CodeGen/Targets/X86.cpp:3264-3350`). Clang uses registers only if `gp_offset <= 48 - n*8` and `fp_offset <= 176 - n*16`, so 48/176 forces the overflow area. `overflow_arg_area` is field 2. It is rounded up to 16 when the type alignment exceeds 8 and advanced by the size rounded to 8. That matches `layoutVarargSlot`.
- **AAPCS64** (`clang/lib/CodeGen/Targets/AArch64.cpp:915-960`). The field order is `__stack, __gr_top, __vr_top, __gr_offs, __vr_offs`, and "If reg_offs >= 0 we're already using the stack".

Varargs are not needed by the milestone-1 corpus, which prints through `puts` only.

### 11.5 DAGToDAG: CALL split and memory operands

```cpp
void MSILDAGToDAGISel::Select(SDNode *Node) {
  if (Node->isMachineOpcode()) { Node->setNodeId(-1); return; }
  SDLoc DL(Node);
  if (Node->getOpcode() == MSILISD::CALL) {
    SmallVector<SDValue, 16> Ops;
    for (unsigned I = 1, E = Node->getNumOperands(); I < E; ++I) {
      SDValue Op = Node->getOperand(I);
      if (I == 1 && (Op.getOpcode() == MSILISD::FUNC_ADDR ||
                     Op.getOpcode() == MSILISD::EXT_SYM)) {
        SDValue Inner = Op.getOperand(0);
        if (auto *G = dyn_cast<GlobalAddressSDNode>(Inner)) {
          if (isa<Function>(G->getGlobal()->stripPointerCastsAndAliases()))
            Op = Inner;            // direct call
        } else {
          Op = Inner;              // TargetExternalSymbol
        }
      }
      Ops.push_back(Op);
    }
    Ops.push_back(Node->getOperand(0)); // chain last
    MachineSDNode *Params =
        CurDAG->getMachineNode(MSIL::CALL_PARAMS, DL, MVT::Glue, Ops);
    MachineSDNode *Results = CurDAG->getMachineNode(
        MSIL::CALL_RESULTS, DL, Node->getVTList(), SDValue(Params, 0));
    ReplaceNode(Node, Results);
    return;
  }
  SelectCode(Node);
}

bool MSILDAGToDAGISel::SelectMemOperands(SDNode *Root, SDValue N,
                                         SDValue &Flags, SDValue &Addr) {
  auto *Mem = dyn_cast<MemSDNode>(Root); // load/store is the pattern root
  if (!Mem) return false;
  unsigned Size = Mem->getMemoryVT().getStoreSize().getFixedValue();
  Flags = CurDAG->getTargetConstant(
      encodeMemFlags(Mem->getAlign(), Size, Mem->isVolatile()), SDLoc(N), MVT::i32);
  Addr = N;                              // may be a TargetFrameIndex
  return true;
}
```

`LowerCallResults` in the custom inserter is wasm's (786-911) minus funcref, tables and ret-calls:
- **Indirect** (operand 0 is a register or an FI): move the callee to the end of the use list and build `CALL_INDIRECT` with defs, then `cc` (immediate 0 = Managed), then uses.
- **Direct:** build `CALL` with defs, then uses (the callee symbol first).

### 11.6 The SELECT custom inserter (triangle + PHI)

```cpp
// %dst = SELECT_xx %t, %f, %c
//   BB:    ...; brtrue %c, Sink          (falls through to FalseBB)
//   False: (empty, falls through)
//   Sink:  %dst = PHI [%t, BB], [%f, False]
// PHI elimination yields: r = t; brtrue c, Sink; r = f; Sink:  (C# '?:' shape)
static MachineBasicBlock *emitSelect(MachineInstr &MI, MachineBasicBlock *BB,
                                     const TargetInstrInfo &TII) {
  MachineFunction *MF = BB->getParent();
  DebugLoc DL = MI.getDebugLoc();
  Register Dst = MI.getOperand(0).getReg(), T = MI.getOperand(1).getReg(),
           F = MI.getOperand(2).getReg(), C = MI.getOperand(3).getReg();
  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();
  MachineBasicBlock *FalseMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MF->insert(It, FalseMBB);
  MF->insert(It, SinkMBB);
  SinkMBB->splice(SinkMBB->begin(), BB, std::next(MI.getIterator()), BB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(BB);
  BB->addSuccessor(FalseMBB);
  BB->addSuccessor(SinkMBB);
  FalseMBB->addSuccessor(SinkMBB);
  BuildMI(BB, DL, TII.get(MSIL::BR_IF)).addMBB(SinkMBB).addReg(C);
  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(TargetOpcode::PHI), Dst)
      .addReg(T).addMBB(BB).addReg(F).addMBB(FalseMBB);
  MI.eraseFromParent();
  return SinkMBB;
}

MachineBasicBlock *
MSILTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *Subtarget->getInstrInfo();
  switch (MI.getOpcode()) {
  case MSIL::SELECT_I32: case MSIL::SELECT_I64:
  case MSIL::SELECT_F32: case MSIL::SELECT_F64:
    return emitSelect(MI, BB, TII);
  case MSIL::CALL_RESULTS:
    return LowerCallResults(MI, MI.getDebugLoc(), BB, TII);
  default:
    llvm_unreachable("unexpected instr type to insert");
  }
}
```

There is a valid alternative, verified in `cilsem.il` `SelStack`: a single MI that keeps the value on the stack.

```
a b c; brtrue.s L; stloc tmp; pop; ldloc tmp; br.s D; L: pop; D:
```

It needs one scratch local per type and internal labels in the printer, and the JIT spills the non-empty stack at the join. Keep the triangle for M1. Measure in phase 4.

### 11.7 InstrInfo branch analysis: the inverse table differs for FP

`analyzeBranch`, `insertBranch`, `removeBranch` and `reverseBranchCondition` must understand `BR`, `BR_IF`, `BR_UNLESS` and the fused `B*` family. Encode `Cond` as `{imm(opcode), lhs, rhs}` for the fused family and `{imm(BR_IF|BR_UNLESS), c}` for the others. Inverses:

| Branch | Integer inverse | FP inverse |
|---|---|---|
| beq | bne.un | bne.un |
| bne.un | beq | beq |
| blt | bge | **bge.un** |
| blt.un | bge.un | **bge** |
| ble | bgt | **bgt.un** |
| ble.un | bgt.un | **bgt** |
| bgt | ble | **ble.un** |
| bgt.un | ble.un | **ble** |
| bge | blt | **blt.un** |
| bge.un | blt.un | **blt** |
| brtrue | brfalse | brfalse |

The FP column follows from section 3.2: `blt` is OLT, whose complement is UGE = `bge.un`.

---

## 12. Interactions with other components

- **ExplicitLocals (passes-frame §7).** The ARGUMENT vreg → `ldarg`/`starg`. `LOCAL_TEE` prints `dup; stloc` (Peak +1). `DROP` prints `pop`. Unused call results get `DROP`, because wasm keeps a def for every IR return value and the signature stays correct.
- **RegStackify.** Keep wasm's DIV/REM exemption (no CIL fp→int conversion traps, so drop those cases). Add the LOCALLOC "never stackify the def" rule (passes-frame §5.5). Remat relies on `isAsCheapAsAMove` + `isReMaterializable` on `CONST_*`, `GLOBAL_ADDR` and `FUNC_ADDR`.
- **Passes not ported** (CFGStackify, LowerBrUnless, FALLTHROUGH_RETURN, SetP2AlignOperands). `BR_UNLESS` is CIL `brfalse`. memflags come from ISel through the `WantsRoot` ComplexPattern, so no SetP2Align-style pass is needed. If a later MIR pass loses MMOs, nothing breaks, because the flags are operands.
- **FrameLowering.** passes-frame's `ADD_PTR_I32`/`AND_PTR_I32` are this report's `ADD_I64_I32`/`AND_I64_I32`.
- **AsmPrinter.** It needs:
  - the per-profile member table (section 5.9);
  - the `MemFlags` prefix printer;
  - the `CONST_I64` form choice;
  - `BR_TABLE` printing (`switch` + `br`);
  - `TRAP` (`ldnull; throw`);
  - the signature printers (section 8.3).
- **Clang/driver.** `-ffreestanding` keeps `memcpy(…)` calls in the source as calls to the `memcpy` symbol (no builtin). In M1 such a call reaches the printer as an undefined external function and must be diagnosed, or the corpus avoids explicit `memcpy`. Intrinsic `llvm.memcpy` (struct copies) is always handled by `MSILSelectionDAGInfo`.

---

## 13. Test plan for the ISel work

LLC FileCheck tests (`llvm/test/CodeGen/MSIL/`, RUN: `llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %s | FileCheck %s`):
1. `i32-arith.ll`, `i64-arith.ll`: every binary op; `neg`/`not`; div/rem forms; mixed `add (sext)` → no `conv.i`.
2. `icmp.ll`: all 10 predicates, both widths, checking the `ceq; ldc.i4.0; ceq` forms and `x != 0` → `cgt.un`.
3. `fcmp.ll`: all 14 predicates, f32/f64. Check the Expand for `o`/`uno`/`one`/`ueq`.
4. `br-fused.ll`: `brcond (icmp/fcmp)` → single `b*`; plus a branch-folding test that needs the inverse table (FP `blt` → `bge.un`).
5. `select.ll`: the triangle shape; `select` of each type.
6. `conv.ll`: every row of 5.4, especially `uitofp i64 to float` (expects the `__floatundisf` select form, no `conv.r.un; conv.r4`).
7. `load-store.ll`: the ext/trunc matrix; i64 extloads → `ldind.i4; conv.i`; `align 1` → `unaligned. 1`; `volatile` → `volatile.`.
8. `memcpy.ll`: constant memcpy/memset → `cpblk`/`initblk`; variable → `NativeMemory::Copy/Fill`; memmove → Copy; never `call memcpy`.
9. `global-addr.ll`, `func-addr.ll`: `GLOBAL_ADDR` with offset folding; `ldftn` with signature.
10. `call.ll`, `call-indirect.ll`: direct, indirect (`calli` signature), void and unused results (`pop`), i128 args split, `{i64,i64}` return demoted to sret.
11. `switch.ll`: `switch (…)` + `br`.
12. `i128.ll`: add/sub/mul/shift inline (no `__multi3`/`__ashlti3`), sdiv → `__divti3` call (expected, diagnosed by the printer in M1).
13. `bitops.ll`: ctlz/cttz/ctpop/bswap/rotl with the `BitOperations` calls (net8) and the Expand path (net48).
14. `fp-math.ll`: `frem` → `rem`; fabs/sqrt/ceil/…; fminimum → `Math::Min`.
15. `trap.ll`: `llvm.trap` and `unreachable` → `ldnull; throw`; a noreturn call at a function's end still gets a terminator.
16. `varargs.ll` (once enabled): buffer layout of `i32, double, i128, byval struct` on SysV.

Execution tests (lit, ilasm + dotnet) should port `cilsem.il`'s probes as C programs. Shift-by-variable, NaN compares, u64→float and fmod give one-line expected outputs.

---

## 14. Risks and open questions

1. **Native int for all 64-bit values.** This is cleanest for verification and pointers, but exported C# APIs (phase 2) want `int64` for `long long`. Proposal: print exported signatures with C-level types and add `conv` at the boundaries. The ILP32 triples need a different rule (open).
2. **Shift-count semantics.** CoreCLR masks, but the spec says unspecified. We keep LLVM's masks, which costs nothing because RyuJIT folds `& 31`. Never add wasm's mask-stripping patterns.
3. **Runtime-version-dependent conversions.**
   - `conv.r.un; conv.r4` is wrong on .NET 8 and Framework and right on .NET 10. We always use the expansion.
   - Out-of-range `conv.i4` changed in .NET 9 (saturating). This only matters for `fptosi.sat`, which stays Expand.
4. **ECMA float precision.** ECMA allows F to be wider than `float32`. All 64-bit JITs keep single precision (verified on x64), but the .NET Framework **x86** JIT uses x87. For the later i686 + net48 triple, emit `conv.r4`/`conv.r8` after every FP op.
5. **Select diamonds.** -O2 code has many selects. RyuJIT if-converts simple diamonds (.NET 8+), but this is not measured. Keep the in-stack form ready.
6. **memcpy and memmove.**
   - Variable-length memcpy uses `NativeMemory.Copy`, which is .NET 7+ (fine for net8).
   - On net48: `Buffer.MemoryCopy` for copies, and a runtime helper for variable-length memset.
   - Volatile variable-length memcpy loses its `volatile.` prefix (acceptable).
7. **libcall set inheritance.** MSIL triples get X86's or AArch64's libcalls. An eventual `MSILSystemLibrary` must win over `isX86`/`isAArch64` explicitly (first match, def-name order).
8. **BCL member identity per profile.** Section 5.9 is net8-verified only. For net48, `BitOperations`, `NativeMemory`, `MathF`, `BitConverter.SingleToInt32Bits`, `Math.CopySign`, `Math.FusedMultiplyAdd` and `Double.MinNumber` do not exist (so those ops are Expand/LibCall), and `Math.Min` has non-IEEE −0 behavior (verified).
9. **Atomics** are only sketched. `Interlocked.CompareExchange(int32&, …)` with a native int pointer works (verified), so phase 3 can lower `cmpxchg` to it, `atomicrmw` through AtomicExpand CAS loops, and fences to `Interlocked.MemoryBarrier`.
10. **Function-pointer identity.** `ldftn` of the same method compares equal (verified). Cross-assembly identity and native-callable thunks are Design 4.5 (phase 2+).
11. **Unverified items.** CIL opcode byte values in section 10 (text output does not need them); whether RyuJIT folds `initonly` static native int fields at Tier1; the ILVerify package names; that `-gen-sd-node-info` accepts the `MSILISD::` namespace without extra options (wasm passes none, CMakeLists.txt).

---

## Appendix A: raw probe output (.NET 8, tiered)

```
shl32(1,32) = 1        shl32(1,33) = 2       shr32(-8,32) = -8      shrun32(-8,33) = 0x7FFFFFFC
shlI(1,64) = 1         shlI(1,65) = 2        shlConstFold 1<<33 = 2
clt(NaN,1)=0 clt.un=1 cgt=0 cgt.un=1 ceq(NaN,NaN)=0
blt=0 blt.un=1 bge=0 bge.un=1 ble=0 ble.un=1 bgt=0 bgt.un=1 beq(NaN,NaN)=0 bne.un=1 bge(2,1)=1 ble(1,1)=1
conv.r.un(u64 max)->r8 bits=0x43F0000000000000
conv.r.un;conv.r4(0x8000008000000001) bits(as r8)=0x43E0000000000000   [net10: 0x43E0000020000000]
conv.r4(i64 0x4000004000000001)       bits(as r8)=0x43D0000020000000   (correct)
conv.u8(1.8e19) = 0xF9CCD8A1C5080000 (=18000000000000000000)   conv.u4(4e9)=4000000000
conv.i4(3e9) = 0x80000000   [net10: 0x7FFFFFFF]
rem(5.5,2)=1.5 rem(-5.5,2)=-1.5 rem(-0.0,1)=-0 rem(inf,2)=NaN rem(5,inf)=5 rem(5,0)=NaN rem(1e300,3)=0
neg(0.0) = -0          f32 16777216f+1f = 16777216
Math.Min(-0.0,0.0)=-0 Math.Min(0.0,-0.0)=-0 Math.Min(NaN,1)=NaN Math.Max(-0.0,0.0)=0   [Framework: Min(-0,0)=+0]
MixAdd(native 5, int64 7)=12 (JIT ok; PEVerify: Unexpected type)   calli Add1(41)=42   ldftn stable eq=1
switch(native int 2)=102   switch(native int 0x100000001)=default
div(INT_MIN,-1) -> OverflowException   rem(INT_MIN,-1) -> OverflowException
localloc with non-empty stack -> InvalidProgramException   .maxstack too small -> InvalidProgramException
0x100000000 + (int32)-1 = 0xFFFFFFFF   0x123456789 & (int32)-16 = 0x123456780   (int32 sign-extended)
```
