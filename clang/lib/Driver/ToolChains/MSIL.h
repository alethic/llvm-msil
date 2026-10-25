//===--- MSIL.h - MSIL ToolChain Implementations ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An MSIL target is "this native platform, but CIL instead of machine code"
// (llvm/docs/MSIL/Design.md section 3), so the MSIL toolchain of a
// '<native triple>-msil' triple is the native toolchain (Linux, MSVC or
// MinGW) for everything that shapes the compile: header search, sysroot,
// effective triple and cc1 options. It differs where the output does: msil-ld
// links, there is no external assembler, no PIC and no unwind tables, -c
// writes CIL objects, and compile-and-link compiles to LLVM bitcode until
// msil-ld links CIL objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MSIL_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MSIL_H

#include "Linux.h"
#include "MSVC.h"
#include "MinGW.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace tools {
namespace msil {

/// msil-ld, the lld flavor that links LLVM bitcode (and, later, CIL objects)
/// into a .NET assembly.
class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  explicit Linker(const ToolChain &TC) : Tool("msil::Linker", "msil-ld", TC) {}
  bool isLinkJob() const override { return true; }
  bool hasIntegratedCPP() const override { return false; }
  std::string getLinkerPath(const llvm::opt::ArgList &Args) const;
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // end namespace msil
} // end namespace tools

namespace toolchains {

/// The toolchain of an MSIL triple: \p NativeTC, the toolchain of its native
/// triple, with msil-ld as the linker.
template <class NativeTC>
class LLVM_LIBRARY_VISIBILITY MSILToolChain final : public NativeTC {
public:
  MSILToolChain(const Driver &D, const llvm::Triple &Triple,
                const llvm::opt::ArgList &Args);

  const char *getDefaultLinker() const override { return "msil-ld"; }
  bool HasNativeLLVMSupport() const override { return true; }
  // The backend's relocation model is static whatever the driver passes
  // (decision D8), and an assembly has no PIC, PIE or native unwind tables.
  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return false; }
  ToolChain::UnwindTableLevel
  getDefaultUnwindTableLevel(const llvm::opt::ArgList &Args) const override {
    return ToolChain::UnwindTableLevel::None;
  }
  SanitizerMask
  getSupportedSanitizers(BoundArch BA,
                         Action::OffloadKind DeviceOffloadKind) const override;
  // The helpers of Design.md 6 are a compiler-rt style archive in the
  // per-target runtime directory of the MSIL triple, never a native libgcc.
  ToolChain::RuntimeLibType GetDefaultRuntimeLibType() const override {
    return ToolChain::RLT_CompilerRT;
  }
  /// Compile-and-link compiles to LLVM bitcode until msil-ld links CIL
  /// objects (M20, which removes this override): without -flto, -flto= or
  /// -fno-lto, every compile is a full-LTO compile, except -c (clang-cl /c),
  /// which writes a CIL object, -S and -emit-llvm, which keep their non-LTO
  /// output, and -via-file-asm, /FA and /Fa, which keep the assemble step that
  /// rejects ILAsm text.
  LTOKind
  getLTOMode(const llvm::opt::ArgList &Args,
             Action::OffloadKind Kind = Action::OFK_None) const override;

protected:
  Tool *buildLinker() const override;
  Tool *buildAssembler() const override;
  Tool *buildStaticLibTool() const override;
};

extern template class MSILToolChain<Linux>;
extern template class MSILToolChain<MSVCToolChain>;
extern template class MSILToolChain<MinGW>;

/// The MSIL toolchain for an MSIL triple, never null. A triple that no native
/// toolchain fits (decision D6 rejects it) gets a toolchain whose link,
/// assemble and static-library tools report the triple as unknown; the
/// frontend rejects its compiles.
std::unique_ptr<ToolChain> createMSILToolChain(const Driver &D,
                                               const llvm::Triple &Triple,
                                               const llvm::opt::ArgList &Args);

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MSIL_H
