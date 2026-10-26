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
// a ReferenceSet, plan 5.11), and libraries built by this toolchain, which
// carry its symbol table in an Assembly-row attribute (plan 5.9.15) and reach
// the link only through createManagedLibraryFile().
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_ASSEMBLY_FILES_H
#define LLD_MSIL_ASSEMBLY_FILES_H

#include "ReferenceSet.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace llvm::cli {
class MetadataReader;
} // namespace llvm::cli

namespace lld::msil {
class InputFile;

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

// The driver's one path for a library built by this toolchain (plan 5.9.2,
// 5.9.4, 5.11.1), positional and, from M22, found by -l. In M21 it reports
// "<path>: linking .NET libraries built by this toolchain is not supported
// yet" and returns null; M22 makes it return a ManagedLibraryFile.
InputFile *createManagedLibraryFile(MemoryBufferRef mb);
} // namespace lld::msil

#endif
