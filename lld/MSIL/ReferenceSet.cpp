//===- ReferenceSet.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The reference set of plan 5.11.2-5.11.4, modeled on the bcl-binding
// recon's resolver (recon/p2/bcl-binding-files/tool/Program.cs: Asm.Open,
// RefSet.Load, CorLib, the forwarder walk) and on Roslyn's
// CommonReferenceManager (IndexOfCorLibrary).
//
//===----------------------------------------------------------------------===//

#include "ReferenceSet.h"
#include "AssemblyFiles.h"
#include "InputFiles.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <algorithm>
#include <functional>
#include <numeric>

using namespace llvm;
using namespace llvm::sys;

namespace lld::msil {
//===----------------------------------------------------------------------===//
// Type names and paths
//===----------------------------------------------------------------------===//

std::string joinTypeName(StringRef ns, StringRef name) {
  if (ns.empty())
    return name.str();
  return (ns + "." + name).str();
}

std::pair<StringRef, StringRef> splitTypeName(StringRef fullName) {
  size_t dot = fullName.substr(0, fullName.find('/')).rfind('.');
  if (dot == StringRef::npos)
    return {StringRef(), fullName};
  return {fullName.take_front(dot), fullName.drop_front(dot + 1)};
}

// getIndexTypeName() for a type at \p depth levels of nesting below the type
// that was asked for.
static Expected<std::pair<std::string, std::string>>
getIndexTypeNameAt(const cli::MetadataReader &r, uint32_t token,
                   unsigned depth) {
  if (depth > maxTypeNestingDepth)
    return createStringError("type nesting too deep");
  uint8_t table = cli::getTokenType(token);
  if (table == static_cast<uint8_t>(cli::TableId::TypeDef)) {
    Expected<cli::TypeDefRow> row = r.getTypeDef(cli::getTokenRow(token));
    if (!row)
      return row.takeError();
    Expected<uint32_t> enclosing = r.getEnclosingType(token);
    if (!enclosing)
      return enclosing.takeError();
    if (!*enclosing)
      return std::make_pair(row->Namespace.str(), row->Name.str());
    auto outer = getIndexTypeNameAt(r, *enclosing, depth + 1);
    if (!outer)
      return outer.takeError();
    outer->second += "/" + joinTypeName(row->Namespace, row->Name);
    return outer;
  }
  if (table == static_cast<uint8_t>(cli::TableId::TypeRef)) {
    Expected<cli::TypeRefRow> row = r.getTypeRef(cli::getTokenRow(token));
    if (!row)
      return row.takeError();
    // A ResolutionScope of TypeRef row 0 is no enclosing type.
    if (cli::getTokenType(row->ResolutionScope) !=
            static_cast<uint8_t>(cli::TableId::TypeRef) ||
        cli::getTokenRow(row->ResolutionScope) == 0)
      return std::make_pair(row->Namespace.str(), row->Name.str());
    auto outer = getIndexTypeNameAt(r, row->ResolutionScope, depth + 1);
    if (!outer)
      return outer.takeError();
    outer->second += "/" + joinTypeName(row->Namespace, row->Name);
    return outer;
  }
  return createStringError("token 0x" +
                           utohexstr(token, /*LowerCase=*/false, /*Width=*/8) +
                           " is not a type definition or reference");
}

Expected<std::pair<std::string, std::string>>
getIndexTypeName(const cli::MetadataReader &reader, uint32_t token) {
  return getIndexTypeNameAt(reader, token, /*depth=*/0);
}

std::string getDisplayPath(StringRef path) {
  return path::convert_to_slash(path);
}

//===----------------------------------------------------------------------===//
// AssemblyIdentity
//===----------------------------------------------------------------------===//

std::string AssemblyIdentity::getVersionString() const {
  return (Twine(Major) + "." + Twine(Minor) + "." + Twine(Build) + "." +
          Twine(Revision))
      .str();
}

std::string AssemblyIdentity::getPublicKeyTokenString() const {
  if (!PublicKeyToken)
    return "null";
  return toHex(ArrayRef<uint8_t>(*PublicKeyToken), /*LowerCase=*/true);
}

std::string AssemblyIdentity::str() const {
  std::string s = Name + " " + getVersionString();
  if (!Culture.empty() && !StringRef(Culture).equals_insensitive("neutral"))
    s += " culture=" + Culture;
  return s + " [" + getPublicKeyTokenString() + "]";
}

bool AssemblyIdentity::isSameIdentity(const AssemblyIdentity &other) const {
  return StringRef(Name).equals_insensitive(other.Name) &&
         StringRef(Culture).equals_insensitive(other.Culture) &&
         Major == other.Major && Minor == other.Minor && Build == other.Build &&
         Revision == other.Revision && PublicKeyToken == other.PublicKeyToken;
}

//===----------------------------------------------------------------------===//
// ReferenceSet
//===----------------------------------------------------------------------===//

// One file of the set: the file, its normalized path, and the key that
// orders the files independently of the order they were added in.
struct ReferenceSet::Entry {
  std::unique_ptr<ReferenceAssemblyFile> file;
  std::string normalizedPath;
  std::string sortKey;
};

ReferenceSet::ReferenceSet() = default;
ReferenceSet::~ReferenceSet() = default;

// The path that identifies a file: absolute, without "." and ".."
// components, with native separators, and lowercased on Windows hosts, whose
// file systems ignore case. The same normalized path twice is one file (plan
// 5.11.2).
static std::string normalizePath(StringRef path) {
  SmallString<256> s(path);
  if (std::error_code ec = fs::make_absolute(s))
    s = path;
  path::remove_dots(s, /*remove_dot_dot=*/true);
  path::native(s);
  if (path::is_style_windows(path::Style::native))
    return StringRef(s).lower();
  return std::string(s);
}

// The key that orders the files of the set (their rank): identities ignoring
// case first (mscorlib before System.Runtime), then exactly, then normalized
// paths.
static std::string getSortKey(const AssemblyIdentity &id,
                              StringRef normalizedPath) {
  std::string text = id.str();
  return StringRef(text).lower() + '\0' + text + '\0' + normalizedPath.str();
}

Error ReferenceSet::addPath(StringRef path, bool explicitPath) {
  // Any reference turns the built-in table off, even one that adds no file
  // (plan 5.11.7).
  referenceGiven = true;
  if (!fs::is_directory(path)) {
    std::optional<MemoryBufferRef> mb = readFile(path);
    if (!mb)
      return Error::success(); // readFile() reported it
    return addFile(*mb, explicitPath);
  }

  // Every *.dll of the directory, not recursively, in file-name order, so
  // that the warnings about duplicate identities do not depend on the order
  // in which the file system lists the directory.
  std::vector<std::string> dlls;
  std::error_code ec;
  for (fs::directory_iterator it(path, ec), end; it != end && !ec;
       it.increment(ec)) {
    StringRef file = it->path();
    if (!path::extension(file).equals_insensitive(".dll"))
      continue;
    ErrorOr<fs::basic_file_status> status = it->status();
    if (!status || !fs::is_regular_file(*status))
      continue;
    dlls.push_back(file.str());
  }
  if (ec)
    return createStringError("cannot read directory " + getDisplayPath(path) +
                             ": " + ec.message());
  llvm::sort(dlls, [](const std::string &a, const std::string &b) {
    StringRef fa = path::filename(a), fb = path::filename(b);
    int c = fa.compare_insensitive(fb);
    return c != 0 ? c < 0 : fa < fb;
  });

  Error errs = Error::success();
  for (const std::string &file : dlls) {
    std::optional<MemoryBufferRef> mb = readFile(file);
    if (!mb)
      continue;
    errs = joinErrors(std::move(errs), addFile(*mb, /*explicitPath=*/false));
  }
  return errs;
}

Error ReferenceSet::addFile(MemoryBufferRef mb, bool explicitPath) {
  referenceGiven = true;
  StringRef path = mb.getBufferIdentifier();
  std::string normalized = normalizePath(path);
  // The same path twice is one file once it was added or diagnosed.
  if (paths.contains(normalized))
    return Error::success();

  // Only .NET assemblies are references: a PE image with a CLI header and an
  // Assembly row. A directory's other files are skipped silently, and their
  // paths are not recorded, so that naming one explicitly still reports it.
  auto notAnAssembly = [&]() -> Error {
    if (!explicitPath)
      return Error::success();
    paths.insert(normalized);
    return createStringError(getDisplayPath(path) + ": not a .NET assembly");
  };
  if (!isCLIImage(mb))
    return notAnAssembly();
  Expected<std::unique_ptr<cli::MetadataReader>> readerOrErr =
      cli::MetadataReader::create(
          ArrayRef(reinterpret_cast<const uint8_t *>(mb.getBufferStart()),
                   mb.getBufferSize()));
  if (readerOrErr && !(*readerOrErr)->isAssembly())
    return notAnAssembly();
  // From here on the file is added or diagnosed.
  paths.insert(normalized);
  if (!readerOrErr)
    return createStringError(getDisplayPath(path) + ": " +
                             toString(readerOrErr.takeError()));
  Expected<std::unique_ptr<ReferenceAssemblyFile>> fileOrErr =
      ReferenceAssemblyFile::create(mb, std::move(*readerOrErr));
  if (!fileOrErr)
    return fileOrErr.takeError();
  std::unique_ptr<ReferenceAssemblyFile> file = std::move(*fileOrErr);

  // A library built by this toolchain is a link input, not a reference
  // (plan 5.9.4, 5.11.1).
  if (isToolchainLibrary(file->getReader())) {
    warn(getDisplayPath(path) +
         " is a library built by this toolchain; pass it as a link input "
         "instead");
    return Error::success();
  }

  // Identities (plan 5.11.2): no unification. The simple name decides which
  // earlier file to compare with, ignoring case as the runtimes do.
  const AssemblyIdentity &id = file->getIdentity();
  std::string sortKey = getSortKey(id, normalized);
  std::string simpleName = StringRef(id.Name).lower();
  auto it = simpleNames.find(simpleName);
  if (it != simpleNames.end()) {
    const Entry &firstEntry = *entries[it->second];
    const ReferenceAssemblyFile &first = *firstEntry.file;
    // Another version or key is an error whatever the order of the two,
    // which the text names in the order of the set, each file with its own
    // identity (plan 5.11.9).
    if (!first.getIdentity().isSameIdentity(id)) {
      StringRef lowPath = first.getPath(), highPath = path;
      std::string lowId = first.getIdentity().str(), highId = id.str();
      if (sortKey < firstEntry.sortKey) {
        std::swap(lowPath, highPath);
        std::swap(lowId, highId);
      }
      return createStringError("references " + getDisplayPath(lowPath) + " (" +
                               lowId + ") and " + getDisplayPath(highPath) +
                               " (" + highId +
                               ") have the same name but different versions "
                               "or keys");
    }
    // The same bytes are one reference: either file gives the same output.
    if (first.getMemoryBufferRef().getBuffer() == mb.getBuffer()) {
      warn(getDisplayPath(path) + " has the same identity as " +
           getDisplayPath(first.getPath()) + " (" + id.str() +
           "); using the first");
      return Error::success();
    }
    // Other bytes are an error whatever the order of the two, which the
    // text names in the order of the set.
    StringRef lowPath = first.getPath(), highPath = path;
    std::string identity = first.getIdentity().str();
    if (sortKey < firstEntry.sortKey) {
      std::swap(lowPath, highPath);
      identity = id.str();
    }
    return createStringError("references " + getDisplayPath(lowPath) + " and " +
                             getDisplayPath(highPath) +
                             " have the same identity (" + identity +
                             ") but different contents");
  }

  auto entry = std::make_unique<Entry>();
  entry->normalizedPath = std::move(normalized);
  entry->sortKey = std::move(sortKey);
  entry->file = std::move(file);
  simpleNames[simpleName] = entries.size();
  fileList.push_back(entry->file.get());
  entries.push_back(std::move(entry));
  // Lookups index the new file; the core library is computed again.
  haveCore = false;
  return Error::success();
}

const ReferenceAssemblyFile *
ReferenceSet::findAssembly(StringRef simpleName) const {
  auto it = simpleNames.find(simpleName.lower());
  if (it == simpleNames.end())
    return nullptr;
  return entries[it->second]->file.get();
}

void ReferenceSet::ensureIndexed() const {
  if (numIndexed == entries.size())
    return;
  while (numIndexed < entries.size())
    indexFile(numIndexed++);

  // The rank of each file in the order of identities and paths.
  std::vector<uint32_t> order(entries.size());
  std::iota(order.begin(), order.end(), 0);
  llvm::sort(order, [&](uint32_t a, uint32_t b) {
    return entries[a]->sortKey < entries[b]->sortKey;
  });
  ranks.assign(entries.size(), 0);
  for (auto [rank, fileIndex] : llvm::enumerate(order))
    ranks[fileIndex] = rank;
}

// Indexes the public TypeDefs and the forwarders of one file (plan 5.11.2).
// Rows that cannot be read are skipped: the reader reports such damage when
// the binder decodes the rows it needs.
void ReferenceSet::indexFile(uint32_t fileIndex) const {
  const cli::MetadataReader &r = entries[fileIndex]->file->getReader();

  // TypeDefs. A nested type is visible when it is nested-public and its
  // enclosing type is visible; a top-level type when it is public. Full
  // names of visible enclosing types are memoized (std::nullopt: invisible).
  uint32_t numTypes = r.getRowCount(cli::TableId::TypeDef);
  DenseMap<uint32_t, std::optional<std::string>> visibleNames;
  std::function<std::optional<std::string>(uint32_t, unsigned)> getVisibleName =
      [&](uint32_t row, unsigned depth) -> std::optional<std::string> {
    auto memo = visibleNames.find(row);
    if (memo != visibleNames.end())
      return memo->second;
    std::optional<std::string> result;
    Expected<cli::TypeDefRow> def = r.getTypeDef(row);
    Expected<uint32_t> enclosing =
        r.getEnclosingType(cli::makeToken(cli::TableId::TypeDef, row));
    if (!def || !enclosing) {
      consumeError(def.takeError());
      consumeError(enclosing.takeError());
    } else {
      cli::TypeAttributes visibility =
          def->Flags & cli::TypeAttributes::VisibilityMask;
      if (*enclosing == 0) {
        if (visibility == cli::TypeAttributes::Public)
          result = joinTypeName(def->Namespace, def->Name);
      } else if (visibility == cli::TypeAttributes::NestedPublic &&
                 depth < maxTypeNestingDepth) {
        if (std::optional<std::string> outer =
                getVisibleName(cli::getTokenRow(*enclosing), depth + 1))
          result = *outer + "/" + joinTypeName(def->Namespace, def->Name);
      }
    }
    visibleNames[row] = result;
    return result;
  };
  for (uint32_t row = 1; row <= numTypes; ++row) {
    std::optional<std::string> name = getVisibleName(row, 0);
    if (!name)
      continue;
    NameEntry e;
    e.fileIndex = fileIndex;
    e.token = cli::makeToken(cli::TableId::TypeDef, row);
    names[*name].push_back(e);
  }

  // Forwarders: a top-level ExportedType with the forwarder bit whose
  // Implementation is an AssemblyRef; a nested one chains through its
  // enclosing ExportedType (without the bit) to such a row. Types exported
  // from another module of the assembly (Implementation is a File) are not
  // forwarders.
  uint32_t numExported = r.getRowCount(cli::TableId::ExportedType);
  struct Forward {
    std::string name;
    uint32_t assemblyRef = 0;
  };
  DenseMap<uint32_t, std::optional<Forward>> forwards;
  std::function<std::optional<Forward>(uint32_t, unsigned)> getForward =
      [&](uint32_t row, unsigned depth) -> std::optional<Forward> {
    auto memo = forwards.find(row);
    if (memo != forwards.end())
      return memo->second;
    std::optional<Forward> result;
    Expected<cli::ExportedTypeRow> et = r.getExportedType(row);
    if (!et) {
      consumeError(et.takeError());
    } else {
      uint8_t implTable = cli::getTokenType(et->Implementation);
      if (implTable == static_cast<uint8_t>(cli::TableId::AssemblyRef) &&
          cli::getTokenRow(et->Implementation) != 0) {
        if ((et->Flags & cli::TypeAttributes::Forwarder) ==
            cli::TypeAttributes::Forwarder)
          result = Forward{joinTypeName(et->Namespace, et->Name),
                           et->Implementation};
      } else if (implTable ==
                     static_cast<uint8_t>(cli::TableId::ExportedType) &&
                 cli::getTokenRow(et->Implementation) != 0 &&
                 depth < maxTypeNestingDepth) {
        if (std::optional<Forward> outer =
                getForward(cli::getTokenRow(et->Implementation), depth + 1))
          result =
              Forward{outer->name + "/" + joinTypeName(et->Namespace, et->Name),
                      outer->assemblyRef};
      }
    }
    forwards[row] = result;
    return result;
  };
  for (uint32_t row = 1; row <= numExported; ++row) {
    std::optional<Forward> f = getForward(row, 0);
    if (!f)
      continue;
    Expected<cli::AssemblyRefRow> ref =
        r.getAssemblyRef(cli::getTokenRow(f->assemblyRef));
    if (!ref) {
      consumeError(ref.takeError());
      continue;
    }
    NameEntry e;
    e.fileIndex = fileIndex;
    e.token = cli::makeToken(cli::TableId::ExportedType, row);
    e.isForwarder = true;
    e.assemblyRef = f->assemblyRef;
    e.destination = ref->Name;
    names[f->name].push_back(e);
  }
}

ArrayRef<ReferenceSet::NameEntry>
ReferenceSet::lookup(StringRef fullName) const {
  ensureIndexed();
  auto it = names.find(fullName);
  if (it == names.end())
    return {};
  return it->second;
}

TypeCandidates ReferenceSet::findType(StringRef ns, StringRef name) const {
  // The order of identities and paths, not the order of the references.
  SmallVector<NameEntry, 2> matches(lookup(joinTypeName(ns, name)));
  llvm::sort(matches, [&](const NameEntry &a, const NameEntry &b) {
    return std::make_pair(ranks[a.fileIndex], a.token) <
           std::make_pair(ranks[b.fileIndex], b.token);
  });
  TypeCandidates result;
  for (const NameEntry &e : matches) {
    const ReferenceAssemblyFile *file = entries[e.fileIndex]->file.get();
    if (e.isForwarder)
      result.Forwarders.push_back(
          {file, e.token, e.assemblyRef, e.destination});
    else
      result.Definitions.push_back({file, e.token});
  }
  return result;
}

// The names of the AssemblyRef rows of \p r, joined by ", ".
static std::string getAssemblyRefNames(const cli::MetadataReader &r) {
  std::string names;
  for (uint32_t row = 1, n = r.getRowCount(cli::TableId::AssemblyRef); row <= n;
       ++row) {
    Expected<cli::AssemblyRefRow> ref = r.getAssemblyRef(row);
    if (!ref) {
      consumeError(ref.takeError());
      continue;
    }
    if (!names.empty())
      names += ", ";
    names += ref->Name;
  }
  return names;
}

Expected<const ReferenceAssemblyFile *> ReferenceSet::coreLibrary() const {
  ensureIndexed();
  if (!haveCore) {
    haveCore = true;
    core = nullptr;
    coreError.clear();

    // What each file does with System.Object, in the order of identities
    // and paths.
    std::vector<uint32_t> order(entries.size());
    std::iota(order.begin(), order.end(), 0);
    llvm::sort(order,
               [&](uint32_t a, uint32_t b) { return ranks[a] < ranks[b]; });
    std::vector<std::string> notes(entries.size());
    SmallVector<uint32_t, 2> candidates;
    for (const NameEntry &e : lookup("System.Object")) {
      const cli::MetadataReader &r = entries[e.fileIndex]->file->getReader();
      std::string &note = notes[e.fileIndex];
      if (e.isForwarder) {
        if (note.empty())
          note = ("(facade, forwards System.Object to " + e.destination + ")")
                     .str();
        continue;
      }
      // A definition: the core library has no AssemblyRef rows, and its
      // System.Object has no base type.
      Expected<cli::TypeDefRow> def = r.getTypeDef(cli::getTokenRow(e.token));
      if (!def) {
        consumeError(def.takeError());
        continue;
      }
      if (r.getRowCount(cli::TableId::AssemblyRef) != 0) {
        note = "(defines System.Object but references " +
               getAssemblyRefNames(r) + ")";
        continue;
      }
      if (def->Extends != 0) {
        note = "(defines System.Object with a base type)";
        continue;
      }
      note.clear();
      if (!is_contained(candidates, e.fileIndex))
        candidates.push_back(e.fileIndex);
    }

    auto describe = [&](uint32_t i) {
      const ReferenceAssemblyFile &f = *entries[i]->file;
      return f.getIdentity().str() + " (" + getDisplayPath(f.getPath()) + ")";
    };
    if (candidates.size() == 1) {
      core = entries[candidates.front()]->file.get();
    } else if (candidates.empty()) {
      // The files that define or forward System.Object, else every file.
      std::string given;
      bool any = any_of(notes, [](const std::string &n) { return !n.empty(); });
      for (uint32_t i : order) {
        if (any && notes[i].empty())
          continue;
        if (!given.empty())
          given += ", ";
        given += entries[i]->file->getIdentity().str();
        if (!notes[i].empty())
          given += " " + notes[i];
      }
      // A reference that added no file (an empty directory, plan 5.11.7).
      if (given.empty())
        given = "no references";
      coreError = "the references contain no core library (an assembly that "
                  "defines System.Object and references no other assembly); "
                  "given: " +
                  given;
    } else {
      llvm::sort(candidates,
                 [&](uint32_t a, uint32_t b) { return ranks[a] < ranks[b]; });
      std::string list;
      for (auto [n, i] : llvm::enumerate(candidates)) {
        if (n != 0)
          list += n + 1 == candidates.size() ? " and " : ", ";
        list += describe(i);
      }
      size_t n = candidates.size();
      coreError = "the references mix " +
                  (n == 2 ? std::string("two") : std::to_string(n)) +
                  " core libraries: " + list;
    }
  }
  if (!core)
    return createStringError(coreError);
  return core;
}
} // namespace lld::msil
