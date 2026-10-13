//===-- MSILMCExpr.h - MSIL member reference expressions --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSILMemberRefExpr is the operand of every CIL instruction that names a
/// metadata token: the method of a 'call' or 'ldftn', the address field of a
/// global ('ldsfld'), the stand-alone signature of a 'calli', and BCL members.
/// MSILMCInstLower creates it with the signature from MSILAsmPrinter's table,
/// and MSILInstPrinter prints it through MCAsmInfo::printExpr, so the printer
/// needs no side channel.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCEXPR_H
#define LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCEXPR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCExpr.h"
#include <cstdint>

namespace llvm {

class MCContext;

namespace MSIL {
struct Signature;
} // end namespace MSIL

/// A CIL member reference or call-site signature. MCExprs are allocated in the
/// MCContext and never destroyed, so every member is trivially destructible;
/// the names are copied into the MCContext and the signatures must outlive
/// the expression (MSILAsmPrinter owns them).
class MSILMemberRefExpr final : public MCTargetExpr {
public:
  enum Kind : uint8_t {
    /// A method: prints "<ret> '<name>'(<params>)" (call, ldftn).
    Method,
    /// A global's address field: prints "native int '<name>'" (ldsfld). The
    /// offset is printed by the GLOBAL_ADDR case of MSILInstPrinter.
    Field,
    /// A call-site signature: prints "<ret>(<params>)" (calli), with an
    /// 'unmanaged cdecl' or 'unmanaged stdcall' prefix for unmanaged calls.
    StandAloneSig,
    /// A BCL member, printed verbatim, for example
    /// "void [mscorlib]System.Threading.Interlocked::MemoryBarrier()".
    BCLMember,
  };

private:
  const Kind K;
  const StringRef Name;
  const MSIL::Signature *const Sig;
  const int64_t Offset;

  MSILMemberRefExpr(Kind K, StringRef Name, const MSIL::Signature *Sig,
                    int64_t Offset)
      : K(K), Name(Name), Sig(Sig), Offset(Offset) {}

public:
  /// A method named \p ILName (the IR name, unquoted) with signature \p S.
  static const MSILMemberRefExpr *
  createMethod(StringRef ILName, const MSIL::Signature *S, MCContext &Ctx);
  /// The address field of the global \p ILName, plus a byte offset.
  static const MSILMemberRefExpr *createField(StringRef ILName, int64_t Offset,
                                              MCContext &Ctx);
  /// The stand-alone signature \p S of a 'calli'.
  static const MSILMemberRefExpr *createSig(const MSIL::Signature *S,
                                            MCContext &Ctx);
  /// A BCL member reference, \p Text printed verbatim.
  static const MSILMemberRefExpr *createBCL(StringRef Text, MCContext &Ctx);

  /// The kind of member reference (hides MCExpr::getKind, which is always
  /// MCExpr::Target for this class).
  Kind getKind() const { return K; }
  /// The IL name (Method, Field) or the text (BCLMember); empty for
  /// StandAloneSig.
  StringRef getName() const { return Name; }
  /// The signature (Method, StandAloneSig); null otherwise.
  const MSIL::Signature *getSignature() const { return Sig; }
  /// The byte offset added to a Field's address; 0 otherwise.
  int64_t getOffset() const { return Offset; }

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res,
                                 const MCAssembler *Asm) const override {
    return false;
  }
  bool isEqualTo(const MCExpr *X) const override;
  void visitUsedExpr(MCStreamer &Streamer) const override {}
  MCFragment *findAssociatedFragment() const override { return nullptr; }

  // MSILMemberRefExpr is the only MCTargetExpr of the MSIL target.
  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCEXPR_H
