//===- BCLBinder.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BCL reference binding (llvm/docs/MSIL/ImplementationPlan.md 5.11): the
// binder turns requests for .NET types and members (TypeRefs and MemberRefs
// on the objects' [msil.bcl] placeholder scope, and the members that
// msil-ld's own synthesized code needs) into rows of the output's metadata,
// bound by definition against the link's reference assemblies (ReferenceSet,
// plan 5.11.3-5.11.6), or, in a link without references, against the
// built-in [mscorlib] table of the mscorlib-safe BCLMembers.def entries (plan
// 5.11.7, decision D10).
//
// The declarations are plan 5.11.5's, pinned so that the driver and the
// writer compile against them; BCLBinder.cpp defines BCLBinder::Impl and every
// member.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_BCLBINDER_H
#define LLD_MSIL_BCLBINDER_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/Signature.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace llvm {
class raw_ostream;
namespace cli {
class MetadataBuilder;
} // namespace cli
} // namespace llvm

namespace lld::msil {
class InputFile;
class ReferenceSet;

// What a link does when the references do not define a requested member
// (plan 5.11.5, Q11): an error (Required), omit what needs it (OptionalDrop),
// or call the entry's fallback helper instead (OptionalSubstitute).
enum class RequestStrength : uint8_t {
  Required,
  OptionalDrop,
  OptionalSubstitute
}; // cli::BCLStrength's values
static_assert(
    static_cast<uint8_t>(RequestStrength::Required) ==
            static_cast<uint8_t>(llvm::cli::BCLStrength::Required) &&
        static_cast<uint8_t>(RequestStrength::OptionalDrop) ==
            static_cast<uint8_t>(llvm::cli::BCLStrength::OptionalDrop) &&
        static_cast<uint8_t>(RequestStrength::OptionalSubstitute) ==
            static_cast<uint8_t>(llvm::cli::BCLStrength::OptionalSubstitute),
    "RequestStrength has cli::BCLStrength's values");

// One request for a .NET entity (plan 5.11.5).
struct BCLRequest {
  enum Kind : uint8_t {
    Type,
    Method,
    Field,
    TypeSpec,
    MethodSpec,
    AttributeCtor,
    TypeName
  } K = Type;
  RequestStrength Strength = RequestStrength::Required;
  // The codegen switch that made the request, for diagnostics; empty by
  // default.
  StringRef Switch;
  // Null for msil-ld's own synthesized code.
  const InputFile *RequestedBy = nullptr;
  // Name nested as Outer/Inner; a TypeName request holds the whole string in
  // Name.
  StringRef Namespace, Name;
  StringRef Member;
  // Methods, attribute constructors.
  std::optional<llvm::cli::MethodSig> MethodSignature;
  // Fields, type specs.
  std::optional<llvm::cli::TypeSig> FieldOrSpec;
  // Method specs.
  SmallVector<llvm::cli::TypeSig, 0> Args;
  // TypeRef values in these signatures are output TypeRef, TypeDef or
  // TypeSpec tokens (EntityHandle::Value, final for those tables, plan
  // 5.7.2), such as the binder returned for earlier requests.
};

// What msil-ld's checks of managed calls need to know of a type of the
// references (plan 5.20.7 MK2-MK4).
enum class BCLTypeKind : uint8_t {
  // A class or an interface, System.ValueType and System.Enum included.
  Reference,
  // A value type that is not an enum: its base type is System.ValueType.
  ValueType,
  // An enum: its base type is System.Enum.
  Enum,
};

class BCLBinder {
public:
  /// Binds against Refs, or against the built-in [mscorlib] table when
  /// Refs.empty() (5.11.7).
  BCLBinder(const ReferenceSet &Refs, llvm::cli::MetadataBuilder &Out);
  ~BCLBinder();
  /// The bound output entity; std::nullopt when an optional request falls
  /// back.
  Expected<std::optional<llvm::cli::EntityHandle>> bind(const BCLRequest &R);
  /// Capability query without emitting rows (the data-import helper's
  /// choice, 5.10.12).
  bool isDefined(const BCLRequest &R);
  Expected<llvm::cli::EntityHandle> bindType(StringRef Ns, StringRef Name);
  /// The trailer sink's getBCLMember (5.9.10).
  Expected<llvm::cli::EntityHandle> bindMember(llvm::cli::BCLMemberId Id);
  Error rewriteAttributeBlob(const llvm::cli::MethodSig &Ctor,
                             ArrayRef<uint8_t> In,
                             llvm::SmallVectorImpl<uint8_t> &Out);
  /// --print-bcl-binding.
  void printBinding(StringRef Request, llvm::raw_ostream &OS);

  // The type queries of msil-ld's managed-call checks (plan 5.20.7 MK2-MK4,
  // MK7).
  // They resolve against the references and emit no row. A type that does
  // not resolve gives the binder's error for it (not defined, defined twice,
  // forwarded to a missing assembly), and every query is an Error in a link
  // without references, whose built-in table records no type's kind.

  /// True if the link binds against references, false for the built-in
  /// [mscorlib] table (5.11.7).
  bool hasReferences() const;
  /// The kind of the type \p Ns.\p Name (a nested type's Name as
  /// 'Outer/Inner'), from its TypeDef's flags and base type.
  Expected<BCLTypeKind> getTypeKind(StringRef Ns, StringRef Name);
  /// The underlying type of the enum \p Ns.\p Name: the element type of its
  /// instance field value__ (ELEMENT_TYPE_BOOLEAN to ELEMENT_TYPE_U8,
  /// ELEMENT_TYPE_I or ELEMENT_TYPE_U). An Error if the type is not an enum.
  Expected<llvm::cli::ElementType> getEnumUnderlyingType(StringRef Ns,
                                                         StringRef Name);
  /// The full names ("Ns.Name", nested parts 'Outer/Inner') of the base types
  /// of \p Ns.\p Name, nearest first, through every reference of the set:
  /// for a class, up to System.Object. A generic base type counts as its
  /// generic type; the walk ends at a base that is neither a TypeDef, a
  /// TypeRef nor a generic instance of one.
  Expected<SmallVector<std::string, 4>> getBaseTypes(StringRef Ns,
                                                     StringRef Name);
  /// The flags of the public field \p Field that \p Ns.\p Name itself
  /// declares (a field request binds in that type only, 5.11.4): MK7 reads
  /// its Literal flag. With several public fields of that name, which only
  /// hand-written IL declares, the first one's. An Error if the type does
  /// not resolve or declares no such public field (binding the member
  /// reports that).
  Expected<llvm::cli::FieldAttributes>
  getFieldFlags(StringRef Ns, StringRef Name, StringRef Field);
  /// The full name of the type that declares the method that the Method
  /// request \p R binds to (plan 5.20.3 MS5, 5.20.5 ML2 (2)): R's own type,
  /// spelled as R spells it, when that type declares the method, else the
  /// base type in which the member search (5.11.4) found it. In a link
  /// without references, R's own type when the built-in table has the row
  /// (its rows are declared by their owners). The request's signature holds
  /// output tokens of this binder, as for bind(). An Error when the request
  /// does not bind, with the text bind() would give; no row is emitted.
  Expected<std::string> getDeclaringType(const BCLRequest &R);

private:
  struct Impl; // defined in BCLBinder.cpp
  std::unique_ptr<Impl> I;
};

/// --print-bcl-table (5.11.1).
void printBuiltinBCLTable(llvm::raw_ostream &OS);
} // namespace lld::msil

#endif
