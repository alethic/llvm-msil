//===- CustomAttributeTest.cpp - ECMA-335 custom attribute blobs ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/CustomAttribute.h (plan 5.7.4, ECMA-335 II.23.3): the E1
// AssemblyMetadataAttribute blob of metadata-writer.md 4.6, the uint8[]
// carrier of the link-info record and its errors, and blobs that csc wrote:
// enum, System.Type, boxed, array and named arguments. The csc blobs were
// compiled in scratch (csc from the .NET 10.0.402 SDK against the net8.0
// reference pack) and read with System.Reflection.Metadata; each test quotes
// the C# attribute it comes from. In that assembly the enums are TypeDefs
// NS.E8 (row 2, underlying uint8), NS.E32 (row 3, int32) and NS.E64 (row 4,
// int64), and System.Type is TypeRef row 0x0E.
//
// Every blob is decoded, compared field by field and re-encoded byte for
// byte. The constructor signature fixes each fixed argument's type; an enum
// parameter gets its underlying type from the EnumTypeResolver (by TypeRef).
// An enum in a boxed value or a named argument (ENUM 0x55) is spelled only by
// its serialized type name, so its width comes only from the EnumNameResolver
// (section 12 '5.7.4 amended (enums named only by name)'): those blobs are
// decoded with one, and without one they must be an Error, never a guess.
// Encoder errors and the element type of empty and null arrays follow section
// 12's '5.7.12 amended' and '5.7.4 amended (CAValue arrays)' rows.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/CustomAttribute.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/Signature.h"
#include "llvm/Support/Error.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

using ByteVec = std::vector<uint8_t>;
using Kind = CAValue::Kind;

ByteVec toVec(ArrayRef<uint8_t> A) { return ByteVec(A.begin(), A.end()); }

// "01 00 0B ..." -> bytes.
ByteVec fromHex(StringRef Hex) {
  ByteVec R;
  SmallVector<StringRef, 64> Parts;
  Hex.split(Parts, ' ', -1, /*KeepEmpty=*/false);
  for (StringRef P : Parts) {
    unsigned V = 0;
    EXPECT_FALSE(P.getAsInteger(16, V)) << P;
    R.push_back(static_cast<uint8_t>(V));
  }
  return R;
}

constexpr uint32_t TR(uint32_t Row) { return 0x01000000 | Row; }
constexpr uint32_t TD(uint32_t Row) { return 0x02000000 | Row; }

//===----------------------------------------------------------------------===//
// Values
//===----------------------------------------------------------------------===//

CAValue scalar(Kind K, uint64_t Bits) {
  CAValue V;
  V.K = K;
  V.Bits = Bits;
  return V;
}
CAValue i4(int32_t X) { return scalar(Kind::I4, uint64_t(int64_t(X))); }
CAValue u1(uint8_t X) { return scalar(Kind::U1, X); }
CAValue str(std::optional<std::string> S) {
  CAValue V;
  V.K = Kind::String;
  V.Str = std::move(S);
  return V;
}
CAValue type(std::optional<std::string> S) {
  CAValue V;
  V.K = Kind::Type;
  V.Str = std::move(S);
  return V;
}
CAValue enumValue(ElementType Underlying, uint64_t Bits,
                  std::string TypeName = "") {
  CAValue V;
  V.K = Kind::Enum;
  V.Bits = Bits;
  V.EnumUnderlying = Underlying;
  V.EnumTypeName = std::move(TypeName);
  return V;
}
CAValue boxed(CAValue Inner) {
  CAValue V;
  V.K = Kind::Boxed;
  V.BoxedValue = std::make_unique<CAValue>(std::move(Inner));
  return V;
}
CAValue array(std::optional<std::vector<CAValue>> Elements) {
  CAValue V;
  V.K = Kind::Array;
  V.Elements = std::move(Elements);
  return V;
}
CANamedArg named(bool IsField, std::string Name, CAValue Value) {
  CANamedArg A;
  A.IsField = IsField;
  A.Name = std::move(Name);
  A.Value = std::move(Value);
  return A;
}
CustomAttributeValue attr(std::vector<CAValue> Fixed,
                          std::vector<CANamedArg> Named = {}) {
  CustomAttributeValue V;
  V.FixedArgs = std::move(Fixed);
  V.NamedArgs = std::move(Named);
  return V;
}

unsigned bitWidth(Kind K, ElementType EnumUnderlying) {
  switch (K) {
  case Kind::Bool:
  case Kind::I1:
  case Kind::U1:
    return 8;
  case Kind::Char:
  case Kind::I2:
  case Kind::U2:
    return 16;
  case Kind::I4:
  case Kind::U4:
  case Kind::R4:
    return 32;
  case Kind::I8:
  case Kind::U8:
  case Kind::R8:
    return 64;
  case Kind::Enum:
    switch (EnumUnderlying) {
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
    default:
      return 64;
    }
  default:
    return 0;
  }
}

// Compares the fields that the kind uses. Integers compare in their own width,
// so a decoder may keep a negative int8 sign- or zero-extended in Bits.
void expectSame(const CAValue &Got, const CAValue &Want) {
  ASSERT_EQ(static_cast<int>(Got.K), static_cast<int>(Want.K));
  if (unsigned W = bitWidth(Want.K, Want.EnumUnderlying)) {
    uint64_t Mask = W == 64 ? ~uint64_t(0) : (uint64_t(1) << W) - 1;
    EXPECT_EQ(Got.Bits & Mask, Want.Bits & Mask);
  }
  switch (Want.K) {
  case Kind::Enum:
    EXPECT_EQ(Got.EnumUnderlying, Want.EnumUnderlying);
    if (!Want.EnumTypeName.empty())
      EXPECT_EQ(Got.EnumTypeName, Want.EnumTypeName);
    break;
  case Kind::String:
  case Kind::Type:
    EXPECT_EQ(Got.Str, Want.Str);
    break;
  case Kind::Boxed:
    ASSERT_TRUE(Got.BoxedValue != nullptr);
    expectSame(*Got.BoxedValue, *Want.BoxedValue);
    break;
  case Kind::Array:
    ASSERT_EQ(Got.Elements.has_value(), Want.Elements.has_value());
    if (Want.Elements) {
      ASSERT_EQ(Got.Elements->size(), Want.Elements->size());
      for (size_t I = 0; I != Want.Elements->size(); ++I) {
        SCOPED_TRACE("element " + std::to_string(I));
        expectSame((*Got.Elements)[I], (*Want.Elements)[I]);
      }
    }
    break;
  default:
    break;
  }
}

void expectSame(const CustomAttributeValue &Got,
                const CustomAttributeValue &Want) {
  ASSERT_EQ(Got.FixedArgs.size(), Want.FixedArgs.size());
  for (size_t I = 0; I != Want.FixedArgs.size(); ++I) {
    SCOPED_TRACE("fixed argument " + std::to_string(I));
    expectSame(Got.FixedArgs[I], Want.FixedArgs[I]);
  }
  ASSERT_EQ(Got.NamedArgs.size(), Want.NamedArgs.size());
  for (size_t I = 0; I != Want.NamedArgs.size(); ++I) {
    SCOPED_TRACE("named argument " + Want.NamedArgs[I].Name);
    EXPECT_EQ(Got.NamedArgs[I].IsField, Want.NamedArgs[I].IsField);
    EXPECT_EQ(Got.NamedArgs[I].Name, Want.NamedArgs[I].Name);
    expectSame(Got.NamedArgs[I].Value, Want.NamedArgs[I].Value);
  }
}

//===----------------------------------------------------------------------===//
// Constructor signatures (all 'instance void(...)')
//===----------------------------------------------------------------------===//

const TypeSig Str = TypeSig::get(ELEMENT_TYPE_STRING);
const TypeSig Obj = TypeSig::get(ELEMENT_TYPE_OBJECT);
const TypeSig I4 = TypeSig::get(ELEMENT_TYPE_I4);
const TypeSig U1 = TypeSig::get(ELEMENT_TYPE_U1);
const TypeSig E8 = TypeSig::getValueType(TD(2));
const TypeSig E32 = TypeSig::getValueType(TD(3));
const TypeSig E64 = TypeSig::getValueType(TD(4));
const TypeSig SystemType = TypeSig::getClass(TR(0x0E));

MethodSig ctor(std::vector<TypeSig> Params) {
  MethodSig S;
  S.Header = SIG_HASTHIS;
  S.Params = std::move(Params);
  return S;
}

Expected<ElementType> resolveEnum(uint32_t TypeRef) {
  switch (TypeRef) {
  case TD(2):
    return ELEMENT_TYPE_U1;
  case TD(3):
    return ELEMENT_TYPE_I4;
  case TD(4):
    return ELEMENT_TYPE_I8;
  default:
    return createStringError(inconvertibleErrorCode(), "not an enum: 0x%x",
                             TypeRef);
  }
}

// The enums of the csc assembly by their serialized names, as a binder that
// reads the referenced assembly resolves them.
Expected<ElementType> resolveEnumName(StringRef Name) {
  if (Name == "NS.E8")
    return ELEMENT_TYPE_U1;
  if (Name == "NS.E32")
    return ELEMENT_TYPE_I4;
  if (Name == "NS.E64")
    return ELEMENT_TYPE_I8;
  return createStringError(inconvertibleErrorCode(), "not an enum: '%s'",
                           Name.str().c_str());
}

// Encodes Want and compares with Blob; decodes Blob and compares with Want;
// re-encodes the decoded value and compares with Blob.
void roundTrip(const MethodSig &Ctor, const ByteVec &Blob,
               const CustomAttributeValue &Want, EnumTypeResolver Resolve = {},
               EnumNameResolver ResolveName = {}) {
  SmallVector<uint8_t, 64> Out;
  EXPECT_THAT_ERROR(encodeCustomAttribute(Ctor, Want, Out, Resolve),
                    Succeeded());
  EXPECT_EQ(toVec(Out), Blob);
  Expected<CustomAttributeValue> D =
      decodeCustomAttribute(Ctor, Blob, Resolve, ResolveName);
  ASSERT_THAT_EXPECTED(D, Succeeded());
  expectSame(*D, Want);
  SmallVector<uint8_t, 64> Again;
  EXPECT_THAT_ERROR(encodeCustomAttribute(Ctor, *D, Again, Resolve),
                    Succeeded());
  EXPECT_EQ(toVec(Again), Blob);
}

void encodesTo(const MethodSig &Ctor, const CustomAttributeValue &V,
               const ByteVec &Blob) {
  SmallVector<uint8_t, 64> Out;
  EXPECT_THAT_ERROR(encodeCustomAttribute(Ctor, V, Out), Succeeded());
  EXPECT_EQ(toVec(Out), Blob);
}

void expectInvalidBlob(const MethodSig &Ctor, const ByteVec &Blob,
                       EnumTypeResolver Resolve = {},
                       EnumNameResolver ResolveName = {}) {
  EXPECT_THAT_EXPECTED(
      decodeCustomAttribute(Ctor, Blob, Resolve, ResolveName),
      FailedWithMessage(testing::StartsWith("invalid custom attribute blob: ")))
      << "blob of " << Blob.size() << " bytes";
}

// The message of a decode error, which starts with the decoder's prefix;
// lowercased, so that callers match words of the cause in any case.
std::string decodeErrorMessage(const MethodSig &Ctor, const ByteVec &Blob,
                               EnumTypeResolver Resolve = {},
                               EnumNameResolver ResolveName = {}) {
  Expected<CustomAttributeValue> D =
      decodeCustomAttribute(Ctor, Blob, Resolve, ResolveName);
  if (D) {
    ADD_FAILURE() << "the blob decoded without an error";
    return "";
  }
  std::string Msg = toString(D.takeError());
  EXPECT_THAT(Msg, testing::StartsWith("invalid custom attribute blob: "));
  return StringRef(Msg).lower();
}

// Without an EnumNameResolver an ENUM-tagged value is an Error that names the
// enum type and the offset; the decoder never guesses a width.
void expectNeedsEnumName(const MethodSig &Ctor, const ByteVec &Blob,
                         StringRef EnumName) {
  std::string Msg = decodeErrorMessage(Ctor, Blob);
  EXPECT_THAT(Msg, testing::HasSubstr(EnumName.lower()));
  EXPECT_THAT(Msg, testing::HasSubstr("offset 0x"));
}

// encodeCustomAttribute's errors start with their prefix (section 12 '5.7.12
// amended') and append nothing to Out. Cause, when given, is a word of the
// cause, matched in any case.
void expectEncodeError(const MethodSig &Ctor, const CustomAttributeValue &V,
                       StringRef Cause = "", EnumTypeResolver Resolve = {}) {
  SmallVector<uint8_t, 16> Out = {0xAB, 0xCD};
  Error E = encodeCustomAttribute(Ctor, V, Out, Resolve);
  if (!E) {
    ADD_FAILURE() << "the value encoded without an error";
    return;
  }
  std::string Msg = toString(std::move(E));
  EXPECT_THAT(Msg, testing::StartsWith("cannot encode custom attribute: "));
  if (!Cause.empty())
    EXPECT_THAT(StringRef(Msg).lower(), testing::HasSubstr(Cause.lower()));
  EXPECT_EQ(toVec(Out), (ByteVec{0xAB, 0xCD})) << "nothing is appended";
}

const char *const SysString =
    "System.String, System.Runtime, Version=8.0.0.0, "
    "Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a";
const char *const SysInt32 = "System.Int32, System.Runtime, Version=8.0.0.0, "
                             "Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a";

//===----------------------------------------------------------------------===//
// E1 and the byte-array carrier
//===----------------------------------------------------------------------===//

// metadata-writer.md 4.6 / hello-ilasm.mdump.txt: the assembly's
// AssemblyMetadataAttribute("msil.triple", "x86_64-pc-windows-msvc-msil").
TEST(CLICustomAttributeTest, E1AssemblyMetadataAttribute) {
  ByteVec Blob = fromHex(
      "01 00 0B 6D 73 69 6C 2E 74 72 69 70 6C 65 1B 78 38 36 5F 36 34 2D 70 "
      "63 2D 77 69 6E 64 6F 77 73 2D 6D 73 76 63 2D 6D 73 69 6C 00 00");
  roundTrip(ctor({Str, Str}), Blob,
            attr({str("msil.triple"), str("x86_64-pc-windows-msvc-msil")}));
}

TEST(CLICustomAttributeTest, NoArguments) {
  roundTrip(ctor({}), {0x01, 0x00, 0x00, 0x00}, attr({}));
}

// csc: [T(new byte[] { 1, 2, 3 })] and [T(new byte[0])] on a constructor
// taking byte[]: 01 00, u32 count, the bytes, NumNamed 00 00.
TEST(CLICustomAttributeTest, ByteArrayCarrier) {
  ByteVec Three = fromHex("01 00 03 00 00 00 01 02 03 00 00");
  ByteVec Empty = fromHex("01 00 00 00 00 00 00 00");
  SmallVector<uint8_t, 16> Out;
  encodeByteArrayAttribute({1, 2, 3}, Out);
  EXPECT_EQ(toVec(Out), Three);
  Out.clear();
  encodeByteArrayAttribute({}, Out);
  EXPECT_EQ(toVec(Out), Empty);

  Expected<ArrayRef<uint8_t>> P = decodeByteArrayAttribute(Three);
  ASSERT_THAT_EXPECTED(P, Succeeded());
  EXPECT_EQ(toVec(*P), (ByteVec{1, 2, 3}));
  Expected<ArrayRef<uint8_t>> P0 = decodeByteArrayAttribute(Empty);
  ASSERT_THAT_EXPECTED(P0, Succeeded());
  EXPECT_TRUE(P0->empty());

  // The same blobs through the general codec.
  MethodSig ByteArrayCtor = ctor({TypeSig::getSZArray(U1)});
  roundTrip(ByteArrayCtor, Three,
            attr({array(std::vector<CAValue>{u1(1), u1(2), u1(3)})}));
  roundTrip(ByteArrayCtor, Empty, attr({array(std::vector<CAValue>{})}));

  // A payload larger than 64 KiB.
  ByteVec Big(70000);
  for (size_t I = 0; I != Big.size(); ++I)
    Big[I] = static_cast<uint8_t>(I * 7);
  Out.clear();
  encodeByteArrayAttribute(Big, Out);
  ASSERT_EQ(Out.size(), Big.size() + 8);
  EXPECT_EQ(toVec(ArrayRef<uint8_t>(Out).take_front(6)),
            (ByteVec{0x01, 0x00, 0x70, 0x11, 0x01, 0x00}));
  Expected<ArrayRef<uint8_t>> PB = decodeByteArrayAttribute(Out);
  ASSERT_THAT_EXPECTED(PB, Succeeded());
  EXPECT_EQ(toVec(*PB), Big);
}

TEST(CLICustomAttributeTest, ByteArrayCarrierErrors) {
  const char *Bad[] = {
      "",                              // empty
      "01 00 00",                      // no room for the count
      "00 01 00 00 00 00 00 00",       // bad prolog
      "01 00 FF FF FF FF 00 00",       // null array
      "01 00 05 00 00 00 01 02 00 00", // shorter than its count
      "01 00 FE FF FF FF",             // a count far past the end
      "01 00 01 00 00 00 AA",          // no NumNamed
      "01 00 01 00 00 00 AA 00 00 BB", // trailing bytes
      "01 00 01 00 00 00 AA 01 00 53 08 01 46 07 00 00 00", // a named argument
  };
  for (const char *Hex : Bad) {
    SCOPED_TRACE(Hex);
    ByteVec Blob = fromHex(Hex);
    EXPECT_THAT_EXPECTED(decodeByteArrayAttribute(Blob),
                         FailedWithMessage(testing::StartsWith(
                             "invalid custom attribute blob: ")));
  }
}

//===----------------------------------------------------------------------===//
// csc-built blobs
//===----------------------------------------------------------------------===//

// [T(E32.X)] and [T(E8.B, E64.Big)]: enum parameters, resolved through the
// resolver with the constructor signature's valuetype TypeDefs.
TEST(CLICustomAttributeTest, EnumFixedArguments) {
  roundTrip(ctor({E32}), fromHex("01 00 05 00 00 00 00 00"),
            attr({enumValue(ELEMENT_TYPE_I4, 5)}), resolveEnum);
  roundTrip(ctor({E8, E64}), fromHex("01 00 C8 89 67 45 23 01 00 00 00 00 00"),
            attr({enumValue(ELEMENT_TYPE_U1, 200),
                  enumValue(ELEMENT_TYPE_I8, 0x123456789)}),
            resolveEnum);
  // [T(new E32[] { E32.X, E32.Neg })]
  roundTrip(ctor({TypeSig::getSZArray(E32)}),
            fromHex("01 00 02 00 00 00 05 00 00 00 FE FF FF FF 00 00"),
            attr({array(std::vector<CAValue>{
                enumValue(ELEMENT_TYPE_I4, 5),
                enumValue(ELEMENT_TYPE_I4, uint64_t(int64_t(-2)))})}),
            resolveEnum);

  // The resolver sees the TypeRef of the constructor's parameter.
  std::vector<uint32_t> Asked;
  auto Recording = [&](uint32_t T) -> Expected<ElementType> {
    Asked.push_back(T);
    return resolveEnum(T);
  };
  Expected<CustomAttributeValue> D = decodeCustomAttribute(
      ctor({E8, E64}), fromHex("01 00 C8 89 67 45 23 01 00 00 00 00 00"),
      Recording);
  ASSERT_THAT_EXPECTED(D, Succeeded());
  ASSERT_FALSE(Asked.empty());
  for (uint32_t T : Asked)
    EXPECT_TRUE(T == TD(2) || T == TD(4)) << utohexstr(T);
}

TEST(CLICustomAttributeTest, EnumParameterNeedsAResolver) {
  ByteVec Blob = fromHex("01 00 05 00 00 00 00 00");
  // The error names the offset (the argument at 0x2).
  EXPECT_THAT(decodeErrorMessage(ctor({E32}), Blob),
              testing::HasSubstr("offset 0x"));
  // A name resolver does not stand in for the TypeRef resolver.
  expectInvalidBlob(ctor({E32}), Blob, {}, resolveEnumName);
  expectEncodeError(ctor({E32}), attr({enumValue(ELEMENT_TYPE_I4, 5)}));
  // A resolver's error fails the decode.
  auto Refuse = [](uint32_t) -> Expected<ElementType> {
    return createStringError(inconvertibleErrorCode(), "unknown enum");
  };
  EXPECT_THAT_EXPECTED(decodeCustomAttribute(ctor({E32}), Blob, Refuse),
                       Failed());
}

// [T(typeof(string))], [T(typeof(TAttribute))], [T((Type)null)]: a CLASS
// parameter is always System.Type (II.23.3), whatever TypeRef it names.
TEST(CLICustomAttributeTest, TypeArguments) {
  ByteVec StringBlob = fromHex(
      "01 00 60 53 79 73 74 65 6D 2E 53 74 72 69 6E 67 2C 20 53 79 73 74 65 6D "
      "2E 52 75 6E 74 69 6D 65 2C 20 56 65 72 73 69 6F 6E 3D 38 2E 30 2E 30 2E "
      "30 2C 20 43 75 6C 74 75 72 65 3D 6E 65 75 74 72 61 6C 2C 20 50 75 62 6C "
      "69 63 4B 65 79 54 6F 6B 65 6E 3D 62 30 33 66 35 66 37 66 31 31 64 35 30 "
      "61 33 61 00 00");
  roundTrip(ctor({SystemType}), StringBlob, attr({type(SysString)}));
  roundTrip(ctor({SystemType}),
            fromHex("01 00 0D 4E 53 2E 54 41 74 74 72 69 62 75 74 65 00 00"),
            attr({type("NS.TAttribute")}));
  roundTrip(ctor({SystemType}), fromHex("01 00 FF 00 00"),
            attr({type(std::nullopt)}));
  roundTrip(ctor({TypeSig::getClass(TR(1))}),
            fromHex("01 00 0D 4E 53 2E 54 41 74 74 72 69 62 75 74 65 00 00"),
            attr({type("NS.TAttribute")}));
  // [T(new Type[] { typeof(int), null })]
  ByteVec Types = {0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x5F};
  Types.insert(Types.end(), SysInt32, SysInt32 + StringRef(SysInt32).size());
  Types.insert(Types.end(), {0xFF, 0x00, 0x00});
  roundTrip(
      ctor({TypeSig::getSZArray(SystemType)}), Types,
      attr({array(std::vector<CAValue>{type(SysInt32), type(std::nullopt)})}));
}

// object parameters: a type tag, then the value (II.23.3 FieldOrPropType).
TEST(CLICustomAttributeTest, BoxedArguments) {
  MethodSig ObjCtor = ctor({Obj});
  // [T((object)42)]
  roundTrip(ObjCtor, fromHex("01 00 08 2A 00 00 00 00 00"),
            attr({boxed(i4(42))}));
  // [T((object)"str")]
  roundTrip(ObjCtor, fromHex("01 00 0E 03 73 74 72 00 00"),
            attr({boxed(str("str"))}));
  // [T((object)null)]
  roundTrip(ObjCtor, fromHex("01 00 0E FF 00 00"),
            attr({boxed(str(std::nullopt))}));
  // [T((object)typeof(int))]
  ByteVec TypeBlob = {0x01, 0x00, 0x50, 0x5F};
  TypeBlob.insert(TypeBlob.end(), SysInt32,
                  SysInt32 + StringRef(SysInt32).size());
  TypeBlob.insert(TypeBlob.end(), {0x00, 0x00});
  roundTrip(ObjCtor, TypeBlob, attr({boxed(type(SysInt32))}));
  // [T((object)new int[] { 3 })]
  roundTrip(ObjCtor, fromHex("01 00 1D 08 01 00 00 00 03 00 00 00 00 00"),
            attr({boxed(array(std::vector<CAValue>{i4(3)}))}));
  // [T((object)E8.B)] and [T((object)E64.Big)]: enums by type name.
  ByteVec BoxedE8 = fromHex("01 00 55 05 4E 53 2E 45 38 C8 00 00");
  roundTrip(ObjCtor, BoxedE8,
            attr({boxed(enumValue(ELEMENT_TYPE_U1, 200, "NS.E8"))}), {},
            resolveEnumName);
  expectNeedsEnumName(ObjCtor, BoxedE8, "NS.E8");
  ByteVec BoxedE64 =
      fromHex("01 00 55 06 4E 53 2E 45 36 34 89 67 45 23 01 00 00 00 00 00");
  roundTrip(ObjCtor, BoxedE64,
            attr({boxed(enumValue(ELEMENT_TYPE_I8, 0x123456789, "NS.E64"))}),
            {}, resolveEnumName);
  expectNeedsEnumName(ObjCtor, BoxedE64, "NS.E64");
}

TEST(CLICustomAttributeTest, ArrayArguments) {
  MethodSig IntArrayCtor = ctor({TypeSig::getSZArray(I4)});
  // [T(new int[] { 1, 2 })]
  roundTrip(IntArrayCtor,
            fromHex("01 00 02 00 00 00 01 00 00 00 02 00 00 00 00 00"),
            attr({array(std::vector<CAValue>{i4(1), i4(2)})}));
  // [T((int[])null)]
  roundTrip(IntArrayCtor, fromHex("01 00 FF FF FF FF 00 00"),
            attr({array(std::nullopt)}));
  // [T(new string[] { "a", null, "" })]
  roundTrip(ctor({TypeSig::getSZArray(Str)}),
            fromHex("01 00 03 00 00 00 01 61 FF 00 00 00"),
            attr({array(
                std::vector<CAValue>{str("a"), str(std::nullopt), str("")})}));
  // [T(new object[] { 1, "s", null, typeof(int), E32.X })]: tagged elements.
  ByteVec ObjArray =
      fromHex("01 00 05 00 00 00 08 01 00 00 00 0E 01 73 0E FF 50 5F");
  ObjArray.insert(ObjArray.end(), SysInt32,
                  SysInt32 + StringRef(SysInt32).size());
  ByteVec Tail = fromHex("55 06 4E 53 2E 45 33 32 05 00 00 00 00 00");
  ObjArray.insert(ObjArray.end(), Tail.begin(), Tail.end());
  roundTrip(ctor({TypeSig::getSZArray(Obj)}), ObjArray,
            attr({array(std::vector<CAValue>{
                boxed(i4(1)), boxed(str("s")), boxed(str(std::nullopt)),
                boxed(type(SysInt32)),
                boxed(enumValue(ELEMENT_TYPE_I4, 5, "NS.E32"))})}),
            {}, resolveEnumName);
  expectNeedsEnumName(ctor({TypeSig::getSZArray(Obj)}), ObjArray, "NS.E32");
  // The same without the enum element decodes without a name resolver.
  ByteVec ObjArray4 =
      fromHex("01 00 04 00 00 00 08 01 00 00 00 0E 01 73 0E FF 50 5F");
  ObjArray4.insert(ObjArray4.end(), SysInt32,
                   SysInt32 + StringRef(SysInt32).size());
  ObjArray4.insert(ObjArray4.end(), {0x00, 0x00});
  roundTrip(ctor({TypeSig::getSZArray(Obj)}), ObjArray4,
            attr({array(std::vector<CAValue>{boxed(i4(1)), boxed(str("s")),
                                             boxed(str(std::nullopt)),
                                             boxed(type(SysInt32))})}));
}

// [T(true, 'A', (sbyte)-1, (byte)255, (short)-2, (ushort)65535, -3,
//    4000000000u, -5L, 18000000000000000000ul, 1.5f, -2.25, "été")]
TEST(CLICustomAttributeTest, EveryPrimitiveType) {
  MethodSig Ctor =
      ctor({TypeSig::get(ELEMENT_TYPE_BOOLEAN), TypeSig::get(ELEMENT_TYPE_CHAR),
            TypeSig::get(ELEMENT_TYPE_I1), U1, TypeSig::get(ELEMENT_TYPE_I2),
            TypeSig::get(ELEMENT_TYPE_U2), I4, TypeSig::get(ELEMENT_TYPE_U4),
            TypeSig::get(ELEMENT_TYPE_I8), TypeSig::get(ELEMENT_TYPE_U8),
            TypeSig::get(ELEMENT_TYPE_R4), TypeSig::get(ELEMENT_TYPE_R8), Str});
  ByteVec Blob = fromHex(
      "01 00 01 41 00 FF FF FE FF FF FF FD FF FF FF 00 28 6B EE FB FF FF FF FF "
      "FF FF FF 00 00 08 C5 A1 D8 CC F9 00 00 C0 3F 00 00 00 00 00 00 02 C0 05 "
      "C3 A9 74 C3 A9 00 00");
  roundTrip(
      Ctor, Blob,
      attr({scalar(Kind::Bool, 1), scalar(Kind::Char, 'A'),
            scalar(Kind::I1, uint64_t(int64_t(-1))), u1(255),
            scalar(Kind::I2, uint64_t(int64_t(-2))), scalar(Kind::U2, 65535),
            i4(-3), scalar(Kind::U4, 4000000000u),
            scalar(Kind::I8, uint64_t(int64_t(-5))),
            scalar(Kind::U8, 18000000000000000000ull),
            scalar(Kind::R4, 0x3FC00000),            // 1.5f
            scalar(Kind::R8, 0xC002000000000000ull), // -2.25
            str(std::string("\xC3\xA9t\xC3\xA9"))}));
  // [T((string)null)]
  roundTrip(ctor({Str}), fromHex("01 00 FF 00 00"), attr({str(std::nullopt)}));
}

// [T(Field = 7, Prop = "p")]: NumNamed, then FIELD (0x53) or PROPERTY (0x54),
// the type, the name and the value.
TEST(CLICustomAttributeTest, NamedArguments) {
  roundTrip(
      ctor({}),
      fromHex("01 00 02 00 53 08 05 46 69 65 6C 64 07 00 00 00 54 0E 04 "
              "50 72 6F 70 01 70"),
      attr({}, {named(true, "Field", i4(7)), named(false, "Prop", str("p"))}));
  // [T(ObjField = 3.5)]: a boxed named field (the first half of csc's
  // [T(ObjField = 3.5, Enum32Prop = E32.Neg)]).
  roundTrip(ctor({}),
            fromHex("01 00 01 00 53 51 08 4F 62 6A 46 69 65 6C 64 0D 00 00 00 "
                    "00 00 00 0C 40"),
            attr({}, {named(true, "ObjField",
                            boxed(scalar(Kind::R8, 0x400C000000000000ull)))}));
}

// Named enum, Type and array arguments; enums carry their type name.
TEST(CLICustomAttributeTest, NamedEnumTypeAndArrayArguments) {
  MethodSig Ctor = ctor({});
  // [T(EnumField = E8.B, TypeProp = typeof(string), ArrProp = new int[] {9})]
  ByteVec Blob = fromHex("01 00 03 00 53 55 05 4E 53 2E 45 38 09 45 6E 75 6D "
                         "46 69 65 6C 64 C8 54 50 08 54 79 70 65 50 72 6F 70 "
                         "60");
  Blob.insert(Blob.end(), SysString, SysString + StringRef(SysString).size());
  ByteVec Tail = fromHex("54 1D 08 07 41 72 72 50 72 6F 70 01 00 00 00 09 00 "
                         "00 00");
  Blob.insert(Blob.end(), Tail.begin(), Tail.end());
  roundTrip(
      Ctor, Blob,
      attr({},
           {named(true, "EnumField", enumValue(ELEMENT_TYPE_U1, 200, "NS.E8")),
            named(false, "TypeProp", type(SysString)),
            named(false, "ArrProp", array(std::vector<CAValue>{i4(9)}))}),
      {}, resolveEnumName);
  expectNeedsEnumName(Ctor, Blob, "NS.E8");
  // The decoded int[] carries its element type.
  Expected<CustomAttributeValue> D =
      decodeCustomAttribute(Ctor, Blob, {}, resolveEnumName);
  ASSERT_THAT_EXPECTED(D, Succeeded());
  ASSERT_EQ(D->NamedArgs.size(), 3u);
  EXPECT_EQ(D->NamedArgs[2].Value.EnumUnderlying, ELEMENT_TYPE_I4);

  // [T(ObjField = 3.5, Enum32Prop = E32.Neg)]
  ByteVec Enum32Prop =
      fromHex("01 00 02 00 53 51 08 4F 62 6A 46 69 65 6C 64 0D 00 00 00 00 00 "
              "00 0C 40 54 55 06 4E 53 2E 45 33 32 0A 45 6E 75 6D 33 32 50 72 "
              "6F 70 FE FF FF FF");
  roundTrip(Ctor, Enum32Prop,
            attr({}, {named(true, "ObjField",
                            boxed(scalar(Kind::R8, 0x400C000000000000ull))),
                      named(false, "Enum32Prop",
                            enumValue(ELEMENT_TYPE_I4, uint64_t(int64_t(-2)),
                                      "NS.E32"))}),
            {}, resolveEnumName);
  expectNeedsEnumName(Ctor, Enum32Prop, "NS.E32");

  // [T(ObjField = E32.X)]
  ByteVec BoxedField = fromHex("01 00 01 00 53 51 08 4F 62 6A 46 69 65 6C 64 "
                               "55 06 4E 53 2E 45 33 32 05 00 00 00");
  roundTrip(Ctor, BoxedField,
            attr({}, {named(true, "ObjField",
                            boxed(enumValue(ELEMENT_TYPE_I4, 5, "NS.E32")))}),
            {}, resolveEnumName);
  expectNeedsEnumName(Ctor, BoxedField, "NS.E32");

  // [T(ObjField = new E8[] { E8.A })]: the enum is the array's element type.
  ByteVec BoxedArray = fromHex("01 00 01 00 53 51 08 4F 62 6A 46 69 65 6C 64 "
                               "1D 55 05 4E 53 2E 45 38 01 00 00 00 01");
  roundTrip(Ctor, BoxedArray,
            attr({}, {named(true, "ObjField",
                            boxed(array(std::vector<CAValue>{
                                enumValue(ELEMENT_TYPE_U1, 1, "NS.E8")})))}),
            {}, resolveEnumName);
  expectNeedsEnumName(Ctor, BoxedArray, "NS.E8");
  Expected<CustomAttributeValue> DA =
      decodeCustomAttribute(Ctor, BoxedArray, {}, resolveEnumName);
  ASSERT_THAT_EXPECTED(DA, Succeeded());
  ASSERT_EQ(DA->NamedArgs.size(), 1u);
  ASSERT_TRUE(DA->NamedArgs[0].Value.BoxedValue != nullptr);
  const CAValue &Arr = *DA->NamedArgs[0].Value.BoxedValue;
  EXPECT_EQ(Arr.EnumUnderlying,
            static_cast<ElementType>(SERIALIZATION_TYPE_ENUM));
  EXPECT_EQ(Arr.EnumTypeName, "NS.E8");
}

// The verifier's csc blob (scratch M16/verify-ca-enumguess): for 'enum E :
// byte' and [A(P = new E[] { (E)1, (E)2, (E)3, (E)4 },
// Q = "abcdefgT\b\u0001R\0\0\0\0")] csc wrote 01 00 02 00 | 54 1D 55 01 'E'
// 01 'P' 04 00 00 00 01 02 03 04 | 54 0E 01 'Q' 0F 'abcdefgT' 08 01 'R'
// 00 00 00 00. Read with int32 elements, the same bytes are also a whole blob
// (four int32s, then a property 'R' = 0), so only a resolver can tell.
TEST(CLICustomAttributeTest, EnumWidthComesOnlyFromTheNameResolver) {
  MethodSig Ctor = ctor({});
  ByteVec Blob = fromHex("01 00 02 00 54 1D 55 01 45 01 50 04 00 00 00 01 02 "
                         "03 04 54 0E 01 51 0F 61 62 63 64 65 66 67 54 08 01 "
                         "52 00 00 00 00");
  EXPECT_THAT(decodeErrorMessage(Ctor, Blob), testing::HasSubstr("offset 0x"));

  std::vector<std::string> Asked;
  auto ByteEnum = [&](StringRef Name) -> Expected<ElementType> {
    Asked.push_back(Name.str());
    if (Name == "E")
      return ELEMENT_TYPE_U1;
    return createStringError(inconvertibleErrorCode(), "not an enum");
  };
  std::vector<CAValue> Elements;
  for (uint64_t V : {1, 2, 3, 4})
    Elements.push_back(enumValue(ELEMENT_TYPE_U1, V, "E"));
  roundTrip(Ctor, Blob,
            attr({}, {named(false, "P", array(Elements)),
                      named(false, "Q",
                            str(std::string("abcdefgT\b\x01R\0\0\0\0", 15)))}),
            {}, ByteEnum);
  ASSERT_FALSE(Asked.empty());
  for (const std::string &N : Asked)
    EXPECT_EQ(N, "E");

  Expected<CustomAttributeValue> D =
      decodeCustomAttribute(Ctor, Blob, {}, ByteEnum);
  ASSERT_THAT_EXPECTED(D, Succeeded());
  ASSERT_EQ(D->NamedArgs.size(), 2u);
  EXPECT_EQ(D->NamedArgs[0].Value.EnumUnderlying,
            static_cast<ElementType>(SERIALIZATION_TYPE_ENUM));
  EXPECT_EQ(D->NamedArgs[0].Value.EnumTypeName, "E");
  ASSERT_TRUE(D->NamedArgs[1].Value.Str.has_value());
  EXPECT_EQ(D->NamedArgs[1].Value.Str->size(), 15u);
}

// [T((object)(E64)0x0253000100000005)], as the library and csc write it: 01 00
// 55 06 'NS.E64' 05 00 00 00 01 00 53 02 00 00. Read as an int32 enum, the
// same bytes are also a whole blob (5, then a bool field '' = false).
TEST(CLICustomAttributeTest, BoxedInt64EnumIsNotGuessed) {
  MethodSig ObjCtor = ctor({Obj});
  ByteVec Blob =
      fromHex("01 00 55 06 4E 53 2E 45 36 34 05 00 00 00 01 00 53 02 00 00");
  expectNeedsEnumName(ObjCtor, Blob, "NS.E64");
  roundTrip(ObjCtor, Blob,
            attr({boxed(
                enumValue(ELEMENT_TYPE_I8, 0x0253000100000005ull, "NS.E64"))}),
            {}, resolveEnumName);
}

TEST(CLICustomAttributeTest, EnumNameResolverErrors) {
  MethodSig ObjCtor = ctor({Obj});
  ByteVec BoxedE8 = fromHex("01 00 55 05 4E 53 2E 45 38 C8 00 00");
  // [T(Enum32Prop = E32.Neg)]: a named argument.
  ByteVec NamedE32 = fromHex("01 00 01 00 54 55 06 4E 53 2E 45 33 32 0A 45 6E "
                             "75 6D 33 32 50 72 6F 70 FE FF FF FF");
  encodesTo(ctor({}),
            attr({}, {named(false, "Enum32Prop",
                            enumValue(ELEMENT_TYPE_I4, uint64_t(int64_t(-2)),
                                      "NS.E32"))}),
            NamedE32);

  // The resolver's own error is returned unchanged.
  auto Refuse = [](StringRef Name) -> Expected<ElementType> {
    return createStringError(inconvertibleErrorCode(), "no enum named '%s'",
                             Name.str().c_str());
  };
  EXPECT_THAT_EXPECTED(decodeCustomAttribute(ObjCtor, BoxedE8, {}, Refuse),
                       FailedWithMessage("no enum named 'NS.E8'"));
  EXPECT_THAT_EXPECTED(decodeCustomAttribute(ctor({}), NamedE32, {}, Refuse),
                       FailedWithMessage("no enum named 'NS.E32'"));

  // An underlying type that is not an integer is an Error.
  for (ElementType Bad : {ELEMENT_TYPE_R4, ELEMENT_TYPE_R8, ELEMENT_TYPE_STRING,
                          ELEMENT_TYPE_OBJECT}) {
    SCOPED_TRACE(static_cast<int>(Bad));
    auto Returns = [Bad](StringRef) -> Expected<ElementType> { return Bad; };
    expectInvalidBlob(ObjCtor, BoxedE8, {}, Returns);
    expectInvalidBlob(ctor({}), NamedE32, {}, Returns);
  }
}

// Section 12 '5.7.4 amended (CAValue arrays)': an Array in a named argument or
// a boxed value writes its element type. A non-empty array writes the kind of
// its elements; an empty or null array writes its own EnumUnderlying
// (BOOLEAN..R8, STRING, or SERIALIZATION_TYPE_TYPE, _TAGGED_OBJECT or _ENUM
// cast to ElementType, with EnumTypeName for _ENUM), so the default
// ELEMENT_TYPE_I4 writes int32[]. The decoder sets both fields on every array
// it returns. The blobs are csc's for a property 'Names' of type int[],
// string[], bool[], byte[], double[], Type[], object[] and NS.E8[] set to an
// empty array (new int[0], ...) or to null (count FF FF FF FF), for
// [T((object)new string[0])], [T((object)new int[0])] and for
// [S(Names = new string[] { "a" })].
TEST(CLICustomAttributeTest, EmptyAndNullArraysWriteTheirOwnElementType) {
  MethodSig Ctor = ctor({});
  const ElementType TypeTag = static_cast<ElementType>(SERIALIZATION_TYPE_TYPE);
  const ElementType ObjectTag =
      static_cast<ElementType>(SERIALIZATION_TYPE_TAGGED_OBJECT);
  const ElementType EnumTag = static_cast<ElementType>(SERIALIZATION_TYPE_ENUM);
  struct {
    bool SetElem; // false: the default fields
    ElementType Elem;
    const char *EnumName;
    bool Null;
    const char *TypeHex; // after 54 1D
  } Cases[] = {
      {false, ELEMENT_TYPE_I4, "", false, "08"},
      {true, ELEMENT_TYPE_STRING, "", false, "0E"},
      {false, ELEMENT_TYPE_I4, "", true, "08"},
      {true, ELEMENT_TYPE_STRING, "", true, "0E"},
      {true, ELEMENT_TYPE_BOOLEAN, "", false, "02"},
      {true, ELEMENT_TYPE_U1, "", false, "05"},
      {true, ELEMENT_TYPE_R8, "", true, "0D"},
      {true, TypeTag, "", false, "50"},
      {true, ObjectTag, "", true, "51"},
      {true, EnumTag, "NS.E8", false, "55 05 4E 53 2E 45 38"},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(std::string(C.TypeHex) + (C.Null ? " null" : " empty"));
    CAValue V = C.Null ? array(std::nullopt) : array(std::vector<CAValue>{});
    EXPECT_EQ(V.EnumUnderlying, ELEMENT_TYPE_I4) << "the default";
    if (C.SetElem)
      V.EnumUnderlying = C.Elem;
    V.EnumTypeName = C.EnumName;
    ByteVec Blob = fromHex(std::string("01 00 01 00 54 1D ") + C.TypeHex +
                           " 05 4E 61 6D 65 73 " +
                           (C.Null ? "FF FF FF FF" : "00 00 00 00"));
    roundTrip(Ctor, Blob, attr({}, {named(false, "Names", V)}), {},
              resolveEnumName);
    Expected<CustomAttributeValue> D =
        decodeCustomAttribute(Ctor, Blob, {}, resolveEnumName);
    ASSERT_THAT_EXPECTED(D, Succeeded());
    ASSERT_EQ(D->NamedArgs.size(), 1u);
    const CAValue &Got = D->NamedArgs[0].Value;
    EXPECT_EQ(static_cast<int>(Got.K), static_cast<int>(Kind::Array));
    EXPECT_EQ(Got.Elements.has_value(), !C.Null);
    EXPECT_EQ(Got.EnumUnderlying, C.Elem);
    EXPECT_EQ(Got.EnumTypeName, C.EnumName);
  }

  // A boxed empty array: [T((object)new string[0])] on an object parameter.
  CAValue EmptyStrings = array(std::vector<CAValue>{});
  EmptyStrings.EnumUnderlying = ELEMENT_TYPE_STRING;
  roundTrip(ctor({Obj}), fromHex("01 00 1D 0E 00 00 00 00 00 00"),
            attr({boxed(EmptyStrings)}));
  roundTrip(ctor({Obj}), fromHex("01 00 1D 08 00 00 00 00 00 00"),
            attr({boxed(array(std::vector<CAValue>{}))}));

  // A non-empty array writes its elements' kind, whatever its own fields say:
  // [T(Names = new string[] { "a" })].
  roundTrip(
      Ctor, fromHex("01 00 01 00 54 1D 0E 05 4E 61 6D 65 73 01 00 00 00 01 61"),
      attr({}, {named(false, "Names", array(std::vector<CAValue>{str("a")}))}));
}

// Equality follows the blob (section 12 '5.7.4 amended (CAValue arrays)'): the
// element-type fields of an empty or null array are written, so == tells such
// arrays apart, while a non-empty array writes its elements' kind, so its own
// fields do not take part. An empty string[] and an empty int32[] encode as
// 54 1D 0E and 54 1D 08, a null Type[] and a null object[] as 54 1D 50 and
// 54 1D 51.
TEST(CLICustomAttributeTest, ArrayEqualityFollowsTheBlob) {
  MethodSig Ctor = ctor({});
  const ElementType TypeTag = static_cast<ElementType>(SERIALIZATION_TYPE_TYPE);
  const ElementType ObjectTag =
      static_cast<ElementType>(SERIALIZATION_TYPE_TAGGED_OBJECT);
  auto prop = [](CAValue V) { return attr({}, {named(false, "P", V)}); };
  auto encoded = [&](const CustomAttributeValue &V) {
    SmallVector<uint8_t, 32> Out;
    EXPECT_THAT_ERROR(encodeCustomAttribute(Ctor, V, Out), Succeeded());
    return toVec(Out);
  };

  CAValue EmptyStrings = array(std::vector<CAValue>{});
  EmptyStrings.EnumUnderlying = ELEMENT_TYPE_STRING;
  CAValue EmptyInts = array(std::vector<CAValue>{}); // the default I4
  CAValue NullTypes = array(std::nullopt);
  NullTypes.EnumUnderlying = TypeTag;
  CAValue NullObjects = array(std::nullopt);
  NullObjects.EnumUnderlying = ObjectTag;
  struct {
    const char *Name;
    const CAValue &A, &B;
    const char *HexA, *HexB; // the blobs of A and B as property P
  } Pairs[] = {
      {"empty string[] and int32[]", EmptyStrings, EmptyInts,
       "01 00 01 00 54 1D 0E 01 50 00 00 00 00",
       "01 00 01 00 54 1D 08 01 50 00 00 00 00"},
      {"null Type[] and object[]", NullTypes, NullObjects,
       "01 00 01 00 54 1D 50 01 50 FF FF FF FF",
       "01 00 01 00 54 1D 51 01 50 FF FF FF FF"},
  };
  for (const auto &P : Pairs) {
    SCOPED_TRACE(P.Name);
    EXPECT_EQ(encoded(prop(P.A)), fromHex(P.HexA));
    EXPECT_EQ(encoded(prop(P.B)), fromHex(P.HexB));
    EXPECT_TRUE(P.A == P.A);
    EXPECT_FALSE(P.A != P.A);
    EXPECT_FALSE(P.A == P.B);
    EXPECT_TRUE(P.A != P.B);
    EXPECT_FALSE(P.B == P.A);
    EXPECT_TRUE(prop(P.A) == prop(P.A));
    EXPECT_FALSE(prop(P.A) != prop(P.A));
    EXPECT_FALSE(prop(P.A) == prop(P.B));
    EXPECT_TRUE(prop(P.A) != prop(P.B));
    // The same arrays boxed, as an object constructor argument.
    EXPECT_FALSE(attr({boxed(P.A)}) == attr({boxed(P.B)}));
    EXPECT_TRUE(attr({boxed(P.A)}) != attr({boxed(P.B)}));
  }
  // An empty and a null array of one element type differ too.
  CAValue NullStrings = array(std::nullopt);
  NullStrings.EnumUnderlying = ELEMENT_TYPE_STRING;
  EXPECT_FALSE(EmptyStrings == NullStrings);

  // decode(encode(V)) == V for a client-built non-empty string[], which keeps
  // the default EnumUnderlying (the decoder sets STRING), and for an empty
  // string[]. A non-empty array's own fields do not take part.
  CAValue OneString = array(std::vector<CAValue>{str("a")});
  ASSERT_EQ(OneString.EnumUnderlying, ELEMENT_TYPE_I4);
  CAValue OneStringTyped = OneString;
  OneStringTyped.EnumUnderlying = ELEMENT_TYPE_STRING;
  EXPECT_TRUE(OneString == OneStringTyped);
  EXPECT_FALSE(OneString != OneStringTyped);
  for (const CAValue *V : {&OneString, &EmptyStrings}) {
    CustomAttributeValue Want = prop(*V);
    Expected<CustomAttributeValue> D =
        decodeCustomAttribute(Ctor, encoded(Want));
    ASSERT_THAT_EXPECTED(D, Succeeded());
    ASSERT_EQ(D->NamedArgs.size(), 1u);
    EXPECT_EQ(D->NamedArgs[0].Value.EnumUnderlying, ELEMENT_TYPE_STRING);
    EXPECT_TRUE(*D == Want);
    EXPECT_FALSE(*D != Want);
    EXPECT_TRUE(D->NamedArgs[0].Value == *V);
  }
}

TEST(CLICustomAttributeTest, EncodeErrors) {
  // Argument counts: 'the constructor has 0 parameters and the value 1 fixed
  // arguments', and the reverse.
  expectEncodeError(ctor({}), attr({i4(1)}), "argument");
  expectEncodeError(ctor({I4, I4}), attr({i4(1)}), "argument");
  // A kind that does not match its parameter.
  expectEncodeError(ctor({I4}), attr({str("x")}));
  expectEncodeError(ctor({Str}), attr({i4(1)}));
  expectEncodeError(ctor({TypeSig::getSZArray(I4)}), attr({i4(1)}));
  expectEncodeError(ctor({TypeSig::getSZArray(I4)}),
                    attr({array(std::vector<CAValue>{str("x")})}));
  // An enum parameter without Resolve.
  expectEncodeError(ctor({E32}), attr({enumValue(ELEMENT_TYPE_I4, 5)}));
  // An unsupported parameter type (II.23.3 has no pointers).
  expectEncodeError(ctor({TypeSig::getPointer(I4)}), attr({i4(1)}));
  // Array elements of different kinds, in a named argument and boxed.
  expectEncodeError(
      ctor({}),
      attr({}, {named(false, "Mixed",
                      array(std::vector<CAValue>{i4(1), str("x")}))}));
  expectEncodeError(ctor({Obj}),
                    attr({boxed(array(std::vector<CAValue>{i4(1), u1(2)}))}));
}

TEST(CLICustomAttributeTest, DecodeErrors) {
  MethodSig IntCtor = ctor({I4});
  const char *BadInt[] = {
      "",                                                // empty
      "01",                                              // half a prolog
      "02 00 05 00 00 00 00 00",                         // bad prolog
      "01 00 05 00",                                     // a truncated int32
      "01 00 05 00 00 00",                               // no NumNamed
      "01 00 05 00 00 00 00",                            // half a NumNamed
      "01 00 05 00 00 00 00 00 FF",                      // trailing bytes
      "01 00 05 00 00 00 01 00",                         // a missing named arg
      "01 00 05 00 00 00 01 00 52 08 01 46 07 00 00 00", // not FIELD/PROPERTY
      "01 00 05 00 00 00 01 00 53 99 01 46 07 00 00 00", // unknown type
      "01 00 05 00 00 00 01 00 53 08 05 46 07 00 00 00", // name past the end
      "01 00 05 00 00 00 01 00 53 08 01 46 07 00",       // truncated value
  };
  for (const char *Hex : BadInt) {
    SCOPED_TRACE(Hex);
    expectInvalidBlob(IntCtor, fromHex(Hex));
  }
  MethodSig StrCtor = ctor({Str});
  expectInvalidBlob(StrCtor, fromHex("01 00 05 61 00 00")); // short string
  expectInvalidBlob(StrCtor, fromHex("01 00 E0 61 00 00")); // bad length
  expectInvalidBlob(ctor({TypeSig::getSZArray(I4)}),
                    fromHex("01 00 FE FF FF 7F 00 00"));     // huge count
  expectInvalidBlob(ctor({Obj}), fromHex("01 00 99 00 00")); // unknown tag
  expectInvalidBlob(ctor({Obj}), fromHex("01 00 08 2A 00")); // short value
  expectInvalidBlob(ctor({SystemType}), fromHex("01 00 04 4E 53 00 00"));

  // Errors name the cause and the offset (plan M16.a): the prolog (II.23.3)
  // at 0x0, trailing bytes at 0x8.
  std::string Prolog =
      decodeErrorMessage(IntCtor, fromHex("02 00 05 00 00 00 00 00"));
  EXPECT_THAT(Prolog, testing::HasSubstr("prolog"));
  EXPECT_THAT(Prolog, testing::HasSubstr("offset 0x"));
  EXPECT_THAT(
      decodeErrorMessage(IntCtor, fromHex("01 00 05 00 00 00 00 00 FF")),
      testing::HasSubstr("offset 0x"));
}

TEST(CLICustomAttributeTest, ValuesAreDeepCopies) {
  CAValue Orig = boxed(array(std::vector<CAValue>{i4(1), str("x")}));
  CAValue Copy = Orig;
  ASSERT_TRUE(Copy.BoxedValue != nullptr);
  EXPECT_NE(Copy.BoxedValue.get(), Orig.BoxedValue.get());
  (*Copy.BoxedValue->Elements)[0].Bits = 99;
  (*Copy.BoxedValue->Elements)[1].Str = "changed";
  EXPECT_EQ((*Orig.BoxedValue->Elements)[0].Bits, 1u);
  EXPECT_EQ((*Orig.BoxedValue->Elements)[1].Str,
            std::optional<std::string>("x"));

  CAValue Assigned;
  Assigned = Orig;
  ASSERT_TRUE(Assigned.BoxedValue != nullptr);
  EXPECT_NE(Assigned.BoxedValue.get(), Orig.BoxedValue.get());
  expectSame(Assigned, Orig);

  CustomAttributeValue V = attr({Orig}, {named(true, "F", Orig)});
  CustomAttributeValue W = V;
  W.NamedArgs[0].Value.BoxedValue->K = Kind::I4;
  EXPECT_EQ(static_cast<int>(V.NamedArgs[0].Value.BoxedValue->K),
            static_cast<int>(Kind::Array));
}

} // namespace
