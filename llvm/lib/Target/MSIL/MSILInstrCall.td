//===- MSILInstrCall.td - MSIL call codegen support --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyInstrCall.td at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL call code-gen constructs. Arguments travel on the CIL evaluation
/// stack, so there is no stack pointer and no outgoing argument area.
///
//===----------------------------------------------------------------------===//

def SDT_MSILCallSeqStart : SDCallSeqStart<[SDTCisVT<0, iPTR>,
                                           SDTCisVT<1, iPTR>]>;
def SDT_MSILCallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, iPTR>, SDTCisVT<1, iPTR>]>;

def MSILcallseq_start : SDNode<"ISD::CALLSEQ_START", SDT_MSILCallSeqStart,
                               [SDNPHasChain, SDNPOutGlue]>;
def MSILcallseq_end : SDNode<"ISD::CALLSEQ_END", SDT_MSILCallSeqEnd,
                             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

// Call sequence markers. MSILTargetLowering::LowerCall emits no CALLSEQ_START
// or CALLSEQ_END (as in WebAssembly, there is no outgoing argument area), so
// these only give MSILInstrInfo its call frame opcodes. They would print
// nothing and are erased by MSILFrameLowering::eliminateCallFramePseudoInstr.
let isCodeGenOnly = 1 in {
defm ADJCALLSTACKDOWN : I<(outs), (ins i32imm:$amt, i32imm:$amt2),
                          [(MSILcallseq_start timm:$amt, timm:$amt2)]>;
defm ADJCALLSTACKUP : I<(outs), (ins i32imm:$amt, i32imm:$amt2),
                        [(MSILcallseq_end timm:$amt, timm:$amt2)]>;
} // isCodeGenOnly = 1

let isCall = 1, hasSideEffects = 1, mayLoad = 0, mayStore = 0 in {

// CALL should take both variadic arguments and produce variadic results, but
// this is not possible to model directly. Instead, we select calls to a
// CALL_PARAMS taking variadic arguments linked with a CALL_RESULTS that handles
// producing the call's variadic results. We recombine the two in a custom
// inserter hook after DAG ISel, so passes over MachineInstrs will only ever
// observe CALL and CALL_INDIRECT with all of the expected variadic uses and
// defs. Neither pseudo is ever printed.
let isPseudo = 1 in
defm CALL_PARAMS : I<(outs), (ins function_op:$callee, variable_ops), []>;

let variadicOpsAreDefs = 1, usesCustomInserter = 1, isPseudo = 1 in
defm CALL_RESULTS : I<(outs), (ins variable_ops), []>;

// The variadic operands are the result (if any), then for CALL the callee
// (its operand index is the number of results), then the arguments; for
// CALL_INDIRECT the callee address is the last argument (calli order).
// CALL_INDIRECT carries no signature operand: MSILMCInstLower builds the
// managed call-site signature from the result and argument registers and
// inserts it after the results, and MSILInstPrinter prints the method
// reference or the call-site signature.
let variadicOpsAreDefs = 1 in {
defm CALL : I<(outs), (ins function_op:$callee, variable_ops), [],
              "call", 0x28>;
defm CALL_INDIRECT : I<(outs), (ins variable_ops), [], "calli", 0x29>;
} // variadicOpsAreDefs = 1

} // isCall = 1, hasSideEffects = 1, mayLoad = 0, mayStore = 0
