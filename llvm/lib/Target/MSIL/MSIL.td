//===- MSIL.td - Describe the MSIL target machine ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssembly.td at d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This is the target description file for MSIL: .NET Common Intermediate
/// Language (ECMA-335), emitted as ILAsm text.
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces which we are implementing
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

include "MSILRegisterInfo.td"

//===----------------------------------------------------------------------===//
// Instruction Descriptions
//===----------------------------------------------------------------------===//

include "MSILInstrInfo.td"

// Pointers are 64-bit native ints (decision D2): only 64-bit triples exist in
// Phase 1, so the generic pseudos' pointer operands use I64.
defm : RemapAllTargetPseudoPointerOperands<I64>;

def MSILInstrInfo : InstrInfo {
  // Records without a pattern must say whether they have side effects, load
  // or store; TableGen never guesses (a guessed hasSideEffects = 1 would keep
  // RegStackify and rematerialization away from pure records).
  let guessInstructionProperties = 0;
}

//===----------------------------------------------------------------------===//
// MSIL Processors supported.
//===----------------------------------------------------------------------===//

// One processor and no subtarget features in Phase 1 (decision D7): the native
// CPU and feature strings clang attaches are mapped to "generic" and dropped in
// C++ (MSIL::getMSILCPU, MSIL::filterMSILFeatures).
def : ProcessorModel<"generic", NoSchedModel, []>;

//===----------------------------------------------------------------------===//
// Target Declaration
//===----------------------------------------------------------------------===//

def MSILAsmWriter : AsmWriter {
  string AsmWriterClassName  = "InstPrinter";
  int PassSubtarget = 1;
  int Variant = 0;
  bit isMCAsmWriter = 1;
}

// No assembly parser: ilasm assembles the output.
def MSIL : Target {
  let InstructionSet = MSILInstrInfo;
  let AssemblyWriters = [MSILAsmWriter];
}
