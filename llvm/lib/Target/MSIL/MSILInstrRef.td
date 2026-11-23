//===- MSILInstrRef.td - MSIL .NET reference codegen -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The records of .NET references (ImplementationPlan.md 5.21.7 RB2-RB4): IR
/// target("msil.ref"), the value type MVT::externref, the register class OBJ
/// and the CIL type 'object'. A reference lives in IL locals, arguments,
/// results and on the evaluation stack only, so there is no memory form (no
/// ldind.ref or stind.ref): a load or a store of one is never selected.
/// MSILPromoteReferences (slot P15) promotes clang's -O0 slots of references
/// to values before instruction selection, and MSILTargetLowering reports any
/// load or store that is left (T-RB6).
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MSIL-specific DAG Nodes.
//===----------------------------------------------------------------------===//

// The comparison of two references (true when both are the same object or
// both are null, as IL 'ceq' compares them) and the test for null, each an
// int32 0 or 1. MSILTargetLowering::LowerCall makes them of each direct call
// of __msil_ref.eq: REF_IS_NULL when one operand is the null reference, the
// constant 1 when both are, and REF_EQ otherwise. Its
// computeKnownBitsForTargetNode reports every bit above bit 0 as zero, so the
// boolean patterns (bool_node) take them.
def SDT_MSILRefEq : SDTypeProfile<1, 2, [SDTCisVT<0, i32>,
                                         SDTCisVT<1, externref>,
                                         SDTCisVT<2, externref>]>;
def SDT_MSILRefIsNull : SDTypeProfile<1, 1, [SDTCisVT<0, i32>,
                                             SDTCisVT<1, externref>]>;
def MSILref_eq : SDNode<"MSILISD::REF_EQ", SDT_MSILRefEq, [SDNPCommutative]>;
def MSILref_is_null : SDNode<"MSILISD::REF_IS_NULL", SDT_MSILRefIsNull>;

//===----------------------------------------------------------------------===//
// Arguments, copies, locals and the null reference.
//===----------------------------------------------------------------------===//

// ARGUMENT_OBJ; COPY_OBJ, TEE_OBJ, LOCAL_GET_OBJ (ldloc), LOCAL_SET_OBJ
// (stloc), LOCAL_TEE_OBJ (dup; stloc), ARG_GET_OBJ (ldarg), ARG_SET_OBJ
// (starg), ARG_TEE_OBJ (dup; starg) and DROP_OBJ (pop), with the operand
// lists of the other classes (plan 5.4).
defm "" : ARGUMENT<OBJ, externref>;
defm "" : LOCAL<OBJ>;

// 'ldnull'. The null reference is IR zeroinitializer, which
// SelectionDAGBuilder::getValueImpl turns into llvm.wasm.ref.null.extern for
// every MVT::externref (plan 5.21.6 RI2); it is also the value of an OBJ
// IMPLICIT_DEF (MSILExplicitLocals, MSILRegStackify). It has no immediate, so
// MSIL::isScalarConst, which compares the immediates of constants, leaves it
// out.
let isAsCheapAsAMove = 1, isReMaterializable = 1, hasSideEffects = 0,
    mayLoad = 0, mayStore = 0 in
defm CONST_NULL : I<(outs OBJ:$res), (ins),
                    [(set OBJ:$res, (int_wasm_ref_null_extern))],
                    "ldnull", 0x14>;

//===----------------------------------------------------------------------===//
// Comparisons, branches and selects.
//===----------------------------------------------------------------------===//

// 'ceq' on two objects: their identity, an int32 0 or 1.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isCommutable = 1 in
defm CEQ_OBJ : I<(outs I32:$dst), (ins OBJ:$lhs, OBJ:$rhs),
                 [(set I32:$dst, (MSILref_eq OBJ:$lhs, OBJ:$rhs))],
                 "ceq", 0xFE01>;
// 'ldnull; ceq' for the null test as a value.
def : Pat<(i32 (MSILref_is_null OBJ:$ref)), (CEQ_OBJ OBJ:$ref, (CONST_NULL))>;

// Compare and branch, as for the other classes (MSILInstrControl.td): 'beq L'
// branches when the two objects are the same or both null, 'bne.un L' when
// they differ; 'brtrue L' branches on an object and 'brfalse L' on null.
let isBranch = 1, isTerminator = 1, hasCtrlDep = 1, hasSideEffects = 0,
    mayLoad = 0, mayStore = 0 in {
defm BEQ_OBJ : I<(outs), (ins bb_op:$dst, OBJ:$lhs, OBJ:$rhs), [],
                 "beq $dst", 0x3B>;
defm BNE_UN_OBJ : I<(outs), (ins bb_op:$dst, OBJ:$lhs, OBJ:$rhs), [],
                    "bne.un $dst", 0x40>;
defm BR_IF_OBJ : I<(outs), (ins bb_op:$dst, OBJ:$ref), [],
                   "brtrue $dst", 0x3A>;
defm BR_UNLESS_OBJ : I<(outs), (ins bb_op:$dst, OBJ:$ref), [],
                       "brfalse $dst", 0x39>;
} // isBranch = 1, isTerminator = 1, hasCtrlDep = 1, hasSideEffects = 0, ...

// brcond of a comparison or of a null test, and of their negations, which the
// DAG spells '(xor X, 1)' and, after DAGCombiner's rebuildSetCC on a boolean,
// '(setne X, 1)' or '(seteq X, 0)'; '(setne X, 0)' and '(seteq X, 1)' are X.
// A shape these patterns miss still compiles correctly, as 'ceq' and
// 'brtrue'.
def : Pat<(brcond (i32 (MSILref_eq OBJ:$lhs, OBJ:$rhs)), bb:$dst),
          (BEQ_OBJ bb_op:$dst, OBJ:$lhs, OBJ:$rhs)>;
def : Pat<(brcond (i32 (setne (i32 (MSILref_eq OBJ:$lhs, OBJ:$rhs)), 0)),
                  bb:$dst),
          (BEQ_OBJ bb_op:$dst, OBJ:$lhs, OBJ:$rhs)>;
def : Pat<(brcond (i32 (seteq (i32 (MSILref_eq OBJ:$lhs, OBJ:$rhs)), 1)),
                  bb:$dst),
          (BEQ_OBJ bb_op:$dst, OBJ:$lhs, OBJ:$rhs)>;
def : Pat<(brcond (i32 (xor (i32 (MSILref_eq OBJ:$lhs, OBJ:$rhs)), 1)),
                  bb:$dst),
          (BNE_UN_OBJ bb_op:$dst, OBJ:$lhs, OBJ:$rhs)>;
def : Pat<(brcond (i32 (seteq (i32 (MSILref_eq OBJ:$lhs, OBJ:$rhs)), 0)),
                  bb:$dst),
          (BNE_UN_OBJ bb_op:$dst, OBJ:$lhs, OBJ:$rhs)>;
def : Pat<(brcond (i32 (setne (i32 (MSILref_eq OBJ:$lhs, OBJ:$rhs)), 1)),
                  bb:$dst),
          (BNE_UN_OBJ bb_op:$dst, OBJ:$lhs, OBJ:$rhs)>;
def : Pat<(brcond (i32 (MSILref_is_null OBJ:$ref)), bb:$dst),
          (BR_UNLESS_OBJ bb_op:$dst, OBJ:$ref)>;
def : Pat<(brcond (i32 (setne (i32 (MSILref_is_null OBJ:$ref)), 0)), bb:$dst),
          (BR_UNLESS_OBJ bb_op:$dst, OBJ:$ref)>;
def : Pat<(brcond (i32 (seteq (i32 (MSILref_is_null OBJ:$ref)), 1)), bb:$dst),
          (BR_UNLESS_OBJ bb_op:$dst, OBJ:$ref)>;
def : Pat<(brcond (i32 (xor (i32 (MSILref_is_null OBJ:$ref)), 1)), bb:$dst),
          (BR_IF_OBJ bb_op:$dst, OBJ:$ref)>;
def : Pat<(brcond (i32 (seteq (i32 (MSILref_is_null OBJ:$ref)), 0)), bb:$dst),
          (BR_IF_OBJ bb_op:$dst, OBJ:$ref)>;
def : Pat<(brcond (i32 (setne (i32 (MSILref_is_null OBJ:$ref)), 1)), bb:$dst),
          (BR_IF_OBJ bb_op:$dst, OBJ:$ref)>;

// CIL has no select instruction (decision D34): SELECT_OBJ yields $t when
// $cond is non-zero and $f otherwise, and MSILTargetLowering::emitSelect, its
// custom inserter, turns it into a branch triangle that ends in a PHI, as for
// the other classes.
let usesCustomInserter = 1, isPseudo = 1, hasSideEffects = 0, mayLoad = 0,
    mayStore = 0 in
defm SELECT_OBJ : I<(outs OBJ:$dst), (ins I32:$cond, OBJ:$t, OBJ:$f),
                    [(set OBJ:$dst, (select I32:$cond, OBJ:$t, OBJ:$f))]>;
