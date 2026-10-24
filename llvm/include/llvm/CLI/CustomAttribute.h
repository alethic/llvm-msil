//===- llvm/CLI/CustomAttribute.h - ECMA-335 custom attributes --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the codec of custom attribute value blobs (II.23.3):
// the prolog, the fixed arguments typed by the constructor's signature, and
// the named field and property arguments with their FieldOrPropType tags. It
// also declares the byte-array carrier, an attribute whose constructor takes
// one uint8[], which holds the link-info record (Q3) and the symbol-table
// attribute (Q25).
//
// Parsing and formatting the assembly-qualified type names of Type arguments
// belongs to the reference binder (M21); this codec only carries the strings.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_CUSTOMATTRIBUTE_H
#define LLVM_CLI_CUSTOMATTRIBUTE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/Signature.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
namespace cli {

/// One fixed argument, named-argument value or array element.
///
/// - Bool, Char, I1..U8, R4, R8: Bits. Signed integers are sign-extended to
///   64 bits by the decoder, unsigned ones, Bool and Char zero-extended; R4
///   and R8 hold the raw IEEE bits. The encoder writes the low bytes.
/// - String: Str (std::nullopt is the null string).
/// - Type: Str, the serialized (assembly-qualified) type name; std::nullopt
///   is a null Type.
/// - Enum: Bits (the value, extended per EnumUnderlying) and EnumUnderlying,
///   an integer element type (BOOLEAN, CHAR, I1..U8). Where the blob names
///   the enum (named arguments, boxed values, elements of their arrays),
///   EnumTypeName is that name; fixed arguments have none.
/// - Boxed (SERIALIZATION_TYPE_TAGGED_OBJECT, an 'object' parameter or named
///   argument): BoxedValue, which is not itself Boxed.
/// - Array (SZARRAY): Elements, std::nullopt for a null array. Named
///   arguments and boxed values write the element type into the blob: it is
///   the kind of the first element, or, for an empty or null array, given by
///   the array's own EnumUnderlying (BOOLEAN..R8, STRING, or one of
///   SERIALIZATION_TYPE_TYPE, _TAGGED_OBJECT and _ENUM cast to ElementType)
///   and, for _ENUM, EnumTypeName. The decoder sets both for every array.
struct LLVM_ABI CAValue {
  enum class Kind : uint8_t {
    Bool,
    Char,
    I1,
    U1,
    I2,
    U2,
    I4,
    U4,
    I8,
    U8,
    R4,
    R8,
    String,
    Type,
    Enum,
    Boxed,
    Array
  };

  Kind K = Kind::I4;
  /// Integers (two's complement), bool, char, R4/R8 raw bits, enum value.
  uint64_t Bits = 0;
  /// String; Type (an assembly-qualified type name); std::nullopt = null.
  std::optional<std::string> Str;
  ElementType EnumUnderlying = ELEMENT_TYPE_I4;
  /// Enum values in named arguments and boxed values.
  std::string EnumTypeName;
  /// Boxed (SERIALIZATION_TYPE_TAGGED_OBJECT).
  std::unique_ptr<CAValue> BoxedValue;
  /// Array; std::nullopt = null array.
  std::optional<std::vector<CAValue>> Elements;

  CAValue() = default;
  /// Deep copies (BoxedValue included).
  CAValue(const CAValue &Other);
  CAValue(CAValue &&Other) = default;
  CAValue &operator=(const CAValue &Other);
  CAValue &operator=(CAValue &&Other) = default;
  ~CAValue();

  /// Compares the fields that Kind uses: Bits for the scalars; Str for String
  /// and Type; Bits, EnumUnderlying and EnumTypeName for Enum; the boxed
  /// values; for an Array, the elements and, when it is empty or null, also
  /// its element type: EnumUnderlying, and EnumTypeName when EnumUnderlying
  /// is SERIALIZATION_TYPE_ENUM. A non-empty array's element-type fields are
  /// not compared, since its elements give its element type.
  LLVM_ABI friend bool operator==(const CAValue &L, const CAValue &R);
  friend bool operator!=(const CAValue &L, const CAValue &R) {
    return !(L == R);
  }
};

/// A named argument (II.23.3 NamedArg): a field (FIELD 0x53) or property
/// (PROPERTY 0x54) and its value, whose kind gives the FieldOrPropType.
struct CANamedArg {
  bool IsField = false;
  std::string Name;
  CAValue Value;

  friend bool operator==(const CANamedArg &L, const CANamedArg &R) {
    return L.IsField == R.IsField && L.Name == R.Name && L.Value == R.Value;
  }
  friend bool operator!=(const CANamedArg &L, const CANamedArg &R) {
    return !(L == R);
  }
};

/// A custom attribute's value: one fixed argument per constructor parameter,
/// then the named arguments in blob order.
struct CustomAttributeValue {
  std::vector<CAValue> FixedArgs;
  std::vector<CANamedArg> NamedArgs;

  friend bool operator==(const CustomAttributeValue &L,
                         const CustomAttributeValue &R) {
    return L.FixedArgs == R.FixedArgs && L.NamedArgs == R.NamedArgs;
  }
  friend bool operator!=(const CustomAttributeValue &L,
                         const CustomAttributeValue &R) {
    return !(L == R);
  }
};

/// Resolves the underlying type of an enum parameter (a valuetype TypeRef in
/// the constructor signature) to an integer element type.
using EnumTypeResolver = function_ref<Expected<ElementType>(uint32_t TypeRef)>;
/// Resolves the underlying type of an enum that a blob names by its
/// serialized (assembly-qualified) type name, as named arguments and boxed
/// values do (ENUM 0x55), to an integer element type.
using EnumNameResolver =
    function_ref<Expected<ElementType>(StringRef SerializedTypeName)>;

/// Encodes V for the constructor Ctor and appends the blob to Out (nothing is
/// appended on failure). Each fixed argument's kind must match its parameter
/// type: BOOLEAN..R8 the scalar kinds, STRING String, CLASS (always
/// System.Type, the only class II.23.3 allows) Type, OBJECT Boxed, VALUETYPE
/// (an enum, whose underlying type Resolve gives; without Resolve this is an
/// Error) Enum, SZARRAY of those Array. Errors start with "cannot encode
/// custom attribute: ".
LLVM_ABI Error encodeCustomAttribute(const MethodSig &Ctor,
                                     const CustomAttributeValue &V,
                                     SmallVectorImpl<uint8_t> &Out,
                                     EnumTypeResolver Resolve = {});

/// Decodes a whole custom attribute blob for the constructor Ctor. Errors
/// start with "invalid custom attribute blob: " and name the cause and the
/// offset; errors of the resolvers are returned unchanged. An enum parameter
/// without Resolve, an enum parameter whose underlying type is not an integer
/// type, and a parameter type that II.23.3 does not allow are Errors naming
/// the parameter type and the offset of that fixed argument. An ENUM-tagged
/// value (0x55 and the enum's serialized type name: a named argument, a boxed
/// value, or the element type of their arrays) names its enum only by that
/// name, and ResolveName gives its underlying type, which must be an integer
/// type (otherwise an Error). Without ResolveName such a value is an Error
/// naming the enum type and the offset: the decoder never guesses the width of
/// an enum value. Callers that decode blobs with such values (the reference
/// binder, M21) pass both resolvers.
LLVM_ABI Expected<CustomAttributeValue>
decodeCustomAttribute(const MethodSig &Ctor, ArrayRef<uint8_t> Blob,
                      EnumTypeResolver Resolve = {},
                      EnumNameResolver ResolveName = {});

/// The carrier of the link-info record (Q3) and of the symbol-table attribute
/// (Q25): a constructor taking uint8[]. Appends 01 00, the u32 element count,
/// the bytes, and 00 00 (no named arguments).
LLVM_ABI void encodeByteArrayAttribute(ArrayRef<uint8_t> Payload,
                                       SmallVectorImpl<uint8_t> &Out);

/// The payload of a byte-array carrier blob (a view into Blob). A null array,
/// a short blob, a wrong prolog, named arguments and trailing bytes are
/// Errors ("invalid custom attribute blob: ...").
LLVM_ABI Expected<ArrayRef<uint8_t>>
decodeByteArrayAttribute(ArrayRef<uint8_t> Blob);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_CUSTOMATTRIBUTE_H
