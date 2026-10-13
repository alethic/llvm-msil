//===- MSILTargetMachineTest.cpp - Tests for the MSIL TargetMachine -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The code generation options the MSIL TargetMachine forces whatever the
// driver passes (ImplementationPlan.md decision D8, M2.a item (2)): no
// exception model (clang's MinGW toolchain passes -exception-model=seh even
// for C, which BackendUtil turns into WinEH), Reloc::Static, CodeModel::Small
// by default, no address-significance table, TrapUnreachable on and
// NoTrapAfterNoreturn off. The options are given the opposite values here, so
// a TargetMachine that keeps the incoming options fails these tests.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"

#include <memory>
#include <optional>
#include <string>

using namespace llvm;

namespace {

// The MinGW triples are the ones clang passes -exception-model=seh for; the
// Linux and MSVC triples get the same options from other front ends.
const char *const MSILTriples[] = {
    "x86_64-w64-windows-gnu-msil",
    "aarch64-w64-windows-gnu-msil",
    "x86_64-unknown-linux-gnu-msil",
    "x86_64-pc-windows-msvc-msil",
};

// What a native x86_64 MinGW compile hands the TargetMachine: every option
// that decision D8 overrides has the other value.
TargetOptions nativeMinGWOptions() {
  TargetOptions Options;
  Options.ExceptionModel = ExceptionHandling::WinEH;
  Options.EmitAddrsig = true;
  Options.TrapUnreachable = false;
  Options.NoTrapAfterNoreturn = true;
  return Options;
}

// A readable name for failure messages: the "exception-model" module flag
// spelling, or the enumerator value for the models that have none.
std::string ehName(ExceptionHandling EH) {
  StringRef Name = getExceptionModelName(EH);
  return Name.empty() ? "#" + std::to_string(static_cast<int>(EH)) : Name.str();
}

class MSILTargetMachineTest : public testing::Test {
protected:
  static void SetUpTestSuite() {
    LLVMInitializeMSILTargetInfo();
    LLVMInitializeMSILTargetMC();
    LLVMInitializeMSILTarget();
  }

  static std::unique_ptr<TargetMachine>
  createTM(StringRef TripleStr, const TargetOptions &Options,
           std::optional<Reloc::Model> RM) {
    Triple TT(TripleStr);
    std::string Error;
    const Target *T = TargetRegistry::lookupTarget(TT, Error);
    EXPECT_NE(nullptr, T) << TripleStr.str() << ": " << Error;
    if (!T)
      return nullptr;
    EXPECT_STREQ("msil", T->getName());
    return std::unique_ptr<TargetMachine>(T->createTargetMachine(
        TT, "", "", Options, RM, std::nullopt, CodeGenOptLevel::Default));
  }
};

TEST_F(MSILTargetMachineTest, MSILOverridesNativeCodeGenOptions) {
  for (const char *TripleStr : MSILTriples) {
    SCOPED_TRACE(TripleStr);
    std::unique_ptr<TargetMachine> TM =
        createTM(TripleStr, nativeMinGWOptions(), Reloc::PIC_);
    ASSERT_TRUE(TM);
    EXPECT_EQ(ExceptionHandling::None, TM->Options.ExceptionModel)
        << ehName(TM->Options.ExceptionModel);
    EXPECT_EQ(ExceptionHandling::None,
              TM->getMCAsmInfo().getExceptionHandlingType())
        << ehName(TM->getMCAsmInfo().getExceptionHandlingType());
    EXPECT_EQ(Reloc::Static, TM->getRelocationModel());
    EXPECT_FALSE(TM->Options.EmitAddrsig);
    EXPECT_TRUE(TM->Options.TrapUnreachable);
    EXPECT_FALSE(TM->Options.NoTrapAfterNoreturn);
  }
}

TEST_F(MSILTargetMachineTest, MSILExceptionModelIsNoneForEveryIncomingModel) {
  const ExceptionHandling Models[] = {
      ExceptionHandling::Default,  ExceptionHandling::None,
      ExceptionHandling::DwarfCFI, ExceptionHandling::SjLj,
      ExceptionHandling::ARM,      ExceptionHandling::WinEH,
      ExceptionHandling::Wasm,     ExceptionHandling::Emscripten,
      ExceptionHandling::AIX,      ExceptionHandling::ZOS,
  };
  for (const char *TripleStr : MSILTriples) {
    for (ExceptionHandling Model : Models) {
      SCOPED_TRACE(std::string(TripleStr) + ", incoming exception model " +
                   ehName(Model));
      TargetOptions Options = nativeMinGWOptions();
      Options.ExceptionModel = Model;
      std::unique_ptr<TargetMachine> TM =
          createTM(TripleStr, Options, std::nullopt);
      ASSERT_TRUE(TM);
      EXPECT_EQ(ExceptionHandling::None, TM->Options.ExceptionModel)
          << ehName(TM->Options.ExceptionModel);
      // The MCAsmInfo is set up from the options after the override, so no
      // exception tables or unwind directives can be requested from it.
      EXPECT_EQ(ExceptionHandling::None,
                TM->getMCAsmInfo().getExceptionHandlingType())
          << ehName(TM->getMCAsmInfo().getExceptionHandlingType());
    }
  }
}

TEST_F(MSILTargetMachineTest, MSILRelocationModelIsAlwaysStatic) {
  const std::optional<Reloc::Model> Models[] = {
      std::nullopt, Reloc::Static, Reloc::PIC_,      Reloc::DynamicNoPIC,
      Reloc::ROPI,  Reloc::RWPI,   Reloc::ROPI_RWPI,
  };
  for (const char *TripleStr : MSILTriples) {
    for (std::optional<Reloc::Model> RM : Models) {
      SCOPED_TRACE(std::string(TripleStr) + ", incoming relocation model " +
                   (RM ? std::to_string(static_cast<int>(*RM)) : "none"));
      std::unique_ptr<TargetMachine> TM =
          createTM(TripleStr, nativeMinGWOptions(), RM);
      ASSERT_TRUE(TM);
      EXPECT_EQ(Reloc::Static, TM->getRelocationModel());
      EXPECT_FALSE(TM->isPositionIndependent());
    }
  }
}

TEST_F(MSILTargetMachineTest, MSILDefaultCodeModelIsSmall) {
  for (const char *TripleStr : MSILTriples) {
    SCOPED_TRACE(TripleStr);
    std::unique_ptr<TargetMachine> TM =
        createTM(TripleStr, TargetOptions(), std::nullopt);
    ASSERT_TRUE(TM);
    EXPECT_EQ(CodeModel::Small, TM->getCodeModel());
  }
}

TEST_F(MSILTargetMachineTest, MSILValuesAreVirtualRegisters) {
  // CIL has no physical registers: values stay virtual until ExplicitLocals.
  for (const char *TripleStr : MSILTriples) {
    SCOPED_TRACE(TripleStr);
    std::unique_ptr<TargetMachine> TM =
        createTM(TripleStr, TargetOptions(), std::nullopt);
    ASSERT_TRUE(TM);
    EXPECT_FALSE(TM->usesPhysRegsForValues());
  }
}

} // namespace
