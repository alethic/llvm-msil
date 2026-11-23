//===- llvm/CLI/ManagedCallIR.h - Managed-call IR rules ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The IR rules of managed calls (ImplementationPlan.md 5.20.4 MR4 and 5.20.5
// ML1 as amended) that slot P0 applies when it makes the stubs
// (MSIL::createManagedCallStubs, plan 5.20.5 ML2):
//   - which IR type fits a parameter's or the result's conversion code
//     (plan 5.20.3 MS5), the IR type of a .NET reference and the type of the
//     reference comparison helper (plan 5.21.6 RI1, RI6);
//   - when two !msil.managed.calls entries of one IR name agree (MR4, T-M17);
//   - which use of a managed-call declaration is a direct call with its own
//     function type, the one use that a stub keeps (T-M10, T-M17);
//   - which such call has a 'signext' or 'zeroext' of its own, at a narrow
//     parameter or result, that would give the declaration another stub
//     (T-M17): a module that the IR linker merged from units which msil-ld
//     refuses as separate inputs, by their stubs, is refused where such a
//     call remains, and one merged from units that msil-ld accepts links;
//   - which instructions and global values hold the other uses, T-M10's
//     holders (collectManagedCallHolders), with T-M10's text and the names
//     by which the holders are printed.
// msil-ld's --lto-emit-asm checks apply the first, the third and the fourth
// alike, so that both refuse the same declarations. msil-ld decides T-M17
// itself before LTO, comparing the bitcode inputs' entries with
// haveEqualManagedCallEntries (ASSEMBLY by the kind of link), and removes
// the list from the modules that LTO compiles, so that P0 compares entries
// only in a module that msil-ld did not link (llc). msil-ld also decides
// P0's T-M17 for a call of another function type or extension and T-M10
// itself before LTO's optimizer, in every LTO mode: it walks each bitcode
// input's modules, as the unit's own optimizer left them, with the third and
// the fourth rules and collectManagedCallHolders, and prints T-M10's text
// after the input's and the holders' names, so that P0 (clang -c, -S and
// llc) and msil-ld refuse the same declarations and name the same holders,
// but for the code of available_externally functions and variables: msil-ld
// drops their bodies and initializers first, as no output compiles them
// (EliminateAvailableExternallyPass drops them before P0 in clang -c at -O1
// and above, and LTO links another unit's definition in their place or
// drops the copy), while P0 counts the calls and uses there in a module
// that keeps them (llc on such IR, or clang -c at -O0 with an always_inline
// one). In LTO's code generation P0 then reports T-M17 and T-M10 only for a
// call or a use that LTO's optimizer made, or for an input whose symbol
// table lists no '__msil_mc.' name (hand-written IR).
//
// The file is header-only, and only code that links LLVMCore includes it (the
// MSIL target and lldMSIL). LLVMCLI's own sources and CLITests do not include
// it, so plan 5.7.1's LINK_COMPONENTS of LLVMCLI (BinaryFormat MC Support
// TargetParser) stay unchanged.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_MANAGEDCALLIR_H
#define LLVM_CLI_MANAGEDCALLIR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/ManagedSignature.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <string>

namespace llvm::cli {

//===----------------------------------------------------------------------===//
// The IR types of the conversions (plan 5.20.3 MS5)
//===----------------------------------------------------------------------===//

/// A handle's IR type (5.20.9): a pointer or a pointer-wide integer.
inline bool isManagedHandleType(const Type *T, const DataLayout &DL) {
  return T->isPointerTy() ||
         (T->isIntegerTy() &&
          T->getIntegerBitWidth() == DL.getPointerSizeInBits());
}

/// Whether \p T is the IR type of a .NET reference, target("msil.ref")
/// (ReferenceTypeName, plan 5.21.6 RI1), which has no parameters.
inline bool isManagedReferenceType(const Type *T) {
  const auto *TT = dyn_cast<TargetExtType>(T);
  return TT && TT->getName() == ReferenceTypeName &&
         TT->getNumTypeParameters() == 0 && TT->getNumIntParameters() == 0;
}

/// Whether \p FTy is the type of the reference comparison helper
/// (ReferenceEqName, plan 5.21.6 RI6): i1 (target("msil.ref"),
/// target("msil.ref")), not variadic.
inline bool isManagedReferenceEqType(const FunctionType *FTy) {
  return !FTy->isVarArg() && FTy->getReturnType()->isIntegerTy(1) &&
         FTy->getNumParams() == 2 &&
         isManagedReferenceType(FTy->getParamType(0)) &&
         isManagedReferenceType(FTy->getParamType(1));
}

/// Whether \p T is an IR integer of one of the widths \p Widths.
inline bool isIntegerTypeOfWidth(const Type *T, ArrayRef<unsigned> Widths) {
  return T->isIntegerTy() && is_contained(Widths, T->getIntegerBitWidth());
}

/// Whether \p C converts between the member's type \p CIL and the IR type
/// \p IRTy of a parameter or of the result (MS5), at the IR widths that
/// clang's Sema gives the C types of each row (plan 5.20.5 ML2 (1)): 'val'
/// with an integer type takes an integer of exactly that width, or an i1 (a
/// C _Bool) for an 8-bit type; char an i16; native int and native uint a
/// pointer or a pointer-wide integer; T* a pointer; 'enum' an i1, i8, i16,
/// i32 or i64; 'bool' an i1, i8, i16 or i32; 'ref' a reference type (string,
/// object, a class or an array) with the IR reference type
/// (isManagedReferenceType, plan 5.21.9 RM6). 'this-ref' fits TYPE, given as
/// \p CIL, when it is a class, an interface, System.String or System.Object
/// (TYPE's Class, the kind that parseManagedTypeName gives them all, or
/// String or Object), with the IR reference type. The other 'this' codes and
/// 'err' are checked by their positions, and 'void' only for the result.
inline bool fitsManagedConversion(ManagedConv C, const ManagedType &CIL,
                                  const Type *IRTy, const DataLayout &DL) {
  using TK = ManagedType::TypeKind;
  switch (C) {
  case ManagedConv::Bool:
    return CIL.Kind == TK::Bool && isIntegerTypeOfWidth(IRTy, {1, 8, 16, 32});
  case ManagedConv::Val:
    switch (CIL.Kind) {
    case TK::Float32:
      return IRTy->isFloatTy();
    case TK::Float64:
      return IRTy->isDoubleTy();
    case TK::Int8:
    case TK::UInt8:
      return isIntegerTypeOfWidth(IRTy, {1, 8});
    case TK::Char:
    case TK::Int16:
    case TK::UInt16:
      return isIntegerTypeOfWidth(IRTy, {16});
    case TK::Int32:
    case TK::UInt32:
      return isIntegerTypeOfWidth(IRTy, {32});
    case TK::Int64:
    case TK::UInt64:
      return isIntegerTypeOfWidth(IRTy, {64});
    case TK::NativeInt:
    case TK::NativeUInt:
      return isManagedHandleType(IRTy, DL);
    case TK::Pointer:
      return IRTy->isPointerTy();
    default:
      return false;
    }
  case ManagedConv::Enum:
    return CIL.Kind == TK::ValueType && !CIL.isGCHandle() &&
           isIntegerTypeOfWidth(IRTy, {1, 8, 16, 32, 64});
  case ManagedConv::UTF8:
  case ManagedConv::UTF16:
  case ManagedConv::UTF32:
    return CIL.Kind == TK::String && IRTy->isPointerTy();
  case ManagedConv::Obj:
    return CIL.isReferenceType() && isManagedHandleType(IRTy, DL);
  case ManagedConv::Boxed:
    return CIL.Kind == TK::ValueType && !CIL.isGCHandle() &&
           isManagedHandleType(IRTy, DL);
  case ManagedConv::GCH:
    return CIL.isGCHandle() && isManagedHandleType(IRTy, DL);
  case ManagedConv::Void:
    return IRTy->isVoidTy();
  case ManagedConv::Ref:
    return CIL.isReferenceType() && isManagedReferenceType(IRTy);
  case ManagedConv::ThisRef:
    return (CIL.Kind == TK::Class || CIL.Kind == TK::String ||
            CIL.Kind == TK::Object) &&
           isManagedReferenceType(IRTy);
  case ManagedConv::ThisObj:
  case ManagedConv::ThisVal:
  case ManagedConv::ThisGCH:
  case ManagedConv::Err:
    return false;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// The !msil.managed.calls entries (plan 5.20.4 MR4)
//===----------------------------------------------------------------------===//

/// The number of operands of a !msil.managed.calls entry before its
/// optional attributes operand (cli::ManagedCallsMetadata): the IR name,
/// TYPE, MEMBER, SIGNATURE, ASSEMBLY, the conversions and the catch list.
inline constexpr unsigned NumManagedCallOperands = 7;

/// The operand of a !msil.managed.calls entry that holds ASSEMBLY, the
/// optional fourth msil_method argument: "" when the declaration has none.
inline constexpr unsigned ManagedCallAssemblyOperand = 4;

/// Whether the !msil.managed.calls entries \p A and \p B have equal operands
/// below \p End; an operand that only one of them has is a difference.
inline bool haveEqualManagedCallOperands(const MDNode *A, const MDNode *B,
                                         unsigned End = ~0u) {
  End = std::min(End, std::max(A->getNumOperands(), B->getNumOperands()));
  for (unsigned I = 0; I != End; ++I) {
    const Metadata *X =
        I < A->getNumOperands() ? A->getOperand(I).get() : nullptr;
    const Metadata *Y =
        I < B->getNumOperands() ? B->getOperand(I).get() : nullptr;
    if (X != Y)
      return false;
  }
  return true;
}

/// How haveEqualManagedCallEntries compares ASSEMBLY.
enum class ManagedCallAssemblyRule {
  /// ASSEMBLY never differs. A link that writes an image binds every member
  /// by its name against the references, whatever scope a stub names (a CIL
  /// object names [msil.bcl]), so the operand changes nothing in the image.
  Ignore,
  /// ASSEMBLY as the scope that whole-program text names: the operand, or
  /// ManagedDefaultAssembly when it is "".
  TextScope,
};

/// haveEqualManagedCallOperands, but for ASSEMBLY, which \p Rule compares.
/// msil-ld's T-M17 before LTO uses it; slot P0 compares every operand
/// (haveEqualManagedCallOperands).
inline bool haveEqualManagedCallEntries(const MDNode *A, const MDNode *B,
                                        ManagedCallAssemblyRule Rule,
                                        unsigned End = ~0u) {
  End = std::min(End, std::max(A->getNumOperands(), B->getNumOperands()));
  for (unsigned I = 0; I != End; ++I) {
    const Metadata *X =
        I < A->getNumOperands() ? A->getOperand(I).get() : nullptr;
    const Metadata *Y =
        I < B->getNumOperands() ? B->getOperand(I).get() : nullptr;
    if (X == Y)
      continue;
    if (I != ManagedCallAssemblyOperand)
      return false;
    if (Rule == ManagedCallAssemblyRule::Ignore)
      continue;
    const auto *SX = dyn_cast_or_null<MDString>(X);
    const auto *SY = dyn_cast_or_null<MDString>(Y);
    if (!SX || !SY)
      return false;
    auto Scope = [](const MDString *S) {
      return S->getString().empty() ? StringRef(ManagedDefaultAssembly)
                                    : S->getString();
    };
    if (Scope(SX) != Scope(SY))
      return false;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// The uses of a managed-call declaration (T-M10, T-M17)
//===----------------------------------------------------------------------===//

/// Whether \p U is the callee operand of a call (not a callbr), whatever
/// the call's function type.
inline bool isManagedCalleeUse(const Use &U) {
  const auto *CB = dyn_cast<CallBase>(U.getUser());
  return CB && CB->isCallee(&U) && !isa<CallBrInst>(CB);
}

/// Whether \p U is a direct call of the managed-call declaration \p F with
/// its own function type: the one use that a stub keeps.
inline bool isDirectManagedCall(const Use &U, const Function &F) {
  return isManagedCalleeUse(U) &&
         cast<CallBase>(U.getUser())->getFunctionType() == F.getFunctionType();
}

/// Whether a direct call of \p F has a function type other than \p F's.
inline bool hasManagedCallOfOtherType(const Function &F) {
  return any_of(F.uses(), [&](const Use &U) {
    return isManagedCalleeUse(U) && !isDirectManagedCall(U, F);
  });
}

namespace detail {
/// The extension attribute of a narrow integer position.
enum class ManagedCallExtension : uint8_t { None, Sign, Zero };

/// The 'signext' or 'zeroext' in \p Attrs; the IR verifier refuses both.
inline ManagedCallExtension getManagedCallExtension(AttributeSet Attrs) {
  if (Attrs.hasAttribute(Attribute::SExt))
    return ManagedCallExtension::Sign;
  if (Attrs.hasAttribute(Attribute::ZExt))
    return ManagedCallExtension::Zero;
  return ManagedCallExtension::None;
}

/// Whether the stub that slot P0 makes of a managed-call declaration differs
/// when one position of it, the result (\p IsResult) or a parameter, with
/// the conversion code \p C and the IR type \p T, has the extension \p A or
/// \p B (hasManagedCallOfOtherExtension lists the positions).
inline bool changesManagedCallStub(bool IsResult, ManagedConv C, const Type *T,
                                   ManagedCallExtension A,
                                   ManagedCallExtension B) {
  using Ext = ManagedCallExtension;
  if (A == B || !isIntegerTypeOfWidth(T, {1, 8, 16}))
    return false;
  bool I1 = T->getIntegerBitWidth() == 1;
  if (IsResult)
    return !I1 && (C == ManagedConv::Val || C == ManagedConv::Enum);
  switch (C) {
  case ManagedConv::Bool:
    return (A == Ext::None) != (B == Ext::None);
  case ManagedConv::Val:
    return I1 && (A == Ext::Zero) != (B == Ext::Zero);
  case ManagedConv::Enum:
    return (A == Ext::Zero) != (B == Ext::Zero);
  default:
    return false;
  }
}
} // namespace detail

/// Whether a direct call of the managed-call declaration \p F with its own
/// function type (isDirectManagedCall) has, at a parameter or the result, a
/// 'signext' or 'zeroext' of its own that \p F's attributes do not have
/// there, where that attribute would give \p F another stub. Slot P0 reports
/// T-M17's first form for such a call, as for a call of another function
/// type, and so does msil-ld for the bitcode inputs before LTO; msil-ld's
/// --lto-emit-asm checks leave \p F to P0. The IR linker leaves such a call
/// when two units declare \p F with C prototypes that differ only in a
/// narrow integer's signedness (a 'short' and an 'unsigned short' result):
/// it keeps the first unit's declaration with its parameter and result
/// attributes, and each call keeps its own. One unit makes the same IR when
/// it calls \p F through a cast pointer that changes only that.
///
/// Only the call's own attribute list counts (never CallBase::paramHasAttr
/// or hasRetAttr, which fall back to \p F's): a position where the call has
/// neither attribute reads \p F's, as \p F's stub gives it. The positions
/// where the extension changes the stub, by \p F's conversion code
/// ("msil-managed-conv") and IR type (plan 5.20.3 MS5 as amended, and the
/// exact IL types of 'enum' conversions of plan 5.20.7 MK2 as amended):
///   - an i8 or i16 result converted by 'val' or 'enum': the stub ends in
///     conv.i1 or conv.i2 with signext, conv.u1 or conv.u2 with zeroext and
///     in neither without an attribute, and an 'enum' result's exact type is
///     unsigned only with zeroext, so any other extension counts;
///   - an i1, i8 or i16 parameter converted by 'bool': the stub narrows the
///     value before it tests it only without an attribute, while signext and
///     zeroext make one stub, so a call's attribute counts where \p F's
///     position has neither;
///   - an i1 parameter converted by 'val', and an i1, i8 or i16 parameter
///     converted by 'enum': the stub narrows an i1, and gives an 'enum'
///     parameter an unsigned exact type, only without zeroext, while signext
///     makes the stub of no attribute, so a call's attribute counts where it
///     changes whether the position is zeroext.
/// No other position counts, because its stub is the same with signext,
/// with zeroext and with neither: a 'val' i8 or i16 parameter (the member's
/// call truncates the int32 to the member's parameter of that width), a
/// 'bool' result and an i1 result (both made 0 or 1), a wider integer, a
/// pointer and a floating-point value.
///
/// clang writes each call's ABI attributes as its declaration's, so a merged
/// call that this refuses comes from a unit whose declaration makes another
/// stub than \p F's, and msil-ld, which compares the stubs that separate
/// units make of their declarations (each declaration compiled alone),
/// refuses those units too: units that msil-ld accepts still link after the
/// IR linker merges them. Units that it refuses may link merged where no
/// call of a later unit keeps an attribute of its own that counts, as \p F's
/// stub then serves every call. False when \p F's conversions do not parse
/// or are not one per IR parameter: P0 reports such attributes as invalid.
inline bool hasManagedCallOfOtherExtension(const Function &F) {
  Expected<ManagedConvList> Conv = parseManagedConvList(
      F.getFnAttribute(ManagedConvAttr).getValueAsString());
  if (!Conv) {
    consumeError(Conv.takeError());
    return false;
  }
  const FunctionType *FTy = F.getFunctionType();
  if (Conv->Params.size() != FTy->getNumParams())
    return false;
  AttributeList Decl = F.getAttributes();
  auto Differs = [&](bool IsResult, ManagedConv C, const Type *T,
                     AttributeSet Own, AttributeSet Declared) {
    detail::ManagedCallExtension CallExt = detail::getManagedCallExtension(Own);
    return CallExt != detail::ManagedCallExtension::None &&
           detail::changesManagedCallStub(
               IsResult, C, T, CallExt,
               detail::getManagedCallExtension(Declared));
  };
  return any_of(F.uses(), [&](const Use &U) {
    if (!isDirectManagedCall(U, F))
      return false;
    AttributeList Own = cast<CallBase>(U.getUser())->getAttributes();
    if (Differs(/*IsResult=*/true, Conv->Result, FTy->getReturnType(),
                Own.getRetAttrs(), Decl.getRetAttrs()))
      return true;
    for (unsigned I = 0, E = FTy->getNumParams(); I != E; ++I)
      if (Differs(/*IsResult=*/false, Conv->Params[I], FTy->getParamType(I),
                  Own.getParamAttrs(I), Decl.getParamAttrs(I)))
        return true;
    return false;
  });
}

//===----------------------------------------------------------------------===//
// The holders of a managed-call declaration's other uses (T-M10)
//===----------------------------------------------------------------------===//

/// T-M10's text (plan 5.20.3) for the managed-call function \p Symbol, which
/// is getManagedCallSymbol of the declaration's IR name. Slot P0 prints it
/// for each holder that collectManagedCallHolders finds, in its diagnostic
/// of a holding function and after getManagedCallGlobalHolderName and ": "
/// for a global value; msil-ld prints it after the bitcode input's name,
/// ": ", getManagedCallFunctionHolderName or getManagedCallGlobalHolderName
/// and ": ".
inline std::string getManagedCallAddressText(StringRef Symbol) {
  return ("taking the address of the managed-call function '" + Symbol +
          "' is not supported for MSIL yet")
      .str();
}

namespace detail {
/// Whether \p GV is llvm.used or llvm.compiler.used, whose listing of a
/// global value is not an address that the program takes.
inline bool isManagedCallUsedList(const GlobalValue &GV) {
  const auto *Var = dyn_cast<GlobalVariable>(&GV);
  return Var && (Var->getName() == "llvm.used" ||
                 Var->getName() == "llvm.compiler.used");
}

/// collectManagedCallHolders's walk over the uses of \p V, the declaration
/// \p F or a constant that holds it. \p ListsF is true when \p V is an array
/// that has \p F, or a pointer cast of \p F, as an element: such an array is
/// llvm.used's or llvm.compiler.used's initializer, whose element slot P0
/// removes (removeFromUsedLists) and never reports, or another global's,
/// which holds the address.
inline void
collectManagedCallHolders(const Value &V, const Function &F, bool ListsF,
                          SmallSetVector<const Instruction *, 4> &Insts,
                          SmallSetVector<const GlobalValue *, 4> &Globals) {
  for (const Use &U : V.uses()) {
    const User *Usr = U.getUser();
    if (&V == &F && isDirectManagedCall(U, F))
      continue;
    if (const auto *I = dyn_cast<Instruction>(Usr)) {
      Insts.insert(I);
    } else if (const auto *GV = dyn_cast<GlobalValue>(Usr)) {
      if (!ListsF || !isManagedCallUsedList(*GV))
        Globals.insert(GV);
    } else if (const auto *C = dyn_cast<Constant>(Usr)) {
      collectManagedCallHolders(
          *C, F, isa<ConstantArray>(C) && V.stripPointerCasts() == &F, Insts,
          Globals);
    }
  }
}

/// \p Kind, a space and \p GV's name as the IR spells an operand ('@f',
/// '@"a b"', '@0'), as the MSIL backend's diagnostics name global values
/// (MSIL::getIRName).
inline std::string getManagedCallHolderName(StringRef Kind,
                                            const GlobalValue &GV) {
  std::string Name = (Kind + " ").str();
  raw_string_ostream OS(Name);
  GV.printAsOperand(OS, /*PrintType=*/false);
  return Name;
}
} // namespace detail

/// T-M10's holders: every use of the managed-call declaration \p F, looking
/// through constants, that is neither a direct call of \p F with its own
/// function type (isDirectManagedCall) nor \p F's own element of the
/// llvm.used or llvm.compiler.used array (an element whose pointer casts
/// strip to \p F). The instruction that holds such a use goes to \p Insts;
/// the global value whose initializer, aliasee, resolver, personality,
/// prefix or prologue holds one goes to \p Globals; each once, in use-list
/// order. Slot P0 reports T-M10 once per function of \p Insts and once per
/// member of \p Globals, unless \p F got T-M17; msil-ld reports the same
/// holders in the bitcode inputs before LTO, once it has dropped the bodies
/// and initializers of their available_externally functions and variables,
/// which no output compiles.
inline void
collectManagedCallHolders(const Function &F,
                          SmallSetVector<const Instruction *, 4> &Insts,
                          SmallSetVector<const GlobalValue *, 4> &Globals) {
  detail::collectManagedCallHolders(F, F, /*ListsF=*/false, Insts, Globals);
}

/// How T-M10 names a global value of collectManagedCallHolders's \p Globals:
/// "global variable @<g>" for a global variable, "global value @<g>" for any
/// other (an alias, an ifunc, a function whose personality, prefix or
/// prologue holds the use), the name as GlobalValue::printAsOperand spells
/// it without its type.
inline std::string getManagedCallGlobalHolderName(const GlobalValue &GV) {
  return detail::getManagedCallHolderName(
      isa<GlobalVariable>(GV) ? "global variable" : "global value", GV);
}

/// How msil-ld's T-M10 names a function whose instructions hold a use
/// (collectManagedCallHolders's \p Insts): "function @<f>", the name spelled
/// as getManagedCallGlobalHolderName spells it, as the MSIL backend's
/// 'function @<f>: ' errors spell it.
inline std::string getManagedCallFunctionHolderName(const Function &F) {
  return detail::getManagedCallHolderName("function", F);
}

} // namespace llvm::cli

#endif // LLVM_CLI_MANAGEDCALLIR_H
