//===- SymbolTableTest.cpp - P2-C symbol-table attribute codec ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/SymbolTable.h against the MSIL implementation plan (5.7.9,
// 5.9.15 and M17.c item 8): the payload of the '<msil>SymbolTable'
// attribute, written with 5.8.8's primitives. The test encodes payloads by
// hand, field by field: magic "MSYM", u8 major 1, u8 minor 0, u8 flags, uleb
// ABI version, str triple, str C runtime identity, 4 x u8 data model, uleb n
// + str export classes, uleb n + data symbols (str name, uleb size, u8 log2
// alignment, u8 flags: 1 const, 2 thread-local, 4 weak), uleb n + flagged
// functions (str name, u8 flags: 1 variadic, 2 weak). Until M22 the header
// flag INTERFACE (0x01) and the function flag ABI (0x04) are unknown flags.
// The model's members are not part of the contract, so payloads go through
// decodeSymbolTable and encodeSymbolTable as bytes. Expected values never
// come from the implementation under test.
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

// In payload order: the header line (` flags=0x<2X>` only when the header
// flags are not 0, which M17's decoder refuses), one `ExportClass '<n>'` per
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

// INTERFACE (0x01) and ABI (0x04) arrive with M22; until then they are
// unknown flags like any other.
TEST(CLISymbolTableTest, UnknownFlags) {
  Payload P = samplePayload();
  for (uint8_t F : {0x01, 0x02, 0x80}) {
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
  for (uint8_t F : {0x04, 0x08, 0x80}) {
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

} // namespace
