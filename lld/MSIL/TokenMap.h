//===- TokenMap.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The IL token walker and the per-object token maps of msil-ld (plan 5.9.7;
// lld/wasm has no counterpart: a CIL object carries no code relocations,
// because the self-describing IL opcode stream finds every token).
//
// The walker decodes every method body of an object with lib/CLI's opcode
// table and records each token operand (offset, token) in the InputMethod.
// The token map then tells, for every token of the object, what it names in
// the output: a symbol (resolved by the row's name, R1), a function address
// field, a BCL row, a stand-alone signature or a user string. The map holds
// symbol table entries, which resolution changes in place, so a lookup
// always gives the final binding once resolution is complete.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_TOKEN_MAP_H
#define LLD_MSIL_TOKEN_MAP_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include <cstdint>
#include <vector>

namespace llvm::cli {
struct MethodSig;
} // namespace llvm::cli

namespace lld::msil {
class InputMethod;
class ObjFile;
class Symbol;

// What a token of a CIL object names (plan 5.9.7's table).
struct TokenTarget {
  enum Kind : uint8_t {
    // Not a row of the object (or the carrier's own rows, which no code
    // names).
    None,
    // A method: a MethodDef with a body, a MethodDef import stub (an ImplMap
    // row), or a MemberRef with a method signature on '<msil>Symbols'. The
    // output token is the method of `sym`: an object's or LTO object's
    // method, an adapter (plan 5.9.8), a trap stub of an undefined weak
    // function or a P/Invoke stub of a natively bound one.
    Method,
    // A data address: a FieldDef, or a MemberRef with a field signature on
    // '<msil>Symbols'. The output token is the address field of `sym`: a
    // defined or common DATA symbol, or a native data import (plan 5.10.12).
    Field,
    // A function address: a MemberRef on '<msil>FnAddr'. The output token is
    // the '<msil>fn_' field of the method of `sym` (one per method; aliases
    // share their target's).
    FnAddr,
    // A MemberRef on another TypeRef, a TypeRef, a TypeSpec or a MethodSpec:
    // what the BCL binder (BCLBinder.h, plan 5.11) makes of it. CIL objects
    // carry no custom attribute that the output copies (the link-info
    // carrier stays behind); one that a later input kind brings has its
    // System.Type arguments rewritten by BCLBinder::rewriteAttributeBlob.
    BCL,
    // A TypeDef: in Phase 2 objects only '<Module>' (row 1).
    TypeDef,
    // A StandAloneSig (a local or calli signature): re-encoded and
    // deduplicated in the output.
    StandAloneSig,
    // An ldstr operand (token type 0x70): the string is re-added to the
    // output's #US heap.
    UserString,
  };

  // How a Method, Field or FnAddr token names its symbol.
  enum Form : uint8_t {
    NoForm,
    // A MethodDef with a body or a FieldDef: the object's own definition
    // (R1). It may still resolve to another input's definition: a losing
    // WEAK copy, a discarded COMDAT member, or a common.
    Definition,
    // A body-less MethodDef with an ImplMap row: an import stub. Its
    // signature is the call side; the symbol's import attributes are the
    // symbol table's (SymbolTable::isExplicitImport, ObjFile::getImport).
    Stub,
    // A MemberRef on '<msil>Symbols' or '<msil>FnAddr'.
    Placeholder,
  };

  Kind kind = None;
  Form form = NoForm;
  // The token in the object.
  uint32_t token = 0;
  // Method, Field, FnAddr: the symbol that the row's name resolves to: the
  // object's local symbol of that name, or the symbol table's entry.
  Symbol *sym = nullptr;
  // Method, Field, FnAddr: the SYMTAB index of that name in the object.
  int32_t symbolIndex = -1;
  // Method with form Definition: the object's own method of the row.
  InputMethod *method = nullptr;
  // Method (every form) and BCL MemberRefs with a method signature: the
  // signature of the row, as the object encodes it. For a site that the
  // writer binds by name to a definition (plan 5.9.8) it is the call side.
  const llvm::cli::MethodSig *signature = nullptr;
  // Method: the row's SIGNATURES entry, or null.
  const llvm::MSILLink::SignatureInfo *signatureInfo = nullptr;
  // BCL: the name of the AssemblyRef that scopes the row's TypeRef: msil.bcl
  // in llc's objects; the binder binds any name but the placeholders' by the
  // type's name (plan 5.11.5).
  StringRef scope;
  // StandAloneSig: the signature blob.
  ArrayRef<uint8_t> blob;

  // True for a Method token in Definition form whose symbol did not resolve
  // to the object's own method: a call site that the writer checks against
  // the winning definition (plan 5.9.8). Valid after resolution.
  bool isRedirected() const;
};

// The token map of one CIL object.
class TokenMap {
public:
  // Builds the map of \p file once its symbols are in the symbol table
  // (ObjFile::parse). Reports a malformed row (a signature that does not
  // decode) and returns false.
  bool build(ObjFile &file);

  // What \p token, a token of the object's metadata or an ldstr operand,
  // names. A token that names no row (or a table the map does not hold)
  // gives Kind None.
  TokenTarget lookup(uint32_t token) const;

private:
  // Rows by table, indexed by row - 1.
  std::vector<TokenTarget> methodDefs, fields, memberRefs, typeRefs, typeDefs,
      typeSpecs, methodSpecs, standAloneSigs;
};

// The token walker (plan 5.9.7): decodes the body of \p m (tiny or fat
// header, EH sections) with lib/CLI's opcode table and records every token
// operand in m.sites. Unknown opcodes, truncated bodies, a token of a table
// the operand kind does not allow, a token whose row does not exist and a
// bad local signature or catch class token are reported as "<file>:
// malformed method body in '<m>' at IL offset <n>"; returns false then.
bool decodeMethod(ObjFile &file, InputMethod &m);
} // namespace lld::msil

#endif
