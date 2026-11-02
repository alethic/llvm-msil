//===- ExpandVariadics.h - expand variadic functions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_IPO_EXPANDVARIADICS_H
#define LLVM_TRANSFORMS_IPO_EXPANDVARIADICS_H

#include "llvm/IR/CallingConv.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;
class ModulePass;
class Triple;

enum class ExpandVariadicsMode {
  Unspecified, // Use the implementation defaults
  Disable,     // Disable the pass entirely
  Optimize,    // Optimise without changing ABI
  Lowering,    // Change variadic calling convention
};

class ExpandVariadicsPass : public RequiredPassInfoMixin<ExpandVariadicsPass> {
  const ExpandVariadicsMode Mode;

public:
  // Operates under passed mode unless overridden on commandline
  LLVM_ABI ExpandVariadicsPass(ExpandVariadicsMode Mode);

  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

LLVM_ABI ModulePass *createExpandVariadicsPass(ExpandVariadicsMode);

/// Whether ExpandVariadics gives the MSIL triple \p T an MSIL VariadicABIInfo,
/// so MSIL's IR preparation can report the variadic definitions it would leave
/// alone.
LLVM_ABI bool hasMSILVariadicABI(const Triple &T);

/// Whether ExpandVariadics expands variadic functions and calls of the calling
/// convention \p CC (C and SPIR_FUNC).
LLVM_ABI bool isExpandedVariadicCallingConv(CallingConv::ID CC);

/// The function attribute of a variadic forwarder that MSIL's IR preparation
/// leaves under the name of a whole-program alias of a variadic function: the
/// lowering passes the forwarder's va_list on to the aliasee.
inline constexpr char MSILVariadicForwarderAttr[] = "msil-variadic-forwarder";

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_EXPANDVARIADICS_H
