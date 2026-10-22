//===- LTO.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/LTO.cpp at 64f50ffd4f1c.
//
// LTO compiles the bitcode inputs for the codegen triple (plan 5.9.13):
// stock-clang bitcode for the native triple is compiled for its '-msil' twin
// (lto::Config::OverrideTriple), and MSILLinkHooksPass hands msil-ld's
// decisions to the MSIL backend as module flags of the merged module.
//
//===----------------------------------------------------------------------===//

#include "LTO.h"
#include "Config.h"
#include "InputFiles.h"
#include "Symbols.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Strings.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/LTO/Config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Pass.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using namespace lld::msil;
using namespace lld;

namespace {
/// msil-ld's hooks in LTO's code generator (plan 5.9.13), a legacy pass that
/// lto::Config::PreCodeGenPassesHook adds ahead of the MSIL backend's passes.
/// In M14 it sets two module flags of the merged module and nothing else:
/// "msil-assembly-name" (MDString, behavior Override), the assembly name that
/// the AsmPrinter prints (decision D28 as amended for M14; LTO names the
/// merged module ld-temp.o), and "msil-lto" (i32 1, behavior Override), which
/// makes the backend report an undefined function that the standalone import
/// table does not import as msil-ld's "undefined symbol: <name>" (plan
/// 5.9.6). M15 makes it the import-annotation pass of plan 5.10.13.
class MSILLinkHooksPass final : public ModulePass {
public:
  static char ID;

  explicit MSILLinkHooksPass(std::string assemblyName)
      : ModulePass(ID), assemblyName(std::move(assemblyName)) {}

  StringRef getPassName() const override { return "msil-ld link hooks"; }

  // The flags are set while the pass manager initializes its passes, not in
  // runOnModule: the legacy pass manager initializes every pass of the code
  // generator before it runs the first one, and the AsmPrinter prints the
  // assembly name when it is initialized (emitStartOfAsmFile). This pass
  // comes first, so its initialization runs first.
  bool doInitialization(Module &m) override {
    LLVMContext &context = m.getContext();
    if (!assemblyName.empty())
      m.setModuleFlag(Module::Override, "msil-assembly-name",
                      MDString::get(context, assemblyName));
    m.setModuleFlag(Module::Override, "msil-lto", 1);
    return true;
  }

  bool runOnModule(Module &) override { return false; }

  void getAnalysisUsage(AnalysisUsage &au) const override {
    au.setPreservesAll();
  }

private:
  std::string assemblyName;
};
} // namespace

char MSILLinkHooksPass::ID = 0;

// The name of the output assembly: --assembly-name or -soname, else the stem
// of -o (only the last extension is dropped, so -o My.App.exe names the
// assembly My.App).
static std::string getAssemblyName() {
  if (!ctx.arg.assemblyName.empty())
    return ctx.arg.assemblyName.str();
  StringRef stem = sys::path::stem(ctx.arg.outputFile);
  if (stem.empty())
    stem = sys::path::filename(ctx.arg.outputFile);
  return stem.str();
}

static lto::Config createConfig() {
  lto::Config c;
  c.LibraryOptions = &ctx.arg.libraryOptions;
  c.PTO = PipelineTuningOptions(c.LibraryOptions);
  c.Options = initTargetOptionsFromCodeGenFlags();

  // Always emit a section per function/data with LTO.
  c.Options.FunctionSections = true;
  c.Options.DataSections = true;

  c.DisableVerify = ctx.arg.disableVerify;
  c.DiagHandler = diagnosticHandler;
  c.OptLevel = ctx.arg.ltoo;
  c.CPU = getCPUStr();
  c.MAttrs = getMAttrs();
  c.CGOptLevel = ctx.arg.ltoCgo;

  // The codegen triple, an MSIL triple: inputs whose triple is its native
  // twin (stock-clang bitcode) are compiled for it too (lld-flavor 3.3).
  c.OverrideTriple = ctx.arg.target.str();

  // The MSIL backend computes every address at run time (decision D8).
  if (auto relocModel = getRelocModelFromCMModel())
    c.RelocModel = *relocModel;
  else
    c.RelocModel = Reloc::Static;

  // --lto-emit-asm writes the ILAsm text of the whole program, with the
  // comments that llc and clang -S print (lld/ELF/LTO.cpp:169-172).
  if (ctx.arg.ltoEmitAsm) {
    c.CGFileType = CodeGenFileType::AssemblyFile;
    c.Options.MCOptions.AsmVerbose = true;
  }

  c.PreCodeGenPassesHook = [name = getAssemblyName()](legacy::PassManager &pm) {
    pm.add(new MSILLinkHooksPass(name));
  };

  if (ctx.arg.saveTemps)
    checkError(c.addSaveTemps(ctx.arg.outputFile.str() + ".",
                              /*UseInputModulePath*/ true));
  return c;
}

namespace lld::msil {
BitcodeCompiler::BitcodeCompiler() {
  // Initialize ltoObj. ThinLTO input is linked only into CIL objects (M20),
  // and the driver rejects it before LTO runs.
  auto onIndexWrite = [&](StringRef s) { thinIndices.erase(s); };
  lto::ThinBackend backend = lto::createInProcessThinBackend(
      llvm::heavyweight_hardware_concurrency(ctx.arg.thinLTOJobs),
      onIndexWrite);
  ltoObj = std::make_unique<lto::LTO>(createConfig(), backend,
                                      ctx.arg.ltoPartitions);
}

BitcodeCompiler::~BitcodeCompiler() = default;

void BitcodeCompiler::add(BitcodeFile &f) {
  lto::InputFile &obj = *f.obj;
  unsigned symNum = 0;
  ArrayRef<Symbol *> syms = f.getSymbols();
  std::vector<lto::SymbolResolution> resols(syms.size());

  // Provide a resolution to the LTO API for each symbol.
  for (const lto::InputFile::Symbol &objSym : obj.symbols()) {
    Symbol *sym = syms[symNum];
    lto::SymbolResolution &r = resols[symNum];
    ++symNum;

    // Ideally we shouldn't check for SF_Undefined but currently IRObjectFile
    // reports two symbols for module ASM defined. Without this check, lld
    // flags an undefined in IR with a definition in ASM as prevailing.
    // Once IRObjectFile is fixed to report only one symbol this hack can
    // be removed.
    //
    // A definition in a discarded COMDAT copy enters the table as an
    // undefined symbol of the file that discarded it, so the symbol must
    // also be defined (or common) for this file's copy to prevail.
    r.Prevailing = !objSym.isUndefined() &&
                   (sym->isDefined() || sym->isCommon()) &&
                   sym->getFile() == &f;
    // Only the entry symbol, -u symbols and NO_STRIP symbols stay visible:
    // every other definition is internal to the merged module. There is no
    // --wrap (LinkerRedefined) in msil-ld.
    r.VisibleToRegularObj = sym->isUsedInRegularObj || sym->isNoStrip();
    // lld/wasm undefines the prevailing symbol here, so that the LTO object
    // defines it. Under --lto-emit-asm there is no LTO object, and the map
    // file shows the bitcode definitions; M20 parses LTO objects.
  }
  checkError(ltoObj->add(std::move(f.obj), resols));
}

void BitcodeCompiler::setBitcodeLibFuncs(ArrayRef<StringRef> bitcodeLibFuncs) {
  ltoObj->setBitcodeLibFuncs(bitcodeLibFuncs);
}

// Merge all the bitcode files we have seen, codegen the result
// and return the resulting objects.
SmallVector<InputFile *, 0> BitcodeCompiler::compile() {
  unsigned maxTasks = ltoObj->getMaxTasks();
  buf.resize(maxTasks);
  files.resize(maxTasks);
  filenames.resize(maxTasks);

  // The --thinlto-cache-dir option specifies the path to a directory in which
  // to cache native object files for ThinLTO incremental builds. If a path was
  // specified, configure LTO to use it as the cache directory.
  FileCache cache;
  if (!ctx.arg.thinLTOCacheDir.empty())
    cache = check(localCache("ThinLTO", "Thin", ctx.arg.thinLTOCacheDir,
                             [&](size_t task, const Twine &moduleName,
                                 std::unique_ptr<MemoryBuffer> mb) {
                               files[task] = std::move(mb);
                               filenames[task] = moduleName.str();
                             }));

  checkError(ltoObj->run(
      [&](size_t task, const Twine &moduleName) {
        buf[task].first = moduleName.str();
        return std::make_unique<CachedFileStream>(
            std::make_unique<raw_svector_ostream>(buf[task].second));
      },
      cache));

  if (!ctx.arg.thinLTOCacheDir.empty())
    check(pruneCache(ctx.arg.thinLTOCacheDir, CachePruningPolicy(), files));

  // --lto-emit-asm: the ILAsm text of the whole program goes to -o. LTO has
  // one task, the merged module: ThinLTO input and more than one partition
  // give one module per task, which only CIL objects can link, and the driver
  // reports them before LTO runs. Like lld's writers, msil-ld writes no
  // output after an error (the backend reports undefined functions), and the
  // runs of LLD_IN_TEST that print nothing do not write standard output.
  if (ctx.arg.ltoEmitAsm) {
    assert(maxTasks == 1 && "ThinLTO and LTO partitions need CIL objects");
    if (errorCount() == 0 &&
        !(errorHandler().disableOutput && ctx.arg.outputFile == "-"))
      saveBuffer(buf[0].second, ctx.arg.outputFile);
    return {};
  }

  SmallVector<InputFile *, 0> ret;
  for (unsigned i = 0; i != maxTasks; ++i) {
    StringRef objBuf = buf[i].second;
    StringRef bitcodeFilePath = buf[i].first;
    if (files[i]) {
      // When files[i] is not null, we get the native relocatable file from the
      // cache. filenames[i] contains the original BitcodeFile's identifier.
      objBuf = files[i]->getBuffer();
      bitcodeFilePath = filenames[i];
    } else {
      objBuf = buf[i].second;
      bitcodeFilePath = buf[i].first;
    }
    if (objBuf.empty())
      continue;

    // If the input bitcode file is path/to/x.o and -o specifies a.out, the
    // corresponding native relocatable file path will look like:
    // path/to/a.out.lto.x.o.
    StringRef ltoObjName;
    if (bitcodeFilePath == "ld-temp.o") {
      ltoObjName =
          saver().save(Twine(ctx.arg.outputFile) + ".lto" +
                       (i == 0 ? Twine("") : Twine('.') + Twine(i)) + ".o");
    } else {
      StringRef directory = sys::path::parent_path(bitcodeFilePath);
      // For an archive member, which has an identifier like "d/a.a(coll.o at
      // 8)" (see BitcodeFile::BitcodeFile), use the filename; otherwise, use
      // the stem (d/a.o => a).
      StringRef baseName = bitcodeFilePath.ends_with(")")
                               ? sys::path::filename(bitcodeFilePath)
                               : sys::path::stem(bitcodeFilePath);
      StringRef outputFileBaseName = sys::path::filename(ctx.arg.outputFile);
      SmallString<256> path;
      sys::path::append(path, directory,
                        outputFileBaseName + ".lto." + baseName + ".o");
      sys::path::remove_dots(path, true);
      ltoObjName = saver().save(path.str());
    }
    if (ctx.arg.saveTemps)
      saveBuffer(objBuf, ltoObjName);
    // CIL objects are M20's: until then createObjectFile reports them.
    if (InputFile *obj = createObjectFile(MemoryBufferRef(objBuf, ltoObjName)))
      ret.push_back(obj);
  }

  return ret;
}
} // namespace lld::msil
