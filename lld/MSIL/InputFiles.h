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
// The input files of msil-ld: LLVM bitcode, as files, as archive members and
// between --start-lib and --end-lib (plan 5.9.4), and native libraries
// (NativeLibraryFile, plan 5.10.8). M20 adds CIL objects (ObjFile) and M21-M22
// .NET assemblies.
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
    NativeLibraryKind,
  };

  virtual ~InputFile() {}

  // Returns the filename.
  StringRef getName() const { return mb.getBufferIdentifier(); }

  // The contents of the file, named by getName().
  MemoryBufferRef getMemoryBufferRef() const { return mb; }

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

  // The linker options of the file (plan 5.9.4, 5.10.6), one option per
  // element. On Windows triples they are the irsymtab's COFF linker options,
  // tokenized as lld-link tokenizes a .drectve section; on other triples
  // every string of the module's !llvm.linker.options is one option, so that
  // a library name with a space, which clang records unquoted as -l<name>,
  // stays one name (section 12 of 2026-10-22). Called for a loaded file
  // only.
  SmallVector<std::string, 0> getLinkerOptions() const;
};

// A native library (plan 5.10.8): an ELF shared object, an archive whose
// native members the native index reads (an import library, oldnames.lib, an
// archive of machine code, or the native members of an archive of bitcode),
// a native DLL, or the built-in library of --ucrtbase-imports. The driver
// creates one per library it reads, in command-line order, and the symbol
// table adds it to ctx.nativeIndex when it loads it. The native index
// creates one per archive member that defines machine code, as the file of
// its NativeCodeOnly definitions.
class NativeLibraryFile : public InputFile {
public:
  enum Format : uint8_t { ELFShared, Archive, PEDll, Builtin, ArchiveMember };

  NativeLibraryFile(MemoryBufferRef m, Format format)
      : InputFile(NativeLibraryKind, m), format(format) {}
  static bool classof(const InputFile *f) {
    return f->kind() == NativeLibraryKind;
  }

  Format format;
};

// Creates the input file for a bitcode file, an archive member or a file
// between --start-lib and --end-lib. Anything else is not linkable by this
// msil-ld: an archive member that is neither bitcode nor a native member (an
// import member or a native object, which the driver gives to the native
// index) is skipped with the warning "<archive>(<member>): not LLVM bitcode;
// ignored", and any other file is the error "unknown file type: <file>". Both
// return null, as does a bitcode file with module-level inline assembly,
// after D26's error (plan 5.9.14).
InputFile *createObjectFile(MemoryBufferRef mb, StringRef archiveName = "",
                            uint64_t offsetInArchive = 0, bool lazy = false);

// Opens a given file.
std::optional<MemoryBufferRef> readFile(StringRef path);
} // namespace msil

// Returns "foo.o" or "foo.a(bar.o)", or "<internal>" for null.
std::string toString(const msil::InputFile *file);
} // namespace lld

#endif
