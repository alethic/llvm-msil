//===- llvm/CLI/Signature.h - ECMA-335 signatures ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the one signature model of the ECMA-335 library
// (II.23.2): TypeSig, MethodSig and PropertySig, their encoders and decoders
// for every signature blob kind (method, field, local, property, TypeSpec and
// MethodSpec), mapTypeRefs, which renumbers the type references a signature
// holds, and the printers of canonical listing v1's signature text.
//
// Every Phase-2 signature uses this model: definitions, call sites, calli,
// BCL members, P/Invoke stubs and reference binding. The MSIL target keeps
// its Phase-1 MSIL::Signature as the codegen view.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_SIGNATURE_H
#define LLVM_CLI_SIGNATURE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {
class raw_ostream;

namespace cli {

/// A custom modifier (II.23.2.7): CMOD_REQD or CMOD_OPT and its type. A
/// TypeSig's modifiers are kept in blob order.
struct CustomModifier {
  bool IsRequired = false;
  uint32_t Type = 0; ///< A TypeDefOrRefOrSpec token or a client id.

  friend bool operator==(const CustomModifier &L, const CustomModifier &R) {
    return L.IsRequired == R.IsRequired && L.Type == R.Type;
  }
  friend bool operator!=(const CustomModifier &L, const CustomModifier &R) {
    return !(L == R);
  }
};

/// The shape of a general array (II.23.2.13). Rank is at least 1; Sizes and
/// LowerBounds give the leading dimensions that have them.
struct ArrayShape {
  uint32_t Rank = 0;
  SmallVector<uint32_t, 0> Sizes;
  SmallVector<int32_t, 0> LowerBounds;

  friend bool operator==(const ArrayShape &L, const ArrayShape &R) {
    return L.Rank == R.Rank && L.Sizes == R.Sizes &&
           L.LowerBounds == R.LowerBounds;
  }
  friend bool operator!=(const ArrayShape &L, const ArrayShape &R) {
    return !(L == R);
  }
};

struct MethodSig;

/// One type of a signature (II.23.2.12), with the custom modifiers in front
/// of it. A regular value type: copyable, ==, hash_value().
///
/// "TypeRef" values are TypeDefOrRefOrSpec tokens (table 0x01, 0x02 or 0x1B)
/// of the module the signature belongs to, or client ids that the client maps
/// to tokens with mapTypeRefs() before encoding (the MC layer has no tokens
/// yet). Only the encoders require tokens.
///
/// Representation: a TypeSig is a primitive element type held inline when it
/// has no modifiers (no allocation; the default TypeSig is void), and
/// otherwise a shared pointer to an immutable node that holds the element
/// type, the TypeRef or generic parameter index, the operands, the array
/// shape, the function-pointer signature, the modifiers and a hash of all of
/// them. Nodes are never changed after construction, so a copy shares the
/// node (one reference-count increment) and building a type from operands
/// does not copy their trees. Equality is structural (a shared node is a fast
/// path) and the hash depends only on the content, never on addresses.
class LLVM_ABI TypeSig {
public:
  /// void.
  TypeSig() = default;

  /// VOID..R8, I, U, STRING, OBJECT or TYPEDBYREF (asserts a primitive).
  static TypeSig get(ElementType Primitive);
  static TypeSig getClass(uint32_t TypeRef);
  static TypeSig getValueType(uint32_t TypeRef);
  /// void* is getPointer(TypeSig()).
  static TypeSig getPointer(TypeSig Pointee);
  static TypeSig getByRef(TypeSig Referent);
  static TypeSig getSZArray(TypeSig Element);
  static TypeSig getArray(TypeSig Element, ArrayShape Shape);
  static TypeSig getGenericInst(bool IsValueType, uint32_t GenericType,
                                ArrayRef<TypeSig> Args);
  /// VAR (a type's parameter) or MVAR (a method's parameter).
  static TypeSig getGenericParam(bool IsMethodParam, uint32_t Index);
  static TypeSig getFunctionPointer(const MethodSig &Sig);
  /// PINNED: local signatures only.
  static TypeSig getPinned(TypeSig Local);

  /// This type with M appended after the existing modifiers.
  TypeSig withModifier(CustomModifier M) const;
  /// This type with Ms appended, in order, after the existing modifiers.
  TypeSig withModifiers(ArrayRef<CustomModifier> Ms) const;

  /// The element type: a primitive, CLASS, VALUETYPE, PTR, BYREF, SZARRAY,
  /// ARRAY, GENERICINST, VAR, MVAR, FNPTR or PINNED.
  ElementType getElementType() const;
  /// CLASS, VALUETYPE; the generic type of GENERICINST.
  uint32_t getTypeRef() const;
  /// GENERICINST: ELEMENT_TYPE_VALUETYPE or ELEMENT_TYPE_CLASS, the kind that
  /// follows GENERICINST in the blob.
  ElementType getGenericInstKind() const;
  /// The pointee, element, referent, or generic arguments; empty for the
  /// other kinds.
  ArrayRef<TypeSig> getOperands() const;
  /// ARRAY only.
  const ArrayShape &getArrayShape() const;
  /// FNPTR only.
  const MethodSig &getFunctionSig() const;
  /// VAR and MVAR only.
  uint32_t getGenericParamIndex() const;
  /// True for MVAR.
  bool isMethodGenericParam() const;
  /// The custom modifiers in blob order.
  ArrayRef<CustomModifier> getModifiers() const;

  LLVM_ABI friend bool operator==(const TypeSig &L, const TypeSig &R);
  friend bool operator!=(const TypeSig &L, const TypeSig &R) {
    return !(L == R);
  }
  LLVM_ABI friend hash_code hash_value(const TypeSig &T);

private:
  struct Node;
  explicit TypeSig(std::shared_ptr<const Node> N);

  /// Null for a primitive without modifiers.
  std::shared_ptr<const Node> N;
  /// The primitive when N is null.
  ElementType Primitive = ELEMENT_TYPE_VOID;
};

/// A method signature (II.23.2.1-II.23.2.3): MethodDefSig, MethodRefSig and
/// StandAloneMethodSig.
struct MethodSig {
  /// The calling-convention kind | SIG_HASTHIS | SIG_EXPLICITTHIS |
  /// SIG_GENERIC.
  uint8_t Header = SIG_DEFAULT;
  /// Written after the header when Header has SIG_GENERIC.
  uint32_t GenericParamCount = 0;
  /// May carry modifiers, BYREF, TYPEDBYREF.
  TypeSig Return;
  std::vector<TypeSig> Params;
  /// If < Params.size(), SENTINEL precedes Params[RequiredParamCount]
  /// (vararg call sites in MemberRef and StandAloneSig signatures only). Any
  /// value >= Params.size() means no SENTINEL; == and hash_value treat them
  /// alike.
  uint32_t RequiredParamCount = UINT32_MAX;

  LLVM_ABI friend bool operator==(const MethodSig &L, const MethodSig &R);
  friend bool operator!=(const MethodSig &L, const MethodSig &R) {
    return !(L == R);
  }
};

/// A property signature (II.23.2.5).
struct PropertySig {
  bool HasThis = false;
  TypeSig Type;
  std::vector<TypeSig> Params;

  friend bool operator==(const PropertySig &L, const PropertySig &R) {
    return L.HasThis == R.HasThis && L.Type == R.Type && L.Params == R.Params;
  }
  friend bool operator!=(const PropertySig &L, const PropertySig &R) {
    return !(L == R);
  }
};

LLVM_ABI hash_code hash_value(const TypeSig &T);
LLVM_ABI hash_code hash_value(const MethodSig &S);

/// The encoders append to Out. They assert on invalid input: a TypeRef or
/// modifier type that is not a TypeDef, TypeRef or TypeSpec token, a SENTINEL
/// outside a VARARG or C signature, PINNED outside the top of a local, an
/// ARRAY of rank 0, a GENERICINST or MethodSpec without arguments, or a value
/// too large for a compressed integer.
LLVM_ABI void encodeMethodSig(const MethodSig &S,
                              SmallVectorImpl<uint8_t> &Out);
/// 0x06 + type.
LLVM_ABI void encodeFieldSig(const TypeSig &T, SmallVectorImpl<uint8_t> &Out);
/// 0x07 + count + types.
LLVM_ABI void encodeLocalSig(ArrayRef<TypeSig> Locals,
                             SmallVectorImpl<uint8_t> &Out);
LLVM_ABI void encodePropertySig(const PropertySig &S,
                                SmallVectorImpl<uint8_t> &Out);
LLVM_ABI void encodeTypeSpec(const TypeSig &T, SmallVectorImpl<uint8_t> &Out);
/// 0x0A + count + types.
LLVM_ABI void encodeMethodSpec(ArrayRef<TypeSig> Args,
                               SmallVectorImpl<uint8_t> &Out);

/// The decoders consume the whole blob. A failure is an Error whose message
/// starts with "invalid signature: " and names the cause and the offset:
/// truncation, a bad compressed integer, an unknown element type, a
/// TypeDefOrRefOrSpecEncoded value with tag 3 or row 0, trailing bytes, a
/// misplaced SENTINEL or PINNED, a wrong header, an ARRAY of rank 0 or above
/// 32 (the CLR's MAX_RANK), a GENERICINST or MethodSpec without arguments, or
/// nesting deeper than 64 levels (a type inside more than 64 PTR, BYREF,
/// SZARRAY, ARRAY, GENERICINST, FNPTR or PINNED constructors).
LLVM_ABI Expected<MethodSig> decodeMethodSig(ArrayRef<uint8_t> Blob);
LLVM_ABI Expected<TypeSig> decodeFieldSig(ArrayRef<uint8_t> Blob);
LLVM_ABI Expected<std::vector<TypeSig>> decodeLocalSig(ArrayRef<uint8_t> Blob);
LLVM_ABI Expected<PropertySig> decodePropertySig(ArrayRef<uint8_t> Blob);
LLVM_ABI Expected<TypeSig> decodeTypeSpec(ArrayRef<uint8_t> Blob);
LLVM_ABI Expected<std::vector<TypeSig>>
decodeMethodSpec(ArrayRef<uint8_t> Blob);

/// Replaces every TypeRef value by Map(value): CLASS and VALUETYPE operands,
/// the generic type of GENERICINST, modifier types, and everything inside
/// operands and function-pointer signatures (msil-ld renumbering, binding).
LLVM_ABI TypeSig mapTypeRefs(const TypeSig &T,
                             function_ref<uint32_t(uint32_t)> Map);
LLVM_ABI MethodSig mapTypeRefs(const MethodSig &S,
                               function_ref<uint32_t(uint32_t)> Map);

/// Canonical listing v1's signature text (plan 5.7.10). PrintTypeRef prints a
/// TypeRef value as listing v1's '<type>' outside signatures; the 'class ' or
/// 'valuetype ' in front of it comes from printTypeSig, and so do the
/// modifiers, which follow their type in blob order ("int32 modreq(A)
/// modopt(B)" for CMOD_REQD A, CMOD_OPT B, I4). SZARRAY prints "<t>[]"; a
/// general ARRAY prints "<t>[" and its Rank dimensions joined by ",", then
/// "]", where a dimension prints "<lo>...<hi>" (hi = lo + size - 1),
/// "<lo>...", "<size>" or nothing, and a rank-1 array with neither a size nor
/// a lower bound prints "..." ("int32[...]", "int32[0...3,-1...]",
/// "int32[4]", "int32[,]"). Without PrintTypeRef, TypeRef values print as
/// 0x<8 hex digits>.
LLVM_ABI void
printTypeSig(raw_ostream &OS, const TypeSig &T,
             function_ref<void(raw_ostream &, uint32_t TypeRef)> PrintTypeRef);
LLVM_ABI void printMethodSig(
    raw_ostream &OS, const MethodSig &S,
    function_ref<void(raw_ostream &, uint32_t TypeRef)> PrintTypeRef);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_SIGNATURE_H
