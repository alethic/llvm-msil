//===- Signatures.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Call-site signature checks and adapters of msil-ld
// (llvm/docs/MSIL/ImplementationPlan.md 5.9.8, Q8; wasm-ld's
// reportFunctionSignatureMismatch, lld/wasm/SymbolTable.cpp:378-398, and its
// undefined-function stubs, lld/wasm/SymbolTable.cpp:1159-1180):
//
// - every call site that the link binds by name to a definition (a MemberRef
//   on '<msil>Symbols', an import stub resolved to a definition, the
//   MethodDef of a losing WEAK or discarded COMDAT copy) is compared with the
//   definition by cli::classifyCall, and calls the definition directly, an
//   adapter '<msil>adapt_<name>_<n>' (with a warning) or reports an error;
// - a call of an undefined weak function calls a trap stub
//   '<msil>undef_<name>' (ldnull, throw) with the call's signature;
// - a call of a natively bound function calls its P/Invoke stub, after the
//   link-time checks that plan 5.10.13 moves out of code generation
//   (5.10.14's variadic, non-passable and callback texts, and Phase 1's
//   prototype-less text, M19.b (2a)).
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_SIGNATURES_H
#define LLD_MSIL_SIGNATURES_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include <cstdint>
#include <optional>

namespace lld::msil {
class ObjFile;
class OutputModule;
class SymbolBinder;
struct TokenTarget;

class CallBinder {
public:
  CallBinder(OutputModule &out, SymbolBinder &symbols)
      : out(out), symbols(symbols) {}

  // The checks of plan 5.10.14 for every reference that native lookup bound
  // (SymbolBinder::getReferences), one error per symbol: the callback list
  // for calls and addresses (explicit imports exempt), variadic calls,
  // values a P/Invoke cannot pass (SRET, BYVAL, NATIVE_INCOMPATIBLE, and the
  // `long double` functions where the native type is wider; explicit
  // imports exempt from the last), and a prototype-less function used only
  // by address.
  void checkNativeReferences();

  // The output method that the Method token \p t of \p f calls: the
  // definition, an adapter, a trap stub or a P/Invoke stub. std::nullopt
  // after an error (reported here or by SymbolBinder::reportUnresolved).
  std::optional<uint32_t> bindCall(ObjFile &f, const TokenTarget &t);

private:
  OutputModule &out;
  SymbolBinder &symbols;
  // The adapters and trap stubs, by key.
  llvm::StringMap<uint32_t> adapters, trapStubs;
  // The number of adapters of each definition (n of '<msil>adapt_<name>_<n>').
  llvm::DenseMap<uint32_t, uint32_t> adapterCounts;
  // The diagnostics already reported.
  llvm::StringSet<> reported;
};
} // namespace lld::msil

#endif
