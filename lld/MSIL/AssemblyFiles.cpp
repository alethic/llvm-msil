//===- AssemblyFiles.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AssemblyFiles.h"
#include "BCLBinder.h"
#include "Config.h"
#include "Exports.h"
#include "InputFiles.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CLI/SymbolTable.h"
#include "llvm/Support/Path.h"
#include "llvm/TargetParser/Triple.h"
#include <tuple>

using namespace llvm;

namespace lld::msil {
static ArrayRef<uint8_t> getBytes(MemoryBufferRef mb) {
  return ArrayRef(reinterpret_cast<const uint8_t *>(mb.getBufferStart()),
                  mb.getBufferSize());
}

//===----------------------------------------------------------------------===//
// Attributes of the Assembly row
//===----------------------------------------------------------------------===//

// The full name (MetadataReader::getTypeFullName) of the type that declares
// the attribute constructor \p ctor, a MethodDef or a MemberRef whose parent
// is a TypeDef or TypeRef; "" for any other constructor.
static Expected<std::string> getAttributeTypeName(const cli::MetadataReader &r,
                                                  uint32_t ctor) {
  uint32_t type = 0;
  uint8_t table = cli::getTokenType(ctor);
  if (table == static_cast<uint8_t>(cli::TableId::MethodDef)) {
    Expected<uint32_t> owner = r.getOwnerTypeDef(ctor);
    if (!owner)
      return owner.takeError();
    type = *owner;
  } else if (table == static_cast<uint8_t>(cli::TableId::MemberRef)) {
    Expected<cli::MemberRefRow> ref = r.getMemberRef(cli::getTokenRow(ctor));
    if (!ref)
      return ref.takeError();
    uint8_t parentTable = cli::getTokenType(ref->Parent);
    if (cli::getTokenRow(ref->Parent) != 0 &&
        (parentTable == static_cast<uint8_t>(cli::TableId::TypeDef) ||
         parentTable == static_cast<uint8_t>(cli::TableId::TypeRef)))
      type = ref->Parent;
  }
  if (type == 0)
    return std::string();
  return r.getTypeFullName(type);
}

// The signature of the attribute constructor \p ctor (a MethodDef or a
// MemberRef).
static Expected<cli::MethodSig>
getConstructorSignature(const cli::MetadataReader &r, uint32_t ctor) {
  uint32_t blob = 0;
  if (cli::getTokenType(ctor) ==
      static_cast<uint8_t>(cli::TableId::MethodDef)) {
    Expected<cli::MethodDefRow> def = r.getMethodDef(cli::getTokenRow(ctor));
    if (!def)
      return def.takeError();
    blob = def->Signature;
  } else {
    Expected<cli::MemberRefRow> ref = r.getMemberRef(cli::getTokenRow(ctor));
    if (!ref)
      return ref.takeError();
    blob = ref->Signature;
  }
  Expected<ArrayRef<uint8_t>> bytes = r.getBlob(blob);
  if (!bytes)
    return bytes.takeError();
  return cli::decodeMethodSig(*bytes);
}

// True if a custom attribute of \p parent has a constructor of the class
// \p typeName that \p accept (when given) accepts. Metadata that cannot be
// read counts as no such attribute.
static bool hasAttribute(const cli::MetadataReader &r, uint32_t parent,
                         StringRef typeName,
                         function_ref<bool(uint32_t ctor)> accept) {
  Expected<SmallVector<uint32_t, 4>> rows = r.getCustomAttributes(parent);
  if (!rows) {
    consumeError(rows.takeError());
    return false;
  }
  for (uint32_t row : *rows) {
    Expected<cli::CustomAttributeRow> ca = r.getCustomAttribute(row);
    if (!ca) {
      consumeError(ca.takeError());
      continue;
    }
    Expected<std::string> name = getAttributeTypeName(r, ca->Constructor);
    if (!name) {
      consumeError(name.takeError());
      continue;
    }
    if (*name == typeName && (!accept || accept(ca->Constructor)))
      return true;
  }
  return false;
}

bool hasCustomAttribute(const cli::MetadataReader &r, uint32_t parent,
                        StringRef typeName) {
  return hasAttribute(r, parent, typeName, nullptr);
}

bool isToolchainLibrary(const cli::MetadataReader &r) {
  // The constructor takes the uint8[] payload (plan 5.9.15).
  if (!r.isAssembly())
    return false;
  uint32_t assembly = cli::makeToken(cli::TableId::Assembly, 1);
  return hasAttribute(r, assembly, cli::SymbolTableType, [&](uint32_t ctor) {
    Expected<cli::MethodSig> sig = getConstructorSignature(r, ctor);
    if (!sig) {
      consumeError(sig.takeError());
      return false;
    }
    if (sig->Params.size() != 1)
      return false;
    const cli::TypeSig &param = sig->Params.front();
    return param.getElementType() == cli::ELEMENT_TYPE_SZARRAY &&
           param.getOperands().front().getElementType() == cli::ELEMENT_TYPE_U1;
  });
}

CLIImageKind classifyCLIImage(MemoryBufferRef mb) {
  Expected<std::unique_ptr<cli::MetadataReader>> reader =
      cli::MetadataReader::create(getBytes(mb));
  if (!reader) {
    consumeError(reader.takeError());
    return CLIImageKind::Object;
  }
  if (!(*reader)->isAssembly())
    return CLIImageKind::Object;
  if (isToolchainLibrary(**reader))
    return CLIImageKind::ToolchainLibrary;
  return CLIImageKind::ReferenceAssembly;
}

//===----------------------------------------------------------------------===//
// ReferenceAssemblyFile
//===----------------------------------------------------------------------===//

ReferenceAssemblyFile::ReferenceAssemblyFile(
    MemoryBufferRef mb, std::unique_ptr<cli::MetadataReader> reader)
    : mb(mb), reader(std::move(reader)) {}

ReferenceAssemblyFile::~ReferenceAssemblyFile() = default;

Expected<std::unique_ptr<ReferenceAssemblyFile>>
ReferenceAssemblyFile::create(MemoryBufferRef mb,
                              std::unique_ptr<cli::MetadataReader> reader) {
  std::string path = getDisplayPath(mb.getBufferIdentifier());
  if (!reader->isAssembly())
    return createStringError(path + ": not a .NET assembly");

  // The identity (plan 5.11.2): the Assembly row, and the token of its
  // public key (the Assembly table holds the whole key).
  Expected<cli::AssemblyRow> row = reader->getAssembly();
  if (!row)
    return createStringError(path + ": " + toString(row.takeError()));
  Expected<ArrayRef<uint8_t>> key = reader->getBlob(row->PublicKey);
  if (!key)
    return createStringError(path + ": " + toString(key.takeError()));

  std::unique_ptr<ReferenceAssemblyFile> file(
      new ReferenceAssemblyFile(mb, std::move(reader)));
  AssemblyIdentity &id = file->identity;
  id.Name = row->Name.str();
  id.Culture = row->Culture.str();
  id.Major = row->Version.Major;
  id.Minor = row->Version.Minor;
  id.Build = row->Version.Build;
  id.Revision = row->Version.Revision;
  if (!key->empty())
    id.PublicKeyToken = cli::computePublicKeyToken(*key);
  return std::move(file);
}

bool ReferenceAssemblyFile::isReferenceAssembly() const {
  if (!referenceAssembly)
    referenceAssembly = hasCustomAttribute(
        *reader, cli::makeToken(cli::TableId::Assembly, 1),
        "System.Runtime.CompilerServices.ReferenceAssemblyAttribute");
  return *referenceAssembly;
}

//===----------------------------------------------------------------------===//
// Libraries built by this toolchain
//===----------------------------------------------------------------------===//

// The name of an accessor (plan 5.9.15): '<msil>addr_<name>'.
static constexpr StringLiteral accessorPrefix = "<msil>addr_";

// "<Major>.<Minor>.<Build>.<Revision>".
static std::string getVersionString(const cli::AssemblyVersion &v) {
  return (Twine(v.Major) + "." + Twine(v.Minor) + "." + Twine(v.Build) + "." +
          Twine(v.Revision))
      .str();
}

static bool isNewer(const cli::AssemblyVersion &a,
                    const cli::AssemblyVersion &b) {
  return std::tie(a.Major, a.Minor, a.Build, a.Revision) >
         std::tie(b.Major, b.Minor, b.Build, b.Revision);
}

// Plan 5.8.9's triple key: the architecture, the OS and the environment
// without its version of an MSIL triple; the vendor is ignored.
static bool hasTripleKey(const Triple &tt, const Triple &target) {
  return tt.isOSBinFormatMSIL() && tt.getArch() == target.getArch() &&
         tt.getSubArch() == target.getSubArch() &&
         tt.getOS() == target.getOS() &&
         tt.getEnvironment() == target.getEnvironment();
}

// The data model as llvm-cil-dump prints it (plan 5.8.11).
static std::string getDataModelString(unsigned pointerSize, unsigned longSize,
                                      unsigned wcharSize,
                                      unsigned longDoubleSize) {
  return (Twine(pointerSize) + "," + Twine(longSize) + "," + Twine(wcharSize) +
          "," + Twine(longDoubleSize))
      .str();
}

// True if \p sig is the signature of an accessor: 'native int()', static.
static bool isAccessorSignature(const cli::MethodSig &sig) {
  return sig.Header == cli::SIG_DEFAULT && sig.Params.empty() &&
         sig.Return == cli::TypeSig::get(cli::ELEMENT_TYPE_I);
}

static bool isPublicStatic(cli::MethodAttributes flags) {
  return (flags & cli::MethodAttributes::MemberAccessMask) ==
             cli::MethodAttributes::Public &&
         (flags & cli::MethodAttributes::Static) ==
             cli::MethodAttributes::Static;
}

ManagedLibraryFile::ManagedLibraryFile(
    MemoryBufferRef mb, std::unique_ptr<cli::MetadataReader> reader)
    : InputFile(ManagedLibraryKind, mb), reader(std::move(reader)) {}

ManagedLibraryFile::~ManagedLibraryFile() = default;

void ManagedLibraryFile::reportMalformed(const Twine &what) {
  error(toString(this) + ": malformed export table: " + what);
}

bool ManagedLibraryFile::parse() {
  if (parsed)
    return true;
  parsed = true;
  std::string path = toString(this);

  // The identity (plan 5.9.15): the Assembly row; a toolchain library has
  // no key.
  Expected<cli::AssemblyRow> row = reader->getAssembly();
  if (!row) {
    error(path + ": " + toString(row.takeError()));
    return false;
  }
  version = row->Version;
  identity.Name = row->Name.str();
  identity.Culture = row->Culture.str();
  identity.Major = version.Major;
  identity.Minor = version.Minor;
  identity.Build = version.Build;
  identity.Revision = version.Revision;

  // The symbol table: the uint8[] payloads of '<msil>SymbolTable' on the
  // Assembly row. AllowMultiple leaves room for one table per triple; this
  // link reads the one for its triple (the first otherwise, which the checks
  // then refuse).
  Expected<SmallVector<ArrayRef<uint8_t>, 1>> payloads =
      reader->getByteArrayAttributes(cli::makeToken(cli::TableId::Assembly, 1),
                                     "", cli::SymbolTableType);
  if (!payloads) {
    error(path + ": " + toString(payloads.takeError()));
    return false;
  }
  if (payloads->empty()) {
    reportMalformed("no symbol table");
    return false;
  }
  std::optional<cli::LibrarySymbolTable> chosen;
  const Triple &target = ctx.arg.target;
  for (ArrayRef<uint8_t> payload : *payloads) {
    Expected<cli::LibrarySymbolTable> t = cli::decodeSymbolTable(payload);
    if (!t) {
      error(path + ": " + toString(t.takeError()));
      return false;
    }
    if (!chosen || (!target.str().empty() &&
                    !hasTripleKey(Triple(chosen->Triple), target) &&
                    hasTripleKey(Triple(t->Triple), target)))
      chosen = std::move(*t);
  }
  table = std::move(*chosen);

  if (isInterface())
    log(path + " is an interface library; deploy the library built from the "
               "same objects");
  return true;
}

bool ManagedLibraryFile::check(const InputFile *model,
                               ArrayRef<ManagedLibraryFile *> libraries) {
  std::string path = toString(this);
  const Triple &target = ctx.arg.target;

  // The toolchain ABI version: an object's record, or, for bitcode, the
  // version of this toolchain, which compiles it.
  if (model) {
    uint32_t abi0 = MSILLink::ToolchainABIVersion;
    if (auto *obj = dyn_cast<ObjFile>(model))
      abi0 = obj->getRecord().Hdr.ABIVersion;
    if (table.ABIVersion != abi0) {
      error(path + ": toolchain ABI version " + Twine(table.ABIVersion) +
            " differs from " + Twine(abi0) + " in " + toString(model));
      return false;
    }
  }

  // The triple key.
  if (!target.str().empty() && !hasTripleKey(Triple(table.Triple), target)) {
    error(path + ": target triple " + table.Triple + " is incompatible with " +
          target.str());
    return false;
  }

  // The data model: an object's whole header; a bitcode input gives only
  // the pointer and long sizes, from its triple, which has the codegen
  // triple's key (plan 5.8.9).
  if (model) {
    MSILLink::Header h0;
    bool whole = false;
    if (auto *obj = dyn_cast<ObjFile>(model)) {
      h0 = obj->getRecord().Hdr;
      whole = true;
    } else {
      MSILLink::setDataModel(h0, target, target.isArch64Bit() ? 8 : 4,
                             target.getDefaultWCharSize());
    }
    if (table.PointerSize != h0.PointerSize || table.LongSize != h0.LongSize ||
        (whole && (table.WCharSize != h0.WCharSize ||
                   table.LongDoubleSize != h0.LongDoubleSize))) {
      error(path + ": data model " +
            getDataModelString(table.PointerSize, table.LongSize,
                               table.WCharSize, table.LongDoubleSize) +
            " differs from " +
            getDataModelString(h0.PointerSize, h0.LongSize, h0.WCharSize,
                               h0.LongDoubleSize) +
            " in " + toString(model));
      return false;
    }
  }

  // The C runtime (exports-multiasm 7.2): none matches anything.
  std::string crt0 = getCRuntimeIdentity();
  if (table.CRuntime != "none" && crt0 != "none" && table.CRuntime != crt0) {
    error(path + ": C runtime '" + table.CRuntime + "' differs from '" + crt0 +
          "' of this link");
    return false;
  }

  if (!readExports())
    return false;
  checkAssemblyRefs(libraries);
  // A bitcode model gave the pointer and long sizes only: the LTO objects'
  // headers give the rest (checkLTODataModel).
  needsLTODataModel = model && !isa<ObjFile>(model);
  return true;
}

bool ManagedLibraryFile::checkLTODataModel(const ObjFile &lto) {
  if (!needsLTODataModel)
    return true;
  needsLTODataModel = false;
  const MSILLink::Header &h0 = lto.getRecord().Hdr;
  if (table.WCharSize == h0.WCharSize &&
      table.LongDoubleSize == h0.LongDoubleSize)
    return true;
  error(toString(this) + ": data model " +
        getDataModelString(table.PointerSize, table.LongSize, table.WCharSize,
                           table.LongDoubleSize) +
        " differs from " +
        getDataModelString(h0.PointerSize, h0.LongSize, h0.WCharSize,
                           h0.LongDoubleSize) +
        " in " + toString(&lto));
  return false;
}

// The symbol map (exports-multiasm 5.4 step 4). The first violation is the
// only one reported.
bool ManagedLibraryFile::readExports() {
  auto readError = [&](Error e) {
    error(toString(this) + ": " + toString(std::move(e)));
    return false;
  };
  auto malformed = [&](const Twine &what) {
    reportMalformed(what);
    return false;
  };
  // Every library has an export class (plan 5.9.15): the writer names the
  // first one in phase 2's 'ldtoken', used or not.
  if (table.ExportClasses.empty())
    return malformed("no export class");
  auto add = [&](ManagedExport *e) {
    if (!exportMap.try_emplace(e->Name, e).second)
      return malformed("symbol '" + e->Name + "' is exported twice");
    exportList.push_back(e);
    return true;
  };
  // The decoded signature of an accessor candidate.
  auto getSignature =
      [&](const cli::MethodDefRow &def) -> Expected<cli::MethodSig> {
    Expected<ArrayRef<uint8_t>> blob = reader->getBlob(def.Signature);
    if (!blob)
      return blob.takeError();
    return cli::decodeMethodSig(*blob);
  };

  // The getters of the classes' properties, by property name.
  struct PropertyGetter {
    uint32_t classIndex;
    uint32_t getter;
  };
  StringMap<PropertyGetter> getters;

  uint32_t numTypes = reader->getRowCount(cli::TableId::TypeDef);
  for (auto [ci, className] : llvm::enumerate(table.ExportClasses)) {
    // A public top-level TypeDef of that full name.
    uint32_t typeRow = 0;
    cli::TypeDefRow typeDef;
    for (uint32_t row = 1; row <= numTypes && !typeRow; ++row) {
      Expected<cli::TypeDefRow> def = reader->getTypeDef(row);
      if (!def)
        return readError(def.takeError());
      if (joinTypeName(def->Namespace, def->Name) != className)
        continue;
      Expected<uint32_t> enclosing =
          reader->getEnclosingType(cli::makeToken(cli::TableId::TypeDef, row));
      if (!enclosing)
        return readError(enclosing.takeError());
      if (*enclosing)
        continue;
      typeRow = row;
      typeDef = *def;
    }
    if (!typeRow)
      return malformed("export class '" + className + "' is not defined");
    if ((typeDef.Flags & cli::TypeAttributes::VisibilityMask) !=
        cli::TypeAttributes::Public)
      return malformed("export class '" + className + "' is not public");
    classNames.push_back({typeDef.Namespace, typeDef.Name});

    // Its public static methods: the accessors, and the functions.
    StringMap<uint32_t> accessors;
    SmallVector<std::pair<cli::MethodDefRow, uint32_t>, 0> functions;
    for (uint32_t row = typeDef.MethodBegin; row < typeDef.MethodEnd; ++row) {
      Expected<cli::MethodDefRow> def = reader->getMethodDef(row);
      if (!def)
        return readError(def.takeError());
      if (!isPublicStatic(def->Flags))
        continue;
      uint32_t token = cli::makeToken(cli::TableId::MethodDef, row);
      StringRef name = def->Name;
      if (!name.starts_with("<msil>")) {
        functions.push_back({*def, token});
        continue;
      }
      if (!name.consume_front(accessorPrefix))
        continue;
      Expected<cli::MethodSig> sig = getSignature(*def);
      if (!sig)
        return malformed("accessor '" + def->Name +
                         "': " + toString(sig.takeError()));
      if (!isAccessorSignature(*sig))
        return malformed("accessor '" + def->Name + "' is not 'native int()'");
      accessors[name] = token;
    }
    for (const auto &[def, token] : functions) {
      auto it = accessors.find(def.Name);
      if (it == accessors.end())
        return malformed("function '" + def.Name + "' has no accessor '" +
                         accessorPrefix + def.Name + "'");
      Expected<ArrayRef<uint8_t>> blob = reader->getBlob(def.Signature);
      if (!blob)
        return readError(blob.takeError());
      Expected<cli::MethodSig> sig = cli::decodeMethodSig(*blob);
      if (!sig)
        return malformed("function '" + def.Name +
                         "': " + toString(sig.takeError()));
      auto *e = make<ManagedExport>();
      e->K = ManagedExport::Function;
      e->Name = def.Name;
      e->File = this;
      e->Class = ci;
      e->MethodDef = token;
      e->Accessor = it->second;
      e->SignatureBlob = *blob;
      e->Signature = std::move(*sig);
      if (!add(e))
        return false;
    }

    // The properties and their getters.
    Expected<std::pair<uint32_t, uint32_t>> range = reader->getPropertyRange(
        cli::makeToken(cli::TableId::TypeDef, typeRow));
    if (!range)
      return readError(range.takeError());
    for (uint32_t row = range->first; row < range->second; ++row) {
      Expected<cli::PropertyRow> prop = reader->getProperty(row);
      if (!prop)
        return readError(prop.takeError());
      Expected<SmallVector<cli::MethodSemanticsRow, 2>> semantics =
          reader->getMethodSemantics(
              cli::makeToken(cli::TableId::Property, row));
      if (!semantics)
        return readError(semantics.takeError());
      for (const cli::MethodSemanticsRow &s : *semantics)
        if (s.Semantics == cli::MethodSemanticsAttributes::Getter &&
            cli::getTokenType(s.Method) ==
                static_cast<uint8_t>(cli::TableId::MethodDef))
          getters.try_emplace(
              prop->Name, PropertyGetter{static_cast<uint32_t>(ci), s.Method});
    }
  }

  // The data symbols: a property of their name whose getter is their
  // accessor.
  for (const cli::LibraryDataSymbol &d : table.DataSymbols) {
    auto it = getters.find(d.Name);
    bool ok = false;
    if (it != getters.end()) {
      Expected<cli::MethodDefRow> def =
          reader->getMethodDef(cli::getTokenRow(it->second.getter));
      if (!def)
        return readError(def.takeError());
      Expected<cli::MethodSig> sig = getSignature(*def);
      if (!sig)
        return readError(sig.takeError());
      ok = isPublicStatic(def->Flags) && isAccessorSignature(*sig) &&
           def->Name.starts_with(accessorPrefix) &&
           def->Name.drop_front(accessorPrefix.size()) == d.Name;
    }
    if (!ok)
      return malformed("data symbol '" + d.Name +
                       "' has no property whose getter is '" + accessorPrefix +
                       d.Name + "'");
    auto *e = make<ManagedExport>();
    e->K = ManagedExport::Data;
    e->Name = d.Name;
    e->File = this;
    e->Class = it->second.classIndex;
    e->Accessor = it->second.getter;
    e->Flags = d.Flags;
    e->Size = d.Size;
    e->AlignLog2 = d.AlignLog2;
    if (!add(e))
      return false;
  }

  // The flagged functions (weak, variadic, with an ABI descriptor) must be
  // functions of the classes.
  for (const cli::LibraryFunctionFlags &f : table.Functions) {
    auto it = exportMap.find(f.Name);
    if (it == exportMap.end() || it->second->isData())
      return malformed("function '" + f.Name +
                       "' of the symbol table is not a method of an export "
                       "class");
    ManagedExport &e = *it->second;
    e.Flags = f.Flags;
    if (f.Flags & cli::LibraryFunctionFlags::ABI)
      e.ABI = f.Signature;
  }
  return true;
}

// Writer.cpp's getAssemblyName.
std::string getOutputAssemblyName() {
  if (!ctx.arg.assemblyName.empty())
    return ctx.arg.assemblyName.str();
  StringRef stem = sys::path::stem(ctx.arg.outputFile);
  if (stem.empty())
    stem = sys::path::filename(ctx.arg.outputFile);
  return stem.str();
}

// The library's own AssemblyRefs (bcl-binding 5.9, plan 5.11.9's M22
// texts).
void ManagedLibraryFile::checkAssemblyRefs(
    ArrayRef<ManagedLibraryFile *> libraries) {
  std::string path = toString(this);
  std::string output = getOutputAssemblyName();
  uint32_t n = reader->getRowCount(cli::TableId::AssemblyRef);
  for (uint32_t row = 1; row <= n; ++row) {
    Expected<cli::AssemblyRefRow> ref = reader->getAssemblyRef(row);
    if (!ref) {
      error(path + ": " + toString(ref.takeError()));
      return;
    }
    StringRef name = ref->Name;
    // Another library of this link: its own checks cover it.
    if (any_of(libraries, [&](const ManagedLibraryFile *lib) {
          return StringRef(lib->getIdentity().Name).equals_insensitive(name);
        }))
      continue;
    // The output itself (the other half of a cycle): a library of this link
    // in spirit.
    if (name.equals_insensitive(output))
      continue;

    // Without references the built-in table binds mscorlib 4.0.0.0 (plan
    // 5.11.7).
    std::optional<cli::AssemblyVersion> refVersion;
    if (ctx.references->empty()) {
      if (name.equals_insensitive("mscorlib"))
        refVersion = cli::AssemblyVersion{4, 0, 0, 0};
    } else if (const ReferenceAssemblyFile *file =
                   ctx.references->findAssembly(name)) {
      const AssemblyIdentity &id = file->getIdentity();
      refVersion =
          cli::AssemblyVersion{id.Major, id.Minor, id.Build, id.Revision};
    }
    if (!refVersion) {
      warn(path + " references " + name +
           ", which is not among the references");
      continue;
    }
    if (isNewer(ref->Version, *refVersion))
      error(path + " references " + name + " " +
            getVersionString(ref->Version) + ", newer than the reference " +
            getVersionString(*refVersion));
  }
}

const ManagedExport *ManagedLibraryFile::findExport(StringRef name) const {
  return exportMap.lookup(name);
}

uint32_t ManagedLibraryFile::getAssemblyRef(cli::MetadataBuilder &mb) {
  // Flags 0 and no key or token, whatever the library's Assembly row says
  // (exports-multiasm 3: a token makes Framework refuse the library).
  cli::AssemblyRefInfo info;
  info.Name = identity.Name;
  info.Version = version;
  info.Culture = identity.Culture;
  return mb.addAssemblyRef(info).Value;
}

uint32_t ManagedLibraryFile::getExportClassTypeRef(cli::MetadataBuilder &mb,
                                                   uint32_t classIndex) {
  assert(classIndex < table.ExportClasses.size() &&
         "check() rejects a symbol table without that export class");
  StringRef ns, name;
  if (classIndex < classNames.size() && !classNames[classIndex].second.empty())
    std::tie(ns, name) = classNames[classIndex];
  else if (classIndex < table.ExportClasses.size())
    std::tie(ns, name) = splitTypeName(table.ExportClasses[classIndex]);
  return mb.addTypeRef(cli::EntityHandle{getAssemblyRef(mb)}, ns, name).Value;
}

Expected<uint32_t> ManagedLibraryFile::mapTypeToken(uint32_t token,
                                                    cli::MetadataBuilder &mb,
                                                    BCLBinder *binder) {
  uint8_t type = cli::getTokenType(token);
  // A type of the library itself: [lib]Ns.Name, nested through its
  // enclosing types' TypeRefs.
  if (type == static_cast<uint8_t>(cli::TableId::TypeDef)) {
    SmallVector<uint32_t, 4> chain{token};
    for (unsigned depth = 0;; ++depth) {
      if (depth > maxTypeNestingDepth)
        return createStringError("type nesting too deep");
      Expected<uint32_t> enclosing = reader->getEnclosingType(chain.back());
      if (!enclosing)
        return enclosing.takeError();
      if (!*enclosing)
        break;
      chain.push_back(*enclosing);
    }
    cli::EntityHandle scope{getAssemblyRef(mb)};
    for (uint32_t t : llvm::reverse(chain)) {
      Expected<cli::TypeDefRow> def = reader->getTypeDef(cli::getTokenRow(t));
      if (!def)
        return def.takeError();
      scope = mb.addTypeRef(scope, def->Namespace, def->Name);
    }
    return scope.Value;
  }
  if (type == static_cast<uint8_t>(cli::TableId::TypeRef)) {
    Expected<std::pair<std::string, std::string>> name =
        getIndexTypeName(*reader, token);
    if (!name)
      return name.takeError();
    if (!binder)
      return createStringError("type '" +
                               joinTypeName(name->first, name->second) +
                               "' needs the BCL binder");
    Expected<cli::EntityHandle> h = binder->bindType(name->first, name->second);
    if (!h)
      return h.takeError();
    return h->Value;
  }
  return createStringError("token 0x" + utohexstr(token, false, 8) +
                           " is a type specification");
}

Expected<cli::MethodSig> ManagedLibraryFile::getOutputSignature(
    const ManagedExport &e, cli::MetadataBuilder &mb, BCLBinder *binder) {
  std::optional<std::string> failure;
  cli::MethodSig sig =
      cli::mapTypeRefs(e.Signature, [&](uint32_t token) -> uint32_t {
        if (failure)
          return token;
        Expected<uint32_t> t = mapTypeToken(token, mb, binder);
        if (!t) {
          failure = toString(t.takeError());
          return token;
        }
        return *t;
      });
  if (failure)
    return createStringError(toString(this) + ": the signature of '" + e.Name +
                             "': " + *failure);
  return sig;
}

Expected<uint32_t> ManagedLibraryFile::getMemberRef(const ManagedExport &e,
                                                    bool accessor,
                                                    cli::MetadataBuilder &mb,
                                                    BCLBinder *binder) {
  cli::EntityHandle parent{getExportClassTypeRef(mb, e.Class)};
  if (accessor) {
    static constexpr uint8_t accessorSig[] = {cli::SIG_DEFAULT, 0,
                                              cli::ELEMENT_TYPE_I};
    return mb
        .addMemberRef(parent, (accessorPrefix + e.Name).str(),
                      mb.addBlob(accessorSig))
        .Value;
  }
  assert(!e.isData() && "only functions are called");
  Expected<cli::MethodSig> sig = getOutputSignature(e, mb, binder);
  if (!sig)
    return sig.takeError();
  SmallVector<uint8_t, 16> blob;
  cli::encodeMethodSig(*sig, blob);
  return mb.addMemberRef(parent, e.Name, mb.addBlob(blob)).Value;
}

InputFile *createManagedLibraryFile(MemoryBufferRef mb) {
  Expected<std::unique_ptr<cli::MetadataReader>> reader =
      cli::MetadataReader::create(getBytes(mb));
  if (!reader) {
    error(getDisplayPath(mb.getBufferIdentifier()) + ": " +
          toString(reader.takeError()));
    return nullptr;
  }
  return make<ManagedLibraryFile>(mb, std::move(*reader));
}
} // namespace lld::msil
