//===-- MSILMCTargetDesc.h - MSIL target descriptions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from
// llvm/lib/Target/WebAssembly/MCTargetDesc/WebAssemblyMCTargetDesc.h at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides MSIL-specific target descriptions: the operand types,
/// target flags and opcode predicates of the TableGen records, the CIL opcode
/// queries of the encoder, and the MC factories that
/// LLVMInitializeMSILTargetMC registers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCTARGETDESC_H
#define LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCTARGETDESC_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include <cstdint>
#include <memory>
#include <string>

namespace llvm {

class formatted_raw_ostream;
class MCAsmBackend;
class MCAsmInfo;
class MCCodeEmitter;
class MCContext;
class MCInst;
class MCInstPrinter;
class MCMSILObjectTargetWriter;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCStreamer;
class MCSubtargetInfo;
class MCTargetOptions;
class MCTargetStreamer;
class Target;
class Triple;

// MC factories registered by LLVMInitializeMSILTargetMC.
MCAsmInfo *createMSILMCAsmInfo(const MCRegisterInfo &MRI, const Triple &TT,
                               const MCTargetOptions &Options);
MCInstrInfo *createMSILMCInstrInfo();
MCRegisterInfo *createMSILMCRegisterInfo(const Triple &TT);
MCSubtargetInfo *createMSILMCSubtargetInfo(const Triple &TT, StringRef CPU,
                                           StringRef FS);
MCInstPrinter *createMSILMCInstPrinter(const Triple &TT, unsigned SyntaxVariant,
                                       const MCAsmInfo &MAI,
                                       const MCInstrInfo &MII,
                                       const MCRegisterInfo &MRI);
MCTargetStreamer *createMSILAsmTargetStreamer(MCStreamer &S,
                                              formatted_raw_ostream &OS,
                                              MCInstPrinter *InstPrint);
MCTargetStreamer *createMSILNullTargetStreamer(MCStreamer &S);

/// The binary encoder of the CIL_ records: one CIL instruction per MCInst
/// (MSILMCCodeEmitter.cpp).
MCCodeEmitter *createMSILMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx);

/// The assembler backend (MSILAsmBackend.cpp): the token fixup kinds of
/// MSILFixupKinds.h and the PC-relative branch and switch fixups.
MCAsmBackend *createMSILAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                   const MCRegisterInfo &MRI,
                                   const MCTargetOptions &Options);

/// The target object writer that MSILAsmBackend gives the MSIL object writer
/// (llvm/MC/MCMSILObjectWriter.h). Until M19 writes CIL objects this is
/// M13's default MCMSILObjectTargetWriter, whose writeObject reports that CIL
/// object files are not supported yet.
std::unique_ptr<MCObjectTargetWriter> createMSILObjectTargetWriter();

namespace MSIL {

/// Target operand types: the OperandType of the operand definitions in
/// MSILInstrInfo.td.
enum OperandType {
  /// CIL local index (ldloc, stloc).
  OPERAND_LOCAL = MCOI::OPERAND_FIRST_TARGET,
  /// CIL argument index (ldarg, starg).
  OPERAND_ARG,
  /// 32-bit integer immediates.
  OPERAND_I32IMM,
  /// 64-bit integer immediates.
  OPERAND_I64IMM,
  /// 32-bit floating-point immediates.
  OPERAND_F32IMM,
  /// 64-bit floating-point immediates.
  OPERAND_F64IMM,
  /// A function: the method reference (the callee of a call) or the function's
  /// address field (FUNC_ADDR, ldsfld).
  OPERAND_METHOD,
  /// Address field of a global variable (ldsfld).
  OPERAND_DATA,
  /// Stand-alone call-site signature (calli).
  OPERAND_SIGNATURE,
  /// Memory access flags (the unaligned. and volatile. prefixes).
  OPERAND_MEMFLAGS,
  /// The metadata token of a CIL_ record (InlineMethod, InlineField,
  /// InlineType, InlineTok, InlineSig, InlineString): an MSILMemberRefExpr.
  OPERAND_TOKEN,
};

/// Encoding of a memflags_op immediate, filled by MSILSetMemFlags.
/// MSILMCInstLower turns it into CIL_Unaligned and CIL_Volatile prefix
/// instructions ahead of the access.
enum MemFlags : unsigned {
  /// Bits 0-2: the 'unaligned.' value N (1, 2 or 4); 0 means no prefix.
  MemFlagsUnalignedMask = 0x7,
  /// Bit 3: the 'volatile.' prefix.
  MemFlagsVolatile = 0x8,
};

/// Marks a virtual register that has no CIL local (WebAssembly's UnusedReg).
static const unsigned UnusedReg = -1u;

/// Map a CPU name from the native toolchain ("", "x86-64", "generic", ...) to
/// the MSIL processor, "generic" in Phase 1 ("help" is kept).
std::string getMSILCPU(StringRef CPU);

/// Keep only the subtarget features MSIL defines: none in Phase 1, so the
/// native feature strings clang attaches are dropped silently.
std::string filterMSILFeatures(StringRef FS);

} // end namespace MSIL

namespace MSILII {

/// Target-specific flags of MCInstrDesc::TSFlags. Keep in sync with
/// MSILInstrFormats.td.
enum : uint64_t {
  /// Bits 1-0: extra evaluation-stack depth, above max(pops, pushes), reached
  /// by the CIL_ instructions that MSILMCInstLower::lowerImpl expands the
  /// record into (StackPeakExtra, MSILInstrFormats.td).
  StackPeakExtraMask = 0x3,
  /// Bit 2: the instruction has side effects only because it traps on
  /// undefined behaviour (division and remainder), so RegStackify may move it.
  TrapsOnlyOnUB = 0x4,
  /// Bits 18-3: the record's CIL opcode value (the Inst field): 0x00XX for a
  /// one-byte opcode, 0xFEXX for a two-byte opcode, MSIL::NoCILOpcode for a
  /// pseudo or a record that stands for several CIL instructions.
  CILOpcodeShift = 3,
  CILOpcodeMask = 0xFFFFull << CILOpcodeShift,
};

} // end namespace MSILII

namespace MSIL {

/// The CIL opcode value of the records that are not one CIL instruction.
constexpr uint16_t NoCILOpcode = 0xFFFF;

/// The CIL opcode value of a record: a cli::Opcode value, or NoCILOpcode.
inline uint16_t getCILOpcodeValue(const MCInstrDesc &Desc) {
  return uint16_t((Desc.TSFlags & MSILII::CILOpcodeMask) >>
                  MSILII::CILOpcodeShift);
}

/// Whether \p Opcode is a CIL instruction record: a CIL_ record of
/// MSILInstrCIL.td, which stands for exactly one CIL instruction. This is the
/// shared test for "this MCInst is a CIL instruction"; only such MCInsts reach
/// a streamer, the encoder and getCILInstSize. It is false for the
/// target-independent opcodes (KILL, IMPLICIT_DEF, ...: TSFlags 0, the value
/// of nop), for the MachineInstr-level records, including the one-to-one ones
/// such as ADD_I32 whose CIL value is that of their CIL_ twin, and for the
/// pseudos (value NoCILOpcode). The name test is the one
/// MSILInstPrinter::printInst makes.
inline bool isCILRecord(const MCInstrInfo &MCII, unsigned Opcode) {
  return isTargetSpecificOpcode(Opcode) &&
         getCILOpcodeValue(MCII.get(Opcode)) != NoCILOpcode &&
         MCII.getName(Opcode).starts_with("CIL_");
}

/// The number of bytes MSILMCCodeEmitter encodes \p MI in: the opcode (1 or
/// 2 bytes) and its inline operand (a switch: the count and 4 bytes per
/// target). \p MI must be a CIL instruction record (isCILRecord;
/// reportFatalInternalError otherwise).
unsigned getCILInstSize(const MCInst &MI, const MCInstrInfo &MCII);

/// The target object writer that \p MAB, an MSIL asm backend
/// (createMSILAsmBackend), created last through createObjectTargetWriter,
/// or null if it has created none. The MCObjectWriter that the backend made
/// owns it, so it lives as long as the assembler of that writer.
MCMSILObjectTargetWriter *getObjectTargetWriter(const MCAsmBackend &MAB);

} // end namespace MSIL

} // end namespace llvm

// Defines symbolic names for MSIL registers. This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "MSILGenRegisterInfo.inc"

// Defines symbolic names for the MSIL instructions.
//
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "MSILGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "MSILGenSubtargetInfo.inc"

namespace llvm {
namespace MSIL {

inline bool isArgument(unsigned Opc) {
  switch (Opc) {
  case MSIL::ARGUMENT_I32:
  case MSIL::ARGUMENT_I64:
  case MSIL::ARGUMENT_F32:
  case MSIL::ARGUMENT_F64:
    return true;
  default:
    return false;
  }
}

inline bool isCopy(unsigned Opc) {
  switch (Opc) {
  case MSIL::COPY_I32:
  case MSIL::COPY_I64:
  case MSIL::COPY_F32:
  case MSIL::COPY_F64:
    return true;
  default:
    return false;
  }
}

inline bool isTee(unsigned Opc) {
  switch (Opc) {
  case MSIL::TEE_I32:
  case MSIL::TEE_I64:
  case MSIL::TEE_F32:
  case MSIL::TEE_F64:
    return true;
  default:
    return false;
  }
}

inline bool isScalarConst(unsigned Opc) {
  switch (Opc) {
  case MSIL::CONST_I32:
  case MSIL::CONST_I64:
  case MSIL::CONST_F32:
  case MSIL::CONST_F64:
    return true;
  default:
    return false;
  }
}

inline bool isCallDirect(unsigned Opc) { return Opc == MSIL::CALL; }

inline bool isCallIndirect(unsigned Opc) { return Opc == MSIL::CALL_INDIRECT; }

} // end namespace MSIL
} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCTARGETDESC_H
