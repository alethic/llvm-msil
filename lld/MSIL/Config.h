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

#include "NativeLibraries.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/LibraryOptionsContext.h"
#include "llvm/TargetParser/Triple.h"
#include <string>
#include <tuple>
#include <utility>

namespace llvm {
enum class CodeGenOptLevel;
} // namespace llvm

namespace lld::msil {
class InputFile;
class BitcodeFile;
class NativeLibraryFile;
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
// (lld's ErrorHandler), and the rejected -r. -Bstatic and -Bdynamic set
// isStatic while the driver reads the inputs (lld/ELF's member), and
// --ucrtbase-imports adds its built-in library where it appears.
struct Config {
  bool allowMultipleDefinition;
  // --dependent-libraries (default) and --no-dependent-libraries.
  bool dependentLibraries;
  bool disableVerify;
  // -Bstatic: -l accepts static libraries only (plan 5.10.9).
  bool isStatic = false;
  bool ltoEmitAsm;
  bool printImports;
  bool saveTemps;
  bool shared;
  bool stripAll;
  bool trace;
  // True if --ucrtbase-imports is given (an error on non-Windows targets).
  bool ucrtbaseImports;
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
  // The native libraries read, in load order (plan 5.10.10).
  llvm::SmallVector<NativeLibraryFile *, 0> nativeLibraryFiles;
  // Every loaded file (bitcode files, extracted archive members and native
  // libraries) in load order: the Inputs: section of the map file.
  llvm::SmallVector<InputFile *, 0> loadedFiles;

  // The definitions of the native libraries (plan 5.10.8).
  NativeIndex nativeIndex;

  // The /alternatename:<from>=<to> linker options of the inputs (plan 5.9.4),
  // in the order they were read, and the ones that apply because <from> is
  // still undefined after every input is loaded (lld-link's rule); the
  // import-annotation pass resolves <from> to <to>.
  llvm::MapVector<llvm::StringRef, llvm::StringRef> alternateNames;
  llvm::MapVector<llvm::StringRef, llvm::StringRef> appliedAlternateNames;

  // The /failifmismatch:<key>=<value> linker options: the value of each key
  // and the last input that gave it (lld-link's mustMatch).
  llvm::StringMap<std::pair<llvm::StringRef, const InputFile *>> mustMatch;

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
