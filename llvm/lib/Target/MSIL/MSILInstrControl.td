//===- MSILInstrControl.td - MSIL control-flow codegen -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyInstrControl.td at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL control-flow code-gen constructs. Branches always use the long CIL
/// forms; block labels are basic-block symbols.
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MSIL-specific DAG Nodes.
//===----------------------------------------------------------------------===//

// (chain, index, jump-table entry MBBs..., first entry MBB again): the int32
// index is the jump-table index, which SelectionDAGBuilder range-checks before
// the table unless the switch's default is unreachable. The last MBB is a
// dummy target, not the switch's default (MSILTargetLowering::LowerBR_JT).
def SDT_MSILBrTable : SDTypeProfile<0, -1, [SDTCisVT<0, i32>]>;
def MSILbr_table : SDNode<"MSILISD::BR_TABLE", SDT_MSILBrTable,
                          [SDNPHasChain, SDNPVariadic]>;

// An int32 that is known to be 0 or 1. The matcher may try the predicate on a
// compare operand of another type before it checks the type.
def bool_node : PatLeaf<(i32 I32:$cond), [{
  return Op.getValueType() == MVT::i32 &&
         CurDAG->MaskedValueIsZero(Op, APInt::getBitsSetFrom(32, 1));
}]>;

//===----------------------------------------------------------------------===//
// Branches.
//===----------------------------------------------------------------------===//

let isBranch = 1, isTerminator = 1, hasCtrlDep = 1 in {
// The condition operand is a boolean value which MSIL represents as i32:
// brtrue branches when it is not zero, brfalse when it is zero.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
defm BR_UNLESS : I<(outs), (ins bb_op:$dst, I32:$cond), [],
                   "brfalse $dst", 0x39>;
defm BR_IF : I<(outs), (ins bb_op:$dst, I32:$cond),
               [(brcond I32:$cond, bb:$dst)],
               "brtrue $dst", 0x3A>;
let isBarrier = 1 in
defm BR : I<(outs), (ins bb_op:$dst),
            [(br bb:$dst)],
            "br $dst", 0x38>;
} // isBranch = 1, isTerminator = 1, hasCtrlDep = 1

def : Pat<(brcond (i32 (setne I32:$cond, 0)), bb:$dst),
          (BR_IF bb_op:$dst, I32:$cond)>;
def : Pat<(brcond (i32 (seteq I32:$cond, 0)), bb:$dst),
          (BR_UNLESS bb_op:$dst, I32:$cond)>;
def : Pat<(brcond (i32 (xor bool_node:$cond, (i32 1))), bb:$dst),
          (BR_UNLESS bb_op:$dst, I32:$cond)>;
// DAGCombiner turns 'brcond (xor x, 1)' into 'brcond (setne x, 1)'
// (rebuildSetCC); on a boolean that is brfalse too, and 'x == 1' is brtrue.
def : Pat<(brcond (i32 (setne bool_node:$cond, 1)), bb:$dst),
          (BR_UNLESS bb_op:$dst, I32:$cond)>;
def : Pat<(brcond (i32 (seteq bool_node:$cond, 1)), bb:$dst),
          (BR_IF bb_op:$dst, I32:$cond)>;

// Compare and branch: 'b<cc> L' pops $rhs and then $lhs, which were pushed in
// operand order, and branches to L when '$lhs <cc> $rhs' holds. Both
// operands have the same class: int32, native int, float32 or float64.
// On integers, the '.un' forms compare unsigned. On floating-point values,
// beq, blt, ble, bgt and bge are false when an operand is a NaN (ordered),
// and bne.un, blt.un, ble.un, bgt.un and bge.un are true then (unordered):
// blt is OLT, blt.un is ULT, bne.un is UNE, and so on (isel.md 5.3, verified
// with NaN). Their inverses differ for floating point
// (MSILInstrInfo::reverseBranchCondition): the inverse of blt is bge on
// integers, but bge.un on floating-point values.
multiclass CompareBranch<string name, bits<16> cilop> {
  foreach rc = [I32, I64, F32, F64] in
  defm _#rc : I<(outs), (ins bb_op:$dst, rc:$lhs, rc:$rhs), [],
                name # " $dst", cilop>;
}

let isBranch = 1, isTerminator = 1, hasCtrlDep = 1, hasSideEffects = 0,
    mayLoad = 0, mayStore = 0 in {
defm BEQ : CompareBranch<"beq", 0x3B>;
defm BGE : CompareBranch<"bge", 0x3C>;
defm BGT : CompareBranch<"bgt", 0x3D>;
defm BLE : CompareBranch<"ble", 0x3E>;
defm BLT : CompareBranch<"blt", 0x3F>;
defm BNE_UN : CompareBranch<"bne.un", 0x40>;
defm BGE_UN : CompareBranch<"bge.un", 0x41>;
defm BGT_UN : CompareBranch<"bgt.un", 0x42>;
defm BLE_UN : CompareBranch<"ble.un", 0x43>;
defm BLT_UN : CompareBranch<"blt.un", 0x44>;
} // isBranch = 1, isTerminator = 1, hasCtrlDep = 1, hasSideEffects = 0, ...

// brcond of a compare becomes one compare-and-branch. A compare with 0 as
// the condition of brtrue or brfalse is matched above instead: the literal 0
// makes those patterns more specific.
multiclass CompareBranchPats<list<CondCode> ccs, string inst,
                             list<MSILRegClass> rcs> {
  foreach rc = rcs in
  foreach cc = ccs in
  def : Pat<(brcond (i32 (setcc rc:$lhs, rc:$rhs, cc)), bb:$dst),
            (!cast<Instruction>(inst#"_"#rc) bb_op:$dst, rc:$lhs, rc:$rhs)>;
}
// Integers.
defm : CompareBranchPats<[SETEQ], "BEQ", [I32, I64]>;
defm : CompareBranchPats<[SETNE], "BNE_UN", [I32, I64]>;
defm : CompareBranchPats<[SETLT], "BLT", [I32, I64]>;
defm : CompareBranchPats<[SETULT], "BLT_UN", [I32, I64]>;
defm : CompareBranchPats<[SETLE], "BLE", [I32, I64]>;
defm : CompareBranchPats<[SETULE], "BLE_UN", [I32, I64]>;
defm : CompareBranchPats<[SETGT], "BGT", [I32, I64]>;
defm : CompareBranchPats<[SETUGT], "BGT_UN", [I32, I64]>;
defm : CompareBranchPats<[SETGE], "BGE", [I32, I64]>;
defm : CompareBranchPats<[SETUGE], "BGE_UN", [I32, I64]>;
// Floating point. The "don't care" predicates, which LLVM uses when no
// operand can be a NaN, take the ordered forms (une for ne), as the compares
// as values do (MSILInstrFloat.td). SETO, SETUO, SETONE and SETUEQ are
// Expand: the legalizer builds them from other compares, and brtrue tests
// the result.
defm : CompareBranchPats<[SETOEQ, SETEQ], "BEQ", [F32, F64]>;
defm : CompareBranchPats<[SETUNE, SETNE], "BNE_UN", [F32, F64]>;
defm : CompareBranchPats<[SETOLT, SETLT], "BLT", [F32, F64]>;
defm : CompareBranchPats<[SETULT], "BLT_UN", [F32, F64]>;
defm : CompareBranchPats<[SETOLE, SETLE], "BLE", [F32, F64]>;
defm : CompareBranchPats<[SETULE], "BLE_UN", [F32, F64]>;
defm : CompareBranchPats<[SETOGT, SETGT], "BGT", [F32, F64]>;
defm : CompareBranchPats<[SETUGT], "BGT_UN", [F32, F64]>;
defm : CompareBranchPats<[SETOGE, SETGE], "BGE", [F32, F64]>;
defm : CompareBranchPats<[SETUGE], "BGE_UN", [F32, F64]>;

// CIL 'switch' on an int32 index. The variable operands are the jump-table
// entries and then the first entry again, which MSILInstPrinter prints as
// 'switch (L0, L1, ...)' followed by 'br L0'. That last target is a dummy
// that is already a successor, not the switch's default: it never executes,
// because the range check before the table, which is never removed (decision
// D35), sends every out-of-range value to the default, and when the default
// is unreachable an out-of-range index is undefined behaviour. There is no
// I64 form: the jump-table index is always i32 (decision D35). Duplicating a
// switch is almost never a good idea (as for WebAssembly's br_table).
let isBranch = 1, isTerminator = 1, isBarrier = 1, hasCtrlDep = 1,
    isNotDuplicable = 1, hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
defm BR_TABLE_I32 : I<(outs), (ins I32:$index, variable_ops),
                      [(MSILbr_table I32:$index)],
                      "switch", 0x45>;

//===----------------------------------------------------------------------===//
// Selects.
//===----------------------------------------------------------------------===//

// CIL has no select instruction (decision D34). SELECT_<RC> yields $t when
// $cond is non-zero and $f otherwise; MSILTargetLowering::emitSelect, its
// custom inserter, turns it into a branch triangle that ends in a PHI:
//   BR_IF %sink, $cond   (taken edge: $t)
//   <empty block>        (falls through to %sink: $f)
//   %sink: $dst = PHI $t, $f
let usesCustomInserter = 1, isPseudo = 1, hasSideEffects = 0, mayLoad = 0,
    mayStore = 0 in
foreach rc = [I32, I64, F32, F64] in
defm SELECT_#rc : I<(outs rc:$dst), (ins I32:$cond, rc:$t, rc:$f),
                    [(set rc:$dst, (select I32:$cond, rc:$t, rc:$f))]>;

//===----------------------------------------------------------------------===//
// Returns and traps.
//===----------------------------------------------------------------------===//

let hasCtrlDep = 1, isBarrier = 1, isTerminator = 1 in {
// The returned value, if any, is the variable operand.
let isReturn = 1 in
defm RETURN : I<(outs), (ins variable_ops), [(MSILreturn)], "ret", 0x2A>;

// 'ldnull; throw': CIL forbids falling off the end of a method, and the
// TargetMachine sets TrapUnreachable, so every unreachable ends here. The
// legalizer also turns llvm.debugtrap and llvm.ubsantrap into a trap (their
// default Expand action): Phase 1 has no debugger break.
let isTrap = 1, StackPeakExtra = 1 in
defm TRAP : I<(outs), (ins), [(trap)], "ldnull\n  throw">;
} // hasCtrlDep = 1, isBarrier = 1, isTerminator = 1
