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
// decisions D10, D11, D12, D14, D27 and D29, the complete example of recon
// asmprinter.md section 9) through the API of plan 5.3 as M18 amends it
// (section 12): comments and blank lines instead of verbatim IL lines,
// cli::TypeSig locals, the .cctor and '<msil>Main' headers chosen by
// MSILMethodKind, 'nomangle' in every pinvokeimpl (5.10.11), and the seven
// member-reference classes of 5.8.3 in the whole-program form (Phase 1's
// text) and the object form (5.8.4). The asm streamer is created through the
// target registry, as llc creates it: an MCAsmStreamer with the MSIL
// InstPrinter and the registered MSIL asm target streamer, writing to a
// raw_svector_ostream.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILTargetStreamer.h"
#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "MSILMCInstLower.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/Signature.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbolMSIL.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCValue.h"
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

// The local types Phase 1 declares, as cli::TypeSig.
cli::TypeSig int32Sig() { return cli::TypeSig::get(cli::ELEMENT_TYPE_I4); }
cli::TypeSig int64Sig() { return cli::TypeSig::get(cli::ELEMENT_TYPE_I8); }
cli::TypeSig nativeIntSig() { return cli::TypeSig::get(cli::ELEMENT_TYPE_I); }
cli::TypeSig float32Sig() { return cli::TypeSig::get(cli::ELEMENT_TYPE_R4); }
cli::TypeSig float64Sig() { return cli::TypeSig::get(cli::ELEMENT_TYPE_R8); }
cli::TypeSig stringArraySig() {
  return cli::TypeSig::getSZArray(cli::TypeSig::get(cli::ELEMENT_TYPE_STRING));
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
  // prints an expression operand by default and as the -show-mc-encoding
  // fixup comments print it: the whole-program form.
  std::string print(const MCExpr *E) {
    std::string S;
    raw_string_ostream SOS(S);
    MAI->printExpr(SOS, *E);
    return S;
  }

  // printOperand in the whole-program form (ObjectForm false) or the object
  // form (true).
  static std::string form(const MSILMemberRefExpr *E, bool ObjectForm) {
    std::string S;
    raw_string_ostream SOS(S);
    E->printOperand(SOS, ObjectForm);
    return S;
  }

  // Checks both forms of \p E, and that the default printing is the
  // whole-program form.
  void expectForms(const MSILMemberRefExpr *E, StringRef WholeProgram,
                   StringRef Object) {
    ASSERT_NE(nullptr, E);
    EXPECT_EQ(WholeProgram.str(), form(E, /*ObjectForm=*/false));
    EXPECT_EQ(Object.str(), form(E, /*ObjectForm=*/true));
    EXPECT_EQ(WholeProgram.str(), print(E));
  }

  static const MCSymbolMSIL *msilSymbol(const MSILMemberRefExpr *E) {
    return static_cast<const MCSymbolMSIL *>(E->getSymbol());
  }

  // The value of \p E for the object writer: its symbol and nothing else.
  void expectRelocatable(const MSILMemberRefExpr *E) {
    MCValue V;
    ASSERT_TRUE(E->evaluateAsRelocatable(V, nullptr));
    EXPECT_EQ(E->getSymbol(), V.getAddSym());
    EXPECT_EQ(nullptr, V.getSubSym());
    EXPECT_EQ(0, V.getConstant());
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
// Method definitions (plan 5.1, decisions D27 and D29)
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

TEST_F(MSILTargetStreamerTest, MSILMethodKindDefaultsToFunction) {
  MSILMethodDecl M;
  EXPECT_EQ(MSILMethodKind::Function, M.Kind);
  EXPECT_TRUE(M.Public);
}

// MSILMethodKind gives Phase 1's headers of the synthesized methods: the
// module initializer and the entry point (D27), whatever access the
// declaration names.
TEST_F(MSILTargetStreamerTest, MSILModuleInitializerHeader) {
  Signature Void = makeSig({}, {});
  for (bool Public : {false, true}) {
    MSILMethodDecl Cctor = makeDecl(".cctor", Void, Public);
    Cctor.Kind = MSILMethodKind::ModuleInitializer;
    TS->emitMethodBegin(Cctor);
    EXPECT_EQ(".method private hidebysig specialname rtspecialname static "
              "void .cctor() cil managed\n"
              "{\n",
              take());
  }
  TS->emitMethodEnd(".cctor");
  EXPECT_EQ("} // end of method '.cctor'\n", take());
}

TEST_F(MSILTargetStreamerTest, MSILEntryPointHeader) {
  Signature Main = makeSig({ValType::I32}, {});
  for (bool Public : {false, true}) {
    MSILMethodDecl EntryPoint = makeDecl("<msil>Main", Main, Public);
    EntryPoint.Kind = MSILMethodKind::EntryPoint;
    TS->emitMethodBegin(EntryPoint);
    EXPECT_EQ(".method private hidebysig static int32 '<msil>Main'(string[] "
              "args) cil managed\n"
              "{\n",
              take());
  }
}

// '<msil>Main' for main(int, char **), the frame of llvm/test/CodeGen/MSIL
// entrypoint.ll: header, .entrypoint, .maxstack, the string[] local and the
// end line.
TEST_F(MSILTargetStreamerTest, MSILEntryPointFrame) {
  Signature Main = makeSig({ValType::I32}, {});
  MSILMethodDecl EntryPoint = makeDecl("<msil>Main", Main, /*Public=*/false);
  EntryPoint.Kind = MSILMethodKind::EntryPoint;
  TS->emitMethodBegin(EntryPoint);
  TS->emitEntryPoint();
  TS->emitMaxStack(3);
  TS->emitLocals({stringArraySig(), int32Sig(), nativeIntSig(), int32Sig()});
  TS->emitMethodEnd("<msil>Main");
  EXPECT_EQ(".method private hidebysig static int32 '<msil>Main'(string[] "
            "args) cil managed\n"
            "{\n"
            "  .entrypoint\n"
            "  .maxstack 3\n"
            "  .locals (string[], int32, native int, int32)\n"
            "} // end of method '<msil>Main'\n",
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

// Locals are cli::TypeSig (canonical listing v1 text) and never 'init'
// (decision D13).
TEST_F(MSILTargetStreamerTest, MSILLocals) {
  TS->emitLocals({int32Sig(), int32Sig()});
  std::string Text = take();
  EXPECT_EQ("  .locals (int32, int32)\n", Text);
  EXPECT_EQ(std::string::npos, Text.find("init"));

  TS->emitLocals({nativeIntSig(), float32Sig(), float64Sig(), int32Sig()});
  Text = take();
  EXPECT_EQ("  .locals (native int, float32, float64, int32)\n", Text);
  EXPECT_EQ(std::string::npos, Text.find("init"));

  TS->emitLocals({nativeIntSig()});
  EXPECT_EQ("  .locals (native int)\n", take());

  TS->emitLocals(
      {int64Sig(), stringArraySig(), cli::TypeSig::getPointer(cli::TypeSig())});
  EXPECT_EQ("  .locals (int64, string[], void*)\n", take());
}

TEST_F(MSILTargetStreamerTest, MSILLocalsEmptyPrintsNothing) {
  // Not even a blank line.
  TS->emitLocals(ArrayRef<cli::TypeSig>());
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
  TS->emitLocals({int32Sig(), int32Sig()});
  TS->emitMethodEnd("sum");
  EXPECT_EQ(".method public static int32 'sum'(native int, int32) cil managed\n"
            "{\n"
            "  .maxstack 4\n"
            "  .locals (int32, int32)\n"
            "} // end of method 'sum'\n",
            take());
}

//===----------------------------------------------------------------------===//
// Comments and blank lines (the trailer's section lines)
//===----------------------------------------------------------------------===//

TEST_F(MSILTargetStreamerTest, MSILComment) {
  TS->emitComment("---- undefined external functions: P/Invoke ----");
  EXPECT_EQ("// ---- undefined external functions: P/Invoke ----\n", take());
  TS->emitComment(Twine("---- data segment: ") + Twine(24) + " bytes ----");
  EXPECT_EQ("// ---- data segment: 24 bytes ----\n", take());
}

TEST_F(MSILTargetStreamerTest, MSILBlankLine) {
  TS->emitComment("one");
  TS->emitBlankLine();
  TS->emitComment("two");
  EXPECT_EQ("// one\n"
            "\n"
            "// two\n",
            take());
  // A blank line alone is one newline.
  TS->emitBlankLine();
  FOut->flush();
  EXPECT_EQ("\n", Buffer.str().str());
  Buffer.clear();
}

//===----------------------------------------------------------------------===//
// P/Invoke declarations (plan 5.1, decision D14, 5.10.11: nomangle from M18)
//===----------------------------------------------------------------------===//

TEST_F(MSILTargetStreamerTest, MSILPInvoke) {
  Signature Puts = makeSig({ValType::I32}, {ValType::NativeInt});
  TS->emitPInvoke(makeDecl("puts", Puts, /*Public=*/true), "ucrtbase", "puts");
  EXPECT_EQ(".method public hidebysig static pinvokeimpl(\"ucrtbase\" "
            "nomangle cdecl) int32 'puts'(native int) cil managed preservesig "
            "{}\n",
            take());

  Signature Sqrt = makeSig({ValType::F64}, {ValType::F64});
  TS->emitPInvoke(makeDecl("sqrt", Sqrt, /*Public=*/true), "libm.so.6", "sqrt");
  EXPECT_EQ(".method public hidebysig static pinvokeimpl(\"libm.so.6\" "
            "nomangle cdecl) float64 'sqrt'(float64) cil managed preservesig "
            "{}\n",
            take());
}

TEST_F(MSILTargetStreamerTest, MSILPInvokeWithEntryPoint) {
  // An entry point that differs from the method name (msil-import-name) is
  // named with 'as'.
  Signature Puts = makeSig({ValType::I32}, {ValType::NativeInt});
  TS->emitPInvoke(makeDecl("msvcrt_puts", Puts, /*Public=*/true), "msvcrt",
                  "puts");
  EXPECT_EQ(".method public hidebysig static "
            "pinvokeimpl(\"msvcrt\" as \"puts\" nomangle cdecl) "
            "int32 'msvcrt_puts'(native int) cil managed preservesig {}\n",
            take());
}

TEST_F(MSILTargetStreamerTest, MSILPInvokeLinkedAccess) {
  // msil-ld's linked P/Invokes (M15) are 'assembly' and keep 'nomangle'.
  Signature Strlen = makeSig({ValType::NativeInt}, {ValType::NativeInt});
  TS->emitPInvoke(makeDecl("my_strlen", Strlen, /*Public=*/false),
                  "api-ms-win-crt-string-l1-1-0.dll", "strlen");
  EXPECT_EQ(".method assembly hidebysig static "
            "pinvokeimpl(\"api-ms-win-crt-string-l1-1-0.dll\" as \"strlen\" "
            "nomangle cdecl) native int 'my_strlen'(native int) cil managed "
            "preservesig {}\n",
            take());
}

TEST_F(MSILTargetStreamerTest, MSILPInvokeStdcall) {
  Signature Tick = makeSig({ValType::I32}, {});
  Tick.CC = MSIL::CallConv::UnmanagedStdcall;
  TS->emitPInvoke(makeDecl("GetTickCount", Tick, /*Public=*/true),
                  "kernel32.dll", "GetTickCount");
  EXPECT_EQ(".method public hidebysig static pinvokeimpl(\"kernel32.dll\" "
            "nomangle stdcall) int32 'GetTickCount'() cil managed preservesig "
            "{}\n",
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
// The null target streamer (llc -filetype=null)
//===----------------------------------------------------------------------===//

TEST_F(MSILTargetStreamerTest, MSILNullTargetStreamerAcceptsEveryDirective) {
  std::unique_ptr<MCStreamer> Null(TheTarget->createNullStreamer(*Ctx));
  ASSERT_TRUE(Null);
  ASSERT_NE(nullptr, Null->getTargetStreamer());
  auto &NTS = static_cast<MSILTargetStreamer &>(*Null->getTargetStreamer());
  Signature Sig = makeSig({ValType::I32}, {ValType::NativeInt});
  Signature Void = makeSig({}, {});
  const uint8_t Bytes[] = {1, 2, 3};
  MSILMethodDecl Cctor = makeDecl(".cctor", Void, false);
  Cctor.Kind = MSILMethodKind::ModuleInitializer;
  MSILMethodDecl Main = makeDecl("<msil>Main", Sig, false);
  Main.Kind = MSILMethodKind::EntryPoint;
  NTS.emitFileHeader(true, "prog.c", "x86_64-pc-windows-msvc-msil");
  NTS.emitAssemblyRefs();
  NTS.emitAssembly("prog");
  NTS.emitMethodBegin(makeDecl("f", Sig, true));
  NTS.emitMethodBegin(Cctor);
  NTS.emitMethodBegin(Main);
  NTS.emitEntryPoint();
  NTS.emitMaxStack(1);
  NTS.emitLocals({int32Sig(), stringArraySig()});
  NTS.emitMethodEnd("f");
  NTS.emitPInvoke(makeDecl("puts", Sig, true), "ucrtbase", "puts");
  NTS.emitDataImage("<msil>image_t", "<msil>image", "<msil>image_d", Bytes);
  NTS.emitAddressField("g", true);
  NTS.emitAddressField("<msil>fn_f", false, /*Generated=*/true);
  NTS.emitComment("---- data segment ----");
  NTS.emitBlankLine();
  // Nothing reaches the asm streamer's output.
  EXPECT_EQ("", take());
}

//===----------------------------------------------------------------------===//
// MSILMemberRefExpr (plan 5.3 and M18.a (2), (4), (5); 5.8.3 classes, 5.8.4
// object-form text)
//===----------------------------------------------------------------------===//

// A Function defined in this module: its own MethodDef in both forms.
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
  expectForms(E, "int32 'sum'(native int, int32)",
              "int32 'sum'(native int, int32)");

  Signature Void = makeSig({}, {});
  expectForms(MSILMemberRefExpr::createMethod("f", &Void, *Ctx), "void 'f'()",
              "void 'f'()");
  Signature FP = makeSig({ValType::F32}, {ValType::F32, ValType::F64});
  expectForms(MSILMemberRefExpr::createMethod("add", &FP, *Ctx),
              "float32 'add'(float32, float64)",
              "float32 'add'(float32, float64)");
  // Decision D11 escapes.
  expectForms(MSILMemberRefExpr::createMethod("?foo@@YAXXZ", &Void, *Ctx),
              "void '?foo@@YAXXZ'()", "void '?foo@@YAXXZ'()");
  expectForms(MSILMemberRefExpr::createMethod("it's", &Void, *Ctx),
              "void 'it\\'s'()", "void 'it\\'s'()");
}

// Any other function: the whole-program text is Phase 1's, and the object
// form names the placeholder member of [msil.extern]'<msil>Symbols'.
TEST_F(MSILTargetStreamerTest, MSILMemberRefExternMethod) {
  Signature Sig = makeSig({ValType::I32}, {ValType::I32});
  const MSILMemberRefExpr *E =
      MSILMemberRefExpr::createExternMethod("add3", &Sig, *Ctx);
  ASSERT_NE(nullptr, E);
  EXPECT_EQ(MSILMemberRefExpr::ExternMethod, E->getKind());
  EXPECT_EQ("add3", E->getName());
  ASSERT_NE(nullptr, E->getSignature());
  EXPECT_TRUE(*E->getSignature() == Sig);
  expectForms(E, "int32 'add3'(int32)",
              "int32 [msil.extern]'<msil>Symbols'::'add3'(int32)");

  Signature Void = makeSig({}, {});
  expectForms(MSILMemberRefExpr::createExternMethod("it's", &Void, *Ctx),
              "void 'it\\'s'()",
              "void [msil.extern]'<msil>Symbols'::'it\\'s'()");
  Signature Ptr = makeSig({ValType::NativeInt}, {ValType::NativeInt});
  expectForms(MSILMemberRefExpr::createExternMethod("ldexp", &Ptr, *Ctx),
              "native int 'ldexp'(native int)",
              "native int [msil.extern]'<msil>Symbols'::'ldexp'(native int)");
}

TEST_F(MSILTargetStreamerTest, MSILMemberRefMethodNameIsCopied) {
  // Expressions live as long as the MCContext; the name is copied into it.
  Signature Void = makeSig({}, {});
  std::string Name = "callee";
  const MSILMemberRefExpr *E =
      MSILMemberRefExpr::createMethod(Name, &Void, *Ctx);
  std::string Ext = "other";
  const MSILMemberRefExpr *X = MSILMemberRefExpr::createExternField(Ext, *Ctx);
  Name.assign(Name.size(), 'x');
  Ext.assign(Ext.size(), 'y');
  EXPECT_EQ("callee", E->getName());
  EXPECT_EQ("void 'callee'()", print(E));
  EXPECT_EQ("other", X->getName());
  EXPECT_EQ("native int [msil.extern]'<msil>Symbols'::'other'",
            form(X, /*ObjectForm=*/true));
}

// A global defined in this module: its address field (decision D12).
TEST_F(MSILTargetStreamerTest, MSILMemberRefField) {
  const MSILMemberRefExpr *E = MSILMemberRefExpr::createField("table", *Ctx);
  ASSERT_NE(nullptr, E);
  EXPECT_EQ(MSILMemberRefExpr::Field, E->getKind());
  EXPECT_EQ("table", E->getName());
  expectForms(E, "native int 'table'", "native int 'table'");
  expectForms(MSILMemberRefExpr::createField(".L.str", *Ctx),
              "native int '.L.str'", "native int '.L.str'");
}

// The data image field, a value-type field of this module.
TEST_F(MSILTargetStreamerTest, MSILMemberRefValueTypeField) {
  const MSILMemberRefExpr *E = MSILMemberRefExpr::createValueTypeField(
      "<msil>image", "<msil>image_t", *Ctx);
  ASSERT_NE(nullptr, E);
  EXPECT_EQ(MSILMemberRefExpr::Field, E->getKind());
  EXPECT_EQ("<msil>image", E->getName());
  EXPECT_EQ("<msil>image_t", E->getFieldTypeName());
  expectForms(E, "valuetype '<msil>image_t' '<msil>image'",
              "valuetype '<msil>image_t' '<msil>image'");
}

// A global declared here or an alias (object mode only): Phase 1's text in
// the whole-program form, the placeholder member in the object form.
TEST_F(MSILTargetStreamerTest, MSILMemberRefExternField) {
  const MSILMemberRefExpr *E = MSILMemberRefExpr::createExternField("x", *Ctx);
  ASSERT_NE(nullptr, E);
  EXPECT_EQ(MSILMemberRefExpr::ExternField, E->getKind());
  EXPECT_EQ("x", E->getName());
  expectForms(E, "native int 'x'",
              "native int [msil.extern]'<msil>Symbols'::'x'");
}

// The address of a function (M9, 5.8.3 R2): its '<msil>fn_' field in the
// whole-program form, the [msil.extern]'<msil>FnAddr' placeholder in the
// object form.
TEST_F(MSILTargetStreamerTest, MSILMemberRefFnAddr) {
  EXPECT_EQ("<msil>fn_", StringRef(MSIL::FnAddrFieldPrefix));
  const MSILMemberRefExpr *E = MSILMemberRefExpr::createFnAddr("twice", *Ctx);
  ASSERT_NE(nullptr, E);
  EXPECT_EQ(MSILMemberRefExpr::FnAddr, E->getKind());
  EXPECT_EQ("twice", E->getName());
  expectForms(E, "native int '<msil>fn_twice'",
              "native int [msil.extern]'<msil>FnAddr'::'twice'");
  expectForms(MSILMemberRefExpr::createFnAddr("it's", *Ctx),
              "native int '<msil>fn_it\\'s'",
              "native int [msil.extern]'<msil>FnAddr'::'it\\'s'");
}

// 'calli int32(native int, int32)' (managed in Phase 1): the same text in
// both forms.
TEST_F(MSILTargetStreamerTest, MSILMemberRefStandAloneSig) {
  Signature Sig = makeSig({ValType::I32}, {ValType::NativeInt, ValType::I32});
  const MSILMemberRefExpr *E = MSILMemberRefExpr::createSig(&Sig, *Ctx);
  ASSERT_NE(nullptr, E);
  EXPECT_EQ(MSILMemberRefExpr::StandAloneSig, E->getKind());
  ASSERT_NE(nullptr, E->getSignature());
  EXPECT_TRUE(*E->getSignature() == Sig);
  expectForms(E, "int32(native int, int32)", "int32(native int, int32)");

  Signature Void = makeSig({}, {});
  expectForms(MSILMemberRefExpr::createSig(&Void, *Ctx), "void()", "void()");
  Signature FP = makeSig({ValType::F64}, {ValType::F32});
  expectForms(MSILMemberRefExpr::createSig(&FP, *Ctx), "float64(float32)",
              "float64(float32)");
}

// BCL members print from BCLMembers.def: [mscorlib] in the whole-program
// form (5.11.8), the unbound [msil.bcl] in the object form.
TEST_F(MSILTargetStreamerTest, MSILMemberRefBCLMember) {
  const MSILMemberRefExpr *E = MSILMemberRefExpr::createBCL(
      cli::BCLMemberId::Interlocked_MemoryBarrier, *Ctx);
  ASSERT_NE(nullptr, E);
  EXPECT_EQ(MSILMemberRefExpr::BCLMember, E->getKind());
  EXPECT_TRUE(E->getBCLMember() == cli::BCLMemberId::Interlocked_MemoryBarrier);
  expectForms(E, "void [mscorlib]System.Threading.Interlocked::MemoryBarrier()",
              "void [msil.bcl]System.Threading.Interlocked::MemoryBarrier()");
  expectForms(MSILMemberRefExpr::createBCL(
                  cli::BCLMemberId::Marshal_AllocHGlobal, *Ctx),
              "native int [mscorlib]System.Runtime.InteropServices.Marshal::"
              "AllocHGlobal(native int)",
              "native int [msil.bcl]System.Runtime.InteropServices.Marshal::"
              "AllocHGlobal(native int)");

  // Every method of the list prints exactly as printBCLMemberRef does.
  for (const cli::BCLMemberInfo &Info : cli::getAllBCLMembers()) {
    if (!Info.isMethod())
      continue;
    SCOPED_TRACE(StringRef(Info.IdName).str());
    std::string Whole, Object;
    raw_string_ostream WOS(Whole), OOS(Object);
    cli::printBCLMemberRef(WOS, Info.Id, "mscorlib");
    cli::printBCLMemberRef(OOS, Info.Id, "msil.bcl");
    expectForms(MSILMemberRefExpr::createBCL(Info.Id, *Ctx), Whole, Object);
  }
}

// Every expression is relocatable to the MCSymbolMSIL that M19's writer
// reads the operand class from: the named kinds use the context's symbol of
// the IL name (FnAddr: the function's own), StandAloneSig and BCLMember a
// temporary symbol; the symbol kind is the class's (M13's kinds).
TEST_F(MSILTargetStreamerTest, MSILMemberRefSymbols) {
  Signature Sig = makeSig({ValType::I32}, {ValType::I32});
  const MSILMemberRefExpr *Method =
      MSILMemberRefExpr::createMethod("m", &Sig, *Ctx);
  const MSILMemberRefExpr *Extern =
      MSILMemberRefExpr::createExternMethod("e", &Sig, *Ctx);
  const MSILMemberRefExpr *Field = MSILMemberRefExpr::createField("g", *Ctx);
  const MSILMemberRefExpr *ExtField =
      MSILMemberRefExpr::createExternField("x", *Ctx);
  const MSILMemberRefExpr *FnAddr = MSILMemberRefExpr::createFnAddr("h", *Ctx);
  const MSILMemberRefExpr *StandAlone =
      MSILMemberRefExpr::createSig(&Sig, *Ctx);
  const MSILMemberRefExpr *BCL =
      MSILMemberRefExpr::createBCL(cli::BCLMemberId::Buffer_MemoryCopy, *Ctx);

  using SymKind = MCSymbolMSIL::Kind;
  const struct {
    const MSILMemberRefExpr *E;
    const char *Name; // nullptr: a temporary symbol
    SymKind Kind;
  } Cases[] = {
      {Method, "m", SymKind::Method},
      {Extern, "e", SymKind::Method},
      {Field, "g", SymKind::Data},
      {ExtField, "x", SymKind::Data},
      {FnAddr, "h", SymKind::Method},
      {StandAlone, nullptr, SymKind::Signature},
      {BCL, nullptr, SymKind::ExternalMember},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.Name ? C.Name : "temporary");
    ASSERT_NE(nullptr, C.E->getSymbol());
    const MCSymbolMSIL *S = msilSymbol(C.E);
    EXPECT_TRUE(S->getKind() == C.Kind);
    if (C.Name) {
      EXPECT_FALSE(S->isTemporary());
      EXPECT_EQ(C.E->getSymbol(), Ctx->lookupSymbol(C.Name));
    } else {
      EXPECT_TRUE(S->isTemporary());
    }
    expectRelocatable(C.E);
  }
  // The BCL member's symbol names the member, counted from 1.
  EXPECT_EQ(uint32_t(cli::BCLMemberId::Buffer_MemoryCopy) + 1,
            msilSymbol(BCL)->getExternalMemberId());
  // Two references to one function share its symbol.
  EXPECT_EQ(Method->getSymbol(),
            MSILMemberRefExpr::createMethod("m", &Sig, *Ctx)->getSymbol());
  // The address of a function names the function itself.
  EXPECT_EQ(Method->getSymbol(),
            MSILMemberRefExpr::createFnAddr("m", *Ctx)->getSymbol());
}

// A symbol keeps the kind it already has: only an Unknown symbol takes the
// class's kind.
TEST_F(MSILTargetStreamerTest, MSILMemberRefKeepsAKnownSymbolKind) {
  auto *Pre = static_cast<MCSymbolMSIL *>(Ctx->getOrCreateSymbol("pre"));
  Pre->setKind(MCSymbolMSIL::Kind::Data);
  Signature Void = makeSig({}, {});
  const MSILMemberRefExpr *E =
      MSILMemberRefExpr::createMethod("pre", &Void, *Ctx);
  EXPECT_EQ(Pre, E->getSymbol());
  EXPECT_TRUE(Pre->getKind() == MCSymbolMSIL::Kind::Data);
}

//===----------------------------------------------------------------------===//
// The printer's object-form option (llc -M object-form, which M19 sets)
//===----------------------------------------------------------------------===//

TEST_F(MSILTargetStreamerTest, MSILPrinterObjectFormOption) {
  Signature Sig = makeSig({ValType::I32}, {ValType::I32});
  const MSILMemberRefExpr *Ext =
      MSILMemberRefExpr::createExternMethod("add3", &Sig, *Ctx);
  const MSILMemberRefExpr *Fn = MSILMemberRefExpr::createFnAddr("twice", *Ctx);
  const MSILMemberRefExpr *Copy =
      MSILMemberRefExpr::createBCL(cli::BCLMemberId::Buffer_MemoryCopy, *Ctx);
  auto Call = [&](const MCExpr *E, cli::Opcode Op) {
    MCInst I;
    I.setOpcode(MSIL::getMCOpcodeForCIL(Op));
    I.addOperand(MCOperand::createExpr(E));
    return I;
  };
  const MCInst Insts[] = {Call(Ext, cli::Opcode::Call),
                          Call(Fn, cli::Opcode::Ldsfld),
                          Call(Copy, cli::Opcode::Call)};

  // The default printer: Phase 1's text.
  Streamer->switchSection(MOFI->getTextSection());
  take();
  for (const MCInst &I : Insts)
    Streamer->emitInstruction(I, *STI);
  EXPECT_EQ("  call int32 'add3'(int32)\n"
            "  ldsfld native int '<msil>fn_twice'\n"
            "  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, "
            "int64, int64)\n",
            take());

  // A printer given the option prints the object form (5.8.4).
  std::unique_ptr<MCInstPrinter> IP(TheTarget->createMCInstPrinter(
      TT, MAI->getAssemblerDialect(), *MAI, *MII, *MRI));
  ASSERT_TRUE(IP);
  EXPECT_FALSE(IP->applyTargetSpecificCLOption("no-such-option"));
  EXPECT_TRUE(IP->applyTargetSpecificCLOption("object-form"));
  SmallString<256> ObjBuffer;
  raw_svector_ostream ObjOS(ObjBuffer);
  auto FOS = std::make_unique<formatted_raw_ostream>(ObjOS);
  formatted_raw_ostream *ObjOut = FOS.get();
  std::unique_ptr<MCStreamer> ObjStreamer(TheTarget->createAsmStreamer(
      *Ctx, std::move(FOS), std::move(IP), nullptr, nullptr));
  ASSERT_TRUE(ObjStreamer);
  ObjStreamer->switchSection(MOFI->getTextSection());
  ObjOut->flush();
  ObjBuffer.clear();
  for (const MCInst &I : Insts)
    ObjStreamer->emitInstruction(I, *STI);
  ObjOut->flush();
  EXPECT_EQ("  call int32 [msil.extern]'<msil>Symbols'::'add3'(int32)\n"
            "  ldsfld native int [msil.extern]'<msil>FnAddr'::'twice'\n"
            "  call void [msil.bcl]System.Buffer::MemoryCopy(void*, void*, "
            "int64, int64)\n",
            ObjBuffer.str().str());
}

} // namespace
