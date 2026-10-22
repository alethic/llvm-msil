//===- MapFile.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/MapFile.h at 64f50ffd4f1c.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_MAPFILE_H
#define LLD_MSIL_MAPFILE_H

namespace lld::msil {
// Writes the map of -Map=<file>, or of --print-map and -M to standard output
// (plan 5.9.14). The driver calls it after LTO.
void writeMapFile();
} // namespace lld::msil

#endif
