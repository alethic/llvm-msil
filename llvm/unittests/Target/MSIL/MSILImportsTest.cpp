//===- MSILImportsTest.cpp - Tests for the MSIL import tables -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The two import modes of the MSIL backend (ImplementationPlan.md 5.10.13):
//
// - Standalone (clang -S, llc): MSIL::getPInvokeModule is the import table of
//   decision D14, frozen until the end of Phase 2 (no entry added or
//   removed). These tests are a snapshot of D14 as plan section 3 states it,
//   with the section 12 amendment 'Windows import sets follow the DLL
//   exports': puts and putchar from the C library, the C math functions
//   (sqrt ... hypot) and their 'f' variants from the math library, a non-empty
//   "msil-import-module" attribute whatever the name and the triple, and
//   nothing else, on the four x86_64 triples and aarch64 Linux.
// - Linked text (msil-ld's --lto-emit-asm, M15): MSIL::isLinkedModule is
//   true when the module flag "msil-linked" is 1, and MSIL::getLinkedImport
//   returns a function's non-empty "msil-import-module" (its entry is
//   "msil-import-name", else the name), else the symbol's kind-0 operand of
//   the named metadata !msil.imports (!{!"<symbol>", !"<module>",
//   !"<entry>", i32 <kind>}), else nothing; it never consults D14.
//
//===----------------------------------------------------------------------===//

#include "MSILUtilities.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;

namespace {

// D14's C math functions; each also has an 'f' variant.
const char *const MathFunctions[] = {
    "sqrt",  "cbrt",      "floor", "ceil",      "trunc", "round",
    "rint",  "nearbyint", "fmod",  "remainder", "fma",   "fmin",
    "fmax",  "fdim",      "sin",   "cos",       "tan",   "asin",
    "acos",  "atan",      "atan2", "sinh",      "cosh",  "tanh",
    "exp",   "exp2",      "expm1", "log",       "log2",  "log10",
    "log1p", "pow",       "ldexp", "frexp",     "modf",  "hypot"};

// The math functions that msvcrt.dll exports under their own names (D14 as
// amended: 'Windows import sets follow the DLL exports'); with the 'f'
// variants but ldexpf and frexpf.
const char *const MsvcrtMathFunctions[] = {
    "sqrt", "floor", "ceil",  "fmod",  "sin",   "cos",   "tan",
    "asin", "acos",  "atan",  "atan2", "sinh",  "cosh",  "tanh",
    "exp",  "log",   "log10", "pow",   "ldexp", "frexp", "modf"};

// Names that are not Phase-1 imports on any triple: other C library and
// math functions, a long double variant, compiler-rt helpers, other spellings
// of import names and the empty name.
const char *const NotImports[] = {
    "strlen",   "malloc", "free",  "printf", "abs",   "memcpy",
    "exit",     "lround", "exp10", "sincos", "sqrtl", "__powidf2",
    "__divti3", "Puts",   "SQRT",  "puts_",  "_sqrt", ""};

std::vector<std::string> allMathNames() {
  std::vector<std::string> Names;
  for (const char *Name : MathFunctions) {
    Names.push_back(Name);
    Names.push_back(std::string(Name) + "f");
  }
  return Names;
}

bool contains(ArrayRef<const char *> List, StringRef Name) {
  for (const char *Entry : List)
    if (Name == Entry)
      return true;
  return false;
}

// Expects \p Name to be imported from \p Lib on \p TripleName (std::nullopt:
// not an import).
void expectModule(StringRef TripleName, StringRef Name,
                  std::optional<StringRef> Lib) {
  Triple TT(TripleName);
  std::optional<StringRef> Got = MSIL::getPInvokeModule(TT, Name);
  SCOPED_TRACE((TripleName + ": '" + Name + "'").str());
  ASSERT_EQ(Lib.has_value(), Got.has_value())
      << (Got ? ("unexpectedly imported from " + *Got).str()
              : std::string("unexpectedly not imported"));
  if (Lib)
    EXPECT_EQ(*Lib, *Got);
}

std::unique_ptr<Module> parse(LLVMContext &Ctx, StringRef Source) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Source, Err, Ctx);
  if (!M) {
    std::string Msg;
    raw_string_ostream OS(Msg);
    Err.print("MSILImportsTest", OS);
    ADD_FAILURE() << OS.str();
  }
  return M;
}

TEST(MSILImportsTest, StandaloneTableLinuxGnu) {
  for (StringRef TT :
       {"x86_64-unknown-linux-gnu-msil", "aarch64-unknown-linux-gnu-msil"}) {
    // glibc keeps libm separate, and CoreCLR maps only "libc".
    expectModule(TT, "puts", StringRef("libc"));
    expectModule(TT, "putchar", StringRef("libc"));
    for (const std::string &Name : allMathNames())
      expectModule(TT, Name, StringRef("libm.so.6"));
    for (const char *Name : NotImports)
      expectModule(TT, Name, std::nullopt);
  }
}

TEST(MSILImportsTest, StandaloneTableLinuxMusl) {
  StringRef TT = "x86_64-unknown-linux-musl-msil";
  expectModule(TT, "puts", StringRef("libc"));
  expectModule(TT, "putchar", StringRef("libc"));
  for (const std::string &Name : allMathNames())
    expectModule(TT, Name, StringRef("libc"));
  for (const char *Name : NotImports)
    expectModule(TT, Name, std::nullopt);
}

TEST(MSILImportsTest, StandaloneTableWindowsMsvc) {
  StringRef TT = "x86_64-pc-windows-msvc-msil";
  expectModule(TT, "puts", StringRef("ucrtbase"));
  expectModule(TT, "putchar", StringRef("ucrtbase"));
  // ucrtbase.dll exports every D14 math function but frexpf, ldexpf and
  // hypotf, which the UCRT headers implement inline.
  for (const std::string &Name : allMathNames()) {
    bool Missing = Name == "frexpf" || Name == "ldexpf" || Name == "hypotf";
    expectModule(TT, Name,
                 Missing ? std::nullopt
                         : std::optional<StringRef>(StringRef("ucrtbase")));
  }
  for (const char *Name : NotImports)
    expectModule(TT, Name, std::nullopt);
}

TEST(MSILImportsTest, StandaloneTableWindowsGnu) {
  StringRef TT = "x86_64-w64-windows-gnu-msil";
  expectModule(TT, "puts", StringRef("msvcrt"));
  expectModule(TT, "putchar", StringRef("msvcrt"));
  unsigned Imported = 0;
  for (const std::string &Name : allMathNames()) {
    StringRef Base = Name;
    bool IsF = !contains(MathFunctions, Name);
    if (IsF)
      Base = Base.drop_back();
    bool Exported = contains(MsvcrtMathFunctions, Base) && Name != "ldexpf" &&
                    Name != "frexpf";
    Imported += Exported;
    expectModule(TT, Name,
                 Exported ? std::optional<StringRef>(StringRef("msvcrt"))
                          : std::nullopt);
  }
  // 21 double functions and 19 float ones.
  EXPECT_EQ(40u, Imported);
  for (const char *Name : NotImports)
    expectModule(TT, Name, std::nullopt);
}

TEST(MSILImportsTest, StandaloneTableImportAttribute) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parse(Ctx, R"(
    declare i64 @strlen(ptr) #0
    declare double @sqrt(double) #1
    declare i64 @renamed(i64) #2
    declare double @plain(double)
    declare double @cbrt(double) #3
    declare i64 @emptied(i64) #3
    attributes #0 = { "msil-import-module"="mylib" }
    attributes #1 = { "msil-import-module"="C:\5Clibs\5Cfoo.dll" }
    attributes #2 = { "msil-import-module"="mylib" "msil-import-name"="real_name" }
    attributes #3 = { "msil-import-module"="" }
  )");
  ASSERT_TRUE(M);
  for (StringRef TripleName :
       {"x86_64-unknown-linux-gnu-msil", "x86_64-unknown-linux-musl-msil",
        "x86_64-pc-windows-msvc-msil", "x86_64-w64-windows-gnu-msil",
        "aarch64-unknown-linux-gnu-msil"}) {
    SCOPED_TRACE(TripleName);
    Triple TT(TripleName);
    // A non-empty attribute wins whatever the name, also over the table.
    EXPECT_EQ(std::optional<StringRef>("mylib"),
              MSIL::getPInvokeModule(TT, "strlen", M->getFunction("strlen")));
    EXPECT_EQ(std::optional<StringRef>("C:\\libs\\foo.dll"),
              MSIL::getPInvokeModule(TT, "sqrt", M->getFunction("sqrt")));
    EXPECT_EQ(std::optional<StringRef>("mylib"),
              MSIL::getPInvokeModule(TT, "renamed", M->getFunction("renamed")));
    // Without the attribute, or with an empty one, the table decides.
    EXPECT_FALSE(MSIL::getPInvokeModule(TT, "plain", M->getFunction("plain"))
                     .has_value());
    EXPECT_FALSE(
        MSIL::getPInvokeModule(TT, "emptied", M->getFunction("emptied"))
            .has_value());
    EXPECT_EQ(MSIL::getPInvokeModule(TT, "cbrt"),
              MSIL::getPInvokeModule(TT, "cbrt", M->getFunction("cbrt")));
  }
  // The entry point is msil-import-name when it is non-empty, else the name.
  EXPECT_EQ("real_name",
            MSIL::getPInvokeEntry("renamed", M->getFunction("renamed")));
  EXPECT_EQ("strlen",
            MSIL::getPInvokeEntry("strlen", M->getFunction("strlen")));
  EXPECT_EQ("sqrt", MSIL::getPInvokeEntry("sqrt", nullptr));
}

TEST(MSILImportsTest, IsLinkedModule) {
  struct Case {
    const char *Flags;
    bool Linked;
  } Cases[] = {
      // No module flags at all.
      {"", false},
      // msil-ld's annotation: msil-linked = 1, with or without M14's msil-lto.
      {"!llvm.module.flags = !{!0}\n"
       "!0 = !{i32 4, !\"msil-linked\", i32 1}\n",
       true},
      {"!llvm.module.flags = !{!0, !1}\n"
       "!0 = !{i32 4, !\"msil-lto\", i32 1}\n"
       "!1 = !{i32 4, !\"msil-linked\", i32 1}\n",
       true},
      // M14's flag alone is not linked mode.
      {"!llvm.module.flags = !{!0}\n"
       "!0 = !{i32 4, !\"msil-lto\", i32 1}\n",
       false},
      // Only the value 1 is linked; a flag that is no integer counts as
      // absent, as a malformed msil-lto does.
      {"!llvm.module.flags = !{!0}\n"
       "!0 = !{i32 4, !\"msil-linked\", i32 0}\n",
       false},
      {"!llvm.module.flags = !{!0}\n"
       "!0 = !{i32 4, !\"msil-linked\", !\"yes\"}\n",
       false},
      // !msil.imports alone does not make a module linked.
      {"!msil.imports = !{!0}\n"
       "!0 = !{!\"sqrt\", !\"libm.so.6\", !\"sqrt\", i32 0}\n",
       false},
  };
  for (const Case &C : Cases) {
    SCOPED_TRACE(C.Flags);
    LLVMContext Ctx;
    std::unique_ptr<Module> M =
        parse(Ctx, std::string("source_filename = \"ld-temp.o\"\n") + C.Flags);
    ASSERT_TRUE(M);
    EXPECT_EQ(C.Linked, MSIL::isLinkedModule(*M));
  }
}

// The linked module of the next two tests: attribute-bound declarations as
// msil-ld leaves them, and !msil.imports for library calls.
const char LinkedModule[] = R"(
  source_filename = "ld-temp.o"
  declare i32 @puts(ptr) #0
  declare ptr @strdup(ptr) #1
  declare i32 @mylib_add(i32, i32) #2
  declare i32 @htons(i32) #3
  declare double @sqrt(double)
  declare double @cbrt(double) #4
  declare double @fmod(double, double) #5
  declare i32 @ext(i32)
  declare i32 @emptied(i32) #6
  !llvm.module.flags = !{!0, !1}
  !0 = !{i32 4, !"msil-lto", i32 1}
  !1 = !{i32 4, !"msil-linked", i32 1}
  !msil.imports = !{!10, !11, !12, !13, !14, !15}
  !10 = !{!"sqrt", !"libm.so.6", !"sqrt", i32 0}
  !11 = !{!"pow", !"api-ms-win-crt-math-l1-1-0.dll", !"pow", i32 0}
  !12 = !{!"trunc", !"mymath.dll", !"my_trunc", i32 0}
  !13 = !{!"cbrt", !"libm.so.6", !"cbrt", i32 0}
  !14 = !{!"stdout", !"libc.so.6", !"stdout", i32 1}
  !15 = !{!"fmod", !"ucrtbase.dll", !"fmod", i32 0}
  attributes #0 = { "msil-import-module"="libc.so.6" "msil-import-name"="puts" }
  attributes #1 = { "msil-import-module"="api-ms-win-crt-string-l1-1-0.dll" "msil-import-name"="_strdup" }
  attributes #2 = { "msil-import-module"="native.dll" }
  attributes #3 = { "msil-import-module"="WS2_32.dll" "msil-import-name"="#9" }
  attributes #4 = { "msil-import-module"="libmine.so.1" }
  attributes #5 = { "msil-import-name"="_fmod_entry" }
  attributes #6 = { "msil-import-module"="" }
)";

void expectImport(const std::optional<MSIL::NativeImport> &Got,
                  StringRef Module, StringRef Entry) {
  ASSERT_TRUE(Got.has_value());
  EXPECT_EQ(Module, Got->Module);
  EXPECT_EQ(Entry, Got->Entry);
}

TEST(MSILImportsTest, LinkedImportFromAttributes) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parse(Ctx, LinkedModule);
  ASSERT_TRUE(M);
  ASSERT_TRUE(MSIL::isLinkedModule(*M));
  // The module verbatim; the entry is msil-import-name, else the name.
  expectImport(MSIL::getLinkedImport(*M, "puts", M->getFunction("puts")),
               "libc.so.6", "puts");
  expectImport(MSIL::getLinkedImport(*M, "strdup", M->getFunction("strdup")),
               "api-ms-win-crt-string-l1-1-0.dll", "_strdup");
  expectImport(
      MSIL::getLinkedImport(*M, "mylib_add", M->getFunction("mylib_add")),
      "native.dll", "mylib_add");
  expectImport(MSIL::getLinkedImport(*M, "htons", M->getFunction("htons")),
               "WS2_32.dll", "#9");
  // The attribute comes before the !msil.imports operand of the same name.
  expectImport(MSIL::getLinkedImport(*M, "cbrt", M->getFunction("cbrt")),
               "libmine.so.1", "cbrt");
}

TEST(MSILImportsTest, LinkedImportFromMetadata) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parse(Ctx, LinkedModule);
  ASSERT_TRUE(M);
  // Library calls (no function) take the kind-0 operand of their name.
  expectImport(MSIL::getLinkedImport(*M, "pow", nullptr),
               "api-ms-win-crt-math-l1-1-0.dll", "pow");
  expectImport(MSIL::getLinkedImport(*M, "trunc", nullptr), "mymath.dll",
               "my_trunc");
  expectImport(MSIL::getLinkedImport(*M, "cbrt", nullptr), "libm.so.6", "cbrt");
  // A declaration without msil-import-module, or with an empty one, too.
  expectImport(MSIL::getLinkedImport(*M, "sqrt", M->getFunction("sqrt")),
               "libm.so.6", "sqrt");
  // msil-import-name alone does not bind: the operand gives module and entry.
  expectImport(MSIL::getLinkedImport(*M, "fmod", M->getFunction("fmod")),
               "ucrtbase.dll", "fmod");
  // Kind 1 (data) is not a function import.
  EXPECT_FALSE(MSIL::getLinkedImport(*M, "stdout", nullptr).has_value());
  // Nothing binds these: no attribute and no operand. D14 is never
  // consulted, so puts (with no function), floor and putchar are unresolved
  // too.
  EXPECT_FALSE(
      MSIL::getLinkedImport(*M, "ext", M->getFunction("ext")).has_value());
  EXPECT_FALSE(MSIL::getLinkedImport(*M, "emptied", M->getFunction("emptied"))
                   .has_value());
  EXPECT_FALSE(MSIL::getLinkedImport(*M, "puts", nullptr).has_value());
  EXPECT_FALSE(MSIL::getLinkedImport(*M, "floor", nullptr).has_value());
  EXPECT_FALSE(MSIL::getLinkedImport(*M, "putchar", nullptr).has_value());
  // Names are compared exactly.
  EXPECT_FALSE(MSIL::getLinkedImport(*M, "Sqrt", nullptr).has_value());
  EXPECT_FALSE(MSIL::getLinkedImport(*M, "sqrtf", nullptr).has_value());
}

TEST(MSILImportsTest, LinkedImportWithoutMetadata) {
  // A linked module without !msil.imports binds only through attributes.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parse(Ctx, R"(
    declare i32 @puts(ptr) #0
    declare double @sqrt(double)
    !llvm.module.flags = !{!0}
    !0 = !{i32 4, !"msil-linked", i32 1}
    attributes #0 = { "msil-import-module"="libc.so.6" }
  )");
  ASSERT_TRUE(M);
  ASSERT_TRUE(MSIL::isLinkedModule(*M));
  expectImport(MSIL::getLinkedImport(*M, "puts", M->getFunction("puts")),
               "libc.so.6", "puts");
  EXPECT_FALSE(
      MSIL::getLinkedImport(*M, "sqrt", M->getFunction("sqrt")).has_value());
  EXPECT_FALSE(MSIL::getLinkedImport(*M, "sqrt", nullptr).has_value());
}

} // end anonymous namespace
