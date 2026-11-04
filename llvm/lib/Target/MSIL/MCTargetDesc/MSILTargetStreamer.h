//===-- MSILTargetStreamer.h - MSIL target streamer -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the MSIL target streamer: the ILAsm directives of a
/// module (header, method headers and footers, P/Invoke declarations, the data
/// image, the address fields, comments and blank lines). Instructions, one
/// CIL_ MCInst per CIL instruction, and labels go through the MCStreamer
/// (MSILInstPrinter in text), so the AsmPrinter emits only MCInsts, labels and
/// these structured calls.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILTARGETSTREAMER_H
#define LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILTARGETSTREAMER_H

#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/Signature.h"
#include "llvm/MC/MCStreamer.h"
#include <cstdint>

namespace llvm {

class MCSymbol;

/// What a method definition is: the method of a C function, or one of the
/// methods the AsmPrinter synthesizes (decisions D12, D27).
enum class MSILMethodKind : uint8_t {
  /// '.method <public|assembly> static <ret> '<name>'(<params>) cil managed'.
  Function,
  /// '.method private hidebysig specialname rtspecialname static void
  /// .cctor() cil managed', the module initializer.
  ModuleInitializer,
  /// '.method private hidebysig static int32 '<name>'(string[] args) cil
  /// managed', the method of '.entrypoint'.
  EntryPoint,
};

/// A method definition or P/Invoke declaration.
struct MSILMethodDecl {
  /// The IL name, unquoted (the streamer quotes it).
  StringRef Name;
  /// The method signature (Function and P/Invoke declarations).
  const MSIL::Signature *Sig = nullptr;
  /// 'public' (non-local linkage) or 'assembly' (internal or private).
  bool Public = true;
  /// The kind of method (method definitions only).
  MSILMethodKind Kind = MSILMethodKind::Function;
};

/// MSIL-specific streamer interface for the ILAsm directives.
///
/// In whole-program mode (-S, llc and msil-ld's --lto-emit-asm text) the
/// AsmPrinter emits: emitFileHeader, emitAssemblyRefs and emitAssembly; the
/// methods of the functions; the module trailer (P/Invoke declarations, data
/// image and relocation table, address fields, comments, and the methods
/// that cli::buildTrailer makes: the data-import helpers, the module
/// initializer and the entry point). In object mode
/// (MSILTargetMachine::isObjectMode(), plan 5.8) it emits, in this order (the
/// object event order, plan 5.3 as amended for M19):
/// - emitObjectHeader;
/// - one method per function definition, in IR order (emitMethodBegin,
///   emitMaxStack, emitLocals, emitExceptionClause for each of its clauses,
///   the instructions and labels, emitMethodEnd);
/// - emitAddressField for each defined global variable (commons included),
///   in the order of the link-info record's SYMTAB;
/// - emitPInvoke for each import stub of plan 5.8.3: the stubs on a module and
///   on 'msil.unresolved' in first-use order, then the address-only stubs in
///   SYMTAB order;
/// - emitLinkInfo, last.
/// It also emits comments and blank lines in object-form text, never the
/// whole-program directives (emitFileHeader, emitAssemblyRefs, emitAssembly,
/// emitEntryPoint, emitDataImage). Object-form text is the ILAsm of exactly
/// the object that MSILObjectWriter writes from the same events (plan 5.8.4).
class MSILTargetStreamer : public MCTargetStreamer {
public:
  explicit MSILTargetStreamer(MCStreamer &S);
  ~MSILTargetStreamer() override;

  /// The leading comments: source file name and target triple, preceded by a
  /// UTF-8 byte order mark if \p BOM (any global name is not ASCII).
  virtual void emitFileHeader(bool BOM, StringRef SourceFile,
                              StringRef TripleStr) = 0;
  /// .assembly extern mscorlib { ... }
  virtual void emitAssemblyRefs() = 0;
  /// .assembly '<Name>' { .ver 0:0:0:0 }
  virtual void emitAssembly(StringRef Name) = 0;
  /// The header of \p M (by its kind, see MSILMethodKind) followed by the
  /// line '{'.
  virtual void emitMethodBegin(const MSILMethodDecl &M) = 0;
  /// .entrypoint
  virtual void emitEntryPoint() = 0;
  /// .maxstack N
  virtual void emitMaxStack(unsigned N) = 0;
  /// .locals (<types>): never 'init'; nothing if \p Locals is empty. The
  /// types print as canonical listing v1 text (int32, native int, string[],
  /// ...); type references are not used.
  virtual void emitLocals(ArrayRef<cli::TypeSig> Locals) = 0;
  /// An exception handling clause of the current method (plan 5.15.4),
  /// emitted after emitLocals and before the method's first instruction,
  /// innermost clause first, with the labels that the method's code defines
  /// later through MCStreamer::emitLabel: the protected block runs from
  /// \p TryBegin to \p TryEnd, the handler from \p HandlerBegin to
  /// \p HandlerEnd, and a Filter clause's filter block from \p FilterBegin
  /// (null for every other kind) to \p HandlerBegin. The blocks are
  /// contiguous: a Filter clause's \p TryEnd is its \p FilterBegin, and
  /// every other clause's \p TryEnd is its \p HandlerBegin. Catch clauses
  /// (which need a class token) and blocks that are not contiguous are
  /// internal errors. In text the scoped forms '.try { } filter { } { }' and
  /// '.try { } fault { }' print around the labels; an object records the
  /// clause in the method body's EH section with the labels' offsets.
  virtual void emitExceptionClause(cli::ExceptionClauseKind Kind,
                                   const MCSymbol *TryBegin,
                                   const MCSymbol *TryEnd,
                                   const MCSymbol *HandlerBegin,
                                   const MCSymbol *HandlerEnd,
                                   const MCSymbol *FilterBegin) = 0;
  /// } // end of method '<Name>'
  virtual void emitMethodEnd(StringRef Name) = 0;
  /// A P/Invoke declaration of \p M into library \p Lib; \p Entry is the
  /// native entry point when it differs from the method name:
  /// 'pinvokeimpl("<Lib>" [as "<Entry>"] nomangle cdecl)' (ImplMap flags
  /// 0x0201, plan 5.10.11).
  virtual void emitPInvoke(const MSILMethodDecl &M, StringRef Lib,
                           StringRef Entry) = 0;
  /// The initialized data image: a sized value type \p TypeName, the RVA blob
  /// \p Label holding \p Bytes (not empty), and the static field \p FieldName
  /// of that type placed at the blob.
  virtual void emitDataImage(StringRef TypeName, StringRef FieldName,
                             StringRef Label, ArrayRef<uint8_t> Bytes) = 0;
  /// .field <access> static initonly native int '<Name>': the address of a
  /// global variable, 'public' if \p Public (non-local linkage) and
  /// 'assembly' otherwise, or 'private' if \p Generated (the base of the data
  /// segment, which the compiler generates).
  virtual void emitAddressField(StringRef Name, bool Public,
                                bool Generated = false) = 0;
  /// A comment line, '// <Text>' (the section comments of the module
  /// trailer). Nothing in an object.
  virtual void emitComment(const Twine &Text) = 0;
  /// An empty line, between the parts of the module. Nothing in an object.
  virtual void emitBlankLine() = 0;
  /// The start of a CIL object (object mode only, plan 5.8.4): in text, the
  /// comments '// MSIL object generated by LLVM', the source file name and
  /// the target triple, preceded by a UTF-8 byte order mark if \p BOM (any
  /// name or import string is not ASCII, decision D11), and the three
  /// placeholder assembly references
  /// '.assembly extern 'msil.extern' { .ver 0:0:0:0 }', 'msil.bcl' and
  /// 'msil.link' (plan 5.8.3). From here on the text is object-form text:
  /// every P/Invoke declaration spells 'as "<entry>"'. In an object it
  /// starts the module (MSILObjectWriter::setModule).
  virtual void emitObjectHeader(bool BOM, StringRef SourceFile,
                                StringRef TripleStr) = 0;
  /// The end of a CIL object (object mode only, plan 5.8.4, 5.8.7): in text,
  /// '.module '<ModuleName>'' and the custom attribute on the Module row that
  /// carries the link-info record \p R,
  /// '.custom instance void [msil.link]'<msil>LinkInfo'::.ctor(uint8[]) =
  /// ( 01 00 <N, LE32> <the N bytes of cli::encodeLinkInfo(R)> 00 00 )'.
  /// \p ModuleName is the Module row's name (rule O3). Every token in \p R
  /// is numbered by row order R6; MSILObjectWriter checks it against its own
  /// rows (MSILObjectWriter::setLinkInfo).
  virtual void emitLinkInfo(StringRef ModuleName,
                            const MSILLink::Record &R) = 0;

  // The parts of the trailer's synthesized methods (cli::buildTrailer, plan
  // 5.9.10) that MSIL::Signature, MSILMethodDecl and MSILMemberRefExpr cannot
  // spell: the headers of the helper methods (the data-import helpers, whose
  // signatures take strings, and the trailer definitions, which have
  // implementation flags), and the data-import helpers' instructions whose
  // token operand is a string literal or such a helper. Whole-program text
  // only: the defaults are internal errors, which the object streamer keeps.

  /// The header of a synthesized helper method followed by the line '{':
  /// '.method <access> [hidebysig] [specialname] [rtspecialname] [static]
  /// <ret> '<Name>'(<params>) cil managed [noinlining]', from \p Flags, \p Sig
  /// and \p ImplFlags (plan 5.10.12: '<msil>dataimport',
  /// '<msil>init_dataimports'; plan 5.14.4 T4: the trailer definitions, such
  /// as '__msil_compiler_barrier'). Of the implementation flags only
  /// NoInlining ('noinlining') prints; any other nonzero \p ImplFlags is an
  /// internal error.
  virtual void emitHelperBegin(StringRef Name, cli::MethodAttributes Flags,
                               const cli::MethodSig &Sig,
                               cli::MethodImplAttributes ImplFlags);
  /// An instruction whose token operand is the string literal \p UTF8:
  /// '  ldstr "<UTF8>"'.
  virtual void emitStringInstruction(cli::Opcode Op, StringRef UTF8);
  /// An instruction whose token operand is the synthesized helper \p Name
  /// with signature \p Sig: '  call <ret> '<Name>'(<params>)'.
  virtual void emitHelperInstruction(cli::Opcode Op, StringRef Name,
                                     const cli::MethodSig &Sig);
  /// '.field public static native int '<Name>'': the address field of a
  /// native data import, which '<msil>init_dataimports' stores, so it is not
  /// 'initonly' (ECMA-335 II.16.1.2: only the type's .cctor may store an
  /// initonly static field; plan 5.10.12).
  virtual void emitDataImportField(StringRef Name);

protected:
  /// Checks the operands of emitExceptionClause: the labels that \p Kind
  /// needs, and contiguous blocks. Anything else is an internal error.
  static void
  checkExceptionClause(cli::ExceptionClauseKind Kind, const MCSymbol *TryBegin,
                       const MCSymbol *TryEnd, const MCSymbol *HandlerBegin,
                       const MCSymbol *HandlerEnd, const MCSymbol *FilterBegin);
};

/// The ILAsm text implementation. Every directive is written as whole lines
/// through MCStreamer::emitRawText, so pending verbose-asm comments end up on
/// the line they belong to.
class MSILTargetAsmStreamer final : public MSILTargetStreamer {
  /// Set by emitObjectHeader: the text is object-form text.
  bool ObjectForm = false;

  /// The scope lines of the current method's clauses that print before a
  /// label (emitLabel): each clause boundary at that label, in clause order
  /// (innermost first).
  enum class ScopeMark : uint8_t { Open, Middle, Close };
  struct ScopeLine {
    /// The index of the clause in the method (innermost first).
    unsigned Clause;
    /// The position of the boundary among its clause's, from 0.
    unsigned Ordinal;
    ScopeMark Mark;
    /// The line, such as '  } filter {'.
    StringRef Text;
  };
  DenseMap<const MCSymbol *, SmallVector<ScopeLine, 2>> ScopeLines;
  /// For each clause of the current method, the number of its boundaries
  /// printed so far, and the number it has.
  SmallVector<std::pair<unsigned, unsigned>, 2> ClauseProgress;

  void emitLine(const Twine &Line);

public:
  explicit MSILTargetAsmStreamer(MCStreamer &S);

  void emitFileHeader(bool BOM, StringRef SourceFile,
                      StringRef TripleStr) override;
  void emitAssemblyRefs() override;
  void emitAssembly(StringRef Name) override;
  void emitMethodBegin(const MSILMethodDecl &M) override;
  void emitEntryPoint() override;
  void emitMaxStack(unsigned N) override;
  void emitLocals(ArrayRef<cli::TypeSig> Locals) override;
  void emitExceptionClause(cli::ExceptionClauseKind Kind,
                           const MCSymbol *TryBegin, const MCSymbol *TryEnd,
                           const MCSymbol *HandlerBegin,
                           const MCSymbol *HandlerEnd,
                           const MCSymbol *FilterBegin) override;
  /// Prints the scope lines of the clauses that begin, change block or end
  /// at \p Symbol (MCStreamer::emitLabel calls this before it prints the
  /// label): the ends first, innermost first, then the block changes, then
  /// the beginnings, outermost first.
  void emitLabel(MCSymbol *Symbol) override;
  void emitMethodEnd(StringRef Name) override;
  void emitPInvoke(const MSILMethodDecl &M, StringRef Lib,
                   StringRef Entry) override;
  void emitDataImage(StringRef TypeName, StringRef FieldName, StringRef Label,
                     ArrayRef<uint8_t> Bytes) override;
  void emitAddressField(StringRef Name, bool Public, bool Generated) override;
  void emitComment(const Twine &Text) override;
  void emitBlankLine() override;
  void emitObjectHeader(bool BOM, StringRef SourceFile,
                        StringRef TripleStr) override;
  void emitLinkInfo(StringRef ModuleName, const MSILLink::Record &R) override;
  void emitHelperBegin(StringRef Name, cli::MethodAttributes Flags,
                       const cli::MethodSig &Sig,
                       cli::MethodImplAttributes ImplFlags) override;
  void emitStringInstruction(cli::Opcode Op, StringRef UTF8) override;
  void emitHelperInstruction(cli::Opcode Op, StringRef Name,
                             const cli::MethodSig &Sig) override;
  void emitDataImportField(StringRef Name) override;
};

/// The implementation for -filetype=null: emits nothing.
class MSILTargetNullStreamer final : public MSILTargetStreamer {
public:
  explicit MSILTargetNullStreamer(MCStreamer &S) : MSILTargetStreamer(S) {}

  void emitFileHeader(bool, StringRef, StringRef) override {}
  void emitAssemblyRefs() override {}
  void emitAssembly(StringRef) override {}
  void emitMethodBegin(const MSILMethodDecl &) override {}
  void emitEntryPoint() override {}
  void emitMaxStack(unsigned) override {}
  void emitLocals(ArrayRef<cli::TypeSig>) override {}
  void emitExceptionClause(cli::ExceptionClauseKind, const MCSymbol *,
                           const MCSymbol *, const MCSymbol *, const MCSymbol *,
                           const MCSymbol *) override {}
  void emitMethodEnd(StringRef) override {}
  void emitPInvoke(const MSILMethodDecl &, StringRef, StringRef) override {}
  void emitDataImage(StringRef, StringRef, StringRef,
                     ArrayRef<uint8_t>) override {}
  void emitAddressField(StringRef, bool, bool) override {}
  void emitComment(const Twine &) override {}
  void emitBlankLine() override {}
  void emitObjectHeader(bool, StringRef, StringRef) override {}
  void emitLinkInfo(StringRef, const MSILLink::Record &) override {}
  void emitHelperBegin(StringRef, cli::MethodAttributes, const cli::MethodSig &,
                       cli::MethodImplAttributes) override {}
  void emitStringInstruction(cli::Opcode, StringRef) override {}
  void emitHelperInstruction(cli::Opcode, StringRef,
                             const cli::MethodSig &) override {}
  void emitDataImportField(StringRef) override {}
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILTARGETSTREAMER_H
