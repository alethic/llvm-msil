//===- AssemblyFiles.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The .NET assemblies that msil-ld reads (llvm/docs/MSIL/ImplementationPlan.md
// 5.9.2, 5.9.4): reference assemblies (ReferenceAssemblyFile, the members of
// a ReferenceSet, plan 5.11), and libraries built by this toolchain
// (ManagedLibraryFile), which carry its symbol table in an Assembly-row
// attribute (plan 5.9.15) and reach the link only through
// createManagedLibraryFile().
//
// A library's exports bind late, before the native libraries (plan 5.9.6
// step 1): SymbolTable::bindLateSymbols() makes ManagedFunction and
// ManagedData symbols of them, and the writer reaches them through rows that
// this file makes in the output: one AssemblyRef per library (its name,
// version and culture, Flags 0, no key or token), a TypeRef per export
// class, and MemberRefs on it for the exported methods (with the library's
// signatures) and the '<msil>addr_<name>' accessors (exports-multiasm 10.2).
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_ASSEMBLY_FILES_H
#define LLD_MSIL_ASSEMBLY_FILES_H

#include "InputFiles.h"
#include "ReferenceSet.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CLI/SymbolTable.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace llvm::cli {
class MetadataBuilder;
class MetadataReader;
} // namespace llvm::cli

namespace lld::msil {
class BCLBinder;
class ManagedLibraryFile;

// One reference assembly (plan 5.9.2, 5.11.2): a mapped file, its
// cli::MetadataReader and its identity. The reader is lazy, so a file costs
// its headers until the binder reads its rows.
class ReferenceAssemblyFile {
public:
  // The file of \p mb, a PE image with a CLI header and an Assembly row,
  // which the caller keeps alive (readFile()'s buffers live for the link),
  // and \p reader, the reader of \p mb that the caller made. Errors are
  // "<path>: " and the reader's message, or "<path>: not a .NET assembly"
  // for an image without an Assembly row (paths as getDisplayPath() prints
  // them).
  static Expected<std::unique_ptr<ReferenceAssemblyFile>>
  create(MemoryBufferRef mb, std::unique_ptr<llvm::cli::MetadataReader> reader);
  ~ReferenceAssemblyFile();

  // The path the file was read from (its buffer identifier).
  StringRef getPath() const { return mb.getBufferIdentifier(); }
  // The bytes of the file (ReferenceSet compares the files of one identity).
  MemoryBufferRef getMemoryBufferRef() const { return mb; }
  const AssemblyIdentity &getIdentity() const { return identity; }
  // The token of the Assembly row's public key; std::nullopt without a key.
  const std::optional<std::array<uint8_t, 8>> &getPublicKeyToken() const {
    return identity.PublicKeyToken;
  }
  const llvm::cli::MetadataReader &getReader() const { return *reader; }
  // True if the Assembly row carries
  // [System.Runtime.CompilerServices.ReferenceAssemblyAttribute]. The packs'
  // contract assemblies do; implementation assemblies (the shared framework,
  // System.Private.*) and the Framework's own mscorlib do not (bcl-binding
  // 2.1). Read on first use.
  bool isReferenceAssembly() const;

private:
  ReferenceAssemblyFile(MemoryBufferRef mb,
                        std::unique_ptr<llvm::cli::MetadataReader> reader);

  MemoryBufferRef mb;
  std::unique_ptr<llvm::cli::MetadataReader> reader;
  AssemblyIdentity identity;
  mutable std::optional<bool> referenceAssembly;
};

// True if a custom attribute of \p parent (a token of a HasCustomAttribute
// table, makeToken(TableId::Assembly, 1) for the Assembly row) has a
// constructor whose declaring type is \p typeName, a full name as
// cli::MetadataReader::getTypeFullName spells it
// ("System.ObsoleteAttribute"). Metadata that cannot be read counts as no
// such attribute.
bool hasCustomAttribute(const llvm::cli::MetadataReader &reader,
                        uint32_t parent, StringRef typeName);

// True if the Assembly row of \p reader carries the symbol table of a
// library built by this toolchain (plan 5.9.15): a custom attribute whose
// constructor takes uint8[] and belongs to the class '<msil>SymbolTable'
// (cli::SymbolTableType, no namespace). False for an image without an
// Assembly row and for metadata that cannot be read.
bool isToolchainLibrary(const llvm::cli::MetadataReader &reader);

// What a PE image with a CLI header is (plan 5.9.4): a CIL object or C#
// netmodule (no Assembly row; ObjFile reads and checks it), a reference
// assembly (an Assembly row, no toolchain symbol table), or a library built
// by this toolchain. An image whose metadata cannot be read is an Object, so
// that ObjFile reports the problem.
enum class CLIImageKind : uint8_t {
  Object,
  ReferenceAssembly,
  ToolchainLibrary
};
CLIImageKind classifyCLIImage(MemoryBufferRef mb);

// One export of a library built by this toolchain (plan 5.9.15), as the
// library's symbol table and its export classes describe it.
struct ManagedExport {
  enum Kind : uint8_t { Function, Data };
  Kind K = Function;
  // The symbol name: the name of the exported method, or of the variable's
  // property.
  StringRef Name;
  // The library, and the index of the export class in its symbol table's
  // ExportClasses.
  ManagedLibraryFile *File = nullptr;
  uint32_t Class = 0;
  // Function: the MethodDef token of the exported method in the library; 0
  // for data.
  uint32_t MethodDef = 0;
  // The MethodDef token of the public static 'native int
  // <msil>addr_<name>()' accessor of the export class; for data, the getter
  // of the variable's property.
  uint32_t Accessor = 0;
  // Function: the method's signature blob in the library, and the decoded
  // signature, whose TypeRef values are tokens of the library
  // (ManagedLibraryFile::getOutputSignature() maps them).
  ArrayRef<uint8_t> SignatureBlob;
  llvm::cli::MethodSig Signature;
  // The symbol table's flags: cli::LibraryFunctionFlags (Variadic, Weak,
  // ABI) for a function, 0 for a function that the table does not list;
  // cli::LibraryDataSymbol flags (Const, ThreadLocal, Weak) for data.
  uint8_t Flags = 0;
  // Function with flag ABI: the definition's SIGNATURES descriptor (plan
  // 5.8.8; Token 0).
  std::optional<llvm::MSILLink::SignatureInfo> ABI;
  // Data: the size and log2 alignment that the table gives.
  uint64_t Size = 0;
  uint8_t AlignLog2 = 0;

  bool isData() const { return K == Data; }
  // The descriptor for cli::CallShape::get(), or null (a prototyped
  // definition without sret or byval parameters).
  const llvm::MSILLink::SignatureInfo *getSignatureInfo() const {
    return ABI ? &*ABI : nullptr;
  }
};

// A library built by this toolchain (plan 5.9.15): a PE image with a CLI
// header and an Assembly row that carries the toolchain's symbol table
// ('<msil>SymbolTable', isToolchainLibrary). The driver reads it, positional
// or found by -l, and hands it to createManagedLibraryFile();
// SymbolTable::addFile keeps the libraries in command-line order and parses
// each one. Every text names the file as toString(InputFile *) does.
class ManagedLibraryFile : public InputFile {
public:
  // The file of \p mb, whose buffer the driver keeps alive for the link, and
  // \p reader, the metadata of \p mb.
  ManagedLibraryFile(MemoryBufferRef mb,
                     std::unique_ptr<llvm::cli::MetadataReader> reader);
  ~ManagedLibraryFile();
  static bool classof(const InputFile *f) {
    return f->kind() == ManagedLibraryKind;
  }

  // Reads the identity (the Assembly row) and decodes the symbol table
  // (cli::decodeSymbolTable): of several '<msil>SymbolTable' payloads, the
  // first whose triple has the codegen triple's key, else the first. A
  // table that does not decode is "<file>: invalid symbol table: ...", which
  // covers the table version (another major version is refused, another
  // minor one is read). An interface library (flag INTERFACE) gives the
  // --verbose note "<file> is an interface library; deploy the library built
  // from the same objects". Returns false after an error (reported).
  // SymbolTable::addFile calls it once.
  bool parse();

  // The checks of plan 5.9.15 against the link, in order, once every input is
  // loaded (exports-multiasm 5.4 steps 3-5; checkManagedLibraries() calls
  // it): the toolchain ABI version, the triple key and the data model with
  // 5.9.14's texts ("<file>: toolchain ABI version <a> differs from <a0> in
  // <f0>", "<file>: target triple <t> is incompatible with <t0>", "<file>:
  // data model <m> differs from <m0> in <f0>"), then the C runtime ("<file>:
  // C runtime '<c>' differs from '<c0>' of this link"; none on either side
  // matches anything), each against \p model (the first CIL object, else the
  // first bitcode file, whose triple gives only the pointer and long sizes
  // and whose ABI version is this toolchain's; null without either). The
  // first failing check is the only one reported. Then the symbol map: the
  // table lists an export class ("no export class" otherwise), each export
  // class must be a public top-level TypeDef, each of its public
  // static methods outside '<msil>' is a function with a public static
  // 'native int <msil>addr_<name>()', each data symbol has a property whose
  // getter is such an accessor named after it, and each function the table
  // flags is such a method; a violation is "<file>: malformed export table:
  // <what>". Last, the library's own AssemblyRefs that name no library of
  // the link (\p libraries) are compared with the references, or without
  // references with the built-in table's mscorlib 4.0.0.0 (bcl-binding 5.9,
  // plan 5.11.9): a newer version is the error "<file> references <name>
  // <version>, newer than the reference <version>", a name the references do
  // not have the warning "<file> references <name>, which is not among the
  // references". Returns false after an error.
  bool check(const InputFile *model, ArrayRef<ManagedLibraryFile *> libraries);

  // The rest of the data model once LTO has written its objects (plan 5.8.9,
  // 5.9.13: a bitcode input's data model is checked through the LTO
  // objects' headers): when check() passed with a bitcode file as the model,
  // which gave only the pointer and long sizes, compares the table's wchar_t
  // and long double sizes with the header of \p lto, the first LTO object,
  // and reports "<file>: data model <m> differs from <m0> in <lto>" (5.9.14).
  // Does nothing when check() had a CIL object as the model, had none, or
  // failed. SymbolTable::compileBitcodeFiles() calls it, before late binding.
  // Returns false after an error.
  bool checkLTODataModel(const ObjFile &lto);

  // The identity of the Assembly row (no key: a toolchain library has none).
  const AssemblyIdentity &getIdentity() const { return identity; }
  const llvm::cli::LibrarySymbolTable &getTable() const { return table; }
  const llvm::cli::MetadataReader &getReader() const { return *reader; }
  // True for an interface library (--interface-only; flag INTERFACE), whose
  // methods are stubs: linking against it binds as against the library built
  // from the same objects.
  bool isInterface() const {
    return table.Flags & llvm::cli::LibraryTableFlags::Interface;
  }

  // The export of \p name, or null. Valid after check().
  const ManagedExport *findExport(StringRef name) const;
  // Every export: the functions of each export class in method order, then
  // the data symbols in table order. Valid after check().
  ArrayRef<ManagedExport *> exports() const { return exportList; }

  // The rows of the output (plan 5.9.15, M22.c item 7). The builder
  // deduplicates them, so every call may make them again:
  // - the AssemblyRef of the library: its name, version and culture, Flags 0,
  //   no key or token;
  uint32_t getAssemblyRef(llvm::cli::MetadataBuilder &mb);
  // - the TypeRef [lib]Ns.Class of the export class \p classIndex, an index
  //   of the table's ExportClasses (check() refuses a table without one);
  //   the first one by default, which phase 2's 'ldtoken' names (M22.b's
  //   TrailerInput::LibraryTypes, used or not);
  uint32_t getExportClassTypeRef(llvm::cli::MetadataBuilder &mb,
                                 uint32_t classIndex = 0);
  // - the MemberRef of \p e on its class: with \p accessor,
  //   '<msil>addr_<name>' with signature 00 00 18 ('native int()'); else the
  //   exported method with the library's signature, its TypeDefOrRef entries
  //   mapped as getOutputSignature() maps them. \p e must be a function's
  //   unless \p accessor. Errors are getOutputSignature()'s.
  Expected<uint32_t> getMemberRef(const ManagedExport &e, bool accessor,
                                  llvm::cli::MetadataBuilder &mb,
                                  BCLBinder *binder = nullptr);
  // The signature of the function \p e with output tokens: a TypeRef of the
  // library is bound by its name through \p binder (plan 5.11.4; Phase 2's C
  // signatures name no type), and a TypeDef of the library becomes the
  // TypeRef [lib]Ns.Name. An Error names the function and the type: a type
  // that \p binder cannot bind or that there is no binder for, or a
  // TypeSpec.
  Expected<llvm::cli::MethodSig>
  getOutputSignature(const ManagedExport &e, llvm::cli::MetadataBuilder &mb,
                     BCLBinder *binder = nullptr);

private:
  bool readExports();
  void checkAssemblyRefs(ArrayRef<ManagedLibraryFile *> libraries);
  Expected<uint32_t> mapTypeToken(uint32_t token,
                                  llvm::cli::MetadataBuilder &mb,
                                  BCLBinder *binder);
  void reportMalformed(const llvm::Twine &what);

  std::unique_ptr<llvm::cli::MetadataReader> reader;
  AssemblyIdentity identity;
  llvm::cli::AssemblyVersion version;
  llvm::cli::LibrarySymbolTable table;
  // The export classes' TypeDef namespaces and names, by table index.
  SmallVector<std::pair<StringRef, StringRef>, 1> classNames;
  SmallVector<ManagedExport *, 0> exportList;
  llvm::StringMap<ManagedExport *> exportMap;
  bool parsed = false;
  // Set by check() when it passed against a bitcode model, whose wchar_t and
  // long double sizes checkLTODataModel() compares after LTO.
  bool needsLTODataModel = false;
};

// The driver's one path for a library built by this toolchain (plan 5.9.2,
// 5.9.4, 5.11.1), positional and found by -l: a ManagedLibraryFile, or null
// after the error "<path>: " and the reader's message.
InputFile *createManagedLibraryFile(MemoryBufferRef mb);

// The name of the output assembly as the writer names its Assembly row:
// --assembly-name or -soname, else the stem of -o (only the last extension
// is dropped; plan 5.9.12). A library input of that name is an error
// (SymbolTable::addFile), and a library's AssemblyRef of that name refers to
// the output (ManagedLibraryFile::check).
std::string getOutputAssemblyName();
} // namespace lld::msil

#endif
