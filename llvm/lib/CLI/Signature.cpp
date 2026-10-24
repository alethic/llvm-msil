//===- Signature.cpp - ECMA-335 signatures --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The signature model, the encoders and decoders of II.23.2, mapTypeRefs and
// the printers of canonical listing v1's signature text.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/Signature.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/Blob.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <optional>
#include <string>

using namespace llvm;
using namespace llvm::cli;

#define DEBUG_TYPE "cli-signature"

/// The deepest type nesting the decoders accept: a type may sit inside at
/// most this many PTR, BYREF, SZARRAY, ARRAY, GENERICINST, FNPTR or PINNED
/// constructors.
static constexpr unsigned MaxSignatureDepth = 64;

/// The largest ARRAY rank the decoders accept: the CLR's MAX_RANK, so no
/// runtime loads an array type of a higher rank. The bound also keeps
/// printTypeSig's output proportional to the blob.
static constexpr uint32_t MaxArrayRank = 32;

static bool isPrimitive(ElementType ET) {
  switch (ET) {
  case ELEMENT_TYPE_VOID:
  case ELEMENT_TYPE_BOOLEAN:
  case ELEMENT_TYPE_CHAR:
  case ELEMENT_TYPE_I1:
  case ELEMENT_TYPE_U1:
  case ELEMENT_TYPE_I2:
  case ELEMENT_TYPE_U2:
  case ELEMENT_TYPE_I4:
  case ELEMENT_TYPE_U4:
  case ELEMENT_TYPE_I8:
  case ELEMENT_TYPE_U8:
  case ELEMENT_TYPE_R4:
  case ELEMENT_TYPE_R8:
  case ELEMENT_TYPE_STRING:
  case ELEMENT_TYPE_TYPEDBYREF:
  case ELEMENT_TYPE_I:
  case ELEMENT_TYPE_U:
  case ELEMENT_TYPE_OBJECT:
    return true;
  default:
    return false;
  }
}

/// The calling-convention kinds of a method signature (II.23.2.1-3).
static bool isMethodSigHeader(uint8_t Header) {
  if (Header & 0x80)
    return false;
  uint8_t Kind = Header & SIG_KIND_MASK;
  return Kind <= SIG_VARARG || Kind == SIG_UNMANAGED;
}

static bool allowsSentinel(uint8_t Header) {
  uint8_t Kind = Header & SIG_KIND_MASK;
  return Kind == SIG_VARARG || Kind == SIG_C;
}

/// The SENTINEL position that matters for equality and hashing.
static uint32_t effectiveRequiredCount(const MethodSig &S) {
  return S.RequiredParamCount < S.Params.size() ? S.RequiredParamCount
                                                : UINT32_MAX;
}

static uint32_t effectiveGenericCount(const MethodSig &S) {
  return (S.Header & SIG_GENERIC) ? S.GenericParamCount : 0;
}

//===----------------------------------------------------------------------===//
// TypeSig
//===----------------------------------------------------------------------===//

/// The immutable node of a TypeSig that is not a primitive without
/// modifiers. Fields that the element type does not use keep their default
/// values, so == can compare all of them.
struct TypeSig::Node {
  ElementType ET = ELEMENT_TYPE_VOID;
  /// GENERICINST: CLASS or VALUETYPE.
  ElementType InstKind = ELEMENT_TYPE_END;
  /// The TypeRef (CLASS, VALUETYPE, GENERICINST) or the generic parameter
  /// index (VAR, MVAR).
  uint32_t Value = 0;
  SmallVector<TypeSig, 1> Operands;
  ArrayShape Shape;
  std::optional<MethodSig> FnSig;
  SmallVector<CustomModifier, 0> Modifiers;
  /// hash_value of everything above.
  hash_code Hash;

  explicit Node(ElementType ET) : ET(ET) {}

  /// Computes the hash and wraps the node.
  static TypeSig make(Node &&N) {
    N.Hash = N.computeHash();
    return TypeSig(std::make_shared<const Node>(std::move(N)));
  }

  /// T with Mods appended after its modifiers.
  static TypeSig addModifiers(const TypeSig &T, ArrayRef<CustomModifier> Mods) {
    if (Mods.empty())
      return T;
    Node New = T.N ? *T.N : Node(T.Primitive);
    New.Modifiers.append(Mods.begin(), Mods.end());
    return make(std::move(New));
  }

  hash_code computeHash() const {
    hash_code H =
        hash_combine(static_cast<uint8_t>(ET), static_cast<uint8_t>(InstKind),
                     Value, hash_combine_range(Operands));
    H = hash_combine(H, Shape.Rank, hash_combine_range(Shape.Sizes),
                     hash_combine_range(Shape.LowerBounds));
    if (FnSig)
      H = hash_combine(H, *FnSig);
    for (const CustomModifier &M : Modifiers)
      H = hash_combine(H, M.IsRequired, M.Type);
    return H;
  }
};

TypeSig::TypeSig(std::shared_ptr<const Node> N) : N(std::move(N)) {}

TypeSig TypeSig::get(ElementType Primitive) {
  assert(isPrimitive(Primitive) && "not a primitive element type");
  TypeSig T;
  T.Primitive = Primitive;
  return T;
}

TypeSig TypeSig::getClass(uint32_t TypeRef) {
  Node N(ELEMENT_TYPE_CLASS);
  N.Value = TypeRef;
  return Node::make(std::move(N));
}

TypeSig TypeSig::getValueType(uint32_t TypeRef) {
  Node N(ELEMENT_TYPE_VALUETYPE);
  N.Value = TypeRef;
  return Node::make(std::move(N));
}

TypeSig TypeSig::getPointer(TypeSig Pointee) {
  Node N(ELEMENT_TYPE_PTR);
  N.Operands.push_back(std::move(Pointee));
  return Node::make(std::move(N));
}

TypeSig TypeSig::getByRef(TypeSig Referent) {
  Node N(ELEMENT_TYPE_BYREF);
  N.Operands.push_back(std::move(Referent));
  return Node::make(std::move(N));
}

TypeSig TypeSig::getSZArray(TypeSig Element) {
  Node N(ELEMENT_TYPE_SZARRAY);
  N.Operands.push_back(std::move(Element));
  return Node::make(std::move(N));
}

TypeSig TypeSig::getArray(TypeSig Element, ArrayShape Shape) {
  Node N(ELEMENT_TYPE_ARRAY);
  N.Operands.push_back(std::move(Element));
  N.Shape = std::move(Shape);
  return Node::make(std::move(N));
}

TypeSig TypeSig::getGenericInst(bool IsValueType, uint32_t GenericType,
                                ArrayRef<TypeSig> Args) {
  Node N(ELEMENT_TYPE_GENERICINST);
  N.InstKind = IsValueType ? ELEMENT_TYPE_VALUETYPE : ELEMENT_TYPE_CLASS;
  N.Value = GenericType;
  N.Operands.append(Args.begin(), Args.end());
  return Node::make(std::move(N));
}

TypeSig TypeSig::getGenericParam(bool IsMethodParam, uint32_t Index) {
  Node N(IsMethodParam ? ELEMENT_TYPE_MVAR : ELEMENT_TYPE_VAR);
  N.Value = Index;
  return Node::make(std::move(N));
}

TypeSig TypeSig::getFunctionPointer(const MethodSig &Sig) {
  Node N(ELEMENT_TYPE_FNPTR);
  N.FnSig = Sig;
  return Node::make(std::move(N));
}

TypeSig TypeSig::getPinned(TypeSig Local) {
  Node N(ELEMENT_TYPE_PINNED);
  N.Operands.push_back(std::move(Local));
  return Node::make(std::move(N));
}

TypeSig TypeSig::withModifier(CustomModifier M) const {
  return Node::addModifiers(*this, M);
}

TypeSig TypeSig::withModifiers(ArrayRef<CustomModifier> Ms) const {
  return Node::addModifiers(*this, Ms);
}

ElementType TypeSig::getElementType() const { return N ? N->ET : Primitive; }

uint32_t TypeSig::getTypeRef() const {
  assert(N &&
         (N->ET == ELEMENT_TYPE_CLASS || N->ET == ELEMENT_TYPE_VALUETYPE ||
          N->ET == ELEMENT_TYPE_GENERICINST) &&
         "not a CLASS, VALUETYPE or GENERICINST type");
  return N->Value;
}

ElementType TypeSig::getGenericInstKind() const {
  assert(N && N->ET == ELEMENT_TYPE_GENERICINST && "not a GENERICINST type");
  return N->InstKind;
}

ArrayRef<TypeSig> TypeSig::getOperands() const {
  if (!N)
    return {};
  return N->Operands;
}

const ArrayShape &TypeSig::getArrayShape() const {
  assert(N && N->ET == ELEMENT_TYPE_ARRAY && "not an ARRAY type");
  return N->Shape;
}

const MethodSig &TypeSig::getFunctionSig() const {
  assert(N && N->ET == ELEMENT_TYPE_FNPTR && "not a FNPTR type");
  return *N->FnSig;
}

uint32_t TypeSig::getGenericParamIndex() const {
  assert(N && (N->ET == ELEMENT_TYPE_VAR || N->ET == ELEMENT_TYPE_MVAR) &&
         "not a VAR or MVAR type");
  return N->Value;
}

bool TypeSig::isMethodGenericParam() const {
  return getElementType() == ELEMENT_TYPE_MVAR;
}

ArrayRef<CustomModifier> TypeSig::getModifiers() const {
  if (!N)
    return {};
  return N->Modifiers;
}

namespace llvm {
namespace cli {

bool operator==(const TypeSig &L, const TypeSig &R) {
  // A primitive without modifiers never has a node, so a TypeSig with a node
  // never equals one without.
  if (L.N == R.N)
    return L.N || L.Primitive == R.Primitive;
  if (!L.N || !R.N)
    return false;
  const TypeSig::Node &A = *L.N, &B = *R.N;
  return A.Hash == B.Hash && A.ET == B.ET && A.InstKind == B.InstKind &&
         A.Value == B.Value && A.Modifiers == B.Modifiers &&
         A.Operands == B.Operands && A.Shape == B.Shape && A.FnSig == B.FnSig;
}

hash_code hash_value(const TypeSig &T) {
  if (!T.N)
    return llvm::hash_value(static_cast<uint8_t>(T.Primitive));
  return T.N->Hash;
}

bool operator==(const MethodSig &L, const MethodSig &R) {
  return L.Header == R.Header &&
         effectiveGenericCount(L) == effectiveGenericCount(R) &&
         effectiveRequiredCount(L) == effectiveRequiredCount(R) &&
         L.Return == R.Return && L.Params == R.Params;
}

hash_code hash_value(const MethodSig &S) {
  return hash_combine(S.Header, effectiveGenericCount(S),
                      effectiveRequiredCount(S), S.Return,
                      hash_combine_range(S.Params));
}

} // namespace cli
} // namespace llvm

//===----------------------------------------------------------------------===//
// Encoders
//===----------------------------------------------------------------------===//

namespace {
class SigEncoder {
public:
  explicit SigEncoder(SmallVectorImpl<uint8_t> &Out) : W(Out) {}

  void typeRef(uint32_t Token) {
    W.writeCompressedUInt(encodeTypeDefOrRefOrSpec(Token));
  }

  void type(const TypeSig &T, bool AllowPinned = false) {
    for (const CustomModifier &M : T.getModifiers()) {
      W.writeU8(M.IsRequired ? ELEMENT_TYPE_CMOD_REQD : ELEMENT_TYPE_CMOD_OPT);
      typeRef(M.Type);
    }
    ElementType ET = T.getElementType();
    if (isPrimitive(ET)) {
      W.writeU8(ET);
      return;
    }
    W.writeU8(ET);
    switch (ET) {
    case ELEMENT_TYPE_CLASS:
    case ELEMENT_TYPE_VALUETYPE:
      typeRef(T.getTypeRef());
      return;
    case ELEMENT_TYPE_PTR:
    case ELEMENT_TYPE_BYREF:
    case ELEMENT_TYPE_SZARRAY:
      type(T.getOperands()[0]);
      return;
    case ELEMENT_TYPE_PINNED:
      assert(AllowPinned && "PINNED outside the top of a local signature");
      (void)AllowPinned;
      type(T.getOperands()[0]);
      return;
    case ELEMENT_TYPE_ARRAY: {
      type(T.getOperands()[0]);
      const ArrayShape &S = T.getArrayShape();
      assert(S.Rank != 0 && "array rank 0");
      W.writeCompressedUInt(S.Rank);
      W.writeCompressedUInt(S.Sizes.size());
      for (uint32_t Size : S.Sizes)
        W.writeCompressedUInt(Size);
      W.writeCompressedUInt(S.LowerBounds.size());
      for (int32_t Bound : S.LowerBounds)
        W.writeCompressedInt(Bound);
      return;
    }
    case ELEMENT_TYPE_GENERICINST: {
      ArrayRef<TypeSig> Args = T.getOperands();
      assert(!Args.empty() && "GENERICINST without arguments");
      W.writeU8(T.getGenericInstKind());
      typeRef(T.getTypeRef());
      W.writeCompressedUInt(Args.size());
      for (const TypeSig &A : Args)
        type(A);
      return;
    }
    case ELEMENT_TYPE_VAR:
    case ELEMENT_TYPE_MVAR:
      W.writeCompressedUInt(T.getGenericParamIndex());
      return;
    case ELEMENT_TYPE_FNPTR:
      method(T.getFunctionSig());
      return;
    default:
      llvm_unreachable("TypeSig with an unexpected element type");
    }
  }

  void method(const MethodSig &S) {
    assert(isMethodSigHeader(S.Header) && "not a method signature header");
    W.writeU8(S.Header);
    if (S.Header & SIG_GENERIC)
      W.writeCompressedUInt(S.GenericParamCount);
    W.writeCompressedUInt(S.Params.size());
    type(S.Return);
    bool HasSentinel = S.RequiredParamCount < S.Params.size();
    assert((!HasSentinel || allowsSentinel(S.Header)) &&
           "SENTINEL outside a VARARG or C signature");
    for (size_t I = 0, E = S.Params.size(); I != E; ++I) {
      if (HasSentinel && I == S.RequiredParamCount)
        W.writeU8(ELEMENT_TYPE_SENTINEL);
      type(S.Params[I]);
    }
  }

  BlobWriter W;
};
} // namespace

void cli::encodeMethodSig(const MethodSig &S, SmallVectorImpl<uint8_t> &Out) {
  SigEncoder(Out).method(S);
}

void cli::encodeFieldSig(const TypeSig &T, SmallVectorImpl<uint8_t> &Out) {
  SigEncoder E(Out);
  E.W.writeU8(SIG_FIELD);
  E.type(T);
}

void cli::encodeLocalSig(ArrayRef<TypeSig> Locals,
                         SmallVectorImpl<uint8_t> &Out) {
  SigEncoder E(Out);
  E.W.writeU8(SIG_LOCAL);
  E.W.writeCompressedUInt(Locals.size());
  for (const TypeSig &T : Locals)
    E.type(T, /*AllowPinned=*/true);
}

void cli::encodePropertySig(const PropertySig &S,
                            SmallVectorImpl<uint8_t> &Out) {
  SigEncoder E(Out);
  E.W.writeU8(SIG_PROPERTY | (S.HasThis ? SIG_HASTHIS : 0));
  E.W.writeCompressedUInt(S.Params.size());
  E.type(S.Type);
  for (const TypeSig &T : S.Params)
    E.type(T);
}

void cli::encodeTypeSpec(const TypeSig &T, SmallVectorImpl<uint8_t> &Out) {
  SigEncoder(Out).type(T);
}

void cli::encodeMethodSpec(ArrayRef<TypeSig> Args,
                           SmallVectorImpl<uint8_t> &Out) {
  assert(!Args.empty() && "MethodSpec without arguments");
  SigEncoder E(Out);
  E.W.writeU8(SIG_GENERICINST);
  E.W.writeCompressedUInt(Args.size());
  for (const TypeSig &T : Args)
    E.type(T);
}

//===----------------------------------------------------------------------===//
// Decoders
//===----------------------------------------------------------------------===//

namespace {
/// A recursive-descent decoder over a BlobReader. Every function returns
/// false after the first failure, which is either the reader's (truncation,
/// bad compressed integer) or a recorded semantic one.
class SigDecoder {
public:
  explicit SigDecoder(ArrayRef<uint8_t> Blob) : Blob(Blob), R(Blob) {}

  bool fail(const Twine &Cause, size_t Offset) {
    if (!Msg && !R.hasError())
      Msg = (Cause + " at offset 0x" + utohexstr(Offset)).str();
    return false;
  }

  Error takeError() {
    Error E = R.takeError();
    std::string Text = Msg ? *Msg : toString(std::move(E));
    if (Msg)
      consumeError(std::move(E));
    return createStringError(make_error_code(errc::illegal_byte_sequence),
                             "invalid signature: " + Text);
  }

  /// The next byte, if any, without consuming it.
  std::optional<uint8_t> peek() const {
    if (R.hasError() || R.empty())
      return std::nullopt;
    return Blob[R.tell()];
  }

  bool u8(uint8_t &V) {
    V = R.readU8();
    return !R.hasError();
  }

  bool compressed(uint32_t &V) {
    V = R.readCompressedUInt();
    return !R.hasError();
  }

  bool typeRef(uint32_t &Token) {
    size_t Off = R.tell();
    uint32_t Coded;
    if (!compressed(Coded))
      return false;
    std::optional<uint32_t> T = decodeTypeDefOrRefOrSpec(Coded);
    if (!T) {
      const char *Why = (Coded & 3) == 3    ? "tag 3"
                        : (Coded >> 2) == 0 ? "row 0"
                                            : "row out of range";
      return fail("bad TypeDefOrRefOrSpecEncoded value 0x" + utohexstr(Coded) +
                      " (" + Why + ")",
                  Off);
    }
    Token = *T;
    return true;
  }

  bool type(TypeSig &T, unsigned Depth, bool AllowPinned = false);
  bool methodBody(MethodSig &S, uint8_t Header, unsigned Depth);
  bool shape(ArrayShape &S);

  /// Checks that the whole blob was consumed.
  bool finish() {
    if (R.hasError() || Msg)
      return false;
    if (R.empty())
      return true;
    size_t Off = R.tell();
    if (Blob[Off] == ELEMENT_TYPE_SENTINEL)
      return fail("misplaced SENTINEL", Off);
    size_t N = R.remaining();
    return fail(Twine(N) + (N == 1 ? " trailing byte" : " trailing bytes"),
                Off);
  }

  ArrayRef<uint8_t> Blob;
  BlobReader R;
  std::optional<std::string> Msg;
};
} // namespace

bool SigDecoder::shape(ArrayShape &S) {
  size_t Off = R.tell();
  if (!compressed(S.Rank))
    return false;
  if (S.Rank == 0 || S.Rank > MaxArrayRank)
    return fail("array rank " + Twine(S.Rank), Off);
  uint32_t N;
  if (!compressed(N))
    return false;
  for (uint32_t I = 0; I != N; ++I) {
    uint32_t Size = R.readCompressedUInt();
    if (R.hasError())
      return false;
    S.Sizes.push_back(Size);
  }
  if (!compressed(N))
    return false;
  for (uint32_t I = 0; I != N; ++I) {
    int32_t Bound = R.readCompressedInt();
    if (R.hasError())
      return false;
    S.LowerBounds.push_back(Bound);
  }
  return true;
}

bool SigDecoder::type(TypeSig &T, unsigned Depth, bool AllowPinned) {
  if (Depth > MaxSignatureDepth)
    return fail("nesting deeper than " + Twine(MaxSignatureDepth) + " levels",
                R.tell());
  SmallVector<CustomModifier, 2> Mods;
  while (std::optional<uint8_t> B = peek()) {
    if (*B != ELEMENT_TYPE_CMOD_REQD && *B != ELEMENT_TYPE_CMOD_OPT)
      break;
    R.readU8();
    CustomModifier M;
    M.IsRequired = *B == ELEMENT_TYPE_CMOD_REQD;
    if (!typeRef(M.Type))
      return false;
    Mods.push_back(M);
  }

  size_t Off = R.tell();
  uint8_t B;
  if (!u8(B))
    return false;
  ElementType ET = static_cast<ElementType>(B);
  if (isPrimitive(ET)) {
    T = TypeSig::get(ET);
  } else {
    switch (ET) {
    case ELEMENT_TYPE_CLASS:
    case ELEMENT_TYPE_VALUETYPE: {
      uint32_t Token;
      if (!typeRef(Token))
        return false;
      T = ET == ELEMENT_TYPE_CLASS ? TypeSig::getClass(Token)
                                   : TypeSig::getValueType(Token);
      break;
    }
    case ELEMENT_TYPE_PTR:
    case ELEMENT_TYPE_BYREF:
    case ELEMENT_TYPE_SZARRAY: {
      TypeSig Op;
      if (!type(Op, Depth + 1))
        return false;
      T = ET == ELEMENT_TYPE_PTR     ? TypeSig::getPointer(std::move(Op))
          : ET == ELEMENT_TYPE_BYREF ? TypeSig::getByRef(std::move(Op))
                                     : TypeSig::getSZArray(std::move(Op));
      break;
    }
    case ELEMENT_TYPE_PINNED: {
      if (!AllowPinned)
        return fail("misplaced PINNED", Off);
      TypeSig Op;
      if (!type(Op, Depth + 1))
        return false;
      T = TypeSig::getPinned(std::move(Op));
      break;
    }
    case ELEMENT_TYPE_ARRAY: {
      TypeSig Elem;
      if (!type(Elem, Depth + 1))
        return false;
      ArrayShape S;
      if (!shape(S))
        return false;
      T = TypeSig::getArray(std::move(Elem), std::move(S));
      break;
    }
    case ELEMENT_TYPE_GENERICINST: {
      size_t KindOff = R.tell();
      uint8_t Kind;
      if (!u8(Kind))
        return false;
      if (Kind != ELEMENT_TYPE_CLASS && Kind != ELEMENT_TYPE_VALUETYPE)
        return fail("expected CLASS or VALUETYPE after GENERICINST, found 0x" +
                        utohexstr(Kind),
                    KindOff);
      uint32_t Generic;
      if (!typeRef(Generic))
        return false;
      size_t CountOff = R.tell();
      uint32_t Count;
      if (!compressed(Count))
        return false;
      if (Count == 0)
        return fail("generic instantiation without arguments", CountOff);
      SmallVector<TypeSig, 4> Args;
      for (uint32_t I = 0; I != Count; ++I) {
        TypeSig A;
        if (!type(A, Depth + 1))
          return false;
        Args.push_back(std::move(A));
      }
      T = TypeSig::getGenericInst(Kind == ELEMENT_TYPE_VALUETYPE, Generic,
                                  Args);
      break;
    }
    case ELEMENT_TYPE_VAR:
    case ELEMENT_TYPE_MVAR: {
      uint32_t Index;
      if (!compressed(Index))
        return false;
      T = TypeSig::getGenericParam(ET == ELEMENT_TYPE_MVAR, Index);
      break;
    }
    case ELEMENT_TYPE_FNPTR: {
      size_t HeaderOff = R.tell();
      uint8_t Header;
      if (!u8(Header))
        return false;
      if (!isMethodSigHeader(Header))
        return fail("bad method signature header 0x" + utohexstr(Header),
                    HeaderOff);
      MethodSig S;
      if (!methodBody(S, Header, Depth + 1))
        return false;
      T = TypeSig::getFunctionPointer(S);
      break;
    }
    case ELEMENT_TYPE_SENTINEL:
      return fail("misplaced SENTINEL", Off);
    default:
      return fail("unknown element type 0x" + utohexstr(B), Off);
    }
  }
  T = T.withModifiers(Mods);
  return true;
}

bool SigDecoder::methodBody(MethodSig &S, uint8_t Header, unsigned Depth) {
  S.Header = Header;
  if ((Header & SIG_GENERIC) && !compressed(S.GenericParamCount))
    return false;
  uint32_t Count;
  if (!compressed(Count))
    return false;
  if (!type(S.Return, Depth))
    return false;
  for (uint32_t I = 0; I != Count; ++I) {
    if (std::optional<uint8_t> B = peek(); B && *B == ELEMENT_TYPE_SENTINEL) {
      size_t Off = R.tell();
      if (!allowsSentinel(Header) || S.RequiredParamCount != UINT32_MAX)
        return fail("misplaced SENTINEL", Off);
      R.readU8();
      S.RequiredParamCount = I;
    }
    TypeSig P;
    if (!type(P, Depth))
      return false;
    S.Params.push_back(std::move(P));
  }
  return true;
}

Expected<MethodSig> cli::decodeMethodSig(ArrayRef<uint8_t> Blob) {
  SigDecoder D(Blob);
  uint8_t Header;
  if (!D.u8(Header))
    return D.takeError();
  if (!isMethodSigHeader(Header)) {
    D.fail("bad method signature header 0x" + utohexstr(Header), 0);
    return D.takeError();
  }
  MethodSig S;
  if (!D.methodBody(S, Header, 0) || !D.finish())
    return D.takeError();
  return S;
}

Expected<TypeSig> cli::decodeFieldSig(ArrayRef<uint8_t> Blob) {
  SigDecoder D(Blob);
  uint8_t Header;
  if (!D.u8(Header))
    return D.takeError();
  if (Header != SIG_FIELD) {
    D.fail("expected a field signature (0x06), found 0x" + utohexstr(Header),
           0);
    return D.takeError();
  }
  TypeSig T;
  if (!D.type(T, 0) || !D.finish())
    return D.takeError();
  return T;
}

Expected<std::vector<TypeSig>> cli::decodeLocalSig(ArrayRef<uint8_t> Blob) {
  SigDecoder D(Blob);
  uint8_t Header;
  if (!D.u8(Header))
    return D.takeError();
  if (Header != SIG_LOCAL) {
    D.fail("expected a local signature (0x07), found 0x" + utohexstr(Header),
           0);
    return D.takeError();
  }
  uint32_t Count;
  if (!D.compressed(Count))
    return D.takeError();
  std::vector<TypeSig> Locals;
  for (uint32_t I = 0; I != Count; ++I) {
    TypeSig T;
    if (!D.type(T, 0, /*AllowPinned=*/true))
      return D.takeError();
    Locals.push_back(std::move(T));
  }
  if (!D.finish())
    return D.takeError();
  return Locals;
}

Expected<PropertySig> cli::decodePropertySig(ArrayRef<uint8_t> Blob) {
  SigDecoder D(Blob);
  uint8_t Header;
  if (!D.u8(Header))
    return D.takeError();
  if ((Header & ~SIG_HASTHIS) != SIG_PROPERTY) {
    D.fail("bad property signature header 0x" + utohexstr(Header), 0);
    return D.takeError();
  }
  PropertySig S;
  S.HasThis = Header & SIG_HASTHIS;
  uint32_t Count;
  if (!D.compressed(Count) || !D.type(S.Type, 0))
    return D.takeError();
  for (uint32_t I = 0; I != Count; ++I) {
    TypeSig T;
    if (!D.type(T, 0))
      return D.takeError();
    S.Params.push_back(std::move(T));
  }
  if (!D.finish())
    return D.takeError();
  return S;
}

Expected<TypeSig> cli::decodeTypeSpec(ArrayRef<uint8_t> Blob) {
  SigDecoder D(Blob);
  TypeSig T;
  if (!D.type(T, 0) || !D.finish())
    return D.takeError();
  return T;
}

Expected<std::vector<TypeSig>> cli::decodeMethodSpec(ArrayRef<uint8_t> Blob) {
  SigDecoder D(Blob);
  uint8_t Header;
  if (!D.u8(Header))
    return D.takeError();
  if (Header != SIG_GENERICINST) {
    D.fail("expected a MethodSpec signature (0x0A), found 0x" +
               utohexstr(Header),
           0);
    return D.takeError();
  }
  uint32_t Count;
  if (!D.compressed(Count))
    return D.takeError();
  if (Count == 0) {
    D.fail("MethodSpec without arguments", 1);
    return D.takeError();
  }
  std::vector<TypeSig> Args;
  for (uint32_t I = 0; I != Count; ++I) {
    TypeSig T;
    if (!D.type(T, 0))
      return D.takeError();
    Args.push_back(std::move(T));
  }
  if (!D.finish())
    return D.takeError();
  return Args;
}

//===----------------------------------------------------------------------===//
// mapTypeRefs
//===----------------------------------------------------------------------===//

TypeSig cli::mapTypeRefs(const TypeSig &T,
                         function_ref<uint32_t(uint32_t)> Map) {
  TypeSig R;
  ElementType ET = T.getElementType();
  if (isPrimitive(ET)) {
    R = TypeSig::get(ET);
  } else {
    switch (ET) {
    case ELEMENT_TYPE_CLASS:
      R = TypeSig::getClass(Map(T.getTypeRef()));
      break;
    case ELEMENT_TYPE_VALUETYPE:
      R = TypeSig::getValueType(Map(T.getTypeRef()));
      break;
    case ELEMENT_TYPE_PTR:
      R = TypeSig::getPointer(mapTypeRefs(T.getOperands()[0], Map));
      break;
    case ELEMENT_TYPE_BYREF:
      R = TypeSig::getByRef(mapTypeRefs(T.getOperands()[0], Map));
      break;
    case ELEMENT_TYPE_SZARRAY:
      R = TypeSig::getSZArray(mapTypeRefs(T.getOperands()[0], Map));
      break;
    case ELEMENT_TYPE_PINNED:
      R = TypeSig::getPinned(mapTypeRefs(T.getOperands()[0], Map));
      break;
    case ELEMENT_TYPE_ARRAY:
      R = TypeSig::getArray(mapTypeRefs(T.getOperands()[0], Map),
                            T.getArrayShape());
      break;
    case ELEMENT_TYPE_GENERICINST: {
      SmallVector<TypeSig, 4> Args;
      for (const TypeSig &A : T.getOperands())
        Args.push_back(mapTypeRefs(A, Map));
      R = TypeSig::getGenericInst(T.getGenericInstKind() ==
                                      ELEMENT_TYPE_VALUETYPE,
                                  Map(T.getTypeRef()), Args);
      break;
    }
    case ELEMENT_TYPE_VAR:
    case ELEMENT_TYPE_MVAR:
      R = TypeSig::getGenericParam(ET == ELEMENT_TYPE_MVAR,
                                   T.getGenericParamIndex());
      break;
    case ELEMENT_TYPE_FNPTR:
      R = TypeSig::getFunctionPointer(mapTypeRefs(T.getFunctionSig(), Map));
      break;
    default:
      llvm_unreachable("TypeSig with an unexpected element type");
    }
  }
  SmallVector<CustomModifier, 2> Mods;
  for (const CustomModifier &M : T.getModifiers())
    Mods.push_back({M.IsRequired, Map(M.Type)});
  return R.withModifiers(Mods);
}

MethodSig cli::mapTypeRefs(const MethodSig &S,
                           function_ref<uint32_t(uint32_t)> Map) {
  MethodSig R;
  R.Header = S.Header;
  R.GenericParamCount = S.GenericParamCount;
  R.RequiredParamCount = S.RequiredParamCount;
  R.Return = mapTypeRefs(S.Return, Map);
  R.Params.reserve(S.Params.size());
  for (const TypeSig &P : S.Params)
    R.Params.push_back(mapTypeRefs(P, Map));
  return R;
}

//===----------------------------------------------------------------------===//
// Printing (canonical listing v1, plan 5.7.10)
//===----------------------------------------------------------------------===//

using PrintTypeRefFn = function_ref<void(raw_ostream &, uint32_t)>;

static void printRef(raw_ostream &OS, uint32_t TypeRef, PrintTypeRefFn P) {
  if (P)
    P(OS, TypeRef);
  else
    OS << format_hex(TypeRef, 10, /*Upper=*/true);
}

static StringRef getPrimitiveName(ElementType ET) {
  switch (ET) {
  case ELEMENT_TYPE_VOID:
    return "void";
  case ELEMENT_TYPE_BOOLEAN:
    return "bool";
  case ELEMENT_TYPE_CHAR:
    return "char";
  case ELEMENT_TYPE_I1:
    return "int8";
  case ELEMENT_TYPE_U1:
    return "uint8";
  case ELEMENT_TYPE_I2:
    return "int16";
  case ELEMENT_TYPE_U2:
    return "uint16";
  case ELEMENT_TYPE_I4:
    return "int32";
  case ELEMENT_TYPE_U4:
    return "uint32";
  case ELEMENT_TYPE_I8:
    return "int64";
  case ELEMENT_TYPE_U8:
    return "uint64";
  case ELEMENT_TYPE_R4:
    return "float32";
  case ELEMENT_TYPE_R8:
    return "float64";
  case ELEMENT_TYPE_I:
    return "native int";
  case ELEMENT_TYPE_U:
    return "native uint";
  case ELEMENT_TYPE_STRING:
    return "string";
  case ELEMENT_TYPE_OBJECT:
    return "object";
  case ELEMENT_TYPE_TYPEDBYREF:
    return "typedref";
  default:
    llvm_unreachable("not a primitive element type");
  }
}

/// A general array's brackets (plan 5.7.10 as amended in section 12): the
/// Rank dimension texts joined by ','. Dimension I has a lower bound when
/// I < NumLoBounds and a size when I < NumSizes, and prints '<lo>...<hi>'
/// with both (hi = lo + size - 1, in 64 bits), '<lo>...' with a lower bound
/// only, '<size>' with a size only, and nothing with neither, except that a
/// rank-1 array with neither prints '...' (so that it differs from SZARRAY's
/// '[]').
static void printArrayShape(raw_ostream &OS, const ArrayShape &S) {
  OS << '[';
  for (uint32_t I = 0; I != S.Rank; ++I) {
    if (I > 0)
      OS << ',';
    bool HasLo = I < S.LowerBounds.size();
    bool HasSize = I < S.Sizes.size();
    if (HasLo) {
      int64_t Lo = S.LowerBounds[I];
      OS << Lo << "...";
      if (HasSize)
        OS << Lo + int64_t(S.Sizes[I]) - 1;
    } else if (HasSize) {
      OS << S.Sizes[I];
    } else if (S.Rank == 1) {
      OS << "...";
    }
  }
  OS << ']';
}

void cli::printTypeSig(raw_ostream &OS, const TypeSig &T,
                       PrintTypeRefFn PrintTypeRef) {
  ElementType ET = T.getElementType();
  if (isPrimitive(ET)) {
    OS << getPrimitiveName(ET);
  } else {
    switch (ET) {
    case ELEMENT_TYPE_CLASS:
      OS << "class ";
      printRef(OS, T.getTypeRef(), PrintTypeRef);
      break;
    case ELEMENT_TYPE_VALUETYPE:
      OS << "valuetype ";
      printRef(OS, T.getTypeRef(), PrintTypeRef);
      break;
    case ELEMENT_TYPE_PTR:
      printTypeSig(OS, T.getOperands()[0], PrintTypeRef);
      OS << '*';
      break;
    case ELEMENT_TYPE_BYREF:
      printTypeSig(OS, T.getOperands()[0], PrintTypeRef);
      OS << '&';
      break;
    case ELEMENT_TYPE_SZARRAY:
      printTypeSig(OS, T.getOperands()[0], PrintTypeRef);
      OS << "[]";
      break;
    case ELEMENT_TYPE_ARRAY: {
      printTypeSig(OS, T.getOperands()[0], PrintTypeRef);
      printArrayShape(OS, T.getArrayShape());
      break;
    }
    case ELEMENT_TYPE_GENERICINST: {
      OS << (T.getGenericInstKind() == ELEMENT_TYPE_VALUETYPE ? "valuetype "
                                                              : "class ");
      printRef(OS, T.getTypeRef(), PrintTypeRef);
      OS << '<';
      ListSeparator LS;
      for (const TypeSig &A : T.getOperands()) {
        OS << LS;
        printTypeSig(OS, A, PrintTypeRef);
      }
      OS << '>';
      break;
    }
    case ELEMENT_TYPE_VAR:
      OS << '!' << T.getGenericParamIndex();
      break;
    case ELEMENT_TYPE_MVAR:
      OS << "!!" << T.getGenericParamIndex();
      break;
    case ELEMENT_TYPE_FNPTR:
      OS << "method ";
      printMethodSig(OS, T.getFunctionSig(), PrintTypeRef);
      break;
    case ELEMENT_TYPE_PINNED:
      printTypeSig(OS, T.getOperands()[0], PrintTypeRef);
      OS << " pinned";
      break;
    default:
      llvm_unreachable("TypeSig with an unexpected element type");
    }
  }
  for (const CustomModifier &M : T.getModifiers()) {
    OS << (M.IsRequired ? " modreq(" : " modopt(");
    printRef(OS, M.Type, PrintTypeRef);
    OS << ')';
  }
}

void cli::printMethodSig(raw_ostream &OS, const MethodSig &S,
                         PrintTypeRefFn PrintTypeRef) {
  if (S.Header & SIG_HASTHIS)
    OS << "instance ";
  if (S.Header & SIG_EXPLICITTHIS)
    OS << "explicit ";
  switch (S.Header & SIG_KIND_MASK) {
  case SIG_DEFAULT:
    break;
  case SIG_C:
    OS << "unmanaged cdecl ";
    break;
  case SIG_STDCALL:
    OS << "unmanaged stdcall ";
    break;
  case SIG_THISCALL:
    OS << "unmanaged thiscall ";
    break;
  case SIG_FASTCALL:
    OS << "unmanaged fastcall ";
    break;
  case SIG_VARARG:
    OS << "vararg ";
    break;
  case SIG_UNMANAGED:
    OS << "unmanaged ";
    break;
  default:
    // Not a method signature (the decoders never produce one): the kind as a
    // number, as System.Reflection.Metadata spells an unnamed convention.
    OS << unsigned(S.Header & SIG_KIND_MASK) << ' ';
    break;
  }
  if ((S.Header & SIG_GENERIC) && S.GenericParamCount > 0)
    OS << '<' << S.GenericParamCount << "> ";
  printTypeSig(OS, S.Return, PrintTypeRef);
  OS << '(';
  for (size_t I = 0, E = S.Params.size(); I != E; ++I) {
    if (I > 0)
      OS << ", ";
    if (I == S.RequiredParamCount)
      OS << "..., ";
    printTypeSig(OS, S.Params[I], PrintTypeRef);
  }
  OS << ')';
}
