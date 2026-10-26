//===- llvm/CLI/CallAdapter.h - MSIL call-site signature checks -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the call-site signature check of ImplementationPlan.md
// 5.9.8 (Q8): a call that the link binds by name to a definition compares the
// call's IL signature and SIGNATURES descriptor (plan 5.8.8) with the
// definition's. Equal shapes call the definition directly; the differences
// that x86-64 C tolerates natively go through an adapter (with a warning);
// the rest are errors. msil-ld applies it to the call sites of CIL objects,
// and MSILFixFunctionBitcasts to the calls of an LTO module (module flag
// "msil-lto"), so the object and LTO paths agree.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_CALLADAPTER_H
#define LLVM_CLI_CALLADAPTER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/Signature.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
class raw_ostream;

namespace cli {

class ILBuilder;

/// The IL value classes of 5.9.8's table. Every type outside the five (a
/// value type, a typed pointer, a generic instance, int64, the narrow
/// integers, ...) is Other, which binds only by an exact match.
enum class ValueClass : uint8_t {
  Void,
  Int32,
  NativeInt,
  Float32,
  Float64,
  Other
};

/// The class of \p T: void, int32 and unsigned int32, native int and
/// unsigned native int, float32, float64, or Other.
LLVM_ABI ValueClass getValueClass(const TypeSig &T);

/// The shape of one side of a call: its IL signature with the value class of
/// the result and of each parameter, and its SIGNATURES descriptor (5.8.8).
struct CallShape {
  MethodSig Sig;
  ValueClass Result = ValueClass::Void;
  SmallVector<ValueClass, 4> Params;
  /// MSILLink::CallFlags: NoPrototype, VarArg, Sret, NativeIncompatible
  /// (classifyCall ignores NoPrototype and NativeIncompatible).
  uint32_t Flags = 0;
  uint32_t FixedParams = 0;
  uint64_t SretSize = 0;
  uint8_t SretAlignLog2 = 0;
  std::vector<MSILLink::ByvalParam> Byval;

  /// The shape of the signature \p Sig with the descriptor \p Info; a null
  /// \p Info is no entry: prototyped, not variadic, no sret or byval
  /// parameter.
  LLVM_ABI static CallShape get(const MethodSig &Sig,
                                const MSILLink::SignatureInfo *Info);

  /// VARARG in the descriptor, or a vararg calling convention or SENTINEL in
  /// the signature.
  LLVM_ABI bool isVarArg() const;
};

/// What a call needs (5.9.8): the token names the definition (Direct), an
/// adapter in between (Adapter, with a warning), or nothing works (Error).
enum class CallMatchKind : uint8_t { Direct, Adapter, Error };

/// What an adapter does with one parameter of the definition: pass the
/// call's argument, convert it (conv.i4: native int to int32, conv.i: int32
/// to native int, sign-extending), or pass zero for a missing argument.
enum class ArgAction : uint8_t { Pass, ConvI4, ConvI, Zero };

/// What an adapter does with the definition's result: return it, convert
/// it, pop it (the call expects none), or return zero (the definition
/// returns void).
enum class ResultAction : uint8_t { Pass, ConvI4, ConvI, Pop, Zero };

/// The decision of classifyCall.
struct CallMatch {
  CallMatchKind Kind = CallMatchKind::Direct;
  /// Adapter: one action per parameter of the definition.
  SmallVector<ArgAction, 4> Args;
  /// Adapter: the call's extra trailing arguments, which the adapter drops.
  unsigned DroppedArgs = 0;
  ResultAction Result = ResultAction::Pass;
  /// The classes of the definition's parameters and result and of the
  /// call's result: the types of the zeros an adapter passes or returns.
  SmallVector<ValueClass, 4> DefParams;
  ValueClass DefResult = ValueClass::Void;
  ValueClass CallResult = ValueClass::Void;
  /// Adapter: what it does, for '>>> adapter: <Detail>' (for example 'drops
  /// 1 extra argument'). Error: why the call cannot be adapted, for
  /// '>>> <Detail>' (a variadic side gives 'variadic functions are not
  /// supported yet'). Empty for Direct.
  std::string Detail;
};

/// 5.9.8's table, and only it, over the shapes of a call \p Call and of the
/// definition \p Def it binds to (in order of precedence):
/// - VARARG on either side: error;
/// - SRET on one side only, or BYVAL parameters at different positions or
///   with different sizes or alignments: error;
/// - a type of class Other in either signature, or another calling
///   convention: an exact match (signature and descriptor) is Direct, else
///   error;
/// - integer versus floating point, or float32 versus float64, in a
///   parameter or the result: error (different native register classes);
/// - extra trailing call arguments (dropped), missing trailing arguments
///   (zero of each parameter's type), a native int argument for an int32
///   parameter (conv.i4) and the reverse (conv.i), a result the call
///   discards (pop) or that the definition does not return (zero), and a
///   result of int32 versus native int (conv.i4 / conv.i): adapter;
/// - no difference: direct.
/// NO_PROTOTYPE takes no part: it is why a C call may differ at all.
LLVM_ABI CallMatch classifyCall(const CallShape &Call, const CallShape &Def);

/// Emits the body of the adapter that \p M describes (an Adapter or Direct
/// match) into \p B, whose method has the call's signature \p CallSig (B
/// returns a value iff CallSig does): load, convert or zero-fill each of
/// the definition's arguments, call \p Callee, convert, pop or zero the
/// result, ret. ILBuilder computes .maxstack.
LLVM_ABI void buildAdapter(const CallMatch &M, const MethodSig &CallSig,
                           TokenRef Callee, ILBuilder &B);

/// Prints the IL signature of \p S as the 'defined as' and 'called as' lines
/// of 5.9.8's text spell it: canonical listing v1, 'int32(int32, native
/// int)'.
LLVM_ABI void printCallShape(raw_ostream &OS, const CallShape &S);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_CALLADAPTER_H
