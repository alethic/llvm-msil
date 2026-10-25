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
/// This file declares the class that lowers MSIL MachineInstrs to MCInsts,
/// one CIL_ record (MSILInstrCIL.td) per CIL instruction, and the helpers that
/// build CIL_ MCInsts in their shortest forms and choose branch forms.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILMCINSTLOWER_H
#define LLVM_LIB_TARGET_MSIL_MSILMCINSTLOWER_H

#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <utility>

namespace llvm {
class Function;
class MCContext;
class MCInstrInfo;
class MCSymbol;
class MSILAsmPrinter;
class MSILMemberRefExpr;
class MachineFunction;
class MachineInstr;
class MachineOperand;

/// This class is used to lower an MachineInstr into MCInsts.
///
/// Each MachineInstr becomes one or more CIL_ MCInsts (plan M18.a (2)): tees
/// become 'dup' and the store, constants and indices take their shortest
/// forms, memory flags become the 'unaligned.' and 'volatile.' prefixes,
/// BR_TABLE_I32 becomes 'switch' and 'br', and so on; records that are one
/// CIL instruction map one to one. Token operands are MSILMemberRefExprs whose
/// kind is the operand class of plan 5.8.3. Branches take the forms that
/// computeBranchForms chose for the function, or their long forms.
class LLVM_LIBRARY_VISIBILITY MSILMCInstLower {
  MCContext &Ctx;
  MSILAsmPrinter &Printer;
  /// The branches of the function being emitted that take their short form:
  /// (MachineInstr, index of the branch among the MCInsts it lowers to).
  DenseSet<std::pair<const MachineInstr *, unsigned>> ShortBranches;

  /// Lowers \p MI into \p Out with every branch in its long form. \p Emit is
  /// false while computeBranchForms lays the function out: no diagnostics,
  /// no notes to the printer, and tokens are placeholder immediates (only
  /// sizes matter).
  void lowerImpl(const MachineInstr &MI, SmallVectorImpl<MCInst> &Out,
                 bool Emit) const;

  /// The token of the callee operand \p MO of the direct call \p MI.
  MCOperand lowerCallee(const MachineInstr &MI, const MachineOperand &MO,
                        bool Emit) const;

  /// The token of the address field that FUNC_ADDR \p MI reads (\p MO is its
  /// function operand).
  MCOperand lowerFunctionAddress(const MachineInstr &MI,
                                 const MachineOperand &MO, bool Emit) const;

  /// The token of the address field of the global variable of GLOBAL_ADDR
  /// \p MI (\p MO is its data operand, without its offset).
  MCOperand lowerDataAddress(const MachineInstr &MI, const MachineOperand &MO,
                             bool Emit) const;

  /// The operand of 'calli' for the indirect call \p MI: the call-site
  /// signature (getCallSiteSignature without the callee address that calli
  /// pops last), managed, interned by the printer.
  MCOperand lowerCallIndirectSignature(const MachineInstr &MI, bool Emit) const;

public:
  MSILMCInstLower(MCContext &Ctx, MSILAsmPrinter &Printer)
      : Ctx(Ctx), Printer(Printer) {}

  /// Whether \p MI emits CIL code: every target-specific record except the
  /// ARGUMENT_* pseudos. This is the one test of which MachineInstrs make up
  /// the emitted code: computeBranchForms lays out exactly these, and lower(),
  /// MSILAsmPrinter::emitInstruction's only path to the streamer, emits code
  /// for exactly these.
  static bool isLowered(const MachineInstr &MI);

  /// Lowers \p MI into one CIL_ MCInst per CIL instruction, appended to
  /// \p Out, with the branch forms of computeBranchForms. Appends nothing for
  /// an ARGUMENT_* pseudo; any other instruction that isLowered rejects (a
  /// target-independent one that AsmPrinter does not handle itself, such as
  /// FENTRY_CALL) is an internal error.
  void lower(const MachineInstr &MI, SmallVectorImpl<MCInst> &Out) const;

  /// Chooses the form of every branch of \p MF before it is emitted (msil-ld
  /// never relaxes a branch, plan 5.8): lowers the function once into a
  /// scratch list (no diagnostics, no notes), records the index of each basic
  /// block's label, and runs MSIL::relaxBranches. lower() then uses the
  /// chosen forms, the 'br' after a 'switch' included.
  void computeBranchForms(const MachineFunction &MF);

  /// The signature that the call \p MI actually uses: its explicit register
  /// defs are the values it pushes and its explicit register uses the values
  /// it pops, in CIL types (after MSILExplicitLocals every register operand
  /// is a value on the evaluation stack). For a direct call this is the
  /// signature of the method it calls; for an indirect call the last
  /// parameter is the callee address.
  static MSIL::Signature getCallSiteSignature(const MachineInstr &MI);

  /// The token of the function \p F, whose IL name is \p ILName, as a method
  /// called (or loaded with 'ldftn') with signature \p Sig: a Method if this
  /// module defines \p F or \p F carries "msil-import-module" or
  /// "msil-import-name" (its import stub, plan 5.8.3 R7), an ExternMethod
  /// otherwise. \p Sig must outlive the expression.
  static const MSILMemberRefExpr *createMethodRef(const Function &F,
                                                  StringRef ILName,
                                                  const MSIL::Signature &Sig,
                                                  MCContext &Ctx);
};

namespace MSIL {

/// The CIL_ record of the CIL opcode \p Op.
unsigned getMCOpcodeForCIL(cli::Opcode Op);

/// 'ldc.i4' of \p Value in its shortest form: ldc.i4.m1, ldc.i4.0-8,
/// ldc.i4.s or ldc.i4 (cli::ILBuilder::emitLdcI4's rule).
MCInst buildLdcI4(int32_t Value);

/// The shortest form of the argument or local instruction \p LongForm
/// (ldarg, ldarga, starg, ldloc, ldloca or stloc) with index \p Index: the
/// '.0'-'.3' opcodes (ldarg, ldloc and stloc only), the '.s' form below 256,
/// else the long form (cli::ILBuilder's rule).
MCInst buildVarInst(cli::Opcode LongForm, unsigned Index);

/// Chooses branch forms (metadata-writer 5.2, cli::ILBuilder's rule): makes
/// every branch in \p Insts whose target is one of \p Labels short, then
/// widens each branch whose displacement from the next instruction leaves
/// [-128, 127], until nothing changes. \p Labels gives each label symbol the
/// index of the instruction it precedes (Insts.size() for the end). A branch
/// to another symbol becomes long; one whose target is an immediate keeps its
/// form. Switch entries are always 32-bit. Sizes come from
/// MSIL::getCILInstSize, so the result does not depend on the input forms.
void relaxBranches(MutableArrayRef<MCInst> Insts,
                   ArrayRef<std::pair<const MCSymbol *, unsigned>> Labels,
                   const MCInstrInfo &MCII);

} // end namespace MSIL

} // end namespace llvm

#endif
