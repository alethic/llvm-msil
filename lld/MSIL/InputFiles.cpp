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
#include "InputChunks.h"
#include "SymbolTable.h"
#include "TokenMap.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Reproduce.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CLI/LinkInfo.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/CLI/PEImage.h"
#include "llvm/CLI/Signature.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/COFF.h"
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

namespace MSILLink = llvm::MSILLink;

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

bool isCLIImage(MemoryBufferRef mb) {
  if (identify_magic(mb.getBuffer()) != file_magic::pecoff_executable)
    return false;
  Expected<cli::PEImage> image = cli::PEImage::create(
      ArrayRef(reinterpret_cast<const uint8_t *>(mb.getBufferStart()),
               mb.getBufferSize()));
  if (image)
    return image->hasCLIHeader();
  consumeError(image.takeError());
  // A damaged image whose headers llvm::object still reads (its sections run
  // past the end of the file, say): its CLR directory decides, so that the
  // CIL object reader reports the damage.
  Expected<std::unique_ptr<object::COFFObjectFile>> obj =
      object::COFFObjectFile::create(mb);
  if (!obj) {
    consumeError(obj.takeError());
    return false;
  }
  const object::data_directory *dir =
      (*obj)->getDataDirectory(COFF::CLR_RUNTIME_HEADER);
  return dir && dir->RelativeVirtualAddress != 0;
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

  // A PE with a CLI header: a CIL object, or a .NET assembly, which the
  // ObjFile constructor reports (plan 5.9.4).
  if (isCLIImage(mb)) {
    auto *f = make<ObjFile>(mb, archiveName, lazy);
    return f->isValid() ? f : nullptr;
  }

  // An archive member that is neither bitcode, a CIL object nor native (the
  // driver gives native members to the native index) is skipped (plan
  // 5.9.4).
  if (!archiveName.empty()) {
    warn(archiveName + "(" + mb.getBufferIdentifier() +
         "): not LLVM bitcode; ignored");
    return nullptr;
  }
  error("unknown file type: " + mb.getBufferIdentifier());
  return nullptr;
}

ObjFile::ObjFile(MemoryBufferRef m, StringRef archiveName, bool lazy)
    : InputFile(ObjectKind, m) {
  this->lazy = lazy;
  this->archiveName = std::string(archiveName);

  // The image and the metadata (plan 5.9.4). Errors are error()s, not lld's
  // fatal(), so that the printing iteration of LLD_IN_TEST=2 still reports
  // them (plan 5.9.1).
  Expected<std::unique_ptr<cli::MetadataReader>> mdOrErr =
      cli::MetadataReader::create(
          ArrayRef(reinterpret_cast<const uint8_t *>(mb.getBufferStart()),
                   mb.getBufferSize()));
  if (!mdOrErr) {
    error(toString(this) + ": " + toString(mdOrErr.takeError()));
    return;
  }
  reader = std::move(*mdOrErr);

  // .NET assemblies are reference assemblies (M21) or libraries of this
  // toolchain (M22).
  if (reader->isAssembly()) {
    error(toString(this) + ": .NET assembly inputs are not supported yet");
    return;
  }

  // The record (plan 5.8.7): a module without one is no CIL object (a C#
  // netmodule, Q7).
  Expected<ArrayRef<uint8_t>> bytesOrErr = cli::findLinkInfo(*reader);
  if (!bytesOrErr) {
    std::string msg = toString(bytesOrErr.takeError());
    if (msg == "no link-info record")
      error(toString(this) + ": not a CIL object (no link-info record)");
    else
      error(toString(this) + ": " + msg);
    return;
  }
  Expected<MSILLink::Record> recordOrErr = cli::decodeLinkInfo(*bytesOrErr);
  if (!recordOrErr) {
    error(toString(this) + ": " + toString(recordOrErr.takeError()));
    return;
  }
  record = std::move(*recordOrErr);

  // decodeLinkInfo checked that the names are unique (C8).
  for (auto [i, sym] : llvm::enumerate(record.Symbols))
    symbolIndexes[CachedHashStringRef(sym.Name)] = i;
  valid = true;
}

ObjFile::~ObjFile() = default;

int32_t ObjFile::findSymbolIndex(StringRef name) const {
  auto it = symbolIndexes.find(CachedHashStringRef(name));
  return it == symbolIndexes.end() ? -1 : static_cast<int32_t>(it->second);
}

InputMethod *ObjFile::getMethod(uint32_t methodDefToken) const {
  if (cli::getTokenType(methodDefToken) !=
      static_cast<uint8_t>(cli::TableId::MethodDef))
    return nullptr;
  uint32_t row = cli::getTokenRow(methodDefToken);
  if (row == 0 || row > methodByRow.size())
    return nullptr;
  return methodByRow[row - 1];
}

const MSILLink::SignatureInfo *ObjFile::getSignatureInfo(uint32_t token) const {
  return sigInfos.lookup(token);
}

std::optional<ObjFile::ImportAttributes>
ObjFile::getImport(uint32_t symbolIndex) const {
  auto it = imports.find(symbolIndex);
  if (it == imports.end())
    return std::nullopt;
  return it->second;
}

SmallVector<StringRef, 0> ObjFile::getDependentLibraries() const {
  SmallVector<StringRef, 0> ret;
  for (const MSILLink::LinkerOption &opt : record.LinkerOptions)
    if (opt.Kind == MSILLink::LinkerOptionKind::DependentLib &&
        !opt.Args.empty())
      ret.push_back(opt.Args.front());
  return ret;
}

// One string of a LINKER_OPTION entry is one option, except on Windows
// triples, where clang quotes a library name with a space
// (/DEFAULTLIB:"my lib.lib") as lld-link's directive tokenizer expects; the
// string is tokenized as BitcodeFile::getLinkerOptions() tokenizes the
// irsymtab's linker options there (section 12 of 2026-10-22).
SmallVector<std::string, 0> ObjFile::getLinkerOptions() const {
  SmallVector<std::string, 0> ret;
  bool windows = Triple(record.Hdr.Triple).isOSWindows();
  for (const MSILLink::LinkerOption &opt : record.LinkerOptions) {
    if (opt.Kind != MSILLink::LinkerOptionKind::LinkerOption)
      continue;
    for (const std::string &arg : opt.Args) {
      if (!windows) {
        ret.push_back(arg);
        continue;
      }
      SmallVector<StringRef, 4> tokens;
      cl::TokenizeWindowsCommandLineNoCopy(saver().save(arg), saver(), tokens);
      for (StringRef tok : tokens)
        ret.push_back(tok.str());
    }
  }
  return ret;
}

// The methods with a body (decoded by the token walker, plan 5.9.7) and the
// import attributes of the stubs (body-less MethodDefs with an ImplMap row,
// C4). Returns false after an error.
bool ObjFile::readMethods() {
  // The FUNCTION symbol of each MethodDef with a body (C1, C3).
  DenseMap<uint32_t, uint32_t> symbolByToken;
  for (auto [i, sym] : llvm::enumerate(record.Symbols))
    if (sym.Kind == MSILLink::SymbolKind::Function &&
        !(sym.Flags & (MSILLink::Undefined | MSILLink::Alias)))
      symbolByToken[sym.Token] = i;

  uint32_t numRows = reader->getRowCount(cli::TableId::MethodDef);
  methodByRow.assign(numRows, nullptr);
  for (uint32_t row = 1; row <= numRows; ++row) {
    uint32_t token = cli::makeToken(cli::TableId::MethodDef, row);
    Expected<cli::MethodDefRow> def = reader->getMethodDef(row);
    if (!def) {
      error(toString(this) + ": " + toString(def.takeError()));
      return false;
    }

    // A stub: the symbol of the method's name takes the stub's module (none
    // for msil.unresolved) and import name.
    if (def->RVA == 0) {
      Expected<std::optional<cli::ImplMapRow>> implMap =
          reader->findImplMap(token);
      if (!implMap) {
        error(toString(this) + ": " + toString(implMap.takeError()));
        return false;
      }
      int32_t index = findSymbolIndex(def->Name);
      if (!*implMap || index < 0)
        continue;
      Expected<cli::ModuleRefRow> moduleRef =
          reader->getModuleRef(cli::getTokenRow((*implMap)->ImportScope));
      if (!moduleRef) {
        error(toString(this) + ": " + toString(moduleRef.takeError()));
        return false;
      }
      StringRef module = moduleRef->Name;
      if (module == MSILLink::UnresolvedModule)
        module = "";
      imports.try_emplace(index,
                          ImportAttributes{module, (*implMap)->ImportName});
      continue;
    }

    auto it = symbolByToken.find(token);
    if (it == symbolByToken.end())
      continue; // validateLinkInfo rejects this (C3)
    auto *m = make<InputMethod>(this, token, def->Name, it->second);
    m->flags = def->Flags;
    m->implFlags = def->ImplFlags;
    m->signatureInfo = getSignatureInfo(token);
    Expected<ArrayRef<uint8_t>> blob = reader->getBlob(def->Signature);
    if (!blob) {
      error(toString(this) + ": " + toString(blob.takeError()));
      return false;
    }
    Expected<cli::MethodSig> sig = cli::decodeMethodSig(*blob);
    if (!sig) {
      error(toString(this) + ": '" + def->Name +
            "': " + toString(sig.takeError()));
      return false;
    }
    m->signature = make<cli::MethodSig>(std::move(*sig));
    if (!decodeMethod(*this, *m))
      return false;
    methodByRow[row - 1] = m;
    methodList.push_back(m);
  }

  // IMPORT data: the record carries the attributes (plan 5.8.8).
  for (auto [i, sym] : llvm::enumerate(record.Symbols))
    if (sym.Flags & MSILLink::Import)
      imports.try_emplace(i,
                          ImportAttributes{sym.ImportModule, sym.ImportName});
  return true;
}

// The record's segments, each named after the symbol that defines it, with
// its slice of DATA_RELOCS (sorted by segment and offset, C8).
void ObjFile::readSegments() {
  for (auto [i, seg] : llvm::enumerate(record.Segments))
    segmentList.push_back(make<InputSegment>(this, i, seg.Name, seg));
  for (auto [i, sym] : llvm::enumerate(record.Symbols)) {
    if (sym.Kind != MSILLink::SymbolKind::Data ||
        (sym.Flags & (MSILLink::Undefined | MSILLink::Alias)) ||
        (sym.Flags & MSILLink::BindingMask) == MSILLink::BindingCommon ||
        sym.Segment >= segmentList.size())
      continue;
    InputSegment *seg = segmentList[sym.Segment];
    if (seg->symbolIndex >= 0)
      continue;
    seg->symbolIndex = i;
    seg->name = sym.Name;
  }
  ArrayRef<MSILLink::Reloc> relocs = record.Relocs;
  for (InputSegment *seg : segmentList) {
    size_t n = 0;
    while (n < relocs.size() && relocs[n].Segment == seg->index)
      ++n;
    seg->relocs = relocs.take_front(n);
    relocs = relocs.drop_front(n);
  }
}

static StringRef getSelectionKindName(MSILLink::ComdatSelection kind) {
  switch (kind) {
  case MSILLink::ComdatSelection::Any:
    return "any";
  case MSILLink::ComdatSelection::ExactMatch:
    return "exactmatch";
  case MSILLink::ComdatSelection::Largest:
    return "largest";
  case MSILLink::ComdatSelection::NoDeduplicate:
    return "nodeduplicate";
  case MSILLink::ComdatSelection::SameSize:
    return "samesize";
  }
  llvm_unreachable("unknown comdat selection kind");
}

// COMDAT groups (plan 5.9.5, lld/ELF/InputFiles.cpp:1905-1912): the first
// object that presents a group keeps it; NoDeduplicate groups are always
// kept. The methods and segments of a discarded copy are discarded, and the
// symbols they define (aliases of them included) are marked in
// \p discardedSymbols: they enter the table as undefined symbols marked
// "discarded". Under \p ignoreComdats (an LTO object) every group is kept
// and none is claimed: the object's BitcodeFiles claimed them, and the IR
// linker kept only the prevailing copies (lld/wasm/InputFiles.cpp,
// lld/ELF/InputFiles.cpp's ignoreComdats).
void ObjFile::readComdats(std::vector<bool> &discardedSymbols,
                          bool ignoreComdats) {
  auto methodOf = [&](uint32_t index) -> InputMethod * {
    if (index >= record.Symbols.size())
      return nullptr;
    const MSILLink::Symbol &sym = record.Symbols[index];
    if (sym.Flags & (MSILLink::Undefined | MSILLink::Alias))
      return nullptr;
    return getMethod(sym.Token);
  };

  for (auto [i, group] : llvm::enumerate(record.Comdats)) {
    if (!ignoreComdats && group.Selection != MSILLink::ComdatSelection::Any &&
        group.Selection != MSILLink::ComdatSelection::NoDeduplicate)
      error("comdat '" + group.Name + "' in " + toString(this) +
            " uses selection kind '" + getSelectionKindName(group.Selection) +
            "', which msil-ld does not support");
    bool kept = ignoreComdats ||
                group.Selection == MSILLink::ComdatSelection::NoDeduplicate ||
                symtab->addComdat(group.Name);
    keptComdats.push_back(kept);
    for (auto [kind, index] : group.Members) {
      if (kind == MSILLink::ComdatMemberKind::Segment) {
        if (index >= segmentList.size())
          continue;
        segmentList[index]->comdat = i;
        if (!kept)
          segmentList[index]->discarded = true;
        continue;
      }
      if (kept || index >= discardedSymbols.size())
        continue;
      discardedSymbols[index] = true;
      if (InputMethod *m = methodOf(index))
        m->discarded = true;
    }
  }

  // The data symbols of discarded segments, then the aliases of discarded
  // symbols (an aliasee is never an alias, C8).
  for (auto [i, sym] : llvm::enumerate(record.Symbols)) {
    if (sym.Kind == MSILLink::SymbolKind::Data &&
        !(sym.Flags & (MSILLink::Undefined | MSILLink::Alias)) &&
        (sym.Flags & MSILLink::BindingMask) != MSILLink::BindingCommon &&
        sym.Segment < segmentList.size() && segmentList[sym.Segment]->discarded)
      discardedSymbols[i] = true;
  }
  for (auto [i, sym] : llvm::enumerate(record.Symbols))
    if ((sym.Flags & MSILLink::Alias) &&
        sym.Aliasee < discardedSymbols.size() && discardedSymbols[sym.Aliasee])
      discardedSymbols[i] = true;
}

// The symbol of SYMTAB entry \p index (plan 5.9.5): a LOCAL symbol of the
// object, or the result of resolving the entry against the symbol table.
Symbol *ObjFile::createSymbol(uint32_t index, bool discarded) {
  const MSILLink::Symbol &s = record.Symbols[index];
  uint32_t bind = s.Flags & MSILLink::BindingMask;
  // The visibility values equal ELF's STV_* (plan 5.8.8).
  uint8_t visibility =
      (s.Flags & MSILLink::VisibilityMask) >> MSILLink::VisibilityShift;
  bool isFunction = s.Kind == MSILLink::SymbolKind::Function;
  uint8_t type = isFunction ? STT_FUNC : STT_OBJECT;
  bool tls = s.Flags & MSILLink::ThreadLocal;
  bool alias = s.Flags & MSILLink::Alias;
  bool noStrip = s.Flags & MSILLink::NoStrip;
  uint8_t binding = bind == MSILLink::BindingWeak    ? STB_WEAK
                    : bind == MSILLink::BindingLocal ? STB_LOCAL
                                                     : STB_GLOBAL;
  StringRef name = s.Name;

  // The definition's method, or segment and offset; an alias's is its
  // aliasee's (plan 5.9.5).
  const MSILLink::Symbol &def = alias ? record.Symbols[s.Aliasee] : s;
  InputMethod *method = isFunction ? getMethod(def.Token) : nullptr;
  InputSegment *segment = nullptr;
  uint64_t offset = 0;
  if (!isFunction && bind != MSILLink::BindingCommon &&
      !(s.Flags & MSILLink::Undefined) && def.Segment < segmentList.size()) {
    segment = segmentList[def.Segment];
    offset = def.Offset + (alias ? s.AliasOffset : 0);
  }

  // LOCAL symbols bind within their object; they never enter the table.
  if (binding == STB_LOCAL) {
    Symbol *local;
    if (discarded)
      local = make<Undefined>(this, name, STB_LOCAL, visibility, type, tls,
                              /*discarded=*/true);
    else if (isFunction)
      local = make<DefinedFunction>(this, name, STB_LOCAL, visibility, method);
    else
      local = make<DefinedData>(this, name, STB_LOCAL, visibility, tls, segment,
                                offset, s.Size);
    local->isAlias = alias;
    local->noStrip = noStrip;
    return local;
  }

  Symbol *sym;
  if (s.Flags & MSILLink::Undefined) {
    Undefined newSym(this, name, binding, visibility, type, tls);
    newSym.dllImport = s.Flags & MSILLink::DLLImport;
    sym = symtab->addSymbol(newSym);
    sym->referenced = true;
    // The import attributes of a reference (plan 5.10.7): two inputs that
    // disagree are an error; one that gives none takes the other's.
    if (std::optional<ImportAttributes> imp = getImport(index))
      symtab->addImportAttributes(*sym, *this, imp->module, imp->name);
  } else if (discarded) {
    // A definition in a COMDAT group that an earlier input presented enters
    // as an undefined symbol marked as discarded (plan 5.9.5).
    sym =
        symtab->addSymbol(Undefined(this, name, binding, visibility, type, tls,
                                    /*discarded=*/true));
  } else if (bind == MSILLink::BindingCommon) {
    CommonSymbol newSym(this, name, STB_GLOBAL, visibility, tls,
                        1u << s.CommonAlignLog2, s.CommonSize);
    newSym.noStrip = noStrip;
    sym = symtab->addSymbol(newSym);
  } else if (isFunction) {
    DefinedFunction newSym(this, name, binding, visibility, method);
    newSym.isAlias = alias;
    newSym.noStrip = noStrip;
    sym = symtab->addSymbol(newSym);
  } else {
    DefinedData newSym(this, name, binding, visibility, tls, segment, offset,
                       s.Size);
    newSym.isAlias = alias;
    newSym.noStrip = noStrip;
    sym = symtab->addSymbol(newSym);
  }
  // A CIL object is a regular object: LTO keeps every bitcode definition it
  // refers to visible (plan 5.9.13).
  sym->isUsedInRegularObj = true;
  return sym;
}

void ObjFile::parse(bool ignoreComdats) {
  if (parsed)
    return;
  parsed = true;

  // C1-C7 between the record and the metadata (plan 5.8.5).
  if (Error e = cli::validateLinkInfo(record, *reader)) {
    error(toString(this) + ": malformed CIL object: " + toString(std::move(e)));
    return;
  }

  for (const MSILLink::SignatureInfo &info : record.Signatures)
    sigInfos[info.Token] = &info;
  if (!readMethods())
    return;
  readSegments();
  std::vector<bool> discardedSymbols(record.Symbols.size());
  readComdats(discardedSymbols, ignoreComdats);

  // Undefined symbols can trigger recursive extract. Process defined symbols
  // first so that the relative order between a defined symbol and an
  // undefined symbol does not change the symbol resolution behavior
  // (lld/ELF/InputFiles.cpp:1247-1252).
  symbols.assign(record.Symbols.size(), nullptr);
  for (auto [i, sym] : llvm::enumerate(record.Symbols))
    if (!(sym.Flags & MSILLink::Undefined))
      symbols[i] = createSymbol(i, discardedSymbols[i]);
  for (auto [i, sym] : llvm::enumerate(record.Symbols))
    if (sym.Flags & MSILLink::Undefined)
      symbols[i] = createSymbol(i, /*discarded=*/false);

  tokenMap.build(*this);
}

// lld/ELF/InputFiles.cpp:2024-2041: every symbol that the object defines
// (weak and common ones and aliases included) is a lazy symbol.
void ObjFile::parseLazy() {
  for (const MSILLink::Symbol &sym : record.Symbols) {
    if ((sym.Flags & MSILLink::Undefined) ||
        (sym.Flags & MSILLink::BindingMask) == MSILLink::BindingLocal)
      continue;
    symtab->addLazy(sym.Name, this);
    // addLazy() may trigger this->extract() if an existing symbol is an
    // undefined symbol. If that happens, this function has served its
    // purpose, and we can exit from the loop early.
    if (!lazy)
      break;
  }
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
