//===-- MSILMCExpr.cpp - MSIL member reference expressions ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Creation and printing of MSILMemberRefExpr.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

const MSILMemberRefExpr *
MSILMemberRefExpr::createMethod(StringRef ILName, const MSIL::Signature *S,
                                MCContext &Ctx) {
  assert(S && "a method reference needs a signature");
  return new (Ctx)
      MSILMemberRefExpr(Method, Ctx.allocateString(ILName), S, /*Offset=*/0);
}

const MSILMemberRefExpr *MSILMemberRefExpr::createField(StringRef ILName,
                                                        int64_t Offset,
                                                        MCContext &Ctx) {
  return new (Ctx)
      MSILMemberRefExpr(Field, Ctx.allocateString(ILName), nullptr, Offset);
}

const MSILMemberRefExpr *MSILMemberRefExpr::createSig(const MSIL::Signature *S,
                                                      MCContext &Ctx) {
  assert(S && "a call-site signature reference needs a signature");
  return new (Ctx) MSILMemberRefExpr(StandAloneSig, StringRef(), S,
                                     /*Offset=*/0);
}

const MSILMemberRefExpr *MSILMemberRefExpr::createBCL(StringRef Text,
                                                      MCContext &Ctx) {
  return new (Ctx) MSILMemberRefExpr(BCLMember, Ctx.allocateString(Text),
                                     nullptr, /*Offset=*/0);
}

void MSILMemberRefExpr::printImpl(raw_ostream &OS, const MCAsmInfo *) const {
  switch (K) {
  case Method:
    // int32 'sum'(native int, int32)
    MSIL::printResult(OS, *Sig);
    OS << ' ';
    MSIL::printQuotedName(OS, Name);
    MSIL::printParamList(OS, *Sig);
    return;
  case Field:
    // native int 'table'
    OS << "native int ";
    MSIL::printQuotedName(OS, Name);
    return;
  case StandAloneSig:
    // [unmanaged cdecl ]int32(native int, int32)
    switch (Sig->CC) {
    case MSIL::CallConv::Managed:
      break;
    case MSIL::CallConv::UnmanagedCdecl:
      OS << "unmanaged cdecl ";
      break;
    case MSIL::CallConv::UnmanagedStdcall:
      OS << "unmanaged stdcall ";
      break;
    }
    MSIL::printResult(OS, *Sig);
    MSIL::printParamList(OS, *Sig);
    return;
  case BCLMember:
    OS << Name;
    return;
  }
  llvm_unreachable("MSIL: unknown member reference kind");
}

bool MSILMemberRefExpr::isEqualTo(const MCExpr *X) const {
  const auto *E = dyn_cast<MSILMemberRefExpr>(X);
  if (!E || E->K != K || E->Name != Name || E->Offset != Offset)
    return false;
  if (!Sig || !E->Sig)
    return Sig == E->Sig;
  return *Sig == *E->Sig;
}
