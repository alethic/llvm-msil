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
// From M20 the inputs include CIL objects, the symbols the definitions of CIL
// objects and LTO objects and the linker-defined __start_X/__stop_X, and a
// map of a PE adds the sections Image:, Methods:, Fields: and Imports:, which
// show the offsets and tokens that exist only in the PE (writeResult() fills
// outputMap):
//
//   Image: 32 bytes (20 initialized), align 16
//     Offset     Size Align Section / input / symbol
//          0        6     1 .rodata
//          0        6     1   a.o:segment 0
//          0        6     1     msg
//   Methods:
//     Token      Owner      Input          Symbol
//     0x06000001 <Module>   a.o            main
//   Fields:
//     Token      Owner      Offset   Symbol
//     0x04000001 <Module>        0   msg
//   Imports:
//     Token      Module!Entry                              Symbol
//     0x06000004 api-ms-win-crt-stdio-l1-1-0.dll!puts      puts
//
// From M22 Symbols: also lists the symbols that late binding bound to the
// exports of .NET libraries built by this toolchain (ManagedFunction,
// ManagedData, the library as input), after the natively bound ones.
//
//===----------------------------------------------------------------------===//

#include "MapFile.h"
#include "AssemblyFiles.h"
#include "Config.h"
#include "InputFiles.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace llvm;
using namespace lld;
using namespace lld::msil;

OutputMap lld::msil::outputMap;

// Returns a list of all symbols that we want to print out: the definitions
// and common symbols that won resolution, by input in load order (CIL objects
// and bitcode files, then the objects that LTO wrote) and, within an input,
// in the order of its symbol table; then the symbols bound to native
// libraries, by library in load order and, within a library, in the order of
// the symbol table; then the symbols bound to the exports of .NET libraries
// (late binding step 1, plan 5.9.6), by library in link order
// (SymbolTable::getManagedLibraries()) and, within a library, in the order of
// the symbol table; then __start_X and __stop_X.
static std::vector<Symbol *> getSymbols() {
  std::vector<Symbol *> v;
  DenseSet<const Symbol *> seen;
  auto addFile = [&](InputFile *file) {
    for (Symbol *sym : file->getSymbols())
      if (sym && !sym->isLocal() && (sym->isDefined() || sym->isCommon()) &&
          sym->getFile() == file && seen.insert(sym).second)
        v.push_back(sym);
  };
  for (InputFile *file : ctx.loadedFiles)
    if (isa<BitcodeFile>(file) || isa<ObjFile>(file))
      addFile(file);
  for (ObjFile *file : ctx.objectFiles)
    if (file->isLTO)
      addFile(file);

  DenseMap<const InputFile *, std::vector<Symbol *>> natives, managed;
  for (Symbol *sym : symtab->symbols()) {
    if (sym->isNative())
      natives[sym->getFile()].push_back(sym);
    else if (sym->isManaged())
      managed[sym->getFile()].push_back(sym);
  }
  for (NativeLibraryFile *file : ctx.nativeLibraryFiles) {
    auto it = natives.find(file);
    if (it != natives.end())
      append_range(v, it->second);
  }
  for (ManagedLibraryFile *file : symtab->getManagedLibraries()) {
    auto it = managed.find(file);
    if (it != managed.end())
      append_range(v, it->second);
  }
  for (SyntheticData *sym : ctx.sym.startStop)
    if (seen.insert(sym).second)
      v.push_back(sym);
  return v;
}

// Pads \p s on the left to \p width columns.
static std::string rightJustify(StringRef s, size_t width) {
  return std::string(width > s.size() ? width - s.size() : 0, ' ') + s.str();
}

static std::string toHex(uint32_t token) {
  return "0x" + utohexstr(token, /*LowerCase=*/false, /*Width=*/8);
}

// The sections of a PE map (plan 5.9.14's example): the columns have the
// example's minimum widths and grow with their contents.
static void writePESections(raw_ostream &os) {
  const OutputMap &map = outputMap;
  os << "Image: " << map.totalSize << " bytes (" << map.initSize
     << " initialized), align " << map.align << '\n';
  os << rightJustify("Offset", 8) << ' ' << rightJustify("Size", 8) << ' '
     << rightJustify("Align", 5) << " Section / input / symbol\n";
  for (const OutputMap::ImageRow &row : map.image)
    os << rightJustify(std::to_string(row.offset), 8) << ' '
       << rightJustify(std::to_string(row.size), 8) << ' '
       << rightJustify(std::to_string(row.align), 5) << ' '
       << std::string(2 * row.depth, ' ') << row.name << '\n';

  size_t ownerWidth = 10, inputWidth = 14;
  for (const OutputMap::MethodRow &row : map.methods) {
    ownerWidth = std::max(ownerWidth, row.owner.size());
    inputWidth = std::max(inputWidth, row.input.size());
  }
  os << "Methods:\n";
  os << "  " << left_justify("Token", 10) << ' '
     << left_justify("Owner", ownerWidth) << ' '
     << left_justify("Input", inputWidth) << " Symbol\n";
  for (const OutputMap::MethodRow &row : map.methods)
    os << "  " << toHex(row.token) << ' ' << left_justify(row.owner, ownerWidth)
       << ' ' << left_justify(row.input, inputWidth) << ' ' << row.symbol
       << '\n';

  size_t fieldOwnerWidth = 10, offsetWidth = 6;
  for (const OutputMap::FieldRow &row : map.fields) {
    fieldOwnerWidth = std::max(fieldOwnerWidth, row.owner.size());
    if (row.offset)
      offsetWidth = std::max(offsetWidth, std::to_string(*row.offset).size());
  }
  os << "Fields:\n";
  os << "  " << left_justify("Token", 10) << ' '
     << left_justify("Owner", fieldOwnerWidth) << ' '
     << rightJustify("Offset", offsetWidth) << "   Symbol\n";
  for (const OutputMap::FieldRow &row : map.fields)
    os << "  " << toHex(row.token) << ' '
       << left_justify(row.owner, fieldOwnerWidth) << ' '
       << rightJustify(row.offset ? std::to_string(*row.offset) : "-",
                       offsetWidth)
       << "   " << row.symbol << '\n';

  size_t entryWidth = 41;
  for (const OutputMap::ImportRow &row : map.imports)
    entryWidth = std::max(entryWidth, row.moduleEntry.size());
  os << "Imports:\n";
  os << "  " << left_justify("Token", 10) << ' '
     << left_justify("Module!Entry", entryWidth) << " Symbol\n";
  for (const OutputMap::ImportRow &row : map.imports)
    os << "  " << toHex(row.token) << ' '
       << left_justify(row.moduleEntry, entryWidth) << ' ' << row.symbol
       << '\n';
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

  // A PE has the sections of M20; the text of --lto-emit-asm does not.
  if (!ctx.arg.ltoEmitAsm && outputMap.valid)
    writePESections(os);
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
