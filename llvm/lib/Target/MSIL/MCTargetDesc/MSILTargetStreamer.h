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
/// image and the address fields). Instructions and labels go through the
/// stock MCAsmStreamer and MSILInstPrinter.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILTARGETSTREAMER_H
#define LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILTARGETSTREAMER_H

#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCStreamer.h"
#include <cstdint>

namespace llvm {

/// A method definition or P/Invoke declaration.
struct MSILMethodDecl {
  /// The IL name, unquoted (the streamer quotes it).
  StringRef Name;
  /// The method signature.
  const MSIL::Signature *Sig = nullptr;
  /// 'public' (non-local linkage) or 'assembly' (internal or private).
  bool Public = true;
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
  /// .method <public|assembly> static <ret> '<name>'(<params>) cil managed
  /// followed by the line '{'.
  virtual void emitMethodBegin(const MSILMethodDecl &M) = 0;
  /// .entrypoint
  virtual void emitEntryPoint() = 0;
  /// .maxstack N
  virtual void emitMaxStack(unsigned N) = 0;
  /// .locals (<types>): never 'init'; nothing if \p Locals is empty.
  virtual void emitLocals(ArrayRef<MSIL::ValType> Locals) = 0;
  /// } // end of method '<Name>'
  virtual void emitMethodEnd(StringRef Name) = 0;
  /// A P/Invoke declaration of \p M into library \p Lib; \p Entry is the
  /// native entry point when it differs from the method name.
  virtual void emitPInvoke(const MSILMethodDecl &M, StringRef Lib,
                           StringRef Entry) = 0;
  /// The initialized data image: a sized value type \p TypeName, the RVA blob
  /// \p Label holding \p Bytes (not empty), and the static field \p FieldName
  /// of that type placed at the blob.
  virtual void emitDataImage(StringRef TypeName, StringRef FieldName,
                             StringRef Label, ArrayRef<uint8_t> Bytes) = 0;
  /// .field <access> static initonly native int '<Name>': a global's address.
  virtual void emitAddressField(StringRef Name, bool Public) = 0;
  /// One line of text, verbatim (synthesized methods such as the module
  /// initializer and the entry point, and section comments).
  virtual void emitILLine(const Twine &Line) = 0;
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
  void emitLocals(ArrayRef<MSIL::ValType> Locals) override;
  void emitMethodEnd(StringRef Name) override;
  void emitPInvoke(const MSILMethodDecl &M, StringRef Lib,
                   StringRef Entry) override;
  void emitDataImage(StringRef TypeName, StringRef FieldName, StringRef Label,
                     ArrayRef<uint8_t> Bytes) override;
  void emitAddressField(StringRef Name, bool Public) override;
  void emitILLine(const Twine &Line) override;
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
  void emitLocals(ArrayRef<MSIL::ValType>) override {}
  void emitMethodEnd(StringRef) override {}
  void emitPInvoke(const MSILMethodDecl &, StringRef, StringRef) override {}
  void emitDataImage(StringRef, StringRef, StringRef,
                     ArrayRef<uint8_t>) override {}
  void emitAddressField(StringRef, bool) override {}
  void emitILLine(const Twine &) override {}
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILTARGETSTREAMER_H
