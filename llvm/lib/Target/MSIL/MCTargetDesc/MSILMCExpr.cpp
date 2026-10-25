//===-- MSILMCExpr.cpp - MSIL member reference expressions ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Creation, printing and evaluation of MSILMemberRefExpr.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolMSIL.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

/// The symbol kind of the references of kind \p K (plan M18.a (5)).
static MCSymbolMSIL::Kind getSymbolKind(MSILMemberRefExpr::Kind K) {
  switch (K) {
  case MSILMemberRefExpr::Method:
  case MSILMemberRefExpr::ExternMethod:
  case MSILMemberRefExpr::FnAddr:
    return MCSymbolMSIL::Kind::Method;
  case MSILMemberRefExpr::Field:
  case MSILMemberRefExpr::ExternField:
    return MCSymbolMSIL::Kind::Data;
  case MSILMemberRefExpr::StandAloneSig:
    return MCSymbolMSIL::Kind::Signature;
  case MSILMemberRefExpr::BCLMember:
    return MCSymbolMSIL::Kind::ExternalMember;
  }
  llvm_unreachable("MSIL: unknown member reference kind");
}

const MSILMemberRefExpr *MSILMemberRefExpr::create(Kind K, StringRef Name,
                                                   StringRef FieldType,
                                                   const MSIL::Signature *S,
                                                   cli::BCLMemberId BCL,
                                                   MCContext &Ctx) {
  // The named kinds evaluate to the symbol of the IL name, which the object
  // writer's symbol table uses (a function's address field names the
  // function, plan 5.8.3 R2); a signature or a BCL member has no C symbol.
  bool Named = K != StandAloneSig && K != BCLMember;
  MCSymbol *Sym =
      Named ? Ctx.getOrCreateSymbol(Name) : Ctx.createTempSymbol("msil_ref");
  // Every symbol of an MSIL MCContext is an MCSymbolMSIL. A symbol that a
  // definition or an earlier reference classified keeps its kind.
  if (Ctx.getObjectFileType() == MCContext::IsMSIL) {
    auto *MSILSym = static_cast<MCSymbolMSIL *>(Sym);
    if (MSILSym->getKind() == MCSymbolMSIL::Kind::Unknown)
      MSILSym->setKind(getSymbolKind(K));
    // ExternalMemberId 0 means none.
    if (K == BCLMember)
      MSILSym->setExternalMemberId(static_cast<uint32_t>(BCL) + 1);
  }
  return new (Ctx)
      MSILMemberRefExpr(K, Named ? Ctx.allocateString(Name) : StringRef(),
                        Ctx.allocateString(FieldType), S, BCL, Sym);
}

const MSILMemberRefExpr *
MSILMemberRefExpr::createMethod(StringRef ILName, const MSIL::Signature *S,
                                MCContext &Ctx) {
  assert(S && "a method reference needs a signature");
  return create(Method, ILName, StringRef(), S, cli::BCLMemberId(), Ctx);
}

const MSILMemberRefExpr *MSILMemberRefExpr::createExternMethod(
    StringRef ILName, const MSIL::Signature *S, MCContext &Ctx) {
  assert(S && "a method reference needs a signature");
  return create(ExternMethod, ILName, StringRef(), S, cli::BCLMemberId(), Ctx);
}

const MSILMemberRefExpr *MSILMemberRefExpr::createField(StringRef ILName,
                                                        MCContext &Ctx) {
  return create(Field, ILName, StringRef(), nullptr, cli::BCLMemberId(), Ctx);
}

const MSILMemberRefExpr *
MSILMemberRefExpr::createValueTypeField(StringRef ILName, StringRef TypeName,
                                        MCContext &Ctx) {
  assert(!TypeName.empty() && "a value type field needs its type");
  return create(Field, ILName, TypeName, nullptr, cli::BCLMemberId(), Ctx);
}

const MSILMemberRefExpr *MSILMemberRefExpr::createExternField(StringRef ILName,
                                                              MCContext &Ctx) {
  return create(ExternField, ILName, StringRef(), nullptr, cli::BCLMemberId(),
                Ctx);
}

const MSILMemberRefExpr *MSILMemberRefExpr::createFnAddr(StringRef ILName,
                                                         MCContext &Ctx) {
  return create(FnAddr, ILName, StringRef(), nullptr, cli::BCLMemberId(), Ctx);
}

const MSILMemberRefExpr *MSILMemberRefExpr::createSig(const MSIL::Signature *S,
                                                      MCContext &Ctx) {
  assert(S && "a call-site signature reference needs a signature");
  return create(StandAloneSig, StringRef(), StringRef(), S, cli::BCLMemberId(),
                Ctx);
}

const MSILMemberRefExpr *MSILMemberRefExpr::createBCL(cli::BCLMemberId Id,
                                                      MCContext &Ctx) {
  return create(BCLMember, StringRef(), StringRef(), nullptr, Id, Ctx);
}

/// Prints the placeholder parent of an object's MemberRefs to undefined C
/// symbols and function addresses, "[msil.extern]'<Type>'::" (plan 5.8.3).
static void printPlaceholderParent(raw_ostream &OS, StringRef Type) {
  OS << '[' << MSILLink::ExternAssembly << ']';
  MSIL::printQuotedName(OS, Type);
  OS << "::";
}

void MSILMemberRefExpr::printOperand(raw_ostream &OS, bool ObjectForm) const {
  switch (K) {
  case Method:
  case ExternMethod:
    // int32 'sum'(native int, int32)
    // int32 [msil.extern]'<msil>Symbols'::'sum'(native int, int32)
    MSIL::printResult(OS, *Sig);
    OS << ' ';
    if (ObjectForm && K == ExternMethod)
      printPlaceholderParent(OS, MSILLink::SymbolsType);
    MSIL::printQuotedName(OS, Name);
    MSIL::printParamList(OS, *Sig);
    return;
  case Field:
    // native int 'table'
    // valuetype '<msil>image_t' '<msil>image'
    if (FieldType.empty()) {
      OS << "native int ";
    } else {
      OS << "valuetype ";
      MSIL::printQuotedName(OS, FieldType);
      OS << ' ';
    }
    MSIL::printQuotedName(OS, Name);
    return;
  case ExternField:
    // native int 'x'
    // native int [msil.extern]'<msil>Symbols'::'x'
    OS << "native int ";
    if (ObjectForm)
      printPlaceholderParent(OS, MSILLink::SymbolsType);
    MSIL::printQuotedName(OS, Name);
    return;
  case FnAddr:
    // native int '<msil>fn_f'
    // native int [msil.extern]'<msil>FnAddr'::'f'
    OS << "native int ";
    if (ObjectForm) {
      printPlaceholderParent(OS, MSILLink::FnAddrType);
      MSIL::printQuotedName(OS, Name);
    } else {
      MSIL::printQuotedName(OS, (Twine(MSIL::FnAddrFieldPrefix) + Name).str());
    }
    return;
  case StandAloneSig:
    // [unmanaged cdecl ]int32(native int, int32)
    switch (Sig->CC) {
    case MSIL::CallConv::Managed:
      break;
    case MSIL::CallConv::UnmanagedCdecl:
      OS << "unmanaged cdecl ";
      break;
    case MSIL::CallConv::UnmanagedStdcall:
      OS << "unmanaged stdcall ";
      break;
    }
    MSIL::printResult(OS, *Sig);
    MSIL::printParamList(OS, *Sig);
    return;
  case BCLMember:
    // void [mscorlib]System.Threading.Interlocked::MemoryBarrier()
    // void [msil.bcl]System.Threading.Interlocked::MemoryBarrier()
    cli::printBCLMemberRef(OS, BCL,
                           ObjectForm ? StringRef(MSILLink::BCLAssembly)
                                      : StringRef(cli::MscorlibAssembly));
    return;
  }
  llvm_unreachable("MSIL: unknown member reference kind");
}

void MSILMemberRefExpr::printImpl(raw_ostream &OS, const MCAsmInfo *) const {
  printOperand(OS, /*ObjectForm=*/false);
}

bool MSILMemberRefExpr::evaluateAsRelocatableImpl(
    MCValue &Res, const MCAssembler * /*Asm*/) const {
  // Never absolute: the object writer assigns the token (MSILAsmBackend
  // sends every token fixup to recordRelocation), reading the operand class
  // from the fixup's value, this expression.
  Res = MCValue::get(Sym);
  return true;
}

bool MSILMemberRefExpr::isEqualTo(const MCExpr *X) const {
  const auto *E = dyn_cast<MSILMemberRefExpr>(X);
  if (!E || E->K != K || E->Name != Name || E->FieldType != FieldType ||
      (K == BCLMember && E->BCL != BCL))
    return false;
  if (!Sig || !E->Sig)
    return Sig == E->Sig;
  return *Sig == *E->Sig;
}

void MSILMemberRefExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedSymbol(*Sym);
}

MCFragment *MSILMemberRefExpr::findAssociatedFragment() const {
  return Sym->getFragment();
}
