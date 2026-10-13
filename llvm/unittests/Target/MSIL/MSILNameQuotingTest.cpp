//===- MSILNameQuotingTest.cpp - Tests for MSIL::printQuotedName ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Every symbol-derived ILAsm identifier is single-quoted by
// MSIL::printQuotedName (ImplementationPlan.md decision D11, recon
// asmprinter.md 4): a quote becomes \', a backslash becomes \134 (ilasm drops
// "\\"), bytes below 0x20 and 0x7f become three-digit octal escapes, and every
// other byte, including UTF-8 sequences, is written unchanged. Quoting is what
// lets ILAsm keywords such as 'add' or 'int32' and C++ or MSVC names such as
// '?foo@@YAHXZ' be used as method and field names.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

#include <string>

using namespace llvm;

namespace {

std::string quote(StringRef Name) {
  std::string S;
  raw_string_ostream OS(S);
  MSIL::printQuotedName(OS, Name);
  return OS.str();
}

// The escape rule of decision D11 for one byte, written out independently of
// the implementation.
std::string expectedByte(unsigned char C) {
  if (C == '\'')
    return "\\'";
  if (C == '\\')
    return "\\134";
  if (C < 0x20 || C == 0x7f) {
    std::string S = "\\";
    S += char('0' + (C >> 6));
    S += char('0' + ((C >> 3) & 7));
    S += char('0' + (C & 7));
    return S;
  }
  return std::string(1, char(C));
}

TEST(MSILNameQuotingTest, ILAsmKeywordsAreQuoted) {
  EXPECT_EQ("'add'", quote("add"));
  EXPECT_EQ("'int32'", quote("int32"));
  EXPECT_EQ("'value'", quote("value"));
  EXPECT_EQ("'ret'", quote("ret"));
  EXPECT_EQ("'call'", quote("call"));
  EXPECT_EQ("'native'", quote("native"));
  EXPECT_EQ("'float64'", quote("float64"));
  EXPECT_EQ("'main'", quote("main"));
}

TEST(MSILNameQuotingTest, MSILMangledAndGeneratedNames) {
  // MSVC C++ mangling and an MSVC string-literal symbol (decision D26).
  EXPECT_EQ("'?foo@@YAHXZ'", quote("?foo@@YAHXZ"));
  EXPECT_EQ("'??_C@_05CJBACGMB@hello?$AA@'",
            quote("??_C@_05CJBACGMB@hello?$AA@"));
  EXPECT_EQ("'a.b$c'", quote("a.b$c"));
  // Private globals and the compiler-generated '<msil>' names.
  EXPECT_EQ("'.L.str'", quote(".L.str"));
  EXPECT_EQ("'<msil>Main'", quote("<msil>Main"));
  EXPECT_EQ("'<msil>image_t'", quote("<msil>image_t"));
  // Itanium C++ mangling.
  EXPECT_EQ("'_ZN3foo3barEv'", quote("_ZN3foo3barEv"));
}

TEST(MSILNameQuotingTest, MSILQuoteIsEscaped) {
  EXPECT_EQ("'it\\'s'", quote("it's"));
  EXPECT_EQ("'\\''", quote("'"));
  EXPECT_EQ("'\\'\\''", quote("''"));
}

TEST(MSILNameQuotingTest, MSILBackslashIsOctal) {
  EXPECT_EQ("'a\\134b'", quote("a\\b"));
  EXPECT_EQ("'\\134'", quote("\\"));
  EXPECT_EQ("'\\134\\134'", quote("\\\\"));
  // The escape of a quote is not confused with a backslash before a quote.
  EXPECT_EQ("'\\134\\''", quote("\\'"));
}

TEST(MSILNameQuotingTest, MSILControlBytesAreOctal) {
  EXPECT_EQ("'a\\011b'", quote("a\tb"));
  EXPECT_EQ("'\\011'", quote("\t"));
  EXPECT_EQ("'\\012'", quote("\n"));
  EXPECT_EQ("'\\015'", quote("\r"));
  EXPECT_EQ("'\\001'", quote("\x01"));
  EXPECT_EQ("'\\037'", quote("\x1f"));
  EXPECT_EQ("'\\177'", quote("\x7f"));
  EXPECT_EQ("'a\\000b'", quote(StringRef("a\0b", 3)));
}

TEST(MSILNameQuotingTest, MSILPrintableASCIIIsUnchanged) {
  EXPECT_EQ("' '", quote(" "));
  EXPECT_EQ("'a b'", quote("a b"));
  EXPECT_EQ("'\"'", quote("\""));
  EXPECT_EQ("'~!@#$%^&*()_+-=[]{}|;:,.<>/?`'",
            quote("~!@#$%^&*()_+-=[]{}|;:,.<>/?`"));
  EXPECT_EQ("'0123456789'", quote("0123456789"));
}

TEST(MSILNameQuotingTest, MSILUTF8IsRaw) {
  // UTF-8 is written as is; the AsmPrinter adds a BOM to such a file.
  EXPECT_EQ("'caf\xC3\xA9'", quote("caf\xC3\xA9"));
  EXPECT_EQ("'\xE2\x82\xAC'", quote("\xE2\x82\xAC"));
  EXPECT_EQ("'\xF0\x9F\x98\x80x'", quote("\xF0\x9F\x98\x80x"));
  // Bytes >= 0x80 are never escaped, even when they are not valid UTF-8.
  EXPECT_EQ("'\x80\xFF'", quote("\x80\xFF"));
}

TEST(MSILNameQuotingTest, MSILEmptyName) { EXPECT_EQ("''", quote("")); }

TEST(MSILNameQuotingTest, MSILMixedEscapes) {
  EXPECT_EQ("'a\\'b\\134c\\011d\xC3\xA9'", quote("a'b\\c\td\xC3\xA9"));
}

TEST(MSILNameQuotingTest, MSILEveryByte) {
  for (unsigned C = 0; C < 256; ++C) {
    char Byte = char(C);
    EXPECT_EQ("'" + expectedByte(C) + "'", quote(StringRef(&Byte, 1)))
        << "byte " << C;
  }
}

TEST(MSILNameQuotingTest, MSILAppendsToTheStream) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "call int32 ";
  MSIL::printQuotedName(OS, "add");
  OS << "(int32, int32)";
  EXPECT_EQ("call int32 'add'(int32, int32)", OS.str());
}

} // namespace
