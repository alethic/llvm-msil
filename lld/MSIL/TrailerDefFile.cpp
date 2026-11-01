//===- TrailerDefFile.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The lazy input '<trailer>' (see TrailerDefFile.h).
//
//===----------------------------------------------------------------------===//

#include "TrailerDefFile.h"
#include "Config.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CLI/TrailerDefs.h"

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::msil;

TrailerDefFile::TrailerDefFile()
    : InputFile(TrailerDefKind, MemoryBufferRef("", "<trailer>")) {
  lazy = true;
}

void TrailerDefFile::parseLazy() {
  for (const cli::TrailerDefInfo &row : cli::getTrailerDefs())
    if (findOfferedTrailerDef(row.Name) == &row)
      symbols.push_back(symtab->addLazy(row.Name, this));
}

void TrailerDefFile::extract(StringRef name) {
  if (!loaded) {
    loaded = true;
    if (ctx.arg.trace)
      message(toString(this));
    ctx.loadedFiles.push_back(this);
  }
  // A global function of default visibility: the method is 'assembly'
  // whatever the symbol's visibility, and nothing exports it, while a
  // hidden definition would make the name hidden for a definition of an
  // object that replaces it (visibility merges).
  symtab->addSymbol(DefinedFunction(this, name, STB_GLOBAL, STV_DEFAULT));
}

const cli::TrailerDefInfo *lld::msil::findOfferedTrailerDef(StringRef name) {
  const cli::TrailerDefInfo *row = cli::findTrailerDef(name);
  if (row && cli::isOfferedIn(*row, ctx.arg.target,
                              /*IsExecutable=*/!ctx.arg.shared))
    return row;
  return nullptr;
}

const cli::TrailerDefInfo *lld::msil::getTrailerDefinition(const Symbol *sym) {
  if (!isa_and_nonnull<DefinedFunction>(sym) ||
      !isa_and_nonnull<TrailerDefFile>(sym->getFile()))
    return nullptr;
  // A name that /alternatename resolved to a trailer definition is a copy of
  // it (SymbolTable::resolveAlternateNames): follow the applied names.
  StringRef name = sym->getName();
  for (size_t i = 0, e = ctx.appliedAlternateNames.size(); i <= e; ++i) {
    if (const cli::TrailerDefInfo *row = cli::findTrailerDef(name))
      return row;
    auto it = ctx.appliedAlternateNames.find(name);
    if (it == ctx.appliedAlternateNames.end())
      break;
    name = it->second;
  }
  return nullptr;
}
