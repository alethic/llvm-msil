//===- Driver.cpp - msil-ld prototype (P2 recon 'lld-flavor') -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A deliberately small lld flavor that exercises the parts of msil-ld that do
// not depend on the Phase 2 object format or the PE writer:
//   - flavor registration (argv[0] 'msil-ld', '-flavor msil'),
//   - lld/Common option parsing, response files, --reproduce, -mllvm,
//   - inputs: bitcode files, archives (lazy members), --start-lib/--end-lib,
//     --whole-archive, -L/-l search with -Bstatic/-Bdynamic,
//   - a symbol table with lazy extraction, weak/strong resolution, the lld
//     duplicate-symbol and undefined-symbol diagnostics, --why-extract, -Map,
//   - native DLLs as the last resolution step (Design.md 5.2 step 2.3): their
//     export tables turn remaining undefined functions into P/Invoke imports,
//     handed to codegen as "msil-import-module" attributes by a module pass
//     that lto::Config::PreCodeGenPassesHook adds,
//   - regular LTO through the MSIL TargetMachine with lto::Config::
//     OverrideTriple set to the -msil triple, so stock-clang bitcode for the
//     native triple is retargeted, writing ILAsm text (--lto-emit-asm, the
//     ld.lld spelling) that ilasm assembles.
// Not a design for the real lld/MSIL sources (see the report for those).
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Args.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Driver.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Filesystem.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Reproduce.h"
#include "lld/Common/Strings.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/LTO/Config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Pass.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TarWriter.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Threading.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::opt;
using namespace llvm::sys;

namespace lld::msil {
namespace {

enum {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};

#define OPTTABLE_CODE
#include "Options.inc"

class MSILOptTable : public opt::OptTable {
public:
  MSILOptTable() : opt::OptTable(optionTables()) {}
  opt::InputArgList parse(ArrayRef<const char *> argv);
};

struct Config {
  StringRef outputFile;
  SmallVector<StringRef, 0> searchPaths;
  bool isStatic = false;
  bool emitAsm = false;
  bool allowMultipleDefinition = false;
  bool shared = false;
  unsigned ltoo = 2;
  CodeGenOptLevel ltoCgo = CodeGenOptLevel::Default;
  std::string triple; // The -msil triple codegen uses.
  StringRef mapFile;
  StringRef whyExtract;
  SmallVector<StringRef, 0> references;
  llvm::cl::LibraryOptionsContext libraryOptions;
};

class InputFile;

struct Symbol {
  enum Kind : uint8_t { Undefined, Defined, Lazy, Native };
  StringRef name;
  InputFile *file = nullptr; // Defining, lazy-providing or first referencing.
  Kind kind = Undefined;
  bool weak = false;
  StringRef nativeLib; // Kind == Native: the P/Invoke module name.
};

class InputFile {
public:
  InputFile(MemoryBufferRef mb, StringRef archiveName, bool lazy)
      : mb(mb), archiveName(archiveName), lazy(lazy) {}
  MemoryBufferRef mb;
  std::string archiveName;
  bool lazy;
  bool parsed = false;
  std::unique_ptr<lto::InputFile> obj;
  std::vector<Symbol *> symbols; // One per obj->symbols() entry.
};

struct Ctx {
  Config arg;
  std::vector<InputFile *> files;      // Eager inputs in command-line order.
  std::vector<InputFile *> ltoFiles;   // Parsed bitcode, in parse order.
  DenseMap<CachedHashStringRef, Symbol *> symMap;
  std::vector<Symbol *> symbols;
  StringMap<StringRef> nativeExports; // Symbol -> P/Invoke module (first wins).
  std::vector<std::tuple<std::string, const InputFile *, StringRef>> whyExtract;
  std::unique_ptr<TarWriter> tar;
};
Ctx ctx;

std::string fileName(const InputFile *f) {
  if (!f)
    return "<internal>";
  if (f->archiveName.empty())
    return std::string(f->mb.getBufferIdentifier());
  return (f->archiveName + "(" + path::filename(f->mb.getBufferIdentifier()) +
          ")")
      .str();
}

opt::InputArgList MSILOptTable::parse(ArrayRef<const char *> argv) {
  SmallVector<const char *, 256> vec(argv.data(), argv.data() + argv.size());
  unsigned missingIndex, missingCount;
  opt::InputArgList args = ParseArgs(vec, missingIndex, missingCount);
  cl::TokenizerCallback quoting = Triple(sys::getProcessTriple()).isOSWindows()
                                      ? cl::TokenizeWindowsCommandLine
                                      : cl::TokenizeGNUCommandLine;
  if (auto *arg = args.getLastArg(OPT_rsp_quoting))
    quoting = StringRef(arg->getValue()) == "windows"
                  ? cl::TokenizeWindowsCommandLine
                  : cl::TokenizeGNUCommandLine;
  cl::ExpandResponseFiles(saver(), quoting, vec);
  args = ParseArgs(vec, missingIndex, missingCount);
  if (missingCount)
    error(Twine(args.getArgString(missingIndex)) + ": missing argument");
  for (auto *arg : args.filtered(OPT_UNKNOWN))
    error("unknown argument: " + arg->getAsString(args));
  return args;
}

std::optional<MemoryBufferRef> readFile(StringRef path) {
  log("Loading: " + path);
  auto mbOrErr = MemoryBuffer::getFile(path);
  if (auto ec = mbOrErr.getError()) {
    error("cannot open " + path + ": " + ec.message());
    return std::nullopt;
  }
  MemoryBufferRef mbref = (*mbOrErr)->getMemBufferRef();
  make<std::unique_ptr<MemoryBuffer>>(std::move(*mbOrErr));
  if (ctx.tar)
    ctx.tar->append(relativeToRoot(path), mbref.getBuffer());
  return mbref;
}

//===-- Symbol table ------------------------------------------------------===//

Symbol *insert(StringRef name, bool &isNew) {
  auto [it, inserted] =
      ctx.symMap.try_emplace(CachedHashStringRef(name), nullptr);
  isNew = inserted;
  if (inserted) {
    it->second = make<Symbol>();
    it->second->name = name;
    ctx.symbols.push_back(it->second);
  }
  return it->second;
}

Symbol *find(StringRef name) {
  return ctx.symMap.lookup(CachedHashStringRef(name));
}

void parseBitcode(InputFile *f);

void extract(InputFile *f, StringRef why, StringRef symName) {
  if (f->parsed)
    return;
  if (!ctx.arg.whyExtract.empty())
    ctx.whyExtract.emplace_back(why.str(), f, symName);
  f->lazy = false;
  parseBitcode(f);
}

void addLazy(StringRef name, InputFile *f) {
  bool isNew;
  Symbol *s = insert(name, isNew);
  if (isNew) {
    s->kind = Symbol::Lazy;
    s->file = f;
    return;
  }
  if (s->kind == Symbol::Undefined)
    extract(f, fileName(s->file), name);
}

void reportDuplicate(Symbol *s, InputFile *newFile) {
  if (ctx.arg.allowMultipleDefinition)
    return;
  error("duplicate symbol: " + s->name + "\n>>> defined in " +
        fileName(s->file) + "\n>>> defined in " + fileName(newFile));
}

void parseBitcode(InputFile *f) {
  f->parsed = true;
  Triple tt(f->obj->getTargetTriple());
  Triple want(ctx.arg.triple);
  Triple a = tt.getNativeFormatTriple(), b = want.getNativeFormatTriple();
  if (a.getArch() != b.getArch() || a.getOS() != b.getOS() ||
      a.getEnvironment() != b.getEnvironment()) {
    error(fileName(f) + ": target triple '" + tt.str() +
          "' is not compatible with '" + want.str() + "'");
    return;
  }
  if (!tt.isOSBinFormatMSIL())
    log(fileName(f) + ": native-triple bitcode, retargeted to " + want.str());
  ctx.ltoFiles.push_back(f);
  for (const lto::InputFile::Symbol &objSym : f->obj->symbols()) {
    StringRef name = saver().save(objSym.getName());
    bool isNew;
    Symbol *s = insert(name, isNew);
    f->symbols.push_back(s);
    if (objSym.isUndefined()) {
      if (isNew) {
        s->kind = Symbol::Undefined;
        s->file = f;
        s->weak = objSym.isWeak();
      } else if (s->kind == Symbol::Lazy) {
        extract(s->file, fileName(f), name);
      }
      continue;
    }
    if (isNew || s->kind == Symbol::Undefined || s->kind == Symbol::Lazy) {
      s->kind = Symbol::Defined;
      s->file = f;
      s->weak = objSym.isWeak();
      continue;
    }
    // Two definitions: the first strong one wins, weak ones yield.
    if (objSym.isWeak())
      continue;
    if (s->weak) {
      s->file = f;
      s->weak = false;
      continue;
    }
    reportDuplicate(s, f);
  }
}

//===-- Inputs ------------------------------------------------------------===//

bool inWholeArchive = false;
bool inLib = false;

InputFile *createBitcode(MemoryBufferRef mb, StringRef archiveName,
                         uint64_t offset, bool lazy) {
  // As lld/wasm BitcodeFile: archive members get unique names for ThinLTO.
  StringRef name =
      archiveName.empty()
          ? mb.getBufferIdentifier()
          : saver().save(archiveName + "(" +
                         path::filename(mb.getBufferIdentifier()) + " at " +
                         utostr(offset) + ")");
  auto *f = make<InputFile>(MemoryBufferRef(mb.getBuffer(), name), archiveName,
                            lazy);
  f->obj = check(lto::InputFile::create(f->mb));
  return f;
}

void addNativeDll(MemoryBufferRef mb, StringRef path) {
  std::unique_ptr<Binary> bin = CHECK(createBinary(mb), path);
  auto *coff = dyn_cast<COFFObjectFile>(bin.get());
  if (!coff) {
    error(path + ": not a PE file");
    return;
  }
  const data_directory *clr = coff->getDataDirectory(COFF::CLR_RUNTIME_HEADER);
  if (clr && clr->RelativeVirtualAddress) {
    error(path + ": managed assembly inputs (Design.md 5.4) are P2-C; not in "
                 "this prototype");
    return;
  }
  StringRef lib = path::stem(path); // "ucrtbase", no suffix (Design.md 5.3)
  unsigned n = 0;
  for (const ExportDirectoryEntryRef &e : coff->export_directories()) {
    StringRef name;
    if (Error err = e.getSymbolName(name)) {
      consumeError(std::move(err));
      continue;
    }
    if (!name.empty() && ctx.nativeExports.try_emplace(name, lib).second)
      ++n;
  }
  log(path + ": " + Twine(n) + " native exports");
}

void addFile(StringRef path) {
  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer)
    return;
  MemoryBufferRef mbref = *buffer;
  switch (identify_magic(mbref.getBuffer())) {
  case file_magic::archive: {
    std::unique_ptr<Archive> file =
        CHECK(Archive::create(mbref), path + ": failed to parse archive");
    Error err = Error::success();
    for (const Archive::Child &c : file->children(err)) {
      MemoryBufferRef m =
          CHECK(c.getMemoryBufferRef(),
                path + ": could not get the buffer for a child of the archive");
      if (identify_magic(m.getBuffer()) != file_magic::bitcode) {
        warn(path + ": archive member '" + m.getBufferIdentifier() +
             "' is neither an MSIL object file nor LLVM bitcode");
        continue;
      }
      ctx.files.push_back(
          createBitcode(m, path, c.getChildOffset(), !inWholeArchive));
    }
    if (err)
      error(path + ": Archive::children failed: " + toString(std::move(err)));
    for (std::unique_ptr<MemoryBuffer> &mb : file->takeThinBuffers())
      make<std::unique_ptr<MemoryBuffer>>(std::move(mb));
    return;
  }
  case file_magic::bitcode:
    ctx.files.push_back(createBitcode(mbref, "", 0, inLib));
    return;
  case file_magic::pecoff_executable:
    addNativeDll(mbref, path);
    return;
  default:
    error("unknown file type: " + mbref.getBufferIdentifier());
  }
}

std::optional<std::string> findFile(StringRef dir, const Twine &name) {
  SmallString<128> s;
  path::append(s, dir, name);
  if (fs::exists(s))
    return std::string(s);
  return std::nullopt;
}

// Design.md 5.2: per search directory, libfoo.a, then foo.dll (managed with
// export metadata, or native), then libfoo.so (native, not in this prototype).
void addLibrary(StringRef name) {
  for (StringRef dir : ctx.arg.searchPaths) {
    if (std::optional<std::string> s = findFile(dir, "lib" + name + ".a")) {
      addFile(saver().save(*s));
      return;
    }
    if (ctx.arg.isStatic)
      continue;
    if (std::optional<std::string> s = findFile(dir, name + ".dll")) {
      addFile(saver().save(*s));
      return;
    }
  }
  error("unable to find library -l" + name, ErrorTag::LibNotFound, {name});
}

void createFiles(opt::InputArgList &args) {
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
  if (ctx.files.empty() && errorCount() == 0)
    error("no input files");
}

//===-- LTO through the MSIL TargetMachine --------------------------------===//

// Gives every declaration that the linker resolved to a native DLL the
// attribute that the MSIL backend turns into a P/Invoke (Phase-1 contract,
// plan D14). Runs first in the codegen pass manager.
struct AnnotateNativeImports : public ModulePass {
  static char ID;
  AnnotateNativeImports() : ModulePass(ID) {}
  StringRef getPassName() const override { return "msil-ld native imports"; }
  bool runOnModule(Module &m) override {
    bool changed = false;
    for (Function &f : m) {
      if (!f.isDeclaration() || f.isIntrinsic() ||
          f.hasFnAttribute("msil-import-module"))
        continue;
      Symbol *s = find(f.getName());
      if (!s || s->kind != Symbol::Native)
        continue;
      f.addFnAttr("msil-import-module", s->nativeLib);
      changed = true;
    }
    return changed;
  }
};
char AnnotateNativeImports::ID = 0;

void compileBitcode() {
  lto::Config c;
  c.LibraryOptions = &ctx.arg.libraryOptions;
  c.PTO = PipelineTuningOptions(c.LibraryOptions);
  c.Options = initTargetOptionsFromCodeGenFlags();
  c.DiagHandler = diagnosticHandler;
  c.OptLevel = ctx.arg.ltoo;
  c.CGOptLevel = ctx.arg.ltoCgo;
  c.CPU = getCPUStr();
  c.MAttrs = getMAttrs();
  c.RelocModel = Reloc::Static;
  c.OverrideTriple = ctx.arg.triple; // Retarget native-triple bitcode.
  c.CGFileType = CodeGenFileType::AssemblyFile;
  c.PreCodeGenPassesHook = [](legacy::PassManager &pm) {
    pm.add(new AnnotateNativeImports());
  };
  lto::LTO lto(std::move(c),
               lto::createInProcessThinBackend(
                   llvm::heavyweight_hardware_concurrency()),
               /*ParallelCodeGenParallelismLevel=*/1);

  for (InputFile *f : ctx.ltoFiles) {
    std::vector<lto::SymbolResolution> resols(f->symbols.size());
    size_t i = 0;
    for (const lto::InputFile::Symbol &objSym : f->obj->symbols()) {
      Symbol *s = f->symbols[i];
      lto::SymbolResolution &r = resols[i++];
      r.Prevailing = !objSym.isUndefined() && s->kind == Symbol::Defined &&
                     s->file == f;
      // The generated '<msil>Main' calls main; -shared exports every
      // default-visibility definition (Design.md 5.4).
      r.VisibleToRegularObj =
          r.Prevailing &&
          ((!ctx.arg.shared && s->name == "main") ||
           (ctx.arg.shared &&
            objSym.getVisibility() == GlobalValue::DefaultVisibility));
    }
    checkError(lto.add(std::move(f->obj), resols));
  }

  std::vector<SmallString<0>> bufs(lto.getMaxTasks());
  checkError(lto.run(
      [&](size_t task, const Twine &) {
        return std::make_unique<CachedFileStream>(
            std::make_unique<raw_svector_ostream>(bufs[task]));
      },
      FileCache()));
  if (errorCount())
    return;

  unsigned nonEmpty = 0;
  for (auto &b : bufs)
    nonEmpty += !b.empty();
  if (nonEmpty != 1) {
    // ThinLTO or LTO partitions give one IL module each; merging them needs
    // the object format and the PE writer (P2-A/P2-B).
    error("LTO produced " + Twine(nonEmpty) +
          " modules; this prototype writes exactly one");
    return;
  }
  for (auto &b : bufs)
    if (!b.empty())
      saveBuffer(b, ctx.arg.outputFile);
}

//===-- Driver ------------------------------------------------------------===//

std::string createResponseFile(const opt::InputArgList &args) {
  SmallString<0> data;
  raw_svector_ostream os(data);
  for (auto *arg : args) {
    switch (arg->getOption().getID()) {
    case OPT_reproduce:
      break;
    case OPT_INPUT:
      os << quote(relativeToRoot(arg->getValue())) << "\n";
      break;
    case OPT_o:
      os << "-o " << quote(path::filename(arg->getValue())) << "\n";
      break;
    default:
      os << toString(*arg) << "\n";
    }
  }
  return std::string(data);
}

void writeMapFile() {
  if (ctx.arg.mapFile.empty())
    return;
  std::error_code ec;
  raw_fd_ostream os(ctx.arg.mapFile, ec, fs::OF_None);
  if (ec) {
    error("cannot open map file " + ctx.arg.mapFile + ": " + ec.message());
    return;
  }
  os << "symbol\tkind\tfile\n";
  for (Symbol *s : ctx.symbols) {
    if (s->kind == Symbol::Defined)
      os << s->name << "\tdefined" << (s->weak ? "-weak" : "") << '\t'
         << fileName(s->file) << '\n';
    else if (s->kind == Symbol::Native)
      os << s->name << "\tpinvoke\t" << s->nativeLib << '\n';
  }
}

void linkerMain(ArrayRef<const char *> argsArr) {
  MSILOptTable parser;
  opt::InputArgList args = parser.parse(argsArr.slice(1));
  errorHandler().errorLimit = args::getInteger(args, OPT_error_limit, 20);
  errorHandler().verbose = args.hasArg(OPT_verbose);

  if (args.hasArg(OPT_help)) {
    parser.printHelp(errorHandler().outs(),
                     (std::string(argsArr[0]) + " [options] file...").c_str(),
                     "LLVM Linker for MSIL (prototype)", false);
    return;
  }
  if (args.hasArg(OPT_v) || args.hasArg(OPT_version))
    errorHandler().outs() << getLLDVersion() << " (msil-ld prototype)\n";
  if (args.hasArg(OPT_version) || (args.hasArg(OPT_v) && !args.hasArg(OPT_INPUT)))
    return;

  if (auto *arg = args.getLastArg(OPT_reproduce)) {
    StringRef path = arg->getValue();
    Expected<std::unique_ptr<TarWriter>> tw =
        TarWriter::create(path, path::stem(path));
    if (tw) {
      ctx.tar = std::move(*tw);
      ctx.tar->append("response.txt", createResponseFile(args));
      ctx.tar->append("version.txt", getLLDVersion() + "\n");
    } else {
      error("--reproduce: " + toString(tw.takeError()));
    }
  }

  std::vector<const char *> v;
  v.push_back("msil-ld (LLVM option parsing)");
  for (auto *arg : args.filtered(OPT_mllvm))
    v.push_back(arg->getValue());
  cl::ResetAllOptionOccurrences();
  cl::parseCommandLineOptions(ctx.arg.libraryOptions, v);

  ctx.arg.outputFile = args.getLastArgValue(OPT_o);
  ctx.arg.searchPaths = args::getStrings(args, OPT_library_path);
  ctx.arg.emitAsm = args.hasArg(OPT_lto_emit_asm);
  ctx.arg.shared = args.hasArg(OPT_shared);
  ctx.arg.allowMultipleDefinition =
      args.hasFlag(OPT_allow_multiple_definition,
                   OPT_no_allow_multiple_definition, false);
  ctx.arg.ltoo = args::getInteger(args, OPT_lto_O, 2);
  if (auto level = CodeGenOpt::getLevel(args::getCGOptLevel(ctx.arg.ltoo)))
    ctx.arg.ltoCgo = *level;
  ctx.arg.mapFile = args.getLastArgValue(OPT_Map);
  ctx.arg.whyExtract = args.getLastArgValue(OPT_why_extract);
  ctx.arg.references = args::getStrings(args, OPT_reference);
  if (ctx.arg.outputFile.empty())
    error("no output file specified");
  if (!ctx.arg.emitAsm)
    error("writing the assembly needs the PE writer; this prototype supports "
          "only --lto-emit-asm");

  // The codegen triple: --target, else the first bitcode input's. A native
  // triple names the -msil triple derived from it.
  if (auto *arg = args.getLastArg(OPT_target))
    ctx.arg.triple = Triple::normalize(arg->getValue());
  if (errorCount())
    return;

  createFiles(args);
  if (errorCount())
    return;
  if (ctx.arg.triple.empty())
    for (InputFile *f : ctx.files)
      if (f->obj) {
        ctx.arg.triple = f->obj->getTargetTriple();
        break;
      }
  if (!Triple(ctx.arg.triple).isOSBinFormatMSIL())
    ctx.arg.triple += "-msil";
  if (!Triple(ctx.arg.triple).isOSBinFormatMSIL()) {
    error("cannot derive an MSIL triple from '" + ctx.arg.triple + "'");
    return;
  }
  log("codegen triple: " + ctx.arg.triple);

  // Resolution order (Design.md 5.2): objects and archives first, in
  // command-line order with lazy extraction ...
  for (InputFile *f : ctx.files) {
    if (!f->lazy) {
      parseBitcode(f);
      continue;
    }
    for (const lto::InputFile::Symbol &objSym : f->obj->symbols())
      if (!objSym.isUndefined())
        addLazy(saver().save(objSym.getName()), f);
  }
  if (errorCount())
    return;
  // ... then native shared libraries for whatever is still undefined.
  for (Symbol *s : ctx.symbols) {
    if (s->kind != Symbol::Undefined)
      continue;
    auto it = ctx.nativeExports.find(s->name);
    if (it != ctx.nativeExports.end()) {
      s->kind = Symbol::Native;
      s->nativeLib = it->second;
      continue;
    }
    if (!s->weak)
      error("undefined symbol: " + s->name + "\n>>> referenced by " +
                fileName(s->file),
            ErrorTag::SymbolNotFound, {s->name});
  }
  if (!ctx.arg.whyExtract.empty()) {
    std::error_code ec;
    raw_fd_ostream os(ctx.arg.whyExtract, ec, fs::OF_None);
    os << "reference\textracted\tsymbol\n";
    for (auto &[ref, f, sym] : ctx.whyExtract)
      os << ref << '\t' << fileName(f) << '\t' << sym << '\n';
  }
  writeMapFile();
  if (errorCount())
    return;
  compileBitcode();
}

} // namespace

bool link(ArrayRef<const char *> args, raw_ostream &stdoutOS,
          raw_ostream &stderrOS, bool exitEarly, bool disableOutput) {
  auto *context = new CommonLinkerContext;
  context->e.initialize(stdoutOS, stderrOS, exitEarly, disableOutput);
  context->e.cleanupCallback = []() {
    ctx.~Ctx();
    new (&ctx) Ctx();
  };
  context->e.logName = args::getFilenameWithoutExe(args[0]);
  context->e.errorLimitExceededMsg = "too many errors emitted, stopping now "
                                     "(use --error-limit=0 to see all errors)";
  // LTO compiles bitcode through the MSIL TargetMachine only; the AsmPrinter
  // is needed for --lto-emit-asm (and later for the object writer).
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  linkerMain(args);
  return errorCount() == 0;
}

} // namespace lld::msil
