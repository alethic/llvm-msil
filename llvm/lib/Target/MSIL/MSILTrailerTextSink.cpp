//===-- MSILTrailerTextSink.cpp - The text sink of the trailer ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements MSILTrailerTextSink, the sink through which the MSIL
/// AsmPrinter prints the module trailer of whole-program text (plan 5.9.10),
/// and the AsmPrinter's whole-program fills of cli::TrailerInput (plan
/// 5.13.1 L9).
///
//===----------------------------------------------------------------------===//

#include "MSILTrailerTextSink.h"
#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "MCTargetDesc/MSILTargetStreamer.h"
#include "MSILAsmPrinter.h"
#include "MSILMCInstLower.h"
#include "MSILUtilities.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/CLI/TrailerDefs.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <utility>

using namespace llvm;

cli::EntityRef MSILTrailerTextSink::addEntity(Entity E) {
  Entities.push_back(std::move(E));
  return Entities.size();
}

cli::EntityRef MSILTrailerTextSink::add(const MSILMemberRefExpr *Ref) {
  Entity E;
  E.Ref = Ref;
  return addEntity(std::move(E));
}

cli::EntityRef MSILTrailerTextSink::getDataField() {
  if (!DataField)
    DataField =
        add(MSILMemberRefExpr::createField(cli::DataFieldName, AP.OutContext));
  return DataField;
}

cli::EntityRef MSILTrailerTextSink::addRVAData(StringRef TypeName,
                                               StringRef FieldName,
                                               ArrayRef<uint8_t> Bytes) {
  RVA.push_back({FieldName.str(), Bytes.vec()});
  return add(MSILMemberRefExpr::createValueTypeField(
      Saver.save(FieldName), Saver.save(TypeName), AP.OutContext));
}

cli::EntityRef MSILTrailerTextSink::getBCLMember(cli::BCLMemberId Id) {
  return add(MSILMemberRefExpr::createBCL(Id, AP.OutContext));
}

cli::EntityRef MSILTrailerTextSink::declareHelper(StringRef Name,
                                                  const cli::MethodSig &Sig) {
  Entity E;
  E.K = Entity::Helper;
  E.Name = Name.str();
  E.Sig = Sig;
  return addEntity(std::move(E));
}

cli::EntityRef MSILTrailerTextSink::addUserString(StringRef UTF8) {
  Entity E;
  E.K = Entity::String;
  E.Name = UTF8.str();
  return addEntity(std::move(E));
}

cli::EntityRef MSILTrailerTextSink::addTrailerField(StringRef Name) {
  StringRef Saved = Saver.save(Name);
  TrailerFields.push_back(Saved);
  return add(MSILMemberRefExpr::createField(Saved, AP.OutContext));
}

void MSILTrailerTextSink::emitMethod(const cli::TrailerMethod &M) {
  Methods.push_back(M);
}

ArrayRef<StringRef> MSILTrailerTextSink::getTrailerFields() const {
  return TrailerFields;
}

ArrayRef<uint8_t> MSILTrailerTextSink::getRVAData(StringRef FieldName) const {
  for (const RVAData &D : RVA)
    if (D.FieldName == FieldName)
      return D.Bytes;
  return {};
}

void MSILTrailerTextSink::emitMethods() {
  for (const cli::TrailerMethod &M : Methods)
    printMethod(M);
}

void MSILTrailerTextSink::printMethod(const cli::TrailerMethod &M) {
  // Only msil-ld's -shared outputs initialize in two phases (plan 5.9.10).
  assert(M.Owner == cli::TrailerOwner::Module &&
         "whole-program output has one module initializer");
  MSILTargetStreamer &TS = AP.getTargetStreamer();
  MSILMethodDecl Decl;
  Decl.Name = M.Name;
  Decl.Public = false;
  // The implementation flags that this sink prints (plan 5.14.4 T4): those of
  // the trailer definitions, which are helpers, and of them only
  // 'noinlining', which MSILTargetStreamer::emitHelperBegin prints.
  bool NoInlining = M.ImplFlags == cli::MethodImplAttributes::NoInlining;
  if (M.ImplFlags != cli::MethodImplAttributes{} &&
      (!NoInlining || M.Name == cli::ModuleInitializerName ||
       M.Name == cli::EntryPointName))
    reportFatalInternalError(Twine("MSIL: the implementation flags of the "
                                   "trailer method '") +
                             M.Name + "' are not supported by this sink");
  if (M.Name == cli::ModuleInitializerName) {
    Decl.Kind = MSILMethodKind::ModuleInitializer;
    TS.emitMethodBegin(Decl);
  } else if (M.Name == cli::EntryPointName) {
    Decl.Kind = MSILMethodKind::EntryPoint;
    TS.emitMethodBegin(Decl);
    TS.emitEntryPoint();
  } else {
    TS.emitHelperBegin(M.Name, M.Flags, M.Sig, M.ImplFlags);
  }
  const cli::ILBuilder &B = M.Body;
  TS.emitMaxStack(B.getMaxStack());
  // Never 'init' (decision D13): every local is written before it is read.
  TS.emitLocals(M.Locals);

  // Method-local labels, temporary symbols like the block labels: never the
  // symbol of a C function or variable of the same IL name. Text names them
  // '@<name>' (MCAsmInfo's InternalSymbolPrefix), e.g. '@argv_loop'.
  MCContext &Ctx = AP.OutContext;
  SmallVector<MCSymbol *, 4> LabelSyms;
  SmallVector<std::pair<uint32_t, unsigned>, 4> LabelOffsets;
  for (unsigned Id = 0, E = M.LabelNames.size(); Id != E; ++Id) {
    LabelSyms.push_back(
        Ctx.createTempSymbol(M.LabelNames[Id], /*AlwaysAddSuffix=*/false));
    LabelOffsets.push_back({B.getLabelOffset(cli::ILLabel{Id}), Id});
  }
  auto Label = [&](cli::ILLabel L) {
    assert(L.isValid() && L.Id < LabelSyms.size() && "unnamed label");
    return MCOperand::createExpr(MCSymbolRefExpr::create(LabelSyms[L.Id], Ctx));
  };
  // The labels in offset order, each emitted before the instruction at its
  // offset (or after the last one).
  llvm::sort(LabelOffsets);
  auto *NextLabel = LabelOffsets.begin();
  auto EmitLabelsUpTo = [&](uint32_t Offset) {
    for (; NextLabel != LabelOffsets.end() && NextLabel->first <= Offset;
         ++NextLabel)
      AP.OutStreamer->emitLabel(LabelSyms[NextLabel->second]);
  };

  for (const cli::ILInstr &I : B.instructions()) {
    EmitLabelsUpTo(I.Offset);
    MCInst Inst;
    Inst.setOpcode(MSIL::getMCOpcodeForCIL(I.Op));
    switch (cli::getOpcodeInfo(I.Op).Operand) {
    case cli::OperandKind::InlineNone:
      break;
    case cli::OperandKind::ShortInlineVar:
    case cli::OperandKind::InlineVar:
    case cli::OperandKind::ShortInlineI:
    case cli::OperandKind::InlineI:
    case cli::OperandKind::InlineI8:
      Inst.addOperand(MCOperand::createImm(I.Imm));
      break;
    case cli::OperandKind::ShortInlineR:
      Inst.addOperand(MCOperand::createSFPImm(static_cast<uint32_t>(I.FPBits)));
      break;
    case cli::OperandKind::InlineR:
      Inst.addOperand(MCOperand::createDFPImm(I.FPBits));
      break;
    case cli::OperandKind::ShortInlineBrTarget:
    case cli::OperandKind::InlineBrTarget:
      Inst.addOperand(Label(I.Target));
      break;
    case cli::OperandKind::InlineMethod:
    case cli::OperandKind::InlineField:
    case cli::OperandKind::InlineType:
    case cli::OperandKind::InlineTok:
    case cli::OperandKind::InlineSig:
    case cli::OperandKind::InlineString: {
      assert(I.Token.getKind() == cli::TokenRef::Kind::External &&
             I.Token.getValue() != 0 && I.Token.getValue() <= Entities.size() &&
             "unknown entity");
      const Entity &E = Entities[I.Token.getValue() - 1];
      if (E.K == Entity::String) {
        TS.emitStringInstruction(I.Op, E.Name);
        continue;
      }
      if (E.K == Entity::Helper) {
        TS.emitHelperInstruction(I.Op, E.Name, E.Sig);
        continue;
      }
      Inst.addOperand(MCOperand::createExpr(E.Ref));
      break;
    }
    case cli::OperandKind::InlineSwitch:
      for (cli::ILLabel L : B.getSwitchTargets(I))
        Inst.addOperand(Label(L));
      break;
    }
    // The module trailer has no MachineFunction: the TargetMachine's
    // subtarget (MSIL has one processor and no features, decision D7).
    AP.OutStreamer->emitInstruction(Inst, AP.TM.getMCSubtargetInfo());
  }
  EmitLabelsUpTo(B.getCodeSize());
  TS.emitMethodEnd(M.Name);
}

void MSILAsmPrinter::fillTrailerDefs(Module &M, MSILTrailerTextSink &Sink,
                                     cli::TrailerInput &In) {
  // Each trailer definition (plan 5.14.4 T6) that the module references with
  // the registry's signature and does not define, once, in TrailerDefId
  // order: through a library call of its name (LowerCall rejects one of
  // another signature) or an undefined function of its name, called or used
  // by address. emitPInvokeDeclarations gives those references no P/Invoke;
  // the code names the method by name and signature (MSILMemberRefExpr), so
  // the request's entity serves only cli::buildTrailer.
  for (const cli::TrailerDefInfo &D : cli::getTrailerDefs()) {
    if (!MSIL::getOfferedTrailerDef(M, D.Name))
      continue;
    auto References = [&](StringRef Name, const MSIL::Signature &Sig) {
      return Name == D.Name && MSIL::matchesTrailerDefSignature(D, Sig);
    };
    auto IsLibCall = [&](const auto &P) {
      return References(P.first, *P.second);
    };
    auto IsExternFunc = [&](const auto &P) {
      return References(MSIL::getILName(*P.first, TM), *P.second);
    };
    if (none_of(ExtSymOrder, IsLibCall) && none_of(ExternFuncs, IsExternFunc))
      continue;
    In.Defs.push_back(
        {D.Id, Sink.declareHelper(D.Name, cli::getTrailerDefSignature(D))});
  }
}

void MSILAsmPrinter::fillTrailerUCRTStdio(Module &M, MSILTrailerTextSink &Sink,
                                          cli::TrailerInput &In) {
  // The UCRT's <stdio.h> defines both functions inline (COMDAT) in every TU
  // that includes it, each returning the address of its own options word,
  // which the printf and scanf functions pass to __stdio_common_v*. The
  // module initializer sets the option bits there, as msil-ld's does for a
  // link (plan 5.15.2, hl H3). In msil-ld's linked text (--lto-emit-asm) the
  // bits are the link's, which msil-ld passes as the module flags
  // "msil-ucrt-printf-bits" and "msil-ucrt-scanf-bits", so that the text and
  // the PE agree; standalone whole-program text has neither flag and keeps
  // the default bits. A flag that is not an integer constant counts as
  // absent. A definition of another signature is not the UCRT's and is left
  // alone.
  if (!TM.getTargetTriple().isWindowsMSVCEnvironment())
    return;
  cli::StdioOptions Options;
  auto ReadBits = [&](StringRef Flag, uint64_t &Bits) {
    if (const auto *C =
            mdconst::dyn_extract_or_null<ConstantInt>(M.getModuleFlag(Flag)))
      Bits = C->getLimitedValue();
  };
  ReadBits("msil-ucrt-printf-bits", Options.PrintfBits);
  ReadBits("msil-ucrt-scanf-bits", Options.ScanfBits);
  auto Lookup = [&](StringRef Name) -> cli::EntityRef {
    const Function *F = MSIL::getFunctionByILName(M, Name);
    if (!F || F->isIntrinsic() || F->isDeclarationForLinker())
      return 0;
    const MSIL::Signature &Sig = getSignature(*F);
    if (!Sig.Params.empty() || Sig.Results.size() != 1 ||
        Sig.Results[0] != MSIL::ValType::NativeInt)
      return 0;
    return Sink.add(MSILMCInstLower::createMethodRef(
        *F, MSIL::getILName(*F, TM), Sig, OutContext));
  };
  Options.PrintfOptions = Lookup("__local_stdio_printf_options");
  Options.ScanfOptions = Lookup("__local_stdio_scanf_options");
  if (Options.PrintfOptions || Options.ScanfOptions)
    In.UCRTStdio = Options;
}
