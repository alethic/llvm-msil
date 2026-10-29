//===-- MSILMCAsmInfo.cpp - MSIL asm properties ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the definition of the MSILMCAsmInfo properties. ILAsm
/// directives are written by MSILTargetAsmStreamer as raw text; these settings
/// make sure that nothing the generic AsmPrinter emits on its own turns into a
/// GNU directive ilasm would reject (cf. NVPTXMCAsmInfo).
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCAsmInfo.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

MSILMCAsmInfo::~MSILMCAsmInfo() = default; // anchor.

MSILMCAsmInfo::MSILMCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfo(Options) {
  CodePointerSize = CalleeSaveStackSlotSize = TT.isArch64Bit() ? 8 : 4;

  // ILAsm accepts // and /* */ comments.
  CommentString = "//";

  // Block labels print as @BB0_1 and temporary labels as @func_end0: '@' may
  // start an ILAsm identifier, no C identifier contains it, and ilasm rejects
  // labels that start with '.'.
  InternalSymbolPrefix = "@";
  AllowAtInName = true;

  // MCSymbol::print would quote with double quotes, which ILAsm identifiers do
  // not accept: fail loudly instead. Symbol-derived identifiers are printed
  // single-quoted by MSIL::printQuotedName.
  SupportsQuotedNames = false;

  // No .file, .ident, .type/.size or function alignment directives.
  HasSingleParameterDotFile = false;
  HasIdentDirective = false;
  HasDotTypeDotSizeDirective = false;
  HasFunctionAlignment = false;

  // No .hidden/.protected/.exported: CIL accessibility is part of the method
  // and field headers.
  HiddenVisibilityAttr = HiddenDeclarationVisibilityAttr = MCSA_Invalid;
  ProtectedVisibilityAttr = ExportedVisibilityAttr = MCSA_Invalid;

  // MSILAsmPrinter::emitLinkage prints nothing; should .globl or .weak ever be
  // reached, they are comments.
  GlobalDirective = "\t// .globl\t";
  WeakDirective = "\t// .weak\t";

  // No DWARF or CodeView (Portable PDB is a later phase) and no unwinding.
  // The exception model comes from the module, not from here:
  // Module::getExceptionModel() falls back to
  // Triple::getDefaultExceptionHandling(), which is None for the MSIL object
  // format.
  SupportsDebugInformation = false;

  // ILAsm text only: ilasm assembles it, there is no object writer.
  UseIntegratedAssembler = false;

  // printSwitchToSection keeps the empty MCAsmInfo implementation, so section
  // switches print nothing.
}
