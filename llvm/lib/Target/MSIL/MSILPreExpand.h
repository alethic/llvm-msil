//===-- MSILPreExpand.h - IR rewrites before ExpandIRInsts ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the module rewrites that MSILPrepareIR applies before
/// any pass runs (its doInitialization), for IR that ExpandIRInsts must see
/// in a form it expands.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILPREEXPAND_H
#define LLVM_LIB_TARGET_MSIL_MSILPREEXPAND_H

namespace llvm {

class Module;

namespace MSIL {

/// Replaces each constrained conversion between floating point and an integer
/// wider than 64 bits (MaxSelectedFPConvertBits, MSILUtilities.h;
/// llvm.experimental.constrained.sitofp and uitofp with such an operand,
/// fptosi and fptoui with such a result, scalar or vector) by the plain
/// instruction, so that ExpandIRInsts expands it inline as it expands the
/// plain conversions. CIL has one rounding mode and no floating-point
/// exceptions, so the rounding and exception operands carry nothing. Returns
/// whether the module changed.
bool relaxStrictInt128Conversions(Module &M);

} // namespace MSIL
} // namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MSILPREEXPAND_H
