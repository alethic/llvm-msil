//===- AssemblyFiles.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AssemblyFiles.h"
#include "InputFiles.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CLI/SymbolTable.h"

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

InputFile *createManagedLibraryFile(MemoryBufferRef mb) {
  error(getDisplayPath(mb.getBufferIdentifier()) +
        ": linking .NET libraries built by this toolchain is not supported "
        "yet");
  return nullptr;
}
} // namespace lld::msil
