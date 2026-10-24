//===- llvm/CLI/ILBuilder.h - ECMA-335 CIL method builder -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares ILBuilder, the assembler of synthesized CIL method
// bodies: msil-ld's module initializer, '<msil>Main', stubs and accessors,
// and the MSIL target's synthesized methods. A client emits instructions
// with symbolic labels and token references, calls layout(), and then either
// reads instructions() (the MSIL target turns them into MCInsts) or calls
// encode() for the bytes (msil-ld and the library).
//
// layout() chooses the branch forms, assigns offsets and computes the
// maximum stack depth:
// - argument, local and ldc.i4 emitters use the shortest form, as the MSIL
//   target prints it (ldarg.0-3, ldarg.s below 256, ldarg; starg, ldarga and
//   ldloca have no .N forms; ldc.i4.m1, ldc.i4.0-8, ldc.i4.s, ldc.i4);
//   an argument or local index above 65534 is a layout error;
// - every branch starts short and is widened while its displacement leaves
//   [-128, 127], until nothing changes; switch entries are always 32-bit;
//   with setRelaxBranches(false) branches keep the form they were given;
// - the stack depth follows ECMA-335 III.1.7.5's forward pass: after an
//   unconditional transfer (br, leave, ret, throw, rethrow, endfinally,
//   endfilter, jmp) the depth is the one a forward branch recorded for the
//   next instruction, else 0; catch and filter handlers and filter blocks
//   start at depth 1, try blocks and finally and fault handlers at 0;
//   leave empties the stack.
// Layout errors have the prefix "invalid IL body: ".
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_ILBUILDER_H
#define LLVM_CLI_ILBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

namespace llvm {
namespace cli {

/// A position in the code of one ILBuilder, created by createLabel() and
/// placed once by placeLabel().
struct ILLabel {
  uint32_t Id = UINT32_MAX;
  bool isValid() const { return Id != UINT32_MAX; }
};

/// One instruction of an ILBuilder. Before layout() Op is the opcode as
/// emitted and Offset is 0; after it, the final opcode (branches in their
/// chosen form) and its offset in the code.
struct ILInstr {
  Opcode Op = Opcode::Nop;
  uint32_t Offset = 0;
  /// The inline operand of ShortInlineI, InlineI, InlineI8, ShortInlineVar
  /// and InlineVar opcodes, as emitted; 0 for every other opcode (ldarg.1 and
  /// ldc.i4.5 included).
  int64_t Imm = 0;
  /// ldc.r4 (the low 32 bits) and ldc.r8: the exact IEEE bits.
  uint64_t FPBits = 0;
  /// The token operand; kind None for opcodes without one.
  TokenRef Token;
  /// The target of a branch; invalid for every other opcode.
  ILLabel Target;
  /// The targets of a switch: getSwitchTargets().
  uint32_t SwitchBegin = 0, SwitchCount = 0;
};

/// One exception handling clause with label boundaries: [TryBegin, TryEnd)
/// is the try block, [HandlerBegin, HandlerEnd) the handler, and for a
/// filter [FilterBegin, HandlerBegin) the filter block. CatchType is the
/// class of a catch clause; FilterBegin and CatchType are ignored for the
/// other kinds.
struct ILExceptionClause {
  ExceptionClauseKind Kind = ExceptionClauseKind::Catch;
  ILLabel TryBegin, TryEnd, HandlerBegin, HandlerEnd;
  ILLabel FilterBegin;
  TokenRef CatchType;
};

/// Turns a token operand into its final token (MetadataBuilder::resolveToken
/// resolves Token and Handle references; a client with External references
/// wraps it).
using TokenResolver = function_ref<Expected<uint32_t>(TokenRef)>;

class ILBuilder {
public:
  /// \p ReturnsValue: whether the method returns a value, which ret pops.
  LLVM_ABI explicit ILBuilder(bool ReturnsValue);

  /// A new, unplaced label.
  LLVM_ABI ILLabel createLabel();
  /// Places \p L before the next instruction emitted (or at the end of the
  /// code if none follows). Each label is placed once.
  LLVM_ABI void placeLabel(ILLabel L);

  /// An InlineNone opcode other than ret (use emitRet), prefixes included.
  LLVM_ABI void emit(Opcode Op);
  /// A ShortInlineI, InlineI, InlineI8, ShortInlineVar or InlineVar opcode
  /// with \p Value as its operand, in the form given (never shortened).
  LLVM_ABI void emitImm(Opcode Op, int64_t Value);
  /// ldc.r4 and ldc.r8 with the exact bits \p Bits (signaling NaNs kept).
  LLVM_ABI void emitR4(uint32_t Bits);
  LLVM_ABI void emitR8(uint64_t Bits);
  /// An opcode with a token operand and a fixed stack effect: field, type,
  /// tok and string operands, ldftn and ldvirtftn, and constrained.
  LLVM_ABI void emitToken(Opcode Op, TokenRef T);
  /// call, callvirt, newobj, calli or jmp (ldftn and ldvirtftn are accepted
  /// too). \p Pops counts every value the instruction pops (this and calli's
  /// function pointer included), \p Pushes the values it pushes (0 or 1).
  LLVM_ABI void emitCall(Opcode Op, TokenRef Callee, unsigned Pops,
                         unsigned Pushes);
  /// ret; pops one value iff the method returns one.
  LLVM_ABI void emitRet();
  /// br, brfalse, brtrue, b<cc>[.un] or leave, in the short or long
  /// spelling, to \p Target.
  LLVM_ABI void emitBranch(Opcode Op, ILLabel Target);
  /// switch to \p Targets (32-bit entries).
  LLVM_ABI void emitSwitch(ArrayRef<ILLabel> Targets);
  /// The shortest ldc.i4 form: ldc.i4.m1, ldc.i4.0-8, ldc.i4.s, ldc.i4.
  LLVM_ABI void emitLdcI4(int32_t V);
  /// The shortest forms of the argument and local instructions; an index
  /// above 65534 is a layout error.
  LLVM_ABI void emitLdarg(unsigned N);
  LLVM_ABI void emitLdarga(unsigned N);
  LLVM_ABI void emitStarg(unsigned N);
  LLVM_ABI void emitLdloc(unsigned N);
  LLVM_ABI void emitLdloca(unsigned N);
  LLVM_ABI void emitStloc(unsigned N);

  /// Adds a clause; nested clauses are added innermost first (II.19).
  LLVM_ABI void addExceptionClause(const ILExceptionClause &C);

  /// Whether layout() may change branch forms (the default). Without
  /// relaxation each branch keeps the form it was given, and a short form
  /// whose displacement does not fit is a layout error.
  void setRelaxBranches(bool Relax) { RelaxBranches = Relax; }
  /// An optional floor for getMaxStack(); at most 65535.
  LLVM_ABI void setMaxStack(unsigned N);

  /// Chooses branch forms, assigns offsets and computes stack depths. Gives
  /// an Error for an argument or local index above 65534, a used label that
  /// is never placed, a label placed between a prefix and its instruction, a
  /// clause that ends before it begins, a clause listed after a clause that
  /// encloses it, a short branch that does not fit without relaxation, a
  /// stack underflow, a depth mismatch at a label, and a maximum depth above
  /// 65535. Nothing may be emitted afterwards.
  LLVM_ABI Error layout();

  /// The instructions with their final opcodes and offsets; valid after a
  /// successful layout().
  ArrayRef<ILInstr> instructions() const {
    assert(LaidOut && "instructions() before a successful layout()");
    return Instrs;
  }
  /// The targets of the switch \p I, one of instructions().
  ArrayRef<ILLabel> getSwitchTargets(const ILInstr &I) const {
    return ArrayRef<ILLabel>(SwitchTargets).slice(I.SwitchBegin, I.SwitchCount);
  }
  /// The offset of the placed label \p L; after layout().
  LLVM_ABI uint32_t getLabelOffset(ILLabel L) const;
  /// The size of the code in bytes; after layout().
  uint32_t getCodeSize() const {
    assert(LaidOut && "getCodeSize() before a successful layout()");
    return CodeSize;
  }
  /// The larger of the computed maximum stack depth and the floor; after
  /// layout().
  unsigned getMaxStack() const {
    assert(LaidOut && "getMaxStack() before a successful layout()");
    return std::max(ComputedMaxStack, MaxStackFloor);
  }

  /// Writes the code to \p Code and the clauses to \p Clauses (both are
  /// cleared first), resolving every token operand and catch type through
  /// \p Resolve; the first Error from \p Resolve is returned. After a
  /// successful layout().
  LLVM_ABI Error encode(SmallVectorImpl<uint8_t> &Code,
                        SmallVectorImpl<ExceptionClause> &Clauses,
                        TokenResolver Resolve) const;

private:
  /// The stack effect of an instruction, fixed when it is emitted.
  struct StackEffect {
    unsigned Pops = 0, Pushes = 0;
  };

  void addInstr(Opcode Op, StackEffect Effect);
  StackEffect getFixedStackEffect(Opcode Op) const;
  void emitIndex(std::optional<Opcode> Short0, Opcode ShortS, Opcode Long,
                 unsigned N, const char *What);
  void checkLabel(ILLabel L) const;
  void noteError(const Twine &Msg);
  Error checkLabels() const;
  Error checkClauses() const;
  Error assignOffsets();
  Error computeStackDepths();

  bool ReturnsValue;
  bool RelaxBranches = true;
  /// layout() was called: nothing may be emitted.
  bool Frozen = false;
  /// layout() succeeded.
  bool LaidOut = false;
  unsigned MaxStackFloor = 0;
  unsigned ComputedMaxStack = 0;
  uint32_t CodeSize = 0;
  SmallVector<ILInstr, 0> Instrs;
  /// Per instruction: the opcode as emitted and the stack effect.
  SmallVector<Opcode, 0> EmittedOps;
  SmallVector<StackEffect, 0> Effects;
  SmallVector<ILLabel, 0> SwitchTargets;
  /// Per label: the index of the instruction it precedes (Instrs.size() at
  /// the end), or UnplacedLabel.
  SmallVector<uint32_t, 0> LabelPositions;
  /// Per instruction index, and one past the end: the offset (after layout).
  SmallVector<uint32_t, 0> Offsets;
  SmallVector<ILExceptionClause, 0> Clauses;
  /// The first error found while emitting, reported by layout().
  std::string EmitError;

  static constexpr uint32_t UnplacedLabel = UINT32_MAX;
};

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_ILBUILDER_H
