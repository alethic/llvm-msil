//===-- MSILSelectionDAGInfo.cpp - MSIL SelectionDAG Info -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblySelectionDAGInfo.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the MSILSelectionDAGInfo class.
///
/// memcpy, memmove and memset never become library calls (decision D15):
/// there is no C library to link against in Phase 1. A memcpy with a constant
/// length that fits an unsigned int32 is 'cpblk'; every other memcpy and every
/// memmove calls System.Buffer::MemoryCopy, which has memmove semantics on
/// every runtime (an overlapping cpblk does not); memset is 'initblk'.
///
//===----------------------------------------------------------------------===//

#include "MSILSelectionDAGInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/MathExtras.h"

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

/// The size of the memory operand of a block of \p Bytes bytes. A
/// MachineMemOperand keeps a precise size as a scalar type of 8 * Bytes bits,
/// which must fit in 32 bits; a larger block, and a variable length, get an
/// unknown size after the pointer.
static LocationSize getBlockSize(std::optional<uint64_t> Bytes) {
  if (Bytes && isUInt<29>(*Bytes))
    return LocationSize::precise(*Bytes);
  return LocationSize::afterPointer();
}

static std::optional<uint64_t> getConstantLength(SDValue Size) {
  if (const auto *C = dyn_cast<ConstantSDNode>(Size))
    return C->getZExtValue();
  return std::nullopt;
}

/// A memory operand of a block access. The memory intrinsic nodes carry these,
/// so the alias analysis of the schedulers sees the accessed memory and
/// MSILSetMemFlags turns their alignment and volatility into the 'unaligned.'
/// and 'volatile.' prefixes of cpblk and initblk.
static MachineMemOperand *getBlockMemOperand(SelectionDAG &DAG,
                                             MachinePointerInfo PtrInfo,
                                             MachineMemOperand::Flags Flags,
                                             bool IsVolatile, LocationSize Size,
                                             Align A) {
  if (IsVolatile)
    Flags |= MachineMemOperand::MOVolatile;
  return DAG.getMachineFunction().getMachineMemOperand(PtrInfo, Flags, Size, A);
}

/// MSILISD::MEMMOVE (src, dst, len): 'conv.i8; dup; call void
/// [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64)' with a
/// native int length. A volatile copy is an ordinary call: CIL has no prefix
/// for it.
static SDValue emitMemMove(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                           SDValue Dst, SDValue Src, SDValue Size,
                           Align DstAlign, Align SrcAlign, bool IsVolatile,
                           MachinePointerInfo DstPtrInfo,
                           MachinePointerInfo SrcPtrInfo) {
  LocationSize MemSize = getBlockSize(getConstantLength(Size));
  MachineMemOperand *MMOs[] = {
      getBlockMemOperand(DAG, DstPtrInfo, MachineMemOperand::MOStore,
                         IsVolatile, MemSize, DstAlign),
      getBlockMemOperand(DAG, SrcPtrInfo, MachineMemOperand::MOLoad, IsVolatile,
                         MemSize, SrcAlign)};
  SDValue Ops[] = {Chain, Src, Dst, DAG.getZExtOrTrunc(Size, DL, MVT::i64)};
  return DAG.getMemIntrinsicNode(MSILISD::MEMMOVE, DL,
                                 DAG.getVTList(MVT::Other), Ops, MVT::i8, MMOs);
}

SDValue MSILSelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align DstAlign, Align SrcAlign, bool IsVolatile,
    bool AlwaysInline, MachinePointerInfo DstPtrInfo,
    MachinePointerInfo SrcPtrInfo) const {
  // cpblk takes an unsigned int32 length. A variable length, or a larger
  // constant, goes to Buffer::MemoryCopy, which takes an int64.
  std::optional<uint64_t> Length = getConstantLength(Size);
  if (!Length || !isUInt<32>(*Length))
    return emitMemMove(DAG, DL, Chain, Dst, Src, Size, DstAlign, SrcAlign,
                       IsVolatile, DstPtrInfo, SrcPtrInfo);

  // MSILISD::CPBLK (dst, src, len): '[unaligned. N] [volatile.] cpblk'.
  LocationSize MemSize = getBlockSize(Length);
  MachineMemOperand *MMOs[] = {
      getBlockMemOperand(DAG, DstPtrInfo, MachineMemOperand::MOStore,
                         IsVolatile, MemSize, DstAlign),
      getBlockMemOperand(DAG, SrcPtrInfo, MachineMemOperand::MOLoad, IsVolatile,
                         MemSize, SrcAlign)};
  SDValue Ops[] = {Chain, Dst, Src, DAG.getConstant(*Length, DL, MVT::i32)};
  return DAG.getMemIntrinsicNode(MSILISD::CPBLK, DL, DAG.getVTList(MVT::Other),
                                 Ops, MVT::i8, MMOs);
}

SDValue MSILSelectionDAGInfo::EmitTargetCodeForMemmove(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align DstAlign, Align SrcAlign, bool IsVolatile,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  // Never cpblk: ECMA-335 leaves overlapping cpblk unspecified, and CoreCLR
  // and .NET Framework give different results for a constant length.
  return emitMemMove(DAG, DL, Chain, Dst, Src, Size, DstAlign, SrcAlign,
                     IsVolatile, DstPtrInfo, SrcPtrInfo);
}

SDValue MSILSelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Val,
    SDValue Size, Align Alignment, bool IsVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo) const {
  // MSILISD::INITBLK (dst, value, len): '[unaligned. N] [volatile.] initblk',
  // which stores the low 8 bits of the int32 value into each byte.
  SDValue Value = DAG.getAnyExtOrTrunc(Val, DL, MVT::i32);
  auto EmitInitBlk = [&](SDValue InChain, SDValue Addr, SDValue Len,
                         MachinePointerInfo PtrInfo, LocationSize MemSize,
                         Align A) {
    MachineMemOperand *MMO = getBlockMemOperand(
        DAG, PtrInfo, MachineMemOperand::MOStore, IsVolatile, MemSize, A);
    SDValue Ops[] = {InChain, Addr, Value, Len};
    return DAG.getMemIntrinsicNode(
        MSILISD::INITBLK, DL, DAG.getVTList(MVT::Other), Ops, MVT::i8, MMO);
  };

  // initblk takes an unsigned int32 length. A variable length is truncated
  // with conv.i4: memset of 4 GiB or more with a variable length is a
  // documented Phase-1 limit.
  std::optional<uint64_t> Length = getConstantLength(Size);
  if (!Length)
    return EmitInitBlk(Chain, Dst, DAG.getZExtOrTrunc(Size, DL, MVT::i32),
                       DstPtrInfo, getBlockSize(std::nullopt), Alignment);

  // A constant length is never truncated: one that does not fit an unsigned
  // int32 is split into blocks of 2 GiB, the remainder last.
  constexpr uint64_t MaxBlock = uint64_t(1) << 31;
  SDValue OutChain = Chain;
  uint64_t Offset = 0;
  do {
    uint64_t Rest = *Length - Offset;
    uint64_t Block = isUInt<32>(Rest) ? Rest : MaxBlock;
    SDValue Addr =
        Offset ? DAG.getObjectPtrOffset(DL, Dst, TypeSize::getFixed(Offset))
               : Dst;
    OutChain =
        EmitInitBlk(OutChain, Addr, DAG.getConstant(Block, DL, MVT::i32),
                    DstPtrInfo.getWithOffset(Offset), getBlockSize(Block),
                    commonAlignment(Alignment, Offset));
    Offset += Block;
  } while (Offset != *Length);
  return OutChain;
}
