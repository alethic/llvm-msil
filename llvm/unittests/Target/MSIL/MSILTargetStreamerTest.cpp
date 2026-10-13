//===- MSILTargetStreamerTest.cpp - Tests for the MSIL target streamer ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The ILAsm text of the MSIL target streamer and of MSILMemberRefExpr, checked
// line by line against the output contract (ImplementationPlan.md 5.1,
// decisions D10, D11 and D29, and the complete example of recon asmprinter.md
// section 9) through the API of plan 5.3. The asm streamer is created through
// the target registry, as llc creates it: an MCAsmStreamer with the MSIL
// InstPrinter and the registered MSIL asm target streamer, writing to a
// raw_svector_ostream.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILTargetStreamer.h"
#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using MSIL::Signature;
using MSIL::ValType;

namespace {

Signature makeSig(ArrayRef<ValType> Results, ArrayRef<ValType> Params) {
  Signature S;
  S.Results.append(Results.begin(), Results.end());
  S.Params.append(Params.begin(), Params.end());
  return S;
}

MSILMethodDecl makeDecl(StringRef Name, const Signature &Sig, bool Public) {
  MSILMethodDecl M;
  M.Name = Name;
  M.Sig = &Sig;
  M.Public = Public;
  return M;
}

class MSILTargetStreamerTest : public testing::Test {
protected:
  static void SetUpTestSuite() {
    LLVMInitializeMSILTargetInfo();
    LLVMInitializeMSILTargetMC();
  }

  void SetUp() override {
    std::string Error;
    TheTarget = TargetRegistry::lookupTarget(TT, Error);
    ASSERT_NE(nullptr, TheTarget) << Error;
    MRI.reset(TheTarget->createMCRegInfo(TT));
    ASSERT_TRUE(MRI);
    MAI.reset(TheTarget->createMCAsmInfo(*MRI, TT, MCOptions));
    ASSERT_TRUE(MAI);
    MII.reset(TheTarget->createMCInstrInfo());
    ASSERT_TRUE(MII);
    STI.reset(TheTarget->createMCSubtargetInfo(TT, "", ""));
    ASSERT_TRUE(STI);
    Ctx = std::make_unique<MCContext>(TT, *MAI, *MRI, *STI);
    MOFI.reset(TheTarget->createMCObjectFileInfo(*Ctx, /*PIC=*/false));
    Ctx->setObjectFileInfo(MOFI.get());

    std::unique_ptr<MCInstPrinter> IP(TheTarget->createMCInstPrinter(
        TT, MAI->getAssemblerDialect(), *MAI, *MII, *MRI));
    ASSERT_TRUE(IP);
    auto FOS = std::make_unique<formatted_raw_ostream>(OS);
    FOut = FOS.get();
    Streamer.reset(TheTarget->createAsmStreamer(
        *Ctx, std::move(FOS), std::move(IP), nullptr, nullptr));
    ASSERT_TRUE(Streamer);
    ASSERT_NE(nullptr, Streamer->getTargetStreamer());
    TS = static_cast<MSILTargetStreamer *>(Streamer->getTargetStreamer());
  }

  // Everything written since the last call, without trailing blank lines.
  // Plan 5.1 separates the module's parts with blank lines (after the
  // .assembly line, after each method, between the trailer sections); which
  // call writes a separator is layout, not contract, so only the lines
  // themselves are compared. A blank line anywhere else still shows up.
  std::string take() {
    FOut->flush();
    std::string S(Buffer.str());
    Buffer.clear();
    while (StringRef(S).ends_with("\n\n"))
      S.pop_back();
    return S;
  }

  // The ILAsm text of a member-reference expression, as MSILInstPrinter
  // prints an expression operand.
  std::string print(const MCExpr *E) {
    std::string S;
    raw_string_ostream SOS(S);
    MAI->printExpr(SOS, *E);
    return S;
  }

  Triple TT{"x86_64-pc-windows-msvc-msil"};
  const Target *TheTarget = nullptr;
  MCTargetOptions MCOptions;
  std::unique_ptr<MCRegisterInfo> MRI;
  std::unique_ptr<MCAsmInfo> MAI;
  std::unique_ptr<MCInstrInfo> MII;
  std::unique_ptr<MCSubtargetInfo> STI;
  std::unique_ptr<MCContext> Ctx;
  std::unique_ptr<MCObjectFileInfo> MOFI;
  SmallString<1024> Buffer;
  raw_svector_ostream OS{Buffer};
  formatted_raw_ostream *FOut = nullptr;
  std::unique_ptr<MCStreamer> Streamer;
  MSILTargetStreamer *TS = nullptr;
};

//===----------------------------------------------------------------------===//
// Module header (plan 5.1, decision D10)
//===----------------------------------------------------------------------===//

TEST_F(MSILTargetStreamerTest, MSILFileHeader) {
  TS->emitFileHeader(/*BOM=*/false, "prog.c", "x86_64-pc-windows-msvc-msil");
  EXPECT_EQ("// MSIL module generated by LLVM\n"
            "// source_filename = \"prog.c\"\n"
            "// target triple   = \"x86_64-pc-windows-msvc-msil\"\n",
            take());
}

TEST_F(MSILTargetStreamerTest, MSILFileHeaderWithBOM) {
  // Decision D11: a UTF-8 byte order mark when a global name is not ASCII.
  TS->emitFileHeader(/*BOM=*/true, "prog.c", "x86_64-unknown-linux-gnu-msil");
  EXPECT_EQ("\xEF\xBB\xBF"
            "// MSIL module generated by LLVM\n"
            "// source_filename = \"prog.c\"\n"
            "// target triple   = \"x86_64-unknown-linux-gnu-msil\"\n",
            take());
}

TEST_F(MSILTargetStreamerTest, MSILAssemblyRefsAndAssembly) {
  TS->emitAssemblyRefs();
  EXPECT_EQ(".assembly extern mscorlib { .publickeytoken = "
            "(B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }\n",
            take());
  TS->emitAssembly("prog");
  EXPECT_EQ(".assembly 'prog' { .ver 0:0:0:0 }\n", take());
}

//===----------------------------------------------------------------------===//
// Method definitions (plan 5.1, decision D29)
//===----------------------------------------------------------------------===//

TEST_F(MSILTargetStreamerTest, MSILMethodBeginPublic) {
  Signature Sum = makeSig({ValType::I32}, {ValType::NativeInt, ValType::I32});
  TS->emitMethodBegin(makeDecl("sum", Sum, /*Public=*/true));
  EXPECT_EQ(".method public static int32 'sum'(native int, int32) cil managed\n"
            "{\n",
            take());

  Signature NoParams = makeSig({ValType::NativeInt}, {});
  TS->emitMethodBegin(makeDecl("main", NoParams, /*Public=*/true));
  EXPECT_EQ(".method public static native int 'main'() cil managed\n"
            "{\n",
            take());
}

TEST_F(MSILTargetStreamerTest, MSILMethodBeginNonPublic) {
  // Internal and private linkage give 'assembly', never 'private'.
  Signature Void = makeSig({}, {});
  TS->emitMethodBegin(makeDecl("helper", Void, /*Public=*/false));
  EXPECT_EQ(".method assembly static void 'helper'() cil managed\n"
            "{\n",
            take());

  Signature FP = makeSig({ValType::F64}, {ValType::F32, ValType::F64});
  TS->emitMethodBegin(makeDecl("add", FP, /*Public=*/false));
  EXPECT_EQ(".method assembly static float64 'add'(float32, float64) cil "
            "managed\n"
            "{\n",
            take());
}

TEST_F(MSILTargetStreamerTest, MSILMethodNamesAreQuoted) {
  // Decision D11: the name is quoted and escaped by MSIL::printQuotedName, in
  // the header and in the end-of-method comment alike.
  Signature Sig = makeSig({ValType::I32}, {ValType::I32});
  TS->emitMethodBegin(makeDecl("it's", Sig, /*Public=*/true));
  EXPECT_EQ(".method public static int32 'it\\'s'(int32) cil managed\n"
            "{\n",
            take());
  TS->emitMethodEnd("it's");
  EXPECT_EQ("} // end of method 'it\\'s'\n", take());

  // A control character never breaks the end-of-method comment line.
  TS->emitMethodEnd("a\nb");
  EXPECT_EQ("} // end of method 'a\\012b'\n", take());
}

TEST_F(MSILTargetStreamerTest, MSILMaxStack) {
  TS->emitMaxStack(4);
  EXPECT_EQ("  .maxstack 4\n", take());
  TS->emitMaxStack(0);
  EXPECT_EQ("  .maxstack 0\n", take());
  TS->emitMaxStack(1000);
  EXPECT_EQ("  .maxstack 1000\n", take());
}

TEST_F(MSILTargetStreamerTest, MSILLocals) {
  TS->emitLocals({ValType::I32, ValType::I32});
  std::string Text = take();
  EXPECT_EQ("  .locals (int32, int32)\n", Text);
  EXPECT_EQ(std::string::npos, Text.find("init"));

  TS->emitLocals(
      {ValType::NativeInt, ValType::F32, ValType::F64, ValType::I32});
  Text = take();
  EXPECT_EQ("  .locals (native int, float32, float64, int32)\n", Text);
  EXPECT_EQ(std::string::npos, Text.find("init"));

  TS->emitLocals({ValType::NativeInt});
  EXPECT_EQ("  .locals (native int)\n", take());
}

TEST_F(MSILTargetStreamerTest, MSILLocalsEmptyPrintsNothing) {
  // Not even a blank line.
  TS->emitLocals({});
  EXPECT_EQ("", take());
}

TEST_F(MSILTargetStreamerTest, MSILEntryPoint) {
  TS->emitEntryPoint();
  EXPECT_EQ("  .entrypoint\n", take());
}

TEST_F(MSILTargetStreamerTest, MSILMethodEnd) {
  TS->emitMethodEnd("sum");
  EXPECT_EQ("} // end of method 'sum'\n", take());
  TS->emitMethodEnd("<msil>Main");
  EXPECT_EQ("} // end of method '<msil>Main'\n", take());
}

TEST_F(MSILTargetStreamerTest, MSILMethodFrame) {
  // The method 'sum' of plan 5.1 without its body.
  Signature Sum = makeSig({ValType::I32}, {ValType::NativeInt, ValType::I32});
  TS->emitMethodBegin(makeDecl("sum", Sum, /*Public=*/true));
  TS->emitMaxStack(4);
  TS->emitLocals({ValType::I32, ValType::I32});
  TS->emitMethodEnd("sum");
  EXPECT_EQ(".method public static int32 'sum'(native int, int32) cil managed\n"
            "{\n"
            "  .maxstack 4\n"
            "  .locals (int32, int32)\n"
            "} // end of method 'sum'\n",
            take());
}

//===----------------------------------------------------------------------===//
// P/Invoke declarations (plan 5.1, decision D14)
//===----------------------------------------------------------------------===//

TEST_F(MSILTargetStreamerTest, MSILPInvoke) {
  Signature Puts = makeSig({ValType::I32}, {ValType::NativeInt});
  TS->emitPInvoke(makeDecl("puts", Puts, /*Public=*/true), "ucrtbase", "puts");
  EXPECT_EQ(".method public hidebysig static pinvokeimpl(\"ucrtbase\" cdecl) "
            "int32 'puts'(native int) cil managed preservesig {}\n",
            take());

  Signature Sqrt = makeSig({ValType::F64}, {ValType::F64});
  TS->emitPInvoke(makeDecl("sqrt", Sqrt, /*Public=*/true), "libm.so.6", "sqrt");
  EXPECT_EQ(".method public hidebysig static pinvokeimpl(\"libm.so.6\" cdecl) "
            "float64 'sqrt'(float64) cil managed preservesig {}\n",
            take());
}

TEST_F(MSILTargetStreamerTest, MSILPInvokeWithEntryPoint) {
  // An entry point that differs from the method name (msil-import-name) is
  // named with 'as'.
  Signature Puts = makeSig({ValType::I32}, {ValType::NativeInt});
  TS->emitPInvoke(makeDecl("msvcrt_puts", Puts, /*Public=*/true), "msvcrt",
                  "puts");
  EXPECT_EQ(".method public hidebysig static "
            "pinvokeimpl(\"msvcrt\" as \"puts\" cdecl) "
            "int32 'msvcrt_puts'(native int) cil managed preservesig {}\n",
            take());
}

//===----------------------------------------------------------------------===//
// Data image (plan 5.1, decision D12, recon asmprinter.md section 9)
//===----------------------------------------------------------------------===//

// Checks the byte lines of the bytearray of \p Label in \p Text against
// \p Bytes: two-space indent, 16 bytes per line as two uppercase hex digits
// separated by single spaces, the closing ')' after the last byte, and every
// line's offset comment '// 0x<4 hex digits>' at the column where a full line
// puts it (2 + 47 + 3).
void checkByteLines(StringRef Text, StringRef Label, ArrayRef<uint8_t> Bytes) {
  std::string Open = (".data cil '" + Label + "' = bytearray (\n").str();
  size_t Begin = Text.find(Open);
  ASSERT_NE(StringRef::npos, Begin) << Text.str();
  Begin += Open.size();
  size_t End = Text.find("\n.field ", Begin);
  ASSERT_NE(StringRef::npos, End) << Text.str();
  SmallVector<StringRef, 8> Lines;
  Text.slice(Begin, End).split(Lines, '\n');
  ASSERT_EQ((Bytes.size() + 15) / 16, Lines.size()) << Text.str();
  for (size_t I = 0; I < Lines.size(); ++I) {
    StringRef Line = Lines[I];
    SCOPED_TRACE(Line.str());
    ArrayRef<uint8_t> Chunk = Bytes.slice(16 * I).take_front(16);
    std::string Expected = " ";
    for (uint8_t B : Chunk)
      Expected += " " + utohexstr(B, /*LowerCase=*/false, /*Width=*/2);
    if (I + 1 == Lines.size())
      Expected += " )";
    EXPECT_EQ(52u, Line.find("//"));
    EXPECT_EQ(Expected, Line.take_front(52).rtrim(' ').str());
    std::string Comment;
    raw_string_ostream CS(Comment);
    CS << "// 0x" << format_hex_no_prefix(16 * I, 4, /*Upper=*/true);
    EXPECT_EQ(Comment, Line.drop_front(52).str());
  }
}

TEST_F(MSILTargetStreamerTest, MSILDataImage) {
  // The data segment of the complete example (recon asmprinter.md section 9):
  // table, "zero", "one", names (relocated, zero in the image), op (relocated)
  // and "hello from msil", 72 initialized bytes.
  std::vector<uint8_t> Bytes = {
      0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 'z',  'e',  'r',  'o',
      0x00, 'o',  'n',  'e',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 'h',  'e',  'l',  'l',
      'o',  ' ',  'f',  'r',  'o',  'm',  ' ',  'm',  's',  'i',  'l',  0x00};
  ASSERT_EQ(72u, Bytes.size());
  TS->emitDataImage("<msil>image_t", "<msil>image", "<msil>image_d", Bytes);
  std::string Text = take();
  EXPECT_EQ(".class private explicit ansi sealed '<msil>image_t' extends "
            "[mscorlib]System.ValueType\n"
            "{\n"
            "  .pack 1\n"
            "  .size 72\n"
            "}\n"
            ".data cil '<msil>image_d' = bytearray (\n"
            "  05 00 00 00 03 00 00 00 09 00 00 00 01 00 00 00   // 0x0000\n"
            "  07 00 00 00 7A 65 72 6F 00 6F 6E 65 00 00 00 00   // 0x0010\n"
            "  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   // 0x0020\n"
            "  00 00 00 00 00 00 00 00 68 65 6C 6C 6F 20 66 72   // 0x0030\n"
            "  6F 6D 20 6D 73 69 6C 00 )                         // 0x0040\n"
            ".field private static valuetype '<msil>image_t' '<msil>image' "
            "at '<msil>image_d'\n",
            Text);
  checkByteLines(Text, "<msil>image_d", Bytes);
}

TEST_F(MSILTargetStreamerTest, MSILDataImageShortImage) {
  // Fewer than 16 bytes, including bytes >= 0x80: one line, uppercase hex,
  // the offset comment at the column of a full line.
  std::vector<uint8_t> Bytes = {0x00, 0x7F, 0x80, 0xAB, 0xFF};
  TS->emitDataImage("<msil>image_t", "<msil>image", "<msil>image_d", Bytes);
  std::string Text = take();
  EXPECT_EQ(".class private explicit ansi sealed '<msil>image_t' extends "
            "[mscorlib]System.ValueType\n"
            "{\n"
            "  .pack 1\n"
            "  .size 5\n"
            "}\n"
            ".data cil '<msil>image_d' = bytearray (\n"
            "  00 7F 80 AB FF )                                  // 0x0000\n"
            ".field private static valuetype '<msil>image_t' '<msil>image' "
            "at '<msil>image_d'\n",
            Text);
  checkByteLines(Text, "<msil>image_d", Bytes);
}

TEST_F(MSILTargetStreamerTest, MSILDataImageWholeLines) {
  // A multiple of 16 bytes: the ')' still follows the last byte and the
  // offset comments stay in one column.
  std::vector<uint8_t> Bytes;
  for (unsigned I = 0; I < 48; ++I)
    Bytes.push_back(uint8_t(I * 37 + 3));
  TS->emitDataImage("<msil>image_t", "<msil>image", "<msil>image_d", Bytes);
  std::string Text = take();
  EXPECT_TRUE(StringRef(Text).starts_with(
      ".class private explicit ansi sealed '<msil>image_t' extends "
      "[mscorlib]System.ValueType\n"
      "{\n"
      "  .pack 1\n"
      "  .size 48\n"
      "}\n"
      ".data cil '<msil>image_d' = bytearray (\n"))
      << Text;
  EXPECT_TRUE(StringRef(Text).ends_with(
      "\n.field private static valuetype '<msil>image_t' '<msil>image' "
      "at '<msil>image_d'\n"))
      << Text;
  checkByteLines(Text, "<msil>image_d", Bytes);
}

//===----------------------------------------------------------------------===//
// Address fields (plan 5.1, decision D12)
//===----------------------------------------------------------------------===//

TEST_F(MSILTargetStreamerTest, MSILAddressField) {
  TS->emitAddressField("table", /*Public=*/true);
  EXPECT_EQ(".field public static initonly native int 'table'\n", take());
  TS->emitAddressField(".L.str", /*Public=*/false);
  EXPECT_EQ(".field assembly static initonly native int '.L.str'\n", take());
}

//===----------------------------------------------------------------------===//
// Synthesized bodies (.cctor, '<msil>Main'): one line, verbatim
//===----------------------------------------------------------------------===//

TEST_F(MSILTargetStreamerTest, MSILILLine) {
  TS->emitILLine("  ldc.i4 91");
  EXPECT_EQ("  ldc.i4 91\n", take());
  TS->emitILLine(Twine("  call int32 ") + "'main'" + "(int32, native int)");
  EXPECT_EQ("  call int32 'main'(int32, native int)\n", take());
}

//===----------------------------------------------------------------------===//
// The null target streamer (llc -filetype=null)
//===----------------------------------------------------------------------===//

TEST_F(MSILTargetStreamerTest, MSILNullTargetStreamerAcceptsEveryDirective) {
  std::unique_ptr<MCStreamer> Null(TheTarget->createNullStreamer(*Ctx));
  ASSERT_TRUE(Null);
  ASSERT_NE(nullptr, Null->getTargetStreamer());
  auto &NTS = static_cast<MSILTargetStreamer &>(*Null->getTargetStreamer());
  Signature Sig = makeSig({ValType::I32}, {ValType::NativeInt});
  const uint8_t Bytes[] = {1, 2, 3};
  NTS.emitFileHeader(true, "prog.c", "x86_64-pc-windows-msvc-msil");
  NTS.emitAssemblyRefs();
  NTS.emitAssembly("prog");
  NTS.emitMethodBegin(makeDecl("f", Sig, true));
  NTS.emitEntryPoint();
  NTS.emitMaxStack(1);
  NTS.emitLocals({ValType::I32});
  NTS.emitMethodEnd("f");
  NTS.emitPInvoke(makeDecl("puts", Sig, true), "ucrtbase", "puts");
  NTS.emitDataImage("<msil>image_t", "<msil>image", "<msil>image_d", Bytes);
  NTS.emitAddressField("g", true);
  NTS.emitILLine("  ret");
  // Nothing reaches the asm streamer's output.
  EXPECT_EQ("", take());
}

//===----------------------------------------------------------------------===//
// MSILMemberRefExpr (plan 5.3; recon asmprinter.md 6.3)
//===----------------------------------------------------------------------===//

TEST_F(MSILTargetStreamerTest, MSILMemberRefMethod) {
  Signature Sum = makeSig({ValType::I32}, {ValType::NativeInt, ValType::I32});
  const MSILMemberRefExpr *E =
      MSILMemberRefExpr::createMethod("sum", &Sum, *Ctx);
  ASSERT_NE(nullptr, E);
  EXPECT_EQ(MSILMemberRefExpr::Method, E->getKind());
  EXPECT_EQ("sum", E->getName());
  ASSERT_NE(nullptr, E->getSignature());
  EXPECT_TRUE(*E->getSignature() == Sum);
  // The same text as the callee's .method header: 'call int32 'sum'(...)'.
  EXPECT_EQ("int32 'sum'(native int, int32)", print(E));

  Signature Void = makeSig({}, {});
  EXPECT_EQ("void 'f'()",
            print(MSILMemberRefExpr::createMethod("f", &Void, *Ctx)));
  Signature FP = makeSig({ValType::F32}, {ValType::F32, ValType::F64});
  EXPECT_EQ("float32 'add'(float32, float64)",
            print(MSILMemberRefExpr::createMethod("add", &FP, *Ctx)));
  // Decision D11 escapes.
  EXPECT_EQ("void '?foo@@YAXXZ'()",
            print(MSILMemberRefExpr::createMethod("?foo@@YAXXZ", &Void, *Ctx)));
  EXPECT_EQ("void 'it\\'s'()",
            print(MSILMemberRefExpr::createMethod("it's", &Void, *Ctx)));
}

TEST_F(MSILTargetStreamerTest, MSILMemberRefMethodNameIsCopied) {
  // Expressions live as long as the MCContext; the name is copied into it.
  Signature Void = makeSig({}, {});
  std::string Name = "callee";
  const MSILMemberRefExpr *E =
      MSILMemberRefExpr::createMethod(Name, &Void, *Ctx);
  Name.assign(Name.size(), 'x');
  EXPECT_EQ("callee", E->getName());
  EXPECT_EQ("void 'callee'()", print(E));
}

TEST_F(MSILTargetStreamerTest, MSILMemberRefField) {
  const MSILMemberRefExpr *E = MSILMemberRefExpr::createField("table", 0, *Ctx);
  ASSERT_NE(nullptr, E);
  EXPECT_EQ(MSILMemberRefExpr::Field, E->getKind());
  EXPECT_EQ("table", E->getName());
  EXPECT_EQ(0, E->getOffset());
  EXPECT_EQ("native int 'table'", print(E));

  // The offset is not part of the field reference ('ldsfld native int 'g''
  // is followed by 'ldc.i4 <off>; add').
  const MSILMemberRefExpr *Str =
      MSILMemberRefExpr::createField(".L.str", 20, *Ctx);
  EXPECT_EQ(MSILMemberRefExpr::Field, Str->getKind());
  EXPECT_EQ(".L.str", Str->getName());
  EXPECT_EQ(20, Str->getOffset());
  EXPECT_EQ("native int '.L.str'", print(Str));
}

TEST_F(MSILTargetStreamerTest, MSILMemberRefStandAloneSig) {
  // 'calli int32(native int)' (managed in Phase 1).
  Signature Sig = makeSig({ValType::I32}, {ValType::NativeInt, ValType::I32});
  const MSILMemberRefExpr *E = MSILMemberRefExpr::createSig(&Sig, *Ctx);
  ASSERT_NE(nullptr, E);
  EXPECT_EQ(MSILMemberRefExpr::StandAloneSig, E->getKind());
  ASSERT_NE(nullptr, E->getSignature());
  EXPECT_TRUE(*E->getSignature() == Sig);
  EXPECT_EQ("int32(native int, int32)", print(E));

  Signature Void = makeSig({}, {});
  EXPECT_EQ("void()", print(MSILMemberRefExpr::createSig(&Void, *Ctx)));
  Signature FP = makeSig({ValType::F64}, {ValType::F32});
  EXPECT_EQ("float64(float32)", print(MSILMemberRefExpr::createSig(&FP, *Ctx)));
}

TEST_F(MSILTargetStreamerTest, MSILMemberRefBCLMember) {
  const char *Barrier =
      "void [mscorlib]System.Threading.Interlocked::MemoryBarrier()";
  const MSILMemberRefExpr *E = MSILMemberRefExpr::createBCL(Barrier, *Ctx);
  ASSERT_NE(nullptr, E);
  EXPECT_EQ(MSILMemberRefExpr::BCLMember, E->getKind());
  EXPECT_EQ(Barrier, print(E));

  const char *Alloc = "native int [mscorlib]System.Runtime.InteropServices."
                      "Marshal::AllocHGlobal(native int)";
  EXPECT_EQ(Alloc, print(MSILMemberRefExpr::createBCL(Alloc, *Ctx)));
}

} // namespace
