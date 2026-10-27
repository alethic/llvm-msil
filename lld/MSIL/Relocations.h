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
// with the data-import helper (NativeLibrary when the references define it,
// else the kernel32 or dlopen/dlsym P/Invokes), the C library's environ
// among them when a constructor takes envp.
//
// It also records which symbols the live methods and segments refer to
// (plan 5.9.11: references from dead units are not reported, as in lld) and
// reports the ones that stay unresolved: 5.9.6 steps 3-4 ('undefined
// symbol', 'undefined hidden symbol', a discarded COMDAT copy), the
// definitions that native lookup passed over (5.10.14), and natively bound
// symbols used as the other kind. The exports of libraries built by this
// toolchain that late binding bound (plan 5.9.15) need no field: code calls
// their methods and accessors (CallBinder::bindManaged), a data relocation
// against one is a cross-assembly relocation (getCrossRelocations()), and a
// symbol difference against one is an error.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_RELOCATIONS_H
#define LLD_MSIL_RELOCATIONS_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CLI/Trailer.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lld::msil {
class NativeData;
class NativeFunction;
class ObjFile;
class OutputImage;
class OutputModule;
class Symbol;
struct ManagedExport;
struct NativeExport;

// A data relocation of the output's image whose target is an export of a
// library built by this toolchain (plan 5.9.15, M22.c item 6): FNADDR64,
// FNADDR32, ADDR64 or ADDR32. Phase 2 of the initialization stores at
// OutputSite (an offset in the data image) Width bytes (8 or 4) of the
// value of the export's '<msil>addr_<name>' accessor plus Addend (M22.b's
// cli::CrossReloc, whose Accessor is the MemberRef that
// ManagedLibraryFile::getMemberRef gives with Accessor true).
struct CrossRelocation {
  uint64_t OutputSite;
  uint8_t Width;
  const ManagedExport *Target;
  int64_t Addend;
};

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
  // in code order, then segments), and the import stubs' signatures. A token
  // site of a live method that names a library export
  // (TokenTarget::isManaged) must be an ldsfld for an address (a Field or
  // FnAddr token) and a call for a Method token; any other opcode is "<file>:
  // unsupported use of imported symbol '<s>' in '<m>' at IL offset <n>"
  // (plan 5.9.7; importers never take a library address with ldftn).
  void collectReferences();
  ArrayRef<SymbolReference> getReferences() const { return references; }

  // Reports the referenced symbols that stay unresolved, and natively bound
  // symbols used as the other kind: "'<name>' in <module> is data, but
  // <file> calls it as a function" and "symbol type mismatch". A library
  // export is resolved; a symbol difference (DIFF) in the image against one
  // is "symbol difference '<a>' - '<b>' in <file> refers to a symbol of
  // <lib>" (the image's relocations then pass that DIFF over). A weak
  // symbol that late binding left undefined is null (plan 5.9.6 step 3),
  // unless a native object given on the command line defines it: that is
  // plan 5.10.14's "symbol '<s>' is defined only in native object code
  // (<file>); MSIL cannot link machine code", as for a non-weak one. An
  // --interface-only link never calls this: it binds nothing late and may
  // leave symbols undefined (plan 5.9.15).
  void reportUnresolved();

  // The data relocations of the image's segments against library exports,
  // in site order (plan 5.9.15): phase 2's cross-assembly relocations. The
  // image's own relocations (OutputImage::addRelocations) pass them over.
  // Valid once the image is laid out.
  std::vector<CrossRelocation> getCrossRelocations() const;

  // The signature for the P/Invoke stub of the natively bound function
  // \p sym that takes its address (plan 5.10.11): the first import stub's
  // (the declared prototype), else the first call's; std::nullopt if no
  // object gives one (a prototype-less function used only by address).
  std::optional<NativeSignature> getNativeSignature(const Symbol *sym) const;

  // The address field of the DATA symbol \p sym, made on first use: a
  // definition, an alias, a common symbol, __start_X/__stop_X, a native data
  // import or an undefined weak symbol (never stored). std::nullopt for
  // anything else (already reported), and for a library export, whose
  // address its accessor gives (CallBinder::bindManaged).
  std::optional<uint32_t> getDataField(Symbol *sym);
  std::optional<uint32_t> findDataField(const Symbol *sym) const;
  // The '<msil>fn_' field of the method that the FUNCTION symbol \p sym
  // resolves to (one per method: an alias shares its target's; a native
  // function's names its canonical P/Invoke stub; an undefined weak
  // function's is never stored), made on first use. std::nullopt for a
  // library export, whose address its accessor gives.
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
  // symbol tables, and the helper: NativeLibrary's Load and GetExport when
  // the references define both (BCLBinder::isDefined, M21), else M20's
  // kernel32 pair on Windows triples or dlopen/dlsym from the native
  // libraries on ELF triples; reports 5.10.14's text when no helper is
  // available. Returns false after an error.
  bool addDataImports(llvm::cli::TrailerInput &in);
  // envp for the global constructors that take it (plan 5.9.10 amended,
  // constructor arguments), after addDataImports(): sets in.EnvironField to
  // the address field of the C library's environ. That is the program's own
  // import when its NativeData 'environ' has a field (the writer makes it
  // before addDataImports(), which imports it), else '<msil>environ', a data
  // import of the first native data definition of environ in link order,
  // with the data-import helper of addDataImports() when in.ImportHelper is
  // unset. --print-imports and the map list it as a program's own import of
  // environ. Without a native data definition it reports "global constructor
  // '<ctorName>' takes envp, but no native library of the link defines
  // 'environ'". Returns false after an error.
  bool addEnvironImport(llvm::cli::TrailerInput &in, StringRef ctorName);
  // Sets the image offsets of the address fields once the image is final.
  void setImageOffsets();
  // --print-imports (plan 5.10.14) for the PE.
  void printImports() const;

private:
  // The key of a '<msil>fn_' field: the definition's method, or the symbol.
  const void *getFnAddrKey(const Symbol *sym) const;
  // The data-import helper (plan 5.10.12): getNativeLibraryHelper(), else
  // the kernel32 or dlopen/dlsym P/Invokes of findDataImportFunctions(),
  // which --print-imports lists (helperFunctions). When neither is
  // available it calls \p reportUnavailable, which reports 5.10.14's text,
  // and returns std::nullopt, as after an error.
  std::optional<llvm::cli::DataImportHelper>
  getDataImportHelper(llvm::function_ref<void()> reportUnavailable);
  // The NativeLibrary helper (plan 5.10.12) when the references define
  // NativeLibrary::Load(string) and GetExport(native int, string), bound;
  // std::nullopt otherwise (and after an error, which is reported).
  std::optional<llvm::cli::DataImportHelper> getNativeLibraryHelper();

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
  // The C library's environ that addEnvironImport() chose, for
  // --print-imports.
  const NativeData *environImport = nullptr;
};
} // namespace lld::msil

#endif
