//===-- MSILMCInstLower.cpp - Convert MSIL MachineInstr to an MCInst ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyMCInstLower.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains code to lower MSIL MachineInstrs to MCInsts: one CIL_
/// record (MSILInstrCIL.td) per CIL instruction, in the shortest forms
/// (plan M18.a (2), (3); Q26), so that the printer and the encoder read one
/// record and the text and the binary agree.
///
//===----------------------------------------------------------------------===//

#include "MSILMCInstLower.h"
#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSILAsmPrinter.h"
#include "MSILUtilities.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

using namespace llvm;

//===----------------------------------------------------------------------===//
// CIL_ records.
//===----------------------------------------------------------------------===//

unsigned MSIL::getMCOpcodeForCIL(cli::Opcode Op) {
  switch (Op) {
#define CIL_OPCODE(Enum, Text, Value, Operand, Pop, Push, Flow)                \
  case cli::Opcode::Enum:                                                      \
    return MSIL::CIL_##Enum;
#include "llvm/BinaryFormat/CILOpcodes.def"
  }
  llvm_unreachable("MSIL: unknown CIL opcode");
}

/// The CIL opcode of the record \p Desc, or none for a MachineInstr-level
/// record that stands for several CIL instructions, for a pseudo, and for a
/// target-independent instruction (TargetOpcode::*), whose TSFlags are 0, the
/// value of 'nop'.
static std::optional<cli::Opcode> getCILOpcode(const MCInstrDesc &Desc) {
  if (!isTargetSpecificOpcode(Desc.getOpcode()))
    return std::nullopt;
  uint16_t Value = MSIL::getCILOpcodeValue(Desc);
  if (Value == MSIL::NoCILOpcode)
    return std::nullopt;
  return cli::getOpcodeByValue(Value);
}

/// The CIL opcode of the CIL_ MCInst \p MI.
static cli::Opcode getCILOpcode(const MCInst &MI, const MCInstrInfo &MCII) {
  std::optional<cli::Opcode> Op = getCILOpcode(MCII.get(MI.getOpcode()));
  if (!Op)
    reportFatalInternalError(Twine("MSIL: ") + MCII.getName(MI.getOpcode()) +
                             " is not a CIL instruction record");
  return *Op;
}

static bool isBranchTarget(cli::OperandKind K) {
  return K == cli::OperandKind::ShortInlineBrTarget ||
         K == cli::OperandKind::InlineBrTarget;
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

MCInst MSIL::buildLdcI4(int32_t Value) {
  // ldc.i4.m1 and ldc.i4.0-8 are consecutive opcodes (ECMA-335 III.3.40).
  if (Value >= -1 && Value <= 8)
    return makeInst(static_cast<cli::Opcode>(
        static_cast<uint16_t>(cli::Opcode::Ldc_i4_0) + Value));
  if (isInt<8>(Value))
    return makeInst(cli::Opcode::Ldc_i4_s, MCOperand::createImm(Value));
  return makeInst(cli::Opcode::Ldc_i4, MCOperand::createImm(Value));
}

MCInst MSIL::buildVarInst(cli::Opcode LongForm, unsigned Index) {
  // The short form, and the first of the four forms with the index in the
  // opcode, which are consecutive (ECMA-335 III.3.38, III.3.43, III.3.63).
  cli::Opcode Short;
  std::optional<cli::Opcode> Zero;
  switch (LongForm) {
  case cli::Opcode::Ldarg:
    Short = cli::Opcode::Ldarg_s;
    Zero = cli::Opcode::Ldarg_0;
    break;
  case cli::Opcode::Ldarga:
    Short = cli::Opcode::Ldarga_s;
    break;
  case cli::Opcode::Starg:
    Short = cli::Opcode::Starg_s;
    break;
  case cli::Opcode::Ldloc:
    Short = cli::Opcode::Ldloc_s;
    Zero = cli::Opcode::Ldloc_0;
    break;
  case cli::Opcode::Ldloca:
    Short = cli::Opcode::Ldloca_s;
    break;
  case cli::Opcode::Stloc:
    Short = cli::Opcode::Stloc_s;
    Zero = cli::Opcode::Stloc_0;
    break;
  default:
    reportFatalInternalError(
        Twine("MSIL: '") + cli::getOpcodeInfo(LongForm).Name +
        "' is not the long form of an argument or local instruction");
  }
  if (Zero && Index < 4)
    return makeInst(
        static_cast<cli::Opcode>(static_cast<uint16_t>(*Zero) + Index));
  // An index above 65534 does not fit; the AsmPrinter has diagnosed such a
  // method, and the encoder reports the operand.
  return makeInst(isUInt<8>(Index) ? Short : LongForm,
                  MCOperand::createImm(Index));
}

void MSIL::relaxBranches(MutableArrayRef<MCInst> Insts,
                         ArrayRef<std::pair<const MCSymbol *, unsigned>> Labels,
                         const MCInstrInfo &MCII) {
  DenseMap<const MCSymbol *, unsigned> LabelIndex;
  for (const auto &[Sym, Index] : Labels) {
    assert(Index <= Insts.size() && "a label beyond the end of the code");
    LabelIndex.try_emplace(Sym, Index);
  }

  // Every branch to a known label starts short (metadata-writer 5.2).
  struct Branch {
    unsigned Inst, Target;
    unsigned Short, Long;
  };
  SmallVector<Branch, 16> Branches;
  for (unsigned I = 0, E = Insts.size(); I != E; ++I) {
    MCInst &MI = Insts[I];
    cli::Opcode Op = getCILOpcode(MI, MCII);
    if (!isBranchTarget(cli::getOpcodeInfo(Op).Operand))
      continue;
    unsigned Short = MSIL::getMCOpcodeForCIL(*cli::getShortBranchForm(Op));
    unsigned Long = MSIL::getMCOpcodeForCIL(*cli::getLongBranchForm(Op));
    if (MI.getNumOperands() != 1)
      reportFatalInternalError(Twine("MSIL: ") + MCII.getName(MI.getOpcode()) +
                               " without one target");
    const MCOperand &MO = MI.getOperand(0);
    // A displacement (an ILAsm parser's numeric target) keeps its form.
    if (MO.isImm())
      continue;
    const auto *Ref =
        MO.isExpr() ? dyn_cast<MCSymbolRefExpr>(MO.getExpr()) : nullptr;
    auto It = Ref ? LabelIndex.find(&Ref->getSymbol()) : LabelIndex.end();
    if (It == LabelIndex.end()) {
      // Not a label of this code: only the long form is always safe.
      MI.setOpcode(Long);
      continue;
    }
    MI.setOpcode(Short);
    Branches.push_back({I, It->second, Short, Long});
  }

  // Widen until nothing changes. Widening only grows the code, so this
  // terminates, and the result does not depend on the input forms.
  SmallVector<uint64_t, 64> Offsets(Insts.size() + 1);
  bool Changed;
  do {
    uint64_t Offset = 0;
    for (unsigned I = 0, E = Insts.size(); I != E; ++I) {
      Offsets[I] = Offset;
      Offset += MSIL::getCILInstSize(Insts[I], MCII);
    }
    Offsets[Insts.size()] = Offset;
    Changed = false;
    for (const Branch &B : Branches) {
      MCInst &MI = Insts[B.Inst];
      if (MI.getOpcode() != B.Short)
        continue;
      int64_t Disp = int64_t(Offsets[B.Target]) - int64_t(Offsets[B.Inst + 1]);
      if (!isInt<8>(Disp)) {
        MI.setOpcode(B.Long);
        Changed = true;
      }
    }
  } while (Changed);
}

//===----------------------------------------------------------------------===//
// MSILMCInstLower.
//===----------------------------------------------------------------------===//

/// Reports an internal error about \p MI.
[[noreturn]] static void reportBadInstr(const MachineInstr &MI,
                                        const Twine &Problem) {
  std::string Str;
  raw_string_ostream OS(Str);
  MI.print(OS);
  reportFatalInternalError("MSIL: " + Problem + " in " + Twine(Str));
}

/// Whether \p F carries one of the C import attributes (plan 5.10.7), so that
/// object mode gives it an import stub (plan 5.8.3 R7).
static bool hasImportAttribute(const Function &F) {
  return !F.getFnAttribute("msil-import-module").getValueAsString().empty() ||
         !F.getFnAttribute("msil-import-name").getValueAsString().empty();
}

const MSILMemberRefExpr *
MSILMCInstLower::createMethodRef(const Function &F, StringRef ILName,
                                 const MSIL::Signature &Sig, MCContext &Ctx) {
  // R1 and R7: a function defined here is a MethodDef of the module, and so
  // is the import stub of an import-attributed declaration; every other
  // function (declarations, available_externally definitions, extern_weak)
  // is a placeholder reference.
  if (!F.isDeclarationForLinker() || hasImportAttribute(F))
    return MSILMemberRefExpr::createMethod(ILName, &Sig, Ctx);
  return MSILMemberRefExpr::createExternMethod(ILName, &Sig, Ctx);
}

MCOperand MSILMCInstLower::lowerCallee(const MachineInstr &MI,
                                       const MachineOperand &MO,
                                       bool Emit) const {
  if (!Emit)
    return MCOperand::createImm(0);
  switch (MO.getType()) {
  case MachineOperand::MO_GlobalAddress: {
    const GlobalValue *GV = MO.getGlobal();
    StringRef Name = MSIL::getILName(*GV, Printer.TM);
    // MSILTargetLowering::isOffsetFoldingLegal never folds an offset into a
    // function address: 'call' names a method.
    if (MO.getOffset() != 0)
      reportFatalInternalError("MSIL: reference to function '" + Name +
                               "' with an offset");
    MSIL::Signature CallSig = getCallSiteSignature(MI);
    const auto *F = dyn_cast<Function>(GV);
    if (!F) {
      // An alias: MSILPrepareIR replaces every alias in whole-program mode,
      // so only object mode gets here; a call of a weak alias must stay
      // overridable (plan 5.8.3 R1).
      return MCOperand::createExpr(MSILMemberRefExpr::createExternMethod(
          Name, &Printer.internSignature(CallSig), Ctx));
    }
    assert(!F->isIntrinsic() && "intrinsics are never called");
    // The method header of a definition prints the same signature, so the
    // call token always matches the method.
    const MSIL::Signature &CalleeSig = Printer.getSignature(*F);
    if (!F->isDeclarationForLinker())
      return MCOperand::createExpr(
          MSILMemberRefExpr::createMethod(Name, &CalleeSig, Ctx));
    // An undefined function, or one whose body is not compiled here
    // (available_externally): a P/Invoke declaration of the module trailer.
    // Object mode names it with the signature of the call (plan 5.8.3 R3);
    // whole-program ISel makes that the callee's, whose interned copy is
    // then reused.
    Printer.noteExternFunction(*F);
    const MSIL::Signature &Sig =
        CallSig == CalleeSig ? CalleeSig : Printer.internSignature(CallSig);
    return MCOperand::createExpr(createMethodRef(*F, Name, Sig, Ctx));
  }
  case MachineOperand::MO_ExternalSymbol: {
    // A library call. Its name is a Phase-1 import or diagnosed by LowerCall
    // (decision D14). It has no IR function type, so the call itself spells
    // the signature of the P/Invoke declaration that the module trailer
    // emits for the symbol.
    StringRef Name = MO.getSymbolName();
    if (MO.getOffset() != 0)
      reportFatalInternalError("MSIL: reference to external symbol '" + Name +
                               "' with an offset");
    const MSIL::Signature &Sig =
        Printer.noteExternalSymbol(Name, getCallSiteSignature(MI));
    return MCOperand::createExpr(
        MSILMemberRefExpr::createExternMethod(Name, &Sig, Ctx));
  }
  default:
    reportBadInstr(MI, "unexpected callee operand");
  }
}

MCOperand MSILMCInstLower::lowerFunctionAddress(const MachineInstr &MI,
                                                const MachineOperand &MO,
                                                bool Emit) const {
  if (!Emit)
    return MCOperand::createImm(0);
  switch (MO.getType()) {
  case MachineOperand::MO_GlobalAddress: {
    const GlobalValue *GV = MO.getGlobal();
    StringRef Name = MSIL::getILName(*GV, Printer.TM);
    // The address field of a function holds the address of the method
    // itself (MSILTargetLowering::isOffsetFoldingLegal).
    if (MO.getOffset() != 0)
      reportFatalInternalError("MSIL: reference to function '" + Name +
                               "' with an offset");
    // FUNC_ADDR reads the function's address field, which <Module>::.cctor
    // sets once with 'ldftn' of the method (or of the P/Invoke declaration),
    // so that every pointer to the function has one value (plan 5.8.3 R2).
    if (const auto *F = dyn_cast<Function>(GV)) {
      assert(!F->isIntrinsic() && "intrinsics have no address");
      if (F->isDeclarationForLinker())
        Printer.noteExternFunction(*F);
      Printer.noteFunctionAddress(*F);
    }
    return MCOperand::createExpr(MSILMemberRefExpr::createFnAddr(Name, Ctx));
  }
  case MachineOperand::MO_ExternalSymbol: {
    // SelectionDAG names library functions only as callees, which LowerCall
    // turns into target symbols itself, so the address of one (FUNC_ADDR of
    // a TargetExternalSymbol) has no signature to take it with. Report it,
    // and give FUNC_ADDR an address field operand (no field of that name is
    // declared; the error fails the compilation).
    StringRef Name = MO.getSymbolName();
    const Function &F = MI.getMF()->getFunction();
    F.getContext().diagnose(
        DiagnosticInfoUnsupported(F,
                                  "the address of library function '" + Name +
                                      "' is not supported for MSIL",
                                  MI.getDebugLoc()));
    return MCOperand::createExpr(MSILMemberRefExpr::createFnAddr(Name, Ctx));
  }
  default:
    reportBadInstr(MI, "unexpected function address operand");
  }
}

MCOperand MSILMCInstLower::lowerDataAddress(const MachineInstr &MI,
                                            const MachineOperand &MO,
                                            bool Emit) const {
  if (!Emit)
    return MCOperand::createImm(0);
  if (!MO.isGlobal() || isa<Function>(MO.getGlobal()))
    reportBadInstr(MI, "GLOBAL_ADDR of something other than a variable");
  const GlobalValue *GV = MO.getGlobal();
  StringRef Name = MSIL::getILName(*GV, Printer.TM);
  const auto *GVar = dyn_cast<GlobalVariable>(GV);
  // An alias of a variable: object mode only (MSILPrepareIR replaces aliases
  // in whole-program mode).
  if (!GVar)
    return MCOperand::createExpr(
        MSILMemberRefExpr::createExternField(Name, Ctx));
  // A variable that holds no program data (llvm.used, a variable in the
  // llvm.metadata section) has no field, and one that this module does not
  // define is an error in whole-program mode, reported at the end of the
  // module, where it still gets a field.
  if (MSIL::isSpecialLLVMGlobal(*GVar)) {
    Printer.noteNonDataGlobal(*GVar);
  } else if (GVar->isDeclarationForLinker()) {
    Printer.noteExternGlobal(*GVar);
    return MCOperand::createExpr(
        MSILMemberRefExpr::createExternField(Name, Ctx));
  }
  return MCOperand::createExpr(MSILMemberRefExpr::createField(Name, Ctx));
}

MSIL::Signature MSILMCInstLower::getCallSiteSignature(const MachineInstr &MI) {
  const MachineFunction &MF = *MI.getMF();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  unsigned PointerSizeInBits = MF.getDataLayout().getPointerSizeInBits();
  MSIL::Signature Sig;
  for (const MachineOperand &MO : MI.explicit_operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    const TargetRegisterClass *RC = Reg.isVirtual()
                                        ? MRI.getRegClass(Reg)
                                        : TRI.getMinimalPhysRegClass(Reg);
    MSIL::ValType T =
        MSIL::toValType(*TRI.legalclasstypes_begin(*RC), PointerSizeInBits);
    if (MO.isDef())
      Sig.Results.push_back(T);
    else
      Sig.Params.push_back(T);
  }
  return Sig;
}

MCOperand MSILMCInstLower::lowerCallIndirectSignature(const MachineInstr &MI,
                                                      bool Emit) const {
  // calli pops the arguments and then the address of the callee, which
  // MSILTargetLowering::emitCallResults moved behind the arguments, so the
  // address is the last register use and not a parameter of the signature.
  const MachineOperand &Callee = MSIL::getCalleeOp(MI);
  if (!Callee.isReg() || Callee.isDef())
    reportFatalInternalError("MSIL: indirect call without a callee address");
  if (!Emit)
    return MCOperand::createImm(0);
  MSIL::Signature Sig = getCallSiteSignature(MI);
  [[maybe_unused]] unsigned PointerSizeInBits =
      MI.getMF()->getDataLayout().getPointerSizeInBits();
  assert(!Sig.Params.empty() &&
         Sig.Params.back() ==
             MSIL::toValType(MVT::getIntegerVT(PointerSizeInBits),
                             PointerSizeInBits) &&
         "the callee address is a pointer");
  assert(Sig.Results.size() <= 1 && "a CIL method returns at most one value");
  Sig.Params.pop_back();
  // A managed call: in Phase 1 every function address is the value that
  // <Module>::.cctor stored in the function's address field with ldftn, of
  // a method of this module or of a P/Invoke declaration, whose stub the
  // runtime calls as a managed method (recon/cil-runtime.md e07, e18).
  // Native callers and unmanaged calli wait for phase 3.
  Sig.CC = MSIL::CallConv::Managed;
  // The interned signature lives as long as the printer, as the
  // MSILMemberRefExpr that refers to it needs.
  return MCOperand::createExpr(
      MSILMemberRefExpr::createSig(&Printer.internSignature(Sig), Ctx));
}

/// Appends the 'unaligned. N' and 'volatile.' prefixes of the memflags
/// immediate \p Flags (MSIL::MemFlags; decision D24) to \p Out.
static void lowerMemFlags(int64_t Flags, SmallVectorImpl<MCInst> &Out) {
  assert((uint64_t(Flags) & ~uint64_t(MSIL::MemFlagsUnalignedMask |
                                      MSIL::MemFlagsVolatile)) == 0 &&
         "unknown memflags bits");
  if (unsigned Unaligned = Flags & MSIL::MemFlagsUnalignedMask) {
    assert((Unaligned == 1 || Unaligned == 2 || Unaligned == 4) &&
           "'unaligned.' takes 1, 2 or 4");
    Out.push_back(
        makeInst(cli::Opcode::Unaligned, MCOperand::createImm(Unaligned)));
  }
  if (Flags & MSIL::MemFlagsVolatile)
    Out.push_back(makeInst(cli::Opcode::Volatile));
}

/// Appends '<shortest ldc.i4 of Value>' (or 'ldc.i8 Value') and 'conv.i' to
/// \p Out: \p Value as a native int (decision D2).
static void lowerNativeInt(int64_t Value, SmallVectorImpl<MCInst> &Out) {
  if (isInt<32>(Value))
    Out.push_back(MSIL::buildLdcI4(static_cast<int32_t>(Value)));
  else
    Out.push_back(makeInst(cli::Opcode::Ldc_i8, MCOperand::createImm(Value)));
  Out.push_back(makeInst(cli::Opcode::Conv_i));
}

void MSILMCInstLower::lowerImpl(const MachineInstr &MI,
                                SmallVectorImpl<MCInst> &Out, bool Emit) const {
  // The index or constant immediate of the records below (the operand lists
  // of plan 5.4).
  auto Imm = [&MI](unsigned OpNo) { return MI.getOperand(OpNo).getImm(); };

  // The cases below that expand one record into several CIL instructions (the
  // tees, CONST_I64, GLOBAL_ADDR, BR_TABLE_I32, TRAP, MEMMOVE) must keep the
  // record's StackPeakExtra (MSILInstrFormats.td) in step with the expansion's
  // own peak above max(pops, pushes): MSILStackCheck computes .maxstack from
  // that field, not from the expansion.
  switch (MI.getOpcode()) {
  case MSIL::ARG_GET_I32:
  case MSIL::ARG_GET_I64:
  case MSIL::ARG_GET_F32:
  case MSIL::ARG_GET_F64:
    Out.push_back(MSIL::buildVarInst(cli::Opcode::Ldarg, Imm(1)));
    return;
  case MSIL::ARG_TEE_I32:
  case MSIL::ARG_TEE_I64:
  case MSIL::ARG_TEE_F32:
  case MSIL::ARG_TEE_F64:
    Out.push_back(makeInst(cli::Opcode::Dup));
    Out.push_back(MSIL::buildVarInst(cli::Opcode::Starg, Imm(1)));
    return;
  case MSIL::ARG_SET_I32:
  case MSIL::ARG_SET_I64:
  case MSIL::ARG_SET_F32:
  case MSIL::ARG_SET_F64:
    Out.push_back(MSIL::buildVarInst(cli::Opcode::Starg, Imm(0)));
    return;
  case MSIL::LOCAL_GET_I32:
  case MSIL::LOCAL_GET_I64:
  case MSIL::LOCAL_GET_F32:
  case MSIL::LOCAL_GET_F64:
    Out.push_back(MSIL::buildVarInst(cli::Opcode::Ldloc, Imm(1)));
    return;
  case MSIL::LOCAL_TEE_I32:
  case MSIL::LOCAL_TEE_I64:
  case MSIL::LOCAL_TEE_F32:
  case MSIL::LOCAL_TEE_F64:
    Out.push_back(makeInst(cli::Opcode::Dup));
    Out.push_back(MSIL::buildVarInst(cli::Opcode::Stloc, Imm(1)));
    return;
  case MSIL::LOCAL_SET_I32:
  case MSIL::LOCAL_SET_I64:
  case MSIL::LOCAL_SET_F32:
  case MSIL::LOCAL_SET_F64:
    Out.push_back(MSIL::buildVarInst(cli::Opcode::Stloc, Imm(0)));
    return;

  case MSIL::CONST_I32: {
    // MachineOperands hold the constant sign-extended; MIR may spell it
    // zero-extended.
    int64_t Value = Imm(1);
    assert((isInt<32>(Value) || isUInt<32>(Value)) && "CONST_I32 out of range");
    Out.push_back(MSIL::buildLdcI4(static_cast<int32_t>(Value)));
    return;
  }
  case MSIL::CONST_I64:
    // native int (decision D2): conv.i sign-extends the int32 of ldc.i4.
    lowerNativeInt(Imm(1), Out);
    return;
  case MSIL::CONST_F32:
  case MSIL::CONST_F64: {
    // Bit patterns, so NaN payloads and -0.0 are exact.
    const ConstantFP *FP = MI.getOperand(1).getFPImm();
    uint64_t Bits = FP->getValueAPF().bitcastToAPInt().getZExtValue();
    if (MI.getOpcode() == MSIL::CONST_F32)
      Out.push_back(
          makeInst(cli::Opcode::Ldc_r4,
                   MCOperand::createSFPImm(static_cast<uint32_t>(Bits))));
    else
      Out.push_back(
          makeInst(cli::Opcode::Ldc_r8, MCOperand::createDFPImm(Bits)));
    return;
  }

  case MSIL::GLOBAL_ADDR: {
    // The global's address field (decision D12), which the module
    // initializer sets, plus the offset that ISel folded into the address.
    const MachineOperand &MO = MI.getOperand(1);
    Out.push_back(
        makeInst(cli::Opcode::Ldsfld, lowerDataAddress(MI, MO, Emit)));
    if (int64_t Offset = MO.getOffset()) {
      if (isInt<32>(Offset)) {
        Out.push_back(MSIL::buildLdcI4(static_cast<int32_t>(Offset)));
      } else {
        Out.push_back(
            makeInst(cli::Opcode::Ldc_i8, MCOperand::createImm(Offset)));
        Out.push_back(makeInst(cli::Opcode::Conv_i));
      }
      Out.push_back(makeInst(cli::Opcode::Add));
    }
    return;
  }
  case MSIL::FUNC_ADDR:
    Out.push_back(makeInst(cli::Opcode::Ldsfld,
                           lowerFunctionAddress(MI, MI.getOperand(1), Emit)));
    return;

  case MSIL::CALL:
    Out.push_back(makeInst(cli::Opcode::Call,
                           lowerCallee(MI, MSIL::getCalleeOp(MI), Emit)));
    return;
  case MSIL::CALL_INDIRECT:
    Out.push_back(
        makeInst(cli::Opcode::Calli, lowerCallIndirectSignature(MI, Emit)));
    return;

  case MSIL::BR_TABLE_I32: {
    // 'switch (L0, L1, ...)' then 'br L0': operand 0 is the int32 index (a
    // value on the stack, decision D35), then the jump-table blocks that
    // MSILTargetLowering::LowerBR_JT copied into the instruction, then the
    // first of them again. 'switch' falls through when the index is out of
    // range (taken as unsigned), so that last operand is the target of the
    // 'br' after it: a dummy fall-through target that is already a CFG
    // successor of the block, not the C switch's default. It never executes:
    // LLVM's range check before the jump table (never removed, D35) sends
    // every out-of-range value to the real default first, and when the
    // default is unreachable an out-of-range index is undefined behaviour.
    unsigned E = MI.getNumExplicitOperands();
    if (E < 2 || !MI.getOperand(0).isReg())
      reportBadInstr(MI, "switch without an index or a fall-through target");
    auto Label = [&](unsigned I) {
      const MachineOperand &MO = MI.getOperand(I);
      if (!MO.isMBB())
        reportBadInstr(MI,
                       "switch target " + Twine(I) + " is not a basic block");
      return MCOperand::createExpr(
          MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
    };
    MCInst Switch = makeInst(cli::Opcode::Switch);
    for (unsigned I = 1; I + 1 != E; ++I)
      Switch.addOperand(Label(I));
    Out.push_back(Switch);
    Out.push_back(makeInst(cli::Opcode::Br, Label(E - 1)));
    return;
  }
  case MSIL::TRAP:
    Out.push_back(makeInst(cli::Opcode::Ldnull));
    Out.push_back(makeInst(cli::Opcode::Throw));
    return;
  case MSIL::MEMMOVE:
    // 'conv.i8; dup' turns the native int length into both int64 arguments.
    Out.push_back(makeInst(cli::Opcode::Conv_i8));
    Out.push_back(makeInst(cli::Opcode::Dup));
    Out.push_back(
        makeInst(cli::Opcode::Call,
                 Emit ? MCOperand::createExpr(MSILMemberRefExpr::createBCL(
                            cli::BCLMemberId::Buffer_MemoryCopy, Ctx))
                      : MCOperand::createImm(0)));
    return;
  case MSIL::MEMORY_BARRIER:
    Out.push_back(
        makeInst(cli::Opcode::Call,
                 Emit ? MCOperand::createExpr(MSILMemberRefExpr::createBCL(
                            cli::BCLMemberId::Interlocked_MemoryBarrier, Ctx))
                      : MCOperand::createImm(0)));
    return;
  default:
    break;
  }

  // Every other record is one CIL instruction whose operands are values on
  // the evaluation stack (registers), at most one basic-block target, and
  // the memflags immediate of a memory access.
  const MCInstrDesc &Desc = MI.getDesc();
  std::optional<cli::Opcode> Op = getCILOpcode(Desc);
  if (!Op)
    reportBadInstr(MI, "no CIL lowering");
  const cli::OpcodeInfo &Info = cli::getOpcodeInfo(*Op);
  MCInst Inst = makeInst(*Op);
  for (unsigned I = 0, E = MI.getNumExplicitOperands(); I != E; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    // A value that the instruction pops or pushes.
    if (MO.isReg())
      continue;
    if (I < Desc.getNumOperands() &&
        Desc.operands()[I].OperandType == MSIL::OPERAND_MEMFLAGS) {
      lowerMemFlags(MO.getImm(), Out);
      continue;
    }
    if (MO.isMBB() && isBranchTarget(Info.Operand) &&
        Inst.getNumOperands() == 0) {
      // A block label, '@BB<f>_<b>'.
      Inst.addOperand(MCOperand::createExpr(
          MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx)));
      continue;
    }
    reportBadInstr(MI, "unexpected operand " + Twine(I));
  }
  if (Inst.getNumOperands() != (isBranchTarget(Info.Operand) ? 1u : 0u) ||
      (!isBranchTarget(Info.Operand) &&
       Info.Operand != cli::OperandKind::InlineNone))
    reportBadInstr(MI, Twine("the operands do not match '") + Info.Name + "'");
  Out.push_back(Inst);
}

bool MSILMCInstLower::isLowered(const MachineInstr &MI) {
  // AsmPrinter::emitFunctionBody handles the target-independent instructions
  // it knows itself, and none of them emits code; any other one has no CIL
  // lowering. The ARGUMENT_* pseudos only give the incoming arguments virtual
  // registers; MSILExplicitLocals has turned their uses into ldarg.
  unsigned Opc = MI.getOpcode();
  return isTargetSpecificOpcode(Opc) && !MSIL::isArgument(Opc);
}

void MSILMCInstLower::lower(const MachineInstr &MI,
                            SmallVectorImpl<MCInst> &Out) const {
  // Only the instructions that computeBranchForms laid out emit code, so the
  // branch forms it chose fit the emitted code.
  if (!isLowered(MI)) {
    if (MSIL::isArgument(MI.getOpcode()))
      return;
    // FENTRY_CALL, PATCHABLE_*, STACKMAP and the like.
    reportBadInstr(MI, "no CIL lowering");
  }
  unsigned Begin = Out.size();
  lowerImpl(MI, Out, /*Emit=*/true);
  if (ShortBranches.empty())
    return;
  // The branches that computeBranchForms made short.
  for (unsigned I = Begin, E = Out.size(); I != E; ++I) {
    if (!ShortBranches.contains({&MI, I - Begin}))
      continue;
    const MCInstrInfo &MCII = *MI.getMF()->getSubtarget().getInstrInfo();
    std::optional<cli::Opcode> Short =
        cli::getShortBranchForm(getCILOpcode(Out[I], MCII));
    assert(Short && "only a branch takes a short form");
    Out[I].setOpcode(MSIL::getMCOpcodeForCIL(*Short));
  }
}

void MSILMCInstLower::computeBranchForms(const MachineFunction &MF) {
  ShortBranches.clear();
  const MCInstrInfo &MCII = *MF.getSubtarget().getInstrInfo();
  // The function as MSILAsmPrinter::emitInstruction will emit it, with the
  // MachineInstr and the position among its MCInsts of every MCInst, and the
  // index of every block's label.
  SmallVector<MCInst, 64> Insts;
  SmallVector<std::pair<const MachineInstr *, unsigned>, 64> Origins;
  SmallVector<std::pair<const MCSymbol *, unsigned>, 16> Labels;
  for (const MachineBasicBlock &MBB : MF) {
    Labels.push_back({MBB.getSymbol(), Insts.size()});
    for (const MachineInstr &MI : MBB) {
      // Exactly the instructions that lower() emits code for.
      if (!isLowered(MI))
        continue;
      unsigned Begin = Insts.size();
      lowerImpl(MI, Insts, /*Emit=*/false);
      for (unsigned I = Begin, E = Insts.size(); I != E; ++I)
        Origins.push_back({&MI, I - Begin});
    }
  }
  MSIL::relaxBranches(Insts, Labels, MCII);
  for (unsigned I = 0, E = Insts.size(); I != E; ++I) {
    cli::Opcode Op = getCILOpcode(Insts[I], MCII);
    if (cli::getOpcodeInfo(Op).Operand == cli::OperandKind::ShortInlineBrTarget)
      ShortBranches.insert(Origins[I]);
  }
}
