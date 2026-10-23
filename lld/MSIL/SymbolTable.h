//===- SymbolTable.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/SymbolTable.h at 64f50ffd4f1c.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_SYMBOL_TABLE_H
#define LLD_MSIL_SYMBOL_TABLE_H

#include "InputFiles.h"
#include "LTO.h"
#include "Symbols.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include <memory>
#include <vector>

namespace lld::msil {
// SymbolTable is a bucket of all known symbols, including defined,
// undefined, or lazy symbols (the last one is symbols in archive
// files whose archive members are not yet loaded).
//
// We put all symbols of all files to a SymbolTable, and the
// SymbolTable selects the "best" symbols if there are name
// conflicts. For example, obviously, a defined symbol is better than
// an undefined symbol. Or, if there's a conflict between a lazy and a
// undefined, it'll read an archive member to read a real definition
// to replace the lazy symbol. The rules are lld/ELF's (Symbol::resolve,
// plan 5.9.5); addSymbol() applies them and checks that one name is not used
// for a function and for data, or for TLS and non-TLS data.
//
// The driver's order (plan 5.9.6): addFile() for every input, extracted
// member and native library, addUndefined() for -u and the entry, then
// bindLateSymbols() and compileBitcodeFiles(). The import-annotation pass
// (plan 5.10.13) reports unresolved references after LTO through
// reportReference().
class SymbolTable {
public:
  // Every symbol in the table, in insertion order.
  ArrayRef<Symbol *> symbols() const { return symVector; }

  // Parses \p file: a lazy file adds lazy symbols, a bitcode file its
  // symbols, and a native library its definitions to ctx.nativeIndex (they
  // bind late). \p symName names the symbol whose lazy definition extracted
  // the file, for diagnostics.
  void addFile(InputFile *file, StringRef symName = {});

  // This function is where all the optimizations of link-time
  // optimization happens. M14's LTO (--lto-emit-asm) writes the ILAsm text of
  // the whole program to -o and adds no files.
  void compileBitcodeFiles();

  // The plan 5.9.6 rules for the symbols that are still undefined after every
  // input is loaded (M15: bitcode inputs and native libraries; managed
  // libraries are M22's step 1):
  // - step 2: an undefined symbol of default visibility, weak or global (a
  //   lazy symbol that only weak references met included), binds to the first
  //   bindable definition of its name in the native libraries, in link order
  //   (NativeFunction or NativeData); --trace-symbol prints "<file>: shared
  //   definition of <sym>". Bitcode symbol tables carry no import names, so
  //   the symbol name is looked up;
  // - an undefined weak symbol that nothing binds stays undefined, for the
  //   backend (null, and a trap stub for calls; step 3);
  // - a definition whose COMDAT copy was discarded and that an input still
  //   refers to is "relocation refers to a symbol in a discarded section";
  // - an undefined hidden (or internal) symbol is "undefined hidden symbol"
  //   and an undefined protected one "undefined protected symbol"; neither
  //   binds late;
  // - any other undefined symbol, function or data, goes to LTO, whose
  //   import-annotation pass binds or reports what the merged module still
  //   refers to (plan 5.9.6 "When it runs", 5.10.13);
  // - an entry symbol (ctx.arg.entry, unless -shared) that is not defined is
  //   "undefined symbol: <entry>" with ">>> referenced by the entry point".
  //   The driver does not report it again.
  // It also records which inputs refer to each symbol, for reportReference(),
  // because LTO takes the inputs' symbol tables.
  void bindLateSymbols();

  // Reports the error \p msg about the symbol \p name, which LTO's merged
  // module refers to, in lld/ELF's form: up to three ">>> referenced by
  // <file>:(<definition>)" lines for the inputs whose symbol tables refer to
  // it, or ">>> referenced by ld-temp.o" for a name that only the merged
  // module refers to (a library function the optimizer introduced; plan
  // 5.9.6, 5.10.14).
  void reportReference(std::string msg, StringRef name);

  // The first input whose symbol table refers to \p name, or "ld-temp.o" (as
  // reportReference() names them).
  std::string getReferencingFile(StringRef name);

  // Returns the symbol of \p name, or null if no input has presented it.
  Symbol *find(StringRef name);

  // Returns the table's entry for \p name, creating a placeholder (which the
  // caller must resolve at once) for a new name.
  Symbol *insert(StringRef name);

  // Resolves \p newSym against the table's entry of its name (plan 5.9.5),
  // after checking the kind and TLS attribute (plan 5.9.14's "symbol type
  // mismatch" and "TLS attribute mismatch"). Returns the entry.
  Symbol *addSymbol(const Symbol &newSym);

  // Adds a reference that no input file makes: -u <sym>, --undefined=<sym>
  // and the entry symbol (lld/ELF's addUnusedUndefined and handleUndefined).
  // The symbol is visible to regular objects, so LTO keeps it. A lazy symbol
  // is extracted, and --why-extract records \p reference ("-u foo",
  // "--entry") as the referencing side.
  Symbol *addUndefined(StringRef name, StringRef reference);

  // Adds the lazy definition of \p name in \p file (an archive member or a
  // --start-lib file).
  Symbol *addLazy(StringRef name, InputFile *file);

  // Returns true if \p name is a COMDAT group that no earlier input presented;
  // that input keeps its copy of the group (plan 5.9.5).
  bool addComdat(StringRef name);

  // Set a flag for --trace-symbol so that we can print out a log message
  // if a new symbol with the same name is inserted into the symbol table.
  void trace(StringRef name);

private:
  // An input file that refers to a symbol, and the symbol's name in that
  // file's IR.
  struct Reference {
    BitcodeFile *file;
    StringRef irName;
  };

  // The inputs that refer to \p sym, in load order.
  ArrayRef<Reference> getReferences(const Symbol *sym);

  void reportUnresolved(std::string msg, ArrayRef<Reference> refs,
                        bool isEntry);

  // The undefined symbols of the bitcode files' symbol tables, by symbol,
  // collected once every input is loaded.
  llvm::DenseMap<const Symbol *, SmallVector<Reference, 1>> references;
  bool haveReferences = false;

  // Maps symbol names to index into the symVector.  -1 means that symbols
  // is to not yet in the vector but it should have tracing enabled if it is
  // ever added.
  llvm::DenseMap<llvm::CachedHashStringRef, int> symMap;
  std::vector<Symbol *> symVector;

  // Comdat groups define "link once" sections. If two comdat groups have the
  // same name, only one of them is linked, and the other is ignored. This set
  // is used to uniquify them.
  llvm::DenseSet<llvm::CachedHashStringRef> comdatGroups;

  // Set once LTO has started, to prevent further bitcode files being
  // included. (lld/wasm's static BitcodeFile::doneLTO: a member here, because
  // the driver creates a new SymbolTable for every link.)
  bool doneLTO = false;

  // For LTO.
  std::unique_ptr<BitcodeCompiler> lto;
};

extern SymbolTable *symtab;
} // namespace lld::msil

#endif
