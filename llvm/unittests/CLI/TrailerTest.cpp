//===- TrailerTest.cpp - module initializer and entry point builder -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/Trailer.h (ImplementationPlan.md 5.9.10, M20), the trailer
// builder that whole-program output and msil-ld share, through a recording
// TrailerSink:
// - up to the relocation-loop threshold, '<Module>::.cctor' is Phase 1's
//   sequence instruction for instruction: the .cctor and '<msil>Main' that
//   HEAD's llc printed for llvm/test/CodeGen/MSIL/module-initializer.ll and
//   global-relocs.ll (both of its modules) are rebuilt from the same inputs,
//   with the 'unaligned.' prefixes of D24 only where an address is not
//   naturally aligned, 4-byte stores, the same .maxstack, the image-only and
//   zero-only shapes, constructors that return a value, and both D27 shapes;
// - above it, the '<msil>relocs' table of 16-byte entries {u32 Site; u32
//   Kind; i64 Value} (kinds 0-3) in site order and one 8-byte address slot
//   per distinct (field, addend) pair of the relocations without an image
//   target (countAddressSlots), with one ldftn per function;
// - native data imports: '<msil>dataimport'(string, string) : native int
//   shaped as recon native-imports-files/data-win.il (Kernel32) or with
//   dlopen(path, 1) and dlsym (Dlfcn), called from '<msil>init_dataimports'
//   before the relocations, never from .cctor itself, and
//   EntryPointNotFoundException("data import '<name>' not found in <module>")
//   for a missing symbol.
// A small CIL interpreter runs the recorded methods on a simulated heap: it
// checks every store against the alignment it is allowed to assume (the
// natural one, or that of an 'unaligned.' prefix), that the data block is
// aligned to MaxAlign, that the zero part is cleared, and that every site,
// address field and imported address holds the value the inputs describe,
// whichever instruction sequence the loop uses.
// Expected sequences come from HEAD's llc output (normalized: every ldc.i4
// form as 'ldc.i4 <n>', short branches as long ones, labels L0, L1, ... in
// code order), expected values from the plan; never from the implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/Trailer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/ImageLayout.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/CLI/Signature.h"
#include "llvm/Support/MathExtras.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

using ByteVec = std::vector<uint8_t>;
using Lines = std::vector<std::string>;

TypeSig tI4() { return TypeSig::get(ELEMENT_TYPE_I4); }
TypeSig tNI() { return TypeSig::get(ELEMENT_TYPE_I); }
TypeSig tStr() { return TypeSig::get(ELEMENT_TYPE_STRING); }

MethodSig sig(TypeSig Ret, std::vector<TypeSig> Params) {
  MethodSig S;
  S.Return = Ret;
  S.Params = std::move(Params);
  return S;
}

bool isVoid(const TypeSig &T) {
  return T.getElementType() == ELEMENT_TYPE_VOID;
}

//===----------------------------------------------------------------------===//
// The recording sink
//===----------------------------------------------------------------------===//

class RecordingSink final : public TrailerSink {
public:
  enum class Kind {
    DataField,
    RVAField,
    BCL,
    Helper,
    UserString,
    Field,
    Method,
    Native
  };
  struct Entity {
    Kind K = Kind::Field;
    std::string Name; // the text of a user string
    std::string TypeName;
    ByteVec Bytes;
    BCLMemberId Bcl = BCLMemberId::Marshal;
    MethodSig Sig;
  };
  struct Instr {
    Opcode Op = Opcode::Nop;
    uint32_t Offset = 0;
    int64_t Imm = 0;
    uint64_t FPBits = 0;
    TokenRef Token;
    std::optional<uint32_t> Target;
    std::vector<uint32_t> SwitchTargets;
  };
  struct Method {
    std::string Name;
    MethodAttributes Flags = MethodAttributes::PrivateScope;
    MethodSig Sig;
    std::vector<TypeSig> Locals;
    unsigned MaxStack = 0;
    uint32_t CodeSize = 0;
    std::vector<Instr> Code;
  };

  // Entities the test defines (what the AsmPrinter or msil-ld would own).
  EntityRef field(StringRef Name) { return add(Kind::Field, Name); }
  EntityRef method(StringRef Name, MethodSig Sig) {
    EntityRef R = add(Kind::Method, Name);
    Entities[R - 1].Sig = std::move(Sig);
    return R;
  }
  EntityRef native(StringRef Name, MethodSig Sig) {
    EntityRef R = add(Kind::Native, Name);
    Entities[R - 1].Sig = std::move(Sig);
    return R;
  }

  EntityRef getDataField() override {
    if (!DataField)
      DataField = add(Kind::DataField, "<msil>data");
    return DataField;
  }
  EntityRef addRVAData(StringRef TypeName, StringRef FieldName,
                       ArrayRef<uint8_t> Bytes) override {
    EntityRef R = add(Kind::RVAField, FieldName);
    Entities[R - 1].TypeName = TypeName.str();
    Entities[R - 1].Bytes.assign(Bytes.begin(), Bytes.end());
    return R;
  }
  EntityRef getBCLMember(BCLMemberId Id) override {
    auto It = BCLRefs.find(static_cast<unsigned>(Id));
    if (It != BCLRefs.end())
      return It->second;
    EntityRef R = add(Kind::BCL, getBCLMemberInfo(Id).IdName);
    Entities[R - 1].Bcl = Id;
    BCLRefs[static_cast<unsigned>(Id)] = R;
    return R;
  }
  EntityRef declareHelper(StringRef Name, const MethodSig &Sig) override {
    EntityRef R = add(Kind::Helper, Name);
    Entities[R - 1].Sig = Sig;
    return R;
  }
  EntityRef addUserString(StringRef UTF8) override {
    return add(Kind::UserString, UTF8);
  }
  void emitMethod(const TrailerMethod &M) override {
    Method R;
    R.Name = std::string(M.Name);
    R.Flags = M.Flags;
    R.Sig = M.Sig;
    R.Locals.assign(M.Locals.begin(), M.Locals.end());
    R.MaxStack = M.Body.getMaxStack();
    R.CodeSize = M.Body.getCodeSize();
    for (const ILInstr &I : M.Body.instructions()) {
      Instr X;
      X.Op = I.Op;
      X.Offset = I.Offset;
      X.Imm = I.Imm;
      X.FPBits = I.FPBits;
      X.Token = I.Token;
      if (I.Target.isValid())
        X.Target = M.Body.getLabelOffset(I.Target);
      if (I.Op == Opcode::Switch)
        for (ILLabel L : M.Body.getSwitchTargets(I))
          X.SwitchTargets.push_back(M.Body.getLabelOffset(L));
      R.Code.push_back(std::move(X));
    }
    Methods.push_back(std::move(R));
  }

  const Entity *entity(TokenRef T) const {
    if (T.getKind() != TokenRef::Kind::External || T.getValue() == 0 ||
        T.getValue() > Entities.size())
      return nullptr;
    return &Entities[T.getValue() - 1];
  }
  const Entity &get(EntityRef R) const { return Entities.at(R - 1); }

  const Method *findMethod(StringRef Name) const {
    const Method *Found = nullptr;
    for (const Method &M : Methods)
      if (M.Name == Name) {
        EXPECT_EQ(Found, nullptr) << "two methods named " << Name.str();
        Found = &M;
      }
    return Found;
  }
  std::vector<std::string> methodNames() const {
    std::vector<std::string> R;
    for (const Method &M : Methods)
      R.push_back(M.Name);
    return R;
  }
  const Entity *findRVA(StringRef FieldName) const {
    const Entity *Found = nullptr;
    for (const Entity &E : Entities)
      if (E.K == Kind::RVAField && E.Name == FieldName) {
        EXPECT_EQ(Found, nullptr) << "two RVA fields named " << FieldName.str();
        Found = &E;
      }
    return Found;
  }
  bool hasEntity(Kind K) const {
    return any_of(Entities, [&](const Entity &E) { return E.K == K; });
  }

  // The method as normalized text lines.
  Lines render(const Method &M) const {
    std::set<uint32_t> Targets;
    for (const Instr &I : M.Code) {
      if (I.Target)
        Targets.insert(*I.Target);
      Targets.insert(I.SwitchTargets.begin(), I.SwitchTargets.end());
    }
    std::map<uint32_t, unsigned> Labels;
    for (uint32_t T : Targets)
      Labels.emplace(T, Labels.size());
    Lines Out;
    for (const Instr &I : M.Code) {
      auto It = Labels.find(I.Offset);
      if (It != Labels.end())
        Out.push_back("L" + std::to_string(It->second) + ":");
      Out.push_back(renderInstr(I, Labels));
    }
    auto End = Labels.find(M.CodeSize);
    if (End != Labels.end())
      Out.push_back("L" + std::to_string(End->second) + ":");
    return Out;
  }
  Lines render(StringRef MethodName) const {
    const Method *M = findMethod(MethodName);
    if (!M) {
      ADD_FAILURE() << "no method " << MethodName.str();
      return {};
    }
    return render(*M);
  }

  std::vector<Entity> Entities;
  std::vector<Method> Methods;
  EntityRef DataField = 0;

private:
  EntityRef add(Kind K, StringRef Name) {
    Entity E;
    E.K = K;
    E.Name = Name.str();
    Entities.push_back(std::move(E));
    return Entities.size();
  }

  std::string entityText(TokenRef T) const {
    const Entity *E = entity(T);
    if (!E)
      return "<bad token>";
    if (E->K == Kind::UserString)
      return "\"" + E->Name + "\"";
    return E->Name;
  }

  std::string renderInstr(const Instr &I,
                          const std::map<uint32_t, unsigned> &Labels) const {
    const OpcodeInfo &Info = getOpcodeInfo(I.Op);
    StringRef Name = Info.Name;
    auto Label = [&](uint32_t Off) {
      auto It = Labels.find(Off);
      return It == Labels.end() ? std::string("L?")
                                : "L" + std::to_string(It->second);
    };
    if (Name.starts_with("ldc.i4")) {
      if (Info.Operand == OperandKind::InlineNone) {
        StringRef Suffix = Name.drop_front(7);
        int V = Suffix == "m1" ? -1 : Suffix[0] - '0';
        return "ldc.i4 " + std::to_string(V);
      }
      return "ldc.i4 " + std::to_string(I.Imm);
    }
    auto [Head, Tail] = Name.split('.');
    if (Head == "ldarg" || Head == "ldloc" || Head == "stloc" ||
        Head == "starg" || Head == "ldarga" || Head == "ldloca") {
      if (Tail.size() == 1 && isDigit(Tail[0]))
        return (Head + " " + Tail).str();
      return (Head + " " + Twine(I.Imm)).str();
    }
    switch (Info.Operand) {
    case OperandKind::InlineNone:
      return Name.str();
    case OperandKind::ShortInlineBrTarget:
    case OperandKind::InlineBrTarget: {
      StringRef Base = Name;
      Base.consume_back(".s");
      return Base.str() + " " + Label(I.Target ? *I.Target : ~0u);
    }
    case OperandKind::InlineSwitch: {
      std::string S = "switch (";
      for (size_t K = 0; K != I.SwitchTargets.size(); ++K)
        S += (K ? ", " : "") + Label(I.SwitchTargets[K]);
      return S + ")";
    }
    case OperandKind::ShortInlineI:
    case OperandKind::InlineI:
    case OperandKind::InlineI8:
    case OperandKind::ShortInlineVar:
    case OperandKind::InlineVar:
      return Name.str() + " " + std::to_string(I.Imm);
    case OperandKind::ShortInlineR:
      return Name.str() + " 0x" + utohexstr(I.FPBits & 0xFFFFFFFF);
    case OperandKind::InlineR:
      return Name.str() + " 0x" + utohexstr(I.FPBits);
    default:
      return Name.str() + " " + entityText(I.Token);
    }
  }

  std::map<unsigned, EntityRef> BCLRefs;
};

using Kind = RecordingSink::Kind;

// A sequence written as instructions separated by ';' or line breaks.
Lines lines(StringRef Text) {
  Lines R;
  SmallVector<StringRef, 64> Parts;
  Text.split(Parts, ';');
  for (StringRef Part : Parts) {
    SmallVector<StringRef, 4> Sub;
    Part.split(Sub, '\n');
    for (StringRef L : Sub)
      if (!L.trim().empty())
        R.push_back(L.trim().str());
  }
  return R;
}

//===----------------------------------------------------------------------===//
// A small CIL interpreter over recorded methods
//===----------------------------------------------------------------------===//

class Memory {
public:
  enum class Enc { Plain, Ansi, Uni };
  struct Block {
    uint64_t Base;
    ByteVec Bytes;
    bool ReadOnly;
    Enc E;
  };

  uint64_t alloc(uint64_t Size, uint8_t Fill, unsigned Misalign,
                 bool ReadOnly = false, Enc E = Enc::Plain) {
    Next = alignTo(Next, 0x10000);
    uint64_t Base = Next + Misalign;
    Blocks.push_back({Base, ByteVec(Size, Fill), ReadOnly, E});
    Next = Base + Size + 0x10000;
    return Base;
  }
  Block *find(uint64_t Addr, uint64_t Size) {
    for (Block &B : Blocks)
      if (Addr >= B.Base && Addr + Size <= B.Base + B.Bytes.size() &&
          Addr + Size >= Addr)
        return &B;
    return nullptr;
  }
  bool read(uint64_t Addr, unsigned Size, uint64_t &Out) {
    Block *B = find(Addr, Size);
    if (!B)
      return false;
    Out = 0;
    for (unsigned I = 0; I != Size; ++I)
      Out |= uint64_t(B->Bytes[Addr - B->Base + I]) << (8 * I);
    return true;
  }
  // False if the range is outside every block or read-only.
  bool write(uint64_t Addr, unsigned Size, uint64_t V) {
    Block *B = find(Addr, Size);
    if (!B || B->ReadOnly)
      return false;
    for (unsigned I = 0; I != Size; ++I)
      B->Bytes[Addr - B->Base + I] = uint8_t(V >> (8 * I));
    return true;
  }
  // The string at the start of a block of encoding E.
  std::optional<std::string> readString(uint64_t Addr, Enc E) {
    Block *B = find(Addr, 1);
    if (!B || B->Base != Addr || B->E != E)
      return std::nullopt;
    std::string S;
    if (E == Enc::Uni) {
      for (size_t I = 0; I + 1 < B->Bytes.size(); I += 2) {
        unsigned C = B->Bytes[I] | (B->Bytes[I + 1] << 8);
        if (!C)
          return S;
        if (C > 0x7F)
          return std::nullopt; // the tests use ASCII names
        S.push_back(char(C));
      }
      return std::nullopt;
    }
    for (uint8_t C : B->Bytes) {
      if (!C)
        return S;
      S.push_back(char(C));
    }
    return std::nullopt;
  }

private:
  uint64_t Next = 0x10000000;
  std::deque<Block> Blocks;
};

struct Value {
  enum class T { None, I4, I8, NI, Str, StrArr, Exc };
  T Ty = T::None;
  int64_t I = 0; // I4 values are kept sign-extended
  std::string S;
  std::vector<std::string> Arr;

  static Value i4(int64_t V) {
    Value X;
    X.Ty = T::I4;
    X.I = int32_t(uint32_t(V));
    return X;
  }
  static Value ni(uint64_t V) {
    Value X;
    X.Ty = T::NI;
    X.I = int64_t(V);
    return X;
  }
  static Value i8(int64_t V) {
    Value X;
    X.Ty = T::I8;
    X.I = V;
    return X;
  }
  static Value str(std::string S) {
    Value X;
    X.Ty = T::Str;
    X.S = std::move(S);
    return X;
  }
  bool isInt() const { return Ty == T::I4 || Ty == T::I8 || Ty == T::NI; }
};

class Interp {
public:
  using Hook =
      std::function<Value(const RecordingSink::Entity &, std::vector<Value> &)>;
  struct Outcome {
    bool Threw = false;
    std::string Message;
    std::optional<Value> Ret;
  };

  explicit Interp(const RecordingSink &Sink) : Sink(Sink) {}

  static uint64_t fnPtr(EntityRef R) {
    return 0x7FF000000000ull + uint64_t(R) * 0x40;
  }

  // Runs a method that the sink recorded; the first problem is in Failure.
  Outcome run(const RecordingSink::Method &M, std::vector<Value> Args);
  Outcome run(StringRef Name) {
    const RecordingSink::Method *M = Sink.findMethod(Name);
    if (!M) {
      fail("no method " + Name);
      return {};
    }
    return run(*M, {});
  }

  void fail(const Twine &Msg) {
    if (Failure.empty())
      Failure = Msg.str();
  }

  const RecordingSink &Sink;
  Memory Mem;
  std::map<EntityRef, Value> Fields;
  std::map<EntityRef, uint64_t> RVAAddr;
  std::vector<std::string> CommandLine;
  Hook OnCall; // test methods and native functions
  std::string Failure;
  // Every store the methods did: (method, address, width, unaligned prefix).
  struct Store {
    std::string Method;
    uint64_t Addr;
    unsigned Width;
    unsigned Unaligned;
  };
  std::vector<Store> Stores;
  unsigned AllocHGlobalCalls = 0;

private:
  unsigned Depth = 0;
};

Interp::Outcome Interp::run(const RecordingSink::Method &M,
                            std::vector<Value> Args) {
  Outcome Out;
  if (++Depth > 16) {
    fail("call depth");
    return Out;
  }
  scope_exit Done([&] { --Depth; });
  std::map<uint32_t, size_t> IndexAt;
  for (size_t K = 0; K != M.Code.size(); ++K)
    IndexAt[M.Code[K].Offset] = K;
  IndexAt[M.CodeSize] = M.Code.size();
  std::vector<Value> Locals(M.Locals.size());
  std::vector<Value> Stack;
  unsigned Unaligned = 0;
  const std::string Where = "in '" + M.Name + "': ";

  auto Pop = [&](Value &V) {
    if (Stack.empty()) {
      fail(Where + "stack underflow");
      return false;
    }
    V = Stack.back();
    Stack.pop_back();
    return true;
  };
  auto Addr = [&](const Value &V, uint64_t &A) {
    if (V.Ty != Value::T::NI && V.Ty != Value::T::I4) {
      fail(Where + "an address that is not a native int");
      return false;
    }
    A = uint64_t(V.I);
    return true;
  };
  // The alignment a memory access may assume: the natural one, or that of
  // its 'unaligned.' prefix.
  auto CheckAlign = [&](uint64_t A, unsigned Natural, StringRef What) {
    unsigned Need = Unaligned ? Unaligned : Natural;
    if (A % Need)
      fail(Where + What + " at an address that is not " + Twine(Need) +
           "-aligned (offset " + Twine(A % 16) + " modulo 16)");
  };
  auto Jump = [&](uint32_t Off, size_t &PC) {
    auto It = IndexAt.find(Off);
    if (It == IndexAt.end()) {
      fail(Where + "branch to no instruction");
      return false;
    }
    PC = It->second;
    return true;
  };

  size_t PC = 0;
  for (unsigned Steps = 0;; ++Steps) {
    if (!Failure.empty())
      return Out;
    if (Steps > 2000000) {
      fail(Where + "no end after 2000000 steps");
      return Out;
    }
    if (PC >= M.Code.size()) {
      fail(Where + "fell off the end");
      return Out;
    }
    const RecordingSink::Instr &I = M.Code[PC++];
    unsigned Prefix = Unaligned;
    if (I.Op != Opcode::Unaligned && I.Op != Opcode::Volatile)
      Unaligned = 0;
    Value A, B, C;
    switch (I.Op) {
    case Opcode::Nop:
    case Opcode::Volatile:
      break;
    case Opcode::Unaligned:
      if (I.Imm != 1 && I.Imm != 2 && I.Imm != 4)
        fail(Where + "unaligned. " + Twine(I.Imm));
      Unaligned = I.Imm;
      break;
    case Opcode::Ldc_i4_m1:
    case Opcode::Ldc_i4_0:
    case Opcode::Ldc_i4_1:
    case Opcode::Ldc_i4_2:
    case Opcode::Ldc_i4_3:
    case Opcode::Ldc_i4_4:
    case Opcode::Ldc_i4_5:
    case Opcode::Ldc_i4_6:
    case Opcode::Ldc_i4_7:
    case Opcode::Ldc_i4_8:
      Stack.push_back(Value::i4(int(I.Op) - int(Opcode::Ldc_i4_0)));
      break;
    case Opcode::Ldc_i4_s:
    case Opcode::Ldc_i4:
      Stack.push_back(Value::i4(I.Imm));
      break;
    case Opcode::Ldc_i8:
      Stack.push_back(Value::i8(I.Imm));
      break;
    case Opcode::Ldarg_0:
    case Opcode::Ldarg_1:
    case Opcode::Ldarg_2:
    case Opcode::Ldarg_3:
    case Opcode::Ldarg_s:
    case Opcode::Ldarg: {
      int64_t N = I.Op == Opcode::Ldarg_s || I.Op == Opcode::Ldarg
                      ? I.Imm
                      : int(I.Op) - int(Opcode::Ldarg_0);
      if (N < 0 || size_t(N) >= Args.size()) {
        fail(Where + "no argument " + Twine(N));
        break;
      }
      Stack.push_back(Args[N]);
      break;
    }
    case Opcode::Ldloc_0:
    case Opcode::Ldloc_1:
    case Opcode::Ldloc_2:
    case Opcode::Ldloc_3:
    case Opcode::Ldloc_s:
    case Opcode::Ldloc: {
      int64_t N = I.Op == Opcode::Ldloc_s || I.Op == Opcode::Ldloc
                      ? I.Imm
                      : int(I.Op) - int(Opcode::Ldloc_0);
      if (N < 0 || size_t(N) >= Locals.size() ||
          Locals[N].Ty == Value::T::None) {
        fail(Where + "read of local " + Twine(N) + " before any store");
        break;
      }
      Stack.push_back(Locals[N]);
      break;
    }
    case Opcode::Stloc_0:
    case Opcode::Stloc_1:
    case Opcode::Stloc_2:
    case Opcode::Stloc_3:
    case Opcode::Stloc_s:
    case Opcode::Stloc: {
      int64_t N = I.Op == Opcode::Stloc_s || I.Op == Opcode::Stloc
                      ? I.Imm
                      : int(I.Op) - int(Opcode::Stloc_0);
      if (N < 0 || size_t(N) >= Locals.size()) {
        fail(Where + "no local " + Twine(N));
        break;
      }
      if (Pop(A))
        Locals[N] = A;
      break;
    }
    case Opcode::Dup:
      if (Pop(A)) {
        Stack.push_back(A);
        Stack.push_back(A);
      }
      break;
    case Opcode::Pop:
      Pop(A);
      break;
    case Opcode::Ldnull:
      Stack.push_back(Value::ni(0));
      break;
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Xor:
    case Opcode::Shl:
    case Opcode::Shr:
    case Opcode::Shr_un:
    case Opcode::Div:
    case Opcode::Div_un:
    case Opcode::Rem:
    case Opcode::Rem_un: {
      if (!Pop(B) || !Pop(A))
        break;
      if (!A.isInt() || !B.isInt()) {
        fail(Where + "arithmetic on a non-integer");
        break;
      }
      bool Shift =
          I.Op == Opcode::Shl || I.Op == Opcode::Shr || I.Op == Opcode::Shr_un;
      Value::T RT;
      if (Shift || A.Ty == B.Ty)
        RT = A.Ty;
      else if ((A.Ty == Value::T::I4 && B.Ty == Value::T::NI) ||
               (A.Ty == Value::T::NI && B.Ty == Value::T::I4))
        RT = Value::T::NI;
      else {
        fail(Where + "int64 mixed with another integer type (D2)");
        break;
      }
      bool Is32 = RT == Value::T::I4;
      uint64_t X = uint64_t(A.I), Y = uint64_t(B.I), R = 0;
      switch (I.Op) {
      case Opcode::Add:
        R = X + Y;
        break;
      case Opcode::Sub:
        R = X - Y;
        break;
      case Opcode::Mul:
        R = X * Y;
        break;
      case Opcode::And:
        R = X & Y;
        break;
      case Opcode::Or:
        R = X | Y;
        break;
      case Opcode::Xor:
        R = X ^ Y;
        break;
      case Opcode::Shl:
        R = X << (Y & (Is32 ? 31 : 63));
        break;
      case Opcode::Shr:
        R = Is32 ? uint64_t(int32_t(X) >> (Y & 31))
                 : uint64_t(int64_t(X) >> (Y & 63));
        break;
      case Opcode::Shr_un:
        R = Is32 ? uint64_t(uint32_t(X) >> (Y & 31)) : X >> (Y & 63);
        break;
      default:
        if (!Y) {
          fail(Where + "division by zero");
          break;
        }
        if (I.Op == Opcode::Div)
          R = Is32 ? uint64_t(int32_t(X) / int32_t(Y))
                   : uint64_t(int64_t(X) / int64_t(Y));
        else if (I.Op == Opcode::Rem)
          R = Is32 ? uint64_t(int32_t(X) % int32_t(Y))
                   : uint64_t(int64_t(X) % int64_t(Y));
        else if (I.Op == Opcode::Div_un)
          R = Is32 ? uint64_t(uint32_t(X) / uint32_t(Y)) : X / Y;
        else
          R = Is32 ? uint64_t(uint32_t(X) % uint32_t(Y)) : X % Y;
        break;
      }
      if (Is32)
        Stack.push_back(Value::i4(R));
      else {
        Value V;
        V.Ty = RT;
        V.I = int64_t(R);
        Stack.push_back(V);
      }
      break;
    }
    case Opcode::Neg:
    case Opcode::Not:
      if (!Pop(A))
        break;
      if (!A.isInt()) {
        fail(Where + "neg/not on a non-integer");
        break;
      }
      A.I = I.Op == Opcode::Neg ? int64_t(0 - uint64_t(A.I)) : ~A.I;
      if (A.Ty == Value::T::I4)
        A = Value::i4(A.I);
      Stack.push_back(A);
      break;
    case Opcode::Conv_i1:
    case Opcode::Conv_u1:
    case Opcode::Conv_i2:
    case Opcode::Conv_u2:
    case Opcode::Conv_i4:
    case Opcode::Conv_u4:
    case Opcode::Conv_i8:
    case Opcode::Conv_u8:
    case Opcode::Conv_i:
    case Opcode::Conv_u: {
      if (!Pop(A))
        break;
      if (!A.isInt()) {
        fail(Where + "conversion of a non-integer");
        break;
      }
      uint64_t X = uint64_t(A.I);
      // An int32 is zero-extended by the unsigned conversions.
      uint64_t U = A.Ty == Value::T::I4 ? uint64_t(uint32_t(X)) : X;
      switch (I.Op) {
      case Opcode::Conv_i1:
        Stack.push_back(Value::i4(int8_t(X)));
        break;
      case Opcode::Conv_u1:
        Stack.push_back(Value::i4(uint8_t(X)));
        break;
      case Opcode::Conv_i2:
        Stack.push_back(Value::i4(int16_t(X)));
        break;
      case Opcode::Conv_u2:
        Stack.push_back(Value::i4(uint16_t(X)));
        break;
      case Opcode::Conv_i4:
      case Opcode::Conv_u4:
        Stack.push_back(Value::i4(X));
        break;
      case Opcode::Conv_i8:
        Stack.push_back(Value::i8(int64_t(X)));
        break;
      case Opcode::Conv_u8:
        Stack.push_back(Value::i8(int64_t(U)));
        break;
      case Opcode::Conv_i:
        Stack.push_back(Value::ni(X));
        break;
      default:
        Stack.push_back(Value::ni(U));
        break;
      }
      break;
    }
    case Opcode::Ceq:
    case Opcode::Cgt:
    case Opcode::Cgt_un:
    case Opcode::Clt:
    case Opcode::Clt_un:
    case Opcode::Beq:
    case Opcode::Beq_s:
    case Opcode::Bne_un:
    case Opcode::Bne_un_s:
    case Opcode::Bge:
    case Opcode::Bge_s:
    case Opcode::Bge_un:
    case Opcode::Bge_un_s:
    case Opcode::Bgt:
    case Opcode::Bgt_s:
    case Opcode::Bgt_un:
    case Opcode::Bgt_un_s:
    case Opcode::Ble:
    case Opcode::Ble_s:
    case Opcode::Ble_un:
    case Opcode::Ble_un_s:
    case Opcode::Blt:
    case Opcode::Blt_s:
    case Opcode::Blt_un:
    case Opcode::Blt_un_s: {
      if (!Pop(B) || !Pop(A))
        break;
      if (!A.isInt() || !B.isInt()) {
        fail(Where + "comparison of a non-integer");
        break;
      }
      int64_t X = A.I, Y = B.I;
      bool Both32 = A.Ty == Value::T::I4 && B.Ty == Value::T::I4;
      uint64_t UX = Both32 ? uint32_t(X) : uint64_t(X);
      uint64_t UY = Both32 ? uint32_t(Y) : uint64_t(Y);
      StringRef N = getOpcodeInfo(I.Op).Name;
      N.consume_back(".s");
      bool R;
      if (N == "ceq" || N == "beq")
        R = X == Y;
      else if (N == "bne.un")
        R = X != Y;
      else if (N == "cgt" || N == "bgt")
        R = X > Y;
      else if (N == "cgt.un" || N == "bgt.un")
        R = UX > UY;
      else if (N == "bge")
        R = X >= Y;
      else if (N == "bge.un")
        R = UX >= UY;
      else if (N == "clt" || N == "blt")
        R = X < Y;
      else if (N == "clt.un" || N == "blt.un")
        R = UX < UY;
      else if (N == "ble")
        R = X <= Y;
      else
        R = UX <= UY;
      if (N.starts_with("c"))
        Stack.push_back(Value::i4(R));
      else if (R)
        Jump(*I.Target, PC);
      break;
    }
    case Opcode::Br:
    case Opcode::Br_s:
      Jump(*I.Target, PC);
      break;
    case Opcode::Brtrue:
    case Opcode::Brtrue_s:
    case Opcode::Brfalse:
    case Opcode::Brfalse_s: {
      if (!Pop(A))
        break;
      bool True = A.isInt() ? A.I != 0 : A.Ty != Value::T::None;
      bool Want = I.Op == Opcode::Brtrue || I.Op == Opcode::Brtrue_s;
      if (True == Want)
        Jump(*I.Target, PC);
      break;
    }
    case Opcode::Switch: {
      if (!Pop(A))
        break;
      if (A.Ty != Value::T::I4) {
        fail(Where + "switch on a non-int32");
        break;
      }
      if (uint32_t(A.I) < I.SwitchTargets.size())
        Jump(I.SwitchTargets[uint32_t(A.I)], PC);
      break;
    }
    case Opcode::Ldind_i1:
    case Opcode::Ldind_u1:
    case Opcode::Ldind_i2:
    case Opcode::Ldind_u2:
    case Opcode::Ldind_i4:
    case Opcode::Ldind_u4:
    case Opcode::Ldind_i8:
    case Opcode::Ldind_i: {
      uint64_t P;
      if (!Pop(A) || !Addr(A, P))
        break;
      unsigned W = 8;
      if (I.Op == Opcode::Ldind_i1 || I.Op == Opcode::Ldind_u1)
        W = 1;
      else if (I.Op == Opcode::Ldind_i2 || I.Op == Opcode::Ldind_u2)
        W = 2;
      else if (I.Op == Opcode::Ldind_i4 || I.Op == Opcode::Ldind_u4)
        W = 4;
      Unaligned = Prefix;
      CheckAlign(P, W, getOpcodeInfo(I.Op).Name);
      Unaligned = 0;
      uint64_t V;
      if (!Mem.read(P, W, V)) {
        fail(Where + getOpcodeInfo(I.Op).Name + " outside the heap");
        break;
      }
      switch (I.Op) {
      case Opcode::Ldind_i1:
        Stack.push_back(Value::i4(int8_t(V)));
        break;
      case Opcode::Ldind_i2:
        Stack.push_back(Value::i4(int16_t(V)));
        break;
      case Opcode::Ldind_u1:
      case Opcode::Ldind_u2:
      case Opcode::Ldind_i4:
      case Opcode::Ldind_u4:
        Stack.push_back(Value::i4(V));
        break;
      case Opcode::Ldind_i8:
        Stack.push_back(Value::i8(int64_t(V)));
        break;
      default:
        Stack.push_back(Value::ni(V));
        break;
      }
      break;
    }
    case Opcode::Stind_i1:
    case Opcode::Stind_i2:
    case Opcode::Stind_i4:
    case Opcode::Stind_i8:
    case Opcode::Stind_i: {
      uint64_t P;
      if (!Pop(B) || !Pop(A) || !Addr(A, P))
        break;
      if (!B.isInt()) {
        fail(Where + "store of a non-integer");
        break;
      }
      unsigned W = I.Op == Opcode::Stind_i1   ? 1
                   : I.Op == Opcode::Stind_i2 ? 2
                   : I.Op == Opcode::Stind_i4 ? 4
                                              : 8;
      if (W == 8 && B.Ty == Value::T::I4 && I.Op == Opcode::Stind_i)
        B = Value::ni(B.I); // int32 to native int: sign-extended
      Unaligned = Prefix;
      CheckAlign(P, W, getOpcodeInfo(I.Op).Name);
      Unaligned = 0;
      Stores.push_back({M.Name, P, W, Prefix});
      if (!Mem.write(P, W, uint64_t(B.I)))
        fail(Where + getOpcodeInfo(I.Op).Name + " outside the writable heap");
      break;
    }
    case Opcode::Cpblk:
    case Opcode::Initblk: {
      uint64_t Dst, Src = 0;
      if (!Pop(C) || !Pop(B) || !Pop(A) || !Addr(A, Dst))
        break;
      if (C.Ty != Value::T::I4) {
        fail(Where + "a block size that is not an int32");
        break;
      }
      uint64_t Size = uint32_t(C.I);
      Unaligned = Prefix;
      CheckAlign(Dst, 8, getOpcodeInfo(I.Op).Name);
      if (I.Op == Opcode::Cpblk) {
        if (!Addr(B, Src))
          break;
        CheckAlign(Src, 8, "the source of cpblk");
      }
      Unaligned = 0;
      for (uint64_t K = 0; K != Size; ++K) {
        uint64_t V = uint8_t(B.I);
        if (I.Op == Opcode::Cpblk && !Mem.read(Src + K, 1, V)) {
          fail(Where + "cpblk reads outside the heap");
          break;
        }
        if (!Mem.write(Dst + K, 1, V)) {
          fail(Where + getOpcodeInfo(I.Op).Name +
               " writes outside the writable heap");
          break;
        }
      }
      break;
    }
    case Opcode::Ldsfld:
    case Opcode::Stsfld:
    case Opcode::Ldsflda: {
      const RecordingSink::Entity *E = Sink.entity(I.Token);
      if (!E) {
        fail(Where + "a field token that the sink did not hand out");
        break;
      }
      EntityRef R = I.Token.getValue();
      if (I.Op == Opcode::Ldsflda) {
        if (E->K != Kind::RVAField) {
          fail(Where + "ldsflda of '" + E->Name + "'");
          break;
        }
        auto It = RVAAddr.find(R);
        if (It == RVAAddr.end()) {
          uint64_t P =
              Mem.alloc(std::max<size_t>(E->Bytes.size(), 1), 0, 0, true);
          for (size_t K = 0; K != E->Bytes.size(); ++K)
            Mem.find(P + K, 1)->Bytes[K] = E->Bytes[K];
          It = RVAAddr.emplace(R, P).first;
        }
        Stack.push_back(Value::ni(It->second));
        break;
      }
      if (E->K != Kind::Field && E->K != Kind::DataField) {
        fail(Where + "ldsfld/stsfld of '" + E->Name + "'");
        break;
      }
      if (I.Op == Opcode::Ldsfld) {
        auto It = Fields.find(R);
        Stack.push_back(It == Fields.end() ? Value::ni(0) : It->second);
        break;
      }
      if (!Pop(A))
        break;
      if (A.Ty != Value::T::NI) {
        fail(Where + "stsfld of a value that is not a native int into '" +
             E->Name + "'");
        break;
      }
      Fields[R] = A;
      break;
    }
    case Opcode::Ldftn: {
      const RecordingSink::Entity *E = Sink.entity(I.Token);
      if (!E || E->K != Kind::Method) {
        fail(Where + "ldftn of something other than a test method");
        break;
      }
      Stack.push_back(Value::ni(fnPtr(I.Token.getValue())));
      break;
    }
    case Opcode::Ldstr: {
      const RecordingSink::Entity *E = Sink.entity(I.Token);
      if (!E || E->K != Kind::UserString) {
        fail(Where + "ldstr of something other than a user string");
        break;
      }
      Stack.push_back(Value::str(E->Name));
      break;
    }
    case Opcode::Ldlen:
      if (!Pop(A))
        break;
      if (A.Ty != Value::T::StrArr) {
        fail(Where + "ldlen of a non-array");
        break;
      }
      Stack.push_back(Value::ni(A.Arr.size()));
      break;
    case Opcode::Ldelem_ref:
      if (!Pop(B) || !Pop(A))
        break;
      if (A.Ty != Value::T::StrArr || !B.isInt() || B.I < 0 ||
          size_t(B.I) >= A.Arr.size()) {
        fail(Where + "bad ldelem.ref");
        break;
      }
      Stack.push_back(Value::str(A.Arr[B.I]));
      break;
    case Opcode::Newobj: {
      const RecordingSink::Entity *E = Sink.entity(I.Token);
      if (!E || E->K != Kind::BCL ||
          E->Bcl != BCLMemberId::EntryPointNotFoundException_ctor) {
        fail(Where + "newobj of something unexpected");
        break;
      }
      if (!Pop(A))
        break;
      if (A.Ty != Value::T::Str) {
        fail(Where + "EntryPointNotFoundException(string) without a string");
        break;
      }
      Value X;
      X.Ty = Value::T::Exc;
      X.S = A.S;
      Stack.push_back(X);
      break;
    }
    case Opcode::Throw:
      if (!Pop(A))
        break;
      if (A.Ty != Value::T::Exc) {
        fail(Where + "throw of a non-exception");
        break;
      }
      Out.Threw = true;
      Out.Message = A.S;
      return Out;
    case Opcode::Ret:
      if (!isVoid(M.Sig.Return)) {
        if (!Pop(A))
          return Out;
        Out.Ret = A;
      }
      if (!Stack.empty())
        fail(Where + "ret with " + Twine(Stack.size()) +
             " values left on the stack");
      return Out;
    case Opcode::Call: {
      const RecordingSink::Entity *E = Sink.entity(I.Token);
      if (!E) {
        fail(Where + "a call token that the sink did not hand out");
        break;
      }
      if (E->K == Kind::BCL) {
        switch (E->Bcl) {
        case BCLMemberId::Marshal_AllocHGlobal: {
          if (!Pop(A))
            break;
          if (A.Ty != Value::T::NI) {
            fail(Where + "AllocHGlobal(native int) of another type");
            break;
          }
          ++AllocHGlobalCalls;
          // Garbage-filled, and 8-aligned but not 16-aligned: AllocHGlobal
          // promises pointer alignment only, so the data block must be
          // aligned up to MaxAlign.
          Stack.push_back(Value::ni(Mem.alloc(uint64_t(A.I), 0xCD, 8)));
          break;
        }
        case BCLMemberId::Marshal_StringToHGlobalAnsi:
        case BCLMemberId::Marshal_StringToHGlobalUni: {
          if (!Pop(A))
            break;
          if (A.Ty != Value::T::Str) {
            fail(Where + "StringToHGlobal of a non-string");
            break;
          }
          bool Uni = E->Bcl == BCLMemberId::Marshal_StringToHGlobalUni;
          ByteVec Bytes;
          for (char Ch : A.S) {
            Bytes.push_back(uint8_t(Ch));
            if (Uni)
              Bytes.push_back(0);
          }
          Bytes.insert(Bytes.end(), Uni ? 2 : 1, 0);
          uint64_t P = Mem.alloc(Bytes.size(), 0, 0, false,
                                 Uni ? Memory::Enc::Uni : Memory::Enc::Ansi);
          Mem.find(P, 1)->Bytes = Bytes;
          Stack.push_back(Value::ni(P));
          break;
        }
        case BCLMemberId::Environment_GetCommandLineArgs: {
          Value X;
          X.Ty = Value::T::StrArr;
          X.Arr = CommandLine;
          Stack.push_back(X);
          break;
        }
        default:
          fail(Where + "unexpected call of " + E->Name);
          break;
        }
        break;
      }
      std::vector<Value> CallArgs(E->Sig.Params.size());
      for (size_t K = CallArgs.size(); K-- > 0;)
        if (!Pop(CallArgs[K]))
          break;
      if (!Failure.empty())
        break;
      if (E->K == Kind::Helper) {
        const RecordingSink::Method *Callee = Sink.findMethod(E->Name);
        if (!Callee) {
          fail(Where + "call of the undefined helper " + E->Name);
          break;
        }
        Outcome O = run(*Callee, CallArgs);
        if (O.Threw)
          return O;
        if (!isVoid(E->Sig.Return)) {
          if (!O.Ret) {
            fail(Where + E->Name + " returned nothing");
            break;
          }
          Stack.push_back(*O.Ret);
        }
        break;
      }
      if (E->K != Kind::Method && E->K != Kind::Native) {
        fail(Where + "call of '" + E->Name + "'");
        break;
      }
      if (!OnCall) {
        fail(Where + "call of '" + E->Name + "' without a hook");
        break;
      }
      Value R = OnCall(*E, CallArgs);
      if (!isVoid(E->Sig.Return))
        Stack.push_back(R);
      break;
    }
    default:
      fail(Where + "the interpreter does not know " + getOpcodeInfo(I.Op).Name);
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// Trailer inputs
//===----------------------------------------------------------------------===//

ImageLayout makeLayout(ByteVec Image, uint64_t EndBeforeSlots,
                       unsigned NumSlots = 0, uint8_t MaxAlignLog2 = 4) {
  ImageLayout L;
  L.InitSize = Image.size();
  L.Image = std::move(Image);
  L.SlotsOffset = NumSlots ? alignTo(EndBeforeSlots, 8) : EndBeforeSlots;
  L.TotalSize = L.SlotsOffset + 8 * NumSlots;
  L.MaxAlignLog2 = MaxAlignLog2;
  return L;
}

RuntimeReloc reloc(uint64_t Site, uint8_t Width, EntityRef Field,
                   std::optional<uint64_t> ImageOffset, int64_t Addend = 0) {
  RuntimeReloc R;
  R.Site = Site;
  R.Width = Width;
  R.Field = Field;
  R.ImageOffset = ImageOffset;
  R.Addend = Addend;
  return R;
}

EntryPoint entry(EntityRef Main, bool HasArgs) {
  EntryPoint E;
  E.Main = Main;
  E.HasArgs = HasArgs;
  E.PointerSize = 8;
  return E;
}

// A whole trailer input with the entities the sink owns.
struct Program {
  RecordingSink Sink;
  ImageLayout Layout;
  TrailerInput In;
  std::map<std::string, EntityRef> Refs;

  EntityRef addressField(StringRef Name, uint64_t Offset) {
    EntityRef R = Sink.field(Name);
    Refs[Name.str()] = R;
    In.AddressFields.push_back({R, Offset});
    return R;
  }
  EntityRef fnField(StringRef Fn, MethodSig Sig) {
    EntityRef M = Sink.method(Fn, std::move(Sig));
    EntityRef F = Sink.field(("<msil>fn_" + Fn).str());
    Refs[Fn.str()] = M;
    Refs[("<msil>fn_" + Fn).str()] = F;
    In.FnAddrFields.push_back({F, M});
    return F;
  }
  EntityRef ref(StringRef Name) const {
    auto It = Refs.find(Name.str());
    EXPECT_NE(It, Refs.end()) << Name.str();
    return It == Refs.end() ? 0 : It->second;
  }
  void build() {
    if (In.Image)
      In.Image = &Layout;
    buildTrailer(In, Sink);
  }
};

// module-initializer.ll: table +0, '.str' +20, '.str.1' +25, names +32,
// op +48, '.str.2' +56, hits +72 (zero); the three relocations names[0],
// names[1] and op = sum; main(int32, native int).
const ByteVec ModuleInitializerImage = {
    0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x7A, 0x65, 0x72, 0x6F,
    0x00, 0x6F, 0x6E, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x65, 0x6C, 0x6C,
    0x6F, 0x20, 0x66, 0x72, 0x6F, 0x6D, 0x20, 0x6D, 0x73, 0x69, 0x6C, 0x00};

void makeModuleInitializer(Program &P, unsigned NumSlots = 0) {
  P.Layout = makeLayout(ModuleInitializerImage, 76, NumSlots);
  P.In.Image = &P.Layout;
  P.addressField("table", 0);
  EntityRef Str = P.addressField(".str", 20);
  EntityRef Str1 = P.addressField(".str.1", 25);
  P.addressField("names", 32);
  P.addressField("op", 48);
  P.addressField(".str.2", 56);
  P.addressField("hits", 72);
  EntityRef FnSum = P.fnField("sum", sig(tI4(), {tNI(), tI4()}));
  P.In.Relocs = {reloc(32, 8, Str, 20), reloc(40, 8, Str1, 25),
                 reloc(48, 8, FnSum, std::nullopt)};
  EntityRef Main = P.Sink.method("main", sig(tI4(), {tI4(), tNI()}));
  P.Refs["main"] = Main;
  P.In.Entry = entry(Main, true);
}

// global-relocs.ll's relocs.ll.
ByteVec globalRelocsImage() {
  ByteVec Image(144, 0);
  for (int I = 0; I != 4; ++I)
    Image[4 * I] = I + 1;
  Image[88] = 9;
  Image[104] = 5;
  Image[116] = 7;
  return Image;
}

void makeGlobalRelocs(Program &P, unsigned NumSlots = 0) {
  P.Layout = makeLayout(globalRelocsImage(), 148, NumSlots);
  P.In.Image = &P.Layout;
  const char *Names[] = {"target", "p0",    "p8",   "pneg",  "pend",
                         "fp",     "ifp",   "tbl",  "mixed", "s4",
                         "f4",     "s4add", "self", "pz",    "zero"};
  const uint64_t Offsets[] = {0,  16,  24,  32,  40,  48,  56, 64,
                              88, 112, 120, 124, 128, 136, 144};
  for (int I = 0; I != 15; ++I)
    P.addressField(Names[I], Offsets[I]);
  EntityRef Callee = P.fnField("callee", sig(tI4(), {tI4()}));
  EntityRef Internal = P.fnField("internal_fn", sig(TypeSig(), {}));
  EntityRef Target = P.ref("target");
  P.In.Relocs = {
      reloc(16, 8, Target, 0),
      reloc(24, 8, Target, 0, 8),
      reloc(32, 8, Target, 0, -4),
      reloc(40, 8, Target, 0, 16),
      reloc(48, 8, Callee, std::nullopt),
      reloc(56, 8, Internal, std::nullopt),
      reloc(64, 8, Callee, std::nullopt),
      reloc(72, 8, Target, 0),
      reloc(96, 8, P.ref("p0"), 16),
      reloc(112, 4, Target, 0),
      reloc(120, 4, Callee, std::nullopt),
      reloc(124, 4, Target, 0, 12),
      reloc(128, 8, P.ref("self"), 128),
      reloc(136, 8, P.ref("zero"), 144),
  };
}

// global-relocs.ll's unaligned.ll: x +0, the packed pk +16, q4 +56, h +72,
// zz +74 (zero).
ByteVec unalignedImage() {
  ByteVec Image(74, 0);
  Image[0] = 0x0B;
  Image[16] = 0x61;
  Image[25] = 0x62;
  Image[34] = 0x07;
  Image[44] = 0x09;
  Image[56] = 0x01;
  Image[62] = 0x02;
  Image[67] = 0x03;
  Image[72] = 0x05;
  return Image;
}

void makeUnaligned(Program &P, unsigned NumSlots = 0) {
  P.Layout = makeLayout(unalignedImage(), 76, NumSlots);
  P.In.Image = &P.Layout;
  EntityRef X = P.addressField("x", 0);
  P.addressField("pk", 16);
  P.addressField("q4", 56);
  P.addressField("h", 72);
  P.addressField("zz", 74);
  EntityRef Fn = P.fnField("fn", sig(tI4(), {tI4()}));
  P.In.Relocs = {
      reloc(17, 8, X, 0), reloc(26, 8, X, 0, 2), reloc(36, 8, Fn, std::nullopt),
      reloc(48, 8, X, 0), reloc(58, 4, X, 0, 4), reloc(63, 4, Fn, std::nullopt),
      reloc(68, 4, X, 0)};
}

// The sequences HEAD's llc printed (x86_64-pc-windows-msvc-msil, -O2),
// normalized.
const char ModuleInitializerCctor[] = R"(
ldc.i4 91; conv.i; call Marshal_AllocHGlobal; ldc.i4 15; conv.i; add;
ldc.i4 -16; conv.i; and; stsfld <msil>data; ldsfld <msil>data;
ldsflda <msil>image; ldc.i4 72; cpblk; ldsfld <msil>data; ldc.i4 72; add;
ldc.i4 0; ldc.i4 4; initblk; ldsfld <msil>data; stsfld table;
ldsfld <msil>data; ldc.i4 20; add; stsfld .str; ldsfld <msil>data; ldc.i4 25;
add; stsfld .str.1; ldsfld <msil>data; ldc.i4 32; add; stsfld names;
ldsfld <msil>data; ldc.i4 48; add; stsfld op; ldsfld <msil>data; ldc.i4 56;
add; stsfld .str.2; ldsfld <msil>data; ldc.i4 72; add; stsfld hits; ldftn sum;
stsfld <msil>fn_sum; ldsfld <msil>data; ldc.i4 32; add; ldsfld .str; stind.i;
ldsfld <msil>data; ldc.i4 40; add; ldsfld .str.1; stind.i; ldsfld <msil>data;
ldc.i4 48; add; ldsfld <msil>fn_sum; stind.i; ret
)";

const char MainWithArgs[] = R"(
call Environment_GetCommandLineArgs; stloc 0; ldloc 0; ldlen; conv.i4;
stloc 1; ldloc 1; ldc.i4 1; add; conv.i; ldc.i4 8; mul;
call Marshal_AllocHGlobal; stloc 2; ldc.i4 0; stloc 3; br L1; L0:; ldloc 2;
ldloc 3; conv.i; ldc.i4 8; mul; add; ldloc 0; ldloc 3; ldelem.ref;
call Marshal_StringToHGlobalAnsi; stind.i; ldloc 3; ldc.i4 1; add; stloc 3;
L1:; ldloc 3; ldloc 1; blt L0; ldloc 2; ldloc 1; conv.i; ldc.i4 8; mul; add;
ldc.i4 0; conv.i; stind.i; ldloc 1; ldloc 2; call main; ret
)";

const char GlobalRelocsCctor[] = R"(
ldc.i4 163; conv.i; call Marshal_AllocHGlobal; ldc.i4 15; conv.i; add;
ldc.i4 -16; conv.i; and; stsfld <msil>data; ldsfld <msil>data;
ldsflda <msil>image; ldc.i4 144; cpblk; ldsfld <msil>data; ldc.i4 144; add;
ldc.i4 0; ldc.i4 4; initblk; ldsfld <msil>data; stsfld target;
ldsfld <msil>data; ldc.i4 16; add; stsfld p0; ldsfld <msil>data; ldc.i4 24;
add; stsfld p8; ldsfld <msil>data; ldc.i4 32; add; stsfld pneg;
ldsfld <msil>data; ldc.i4 40; add; stsfld pend; ldsfld <msil>data; ldc.i4 48;
add; stsfld fp; ldsfld <msil>data; ldc.i4 56; add; stsfld ifp;
ldsfld <msil>data; ldc.i4 64; add; stsfld tbl; ldsfld <msil>data; ldc.i4 88;
add; stsfld mixed; ldsfld <msil>data; ldc.i4 112; add; stsfld s4;
ldsfld <msil>data; ldc.i4 120; add; stsfld f4; ldsfld <msil>data; ldc.i4 124;
add; stsfld s4add; ldsfld <msil>data; ldc.i4 128; add; stsfld self;
ldsfld <msil>data; ldc.i4 136; add; stsfld pz; ldsfld <msil>data; ldc.i4 144;
add; stsfld zero; ldftn callee; stsfld <msil>fn_callee; ldftn internal_fn;
stsfld <msil>fn_internal_fn; ldsfld <msil>data; ldc.i4 16; add; ldsfld target;
stind.i; ldsfld <msil>data; ldc.i4 24; add; ldsfld target; ldc.i4 8; add;
stind.i; ldsfld <msil>data; ldc.i4 32; add; ldsfld target; ldc.i4 -4; add;
stind.i; ldsfld <msil>data; ldc.i4 40; add; ldsfld target; ldc.i4 16; add;
stind.i; ldsfld <msil>data; ldc.i4 48; add; ldsfld <msil>fn_callee; stind.i;
ldsfld <msil>data; ldc.i4 56; add; ldsfld <msil>fn_internal_fn; stind.i;
ldsfld <msil>data; ldc.i4 64; add; ldsfld <msil>fn_callee; stind.i;
ldsfld <msil>data; ldc.i4 72; add; ldsfld target; stind.i; ldsfld <msil>data;
ldc.i4 96; add; ldsfld p0; stind.i; ldsfld <msil>data; ldc.i4 112; add;
ldsfld target; conv.i4; stind.i4; ldsfld <msil>data; ldc.i4 120; add;
ldsfld <msil>fn_callee; conv.i4; stind.i4; ldsfld <msil>data; ldc.i4 124; add;
ldsfld target; ldc.i4 12; add; conv.i4; stind.i4; ldsfld <msil>data;
ldc.i4 128; add; ldsfld self; stind.i; ldsfld <msil>data; ldc.i4 136; add;
ldsfld zero; stind.i; ret
)";

const char UnalignedCctor[] = R"(
ldc.i4 91; conv.i; call Marshal_AllocHGlobal; ldc.i4 15; conv.i; add;
ldc.i4 -16; conv.i; and; stsfld <msil>data; ldsfld <msil>data;
ldsflda <msil>image; ldc.i4 74; cpblk; ldsfld <msil>data; ldc.i4 74; add;
ldc.i4 0; ldc.i4 2; unaligned. 2; initblk; ldsfld <msil>data; stsfld x;
ldsfld <msil>data; ldc.i4 16; add; stsfld pk; ldsfld <msil>data; ldc.i4 56;
add; stsfld q4; ldsfld <msil>data; ldc.i4 72; add; stsfld h;
ldsfld <msil>data; ldc.i4 74; add; stsfld zz; ldftn fn; stsfld <msil>fn_fn;
ldsfld <msil>data; ldc.i4 17; add; ldsfld x; unaligned. 1; stind.i;
ldsfld <msil>data; ldc.i4 26; add; ldsfld x; ldc.i4 2; add; unaligned. 2;
stind.i; ldsfld <msil>data; ldc.i4 36; add; ldsfld <msil>fn_fn; unaligned. 4;
stind.i; ldsfld <msil>data; ldc.i4 48; add; ldsfld x; stind.i;
ldsfld <msil>data; ldc.i4 58; add; ldsfld x; ldc.i4 4; add; conv.i4;
unaligned. 2; stind.i4; ldsfld <msil>data; ldc.i4 63; add; ldsfld <msil>fn_fn;
conv.i4; unaligned. 1; stind.i4; ldsfld <msil>data; ldc.i4 68; add; ldsfld x;
conv.i4; stind.i4; ret
)";

const char ZeroOnlyCctor[] = R"(
ldc.i4 47; conv.i; call Marshal_AllocHGlobal; ldc.i4 15; conv.i; add;
ldc.i4 -16; conv.i; and; stsfld <msil>data; ldsfld <msil>data; ldc.i4 0;
ldc.i4 32; initblk; ldsfld <msil>data; stsfld z; ldsfld <msil>data; ldc.i4 8;
add; stsfld z2; ret
)";

const char InitOnlyCctor[] = R"(
ldc.i4 31; conv.i; call Marshal_AllocHGlobal; ldc.i4 15; conv.i; add;
ldc.i4 -16; conv.i; and; stsfld <msil>data; ldsfld <msil>data;
ldsflda <msil>image; ldc.i4 16; cpblk; ldsfld <msil>data; stsfld a;
ldsfld <msil>data; ldc.i4 8; add; stsfld b; ret
)";

const MethodAttributes CctorFlags =
    MethodAttributes::Private | MethodAttributes::HideBySig |
    MethodAttributes::SpecialName | MethodAttributes::RTSpecialName |
    MethodAttributes::Static;
const MethodAttributes MainFlags = MethodAttributes::Private |
                                   MethodAttributes::HideBySig |
                                   MethodAttributes::Static;

// Runs '.cctor' and checks the data block against the inputs: alignment,
// the image, the cleared zero part, every address and function field, and
// every relocation site. Imports maps a data-import field to the address
// the simulated native lookup gives.
void runAndCheckData(Program &P, Interp &VM,
                     const std::map<EntityRef, uint64_t> &Imports = {}) {
  if (!VM.OnCall)
    VM.OnCall = [](const RecordingSink::Entity &E, std::vector<Value> &) {
      return Value::i4(0);
    };
  Interp::Outcome O = VM.run(".cctor");
  ASSERT_EQ(VM.Failure, "");
  ASSERT_FALSE(O.Threw) << O.Message;
  const ImageLayout &L = P.Layout;
  ASSERT_NE(P.Sink.DataField, 0u);
  uint64_t Data = uint64_t(VM.Fields[P.Sink.DataField].I);
  EXPECT_EQ(Data % (uint64_t(1) << L.MaxAlignLog2), 0u)
      << "'<msil>data' is not aligned to MaxAlign";
  ASSERT_NE(VM.Mem.find(Data, L.TotalSize), nullptr)
      << "the data block does not hold TotalSize bytes";
  std::map<EntityRef, uint64_t> FieldValue;
  for (auto [F, Off] : P.In.AddressFields) {
    FieldValue[F] = Data + Off;
    EXPECT_EQ(uint64_t(VM.Fields[F].I), Data + Off) << P.Sink.get(F).Name;
  }
  for (auto [F, M] : P.In.FnAddrFields) {
    FieldValue[F] = Interp::fnPtr(M);
    EXPECT_EQ(uint64_t(VM.Fields[F].I), Interp::fnPtr(M)) << P.Sink.get(F).Name;
  }
  for (auto [F, V] : Imports) {
    FieldValue[F] = V;
    EXPECT_EQ(uint64_t(VM.Fields[F].I), V) << P.Sink.get(F).Name;
  }
  ByteVec Expected(L.Image.begin(), L.Image.end());
  Expected.resize(L.SlotsOffset, 0);
  for (const RuntimeReloc &R : P.In.Relocs) {
    uint64_t V = R.ImageOffset ? Data + *R.ImageOffset : FieldValue[R.Field];
    V += uint64_t(R.Addend);
    for (unsigned K = 0; K != R.Width; ++K)
      Expected[R.Site + K] = uint8_t(V >> (8 * K));
  }
  for (uint64_t K = 0; K != L.SlotsOffset; ++K) {
    uint64_t B;
    ASSERT_TRUE(VM.Mem.read(Data + K, 1, B));
    EXPECT_EQ(B, Expected[K]) << "data byte " << K;
  }
}

unsigned countOpcode(const RecordingSink::Method &M, Opcode Op) {
  return count_if(M.Code,
                  [&](const RecordingSink::Instr &I) { return I.Op == Op; });
}

//===----------------------------------------------------------------------===//
// Straight-line mode: Phase 1's sequences
//===----------------------------------------------------------------------===//

TEST(CLITrailerTest, ModuleInitializerSequence) {
  Program P;
  makeModuleInitializer(P);
  P.build();
  EXPECT_EQ(P.Sink.methodNames(),
            (std::vector<std::string>{".cctor", "<msil>Main"}));
  EXPECT_EQ(P.Sink.render(".cctor"), lines(ModuleInitializerCctor));
  const RecordingSink::Method *C = P.Sink.findMethod(".cctor");
  ASSERT_NE(C, nullptr);
  EXPECT_EQ(C->MaxStack, 3u);
  EXPECT_EQ(C->Flags, CctorFlags);
  EXPECT_TRUE(isVoid(C->Sig.Return));
  EXPECT_TRUE(C->Sig.Params.empty());
  EXPECT_TRUE(C->Locals.empty());
  const RecordingSink::Entity *Img = P.Sink.findRVA("<msil>image");
  ASSERT_NE(Img, nullptr);
  EXPECT_EQ(Img->TypeName, "<msil>image_t");
  EXPECT_EQ(Img->Bytes, ModuleInitializerImage);
  EXPECT_EQ(P.Sink.findRVA("<msil>relocs"), nullptr);
  Interp VM(P.Sink);
  runAndCheckData(P, VM);
}

TEST(CLITrailerTest, EntryPointWithArgs) {
  Program P;
  makeModuleInitializer(P);
  P.build();
  EXPECT_EQ(P.Sink.render("<msil>Main"), lines(MainWithArgs));
  const RecordingSink::Method *M = P.Sink.findMethod("<msil>Main");
  ASSERT_NE(M, nullptr);
  EXPECT_EQ(M->MaxStack, 3u);
  EXPECT_EQ(M->Flags, MainFlags);
  EXPECT_EQ(M->Sig.Return, tI4());
  EXPECT_EQ(M->Sig.Params, (std::vector<TypeSig>{TypeSig::getSZArray(tStr())}));
  EXPECT_EQ(M->Locals, (std::vector<TypeSig>{TypeSig::getSZArray(tStr()), tI4(),
                                             tNI(), tI4()}));

  // argv[0] is the program path, then the arguments, then NULL.
  Interp VM(P.Sink);
  VM.CommandLine = {"C:\\t\\prog.dll", "alpha", "b"};
  bool Called = false;
  VM.OnCall = [&](const RecordingSink::Entity &E, std::vector<Value> &Args) {
    EXPECT_EQ(E.Name, "main");
    Called = true;
    EXPECT_EQ(Args.size(), 2u);
    if (Args.size() != 2)
      return Value::i4(0);
    EXPECT_EQ(Args[0].Ty, Value::T::I4);
    EXPECT_EQ(Args[0].I, 3);
    EXPECT_EQ(Args[1].Ty, Value::T::NI);
    uint64_t Argv = uint64_t(Args[1].I);
    for (unsigned K = 0; K != 4; ++K) {
      uint64_t Ptr = 1;
      EXPECT_TRUE(VM.Mem.read(Argv + 8 * K, 8, Ptr));
      if (K == 3) {
        EXPECT_EQ(Ptr, 0u) << "argv[argc]";
        continue;
      }
      std::optional<std::string> S = VM.Mem.readString(Ptr, Memory::Enc::Ansi);
      EXPECT_EQ(S, VM.CommandLine[K]);
    }
    return Value::i4(7);
  };
  Interp::Outcome O = VM.run("<msil>Main");
  EXPECT_EQ(VM.Failure, "");
  EXPECT_TRUE(Called);
  ASSERT_TRUE(O.Ret.has_value());
  EXPECT_EQ(O.Ret->I, 7);
}

TEST(CLITrailerTest, EntryPointWithoutArgs) {
  Program P;
  EntityRef Main = P.Sink.method("main", sig(tI4(), {}));
  P.In.Entry = entry(Main, false);
  P.build();
  EXPECT_EQ(P.Sink.methodNames(), (std::vector<std::string>{"<msil>Main"}));
  EXPECT_EQ(P.Sink.render("<msil>Main"), (Lines{"call main", "ret"}));
  const RecordingSink::Method *M = P.Sink.findMethod("<msil>Main");
  ASSERT_NE(M, nullptr);
  EXPECT_EQ(M->MaxStack, 1u);
  EXPECT_EQ(M->Flags, MainFlags);
  EXPECT_TRUE(M->Locals.empty());
  Interp VM(P.Sink);
  VM.OnCall = [](const RecordingSink::Entity &, std::vector<Value> &) {
    return Value::i4(42);
  };
  Interp::Outcome O = VM.run("<msil>Main");
  EXPECT_EQ(VM.Failure, "");
  ASSERT_TRUE(O.Ret.has_value());
  EXPECT_EQ(O.Ret->I, 42);
  // Without data there is no data field and no .cctor.
  EXPECT_FALSE(P.Sink.hasEntity(Kind::DataField));
}

TEST(CLITrailerTest, NothingToEmit) {
  Program P;
  P.build();
  EXPECT_TRUE(P.Sink.Methods.empty());
  EXPECT_FALSE(P.Sink.hasEntity(Kind::DataField));
  EXPECT_FALSE(P.Sink.hasEntity(Kind::RVAField));
}

TEST(CLITrailerTest, GlobalRelocsSequence) {
  Program P;
  makeGlobalRelocs(P);
  P.build();
  EXPECT_EQ(P.Sink.methodNames(), (std::vector<std::string>{".cctor"}));
  EXPECT_EQ(P.Sink.render(".cctor"), lines(GlobalRelocsCctor));
  EXPECT_EQ(P.Sink.findMethod(".cctor")->MaxStack, 3u);
  Interp VM(P.Sink);
  runAndCheckData(P, VM);
}

TEST(CLITrailerTest, UnalignedSitesGetPrefixesOnlyWhereNeeded) {
  Program P;
  makeUnaligned(P);
  P.build();
  EXPECT_EQ(P.Sink.render(".cctor"), lines(UnalignedCctor));
  EXPECT_EQ(P.Sink.findMethod(".cctor")->MaxStack, 3u);
  Interp VM(P.Sink);
  runAndCheckData(P, VM);
}

TEST(CLITrailerTest, ZeroOnlyImage) {
  // No initialized byte: no RVA image and no cpblk.
  Program P;
  P.Layout = makeLayout({}, 32);
  P.In.Image = &P.Layout;
  P.addressField("z", 0);
  P.addressField("z2", 8);
  P.build();
  EXPECT_EQ(P.Sink.render(".cctor"), lines(ZeroOnlyCctor));
  EXPECT_FALSE(P.Sink.hasEntity(Kind::RVAField));
  Interp VM(P.Sink);
  runAndCheckData(P, VM);
}

TEST(CLITrailerTest, InitializedOnlyImage) {
  // No zero part: no initblk.
  Program P;
  P.Layout = makeLayout({5, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0}, 16);
  P.In.Image = &P.Layout;
  P.addressField("a", 0);
  P.addressField("b", 8);
  P.build();
  EXPECT_EQ(P.Sink.render(".cctor"), lines(InitOnlyCctor));
  Interp VM(P.Sink);
  runAndCheckData(P, VM);
}

TEST(CLITrailerTest, ConstructorsRunInOrderAndValuesArePopped) {
  // Ctors arrive in run order (5.9.9); one returns a value.
  Program P;
  EntityRef C2 = P.Sink.method("c2", sig(tI4(), {}));
  EntityRef C1 = P.Sink.method("c1", sig(TypeSig(), {}));
  EntityRef C3 = P.Sink.method("c3", sig(tI4(), {}));
  P.In.Ctors = {{C2, true}, {C1, false}, {C3, true}};
  EntityRef Main = P.Sink.method("main", sig(tI4(), {}));
  P.In.Entry = entry(Main, false);
  P.build();
  EXPECT_EQ(P.Sink.methodNames(),
            (std::vector<std::string>{".cctor", "<msil>Main"}));
  EXPECT_EQ(P.Sink.render(".cctor"),
            (Lines{"call c2", "pop", "call c1", "call c3", "pop", "ret"}));
  EXPECT_EQ(P.Sink.findMethod(".cctor")->MaxStack, 1u);
  EXPECT_FALSE(P.Sink.hasEntity(Kind::DataField));
}

TEST(CLITrailerTest, ConstructorsRunAfterRelocations) {
  Program P;
  makeModuleInitializer(P);
  EntityRef Ctor = P.Sink.method("ctor", sig(TypeSig(), {}));
  P.In.Ctors = {{Ctor, false}};
  P.build();
  Lines Cctor = P.Sink.render(".cctor");
  Lines Expected = lines(ModuleInitializerCctor);
  Expected.insert(Expected.end() - 1, "call ctor");
  EXPECT_EQ(Cctor, Expected);
}

//===----------------------------------------------------------------------===//
// The relocation loop
//===----------------------------------------------------------------------===//

TEST(CLITrailerTest, CountAddressSlots) {
  Program P;
  makeGlobalRelocs(P);
  ArrayRef<RuntimeReloc> R = P.In.Relocs; // 14 relocations
  // Straight-line: no slots.
  EXPECT_EQ(countAddressSlots(R, 32), 0u);
  EXPECT_EQ(countAddressSlots(R, 14), 0u);
  // Loop: one slot per distinct (field, addend) without an image target:
  // callee (three sites) and internal_fn.
  EXPECT_EQ(countAddressSlots(R, 13), 2u);
  EXPECT_EQ(countAddressSlots(R, 0), 2u);

  EntityRef F = P.ref("<msil>fn_callee");
  EntityRef G = P.ref("<msil>fn_internal_fn");
  std::vector<RuntimeReloc> S = {
      reloc(0, 8, F, std::nullopt),     reloc(8, 8, F, std::nullopt, 4),
      reloc(16, 4, F, std::nullopt, 4), reloc(20, 8, G, std::nullopt),
      reloc(32, 8, P.ref("target"), 0), reloc(40, 8, F, std::nullopt)};
  EXPECT_EQ(countAddressSlots(S, 0), 3u);
  EXPECT_EQ(countAddressSlots(S, 6), 0u);
  EXPECT_EQ(countAddressSlots(S, 5), 3u);
  // Only image targets: none.
  std::vector<RuntimeReloc> T = {reloc(0, 8, 0, 16), reloc(8, 8, 0, 24, 4)};
  EXPECT_EQ(countAddressSlots(T, 0), 0u);
  EXPECT_EQ(countAddressSlots({}, 0), 0u);
}

TEST(CLITrailerTest, ThresholdIsInclusive) {
  // At most RelocLoopThreshold relocations: straight-line.
  {
    Program P;
    makeUnaligned(P);
    P.In.RelocLoopThreshold = 7;
    P.build();
    EXPECT_EQ(P.Sink.render(".cctor"), lines(UnalignedCctor));
    EXPECT_EQ(P.Sink.findRVA("<msil>relocs"), nullptr);
  }
  {
    Program P;
    makeUnaligned(P, /*NumSlots=*/1);
    P.In.RelocLoopThreshold = 6;
    P.build();
    EXPECT_NE(P.Sink.findRVA("<msil>relocs"), nullptr);
    EXPECT_NE(P.Sink.render(".cctor"), lines(UnalignedCctor));
  }
  {
    // A module without relocations never has a table.
    Program P;
    P.Layout = makeLayout({5, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0}, 16);
    P.In.Image = &P.Layout;
    P.addressField("a", 0);
    P.addressField("b", 8);
    P.In.RelocLoopThreshold = 0;
    P.build();
    EXPECT_EQ(P.Sink.render(".cctor"), lines(InitOnlyCctor));
    EXPECT_EQ(P.Sink.findRVA("<msil>relocs"), nullptr);
  }
}

struct TableEntry {
  uint32_t Site, Kind;
  int64_t Value;
  bool operator==(const TableEntry &O) const {
    return Site == O.Site && Kind == O.Kind && Value == O.Value;
  }
};

std::ostream &operator<<(std::ostream &OS, const TableEntry &E) {
  return OS << "{" << E.Site << ", " << E.Kind << ", " << E.Value << "}";
}

std::vector<TableEntry> decodeTable(const RecordingSink &S) {
  const RecordingSink::Entity *E = S.findRVA("<msil>relocs");
  if (!E) {
    ADD_FAILURE() << "no '<msil>relocs' table";
    return {};
  }
  EXPECT_EQ(E->TypeName, "<msil>relocs_t");
  EXPECT_EQ(E->Bytes.size() % 16, 0u);
  std::vector<TableEntry> R;
  for (size_t K = 0; K + 16 <= E->Bytes.size(); K += 16) {
    auto U = [&](size_t Off, unsigned N) {
      uint64_t V = 0;
      for (unsigned B = 0; B != N; ++B)
        V |= uint64_t(E->Bytes[K + Off + B]) << (8 * B);
      return V;
    };
    R.push_back({uint32_t(U(0, 4)), uint32_t(U(4, 4)), int64_t(U(8, 8))});
  }
  return R;
}

TEST(CLITrailerTest, LoopTableEncodesKindsZeroToThree) {
  Program P;
  makeUnaligned(P, /*NumSlots=*/1); // slots at 80
  P.In.RelocLoopThreshold = 0;
  P.build();
  // Kind 0/1: image targets (Value = offset + addend), 2/3: through the one
  // slot of (fn, 0).
  EXPECT_EQ(decodeTable(P.Sink), (std::vector<TableEntry>{{17, 0, 0},
                                                          {26, 0, 2},
                                                          {36, 2, 80},
                                                          {48, 0, 0},
                                                          {58, 1, 4},
                                                          {63, 3, 80},
                                                          {68, 1, 0}}));
  const RecordingSink::Method *C = P.Sink.findMethod(".cctor");
  ASSERT_NE(C, nullptr);
  // One ldftn per function stays (M9's Framework function pointers).
  EXPECT_EQ(countOpcode(*C, Opcode::Ldftn), 1u);
  // AllocHGlobal(TotalSize + MaxAlign - 1) with the slot: 88 + 15.
  Lines Text = P.Sink.render(*C);
  ASSERT_GE(Text.size(), 3u);
  EXPECT_EQ(Text[0], "ldc.i4 103");
  EXPECT_EQ(Text[2], "call Marshal_AllocHGlobal");
}

TEST(CLITrailerTest, LoopAppliesUnalignedAndFourByteSites) {
  Program P;
  makeUnaligned(P, /*NumSlots=*/1);
  P.In.RelocLoopThreshold = 1;
  P.build();
  Interp VM(P.Sink);
  runAndCheckData(P, VM);
  // The loop stores through 'unaligned. 1'.
  unsigned LoopStores = 0;
  for (const Interp::Store &S : VM.Stores)
    if (S.Method == ".cctor" && S.Unaligned == 1)
      ++LoopStores;
  EXPECT_GE(LoopStores, 7u);
}

TEST(CLITrailerTest, LoopTableSharesSlotsPerFieldAndAddend) {
  Program P;
  makeGlobalRelocs(P, /*NumSlots=*/2); // slots at 152 and 160
  P.In.RelocLoopThreshold = 1;
  P.build();
  std::vector<TableEntry> T = decodeTable(P.Sink);
  ASSERT_EQ(T.size(), 14u);
  int64_t SlotCallee = T[4].Value, SlotInternal = T[5].Value;
  EXPECT_NE(SlotCallee, SlotInternal);
  for (int64_t S : {SlotCallee, SlotInternal})
    EXPECT_TRUE(S == 152 || S == 160) << S;
  EXPECT_EQ(T, (std::vector<TableEntry>{{16, 0, 0},
                                        {24, 0, 8},
                                        {32, 0, -4},
                                        {40, 0, 16},
                                        {48, 2, SlotCallee},
                                        {56, 2, SlotInternal},
                                        {64, 2, SlotCallee},
                                        {72, 0, 0},
                                        {96, 0, 16},
                                        {112, 1, 0},
                                        {120, 3, SlotCallee},
                                        {124, 1, 12},
                                        {128, 0, 128},
                                        {136, 0, 144}}));
  const RecordingSink::Method *C = P.Sink.findMethod(".cctor");
  ASSERT_NE(C, nullptr);
  EXPECT_EQ(countOpcode(*C, Opcode::Ldftn), 2u);
  Interp VM(P.Sink);
  runAndCheckData(P, VM);
}

TEST(CLITrailerTest, LoopModeInitializerMatchesStraightLine) {
  // The same program in both modes leaves the same data, and the loop's
  // .cctor still calls the constructors last.
  Program P;
  makeModuleInitializer(P, /*NumSlots=*/1);
  EntityRef Ctor = P.Sink.method("ctor", sig(TypeSig(), {}));
  P.In.Ctors = {{Ctor, false}};
  P.In.RelocLoopThreshold = 0;
  P.build();
  EXPECT_EQ(P.Sink.render("<msil>Main"), lines(MainWithArgs));
  Interp VM(P.Sink);
  std::vector<uint64_t> AtCtor;
  VM.OnCall = [&](const RecordingSink::Entity &E, std::vector<Value> &) {
    EXPECT_EQ(E.Name, "ctor");
    // Every relocation site is set when the constructor runs.
    uint64_t Data = uint64_t(VM.Fields[P.Sink.DataField].I);
    for (uint64_t Site : {32, 40, 48}) {
      uint64_t V = 0;
      VM.Mem.read(Data + Site, 8, V);
      AtCtor.push_back(V);
    }
    return Value::i4(0);
  };
  runAndCheckData(P, VM);
  uint64_t Data = uint64_t(VM.Fields[P.Sink.DataField].I);
  EXPECT_EQ(AtCtor, (std::vector<uint64_t>{Data + 20, Data + 25,
                                           Interp::fnPtr(P.ref("sum"))}));
}

//===----------------------------------------------------------------------===//
// Native data imports
//===----------------------------------------------------------------------===//

// A program with two data relocations to imported data and a constructor:
// p (8 bytes at 0) = &_iob + 48 and q (4 bytes at 8) = (int)&gdata.
struct ImportProgram : Program {
  EntityRef Iob, Fmode, GData, Load, GetSymbol, Ctor;
  std::vector<uint64_t> AtCtor;

  void make(DataImportHelper::Kind K, unsigned NumSlots = 0,
            unsigned Threshold = 32) {
    Layout = makeLayout(ByteVec(12, 0), 16, NumSlots);
    In.Image = &Layout;
    addressField("p", 0);
    addressField("q", 8);
    Iob = Sink.field("_iob");
    Fmode = Sink.field("_fmode");
    GData = Sink.field("gdata");
    bool Win = K == DataImportHelper::Kind::Kernel32;
    StringRef Lib = Win ? "msvcrt.dll" : "libc.so.6";
    StringRef Test = Win ? "testlib.dll" : "libtest.so";
    In.DataImports = {dataImport(Iob, Lib, "_iob"),
                      dataImport(Fmode, Lib, "_fmode"),
                      dataImport(GData, Test, "gdata")};
    if (Win) {
      Load = Sink.native("LoadLibraryW", sig(tNI(), {tNI()}));
      GetSymbol = Sink.native("GetProcAddress", sig(tNI(), {tNI(), tNI()}));
    } else {
      Load = Sink.native("dlopen", sig(tNI(), {tNI(), tI4()}));
      GetSymbol = Sink.native("dlsym", sig(tNI(), {tNI(), tNI()}));
    }
    DataImportHelper H;
    H.K = K;
    H.Load = Load;
    H.GetSymbol = GetSymbol;
    In.ImportHelper = H;
    In.Relocs = {reloc(0, 8, Iob, std::nullopt, 48),
                 reloc(8, 4, GData, std::nullopt)};
    In.RelocLoopThreshold = Threshold;
    Ctor = Sink.method("ctor", sig(TypeSig(), {}));
    In.Ctors = {{Ctor, false}};
  }

  static DataImport dataImport(EntityRef F, StringRef Module, StringRef Entry) {
    DataImport D;
    D.Field = F;
    D.Module = Module.str();
    D.Entry = Entry.str();
    return D;
  }

  // The simulated native loader: modules and their data exports.
  void install(Interp &VM, bool Win, StringRef Missing = "") {
    VM.OnCall = [this, &VM, Win, Missing = Missing.str()](
                    const RecordingSink::Entity &E, std::vector<Value> &Args) {
      static const std::map<std::string, uint64_t> Handles = {
          {"msvcrt.dll", 0x71000000},
          {"testlib.dll", 0x72000000},
          {"libc.so.6", 0x73000000},
          {"libtest.so", 0x74000000}};
      static const std::map<std::pair<uint64_t, std::string>, uint64_t>
          Exports = {{{0x71000000, "_iob"}, 0x55550000},
                     {{0x71000000, "_fmode"}, 0x55550100},
                     {{0x72000000, "gdata"}, 0x66660000},
                     {{0x73000000, "_iob"}, 0x57570000},
                     {{0x73000000, "_fmode"}, 0x57570100},
                     {{0x74000000, "gdata"}, 0x68680000}};
      if (E.Name == "ctor") {
        uint64_t Data = uint64_t(VM.Fields[Sink.DataField].I), V = 0;
        VM.Mem.read(Data, 8, V);
        AtCtor.push_back(V);
        VM.Mem.read(Data + 8, 4, V);
        AtCtor.push_back(V);
        return Value::i4(0);
      }
      if (E.Name == "LoadLibraryW" || E.Name == "dlopen") {
        EXPECT_EQ(E.Name == "LoadLibraryW", Win);
        if (!Win) {
          // dlopen(path, RTLD_LAZY)
          EXPECT_EQ(Args.size(), 2u);
          if (Args.size() == 2) {
            EXPECT_EQ(Args[1].Ty, Value::T::I4);
            EXPECT_EQ(Args[1].I, 1);
          }
        }
        std::optional<std::string> Name = VM.Mem.readString(
            uint64_t(Args[0].I), Win ? Memory::Enc::Uni : Memory::Enc::Ansi);
        EXPECT_TRUE(Name.has_value())
            << E.Name << " needs a "
            << (Win ? "StringToHGlobalUni" : "StringToHGlobalAnsi")
            << " string";
        auto It = Name ? Handles.find(*Name) : Handles.end();
        return Value::ni(It == Handles.end() ? 0 : It->second);
      }
      if (E.Name == "GetProcAddress" || E.Name == "dlsym") {
        std::optional<std::string> Name =
            VM.Mem.readString(uint64_t(Args[1].I), Memory::Enc::Ansi);
        EXPECT_TRUE(Name.has_value())
            << E.Name << " needs a StringToHGlobalAnsi string";
        if (!Name || *Name == Missing)
          return Value::ni(0);
        auto It = Exports.find({uint64_t(Args[0].I), *Name});
        return Value::ni(It == Exports.end() ? 0 : It->second);
      }
      ADD_FAILURE() << "unexpected call of " << E.Name;
      return Value::i4(0);
    };
  }
};

void checkHelperShape(const RecordingSink &S, bool Win) {
  const RecordingSink::Method *H = S.findMethod("<msil>dataimport");
  ASSERT_NE(H, nullptr);
  EXPECT_EQ(H->Sig.Return, tNI());
  EXPECT_EQ(H->Sig.Params, (std::vector<TypeSig>{tStr(), tStr()}));
  EXPECT_TRUE((H->Flags & MethodAttributes::Static) ==
              MethodAttributes::Static);
  // The calls, in order: data-win.il's shape for Kernel32; dlopen(path, 1)
  // and dlsym for Dlfcn.
  Lines Calls;
  for (const std::string &L : S.render(*H))
    if (StringRef(L).starts_with("call "))
      Calls.push_back(L);
  if (Win)
    EXPECT_EQ(
        Calls,
        (Lines{"call Marshal_StringToHGlobalUni", "call LoadLibraryW",
               "call Marshal_StringToHGlobalAnsi", "call GetProcAddress"}));
  else
    EXPECT_EQ(Calls, (Lines{"call Marshal_StringToHGlobalAnsi", "call dlopen",
                            "call Marshal_StringToHGlobalAnsi", "call dlsym"}));
  const RecordingSink::Method *Init = S.findMethod("<msil>init_dataimports");
  ASSERT_NE(Init, nullptr);
  EXPECT_TRUE(isVoid(Init->Sig.Return));
  EXPECT_TRUE(Init->Sig.Params.empty());
  // .cctor calls the initializer, never the helper itself.
  Lines Cctor = S.render(".cctor");
  EXPECT_EQ(count(Cctor, "call <msil>init_dataimports"), 1);
  EXPECT_EQ(count(Cctor, "call <msil>dataimport"), 0);
  EXPECT_EQ(count(S.render(*Init), "call <msil>dataimport"), 3);
}

TEST(CLITrailerTest, DataImportsKernel32) {
  ImportProgram P;
  P.make(DataImportHelper::Kind::Kernel32);
  P.build();
  std::vector<std::string> Names = P.Sink.methodNames();
  for (const char *N : {"<msil>dataimport", "<msil>init_dataimports", ".cctor"})
    EXPECT_EQ(count(Names, N), 1) << N;
  checkHelperShape(P.Sink, /*Win=*/true);
  Interp VM(P.Sink);
  P.install(VM, /*Win=*/true);
  runAndCheckData(
      P, VM,
      {{P.Iob, 0x55550000}, {P.Fmode, 0x55550100}, {P.GData, 0x66660000}});
  // The data relocations ran after the imports and before the constructor.
  EXPECT_EQ(P.AtCtor, (std::vector<uint64_t>{0x55550000 + 48, 0x66660000}));
}

TEST(CLITrailerTest, DataImportsDlfcn) {
  ImportProgram P;
  P.make(DataImportHelper::Kind::Dlfcn);
  P.build();
  checkHelperShape(P.Sink, /*Win=*/false);
  Interp VM(P.Sink);
  P.install(VM, /*Win=*/false);
  runAndCheckData(
      P, VM,
      {{P.Iob, 0x57570000}, {P.Fmode, 0x57570100}, {P.GData, 0x68680000}});
  EXPECT_EQ(P.AtCtor, (std::vector<uint64_t>{0x57570000 + 48, 0x68680000}));
}

TEST(CLITrailerTest, MissingDataImportThrows) {
  for (bool Win : {true, false}) {
    ImportProgram P;
    P.make(Win ? DataImportHelper::Kind::Kernel32
               : DataImportHelper::Kind::Dlfcn);
    P.build();
    Interp VM(P.Sink);
    P.install(VM, Win, "_fmode");
    Interp::Outcome O = VM.run(".cctor");
    EXPECT_EQ(VM.Failure, "");
    EXPECT_TRUE(O.Threw);
    EXPECT_EQ(O.Message, Win ? "data import '_fmode' not found in msvcrt.dll"
                             : "data import '_fmode' not found in libc.so.6");
    EXPECT_TRUE(P.AtCtor.empty()) << "a constructor ran after the failure";
  }
}

TEST(CLITrailerTest, DataImportsInLoopMode) {
  // Import fields are relocation targets without an image offset: kinds 2
  // and 3 through slots filled after '<msil>init_dataimports'.
  for (bool Win : {true, false}) {
    ImportProgram P;
    P.make(Win ? DataImportHelper::Kind::Kernel32
               : DataImportHelper::Kind::Dlfcn,
           /*NumSlots=*/2, /*Threshold=*/0);
    P.build();
    std::vector<TableEntry> T = decodeTable(P.Sink);
    ASSERT_EQ(T.size(), 2u);
    EXPECT_EQ(T[0].Site, 0u);
    EXPECT_EQ(T[0].Kind, 2u);
    EXPECT_EQ(T[1].Site, 8u);
    EXPECT_EQ(T[1].Kind, 3u);
    EXPECT_NE(T[0].Value, T[1].Value);
    for (int64_t V : {T[0].Value, T[1].Value})
      EXPECT_TRUE(V == 16 || V == 24) << V;
    Interp VM(P.Sink);
    P.install(VM, Win);
    uint64_t Iob = Win ? 0x55550000 : 0x57570000;
    uint64_t Fmode = Win ? 0x55550100 : 0x57570100;
    uint64_t GData = Win ? 0x66660000 : 0x68680000;
    runAndCheckData(P, VM, {{P.Iob, Iob}, {P.Fmode, Fmode}, {P.GData, GData}});
    EXPECT_EQ(P.AtCtor, (std::vector<uint64_t>{Iob + 48, GData}));
  }
}

} // namespace
