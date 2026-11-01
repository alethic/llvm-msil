//===- llvm/CLI/CallMismatchDiagnostic.h - Signature mismatches -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares cli::DiagnosticInfoCallMismatch, the diagnostic with
// which MSILFixFunctionBitcasts reports ImplementationPlan.md 5.9.8's
// signature mismatch of a call of a definition in msil-ld's merged LTO module
// (module flag "msil-lto"). Its kind is a plugin diagnostic kind
// (getNextAvailablePluginDiagnosticKind), and its fields hold what
// cli::classifyCall decided, so msil-ld's LTO diagnostic handler reads the
// callee's name and the two shapes from them instead of parsing the message
// (plan 5.14.2 N-1). It prints the text of 5.9.8 without the ' in <file>'
// parts.
//
// The file is header-only, and only code that links LLVMCore includes it (the
// MSIL target and lldMSIL). LLVMCLI's own sources and CLITests do not include
// it, so plan 5.7.1's LINK_COMPONENTS of LLVMCLI (BinaryFormat MC Support
// TargetParser) stay unchanged.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_CALLMISMATCHDIAGNOSTIC_H
#define LLVM_CLI_CALLMISMATCHDIAGNOSTIC_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CLI/CallAdapter.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include <cassert>
#include <string>

namespace llvm::cli {

/// 5.9.8's signature mismatch of a call whose shape cli::classifyCall does
/// not find Direct: a warning for an Adapter, an error for an Error.
class DiagnosticInfoCallMismatch final : public DiagnosticInfo {
public:
  /// \p Kind is CallMatchKind::Adapter (severity DS_Warning) or
  /// CallMatchKind::Error (DS_Error), never Direct.
  DiagnosticInfoCallMismatch(StringRef Name, CallMatchKind Kind,
                             StringRef Defined, StringRef Called,
                             StringRef Detail)
      : DiagnosticInfo(getKindID(),
                       Kind == CallMatchKind::Error ? DS_Error : DS_Warning),
        Name(Name), Defined(Defined), Called(Called), Detail(Detail),
        MatchKind(Kind) {
    assert(Kind != CallMatchKind::Direct && "a direct call is no mismatch");
  }

  /// The callee's IL name.
  StringRef getName() const { return Name; }
  CallMatchKind getMatchKind() const { return MatchKind; }
  /// The definition's shape, as cli::printCallShape prints it.
  StringRef getDefined() const { return Defined; }
  /// The call's shape, as cli::printCallShape prints it.
  StringRef getCalled() const { return Called; }
  /// CallMatch::Detail: what the adapter does, or why the call cannot be
  /// adapted.
  StringRef getDetail() const { return Detail; }

  /// Prints "function signature mismatch: <Name>", the lines ">>> defined as
  /// <Defined>" and ">>> called as <Called>", and ">>> adapter: <Detail>" for
  /// an Adapter or ">>> <Detail>" for an Error.
  void print(DiagnosticPrinter &DP) const override {
    DP << "function signature mismatch: " << Name << "\n>>> defined as "
       << Defined << "\n>>> called as " << Called << "\n>>> ";
    if (MatchKind == CallMatchKind::Adapter)
      DP << "adapter: ";
    DP << Detail;
  }

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == getKindID();
  }

private:
  /// The plugin kind of this class, allocated when it is first used.
  static int getKindID() {
    static const int ID = getNextAvailablePluginDiagnosticKind();
    return ID;
  }

  std::string Name, Defined, Called, Detail;
  CallMatchKind MatchKind;
};

} // namespace llvm::cli

#endif // LLVM_CLI_CALLMISMATCHDIAGNOSTIC_H
