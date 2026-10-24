//===- SignatureTest.cpp - ECMA-335 signature model and codecs ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/Signature.h (plan 5.7.3): the TypeSig and MethodSig value
// types, the encoders and decoders of method, field, local, property, TypeSpec
// and MethodSpec signatures (ECMA-335 II.23.2), every decode error with its
// offset and cause (section 12 '5.7.3 clarified'), the TypeRef mapping that
// msil-ld renumbers with, and the canonical listing v1 print grammar (plan
// 5.7.10; general arrays as amended in section 12). Expected bytes are the
// vectors of plan 5.7.3 and metadata-writer.md 4.6 (bytes the runtimes
// accepted), ECMA-335's grammar, signatures that csc wrote and
// System.Reflection.Metadata read in scratch, and the ARRAY shapes that SRM's
// BlobEncoder wrote in scratch; none come from the implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/Signature.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

using ByteVec = std::vector<uint8_t>;

ByteVec toVec(ArrayRef<uint8_t> A) { return ByteVec(A.begin(), A.end()); }

ByteVec cat(std::initializer_list<ByteVec> Parts) {
  ByteVec R;
  for (const ByteVec &P : Parts)
    R.insert(R.end(), P.begin(), P.end());
  return R;
}

constexpr uint32_t TR(uint32_t Row) { return 0x01000000 | Row; } // TypeRef
constexpr uint32_t TD(uint32_t Row) { return 0x02000000 | Row; } // TypeDef
constexpr uint32_t TS(uint32_t Row) { return 0x1B000000 | Row; } // TypeSpec

void printRef(raw_ostream &OS, uint32_t T) {
  switch (T) {
  case TR(1):
    OS << "[mscorlib]System.Object";
    return;
  case TR(2):
    OS << "[mscorlib]System.Collections.Generic.List`1";
    return;
  case TR(3):
    OS << "[mscorlib]System.Nullable`1";
    return;
  case TR(4):
    OS << "[mscorlib]System.Runtime.CompilerServices.IsVolatile";
    return;
  case TR(5):
    OS << "[mscorlib]System.Runtime.CompilerServices.IsConst";
    return;
  case TR(0x10):
    OS << "[System.Runtime]System.Runtime.InteropServices.InAttribute";
    return;
  case TD(2):
    OS << "'<msil>image_t'";
    return;
  default:
    OS << "#" << utohexstr(T);
    return;
  }
}

std::string print(const TypeSig &T) {
  std::string S;
  raw_string_ostream OS(S);
  printTypeSig(OS, T, printRef);
  OS.flush();
  return S;
}

std::string print(const MethodSig &M) {
  std::string S;
  raw_string_ostream OS(S);
  printMethodSig(OS, M, printRef);
  OS.flush();
  return S;
}

TypeSig P(ElementType E) { return TypeSig::get(E); }

MethodSig method(uint8_t Header, TypeSig Ret, std::vector<TypeSig> Params,
                 uint32_t RequiredParamCount = UINT32_MAX) {
  MethodSig S;
  S.Header = Header;
  S.Return = std::move(Ret);
  S.Params = std::move(Params);
  S.RequiredParamCount = RequiredParamCount;
  return S;
}

ArrayShape shape(uint32_t Rank, std::vector<uint32_t> Sizes = {},
                 std::vector<int32_t> LowerBounds = {}) {
  ArrayShape S;
  S.Rank = Rank;
  S.Sizes.assign(Sizes.begin(), Sizes.end());
  S.LowerBounds.assign(LowerBounds.begin(), LowerBounds.end());
  return S;
}

CustomModifier modreq(uint32_t Type) { return CustomModifier{true, Type}; }
CustomModifier modopt(uint32_t Type) { return CustomModifier{false, Type}; }

ByteVec encode(const MethodSig &S) {
  SmallVector<uint8_t, 32> Out;
  encodeMethodSig(S, Out);
  return toVec(Out);
}

ByteVec encodeField(const TypeSig &T) {
  SmallVector<uint8_t, 32> Out;
  encodeFieldSig(T, Out);
  return toVec(Out);
}

ByteVec encodeLocals(ArrayRef<TypeSig> Locals) {
  SmallVector<uint8_t, 32> Out;
  encodeLocalSig(Locals, Out);
  return toVec(Out);
}

// Encodes S, compares with Bytes, decodes Bytes and compares with S.
void checkMethod(const MethodSig &S, const ByteVec &Bytes) {
  EXPECT_EQ(encode(S), Bytes);
  Expected<MethodSig> D = decodeMethodSig(Bytes);
  ASSERT_THAT_EXPECTED(D, Succeeded());
  EXPECT_TRUE(*D == S) << "decoded as " << print(*D);
}

void checkField(const TypeSig &T, const ByteVec &Bytes) {
  EXPECT_EQ(encodeField(T), Bytes);
  Expected<TypeSig> D = decodeFieldSig(Bytes);
  ASSERT_THAT_EXPECTED(D, Succeeded());
  EXPECT_TRUE(*D == T) << "decoded as " << print(*D);
}

void checkLocals(const std::vector<TypeSig> &L, const ByteVec &Bytes) {
  EXPECT_EQ(encodeLocals(L), Bytes);
  Expected<std::vector<TypeSig>> D = decodeLocalSig(Bytes);
  ASSERT_THAT_EXPECTED(D, Succeeded());
  EXPECT_TRUE(*D == L);
}

// Every decode error starts with the prefix and names the cause and the offset
// (plan 5.7.3; section 12 '5.7.3 clarified (signature decode errors)'). Cause,
// when given, is a word of the cause, matched in any case.
template <typename T> void expectInvalid(Expected<T> E, StringRef Cause = "") {
  if (E) {
    ADD_FAILURE() << "the blob decoded without an error";
    return;
  }
  std::string Msg = toString(E.takeError());
  EXPECT_THAT(Msg, testing::StartsWith("invalid signature: "));
  EXPECT_THAT(Msg, testing::HasSubstr("offset 0x"));
  if (!Cause.empty())
    EXPECT_THAT(StringRef(Msg).lower(), testing::HasSubstr(Cause.lower()));
}

const TypeSig I4 = TypeSig::get(ELEMENT_TYPE_I4);
const TypeSig I = TypeSig::get(ELEMENT_TYPE_I);
const TypeSig Str = TypeSig::get(ELEMENT_TYPE_STRING);
const TypeSig Void;

//===----------------------------------------------------------------------===//
// The vectors of plan 5.7.3 and metadata-writer.md 4.6
//===----------------------------------------------------------------------===//

TEST(CLISignatureTest, PlanVectorsMethods) {
  struct {
    MethodSig Sig;
    ByteVec Bytes;
    const char *Text;
  } Cases[] = {
      {method(SIG_DEFAULT, I4, {}), {0x00, 0x00, 0x08}, "int32()"},
      {method(SIG_DEFAULT, I4, {I4}), {0x00, 0x01, 0x08, 0x08}, "int32(int32)"},
      {method(SIG_DEFAULT, I4, {I}),
       {0x00, 0x01, 0x08, 0x18},
       "int32(native int)"},
      {method(SIG_DEFAULT, I4, {TypeSig::getSZArray(Str)}),
       {0x00, 0x01, 0x08, 0x1D, 0x0E},
       "int32(string[])"},
      {method(SIG_DEFAULT, Void, {}), {0x00, 0x00, 0x01}, "void()"},
      {method(SIG_HASTHIS, Void, {Str, Str}),
       {0x20, 0x02, 0x01, 0x0E, 0x0E},
       "instance void(string, string)"},
      {method(SIG_DEFAULT, I, {I}),
       {0x00, 0x01, 0x18, 0x18},
       "native int(native int)"},
      {method(SIG_DEFAULT, I, {Str}),
       {0x00, 0x01, 0x18, 0x0E},
       "native int(string)"},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.Text);
    checkMethod(C.Sig, C.Bytes);
    EXPECT_EQ(print(C.Sig), C.Text);
  }
}

TEST(CLISignatureTest, PlanVectorsFieldsAndLocals) {
  // field valuetype '<msil>image_t' (TypeDef row 2: 2 << 2 | 0).
  checkField(TypeSig::getValueType(TD(2)), {0x06, 0x11, 0x08});
  EXPECT_EQ(print(TypeSig::getValueType(TD(2))), "valuetype '<msil>image_t'");
  checkField(I, {0x06, 0x18});
  EXPECT_EQ(print(I), "native int");
  checkLocals({I4, I, I4, I4}, {0x07, 0x04, 0x08, 0x18, 0x08, 0x08});
}

//===----------------------------------------------------------------------===//
// Element types and constructed types
//===----------------------------------------------------------------------===//

TEST(CLISignatureTest, EveryPrimitiveElementType) {
  struct {
    ElementType E;
    const char *Text;
  } Prims[] = {
      {ELEMENT_TYPE_BOOLEAN, "bool"},
      {ELEMENT_TYPE_CHAR, "char"},
      {ELEMENT_TYPE_I1, "int8"},
      {ELEMENT_TYPE_U1, "uint8"},
      {ELEMENT_TYPE_I2, "int16"},
      {ELEMENT_TYPE_U2, "uint16"},
      {ELEMENT_TYPE_I4, "int32"},
      {ELEMENT_TYPE_U4, "uint32"},
      {ELEMENT_TYPE_I8, "int64"},
      {ELEMENT_TYPE_U8, "uint64"},
      {ELEMENT_TYPE_R4, "float32"},
      {ELEMENT_TYPE_R8, "float64"},
      {ELEMENT_TYPE_I, "native int"},
      {ELEMENT_TYPE_U, "native uint"},
      {ELEMENT_TYPE_STRING, "string"},
      {ELEMENT_TYPE_OBJECT, "object"},
      {ELEMENT_TYPE_TYPEDBYREF, "typedref"},
  };
  std::vector<TypeSig> Params;
  ByteVec Bytes = {0x00, uint8_t(std::size(Prims)), 0x01};
  std::string Text = "void(";
  for (const auto &Pr : Prims) {
    SCOPED_TRACE(Pr.Text);
    TypeSig T = TypeSig::get(Pr.E);
    EXPECT_EQ(T.getElementType(), Pr.E);
    EXPECT_TRUE(T.getModifiers().empty());
    EXPECT_EQ(print(T), Pr.Text);
    if (Pr.E != ELEMENT_TYPE_TYPEDBYREF) // typedref is no field type
      checkField(T, {0x06, uint8_t(Pr.E)});
    Params.push_back(T);
    Bytes.push_back(Pr.E);
    Text += std::string(Params.size() == 1 ? "" : ", ") + Pr.Text;
  }
  Text += ")";
  MethodSig S = method(SIG_DEFAULT, Void, Params);
  checkMethod(S, Bytes);
  EXPECT_EQ(print(S), Text);
  // typedref as a return type and a local.
  checkMethod(method(SIG_DEFAULT, P(ELEMENT_TYPE_TYPEDBYREF), {}),
              {0x00, 0x00, 0x16});
  checkLocals({P(ELEMENT_TYPE_TYPEDBYREF)}, {0x07, 0x01, 0x16});
}

TEST(CLISignatureTest, DefaultTypeSigIsVoid) {
  TypeSig T;
  EXPECT_EQ(T.getElementType(), ELEMENT_TYPE_VOID);
  EXPECT_TRUE(T == TypeSig::get(ELEMENT_TYPE_VOID));
  EXPECT_EQ(hash_value(T), hash_value(TypeSig::get(ELEMENT_TYPE_VOID)));
  EXPECT_EQ(print(T), "void");
  MethodSig M;
  EXPECT_EQ(M.Header, SIG_DEFAULT);
  EXPECT_EQ(M.GenericParamCount, 0u);
  EXPECT_TRUE(M.Return == T);
  EXPECT_TRUE(M.Params.empty());
  EXPECT_EQ(M.RequiredParamCount, UINT32_MAX);
  EXPECT_EQ(encode(M), (ByteVec{0x00, 0x00, 0x01}));
}

// Each type as the one parameter of 'void(T)': 00 01 01 <T>.
TEST(CLISignatureTest, ConstructedTypes) {
  struct {
    TypeSig T;
    ByteVec Bytes;
    const char *Text;
  } Cases[] = {
      {TypeSig::getPointer(Void), {0x0F, 0x01}, "void*"},
      {TypeSig::getPointer(I4), {0x0F, 0x08}, "int32*"},
      {TypeSig::getPointer(TypeSig::getPointer(P(ELEMENT_TYPE_U1))),
       {0x0F, 0x0F, 0x05},
       "uint8**"},
      {TypeSig::getByRef(I4), {0x10, 0x08}, "int32&"},
      {TypeSig::getByRef(TypeSig::getPointer(I)),
       {0x10, 0x0F, 0x18},
       "native int*&"},
      {TypeSig::getSZArray(Str), {0x1D, 0x0E}, "string[]"},
      {TypeSig::getSZArray(TypeSig::getSZArray(I4)),
       {0x1D, 0x1D, 0x08},
       "int32[][]"},
      {TypeSig::getClass(TR(1)), {0x12, 0x05}, "class [mscorlib]System.Object"},
      {TypeSig::getValueType(TD(2)), {0x11, 0x08}, "valuetype '<msil>image_t'"},
      {TypeSig::getClass(TD(1)), {0x12, 0x04}, "class #2000001"},
      {TypeSig::getValueType(TS(1)), {0x11, 0x06}, "valuetype #1B000001"},
      {TypeSig::getClass(TR(0x100)), {0x12, 0x84, 0x01}, "class #1000100"},
      {TypeSig::getClass(TR(0xFFFFFF)),
       {0x12, 0xC3, 0xFF, 0xFF, 0xFD},
       "class #1FFFFFF"},
      {TypeSig::getSZArray(TypeSig::getClass(TR(1))),
       {0x1D, 0x12, 0x05},
       "class [mscorlib]System.Object[]"},
      {TypeSig::getPointer(TypeSig::getValueType(TD(2))),
       {0x0F, 0x11, 0x08},
       "valuetype '<msil>image_t'*"},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.Text);
    EXPECT_EQ(print(C.T), C.Text);
    checkMethod(method(SIG_DEFAULT, Void, {C.T}),
                cat({{0x00, 0x01, 0x01}, C.Bytes}));
    EXPECT_EQ(print(method(SIG_DEFAULT, Void, {C.T})),
              std::string("void(") + C.Text + ")");
    SmallVector<uint8_t, 16> Spec;
    encodeTypeSpec(C.T, Spec);
    EXPECT_EQ(toVec(Spec), C.Bytes);
    Expected<TypeSig> D = decodeTypeSpec(C.Bytes);
    ASSERT_THAT_EXPECTED(D, Succeeded());
    EXPECT_TRUE(*D == C.T);
  }
}

// II.23.2.13: ARRAY Type Rank NumSizes Size* NumLoBounds LoBound*, the lower
// bounds as compressed signed integers. The listing (plan 5.7.10, amended in
// section 12: '<type> in signatures, general arrays') prints '<t>[', the Rank
// dimension texts joined by ',' and ']'. Dimension i has a lower bound when
// i < NumLoBounds and a size when i < NumSizes, and prints '<lo>...<hi>' with
// both (hi = lo + size - 1 in decimal, so a size of 0 gives lo - 1),
// '<lo>...' with a lower bound only, '<size>' with a size only and nothing
// with neither, except that a rank-1 array with neither prints '...'. Only
// SZARRAY prints '<t>[]'.
TEST(CLISignatureTest, ArrayShapes) {
  struct {
    TypeSig T;
    ByteVec Bytes;
    const char *Text;
  } Cases[] = {
      {TypeSig::getArray(I4, shape(2)),
       {0x14, 0x08, 0x02, 0x00, 0x00},
       "int32[,]"},
      {TypeSig::getArray(P(ELEMENT_TYPE_R8), shape(3, {2, 3}, {0, -1})),
       {0x14, 0x0D, 0x03, 0x02, 0x02, 0x03, 0x02, 0x00, 0x7F},
       "float64[0...1,-1...1,]"},
      {TypeSig::getArray(I4, shape(1, {4}, {-3})),
       {0x14, 0x08, 0x01, 0x01, 0x04, 0x01, 0x7B},
       "int32[-3...0]"},
      {TypeSig::getArray(Str, shape(2, {0x80}, {64})),
       {0x14, 0x0E, 0x02, 0x01, 0x80, 0x80, 0x01, 0x80, 0x80},
       "string[64...191,]"},
      // Rank 1 with neither a size nor a lower bound.
      {TypeSig::getArray(I4, shape(1)),
       {0x14, 0x08, 0x01, 0x00, 0x00},
       "int32[...]"},
      // A size without a lower bound.
      {TypeSig::getArray(I4, shape(1, {4}, {})),
       {0x14, 0x08, 0x01, 0x01, 0x04, 0x00},
       "int32[4]"},
      // A lower bound only.
      {TypeSig::getArray(I4, shape(1, {}, {0})),
       {0x14, 0x08, 0x01, 0x00, 0x01, 0x00},
       "int32[0...]"},
      // A size of 0 with a lower bound: hi = lo - 1.
      {TypeSig::getArray(I4, shape(1, {0}, {5})),
       {0x14, 0x08, 0x01, 0x01, 0x00, 0x01, 0x0A},
       "int32[5...4]"},
      // The row's other examples: [5...10], C#'s int[,] (lower bounds 0, no
      // sizes) and a size on the first dimension only.
      {TypeSig::getArray(I4, shape(1, {6}, {5})),
       {0x14, 0x08, 0x01, 0x01, 0x06, 0x01, 0x0A},
       "int32[5...10]"},
      {TypeSig::getArray(I4, shape(2, {}, {0, 0})),
       {0x14, 0x08, 0x02, 0x00, 0x02, 0x00, 0x00},
       "int32[0...,0...]"},
      {TypeSig::getArray(I4, shape(2, {4}, {0, -1})),
       {0x14, 0x08, 0x02, 0x01, 0x04, 0x02, 0x00, 0x7F},
       "int32[0...3,-1...]"},
      // A negative upper bound, and the compressed-integer limits (II.23.2:
      // size 0x1FFFFFFF is DF FF FF FF, lower bounds -2^28 and 2^28 - 1 are
      // C0 00 00 01 and DF FF FF FE).
      {TypeSig::getArray(I4, shape(1, {2}, {-5})),
       {0x14, 0x08, 0x01, 0x01, 0x02, 0x01, 0x77},
       "int32[-5...-4]"},
      {TypeSig::getArray(I4, shape(1, {0x1FFFFFFF}, {-0x10000000})),
       {0x14, 0x08, 0x01, 0x01, 0xDF, 0xFF, 0xFF, 0xFF, 0x01, 0xC0, 0x00, 0x00,
        0x01},
       "int32[-268435456...268435454]"},
      {TypeSig::getArray(I4, shape(1, {0x1FFFFFFF}, {0x0FFFFFFF})),
       {0x14, 0x08, 0x01, 0x01, 0xDF, 0xFF, 0xFF, 0xFF, 0x01, 0xDF, 0xFF, 0xFF,
        0xFE},
       "int32[268435455...805306365]"},
  };
  // SZARRAY keeps '[]', which no general array prints: a MemberRef naming
  // f(int32[0...]) does not bind to f(int32[]).
  TypeSig SZ = TypeSig::getSZArray(I4);
  EXPECT_EQ(print(SZ), "int32[]");
  checkField(SZ, {0x06, 0x1D, 0x08});
  std::vector<std::string> Int32Texts;
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.Text);
    EXPECT_EQ(C.T.getElementType(), ELEMENT_TYPE_ARRAY);
    EXPECT_EQ(print(C.T), C.Text);
    checkField(C.T, cat({{0x06}, C.Bytes}));
    EXPECT_NE(print(C.T), print(SZ));
    EXPECT_FALSE(C.T == SZ);
    EXPECT_EQ(print(method(SIG_DEFAULT, Void, {C.T})),
              std::string("void(") + C.Text + ")");
    if (StringRef(C.Text).starts_with("int32["))
      Int32Texts.push_back(C.Text);
  }
  // Different shapes of int32 print differently.
  std::sort(Int32Texts.begin(), Int32Texts.end());
  EXPECT_TRUE(std::adjacent_find(Int32Texts.begin(), Int32Texts.end()) ==
              Int32Texts.end());
  TypeSig A = TypeSig::getArray(P(ELEMENT_TYPE_R8), shape(3, {2, 3}, {0, -1}));
  ASSERT_EQ(A.getOperands().size(), 1u);
  EXPECT_TRUE(A.getOperands()[0] == P(ELEMENT_TYPE_R8));
  EXPECT_EQ(A.getArrayShape().Rank, 3u);
  EXPECT_EQ(std::vector<uint32_t>(A.getArrayShape().Sizes.begin(),
                                  A.getArrayShape().Sizes.end()),
            (std::vector<uint32_t>{2, 3}));
  EXPECT_EQ(std::vector<int32_t>(A.getArrayShape().LowerBounds.begin(),
                                 A.getArrayShape().LowerBounds.end()),
            (std::vector<int32_t>{0, -1}));
  // Shapes take part in equality.
  EXPECT_FALSE(A == TypeSig::getArray(P(ELEMENT_TYPE_R8), shape(3, {2, 3})));
  EXPECT_FALSE(TypeSig::getArray(I4, shape(2)) ==
               TypeSig::getArray(I4, shape(3)));
  EXPECT_FALSE(TypeSig::getArray(I4, shape(1)) == TypeSig::getSZArray(I4));
}

// II.23.2.12: GENERICINST (CLASS | VALUETYPE) TypeDefOrRefOrSpec GenArgCount
// Type+; TypeSpec blobs hold the bare type, MethodSpec blobs 0x0A, a count and
// the types (II.23.2.15).
TEST(CLISignatureTest, GenericInstantiations) {
  TypeSig List = TypeSig::getGenericInst(false, TR(2), {I4, Str});
  EXPECT_EQ(List.getElementType(), ELEMENT_TYPE_GENERICINST);
  EXPECT_EQ(List.getTypeRef(), TR(2));
  ASSERT_EQ(List.getOperands().size(), 2u);
  EXPECT_TRUE(List.getOperands()[0] == I4);
  EXPECT_TRUE(List.getOperands()[1] == Str);
  EXPECT_EQ(print(List),
            "class [mscorlib]System.Collections.Generic.List`1<int32, string>");
  checkField(List, {0x06, 0x15, 0x12, 0x09, 0x02, 0x08, 0x0E});

  TypeSig Nullable =
      TypeSig::getGenericInst(true, TR(3), {TypeSig::getGenericParam(true, 0)});
  EXPECT_EQ(print(Nullable), "valuetype [mscorlib]System.Nullable`1<!!0>");
  checkField(Nullable, {0x06, 0x15, 0x11, 0x0D, 0x01, 0x1E, 0x00});
  EXPECT_FALSE(Nullable ==
               TypeSig::getGenericInst(false, TR(3),
                                       {TypeSig::getGenericParam(true, 0)}));

  // Nested: List<Nullable<int32>[]>.
  TypeSig Nested = TypeSig::getGenericInst(
      false, TR(2),
      {TypeSig::getSZArray(TypeSig::getGenericInst(true, TR(3), {I4}))});
  checkField(Nested, {0x06, 0x15, 0x12, 0x09, 0x01, 0x1D, 0x15, 0x11, 0x0D,
                      0x01, 0x08});
  EXPECT_EQ(print(Nested), "class [mscorlib]System.Collections.Generic.List`1"
                           "<valuetype [mscorlib]System.Nullable`1<int32>[]>");

  SmallVector<uint8_t, 16> Spec;
  encodeTypeSpec(List, Spec);
  EXPECT_EQ(toVec(Spec), (ByteVec{0x15, 0x12, 0x09, 0x02, 0x08, 0x0E}));
  Expected<TypeSig> DSpec = decodeTypeSpec(Spec);
  ASSERT_THAT_EXPECTED(DSpec, Succeeded());
  EXPECT_TRUE(*DSpec == List);

  SmallVector<uint8_t, 16> MSpec;
  encodeMethodSpec({I4, Str}, MSpec);
  EXPECT_EQ(toVec(MSpec), (ByteVec{0x0A, 0x02, 0x08, 0x0E}));
  Expected<std::vector<TypeSig>> DM = decodeMethodSpec(MSpec);
  ASSERT_THAT_EXPECTED(DM, Succeeded());
  EXPECT_TRUE(*DM == (std::vector<TypeSig>{I4, Str}));
}

TEST(CLISignatureTest, GenericParameters) {
  TypeSig Var0 = TypeSig::getGenericParam(false, 0);
  TypeSig MVar1 = TypeSig::getGenericParam(true, 1);
  TypeSig Var200 = TypeSig::getGenericParam(false, 200);
  EXPECT_EQ(Var0.getElementType(), ELEMENT_TYPE_VAR);
  EXPECT_FALSE(Var0.isMethodGenericParam());
  EXPECT_EQ(Var0.getGenericParamIndex(), 0u);
  EXPECT_EQ(MVar1.getElementType(), ELEMENT_TYPE_MVAR);
  EXPECT_TRUE(MVar1.isMethodGenericParam());
  EXPECT_EQ(MVar1.getGenericParamIndex(), 1u);
  EXPECT_FALSE(TypeSig::getGenericParam(false, 1) == MVar1);
  EXPECT_EQ(print(Var0), "!0");
  EXPECT_EQ(print(MVar1), "!!1");
  EXPECT_EQ(print(Var200), "!200");
  checkField(Var0, {0x06, 0x13, 0x00});
  checkField(MVar1, {0x06, 0x1E, 0x01});
  checkField(Var200, {0x06, 0x13, 0x80, 0xC8});

  // II.23.2.1: a generic method has GENERIC in the header and the generic
  // parameter count before the parameter count.
  MethodSig G = method(SIG_GENERIC, TypeSig::getGenericParam(true, 0),
                       {TypeSig::getGenericParam(true, 0)});
  G.GenericParamCount = 1;
  checkMethod(G, {0x10, 0x01, 0x01, 0x1E, 0x00, 0x1E, 0x00});
  EXPECT_EQ(print(G), "<1> !!0(!!0)");
  MethodSig GI = method(SIG_HASTHIS | SIG_GENERIC, Void,
                        {TypeSig::getGenericParam(true, 1), Var0});
  GI.GenericParamCount = 2;
  checkMethod(GI, {0x30, 0x02, 0x02, 0x01, 0x1E, 0x01, 0x13, 0x00});
  EXPECT_EQ(print(GI), "instance <2> void(!!1, !0)");
}

TEST(CLISignatureTest, FunctionPointers) {
  MethodSig Cdecl = method(SIG_C, I4, {I4});
  TypeSig F = TypeSig::getFunctionPointer(Cdecl);
  EXPECT_EQ(F.getElementType(), ELEMENT_TYPE_FNPTR);
  EXPECT_TRUE(F.getFunctionSig() == Cdecl);
  EXPECT_EQ(print(F), "method unmanaged cdecl int32(int32)");
  checkField(F, {0x06, 0x1B, 0x01, 0x01, 0x08, 0x08});

  TypeSig Managed = TypeSig::getFunctionPointer(method(SIG_DEFAULT, Void, {}));
  EXPECT_EQ(print(Managed), "method void()");
  checkField(Managed, {0x06, 0x1B, 0x00, 0x00, 0x01});
  EXPECT_FALSE(F == Managed);

  // A function pointer that returns a function pointer and takes a pointer.
  TypeSig Outer = TypeSig::getFunctionPointer(
      method(SIG_STDCALL, F, {TypeSig::getPointer(Void)}));
  EXPECT_EQ(print(Outer), "method unmanaged stdcall method unmanaged cdecl "
                          "int32(int32)(void*)");
  checkMethod(
      method(SIG_DEFAULT, Outer, {}),
      {0x00, 0x00, 0x1B, 0x02, 0x01, 0x1B, 0x01, 0x01, 0x08, 0x08, 0x0F, 0x01});
}

// II.23.2.7: custom modifiers precede the type they modify, in blob order;
// withModifier appends after the existing ones and the listing prints them in
// the same order after the type.
TEST(CLISignatureTest, ModifiersInBlobOrder) {
  TypeSig A = I4.withModifier(modreq(TR(4)));
  EXPECT_TRUE(I4.getModifiers().empty()) << "withModifier returns a copy";
  ASSERT_EQ(A.getModifiers().size(), 1u);
  EXPECT_TRUE(A.getModifiers()[0].IsRequired);
  EXPECT_EQ(A.getModifiers()[0].Type, TR(4));
  EXPECT_EQ(A.getElementType(), ELEMENT_TYPE_I4);
  checkField(A, {0x06, 0x1F, 0x11, 0x08});
  EXPECT_EQ(
      print(A),
      "int32 modreq([mscorlib]System.Runtime.CompilerServices.IsVolatile)");

  TypeSig B = I4.withModifier(modopt(TR(5))).withModifier(modreq(TR(4)));
  ASSERT_EQ(B.getModifiers().size(), 2u);
  EXPECT_FALSE(B.getModifiers()[0].IsRequired);
  EXPECT_EQ(B.getModifiers()[0].Type, TR(5));
  EXPECT_TRUE(B.getModifiers()[1].IsRequired);
  checkField(B, {0x06, 0x20, 0x15, 0x1F, 0x11, 0x08});
  EXPECT_EQ(print(B),
            "int32 modopt([mscorlib]System.Runtime.CompilerServices.IsConst) "
            "modreq([mscorlib]System.Runtime.CompilerServices.IsVolatile)");
  EXPECT_FALSE(B == I4.withModifier(modreq(TR(4))).withModifier(modopt(TR(5))))
      << "modifier order is significant";
  EXPECT_FALSE(A == I4.withModifier(modopt(TR(4)))) << "modreq is not modopt";
  EXPECT_FALSE(A == I4);

  // A modified pointee and a modified pointer.
  TypeSig PtrToMod = TypeSig::getPointer(I4.withModifier(modopt(TR(5))));
  TypeSig ModPtr = TypeSig::getPointer(I4).withModifier(modopt(TR(5)));
  checkField(PtrToMod, {0x06, 0x0F, 0x20, 0x15, 0x08});
  checkField(ModPtr, {0x06, 0x20, 0x15, 0x0F, 0x08});
  EXPECT_EQ(print(PtrToMod),
            "int32 modopt([mscorlib]System.Runtime.CompilerServices.IsConst)*");
  EXPECT_EQ(print(ModPtr),
            "int32* modopt([mscorlib]System.Runtime.CompilerServices.IsConst)");

  // csc: 'public static ref readonly char RefReadonly(string s)' has the
  // signature 00 01 1F 41 10 03 0E (InAttribute is TypeRef row 0x10), the
  // shape of bcl-binding.md 3.4's GetPinnableReference.
  MethodSig RO = method(
      SIG_DEFAULT,
      TypeSig::getByRef(P(ELEMENT_TYPE_CHAR)).withModifier(modreq(TR(0x10))),
      {Str});
  checkMethod(RO, {0x00, 0x01, 0x1F, 0x41, 0x10, 0x03, 0x0E});
  EXPECT_EQ(print(RO), "char& modreq([System.Runtime]System.Runtime."
                       "InteropServices.InAttribute)(string)");
  EXPECT_FALSE(
      RO == method(SIG_DEFAULT, TypeSig::getByRef(P(ELEMENT_TYPE_CHAR)), {Str}))
      << "the binder must keep modifiers";

  // A modified return type and parameter.
  checkMethod(method(SIG_DEFAULT, Void.withModifier(modopt(TR(5))),
                     {I4.withModifier(modreq(TR(4)))}),
              {0x00, 0x01, 0x20, 0x15, 0x01, 0x1F, 0x11, 0x08});
}

// II.23.2.6: LocalVarSig with PINNED before BYREF.
TEST(CLISignatureTest, PinnedLocals) {
  TypeSig Pinned = TypeSig::getPinned(TypeSig::getByRef(P(ELEMENT_TYPE_U1)));
  EXPECT_EQ(print(Pinned), "uint8& pinned");
  checkLocals({Pinned, TypeSig::getPointer(Void)},
              {0x07, 0x02, 0x45, 0x10, 0x05, 0x0F, 0x01});
  checkLocals({TypeSig::getPinned(Str)}, {0x07, 0x01, 0x45, 0x0E});
  EXPECT_FALSE(Pinned == TypeSig::getByRef(P(ELEMENT_TYPE_U1)));
}

// II.23.2.2: a vararg call site's MethodRefSig puts SENTINEL before the first
// optional parameter; a vararg definition has none.
TEST(CLISignatureTest, SentinelAtVarargCallSites) {
  MethodSig Call = method(SIG_VARARG, Void, {I4, I4, P(ELEMENT_TYPE_R8)}, 1);
  checkMethod(Call, {0x05, 0x03, 0x01, 0x08, 0x41, 0x08, 0x0D});
  EXPECT_EQ(print(Call), "vararg void(int32, ..., int32, float64)");

  MethodSig AllOptional = method(SIG_VARARG, I4, {I4, I}, 0);
  checkMethod(AllOptional, {0x05, 0x02, 0x08, 0x41, 0x08, 0x18});
  EXPECT_EQ(print(AllOptional), "vararg int32(..., int32, native int)");

  MethodSig Def = method(SIG_VARARG, Void, {I4});
  checkMethod(Def, {0x05, 0x01, 0x01, 0x08});
  EXPECT_EQ(print(Def), "vararg void(int32)");
  Expected<MethodSig> D = decodeMethodSig(ByteVec{0x05, 0x01, 0x01, 0x08});
  ASSERT_THAT_EXPECTED(D, Succeeded());
  EXPECT_EQ(D->RequiredParamCount, UINT32_MAX);
  EXPECT_FALSE(Call ==
               method(SIG_VARARG, Void, {I4, I4, P(ELEMENT_TYPE_R8)}, 2));

  // A C call site through calli may also have optional arguments.
  MethodSig CCall = method(SIG_C, I4, {I, I4}, 1);
  checkMethod(CCall, {0x01, 0x02, 0x08, 0x18, 0x41, 0x08});
  EXPECT_EQ(print(CCall), "unmanaged cdecl int32(native int, ..., int32)");
}

TEST(CLISignatureTest, CallingConventionsAndThis) {
  struct {
    uint8_t Header;
    const char *Text;
  } Cases[] = {
      {SIG_DEFAULT, "void(int32)"},
      {SIG_C, "unmanaged cdecl void(int32)"},
      {SIG_STDCALL, "unmanaged stdcall void(int32)"},
      {SIG_THISCALL, "unmanaged thiscall void(int32)"},
      {SIG_FASTCALL, "unmanaged fastcall void(int32)"},
      {SIG_VARARG, "vararg void(int32)"},
      {SIG_UNMANAGED, "unmanaged void(int32)"},
      {SIG_HASTHIS, "instance void(int32)"},
      {SIG_HASTHIS | SIG_EXPLICITTHIS, "instance explicit void(int32)"},
      {SIG_HASTHIS | SIG_C, "instance unmanaged cdecl void(int32)"},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.Text);
    MethodSig S = method(C.Header, Void, {I4});
    checkMethod(S, {C.Header, 0x01, 0x01, 0x08});
    EXPECT_EQ(print(S), C.Text);
  }
  EXPECT_FALSE(method(SIG_C, Void, {}) == method(SIG_STDCALL, Void, {}));
}

// II.23.2.5: PROPERTY [| HASTHIS] ParamCount Type Param*.
TEST(CLISignatureTest, PropertySignatures) {
  struct {
    bool HasThis;
    TypeSig Type;
    std::vector<TypeSig> Params;
    ByteVec Bytes;
  } Cases[] = {
      {true, I4, {}, {0x28, 0x00, 0x08}},
      {false, Str, {I4, I4}, {0x08, 0x02, 0x0E, 0x08, 0x08}},
      {true, TypeSig::getClass(TR(1)), {Str}, {0x28, 0x01, 0x12, 0x05, 0x0E}},
  };
  for (const auto &C : Cases) {
    PropertySig S;
    S.HasThis = C.HasThis;
    S.Type = C.Type;
    S.Params = C.Params;
    SmallVector<uint8_t, 16> Out;
    encodePropertySig(S, Out);
    EXPECT_EQ(toVec(Out), C.Bytes);
    Expected<PropertySig> D = decodePropertySig(C.Bytes);
    ASSERT_THAT_EXPECTED(D, Succeeded());
    EXPECT_EQ(D->HasThis, C.HasThis);
    EXPECT_TRUE(D->Type == C.Type);
    EXPECT_TRUE(D->Params == C.Params);
  }
}

//===----------------------------------------------------------------------===//
// Decode errors
//===----------------------------------------------------------------------===//

TEST(CLISignatureTest, DecodeErrors) {
  // Truncation.
  expectInvalid(decodeMethodSig(ByteVec{}));
  expectInvalid(decodeMethodSig(ByteVec{0x00}));
  expectInvalid(decodeMethodSig(ByteVec{0x00, 0x00}));
  expectInvalid(decodeMethodSig(ByteVec{0x00, 0x01, 0x08}));
  expectInvalid(decodeMethodSig(ByteVec{0x10, 0x01}));
  expectInvalid(decodeFieldSig(ByteVec{0x06}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x12}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x0F}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x14, 0x08, 0x02, 0x01}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x15, 0x12, 0x09, 0x02, 0x08}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x1B, 0x00, 0x01, 0x01}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x1F, 0x05}));
  expectInvalid(decodeLocalSig(ByteVec{0x07, 0x02, 0x08}));
  expectInvalid(decodePropertySig(ByteVec{0x28, 0x01, 0x08}));
  expectInvalid(decodeTypeSpec(ByteVec{}));
  expectInvalid(decodeMethodSpec(ByteVec{0x0A, 0x02, 0x08}));
  // A bad compressed integer (lead byte 111xxxxx).
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x12, 0xFF}));
  expectInvalid(decodeMethodSig(ByteVec{0x00, 0xE0, 0x01}));
  // Unknown element types.
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x17}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x1A}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x60}));
  expectInvalid(decodeMethodSig(ByteVec{0x00, 0x01, 0x01, 0x3F}));
  // TypeDefOrRefOrSpecEncoded tag 3.
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x12, 0x07}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x1F, 0x07, 0x08}));
  // Trailing bytes.
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x08, 0x08}));
  expectInvalid(decodeMethodSig(ByteVec{0x00, 0x00, 0x08, 0x00}));
  expectInvalid(decodeLocalSig(ByteVec{0x07, 0x01, 0x08, 0x08}));
  expectInvalid(decodeTypeSpec(ByteVec{0x08, 0x08}));
  expectInvalid(decodeMethodSpec(ByteVec{0x0A, 0x01, 0x08, 0x08}));
  expectInvalid(decodePropertySig(ByteVec{0x08, 0x00, 0x08, 0x00}));
  // Misplaced SENTINEL: in a field, as the return type, twice, or with no
  // parameter after it.
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x41, 0x08}));
  expectInvalid(decodeMethodSig(ByteVec{0x05, 0x01, 0x41, 0x08}));
  expectInvalid(
      decodeMethodSig(ByteVec{0x05, 0x03, 0x01, 0x08, 0x41, 0x08, 0x41, 0x08}));
  expectInvalid(decodeMethodSig(ByteVec{0x05, 0x01, 0x01, 0x08, 0x41}));
  // A header of the wrong kind.
  expectInvalid(decodeFieldSig(ByteVec{0x00, 0x08}));
  expectInvalid(decodeLocalSig(ByteVec{0x06, 0x01, 0x08}));
  expectInvalid(decodeMethodSig(ByteVec{0x06, 0x08}));
  expectInvalid(decodePropertySig(ByteVec{0x00, 0x00, 0x08}));
  expectInvalid(decodeMethodSpec(ByteVec{0x07, 0x01, 0x08}));
}

// The shapes the encoders assert on are decode errors, so that a decode
// followed by an encode never asserts (section 12 '5.7.3 clarified (signature
// decode errors)'); the expected cause words are the row's.
TEST(CLISignatureTest, DecodeErrorsForShapesEncodersReject) {
  // An ARRAY of rank 0.
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x14, 0x08, 0x00, 0x00, 0x00}),
                "rank");
  expectInvalid(decodeTypeSpec(ByteVec{0x14, 0x08, 0x00, 0x00, 0x00}), "rank");
  // An ARRAY of rank above 32, the CLR's MAX_RANK (section 12 '5.7.3 and
  // 5.7.11 amended (signature decode errors in both dumpers)'): ranks 33, 256
  // (81 00) and 0x1FFFFFFF (DF FF FF FF) in a TypeSpec, rank 33 in a field, a
  // local and a parameter.
  expectInvalid(decodeTypeSpec(ByteVec{0x14, 0x08, 0x21, 0x00, 0x00}), "rank");
  expectInvalid(decodeTypeSpec(ByteVec{0x14, 0x08, 0x81, 0x00, 0x00, 0x00}),
                "rank");
  expectInvalid(
      decodeTypeSpec(ByteVec{0x14, 0x08, 0xDF, 0xFF, 0xFF, 0xFF, 0x00, 0x00}),
      "rank");
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x14, 0x08, 0x21, 0x00, 0x00}),
                "rank");
  expectInvalid(
      decodeLocalSig(ByteVec{0x07, 0x01, 0x14, 0x08, 0x21, 0x00, 0x00}),
      "rank");
  expectInvalid(
      decodeMethodSig(ByteVec{0x00, 0x01, 0x01, 0x14, 0x08, 0x21, 0x00, 0x00}),
      "rank");
  // A GENERICINST without arguments (a TypeSpec of List`1 with 0 arguments).
  expectInvalid(decodeTypeSpec(ByteVec{0x15, 0x12, 0x09, 0x00}), "argument");
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x15, 0x11, 0x0D, 0x00}),
                "argument");
  // A MethodSpec without arguments.
  expectInvalid(decodeMethodSpec(ByteVec{0x0A, 0x00}), "argument");
  // PINNED anywhere but at the top level of a local: a field, a parameter,
  // under PTR in a local, a return type, a generic argument.
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x45, 0x08}), "pinned");
  expectInvalid(decodeMethodSig(ByteVec{0x00, 0x01, 0x01, 0x45, 0x08}),
                "pinned");
  expectInvalid(decodeLocalSig(ByteVec{0x07, 0x01, 0x0F, 0x45, 0x08}),
                "pinned");
  expectInvalid(decodeMethodSig(ByteVec{0x00, 0x00, 0x45, 0x08}));
  expectInvalid(decodeMethodSpec(ByteVec{0x0A, 0x01, 0x45, 0x08}));
  // A SENTINEL in a method signature whose calling convention is neither
  // VARARG nor C: DEFAULT, STDCALL and a HASTHIS DEFAULT.
  expectInvalid(decodeMethodSig(ByteVec{0x00, 0x02, 0x01, 0x08, 0x41, 0x08}),
                "sentinel");
  expectInvalid(decodeMethodSig(ByteVec{0x02, 0x02, 0x01, 0x08, 0x41, 0x08}),
                "sentinel");
  expectInvalid(decodeMethodSig(ByteVec{0x20, 0x02, 0x01, 0x08, 0x41, 0x08}),
                "sentinel");
  // A byte other than CLASS or VALUETYPE after GENERICINST (it must not be
  // re-encoded as 0x12).
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x15, 0x1F, 0x09, 0x01, 0x08}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x15, 0x08, 0x09, 0x01, 0x08}));
  expectInvalid(decodeTypeSpec(ByteVec{0x15, 0x15, 0x09, 0x01, 0x08}));
  // A TypeDefOrRefOrSpecEncoded value with row 0: a CLASS operand, a
  // VALUETYPE operand, a modifier type and a GENERICINST type.
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x12, 0x01}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x11, 0x00}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x1F, 0x01, 0x08}));
  expectInvalid(decodeFieldSig(ByteVec{0x06, 0x15, 0x12, 0x02, 0x01, 0x08}));

  // The legal neighbours still decode: PINNED at the top of a local, a
  // SENTINEL in VARARG and C call sites, rank 1, one generic argument.
  Expected<std::vector<TypeSig>> L =
      decodeLocalSig(ByteVec{0x07, 0x01, 0x45, 0x0F, 0x08});
  ASSERT_THAT_EXPECTED(L, Succeeded());
  EXPECT_EQ(print((*L)[0]), "int32* pinned");
  EXPECT_THAT_EXPECTED(
      decodeMethodSig(ByteVec{0x05, 0x02, 0x01, 0x08, 0x41, 0x08}),
      Succeeded());
  EXPECT_THAT_EXPECTED(
      decodeMethodSig(ByteVec{0x01, 0x02, 0x01, 0x08, 0x41, 0x08}),
      Succeeded());
  EXPECT_THAT_EXPECTED(decodeTypeSpec(ByteVec{0x14, 0x08, 0x01, 0x00, 0x00}),
                       Succeeded());
  EXPECT_THAT_EXPECTED(decodeTypeSpec(ByteVec{0x15, 0x12, 0x09, 0x01, 0x08}),
                       Succeeded());

  // Rank 32 decodes, and prints its 32 dimensions without sizes or lower
  // bounds as '<t>[', 31 commas and ']' (section 12 '5.7.10 amended (general
  // arrays)'); it round-trips in a field, a local and a parameter.
  TypeSig Rank32 = TypeSig::getArray(I4, shape(32));
  std::string Rank32Text = "int32[" + std::string(31, ',') + "]";
  Expected<TypeSig> T = decodeTypeSpec(ByteVec{0x14, 0x08, 0x20, 0x00, 0x00});
  ASSERT_THAT_EXPECTED(T, Succeeded());
  EXPECT_EQ(T->getElementType(), ELEMENT_TYPE_ARRAY);
  EXPECT_EQ(T->getArrayShape().Rank, 32u);
  EXPECT_TRUE(T->getArrayShape().Sizes.empty());
  EXPECT_TRUE(T->getArrayShape().LowerBounds.empty());
  EXPECT_EQ(print(*T), Rank32Text);
  EXPECT_TRUE(*T == Rank32);
  checkField(Rank32, {0x06, 0x14, 0x08, 0x20, 0x00, 0x00});
  checkLocals({Rank32}, {0x07, 0x01, 0x14, 0x08, 0x20, 0x00, 0x00});
  MethodSig M = method(SIG_DEFAULT, Void, {Rank32});
  checkMethod(M, {0x00, 0x01, 0x01, 0x14, 0x08, 0x20, 0x00, 0x00});
  EXPECT_EQ(print(M), "void(" + Rank32Text + ")");
}

// Counts that promise more than the blob holds are truncation errors (and
// never a huge allocation); nesting deeper than 64 levels is an error.
TEST(CLISignatureTest, DecodeErrorsCountsAndDepth) {
  expectInvalid(decodeMethodSig(ByteVec{0x00, 0xDF, 0xFF, 0xFF, 0xFF, 0x01}));
  expectInvalid(decodeLocalSig(ByteVec{0x07, 0xDF, 0xFF, 0xFF, 0xFF, 0x08}));
  expectInvalid(decodeMethodSpec(ByteVec{0x0A, 0xDF, 0xFF, 0xFF, 0xFF, 0x08}));
  expectInvalid(
      decodeFieldSig(ByteVec{0x06, 0x15, 0x12, 0x05, 0xDF, 0xFF, 0xFF, 0xFF}));
  expectInvalid(
      decodeFieldSig(ByteVec{0x06, 0x14, 0x08, 0x01, 0xDF, 0xFF, 0xFF, 0xFF}));
  expectInvalid(decodePropertySig(ByteVec{0x08, 0xDF, 0xFF, 0xFF, 0xFF, 0x08}));

  ByteVec Shallow = {0x06};
  Shallow.insert(Shallow.end(), 10, 0x0F);
  Shallow.push_back(0x08);
  Expected<TypeSig> S = decodeFieldSig(Shallow);
  ASSERT_THAT_EXPECTED(S, Succeeded());
  EXPECT_EQ(print(*S), "int32**********");

  ByteVec DeepPointers = {0x06};
  DeepPointers.insert(DeepPointers.end(), 200, 0x0F);
  DeepPointers.push_back(0x08);
  expectInvalid(decodeFieldSig(DeepPointers));

  ByteVec DeepArrays = {0x06};
  DeepArrays.insert(DeepArrays.end(), 1000, 0x1D);
  DeepArrays.push_back(0x08);
  expectInvalid(decodeFieldSig(DeepArrays));

  // Function pointers returning function pointers.
  ByteVec DeepFnPtrs = {0x06};
  for (int K = 0; K != 100; ++K)
    DeepFnPtrs.insert(DeepFnPtrs.end(), {0x1B, 0x00, 0x00});
  DeepFnPtrs.push_back(0x01);
  expectInvalid(decodeFieldSig(DeepFnPtrs));

  // Generic arguments nested in generic arguments.
  ByteVec DeepGenerics = {0x06};
  for (int K = 0; K != 100; ++K)
    DeepGenerics.insert(DeepGenerics.end(), {0x15, 0x12, 0x09, 0x01});
  DeepGenerics.push_back(0x08);
  expectInvalid(decodeFieldSig(DeepGenerics));
}

//===----------------------------------------------------------------------===//
// mapTypeRefs
//===----------------------------------------------------------------------===//

// Every TypeRef position: CLASS and VALUETYPE operands, the GENERICINST type
// and its arguments, modifier types, the return and parameter types of a
// function pointer, and the elements of pointers, byrefs and arrays. The
// signature is built with client ids (not tokens), mapped to TypeRef tokens,
// and only then encoded.
TEST(CLISignatureTest, MapTypeRefsReachesEveryPosition) {
  MethodSig Client = method(
      SIG_DEFAULT, TypeSig::getClass(1),
      {
          TypeSig::getValueType(2),
          TypeSig::getGenericInst(
              false, 3, {TypeSig::getClass(4), TypeSig::getValueType(5)}),
          I4.withModifier(modreq(6)).withModifier(modopt(7)),
          TypeSig::getFunctionPointer(
              method(SIG_C, TypeSig::getClass(8),
                     {TypeSig::getValueType(9).withModifier(modopt(10))})),
          TypeSig::getPointer(TypeSig::getValueType(11)),
          TypeSig::getByRef(TypeSig::getClass(12)),
          TypeSig::getSZArray(TypeSig::getClass(13)),
          TypeSig::getArray(TypeSig::getValueType(14), shape(2)),
          I4,
      });
  MethodSig Before = Client;
  std::vector<uint32_t> Seen;
  MethodSig Mapped = mapTypeRefs(Client, [&](uint32_t Id) {
    Seen.push_back(Id);
    return TR(Id);
  });
  EXPECT_TRUE(Client == Before) << "the input is not modified";
  std::sort(Seen.begin(), Seen.end());
  Seen.erase(std::unique(Seen.begin(), Seen.end()), Seen.end());
  EXPECT_EQ(Seen, (std::vector<uint32_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                         13, 14}));
  // TypeRef row r is (r << 2) | 1.
  checkMethod(Mapped, {0x00, 0x09,                                     //
                       0x12, 0x05,                                     // ret
                       0x11, 0x09,                                     // p1
                       0x15, 0x12, 0x0D, 0x02, 0x12, 0x11, 0x11, 0x15, // p2
                       0x1F, 0x19, 0x20, 0x1D, 0x08,                   // p3
                       0x1B, 0x01, 0x01, 0x12, 0x21, 0x20, 0x29, 0x11,
                       0x25,                         // p4
                       0x0F, 0x11, 0x2D,             // p5
                       0x10, 0x12, 0x31,             // p6
                       0x1D, 0x12, 0x35,             // p7
                       0x14, 0x11, 0x39, 0x02, 0x00, // p8
                       0x00,                         //
                       0x08});                       // p9
  EXPECT_EQ(Mapped.Header, Client.Header);
  EXPECT_EQ(Mapped.RequiredParamCount, Client.RequiredParamCount);
}

TEST(CLISignatureTest, MapTypeRefsOnTypes) {
  auto ToTypeSpec = [](uint32_t Id) { return TS(Id); };
  TypeSig G = TypeSig::getGenericInst(true, 3, {TypeSig::getClass(4), I4});
  TypeSig M = mapTypeRefs(G, ToTypeSpec);
  EXPECT_EQ(M.getTypeRef(), TS(3));
  ASSERT_EQ(M.getOperands().size(), 2u);
  EXPECT_EQ(M.getOperands()[0].getTypeRef(), TS(4));
  EXPECT_TRUE(M.getOperands()[1] == I4);
  EXPECT_EQ(G.getTypeRef(), 3u) << "the input is not modified";
  checkField(M, {0x06, 0x15, 0x11, 0x0E, 0x02, 0x12, 0x12, 0x08});

  // Types without a TypeRef map to themselves without calling Map.
  unsigned Calls = 0;
  TypeSig Plain = TypeSig::getPointer(TypeSig::getSZArray(Str));
  TypeSig Same = mapTypeRefs(Plain, [&](uint32_t Id) {
    ++Calls;
    return Id;
  });
  EXPECT_TRUE(Same == Plain);
  EXPECT_EQ(Calls, 0u);

  // The header, generic parameter count and sentinel position are kept.
  MethodSig V = method(SIG_VARARG | SIG_HASTHIS, TypeSig::getClass(7),
                       {I4, TypeSig::getValueType(8)}, 1);
  MethodSig VM = mapTypeRefs(V, [](uint32_t Id) { return TD(Id); });
  EXPECT_EQ(VM.Header, SIG_VARARG | SIG_HASTHIS);
  EXPECT_EQ(VM.RequiredParamCount, 1u);
  checkMethod(VM, {0x25, 0x02, 0x12, 0x1C, 0x08, 0x41, 0x11, 0x20});
  MethodSig GM = method(SIG_GENERIC, TypeSig::getClass(1), {});
  GM.GenericParamCount = 3;
  EXPECT_EQ(
      mapTypeRefs(GM, [](uint32_t Id) { return TR(Id); }).GenericParamCount,
      3u);
}

//===----------------------------------------------------------------------===//
// Value semantics
//===----------------------------------------------------------------------===//

TypeSig buildComplex() {
  return TypeSig::getFunctionPointer(method(
      SIG_C | SIG_HASTHIS,
      TypeSig::getGenericInst(false, TR(2),
                              {TypeSig::getArray(I4, shape(2, {3}, {-1}))}),
      {TypeSig::getByRef(TypeSig::getClass(TR(1)).withModifier(modopt(TR(5)))),
       TypeSig::getGenericParam(true, 2)},
      1));
}

TEST(CLISignatureTest, EqualityAndHashing) {
  TypeSig A = buildComplex(), B = buildComplex();
  EXPECT_TRUE(A == B);
  EXPECT_EQ(hash_value(A), hash_value(B));
  TypeSig C = A; // copies are equal and independent
  EXPECT_TRUE(C == A);
  C = C.withModifier(modreq(TR(4)));
  EXPECT_FALSE(C == A);
  EXPECT_TRUE(A == B);

  MethodSig M1 = method(SIG_HASTHIS, I4, {Str, TypeSig::getClass(TR(1))});
  MethodSig M2 = method(SIG_HASTHIS, I4, {Str, TypeSig::getClass(TR(1))});
  EXPECT_TRUE(M1 == M2);
  EXPECT_EQ(hash_value(M1), hash_value(M2));
  MethodSig M3 = M1;
  M3.Header = SIG_DEFAULT;
  EXPECT_FALSE(M1 == M3);
  MethodSig M4 = M1;
  M4.Params.pop_back();
  EXPECT_FALSE(M1 == M4);
  MethodSig M5 = M1;
  M5.Return = I;
  EXPECT_FALSE(M1 == M5);
  MethodSig M6 = M1;
  M6.Header |= SIG_GENERIC;
  M6.GenericParamCount = 1;
  MethodSig M7 = M6;
  M7.GenericParamCount = 2;
  EXPECT_FALSE(M6 == M7);

  // Structurally different types.
  EXPECT_FALSE(TypeSig::getClass(TR(1)) == TypeSig::getValueType(TR(1)));
  EXPECT_FALSE(TypeSig::getClass(TR(1)) == TypeSig::getClass(TR(2)));
  EXPECT_FALSE(TypeSig::getPointer(I4) == TypeSig::getByRef(I4));
  EXPECT_FALSE(TypeSig::getSZArray(I4) == TypeSig::getSZArray(I));
  EXPECT_FALSE(P(ELEMENT_TYPE_I4) == P(ELEMENT_TYPE_U4));
  EXPECT_FALSE(TypeSig::getGenericInst(false, TR(2), {I4}) ==
               TypeSig::getGenericInst(false, TR(2), {I4, I4}));
  // Equal content built through different paths hashes equally.
  EXPECT_EQ(hash_value(TypeSig::getPointer(TypeSig())),
            hash_value(TypeSig::getPointer(TypeSig::get(ELEMENT_TYPE_VOID))));
}

TEST(CLISignatureTest, Accessors) {
  TypeSig Cls = TypeSig::getClass(TR(5));
  EXPECT_EQ(Cls.getElementType(), ELEMENT_TYPE_CLASS);
  EXPECT_EQ(Cls.getTypeRef(), TR(5));
  TypeSig VT = TypeSig::getValueType(TD(9));
  EXPECT_EQ(VT.getElementType(), ELEMENT_TYPE_VALUETYPE);
  EXPECT_EQ(VT.getTypeRef(), TD(9));
  TypeSig Ptr = TypeSig::getPointer(I4);
  EXPECT_EQ(Ptr.getElementType(), ELEMENT_TYPE_PTR);
  ASSERT_EQ(Ptr.getOperands().size(), 1u);
  EXPECT_TRUE(Ptr.getOperands()[0] == I4);
  TypeSig Ref = TypeSig::getByRef(Str);
  EXPECT_EQ(Ref.getElementType(), ELEMENT_TYPE_BYREF);
  ASSERT_EQ(Ref.getOperands().size(), 1u);
  EXPECT_TRUE(Ref.getOperands()[0] == Str);
  TypeSig Arr = TypeSig::getSZArray(VT);
  EXPECT_EQ(Arr.getElementType(), ELEMENT_TYPE_SZARRAY);
  ASSERT_EQ(Arr.getOperands().size(), 1u);
  EXPECT_TRUE(Arr.getOperands()[0] == VT);
  MethodSig FS = method(SIG_STDCALL, Void, {Ptr});
  EXPECT_TRUE(TypeSig::getFunctionPointer(FS).getFunctionSig() == FS);
}

} // namespace
