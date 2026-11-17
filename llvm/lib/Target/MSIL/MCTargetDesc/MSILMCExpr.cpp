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
/// The managed-call kinds (plan 5.20.5 ML3) print the member and the types
/// that a declaration's attributes name: in whole-program form in the
/// declaration's text scope ("msil-managed-asm", default mscorlib), in object
/// form in [msil.bcl], where msil-ld binds them by name and full signature.
/// In object form the parent of a static member of a TYPE written 'valuetype
/// T' is the TypeSpec 'valuetype [msil.bcl]T', so that msil-ld sees how the
/// declaration named the type (MSIL::hasValueTypeSpecParent). The run-time
/// checks of whole-program text (plan 5.20.6 MW2 as amended) add a TypeSpec
/// of TYPE's declared kind and the messages of their exceptions, which no
/// object carries.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/ManagedSignature.h"
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
  case MSILMemberRefExpr::ManagedMember:
  case MSILMemberRefExpr::ManagedType:
  case MSILMemberRefExpr::ManagedTypeSpec:
    return MCSymbolMSIL::Kind::ExternalMember;
  case MSILMemberRefExpr::UserString:
    // A string of whole-program text names nothing that an object binds.
    return MCSymbolMSIL::Kind::Unknown;
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

const MSILMemberRefExpr *MSILMemberRefExpr::createManaged(
    Kind K, StringRef Member, const cli::ManagedType *Type,
    const cli::ManagedSignature *S, StringRef Scope, MCContext &Ctx) {
  assert(Type && "a managed-call reference needs its type");
  // Like a BCL member, a managed member or type has no C symbol; its
  // ExternalMemberId stays 0 (none).
  MCSymbol *Sym = Ctx.createTempSymbol("msil_ref");
  if (Ctx.getObjectFileType() == MCContext::IsMSIL) {
    auto *MSILSym = static_cast<MCSymbolMSIL *>(Sym);
    if (MSILSym->getKind() == MCSymbolMSIL::Kind::Unknown)
      MSILSym->setKind(getSymbolKind(K));
  }
  return new (Ctx) MSILMemberRefExpr(K, Ctx.allocateString(Member), Type, S,
                                     Ctx.allocateString(Scope), Sym);
}

const MSILMemberRefExpr *MSILMemberRefExpr::createManagedMember(
    const cli::ManagedType *Owner, StringRef Member,
    const cli::ManagedSignature *S, StringRef Scope, MCContext &Ctx) {
  assert(Owner && Owner->isNamed() && S &&
         "a managed member belongs to a class or a value type and has a "
         "signature");
  return createManaged(ManagedMember, Member, Owner, S, Scope, Ctx);
}

const MSILMemberRefExpr *
MSILMemberRefExpr::createManagedType(const cli::ManagedType *Type,
                                     StringRef Scope, MCContext &Ctx) {
  return createManaged(ManagedType, StringRef(), Type, nullptr, Scope, Ctx);
}

const MSILMemberRefExpr *
MSILMemberRefExpr::createManagedTypeSpec(const cli::ManagedType *Type,
                                         StringRef Scope, MCContext &Ctx) {
  assert(Type && Type->isNamed() &&
         "a type specification of a kind is a class or a value type");
  return createManaged(ManagedTypeSpec, StringRef(), Type, nullptr, Scope, Ctx);
}

const MSILMemberRefExpr *MSILMemberRefExpr::createUserString(StringRef Text,
                                                             MCContext &Ctx) {
  // Like a BCL member, a string has no C symbol; its symbol stays Unknown.
  return new (Ctx) MSILMemberRefExpr(UserString, Ctx.allocateString(Text),
                                     StringRef(), nullptr, cli::BCLMemberId(),
                                     Ctx.createTempSymbol("msil_ref"));
}

StringRef MSIL::getManagedTypeRefName(const cli::ManagedType &T) {
  using TK = cli::ManagedType::TypeKind;
  switch (T.Kind) {
  case TK::Class:
  case TK::ValueType:
    return T.TypeName;
  case TK::Bool:
    return "System.Boolean";
  case TK::Char:
    return "System.Char";
  case TK::Int8:
    return "System.SByte";
  case TK::UInt8:
    return "System.Byte";
  case TK::Int16:
    return "System.Int16";
  case TK::UInt16:
    return "System.UInt16";
  case TK::Int32:
    return "System.Int32";
  case TK::UInt32:
    return "System.UInt32";
  case TK::Int64:
    return "System.Int64";
  case TK::UInt64:
    return "System.UInt64";
  case TK::Float32:
    return "System.Single";
  case TK::Float64:
    return "System.Double";
  case TK::NativeInt:
    return "System.IntPtr";
  case TK::NativeUInt:
    return "System.UIntPtr";
  case TK::String:
    return "System.String";
  case TK::Object:
    return "System.Object";
  case TK::Void:
  case TK::Pointer:
  case TK::SZArray:
    break;
  }
  return "";
}

bool MSIL::isOneByteValueType(StringRef FullName) {
  static constexpr StringLiteral OneByteValueTypes[] = {
      "System.Boolean", "System.Char",   "System.SByte",  "System.Byte",
      "System.Int16",   "System.UInt16", "System.Int32",  "System.UInt32",
      "System.Int64",   "System.UInt64", "System.Single", "System.Double",
      "System.IntPtr",  "System.UIntPtr"};
  return is_contained(OneByteValueTypes, FullName);
}

bool MSIL::hasValueTypeSpecParent(const cli::ManagedType &Owner,
                                  const cli::ManagedSignature &Sig) {
  if (Owner.Kind != cli::ManagedType::TypeKind::ValueType || Sig.HasThis ||
      Owner.isGCHandle())
    return false;
  // The value types that a signature spells in one byte keep their TypeRef,
  // since the TypeSpec would be the long form. System.String and
  // System.Object are classes, so 'valuetype' misnames them, and msil-ld
  // reports it (T-M16).
  return !isOneByteValueType(Owner.TypeName);
}

/// Prints the full name \p TypeName of a class or value type ("Ns.Name",
/// "Ns.Outer/Inner") as cli::ManagedType::print does: each '/'-separated part
/// bare when ilasm reads it as a dotted name (cli::isILAsmBareDottedName),
/// else single-quoted ("'value'", "Ns.Outer/'value'", "'Ns.Café'").
static void printILAsmTypeName(raw_ostream &OS, StringRef TypeName) {
  SmallVector<StringRef, 2> Parts;
  TypeName.split(Parts, '/');
  ListSeparator Slash("/");
  for (StringRef Part : Parts) {
    OS << Slash;
    if (cli::isILAsmBareDottedName(Part))
      OS << Part;
    else
      cli::printILAsmQuotedName(OS, Part);
  }
}

/// Prints the operand of a managed type token in \p Scope: a class, a value
/// type or a primitive by its class name ("[mscorlib]System.IO.Stream",
/// "[mscorlib]System.Byte"), a pointer or array type as a signature type
/// ("uint8[]", "class [mscorlib]System.IO.Stream[]").
static void printManagedTypeToken(raw_ostream &OS, const cli::ManagedType &T,
                                  StringRef Scope) {
  StringRef Name = MSIL::getManagedTypeRefName(T);
  if (Name.empty()) {
    T.print(OS, Scope);
    return;
  }
  cli::printILAsmScope(OS, Scope);
  printILAsmTypeName(OS, Name);
}

/// Prints the member \p Member of \p Owner with its own signature \p Sig in
/// \p Scope: "instance void [mscorlib]System.IO.FileStream::.ctor(string,
/// valuetype [mscorlib]System.IO.FileMode)", or "int64
/// [System]System.Diagnostics.Stopwatch::Frequency" for a field, which ILAsm
/// spells without 'instance'. \p Owner and \p Member are quoted as ilasm
/// needs them ("[mscorlib]Ns.Outer/'value'::'int32'"); '.ctor' never is.
/// \p ValueTypeParent: the owner is printed as the TypeSpec 'valuetype
/// [msil.bcl]T' (MSIL::hasValueTypeSpecParent, object form only).
static void printManagedMember(raw_ostream &OS, StringRef Owner,
                               StringRef Member,
                               const cli::ManagedSignature &Sig,
                               StringRef Scope, bool ValueTypeParent = false) {
  if (Sig.HasThis && !Sig.IsField)
    OS << "instance ";
  Sig.Return.print(OS, Scope);
  OS << ' ';
  if (ValueTypeParent)
    OS << "valuetype ";
  cli::printILAsmScope(OS, Scope);
  printILAsmTypeName(OS, Owner);
  OS << "::";
  if (Member == ".ctor" || Member == ".cctor" ||
      cli::isILAsmBareDottedName(Member))
    OS << Member;
  else
    cli::printILAsmQuotedName(OS, Member);
  if (Sig.IsField)
    return;
  OS << '(';
  ListSeparator LS;
  for (const cli::ManagedType &P : Sig.Params) {
    OS << LS;
    P.print(OS, Scope);
  }
  OS << ')';
}

/// The signature of the BCL method \p Info as ManagedSignature, or
/// std::nullopt if a type of its BCLMembers.def text is not one that
/// cli::parseManagedSigType accepts.
static std::optional<cli::ManagedSignature>
getBCLManagedSignature(const cli::BCLMemberInfo &Info) {
  cli::ManagedSignature Sig;
  Sig.HasThis = Info.HasThis;
  Expected<cli::ManagedType> Return = cli::parseManagedSigType(Info.Return);
  if (!Return) {
    consumeError(Return.takeError());
    return std::nullopt;
  }
  Sig.Return = std::move(*Return);
  StringRef Params = StringRef(Info.Params).trim();
  if (Params.empty())
    return Sig;
  SmallVector<StringRef, 4> Parts;
  Params.split(Parts, ',');
  for (StringRef Part : Parts) {
    Expected<cli::ManagedType> T = cli::parseManagedSigType(Part.trim());
    if (!T) {
      consumeError(T.takeError());
      return std::nullopt;
    }
    Sig.Params.push_back(std::move(*T));
  }
  return Sig;
}

/// Prints the BCL member \p Id in \p Scope. A BCLMembers.def row spells a
/// class or value type in its signature without a scope ("valuetype
/// System.Runtime.InteropServices.GCHandle"), which ILAsm text scopes as it
/// scopes the owner; every other row prints as cli::printBCLMemberRef does.
static void printBCLMember(raw_ostream &OS, cli::BCLMemberId Id,
                           StringRef Scope) {
  const cli::BCLMemberInfo &Info = cli::getBCLMemberInfo(Id);
  auto Named = [](StringRef Text) {
    return Text.contains("class ") || Text.contains("valuetype ");
  };
  std::optional<cli::ManagedSignature> Sig;
  if (Info.isMethod() && (Named(Info.Return) || Named(Info.Params)))
    Sig = getBCLManagedSignature(Info);
  if (!Sig) {
    cli::printBCLMemberRef(OS, Id, Scope);
    return;
  }
  const cli::BCLMemberInfo &Owner = cli::getBCLMemberInfo(Info.Owner);
  std::string OwnerName = Owner.Namespace.empty()
                              ? Owner.Name.str()
                              : (Owner.Namespace + "." + Owner.Name).str();
  printManagedMember(OS, OwnerName, Info.Name, *Sig, Scope);
}

/// The identity of a member of \p Owner (rule R6, getMemberRefIdentity),
/// whose parent is the TypeRef of \p Owner, or with \p ValueTypeParent the
/// TypeSpec 'valuetype Owner' (MSIL::hasValueTypeSpecParent). A type name
/// has no space, so "valuetype " starts no other identity.
static std::string memberIdentity(StringRef Owner, StringRef Member,
                                  const cli::ManagedSignature &Sig,
                                  bool ValueTypeParent = false) {
  return ((ValueTypeParent ? "valuetype " : "") + Owner + "::" + Member + " " +
          Sig.str())
      .str();
}

std::optional<std::string>
MSIL::getMemberRefIdentity(const MSILMemberRefExpr &Ref) {
  switch (Ref.getKind()) {
  case MSILMemberRefExpr::ManagedMember: {
    const cli::ManagedType &Owner = *Ref.getManagedType();
    const cli::ManagedSignature &Sig = *Ref.getManagedSignature();
    return memberIdentity(Owner.TypeName, Ref.getName(), Sig,
                          hasValueTypeSpecParent(Owner, Sig));
  }
  case MSILMemberRefExpr::BCLMember: {
    const cli::BCLMemberInfo &Info = cli::getBCLMemberInfo(Ref.getBCLMember());
    if (!Info.isMethod())
      return std::nullopt;
    std::optional<cli::ManagedSignature> Sig = getBCLManagedSignature(Info);
    if (!Sig)
      return std::nullopt;
    const cli::BCLMemberInfo &Owner = cli::getBCLMemberInfo(Info.Owner);
    std::string OwnerName = Owner.Namespace.empty()
                                ? Owner.Name.str()
                                : (Owner.Namespace + "." + Owner.Name).str();
    return memberIdentity(OwnerName, Info.Name, *Sig);
  }
  default:
    return std::nullopt;
  }
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
    printBCLMember(OS, BCL,
                   ObjectForm ? StringRef(MSILLink::BCLAssembly)
                              : StringRef(cli::MscorlibAssembly));
    return;
  case ManagedMember:
    // instance int64 [mscorlib]System.DateTime::get_Ticks()
    // instance int64 [msil.bcl]System.DateTime::get_Ticks()
    // In object form, a static member of a TYPE written 'valuetype T' has a
    // TypeSpec parent (MSIL::hasValueTypeSpecParent):
    // int32 valuetype [msil.bcl]Contoso.Clock::Now()
    printManagedMember(OS, MType->TypeName, Name, *MSig,
                       ObjectForm ? StringRef(MSILLink::BCLAssembly) : Scope,
                       ObjectForm &&
                           MSIL::hasValueTypeSpecParent(*MType, *MSig));
    return;
  case ManagedType:
    // [mscorlib]System.IO.Stream, [msil.bcl]System.IO.Stream, uint8[]
    printManagedTypeToken(
        OS, *MType, ObjectForm ? StringRef(MSILLink::BCLAssembly) : Scope);
    return;
  case ManagedTypeSpec:
    // class [mscorlib]System.IO.Stream, valuetype [mscorlib]System.DateTime
    MType->print(OS, ObjectForm ? StringRef(MSILLink::BCLAssembly) : Scope);
    return;
  case UserString:
    // "managed call 'f': ..."
    MSIL::printQuotedString(OS, Name);
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
  if (K == ManagedMember || K == ManagedType || K == ManagedTypeSpec)
    return E->Scope == Scope && *E->MType == *MType &&
           (!MSig || !E->MSig ? MSig == E->MSig : *E->MSig == *MSig);
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
