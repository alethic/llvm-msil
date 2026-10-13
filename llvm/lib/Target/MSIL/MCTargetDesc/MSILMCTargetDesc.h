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
/// target flags and opcode predicates of the TableGen records, and the MC
/// factories that LLVMInitializeMSILTargetMC registers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCTARGETDESC_H
#define LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCTARGETDESC_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInstrDesc.h"
#include <cstdint>
#include <string>

namespace llvm {

class formatted_raw_ostream;
class MCAsmInfo;
class MCInstPrinter;
class MCInstrInfo;
class MCRegisterInfo;
class MCStreamer;
class MCSubtargetInfo;
class MCTargetOptions;
class MCTargetStreamer;
class Triple;

// MC factories registered by LLVMInitializeMSILTargetMC. There is no code
// emitter and no asm backend: MSIL emits ILAsm text only.
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
  /// Method reference (call, ldftn).
  OPERAND_METHOD,
  /// Address field of a global variable (ldsfld).
  OPERAND_DATA,
  /// Stand-alone call-site signature (calli).
  OPERAND_SIGNATURE,
  /// Memory access flags (the unaligned. and volatile. prefixes).
  OPERAND_MEMFLAGS,
};

/// Encoding of a memflags_op immediate, filled by MSILSetMemFlags and printed
/// as prefix lines by MSILInstPrinter.
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
  /// Bits 1-0: extra evaluation-stack depth the CIL text of the instruction
  /// needs above max(pops, pushes) (StackPeakExtra).
  StackPeakExtraMask = 0x3,
  /// Bit 2: the instruction has side effects only because it traps on
  /// undefined behaviour (division and remainder), so RegStackify may move it.
  TrapsOnlyOnUB = 0x4,
};

} // end namespace MSILII

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
