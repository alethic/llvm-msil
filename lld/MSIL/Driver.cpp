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
// .NET assemblies (llvm/docs/MSIL/ImplementationPlan.md 5.9). In M14 its
// inputs are LLVM bitcode files and archives of them, which LTO compiles for
// the codegen triple; --lto-emit-asm writes the whole program as ILAsm text,
// and ilasm turns that into an assembly (msil-ld writes PE files from M20).
// Native libraries are accepted without being read (M15 reads them).
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Driver.h"
#include "Config.h"
#include "InputFiles.h"
#include "MapFile.h"
#include "SymbolTable.h"
#include "lld/Common/Args.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Filesystem.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Reproduce.h"
#include "lld/Common/Strings.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/STLExtras.h"
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

// A library that -l found: its path, and whether it is a static library
// (plan 5.10.9).
struct FoundLibrary {
  std::string path;
  bool isStatic;
};

class LinkerDriver {
public:
  LinkerDriver(Ctx &);
  void linkerMain(ArrayRef<const char *> argsArr);

private:
  void createFiles(opt::InputArgList &args);
  void addFile(StringRef path);
  void addLibrary(StringRef name);
  void addNativeLibrary(StringRef path);
  void addInputFile(InputFile *file);

  Ctx &ctx;

  // True if we are in --whole-archive and --no-whole-archive.
  bool inWholeArchive = false;

  // True if we are in --start-lib and --end-lib.
  bool inLib = false;

  // True if an input is a native library, which is not an InputFile in M14.
  bool hasNativeLibrary = false;

  std::vector<InputFile *> files;
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
// Each slice consists of a member file in the archive.
std::vector<std::pair<MemoryBufferRef, uint64_t>> static getArchiveMembers(
    MemoryBufferRef mb) {
  std::unique_ptr<Archive> file =
      CHECK(Archive::create(mb),
            mb.getBufferIdentifier() + ": failed to parse archive");

  std::vector<std::pair<MemoryBufferRef, uint64_t>> v;
  Error err = Error::success();
  for (const Archive::Child &c : file->children(err)) {
    MemoryBufferRef mbref =
        CHECK(c.getMemoryBufferRef(),
              mb.getBufferIdentifier() +
                  ": could not get the buffer for a child of the archive");
    v.push_back(std::make_pair(mbref, c.getChildOffset()));
  }
  if (err)
    fatal(mb.getBufferIdentifier() +
          ": Archive::children failed: " + toString(std::move(err)));

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

// Returns true if the file at \p path is a static library (plan 5.10.9): an
// archive with at least one LLVM bitcode or CIL object member, or a bitcode
// file. Other files that -l finds are native libraries. The candidate is read
// only to be classified; addFile() reads the one -l chooses.
static bool isStaticLibrary(StringRef path) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> mbOrErr =
      MemoryBuffer::getFile(path, /*IsText=*/false,
                            /*RequiresNullTerminator=*/false);
  if (!mbOrErr)
    return false;
  MemoryBufferRef mb = (*mbOrErr)->getMemBufferRef();
  file_magic magic = identify_magic(mb.getBuffer());
  if (magic == file_magic::bitcode)
    return true;
  if (magic != file_magic::archive)
    return false;

  Expected<std::unique_ptr<Archive>> file = Archive::create(mb);
  if (!file) {
    consumeError(file.takeError());
    return false;
  }
  bool found = false;
  Error err = Error::success();
  for (const Archive::Child &c : (*file)->children(err)) {
    Expected<MemoryBufferRef> member = c.getMemoryBufferRef();
    if (!member) {
      consumeError(member.takeError());
      continue;
    }
    if (isBitcodeOrCILObject(*member)) {
      found = true;
      break;
    }
  }
  consumeError(std::move(err));
  return found;
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

// Native libraries (import libraries, DLLs, shared objects and their linker
// scripts, archives of native objects) are M15's (plan 5.10.8): msil-ld
// accepts them without reading their contents, and the code generator binds
// undefined functions through the D14 table until then (plan 5.9.6).
void LinkerDriver::addNativeLibrary(StringRef path) {
  log(path + ": native library, accepted without reading it");
  hasNativeLibrary = true;
}

void LinkerDriver::addFile(StringRef path) {
  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer)
    return;
  MemoryBufferRef mbref = *buffer;

  switch (identify_magic(mbref.getBuffer())) {
  case file_magic::archive: {
    auto members = getArchiveMembers(mbref);

    // An archive without LLVM bitcode or CIL object members (an import
    // library, or an archive of native objects) is a native library (plan
    // 5.9.4, 5.10.9).
    if (none_of(members,
                [](const auto &p) { return isBitcodeOrCILObject(p.first); })) {
      addNativeLibrary(path);
      return;
    }

    for (const auto &[m, offset] : members) {
      if (hasCLIHeader(m)) {
        error(path + "(" + m.getBufferIdentifier() +
              "): linking CIL object files and .NET assemblies is not "
              "supported yet");
        continue;
      }
      // Handle -whole-archive. Any other member is lazy. A member that is not
      // LLVM bitcode is reported and skipped by createObjectFile().
      addInputFile(createObjectFile(m, path, offset, !inWholeArchive));
    }
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
    addNativeLibrary(path);
    return;
  case file_magic::elf_shared_object:
    addNativeLibrary(path);
    return;
  default:
    error("unknown file type: " + mbref.getBufferIdentifier());
  }
}

static std::optional<std::string> findFromSearchPaths(StringRef path) {
  for (StringRef dir : ctx.arg.searchPaths)
    if (std::optional<std::string> s = findFile(dir, path))
      return s;
  return std::nullopt;
}

// The file names that -l<name> looks for in each -L directory, in the order of
// plan 5.10.9, for the codegen triple. Until a --target or a bitcode input
// names it, every triple's names are candidates.
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
// order, and the first one that holds a candidate wins. Inside it a static
// library wins (Design.md 5.2's static-first rule, the opposite of GNU ld's),
// then the first candidate in list order, which is a native library.
static std::optional<FoundLibrary> searchLibraryBaseName(StringRef name) {
  SmallVector<std::string, 0> candidates = getLibraryCandidates(name);
  for (StringRef dir : ctx.arg.searchPaths) {
    std::optional<std::string> native;
    for (const std::string &candidate : candidates) {
      std::optional<std::string> s = findFile(dir, candidate);
      if (!s)
        continue;
      if (isStaticLibrary(*s))
        return FoundLibrary{std::move(*s), true};
      if (!native)
        native = std::move(s);
    }
    if (native)
      return FoundLibrary{std::move(*native), false};
  }
  return std::nullopt;
}

// This is for -l<namespec>. -l:<file> takes that file of any kind.
static std::optional<FoundLibrary> searchLibrary(StringRef name) {
  if (name.starts_with(":")) {
    if (std::optional<std::string> s = findFromSearchPaths(name.substr(1)))
      return FoundLibrary{*s, isStaticLibrary(*s)};
    return std::nullopt;
  }
  return searchLibraryBaseName(name);
}

// Add a given library by searching it from input search paths.
void LinkerDriver::addLibrary(StringRef name) {
  std::optional<FoundLibrary> lib = searchLibrary(name);
  if (!lib) {
    error("unable to find library -l" + name, ErrorTag::LibNotFound, {name});
    return;
  }
  StringRef path = saver().save(lib->path);
  if (lib->isStatic)
    addFile(path);
  else if (readFile(path)) // For --reproduce.
    addNativeLibrary(path);
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
  if (files.empty() && !hasNativeLibrary && errorCount() == 0)
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
  // symbols that we need to the symbol table.
  for (InputFile *f : files)
    symtab->addFile(f);
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

  // The symbols that are still undefined (plan 5.9.6, M14): undefined data,
  // hidden symbols and the entry point are errors here, and undefined
  // functions go to LTO, which imports them through the D14 table.
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
