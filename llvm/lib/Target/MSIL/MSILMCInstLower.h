//===-- MSILMCInstLower.h - Lower MachineInstr to MCInst --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyMCInstLower.h at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the class to lower MSIL MachineInstrs to their
/// corresponding MCInst records.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILMCINSTLOWER_H
#define LLVM_LIB_TARGET_MSIL_MSILMCINSTLOWER_H

#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class MCContext;
class MachineInstr;
class MachineOperand;
class MSILAsmPrinter;

/// This class is used to lower an MachineInstr into an MCInst.
class LLVM_LIBRARY_VISIBILITY MSILMCInstLower {
  MCContext &Ctx;
  MSILAsmPrinter &Printer;

  /// Lowers a function operand (the callee of a 'call', the target of an
  /// 'ldftn') to the method reference, with the signature from the printer's
  /// table, and notes an undefined function for the module trailer.
  MCOperand lowerFunctionOperand(const MachineOperand &MO) const;

  /// Lowers the external symbol callee of the direct call \p MI (a library
  /// call) to a method reference whose signature is the call's own, and notes
  /// the symbol for its P/Invoke declaration in the module trailer.
  MCOperand lowerExternalSymbolOperand(const MachineInstr &MI,
                                       const MachineOperand &MO) const;

public:
  MSILMCInstLower(MCContext &Ctx, MSILAsmPrinter &Printer)
      : Ctx(Ctx), Printer(Printer) {}
  void lower(const MachineInstr *MI, MCInst &OutMI) const;

  /// The signature that the call \p MI actually uses: its explicit register
  /// defs are the values it pushes and its explicit register uses the values
  /// it pops, in CIL types (after MSILExplicitLocals every register operand
  /// is a value on the evaluation stack). For a direct call this is the
  /// signature of the method it calls.
  static MSIL::Signature getCallSiteSignature(const MachineInstr &MI);
};
} // end namespace llvm

#endif
