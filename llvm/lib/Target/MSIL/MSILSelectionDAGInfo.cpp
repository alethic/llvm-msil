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
/// memcpy, memmove and memset never become library calls (decision D15): the
/// intrinsics stay in IL in every output, and standalone text calls no C
/// library function outside the import table of decision D14. A memcpy with a
/// constant length that fits an unsigned int32 is 'cpblk'; every other memcpy
/// and every memmove calls System.Buffer::MemoryCopy, which has memmove
/// semantics on every runtime (an overlapping cpblk does not).
///
/// A memset is 'initblk', whose length is an unsigned int32, and no memset
/// length is ever cut. A constant length that does not fit 32 bits is split
/// into blocks of 2 GiB, the rest last. A length known only at run time that
/// may have bits at or above bit 32 becomes MSILISD::INITBLK_LONG, a guarded
/// loop that MSILTargetLowering::emitLongInitBlk builds after instruction
/// selection: when none of those bits is set, one initblk fills the block;
/// otherwise initblks of 2 GiB fill it while the rest does not fit 32 bits,
/// and one more initblk fills the rest. The hidden option
/// -msil-memset-guard-bits=<N> moves the guard to bits N and up and the blocks
/// to 2^(N-1) bytes, so that tests can run the loop at small lengths.
///
//===----------------------------------------------------------------------===//

#include "MSILSelectionDAGInfo.h"
#include "llvm/ADT/APInt.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"

#define GET_SDNODE_DESC
#include "MSILGenSDNodeInfo.inc"

using namespace llvm;

#define DEBUG_TYPE "msil-selectiondag-info"

namespace {

/// The parser of -msil-memset-guard-bits: it refuses a value that is not a
/// number from 4 to 32 while the command line is parsed, before any input is
/// read, and never clamps one. From 4 on, every block of the loop starts at a
/// multiple of 8 bytes from the destination, so the initblks of the loop and
/// of the rest keep the pseudo's 'unaligned.' prefix exactly.
struct MemsetGuardBitsParser : public cl::parser<unsigned> {
  MemsetGuardBitsParser(cl::Option &O) : cl::parser<unsigned>(O) {}

  // Returns true on error, as cl::parser<unsigned>::parse does.
  bool parse(cl::Option &O, StringRef ArgName, StringRef Arg, unsigned &Value) {
    if (Arg.getAsInteger(0, Value) || Value < 4 || Value > 32)
      return O.error("'" + Arg + "' is not a number from 4 to 32");
    return false;
  }
};

} // end anonymous namespace

// A memset whose length is known only at run time and may have bits at or
// above bit N tests them and fills blocks of 2^(N-1) bytes in a loop while one
// of them is set. The default, 32, fills a length that initblk cannot take in
// blocks of 2 GiB; a smaller N runs the loop at small lengths, for tests.
static cl::opt<unsigned, false, MemsetGuardBitsParser> MemsetGuardBits(
    "msil-memset-guard-bits", cl::Hidden, cl::init(32), cl::value_desc("N"),
    cl::desc("MSIL: guard a memset whose length is known only at run time "
             "on bits N and up of the length, and fill blocks of 2^(N-1) "
             "bytes while one of them is set (4 to 32)"));

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

  // initblk takes an unsigned int32 length, and a length known only at run
  // time is never cut. N is 32 unless -msil-memset-guard-bits says otherwise.
  // When bits N and up of the zero-extended length are known to be zero, one
  // initblk takes its low 32 bits: at any N, a length proved below 2^N, and at
  // the default N = 32 also every length of 32 bits, which a smaller N guards
  // unless it is proved below 2^N. Otherwise MSILISD::INITBLK_LONG
  // carries the native int length and N, and its custom inserter
  // (MSILTargetLowering::emitLongInitBlk) builds a guard on those bits, a loop
  // of initblks of 2^(N-1) bytes while one of them is set, and one initblk of
  // the rest.
  std::optional<uint64_t> Length = getConstantLength(Size);
  if (!Length) {
    unsigned GuardBits = MemsetGuardBits;
    LocationSize MemSize = getBlockSize(std::nullopt);
    SDValue Len = DAG.getZExtOrTrunc(Size, DL, MVT::i64);
    if (DAG.MaskedValueIsZero(Len, APInt::getBitsSetFrom(64, GuardBits)))
      return EmitInitBlk(Chain, Dst, DAG.getZExtOrTrunc(Size, DL, MVT::i32),
                         DstPtrInfo, MemSize, Alignment);
    // MSILISD::INITBLK_LONG (dst, value, len, N): the store memory operand
    // has an unknown size, as for a single initblk of a run-time length.
    MachineMemOperand *MMO =
        getBlockMemOperand(DAG, DstPtrInfo, MachineMemOperand::MOStore,
                           IsVolatile, MemSize, Alignment);
    SDValue Ops[] = {Chain, Dst, Value, Len,
                     DAG.getTargetConstant(GuardBits, DL, MVT::i32)};
    return DAG.getMemIntrinsicNode(MSILISD::INITBLK_LONG, DL,
                                   DAG.getVTList(MVT::Other), Ops, MVT::i8,
                                   MMO);
  }

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
