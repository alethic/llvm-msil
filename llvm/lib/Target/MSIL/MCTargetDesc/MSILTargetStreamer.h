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
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/Signature.h"
#include "llvm/MC/MCStreamer.h"
#include <cstdint>

namespace llvm {

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
};

/// The ILAsm text implementation. Every directive is written as whole lines
/// through MCStreamer::emitRawText, so pending verbose-asm comments end up on
/// the line they belong to.
class MSILTargetAsmStreamer final : public MSILTargetStreamer {
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
  void emitMethodEnd(StringRef Name) override;
  void emitPInvoke(const MSILMethodDecl &M, StringRef Lib,
                   StringRef Entry) override;
  void emitDataImage(StringRef TypeName, StringRef FieldName, StringRef Label,
                     ArrayRef<uint8_t> Bytes) override;
  void emitAddressField(StringRef Name, bool Public, bool Generated) override;
  void emitComment(const Twine &Text) override;
  void emitBlankLine() override;
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
  void emitMethodEnd(StringRef) override {}
  void emitPInvoke(const MSILMethodDecl &, StringRef, StringRef) override {}
  void emitDataImage(StringRef, StringRef, StringRef,
                     ArrayRef<uint8_t>) override {}
  void emitAddressField(StringRef, bool, bool) override {}
  void emitComment(const Twine &) override {}
  void emitBlankLine() override {}
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILTARGETSTREAMER_H
