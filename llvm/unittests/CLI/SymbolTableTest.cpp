//===- SymbolTableTest.cpp - P2-C symbol-table attribute codec ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/SymbolTable.h against the MSIL implementation plan (5.7.9,
// 5.9.15, M17.c item 8 and M22.a item 4): the payload of the
// '<msil>SymbolTable' attribute, written with 5.8.8's primitives. The test
// encodes payloads by hand, field by field: magic "MSYM", u8 major 1, u8
// minor 0, u8 flags (0x01 INTERFACE, M22), uleb ABI version, str triple, str
// C runtime identity, 4 x u8 data model, uleb n + str export classes, uleb n
// + data symbols (str name, uleb size, u8 log2 alignment, u8 flags: 1 const,
// 2 thread-local, 4 weak), uleb n + flagged functions (str name, u8 flags:
// 1 variadic, 2 weak, 4 ABI (M22), the last followed by the definition's
// SIGNATURES descriptor in 5.8.8's encoding: uleb CallFlags (NO_PROTOTYPE 1,
// VARARG 2, SRET 4, NATIVE_INCOMPATIBLE 8 from M20's amendment of 5.8.8),
// uleb FixedParams if VARARG, uleb SretSize and u8 SretAlignLog2 if SRET,
// uleb byval count, then per byval parameter uleb Param, uleb Size and u8
// AlignLog2). The test reads 'descriptor' as a
// SIGNATURES entry without its token: a library's table names the function,
// and the token would mean nothing to an importer. Every other flag bit is
// still unknown and refused.
// The model's members are not part of the contract, so payloads go through
// decodeSymbolTable and encodeSymbolTable as bytes; of M22's additions the
// test names only the flag constants of M22.a's Provides line
// (LibraryTableFlags::Interface, LibraryFunctionFlags::ABI). The plan leaves
// printSymbolTable's spelling of the two to M22.a, so the printer checks
// accept ' flags=0x01' or ' flags=INTERFACE' on the header line and look
// for the descriptor in the words of a link-info Signature line (5.8.11:
// 'NO_PROTOTYPE', 'SRET size=<n> align=<bytes>', 'NATIVE_INCOMPATIBLE',
// 'byval=<param>:<size>:<align>'). Expected values never come from the
// implementation under test.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/SymbolTable.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/CustomAttribute.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/PEWriter.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;
using testing::StartsWith;

namespace {

using ByteVec = std::vector<uint8_t>;

constexpr char TablePrefix[] = "invalid symbol table: ";

// The text of a name constant, whether it is a char array or a string type.
template <typename T> std::string text(const T &V) {
  return std::string(std::begin(V), std::end(V));
}
template <size_t N> std::string text(const char (&A)[N]) {
  return std::string(A, N > 0 && A[N - 1] == '\0' ? N - 1 : N);
}

// A payload, field by field.
struct Payload {
  ByteVec B;
  // Offsets of fields the tests change.
  size_t Major = 0, Flags = 0, FirstDataFlags = 0, FirstFunctionFlags = 0;

  void u8(uint8_t V) { B.push_back(V); }
  void uleb(uint64_t V) {
    do {
      uint8_t X = V & 0x7F;
      V >>= 7;
      B.push_back(V ? X | 0x80 : X);
    } while (V);
  }
  void str(StringRef S) {
    uleb(S.size());
    B.insert(B.end(), S.begin(), S.end());
  }
};

struct DataSym {
  const char *Name;
  uint64_t Size;
  uint8_t AlignLog2, Flags;
};
struct FlaggedFn {
  const char *Name;
  uint8_t Flags;
  // With ABI (0x04): the descriptor's bytes, written after the flags.
  ByteVec Descriptor = {};
};

Payload payload(uint8_t Flags, uint64_t ABIVersion, StringRef Triple,
                StringRef CRuntime, ArrayRef<uint8_t> DataModel,
                ArrayRef<const char *> Classes, ArrayRef<DataSym> Data,
                ArrayRef<FlaggedFn> Functions) {
  Payload P;
  P.B = {0x4D, 0x53, 0x59, 0x4D};
  P.Major = P.B.size();
  P.u8(1);
  P.u8(0);
  P.Flags = P.B.size();
  P.u8(Flags);
  P.uleb(ABIVersion);
  P.str(Triple);
  P.str(CRuntime);
  P.B.insert(P.B.end(), DataModel.begin(), DataModel.end());
  P.uleb(Classes.size());
  for (const char *C : Classes)
    P.str(C);
  P.uleb(Data.size());
  for (const DataSym &D : Data) {
    P.str(D.Name);
    P.uleb(D.Size);
    P.u8(D.AlignLog2);
    if (&D == Data.begin())
      P.FirstDataFlags = P.B.size();
    P.u8(D.Flags);
  }
  P.uleb(Functions.size());
  for (const FlaggedFn &F : Functions) {
    P.str(F.Name);
    if (&F == Functions.begin())
      P.FirstFunctionFlags = P.B.size();
    P.u8(F.Flags);
    P.B.insert(P.B.end(), F.Descriptor.begin(), F.Descriptor.end());
  }
  return P;
}

const uint8_t WinDataModel[] = {8, 4, 2, 8};
const DataSym SampleData[] = {{"counter", 4, 2, 0},
                              {"table", 300, 3, 1},      // const
                              {"tls_v", 8, 3, 2 | 4}};   // thread-local, weak
const FlaggedFn SampleFunctions[] = {{"printf_like", 1}, // variadic
                                     {"weak_fn", 2}};    // weak

Payload samplePayload() {
  const char *Classes[] = {"lib", "My.Exports"};
  return payload(0, 1, "x86_64-pc-windows-msvc-msil", "ucrtbase", WinDataModel,
                 Classes, SampleData, SampleFunctions);
}

ByteVec reencode(ArrayRef<uint8_t> Bytes) {
  Expected<LibrarySymbolTable> T = decodeSymbolTable(Bytes);
  EXPECT_THAT_EXPECTED(T, Succeeded());
  if (!T)
    return {};
  SmallVector<uint8_t, 0> Out;
  encodeSymbolTable(*T, Out);
  return ByteVec(Out.begin(), Out.end());
}

void expectTableError(ArrayRef<uint8_t> Bytes) {
  Expected<LibrarySymbolTable> T = decodeSymbolTable(Bytes);
  ASSERT_FALSE(bool(T)) << "decodeSymbolTable accepted the payload";
  EXPECT_THAT(toString(T.takeError()), StartsWith(TablePrefix));
}

//===----------------------------------------------------------------------===//
// Names and round trips
//===----------------------------------------------------------------------===//

TEST(CLISymbolTableTest, Names) {
  EXPECT_EQ(text(SymbolTableMagic), "MSYM");
  EXPECT_EQ(text(SymbolTableType), "<msil>SymbolTable");
}

// Every field written by hand comes back byte for byte.
TEST(CLISymbolTableTest, RoundTrip) {
  Payload P = samplePayload();
  // The hand-encoded layout's fixed part: magic, 1, 0, flags 0, ABI 1, the
  // 27-byte triple.
  const uint8_t Head[] = {0x4D, 0x53, 0x59, 0x4D, 0x01,
                          0x00, 0x00, 0x01, 0x1B, 'x'};
  ASSERT_EQ(ArrayRef<uint8_t>(P.B).take_front(sizeof(Head)),
            ArrayRef<uint8_t>(Head));
  EXPECT_EQ(reencode(P.B), P.B);
  // Equal tables give equal bytes.
  EXPECT_EQ(reencode(P.B), reencode(P.B));
}

TEST(CLISymbolTableTest, RoundTripVariants) {
  const uint8_t LinuxDataModel[] = {8, 8, 4, 8};
  const uint8_t Narrow[] = {4, 4, 2, 8};
  struct {
    const char *What;
    Payload P;
  } Cases[] = {
      {"empty lists, runtime none",
       payload(0, 1, "x86_64-unknown-linux-gnu-msil", "none", LinuxDataModel,
               {}, {}, {})},
      {"a multi-byte ABI version and sizes",
       payload(0, 300, "aarch64-unknown-linux-musl-msil", "c", LinuxDataModel,
               {"A"}, {{"big", 1ull << 40, 30, 0}}, {})},
      {"every data and function flag",
       payload(0, 1, "x86_64-w64-windows-gnu-msil", "msvcrt", Narrow, {"x.y.Z"},
               {{"a", 1, 0, 1}, {"b", 2, 1, 2}, {"c", 3, 2, 4}, {"d", 4, 3, 7}},
               {{"f", 1}, {"g", 2}, {"h", 3}})},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.What);
    EXPECT_EQ(reencode(C.P.B), C.P.B);
  }
}

//===----------------------------------------------------------------------===//
// printSymbolTable (section 12's row of 2026-10-23, printer spellings)
//===----------------------------------------------------------------------===//

// The printed lines of a payload, which must decode.
std::vector<std::string> printLines(ArrayRef<uint8_t> Bytes) {
  Expected<LibrarySymbolTable> T = decodeSymbolTable(Bytes);
  if (!T) {
    ADD_FAILURE() << "decodeSymbolTable refused the payload: "
                  << toString(T.takeError());
    return {};
  }
  std::string S;
  raw_string_ostream OS(S);
  printSymbolTable(*T, OS);
  OS.flush();
  EXPECT_TRUE(S.empty() || StringRef(S).ends_with("\n")) << "no final newline";
  std::vector<std::string> Lines;
  StringRef Text(S);
  while (!Text.empty()) {
    auto [Line, Rest] = Text.split('\n');
    Lines.push_back(Line.str());
    Text = Rest;
  }
  return Lines;
}

// In payload order: the header line (with a suffix only when the header
// flags are not 0: InterfacePrint below), one `ExportClass '<n>'` per
// export class, one `Data[ CONST][ TLS][ WEAK] '<n>' size=<n> align=<bytes>`
// per data symbol and one `Function[ VARARG][ WEAK] '<n>'` per flagged
// function.
TEST(CLISymbolTableTest, Print) {
  std::vector<std::string> Want = {
      "SymbolTable v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 "
      "runtime='ucrtbase' datamodel=8,4,2,8",
      "ExportClass 'lib'",
      "ExportClass 'My.Exports'",
      "Data 'counter' size=4 align=4",
      "Data CONST 'table' size=300 align=8",
      "Data TLS WEAK 'tls_v' size=8 align=8",
      "Function VARARG 'printf_like'",
      "Function WEAK 'weak_fn'"};
  EXPECT_EQ(printLines(samplePayload().B), Want);
}

TEST(CLISymbolTableTest, PrintVariants) {
  const uint8_t LinuxDataModel[] = {8, 8, 4, 8};
  const uint8_t Narrow[] = {4, 4, 2, 8};
  struct {
    const char *What;
    Payload P;
    std::vector<std::string> Want;
  } Cases[] = {
      {"empty lists",
       payload(0, 1, "x86_64-unknown-linux-gnu-msil", "none", LinuxDataModel,
               {}, {}, {}),
       {"SymbolTable v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 "
        "runtime='none' datamodel=8,8,4,8"}},
      {"a multi-byte ABI version and sizes",
       payload(0, 300, "aarch64-unknown-linux-musl-msil", "c", LinuxDataModel,
               {"A"}, {{"big", 1ull << 40, 30, 0}}, {}),
       {"SymbolTable v1.0 triple=aarch64-unknown-linux-musl-msil abi=300 "
        "runtime='c' datamodel=8,8,4,8",
        "ExportClass 'A'", "Data 'big' size=1099511627776 align=1073741824"}},
      {"every data and function flag",
       payload(0, 1, "x86_64-w64-windows-gnu-msil", "msvcrt", Narrow, {"x.y.Z"},
               {{"a", 1, 0, 1}, {"b", 2, 1, 2}, {"c", 3, 2, 4}, {"d", 4, 3, 7}},
               {{"f", 1}, {"g", 2}, {"h", 3}}),
       {"SymbolTable v1.0 triple=x86_64-w64-windows-gnu-msil abi=1 "
        "runtime='msvcrt' datamodel=4,4,2,8",
        "ExportClass 'x.y.Z'", "Data CONST 'a' size=1 align=1",
        "Data TLS 'b' size=2 align=2", "Data WEAK 'c' size=3 align=4",
        "Data CONST TLS WEAK 'd' size=4 align=8", "Function VARARG 'f'",
        "Function WEAK 'g'", "Function VARARG WEAK 'h'"}},
      // Quoting per 5.7.10: '\' -> '\\', ''' -> '\'', 0x01-0x1F and 0x7F ->
      // '\xHH'.
      {"quoted names",
       payload(0, 1, "x86_64-pc-windows-msvc-msil", "it's", WinDataModel,
               {"a\\b"}, {{"q'\x01", 1, 0, 0}}, {{"t\x7F", 2}}),
       {"SymbolTable v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 "
        "runtime='it\\'s' datamodel=8,4,2,8",
        "ExportClass 'a\\\\b'", "Data 'q\\'\\x01' size=1 align=1",
        "Function WEAK 't\\x7F'"}},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.What);
    EXPECT_EQ(printLines(C.P.B), C.Want);
  }
}

//===----------------------------------------------------------------------===//
// Errors
//===----------------------------------------------------------------------===//

TEST(CLISymbolTableTest, BadMagic) {
  Payload P = samplePayload();
  ByteVec B = P.B;
  B[3] = 'X';
  expectTableError(B);
  // The link-info record's magic is not a symbol table either.
  B = P.B;
  B[2] = 'L';
  B[3] = 'K';
  expectTableError(B);
}

TEST(CLISymbolTableTest, OtherMajorVersions) {
  Payload P = samplePayload();
  for (uint8_t Major : {0, 2, 0xFF}) {
    SCOPED_TRACE("major " + std::to_string(Major));
    ByteVec B = P.B;
    B[P.Major] = Major;
    expectTableError(B);
  }
}

// M22 defines the header flag INTERFACE (0x01) and the function flag ABI
// (0x04, followed by a descriptor; the ABI tests below); every other bit is
// unknown, alone or next to a known one.
TEST(CLISymbolTableTest, UnknownFlags) {
  Payload P = samplePayload();
  for (uint8_t F : {0x02, 0x04, 0x03, 0x80, 0x81}) {
    SCOPED_TRACE("header flag " + std::to_string(F));
    ByteVec B = P.B;
    B[P.Flags] = F;
    expectTableError(B);
  }
  for (uint8_t F : {0x08, 0x10, 0x80}) {
    SCOPED_TRACE("data symbol flag " + std::to_string(F));
    ByteVec B = P.B;
    B[P.FirstDataFlags] = F;
    expectTableError(B);
  }
  for (uint8_t F : {0x08, 0x09, 0x10, 0x80}) {
    SCOPED_TRACE("function flag " + std::to_string(F));
    ByteVec B = P.B;
    B[P.FirstFunctionFlags] = F;
    expectTableError(B);
  }
}

TEST(CLISymbolTableTest, TrailingBytes) {
  Payload P = samplePayload();
  for (uint8_t X : {0x00, 0x01}) {
    ByteVec B = P.B;
    B.push_back(X);
    expectTableError(B);
  }
}

TEST(CLISymbolTableTest, Truncation) {
  Payload P = samplePayload();
  for (size_t N = 0; N < P.B.size(); ++N) {
    SCOPED_TRACE("cut to " + std::to_string(N) + " bytes");
    expectTableError(ArrayRef<uint8_t>(P.B).take_front(N));
  }
}

// 5.8.8's primitives: a uleb longer than 10 bytes, and a str with a NUL.
TEST(CLISymbolTableTest, BadPrimitives) {
  Payload P = samplePayload();
  {
    SCOPED_TRACE("11-byte ABI version");
    ByteVec B = P.B;
    const size_t ABI = P.Flags + 1;
    ASSERT_EQ(B[ABI], 0x01);
    B[ABI] = 0x81;
    B.insert(B.begin() + ABI + 1,
             {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00});
    expectTableError(B);
  }
  {
    SCOPED_TRACE("NUL in a name");
    const char *Classes[] = {"lib"};
    const DataSym Data[] = {{"a\0b", 1, 0, 0}};
    Payload Q = payload(0, 1, "t", "none", WinDataModel, Classes, {}, {});
    // Replace the data-symbol count (0) by one symbol named "a\0b".
    ASSERT_EQ(Q.B[Q.B.size() - 2], 0x00);
    Q.B.resize(Q.B.size() - 2);
    Q.u8(1);
    Q.uleb(3);
    Q.B.insert(Q.B.end(), {'a', 0x00, 'b'});
    Q.uleb(Data[0].Size);
    Q.u8(0);
    Q.u8(0);
    Q.u8(0);
    expectTableError(Q.B);
  }
}

//===----------------------------------------------------------------------===//
// The carrier: Assembly-row attributes of a private '<msil>SymbolTable'
//===----------------------------------------------------------------------===//

// 5.9.15: the class is emitted into the library, its constructor takes
// uint8[], and the attribute (AllowMultiple) sits on the Assembly row.
TEST(CLISymbolTableTest, CarrierOnTheAssemblyRow) {
  Payload A = samplePayload();
  Payload B = payload(0, 1, "x86_64-pc-windows-msvc-msil", "ucrtbase",
                      WinDataModel, {"Other"}, {}, {});
  MetadataBuilder MB;
  MB.setModuleName("lib.dll");
  AssemblyIdentity Id;
  Id.Name = "lib";
  MB.setAssembly(Id);
  AssemblyRefInfo Core;
  Core.Name = "mscorlib";
  Core.Version = {4, 0, 0, 0};
  EntityHandle Attr =
      MB.addTypeRef(MB.addAssemblyRef(Core), "System", "Attribute");
  EntityHandle Class =
      MB.addTypeDef(TypeAttributes::NotPublic | TypeAttributes::Sealed, "",
                    text(SymbolTableType), Attr);
  const uint8_t CtorSig[] = {0x20, 0x01, 0x01, 0x1D, 0x05};
  EntityHandle Ctor = MB.addMethod(
      Class,
      MethodAttributes::Public | MethodAttributes::HideBySig |
          MethodAttributes::SpecialName | MethodAttributes::RTSpecialName,
      MethodImplAttributes::IL, ".ctor", MB.addBlob(CtorSig));
  EntityHandle AssemblyRow;
  AssemblyRow.Value = makeToken(TableId::Assembly, 1);
  for (const Payload *P : {&A, &B}) {
    SmallVector<uint8_t, 0> Blob;
    encodeByteArrayAttribute(P->B, Blob);
    MB.addCustomAttribute(AssemblyRow, Ctor, MB.addBlob(Blob));
  }
  MethodBodyStream Bodies;
  const uint8_t Ret[] = {0x2A};
  MethodBodyInfo Body;
  Body.Code = Ret;
  MB.setMethodBodyOffset(Ctor, Bodies.addBody(Body));
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());

  Expected<PEOptions> O = PEOptions::get(Triple("x86_64-pc-windows-msvc-msil"),
                                         PEImageKind::Library);
  ASSERT_THAT_EXPECTED(O, Succeeded());
  PEContent C;
  C.Metadata = &MB;
  C.ILStream = Bodies.getBytes();
  SmallVector<uint8_t, 0> File;
  ASSERT_THAT_ERROR(writePEImage(*O, C, File), Succeeded());
  Expected<std::unique_ptr<MetadataReader>> R = MetadataReader::create(File);
  ASSERT_THAT_EXPECTED(R, Succeeded());
  Expected<SmallVector<ArrayRef<uint8_t>, 1>> Payloads =
      (*R)->getByteArrayAttributes(makeToken(TableId::Assembly, 1), "",
                                   text(SymbolTableType));
  ASSERT_THAT_EXPECTED(Payloads, Succeeded());
  ASSERT_EQ(Payloads->size(), 2u);
  EXPECT_EQ((*Payloads)[0], ArrayRef<uint8_t>(A.B));
  EXPECT_EQ((*Payloads)[1], ArrayRef<uint8_t>(B.B));
  for (ArrayRef<uint8_t> P : *Payloads)
    EXPECT_EQ(reencode(P), ByteVec(P.begin(), P.end()));
}

//===----------------------------------------------------------------------===//
// M22: INTERFACE and the ABI descriptor (M22.a item 4)
//===----------------------------------------------------------------------===//

void appendULEB(ByteVec &B, uint64_t V) {
  do {
    uint8_t X = V & 0x7F;
    V >>= 7;
    B.push_back(V ? X | 0x80 : X);
  } while (V);
}

// A SIGNATURES descriptor (5.8.8) without its token.
struct Descriptor {
  enum : uint64_t {
    NoPrototype = 0x1,
    VarArg = 0x2,
    Sret = 0x4,
    NativeIncompatible = 0x8
  };
  struct Byval {
    uint64_t Param, Size;
    uint8_t AlignLog2;
  };
  uint64_t CallFlags = 0;
  uint64_t FixedParams = 0;
  uint64_t SretSize = 0;
  uint8_t SretAlignLog2 = 0;
  std::vector<Byval> Byvals;

  ByteVec bytes() const {
    ByteVec B;
    appendULEB(B, CallFlags);
    if (CallFlags & VarArg)
      appendULEB(B, FixedParams);
    if (CallFlags & Sret) {
      appendULEB(B, SretSize);
      B.push_back(SretAlignLog2);
    }
    appendULEB(B, Byvals.size());
    for (const Byval &V : Byvals) {
      appendULEB(B, V.Param);
      appendULEB(B, V.Size);
      B.push_back(V.AlignLog2);
    }
    return B;
  }
};

constexpr uint8_t InterfaceFlag = 0x01, WeakFn = 0x02, ABIFn = 0x04;

Descriptor sretDescriptor(uint64_t Size, uint8_t AlignLog2) {
  Descriptor D;
  D.CallFlags = Descriptor::Sret;
  D.SretSize = Size;
  D.SretAlignLog2 = AlignLog2;
  return D;
}

Descriptor byvalDescriptor(std::vector<Descriptor::Byval> Byvals) {
  Descriptor D;
  D.Byvals = std::move(Byvals);
  return D;
}

Descriptor noPrototypeDescriptor() {
  Descriptor D;
  D.CallFlags = Descriptor::NoPrototype;
  return D;
}

// A library table with one class, one variable and functions flagged ABI
// with every descriptor shape the plan names (SRET, byval, NO_PROTOTYPE),
// combinations of them, a weak one and multi-byte sizes.
std::vector<FlaggedFn> abiFunctions() {
  Descriptor SretByval = sretDescriptor(24, 3);
  SretByval.Byvals = {{1, 16, 3}};
  Descriptor KnrSret = sretDescriptor(12, 2);
  KnrSret.CallFlags |= Descriptor::NoPrototype;
  return {
      {"mk", ABIFn, sretDescriptor(12, 2).bytes()},
      {"take", ABIFn, byvalDescriptor({{0, 12, 2}}).bytes()},
      {"knr", ABIFn, noPrototypeDescriptor().bytes()},
      {"both", ABIFn, SretByval.bytes()},
      {"knr_sret", ABIFn, KnrSret.bytes()},
      {"weak_mk", WeakFn | ABIFn, sretDescriptor(8, 3).bytes()},
      {"wide", ABIFn,
       byvalDescriptor({{0, 300, 4}, {2, 1ull << 33, 0}, {200, 1, 0}}).bytes()},
      {"weak_only", WeakFn},
  };
}

Payload abiPayload(uint8_t HeaderFlags = 0) {
  const char *Classes[] = {"Contoso.Lib"};
  const DataSym Data[] = {{"counter", 4, 2, 0}};
  std::vector<FlaggedFn> Fns = abiFunctions();
  return payload(HeaderFlags, 1, "x86_64-pc-windows-msvc-msil", "ucrt",
                 WinDataModel, Classes, Data, Fns);
}

// The names of M22.a's Provides line carry the plan's values.
TEST(CLISymbolTableTest, M22FlagNames) {
  EXPECT_EQ(static_cast<uint8_t>(LibraryTableFlags::Interface), InterfaceFlag);
  EXPECT_EQ(static_cast<uint8_t>(LibraryFunctionFlags::ABI), ABIFn);
  EXPECT_EQ(static_cast<uint8_t>(LibraryFunctionFlags::Variadic), 0x01);
  EXPECT_EQ(static_cast<uint8_t>(LibraryFunctionFlags::Weak), WeakFn);
}

// INTERFACE comes back byte for byte, alone and with ABI functions.
TEST(CLISymbolTableTest, InterfaceRoundTrip) {
  Payload P = samplePayload();
  ByteVec B = P.B;
  B[P.Flags] = InterfaceFlag;
  EXPECT_EQ(reencode(B), B);
  Payload Q = abiPayload(InterfaceFlag);
  ASSERT_EQ(Q.B[Q.Flags], InterfaceFlag);
  EXPECT_EQ(reencode(Q.B), Q.B);
  // The flag is the only difference between a library and its interface.
  ByteVec Real = abiPayload().B;
  ASSERT_EQ(Real.size(), Q.B.size());
  for (size_t I = 0; I < Real.size(); ++I)
    if (I != Q.Flags)
      EXPECT_EQ(Real[I], Q.B[I]) << "byte " << I;
}

TEST(CLISymbolTableTest, InterfacePrint) {
  Payload P = samplePayload();
  ByteVec B = P.B;
  B[P.Flags] = InterfaceFlag;
  std::vector<std::string> Lines = printLines(B);
  ASSERT_EQ(Lines.size(), 8u);
  const std::string Head =
      "SymbolTable v1.0 triple=x86_64-pc-windows-msvc-msil "
      "abi=1 runtime='ucrtbase' datamodel=8,4,2,8";
  // The header flags print by name (section 12, 2026-10-27, '5.9.15 and
  // 5.8.11 clarified'), never as a number.
  EXPECT_EQ(Lines[0], Head + " flags=INTERFACE");
  // The rest is printed as without the flag.
  std::vector<std::string> Plain = printLines(P.B);
  ASSERT_EQ(Plain.size(), 8u);
  EXPECT_EQ(Plain[0], Head);
  for (size_t I = 1; I < Lines.size(); ++I)
    EXPECT_EQ(Lines[I], Plain[I]);
}

// Every descriptor comes back byte for byte: SRET, byval (one, several,
// multi-byte uleb fields), NO_PROTOTYPE, combinations, WEAK | ABI.
TEST(CLISymbolTableTest, ABIDescriptorRoundTrip) {
  Payload P = abiPayload();
  // The hand encoding of mk's entry: name, flags 04, CallFlags 04, SretSize
  // 0C, SretAlignLog2 02, no byval.
  const uint8_t Mk[] = {0x02, 'm', 'k', 0x04, 0x04, 0x0C, 0x02, 0x00};
  ASSERT_EQ(ArrayRef<uint8_t>(P.B).slice(P.FirstFunctionFlags - 3, sizeof(Mk)),
            ArrayRef<uint8_t>(Mk));
  EXPECT_EQ(reencode(P.B), P.B);
  for (const FlaggedFn &F : abiFunctions()) {
    SCOPED_TRACE(F.Name);
    const char *Classes[] = {"lib"};
    Payload One = payload(0, 1, "x86_64-unknown-linux-gnu-msil", "c",
                          {8, 8, 4, 8}, Classes, {}, {F});
    EXPECT_EQ(reencode(One.B), One.B);
  }
}

// One Function line per flagged function, in payload order, naming the
// function and spelling its descriptor as a link-info Signature line does:
// `Function[ VARARG][ WEAK][ ABI] '<n>'`, then after ABI the descriptor's
// items in the order `[ NO_PROTOTYPE][ VARARG fixed=<n>][ SRET size=<n>
// align=<bytes>][ NATIVE_INCOMPATIBLE][ byval=<param>:<size>:<align>]...`
// (section 12, 2026-10-27, '5.9.15 and 5.8.11 clarified'). Each line is
// compared whole.
TEST(CLISymbolTableTest, ABIDescriptorPrint) {
  std::vector<std::string> Want = {
      "SymbolTable v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 "
      "runtime='ucrt' datamodel=8,4,2,8",
      "ExportClass 'Contoso.Lib'", "Data 'counter' size=4 align=4",
      "Function ABI 'mk' SRET size=12 align=4",
      "Function ABI 'take' byval=0:12:4", "Function ABI 'knr' NO_PROTOTYPE",
      "Function ABI 'both' SRET size=24 align=8 byval=1:16:8",
      "Function ABI 'knr_sret' NO_PROTOTYPE SRET size=12 align=4",
      "Function WEAK ABI 'weak_mk' SRET size=8 align=8",
      "Function ABI 'wide' byval=0:300:16 byval=2:8589934592:1 byval=200:1:1",
      // A function without ABI keeps M17's line.
      "Function WEAK 'weak_only'"};
  EXPECT_EQ(printLines(abiPayload().B), Want);
}

// The descriptor is part of the payload: cut anywhere it is truncation, an
// unknown CallFlags bit is refused, and bytes after the last entry are
// trailing bytes. ABI without a descriptor runs into the end.
TEST(CLISymbolTableTest, ABIDescriptorErrors) {
  const char *Classes[] = {"lib"};
  Descriptor D = sretDescriptor(300, 2);
  D.Byvals = {{1, 12, 2}};
  Payload P = payload(0, 1, "x86_64-pc-windows-msvc-msil", "none", WinDataModel,
                      Classes, {}, {{"f", ABIFn, D.bytes()}});
  ASSERT_THAT_EXPECTED(decodeSymbolTable(P.B), Succeeded());
  for (size_t N = P.FirstFunctionFlags + 1; N < P.B.size(); ++N) {
    SCOPED_TRACE("cut to " + std::to_string(N) + " bytes");
    expectTableError(ArrayRef<uint8_t>(P.B).take_front(N));
  }
  {
    // 0x08 is NATIVE_INCOMPATIBLE (ABIDescriptorNativeIncompatible below).
    SCOPED_TRACE("unknown CallFlags bit 0x10");
    ByteVec B = P.B;
    ASSERT_EQ(B[P.FirstFunctionFlags + 1], 0x04);
    B[P.FirstFunctionFlags + 1] = 0x04 | 0x10;
    expectTableError(B);
  }
  {
    SCOPED_TRACE("a trailing byte after the descriptor");
    ByteVec B = P.B;
    B.push_back(0x00);
    expectTableError(B);
  }
  {
    SCOPED_TRACE("ABI and no descriptor");
    Payload Q = payload(0, 1, "x86_64-pc-windows-msvc-msil", "none",
                        WinDataModel, Classes, {}, {{"f", ABIFn}});
    expectTableError(Q.B);
  }
}

// M20's amendment of 5.8.8 adds the CallFlags bit NATIVE_INCOMPATIBLE (0x8)
// to the SIGNATURES entry of a definition whose IR type is not a native
// scalar (an i128 result, an aggregate returned through a hidden pointer).
// A library's table copies the definition's descriptor, so the bit is known:
// alone and next to SRET and byval it decodes, comes back byte for byte and
// prints as a link-info Signature line spells it.
TEST(CLISymbolTableTest, ABIDescriptorNativeIncompatible) {
  Descriptor Wide;
  Wide.CallFlags = Descriptor::NativeIncompatible;
  Descriptor Pair = sretDescriptor(16, 3);
  Pair.CallFlags |= Descriptor::NativeIncompatible;
  Pair.Byvals = {{1, 16, 3}};
  const char *Classes[] = {"lib"};
  Payload P = payload(0, 1, "x86_64-pc-windows-msvc-msil", "none", WinDataModel,
                      Classes, {},
                      {{"add128", ABIFn, Wide.bytes()},
                       {"mk_pair", WeakFn | ABIFn, Pair.bytes()}});
  // The hand encoding: add128's name, flags 04, CallFlags 08, no byval; then
  // mk_pair's name, flags 06, CallFlags 0C, SretSize 10, SretAlignLog2 03,
  // one byval (Param 01, Size 10, AlignLog2 03).
  const uint8_t Entries[] = {
      0x06, 'a', 'd', 'd', '1', '2',  '8',  0x04, 0x08, 0x00, 0x07, 'm',  'k',
      '_',  'p', 'a', 'i', 'r', 0x06, 0x0C, 0x10, 0x03, 0x01, 0x01, 0x10, 0x03};
  ASSERT_GE(P.B.size(), sizeof(Entries));
  ASSERT_EQ(ArrayRef<uint8_t>(P.B).take_back(sizeof(Entries)),
            ArrayRef<uint8_t>(Entries));
  ASSERT_THAT_EXPECTED(decodeSymbolTable(P.B), Succeeded());
  EXPECT_EQ(reencode(P.B), P.B);

  // Whole lines, as ABIDescriptorPrint: NATIVE_INCOMPATIBLE after SRET and
  // before the byval items, never an unknown bit in hex.
  std::vector<std::string> Want = {
      "SymbolTable v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 "
      "runtime='none' datamodel=8,4,2,8",
      "ExportClass 'lib'", "Function ABI 'add128' NATIVE_INCOMPATIBLE",
      "Function WEAK ABI 'mk_pair' SRET size=16 align=8 NATIVE_INCOMPATIBLE "
      "byval=1:16:8"};
  EXPECT_EQ(printLines(P.B), Want);
}

} // namespace
