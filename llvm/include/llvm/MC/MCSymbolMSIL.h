//===- MCSymbolMSIL.h - MSIL (.NET CIL) Machine Code Symbols ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares MCSymbolMSIL, the symbol that MCContext creates for MSIL
// triples. Like MCSymbolWasm, it carries what the object writer needs about a
// symbol beyond its value: what it names (a method, a data symbol, a function
// address field, a stand-alone signature or an external member), its binding
// and visibility, its signature and its import attributes.
//
// lib/MC only stores these fields. MCMSILStreamer sets the ones that generic
// symbol attributes describe. The MSIL target sets the others from its MC
// layer and AsmPrinter, and its object writer reads them (milestones M18 and
// M19 of llvm/docs/MSIL/ImplementationPlan.md).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSYMBOLMSIL_H
#define LLVM_MC_MCSYMBOLMSIL_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolTableEntry.h"
#include <cstdint>
#include <optional>

// The method signature type of the ECMA-335 library (llvm/CLI). LLVMCLI
// depends on LLVMMC, so lib/MC only names it.
namespace llvm::cli {
struct MethodSig;
} // namespace llvm::cli

namespace llvm {

class MCExpr;

/// A symbol of an MSIL object. MCContext allocates symbols without running
/// their destructors, so this class must stay trivially destructible: the
/// strings that the import attributes and the signature point to are owned by
/// the MSIL target streamer, which outlives the assembler.
class MCSymbolMSIL : public MCSymbol {
public:
  /// What the symbol names.
  enum class Kind : uint8_t {
    /// Not classified yet (a symbol that only a reference created).
    Unknown,
    /// A C function: a defined method (it has a method section) or an
    /// undefined function.
    Method,
    /// A C global variable: its address field; size and alignment are on the
    /// symbol.
    Data,
    /// The address field '<msil>fn_<f>' of the function getFuncTarget(): it
    /// holds the function's pointer value, and the linker owns it.
    FuncAddrField,
    /// A temporary symbol that carries the stand-alone signature of a 'calli'
    /// (wasm's type index).
    Signature,
    /// A type or member of the base class library, bound by the linker and
    /// identified by getExternalMemberId().
    ExternalMember
  };

private:
  Kind SymKind = Kind::Unknown;
  bool IsWeak = false;
  bool IsNoStrip = false;
  MSILLink::Visibility Vis = MSILLink::Visibility::Default;
  const cli::MethodSig *Sig = nullptr;
  std::optional<StringRef> ImportModule;
  std::optional<StringRef> ImportName;
  const MCSymbolMSIL *FuncTarget = nullptr;
  uint32_t ExternalMemberId = 0;

  /// An expression describing how to calculate the size of a symbol. If a
  /// symbol has no size this field will be NULL.
  const MCExpr *SymbolSize = nullptr;

public:
  MCSymbolMSIL(const MCSymbolTableEntry *Name, bool IsTemporary)
      : MCSymbol(Name, IsTemporary) {}

  Kind getKind() const { return SymKind; }
  void setKind(Kind K) { SymKind = K; }

  /// Visible outside the object (a global or weak binding).
  bool isExternal() const { return IsExternal; }
  void setExternal(bool Value) const { IsExternal = Value; }

  /// Weak binding (also external).
  bool isWeak() const { return IsWeak; }
  void setWeak(bool Value = true) { IsWeak = Value; }

  /// The ELF-style visibility of the link-info record.
  MSILLink::Visibility getVisibility() const { return Vis; }
  void setVisibility(MSILLink::Visibility V) { Vis = V; }

  /// The symbol is in llvm.used: the linker keeps it.
  bool isNoStrip() const { return IsNoStrip; }
  void setNoStrip(bool Value = true) { IsNoStrip = Value; }

  /// The signature of a Method or a Signature symbol, or null.
  const cli::MethodSig *getSignature() const { return Sig; }
  void setSignature(const cli::MethodSig *S) { Sig = S; }

  /// The IR attribute "msil-import-module", if any.
  std::optional<StringRef> getImportModule() const { return ImportModule; }
  void setImportModule(std::optional<StringRef> Name) { ImportModule = Name; }

  /// The IR attribute "msil-import-name", if any.
  std::optional<StringRef> getImportName() const { return ImportName; }
  void setImportName(std::optional<StringRef> Name) { ImportName = Name; }

  /// The function whose address a FuncAddrField symbol holds, or null.
  const MCSymbolMSIL *getFuncTarget() const { return FuncTarget; }
  void setFuncTarget(const MCSymbolMSIL *F) { FuncTarget = F; }

  /// The base class library entity of an ExternalMember symbol; 0 is none.
  uint32_t getExternalMemberId() const { return ExternalMemberId; }
  void setExternalMemberId(uint32_t Id) { ExternalMemberId = Id; }

  /// The size of a Data symbol, or null.
  const MCExpr *getSize() const { return SymbolSize; }
  void setSize(const MCExpr *SS) { SymbolSize = SS; }
};

} // end namespace llvm

#endif // LLVM_MC_MCSYMBOLMSIL_H
