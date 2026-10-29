//===- unittests/MC/TargetRegistry.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// The target registry code lives in Support, but it relies on linking in all
// LLVM targets. We keep this test with the MC tests, which already do that, to
// keep the SupportTests target small.

#include "llvm/MC/TargetRegistry.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionMSIL.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Triple.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace llvm;
using ::testing::HasSubstr;

namespace {

/// The MSIL target registers under this name. It never claims an
/// architecture; TargetRegistry::lookupTarget selects it for every triple whose
/// object format is msil, or by name with -march=msil (llvm/docs/MSIL/Design.md
/// section 3, ImplementationPlan.md decision D4).
constexpr const char *MSILTargetName = "msil";

/// MSIL triples: the native triples of Design.md section 3 plus the "-msil"
/// suffix, and the versioned spelling the MSVC toolchain produces.
constexpr const char *MSILTriples[] = {
    "x86_64-unknown-linux-gnu-msil",      "aarch64-unknown-linux-gnu-msil",
    "x86_64-unknown-linux-musl-msil",     "aarch64-unknown-linux-musl-msil",
    "x86_64-pc-windows-msvc-msil",        "aarch64-pc-windows-msvc-msil",
    "x86_64-w64-windows-gnu-msil",        "aarch64-apple-darwin-msil",
    "x86_64-pc-windows-msvc19.40.0-msil",
};

/// The same platforms without the suffix.
constexpr const char *NativeTriples[] = {
    "x86_64-unknown-linux-gnu",  "aarch64-unknown-linux-gnu",
    "x86_64-unknown-linux-musl", "aarch64-unknown-linux-musl",
    "x86_64-pc-windows-msvc",    "aarch64-pc-windows-msvc",
    "x86_64-w64-windows-gnu",    "aarch64-apple-darwin",
};

const Target *findTargetNamed(StringRef Name) {
  for (const Target &T : TargetRegistry::targets())
    if (Name == T.getName())
      return &T;
  return nullptr;
}

bool anyTargetRegistered() {
  return TargetRegistry::targets().begin() != TargetRegistry::targets().end();
}

/// -march=<a target other than msil> cannot compile an MSIL triple, whether or
/// not the MSIL target is built, and the lookup leaves the triple alone.
void expectOtherMarchRejected(const char *TripleStr) {
  for (const Target &Other : TargetRegistry::targets()) {
    StringRef Name = Other.getName();
    if (Name == MSILTargetName)
      continue;
    SCOPED_TRACE("-march=" + Name.str() + " with " + TripleStr);
    Triple TT(TripleStr);
    std::string Error;
    EXPECT_EQ(nullptr, TargetRegistry::lookupTarget(Name, TT, Error));
    EXPECT_FALSE(Error.empty());
    EXPECT_EQ(TripleStr, TT.str());
    EXPECT_TRUE(TT.isOSBinFormatMSIL());
  }
}

TEST(TargetRegistry, TargetHasArchType) {
  // Presence of at least one target will be asserted when done with the loop,
  // else this would pass by accident if InitializeAllTargetInfos were omitted.
  int Count = 0;

  llvm::InitializeAllTargetInfos();

  for (const Target &T : TargetRegistry::targets()) {
    StringRef Name = T.getName();
    // There is really no way (at present) to ask a Target whether it targets
    // a specific architecture, because the logic for that is buried in a
    // predicate.
    // We can't ask the predicate "Are you a function that always returns
    // false?"
    // So given that the cpp backend truly has no target arch, it is skipped.
    // The msil target has no architecture either: it is selected by the
    // triple's object format, and -march=msil keeps the triple's arch.
    if (Name != "cpp" && Name != "msil") {
      Triple::ArchType Arch = Triple::getArchTypeForLLVMName(Name);
      EXPECT_NE(Arch, Triple::UnknownArch);
      ++Count;
    }
  }
  ASSERT_NE(Count, 0);
}

TEST(TargetRegistry, IsValidFeatureListFormat) {
  // Valid strings

  // Empty string is a valid feature string
  EXPECT_TRUE(Target::isValidFeatureListFormat(""));

  EXPECT_TRUE(Target::isValidFeatureListFormat("+some_feature"));
  EXPECT_TRUE(Target::isValidFeatureListFormat("-some_feature"));
  EXPECT_TRUE(
      Target::isValidFeatureListFormat("+feature1,-feature2,+feature3"));
  EXPECT_TRUE(Target::isValidFeatureListFormat("+123"));

  // Strings with single trailing comma are also valid
  EXPECT_TRUE(Target::isValidFeatureListFormat("-feature,"));
  EXPECT_TRUE(
      Target::isValidFeatureListFormat("-feature1,+feature2,+feature3,"));

  // Invalid strings

  // Feature don't start with '+' or '-'
  EXPECT_FALSE(Target::isValidFeatureListFormat("invalid_string"));
  EXPECT_FALSE(Target::isValidFeatureListFormat("+good,bad"));
  EXPECT_FALSE(Target::isValidFeatureListFormat("bad,+good"));

  // String has spaces
  EXPECT_FALSE(Target::isValidFeatureListFormat(" "));
  EXPECT_FALSE(Target::isValidFeatureListFormat(", "));
  EXPECT_FALSE(Target::isValidFeatureListFormat(" avx"));
  EXPECT_FALSE(Target::isValidFeatureListFormat("+avx, -sse"));

  // Redundant commas
  EXPECT_FALSE(Target::isValidFeatureListFormat("+feature1,,+feature2"));
  EXPECT_FALSE(Target::isValidFeatureListFormat(",+feature"));
  EXPECT_FALSE(
      Target::isValidFeatureListFormat("+feature1,,,+feature2,,+feature3"));

  // Feature consists only of '+' or '-'
  EXPECT_FALSE(Target::isValidFeatureListFormat("+"));
  EXPECT_FALSE(Target::isValidFeatureListFormat("-"));
  EXPECT_FALSE(Target::isValidFeatureListFormat("+avx,-"));

  // Only commas
  EXPECT_FALSE(Target::isValidFeatureListFormat(","));
  EXPECT_FALSE(Target::isValidFeatureListFormat(",,"));
  EXPECT_FALSE(Target::isValidFeatureListFormat(",,,"));
}

// Without the MSIL target, an MSIL triple has no target at all: the native
// architecture in the triple only selects the C ABI, so the X86 or AArch64
// target must not be picked instead. The error says how to build the target.
TEST(TargetRegistry, MSILLookupWithoutTarget) {
  llvm::InitializeAllTargetInfos();
  if (findTargetNamed(MSILTargetName))
    GTEST_SKIP() << "the MSIL target is built";
  if (!anyTargetRegistered())
    GTEST_SKIP() << "no targets are built";

  for (const char *Str : MSILTriples) {
    SCOPED_TRACE(Str);
    std::string Error;
    EXPECT_EQ(nullptr, TargetRegistry::lookupTarget(Triple(Str), Error));
    EXPECT_THAT(Error, HasSubstr(Str));
    EXPECT_THAT(Error, HasSubstr("LLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MSIL"));

    // The same through the -march overload without -march.
    Triple TT(Str);
    Error.clear();
    EXPECT_EQ(nullptr, TargetRegistry::lookupTarget("", TT, Error));
    EXPECT_FALSE(Error.empty());
    EXPECT_EQ(Str, TT.str());
  }

  // -march=msil names a target that is not registered.
  Triple Native("x86_64-unknown-linux-gnu");
  std::string Error;
  EXPECT_EQ(nullptr,
            TargetRegistry::lookupTarget(MSILTargetName, Native, Error));
  EXPECT_FALSE(Error.empty());
  EXPECT_EQ("x86_64-unknown-linux-gnu", Native.str());

  // Any other -march is rejected for an MSIL triple.
  expectOtherMarchRejected("x86_64-unknown-linux-gnu-msil");
  expectOtherMarchRejected("x86_64-pc-windows-msvc-msil");

  // Native triples still find their native target.
  if (const Target *X86_64 = findTargetNamed("x86-64")) {
    EXPECT_EQ(X86_64, TargetRegistry::lookupTarget(
                          Triple("x86_64-unknown-linux-gnu"), Error));
    EXPECT_EQ(X86_64, TargetRegistry::lookupTarget(
                          Triple("x86_64-pc-windows-msvc"), Error));
  }
}

// Every MSIL triple selects the msil target, whatever its architecture, and
// the msil target is never selected for a triple that is not MSIL.
TEST(TargetRegistry, MSILLookupByObjectFormat) {
  llvm::InitializeAllTargetInfos();
  const Target *MSIL = findTargetNamed(MSILTargetName);
  if (!MSIL)
    GTEST_SKIP() << "the MSIL target is not built";

  // "msil" is not an architecture name, so -march=msil cannot rewrite the
  // triple's architecture.
  EXPECT_EQ(Triple::UnknownArch,
            Triple::getArchTypeForLLVMName(MSIL->getName()));

  for (const char *Str : MSILTriples) {
    SCOPED_TRACE(Str);
    std::string Error;
    EXPECT_EQ(MSIL, TargetRegistry::lookupTarget(Triple(Str), Error)) << Error;

    // The -march overload without -march gives the same target and leaves
    // the triple alone.
    Triple TT(Str);
    EXPECT_EQ(MSIL, TargetRegistry::lookupTarget("", TT, Error)) << Error;
    EXPECT_EQ(Str, TT.str());
  }
  // The architecture does not matter.
  std::string Error;
  EXPECT_EQ(MSIL, TargetRegistry::lookupTarget(
                      Triple("i686-pc-windows-msvc-msil"), Error))
      << Error;

  for (const char *Str : NativeTriples) {
    SCOPED_TRACE(Str);
    if (const Target *T = TargetRegistry::lookupTarget(Triple(Str), Error))
      EXPECT_NE(MSIL, T);
  }
  if (const Target *T = TargetRegistry::lookupTarget(Triple(""), Error))
    EXPECT_NE(MSIL, T);
  if (const Target *X86_64 = findTargetNamed("x86-64")) {
    EXPECT_EQ(X86_64, TargetRegistry::lookupTarget(
                          Triple("x86_64-unknown-linux-gnu"), Error));
    EXPECT_EQ(X86_64, TargetRegistry::lookupTarget(
                          Triple("x86_64-pc-windows-msvc"), Error));
  }
}

// -march=msil keeps the triple's architecture, vendor, OS and environment
// (they select the C ABI) and only switches the object format to msil.
TEST(TargetRegistry, MSILMarchSetsObjectFormat) {
  llvm::InitializeAllTargetInfos();
  const Target *MSIL = findTargetNamed(MSILTargetName);
  if (!MSIL)
    GTEST_SKIP() << "the MSIL target is not built";

  struct {
    const char *In;
    const char *Out;
  } Cases[] = {
      {"x86_64-unknown-linux-gnu", "x86_64-unknown-linux-gnu-msil"},
      {"aarch64-unknown-linux-musl", "aarch64-unknown-linux-musl-msil"},
      {"x86_64-pc-windows-msvc", "x86_64-pc-windows-msvc-msil"},
      {"aarch64-pc-windows-msvc", "aarch64-pc-windows-msvc-msil"},
      {"x86_64-w64-windows-gnu", "x86_64-w64-windows-gnu-msil"},
      {"aarch64-apple-darwin", "aarch64-apple-darwin-msil"},
      // setObjectFormat() rebuilds the environment from its type name, so
      // -march=msil drops a native environment version. The version has no
      // effect on MSIL code generation (an accepted risk, registry-tm.md
      // section 9); -mtriple=...-msil keeps it (below).
      {"x86_64-pc-windows-msvc19.40.0", "x86_64-pc-windows-msvc-msil"},
      // A triple that is already MSIL is left exactly as it is.
      {"x86_64-unknown-linux-gnu-msil", "x86_64-unknown-linux-gnu-msil"},
      {"x86_64-pc-windows-msvc19.40.0-msil",
       "x86_64-pc-windows-msvc19.40.0-msil"},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.In);
    Triple In(C.In);
    Triple TT(C.In);
    std::string Error;
    EXPECT_EQ(MSIL, TargetRegistry::lookupTarget(MSILTargetName, TT, Error))
        << Error;
    EXPECT_EQ(C.Out, TT.str());
    EXPECT_TRUE(TT.isOSBinFormatMSIL());
    EXPECT_EQ(In.getArch(), TT.getArch());
    EXPECT_EQ(In.getVendor(), TT.getVendor());
    EXPECT_EQ(In.getOS(), TT.getOS());
    EXPECT_EQ(In.getEnvironment(), TT.getEnvironment());
  }
}

// -march=<any other target> cannot compile an MSIL triple.
TEST(TargetRegistry, MSILRejectsOtherMarch) {
  llvm::InitializeAllTargetInfos();
  if (!findTargetNamed(MSILTargetName))
    GTEST_SKIP() << "the MSIL target is not built";

  for (const char *Str : MSILTriples)
    expectOtherMarchRejected(Str);
}

/// Initializes \p MOFI for an IsMSIL context and checks the four logical
/// sections that MCObjectFileInfo creates through MCContext::getMSILSection.
void expectMSILSections(MCContext &Ctx, MCObjectFileInfo &MOFI) {
  MOFI.initMCObjectFileInfo(Ctx, /*PIC=*/false);
  Ctx.setObjectFileInfo(&MOFI);
  MCSection *Text = MOFI.getTextSection();
  MCSection *Data = MOFI.getDataSection();
  MCSection *BSS = MOFI.getBSSSection();
  MCSection *ReadOnly = MOFI.getReadOnlySection();
  ASSERT_NE(nullptr, Text);
  ASSERT_NE(nullptr, Data);
  ASSERT_NE(nullptr, BSS);
  ASSERT_NE(nullptr, ReadOnly);
  SmallPtrSet<MCSection *, 4> Distinct = {Text, Data, BSS, ReadOnly};
  EXPECT_EQ(4u, Distinct.size());

  // Every section of an IsMSIL context is an MCSectionMSIL.
  EXPECT_TRUE(static_cast<MCSectionMSIL *>(Text)->getKind().isText());
  EXPECT_TRUE(static_cast<MCSectionMSIL *>(Data)->getKind().isData());
  EXPECT_TRUE(static_cast<MCSectionMSIL *>(BSS)->getKind().isBSS());
  EXPECT_TRUE(static_cast<MCSectionMSIL *>(ReadOnly)->getKind().isReadOnly());
  EXPECT_TRUE(Text->isText());
  EXPECT_FALSE(Text->isBssSection());
  EXPECT_FALSE(Data->isText());
  EXPECT_FALSE(Data->isBssSection());
  EXPECT_FALSE(BSS->isText());
  EXPECT_TRUE(BSS->isBssSection());
  EXPECT_FALSE(ReadOnly->isText());
  EXPECT_FALSE(ReadOnly->isBssSection());

  // Sections are uniqued by name: the text section is ".text".
  MCSectionMSIL *Text1 = Ctx.getMSILSection(".text", SectionKind::getText());
  MCSectionMSIL *Text2 = Ctx.getMSILSection(".text", SectionKind::getText());
  ASSERT_NE(nullptr, Text1);
  EXPECT_EQ(Text1, Text2);
  EXPECT_EQ(Text, Text1);
  EXPECT_EQ(".text", Text1->getName());
  EXPECT_TRUE(Text1->getKind().isText());
}

// The MSIL MC environment (decision D5) does not need the MSIL target: an
// MCContext for any MSIL triple uses the IsMSIL environment, whatever the
// native object format of the triple's platform, and MCObjectFileInfo gives
// it text, data, bss and read-only sections.
TEST(TargetRegistry, MSILMCContextEnvironment) {
  MCTargetOptions Options;
  MCAsmInfo MAI(Options);
  MCRegisterInfo MRI;

  for (const char *Str : MSILTriples) {
    SCOPED_TRACE(Str);
    Triple TT(Str);
    MCSubtargetInfo STI(TT, "", "", "", "", {}, {}, {}, nullptr, nullptr,
                        nullptr, nullptr, nullptr, nullptr, nullptr);
    MCContext Ctx(TT, MAI, MRI, STI);
    EXPECT_EQ(MCContext::IsMSIL, Ctx.getObjectFileType());
  }
  // Native triples keep their native environment.
  struct {
    const char *Str;
    MCContext::Environment Env;
  } NativeCases[] = {
      {"x86_64-unknown-linux-gnu", MCContext::IsELF},
      {"x86_64-pc-windows-msvc", MCContext::IsCOFF},
      {"aarch64-apple-darwin", MCContext::IsMachO},
  };
  for (const auto &C : NativeCases) {
    SCOPED_TRACE(C.Str);
    Triple TT(C.Str);
    MCSubtargetInfo STI(TT, "", "", "", "", {}, {}, {}, nullptr, nullptr,
                        nullptr, nullptr, nullptr, nullptr, nullptr);
    MCContext Ctx(TT, MAI, MRI, STI);
    EXPECT_EQ(C.Env, Ctx.getObjectFileType());
  }

  Triple TT("x86_64-unknown-linux-gnu-msil");
  MCSubtargetInfo STI(TT, "", "", "", "", {}, {}, {}, nullptr, nullptr, nullptr,
                      nullptr, nullptr, nullptr, nullptr);
  MCContext Ctx(TT, MAI, MRI, STI);
  ASSERT_EQ(MCContext::IsMSIL, Ctx.getObjectFileType());
  MCObjectFileInfo MOFI;
  expectMSILSections(Ctx, MOFI);

  // Symbols are plain MCSymbols.
  MCSymbol *Temp = Ctx.createTempSymbol();
  ASSERT_NE(nullptr, Temp);
  EXPECT_TRUE(Temp->isTemporary());
  MCSymbol *Foo = Ctx.getOrCreateSymbol("foo");
  ASSERT_NE(nullptr, Foo);
  EXPECT_EQ("foo", Foo->getName());
  EXPECT_FALSE(Foo->isTemporary());
  EXPECT_EQ(Foo, Ctx.getOrCreateSymbol("foo"));

  // reset() destroys the sections and forgets their names, so they can be
  // created again.
  Ctx.reset();
  EXPECT_EQ(MCContext::IsMSIL, Ctx.getObjectFileType());
  {
    SCOPED_TRACE("after reset()");
    expectMSILSections(Ctx, MOFI);
  }
  EXPECT_NE(nullptr, Ctx.createTempSymbol());
}

TEST(TargetRegistry, SubtargetCopyPreservesHwMode) {
  InitializeAllTargetInfos();
  InitializeAllTargetMCs();

  for (StringRef TripleName :
       {"x86_64-unknown-linux-gnu", "riscv64-unknown-elf"}) {
    SCOPED_TRACE(TripleName);
    Triple TT(TripleName);
    std::string Error;
    const Target *TheTarget = TargetRegistry::lookupTarget(TT, Error);
    if (!TheTarget)
      continue;
    std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TT));
    MCTargetOptions MCOptions;
    std::unique_ptr<MCAsmInfo> MAI(
        TheTarget->createMCAsmInfo(*MRI, TT, MCOptions));
    std::unique_ptr<MCSubtargetInfo> STI(
        TheTarget->createMCSubtargetInfo(TT, "", ""));
    ASSERT_TRUE(MRI && MAI && STI);
    ASSERT_NE(STI->getHwMode(), 0u);
    ASSERT_NE(STI->getHwModeSet(), 0u);
    MCContext Ctx(TT, *MAI, *MRI, *STI);
    MCSubtargetInfo &Copy = Ctx.getSubtargetCopy(*STI);
    EXPECT_EQ(Copy.getHwMode(), STI->getHwMode());
    EXPECT_EQ(Copy.getHwModeSet(), STI->getHwModeSet());
  }
}

} // end namespace
