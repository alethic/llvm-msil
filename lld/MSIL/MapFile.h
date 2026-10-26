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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lld::msil {
// What the PE map shows of the image that writeResult() wrote (plan 5.9.14):
// the sections Image:, Methods:, Fields: and Imports:, which exist only for a
// PE (not under --lto-emit-asm). writeResult() fills it; it holds no
// pointers, so a later link never reads freed memory through it.
struct OutputMap {
  // Set by a writeResult() that wrote its PE.
  bool valid = false;
  uint64_t totalSize = 0, initSize = 0;
  uint64_t align = 0;

  // One row of the Image: table: an output section (depth 0), an input
  // segment, common symbol or the address slots (depth 1) and a symbol
  // (depth 2).
  struct ImageRow {
    uint64_t offset, size, align;
    unsigned depth;
    std::string name;
  };
  std::vector<ImageRow> image;

  struct MethodRow {
    uint32_t token;
    std::string owner, input, symbol;
  };
  std::vector<MethodRow> methods;

  struct FieldRow {
    uint32_t token;
    std::string owner;
    std::optional<uint64_t> offset;
    std::string symbol;
  };
  std::vector<FieldRow> fields;

  struct ImportRow {
    uint32_t token;
    std::string moduleEntry, symbol;
  };
  std::vector<ImportRow> imports;
};

extern OutputMap outputMap;

// Writes the map of -Map=<file>, or of --print-map and -M to standard output
// (plan 5.9.14). The driver calls it after LTO, and after writeResult() when
// msil-ld writes a PE.
void writeMapFile();
} // namespace lld::msil

#endif
