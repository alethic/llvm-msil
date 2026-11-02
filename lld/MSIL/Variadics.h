//===- Variadics.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The C runtime tables of native variadic functions and the link-time texts
// of variadic calls (llvm/docs/MSIL/ImplementationPlan.md 5.15.2, 'Link time
// (msil-ld)'; 5.14.8). The backend expands every variadic definition and
// every call of an undefined variadic function over a native-layout va_list
// (record flag VARARG), except the calls of the triple's fixed-arity natives,
// which it calls with the call site's signature (FIXED_ARITY). msil-ld binds:
//
// - a VARARG reference bound natively to the va_list variant of the function
//   (printf as vprintf), found in the native index only, with the expanded
//   signature (rule 2);
// - a FIXED_ARITY reference natively with one P/Invoke per IL signature and
//   no variant (rule 3); bound to an MSIL VARARG definition or a Variadic
//   export of a library built by this toolchain it is an error (5.14.8);
// - the address of a natively bound variadic function is an error until
//   M28's adapters (rule 4, getFixedArityAdapter).
//
// The tables come from llvm/docs/MSIL/recon/p3/variadics-files/
// (glibc-variadics.txt, msvcrt-variadics.txt, ucrt-variadics.txt).
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_VARIADICS_H
#define LLD_MSIL_VARIADICS_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <optional>
#include <string>

namespace llvm {
class Triple;
} // namespace llvm

namespace lld::msil {
struct Ctx;
struct NativeExport;

// The C runtime of a link (plan 5.15.2 rule 5), which selects the tables.
enum class CRuntime : uint8_t { None, Glibc, Musl, UCRT, Msvcrt };

/// linux-gnu Glibc (any Linux triple but musl and Android, as the backend's
/// fixed-arity list), linux-musl Musl, windows-msvc UCRT, windows-gnu Msvcrt
/// (UCRT when the C runtime identity of 5.9.15 is ucrt or ucrtbase),
/// otherwise None.
CRuntime getCRuntime(const llvm::Triple &TT, llvm::StringRef CRuntimeIdentity);

/// The va_list variant of native variadic function Name: the same fixed
/// parameters followed by a va_list.
std::optional<llvm::StringRef> getVaListVariant(llvm::StringRef Name,
                                                CRuntime RT);

/// The variadic slots that the address adapter of a fixed-arity native reads
/// (5.15.3, M28), in order; std::nullopt: no adapter, so taking the
/// function's address is a link error.
enum class VarSlot : uint8_t { Int32, NativeInt };
std::optional<llvm::ArrayRef<VarSlot>>
getFixedArityAdapter(llvm::StringRef Name, CRuntime RT);

/// Returns true if \p Name is on the fixed-arity list of \p RT: a native
/// variadic function without a va_list variant whose calls the backend
/// rewrites into fixed-arity calls (MSILPrepareVariadics, plan 5.15.2). The
/// backend keeps the same lists for the triples' C runtimes; msil-ld needs
/// them where it decides before code generation, in LTO's import analysis
/// under --lto-emit-asm, whose merged module still has the variadic calls.
/// No name is on a fixed-arity list and in the va_list variant table of the
/// same C runtime (asserted at first use, with the order of every list).
bool isFixedArityFunction(llvm::StringRef Name, CRuntime RT);

/// Rule 2's text for a natively bound VARARG reference whose name has no
/// table entry: "native function '<name>' (<module>) is variadic and has
/// neither a va_list variant nor an entry in the fixed-arity list".
std::string getNoVaListVariantText(llvm::StringRef Name,
                                   llvm::StringRef Module);

/// Rule 2's text for a variant that no native library defines: "native
/// function '<name>' (<module>) is variadic, and its va_list variant
/// '<variant>' is not defined by the native libraries".
std::string getUndefinedVaListVariantText(llvm::StringRef Name,
                                          llvm::StringRef Module,
                                          llvm::StringRef Variant);

/// Rule 4's text, until M28: "the address of native variadic function
/// '<name>' (<module>) is not supported for MSIL yet".
std::string getVariadicAddressText(llvm::StringRef Name,
                                   llvm::StringRef Module);

/// Plan 5.14.8's text for a FIXED_ARITY call in \p File bound to the VARARG
/// definition in \p DefFile (an object, an LTO object, an archive member or
/// a library built by this toolchain): "fixed-arity call of '<name>' in
/// <file> binds to the variadic definition in <file2>, which MSIL cannot call
/// with fixed arity".
std::string getFixedArityToVariadicText(llvm::StringRef Name,
                                        llvm::StringRef File,
                                        llvm::StringRef DefFile);

// What plan 5.15.2's link-time rule 2 binds a VARARG reference (an expanded
// call: its fixed arguments and a va_list) of the natively bound function
// \p name, whose native definition is \p e, to: the va_list variant of the C
// runtime of the link (Variadics.h: getCRuntime of the codegen triple and the
// C runtime identity of plan 5.9.15) that the table names for e's entry or
// for \p name (printf -> vprintf), looked up in the native index only, so
// that a program's own definition of the variant never captures the
// reference. The P/Invoke keeps \p name and takes the variant's module and
// entry (plan 5.10.11). Variant is null after an error, and Error is rule
// 2's text: no table entry (getNoVaListVariantText), or a variant that no
// native library defines as a function (getUndefinedVaListVariantText).
// The writer (CallBinder::checkNativeReferences) and LTO's import analysis
// ask it; FIXED_ARITY references bind to \p e itself (rule 3).
struct VaListVariantBinding {
  const NativeExport *Variant = nullptr;
  std::string Error;
};
VaListVariantBinding bindVaListVariant(StringRef name, const NativeExport &e,
                                       const Ctx &ctx);
} // namespace lld::msil

#endif
