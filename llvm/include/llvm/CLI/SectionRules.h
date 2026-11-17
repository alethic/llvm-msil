//===- llvm/CLI/SectionRules.h - MSIL section rules of a triple -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file selects the link-rule family of a triple (ImplementationPlan.md
// 5.18.5): cli::getSectionRules, the predicates on a rule set and
// cli::bindsNatively are the only code outside ImageLayout.cpp that names a
// SectionRules enumerator other than as a 'case' label, so a new rule set
// cannot silently take another's behaviour. Everything here is inline, so a
// library that needs only these rules (clang's Sema) includes this header
// without linking LLVMCLI; llvm/CLI/ImageLayout.h includes it for the users
// of the image layout.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_SECTIONRULES_H
#define LLVM_CLI_SECTIONRULES_H

#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>

namespace llvm {
namespace cli {

/// The section rules of a triple (5.9.9 rule 1, plan 5.18.5 LR1): Dotnet for
/// the portable triples (msil64/msil32-unknown-dotnet), COFF for Windows
/// triples, ELF for every other one. Code outside this header and
/// ImageLayout.cpp names an enumerator only as a 'case' label of a switch
/// without a 'default'; everything else asks getSectionRules and the
/// predicates below.
enum class SectionRules : uint8_t { ELF, COFF, Dotnet };

/// The section rules of \p TT.
inline SectionRules getSectionRules(const Triple &TT) {
  if (TT.isOSDotNet())
    return SectionRules::Dotnet;
  if (TT.isOSWindows())
    return SectionRules::COFF;
  return SectionRules::ELF;
}

/// Whether outputs of \p TT bind native code: native libraries, P/Invokes,
/// the native data imports, the host C runtime and its ABI (plan 5.18.5 LR2's
/// native-binding sites). False for the portable triples, which bind no
/// native code (brief Q3).
inline bool bindsNatively(const Triple &TT) {
  switch (getSectionRules(TT)) {
  case SectionRules::ELF:
  case SectionRules::COFF:
    return true;
  case SectionRules::Dotnet:
    return false;
  }
  llvm_unreachable("unknown section rules");
}

/// Whether a link under \p Rules defines __start_<X> and __stop_<X> for a
/// referenced output section whose name is a C identifier (5.9.9, LR9): ELF
/// and Dotnet; never under COFF rules.
inline bool definesStartStop(SectionRules Rules) {
  switch (Rules) {
  case SectionRules::ELF:
  case SectionRules::Dotnet:
    return true;
  case SectionRules::COFF:
    return false;
  }
  llvm_unreachable("unknown section rules");
}

/// Whether \p Rules group input sections by the part of their name before
/// the first '$' and sort the members by full name (lld/COFF's grouping):
/// COFF only. ELF and Dotnet group by verbatim name with only .init_array.N
/// (and, under ELF, .fini_array.N) collapsed (LR9).
inline bool groupsDollarSections(SectionRules Rules) {
  switch (Rules) {
  case SectionRules::COFF:
    return true;
  case SectionRules::ELF:
  case SectionRules::Dotnet:
    return false;
  }
  llvm_unreachable("unknown section rules");
}

/// Whether a constructor under \p Rules may take arguments: ELF only, where
/// glibc passes argc, argv and envp to an .init_array entry (TrailerInput::
/// CtorArgs). The MSVC CRT passes none, and under Dotnet rules a constructor
/// with parameters is T-P6 (LR5, brief Q7).
inline bool constructorsTakeArgs(SectionRules Rules) {
  switch (Rules) {
  case SectionRules::ELF:
    return true;
  case SectionRules::COFF:
  case SectionRules::Dotnet:
    return false;
  }
  llvm_unreachable("unknown section rules");
}

/// Whether a null slot of a constructor table is the Dotnet error T-P7
/// ('null constructor slot in '<section>' of '<file>': msil64-unknown-dotnet
/// calls every slot', LR5): Dotnet only. COFF skips such a slot
/// (skipsNullConstructorSlots in llvm/CLI/Trailer.h), and ELF keeps its own
/// error for a slot that is not a function address.
inline bool rejectsNullConstructorSlots(SectionRules Rules) {
  switch (Rules) {
  case SectionRules::Dotnet:
    return true;
  case SectionRules::ELF:
  case SectionRules::COFF:
    return false;
  }
  llvm_unreachable("unknown section rules");
}

/// Whether \p Rules have destructor and pre-initialization sections at all
/// (.fini_array and .preinit_array under ELF rules, .CRT$XT* under COFF
/// rules): ELF and COFF. Under Dotnet rules .fini_array, .preinit_array,
/// .ctors, .dtors and .CRT$X* are T-P8 (LR7); destructors register through
/// __cxa_atexit instead (LR6).
inline bool allowsFiniSections(SectionRules Rules) {
  switch (Rules) {
  case SectionRules::ELF:
  case SectionRules::COFF:
    return true;
  case SectionRules::Dotnet:
    return false;
  }
  llvm_unreachable("unknown section rules");
}

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_SECTIONRULES_H
