//===-- MSILTargetTransformInfo.h - MSIL-specific TTI -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyTargetTransformInfo.h at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares a TargetTransformInfoImplBase conforming object specific
/// to the MSIL target machine.
///
/// The IR optimizer queries it long before the code it describes can be
/// generated, so every hook here is final (it is never a stub). It turns off
/// what CIL code cannot use or should not get: vector types, lookup tables of
/// constants (they need loads and globals instead of a CIL switch) and partial
/// or runtime unrolling (large methods slow down the JIT).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_MSIL_MSILTARGETTRANSFORMINFO_H

#include "MSILTargetMachine.h"
#include "llvm/CodeGen/BasicTTIImpl.h"

namespace llvm {

class MSILTTIImpl final : public BasicTTIImplBase<MSILTTIImpl> {
  using BaseT = BasicTTIImplBase<MSILTTIImpl>;
  using TTI = TargetTransformInfo;
  friend BaseT;

  const MSILSubtarget *ST;
  const MSILTargetLowering *TLI;

  const MSILSubtarget *getST() const { return ST; }
  const MSILTargetLowering *getTLI() const { return TLI; }

public:
  MSILTTIImpl(const MSILTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  /// \name Scalar TTI Implementations
  /// @{

  bool shouldBuildLookupTables() const override;
  bool shouldBuildRelLookupTables() const override;

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE) const override;

  /// @}

  /// \name Vector TTI Implementations
  /// @{

  unsigned getNumberOfRegisters(unsigned ClassID) const override;
  TypeSize getRegisterBitWidth(TTI::RegisterKind K) const override;

  /// @}
};

} // end namespace llvm

#endif
