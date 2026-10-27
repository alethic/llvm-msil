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
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/Support/LibraryOptionsContext.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>
#include <memory>
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
class ObjFile;
class ReferenceSet;
class Symbol;
class SyntheticData;
class SyntheticSegment;

// This struct contains the global configuration for the linker.
// Most fields are direct mapping from the command line options
// and such fields have the same name as the corresponding options.
// Most fields are initialized by the driver.
//
// Options without a member act while the driver reads them: -l and -l: (the
// libraries member keeps their values), --whole-archive, --no-whole-archive,
// --start-lib and --end-lib (input order), --trace-symbol and -y
// (SymbolTable::trace), --reproduce (tar), --fatal-warnings, --error-limit,
// --verbose and --color-diagnostics (lld's ErrorHandler), and the rejected
// -r. -Bstatic and -Bdynamic set isStatic while the driver reads the inputs
// (lld/ELF's member), --ucrtbase-imports adds its built-in library where it
// appears, and --reference adds its file or directory to ctx.references where
// it appears (references keeps the values too).
struct Config {
  bool allowMultipleDefinition;
  // --dependent-libraries (default) and --no-dependent-libraries.
  bool dependentLibraries;
  bool disableVerify;
  // --export-all-symbols (plan 5.9.15): on Windows triples, a -shared
  // library exports every candidate even when some input has dllexport
  // definitions.
  bool exportAllSymbols;
  // --gc-sections (default) and --no-gc-sections (plan 5.9.11).
  bool gcSections;
  // --interface-only (plan 5.9.15; only with -shared): an interface library
  // with stub bodies, linked without late binding.
  bool interfaceOnly;
  // -Bstatic: -l accepts static libraries only (plan 5.10.9).
  bool isStatic = false;
  bool ltoEmitAsm;
  // --print-bcl-table (plan 5.11.1): print the built-in [mscorlib] table and
  // exit.
  bool printBclTable;
  bool printGcSections;
  bool printImports;
  bool saveTemps;
  // -shared and --shared (plan 5.9.15): a library assembly (a DLL image
  // without an entry point) with exports.
  bool shared;
  bool stripAll;
  bool trace;
  // True if --ucrtbase-imports is given (an error on non-Windows targets).
  bool ucrtbaseImports;
  bool warnCommon;
  unsigned ltoPartitions;
  unsigned ltoo;
  llvm::CodeGenOptLevel ltoCgo;
  // --msil-reloc-loop-threshold (hidden, plan 5.9.10): the number of data
  // relocations above which the trailer applies them through the relocation
  // loop. Default 32.
  uint32_t msilRelocLoopThreshold = 32;
  // True if --msil-reloc-loop-threshold is given: under --lto-emit-asm its
  // value reaches code generation as the module flag
  // msil-reloc-loop-threshold (MSILLinkHooksPass, plan 5.9.10, 5.9.13).
  bool msilRelocLoopThresholdGiven = false;
  // --msil-type-member-limit (hidden, plan 5.9.12): replaces the per-type
  // limits of 65,524 methods and 65,534 fields when it is smaller. UINT32_MAX
  // (the default) keeps the real limits.
  uint32_t msilTypeMemberLimit = UINT32_MAX;

  // --assembly-name and -soname. Empty means the stem of outputFile.
  llvm::StringRef assemblyName;
  // --assembly-version (plan 5.9.15): the version of the Assembly row of
  // every output; missing parts are 0 (default 0.0.0.0).
  llvm::cli::AssemblyVersion assemblyVersion;
  llvm::StringRef entry;
  // --export-class=<Ns.Name> (plan 5.9.15) as given, which the driver has
  // checked with parseClassName(). Empty means the assembly name.
  llvm::StringRef exportClass;
  llvm::StringRef mapFile;
  llvm::StringRef outputFile;
  llvm::StringRef sysroot;
  llvm::StringRef thinLTOCacheDir;
  llvm::StringRef thinLTOJobs;
  llvm::StringRef whyExtract;

  // --target: the codegen triple, an MSIL triple.
  llvm::Triple target;

  // --exclude-libs=<list> (lld/ELF's): the archive file names, split at ','
  // and ':', whose members' definitions a -shared library does not export;
  // "ALL" names every archive.
  llvm::DenseSet<llvm::StringRef> excludeLibs;

  // --print-bcl-binding=<request> (hidden, plan 5.11.5), in command-line
  // order: print how the references bind each request and exit.
  llvm::SmallVector<llvm::StringRef, 0> printBclBinding;
  // The values of -l (the -l:<file> ones with their ':'), in command-line
  // order: getCRuntimeIdentity() (plan 5.9.15) reads them. The driver
  // searches each where it appears.
  llvm::SmallVector<llvm::StringRef, 0> libraries;
  // --reference=<file|dir> (plan 5.11.1), in command-line order.
  llvm::SmallVector<llvm::StringRef, 0> references;
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
  // The loaded CIL objects (inputs and extracted archive members) in load
  // order, then the objects that LTO wrote (ObjFile::isLTO, plan 5.9.13).
  llvm::SmallVector<ObjFile *, 0> objectFiles;
  // The synthetic segments of the data image: the common symbols that
  // survived resolution, in plan 5.9.9 rule 3's order of their first
  // declarations (SymbolTable::finalizeObjectSymbols), and the writer's
  // address slots.
  llvm::SmallVector<SyntheticSegment *, 0> syntheticSegments;
  // The native libraries read, in load order (plan 5.10.10).
  llvm::SmallVector<NativeLibraryFile *, 0> nativeLibraryFiles;
  // Every loaded file (CIL objects, bitcode files, extracted archive members
  // and native libraries; not the objects that LTO wrote) in load order: the
  // Inputs: section of the map file.
  llvm::SmallVector<InputFile *, 0> loadedFiles;

  // The definitions of the native libraries (plan 5.10.8).
  NativeIndex nativeIndex;

  // The reference assemblies (plan 5.11.2): --reference files and
  // directories and the positional .NET assemblies without the toolchain's
  // symbol table, in command-line order. Never null; Ctx::reset() makes a
  // new, empty set, so that every link (LLD_IN_TEST=2) starts from none.
  // ReferenceSet.h defines the class: Config.h does not include it, so that
  // its lld::msil::AssemblyIdentity stays out of the TUs that use
  // llvm::cli's under a using-directive.
  std::unique_ptr<ReferenceSet> references;

  // The /alternatename:<from>=<to> linker options of the inputs (plan 5.9.4),
  // in the order they were read, and the ones that apply because <from> is
  // still undefined after every input is loaded (lld-link's rule); the
  // import-annotation pass resolves <from> to <to>.
  llvm::MapVector<llvm::StringRef, llvm::StringRef> alternateNames;
  llvm::MapVector<llvm::StringRef, llvm::StringRef> appliedAlternateNames;

  // The /failifmismatch:<key>=<value> linker options: the value of each key
  // and the last input that gave it (lld-link's mustMatch).
  llvm::StringMap<std::pair<llvm::StringRef, const InputFile *>> mustMatch;

  // The definitions of the members of the --exclude-libs archives (CIL
  // objects and bitcode files), which a -shared library never exports (plan
  // 5.9.15): excludeLibs() records them before LTO, as lld/ELF does, so that
  // LTO may internalize them and the LTO objects that define them again stay
  // excluded.
  llvm::DenseSet<const Symbol *> excludedSymbols;

  // linker-generated symbols
  struct MSILSym {
    // The linker-defined symbols of the data image, in the order the writer
    // defined them through SymbolTable::addSyntheticData(): __start_X and
    // __stop_X (plan 5.9.9).
    llvm::SmallVector<SyntheticData *, 0> startStop;
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
