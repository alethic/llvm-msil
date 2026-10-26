//===- Relocations.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The output entities that the symbols of msil-ld's link name
// (llvm/docs/MSIL/ImplementationPlan.md 5.9.6, 5.9.7, 5.9.12, 5.10.11,
// 5.10.12; lld/ELF's Relocations.cpp and lld/wasm's Relocations.cpp have the
// role for native objects): the address field of every DATA symbol that the
// output uses, one '<msil>fn_' field per resolved method, one P/Invoke stub
// per natively bound function and IL signature, and the native data imports
// with the data-import helper.
//
// It also records which symbols the live methods and segments refer to
// (plan 5.9.11: references from dead units are not reported, as in lld) and
// reports the ones that stay unresolved: 5.9.6 steps 3-4 ('undefined
// symbol', 'undefined hidden symbol', a discarded COMDAT copy), the
// definitions that native lookup passed over (5.10.14), and natively bound
// symbols used as the other kind.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_RELOCATIONS_H
#define LLD_MSIL_RELOCATIONS_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CLI/Trailer.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace lld::msil {
class NativeFunction;
class ObjFile;
class OutputImage;
class OutputModule;
class Symbol;
struct NativeExport;

// One reference of a live method or of a segment of the image to a symbol.
struct SymbolReference {
  enum Kind : uint8_t {
    // A Method token: a call, or the MethodDef of an import stub.
    Call,
    // A '<msil>FnAddr' token or an FNADDR relocation: a function address.
    Address,
    // A Field token, or an ADDR, DIFF or TLS_OFFSET relocation.
    Data,
  };
  Kind kind;
  ObjFile *file;
  Symbol *sym;
  // Call: the call side's signature (as the object encodes it) and its
  // SIGNATURES entry.
  const llvm::cli::MethodSig *signature = nullptr;
  const llvm::MSILLink::SignatureInfo *signatureInfo = nullptr;
};

// The signature of a natively bound function that its import stubs or its
// first call give, with the SIGNATURES entry and the object.
struct NativeSignature {
  ObjFile *file;
  const llvm::cli::MethodSig *signature;
  const llvm::MSILLink::SignatureInfo *signatureInfo;
};

class SymbolBinder {
public:
  SymbolBinder(OutputModule &out, OutputImage &image)
      : out(out), image(image) {}

  // The address fields of the objects' own FieldDefs (plan 5.9.12: by
  // object in link order and row order): every definition that won and lies
  // in a live segment, and every surviving common symbol at the object of
  // its first declaration.
  void addObjectFields();

  // Records the references of the live methods and of the image's segments,
  // in first-use order (objects in link order, methods in row order, sites
  // in code order, then segments), and the import stubs' signatures.
  void collectReferences();
  ArrayRef<SymbolReference> getReferences() const { return references; }

  // Reports the referenced symbols that stay unresolved, and natively bound
  // symbols used as the other kind: "'<name>' in <module> is data, but
  // <file> calls it as a function" and "symbol type mismatch".
  void reportUnresolved();

  // The signature for the P/Invoke stub of the natively bound function
  // \p sym that takes its address (plan 5.10.11): the first import stub's
  // (the declared prototype), else the first call's; std::nullopt if no
  // object gives one (a prototype-less function used only by address).
  std::optional<NativeSignature> getNativeSignature(const Symbol *sym) const;

  // The address field of the DATA symbol \p sym, made on first use: a
  // definition, an alias, a common symbol, __start_X/__stop_X, a native data
  // import or an undefined weak symbol (never stored). std::nullopt for
  // anything else (already reported).
  std::optional<uint32_t> getDataField(Symbol *sym);
  std::optional<uint32_t> findDataField(const Symbol *sym) const;
  // The '<msil>fn_' field of the method that the FUNCTION symbol \p sym
  // resolves to (one per method: an alias shares its target's; a native
  // function's names its canonical P/Invoke stub; an undefined weak
  // function's is never stored), made on first use.
  std::optional<uint32_t> getFnAddrField(Symbol *sym);
  // The P/Invoke stub of \p name with output signature \p sig (plan
  // 5.10.11), made on first use.
  uint32_t getPInvokeStub(StringRef name, StringRef module, StringRef entry,
                          const llvm::cli::MethodSig &sig);
  // The canonical P/Invoke stub of the natively bound function \p sym (plan
  // 5.10.11): the one of getNativeSignature(). std::nullopt if no object
  // gives a signature (CallBinder reports the function).
  std::optional<uint32_t> getCanonicalPInvokeStub(const NativeFunction *sym);

  // The trailer's '<msil>fn_' fields in 5.9.10's canonical order.
  void addFnAddrFields(llvm::cli::TrailerInput &in);
  // The trailer's data imports (plan 5.10.12), in the order of the objects'
  // symbol tables, and the helper; reports 5.10.14's text when no helper
  // is available. Returns false after an error.
  bool addDataImports(llvm::cli::TrailerInput &in);
  // Sets the image offsets of the address fields once the image is final.
  void setImageOffsets();
  // --print-imports (plan 5.10.14) for the PE.
  void printImports() const;

private:
  // The key of a '<msil>fn_' field: the definition's method, or the symbol.
  const void *getFnAddrKey(const Symbol *sym) const;

  OutputModule &out;
  OutputImage &image;
  SmallVector<SymbolReference, 0> references;
  llvm::DenseMap<const Symbol *, NativeSignature> stubSignatures;
  llvm::DenseMap<const Symbol *, uint32_t> dataFields;
  llvm::DenseMap<const void *, uint32_t> fnAddrFields;
  llvm::StringMap<uint32_t> pinvokeStubs;
  // The functions of the data-import helper, for --print-imports.
  struct HelperFunction {
    std::string name, module, entry;
    const NativeExport *def;
  };
  SmallVector<HelperFunction, 2> helperFunctions;
};
} // namespace lld::msil

#endif
