//===- llvm/CLI/TrailerDefs.h - Functions the trailer defines ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the registry of the trailer definitions
// (ImplementationPlan.md 5.14.4): the C-named functions of
// llvm/CLI/TrailerDefs.def, such as __msil_compiler_barrier, whose IL the
// trailer builder writes (cli::buildTrailer, llvm/CLI/Trailer.h) when an
// output references the name and nothing else defines it. Compiled code
// calls them as ordinary undefined functions. msil-ld offers them through
// its lazy input '<trailer>', searched after every archive and before the
// native libraries; whole-program text (-S, llc, --lto-emit-asm) defines
// each one that its module references and does not define. Either way the
// trailer's caller declares the method with TrailerSink::declareHelper and
// lists it in TrailerInput::Defs, and buildTrailer calls the row's emitter.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_TRAILERDEFS_H
#define LLVM_CLI_TRAILERDEFS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CLI/Signature.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {
class Triple;

namespace cli {

struct TrailerInput;
class TrailerSink;

/// A handle that the sink defines and later prints or encodes; 0 is none.
using EntityRef = uint32_t;

/// A row of TrailerDefs.def; the order of the rows fixes the values.
enum class TrailerDefId : uint16_t {
#define CLI_TRAILER_DEF(Id, Name, Return, Params, Scope, Emitter) Id,
#include "llvm/CLI/TrailerDefs.def"
};

/// The outputs in which the trailer defines a row's name (plan 5.14.4 T3).
/// Enumerators are only ever appended.
enum class TrailerDefScope : uint8_t {
  /// Every output that references the name.
  Any,
  /// An output with an entry point: an msil-ld link that is not -shared, or
  /// whole-program text whose module defines main or wmain (under
  /// --lto-emit-asm, of a link that is not -shared).
  Executable,
  /// An Executable output of a Windows triple.
  WindowsExecutable
};

/// One row of TrailerDefs.def.
struct TrailerDefInfo {
  TrailerDefId Id;
  /// The C symbol name, which is also the method's name.
  StringLiteral Name;
  /// The return type, and the parameter types joined by ", " ("" for none),
  /// as canonical listing v1 text (plan 5.7.10).
  StringLiteral Return, Params;
  TrailerDefScope Scope;
};

/// What a row's emitter writes the definition from (plan 5.14.4 T8): the
/// trailer's input and sink, the row, and Method, the entity that the sink's
/// declareHelper returned for Info.Name (TrailerDefRequest::Method), which
/// the calls of the name refer to. The emitter hands Sink.emitMethod a
/// TrailerMethod named Info.Name, assembly and static, with
/// getTrailerDefSignature(Info) and the ImplFlags it needs (T4), and may add
/// fields and types through other TrailerSink members (5.14.5 S5).
struct TrailerDefContext {
  const TrailerInput &In;
  TrailerSink &Sink;
  const TrailerDefInfo &Info;
  EntityRef Method;
};

/// Every row of TrailerDefs.def, indexed by TrailerDefId.
LLVM_ABI ArrayRef<TrailerDefInfo> getTrailerDefs();

/// The row whose Name is exactly \p Name; null if there is none.
LLVM_ABI const TrailerDefInfo *findTrailerDef(StringRef Name);

/// Whether the trailer defines \p D in an output for \p TT: always for
/// TrailerDefScope::Any, if \p IsExecutable for Executable, and if
/// \p IsExecutable and \p TT is a Windows triple for WindowsExecutable.
LLVM_ABI bool isOfferedIn(const TrailerDefInfo &D, const Triple &TT,
                          bool IsExecutable);

/// Whether \p Name is a toolchain name, '__msil_*' (plan 5.14.2 N3): objects,
/// the helper archive and the trailer define such names, never a native
/// library or another assembly.
inline bool isToolchainName(StringRef Name) {
  return Name.starts_with("__msil_");
}

/// The IL signature of \p D: a static method with the default calling
/// convention that returns D.Return and takes D.Params. Their text names
/// primitive types as canonical listing v1 spells them: void (only as the
/// return type), bool, int8, uint8, int16, uint16, int32, uint32, int64,
/// uint64, float32, float64, native int and native uint (also spelled as ILAsm
/// does, native unsigned int); any other text is a fatal internal error.
LLVM_ABI MethodSig getTrailerDefSignature(const TrailerDefInfo &D);

/// The emitters of the rows (the Emitter column of TrailerDefs.def), each
/// defined in its feature's file in llvm/lib/CLI: it writes the method of
/// Ctx.Info through Ctx.Sink.
#define CLI_TRAILER_DEF(Id, Name, Return, Params, Scope, Emitter)              \
  LLVM_ABI void Emitter(TrailerDefContext &Ctx);
#include "llvm/CLI/TrailerDefs.def"

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_TRAILERDEFS_H
