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

EntityRef TrailerSink::addTrailerField(StringRef Name) {
  reportFatalInternalError("MSIL trailer: this sink cannot make the field '" +
                           Name + "'");
}

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

/// 'unaligned. N' before an access to Base + \p Offset that may be less
/// aligned than the instruction assumes (\p Natural), with N the largest of
/// 1, 2 and 4 that the address guarantees, as MSILSetMemFlags decides it for
/// code (decision D24, ECMA-335 III.2.5). Base is aligned to \p MaxAlign.
static void emitUnaligned(ILBuilder &B, Align MaxAlign, uint64_t Offset,
                          uint64_t Natural) {
  uint64_t A = commonAlignment(MaxAlign, Offset).value();
  if (A < Natural)
    B.emitImm(Opcode::Unaligned, std::min<uint64_t>(A, 4));
}

/// A method of the trailer: \p Name with \p Flags and \p Sig, held by
/// \p Owner.
static TrailerMethod makeMethod(StringRef Name, MethodAttributes Flags,
                                MethodSig Sig,
                                TrailerOwner Owner = TrailerOwner::Module) {
  bool ReturnsValue = Sig.Return.getElementType() != ELEMENT_TYPE_VOID;
  TrailerMethod M{
      Name.str(), Flags, std::move(Sig), {}, ILBuilder(ReturnsValue), {}};
  M.Owner = Owner;
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
/// then the export's address (0 when either is missing, except that
/// NativeLibrary's members throw instead).
static void buildDataImportHelper(const DataImportHelper &H,
                                  const MethodSig &Sig, TrailerOwner Owner,
                                  TrailerSink &Sink) {
  TrailerMethod M =
      makeMethod(DataImportHelperName,
                 MethodAttributes::Private | MethodAttributes::HideBySig |
                     MethodAttributes::Static,
                 Sig, Owner);
  ILBuilder &B = M.Body;
  B.emitLdarg(0);
  switch (H.K) {
  case DataImportHelper::Kind::NativeLibrary:
    // NativeLibrary::Load(string) and GetExport(native int, string) take the
    // names as strings (M21).
    B.emitCall(Opcode::Call, tok(H.Load), 1, 1);
    B.emitLdarg(1);
    B.emitCall(Opcode::Call, tok(H.GetSymbol), 2, 1);
    B.emitRet();
    finish(M, Sink);
    return;
  case DataImportHelper::Kind::Kernel32:
    // LoadLibraryW takes the module name in UTF-16.
    B.emitCall(Opcode::Call,
               tok(Sink.getBCLMember(BCLMemberId::Marshal_StringToHGlobalUni)),
               1, 1);
    B.emitCall(Opcode::Call, tok(H.Load), 1, 1);
    break;
  case DataImportHelper::Kind::Dlfcn:
    // dlopen(path, RTLD_LAZY): the path in the native narrow encoding.
    B.emitCall(Opcode::Call,
               tok(Sink.getBCLMember(BCLMemberId::Marshal_StringToHGlobalAnsi)),
               1, 1);
    B.emitLdcI4(1);
    B.emitCall(Opcode::Call, tok(H.Load), 2, 1);
    break;
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
                                 TrailerOwner Owner, TrailerSink &Sink) {
  TrailerMethod M =
      makeMethod(InitDataImportsName,
                 MethodAttributes::Private | MethodAttributes::HideBySig |
                     MethodAttributes::Static,
                 getVoidSig(), Owner);
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

/// A type initializer, <Module>::.cctor or '<msil>Data'::.cctor (5.9.10).
static TrailerMethod makeTypeInitializer(TrailerOwner Owner) {
  return makeMethod(ModuleInitializerName,
                    MethodAttributes::Private | MethodAttributes::HideBySig |
                        MethodAttributes::SpecialName |
                        MethodAttributes::RTSpecialName |
                        MethodAttributes::Static,
                    getVoidSig(), Owner);
}

/// Appends argc and a NULL-terminated argv, as a native main gets them, to
/// \p M, in four new locals from \p FirstLocal on: the string[] of
/// Environment.GetCommandLineArgs(), argc, argv (\p PointerBytes-byte
/// pointers) and a counter. The arguments start with the program (the .dll),
/// and Marshal.StringToHGlobalAnsi converts them to the native narrow
/// encoding (UTF-8 on Unix, the ANSI code page on Windows). The strings and
/// the array are never freed: they live as long as the program. Its labels
/// are '<LabelPrefix>_loop' and '<LabelPrefix>_cond'.
static void emitArgv(TrailerMethod &M, unsigned FirstLocal,
                     unsigned PointerBytes, StringRef LabelPrefix,
                     TrailerSink &Sink) {
  ILBuilder &B = M.Body;
  // A pointer is a native int on 64-bit triples (decision D2).
  TypeSig PointerType =
      TypeSig::get(PointerBytes == 8 ? ELEMENT_TYPE_I : ELEMENT_TYPE_I4);
  assert(M.Locals.size() == FirstLocal && "the locals are appended");
  M.Locals.push_back(TypeSig::getSZArray(TypeSig::get(ELEMENT_TYPE_STRING)));
  M.Locals.push_back(TypeSig::get(ELEMENT_TYPE_I4));
  M.Locals.push_back(PointerType);
  M.Locals.push_back(TypeSig::get(ELEMENT_TYPE_I4));
  unsigned Args = FirstLocal, Argc = FirstLocal + 1, Argv = FirstLocal + 2,
           I = FirstLocal + 3;
  ILLabel Loop = createLabel(M, (LabelPrefix + "_loop").str());
  ILLabel Cond = createLabel(M, (LabelPrefix + "_cond").str());
  // Pushes the address of argv[<IndexLocal>].
  auto AddArgvSlotAddress = [&](unsigned IndexLocal) {
    B.emitLdloc(Argv);
    B.emitLdloc(IndexLocal);
    B.emit(Opcode::Conv_i);
    B.emitLdcI4(PointerBytes);
    B.emit(Opcode::Mul);
    B.emit(Opcode::Add);
  };

  B.emitCall(
      Opcode::Call,
      tok(Sink.getBCLMember(BCLMemberId::Environment_GetCommandLineArgs)), 0,
      1);
  B.emitStloc(Args);
  // argc = args.Length
  B.emitLdloc(Args);
  B.emit(Opcode::Ldlen);
  B.emit(Opcode::Conv_i4);
  B.emitStloc(Argc);
  // argv = AllocHGlobal((argc + 1) * sizeof(char *))
  B.emitLdloc(Argc);
  B.emitLdcI4(1);
  B.emit(Opcode::Add);
  B.emit(Opcode::Conv_i);
  B.emitLdcI4(PointerBytes);
  B.emit(Opcode::Mul);
  B.emitCall(Opcode::Call,
             tok(Sink.getBCLMember(BCLMemberId::Marshal_AllocHGlobal)), 1, 1);
  B.emitStloc(Argv);
  // for (i = 0; i < argc; ++i) argv[i] = StringToHGlobalAnsi(args[i])
  B.emitLdcI4(0);
  B.emitStloc(I);
  B.emitBranch(Opcode::Br, Cond);
  B.placeLabel(Loop);
  AddArgvSlotAddress(I);
  B.emitLdloc(Args);
  B.emitLdloc(I);
  B.emit(Opcode::Ldelem_ref);
  B.emitCall(Opcode::Call,
             tok(Sink.getBCLMember(BCLMemberId::Marshal_StringToHGlobalAnsi)),
             1, 1);
  B.emit(Opcode::Stind_i);
  B.emitLdloc(I);
  B.emitLdcI4(1);
  B.emit(Opcode::Add);
  B.emitStloc(I);
  B.placeLabel(Cond);
  B.emitLdloc(I);
  B.emitLdloc(Argc);
  B.emitBranch(Opcode::Blt, Loop);
  // argv[argc] = NULL
  AddArgvSlotAddress(Argc);
  B.emitLdcI4(0);
  B.emit(Opcode::Conv_i);
  B.emit(Opcode::Stind_i);
}

/// Phase 1 of the initialization (5.9.10), appended to \p M: the image, the
/// address fields, the function addresses, the data imports and the
/// relocations within the assembly.
static void emitPhase1(const TrailerInput &In, EntityRef InitImports,
                       TrailerMethod &M, TrailerSink &Sink) {
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
    auto AddUnaligned = [&](uint64_t Offset, uint64_t Natural) {
      emitUnaligned(B, MaxAlign, Offset, Natural);
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
}

namespace {
/// The fields '<msil>argc' and '<msil>argv' through which <Module>::.cctor
/// hands '<msil>Main' the argc and argv that it built for the constructors
/// (TrailerInput::CtorArgs); 0 when '<msil>Main' builds its own.
struct SharedArgs {
  EntityRef Argc = 0, Argv = 0;
};
} // end anonymous namespace

/// Phase 2 of the initialization (5.9.10, 5.9.15), appended to \p M: the
/// libraries, the relocations against their exports and the constructors.
/// With \p Shared, argc and argv are stored in its fields once they are
/// built.
static void emitPhase2(const TrailerInput &In, TrailerMethod &M,
                       TrailerSink &Sink, SharedArgs *Shared) {
  ILBuilder &B = M.Body;

  // Every linked library initializes before this assembly's constructors
  // and main: the JIT runs a library's module initializer when it resolves
  // a token of the library (exports-multiasm 6.1, 6.3).
  for (EntityRef Type : In.LibraryTypes) {
    B.emitToken(Opcode::Ldtoken, tok(Type));
    B.emit(Opcode::Pop);
  }

  // The relocations against library exports, straight-line: the accessor's
  // value plus the addend, at '<msil>data' plus the site.
  if (!In.CrossRelocs.empty()) {
    const ImageLayout *L = In.Image;
    assert(L && "relocations need an image");
    EntityRef Base = Sink.getDataField();
    Align MaxAlign(uint64_t(1) << L->MaxAlignLog2);
    for (const CrossReloc &R : In.CrossRelocs) {
      assert(R.Site < L->InitSize && "relocation sites lie in the image");
      B.emitToken(Opcode::Ldsfld, tok(Base));
      emitOffset(B, R.Site);
      B.emitCall(Opcode::Call, tok(R.Accessor), 0, 1);
      emitOffset(B, R.Addend);
      if (R.Width == 4) {
        B.emit(Opcode::Conv_i4);
        emitUnaligned(B, MaxAlign, R.Site, 4);
        B.emit(Opcode::Stind_i4);
      } else {
        assert(R.Width == 8 && "relocations hold 4 or 8 bytes");
        emitUnaligned(B, MaxAlign, R.Site, PointerSize);
        B.emit(Opcode::Stind_i);
      }
    }
  }

  // The constructors last, in run order: the data they may touch is in
  // place. Each is called directly, with argc, argv and envp as far as it
  // takes them (CtorArgs); a result is popped.
  assert((In.CtorArgs.empty() || In.CtorArgs.size() == In.Ctors.size()) &&
         "one argument count per constructor");
  std::optional<unsigned> ArgvLocals;
  for (auto [Index, Ctor] : enumerate(In.Ctors)) {
    auto [Method, ReturnsValue] = Ctor;
    unsigned NumArgs = In.CtorArgs.empty() ? 0 : In.CtorArgs[Index];
    assert(NumArgs <= 3 && "constructors take argc, argv and envp at most");
    if (NumArgs != 0 && !ArgvLocals) {
      ArgvLocals = M.Locals.size();
      emitArgv(M, *ArgvLocals, PointerSize, "ctor_argv", Sink);
      if (Shared) {
        // For '<msil>Main', so that main gets this argv, as glibc passes
        // one argv to the constructors and main. A native int field holds
        // argc.
        Shared->Argc = Sink.addTrailerField(ArgcFieldName);
        Shared->Argv = Sink.addTrailerField(ArgvFieldName);
        B.emitLdloc(*ArgvLocals + 1);
        B.emit(Opcode::Conv_i);
        B.emitToken(Opcode::Stsfld, tok(Shared->Argc));
        B.emitLdloc(*ArgvLocals + 2);
        B.emitToken(Opcode::Stsfld, tok(Shared->Argv));
      }
    }
    if (NumArgs >= 1)
      B.emitLdloc(*ArgvLocals + 1);
    if (NumArgs >= 2)
      B.emitLdloc(*ArgvLocals + 2);
    if (NumArgs >= 3) {
      // envp is the C library's environ: the pointer at its address.
      assert(In.EnvironField && "envp needs the address of environ");
      B.emitToken(Opcode::Ldsfld, tok(In.EnvironField));
      B.emit(Opcode::Ldind_i);
    }
    B.emitCall(Opcode::Call, tok(Method), NumArgs, ReturnsValue ? 1 : 0);
    if (ReturnsValue)
      B.emit(Opcode::Pop);
  }
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
/// function main (decision D27), with the argc and argv of \p Shared when
/// <Module>::.cctor stored them there.
static void buildEntryPoint(const EntryPoint &E, const SharedArgs &Shared,
                            TrailerSink &Sink) {
  MethodSig Sig;
  Sig.Return = TypeSig::get(ELEMENT_TYPE_I4);
  Sig.Params = {TypeSig::getSZArray(TypeSig::get(ELEMENT_TYPE_STRING))};
  TrailerMethod M =
      makeMethod(EntryPointName,
                 MethodAttributes::Private | MethodAttributes::HideBySig |
                     MethodAttributes::Static,
                 std::move(Sig));
  ILBuilder &B = M.Body;
  if (E.HasArgs && Shared.Argv) {
    // return main(argc, argv), as the constructors got them: the type
    // initializer of <Module> runs before a static field of <Module> is
    // first read (ECMA-335 II.10.5.3.1).
    B.emitToken(Opcode::Ldsfld, tok(Shared.Argc));
    B.emit(Opcode::Conv_i4);
    B.emitToken(Opcode::Ldsfld, tok(Shared.Argv));
  } else if (E.HasArgs) {
    // Locals: 0 = the string[], 1 = argc, 2 = argv, 3 = i.
    emitArgv(M, 0, E.PointerSize, "argv", Sink);
    // return main(argc, argv)
    B.emitLdloc(1);
    B.emitLdloc(2);
  }
  B.emitCall(Opcode::Call, tok(E.Main), E.HasArgs ? 2 : 0, 1);
  B.emitRet();
  finish(M, Sink);
}

bool cli::skipsNullConstructorSlots(SectionRules Rules) {
  return Rules == SectionRules::COFF;
}

unsigned cli::countAddressSlots(ArrayRef<RuntimeReloc> Relocs,
                                unsigned RelocLoopThreshold) {
  if (Relocs.size() <= RelocLoopThreshold)
    return 0;
  SlotMap Slots;
  return collectSlots(Relocs, Slots).size();
}

void cli::buildTrailer(const TrailerInput &In, TrailerSink &Sink) {
  // Phase 1 and its helpers belong to '<msil>Data' in a two-phase output.
  TrailerOwner Phase1Owner =
      In.TwoPhase ? TrailerOwner::Data : TrailerOwner::Module;
  EntityRef InitImports = 0;
  if (!In.DataImports.empty()) {
    assert(In.ImportHelper && "data imports need a helper");
    MethodSig HelperSig;
    HelperSig.Return = TypeSig::get(ELEMENT_TYPE_I);
    HelperSig.Params = {TypeSig::get(ELEMENT_TYPE_STRING),
                        TypeSig::get(ELEMENT_TYPE_STRING)};
    EntityRef Helper = Sink.declareHelper(DataImportHelperName, HelperSig);
    InitImports = Sink.declareHelper(InitDataImportsName, getVoidSig());
    buildDataImportHelper(*In.ImportHelper, HelperSig, Phase1Owner, Sink);
    buildInitDataImports(In.DataImports, Helper, Phase1Owner, Sink);
  }
  bool HasPhase1 = In.Image || !In.FnAddrFields.empty() || InitImports;
  bool HasPhase2 =
      !In.LibraryTypes.empty() || !In.CrossRelocs.empty() || !In.Ctors.empty();
  SharedArgs Shared;
  if (In.TwoPhase) {
    if (HasPhase1) {
      TrailerMethod M = makeTypeInitializer(TrailerOwner::Data);
      emitPhase1(In, InitImports, M, Sink);
      M.Body.emitRet();
      finish(M, Sink);
    }
    if (HasPhase2) {
      TrailerMethod M = makeTypeInitializer(TrailerOwner::Module);
      emitPhase2(In, M, Sink, /*Shared=*/nullptr);
      M.Body.emitRet();
      finish(M, Sink);
    }
  } else if (HasPhase1 || HasPhase2) {
    // A main that takes argc and argv gets those of the constructors that
    // take them, if any do (CtorArgs).
    bool MainTakesArgs =
        In.Entry && In.Entry->HasArgs && In.Entry->PointerSize == PointerSize;
    TrailerMethod M = makeTypeInitializer(TrailerOwner::Module);
    emitPhase1(In, InitImports, M, Sink);
    emitPhase2(In, M, Sink, MainTakesArgs ? &Shared : nullptr);
    M.Body.emitRet();
    finish(M, Sink);
  }
  if (In.Entry)
    buildEntryPoint(*In.Entry, Shared, Sink);
}
