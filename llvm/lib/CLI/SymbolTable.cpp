//===- SymbolTable.cpp - MSIL library symbol table ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the codec and the printer of the symbol-table
// attribute of libraries (plan sections 5.7.9 and 5.9.15), written with the
// link-info record's primitives.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/SymbolTable.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/LinkInfo.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>
#include <utility>

#define DEBUG_TYPE "cli-symboltable"

using namespace llvm;
using namespace llvm::cli;

/// The largest log2 alignment, as for the link-info record's segments.
static constexpr unsigned MaxAlignLog2 = 30;

static std::string hex(uint64_t V, unsigned Digits) {
  return "0x" + utohexstr(V, /*LowerCase=*/false, Digits);
}

/// A name in the canonical listing's quoting (5.7.10).
static void printQuoted(raw_ostream &OS, StringRef S) {
  OS << '\'';
  for (unsigned char C : S) {
    if (C == '\\')
      OS << "\\\\";
    else if (C == '\'')
      OS << "\\'";
    else if (C < 0x20 || C == 0x7F)
      OS << "\\x" << hexdigit(C >> 4) << hexdigit(C & 0xF);
    else
      OS << C;
  }
  OS << '\'';
}

void cli::encodeSymbolTable(const LibrarySymbolTable &T,
                            SmallVectorImpl<uint8_t> &Out) {
  RecordWriter W(Out);
  W.writeBytes(arrayRefFromStringRef(
      StringRef(SymbolTableMagic, sizeof(SymbolTableMagic))));
  W.writeU8(T.Major);
  W.writeU8(T.Minor);
  W.writeU8(T.Flags);
  W.writeULEB(T.ABIVersion);
  W.writeString(T.Triple);
  W.writeString(T.CRuntime);
  W.writeU8(T.PointerSize);
  W.writeU8(T.LongSize);
  W.writeU8(T.WCharSize);
  W.writeU8(T.LongDoubleSize);
  W.writeULEB(T.ExportClasses.size());
  for (const std::string &C : T.ExportClasses)
    W.writeString(C);
  W.writeULEB(T.DataSymbols.size());
  for (const LibraryDataSymbol &D : T.DataSymbols) {
    W.writeString(D.Name);
    W.writeULEB(D.Size);
    W.writeU8(D.AlignLog2);
    W.writeU8(D.Flags);
  }
  W.writeULEB(T.Functions.size());
  for (const LibraryFunctionFlags &F : T.Functions) {
    W.writeString(F.Name);
    W.writeU8(F.Flags);
  }
}

/// Reads a count of entries of at least one byte each.
static uint32_t readCount(RecordReader &Rd, const Twine &Field) {
  uint64_t Off = Rd.getOffset();
  uint32_t N = Rd.readULEB32(Field);
  if (!Rd.hasError() && N > Rd.bytesLeft())
    Rd.setError(Field, Off,
                Twine(N) + " entries cannot fit in the " +
                    Twine(Rd.bytesLeft()) + " bytes left");
  return Rd.hasError() ? 0 : N;
}

/// Reads a flags byte and refuses bits outside \p Known.
static uint8_t readFlags(RecordReader &Rd, const Twine &Field, uint8_t Known) {
  uint64_t Off = Rd.getOffset();
  uint8_t Flags = Rd.readU8(Field);
  if (!Rd.hasError() && (Flags & ~Known))
    Rd.setUnsupported(Field + " at offset " + Twine(Off) + ": unknown flags " +
                      hex(Flags & ~Known, 2) + " (this toolchain knows " +
                      hex(Known, 2) + ")");
  return Flags;
}

Expected<LibrarySymbolTable> cli::decodeSymbolTable(ArrayRef<uint8_t> Bytes) {
  RecordReader Rd(Bytes, "symbol table");
  LibrarySymbolTable T;
  ArrayRef<uint8_t> Magic = Rd.readBytes(4, "magic");
  if (!Rd.hasError() &&
      Magic != arrayRefFromStringRef(
                   StringRef(SymbolTableMagic, sizeof(SymbolTableMagic))))
    Rd.setError("magic", 0, "expected 4D 53 59 4D (\"MSYM\")");
  T.Major = Rd.readU8("major version");
  if (!Rd.hasError() && T.Major != SymbolTableMajorVersion) {
    if (T.Major > SymbolTableMajorVersion)
      Rd.setUnsupported("version " + Twine(unsigned(T.Major)) +
                        " is newer than this toolchain supports (" +
                        Twine(unsigned(SymbolTableMajorVersion)) + ")");
    else
      Rd.setError("major version", 4,
                  "version " + Twine(unsigned(T.Major)) +
                      " is not supported (this toolchain reads version " +
                      Twine(unsigned(SymbolTableMajorVersion)) + ")");
  }
  T.Minor = Rd.readU8("minor version");
  T.Flags = readFlags(Rd, "flags", LibrarySymbolTable::KnownFlags);
  T.ABIVersion = Rd.readULEB32("ABI version");
  T.Triple = Rd.readString("triple").str();
  T.CRuntime = Rd.readString("C runtime identity").str();
  T.PointerSize = Rd.readU8("data model (pointer size)");
  T.LongSize = Rd.readU8("data model (long size)");
  T.WCharSize = Rd.readU8("data model (wchar_t size)");
  T.LongDoubleSize = Rd.readU8("data model (long double size)");

  uint32_t N = readCount(Rd, "export class count");
  T.ExportClasses.reserve(N);
  for (uint32_t I = 0; I < N && !Rd.hasError(); ++I)
    T.ExportClasses.push_back(Rd.readString("export class " + Twine(I)).str());

  N = readCount(Rd, "data symbol count");
  T.DataSymbols.reserve(N);
  for (uint32_t I = 0; I < N && !Rd.hasError(); ++I) {
    const std::string F = "data symbol " + std::to_string(I);
    LibraryDataSymbol D;
    D.Name = Rd.readString(F + " name").str();
    D.Size = Rd.readULEB(F + " size");
    uint64_t AlignOff = Rd.getOffset();
    D.AlignLog2 = Rd.readU8(F + " alignment");
    if (!Rd.hasError() && D.AlignLog2 > MaxAlignLog2)
      Rd.setError(F + " alignment", AlignOff,
                  "alignment 2^" + Twine(unsigned(D.AlignLog2)) +
                      " is above 2^" + Twine(MaxAlignLog2));
    D.Flags = readFlags(Rd, F + " flags", LibraryDataSymbol::KnownFlags);
    T.DataSymbols.push_back(std::move(D));
  }

  N = readCount(Rd, "function count");
  T.Functions.reserve(N);
  for (uint32_t I = 0; I < N && !Rd.hasError(); ++I) {
    const std::string F = "function " + std::to_string(I);
    LibraryFunctionFlags Fn;
    Fn.Name = Rd.readString(F + " name").str();
    Fn.Flags = readFlags(Rd, F + " flags", LibraryFunctionFlags::KnownFlags);
    T.Functions.push_back(std::move(Fn));
  }
  Rd.expectEnd("the end of the symbol table");
  if (Rd.hasError())
    return Rd.takeError();
  return std::move(T);
}

/// Prints the names of the set bits of \p Flags, each after a blank, and the
/// unknown rest as hex.
static void printFlags(raw_ostream &OS, uint8_t Flags,
                       ArrayRef<std::pair<uint8_t, const char *>> Names) {
  for (const auto &[Bit, Name] : Names)
    if (Flags & Bit) {
      OS << ' ' << Name;
      Flags &= ~Bit;
    }
  if (Flags)
    OS << ' ' << hex(Flags, 2);
}

void cli::printSymbolTable(const LibrarySymbolTable &T, raw_ostream &OS) {
  OS << "SymbolTable v" << unsigned(T.Major) << '.' << unsigned(T.Minor)
     << " triple=" << T.Triple << " abi=" << T.ABIVersion << " runtime=";
  printQuoted(OS, T.CRuntime);
  OS << " datamodel=" << unsigned(T.PointerSize) << ',' << unsigned(T.LongSize)
     << ',' << unsigned(T.WCharSize) << ',' << unsigned(T.LongDoubleSize);
  if (T.Flags)
    OS << " flags=" << hex(T.Flags, 2);
  OS << '\n';
  for (const std::string &C : T.ExportClasses) {
    OS << "ExportClass ";
    printQuoted(OS, C);
    OS << '\n';
  }
  static constexpr std::pair<uint8_t, const char *> DataFlagNames[] = {
      {LibraryDataSymbol::Const, "CONST"},
      {LibraryDataSymbol::ThreadLocal, "TLS"},
      {LibraryDataSymbol::Weak, "WEAK"}};
  for (const LibraryDataSymbol &D : T.DataSymbols) {
    OS << "Data";
    printFlags(OS, D.Flags, DataFlagNames);
    OS << ' ';
    printQuoted(OS, D.Name);
    OS << " size=" << D.Size << " align=";
    if (D.AlignLog2 < 64)
      OS << (uint64_t(1) << D.AlignLog2);
    else
      OS << "2^" << unsigned(D.AlignLog2);
    OS << '\n';
  }
  static constexpr std::pair<uint8_t, const char *> FunctionFlagNames[] = {
      {LibraryFunctionFlags::Variadic, "VARARG"},
      {LibraryFunctionFlags::Weak, "WEAK"}};
  for (const LibraryFunctionFlags &F : T.Functions) {
    OS << "Function";
    printFlags(OS, F.Flags, FunctionFlagNames);
    OS << ' ';
    printQuoted(OS, F.Name);
    OS << '\n';
  }
}
