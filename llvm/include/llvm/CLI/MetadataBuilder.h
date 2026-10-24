//===- llvm/CLI/MetadataBuilder.h - ECMA-335 metadata writer ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares MetadataBuilder, which collects the rows of one module's
// metadata tables and heaps and writes the metadata root (II.24.2) of a PE
// image or a CIL object.
//
// Rows and handles:
// - the stable tables (AssemblyRef, ModuleRef, TypeRef, TypeDef, TypeSpec,
//   MemberRef, StandAloneSig and MethodSpec) number their rows at insertion,
//   so their EntityHandle is the final token at once; every table but TypeDef
//   merges a row that has the same content as an earlier one, so rows are in
//   first-add order (5.8.3 R6) and a signature blob that names them is final
//   when it is added;
// - the member tables (Field, MethodDef, Param and Property) give provisional
//   handles; finalize() groups their rows into runs by owner (TypeDefs, and
//   MethodDefs for Param), in insertion order within an owner, and numbers
//   them, after which getToken() and resolveToken() give their tokens;
// - finalize() writes the implicit PropertyMap and NestedClass rows, and
//   stable-sorts the sorted tables (ClassLayout, CustomAttribute, FieldLayout,
//   FieldRVA, ImplMap, MethodSemantics, NestedClass) on their keys as encoded.
//
// The 0xFFFF padding rule: if Field, MethodDef or Param would have exactly
// 0xFFFF rows, finalize() appends inert rows owned by the last TypeDef until
// none of them has: a field '<msil>pad' (private static int32), a method
// '<msil>pad' (private static hidebysig void()) whose body is a tiny 'ret'
// added to the MethodBodyStream once and shared by every pad method, and a
// sequence-0 Param owned by the last pad method (one is added first if there
// is none). The runtimes reject a 2-byte list column that would have to hold
// 0x10000. The last TypeDef must be a class, not an interface.
//
// Content checks are recorded when a row is added: a NUL byte in a name, a
// name longer than MaxIdentifierBytes (TypeDef and TypeRef names count as
// Namespace.Name), a table that would pass MaxTableRows (the add then gives a
// nil handle), a blob longer than MaxCompressedUInt, and an invalid #US
// string. finalize() returns the first one as an Error with the prefix
// "cannot write CLI metadata: " that names the entity; without one, it checks
// the methods and fields of each type, pad rows included, against
// MaxMethodsPerType and MaxFieldsPerType. Misuse of the API (a handle of the
// wrong table, an add after finalize(), a member token before it, serializing
// after a failed finalize()) is an assertion.
//
// The serialized bytes depend only on the calls and their order: hash maps
// serve only lookups, and #Strings is a StringTableBuilder, whose layout
// depends only on the strings.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_METADATABUILDER_H
#define LLVM_CLI_METADATABUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace llvm {
namespace cli {

/// The identity of the assembly that a module defines (its Assembly row).
struct AssemblyIdentity {
  std::string Name;
  AssemblyVersion Version;
  AssemblyFlags Flags{};
  AssemblyHashAlgorithm HashAlg = AssemblyHashAlgorithm::SHA1;
  SmallVector<uint8_t, 0> PublicKey;
  std::string Culture;
};

/// The identity of a referenced assembly (an AssemblyRef row).
struct AssemblyRefInfo {
  std::string Name;
  AssemblyVersion Version;
  AssemblyFlags Flags{};
  SmallVector<uint8_t, 0> PublicKeyOrToken;
  std::string Culture;
  SmallVector<uint8_t, 0> HashValue;
};

/// The metadata of one module: its tables and heaps, and their serialization
/// as a metadata root with the streams #~, #Strings, #US, #GUID and #Blob.
class MetadataBuilder {
public:
  /// Creates the Module row, #GUID entry 1 (the MVID, zero until the PE
  /// writer stamps it) and TypeDef row 1, '<Module>'.
  LLVM_ABI MetadataBuilder();
  LLVM_ABI ~MetadataBuilder();
  LLVM_ABI MetadataBuilder(MetadataBuilder &&);
  LLVM_ABI MetadataBuilder &operator=(MetadataBuilder &&);

  /// TypeDef row 1, '<Module>'.
  LLVM_ABI EntityHandle getModuleType() const;
  /// The Name of the Module row (the file name of the image or object).
  LLVM_ABI void setModuleName(StringRef Name);
  /// Adds the Assembly row, or replaces it. A netmodule has none.
  LLVM_ABI void setAssembly(const AssemblyIdentity &Id);
  LLVM_ABI bool hasAssembly() const;

  /// Adds a blob and returns its #Blob offset, which is final at once.
  LLVM_ABI uint32_t addBlob(ArrayRef<uint8_t> Bytes);
  /// Adds an ldstr string and returns its token, 0x70000000 | #US offset.
  /// An invalid string is a content check (and gives 0x70000000).
  LLVM_ABI uint32_t addUserString(StringRef UTF8);

  // Stable tables: the handle is the final token. Every table but TypeDef
  // returns the existing row for a row with the same content: AssemblyRef
  // with every field equal, ModuleRef with the same name (case-sensitive),
  // TypeRef with the same scope, namespace and name, TypeSpec and
  // StandAloneSig with the same blob, MemberRef with the same parent, name
  // and signature (5.8.3 R3), MethodSpec with the same method and blob.

  LLVM_ABI EntityHandle addAssemblyRef(const AssemblyRefInfo &R);
  LLVM_ABI EntityHandle addModuleRef(StringRef Name);
  /// \p ResolutionScope is a Module, ModuleRef, AssemblyRef or TypeRef (an
  /// enclosing type) handle, or nil.
  LLVM_ABI EntityHandle addTypeRef(EntityHandle ResolutionScope,
                                   StringRef Namespace, StringRef Name);
  /// \p Extends is a TypeDef, TypeRef or TypeSpec handle, or nil. A nested
  /// type names its \p EnclosingType, which was added before it, and gets a
  /// NestedClass row.
  LLVM_ABI EntityHandle addTypeDef(TypeAttributes Flags, StringRef Namespace,
                                   StringRef Name, EntityHandle Extends,
                                   EntityHandle EnclosingType = {});
  LLVM_ABI EntityHandle addTypeSpec(uint32_t SigBlob);
  /// \p Parent is a TypeDef, TypeRef, ModuleRef, MethodDef or TypeSpec
  /// handle.
  LLVM_ABI EntityHandle addMemberRef(EntityHandle Parent, StringRef Name,
                                     uint32_t SigBlob);
  LLVM_ABI EntityHandle addStandAloneSig(uint32_t SigBlob);
  /// \p Method is a MethodDef or MemberRef handle.
  LLVM_ABI EntityHandle addMethodSpec(EntityHandle Method,
                                      uint32_t InstantiationBlob);

  // Member tables: grouped by owner at finalize(), in insertion order within
  // an owner. The handles are provisional until finalize().

  LLVM_ABI EntityHandle addField(EntityHandle Owner, FieldAttributes Flags,
                                 StringRef Name, uint32_t SigBlob);
  LLVM_ABI EntityHandle addMethod(EntityHandle Owner, MethodAttributes Flags,
                                  MethodImplAttributes ImplFlags,
                                  StringRef Name, uint32_t SigBlob);
  LLVM_ABI EntityHandle addParam(EntityHandle Method, uint16_t Sequence,
                                 ParamAttributes Flags, StringRef Name);
  LLVM_ABI EntityHandle addProperty(EntityHandle Owner,
                                    PropertyAttributes Flags, StringRef Name,
                                    uint32_t SigBlob);

  // Sorted tables: stable-sorted by key at finalize().

  LLVM_ABI void addMethodSemantics(EntityHandle Property,
                                   MethodSemanticsAttributes Semantics,
                                   EntityHandle Method);
  /// \p Method is a MethodDef (or Field) handle; \p ModuleRef the module that
  /// exports \p ImportName.
  LLVM_ABI void addImplMap(EntityHandle Method, PInvokeAttributes Flags,
                           StringRef ImportName, EntityHandle ModuleRef);
  LLVM_ABI void addClassLayout(EntityHandle TypeDef, uint16_t PackingSize,
                               uint32_t ClassSize);
  LLVM_ABI void addFieldLayout(EntityHandle Field, uint32_t Offset);
  /// The field's RVA is the mapped-data RVA that serialize() is given plus
  /// \p MappedDataOffset.
  LLVM_ABI void addFieldRVA(EntityHandle Field, uint32_t MappedDataOffset);
  /// \p Parent is any HasCustomAttribute row (the Module row is
  /// makeToken(TableId::Module, 1) and the Assembly row
  /// makeToken(TableId::Assembly, 1)); \p Constructor a MethodDef or MemberRef.
  LLVM_ABI void addCustomAttribute(EntityHandle Parent,
                                   EntityHandle Constructor,
                                   uint32_t ValueBlob);
  /// The offset of the method's body in the IL stream (the MethodDef's RVA
  /// is the IL stream RVA plus this offset). Allowed before and after
  /// finalize(), because bodies that use member tokens can be encoded only
  /// after finalize() has numbered the member rows.
  LLVM_ABI void setMethodBodyOffset(EntityHandle Method,
                                    uint32_t ILStreamOffset);

  /// Applies the 0xFFFF padding rule (the pad body goes to \p Bodies),
  /// numbers the member rows, writes the implicit PropertyMap and NestedClass
  /// rows, stable-sorts the sorted tables on their encoded keys, lays out
  /// #Strings and the metadata, and returns the first content check as an
  /// Error. Nothing may be added afterwards; after an Error the builder can
  /// only be destroyed. Body offsets are not checked (checkMethodBodies()).
  LLVM_ABI Error finalize(MethodBodyStream &Bodies);
  /// After a successful finalize(): an Error "method '<owner>::<name>' has no
  /// body" for the first method (in row order) without a body offset that
  /// is not abstract, not pinvokeimpl and whose code type is not runtime.
  LLVM_ABI Error checkMethodBodies() const;
  LLVM_ABI bool isFinalized() const;

  /// The token of a row: at once for the stable tables, the Module row and
  /// the Assembly row; after finalize() for the member tables.
  LLVM_ABI uint32_t getToken(EntityHandle H) const;
  /// Token: the token itself; Handle: getToken(). None, External (a client
  /// that uses External refs resolves them itself), a handle that names no
  /// row, and a member handle before a successful finalize() give an Error.
  LLVM_ABI Expected<uint32_t> resolveToken(TokenRef T) const;
  /// The number of rows of table \p T, the implicit PropertyMap and
  /// NestedClass rows included; after finalize(), the pad rows too. 0 for
  /// the tables the builder does not write.
  LLVM_ABI uint32_t getRowCount(TableId T) const;
  /// After finalize(): the size of the serialized metadata, which does not
  /// depend on the RVAs (the RVA columns are 4 bytes wide).
  LLVM_ABI size_t getSerializedSize() const;
  /// After finalize(): the offset of the MVID (#GUID entry 1) in the
  /// serialized metadata.
  LLVM_ABI size_t getMvidOffset() const;
  /// After finalize(): writes the metadata to the first getSerializedSize()
  /// bytes of \p Out. A MethodDef's RVA is \p ILStreamRVA plus its body
  /// offset (0 without a body); a FieldRVA's RVA is \p MappedDataRVA plus its
  /// offset.
  LLVM_ABI void serialize(MutableArrayRef<uint8_t> Out, uint32_t ILStreamRVA,
                          uint32_t MappedDataRVA) const;

private:
  struct Impl;
  std::unique_ptr<Impl> I;
};

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_METADATABUILDER_H
