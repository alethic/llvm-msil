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
/// function), the stand-alone signature of a 'calli', BCL members, and the
/// member and types that a managed-call stub names (plan 5.20.5 ML3), with
/// the type specifications and strings of its run-time checks, which only
/// whole-program text has (plan 5.20.6 MW2 as amended). Its kind is the
/// operand class of plan 5.8.3, which depends on the IR alone
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
#include <optional>
#include <string>

namespace llvm {

class MCContext;
class MCSymbol;

namespace cli {
struct ManagedSignature;
struct ManagedType;
} // end namespace cli

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
    /// The member that a managed-call stub accesses (plan 5.20.5 ML3): the
    /// method, constructor or field named by the declaration's
    /// "msil-managed-type", "msil-managed-member" and "msil-managed-sig"
    /// (call, callvirt, newobj, ldsfld, ldfld). Whole-program form names it
    /// in the declaration's text scope ("msil-managed-asm", default
    /// mscorlib), object form in [msil.bcl], where msil-ld binds it by name
    /// and full signature.
    ManagedMember,
    /// A type that a managed-call stub names (plan 5.20.5 ML3): a TypeRef,
    /// or an SZARRAY TypeSpec, for castclass, isinst, box, unbox, unbox.any,
    /// newarr and constrained., and for the ldtoken and sizeof of the enum
    /// checks of whole-program text; scoped as ManagedMember.
    ManagedType,
    /// The class or value type of a managed call as a TypeSpec of the kind
    /// that the declaration gives it, "class [<scope>]T" or "valuetype
    /// [<scope>]V": whole-program text names TYPE so in the castclass of a
    /// 'this-obj' handle, the unbox of a 'this-val' handle and a
    /// constructor's castclass (a class) or box (a value type), and the
    /// runtime checks that kind against the type's definition when it
    /// resolves the token (plan 5.20.6 MW2 as amended: T-M16 at run time).
    /// Objects name TYPE's TypeRef (ManagedType), whose kind msil-ld checks
    /// (MK3); the object writer refuses this kind.
    ManagedTypeSpec,
    /// A string literal of whole-program text (ldstr): the message of a
    /// managed-call stub's failed run-time enum check (plan 5.20.6 MW2 as
    /// amended: T-M7 at run time), printed as MSIL::printQuotedString
    /// prints it. A CIL object has no user strings (plan 5.8), so the object
    /// writer refuses this kind.
    UserString,
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
  /// ManagedMember: the type that declares the member; ManagedType: the type.
  const cli::ManagedType *const MType = nullptr;
  /// ManagedMember: the member's own signature.
  const cli::ManagedSignature *const MSig = nullptr;
  /// ManagedMember and ManagedType: the scope of whole-program form.
  const StringRef Scope = StringRef();

  MSILMemberRefExpr(Kind K, StringRef Name, StringRef FieldType,
                    const MSIL::Signature *Sig, cli::BCLMemberId BCL,
                    MCSymbol *Sym)
      : K(K), Name(Name), FieldType(FieldType), Sig(Sig), BCL(BCL), Sym(Sym) {}
  MSILMemberRefExpr(Kind K, StringRef Name, const cli::ManagedType *MType,
                    const cli::ManagedSignature *MSig, StringRef Scope,
                    MCSymbol *Sym)
      : K(K), Name(Name), Sig(nullptr), BCL(), Sym(Sym), MType(MType),
        MSig(MSig), Scope(Scope) {}

  static const MSILMemberRefExpr *create(Kind K, StringRef Name,
                                         StringRef FieldType,
                                         const MSIL::Signature *S,
                                         cli::BCLMemberId BCL, MCContext &Ctx);
  static const MSILMemberRefExpr *createManaged(Kind K, StringRef Member,
                                                const cli::ManagedType *Type,
                                                const cli::ManagedSignature *S,
                                                StringRef Scope,
                                                MCContext &Ctx);

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
  /// The member \p Member (".ctor" for a constructor) of the type \p Owner,
  /// with the member's own signature \p S, printed in assembly \p Scope in
  /// whole-program form (plan 5.20.2 MA4).
  static const MSILMemberRefExpr *
  createManagedMember(const cli::ManagedType *Owner, StringRef Member,
                      const cli::ManagedSignature *S, StringRef Scope,
                      MCContext &Ctx);
  /// The type \p Type (a class, a value type or an SZARRAY), printed in
  /// assembly \p Scope in whole-program form.
  static const MSILMemberRefExpr *
  createManagedType(const cli::ManagedType *Type, StringRef Scope,
                    MCContext &Ctx);
  /// The class or value type \p Type as a TypeSpec of its kind, printed in
  /// assembly \p Scope (whole-program text only).
  static const MSILMemberRefExpr *
  createManagedTypeSpec(const cli::ManagedType *Type, StringRef Scope,
                        MCContext &Ctx);
  /// The string literal \p Text (whole-program text only).
  static const MSILMemberRefExpr *createUserString(StringRef Text,
                                                   MCContext &Ctx);

  /// The kind of member reference (hides MCExpr::getKind, which is always
  /// MCExpr::Target for this class).
  Kind getKind() const { return K; }
  /// The IL name; empty for StandAloneSig, BCLMember, ManagedType and
  /// ManagedTypeSpec. For FnAddr, the name of the function whose address
  /// field it is; for ManagedMember, the member's name; for UserString, the
  /// string.
  StringRef getName() const { return Name; }
  /// The value type of a Field that is not an address field; empty
  /// otherwise.
  StringRef getFieldTypeName() const { return FieldType; }
  /// The signature (Method, ExternMethod, StandAloneSig); null otherwise.
  const MSIL::Signature *getSignature() const { return Sig; }
  /// The BCL member of a BCLMember.
  cli::BCLMemberId getBCLMember() const { return BCL; }
  /// The type that declares a ManagedMember, or the type of a ManagedType or
  /// ManagedTypeSpec; null otherwise.
  const cli::ManagedType *getManagedType() const { return MType; }
  /// The member's own signature (ManagedMember); null otherwise.
  const cli::ManagedSignature *getManagedSignature() const { return MSig; }
  /// The scope of whole-program form (ManagedMember, ManagedType,
  /// ManagedTypeSpec); empty otherwise.
  StringRef getScope() const { return Scope; }
  /// The symbol of the reference: the symbol of the IL name for the named
  /// kinds (for FnAddr, the function's), and a temporary symbol for
  /// StandAloneSig, BCLMember, ManagedMember, ManagedType, ManagedTypeSpec
  /// and UserString. In an MSIL MCContext it is an MCSymbolMSIL whose kind
  /// (Method, Data, Signature or ExternalMember) this expression set if it
  /// was Unknown, except a UserString's, which stays Unknown; a BCLMember's
  /// has ExternalMemberId BCLMemberId + 1.
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

namespace MSIL {
/// The full name of the class by which a token names the managed-call type
/// \p T (plan 5.20.5 ML3): a class's or value type's own name, a primitive's
/// class in the core library ("System.Byte" for uint8, "System.String" for
/// string); empty for a pointer or array type, which a TypeSpec names.
StringRef getManagedTypeRefName(const cli::ManagedType &T);

/// Whether \p FullName names one of the value types that a signature spells
/// in one byte (ECMA-335 II.23.1.16): System.Boolean, Char, SByte, Byte,
/// Int16, UInt16, Int32, UInt32, Int64, UInt64, Single, Double, IntPtr and
/// UIntPtr. 'valuetype' and their TypeRef would be the long form, which
/// PEVerify /md refuses (0x80131298).
bool isOneByteValueType(StringRef FullName);

/// Whether an object names the managed-call member of \p Owner with the
/// signature \p Sig on a TypeSpec parent, ELEMENT_TYPE_VALUETYPE and the
/// [msil.bcl] TypeRef of \p Owner, rather than on that TypeRef (MK3): a
/// static method or static field of a TYPE written 'valuetype T', T neither
/// a value type that a signature spells in one byte (System.Boolean, Char,
/// SByte, Byte, Int16, UInt16, Int32, UInt32, Int64, UInt64, Single, Double,
/// IntPtr and UIntPtr) nor GCHandle. msil-ld reads from the parent that the
/// declaration said 'valuetype'. Instance members, constructors, images and
/// whole-program text keep the TypeRef.
bool hasValueTypeSpecParent(const cli::ManagedType &Owner,
                            const cli::ManagedSignature &Sig);

/// The identity of the MemberRef row that \p Ref names when it is a BCL
/// method or a managed member: "<owner>::<member> <signature>", the same for
/// a BCL method and a managed member that name one member with one
/// signature, whose rows MetadataBuilder merges (rule R6), and "valuetype
/// <owner>::<member> <signature>" for a managed member whose parent is a
/// TypeSpec (hasValueTypeSpecParent), which is a row of its own, as
/// MetadataBuilder keys rows by parent, name and signature. std::nullopt for
/// every other kind, and for a BCL method whose BCLMembers.def text
/// cli::parseManagedSigType does not accept.
std::optional<std::string> getMemberRefIdentity(const MSILMemberRefExpr &Ref);
} // end namespace MSIL

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCEXPR_H
