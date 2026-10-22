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
// The driver's order (plan 5.9.6): addFile() for every input and extracted
// member, addUndefined() for -u and the entry, then bindLateSymbols() and
// compileBitcodeFiles().
class SymbolTable {
public:
  // Every symbol in the table, in insertion order.
  ArrayRef<Symbol *> symbols() const { return symVector; }

  // Parses \p file: a lazy file adds lazy symbols, any other file its
  // symbols. \p symName names the symbol whose lazy definition extracted the
  // file, for diagnostics.
  void addFile(InputFile *file, StringRef symName = {});

  // This function is where all the optimizations of link-time
  // optimization happens. M14's LTO (--lto-emit-asm) writes the ILAsm text of
  // the whole program to -o and adds no files.
  void compileBitcodeFiles();

  // The plan 5.9.6 rules for the symbols that are still undefined after every
  // input is loaded, in M14 (bitcode only, no native or managed libraries):
  // - an undefined weak symbol stays undefined, for the backend (null, and a
  //   trap stub for calls);
  // - a definition whose COMDAT copy was discarded and that an input still
  //   refers to is "relocation refers to a symbol in a discarded section";
  // - an undefined hidden (or internal) symbol is "undefined hidden symbol"
  //   and an undefined protected one "undefined protected symbol";
  // - undefined data is "undefined symbol", with ">>> referenced by" lines;
  // - an undefined function goes to LTO code generation, which imports it
  //   through the D14 table or reports "undefined symbol" (msil-lto);
  // - an entry symbol (ctx.arg.entry, unless -shared) that is not defined is
  //   "undefined symbol: <entry>" with ">>> referenced by the entry point".
  //   The driver does not report it again.
  void bindLateSymbols();

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
