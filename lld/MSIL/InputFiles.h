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
// The input files of msil-ld (plan 5.9.4): CIL objects (ObjFile, plan 5.8)
// and LLVM bitcode, as files, as archive members and between --start-lib and
// --end-lib, the in-memory CIL objects that LTO writes, native libraries
// (NativeLibraryFile, plan 5.10.8) and native objects given on the command
// line. The driver classifies .NET assemblies (AssemblyFiles.h): reference
// assemblies join the reference set (plan 5.11), and libraries built by this
// toolchain go to createManagedLibraryFile(), which makes a
// ManagedLibraryFile (plan 5.9.15).
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_INPUT_FILES_H
#define LLD_MSIL_INPUT_FILES_H

#include "Symbols.h"
#include "TokenMap.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
class TarWriter;
namespace cli {
class MetadataReader;
} // namespace cli
} // namespace llvm

namespace lld {
namespace msil {
class InputMethod;
class InputSegment;

// If --reproduce option is given, all input files are written
// to this tar archive.
extern std::unique_ptr<llvm::TarWriter> tar;

class InputFile {
public:
  enum Kind {
    ObjectKind,
    BitcodeKind,
    NativeLibraryKind,
    // A library built by this toolchain (AssemblyFiles.h, plan 5.9.15).
    ManagedLibraryKind,
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

// A CIL object (plan 5.8): an ECMA-335 netmodule (a PE with a CLI header and
// no Assembly row) whose Module row carries the link-info record. The
// constructor reads the image and the metadata (cli::PEImage,
// cli::MetadataReader) and decodes the record (cli::findLinkInfo,
// cli::decodeLinkInfo); parse() validates it against the metadata
// (cli::validateLinkInfo, C1-C7), decodes the method bodies, makes the
// methods and segments and adds the symbols. The texts are plan 5.8.5's and
// 5.9.14's:
// - for a PE with an Assembly row, which the driver never passes (it
//   classifies .NET assemblies first, plan 5.9.4): the warning
//   "<archive>(<member>): .NET assemblies in archives are not linked;
//   ignored" for an archive member, else "<file>: a .NET assembly is not a
//   CIL object";
// - "<file>: not a CIL object (no link-info record)";
// - "<file>: " and the reader's or the decoder's message
//   ("invalid CLI image: ...", "invalid link-info record: ...");
// - "<file>: malformed CIL object: C<n>: ..." for a validation failure;
// - "<file>: malformed method body in '<m>' at IL offset <n>" (the walker).
// LTO's in-memory objects are ObjFiles too (isLTO).
class ObjFile : public InputFile {
public:
  ObjFile(MemoryBufferRef m, StringRef archiveName, bool lazy);
  ~ObjFile();
  static bool classof(const InputFile *f) { return f->kind() == ObjectKind; }

  // False if the constructor reported an error; createObjectFile() then
  // drops the file.
  bool isValid() const { return valid; }

  // Adds the object's symbols to the symbol table (plan 5.9.5): COMDAT
  // groups first (the first object that presents a group keeps it; the
  // members of a discarded copy are discarded and the symbols they define
  // enter as undefined symbols marked "discarded"), then the defined, common
  // and alias symbols, then the undefined ones. LOCAL symbols never enter the
  // table: they bind within the object. getSymbols() then holds one entry per
  // SYMTAB symbol, in SYMTAB order. Also decodes every method body (the
  // token walker) and builds the token map. Returns early after an error.
  // \p ignoreComdats keeps every COMDAT group without claiming it in the
  // symbol table: LTO objects are parsed so, because their BitcodeFiles
  // already claimed the groups and the IR linker picked the prevailing copies
  // (lld/wasm's ObjFile::parse(true), lld/ELF's shouldIgnoreComdats).
  void parse(bool ignoreComdats = false);
  // Adds every non-LOCAL, non-UNDEFINED SYMTAB symbol (WEAK, COMMON and ALIAS
  // included) to the symbol table as a lazy symbol (plan 5.9.4).
  void parseLazy();

  const llvm::MSILLink::Record &getRecord() const { return record; }
  const llvm::cli::MetadataReader &getReader() const { return *reader; }

  // The methods with a body, in MethodDef row order.
  ArrayRef<InputMethod *> methods() const { return methodList; }
  // The record's segments, in SEGMENTS order.
  ArrayRef<InputSegment *> segments() const { return segmentList; }
  const TokenMap &getTokenMap() const { return tokenMap; }

  // The symbol of SYMTAB index \p index: the symbol table's entry, or the
  // object's local symbol (null before parse()).
  Symbol *getSymbol(uint32_t index) const {
    return index < symbols.size() ? symbols[index] : nullptr;
  }
  // The SYMTAB index of \p name in this object, or -1.
  int32_t findSymbolIndex(StringRef name) const;
  // The method of a MethodDef token of this object, or null for a stub.
  InputMethod *getMethod(uint32_t methodDefToken) const;
  // The SIGNATURES entry of \p token (a MethodDef or a method MemberRef), or
  // null.
  const llvm::MSILLink::SignatureInfo *getSignatureInfo(uint32_t token) const;
  // True if this object kept COMDAT group \p index (plan 5.9.5). An INIT
  // entry whose group was not kept is dropped with it.
  bool isComdatKept(uint32_t index) const { return keptComdats[index]; }

  // The import attributes of an undefined symbol (plan 5.10.7): for a
  // FUNCTION, from its import stubs' ImplMap rows (module: the ModuleRef
  // verbatim, or empty for msil.unresolved; name: the import name); for DATA,
  // the record's IMPORT fields. std::nullopt for a symbol without them.
  struct ImportAttributes {
    StringRef module;
    StringRef name;
  };
  std::optional<ImportAttributes> getImport(uint32_t symbolIndex) const;

  // The object's linker requests (plan 5.9.4): the DEPENDENT_LIB entries,
  // and the options of the LINKER_OPTION entries, one option per element. On
  // Windows triples each string is tokenized as lld-link tokenizes a
  // .drectve section (clang quotes a library name with a space there); on
  // other triples each string is one option.
  SmallVector<StringRef, 0> getDependentLibraries() const;
  SmallVector<std::string, 0> getLinkerOptions() const;

  // True for an object that LTO wrote (plan 5.9.13).
  bool isLTO = false;

private:
  bool valid = false;
  bool parsed = false;
  std::unique_ptr<llvm::cli::MetadataReader> reader;
  llvm::MSILLink::Record record;
  SmallVector<InputMethod *, 0> methodList;
  SmallVector<InputSegment *, 0> segmentList;
  // MethodDef row - 1 -> method (null for a stub).
  std::vector<InputMethod *> methodByRow;
  std::vector<bool> keptComdats;
  llvm::DenseMap<uint32_t, const llvm::MSILLink::SignatureInfo *> sigInfos;
  llvm::DenseMap<uint32_t, ImportAttributes> imports;
  llvm::DenseMap<llvm::CachedHashStringRef, uint32_t> symbolIndexes;
  TokenMap tokenMap;

  bool readMethods();
  void readSegments();
  void readComdats(std::vector<bool> &discardedSymbols, bool ignoreComdats);
  void readImports();
  Symbol *createSymbol(uint32_t index, bool discarded);
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
// creates one per archive member that defines machine code (ArchiveMember),
// as the file of its NativeCodeOnly definitions. createObjectFile() makes one
// of format NativeObject for a native COFF or ELF object given on the command
// line (section 12 of 2026-10-26, Rust-found defect 3), which the native
// index reads as an archive member: its global definitions are machine code,
// which a reference cannot bind and which comes before every library's
// definition, and an object that nothing refers to is skipped.
class NativeLibraryFile : public InputFile {
public:
  enum Format : uint8_t {
    ELFShared,
    Archive,
    PEDll,
    Builtin,
    ArchiveMember,
    NativeObject
  };

  NativeLibraryFile(MemoryBufferRef m, Format format)
      : InputFile(NativeLibraryKind, m), format(format) {}
  static bool classof(const InputFile *f) {
    return f->kind() == NativeLibraryKind;
  }

  Format format;
};

// Creates the input file for a CIL object or bitcode file, an archive member
// or a file between --start-lib and --end-lib, or one of LTO's in-memory
// objects. A native COFF or ELF object given on the command line (not an
// archive member, which the driver gives to the native index) becomes a
// NativeLibraryFile of format NativeObject, noted under --verbose: its
// definitions are machine code that only a reference makes an error
// (NativeIndex::addFile). Anything else is not linkable by this
// msil-ld: an archive member that is none of these nor a native member (an
// import member or a native object) is skipped with the warning
// "<archive>(<member>): not LLVM bitcode; ignored", a file of a format that
// llvm::identify_magic recognizes is the error "unknown file type: <file>
// (<format>)" (a WebAssembly object, a Mach-O file, ...), and any other file
// is the error "unknown file type: <file>". These return null, as do a
// bitcode file with module-level inline assembly, after D26's error (plan
// 5.9.14), and a PE with a CLI header that is no valid CIL object, after
// ObjFile's error or, for a .NET assembly in an archive, its warning.
InputFile *createObjectFile(MemoryBufferRef mb, StringRef archiveName = "",
                            uint64_t offsetInArchive = 0, bool lazy = false);

// Returns true if \p mb is a PE image with a CLI header (ECMA-335
// II.25.3.3): a CIL object or a .NET assembly. Any other PE image is a native
// DLL.
bool isCLIImage(MemoryBufferRef mb);

// Opens a given file.
std::optional<MemoryBufferRef> readFile(StringRef path);
} // namespace msil

// Returns "foo.o" or "foo.a(bar.o)", or "<internal>" for null.
std::string toString(const msil::InputFile *file);
} // namespace lld

#endif
