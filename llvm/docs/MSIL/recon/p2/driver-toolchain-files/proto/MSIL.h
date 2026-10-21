//===--- MSIL.h - MSIL ToolChain Implementations ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PROTOTYPE (recon driver-toolchain, scratch only). An MSIL target is "this
// native platform, but CIL instead of machine code" (Design.md 3), so the MSIL
// toolchain is the native toolchain (Linux, MSVC or MinGW) for everything that
// shapes the compile (header search, sysroot, effective triple, cc1 options),
// with msil-ld as the linker and no external assembler.
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

template <class NativeTC>
class LLVM_LIBRARY_VISIBILITY MSILToolChain final : public NativeTC {
public:
  MSILToolChain(const Driver &D, const llvm::Triple &Triple,
                const llvm::opt::ArgList &Args);

  const char *getDefaultLinker() const override { return "msil-ld"; }
  bool HasNativeLLVMSupport() const override { return true; }
  bool IsIntegratedAssemblerDefault() const override { return true; }
  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return false; }
  ToolChain::UnwindTableLevel
  getDefaultUnwindTableLevel(const llvm::opt::ArgList &) const override {
    return ToolChain::UnwindTableLevel::None;
  }
  SanitizerMask
  getSupportedSanitizers(BoundArch BA,
                         Action::OffloadKind DeviceOffloadKind) const override;

protected:
  Tool *buildLinker() const override;
  Tool *buildAssembler() const override;
  Tool *buildStaticLibTool() const override;
};

extern template class MSILToolChain<Linux>;
extern template class MSILToolChain<MSVCToolChain>;
extern template class MSILToolChain<MinGW>;

/// Creates the toolchain for an MSIL triple, or returns null (after a
/// diagnostic) for an MSIL triple whose native platform has no MSIL toolchain.
std::unique_ptr<ToolChain> createMSILToolChain(const Driver &D,
                                               const llvm::Triple &Triple,
                                               const llvm::opt::ArgList &Args);

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MSIL_H
