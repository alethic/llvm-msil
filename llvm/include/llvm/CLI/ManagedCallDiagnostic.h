//===- llvm/CLI/ManagedCallDiagnostic.h - Managed-call errors ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares cli::DiagnosticInfoManagedCall, the diagnostic with which
// the MSIL target reports the errors of a managed call (ImplementationPlan.md
// 5.20) that slot P0 (MSIL::createManagedCallStubs) and the AsmPrinter find:
// T-M17's two forms, for translation units that declare one managed call
// differently, and a declaration whose "msil-managed-*" attributes do not
// describe a valid managed call. Its kind is a plugin diagnostic kind
// (getNextAvailablePluginDiagnosticKind), and its fields hold the managed
// call's symbol, the error and its detail, so msil-ld's LTO diagnostic handler
// reads them instead of matching the printed text. It prints the texts that
// clang, llc and msil-ld report.
//
// The file is header-only, and only code that links LLVMCore includes it (the
// MSIL target and lldMSIL). LLVMCLI's own sources and CLITests do not include
// it, so plan 5.7.1's LINK_COMPONENTS of LLVMCLI (BinaryFormat MC Support
// TargetParser) stay unchanged.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_MANAGEDCALLDIAGNOSTIC_H
#define LLVM_CLI_MANAGEDCALLDIAGNOSTIC_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include <cstdint>
#include <string>

namespace llvm::cli {

/// The error that a DiagnosticInfoManagedCall reports.
enum class ManagedCallErrorKind : uint8_t {
  /// T-M17: the translation units give the managed call different
  /// 'msil_method' or 'msil_catch' arguments, or different C prototypes.
  DifferentArguments,
  /// T-M17's second form: the units differ only in the managed call's
  /// 'dllexport', 'constructor' and 'destructor' attributes.
  DifferentAttributes,
  /// The declaration's "msil-managed-*" attributes do not describe a valid
  /// managed call.
  InvalidAttributes,
};

/// An error of a managed call found by the MSIL target (severity DS_Error).
class DiagnosticInfoManagedCall final : public DiagnosticInfo {
public:
  /// \p Symbol is the managed call's name without the '__msil_mc.' prefix
  /// (cli::getManagedCallSymbol). \p Detail is the reason of an
  /// InvalidAttributes error, and empty for the others.
  DiagnosticInfoManagedCall(StringRef Symbol, ManagedCallErrorKind Kind,
                            StringRef Detail = {})
      : DiagnosticInfo(getKindID(), DS_Error), Symbol(Symbol), Detail(Detail),
        ErrorKind(Kind) {}

  /// The managed call's symbol: the C name, the asm label or the C++ mangled
  /// name.
  StringRef getSymbol() const { return Symbol; }
  ManagedCallErrorKind getErrorKind() const { return ErrorKind; }
  /// Why the attributes of an InvalidAttributes error are not valid.
  StringRef getDetail() const { return Detail; }

  /// Prints "managed call '<Symbol>' is declared with different
  /// 'msil_method' or 'msil_catch' arguments in different translation
  /// units", "managed call '<Symbol>' is declared with different
  /// 'dllexport', 'constructor' or 'destructor' attributes in different
  /// translation units", or "managed call '<Symbol>' has invalid
  /// "msil-managed-*" attributes: <Detail>".
  void print(DiagnosticPrinter &DP) const override {
    DP << "managed call '" << Symbol << "' ";
    switch (ErrorKind) {
    case ManagedCallErrorKind::DifferentArguments:
      DP << "is declared with different 'msil_method' or 'msil_catch' "
            "arguments in different translation units";
      break;
    case ManagedCallErrorKind::DifferentAttributes:
      DP << "is declared with different 'dllexport', 'constructor' or "
            "'destructor' attributes in different translation units";
      break;
    case ManagedCallErrorKind::InvalidAttributes:
      DP << "has invalid \"msil-managed-*\" attributes: " << Detail;
      break;
    }
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

  std::string Symbol, Detail;
  ManagedCallErrorKind ErrorKind;
};

} // namespace llvm::cli

#endif // LLVM_CLI_MANAGEDCALLDIAGNOSTIC_H
