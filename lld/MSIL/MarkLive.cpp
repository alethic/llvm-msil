//===- MarkLive.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/MarkLive.cpp at 64f50ffd4f1c.
//
// This file implements --gc-sections (plan 5.9.11), which is a feature to
// remove unused chunks from the output. Unused chunks are those that are not
// reachable from known root symbols or chunks. This feature is implemented as
// a mark-sweep garbage collector.
//
// Here's how it works. Each InputChunk (a method, a segment or a common
// symbol's synthetic segment) has a "live" bit. The bit is off by default.
// Starting with the GC-roots, visit all reachable chunks and set their live
// bits. The writer will then ignore chunks whose live bits are off, so that
// such chunks do not appear in the output.
//
// The roots are more conservative than lld/ELF's (judge-c-fidelity 6.3) and
// keep whatever lld-link's /OPT:REF keeps: the entry symbol, -u symbols,
// NO_STRIP symbols, RETAIN segments, every INIT and FINI entry, every segment
// of a section that cli::classifySection puts in the constructor-table,
// destructor-table or unsupported class (the writer runs the tables and
// reports the rest, so they are never dropped silently; thread-local data is
// not such a root), every segment of a section that a referenced __start_ or
// __stop_ symbol names (lld/ELF's -z nostart-stop-gc), and, on Windows
// triples, every segment of an explicit section (one whose name is not 5.8.8's
// .rodata, .data, .bss, .tdata or .tbss) that belongs to no COMDAT group, as
// lld-link's /OPT:REF discards only COMDAT sections. Marking follows the token
// sites of live methods (plan 5.9.7), the relocations of live segments, and
// each symbol to its defining method or segment (an alias's is its
// aliasee's). A live segment of an explicit section keeps every segment of
// its object with the same section name and COMDAT group (or none) live, as
// lld/ELF keeps a whole input section, of which an object has one per section
// name and group, where the record has one segment per global (plan 5.9.11 as
// amended).
//
//===----------------------------------------------------------------------===//

#include "MarkLive.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputFiles.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "TokenMap.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Strings.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/ImageLayout.h"
#include "llvm/Support/Debug.h"
#include <tuple>

#define DEBUG_TYPE "lld"

using namespace llvm;

namespace lld::msil {
namespace {
class MarkLive {
public:
  void run();

private:
  void enqueue(Symbol *sym);
  void enqueue(InputChunk *chunk);
  void enqueueSection(const InputSegment *seg);
  void mark();

  // A list of chunks to visit.
  SmallVector<InputChunk *, 256> queue;

  // The segments of each explicit section of each object, by object, section
  // name and COMDAT group (-1 for none): lld/ELF's input sections. An entry
  // goes once its first segment is marked, which enqueues all of them.
  DenseMap<std::tuple<const ObjFile *, StringRef, int32_t>,
           SmallVector<InputSegment *, 1>>
      explicitSections;
};
} // namespace

// True if \p name is a section that the IR names, not one of the default
// sections that the record gives a global without one (plan 5.8.8).
static bool isExplicitSection(StringRef name) {
  return name != ".rodata" && name != ".data" && name != ".bss" &&
         name != ".tdata" && name != ".tbss";
}

// True if the output keeps every segment of section \p name whatever refers
// to it: the constructor and destructor tables, which the writer runs, and
// the sections it reports (plan 5.9.9). Thread-local data is not kept: an
// unreferenced segment of it is dropped, not reported.
static bool isTableSection(StringRef name, cli::SectionRules rules) {
  switch (cli::classifySection(name, rules)) {
  case cli::SectionClass::ConstructorTable:
  case cli::SectionClass::DestructorTable:
  case cli::SectionClass::Unsupported:
    return true;
  case cli::SectionClass::Data:
  case cli::SectionClass::TLS:
    return false;
  }
  llvm_unreachable("unknown section class");
}

void MarkLive::enqueue(Symbol *sym) {
  if (!sym || sym->live)
    return;
  LLVM_DEBUG(dbgs() << "markLive: " << sym->getName() << "\n");
  sym->live = true;
  if (InputChunk *chunk = getDefiningChunk(sym))
    enqueue(chunk);
}

void MarkLive::enqueue(InputChunk *chunk) {
  if (!chunk || chunk->live || chunk->discarded)
    return;
  LLVM_DEBUG(dbgs() << "markLive: " << toString(chunk) << "\n");
  chunk->live = true;
  queue.push_back(chunk);
}

// Marks the other segments of \p seg's explicit section live (plan 5.9.11
// (a)): the segments of its object with its section name and COMDAT group.
void MarkLive::enqueueSection(const InputSegment *seg) {
  auto it = explicitSections.find({seg->file, seg->sectionName, seg->comdat});
  if (it == explicitSections.end())
    return;
  SmallVector<InputSegment *, 1> members = std::move(it->second);
  explicitSections.erase(it);
  for (InputSegment *member : members)
    enqueue(member);
}

void MarkLive::run() {
  // Add GC root symbols: the entry symbol, which '<msil>Main' calls, and the
  // -u symbols.
  if (!ctx.arg.entry.empty() && !ctx.arg.shared)
    enqueue(symtab->find(ctx.arg.entry));
  for (StringRef name : ctx.arg.undefined)
    enqueue(symtab->find(name));

  // We need to preserve any no-strip symbol.
  for (Symbol *sym : symtab->symbols())
    if (sym->isNoStrip())
      enqueue(sym);

  // The sections that a referenced __start_<X> or __stop_<X> names, on the
  // triples that define such symbols (plan 5.9.9).
  DenseSet<StringRef> startStopSections;
  if (!ctx.arg.target.isOSWindows()) {
    for (Symbol *sym : symtab->symbols()) {
      if (!sym->isUndefined() && !(sym->isLazy() && sym->isWeak()))
        continue;
      StringRef name = sym->getName();
      if ((name.consume_front("__start_") || name.consume_front("__stop_")) &&
          isValidCIdentifier(name))
        startStopSections.insert(name);
    }
  }

  // The section rules of the triple (plan 5.9.9 rule 1), as the writer
  // chooses them.
  cli::SectionRules rules = ctx.arg.target.isOSWindows()
                                ? cli::SectionRules::COFF
                                : cli::SectionRules::ELF;

  for (ObjFile *f : ctx.objectFiles) {
    for (InputSegment *seg : f->segments())
      if (isExplicitSection(seg->sectionName))
        explicitSections[{f, seg->sectionName, seg->comdat}].push_back(seg);
  }

  for (ObjFile *f : ctx.objectFiles) {
    const MSILLink::Record &record = f->getRecord();
    // The object's no-strip symbols, its local ones included.
    for (auto [i, s] : llvm::enumerate(record.Symbols))
      if (s.Flags & MSILLink::NoStrip)
        enqueue(f->getSymbol(i));
    // Every INIT and FINI entry, unless its COMDAT group was discarded.
    for (const MSILLink::InitEntry &e : record.Inits)
      if (!e.ComdatPlusOne || f->isComdatKept(e.ComdatPlusOne - 1))
        enqueue(f->getSymbol(e.Symbol));
    // RETAIN segments, the table sections, the sections of __start_ and
    // __stop_ and, under COFF rules, the explicit sections outside COMDAT
    // groups (lld-link's /OPT:REF).
    for (InputSegment *seg : f->segments())
      if (seg->isRetained() || isTableSection(seg->sectionName, rules) ||
          startStopSections.contains(seg->sectionName) ||
          (rules == cli::SectionRules::COFF && seg->comdat < 0 &&
           isExplicitSection(seg->sectionName)))
        enqueue(seg);
  }

  mark();
}

void MarkLive::mark() {
  // Follow token sites and relocations to mark all reachable chunks.
  while (!queue.empty()) {
    InputChunk *c = queue.pop_back_val();
    ObjFile *f = c->file;
    if (auto *m = dyn_cast<InputMethod>(c)) {
      const TokenMap &tokens = f->getTokenMap();
      for (const TokenSite &site : m->sites)
        enqueue(tokens.lookup(site.token).sym);
      continue;
    }
    if (auto *seg = dyn_cast<InputSegment>(c)) {
      enqueueSection(seg);
      for (const MSILLink::Reloc &r : seg->relocs) {
        enqueue(f->getSymbol(r.Symbol));
        if (r.Type == MSILLink::RelocType::Diff)
          enqueue(f->getSymbol(r.Symbol2));
      }
    }
  }
}

void markLive() {
  // Without --gc-sections every chunk that lost no resolution is live, and so
  // is every symbol of the objects.
  if (!ctx.arg.gcSections) {
    for (ObjFile *f : ctx.objectFiles) {
      for (InputMethod *m : f->methods())
        m->live = !m->discarded;
      for (InputSegment *seg : f->segments())
        seg->live = !seg->discarded;
      for (Symbol *sym : f->getSymbols())
        if (sym)
          sym->live = true;
    }
    for (SyntheticSegment *seg : ctx.syntheticSegments)
      seg->live = !seg->discarded;
    return;
  }

  LLVM_DEBUG(dbgs() << "markLive\n");

  MarkLive marker;
  marker.run();

  // Report garbage-collected sections.
  if (ctx.arg.printGcSections) {
    for (const ObjFile *obj : ctx.objectFiles) {
      for (InputMethod *m : obj->methods())
        if (!m->live && !m->discarded)
          message("removing unused section " + toString(m));
      for (InputSegment *seg : obj->segments())
        if (!seg->live && !seg->discarded)
          message("removing unused section " + toString(seg));
    }
    for (SyntheticSegment *seg : ctx.syntheticSegments)
      if (!seg->live && !seg->discarded)
        message("removing unused section " + toString(seg));
  }
}
} // namespace lld::msil
