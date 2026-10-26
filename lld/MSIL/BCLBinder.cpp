//===- BCLBinder.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The BCL binder of msil-ld (BCLBinder.h; llvm/docs/MSIL/ImplementationPlan.md
// 5.11.3-5.11.9), after Roslyn's binding of metadata references as the
// bcl-binding recon describes it (recon/p2/bcl-binding.md 4 and 5; its
// resolver, recon/p2/bcl-binding-files/tool/Program.cs, FindMember):
//
// - With references, a type binds to its unique public definition in the
//   set, whatever the order of the references; a facade's forwarder is
//   followed only to report a missing destination, never bound. A member
//   binds by name and full signature (calling convention, generic arity,
//   parameter count, return and parameter types; custom modifiers ignored),
//   searched in the type and then in its base types (a constructor or a
//   field in the type only; a base type's method not when a nearer
//   non-public method of its exact signature would take the runtime's
//   resolution), and is emitted on the requested type with the definition's
//   signature, modifiers included, re-encoded with output tokens. Every
//   needed base type and every type of a bound signature binds by the same
//   rules. AssemblyRefs come from the defining Assembly rows, in first-use
//   order.
// - Without references, the built-in table binds the mscorlib-safe entries
//   of BCLMembers.def to mscorlib 4.0.0.0 (decision D10), with exactly the
//   rows and the row order of M20's stand-in, so that links without
//   references keep their bytes (plan 5.11.7).
//
// A request is resolved first, which reads the references and writes no
// row (isDefined() stops there), and emitted second. The output depends on
// the requests, their order and the reference set only: the maps below
// serve lookups, never the order of rows.
//
//===----------------------------------------------------------------------===//

#include "BCLBinder.h"
#include "AssemblyFiles.h"
#include "InputFiles.h"
#include "ReferenceSet.h"
#include "Synthetic.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/CustomAttribute.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <variant>

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace lld;
using namespace lld::msil;

namespace {
// A failure to bind. notDefined marks the failures that mean 'the
// references do not define it' (a type or member that is missing, a type
// forwarded to a missing assembly, an entity outside the built-in table),
// after which an optional request falls back (plan 5.11.5); every other
// failure is an error whatever the strength.
class BindError : public ErrorInfo<BindError> {
public:
  static char ID;

  BindError(const Twine &msg, bool notDefined)
      : msg(msg.str()), notDefined(notDefined) {}

  void log(raw_ostream &os) const override { os << msg; }
  std::error_code convertToErrorCode() const override {
    return inconvertibleErrorCode();
  }

  std::string msg;
  bool notDefined;
};

char BindError::ID;

// A cached failure.
struct Failure {
  std::string msg;
  bool notDefined = false;
};

// A TypeDef of a reference assembly.
struct TypeDefinition {
  const ReferenceAssemblyFile *file = nullptr;
  uint32_t token = 0;
};

// A MethodDef or Field token of owner: for a MethodDef, the requested type
// or one of its base types; for a Field, always the requested type.
struct MemberDefinition {
  TypeDefinition owner;
  uint32_t token = 0;
};

// What the binder knows of an output TypeRef or TypeSpec that it made.
struct OutputType {
  // TypeRefs: the namespace, and the name nested as Outer/Inner.
  std::string ns, name;
  // TypeRefs bound with references: the definition.
  TypeDefinition def;
  // TypeSpecs: the type, with output tokens.
  std::optional<cli::TypeSig> spec;
  // "[<assembly>]<Ns>.<Name>", the text of --print-bcl-binding.
  std::string display;
};
} // namespace

// The custom attributes the binder looks for (plan 5.11.4).
static constexpr StringLiteral obsoleteAttribute = "System.ObsoleteAttribute";
static constexpr StringLiteral previewAttribute =
    "System.Runtime.Versioning.RequiresPreviewFeaturesAttribute";

// The public-key token of mscorlib's ECMA key (decision D10).
static constexpr uint8_t mscorlibToken[] = {0xB7, 0x7A, 0x5C, 0x56,
                                            0x19, 0x34, 0xE0, 0x89};

static Error bindError(const Twine &msg, bool notDefined = false) {
  return make_error<BindError>(msg, notDefined);
}

static Failure toFailure(Error e) {
  Failure f;
  handleAllErrors(
      std::move(e), [&](const BindError &b) { f = {b.msg, b.notDefined}; },
      [&](const ErrorInfoBase &b) { f = {b.message(), false}; });
  return f;
}

static Error toError(const Failure &f) {
  return bindError(f.msg, f.notDefined);
}

// "<path>: <message>" for metadata of \p file that cannot be read.
static Error fileError(const ReferenceAssemblyFile &file, Error e) {
  return bindError(getDisplayPath(file.getPath()) + ": " +
                   toString(std::move(e)));
}

// Who made a request, for diagnostics: "<file>[ for <switch>]".
static std::string getRequester(const BCLRequest &r) {
  std::string s = r.RequestedBy ? toString(r.RequestedBy) : "<internal>";
  if (!r.Switch.empty())
    s += (" for " + r.Switch).str();
  return s;
}

// The calling convention of a method signature as diagnostics print it
// before the return type (plan 5.11.9): "[instance ][explicit ][vararg ]",
// in ILAsm's order.
static void printCallingConvention(raw_ostream &os, uint8_t header) {
  if (header & cli::SIG_HASTHIS)
    os << "instance ";
  if (header & cli::SIG_EXPLICITTHIS)
    os << "explicit ";
  if ((header & cli::SIG_KIND_MASK) == cli::SIG_VARARG)
    os << "vararg ";
}

// ECMA-335 II.23.3's escaping of the characters that a serialized type name
// gives a meaning to.
static std::string escapeTypeNamePart(StringRef name) {
  std::string s;
  for (char c : name) {
    if (StringRef(",+&*[]\\").contains(c))
      s += '\\';
    s += c;
  }
  return s;
}

namespace {
// A System.Type value of a custom-attribute blob (plan 5.11.5's TypeName
// request): "Ns.Name[+Nested][, assembly part]", the assembly part ignored.
struct ParsedTypeName {
  std::string ns;
  // Nested as Outer/Inner.
  std::string name;
};
} // namespace

static Expected<ParsedTypeName> parseTypeName(StringRef text) {
  SmallVector<std::string, 2> parts(1);
  for (size_t i = 0, e = text.size(); i != e; ++i) {
    char c = text[i];
    if (c == '\\' && i + 1 != e) {
      parts.back() += text[++i];
      continue;
    }
    // The assembly part.
    if (c == ',')
      break;
    if (c == '+') {
      parts.emplace_back();
      continue;
    }
    if (c == '[' || c == ']' || c == '*' || c == '&')
      return bindError("type name '" + text +
                       "' has generic arguments, an array, a pointer or a "
                       "by-ref, which msil-ld does not bind yet");
    parts.back() += c;
  }
  for (std::string &p : parts) {
    p = StringRef(p).trim().str();
    if (p.empty())
      return bindError("invalid type name '" + text + "'");
  }
  auto [ns, outer] = splitTypeName(parts.front());
  ParsedTypeName result{ns.str(), outer.str()};
  for (const std::string &nested : drop_begin(parts))
    result.name += "/" + nested;
  return result;
}

//===----------------------------------------------------------------------===//
// BCLBinder::Impl
//===----------------------------------------------------------------------===//

struct BCLBinder::Impl {
  Impl(const ReferenceSet &refs, cli::MetadataBuilder &out)
      : refs(refs), out(out), builtin(refs.empty()) {}

  // Resolution: reads the references, writes no row.
  Error checkCore();
  Expected<TypeDefinition> resolveType(StringRef ns, StringRef name);
  Expected<TypeDefinition> lookupType(StringRef ns, StringRef name,
                                      const std::string &full);
  Expected<std::optional<TypeDefinition>>
  resolveBase(const TypeDefinition &type, uint32_t extends);
  Expected<MemberDefinition> resolveMember(const BCLRequest &r,
                                           const TypeDefinition &parent);
  bool typesMatch(const cli::TypeSig &req, const cli::TypeSig &def,
                  const ReferenceAssemblyFile &file, unsigned depth);
  bool typeRefsMatch(uint32_t req, uint32_t def,
                     const ReferenceAssemblyFile &file, unsigned depth);
  bool methodSigsMatch(const cli::MethodSig &req, const cli::MethodSig &def,
                       const ReferenceAssemblyFile &file, unsigned depth);
  bool sameTypes(const cli::TypeSig &a, const ReferenceAssemblyFile &fileA,
                 const cli::TypeSig &b, const ReferenceAssemblyFile &fileB,
                 unsigned depth);
  bool sameTypeTokens(uint32_t a, const ReferenceAssemblyFile &fileA,
                      uint32_t b, const ReferenceAssemblyFile &fileB,
                      unsigned depth);
  bool sameMethodSigs(const cli::MethodSig &a,
                      const ReferenceAssemblyFile &fileA,
                      const cli::MethodSig &b,
                      const ReferenceAssemblyFile &fileB, unsigned depth);
  bool requiresPreview(const ReferenceAssemblyFile &file,
                       ArrayRef<uint32_t> tokens);
  Expected<cli::ElementType> getEnumUnderlyingType(const TypeDefinition &type,
                                                   StringRef what);
  Expected<cli::ElementType> getEnumUnderlyingType(StringRef ns,
                                                   StringRef name);

  // The built-in [mscorlib] table (plan 5.11.7).
  Error notInTable(StringRef what);
  const cli::BCLMemberInfo *findBuiltinType(StringRef ns, StringRef name);
  const cli::BCLMemberInfo *findBuiltinMethod(const BCLRequest &r);
  uint32_t getMscorlib();
  uint32_t emitBuiltinType(StringRef ns, StringRef name);
  Expected<uint32_t> emitBuiltinMember(const cli::BCLMemberInfo &info);

  // Emission.
  uint32_t getAssemblyRef(const ReferenceAssemblyFile &file);
  Expected<uint32_t> emitType(const TypeDefinition &type, unsigned depth = 0);
  Expected<uint32_t> mapDefinitionType(const ReferenceAssemblyFile &file,
                                       uint32_t token, unsigned depth);
  uint32_t addTypeSpec(const cli::TypeSig &spec);
  void warnPrivate(const ReferenceAssemblyFile &file, StringRef typeName);

  // Requests.
  Expected<uint32_t> bindTypeByName(StringRef ns, StringRef name);
  Expected<uint32_t> bindMemberRequest(const BCLRequest &r);
  Expected<uint32_t> bindEntity(const BCLRequest &r);
  Expected<uint32_t> bindMemberId(cli::BCLMemberId id);
  Expected<std::string> bindTypeName(StringRef text);
  bool isDefined(const BCLRequest &r);
  void noteFallback(const BCLRequest &r);
  std::string getHelperSymbol(const BCLRequest &r);
  Error rewriteValue(cli::CAValue &v, unsigned depth);
  Error rewriteAttributeBlob(const cli::MethodSig &ctor, ArrayRef<uint8_t> in,
                             SmallVectorImpl<uint8_t> &blob);
  std::string describe(StringRef request);
  std::string describeAttribute(StringRef request, StringRef rest);

  // Texts.
  void printRequestRef(raw_ostream &os, uint32_t token);
  void printDisplayRef(raw_ostream &os, uint32_t token);
  void printDefinitionRef(raw_ostream &os, const ReferenceAssemblyFile &file,
                          uint32_t token);
  std::string formatRequest(const BCLRequest &r);
  std::string getAssemblyQualifiedName(const TypeDefinition &type);

  const ReferenceSet &refs;
  cli::MetadataBuilder &out;
  // No references: the built-in table.
  const bool builtin;

  // The core-library check (plan 5.11.3), made once.
  bool coreChecked = false;
  std::optional<std::string> coreError;
  // Type lookups by full name.
  StringMap<std::variant<TypeDefinition, Failure>> types;
  // Whether an assembly carries [RequiresPreviewFeatures].
  DenseMap<const ReferenceAssemblyFile *, bool> previewAssemblies;

  // The rows made: AssemblyRefs, TypeRefs by definition (or, for the
  // built-in table, by full name), what each output TypeRef or TypeSpec
  // names, the MemberRefs of BCLMembers.def entries, and the text of each
  // MemberRef for --print-bcl-binding.
  uint32_t mscorlib = 0;
  DenseMap<const ReferenceAssemblyFile *, uint32_t> assemblyRefs;
  DenseMap<std::pair<const ReferenceAssemblyFile *, uint32_t>, uint32_t>
      typeRefs;
  StringMap<uint32_t> builtinTypeRefs;
  DenseMap<uint32_t, OutputType> outputTypes;
  DenseMap<unsigned, uint32_t> memberIds;
  DenseMap<uint32_t, std::string> memberTexts;

  // Diagnostics given once.
  DenseSet<const ReferenceAssemblyFile *> warnedPrivate;
  DenseSet<std::pair<const ReferenceAssemblyFile *, uint32_t>> warnedObsolete;
};

Error BCLBinder::Impl::checkCore() {
  if (!coreChecked) {
    coreChecked = true;
    Expected<const ReferenceAssemblyFile *> core = refs.coreLibrary();
    if (!core)
      coreError = toString(core.takeError());
  }
  if (coreError)
    return bindError(*coreError);
  return Error::success();
}

Expected<TypeDefinition> BCLBinder::Impl::resolveType(StringRef ns,
                                                      StringRef name) {
  if (Error e = checkCore())
    return std::move(e);
  std::string full = joinTypeName(ns, name);
  auto it = types.find(full);
  if (it == types.end()) {
    Expected<TypeDefinition> d = lookupType(ns, name, full);
    std::variant<TypeDefinition, Failure> v;
    if (d)
      v = *d;
    else
      v = toFailure(d.takeError());
    it = types.try_emplace(full, std::move(v)).first;
  }
  if (const auto *f = std::get_if<Failure>(&it->second))
    return toError(*f);
  return std::get<TypeDefinition>(it->second);
}

// Plan 5.11.4: the unique public definition, in either reference order; a
// name that only facades forward is followed through the set to the point
// where the chain breaks, and never bound.
Expected<TypeDefinition> BCLBinder::Impl::lookupType(StringRef ns,
                                                     StringRef name,
                                                     const std::string &full) {
  TypeCandidates c = refs.findType(ns, name);
  auto describeFile = [](const ReferenceAssemblyFile *f) {
    return f->getIdentity().str() + " (" + getDisplayPath(f->getPath()) + ")";
  };
  size_t n = c.Definitions.size();
  if (n == 1)
    return TypeDefinition{c.Definitions.front().File,
                          c.Definitions.front().TypeDef};
  if (n > 1) {
    std::string list;
    for (auto [i, d] : enumerate(c.Definitions)) {
      if (i)
        list += i + 1 == n ? " and " : ", ";
      list += describeFile(d.File);
    }
    return bindError("type '" + full + "' is defined in " +
                     (n == 2 ? std::string("two") : std::to_string(n)) +
                     " references: " + list);
  }
  if (!c.Forwarders.empty()) {
    const TypeCandidates::Forwarder *f = &c.Forwarders.front();
    SmallPtrSet<const ReferenceAssemblyFile *, 4> seen;
    while (seen.insert(f->File).second) {
      const ReferenceAssemblyFile *dest = refs.findAssembly(f->Destination);
      if (!dest)
        return bindError(Twine("type '") + full + "' is forwarded by " +
                             f->File->getIdentity().str() + " to " +
                             f->Destination +
                             ", which is not among the references",
                         /*notDefined=*/true);
      // The destination defines no public type of that name (else N > 0);
      // it may forward it in turn.
      const TypeCandidates::Forwarder *next = nullptr;
      for (const TypeCandidates::Forwarder &g : c.Forwarders)
        if (g.File == dest) {
          next = &g;
          break;
        }
      if (!next)
        break;
      f = next;
    }
  }
  return bindError("type '" + full + "' is not defined in the references (" +
                       Twine(refs.files().size()) + " assemblies)",
                   /*notDefined=*/true);
}

// The type of the TypeSpec \p token of \p r.
static Expected<cli::TypeSig> decodeTypeSpecToken(const cli::MetadataReader &r,
                                                  uint32_t token) {
  Expected<cli::TypeSpecRow> row = r.getTypeSpec(cli::getTokenRow(token));
  if (!row)
    return row.takeError();
  Expected<ArrayRef<uint8_t>> blob = r.getBlob(row->Signature);
  if (!blob)
    return blob.takeError();
  return cli::decodeTypeSpec(*blob);
}

// The definition of the base type \p extends of \p type, for the member
// search (plan 5.11.4; section 12, '5.11.4 amended (the base-type member
// search)' item 3). A base type binds by the type rules like any type: its
// failure (not defined, defined twice, forwarded to a missing assembly) is
// the search's, with its strength rule, and metadata of \p type that cannot
// be read is its file's error. std::nullopt ends the search: a base that is
// neither a TypeDef, a TypeRef nor a generic instance of one.
Expected<std::optional<TypeDefinition>>
BCLBinder::Impl::resolveBase(const TypeDefinition &type, uint32_t extends) {
  const ReferenceAssemblyFile &file = *type.file;
  const cli::MetadataReader &r = file.getReader();
  uint8_t table = cli::getTokenType(extends);
  if (table == static_cast<uint8_t>(cli::TableId::TypeSpec)) {
    // A generic base: its generic type (members are compared without
    // substituting the type arguments).
    Expected<cli::TypeSig> spec = decodeTypeSpecToken(r, extends);
    if (!spec)
      return fileError(file, spec.takeError());
    if (spec->getElementType() != cli::ELEMENT_TYPE_GENERICINST)
      return std::nullopt;
    extends = spec->getTypeRef();
    table = cli::getTokenType(extends);
  }
  if (table == static_cast<uint8_t>(cli::TableId::TypeDef))
    return TypeDefinition{type.file, extends};
  if (table != static_cast<uint8_t>(cli::TableId::TypeRef))
    return std::nullopt;
  auto name = getIndexTypeName(r, extends);
  if (!name)
    return fileError(file, name.takeError());
  Expected<TypeDefinition> base = resolveType(name->first, name->second);
  if (!base)
    return base.takeError();
  return *base;
}

void BCLBinder::Impl::printRequestRef(raw_ostream &os, uint32_t token) {
  auto it = outputTypes.find(token);
  if (it == outputTypes.end()) {
    os << format_hex(token, 10, /*Upper=*/true);
  } else if (it->second.spec) {
    cli::TypeSig spec = *it->second.spec;
    cli::printTypeSig(
        os, spec, [&](raw_ostream &os, uint32_t t) { printRequestRef(os, t); });
  } else {
    os << joinTypeName(it->second.ns, it->second.name);
  }
}

void BCLBinder::Impl::printDisplayRef(raw_ostream &os, uint32_t token) {
  auto it = outputTypes.find(token);
  if (it == outputTypes.end())
    os << format_hex(token, 10, /*Upper=*/true);
  else
    os << it->second.display;
}

void BCLBinder::Impl::printDefinitionRef(raw_ostream &os,
                                         const ReferenceAssemblyFile &file,
                                         uint32_t token) {
  auto name = getIndexTypeName(file.getReader(), token);
  if (!name) {
    consumeError(name.takeError());
    os << format_hex(token, 10, /*Upper=*/true);
    return;
  }
  os << joinTypeName(name->first, name->second);
}

// A request as diagnostics name it, in the shape of cli::printBCLMemberRef
// without a scope: "[instance ][explicit ][vararg ]<return>
// <Ns>.<Type>::<Member>(<params>)",
// "<type> <Ns>.<Type>::<Field>", or the type's full name.
std::string BCLBinder::Impl::formatRequest(const BCLRequest &r) {
  std::string s;
  raw_string_ostream os(s);
  auto ref = [&](raw_ostream &os, uint32_t t) { printRequestRef(os, t); };
  switch (r.K) {
  case BCLRequest::Type:
    os << joinTypeName(r.Namespace, r.Name);
    break;
  case BCLRequest::TypeName:
    os << r.Name;
    break;
  case BCLRequest::TypeSpec:
    if (r.FieldOrSpec)
      cli::printTypeSig(os, *r.FieldOrSpec, ref);
    break;
  case BCLRequest::Field:
    if (r.FieldOrSpec) {
      cli::printTypeSig(os, *r.FieldOrSpec, ref);
      os << ' ';
    }
    os << joinTypeName(r.Namespace, r.Name) << "::" << r.Member;
    break;
  case BCLRequest::Method:
  case BCLRequest::AttributeCtor:
  case BCLRequest::MethodSpec: {
    const cli::MethodSig *sig =
        r.MethodSignature ? &*r.MethodSignature : nullptr;
    if (sig) {
      printCallingConvention(os, sig->Header);
      cli::printTypeSig(os, sig->Return, ref);
      os << ' ';
    }
    os << joinTypeName(r.Namespace, r.Name) << "::"
       << (r.Member.empty() && r.K == BCLRequest::AttributeCtor
               ? StringRef(".ctor")
               : r.Member);
    if (sig && (sig->Header & cli::SIG_GENERIC))
      os << '<' << sig->GenericParamCount << '>';
    if (r.K == BCLRequest::MethodSpec && !r.Args.empty()) {
      os << '<';
      ListSeparator ls;
      for (const cli::TypeSig &a : r.Args) {
        os << ls;
        cli::printTypeSig(os, a, ref);
      }
      os << '>';
    }
    os << '(';
    if (sig) {
      for (size_t i = 0, e = sig->Params.size(); i != e; ++i) {
        if (i)
          os << ", ";
        if (i == sig->RequiredParamCount)
          os << "..., ";
        cli::printTypeSig(os, sig->Params[i], ref);
      }
    }
    os << ')';
    break;
  }
  }
  return s;
}

bool BCLBinder::Impl::typeRefsMatch(uint32_t req, uint32_t def,
                                    const ReferenceAssemblyFile &file,
                                    unsigned depth) {
  auto it = outputTypes.find(req);
  if (it == outputTypes.end())
    return false;
  OutputType reqType = it->second;
  if (cli::getTokenType(def) == static_cast<uint8_t>(cli::TableId::TypeSpec)) {
    if (!reqType.spec)
      return false;
    const cli::MetadataReader &r = file.getReader();
    Expected<cli::TypeSpecRow> row = r.getTypeSpec(cli::getTokenRow(def));
    if (!row) {
      consumeError(row.takeError());
      return false;
    }
    Expected<ArrayRef<uint8_t>> blob = r.getBlob(row->Signature);
    if (!blob) {
      consumeError(blob.takeError());
      return false;
    }
    Expected<cli::TypeSig> spec = cli::decodeTypeSpec(*blob);
    if (!spec) {
      consumeError(spec.takeError());
      return false;
    }
    return typesMatch(*reqType.spec, *spec, file, depth + 1);
  }
  if (reqType.spec)
    return false;
  auto name = getIndexTypeName(file.getReader(), def);
  if (!name) {
    consumeError(name.takeError());
    return false;
  }
  return name->first == reqType.ns && name->second == reqType.name;
}

// Plan 5.11.4: the same type once custom modifiers are ignored, the
// definition's type tokens compared by full name with the types that the
// request's output tokens name.
bool BCLBinder::Impl::typesMatch(const cli::TypeSig &req,
                                 const cli::TypeSig &def,
                                 const ReferenceAssemblyFile &file,
                                 unsigned depth) {
  if (depth > maxTypeNestingDepth)
    return false;
  cli::ElementType et = req.getElementType();
  if (et != def.getElementType())
    return false;
  switch (et) {
  case cli::ELEMENT_TYPE_CLASS:
  case cli::ELEMENT_TYPE_VALUETYPE:
    return typeRefsMatch(req.getTypeRef(), def.getTypeRef(), file, depth);
  case cli::ELEMENT_TYPE_GENERICINST: {
    if (req.getGenericInstKind() != def.getGenericInstKind() ||
        !typeRefsMatch(req.getTypeRef(), def.getTypeRef(), file, depth) ||
        req.getOperands().size() != def.getOperands().size())
      return false;
    for (auto [a, b] : zip(req.getOperands(), def.getOperands()))
      if (!typesMatch(a, b, file, depth + 1))
        return false;
    return true;
  }
  case cli::ELEMENT_TYPE_ARRAY:
    if (req.getArrayShape() != def.getArrayShape())
      return false;
    [[fallthrough]];
  case cli::ELEMENT_TYPE_PTR:
  case cli::ELEMENT_TYPE_BYREF:
  case cli::ELEMENT_TYPE_SZARRAY:
  case cli::ELEMENT_TYPE_PINNED:
    return typesMatch(req.getOperands().front(), def.getOperands().front(),
                      file, depth + 1);
  case cli::ELEMENT_TYPE_VAR:
  case cli::ELEMENT_TYPE_MVAR:
    return req.getGenericParamIndex() == def.getGenericParamIndex();
  case cli::ELEMENT_TYPE_FNPTR:
    return methodSigsMatch(req.getFunctionSig(), def.getFunctionSig(), file,
                           depth + 1);
  default:
    // A primitive.
    return true;
  }
}

// Plan 5.11.4: the calling convention (static or instance, vararg,
// generic), the generic arity, the parameter count, the return type and the
// parameter types. A vararg call site's arguments after its sentinel are
// not the definition's.
bool BCLBinder::Impl::methodSigsMatch(const cli::MethodSig &req,
                                      const cli::MethodSig &def,
                                      const ReferenceAssemblyFile &file,
                                      unsigned depth) {
  constexpr uint8_t convention = cli::SIG_KIND_MASK | cli::SIG_HASTHIS |
                                 cli::SIG_EXPLICITTHIS | cli::SIG_GENERIC;
  if ((req.Header & convention) != (def.Header & convention))
    return false;
  if ((def.Header & cli::SIG_GENERIC) &&
      req.GenericParamCount != def.GenericParamCount)
    return false;
  size_t fixed = std::min<size_t>(req.RequiredParamCount, req.Params.size());
  if (fixed != def.Params.size() ||
      !typesMatch(req.Return, def.Return, file, depth))
    return false;
  for (size_t i = 0; i != fixed; ++i)
    if (!typesMatch(req.Params[i], def.Params[i], file, depth))
      return false;
  return true;
}

// Whether the type tokens \p a of \p fileA and \p b of \p fileB name the same
// type: TypeDefs and TypeRefs by full name, TypeSpecs by their types.
bool BCLBinder::Impl::sameTypeTokens(uint32_t a,
                                     const ReferenceAssemblyFile &fileA,
                                     uint32_t b,
                                     const ReferenceAssemblyFile &fileB,
                                     unsigned depth) {
  constexpr uint8_t typeSpec = static_cast<uint8_t>(cli::TableId::TypeSpec);
  bool isSpec = cli::getTokenType(a) == typeSpec;
  if (isSpec != (cli::getTokenType(b) == typeSpec))
    return false;
  if (isSpec) {
    Expected<cli::TypeSig> specA = decodeTypeSpecToken(fileA.getReader(), a);
    Expected<cli::TypeSig> specB = decodeTypeSpecToken(fileB.getReader(), b);
    if (!specA || !specB) {
      consumeError(specA.takeError());
      consumeError(specB.takeError());
      return false;
    }
    return sameTypes(*specA, fileA, *specB, fileB, depth + 1);
  }
  auto nameA = getIndexTypeName(fileA.getReader(), a);
  auto nameB = getIndexTypeName(fileB.getReader(), b);
  if (!nameA || !nameB) {
    consumeError(nameA.takeError());
    consumeError(nameB.takeError());
    return false;
  }
  return *nameA == *nameB;
}

// Whether two definitions' types are the same for the runtime's member
// resolution: as typesMatch, but custom modifiers included, and both sides
// definition types, compared by full name across files.
bool BCLBinder::Impl::sameTypes(const cli::TypeSig &a,
                                const ReferenceAssemblyFile &fileA,
                                const cli::TypeSig &b,
                                const ReferenceAssemblyFile &fileB,
                                unsigned depth) {
  if (depth > maxTypeNestingDepth)
    return false;
  ArrayRef<cli::CustomModifier> modsA = a.getModifiers();
  ArrayRef<cli::CustomModifier> modsB = b.getModifiers();
  if (modsA.size() != modsB.size())
    return false;
  for (auto [x, y] : zip(modsA, modsB))
    if (x.IsRequired != y.IsRequired ||
        !sameTypeTokens(x.Type, fileA, y.Type, fileB, depth))
      return false;
  cli::ElementType et = a.getElementType();
  if (et != b.getElementType())
    return false;
  switch (et) {
  case cli::ELEMENT_TYPE_CLASS:
  case cli::ELEMENT_TYPE_VALUETYPE:
    return sameTypeTokens(a.getTypeRef(), fileA, b.getTypeRef(), fileB, depth);
  case cli::ELEMENT_TYPE_GENERICINST: {
    if (a.getGenericInstKind() != b.getGenericInstKind() ||
        !sameTypeTokens(a.getTypeRef(), fileA, b.getTypeRef(), fileB, depth) ||
        a.getOperands().size() != b.getOperands().size())
      return false;
    for (auto [x, y] : zip(a.getOperands(), b.getOperands()))
      if (!sameTypes(x, fileA, y, fileB, depth + 1))
        return false;
    return true;
  }
  case cli::ELEMENT_TYPE_ARRAY:
    if (a.getArrayShape() != b.getArrayShape())
      return false;
    [[fallthrough]];
  case cli::ELEMENT_TYPE_PTR:
  case cli::ELEMENT_TYPE_BYREF:
  case cli::ELEMENT_TYPE_SZARRAY:
  case cli::ELEMENT_TYPE_PINNED:
    return sameTypes(a.getOperands().front(), fileA, b.getOperands().front(),
                     fileB, depth + 1);
  case cli::ELEMENT_TYPE_VAR:
  case cli::ELEMENT_TYPE_MVAR:
    return a.getGenericParamIndex() == b.getGenericParamIndex();
  case cli::ELEMENT_TYPE_FNPTR:
    return sameMethodSigs(a.getFunctionSig(), fileA, b.getFunctionSig(), fileB,
                          depth + 1);
  default:
    // A primitive.
    return true;
  }
}

// Whether two definitions' method signatures are the same, custom modifiers
// included: the whole calling convention, the generic arity and every type.
bool BCLBinder::Impl::sameMethodSigs(const cli::MethodSig &a,
                                     const ReferenceAssemblyFile &fileA,
                                     const cli::MethodSig &b,
                                     const ReferenceAssemblyFile &fileB,
                                     unsigned depth) {
  if (a.Header != b.Header || a.Params.size() != b.Params.size() ||
      std::min<size_t>(a.RequiredParamCount, a.Params.size()) !=
          std::min<size_t>(b.RequiredParamCount, b.Params.size()))
    return false;
  if ((a.Header & cli::SIG_GENERIC) &&
      a.GenericParamCount != b.GenericParamCount)
    return false;
  if (!sameTypes(a.Return, fileA, b.Return, fileB, depth))
    return false;
  for (auto [x, y] : zip(a.Params, b.Params))
    if (!sameTypes(x, fileA, y, fileB, depth))
      return false;
  return true;
}

bool BCLBinder::Impl::requiresPreview(const ReferenceAssemblyFile &file,
                                      ArrayRef<uint32_t> tokens) {
  const cli::MetadataReader &r = file.getReader();
  for (uint32_t t : tokens)
    if (hasCustomAttribute(r, t, previewAttribute))
      return true;
  auto [it, inserted] = previewAssemblies.try_emplace(&file, false);
  if (inserted)
    it->second = hasCustomAttribute(
        r, cli::makeToken(cli::TableId::Assembly, 1), previewAttribute);
  return it->second;
}

// Plan 5.11.4 with section 12's '5.11.4 amended (the base-type member
// search)' and '5.11.4 amended (field requests search the requested type
// only; ...)': the public member of the request's name and signature in
// \p parent or, failing that, in the nearest base type that has one; only
// public members count. Constructors are not inherited (ECMA-335 II.10.5.1),
// and the runtime does not resolve a field MemberRef through base types
// (MissingFieldException), so a constructor or a field is searched in
// \p parent only. The MemberRef stays on \p parent, and the runtime
// resolves a method MemberRef from there upwards to the first method of
// that name and of the emitted signature, custom modifiers included,
// whatever its accessibility, checking access afterwards: so a non-public
// method of exactly the match's signature in \p parent or a type between it
// and the match hides the match, and the request is undefined. The hiding
// rule concerns methods only.
Expected<MemberDefinition>
BCLBinder::Impl::resolveMember(const BCLRequest &r,
                               const TypeDefinition &parent) {
  bool isField = r.K == BCLRequest::Field;
  const cli::MethodSig *reqSig =
      r.MethodSignature ? &*r.MethodSignature : nullptr;
  const cli::TypeSig *reqType = r.FieldOrSpec ? &*r.FieldOrSpec : nullptr;
  if (isField ? !reqType : !reqSig)
    return bindError("BCL request for '" + formatRequest(r) +
                     "' has no signature");
  StringRef member = r.Member;
  if (member.empty() && r.K == BCLRequest::AttributeCtor)
    member = ".ctor";
  bool isConstructor = r.K == BCLRequest::AttributeCtor ||
                       (!isField && (member == ".ctor" || member == ".cctor"));
  // Constructors and fields are searched in \p parent only.
  bool searchesOwnTypeOnly = isConstructor || isField;

  // The members of the name that the search saw, for the error.
  std::vector<std::string> present;
  auto undefinedMember = [&]() -> Error {
    const ReferenceAssemblyFile &file = *parent.file;
    return bindError(
        Twine("undefined .NET member '") + formatRequest(r) +
            "' (requested by " + getRequester(r) + "); '" +
            joinTypeName(r.Namespace, r.Name) + "' is defined in " +
            file.getIdentity().str() + " (" + getDisplayPath(file.getPath()) +
            ") but has no such member; overloads present: " +
            (present.empty() ? std::string("none") : join(present, "; ")),
        /*notDefined=*/true);
  };
  // The non-public methods of the name in the types before the current one.
  struct NonPublicMethod {
    const ReferenceAssemblyFile *file;
    cli::MethodSig sig;
  };
  SmallVector<NonPublicMethod, 2> nearer;

  TypeDefinition type = parent;
  for (unsigned depth = 0; depth <= maxTypeNestingDepth; ++depth) {
    const ReferenceAssemblyFile &file = *type.file;
    const cli::MetadataReader &rd = file.getReader();
    Expected<cli::TypeDefRow> row = rd.getTypeDef(cli::getTokenRow(type.token));
    if (!row)
      return fileError(file, row.takeError());
    // Members found in a base type are listed with their declaring type.
    std::string owner;
    if (depth) {
      raw_string_ostream os(owner);
      printDefinitionRef(os, file, type.token);
      owner += "::";
    }
    auto defRef = [&](raw_ostream &os, uint32_t t) {
      printDefinitionRef(os, file, t);
    };

    SmallVector<uint32_t, 2> matches;
    // The signature of the last public method match, and the non-public
    // methods.
    cli::MethodSig matchMethod;
    SmallVector<NonPublicMethod, 2> nonPublic;
    uint32_t begin = isField ? row->FieldBegin : row->MethodBegin;
    uint32_t end = isField ? row->FieldEnd : row->MethodEnd;
    for (uint32_t index = begin; index < end; ++index) {
      std::string text;
      raw_string_ostream os(text);
      bool isPublic, match;
      if (isField) {
        Expected<cli::FieldRow> f = rd.getField(index);
        if (!f)
          return fileError(file, f.takeError());
        if (f->Name != member)
          continue;
        Expected<ArrayRef<uint8_t>> blob = rd.getBlob(f->Signature);
        if (!blob)
          return fileError(file, blob.takeError());
        Expected<cli::TypeSig> t = cli::decodeFieldSig(*blob);
        if (!t)
          return fileError(file, t.takeError());
        isPublic = (f->Flags & cli::FieldAttributes::FieldAccessMask) ==
                   cli::FieldAttributes::Public;
        match = typesMatch(*reqType, *t, file, 0);
        if ((f->Flags & cli::FieldAttributes::Static) ==
            cli::FieldAttributes::Static)
          os << "static ";
        cli::printTypeSig(os, *t, defRef);
        os << ' ' << owner << f->Name;
        if (match && isPublic)
          matches.push_back(cli::makeToken(cli::TableId::Field, index));
      } else {
        Expected<cli::MethodDefRow> m = rd.getMethodDef(index);
        if (!m)
          return fileError(file, m.takeError());
        if (m->Name != member)
          continue;
        Expected<ArrayRef<uint8_t>> blob = rd.getBlob(m->Signature);
        if (!blob)
          return fileError(file, blob.takeError());
        Expected<cli::MethodSig> sig = cli::decodeMethodSig(*blob);
        if (!sig)
          return fileError(file, sig.takeError());
        isPublic = (m->Flags & cli::MethodAttributes::MemberAccessMask) ==
                   cli::MethodAttributes::Public;
        match = methodSigsMatch(*reqSig, *sig, file, 0);
        printCallingConvention(os, sig->Header);
        cli::printTypeSig(os, sig->Return, defRef);
        os << ' ' << owner << m->Name;
        if (sig->Header & cli::SIG_GENERIC)
          os << '<' << sig->GenericParamCount << '>';
        os << '(';
        ListSeparator ls;
        for (const cli::TypeSig &p : sig->Params) {
          os << ls;
          cli::printTypeSig(os, p, defRef);
        }
        os << ')';
        if (match && isPublic) {
          matches.push_back(cli::makeToken(cli::TableId::MethodDef, index));
          matchMethod = *sig;
        } else if (!isPublic) {
          nonPublic.push_back({&file, *sig});
        }
      }
      if (!isPublic)
        text += " (not public)";
      present.push_back(std::move(text));
    }

    if (matches.size() == 1) {
      // A nearer non-public method that differs from the match only in a
      // custom modifier does not hide it: the runtime compares modifiers.
      for (const NonPublicMethod &m : nearer)
        if (sameMethodSigs(m.sig, *m.file, matchMethod, file, 0))
          return undefinedMember();
      return MemberDefinition{type, matches.front()};
    }
    if (matches.size() > 1) {
      std::string parents;
      raw_string_ostream os(parents);
      printDefinitionRef(os, file, type.token);
      return bindError(
          Twine(".NET member '") + formatRequest(r) + "' (requested by " +
          getRequester(r) + ") matches several members of '" + parents +
          "' in " + file.getIdentity().str() + " (" +
          getDisplayPath(file.getPath()) +
          ") when custom modifiers are ignored: " + join(present, "; "));
    }
    if (searchesOwnTypeOnly || row->Extends == 0)
      break;
    Expected<std::optional<TypeDefinition>> base =
        resolveBase(type, row->Extends);
    if (!base)
      return base.takeError();
    if (!*base)
      break;
    append_range(nearer, nonPublic);
    type = **base;
  }
  return undefinedMember();
}

// The underlying type of the enum \p type: the type of its instance field
// (value__).
Expected<cli::ElementType>
BCLBinder::Impl::getEnumUnderlyingType(const TypeDefinition &type,
                                       StringRef what) {
  const cli::MetadataReader &r = type.file->getReader();
  Expected<cli::TypeDefRow> row = r.getTypeDef(cli::getTokenRow(type.token));
  if (!row)
    return fileError(*type.file, row.takeError());
  for (uint32_t index = row->FieldBegin; index < row->FieldEnd; ++index) {
    Expected<cli::FieldRow> f = r.getField(index);
    if (!f)
      return fileError(*type.file, f.takeError());
    if ((f->Flags & cli::FieldAttributes::Static) ==
        cli::FieldAttributes::Static)
      continue;
    Expected<ArrayRef<uint8_t>> blob = r.getBlob(f->Signature);
    if (!blob)
      return fileError(*type.file, blob.takeError());
    Expected<cli::TypeSig> t = cli::decodeFieldSig(*blob);
    if (!t)
      return fileError(*type.file, t.takeError());
    cli::ElementType et = t->getElementType();
    if ((et >= cli::ELEMENT_TYPE_BOOLEAN && et <= cli::ELEMENT_TYPE_U8) ||
        et == cli::ELEMENT_TYPE_I || et == cli::ELEMENT_TYPE_U)
      return et;
    break;
  }
  return bindError("'" + what + "' in " + type.file->getIdentity().str() +
                   " is not an enum with an integer underlying type");
}

Expected<cli::ElementType>
BCLBinder::Impl::getEnumUnderlyingType(StringRef ns, StringRef name) {
  if (builtin)
    return notInTable(joinTypeName(ns, name));
  Expected<TypeDefinition> d = resolveType(ns, name);
  if (!d)
    return d.takeError();
  return getEnumUnderlyingType(*d, joinTypeName(ns, name));
}

//===----------------------------------------------------------------------===//
// The built-in [mscorlib] table (plan 5.11.7)
//===----------------------------------------------------------------------===//

Error BCLBinder::Impl::notInTable(StringRef what) {
  return bindError(".NET member '" + what +
                       "' is not in msil-ld's built-in mscorlib table; pass "
                       "--reference=<reference assemblies>",
                   /*notDefined=*/true);
}

const cli::BCLMemberInfo *BCLBinder::Impl::findBuiltinType(StringRef ns,
                                                           StringRef name) {
  for (const cli::BCLMemberInfo &info : cli::getAllBCLMembers())
    if (info.isType() && info.MscorlibSafe && info.Namespace == ns &&
        info.Name == name)
      return &info;
  return nullptr;
}

// The mscorlib-safe method entry of the request: its type, name, instance
// flag and signature, compared as canonical listing v1 text, as the entries
// spell their signatures.
const cli::BCLMemberInfo *
BCLBinder::Impl::findBuiltinMethod(const BCLRequest &r) {
  if (!r.MethodSignature)
    return nullptr;
  const cli::MethodSig &sig = *r.MethodSignature;
  if ((sig.Header & ~cli::SIG_HASTHIS) != cli::SIG_DEFAULT ||
      sig.RequiredParamCount < sig.Params.size())
    return nullptr;
  auto ref = [&](raw_ostream &os, uint32_t t) { printRequestRef(os, t); };
  std::string ret, params;
  raw_string_ostream retOs(ret), paramsOs(params);
  cli::printTypeSig(retOs, sig.Return, ref);
  ListSeparator ls;
  for (const cli::TypeSig &p : sig.Params) {
    paramsOs << ls;
    cli::printTypeSig(paramsOs, p, ref);
  }
  StringRef member = r.Member;
  if (member.empty() && r.K == BCLRequest::AttributeCtor)
    member = ".ctor";
  for (const cli::BCLMemberInfo &info : cli::getAllBCLMembers()) {
    if (!info.isMethod() || !info.MscorlibSafe || info.Name != member ||
        info.HasThis != bool(sig.Header & cli::SIG_HASTHIS) ||
        info.Return != ret || info.Params != params)
      continue;
    const cli::BCLMemberInfo &owner = cli::getBCLMemberInfo(info.Owner);
    if (owner.Namespace == r.Namespace && owner.Name == r.Name)
      return &info;
  }
  return nullptr;
}

// mscorlib 4.0.0.0, public key token b77a5c561934e089, Flags 0 (D10).
uint32_t BCLBinder::Impl::getMscorlib() {
  if (!mscorlib) {
    cli::AssemblyRefInfo ref;
    ref.Name = cli::MscorlibAssembly.str();
    ref.Version.Major = 4;
    ref.PublicKeyOrToken.assign(std::begin(mscorlibToken),
                                std::end(mscorlibToken));
    mscorlib = out.getToken(out.addAssemblyRef(ref));
  }
  return mscorlib;
}

// A type of the table in [mscorlib]; a nested type 'Outer/Inner' is scoped
// to its enclosing type's TypeRef.
uint32_t BCLBinder::Impl::emitBuiltinType(StringRef ns, StringRef name) {
  std::string full = joinTypeName(ns, name);
  if (auto it = builtinTypeRefs.find(full); it != builtinTypeRefs.end())
    return it->second;
  SmallVector<StringRef, 2> names;
  name.split(names, '/');
  cli::EntityHandle scope{getMscorlib()};
  cli::EntityHandle ref = out.addTypeRef(scope, ns, names.front());
  for (StringRef inner : drop_begin(names))
    ref = out.addTypeRef(ref, "", inner);
  OutputType &t = outputTypes[ref.Value];
  t.ns = ns.str();
  t.name = name.str();
  t.display = ("[" + cli::MscorlibAssembly + "]" + full).str();
  builtinTypeRefs[full] = ref.Value;
  return ref.Value;
}

// The MemberRef of a method of the table, with the rows in the order of
// M20's stand-in: the type's TypeRef (and, the first time, the
// AssemblyRef), then the signature blob, then the MemberRef.
Expected<uint32_t>
BCLBinder::Impl::emitBuiltinMember(const cli::BCLMemberInfo &info) {
  if (auto it = memberIds.find(static_cast<unsigned>(info.Id));
      it != memberIds.end())
    return it->second;
  Expected<cli::MethodSig> sig = parseBCLMemberSig(
      info, [&](StringRef ns, StringRef name, bool) -> Expected<uint32_t> {
        if (!findBuiltinType(ns, name))
          return notInTable(joinTypeName(ns, name));
        return emitBuiltinType(ns, name);
      });
  if (!sig)
    return sig.takeError();
  SmallVector<uint8_t, 16> blob;
  cli::encodeMethodSig(*sig, blob);
  const cli::BCLMemberInfo &owner = cli::getBCLMemberInfo(info.Owner);
  cli::EntityHandle parent{emitBuiltinType(owner.Namespace, owner.Name)};
  uint32_t token =
      out.getToken(out.addMemberRef(parent, info.Name, out.addBlob(blob)));
  std::string text;
  raw_string_ostream os(text);
  cli::printBCLMemberRef(os, info.Id, cli::MscorlibAssembly);
  memberTexts[token] = std::move(text);
  memberIds[static_cast<unsigned>(info.Id)] = token;
  return token;
}

//===----------------------------------------------------------------------===//
// Emission with references (plan 5.11.6)
//===----------------------------------------------------------------------===//

// One AssemblyRef per defining assembly, in first-use order: the version,
// name and culture of its Assembly row, Flags 0, the 8-byte public-key token
// (none without a key) and no hash.
uint32_t BCLBinder::Impl::getAssemblyRef(const ReferenceAssemblyFile &file) {
  auto [it, inserted] = assemblyRefs.try_emplace(&file, 0);
  if (!inserted)
    return it->second;
  const AssemblyIdentity &id = file.getIdentity();
  cli::AssemblyRefInfo ref;
  ref.Name = id.Name;
  ref.Version.Major = id.Major;
  ref.Version.Minor = id.Minor;
  ref.Version.Build = id.Build;
  ref.Version.Revision = id.Revision;
  if (id.PublicKeyToken)
    ref.PublicKeyOrToken.assign(id.PublicKeyToken->begin(),
                                id.PublicKeyToken->end());
  // An empty culture is the neutral one, however the Assembly row spells it.
  if (!StringRef(id.Culture).equals_insensitive("neutral"))
    ref.Culture = id.Culture;
  uint32_t token = out.getToken(out.addAssemblyRef(ref));
  assemblyRefs[&file] = token;
  return token;
}

// Plan 5.11.2: binding a type of an implementation assembly
// (System.Private.*, without [ReferenceAssembly]) ties the output to one
// runtime build. Reported once per assembly, naming the first such type.
void BCLBinder::Impl::warnPrivate(const ReferenceAssemblyFile &file,
                                  StringRef typeName) {
  if (!StringRef(file.getIdentity().Name)
           .starts_with_insensitive("System.Private.") ||
      file.isReferenceAssembly() || !warnedPrivate.insert(&file).second)
    return;
  warn(getDisplayPath(file.getPath()) +
       " is not a reference assembly; binding '" + typeName + "' to " +
       file.getIdentity().str() + " ties the output to one runtime build");
}

// The TypeRef of a definition, scoped to its assembly's AssemblyRef, or a
// nested type to its enclosing type's TypeRef (plan 5.11.6).
Expected<uint32_t> BCLBinder::Impl::emitType(const TypeDefinition &type,
                                             unsigned depth) {
  auto key = std::make_pair(type.file, type.token);
  if (auto it = typeRefs.find(key); it != typeRefs.end())
    return it->second;
  const ReferenceAssemblyFile &file = *type.file;
  if (depth > maxTypeNestingDepth)
    return bindError(getDisplayPath(file.getPath()) +
                     ": type nesting too deep");
  const cli::MetadataReader &r = file.getReader();
  Expected<cli::TypeDefRow> row = r.getTypeDef(cli::getTokenRow(type.token));
  if (!row)
    return fileError(file, row.takeError());
  Expected<uint32_t> enclosing = r.getEnclosingType(type.token);
  if (!enclosing)
    return fileError(file, enclosing.takeError());

  OutputType info;
  info.def = type;
  cli::EntityHandle scope;
  if (*enclosing) {
    Expected<uint32_t> outer = emitType({type.file, *enclosing}, depth + 1);
    if (!outer)
      return outer.takeError();
    const OutputType &o = outputTypes[*outer];
    info.ns = o.ns;
    info.name = o.name + "/" + joinTypeName(row->Namespace, row->Name);
    scope = cli::EntityHandle{*outer};
  } else {
    info.ns = row->Namespace.str();
    info.name = row->Name.str();
    scope = cli::EntityHandle{getAssemblyRef(file)};
  }
  std::string full = joinTypeName(info.ns, info.name);
  warnPrivate(file, full);
  uint32_t token =
      out.getToken(out.addTypeRef(scope, row->Namespace, row->Name));
  info.display = "[" + file.getIdentity().Name + "]" + full;
  outputTypes[token] = std::move(info);
  typeRefs[key] = token;
  return token;
}

uint32_t BCLBinder::Impl::addTypeSpec(const cli::TypeSig &spec) {
  SmallVector<uint8_t, 16> blob;
  cli::encodeTypeSpec(spec, blob);
  uint32_t token = out.getToken(out.addTypeSpec(out.addBlob(blob)));
  OutputType &t = outputTypes[token];
  if (!t.spec) {
    t.spec = spec;
    raw_string_ostream os(t.display);
    cli::printTypeSig(os, spec, [&](raw_ostream &os, uint32_t ref) {
      printDisplayRef(os, ref);
    });
  }
  return token;
}

// The output token of a type token of a definition's signature in \p file:
// a TypeDef or TypeRef binds by its full name like any type request, so
// every type of a bound signature, modifiers included, binds by the rules
// of plan 5.11.4; a TypeSpec is re-encoded with output tokens.
Expected<uint32_t>
BCLBinder::Impl::mapDefinitionType(const ReferenceAssemblyFile &file,
                                   uint32_t token, unsigned depth) {
  if (depth > maxTypeNestingDepth)
    return bindError(getDisplayPath(file.getPath()) +
                     ": type nesting too deep");
  const cli::MetadataReader &r = file.getReader();
  if (cli::getTokenType(token) ==
      static_cast<uint8_t>(cli::TableId::TypeSpec)) {
    Expected<cli::TypeSpecRow> row = r.getTypeSpec(cli::getTokenRow(token));
    if (!row)
      return fileError(file, row.takeError());
    Expected<ArrayRef<uint8_t>> blob = r.getBlob(row->Signature);
    if (!blob)
      return fileError(file, blob.takeError());
    Expected<cli::TypeSig> spec = cli::decodeTypeSpec(*blob);
    if (!spec)
      return fileError(file, spec.takeError());
    std::optional<Failure> failed;
    cli::TypeSig mapped = cli::mapTypeRefs(*spec, [&](uint32_t t) -> uint32_t {
      if (failed)
        return t;
      Expected<uint32_t> m = mapDefinitionType(file, t, depth + 1);
      if (!m) {
        failed = toFailure(m.takeError());
        return t;
      }
      return *m;
    });
    if (failed)
      return toError(*failed);
    return addTypeSpec(mapped);
  }
  auto name = getIndexTypeName(r, token);
  if (!name)
    return fileError(file, name.takeError());
  return bindTypeByName(name->first, name->second);
}

//===----------------------------------------------------------------------===//
// Requests
//===----------------------------------------------------------------------===//

Expected<uint32_t> BCLBinder::Impl::bindTypeByName(StringRef ns,
                                                   StringRef name) {
  if (builtin) {
    if (!findBuiltinType(ns, name))
      return notInTable(joinTypeName(ns, name));
    return emitBuiltinType(ns, name);
  }
  Expected<TypeDefinition> d = resolveType(ns, name);
  if (!d)
    return d.takeError();
  if (requiresPreview(*d->file, d->token))
    return bindError("'" + joinTypeName(ns, name) +
                     "' requires preview features");
  return emitType(*d);
}

// A method or field request (plan 5.11.4, 5.11.6): the MemberRef on the
// requested type, whose signature is the definition's re-encoded with
// output tokens.
Expected<uint32_t> BCLBinder::Impl::bindMemberRequest(const BCLRequest &r) {
  if (builtin) {
    if (r.K == BCLRequest::Field)
      return notInTable(formatRequest(r));
    const cli::BCLMemberInfo *info = findBuiltinMethod(r);
    if (!info)
      return notInTable(formatRequest(r));
    return emitBuiltinMember(*info);
  }

  Expected<TypeDefinition> parent = resolveType(r.Namespace, r.Name);
  if (!parent)
    return parent.takeError();
  Expected<MemberDefinition> def = resolveMember(r, *parent);
  if (!def)
    return def.takeError();
  const ReferenceAssemblyFile &file = *def->owner.file;
  const cli::MetadataReader &rd = file.getReader();
  if (requiresPreview(file, {def->token, def->owner.token}))
    return bindError("'" + formatRequest(r) + "' requires preview features");
  if (hasCustomAttribute(rd, def->token, obsoleteAttribute) &&
      warnedObsolete.insert({&file, def->token}).second)
    warn("'" + formatRequest(r) + "' is obsolete in " +
         file.getIdentity().str());

  // The parent's TypeRef, then the signature's types, then the blob and the
  // MemberRef.
  Expected<uint32_t> parentRef = bindTypeByName(r.Namespace, r.Name);
  if (!parentRef)
    return parentRef.takeError();

  std::optional<Failure> failed;
  auto map = [&](uint32_t t) -> uint32_t {
    if (failed)
      return t;
    Expected<uint32_t> m = mapDefinitionType(file, t, 0);
    if (!m) {
      failed = toFailure(m.takeError());
      return t;
    }
    return *m;
  };
  SmallVector<uint8_t, 16> blob;
  StringRef name;
  std::string text;
  raw_string_ostream os(text);
  auto display = [&](raw_ostream &os, uint32_t t) { printDisplayRef(os, t); };
  if (r.K == BCLRequest::Field) {
    Expected<cli::FieldRow> f = rd.getField(cli::getTokenRow(def->token));
    if (!f)
      return fileError(file, f.takeError());
    Expected<ArrayRef<uint8_t>> bytes = rd.getBlob(f->Signature);
    if (!bytes)
      return fileError(file, bytes.takeError());
    Expected<cli::TypeSig> t = cli::decodeFieldSig(*bytes);
    if (!t)
      return fileError(file, t.takeError());
    cli::TypeSig mapped = cli::mapTypeRefs(*t, map);
    if (failed)
      return toError(*failed);
    cli::encodeFieldSig(mapped, blob);
    name = f->Name;
    cli::printTypeSig(os, mapped, display);
    os << ' ';
    printDisplayRef(os, *parentRef);
    os << "::" << name;
  } else {
    Expected<cli::MethodDefRow> m =
        rd.getMethodDef(cli::getTokenRow(def->token));
    if (!m)
      return fileError(file, m.takeError());
    Expected<ArrayRef<uint8_t>> bytes = rd.getBlob(m->Signature);
    if (!bytes)
      return fileError(file, bytes.takeError());
    Expected<cli::MethodSig> sig = cli::decodeMethodSig(*bytes);
    if (!sig)
      return fileError(file, sig.takeError());
    cli::MethodSig mapped = cli::mapTypeRefs(*sig, map);
    if (failed)
      return toError(*failed);
    // A vararg call site keeps its arguments after the sentinel.
    const cli::MethodSig &req = *r.MethodSignature;
    if (req.RequiredParamCount < req.Params.size()) {
      mapped.RequiredParamCount = mapped.Params.size();
      mapped.Params.insert(mapped.Params.end(),
                           req.Params.begin() + req.RequiredParamCount,
                           req.Params.end());
    }
    cli::encodeMethodSig(mapped, blob);
    name = m->Name;
    if (mapped.Header & cli::SIG_HASTHIS)
      os << "instance ";
    cli::printTypeSig(os, mapped.Return, display);
    os << ' ';
    printDisplayRef(os, *parentRef);
    os << "::" << name << '(';
    for (size_t i = 0, e = mapped.Params.size(); i != e; ++i) {
      if (i)
        os << ", ";
      if (i == mapped.RequiredParamCount)
        os << "..., ";
      cli::printTypeSig(os, mapped.Params[i], display);
    }
    os << ')';
  }
  uint32_t token = out.getToken(
      out.addMemberRef(cli::EntityHandle{*parentRef}, name, out.addBlob(blob)));
  memberTexts.try_emplace(token, std::move(text));
  return token;
}

Expected<uint32_t> BCLBinder::Impl::bindEntity(const BCLRequest &r) {
  switch (r.K) {
  case BCLRequest::Type:
    return bindTypeByName(r.Namespace, r.Name);
  case BCLRequest::TypeName: {
    Expected<ParsedTypeName> p = parseTypeName(r.Name);
    if (!p)
      return p.takeError();
    return bindTypeByName(p->ns, p->name);
  }
  case BCLRequest::Method:
  case BCLRequest::AttributeCtor:
  case BCLRequest::Field:
    return bindMemberRequest(r);
  case BCLRequest::TypeSpec:
    if (!r.FieldOrSpec)
      return bindError("BCL type specification request without a type");
    return addTypeSpec(*r.FieldOrSpec);
  case BCLRequest::MethodSpec: {
    BCLRequest method = r;
    method.K = BCLRequest::Method;
    Expected<uint32_t> m = bindMemberRequest(method);
    if (!m)
      return m.takeError();
    if (r.Args.empty())
      return bindError("generic method instantiation of '" + formatRequest(r) +
                       "' without type arguments");
    SmallVector<uint8_t, 16> blob;
    cli::encodeMethodSpec(r.Args, blob);
    return out.getToken(
        out.addMethodSpec(cli::EntityHandle{*m}, out.addBlob(blob)));
  }
  }
  llvm_unreachable("unknown BCL request kind");
}

Expected<uint32_t> BCLBinder::Impl::bindMemberId(cli::BCLMemberId id) {
  const cli::BCLMemberInfo &info = cli::getBCLMemberInfo(id);
  if (info.isType())
    return bindTypeByName(info.Namespace, info.Name);
  if (builtin) {
    if (!info.MscorlibSafe) {
      std::string text;
      raw_string_ostream os(text);
      cli::printBCLMemberRef(os, id, "");
      return notInTable(text);
    }
    return emitBuiltinMember(info);
  }
  if (auto it = memberIds.find(static_cast<unsigned>(id));
      it != memberIds.end())
    return it->second;
  Expected<BCLRequest> r =
      getBCLMemberRequest(id, [&](StringRef ns, StringRef name, bool) {
        return bindTypeByName(ns, name);
      });
  if (!r)
    return r.takeError();
  Expected<uint32_t> token = bindMemberRequest(*r);
  if (token)
    memberIds[static_cast<unsigned>(id)] = *token;
  return token;
}

// A System.Type value (plan 5.11.5's TypeName): the assembly-qualified name
// of the bound definition, as Roslyn writes it
// (recon/p2/bcl-binding-files/roslyn/uco-blob.txt). No row is written: the
// runtime loads the assembly by that name.
Expected<std::string> BCLBinder::Impl::bindTypeName(StringRef text) {
  Expected<ParsedTypeName> p = parseTypeName(text);
  if (!p)
    return p.takeError();
  if (builtin) {
    if (!findBuiltinType(p->ns, p->name))
      return notInTable(joinTypeName(p->ns, p->name));
    std::string typePart;
    SmallVector<StringRef, 2> names;
    StringRef(p->name).split(names, '/');
    for (auto [i, n] : enumerate(names))
      typePart += i ? "+" + escapeTypeNamePart(n)
                    : escapeTypeNamePart(joinTypeName(p->ns, n));
    return typePart + ", mscorlib, Version=4.0.0.0, Culture=neutral, "
                      "PublicKeyToken=b77a5c561934e089";
  }
  Expected<TypeDefinition> d = resolveType(p->ns, p->name);
  if (!d)
    return d.takeError();
  if (requiresPreview(*d->file, d->token))
    return bindError("'" + joinTypeName(p->ns, p->name) +
                     "' requires preview features");
  return getAssemblyQualifiedName(*d);
}

std::string
BCLBinder::Impl::getAssemblyQualifiedName(const TypeDefinition &type) {
  const cli::MetadataReader &r = type.file->getReader();
  // The type and its enclosing types, innermost first.
  SmallVector<std::string, 2> parts;
  uint32_t token = type.token;
  for (unsigned depth = 0; token && depth <= maxTypeNestingDepth; ++depth) {
    Expected<cli::TypeDefRow> row = r.getTypeDef(cli::getTokenRow(token));
    Expected<uint32_t> enclosing = r.getEnclosingType(token);
    if (!row || !enclosing) {
      consumeError(row.takeError());
      consumeError(enclosing.takeError());
      break;
    }
    parts.push_back(
        escapeTypeNamePart(joinTypeName(row->Namespace, row->Name)));
    token = *enclosing;
  }
  std::string s;
  for (auto [i, p] : enumerate(reverse(parts)))
    s += (i ? "+" : "") + p;
  const AssemblyIdentity &id = type.file->getIdentity();
  s += ", " + id.Name + ", Version=" + id.getVersionString() + ", Culture=" +
       (id.Culture.empty() ? std::string("neutral") : id.Culture) +
       ", PublicKeyToken=" + id.getPublicKeyTokenString();
  return s;
}

bool BCLBinder::Impl::isDefined(const BCLRequest &r) {
  switch (r.K) {
  case BCLRequest::Type:
  case BCLRequest::TypeName: {
    std::string ns = r.Namespace.str(), name = r.Name.str();
    if (r.K == BCLRequest::TypeName) {
      Expected<ParsedTypeName> p = parseTypeName(r.Name);
      if (!p) {
        consumeError(p.takeError());
        return false;
      }
      ns = p->ns;
      name = p->name;
    }
    if (builtin)
      return findBuiltinType(ns, name);
    Expected<TypeDefinition> d = resolveType(ns, name);
    if (!d) {
      consumeError(d.takeError());
      return false;
    }
    return !requiresPreview(*d->file, d->token);
  }
  case BCLRequest::Method:
  case BCLRequest::AttributeCtor:
  case BCLRequest::Field:
  case BCLRequest::MethodSpec: {
    if (builtin)
      return r.K != BCLRequest::Field && findBuiltinMethod(r);
    Expected<TypeDefinition> parent = resolveType(r.Namespace, r.Name);
    if (!parent) {
      consumeError(parent.takeError());
      return false;
    }
    if (requiresPreview(*parent->file, parent->token))
      return false;
    BCLRequest member = r;
    if (member.K == BCLRequest::MethodSpec)
      member.K = BCLRequest::Method;
    Expected<MemberDefinition> def = resolveMember(member, *parent);
    if (!def) {
      consumeError(def.takeError());
      return false;
    }
    return !requiresPreview(*def->owner.file, {def->token, def->owner.token});
  }
  case BCLRequest::TypeSpec:
    return r.FieldOrSpec.has_value();
  }
  return false;
}

// The helper an OptionalSubstitute request calls instead (plan 5.11.5): the
// Fallback of its BCLMembers.def entry, or, for a request without one, a
// name derived from the member's.
std::string BCLBinder::Impl::getHelperSymbol(const BCLRequest &r) {
  std::string derived;
  for (const cli::BCLMemberInfo &info : cli::getAllBCLMembers()) {
    if (!info.isMethod() || info.Name != r.Member)
      continue;
    const cli::BCLMemberInfo &owner = cli::getBCLMemberInfo(info.Owner);
    if (owner.Namespace != r.Namespace || owner.Name != r.Name)
      continue;
    if (!info.Fallback.empty())
      return info.Fallback.str();
    return ("__msil_bcl_" + info.IdName).str();
  }
  for (char c : (r.Name + "_" + r.Member).str())
    derived += isAlnum(c) ? c : '_';
  return "__msil_bcl_" + derived;
}

// Plan 5.11.5's --verbose note for an optional request that falls back.
void BCLBinder::Impl::noteFallback(const BCLRequest &r) {
  std::string what = r.Strength == RequestStrength::OptionalDrop
                         ? std::string("omitted")
                         : "using helper '" + getHelperSymbol(r) + "'";
  log("note: '" + formatRequest(r) + "' is not defined by the references; " +
      what + " (requested by " + getRequester(r) + ")");
}

Error BCLBinder::Impl::rewriteValue(cli::CAValue &v, unsigned depth) {
  if (depth > maxTypeNestingDepth)
    return bindError("invalid custom attribute blob: values nested too deep");
  auto rewrite = [&](std::string &name) -> Error {
    Expected<std::string> q = bindTypeName(name);
    if (!q)
      return q.takeError();
    name = std::move(*q);
    return Error::success();
  };
  switch (v.K) {
  case cli::CAValue::Kind::Type:
    if (v.Str)
      return rewrite(*v.Str);
    return Error::success();
  case cli::CAValue::Kind::Enum:
    if (!v.EnumTypeName.empty())
      return rewrite(v.EnumTypeName);
    return Error::success();
  case cli::CAValue::Kind::Boxed:
    if (v.BoxedValue)
      return rewriteValue(*v.BoxedValue, depth + 1);
    return Error::success();
  case cli::CAValue::Kind::Array:
    if (v.EnumUnderlying ==
            static_cast<cli::ElementType>(cli::SERIALIZATION_TYPE_ENUM) &&
        !v.EnumTypeName.empty())
      if (Error e = rewrite(v.EnumTypeName))
        return e;
    if (v.Elements)
      for (cli::CAValue &e : *v.Elements)
        if (Error err = rewriteValue(e, depth + 1))
          return err;
    return Error::success();
  default:
    return Error::success();
  }
}

// Plan 5.11.4 and 5.11.6: every System.Type value of the blob (fixed and
// named arguments, boxed values and array elements, and the serialized
// names of enums), bound like a type and written as the assembly-qualified
// name of its definition; the blob is decoded and encoded with the enums'
// underlying types from the references.
Error BCLBinder::Impl::rewriteAttributeBlob(const cli::MethodSig &ctor,
                                            ArrayRef<uint8_t> in,
                                            SmallVectorImpl<uint8_t> &blob) {
  auto resolveEnum = [&](uint32_t typeRef) -> Expected<cli::ElementType> {
    auto it = outputTypes.find(typeRef);
    if (it == outputTypes.end() || it->second.spec)
      return bindError("custom attribute constructor names an enum type "
                       "that msil-ld did not bind");
    if (!it->second.def.file)
      return notInTable(joinTypeName(it->second.ns, it->second.name));
    OutputType t = it->second;
    return getEnumUnderlyingType(t.def, joinTypeName(t.ns, t.name));
  };
  auto resolveEnumName =
      [&](StringRef serializedName) -> Expected<cli::ElementType> {
    Expected<ParsedTypeName> p = parseTypeName(serializedName);
    if (!p)
      return p.takeError();
    return getEnumUnderlyingType(p->ns, p->name);
  };
  Expected<cli::CustomAttributeValue> v =
      cli::decodeCustomAttribute(ctor, in, resolveEnum, resolveEnumName);
  if (!v)
    return v.takeError();
  for (cli::CAValue &a : v->FixedArgs)
    if (Error e = rewriteValue(a, 0))
      return e;
  for (cli::CANamedArg &a : v->NamedArgs)
    if (Error e = rewriteValue(a.Value, 0))
      return e;
  return cli::encodeCustomAttribute(ctor, *v, blob, resolveEnum);
}

// --print-bcl-binding (plan 5.11.5): the result of one request, or its
// error text.
std::string BCLBinder::Impl::describe(StringRef request) {
  auto bind = [&](const BCLRequest &r) -> std::string {
    Expected<uint32_t> token = bindEntity(r);
    if (!token) {
      Failure f = toFailure(token.takeError());
      if (r.Strength == RequestStrength::Required || !f.notDefined)
        return f.msg;
      noteFallback(r);
      return r.Strength == RequestStrength::OptionalDrop
                 ? std::string("omitted")
                 : "helper '" + getHelperSymbol(r) + "'";
    }
    if (auto it = memberTexts.find(*token); it != memberTexts.end())
      return it->second;
    std::string text;
    raw_string_ostream os(text);
    printDisplayRef(os, *token);
    return text;
  };

  StringRef rest = request;
  if (rest.consume_front("type:")) {
    auto [ns, name] = splitTypeName(rest);
    BCLRequest r;
    r.K = BCLRequest::Type;
    r.Namespace = ns;
    r.Name = name;
    return bind(r);
  }
  if (rest.consume_front("typename:")) {
    Expected<std::string> q = bindTypeName(rest);
    if (!q)
      return toString(q.takeError());
    return *q;
  }
  if (rest.consume_front("member:")) {
    auto [idName, suffix] = rest.split(':');
    const cli::BCLMemberInfo *info = nullptr;
    for (const cli::BCLMemberInfo &i : cli::getAllBCLMembers())
      if (i.IdName == idName)
        info = &i;
    if (!info)
      return ("unknown BCL member '" + idName + "'").str();
    RequestStrength strength = static_cast<RequestStrength>(info->Strength);
    if (suffix == "drop")
      strength = RequestStrength::OptionalDrop;
    else if (suffix == "substitute")
      strength = RequestStrength::OptionalSubstitute;
    else if (!suffix.empty())
      return ("unknown strength '" + suffix +
              "'; expected 'drop' or 'substitute'")
          .str();
    Expected<BCLRequest> r = getBCLMemberRequest(
        info->Id,
        [&](StringRef ns, StringRef name, bool) -> Expected<uint32_t> {
          return bindTypeByName(ns, name);
        });
    if (!r)
      return toString(r.takeError());
    r->Strength = strength;
    return bind(*r);
  }
  if (rest.consume_front("attr:"))
    return describeAttribute(request, rest);
  return ("unknown request '" + request +
          "'; expected type:<Ns>.<Name>[/<Nested>], "
          "member:<BCLMemberId>[:drop|:substitute], typename:<name> or "
          "attr:(<params>):<hex>")
      .str();
}

// The request attr:(<params>):<hex> (plan 5.11.5): the custom-attribute blob
// <hex> rewritten by rewriteAttributeBlob for the constructor
// 'instance void .ctor(<params>)', its parameters in BCLMembers.def's
// signature text, class and value types bound by name; the result in
// uppercase hex digits.
std::string BCLBinder::Impl::describeAttribute(StringRef request,
                                               StringRef rest) {
  size_t close = rest.find("):");
  if (!rest.starts_with("(") || close == StringRef::npos)
    return ("malformed request '" + request +
            "'; expected attr:(<params>):<hex>")
        .str();
  StringRef params = rest.slice(1, close);
  StringRef hex = rest.drop_front(close + 2);
  std::string in;
  if (hex.size() % 2 || !tryGetFromHex(hex, in))
    return ("the blob of request '" + request +
            "' is not two hex digits per byte")
        .str();
  Expected<cli::MethodSig> ctor = parseBCLMethodSig(
      "void", params, /*hasThis=*/true, "the attribute constructor",
      [&](StringRef ns, StringRef name, bool) {
        return bindTypeByName(ns, name);
      });
  if (!ctor)
    return toString(ctor.takeError());
  SmallVector<uint8_t, 64> blob;
  if (Error e = rewriteAttributeBlob(*ctor, arrayRefFromStringRef(in), blob))
    return toString(std::move(e));
  return toHex(blob, /*LowerCase=*/false);
}

//===----------------------------------------------------------------------===//
// BCLBinder
//===----------------------------------------------------------------------===//

BCLBinder::BCLBinder(const ReferenceSet &Refs, cli::MetadataBuilder &Out)
    : I(std::make_unique<Impl>(Refs, Out)) {}

BCLBinder::~BCLBinder() = default;

Expected<std::optional<cli::EntityHandle>>
BCLBinder::bind(const BCLRequest &R) {
  Expected<uint32_t> token = I->bindEntity(R);
  if (token)
    return std::optional<cli::EntityHandle>(cli::EntityHandle{*token});
  if (R.Strength == RequestStrength::Required)
    return token.takeError();
  // An optional request falls back when the references do not define what
  // it names (plan 5.11.5); any other failure stays an error.
  Failure f = toFailure(token.takeError());
  if (!f.notDefined)
    return toError(f);
  I->noteFallback(R);
  return std::nullopt;
}

bool BCLBinder::isDefined(const BCLRequest &R) { return I->isDefined(R); }

Expected<cli::EntityHandle> BCLBinder::bindType(StringRef Ns, StringRef Name) {
  Expected<uint32_t> token = I->bindTypeByName(Ns, Name);
  if (!token)
    return token.takeError();
  return cli::EntityHandle{*token};
}

Expected<cli::EntityHandle> BCLBinder::bindMember(cli::BCLMemberId Id) {
  Expected<uint32_t> token = I->bindMemberId(Id);
  if (!token)
    return token.takeError();
  return cli::EntityHandle{*token};
}

Error BCLBinder::rewriteAttributeBlob(const cli::MethodSig &Ctor,
                                      ArrayRef<uint8_t> In,
                                      SmallVectorImpl<uint8_t> &Out) {
  return I->rewriteAttributeBlob(Ctor, In, Out);
}

void BCLBinder::printBinding(StringRef Request, raw_ostream &OS) {
  OS << Request << " -> " << I->describe(Request) << '\n';
}

void lld::msil::printBuiltinBCLTable(raw_ostream &OS) {
  for (const cli::BCLMemberInfo &info : cli::getAllBCLMembers()) {
    if (!info.MscorlibSafe)
      continue;
    OS << (info.isType() ? "type " : "method ");
    cli::printBCLMemberRef(OS, info.Id, cli::MscorlibAssembly);
    OS << '\n';
  }
}
