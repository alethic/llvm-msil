//===- InputFiles.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/InputFiles.cpp at 64f50ffd4f1c.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Config.h"
#include "SymbolTable.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Reproduce.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/IRSymtab.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TarWriter.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::sys;

namespace lld {
// Returns a string in the format of "foo.o" or "foo.a(bar.o)".
std::string toString(const msil::InputFile *file) {
  if (!file)
    return "<internal>";

  if (file->archiveName.empty())
    return std::string(file->getName());

  return (file->archiveName + "(" + file->getName() + ")").str();
}

namespace msil {
std::unique_ptr<llvm::TarWriter> tar;

std::optional<MemoryBufferRef> readFile(StringRef path) {
  log("Loading: " + path);

  auto mbOrErr = MemoryBuffer::getFile(path);
  if (auto ec = mbOrErr.getError()) {
    error("cannot open " + path + ": " + ec.message());
    return std::nullopt;
  }
  std::unique_ptr<MemoryBuffer> &mb = *mbOrErr;
  MemoryBufferRef mbref = mb->getMemBufferRef();
  make<std::unique_ptr<MemoryBuffer>>(std::move(mb)); // take MB ownership

  if (tar)
    tar->append(relativeToRoot(path), mbref.getBuffer());
  return mbref;
}

InputFile *createObjectFile(MemoryBufferRef mb, StringRef archiveName,
                            uint64_t offsetInArchive, bool lazy) {
  file_magic magic = identify_magic(mb.getBuffer());
  if (magic == file_magic::bitcode) {
    auto *f = make<BitcodeFile>(mb, archiveName, offsetInArchive, lazy);
    // A file the constructor rejected (module-level inline assembly, a
    // malformed file) has no lto::InputFile; its error is reported and no
    // caller sees it.
    return f->obj ? f : nullptr;
  }

  // CIL objects are M20's; until then an archive member that is neither
  // bitcode nor native (the driver gives native members to the native index)
  // is skipped (plan 5.9.4).
  if (!archiveName.empty()) {
    warn(archiveName + "(" + mb.getBufferIdentifier() +
         "): not LLVM bitcode; ignored");
    return nullptr;
  }
  error("unknown file type: " + mb.getBufferIdentifier());
  return nullptr;
}

// The producer string that irsymtab writes into a table and requires of a
// table it reads (kExpectedProducerName in llvm/lib/Object/IRSymtab.cpp, which
// is not exported), read back from the table of an empty module.
static StringRef getIRSymtabProducer() {
  static const std::string producer = [] {
    LLVMContext context;
    llvm::Module m("", context);
    m.setDataLayout("e"); // irsymtab::build requires one
    llvm::Module *mods[] = {&m};
    SmallVector<char, 0> symtab;
    StringTableBuilder strtabBuilder(StringTableBuilder::RAW);
    BumpPtrAllocator alloc;
    if (Error e = irsymtab::build(mods, symtab, strtabBuilder, alloc)) {
      consumeError(std::move(e));
      return std::string();
    }
    strtabBuilder.finalizeInOrder();
    std::string strtab(strtabBuilder.getSize(), '\0');
    strtabBuilder.write(reinterpret_cast<uint8_t *>(strtab.data()));
    auto *hdr =
        reinterpret_cast<const irsymtab::storage::Header *>(symtab.data());
    return hdr->Producer.get(strtab).str();
  }();
  return producer;
}

// True if irsymtab::readBitcode would rebuild the file's symbol table from
// the IR (its conditions, llvm/lib/Object/IRSymtab.cpp:390-424).
static bool needsIRSymtabRebuild(const BitcodeFileContents &bfc) {
  using irsymtab::storage::Header;
  if (bfc.StrtabForSymtab.empty() || bfc.Symtab.size() < sizeof(Header))
    return true;
  auto *hdr = reinterpret_cast<const Header *>(bfc.Symtab.data());
  unsigned version = hdr->Version;
  return version != Header::kCurrentVersion ||
         hdr->Producer.get(bfc.StrtabForSymtab) != getIRSymtabProducer() ||
         hdr->Modules.Size != bfc.Mods.size();
}

// BitcodeWriter writes no symbol table for a module with module-level inline
// assembly whose target has no asm parser, which is every MSIL triple, and
// lto::InputFile::create would rebuild it through ModuleSymbolTable, which
// asserts on such a target (llvm/lib/Object/ModuleSymbolTable.cpp:81).
// Returns true for such a file (plan 5.9.14). Only a file without a usable
// table pays for loading its modules; any error is left to
// lto::InputFile::create.
static bool hasModuleAsmWithoutAsmParser(MemoryBufferRef mb) {
  Expected<BitcodeFileContents> bfcOrErr = getBitcodeFileContents(mb);
  if (!bfcOrErr) {
    consumeError(bfcOrErr.takeError());
    return false;
  }
  if (!needsIRSymtabRebuild(*bfcOrErr))
    return false;

  LLVMContext context;
  for (BitcodeModule &bm : bfcOrErr->Mods) {
    Expected<std::unique_ptr<llvm::Module>> mOrErr =
        bm.getLazyModule(context, /*ShouldLazyLoadMetadata=*/true,
                         /*IsImporting=*/false);
    if (!mOrErr) {
      consumeError(mOrErr.takeError());
      return false;
    }
    if (!(*mOrErr)->hasModuleInlineAsm())
      continue;
    std::string err;
    const Target *t =
        TargetRegistry::lookupTarget((*mOrErr)->getTargetTriple(), err);
    if (!t || !t->hasMCAsmParser())
      return true;
  }
  return false;
}

BitcodeFile::BitcodeFile(MemoryBufferRef m, StringRef archiveName,
                         uint64_t offsetInArchive, bool lazy)
    : InputFile(BitcodeKind, m) {
  this->lazy = lazy;
  this->archiveName = std::string(archiveName);

  std::string path = mb.getBufferIdentifier().str();

  // ThinLTO assumes that all MemoryBufferRefs given to it have a unique
  // name. If two archives define two members with the same name, this
  // causes a collision which result in only one of the objects being taken
  // into consideration at LTO time (which very likely causes undefined
  // symbols later in the link stage). So we append file offset to make
  // filename unique.
  StringRef name = archiveName.empty()
                       ? saver().save(path)
                       : saver().save(archiveName + "(" + path::filename(path) +
                                      " at " + utostr(offsetInArchive) + ")");
  MemoryBufferRef mbref(mb.getBuffer(), name);

  // D26's error, before lto::InputFile::create asserts on the file. It is an
  // error(), not a fatal(), so that the printing iteration of LLD_IN_TEST=2
  // still runs (plan 5.9.1); obj stays null and createObjectFile drops the
  // file.
  if (hasModuleAsmWithoutAsmParser(mbref)) {
    error(toString(this) +
          ": module-level inline assembly is not supported for MSIL");
    return;
  }
  // A malformed file is lld's CHECK text, also as an error().
  Expected<std::unique_ptr<lto::InputFile>> objOrErr =
      lto::InputFile::create(mbref);
  if (!objOrErr) {
    error(toString(this) + ": " + toString(objOrErr.takeError()));
    return;
  }
  obj = std::move(*objOrErr);
}

static uint8_t mapVisibility(GlobalValue::VisibilityTypes gvVisibility) {
  switch (gvVisibility) {
  case GlobalValue::DefaultVisibility:
    return STV_DEFAULT;
  case GlobalValue::HiddenVisibility:
    return STV_HIDDEN;
  case GlobalValue::ProtectedVisibility:
    return STV_PROTECTED;
  }
  llvm_unreachable("unknown visibility");
}

static StringRef getSelectionKindName(Comdat::SelectionKind kind) {
  switch (kind) {
  case Comdat::Any:
    return "any";
  case Comdat::ExactMatch:
    return "exactmatch";
  case Comdat::Largest:
    return "largest";
  case Comdat::NoDeduplicate:
    return "nodeduplicate";
  case Comdat::SameSize:
    return "samesize";
  }
  llvm_unreachable("unknown comdat selection kind");
}

// Adds one symbol of a bitcode file to the symbol table, its kind taken from
// the irsymtab flags (lld/ELF/InputFiles.cpp:1866-1904).
static Symbol *createBitcodeSymbol(const lto::InputFile::Symbol &objSym,
                                   BitcodeFile &f) {
  uint8_t binding = objSym.isWeak() ? STB_WEAK : STB_GLOBAL;
  uint8_t type = objSym.isExecutable() ? STT_FUNC : STT_OBJECT;
  uint8_t visibility = mapVisibility(objSym.getVisibility());
  bool tls = objSym.isTLS();

  // Symbols can be duplicated in bitcode files because of '#include' and
  // linkonce_odr. Use uniqueSaver to save symbol names for de-duplication.
  // Update objSym.Name to reference (via StringRef) the string saver's copy;
  // this way LTO can reference the same string saver's copy rather than
  // keeping copies of its own.
  objSym.Name = uniqueSaver().save(objSym.getName());
  StringRef name = objSym.getName();

  // The symbol table names a dllimport declaration __imp_<name>. As lld-link
  // does for a locally defined symbol that is imported, it binds to <name>.
  bool dllImport = false;
  if (name.starts_with("__imp_") &&
      name.drop_front(6) ==
          GlobalValue::dropLLVMManglingEscape(objSym.getIRName())) {
    name = name.drop_front(6);
    dllImport = true;
  }

  if (objSym.isUndefined()) {
    Undefined newSym(&f, name, binding, visibility, type, tls);
    newSym.dllImport = dllImport;
    newSym.noStrip = objSym.isUsed();
    Symbol *sym = symtab->addSymbol(newSym);
    sym->referenced = true;
    // lld/COFF/InputFiles.cpp:1422-1434: the LTO compilation sees a reference
    // through __imp_<name> as a plain dllimport declaration of <name> and does
    // not relate it to the table entry. Mark the symbol as used in a regular
    // object so that a definition in another bitcode input or archive member
    // is kept visible and survives LTO to fulfill the reference (plan 5.9.13).
    if (dllImport)
      sym->isUsedInRegularObj = true;
    return sym;
  }

  // A definition in a COMDAT group that an earlier input presented enters as
  // an undefined symbol marked as discarded (plan 5.9.5; lld/ELF's
  // ObjFile::initializeSymbols does this for definitions in discarded
  // sections, lld/wasm's createBitcodeSymbol for bitcode).
  int c = objSym.getComdatIndex();
  if (c != -1 && !f.keptComdats[c])
    return symtab->addSymbol(Undefined(&f, name, binding, visibility, type, tls,
                                       /*discarded=*/true));

  if (objSym.isCommon()) {
    CommonSymbol newSym(&f, name, binding, visibility, tls,
                        objSym.getCommonAlignment(), objSym.getCommonSize());
    newSym.noStrip = objSym.isUsed();
    return symtab->addSymbol(newSym);
  }
  if (type == STT_FUNC) {
    DefinedFunction newSym(&f, name, binding, visibility);
    newSym.noStrip = objSym.isUsed();
    return symtab->addSymbol(newSym);
  }
  DefinedData newSym(&f, name, binding, visibility, tls);
  newSym.noStrip = objSym.isUsed();
  return symtab->addSymbol(newSym);
}

void BitcodeFile::parse() {
  // COMDAT groups (lld/ELF/InputFiles.cpp:1906-1912): the first input that
  // presents a group keeps it, and NoDeduplicate groups are always kept. C
  // produces only Any and NoDeduplicate.
  for (std::pair<StringRef, Comdat::SelectionKind> s : obj->getComdatTable()) {
    if (s.second != Comdat::Any && s.second != Comdat::NoDeduplicate)
      error("comdat '" + s.first + "' in " + toString(this) +
            " uses selection kind '" + getSelectionKindName(s.second) +
            "', which msil-ld does not support");
    keptComdats.push_back(s.second == Comdat::NoDeduplicate ||
                          symtab->addComdat(s.first));
  }

  // Undefined symbols can trigger recursive extract. Process defined symbols
  // first so that the relative order between a defined symbol and an
  // undefined symbol does not change the symbol resolution behavior
  // (lld/ELF/InputFiles.cpp:1247-1252 and 1918-1925).
  symbols.assign(obj->symbols().size(), nullptr);
  for (auto [i, irSym] : llvm::enumerate(obj->symbols()))
    if (!irSym.isUndefined())
      symbols[i] = createBitcodeSymbol(irSym, *this);
  for (auto [i, irSym] : llvm::enumerate(obj->symbols()))
    if (irSym.isUndefined())
      symbols[i] = createBitcodeSymbol(irSym, *this);
}

// lld/ELF/InputFiles.cpp:2024-2041: every definition of the file is a lazy
// symbol (weak and common ones and aliases included).
void BitcodeFile::parseLazy() {
  for (const lto::InputFile::Symbol &irSym : obj->symbols()) {
    if (irSym.isUndefined())
      continue;
    irSym.Name = uniqueSaver().save(irSym.getName());
    symtab->addLazy(irSym.getName(), this);
    // addLazy() may trigger this->extract() if an existing symbol is an
    // undefined symbol. If that happens, this function has served its
    // purpose, and we can exit from the loop early.
    if (!lazy)
      break;
  }
}

// The linker options of the file (plan 5.9.4, 5.10.6). Windows triples
// record each library request quoted (/DEFAULTLIB:"my lib.lib"), so the
// irsymtab's string is tokenized as lld-link's parseDirectives tokenizes a
// .drectve section. Other MSIL triples record "-l<name>" unquoted, and
// IRSymtab joins the options with spaces (section 12 of 2026-10-22), so the
// module's !llvm.linker.options is read string by string instead; only a
// module that cannot be read falls back to tokenizing.
SmallVector<std::string, 0> BitcodeFile::getLinkerOptions() const {
  SmallVector<std::string, 0> ret;
  StringRef opts = obj->getCOFFLinkerOpts();
  if (opts.empty())
    return ret;
  auto tokenize = [&] {
    ret.clear();
    SmallVector<StringRef, 16> tokens;
    cl::TokenizeWindowsCommandLineNoCopy(opts, saver(), tokens);
    for (StringRef tok : tokens)
      ret.push_back(tok.str());
    return ret;
  };
  if (Triple(obj->getTargetTriple()).isOSWindows())
    return tokenize();

  Expected<std::vector<BitcodeModule>> modsOrErr = getBitcodeModuleList(mb);
  if (!modsOrErr) {
    consumeError(modsOrErr.takeError());
    return tokenize();
  }
  for (BitcodeModule &bm : *modsOrErr) {
    LLVMContext context;
    Expected<std::unique_ptr<llvm::Module>> mOrErr =
        bm.getLazyModule(context, /*ShouldLazyLoadMetadata=*/true,
                         /*IsImporting=*/false);
    if (!mOrErr) {
      consumeError(mOrErr.takeError());
      return tokenize();
    }
    if (Error e = (*mOrErr)->materializeMetadata()) {
      consumeError(std::move(e));
      return tokenize();
    }
    if (NamedMDNode *md = (*mOrErr)->getNamedMetadata("llvm.linker.options"))
      for (const MDNode *node : md->operands())
        for (const MDOperand &op : node->operands())
          if (auto *str = dyn_cast_or_null<MDString>(op.get()))
            ret.push_back(str->getString().str());
  }
  return ret;
}
} // namespace msil
} // namespace lld
