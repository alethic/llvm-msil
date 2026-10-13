//===-- MSILSelectionDAGInfo.h - MSIL SelectionDAG Info ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblySelectionDAGInfo.h at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the MSIL subclass for SelectionDAGTargetInfo.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILSELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_MSIL_MSILSELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SDNODE_ENUM
#include "MSILGenSDNodeInfo.inc"

namespace llvm {
namespace MSILISD {

enum NodeType : unsigned {
  // A call with variadic results, which TableGen cannot describe:
  // (chain, callee, arguments...) -> (results..., chain). Instruction
  // selection splits it into CALL_PARAMS and CALL_RESULTS.
  CALL = GENERATED_OPCODE_END,
};

} // namespace MSILISD

class MSILSelectionDAGInfo final : public SelectionDAGGenTargetInfo {
public:
  MSILSelectionDAGInfo();

  ~MSILSelectionDAGInfo() override;

  const char *getTargetNodeName(unsigned Opcode) const override;

  // memcpy, memmove and memset never become libcalls (cpblk, initblk and
  // System.Buffer::MemoryCopy).
  SDValue EmitTargetCodeForMemcpy(SelectionDAG &DAG, const SDLoc &DL,
                                  SDValue Chain, SDValue Dst, SDValue Src,
                                  SDValue Size, Align DstAlign, Align SrcAlign,
                                  bool IsVolatile, bool AlwaysInline,
                                  MachinePointerInfo DstPtrInfo,
                                  MachinePointerInfo SrcPtrInfo) const override;
  SDValue
  EmitTargetCodeForMemmove(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                           SDValue Dst, SDValue Src, SDValue Size,
                           Align DstAlign, Align SrcAlign, bool IsVolatile,
                           MachinePointerInfo DstPtrInfo,
                           MachinePointerInfo SrcPtrInfo) const override;
  SDValue EmitTargetCodeForMemset(SelectionDAG &DAG, const SDLoc &DL,
                                  SDValue Chain, SDValue Dst, SDValue Val,
                                  SDValue Size, Align Alignment,
                                  bool IsVolatile, bool AlwaysInline,
                                  MachinePointerInfo DstPtrInfo) const override;
};

} // end namespace llvm

#endif
