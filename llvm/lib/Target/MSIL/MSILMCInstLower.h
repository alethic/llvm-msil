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

public:
  MSILMCInstLower(MCContext &Ctx, MSILAsmPrinter &Printer)
      : Ctx(Ctx), Printer(Printer) {}
  void lower(const MachineInstr *MI, MCInst &OutMI) const;
};
} // end namespace llvm

#endif
