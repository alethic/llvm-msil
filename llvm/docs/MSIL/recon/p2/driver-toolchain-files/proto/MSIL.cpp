//===--- MSIL.cpp - MSIL ToolChain Implementations --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PROTOTYPE (recon driver-toolchain, scratch only).
//
//===----------------------------------------------------------------------===//

#include "ToolChains/MSIL.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

std::string msil::Linker::getLinkerPath(const ArgList &Args) const {
  const ToolChain &TC = getToolChain();
  // --ld-path= names the executable, as for every other toolchain.
  if (const Arg *A = Args.getLastArg(options::OPT_ld_path_EQ)) {
    std::string Path(A->getValue());
    if (!Path.empty()) {
      if (llvm::sys::path::parent_path(Path).empty())
        Path = TC.GetProgramPath(A->getValue());
      if (llvm::sys::fs::can_execute(Path))
        return Path;
    }
    TC.getDriver().Diag(diag::err_drv_invalid_linker_name)
        << A->getAsString(Args);
  }
  // msil-ld is the only linker that can link CIL objects and MSIL bitcode.
  // -fuse-ld=lld and -fuse-ld=ld name it (as -fuse-ld=lld names wasm-ld for
  // WebAssembly); an absolute path is taken as is; anything else is an error.
  if (const Arg *A = Args.getLastArg(options::OPT_fuse_ld_EQ)) {
    StringRef UseLinker = A->getValue();
    if (!UseLinker.empty()) {
      if (llvm::sys::path::is_absolute(UseLinker) &&
          llvm::sys::fs::can_execute(UseLinker))
        return std::string(UseLinker);
      if (UseLinker != "lld" && UseLinker != "ld" && UseLinker != "msil-ld")
        TC.getDriver().Diag(diag::err_drv_invalid_linker_name)
            << A->getAsString(Args);
    }
  }
  return TC.GetProgramPath(TC.getDefaultLinker());
}

/// msil-ld is a GNU-style lld flavor and does not read %LIB%, so pass the
/// directories link.exe would search: %LIB% from a developer prompt, or the
/// VC, UCRT and Windows SDK library directories the MSVC toolchain detected.
static void addMSVCLibraryPaths(const MSVCToolChain &TC, const ArgList &Args,
                                ArgStringList &CmdArgs) {
  bool Explicit = Args.hasArg(
      options::OPT__SLASH_vctoolsdir, options::OPT__SLASH_vctoolsversion,
      options::OPT__SLASH_winsysroot, options::OPT__SLASH_winsdkdir,
      options::OPT__SLASH_winsdkversion);
  if (!Explicit) {
    if (std::optional<std::string> Lib = llvm::sys::Process::GetEnv("LIB")) {
      SmallVector<StringRef, 8> Dirs;
      StringRef(*Lib).split(Dirs, ";", /*MaxSplit=*/-1, /*KeepEmpty=*/false);
      if (!Dirs.empty()) {
        for (StringRef Dir : Dirs)
          CmdArgs.push_back(Args.MakeArgString("-L" + Dir));
        return;
      }
    }
  }
  if (TC.FoundMSVCInstall())
    CmdArgs.push_back(Args.MakeArgString(
        "-L" + TC.getSubDirectoryPath(llvm::SubDirectoryType::Lib)));
  std::string Path;
  if (TC.useUniversalCRT() && TC.getUniversalCRTLibraryPath(Args, Path))
    CmdArgs.push_back(Args.MakeArgString("-L" + Path));
  if (TC.getWindowsSDKLibraryPath(Args, Path))
    CmdArgs.push_back(Args.MakeArgString("-L" + Path));
}

/// The C library of the triple (Design.md 6) and, where native programs get
/// them implicitly, the system libraries. msil-ld turns only referenced symbols
/// into P/Invokes, so an unused default library costs nothing.
static void addDefaultLibs(const ToolChain &TC, const ArgList &Args,
                           ArgStringList &CmdArgs) {
  const llvm::Triple &T = TC.getTriple();
  if (T.isOSLinux()) {
    if (Args.hasArg(options::OPT_pthread))
      CmdArgs.push_back("-lpthread");
    CmdArgs.push_back("-lc");
    // glibc keeps libm separate, and the backend itself calls it for
    // operations that are instructions natively (sqrt, fma, floor, ...).
    if (!T.isMusl())
      CmdArgs.push_back("-lm");
    return;
  }
  if (T.isWindowsMSVCEnvironment()) {
    CmdArgs.push_back("-lucrt");     // ucrt.lib: api-ms-win-crt-*.dll
    CmdArgs.push_back("-lkernel32"); // native programs get it from the CRT
    return;
  }
  if (T.isWindowsGNUEnvironment()) {
    // As the MinGW toolchain does: the user's C runtime choice wins.
    bool UserCRT = false;
    for (const std::string &Lib : Args.getAllArgValues(options::OPT_l)) {
      StringRef L(Lib);
      UserCRT |= L.starts_with("msvcr") || L.starts_with("ucrt") ||
                 L.starts_with("crtdll");
    }
    if (!UserCRT)
      CmdArgs.push_back("-lmsvcrt");
    for (const char *Lib :
         {"-ladvapi32", "-lshell32", "-luser32", "-lkernel32"})
      CmdArgs.push_back(Lib);
  }
}

/// With -flto the code generator runs inside msil-ld, so the options that
/// steer it go there (lld/wasm spellings; there is no gold plugin).
static void addMSILLTOOptions(const ToolChain &TC, const ArgList &Args,
                          ArgStringList &CmdArgs) {
  const Driver &D = TC.getDriver();
  if (Args.getLastArg(options::OPT_O_Group)) {
    unsigned OptLevel = getOptimizationLevel(Args, InputKind(), D.getDiags());
    CmdArgs.push_back(Args.MakeArgString("--lto-O" + Twine(OptLevel)));
  }
  StringRef Parallelism = getLTOParallelism(Args, D);
  if (!Parallelism.empty())
    CmdArgs.push_back(Args.MakeArgString("--thinlto-jobs=" + Parallelism));
  if (const Arg *A = Args.getLastArg(options::OPT_flto_partitions_EQ))
    CmdArgs.push_back(
        Args.MakeArgString("--lto-partitions=" + Twine(A->getValue())));
  // Backend options (-mllvm -msil-...) must reach the code generator, which
  // runs at link time, as HIP forwards them to its device linker.
  for (const Arg *A : Args.filtered(options::OPT_mllvm))
    CmdArgs.push_back(Args.MakeArgString("--mllvm=" + Twine(A->getValue(0))));
  if (D.isSaveTempsEnabled())
    CmdArgs.push_back("--save-temps");
}

void msil::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                const InputInfo &Output,
                                const InputInfoList &Inputs,
                                const ArgList &Args,
                                const char *LinkingOutput) const {
  const ToolChain &TC = getToolChain();
  const Driver &D = TC.getDriver();
  const llvm::Triple &Triple = TC.getTriple();
  ArgStringList CmdArgs;

  CmdArgs.push_back(Args.MakeArgString("--target=" + Triple.str()));
  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  if (Args.hasArg(options::OPT_shared))
    CmdArgs.push_back("--shared");
  // No static C library exists for any MSIL triple yet (plan P5-A).
  if (const Arg *A = Args.getLastArg(options::OPT_static))
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getAsString(Args) << Triple.str();
  // There is no PIC, PIE or dynamic symbol table in an assembly.
  Args.ClaimAllArgs(options::OPT_pie);
  Args.ClaimAllArgs(options::OPT_no_pie);
  Args.ClaimAllArgs(options::OPT_rdynamic);
  if (Args.hasArg(options::OPT_s))
    CmdArgs.push_back("--strip-all");

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_u});
  TC.AddFilePathLibArgs(Args, CmdArgs);
  if (Triple.isWindowsMSVCEnvironment())
    addMSVCLibraryPaths(static_cast<const MSVCToolChain &>(TC), Args, CmdArgs);

  if (TC.isUsingLTO(Args))
    addMSILLTOOptions(TC, Args, CmdArgs);

  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    addDefaultLibs(TC, Args, CmdArgs);
    // Helpers compiled for this MSIL triple and linked statically, as native
    // programs link compiler-rt builtins (Design.md 6), once they exist.
    std::string Builtins = TC.getCompilerRT(Args, "builtins");
    if (TC.getVFS().exists(Builtins))
      CmdArgs.push_back(Args.MakeArgString(Builtins));
  }

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileCurCP(),
      Args.MakeArgString(getLinkerPath(Args)), CmdArgs, Inputs, Output));
}

template <class NativeTC>
MSILToolChain<NativeTC>::MSILToolChain(const Driver &D,
                                       const llvm::Triple &Triple,
                                       const ArgList &Args)
    : NativeTC(D, Triple, Args) {}

template <class NativeTC>
SanitizerMask MSILToolChain<NativeTC>::getSupportedSanitizers(
    BoundArch BA, Action::OffloadKind DeviceOffloadKind) const {
  // Only checks that need neither a native runtime library nor native code
  // layout tricks (CFI jump tables, shadow stacks, KCFI type ids).
  return (SanitizerKind::Undefined & ~SanitizerKind::Vptr) |
         SanitizerKind::FloatDivideByZero |
         SanitizerKind::UnsignedIntegerOverflow |
         SanitizerKind::UnsignedShiftBase | SanitizerKind::ImplicitConversion |
         SanitizerKind::Nullability | SanitizerKind::LocalBounds;
}

template <class NativeTC> Tool *MSILToolChain<NativeTC>::buildLinker() const {
  return new tools::msil::Linker(*this);
}

template <class NativeTC>
Tool *MSILToolChain<NativeTC>::buildAssembler() const {
  // Only reached with -fno-integrated-as: there is no external CIL assembler
  // (ilasm reads its own syntax and writes assemblies, not objects).
  this->getDriver().Diag(clang::diag::err_no_external_assembler);
  return nullptr;
}

template <class NativeTC>
Tool *MSILToolChain<NativeTC>::buildStaticLibTool() const {
  return new tools::gnutools::StaticLibTool(*this);
}

namespace clang {
namespace driver {
namespace toolchains {
template class MSILToolChain<Linux>;
template class MSILToolChain<MSVCToolChain>;
template class MSILToolChain<MinGW>;
} // namespace toolchains
} // namespace driver
} // namespace clang

std::unique_ptr<ToolChain>
toolchains::createMSILToolChain(const Driver &D, const llvm::Triple &Triple,
                                const ArgList &Args) {
  if (Triple.isOSLinux())
    return std::make_unique<MSILToolChain<Linux>>(D, Triple, Args);
  if (Triple.isWindowsMSVCEnvironment())
    return std::make_unique<MSILToolChain<MSVCToolChain>>(D, Triple, Args);
  if (Triple.isWindowsGNUEnvironment())
    return std::make_unique<MSILToolChain<MinGW>>(D, Triple, Args);
  return nullptr;
}
