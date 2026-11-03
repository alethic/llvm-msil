//===- TrailerGuard.cpp - The portable width and byte-order guard ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file writes the width and byte-order guard of a portable output
// (msil64-unknown-dotnet, ImplementationPlan.md 5.18.7 PE4-PE7). Its images
// are AnyCPU (PE32, machine I386, ILONLY), so every runtime loads them, also
// into a 32-bit process, where the native ints of decision D2 would silently
// compute 32-bit results, and on a big-endian runtime, where the image copy
// and every ldind would read bytes in the wrong order. The guard is the first
// code of <Module>::.cctor, which runs before any other type initializer and
// any global method of the module, also when a host calls an export of a
// library, and whose failure is sticky:
//
//     ldc.i4.m1
//     conv.i          // sign-extends: all ones in a native int
//     conv.u8         // 0x00000000FFFFFFFF in a 32-bit process
//     ldc.i8 -1
//     bne.un guard_width
//     ldc.i4.1
//     call uint8[] System.BitConverter::GetBytes(int32)
//     ldc.i4.0
//     ldelem.u1       // 1 in a little-endian process
//     brfalse guard_order
//     ...             // the rest of the initializer, then 'ret'
//   guard_width:
//     ldstr "<assembly> is built for <triple> and needs a 64-bit .NET
//            process; this process is 32-bit"
//     newobj instance void System.PlatformNotSupportedException::.ctor(string)
//     throw
//   guard_order:
//     ldstr "<assembly> is built for <triple> and needs a little-endian .NET
//            process"
//     newobj instance void System.PlatformNotSupportedException::.ctor(string)
//     throw
//
// The sign extension through conv.i is load-bearing: conv.u would zero-extend
// the int32 -1 and fail on 64-bit processes too. The byte-order test reads
// the first element of a managed array, which verifies, unlike an ldind.u1
// of an int32 local (brief Q2). A thrown exception, not Environment.FailFast,
// which prints nothing on the .NET Framework console, and not
// Environment.Exit, which would end a host that only loaded a library
// (brief Q1).
//
//===----------------------------------------------------------------------===//

#include "TrailerParts.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/Trailer.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <string>

using namespace llvm;
using namespace llvm::cli;

/// The names that a text sink prints for the two failure labels.
static constexpr char WidthLabelName[] = "guard_width";
static constexpr char OrderLabelName[] = "guard_order";

/// T-P10's two messages (plan 5.18.7 PE6).
static std::string getWidthMessage(const PortableGuard &G) {
  return (Twine(G.Assembly) + " is built for " + G.Triple + " and needs a " +
          Twine(unsigned(G.PointerSize) * 8) +
          "-bit .NET process; this process is 32-bit")
      .str();
}

static std::string getOrderMessage(const PortableGuard &G) {
  return (Twine(G.Assembly) + " is built for " + G.Triple +
          " and needs a little-endian .NET process")
      .str();
}

PortableGuardLabels cli::emitPortableGuard(TrailerSink &Sink,
                                           const PortableGuard &G,
                                           TrailerMethod &Cctor) {
  // msil32's guard (a 32-bit process only) is P-4's, which defines its
  // envelope; msil32 is refused long before a trailer is built (T-P1).
  if (G.PointerSize != 8)
    reportFatalInternalError(Twine("MSIL trailer: the portable guard for ") +
                             Twine(unsigned(G.PointerSize)) +
                             "-byte pointers is not implemented until P-4");
  ILBuilder &B = Cctor.Body;
  assert(Cctor.Name == ModuleInitializerName &&
         Cctor.Owner == TrailerOwner::Module && Cctor.LabelNames.empty() &&
         "the guard is the first code of <Module>::.cctor");
  ILLabel Width = B.createLabel();
  Cctor.LabelNames.push_back(WidthLabelName);
  ILLabel Order = B.createLabel();
  Cctor.LabelNames.push_back(OrderLabelName);
  assert(Width.Id == 0 && Order.Id == 1 && "the guard's labels come first");

  // The width: (uint64)(native int)-1 is all ones only in a 64-bit process.
  B.emitLdcI4(-1);
  B.emit(Opcode::Conv_i);
  B.emit(Opcode::Conv_u8);
  B.emitImm(Opcode::Ldc_i8, -1);
  B.emitBranch(Opcode::Bne_un, Width);

  // The byte order: BitConverter.GetBytes(1)[0] is 1 only in a little-endian
  // process.
  B.emitLdcI4(1);
  B.emitCall(Opcode::Call,
             TokenRef::getExternal(
                 Sink.getBCLMember(BCLMemberId::BitConverter_GetBytes)),
             1, 1);
  B.emitLdcI4(0);
  B.emit(Opcode::Ldelem_u1);
  B.emitBranch(Opcode::Brfalse, Order);
  return {Width, Order};
}

void cli::emitPortableGuardFailures(TrailerSink &Sink, const PortableGuard &G,
                                    TrailerMethod &Cctor,
                                    const PortableGuardLabels &Labels) {
  assert(Labels.Width.isValid() && Labels.Order.isValid() &&
         Labels.Width.Id < Cctor.LabelNames.size() &&
         Labels.Order.Id < Cctor.LabelNames.size() &&
         Cctor.LabelNames[Labels.Width.Id] == WidthLabelName &&
         Cctor.LabelNames[Labels.Order.Id] == OrderLabelName &&
         "emitPortableGuard created the labels in this initializer");
  ILBuilder &B = Cctor.Body;
  EntityRef Ctor =
      Sink.getBCLMember(BCLMemberId::PlatformNotSupportedException_ctor);
  auto EmitThrow = [&](ILLabel L, const std::string &Message) {
    B.placeLabel(L);
    B.emitToken(Opcode::Ldstr,
                TokenRef::getExternal(Sink.addUserString(Message)));
    B.emitCall(Opcode::Newobj, TokenRef::getExternal(Ctor), 1, 1);
    B.emit(Opcode::Throw);
  };
  EmitThrow(Labels.Width, getWidthMessage(G));
  EmitThrow(Labels.Order, getOrderMessage(G));
}
