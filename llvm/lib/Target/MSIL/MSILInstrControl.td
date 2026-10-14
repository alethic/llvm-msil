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

let isBranch = 1, isTerminator = 1, hasCtrlDep = 1 in {
// The condition operand is a boolean value which MSIL represents as i32.
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

// CIL 'switch' on an int32 index; the variable operands are the target basic
// blocks and the last one is the default, printed as 'switch (L0, L1, ...)'
// followed by 'br Ldefault' by MSILInstPrinter. There is no I64 form: the
// jump-table index is always i32 (decision D35). Duplicating a switch is
// almost never a good idea (as for WebAssembly's br_table).
let isBranch = 1, isTerminator = 1, isBarrier = 1, hasCtrlDep = 1,
    isNotDuplicable = 1, hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
defm BR_TABLE_I32 : I<(outs), (ins I32:$index, variable_ops), [],
                      "switch", 0x45>;

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

let hasCtrlDep = 1, isBarrier = 1, isTerminator = 1 in {
// The returned value, if any, is the variable operand.
let isReturn = 1 in
defm RETURN : I<(outs), (ins variable_ops), [(MSILreturn)], "ret", 0x2A>;

// 'ldnull; throw': CIL forbids falling off the end of a method, and the
// TargetMachine sets TrapUnreachable, so every unreachable ends here.
let isTrap = 1, StackPeakExtra = 1 in
defm TRAP : I<(outs), (ins), [(trap)], "ldnull\n  throw">;
} // hasCtrlDep = 1, isBarrier = 1, isTerminator = 1
