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
// M14 has the kinds of bitcode linking and M15 the late-bound native kinds
// (NativeFunction, NativeData; plan 5.9.6 step 2, 5.10). M20 adds the
// payloads of CIL objects (an InputMethod for a DefinedFunction, an
// InputSegment, offset and size for a DefinedData, a SyntheticSegment for a
// common symbol), the local symbols of CIL objects (STB_LOCAL, never in the
// symbol table) and SyntheticData; M22 adds the managed late-bound kinds.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_SYMBOLS_H
#define LLD_MSIL_SYMBOLS_H

#include "lld/Common/LLVM.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>
#include <string>

namespace lld {
namespace msil {
class InputChunk;
class InputFile;
class InputMethod;
class InputSegment;
class SyntheticSegment;
struct NativeExport;
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
    // Late-bound definitions in native libraries (plan 5.9.6 step 2), which
    // SymbolTable::bindLateSymbols() creates after every input is loaded and
    // which never enter the table through resolution.
    NativeFunctionKind,
    NativeDataKind,
    // A linker-defined DATA symbol at an offset of the output's data image
    // (__start_X and __stop_X, plan 5.9.9), which the writer defines through
    // SymbolTable::addSyntheticData() after the layout.
    SyntheticDataKind,
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
  // The ELF symbol binding: STB_GLOBAL or STB_WEAK for the symbols of the
  // table, STB_LOCAL for the local symbols of a CIL object, which bind only
  // within it (plan 5.9.5). As in lld/ELF, resolution also changes it in
  // place: an undefined weak reference does not extract archive members, so
  // a lazy symbol remembers that it is weak.
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

  // True if a regular (non-bitcode) input refers to the symbol or defines it:
  // the command line (-u, the entry) and CIL objects (plan 5.9.13). A bitcode
  // reference through a dllimport declaration (__imp_<name>) sets it too, as
  // in lld/COFF. LTO keeps such symbols visible.
  LLVM_PREFERRED_TYPE(bool)
  uint8_t isUsedInRegularObj : 1;

  // True if any input marks the symbol as used (llvm.used, NO_STRIP), so LTO
  // and --gc-sections keep it.
  LLVM_PREFERRED_TYPE(bool)
  uint8_t noStrip : 1;

  // Set by markLive() (plan 5.9.11) when the output keeps the symbol: a live
  // method or segment refers to it, or it is a root. Without --gc-sections
  // every symbol of a loaded CIL object is live.
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
  // (__imp_<name> in a bitcode symbol table, or the DLLIMPORT flag of a CIL
  // object's record).
  LLVM_PREFERRED_TYPE(bool)
  uint8_t dllImport : 1;

  // True for a definition through an alias (the record's ALIAS flag): a
  // DefinedFunction whose method, or a DefinedData whose segment, belongs to
  // the aliasee (plan 5.9.5).
  LLVM_PREFERRED_TYPE(bool)
  uint8_t isAlias : 1;

  StringRef getName() const { return name; }
  void setName(StringRef s) { name = s; }

  InputFile *getFile() const { return file; }

  bool isPlaceholder() const { return symbolKind == PlaceholderKind; }
  bool isDefined() const {
    return symbolKind == DefinedFunctionKind || symbolKind == DefinedDataKind ||
           symbolKind == SyntheticDataKind;
  }
  bool isUndefined() const { return symbolKind == UndefinedKind; }
  bool isCommon() const { return symbolKind == CommonKind; }
  bool isLazy() const { return symbolKind == LazyKind; }
  bool isNative() const {
    return symbolKind == NativeFunctionKind || symbolKind == NativeDataKind;
  }

  bool isGlobal() const { return binding == llvm::ELF::STB_GLOBAL; }
  bool isWeak() const { return binding == llvm::ELF::STB_WEAK; }
  bool isLocal() const { return binding == llvm::ELF::STB_LOCAL; }

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
        live(false), traced(false), referenced(false), dllImport(false),
        isAlias(false) {}

  // Makes \p sym this symbol: the kind, file, binding, type, TLS bit and
  // alias bit are this symbol's, while the name, the merged visibility and
  // the other flags stay \p sym's (lld/ELF's Symbol::overwrite).
  void overwrite(Symbol &sym, Kind k) const;

private:
  bool shouldReplace(const Defined &other) const;
};

// A definition: a function or data of a bitcode file or a CIL object, or a
// linker-defined symbol (SyntheticData).
class Defined : public Symbol {
public:
  static bool classof(const Symbol *s) { return s->isDefined(); }

  // Copies this definition, its payload included, into \p sym (defined
  // below the subclasses).
  inline void overwrite(Symbol &sym) const;

protected:
  Defined(Kind k, InputFile *file, StringRef name, uint8_t binding,
          uint8_t visibility, uint8_t type, bool tls)
      : Symbol(k, file, name, binding, visibility, type, tls) {}
};

// A function definition. The method is null for a definition of a bitcode
// file (LTO compiles it; the LTO object that defines it again carries the
// method). For an alias it is the aliasee's method, so that an alias and its
// aliasee share one method and one '<msil>fn_' field (plan 5.9.5, 5.9.7).
class DefinedFunction : public Defined {
public:
  DefinedFunction(InputFile *file, StringRef name, uint8_t binding,
                  uint8_t visibility, InputMethod *method = nullptr)
      : Defined(DefinedFunctionKind, file, name, binding, visibility,
                llvm::ELF::STT_FUNC, /*tls=*/false),
        method(method) {}

  static bool classof(const Symbol *s) {
    return s->kind() == DefinedFunctionKind;
  }

  void overwrite(Symbol &sym) const {
    Symbol::overwrite(sym, DefinedFunctionKind);
    static_cast<DefinedFunction &>(sym).method = method;
  }

  InputMethod *method;
};

// A data definition. A definition of a CIL object lies at offset in a
// segment of that object and has the record's size; an alias lies in the
// aliasee's segment, at the aliasee's offset plus its AliasOffset (plan
// 5.9.5). The segment is null for a definition of a bitcode file.
class DefinedData : public Defined {
public:
  DefinedData(InputFile *file, StringRef name, uint8_t binding,
              uint8_t visibility, bool tls, InputSegment *segment = nullptr,
              uint64_t offset = 0, uint64_t size = 0)
      : Defined(DefinedDataKind, file, name, binding, visibility,
                llvm::ELF::STT_OBJECT, tls),
        segment(segment), offset(offset), size(size) {}

  static bool classof(const Symbol *s) { return s->kind() == DefinedDataKind; }

  void overwrite(Symbol &sym) const {
    Symbol::overwrite(sym, DefinedDataKind);
    auto &s = static_cast<DefinedData &>(sym);
    s.segment = segment;
    s.offset = offset;
    s.size = size;
  }

  InputSegment *segment;
  uint64_t offset;
  uint64_t size;
};

// A linker-defined DATA symbol at \p offset bytes into the output's data
// image: __start_X and __stop_X on non-Windows triples (plan 5.9.9). It has
// no file and is never resolved against; SymbolTable::addSyntheticData()
// creates it after the layout.
class SyntheticData : public Defined {
public:
  SyntheticData(StringRef name, uint64_t offset)
      : Defined(SyntheticDataKind, /*file=*/nullptr, name,
                llvm::ELF::STB_GLOBAL, llvm::ELF::STV_DEFAULT,
                llvm::ELF::STT_OBJECT, /*tls=*/false),
        offset(offset) {}

  static bool classof(const Symbol *s) {
    return s->kind() == SyntheticDataKind;
  }

  void overwrite(Symbol &sym) const {
    Symbol::overwrite(sym, SyntheticDataKind);
    static_cast<SyntheticData &>(sym).offset = offset;
  }

  uint64_t offset;
};

void Defined::overwrite(Symbol &sym) const {
  switch (kind()) {
  case DefinedFunctionKind:
    return static_cast<const DefinedFunction *>(this)->overwrite(sym);
  case DefinedDataKind:
    return static_cast<const DefinedData *>(this)->overwrite(sym);
  case SyntheticDataKind:
    return static_cast<const SyntheticData *>(this)->overwrite(sym);
  default:
    llvm_unreachable("not a definition");
  }
}

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
// module. A common symbol of CIL objects that survives resolution gets a
// SyntheticSegment (SymbolTable::finalizeObjectSymbols): its place in the
// zero part of the output's data image (plan 5.9.9 rule 3).
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
    s.segment = segment;
  }

  static bool classof(const Symbol *s) { return s->isCommon(); }

  uint64_t getSize() const { return size; }
  uint32_t getAlignment() const { return alignment; }

  uint32_t alignment;
  uint64_t size;
  SyntheticSegment *segment = nullptr;
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

// A definition in a native library that late binding chose for a symbol
// that no input defines (plan 5.9.6 step 2, 5.10.10): the first bindable
// definition in link order. It keeps the symbol's binding and visibility; its
// file is the native library. Native definitions bind late, so they never
// preempt a definition or extract an archive member. A symbol with an
// explicit import module (an EXPLICIT_IMPORT stub, or IMPORT data with a
// module) binds to a NativeExport that late binding makes from the import
// attributes, unchecked (plan 5.10.7); its file is the CIL object that gave
// the module (SymbolTable::isExplicitImport).
class NativeSymbol : public Symbol {
public:
  static bool classof(const Symbol *s) { return s->isNative(); }

  void overwrite(Symbol &sym) const {
    Symbol::overwrite(sym, kind());
    static_cast<NativeSymbol &>(sym).nativeExport = nativeExport;
  }

  // The definition: its module, entry, kind and version.
  const NativeExport *nativeExport;

protected:
  NativeSymbol(Kind k, InputFile *file, StringRef name, uint8_t binding,
               uint8_t visibility, uint8_t type,
               const NativeExport *nativeExport)
      : Symbol(k, file, name, binding, visibility, type, /*tls=*/false),
        nativeExport(nativeExport) {}
};

// A native function: an ELF function or IFUNC, a COFF code import or a DLL's
// code export.
class NativeFunction : public NativeSymbol {
public:
  NativeFunction(InputFile *file, StringRef name, uint8_t binding,
                 uint8_t visibility, const NativeExport *nativeExport)
      : NativeSymbol(NativeFunctionKind, file, name, binding, visibility,
                     llvm::ELF::STT_FUNC, nativeExport) {}

  static bool classof(const Symbol *s) {
    return s->kind() == NativeFunctionKind;
  }
};

// Native data: an ELF object, a COFF data or const import or a DLL's data
// export, which the output imports at startup through the data-import helper
// (plan 5.10.12).
class NativeData : public NativeSymbol {
public:
  NativeData(InputFile *file, StringRef name, uint8_t binding,
             uint8_t visibility, const NativeExport *nativeExport)
      : NativeSymbol(NativeDataKind, file, name, binding, visibility,
                     llvm::ELF::STT_OBJECT, nativeExport) {}

  static bool classof(const Symbol *s) { return s->kind() == NativeDataKind; }
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
  alignas(NativeFunction) char f[sizeof(NativeFunction)];
  alignas(NativeData) char g[sizeof(NativeData)];
  alignas(SyntheticData) char h[sizeof(SyntheticData)];
};

// It is important to keep the size of SymbolUnion small for performance and
// memory usage reasons.
static_assert(sizeof(SymbolUnion) <= 64, "SymbolUnion too large");

void printTraceSymbol(const Symbol &sym, StringRef name);

// The chunk that defines \p sym: its method, its segment (for an alias, the
// aliasee's), or a common symbol's synthetic segment. Null for any other
// symbol, and for a null \p sym.
InputChunk *getDefiningChunk(const Symbol *sym);
} // namespace msil

// Returns a symbol name for an error message.
std::string toString(const msil::Symbol &sym);
// Returns the name of a symbol kind ("DefinedFunction", "CommonSymbol", ...).
std::string toString(msil::Symbol::Kind kind);
} // namespace lld

#endif
