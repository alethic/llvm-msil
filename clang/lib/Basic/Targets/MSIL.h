//===--- MSIL.h - Declare MSIL target feature support -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares MSILTargetInfo, the TargetInfo of the MSIL object format.
//
// A "<native triple>-msil" triple, such as x86_64-unknown-linux-gnu-msil,
// names a native platform and its C ABI, but asks for CIL (a .NET assembly)
// instead of machine code. MSILTargetInfo therefore wraps the native
// TargetInfo and keeps its data model, struct layout, va_list, target features
// and predefined macros, with three changes:
//  * __MSIL__ is predefined,
//  * long double is IEEE double with double's size and alignment, because CIL
//    has no extended-precision floating-point type, and
//  * global register variables are rejected, because CIL has no machine
//    registers (CodeGen would access them through llvm.read_register and
//    llvm.write_register).
// Layout limits that the native TargetInfo derives from the object format are
// recomputed from the native triple, so they stay the native ones.
//
// Inline assembly is rejected in CodeGen, not here, so that unused static
// inline functions in system headers that contain asm statements still
// compile. Local register variables need no check of their own: they matter
// only as asm operands.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_MSIL_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_MSIL_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>

namespace clang {
namespace targets {

// The native TargetInfo of an MSIL triple, with long double mapped to double
// and __MSIL__ predefined.
template <typename Native>
class LLVM_LIBRARY_VISIBILITY MSILTargetInfo final : public Native {
  // The same fields TargetInfo::adjust() sets for -mlong-double-64.
  void useDoubleForLongDouble() {
    this->LongDoubleWidth = this->DoubleWidth;
    this->LongDoubleAlign = this->DoubleAlign;
    this->LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  }

public:
  MSILTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : Native(Triple, Opts) {
    assert(Triple.isOSBinFormatMSIL() && "not an MSIL triple");
    // Set this before handleTargetFeatures() runs: X86TargetInfo disables
    // long double under -mno-x87 only while its format is x87DoubleExtended.
    useDoubleForLongDouble();
    // X86TargetInfo raises the vector and TLS alignment limits to 8192 bytes
    // for Windows COFF only, a test the msil format fails. Apply the same
    // test to the native triple. AArch64's limits do not depend on the
    // format.
    const llvm::Triple NativeTriple = Triple.getNativeFormatTriple();
    if (NativeTriple.isX86() && NativeTriple.isOSWindows() &&
        NativeTriple.isOSBinFormatCOFF())
      this->MaxVectorAlign = this->MaxTLSAlign = 8192u * this->getCharWidth();
  }

  // CIL has no machine registers to bind a global register variable to.
  bool validateGlobalRegisterVariable(StringRef RegName, unsigned RegSize,
                                      bool &HasSizeMismatch) const override {
    HasSizeMismatch = false;
    return false;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    Native::getTargetDefines(Opts, Builder);
    Builder.defineMacro("__MSIL__");
  }

  void adjust(DiagnosticsEngine &Diags, LangOptions &Opts,
              const TargetInfo *Aux) override {
    Native::adjust(Diags, Opts, Aux);
    // TargetInfo::adjust() has just applied -mlong-double-80 or
    // -mlong-double-128, which CIL cannot honor: diagnose and restore double.
    if (Opts.LongDoubleSize && Opts.LongDoubleSize != this->DoubleWidth)
      Diags.Report(diag::err_opt_not_valid_on_target)
          << ("-mlong-double-" + llvm::Twine(Opts.LongDoubleSize)).str();
    useDoubleForLongDouble();
  }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_MSIL_H
