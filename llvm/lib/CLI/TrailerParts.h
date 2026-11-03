//===- TrailerParts.h - Parts of the MSIL module trailer --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the parts of the trailer that cli::buildTrailer
// (Trailer.cpp, ImplementationPlan.md 5.9.10) calls, one source file per
// concern (5.14.5 S1, 5.13.3): the UCRT stdio options (TrailerStdio.cpp) and
// the portable width and byte-order guard (TrailerGuard.cpp). It is private
// to lib/CLI: the producers fill cli::TrailerInput, and only the builder
// calls the parts.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CLI_TRAILERPARTS_H
#define LLVM_LIB_CLI_TRAILERPARTS_H

#include "llvm/CLI/ILBuilder.h"

namespace llvm {
namespace cli {

class TrailerSink;
struct PortableGuard;
struct TrailerInput;
struct TrailerMethod;

/// The two branch targets of the portable guard in <Module>::.cctor, which
/// emitPortableGuard creates and emitPortableGuardFailures places.
struct PortableGuardLabels {
  ILLabel Width; ///< guard_width: the process is not 64-bit.
  ILLabel Order; ///< guard_order: the process is not little-endian.
};

/// Emits the width and byte-order guard of a portable output (plan 5.18.7
/// PE5-PE7) as the first code of \p Cctor, <Module>::.cctor, which must still
/// be empty:
///
///   ldc.i4.m1; conv.i; conv.u8; ldc.i8 -1; bne.un guard_width
///   ldc.i4.1; call uint8[] BitConverter::GetBytes(int32); ldc.i4.0
///   ldelem.u1; brfalse guard_order
///
/// Returns the two labels, the first of \p Cctor (ids 0 and 1), which the
/// caller passes to emitPortableGuardFailures to place after the
/// initializer's last 'ret'.
PortableGuardLabels emitPortableGuard(TrailerSink &Sink, const PortableGuard &G,
                                      TrailerMethod &Cctor);

/// Appends the guard's two failure blocks to \p Cctor after its last 'ret':
/// at each of \p Labels 'ldstr <T-P10 message>; newobj
/// PlatformNotSupportedException::.ctor(string); throw', the width message
/// first. \p Labels are what emitPortableGuard returned for \p Cctor.
void emitPortableGuardFailures(TrailerSink &Sink, const PortableGuard &G,
                               TrailerMethod &Cctor,
                               const PortableGuardLabels &Labels);

/// True when In.UCRTStdio names a function whose option word the module
/// initializer sets (TrailerStdio.cpp; plan 5.15.2, hosted-libc H3), so that
/// it alone makes <Module>::.cctor exist.
bool needsUCRTStdioOptions(const TrailerInput &In);

/// Appends to \p M, the initializer that runs phase 2 (5.9.10: after the
/// relocations against library exports, before the constructors), the OR of
/// In.UCRTStdio's bits into the option word of each function it names:
/// 'call native int <f>(); dup; ldind.i8; ldc.i8 <bits>; or; stind.i8',
/// printf's first. Nothing when needsUCRTStdioOptions(In) is false.
void emitUCRTStdioOptions(const TrailerInput &In, TrailerMethod &M);

} // namespace cli
} // namespace llvm

#endif // LLVM_LIB_CLI_TRAILERPARTS_H
