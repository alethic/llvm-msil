//===- TrailerDefsTest.cpp - the registry of trailer definitions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/TrailerDefs.h (ImplementationPlan.md 5.14.4 and 5.14.11,
// M24), the registry of trailer definitions: C-named functions whose IL the
// trailer builder writes when nothing else defines them (K2).
// - The rows: getTrailerDefs() is TrailerDefs.def in row order, so a
//   TrailerDefId is its row's index (5.13.7 B1); M24's first row is
//   '__msil_compiler_barrier', void(), scope Any, emitted by
//   emitCompilerBarrier; every row follows T3's naming rules.
// - findTrailerDef by exact name; isOfferedIn for the three scopes on Windows
//   and Linux MSIL triples, executable or not, with synthetic infos for the
//   scopes that no row uses; isToolchainName (N3, 5.14.2).
// - getTrailerDefSignature: a static method with the default calling
//   convention whose types are the row's canonical listing v1 text (5.7.10),
//   for every row and for each element type the registry accepts; any other
//   type is a fatal internal error.
// - emitCompilerBarrier through a recording sink: one emitMethod of
//   '.method assembly static void '__msil_compiler_barrier'() cil managed
//   noinlining' in <Module> whose body is one 'ret' (D36 as amended by the
//   Phase 3 decision brief 3.2; 5.15.6).
// Expected values come from the plan, never from the implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/TrailerDefs.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CLI/Trailer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

//===----------------------------------------------------------------------===//
// TrailerDefs.def, expanded by the test
//===----------------------------------------------------------------------===//

// One row as TrailerDefs.def writes it (5.14.4 T2).
struct DefRow {
  TrailerDefId Id;
  StringRef IdName;
  StringRef Name, Return, Params;
  TrailerDefScope Scope;
  StringRef Emitter;
};

const DefRow DefRows[] = {
#define CLI_TRAILER_DEF(Id, Name, Return, Params, Scope, Emitter)              \
  {TrailerDefId::Id,       #Id,     Name, Return, Params,                      \
   TrailerDefScope::Scope, #Emitter},
#include "llvm/CLI/TrailerDefs.def"
};

// The X-macro declares every row's emitter as void(TrailerDefContext &)
// (5.14.4 T2, T8).
#define CLI_TRAILER_DEF(Id, Name, Return, Params, Scope, Emitter)              \
  static_assert(                                                               \
      std::is_same_v<decltype(&Emitter), void (*)(TrailerDefContext &)>,       \
      #Emitter " is not declared as void(TrailerDefContext &)");
#include "llvm/CLI/TrailerDefs.def"

//===----------------------------------------------------------------------===//
// Signatures
//===----------------------------------------------------------------------===//

// The canonical listing v1 spellings (5.7.10) of the element types that a
// trailer definition's signature may use (M24.b: void, bool, int8-uint64,
// float32, float64, native int and native uint).
struct Spelling {
  StringRef Text;
  ElementType Type;
};

const Spelling Spellings[] = {
    {"void", ELEMENT_TYPE_VOID},    {"bool", ELEMENT_TYPE_BOOLEAN},
    {"int8", ELEMENT_TYPE_I1},      {"uint8", ELEMENT_TYPE_U1},
    {"int16", ELEMENT_TYPE_I2},     {"uint16", ELEMENT_TYPE_U2},
    {"int32", ELEMENT_TYPE_I4},     {"uint32", ELEMENT_TYPE_U4},
    {"int64", ELEMENT_TYPE_I8},     {"uint64", ELEMENT_TYPE_U8},
    {"float32", ELEMENT_TYPE_R4},   {"float64", ELEMENT_TYPE_R8},
    {"native int", ELEMENT_TYPE_I}, {"native uint", ELEMENT_TYPE_U},
};

std::optional<TypeSig> parseType(StringRef Text) {
  for (const Spelling &S : Spellings)
    if (S.Text == Text)
      return TypeSig::get(S.Type);
  return std::nullopt;
}

// The signature a row's text describes (5.14.4 T2, T4): a static method with
// the default calling convention, no generic parameters and no sentinel;
// Params is "" or the parameter types joined by ", ", as in BCLMembers.def.
// Only the return type may be void (II.23.2.10, II.23.2.11).
std::optional<MethodSig> parseSig(StringRef Return, StringRef Params) {
  MethodSig S;
  std::optional<TypeSig> R = parseType(Return);
  if (!R)
    return std::nullopt;
  S.Return = *R;
  if (Params.empty())
    return S;
  SmallVector<StringRef, 8> Parts;
  Params.split(Parts, ", ");
  for (StringRef P : Parts) {
    std::optional<TypeSig> T = parseType(P);
    if (!T || T->getElementType() == ELEMENT_TYPE_VOID)
      return std::nullopt;
    S.Params.push_back(*T);
  }
  return S;
}

// Canonical listing v1's signature text, as printMethodSig writes it.
std::string sigText(const MethodSig &S) {
  std::string Text;
  raw_string_ostream OS(Text);
  printMethodSig(OS, S, [](raw_ostream &OS, uint32_t TypeRef) {
    OS << "<type reference " << TypeRef << ">";
  });
  OS.flush();
  return Text;
}

//===----------------------------------------------------------------------===//
// Scopes and triples
//===----------------------------------------------------------------------===//

struct TripleCase {
  const char *Text;
  bool Windows;
};

// D6's MSIL triples, Windows and Linux.
const TripleCase Triples[] = {
    {"x86_64-pc-windows-msvc-msil", true},
    {"aarch64-pc-windows-msvc-msil", true},
    {"x86_64-w64-windows-gnu-msil", true},
    {"x86_64-unknown-linux-gnu-msil", false},
    {"aarch64-unknown-linux-gnu-msil", false},
    {"x86_64-unknown-linux-musl-msil", false},
};

const char *scopeName(TrailerDefScope S) {
  switch (S) {
  case TrailerDefScope::Any:
    return "Any";
  case TrailerDefScope::Executable:
    return "Executable";
  case TrailerDefScope::WindowsExecutable:
    return "WindowsExecutable";
  }
  return "<unknown scope>";
}

// 5.14.4 T3 and M24.b: Any is every output; Executable an output with an
// entry point; WindowsExecutable such an output for a Windows triple.
bool expectedOffer(TrailerDefScope S, bool Windows, bool IsExecutable) {
  switch (S) {
  case TrailerDefScope::Any:
    return true;
  case TrailerDefScope::Executable:
    return IsExecutable;
  case TrailerDefScope::WindowsExecutable:
    return IsExecutable && Windows;
  }
  ADD_FAILURE() << "a scope this test does not know: " << unsigned(S);
  return false;
}

//===----------------------------------------------------------------------===//
// The recording sink
//===----------------------------------------------------------------------===//

class RecordingSink final : public TrailerSink {
public:
  struct Method {
    std::string Name;
    MethodAttributes Flags = MethodAttributes::PrivateScope;
    MethodImplAttributes ImplFlags = MethodImplAttributes::IL;
    TrailerOwner Owner = TrailerOwner::Module;
    MethodSig Sig;
    std::vector<TypeSig> Locals;
    size_t NumLabelNames = 0;
    unsigned MaxStack = 0;
    uint32_t CodeSize = 0;
    std::vector<ILInstr> Code;
    std::vector<uint8_t> Bytes;
    size_t NumClauses = 0;
  };

  EntityRef getDataField() override { return other("getDataField"); }
  EntityRef addRVAData(StringRef, StringRef FieldName,
                       ArrayRef<uint8_t>) override {
    return other(("addRVAData " + FieldName).str());
  }
  EntityRef getBCLMember(BCLMemberId Id) override {
    return other(("getBCLMember " + getBCLMemberInfo(Id).IdName).str());
  }
  EntityRef declareHelper(StringRef Name, const MethodSig &Sig) override {
    Helpers.push_back({Name.str(), Sig});
    return ++NextRef;
  }
  EntityRef addUserString(StringRef UTF8) override {
    return other(("addUserString " + UTF8).str());
  }
  void emitMethod(const TrailerMethod &M) override {
    Method R;
    R.Name = std::string(M.Name);
    R.Flags = M.Flags;
    R.ImplFlags = M.ImplFlags;
    R.Owner = M.Owner;
    R.Sig = M.Sig;
    R.Locals.assign(M.Locals.begin(), M.Locals.end());
    R.NumLabelNames = M.LabelNames.size();
    R.MaxStack = M.Body.getMaxStack();
    R.CodeSize = M.Body.getCodeSize();
    R.Code.assign(M.Body.instructions().begin(), M.Body.instructions().end());
    // The body's bytes; a token operand would be an error.
    auto NoTokens = [](TokenRef T) -> Expected<uint32_t> {
      return createStringError(inconvertibleErrorCode(),
                               "a token operand (kind %u, value %u)",
                               unsigned(T.getKind()), T.getValue());
    };
    SmallVector<uint8_t, 16> Bytes;
    SmallVector<ExceptionClause, 0> Clauses;
    EXPECT_THAT_ERROR(M.Body.encode(Bytes, Clauses, NoTokens), Succeeded());
    R.Bytes.assign(Bytes.begin(), Bytes.end());
    R.NumClauses = Clauses.size();
    Methods.push_back(std::move(R));
  }

  // Every declareHelper call: (name, signature).
  std::vector<std::pair<std::string, MethodSig>> Helpers;
  std::vector<Method> Methods;
  // Every other sink call, by name.
  std::vector<std::string> OtherCalls;

private:
  EntityRef other(std::string Call) {
    OtherCalls.push_back(std::move(Call));
    return ++NextRef;
  }

  EntityRef NextRef = 0;
};

// M24's row, or null (the callers report it).
const TrailerDefInfo *findBarrier() {
  return findTrailerDef("__msil_compiler_barrier");
}

//===----------------------------------------------------------------------===//
// The rows
//===----------------------------------------------------------------------===//

TEST(CLITrailerDefsTest, RegistryIsTheDefFileInRowOrder) {
  static_assert(std::is_same_v<std::underlying_type_t<TrailerDefId>, uint16_t>);
  static_assert(
      std::is_same_v<std::underlying_type_t<TrailerDefScope>, uint8_t>);
  // 5.14.11's scopes, in order (T3 appends new ones).
  EXPECT_EQ(unsigned(TrailerDefScope::Any), 0u);
  EXPECT_EQ(unsigned(TrailerDefScope::Executable), 1u);
  EXPECT_EQ(unsigned(TrailerDefScope::WindowsExecutable), 2u);

  ArrayRef<TrailerDefInfo> Defs = getTrailerDefs();
  ASSERT_FALSE(Defs.empty());
  ASSERT_EQ(Defs.size(), std::size(DefRows));
  for (size_t K = 0; K != Defs.size(); ++K) {
    const TrailerDefInfo &D = Defs[K];
    const DefRow &R = DefRows[K];
    SCOPED_TRACE(R.IdName);
    // B1: the row order fixes the ids, which objects and tables depend on.
    EXPECT_EQ(unsigned(R.Id), K) << "TrailerDefId::" << R.IdName;
    EXPECT_EQ(unsigned(D.Id), K);
    EXPECT_EQ(D.Name, R.Name);
    EXPECT_EQ(D.Return, R.Return);
    EXPECT_EQ(D.Params, R.Params);
    EXPECT_EQ(D.Scope, R.Scope) << scopeName(D.Scope);
  }
}

TEST(CLITrailerDefsTest, CompilerBarrierIsTheFirstRow) {
  // M24's block (5.14.4 T2): CLI_TRAILER_DEF(CompilerBarrier,
  // "__msil_compiler_barrier", "void", "", Any, emitCompilerBarrier).
  EXPECT_EQ(unsigned(TrailerDefId::CompilerBarrier), 0u);
  ASSERT_FALSE(getTrailerDefs().empty());
  const TrailerDefInfo &D = getTrailerDefs()[0];
  EXPECT_EQ(D.Id, TrailerDefId::CompilerBarrier);
  EXPECT_EQ(D.Name, "__msil_compiler_barrier");
  EXPECT_EQ(D.Return, "void");
  EXPECT_EQ(D.Params, "");
  EXPECT_EQ(D.Scope, TrailerDefScope::Any) << scopeName(D.Scope);
  EXPECT_EQ(DefRows[0].IdName, "CompilerBarrier");
  EXPECT_EQ(DefRows[0].Emitter, "emitCompilerBarrier");
  EXPECT_TRUE(isToolchainName(D.Name));
}

TEST(CLITrailerDefsTest, NamesFollowTheNamingRules) {
  StringSet<> Seen;
  for (const TrailerDefInfo &D : getTrailerDefs()) {
    SCOPED_TRACE(D.Name);
    EXPECT_TRUE(Seen.insert(D.Name).second) << "two rows define one name";
    // A C symbol name, never a '<msil>' name: those are link-content
    // services, which objects never reference (5.14.5 S3, S4).
    ASSERT_FALSE(D.Name.empty());
    EXPECT_TRUE(isAlpha(D.Name[0]) || D.Name[0] == '_');
    EXPECT_TRUE(all_of(D.Name, [](char C) { return isAlnum(C) || C == '_'; }))
        << "not a C identifier";
    // T3: a toolchain name (N3), or a standard C name that the trailer
    // overrides only in a narrower scope than Any.
    EXPECT_TRUE(isToolchainName(D.Name) || D.Scope != TrailerDefScope::Any)
        << "a standard C name with scope Any";
    // T3: exit and its family stay native P/Invokes to the host C library
    // (user decision R4/R5 (1)) ...
    for (StringRef Native : {"exit", "_exit", "_Exit", "quick_exit", "abort"})
      EXPECT_NE(D.Name, Native);
    // ... and compiler-runtime names (N1) are helper-archive members (K1),
    // such as these compiler-rt builtins.
    for (StringRef N1 : {"__extendhfsf2", "__truncsfhf2", "__extendbfsf2",
                         "__truncsfbf2", "__floattidf", "__fixdfti", "__divti3",
                         "__udivti3", "__muldc3", "__powidf2"})
      EXPECT_NE(D.Name, N1);
    // Return and Params are signature text, never empty for the return.
    EXPECT_FALSE(D.Return.empty());
  }
}

//===----------------------------------------------------------------------===//
// Lookup, scopes and toolchain names
//===----------------------------------------------------------------------===//

TEST(CLITrailerDefsTest, FindTrailerDefByExactName) {
  for (const TrailerDefInfo &D : getTrailerDefs()) {
    const TrailerDefInfo *F = findTrailerDef(D.Name);
    ASSERT_NE(F, nullptr) << D.Name;
    EXPECT_EQ(F->Id, D.Id) << D.Name;
    EXPECT_EQ(F->Name, D.Name);
    EXPECT_EQ(F->Return, D.Return);
    EXPECT_EQ(F->Params, D.Params);
    EXPECT_EQ(F->Scope, D.Scope) << scopeName(F->Scope);
  }
  const TrailerDefInfo *B = findTrailerDef("__msil_compiler_barrier");
  ASSERT_NE(B, nullptr);
  EXPECT_EQ(B->Id, TrailerDefId::CompilerBarrier);

  // No prefix, extension, case folding, trimming or quoting: the name is the
  // C symbol exactly (an IR name, a SYMTAB name or a native export).
  for (StringRef Miss : {"", "_", "__msil_", "__msil_compiler_barrie",
                         "__msil_compiler_barrier2", "__msil_compiler_barrier ",
                         " __msil_compiler_barrier", "__MSIL_COMPILER_BARRIER",
                         "__msil_Compiler_Barrier", "_msil_compiler_barrier",
                         "msil_compiler_barrier", "___msil_compiler_barrier",
                         "'__msil_compiler_barrier'", "<msil>compiler_barrier",
                         "__msil_compiler_barrier.1", "compiler_barrier",
                         "exit", "__extendhfsf2", "__atomic_load_4"})
    EXPECT_EQ(findTrailerDef(Miss), nullptr) << "'" << Miss << "'";
  // An embedded NUL is part of the name.
  EXPECT_EQ(findTrailerDef(StringRef("__msil_compiler_barrier\0", 24)),
            nullptr);
}

TEST(CLITrailerDefsTest, IsOfferedInByScope) {
  // Synthetic infos: isOfferedIn decides by the info's Scope alone, also for
  // the scopes no row uses yet and for an info that reuses the barrier's id
  // and name with another scope.
  const TrailerDefInfo Synthetic[] = {
      {TrailerDefId::CompilerBarrier, "__msil_synthetic_any", "void", "",
       TrailerDefScope::Any},
      {TrailerDefId::CompilerBarrier, "__msil_synthetic_executable", "int32",
       "int32, native int", TrailerDefScope::Executable},
      {TrailerDefId::CompilerBarrier, "GetCommandLineW", "native int", "",
       TrailerDefScope::WindowsExecutable},
      {TrailerDefId::CompilerBarrier, "__msil_compiler_barrier", "void", "",
       TrailerDefScope::Executable},
      {TrailerDefId::CompilerBarrier, "__msil_compiler_barrier", "void", "",
       TrailerDefScope::WindowsExecutable},
  };
  const TrailerDefInfo *Barrier = findBarrier();
  EXPECT_NE(Barrier, nullptr) << "no row '__msil_compiler_barrier'";
  for (const TripleCase &C : Triples) {
    Triple TT(C.Text);
    ASSERT_TRUE(TT.isOSBinFormatMSIL()) << C.Text;
    ASSERT_EQ(TT.isOSWindows(), C.Windows) << C.Text;
    for (bool IsExecutable : {false, true}) {
      SCOPED_TRACE(std::string(C.Text) +
                   (IsExecutable ? ", executable" : ", not executable"));
      for (const TrailerDefInfo &D : Synthetic)
        EXPECT_EQ(isOfferedIn(D, TT, IsExecutable),
                  expectedOffer(D.Scope, C.Windows, IsExecutable))
            << D.Name << " (" << scopeName(D.Scope) << ")";
      for (const TrailerDefInfo &D : getTrailerDefs())
        EXPECT_EQ(isOfferedIn(D, TT, IsExecutable),
                  expectedOffer(D.Scope, C.Windows, IsExecutable))
            << D.Name << " (" << scopeName(D.Scope) << ")";
      // The barrier is defined in every output, libraries included.
      if (Barrier)
        EXPECT_TRUE(isOfferedIn(*Barrier, TT, IsExecutable));
    }
  }
}

TEST(CLITrailerDefsTest, IsToolchainName) {
  // N3 (5.14.2): exactly the names that start with "__msil_".
  for (StringRef Yes : {"__msil_compiler_barrier", "__msil_", "__msil_x",
                        "__msil_fatal", "__msil_compiler_barrier.1"})
    EXPECT_TRUE(isToolchainName(Yes)) << Yes;
  // Near misses of the prefix ...
  for (StringRef No :
       {"", "_", "__", "__msil", "__msi_x", "_msil_x", "msil_x", "__MSIL_x",
        "__Msil_x", "___msil_x", " __msil_x", "x__msil_y"})
    EXPECT_FALSE(isToolchainName(No)) << "'" << No << "'";
  // ... and names of the other classes: link-content services, compiler
  // runtime and atomic helpers, setjmp, a Windows API and a C library name.
  for (StringRef No :
       {"<msil>Main", "<msil>dataimport", "__atomic_load_4", "__extendhfsf2",
        "_setjmp", "GetCommandLineW", "__p___argc", "exit"})
    EXPECT_FALSE(isToolchainName(No)) << No;
}

//===----------------------------------------------------------------------===//
// Signatures
//===----------------------------------------------------------------------===//

TEST(CLITrailerDefsTest, SignatureOfEveryRow) {
  for (const TrailerDefInfo &D : getTrailerDefs()) {
    SCOPED_TRACE(D.Name);
    MethodSig S = getTrailerDefSignature(D);
    // Static, default calling convention (T4), not generic, no sentinel.
    EXPECT_EQ(unsigned(S.Header), unsigned(SIG_DEFAULT));
    EXPECT_EQ(S.GenericParamCount, 0u);
    EXPECT_GE(S.RequiredParamCount, S.Params.size());
    std::optional<MethodSig> Expected = parseSig(D.Return, D.Params);
    ASSERT_TRUE(Expected.has_value()) << "a type this test does not know in "
                                      << D.Return << "(" << D.Params << ")";
    EXPECT_TRUE(S == *Expected) << sigText(S);
    // Return and Params are canonical listing v1 text (T2).
    EXPECT_EQ(sigText(S), (D.Return + "(" + D.Params + ")").str());
    // Every call builds the same signature.
    EXPECT_TRUE(getTrailerDefSignature(D) == S);
  }
  // M24's row: void().
  const TrailerDefInfo *Barrier = findBarrier();
  ASSERT_NE(Barrier, nullptr) << "no row '__msil_compiler_barrier'";
  MethodSig B = getTrailerDefSignature(*Barrier);
  EXPECT_TRUE(B == MethodSig()) << sigText(B);
  EXPECT_EQ(B.Return.getElementType(), ELEMENT_TYPE_VOID);
  EXPECT_TRUE(B.Params.empty());
  EXPECT_EQ(sigText(B), "void()");
}

TEST(CLITrailerDefsTest, SignatureElementTypes) {
  // Each element type the registry accepts, as a return type (M24.b's list;
  // "native uint" is 5.7.10's canonical spelling) ...
  struct ReturnCase {
    TrailerDefInfo Info;
    ElementType Type;
  };
  const ReturnCase Returns[] = {
      {{TrailerDefId::CompilerBarrier, "__msil_t", "void", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_VOID},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "bool", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_BOOLEAN},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "int8", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_I1},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "uint8", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_U1},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "int16", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_I2},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "uint16", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_U2},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "int32", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_I4},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "uint32", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_U4},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "int64", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_I8},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "uint64", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_U8},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "float32", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_R4},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "float64", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_R8},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "native int", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_I},
      {{TrailerDefId::CompilerBarrier, "__msil_t", "native uint", "",
        TrailerDefScope::Any},
       ELEMENT_TYPE_U},
  };
  for (const ReturnCase &C : Returns) {
    SCOPED_TRACE(C.Info.Return);
    MethodSig S = getTrailerDefSignature(C.Info);
    EXPECT_EQ(unsigned(S.Header), unsigned(SIG_DEFAULT));
    EXPECT_EQ(S.Return, TypeSig::get(C.Type));
    EXPECT_TRUE(S.Params.empty());
  }

  // ... and as parameters, in order.
  const TrailerDefInfo AllParams{
      TrailerDefId::CompilerBarrier, "__msil_t", "void",
      "bool, int8, uint8, int16, uint16, int32, uint32, int64, uint64, "
      "float32, float64, native int, native uint",
      TrailerDefScope::Any};
  MethodSig P = getTrailerDefSignature(AllParams);
  EXPECT_EQ(unsigned(P.Header), unsigned(SIG_DEFAULT));
  EXPECT_EQ(P.GenericParamCount, 0u);
  EXPECT_GE(P.RequiredParamCount, P.Params.size());
  EXPECT_EQ(P.Return, TypeSig());
  std::vector<TypeSig> Expected;
  for (ElementType T :
       {ELEMENT_TYPE_BOOLEAN, ELEMENT_TYPE_I1, ELEMENT_TYPE_U1, ELEMENT_TYPE_I2,
        ELEMENT_TYPE_U2, ELEMENT_TYPE_I4, ELEMENT_TYPE_U4, ELEMENT_TYPE_I8,
        ELEMENT_TYPE_U8, ELEMENT_TYPE_R4, ELEMENT_TYPE_R8, ELEMENT_TYPE_I,
        ELEMENT_TYPE_U})
    Expected.push_back(TypeSig::get(T));
  EXPECT_EQ(P.Params, Expected);
  EXPECT_EQ(sigText(P),
            (AllParams.Return + "(" + AllParams.Params + ")").str());

  // M24.b's text spells native uint "native unsigned int"; both spellings
  // give ELEMENT_TYPE_U.
  const TrailerDefInfo Unsigned{
      TrailerDefId::CompilerBarrier, "__msil_t", "native unsigned int",
      "native unsigned int, native uint", TrailerDefScope::Any};
  MethodSig U = getTrailerDefSignature(Unsigned);
  EXPECT_EQ(U.Return, TypeSig::get(ELEMENT_TYPE_U));
  EXPECT_EQ(U.Params, (std::vector<TypeSig>{TypeSig::get(ELEMENT_TYPE_U),
                                            TypeSig::get(ELEMENT_TYPE_U)}));
}

#if GTEST_HAS_DEATH_TEST
TEST(CLITrailerDefsTest, UnsupportedTypesAreFatal) {
  // Any other type text is a fatal internal error (M24.b), never a guessed
  // signature: a canonical type outside the list, and an unknown one.
  const TrailerDefInfo BadReturn{TrailerDefId::CompilerBarrier,
                                 "__msil_synthetic_bad", "string", "",
                                 TrailerDefScope::Any};
  EXPECT_DEATH((void)getTrailerDefSignature(BadReturn), "string");
  const TrailerDefInfo BadParam{TrailerDefId::CompilerBarrier,
                                "__msil_synthetic_bad", "void",
                                "int32, float128", TrailerDefScope::Any};
  EXPECT_DEATH((void)getTrailerDefSignature(BadParam), "float128");
}
#endif

//===----------------------------------------------------------------------===//
// The barrier's emitter
//===----------------------------------------------------------------------===//

// '.method assembly static void '__msil_compiler_barrier'() cil managed
// noinlining' in <Module>, with the body 'ret' (5.14.4 T2, T4; M24.b).
void checkBarrier(const RecordingSink::Method &M) {
  EXPECT_EQ(M.Name, "__msil_compiler_barrier");
  // assembly static: 0x0013, no hidebysig, no specialname.
  EXPECT_EQ(unsigned(M.Flags),
            unsigned(MethodAttributes::Assembly | MethodAttributes::Static));
  EXPECT_EQ(unsigned(M.Flags), 0x0013u);
  // cil managed noinlining: IL (0) | Managed (0) | NoInlining (0x0008).
  EXPECT_EQ(unsigned(M.ImplFlags), unsigned(MethodImplAttributes::NoInlining));
  EXPECT_EQ(unsigned(M.ImplFlags), 0x0008u);
  EXPECT_EQ(M.Owner, TrailerOwner::Module);
  EXPECT_TRUE(M.Sig == MethodSig()) << sigText(M.Sig);
  EXPECT_EQ(sigText(M.Sig), "void()");
  EXPECT_TRUE(M.Locals.empty());
  EXPECT_EQ(M.NumLabelNames, 0u);
  // One 'ret', laid out at offset 0; nothing on the stack.
  ASSERT_EQ(M.Code.size(), 1u);
  EXPECT_EQ(unsigned(M.Code[0].Op), unsigned(Opcode::Ret));
  EXPECT_EQ(M.Code[0].Offset, 0u);
  EXPECT_EQ(M.Code[0].Token.getKind(), TokenRef::Kind::None);
  EXPECT_FALSE(M.Code[0].Target.isValid());
  EXPECT_EQ(M.CodeSize, 1u);
  EXPECT_EQ(M.MaxStack, 0u);
  EXPECT_EQ(M.Bytes, (std::vector<uint8_t>{0x2A}));
  EXPECT_EQ(M.NumClauses, 0u);
}

TEST(CLITrailerDefsTest, CompilerBarrierThroughARecordingSink) {
  const TrailerDefInfo *Row = findBarrier();
  ASSERT_NE(Row, nullptr) << "no row '__msil_compiler_barrier'";
  const TrailerDefInfo &Info = *Row;
  RecordingSink Sink;
  // The caller declares the definition before the trailer is built (5.14.11:
  // Method is what the sink's declareHelper returned for Info.Name).
  EntityRef Method =
      Sink.declareHelper(Info.Name, getTrailerDefSignature(Info));
  TrailerInput In;
  TrailerDefContext Ctx{In, Sink, Info, Method};
  emitCompilerBarrier(Ctx);

  // One emitMethod and no other sink call: no field, image, BCL member,
  // helper or user string.
  EXPECT_EQ(Sink.OtherCalls, std::vector<std::string>());
  EXPECT_EQ(Sink.Helpers.size(), 1u) << "the emitter declared a helper";
  ASSERT_EQ(Sink.Methods.size(), 1u);
  const RecordingSink::Method &M = Sink.Methods[0];
  checkBarrier(M);
  // The definition is the declared method: same name, same signature (the
  // sinks pair the two by name, as for '<msil>dataimport', 5.9.10).
  ASSERT_EQ(Sink.Helpers.size(), 1u);
  EXPECT_EQ(M.Name, Sink.Helpers[0].first);
  EXPECT_TRUE(M.Sig == Sink.Helpers[0].second);

  // The definition depends on its row alone (T10): another sink, another
  // declared method and the input of a -shared output give the same method,
  // still owned by <Module>.
  RecordingSink Sink2;
  Sink2.declareHelper("<msil>dataimport", MethodSig());
  EntityRef Method2 =
      Sink2.declareHelper(Info.Name, getTrailerDefSignature(Info));
  TrailerInput In2;
  In2.TwoPhase = true;
  In2.RelocLoopThreshold = 0;
  TrailerDefContext Ctx2{In2, Sink2, Info, Method2};
  emitCompilerBarrier(Ctx2);
  EXPECT_EQ(Sink2.OtherCalls, std::vector<std::string>());
  EXPECT_EQ(Sink2.Helpers.size(), 2u);
  ASSERT_EQ(Sink2.Methods.size(), 1u);
  checkBarrier(Sink2.Methods[0]);
}

} // namespace
