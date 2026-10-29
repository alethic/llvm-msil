//===- MSILTargetMachineTest.cpp - Tests for the MSIL TargetMachine -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The code generation options the MSIL TargetMachine forces whatever the
// driver passes (ImplementationPlan.md decision D8, M2.a item (2)):
// Reloc::Static, CodeModel::Small by default, no address-significance table,
// TrapUnreachable on and NoTrapAfterNoreturn off. The options are given the
// opposite values here, so a TargetMachine that keeps the incoming options
// fails these tests.
//
// There is no exception model either. Codegen reads it from
// Module::getExceptionModel(): the "exception-model" module flag, else the
// triple's default, which is None for every MSIL triple. The TargetMachine
// supports no model, so TargetPassConfig adds no EH preparation pass, and code
// generation resets an incoming flag to "none": IR built for a native triple
// keeps its flag, and llc turns -exception-model into one (clang's MinGW
// toolchain passes -exception-model=seh even for C, but clang emits no flag
// for an MSIL triple).
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
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

// Every exception model a target can support. None is supported trivially
// (supportsExceptionModel asserts on it), and Default is not a model.
const ExceptionHandling NativeExceptionModels[] = {
    ExceptionHandling::DwarfCFI, ExceptionHandling::SjLj,
    ExceptionHandling::ARM,      ExceptionHandling::WinEH,
    ExceptionHandling::Wasm,     ExceptionHandling::Emscripten,
    ExceptionHandling::AIX,      ExceptionHandling::ZOS,
};

// What a native x86_64 MinGW compile hands the TargetMachine: every option
// that the MSIL TargetMachine overrides has the other value.
TargetOptions nativeMinGWOptions() {
  TargetOptions Options;
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
    LLVMInitializeMSILAsmPrinter();
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
    EXPECT_EQ(Reloc::Static, TM->getRelocationModel());
    EXPECT_FALSE(TM->Options.EmitAddrsig);
    EXPECT_TRUE(TM->Options.TrapUnreachable);
    EXPECT_FALSE(TM->Options.NoTrapAfterNoreturn);
  }
}

TEST_F(MSILTargetMachineTest, MSILSupportsNoExceptionModel) {
  for (const char *TripleStr : MSILTriples) {
    SCOPED_TRACE(TripleStr);
    // A module without an "exception-model" flag gets the triple's default.
    ExceptionHandling TripleDefault =
        Triple(TripleStr).getDefaultExceptionHandling();
    EXPECT_EQ(ExceptionHandling::None, TripleDefault) << ehName(TripleDefault);
    // So no EH preparation pass is added for any model.
    std::unique_ptr<TargetMachine> TM =
        createTM(TripleStr, nativeMinGWOptions(), std::nullopt);
    ASSERT_TRUE(TM);
    for (ExceptionHandling Model : NativeExceptionModels)
      EXPECT_FALSE(TM->supportsExceptionModel(Model)) << ehName(Model);
  }
}

TEST_F(MSILTargetMachineTest, MSILCodeGenResetsExceptionModelFlag) {
  // The flag values that Module::getExceptionModel() parses, other than
  // "none". The base AsmPrinter would print .cfi directives around the
  // definition, which may unwind, for "dwarf", and would use the ARM target
  // streamer for "arm".
  const char *const Flags[] = {"dwarf", "sjlj", "arm",
                               "wineh", "wasm", "emscripten"};
  const char *const Body = "define i32 @may_unwind(i32 %x) {\n"
                           "  %r = add i32 %x, 1\n"
                           "  ret i32 %r\n"
                           "}\n"
                           "!llvm.module.flags = !{!0}\n";
  for (const char *TripleStr : MSILTriples) {
    for (const char *Flag : Flags) {
      SCOPED_TRACE(std::string(TripleStr) + ", \"exception-model\" = \"" +
                   Flag + "\"");
      std::unique_ptr<TargetMachine> TM =
          createTM(TripleStr, TargetOptions(), std::nullopt);
      ASSERT_TRUE(TM);
      std::string Source =
          (Twine("target triple = \"") + TripleStr + "\"\n" + Body +
           "!0 = !{i32 1, !\"exception-model\", !\"" + Flag + "\"}\n")
              .str();
      LLVMContext Ctx;
      SMDiagnostic Err;
      std::unique_ptr<Module> M = parseAssemblyString(Source, Err, Ctx);
      ASSERT_TRUE(M) << Err.getMessage().str();
      M->setDataLayout(TM->createDataLayout());

      SmallString<1024> Text;
      raw_svector_ostream OS(Text);
      legacy::PassManager PM;
      ASSERT_FALSE(TM->addPassesToEmitFile(PM, OS, nullptr,
                                           CodeGenFileType::AssemblyFile));
      PM.run(*M);

      EXPECT_EQ(ExceptionHandling::None, M->getExceptionModel())
          << ehName(M->getExceptionModel());
      EXPECT_NE(StringRef::npos, Text.str().find("'may_unwind'"))
          << std::string(Text);
      EXPECT_EQ(StringRef::npos, Text.str().find(".cfi")) << std::string(Text);
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
