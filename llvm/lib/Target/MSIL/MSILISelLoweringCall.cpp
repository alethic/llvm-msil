//===-- MSILISelLoweringCall.cpp - MSIL call lowering ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyISelLowering.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the parts of MSILTargetLowering that lower calls,
/// formal arguments and returns.
///
/// Every formal argument becomes an MSILISD::ARGUMENT node (an ARGUMENT_<RC>
/// pseudo that MSILExplicitLocals turns into ldarg), and a return becomes an
/// MSILISD::RETURN node with at most one value (ret). The parameter and result
/// types are recorded in MSILFunctionInfo, from which the AsmPrinter prints
/// the method header.
///
/// A call becomes an MSILISD::CALL node (chain, callee, arguments...) whose
/// results are the call's result (at most one) and the chain. Instruction
/// selection splits it into CALL_PARAMS and CALL_RESULTS, and the custom
/// inserter emitCallResults joins them into one CALL (a direct 'call' of a
/// method) or CALL_INDIRECT ('calli' through an address).
///
/// A library call, which SelectionDAG makes for an operation that it cannot
/// expand inline, names its callee by an external symbol. Phase 1 has no
/// linker and no runtime library, so LowerCall applies the library-call
/// policy of decision D14 to it: a call of a function of this module, a
/// P/Invoke of a Phase-1 import, or an error.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSILISelLowering.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILSelectionDAGInfo.h"
#include "MSILSubtarget.h"
#include "MSILUtilities.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
using namespace llvm;

#define DEBUG_TYPE "msil-lower"

/// Reports \p Msg as an error in the current function. Compilation goes on, so
/// the caller must still build a well-formed DAG.
static void fail(const SDLoc &DL, SelectionDAG &DAG, const Twine &Msg) {
  MachineFunction &MF = DAG.getMachineFunction();
  DAG.getContext()->diagnose(
      DiagnosticInfoUnsupported(MF.getFunction(), Msg, DL.getDebugLoc()));
}

/// Returns true if a call with the lowered arguments and results of \p CLI
/// matches the signature of \p F's method, which comes from F's own type
/// (MSILAsmPrinter::getSignature), so that a direct call of it is valid.
static bool matchesSignature(const Function &F,
                             const TargetLowering::CallLoweringInfo &CLI) {
  // A variadic method gets no parameter for the variable arguments.
  if (F.isVarArg())
    return false;
  SmallVector<MVT, 4> Params;
  SmallVector<MVT, 4> Results;
  MSIL::computeSignatureVTs(F.getFunctionType(), &F,
                            CLI.DAG.getMachineFunction().getFunction(),
                            CLI.DAG.getTarget(), Params, Results);
  return equal(Params,
               map_range(CLI.Outs,
                         [](const ISD::OutputArg &Out) { return Out.VT; })) &&
         equal(Results, map_range(CLI.Ins, [](const ISD::InputArg &In) {
                 return In.VT;
               }));
}

// Test whether the given calling convention is supported. Every call is a
// managed call with the arguments on the evaluation stack and there are no
// call-clobbered registers, so the language-independent conventions are all
// handled the same.
static bool callingConvSupported(CallingConv::ID CallConv) {
  return CallConv == CallingConv::C || CallConv == CallingConv::Fast ||
         CallConv == CallingConv::Cold ||
         CallConv == CallingConv::PreserveMost ||
         CallConv == CallingConv::PreserveAll;
}

SDValue MSILTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  // LowerReturn does not repeat this check: every definition passes through
  // here, so the diagnostic appears once per function.
  if (!callingConvSupported(CallConv))
    fail(DL, DAG, "calling convention is not supported for MSIL");
  // MSILPrepareIR has already diagnosed every variadic definition and deleted
  // its body; this is a backstop. The signature gets no extra parameter for
  // the variable arguments (computeSignatureVTs adds none either).
  if (IsVarArg)
    fail(DL, DAG, "variadic functions are not supported for MSIL yet");

  MachineFunction &MF = DAG.getMachineFunction();
  auto *MFI = MF.getInfo<MSILFunctionInfo>();

  // Set up the incoming ARGUMENTS value, which serves to represent the liveness
  // of the incoming values before they're represented by virtual registers.
  MF.getRegInfo().addLiveIn(MSIL::ARGUMENTS);

  for (const ISD::InputArg &In : Ins) {
    // Each of these arguments is a pointer. After the diagnostic it is lowered
    // as the plain pointer parameter it is, so the output stays well-formed.
    // SelectionDAG also sets the byval flag for inalloca and preallocated
    // arguments, so test those first.
    if (In.Flags.isInAlloca())
      fail(DL, DAG, "inalloca arguments are not supported for MSIL");
    else if (In.Flags.isPreallocated())
      fail(DL, DAG, "preallocated arguments are not supported for MSIL");
    else if (In.Flags.isByVal())
      fail(DL, DAG, "byval arguments are not supported for MSIL yet");
    if (In.Flags.isNest())
      fail(DL, DAG, "nest arguments are not supported for MSIL");
    // An sret argument is an ordinary pointer parameter. Narrow integers
    // arrive as int32 (In.VT is the register type); SelectionDAGBuilder adds
    // AssertSext/AssertZext for the signext/zeroext attributes.
    InVals.push_back(In.Used ? DAG.getNode(MSILISD::ARGUMENT, DL, In.VT,
                                           DAG.getTargetConstant(InVals.size(),
                                                                 DL, MVT::i32))
                             : DAG.getUNDEF(In.VT));

    // Record the number and types of arguments.
    MFI->addParam(In.VT);
  }

  // Record the number and types of results. The method header, direct calls
  // and ldftn compute their signatures from the IR type with
  // computeSignatureVTs, so the parameters recorded above must agree with it.
  SmallVector<MVT, 4> Params;
  SmallVector<MVT, 4> Results;
  MSIL::computeSignatureVTs(MF.getFunction().getFunctionType(),
                            &MF.getFunction(), MF.getFunction(),
                            DAG.getTarget(), Params, Results);
  for (MVT VT : Results)
    MFI->addResult(VT);
  assert(MFI->getParams().size() == Params.size() &&
         std::equal(MFI->getParams().begin(), MFI->getParams().end(),
                    Params.begin()) &&
         "formal arguments disagree with computeSignatureVTs");

  return Chain;
}

SDValue MSILTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc DL = CLI.DL;
  SDValue Callee = CLI.Callee;
  MachineFunction &MF = DAG.getMachineFunction();

  // There is no 'tail.' prefix in Phase 1, so every call is an ordinary call
  // (a musttail call is diagnosed below).
  CLI.IsTailCall = false;

  // A call that cannot be lowered builds no call node: instruction selection,
  // the custom inserter and MCInstLower never meet it, and its results are
  // undefined. After a diagnostic, compilation goes on with this well-formed
  // DAG and llc exits with an error.
  auto DropCall = [&]() -> SDValue {
    for (const ISD::InputArg &In : CLI.Ins)
      InVals.push_back(DAG.getUNDEF(In.VT));
    return CLI.Chain;
  };
  auto Reject = [&](const Twine &Msg) -> SDValue {
    fail(DL, DAG, Msg);
    return DropCall();
  };

  // The callee of a direct call: a function of the module, or the external
  // symbol of a library call that becomes a P/Invoke declaration.
  const Function *F = nullptr;
  const char *LibCallee = nullptr;
  int64_t CalleeOffset = 0;

  // SelectionDAG calls a library function, named by an external symbol, for
  // an operation that it cannot expand inline (sqrt, fmin, an i128 to
  // floating-point conversion, ...). Phase 1 has no linker and no runtime
  // library, so the library-call policy of decision D14 applies.
  if (const auto *ES = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    StringRef Name = ES->getSymbol();
    auto Mismatch = [&]() {
      return Reject(Twine("library call '") + Name +
                    "' does not match the type of the function of that name "
                    "in this module");
    };
    // Methods are named by their IL name, so a function @"\01sqrt" is the
    // one a call to 'sqrt' binds to.
    const Function *Named =
        MSIL::getFunctionByILName(*MF.getFunction().getParent(), Name);
    if (Named && Named->isIntrinsic())
      Named = nullptr;
    if (Named && !Named->isDeclarationForLinker()) {
      // A function of that name defined in this module is the one a linker
      // would bind the call to (a freestanding program's own sqrt, or the
      // function of -ftrap-function), and a P/Invoke of that name would clash
      // with its method: call the method.
      if (!matchesSignature(*Named, CLI))
        return Mismatch();
      F = Named;
    } else {
      // Otherwise it must be a Phase-1 import: puts, putchar, the C math
      // library, or a declaration with "msil-import-module". The compiler-rt
      // helpers (__divti3, __mulsc3, __extendhfsf2, __atomic_*, ...) wait for
      // the phase-3 runtime library.
      const Triple &TT = DAG.getTarget().getTargetTriple();
      if (!MSIL::getPInvokeModule(TT, Name, Named)) {
        if (Name.starts_with("__"))
          return Reject(Twine("runtime helper '") + Name +
                        "' is not available for MSIL yet (phase 3)");
        return Reject(Twine("library call '") + Name +
                      "' is not a Phase-1 import");
      }
      // An import that this module also declares is called through that
      // declaration, so that one P/Invoke declaration, with the
      // declaration's "msil-import-*" attributes, serves the library call and
      // the module's own calls. Any other import becomes a P/Invoke
      // declaration of the external symbol
      // (MSILAsmPrinter::noteExternalSymbol), whose library comes from the
      // import table alone.
      if (Named && matchesSignature(*Named, CLI))
        F = Named;
      else if (MSIL::getPInvokeModule(TT, Name))
        LibCallee = ES->getSymbol();
      else
        return Mismatch();
    }
  }

  // Calling an undefined value is undefined behavior. MSILPrepareIR leaves
  // such calls behind when it replaces a diagnosed alias by poison.
  if (Callee.isUndef())
    return DropCall();

  if (!callingConvSupported(CLI.CallConv))
    return Reject("calling convention is not supported for MSIL");
  if (CLI.IsPatchPoint)
    return Reject("patchpoints are not supported for MSIL");
  if (CLI.CB && CLI.CB->isMustTailCall())
    return Reject("musttail calls are not supported for MSIL yet");

  // A direct call names the callee's method, whose signature comes from the
  // callee's own type (MSILAsmPrinter::getSignature), while the arguments and
  // results here come from the call site's type. If the two differ, the call
  // token would not match the method and the JIT would reject the call.
  // MSILFixFunctionBitcasts (M9) makes them agree before ISel; this is the
  // backstop.
  if (const auto *GA = dyn_cast<GlobalAddressSDNode>(Callee)) {
    F = dyn_cast<Function>(GA->getGlobal());
    CalleeOffset = GA->getOffset();
  }

  // A narrow (i1, i8, i16) result is returned as an int32, and the zeroext
  // and signext attributes of the result would let SelectionDAG trust its
  // upper bits (TargetLowering::LowerCallTo adds AssertZext/AssertSext after
  // this returns). That holds only for a function defined in this module,
  // whose own return lowering extends the value. Any other callee is native
  // code: an undefined function or a library call becomes a P/Invoke, and an
  // indirect callee may be one too. The native ABIs (MS x64, SysV x86-64,
  // AAPCS64) leave the bits above a narrow result unspecified (clang -O2
  // compiles 'unsigned char f(int x) { return x; }' for x86-64 to
  // 'movl %ecx, %eax; retq'), and the marshaller passes the whole register
  // through, so the result must be extended again after the call. The int32
  // signature stays: a narrow argument is already correct, as the managed
  // caller passes it fully extended.
  if (!F || F->isDeclarationForLinker()) {
    CLI.RetZExt = false;
    CLI.RetSExt = false;
  }
  // SelectionDAGBuilder lowers every recognized intrinsic itself, so only an
  // 'llvm.*' name that LLVM does not know reaches this point. It has no CIL
  // method; the asserts in MSILMCInstLower and MSILAsmPrinter stay internal
  // invariants.
  if (F && F->isIntrinsic())
    return Reject(Twine("unknown intrinsic '") + F->getName() +
                  "' is not supported for MSIL");
  if (F && CLI.CB && CLI.CB->getFunctionType() != F->getFunctionType())
    return Reject("mismatched call signature");

  if (CLI.IsVarArg)
    return Reject("calls to variadic functions are not supported for MSIL yet");

  for (const ISD::OutputArg &Out : CLI.Outs) {
    // SelectionDAG also sets the byval flag for inalloca and preallocated
    // arguments, so test those first.
    if (Out.Flags.isInAlloca())
      return Reject("inalloca arguments are not supported for MSIL");
    if (Out.Flags.isPreallocated())
      return Reject("preallocated arguments are not supported for MSIL");
    if (Out.Flags.isByVal())
      return Reject("byval arguments are not supported for MSIL yet");
    if (Out.Flags.isNest())
      return Reject("nest arguments are not supported for MSIL");
    // An sret argument (including the hidden one of a result that
    // CanLowerReturn demotes) is an ordinary pointer argument. Narrow
    // integers arrive as int32 (Out.VT is the register type), already
    // extended as the signext/zeroext attributes say.
  }

  // A direct callee needs no address: CALL_PARAMS takes the function or
  // library symbol itself, and MSILMCInstLower turns it into the method
  // reference. Any other callee is an address computed into a register,
  // called with 'calli'.
  MVT PtrVT = getPointerTy(MF.getDataLayout());
  if (F)
    Callee = DAG.getTargetGlobalAddress(F, DL, PtrVT, CalleeOffset);
  else if (LibCallee)
    Callee = DAG.getTargetExternalSymbol(LibCallee, PtrVT);

  SmallVector<SDValue, 16> Ops;
  Ops.push_back(CLI.Chain);
  Ops.push_back(Callee);
  Ops.append(CLI.OutVals.begin(), CLI.OutVals.end());

  // CanLowerReturn demotes anything that needs more than one value.
  assert(CLI.Ins.size() <= 1 && "a CIL method returns at most one value");
  SmallVector<EVT, 2> InTys;
  for (const ISD::InputArg &In : CLI.Ins) {
    assert(!In.Flags.isByVal() && "byval is not valid for return values");
    assert(!In.Flags.isNest() && "nest is not valid for return values");
    InTys.push_back(In.VT);
  }
  InTys.push_back(MVT::Other);
  SDValue Res = DAG.getNode(MSILISD::CALL, DL, DAG.getVTList(InTys), Ops);

  for (unsigned I = 0, E = CLI.Ins.size(); I != E; ++I)
    InVals.push_back(Res.getValue(I));

  // Return the chain.
  return Res.getValue(CLI.Ins.size());
}

bool MSILTargetLowering::CanLowerReturn(
    CallingConv::ID /*CallConv*/, MachineFunction & /*MF*/, bool /*IsVarArg*/,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext & /*Context*/,
    const Type * /*RetTy*/) const {
  // A CIL method returns at most one value. Anything larger is demoted to a
  // hidden sret pointer parameter (computeSignatureVTs does the same).
  return Outs.size() <= 1;
}

SDValue
MSILTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID /*CallConv*/,
                                bool /*IsVarArg*/,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                const SDLoc &DL, SelectionDAG &DAG) const {
  assert(Outs.size() <= 1 && "MSIL can only return up to one value");

  // An i1, i8 or i16 result arrives as an int32 that SelectionDAGBuilder has
  // already extended as the signext/zeroext return attribute says (any
  // extension without one), and the method header says int32 too.
  SmallVector<SDValue, 4> RetOps(1, Chain);
  RetOps.append(OutVals.begin(), OutVals.end());
  Chain = DAG.getNode(MSILISD::RETURN, DL, MVT::Other, RetOps);

  // The result types were recorded by LowerFormalArguments.
#ifndef NDEBUG
  for (const ISD::OutputArg &Out : Outs) {
    assert(!Out.Flags.isByVal() && "byval is not valid for return values");
    assert(!Out.Flags.isNest() && "nest is not valid for return values");
    assert(!Out.Flags.isVarArg() && "non-fixed return value is not valid");
    assert(!Out.Flags.isInAlloca() && !Out.Flags.isInConsecutiveRegs() &&
           !Out.Flags.isInConsecutiveRegsLast() &&
           "MSIL never requests in-memory or consecutive-register results");
  }
#endif

  return Chain;
}

MachineBasicBlock *
MSILTargetLowering::emitCallResults(MachineInstr &CallResults,
                                    MachineBasicBlock *BB) const {
  // Instruction selection glues CALL_PARAMS to CALL_RESULTS, so the two are
  // adjacent.
  MachineInstr &CallParams = *CallResults.getPrevNode();
  assert(CallParams.getOpcode() == MSIL::CALL_PARAMS);
  assert(CallResults.getOpcode() == MSIL::CALL_RESULTS);

  // A direct call has the function symbol as its callee; an indirect call has
  // the callee's address in a register (or a frame index).
  bool IsIndirect =
      CallParams.getOperand(0).isReg() || CallParams.getOperand(0).isFI();
  unsigned CallOp = IsIndirect ? MSIL::CALL_INDIRECT : MSIL::CALL;

  MachineFunction &MF = *BB->getParent();
  const TargetInstrInfo &TII = *Subtarget->getInstrInfo();
  MachineInstrBuilder MIB(
      MF, MF.CreateMachineInstr(TII.get(CallOp), CallResults.getDebugLoc()));

  // Move the function pointer to the end of the arguments for indirect calls:
  // 'calli' pops the address after the arguments.
  if (IsIndirect) {
    MachineOperand FnPtr = CallParams.getOperand(0);
    CallParams.removeOperand(0);
    CallParams.addOperand(FnPtr);
  }

  // The results come first (variadicOpsAreDefs). For a direct call the callee
  // follows them, so its operand index is the number of results.
  for (const MachineOperand &Def : CallResults.defs())
    MIB.add(Def);

  // Avoid duplicating the implicit operands.
  for (const MachineOperand &Use : CallParams.explicit_uses())
    MIB.add(Use);

  BB->insert(CallResults.getIterator(), MIB);
  // With -emit-call-site-info (clang sets it for -g at -O1 and above), the
  // scheduler records call-site info for each of the two pseudos, as both are
  // calls. CALL_RESULTS replaced the MSILISD::CALL node and carries that
  // node's info, so it moves to the new call; CALL_PARAMS' entry is dropped.
  // WebAssembly's LowerCallResults omits this, but clang never asks for
  // call-site info there: WebAssembly has no debug entry values.
  MF.eraseAdditionalCallInfo(&CallParams);
  MF.moveAdditionalCallInfo(&CallResults, MIB.getInstr());
  CallParams.eraseFromParent();
  CallResults.eraseFromParent();

  return BB;
}
