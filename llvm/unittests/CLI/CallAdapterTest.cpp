//===- CallAdapterTest.cpp - call-site signature checks and adapters ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/CallAdapter.h (ImplementationPlan.md 5.9.8, Q8, M20): every
// row of the adapter table, in both directions (call versus definition):
// - no difference: direct;
// - extra trailing arguments are dropped, missing ones are zero of each
//   parameter's type;
// - a native int argument for an int32 parameter gets conv.i4, an int32
//   argument for a native int parameter conv.i;
// - a result the definition does not return is zero, a result the call
//   discards is popped, int32 versus native int results are converted;
// - integer versus floating point and float32 versus float64, in a
//   parameter or the result, are errors; so are SRET on one side only, BYVAL
//   at different positions or with another size or alignment, VARARG on
//   either side ('variadic functions are not supported yet') and any
//   difference in a signature with a value type or a typed pointer.
// Each adapter that buildAdapter emits is evaluated symbolically: what the
// callee receives, what the adapter returns, that the stack is empty at
// 'ret', and its .maxstack (computed by ILBuilder). printCallShape gives the
// 'int32(int32, native int)' text of 5.9.8's messages.
// Expected values come from the plan's table, never from the implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/CallAdapter.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/CLI/Signature.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

using Strings = std::vector<std::string>;

TypeSig tVoid() { return TypeSig(); }
TypeSig tI4() { return TypeSig::get(ELEMENT_TYPE_I4); }
TypeSig tNI() { return TypeSig::get(ELEMENT_TYPE_I); }
TypeSig tR4() { return TypeSig::get(ELEMENT_TYPE_R4); }
TypeSig tR8() { return TypeSig::get(ELEMENT_TYPE_R8); }
// A value type and a typed pointer (P3 and later; exact match or error).
TypeSig tVT(uint32_t Row) {
  return TypeSig::getValueType(makeToken(TableId::TypeRef, Row));
}
TypeSig tI4Ptr() { return TypeSig::getPointer(tI4()); }

MethodSig sig(TypeSig Ret, std::vector<TypeSig> Params) {
  MethodSig S;
  S.Return = Ret;
  S.Params = std::move(Params);
  return S;
}

bool isVoid(const TypeSig &T) {
  return T.getElementType() == ELEMENT_TYPE_VOID;
}

MSILLink::SignatureInfo sretInfo(uint64_t Size, uint8_t AlignLog2) {
  MSILLink::SignatureInfo I;
  I.Token = makeToken(TableId::MemberRef, 1);
  I.Flags = MSILLink::Sret;
  I.SretSize = Size;
  I.SretAlignLog2 = AlignLog2;
  return I;
}

MSILLink::SignatureInfo byvalInfo(uint32_t Param, uint64_t Size,
                                  uint8_t AlignLog2) {
  MSILLink::SignatureInfo I;
  I.Token = makeToken(TableId::MemberRef, 1);
  I.Byval.push_back({Param, Size, AlignLog2});
  return I;
}

MSILLink::SignatureInfo flagsInfo(uint32_t Flags, uint32_t FixedParams = 0) {
  MSILLink::SignatureInfo I;
  I.Token = makeToken(TableId::MemberRef, 1);
  I.Flags = Flags;
  I.FixedParams = FixedParams;
  return I;
}

CallMatch match(const MethodSig &Call, const MethodSig &Def,
                const MSILLink::SignatureInfo *CallInfo = nullptr,
                const MSILLink::SignatureInfo *DefInfo = nullptr) {
  return classifyCall(CallShape::get(Call, CallInfo),
                      CallShape::get(Def, DefInfo));
}

CallMatchKind kind(const MethodSig &Call, const MethodSig &Def,
                   const MSILLink::SignatureInfo *CallInfo = nullptr,
                   const MSILLink::SignatureInfo *DefInfo = nullptr) {
  return match(Call, Def, CallInfo, DefInfo).Kind;
}

// buildAdapter's result, whatever its type, is checked when it is an Error.
template <typename Fn> void callChecked(Fn &&F) {
  using R = decltype(F());
  if constexpr (std::is_void_v<R>)
    F();
  else if constexpr (std::is_same_v<R, Error>)
    EXPECT_THAT_ERROR(F(), Succeeded());
  else
    (void)F();
}

// A value on the evaluation stack of an adapter: its IL class and either a
// constant or an expression over the adapter's arguments ("a0") and the
// callee's result ("r").
struct Sym {
  ValueClass C = ValueClass::Other;
  bool Const = false;
  double V = 0;
  std::string Expr;
};

const char *className(ValueClass C) {
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
  default:
    return "other";
  }
}

bool isFloat(ValueClass C) {
  return C == ValueClass::Float32 || C == ValueClass::Float64;
}

// "zero" for a constant zero of class Want (int32 0 also stands for a
// native int zero, and the CIL stack type F holds both float classes), the
// expression when the class is Want, else a mismatch.
std::string describe(const Sym &S, ValueClass Want) {
  if (S.Const && S.V == 0 &&
      (S.C == Want ||
       (Want == ValueClass::NativeInt && S.C == ValueClass::Int32) ||
       (isFloat(Want) && isFloat(S.C))))
    return "zero";
  if (S.C != Want)
    return std::string("<") + className(S.C) + " for " + className(Want) +
           ": " + (S.Const ? std::to_string(S.V) : S.Expr) + ">";
  return S.Const ? "const " + std::to_string(S.V) : S.Expr;
}

struct AdapterRun {
  CallMatch Match;
  Strings Args;    // what the callee received, per definition parameter
  std::string Ret; // what the adapter returns; "" for void
  unsigned MaxStack = 0;
  unsigned Calls = 0;
  std::string Failure;
};

const TokenRef Callee = TokenRef::getToken(0x06000042);

AdapterRun runAdapter(const MethodSig &Call, const MethodSig &Def,
                      const MSILLink::SignatureInfo *CallInfo = nullptr,
                      const MSILLink::SignatureInfo *DefInfo = nullptr) {
  AdapterRun R;
  R.Match = match(Call, Def, CallInfo, DefInfo);
  EXPECT_EQ(R.Match.Kind, CallMatchKind::Adapter);
  EXPECT_FALSE(R.Match.Detail.empty()) << "an adapter without a description";
  ILBuilder B(!isVoid(Call.Return));
  callChecked([&] { return buildAdapter(R.Match, Call, Callee, B); });
  if (Error E = B.layout()) {
    R.Failure = "layout: " + toString(std::move(E));
    return R;
  }
  R.MaxStack = B.getMaxStack();
  std::vector<Sym> Stack;
  auto Fail = [&](const std::string &Msg) {
    if (R.Failure.empty())
      R.Failure = Msg;
  };
  auto Pop = [&](Sym &S) {
    if (Stack.empty()) {
      Fail("stack underflow");
      return false;
    }
    S = Stack.back();
    Stack.pop_back();
    return true;
  };
  bool Returned = false;
  for (const ILInstr &I : B.instructions()) {
    if (Returned) {
      Fail("code after ret");
      break;
    }
    const OpcodeInfo &Info = getOpcodeInfo(I.Op);
    StringRef Name = Info.Name;
    Sym S;
    if (Name.starts_with("ldarg") && !Name.starts_with("ldarga")) {
      int64_t N =
          Info.Operand == OperandKind::InlineNone ? Name.back() - '0' : I.Imm;
      if (N < 0 || size_t(N) >= Call.Params.size()) {
        Fail("ldarg " + std::to_string(N) + " out of range");
        break;
      }
      S.C = getValueClass(Call.Params[N]);
      S.Expr = "a" + std::to_string(N);
      Stack.push_back(S);
    } else if (Name.starts_with("ldc.i4")) {
      S.C = ValueClass::Int32;
      S.Const = true;
      if (Info.Operand != OperandKind::InlineNone)
        S.V = double(int32_t(I.Imm));
      else
        S.V = Name.ends_with("m1") ? -1 : Name.back() - '0';
      Stack.push_back(S);
    } else if (I.Op == Opcode::Ldc_i8) {
      S.C = ValueClass::Other; // int64: only valid when converted
      S.Const = true;
      S.V = double(I.Imm);
      Stack.push_back(S);
    } else if (I.Op == Opcode::Ldc_r4 || I.Op == Opcode::Ldc_r8) {
      S.C = I.Op == Opcode::Ldc_r4 ? ValueClass::Float32 : ValueClass::Float64;
      S.Const = true;
      if (I.Op == Opcode::Ldc_r4) {
        uint32_t Bits = uint32_t(I.FPBits);
        float F;
        std::memcpy(&F, &Bits, 4);
        S.V = F;
      } else {
        double D;
        uint64_t Bits = I.FPBits;
        std::memcpy(&D, &Bits, 8);
        S.V = D;
      }
      Stack.push_back(S);
    } else if (I.Op == Opcode::Conv_i4 || I.Op == Opcode::Conv_i ||
               I.Op == Opcode::Conv_u || I.Op == Opcode::Conv_r4 ||
               I.Op == Opcode::Conv_r8) {
      if (!Pop(S))
        break;
      Sym T = S;
      T.C = I.Op == Opcode::Conv_i4   ? ValueClass::Int32
            : I.Op == Opcode::Conv_r4 ? ValueClass::Float32
            : I.Op == Opcode::Conv_r8 ? ValueClass::Float64
                                      : ValueClass::NativeInt;
      if (!S.Const)
        T.Expr = Name.str() + "(" + S.Expr + ")";
      Stack.push_back(T);
    } else if (I.Op == Opcode::Call) {
      ++R.Calls;
      if (I.Token != Callee) {
        Fail("a call of another token");
        break;
      }
      std::vector<Sym> Args(Def.Params.size());
      for (size_t K = Args.size(); K-- > 0;)
        if (!Pop(Args[K]))
          break;
      for (size_t K = 0; K != Args.size(); ++K)
        R.Args.push_back(describe(Args[K], getValueClass(Def.Params[K])));
      if (!isVoid(Def.Return)) {
        S.C = getValueClass(Def.Return);
        S.Expr = "r";
        Stack.push_back(S);
      }
    } else if (I.Op == Opcode::Pop) {
      Pop(S);
    } else if (I.Op == Opcode::Nop) {
    } else if (I.Op == Opcode::Ret) {
      Returned = true;
      if (!isVoid(Call.Return)) {
        if (!Pop(S))
          break;
        R.Ret = describe(S, getValueClass(Call.Return));
      }
      if (!Stack.empty())
        Fail("values left on the stack at ret");
    } else {
      Fail("unexpected instruction " + Name.str());
      break;
    }
  }
  if (!Returned)
    Fail("no ret");
  if (R.Calls != 1)
    Fail("the adapter calls the definition " + std::to_string(R.Calls) +
         " times");
  return R;
}

void expectError(const MethodSig &Call, const MethodSig &Def,
                 const MSILLink::SignatureInfo *CallInfo = nullptr,
                 const MSILLink::SignatureInfo *DefInfo = nullptr) {
  CallMatch M = match(Call, Def, CallInfo, DefInfo);
  EXPECT_EQ(M.Kind, CallMatchKind::Error);
  EXPECT_FALSE(M.Detail.empty()) << "an error without a reason";
}

std::string printShape(const MethodSig &S,
                       const MSILLink::SignatureInfo *Info = nullptr) {
  std::string Out;
  raw_string_ostream OS(Out);
  printCallShape(OS, CallShape::get(S, Info));
  return Out;
}

//===----------------------------------------------------------------------===//
// Direct calls
//===----------------------------------------------------------------------===//

TEST(CLICallAdapterTest, IdenticalSignaturesAreDirect) {
  for (const MethodSig &S :
       {sig(tI4(), {tI4(), tNI()}), sig(tVoid(), {}), sig(tR8(), {tR4()}),
        sig(tNI(), {tNI(), tNI(), tI4()}), sig(tR4(), {tR8(), tI4()})})
    EXPECT_EQ(kind(S, S), CallMatchKind::Direct) << printShape(S);
}

TEST(CLICallAdapterTest, NoPrototypeFlagAloneIsDirect) {
  // A K&R call site whose IL signature equals the definition's.
  MethodSig S = sig(tI4(), {tI4(), tI4()});
  MSILLink::SignatureInfo NoProto = flagsInfo(MSILLink::NoPrototype);
  EXPECT_EQ(kind(S, S, &NoProto, nullptr), CallMatchKind::Direct);
}

//===----------------------------------------------------------------------===//
// Adapters
//===----------------------------------------------------------------------===//

TEST(CLICallAdapterTest, ExtraTrailingArgumentsAreDropped) {
  {
    // sigmismatch: int f(int, int); called, int f(int a) defined.
    AdapterRun R = runAdapter(sig(tI4(), {tI4(), tI4()}), sig(tI4(), {tI4()}));
    EXPECT_EQ(R.Failure, "");
    EXPECT_EQ(R.Args, (Strings{"a0"}));
    EXPECT_EQ(R.Ret, "r");
    EXPECT_EQ(R.MaxStack, 1u);
  }
  {
    // Dropped arguments of any class.
    AdapterRun R = runAdapter(sig(tVoid(), {tI4(), tR8(), tNI(), tR4()}),
                              sig(tVoid(), {tI4()}));
    EXPECT_EQ(R.Failure, "");
    EXPECT_EQ(R.Args, (Strings{"a0"}));
    EXPECT_EQ(R.Ret, "");
    EXPECT_EQ(R.MaxStack, 1u);
  }
  {
    AdapterRun R = runAdapter(sig(tNI(), {tNI()}), sig(tNI(), {}));
    EXPECT_EQ(R.Failure, "");
    EXPECT_TRUE(R.Args.empty());
    EXPECT_EQ(R.Ret, "r");
    EXPECT_EQ(R.MaxStack, 1u);
  }
}

TEST(CLICallAdapterTest, MissingTrailingArgumentsAreZero) {
  {
    AdapterRun R = runAdapter(sig(tI4(), {tI4()}),
                              sig(tI4(), {tI4(), tI4(), tNI(), tR4(), tR8()}));
    EXPECT_EQ(R.Failure, "");
    EXPECT_EQ(R.Args, (Strings{"a0", "zero", "zero", "zero", "zero"}));
    EXPECT_EQ(R.Ret, "r");
    EXPECT_EQ(R.MaxStack, 5u);
  }
  {
    AdapterRun R = runAdapter(sig(tVoid(), {}), sig(tVoid(), {tNI()}));
    EXPECT_EQ(R.Failure, "");
    EXPECT_EQ(R.Args, (Strings{"zero"}));
    EXPECT_EQ(R.MaxStack, 1u);
  }
  {
    // knr-two-arities: f(1) of int f(int a, int b).
    AdapterRun R = runAdapter(sig(tI4(), {tI4()}), sig(tI4(), {tI4(), tI4()}));
    EXPECT_EQ(R.Failure, "");
    EXPECT_EQ(R.Args, (Strings{"a0", "zero"}));
    EXPECT_EQ(R.MaxStack, 2u);
  }
}

TEST(CLICallAdapterTest, NativeIntArgumentForInt32Parameter) {
  AdapterRun R = runAdapter(sig(tI4(), {tNI()}), sig(tI4(), {tI4()}));
  EXPECT_EQ(R.Failure, "");
  EXPECT_EQ(R.Args, (Strings{"conv.i4(a0)"}));
  EXPECT_EQ(R.Ret, "r");
  EXPECT_EQ(R.MaxStack, 1u);
}

TEST(CLICallAdapterTest, Int32ArgumentForNativeIntParameter) {
  AdapterRun R =
      runAdapter(sig(tVoid(), {tI4(), tI4()}), sig(tVoid(), {tNI(), tI4()}));
  EXPECT_EQ(R.Failure, "");
  EXPECT_EQ(R.Args, (Strings{"conv.i(a0)", "a1"}));
  EXPECT_EQ(R.Ret, "");
  EXPECT_EQ(R.MaxStack, 2u);
}

TEST(CLICallAdapterTest, ResultOfVoidDefinitionIsZero) {
  {
    AdapterRun R = runAdapter(sig(tI4(), {tI4()}), sig(tVoid(), {tI4()}));
    EXPECT_EQ(R.Failure, "");
    EXPECT_EQ(R.Args, (Strings{"a0"}));
    EXPECT_EQ(R.Ret, "zero");
    EXPECT_EQ(R.MaxStack, 1u);
  }
  for (TypeSig T : {tNI(), tR4(), tR8()}) {
    AdapterRun R = runAdapter(sig(T, {}), sig(tVoid(), {}));
    EXPECT_EQ(R.Failure, "");
    EXPECT_EQ(R.Ret, "zero");
    EXPECT_EQ(R.MaxStack, 1u);
  }
}

TEST(CLICallAdapterTest, DiscardedResultIsPopped) {
  {
    AdapterRun R = runAdapter(sig(tVoid(), {tI4()}), sig(tI4(), {tI4()}));
    EXPECT_EQ(R.Failure, "");
    EXPECT_EQ(R.Args, (Strings{"a0"}));
    EXPECT_EQ(R.Ret, "");
    EXPECT_EQ(R.MaxStack, 1u);
  }
  for (TypeSig T : {tNI(), tR4(), tR8()}) {
    AdapterRun R =
        runAdapter(sig(tVoid(), {tI4(), tI4()}), sig(T, {tI4(), tI4()}));
    EXPECT_EQ(R.Failure, "");
    EXPECT_EQ(R.Args, (Strings{"a0", "a1"}));
    EXPECT_EQ(R.Ret, "");
    EXPECT_EQ(R.MaxStack, 2u);
  }
}

TEST(CLICallAdapterTest, Int32VersusNativeIntResult) {
  {
    AdapterRun R = runAdapter(sig(tI4(), {}), sig(tNI(), {}));
    EXPECT_EQ(R.Failure, "");
    EXPECT_EQ(R.Ret, "conv.i4(r)");
    EXPECT_EQ(R.MaxStack, 1u);
  }
  {
    AdapterRun R = runAdapter(sig(tNI(), {tNI()}), sig(tI4(), {tNI()}));
    EXPECT_EQ(R.Failure, "");
    EXPECT_EQ(R.Args, (Strings{"a0"}));
    EXPECT_EQ(R.Ret, "conv.i(r)");
    EXPECT_EQ(R.MaxStack, 1u);
  }
}

TEST(CLICallAdapterTest, AdaptationsCombine) {
  // conv.i4 on the first argument, the third dropped, the result converted.
  AdapterRun R =
      runAdapter(sig(tI4(), {tNI(), tI4(), tI4()}), sig(tNI(), {tI4(), tI4()}));
  EXPECT_EQ(R.Failure, "");
  EXPECT_EQ(R.Args, (Strings{"conv.i4(a0)", "a1"}));
  EXPECT_EQ(R.Ret, "conv.i4(r)");
  EXPECT_EQ(R.MaxStack, 2u);
  // A missing argument, conv.i, and a popped result.
  AdapterRun S =
      runAdapter(sig(tVoid(), {tI4()}), sig(tI4(), {tNI(), tR8(), tI4()}));
  EXPECT_EQ(S.Failure, "");
  EXPECT_EQ(S.Args, (Strings{"conv.i(a0)", "zero", "zero"}));
  EXPECT_EQ(S.Ret, "");
  EXPECT_EQ(S.MaxStack, 3u);
}

TEST(CLICallAdapterTest, AdapterIsOneCallOfTheDefinition) {
  MethodSig Call = sig(tI4(), {tNI(), tI4()});
  MethodSig Def = sig(tI4(), {tI4()});
  CallMatch M = match(Call, Def);
  ASSERT_EQ(M.Kind, CallMatchKind::Adapter);
  ILBuilder B(true);
  callChecked([&] { return buildAdapter(M, Call, Callee, B); });
  ASSERT_THAT_ERROR(B.layout(), Succeeded());
  ArrayRef<ILInstr> Code = B.instructions();
  ASSERT_FALSE(Code.empty());
  EXPECT_EQ(Code.back().Op, Opcode::Ret);
  unsigned Calls = 0;
  for (const ILInstr &I : Code)
    if (I.Op == Opcode::Call || I.Op == Opcode::Callvirt ||
        I.Op == Opcode::Calli || I.Op == Opcode::Jmp) {
      ++Calls;
      EXPECT_EQ(I.Op, Opcode::Call);
      EXPECT_EQ(I.Token, Callee);
    }
  EXPECT_EQ(Calls, 1u);
}

//===----------------------------------------------------------------------===//
// Errors
//===----------------------------------------------------------------------===//

TEST(CLICallAdapterTest, IntegerVersusFloatingPointIsAnError) {
  // Parameters, both directions and both integer classes.
  for (TypeSig I : {tI4(), tNI()})
    for (TypeSig F : {tR4(), tR8()}) {
      expectError(sig(tVoid(), {I}), sig(tVoid(), {F}));
      expectError(sig(tVoid(), {F}), sig(tVoid(), {I}));
      // The result.
      expectError(sig(I, {}), sig(F, {}));
      expectError(sig(F, {}), sig(I, {}));
    }
  // float-int-mismatch: int f(int) called, double f(double) defined.
  expectError(sig(tI4(), {tI4()}), sig(tR8(), {tR8()}));
}

TEST(CLICallAdapterTest, Float32VersusFloat64IsAnError) {
  expectError(sig(tVoid(), {tR4()}), sig(tVoid(), {tR8()}));
  expectError(sig(tVoid(), {tR8()}), sig(tVoid(), {tR4()}));
  expectError(sig(tR4(), {}), sig(tR8(), {}));
  expectError(sig(tR8(), {}), sig(tR4(), {}));
}

TEST(CLICallAdapterTest, AdaptableAndErrorIsAnError) {
  // An extra argument (adaptable) with a float/int result (not).
  expectError(sig(tI4(), {tI4(), tI4()}), sig(tR8(), {tI4()}));
  // conv.i4 (adaptable) with a float32/float64 parameter.
  expectError(sig(tVoid(), {tNI(), tR4()}), sig(tVoid(), {tI4(), tR8()}));
}

TEST(CLICallAdapterTest, SretOnOneSideIsAnError) {
  MethodSig S = sig(tVoid(), {tNI(), tI4()});
  MSILLink::SignatureInfo Sret = sretInfo(16, 3);
  expectError(S, S, &Sret, nullptr);
  expectError(S, S, nullptr, &Sret);
  EXPECT_EQ(kind(S, S, &Sret, &Sret), CallMatchKind::Direct);
}

TEST(CLICallAdapterTest, ByvalDifferencesAreErrors) {
  MethodSig S = sig(tVoid(), {tNI(), tNI()});
  MSILLink::SignatureInfo At1 = byvalInfo(1, 16, 3);
  MSILLink::SignatureInfo At0 = byvalInfo(0, 16, 3);
  MSILLink::SignatureInfo Size = byvalInfo(1, 24, 3);
  MSILLink::SignatureInfo Align = byvalInfo(1, 16, 4);
  expectError(S, S, &At1, &At0);
  expectError(S, S, &At0, &At1);
  expectError(S, S, &At1, &Size);
  expectError(S, S, &Size, &At1);
  expectError(S, S, &At1, &Align);
  expectError(S, S, &Align, &At1);
  expectError(S, S, &At1, nullptr);
  expectError(S, S, nullptr, &At1);
  EXPECT_EQ(kind(S, S, &At1, &At1), CallMatchKind::Direct);
}

TEST(CLICallAdapterTest, VarargIsNotSupportedYet) {
  MethodSig S = sig(tI4(), {tI4(), tI4()});
  MSILLink::SignatureInfo VarArg = flagsInfo(MSILLink::VarArg, 1);
  EXPECT_TRUE(CallShape::get(S, &VarArg).isVarArg());
  EXPECT_FALSE(CallShape::get(S, nullptr).isVarArg());
  for (auto [C, D] : {std::pair{&VarArg, (MSILLink::SignatureInfo *)nullptr},
                      std::pair{(MSILLink::SignatureInfo *)nullptr, &VarArg},
                      std::pair{&VarArg, &VarArg}}) {
    CallMatch M = match(S, S, C, D);
    EXPECT_EQ(M.Kind, CallMatchKind::Error);
    EXPECT_EQ(M.Detail, "variadic functions are not supported yet");
  }
}

TEST(CLICallAdapterTest, ValueTypesAndTypedPointersMatchExactly) {
  EXPECT_EQ(kind(sig(tVoid(), {tVT(1)}), sig(tVoid(), {tVT(1)})),
            CallMatchKind::Direct);
  EXPECT_EQ(kind(sig(tI4Ptr(), {tI4Ptr()}), sig(tI4Ptr(), {tI4Ptr()})),
            CallMatchKind::Direct);
  expectError(sig(tVoid(), {tVT(1)}), sig(tVoid(), {tVT(2)}));
  expectError(sig(tVoid(), {tI4Ptr()}), sig(tVoid(), {tNI()}));
  expectError(sig(tVoid(), {tNI()}), sig(tVoid(), {tI4Ptr()}));
  expectError(sig(tI4Ptr(), {}), sig(tNI(), {}));
  // Otherwise adaptable differences are errors next to such a type.
  expectError(sig(tVoid(), {tVT(1), tI4()}), sig(tVoid(), {tVT(1)}));
  expectError(sig(tI4(), {tI4Ptr()}), sig(tVoid(), {tI4Ptr()}));
}

//===----------------------------------------------------------------------===//
// Shapes
//===----------------------------------------------------------------------===//

TEST(CLICallAdapterTest, ValueClasses) {
  EXPECT_EQ(getValueClass(tVoid()), ValueClass::Void);
  EXPECT_EQ(getValueClass(tI4()), ValueClass::Int32);
  EXPECT_EQ(getValueClass(tNI()), ValueClass::NativeInt);
  EXPECT_EQ(getValueClass(tR4()), ValueClass::Float32);
  EXPECT_EQ(getValueClass(tR8()), ValueClass::Float64);
  EXPECT_EQ(getValueClass(tVT(1)), ValueClass::Other);
  EXPECT_EQ(getValueClass(tI4Ptr()), ValueClass::Other);
}

TEST(CLICallAdapterTest, PrintCallShape) {
  EXPECT_EQ(printShape(sig(tI4(), {tI4(), tNI()})), "int32(int32, native int)");
  EXPECT_EQ(printShape(sig(tI4(), {tI4()})), "int32(int32)");
  EXPECT_EQ(printShape(sig(tVoid(), {})), "void()");
  EXPECT_EQ(printShape(sig(tR8(), {tR4(), tR8()})),
            "float64(float32, float64)");
  EXPECT_EQ(printShape(sig(tNI(), {tNI()})), "native int(native int)");
}

} // namespace
