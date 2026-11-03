//===--- MSIL.h - Declare MSIL target feature support -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares MSILTargetInfo, the TargetInfo of the MSIL object format,
// and MSILPortableTargetInfo, the TargetInfo of the portable MSIL triples.
//
// A "<native triple>-msil" triple, such as x86_64-unknown-linux-gnu-msil,
// names a native platform and its C ABI, but asks for CIL (a .NET assembly)
// instead of machine code. MSILTargetInfo therefore wraps the native
// TargetInfo and keeps its data model, struct layout, va_list, target features
// and predefined macros, with these changes:
//  * __MSIL__ is predefined,
//  * __ELF__ is predefined where the native object format is ELF (the Linux
//    triples), as in a native build: clang defines it only for the ELF object
//    format, and msil-ld links these triples with ELF's rules,
//  * no atomic wider than 8 bytes is inline or lock-free, because the CLR has
//    no 16-byte atomic operation (such atomics take the lock path):
//    MaxAtomicInlineWidth is capped at 64 bits, while MaxAtomicPromoteWidth
//    stays native, so the size and alignment of _Atomic types do not change,
//  * for the same reason __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16 is not predefined,
//    although the native AArch64 TargetInfo defines it always and the x86-64
//    one with cx16,
//  * long double is IEEE double with double's size and alignment, because CIL
//    has no extended-precision floating-point type, and
//  * global register variables are rejected, because CIL has no machine
//    registers (CodeGen would access them through llvm.read_register and
//    llvm.write_register).
// Layout limits that the native TargetInfo derives from the object format are
// recomputed from the native triple, so they stay the native ones.
//
// Inline assembly is rejected in CodeGen, not here, so that unused static
// inline functions in system headers that contain asm statements still
// compile. Local register variables need no check of their own: they matter
// only as asm operands.
//
// The portable triples msil64-unknown-dotnet and msil32-unknown-dotnet name
// no native platform: their assemblies run on any .NET runtime with the
// pointer width of the architecture. MSILPortableTargetInfo therefore derives
// from TargetInfo itself, not from a native TargetInfo, so that no host data
// model, macro or ABI leaks in. Its data model is LP64 on msil64 and ILP32 on
// msil32, with WebAssembly's type spellings, which give both widths the same
// C++ mangling: size_t, ptrdiff_t and intptr_t are unsigned long and long, and
// int64_t and intmax_t are long long. char is signed, wchar_t and wint_t are
// int, long double is IEEE double, and va_list is char *. It predefines
// __MSIL__, __msil64__ or __msil32__ and __dotnet__, and no macro of a host
// operating system, architecture or object format (no __ELF__: the object
// format is MSIL). An asm statement with an operand is a Sema error here:
// validateInputSize and validateOutputSize refuse every operand, whatever its
// constraint, because the generic constraint checks accept 'r', 'm', 'g' and
// the like without asking the target. An asm statement without operands and
// the 'memory' clobber are untouched; CodeGen accepts only an empty asm
// statement without operands, as on every MSIL triple.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_MSIL_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_MSIL_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>

namespace clang {
namespace targets {

// The native TargetInfo of an MSIL triple, with long double mapped to double,
// inline atomics capped at 8 bytes, and __MSIL__ (on ELF platforms also
// __ELF__) predefined.
template <typename Native>
class LLVM_LIBRARY_VISIBILITY MSILTargetInfo final : public Native {
  // The same fields TargetInfo::adjust() sets for -mlong-double-64.
  void useDoubleForLongDouble() {
    this->LongDoubleWidth = this->DoubleWidth;
    this->LongDoubleAlign = this->DoubleAlign;
    this->LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  }

public:
  MSILTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : Native(Triple, Opts) {
    assert(Triple.isOSBinFormatMSIL() && "not an MSIL triple");
    // Set this before handleTargetFeatures() runs: X86TargetInfo disables
    // long double under -mno-x87 only while its format is x87DoubleExtended.
    useDoubleForLongDouble();
    // X86TargetInfo raises the vector and TLS alignment limits to 8192 bytes
    // for Windows COFF only, a test the msil format fails. Apply the same
    // test to the native triple. AArch64's limits do not depend on the
    // format.
    const llvm::Triple NativeTriple = Triple.getNativeFormatTriple();
    if (NativeTriple.isX86() && NativeTriple.isOSWindows() &&
        NativeTriple.isOSBinFormatCOFF())
      this->MaxVectorAlign = this->MaxTLSAlign = 8192u * this->getCharWidth();
  }

  // CIL has no machine registers to bind a global register variable to.
  bool validateGlobalRegisterVariable(StringRef RegName, unsigned RegSize,
                                      bool &HasSizeMismatch) const override {
    HasSizeMismatch = false;
    return false;
  }

  // The CLR has no 16-byte atomic operation, so 16-byte atomics are never
  // inline (aarch64, and x86-64 with cx16, claim 128 bits). The promote width
  // stays native: it, not the inline width, sets the size and alignment of
  // _Atomic types.
  void setMaxAtomicWidth() override {
    Native::setMaxAtomicWidth();
    if (this->MaxAtomicInlineWidth > 64)
      this->MaxAtomicInlineWidth = 64;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    Native::getTargetDefines(Opts, Builder);
    Builder.defineMacro("__MSIL__");
    // InitializePredefinedMacros defines __ELF__ only for the ELF object
    // format, which no MSIL triple has; define it as the native build does.
    if (this->getTriple().getNativeFormatTriple().isOSBinFormatELF())
      Builder.defineMacro("__ELF__");
    // The native AArch64 TargetInfo defines this always and the x86-64 one
    // with cx16, whatever the inline width: undefine it to match the cap in
    // setMaxAtomicWidth().
    Builder.undefineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16");
  }

  void adjust(DiagnosticsEngine &Diags, LangOptions &Opts,
              const TargetInfo *Aux) override {
    Native::adjust(Diags, Opts, Aux);
    // TargetInfo::adjust() has just applied -mlong-double-80 or
    // -mlong-double-128, which CIL cannot honor: diagnose and restore double.
    if (Opts.LongDoubleSize && Opts.LongDoubleSize != this->DoubleWidth)
      Diags.Report(diag::err_opt_not_valid_on_target)
          << ("-mlong-double-" + llvm::Twine(Opts.LongDoubleSize)).str();
    useDoubleForLongDouble();
  }
};

// The TargetInfo of the portable triples msil64-unknown-dotnet (LP64) and
// msil32-unknown-dotnet (ILP32).
class LLVM_LIBRARY_VISIBILITY MSILPortableTargetInfo final : public TargetInfo {
  // The same fields TargetInfo::adjust() sets for -mlong-double-64.
  void useDoubleForLongDouble() {
    LongDoubleWidth = DoubleWidth;
    LongDoubleAlign = DoubleAlign;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  }

public:
  MSILPortableTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    assert(Triple.isMSIL() && Triple.isOSDotNet() &&
           "not a portable MSIL triple");
    if (Triple.getArch() == llvm::Triple::msil64) {
      PointerWidth = PointerAlign = 64;
      LongWidth = LongAlign = 64;
      // __BIGGEST_ALIGNMENT__ and the default operator new alignment are 16.
      SuitableAlign = NewAlign = 128;
      // A _BitInt wider than 64 bits is aligned as __int128 and as the data
      // layout aligns its IR integer type (i128:128), so that va_arg and
      // ExpandVariadics, which sees only the IR type, give it the same slot.
      BitIntMaxAlign = 128;
    } else {
      // ILP32 keeps TargetInfo's widths; both alignments are 8.
      SuitableAlign = NewAlign = 64;
    }
    // WebAssembly's spellings, the same on both widths, so that the C++
    // mangling of size_t and int64_t does not depend on the width.
    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    IntPtrType = SignedLong;
    IntMaxType = SignedLongLong;
    Int64Type = SignedLongLong;
    // wchar_t is a signed 32-bit int. wint_t keeps TargetInfo's default, int,
    // as on WebAssembly: __WINT_TYPE__ is int and __WINT_UNSIGNED__ is not
    // defined.
    WCharType = SignedInt;
    // CIL has no extended-precision floating-point type.
    useDoubleForLongDouble();
    // _Float16 and __bf16 convert through clang_rt.builtins, as on the host
    // MSIL triples.
    HasFloat16 = true;
    HasBFloat16 = true;
    BFloat16Width = BFloat16Align = 16;
    BFloat16Format = &llvm::APFloat::BFloat();
    // The CLR's Interlocked operations cover 4 and 8 bytes in 32-bit and
    // 64-bit processes alike, and nothing wider.
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 64;
    // CIL accesses unaligned memory through the unaligned. prefix.
    HasUnalignedAccess = true;
    resetDataLayout();
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    Builder.defineMacro("__MSIL__");
    Builder.defineMacro(getTriple().getArch() == llvm::Triple::msil64
                            ? "__msil64__"
                            : "__msil32__");
    Builder.defineMacro("__dotnet__");
    // Lock-free up to 8 bytes (MaxAtomicInlineWidth), never 16.
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
  }

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return CharPtrBuiltinVaList;
  }

  // The only CPU is generic, and there are no target features.
  bool isValidCPUName(StringRef Name) const override {
    return Name == "generic";
  }
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override {
    Values.push_back("generic");
  }
  bool setCPU(StringRef Name) override { return isValidCPUName(Name); }

  // CIL has no machine registers: no asm operand is valid, and global
  // register variables are rejected. TargetInfo::validateInputConstraint and
  // validateOutputConstraint accept the generic constraints ('r', 'm', 'g',
  // 'i', 'X', ...) without consulting validateAsmConstraint, so the refusal
  // lives in validateInputSize and validateOutputSize, which Sema calls for
  // every non-dependent operand: each one is an error ("invalid input size
  // for constraint 'r'"). Operand-free asm statements are untouched.
  ArrayRef<const char *> getGCCRegNames() const override { return {}; }
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return {};
  }
  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    return false;
  }
  bool validateInputSize(const llvm::StringMap<bool> &FeatureMap,
                         StringRef Constraint, unsigned Size) const override {
    return false;
  }
  bool validateOutputSize(const llvm::StringMap<bool> &FeatureMap,
                          StringRef Constraint, unsigned Size) const override {
    return false;
  }
  std::string_view getClobbers() const override { return ""; }
  bool validateGlobalRegisterVariable(StringRef RegName, unsigned RegSize,
                                      bool &HasSizeMismatch) const override {
    HasSizeMismatch = false;
    return false;
  }

  // __int128 follows TargetInfo's rule (msil64 only), and with it the widest
  // _BitInt passed directly: 128 bits on msil64, 64 on msil32. A wider
  // _BitInt, up to LLVM's widest integer type as on x86, is passed
  // indirectly (DefaultABIInfo's byval) and returned through sret.
  bool hasBitIntType() const override { return true; }
  size_t getMaxBitIntWidth() const override {
    return llvm::IntegerType::MAX_INT_BITS;
  }

  // long long, not long, is the 64-bit type on both widths (int64_t,
  // int_least64_t and int_fast64_t).
  IntType getIntTypeByWidth(unsigned BitWidth, bool IsSigned) const override {
    return BitWidth == 64 ? (IsSigned ? SignedLongLong : UnsignedLongLong)
                          : TargetInfo::getIntTypeByWidth(BitWidth, IsSigned);
  }
  IntType getLeastIntTypeByWidth(unsigned BitWidth,
                                 bool IsSigned) const override {
    return BitWidth == 64
               ? (IsSigned ? SignedLongLong : UnsignedLongLong)
               : TargetInfo::getLeastIntTypeByWidth(BitWidth, IsSigned);
  }

  // Only CC_C is supported: TargetInfo's checkCallingConvention warns about
  // every other convention, which is then ignored.

  void adjust(DiagnosticsEngine &Diags, LangOptions &Opts,
              const TargetInfo *Aux) override {
    TargetInfo::adjust(Diags, Opts, Aux);
    // TargetInfo::adjust() has just applied -mlong-double-80 or
    // -mlong-double-128, which CIL cannot honor: diagnose and restore double.
    if (Opts.LongDoubleSize && Opts.LongDoubleSize != DoubleWidth)
      Diags.Report(diag::err_opt_not_valid_on_target)
          << ("-mlong-double-" + llvm::Twine(Opts.LongDoubleSize)).str();
    useDoubleForLongDouble();
    // wchar_t is a signed 32-bit int, which the portable C library's wide
    // character functions assume: diagnose -fshort-wchar and the cc1 options
    // that change the type, and restore int.
    if (WCharType != SignedInt) {
      const char *Option = Opts.WCharSize == 2   ? "-fshort-wchar"
                           : Opts.WCharSize == 1 ? "-fwchar-type=char"
                                                 : "-fno-signed-wchar";
      Diags.Report(diag::err_opt_not_valid_on_target) << Option;
      WCharType = SignedInt;
      Opts.WCharSize = 4;
      Opts.WCharIsSigned = true;
    }
  }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_MSIL_H
