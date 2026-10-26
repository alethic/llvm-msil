//===- LTO.h ----------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/LTO.h at 64f50ffd4f1c.
//
// This file provides a way to combine bitcode files into one .NET assembly
// by compiling them using LLVM.
//
// msil-ld's inputs are CIL objects and LLVM bitcode files, so the linker has
// to compile bitcode files into CIL. This file provides that functionality.
// Under --lto-emit-asm, compile() writes the ILAsm text of the whole-program
// module to -o itself and returns no files; without --lto-emit-asm it returns
// the LTO output as in-memory CIL objects (one per ThinLTO module or
// partition, plan 5.9.13).
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_LTO_H
#define LLD_MSIL_LTO_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <string>
#include <vector>

namespace llvm {
namespace lto {
class LTO;
}
} // namespace llvm

namespace lld::msil {
class BitcodeFile;
class InputFile;

class BitcodeCompiler {
public:
  BitcodeCompiler();
  ~BitcodeCompiler();

  void add(BitcodeFile &f);
  SmallVector<InputFile *, 0> compile();
  void setBitcodeLibFuncs(ArrayRef<StringRef> bitcodeLibFuncs);

private:
  std::unique_ptr<llvm::lto::LTO> ltoObj;
  // An array of (module name, native relocatable file content) pairs.
  SmallVector<std::pair<std::string, SmallString<0>>, 0> buf;
  std::vector<std::unique_ptr<MemoryBuffer>> files;
  SmallVector<std::string, 0> filenames;
  std::unique_ptr<llvm::raw_fd_ostream> indexFile;
  llvm::DenseSet<StringRef> thinIndices;
  // The sections <X> that a __start_<X> or __stop_<X> symbol names: their
  // definitions stay visible to regular objects (lld/ELF/LTO.cpp's
  // usedStartStop).
  llvm::DenseSet<StringRef> usedStartStop;
};

// One line of --print-imports (plan 5.10.14).
struct PrintedImport {
  std::string symbol;
  std::string module;
  std::string entry;
  bool isData;
  std::string version;
  // True for a library read from an ELF shared object (version floor).
  bool isELF;
};

// Prints --print-imports (plan 5.10.14): one line per bound symbol, sorted
// by symbol, "<symbol> -> <module>!<entry> [function|data, <version>|-]",
// then one line per ELF module, "<module>: version floor <version>|-". The
// import-annotation pass prints the --lto-emit-asm report; the writer of a
// PE prints its own with the same function.
void printImports(std::vector<PrintedImport> &rows);
} // namespace lld::msil

#endif
