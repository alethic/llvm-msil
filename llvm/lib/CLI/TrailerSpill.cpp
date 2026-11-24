//===- TrailerSpill.cpp - The touch of a library whose data spills --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file writes the spilled-data touch into the module initializer of a
// two-phase output (ImplementationPlan.md FX-1, 5.9.10, 5.9.15) and counts
// the fields that the trailer makes in '<msil>Data'.
//
// A -shared library initializes in two phases: '<msil>Data'::.cctor (phase 1)
// allocates and fills the image and stores the address fields, and
// <Module>::.cctor runs the rest. When '<msil>Data' has more fields than one
// type may hold, msil-ld moves the rest into spill classes ('<msil>Spill<N>'),
// each with a .cctor that reads a field of '<msil>Data', so that phase 1 has
// run before code reads a spilled field. Phase 1's stores into the spilled
// fields run those .cctors in turn, so the type initializers of '<msil>Data'
// and of a spill class depend on each other: when one thread first touches a
// spill class while another first touches '<msil>Data', the CLR breaks the
// deadlock (ECMA-335 II.10.5.3.3) and may let the first thread read a spilled
// field before phase 1 stored it, a zero address. msil-ld therefore sets
// TrailerInput::SpilledData for such a library when it has a phase 1 (in
// one without, nothing stores the spilled fields), and its <Module>::.cctor
// then starts, after the portable guard if there is one and before the
// 'ldtoken's of phase 2, with
//
//   ldsfld native int '<msil>Data'::'<msil>data'
//   pop
//
// so that phase 1 runs inside the module initializer, on one thread, with
// each spill class's .cctor nested in it, and every other thread that uses
// the library waits for the module initializer, which runs before any other
// type initializer of the module (ECMA-335 II.10.5.3). '<msil>data' is a
// field of the trailer, which never spills. The spill classes keep their
// .cctors.
//
//===----------------------------------------------------------------------===//

#include "TrailerParts.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/Trailer.h"
#include <cassert>

using namespace llvm;
using namespace llvm::cli;

void cli::emitSpilledDataTouch(TrailerSink &Sink, TrailerMethod &Cctor) {
  assert(Cctor.Name == ModuleInitializerName &&
         Cctor.Owner == TrailerOwner::Module &&
         "the touch is code of <Module>::.cctor");
  ILBuilder &B = Cctor.Body;
  // The value is not needed: the read runs '<msil>Data'::.cctor unless it
  // has already run.
  EntityRef Data = Sink.getDataField();
  assert(Data != 0 && "no entity for '<msil>data'");
  B.emitToken(Opcode::Ldsfld, TokenRef::getExternal(Data));
  B.emit(Opcode::Pop);
}

unsigned cli::countTrailerDataFields(const TrailerInput &In) {
  // Only a two-phase output has '<msil>Data'; a one-phase output keeps
  // '<msil>data' in <Module>.
  if (!In.TwoPhase)
    return 0;
  unsigned Fields = 0;
  // '<msil>data', which the sink makes in '<msil>Data' when phase 1 stores
  // the image base in it (the relocations against library exports read it
  // too, and need an image) or when the touch reads it. The RVA fields of the
  // image and of the relocation table stay in <Module> (5.9.15), and a
  // two-phase output makes no '<msil>argc' and '<msil>argv'.
  if (In.Image || In.SpilledData)
    ++Fields;
  // A part that makes another field of '<msil>Data' counts it here too, so
  // that this stays exactly the number of fields that buildTrailer makes
  // there, on which msil-ld's decision to spill relies (CLITrailerTest checks
  // it on every input shape).
  return Fields;
}
