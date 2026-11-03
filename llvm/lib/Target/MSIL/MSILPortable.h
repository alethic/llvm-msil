//===-- MSILPortable.h - The portable triples' checks and texts -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the checks of the portable triples
/// (msil64/msil32-unknown-dotnet, plan 5.18), which bind no native code, and
/// the texts of the Dotnet link-rule family (plan 5.18.5) that whole-program
/// text and objects report. MSILPrepareIR and MSILAsmPrinter call them from
/// their hooks.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILPORTABLE_H
#define LLVM_LIB_TARGET_MSIL_MSILPORTABLE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CLI/ImageLayout.h"
#include <optional>
#include <string>

namespace llvm {

class Module;
class Triple;

namespace MSIL {

/// Checks the module \p M of a portable target (one that does not bind
/// natively, cli::bindsNatively; plan 5.18.6 PL4): a function or global
/// variable declaration with an "msil-import-module" attribute (C's
/// import_module) is the error T-P2, "'<f>' is declared with import module
/// '<module>', which msil64-unknown-dotnet does not support: the portable
/// target binds no native code", and loses its "msil-import-module" and
/// "msil-import-name" attributes, so no P/Invoke and no IMPORT symbol is made
/// of it. A reported function's calls are erased (an invoke branches to its
/// normal destination) and every other use becomes poison, so that no second
/// error names it; a reported variable stays an external variable, which
/// whole-program text reports as it reports any other one. Returns whether
/// the module changed.
bool checkPortableModule(Module &M);

/// The normalized text of the target triple \p TT, as a portable text
/// (T-P1, the T-P10 guard strings) or an object's record triple key names it:
/// a portable triple's vendor other than `unknown` is reduced to `unknown`
/// (plan 5.18.1 PT5), so that msil64-pc-dotnet builds msil64-unknown-dotnet.
/// Another triple is normalized only.
std::string getTripleText(const Triple &TT);

/// T-P1, the code generation error of the target triple \p TT, which is
/// msil32-unknown-dotnet (plan 5.18.11 P4-1): "target '<triple>' is not
/// supported yet: only msil64-unknown-dotnet is implemented".
std::string getUnsupportedPortableTargetText(const Triple &TT);

/// T-P3, the error of the undefined function \p Name in standalone text of a
/// portable triple (plan 5.18.6 PL5): "function '<f>' is not defined in this
/// module: standalone text for msil64-unknown-dotnet imports nothing (link
/// with msil-ld)".
std::string getStandaloneUndefinedText(StringRef Name);

/// The error of the constructor \p Name, whose IL signature takes arguments
/// that a module initializer under \p Rules does not pass
/// (cli::constructorsTakeArgs): T-P6, "constructor '<f>' takes arguments,
/// which msil64-unknown-dotnet does not pass" (plan 5.18.5 LR5), under Dotnet
/// rules; "global constructor '<f>' takes arguments, which MSIL does not
/// pass" under ELF and COFF rules.
std::string getConstructorArgsText(StringRef Name, cli::SectionRules Rules);

/// T-P7, the error of a null slot of the constructor table \p Section in the
/// module of the source file \p File (cli::rejectsNullConstructorSlots, plan
/// 5.18.5 LR5): "null constructor slot in '<section>' of '<file>':
/// msil64-unknown-dotnet calls every slot".
std::string getNullConstructorSlotText(StringRef Section, StringRef File);

/// The text of T-P8 for the named section \p Section under \p Rules (plan
/// 5.18.5 LR7): under Dotnet rules, "section '<name>' is not supported for
/// msil64-unknown-dotnet: <reason>" for .fini_array and .dtors (reason "the
/// portable target runs no destructor sections"), .preinit_array ("the
/// portable target has no pre-initialization stage"), .ctors and .CRT$X*
/// ("the portable target runs constructors only from .init_array"), each
/// with its .N forms; std::nullopt for every other section and under ELF and
/// COFF rules, whose sections cli::classifySection decides.
std::optional<std::string> getUnsupportedSectionText(StringRef Section,
                                                     cli::SectionRules Rules);

} // namespace MSIL
} // namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MSILPORTABLE_H
