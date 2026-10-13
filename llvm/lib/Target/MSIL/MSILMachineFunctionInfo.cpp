//===-- MSILMachineFunctionInfo.cpp - MSIL Machine Function Info ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyMachineFunctionInfo.cpp
// at 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements MSIL-specific per-machine-function information.
///
//===----------------------------------------------------------------------===//

#include "MSILMachineFunctionInfo.h"
#include "MSILISelLowering.h"
#include "MSILSubtarget.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

MSILFunctionInfo::~MSILFunctionInfo() = default; // anchor.

MachineFunctionInfo *
MSILFunctionInfo::clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
                        const DenseMap<MachineBasicBlock *, MachineBasicBlock *>
                            &Src2DstMBB) const {
  return DestMF.cloneInfo<MSILFunctionInfo>(*this);
}

void MSILFunctionInfo::initWARegs(MachineRegisterInfo &MRI) {
  assert(WARegs.empty());
  unsigned Reg = MSIL::UnusedReg;
  WARegs.resize(MRI.getNumVirtRegs(), Reg);
}

void llvm::MSIL::computeLegalValueVTs(const MSILTargetLowering &TLI,
                                      LLVMContext &Ctx, const DataLayout &DL,
                                      Type *Ty,
                                      SmallVectorImpl<MVT> &ValueVTs) {
  SmallVector<EVT, 4> VTs;
  ComputeValueVTs(TLI, DL, Ty, VTs);

  for (EVT VT : VTs) {
    unsigned NumRegs = TLI.getNumRegisters(Ctx, VT);
    MVT RegisterVT = TLI.getRegisterType(Ctx, VT);
    for (unsigned I = 0; I != NumRegs; ++I)
      ValueVTs.push_back(RegisterVT);
  }
}

void llvm::MSIL::computeLegalValueVTs(const Function &F,
                                      const TargetMachine &TM, Type *Ty,
                                      SmallVectorImpl<MVT> &ValueVTs) {
  const DataLayout &DL(F.getDataLayout());
  const MSILTargetLowering &TLI =
      *TM.getSubtarget<MSILSubtarget>(F).getTargetLowering();
  computeLegalValueVTs(TLI, F.getContext(), DL, Ty, ValueVTs);
}

void llvm::MSIL::computeSignatureVTs(const FunctionType *Ty,
                                     const Function *TargetFunc,
                                     const Function &ContextFunc,
                                     const TargetMachine &TM,
                                     SmallVectorImpl<MVT> &Params,
                                     SmallVectorImpl<MVT> &Results) {
  computeLegalValueVTs(ContextFunc, TM, Ty->getReturnType(), Results);

  const DataLayout &DL = ContextFunc.getDataLayout();
  MVT PtrVT = MVT::getIntegerVT(DL.getPointerSizeInBits());
  if (Results.size() > 1) {
    // A CIL method returns at most one value, so SelectionDAGBuilder demotes
    // anything larger to a hidden sret pointer parameter (see
    // MSILTargetLowering::CanLowerReturn). Do the same here.
    Results.clear();
    Params.push_back(PtrVT);
  }

  // Variadic functions are not supported yet, so a variadic signature has no
  // extra parameter for the variable arguments.
  for (auto *Param : Ty->params())
    computeLegalValueVTs(ContextFunc, TM, Param, Params);
}

static MVT parseMVT(StringRef Type) {
  return StringSwitch<MVT>(Type)
      .Case("i32", MVT::i32)
      .Case("i64", MVT::i64)
      .Case("f32", MVT::f32)
      .Case("f64", MVT::f64)
      .Default(MVT::INVALID_SIMPLE_VALUE_TYPE);
}

bool yaml::MSILFunctionInfo::isValueTypeName(StringRef Name) {
  return parseMVT(Name) != MVT::INVALID_SIMPLE_VALUE_TYPE;
}

void yaml::MSILFunctionInfo::checkValueTypes(yaml::IO &YamlIO) const {
  // An earlier error (a malformed list, say) has been reported already.
  if (YamlIO.error())
    return;
  auto Check = [&](ArrayRef<FlowStringValue> List, StringRef Key) {
    for (const FlowStringValue &VT : List) {
      if (!isValueTypeName(VT.Value)) {
        YamlIO.setError(Twine("unknown MSIL value type '") + VT.Value +
                        "' in machineFunctionInfo " + Key);
        return false;
      }
    }
    return true;
  };
  if (Check(Params, "params"))
    Check(Results, "results");
}

yaml::MSILFunctionInfo::MSILFunctionInfo(const llvm::MachineFunction &MF,
                                         const llvm::MSILFunctionInfo &MFI)
    : MaxStack(MFI.getMaxStack()) {
  for (auto VT : MFI.getParams())
    Params.push_back(EVT(VT).getEVTString());
  for (auto VT : MFI.getResults())
    Results.push_back(EVT(VT).getEVTString());
}

void yaml::MSILFunctionInfo::mappingImpl(yaml::IO &YamlIO) {
  MappingTraits<MSILFunctionInfo>::mapping(YamlIO, *this);
}

void MSILFunctionInfo::initializeBaseYamlFields(
    MachineFunction &MF, const yaml::MSILFunctionInfo &YamlMFI) {
  MaxStack = YamlMFI.MaxStack;
  // MappingTraits<yaml::MSILFunctionInfo> rejected unknown value types.
  for (auto VT : YamlMFI.Params) {
    MVT Param = parseMVT(VT.Value);
    assert(Param != MVT::INVALID_SIMPLE_VALUE_TYPE && "unknown param type");
    addParam(Param);
  }
  for (auto VT : YamlMFI.Results) {
    MVT Result = parseMVT(VT.Value);
    assert(Result != MVT::INVALID_SIMPLE_VALUE_TYPE && "unknown result type");
    addResult(Result);
  }
}
