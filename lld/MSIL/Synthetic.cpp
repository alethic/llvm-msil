//===- Synthetic.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The output model of msil-ld, its BCL requests and the trailer sink (see
// Synthetic.h).
//
//===----------------------------------------------------------------------===//

#include "Synthetic.h"
#include "InputFiles.h"
#include "ReferenceSet.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::cli;
using namespace lld;
using namespace lld::msil;

namespace MSILLink = llvm::MSILLink;

uint32_t OutputModule::addMethod(OutputMethod m) {
  methods.push_back(std::move(m));
  return methods.size() - 1;
}

uint32_t OutputModule::addField(OutputField f) {
  fields.push_back(std::move(f));
  return fields.size() - 1;
}

std::optional<uint32_t> OutputModule::getMethodOf(const InputMethod *m) const {
  auto it = methodOf.find(m);
  if (it == methodOf.end())
    return std::nullopt;
  return it->second;
}

EntityRef OutputModule::addEntity(Entity e) {
  entities.push_back(e);
  return entities.size();
}

EntityRef OutputModule::getMethodEntity(uint32_t index) {
  EntityRef &ref = methodEntities[index];
  if (!ref)
    ref = addEntity({Entity::Method, index});
  return ref;
}

EntityRef OutputModule::getFieldEntity(uint32_t index) {
  EntityRef &ref = fieldEntities[index];
  if (!ref)
    ref = addEntity({Entity::Field, index});
  return ref;
}

EntityRef OutputModule::getTokenEntity(uint32_t token) {
  EntityRef &ref = tokenEntities[token];
  if (!ref)
    ref = addEntity({Entity::Token, token});
  return ref;
}

Expected<uint32_t> OutputModule::resolveEntity(EntityRef ref) const {
  if (ref == 0 || ref > entities.size())
    return createStringError(inconvertibleErrorCode(),
                             "unknown trailer entity " + Twine(ref));
  const Entity &e = entities[ref - 1];
  switch (e.kind) {
  case Entity::Method:
    return mb.getToken(methods[e.value].handle);
  case Entity::Field:
    return mb.getToken(fields[e.value].handle);
  case Entity::Token:
    return e.value;
  }
  llvm_unreachable("unknown entity kind");
}

Expected<uint32_t> OutputModule::resolveToken(TokenRef ref) const {
  if (ref.getKind() == TokenRef::Kind::External)
    return resolveEntity(ref.getValue());
  return mb.resolveToken(ref);
}

uint32_t OutputModule::getToken(OutRef ref) const {
  switch (ref.kind) {
  case OutRef::Method:
    return mb.getToken(methods[ref.value].handle);
  case OutRef::Field:
    return mb.getToken(fields[ref.value].handle);
  case OutRef::Token:
    return ref.value;
  case OutRef::None:
    break;
  }
  return 0;
}

std::string OutputModule::getSignatureKey(const MethodSig &sig) {
  SmallVector<uint8_t, 16> bytes;
  encodeMethodSig(sig, bytes);
  return std::string(bytes.begin(), bytes.end());
}

uint32_t OutputModule::addLocalSig(ArrayRef<TypeSig> locals) {
  if (locals.empty())
    return 0;
  SmallVector<uint8_t, 16> blob;
  encodeLocalSig(locals, blob);
  return mb.getToken(mb.addStandAloneSig(mb.addBlob(blob)));
}

//===----------------------------------------------------------------------===//
// BCL references (plan 5.9.7, 5.11)
//===----------------------------------------------------------------------===//

// A type of BCLMembers.def's signature text (canonical listing v1): a
// primitive, a pointer or an SZARRAY of one, or a class or value type that
// \p bindType binds. \p what names the signature in errors.
static Expected<TypeSig> parseBCLType(StringRef text, bool allowVoid,
                                      BCLTypeBinder bindType, StringRef what) {
  auto fail = [&]() {
    return createStringError(inconvertibleErrorCode(),
                             "cannot read the signature of " + what + " at '" +
                                 text + "'");
  };
  text = text.trim();
  if (text.consume_back("*")) {
    Expected<TypeSig> pointee =
        parseBCLType(text, /*allowVoid=*/true, bindType, what);
    if (!pointee)
      return pointee.takeError();
    return TypeSig::getPointer(*pointee);
  }
  if (text.consume_back("[]")) {
    Expected<TypeSig> element =
        parseBCLType(text, /*allowVoid=*/false, bindType, what);
    if (!element)
      return element.takeError();
    return TypeSig::getSZArray(*element);
  }
  if (text == "void") {
    if (!allowVoid)
      return fail();
    return TypeSig();
  }
  bool isValueType = text.consume_front("valuetype ");
  if (isValueType || text.consume_front("class ")) {
    // "Ns.Name", a nested type as "Ns.Outer/Inner".
    StringRef full = text.trim();
    auto [ns, name] = splitTypeName(full);
    if (!bindType)
      return createStringError(inconvertibleErrorCode(),
                               "the signature of " + what +
                                   " names the type '" + full +
                                   "', which is not bound here");
    Expected<uint32_t> token = bindType(ns, name, isValueType);
    if (!token)
      return token.takeError();
    return isValueType ? TypeSig::getValueType(*token)
                       : TypeSig::getClass(*token);
  }
  static constexpr std::pair<StringLiteral, ElementType> primitives[] = {
      {"bool", ELEMENT_TYPE_BOOLEAN},
      {"char", ELEMENT_TYPE_CHAR},
      {"int8", ELEMENT_TYPE_I1},
      {"uint8", ELEMENT_TYPE_U1},
      {"int16", ELEMENT_TYPE_I2},
      {"uint16", ELEMENT_TYPE_U2},
      {"int32", ELEMENT_TYPE_I4},
      {"uint32", ELEMENT_TYPE_U4},
      {"int64", ELEMENT_TYPE_I8},
      {"uint64", ELEMENT_TYPE_U8},
      {"float32", ELEMENT_TYPE_R4},
      {"float64", ELEMENT_TYPE_R8},
      {"native int", ELEMENT_TYPE_I},
      {"native uint", ELEMENT_TYPE_U},
      {"string", ELEMENT_TYPE_STRING},
      {"object", ELEMENT_TYPE_OBJECT},
      {"typedref", ELEMENT_TYPE_TYPEDBYREF},
  };
  for (const auto &[name, type] : primitives)
    if (text == name)
      return TypeSig::get(type);
  return fail();
}

Expected<MethodSig> lld::msil::parseBCLMethodSig(StringRef ret,
                                                 StringRef params, bool hasThis,
                                                 StringRef what,
                                                 BCLTypeBinder bindType) {
  MethodSig sig;
  sig.Header = hasThis ? SIG_HASTHIS : SIG_DEFAULT;
  Expected<TypeSig> retType =
      parseBCLType(ret, /*allowVoid=*/true, bindType, what);
  if (!retType)
    return retType.takeError();
  sig.Return = *retType;
  params = params.trim();
  if (!params.empty()) {
    SmallVector<StringRef, 4> parts;
    params.split(parts, ',');
    for (StringRef part : parts) {
      Expected<TypeSig> type =
          parseBCLType(part, /*allowVoid=*/false, bindType, what);
      if (!type)
        return type.takeError();
      sig.Params.push_back(*type);
    }
  }
  return sig;
}

Expected<MethodSig> lld::msil::parseBCLMemberSig(const BCLMemberInfo &info,
                                                 BCLTypeBinder bindType) {
  assert(info.isMethod() && "not a BCL method");
  return parseBCLMethodSig(info.Return, info.Params, info.HasThis,
                           ("BCL member '" + info.IdName + "'").str(),
                           bindType);
}

Expected<BCLRequest> lld::msil::getBCLMemberRequest(BCLMemberId id,
                                                    BCLTypeBinder bindType) {
  const BCLMemberInfo &info = getBCLMemberInfo(id);
  BCLRequest r;
  if (info.isType()) {
    r.K = BCLRequest::Type;
    r.Namespace = info.Namespace;
    r.Name = info.Name;
    return r;
  }
  const BCLMemberInfo &owner = getBCLMemberInfo(info.Owner);
  r.K = BCLRequest::Method;
  r.Strength = static_cast<RequestStrength>(info.Strength);
  r.Namespace = owner.Namespace;
  r.Name = owner.Name;
  r.Member = info.Name;
  Expected<MethodSig> sig = parseBCLMemberSig(info, bindType);
  if (!sig)
    return sig.takeError();
  r.MethodSignature = std::move(*sig);
  return r;
}

void OutputModule::reportOnce(const Twine &msg) {
  std::string s = msg.str();
  if (reported.try_emplace(s, true).second)
    error(s);
}

void OutputModule::reportUnsupported(ObjFile &f, const Twine &what) {
  reportOnce(toString(&f) + ": " + what);
}

uint32_t OutputModule::bind(const BCLRequest &r) {
  assert(binder && "the writer sets the binder before it binds a token");
  Expected<std::optional<EntityHandle>> handle = binder->bind(r);
  if (!handle) {
    handleAllErrors(handle.takeError(),
                    [&](const ErrorInfoBase &e) { reportOnce(e.message()); });
    return 0;
  }
  // The objects' requests and msil-ld's own are Required: they never fall
  // back (plan 5.11.5).
  assert(*handle && "a Required request fell back");
  return *handle ? (*handle)->Value : 0;
}

uint32_t OutputModule::getBCLType(BCLMemberId id) {
  assert(getBCLMemberInfo(id).isType() && "not a BCL type");
  return getBCLMember(id);
}

uint32_t OutputModule::getBCLMember(BCLMemberId id) {
  assert(binder && "the writer sets the binder before the trailer");
  Expected<EntityHandle> handle = binder->bindMember(id);
  if (!handle) {
    handleAllErrors(handle.takeError(),
                    [&](const ErrorInfoBase &e) { reportOnce(e.message()); });
    return 0;
  }
  return handle->Value;
}

Expected<std::pair<std::string, std::string>>
OutputModule::getTypeRefName(ObjFile &f, uint32_t token) {
  const MetadataReader &md = f.getReader();
  // The outermost TypeRef must be scoped to an assembly other than the
  // placeholders msil.extern and msil.link.
  for (uint32_t outer = token, depth = 0;; ++depth) {
    Expected<TypeRefRow> row = md.getTypeRef(getTokenRow(outer));
    if (!row)
      return row.takeError();
    if (depth > maxTypeNestingDepth)
      return createStringError(inconvertibleErrorCode(),
                               "nested type reference '" + row->Name +
                                   "' is too deep");
    uint32_t scope = row->ResolutionScope;
    TableId table = static_cast<TableId>(getTokenType(scope));
    if (table == TableId::TypeRef && getTokenRow(scope) != 0) {
      outer = scope;
      continue;
    }
    if (table != TableId::AssemblyRef || getTokenRow(scope) == 0)
      return createStringError(inconvertibleErrorCode(),
                               "type reference '" + row->Name +
                                   "' is not scoped to an assembly");
    Expected<AssemblyRefRow> asmRef = md.getAssemblyRef(getTokenRow(scope));
    if (!asmRef)
      return asmRef.takeError();
    if (asmRef->Name == MSILLink::ExternAssembly ||
        asmRef->Name == MSILLink::LinkInfoAssembly)
      return createStringError(inconvertibleErrorCode(),
                               "type reference '" + row->Name + "' of [" +
                                   asmRef->Name + "] is not a .NET type");
    break;
  }
  // The names as the reference set's index spells them ("Outer/Inner", each
  // nested part with its namespace).
  return getIndexTypeName(md, token);
}

uint32_t OutputModule::mapTypeToken(ObjFile &f, uint32_t token) {
  auto key = std::make_pair(static_cast<const ObjFile *>(&f), token);
  switch (static_cast<TableId>(getTokenType(token))) {
  case TableId::TypeRef: {
    if (auto it = typeTokens.find(key); it != typeTokens.end())
      return it->second;
    uint32_t result = 0;
    Expected<std::pair<std::string, std::string>> name =
        getTypeRefName(f, token);
    if (!name) {
      reportUnsupported(f, toString(name.takeError()));
    } else {
      // A type request, bound by name whatever the AssemblyRef says (plan
      // 5.11.5; msil.bcl in llc's objects).
      BCLRequest r;
      r.K = BCLRequest::Type;
      r.RequestedBy = &f;
      r.Namespace = name->first;
      r.Name = name->second;
      result = bind(r);
    }
    typeTokens[key] = result;
    return result;
  }
  case TableId::TypeDef:
    // Phase 2 objects define only '<Module>' (plan 5.8.2).
    if (getTokenRow(token) == 1)
      return mb.getModuleType().Value;
    reportUnsupported(f, "a type definition other than '<Module>' is not "
                         "supported yet");
    return 0;
  case TableId::TypeSpec: {
    if (auto it = typeTokens.find(key); it != typeTokens.end())
      return it->second;
    const MetadataReader &md = f.getReader();
    Expected<TypeSpecRow> row = md.getTypeSpec(getTokenRow(token));
    if (!row) {
      reportUnsupported(f, toString(row.takeError()));
      return 0;
    }
    Expected<ArrayRef<uint8_t>> blob = md.getBlob(row->Signature);
    if (!blob) {
      reportUnsupported(f, toString(blob.takeError()));
      return 0;
    }
    Expected<TypeSig> type = decodeTypeSpec(*blob);
    if (!type) {
      reportUnsupported(f, toString(type.takeError()));
      return 0;
    }
    bool failed = false;
    TypeSig mapped = mapTypeRefs(*type, [&](uint32_t t) {
      uint32_t r = mapTypeToken(f, t);
      failed |= r == 0;
      return r ? r : t;
    });
    uint32_t result = 0;
    if (!failed) {
      BCLRequest r;
      r.K = BCLRequest::TypeSpec;
      r.RequestedBy = &f;
      r.FieldOrSpec = std::move(mapped);
      result = bind(r);
    }
    typeTokens[key] = result;
    return result;
  }
  default:
    reportUnsupported(f, "token 0x" + utohexstr(token, false, 8) +
                             " is not a type");
    return 0;
  }
}

MethodSig OutputModule::mapSig(ObjFile &f, const MethodSig &sig) {
  return mapTypeRefs(sig, [&](uint32_t t) {
    uint32_t r = mapTypeToken(f, t);
    return r ? r : t;
  });
}

uint32_t OutputModule::mapBCLToken(ObjFile &f, uint32_t token) {
  switch (static_cast<TableId>(getTokenType(token))) {
  case TableId::TypeRef:
  case TableId::TypeSpec:
  case TableId::TypeDef:
    return mapTypeToken(f, token);
  case TableId::MemberRef: {
    const MetadataReader &md = f.getReader();
    Expected<MemberRefRow> row = md.getMemberRef(getTokenRow(token));
    if (!row) {
      reportUnsupported(f, toString(row.takeError()));
      return 0;
    }
    if (getTokenType(row->Parent) != static_cast<uint8_t>(TableId::TypeRef)) {
      reportUnsupported(f, "member reference '" + row->Name +
                               "' has a parent other than a type reference, "
                               "which is not supported yet");
      return 0;
    }
    // The parent's TypeRef first, as M20's stand-in made it, so that a link
    // without references keeps its row order (plan 5.11.7).
    if (!mapTypeToken(f, row->Parent))
      return 0;
    Expected<std::pair<std::string, std::string>> parent =
        getTypeRefName(f, row->Parent);
    if (!parent) {
      reportUnsupported(f, toString(parent.takeError()));
      return 0;
    }
    Expected<ArrayRef<uint8_t>> blob = md.getBlob(row->Signature);
    if (!blob) {
      reportUnsupported(f, toString(blob.takeError()));
      return 0;
    }
    bool failed = false;
    auto map = [&](uint32_t t) {
      uint32_t r = mapTypeToken(f, t);
      failed |= r == 0;
      return r ? r : t;
    };
    // A method or field request on the parent, with the object's signature
    // in output tokens (plan 5.11.4, 5.11.5).
    BCLRequest r;
    r.RequestedBy = &f;
    r.Namespace = parent->first;
    r.Name = parent->second;
    r.Member = row->Name;
    if (!blob->empty() && ((*blob)[0] & SIG_KIND_MASK) == SIG_FIELD) {
      Expected<TypeSig> type = decodeFieldSig(*blob);
      if (!type) {
        reportUnsupported(f,
                          "'" + row->Name + "': " + toString(type.takeError()));
        return 0;
      }
      r.K = BCLRequest::Field;
      r.FieldOrSpec = mapTypeRefs(*type, map);
    } else {
      Expected<MethodSig> sig = decodeMethodSig(*blob);
      if (!sig) {
        reportUnsupported(f,
                          "'" + row->Name + "': " + toString(sig.takeError()));
        return 0;
      }
      r.K = BCLRequest::Method;
      r.MethodSignature = mapTypeRefs(*sig, map);
    }
    if (failed)
      return 0;
    return bind(r);
  }
  default:
    // MethodSpecs (generic method instantiations) do not occur in C (plan
    // 5.9.7).
    reportUnsupported(f, "token 0x" + utohexstr(token, false, 8) +
                             " names a generic method instantiation, which "
                             "is not supported yet");
    return 0;
  }
}

uint32_t OutputModule::mapStandAloneSig(ObjFile &f, ArrayRef<uint8_t> blob) {
  bool failed = false;
  auto map = [&](uint32_t t) {
    uint32_t r = mapTypeToken(f, t);
    failed |= r == 0;
    return r ? r : t;
  };
  SmallVector<uint8_t, 16> bytes;
  if (!blob.empty() && (blob[0] & SIG_KIND_MASK) == SIG_LOCAL) {
    Expected<std::vector<TypeSig>> locals = decodeLocalSig(blob);
    if (!locals) {
      reportUnsupported(f, toString(locals.takeError()));
      return 0;
    }
    std::vector<TypeSig> mapped;
    for (const TypeSig &t : *locals)
      mapped.push_back(mapTypeRefs(t, map));
    encodeLocalSig(mapped, bytes);
  } else {
    Expected<MethodSig> sig = decodeMethodSig(blob);
    if (!sig) {
      reportUnsupported(f, toString(sig.takeError()));
      return 0;
    }
    encodeMethodSig(mapTypeRefs(*sig, map), bytes);
  }
  if (failed)
    return 0;
  return mb.getToken(mb.addStandAloneSig(mb.addBlob(bytes)));
}

uint32_t OutputModule::mapUserString(ObjFile &f, uint32_t token) {
  Expected<std::string> str = f.getReader().getUserString(getTokenRow(token));
  if (!str) {
    reportUnsupported(f, toString(str.takeError()));
    return 0;
  }
  return mb.addUserString(*str);
}

//===----------------------------------------------------------------------===//
// The trailer sink (plan 5.9.10)
//===----------------------------------------------------------------------===//

EntityRef RowTrailerSink::getDataField() {
  if (!dataField) {
    // Phase 1's '.field private static initonly native int '<msil>data''.
    OutputField f(OutputField::Trailer);
    f.name = DataFieldName.str();
    f.flags = FieldAttributes::Private | FieldAttributes::Static |
              FieldAttributes::InitOnly;
    dataField = out.addField(std::move(f));
  }
  return out.getFieldEntity(*dataField);
}

EntityRef RowTrailerSink::addRVAData(StringRef typeName, StringRef fieldName,
                                     ArrayRef<uint8_t> bytes) {
  // Phase 1's '.field private static valuetype '<type>' '<field>' at ...',
  // whose type is an explicit-layout value type of the data's size.
  OutputField f(OutputField::Trailer);
  f.name = fieldName.str();
  f.flags = FieldAttributes::Private | FieldAttributes::Static |
            FieldAttributes::HasFieldRVA;
  f.rva = out.rvaData.size();
  uint32_t index = out.addField(std::move(f));
  out.rvaData.push_back({typeName.str(),
                         std::vector<uint8_t>(bytes.begin(), bytes.end()),
                         index, EntityHandle()});
  return out.getFieldEntity(index);
}

EntityRef RowTrailerSink::getBCLMember(BCLMemberId id) {
  return out.getTokenEntity(out.getBCLMember(id));
}

EntityRef RowTrailerSink::declareHelper(StringRef name, const MethodSig &sig) {
  auto [it, inserted] = helpers.try_emplace(name, 0);
  if (inserted) {
    OutputMethod m(OutputMethod::Trailer);
    m.name = name.str();
    m.sig = sig;
    it->second = out.addMethod(std::move(m));
  }
  return out.getMethodEntity(it->second);
}

EntityRef RowTrailerSink::addUserString(StringRef utf8) {
  return out.getTokenEntity(out.mb.addUserString(utf8));
}

void RowTrailerSink::emitMethod(const TrailerMethod &tm) {
  uint32_t index;
  if (auto it = helpers.find(tm.Name); it != helpers.end()) {
    index = it->second;
  } else {
    OutputMethod m(OutputMethod::Trailer);
    m.name = tm.Name;
    index = out.addMethod(std::move(m));
    helpers[tm.Name] = index;
  }
  OutputMethod &m = out.methods[index];
  m.flags = tm.Flags;
  m.implFlags = MethodImplAttributes::IL | MethodImplAttributes::Managed;
  m.sig = tm.Sig;
  m.body = std::make_unique<ILBuilder>(tm.Body);
  m.localSigToken = out.addLocalSig(tm.Locals);
}
