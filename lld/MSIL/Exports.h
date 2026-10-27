//===- Exports.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The exports of a library assembly (-shared; llvm/docs/MSIL/
// ImplementationPlan.md 5.9.15, Q22-Q25): which definitions a -shared link
// exports, the export class that holds them, and the payload of the
// library's '<msil>SymbolTable' attribute. The writer turns the plan into
// rows: the exported functions become public static methods of the export
// class, each with a '<msil>addr_' accessor, and every exported variable an
// accessor and a property.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_EXPORTS_H
#define LLD_MSIL_EXPORTS_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lld::msil {
class Symbol;

// One exported definition. Sym is the symbol table's entry (a
// DefinedFunction, a DefinedData or a CommonSymbol of a CIL object or an LTO
// object). For an alias (Sym->isAlias), AliasOf is its aliasee, a definition
// of the same object (possibly a LOCAL one), or null when the aliasee lost
// resolution to another input's definition: a DefinedFunction alias shares
// its aliasee's method, and a DefinedData alias lies in its aliasee's
// segment at its own offset. Size and AlignLog2 describe a variable (0 for a
// function); Flags are the variable's cli::LibraryDataSymbol flags (Const,
// Weak) or the function's cli::LibraryFunctionFlags (Variadic, Weak, ABI),
// as the symbol table lists them.
struct ExportedSymbol {
  Symbol *Sym;
  bool IsData;
  Symbol *AliasOf;
  uint64_t Size;
  uint8_t AlignLog2;
  uint8_t Flags;
};

// The exports of a -shared link (plan 5.9.15): the export class (Namespace
// empty for a class without one), the exported functions and variables in
// input order (CIL objects in load order, then the LTO objects; each in
// SYMTAB order), and the payload of the '<msil>SymbolTable' attribute
// (cli::encodeSymbolTable), which the writer frames with
// cli::encodeByteArrayAttribute. Under --interface-only the plan is the one
// the real link of the same inputs makes, and the table is flagged
// INTERFACE.
struct ExportPlan {
  std::string Namespace, Name;
  std::vector<ExportedSymbol> Functions, Data;
  SmallVector<uint8_t, 0> SymbolTablePayload;
};

// Computes the export plan of a -shared link after resolution, LTO and late
// binding (Q22):
// - the candidates are the defined GLOBAL and WEAK symbols (COMMON ones as
//   data) of default or protected visibility, except names that start with
//   '<msil>' or are '.ctor' or '.cctor', and the definitions of the members of
//   --exclude-libs archives;
// - on Windows triples, a candidate that is not DLLEXPORT and belongs to a
//   COMDAT group of its object (a FUNCTION member, or DATA in a member
//   segment) is not exported, with or without --export-all-symbols (section
//   12 row 'Q22 as implemented' of 2026-10-26);
// - on Windows triples, if a candidate is DLLEXPORT and --export-all-symbols
//   is absent, only the DLLEXPORT candidates are exported.
// The export class is --export-class, else the assembly name, parsed by
// parseClassName(). The errors are the export class's "invalid
// --export-class value '<v>'" and, for each thread-local candidate,
// "thread-local storage is not supported yet: <s> in <file>"; the returned
// Error holds one ErrorInfo per diagnostic (report each with lld's error(),
// for example through handleAllErrors). Reports nothing itself.
Expected<ExportPlan> computeExportPlan();

// True if a -shared link may export \p sym: a definition (or common symbol)
// with GLOBAL or WEAK binding and default or protected visibility, whose name
// does not start with '<msil>' or with '??_C@' (an MSVC string literal) and is
// not '.ctor' or '.cctor', and which is no --exclude-libs member's. LTO keeps
// such bitcode definitions visible to regular objects under -shared, because
// irsymtab has no dllexport bit (plan 5.9.13); the Windows dllexport and
// COMDAT rules of computeExportPlan() then apply to the LTO objects.
bool mayBeExported(const Symbol &sym);

// The C runtime identity of the link (plan 5.9.15): the first -l value in
// command-line order (driver defaults included) that is c, msvcrt, ucrt,
// ucrtbase or crtdll or starts with msvcr, as given; else "none".
std::string getCRuntimeIdentity();

// Splits an export class name, the value of --export-class or the assembly
// name, at its last '.' into (namespace, name); a name without '.' has an
// empty namespace. An empty name, a '/', more than 1,023 bytes (Q27), a
// namespace or name that starts with '<msil>', and the value '<Module>' are
// the error "invalid --export-class value '<value>'". The result points into
// \p value.
Expected<std::pair<StringRef, StringRef>> parseClassName(StringRef value);

// --exclude-libs (lld/ELF's excludeLibs): records the definitions of the
// members of the --exclude-libs archives among the loaded CIL objects and
// bitcode files in ctx.excludedSymbols. The driver calls it after every
// input is loaded and before LTO, and again for the members that LTO's
// objects extract.
void excludeLibs();

// Plan 5.9.11: the export set of a -shared link is a root of --gc-sections.
// Marks the symbols that computeExportPlan() exports as no-strip, which
// markLive() keeps, without reporting anything. The driver calls it after
// late binding and before markLive().
void markExportsLive();
} // namespace lld::msil

#endif
