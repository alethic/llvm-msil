//===-- MSILEHClauses.h - IL exception clauses of methods -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the code that the AsmPrinter writes itself around and
/// instead of a method body (ImplementationPlan.md 5.15.4):
/// - the whole-body '.try' of a function whose IR carries
///   "msil-sjlj-invoke"="<i>" (a setjmp wrapper that MSILLowerSjLj made, i the
///   IR index of its native int status parameter), with the fixed filter that
///   accepts only the ExternalException of a longjmp aimed at its own armed
///   status block, the empty handler and the tail that returns;
/// - the body of the throw helper "msil-sjlj-throw" ('__msil_sjlj_throw'),
///   which throws that ExternalException with the status block's low 32 bits
///   as its HResult.
/// Their clauses reach the streamers through
/// MSILTargetStreamer::emitExceptionClause.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILEHCLAUSES_H
#define LLVM_LIB_TARGET_MSIL_MSILEHCLAUSES_H

#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <optional>

namespace llvm {
class Function;
class MCContext;
class MCSection;
class MCStreamer;
class MCSymbol;
class MSILTargetStreamer;
class TargetMachine;

namespace MSIL {

/// The whole-body exception clause that the AsmPrinter gives a method.
enum class WholeBodyKind : uint8_t {
  /// None: the method body is printed as it is.
  None,
  /// The setjmp wrapper's '.try' with the fixed filter (plan 5.15.4), for a
  /// function with "msil-sjlj-invoke"="<status index>".
  SjLjFilter,
};

/// The whole-body clause of \p F.
WholeBodyKind getWholeBodyKind(const Function &F);

/// Whether \p F is the throw helper "msil-sjlj-throw" ('__msil_sjlj_throw'),
/// whose IR body is 'unreachable' and whose IL body the AsmPrinter writes.
bool isSjLjThrowHelper(const Function &F);

/// The least .maxstack of \p F's method: 3 for a function with a whole-body
/// clause or the throw helper (plan 5.15.4), else 0. MSILStackCheck raises
/// the computed value to it.
unsigned getMinMaxStack(const Function &F);

/// Emits the IL body of the throw helper \p F through \p EmitInst: 'ldnull',
/// 'ldarg.0', 'conv.i4', 'newobj instance void
/// <core>System.Runtime.InteropServices.ExternalException::.ctor(string,
/// int32)' and 'throw'. A helper whose signature is not void(native int) is
/// an internal error.
void emitSjLjThrowBody(const Function &F, const TargetMachine &TM,
                       MCContext &Ctx,
                       function_ref<void(const MCInst &)> EmitInst);

} // end namespace MSIL

/// The whole-body '.try' with the fixed filter of a setjmp wrapper (plan
/// 5.15.4, M27.a (2)). Its labels are '@EH<f>_<k>', f the function number
/// as in '@BB<f>_<b>' and k: 0 TryBegin, 1 FilterBegin (also TryEnd), 2-4 in
/// the filter, 5 HandlerBegin, 6 Done (also HandlerEnd).
///
/// The AsmPrinter uses it in this order:
/// - the constructor, while it lists the method's locals: a value-returning
///   method gets the extra local R of its result type after them;
/// - emitBegin, after .locals and before the first instruction: the clause
///   event and the label TryBegin;
/// - the body, whose RETURNs MSILMCInstLower lowers to '[stloc R] leave
///   <Done>' (MSILMCInstLower::setReturnAsLeave with getDoneLabel and
///   getResultLocal);
/// - emitEnd, after the last instruction: the filter, the handler and the
///   tail '@EH<f>_6: [ldloc R] ret'.
class LLVM_LIBRARY_VISIBILITY MSILWholeBodyTry {
  enum : unsigned {
    TryBegin = 0,
    FilterBegin = 1,
    FilterMatch = 2,
    FilterNoMatch = 3,
    FilterEnd = 4,
    HandlerBegin = 5,
    Done = 6,
    NumLabels = 7,
  };
  MCContext &Ctx;
  MCSymbol *Labels[NumLabels];
  /// The IL argument index of the status block parameter.
  unsigned StatusArg;
  /// The local R that holds the result across the 'leave' (value-returning
  /// wrappers only).
  std::optional<unsigned> ResultLocal;
  /// The section of the method's code (the streamer's section at
  /// emitBegin), where emitEnd's code goes too.
  MCSection *Section = nullptr;

public:
  /// Prepares the clause of \p F, whose getWholeBodyKind is SjLjFilter, with
  /// the IL results \p Results of its method; \p FunctionNumber names the
  /// labels. A value-returning method's local R is appended to \p Locals.
  /// An attribute value that does not name a pointer parameter is an
  /// internal error.
  MSILWholeBodyTry(const Function &F, const TargetMachine &TM, MCContext &Ctx,
                   unsigned FunctionNumber, ArrayRef<MVT> Results,
                   SmallVectorImpl<cli::TypeSig> &Locals);

  /// The label after the handler, which every 'leave' targets.
  MCSymbol *getDoneLabel() const { return Labels[Done]; }
  std::optional<unsigned> getResultLocal() const { return ResultLocal; }

  /// Emits the clause event (MSILTargetStreamer::emitExceptionClause) and
  /// then the label TryBegin through \p OS.
  void emitBegin(MSILTargetStreamer &TS, MCStreamer &OS);

  /// Emits the code after the body, the filter, the handler and the tail,
  /// with their labels through \p OS and the instructions through
  /// \p EmitInst, in the method's section (AsmPrinter switches to the
  /// function's section before emitFunctionBodyEnd, and a method of an
  /// object has a section of its own), then switches back.
  void emitEnd(MCStreamer &OS,
               function_ref<void(const MCInst &)> EmitInst) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MSILEHCLAUSES_H
