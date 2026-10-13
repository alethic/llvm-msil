//===-- MSILDebugValueManager.h - MSIL DebugValue Manager -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// STUB: implemented in M3
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the class that keeps the debug values of a virtual
/// register in sync when MSILRegStackify and MSILExplicitLocals move, clone
/// or delete its definition.
///
/// Until M3 the class is empty; M3 replaces it with a copy of
/// WebAssemblyDebugValueManager.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILDEBUGVALUEMANAGER_H
#define LLVM_LIB_TARGET_MSIL_MSILDEBUGVALUEMANAGER_H

namespace llvm {

/// This class keeps DBG_VALUEs consistent when a definition moves.
class MSILDebugValueManager {};

} // end namespace llvm

#endif
