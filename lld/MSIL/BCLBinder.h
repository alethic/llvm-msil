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

private:
  struct Impl; // defined in BCLBinder.cpp
  std::unique_ptr<Impl> I;
};

/// --print-bcl-table (5.11.1).
void printBuiltinBCLTable(llvm::raw_ostream &OS);
} // namespace lld::msil

#endif
