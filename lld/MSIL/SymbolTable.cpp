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
#include "AssemblyFiles.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputFiles.h"
#include "LTO.h"
#include "NativeLibraries.h"
#include "TokenMap.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Strings.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CLI/Signature.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/MathExtras.h"
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
  // (plan 5.9.6 step 2), so it adds nothing to the symbol table. A native
  // object given on the command line adds its definitions as machine code.
  if (auto *f = dyn_cast<NativeLibraryFile>(file)) {
    if (ctx.arg.trace)
      message(toString(file));
    ctx.nativeLibraryFiles.push_back(f);
    ctx.loadedFiles.push_back(f);
    ctx.nativeIndex.addFile(*f, ctx.arg.target);
    return;
  }

  // A library built by this toolchain (plan 5.9.15): its exports bind late,
  // before the native libraries (plan 5.9.6 step 1).
  if (auto *f = dyn_cast<ManagedLibraryFile>(file)) {
    if (ctx.arg.ltoEmitAsm) {
      error(toString(file) + ": --lto-emit-asm cannot import from a .NET "
                             "library; link without --lto-emit-asm");
      return;
    }
    if (!f->parse())
      return;
    // One assembly of a name: the same file again adds nothing.
    for (ManagedLibraryFile *lib : managedLibraries) {
      if (!StringRef(lib->getIdentity().Name)
               .equals_insensitive(f->getIdentity().Name))
        continue;
      if (lib->getMemoryBufferRef().getBuffer() !=
          f->getMemoryBufferRef().getBuffer())
        error(toString(file) + ": library assembly '" + f->getIdentity().Name +
              "' is also linked from " + toString(lib) +
              "; the CLR loads one assembly of a name");
      return;
    }
    // The output is an assembly of that name too: the CLR would resolve the
    // library's TypeRefs to the output itself (foo.exe linked against
    // foo.dll, or a library linked against an older build of itself). The
    // library stays in the list, so that the same file again adds nothing.
    if (StringRef(f->getIdentity().Name)
            .equals_insensitive(getOutputAssemblyName()))
      error(toString(file) + ": library assembly '" + f->getIdentity().Name +
            "' has the name of the output; the CLR loads one assembly of a "
            "name");
    if (ctx.arg.trace)
      message(toString(file));
    managedLibraries.push_back(f);
    ctx.loadedFiles.push_back(f);
    return;
  }

  // A CIL object (plan 5.9.4). Objects may also be extracted after LTO.
  if (auto *f = dyn_cast<ObjFile>(file)) {
    if (f->lazy) {
      f->parseLazy();
      return;
    }
    if (ctx.arg.trace)
      message(toString(file));
    ctx.objectFiles.push_back(f);
    ctx.loadedFiles.push_back(f);
    f->parse();
    return;
  }

  // The other inputs are LLVM bitcode files.
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

  // Under --lto-emit-asm compile() writes the output itself. Otherwise the
  // LTO objects are CIL objects, parsed like any object
  // (lld/wasm/SymbolTable.cpp:112-117); they define the prevailing symbols
  // that BitcodeCompiler::add() made undefined, and late binding runs over
  // them (plan 5.9.6, 5.9.13). They are parsed with COMDATs ignored: the
  // BitcodeFiles claimed every group already and the IR linker kept only the
  // prevailing copies, so all of their groups are kept.
  ObjFile *firstLTO = nullptr;
  for (InputFile *file : lto->compile()) {
    auto *obj = dyn_cast<ObjFile>(file);
    if (!obj) {
      error(toString(file) + ": LTO wrote no CIL object");
      continue;
    }
    obj->isLTO = true;
    if (ctx.arg.trace)
      message(toString(obj));
    ctx.objectFiles.push_back(obj);
    obj->parse(/*ignoreComdats=*/true);
    if (!firstLTO)
      firstLTO = obj;
  }

  // The libraries that checkManagedLibraries() compared with a bitcode model
  // before LTO, which gave only the pointer and long sizes: the first LTO
  // object's header gives their wchar_t and long double sizes, before late
  // binding (plan 5.9.13).
  if (firstLTO)
    for (ManagedLibraryFile *lib : managedLibraries)
      lib->checkLTODataModel(*firstLTO);
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

// Plan 5.9.5's function-or-data check between the reference \p sym (an
// undefined symbol, or a lazy symbol that only weak references met) and the
// linker-defined DATA symbol that the output gives it: __start_<X> and
// __stop_<X> on non-Windows triples (plan 5.9.9), which the writer defines
// for CIL objects (SyntheticData) and whole-program code generation under
// --lto-emit-asm. A function reference is a mismatch, reported as checkKind
// reports one: the reference first, then the linker-defined data, which has
// no file ("<internal>"). Returns false after reporting it.
static bool checkLinkerDefinedKind(const Symbol &sym) {
  if (!sym.isFunc())
    return true;
  return checkKind(sym, SyntheticData(sym.getName(), /*offset=*/0));
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

// The methods and segments of the CIL objects whose token sites or
// relocations name \p sym, as "<file>:(<name>)", in load order: only live
// ones if \p liveOnly, and LTO objects only if \p includeLTO.
static SmallVector<std::string, 0>
getObjectReferences(const Symbol *sym, bool liveOnly, bool includeLTO) {
  SmallVector<std::string, 0> refs;
  if (!sym)
    return refs;
  for (ObjFile *f : ctx.objectFiles) {
    if (f->isLTO && !includeLTO)
      continue;
    const TokenMap &tokens = f->getTokenMap();
    for (InputMethod *m : f->methods()) {
      if (m->discarded || (liveOnly && !m->live))
        continue;
      if (any_of(m->sites, [&](const TokenSite &site) {
            return tokens.lookup(site.token).sym == sym;
          }))
        refs.push_back(toString(m));
    }
    for (InputSegment *seg : f->segments()) {
      if (seg->discarded || (liveOnly && !seg->live))
        continue;
      if (any_of(seg->relocs, [&](const MSILLink::Reloc &r) {
            return f->getSymbol(r.Symbol) == sym ||
                   (r.Type == MSILLink::RelocType::Diff &&
                    f->getSymbol(r.Symbol2) == sym);
          }))
        refs.push_back(toString(seg));
    }
  }
  return refs;
}

// Reports an unresolved symbol in lld/ELF's form (reportUndefinedSymbol,
// lld/ELF/Relocations.cpp:550-634): \p msg, then up to three ">>> referenced
// by" lines and the number of further references: the entry point, the
// bitcode inputs \p refs, then the CIL object locations \p objectRefs.
void SymbolTable::reportUnresolved(std::string msg, ArrayRef<Reference> refs,
                                   bool isEntry,
                                   ArrayRef<std::string> objectRefs) {
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
  append_range(locs, objectRefs);

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

void SymbolTable::reportUnresolved(std::string msg, const Symbol *sym,
                                   bool liveOnly) {
  ArrayRef<Reference> refs = getReferences(sym);
  reportUnresolved(std::move(msg), refs, /*isEntry=*/false,
                   getObjectReferences(sym, liveOnly,
                                       /*includeLTO=*/refs.empty()));
}

void SymbolTable::reportUndefined(const Symbol *sym, bool liveOnly) {
  reportUnresolved(getUnresolvedMessage(*sym), sym, liveOnly);
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

// Step 2 of plan 5.9.6 for the symbols of CIL objects and LTO objects, which
// carry import attributes: an explicit module binds unchecked (plan 5.10.7);
// otherwise the native libraries are searched by the import name, else by
// the symbol name.
void SymbolTable::bindObjectSymbol(Symbol &sym) {
  auto it = importInfo.find(&sym);
  ImportInfo *info = it == importInfo.end() ? nullptr : &it->second;
  if (info && !info->module.empty()) {
    auto *e = make<NativeExport>();
    e->Module = info->module;
    e->Entry = info->name.empty() ? sym.getName() : info->name;
    e->Kind = sym.isData() ? NativeKind::Data : NativeKind::Function;
    e->Why = NotBindable::None;
    e->Version = "";
    e->File = const_cast<InputFile *>(info->moduleFile);
    info->explicitBinding = true;
    if (sym.isData())
      NativeData(e->File, sym.getName(), sym.binding, sym.visibility(), e)
          .overwrite(sym);
    else
      NativeFunction(e->File, sym.getName(), sym.binding, sym.visibility(), e)
          .overwrite(sym);
    return;
  }

  StringRef lookupName =
      info && !info->name.empty() ? info->name : sym.getName();
  SmallVector<const NativeExport *, 2> rejected;
  const NativeExport *e = ctx.nativeIndex.lookup(
      lookupName, rejected, /*isFunctionReference=*/!sym.isData());
  if (!rejected.empty())
    append_range(rejectedNative[&sym], rejected);
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

// A definition of the name of \p sym in a native object given on the
// command line (section 12 of 2026-10-26): a native linker always links such
// an object, so the reference binds its machine code whatever the libraries
// define, built by this toolchain (step 1) or native (step 2). It is kept as
// a definition that late binding passed over, which the writer reports for a
// live reference (plan 5.10.14's "defined only in native object code"). A
// symbol with an explicit import module binds to that module unchecked (plan
// 5.10.7).
bool SymbolTable::rejectObjectDefinition(Symbol &sym) {
  if (!getImportModule(&sym).empty())
    return false;
  const NativeExport *e = ctx.nativeIndex.findObjectDefinition(sym.getName());
  if (!e)
    return false;
  rejectedNative[&sym].push_back(e);
  return true;
}

void SymbolTable::checkManagedLibraries() {
  if (librariesChecked)
    return;
  librariesChecked = true;
  if (managedLibraries.empty())
    return;
  // The model (plan 5.8.9): the first loaded CIL object, else the first
  // bitcode file.
  const InputFile *model = nullptr;
  for (ObjFile *f : ctx.objectFiles)
    if (!f->isLTO) {
      model = f;
      break;
    }
  if (!model && !ctx.bitcodeFiles.empty())
    model = ctx.bitcodeFiles.front();
  for (ManagedLibraryFile *lib : managedLibraries)
    lib->check(model, managedLibraries);

  // A bitcode definition that a library also exports stays visible to
  // regular objects, so that its LTO object keeps it global and late binding
  // warns about it as about a CIL object's (Q21): LTO would otherwise
  // internalize it.
  for (ManagedLibraryFile *lib : managedLibraries)
    for (const ManagedExport *e : lib->exports())
      if (Symbol *sym = find(e->Name))
        if ((sym->isDefined() || sym->isCommon()) &&
            isa_and_nonnull<BitcodeFile>(sym->getFile()) &&
            sym->visibility() != STV_HIDDEN &&
            sym->visibility() != STV_INTERNAL)
          sym->isUsedInRegularObj = true;
}

const ManagedExport *SymbolTable::findManagedExport(StringRef name) const {
  for (ManagedLibraryFile *lib : managedLibraries)
    if (const ManagedExport *e = lib->findExport(name))
      return e;
  return nullptr;
}

// Step 1 of plan 5.9.6 (plan 5.9.15, exports-multiasm 10.2): the first
// library in command-line order that exports the symbol's name.
bool SymbolTable::bindManagedSymbol(Symbol &sym) {
  if (managedLibraries.empty())
    return false;
  // An explicit import module stays native (plan 5.10.7); an import name
  // applies to step 2 only.
  if (!getImportModule(&sym).empty())
    return false;
  const ManagedExport *e = nullptr;
  for (ManagedLibraryFile *lib : managedLibraries) {
    const ManagedExport *other = lib->findExport(sym.getName());
    if (!other)
      continue;
    if (!e) {
      e = other;
      continue;
    }
    warn("symbol '" + toString(sym) + "' is exported by " + toString(e->File) +
         " and " + toString(lib) + "; using " + toString(e->File));
  }
  if (!e)
    return false;

  // The kind checks of plan 5.9.5 between the reference and the export. A
  // lazy symbol carries the kind of the weak reference that met it.
  bool isData = e->isData();
  bool isTLS = isData && (e->Flags & cli::LibraryDataSymbol::ThreadLocal);
  const InputFile *refFile = sym.file;
  if (auto *lazy = dyn_cast<LazySymbol>(&sym))
    refFile = lazy->weakRefFile;
  if (sym.type != STT_NOTYPE && sym.isData() != isData)
    error("symbol type mismatch: " + toString(sym) + "\n>>> defined as " +
          (sym.isFunc() ? "function" : "data") + " in " + toString(refFile) +
          "\n>>> defined as " + (isData ? "data" : "function") + " in " +
          toString(e->File));
  else if (sym.type != STT_NOTYPE && sym.isTLS() != isTLS)
    error("TLS attribute mismatch: " + toString(sym) + "\n>>> in " +
          toString(refFile) + "\n>>> in " + toString(e->File));

  if (isData)
    ManagedData(e->File, sym.getName(), sym.binding, sym.visibility(), e)
        .overwrite(sym);
  else
    ManagedFunction(e->File, sym.getName(), sym.binding, sym.visibility(), e)
        .overwrite(sym);
  return true;
}

// True if the type \p t names a type of its module (a TypeRef value), so
// that equal signatures of two objects may still differ.
static bool namesType(const cli::TypeSig &t);

static bool signatureNamesType(const cli::MethodSig &sig) {
  return namesType(sig.Return) ||
         any_of(sig.Params, [](const cli::TypeSig &p) { return namesType(p); });
}

static bool namesType(const cli::TypeSig &t) {
  if (!t.getModifiers().empty())
    return true;
  switch (t.getElementType()) {
  case cli::ELEMENT_TYPE_CLASS:
  case cli::ELEMENT_TYPE_VALUETYPE:
  case cli::ELEMENT_TYPE_GENERICINST:
    return true;
  case cli::ELEMENT_TYPE_FNPTR:
    return signatureNamesType(t.getFunctionSig());
  default:
    return any_of(t.getOperands(),
                  [](const cli::TypeSig &op) { return namesType(op); });
  }
}

// The sections that definitions of the loaded bitcode files name: their
// irsymtab section names, and the sections of their local definitions, which
// the irsymtab does not list. Code generation defines __start_<X> and
// __stop_<X> for a definition in section <X> whatever its linkage, as the
// writer does for the segment that a static variable gives an object (plan
// 5.9.9), so a hidden reference to them links under --lto-emit-asm as it
// does through objects and object-producing LTO (plan 5.9.6). The modules
// are read lazily, without function bodies, and only when a link refers to
// such a name.
static DenseSet<StringRef> getDefinedSections() {
  DenseSet<StringRef> sections;
  for (BitcodeFile *f : ctx.bitcodeFiles) {
    if (!f->obj)
      continue;
    for (const lto::InputFile::Symbol &irSym : f->obj->symbols())
      if (!irSym.isUndefined() && !irSym.getSectionName().empty())
        sections.insert(saver().save(irSym.getSectionName()));

    Expected<std::vector<BitcodeModule>> modsOrErr =
        getBitcodeModuleList(f->obj->getFileBuffer());
    if (!modsOrErr) {
      consumeError(modsOrErr.takeError());
      continue;
    }
    for (BitcodeModule &bm : *modsOrErr) {
      LLVMContext context;
      Expected<std::unique_ptr<Module>> mOrErr =
          bm.getLazyModule(context, /*ShouldLazyLoadMetadata=*/true,
                           /*IsImporting=*/false);
      if (!mOrErr) {
        consumeError(mOrErr.takeError());
        continue;
      }
      for (const GlobalObject &go : (*mOrErr)->global_objects())
        if (go.hasLocalLinkage() && go.hasSection() &&
            !go.isDeclarationForLinker())
          sections.insert(saver().save(go.getSection()));
    }
  }
  return sections;
}

void SymbolTable::bindLateSymbols() {
  // Record the references now: LTO takes the inputs' symbol tables, and the
  // import-annotation pass reports with them after LTO.
  (void)getReferences(nullptr);

  // When LTO writes CIL objects, late binding runs after LTO over the CIL
  // objects and LTO objects (plan 5.9.6 "When it runs").
  bool objectMode = !ctx.arg.ltoEmitAsm;

  // The libraries built by this toolchain (plan 5.9.15): checked once
  // (normally by recordReferences()), then, Q21's -Bsymbolic semantics, an
  // object definition that a library also exports keeps its own definition,
  // with a warning: the library keeps its copy too.
  checkManagedLibraries();
  if (objectMode && !managedLibraries.empty()) {
    for (Symbol *sym : symVector) {
      if ((!sym->isDefined() && !sym->isCommon()) ||
          !isa_and_nonnull<ObjFile>(sym->getFile()) ||
          (sym->visibility() != STV_DEFAULT &&
           sym->visibility() != STV_PROTECTED))
        continue;
      if (const ManagedExport *e = findManagedExport(sym->getName()))
        warn("symbol '" + toString(*sym) + "' is defined in " +
             toString(sym->getFile()) + " and exported by " +
             toString(e->File) + "; the output uses its own definition");
    }
  }

  // The writer defines __start_<X> and __stop_<X> on non-Windows triples
  // when a CIL object has a segment named <X> (plan 5.9.9), so they do not
  // bind natively.
  std::optional<DenseSet<StringRef>> objectSections;
  auto isObjectSectionBoundary = [&](StringRef name) {
    if (ctx.arg.target.isOSWindows())
      return false;
    if (!name.consume_front("__start_") && !name.consume_front("__stop_"))
      return false;
    if (!isValidCIdentifier(name))
      return false;
    if (!objectSections) {
      objectSections.emplace();
      for (ObjFile *f : ctx.objectFiles)
        for (InputSegment *seg : f->segments())
          objectSections->insert(seg->sectionName);
    }
    return objectSections->contains(name);
  };

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
    if (!entrySym) {
      reportUnresolved("undefined symbol: " + ctx.arg.entry.str(), {},
                       /*isEntry=*/true);
    } else if (entrySym->isUndefined() || entrySym->isLazy()) {
      ArrayRef<Reference> refs = getReferences(entrySym);
      reportUnresolved(getUnresolvedMessage(*entrySym), refs,
                       /*isEntry=*/true,
                       getObjectReferences(entrySym, /*liveOnly=*/false,
                                           /*includeLTO=*/refs.empty()));
    }
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

    // Objects: only what an input still refers to binds (a prevailing
    // bitcode definition that LTO dropped is an unreferenced undefined
    // symbol now), and the writer reports the undefined, hidden and
    // discarded symbols that live code refers to (plan 5.9.6 step 4).
    if (objectMode) {
      if (!sym->referenced || discarded)
        continue;
      // The writer defines a referenced __start_<X> or __stop_<X> as data,
      // whatever the reference's visibility: a function reference is a kind
      // mismatch (plan 5.9.5), reported before the writer runs.
      if (isObjectSectionBoundary(sym->getName())) {
        checkLinkerDefinedKind(*sym);
        continue;
      }
      // A definition in a native object given on the command line, then
      // step 1, the libraries built by this toolchain, then step 2, the
      // native libraries.
      if (sym->visibility() == STV_DEFAULT && !rejectObjectDefinition(*sym) &&
          !bindManagedSymbol(*sym))
        bindObjectSymbol(*sym);
      continue;
    }

    // Step 2: the native libraries, by the symbol name. Whatever stays
    // undefined goes to LTO: a weak symbol keeps M12's lowering (null, and a
    // trap stub for calls), and the import-annotation pass reports any other
    // that the merged module still refers to (plan 5.9.6, 5.10.13). Code
    // generation defines __start_<X> and __stop_<X> as data, whatever the
    // reference's visibility, as the writer does for CIL objects: they are
    // neither bound nor reported, and a function reference is a kind mismatch
    // (plan 5.9.5) that is never left to code generation.
    if (!discarded) {
      if (isSectionBoundary(sym->getName())) {
        checkLinkerDefinedKind(*sym);
        continue;
      }
      if (sym->visibility() == STV_DEFAULT) {
        // A native object given on the command line defines it: the lookup
        // refuses its machine code, and the import-annotation pass reports
        // a reference that the merged module keeps. Code generation would
        // make a weak one null, so a referenced weak symbol is reported here.
        if (sym->isWeak())
          if (const NativeExport *e =
                  ctx.nativeIndex.findObjectDefinition(sym->getName())) {
            ArrayRef<Reference> symRefs = getReferences(sym);
            if (!symRefs.empty())
              reportUnresolved(getNativeRejectionText(sym->getName(), *e),
                               symRefs, /*isEntry=*/false);
            continue;
          }
        bindNative(*sym);
        continue;
      }
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

  if (!objectMode)
    return;

  // Step 3: the direct calls of each undefined weak function bind to trap
  // stubs, one per call signature; record the signatures in first-use order.
  for (ObjFile *f : ctx.objectFiles) {
    const TokenMap &tokens = f->getTokenMap();
    for (InputMethod *m : f->methods()) {
      if (m->discarded)
        continue;
      for (const TokenSite &site : m->sites) {
        TokenTarget t = tokens.lookup(site.token);
        if (t.kind != TokenTarget::Method || !t.sym || !t.signature ||
            !t.sym->isUndefWeak())
          continue;
        SmallVector<UndefinedCall, 1> &calls = undefinedCalls[t.sym];
        bool seen = any_of(calls, [&](const UndefinedCall &c) {
          return *c.signature == *t.signature &&
                 (c.file == f || !signatureNamesType(*t.signature));
        });
        if (!seen)
          calls.push_back({f, t.signature, t.signatureInfo});
      }
    }
  }
}

void SymbolTable::resolveAlternateNames() {
  for (const auto &[from, to] : ctx.appliedAlternateNames) {
    Symbol *fromSym = find(from);
    Symbol *toSym = find(to);
    if (!fromSym || !toSym || fromSym == toSym || !fromSym->isUndefined())
      continue;
    // <from>'s references now name <to>'s definition, whatever it is; if
    // <to> is undefined too, <from> stays undefined.
    if (auto *d = dyn_cast<Defined>(toSym))
      d->overwrite(*fromSym);
    else if (auto *c = dyn_cast<CommonSymbol>(toSym))
      c->overwrite(*fromSym);
    else if (auto *n = dyn_cast<NativeSymbol>(toSym))
      n->overwrite(*fromSym);
  }
}

void SymbolTable::finalizeObjectSymbols() {
  // A definition that lost resolution: its own method or segment is not the
  // symbol's (plan 5.9.5). COMDAT losers are discarded already.
  SmallVector<InputChunk *, 0> lost;
  auto lose = [&](InputChunk *c) {
    if (!c->discarded) {
      c->discarded = true;
      lost.push_back(c);
    }
  };
  for (ObjFile *f : ctx.objectFiles) {
    for (auto [i, s] : llvm::enumerate(f->getRecord().Symbols)) {
      uint32_t bind = s.Flags & MSILLink::BindingMask;
      if ((s.Flags & (MSILLink::Undefined | MSILLink::Alias)) ||
          bind == MSILLink::BindingLocal || bind == MSILLink::BindingCommon)
        continue;
      Symbol *sym = f->getSymbol(i);
      if (s.Kind == MSILLink::SymbolKind::Function) {
        InputMethod *own = f->getMethod(s.Token);
        auto *def = dyn_cast_or_null<DefinedFunction>(sym);
        if (own && (!def || def->method != own))
          lose(own);
        continue;
      }
      if (s.Segment >= f->segments().size())
        continue;
      InputSegment *own = f->segments()[s.Segment];
      auto *def = dyn_cast_or_null<DefinedData>(sym);
      if (!def || def->segment != own)
        lose(own);
    }
  }

  // A losing definition's method or segment stays when a symbol that won
  // still lies in it: an alias of the losing definition, which keeps its own
  // binding (lld/ELF never drops the section of an overridden weak
  // definition). The writer then names it apart from the winner's.
  if (!lost.empty()) {
    DenseSet<const InputChunk *> used;
    for (Symbol *sym : symVector)
      if (InputChunk *c = getDefiningChunk(sym))
        used.insert(c);
    for (ObjFile *f : ctx.objectFiles)
      for (Symbol *sym : f->getSymbols())
        if (sym && sym->isLocal())
          if (InputChunk *c = getDefiningChunk(sym))
            used.insert(c);
    for (InputChunk *c : lost)
      if (used.contains(c))
        c->discarded = false;
  }

  // The common symbols that survived, at the SYMTAB position of their first
  // declaration (plan 5.9.9 rule 3), with the merged size and alignment.
  for (ObjFile *f : ctx.objectFiles) {
    for (auto [i, s] : llvm::enumerate(f->getRecord().Symbols)) {
      if ((s.Flags & MSILLink::BindingMask) != MSILLink::BindingCommon)
        continue;
      auto *c = dyn_cast_or_null<CommonSymbol>(f->getSymbol(i));
      if (!c || c->segment)
        continue;
      auto *seg =
          make<SyntheticSegment>(SyntheticSegment::Common, f, c->getName(),
                                 Log2_32(c->getAlignment()), c->getSize());
      seg->symbolIndex = i;
      seg->sym = c;
      c->segment = seg;
      ctx.syntheticSegments.push_back(seg);
    }
  }
}

SyntheticData *SymbolTable::addSyntheticData(StringRef name, uint64_t offset) {
  Symbol *sym = find(name);
  if (!sym || !(sym->isUndefined() || (sym->isLazy() && sym->isWeak())))
    return nullptr;
  // A function symbol does not become data (plan 5.9.5): bindLateSymbols()
  // reported a function reference as a kind mismatch, and one that nothing
  // refers to (a bitcode definition that LTO dropped) stays as it is.
  if (sym->isFunc())
    return nullptr;
  SyntheticData(name, offset).overwrite(*sym);
  auto *s = cast<SyntheticData>(sym);
  s->live = true;
  ctx.sym.startStop.push_back(s);
  return s;
}

void SymbolTable::addImportAttributes(Symbol &sym, InputFile &file,
                                      StringRef module, StringRef name) {
  if (!sym.isUndefined() && !sym.isLazy())
    return;
  // An import name equal to the symbol name, which is what a stub without
  // `as` has, counts as none (plan 5.9.6 step 2).
  if (name == sym.getName())
    name = "";
  if (module.empty() && name.empty())
    return;
  ImportInfo &info = importInfo[&sym];
  if (!name.empty()) {
    if (!info.nameFile) {
      info.name = name;
      info.nameFile = &file;
    } else if (info.name != name) {
      error("import name mismatch for symbol: " + toString(sym) +
            "\n>>> defined as " + info.name + " in " + toString(info.nameFile) +
            "\n>>> defined as " + name + " in " + toString(&file));
    }
  }
  if (!module.empty()) {
    if (!info.moduleFile) {
      info.module = module;
      info.moduleFile = &file;
    } else if (info.module != module) {
      error("import module mismatch for symbol: " + toString(sym) +
            "\n>>> defined as " + info.module + " in " +
            toString(info.moduleFile) + "\n>>> defined as " + module + " in " +
            toString(&file));
    }
  }
}

StringRef SymbolTable::getImportModule(const Symbol *sym) const {
  auto it = importInfo.find(sym);
  return it == importInfo.end() ? StringRef() : it->second.module;
}

StringRef SymbolTable::getImportName(const Symbol *sym) const {
  auto it = importInfo.find(sym);
  return it == importInfo.end() ? StringRef() : it->second.name;
}

bool SymbolTable::isExplicitImport(const Symbol *sym) const {
  auto it = importInfo.find(sym);
  return it != importInfo.end() && it->second.explicitBinding &&
         sym->isNative();
}

ArrayRef<const NativeExport *>
SymbolTable::getRejectedNative(const Symbol *sym) const {
  auto it = rejectedNative.find(sym);
  if (it == rejectedNative.end())
    return {};
  return it->second;
}

ArrayRef<SymbolTable::UndefinedCall>
SymbolTable::getUndefinedCalls(const Symbol *sym) const {
  auto it = undefinedCalls.find(sym);
  if (it == undefinedCalls.end())
    return {};
  return it->second;
}
} // namespace lld::msil
