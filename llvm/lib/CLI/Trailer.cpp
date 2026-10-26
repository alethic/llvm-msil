//===- Trailer.cpp - MSIL module trailer builder --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file builds the synthesized methods of an MSIL output
// (ImplementationPlan.md 5.9.10, see llvm/CLI/Trailer.h). Below the
// relocation-loop threshold the module initializer and the entry point are
// the methods that the MSIL AsmPrinter wrote in Phase 1, instruction for
// instruction (with M18's shortest forms), so -S text does not change.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/Trailer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/ImageLayout.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <map>

using namespace llvm;
using namespace llvm::cli;

#define DEBUG_TYPE "cli-trailer"

TrailerSink::~TrailerSink() = default;

/// The width of a pointer (decision D6: Phase 2's triples are 64-bit), the
/// natural alignment of stind.i and of the zero part's initblk, as Phase 1
/// wrote them.
constexpr uint64_t PointerSize = 8;

/// The size of one '<msil>relocs' entry: {u32 Site; u32 Kind; i64 Value}.
constexpr uint64_t RelocEntrySize = 16;

namespace {
/// The kinds of a '<msil>relocs' entry (5.9.10): bit 1 loads the pointer
/// stored at base + Value (an address slot), bit 0 truncates to 4 bytes.
enum RelocKind : uint32_t {
  StoreAddress = 0,
  StoreAddress32 = 1,
  StoreSlot = 2,
  StoreSlot32 = 3,
};
} // end anonymous namespace

static TokenRef tok(EntityRef E) {
  assert(E != 0 && "no entity");
  return TokenRef::getExternal(E);
}

/// Pushes \p Value as a native int (decision D2), as code does for
/// CONST_I64: the shortest 'ldc.i4' form or 'ldc.i8', then 'conv.i'.
static void emitNativeInt(ILBuilder &B, int64_t Value) {
  if (isInt<32>(Value))
    B.emitLdcI4(static_cast<int32_t>(Value));
  else
    B.emitImm(Opcode::Ldc_i8, Value);
  B.emit(Opcode::Conv_i);
}

/// Adds \p Offset to the native int on top of the stack, exactly as code
/// does after the 'ldsfld' of GLOBAL_ADDR; nothing for 0.
static void emitOffset(ILBuilder &B, int64_t Offset) {
  if (Offset == 0)
    return;
  if (isInt<32>(Offset)) {
    B.emitLdcI4(static_cast<int32_t>(Offset));
  } else {
    B.emitImm(Opcode::Ldc_i8, Offset);
    B.emit(Opcode::Conv_i);
  }
  B.emit(Opcode::Add);
}

/// A method of the trailer: \p Name with \p Flags and \p Sig.
static TrailerMethod makeMethod(StringRef Name, MethodAttributes Flags,
                                MethodSig Sig) {
  bool ReturnsValue = Sig.Return.getElementType() != ELEMENT_TYPE_VOID;
  TrailerMethod M{
      Name.str(), Flags, std::move(Sig), {}, ILBuilder(ReturnsValue), {}};
  return M;
}

/// A new label of \p M, printed as \p Name by a text sink.
static ILLabel createLabel(TrailerMethod &M, StringRef Name) {
  ILLabel L = M.Body.createLabel();
  assert(L.Id == M.LabelNames.size() && "labels are numbered densely");
  M.LabelNames.push_back(Name.str());
  return L;
}

/// Lays \p M out and hands it to \p Sink. The trailer's bodies are fixed
/// shapes, so a layout error is a bug of this file.
static void finish(TrailerMethod &M, TrailerSink &Sink) {
  if (Error E = M.Body.layout())
    reportFatalInternalError(Twine("MSIL trailer: method '") + M.Name +
                             "': " + toString(std::move(E)));
  Sink.emitMethod(M);
}

static MethodSig getVoidSig() { return MethodSig(); }

/// '<msil>dataimport'(string module, string name) : native int, shaped as
/// recon/p2/native-imports-files/data-win.il: the native library's handle,
/// then the export's address (0 when either is missing).
static void buildDataImportHelper(const DataImportHelper &H,
                                  const MethodSig &Sig, TrailerSink &Sink) {
  TrailerMethod M =
      makeMethod(DataImportHelperName,
                 MethodAttributes::Private | MethodAttributes::HideBySig |
                     MethodAttributes::Static,
                 Sig);
  ILBuilder &B = M.Body;
  B.emitLdarg(0);
  if (H.K == DataImportHelper::Kind::Kernel32) {
    // LoadLibraryW takes the module name in UTF-16.
    B.emitCall(Opcode::Call,
               tok(Sink.getBCLMember(BCLMemberId::Marshal_StringToHGlobalUni)),
               1, 1);
    B.emitCall(Opcode::Call, tok(H.Load), 1, 1);
  } else {
    // dlopen(path, RTLD_LAZY): the path in the native narrow encoding.
    B.emitCall(Opcode::Call,
               tok(Sink.getBCLMember(BCLMemberId::Marshal_StringToHGlobalAnsi)),
               1, 1);
    B.emitLdcI4(1);
    B.emitCall(Opcode::Call, tok(H.Load), 2, 1);
  }
  // GetProcAddress and dlsym take the export's name in ANSI. The strings
  // live as long as the program, like argv's (D27).
  B.emitLdarg(1);
  B.emitCall(Opcode::Call,
             tok(Sink.getBCLMember(BCLMemberId::Marshal_StringToHGlobalAnsi)),
             1, 1);
  B.emitCall(Opcode::Call, tok(H.GetSymbol), 2, 1);
  B.emitRet();
  finish(M, Sink);
}

/// '<msil>init_dataimports'(): stores the address of every data import in
/// its field, and throws EntryPointNotFoundException("data import '<entry>'
/// not found in <module>") for a missing one (5.10.12). A separate method,
/// never inlined into .cctor (FeatureIdeas 7).
static void buildInitDataImports(ArrayRef<DataImport> Imports, EntityRef Helper,
                                 TrailerSink &Sink) {
  TrailerMethod M =
      makeMethod(InitDataImportsName,
                 MethodAttributes::Private | MethodAttributes::HideBySig |
                     MethodAttributes::Static,
                 getVoidSig());
  ILBuilder &B = M.Body;
  EntityRef Ctor =
      Sink.getBCLMember(BCLMemberId::EntryPointNotFoundException_ctor);
  for (const DataImport &I : Imports) {
    ILLabel Found = createLabel(M, "dataimport_ok");
    B.emitToken(Opcode::Ldstr, tok(Sink.addUserString(I.Module)));
    B.emitToken(Opcode::Ldstr, tok(Sink.addUserString(I.Entry)));
    B.emitCall(Opcode::Call, tok(Helper), 2, 1);
    B.emit(Opcode::Dup);
    B.emitBranch(Opcode::Brtrue, Found);
    B.emitToken(Opcode::Ldstr,
                tok(Sink.addUserString("data import '" + I.Entry +
                                       "' not found in " + I.Module)));
    B.emitCall(Opcode::Newobj, tok(Ctor), 1, 1);
    B.emit(Opcode::Throw);
    B.placeLabel(Found);
    B.emitToken(Opcode::Stsfld, tok(I.Field));
  }
  B.emitRet();
  finish(M, Sink);
}

/// The relocation loop's address slots (5.9.9 rule 4): one per distinct
/// (Field, Addend) pair of the relocations without an ImageOffset, numbered
/// in first-use order.
using SlotMap = std::map<std::pair<EntityRef, int64_t>, unsigned>;
static SmallVector<std::pair<EntityRef, int64_t>, 0>
collectSlots(ArrayRef<RuntimeReloc> Relocs, SlotMap &Slots) {
  SmallVector<std::pair<EntityRef, int64_t>, 0> Order;
  for (const RuntimeReloc &R : Relocs) {
    if (R.ImageOffset)
      continue;
    assert(R.Field && "a relocation names a field or an image offset");
    auto [It, Inserted] = Slots.try_emplace({R.Field, R.Addend}, Order.size());
    if (Inserted)
      Order.push_back(It->first);
  }
  return Order;
}

/// <Module>::.cctor (5.9.10).
static void buildModuleInitializer(const TrailerInput &In,
                                   EntityRef InitImports, TrailerSink &Sink) {
  TrailerMethod M =
      makeMethod(ModuleInitializerName,
                 MethodAttributes::Private | MethodAttributes::HideBySig |
                     MethodAttributes::SpecialName |
                     MethodAttributes::RTSpecialName | MethodAttributes::Static,
                 getVoidSig());
  ILBuilder &B = M.Body;

  // The address field of every function whose address is used, which code
  // (FUNC_ADDR) and the relocations read: the output's only 'ldftn' of each
  // function, so every pointer to it has one value. On .NET Framework, the
  // 'ldftn' of a method that the JIT compiles before the target first ran
  // yields the target's precode, and in one compiled after it its code (M9).
  auto AddFunctionAddresses = [&] {
    for (const auto &[Field, Method] : In.FnAddrFields) {
      B.emitToken(Opcode::Ldftn, tok(Method));
      B.emitToken(Opcode::Stsfld, tok(Field));
    }
    // The native data before the relocations, which may store its address,
    // and before the constructors, which may read it (5.10.12).
    if (InitImports)
      B.emitCall(Opcode::Call, tok(InitImports), 0, 0);
  };

  if (const ImageLayout *L = In.Image) {
    EntityRef Base = Sink.getDataField();
    uint64_t InitSize = L->InitSize;
    uint64_t ZeroSize = L->TotalSize - InitSize;
    Align MaxAlign(uint64_t(1) << L->MaxAlignLog2);
    int64_t Alignment = MaxAlign.value();
    // 'unaligned. N' before an access to Base + Offset that may be less
    // aligned than the instruction assumes (Natural), with N the largest of
    // 1, 2 and 4 that the address guarantees, as MSILSetMemFlags decides it
    // for code (decision D24, ECMA-335 III.2.5). Base is aligned to MaxAlign.
    auto AddUnaligned = [&](uint64_t Offset, uint64_t Natural) {
      uint64_t A = commonAlignment(MaxAlign, Offset).value();
      if (A < Natural)
        B.emitImm(Opcode::Unaligned, std::min<uint64_t>(A, 4));
    };

    // Base = (AllocHGlobal(TotalSize + MaxAlign - 1) + MaxAlign - 1) &
    // -MaxAlign. AllocHGlobal only guarantees malloc's alignment, and its
    // memory is neither zeroed nor freed.
    emitNativeInt(B, L->TotalSize + Alignment - 1);
    B.emitCall(Opcode::Call,
               tok(Sink.getBCLMember(BCLMemberId::Marshal_AllocHGlobal)), 1, 1);
    emitNativeInt(B, Alignment - 1);
    B.emit(Opcode::Add);
    emitNativeInt(B, -Alignment);
    B.emit(Opcode::And);
    B.emitToken(Opcode::Stsfld, tok(Base));

    // Copy the initialized part from the RVA image. Sizes are int32
    // constants: cpblk and initblk take an unsigned int32 size.
    if (InitSize != 0) {
      EntityRef Image = Sink.addRVAData(ImageTypeName, ImageFieldName,
                                        ArrayRef<uint8_t>(L->Image));
      B.emitToken(Opcode::Ldsfld, tok(Base));
      B.emitToken(Opcode::Ldsflda, tok(Image));
      B.emitLdcI4(static_cast<int32_t>(InitSize));
      B.emit(Opcode::Cpblk);
    }
    // AllocHGlobal does not zero its memory.
    if (ZeroSize != 0) {
      B.emitToken(Opcode::Ldsfld, tok(Base));
      emitOffset(B, InitSize);
      B.emitLdcI4(0);
      B.emitLdcI4(static_cast<int32_t>(ZeroSize));
      AddUnaligned(InitSize, PointerSize);
      B.emit(Opcode::Initblk);
    }

    // The address field of every global, which code reads (GLOBAL_ADDR).
    for (const auto &[Field, Offset] : In.AddressFields) {
      B.emitToken(Opcode::Ldsfld, tok(Base));
      emitOffset(B, Offset);
      B.emitToken(Opcode::Stsfld, tok(Field));
    }
    AddFunctionAddresses();

    if (In.Relocs.size() <= In.RelocLoopThreshold) {
      // Straight-line relocations, now that every address is known: the
      // value of the target's field plus the addend, or the image base plus
      // the target's offset and the addend.
      for (const RuntimeReloc &R : In.Relocs) {
        assert(R.Site < InitSize && "relocation sites lie in the image");
        B.emitToken(Opcode::Ldsfld, tok(Base));
        emitOffset(B, R.Site);
        if (R.Field) {
          B.emitToken(Opcode::Ldsfld, tok(R.Field));
          emitOffset(B, R.Addend);
        } else {
          assert(R.ImageOffset && "a relocation names a field or an offset");
          B.emitToken(Opcode::Ldsfld, tok(Base));
          emitOffset(B, *R.ImageOffset + R.Addend);
        }
        if (R.Width == 4) {
          B.emit(Opcode::Conv_i4);
          AddUnaligned(R.Site, 4);
          B.emit(Opcode::Stind_i4);
        } else {
          assert(R.Width == 8 && "relocations hold 4 or 8 bytes");
          AddUnaligned(R.Site, PointerSize);
          B.emit(Opcode::Stind_i);
        }
      }
    } else {
      // Loop mode (Design.md risk 1: a long straight-line .cctor drops the
      // JIT into MinOpts). The address slots first, from the fields (one
      // 'ldftn' per function stays in the code above): 8-aligned in a base
      // aligned to at least 16, so no 'unaligned.' prefix.
      SlotMap Slots;
      SmallVector<std::pair<EntityRef, int64_t>, 0> SlotOrder =
          collectSlots(In.Relocs, Slots);
      assert((SlotOrder.empty() ||
              (L->SlotsOffset % 8 == 0 &&
               L->SlotsOffset + 8 * SlotOrder.size() <= L->TotalSize)) &&
             "the layout lacks the address slots");
      for (size_t K = 0, E = SlotOrder.size(); K != E; ++K) {
        B.emitToken(Opcode::Ldsfld, tok(Base));
        emitOffset(B, L->SlotsOffset + 8 * K);
        B.emitToken(Opcode::Ldsfld, tok(SlotOrder[K].first));
        emitOffset(B, SlotOrder[K].second);
        B.emit(Opcode::Stind_i);
      }

      // The table: {u32 Site; u32 Kind; i64 Value} per relocation, in site
      // order.
      std::vector<uint8_t> Table(In.Relocs.size() * RelocEntrySize);
      uint8_t *P = Table.data();
      for (const RuntimeReloc &R : In.Relocs) {
        assert(R.Site < InitSize && isUInt<32>(R.Site) &&
               "relocation sites lie in the image");
        assert((R.Width == 4 || R.Width == 8) && "relocations hold 4 or 8 "
                                                 "bytes");
        uint32_t Kind;
        int64_t Value;
        if (R.ImageOffset) {
          Kind = R.Width == 8 ? StoreAddress : StoreAddress32;
          Value = static_cast<int64_t>(*R.ImageOffset) + R.Addend;
        } else {
          Kind = R.Width == 8 ? StoreSlot : StoreSlot32;
          Value = L->SlotsOffset + 8 * Slots.at({R.Field, R.Addend});
        }
        support::endian::write32le(P, static_cast<uint32_t>(R.Site));
        support::endian::write32le(P + 4, Kind);
        support::endian::write64le(P + 8, static_cast<uint64_t>(Value));
        P += RelocEntrySize;
      }
      EntityRef Relocs = Sink.addRVAData(RelocsTypeName, RelocsFieldName,
                                         ArrayRef<uint8_t>(Table));
      uint64_t TableSize = Table.size();
      assert(isInt<32>(TableSize) && "relocation table above 2 GiB");

      // for (i = 0; i < size; i += 16) {
      //   dst = base + table[i].Site;
      //   v = base + table[i].Value;
      //   if (table[i].Kind & 2) v = *(native int *)v;
      //   if (table[i].Kind & 1) *(int32 *)dst = (int32)v;  // unaligned. 1
      //   else *(native int *)dst = v;                      // unaligned. 1
      // }
      // Locals: 0 = i (int32), 1 = Kind (int32). An entry field is read at
      // the managed pointer '<msil>relocs' + i + offset.
      M.Locals = {TypeSig::get(ELEMENT_TYPE_I4), TypeSig::get(ELEMENT_TYPE_I4)};
      ILLabel Loop = createLabel(M, "relocs_loop");
      ILLabel Cond = createLabel(M, "relocs_cond");
      ILLabel Value = createLabel(M, "relocs_value");
      ILLabel Narrow = createLabel(M, "relocs_narrow");
      ILLabel Next = createLabel(M, "relocs_next");
      auto EntryField = [&](int32_t Offset) {
        B.emitToken(Opcode::Ldsflda, tok(Relocs));
        B.emitLdloc(0);
        B.emit(Opcode::Add);
        if (Offset != 0) {
          B.emitLdcI4(Offset);
          B.emit(Opcode::Add);
        }
      };
      B.emitLdcI4(0);
      B.emitStloc(0);
      B.emitBranch(Opcode::Br, Cond);
      B.placeLabel(Loop);
      B.emitToken(Opcode::Ldsfld, tok(Base));
      EntryField(0);
      B.emit(Opcode::Ldind_i4);
      B.emit(Opcode::Add);
      B.emitToken(Opcode::Ldsfld, tok(Base));
      EntryField(8);
      B.emit(Opcode::Ldind_i);
      B.emit(Opcode::Add);
      EntryField(4);
      B.emit(Opcode::Ldind_i4);
      B.emitStloc(1);
      B.emitLdloc(1);
      B.emitLdcI4(StoreSlot);
      B.emit(Opcode::And);
      B.emitBranch(Opcode::Brfalse, Value);
      B.emit(Opcode::Ldind_i);
      B.placeLabel(Value);
      B.emitLdloc(1);
      B.emitLdcI4(StoreAddress32);
      B.emit(Opcode::And);
      B.emitBranch(Opcode::Brtrue, Narrow);
      B.emitImm(Opcode::Unaligned, 1);
      B.emit(Opcode::Stind_i);
      B.emitBranch(Opcode::Br, Next);
      B.placeLabel(Narrow);
      B.emit(Opcode::Conv_i4);
      B.emitImm(Opcode::Unaligned, 1);
      B.emit(Opcode::Stind_i4);
      B.placeLabel(Next);
      B.emitLdloc(0);
      B.emitLdcI4(static_cast<int32_t>(RelocEntrySize));
      B.emit(Opcode::Add);
      B.emitStloc(0);
      B.placeLabel(Cond);
      B.emitLdloc(0);
      B.emitLdcI4(static_cast<int32_t>(TableSize));
      B.emitBranch(Opcode::Blt, Loop);
    }
  } else {
    assert(In.Relocs.empty() && In.AddressFields.empty() &&
           "relocations and address fields need an image");
    // Without a data segment, the function addresses come first.
    AddFunctionAddresses();
  }

  // The constructors last, in run order: the data they may touch is in
  // place. Each is called directly; a result is popped.
  for (const auto &[Method, ReturnsValue] : In.Ctors) {
    B.emitCall(Opcode::Call, tok(Method), 0, ReturnsValue ? 1 : 0);
    if (ReturnsValue)
      B.emit(Opcode::Pop);
  }
  B.emitRet();
  finish(M, Sink);
}

std::optional<bool> cli::getEntryPointHasArgs(const MethodSig &Sig,
                                              bool IsVarArg,
                                              unsigned PointerSize) {
  TypeSig Int32 = TypeSig::get(ELEMENT_TYPE_I4);
  // A pointer is a native int on 64-bit triples (decision D2).
  TypeSig Pointer =
      TypeSig::get(PointerSize == 8 ? ELEMENT_TYPE_I : ELEMENT_TYPE_I4);
  if (Sig.Header != SIG_DEFAULT || Sig.Return != Int32 || IsVarArg ||
      Sig.RequiredParamCount < Sig.Params.size())
    return std::nullopt;
  if (Sig.Params.empty())
    return false;
  if (Sig.Params.size() == 2 && Sig.Params[0] == Int32 &&
      Sig.Params[1] == Pointer)
    return true;
  return std::nullopt;
}

/// '<msil>Main'(string[] args), the .entrypoint method that calls the C
/// function main (decision D27).
static void buildEntryPoint(const EntryPoint &E, TrailerSink &Sink) {
  MethodSig Sig;
  Sig.Return = TypeSig::get(ELEMENT_TYPE_I4);
  Sig.Params = {TypeSig::getSZArray(TypeSig::get(ELEMENT_TYPE_STRING))};
  TrailerMethod M =
      makeMethod(EntryPointName,
                 MethodAttributes::Private | MethodAttributes::HideBySig |
                     MethodAttributes::Static,
                 std::move(Sig));
  ILBuilder &B = M.Body;
  // A pointer is a native int on 64-bit triples (decision D2).
  TypeSig PointerType =
      TypeSig::get(E.PointerSize == 8 ? ELEMENT_TYPE_I : ELEMENT_TYPE_I4);
  if (E.HasArgs) {
    // argc and a NULL-terminated argv as a native main gets them:
    // Environment.GetCommandLineArgs() starts with the program (the .dll),
    // and Marshal.StringToHGlobalAnsi converts to the native narrow encoding
    // (UTF-8 on Unix, the ANSI code page on Windows). The strings and the
    // array are never freed: they live as long as the program.
    // Locals: 0 = the string[], 1 = argc, 2 = argv, 3 = i.
    M.Locals = {TypeSig::getSZArray(TypeSig::get(ELEMENT_TYPE_STRING)),
                TypeSig::get(ELEMENT_TYPE_I4), PointerType,
                TypeSig::get(ELEMENT_TYPE_I4)};
    ILLabel Loop = createLabel(M, "argv_loop");
    ILLabel Cond = createLabel(M, "argv_cond");
    // Pushes the address of argv[<IndexLocal>].
    auto AddArgvSlotAddress = [&](unsigned IndexLocal) {
      B.emitLdloc(2);
      B.emitLdloc(IndexLocal);
      B.emit(Opcode::Conv_i);
      B.emitLdcI4(E.PointerSize);
      B.emit(Opcode::Mul);
      B.emit(Opcode::Add);
    };

    B.emitCall(
        Opcode::Call,
        tok(Sink.getBCLMember(BCLMemberId::Environment_GetCommandLineArgs)), 0,
        1);
    B.emitStloc(0);
    // argc = args.Length
    B.emitLdloc(0);
    B.emit(Opcode::Ldlen);
    B.emit(Opcode::Conv_i4);
    B.emitStloc(1);
    // argv = AllocHGlobal((argc + 1) * sizeof(char *))
    B.emitLdloc(1);
    B.emitLdcI4(1);
    B.emit(Opcode::Add);
    B.emit(Opcode::Conv_i);
    B.emitLdcI4(E.PointerSize);
    B.emit(Opcode::Mul);
    B.emitCall(Opcode::Call,
               tok(Sink.getBCLMember(BCLMemberId::Marshal_AllocHGlobal)), 1, 1);
    B.emitStloc(2);
    // for (i = 0; i < argc; ++i) argv[i] = StringToHGlobalAnsi(args[i])
    B.emitLdcI4(0);
    B.emitStloc(3);
    B.emitBranch(Opcode::Br, Cond);
    B.placeLabel(Loop);
    AddArgvSlotAddress(3);
    B.emitLdloc(0);
    B.emitLdloc(3);
    B.emit(Opcode::Ldelem_ref);
    B.emitCall(Opcode::Call,
               tok(Sink.getBCLMember(BCLMemberId::Marshal_StringToHGlobalAnsi)),
               1, 1);
    B.emit(Opcode::Stind_i);
    B.emitLdloc(3);
    B.emitLdcI4(1);
    B.emit(Opcode::Add);
    B.emitStloc(3);
    B.placeLabel(Cond);
    B.emitLdloc(3);
    B.emitLdloc(1);
    B.emitBranch(Opcode::Blt, Loop);
    // argv[argc] = NULL
    AddArgvSlotAddress(1);
    B.emitLdcI4(0);
    B.emit(Opcode::Conv_i);
    B.emit(Opcode::Stind_i);
    // return main(argc, argv)
    B.emitLdloc(1);
    B.emitLdloc(2);
  }
  B.emitCall(Opcode::Call, tok(E.Main), E.HasArgs ? 2 : 0, 1);
  B.emitRet();
  finish(M, Sink);
}

unsigned cli::countAddressSlots(ArrayRef<RuntimeReloc> Relocs,
                                unsigned RelocLoopThreshold) {
  if (Relocs.size() <= RelocLoopThreshold)
    return 0;
  SlotMap Slots;
  return collectSlots(Relocs, Slots).size();
}

void cli::buildTrailer(const TrailerInput &In, TrailerSink &Sink) {
  EntityRef InitImports = 0;
  if (!In.DataImports.empty()) {
    assert(In.ImportHelper && "data imports need a helper");
    MethodSig HelperSig;
    HelperSig.Return = TypeSig::get(ELEMENT_TYPE_I);
    HelperSig.Params = {TypeSig::get(ELEMENT_TYPE_STRING),
                        TypeSig::get(ELEMENT_TYPE_STRING)};
    EntityRef Helper = Sink.declareHelper(DataImportHelperName, HelperSig);
    InitImports = Sink.declareHelper(InitDataImportsName, getVoidSig());
    buildDataImportHelper(*In.ImportHelper, HelperSig, Sink);
    buildInitDataImports(In.DataImports, Helper, Sink);
  }
  if (In.Image || !In.FnAddrFields.empty() || InitImports || !In.Ctors.empty())
    buildModuleInitializer(In, InitImports, Sink);
  if (In.Entry)
    buildEntryPoint(*In.Entry, Sink);
}
