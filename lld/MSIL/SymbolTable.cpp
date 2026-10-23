//===- SymbolTable.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/SymbolTable.cpp at 64f50ffd4f1c.
//
//===----------------------------------------------------------------------===//

#include "SymbolTable.h"
#include "Config.h"
#include "InputFiles.h"
#include "LTO.h"
#include "NativeLibraries.h"
#include "lld/Common/CommonLinkerContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/StringSaver.h"
#include <optional>

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::ELF;

namespace lld::msil {
SymbolTable *symtab;

void SymbolTable::addFile(InputFile *file, StringRef symName) {
  log("Processing: " + toString(file));

  // A native library: its definitions bind late, after every input is loaded
  // (plan 5.9.6 step 2), so it adds nothing to the symbol table.
  if (auto *f = dyn_cast<NativeLibraryFile>(file)) {
    if (ctx.arg.trace)
      message(toString(file));
    ctx.nativeLibraryFiles.push_back(f);
    ctx.loadedFiles.push_back(f);
    ctx.nativeIndex.addFile(*f, ctx.arg.target);
    return;
  }

  // The other inputs are LLVM bitcode files (M20 adds CIL objects).
  auto *f = cast<BitcodeFile>(file);

  // Lazy object file
  if (file->lazy) {
    ctx.lazyBitcodeFiles.push_back(f);
    f->parseLazy();
    return;
  }

  if (doneLTO) {
    error(toString(file) + ": attempt to add bitcode file after LTO (" +
          symName + ")");
    return;
  }

  if (ctx.arg.trace)
    message(toString(file));

  // LLVM bitcode file
  // This order, first adding to `bitcodeFiles` and then parsing is necessary.
  // See https://github.com/llvm/llvm-project/pull/73095
  ctx.bitcodeFiles.push_back(f);
  ctx.loadedFiles.push_back(f);
  f->parse();
}

// This function is where all the optimizations of link-time
// optimization happens. When LTO is in use, some input files are
// not in native object file format but in the LLVM bitcode format.
// This function compiles bitcode files into a few big native files
// using LLVM functions and replaces bitcode symbols with the results.
// Because all bitcode files that the program consists of are passed
// to the compiler at once, it can do whole-program optimization.
void SymbolTable::compileBitcodeFiles() {
  // Prevent further LTO objects being included
  doneLTO = true;

  // Collect the bitcode library functions that are not safe to call because
  // they were not yet brought in the link. (Such symbols are lazy.)
  llvm::BumpPtrAllocator alloc;
  llvm::StringSaver saver(alloc);
  SmallVector<StringRef> bitcodeLibFuncs;
  if (!ctx.bitcodeFiles.empty()) {
    // The library functions are the codegen triple's, which LTO compiles for
    // (lto::Config::OverrideTriple); a stock-clang input names the native
    // triple instead. Note that the below assumes that the set of possible
    // libfuncs is equivalent for all bitcode translation units.
    llvm::Triple tt = ctx.arg.target;
    if (tt.str().empty())
      tt = llvm::Triple(ctx.bitcodeFiles.front()->obj->getTargetTriple());
    for (StringRef libFunc : llvm::lto::LTO::getLibFuncSymbols(tt, saver)) {
      if (Symbol *sym = find(libFunc)) {
        if (auto *lazy = dyn_cast<LazySymbol>(sym)) {
          if (isa<BitcodeFile>(lazy->getFile()))
            bitcodeLibFuncs.push_back(libFunc);
        }
      }
    }
  }

  // Compile bitcode files and replace bitcode symbols.
  lto.reset(new BitcodeCompiler);
  lto->setBitcodeLibFuncs(bitcodeLibFuncs);

  for (BitcodeFile *f : ctx.bitcodeFiles)
    lto->add(*f);

  // M14 links only with --lto-emit-asm (the driver reports every other LTO
  // link), and then compile() writes the output itself. LTO objects are CIL
  // objects, which M20 parses here.
  if (!lto->compile().empty())
    fatal("LTO produced CIL objects, which this msil-ld cannot link (M20)");
}

Symbol *SymbolTable::find(StringRef name) {
  auto it = symMap.find(CachedHashStringRef(name));
  if (it == symMap.end() || it->second == -1)
    return nullptr;
  Symbol *sym = symVector[it->second];
  return sym->isPlaceholder() ? nullptr : sym;
}

Symbol *SymbolTable::insert(StringRef name) {
  bool trace = false;
  auto p = symMap.insert({CachedHashStringRef(name), (int)symVector.size()});
  int &symIndex = p.first->second;
  bool isNew = p.second;
  if (symIndex == -1) {
    symIndex = symVector.size();
    trace = true;
    isNew = true;
  }

  if (!isNew)
    return symVector[symIndex];

  // make<SymbolUnion>() value-initializes the storage, so the Symbol fields
  // are zero: a PlaceholderKind entry without a file. Set the ones that need
  // a non-zero value.
  Symbol *sym = reinterpret_cast<Symbol *>(make<SymbolUnion>());
  sym->setName(name);
  sym->traced = trace;
  symVector.push_back(sym);
  return sym;
}

static StringRef getTypeName(const Symbol &sym) {
  return sym.isFunc() ? "function" : "data";
}

// The kind checks of plan 5.9.5: one name is not a function in one input and
// data in another (wasm-ld's rule and text; lld/ELF does not check types), and
// not TLS in one input and non-TLS in another
// (lld/ELF/InputFiles.cpp:1390-1399). A lazy symbol is not a definition yet:
// it has the kind of the weak reference that met it, and the check names that
// reference's file, or it has no type. A symbol without a type (a lazy symbol
// that no weak reference met, -u, the entry) matches anything. Returns false
// after reporting a mismatch.
static bool checkKind(const Symbol &existing, const Symbol &newSym) {
  if (existing.isPlaceholder() || existing.type == STT_NOTYPE ||
      newSym.type == STT_NOTYPE)
    return true;
  const InputFile *existingFile = existing.file;
  if (auto *lazy = dyn_cast<LazySymbol>(&existing)) {
    if (!lazy->weakRefFile)
      return true;
    existingFile = lazy->weakRefFile;
  }
  if (existing.isFunc() != newSym.isFunc()) {
    error("symbol type mismatch: " + toString(existing) + "\n>>> defined as " +
          getTypeName(existing) + " in " + toString(existingFile) +
          "\n>>> defined as " + getTypeName(newSym) + " in " +
          toString(newSym.file));
    return false;
  }
  if (existing.isTLS() != newSym.isTLS()) {
    error("TLS attribute mismatch: " + toString(existing) + "\n>>> in " +
          toString(existingFile) + "\n>>> in " + toString(newSym.file));
    return false;
  }
  return true;
}

Symbol *SymbolTable::addSymbol(const Symbol &newSym) {
  Symbol *sym = insert(newSym.getName());
  if (!checkKind(*sym, newSym))
    return sym;

  bool wasLazy = sym->isLazy();
  sym->resolve(newSym);
  // A reference that extracted an archive member meets the member's
  // definition only now.
  if (wasLazy && !sym->isLazy() && sym->file != newSym.file)
    checkKind(*sym, newSym);

  // Certain flags such as NO_STRIP should be maintained if either old or
  // new symbol is marked as such.
  if (newSym.noStrip)
    sym->noStrip = true;
  if (newSym.dllImport)
    sym->dllImport = true;
  return sym;
}

Symbol *SymbolTable::addUndefined(StringRef name, StringRef reference) {
  Symbol *sym = insert(name);
  // Since a symbol may not be used inside the program, LTO may
  // eliminate it. Mark the symbol as "used" to prevent it.
  sym->isUsedInRegularObj = true;
  sym->referenced = true;

  // lld/ELF's handleUndefined (lld/ELF/Driver.cpp:2486-2496).
  if (sym->isLazy()) {
    sym->extract();
    if (!ctx.arg.whyExtract.empty())
      ctx.whyExtractRecords.emplace_back(reference.str(), sym->file, *sym);
    return sym;
  }

  // lld/ELF's addUnusedUndefined (lld/ELF/SymbolTable.cpp:324-326).
  sym->resolve(
      Undefined(/*file=*/nullptr, name, STB_GLOBAL, STV_DEFAULT, STT_NOTYPE));
  return sym;
}

Symbol *SymbolTable::addLazy(StringRef name, InputFile *file) {
  return addSymbol(LazySymbol(*file, name));
}

bool SymbolTable::addComdat(StringRef name) {
  return comdatGroups.insert(CachedHashStringRef(uniqueSaver().save(name)))
      .second;
}

void SymbolTable::trace(StringRef name) {
  auto p = symMap.insert({CachedHashStringRef(name), -1});
  if (!p.second && p.first->second != -1)
    symVector[p.first->second]->traced = true;
}

// Returns the definitions in \p file whose code or initializer uses the global
// value \p irName, in module order: the methods (and data) of ">>> referenced
// by <file>:(<method>)". A bitcode symbol table does not record them, so the
// file's IR is read again; this runs only for a diagnostic, also after LTO
// has taken the file's lto::InputFile.
static SmallVector<std::string, 0>
findReferencingDefinitions(BitcodeFile &file, StringRef irName) {
  SmallVector<std::string, 0> ret;
  if (irName.empty())
    return ret;
  Expected<std::vector<BitcodeModule>> modsOrErr =
      getBitcodeModuleList(file.getMemoryBufferRef());
  if (!modsOrErr) {
    consumeError(modsOrErr.takeError());
    return ret;
  }
  for (BitcodeModule &bm : *modsOrErr) {
    LLVMContext context;
    Expected<std::unique_ptr<Module>> mOrErr = bm.parseModule(context);
    if (!mOrErr) {
      consumeError(mOrErr.takeError());
      continue;
    }
    const GlobalValue *gv = (*mOrErr)->getNamedValue(irName);
    if (!gv)
      continue;

    // The users of gv: instructions name their function, a global variable's
    // initializer the variable, and constant expressions are looked through.
    SmallPtrSet<const GlobalValue *, 8> definitions;
    SmallPtrSet<const User *, 16> visited;
    SmallVector<const User *, 16> worklist(gv->users());
    while (!worklist.empty()) {
      const User *u = worklist.pop_back_val();
      if (!visited.insert(u).second)
        continue;
      if (auto *inst = dyn_cast<Instruction>(u))
        definitions.insert(inst->getFunction());
      else if (auto *g = dyn_cast<GlobalValue>(u))
        definitions.insert(g);
      else
        append_range(worklist, u->users());
    }
    // Intrinsic globals (llvm.used, llvm.global_ctors) are not definitions of
    // the program.
    for (const GlobalValue &g : (*mOrErr)->global_values())
      if (definitions.contains(&g) && !g.getName().starts_with("llvm."))
        ret.push_back(GlobalValue::dropLLVMManglingEscape(g.getName()).str());
  }
  return ret;
}

// Reports an unresolved symbol in lld/ELF's form (reportUndefinedSymbol,
// lld/ELF/Relocations.cpp:550-634): \p msg, then up to three ">>> referenced
// by" lines and the number of further references.
void SymbolTable::reportUnresolved(std::string msg, ArrayRef<Reference> refs,
                                   bool isEntry) {
  SmallVector<std::string, 0> locs;
  if (isEntry)
    locs.push_back("the entry point");
  for (const Reference &ref : refs) {
    SmallVector<std::string, 0> defs =
        findReferencingDefinitions(*ref.file, ref.irName);
    if (defs.empty())
      locs.push_back(toString(ref.file));
    for (const std::string &def : defs)
      locs.push_back(toString(ref.file) + ":(" + def + ")");
  }

  const size_t maxUndefReferences = 3;
  for (const std::string &loc : ArrayRef(locs).take_front(maxUndefReferences))
    msg += "\n>>> referenced by " + loc;
  if (maxUndefReferences < locs.size())
    msg += "\n>>> referenced " +
           std::to_string(locs.size() - maxUndefReferences) + " more times";
  error(msg);
}

// The first line of the report of an unresolved symbol (plan 5.9.14).
static std::string getUnresolvedMessage(const Symbol &sym) {
  if (auto *u = dyn_cast<Undefined>(&sym); u && u->discarded)
    return "relocation refers to a symbol in a discarded section: " +
           toString(sym) + "\n>>> defined in " + toString(sym.file);
  switch (sym.visibility()) {
  case STV_HIDDEN:
  case STV_INTERNAL:
    return "undefined hidden symbol: " + toString(sym);
  case STV_PROTECTED:
    return "undefined protected symbol: " + toString(sym);
  default:
    return "undefined symbol: " + toString(sym);
  }
}

ArrayRef<SymbolTable::Reference> SymbolTable::getReferences(const Symbol *sym) {
  // The inputs that refer to each symbol, in load order: the undefined
  // symbols of the bitcode files' symbol tables. Collected once, before LTO
  // takes the files' lto::InputFiles; the IR names are saved for the same
  // reason.
  if (!haveReferences) {
    haveReferences = true;
    for (BitcodeFile *f : ctx.bitcodeFiles) {
      if (!f->obj)
        continue;
      ArrayRef<Symbol *> syms = f->getSymbols();
      for (auto [i, irSym] : llvm::enumerate(f->obj->symbols()))
        if (irSym.isUndefined() && i < syms.size() && syms[i])
          references[syms[i]].push_back({f, saver().save(irSym.getIRName())});
    }
  }
  auto it = references.find(sym);
  if (it == references.end())
    return {};
  return it->second;
}

void SymbolTable::reportReference(std::string msg, StringRef name) {
  Symbol *sym = find(name);
  ArrayRef<Reference> refs = sym ? getReferences(sym) : ArrayRef<Reference>();
  if (refs.empty()) {
    error(msg + "\n>>> referenced by ld-temp.o");
    return;
  }
  reportUnresolved(std::move(msg), refs, /*isEntry=*/false);
}

std::string SymbolTable::getReferencingFile(StringRef name) {
  Symbol *sym = find(name);
  ArrayRef<Reference> refs = sym ? getReferences(sym) : ArrayRef<Reference>();
  if (refs.empty())
    return "ld-temp.o";
  return toString(refs.front().file);
}

// Step 2 of plan 5.9.6: binds \p sym to the first bindable definition of its
// name in the native libraries, in link order (plan 5.10.10). A definition
// without a type takes the kind of the reference, and a reference without a
// type (-u) takes a function: it is a function reference, which passes over
// the data of COFF import libraries and DLLs.
static void bindNative(Symbol &sym) {
  SmallVector<const NativeExport *, 2> rejected;
  const NativeExport *e = ctx.nativeIndex.lookup(
      sym.getName(), rejected, /*isFunctionReference=*/!sym.isData());
  if (!e)
    return;
  bool isFunction = e->Kind == NativeKind::Function ||
                    (e->Kind == NativeKind::NoType && !sym.isData());
  if (isFunction)
    NativeFunction(e->File, sym.getName(), sym.binding, sym.visibility(), e)
        .overwrite(sym);
  else
    NativeData(e->File, sym.getName(), sym.binding, sym.visibility(), e)
        .overwrite(sym);
}

// The sections that definitions of the loaded bitcode files name (their
// irsymtab section names).
static DenseSet<StringRef> getDefinedSections() {
  DenseSet<StringRef> sections;
  for (BitcodeFile *f : ctx.bitcodeFiles) {
    if (!f->obj)
      continue;
    for (const lto::InputFile::Symbol &irSym : f->obj->symbols())
      if (!irSym.isUndefined() && !irSym.getSectionName().empty())
        sections.insert(saver().save(irSym.getSectionName()));
  }
  return sections;
}

void SymbolTable::bindLateSymbols() {
  // Record the references now: LTO takes the inputs' symbol tables, and the
  // import-annotation pass reports with them after LTO.
  (void)getReferences(nullptr);

  // On non-Windows triples whole-program code generation defines
  // __start_<X> and __stop_<X> when a definition is in section <X>, so they
  // are neither bound nor reported (plan 5.9.6, 5.9.9).
  std::optional<DenseSet<StringRef>> definedSections;
  auto isSectionBoundary = [&](StringRef name) {
    if (ctx.arg.target.isOSWindows())
      return false;
    if (!name.consume_front("__start_") && !name.consume_front("__stop_"))
      return false;
    if (!definedSections)
      definedSections = getDefinedSections();
    return definedSections->contains(name);
  };

  // The entry symbol must be defined (plan 5.9.6). Without -shared the driver
  // names it, main by default, and adds it with addUndefined(). It never
  // binds late.
  Symbol *entrySym = nullptr;
  if (!ctx.arg.entry.empty() && !ctx.arg.shared) {
    entrySym = find(ctx.arg.entry);
    if (!entrySym)
      reportUnresolved("undefined symbol: " + ctx.arg.entry.str(), {},
                       /*isEntry=*/true);
    else if (entrySym->isUndefined() || entrySym->isLazy())
      reportUnresolved(getUnresolvedMessage(*entrySym), getReferences(entrySym),
                       /*isEntry=*/true);
  }

  for (Symbol *sym : symVector) {
    if (sym == entrySym)
      continue;
    // Defined and common symbols are resolved. A lazy symbol that only weak
    // references met is an undefined weak symbol; a lazy symbol that nothing
    // referred to stays as it is.
    bool weakLazy = sym->isLazy() && sym->isWeak();
    if (!sym->isUndefined() && !weakLazy)
      continue;
    auto *u = dyn_cast<Undefined>(sym);
    bool discarded = u && u->discarded;

    // Step 2: the native libraries, by the symbol name. Whatever stays
    // undefined goes to LTO: a weak symbol keeps M12's lowering (null, and a
    // trap stub for calls), and the import-annotation pass reports any other
    // that the merged module still refers to (plan 5.9.6, 5.10.13).
    if (!discarded && sym->visibility() == STV_DEFAULT) {
      if (!isSectionBoundary(sym->getName()))
        bindNative(*sym);
      continue;
    }

    // A weak symbol is never reported, as in lld/ELF (maybeReportUndefined).
    if (sym->isWeak())
      continue;
    // Nothing refers to it: a discarded COMDAT copy that nothing else refers
    // to.
    ArrayRef<Reference> symRefs = getReferences(sym);
    if (symRefs.empty())
      continue;
    reportUnresolved(getUnresolvedMessage(*sym), symRefs, /*isEntry=*/false);
  }
}
} // namespace lld::msil
