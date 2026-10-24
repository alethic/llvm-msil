//===- ILBuilder.cpp - ECMA-335 CIL method builder ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The CIL assembler of synthesized method bodies: shortest operand forms,
// branch relaxation to a fixed point, the stack depth pass of ECMA-335
// III.1.7.5, and the encoding.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/ILBuilder.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <iterator>
#include <optional>

using namespace llvm;
using namespace llvm::cli;
using namespace llvm::support;

/// The largest argument or local index that the index emitters accept.
static constexpr unsigned MaxVarIndex = 65534;
/// The largest .maxstack that a fat header can store.
static constexpr unsigned MaxMaxStack = 0xFFFF;

static Error makeLayoutError(const Twine &Msg) {
  return createStringError(make_error_code(errc::invalid_argument),
                           "invalid IL body: " + Msg);
}

static std::string hex(uint64_t V) { return "0x" + utohexstr(V); }

static bool isPrefix(Opcode Op) {
  return getOpcodeInfo(Op).Flow == FlowControl::Meta;
}

static bool isBranch(Opcode Op) { return getShortBranchForm(Op).has_value(); }

static bool isShortBranch(Opcode Op) {
  return getOpcodeInfo(Op).Operand == OperandKind::ShortInlineBrTarget;
}

static bool isLeave(Opcode Op) {
  return Op == Opcode::Leave || Op == Opcode::Leave_s;
}

/// Whether control never falls through \p Op to the next instruction.
static bool isUnconditionalTransfer(Opcode Op) {
  switch (getOpcodeInfo(Op).Flow) {
  case FlowControl::Branch: // br, br.s, leave, leave.s
  case FlowControl::Return: // ret, endfinally, endfilter
  case FlowControl::Throw:  // throw, rethrow
    return true;
  default:
    return Op == Opcode::Jmp;
  }
}

[[maybe_unused]] static bool isTokenOperand(OperandKind K) {
  switch (K) {
  case OperandKind::InlineMethod:
  case OperandKind::InlineField:
  case OperandKind::InlineType:
  case OperandKind::InlineTok:
  case OperandKind::InlineSig:
  case OperandKind::InlineString:
    return true;
  default:
    return false;
  }
}

ILBuilder::ILBuilder(bool ReturnsValue) : ReturnsValue(ReturnsValue) {}

ILLabel ILBuilder::createLabel() {
  assert(!Frozen && "createLabel() after layout()");
  assert(LabelPositions.size() < UINT32_MAX && "too many labels");
  ILLabel L;
  L.Id = LabelPositions.size();
  LabelPositions.push_back(UnplacedLabel);
  return L;
}

void ILBuilder::checkLabel(ILLabel L) const {
  (void)L;
  assert(L.isValid() && L.Id < LabelPositions.size() &&
         "not a label of this ILBuilder");
}

void ILBuilder::placeLabel(ILLabel L) {
  assert(!Frozen && "placeLabel() after layout()");
  checkLabel(L);
  assert(LabelPositions[L.Id] == UnplacedLabel && "label placed twice");
  LabelPositions[L.Id] = Instrs.size();
}

void ILBuilder::noteError(const Twine &Msg) {
  if (EmitError.empty())
    EmitError = Msg.str();
}

ILBuilder::StackEffect ILBuilder::getFixedStackEffect(Opcode Op) const {
  const OpcodeInfo &Info = getOpcodeInfo(Op);
  int Pops = getStackPops(Info.Pop), Pushes = getStackPushes(Info.Push);
  assert(Pops >= 0 && Pushes >= 0 &&
         "use emitCall or emitRet for a variable stack effect");
  return {static_cast<unsigned>(Pops), static_cast<unsigned>(Pushes)};
}

void ILBuilder::addInstr(Opcode Op, StackEffect Effect) {
  assert(!Frozen && "emit after layout()");
  assert(Instrs.size() < UINT32_MAX - 1 && "too many instructions");
  ILInstr I;
  I.Op = Op;
  Instrs.push_back(I);
  EmittedOps.push_back(Op);
  Effects.push_back(Effect);
}

void ILBuilder::emit(Opcode Op) {
  assert(getOpcodeInfo(Op).Operand == OperandKind::InlineNone &&
         "emit() takes an opcode without an operand");
  assert(Op != Opcode::Ret && "use emitRet()");
  addInstr(Op, getFixedStackEffect(Op));
}

void ILBuilder::emitImm(Opcode Op, int64_t Value) {
  OperandKind K = getOpcodeInfo(Op).Operand;
  switch (K) {
  case OperandKind::ShortInlineI:
    assert((Op == Opcode::Ldc_i4_s ? isInt<8>(Value) : isUInt<8>(Value)) &&
           "operand does not fit in a byte");
    break;
  case OperandKind::InlineI:
    assert((isInt<32>(Value) || isUInt<32>(Value)) &&
           "operand does not fit in 32 bits");
    break;
  case OperandKind::InlineI8:
    break;
  case OperandKind::ShortInlineVar:
    assert(isUInt<8>(Value) && "index does not fit in a byte");
    break;
  case OperandKind::InlineVar:
    assert(isUInt<16>(Value) && "index does not fit in 16 bits");
    break;
  default:
    llvm_unreachable("emitImm() takes an integer or index operand");
  }
  addInstr(Op, getFixedStackEffect(Op));
  Instrs.back().Imm = Value;
}

void ILBuilder::emitR4(uint32_t Bits) {
  addInstr(Opcode::Ldc_r4, getFixedStackEffect(Opcode::Ldc_r4));
  Instrs.back().FPBits = Bits;
}

void ILBuilder::emitR8(uint64_t Bits) {
  addInstr(Opcode::Ldc_r8, getFixedStackEffect(Opcode::Ldc_r8));
  Instrs.back().FPBits = Bits;
}

void ILBuilder::emitToken(Opcode Op, TokenRef T) {
  assert(isTokenOperand(getOpcodeInfo(Op).Operand) &&
         "emitToken() takes an opcode with a token operand");
  addInstr(Op, getFixedStackEffect(Op));
  Instrs.back().Token = T;
}

void ILBuilder::emitCall(Opcode Op, TokenRef Callee, unsigned Pops,
                         unsigned Pushes) {
  // call, callvirt, newobj, calli and jmp; also ldftn and ldvirtftn, whose
  // counts the caller passes like any other.
  OperandKind K = getOpcodeInfo(Op).Operand;
  assert((K == OperandKind::InlineMethod || K == OperandKind::InlineSig) &&
         "emitCall() takes an opcode with a method or signature operand");
  (void)K;
  assert((Op != Opcode::Newobj || Pushes == 1) && "newobj pushes the object");
  assert((Op != Opcode::Jmp || (Pops == 0 && Pushes == 0)) &&
         "jmp neither pops nor pushes");
  addInstr(Op, {Pops, Pushes});
  Instrs.back().Token = Callee;
}

void ILBuilder::emitRet() {
  addInstr(Opcode::Ret, {ReturnsValue ? 1u : 0u, 0u});
}

void ILBuilder::emitBranch(Opcode Op, ILLabel Target) {
  assert(isBranch(Op) && "emitBranch() takes br, b<cc>, brtrue, brfalse or "
                         "leave");
  checkLabel(Target);
  addInstr(Op, getFixedStackEffect(Op));
  Instrs.back().Target = Target;
}

void ILBuilder::emitSwitch(ArrayRef<ILLabel> Targets) {
  assert(SwitchTargets.size() + Targets.size() <= UINT32_MAX &&
         "too many switch targets");
  for (ILLabel L : Targets)
    checkLabel(L);
  addInstr(Opcode::Switch, getFixedStackEffect(Opcode::Switch));
  Instrs.back().SwitchBegin = SwitchTargets.size();
  Instrs.back().SwitchCount = Targets.size();
  SwitchTargets.append(Targets.begin(), Targets.end());
}

void ILBuilder::emitLdcI4(int32_t V) {
  if (V >= -1 && V <= 8) {
    emit(static_cast<Opcode>(static_cast<uint16_t>(Opcode::Ldc_i4_0) + V));
    return;
  }
  if (isInt<8>(V)) {
    emitImm(Opcode::Ldc_i4_s, V);
    return;
  }
  emitImm(Opcode::Ldc_i4, V);
}

/// \p Short0 is the opcode of index 0 of the .0-.3 forms, if the instruction
/// has them.
void ILBuilder::emitIndex(std::optional<Opcode> Short0, Opcode ShortS,
                          Opcode Long, unsigned N, const char *What) {
  if (N > MaxVarIndex) {
    noteError(Twine(What) + " index " + Twine(N) + " is above " +
              Twine(MaxVarIndex));
    // Keep the instruction list complete; layout() fails anyway.
    addInstr(Long, getFixedStackEffect(Long));
    return;
  }
  if (Short0 && N <= 3) {
    emit(static_cast<Opcode>(static_cast<uint16_t>(*Short0) + N));
    return;
  }
  if (isUInt<8>(N)) {
    emitImm(ShortS, N);
    return;
  }
  emitImm(Long, N);
}

void ILBuilder::emitLdarg(unsigned N) {
  emitIndex(Opcode::Ldarg_0, Opcode::Ldarg_s, Opcode::Ldarg, N, "argument");
}

void ILBuilder::emitLdarga(unsigned N) {
  emitIndex(std::nullopt, Opcode::Ldarga_s, Opcode::Ldarga, N, "argument");
}

void ILBuilder::emitStarg(unsigned N) {
  emitIndex(std::nullopt, Opcode::Starg_s, Opcode::Starg, N, "argument");
}

void ILBuilder::emitLdloc(unsigned N) {
  emitIndex(Opcode::Ldloc_0, Opcode::Ldloc_s, Opcode::Ldloc, N, "local");
}

void ILBuilder::emitLdloca(unsigned N) {
  emitIndex(std::nullopt, Opcode::Ldloca_s, Opcode::Ldloca, N, "local");
}

void ILBuilder::emitStloc(unsigned N) {
  emitIndex(Opcode::Stloc_0, Opcode::Stloc_s, Opcode::Stloc, N, "local");
}

void ILBuilder::addExceptionClause(const ILExceptionClause &C) {
  assert(!Frozen && "addExceptionClause() after layout()");
  checkLabel(C.TryBegin);
  checkLabel(C.TryEnd);
  checkLabel(C.HandlerBegin);
  checkLabel(C.HandlerEnd);
  if (C.Kind == ExceptionClauseKind::Filter)
    checkLabel(C.FilterBegin);
  Clauses.push_back(C);
}

void ILBuilder::setMaxStack(unsigned N) {
  assert(N <= MaxMaxStack && ".maxstack is a 16-bit field");
  MaxStackFloor = N;
}

//===----------------------------------------------------------------------===//
// Layout
//===----------------------------------------------------------------------===//

Error ILBuilder::checkLabels() const {
  auto IsPlaced = [&](ILLabel L) {
    return LabelPositions[L.Id] != UnplacedLabel;
  };
  auto Unplaced = [&](ILLabel L, const Twine &Use) {
    return makeLayoutError("label " + Twine(L.Id) + ", the " + Use +
                           ", is never placed");
  };
  for (size_t I = 0, E = Instrs.size(); I != E; ++I) {
    const ILInstr &In = Instrs[I];
    // A Twine must not outlive its operands: build the string.
    auto Target = [&]() -> std::string {
      return ("target of instruction " + Twine(I) + " ('" +
              getOpcodeInfo(In.Op).Name + "')")
          .str();
    };
    if (In.Target.isValid() && !IsPlaced(In.Target))
      return Unplaced(In.Target, Target());
    for (ILLabel L : getSwitchTargets(In))
      if (!IsPlaced(L))
        return Unplaced(L, Target());
  }
  for (size_t I = 0, E = Clauses.size(); I != E; ++I) {
    const ILExceptionClause &C = Clauses[I];
    for (auto [L, What] :
         {std::pair(C.TryBegin, "try begin"), std::pair(C.TryEnd, "try end"),
          std::pair(C.HandlerBegin, "handler begin"),
          std::pair(C.HandlerEnd, "handler end")})
      if (!IsPlaced(L))
        return Unplaced(L, Twine(What) + " of clause " + Twine(I));
    if (C.Kind == ExceptionClauseKind::Filter && !IsPlaced(C.FilterBegin))
      return Unplaced(C.FilterBegin, "filter begin of clause " + Twine(I));
  }
  // A prefix and the instruction it modifies are one unit: nothing may
  // branch between them.
  for (size_t L = 0, E = LabelPositions.size(); L != E; ++L) {
    uint32_t Pos = LabelPositions[L];
    if (Pos == UnplacedLabel || Pos == 0 || !isPrefix(Instrs[Pos - 1].Op))
      continue;
    return makeLayoutError(
        "label " + Twine(L) + " is placed between the prefix '" +
        getOpcodeInfo(Instrs[Pos - 1].Op).Name + "' (instruction " +
        Twine(Pos - 1) + ") and its instruction");
  }
  return Error::success();
}

namespace {
/// A half-open range of code offsets.
struct CodeRange {
  uint32_t Begin, End;
  bool operator==(const CodeRange &O) const {
    return Begin == O.Begin && End == O.End;
  }
  /// Whether \p Inner lies within this range (an empty range only when it is
  /// strictly inside).
  bool contains(const CodeRange &Inner) const {
    return Begin <= Inner.Begin && Inner.End <= End && Inner.Begin < End;
  }
};
} // namespace

Error ILBuilder::checkClauses() const {
  auto Range = [&](ILLabel B, ILLabel E) {
    return CodeRange{Offsets[LabelPositions[B.Id]],
                     Offsets[LabelPositions[E.Id]]};
  };
  SmallVector<CodeRange, 0> Try, Handler, Filter;
  for (size_t I = 0, E = Clauses.size(); I != E; ++I) {
    const ILExceptionClause &C = Clauses[I];
    Try.push_back(Range(C.TryBegin, C.TryEnd));
    Handler.push_back(Range(C.HandlerBegin, C.HandlerEnd));
    bool IsFilter = C.Kind == ExceptionClauseKind::Filter;
    Filter.push_back(IsFilter ? Range(C.FilterBegin, C.HandlerBegin)
                              : CodeRange{0, 0});
    for (auto [R, What] : {std::pair(Try.back(), "try block"),
                           std::pair(Handler.back(), "handler"),
                           std::pair(Filter.back(), "filter block")})
      if (R.End < R.Begin)
        return makeLayoutError("the " + Twine(What) + " of clause " + Twine(I) +
                               " ends at offset " + hex(R.End) +
                               ", before it begins at offset " + hex(R.Begin));
  }
  // II.19: a clause nested in another one comes first. Clause J is nested in
  // clause I when its try block lies in I's try block (and is not the same
  // block: clauses of one try block are listed in handler order), in I's
  // handler or in I's filter block.
  for (size_t I = 0, E = Clauses.size(); I != E; ++I) {
    bool IsFilter = Clauses[I].Kind == ExceptionClauseKind::Filter;
    for (size_t J = I + 1; J != E; ++J) {
      bool Encloses = (Try[I].contains(Try[J]) && !(Try[I] == Try[J])) ||
                      Handler[I].contains(Try[J]) ||
                      (IsFilter && Filter[I].contains(Try[J]));
      if (Encloses)
        return makeLayoutError("clause " + Twine(J) +
                               " is listed after clause " + Twine(I) +
                               ", which encloses it (II.19: inner clauses "
                               "come first)");
    }
  }
  return Error::success();
}

Error ILBuilder::assignOffsets() {
  size_t N = Instrs.size();
  // Branch forms: with relaxation every branch starts short.
  SmallVector<bool, 0> Short(N, false);
  for (size_t I = 0; I != N; ++I) {
    Opcode Op = EmittedOps[I];
    if (isBranch(Op))
      Short[I] = RelaxBranches || isShortBranch(Op);
  }

  auto SizeOf = [&](size_t I) -> uint64_t {
    Opcode Op = EmittedOps[I];
    if (isBranch(Op))
      return Short[I] ? 2 : 5; // Every branch opcode is one byte.
    const OpcodeInfo &Info = getOpcodeInfo(Op);
    uint64_t Size = getOpcodeSize(Op) + getOperandSize(Info.Operand);
    if (Info.Operand == OperandKind::InlineSwitch)
      Size += uint64_t(4) * Instrs[I].SwitchCount;
    return Size;
  };

  Offsets.assign(N + 1, 0);
  // Widening a branch only grows the code, so every displacement keeps or
  // grows its magnitude: the loop ends, and widening every branch that does
  // not fit at once reaches the same fixed point as one at a time.
  for (;;) {
    uint64_t Pos = 0;
    for (size_t I = 0; I != N; ++I) {
      Offsets[I] = Pos;
      Pos += SizeOf(I);
      if (Pos > UINT32_MAX)
        return makeLayoutError("the code is larger than 4 GiB");
    }
    Offsets[N] = Pos;

    bool Changed = false;
    for (size_t I = 0; I != N; ++I) {
      if (!isBranch(EmittedOps[I]) || !Short[I])
        continue;
      int64_t Displacement =
          int64_t(Offsets[LabelPositions[Instrs[I].Target.Id]]) -
          int64_t(Offsets[I + 1]);
      if (isInt<8>(Displacement))
        continue;
      if (!RelaxBranches)
        return makeLayoutError("the displacement " + Twine(Displacement) +
                               " of '" + getOpcodeInfo(EmittedOps[I]).Name +
                               "' at offset " + hex(Offsets[I]) +
                               " does not fit in its short form");
      Short[I] = false;
      Changed = true;
    }
    if (!Changed)
      break;
  }

  for (size_t I = 0; I != N; ++I) {
    ILInstr &In = Instrs[I];
    In.Offset = Offsets[I];
    In.Op = EmittedOps[I];
    if (isBranch(In.Op))
      In.Op = Short[I] ? *getShortBranchForm(In.Op) : *getLongBranchForm(In.Op);
  }
  CodeSize = Offsets[N];
  return Error::success();
}

Error ILBuilder::computeStackDepths() {
  size_t N = Instrs.size();
  constexpr uint32_t Unknown = UINT32_MAX;
  // The depth recorded for each instruction index (N: the end of the code).
  SmallVector<uint32_t, 0> Depth(N + 1, Unknown);

  // Records depth D at Pos; false if Pos already has another depth.
  auto Record = [&](uint32_t Pos, uint32_t D) {
    if (Depth[Pos] == Unknown)
      Depth[Pos] = D;
    return Depth[Pos] == D;
  };
  auto Mismatch = [&](uint32_t Pos, uint32_t D, const Twine &Where) {
    return makeLayoutError("stack depth " + Twine(D) + " " + Where +
                           " does not match the depth " + Twine(Depth[Pos]) +
                           " already recorded at offset " + hex(Offsets[Pos]));
  };

  // Handler and filter entry depths.
  for (size_t I = 0, E = Clauses.size(); I != E; ++I) {
    const ILExceptionClause &C = Clauses[I];
    bool PushesException = C.Kind == ExceptionClauseKind::Catch ||
                           C.Kind == ExceptionClauseKind::Filter;
    auto Preset = [&](ILLabel L, uint32_t D, const char *What) -> Error {
      uint32_t Pos = LabelPositions[L.Id];
      if (Record(Pos, D))
        return Error::success();
      return Mismatch(Pos, D,
                      "at the start of the " + Twine(What) + " of clause " +
                          Twine(I));
    };
    if (Error Err = Preset(C.TryBegin, 0, "try block"))
      return Err;
    if (Error Err = Preset(C.HandlerBegin, PushesException ? 1 : 0, "handler"))
      return Err;
    if (C.Kind == ExceptionClauseKind::Filter)
      if (Error Err = Preset(C.FilterBegin, 1, "filter block"))
        return Err;
  }

  uint32_t Cur = 0;
  uint32_t Max = 0;
  bool Reachable = true;
  for (size_t I = 0; I != N; ++I) {
    const ILInstr &In = Instrs[I];
    if (Reachable) {
      if (!Record(I, Cur))
        return Mismatch(I, Cur, "at offset " + hex(Offsets[I]));
    } else {
      // III.1.7.5: after an unconditional transfer, the depth a forward
      // branch recorded, else 0.
      if (Depth[I] == Unknown)
        Depth[I] = 0;
      Cur = Depth[I];
    }
    Max = std::max(Max, Cur);

    const OpcodeInfo &Info = getOpcodeInfo(In.Op);
    StackEffect Effect = Effects[I];
    if (Effect.Pops > Cur)
      return makeLayoutError(
          "stack underflow at offset " + hex(Offsets[I]) + ": '" + Info.Name +
          "' pops " + Twine(Effect.Pops) + " with a depth of " + Twine(Cur));
    Cur -= Effect.Pops;

    auto Branch = [&](ILLabel L, uint32_t D) -> Error {
      uint32_t Pos = LabelPositions[L.Id];
      if (Record(Pos, D))
        return Error::success();
      return Mismatch(Pos, D,
                      "at the target of '" + Twine(Info.Name) + "' at offset " +
                          hex(Offsets[I]));
    };
    if (In.Target.isValid())
      // leave empties the evaluation stack.
      if (Error Err = Branch(In.Target, isLeave(In.Op) ? 0 : Cur))
        return Err;
    for (ILLabel L : getSwitchTargets(In))
      if (Error Err = Branch(L, Cur))
        return Err;

    if (uint64_t(Cur) + Effect.Pushes > MaxMaxStack)
      return makeLayoutError("the stack depth exceeds " + Twine(MaxMaxStack) +
                             " at offset " + hex(Offsets[I]));
    Cur += Effect.Pushes;
    Max = std::max(Max, Cur);
    Reachable = !isUnconditionalTransfer(In.Op);
  }
  ComputedMaxStack = Max;
  return Error::success();
}

Error ILBuilder::layout() {
  Frozen = true;
  LaidOut = false;
  if (!EmitError.empty())
    return makeLayoutError(EmitError);
  if (Error Err = checkLabels())
    return Err;
  if (Error Err = assignOffsets())
    return Err;
  if (Error Err = checkClauses())
    return Err;
  if (Error Err = computeStackDepths())
    return Err;
  LaidOut = true;
  return Error::success();
}

uint32_t ILBuilder::getLabelOffset(ILLabel L) const {
  assert(LaidOut && "getLabelOffset() before a successful layout()");
  checkLabel(L);
  assert(LabelPositions[L.Id] != UnplacedLabel && "label never placed");
  return Offsets[LabelPositions[L.Id]];
}

//===----------------------------------------------------------------------===//
// Encoding
//===----------------------------------------------------------------------===//

template <typename T> static void append(SmallVectorImpl<uint8_t> &Out, T V) {
  uint8_t B[sizeof(T)];
  endian::write<T, llvm::endianness::little>(B, V);
  Out.append(std::begin(B), std::end(B));
}

Error ILBuilder::encode(SmallVectorImpl<uint8_t> &Code,
                        SmallVectorImpl<ExceptionClause> &OutClauses,
                        TokenResolver Resolve) const {
  assert(LaidOut && "encode() before a successful layout()");
  Code.clear();
  OutClauses.clear();
  Code.reserve(CodeSize);
  for (size_t I = 0, E = Instrs.size(); I != E; ++I) {
    const ILInstr &In = Instrs[I];
    assert(Code.size() == In.Offset && "layout and encoding disagree");
    uint16_t Value = static_cast<uint16_t>(In.Op);
    if (getOpcodeSize(In.Op) == 2)
      Code.push_back(TwoByteOpcodePrefix);
    Code.push_back(Value & 0xFF);

    const OpcodeInfo &Info = getOpcodeInfo(In.Op);
    switch (Info.Operand) {
    case OperandKind::InlineNone:
      break;
    case OperandKind::ShortInlineVar:
    case OperandKind::ShortInlineI:
      Code.push_back(static_cast<uint8_t>(In.Imm));
      break;
    case OperandKind::InlineVar:
      append<uint16_t>(Code, static_cast<uint16_t>(In.Imm));
      break;
    case OperandKind::InlineI:
      append<uint32_t>(Code, static_cast<uint32_t>(In.Imm));
      break;
    case OperandKind::InlineI8:
      append<uint64_t>(Code, static_cast<uint64_t>(In.Imm));
      break;
    case OperandKind::ShortInlineR:
      append<uint32_t>(Code, static_cast<uint32_t>(In.FPBits));
      break;
    case OperandKind::InlineR:
      append<uint64_t>(Code, In.FPBits);
      break;
    case OperandKind::ShortInlineBrTarget:
    case OperandKind::InlineBrTarget: {
      int64_t Displacement =
          int64_t(getLabelOffset(In.Target)) - int64_t(Offsets[I + 1]);
      if (Info.Operand == OperandKind::ShortInlineBrTarget) {
        assert(isInt<8>(Displacement) && "short branch out of range");
        Code.push_back(static_cast<uint8_t>(Displacement));
      } else {
        append<uint32_t>(Code, static_cast<uint32_t>(Displacement));
      }
      break;
    }
    case OperandKind::InlineSwitch: {
      ArrayRef<ILLabel> Targets = getSwitchTargets(In);
      append<uint32_t>(Code, Targets.size());
      for (ILLabel L : Targets)
        append<uint32_t>(Code,
                         static_cast<uint32_t>(int64_t(getLabelOffset(L)) -
                                               int64_t(Offsets[I + 1])));
      break;
    }
    case OperandKind::InlineMethod:
    case OperandKind::InlineField:
    case OperandKind::InlineType:
    case OperandKind::InlineTok:
    case OperandKind::InlineSig:
    case OperandKind::InlineString: {
      Expected<uint32_t> Token = Resolve(In.Token);
      if (!Token) {
        Code.clear();
        return Token.takeError();
      }
      append<uint32_t>(Code, *Token);
      break;
    }
    }
  }
  assert(Code.size() == CodeSize && "layout and encoding disagree");

  for (const ILExceptionClause &C : Clauses) {
    ExceptionClause Out;
    Out.Kind = C.Kind;
    Out.TryOffset = getLabelOffset(C.TryBegin);
    Out.TryLength = getLabelOffset(C.TryEnd) - Out.TryOffset;
    Out.HandlerOffset = getLabelOffset(C.HandlerBegin);
    Out.HandlerLength = getLabelOffset(C.HandlerEnd) - Out.HandlerOffset;
    if (C.Kind == ExceptionClauseKind::Catch) {
      Expected<uint32_t> Token = Resolve(C.CatchType);
      if (!Token) {
        Code.clear();
        OutClauses.clear();
        return Token.takeError();
      }
      Out.ClassTokenOrFilterOffset = *Token;
    } else if (C.Kind == ExceptionClauseKind::Filter) {
      Out.ClassTokenOrFilterOffset = getLabelOffset(C.FilterBegin);
    }
    OutClauses.push_back(Out);
  }
  return Error::success();
}
