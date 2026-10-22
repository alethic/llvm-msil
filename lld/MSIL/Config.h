//===- Config.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/Config.h at 64f50ffd4f1c.
//
// The configuration of msil-ld (Config: one member per command-line option)
// and its other global state (Ctx).
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_CONFIG_H
#define LLD_MSIL_CONFIG_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/LibraryOptionsContext.h"
#include "llvm/TargetParser/Triple.h"
#include <string>
#include <tuple>

namespace llvm {
enum class CodeGenOptLevel;
} // namespace llvm

namespace lld::msil {
class InputFile;
class BitcodeFile;
class Symbol;

// This struct contains the global configuration for the linker.
// Most fields are direct mapping from the command line options
// and such fields have the same name as the corresponding options.
// Most fields are initialized by the driver.
//
// Options without a member act while the driver reads them: -l, -l:,
// --whole-archive, --no-whole-archive, --start-lib and --end-lib (input
// order), --trace-symbol and -y (SymbolTable::trace), --reproduce (tar),
// --fatal-warnings, --error-limit, --verbose and --color-diagnostics
// (lld's ErrorHandler), and the rejected -r.
struct Config {
  bool allowMultipleDefinition;
  bool disableVerify;
  bool ltoEmitAsm;
  bool saveTemps;
  bool shared;
  bool stripAll;
  bool trace;
  bool warnCommon;
  unsigned ltoPartitions;
  unsigned ltoo;
  llvm::CodeGenOptLevel ltoCgo;

  // --assembly-name and -soname. Empty means the stem of outputFile.
  llvm::StringRef assemblyName;
  llvm::StringRef entry;
  llvm::StringRef mapFile;
  llvm::StringRef outputFile;
  llvm::StringRef sysroot;
  llvm::StringRef thinLTOCacheDir;
  llvm::StringRef thinLTOJobs;
  llvm::StringRef whyExtract;

  // --target: the codegen triple, an MSIL triple.
  llvm::Triple target;

  llvm::SmallVector<llvm::StringRef, 0> searchPaths;
  // -u and --undefined, in command-line order.
  llvm::SmallVector<llvm::StringRef, 0> undefined;
  // -mllvm and --mllvm.
  llvm::cl::LibraryOptionsContext libraryOptions;
};

// The Ctx object hold all other (non-configuration) global state.
struct Ctx {
  Config arg;

  llvm::SmallVector<BitcodeFile *, 0> bitcodeFiles;
  llvm::SmallVector<BitcodeFile *, 0> lazyBitcodeFiles;

  // linker-generated symbols
  struct MSILSym {
    // M20 adds the linker-defined symbols of the data image (__start_X,
    // __stop_X and the constructor tables, 5.9.9).
  };
  MSILSym sym;

  // A tuple of (reference, extractedFile, sym). Used by --why-extract=.
  llvm::SmallVector<std::tuple<std::string, const InputFile *, const Symbol &>,
                    0>
      whyExtractRecords;

  Ctx();
  void reset();
};

extern Ctx ctx;
} // namespace lld::msil

#endif
