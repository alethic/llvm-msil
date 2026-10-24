//===- llvm/CLI/Dump.h - ECMA-335 module dumper -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the dumper of the ECMA-335 library, which llvm-cil-dump
// prints with:
// - canonical listing v1: a layout-independent text of a module that two
//   writers of the same program (ilasm and msil-ld, two ilasms, an image and
//   its llvm-cil-copy) must agree on. M16's C# oracle mdcanon writes the same
//   listing from the same specification, byte for byte;
// - the PE, CLI and metadata headers (--headers);
// - every row of every table (--tables);
// - the link-info record of a CIL object and the symbol tables of a library
//   (--link-info).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_DUMP_H
#define LLVM_CLI_DUMP_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

class raw_ostream;

namespace cli {

class MetadataReader;

/// The options of canonical listing v1. Every option defaults to false
/// (keep), which is the listing unchanged.
struct CanonicalOptions {
  /// --scopes=strip: AssemblyRef scopes print [*]; assemblyref lines are
  /// omitted.
  bool StripScopes = false;
  /// --order=decl: table order instead of the sorted order.
  bool DeclarationOrder = false;
  /// --forms=canon: short branches and the short ldarg/ldarga/starg/ldloc/
  /// ldloca/stloc/ldc.i4 forms print without '.s'.
  bool CanonicalForms = false;

  // The six normalizations of the equivalence oracles. They apply while the
  // listing is produced, before sorting.

  /// --assembly-hash=ignore: the assembly line prints hash=*.
  bool IgnoreAssemblyHash = false;
  /// --params=drop: no .param line for a Param row with Flags 0, no
  /// FieldMarshal row with a non-zero NativeType index and no Constant row.
  bool DropParams = false;
  /// --initlocals=ignore-without-locals: a body whose LocalVarSigTok is 0
  /// prints .maxstack without ' initlocals'.
  bool IgnoreInitLocalsWithoutLocals = false;
  /// --snan=quiet: a float32 operand that holds a signaling NaN prints with
  /// bit 22 set.
  bool QuietSignalingNaN = false;
  /// --pinvoke-module=strip: .pinvokeimpl prints '*' for the module, and the
  /// moduleref lines are omitted.
  bool StripPInvokeModule = false;
  /// --pinvoke-access=ignore: the flags of a method with PinvokeImpl
  /// (0x2000) print with MemberAccess (0x0007) cleared.
  bool IgnorePInvokeAccess = false;

  /// --stats appends the '# table', '# heap' and '# image' lines to the
  /// listing; --stats-only prints only them.
  enum class Stats : uint8_t { None, Append, Only } StatsMode = Stats::None;
};

/// Writes canonical listing v1 of \p R, which must have a PE image
/// (MetadataReader::create). Nothing is written to \p OS on an error: a
/// malformed row, signature, body or token, data outside its section, or a
/// TypeSpec nested deeper than 64 levels (counted across blobs, as mdcanon
/// counts them).
LLVM_ABI Error dumpCanonical(const MetadataReader &R, raw_ostream &OS,
                             const CanonicalOptions &Opts = {});

/// Writes the Format, COFF, Optional, Section, CLI, Metadata, Stream and Mvid
/// lines of llvm-cil-dump --headers. \p R must have a PE image.
LLVM_ABI Error dumpHeaders(const MetadataReader &R, raw_ostream &OS);

/// Writes every present table: 'Table: <Name> rows=<n> rowsize=<n>', then
/// one line per row with every column in ECMA order.
LLVM_ABI Error dumpTables(const MetadataReader &R, raw_ostream &OS);

/// Writes the link-info record of the Module row (printLinkInfo) and then
/// every '<msil>SymbolTable' payload of the Assembly row (printSymbolTable).
/// An image with neither is the error "no link-info record or symbol table".
LLVM_ABI Error dumpLinkInfo(const MetadataReader &R, raw_ostream &OS);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_DUMP_H
