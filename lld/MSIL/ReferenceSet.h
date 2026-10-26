//===- ReferenceSet.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The reference assemblies of a link (llvm/docs/MSIL/ImplementationPlan.md
// 5.11.1-5.11.4): the files of --reference=<file|dir> and the positional
// .NET assemblies without the toolchain's symbol table, in command-line
// order, with their identities (from the Assembly row and the public-key
// token of its key), the core-library rule (Roslyn's IndexOfCorLibrary) and
// the type lookup that the BCL binder (BCLBinder.h) builds on.
//
// The set is lazy: adding a file maps it and reads its PE headers, metadata
// root and Assembly row; the first lookup indexes the names of the public
// TypeDefs (nested ones as Outer/Inner) and of the type forwarders
// (ExportedType rows) of every file. No member table is decoded here; the
// binder decodes the members of the types it binds.
//
// Nothing depends on the order of the references: lookups return every
// candidate, sorted by identity and path, and ambiguity is the binder's
// error (plan 5.11.4, 5.11.6).
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_REFERENCE_SET_H
#define LLD_MSIL_REFERENCE_SET_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm::cli {
class MetadataReader;
} // namespace llvm::cli

namespace lld::msil {
// The type names of the reference set's index, which the binder spells the
// same way (plan 5.11.2, 5.11.4): a full name is "<Namespace>.<Name>", or
// "<Name>" without a namespace, and a nested type's name is "Outer/Inner",
// each part with its own namespace, the namespace of the full name being the
// outermost type's.

// "<ns>.<name>", or "<name>" when \p ns is empty.
std::string joinTypeName(StringRef ns, StringRef name);

// Splits a full name at the last '.' before the first '/' into the
// namespace and the name ("" and \p fullName without such a dot).
std::pair<StringRef, StringRef> splitTypeName(StringRef fullName);

// Nesting deeper than this (enclosing TypeDefs, TypeRefs or ExportedTypes) is
// taken for a cycle of damaged metadata: 65 levels are read, the outermost
// type at depth maxTypeNestingDepth.
constexpr unsigned maxTypeNestingDepth = 64;

// The namespace and the name of the TypeDef or TypeRef \p token of
// \p reader as the index spells them (nested parts "Outer/Inner"). A TypeRef
// is nested when its ResolutionScope is a TypeRef other than row 0. Errors
// are the reader's, "type nesting too deep" past maxTypeNestingDepth, and
// "token 0x<token> is not a type definition or reference".
Expected<std::pair<std::string, std::string>>
getIndexTypeName(const llvm::cli::MetadataReader &reader, uint32_t token);

// A path as diagnostics print it: with '/' separators, so that a file reads
// the same whether it was named or found in a directory (whose entries get
// the host's separator). Buffer identifiers keep their spelling.
std::string getDisplayPath(StringRef path);

// The identity of a reference assembly (plan 5.11.2): its Assembly row, with
// the public-key token of the row's key (cli::computePublicKeyToken). This is
// lld::msil's identity; cli::AssemblyIdentity (llvm/CLI/MetadataBuilder.h)
// carries the key itself, for an Assembly row that a writer adds.
struct AssemblyIdentity {
  std::string Name, Culture;
  uint16_t Major = 0, Minor = 0, Build = 0, Revision = 0;
  // std::nullopt for an assembly without a public key.
  std::optional<std::array<uint8_t, 8>> PublicKeyToken;

  // "<Major>.<Minor>.<Build>.<Revision>".
  std::string getVersionString() const;
  // The token as 16 lowercase hex digits, or "null".
  std::string getPublicKeyTokenString() const;
  // The text diagnostics use for <identity> (plan 5.11.9):
  // "<Name> <version> [<token>]", for example
  // "mscorlib 4.0.0.0 [b77a5c561934e089]" or "Fake 1.0.0.0 [null]"; a culture
  // other than neutral follows the version as "culture=<culture>".
  std::string str() const;
  // The same full identity: the name and the culture equal ignoring ASCII
  // case (as the runtimes compare them), the same version and token.
  bool isSameIdentity(const AssemblyIdentity &other) const;
};

class ReferenceAssemblyFile; // AssemblyFiles.h (plan 5.9.2)

// The candidates for a full type name (plan 5.11.4): every public definition
// in the set and every reference that forwards the name, each list sorted by
// the identity and the path of its file, so that nothing depends on the order
// of the references.
struct TypeCandidates {
  // A public TypeDef: top level and public, or nested-public inside an
  // enclosing type that is itself visible.
  struct Definition {
    const ReferenceAssemblyFile *File = nullptr;
    // A TypeDef token of File.
    uint32_t TypeDef = 0;
  };
  // A type forwarder: a top-level ExportedType with TypeAttributes::Forwarder
  // whose Implementation is an AssemblyRef, or a nested ExportedType whose
  // chain of enclosing ExportedTypes ends in one.
  struct Forwarder {
    const ReferenceAssemblyFile *File = nullptr;
    // The ExportedType token of the type itself (a nested type's own row).
    uint32_t ExportedType = 0;
    // The AssemblyRef token of File that the outermost forwarder names, and
    // that AssemblyRef's simple name: the assembly the type is forwarded to.
    // Its version is the forwarding assembly's view (0.0.0.0 in the .NET 8
    // pack's facades, bcl-binding 2.3), never an identity to emit.
    uint32_t AssemblyRef = 0;
    StringRef Destination;
  };
  SmallVector<Definition, 1> Definitions;
  SmallVector<Forwarder, 1> Forwarders;

  bool empty() const { return Definitions.empty() && Forwarders.empty(); }
};

// The reference set of one link (Ctx::references, reset by Ctx::reset()).
class ReferenceSet {
public:
  ReferenceSet();
  ~ReferenceSet();
  ReferenceSet(const ReferenceSet &) = delete;
  ReferenceSet &operator=(const ReferenceSet &) = delete;

  // Adds a file or a directory (plan 5.11.1). A directory contributes its
  // *.dll files (not recursively), in file-name order, as non-explicit files.
  // Every file is read through readFile(), so --reproduce keeps it. Returns
  // the errors of the path (one ErrorInfo per problem; the driver reports
  // each); warnings are reported directly. See addFile() for the rules.
  Error addPath(StringRef path, bool explicitPath);

  // Adds a file that the caller read (the driver's positional reference
  // assemblies; addPath() for each file). Its buffer must stay alive for the
  // link, as readFile()'s buffers do. The rules (plan 5.9.4, 5.11.1, 5.11.2,
  // 5.11.9; diagnostics print paths through getDisplayPath()):
  // - a file without a CLI header or without an Assembly row is the error
  //   "<path>: not a .NET assembly" when \p explicitPath, else skipped
  //   silently;
  // - a CLI image whose metadata cannot be read is "<path>: <reason>";
  // - the same normalized path twice is ignored once the first was added or
  //   diagnosed: a file that a directory skipped silently is checked again
  //   when it is named explicitly;
  // - a library built by this toolchain (isToolchainLibrary) is skipped with
  //   the warning "<path> is a library built by this toolchain; pass it as a
  //   link input instead";
  // - a file with the same full identity and the same bytes as an earlier
  //   one is skipped with the warning "<path> has the same identity as
  //   <earlier path> (<identity>); using the first";
  // - a file with the same full identity as an earlier one but other bytes
  //   is the error "references <path> and <path> have the same identity
  //   (<identity>) but different contents", the two paths in the order of
  //   the set (identities, then normalized paths), and is not added;
  // - a file with the simple name of an earlier one but another version,
  //   culture or token is the error "references <earlier path> (<identity>)
  //   and <path> (<identity>) have the same name but different versions or
  //   keys" (no unification, Roslyn's r15), and is not added.
  Error addFile(MemoryBufferRef mb, bool explicitPath);

  // True when no reference was given: neither addPath() nor addFile() was
  // called. A reference that adds no file (an empty directory, a directory
  // of native DLLs) still makes the set non-empty, so that the link binds
  // against it and fails the core-library rule (plan 5.11.7: any reference
  // turns the built-in table off).
  bool empty() const { return !referenceGiven; }

  // The files of the set in the order they were added.
  ArrayRef<const ReferenceAssemblyFile *> files() const { return fileList; }

  // The core library (plan 5.11.3): the unique file that defines a public,
  // top-level System.Object without a base type and has no AssemblyRef rows.
  // Computed once (again only after a file is added). The errors are plan
  // 5.11.9's:
  // - "the references contain no core library (an assembly that defines
  //   System.Object and references no other assembly); given: <list>", where
  //   <list> names the files that define or forward System.Object, with
  //   "(facade, forwards System.Object to <name>)" or "(defines System.Object
  //   but references <names>)", or every file when none does, or is "no
  //   references" for a set without files;
  // - "the references mix two core libraries: <identity> (<path>) and
  //   <identity> (<path>)"; with three or more, "the references mix <n> core
  //   libraries: <list>" (section 12, 5.11.9 clarified), <n> in digits and
  //   <list> every candidate as "<identity> (<path>)" in rank order, joined
  //   by ", " with " and " before the last.
  Expected<const ReferenceAssemblyFile *> coreLibrary() const;

  // The public definitions and the forwarders of the type \p name in
  // namespace \p ns (nested types as "Outer/Inner", the namespace being the
  // outermost type's; generic arity in the name as metadata spells it,
  // "Vector128`1").
  TypeCandidates findType(StringRef ns, StringRef name) const;

  // The file whose simple name is \p simpleName (ignoring ASCII case), or
  // null: the destination of a forwarder (addFile() keeps one file per simple
  // name).
  const ReferenceAssemblyFile *findAssembly(StringRef simpleName) const;

private:
  // One name of the index: a public TypeDef or a forwarder of a file.
  struct NameEntry {
    uint32_t fileIndex = 0;
    uint32_t token = 0;
    bool isForwarder = false;
    // Forwarders: the AssemblyRef token and its name (see Forwarder).
    uint32_t assemblyRef = 0;
    StringRef destination;
  };
  struct Entry;

  // Indexes the files added since the last call (lazily, from a lookup), and
  // ranks every file in the order of identities and paths.
  void ensureIndexed() const;
  void indexFile(uint32_t fileIndex) const;
  // The index's entries for the full name "<Namespace>.<Name>" or "<Name>".
  ArrayRef<NameEntry> lookup(StringRef fullName) const;

  // Whether addPath() or addFile() was called (see empty()).
  bool referenceGiven = false;
  std::vector<std::unique_ptr<Entry>> entries;
  SmallVector<const ReferenceAssemblyFile *, 0> fileList;
  // The lowercased normalized paths that were added or diagnosed, and
  // lowercased simple name -> file index.
  llvm::StringSet<> paths;
  llvm::StringMap<uint32_t> simpleNames;

  // The lazy index and caches.
  mutable llvm::StringMap<SmallVector<NameEntry, 1>> names;
  mutable uint32_t numIndexed = 0;
  mutable std::vector<uint32_t> ranks;
  mutable bool haveCore = false;
  mutable const ReferenceAssemblyFile *core = nullptr;
  mutable std::string coreError;
};
} // namespace lld::msil

#endif
