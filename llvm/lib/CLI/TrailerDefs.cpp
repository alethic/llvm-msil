//===- TrailerDefs.cpp - Functions the MSIL trailer defines ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the registry of the trailer definitions
// (ImplementationPlan.md 5.14.4, see llvm/CLI/TrailerDefs.h): the rows of
// llvm/CLI/TrailerDefs.def, their signatures and the outputs that offer
// them, and the emitter of the compiler barrier, __msil_compiler_barrier.
// cli::buildTrailer (Trailer.cpp) calls the emitters of the definitions that
// an output uses.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/TrailerDefs.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/Trailer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <utility>

using namespace llvm;
using namespace llvm::cli;

#define DEBUG_TYPE "cli-trailerdefs"

ArrayRef<TrailerDefInfo> cli::getTrailerDefs() {
  // In row order, which is TrailerDefId order.
  static constexpr TrailerDefInfo Defs[] = {
#define CLI_TRAILER_DEF(Id, Name, Return, Params, Scope, Emitter)              \
  {TrailerDefId::Id, Name, Return, Params, TrailerDefScope::Scope},
#include "llvm/CLI/TrailerDefs.def"
  };
  return Defs;
}

const TrailerDefInfo *cli::findTrailerDef(StringRef Name) {
  for (const TrailerDefInfo &D : getTrailerDefs())
    if (D.Name == Name)
      return &D;
  return nullptr;
}

bool cli::isOfferedIn(const TrailerDefInfo &D, const Triple &TT,
                      bool IsExecutable) {
  switch (D.Scope) {
  case TrailerDefScope::Any:
    return true;
  case TrailerDefScope::Executable:
    return IsExecutable;
  case TrailerDefScope::WindowsExecutable:
    return IsExecutable && TT.isOSWindows();
  }
  llvm_unreachable("unknown trailer definition scope");
}

/// The type that \p Text names in the signature of \p D: a primitive type as
/// canonical listing v1 spells it (plan 5.7.10), or native uint as ILAsm
/// spells it, native unsigned int; void only as the return type. Any other
/// text is a malformed row of TrailerDefs.def.
static TypeSig parseSignatureType(const TrailerDefInfo &D, StringRef Text,
                                  bool IsReturn) {
  static constexpr std::pair<StringLiteral, ElementType> Primitives[] = {
      {"void", ELEMENT_TYPE_VOID},
      {"bool", ELEMENT_TYPE_BOOLEAN},
      {"int8", ELEMENT_TYPE_I1},
      {"uint8", ELEMENT_TYPE_U1},
      {"int16", ELEMENT_TYPE_I2},
      {"uint16", ELEMENT_TYPE_U2},
      {"int32", ELEMENT_TYPE_I4},
      {"uint32", ELEMENT_TYPE_U4},
      {"int64", ELEMENT_TYPE_I8},
      {"uint64", ELEMENT_TYPE_U8},
      {"float32", ELEMENT_TYPE_R4},
      {"float64", ELEMENT_TYPE_R8},
      {"native int", ELEMENT_TYPE_I},
      {"native uint", ELEMENT_TYPE_U},
      {"native unsigned int", ELEMENT_TYPE_U},
  };
  Text = Text.trim();
  for (const auto &[Name, Type] : Primitives)
    if (Text == Name && (IsReturn || Type != ELEMENT_TYPE_VOID))
      return TypeSig::get(Type);
  reportFatalInternalError(Twine("MSIL trailer definition '") + D.Name +
                           "': unsupported " +
                           (IsReturn ? "return" : "parameter") + " type '" +
                           Text + "' in TrailerDefs.def");
}

MethodSig cli::getTrailerDefSignature(const TrailerDefInfo &D) {
  // A static method (no HASTHIS) with the default calling convention.
  MethodSig Sig;
  Sig.Return = parseSignatureType(D, D.Return, /*IsReturn=*/true);
  StringRef Params = D.Params;
  if (!Params.trim().empty()) {
    SmallVector<StringRef, 4> Parts;
    Params.split(Parts, ',');
    for (StringRef Part : Parts)
      Sig.Params.push_back(parseSignatureType(D, Part, /*IsReturn=*/false));
  }
  return Sig;
}

/// The method of the row of \p Ctx as plan 5.14.4 T4 shapes every trailer
/// definition: a static method of <Module> named after the C symbol, with
/// assembly access (private to the output) and the row's signature. Its
/// emitter adds the body and the implementation flags.
static TrailerMethod makeDefinition(const TrailerDefContext &Ctx) {
  TrailerMethod M;
  M.Name = Ctx.Info.Name.str();
  M.Flags = MethodAttributes::Assembly | MethodAttributes::Static;
  M.Sig = getTrailerDefSignature(Ctx.Info);
  M.Body = ILBuilder(
      /*ReturnsValue=*/M.Sig.Return.getElementType() != ELEMENT_TYPE_VOID);
  M.Owner = TrailerOwner::Module;
  return M;
}

/// Lays \p M out and hands it to the sink of \p Ctx. A definition's body is
/// a fixed shape, so a layout error is a bug of its emitter.
static void finishDefinition(TrailerMethod &M, TrailerDefContext &Ctx) {
  if (Error E = M.Body.layout())
    reportFatalInternalError(Twine("MSIL trailer definition '") + M.Name +
                             "': " + toString(std::move(E)));
  Ctx.Sink.emitMethod(M);
}

void cli::emitCompilerBarrier(TrailerDefContext &Ctx) {
  // .method assembly static void '__msil_compiler_barrier'() cil managed
  // noinlining { ret }, which every single-thread fence,
  // atomic_signal_fence and empty asm barrier calls (decision D36 as amended
  // in Phase 3). The JIT does not inline the call and must assume that it
  // reads and writes memory, so it keeps the loads and stores on their side
  // of it, which the IL comment that D36 printed before did not make it do:
  // a load hoisted out of a spin loop hung on .NET Framework, .NET 8 without
  // tiered compilation and .NET 10 (recon/p3/atomics-threads.md 5.4).
  assert(Ctx.Info.Id == TrailerDefId::CompilerBarrier &&
         "the emitter of another row");
  TrailerMethod M = makeDefinition(Ctx);
  M.ImplFlags = MethodImplAttributes::NoInlining;
  M.Body.emitRet();
  finishDefinition(M, Ctx);
}
