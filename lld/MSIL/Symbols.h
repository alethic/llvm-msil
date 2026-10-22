//===- Symbols.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/Symbols.h at 64f50ffd4f1c.
//
// The symbols of msil-ld. The symbol kinds and the resolution rules
// (Symbol::resolve) are a port of lld/ELF (lld/ELF/Symbols.h and
// lld/ELF/Symbols.cpp), plan 5.9.5: a symbol has an ELF binding (STB_GLOBAL or
// STB_WEAK; local symbols never enter the symbol table), an ELF visibility
// (STV_*), a type (function or data), a TLS bit and the flags below.
//
// M14 has the kinds of bitcode linking. M20 adds the payloads of CIL objects
// (an InputMethod for a DefinedFunction, an InputSegment and offset for a
// DefinedData) and SyntheticData; M15 and M22 add the late-bound kinds.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_SYMBOLS_H
#define LLD_MSIL_SYMBOLS_H

#include "lld/Common/LLVM.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <string>

namespace lld {
namespace msil {
class InputFile;
class Undefined;
class CommonSymbol;
class Defined;
class LazySymbol;

// The base class for real symbol classes.
//
// As in lld/ELF, a symbol table entry changes its kind in place: resolution
// overwrites the entry with the winning symbol (see overwrite()), so every
// class below has the same base and no virtual functions, and SymbolUnion is
// large enough for any of them.
class Symbol {
public:
  enum Kind : uint8_t {
    // An entry that SymbolTable::insert() has just created and that nothing
    // has resolved yet.
    PlaceholderKind,
    DefinedFunctionKind,
    DefinedDataKind,
    CommonKind,
    UndefinedKind,
    LazyKind,
  };

  Kind kind() const { return static_cast<Kind>(symbolKind); }

  // The file from which this symbol was created: the input that defines it,
  // the input whose reference created it, or the archive member (or
  // --start-lib file) of a lazy symbol. Null for a reference that no input
  // makes (-u and the entry symbol).
  InputFile *file;

  // Symbols are referenced by pointer throughout the linker, so an implicit
  // copy would create a dangerous duplicate.
  Symbol(const Symbol &o) = delete;
  Symbol &operator=(const Symbol &) = delete;

protected:
  StringRef name;

public:
  // The ELF symbol binding: STB_GLOBAL or STB_WEAK. As in lld/ELF, resolution
  // also changes it in place: an undefined weak reference does not extract
  // archive members, so a lazy symbol remembers that it is weak.
  uint8_t binding;

  // The symbol type: STT_FUNC (a function), STT_OBJECT (data) or STT_NOTYPE
  // (a reference that no input makes, and a lazy symbol nothing referenced).
  uint8_t type;

  uint8_t symbolKind;

  // The ELF visibility (STV_*): the most constraining non-default visibility
  // of all inputs' symbols of this name.
  uint8_t vis : 2;

  // True for thread-local data.
  LLVM_PREFERRED_TYPE(bool)
  uint8_t tls : 1;

  // True if a regular (non-bitcode) input refers to the symbol: the
  // command line (-u, the entry) in M14, CIL objects from M20. A bitcode
  // reference through a dllimport declaration (__imp_<name>) sets it too, as
  // in lld/COFF. LTO keeps such symbols visible (plan 5.9.13).
  LLVM_PREFERRED_TYPE(bool)
  uint8_t isUsedInRegularObj : 1;

  // True if any input marks the symbol as used (llvm.used, NO_STRIP), so LTO
  // and --gc-sections keep it.
  LLVM_PREFERRED_TYPE(bool)
  uint8_t noStrip : 1;

  // Set by --gc-sections (M20) when the symbol's method or segment is kept.
  LLVM_PREFERRED_TYPE(bool)
  uint8_t live : 1;

  // True if this symbol is specified by --trace-symbol (-y).
  LLVM_PREFERRED_TYPE(bool)
  uint8_t traced : 1;

  // True if an input file or the command line refers to the symbol. As in
  // lld/ELF, the binding of an undefined symbol becomes STB_WEAK only if its
  // first reference is weak.
  LLVM_PREFERRED_TYPE(bool)
  uint8_t referenced : 1;

  // True if an input refers to the symbol through a dllimport declaration
  // (__imp_<name> in a bitcode symbol table; from M20, the record's flag).
  LLVM_PREFERRED_TYPE(bool)
  uint8_t dllImport : 1;

  StringRef getName() const { return name; }
  void setName(StringRef s) { name = s; }

  InputFile *getFile() const { return file; }

  bool isPlaceholder() const { return symbolKind == PlaceholderKind; }
  bool isDefined() const {
    return symbolKind == DefinedFunctionKind || symbolKind == DefinedDataKind;
  }
  bool isUndefined() const { return symbolKind == UndefinedKind; }
  bool isCommon() const { return symbolKind == CommonKind; }
  bool isLazy() const { return symbolKind == LazyKind; }

  bool isGlobal() const { return binding == llvm::ELF::STB_GLOBAL; }
  bool isWeak() const { return binding == llvm::ELF::STB_WEAK; }

  // True if this is an undefined weak symbol. A weak reference that met a
  // lazy symbol leaves a lazy symbol with a weak binding, so lazy symbols
  // count. This only works once all input files have been added.
  bool isUndefWeak() const { return isWeak() && (isUndefined() || isLazy()); }

  bool isFunc() const { return type == llvm::ELF::STT_FUNC; }
  bool isData() const { return type == llvm::ELF::STT_OBJECT; }
  bool isTLS() const { return tls; }
  bool isNoStrip() const { return noStrip; }

  uint8_t visibility() const { return vis; }
  void setVisibility(uint8_t v) { vis = v; }

  // Symbol resolution, ported from lld/ELF (plan 5.9.5). Every symbol an input
  // file presents is resolved against the table's entry of the same name;
  // resolve() overwrites the entry when the new symbol wins, extracts archive
  // members, merges commons and reports duplicates.
  void resolve(const Symbol &other);
  void resolve(const Undefined &other);
  void resolve(const CommonSymbol &other);
  void resolve(const Defined &other);
  void resolve(const LazySymbol &other);

  // If this is a lazy symbol, loads the input file that defines it (an
  // archive member or a --start-lib file) and adds its symbols to the table.
  void extract() const;

  // Reports a duplicate definition if both this symbol and \p other are
  // non-weak definitions (unless --allow-multiple-definition).
  void checkDuplicate(const Defined &other) const;

protected:
  Symbol(Kind k, InputFile *file, StringRef name, uint8_t binding,
         uint8_t visibility, uint8_t type, bool tls)
      : file(file), name(name), binding(binding), type(type), symbolKind(k),
        vis(visibility), tls(tls), isUsedInRegularObj(false), noStrip(false),
        live(false), traced(false), referenced(false), dllImport(false) {}

  // Makes \p sym this symbol: the kind, file, binding, type and TLS bit are
  // this symbol's, while the name, the merged visibility and the flags stay
  // \p sym's (lld/ELF's Symbol::overwrite).
  void overwrite(Symbol &sym, Kind k) const;

private:
  bool shouldReplace(const Defined &other) const;
};

// A definition: a function or data. M14 definitions come from bitcode, so
// they carry no method or segment yet (M20 adds them).
class Defined : public Symbol {
public:
  static bool classof(const Symbol *s) { return s->isDefined(); }

  void overwrite(Symbol &sym) const { Symbol::overwrite(sym, kind()); }

protected:
  Defined(Kind k, InputFile *file, StringRef name, uint8_t binding,
          uint8_t visibility, uint8_t type, bool tls)
      : Symbol(k, file, name, binding, visibility, type, tls) {}
};

class DefinedFunction : public Defined {
public:
  DefinedFunction(InputFile *file, StringRef name, uint8_t binding,
                  uint8_t visibility)
      : Defined(DefinedFunctionKind, file, name, binding, visibility,
                llvm::ELF::STT_FUNC, /*tls=*/false) {}

  static bool classof(const Symbol *s) {
    return s->kind() == DefinedFunctionKind;
  }
};

class DefinedData : public Defined {
public:
  DefinedData(InputFile *file, StringRef name, uint8_t binding,
              uint8_t visibility, bool tls)
      : Defined(DefinedDataKind, file, name, binding, visibility,
                llvm::ELF::STT_OBJECT, tls) {}

  static bool classof(const Symbol *s) { return s->kind() == DefinedDataKind; }
};

// Represents a common symbol.
//
// On Unix, it is traditionally allowed to write variable definitions
// without initialization expressions (such as "int foo;") to header
// files. Such definition is called "tentative definition".
//
// The linker allows common symbols to be replaced by regular defined
// symbols. If several common symbols of one name meet, the largest size and
// the largest alignment win. Under LTO the IR linker merges the commons the
// same way, so a common symbol that survives resolution stays in the merged
// module.
class CommonSymbol : public Symbol {
public:
  CommonSymbol(InputFile *file, StringRef name, uint8_t binding,
               uint8_t visibility, bool tls, uint32_t alignment, uint64_t size)
      : Symbol(CommonKind, file, name, binding, visibility,
               llvm::ELF::STT_OBJECT, tls),
        alignment(alignment), size(size) {}

  void overwrite(Symbol &sym) const {
    Symbol::overwrite(sym, CommonKind);
    auto &s = static_cast<CommonSymbol &>(sym);
    s.alignment = alignment;
    s.size = size;
  }

  static bool classof(const Symbol *s) { return s->isCommon(); }

  uint64_t getSize() const { return size; }
  uint32_t getAlignment() const { return alignment; }

  uint32_t alignment;
  uint64_t size;
};

class Undefined : public Symbol {
public:
  Undefined(InputFile *file, StringRef name, uint8_t binding,
            uint8_t visibility, uint8_t type, bool tls = false,
            bool discarded = false)
      : Symbol(UndefinedKind, file, name, binding, visibility, type, tls),
        discarded(discarded) {}

  void overwrite(Symbol &sym) const {
    Symbol::overwrite(sym, UndefinedKind);
    static_cast<Undefined &>(sym).discarded = discarded;
  }

  static bool classof(const Symbol *s) { return s->kind() == UndefinedKind; }

  // True if this undefined symbol stands for a definition in a COMDAT group
  // that an earlier input already presented, so the group of `file` was
  // discarded (lld/ELF's discardedSecIdx). It is not a reference, and LTO
  // must not treat `file` as the symbol's prevailing definition.
  bool discarded;
};

// LazySymbol symbols represent symbols in object files between --start-lib and
// --end-lib options. LLD also handles traditional archives as if all the files
// in the archive are surrounded by --start-lib and --end-lib.
//
// A special complication is the handling of weak undefined symbols. They should
// not load a file, but we have to remember we have seen both the weak undefined
// and the lazy. We represent that with a lazy symbol with a weak binding. This
// means that code looking for undefined symbols normally also has to take lazy
// symbols into consideration.
//
// Such a lazy symbol also keeps the weak reference's kind (its type and TLS
// bit) and file, so that the kind checks of plan 5.9.5 compare a definition
// that later replaces it with the reference, as they would without the
// archive. A lazy symbol that no weak reference met has the type STT_NOTYPE
// and matches any kind.
class LazySymbol : public Symbol {
public:
  LazySymbol(InputFile &file, StringRef name)
      : Symbol(LazyKind, &file, name, llvm::ELF::STB_GLOBAL,
               llvm::ELF::STV_DEFAULT, llvm::ELF::STT_NOTYPE, /*tls=*/false) {}

  void overwrite(Symbol &sym) const {
    Symbol::overwrite(sym, LazyKind);
    static_cast<LazySymbol &>(sym).weakRefFile = weakRefFile;
  }

  static bool classof(const Symbol *s) { return s->kind() == LazyKind; }

  // The input of the first weak reference that met this lazy symbol, whose
  // kind the symbol carries; null if no weak reference did. `file` stays the
  // archive member (or --start-lib file) that defines the symbol.
  InputFile *weakRefFile = nullptr;
};

// A buffer class that is large enough to hold any Symbol-derived
// object. SymbolTable::insert() allocates every table entry as one, so that
// overwrite() can turn the entry into a symbol of any kind.
union SymbolUnion {
  alignas(DefinedFunction) char a[sizeof(DefinedFunction)];
  alignas(DefinedData) char b[sizeof(DefinedData)];
  alignas(CommonSymbol) char c[sizeof(CommonSymbol)];
  alignas(Undefined) char d[sizeof(Undefined)];
  alignas(LazySymbol) char e[sizeof(LazySymbol)];
};

// It is important to keep the size of SymbolUnion small for performance and
// memory usage reasons.
static_assert(sizeof(SymbolUnion) <= 64, "SymbolUnion too large");

void printTraceSymbol(const Symbol &sym, StringRef name);
} // namespace msil

// Returns a symbol name for an error message.
std::string toString(const msil::Symbol &sym);
// Returns the name of a symbol kind ("DefinedFunction", "CommonSymbol", ...).
std::string toString(msil::Symbol::Kind kind);
} // namespace lld

#endif
