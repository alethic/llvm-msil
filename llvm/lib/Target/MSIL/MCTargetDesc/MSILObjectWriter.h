//===-- MSILObjectWriter.h - MSIL CIL object writer -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares MSILObjectWriter, the target object writer that writes
/// CIL objects (llvm/docs/MSIL/ImplementationPlan.md section 5.8): an
/// ECMA-335 netmodule with the placeholder rows of plan 5.8.3 in row order
/// R6, the link-info record in the custom attribute of the Module row, and
/// the COFF symbol view.
///
/// It gets its input from two sides:
/// - the object target streamer (MSILTargetObjectStreamer.cpp) passes on the
///   events of the AsmPrinter in their order: the module, the methods (each
///   in its own section), the address fields, the import stubs and the
///   link-info record;
/// - the assembler passes every token fixup to recordRelocation
///   (MSILAsmBackend::applyFixup), with the MSILMemberRefExpr that names the
///   token's entity.
///
/// writeObject numbers the rows, patches the token of each fixup into its
/// method's code, checks every token of the link-info record against its own
/// rows, and writes the PE image (cli::writePEImage).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILOBJECTWRITER_H
#define LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILOBJECTWRITER_H

#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/Signature.h"
#include "llvm/MC/MCMSILObjectWriter.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class MCAssembler;
class MCFixup;
class MCFragment;
class MCSection;
class MCValue;
class raw_pwrite_stream;
struct MSILMethodDecl;

/// The writer of CIL objects. createMSILObjectTargetWriter makes it, and the
/// object target streamer reaches it through MSIL::getObjectTargetWriter.
/// Every string and signature it is given is copied.
class MSILObjectWriter final : public MCMSILObjectTargetWriter {
  /// The entity that a token fixup names, copied from its MSILMemberRefExpr.
  struct TokenOperand {
    MSILMemberRefExpr::Kind Kind = MSILMemberRefExpr::Method;
    /// The IL name (Method, Field, ExternMethod, ExternField, FnAddr).
    std::string Name;
    /// The signature (Method, ExternMethod, StandAloneSig).
    std::optional<MSIL::Signature> Sig;
    /// The BCL member (BCLMember).
    cli::BCLMemberId BCL = cli::BCLMemberId();
  };
  /// A method definition: emitMethodBegin to emitMethodEnd.
  struct MethodRecord {
    /// The section that holds the method's code, and nothing else.
    const MCSection *Section;
    std::string Name;
    MSIL::Signature Sig;
    bool Public;
    unsigned MaxStack;
    std::vector<cli::TypeSig> Locals;
  };
  /// An address field (emitAddressField).
  struct FieldRecord {
    std::string Name;
    bool Public;
  };
  /// An import stub (emitPInvoke).
  struct StubRecord {
    std::string Name;
    MSIL::Signature Sig;
    bool Public;
    std::string Module;
    std::string Entry;
  };

  /// Set by setModule: the target, which gives the PE machine.
  bool HasModule = false;
  Triple TT;
  /// The events, in their order.
  std::vector<MethodRecord> Methods;
  std::vector<FieldRecord> Fields;
  std::vector<StubRecord> Stubs;
  /// Set by setLinkInfo.
  std::string ModuleName;
  std::optional<MSILLink::Record> LinkInfo;
  /// The token fixups of each method section, by offset in the section.
  DenseMap<const MCSection *, std::map<uint64_t, TokenOperand>> TokenSites;

  /// Clears everything above, so that the next object starts empty. The
  /// writer outlives its objects (llc -compile-twice runs the AsmPrinter
  /// twice on one streamer, and MCAssembler::reset does not reach a target
  /// writer), while MCContext::reset destroys the sections that Methods and
  /// TokenSites point to: setModule and writeObject call it.
  void resetObjectState();

public:
  MSILObjectWriter();
  ~MSILObjectWriter() override;

  /// Starts the object of the module whose source file is \p SourceFile,
  /// for the target \p TT (emitObjectHeader). The name of the Module row,
  /// the base name of \p SourceFile (rule O3), comes with setLinkInfo.
  void setModule(StringRef SourceFile, const Triple &TT);
  /// Adds the method definition \p M, whose code is the content of \p Sec,
  /// with its .maxstack and its locals (emitMethodBegin to emitMethodEnd).
  /// Only functions (MSILMethodKind::Function) have methods in an object.
  void addMethod(MCSection &Sec, const MSILMethodDecl &M, unsigned MaxStack,
                 ArrayRef<cli::TypeSig> Locals);
  /// Adds the address field \p Name of a global variable that the object
  /// defines (emitAddressField): 'public' if \p Public, else 'assembly'.
  void addAddressField(StringRef Name, bool Public);
  /// Adds the import stub \p M, 'pinvokeimpl("<Module>" as "<Entry>"
  /// nomangle cdecl)' (emitPInvoke).
  void addPInvokeStub(const MSILMethodDecl &M, StringRef Module,
                      StringRef Entry);
  /// Sets the name of the Module row and the link-info record (emitLinkInfo),
  /// the last event.
  void setLinkInfo(StringRef ModuleName, const MSILLink::Record &R);

  /// Keeps the entity of a token fixup (the MSILMemberRefExpr of the fixup's
  /// value) and its place, for writeObject. FixedValue stays 0: the token is
  /// known only when every row is numbered.
  void recordRelocation(const MCFragment &F, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override;
  /// Writes the CIL object and returns its size, or reports an error through
  /// the MCContext and writes nothing.
  uint64_t writeObject(MCAssembler &Asm, raw_pwrite_stream &OS) override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILOBJECTWRITER_H
