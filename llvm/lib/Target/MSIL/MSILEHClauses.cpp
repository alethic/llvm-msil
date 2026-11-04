//===-- MSILEHClauses.cpp - IL exception clauses of methods ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the whole-body '.try' of the setjmp wrappers and the
/// body of the throw helper (ImplementationPlan.md 5.15.4). MSILLowerSjLj
/// makes both functions; they meet this file only through the attributes
/// "msil-sjlj-invoke"="<status index>" and "msil-sjlj-throw".
///
/// A longjmp arms the target frame's status block {i32 label, i32 val} and
/// throws ExternalException(null, (int)status). A wrapper's filter accepts
/// an exception only when it is an ExternalException, its HResult is the low
/// 32 bits of the wrapper's own status block, and that block is armed (label
/// not 0); no exception type is shared between assemblies. The handler only
/// leaves: the caller reads the armed block after the call and branches to
/// its dispatch block.
///
//===----------------------------------------------------------------------===//

#include "MSILEHClauses.h"
#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MCTargetDesc/MSILTargetStreamer.h"
#include "MSILAsmPrinter.h"
#include "MSILMCInstLower.h"
#include "MSILMachineFunctionInfo.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>

using namespace llvm;

/// The attributes of MSILLowerSjLj's functions (plan 5.15.4).
static constexpr StringLiteral SjLjInvokeAttr = "msil-sjlj-invoke";
static constexpr StringLiteral SjLjThrowAttr = "msil-sjlj-throw";

/// The least .maxstack of the methods with AsmPrinter-written code (plan
/// 5.15.4: the filter and the throw body).
static constexpr unsigned SjLjMaxStack = 3;

MSIL::WholeBodyKind MSIL::getWholeBodyKind(const Function &F) {
  return F.hasFnAttribute(SjLjInvokeAttr) ? WholeBodyKind::SjLjFilter
                                          : WholeBodyKind::None;
}

bool MSIL::isSjLjThrowHelper(const Function &F) {
  return F.hasFnAttribute(SjLjThrowAttr);
}

unsigned MSIL::getMinMaxStack(const Function &F) {
  if (getWholeBodyKind(F) != WholeBodyKind::None || isSjLjThrowHelper(F))
    return SjLjMaxStack;
  return 0;
}

static MCInst makeInst(cli::Opcode Op) {
  MCInst MI;
  MI.setOpcode(MSIL::getMCOpcodeForCIL(Op));
  return MI;
}

static MCInst makeInst(cli::Opcode Op, const MCOperand &MO) {
  MCInst MI = makeInst(Op);
  MI.addOperand(MO);
  return MI;
}

/// An instruction whose token is the BCL member \p Id ('[mscorlib]' in
/// whole-program text, '[msil.bcl]' in an object and its text).
static MCInst makeBCLInst(cli::Opcode Op, cli::BCLMemberId Id, MCContext &Ctx) {
  return makeInst(Op,
                  MCOperand::createExpr(MSILMemberRefExpr::createBCL(Id, Ctx)));
}

void MSIL::emitSjLjThrowBody(const Function &F, const TargetMachine &TM,
                             MCContext &Ctx,
                             function_ref<void(const MCInst &)> EmitInst) {
  // MSILLowerSjLj declares it 'void @__msil_sjlj_throw(ptr)': one native int
  // argument, the address of the target frame's status block.
  SmallVector<MVT, 2> VTs;
  if (F.arg_size() == 1 && F.getReturnType()->isVoidTy() &&
      F.getArg(0)->getType()->isPointerTy())
    MSIL::computeLegalValueVTs(F, TM, F.getArg(0)->getType(), VTs);
  if (VTs.size() != 1)
    reportFatalInternalError("MSIL: the throw helper '" + F.getName() +
                             "' (\"msil-sjlj-throw\") does not take one "
                             "pointer and return void");
  // HResult = the low 32 bits of the status block's address, which no other
  // live frame of the thread shares (sj 4.4). No message: 'ldstr' needs user
  // strings, which objects do not have.
  EmitInst(makeInst(cli::Opcode::Ldnull));
  EmitInst(MSIL::buildVarInst(cli::Opcode::Ldarg, 0));
  EmitInst(makeInst(cli::Opcode::Conv_i4));
  EmitInst(makeBCLInst(cli::Opcode::Newobj,
                       cli::BCLMemberId::ExternalException_ctor, Ctx));
  EmitInst(makeInst(cli::Opcode::Throw));
}

/// The IL argument index of the status block parameter of the wrapper \p F,
/// "msil-sjlj-invoke"="<i>" with i its IR parameter index: the IL parameters
/// are those of MSIL::computeSignatureVTs, a hidden result pointer first
/// when the result takes more than one value, then the values of each IR
/// parameter.
static unsigned getStatusArgument(const Function &F, const TargetMachine &TM) {
  StringRef Value = F.getFnAttribute(SjLjInvokeAttr).getValueAsString();
  unsigned Index;
  SmallVector<MVT, 2> VTs;
  if (!Value.getAsInteger(10, Index) && Index < F.arg_size() &&
      F.getArg(Index)->getType()->isPointerTy())
    MSIL::computeLegalValueVTs(F, TM, F.getArg(Index)->getType(), VTs);
  if (VTs.size() != 1)
    reportFatalInternalError("MSIL: function '" + F.getName() + "' has \"" +
                             SjLjInvokeAttr + "\"=\"" + Value +
                             "\", which names no pointer parameter");
  VTs.clear();
  MSIL::computeLegalValueVTs(F, TM, F.getReturnType(), VTs);
  unsigned ILIndex = VTs.size() > 1 ? 1 : 0;
  for (unsigned I = 0; I != Index; ++I) {
    VTs.clear();
    MSIL::computeLegalValueVTs(F, TM, F.getArg(I)->getType(), VTs);
    ILIndex += VTs.size();
  }
  return ILIndex;
}

MSILWholeBodyTry::MSILWholeBodyTry(const Function &F, const TargetMachine &TM,
                                   MCContext &Ctx, unsigned FunctionNumber,
                                   ArrayRef<MVT> Results,
                                   SmallVectorImpl<cli::TypeSig> &Locals)
    : Ctx(Ctx), StatusArg(getStatusArgument(F, TM)) {
  assert(MSIL::getWholeBodyKind(F) == MSIL::WholeBodyKind::SjLjFilter &&
         "only setjmp wrappers have the fixed filter");
  // R, of the method's IL return type, after the other locals.
  assert(Results.size() <= 1 && "a CIL method returns at most one value");
  if (!Results.empty()) {
    ResultLocal = Locals.size();
    Locals.push_back(MSIL::toTypeSig(MSIL::toValType(
        Results.front(), F.getDataLayout().getPointerSizeInBits())));
  }
  // '@EH<f>_<k>' beside the block labels '@BB<f>_<b>'.
  for (unsigned K = 0; K != NumLabels; ++K)
    Labels[K] =
        Ctx.createBlockSymbol("EH" + Twine(FunctionNumber) + "_" + Twine(K),
                              /*AlwaysEmit=*/false);
}

void MSILWholeBodyTry::emitBegin(MSILTargetStreamer &TS, MCStreamer &OS) {
  // The protected block is the whole body and ends where the filter starts;
  // the handler ends at the tail, which every 'leave' targets.
  TS.emitExceptionClause(cli::ExceptionClauseKind::Filter, Labels[TryBegin],
                         Labels[FilterBegin], Labels[HandlerBegin],
                         Labels[Done], Labels[FilterBegin]);
  Section = OS.getCurrentSectionOnly();
  // The stack is empty at method entry, as a '.try' needs (ECMA-335
  // II.19).
  OS.emitLabel(Labels[TryBegin]);
}

void MSILWholeBodyTry::emitEnd(
    MCStreamer &OS, function_ref<void(const MCInst &)> EmitInst) const {
  assert(Section && "emitEnd before emitBegin");
  MCSection *FunctionSection = OS.getCurrentSectionOnly();
  OS.switchSection(Section);
  auto Label = [&](unsigned K) { OS.emitLabel(Labels[K]); };
  auto Inst = [&](const MCInst &I) { EmitInst(I); };
  auto Op = [&](cli::Opcode O) { Inst(makeInst(O)); };
  // The filter and handler branches stay inside their few bytes, so they
  // take their short forms.
  auto Branch = [&](cli::Opcode O, unsigned K) {
    Inst(makeInst(
        O, MCOperand::createExpr(MCSymbolRefExpr::create(Labels[K], Ctx))));
  };
  auto BCL = [&](cli::Opcode O, cli::BCLMemberId Id) {
    Inst(makeBCLInst(O, Id, Ctx));
  };
  auto LoadStatus = [&] {
    Inst(MSIL::buildVarInst(cli::Opcode::Ldarg, StatusArg));
  };

  // The filter: the exception object is on the stack; 'endfilter' takes 1
  // (handle) or 0 (continue the search).
  Label(FilterBegin);
  BCL(cli::Opcode::Isinst, cli::BCLMemberId::ExternalException);
  Op(cli::Opcode::Dup);
  Branch(cli::Opcode::Brtrue_s, FilterMatch);
  Op(cli::Opcode::Pop);
  Inst(MSIL::buildLdcI4(0));
  Branch(cli::Opcode::Br_s, FilterEnd);
  Label(FilterMatch);
  // Thrown for this frame's status block ...
  BCL(cli::Opcode::Callvirt, cli::BCLMemberId::Exception_get_HResult);
  LoadStatus();
  Op(cli::Opcode::Conv_i4);
  Branch(cli::Opcode::Bne_un_s, FilterNoMatch);
  // ... and the block is armed (its label is not 0).
  LoadStatus();
  Op(cli::Opcode::Ldind_i4);
  Inst(MSIL::buildLdcI4(0));
  Op(cli::Opcode::Cgt_un);
  Branch(cli::Opcode::Br_s, FilterEnd);
  Label(FilterNoMatch);
  Inst(MSIL::buildLdcI4(0));
  Label(FilterEnd);
  Op(cli::Opcode::Endfilter);

  // The handler: the status block is armed already; drop the exception.
  Label(HandlerBegin);
  Op(cli::Opcode::Pop);
  Branch(cli::Opcode::Leave_s, Done);

  // The tail, after the handler: a CIL method never returns from inside a
  // protected block. After a caught longjmp R holds no value; the caller
  // ignores the result and branches to its dispatch block.
  Label(Done);
  if (ResultLocal)
    Inst(MSIL::buildVarInst(cli::Opcode::Ldloc, *ResultLocal));
  Op(cli::Opcode::Ret);
  OS.switchSection(FunctionSection);
}
