//===- Relocations.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The output entities that symbols name, and the reports of unresolved
// references (see Relocations.h).
//
//===----------------------------------------------------------------------===//

#include "Relocations.h"
#include "BCLBinder.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputFiles.h"
#include "LTO.h"
#include "NativeLibraries.h"
#include "OutputSegment.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Synthetic.h"
#include "TokenMap.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CLI/MetadataReader.h"

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::cli;
using namespace lld;
using namespace lld::msil;

namespace MSILLink = llvm::MSILLink;

void SymbolBinder::addObjectFields() {
  for (ObjFile *f : ctx.objectFiles) {
    const MetadataReader &md = f->getReader();
    const MSILLink::Record &record = f->getRecord();
    uint32_t numRows = md.getRowCount(TableId::Field);
    for (uint32_t row = 1; row <= numRows; ++row) {
      // ObjFile::parse validated the rows (C1, C2).
      Expected<FieldRow> fieldRow = md.getField(row);
      if (!fieldRow) {
        consumeError(fieldRow.takeError());
        continue;
      }
      int32_t i = f->findSymbolIndex(fieldRow->Name);
      if (i < 0)
        continue;
      const MSILLink::Symbol &s = record.Symbols[i];
      Symbol *sym = f->getSymbol(i);
      if (!sym || s.Kind != MSILLink::SymbolKind::Data ||
          (s.Flags & (MSILLink::Undefined | MSILLink::Alias)))
        continue;
      // The definition must be this object's, and live: a losing WEAK copy
      // or a discarded COMDAT member names the winner's field, and a common
      // symbol has the field of its first declaration.
      bool own = false;
      if ((s.Flags & MSILLink::BindingMask) == MSILLink::BindingCommon) {
        auto *c = dyn_cast<CommonSymbol>(sym);
        own = c && c->segment && c->segment->file == f &&
              c->segment->symbolIndex == static_cast<uint32_t>(i) &&
              c->segment->live;
      } else if (auto *d = dyn_cast<DefinedData>(sym)) {
        own = s.Segment < f->segments().size() &&
              d->segment == f->segments()[s.Segment] && d->segment->live &&
              !d->segment->discarded;
      }
      if (!own || dataFields.count(sym))
        continue;
      OutputField of(OutputField::Address);
      of.name = sym->getName().str();
      of.renamable = sym->isLocal();
      of.isPublic = !sym->isLocal();
      of.sym = sym;
      of.file = f;
      dataFields[sym] = out.addField(std::move(of));
    }
  }
}

void SymbolBinder::collectReferences() {
  for (ObjFile *f : ctx.objectFiles) {
    const TokenMap &tokens = f->getTokenMap();
    // The token sites of the live methods; a token's target is the same
    // at every site.
    DenseSet<uint32_t> seenTokens;
    for (InputMethod *m : f->methods()) {
      if (!m->live || m->discarded)
        continue;
      for (const TokenSite &site : m->sites) {
        if (!seenTokens.insert(site.token).second)
          continue;
        TokenTarget t = tokens.lookup(site.token);
        if (!t.sym)
          continue;
        switch (t.kind) {
        case TokenTarget::Method:
          references.push_back(
              {SymbolReference::Call, f, t.sym, t.signature, t.signatureInfo});
          break;
        case TokenTarget::Field:
          references.push_back({SymbolReference::Data, f, t.sym});
          break;
        case TokenTarget::FnAddr:
          references.push_back({SymbolReference::Address, f, t.sym});
          break;
        default:
          break;
        }
      }
    }

    // The relocations of the segments in the image.
    DenseSet<std::pair<uint32_t, uint8_t>> seenRelocs;
    auto addReloc = [&](uint32_t index, SymbolReference::Kind kind) {
      Symbol *sym = f->getSymbol(index);
      if (sym && seenRelocs.insert({index, kind}).second)
        references.push_back({kind, f, sym});
    };
    for (InputSegment *seg : f->segments()) {
      if (!image.contains(seg))
        continue;
      for (const MSILLink::Reloc &r : seg->relocs) {
        bool isFunction = r.Type == MSILLink::RelocType::FnAddr64 ||
                          r.Type == MSILLink::RelocType::FnAddr32;
        addReloc(r.Symbol,
                 isFunction ? SymbolReference::Address : SymbolReference::Data);
        if (r.Type == MSILLink::RelocType::Diff)
          addReloc(r.Symbol2, SymbolReference::Data);
      }
    }

    // The import stubs (plan 5.8.3): the declared prototype of a function
    // that may bind natively, also of one that the object only addresses.
    uint32_t numRows = f->getReader().getRowCount(TableId::MethodDef);
    for (uint32_t row = 1; row <= numRows; ++row) {
      TokenTarget t = tokens.lookup(makeToken(TableId::MethodDef, row));
      if (t.kind == TokenTarget::Method && t.form == TokenTarget::Stub &&
          t.sym && t.signature)
        stubSignatures.try_emplace(
            t.sym, NativeSignature{f, t.signature, t.signatureInfo});
    }
  }
}

void SymbolBinder::reportUnresolved() {
  // bindLateSymbols() reported an undefined entry point.
  Symbol *entry = nullptr;
  if (!ctx.arg.entry.empty() && !ctx.arg.shared)
    entry = symtab->find(ctx.arg.entry);

  DenseSet<const Symbol *> done;
  for (const SymbolReference &r : references) {
    Symbol *sym = r.sym;
    // Native data is imported and native functions are called: a function
    // reference of data and a data reference of a function have no CIL
    // meaning (plan 5.10.10, 5.9.5).
    if (auto *n = dyn_cast<NativeData>(sym)) {
      if (r.kind != SymbolReference::Data && done.insert(sym).second)
        symtab->reportUnresolved(("'" + sym->getName() + "' in " +
                                  n->nativeExport->Module + " is data, but " +
                                  toString(r.file) + " calls it as a function")
                                     .str(),
                                 sym, /*liveOnly=*/true);
      continue;
    }
    if (auto *n = dyn_cast<NativeFunction>(sym)) {
      if (r.kind == SymbolReference::Data && done.insert(sym).second)
        error("symbol type mismatch: " + toString(*sym) +
              "\n>>> defined as function in " +
              toString(n->nativeExport->File) + "\n>>> defined as data in " +
              toString(r.file));
      continue;
    }
    if (sym->isDefined() || sym->isCommon() || sym == entry ||
        !done.insert(sym).second)
      continue;

    // Plan 5.9.6 step 3: an undefined weak symbol is null, and its calls
    // trap.
    auto *u = dyn_cast<Undefined>(sym);
    bool discarded = u && u->discarded;
    if (!discarded && sym->isUndefWeak())
      continue;

    // A definition that native lookup passed over (plan 5.10.10, 5.10.14):
    // one a P/Invoke cannot bind, or data of a COFF import library or DLL
    // that a function reference skips.
    if (!discarded && sym->visibility() == ELF::STV_DEFAULT) {
      ArrayRef<const NativeExport *> rejected = symtab->getRejectedNative(sym);
      if (!rejected.empty()) {
        const NativeExport &e = *rejected.front();
        std::string msg = e.Why == NotBindable::None
                              ? ("'" + sym->getName() + "' in " + e.Module +
                                 " is data, but " + toString(r.file) +
                                 " calls it as a function")
                                    .str()
                              : getNativeRejectionText(sym->getName(), e);
        symtab->reportUnresolved(std::move(msg), sym, /*liveOnly=*/true);
        continue;
      }
    }
    // Step 4: 'undefined symbol', 'undefined hidden symbol' or a discarded
    // COMDAT copy, with the live references.
    symtab->reportUndefined(sym, /*liveOnly=*/true);
  }
}

std::optional<NativeSignature>
SymbolBinder::getNativeSignature(const Symbol *sym) const {
  if (auto it = stubSignatures.find(sym); it != stubSignatures.end())
    return it->second;
  for (const SymbolReference &r : references)
    if (r.sym == sym && r.kind == SymbolReference::Call && r.signature)
      return NativeSignature{r.file, r.signature, r.signatureInfo};
  return std::nullopt;
}

std::optional<uint32_t> SymbolBinder::findDataField(const Symbol *sym) const {
  auto it = dataFields.find(sym);
  if (it == dataFields.end())
    return std::nullopt;
  return it->second;
}

std::optional<uint32_t> SymbolBinder::getDataField(Symbol *sym) {
  if (!sym)
    return std::nullopt;
  if (auto it = dataFields.find(sym); it != dataFields.end())
    return it->second;

  OutputField of(OutputField::Address);
  of.name = sym->getName().str();
  of.sym = sym;
  if (auto *d = dyn_cast<DefinedData>(sym)) {
    // An alias that code names (plan 5.9.12), or a definition whose own
    // field the object did not give.
    if (!d->segment)
      return std::nullopt;
    of.renamable = sym->isLocal();
    of.isPublic = !sym->isLocal();
    of.file = d->segment->file;
  } else if (auto *c = dyn_cast<CommonSymbol>(sym)) {
    of.isPublic = true;
    of.file = c->segment ? c->segment->file : nullptr;
  } else if (isa<SyntheticData>(sym)) {
    // __start_X and __stop_X (plan 5.9.9).
    of.isPublic = true;
  } else if (isa<NativeData>(sym)) {
    // Stored by '<msil>init_dataimports' (plan 5.10.12).
    of.kind = OutputField::DataImport;
    of.isPublic = true;
  } else {
    // Plan 5.9.6 step 3: an undefined weak symbol's field stays 0.
    auto *u = dyn_cast<Undefined>(sym);
    if ((u && u->discarded) || !sym->isUndefWeak())
      return std::nullopt;
    of.isPublic = true;
    of.file = dyn_cast_or_null<ObjFile>(sym->getFile());
  }
  uint32_t index = out.addField(std::move(of));
  dataFields[sym] = index;
  return index;
}

const void *SymbolBinder::getFnAddrKey(const Symbol *sym) const {
  if (auto *d = dyn_cast<DefinedFunction>(sym))
    return d->method;
  if (isa<NativeFunction>(sym))
    return sym;
  auto *u = dyn_cast<Undefined>(sym);
  if (!(u && u->discarded) && sym->isUndefWeak())
    return sym;
  return nullptr;
}

std::optional<uint32_t> SymbolBinder::getFnAddrField(Symbol *sym) {
  if (!sym)
    return std::nullopt;
  const void *key = getFnAddrKey(sym);
  if (!key)
    return std::nullopt;
  if (auto it = fnAddrFields.find(key); it != fnAddrFields.end())
    return it->second;

  OutputField of(OutputField::FnAddr);
  of.sym = sym;
  if (auto *d = dyn_cast<DefinedFunction>(sym)) {
    std::optional<uint32_t> method = out.getMethodOf(d->method);
    if (!method)
      return std::nullopt;
    of.fnMethod = *method;
    of.file = d->method->file;
  } else if (auto *n = dyn_cast<NativeFunction>(sym)) {
    if (std::optional<uint32_t> stub = getCanonicalPInvokeStub(n))
      of.fnMethod = *stub;
  }
  uint32_t index = out.addField(std::move(of));
  fnAddrFields[key] = index;
  return index;
}

std::optional<uint32_t>
SymbolBinder::getCanonicalPInvokeStub(const NativeFunction *sym) {
  // The declared prototype if any, else the first signature in input order.
  std::optional<NativeSignature> ns = getNativeSignature(sym);
  if (!ns)
    return std::nullopt;
  const NativeExport &e = *sym->nativeExport;
  return getPInvokeStub(sym->getName(), e.Module, e.Entry,
                        out.mapSig(*ns->file, *ns->signature));
}

uint32_t SymbolBinder::getPInvokeStub(StringRef name, StringRef module,
                                      StringRef entry, const MethodSig &sig) {
  std::string key = name.str();
  key.push_back('\0');
  key += OutputModule::getSignatureKey(sig);
  auto [it, inserted] = pinvokeStubs.try_emplace(key, 0);
  if (!inserted)
    return it->second;
  // One MethodDef per (symbol, IL signature): 'assembly hidebysig static
  // pinvokeimpl', preservesig, no body (plan 5.10.11).
  OutputMethod m(OutputMethod::PInvoke);
  m.name = name.str();
  m.flags = MethodAttributes::Assembly | MethodAttributes::HideBySig |
            MethodAttributes::Static | MethodAttributes::PinvokeImpl;
  m.implFlags = MethodImplAttributes::IL | MethodImplAttributes::Managed |
                MethodImplAttributes::PreserveSig;
  m.sig = sig;
  m.module = module.str();
  m.entry = entry.str();
  it->second = out.addMethod(std::move(m));
  return it->second;
}

void SymbolBinder::addFnAddrFields(TrailerInput &in) {
  // The order of the methods' first FUNCTION symbol over the objects in link
  // order and their symbol tables (plan 5.9.10), which for one object is IR
  // module order, as Phase 1's FuncAddrs.
  DenseSet<uint32_t> placed;
  auto place = [&](uint32_t index) {
    const OutputField &f = out.fields[index];
    if (f.fnMethod != OutputField::NoMethod && placed.insert(index).second)
      in.FnAddrFields.push_back(
          {out.getFieldEntity(index), out.getMethodEntity(f.fnMethod)});
  };
  for (ObjFile *f : ctx.objectFiles) {
    for (auto [i, s] : llvm::enumerate(f->getRecord().Symbols)) {
      if (s.Kind != MSILLink::SymbolKind::Function)
        continue;
      Symbol *sym = f->getSymbol(i);
      const void *key = sym ? getFnAddrKey(sym) : nullptr;
      if (!key)
        continue;
      if (auto it = fnAddrFields.find(key); it != fnAddrFields.end())
        place(it->second);
    }
  }
  for (auto [index, f] : llvm::enumerate(out.fields))
    if (f.kind == OutputField::FnAddr)
      place(index);
}

bool SymbolBinder::addDataImports(TrailerInput &in) {
  // The order of the objects' symbol tables, as the merged module's globals
  // under --lto-emit-asm (O4).
  SmallVector<std::pair<Symbol *, uint32_t>, 0> imports;
  DenseSet<const Symbol *> placed;
  for (ObjFile *f : ctx.objectFiles) {
    for (Symbol *sym : f->getSymbols()) {
      if (!isa_and_nonnull<NativeData>(sym))
        continue;
      auto it = dataFields.find(sym);
      if (it != dataFields.end() && placed.insert(sym).second)
        imports.push_back({sym, it->second});
    }
  }
  if (imports.empty())
    return true;
  auto addImports = [&] {
    for (const auto &[sym, index] : imports) {
      const NativeExport &e = *cast<NativeData>(sym)->nativeExport;
      in.DataImports.push_back(
          {out.getFieldEntity(index), e.Module.str(), e.Entry.str()});
    }
  };

  // The helper's first choice (plan 5.10.12, M21):
  // System.Runtime.InteropServices.NativeLibrary when the references define
  // both members; never in a link without references, whose built-in table
  // has no NativeLibrary entry.
  if (std::optional<DataImportHelper> helper = getNativeLibraryHelper()) {
    in.ImportHelper = *helper;
    addImports();
    return true;
  }
  if (errorCount())
    return false;

  SmallVector<DataImportFunction, 2> functions;
  if (!findDataImportFunctions(ctx.arg.target, ctx.nativeIndex, functions)) {
    for (const auto &[sym, index] : imports)
      symtab->reportUnresolved(
          getDataImportUnavailableText(
              sym->getName(), cast<NativeData>(sym)->nativeExport->Module),
          sym, /*liveOnly=*/true);
    return false;
  }

  // The helper's P/Invokes, bound like any import (plan 5.10.12): Windows
  // triples LoadLibraryW(native int) and GetProcAddress(native int, native
  // int); ELF triples dlopen(native int, int32) and dlsym(native int, native
  // int); every result a native int.
  bool windows = ctx.arg.target.isOSWindows();
  MethodSig load, getSymbol;
  load.Return = getSymbol.Return = TypeSig::get(ELEMENT_TYPE_I);
  load.Params = {TypeSig::get(ELEMENT_TYPE_I)};
  if (!windows)
    load.Params.push_back(TypeSig::get(ELEMENT_TYPE_I4));
  getSymbol.Params = {TypeSig::get(ELEMENT_TYPE_I),
                      TypeSig::get(ELEMENT_TYPE_I)};
  DataImportHelper helper;
  helper.K = windows ? DataImportHelper::Kind::Kernel32
                     : DataImportHelper::Kind::Dlfcn;
  const DataImportFunction &l = functions[0], &g = functions[1];
  helper.Load =
      out.getMethodEntity(getPInvokeStub(l.name, l.module, l.entry, load));
  helper.GetSymbol =
      out.getMethodEntity(getPInvokeStub(g.name, g.module, g.entry, getSymbol));
  in.ImportHelper = helper;
  for (const DataImportFunction &fn : functions)
    helperFunctions.push_back(
        {fn.name.str(), fn.module.str(), fn.entry.str(), fn.def});
  addImports();
  return true;
}

std::optional<DataImportHelper> SymbolBinder::getNativeLibraryHelper() {
  // A capability query first, which writes no row (BCLBinder::isDefined).
  for (BCLMemberId id : {BCLMemberId::NativeLibrary_Load,
                         BCLMemberId::NativeLibrary_GetExport}) {
    Expected<BCLRequest> r = getBCLMemberRequest(id);
    if (!r) {
      // The entries' signatures name no class.
      consumeError(r.takeError());
      return std::nullopt;
    }
    if (!out.binder->isDefined(*r))
      return std::nullopt;
  }
  uint32_t load = out.getBCLMember(BCLMemberId::NativeLibrary_Load);
  uint32_t getExport = out.getBCLMember(BCLMemberId::NativeLibrary_GetExport);
  if (!load || !getExport)
    return std::nullopt;
  DataImportHelper helper;
  helper.K = DataImportHelper::Kind::NativeLibrary;
  helper.Load = out.getTokenEntity(load);
  helper.GetSymbol = out.getTokenEntity(getExport);
  return helper;
}

void SymbolBinder::setImageOffsets() {
  for (OutputField &f : out.fields)
    if (f.kind == OutputField::Address && f.sym)
      f.imageOffset = image.getSymbolOffset(f.sym);
}

void SymbolBinder::printImports() const {
  std::vector<PrintedImport> rows;
  StringSet<> seen;
  auto add = [&](StringRef symbol, StringRef module, StringRef entry,
                 bool isData, const NativeExport *e) {
    if (!seen.insert(symbol).second)
      return;
    auto *native = e ? dyn_cast_or_null<NativeLibraryFile>(e->File) : nullptr;
    rows.push_back({symbol.str(), module.str(), entry.str(), isData,
                    e ? e->Version.str() : std::string(),
                    native && native->format == NativeLibraryFile::ELFShared});
  };
  for (const SymbolReference &r : references)
    if (auto *n = dyn_cast<NativeSymbol>(r.sym))
      add(r.sym->getName(), n->nativeExport->Module, n->nativeExport->Entry,
          isa<NativeData>(r.sym), n->nativeExport);
  for (const HelperFunction &h : helperFunctions)
    add(h.name, h.module, h.entry, /*isData=*/false, h.def);
  lld::msil::printImports(rows);
}
