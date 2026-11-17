//===- ManagedCalls.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// msil-ld's checks of managed calls (ManagedCalls.h; plan 5.20.7 MK2-MK4,
// MK7).
//
// A stub's body has the shape of plan 5.20.5: the argument conversions, one
// member access (call, callvirt, newobj, newarr, ldsfld or ldfld), the result
// conversion and, with msil_catch, one filter clause whose filter tests
// 'isinst <T>' per listed type. The access is the one call-like instruction
// whose token is not one of the stub-internal members of 5.20.13 that the
// conversions call (GCHandle's Alloc, ToIntPtr, FromIntPtr and get_Target,
// the UTF-8 and UTF-32 encoders' constructors, Encoding::GetString(uint8*,
// int32) and String::.ctor(char*)), except that a call directly after a
// 'constrained.' prefix is the access whatever its token names (the
// 'this-val' or 'this-gch' access of plan 5.20.5 ML2 (2) as amended). When
// an unprefixed member is itself one of them, no call-like instruction
// remains; such a member has no parameter that MK2 checks, so nothing is
// lost. The member's parameters pair with the stub's
// own parameters by plan 5.20.3 MS4's positions: a 'this' handle first for
// an instance member other than a constructor.
//
// A type that the references do not define is skipped: binding the stub's
// tokens reports it with the binder's text.
//
//===----------------------------------------------------------------------===//

#include "ManagedCalls.h"
#include "BCLBinder.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputFiles.h"
#include "LTO.h"
#include "ReferenceSet.h"
#include "Synthetic.h"
#include "TokenMap.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/ILDecoder.h"
#include "llvm/CLI/ManagedCallIR.h"
#include "llvm/CLI/ManagedSignature.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/CLI/Signature.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticHandler.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/LTO/LTO.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

using namespace llvm;
using namespace llvm::cli;
using namespace lld;
using namespace lld::msil;

static constexpr StringLiteral systemException = "System.Exception";

// The width in bits of the integer element type \p et; 0 for any other type.
// native int has the width of the codegen triple's pointers.
static unsigned getIntegerWidth(ElementType et) {
  switch (et) {
  case ELEMENT_TYPE_BOOLEAN:
  case ELEMENT_TYPE_I1:
  case ELEMENT_TYPE_U1:
    return 8;
  case ELEMENT_TYPE_CHAR:
  case ELEMENT_TYPE_I2:
  case ELEMENT_TYPE_U2:
    return 16;
  case ELEMENT_TYPE_I4:
  case ELEMENT_TYPE_U4:
    return 32;
  case ELEMENT_TYPE_I8:
  case ELEMENT_TYPE_U8:
    return 64;
  case ELEMENT_TYPE_I:
  case ELEMENT_TYPE_U:
    return ctx.arg.target.isArch64Bit() ? 64 : 32;
  default:
    return 0;
  }
}

namespace {
// The checks of both forms, on type names ("Ns.Name", nested parts
// 'Outer/Inner'); each text is reported once per link.
class Checker {
public:
  explicit Checker(BCLBinder &binder) : binder(binder) {}

  // MK2: \p width bits passed for the member's 1-based CIL parameter
  // \p position, of type 'valuetype \p e' (T-M7).
  void checkEnum(StringRef f, size_t position, unsigned width, StringRef e);
  // MK2: a result of \p width bits read from the member's result (a field
  // member's value) of type 'valuetype \p e' (T-M7's second form).
  void checkEnumResult(StringRef f, unsigned width, StringRef e);
  // MK3: \p t used as a value type (\p asValue) or as a reference type
  // (T-M16).
  void checkKind(StringRef f, StringRef t, bool asValue);
  // MK4: the msil_catch type \p t (T-M9).
  void checkCatch(StringRef f, StringRef t);
  // MK7: the field member \p field of \p t (T-M18).
  void checkLiteral(StringRef f, StringRef t, StringRef field);
  // MK2, MK3 without references: the value type \p v (T-M8). GCHandle, whose
  // kind BCLMembers.def's own rows fix, is exempt.
  void reportValueType(StringRef f, StringRef v);

private:
  std::optional<BCLTypeKind> getKind(StringRef t);
  // True if \p e is an enum of \p width bits, false if it is a struct or an
  // enum of another width; std::nullopt when the references do not tell.
  std::optional<bool> isEnumOfWidth(StringRef e, unsigned width);
  void report(const Twine &msg);

  BCLBinder &binder;
  StringSet<> reported;
};
} // namespace

void Checker::report(const Twine &msg) {
  std::string text = msg.str();
  if (reported.insert(text).second)
    error(text);
}

std::optional<BCLTypeKind> Checker::getKind(StringRef t) {
  auto [ns, name] = splitTypeName(t);
  Expected<BCLTypeKind> kind = binder.getTypeKind(ns, name);
  if (!kind) {
    consumeError(kind.takeError());
    return std::nullopt;
  }
  return *kind;
}

std::optional<bool> Checker::isEnumOfWidth(StringRef e, unsigned width) {
  std::optional<BCLTypeKind> kind = getKind(e);
  if (!kind)
    return std::nullopt;
  if (*kind != BCLTypeKind::Enum)
    return false;
  auto [ns, name] = splitTypeName(e);
  Expected<ElementType> underlying = binder.getEnumUnderlyingType(ns, name);
  if (!underlying) {
    consumeError(underlying.takeError());
    return std::nullopt;
  }
  return getIntegerWidth(*underlying) == width;
}

void Checker::checkEnum(StringRef f, size_t position, unsigned width,
                        StringRef e) {
  if (isEnumOfWidth(e, width).value_or(true))
    return;
  report("managed call '" + f + "': parameter " + Twine(position) + " passes " +
         Twine(width) + " bits for '" + e +
         "', which is not an enum of that width");
}

void Checker::checkEnumResult(StringRef f, unsigned width, StringRef e) {
  if (isEnumOfWidth(e, width).value_or(true))
    return;
  report("managed call '" + f + "': the result has " + Twine(width) +
         " bits for '" + e + "', which is not an enum of that width");
}

void Checker::checkKind(StringRef f, StringRef t, bool asValue) {
  std::optional<BCLTypeKind> kind = getKind(t);
  if (!kind)
    return;
  bool isValue = *kind != BCLTypeKind::Reference;
  if (isValue == asValue)
    return;
  report("managed call '" + f + "': '" + t + "' is a " +
         (isValue ? "value" : "reference") +
         " type, but the declaration uses it as a " +
         (asValue ? "value" : "reference") + " type");
}

void Checker::checkCatch(StringRef f, StringRef t) {
  if (t == systemException)
    return;
  auto [ns, name] = splitTypeName(t);
  Expected<SmallVector<std::string, 4>> bases = binder.getBaseTypes(ns, name);
  if (!bases) {
    consumeError(bases.takeError());
    return;
  }
  if (is_contained(*bases, systemException))
    return;
  report("managed call '" + f + "': 'msil_catch' type '" + t +
         "' does not derive from System.Exception");
}

void Checker::checkLiteral(StringRef f, StringRef t, StringRef field) {
  auto [ns, name] = splitTypeName(t);
  Expected<FieldAttributes> flags = binder.getFieldFlags(ns, name, field);
  if (!flags) {
    consumeError(flags.takeError());
    return;
  }
  if ((*flags & FieldAttributes::Literal) != FieldAttributes::Literal)
    return;
  report("managed call '" + f + "': reading the literal field '" + t +
         "::" + field + "' is not supported for MSIL yet");
}

void Checker::reportValueType(StringRef f, StringRef v) {
  if (v == GCHandleTypeName)
    return;
  report("managed call '" + f + "': '" + v +
         "' is a value type, which msil-ld checks only against references; "
         "pass --reference=<reference assemblies>");
}

//===----------------------------------------------------------------------===//
// Object links: the stubs' bodies
//===----------------------------------------------------------------------===//

namespace {
// A call-like token operand of a stub on a MemberRef: the member, its
// parent's full name, and its method or field signature with the object's
// tokens. \c valueTypeParent is set for a parent that is a TypeSpec
// 'valuetype T' rather than T's TypeRef: an object gives a static method or
// static field that way when the declaration's TYPE is 'valuetype T' (plan
// 5.20.6 MW1 as amended by the row 'MK3 check everywhere').
struct MemberAccess {
  Opcode op;
  std::string type;
  StringRef member;
  std::optional<MethodSig> method;
  std::optional<TypeSig> field;
  bool valueTypeParent = false;
};

// One type token operand of a stub's conversions (box, unbox, unbox.any,
// castclass) or filter (isinst) on a TypeRef. For an unbox.any, \c arg is the
// stub parameter whose ldarg feeds it, when one does.
struct TypeUse {
  Opcode op;
  std::string type;
  std::optional<unsigned> arg;
};
} // namespace

// The full name of the TypeRef \p token of \p f; std::nullopt for any other
// token (a TypeSpec, such as an array type) or a TypeRef that does not read.
static std::optional<std::string> getTypeRefName(const ObjFile &f,
                                                 uint32_t token) {
  if (getTokenType(token) != static_cast<uint8_t>(TableId::TypeRef))
    return std::nullopt;
  auto name = getIndexTypeName(f.getReader(), token);
  if (!name) {
    consumeError(name.takeError());
    return std::nullopt;
  }
  return joinTypeName(name->first, name->second);
}

// The type of the TypeSpec \p token of \p f; std::nullopt for any other token
// or a TypeSpec that does not decode.
static std::optional<TypeSig> getTypeSpec(const ObjFile &f, uint32_t token) {
  if (getTokenType(token) != static_cast<uint8_t>(TableId::TypeSpec))
    return std::nullopt;
  const MetadataReader &md = f.getReader();
  Expected<TypeSpecRow> row = md.getTypeSpec(getTokenRow(token));
  if (!row) {
    consumeError(row.takeError());
    return std::nullopt;
  }
  Expected<ArrayRef<uint8_t>> blob = md.getBlob(row->Signature);
  if (!blob) {
    consumeError(blob.takeError());
    return std::nullopt;
  }
  Expected<TypeSig> type = decodeTypeSpec(*blob);
  if (!type) {
    consumeError(type.takeError());
    return std::nullopt;
  }
  return std::move(*type);
}

// The MemberRef \p token of \p f, decoded: on a TypeRef, or on a TypeSpec
// 'valuetype T' whose T is a TypeRef (MemberAccess::valueTypeParent);
// std::nullopt for another parent or a row that does not decode (binding
// the token reports that).
static std::optional<MemberAccess> decodeMemberRef(const ObjFile &f, Opcode op,
                                                   uint32_t token) {
  const MetadataReader &md = f.getReader();
  Expected<MemberRefRow> row = md.getMemberRef(getTokenRow(token));
  if (!row) {
    consumeError(row.takeError());
    return std::nullopt;
  }
  std::optional<std::string> parent;
  bool valueTypeParent = false;
  if (std::optional<TypeSig> spec = getTypeSpec(f, row->Parent)) {
    if (spec->getElementType() != ELEMENT_TYPE_VALUETYPE)
      return std::nullopt;
    parent = getTypeRefName(f, spec->getTypeRef());
    valueTypeParent = true;
  } else {
    parent = getTypeRefName(f, row->Parent);
  }
  if (!parent)
    return std::nullopt;
  Expected<ArrayRef<uint8_t>> blob = md.getBlob(row->Signature);
  if (!blob) {
    consumeError(blob.takeError());
    return std::nullopt;
  }
  MemberAccess a{op,           std::move(*parent), row->Name,
                 std::nullopt, std::nullopt,       valueTypeParent};
  if (!blob->empty() && ((*blob)[0] & SIG_KIND_MASK) == SIG_FIELD) {
    Expected<TypeSig> type = decodeFieldSig(*blob);
    if (!type) {
      consumeError(type.takeError());
      return std::nullopt;
    }
    a.field = std::move(*type);
  } else {
    Expected<MethodSig> sig = decodeMethodSig(*blob);
    if (!sig) {
      consumeError(sig.takeError());
      return std::nullopt;
    }
    a.method = std::move(*sig);
  }
  return a;
}

// The members that a stub's conversions call (plan 5.20.13, 5.20.5 ML2).
static constexpr BCLMemberId stubInternalMembers[] = {
    BCLMemberId::GCHandle_Alloc,      BCLMemberId::GCHandle_ToIntPtr,
    BCLMemberId::GCHandle_FromIntPtr, BCLMemberId::GCHandle_get_Target,
    BCLMemberId::Encoding_GetString,  BCLMemberId::UTF8Encoding_ctor,
    BCLMemberId::UTF32Encoding_ctor,  BCLMemberId::String_ctor_PChar,
};

// True if \p a is one of stubInternalMembers: its type, name, instance flag
// and signature, compared as canonical listing v1 text, as BCLMembers.def
// spells signatures.
static bool isStubInternalMember(const ObjFile &f, const MemberAccess &a) {
  if (!a.method)
    return false;
  auto printRef = [&](raw_ostream &os, uint32_t token) {
    if (std::optional<std::string> name = getTypeRefName(f, token))
      os << *name;
    else
      os << "<token>";
  };
  std::string ret, params;
  raw_string_ostream retOs(ret), paramsOs(params);
  printTypeSig(retOs, a.method->Return, printRef);
  ListSeparator ls;
  for (const TypeSig &p : a.method->Params) {
    paramsOs << ls;
    printTypeSig(paramsOs, p, printRef);
  }
  bool hasThis = a.method->Header & SIG_HASTHIS;
  for (BCLMemberId id : stubInternalMembers) {
    const BCLMemberInfo &info = getBCLMemberInfo(id);
    const BCLMemberInfo &owner = getBCLMemberInfo(info.Owner);
    if (info.Name == a.member && info.HasThis == hasThis &&
        info.Return == ret && info.Params == params &&
        joinTypeName(owner.Namespace, owner.Name) == a.type)
      return true;
  }
  return false;
}

// The name of a 'valuetype' TypeSig of \p f; std::nullopt for other types.
static std::optional<std::string> getValueTypeName(const ObjFile &f,
                                                   const TypeSig &t) {
  if (t.getElementType() != ELEMENT_TYPE_VALUETYPE)
    return std::nullopt;
  return getTypeRefName(f, t.getTypeRef());
}

namespace {
// What the token sites of a stub hold by 5.20.5's shape (scanStubSites).
struct StubSites {
  // The conversions' type tokens and the filter's isinst types, in code
  // order.
  SmallVector<TypeUse, 4> conversions;
  SmallVector<std::string, 2> catches;
  // The member access, decoded: std::nullopt for a newarr, or for a
  // MemberRef that does not decode (binding its token reports that).
  std::optional<MemberAccess> access;
  unsigned numAccesses = 0;
  // The 'constrained.' prefixes and the access's site (the last one when
  // there are several accesses), by index into the stub's sites.
  SmallVector<size_t, 1> prefixes;
  std::optional<size_t> accessSite;
};
} // namespace

// The token sites of the stub \p m of \p f outside its handlers, read by
// 5.20.5's shape (StubSites): checkStub checks them, and
// bindManagedCallAccesses binds the access of a static method by them.
// std::nullopt when the body does not decode.
static std::optional<StubSites> scanStubSites(const ObjFile &f,
                                              const InputMethod &m) {
  // The filter blocks and the handlers: a filter block runs from its offset
  // to its handler (ECMA-335 II.19).
  SmallVector<std::pair<uint32_t, uint32_t>, 1> filters, handlers;
  for (const ExceptionClause &cl : m.body.Clauses) {
    handlers.push_back({cl.HandlerOffset, cl.HandlerOffset + cl.HandlerLength});
    if (cl.Kind == ExceptionClauseKind::Filter)
      filters.push_back({cl.ClassTokenOrFilterOffset, cl.HandlerOffset});
  }
  auto isIn = [](ArrayRef<std::pair<uint32_t, uint32_t>> blocks,
                 uint32_t offset) {
    return any_of(blocks, [&](const std::pair<uint32_t, uint32_t> &b) {
      return offset >= b.first && offset < b.second;
    });
  };

  // The stub parameter whose ldarg feeds each unbox.any, by the unbox.any's
  // offset: the last ldarg before it in code order. MS5's boxed conversion of
  // parameter j is 'ldarg j; brtrue; ldnull; br; ldarg j; call
  // GCHandle::FromIntPtr; box GCHandle; unbox GCHandle; call
  // GCHandle::get_Target; unbox.any V' (5.20.9 MH1 as built). The body
  // decoded when the object was read (TokenMap::build).
  DenseMap<uint32_t, unsigned> unboxedArgs;
  std::optional<unsigned> lastArg;
  Error decoded = decodeIL(m.body.Code, [&](const ILInstruction &inst) {
    switch (inst.Op) {
    case Opcode::Ldarg_0:
      lastArg = 0;
      break;
    case Opcode::Ldarg_1:
      lastArg = 1;
      break;
    case Opcode::Ldarg_2:
      lastArg = 2;
      break;
    case Opcode::Ldarg_3:
      lastArg = 3;
      break;
    case Opcode::Ldarg_s:
    case Opcode::Ldarg:
      lastArg = static_cast<unsigned>(inst.Imm);
      break;
    case Opcode::Unbox_any:
      if (lastArg)
        unboxedArgs[inst.Offset] = *lastArg;
      break;
    default:
      break;
    }
    return Error::success();
  });
  if (decoded) {
    consumeError(std::move(decoded));
    return std::nullopt;
  }

  // The conversions' type tokens, the filter's, and the member access, in
  // code order.
  SmallVector<TypeUse, 4> conversions;
  SmallVector<std::string, 2> catches;
  std::optional<MemberAccess> access;
  unsigned numAccesses = 0;
  // The 'constrained.' prefixes and the access's site, by index into
  // m.sites.
  SmallVector<size_t, 1> prefixes;
  std::optional<size_t> accessSite;
  for (auto [index, s] : enumerate(m.sites)) {
    if (isIn(filters, s.offset)) {
      if (s.op == Opcode::Isinst)
        if (std::optional<std::string> t = getTypeRefName(f, s.token))
          catches.push_back(std::move(*t));
      continue;
    }
    if (isIn(handlers, s.offset))
      continue;
    switch (s.op) {
    case Opcode::Box:
    case Opcode::Unbox:
    case Opcode::Unbox_any:
    case Opcode::Castclass:
      if (std::optional<std::string> t = getTypeRefName(f, s.token)) {
        std::optional<unsigned> arg;
        if (s.op == Opcode::Unbox_any) {
          auto it = unboxedArgs.find(getInstructionOffset(s));
          if (it != unboxedArgs.end())
            arg = it->second;
        }
        conversions.push_back({s.op, std::move(*t), arg});
      }
      break;
    case Opcode::Constrained:
      prefixes.push_back(index);
      break;
    case Opcode::Newarr:
      ++numAccesses;
      accessSite = index;
      break;
    case Opcode::Call:
    case Opcode::Callvirt:
    case Opcode::Newobj:
    case Opcode::Ldsfld:
    case Opcode::Ldfld: {
      if (getTokenType(s.token) != static_cast<uint8_t>(TableId::MemberRef))
        break;
      std::optional<MemberAccess> a = decodeMemberRef(f, s.op, s.token);
      // The conversions never put a 'constrained.' prefix on their calls
      // (5.20.13, 5.20.3 MS5 as amended: 'call' for the GCHandle members,
      // 'callvirt' for Encoding::GetString). A call directly after one is
      // the member access even when it names one of those members: the
      // placeholder 'constrained. GCHandle' + 'callvirt
      // GCHandle::get_Target' of a 'this-gch' call is one.
      bool afterPrefix =
          index != 0 && m.sites[index - 1].op == Opcode::Constrained &&
          getInstructionOffset(s) == getInstructionOffset(m.sites[index - 1]) +
                                         getOpcodeSize(Opcode::Constrained) + 4;
      if (a && !afterPrefix && isStubInternalMember(f, *a))
        break;
      ++numAccesses;
      accessSite = index;
      access = std::move(a);
      break;
    }
    default:
      break;
    }
  }
  return StubSites{std::move(conversions), std::move(catches),
                   std::move(access),      numAccesses,
                   std::move(prefixes),    accessSite};
}

static void checkStub(Checker &c, const ObjFile &f, const InputMethod &m,
                      bool withReferences) {
  StringRef fn = getManagedCallSymbol(m.name);
  std::optional<StubSites> scan = scanStubSites(f, m);
  if (!scan)
    return;
  ArrayRef<TypeUse> conversions = scan->conversions;
  ArrayRef<std::string> catches = scan->catches;
  const std::optional<MemberAccess> &access = scan->access;
  ArrayRef<size_t> prefixes = scan->prefixes;
  std::optional<size_t> accessSite = scan->accessSite;
  if (scan->numAccesses > 1) {
    error(toString(&f) + ": managed call '" + fn +
          "': the stub's body makes more than one member access; the "
          "object needs a matching toolchain");
    return;
  }
  // The access of a 'this-val' or 'this-gch' instance method (5.20.5 ML2
  // (2) as amended): 'call V::M', or 'constrained. V' directly before a
  // 'callvirt' of a method, 'callvirt V::M' (the placeholder that
  // bindManagedCallAccesses rewrites) or 'callvirt D::M' (from LTO, with D
  // the declaring type, which bindManagedCallAccesses checks). Any other
  // 'constrained.' is not 5.20.5's shape. An access whose MemberRef does not
  // decode is reported when its token binds.
  bool undecoded =
      accessSite && !access && m.sites[*accessSite].op != Opcode::Newarr;
  if (!prefixes.empty() && !undecoded) {
    const TokenSite &p = m.sites[prefixes.front()];
    bool expected =
        prefixes.size() == 1 && accessSite &&
        *accessSite == prefixes.front() + 1 && access && access->method &&
        access->op == Opcode::Callvirt &&
        getInstructionOffset(m.sites[*accessSite]) ==
            getInstructionOffset(p) + getOpcodeSize(Opcode::Constrained) + 4;
    if (!expected) {
      error(toString(&f) + ": managed call '" + fn +
            "': the stub's body has a 'constrained.' prefix that is not "
            "before its member access; the object needs a matching "
            "toolchain");
      return;
    }
  }

  // The 'valuetype' parameters of a method access, with the stub parameter
  // that each is passed from.
  bool thisHandle = access && access->method &&
                    (access->method->Header & SIG_HASTHIS) &&
                    access->op != Opcode::Newobj;
  // A boxed handle: stub parameter \p j, which an unbox.any of \p t opens.
  auto isUnboxedAny = [&](StringRef t, size_t j) {
    return any_of(conversions, [&](const TypeUse &u) {
      return u.op == Opcode::Unbox_any && u.type == t && u.arg == j;
    });
  };

  // MK2, MK3 without references: every value type that the stub uses, other
  // than GCHandle (T-M8), the TYPE 'valuetype T' of a static member included.
  if (!withReferences) {
    for (const TypeUse &u : conversions)
      if (u.op != Opcode::Castclass)
        c.reportValueType(fn, u.type);
    if (!access)
      return;
    if (access->valueTypeParent)
      c.reportValueType(fn, access->type);
    SmallVector<const TypeSig *, 4> types;
    if (access->field) {
      types.push_back(&*access->field);
    } else if (access->method) {
      for (const TypeSig &p : access->method->Params)
        types.push_back(&p);
      types.push_back(&access->method->Return);
    }
    for (const TypeSig *t : types)
      if (std::optional<std::string> v = getValueTypeName(f, *t))
        c.reportValueType(fn, *v);
    return;
  }

  // MK3: box, unbox and unbox.any name value types, castclass reference
  // types; a constructor whose object is not boxed constructs a reference
  // type; a member whose parent is a TypeSpec 'valuetype T' (a static method
  // or field whose TYPE is 'valuetype T') is a value type's (T-M16).
  for (const TypeUse &u : conversions)
    if (u.type != GCHandleTypeName)
      c.checkKind(fn, u.type, /*asValue=*/u.op != Opcode::Castclass);
  if (access && access->op == Opcode::Newobj &&
      none_of(conversions, [&](const TypeUse &u) {
        return u.op == Opcode::Box && u.type == access->type;
      }))
    c.checkKind(fn, access->type, /*asValue=*/false);
  if (access && access->valueTypeParent && access->type != GCHandleTypeName)
    c.checkKind(fn, access->type, /*asValue=*/true);

  // MK2: a 'valuetype E' parameter passed from an integer stub parameter as
  // it is: not GCHandle (passed through FromIntPtr), and not a boxed handle,
  // which the unbox.any of that very parameter opens (T-M7). Another
  // parameter of the same type is checked like any other.
  if (access && access->method && m.signature) {
    ArrayRef<TypeSig> stubParams = m.signature->Params;
    for (auto [k, p] : enumerate(access->method->Params)) {
      std::optional<std::string> e = getValueTypeName(f, p);
      if (!e || *e == GCHandleTypeName)
        continue;
      size_t j = k + (thisHandle ? 1 : 0);
      if (j >= stubParams.size())
        continue;
      ElementType st = stubParams[j].getElementType();
      unsigned width = getIntegerWidth(st);
      if (!width || ((st == ELEMENT_TYPE_I || st == ELEMENT_TYPE_U) &&
                     isUnboxedAny(*e, j)))
        continue;
      c.checkEnum(fn, k + 1, width, *e);
    }
  }

  // MK2: a 'valuetype E' result (a field member's value) returned as an
  // integer as it is: not GCHandle, and not boxed into a handle, which
  // 'box E' does (T-M7's second form). A constructor's result is its object.
  if (access && m.signature) {
    const TypeSig *result = nullptr;
    if (access->field)
      result = &*access->field;
    else if (access->method && access->op != Opcode::Newobj)
      result = &access->method->Return;
    std::optional<std::string> e =
        result ? getValueTypeName(f, *result) : std::nullopt;
    bool boxed = e && any_of(conversions, [&](const TypeUse &u) {
                   return u.op == Opcode::Box && u.type == *e;
                 });
    if (e && *e != GCHandleTypeName && !boxed)
      if (unsigned width =
              getIntegerWidth(m.signature->Return.getElementType()))
        c.checkEnumResult(fn, width, *e);
  }

  // MK7: a literal field (T-M18).
  if (access && access->field)
    c.checkLiteral(fn, access->type, access->member);

  // MK4: the filter's types (T-M9).
  for (const std::string &t : catches)
    c.checkCatch(fn, t);
}

// Whether \p m is a managed-call stub of \p f: a method named
// '__msil_mc.<symbol>' whose FUNCTION symbol is LOCAL (plan 5.20.6 MW1 as
// amended). A GLOBAL or WEAK definition of such a name is not a stub but
// T-M23 (checkReservedManagedCallNames).
static bool isManagedCallStub(const ObjFile &f, const InputMethod &m) {
  if (!isManagedCallName(m.name))
    return false;
  ArrayRef<MSILLink::Symbol> symbols = f.getRecord().Symbols;
  return m.symbolIndex < symbols.size() &&
         (symbols[m.symbolIndex].Flags & MSILLink::BindingMask) ==
             MSILLink::BindingLocal;
}

void lld::msil::checkManagedCallStubs(BCLBinder &binder) {
  Checker c(binder);
  bool withReferences = binder.hasReferences();
  for (ObjFile *f : ctx.objectFiles)
    for (const InputMethod *m : f->methods())
      if (m->live && !m->discarded && isManagedCallStub(*f, *m))
        checkStub(c, *f, *m, withReferences);
}

// A text of the managed-call stub \p m of the CIL object \p f that names what
// its tokens refer to instead of the object's token values: its signature,
// its code with each token operand replaced by the TypeRef, TypeSpec or
// MemberRef that it names, and its exception clauses. The stubs that two
// units make of equal declarations have the same text; stubs that convert,
// access or catch differently, or that have different C prototypes, have
// different texts (plan 5.20.5 ML2 (2)). T-M17 compares stubs by it.
// std::nullopt when a token is of another kind or does not decode.
static std::optional<std::string> getManagedCallStubText(const ObjFile &f,
                                                         const InputMethod &m) {
  if (!m.signature)
    return std::nullopt;
  bool named = true;
  auto printRef = [&](raw_ostream &os, uint32_t token) {
    if (std::optional<std::string> name = getTypeRefName(f, token))
      os << *name;
    else
      named = false;
  };
  std::string text;
  raw_string_ostream os(text);
  printMethodSig(os, *m.signature, printRef);
  ArrayRef<uint8_t> code = m.body.Code;
  size_t pos = 0;
  for (const TokenSite &s : m.sites) {
    if (s.offset < pos || s.offset + 4 > code.size())
      return std::nullopt;
    os << " | " << toHex(code.slice(pos, s.offset - pos)) << " | ";
    pos = s.offset + 4;
    switch (static_cast<TableId>(getTokenType(s.token))) {
    case TableId::TypeRef:
      printRef(os, s.token);
      break;
    case TableId::TypeSpec: {
      std::optional<TypeSig> type = getTypeSpec(f, s.token);
      if (!type)
        return std::nullopt;
      printTypeSig(os, *type, printRef);
      break;
    }
    case TableId::MemberRef: {
      std::optional<MemberAccess> a = decodeMemberRef(f, s.op, s.token);
      if (!a)
        return std::nullopt;
      os << (a->valueTypeParent ? "valuetype " : "") << a->type
         << "::" << a->member << ' ';
      if (a->field)
        printTypeSig(os, *a->field, printRef);
      else
        printMethodSig(os, *a->method, printRef);
      break;
    }
    default:
      return std::nullopt;
    }
  }
  os << " | " << toHex(code.drop_front(pos));
  for (const ExceptionClause &cl : m.body.Clauses)
    os << " | " << static_cast<unsigned>(cl.Kind) << ' ' << cl.TryOffset << ' '
       << cl.TryLength << ' ' << cl.HandlerOffset << ' ' << cl.HandlerLength
       << ' ' << cl.ClassTokenOrFilterOffset;
  if (!named)
    return std::nullopt;
  return text;
}

//===----------------------------------------------------------------------===//
// The units that declare a managed call: T-M23, T-M17 in links with CIL
// objects, T-M21
//===----------------------------------------------------------------------===//

// T-M23's two forms: a stub name '__msil_mc.<...>', or a forwarder name
// '__msil_mc_ctor.<s>' or '__msil_mc_dtor.<s>' (\p forwarder).
static void reportReservedName(const InputFile *file, StringRef name,
                               bool forwarder) {
  error(toString(file) + ": defines '" + name +
        "', which is reserved for the MSIL toolchain (" +
        (forwarder ? "a managed-call forwarder" : "a managed-call stub") + ")");
}

// The symbol <s> of the forwarder name \p name ('__msil_mc_ctor.<s>' or
// '__msil_mc_dtor.<s>'), and whether it is a constructor's.
static std::pair<StringRef, bool> splitForwarderName(StringRef name) {
  bool ctor = name.consume_front(ManagedCtorForwarderPrefix);
  if (!ctor)
    name.consume_front(ManagedDtorForwarderPrefix);
  return {name, ctor};
}

// Whether the SYMTAB symbol \p index of the CIL object \p f, a non-local
// definition with a forwarder name, is the forwarder that clang defines for
// a constructor or destructor managed call <s>: a WEAK, HIDDEN FUNCTION
// definition (not an alias) in the COMDAT group of its own name, with an
// INIT entry (constructor) or a FINI entry (destructor), in an object that
// declares <s> (its LOCAL stub '__msil_mc.<s>').
static bool isManagedCallForwarder(const ObjFile &f, uint32_t index) {
  const MSILLink::Record &record = f.getRecord();
  const MSILLink::Symbol &s = record.Symbols[index];
  auto visibility = static_cast<MSILLink::Visibility>(
      (s.Flags & MSILLink::VisibilityMask) >> MSILLink::VisibilityShift);
  if (s.Kind != MSILLink::SymbolKind::Function ||
      (s.Flags & (MSILLink::Undefined | MSILLink::Alias)) ||
      (s.Flags & MSILLink::BindingMask) != MSILLink::BindingWeak ||
      visibility != MSILLink::Visibility::Hidden)
    return false;
  std::pair<MSILLink::ComdatMemberKind, uint32_t> member(
      MSILLink::ComdatMemberKind::FunctionSymbol, index);
  if (none_of(record.Comdats, [&](const MSILLink::ComdatGroup &g) {
        return g.Name == s.Name && is_contained(g.Members, member);
      }))
    return false;
  auto [symbol, ctor] = splitForwarderName(s.Name);
  MSILLink::InitKind kind =
      ctor ? MSILLink::InitKind::Init : MSILLink::InitKind::Fini;
  if (none_of(record.Inits, [&](const MSILLink::InitEntry &e) {
        return e.Kind == kind && e.Symbol == index;
      }))
    return false;
  int32_t stub = f.findSymbolIndex((ManagedCallPrefix + symbol).str());
  if (stub < 0)
    return false;
  const MSILLink::Symbol &st = record.Symbols[stub];
  return st.Kind == MSILLink::SymbolKind::Function &&
         !(st.Flags & (MSILLink::Undefined | MSILLink::Alias)) &&
         (st.Flags & MSILLink::BindingMask) == MSILLink::BindingLocal;
}

void lld::msil::checkReservedManagedCallNames(const ObjFile &f) {
  if (f.isLTO)
    return;
  for (auto [i, sym] : enumerate(f.getRecord().Symbols)) {
    if ((sym.Flags & MSILLink::Undefined) ||
        (sym.Flags & MSILLink::BindingMask) == MSILLink::BindingLocal)
      continue;
    if (isManagedCallName(sym.Name))
      reportReservedName(&f, sym.Name, /*forwarder=*/false);
    else if (isManagedForwarderName(sym.Name) && !isManagedCallForwarder(f, i))
      reportReservedName(&f, sym.Name, /*forwarder=*/true);
  }
}

void lld::msil::checkReservedManagedCallNames(const BitcodeFile &f) {
  ArrayRef<std::pair<StringRef, Comdat::SelectionKind>> comdats =
      f.obj->getComdatTable();
  // The managed calls that \p f declares (an undefined '__msil_mc.<s>'), read
  // at the first definition with a forwarder name.
  std::optional<StringSet<>> declared;
  for (const lto::InputFile::Symbol &irSym : f.obj->symbols()) {
    StringRef name = irSym.getName();
    if (irSym.isUndefined())
      continue;
    if (isManagedCallName(name)) {
      reportReservedName(&f, name, /*forwarder=*/false);
      continue;
    }
    if (!isManagedForwarderName(name))
      continue;
    if (!declared) {
      declared.emplace();
      for (const lto::InputFile::Symbol &s : f.obj->symbols())
        if (s.isUndefined() && isManagedCallName(s.getName()))
          declared->insert(s.getName());
    }
    // clang's forwarder: a WEAK, HIDDEN function (not an alias) in the COMDAT
    // group of its own name, in an input that declares <s>.
    int c = irSym.getComdatIndex();
    bool forwarder =
        irSym.isExecutable() && !irSym.isIndirect() && irSym.isWeak() &&
        irSym.getVisibility() == GlobalValue::HiddenVisibility && c >= 0 &&
        static_cast<size_t>(c) < comdats.size() && comdats[c].first == name &&
        declared->contains(
            (ManagedCallPrefix + splitForwarderName(name).first).str());
    if (!forwarder)
      reportReservedName(&f, name, /*forwarder=*/true);
  }
}

namespace {
// What one unit shows of an external managed call <s> that it declares: the
// forwarders that it defines for <s> and, in a CIL object, its stub.
struct ManagedCallForwarders {
  // A weak definition of <s> in the COMDAT group <s>: the weak_odr dllexport
  // forwarder of the Windows MSIL triples.
  bool exported = false;
  // The priorities of the INIT_FINI entries of '__msil_mc_ctor.<s>' and
  // '__msil_mc_dtor.<s>' (a CIL object only).
  std::optional<uint32_t> constructor, destructor;
  const InputMethod *stub = nullptr;

  // The attributes as the 8th !msil.managed.calls operand spells them
  // (cli::ManagedCallsMetadata), the text that clang writes: 'dllexport',
  // 'constructor(<prio>)' and 'destructor(<prio>)', joined with ','; empty
  // for none.
  std::string getAttributes() const {
    return printManagedCallAttributes(exported, constructor, destructor);
  }
};
} // namespace

// The symbol <s> of the IR or symbol name \p name of an external managed
// call, '__msil_mc.<s>' without the internal-linkage postfix (5.20.4 MR2);
// std::nullopt for any other name.
static std::optional<StringRef> getExternalManagedCall(StringRef name) {
  if (!isManagedCallName(name))
    return std::nullopt;
  StringRef s = getManagedCallSymbol(name);
  if (name.size() != ManagedCallPrefix.size() + s.size())
    return std::nullopt;
  return s;
}

// The external managed calls that the CIL object \p f declares (a LOCAL
// FUNCTION symbol '__msil_mc.<s>', its stub), with the forwarders that \p f
// defines for them.
static MapVector<StringRef, ManagedCallForwarders>
getManagedCalls(const ObjFile &f) {
  MapVector<StringRef, ManagedCallForwarders> calls;
  const MSILLink::Record &record = f.getRecord();
  auto isDefinition = [](const MSILLink::Symbol &s, uint32_t binding) {
    return s.Kind == MSILLink::SymbolKind::Function &&
           !(s.Flags & (MSILLink::Undefined | MSILLink::Alias)) &&
           (s.Flags & MSILLink::BindingMask) == binding;
  };
  for (const MSILLink::Symbol &s : record.Symbols)
    if (isDefinition(s, MSILLink::BindingLocal))
      if (std::optional<StringRef> name = getExternalManagedCall(s.Name))
        calls[*name].stub = f.getMethod(s.Token);
  if (calls.empty())
    return calls;

  for (const MSILLink::ComdatGroup &group : record.Comdats) {
    for (auto [kind, index] : group.Members) {
      if (kind != MSILLink::ComdatMemberKind::FunctionSymbol ||
          index >= record.Symbols.size())
        continue;
      const MSILLink::Symbol &s = record.Symbols[index];
      auto it = calls.find(s.Name);
      if (it != calls.end() && s.Name == group.Name &&
          isDefinition(s, MSILLink::BindingWeak))
        it->second.exported = true;
    }
  }
  for (const MSILLink::InitEntry &e : record.Inits) {
    if (e.Symbol >= record.Symbols.size())
      continue;
    const MSILLink::Symbol &s = record.Symbols[e.Symbol];
    bool init = e.Kind == MSILLink::InitKind::Init;
    StringRef name = s.Name;
    if ((s.Flags & MSILLink::BindingMask) == MSILLink::BindingLocal ||
        !name.consume_front(init ? ManagedCtorForwarderPrefix
                                 : ManagedDtorForwarderPrefix))
      continue;
    auto it = calls.find(name);
    if (it != calls.end())
      (init ? it->second.constructor : it->second.destructor) = e.Priority;
  }
  return calls;
}

// The external managed calls that the bitcode file \p f declares (an
// undefined '__msil_mc.<s>'), with its dllexport forwarders. Its other
// attributes are in its modules (BitcodeInputs). After LTO, which owns the
// file's symbol table then, it declares none.
static MapVector<StringRef, ManagedCallForwarders>
getManagedCalls(const BitcodeFile &f) {
  MapVector<StringRef, ManagedCallForwarders> calls;
  if (!f.obj)
    return calls;
  for (const lto::InputFile::Symbol &s : f.obj->symbols())
    if (s.isUndefined())
      if (std::optional<StringRef> name = getExternalManagedCall(s.getName()))
        calls[*name];
  if (calls.empty())
    return calls;

  ArrayRef<std::pair<StringRef, Comdat::SelectionKind>> comdats =
      f.obj->getComdatTable();
  for (const lto::InputFile::Symbol &s : f.obj->symbols()) {
    int c = s.getComdatIndex();
    if (s.isUndefined() || !s.isWeak() || c < 0 ||
        static_cast<size_t>(c) >= comdats.size() ||
        comdats[c].first != s.getName())
      continue;
    auto it = calls.find(s.getName());
    if (it != calls.end())
      it->second.exported = true;
  }
  return calls;
}

// Calls \p fn with the name of each non-local definition, GLOBAL or WEAK, of
// the CIL object or bitcode input \p file: a function or data definition, an
// alias included. irsymtab lists no local symbol, and after LTO, which owns
// a bitcode input's symbol table then, a bitcode input lists none.
static void forEachNonLocalDefinition(const InputFile &file,
                                      function_ref<void(StringRef)> fn) {
  if (const auto *f = dyn_cast<ObjFile>(&file)) {
    for (const MSILLink::Symbol &s : f->getRecord().Symbols) {
      uint32_t binding = s.Flags & MSILLink::BindingMask;
      if (!(s.Flags & MSILLink::Undefined) &&
          (binding == MSILLink::BindingGlobal ||
           binding == MSILLink::BindingWeak))
        fn(s.Name);
    }
  } else if (const auto *f = dyn_cast<BitcodeFile>(&file)) {
    if (f->obj)
      for (const lto::InputFile::Symbol &s : f->obj->symbols())
        if (!s.isUndefined() && !s.isCommon())
          fn(s.getName());
  }
}

void lld::msil::checkExportedManagedCalls() {
  // The inputs in load order, then the LTO objects.
  SmallVector<InputFile *, 0> files(ctx.loadedFiles.begin(),
                                    ctx.loadedFiles.end());
  for (ObjFile *f : ctx.objectFiles)
    if (f->isLTO)
      files.push_back(f);

  // Each exported managed call <s>: the first input in load order that has
  // its forwarder, and every input that has it.
  struct Export {
    InputFile *first = nullptr;
    SmallPtrSet<const InputFile *, 2> forwarders;
  };
  MapVector<StringRef, Export> exports;
  for (InputFile *file : files) {
    MapVector<StringRef, ManagedCallForwarders> calls;
    if (auto *f = dyn_cast<ObjFile>(file))
      calls = getManagedCalls(*f);
    else if (auto *f = dyn_cast<BitcodeFile>(file))
      calls = getManagedCalls(*f);
    for (auto &[name, forwarders] : calls) {
      if (!forwarders.exported)
        continue;
      Export &e = exports[name];
      if (!e.first)
        e.first = file;
      e.forwarders.insert(file);
    }
  }
  if (exports.empty())
    return;

  // The first input in load order that defines <s> otherwise, whichever
  // definition resolution keeps.
  StringMap<InputFile *> others;
  for (InputFile *file : files)
    forEachNonLocalDefinition(*file, [&](StringRef name) {
      auto it = exports.find(name);
      if (it != exports.end() && !it->second.forwarders.contains(file))
        others.try_emplace(name, file);
    });
  for (auto &[name, e] : exports)
    if (InputFile *other = others.lookup(name))
      error("managed call '" + name + "' is exported from " +
            toString(e.first) + ", but " + toString(other) + " also defines '" +
            name + "'");
}

namespace {
// The bitcode inputs' modules, each input's read lazily and once: their
// global values and module-level metadata, no function body. They share one
// context, so that the declarations of different inputs compare as they
// would in a merged module (checkBitcodeManagedCalls): equal metadata is one
// node, equal attribute lists are one list and equal types are one type, but
// for the named structs that reading renames (haveMergedEqualTypes). The
// context also compiles the reference stubs (getReferenceStubText), each
// distinct declaration once, and prints no diagnostic: reading errors come
// back as Errors, and code generation's errors are LTO's to report, as it
// compiles the same declarations.
struct BitcodeInputs {
  struct Handler : DiagnosticHandler {
    bool *failed;
    explicit Handler(bool *failed) : failed(failed) {}
    bool handleDiagnostics(const DiagnosticInfo &di) override {
      if (di.getSeverity() == DS_Error)
        *failed = true;
      return true;
    }
  };

  LLVMContext context;
  // Set by an error diagnostic in context.
  bool failed = false;
  // The inputs whose modules do not read.
  SmallPtrSetImpl<const BitcodeFile *> &unreadable;
  // The modules of each input read so far.
  DenseMap<const BitcodeFile *, std::vector<std::unique_ptr<Module>>> modules;
  // The reference stub texts made so far, by the text of the module that
  // getReferenceStubText compiles (which holds only the declaration).
  StringMap<std::optional<std::string>> stubTexts;

  explicit BitcodeInputs(SmallPtrSetImpl<const BitcodeFile *> &unreadable)
      : unreadable(unreadable) {
    context.setDiagnosticHandler(std::make_unique<Handler>(&failed));
  }

  // The modules of the bitcode input \p f; std::nullopt after the error
  // "<file>: <reason>" (section 12 row '5.20.7 MK6 amended'), which adds \p f
  // to the unreadable inputs, and for an input already among them: a unit
  // that does not read takes no part in T-M17, and its error is reported
  // once.
  std::optional<ArrayRef<std::unique_ptr<Module>>>
  getModules(const BitcodeFile &f);
};
} // namespace

std::optional<ArrayRef<std::unique_ptr<Module>>>
BitcodeInputs::getModules(const BitcodeFile &f) {
  if (unreadable.contains(&f))
    return std::nullopt;
  if (auto it = modules.find(&f); it != modules.end())
    return ArrayRef(it->second);
  std::vector<std::unique_ptr<Module>> list;
  Error e = [&]() -> Error {
    Expected<std::vector<BitcodeModule>> bms =
        getBitcodeModuleList(f.getMemoryBufferRef());
    if (!bms)
      return bms.takeError();
    for (BitcodeModule &bm : *bms) {
      Expected<std::unique_ptr<Module>> m =
          bm.getLazyModule(context, /*ShouldLazyLoadMetadata=*/true,
                           /*IsImporting=*/false);
      if (!m)
        return m.takeError();
      if (Error err = (*m)->materializeMetadata())
        return err;
      list.push_back(std::move(*m));
    }
    return Error::success();
  }();
  if (e) {
    error(toString(&f) + ": " + toString(std::move(e)));
    unreadable.insert(&f);
    return std::nullopt;
  }
  return ArrayRef(modules.try_emplace(&f, std::move(list)).first->second);
}

// The attributes operand (the one after the NumManagedCallOperands
// operands) of the first !msil.managed.calls entry of \p irName in \p m;
// empty when it is absent.
static std::string getEntryAttributes(const Module &m, StringRef irName) {
  const NamedMDNode *nmd = m.getNamedMetadata(ManagedCallsMetadata);
  if (!nmd)
    return "";
  for (const MDNode *n : nmd->operands()) {
    const auto *name = n->getNumOperands()
                           ? dyn_cast_or_null<MDString>(n->getOperand(0))
                           : nullptr;
    if (!name || name->getString() != irName)
      continue;
    const auto *attrs =
        n->getNumOperands() > NumManagedCallOperands
            ? dyn_cast_or_null<MDString>(n->getOperand(NumManagedCallOperands))
            : nullptr;
    return attrs ? attrs->getString().str() : "";
  }
  return "";
}

// The text (getManagedCallStubText) of the stub that the backend makes of the
// managed-call declaration \p fn of the bitcode input \p f when it compiles
// the unit to a CIL object (clang -c): \p fn alone, with its function type
// and attributes, in a module of the codegen triple (the link's, else the
// module's, as LTO compiles) and of \p fn's data layout, compiled in
// \p inputs' context, once per distinct such module (BitcodeInputs's cache:
// the inputs that include one header compile its declaration once).
// std::nullopt when code generation reports an error (slot P0 refuses the
// declaration, and LTO reports it), makes no stub, or makes one without a
// text.
static std::optional<std::string> getReferenceStubText(const BitcodeFile &f,
                                                       const Function &fn,
                                                       BitcodeInputs &inputs) {
  const Module &src = *fn.getParent();
  Triple tt =
      ctx.arg.target.str().empty() ? src.getTargetTriple() : ctx.arg.target;
  // Named after the declaration, not the input: the module's name is only
  // the object's Module row, never part of a stub's text.
  Module m(fn.getName(), inputs.context);
  m.setTargetTriple(tt);
  m.setDataLayout(src.getDataLayout());
  Function *decl =
      Function::Create(fn.getFunctionType(), GlobalValue::ExternalLinkage,
                       fn.getAddressSpace(), fn.getName(), &m);
  decl->copyAttributesFrom(&fn);

  // The module as text (triple, data layout, the declaration and its
  // attribute group) is all that code generation reads. Every return below
  // that gives std::nullopt leaves it as the cached result.
  std::string key;
  raw_string_ostream keyOS(key);
  m.print(keyOS, /*AAW=*/nullptr);
  auto [cached, inserted] = inputs.stubTexts.try_emplace(key);
  if (!inserted)
    return cached->second;
  std::optional<std::string> &result = cached->second;

  std::string err;
  const llvm::Target *target = TargetRegistry::lookupTarget(tt, err);
  if (!target)
    return std::nullopt;
  std::unique_ptr<TargetMachine> tm(target->createTargetMachine(
      tt, getCPUStr(), join(getMAttrs(), ","),
      initTargetOptionsFromCodeGenFlags(),
      getRelocModelFromCMModel().value_or(Reloc::Static), CodeModel::Small));
  if (!tm)
    return std::nullopt;

  legacy::PassManager pm;
  pm.add(new TargetLibraryInfoWrapperPass(TargetLibraryInfoImpl(tt)));
  SmallString<0> buf;
  raw_svector_ostream os(buf);
  if (tm->addPassesToEmitFile(pm, os, nullptr, CodeGenFileType::ObjectFile))
    return std::nullopt;
  inputs.failed = false;
  pm.run(m);
  if (inputs.failed)
    return std::nullopt;

  // The object, read like an input; an object that does not read is a
  // toolchain error, reported under this name. It has only LOCAL symbols, so
  // parse() adds nothing to the symbol table.
  StringRef name = saver().save("<'" + fn.getName() + "' of " + toString(&f) +
                                ", compiled alone>");
  auto *obj = make<ObjFile>(MemoryBufferRef(saver().save(buf.str()), name),
                            /*archiveName=*/"", /*lazy=*/false);
  if (!obj->isValid() ||
      any_of(obj->getRecord().Symbols, [](const MSILLink::Symbol &s) {
        return (s.Flags & MSILLink::BindingMask) != MSILLink::BindingLocal;
      }))
    return std::nullopt;
  size_t errorsBefore = errorCount();
  obj->parse(/*ignoreComdats=*/true);
  int32_t index = obj->findSymbolIndex(fn.getName());
  if (errorCount() != errorsBefore || index < 0)
    return std::nullopt;
  const MSILLink::Symbol &s = obj->getRecord().Symbols[index];
  const InputMethod *stub =
      s.Kind == MSILLink::SymbolKind::Function &&
              !(s.Flags & (MSILLink::Undefined | MSILLink::Alias))
          ? obj->getMethod(s.Token)
          : nullptr;
  if (!stub)
    return std::nullopt;
  result = getManagedCallStubText(*obj, *stub);
  return result;
}

// T-M17's two forms for the managed call \p name (plan 5.20.4 MR4 as
// amended): the second, \p onlyAttributes, for units that differ only in
// their 'dllexport', 'constructor' and 'destructor' attributes.
static void reportManagedCallDisagreement(StringRef name, bool onlyAttributes) {
  if (onlyAttributes)
    error("managed call '" + name +
          "' is declared with different 'dllexport', 'constructor' or "
          "'destructor' attributes in different translation units");
  else
    error("managed call '" + name +
          "' is declared with different 'msil_method' or 'msil_catch' "
          "arguments in different translation units");
}

namespace {
// A unit that declares an external managed call, with its forwarders: a CIL
// object or a bitcode input.
struct ManagedCallUnit {
  InputFile *file;
  ManagedCallForwarders forwarders;
};
} // namespace

// The units that declare each external managed call, in load order; the LTO
// objects are not loaded yet.
static MapVector<StringRef, SmallVector<ManagedCallUnit, 2>>
getManagedCallUnits() {
  MapVector<StringRef, SmallVector<ManagedCallUnit, 2>> units;
  for (InputFile *file : ctx.loadedFiles) {
    if (auto *f = dyn_cast<ObjFile>(file)) {
      for (auto &[name, forwarders] : getManagedCalls(*f))
        units[name].push_back({f, forwarders});
    } else if (auto *f = dyn_cast<BitcodeFile>(file)) {
      for (auto &[name, forwarders] : getManagedCalls(*f))
        units[name].push_back({f, forwarders});
    }
  }
  return units;
}

static bool isBitcodeUnit(const ManagedCallUnit &u) {
  return isa<BitcodeFile>(u.file);
}

// The declaration of the managed call whose IR name is \p irName in \p m, or
// null: an undefined '__msil_mc.<s>' without "msil-managed-type" is none (the
// link reports it as undefined).
static const Function *getManagedCallDeclaration(const Module &m,
                                                 StringRef irName) {
  const Function *fn = m.getFunction(irName);
  return fn && fn->isDeclaration() && fn->hasFnAttribute(ManagedTypeAttr)
             ? fn
             : nullptr;
}

// T-M17 for the external managed call \p name between the units \p list that
// declare it, CIL objects and bitcode inputs (ManagedCalls.h,
// checkObjectManagedCalls): first by their stubs, then by their attributes.
// \p bitcode reads the bitcode inputs (null when \p list has none). Returns
// true if it reported T-M17.
static bool compareManagedCallUnits(StringRef name,
                                    ArrayRef<ManagedCallUnit> list,
                                    BitcodeInputs *bitcode) {
  // Each unit's attributes, and a bitcode input's declaration.
  struct Declaration {
    const ObjFile *obj = nullptr;
    const InputMethod *stub = nullptr;
    const BitcodeFile *bc = nullptr;
    const Function *fn = nullptr;
    std::string attributes;
  };
  SmallVector<Declaration, 2> decls;
  std::string irName = (ManagedCallPrefix + name).str();
  for (const ManagedCallUnit &u : list) {
    if (auto *f = dyn_cast<ObjFile>(u.file)) {
      decls.push_back({f, u.forwarders.stub, nullptr, nullptr,
                       u.forwarders.getAttributes()});
      continue;
    }
    auto *bc = cast<BitcodeFile>(u.file);
    std::optional<ArrayRef<std::unique_ptr<Module>>> modules =
        bitcode->getModules(*bc);
    if (!modules)
      continue;
    // The first module that declares it as a managed call.
    for (const std::unique_ptr<Module> &m : *modules) {
      if (const Function *fn = getManagedCallDeclaration(*m, irName)) {
        decls.push_back(
            {nullptr, nullptr, bc, fn, getEntryAttributes(*m, irName)});
        break;
      }
    }
  }
  if (decls.size() < 2)
    return false;

  // The CIL objects' stubs, by the members and conversions they name, then
  // the bitcode declarations' (code generation, only if those agree).
  bool known = true;
  SmallVector<std::string, 2> texts;
  for (bool objects : {true, false}) {
    for (const Declaration &d : decls) {
      if (!known || (d.obj != nullptr) != objects)
        continue;
      std::optional<std::string> text =
          d.obj ? (d.stub ? getManagedCallStubText(*d.obj, *d.stub)
                          : std::nullopt)
                : getReferenceStubText(*d.bc, *d.fn, *bitcode);
      if (text)
        texts.push_back(std::move(*text));
      else
        known = false;
    }
    if (known && !all_equal(texts))
      break;
  }
  bool otherStub = known && !all_equal(texts);
  bool otherAttributes = any_of(drop_begin(decls), [&](const Declaration &d) {
    return d.attributes != decls.front().attributes;
  });
  if (!otherStub && !otherAttributes)
    return false;
  reportManagedCallDisagreement(name, /*onlyAttributes=*/!otherStub);
  return true;
}

// T-M17 in links with CIL objects (ManagedCalls.h) for the names that bitcode
// inputs declare too (\p unreadable, which receives the bitcode inputs whose
// modules do not read, and \p reported, which receives the names that get
// T-M17) or, with null, for the others. Returns false if it reported an
// error.
static bool
checkManagedCallUnits(SmallPtrSetImpl<const BitcodeFile *> *unreadable,
                      StringSet<> *reported) {
  bool withBitcode = unreadable != nullptr;
  size_t errorsBefore = errorCount();
  std::optional<BitcodeInputs> bitcode;
  for (auto &[name, list] : getManagedCallUnits()) {
    if (list.size() < 2 || any_of(list, isBitcodeUnit) != withBitcode ||
        all_of(list, isBitcodeUnit))
      continue;
    if (withBitcode && !bitcode)
      bitcode.emplace(*unreadable);
    if (compareManagedCallUnits(name, list, bitcode ? &*bitcode : nullptr) &&
        reported)
      reported->insert(name);
  }
  return errorCount() == errorsBefore;
}

void lld::msil::checkObjectManagedCalls() {
  checkManagedCallUnits(/*unreadable=*/nullptr, /*reported=*/nullptr);
  checkExportedManagedCalls();
}

bool lld::msil::checkMixedManagedCalls(
    SmallPtrSetImpl<const BitcodeFile *> &unreadable, StringSet<> &reported) {
  return checkManagedCallUnits(&unreadable, &reported);
}

ManagedCallUnitRecord lld::msil::recordManagedCallUnits() {
  ManagedCallUnitRecord record;
  record.numLoadedFiles = ctx.loadedFiles.size();
  // A bitcode input's names are its symbol table's, which LTO takes.
  for (auto &[name, list] : getManagedCallUnits())
    record.units[saver().save(name)] = to_vector<2>(
        map_range(list, [](const ManagedCallUnit &u) { return u.file; }));
  return record;
}

void lld::msil::checkLateManagedCalls(const ManagedCallUnitRecord &record) {
  // The external managed calls that the CIL objects loaded after LTO declare
  // (archive members that the LTO objects extracted), with those objects in
  // load order. The LTO objects are not loaded files: their bitcode inputs
  // stand for them.
  MapVector<StringRef, SmallVector<ManagedCallUnit, 2>> late;
  for (InputFile *file :
       ArrayRef(ctx.loadedFiles).drop_front(record.numLoadedFiles))
    if (auto *f = dyn_cast<ObjFile>(file); f && !f->isLTO)
      for (auto &[name, forwarders] : getManagedCalls(*f))
        late[name].push_back({f, forwarders});
  if (late.empty())
    return;

  // Each name with the units that declared it before LTO, in load order,
  // then the late objects. A CIL object's record stays readable; a bitcode
  // input's modules are read again from its file.
  DenseMap<const ObjFile *, MapVector<StringRef, ManagedCallForwarders>>
      objectCalls;
  SmallPtrSet<const BitcodeFile *, 1> unreadable;
  std::optional<BitcodeInputs> bitcode;
  for (auto &[name, lateUnits] : late) {
    SmallVector<ManagedCallUnit, 2> list;
    if (auto it = record.units.find(name); it != record.units.end()) {
      for (InputFile *file : it->second) {
        auto *f = dyn_cast<ObjFile>(file);
        if (!f) {
          list.push_back({file, {}});
          continue;
        }
        auto [calls, inserted] = objectCalls.try_emplace(f);
        if (inserted)
          calls->second = getManagedCalls(*f);
        if (auto c = calls->second.find(name); c != calls->second.end())
          list.push_back({f, c->second});
      }
    }
    append_range(list, lateUnits);
    if (list.size() < 2)
      continue;
    if (!bitcode && any_of(list, isBitcodeUnit))
      bitcode.emplace(unreadable);
    compareManagedCallUnits(name, list, bitcode ? &*bitcode : nullptr);
  }
}

// A named struct's name without the ".<n>" suffix that a context gives a
// struct whose name it already has (IRMover.cpp's getTypeNamePrefix).
static StringRef getStructNamePrefix(StringRef name) {
  size_t dot = name.rfind('.');
  if (dot == 0 || dot == StringRef::npos || name.back() == '.' ||
      !isDigit(name[dot + 1]))
    return name;
  return name.substr(0, dot);
}

// Whether the types \p a and \p b of two bitcode inputs read into one context
// (BitcodeInputs) are one type once the IR linker merges the inputs: the same
// type, or types of the same shape whose named structs pair up by their names
// without the ".<n>" suffix that reading gave the later one, with the same
// bodies, an opaque struct pairing with any body (IRMover.cpp's
// computeTypeMapping).
static bool haveMergedEqualTypes(Type *a, Type *b) {
  if (a == b)
    return true;
  if (a->getTypeID() != b->getTypeID())
    return false;
  if (auto *sa = dyn_cast<StructType>(a)) {
    auto *sb = cast<StructType>(b);
    if (sa->isLiteral() != sb->isLiteral())
      return false;
    if (!sa->isLiteral()) {
      if (!sa->hasName() || !sb->hasName() ||
          getStructNamePrefix(sa->getName()) !=
              getStructNamePrefix(sb->getName()))
        return false;
      if (sa->isOpaque() || sb->isOpaque())
        return true;
    }
    if (sa->isPacked() != sb->isPacked())
      return false;
  } else if (auto *fa = dyn_cast<FunctionType>(a)) {
    if (fa->isVarArg() != cast<FunctionType>(b)->isVarArg())
      return false;
  } else if (isa<ArrayType>(a)) {
    if (a->getArrayNumElements() != b->getArrayNumElements())
      return false;
  } else if (auto *va = dyn_cast<VectorType>(a)) {
    if (va->getElementCount() != cast<VectorType>(b)->getElementCount())
      return false;
  } else {
    // Integer, floating-point, pointer and target extension types are
    // unique in a context: two of them are different types.
    return false;
  }
  if (a->getNumContainedTypes() != b->getNumContainedTypes())
    return false;
  for (unsigned i = 0, e = a->getNumContainedTypes(); i != e; ++i)
    if (!haveMergedEqualTypes(a->getContainedType(i), b->getContainedType(i)))
      return false;
  return true;
}

namespace {
// A managed-call declaration in a module of the bitcode input \p file.
struct BitcodeDeclaration {
  const BitcodeFile *file;
  const Function *fn;
};
} // namespace

// Whether code generation makes one stub of the managed-call declarations
// \p a and \p b of one IR name, known without compiling them: they have the
// same function type, attribute list and calling convention, in modules of
// the same triple and data layout (what getReferenceStubText compiles).
// BitcodeInputs's context makes equal types and equal attribute lists one.
static bool makeSameStub(const Function &a, const Function &b) {
  return a.getFunctionType() == b.getFunctionType() &&
         a.getAttributes() == b.getAttributes() &&
         a.getCallingConv() == b.getCallingConv() &&
         a.getAddressSpace() == b.getAddressSpace() &&
         a.getParent()->getTargetTriple() == b.getParent()->getTargetTriple() &&
         a.getDataLayout() == b.getDataLayout();
}

// Whether the stubs that code generation makes of the managed-call
// declarations \p decls of one IR name differ: each declaration compiled
// alone, as clang -c compiles its unit (getReferenceStubText), and compared
// by its text, as a mixed link compares a bitcode input's stub
// (compareManagedCallUnits). False when one of them has no text, as there:
// LTO then decides (slot P0 reports a declaration that code generation
// refuses).
static bool haveDifferentStubs(ArrayRef<BitcodeDeclaration> decls,
                               BitcodeInputs &inputs) {
  if (all_of(drop_begin(decls), [&](const BitcodeDeclaration &d) {
        return makeSameStub(*decls.front().fn, *d.fn);
      }))
    return false;
  SmallVector<std::string, 2> texts;
  for (const BitcodeDeclaration &d : decls) {
    std::optional<std::string> text =
        getReferenceStubText(*d.file, *d.fn, inputs);
    if (!text)
      return false;
    texts.push_back(std::move(*text));
  }
  return !all_equal(texts);
}

bool lld::msil::checkBitcodeManagedCalls(
    SmallPtrSetImpl<const BitcodeFile *> &unreadable, StringSet<> &reported) {
  std::optional<BitcodeInputs> bitcode;
  size_t errorsBefore = errorCount();
  // ASSEMBLY counts only where the output names it: in whole-program text.
  ManagedCallAssemblyRule assemblyRule =
      ctx.arg.ltoEmitAsm ? ManagedCallAssemblyRule::TextScope
                         : ManagedCallAssemblyRule::Ignore;
  for (auto &[name, list] : getManagedCallUnits()) {
    if (none_of(list, isBitcodeUnit) || reported.contains(name))
      continue;
    if (!bitcode)
      bitcode.emplace(unreadable);

    // The declarations in the declaring inputs' modules, and the entries of
    // the name there that differ from one another: one input that llvm-link
    // merged from several units may hold several.
    std::string irName = (ManagedCallPrefix + name).str();
    SmallVector<BitcodeDeclaration, 2> decls;
    SmallVector<const MDNode *, 2> entries;
    for (const ManagedCallUnit &u : list) {
      auto *bc = dyn_cast<BitcodeFile>(u.file);
      if (!bc)
        continue;
      std::optional<ArrayRef<std::unique_ptr<Module>>> modules =
          bitcode->getModules(*bc);
      if (!modules)
        continue;
      size_t numDecls = decls.size();
      for (const std::unique_ptr<Module> &m : *modules)
        if (const Function *fn = getManagedCallDeclaration(*m, irName))
          decls.push_back({bc, fn});
      if (decls.size() == numDecls)
        continue;
      for (const std::unique_ptr<Module> &m : *modules) {
        const NamedMDNode *nmd = m->getNamedMetadata(ManagedCallsMetadata);
        if (!nmd)
          continue;
        for (const MDNode *n : nmd->operands()) {
          const auto *entryName =
              n->getNumOperands() ? dyn_cast_or_null<MDString>(n->getOperand(0))
                                  : nullptr;
          if (entryName && entryName->getString() == irName &&
              none_of(entries, [&](const MDNode *e) {
                return haveEqualManagedCallEntries(e, n, assemblyRule);
              }))
            entries.push_back(n);
        }
      }
    }
    if (decls.empty())
      continue;

    // Slot P0's order: the entries (checkManagedCallsMetadata), then, for a
    // name whose entries agree, the function types
    // (cli::hasManagedCallOfOtherType). Then, for declarations that agree in
    // both, their stubs, as object and mixed links compare them. The IR
    // linker keeps the first declaration, with its parameter and result
    // attributes, and leaves each call its own: a later unit's calls that
    // match its own declaration may then have a 'signext' or 'zeroext' of
    // their own that would change the stub
    // (cli::hasManagedCallOfOtherExtension), calls that
    // checkBitcodeManagedCallUses, which applies that rule to each unit's own
    // module before LTO, does not see, and that ThinLTO, which merges no
    // declarations, never makes. Comparing the stubs refuses units whose
    // stubs differ (a signext and a zeroext narrow result end them in
    // different conversions) in every LTO mode, as object links refuse them.
    if (entries.size() > 1) {
      bool onlyAttributes = all_of(drop_begin(entries), [&](const MDNode *n) {
        return haveEqualManagedCallEntries(entries.front(), n, assemblyRule,
                                           NumManagedCallOperands);
      });
      reportManagedCallDisagreement(name, onlyAttributes);
      reported.insert(name);
    } else if (any_of(drop_begin(decls),
                      [&](const BitcodeDeclaration &d) {
                        return !haveMergedEqualTypes(
                            decls.front().fn->getFunctionType(),
                            d.fn->getFunctionType());
                      }) ||
               haveDifferentStubs(decls, *bitcode)) {
      reportManagedCallDisagreement(name, /*onlyAttributes=*/false);
      reported.insert(name);
    }
  }
  return errorCount() == errorsBefore;
}

//===----------------------------------------------------------------------===//
// T-M17 and T-M10 before LTO: the uses in the bitcode inputs' modules
//===----------------------------------------------------------------------===//

namespace {
// Consumes the diagnostics of reading a bitcode input for its uses (a debug
// info version warning, for example): LTO reads the input again and reports
// them, and a reading error comes back as an Error.
struct QuietDiagnosticHandler : DiagnosticHandler {
  bool handleDiagnostics(const DiagnosticInfo &) override { return true; }
};
} // namespace

// Whether the symbol table of the bitcode input \p f lists an undefined
// symbol whose IR name starts with '__msil_mc.', the name that clang gives
// every managed-call declaration, external or internal (5.20.4 MR2).
static bool listsManagedCall(const BitcodeFile &f) {
  return f.obj && any_of(f.obj->symbols(), [](const lto::InputFile::Symbol &s) {
           return s.isUndefined() && isManagedCallName(s.getIRName());
         });
}

// Whether \p fn is a managed-call declaration, which slot P0 makes a stub.
static bool isManagedCallDeclaration(const Function &fn) {
  return fn.isDeclaration() && fn.hasFnAttribute(ManagedTypeAttr);
}

// Drops what no output compiles from \p m, a module of a bitcode input: the
// bodies of its available_externally functions and the initializers of its
// available_externally variables, as EliminateAvailableExternallyPass drops
// them before slot P0 in clang -c at -O1 and above. Code generation never
// compiles such a function (MachineFunctionPass skips it), and LTO links
// another unit's definition in its place (the IR linker) or drops the copy
// (DropImportedManagedCallHoldersPass in a ThinLTO module). So neither a
// call nor another use of a managed call there counts.
static void dropAvailableExternallyCode(Module &m) {
  for (GlobalVariable &gv : m.globals()) {
    if (!gv.hasAvailableExternallyLinkage())
      continue;
    gv.setInitializer(nullptr);
    gv.setLinkage(GlobalValue::ExternalLinkage);
  }
  for (Function &fn : m.functions())
    if (!fn.isDeclaration() && fn.hasAvailableExternallyLinkage())
      fn.deleteBody();
}

// T-M17 and T-M10 for the managed-call declarations of \p m, a module of the
// bitcode input \p f without what no output compiles
// (checkBitcodeManagedCallUses), in the module's order. A declaration that
// its unit also calls with another function type or extension gets T-M17's
// first form, once per name per link (\p disagreeing; never for a name in
// \p reported), and no T-M10; any other gets one T-M10 line per holder, the
// functions first, each distinct line once per link (\p printed).
static void reportManagedCallUses(const BitcodeFile &f, const Module &m,
                                  const StringSet<> &reported,
                                  StringSet<> &disagreeing,
                                  StringSet<> &printed) {
  auto report = [&](const std::string &holder, const std::string &text) {
    std::string line = toString(&f) + ": " + holder + ": " + text;
    if (printed.insert(line).second)
      error(line);
  };
  for (const Function &fn : m.functions()) {
    if (!isManagedCallDeclaration(fn))
      continue;
    // T-M17 before LTO: no T-M10, as slot P0 gives none after T-M17.
    if (std::optional<StringRef> name = getExternalManagedCall(fn.getName());
        name && reported.contains(*name))
      continue;
    // Slot P0's T-M17 for a call of another function type or with an
    // extension of its own, as clang -c reports it for the unit, and no
    // T-M10.
    StringRef symbol = getManagedCallSymbol(fn.getName());
    if (hasManagedCallOfOtherType(fn) || hasManagedCallOfOtherExtension(fn)) {
      if (!reported.contains(symbol) && disagreeing.insert(symbol).second)
        reportManagedCallDisagreement(symbol, /*onlyAttributes=*/false);
      continue;
    }
    SmallSetVector<const Instruction *, 4> insts;
    SmallSetVector<const GlobalValue *, 4> globals;
    collectManagedCallHolders(fn, insts, globals);
    if (insts.empty() && globals.empty())
      continue;
    std::string text = getManagedCallAddressText(symbol);
    SmallPtrSet<const Function *, 4> holders;
    for (const Instruction *inst : insts)
      if (holders.insert(inst->getFunction()).second)
        report(getManagedCallFunctionHolderName(*inst->getFunction()), text);
    for (const GlobalValue *gv : globals)
      report(getManagedCallGlobalHolderName(*gv), text);
  }
}

bool lld::msil::checkBitcodeManagedCallUses(
    SmallPtrSetImpl<const BitcodeFile *> &unreadable,
    const StringSet<> &reported) {
  size_t errorsBefore = errorCount();
  StringSet<> disagreeing, printed;
  for (BitcodeFile *f : ctx.bitcodeFiles) {
    if (unreadable.contains(f) || !listsManagedCall(*f))
      continue;
    // Each input in a context of its own, freed after it. The modules are
    // read lazily; one that declares a managed call is read whole, since its
    // function bodies hold the uses.
    LLVMContext context;
    context.setDiagnosticHandler(std::make_unique<QuietDiagnosticHandler>());
    Error e = [&]() -> Error {
      Expected<std::vector<BitcodeModule>> bms =
          getBitcodeModuleList(f->getMemoryBufferRef());
      if (!bms)
        return bms.takeError();
      for (BitcodeModule &bm : *bms) {
        Expected<std::unique_ptr<Module>> m =
            bm.getLazyModule(context, /*ShouldLazyLoadMetadata=*/true,
                             /*IsImporting=*/false);
        if (!m)
          return m.takeError();
        if (none_of((*m)->functions(), isManagedCallDeclaration))
          continue;
        if (Error err = (*m)->materializeAll())
          return err;
        dropAvailableExternallyCode(**m);
        reportManagedCallUses(*f, **m, reported, disagreeing, printed);
      }
      return Error::success();
    }();
    if (e) {
      error(toString(f) + ": " + toString(std::move(e)));
      unreadable.insert(f);
    }
  }
  return errorCount() == errorsBefore;
}

//===----------------------------------------------------------------------===//
// --lto-emit-asm: the merged module's declarations
//===----------------------------------------------------------------------===//

// The checks below run in the PreCodeGenModuleHook (LTO.cpp), whose errors
// stop code generation. So they skip every declaration that slot P0
// (MSILManagedCalls.cpp createManagedCallStubs) refuses: code generation then
// runs and P0 reports it with the text that the object and image links give
// for the same inputs. P0 refuses T-M17 for a direct call whose function type
// is not its declaration's, or that has a 'signext' or 'zeroext' of its own
// that would give the declaration another stub, T-M10 and a declaration whose
// attributes do not describe a valid managed call; the checks apply its rules
// from llvm/CLI/ManagedCallIR.h, which P0 uses too. P0 sees no
// !msil.managed.calls entry here: msil-ld decided T-M17 before LTO, which
// runs only when that reported nothing, and MSILLinkHooksPass removes the
// list. msil-ld also decided P0's T-M17 for a call of another function type
// or extension and T-M10 for the bitcode inputs' uses before LTO
// (checkBitcodeManagedCallUses), so P0's T-M17 or T-M10 here is for a call
// or a use that LTO's optimizer made, or for a declaration in an input whose
// symbol table lists no '__msil_mc.' name (hand-written IR).

using TK = ManagedType::TypeKind;

// Whether P0 accepts the managed-call declaration \p fn, whose parsed
// attributes are \p type, \p member, \p sig, \p conv and \p numCatches
// msil_catch entries (MSIL::parseManagedCall, plan 5.20.4 MR1); P0 reports
// any other as "managed call '<f>' has invalid "msil-managed-*" attributes:
// <reason>".
static bool isValidManagedCall(const Function &fn, const ManagedType &type,
                               StringRef member, const ManagedSignature &sig,
                               const ManagedConvList &conv, size_t numCatches) {
  if (Error e = checkManagedMemberName(member)) {
    consumeError(std::move(e));
    return false;
  }
  if (checkManagedCallShape(type, member, sig))
    return false;
  if (fn.hasFnAttribute(ManagedAsmAttr) &&
      fn.getFnAttribute(ManagedAsmAttr).getValueAsString().empty())
    return false;

  // One conversion per C parameter: the 'this' handle, the member's
  // parameters and the error code pointer of msil_catch.
  const FunctionType *fnType = fn.getFunctionType();
  const DataLayout &dl = fn.getDataLayout();
  bool isCtor = member == ".ctor";
  bool thisHandle = sig.HasThis && !isCtor;
  size_t numParams = thisHandle + sig.Params.size() + (numCatches != 0);
  if (fnType->isVarArg() || fnType->getNumParams() != numParams ||
      conv.Params.size() != numParams)
    return false;
  unsigned i = 0;
  if (thisHandle) {
    ManagedConv code = conv.Params[0];
    bool fits = (code == ManagedConv::ThisObj && type.Kind == TK::Class) ||
                (code == ManagedConv::ThisVal && type.Kind == TK::ValueType &&
                 !type.isGCHandle()) ||
                (code == ManagedConv::ThisGCH && type.isGCHandle());
    if (!fits || !isManagedHandleType(fnType->getParamType(0), dl))
      return false;
    ++i;
  }
  // "msil-managed-decl" belongs to a static method or a 'this-val' or
  // 'this-gch' instance method only, and names TYPE or one of the base types
  // of a value type or an enum.
  if (fn.hasFnAttribute(ManagedDeclAttr)) {
    StringRef decl = fn.getFnAttribute(ManagedDeclAttr).getValueAsString();
    bool staticMethod = !sig.HasThis && !sig.IsField;
    bool valueThis = thisHandle && !sig.IsField &&
                     (conv.Params[0] == ManagedConv::ThisVal ||
                      conv.Params[0] == ManagedConv::ThisGCH);
    if ((!staticMethod && !valueThis) ||
        (decl != type.TypeName && decl != "System.ValueType" &&
         decl != "System.Object" && decl != "System.Enum"))
      return false;
  }
  for (const ManagedType &p : sig.Params) {
    if (!fitsManagedConversion(conv.Params[i], p, fnType->getParamType(i), dl))
      return false;
    ++i;
  }
  if (numCatches && (conv.Params[i] != ManagedConv::Err ||
                     !fnType->getParamType(i)->isPointerTy()))
    return false;
  const Type *retType = fnType->getReturnType();
  if (conv.Result == ManagedConv::Void)
    return retType->isVoidTy();
  // A constructor's result is never 'enum': its value type is boxed.
  const ManagedType &result = isCtor ? type : sig.Return;
  return !result.isVoid() && !(isCtor && conv.Result == ManagedConv::Enum) &&
         fitsManagedConversion(conv.Result, result, retType, dl);
}

// Whether the managed-call declaration \p fn has a holder of a use that is
// not a direct call of its own function type (cli::collectManagedCallHolders,
// P0's walk): P0 reports such a use as T-M10, unless \p fn got T-M17.
static bool hasNonCallUse(const Function &fn) {
  SmallSetVector<const Instruction *, 4> insts;
  SmallSetVector<const GlobalValue *, 4> globals;
  collectManagedCallHolders(fn, insts, globals);
  return !insts.empty() || !globals.empty();
}

// The width in bits of the C integer that the IR integer type \p t passes:
// an i1 (C's _Bool) passes a byte. 0 for a type that is not an integer.
static unsigned getIRIntegerWidth(const Type *t) {
  return t->isIntegerTy() ? std::max(8u, t->getIntegerBitWidth()) : 0;
}

// The checks of checkStub, from the attributes that the stub's body is made
// of (5.20.4 MR1), so that the text links report what the image links report
// for the same inputs: each type token of checkStub stands for one
// conversion code here. A declaration that P0 refuses as invalid is skipped.
static void checkDeclaration(Checker &c, const Function &fn) {
  for (StringRef kind : {ManagedMemberAttr, ManagedSigAttr, ManagedConvAttr})
    if (!fn.hasFnAttribute(kind))
      return;
  auto attr = [&](StringRef kind) {
    return fn.getFnAttribute(kind).getValueAsString();
  };
  Expected<ManagedType> type = parseManagedTypeName(attr(ManagedTypeAttr));
  Expected<ManagedSignature> sig = parseManagedSignature(attr(ManagedSigAttr));
  Expected<ManagedConvList> conv = parseManagedConvList(attr(ManagedConvAttr));
  Expected<SmallVector<ManagedCatch, 4>> catches =
      parseManagedCatchList(attr(ManagedCatchAttr));
  if (!type || !sig || !conv || !catches) {
    consumeError(type.takeError());
    consumeError(sig.takeError());
    consumeError(conv.takeError());
    consumeError(catches.takeError());
    return;
  }
  StringRef member = attr(ManagedMemberAttr);
  if (!isValidManagedCall(fn, *type, member, *sig, *conv, catches->size()))
    return;
  StringRef f = getManagedCallSymbol(fn.getName());
  bool isCtor = member == ".ctor";
  bool thisHandle = sig->HasThis && !isCtor;
  ManagedConv thisConv = thisHandle ? conv->Params.front() : ManagedConv::Void;

  // MK3: a value type TYPE for every member kind, as the stub's unbox
  // (this-val), its box (a constructor) or the member's TypeSpec parent (a
  // static method or field) names it; a class TYPE as the stub's castclass
  // (this-obj) or unboxed newobj (a constructor) names it. A static member
  // of a class TYPE has a TypeRef parent, which says nothing of the kind.
  if (type->Kind == TK::ValueType && !type->isGCHandle())
    c.checkKind(f, type->TypeName, /*asValue=*/true);
  if (type->Kind == TK::Class && (thisConv == ManagedConv::ThisObj || isCtor))
    c.checkKind(f, type->TypeName, /*asValue=*/false);

  // MK2 and MK3 per parameter: an enum (T-M7), a boxed handle's unbox.any,
  // an object handle's castclass.
  const FunctionType *fnType = fn.getFunctionType();
  for (auto [k, p] : enumerate(sig->Params)) {
    size_t j = k + (thisHandle ? 1 : 0);
    ManagedConv code = conv->Params[j];
    if (code == ManagedConv::Enum)
      c.checkEnum(f, k + 1, getIRIntegerWidth(fnType->getParamType(j)),
                  p.TypeName);
    else if (code == ManagedConv::Boxed)
      c.checkKind(f, p.TypeName, /*asValue=*/true);
    else if (code == ManagedConv::Obj && p.Kind == TK::Class)
      c.checkKind(f, p.TypeName, /*asValue=*/false);
  }
  // A boxed result's box; an enum result (a field member's value) as MK2
  // checks it (T-M7's second form).
  if (!isCtor && conv->Result == ManagedConv::Boxed)
    c.checkKind(f, sig->Return.TypeName, /*asValue=*/true);
  else if (!isCtor && conv->Result == ManagedConv::Enum)
    c.checkEnumResult(f, getIRIntegerWidth(fnType->getReturnType()),
                      sig->Return.TypeName);

  // MK7: a literal field (T-M18).
  if (sig->IsField)
    c.checkLiteral(f, type->TypeName, member);

  // MK4 (T-M9).
  for (const ManagedCatch &e : *catches)
    c.checkCatch(f, e.Type);
}

void lld::msil::checkManagedCallDeclarations(const Module &m) {
  if (ctx.references->empty())
    return;
  // The queries resolve types and emit no row: the text binds nothing.
  MetadataBuilder scratch;
  BCLBinder binder(*ctx.references, scratch);
  Checker c(binder);
  for (const Function &fn : m.functions())
    if (isManagedCallDeclaration(fn) && !hasManagedCallOfOtherType(fn) &&
        !hasManagedCallOfOtherExtension(fn) && !hasNonCallUse(fn))
      checkDeclaration(c, fn);
}

//===----------------------------------------------------------------------===//
// The access of a 'this-val' or 'this-gch' instance method, and of a static
// method that a value type or an enum inherits (5.20.3 MS5, 5.20.5 ML2 (2)
// as amended)
//===----------------------------------------------------------------------===//

// The full name of the type of the 'constrained.' token \p token of \p f: a
// TypeRef, or a TypeSpec of a class or value type on a TypeRef; std::nullopt
// for any other token.
static std::optional<std::string> getConstrainedTypeName(const ObjFile &f,
                                                         uint32_t token) {
  if (getTokenType(token) != static_cast<uint8_t>(TableId::TypeSpec))
    return getTypeRefName(f, token);
  std::optional<TypeSig> type = getTypeSpec(f, token);
  if (!type)
    return std::nullopt;
  if (type->getElementType() != ELEMENT_TYPE_VALUETYPE &&
      type->getElementType() != ELEMENT_TYPE_CLASS)
    return std::nullopt;
  return getTypeRefName(f, type->getTypeRef());
}

// The Method request for the member of the method MemberRef \p a of \p f on
// the type \p type (whose text the request refers to), with the MemberRef's
// signature in output tokens: binding the stub's tokens mapped every type
// that it names. std::nullopt when a type did not map, which that reported.
static std::optional<BCLRequest> getMethodRequest(OutputModule &out, ObjFile &f,
                                                  StringRef type,
                                                  const MemberAccess &a) {
  bool failed = false;
  auto map = [&](uint32_t t) {
    uint32_t r = out.mapTypeToken(f, t);
    failed |= r == 0;
    return r ? r : t;
  };
  BCLRequest r;
  r.K = BCLRequest::Method;
  r.RequestedBy = &f;
  std::tie(r.Namespace, r.Name) = splitTypeName(type);
  r.Member = a.member;
  r.MethodSignature = mapTypeRefs(*a.method, map);
  if (failed)
    return std::nullopt;
  return r;
}

// The output MemberRef with the name and signature of \p r's member on its
// declaring type \p d, which \p binder binds, for the access of the managed
// call \p fn of \p f; std::nullopt after an error.
static std::optional<uint32_t>
bindOnDeclaringType(BCLBinder &binder, const BCLRequest &r, StringRef d,
                    const ObjFile &f, StringRef fn) {
  BCLRequest onDecl = r;
  std::tie(onDecl.Namespace, onDecl.Name) = splitTypeName(d);
  Expected<std::optional<EntityHandle>> handle = binder.bind(onDecl);
  if (!handle) {
    error(toString(&f) + ": managed call '" + fn +
          "': " + toString(handle.takeError()));
    return std::nullopt;
  }
  // A Required request never falls back.
  if (!*handle)
    return std::nullopt;
  return (*handle)->Value;
}

// True if the references define \p t as a value type or an enum. False for
// a reference type, for a type that does not resolve (binding reports it)
// and in a link without references, whose binder knows no type's kind.
static bool isValueTypeOrEnum(BCLBinder &binder, StringRef t) {
  auto [ns, name] = splitTypeName(t);
  Expected<BCLTypeKind> kind = binder.getTypeKind(ns, name);
  if (!kind) {
    consumeError(kind.takeError());
    return false;
  }
  return *kind != BCLTypeKind::Reference;
}

// The access 'call V::M' of the stub \p m of \p f whose member M is a static
// method, when V is a value type or an enum that only inherits M: the
// runtime resolves a method MemberRef upwards only from a reference-type
// parent, so the site gets the MemberRef on M's declaring type D instead
// (System.ValueType, System.Object or System.Enum), as C# emits it. V's kind
// is its TypeDef's, whatever TYPE's spelling: an object names the member on
// a TypeSpec 'valuetype V' or on V's TypeRef. std::nullopt for any other
// stub, among them a site from LTO that names D already, and in a link
// without references.
static std::optional<ManagedAccessRewrite>
bindStaticMethodAccess(OutputModule &out, BCLBinder &binder, ObjFile &f,
                       const InputMethod &m) {
  // checkStub reported a body with more than one access or with a
  // 'constrained.' prefix.
  std::optional<StubSites> scan = scanStubSites(f, m);
  if (!scan || scan->numAccesses != 1 || !scan->prefixes.empty() ||
      !scan->access || !scan->access->method)
    return std::nullopt;
  const MemberAccess &a = *scan->access;
  if (a.op != Opcode::Call || (a.method->Header & SIG_HASTHIS) ||
      !isValueTypeOrEnum(binder, a.type))
    return std::nullopt;
  std::optional<BCLRequest> r = getMethodRequest(out, f, a.type, a);
  if (!r)
    return std::nullopt;
  // Binding the site's token reported a member that does not bind.
  Expected<std::string> d = binder.getDeclaringType(*r);
  if (!d) {
    consumeError(d.takeError());
    return std::nullopt;
  }
  if (*d == a.type)
    return std::nullopt;
  std::optional<uint32_t> token =
      bindOnDeclaringType(binder, *r, *d, f, getManagedCallSymbol(m.name));
  if (!token)
    return std::nullopt;
  return ManagedAccessRewrite{
      std::nullopt, getInstructionOffset(m.sites[*scan->accessSite]), *token};
}

ManagedAccessRewrites lld::msil::bindManagedCallAccesses(OutputModule &out,
                                                         BCLBinder &binder) {
  ManagedAccessRewrites rewrites;
  for (ObjFile *f : ctx.objectFiles) {
    for (const InputMethod *m : f->methods()) {
      if (!m->live || m->discarded || !isManagedCallStub(*f, *m))
        continue;
      if (std::optional<ManagedAccessRewrite> w =
              bindStaticMethodAccess(out, binder, *f, *m))
        rewrites[m].push_back(*w);
      ArrayRef<TokenSite> sites = m->sites;
      for (size_t i = 0, e = sites.size(); i + 1 < e; ++i) {
        // checkStub reported a 'constrained.' that is not directly before
        // the access's callvirt.
        const TokenSite &prefix = sites[i], &site = sites[i + 1];
        if (prefix.op != Opcode::Constrained || site.op != Opcode::Callvirt)
          continue;
        uint32_t prefixOffset = getInstructionOffset(prefix);
        uint32_t accessOffset = getInstructionOffset(site);
        if (accessOffset !=
                prefixOffset + getOpcodeSize(Opcode::Constrained) + 4 ||
            getTokenType(site.token) !=
                static_cast<uint8_t>(TableId::MemberRef))
          continue;
        std::optional<std::string> v = getConstrainedTypeName(*f, prefix.token);
        std::optional<MemberAccess> a =
            decodeMemberRef(*f, Opcode::Callvirt, site.token);
        if (!v || !a || !a->method)
          continue;

        // The request for V::M. A member that does not bind was reported
        // when the site's token bound.
        std::optional<BCLRequest> r = getMethodRequest(out, *f, *v, *a);
        if (!r)
          continue;
        StringRef fn = getManagedCallSymbol(m->name);
        Expected<std::string> d = binder.getDeclaringType(*r);
        if (!d) {
          consumeError(d.takeError());
          // 'constrained. V' + 'callvirt V::M': binding the site's token
          // reported V::M. 'constrained. V' + 'callvirt D::M' with no V::M is
          // in none of MK2's three stub shapes; D::M itself may bind.
          if (a->type != *v)
            error(toString(f) + ": managed call '" + fn +
                  "': the stub calls '" + a->type + "::" + a->member +
                  "' through 'constrained. " + *v + "', but '" + *v +
                  "' has no such member; the object needs a matching "
                  "toolchain");
          continue;
        }
        if (a->type != *v) {
          // Already 'constrained. V' + 'callvirt D::M' (from LTO): D must be
          // the declaring type (MK2's stub shape).
          if (a->type != *d)
            error(toString(f) + ": managed call '" + fn +
                  "': the stub calls '" + a->type + "::" + a->member +
                  "' through 'constrained. " + *v +
                  "', but the member is declared by '" + *d +
                  "'; the object needs a matching toolchain");
          continue;
        }
        if (*d == *v) {
          // V declares M: 'call V::M' after 6 nops.
          rewrites[m].push_back({prefixOffset, accessOffset, 0});
          continue;
        }
        // V inherits M: 'constrained. V' + 'callvirt D::M'.
        if (std::optional<uint32_t> token =
                bindOnDeclaringType(binder, *r, *d, *f, fn))
          rewrites[m].push_back({prefixOffset, accessOffset, *token});
      }
    }
  }
  return rewrites;
}

void lld::msil::applyManagedCallAccessRewrites(
    ArrayRef<ManagedAccessRewrite> rewrites, MutableArrayRef<uint8_t> code) {
  // FE 16 <type token>.
  constexpr uint32_t prefixSize = 6;
  for (const ManagedAccessRewrite &w : rewrites) {
    if (w.prefixOffset) {
      assert(
          *w.prefixOffset + prefixSize == w.accessOffset &&
          w.accessOffset + 5 <= code.size() && code[*w.prefixOffset] == 0xFE &&
          code[*w.prefixOffset + 1] == 0x16 && code[w.accessOffset] == 0x6F &&
          "not a 'constrained.' + 'callvirt' site");
      if (!w.memberToken) {
        std::fill_n(code.begin() + *w.prefixOffset, prefixSize, uint8_t(0x00));
        code[w.accessOffset] = 0x28;
        continue;
      }
    } else {
      assert(w.accessOffset + 5 <= code.size() &&
             code[w.accessOffset] == 0x28 && w.memberToken &&
             "not a static method's 'call' site");
    }
    for (unsigned k = 0; k != 4; ++k)
      code[w.accessOffset + 1 + k] = uint8_t(w.memberToken >> (8 * k));
  }
}

// The declaring type of the member of the managed call \p fn of type \p v, a
// static method or a 'this-val' or 'this-gch' instance method, as \p binder
// binds it; std::nullopt when it does not bind.
static std::optional<std::string>
getDeclaringType(BCLBinder &binder, const Function &fn, StringRef v) {
  Expected<ManagedSignature> sig = parseManagedSignature(
      fn.getFnAttribute(ManagedSigAttr).getValueAsString());
  if (!sig) {
    consumeError(sig.takeError());
    return std::nullopt;
  }
  // The signature in canonical listing v1 text, which the parser of
  // BCLMembers.def's signatures reads.
  std::string ret, params;
  raw_string_ostream retOs(ret), paramsOs(params);
  sig->Return.print(retOs);
  ListSeparator ls;
  for (const ManagedType &p : sig->Params) {
    paramsOs << ls;
    p.print(paramsOs);
  }
  Expected<MethodSig> methodSig = parseBCLMethodSig(
      ret, params, sig->HasThis,
      ("managed call '" + getManagedCallSymbol(fn.getName()) + "'").str(),
      [&](StringRef ns, StringRef name, bool) -> Expected<uint32_t> {
        Expected<EntityHandle> h = binder.bindType(ns, name);
        if (!h)
          return h.takeError();
        return h->Value;
      });
  if (!methodSig) {
    consumeError(methodSig.takeError());
    return std::nullopt;
  }
  BCLRequest r;
  r.K = BCLRequest::Method;
  std::tie(r.Namespace, r.Name) = splitTypeName(v);
  r.Member = fn.getFnAttribute(ManagedMemberAttr).getValueAsString();
  r.MethodSignature = std::move(*methodSig);
  Expected<std::string> d = binder.getDeclaringType(r);
  if (!d) {
    consumeError(d.takeError());
    return std::nullopt;
  }
  return std::move(*d);
}

void lld::msil::annotateManagedCallDeclaringTypes(Module &m) {
  // The static methods and the 'this-val' and 'this-gch' instance methods,
  // with V's full name.
  struct Call {
    Function *fn;
    std::string type;
    bool isStatic;
  };
  SmallVector<Call, 4> calls;
  for (Function &fn : m.functions()) {
    if (!fn.hasFnAttribute(ManagedTypeAttr))
      continue;
    auto attr = [&](StringRef kind) {
      return fn.getFnAttribute(kind).getValueAsString();
    };
    Expected<ManagedType> type = parseManagedTypeName(attr(ManagedTypeAttr));
    Expected<ManagedSignature> sig =
        parseManagedSignature(attr(ManagedSigAttr));
    Expected<ManagedConvList> conv =
        parseManagedConvList(attr(ManagedConvAttr));
    if (!type || !sig || !conv) {
      // The backend reports attributes that do not parse.
      consumeError(type.takeError());
      consumeError(sig.takeError());
      consumeError(conv.takeError());
      continue;
    }
    if (sig->IsField || attr(ManagedMemberAttr) == ".ctor")
      continue;
    if (sig->HasThis && (conv->Params.empty() ||
                         (conv->Params.front() != ManagedConv::ThisVal &&
                          conv->Params.front() != ManagedConv::ThisGCH)))
      continue;
    calls.push_back({&fn, type->TypeName, !sig->HasThis});
  }
  if (calls.empty())
    return;

  // ReferenceSet's index and caches are not thread-safe, and ThinLTO and
  // --lto-partitions generate code on several threads.
  static std::mutex referencesLock;
  std::lock_guard<std::mutex> lock(referencesLock);
  // The queries emit their rows into a scratch builder: LTO binds nothing,
  // and an LTO object's tokens bind in the writer like any object's.
  MetadataBuilder scratch;
  BCLBinder binder(*ctx.references, scratch);
  // A member that does not bind (--lto-emit-asm without references, whose
  // text binds nothing) gets no attribute: the backend's whole-program text
  // then writes its standalone final form (System.Object's four public
  // instance methods through 'constrained. V', 'call V::M' otherwise), and an
  // object-producing LTO link writes the placeholder, or a static method's
  // 'call V::M', which the writer rewrites after binding as for any object.
  // A static method gets the attribute only when V is a value type or an
  // enum that only inherits the member (bindStaticMethodAccess's rule):
  // every other one keeps 'call V::M'.
  for (const Call &call : calls) {
    if (call.isStatic && !isValueTypeOrEnum(binder, call.type))
      continue;
    std::optional<std::string> d =
        getDeclaringType(binder, *call.fn, call.type);
    if (d && (!call.isStatic || *d != call.type))
      call.fn->addFnAttr(ManagedDeclAttr, *d);
  }
}
