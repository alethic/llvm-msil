//===- llvm/CLI/BCLMembers.h - BCL entities the toolchain uses --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The base class library types and members of BCLMembers.def as a table
// (ImplementationPlan.md 5.7.2, 5.11.7), and their ILAsm spelling. Header
// only: the MSIL target, msil-ld's trailer builder and its reference binder
// share it.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_BCLMEMBERS_H
#define LLVM_CLI_BCLMEMBERS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace llvm {
namespace cli {

/// One entry of BCLMembers.def, a type or a method, in file order.
enum class BCLMemberId : uint16_t {
#define CLI_BCL_TYPE(Id, Namespace, Name, MscorlibSafe) Id,
#define CLI_BCL_METHOD(Id, OwnerType, Name, HasThis, Return, Params, Strength, \
                       Fallback, MscorlibSafe)                                 \
  Id,
#include "llvm/CLI/BCLMembers.def"
};

/// What a link does when the references do not define a member (plan
/// 5.11.5): an error (Required), omit what needs it (OptionalDrop), or call
/// the entry's fallback helper instead (OptionalSubstitute).
enum class BCLStrength : uint8_t { Required, OptionalDrop, OptionalSubstitute };

/// The scope that whole-program text (-S, llc and msil-ld's --lto-emit-asm)
/// gives every BCL reference (plan 5.11.8): the assembly of msil-ld's
/// built-in table (plan 5.11.7, decision D10).
inline constexpr StringLiteral MscorlibAssembly = "mscorlib";

/// One BCLMembers.def entry.
struct BCLMemberInfo {
  enum class EntityKind : uint8_t { Type, Method };

  BCLMemberId Id;
  /// The enumerator's name, e.g. "Marshal_AllocHGlobal".
  StringLiteral IdName;
  EntityKind Kind;
  /// A type's namespace ("" for none); "" for a method (see Owner).
  StringLiteral Namespace;
  /// A type's name (a nested type as "Outer/Inner") or a method's name.
  StringLiteral Name;
  /// The type that declares a method; a type's own Id.
  BCLMemberId Owner;
  /// An instance method.
  bool HasThis;
  /// A method's return type and parameter types (joined by ", ") as
  /// canonical listing v1 text; "" for a type.
  StringLiteral Return;
  StringLiteral Params;
  /// Types are Required.
  BCLStrength Strength;
  /// The helper symbol of an OptionalSubstitute member; "" otherwise.
  StringLiteral Fallback;
  /// Present in mscorlib 4.0.0.0 on every runtime: part of msil-ld's
  /// built-in [mscorlib] table.
  bool MscorlibSafe;

  bool isType() const { return Kind == EntityKind::Type; }
  bool isMethod() const { return Kind == EntityKind::Method; }
};

/// Every entry of BCLMembers.def, indexed by BCLMemberId.
inline ArrayRef<BCLMemberInfo> getAllBCLMembers() {
  static constexpr BCLMemberInfo Members[] = {
#define CLI_BCL_TYPE(Id, Namespace, Name, MscorlibSafe)                        \
  {BCLMemberId::Id,                                                            \
   #Id,                                                                        \
   BCLMemberInfo::EntityKind::Type,                                            \
   Namespace,                                                                  \
   Name,                                                                       \
   BCLMemberId::Id,                                                            \
   false,                                                                      \
   "",                                                                         \
   "",                                                                         \
   BCLStrength::Required,                                                      \
   "",                                                                         \
   MscorlibSafe},
#define CLI_BCL_METHOD(Id, OwnerType, Name, HasThis, Return, Params, Strength, \
                       Fallback, MscorlibSafe)                                 \
  {BCLMemberId::Id,                                                            \
   #Id,                                                                        \
   BCLMemberInfo::EntityKind::Method,                                          \
   "",                                                                         \
   Name,                                                                       \
   BCLMemberId::OwnerType,                                                     \
   HasThis,                                                                    \
   Return,                                                                     \
   Params,                                                                     \
   BCLStrength::Strength,                                                      \
   Fallback,                                                                   \
   MscorlibSafe},
#include "llvm/CLI/BCLMembers.def"
  };
  return Members;
}

/// The entry \p Id.
inline const BCLMemberInfo &getBCLMemberInfo(BCLMemberId Id) {
  ArrayRef<BCLMemberInfo> Members = getAllBCLMembers();
  size_t Index = static_cast<size_t>(Id);
  assert(Index < Members.size() && Members[Index].Id == Id &&
         "unknown BCL member");
  return Members[Index];
}

/// Prints the ILAsm reference to \p Id in assembly \p Scope (no scope when it
/// is empty): a type as "[<Scope>]<Namespace>.<Name>", a method as
/// "[instance ]<Return> [<Scope>]<Namespace>.<Type>::<Name>(<Params>)", for
/// example "void [mscorlib]System.Threading.Interlocked::MemoryBarrier()".
inline void printBCLMemberRef(raw_ostream &OS, BCLMemberId Id,
                              StringRef Scope) {
  auto PrintType = [&](const BCLMemberInfo &Type) {
    assert(Type.isType() && "a method's owner is a type");
    if (!Scope.empty())
      OS << '[' << Scope << ']';
    if (!Type.Namespace.empty())
      OS << Type.Namespace << '.';
    OS << Type.Name;
  };
  const BCLMemberInfo &Member = getBCLMemberInfo(Id);
  if (Member.isType()) {
    PrintType(Member);
    return;
  }
  if (Member.HasThis)
    OS << "instance ";
  OS << Member.Return << ' ';
  PrintType(getBCLMemberInfo(Member.Owner));
  OS << "::" << Member.Name << '(' << Member.Params << ')';
}

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_BCLMEMBERS_H
