//===-- MSILTargetObjectStreamer.cpp - MSIL object target streamer --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the MSIL target streamer of object files: it passes the
/// object events of the AsmPrinter (MSILTargetStreamer.h; plan 5.3 as amended
/// for M19) to MSILObjectWriter, in their order. Each method gets a section
/// of its own, so that its branches resolve inside it and the writer finds
/// its code. Comments and blank lines are text only; the whole-program
/// directives never occur in an object.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MCTargetDesc/MSILObjectWriter.h"
#include "MCTargetDesc/MSILTargetStreamer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSectionMSIL.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>
#include <string>

using namespace llvm;

namespace {

/// The object implementation of MSILTargetStreamer.
class MSILTargetObjectStreamer final : public MSILTargetStreamer {
  /// The method between emitMethodBegin and emitMethodEnd, with copies of its
  /// name and signature.
  struct PendingMethod {
    MCSection *Section = nullptr;
    std::string Name;
    MSIL::Signature Sig;
    bool Public = true;
    unsigned MaxStack = 0;
    SmallVector<cli::TypeSig, 8> Locals;
  };
  std::optional<PendingMethod> Pending;
  /// The number in the name of the next method section, ".text.<n>".
  unsigned NextSection = 0;

  /// The writer of this object (MSIL::getObjectTargetWriter): the target
  /// writer that the streamer's asm backend created for its object writer.
  MSILObjectWriter &getWriter();
  /// Reports a call that only whole-program text makes (an internal error).
  [[noreturn]] static void reportWholeProgram(StringRef Directive);
  PendingMethod &getPending(StringRef Directive);

public:
  explicit MSILTargetObjectStreamer(MCStreamer &S) : MSILTargetStreamer(S) {}

  void emitFileHeader(bool, StringRef, StringRef) override {
    reportWholeProgram("the module header");
  }
  void emitAssemblyRefs() override {
    reportWholeProgram("the assembly references");
  }
  void emitAssembly(StringRef) override { reportWholeProgram("the assembly"); }
  void emitMethodBegin(const MSILMethodDecl &M) override;
  void emitEntryPoint() override { reportWholeProgram("the entry point"); }
  void emitMaxStack(unsigned N) override {
    getPending(".maxstack").MaxStack = N;
  }
  void emitLocals(ArrayRef<cli::TypeSig> Locals) override {
    getPending(".locals").Locals.assign(Locals.begin(), Locals.end());
  }
  void emitMethodEnd(StringRef Name) override;
  void emitPInvoke(const MSILMethodDecl &M, StringRef Lib,
                   StringRef Entry) override {
    getWriter().addPInvokeStub(M, Lib, Entry);
  }
  void emitDataImage(StringRef, StringRef, StringRef,
                     ArrayRef<uint8_t>) override {
    reportWholeProgram("the data image");
  }
  void emitAddressField(StringRef Name, bool Public, bool Generated) override {
    // The compiler-generated field of a data segment is whole-program output
    // (decision D12); an object has the fields of its globals only.
    if (Generated)
      reportWholeProgram("a generated address field");
    getWriter().addAddressField(Name, Public);
  }
  void emitComment(const Twine &) override {}
  void emitBlankLine() override {}
  void emitObjectHeader(bool BOM, StringRef SourceFile,
                        StringRef TripleStr) override {
    // The byte order mark is a matter of text: object metadata is UTF-8.
    getWriter().setModule(SourceFile, Triple(TripleStr));
  }
  void emitLinkInfo(StringRef ModuleName, const MSILLink::Record &R) override {
    getWriter().setLinkInfo(ModuleName, R);
  }
};

} // end anonymous namespace

MSILObjectWriter &MSILTargetObjectStreamer::getWriter() {
  auto &S = static_cast<MCObjectStreamer &>(getStreamer());
  MCMSILObjectTargetWriter *W =
      MSIL::getObjectTargetWriter(S.getAssembler().getBackend());
  if (!W)
    reportFatalInternalError(
        "MSIL: the object streamer's asm backend has no CIL object writer");
  // createMSILObjectTargetWriter makes only MSILObjectWriters.
  return static_cast<MSILObjectWriter &>(*W);
}

void MSILTargetObjectStreamer::reportWholeProgram(StringRef Directive) {
  reportFatalInternalError("MSIL: " + Directive +
                           " is whole-program output, which a CIL object does "
                           "not contain");
}

MSILTargetObjectStreamer::PendingMethod &
MSILTargetObjectStreamer::getPending(StringRef Directive) {
  if (!Pending)
    reportFatalInternalError("MSIL: " + Directive + " outside a method");
  return *Pending;
}

void MSILTargetObjectStreamer::emitMethodBegin(const MSILMethodDecl &M) {
  // An object has the methods of its functions only: the module initializer
  // and the entry point are msil-ld's (plan 5.8.2, O5).
  if (M.Kind != MSILMethodKind::Function)
    reportWholeProgram(M.Kind == MSILMethodKind::ModuleInitializer
                           ? "the module initializer"
                           : "the entry point method");
  if (Pending)
    reportFatalInternalError("MSIL: method '" + M.Name +
                             "' begins inside method '" + Pending->Name + "'");
  assert(M.Sig && "a method needs a signature");
  // A section of its own that nothing else uses, so that the method's
  // branches resolve inside it (MSILAsmBackend) and its content is exactly
  // the method's code.
  MCContext &Ctx = getContext();
  MCSectionMSIL *Section;
  do {
    SmallString<16> Name;
    (".text." + Twine(NextSection++)).toVector(Name);
    Section = Ctx.getMSILSection(Name, SectionKind::getText());
  } while (Section->isRegistered());
  getStreamer().switchSection(Section);
  Pending.emplace();
  Pending->Section = Section;
  Pending->Name = M.Name.str();
  Pending->Sig = *M.Sig;
  Pending->Public = M.Public;
}

void MSILTargetObjectStreamer::emitMethodEnd(StringRef Name) {
  PendingMethod &P = getPending("the end of a method");
  assert(Name == P.Name && "a method ends under another name");
  MSILMethodDecl Decl;
  Decl.Name = P.Name;
  Decl.Sig = &P.Sig;
  Decl.Public = P.Public;
  getWriter().addMethod(*P.Section, Decl, P.MaxStack, P.Locals);
  Pending.reset();
}

MCTargetStreamer *
llvm::createMSILObjectTargetStreamer(MCStreamer &S,
                                     const MCSubtargetInfo & /*STI*/) {
  return new MSILTargetObjectStreamer(S);
}
