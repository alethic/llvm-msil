//===-- MSILDebugValueManager.h - MSIL DebugValue Manager -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyDebugValueManager.h at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the MSIL-specific
/// manager for DebugValues associated with the specific MachineInstr.
/// This pass currently does not handle DBG_VALUE_LISTs; they are assumed to
/// have been set to undef in NullifyDebugValueLists pass.
/// TODO Handle DBG_VALUE_LIST
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILDEBUGVALUEMANAGER_H
#define LLVM_LIB_TARGET_MSIL_MSILDEBUGVALUEMANAGER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

class MachineInstr;

namespace MSIL {

/// Target indices of the DBG_VALUE operands that replaceWithLocal creates, as
/// WebAssembly's TI_LOCAL and TI_LOCAL_INDIRECT. The index is the local number
/// of MSILExplicitLocals, which numbers the arguments first and then the CIL
/// locals. Phase 1 emits no debug information, so only the DBG_VALUE comments
/// of the assembly output show them.
enum TargetIndex {
  // Followed by a local number.
  TI_LOCAL,
  // Like TI_LOCAL, but indicates an indirect value.
  TI_LOCAL_INDIRECT,
};

} // end namespace MSIL

class MSILDebugValueManager {
  MachineInstr *Def;
  SmallVector<MachineInstr *, 1> DbgValues;
  Register CurrentReg;
  SmallVector<MachineInstr *, 1>
  getSinkableDebugValues(MachineInstr *Insert) const;
  bool isInsertSamePlace(MachineInstr *Insert) const;

public:
  MSILDebugValueManager(MachineInstr *Def);

  // Sink 'Def', and also sink its eligible DBG_VALUEs to the place before
  // 'Insert'. Convert the original DBG_VALUEs into undefs.
  void sink(MachineInstr *Insert);
  // Clone 'Def' (optionally), and also clone its eligible DBG_VALUEs to the
  // place before 'Insert'.
  void cloneSink(MachineInstr *Insert, Register NewReg = Register(),
                 bool CloneDef = true) const;
  // Update the register for Def and DBG_VALUEs.
  void updateReg(Register Reg);
  // Replace the current register in DBG_VALUEs with the given LocalId target
  // index.
  void replaceWithLocal(unsigned LocalId);
  // Remove Def, and set its DBG_VALUEs to undef.
  void removeDef();
};

} // end namespace llvm

#endif
