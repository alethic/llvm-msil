//===-- MSILLowerSjLj.cpp - Lower setjmp and longjmp for MSIL -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyLowerEmscriptenEHSjLj.cpp
// at 1838cefa99c0.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Lowers the C library's setjmp and longjmp (ImplementationPlan.md 5.15.4;
/// slot P8 of the IR pipeline, 5.13.6), after the setjmp half of the
/// Emscripten scheme: every call that may longjmp after a setjmp goes through
/// a wrapper, and the caller tests a flag after the wrapper returns.
///
/// CIL cannot unwind to an address, but a method can catch an exception that
/// leaves its callee. So:
/// - Every function that calls setjmp gets a status block
///   '%sjlj.st = alloca {i32 label, i32 val}', whose address identifies the
///   function invocation. Each setjmp site (label 1..N within the function)
///   writes a 16-byte prefix into its jmp_buf: the magic 'MSJ1' at offset 0,
///   the site label at offset 4 and the status block's address at offset 8.
///   Nothing is keyed by the buffer's address, so a copied buffer works for
///   the jump; a sigsetjmp site restores the mask through its own buffer.
/// - Each call reachable from a site whose callee may longjmp becomes a call
///   of an internal wrapper, __msil_sjlj_invoke.<n> for a direct callee and
///   __msil_sjlj_invoke_ind.<n> for an indirect one (the callee pointer
///   first), with the status block as its last argument. The wrapper has the
///   attribute "msil-sjlj-invoke"="<index of the status argument>", from
///   which the AsmPrinter puts its whole body in an IL .try with a fixed
///   filter (MSILEHClauses.h). After the call the caller loads the label and
///   branches to its dispatch block when it is not zero.
/// - longjmp, and every other use of a longjmp declaration (a taken address),
///   becomes the per-module helper __msil_longjmp(buf, val), which checks the
///   magic, arms the target status block with val (1 for 0) and then the
///   label, and calls __msil_sjlj_throw(status). The AsmPrinter writes that
///   helper's body: it throws ExternalException(null, (int)status). The
///   wrapper's filter catches the exception only when its HResult is the low
///   32 bits of its own status block's address and that block is armed, and
///   its handler leaves the wrapper normally.
/// - The dispatch block (one per function) loads val, disarms the block
///   (label 0) and switches on the label to the restore block of the site,
///   which restores the stack pointer saved at the site (llvm.stacksave and
///   llvm.stackrestore, plan 5.15.8), restores a saved signal mask, and
///   branches back to the site's continuation, where the setjmp result is
///   'phi [0, site], [val, restore]'.
///
/// Every control-flow edge is explicit IR, so no value needs demotion or
/// volatile; SSA is rebuilt with SSAUpdaterBulk as the Emscripten pass does.
/// A function whose sites reach no call that may longjmp still stores each
/// site's prefix and gets 0 for each result. A module without a reference to
/// the setjmp family is untouched. Calling another returns_twice function
/// (vfork, getcontext) and taking the address of a setjmp function are
/// diagnosed; the function that holds such a use loses its body (it becomes
/// a lone 'unreachable'), and a use in a global initializer becomes null
/// (plan section 2, 'Diagnostics leave well-formed output').
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "MSILUtilities.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SSAUpdaterBulk.h"
#include <string>
#include <tuple>

using namespace llvm;

#define DEBUG_TYPE "msil-lower-sjlj"

static cl::opt<bool> DisableSjLjLowering(
    "msil-disable-sjlj-lowering", cl::Hidden, cl::init(false),
    cl::desc("Leave setjmp and longjmp calls unlowered (for testing msil-ld's "
             "guard against natively bound setjmp functions)"));

namespace {
class MSILLowerSjLjLegacy final : public ModulePass {
  StringRef getPassName() const override {
    return "MSIL Lower setjmp and longjmp";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetLibraryInfoWrapperPass>();
  }

  bool runOnModule(Module &M) override;

public:
  static char ID;
  MSILLowerSjLjLegacy() : ModulePass(ID) {}
};
} // end anonymous namespace

char MSILLowerSjLjLegacy::ID = 0;
INITIALIZE_PASS_BEGIN(MSILLowerSjLjLegacy, DEBUG_TYPE,
                      "MSIL Lower setjmp and longjmp", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(MSILLowerSjLjLegacy, DEBUG_TYPE,
                    "MSIL Lower setjmp and longjmp", false, false)

ModulePass *llvm::createMSILLowerSjLjLegacyPass() {
  return new MSILLowerSjLjLegacy();
}

//===----------------------------------------------------------------------===//
// Names and constants (plan 5.15.4)
//===----------------------------------------------------------------------===//

/// The magic at offset 0 of a jmp_buf that a setjmp site filled: 'MSJ1'.
static constexpr uint32_t SjLjMagic = 0x314A534D;
/// Offsets in the jmp_buf: the site label, the status block's address, the
/// mask-saved flag of sigsetjmp (glibc's and musl's offset) and the saved
/// signal mask.
static constexpr uint64_t LabelOffset = 4;
static constexpr uint64_t StatusOffset = 8;
static constexpr uint64_t MaskSavedOffset = 64;
static constexpr uint64_t MaskOffset = 72;
/// sigprocmask's 'how' values (Linux numbering): SIG_BLOCK and SIG_SETMASK.
static constexpr uint32_t SigBlock = 0;
static constexpr uint32_t SigSetMask = 2;

static constexpr const char *LongjmpHelperName = "__msil_longjmp";
static constexpr const char *ThrowHelperName = "__msil_sjlj_throw";
static constexpr const char *WrapperPrefix = "__msil_sjlj_invoke.";
static constexpr const char *IndirectWrapperPrefix = "__msil_sjlj_invoke_ind.";
static constexpr const char *InvokeAttr = "msil-sjlj-invoke";
static constexpr const char *ThrowAttr = "msil-sjlj-throw";

/// Whether \p Name is a sigsetjmp spelling, whose second argument says
/// whether to save the signal mask.
static bool isSigsetjmpName(StringRef Name) {
  return Name == "__sigsetjmp" || Name == "sigsetjmp";
}

/// The C library functions that TargetLibraryInfo recognizes and that may
/// still longjmp, because they call a function of the program (sj 3.5's
/// callback list; a longjmp out of such a callback works through native
/// frames on Windows triples). abort is on the list too: it raises SIGABRT,
/// whose handler may longjmp (C17 7.22.4.1). Every other function that
/// TargetLibraryInfo recognizes returns to its caller.
static bool isCallbackLibraryName(StringRef Name) {
  return StringSwitch<bool>(Name)
      .Cases({"qsort", "bsearch", "lfind", "lsearch", "tsearch", "twalk",
              "tdelete", "ftw", "nftw", "scandir", "glob"},
             true)
      .Cases({"signal", "sigaction", "raise", "atexit", "at_quick_exit", "exit",
              "pthread_once", "pthread_create", "bsearch_s", "qsort_s",
              "abort"},
             true)
      .Default(false);
}

//===----------------------------------------------------------------------===//
// Diagnostics
//===----------------------------------------------------------------------===//

/// Replaces the body of \p F by a lone 'unreachable', keeping the function
/// defined with its linkage, comdat and attributes (as MSILLowerDynamicAlloca
/// does for a diagnosed function).
static void dropBody(Function &F) {
  for (BasicBlock &BB : F)
    BB.dropAllReferences();
  while (!F.empty())
    F.back().eraseFromParent();
  LLVMContext &Ctx = F.getContext();
  new UnreachableInst(Ctx, BasicBlock::Create(Ctx, "entry", &F));
}

/// Collects the uses of \p F that are not the callee of a call, looking
/// through constant expressions and aggregates: instructions go to \p Insts
/// (with the value they use), and global variables, aliases and ifuncs whose
/// initializer or target holds the use go to \p Globals.
static void collectNonCallUsers(Value &V, Function &F,
                                SmallSetVector<Instruction *, 4> &Insts,
                                SmallSetVector<GlobalValue *, 4> &Globals) {
  for (Use &U : V.uses()) {
    User *Usr = U.getUser();
    if (auto *CB = dyn_cast<CallBase>(Usr); CB && &V == &F && CB->isCallee(&U))
      continue;
    if (auto *I = dyn_cast<Instruction>(Usr))
      Insts.insert(I);
    else if (auto *GV = dyn_cast<GlobalValue>(Usr))
      Globals.insert(GV);
    else if (auto *C = dyn_cast<Constant>(Usr))
      collectNonCallUsers(*C, F, Insts, Globals);
  }
}

/// Reports every use of the setjmp function \p F that is not a direct call
/// (a taken address) and every call of it that is not a plain call: the
/// function that holds such a use loses its body, and a use in a global
/// initializer becomes null. Adds the functions whose bodies were dropped to
/// \p Dropped.
static bool diagnoseSetjmpAddress(Function &F,
                                  SmallPtrSetImpl<Function *> &Dropped) {
  SmallSetVector<Instruction *, 4> Insts;
  SmallSetVector<GlobalValue *, 4> Globals;
  collectNonCallUsers(F, F, Insts, Globals);
  // A call of another kind (callbr) cannot be a site either.
  for (User *U : F.users())
    if (auto *CB = dyn_cast<CallBase>(U); CB && !isa<CallInst>(CB))
      Insts.insert(CB);
  if (Insts.empty() && Globals.empty())
    return false;

  std::string Text =
      ("taking the address of '" + F.getName() + "' is not supported for MSIL")
          .str();
  LLVMContext &Ctx = F.getContext();
  SmallVector<Function *, 4> Holders;
  for (Instruction *I : Insts) {
    Function *Holder = I->getFunction();
    if (!Dropped.insert(Holder).second)
      continue;
    Ctx.diagnose(DiagnosticInfoUnsupported(*Holder, Text, I->getDebugLoc()));
    Holders.push_back(Holder);
  }
  for (Function *Holder : Holders)
    dropBody(*Holder);
  for (GlobalValue *GV : Globals)
    Ctx.diagnose(DiagnosticInfoGeneric(
        (Twine(isa<GlobalVariable>(GV) ? "global variable " : "global value ") +
         MSIL::getIRName(*GV) + ": " + Text)
            .str()));
  // What is left are the uses in initializers and other constants.
  F.replaceUsesWithIf(ConstantPointerNull::get(F.getType()), [](Use &U) {
    auto *CB = dyn_cast<CallBase>(U.getUser());
    return !CB || !CB->isCallee(&U);
  });
  return true;
}

/// Reports every call of a declared returns_twice function other than the
/// setjmp family (vfork, getcontext): CIL cannot return twice from a call.
/// The holding function loses its body. Adds the functions whose bodies were
/// dropped to \p Dropped.
static bool diagnoseReturnsTwice(Module &M,
                                 SmallPtrSetImpl<Function *> &Dropped) {
  bool Changed = false;
  for (Function &F : M) {
    if (!F.isDeclaration() || F.isIntrinsic() ||
        MSIL::isSetjmpName(F.getName()) ||
        !F.hasFnAttribute(Attribute::ReturnsTwice))
      continue;
    std::string Text = ("calling the returns_twice function '" + F.getName() +
                        "' is not supported for MSIL")
                           .str();
    SmallVector<Function *, 4> Holders;
    for (Use &U : F.uses()) {
      auto *CB = dyn_cast<CallBase>(U.getUser());
      if (!CB || !CB->isCallee(&U))
        continue;
      Function *Holder = CB->getFunction();
      if (!Dropped.insert(Holder).second)
        continue;
      F.getContext().diagnose(
          DiagnosticInfoUnsupported(*Holder, Text, CB->getDebugLoc()));
      Holders.push_back(Holder);
    }
    for (Function *Holder : Holders)
      dropBody(*Holder);
    Changed |= !Holders.empty();
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
// The per-module helpers (plan 5.14.6, K4)
//===----------------------------------------------------------------------===//

/// The type of __msil_sjlj_throw: void(ptr status).
static FunctionType *getThrowHelperType(LLVMContext &Ctx) {
  return FunctionType::get(Type::getVoidTy(Ctx), PointerType::get(Ctx, 0),
                           /*isVarArg=*/false);
}

/// The type of __msil_longjmp: void(ptr buf, i32 val).
static FunctionType *getLongjmpHelperType(LLVMContext &Ctx) {
  return FunctionType::get(Type::getVoidTy(Ctx),
                           {PointerType::get(Ctx, 0), Type::getInt32Ty(Ctx)},
                           /*isVarArg=*/false);
}

/// Reports each helper name that a global of the module holds without being
/// a function of the helper's type (a declaration or definition of another
/// type, or a variable): the names are reserved for the toolchain (plan
/// 5.14.6 I2, 5.15.4). Returns whether any was reported; the pass then
/// lowers nothing in the module. A declaration of the helper's own type is
/// completed by MSIL::getOrCreateModuleHelper.
static bool diagnoseReservedHelperNames(Module &M) {
  LLVMContext &Ctx = M.getContext();
  bool Reported = false;
  for (auto [Name, Ty] :
       {std::pair(ThrowHelperName, getThrowHelperType(Ctx)),
        std::pair(LongjmpHelperName, getLongjmpHelperType(Ctx))}) {
    GlobalValue *GV = M.getNamedValue(Name);
    if (!GV)
      continue;
    auto *F = dyn_cast<Function>(GV);
    if (F && F->getFunctionType() == Ty)
      continue;
    Ctx.diagnose(DiagnosticInfoGeneric(
        (Twine("'") + Name +
         "' is reserved for the MSIL toolchain (a setjmp and longjmp helper)")
            .str()));
    Reported = true;
  }
  return Reported;
}

/// Returns __msil_sjlj_throw(ptr status): noreturn, "msil-sjlj-throw", with
/// the IR body 'unreachable'. The AsmPrinter writes its IL body (ldnull;
/// ldarg.0; conv.i4; newobj ExternalException::.ctor(string, int32); throw).
static Function *getThrowHelper(Module &M) {
  FunctionType *Ty = getThrowHelperType(M.getContext());
  return MSIL::getOrCreateModuleHelper(M, ThrowHelperName, Ty, [](Function &F) {
    F.addFnAttr(Attribute::NoReturn);
    F.addFnAttr(ThrowAttr);
    F.getArg(0)->setName("status");
    new UnreachableInst(F.getContext(),
                        BasicBlock::Create(F.getContext(), "entry", &F));
  });
}

/// Returns __msil_longjmp(ptr buf, i32 val): noreturn and without
/// unnamed_addr, because it takes the place of &longjmp. A buffer that a
/// setjmp site filled holds the magic: the helper arms the target status
/// block with val (1 when val is 0, C17 7.13.2.1p4) and then the site label,
/// and throws through __msil_sjlj_throw(status). Any other buffer (one that
/// native code or nothing filled) throws with a null status, whose HResult 0
/// no filter accepts, so the process ends on the unhandled exception.
static Function *getLongjmpHelper(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  FunctionType *Ty = getLongjmpHelperType(Ctx);
  Function *Throw = getThrowHelper(M);
  return MSIL::getOrCreateModuleHelper(
      M, LongjmpHelperName, Ty, [&](Function &F) {
        F.setUnnamedAddr(GlobalValue::UnnamedAddr::None);
        F.addFnAttr(Attribute::NoReturn);
        Argument *Buf = F.getArg(0);
        Argument *Val = F.getArg(1);
        Buf->setName("buf");
        Val->setName("val");

        BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", &F);
        BasicBlock *Arm = BasicBlock::Create(Ctx, "arm", &F);
        BasicBlock *Bad = BasicBlock::Create(Ctx, "nobuf", &F);

        IRBuilder<> IRB(Entry);
        Value *Magic = IRB.CreateAlignedLoad(I32Ty, Buf, Align(4), "magic");
        Value *Ours = IRB.CreateICmpEQ(Magic, IRB.getInt32(SjLjMagic), "ours");
        IRB.CreateCondBr(Ours, Arm, Bad);

        IRB.SetInsertPoint(Arm);
        Value *StatusP = IRB.CreateConstInBoundsGEP1_64(
            IRB.getInt8Ty(), Buf, StatusOffset, "status.p");
        Value *Status =
            IRB.CreateAlignedLoad(PtrTy, StatusP, Align(8), "status");
        Value *IsZero = IRB.CreateICmpEQ(Val, IRB.getInt32(0), "val.zero");
        Value *Val1 = IRB.CreateSelect(IsZero, IRB.getInt32(1), Val, "val.1");
        Value *ValP =
            IRB.CreateConstInBoundsGEP1_64(IRB.getInt8Ty(), Status, 4, "val.p");
        IRB.CreateAlignedStore(Val1, ValP, Align(4));
        Value *LabelP = IRB.CreateConstInBoundsGEP1_64(IRB.getInt8Ty(), Buf,
                                                       LabelOffset, "label.p");
        Value *Label = IRB.CreateAlignedLoad(I32Ty, LabelP, Align(4), "label");
        IRB.CreateAlignedStore(Label, Status, Align(4));
        IRB.CreateCall(Throw, {Status})->setDoesNotReturn();
        IRB.CreateUnreachable();

        IRB.SetInsertPoint(Bad);
        IRB.CreateCall(Throw,
                       {ConstantPointerNull::get(cast<PointerType>(PtrTy))})
            ->setDoesNotReturn();
        IRB.CreateUnreachable();
      });
}

/// Turns every call of the longjmp declaration \p F into a call of
/// \p Helper, and every other use into \p Helper's address. A call whose
/// type is not void(ptr, i32) (a prototype that differs from the C
/// library's) has its arguments converted.
static void replaceLongjmp(Function &F, Function &Helper) {
  FunctionType *HelperTy = Helper.getFunctionType();
  for (User *U : make_early_inc_range(F.users())) {
    auto *CB = dyn_cast<CallInst>(U);
    if (!CB || CB->getCalledOperand() != &F)
      continue;
    if (CB->getFunctionType() == HelperTy) {
      CB->setCalledFunction(&Helper);
      continue;
    }
    // Convert the arguments that a mismatched prototype passes: a pointer or
    // integer buffer and an integer value; anything missing is poison.
    IRBuilder<> IRB(CB);
    Value *Args[2];
    for (unsigned I = 0; I != 2; ++I) {
      Type *Ty = HelperTy->getParamType(I);
      Value *A = I < CB->arg_size() ? CB->getArgOperand(I) : nullptr;
      if (!A || (!A->getType()->isIntegerTy() && !A->getType()->isPointerTy()))
        Args[I] = PoisonValue::get(Ty);
      else if (Ty->isPointerTy())
        Args[I] = A->getType()->isPointerTy()
                      ? IRB.CreatePointerBitCastOrAddrSpaceCast(A, Ty)
                      : IRB.CreateIntToPtr(A, Ty);
      else
        Args[I] = A->getType()->isPointerTy() ? IRB.CreatePtrToInt(A, Ty)
                                              : IRB.CreateSExtOrTrunc(A, Ty);
    }
    CallInst *New = IRB.CreateCall(&Helper, Args);
    New->setDoesNotReturn();
    New->setDebugLoc(CB->getDebugLoc());
    if (!CB->getType()->isVoidTy())
      CB->replaceAllUsesWith(PoisonValue::get(CB->getType()));
    CB->eraseFromParent();
  }
  // A taken address (in code or in an initializer).
  F.replaceAllUsesWith(&Helper);
}

//===----------------------------------------------------------------------===//
// The transformation of one function
//===----------------------------------------------------------------------===//

/// The attributes of argument \p AS at parameter index \p Index of a wrapper
/// or of the call of a wrapper. sret is dropped from a parameter that the
/// callee pointer of an indirect wrapper pushed past the second position,
/// where the verifier rejects it; the inner call keeps it.
static AttributeSet getWrapperParamAttrs(LLVMContext &Ctx, AttributeSet AS,
                                         unsigned Index) {
  if (Index > 1 && AS.hasAttribute(Attribute::StructRet))
    return AS.removeAttribute(Ctx, Attribute::StructRet);
  return AS;
}

namespace {

/// One setjmp site.
struct SetjmpSite {
  CallInst *Call;
  /// The buffer (the call's first argument, as a pointer).
  Value *Buf = nullptr;
  /// The continuation: the block after the call, which begins with Result.
  BasicBlock *Cont = nullptr;
  PHINode *Result = nullptr;
  /// llvm.stacksave at the site.
  Value *SavedSP = nullptr;
  /// Whether the site saves the signal mask (a sigsetjmp site on a Linux or
  /// dotnet triple whose mask argument is not the constant 0).
  bool SavesMask = false;
};

class SjLjLowering {
  Module &M;
  const Triple &TT;
  function_ref<const TargetLibraryInfo &(Function &)> GetTLI;

  /// The wrappers made so far, by (direct callee or null, call-site type,
  /// wrapper type, calling convention, call-site attributes): everything that
  /// the wrapper copies from the call it serves.
  DenseMap<std::tuple<Constant *, FunctionType *, FunctionType *,
                      CallingConv::ID, AttributeList>,
           Function *>
      Wrappers;
  unsigned NextDirect = 0;
  unsigned NextIndirect = 0;

  bool canLongjmp(CallInst &CB, Function &F);
  Function *getWrapper(CallInst &CB, FunctionType *WrapperTy);
  void wrapCall(CallInst &CB, AllocaInst *Status, BasicBlock *&Dispatch,
                Function &F);
  void storePrefix(IRBuilder<> &IRB, Value *Buf, unsigned Label,
                   AllocaInst *Status);
  FunctionCallee getSigprocmask();

public:
  SjLjLowering(Module &M,
               function_ref<const TargetLibraryInfo &(Function &)> GetTLI)
      : M(M), TT(M.getTargetTriple()), GetTLI(GetTLI) {}

  void run(Function &F, SmallPtrSetImpl<Function *> &SetjmpFns);
};

} // end anonymous namespace

/// Whether the call \p CB in the setjmp caller \p F may longjmp, so that it
/// must go through a wrapper (plan 5.15.4 step 3 and M27.b's list).
bool SjLjLowering::canLongjmp(CallInst &CB, Function &F) {
  if (CB.isInlineAsm() || isa<IntrinsicInst>(CB))
    return false;
  // A callee that comes back to a point of the current call stack that
  // includes this invocation (LangRef, willreturn).
  if (CB.hasFnAttr(Attribute::WillReturn))
    return false;
  if (auto *Callee =
          dyn_cast<Function>(CB.getCalledOperand()->stripPointerCasts())) {
    if (Callee->isIntrinsic() || Callee->hasFnAttribute(InvokeAttr) ||
        Callee->hasFnAttribute(ThrowAttr))
      return false;
    // A C library function that calls no function of the program. Only a
    // declaration: a definition of a library name is the program's code.
    // ExpandVariadics (P7) has already given a variadic declaration that it
    // expands (MSILPrepareVariadics' "msil-variadic") a va_list parameter in
    // place of the '...', which TargetLibraryInfo's prototype check rejects:
    // such a declaration (the printf family) is recognized by its name.
    if (Callee->isDeclaration() && !isCallbackLibraryName(Callee->getName())) {
      const TargetLibraryInfo &TLI = GetTLI(F);
      LibFunc LF = TLI.getLibFunc(CB);
      if (LF == NotLibFunc && !CB.isNoBuiltin() && !Callee->isVarArg() &&
          Callee->hasFnAttribute("msil-variadic"))
        LF = TLI.getLibFunc(Callee->getName());
      if (LF != NotLibFunc && TLI.has(LF))
        return false;
    }
  }
  return true;
}

/// Returns the wrapper for the call \p CB, whose wrapper type is
/// \p WrapperTy: one per (callee, call-site type, calling convention,
/// call-site attributes) for a direct call, one per (call-site type, calling
/// convention, call-site attributes) for an indirect one. The wrapper calls
/// the callee with the call-site type, the calling convention and every
/// call-site attribute of each call it serves (all the same), and returns
/// what the callee returns. Under opaque pointers a byval and a plain pointer
/// parameter have the same type, so the attributes are part of the key.
Function *SjLjLowering::getWrapper(CallInst &CB, FunctionType *WrapperTy) {
  Value *CalleeV = CB.getCalledOperand();
  auto *Direct = dyn_cast<Constant>(CalleeV);
  const AttributeList &CallAL = CB.getAttributes();
  auto Key = std::make_tuple(Direct, CB.getFunctionType(), WrapperTy,
                             CB.getCallingConv(), CallAL);
  if (Function *W = Wrappers.lookup(Key))
    return W;

  LLVMContext &Ctx = M.getContext();
  std::string Name;
  do
    Name = Direct ? WrapperPrefix + utostr(NextDirect++)
                  : IndirectWrapperPrefix + utostr(NextIndirect++);
  while (M.getNamedValue(Name));

  Function *W =
      Function::Create(WrapperTy, GlobalValue::InternalLinkage, Name, M);
  unsigned StatusIdx = WrapperTy->getNumParams() - 1;
  unsigned Shift = Direct ? 0 : 1;
  W->addFnAttr(Attribute::NoInline);
  W->addFnAttr(InvokeAttr, utostr(StatusIdx));
  W->addRetAttrs(AttrBuilder(Ctx, CallAL.getRetAttrs()));
  for (unsigned I = 0, E = CB.arg_size(); I != E; ++I)
    W->addParamAttrs(
        I + Shift,
        AttrBuilder(Ctx, getWrapperParamAttrs(Ctx, CallAL.getParamAttrs(I),
                                              I + Shift)));
  if (!Direct)
    W->getArg(0)->setName("callee");
  W->getArg(StatusIdx)->setName("sjlj.st");

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", W);
  SmallVector<Value *, 8> Args;
  for (unsigned I = 0, E = CB.arg_size(); I != E; ++I)
    Args.push_back(W->getArg(I + Shift));
  CallInst *Inner = CallInst::Create(
      CB.getFunctionType(), Direct ? CalleeV : W->getArg(0), Args, "", Entry);
  Inner->setAttributes(CallAL);
  Inner->setCallingConv(CB.getCallingConv());
  if (Inner->getType()->isVoidTy())
    ReturnInst::Create(Ctx, Entry);
  else
    ReturnInst::Create(Ctx, Inner, Entry);

  LLVM_DEBUG(dbgs() << "New wrapper " << Name << " for " << CB << '\n');
  Wrappers[Key] = W;
  return W;
}

/// Replaces \p CB by a call of its wrapper with the status block \p Status
/// as the last argument, followed by a test of the status block's label that
/// branches to \p F's dispatch block (created on first use) when a longjmp
/// to this invocation came back.
void SjLjLowering::wrapCall(CallInst &CB, AllocaInst *Status,
                            BasicBlock *&Dispatch, Function &F) {
  LLVMContext &Ctx = M.getContext();
  SmallVector<Type *, 8> ParamTys;
  SmallVector<Value *, 8> Args;
  bool Direct = isa<Constant>(CB.getCalledOperand());
  if (!Direct) {
    ParamTys.push_back(CB.getCalledOperand()->getType());
    Args.push_back(CB.getCalledOperand());
  }
  for (Value *A : CB.args()) {
    ParamTys.push_back(A->getType());
    Args.push_back(A);
  }
  ParamTys.push_back(Status->getType());
  Args.push_back(Status);
  auto *WrapperTy = FunctionType::get(CB.getType(), ParamTys,
                                      /*isVarArg=*/false);
  Function *W = getWrapper(CB, WrapperTy);

  // The wrapper call keeps the argument and return attributes, shifted past
  // the callee pointer of an indirect wrapper, and no function attribute:
  // noreturn, memory effects and "msil-fixed-arity" describe the inner call,
  // and the wrapper returns after a longjmp.
  const AttributeList &CallAL = CB.getAttributes();
  SmallVector<AttributeSet, 8> ArgAttrs;
  if (!Direct)
    ArgAttrs.push_back(AttributeSet());
  for (unsigned I = 0, E = CB.arg_size(); I != E; ++I)
    ArgAttrs.push_back(
        getWrapperParamAttrs(Ctx, CallAL.getParamAttrs(I), ArgAttrs.size()));
  ArgAttrs.push_back(AttributeSet());

  IRBuilder<> IRB(&CB);
  CallInst *New = IRB.CreateCall(W, Args);
  New->setAttributes(
      AttributeList::get(Ctx, AttributeSet(), CallAL.getRetAttrs(), ArgAttrs));
  New->setDebugLoc(CB.getDebugLoc());
  New->takeName(&CB);
  CB.replaceAllUsesWith(New);
  CB.eraseFromParent();

  BasicBlock *BB = New->getParent();
  BasicBlock *Tail =
      SplitBlock(BB, New->getNextNode(), static_cast<DominatorTree *>(nullptr),
                 /*LI=*/nullptr, /*MSSAU=*/nullptr, "sjlj.cont");
  if (!Dispatch)
    Dispatch = BasicBlock::Create(Ctx, "sjlj.dispatch", &F);
  BB->getTerminator()->eraseFromParent();
  IRB.SetInsertPoint(BB);
  IRB.SetCurrentDebugLocation(New->getDebugLoc());
  Value *Label =
      IRB.CreateAlignedLoad(IRB.getInt32Ty(), Status, Align(4), "sjlj.label");
  Value *Jumped = IRB.CreateICmpNE(Label, IRB.getInt32(0), "sjlj.jumped");
  IRB.CreateCondBr(Jumped, Dispatch, Tail);
}

/// Stores the 16-byte prefix of the site \p Label into \p Buf: the magic, the
/// label and the status block's address.
void SjLjLowering::storePrefix(IRBuilder<> &IRB, Value *Buf, unsigned Label,
                               AllocaInst *Status) {
  IRB.CreateAlignedStore(IRB.getInt32(SjLjMagic), Buf, Align(4));
  IRB.CreateAlignedStore(IRB.getInt32(Label),
                         IRB.CreateConstInBoundsGEP1_64(IRB.getInt8Ty(), Buf,
                                                        LabelOffset,
                                                        "sjlj.buf.label"),
                         Align(4));
  IRB.CreateAlignedStore(Status,
                         IRB.CreateConstInBoundsGEP1_64(IRB.getInt8Ty(), Buf,
                                                        StatusOffset,
                                                        "sjlj.buf.status"),
                         Align(8));
}

/// int sigprocmask(int how, const sigset_t *set, sigset_t *oldset).
FunctionCallee SjLjLowering::getSigprocmask() {
  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  return M.getOrInsertFunction(
      "sigprocmask", FunctionType::get(Type::getInt32Ty(Ctx),
                                       {Type::getInt32Ty(Ctx), PtrTy, PtrTy},
                                       /*isVarArg=*/false));
}

/// Rebuilds SSA form after the CFG changes (from the Emscripten pass): a
/// value defined before a site and used after it reaches the use along the
/// call -> dispatch -> restore -> continuation edges too.
static void rebuildSSA(Function &F) {
  DominatorTree DT(F);
  SSAUpdaterBulk SSA;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (I.getType()->isVoidTy())
        continue;

      if (isa<AllocaInst>(&I)) {
        // If the alloca has any lifetime marker that is no longer dominated
        // by the alloca, remove all lifetime markers. Lifetime markers must
        // always work directly on the alloca, and this is no longer possible.
        bool HasNonDominatedLifetimeMarker = any_of(I.users(), [&](User *U) {
          auto *UserI = cast<Instruction>(U);
          return UserI->isLifetimeStartOrEnd() && !DT.dominates(&I, UserI);
        });
        if (HasNonDominatedLifetimeMarker) {
          for (User *U : make_early_inc_range(I.users())) {
            auto *UserI = cast<Instruction>(U);
            if (UserI->isLifetimeStartOrEnd())
              UserI->eraseFromParent();
          }
        }
      }

      unsigned VarID = SSA.AddVariable(I.getName(), I.getType());
      SSA.AddAvailableValue(VarID, &BB, &I);
      for (Use &U : I.uses()) {
        auto *User = cast<Instruction>(U.getUser());
        if (auto *UserPN = dyn_cast<PHINode>(User))
          if (UserPN->getIncomingBlock(U) == &BB)
            continue;
        if (DT.dominates(&I, User))
          continue;
        SSA.AddUse(VarID, &U);
      }
    }
  }
  SSA.RewriteAllUses(&DT);
}

/// Lowers the setjmp sites of \p F, a function that calls one of the setjmp
/// declarations \p SetjmpFns (plan 5.15.4 steps 1-6).
void SjLjLowering::run(Function &F, SmallPtrSetImpl<Function *> &SetjmpFns) {
  LLVMContext &Ctx = M.getContext();

  // Step 1: no exception model, so invokes become calls (LowerInvoke at P3
  // has normally done that already).
  bool LoweredInvoke = false;
  for (BasicBlock &BB : F)
    if (auto *II = dyn_cast<InvokeInst>(BB.getTerminator())) {
      changeToCall(II);
      LoweredInvoke = true;
    }
  if (LoweredInvoke)
    removeUnreachableBlocks(F);

  // The sites, numbered 1..N in block order.
  SmallVector<SetjmpSite, 4> Sites;
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (auto *CI = dyn_cast<CallInst>(&I))
        if (auto *Callee = dyn_cast<Function>(CI->getCalledOperand());
            Callee && SetjmpFns.contains(Callee))
          Sites.push_back({CI});
  if (Sites.empty())
    return;
  SmallPtrSet<Instruction *, 4> SiteCalls;
  for (SetjmpSite &S : Sites)
    SiteCalls.insert(S.Call);

  // The calls that may longjmp and that a site reaches, computed before the
  // CFG changes: the calls after a site in its block and every call of a
  // block reachable from a site's block.
  df_iterator_default_set<BasicBlock *, 16> Reachable;
  for (SetjmpSite &S : Sites)
    for (BasicBlock *Succ : successors(S.Call->getParent()))
      for (BasicBlock *BB : depth_first_ext(Succ, Reachable))
        (void)BB;
  SmallVector<CallInst *, 16> ToWrap;
  for (BasicBlock &BB : F) {
    bool AfterSite = Reachable.contains(&BB);
    for (Instruction &I : BB) {
      if (SiteCalls.contains(&I)) {
        AfterSite = true;
        continue;
      }
      if (auto *CI = dyn_cast<CallInst>(&I);
          AfterSite && CI && canLongjmp(*CI, F))
        ToWrap.push_back(CI);
    }
  }
  LLVM_DEBUG(dbgs() << "Function " << F.getName() << ": " << Sites.size()
                    << " setjmp sites, " << ToWrap.size()
                    << " calls to wrap\n");

  // Keep the static allocas static: one after a site in the entry block
  // would land in the site's continuation block. Each moves up to the end of
  // the entry block's leading allocas.
  BasicBlock &Entry = F.getEntryBlock();
  auto FirstNonAlloca = [&Entry] {
    BasicBlock::iterator It = Entry.getFirstInsertionPt();
    while (isa<AllocaInst>(*It))
      ++It;
    return It;
  };
  SmallVector<AllocaInst *, 4> LateAllocas;
  bool AfterSite = false;
  for (Instruction &I : Entry) {
    AfterSite |= SiteCalls.contains(&I);
    if (auto *AI = dyn_cast<AllocaInst>(&I);
        AfterSite && AI && AI->isStaticAlloca())
      LateAllocas.push_back(AI);
  }
  for (AllocaInst *AI : LateAllocas)
    AI->moveBefore(FirstNonAlloca());

  // The status block {i32 label, i32 val}, disarmed (label 0) at entry.
  IRBuilder<> IRB(Entry.getFirstInsertionPt());
  auto *StatusTy = StructType::get(IRB.getInt32Ty(), IRB.getInt32Ty());
  AllocaInst *Status = IRB.CreateAlloca(StatusTy, nullptr, "sjlj.st");
  Status->setAlignment(Align(8));
  IRB.SetInsertPoint(FirstNonAlloca());
  IRB.CreateAlignedStore(IRB.getInt32(0), Status, Align(8));

  bool MaskTriple = TT.isOSLinux() || TT.isOSDotNet();
  bool Nullify = ToWrap.empty();
  unsigned Label = 0;
  for (SetjmpSite &S : Sites) {
    ++Label;
    CallInst *CI = S.Call;
    IRB.SetInsertPoint(CI);
    IRB.SetCurrentDebugLocation(CI->getDebugLoc());

    Value *Buf = CI->arg_size() ? CI->getArgOperand(0) : nullptr;
    if (Buf && Buf->getType()->isIntegerTy())
      Buf = IRB.CreateIntToPtr(Buf, IRB.getPtrTy());
    else if (Buf && !Buf->getType()->isPointerTy())
      Buf = nullptr;
    S.Buf = Buf;
    if (Buf)
      storePrefix(IRB, Buf, Label, Status);

    // Step 6: no call that may longjmp follows any site, so each setjmp
    // returns 0.
    if (Nullify) {
      if (!CI->getType()->isVoidTy())
        CI->replaceAllUsesWith(Constant::getNullValue(CI->getType()));
      continue;
    }

    // Step 2: save the stack pointer and, at a sigsetjmp site of a Linux or
    // dotnet triple, the mask-saved flag and the signal mask.
    S.SavedSP = IRB.CreateStackSave("sjlj.ssp");
    auto *Callee = cast<Function>(CI->getCalledOperand());
    if (Buf && MaskTriple && isSigsetjmpName(Callee->getName()) &&
        CI->arg_size() >= 2 && CI->getArgOperand(1)->getType()->isIntegerTy()) {
      Value *SaveMask = CI->getArgOperand(1);
      auto *C = dyn_cast<ConstantInt>(SaveMask);
      S.SavesMask = !C || !C->isZero();
      Value *Flag =
          S.SavesMask
              ? IRB.CreateZExt(
                    IRB.CreateICmpNE(SaveMask,
                                     ConstantInt::get(SaveMask->getType(), 0)),
                    IRB.getInt32Ty(), "sjlj.savemask")
              : IRB.getInt32(0);
      IRB.CreateAlignedStore(
          Flag,
          IRB.CreateConstInBoundsGEP1_64(IRB.getInt8Ty(), Buf, MaskSavedOffset,
                                         "sjlj.buf.masksaved"),
          Align(4));
      if (S.SavesMask) {
        Value *MaskP = IRB.CreateConstInBoundsGEP1_64(
            IRB.getInt8Ty(), Buf, MaskOffset, "sjlj.buf.mask");
        IRB.CreateCall(getSigprocmask(),
                       {IRB.getInt32(SigBlock),
                        ConstantPointerNull::get(IRB.getPtrTy()), MaskP});
      }
    }

    // The continuation, where the result is 0 after the site and val after
    // a longjmp.
    BasicBlock *BB = CI->getParent();
    S.Cont = SplitBlock(BB, CI->getNextNode(),
                        static_cast<DominatorTree *>(nullptr), /*LI=*/nullptr,
                        /*MSSAU=*/nullptr, "setjmp.cont." + Twine(Label));
    if (!CI->getType()->isVoidTy()) {
      IRB.SetInsertPoint(S.Cont->getFirstNonPHIIt());
      S.Result = IRB.CreatePHI(CI->getType(), 2, "setjmp.ret." + Twine(Label));
      S.Result->addIncoming(Constant::getNullValue(CI->getType()), BB);
      CI->replaceAllUsesWith(S.Result);
    }
  }

  if (!Nullify) {
    // Step 3: the wrapped calls.
    BasicBlock *Dispatch = nullptr;
    for (CallInst *CB : ToWrap)
      wrapCall(*CB, Status, Dispatch, F);
    assert(Dispatch && "a wrapped call creates the dispatch block");

    // Step 4: the dispatch block loads val, disarms the status block and
    // switches to the restore block of the site that the label names.
    IRB.SetInsertPoint(Dispatch);
    IRB.SetCurrentDebugLocation(DebugLoc());
    Value *Lbl =
        IRB.CreateAlignedLoad(IRB.getInt32Ty(), Status, Align(8), "sjlj.lbl");
    Value *ValP = IRB.CreateConstInBoundsGEP1_64(IRB.getInt8Ty(), Status, 4,
                                                 "sjlj.val.p");
    Value *Val =
        IRB.CreateAlignedLoad(IRB.getInt32Ty(), ValP, Align(4), "sjlj.val");
    IRB.CreateAlignedStore(IRB.getInt32(0), Status, Align(8));
    BasicBlock *BadBB = BasicBlock::Create(Ctx, "sjlj.bad", &F);
    new UnreachableInst(Ctx, BadBB);
    SwitchInst *SI = IRB.CreateSwitch(Lbl, BadBB, Sites.size());

    Label = 0;
    for (SetjmpSite &S : Sites) {
      ++Label;
      BasicBlock *Restore =
          BasicBlock::Create(Ctx, "sjlj.restore." + Twine(Label), &F);
      SI->addCase(IRB.getInt32(Label), Restore);
      IRB.SetInsertPoint(Restore);
      IRB.SetCurrentDebugLocation(S.Call->getDebugLoc());
      IRB.CreateStackRestore(S.SavedSP);
      if (S.SavesMask) {
        // glibc's longjmp and siglongjmp both restore a saved mask.
        Value *Flag = IRB.CreateAlignedLoad(
            IRB.getInt32Ty(),
            IRB.CreateConstInBoundsGEP1_64(
                IRB.getInt8Ty(), S.Buf, MaskSavedOffset, "sjlj.buf.masksaved"),
            Align(4), "sjlj.masksaved");
        BasicBlock *MaskBB =
            BasicBlock::Create(Ctx, "sjlj.restore.mask." + Twine(Label), &F);
        BasicBlock *Next =
            BasicBlock::Create(Ctx, "sjlj.restore.cont." + Twine(Label), &F);
        IRB.CreateCondBr(IRB.CreateICmpNE(Flag, IRB.getInt32(0)), MaskBB, Next);
        IRB.SetInsertPoint(MaskBB);
        Value *MaskP = IRB.CreateConstInBoundsGEP1_64(
            IRB.getInt8Ty(), S.Buf, MaskOffset, "sjlj.buf.mask");
        IRB.CreateCall(getSigprocmask(),
                       {IRB.getInt32(SigSetMask), MaskP,
                        ConstantPointerNull::get(IRB.getPtrTy())});
        IRB.CreateBr(Next);
        IRB.SetInsertPoint(Next);
      }
      if (S.Result) {
        Type *Ty = S.Result->getType();
        Value *V = Ty->isIntegerTy() ? IRB.CreateSExtOrTrunc(Val, Ty)
                                     : PoisonValue::get(Ty);
        S.Result->addIncoming(V, IRB.GetInsertBlock());
      }
      IRB.CreateBr(S.Cont);
    }
  }

  // The sites go, and with them the frame address or llvm.sponentry that
  // the two-argument MSVC forms pass when nothing else uses it (else
  // instruction selection would report it).
  for (SetjmpSite &S : Sites) {
    SmallVector<Value *, 2> Args(S.Call->args());
    S.Call->eraseFromParent();
    for (Value *A : Args) {
      auto *II = dyn_cast<IntrinsicInst>(A);
      if (!II || !II->use_empty() ||
          (II->getIntrinsicID() != Intrinsic::frameaddress &&
           II->getIntrinsicID() != Intrinsic::sponentry))
        continue;
      Function *Decl = II->getCalledFunction();
      II->eraseFromParent();
      if (Decl->use_empty())
        Decl->eraseFromParent();
    }
  }

  // Step 5.
  if (!Nullify)
    rebuildSSA(F);
}

//===----------------------------------------------------------------------===//
// The module
//===----------------------------------------------------------------------===//

bool MSILLowerSjLjLegacy::runOnModule(Module &M) {
  if (DisableSjLjLowering)
    return false;
  LLVM_DEBUG(dbgs() << "********** Lower setjmp and longjmp **********\n");

  // Only declarations are intercepted: a module that defines a function named
  // longjmp keeps it.
  SmallPtrSet<Function *, 4> SetjmpFns;
  SmallVector<Function *, 4> LongjmpFns;
  for (Function &F : M) {
    if (!F.isDeclaration() || F.isIntrinsic())
      continue;
    if (MSIL::isSetjmpName(F.getName()) && !F.use_empty())
      SetjmpFns.insert(&F);
    else if (MSIL::isLongjmpName(F.getName()) && !F.use_empty())
      LongjmpFns.push_back(&F);
  }

  bool Changed = false;
  SmallPtrSet<Function *, 4> Dropped;
  Changed |= diagnoseReturnsTwice(M, Dropped);
  if (SetjmpFns.empty() && LongjmpFns.empty())
    return Changed;
  // A user global that holds a helper's name stops the lowering before
  // MSIL::getOrCreateModuleHelper could meet it (its internal error is for
  // pass bugs only); the setjmp and longjmp calls stay as they are.
  if (diagnoseReservedHelperNames(M))
    return Changed;
  Changed = true;
  for (Function *F : SetjmpFns)
    diagnoseSetjmpAddress(*F, Dropped);

  // Every longjmp (calls and taken addresses) becomes __msil_longjmp first,
  // so that the calls of it that a site reaches are wrapped below: a
  // function that longjmps to its own buffer catches its own throw.
  for (Function *F : LongjmpFns) {
    if (F->use_empty())
      continue;
    replaceLongjmp(*F, *getLongjmpHelper(M));
  }

  // The setjmp callers, in module order.
  SmallPtrSet<Function *, 8> CallerSet;
  for (Function *S : SetjmpFns)
    for (User *U : S->users())
      if (auto *CB = dyn_cast<CallBase>(U))
        CallerSet.insert(CB->getFunction());
  SmallVector<Function *, 8> Callers;
  for (Function &F : M)
    if (CallerSet.contains(&F))
      Callers.push_back(&F);

  auto GetTLI = [this](Function &F) -> const TargetLibraryInfo & {
    return getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
  };
  SjLjLowering Lowering(M, GetTLI);
  for (Function *F : Callers)
    Lowering.run(*F, SetjmpFns);

  // The declarations that nothing uses any more.
  for (Function *F : SetjmpFns)
    if (F->use_empty())
      F->eraseFromParent();
  for (Function *F : LongjmpFns)
    if (F->use_empty())
      F->eraseFromParent();
  return Changed;
}
