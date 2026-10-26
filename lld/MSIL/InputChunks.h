//===- InputChunks.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/InputChunks.h at 64f50ffd4f1c.
//
// An InputChunk is an indivisible unit of a CIL object (plan 5.9.2): a method
// with a body (InputMethod), one segment of the link-info record
// (InputSegment), or a unit that the linker makes (SyntheticSegment: the
// place of a common symbol, or the relocation loop's address slots).
//
// Chunks are the units of --gc-sections (plan 5.9.11) and of COMDAT
// handling. The writer copies the live ones: a method's body byte for byte
// with its token operands renumbered (plan 5.9.7), a segment's bytes into the
// output's data image (plan 5.9.9).
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_INPUT_CHUNKS_H
#define LLD_MSIL_INPUT_CHUNKS_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/MethodBody.h"
#include <cstdint>
#include <string>

namespace llvm::cli {
struct MethodSig;
} // namespace llvm::cli

namespace lld::msil {
class CommonSymbol;
class ObjFile;

class InputChunk {
public:
  enum Kind : uint8_t { MethodKind, SegmentKind, SyntheticSegmentKind };

  Kind kind() const { return chunkKind; }

  // The CIL object the chunk comes from. For a SyntheticSegment of a common
  // symbol, the object of the common's first declaration; null for the
  // address slots.
  ObjFile *file;

  // The name that diagnostics and --print-gc-sections show: the method's
  // name, the name of the symbol that defines the segment (the record has one
  // segment per defined global), or the common symbol's name.
  StringRef name;

  // Set by markLive() when the output keeps the chunk (plan 5.9.11). Without
  // --gc-sections every chunk that is not discarded is live.
  bool live = false;

  // True if the chunk's definition lost resolution and never reaches the
  // output: a member of a COMDAT group that an earlier input presented (plan
  // 5.9.5), or the method or segment of a definition that another input's
  // definition overrode (a WEAK copy beaten by a GLOBAL definition or a
  // common symbol). Token sites that name such a definition are redirected
  // to the winner (plan 5.9.7).
  bool discarded = false;

protected:
  InputChunk(Kind k, ObjFile *f, StringRef name)
      : file(f), name(name), chunkKind(k) {}

private:
  Kind chunkKind;
};

// One token operand of a method body: the offset of its four bytes in the
// code, the token in the object, and the instruction's opcode.
struct TokenSite {
  uint32_t offset;
  uint32_t token;
  llvm::cli::Opcode op;
};

// A method with a body: a MethodDef row of the object whose RVA is not 0.
// ObjFile::parse decodes the body (tiny or fat header, EH sections) and the
// code, and records every token operand (plan 5.9.7). The writer copies the
// code byte for byte and changes only the token operands, so code size,
// branch offsets, .maxstack and EH offsets never change.
class InputMethod : public InputChunk {
public:
  InputMethod(ObjFile *f, uint32_t token, StringRef name, uint32_t symbolIndex)
      : InputChunk(MethodKind, f, name), token(token),
        symbolIndex(symbolIndex) {}

  static bool classof(const InputChunk *c) { return c->kind() == MethodKind; }

  // The MethodDef token in the object.
  uint32_t token;
  // The SYMTAB index of the method's FUNCTION symbol (C1, C3: exactly one).
  uint32_t symbolIndex;
  // The MethodDef row's flags.
  llvm::cli::MethodAttributes flags{};
  llvm::cli::MethodImplAttributes implFlags{};
  // The MethodDef signature, as the object encodes it (its TypeRef values
  // are the object's tokens).
  const llvm::cli::MethodSig *signature = nullptr;
  // The SIGNATURES entry of the MethodDef (plan 5.8.8), or null for a
  // prototyped definition without sret or byval parameters.
  const llvm::MSILLink::SignatureInfo *signatureInfo = nullptr;
  // The decoded body: MaxStack, InitLocals, the local signature token (a
  // StandAloneSig of the object, or 0), the code (a view into the object)
  // and the EH clauses, whose catch class tokens are the object's.
  llvm::cli::MethodBodyView body;
  // The token operands of the code, in code order.
  SmallVector<TokenSite, 0> sites;
  // The output MethodDef token, for the writer.
  uint32_t outputToken = 0;
};

// One segment of the record's SEGMENTS (plan 5.8.8): the bytes of one
// defined global, with its output section name, flags and alignment, and the
// DATA_RELOCS entries whose sites lie in it.
class InputSegment : public InputChunk {
public:
  InputSegment(ObjFile *f, uint32_t index, StringRef name,
               const llvm::MSILLink::Segment &seg)
      : InputChunk(SegmentKind, f, name), index(index), sectionName(seg.Name),
        flags(seg.Flags), alignLog2(seg.AlignLog2), size(seg.Size),
        bytes(seg.Bytes) {}

  static bool classof(const InputChunk *c) { return c->kind() == SegmentKind; }

  bool isStrings() const { return flags & llvm::MSILLink::SegStrings; }
  bool isTLS() const { return flags & llvm::MSILLink::SegTLS; }
  bool isRetained() const { return flags & llvm::MSILLink::SegRetain; }
  bool isZero() const { return flags & llvm::MSILLink::SegZero; }
  bool isReadOnly() const { return flags & llvm::MSILLink::SegReadOnly; }

  // The SEGMENTS index in the object.
  uint32_t index;
  // The SYMTAB index of the non-alias DATA symbol that defines the segment,
  // or -1 if none does.
  int32_t symbolIndex = -1;
  // The COMDATS index of the group the segment belongs to, or -1.
  int32_t comdat = -1;
  // The output section name: the IR section verbatim ('$' kept), else
  // .rodata, .data, .bss, .tdata or .tbss.
  StringRef sectionName;
  uint32_t flags;
  uint8_t alignLog2;
  uint64_t size;
  // Size bytes, or empty for a ZERO segment. Relocation sites hold zeros.
  ArrayRef<uint8_t> bytes;
  // The DATA_RELOCS entries of this segment, sorted by offset. Their Symbol
  // (and Symbol2) fields are SYMTAB indexes: ObjFile::getSymbol() maps them.
  ArrayRef<llvm::MSILLink::Reloc> relocs;
  // The segment's offset in the output's data image, for the writer.
  int64_t outputOffset = -1;
};

// A unit of the data image that no record segment holds: a common symbol
// that survived resolution (plan 5.9.9 rule 3: in the zero part, at the
// SYMTAB position of its first declaration, with the merged size and
// alignment), or the address slots of the relocation loop (rule 4), which
// the writer makes.
class SyntheticSegment : public InputChunk {
public:
  enum SyntheticKind : uint8_t { Common, AddressSlots };

  SyntheticSegment(SyntheticKind k, ObjFile *f, StringRef name,
                   uint8_t alignLog2, uint64_t size)
      : InputChunk(SyntheticSegmentKind, f, name), syntheticKind(k),
        alignLog2(alignLog2), size(size) {}

  static bool classof(const InputChunk *c) {
    return c->kind() == SyntheticSegmentKind;
  }

  SyntheticKind syntheticKind;
  uint8_t alignLog2;
  uint64_t size;
  // Common: the SYMTAB index of the common's first declaration in `file`,
  // and the common symbol.
  uint32_t symbolIndex = 0;
  CommonSymbol *sym = nullptr;
  // The offset in the output's data image, for the writer.
  int64_t outputOffset = -1;
};
} // namespace lld::msil

namespace lld {
// Returns "<file>:(<name>)", as --print-gc-sections names a chunk.
std::string toString(const msil::InputChunk *c);
} // namespace lld

#endif
