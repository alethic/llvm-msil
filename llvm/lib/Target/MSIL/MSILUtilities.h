//===-- MSILUtilities.h - MSIL utility functions ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyUtilities.h at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the MSIL-specific utility functions:
/// the IL name of a global value and the helpers the MachineInstr passes share.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILUTILITIES_H
#define LLVM_LIB_TARGET_MSIL_MSILUTILITIES_H

#include "llvm/ADT/StringRef.h"

namespace llvm {

class GlobalValue;
class MachineInstr;
class MachineOperand;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class MSILFunctionInfo;
class TargetMachine;

namespace MSIL {

/// Returns the name that identifies \p GV in ILAsm (unquoted; print it with
/// MSIL::printQuotedName). This is the IR name without the '\1' escape, never
/// the Mangler's name: native prefixes such as '_' would break P/Invoke entry
/// points and the names other .NET code sees. Only unnamed values get the
/// TargetMachine's symbol name ('__unnamed_N').
StringRef getILName(const GlobalValue &GV, const TargetMachine &TM);

/// Test whether MI is a child of some other node in an expression tree.
bool isChild(const MachineInstr &MI, const MSILFunctionInfo &MFI);

/// Returns true if \p MI may throw. Phase 1 has no exception handling, so
/// every call may throw and nothing else does.
bool mayThrow(const MachineInstr &MI);

/// Returns the operand number of a callee, assuming the argument is a call
/// instruction.
const MachineOperand &getCalleeOp(const MachineInstr &MI);

/// Returns the appropriate copy opcode for the given register class.
unsigned getCopyOpcodeForRegClass(const TargetRegisterClass *RC);

} // end namespace MSIL

} // end namespace llvm

#endif
