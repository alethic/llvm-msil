//===-- MSILTargetTransformInfo.cpp - MSIL-specific TTI -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the MSIL-specific TargetTransformInfo implementation.
///
//===----------------------------------------------------------------------===//

#include "MSILTargetTransformInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

bool MSILTTIImpl::shouldBuildLookupTables() const {
  // BasicTTIImplBase says yes whenever BR_JT or BRIND is legal or custom.
  // SimplifyCFG would then turn a switch whose cases select constants into a
  // private constant array plus an address computation and a load. That needs
  // memory and global data, and it is larger IL than a CIL switch, which the
  // JIT compiles to a jump table of its own.
  return false;
}

bool MSILTTIImpl::shouldBuildRelLookupTables() const {
  // Relative lookup tables hold 32-bit differences of global addresses
  // (llvm.load.relative). Global addresses exist only at run time in CIL, in
  // a data segment allocated by the module initializer.
  return false;
}

void MSILTTIImpl::getUnrollingPreferences(
    Loop *L, ScalarEvolution &SE, TTI::UnrollingPreferences &UP,
    OptimizationRemarkEmitter *ORE) const {
  BaseT::getUnrollingPreferences(L, SE, UP, ORE);
  // The JIT optimizes small methods better than large ones, and it does its
  // own loop optimizations: no partial or runtime unrolling. Full unrolling
  // of loops with a small constant trip count stays enabled.
  UP.Partial = false;
  UP.Runtime = false;
}

/// Returns true if \p Ty is half or bfloat, or a vector, array or struct that
/// contains one.
static bool containsHalfOrBFloat(Type *Ty) {
  if (auto *AT = dyn_cast<ArrayType>(Ty))
    return containsHalfOrBFloat(AT->getElementType());
  if (auto *STy = dyn_cast<StructType>(Ty))
    return any_of(STy->elements(), containsHalfOrBFloat);
  return Ty->getScalarType()->is16bitFPTy();
}

bool MSILTTIImpl::areTypesABICompatible(const Function *Caller,
                                        const Function *Callee,
                                        ArrayRef<Type *> Types) const {
  // Half and bfloat values passed by value need compiler-rt helpers until
  // phase 3 (D14). ArgumentPromotion would create such parameters from the
  // pointer parameters of functions that LTO internalized.
  if (any_of(Types, containsHalfOrBFloat))
    return false;
  return BaseT::areTypesABICompatible(Caller, Callee, Types);
}

unsigned MSILTTIImpl::getNumberOfRegisters(unsigned ClassID) const {
  // No vector types in CIL yet. Zero vector registers turn off LoopVectorize,
  // SLPVectorizer and VectorCombine. Scalars live in an unbounded set of CIL
  // locals; keep the generic answer for them.
  bool Vector = ClassID == getRegisterClassForType(/*Vector=*/true);
  return Vector ? 0 : BaseT::getNumberOfRegisters(ClassID);
}

TypeSize MSILTTIImpl::getRegisterBitWidth(TTI::RegisterKind K) const {
  switch (K) {
  case TTI::RGK_Scalar:
    // The widest scalar register is a native int.
    return TypeSize::getFixed(getDataLayout().getPointerSizeInBits());
  case TTI::RGK_FixedWidthVector:
    return TypeSize::getFixed(0);
  case TTI::RGK_ScalableVector:
    return TypeSize::getScalable(0);
  }
  llvm_unreachable("Unsupported register kind");
}
