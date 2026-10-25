//===-- MSILMCExpr.h - MSIL member reference expressions --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSILMemberRefExpr is the token operand of every CIL instruction that names
/// a metadata entity: the method of a 'call' or 'ldftn', the field of an
/// 'ldsfld', 'stsfld' or 'ldsflda' (the address field of a global or of a
/// function), the stand-alone signature of a 'calli', and BCL members. Its
/// kind is the operand class of plan 5.8.3, which depends on the IR alone
/// (MSILMCInstLower decides it), and which the object writer reads from the
/// fixup's value.
///
/// It prints in two forms (plan M18.a (4)): whole-program form, the text of
/// -S, llc and msil-ld's --lto-emit-asm, where every class prints as in
/// Phase 1; and object form (the InstPrinter option object-form), where the
/// classes that a CIL object writes as placeholder MemberRefs name their
/// placeholder parents (plan 5.8.3, 5.8.4).
///
/// It is relocatable: its value is a symbol (an MCSymbolMSIL), so every token
/// fixup reaches the assembler backend.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCEXPR_H
#define LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCEXPR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/MC/MCExpr.h"
#include <cstdint>

namespace llvm {

class MCContext;
class MCSymbol;

namespace MSIL {
struct Signature;

/// The prefix of the address field of a function in whole-program output:
/// '<msil>fn_<IL name>' holds the function's pointer value (plan section 12,
/// 'Phase-1 function addresses').
inline constexpr StringLiteral FnAddrFieldPrefix = "<msil>fn_";
} // end namespace MSIL

/// A CIL member reference or call-site signature. MCExprs are allocated in the
/// MCContext and never destroyed, so every member is trivially destructible;
/// the names are copied into the MCContext and the signatures must outlive
/// the expression (MSILAsmPrinter owns them).
class MSILMemberRefExpr final : public MCTargetExpr {
public:
  enum Kind : uint8_t {
    /// A MethodDef of this module: a function defined here, or the import
    /// stub of a declaration with "msil-import-module" or "msil-import-name"
    /// (plan 5.8.3 R7). Prints "<ret> '<name>'(<params>)" (call, ldftn).
    Method,
    /// A FieldDef of this module: the address field of a global defined here
    /// ("native int '<name>'"), or a field of a compiler-generated value type
    /// ("valuetype '<type>' '<name>'", the data image of decision D12).
    Field,
    /// Any other function (a declaration, available_externally, extern_weak,
    /// an alias) and library calls, with the call-site signature. Whole-program
    /// form as Method; object form "<ret> [msil.extern]'<msil>Symbols'::
    /// '<name>'(<params>)".
    ExternMethod,
    /// A global variable declaration or alias. Whole-program form as Field;
    /// object form "native int [msil.extern]'<msil>Symbols'::'<name>'".
    ExternField,
    /// The address field of the function '<name>' (FUNC_ADDR; plan 5.8.3
    /// R2). Whole-program form "native int '<msil>fn_<name>'"; object form
    /// "native int [msil.extern]'<msil>FnAddr'::'<name>'".
    FnAddr,
    /// A call-site signature (calli): "[unmanaged cdecl ]<ret>(<params>)".
    StandAloneSig,
    /// A member of llvm/CLI/BCLMembers.def: cli::printBCLMemberRef in scope
    /// [mscorlib] (whole-program form) or [msil.bcl] (object form).
    BCLMember,
  };

private:
  const Kind K;
  /// The IL name (Method, Field, ExternMethod, ExternField, FnAddr).
  const StringRef Name;
  /// The value type of a Field that is not a native int address field.
  const StringRef FieldType;
  const MSIL::Signature *const Sig;
  const cli::BCLMemberId BCL;
  /// The symbol the expression evaluates to.
  MCSymbol *const Sym;

  MSILMemberRefExpr(Kind K, StringRef Name, StringRef FieldType,
                    const MSIL::Signature *Sig, cli::BCLMemberId BCL,
                    MCSymbol *Sym)
      : K(K), Name(Name), FieldType(FieldType), Sig(Sig), BCL(BCL), Sym(Sym) {}

  static const MSILMemberRefExpr *create(Kind K, StringRef Name,
                                         StringRef FieldType,
                                         const MSIL::Signature *S,
                                         cli::BCLMemberId BCL, MCContext &Ctx);

public:
  /// The method \p ILName (unquoted) of this module, with signature \p S.
  static const MSILMemberRefExpr *
  createMethod(StringRef ILName, const MSIL::Signature *S, MCContext &Ctx);
  /// The function \p ILName that this module does not define, called with
  /// signature \p S.
  static const MSILMemberRefExpr *createExternMethod(StringRef ILName,
                                                     const MSIL::Signature *S,
                                                     MCContext &Ctx);
  /// The native int address field \p ILName of this module.
  static const MSILMemberRefExpr *createField(StringRef ILName, MCContext &Ctx);
  /// The field \p ILName of this module whose type is the value type
  /// \p TypeName of this module (the data image).
  static const MSILMemberRefExpr *
  createValueTypeField(StringRef ILName, StringRef TypeName, MCContext &Ctx);
  /// The address field of the global \p ILName that this module does not
  /// define.
  static const MSILMemberRefExpr *createExternField(StringRef ILName,
                                                    MCContext &Ctx);
  /// The address field of the function \p ILName.
  static const MSILMemberRefExpr *createFnAddr(StringRef ILName,
                                               MCContext &Ctx);
  /// The stand-alone signature \p S of a 'calli'.
  static const MSILMemberRefExpr *createSig(const MSIL::Signature *S,
                                            MCContext &Ctx);
  /// The BCL member \p Id.
  static const MSILMemberRefExpr *createBCL(cli::BCLMemberId Id,
                                            MCContext &Ctx);

  /// The kind of member reference (hides MCExpr::getKind, which is always
  /// MCExpr::Target for this class).
  Kind getKind() const { return K; }
  /// The IL name; empty for StandAloneSig and BCLMember. For FnAddr, the name
  /// of the function whose address field it is.
  StringRef getName() const { return Name; }
  /// The value type of a Field that is not an address field; empty
  /// otherwise.
  StringRef getFieldTypeName() const { return FieldType; }
  /// The signature (Method, ExternMethod, StandAloneSig); null otherwise.
  const MSIL::Signature *getSignature() const { return Sig; }
  /// The BCL member of a BCLMember.
  cli::BCLMemberId getBCLMember() const { return BCL; }
  /// The symbol of the reference: the symbol of the IL name for the named
  /// kinds (for FnAddr, the function's), and a temporary symbol for
  /// StandAloneSig and BCLMember. In an MSIL MCContext it is an MCSymbolMSIL
  /// whose kind (Method, Data, Signature or ExternalMember) this expression
  /// set if it was Unknown; a BCLMember's has ExternalMemberId BCLMemberId + 1.
  MCSymbol *getSymbol() const { return Sym; }

  /// Prints the operand text: whole-program form, or object form if
  /// \p ObjectForm.
  void printOperand(raw_ostream &OS, bool ObjectForm) const;

  /// The whole-program form (MCExpr printing, so also the fixups of
  /// -show-mc-encoding).
  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res,
                                 const MCAssembler *Asm) const override;
  bool isEqualTo(const MCExpr *X) const override;
  void visitUsedExpr(MCStreamer &Streamer) const override;
  MCFragment *findAssociatedFragment() const override;

  // MSILMemberRefExpr is the only MCTargetExpr of the MSIL target.
  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCEXPR_H
