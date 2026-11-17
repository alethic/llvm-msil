//===-- MSILMCTypeUtilities.cpp - MSIL type and name utilities ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// CIL value types, method signatures, and the ILAsm spellings of types and
/// identifiers.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <tuple>

using namespace llvm;

bool MSIL::Signature::operator==(const Signature &RHS) const {
  return Results == RHS.Results && Params == RHS.Params && CC == RHS.CC;
}

bool MSIL::Signature::operator<(const Signature &RHS) const {
  return std::tie(Results, Params, CC) <
         std::tie(RHS.Results, RHS.Params, RHS.CC);
}

MSIL::ValType MSIL::toValType(MVT VT, unsigned PointerSizeInBits) {
  switch (VT.SimpleTy) {
  case MVT::i32:
    return ValType::I32;
  case MVT::i64:
    return PointerSizeInBits == 64 ? ValType::NativeInt : ValType::I64;
  case MVT::f32:
    return ValType::F32;
  case MVT::f64:
    return ValType::F64;
  default:
    llvm_unreachable("MSIL: value type has no CIL type");
  }
}

StringRef MSIL::getValTypeName(ValType T) {
  switch (T) {
  case ValType::I32:
    return "int32";
  case ValType::NativeInt:
    return "native int";
  case ValType::I64:
    return "int64";
  case ValType::F32:
    return "float32";
  case ValType::F64:
    return "float64";
  }
  llvm_unreachable("MSIL: unknown ValType");
}

void MSIL::printValType(raw_ostream &OS, ValType T) { OS << getValTypeName(T); }

// Print the narrow integer \p Exact of an exact parameter or result type
// (Signature::ExactParams, Signature::ExactReturn), or \p T if there is none.
static void printExactType(raw_ostream &OS, cli::ElementType Exact,
                           MSIL::ValType T) {
  switch (Exact) {
  case cli::ELEMENT_TYPE_END:
    MSIL::printValType(OS, T);
    return;
  case cli::ELEMENT_TYPE_I1:
    OS << "int8";
    return;
  case cli::ELEMENT_TYPE_U1:
    OS << "unsigned int8";
    return;
  case cli::ELEMENT_TYPE_I2:
    OS << "int16";
    return;
  case cli::ELEMENT_TYPE_U2:
    OS << "unsigned int16";
    return;
  default:
    llvm_unreachable("MSIL: an exact parameter or result type is a narrow "
                     "integer");
  }
}

void MSIL::printResult(raw_ostream &OS, const Signature &S) {
  assert(S.Results.size() <= 1 && "a CIL method has at most one result");
  assert((!S.Results.empty() || S.ExactReturn == cli::ELEMENT_TYPE_END) &&
         "an exact result type needs a result");
  if (S.Results.empty())
    OS << "void";
  else
    printExactType(OS, S.ExactReturn, S.Results.front());
}

void MSIL::printParamList(raw_ostream &OS, const Signature &S) {
  assert((S.ExactParams.empty() || S.ExactParams.size() == S.Params.size()) &&
         "an exact type for every parameter or for none");
  OS << '(';
  interleaveComma(seq(S.Params.size()), OS, [&](size_t I) {
    printExactType(OS, S.getExactParam(I), S.Params[I]);
  });
  OS << ')';
}

// A backslash followed by three octal digits: the escape ilasm reads in both
// single- and double-quoted strings.
static void printOctalEscape(raw_ostream &OS, unsigned char C) {
  OS << '\\' << char('0' + (C >> 6)) << char('0' + ((C >> 3) & 7))
     << char('0' + (C & 7));
}

void MSIL::printQuotedName(raw_ostream &OS, StringRef Name) {
  OS << '\'';
  for (unsigned char C : Name.bytes()) {
    if (C == '\'')
      OS << "\\'";
    else if (C == '\\' || C < 0x20 || C == 0x7f)
      printOctalEscape(OS, C);
    else
      OS << C;
  }
  OS << '\'';
}

void MSIL::printQuotedString(raw_ostream &OS, StringRef Str) {
  OS << '"';
  for (unsigned char C : Str.bytes()) {
    if (C == '"' || C == '\\' || C < 0x20 || C == 0x7f)
      printOctalEscape(OS, C);
    else
      OS << C;
  }
  OS << '"';
}
