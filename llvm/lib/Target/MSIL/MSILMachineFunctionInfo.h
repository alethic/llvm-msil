//===-- MSILMachineFunctionInfo.h - MSIL machine function info --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyMachineFunctionInfo.h at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares MSIL-specific per-machine-function information.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_MSIL_MSILMACHINEFUNCTIONINFO_H

#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MIRYamlMapping.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include <vector>

namespace llvm {
class MSILTargetLowering;

namespace yaml {
struct MSILFunctionInfo;
}

/// This class is derived from MachineFunctionInfo and contains private
/// MSIL-specific information for each MachineFunction.
class MSILFunctionInfo final : public MachineFunctionInfo {
  std::vector<MVT> Params;
  std::vector<MVT> Results;
  /// The CIL locals of the method (the arguments are not included).
  std::vector<MVT> Locals;

  /// A mapping from CodeGen vreg index to MSIL register number.
  std::vector<unsigned> WARegs;

  /// A mapping from CodeGen vreg index to a boolean value indicating whether
  /// the given register is considered to be "stackified", meaning it has been
  /// determined or made to meet the stack requirements:
  ///   - single use (per path)
  ///   - single def (per path)
  ///   - defined and used in LIFO order with other stack registers
  BitVector VRegStackified;

  // A virtual register holding the frame base. This is FP after it has been
  // replaced by a vreg.
  unsigned FrameBaseVreg = -1U;
  // The local holding the frame base. This is FP after MSILExplicitLocals.
  unsigned FrameBaseLocal = -1U;

  // The .maxstack of the method, computed by MSILStackCheck.
  unsigned MaxStack = 0;

public:
  explicit MSILFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {
  }
  ~MSILFunctionInfo() override;

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  void initializeBaseYamlFields(MachineFunction &MF,
                                const yaml::MSILFunctionInfo &YamlMFI);

  void addParam(MVT VT) { Params.push_back(VT); }
  const std::vector<MVT> &getParams() const { return Params; }

  void addResult(MVT VT) { Results.push_back(VT); }
  const std::vector<MVT> &getResults() const { return Results; }

  void clearParamsAndResults() {
    Params.clear();
    Results.clear();
  }

  void setNumLocals(size_t NumLocals) { Locals.resize(NumLocals, MVT::i32); }
  void setLocal(size_t i, MVT VT) { Locals[i] = VT; }
  void addLocal(MVT VT) { Locals.push_back(VT); }
  const std::vector<MVT> &getLocals() const { return Locals; }

  void setFrameBaseVreg(unsigned Reg) { FrameBaseVreg = Reg; }
  unsigned getFrameBaseVreg() const {
    assert(FrameBaseVreg != -1U && "Frame base vreg hasn't been set");
    return FrameBaseVreg;
  }
  void clearFrameBaseVreg() { FrameBaseVreg = -1U; }
  // Return true if the frame base physreg has been replaced by a virtual reg.
  bool isFrameBaseVirtual() const { return FrameBaseVreg != -1U; }
  void setFrameBaseLocal(unsigned Local) { FrameBaseLocal = Local; }
  unsigned getFrameBaseLocal() const {
    assert(FrameBaseLocal != -1U && "Frame base local hasn't been set");
    return FrameBaseLocal;
  }

  void stackifyVReg(MachineRegisterInfo &MRI, Register VReg) {
    assert(MRI.getUniqueVRegDef(VReg));
    auto I = VReg.virtRegIndex();
    if (I >= VRegStackified.size())
      VRegStackified.resize(I + 1);
    VRegStackified.set(I);
  }
  void unstackifyVReg(Register VReg) {
    auto I = VReg.virtRegIndex();
    if (I < VRegStackified.size())
      VRegStackified.reset(I);
  }
  bool isVRegStackified(Register VReg) const {
    auto I = VReg.virtRegIndex();
    if (I >= VRegStackified.size())
      return false;
    return VRegStackified.test(I);
  }

  void initWARegs(MachineRegisterInfo &MRI);
  void setWAReg(Register VReg, unsigned WAReg) {
    assert(WAReg != MSIL::UnusedReg);
    auto I = VReg.virtRegIndex();
    assert(I < WARegs.size());
    WARegs[I] = WAReg;
  }
  unsigned getWAReg(Register VReg) const {
    auto I = VReg.virtRegIndex();
    assert(I < WARegs.size());
    return WARegs[I];
  }

  unsigned getMaxStack() const { return MaxStack; }
  void setMaxStack(unsigned N) { MaxStack = N; }
};

namespace MSIL {

void computeLegalValueVTs(const MSILTargetLowering &TLI, LLVMContext &Ctx,
                          const DataLayout &DL, Type *Ty,
                          SmallVectorImpl<MVT> &ValueVTs);

void computeLegalValueVTs(const Function &F, const TargetMachine &TM, Type *Ty,
                          SmallVectorImpl<MVT> &ValueVTs);

// Compute the signature for a given FunctionType (Ty). Note that it's not the
// signature for ContextFunc (ContextFunc is just used to get various context)
void computeSignatureVTs(const FunctionType *Ty, const Function *TargetFunc,
                         const Function &ContextFunc, const TargetMachine &TM,
                         SmallVectorImpl<MVT> &Params,
                         SmallVectorImpl<MVT> &Results);

} // end namespace MSIL

namespace yaml {

struct MSILFunctionInfo final : public yaml::MachineFunctionInfo {
  std::vector<FlowStringValue> Params;
  std::vector<FlowStringValue> Results;
  unsigned MaxStack = 0;

  MSILFunctionInfo() = default;
  MSILFunctionInfo(const llvm::MachineFunction &MF,
                   const llvm::MSILFunctionInfo &MFI);

  void mappingImpl(yaml::IO &YamlIO) override;
  ~MSILFunctionInfo() override = default;

  /// Returns true if \p Name is a value type that params and results accept:
  /// i32, i64, f32 or f64.
  static bool isValueTypeName(StringRef Name);

  /// When reading, reports the first params or results entry that is not a
  /// value type (isValueTypeName) as a YAML error, so that the MIR parser fails
  /// with the location instead of building a function with an invalid type.
  void checkValueTypes(yaml::IO &YamlIO) const;
};

template <> struct MappingTraits<MSILFunctionInfo> {
  static void mapping(IO &YamlIO, MSILFunctionInfo &MFI) {
    YamlIO.mapOptional("params", MFI.Params, std::vector<FlowStringValue>());
    YamlIO.mapOptional("results", MFI.Results, std::vector<FlowStringValue>());
    YamlIO.mapOptional("maxStack", MFI.MaxStack, 0u);
    if (!YamlIO.outputting())
      MFI.checkValueTypes(YamlIO);
  }
};

} // end namespace yaml

} // end namespace llvm

#endif
