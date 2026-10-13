//===- MSILTypeUtilitiesTest.cpp - Tests for the MSIL type utilities ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The value types and signatures of MCTargetDesc/MSILMCTypeUtilities.h
// (ImplementationPlan.md 5.3): an i64 is 'native int' on 64-bit triples
// (decision D2), the CIL type names, the result and parameter-list text of
// method signatures (output contract 5.1), and the equality and strict weak
// ordering that let signatures be interned in a std::set.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace llvm;
using MSIL::CallConv;
using MSIL::Signature;
using MSIL::ValType;

namespace {

Signature makeSig(ArrayRef<ValType> Results, ArrayRef<ValType> Params,
                  CallConv CC = CallConv::Managed) {
  Signature S;
  S.Results.append(Results.begin(), Results.end());
  S.Params.append(Params.begin(), Params.end());
  S.CC = CC;
  return S;
}

std::string valTypeText(ValType T) {
  std::string S;
  raw_string_ostream OS(S);
  MSIL::printValType(OS, T);
  return OS.str();
}

std::string resultText(const Signature &Sig) {
  std::string S;
  raw_string_ostream OS(S);
  MSIL::printResult(OS, Sig);
  return OS.str();
}

std::string paramText(const Signature &Sig) {
  std::string S;
  raw_string_ostream OS(S);
  MSIL::printParamList(OS, Sig);
  return OS.str();
}

// Pairwise different signatures: they differ in the results, the parameters
// (types, order or count) or only in the calling convention.
std::vector<Signature> distinctSignatures() {
  return {
      makeSig({}, {}),
      makeSig({}, {ValType::I32}),
      makeSig({ValType::I32}, {}),
      makeSig({ValType::I32}, {ValType::I32}),
      makeSig({ValType::I32}, {ValType::NativeInt, ValType::I32}),
      makeSig({ValType::I32}, {ValType::I32, ValType::NativeInt}),
      makeSig({ValType::NativeInt}, {ValType::NativeInt}),
      makeSig({ValType::NativeInt}, {ValType::NativeInt},
              CallConv::UnmanagedCdecl),
      makeSig({ValType::NativeInt}, {ValType::NativeInt},
              CallConv::UnmanagedStdcall),
      makeSig({ValType::F64}, {ValType::F32, ValType::F64}),
      makeSig({ValType::F32}, {ValType::F32, ValType::F64}),
      makeSig({}, {ValType::I64}),
      makeSig({}, {ValType::I32, ValType::I32}),
      makeSig({}, {ValType::I32, ValType::I32, ValType::I32, ValType::I32,
                   ValType::I32}),
  };
}

TEST(MSILTypeUtilitiesTest, MSILToValType) {
  // Decision D2: on a 64-bit triple every i64 (pointers included) is a
  // CIL 'native int'.
  EXPECT_EQ(ValType::I32, MSIL::toValType(MVT::i32, 64));
  EXPECT_EQ(ValType::NativeInt, MSIL::toValType(MVT::i64, 64));
  EXPECT_EQ(ValType::F32, MSIL::toValType(MVT::f32, 64));
  EXPECT_EQ(ValType::F64, MSIL::toValType(MVT::f64, 64));
  // The pointer size changes nothing for the other types.
  EXPECT_EQ(ValType::I32, MSIL::toValType(MVT::i32, 32));
  EXPECT_EQ(ValType::F32, MSIL::toValType(MVT::f32, 32));
  EXPECT_EQ(ValType::F64, MSIL::toValType(MVT::f64, 32));
}

TEST(MSILTypeUtilitiesTest, MSILValTypeNames) {
  EXPECT_EQ("int32", MSIL::getValTypeName(ValType::I32));
  EXPECT_EQ("native int", MSIL::getValTypeName(ValType::NativeInt));
  EXPECT_EQ("int64", MSIL::getValTypeName(ValType::I64));
  EXPECT_EQ("float32", MSIL::getValTypeName(ValType::F32));
  EXPECT_EQ("float64", MSIL::getValTypeName(ValType::F64));
}

TEST(MSILTypeUtilitiesTest, MSILPrintValTypeMatchesName) {
  for (ValType T : {ValType::I32, ValType::NativeInt, ValType::I64,
                    ValType::F32, ValType::F64})
    EXPECT_EQ(MSIL::getValTypeName(T).str(), valTypeText(T));
}

TEST(MSILTypeUtilitiesTest, MSILPrintResult) {
  EXPECT_EQ("void", resultText(makeSig({}, {})));
  EXPECT_EQ("void", resultText(makeSig({}, {ValType::I32, ValType::F64})));
  EXPECT_EQ("int32", resultText(makeSig({ValType::I32}, {})));
  EXPECT_EQ("native int",
            resultText(makeSig({ValType::NativeInt}, {ValType::I32})));
  EXPECT_EQ("float32", resultText(makeSig({ValType::F32}, {})));
  EXPECT_EQ("float64", resultText(makeSig({ValType::F64}, {})));
  EXPECT_EQ("int64", resultText(makeSig({ValType::I64}, {})));
}

TEST(MSILTypeUtilitiesTest, MSILPrintParamList) {
  EXPECT_EQ("()", paramText(makeSig({}, {})));
  EXPECT_EQ("()", paramText(makeSig({ValType::I32}, {})));
  EXPECT_EQ("(int32)", paramText(makeSig({}, {ValType::I32})));
  EXPECT_EQ(
      "(native int, int32)",
      paramText(makeSig({ValType::I32}, {ValType::NativeInt, ValType::I32})));
  EXPECT_EQ("(float32, float64, int64, native int, int32)",
            paramText(makeSig({}, {ValType::F32, ValType::F64, ValType::I64,
                                   ValType::NativeInt, ValType::I32})));
}

TEST(MSILTypeUtilitiesTest, MSILMethodSignatureText) {
  // The pieces compose to the text of a call site or method header:
  // 'call int32 'sum'(native int, int32)' (output contract 5.1).
  Signature Sum = makeSig({ValType::I32}, {ValType::NativeInt, ValType::I32});
  EXPECT_EQ("int32(native int, int32)", resultText(Sum) + paramText(Sum));
  Signature Puts = makeSig({ValType::I32}, {ValType::NativeInt});
  EXPECT_EQ("int32(native int)", resultText(Puts) + paramText(Puts));
}

TEST(MSILTypeUtilitiesTest, MSILSignatureEquality) {
  std::vector<Signature> Sigs = distinctSignatures();
  for (size_t I = 0; I < Sigs.size(); ++I) {
    Signature Copy = Sigs[I];
    EXPECT_TRUE(Sigs[I] == Sigs[I]) << "signature " << I;
    EXPECT_TRUE(Copy == Sigs[I]) << "signature " << I;
    EXPECT_TRUE(Sigs[I] == Copy) << "signature " << I;
    for (size_t J = 0; J < Sigs.size(); ++J)
      if (I != J)
        EXPECT_FALSE(Sigs[I] == Sigs[J]) << "signatures " << I << ", " << J;
  }
}

TEST(MSILTypeUtilitiesTest, MSILSignatureEqualityIncludesCallConv) {
  Signature Managed = makeSig({ValType::I32}, {ValType::NativeInt});
  Signature Cdecl =
      makeSig({ValType::I32}, {ValType::NativeInt}, CallConv::UnmanagedCdecl);
  EXPECT_EQ(CallConv::Managed, Managed.CC);
  EXPECT_FALSE(Managed == Cdecl);
  EXPECT_TRUE(Managed < Cdecl || Cdecl < Managed);
  Cdecl.CC = CallConv::Managed;
  EXPECT_TRUE(Managed == Cdecl);
}

TEST(MSILTypeUtilitiesTest, MSILSignatureStrictWeakOrdering) {
  std::vector<Signature> Sigs = distinctSignatures();
  for (size_t I = 0; I < Sigs.size(); ++I) {
    // Irreflexive.
    EXPECT_FALSE(Sigs[I] < Sigs[I]) << "signature " << I;
    for (size_t J = 0; J < Sigs.size(); ++J) {
      if (I == J)
        continue;
      // Total on different signatures and asymmetric, which also makes
      // equivalence ('neither is less') coincide with equality.
      EXPECT_NE(Sigs[I] < Sigs[J], Sigs[J] < Sigs[I])
          << "signatures " << I << ", " << J;
      // Transitive.
      for (size_t K = 0; K < Sigs.size(); ++K)
        if (Sigs[I] < Sigs[J] && Sigs[J] < Sigs[K])
          EXPECT_TRUE(Sigs[I] < Sigs[K])
              << "signatures " << I << ", " << J << ", " << K;
    }
  }
}

TEST(MSILTypeUtilitiesTest, MSILSignaturesIntern) {
  // AsmPrinter::internSignature keeps calli signatures in a std::set.
  std::vector<Signature> Sigs = distinctSignatures();
  std::set<Signature> Interned;
  for (int Round = 0; Round < 2; ++Round)
    for (const Signature &S : Sigs)
      Interned.insert(S);
  EXPECT_EQ(Sigs.size(), Interned.size());
  for (const Signature &S : Sigs) {
    auto It = Interned.find(S);
    ASSERT_NE(Interned.end(), It);
    EXPECT_TRUE(*It == S);
  }
  // Sorting is deterministic: the reverse order sorts to the same sequence.
  std::vector<Signature> Forward = Sigs, Backward(Sigs.rbegin(), Sigs.rend());
  std::sort(Forward.begin(), Forward.end());
  std::sort(Backward.begin(), Backward.end());
  for (size_t I = 0; I < Forward.size(); ++I)
    EXPECT_TRUE(Forward[I] == Backward[I]) << "position " << I;
}

} // namespace
