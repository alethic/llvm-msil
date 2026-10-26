//===- llvm/CLI/Trailer.h - MSIL module trailer builder ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the builder of the methods that every MSIL output
// synthesizes (ImplementationPlan.md 5.9.10): <Module>::.cctor, which
// allocates and fills the data image (decision D12), sets the address fields
// of the globals and of the functions whose address is used, initializes the
// native data imports (5.10.12), applies the data relocations and calls the
// constructors; the entry point '<msil>Main' (decision D27); and the helpers
// of the data imports. msil-ld builds rows from it (lld/MSIL Synthetic.cpp)
// and the MSIL target ILAsm text (MSILAsmPrinter, through
// MSILTargetStreamer), so -S output and a one-object link share one
// implementation (Q5).
//
// The builder is pure computation: it asks its sink for the entities it
// names (fields, BCL members, helpers, strings, RVA data) and hands it each
// method with its body laid out (cli::ILBuilder, token operands
// TokenRef::getExternal(EntityRef)).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_TRAILER_H
#define LLVM_CLI_TRAILER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/Signature.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
namespace cli {

struct ImageLayout;

/// A handle that the sink defines and later prints or encodes; 0 is none.
using EntityRef = uint32_t;

/// The names of the synthesized members (plan 5.1, 5.9.10, 5.9.12, 5.10.12):
/// the module initializer, the entry point, the field that holds the image
/// base, the RVA data of the image and of the relocation table with their
/// value types, and the data-import helpers. No dots in type names: ilasm
/// would split them into a namespace and a name.
inline constexpr StringLiteral ModuleInitializerName = ".cctor";
inline constexpr StringLiteral EntryPointName = "<msil>Main";
inline constexpr StringLiteral DataFieldName = "<msil>data";
inline constexpr StringLiteral ImageTypeName = "<msil>image_t";
inline constexpr StringLiteral ImageFieldName = "<msil>image";
inline constexpr StringLiteral RelocsTypeName = "<msil>relocs_t";
inline constexpr StringLiteral RelocsFieldName = "<msil>relocs";
inline constexpr StringLiteral DataImportHelperName = "<msil>dataimport";
inline constexpr StringLiteral InitDataImportsName = "<msil>init_dataimports";

/// A data relocation applied at startup: the image slot at Site receives the
/// address of a global plus Addend (Width 8, stind.i) or its low 32 bits
/// (Width 4, conv.i4 + stind.i4), or a function's pointer value.
struct RuntimeReloc {
  uint64_t Site = 0;
  uint8_t Width = 0;
  /// The target's address field or '<msil>fn_' field; 0 if none, and then
  /// the target lives in the image at ImageOffset.
  EntityRef Field = 0;
  /// The target's offset when it lives in the image.
  std::optional<uint64_t> ImageOffset;
  int64_t Addend = 0;
};

/// A native data import (5.10.12, Q15): the address field Field receives the
/// address of the export Entry of the native library Module.
struct DataImport {
  EntityRef Field = 0;
  std::string Module, Entry;
};

/// The native functions that '<msil>dataimport' calls (5.10.12): Kernel32 on
/// Windows triples, LoadLibraryW (native int(native int)) and GetProcAddress
/// (native int(native int, native int)); Dlfcn on ELF triples, dlopen
/// (native int(native int, int32)) and dlsym (native int(native int,
/// native int)).
struct DataImportHelper {
  enum class Kind : uint8_t { Kernel32, Dlfcn } K = Kind::Kernel32;
  EntityRef Load = 0, GetSymbol = 0;
};

/// The C function that '<msil>Main' calls (decision D27): main(void), or
/// main(int, char **) when HasArgs, with PointerSize-byte pointers.
struct EntryPoint {
  EntityRef Main = 0;
  bool HasArgs = false;
  unsigned PointerSize = 8;
};

/// Whether an entry function with the IL signature \p Sig (variadic if
/// \p IsVarArg or if Sig has a SENTINEL) has one of the shapes that
/// '<msil>Main' calls (decision D27, decided on the IL signature): a default
/// calling convention, an int32 result, not variadic, and no parameters
/// (false: EntryPoint::HasArgs) or exactly (int32, native int) on 8-byte
/// pointers or (int32, int32) on 4-byte ones (true). std::nullopt for any
/// other signature, which is 'unsupported signature for <entry>'.
LLVM_ABI std::optional<bool>
getEntryPointHasArgs(const MethodSig &Sig, bool IsVarArg, unsigned PointerSize);

/// A synthesized method: .cctor, '<msil>Main', '<msil>dataimport',
/// '<msil>init_dataimports'. Body is laid out (ILBuilder::layout), and its
/// token operands are TokenRef::getExternal(EntityRef).
struct TrailerMethod {
  std::string Name;
  MethodAttributes Flags = MethodAttributes::PrivateScope;
  MethodSig Sig;
  std::vector<TypeSig> Locals;
  ILBuilder Body = ILBuilder(/*ReturnsValue=*/false);
  /// The name of each label of Body, by ILLabel::Id, for a text sink (the
  /// MSIL target prints them as temporary symbols, '@argv_loop').
  std::vector<std::string> LabelNames;
};

/// What the trailer is built from. The canonical orders (5.9.10): address
/// fields in image-offset order; '<msil>fn_' fields in the order of their
/// methods' first FUNCTION symbol over the inputs in link order and SYMTAB
/// order (for one object, IR module order); relocations in output site
/// order; constructors in 5.9.9's order (cli::sortConstructors).
struct TrailerInput {
  /// The data image; null when the output has no data segment.
  const ImageLayout *Image = nullptr;
  /// (address field, image offset), in image-offset order.
  std::vector<std::pair<EntityRef, uint64_t>> AddressFields;
  /// ('<msil>fn_' field, method), in canonical order.
  std::vector<std::pair<EntityRef, EntityRef>> FnAddrFields;
  std::vector<DataImport> DataImports;
  /// Set when DataImports is not empty.
  std::optional<DataImportHelper> ImportHelper;
  /// In site order; only with an image.
  std::vector<RuntimeReloc> Relocs;
  /// (method, returns a value), in run order.
  std::vector<std::pair<EntityRef, bool>> Ctors;
  std::optional<EntryPoint> Entry;
  /// Above this many relocations the module initializer applies them through
  /// the '<msil>relocs' table and a loop (llc -msil-reloc-loop-threshold,
  /// msil-ld --msil-reloc-loop-threshold).
  unsigned RelocLoopThreshold = 32;
};

/// The entities the trailer names, and the methods it makes: the MSIL
/// target's text sink (MSILAsmPrinter) and msil-ld's rows (lld/MSIL
/// Synthetic.cpp).
class LLVM_ABI TrailerSink {
public:
  virtual ~TrailerSink();

  /// '<msil>data', the static native int field that holds the image base.
  virtual EntityRef getDataField() = 0;
  /// A static field FieldName of the sized value type TypeName (explicit
  /// layout, pack 1, size Bytes.size()) at RVA data holding Bytes: the
  /// image ('<msil>image' of '<msil>image_t') and the relocation table
  /// ('<msil>relocs' of '<msil>relocs_t').
  virtual EntityRef addRVAData(StringRef TypeName, StringRef FieldName,
                               ArrayRef<uint8_t> Bytes) = 0;
  /// A member of llvm/CLI/BCLMembers.def (plan 5.11).
  virtual EntityRef getBCLMember(BCLMemberId Id) = 0;
  /// The method Name with signature Sig that a later emitMethod defines
  /// ('<msil>dataimport', '<msil>init_dataimports').
  virtual EntityRef declareHelper(StringRef Name, const MethodSig &Sig) = 0;
  /// The user string UTF8 ('ldstr', the operands of the data imports).
  virtual EntityRef addUserString(StringRef UTF8) = 0;
  /// Defines a method: name, flags, signature, locals, maxstack and the
  /// instruction list of M.Body.
  virtual void emitMethod(const TrailerMethod &M) = 0;
};

/// The number of 8-byte address slots that the relocation loop needs
/// (5.9.9 rule 4): none when there are at most RelocLoopThreshold
/// relocations, else one per distinct (Field, Addend) pair of the
/// relocations without an ImageOffset.
LLVM_ABI unsigned countAddressSlots(ArrayRef<RuntimeReloc> Relocs,
                                    unsigned RelocLoopThreshold);

/// Builds the trailer of In and hands it to Sink:
/// - '<msil>dataimport' and '<msil>init_dataimports' when there are data
///   imports;
/// - <Module>::.cctor when there is an image, a function address, a data
///   import or a constructor: allocate (AllocHGlobal(TotalSize + MaxAlign -
///   1)), align, store '<msil>data', cpblk the image, initblk the zero part,
///   store the address fields, ldftn and store the '<msil>fn_' fields, call
///   '<msil>init_dataimports', fill the address slots and run the loop (loop
///   mode only), apply the relocations, call the constructors (a result is
///   popped), ret. With at most RelocLoopThreshold relocations each is
///   stored straight-line, and the method is Phase 1's, instruction for
///   instruction;
/// - '<msil>Main' when In.Entry is set (decision D27).
/// The image's sizes must satisfy 5.9.9 rule 5 except where the caller has
/// already reported them (ImageInput::AllowOversize), and its slots
/// (ImageLayout::SlotsOffset) must hold countAddressSlots(In.Relocs, ...).
/// Phase 2's triples have 64-bit pointers (decision D6): a relocation of
/// width 8 stores a pointer.
LLVM_ABI void buildTrailer(const TrailerInput &In, TrailerSink &Sink);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_TRAILER_H
