//===- MSILMCObjectHooksTest.cpp - Tests for the MSIL MC object hooks -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The lib/MC object interfaces that M13 adds for MSIL (ImplementationPlan.md
// 5.8.11, decision D5 as amended in section 12), tested through the hooks that
// reach them, as M18 and M19 will:
//
// - MCContext::createSymbolImpl creates an MCSymbolMSIL for every symbol of
//   an MSIL context, named or temporary, and each symbol keeps every field;
// - Target::createMCObjectStreamer creates the MCMSILStreamer for both
//   x86_64 MSIL triples, which maps the symbol attributes, common symbols and
//   the local-common error of plan M13.c;
// - MCAsmBackend::createObjectWriter dispatches an MSIL target writer (the
//   format Triple::MSIL) to createMSILObjectWriter, whose writer forwards
//   recordRelocation and writeObject to the target writer;
// - the default MCMSILObjectTargetWriter writes nothing and reports
//   "CIL object files are not supported for MSIL target '<triple>' yet"
//   through the MCContext diagnostic handler, with no crash.
//
// The MSIL target registers no asm backend or code emitter until M18, so the
// tests bring their own (TestAsmBackend, TestCodeEmitter) and target writers.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCMSILObjectWriter.h"
#include "llvm/MC/MCMSILStreamer.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolMSIL.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using namespace llvm;

namespace {

using SymKind = MCSymbolMSIL::Kind;
using MSILLink::Visibility;

// MCContext::createSymbolImpl requires every symbol class to be trivially
// destructible (the context frees symbols without running destructors).
static_assert(std::is_trivially_destructible_v<MCSymbolMSIL>,
              "MCSymbolMSIL must be trivially destructible");

constexpr const char *WinTriple = "x86_64-pc-windows-msvc-msil";
constexpr const char *LinuxTriple = "x86_64-unknown-linux-gnu-msil";
constexpr const char *BothTriples[] = {WinTriple, LinuxTriple};

// What the recording target writer saw.
struct WriterLog {
  unsigned RelocCalls = 0;
  const MCSymbol *RelocSym = nullptr;
  MCFixupKind RelocKind = FK_NONE;
  unsigned WriteCalls = 0;
  const MCAssembler *WriteAsm = nullptr;  // the assembler writeObject got
  const MCAssembler *TargetAsm = nullptr; // MCObjectTargetWriter's own Asm
};

constexpr StringLiteral RecordedObject = "MSIL-TEST-OBJECT";

// A target writer as M18 and M19 write theirs: both virtuals overridden.
class RecordingTargetWriter : public MCMSILObjectTargetWriter {
  WriterLog &Log;

public:
  explicit RecordingTargetWriter(WriterLog &Log) : Log(Log) {}

  void recordRelocation(const MCFragment &, const MCFixup &Fixup,
                        MCValue Target, uint64_t &) override {
    ++Log.RelocCalls;
    Log.RelocSym = Target.getAddSym();
    Log.RelocKind = Fixup.getKind();
  }

  uint64_t writeObject(MCAssembler &Asm, raw_pwrite_stream &OS) override {
    ++Log.WriteCalls;
    Log.WriteAsm = &Asm;
    // MCObjectTargetWriter::getContext and reportError, which a target writer
    // uses for its own diagnostics, need the assembler set on it too.
    Log.TargetAsm = this->Asm;
    OS << RecordedObject;
    return RecordedObject.size();
  }
};

// A target writer of another format, for classof.
class WasmFormatTargetWriter : public MCObjectTargetWriter {
public:
  Triple::ObjectFormatType getFormat() const override { return Triple::Wasm; }
};

using TargetWriterFactory =
    std::function<std::unique_ptr<MCMSILObjectTargetWriter>()>;

// The asm backend M18 will provide, reduced to what the MC layer calls: it
// creates the given target writer, and every fixup that does not resolve
// becomes a relocation, as in every in-tree backend.
class TestAsmBackend : public MCAsmBackend {
  TargetWriterFactory MakeWriter;

public:
  explicit TestAsmBackend(TargetWriterFactory MakeWriter)
      : MCAsmBackend(llvm::endianness::little),
        MakeWriter(std::move(MakeWriter)) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return MakeWriter();
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *, uint64_t Value,
                  bool IsResolved) override {
    maybeAddReloc(F, Fixup, Target, Value, IsResolved);
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *) const override {
    OS.write_zeros(Count);
    return true;
  }
};

class TestCodeEmitter : public MCCodeEmitter {
public:
  void encodeInstruction(const MCInst &, SmallVectorImpl<char> &,
                         SmallVectorImpl<MCFixup> &,
                         const MCSubtargetInfo &) const override {}
};

std::unique_ptr<TestAsmBackend> recordingBackend(WriterLog &Log) {
  return std::make_unique<TestAsmBackend>(
      [&Log] { return std::make_unique<RecordingTargetWriter>(Log); });
}

std::unique_ptr<TestAsmBackend> defaultWriterBackend() {
  return std::make_unique<TestAsmBackend>(
      [] { return std::make_unique<MCMSILObjectTargetWriter>(); });
}

struct Diag {
  SourceMgr::DiagKind Kind;
  std::string Message;
};

// One MSIL MC environment, created through the target registry as llc and
// clang create theirs. The streamer is declared last, so it is destroyed
// before the context it uses.
struct MSILMCEnv {
  Triple TT;
  const Target *TheTarget = nullptr;
  MCTargetOptions Options;
  std::unique_ptr<MCRegisterInfo> MRI;
  std::unique_ptr<MCAsmInfo> MAI;
  std::unique_ptr<MCSubtargetInfo> STI;
  std::unique_ptr<MCContext> Ctx;
  std::unique_ptr<MCObjectFileInfo> MOFI;
  std::vector<Diag> Diags;
  SmallString<64> Bytes;
  raw_svector_ostream OS{Bytes};
  std::unique_ptr<MCStreamer> Streamer;

  explicit MSILMCEnv(StringRef TripleName) : TT(TripleName) {
    std::string Error;
    TheTarget = TargetRegistry::lookupTarget(TT, Error);
    EXPECT_NE(nullptr, TheTarget) << Error;
    if (!TheTarget)
      return;
    MRI.reset(TheTarget->createMCRegInfo(TT));
    MAI.reset(TheTarget->createMCAsmInfo(*MRI, TT, Options));
    STI.reset(TheTarget->createMCSubtargetInfo(TT, "", ""));
    Ctx = std::make_unique<MCContext>(TT, *MAI, *MRI, *STI);
    MOFI.reset(TheTarget->createMCObjectFileInfo(*Ctx, /*PIC=*/false));
    Ctx->setObjectFileInfo(MOFI.get());
    Ctx->setDiagnosticHandler([this](const SMDiagnostic &D, bool,
                                     const SourceMgr &,
                                     std::vector<const MDNode *> &) {
      Diags.push_back({D.getKind(), D.getMessage().str()});
    });
  }

  bool ok() const { return TheTarget && MRI && MAI && STI && Ctx && MOFI; }

  // The object streamer of the registry, as clang's and llc's -filetype=obj
  // paths create it.
  MCStreamer &makeStreamer(std::unique_ptr<MCAsmBackend> MAB) {
    std::unique_ptr<MCObjectWriter> OW = MAB->createObjectWriter(OS);
    EXPECT_NE(nullptr, OW.get());
    Streamer.reset(TheTarget->createMCObjectStreamer(
        TT, *Ctx, std::move(MAB), std::move(OW),
        std::make_unique<TestCodeEmitter>(), *STI));
    EXPECT_NE(nullptr, Streamer.get());
    return *Streamer;
  }

  MCAssembler &assembler() {
    return static_cast<MCObjectStreamer &>(*Streamer).getAssembler();
  }

  MCSymbolMSIL &symbol(StringRef Name) {
    return static_cast<MCSymbolMSIL &>(*Ctx->getOrCreateSymbol(Name));
  }

  // One 8-byte data word that refers to an undefined symbol: the fixup does
  // not resolve, so the backend records it as a relocation.
  void emitReferenceTo(const MCSymbol &Sym) {
    Streamer->switchSection(MOFI->getDataSection());
    Streamer->emitValue(MCSymbolRefExpr::create(&Sym, *Ctx), 8);
  }
};

class MSILMCObjectHooks : public testing::Test {
protected:
  static void SetUpTestSuite() {
    LLVMInitializeMSILTargetInfo();
    LLVMInitializeMSILTargetMC();
  }
};

void expectDefaultFields(const MCSymbolMSIL &S) {
  EXPECT_EQ(SymKind::Unknown, S.getKind());
  EXPECT_FALSE(S.isWeak());
  EXPECT_EQ(Visibility::Default, S.getVisibility());
  EXPECT_FALSE(S.isNoStrip());
  EXPECT_EQ(nullptr, S.getSignature());
  EXPECT_FALSE(S.getImportModule().has_value());
  EXPECT_FALSE(S.getImportName().has_value());
  EXPECT_EQ(nullptr, S.getFuncTarget());
  EXPECT_EQ(0u, S.getExternalMemberId());
  EXPECT_EQ(nullptr, S.getSize());
  EXPECT_FALSE(S.isExternal());
}

int64_t constantValue(const MCExpr *E) {
  int64_t V = -1;
  EXPECT_NE(nullptr, E);
  if (E)
    EXPECT_TRUE(E->evaluateAsAbsolute(V));
  return V;
}

// Every symbol of an MSIL context is an MCSymbolMSIL with the defaults of
// 5.8.11, named or temporary, and keeps every field it is given. The fields
// of all the symbols are set first and read back afterwards: if the context
// allocated a plain MCSymbol, the stores would land in the next symbol's
// storage (or its name entry), and the later symbols' names, lookups or
// fields would come back wrong.
TEST_F(MSILMCObjectHooks, ContextCreatesMSILSymbols) {
  for (const char *TripleName : BothTriples) {
    SCOPED_TRACE(TripleName);
    MSILMCEnv E(TripleName);
    ASSERT_TRUE(E.ok());
    EXPECT_EQ(MCContext::IsMSIL, E.Ctx->getObjectFileType());

    std::vector<std::string> Names = {"alpha", "beta", "gamma", "delta"};
    std::vector<MCSymbol *> Syms;
    for (const std::string &N : Names)
      Syms.push_back(E.Ctx->getOrCreateSymbol(N));
    Syms.push_back(E.Ctx->createTempSymbol());
    Syms.push_back(E.Ctx->createNamedTempSymbol("sig"));
    for (MCSymbol *S : Syms) {
      ASSERT_NE(nullptr, S);
      expectDefaultFields(static_cast<MCSymbolMSIL &>(*S));
    }

    const SymKind Kinds[] = {SymKind::Method,         SymKind::Data,
                             SymKind::FuncAddrField,  SymKind::Signature,
                             SymKind::ExternalMember, SymKind::Method};
    const Visibility Visibilities[] = {Visibility::Internal, Visibility::Hidden,
                                       Visibility::Protected};
    // The signatures are opaque here (llvm::cli::MethodSig is M16's); the
    // symbol only stores the pointer.
    alignas(8) static char SigStorage[6][8];
    std::vector<std::string> Modules, Entries;
    for (size_t I = 0; I != Syms.size(); ++I) {
      Modules.push_back("module" + std::to_string(I) + ".dll");
      Entries.push_back("entry" + std::to_string(I));
    }

    for (size_t I = 0; I != Syms.size(); ++I) {
      auto &S = static_cast<MCSymbolMSIL &>(*Syms[I]);
      S.setKind(Kinds[I]);
      S.setWeak(I % 2 == 0);
      S.setVisibility(Visibilities[I % 3]);
      S.setNoStrip(I % 2 == 1);
      S.setSignature(reinterpret_cast<const cli::MethodSig *>(SigStorage[I]));
      S.setImportModule(StringRef(Modules[I]));
      S.setImportName(StringRef(Entries[I]));
      S.setFuncTarget(
          &static_cast<MCSymbolMSIL &>(*Syms[(I + 1) % Syms.size()]));
      S.setExternalMemberId(0x0A000001 + I);
      S.setSize(MCConstantExpr::create(8 * (I + 1), *E.Ctx));
      S.setExternal(I % 3 != 0);
    }

    for (size_t I = 0; I != Syms.size(); ++I) {
      SCOPED_TRACE(I);
      auto &S = static_cast<MCSymbolMSIL &>(*Syms[I]);
      EXPECT_EQ(Kinds[I], S.getKind());
      EXPECT_EQ(I % 2 == 0, S.isWeak());
      EXPECT_EQ(Visibilities[I % 3], S.getVisibility());
      EXPECT_EQ(I % 2 == 1, S.isNoStrip());
      EXPECT_EQ(reinterpret_cast<const cli::MethodSig *>(SigStorage[I]),
                S.getSignature());
      EXPECT_EQ(std::optional<StringRef>(Modules[I]), S.getImportModule());
      EXPECT_EQ(std::optional<StringRef>(Entries[I]), S.getImportName());
      EXPECT_EQ(&static_cast<MCSymbolMSIL &>(*Syms[(I + 1) % Syms.size()]),
                S.getFuncTarget());
      EXPECT_EQ(0x0A000001 + I, S.getExternalMemberId());
      EXPECT_EQ(int64_t(8 * (I + 1)), constantValue(S.getSize()));
      EXPECT_EQ(I % 3 != 0, S.isExternal());
      // The MCSymbol part is untouched.
      EXPECT_TRUE(S.isUndefined());
      EXPECT_FALSE(S.isRegistered());
      EXPECT_EQ(I >= Names.size(), S.isTemporary());
    }
    for (size_t I = 0; I != Names.size(); ++I) {
      EXPECT_EQ(Names[I], Syms[I]->getName());
      EXPECT_EQ(Syms[I], E.Ctx->lookupSymbol(Names[I]));
      EXPECT_EQ(Syms[I], E.Ctx->getOrCreateSymbol(Names[I]));
    }

    // Every field can be cleared again.
    auto &S = static_cast<MCSymbolMSIL &>(*Syms[0]);
    S.setKind(SymKind::Unknown);
    S.setWeak(false);
    S.setVisibility(Visibility::Default);
    S.setNoStrip(false);
    S.setSignature(nullptr);
    S.setImportModule(std::nullopt);
    S.setImportName(std::nullopt);
    S.setFuncTarget(nullptr);
    S.setExternalMemberId(0);
    S.setSize(nullptr);
    S.setExternal(false);
    expectDefaultFields(S);
    EXPECT_EQ(Kinds[1], static_cast<MCSymbolMSIL &>(*Syms[1]).getKind());
    EXPECT_TRUE(E.Diags.empty());
  }
}

// Target::createMCObjectStreamer returns a streamer for both x86_64 MSIL
// triples: an object streamer whose assembler owns the backend, the emitter
// and the writer it was given. createMSILStreamer builds the same directly.
TEST_F(MSILMCObjectHooks, ObjectStreamerForBothTriples) {
  for (const char *TripleName : BothTriples) {
    SCOPED_TRACE(TripleName);
    MSILMCEnv E(TripleName);
    ASSERT_TRUE(E.ok());
    WriterLog Log;
    auto MAB = recordingBackend(Log);
    TestAsmBackend *RawMAB = MAB.get();
    std::unique_ptr<MCObjectWriter> OW = MAB->createObjectWriter(E.OS);
    ASSERT_NE(nullptr, OW.get());
    MCObjectWriter *RawOW = OW.get();
    auto MCE = std::make_unique<TestCodeEmitter>();
    MCCodeEmitter *RawMCE = MCE.get();
    E.Streamer.reset(E.TheTarget->createMCObjectStreamer(
        E.TT, *E.Ctx, std::move(MAB), std::move(OW), std::move(MCE), *E.STI));
    ASSERT_NE(nullptr, E.Streamer.get());
    MCAssembler *Asm = E.Streamer->getAssemblerPtr();
    ASSERT_NE(nullptr, Asm);
    EXPECT_EQ(RawMAB, Asm->getBackendPtr());
    EXPECT_EQ(RawMCE, Asm->getEmitterPtr());
    EXPECT_EQ(RawOW, &Asm->getWriter());
    EXPECT_EQ(E.Ctx.get(), &E.Streamer->getContext());

    // The direct factory, with the writer of createMSILObjectWriter.
    MSILMCEnv D(TripleName);
    ASSERT_TRUE(D.ok());
    WriterLog DLog;
    std::unique_ptr<MCStreamer> S(createMSILStreamer(
        *D.Ctx, recordingBackend(DLog),
        createMSILObjectWriter(std::make_unique<RecordingTargetWriter>(DLog),
                               D.OS),
        std::make_unique<TestCodeEmitter>()));
    ASSERT_NE(nullptr, S.get());
    EXPECT_NE(nullptr, S->getAssemblerPtr());
    EXPECT_TRUE(E.Diags.empty());
    EXPECT_TRUE(D.Diags.empty());
  }
}

// The attribute mapping of plan M13.c: each supported attribute returns true,
// registers the symbol with the assembler (as MCWasmStreamer does) and sets
// exactly its field.
TEST_F(MSILMCObjectHooks, SymbolAttributeMapping) {
  for (const char *TripleName : BothTriples) {
    SCOPED_TRACE(TripleName);
    MSILMCEnv E(TripleName);
    ASSERT_TRUE(E.ok());
    WriterLog Log;
    MCStreamer &S = E.makeStreamer(recordingBackend(Log));

    struct {
      MCSymbolAttr Attr;
      const char *Name;
      bool External, Weak, NoStrip;
      Visibility Vis;
      SymKind Kind;
    } Cases[] = {
        {MCSA_Global, "global", true, false, false, Visibility::Default,
         SymKind::Unknown},
        {MCSA_Extern, "extern", true, false, false, Visibility::Default,
         SymKind::Unknown},
        {MCSA_Weak, "weak", true, true, false, Visibility::Default,
         SymKind::Unknown},
        {MCSA_WeakReference, "weakref", true, true, false, Visibility::Default,
         SymKind::Unknown},
        {MCSA_Hidden, "hidden", false, false, false, Visibility::Hidden,
         SymKind::Unknown},
        {MCSA_Internal, "internal", false, false, false, Visibility::Internal,
         SymKind::Unknown},
        {MCSA_Protected, "protected", false, false, false,
         Visibility::Protected, SymKind::Unknown},
        {MCSA_NoDeadStrip, "nostrip", false, false, true, Visibility::Default,
         SymKind::Unknown},
        {MCSA_ELF_TypeFunction, "function", false, false, false,
         Visibility::Default, SymKind::Method},
        {MCSA_ELF_TypeObject, "object", false, false, false,
         Visibility::Default, SymKind::Data},
    };
    for (const auto &C : Cases) {
      SCOPED_TRACE(C.Name);
      MCSymbolMSIL &Sym = E.symbol(C.Name);
      EXPECT_TRUE(S.emitSymbolAttribute(&Sym, C.Attr));
      EXPECT_TRUE(Sym.isRegistered());
      EXPECT_EQ(C.External, Sym.isExternal());
      EXPECT_EQ(C.Weak, Sym.isWeak());
      EXPECT_EQ(C.NoStrip, Sym.isNoStrip());
      EXPECT_EQ(C.Vis, Sym.getVisibility());
      EXPECT_EQ(C.Kind, Sym.getKind());
      EXPECT_EQ(nullptr, Sym.getSize());
    }

    // Attributes combine: a global weak hidden function.
    MCSymbolMSIL &F = E.symbol("combined");
    EXPECT_TRUE(S.emitSymbolAttribute(&F, MCSA_Global));
    EXPECT_TRUE(S.emitSymbolAttribute(&F, MCSA_Weak));
    EXPECT_TRUE(S.emitSymbolAttribute(&F, MCSA_Hidden));
    EXPECT_TRUE(S.emitSymbolAttribute(&F, MCSA_ELF_TypeFunction));
    EXPECT_TRUE(F.isExternal());
    EXPECT_TRUE(F.isWeak());
    EXPECT_EQ(Visibility::Hidden, F.getVisibility());
    EXPECT_EQ(SymKind::Method, F.getKind());

    // emitELFSize, which the AsmPrinter emits after a global's data, sets
    // the size.
    MCSymbolMSIL &Sized = E.symbol("sized");
    const MCExpr *Size = MCConstantExpr::create(24, *E.Ctx);
    S.emitELFSize(&Sized, Size);
    EXPECT_EQ(Size, Sized.getSize());
    EXPECT_TRUE(E.Diags.empty());
  }
}

// Every other attribute is unsupported: emitSymbolAttribute returns false
// (no assertion, no diagnostic) and leaves the MSIL fields alone.
TEST_F(MSILMCObjectHooks, UnsupportedSymbolAttributes) {
  const MCSymbolAttr Supported[] = {
      MCSA_Global,        MCSA_Extern,      MCSA_Weak,
      MCSA_WeakReference, MCSA_Hidden,      MCSA_Internal,
      MCSA_Protected,     MCSA_NoDeadStrip, MCSA_ELF_TypeFunction,
      MCSA_ELF_TypeObject};
  for (const char *TripleName : BothTriples) {
    SCOPED_TRACE(TripleName);
    MSILMCEnv E(TripleName);
    ASSERT_TRUE(E.ok());
    WriterLog Log;
    MCStreamer &S = E.makeStreamer(recordingBackend(Log));
    unsigned Unsupported = 0;
    for (int A = MCSA_Cold; A <= MCSA_XPLinkage; ++A) {
      auto Attr = static_cast<MCSymbolAttr>(A);
      if (is_contained(Supported, Attr))
        continue;
      SCOPED_TRACE(A);
      MCSymbolMSIL &Sym = E.symbol("unsupported" + std::to_string(A));
      EXPECT_FALSE(S.emitSymbolAttribute(&Sym, Attr));
      expectDefaultFields(Sym);
      ++Unsupported;
    }
    // MCSA_IndirectSymbol, MCSA_ELF_TypeTLS, MCSA_ELF_TypeIndFunction,
    // MCSA_Local and the Mach-O, XCOFF, COFF and GOFF attributes.
    EXPECT_GE(Unsupported, 20u);
    EXPECT_TRUE(E.Diags.empty());
  }
}

// A common symbol is common (size and alignment on the MCSymbol), external
// data whose size is the common size. Declaring it again the same way is
// fine; a different size or alignment is a clean MCContext error, not a
// fatal error.
TEST_F(MSILMCObjectHooks, CommonSymbol) {
  for (const char *TripleName : BothTriples) {
    SCOPED_TRACE(TripleName);
    MSILMCEnv E(TripleName);
    ASSERT_TRUE(E.ok());
    WriterLog Log;
    MCStreamer &S = E.makeStreamer(recordingBackend(Log));

    MCSymbolMSIL &C = E.symbol("common_c");
    S.emitCommonSymbol(&C, 24, Align(8));
    EXPECT_TRUE(C.isCommon());
    EXPECT_EQ(24u, C.getCommonSize());
    EXPECT_EQ(MaybeAlign(8), C.getCommonAlignment());
    EXPECT_EQ(SymKind::Data, C.getKind());
    EXPECT_TRUE(C.isExternal());
    EXPECT_FALSE(C.isWeak());
    EXPECT_EQ(24, constantValue(C.getSize()));

    S.emitCommonSymbol(&C, 24, Align(8));
    EXPECT_TRUE(E.Diags.empty());
    EXPECT_FALSE(E.Ctx->hadError());

    S.emitCommonSymbol(&C, 32, Align(8));
    ASSERT_EQ(1u, E.Diags.size());
    EXPECT_EQ(SourceMgr::DK_Error, E.Diags[0].Kind);
    EXPECT_EQ("symbol 'common_c' is already declared as a different type",
              E.Diags[0].Message);
    S.emitCommonSymbol(&C, 24, Align(16));
    ASSERT_EQ(2u, E.Diags.size());
    EXPECT_EQ("symbol 'common_c' is already declared as a different type",
              E.Diags[1].Message);
    EXPECT_TRUE(E.Ctx->hadError());
  }
}

// MSIL has no local common symbols: one clean MCContext error.
TEST_F(MSILMCObjectHooks, LocalCommonIsAnError) {
  for (const char *TripleName : BothTriples) {
    SCOPED_TRACE(TripleName);
    MSILMCEnv E(TripleName);
    ASSERT_TRUE(E.ok());
    WriterLog Log;
    MCStreamer &S = E.makeStreamer(recordingBackend(Log));
    MCSymbolMSIL &L = E.symbol("local_common");
    S.emitLocalCommonSymbol(&L, 16, Align(4));
    ASSERT_EQ(1u, E.Diags.size());
    EXPECT_EQ(SourceMgr::DK_Error, E.Diags[0].Kind);
    EXPECT_EQ("local common symbols are not supported for MSIL",
              E.Diags[0].Message);
    EXPECT_TRUE(E.Ctx->hadError());
  }
}

// The MSIL target writer is concrete, has the MSIL format and is found by
// classof (MCAsmBackend::createObjectWriter casts to it).
TEST_F(MSILMCObjectHooks, TargetWriterFormat) {
  std::unique_ptr<MCObjectTargetWriter> Default =
      std::make_unique<MCMSILObjectTargetWriter>();
  EXPECT_EQ(Triple::MSIL, Default->getFormat());
  EXPECT_TRUE(isa<MCMSILObjectTargetWriter>(Default.get()));

  WriterLog Log;
  std::unique_ptr<MCObjectTargetWriter> Recording =
      std::make_unique<RecordingTargetWriter>(Log);
  EXPECT_EQ(Triple::MSIL, Recording->getFormat());
  EXPECT_TRUE(isa<MCMSILObjectTargetWriter>(Recording.get()));

  WasmFormatTargetWriter Other;
  EXPECT_FALSE(isa<MCMSILObjectTargetWriter>(&Other));
}

// MCAsmBackend::createObjectWriter dispatches an MSIL target writer to the
// MSIL object writer, which forwards recordRelocation and writeObject to the
// target writer that M18 and M19 override, with the assembler set on it.
TEST_F(MSILMCObjectHooks, CreateObjectWriterDispatchesToTargetWriter) {
  for (const char *TripleName : BothTriples) {
    SCOPED_TRACE(TripleName);
    MSILMCEnv E(TripleName);
    ASSERT_TRUE(E.ok());
    WriterLog Log;
    E.makeStreamer(recordingBackend(Log));
    MCSymbol *Ext = E.Ctx->getOrCreateSymbol("ext_data");
    E.emitReferenceTo(*Ext);
    E.Streamer->finish();

    EXPECT_TRUE(E.Diags.empty());
    EXPECT_FALSE(E.Ctx->hadError());
    EXPECT_EQ(1u, Log.RelocCalls);
    EXPECT_EQ(Ext, Log.RelocSym);
    EXPECT_EQ(MCFixupKind(FK_Data_8), Log.RelocKind);
    EXPECT_EQ(1u, Log.WriteCalls);
    EXPECT_EQ(&E.assembler(), Log.WriteAsm);
    EXPECT_EQ(&E.assembler(), Log.TargetAsm);
    EXPECT_EQ(RecordedObject, E.Bytes.str());
  }
}

// createMSILObjectWriter called directly: the writer owns the target writer
// and forwards to it.
TEST_F(MSILMCObjectHooks, CreateMSILObjectWriterForwards) {
  MSILMCEnv E(WinTriple);
  ASSERT_TRUE(E.ok());
  WriterLog Log;
  std::unique_ptr<MCObjectWriter> OW = createMSILObjectWriter(
      std::make_unique<RecordingTargetWriter>(Log), E.OS);
  ASSERT_NE(nullptr, OW.get());
  WriterLog BackendLog; // the backend's own writer factory is not used
  E.Streamer.reset(E.TheTarget->createMCObjectStreamer(
      E.TT, *E.Ctx, recordingBackend(BackendLog), std::move(OW),
      std::make_unique<TestCodeEmitter>(), *E.STI));
  ASSERT_NE(nullptr, E.Streamer.get());
  MCSymbol *Ext = E.Ctx->getOrCreateSymbol("ext_fn_address");
  E.emitReferenceTo(*Ext);
  E.Streamer->finish();

  EXPECT_TRUE(E.Diags.empty());
  EXPECT_EQ(1u, Log.RelocCalls);
  EXPECT_EQ(Ext, Log.RelocSym);
  EXPECT_EQ(1u, Log.WriteCalls);
  EXPECT_EQ(&E.assembler(), Log.WriteAsm);
  EXPECT_EQ(&E.assembler(), Log.TargetAsm);
  EXPECT_EQ(RecordedObject, E.Bytes.str());
  EXPECT_EQ(0u, BackendLog.WriteCalls);
}

// Until M19's target writer overrides it, the default writer fails cleanly:
// one MCContext error naming the triple, through the context's diagnostic
// handler, and not a byte of output. Its recordRelocation records nothing,
// so a module with a relocation gets the same single error.
TEST_F(MSILMCObjectHooks, DefaultWriterReportsCleanError) {
  for (const char *TripleName : BothTriples) {
    SCOPED_TRACE(TripleName);
    std::string Expected = std::string("CIL object files are not supported "
                                       "for MSIL target '") +
                           TripleName + "' yet";
    {
      MSILMCEnv E(TripleName);
      ASSERT_TRUE(E.ok());
      E.makeStreamer(defaultWriterBackend());
      E.emitReferenceTo(*E.Ctx->getOrCreateSymbol("ext_data"));
      E.Streamer->finish();
      ASSERT_EQ(1u, E.Diags.size());
      EXPECT_EQ(SourceMgr::DK_Error, E.Diags[0].Kind);
      EXPECT_EQ(Expected, E.Diags[0].Message);
      EXPECT_TRUE(E.Ctx->hadError());
      EXPECT_TRUE(E.Bytes.empty());
    }
    {
      // writeObject itself returns the number of bytes written: 0.
      MSILMCEnv E(TripleName);
      ASSERT_TRUE(E.ok());
      E.makeStreamer(defaultWriterBackend());
      EXPECT_EQ(0u, E.assembler().getWriter().writeObject());
      ASSERT_EQ(1u, E.Diags.size());
      EXPECT_EQ(SourceMgr::DK_Error, E.Diags[0].Kind);
      EXPECT_EQ(Expected, E.Diags[0].Message);
      EXPECT_TRUE(E.Bytes.empty());
    }
  }
}

} // namespace
