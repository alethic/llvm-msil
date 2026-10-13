//===-- MSILSelectionDAGInfo.cpp - MSIL SelectionDAG Info -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblySelectionDAGInfo.cpp at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the MSILSelectionDAGInfo class.
///
//===----------------------------------------------------------------------===//

#include "MSILSelectionDAGInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_SDNODE_DESC
#include "MSILGenSDNodeInfo.inc"

using namespace llvm;

#define DEBUG_TYPE "msil-selectiondag-info"

MSILSelectionDAGInfo::MSILSelectionDAGInfo()
    : SelectionDAGGenTargetInfo(MSILGenSDNodeInfo) {}

MSILSelectionDAGInfo::~MSILSelectionDAGInfo() = default; // anchor

const char *MSILSelectionDAGInfo::getTargetNodeName(unsigned Opcode) const {
  switch (static_cast<MSILISD::NodeType>(Opcode)) {
  case MSILISD::CALL:
    return "MSILISD::CALL";
  }

  return SelectionDAGGenTargetInfo::getTargetNodeName(Opcode);
}

SDValue MSILSelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align DstAlign, Align SrcAlign, bool IsVolatile,
    bool AlwaysInline, MachinePointerInfo DstPtrInfo,
    MachinePointerInfo SrcPtrInfo) const {
  reportFatalUsageError("MSIL: memcpy is implemented in M7");
}

SDValue MSILSelectionDAGInfo::EmitTargetCodeForMemmove(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align DstAlign, Align SrcAlign, bool IsVolatile,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  reportFatalUsageError("MSIL: memmove is implemented in M7");
}

SDValue MSILSelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Val,
    SDValue Size, Align Alignment, bool IsVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo) const {
  reportFatalUsageError("MSIL: memset is implemented in M7");
}
