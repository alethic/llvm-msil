//===- CallAdapter.cpp - MSIL call-site signature checks ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the call-site signature check and the adapters of
// ImplementationPlan.md 5.9.8 (see llvm/CLI/CallAdapter.h).
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/CallAdapter.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;
using namespace llvm::cli;

#define DEBUG_TYPE "cli-calladapter"

ValueClass cli::getValueClass(const TypeSig &T) {
  // A modified type (modreq, modopt) is another type to the runtime.
  if (!T.getModifiers().empty())
    return ValueClass::Other;
  switch (T.getElementType()) {
  case ELEMENT_TYPE_VOID:
    return ValueClass::Void;
  case ELEMENT_TYPE_I4:
  case ELEMENT_TYPE_U4:
    return ValueClass::Int32;
  case ELEMENT_TYPE_I:
  case ELEMENT_TYPE_U:
    return ValueClass::NativeInt;
  case ELEMENT_TYPE_R4:
    return ValueClass::Float32;
  case ELEMENT_TYPE_R8:
    return ValueClass::Float64;
  default:
    return ValueClass::Other;
  }
}

CallShape CallShape::get(const MethodSig &Sig,
                         const MSILLink::SignatureInfo *Info) {
  CallShape S;
  S.Sig = Sig;
  S.Result = getValueClass(Sig.Return);
  for (const TypeSig &P : Sig.Params)
    S.Params.push_back(getValueClass(P));
  if (Info) {
    S.Flags = Info->Flags;
    S.FixedParams = Info->FixedParams;
    S.SretSize = Info->SretSize;
    S.SretAlignLog2 = Info->SretAlignLog2;
    S.Byval = Info->Byval;
  }
  return S;
}

bool CallShape::isVarArg() const {
  return (Flags & MSILLink::VarArg) ||
         (Sig.Header & SIG_KIND_MASK) == SIG_VARARG ||
         Sig.RequiredParamCount < Sig.Params.size();
}

/// The ILAsm spelling of a value class in a diagnostic.
static StringRef getClassName(ValueClass C) {
  switch (C) {
  case ValueClass::Void:
    return "void";
  case ValueClass::Int32:
    return "int32";
  case ValueClass::NativeInt:
    return "native int";
  case ValueClass::Float32:
    return "float32";
  case ValueClass::Float64:
    return "float64";
  case ValueClass::Other:
    return "another type";
  }
  llvm_unreachable("unknown value class");
}

static bool isInteger(ValueClass C) {
  return C == ValueClass::Int32 || C == ValueClass::NativeInt;
}

static bool isSameByval(ArrayRef<MSILLink::ByvalParam> A,
                        ArrayRef<MSILLink::ByvalParam> B) {
  return llvm::equal(
      A, B, [](const MSILLink::ByvalParam &X, const MSILLink::ByvalParam &Y) {
        return X.Param == Y.Param && X.Size == Y.Size &&
               X.AlignLog2 == Y.AlignLog2;
      });
}

static std::string plural(unsigned N, StringRef Noun) {
  return (Twine(N) + " " + Noun + (N == 1 ? "" : "s")).str();
}

CallMatch cli::classifyCall(const CallShape &Call, const CallShape &Def) {
  CallMatch M;
  M.DefParams = Def.Params;
  M.DefResult = Def.Result;
  M.CallResult = Call.Result;
  auto Fail = [&M](const Twine &Why) {
    M.Kind = CallMatchKind::Error;
    M.Args.clear();
    M.DroppedArgs = 0;
    M.Result = ResultAction::Pass;
    M.Detail = Why.str();
    return M;
  };

  // P3-B's variadic buffer.
  if (Call.isVarArg() || Def.isVarArg())
    return Fail("variadic functions are not supported yet");

  // The ABI descriptors: where the native callee finds aggregates.
  bool CallSret = Call.Flags & MSILLink::Sret;
  bool DefSret = Def.Flags & MSILLink::Sret;
  if (CallSret != DefSret)
    return Fail(Twine("only the ") + (DefSret ? "definition" : "call") +
                " passes an aggregate result through a hidden pointer (sret)");
  if (!isSameByval(Call.Byval, Def.Byval))
    return Fail("the aggregates passed by value (byval) differ in position, "
                "size or alignment");

  // Value types, typed pointers and generic instances (P3 and later) bind
  // only by an exact match until FeatureIdeas 3 is decided; so do calling
  // conventions other than the default.
  auto HasOther = [](const CallShape &S) {
    return S.Result == ValueClass::Other ||
           is_contained(S.Params, ValueClass::Other);
  };
  if (HasOther(Call) || HasOther(Def) || Call.Sig.Header != Def.Sig.Header ||
      Call.Sig.GenericParamCount != Def.Sig.GenericParamCount) {
    if (Call.Sig == Def.Sig)
      return M;
    return Fail("the signatures differ in a type or calling convention that "
                "must match exactly");
  }

  SmallVector<std::string, 4> Notes;
  unsigned Missing = 0;
  for (size_t I = 0, E = Def.Params.size(); I != E; ++I) {
    if (I >= Call.Params.size()) {
      // x86-64 callees read garbage there natively; zero is one such value.
      M.Args.push_back(ArgAction::Zero);
      ++Missing;
      continue;
    }
    ValueClass C = Call.Params[I], D = Def.Params[I];
    if (C == D) {
      M.Args.push_back(ArgAction::Pass);
      continue;
    }
    if (!isInteger(C) || !isInteger(D))
      return Fail("argument " + Twine(I + 1) + " is " + getClassName(C) +
                  " at the call but " + getClassName(D) +
                  " in the definition (different native register classes)");
    // The callee reads the low 32 bits natively; the upper half of a
    // sign-extended int32 is unspecified natively.
    M.Args.push_back(C == ValueClass::NativeInt ? ArgAction::ConvI4
                                                : ArgAction::ConvI);
    Notes.push_back(("converts argument " + Twine(I + 1) + " from " +
                     getClassName(C) + " to " + getClassName(D))
                        .str());
  }
  if (Missing)
    Notes.push_back("passes zero for " + plural(Missing, "missing argument"));
  if (Call.Params.size() > Def.Params.size()) {
    // x86-64 callees ignore extra arguments.
    M.DroppedArgs = Call.Params.size() - Def.Params.size();
    Notes.push_back("drops " + plural(M.DroppedArgs, "extra argument"));
  }

  ValueClass C = Call.Result, D = Def.Result;
  if (C != D) {
    if (C == ValueClass::Void) {
      M.Result = ResultAction::Pop;
      Notes.push_back("discards the result");
    } else if (D == ValueClass::Void) {
      M.Result = ResultAction::Zero;
      Notes.push_back("returns zero, as the definition returns no value");
    } else if (isInteger(C) && isInteger(D)) {
      M.Result = D == ValueClass::NativeInt ? ResultAction::ConvI4
                                            : ResultAction::ConvI;
      Notes.push_back(("converts the result from " + getClassName(D) + " to " +
                       getClassName(C))
                          .str());
    } else {
      return Fail("the result is " + Twine(getClassName(C)) +
                  " at the call but " + getClassName(D) +
                  " in the definition (different native register classes)");
    }
  }

  if (Notes.empty()) {
    M.Args.clear();
    return M;
  }
  M.Kind = CallMatchKind::Adapter;
  M.Detail = join(Notes, "; ");
  return M;
}

/// Pushes zero of class \p C.
static void emitZero(ILBuilder &B, ValueClass C) {
  switch (C) {
  case ValueClass::Int32:
    B.emitLdcI4(0);
    return;
  case ValueClass::NativeInt:
    // Decision D2: a native int constant is ldc.i4 + conv.i.
    B.emitLdcI4(0);
    B.emit(Opcode::Conv_i);
    return;
  case ValueClass::Float32:
    B.emitR4(0);
    return;
  case ValueClass::Float64:
    B.emitR8(0);
    return;
  case ValueClass::Void:
  case ValueClass::Other:
    break;
  }
  llvm_unreachable("no zero of this value class");
}

void cli::buildAdapter(const CallMatch &M, const MethodSig &CallSig,
                       TokenRef Callee, ILBuilder &B) {
  assert(M.Kind != CallMatchKind::Error && "an error has no adapter");
  bool Direct = M.Kind == CallMatchKind::Direct;
  assert((Direct || M.Args.size() == M.DefParams.size()) &&
         "one action per parameter of the definition");
  assert(CallSig.Params.size() +
                 (Direct ? 0 : count(M.Args, ArgAction::Zero)) ==
             M.DefParams.size() + M.DroppedArgs &&
         "the adapter's signature is the call's");
  (void)CallSig;
  for (unsigned I = 0, E = M.DefParams.size(); I != E; ++I) {
    ArgAction A = Direct ? ArgAction::Pass : M.Args[I];
    if (A == ArgAction::Zero) {
      emitZero(B, M.DefParams[I]);
      continue;
    }
    B.emitLdarg(I);
    if (A == ArgAction::ConvI4)
      B.emit(Opcode::Conv_i4);
    else if (A == ArgAction::ConvI)
      B.emit(Opcode::Conv_i);
  }
  B.emitCall(Opcode::Call, Callee, M.DefParams.size(),
             M.DefResult != ValueClass::Void ? 1 : 0);
  switch (M.Result) {
  case ResultAction::Pass:
    break;
  case ResultAction::ConvI4:
    B.emit(Opcode::Conv_i4);
    break;
  case ResultAction::ConvI:
    B.emit(Opcode::Conv_i);
    break;
  case ResultAction::Pop:
    B.emit(Opcode::Pop);
    break;
  case ResultAction::Zero:
    emitZero(B, M.CallResult);
    break;
  }
  B.emitRet();
}

void cli::printCallShape(raw_ostream &OS, const CallShape &S) {
  printMethodSig(OS, S.Sig, nullptr);
}
