//===- Exports.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Export selection, the export class and the symbol-table payload of a
// -shared link (llvm/docs/MSIL/ImplementationPlan.md 5.9.15; exports-multiasm
// 5.2 and 10.1). The selection follows the native linkers: on ELF-like
// triples every default-visibility definition, as ld.lld -shared exports it;
// on Windows triples the dllexport definitions when any input has one, as
// link.exe does, and otherwise every default-visibility definition, as GNU
// ld's automatic export for PE DLLs does, except the copies in COMDAT groups
// (section 12 row 'Q22 as implemented' of 2026-10-26).
//
//===----------------------------------------------------------------------===//

#include "Exports.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputFiles.h"
#include "Symbols.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/SymbolTable.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <optional>

using namespace llvm;

namespace lld::msil {
// Names that never become exports: the toolchain's own ('<msil>...'; record
// v1 refuses them in SYMTAB, plan 5.8.8) and the CLR's constructor names,
// which a method of the export class must not have (exports-multiasm 10.1).
static bool isReservedName(StringRef name) {
  return name.starts_with("<msil>") || name == ".ctor" || name == ".cctor";
}

// MSVC string literals ('??_C@...'): linkonce_odr COMDAT data that the
// compiler makes in every translation unit that uses the literal. The
// selection never exports them on Windows triples, because they belong to a
// COMDAT group (selectExports; section 12 row 'Q22 as implemented' of
// 2026-10-26, item (2)), so this test is redundant there. It is kept so that
// LTO does not leave them visible to regular objects under -shared, and
// still internalizes or drops them (plan 5.9.13). A user's external const
// char array is a C identifier and stays exportable.
static bool isCompilerStringLiteral(StringRef name) {
  return name.starts_with("??_C@");
}

bool mayBeExported(const Symbol &sym) {
  // Linker-defined symbols (__start_X, __stop_X) belong to no input.
  if ((!sym.isDefined() && !sym.isCommon()) || isa<SyntheticData>(&sym))
    return false;
  if (!sym.isGlobal() && !sym.isWeak())
    return false;
  if (sym.visibility() != ELF::STV_DEFAULT &&
      sym.visibility() != ELF::STV_PROTECTED)
    return false;
  return !isReservedName(sym.getName()) &&
         !isCompilerStringLiteral(sym.getName()) &&
         !ctx.excludedSymbols.contains(&sym);
}

std::string getCRuntimeIdentity() {
  for (StringRef name : ctx.arg.libraries)
    if (name == "c" || name == "msvcrt" || name == "ucrt" ||
        name == "ucrtbase" || name == "crtdll" || name.starts_with("msvcr"))
      return name.str();
  return "none";
}

Expected<std::pair<StringRef, StringRef>> parseClassName(StringRef value) {
  StringRef ns, name = value;
  size_t dot = value.rfind('.');
  if (dot != StringRef::npos) {
    ns = value.take_front(dot);
    name = value.drop_front(dot + 1);
  }
  // A namespace or name that starts with '<msil>' (D11's prefix for the
  // toolchain's own names) or the class '<Module>' could repeat the
  // TypeNamespace and TypeName of a TypeDef that msil-ld makes ('<Module>',
  // '<msil>Data', '<msil>SymbolTable', the spill classes, ...), which
  // ECMA-335 II.22.37 forbids (section 12 row 'M22.a item (1) and 5.9.3
  // clarified' of 2026-10-26). 'Ns.<Module>' cannot collide.
  bool reserved = ns.starts_with("<msil>") || name.starts_with("<msil>") ||
                  value == "<Module>";
  if (name.empty() || value.contains('/') ||
      value.size() > cli::MaxIdentifierBytes || reserved)
    return createStringError("invalid --export-class value '" + value + "'");
  return std::make_pair(ns, name);
}

void excludeLibs() {
  if (ctx.arg.excludeLibs.empty())
    return;
  bool all = ctx.arg.excludeLibs.contains("ALL");
  auto visit = [&](InputFile *file) {
    if (file->archiveName.empty() ||
        !(all ||
          ctx.arg.excludeLibs.contains(sys::path::filename(file->archiveName))))
      return;
    for (Symbol *sym : file->getSymbols())
      if (sym && (sym->isDefined() || sym->isCommon()) &&
          sym->getFile() == file)
        ctx.excludedSymbols.insert(sym);
  };
  for (ObjFile *file : ctx.objectFiles)
    visit(file);
  for (BitcodeFile *file : ctx.bitcodeFiles)
    visit(file);
}

namespace {
// A candidate (Q22): the definition that won resolution, at the SYMTAB entry
// of the object that defines it.
struct Candidate {
  ObjFile *file;
  uint32_t index;
  Symbol *sym;
  bool dllExport;
};

// The COMDAT groups of an object, by member: the FUNCTION symbols (SYMTAB
// indexes) and the segments (SEGMENTS indexes) that belong to one.
struct ComdatMembers {
  BitVector functions, segments;

  explicit ComdatMembers(const MSILLink::Record &record)
      : functions(record.Symbols.size()), segments(record.Segments.size()) {
    for (const MSILLink::ComdatGroup &group : record.Comdats)
      for (auto [kind, index] : group.Members)
        (kind == MSILLink::ComdatMemberKind::FunctionSymbol ? functions
                                                            : segments)
            .set(index);
  }

  // Whether the defined SYMTAB entry \p index of \p record belongs to a
  // COMDAT group of its object: a FUNCTION symbol that is a member, or a DATA
  // symbol whose segment is one. An alias belongs to its aliasee's group (a
  // DATA alias lies in its aliasee's segment).
  bool contains(const MSILLink::Record &record, uint32_t index) const {
    const MSILLink::Symbol *s = &record.Symbols[index];
    if (s->Flags & MSILLink::Alias) {
      index = s->Aliasee;
      s = &record.Symbols[index];
    }
    if (s->Kind == MSILLink::SymbolKind::Function)
      return functions.test(index);
    return (s->Flags & MSILLink::BindingMask) != MSILLink::BindingCommon &&
           segments.test(s->Segment);
  }
};
} // namespace

// The exported definitions in input order: the CIL objects in load order,
// then the LTO objects (bitcode definitions are the LTO objects' after LTO),
// each in SYMTAB order. A symbol is visited at the object that owns the
// symbol table's entry, so a definition that lost resolution (a weak copy, a
// discarded COMDAT copy, a smaller common) is passed over.
//
// On Windows triples a candidate that is not DLLEXPORT and belongs to a
// COMDAT group of its object is never exported, by Q22's automatic export or
// by --export-all-symbols (section 12 row 'Q22 as implemented' of
// 2026-10-26, item (2)). These are the copies that the compiler makes in
// every translation unit that includes a header: MSVC string literals,
// __declspec(selectany) variables and the linkonce_odr copies of header
// inline functions. Exporting them made Q21's warning about a program's own
// definition shadowing a library's fire for every program that includes the
// header. LTO objects carry the merged module's groups, so the rule applies
// to the bitcode definitions that LTO kept visible (plan 5.9.13).
static SmallVector<Candidate, 0> selectExports() {
  SmallVector<Candidate, 0> candidates;
  bool windows = ctx.arg.target.isOSWindows();
  for (ObjFile *file : ctx.objectFiles) {
    const MSILLink::Record &record = file->getRecord();
    std::optional<ComdatMembers> comdats;
    if (windows && !record.Comdats.empty())
      comdats.emplace(record);
    for (auto [i, s] : enumerate(record.Symbols)) {
      if ((s.Flags & MSILLink::Undefined) ||
          (s.Flags & MSILLink::BindingMask) == MSILLink::BindingLocal)
        continue;
      Symbol *sym = file->getSymbol(i);
      if (!sym || sym->getFile() != file || !mayBeExported(*sym))
        continue;
      bool dllExport = s.Flags & MSILLink::DLLExport;
      if (!dllExport && comdats && comdats->contains(record, i))
        continue;
      candidates.push_back({file, static_cast<uint32_t>(i), sym, dllExport});
    }
  }

  // Windows triples: the dllexport definitions when there are any, unless
  // --export-all-symbols.
  if (ctx.arg.target.isOSWindows() && !ctx.arg.exportAllSymbols &&
      any_of(candidates, [](const Candidate &c) { return c.dllExport; }))
    llvm::erase_if(candidates, [](const Candidate &c) { return !c.dllExport; });
  return candidates;
}

void markExportsLive() {
  // A thread-local candidate is computeExportPlan()'s error, not a root.
  for (const Candidate &c : selectExports())
    if (!c.sym->isTLS())
      c.sym->noStrip = true;
}

// The assembly name: --assembly-name or -soname, else the stem of -o (only
// the last extension is dropped; plan 5.9.12), as the writer names the
// Assembly row.
static std::string getAssemblyName() {
  if (!ctx.arg.assemblyName.empty())
    return ctx.arg.assemblyName.str();
  StringRef stem = sys::path::stem(ctx.arg.outputFile);
  if (stem.empty())
    stem = sys::path::filename(ctx.arg.outputFile);
  return stem.str();
}

// The triple of the symbol table: the codegen triple without its environment
// version (x86_64-pc-windows-msvc19.40.0-msil gives
// x86_64-pc-windows-msvc-msil).
static std::string getTableTriple() {
  Triple tt = ctx.arg.target;
  if (tt.getEnvironment() != Triple::UnknownEnvironment &&
      !tt.getEnvironmentVersion().empty())
    tt.setEnvironment(tt.getEnvironment());
  return tt.str();
}

// The ABI version and the data model of the inputs' records, which the
// driver has checked to be equal (plan 5.8.9); without a CIL object, the
// codegen triple's (5.8.9's rule for a bitcode input).
static MSILLink::Header getRecordHeader() {
  if (!ctx.objectFiles.empty())
    return ctx.objectFiles.front()->getRecord().Hdr;
  MSILLink::Header h;
  const Triple &tt = ctx.arg.target;
  MSILLink::setDataModel(h, tt, tt.isArch64Bit() ? 8 : 4,
                         tt.getDefaultWCharSize());
  return h;
}

// The exported form of a candidate: its kind, the size, alignment and flags
// of a variable (the segment's alignment, lowered by the symbol's offset in
// it; CONST for a READONLY segment), the flags of a function (ABI when its
// MethodDef has a SIGNATURES entry), and an alias's aliasee.
static ExportedSymbol getExportedSymbol(const Candidate &c) {
  Symbol *sym = c.sym;
  ExportedSymbol e{};
  e.Sym = sym;
  if (auto *d = dyn_cast<DefinedData>(sym)) {
    e.IsData = true;
    e.Size = d->size;
    if (const InputSegment *seg = d->segment) {
      e.AlignLog2 = seg->alignLog2;
      if (d->offset)
        e.AlignLog2 = std::min<unsigned>(e.AlignLog2, countr_zero(d->offset));
      if (seg->isReadOnly())
        e.Flags |= cli::LibraryDataSymbol::Const;
    }
    if (sym->isWeak())
      e.Flags |= cli::LibraryDataSymbol::Weak;
  } else if (auto *common = dyn_cast<CommonSymbol>(sym)) {
    e.IsData = true;
    e.Size = common->getSize();
    e.AlignLog2 = Log2_32(common->getAlignment());
  } else if (auto *fn = dyn_cast<DefinedFunction>(sym)) {
    if (sym->isWeak())
      e.Flags |= cli::LibraryFunctionFlags::Weak;
    if (fn->method && fn->method->signatureInfo) {
      e.Flags |= cli::LibraryFunctionFlags::ABI;
      if (fn->method->signatureInfo->Flags & MSILLink::VarArg)
        e.Flags |= cli::LibraryFunctionFlags::Variadic;
    }
  }

  if (sym->isAlias) {
    const MSILLink::Symbol &s = c.file->getRecord().Symbols[c.index];
    Symbol *aliasee = c.file->getSymbol(s.Aliasee);
    if (aliasee && (aliasee->isLocal() || aliasee->getFile() == c.file))
      e.AliasOf = aliasee;
  }
  return e;
}

Expected<ExportPlan> computeExportPlan() {
  ExportPlan plan;
  Error err = Error::success();

  std::string className = ctx.arg.exportClass.empty()
                              ? getAssemblyName()
                              : ctx.arg.exportClass.str();
  Expected<std::pair<StringRef, StringRef>> parsed = parseClassName(className);
  if (parsed) {
    plan.Namespace = parsed->first.str();
    plan.Name = parsed->second.str();
  } else {
    err = joinErrors(std::move(err), parsed.takeError());
  }

  for (const Candidate &c : selectExports()) {
    if (c.sym->isTLS()) {
      err = joinErrors(
          std::move(err),
          createStringError("thread-local storage is not supported yet: " +
                            toString(*c.sym) + " in " + toString(c.file)));
      continue;
    }
    ExportedSymbol e = getExportedSymbol(c);
    (e.IsData ? plan.Data : plan.Functions).push_back(e);
  }
  if (err)
    return std::move(err);

  // The symbol table (Q25; plan 5.9.15, M17.c item 8).
  cli::LibrarySymbolTable table;
  if (ctx.arg.interfaceOnly)
    table.Flags = cli::LibraryTableFlags::Interface;
  MSILLink::Header h = getRecordHeader();
  table.ABIVersion = h.ABIVersion;
  table.Triple = getTableTriple();
  table.CRuntime = getCRuntimeIdentity();
  table.PointerSize = h.PointerSize;
  table.LongSize = h.LongSize;
  table.WCharSize = h.WCharSize;
  table.LongDoubleSize = h.LongDoubleSize;
  table.ExportClasses.push_back(
      plan.Namespace.empty() ? plan.Name : plan.Namespace + "." + plan.Name);
  for (const ExportedSymbol &e : plan.Data) {
    cli::LibraryDataSymbol &d = table.DataSymbols.emplace_back();
    d.Name = e.Sym->getName().str();
    d.Size = e.Size;
    d.AlignLog2 = e.AlignLog2;
    d.Flags = e.Flags;
  }
  for (const ExportedSymbol &e : plan.Functions) {
    if (!e.Flags)
      continue;
    cli::LibraryFunctionFlags &f = table.Functions.emplace_back();
    f.Name = e.Sym->getName().str();
    f.Flags = e.Flags;
    if (e.Flags & cli::LibraryFunctionFlags::ABI) {
      f.Signature = *cast<DefinedFunction>(e.Sym)->method->signatureInfo;
      f.Signature.Token = 0;
    }
  }
  cli::encodeSymbolTable(table, plan.SymbolTablePayload);
  return std::move(plan);
}
} // namespace lld::msil
