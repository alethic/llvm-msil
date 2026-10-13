//===-- MSILMCAsmInfo.h - MSIL asm properties -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the MSILMCAsmInfo class: the ILAsm
/// syntax properties that keep every GNU assembler directive out of the
/// output of the stock MCAsmStreamer.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCASMINFO_H
#define LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCASMINFO_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {

class MCTargetOptions;
class Triple;

class MSILMCAsmInfo final : public MCAsmInfo {
public:
  explicit MSILMCAsmInfo(const Triple &TT, const MCTargetOptions &Options);
  ~MSILMCAsmInfo() override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCASMINFO_H
