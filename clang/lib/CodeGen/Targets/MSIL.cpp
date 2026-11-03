//===- MSIL.cpp -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The C ABI of the portable MSIL triples msil64-unknown-dotnet and
// msil32-unknown-dotnet. The host MSIL triples ("<native triple>-msil") keep
// their native ABIInfo, which createTargetCodeGenInfo chooses by architecture.
//
// The MSIL backend lowers calls from the IR attributes alone (byval, sret,
// signext and zeroext), so the portable ABI is DefaultABIInfo's with these
// changes:
//  * empty records are ignored as arguments and as results, as on
//    WebAssembly and x86-64,
//  * va_arg is lowered here, not to an IR va_arg instruction: va_list is a
//    char * into a buffer of 8-byte slots, a value whose alignment exceeds 8
//    starts at the next multiple of its alignment, and an argument passed
//    indirectly (an aggregate, or a _BitInt wider than 128 bits on msil64 or
//    64 bits on msil32, which DefaultABIInfo passes byval and returns through
//    sret) occupies one slot holding its address. The
//    MSILPortable class of ExpandVariadics (llvm/lib/Transforms/IPO/
//    ExpandVariadics.cpp) lays out the calls with the same rule.
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// MSIL portable ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class MSILPortableABIInfo final : public DefaultABIInfo {
public:
  explicit MSILPortableABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  // DefaultABIInfo's classifyArgumentType and classifyReturnType are not
  // virtual: these hide them, and computeInfo calls them.
  ABIArgInfo classifyArgumentType(QualType Ty) const;
  ABIArgInfo classifyReturnType(QualType RetTy) const;

  void computeInfo(CGFunctionInfo &FI) const override;

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

class MSILPortableTargetCodeGenInfo final : public TargetCodeGenInfo {
public:
  explicit MSILPortableTargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<MSILPortableABIInfo>(CGT)) {}
};

} // namespace

ABIArgInfo MSILPortableABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (isAggregateTypeForABI(Ty)) {
    // Records with non-trivial destructors or copy constructors are passed by
    // address, not by value.
    if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace(),
                                     RAA == CGCXXABI::RAA_DirectInMemory);
    // Empty structures and unions take no argument.
    if (isEmptyRecord(getContext(), Ty, /*AllowArrays=*/true))
      return ABIArgInfo::getIgnore();
  }

  // Other aggregates byval, a _BitInt wider than 128 bits (msil64) or 64 bits
  // (msil32) indirectly byval, everything else directly.
  return DefaultABIInfo::classifyArgumentType(Ty);
}

ABIArgInfo MSILPortableABIInfo::classifyReturnType(QualType RetTy) const {
  // Empty structures and unions are not returned. Records with non-trivial
  // destructors or copy constructors never reach this point: the C++ ABI
  // returns them through a hidden pointer (computeInfo).
  if (isAggregateTypeForABI(RetTy) && !getRecordArgABI(RetTy, getCXXABI()) &&
      isEmptyRecord(getContext(), RetTy, /*AllowArrays=*/true))
    return ABIArgInfo::getIgnore();

  // Other aggregates, and a _BitInt wider than 128 bits (msil64) or 64 bits
  // (msil32), through sret, everything else directly.
  return DefaultABIInfo::classifyReturnType(RetTy);
}

void MSILPortableABIInfo::computeInfo(CGFunctionInfo &FI) const {
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  for (auto &Arg : FI.arguments())
    Arg.info = classifyArgumentType(Arg.type);
}

RValue MSILPortableABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                      QualType Ty, AggValueSlot Slot) const {
  const ABIArgInfo AI = classifyArgumentType(Ty);

  // An ignored argument (an empty record) was not passed and has no slot.
  if (AI.isIgnore())
    return Slot.asRValue();

  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*IsIndirect=*/AI.isIndirect(),
                          getContext().getTypeInfoInChars(Ty),
                          /*SlotSizeAndAlign=*/CharUnits::fromQuantity(8),
                          /*AllowHigherAlign=*/true, Slot);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createMSILPortableTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<MSILPortableTargetCodeGenInfo>(CGM.getTypes());
}
