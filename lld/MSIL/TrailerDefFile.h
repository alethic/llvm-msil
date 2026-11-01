//===- TrailerDefFile.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The lazy input '<trailer>' of msil-ld (llvm/docs/MSIL/ImplementationPlan.md
// 5.14.4 T5): the trailer definitions of llvm/CLI/TrailerDefs.def, such as
// __msil_compiler_barrier, C-named functions whose IL the trailer builder
// writes (cli::buildTrailer).
//
// The driver adds it once, after the inputs of the command line and the
// libraries that they request, so that it is searched like an archive after
// every real archive and before the native libraries, which bind late: a
// name that an object or an archive member defines never extracts it, a
// reference that no input defines extracts its row as a reference extracts
// an archive member (a weak one does not), and so do the references that the
// LTO objects make after LTO (helpers-switches E5). It offers a row only
// when the row's scope holds for the output (cli::isOfferedIn; an output is
// executable unless it is -shared). A definition that it gives is a
// DefinedFunction without a method whose file is '<trailer>', the name that
// the map file, --trace, --trace-symbol and --why-extract show; a definition
// of an object or an archive member that is loaded later still replaces it
// (Symbol::resolve).
//
// The writer declares the method of each definition that the live code uses
// before it binds the calls (RowTrailerSink::declareHelper with the
// registry's signature) and lists it in cli::TrailerInput::Defs. The method
// is an 'assembly' member of '<Module>': no other assembly binds it, and a
// -shared library never exports it (Exports.cpp exports objects'
// definitions only).
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_TRAILER_DEF_FILE_H
#define LLD_MSIL_TRAILER_DEF_FILE_H

#include "InputFiles.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/StringRef.h"

namespace llvm::cli {
struct TrailerDefInfo;
} // namespace llvm::cli

namespace lld::msil {
class Symbol;

class TrailerDefFile : public InputFile {
public:
  TrailerDefFile();
  static bool classof(const InputFile *f) {
    return f->kind() == TrailerDefKind;
  }

  // Adds a lazy symbol for each row that the output is offered
  // (findOfferedTrailerDef), in row order. SymbolTable::addFile calls it
  // when the driver adds the file.
  void parseLazy();

  // Defines \p name, a row that a reference extracted (SymbolTable::addFile
  // with the name of the lazy symbol). The first extraction makes the file
  // a loaded input (ctx.loadedFiles, --trace). The file stays lazy, so that
  // every row is extracted on its own.
  void extract(StringRef name);

private:
  bool loaded = false;
};

// The row of TrailerDefs.def named \p name if the trailer defines it in this
// output (cli::isOfferedIn for the codegen triple; executable unless
// -shared), else null.
const llvm::cli::TrailerDefInfo *findOfferedTrailerDef(StringRef name);

// The row of \p sym if it is a trailer definition, a definition that
// '<trailer>' gives, or a name that /alternatename resolved to one; null for
// any other symbol.
const llvm::cli::TrailerDefInfo *getTrailerDefinition(const Symbol *sym);
} // namespace lld::msil

#endif
