//===- InputFiles.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/InputFiles.h at 64f50ffd4f1c.
//
// The input files of msil-ld. M14 reads LLVM bitcode, as files, as archive
// members and between --start-lib and --end-lib (plan 5.9.4); M20 adds CIL
// objects (ObjFile), M15 native libraries and M21-M22 .NET assemblies.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_INPUT_FILES_H
#define LLD_MSIL_INPUT_FILES_H

#include "Symbols.h"
#include "lld/Common/LLVM.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
class TarWriter;
}

namespace lld {
namespace msil {
// If --reproduce option is given, all input files are written
// to this tar archive.
extern std::unique_ptr<llvm::TarWriter> tar;

class InputFile {
public:
  enum Kind {
    BitcodeKind,
  };

  virtual ~InputFile() {}

  // Returns the filename.
  StringRef getName() const { return mb.getBufferIdentifier(); }

  Kind kind() const { return fileKind; }

  // An archive file name if this file is created from an archive.
  std::string archiveName;

  ArrayRef<Symbol *> getSymbols() const { return symbols; }

  MutableArrayRef<Symbol *> getMutableSymbols() { return symbols; }

  // True if this is a relocatable object file/bitcode file in an ar archive
  // or between --start-lib and --end-lib.
  bool lazy = false;

protected:
  InputFile(Kind k, MemoryBufferRef m) : mb(m), fileKind(k) {}

  MemoryBufferRef mb;

  // List of all symbols referenced or defined by this file.
  std::vector<Symbol *> symbols;

private:
  const Kind fileKind;
};

// .bc file
class BitcodeFile : public InputFile {
public:
  BitcodeFile(MemoryBufferRef m, StringRef archiveName,
              uint64_t offsetInArchive, bool lazy);
  static bool classof(const InputFile *f) { return f->kind() == BitcodeKind; }

  // Adds the file's symbols to the symbol table. getSymbols() then holds one
  // entry per symbol of obj->symbols(), in the same order.
  void parse();
  // Adds the file's definitions to the symbol table as lazy symbols.
  void parseLazy();

  std::unique_ptr<llvm::lto::InputFile> obj;

  // For each entry of obj->getComdatTable(): true if this file's copy of the
  // group is kept (plan 5.9.5).
  std::vector<bool> keptComdats;
};

// Creates the input file for a bitcode file, an archive member or a file
// between --start-lib and --end-lib. Anything else is not linkable by this
// msil-ld: an archive member that is not bitcode is skipped with the warning
// "<archive>(<member>): not LLVM bitcode; ignored", and any other file is the
// error "unknown file type: <file>". Both return null, as does a bitcode file
// with module-level inline assembly, after D26's error (plan 5.9.14).
InputFile *createObjectFile(MemoryBufferRef mb, StringRef archiveName = "",
                            uint64_t offsetInArchive = 0, bool lazy = false);

// Opens a given file.
std::optional<MemoryBufferRef> readFile(StringRef path);
} // namespace msil

// Returns "foo.o" or "foo.a(bar.o)", or "<internal>" for null.
std::string toString(const msil::InputFile *file);
} // namespace lld

#endif
