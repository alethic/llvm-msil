//===- InputChunks.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/InputChunks.cpp at 64f50ffd4f1c.
//
//===----------------------------------------------------------------------===//

#include "InputChunks.h"
#include "InputFiles.h"
#include "Symbols.h"

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace lld;
using namespace lld::msil;

std::string lld::toString(const InputChunk *c) {
  std::string file = c->file ? toString(c->file) : std::string("<internal>");
  return file + ":(" + c->name.str() + ")";
}
