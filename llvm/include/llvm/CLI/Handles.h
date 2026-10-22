//===- llvm/CLI/Handles.h - ECMA-335 entity handles and tokens --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the two small value types that the ECMA-335 library
// uses to name metadata rows before and after they get their final tokens:
// EntityHandle, a row of a MetadataBuilder, and TokenRef, the token operand
// of an instruction before it is encoded. Header-only.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_HANDLES_H
#define LLVM_CLI_HANDLES_H

#include "llvm/BinaryFormat/CLI.h"
#include <cstdint>

namespace llvm {
namespace cli {

/// A row of a MetadataBuilder, token-shaped: TableId << 24 | Index, where
/// Index 0 is nil (a default-constructed handle is nil).
///
/// The stable tables number their rows at insertion, so their handle *is* the
/// final token: TypeRef, TypeDef, TypeSpec, MemberRef, StandAloneSig,
/// ModuleRef, AssemblyRef and MethodSpec. The member tables are numbered by
/// MetadataBuilder::finalize(), so their handle is provisional: Field,
/// MethodDef, Param and Property.
struct EntityHandle {
  uint32_t Value = 0;

  TableId getTable() const { return static_cast<TableId>(getTokenType(Value)); }
  uint32_t getIndex() const { return getTokenRow(Value); }
  bool isNil() const { return getIndex() == 0; }

  friend bool operator==(EntityHandle L, EntityHandle R) {
    return L.Value == R.Value;
  }
  friend bool operator!=(EntityHandle L, EntityHandle R) {
    return L.Value != R.Value;
  }
};

/// A token operand before encoding: nothing, a final token, a MetadataBuilder
/// handle (MetadataBuilder::resolveToken turns it into a token), or a client
/// id such as an MCSymbol index, which the client resolves itself.
class TokenRef {
public:
  enum class Kind : uint8_t { None, Token, Handle, External };

  /// A TokenRef of kind None.
  TokenRef() = default;

  /// A final token.
  static TokenRef getToken(uint32_t Token) {
    return TokenRef(Kind::Token, Token);
  }
  /// A handle that MetadataBuilder::resolveToken resolves.
  static TokenRef getHandle(EntityHandle H) {
    return TokenRef(Kind::Handle, H.Value);
  }
  /// A client symbol (e.g. an MCSymbol index) that the client resolves.
  static TokenRef getExternal(uint32_t Id) {
    return TokenRef(Kind::External, Id);
  }

  Kind getKind() const { return K; }
  /// The token, the handle's Value or the client id; 0 for None.
  uint32_t getValue() const { return Value; }

  friend bool operator==(const TokenRef &L, const TokenRef &R) {
    return L.K == R.K && L.Value == R.Value;
  }
  friend bool operator!=(const TokenRef &L, const TokenRef &R) {
    return !(L == R);
  }

private:
  TokenRef(Kind K, uint32_t Value) : Value(Value), K(K) {}

  uint32_t Value = 0;
  Kind K = Kind::None;
};

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_HANDLES_H
