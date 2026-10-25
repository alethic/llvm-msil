//===-- MSILObjectWriter.cpp - MSIL CIL object writer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements MSILObjectWriter, which writes CIL objects
/// (llvm/docs/MSIL/ImplementationPlan.md section 5.8) with the ECMA-335
/// library: cli::MetadataBuilder for the rows, cli::MethodBodyStream for the
/// method bodies and cli::writePEImage for the netmodule.
///
/// The rows follow row order R6 (plan 5.8.3), the order in which ilasm numbers
/// the rows of the same object's object-form text, so that every token of the
/// link-info record, which the AsmPrinter numbers by the same rule, names the
/// row it means:
/// 1. the AssemblyRefs 'msil.extern', 'msil.bcl' and 'msil.link';
/// 2. per method, in event order: its MethodDef, its locals' StandAloneSig,
///    then the rows its tokens name, in code order (TypeRefs, MemberRefs and
///    StandAloneSigs on first appearance; MetadataBuilder merges duplicates);
/// 3. the address fields;
/// 4. the import stubs, each with its ModuleRef and ImplMap;
/// 5. the record's carrier: the TypeRef '[msil.link]'<msil>LinkInfo'', its
///    constructor's MemberRef and the CustomAttribute on the Module row.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILObjectWriter.h"
#include "MCTargetDesc/MSILFixupKinds.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MCTargetDesc/MSILTargetStreamer.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/CustomAttribute.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/LinkInfo.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/PEWriter.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <utility>

using namespace llvm;

//===----------------------------------------------------------------------===//
// Signatures
//===----------------------------------------------------------------------===//

/// The signature type of the CIL value type \p T.
static cli::TypeSig toTypeSig(MSIL::ValType T) {
  switch (T) {
  case MSIL::ValType::I32:
    return cli::TypeSig::get(cli::ELEMENT_TYPE_I4);
  case MSIL::ValType::NativeInt:
    return cli::TypeSig::get(cli::ELEMENT_TYPE_I);
  case MSIL::ValType::I64:
    return cli::TypeSig::get(cli::ELEMENT_TYPE_I8);
  case MSIL::ValType::F32:
    return cli::TypeSig::get(cli::ELEMENT_TYPE_R4);
  case MSIL::ValType::F64:
    return cli::TypeSig::get(cli::ELEMENT_TYPE_R8);
  }
  llvm_unreachable("MSIL: unknown ValType");
}

/// The ECMA-335 signature blob of \p S. Methods and member references are
/// static and managed (DEFAULT), as their ILAsm text spells them; a 'calli'
/// signature (\p IsCallSite) keeps its unmanaged calling convention, which
/// ilasm writes as the C (SIG_C) or stdcall (SIG_STDCALL) kind.
static SmallVector<uint8_t, 16> encodeSignature(const MSIL::Signature &S,
                                                bool IsCallSite) {
  assert(S.Results.size() <= 1 && "a CIL method has at most one result");
  cli::MethodSig M;
  if (IsCallSite) {
    switch (S.CC) {
    case MSIL::CallConv::Managed:
      M.Header = cli::SIG_DEFAULT;
      break;
    case MSIL::CallConv::UnmanagedCdecl:
      M.Header = cli::SIG_C;
      break;
    case MSIL::CallConv::UnmanagedStdcall:
      M.Header = cli::SIG_STDCALL;
      break;
    }
  }
  if (!S.Results.empty())
    M.Return = toTypeSig(S.Results.front());
  for (MSIL::ValType T : S.Params)
    M.Params.push_back(toTypeSig(T));
  SmallVector<uint8_t, 16> Out;
  cli::encodeMethodSig(M, Out);
  return Out;
}

/// Parses a type of llvm/CLI/BCLMembers.def, which spells types as canonical
/// listing v1 does (plan 5.7.10): a primitive, then any number of '*' and
/// '[]'. \p AllowVoid accepts a plain 'void' (a return type). Anything else
/// gives std::nullopt.
static std::optional<cli::TypeSig> parseBCLType(StringRef Text,
                                                bool AllowVoid) {
  Text = Text.trim();
  if (Text.consume_back("*")) {
    std::optional<cli::TypeSig> Pointee = parseBCLType(Text, true);
    if (!Pointee)
      return std::nullopt;
    return cli::TypeSig::getPointer(*Pointee);
  }
  if (Text.consume_back("[]")) {
    std::optional<cli::TypeSig> Element = parseBCLType(Text, false);
    if (!Element)
      return std::nullopt;
    return cli::TypeSig::getSZArray(*Element);
  }
  if (Text == "void") {
    if (!AllowVoid)
      return std::nullopt;
    return cli::TypeSig();
  }
  static constexpr std::pair<StringLiteral, cli::ElementType> Primitives[] = {
      {"bool", cli::ELEMENT_TYPE_BOOLEAN},
      {"char", cli::ELEMENT_TYPE_CHAR},
      {"int8", cli::ELEMENT_TYPE_I1},
      {"uint8", cli::ELEMENT_TYPE_U1},
      {"int16", cli::ELEMENT_TYPE_I2},
      {"uint16", cli::ELEMENT_TYPE_U2},
      {"int32", cli::ELEMENT_TYPE_I4},
      {"uint32", cli::ELEMENT_TYPE_U4},
      {"int64", cli::ELEMENT_TYPE_I8},
      {"uint64", cli::ELEMENT_TYPE_U8},
      {"float32", cli::ELEMENT_TYPE_R4},
      {"float64", cli::ELEMENT_TYPE_R8},
      {"native int", cli::ELEMENT_TYPE_I},
      {"native uint", cli::ELEMENT_TYPE_U},
      {"string", cli::ELEMENT_TYPE_STRING},
      {"object", cli::ELEMENT_TYPE_OBJECT},
      {"typedref", cli::ELEMENT_TYPE_TYPEDBYREF},
  };
  for (const auto &[Name, ET] : Primitives)
    if (Text == Name)
      return cli::TypeSig::get(ET);
  return std::nullopt;
}

/// Encodes the signature of the BCL method \p Info into \p Out from its
/// BCLMembers.def text. Returns false if a type is not one parseBCLType
/// knows.
static bool encodeBCLMethodSig(const cli::BCLMemberInfo &Info,
                               SmallVectorImpl<uint8_t> &Out) {
  assert(Info.isMethod() && "not a BCL method");
  cli::MethodSig M;
  M.Header = Info.HasThis ? cli::SIG_HASTHIS : cli::SIG_DEFAULT;
  std::optional<cli::TypeSig> Return = parseBCLType(Info.Return, true);
  if (!Return)
    return false;
  M.Return = *Return;
  StringRef Params = StringRef(Info.Params).trim();
  if (!Params.empty()) {
    SmallVector<StringRef, 4> Parts;
    Params.split(Parts, ',');
    for (StringRef Part : Parts) {
      std::optional<cli::TypeSig> T = parseBCLType(Part, false);
      if (!T)
        return false;
      M.Params.push_back(*T);
    }
  }
  cli::encodeMethodSig(M, Out);
  return true;
}

//===----------------------------------------------------------------------===//
// MSILObjectWriter
//===----------------------------------------------------------------------===//

MSILObjectWriter::MSILObjectWriter() = default;

MSILObjectWriter::~MSILObjectWriter() = default;

void MSILObjectWriter::resetObjectState() {
  HasModule = false;
  TT = Triple();
  Methods.clear();
  Fields.clear();
  Stubs.clear();
  ModuleName.clear();
  LinkInfo.reset();
  TokenSites.clear();
}

void MSILObjectWriter::setModule(StringRef /*SourceFile*/, const Triple &T) {
  // Each object starts empty, whatever an earlier one left behind.
  resetObjectState();
  HasModule = true;
  TT = T;
}

void MSILObjectWriter::addMethod(MCSection &Sec, const MSILMethodDecl &M,
                                 unsigned MaxStack,
                                 ArrayRef<cli::TypeSig> Locals) {
  assert(M.Kind == MSILMethodKind::Function && M.Sig &&
         "an object's methods are the functions it defines");
  Methods.push_back(
      {&Sec, M.Name.str(), *M.Sig, M.Public, MaxStack, Locals.vec()});
}

void MSILObjectWriter::addAddressField(StringRef Name, bool Public) {
  Fields.push_back({Name.str(), Public});
}

void MSILObjectWriter::addPInvokeStub(const MSILMethodDecl &M, StringRef Module,
                                      StringRef Entry) {
  assert(M.Sig && "an import stub needs a signature");
  Stubs.push_back({M.Name.str(), *M.Sig, M.Public, Module.str(), Entry.str()});
}

void MSILObjectWriter::setLinkInfo(StringRef Name, const MSILLink::Record &R) {
  ModuleName = Name.str();
  LinkInfo = R;
}

void MSILObjectWriter::recordRelocation(const MCFragment &F,
                                        const MCFixup &Fixup, MCValue Target,
                                        uint64_t &FixedValue) {
  // MSILAsmBackend writes FixedValue into the token's four bytes; writeObject
  // patches the real token once every row is numbered.
  FixedValue = 0;
  MCFixupKind Kind = Fixup.getKind();
  const auto *Ref = dyn_cast<MSILMemberRefExpr>(Fixup.getValue());
  if (Kind < FirstTargetFixupKind || Kind >= MSIL::LastTargetFixupKind ||
      !Ref) {
    // Branches resolve inside their method's section; nothing else is a
    // relocation.
    reportError(Fixup.getLoc(), "MSIL: a CIL object has no relocations other "
                                "than metadata tokens");
    return;
  }
  if (Ref->getKind() == MSILMemberRefExpr::Field &&
      !Ref->getFieldTypeName().empty()) {
    reportError(Fixup.getLoc(), "MSIL: field '" + Ref->getName() +
                                    "' of the data image is whole-program "
                                    "output, which a CIL object does not have");
    return;
  }
  TokenOperand Op;
  Op.Kind = Ref->getKind();
  Op.Name = Ref->getName().str();
  if (const MSIL::Signature *Sig = Ref->getSignature())
    Op.Sig = *Sig;
  if (Op.Kind == MSILMemberRefExpr::BCLMember)
    Op.BCL = Ref->getBCLMember();
  uint64_t Offset = Asm->getFragmentOffset(F) + Fixup.getOffset();
  TokenSites[F.getParent()].insert_or_assign(Offset, std::move(Op));
}

namespace {
/// A token of a method's code, resolved to its row. MethodDef and Field rows
/// get their tokens when MetadataBuilder::finalize numbers the member tables,
/// after the code has named them; every other row's token is final at once.
struct ResolvedToken {
  enum Kind : uint8_t {
    /// Value is an index into the MethodDef handles (definitions, then
    /// stubs).
    MethodRow,
    /// Value is an index into the Field handles.
    FieldRow,
    /// Value is the token.
    Final,
  };
  Kind K;
  uint32_t Value;
};

/// A token site of a method's code: the offset of its four bytes.
struct TokenSite {
  uint64_t Offset;
  ResolvedToken Token;
};
} // end anonymous namespace

uint64_t MSILObjectWriter::writeObject(MCAssembler &Asm,
                                       raw_pwrite_stream &OS) {
  // The object's state goes with the object, on every return path: the
  // sections that Methods and TokenSites point to die with the MCContext's
  // next reset. Declared first, so that it runs after every local that
  // refers to that state (MethodIndex holds StringRefs into Methods).
  scope_exit ClearObjectState([this] { resetObjectState(); });
  MCContext &Ctx = Asm.getContext();
  bool Failed = false;
  auto Fail = [&](const Twine &Msg) {
    Ctx.reportError(SMLoc(), Msg);
    Failed = true;
  };
  if (!HasModule || !LinkInfo) {
    // Only the MSIL AsmPrinter produces the events of an object.
    Fail("MSIL: a CIL object is written from the events of the code "
         "generator, and this one has no " +
         Twine(HasModule ? "link-info record" : "object header"));
    return 0;
  }

  cli::MetadataBuilder MB;
  MB.setModuleName(ModuleName);
  const cli::EntityHandle ModuleType = MB.getModuleType();

  // 1. The placeholder scopes (plan 5.8.3): AssemblyRef rows 1-3, unbound, so
  // version 0.0.0.0 and no key.
  auto AddAssemblyRef = [&](StringRef Name) {
    cli::AssemblyRefInfo Ref;
    Ref.Name = Name.str();
    return MB.addAssemblyRef(Ref);
  };
  const cli::EntityHandle ExternScope =
      AddAssemblyRef(MSILLink::ExternAssembly);
  const cli::EntityHandle BCLScope = AddAssemblyRef(MSILLink::BCLAssembly);
  const cli::EntityHandle LinkScope =
      AddAssemblyRef(MSILLink::LinkInfoAssembly);

  // The rows that the code's Method and Field tokens name: a definition or an
  // import stub by IL name and signature (an import-attributed declaration
  // has one stub per call signature, rule R7), an address field by name.
  const size_t NumDefinitions = Methods.size();
  std::map<std::pair<StringRef, MSIL::Signature>, uint32_t> MethodIndex;
  for (size_t I = 0; I != NumDefinitions; ++I)
    MethodIndex.try_emplace({Methods[I].Name, Methods[I].Sig}, I);
  for (size_t I = 0, E = Stubs.size(); I != E; ++I)
    MethodIndex.try_emplace({Stubs[I].Name, Stubs[I].Sig}, NumDefinitions + I);
  StringMap<uint32_t> FieldIndex;
  for (size_t I = 0, E = Fields.size(); I != E; ++I)
    FieldIndex.try_emplace(Fields[I].Name, I);
  SmallVector<cli::EntityHandle, 0> MethodRows(NumDefinitions + Stubs.size());
  SmallVector<cli::EntityHandle, 0> FieldRows(Fields.size());

  static constexpr uint8_t NativeIntFieldSig[] = {cli::SIG_FIELD,
                                                  cli::ELEMENT_TYPE_I};
  // The tokens of the MemberRefs on '<msil>Symbols' with a method signature:
  // the only MemberRefs a SIGNATURES entry may name (rule C7).
  DenseSet<uint32_t> SymbolsMethodRefs;

  // The TypeRef of a BCL type, '[msil.bcl]<Namespace>.<Name>'; a nested type
  // 'Outer/Inner' is scoped to its enclosing type's TypeRef, as ilasm does.
  auto GetBCLTypeRef = [&](const cli::BCLMemberInfo &Type) {
    SmallVector<StringRef, 2> Names;
    StringRef(Type.Name).split(Names, '/');
    cli::EntityHandle Ref = MB.addTypeRef(BCLScope, Type.Namespace, Names[0]);
    for (StringRef Inner : drop_begin(Names))
      Ref = MB.addTypeRef(Ref, "", Inner);
    return Ref;
  };
  // A MemberRef of a C symbol on its placeholder parent '<Type>' (plan
  // 5.8.3). Each row is added after the rows it refers to, in this order, so
  // that row and heap order never depend on the order in which a compiler
  // evaluates arguments.
  auto AddPlaceholderRef = [&](StringRef Type, StringRef Name,
                               ArrayRef<uint8_t> Sig) {
    cli::EntityHandle Parent = MB.addTypeRef(ExternScope, "", Type);
    uint32_t SigBlob = MB.addBlob(Sig);
    return MB.addMemberRef(Parent, Name, SigBlob);
  };

  // The row of the entity of a token fixup, adding it on first appearance.
  auto Resolve = [&](const TokenOperand &Op) -> std::optional<ResolvedToken> {
    switch (Op.Kind) {
    case MSILMemberRefExpr::Method: {
      auto It =
          Op.Sig ? MethodIndex.find({Op.Name, *Op.Sig}) : MethodIndex.end();
      if (It == MethodIndex.end()) {
        Fail("MSIL: the CIL object has no method '" + Op.Name +
             "' with the signature that its call names");
        return std::nullopt;
      }
      return ResolvedToken{ResolvedToken::MethodRow, It->second};
    }
    case MSILMemberRefExpr::Field: {
      auto It = FieldIndex.find(Op.Name);
      if (It == FieldIndex.end()) {
        Fail("MSIL: the CIL object has no address field '" + Op.Name + "'");
        return std::nullopt;
      }
      return ResolvedToken{ResolvedToken::FieldRow, It->second};
    }
    case MSILMemberRefExpr::ExternMethod: {
      assert(Op.Sig && "a method reference needs a signature");
      cli::EntityHandle Ref =
          AddPlaceholderRef(MSILLink::SymbolsType, Op.Name,
                            encodeSignature(*Op.Sig, /*IsCallSite=*/false));
      SymbolsMethodRefs.insert(Ref.Value);
      return ResolvedToken{ResolvedToken::Final, Ref.Value};
    }
    case MSILMemberRefExpr::ExternField:
      return ResolvedToken{
          ResolvedToken::Final,
          AddPlaceholderRef(MSILLink::SymbolsType, Op.Name, NativeIntFieldSig)
              .Value};
    case MSILMemberRefExpr::FnAddr:
      return ResolvedToken{
          ResolvedToken::Final,
          AddPlaceholderRef(MSILLink::FnAddrType, Op.Name, NativeIntFieldSig)
              .Value};
    case MSILMemberRefExpr::StandAloneSig: {
      assert(Op.Sig && "a call-site signature reference needs a signature");
      uint32_t SigBlob =
          MB.addBlob(encodeSignature(*Op.Sig, /*IsCallSite=*/true));
      return ResolvedToken{ResolvedToken::Final,
                           MB.addStandAloneSig(SigBlob).Value};
    }
    case MSILMemberRefExpr::BCLMember: {
      const cli::BCLMemberInfo &Info = cli::getBCLMemberInfo(Op.BCL);
      if (Info.isType())
        return ResolvedToken{ResolvedToken::Final, GetBCLTypeRef(Info).Value};
      SmallVector<uint8_t, 16> Sig;
      if (!encodeBCLMethodSig(Info, Sig)) {
        Fail("MSIL: cannot encode the signature of BCL member '" + Info.IdName +
             "' (" + Info.Return + " (" + Info.Params +
             ")): llvm/CLI/BCLMembers.def has a type that is not a "
             "primitive, a pointer or an array");
        return std::nullopt;
      }
      cli::EntityHandle Parent =
          GetBCLTypeRef(cli::getBCLMemberInfo(Info.Owner));
      uint32_t SigBlob = MB.addBlob(Sig);
      return ResolvedToken{ResolvedToken::Final,
                           MB.addMemberRef(Parent, Info.Name, SigBlob).Value};
    }
    }
    llvm_unreachable("MSIL: unknown member reference kind");
  };

  // Every token fixup lies in the section of a method.
  SmallPtrSet<const MCSection *, 16> MethodSections;
  for (const MethodRecord &M : Methods)
    MethodSections.insert(M.Section);
  for (const auto &Entry : TokenSites)
    if (!MethodSections.contains(Entry.first)) {
      Fail("MSIL: a metadata token outside every method of the CIL object");
      return 0;
    }

  // 2. The methods, each followed by the rows its code names.
  SmallVector<cli::EntityHandle, 0> LocalSigs(NumDefinitions);
  std::vector<std::vector<TokenSite>> MethodTokens(NumDefinitions);
  for (size_t I = 0; I != NumDefinitions; ++I) {
    const MethodRecord &M = Methods[I];
    // R4: public for non-local linkage, assembly for internal and private.
    uint32_t SigBlob = MB.addBlob(encodeSignature(M.Sig, /*IsCallSite=*/false));
    MethodRows[I] = MB.addMethod(ModuleType,
                                 (M.Public ? cli::MethodAttributes::Public
                                           : cli::MethodAttributes::Assembly) |
                                     cli::MethodAttributes::Static,
                                 cli::MethodImplAttributes::IL |
                                     cli::MethodImplAttributes::Managed,
                                 M.Name, SigBlob);
    if (!M.Locals.empty()) {
      SmallVector<uint8_t, 16> Sig;
      cli::encodeLocalSig(M.Locals, Sig);
      LocalSigs[I] = MB.addStandAloneSig(MB.addBlob(Sig));
    }
    auto It = TokenSites.find(M.Section);
    if (It == TokenSites.end())
      continue;
    for (const auto &[Offset, Op] : It->second)
      if (std::optional<ResolvedToken> T = Resolve(Op))
        MethodTokens[I].push_back({Offset, *T});
  }

  // 3. The address fields of the defined globals, in SYMTAB order.
  const uint32_t FieldSigBlob = MB.addBlob(NativeIntFieldSig);
  for (size_t I = 0, E = Fields.size(); I != E; ++I)
    FieldRows[I] = MB.addField(
        ModuleType,
        (Fields[I].Public ? cli::FieldAttributes::Public
                          : cli::FieldAttributes::Assembly) |
            cli::FieldAttributes::Static | cli::FieldAttributes::InitOnly,
        Fields[I].Name, FieldSigBlob);

  // 4. The import stubs: body-less P/Invoke methods, one ModuleRef per
  // distinct module ('msil.unresolved' included), and ImplMap rows that bind
  // the entry exactly as given (nomangle; plan 5.10.11).
  for (size_t I = 0, E = Stubs.size(); I != E; ++I) {
    const StubRecord &S = Stubs[I];
    cli::EntityHandle Scope = MB.addModuleRef(S.Module);
    uint32_t SigBlob = MB.addBlob(encodeSignature(S.Sig, /*IsCallSite=*/false));
    cli::EntityHandle Method = MB.addMethod(
        ModuleType,
        (S.Public ? cli::MethodAttributes::Public
                  : cli::MethodAttributes::Assembly) |
            cli::MethodAttributes::Static | cli::MethodAttributes::HideBySig |
            cli::MethodAttributes::PinvokeImpl,
        cli::MethodImplAttributes::IL | cli::MethodImplAttributes::Managed |
            cli::MethodImplAttributes::PreserveSig,
        S.Name, SigBlob);
    MethodRows[NumDefinitions + I] = Method;
    cli::PInvokeAttributes Flags =
        cli::PInvokeAttributes::NoMangle |
        (S.Sig.CC == MSIL::CallConv::UnmanagedStdcall
             ? cli::PInvokeAttributes::CallConvStdcall
             : cli::PInvokeAttributes::CallConvCdecl);
    MB.addImplMap(Method, Flags, S.Entry, Scope);
  }

  // 5. The carrier of the link-info record (plan 5.8.7), numbered last:
  // 'instance void [msil.link]'<msil>LinkInfo'::.ctor(uint8[])' on the Module
  // row, with the value 01 00, the int32 count, the record and 00 00.
  cli::EntityHandle CarrierType =
      MB.addTypeRef(LinkScope, "", MSILLink::LinkInfoType);
  static constexpr uint8_t CtorSig[] = {
      cli::SIG_HASTHIS, 1, cli::ELEMENT_TYPE_VOID, cli::ELEMENT_TYPE_SZARRAY,
      cli::ELEMENT_TYPE_U1};
  uint32_t CtorSigBlob = MB.addBlob(CtorSig);
  cli::EntityHandle Ctor = MB.addMemberRef(CarrierType, ".ctor", CtorSigBlob);
  SmallVector<uint8_t, 0> Record;
  cli::encodeLinkInfo(*LinkInfo, Record);
  SmallVector<uint8_t, 0> CarrierValue;
  cli::encodeByteArrayAttribute(Record, CarrierValue);
  uint32_t CarrierBlob = MB.addBlob(CarrierValue);
  cli::EntityHandle ModuleRow;
  ModuleRow.Value = cli::makeToken(cli::TableId::Module, 1);
  MB.addCustomAttribute(ModuleRow, Ctor, CarrierBlob);

  if (Failed)
    return 0;
  cli::MethodBodyStream Bodies;
  if (Error E = MB.finalize(Bodies)) {
    Fail(toString(std::move(E)));
    return 0;
  }

  // Every token of the record must name the writer's row for it (the
  // AsmPrinter numbered them by rule R6); a mismatch is never patched.
  auto Mismatch = [&](const Twine &Name) {
    Fail("MSIL: link-info token mismatch for '" + Name + "'");
  };
  StringMap<uint32_t> DefinitionTokens, FieldTokens;
  DenseSet<uint32_t> MethodDefTokens;
  for (size_t I = 0, E = MethodRows.size(); I != E; ++I) {
    uint32_t Token = MB.getToken(MethodRows[I]);
    MethodDefTokens.insert(Token);
    if (I < NumDefinitions)
      DefinitionTokens.try_emplace(Methods[I].Name, Token);
  }
  for (size_t I = 0, E = FieldRows.size(); I != E; ++I)
    FieldTokens.try_emplace(Fields[I].Name, MB.getToken(FieldRows[I]));
  for (const MSILLink::Symbol &S : LinkInfo->Symbols) {
    // Only defined non-alias symbols (commons included) have a token.
    if (S.Flags & (MSILLink::Undefined | MSILLink::Alias))
      continue;
    const StringMap<uint32_t> &Rows = S.Kind == MSILLink::SymbolKind::Function
                                          ? DefinitionTokens
                                          : FieldTokens;
    auto It = Rows.find(S.Name);
    if (It == Rows.end() || It->second != S.Token)
      Mismatch(S.Name);
  }
  for (const MSILLink::SignatureInfo &SI : LinkInfo->Signatures)
    if (!MethodDefTokens.contains(SI.Token) &&
        !SymbolsMethodRefs.contains(SI.Token))
      Mismatch("0x" + utohexstr(SI.Token, /*LowerCase=*/false, 8));
  if (Failed)
    return 0;

  // The method bodies, with their tokens patched in; never InitLocals
  // (decision D13).
  for (size_t I = 0; I != NumDefinitions; ++I) {
    const MethodRecord &M = Methods[I];
    SmallString<256> Code;
    raw_svector_ostream CodeOS(Code);
    Asm.writeSectionData(CodeOS, M.Section);
    for (const TokenSite &Site : MethodTokens[I]) {
      if (Site.Offset + 4 > Code.size()) {
        Fail("MSIL: a metadata token outside the code of method '" + M.Name +
             "'");
        continue;
      }
      uint32_t Token = Site.Token.Value;
      if (Site.Token.K == ResolvedToken::MethodRow)
        Token = MB.getToken(MethodRows[Token]);
      else if (Site.Token.K == ResolvedToken::FieldRow)
        Token = MB.getToken(FieldRows[Token]);
      support::endian::write32le(Code.data() + Site.Offset, Token);
    }
    if (M.MaxStack > UINT16_MAX) {
      Fail("MSIL: method '" + M.Name + "' needs a .maxstack of " +
           Twine(M.MaxStack) + ", more than the 65535 that CIL allows");
      continue;
    }
    cli::MethodBodyInfo Body;
    Body.Code = arrayRefFromStringRef(Code.str());
    Body.MaxStack = M.MaxStack;
    Body.LocalVarSigToken = MB.getToken(LocalSigs[I]);
    MB.setMethodBodyOffset(MethodRows[I], Bodies.addBody(Body));
  }
  if (Failed)
    return 0;

  // The netmodule (plan 5.8.2), with the COFF symbol view (5.8.6): the marker
  // sections .data, .rdata and .bss after .text, and one symbol per SYMTAB
  // entry.
  Expected<cli::PEOptions> Opts =
      cli::PEOptions::get(TT, cli::PEImageKind::Module);
  if (!Opts) {
    Fail(toString(Opts.takeError()));
    return 0;
  }
  std::vector<cli::CoffSymbol> CoffSymbols =
      cli::buildCOFFSymbolView(*LinkInfo);
  cli::PEContent Content;
  Content.Metadata = &MB;
  Content.ILStream = Bodies.getBytes();
  Content.ExtraSections = cli::getCOFFViewMarkerSections();
  Content.CoffSymbols = CoffSymbols;
  SmallVector<uint8_t, 0> Image;
  if (Error E = cli::writePEImage(*Opts, Content, Image)) {
    Fail(toString(std::move(E)));
    return 0;
  }
  OS.write(reinterpret_cast<const char *>(Image.data()), Image.size());
  return Image.size();
}

std::unique_ptr<MCObjectTargetWriter> llvm::createMSILObjectTargetWriter() {
  return std::make_unique<MSILObjectWriter>();
}
