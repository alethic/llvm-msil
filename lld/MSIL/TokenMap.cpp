//===- TokenMap.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TokenMap.h"
#include "InputChunks.h"
#include "InputFiles.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/ILDecoder.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/CLI/Signature.h"
#include "llvm/Support/Error.h"

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::cli;
using namespace lld;
using namespace lld::msil;

namespace MSILLink = llvm::MSILLink;

bool TokenTarget::isRedirected() const {
  if (kind != Method || form != Definition)
    return false;
  auto *def = dyn_cast_or_null<DefinedFunction>(sym);
  return !def || def->method != method;
}

bool TokenTarget::isManaged() const {
  return (kind == Method || kind == Field || kind == FnAddr) && sym &&
         sym->isManaged();
}

uint32_t lld::msil::getInstructionOffset(const TokenSite &site) {
  return site.offset - getOpcodeSize(site.op);
}

void lld::msil::rewriteManagedAddressSites(const ObjFile &file,
                                           const InputMethod &m,
                                           MutableArrayRef<uint8_t> code) {
  const TokenMap &tokens = file.getTokenMap();
  for (const TokenSite &site : m.sites) {
    if (site.op != Opcode::Ldsfld ||
        !tokens.lookup(site.token).isManagedAddress())
      continue;
    // ldsfld and call are one-byte opcodes followed by the token.
    uint32_t at = getInstructionOffset(site);
    assert(at < code.size() && code[at] == 0x7E && "not an ldsfld");
    code[at] = 0x28;
  }
}

// The tables whose tokens an operand of kind \p kind may name (III.1.2).
static bool isAllowedTable(OperandKind kind, uint8_t type) {
  auto is = [&](TableId t) { return type == static_cast<uint8_t>(t); };
  switch (kind) {
  case OperandKind::InlineMethod:
    return is(TableId::MethodDef) || is(TableId::MemberRef) ||
           is(TableId::MethodSpec);
  case OperandKind::InlineField:
    return is(TableId::Field) || is(TableId::MemberRef);
  case OperandKind::InlineType:
    return is(TableId::TypeDef) || is(TableId::TypeRef) ||
           is(TableId::TypeSpec);
  case OperandKind::InlineTok:
    return is(TableId::TypeDef) || is(TableId::TypeRef) ||
           is(TableId::TypeSpec) || is(TableId::MethodDef) ||
           is(TableId::MemberRef) || is(TableId::MethodSpec) ||
           is(TableId::Field);
  case OperandKind::InlineSig:
    return is(TableId::StandAloneSig);
  case OperandKind::InlineString:
    return type == UserStringTokenType;
  default:
    return false;
  }
}

static bool hasTokenOperand(OperandKind kind) {
  switch (kind) {
  case OperandKind::InlineMethod:
  case OperandKind::InlineField:
  case OperandKind::InlineType:
  case OperandKind::InlineTok:
  case OperandKind::InlineSig:
  case OperandKind::InlineString:
    return true;
  default:
    return false;
  }
}

// True if \p token names an existing row of a table that \p kind allows, or
// an offset inside the #US heap.
static bool isValidToken(const MetadataReader &md, OperandKind kind,
                         uint32_t token) {
  uint8_t type = getTokenType(token);
  uint32_t row = getTokenRow(token);
  if (!isAllowedTable(kind, type))
    return false;
  if (type == UserStringTokenType)
    return row < md.getHeapSize("#US");
  return row != 0 && row <= md.getRowCount(static_cast<TableId>(type));
}

bool lld::msil::decodeMethod(ObjFile &file, InputMethod &m) {
  const MetadataReader &md = file.getReader();
  auto fail = [&](uint64_t offset) {
    error(toString(&file) + ": malformed method body in '" + m.name +
          "' at IL offset " + Twine(offset));
    return false;
  };

  Expected<MethodBodyView> body = md.getMethodBody(m.token);
  if (!body) {
    consumeError(body.takeError());
    return fail(0);
  }
  m.body = std::move(*body);

  // The local signature and the catch class tokens are tokens too.
  if (uint32_t sig = m.body.LocalVarSigToken)
    if (getTokenType(sig) != static_cast<uint8_t>(TableId::StandAloneSig) ||
        !isValidToken(md, OperandKind::InlineSig, sig))
      return fail(0);
  for (const ExceptionClause &c : m.body.Clauses)
    if (c.Kind == ExceptionClauseKind::Catch &&
        !isValidToken(md, OperandKind::InlineType, c.ClassTokenOrFilterOffset))
      return fail(c.HandlerOffset);

  // Every token operand, in code order. A decoding failure lies at the end
  // of the last decoded instruction; a bad token at its instruction.
  uint32_t next = 0;
  std::optional<uint32_t> badToken;
  Error e = decodeIL(m.body.Code, [&](const ILInstruction &inst) -> Error {
    next = inst.Offset + inst.Size;
    OperandKind kind = getOpcodeInfo(inst.Op).Operand;
    if (!hasTokenOperand(kind))
      return Error::success();
    if (!isValidToken(md, kind, inst.Token)) {
      badToken = inst.Offset;
      return createStringError(inconvertibleErrorCode(), "bad token");
    }
    m.sites.push_back({inst.TokenOffset, inst.Token, inst.Op});
    return Error::success();
  });
  if (e) {
    consumeError(std::move(e));
    m.sites.clear();
    return fail(badToken ? *badToken : next);
  }
  return true;
}

bool TokenMap::build(ObjFile &file) {
  const MetadataReader &md = file.getReader();
  bool ok = true;
  auto check = [&](Error e) {
    if (!e)
      return true;
    error(toString(&file) + ": " + toString(std::move(e)));
    ok = false;
    return false;
  };
  auto bind = [&](TokenTarget &t, StringRef name) {
    t.symbolIndex = file.findSymbolIndex(name);
    if (t.symbolIndex >= 0)
      t.sym = file.getSymbol(t.symbolIndex);
  };
  // The decoded method signature of a blob, kept for the link.
  auto decodeSig = [&](uint32_t blobOffset,
                       StringRef name) -> const MethodSig * {
    Expected<ArrayRef<uint8_t>> blob = md.getBlob(blobOffset);
    if (!check(blob.takeError()))
      return nullptr;
    Expected<MethodSig> sig = decodeMethodSig(*blob);
    if (!sig) {
      error(toString(&file) + ": '" + name + "': " + toString(sig.takeError()));
      ok = false;
      return nullptr;
    }
    return make<MethodSig>(std::move(*sig));
  };

  // MethodDefs: the symbol of the row's name (R1); with a body, the object's
  // own method, else an import stub.
  methodDefs.resize(md.getRowCount(TableId::MethodDef));
  for (uint32_t row = 1; row <= methodDefs.size(); ++row) {
    TokenTarget &t = methodDefs[row - 1];
    t.token = makeToken(TableId::MethodDef, row);
    Expected<MethodDefRow> def = md.getMethodDef(row);
    if (!check(def.takeError()))
      continue;
    t.kind = TokenTarget::Method;
    bind(t, def->Name);
    t.signatureInfo = file.getSignatureInfo(t.token);
    if (InputMethod *m = file.getMethod(t.token)) {
      t.form = TokenTarget::Definition;
      t.method = m;
      t.signature = m->signature;
    } else {
      t.form = TokenTarget::Stub;
      t.signature = decodeSig(def->Signature, def->Name);
    }
  }

  // FieldDefs: the address field of the symbol of the row's name.
  fields.resize(md.getRowCount(TableId::Field));
  for (uint32_t row = 1; row <= fields.size(); ++row) {
    TokenTarget &t = fields[row - 1];
    t.token = makeToken(TableId::Field, row);
    Expected<FieldRow> field = md.getField(row);
    if (!check(field.takeError()))
      continue;
    t.kind = TokenTarget::Field;
    t.form = TokenTarget::Definition;
    bind(t, field->Name);
  }

  // TypeRefs: the name of the AssemblyRef that scopes each one. The
  // scaffolding of the placeholders and of the carrier is no BCL request.
  SmallVector<StringRef, 0> typeRefScopes;
  typeRefs.resize(md.getRowCount(TableId::TypeRef));
  typeRefScopes.resize(typeRefs.size());
  SmallVector<TokenTarget::Kind, 0> placeholderKinds(typeRefs.size(),
                                                     TokenTarget::None);
  for (uint32_t row = 1; row <= typeRefs.size(); ++row) {
    TokenTarget &t = typeRefs[row - 1];
    t.token = makeToken(TableId::TypeRef, row);
    Expected<TypeRefRow> ref = md.getTypeRef(row);
    if (!check(ref.takeError()))
      continue;
    StringRef scope;
    if (getTokenType(ref->ResolutionScope) ==
        static_cast<uint8_t>(TableId::AssemblyRef)) {
      Expected<AssemblyRefRow> asmRef =
          md.getAssemblyRef(getTokenRow(ref->ResolutionScope));
      if (!check(asmRef.takeError()))
        continue;
      scope = asmRef->Name;
    }
    typeRefScopes[row - 1] = scope;
    if (scope == MSILLink::ExternAssembly && ref->Namespace.empty()) {
      if (ref->Name == MSILLink::SymbolsType)
        placeholderKinds[row - 1] = TokenTarget::Method;
      else if (ref->Name == MSILLink::FnAddrType)
        placeholderKinds[row - 1] = TokenTarget::FnAddr;
      continue;
    }
    if (scope == MSILLink::ExternAssembly ||
        scope == MSILLink::LinkInfoAssembly)
      continue;
    t.kind = TokenTarget::BCL;
    t.scope = scope;
  }

  // MemberRefs: placeholders on '<msil>Symbols' (a method or a field by its
  // signature) and '<msil>FnAddr', and BCL requests (C5, C6).
  memberRefs.resize(md.getRowCount(TableId::MemberRef));
  for (uint32_t row = 1; row <= memberRefs.size(); ++row) {
    TokenTarget &t = memberRefs[row - 1];
    t.token = makeToken(TableId::MemberRef, row);
    Expected<MemberRefRow> ref = md.getMemberRef(row);
    if (!check(ref.takeError()))
      continue;
    Expected<ArrayRef<uint8_t>> blob = md.getBlob(ref->Signature);
    if (!check(blob.takeError()))
      continue;
    bool isField = !blob->empty() && ((*blob)[0] & SIG_KIND_MASK) == SIG_FIELD;
    if (getTokenType(ref->Parent) != static_cast<uint8_t>(TableId::TypeRef)) {
      t.kind = TokenTarget::BCL;
      continue;
    }
    uint32_t parent = getTokenRow(ref->Parent);
    if (parent == 0 || parent > typeRefs.size())
      continue;
    TokenTarget::Kind placeholder = placeholderKinds[parent - 1];
    if (placeholder == TokenTarget::FnAddr) {
      t.kind = TokenTarget::FnAddr;
      t.form = TokenTarget::Placeholder;
      bind(t, ref->Name);
      continue;
    }
    if (placeholder == TokenTarget::Method) {
      t.kind = isField ? TokenTarget::Field : TokenTarget::Method;
      t.form = TokenTarget::Placeholder;
      bind(t, ref->Name);
      if (!isField) {
        t.signature = decodeSig(ref->Signature, ref->Name);
        t.signatureInfo = file.getSignatureInfo(t.token);
      }
      continue;
    }
    if (typeRefs[parent - 1].kind != TokenTarget::BCL)
      continue; // the carrier's constructor
    t.kind = TokenTarget::BCL;
    t.scope = typeRefScopes[parent - 1];
    if (!isField)
      t.signature = decodeSig(ref->Signature, ref->Name);
  }

  // TypeDefs ('<Module>'), TypeSpecs and MethodSpecs.
  typeDefs.resize(md.getRowCount(TableId::TypeDef));
  for (uint32_t row = 1; row <= typeDefs.size(); ++row) {
    typeDefs[row - 1].token = makeToken(TableId::TypeDef, row);
    typeDefs[row - 1].kind = TokenTarget::TypeDef;
  }
  typeSpecs.resize(md.getRowCount(TableId::TypeSpec));
  for (uint32_t row = 1; row <= typeSpecs.size(); ++row) {
    typeSpecs[row - 1].token = makeToken(TableId::TypeSpec, row);
    typeSpecs[row - 1].kind = TokenTarget::BCL;
  }
  methodSpecs.resize(md.getRowCount(TableId::MethodSpec));
  for (uint32_t row = 1; row <= methodSpecs.size(); ++row) {
    methodSpecs[row - 1].token = makeToken(TableId::MethodSpec, row);
    methodSpecs[row - 1].kind = TokenTarget::BCL;
  }

  // StandAloneSigs: their blobs.
  standAloneSigs.resize(md.getRowCount(TableId::StandAloneSig));
  for (uint32_t row = 1; row <= standAloneSigs.size(); ++row) {
    TokenTarget &t = standAloneSigs[row - 1];
    t.token = makeToken(TableId::StandAloneSig, row);
    Expected<StandAloneSigRow> sig = md.getStandAloneSig(row);
    if (!check(sig.takeError()))
      continue;
    Expected<ArrayRef<uint8_t>> blob = md.getBlob(sig->Signature);
    if (!check(blob.takeError()))
      continue;
    t.kind = TokenTarget::StandAloneSig;
    t.blob = *blob;
  }
  return ok;
}

TokenTarget TokenMap::lookup(uint32_t token) const {
  uint8_t type = getTokenType(token);
  uint32_t row = getTokenRow(token);
  if (type == UserStringTokenType) {
    TokenTarget t;
    t.kind = TokenTarget::UserString;
    t.token = token;
    return t;
  }
  const std::vector<TokenTarget> *rows = nullptr;
  switch (static_cast<TableId>(type)) {
  case TableId::MethodDef:
    rows = &methodDefs;
    break;
  case TableId::Field:
    rows = &fields;
    break;
  case TableId::MemberRef:
    rows = &memberRefs;
    break;
  case TableId::TypeRef:
    rows = &typeRefs;
    break;
  case TableId::TypeDef:
    rows = &typeDefs;
    break;
  case TableId::TypeSpec:
    rows = &typeSpecs;
    break;
  case TableId::MethodSpec:
    rows = &methodSpecs;
    break;
  case TableId::StandAloneSig:
    rows = &standAloneSigs;
    break;
  default:
    break;
  }
  if (!rows || row == 0 || row > rows->size()) {
    TokenTarget t;
    t.token = token;
    return t;
  }
  return (*rows)[row - 1];
}
