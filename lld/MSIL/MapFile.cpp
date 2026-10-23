//===- MapFile.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/MapFile.cpp at 64f50ffd4f1c.
//
// This file implements the -Map option (plan 5.9.14). It shows the inputs in
// load order, native libraries included, and the symbols they define, with
// the size and alignment of common symbols, then the symbols that late binding
// bound to native libraries (the library as input):
//
//   msil-ld map of prog.il (x86_64-pc-windows-msvc-msil)
//   Inputs:
//     main.o
//     ucrt.lib
//     libhelp.a(h1.o)
//   Symbols:
//     Kind            Binding Input           Symbol
//     DefinedFunction Global  main.o          main
//     CommonSymbol    Global  main.o          buf (size 48, align 16)
//     DefinedFunction Global  libhelp.a(h1.o) helper
//     NativeFunction  Global  ucrt.lib        puts
//
// M20 adds the sections Image:, Methods:, Fields: and Imports:, which show
// the tokens and offsets of the PE.
//
//===----------------------------------------------------------------------===//

#include "MapFile.h"
#include "Config.h"
#include "InputFiles.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace llvm;
using namespace lld;
using namespace lld::msil;

// Returns a list of all symbols that we want to print out: the definitions
// and common symbols that won resolution, by input in load order and, within
// an input, in the order of its symbol table; then the symbols bound to
// native libraries, by library in load order and, within a library, in the
// order of the symbol table.
static std::vector<Symbol *> getSymbols() {
  std::vector<Symbol *> v;
  DenseSet<const Symbol *> seen;
  for (BitcodeFile *file : ctx.bitcodeFiles)
    for (Symbol *sym : file->getSymbols())
      if (sym && (sym->isDefined() || sym->isCommon()) &&
          sym->getFile() == file && seen.insert(sym).second)
        v.push_back(sym);

  DenseMap<const InputFile *, std::vector<Symbol *>> natives;
  for (Symbol *sym : symtab->symbols())
    if (sym->isNative())
      natives[sym->getFile()].push_back(sym);
  for (NativeLibraryFile *file : ctx.nativeLibraryFiles) {
    auto it = natives.find(file);
    if (it != natives.end())
      append_range(v, it->second);
  }
  return v;
}

namespace {
// One row of the Symbols: section.
struct SymbolRow {
  std::string kind;
  StringRef binding;
  std::string input;
  std::string name;
};
} // namespace

static void writeMap(raw_ostream &os) {
  // Print out the header line.
  os << "msil-ld map of " << ctx.arg.outputFile << " (" << ctx.arg.target.str()
     << ")\n";

  os << "Inputs:\n";
  for (InputFile *file : ctx.loadedFiles)
    os << "  " << toString(file) << '\n';

  SmallVector<SymbolRow, 0> rows;
  rows.push_back({"Kind", "Binding", "Input", "Symbol"});
  for (Symbol *sym : getSymbols()) {
    std::string name = toString(*sym);
    if (auto *c = dyn_cast<CommonSymbol>(sym))
      name += " (size " + std::to_string(c->getSize()) + ", align " +
              std::to_string(c->getAlignment()) + ")";
    rows.push_back({toString(sym->kind()), sym->isWeak() ? "Weak" : "Global",
                    toString(sym->getFile()), std::move(name)});
  }

  size_t kindWidth = 0, bindingWidth = 0, inputWidth = 0;
  for (const SymbolRow &row : rows) {
    kindWidth = std::max(kindWidth, row.kind.size());
    bindingWidth = std::max(bindingWidth, row.binding.size());
    inputWidth = std::max(inputWidth, row.input.size());
  }

  os << "Symbols:\n";
  for (const SymbolRow &row : rows)
    os << "  " << left_justify(row.kind, kindWidth) << ' '
       << left_justify(row.binding, bindingWidth) << ' '
       << left_justify(row.input, inputWidth) << ' ' << row.name << '\n';
}

void lld::msil::writeMapFile() {
  if (ctx.arg.mapFile.empty())
    return;

  // LLD_IN_TEST runs the link more than once and prints only in the last run
  // (lld/ELF's Ctx::openAuxiliaryFile).
  if (errorHandler().disableOutput && ctx.arg.mapFile == "-") {
    raw_null_ostream os;
    writeMap(os);
    return;
  }

  // Open a map file for writing.
  std::error_code ec;
  raw_fd_ostream os(ctx.arg.mapFile, ec, sys::fs::OF_None);
  if (ec) {
    error("cannot open " + ctx.arg.mapFile + ": " + ec.message());
    return;
  }
  writeMap(os);
}
