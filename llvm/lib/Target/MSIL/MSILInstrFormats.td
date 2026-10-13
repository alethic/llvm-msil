//===- MSILInstrFormats.td - MSIL instruction formats ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyInstrFormats.td at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL instruction format definitions.
///
/// Unlike WebAssembly there is one record per instruction, with no
/// register/stack twins: MachineInstrs keep their virtual-register operands,
/// MSILMCInstLower turns them into placeholder operands in the same positions,
/// and the asm strings name only immediate and symbol operands.
///
//===----------------------------------------------------------------------===//

// MSIL instruction format.
class MSILInst<bits<16> cilop, string asmstr> : Instruction {
  // CIL opcode: 0x00XX for one-byte opcodes, 0xFEXX for two-byte opcodes, and
  // 0xFFFF for pseudos and for records that print as several opcodes.
  bits<16> Inst = cilop;

  // Extra evaluation-stack depth needed while the CIL text of this record
  // executes, above max(pops, pushes). Non-zero only for records that print
  // as several opcodes (for example 'dup; stloc' is 1). MSILStackCheck reads
  // it to compute .maxstack.
  bits<2> StackPeakExtra = 0;

  // Division and remainder: has side effects only because it traps on
  // undefined behaviour, so RegStackify may still move it.
  bit TrapsOnlyOnUB = 0;

  let Namespace = "MSIL";
  let Pattern = [];
  let AsmString = asmstr;

  // Keep in sync with MSILII in MCTargetDesc/MSILMCTargetDesc.h.
  let TSFlags{1-0} = StackPeakExtra;
  let TSFlags{2} = TrapsOnlyOnUB;
}

// Normal instructions. Default instantiation of an MSILInst.
class NI<dag oops, dag iops, list<dag> pattern, string asmstr = "",
         bits<16> cilop = 0xFFFF>
    : MSILInst<cilop, asmstr> {
  dag OutOperandList = oops;
  dag InOperandList  = iops;
  let Pattern        = pattern;
  // Keeps the ARGUMENT_* pseudos, which use ARGUMENTS, at the function entry.
  let Defs           = [ARGUMENTS];
}

// Same name as WebAssembly's multiclass so the per-area files read alike, but
// it defines a single record.
multiclass I<dag oops, dag iops, list<dag> pattern, string asmstr = "",
             bits<16> cilop = 0xFFFF> {
  def "" : NI<oops, iops, pattern, asmstr, cilop>;
}
