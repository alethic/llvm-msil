//===-- MSILFixupKinds.h - MSIL specific fixup entries ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from
// llvm/lib/Target/WebAssembly/MCTargetDesc/WebAssemblyFixupKinds.h at
// 64f50ffd4f1c.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The target fixup kinds of MSIL: one per class of CIL metadata token
/// operand (ECMA-335 III.1.9). MSILMCCodeEmitter writes four zero bytes for
/// a token and a fixup of the kind its OperandKind gives; the object writer
/// reads the token's class from the fixup's expression and assigns the token.
/// Branch displacements and switch entries use the generic FK_Data_1 and
/// FK_Data_4 kinds, PC-relative.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILFIXUPKINDS_H
#define LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace MSIL {

enum Fixups {
  /// InlineMethod: call, callvirt, newobj, jmp, ldftn, ldvirtftn.
  fixup_cil_method_token = FirstTargetFixupKind,
  /// InlineField: ldfld, ldflda, stfld, ldsfld, ldsflda, stsfld.
  fixup_cil_field_token,
  /// InlineSig: calli.
  fixup_cil_sig_token,
  /// InlineType: box, castclass, initobj, ldobj, sizeof, stobj, ...
  fixup_cil_type_token,
  /// InlineTok: ldtoken.
  fixup_cil_tok_token,
  /// InlineString: ldstr.
  fixup_cil_string_token,

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

} // end namespace MSIL
} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILFIXUPKINDS_H
