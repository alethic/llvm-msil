//===- Driver.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/Driver.cpp at 64f50ffd4f1c.
//
// The msil-ld driver: the "msil" flavor of lld, which links C programs into
// .NET assemblies (llvm/docs/MSIL/ImplementationPlan.md 5.9). Its inputs are
// LLVM bitcode files and archives of them, which LTO compiles for the codegen
// triple, and the native libraries that a native link of the same program
// reads (plan 5.10): undefined functions bind to them late and become
// P/Invokes. --lto-emit-asm writes the whole program as ILAsm text, and ilasm
// turns that into an assembly (msil-ld writes PE files from M20).
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Driver.h"
#include "Config.h"
#include "InputFiles.h"
#include "MapFile.h"
#include "NativeLibraries.h"
#include "SymbolTable.h"
#include "lld/Common/Args.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Filesystem.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Reproduce.h"
#include "lld/Common/Strings.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TarWriter.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Threading.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::opt;
using namespace llvm::sys;

namespace lld::msil {
Ctx ctx;

Ctx::Ctx() {}

void Ctx::reset() {
  arg.~Config();
  new (&arg) Config();
  bitcodeFiles.clear();
  lazyBitcodeFiles.clear();
  nativeLibraryFiles.clear();
  loadedFiles.clear();
  nativeIndex.clear();
  alternateNames.clear();
  appliedAlternateNames.clear();
  mustMatch.clear();
  whyExtractRecords.clear();
  sym = MSILSym{};
}

namespace {
// Create enum with OPT_xxx values for each option in Options.td
enum {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};

// This function is called on startup. We need this for LTO since
// LTO calls LLVM functions to compile bitcode files to native code.
// Technically this can be delayed until we read bitcode files, but
// we don't bother to do lazily because the initialization is fast.
static void initLLVM() {
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();
}

class LinkerDriver {
public:
  LinkerDriver(Ctx &);
  void linkerMain(ArrayRef<const char *> argsArr);

private:
  void createFiles(opt::InputArgList &args);
  void addFile(StringRef path);
  void addLibrary(StringRef name);
  void addLinkerScript(MemoryBufferRef mb, std::optional<sys::fs::UniqueID> id);
  void addInputFile(InputFile *file);
  void addPendingFiles();
  void addLinkerOptions(BitcodeFile &f);
  void addRequestedLibrary(StringRef name, std::optional<std::string> path,
                           const InputFile &f);
  void applyAlternateNames();

  Ctx &ctx;

  // True if we are in --whole-archive and --no-whole-archive.
  bool inWholeArchive = false;

  // True if we are in --start-lib and --end-lib.
  bool inLib = false;

  // The inputs in command-line order, then the libraries that the linker
  // options of loaded files request, in the order those files are loaded
  // (plan 5.9.4). addPendingFiles() loads them into the symbol table.
  std::vector<InputFile *> files;
  size_t numLoadedFiles = 0;
  // The bitcode files (ctx.bitcodeFiles) whose linker options are read.
  size_t numLinkerOptionFiles = 0;

  // The files read, so that a library that several inputs request (clang's
  // /DEFAULTLIB:oldnames.lib in every windows-msvc input) is read once, as
  // lld-link reads it.
  DenseSet<sys::fs::UniqueID> visitedFiles;

  // The GNU ld scripts whose inputs addLinkerScript() is adding (inserted
  // when it starts and erased when it ends), and for each of them, innermost
  // last, the script and the offset of the input it is adding, so that an
  // input leading back to one of them is reported as a cycle (plan 5.10.8).
  DenseSet<sys::fs::UniqueID> activeScripts;
  SmallVector<std::pair<MemoryBufferRef, size_t>, 4> scriptInputs;
};
} // anonymous namespace

bool link(ArrayRef<const char *> args, llvm::raw_ostream &stdoutOS,
          llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput) {
  // This driver-specific context will be freed later by unsafeLldMain().
  auto *context = new CommonLinkerContext;

  context->e.initialize(stdoutOS, stderrOS, exitEarly, disableOutput);
  context->e.cleanupCallback = []() {
    ctx.reset();
    tar = nullptr;
  };
  // Diagnostics name msil-ld whatever the program name: "lld -flavor msil" and
  // the triple-prefixed copies are msil-ld too.
  context->e.logName = "msil-ld";
  context->e.errorLimitExceededMsg =
      "too many errors emitted, stopping now (use "
      "--error-limit=0 to see all errors)";

  // Every link starts from fresh global state, so that LLD_IN_TEST=2 can run
  // the driver twice in one process (plan 5.9.1).
  ctx.reset();
  tar = nullptr;
  symtab = make<SymbolTable>();

  initLLVM();
  LinkerDriver(ctx).linkerMain(args);

  return errorCount() == 0;
}

#define OPTTABLE_CODE
#include "Options.inc"

namespace {
class MSILOptTable : public opt::OptTable {
public:
  MSILOptTable() : opt::OptTable(optionTables()) {}
  opt::InputArgList parse(ArrayRef<const char *> argv);
};
} // namespace

// Set color diagnostics according to -color-diagnostics={auto,always,never}
// or -no-color-diagnostics flags.
static void handleColorDiagnostics(opt::InputArgList &args) {
  auto *arg = args.getLastArg(OPT_color_diagnostics, OPT_color_diagnostics_eq,
                              OPT_no_color_diagnostics);
  if (!arg)
    return;
  auto &errs = errorHandler().errs();
  if (arg->getOption().getID() == OPT_color_diagnostics) {
    errs.enable_colors(true);
  } else if (arg->getOption().getID() == OPT_no_color_diagnostics) {
    errs.enable_colors(false);
  } else {
    StringRef s = arg->getValue();
    if (s == "always")
      errs.enable_colors(true);
    else if (s == "never")
      errs.enable_colors(false);
    else if (s != "auto")
      error("unknown option: --color-diagnostics=" + s);
  }
}

// Response files are tokenized as the host's command lines are (msil-ld has
// no --rsp-quoting), and read as UTF-8, which the clang driver writes for
// msil-ld (plan 5.10.3).
static cl::TokenizerCallback getQuotingStyle() {
  if (Triple(sys::getProcessTriple()).isOSWindows())
    return cl::TokenizeWindowsCommandLine;
  return cl::TokenizeGNUCommandLine;
}

// Find a file by concatenating given paths. If a resulting path
// starts with "=", the character is replaced with a --sysroot value
// (lld/ELF/DriverUtils.cpp; plan 5.10.9).
static std::optional<std::string> findFile(StringRef path1,
                                           const Twine &path2) {
  SmallString<128> s;
  if (path1.starts_with("="))
    path::append(s, ctx.arg.sysroot, path1.substr(1), path2);
  else
    path::append(s, path1, path2);

  if (fs::exists(s))
    return std::string(s);
  return std::nullopt;
}

opt::InputArgList MSILOptTable::parse(ArrayRef<const char *> argv) {
  SmallVector<const char *, 256> vec(argv.data(), argv.data() + argv.size());

  unsigned missingIndex;
  unsigned missingCount;

  // Expand response files (arguments in the form of @<filename>)
  // and then parse the arguments.
  cl::ExpandResponseFiles(saver(), getQuotingStyle(), vec);
  opt::InputArgList args = this->ParseArgs(vec, missingIndex, missingCount);

  handleColorDiagnostics(args);
  if (missingCount)
    error(Twine(args.getArgString(missingIndex)) + ": missing argument");

  for (auto *arg : args.filtered(OPT_UNKNOWN))
    error("unknown argument: " + arg->getAsString(args));
  return args;
}

// Returns slices of MB by parsing MB as an archive file.
// Each slice consists of a member file in the archive. A malformed archive
// (a truncated import library, say) is an error(), not lld's fatal(), so that
// the printing iteration of LLD_IN_TEST=2 still reports it (plan 5.9.1);
// it then contributes nothing.
static std::optional<std::vector<std::pair<MemoryBufferRef, uint64_t>>>
getArchiveMembers(MemoryBufferRef mb) {
  Expected<std::unique_ptr<Archive>> fileOrErr = Archive::create(mb);
  if (!fileOrErr) {
    error(mb.getBufferIdentifier() +
          ": failed to parse archive: " + toString(fileOrErr.takeError()));
    return std::nullopt;
  }
  std::unique_ptr<Archive> file = std::move(*fileOrErr);

  std::vector<std::pair<MemoryBufferRef, uint64_t>> v;
  Error err = Error::success();
  for (const Archive::Child &c : file->children(err)) {
    Expected<MemoryBufferRef> mbrefOrErr = c.getMemoryBufferRef();
    if (!mbrefOrErr) {
      error(mb.getBufferIdentifier() +
            ": could not get the buffer for a child of the archive: " +
            toString(mbrefOrErr.takeError()));
      continue;
    }
    v.push_back(std::make_pair(*mbrefOrErr, c.getChildOffset()));
  }
  if (err) {
    error(mb.getBufferIdentifier() +
          ": Archive::children failed: " + toString(std::move(err)));
    return std::nullopt;
  }

  // Take ownership of memory buffers created for members of thin archives.
  for (std::unique_ptr<MemoryBuffer> &mb : file->takeThinBuffers())
    make<std::unique_ptr<MemoryBuffer>>(std::move(mb));

  return v;
}

static bool isBitcode(MemoryBufferRef mb) {
  return identify_magic(mb.getBuffer()) == file_magic::bitcode;
}

// Returns true if \p mb is a PE image with a CLI header (ECMA-335 II.25.3.3):
// a CIL object file (M20 links them) or a .NET assembly (M21, M22). Any other
// PE image is a native DLL.
static bool hasCLIHeader(MemoryBufferRef mb) {
  if (identify_magic(mb.getBuffer()) != file_magic::pecoff_executable)
    return false;
  Expected<std::unique_ptr<COFFObjectFile>> obj = COFFObjectFile::create(mb);
  if (!obj) {
    consumeError(obj.takeError());
    return false;
  }
  const data_directory *dir =
      (*obj)->getDataDirectory(COFF::CLR_RUNTIME_HEADER);
  return dir && dir->RelativeVirtualAddress != 0;
}

static bool isBitcodeOrCILObject(MemoryBufferRef mb) {
  return isBitcode(mb) || hasCLIHeader(mb);
}

// lld/ELF/Driver.cpp's isUnderSysroot: true if \p path is inside the
// --sysroot directory. An absolute path in a linker script found there is
// resolved under the sysroot (GNU ld's rule).
static bool isUnderSysroot(StringRef path) {
  if (ctx.arg.sysroot.empty())
    return false;
  for (; !path.empty(); path = path::parent_path(path))
    if (fs::equivalent(ctx.arg.sysroot, path))
      return true;
  return false;
}

static std::optional<std::string> findFromSearchPaths(StringRef path);

// Resolves the file name \p s of the linker script \p script as lld/ELF's
// ScriptParser::addFile does (lld/ELF/ScriptParser.cpp:319-360): an absolute
// path inside the sysroot when the script is in the sysroot, an absolute path
// as it is, '=' relative to the sysroot, else the script's directory, the
// current directory and the -L directories. Returns the path, or nullopt
// with lld/ELF's text in \p msg.
static std::optional<std::string> resolveScriptPath(StringRef script,
                                                    bool underSysroot,
                                                    StringRef s,
                                                    std::string &msg) {
  if (underSysroot && s.starts_with("/")) {
    std::string path = (ctx.arg.sysroot + s).str();
    if (fs::exists(path))
      return path;
    msg = ("cannot find " + s + " inside " + ctx.arg.sysroot).str();
    return std::nullopt;
  }

  // Case 1: s is an absolute path. Just open it.
  if (s.starts_with("/") || path::is_absolute(s))
    return s.str();
  // Case 2: relative to the sysroot.
  if (s.starts_with("=")) {
    if (ctx.arg.sysroot.empty())
      return s.substr(1).str();
    return (ctx.arg.sysroot + "/" + s.substr(1)).str();
  }
  // Case 4: s is a relative path. Search in the directory of the script file.
  StringRef directory = path::parent_path(script);
  if (!directory.empty()) {
    SmallString<128> path(directory);
    path::append(path, s);
    if (fs::exists(path))
      return std::string(path);
  }
  // Then search in the current working directory.
  if (fs::exists(s))
    return s.str();
  // Finally, search in the list of library paths.
  if (std::optional<std::string> path = findFromSearchPaths(s))
    return path;
  msg = ("unable to find " + s).str();
  return std::nullopt;
}

// True for a name under which a linker script stands for a shared library
// (lib<n>.so; plan 5.10.9).
static bool isSharedLibraryName(StringRef path) {
  StringRef name = path::filename(path);
  return name.ends_with(".so") || name.contains(".so.") ||
         name.ends_with_insensitive(".dll");
}

// Returns the kind (plan 5.10.9) of the library candidate at \p path, which
// -l found, for the codegen triple (a native library of the other object
// format is OtherFormat). The candidate is read only to be classified;
// addFile() reads the one -l chooses, through readFile() (--reproduce). A
// linker script under a shared name is a native shared library; one under a
// static name has the best kind of the files it names.
static LibraryKind classifyCandidate(StringRef path, unsigned depth = 0) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> mbOrErr =
      MemoryBuffer::getFile(path, /*IsText=*/false,
                            /*RequiresNullTerminator=*/false);
  if (!mbOrErr)
    return LibraryKind::NativeShared; // reading it reports the error
  MemoryBufferRef mb = (*mbOrErr)->getMemBufferRef();
  if (identify_magic(mb.getBuffer()) != file_magic::unknown ||
      !isLinkerScript(mb))
    return classifyLibrary(mb, ctx.arg.target);
  if (isSharedLibraryName(path) || depth >= 4)
    return LibraryKind::NativeShared;

  SmallVector<LinkerScriptInput, 4> inputs;
  consumeError(readLinkerScript(mb, inputs));
  bool underSysroot = isUnderSysroot(path);
  bool hasNative = false;
  for (const LinkerScriptInput &input : inputs) {
    if (input.isLibrary)
      continue;
    std::string msg;
    std::optional<std::string> file =
        resolveScriptPath(path, underSysroot, input.name, msg);
    if (!file)
      continue;
    switch (classifyCandidate(*file, depth + 1)) {
    case LibraryKind::Static:
      return LibraryKind::Static;
    case LibraryKind::Import:
    case LibraryKind::NativeShared:
      hasNative = true;
      break;
    case LibraryKind::Managed:
    case LibraryKind::NativeOnly:
    case LibraryKind::OtherFormat:
      break;
    }
  }
  return hasNative ? LibraryKind::NativeShared : LibraryKind::NativeOnly;
}

// The codegen triple for a triple that --target or the first input names: an
// MSIL triple as it is, and a native one with -msil appended (plan 5.9.3).
static Triple getMSILTriple(StringRef triple) {
  Triple tt(triple);
  if (tt.isOSBinFormatMSIL())
    return tt;
  return Triple(triple + "-msil");
}

// Returns true if \p tt is one of D6's MSIL triples, which msil-ld links: an
// x86_64 or aarch64 triple for which the clang driver's createMSILToolChain
// gives an MSILToolChain (plan 5.9.3 --target, section 12 of 2026-10-22).
// The Darwin, i686 and other native triples of stock bitcode are not.
static bool isSupportedTarget(const Triple &tt) {
  if (tt.getArch() != Triple::x86_64 && tt.getArch() != Triple::aarch64)
    return false;
  return tt.isOSLinux() || tt.isWindowsMSVCEnvironment() ||
         tt.isWindowsGNUEnvironment();
}

void LinkerDriver::addInputFile(InputFile *file) {
  if (!file)
    return;
  // Without --target, the first bitcode file names the codegen triple
  // (plan 5.9.3), as lld/ELF infers its target from the first input file,
  // archive members included.
  if (ctx.arg.target.str().empty())
    if (auto *f = dyn_cast<BitcodeFile>(file))
      ctx.arg.target = getMSILTriple(f->obj->getTargetTriple());
  files.push_back(file);
}

void LinkerDriver::addFile(StringRef path) {
  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer)
    return;
  MemoryBufferRef mbref = *buffer;
  std::optional<sys::fs::UniqueID> id;
  if (sys::fs::UniqueID uid; !sys::fs::getUniqueID(path, uid)) {
    id = uid;
    visitedFiles.insert(uid);
  }

  switch (identify_magic(mbref.getBuffer())) {
  case file_magic::archive: {
    std::optional<std::vector<std::pair<MemoryBufferRef, uint64_t>>>
        maybeMembers = getArchiveMembers(mbref);
    if (!maybeMembers)
      return;
    auto &members = *maybeMembers;

    // An archive without LLVM bitcode or CIL object members (an import
    // library, oldnames.lib, an archive of native objects) is a native
    // library (plan 5.9.4, 5.10.8).
    if (none_of(members,
                [](const auto &p) { return isBitcodeOrCILObject(p.first); })) {
      addInputFile(make<NativeLibraryFile>(mbref, NativeLibraryFile::Archive));
      return;
    }

    bool hasNativeMembers = false;
    for (const auto &[m, offset] : members) {
      if (hasCLIHeader(m)) {
        error(path + "(" + m.getBufferIdentifier() +
              "): linking CIL object files and .NET assemblies is not "
              "supported yet");
        continue;
      }
      // Import members and native objects are the native index's.
      if (isNativeArchiveMember(m)) {
        hasNativeMembers = true;
        continue;
      }
      // Handle -whole-archive. Any other member is lazy. A member that is not
      // LLVM bitcode is reported and skipped by createObjectFile().
      addInputFile(createObjectFile(m, path, offset, !inWholeArchive));
    }
    if (hasNativeMembers)
      addInputFile(make<NativeLibraryFile>(mbref, NativeLibraryFile::Archive));
    return;
  }
  case file_magic::bitcode:
    addInputFile(createObjectFile(mbref, "", 0, inLib));
    return;
  case file_magic::pecoff_executable:
    if (hasCLIHeader(mbref)) {
      error(path + ": linking CIL object files and .NET assemblies is not "
                   "supported yet");
      return;
    }
    addInputFile(make<NativeLibraryFile>(mbref, NativeLibraryFile::PEDll));
    return;
  case file_magic::elf_shared_object:
    addInputFile(make<NativeLibraryFile>(mbref, NativeLibraryFile::ELFShared));
    return;
  case file_magic::unknown:
    // A GNU ld script (glibc's libc.so) names the files to read instead.
    if (isLinkerScript(mbref)) {
      addLinkerScript(mbref, id);
      return;
    }
    [[fallthrough]];
  default:
    error("unknown file type: " + mbref.getBufferIdentifier());
  }
}

// Reads the inputs of the GNU ld script \p mb (plan 5.10.8): file names
// resolved as lld/ELF's ScriptParser::addFile resolves them, and -l<name>
// searched like -l, in the script's order and at its place on the command
// line. Errors use lld/ELF's texts. \p id is the script's file ID, if known.
void LinkerDriver::addLinkerScript(MemoryBufferRef mb,
                                   std::optional<sys::fs::UniqueID> id) {
  // An input that leads back to a script whose inputs are still being added
  // is a cycle (lld/ELF's "there is a cycle in linker script INCLUDEs" is the
  // model), reported at that input of the innermost script and skipped. A
  // script read again after its inputs were added is no cycle.
  if (id && activeScripts.contains(*id)) {
    assert(!scriptInputs.empty() && "an active script adds an input");
    auto [script, offset] = scriptInputs.back();
    error(getScriptDiagnostic(script, offset,
                              "there is a cycle in linker script inputs"));
    return;
  }
  if (id)
    activeScripts.insert(*id);
  scriptInputs.push_back({mb, 0});

  StringRef script = mb.getBufferIdentifier();
  SmallVector<LinkerScriptInput, 8> inputs;
  Error err = readLinkerScript(mb, inputs);
  bool underSysroot = isUnderSysroot(script);
  for (const LinkerScriptInput &input : inputs) {
    scriptInputs.back().second = input.offset;
    if (input.isLibrary) {
      addLibrary(input.name);
      continue;
    }
    std::string msg;
    if (std::optional<std::string> path =
            resolveScriptPath(script, underSysroot, input.name, msg))
      addFile(saver().save(*path));
    else
      error(getScriptDiagnostic(mb, input.offset, msg));
  }

  scriptInputs.pop_back();
  if (id)
    activeScripts.erase(*id);
  if (err)
    error(toString(std::move(err)));
}

static std::optional<std::string> findFromSearchPaths(StringRef path) {
  for (StringRef dir : ctx.arg.searchPaths)
    if (std::optional<std::string> s = findFile(dir, path))
      return s;
  return std::nullopt;
}

// The file names that -l<name> looks for in each -L directory, in the order of
// plan 5.10.9, for the codegen triple. Until a --target or a bitcode input
// names it, every triple's names are candidates. On a non-Windows triple
// <name>.dll and lib<name>.dll are candidates only for the managed kind (a
// .NET assembly, which gets the --reference warning): a native DLL there is
// OtherFormat (plan 5.10.8), so a directory with foo.dll and libfoo.so gives
// libfoo.so, as with ld.lld.
static SmallVector<std::string, 0> getLibraryCandidates(StringRef name) {
  const Triple &tt = ctx.arg.target;
  if (tt.str().empty())
    return {("lib" + name + ".a").str(),   ("lib" + name + ".dll.a").str(),
            (name + ".dll.a").str(),       (name + ".lib").str(),
            ("lib" + name + ".dll").str(), (name + ".dll").str(),
            ("lib" + name + ".so").str()};
  if (tt.isWindowsMSVCEnvironment())
    return {("lib" + name + ".a").str(), (name + ".lib").str(),
            (name + ".dll").str()};
  if (tt.isOSWindows())
    return {("lib" + name + ".a").str(),   ("lib" + name + ".dll.a").str(),
            (name + ".dll.a").str(),       (name + ".lib").str(),
            ("lib" + name + ".dll").str(), (name + ".dll").str()};
  return {("lib" + name + ".a").str(), (name + ".dll").str(),
          ("lib" + name + ".dll").str(), ("lib" + name + ".so").str()};
}

// This is for -l<basename> (plan 5.10.9). The -L directories are searched in
// order, and the first one that holds an acceptable candidate wins. Inside it
// a static library wins (Design.md 5.2's static-first rule, the opposite of
// GNU ld's), then the first import library or native shared library in list
// order, of the object format that the codegen triple reads (plan 5.10.8).
// Archives of machine code and native libraries of the other format are
// skipped (noted under --verbose), and so are .NET assemblies until msil-ld
// links managed libraries (M22). -Bstatic accepts static libraries only.
static std::optional<std::string> searchLibraryBaseName(StringRef name) {
  SmallVector<std::string, 0> candidates = getLibraryCandidates(name);
  for (StringRef dir : ctx.arg.searchPaths) {
    std::optional<std::string> native;
    for (const std::string &candidate : candidates) {
      std::optional<std::string> s = findFile(dir, candidate);
      if (!s)
        continue;
      switch (classifyCandidate(*s)) {
      case LibraryKind::Static:
        return s;
      case LibraryKind::Managed:
        warn(*s + ": skipping the .NET assembly that -l" + name +
             " found; use --reference for .NET reference assemblies");
        break;
      case LibraryKind::NativeOnly:
        log("skipping native static archive " + *s +
            " (no CIL or bitcode members)");
        break;
      case LibraryKind::OtherFormat:
        log("skipping " + getOtherFormatMessage(*s, ctx.arg.target));
        break;
      case LibraryKind::Import:
      case LibraryKind::NativeShared:
        if (!ctx.arg.isStatic && !native)
          native = std::move(s);
        break;
      }
    }
    if (native)
      return native;
  }
  return std::nullopt;
}

// This is for -l<namespec>. -l:<file> takes that file of any kind, also
// under -Bstatic, as lld/ELF does.
static std::optional<std::string> searchLibrary(StringRef name) {
  if (name.starts_with(":"))
    return findFromSearchPaths(name.substr(1));
  return searchLibraryBaseName(name);
}

// Add a given library by searching it from input search paths.
void LinkerDriver::addLibrary(StringRef name) {
  std::optional<std::string> path = searchLibrary(name);
  if (!path) {
    error("unable to find library -l" + name, ErrorTag::LibNotFound, {name});
    return;
  }
  addFile(saver().save(*path));
}

// The MSVC C and C++ runtime libraries, which embedded requests name (the
// clang driver passes --dependent-lib=libcmt on windows-msvc) and msil-ld
// ignores, because the default libraries supply the C runtime (plan 5.10.6).
static bool isMSVCRuntimeLibrary(StringRef name) {
  StringRef stem = path::filename(name);
  if (stem.ends_with_insensitive(".lib"))
    stem = stem.drop_back(4);
  return StringSwitch<bool>(stem.lower())
      .Cases({"libcmt", "libcmtd", "msvcrt", "msvcrtd", "libucrt", "libucrtd",
              "ucrt", "ucrtd", "libvcruntime", "libvcruntimed", "vcruntime",
              "vcruntimed", "msvcprt", "msvcprtd", "libcpmt", "libcpmtd"},
             true)
      .Default(false);
}

// Adds the library that an embedded request of \p f names (\p name as the
// request spells it) and that the search found at \p path. A library that is
// read already is not read again.
void LinkerDriver::addRequestedLibrary(StringRef name,
                                       std::optional<std::string> path,
                                       const InputFile &f) {
  if (!path) {
    error("unable to find library from dependent library specifier: " + name +
          "\n>>> requested by " + toString(&f));
    return;
  }
  sys::fs::UniqueID id;
  if (!sys::fs::getUniqueID(*path, id) && visitedFiles.contains(id))
    return;
  addFile(saver().save(*path));
}

// /alternatename:<from>=<to> (lld-link's parseAlternateName, with lld-link's
// texts).
static void parseAlternateName(StringRef arg) {
  auto [from, to] = arg.split('=');
  if (from.empty() || to.empty()) {
    error("/alternatename: invalid argument: " + arg);
    return;
  }
  auto it = ctx.alternateNames.find(from);
  if (it != ctx.alternateNames.end()) {
    if (it->second != to)
      error("/alternatename: conflicts: " + arg);
    return;
  }
  ctx.alternateNames.insert({from, to});
}

// /failifmismatch:<key>=<value> (lld/COFF/DriverUtils.cpp:684-697: two inputs
// that give one key different values are an error, with lld-link's text).
static void checkFailIfMismatch(StringRef arg, const InputFile &source) {
  auto [k, v] = arg.split('=');
  if (k.empty() || v.empty()) {
    error("/failifmismatch: invalid argument: " + arg);
    return;
  }
  auto it = ctx.mustMatch.find(k);
  if (it != ctx.mustMatch.end() && v != it->second.first) {
    error("/failifmismatch: mismatch detected for '" + k + "':\n>>> " +
          toString(it->second.second) + " has value " + it->second.first +
          "\n>>> " + toString(&source) + " has value " + v);
    return;
  }
  ctx.mustMatch[k] = {v, &source};
}

// Returns true if \p opt starts with one of \p prefixes, ignoring case, and
// sets \p value to the rest.
static bool consumePrefix(StringRef opt, ArrayRef<StringRef> prefixes,
                          StringRef &value) {
  for (StringRef prefix : prefixes) {
    if (opt.starts_with_insensitive(prefix)) {
      value = opt.drop_front(prefix.size());
      return true;
    }
  }
  return false;
}

// The embedded library requests and linker options of a loaded bitcode file
// (plan 5.9.4, 5.10.6), unless --no-dependent-libraries: the dependent
// libraries of stock ELF bitcode (searched as lld/ELF searches them), and the
// options -l<name>, -l:<file>, /DEFAULTLIB:<name> (.lib appended to a name
// without an extension, then searched like -l:), /alternatename:<a>=<b> and
// /failifmismatch:<k>=<v>. Requests naming an MSVC C runtime library are
// ignored with a --verbose note; any other option is a warning. The
// libraries are read after the inputs that are already on the list.
void LinkerDriver::addLinkerOptions(BitcodeFile &f) {
  if (!ctx.arg.dependentLibraries || !f.obj)
    return;

  // lld/ELF/InputFiles.cpp:380-395 (addDependentLibrary).
  for (StringRef spec : f.obj->getDependentLibraries()) {
    if (isMSVCRuntimeLibrary(spec)) {
      log("ignoring C runtime library request '" + spec + "' from " +
          toString(&f));
      continue;
    }
    std::optional<std::string> path = searchLibraryBaseName(spec);
    if (!path)
      path = findFromSearchPaths(spec);
    if (!path && fs::exists(spec))
      path = spec.str();
    addRequestedLibrary(spec, std::move(path), f);
  }

  for (const std::string &optString : f.getLinkerOptions()) {
    StringRef opt = saver().save(optString);
    StringRef value;
    if (opt.starts_with("-l") && opt.size() > 2) {
      StringRef name = opt.drop_front(2);
      if (isMSVCRuntimeLibrary(name.starts_with(":") ? name.drop_front(1)
                                                     : name)) {
        log("ignoring C runtime library request '" + name + "' from " +
            toString(&f));
        continue;
      }
      addRequestedLibrary(name, searchLibrary(name), f);
    } else if (consumePrefix(opt, {"/defaultlib:", "-defaultlib:"}, value) &&
               !value.empty()) {
      if (isMSVCRuntimeLibrary(value)) {
        log("ignoring C runtime library request '" + value + "' from " +
            toString(&f));
        continue;
      }
      // lld-link: a name without an extension gets .lib, and an absolute path
      // is taken as it is.
      std::string name = value.str();
      if (!path::has_extension(name))
        name += ".lib";
      std::optional<std::string> path;
      if (path::is_absolute(name) && fs::exists(name))
        path = name;
      else
        path = findFromSearchPaths(name);
      addRequestedLibrary(value, std::move(path), f);
    } else if (consumePrefix(opt, {"/alternatename:", "-alternatename:"},
                             value)) {
      parseAlternateName(value);
    } else if (consumePrefix(opt, {"/failifmismatch:", "-failifmismatch:"},
                             value)) {
      checkFailIfMismatch(value, f);
    } else {
      warn("ignoring linker option '" + opt + "' in " + toString(&f));
    }
  }
}

// Loads the inputs that are not loaded yet into the symbol table, in order,
// and reads the linker options of every bitcode file as soon as it is loaded,
// extracted archive members included; the libraries they request go to the
// end of the list (lld/ELF's order).
void LinkerDriver::addPendingFiles() {
  while (true) {
    if (numLinkerOptionFiles < ctx.bitcodeFiles.size()) {
      addLinkerOptions(*ctx.bitcodeFiles[numLinkerOptionFiles++]);
      continue;
    }
    if (numLoadedFiles < files.size()) {
      symtab->addFile(files[numLoadedFiles++]);
      continue;
    }
    break;
  }
}

// lld-link's rule for /alternatename:<from>=<to>: when <from> is still
// undefined after every input is loaded, and no native library defines it
// (an import of the right kind is a definition for lld-link), its references
// resolve to <to>.
// <to> becomes a reference of the command line, so that an archive member
// that defines it is extracted and LTO keeps the definition; the
// import-annotation pass resolves <from> after LTO.
void LinkerDriver::applyAlternateNames() {
  for (const auto &[from, to] : ctx.alternateNames) {
    Symbol *sym = symtab->find(from);
    if (!sym || !sym->isUndefined())
      continue;
    SmallVector<const NativeExport *, 2> rejected;
    if (ctx.nativeIndex.lookup(from, rejected,
                               /*isFunctionReference=*/!sym->isData()))
      continue;
    ctx.appliedAlternateNames.insert({from, to});
    symtab->addUndefined(to, saver().save("/alternatename:" + from + "=" + to));
  }
}

void LinkerDriver::createFiles(opt::InputArgList &args) {
  for (auto *arg : args) {
    switch (arg->getOption().getID()) {
    case OPT_library:
      addLibrary(arg->getValue());
      break;
    case OPT_INPUT:
      addFile(arg->getValue());
      break;
    case OPT_Bstatic:
      ctx.arg.isStatic = true;
      break;
    case OPT_Bdynamic:
      ctx.arg.isStatic = false;
      break;
    case OPT_ucrtbase_imports:
      // The built-in import library of plan 5.10.4, where the option appears.
      addInputFile(
          make<NativeLibraryFile>(MemoryBufferRef("", "--ucrtbase-imports"),
                                  NativeLibraryFile::Builtin));
      break;
    case OPT_whole_archive:
      inWholeArchive = true;
      break;
    case OPT_no_whole_archive:
      inWholeArchive = false;
      break;
    case OPT_start_lib:
      if (inLib)
        error("nested --start-lib");
      inLib = true;
      break;
    case OPT_end_lib:
      if (!inLib)
        error("stray --end-lib");
      inLib = false;
      break;
    }
  }
  if (files.empty() && errorCount() == 0)
    error("no input files");
}

// The entry point (plan 5.9.3, D27): main, which the backend calls from the
// generated '<msil>Main'. Another entry symbol needs CIL objects (M20).
static StringRef getEntry(opt::InputArgList &args) {
  auto *arg = args.getLastArg(OPT_entry);
  if (!arg)
    return "main";
  return arg->getValue();
}

// Initializes Config members by the command line options.
static void readConfigs(opt::InputArgList &args) {
  // msil-ld writes no relocatable output. Reported while the options are
  // read, so that -r alone prints only this (plan 5.9.3).
  if (args.hasArg(OPT_relocatable))
    error("-r is not supported");

  ctx.arg.allowMultipleDefinition = args.hasFlag(
      OPT_allow_multiple_definition, OPT_no_allow_multiple_definition, false);
  if (auto *arg = args.getLastArg(OPT_assembly_name, OPT_soname))
    ctx.arg.assemblyName = arg->getValue();
  ctx.arg.dependentLibraries =
      args.hasFlag(OPT_dependent_libraries, OPT_no_dependent_libraries, true);
  ctx.arg.disableVerify = args.hasArg(OPT_disable_verify);
  ctx.arg.entry = getEntry(args);
  ctx.arg.ltoEmitAsm = args.hasArg(OPT_lto_emit_asm);
  ctx.arg.ltoo = args::getInteger(args, OPT_lto_O, 2);
  if (ctx.arg.ltoo > 3)
    error("invalid optimization level for LTO: " + Twine(ctx.arg.ltoo));
  unsigned ltoCgo =
      args::getInteger(args, OPT_lto_CGO, args::getCGOptLevel(ctx.arg.ltoo));
  if (auto level = CodeGenOpt::getLevel(ltoCgo))
    ctx.arg.ltoCgo = *level;
  else
    error("invalid codegen optimization level for LTO: " + Twine(ltoCgo));
  ctx.arg.ltoPartitions = args::getInteger(args, OPT_lto_partitions, 1);
  ctx.arg.mapFile = args.getLastArgValue(OPT_Map);
  ctx.arg.outputFile = args.getLastArgValue(OPT_o);
  ctx.arg.printImports = args.hasArg(OPT_print_imports);
  ctx.arg.saveTemps = args.hasArg(OPT_save_temps);
  ctx.arg.searchPaths = args::getStrings(args, OPT_library_path);
  ctx.arg.shared = args.hasArg(OPT_shared);
  // -s and --strip-all are accepted and change nothing (the clang driver
  // passes them, plan 5.10.3).
  ctx.arg.stripAll = args.hasArg(OPT_strip_all);
  ctx.arg.sysroot = args.getLastArgValue(OPT_sysroot);
  ctx.arg.trace = args.hasArg(OPT_trace);
  ctx.arg.thinLTOCacheDir = args.getLastArgValue(OPT_thinlto_cache_dir);
  ctx.arg.thinLTOJobs = args.getLastArgValue(OPT_thinlto_jobs);
  ctx.arg.ucrtbaseImports = args.hasArg(OPT_ucrtbase_imports);
  ctx.arg.undefined = args::getStrings(args, OPT_undefined);
  ctx.arg.warnCommon = args.hasFlag(OPT_warn_common, OPT_no_warn_common, false);
  ctx.arg.whyExtract = args.getLastArgValue(OPT_why_extract);
  errorHandler().verbose = args.hasArg(OPT_verbose);
  LLVM_DEBUG(errorHandler().verbose = true);

  // The codegen triple, an MSIL triple. Without --target, the first bitcode
  // input names it (LinkerDriver::addInputFile).
  if (auto *arg = args.getLastArg(OPT_target))
    ctx.arg.target = getMSILTriple(Triple::normalize(arg->getValue()));

  if (args.hasArg(OPT_print_map))
    ctx.arg.mapFile = "-";

  // A library assembly needs the PE writer and exports (M22); the option is
  // known because the clang driver passes --shared for -shared (plan 5.9.3).
  if (ctx.arg.shared)
    error("-shared is not supported yet");
  // Before M20 the entry point is main (plan 5.9.3, D27).
  if (ctx.arg.entry != "main")
    error("--entry=" + ctx.arg.entry +
          ": an entry point other than main is not supported yet");
}

// Before M20 msil-ld cannot link the CIL objects that LTO writes, so every
// LTO link but a whole-program --lto-emit-asm one is this error (plan 5.9.13).
static void reportObjectOutput() {
  error("ThinLTO, --lto-partitions and LTO without --lto-emit-asm need CIL "
        "object output, which this msil-ld does not have; use --lto-emit-asm "
        "(clang: -Wl,--lto-emit-asm) to write ILAsm text");
}

// Some command line options or some combinations of them are not allowed.
// This function checks for such errors.
static void checkOptions() {
  if (ctx.arg.ltoPartitions == 0)
    error("--lto-partitions: number of threads must be > 0");
  if (!get_threadpool_strategy(ctx.arg.thinLTOJobs))
    error("--thinlto-jobs: invalid job count: " + ctx.arg.thinLTOJobs);

  // ThinLTO input is found when the inputs are loaded (linkerMain).
  if (!ctx.arg.ltoEmitAsm || ctx.arg.ltoPartitions > 1) {
    reportObjectOutput();
    return;
  }

  if (ctx.arg.outputFile.empty())
    error("no output file specified");
}

static void handleLibcall(StringRef name) {
  Symbol *sym = symtab->find(name);
  if (sym && sym->isLazy() && isa<BitcodeFile>(sym->getFile())) {
    if (!ctx.arg.whyExtract.empty())
      ctx.whyExtractRecords.emplace_back("<libcall>", sym->getFile(), *sym);
    cast<LazySymbol>(sym)->extract();
  }
}

// lld/ELF/Driver.cpp's writeWhyExtract.
static void writeWhyExtract() {
  if (ctx.arg.whyExtract.empty())
    return;

  // LLD_IN_TEST runs the link more than once and prints only in the last run
  // (lld/ELF's Ctx::openAuxiliaryFile).
  if (errorHandler().disableOutput && ctx.arg.whyExtract == "-")
    return;

  std::error_code ec;
  raw_fd_ostream os(ctx.arg.whyExtract, ec, sys::fs::OF_None);
  if (ec) {
    error("cannot open --why-extract= file " + ctx.arg.whyExtract + ": " +
          ec.message());
    return;
  }

  os << "reference\textracted\tsymbol\n";
  for (auto &entry : ctx.whyExtractRecords) {
    os << std::get<0>(entry) << '\t' << toString(std::get<1>(entry)) << '\t'
       << toString(std::get<2>(entry)) << '\n';
  }
}

// Reconstructs command line arguments so that so that you can re-run
// the same command with the same inputs. This is for --reproduce.
static std::string createResponseFile(const opt::InputArgList &args) {
  SmallString<0> data;
  raw_svector_ostream os(data);

  // Copy the command line to the output while rewriting paths.
  for (auto *arg : args) {
    switch (arg->getOption().getID()) {
    case OPT_reproduce:
      break;
    case OPT_INPUT:
      os << quote(relativeToRoot(arg->getValue())) << "\n";
      break;
    case OPT_o:
    case OPT_Map:
    case OPT_why_extract:
      // If an output path contains directories, "lld @response.txt" will
      // likely fail because the archive we are creating doesn't contain empty
      // directories for the output path (-o doesn't create directories).
      // Strip directories to prevent the issue (lld/ELF/DriverUtils.cpp).
      os << arg->getSpelling();
      if (opt::Option::RenderStyleKind style =
              arg->getOption().getRenderStyle();
          style == opt::Option::RenderSeparateStyle ||
          style == opt::Option::RenderEqStyle)
        os << ' ';
      os << quote(path::filename(arg->getValue())) << "\n";
      break;
    case OPT_library:
      // -l<name>, the spelling the clang driver uses.
      os << arg->getSpelling() << quote(arg->getValue()) << "\n";
      break;
    case OPT_library_path:
    case OPT_sysroot:
      // The libraries that -l finds are in the archive under these
      // directories (lld/ELF/DriverUtils.cpp).
      os << arg->getSpelling() << " "
         << quote(fs::exists(arg->getValue()) ? relativeToRoot(arg->getValue())
                                              : std::string(arg->getValue()))
         << "\n";
      break;
    default:
      os << toString(*arg) << "\n";
    }
  }
  return std::string(data);
}

// The triple key of plan 5.8.9: the architecture, the OS and the environment
// without its version; the vendor is ignored. \p tt is a bitcode input's
// triple, an MSIL triple or the native triple that stock clang writes (Q12),
// whose object format is its platform's default.
static bool hasTripleKey(const Triple &tt, const Triple &target) {
  if (!tt.isOSBinFormatMSIL() &&
      tt.getObjectFormat() != tt.getDefaultObjectFormat())
    return false;
  return tt.getArch() == target.getArch() &&
         tt.getSubArch() == target.getSubArch() &&
         tt.getOS() == target.getOS() &&
         tt.getEnvironment() == target.getEnvironment();
}

// The data model of a bitcode input, from its triple alone (plan 5.8.9): the
// symbol table carries no DataLayout and no wchar_size flag.
static MSILLink::Header getDataModel(const Triple &tt) {
  MSILLink::Header h;
  MSILLink::setDataModel(h, tt, tt.isArch64Bit() ? 8 : 4,
                         tt.getDefaultWCharSize());
  return h;
}

// The data model as llvm-cil-dump prints it (plan 5.8.11).
static std::string getDataModelString(const MSILLink::Header &h) {
  return std::to_string(h.PointerSize) + "," + std::to_string(h.LongSize) +
         "," + std::to_string(h.WCharSize) + "," +
         std::to_string(h.LongDoubleSize);
}

// Checks every loaded bitcode file in load order (plan 5.8.9, 5.9.13): its
// triple must have the codegen triple's key; bitcode for the native triple
// is accepted with one warning per link (Q12); and its data model must be that
// of the first input with the right key. PointerSize and LongSize are compared,
// WCharSize from M20 (the IR linker already rejects two different wchar_size
// flags). Behind equal triple keys the data models of bitcode inputs cannot
// differ; the check matters once CIL objects carry their own (M20).
static void checkBitcodeFiles() {
  const Triple &target = ctx.arg.target;
  bool warnedNative = false;
  const BitcodeFile *modelFile = nullptr;
  MSILLink::Header model;
  for (BitcodeFile *f : ctx.bitcodeFiles) {
    StringRef triple = f->obj->getTargetTriple();
    Triple tt(triple);
    if (!hasTripleKey(tt, target)) {
      error(toString(f) + ": target triple " + triple +
            " is incompatible with " + target.str());
      continue;
    }
    if (!tt.isOSBinFormatMSIL() && !warnedNative) {
      warn(toString(f) + ": linking bitcode for " + triple + " as " +
           target.str() + "; stock-clang bitcode is a compatibility path");
      warnedNative = true;
    }

    MSILLink::Header h = getDataModel(tt);
    if (!modelFile) {
      modelFile = f;
      model = h;
    } else if (h.PointerSize != model.PointerSize ||
               h.LongSize != model.LongSize) {
      error(toString(f) + ": data model " + getDataModelString(h) +
            " differs from " + getDataModelString(model) + " in " +
            toString(modelFile));
    }
  }
}

// Returns true if \p f has a ThinLTO module, by lto::LTO::addModule's rule (a
// module with a summary that is not a regular LTO module). The lto::InputFile
// itself knows only after it is added to LTO.
static bool isThinLTO(const BitcodeFile *f) {
  Expected<std::vector<BitcodeModule>> modsOrErr =
      getBitcodeModuleList(f->obj->getFileBuffer());
  if (!modsOrErr) {
    consumeError(modsOrErr.takeError());
    return false;
  }
  for (BitcodeModule &bm : *modsOrErr) {
    Expected<BitcodeLTOInfo> info = bm.getLTOInfo();
    if (!info) {
      consumeError(info.takeError());
      continue;
    }
    if (info->IsThinLTO)
      return true;
  }
  return false;
}

LinkerDriver::LinkerDriver(Ctx &ctx) : ctx(ctx) {}

void LinkerDriver::linkerMain(ArrayRef<const char *> argsArr) {
  MSILOptTable parser;
  opt::InputArgList args = parser.parse(argsArr.slice(1));

  // Interpret these flags early because error()/warn() depend on them.
  auto &errHandler = errorHandler();
  errHandler.errorLimit = args::getInteger(args, OPT_error_limit, 20);
  errHandler.fatalWarnings =
      args.hasFlag(OPT_fatal_warnings, OPT_no_fatal_warnings, false);

  // Handle --help
  if (args.hasArg(OPT_help)) {
    parser.printHelp(errHandler.outs(),
                     (std::string(argsArr[0]) + " [options] file...").c_str(),
                     "LLVM Linker", false);
    return;
  }

  // Handle -v or -version.
  if (args.hasArg(OPT_v) || args.hasArg(OPT_version))
    errHandler.outs() << getLLDVersion() << "\n";

  // Handle --reproduce. msil-ld reads no environment variable, so there is no
  // LLD_REPRODUCE (plan 5.9.4).
  if (auto *arg = args.getLastArg(OPT_reproduce)) {
    StringRef path = arg->getValue();
    Expected<std::unique_ptr<TarWriter>> errOrWriter =
        TarWriter::create(path, path::stem(path));
    if (errOrWriter) {
      tar = std::move(*errOrWriter);
      tar->append("response.txt", createResponseFile(args));
      tar->append("version.txt", getLLDVersion() + "\n");
    } else {
      error("--reproduce: " + toString(errOrWriter.takeError()));
    }
  }

  // Parse and evaluate -mllvm options.
  std::vector<const char *> v;
  v.push_back("msil-ld (LLVM option parsing)");
  for (auto *arg : args.filtered(OPT_mllvm))
    v.push_back(arg->getValue());
  cl::ResetAllOptionOccurrences();
  cl::parseCommandLineOptions(ctx.arg.libraryOptions, v);

  readConfigs(args);

  // The behavior of -v or --version is a bit strange, but this is
  // needed for compatibility with GNU linkers.
  if (args.hasArg(OPT_v) && !args.hasArg(OPT_INPUT))
    return;
  if (args.hasArg(OPT_version))
    return;

  createFiles(args);
  if (errorCount())
    return;

  // The codegen triple is fixed now, by --target or by the first bitcode
  // input. An unsupported one stops the link before the per-file triple and
  // data-model checks and before LTO, which would compile for it.
  if (!ctx.arg.target.str().empty() && !isSupportedTarget(ctx.arg.target)) {
    error("unsupported target triple '" + ctx.arg.target.str() + "' for MSIL");
    return;
  }

  // The built-in import library binds functions to ucrtbase.dll, a Windows
  // DLL (plan 5.10.4).
  if (ctx.arg.ucrtbaseImports && !ctx.arg.target.str().empty() &&
      !ctx.arg.target.isOSWindows()) {
    error("--ucrtbase-imports needs a Windows target, not " +
          ctx.arg.target.str());
    return;
  }

  checkOptions();
  if (errorCount())
    return;

  // Fail early if the output file or map file is not writable. If a user has a
  // long link, e.g. due to a large LTO link, they do not wish to run it and
  // find that it failed because there was a mistake in their command-line.
  if (auto e = tryCreateFile(ctx.arg.outputFile))
    error("cannot open output file " + ctx.arg.outputFile + ": " + e.message());
  if (auto e = tryCreateFile(ctx.arg.mapFile))
    error("cannot open map file " + ctx.arg.mapFile + ": " + e.message());
  if (errorCount())
    return;

  // Handle --trace-symbol.
  for (auto *arg : args.filtered(OPT_trace_symbol))
    symtab->trace(arg->getValue());

  // Add all files to the symbol table. This will add almost all
  // symbols that we need to the symbol table. The native libraries go to the
  // native index, and the libraries that the linker options of bitcode files
  // request follow the command line.
  addPendingFiles();
  if (errorCount())
    return;

  // Handle the `--undefined <sym>` options.
  for (StringRef name : ctx.arg.undefined)
    symtab->addUndefined(name, ("-u " + name).str());

  // The entry point is visible to regular objects, so LTO keeps it, and it
  // extracts the archive member that defines it. bindLateSymbols() reports it
  // if nothing defines it.
  if (!ctx.arg.entry.empty())
    symtab->addUndefined(ctx.arg.entry, "--entry");
  addPendingFiles();
  if (errorCount())
    return;

  // If any of our inputs are bitcode files, the LTO code generator may create
  // references to certain library functions that might not be explicit in the
  // bitcode file's symbol table. If any of those library functions are defined
  // in a bitcode file in an archive member, we need to arrange to use LTO to
  // compile those archive members by adding them to the link beforehand. The
  // functions are the codegen triple's, which LTO compiles every input for.
  if (!ctx.bitcodeFiles.empty())
    for (auto *s : lto::LTO::getRuntimeLibcallSymbols(ctx.arg.target))
      handleLibcall(s);
  addPendingFiles();
  if (errorCount())
    return;

  // Every input is loaded: /alternatename applies to the names that are
  // still undefined, and may extract the members that define the targets.
  applyAlternateNames();
  addPendingFiles();
  if (errorCount())
    return;

  // Every input is loaded: check the triples and data models.
  checkBitcodeFiles();
  if (errorCount())
    return;

  // ThinLTO writes one CIL object per module, which this msil-ld cannot link.
  if (any_of(ctx.bitcodeFiles, isThinLTO)) {
    reportObjectOutput();
    return;
  }

  // The symbols that are still undefined (plan 5.9.6): the native libraries
  // bind what they define (step 2); hidden symbols and the entry point are
  // errors here, and the import-annotation pass reports the rest after LTO.
  symtab->bindLateSymbols();
  if (errorCount())
    return;

  // Do link-time optimization. With --lto-emit-asm this writes the ILAsm
  // text of the whole program to the output file.
  symtab->compileBitcodeFiles();
  if (errorCount())
    return;

  writeMapFile();
  writeWhyExtract();
}
} // namespace lld::msil
