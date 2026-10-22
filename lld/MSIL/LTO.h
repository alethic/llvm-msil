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
// msil-ld's inputs are LLVM bitcode files (and, from M20, CIL objects), so the
// linker has to compile bitcode files into CIL. This file provides that
// functionality. Under --lto-emit-asm, compile() writes the ILAsm text of the
// whole-program module to -o itself and returns no files; from M20, without
// --lto-emit-asm, it returns the LTO output as in-memory CIL objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_LTO_H
#define LLD_MSIL_LTO_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
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
};
} // namespace lld::msil

#endif
