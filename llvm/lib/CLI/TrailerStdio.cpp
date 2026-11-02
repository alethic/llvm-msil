//===- TrailerStdio.cpp - The UCRT stdio options in the trailer -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file writes the UCRT's stdio options into the module initializer of a
// *-windows-msvc-msil output (ImplementationPlan.md 5.15.2, hosted-libc H3).
// UCRT's inline printf and scanf functions pass the word that
// __local_stdio_printf_options() (resp. __local_stdio_scanf_options())
// returns to __stdio_common_v*, and the native CRT startup ORs vcruntime's
// defaults into both words before any C initializer runs. An MSIL program
// has no CRT startup, so its initializer does the same: before the
// constructors, for each of the two functions that the output defines,
//
//   call native int <f>()
//   dup
//   ldind.i8
//   ldc.i8 <bits>
//   or
//   stind.i8
//
// Without these bits the UCRT rounds exactly representable halves away from
// zero and reads %s in the wide functions as a narrow string.
//
//===----------------------------------------------------------------------===//

#include "TrailerParts.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/Trailer.h"

using namespace llvm;
using namespace llvm::cli;

bool cli::needsUCRTStdioOptions(const TrailerInput &In) {
  return In.UCRTStdio &&
         (In.UCRTStdio->PrintfOptions || In.UCRTStdio->ScanfOptions);
}

/// *<Options>() |= Bits, the option word being an unsigned long long.
static void emitOrOptions(ILBuilder &B, EntityRef Options, uint64_t Bits) {
  B.emitCall(Opcode::Call, TokenRef::getExternal(Options), 0, 1);
  B.emit(Opcode::Dup);
  B.emit(Opcode::Ldind_i8);
  B.emitImm(Opcode::Ldc_i8, static_cast<int64_t>(Bits));
  B.emit(Opcode::Or);
  B.emit(Opcode::Stind_i8);
}

void cli::emitUCRTStdioOptions(const TrailerInput &In, TrailerMethod &M) {
  if (!needsUCRTStdioOptions(In))
    return;
  const StdioOptions &S = *In.UCRTStdio;
  if (S.PrintfOptions)
    emitOrOptions(M.Body, S.PrintfOptions, S.PrintfBits);
  if (S.ScanfOptions)
    emitOrOptions(M.Body, S.ScanfOptions, S.ScanfBits);
}
