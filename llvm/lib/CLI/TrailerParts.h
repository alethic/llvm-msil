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
// concern (5.14.5 S1, 5.13.3): the UCRT stdio options (TrailerStdio.cpp).
// It is private to lib/CLI: the producers fill cli::TrailerInput, and only
// the builder calls the parts.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CLI_TRAILERPARTS_H
#define LLVM_LIB_CLI_TRAILERPARTS_H

namespace llvm {
namespace cli {

struct TrailerInput;
struct TrailerMethod;

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
