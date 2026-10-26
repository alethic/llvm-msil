//===- Synthetic.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The output model of msil-ld, the BCL stand-in and the trailer sink (see
// Synthetic.h).
//
//===----------------------------------------------------------------------===//

#include "Synthetic.h"
#include "InputFiles.h"
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
                             "msil-ld: unknown trailer entity " + Twine(ref));
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
// The BCL stand-in (plan 5.9.7)
//===----------------------------------------------------------------------===//

// [mscorlib] 4.0.0.0, public key token b77a5c561934e089, flags 0 (D10).
uint32_t OutputModule::getMscorlib() {
  if (mscorlib)
    return mscorlib;
  AssemblyRefInfo ref;
  ref.Name = MscorlibAssembly.str();
  ref.Version.Major = 4;
  static constexpr uint8_t token[] = {0xB7, 0x7A, 0x5C, 0x56,
                                      0x19, 0x34, 0xE0, 0x89};
  ref.PublicKeyOrToken.assign(std::begin(token), std::end(token));
  mscorlib = mb.getToken(mb.addAssemblyRef(ref));
  return mscorlib;
}

uint32_t OutputModule::getBCLType(BCLMemberId id) {
  const BCLMemberInfo &info = getBCLMemberInfo(id);
  assert(info.isType() && "not a BCL type");
  uint32_t &token = bclTokens[static_cast<unsigned>(id)];
  if (token)
    return token;
  // A nested type 'Outer/Inner' is scoped to its enclosing type's TypeRef.
  SmallVector<StringRef, 2> names;
  StringRef(info.Name).split(names, '/');
  EntityHandle scope{getMscorlib()};
  EntityHandle ref = mb.addTypeRef(scope, info.Namespace, names[0]);
  for (StringRef inner : drop_begin(names))
    ref = mb.addTypeRef(ref, "", inner);
  token = ref.Value;
  return token;
}

// A type of BCLMembers.def's signature text (canonical listing v1): a
// primitive, a pointer or an SZARRAY of one.
static std::optional<TypeSig> parseBCLType(StringRef text, bool allowVoid) {
  text = text.trim();
  if (text.consume_back("*")) {
    std::optional<TypeSig> pointee = parseBCLType(text, /*allowVoid=*/true);
    if (!pointee)
      return std::nullopt;
    return TypeSig::getPointer(*pointee);
  }
  if (text.consume_back("[]")) {
    std::optional<TypeSig> element = parseBCLType(text, /*allowVoid=*/false);
    if (!element)
      return std::nullopt;
    return TypeSig::getSZArray(*element);
  }
  if (text == "void") {
    if (!allowVoid)
      return std::nullopt;
    return TypeSig();
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
  return std::nullopt;
}

uint32_t OutputModule::getBCLMember(BCLMemberId id) {
  const BCLMemberInfo &info = getBCLMemberInfo(id);
  if (info.isType())
    return getBCLType(id);
  uint32_t &token = bclTokens[static_cast<unsigned>(id)];
  if (token)
    return token;
  MethodSig sig;
  sig.Header = info.HasThis ? SIG_HASTHIS : SIG_DEFAULT;
  std::optional<TypeSig> ret = parseBCLType(info.Return, /*allowVoid=*/true);
  bool ok = ret.has_value();
  if (ret)
    sig.Return = *ret;
  StringRef params = StringRef(info.Params).trim();
  if (!params.empty()) {
    SmallVector<StringRef, 4> parts;
    params.split(parts, ',');
    for (StringRef part : parts) {
      std::optional<TypeSig> type = parseBCLType(part, /*allowVoid=*/false);
      ok &= type.has_value();
      if (type)
        sig.Params.push_back(*type);
    }
  }
  if (!ok)
    reportFatalInternalError(
        Twine("msil-ld: cannot encode the signature of BCL member '") +
        info.IdName + "'");
  SmallVector<uint8_t, 16> blob;
  encodeMethodSig(sig, blob);
  EntityHandle parent{getBCLType(info.Owner)};
  token = mb.getToken(mb.addMemberRef(parent, info.Name, mb.addBlob(blob)));
  return token;
}

void OutputModule::reportUnsupported(ObjFile &f, const Twine &what) {
  std::string msg = (toString(&f) + ": " + what).str();
  if (reported.try_emplace(msg, true).second)
    error(msg);
}

// A TypeRef row of \p f in the output (\p depth bounds a cycle of nested
// TypeRefs in a malformed object).
uint32_t OutputModule::mapTypeRef(ObjFile &f, uint32_t token, unsigned depth) {
  auto key = std::make_pair(static_cast<const ObjFile *>(&f), token);
  if (auto it = typeTokens.find(key); it != typeTokens.end())
    return it->second;
  const MetadataReader &md = f.getReader();
  Expected<TypeRefRow> row = md.getTypeRef(getTokenRow(token));
  if (!row) {
    reportUnsupported(f, toString(row.takeError()));
    return 0;
  }
  uint32_t scope = row->ResolutionScope;
  uint32_t result = 0;
  switch (static_cast<TableId>(getTokenType(scope))) {
  case TableId::AssemblyRef: {
    Expected<AssemblyRefRow> asmRef = md.getAssemblyRef(getTokenRow(scope));
    if (!asmRef) {
      reportUnsupported(f, toString(asmRef.takeError()));
      return 0;
    }
    if (asmRef->Name != MSILLink::BCLAssembly) {
      reportUnsupported(f, "reference to assembly '" + asmRef->Name +
                               "' needs --reference, which this msil-ld "
                               "does not support yet");
      return 0;
    }
    result = mb.getToken(
        mb.addTypeRef(EntityHandle{getMscorlib()}, row->Namespace, row->Name));
    break;
  }
  case TableId::TypeRef: {
    if (depth > 64) {
      reportUnsupported(f, "nested type reference '" + row->Name +
                               "' is too deep");
      return 0;
    }
    uint32_t outer = mapTypeRef(f, scope, depth + 1);
    if (!outer)
      return 0;
    result = mb.getToken(
        mb.addTypeRef(EntityHandle{outer}, row->Namespace, row->Name));
    break;
  }
  default:
    reportUnsupported(f, "type reference '" + row->Name +
                             "' is not scoped to an assembly, which this "
                             "msil-ld does not support yet");
    return 0;
  }
  typeTokens[key] = result;
  return result;
}

uint32_t OutputModule::mapTypeToken(ObjFile &f, uint32_t token) {
  switch (static_cast<TableId>(getTokenType(token))) {
  case TableId::TypeRef:
    return mapTypeRef(f, token, 0);
  case TableId::TypeDef:
    // Phase 2 objects define only '<Module>' (plan 5.8.2).
    if (getTokenRow(token) == 1)
      return mb.getModuleType().Value;
    reportUnsupported(f, "a type definition other than '<Module>' is not "
                         "supported yet");
    return 0;
  case TableId::TypeSpec: {
    auto key = std::make_pair(static_cast<const ObjFile *>(&f), token);
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
    if (failed)
      return 0;
    SmallVector<uint8_t, 16> bytes;
    encodeTypeSpec(mapped, bytes);
    uint32_t result = mb.getToken(mb.addTypeSpec(mb.addBlob(bytes)));
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
    uint8_t parentType = getTokenType(row->Parent);
    if (parentType != static_cast<uint8_t>(TableId::TypeRef) &&
        parentType != static_cast<uint8_t>(TableId::TypeSpec)) {
      reportUnsupported(f, "member reference '" + row->Name +
                               "' has a parent that this msil-ld does not "
                               "support yet");
      return 0;
    }
    uint32_t parent = mapTypeToken(f, row->Parent);
    if (!parent)
      return 0;
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
    SmallVector<uint8_t, 16> bytes;
    if (!blob->empty() && ((*blob)[0] & SIG_KIND_MASK) == SIG_FIELD) {
      Expected<TypeSig> type = decodeFieldSig(*blob);
      if (!type) {
        reportUnsupported(f,
                          "'" + row->Name + "': " + toString(type.takeError()));
        return 0;
      }
      encodeFieldSig(mapTypeRefs(*type, map), bytes);
    } else {
      Expected<MethodSig> sig = decodeMethodSig(*blob);
      if (!sig) {
        reportUnsupported(f,
                          "'" + row->Name + "': " + toString(sig.takeError()));
        return 0;
      }
      encodeMethodSig(mapTypeRefs(*sig, map), bytes);
    }
    if (failed)
      return 0;
    return mb.getToken(
        mb.addMemberRef(EntityHandle{parent}, row->Name, mb.addBlob(bytes)));
  }
  default:
    // MethodSpecs (generic instantiations) do not occur in C (plan 5.9.7).
    reportUnsupported(f, "token 0x" + utohexstr(token, false, 8) +
                             " names a generic instantiation, which this "
                             "msil-ld does not support yet");
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
