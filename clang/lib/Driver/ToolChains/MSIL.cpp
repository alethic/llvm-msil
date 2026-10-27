//===--- MSIL.cpp - MSIL ToolChain Implementations --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "Gnu.h"
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
  // Get -fuse-ld= first to prevent -Wunused-command-line-argument, as
  // ToolChain::GetLinkerPath does.
  const Arg *UseLd = Args.getLastArg(options::OPT_fuse_ld_EQ);
  // --ld-path= takes precedence over -fuse-ld= and names the executable, as
  // for every other toolchain.
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
    return TC.GetProgramPath(TC.getDefaultLinker());
  }
  // msil-ld is the only linker of MSIL bitcode and CIL objects. -fuse-ld=lld
  // and -fuse-ld=ld name it (as -fuse-ld=lld names wasm-ld for WebAssembly),
  // an absolute path is taken as is, and anything else is an error.
  // CLANG_DEFAULT_LINKER names a native linker and is ignored. The program is
  // always the plain msil-ld: lld picks its flavor from the program name, and
  // would pick the GNU flavor for a name such as '<triple>-gnu-msil-ld'.
  if (UseLd) {
    StringRef UseLinker = UseLd->getValue();
    if (!UseLinker.empty()) {
      if (llvm::sys::path::is_absolute(UseLinker) &&
          llvm::sys::fs::can_execute(UseLinker))
        return std::string(UseLinker);
      if (UseLinker != "lld" && UseLinker != "ld" && UseLinker != "msil-ld")
        TC.getDriver().Diag(diag::err_drv_invalid_linker_name)
            << UseLd->getAsString(Args);
    }
  }
  return TC.GetProgramPath(TC.getDefaultLinker());
}

/// msil-ld is a GNU-style lld flavor and does not read %LIB%, so pass the
/// directories link.exe would search: the %LIB% entries of a developer prompt,
/// or, with a /winsysroot-style option or without LIB, the VC, UCRT and
/// Windows SDK library directories that the MSVC toolchain detected.
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

/// The C library of the triple (Design.md 6) and the system libraries that
/// native programs get implicitly (plan 5.10.4). msil-ld has no default
/// libraries of its own, and it turns only referenced symbols into P/Invokes,
/// so an unused default library costs nothing. -nolibc leaves out the C
/// library and the system libraries that go with it on the Linux and MinGW
/// triples, as their native link jobs do; native windows-msvc ignores it.
/// -pthread adds -lpthread on the Linux and MinGW triples, -nolibc or not (on
/// MinGW after the C runtime), as natively; windows-msvc leaves it unread, as
/// the native MSVC link job does.
static void addDefaultLibs(const ToolChain &TC, const ArgList &Args,
                           ArgStringList &CmdArgs) {
  const llvm::Triple &T = TC.getTriple();
  if (T.isOSLinux()) {
    if (Args.hasArg(options::OPT_pthread) || Args.hasArg(options::OPT_pthreads))
      CmdArgs.push_back("-lpthread");
    if (Args.hasArg(options::OPT_nolibc))
      return;
    CmdArgs.push_back("-lc");
    // glibc keeps libm separate, and the backend itself calls it for
    // operations that are instructions natively (sqrt, fma, floor, ...).
    if (!T.isMusl())
      CmdArgs.push_back("-lm");
    return;
  }
  if (T.isWindowsMSVCEnvironment()) {
    // ucrt.lib names the api-ms-win-crt-*.dll sets; native programs get
    // kernel32 and oldnames from the CRT's /DEFAULTLIB directives. The
    // memory and string functions that native programs take from vcruntime
    // (memcpy, memset, strchr, ...) are not in ucrt.lib: --ucrtbase-imports,
    // an import library built into msil-ld, binds them to ucrtbase.dll, which
    // exports all of them. It comes last, so a user -lvcruntime wins.
    for (const char *Lib :
         {"-lucrt", "-lkernel32", "-loldnames", "--ucrtbase-imports"})
      CmdArgs.push_back(Lib);
    return;
  }
  if (T.isWindowsGNUEnvironment()) {
    // The native MinGW link job reads only -pthread (not -pthreads), and puts
    // winpthreads after the C runtime, -nolibc or not.
    bool PThread = Args.hasArg(options::OPT_pthread);
    if (Args.hasArg(options::OPT_nolibc)) {
      if (PThread)
        CmdArgs.push_back("-lpthread");
      return;
    }
    // As the MinGW toolchain does: a C runtime that the user names takes the
    // place of msvcrt, and is repeated there.
    const char *CRT = "-lmsvcrt";
    for (const std::string &Lib : Args.getAllArgValues(options::OPT_l)) {
      StringRef L(Lib);
      if (L.starts_with("msvcr") || L.starts_with("ucrt") ||
          L.starts_with("crtdll")) {
        CRT = Args.MakeArgString("-l" + L);
        break;
      }
    }
    CmdArgs.push_back(CRT);
    if (PThread)
      CmdArgs.push_back("-lpthread");
    for (const char *Lib :
         {"-ladvapi32", "-lshell32", "-luser32", "-lkernel32"})
      CmdArgs.push_back(Lib);
  }
}

/// With LTO the code generator runs inside msil-ld, so the options that steer
/// it go there, in lld's spellings. tools::addLTOOptions is not used: it would
/// load the gold plugin and pass -plugin-opt= options.
static void addMSILLTOOptions(const ToolChain &TC, const ArgList &Args,
                              ArgStringList &CmdArgs) {
  const Driver &D = TC.getDriver();
  // Without -O, msil-ld's default (--lto-O2) applies.
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
  if (D.isSaveTempsEnabled())
    CmdArgs.push_back("--save-temps");
  // Backend options must reach the code generator, which runs at link time,
  // as HIP forwards them to its device linker. A debugging aid: codegen
  // switches travel as module flags.
  for (const Arg *A : Args.filtered(options::OPT_mllvm)) {
    A->claim();
    CmdArgs.push_back(Args.MakeArgString("--mllvm=" + Twine(A->getValue(0))));
  }
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

  // msil-ld accepts inputs whose native triples match, so the codegen triple
  // is stated (normalized, without the MSVC version).
  CmdArgs.push_back(Args.MakeArgString("--target=" + Triple.str()));
  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  // A library assembly: a DLL image with no entry point. clang-cl's /LD and
  // /LDd ask for a DLL as -shared does; the output keeps clang-cl's name
  // (/LD foo.c writes foo.dll).
  if (Args.hasArg(options::OPT_shared, options::OPT__SLASH_LD,
                  options::OPT__SLASH_LDd))
    CmdArgs.push_back("--shared");
  // No MSIL triple has a static C library.
  if (const Arg *A = Args.getLastArg(options::OPT_static))
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getAsString(Args) << Triple.str();
  // An assembly has no PIC, no PIE and no dynamic symbol table.
  Args.ClaimAllArgs(options::OPT_pie);
  Args.ClaimAllArgs(options::OPT_no_pie);
  Args.ClaimAllArgs(options::OPT_rdynamic);
  // There are no crt files and no libgcc; the native link jobs that consume
  // these options accept them here too.
  Args.ClaimAllArgs(options::OPT_nostartfiles);
  if (Triple.isOSLinux() || Triple.isWindowsGNUEnvironment())
    Args.ClaimAllArgs(options::OPT_static_libgcc);
  if (Triple.isOSLinux())
    Args.ClaimAllArgs(options::OPT_shared_libgcc);
  // The native MinGW link job reads -nolibc whatever -nostdlib and
  // -nodefaultlibs say; addDefaultLibs reads it on the Linux triples.
  if (Triple.isWindowsGNUEnvironment())
    Args.ClaimAllArgs(options::OPT_nolibc);
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
    // The helpers compiled for this MSIL triple, linked statically as native
    // programs link the compiler-rt builtins (Design.md 6), once they exist.
    std::string Builtins = TC.getCompilerRT(Args, "builtins");
    if (TC.getVFS().exists(Builtins))
      CmdArgs.push_back(Args.MakeArgString(Builtins));
  }

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  // lld reads response files as UTF-8.
  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileUTF8(),
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
  // layout (CFI jump tables, shadow stacks, KCFI type ids); UBSan runs in trap
  // mode.
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
  // llvm-ar on every MSIL triple; the native MSVC toolchain has none.
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

namespace {
/// The toolchain of an MSIL triple that decision D6 rejects (Darwin, FreeBSD,
/// Cygwin, windows-itanium, ...). Compiles reach the frontend, which rejects
/// the triple; the link, assemble and static-library tools report it, so that
/// no native tool of the triple is ever planned, link-only invocations
/// included.
class UnsupportedMSILToolChain final : public ToolChain {
public:
  UnsupportedMSILToolChain(const Driver &D, const llvm::Triple &Triple,
                           const ArgList &Args)
      : ToolChain(D, Triple, Args) {}

  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const ArgList &Args) const override { return false; }
  bool isPICDefaultForced() const override { return false; }

protected:
  Tool *buildLinker() const override { return reportUnknownTriple(); }
  Tool *buildAssembler() const override { return reportUnknownTriple(); }
  Tool *buildStaticLibTool() const override { return reportUnknownTriple(); }

private:
  Tool *reportUnknownTriple() const {
    getDriver().Diag(diag::err_target_unknown_triple) << getTripleString();
    return nullptr;
  }
};
} // namespace

std::unique_ptr<ToolChain>
toolchains::createMSILToolChain(const Driver &D, const llvm::Triple &Triple,
                                const ArgList &Args) {
  if (Triple.isOSLinux())
    return std::make_unique<MSILToolChain<Linux>>(D, Triple, Args);
  if (Triple.isWindowsMSVCEnvironment())
    return std::make_unique<MSILToolChain<MSVCToolChain>>(D, Triple, Args);
  if (Triple.isWindowsGNUEnvironment())
    return std::make_unique<MSILToolChain<MinGW>>(D, Triple, Args);
  return std::make_unique<UnsupportedMSILToolChain>(D, Triple, Args);
}
