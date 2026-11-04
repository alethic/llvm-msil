//===-- MSILTargetStreamer.cpp - MSIL target streamer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the MSIL target streamers. The text they write is the
/// ILAsm output contract of the backend (llvm/docs/MSIL/ImplementationPlan.md
/// section 5.1).
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILTargetStreamer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/LinkInfo.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>

using namespace llvm;

MSILTargetStreamer::MSILTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

MSILTargetStreamer::~MSILTargetStreamer() = default;

/// Reports a trailer directive outside whole-program text (an internal
/// error: objects have no trailer, plan 5.8.2).
[[noreturn]] static void reportTrailerOnly(StringRef Directive) {
  reportFatalInternalError("MSIL: " + Directive +
                           " is whole-program output, which a CIL object does "
                           "not contain");
}

void MSILTargetStreamer::emitHelperBegin(StringRef, cli::MethodAttributes,
                                         const cli::MethodSig &,
                                         cli::MethodImplAttributes) {
  reportTrailerOnly("a synthesized helper method");
}

void MSILTargetStreamer::emitStringInstruction(cli::Opcode, StringRef) {
  reportTrailerOnly("a string literal");
}

void MSILTargetStreamer::emitHelperInstruction(cli::Opcode, StringRef,
                                               const cli::MethodSig &) {
  reportTrailerOnly("a call of a synthesized helper method");
}

void MSILTargetStreamer::emitDataImportField(StringRef) {
  reportTrailerOnly("the address field of a native data import");
}

void MSILTargetStreamer::checkExceptionClause(cli::ExceptionClauseKind Kind,
                                              const MCSymbol *TryBegin,
                                              const MCSymbol *TryEnd,
                                              const MCSymbol *HandlerBegin,
                                              const MCSymbol *HandlerEnd,
                                              const MCSymbol *FilterBegin) {
  if (!TryBegin || !TryEnd || !HandlerBegin || !HandlerEnd)
    reportFatalInternalError("MSIL: an exception clause without the labels of "
                             "its blocks");
  switch (Kind) {
  case cli::ExceptionClauseKind::Catch:
    // ILAsm's 'catch' and the clause's class token have no operand here.
    reportFatalInternalError("MSIL: a catch clause is not supported");
  case cli::ExceptionClauseKind::Filter:
    // The filter block lies between the protected block and the handler.
    if (!FilterBegin || TryEnd != FilterBegin)
      reportFatalInternalError("MSIL: the filter block of an exception clause "
                               "does not follow its protected block");
    break;
  case cli::ExceptionClauseKind::Finally:
  case cli::ExceptionClauseKind::Fault:
    if (FilterBegin || TryEnd != HandlerBegin)
      reportFatalInternalError("MSIL: the handler of an exception clause does "
                               "not follow its protected block");
    break;
  }
  if (TryBegin == TryEnd || HandlerBegin == HandlerEnd ||
      (FilterBegin && FilterBegin == HandlerBegin))
    reportFatalInternalError("MSIL: an empty block in an exception clause");
}

/// Prints \p T as canonical listing v1 text, which ILAsm accepts for the
/// primitive types and arrays of the synthesized methods; they have no type
/// references.
static void printSynthesizedType(raw_ostream &OS, const cli::TypeSig &T) {
  cli::printTypeSig(OS, T, [](raw_ostream &, uint32_t) {
    reportFatalInternalError("MSIL: a class or value type in a synthesized "
                             "signature");
  });
}

/// Prints "<ret> '<Name>'(<params>)", the spelling of a method in its header
/// and in a call.
static void printSynthesizedMethod(raw_ostream &OS, StringRef Name,
                                   const cli::MethodSig &Sig) {
  printSynthesizedType(OS, Sig.Return);
  OS << ' ';
  MSIL::printQuotedName(OS, Name);
  OS << '(';
  interleaveComma(Sig.Params, OS, [&OS](const cli::TypeSig &T) {
    printSynthesizedType(OS, T);
  });
  OS << ')';
}

MSILTargetAsmStreamer::MSILTargetAsmStreamer(MCStreamer &S)
    : MSILTargetStreamer(S) {}

void MSILTargetAsmStreamer::emitLine(const Twine &Line) {
  getStreamer().emitRawText(Line);
}

void MSILTargetAsmStreamer::emitFileHeader(bool BOM, StringRef SourceFile,
                                           StringRef TripleStr) {
  SmallString<256> Str;
  raw_svector_ostream OS(Str);
  // Without the byte order mark ilasm reads the file in the ANSI code page and
  // mangles non-ASCII identifiers.
  if (BOM)
    OS << "\xEF\xBB\xBF";
  OS << "// MSIL module generated by LLVM\n";
  // Escaped as in the IR text, so no file name can end the comment line.
  OS << "// source_filename = \"";
  printEscapedString(SourceFile, OS);
  OS << "\"\n";
  OS << "// target triple   = \"";
  printEscapedString(TripleStr, OS);
  OS << '"';
  emitLine(OS.str());
}

void MSILTargetAsmStreamer::emitAssemblyRefs() {
  // Phase 1 references only mscorlib (decision D10); it runs unchanged on .NET
  // Framework 4.8 and on .NET 8 and later.
  emitLine(".assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 "
           "E0 89 ) .ver 4:0:0:0 }");
}

void MSILTargetAsmStreamer::emitAssembly(StringRef Name) {
  SmallString<64> Str;
  raw_svector_ostream OS(Str);
  OS << ".assembly ";
  MSIL::printQuotedName(OS, Name);
  OS << " { .ver 0:0:0:0 }";
  emitLine(OS.str());
  emitLine("");
}

void MSILTargetAsmStreamer::emitMethodBegin(const MSILMethodDecl &M) {
  SmallString<128> Str;
  raw_svector_ostream OS(Str);
  switch (M.Kind) {
  case MSILMethodKind::Function:
    assert(M.Sig && "a method needs a signature");
    OS << ".method " << (M.Public ? "public" : "assembly") << " static ";
    MSIL::printResult(OS, *M.Sig);
    OS << ' ';
    MSIL::printQuotedName(OS, M.Name);
    MSIL::printParamList(OS, *M.Sig);
    break;
  case MSILMethodKind::ModuleInitializer:
    // The runtime runs <Module>::.cctor before any code of the module.
    OS << ".method private hidebysig specialname rtspecialname static void "
          ".cctor()";
    break;
  case MSILMethodKind::EntryPoint:
    // The only method that takes a managed array (decision D27).
    OS << ".method private hidebysig static int32 ";
    MSIL::printQuotedName(OS, M.Name);
    OS << "(string[] args)";
    break;
  }
  // One raw text for both lines: a pending verbose-asm comment ("-- Begin
  // function") then goes after the brace and the header line stays exact.
  OS << " cil managed\n{";
  emitLine(OS.str());
}

void MSILTargetAsmStreamer::emitEntryPoint() { emitLine("  .entrypoint"); }

void MSILTargetAsmStreamer::emitMaxStack(unsigned N) {
  emitLine("  .maxstack " + Twine(N));
}

void MSILTargetAsmStreamer::emitLocals(ArrayRef<cli::TypeSig> Locals) {
  if (Locals.empty())
    return;
  SmallString<128> Str;
  raw_svector_ostream OS(Str);
  // Never 'init' (decision D13). Locals are primitive types and arrays of
  // them, which canonical listing v1 spells as ILAsm does.
  OS << "  .locals (";
  interleaveComma(Locals, OS, [&OS](const cli::TypeSig &T) {
    cli::printTypeSig(OS, T, [](raw_ostream &, uint32_t) {
      reportFatalInternalError("MSIL: a local of a class or value type");
    });
  });
  OS << ')';
  emitLine(OS.str());
}

void MSILTargetAsmStreamer::emitExceptionClause(cli::ExceptionClauseKind Kind,
                                                const MCSymbol *TryBegin,
                                                const MCSymbol *TryEnd,
                                                const MCSymbol *HandlerBegin,
                                                const MCSymbol *HandlerEnd,
                                                const MCSymbol *FilterBegin) {
  checkExceptionClause(Kind, TryBegin, TryEnd, HandlerBegin, HandlerEnd,
                       FilterBegin);
  // The scoped form of ILAsm (II.19): each block is a brace pair, and the
  // lines print before the labels that start and end the blocks.
  unsigned Clause = ClauseProgress.size();
  unsigned Ordinal = 0;
  auto Add = [&](const MCSymbol *Sym, ScopeMark Mark, StringRef Text) {
    ScopeLines[Sym].push_back({Clause, Ordinal++, Mark, Text});
  };
  Add(TryBegin, ScopeMark::Open, "  .try {");
  switch (Kind) {
  case cli::ExceptionClauseKind::Filter:
    Add(FilterBegin, ScopeMark::Middle, "  } filter {");
    Add(HandlerBegin, ScopeMark::Middle, "  } {");
    break;
  case cli::ExceptionClauseKind::Finally:
    Add(HandlerBegin, ScopeMark::Middle, "  } finally {");
    break;
  case cli::ExceptionClauseKind::Fault:
    Add(HandlerBegin, ScopeMark::Middle, "  } fault {");
    break;
  case cli::ExceptionClauseKind::Catch:
    llvm_unreachable("checkExceptionClause rejects catch clauses");
  }
  Add(HandlerEnd, ScopeMark::Close, "  }");
  ClauseProgress.push_back({0, Ordinal});
}

void MSILTargetAsmStreamer::emitLabel(MCSymbol *Symbol) {
  auto It = ScopeLines.find(Symbol);
  if (It == ScopeLines.end())
    return;
  SmallVector<ScopeLine, 2> Lines = std::move(It->second);
  ScopeLines.erase(It);
  // The ends close the innermost block first and the beginnings open the
  // outermost one first, so that nested clauses that share a label nest.
  auto Rank = [](const ScopeLine &L) { return static_cast<unsigned>(L.Mark); };
  llvm::stable_sort(Lines, [&](const ScopeLine &A, const ScopeLine &B) {
    if (A.Mark != B.Mark)
      return Rank(A) > Rank(B);
    return A.Mark == ScopeMark::Open ? A.Clause > B.Clause
                                     : A.Clause < B.Clause;
  });
  for (const ScopeLine &L : Lines) {
    // A clause's boundaries print in their order (open, middles, close);
    // otherwise its blocks are not contiguous in the code.
    unsigned &Printed = ClauseProgress[L.Clause].first;
    if (Printed != L.Ordinal)
      reportFatalInternalError("MSIL: the blocks of an exception clause are "
                               "not contiguous in the code");
    ++Printed;
    emitLine(L.Text);
  }
}

void MSILTargetAsmStreamer::emitMethodEnd(StringRef Name) {
  // Every label of every clause was emitted inside the method.
  if (!ScopeLines.empty() ||
      any_of(ClauseProgress, [](const auto &P) { return P.first != P.second; }))
    reportFatalInternalError("MSIL: an exception clause of method '" + Name +
                             "' names a label that its code does not define");
  ClauseProgress.clear();
  SmallString<64> Str;
  raw_svector_ostream OS(Str);
  OS << "} // end of method ";
  MSIL::printQuotedName(OS, Name);
  emitLine(OS.str());
  emitLine("");
}

void MSILTargetAsmStreamer::emitPInvoke(const MSILMethodDecl &M, StringRef Lib,
                                        StringRef Entry) {
  assert(M.Sig && "a P/Invoke declaration needs a signature");
  SmallString<128> Str;
  raw_svector_ostream OS(Str);
  OS << ".method " << (M.Public ? "public" : "assembly")
     << " hidebysig static pinvokeimpl(";
  MSIL::printQuotedString(OS, Lib);
  // Object-form text always names the entry, the import stub's ImplMap
  // import name (plan 5.8.3); whole-program text only when it differs from
  // the method name.
  if (!Entry.empty() && (ObjectForm || Entry != M.Name)) {
    OS << " as ";
    MSIL::printQuotedString(OS, Entry);
  }
  // The entry point is bound exactly as given (nomangle: no A/W or
  // decorated-name probing; ImplMap flags 0x0201, plan 5.10.11). 64-bit
  // targets have one native convention; stdcall is for 32-bit Windows in a
  // later phase.
  OS << (M.Sig->CC == MSIL::CallConv::UnmanagedStdcall ? " nomangle stdcall) "
                                                       : " nomangle cdecl) ");
  MSIL::printResult(OS, *M.Sig);
  OS << ' ';
  MSIL::printQuotedName(OS, M.Name);
  MSIL::printParamList(OS, *M.Sig);
  OS << " cil managed preservesig {}";
  emitLine(OS.str());
}

void MSILTargetAsmStreamer::emitDataImage(StringRef TypeName,
                                          StringRef FieldName, StringRef Label,
                                          ArrayRef<uint8_t> Bytes) {
  assert(!Bytes.empty() && "omit the data image when no byte is initialized");
  SmallString<128> Str;
  raw_svector_ostream OS(Str);

  // A value type exactly as large as the image, so the RVA field covers it.
  OS << ".class private explicit ansi sealed ";
  MSIL::printQuotedName(OS, TypeName);
  OS << " extends ";
  cli::printBCLMemberRef(OS, cli::BCLMemberId::ValueType,
                         cli::MscorlibAssembly);
  emitLine(OS.str());
  emitLine("{");
  emitLine("  .pack 1");
  emitLine("  .size " + Twine(Bytes.size()));
  emitLine("}");

  Str.clear();
  OS << ".data cil ";
  MSIL::printQuotedName(OS, Label);
  OS << " = bytearray (";
  emitLine(OS.str());

  // 16 bytes per line, the closing parenthesis after the last byte, and the
  // offset of the line's first byte as a comment in a fixed column.
  constexpr size_t BytesPerLine = 16;
  constexpr unsigned CommentColumn = 2 + BytesPerLine * 3 - 1 + 3;
  for (size_t Begin = 0, E = Bytes.size(); Begin < E; Begin += BytesPerLine) {
    size_t End = std::min(Begin + BytesPerLine, E);
    Str.clear();
    OS << ' ';
    for (size_t I = Begin; I != End; ++I)
      OS << ' ' << format_hex_no_prefix(Bytes[I], 2, /*Upper=*/true);
    if (End == E)
      OS << " )";
    OS.indent(std::max<int>(1, int(CommentColumn) - int(Str.size())));
    OS << "// " << format_hex(Begin, 6, /*Upper=*/true);
    emitLine(OS.str());
  }

  Str.clear();
  OS << ".field private static valuetype ";
  MSIL::printQuotedName(OS, TypeName);
  OS << ' ';
  MSIL::printQuotedName(OS, FieldName);
  OS << " at ";
  MSIL::printQuotedName(OS, Label);
  emitLine(OS.str());
}

void MSILTargetAsmStreamer::emitAddressField(StringRef Name, bool Public,
                                             bool Generated) {
  SmallString<64> Str;
  raw_svector_ostream OS(Str);
  // A compiler-generated field is private; the field of a C global is public
  // or assembly like its methods. The caller says which, because an asm
  // label can give a C global any name, '<msil>' ones included.
  StringRef Access = Generated ? "private" : Public ? "public" : "assembly";
  OS << ".field " << Access << " static initonly native int ";
  MSIL::printQuotedName(OS, Name);
  emitLine(OS.str());
}

void MSILTargetAsmStreamer::emitDataImportField(StringRef Name) {
  SmallString<64> Str;
  raw_svector_ostream OS(Str);
  OS << ".field public static native int ";
  MSIL::printQuotedName(OS, Name);
  emitLine(OS.str());
}

void MSILTargetAsmStreamer::emitComment(const Twine &Text) {
  emitLine("// " + Text);
}

void MSILTargetAsmStreamer::emitBlankLine() { emitLine(""); }

void MSILTargetAsmStreamer::emitObjectHeader(bool BOM, StringRef SourceFile,
                                             StringRef TripleStr) {
  ObjectForm = true;
  SmallString<256> Str;
  raw_svector_ostream OS(Str);
  // As in emitFileHeader: ilasm needs the byte order mark for non-ASCII
  // identifiers and quoted strings, and the comments escape the names.
  if (BOM)
    OS << "\xEF\xBB\xBF";
  OS << "// MSIL object generated by LLVM\n";
  OS << "// source_filename = \"";
  printEscapedString(SourceFile, OS);
  OS << "\"\n";
  OS << "// target triple   = \"";
  printEscapedString(TripleStr, OS);
  OS << '"';
  emitLine(OS.str());
  // The placeholder scopes of plan 5.8.3, in the order that gives them
  // AssemblyRef rows 1-3 (rule R6). Unbound, so no key and version 0.
  for (StringRef Name : {MSILLink::ExternAssembly, MSILLink::BCLAssembly,
                         MSILLink::LinkInfoAssembly}) {
    Str.clear();
    OS << ".assembly extern ";
    MSIL::printQuotedName(OS, Name);
    OS << " { .ver 0:0:0:0 }";
    emitLine(OS.str());
  }
  emitLine("");
}

void MSILTargetAsmStreamer::emitHelperBegin(
    StringRef Name, cli::MethodAttributes Flags, const cli::MethodSig &Sig,
    cli::MethodImplAttributes ImplFlags) {
  using cli::MethodAttributes;
  auto Has = [Flags](MethodAttributes A) {
    return (Flags & A) != MethodAttributes::PrivateScope;
  };
  SmallString<128> Str;
  raw_svector_ostream OS(Str);
  OS << ".method ";
  switch (Flags & MethodAttributes::MemberAccessMask) {
  case MethodAttributes::Private:
    OS << "private";
    break;
  case MethodAttributes::Assembly:
    OS << "assembly";
    break;
  case MethodAttributes::Public:
    OS << "public";
    break;
  default:
    reportFatalInternalError("MSIL: a synthesized method's access is not "
                             "private, assembly or public");
  }
  if (Has(MethodAttributes::HideBySig))
    OS << " hidebysig";
  if (Has(MethodAttributes::SpecialName))
    OS << " specialname";
  if (Has(MethodAttributes::RTSpecialName))
    OS << " rtspecialname";
  if (Has(MethodAttributes::Static))
    OS << " static";
  OS << ' ';
  printSynthesizedMethod(OS, Name, Sig);
  OS << " cil managed";
  // The implementation flags of the trailer definitions (plan 5.14.4 T4).
  if (ImplFlags == cli::MethodImplAttributes::NoInlining)
    OS << " noinlining";
  else if (ImplFlags != cli::MethodImplAttributes{})
    reportFatalInternalError("MSIL: a synthesized method's implementation "
                             "flags are not IL managed or noinlining");
  // As in emitMethodBegin: both lines in one raw text.
  OS << "\n{";
  emitLine(OS.str());
}

void MSILTargetAsmStreamer::emitStringInstruction(cli::Opcode Op,
                                                  StringRef UTF8) {
  SmallString<64> Str;
  raw_svector_ostream OS(Str);
  // The escapes of the P/Invoke strings. A non-ASCII string needs the file's
  // byte order mark (decision D11), which the AsmPrinter requests when an
  // import string is not ASCII.
  OS << "  " << cli::getOpcodeInfo(Op).Name << ' ';
  MSIL::printQuotedString(OS, UTF8);
  emitLine(OS.str());
}

void MSILTargetAsmStreamer::emitHelperInstruction(cli::Opcode Op,
                                                  StringRef Name,
                                                  const cli::MethodSig &Sig) {
  SmallString<64> Str;
  raw_svector_ostream OS(Str);
  OS << "  " << cli::getOpcodeInfo(Op).Name << ' ';
  printSynthesizedMethod(OS, Name, Sig);
  emitLine(OS.str());
}

void MSILTargetAsmStreamer::emitLinkInfo(StringRef ModuleName,
                                         const MSILLink::Record &R) {
  SmallVector<uint8_t, 256> Bytes;
  cli::encodeLinkInfo(R, Bytes);
  SmallVector<uint8_t, 256> Blob;
  cli::encodeByteArrayAttribute(Bytes, Blob);

  SmallString<64> Str;
  raw_svector_ostream OS(Str);
  // Both ilasms accept a trailing '.module', attach the '.custom' that
  // follows it to the Module row and number its constructor's MemberRef
  // last (plan 5.8.4).
  OS << ".module ";
  MSIL::printQuotedName(OS, ModuleName);
  emitLine(OS.str());

  // The value blob of the carrier (ECMA-335 II.23.3): the prolog 01 00, the
  // uint8[] as an int32 count and the bytes, and no named arguments (00 00),
  // on one line.
  std::string Line;
  raw_string_ostream LS(Line);
  LS << ".custom instance void [" << MSILLink::LinkInfoAssembly << ']';
  MSIL::printQuotedName(LS, MSILLink::LinkInfoType);
  LS << "::.ctor(uint8[]) = (";
  for (uint8_t B : Blob)
    LS << ' ' << format_hex_no_prefix(B, 2, /*Upper=*/true);
  LS << " )";
  emitLine(Line);
}
