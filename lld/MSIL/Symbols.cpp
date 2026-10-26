//===- Symbols.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/Symbols.cpp at 64f50ffd4f1c.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputFiles.h"
#include "SymbolTable.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::ELF;
using namespace lld::msil;

namespace lld {
std::string toString(const msil::Symbol &sym) { return sym.getName().str(); }

std::string toString(msil::Symbol::Kind kind) {
  switch (kind) {
  case msil::Symbol::PlaceholderKind:
    return "Placeholder";
  case msil::Symbol::DefinedFunctionKind:
    return "DefinedFunction";
  case msil::Symbol::DefinedDataKind:
    return "DefinedData";
  case msil::Symbol::CommonKind:
    return "CommonSymbol";
  case msil::Symbol::UndefinedKind:
    return "Undefined";
  case msil::Symbol::LazyKind:
    return "LazySymbol";
  case msil::Symbol::NativeFunctionKind:
    return "NativeFunction";
  case msil::Symbol::NativeDataKind:
    return "NativeData";
  case msil::Symbol::SyntheticDataKind:
    return "SyntheticData";
  }
  llvm_unreachable("invalid symbol kind");
}

namespace msil {
// lld/wasm's LazySymbol::extract, which guards against a second extraction
// (lld/ELF/Symbols.cpp:282-286 asserts instead).
void Symbol::extract() const {
  assert(isLazy());
  if (file->lazy) {
    file->lazy = false;
    symtab->addFile(file, name);
  }
}

// Print out a log message for --trace-symbol (lld/ELF/Symbols.cpp:297-311).
// A native definition is lld/ELF's shared definition.
void printTraceSymbol(const Symbol &sym, StringRef name) {
  std::string s;
  if (sym.isUndefined())
    s = ": reference to ";
  else if (sym.isLazy())
    s = ": lazy definition of ";
  else if (sym.isCommon())
    s = ": common definition of ";
  else if (sym.isNative())
    s = ": shared definition of ";
  else
    s = ": definition of ";

  message(toString(sym.file) + s + name);
}

InputChunk *getDefiningChunk(const Symbol *sym) {
  if (auto *f = dyn_cast_or_null<DefinedFunction>(sym))
    return f->method;
  if (auto *d = dyn_cast_or_null<DefinedData>(sym))
    return d->segment;
  if (auto *c = dyn_cast_or_null<CommonSymbol>(sym))
    return c->segment;
  return nullptr;
}

// lld/ELF/Symbols.h:251-259. The TLS bit is part of lld/ELF's type (STT_TLS).
void Symbol::overwrite(Symbol &sym, Kind k) const {
  if (sym.traced)
    printTraceSymbol(*this, sym.getName());
  sym.file = file;
  sym.type = type;
  sym.tls = tls;
  sym.isAlias = isAlias;
  sym.binding = binding;
  sym.symbolKind = k;
}

// lld/ELF/Symbols.cpp:314-317.
static void recordWhyExtract(const InputFile *reference,
                             const InputFile &extracted, const Symbol &sym) {
  ctx.whyExtractRecords.emplace_back(toString(reference), &extracted, sym);
}

// The most constraining visibility of two symbols: STV_DEFAULT yields to any
// other, then INTERNAL < HIDDEN < PROTECTED.
static uint8_t mergeVisibility(uint8_t v, uint8_t ov) {
  return v == STV_DEFAULT ? ov : std::min(v, ov);
}

void Symbol::resolve(const Symbol &other) {
  switch (other.kind()) {
  case UndefinedKind:
    return resolve(cast<Undefined>(other));
  case CommonKind:
    return resolve(cast<CommonSymbol>(other));
  case DefinedFunctionKind:
  case DefinedDataKind:
    return resolve(cast<Defined>(other));
  case LazyKind:
    return resolve(cast<LazySymbol>(other));
  case PlaceholderKind:
  case NativeFunctionKind:
  case NativeDataKind:
  case SyntheticDataKind:
    break;
  }
  llvm_unreachable("placeholders, native symbols (which bind late) and "
                   "linker-defined symbols are never resolved against");
}

// Derived from lld/ELF/Symbols.cpp at 64f50ffd4f1c.
//
// Symbol resolution: plan 5.9.5's table, existing state (this) against the
// incoming symbol (other). The shared-library cases are dropped (native and
// managed definitions bind late, plan 5.9.6), and so are --warn-backrefs and
// symbol versions.

// lld/ELF/Symbols.cpp:423-538 (the "Undefined" column).
void Symbol::resolve(const Undefined &other) {
  // :424-427: visibility merges to the most constraining non-default value.
  if (other.visibility() != STV_DEFAULT)
    setVisibility(mergeVisibility(visibility(), other.visibility()));

  // :428-437: a new entry takes the reference. If this is a non-weak
  // definition in a discarded COMDAT copy, override the existing undefined
  // symbol for a better error message later.
  if (isPlaceholder() ||
      (isUndefined() && other.binding != STB_WEAK && other.discarded)) {
    other.overwrite(*this);
    return;
  }

  // :439-440: --trace-symbol.
  if (traced)
    printTraceSymbol(other, getName());

  if (isLazy()) {
    // :442-449: an undefined weak will not extract archive members. See
    // comment on Lazy in Symbols.h for the details. The first weak reference
    // is the one the kind checks name, as for an undefined symbol.
    if (other.binding == STB_WEAK) {
      binding = STB_WEAK;
      type = other.type;
      tls = other.tls;
      auto *lazy = cast<LazySymbol>(this);
      if (!lazy->weakRefFile)
        lazy->weakRefFile = other.file;
      return;
    }

    // :501-505: a non-weak reference extracts the member, recorded for
    // --why-extract.
    extract();

    if (!ctx.arg.whyExtract.empty())
      recordWhyExtract(other.file, *file, *this);
    return;
  }

  if (isUndefined()) {
    // :525-530: the binding will be weak if there is at least one reference
    // and all are weak. The binding has one opportunity to change to weak: if
    // the first reference is weak.
    if (other.binding != STB_WEAK || !referenced)
      binding = other.binding;
    // :531-536: -u and the entry create an Undefined without a file and a
    // type. Adopt the real file and type from the input's undefined.
    if (!file) {
      file = other.file;
      type = other.type;
      tls = other.tls;
    }
  }
}

// lld/ELF/Symbols.cpp:541-557: compare two symbols. Return true if the new
// symbol should win.
bool Symbol::shouldReplace(const Defined &other) const {
  // :542-546: a common symbol yields only to a non-weak definition.
  if (LLVM_UNLIKELY(isCommon())) {
    if (ctx.arg.warnCommon)
      warn("common " + getName() + " is overridden");
    return !other.isWeak();
  }
  // :547-548: a definition replaces an undefined or lazy symbol.
  if (!isDefined())
    return true;

  // :550-556: incoming STB_GLOBAL overrides STB_WEAK; otherwise the first
  // definition stays.
  return !isGlobal() && other.isGlobal();
}

// lld/ELF/Symbols.cpp:559-605 (reportDuplicate, checkDuplicate): two
// non-weak definitions are a duplicate.
void Symbol::checkDuplicate(const Defined &other) const {
  if (isWeak() || other.isWeak())
    return;
  if (ctx.arg.allowMultipleDefinition)
    return;
  error("duplicate symbol: " + toString(*this) + "\n>>> defined in " +
        toString(file) + "\n>>> defined in " + toString(other.file));
}

// lld/ELF/Symbols.cpp:607-641 (the "Common" column).
void Symbol::resolve(const CommonSymbol &other) {
  if (other.visibility() != STV_DEFAULT)
    setVisibility(mergeVisibility(visibility(), other.visibility()));

  // :612-616: a non-weak definition stays, --warn-common notes it.
  if (isDefined() && !isWeak()) {
    if (ctx.arg.warnCommon)
      warn("common " + getName() + " is overridden");
    return;
  }

  // :618-627: two commons: the largest alignment, and the size and file of
  // the larger.
  if (CommonSymbol *oldSym = dyn_cast<CommonSymbol>(this)) {
    if (ctx.arg.warnCommon)
      warn("multiple common of " + getName());
    oldSym->alignment = std::max(oldSym->alignment, other.alignment);
    if (oldSym->size < other.size) {
      oldSym->file = other.file;
      oldSym->size = other.size;
    }
    return;
  }

  // :629-640: the common replaces an undefined, lazy (without extracting the
  // member) or weak defined symbol.
  other.overwrite(*this);
}

// lld/ELF/Symbols.cpp:643-650 (the "Defined" column).
void Symbol::resolve(const Defined &other) {
  if (other.visibility() != STV_DEFAULT)
    setVisibility(mergeVisibility(visibility(), other.visibility()));
  // lld/ELF reports duplicates after resolution (BitcodeFile::postParse,
  // lld/ELF/InputFiles.cpp:1949-1960); with this order the existing
  // definition is still the one to name first.
  if (isDefined())
    checkDuplicate(other);
  if (shouldReplace(other))
    other.overwrite(*this);
}

// lld/ELF/Symbols.cpp:652-679 (the "Lazy" column).
void Symbol::resolve(const LazySymbol &other) {
  if (isPlaceholder()) {
    other.overwrite(*this);
    return;
  }

  // :658-663: a defined, common or lazy symbol keeps its state (the first
  // lazy definition wins).
  if (LLVM_UNLIKELY(!isUndefined()))
    return;

  // :665-673: an undefined weak will not extract archive members. See comment
  // on Lazy in Symbols.h for the details. The lazy symbol keeps the
  // reference's kind and file for the kind checks.
  if (isWeak()) {
    uint8_t ty = type;
    bool t = tls;
    InputFile *ref = file;
    other.overwrite(*this);
    type = ty;
    tls = t;
    binding = STB_WEAK;
    cast<LazySymbol>(this)->weakRefFile = ref;
    return;
  }

  // :675-678: a non-weak undefined symbol extracts the member.
  const InputFile *oldFile = file;
  other.extract();
  if (!ctx.arg.whyExtract.empty())
    recordWhyExtract(oldFile, *file, *this);
}
} // namespace msil
} // namespace lld
