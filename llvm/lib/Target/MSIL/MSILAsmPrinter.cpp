//===-- MSILAsmPrinter.cpp - MSIL LLVM assembly writer --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a printer that converts from our internal representation
/// of machine-dependent LLVM code to ILAsm text.
///
/// The module header comes from emitStartOfAsmFile, each function becomes a
/// global method of '<Module>' (header, .maxstack, .locals, the instructions
/// through MSILMCInstLower and MSILInstPrinter, and the closing brace), and
/// the module trailer (P/Invoke declarations, the data image, the address
/// fields of globals and of the functions whose address is used, the module
/// initializer and the entry point) comes from emitEndOfAsmFile. ILAsm
/// resolves forward references to methods and fields, so code refers to
/// globals symbolically and the trailer is laid out after the last function.
/// Everything else the base AsmPrinter would print (sections, linkage and
/// visibility directives, alignment, constant pools, jump tables) is
/// suppressed here or by MSILMCAsmInfo.
///
/// The trailer is a one-object link with msil-ld's code (Q5, plan 5.9.9,
/// 5.9.10): cli::layoutImage lays the globals out (MSILDataSegment), named
/// sections included, and cli::buildTrailer builds the module initializer,
/// the entry point and the data-import helpers, which MSILTrailerTextSink
/// prints. Without named sections and with at most
/// -msil-reloc-loop-threshold data relocations the text is Phase 1's.
///
/// In object mode (MSILTargetMachine::isObjectMode(), plan 5.8) the same
/// methods are the methods of a CIL object (an object file, or its ILAsm
/// form, object-form text), and emitEndOfAsmFile writes the rest of the
/// object instead of the trailer (emitObjectEnd): the address fields of the
/// defined globals, the import stubs (plan 5.8.3) and the link-info record
/// (plan 5.8.8), whose tokens follow row order R6. There is no import policy
/// (decision D14) and no module initializer, entry point or data image.
///
//===----------------------------------------------------------------------===//

#include "MSILAsmPrinter.h"
#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MCTargetDesc/MSILTargetStreamer.h"
#include "MSIL.h"
#include "MSILDataSegment.h"
#include "MSILMCInstLower.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILSubtarget.h"
#include "MSILTargetMachine.h"
#include "MSILUtilities.h"
#include "TargetInfo/MSILTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/ImageLayout.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CLI/Trailer.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "msil-asm-printer"

static cl::opt<std::string> MSILAssemblyName(
    "msil-assembly-name",
    cl::desc("MSIL: the name of the emitted assembly (default: the stem of "
             "the module's source file name, or 'module')"),
    cl::value_desc("name"));

static cl::opt<unsigned> RelocLoopThreshold(
    "msil-reloc-loop-threshold",
    cl::desc("MSIL: apply the data relocations of the module initializer "
             "through a table and a loop when there are more than this many "
             "(plan 5.9.10)"),
    cl::value_desc("count"), cl::init(32));

/// The relocation-loop threshold of whole-program output (plan 5.9.10,
/// section 12 row 'M20.a (4) and M20.b (6) clarified'):
/// -msil-reloc-loop-threshold when given on the command line (also through
/// -mllvm), else the module flag "msil-reloc-loop-threshold", which msil-ld
/// sets on its merged module under --lto-emit-asm for its own
/// --msil-reloc-loop-threshold, so that its text and its PE apply the
/// relocations alike, else 32. A value that is not an integer constant
/// counts as absent.
static unsigned getRelocLoopThreshold(const Module &M) {
  if (RelocLoopThreshold.getNumOccurrences() > 0)
    return RelocLoopThreshold;
  if (const auto *Flag = mdconst::dyn_extract_or_null<ConstantInt>(
          M.getModuleFlag("msil-reloc-loop-threshold")))
    return Flag->getLimitedValue(std::numeric_limits<unsigned>::max());
  return RelocLoopThreshold;
}

/// The name of the assembly (decision D28 as amended for M14):
/// -msil-assembly-name, else the module flag "msil-assembly-name", which
/// msil-ld sets on its merged LTO module (named ld-temp.o), else the stem of
/// the source file name. Windows separators are recognized on every host, so
/// a module compiled on Windows gets the same name everywhere.
static std::string getAssemblyName(const Module &M) {
  if (!MSILAssemblyName.empty())
    return MSILAssemblyName;
  if (const auto *Flag =
          dyn_cast_or_null<MDString>(M.getModuleFlag("msil-assembly-name"));
      Flag && !Flag->getString().empty())
    return Flag->getString().str();
  StringRef Stem =
      sys::path::stem(M.getSourceFileName(), sys::path::Style::windows);
  if (Stem.empty() || Stem == "." || Stem == "..")
    return "module";
  return Stem.str();
}

/// The Module row's name of an object (rule O3): the base name of the IR
/// source file name, with Windows separators recognized on every host, or
/// '<msil>module' when there is none. It never comes from the output's name,
/// so an object is the same in every directory (rule O4).
static std::string getObjectModuleName(const Module &M) {
  StringRef Name =
      sys::path::filename(M.getSourceFileName(), sys::path::Style::windows);
  if (Name.empty() || Name == "." || Name == "..")
    return "<msil>module";
  return Name.str();
}

/// The signature that \p MFI records (the parameters and results lowered by
/// ISel), in CIL types. The method header prints MSILAsmPrinter::getSignature
/// instead, which call sites share; the two must agree.
[[maybe_unused]] static MSIL::Signature
getFunctionInfoSignature(const MSILFunctionInfo &MFI,
                         unsigned PointerSizeInBits) {
  MSIL::Signature Sig;
  for (MVT VT : MFI.getResults())
    Sig.Results.push_back(MSIL::toValType(VT, PointerSizeInBits));
  for (MVT VT : MFI.getParams())
    Sig.Params.push_back(MSIL::toValType(VT, PointerSizeInBits));
  return Sig;
}

/// Whether \p MI, if it is a direct call, pushes and pops exactly what the
/// 'call' token, which spells the callee's signature, promises. LowerCall
/// builds a direct call only when the call site's function type is the
/// callee's and lowers its operands and results the way getSignature lowers
/// that type; a mismatch would make the JIT reject the method
/// (MissingMethodException or an unbalanced stack). In object mode
/// (\p ObjectMode) the call of a function that the object does not define
/// names the symbol with the call site's signature (plan 5.8.3 R3), and
/// msil-ld checks it against the definition.
[[maybe_unused]] static bool matchesCalleeSignature(const MachineInstr &MI,
                                                    MSILAsmPrinter &AP,
                                                    bool ObjectMode) {
  if (!MSIL::isCallDirect(MI.getOpcode()))
    return true;
  const MachineOperand &Callee = MSIL::getCalleeOp(MI);
  const auto *F =
      Callee.isGlobal() ? dyn_cast<Function>(Callee.getGlobal()) : nullptr;
  if (!F || (ObjectMode && F->isDeclarationForLinker()))
    return true;
  return MSILMCInstLower::getCallSiteSignature(MI) == AP.getSignature(*F);
}

/// ldarg, starg, ldloc and stloc take 16-bit indices, and ilasm silently
/// truncates larger ones. Index 0xFFFF is avoided.
static constexpr size_t MaxArgsOrLocals = 0xFFFF;

/// The label of the RVA blobs of the data image and of the relocation table
/// (decision D12, plan 5.1 and 5.9.10). The other names of the trailer are
/// cli::buildTrailer's (llvm/CLI/Trailer.h): no generated name can be a C
/// function's or variable's, because none takes a string[] and only
/// cli::DataFieldName can collide with the address field of a global variable
/// (checkAddressFieldNames), and cli::ArgcFieldName and cli::ArgvFieldName
/// where the trailer makes them (emitTrailer); the RVA fields have types of
/// their own.
static constexpr StringRef ImageLabel = "<msil>image_d";
static constexpr StringRef RelocsLabel = "<msil>relocs_d";

/// Whether \p Name is a constructor name, which no method or P/Invoke of a
/// function may have: the runtime runs '<Module>::.cctor', which the printer
/// generates (emitTrailer), as the module initializer, and refuses to load a
/// type with a static '.ctor'.
static bool isReservedMethodName(StringRef Name) {
  return Name == ".cctor" || Name == ".ctor";
}

/// Reports that the function \p F, which gets a method or a P/Invoke
/// declaration, has the reserved IL name \p Name.
static void reportReservedMethodName(const Function &F, StringRef Name) {
  F.getContext().emitError("function " + MSIL::getIRName(F) +
                           " has the reserved IL name '" + Name + "'");
}

cli::TypeSig MSIL::toTypeSig(MSIL::ValType T) {
  switch (T) {
  case MSIL::ValType::I32:
    return cli::TypeSig::get(cli::ELEMENT_TYPE_I4);
  case MSIL::ValType::NativeInt:
    return cli::TypeSig::get(cli::ELEMENT_TYPE_I);
  case MSIL::ValType::I64:
    return cli::TypeSig::get(cli::ELEMENT_TYPE_I8);
  case MSIL::ValType::F32:
    return cli::TypeSig::get(cli::ELEMENT_TYPE_R4);
  case MSIL::ValType::F64:
    return cli::TypeSig::get(cli::ELEMENT_TYPE_R8);
  }
  llvm_unreachable("MSIL: unknown value type");
}

cli::MethodSig MSIL::toMethodSig(const MSIL::Signature &S) {
  cli::MethodSig M;
  switch (S.CC) {
  case MSIL::CallConv::Managed:
    M.Header = cli::SIG_DEFAULT;
    break;
  case MSIL::CallConv::UnmanagedCdecl:
    M.Header = cli::SIG_C;
    break;
  case MSIL::CallConv::UnmanagedStdcall:
    M.Header = cli::SIG_STDCALL;
    break;
  }
  if (!S.Results.empty())
    M.Return = toTypeSig(S.Results.front());
  for (MSIL::ValType T : S.Params)
    M.Params.push_back(toTypeSig(T));
  return M;
}

/// The native functions of the data-import helper '<msil>dataimport' (plan
/// 5.10.12) on \p TT: LoadLibraryW and GetProcAddress from kernel32 on
/// Windows triples, dlopen and dlsym on the others, as cli::DataImportHelper
/// calls them.
static std::array<std::pair<StringRef, MSIL::Signature>, 2>
getDataImportFunctions(const Triple &TT) {
  using MSIL::ValType;
  MSIL::Signature Load, GetSymbol;
  Load.Results = {ValType::NativeInt};
  GetSymbol.Results = {ValType::NativeInt};
  GetSymbol.Params = {ValType::NativeInt, ValType::NativeInt};
  if (TT.isOSWindows()) {
    // HMODULE LoadLibraryW(LPCWSTR); FARPROC GetProcAddress(HMODULE, LPCSTR)
    Load.Params = {ValType::NativeInt};
    return {{{"LoadLibraryW", Load}, {"GetProcAddress", GetSymbol}}};
  }
  // void *dlopen(const char *, int); void *dlsym(void *, const char *)
  Load.Params = {ValType::NativeInt, ValType::I32};
  return {{{"dlopen", Load}, {"dlsym", GetSymbol}}};
}

/// The number of glibc's .init_array arguments (int argc, char **argv, char
/// **envp) that a constructor with the IL signature \p S takes, or
/// std::nullopt if it takes other arguments: off Windows its parameters may
/// be a prefix of (int32, native int, native int), on Windows triples there
/// are none (plan section 12, 'M22.b extended', defect 4), as msil-ld decides
/// it (lld/MSIL OutputSegment.cpp). The module initializer builds argc and
/// argv once, and '<msil>Main' passes main the same ones; envp is the C
/// library's environ (cli::TrailerInput::CtorArgs).
static std::optional<unsigned> getConstructorArgs(const MSIL::Signature &S,
                                                  const Triple &TT) {
  if (S.Params.empty())
    return 0;
  if (TT.isOSWindows() || S.Params.size() > 3)
    return std::nullopt;
  for (auto [I, T] : enumerate(S.Params))
    if (T != (I == 0 ? MSIL::ValType::I32 : MSIL::ValType::NativeInt))
      return std::nullopt;
  return S.Params.size();
}

/// The native data import of the symbol \p Name that msil-ld's linked text
/// \p M lists in !msil.imports (plan 5.10.12, 5.10.13): the module and the
/// entry of the first kind-1 operand !{!"<symbol>", !"<module>", !"<entry>",
/// i32 1} for \p Name with a module (the entry defaults to the name). It
/// serves an external variable without import attributes
/// (getLinkedDataImport) and the C library's environ, which a module whose
/// constructors take envp needs.
static std::optional<std::pair<std::string, std::string>>
getListedDataImport(const Module &M, StringRef Name) {
  const NamedMDNode *Imports = M.getNamedMetadata("msil.imports");
  if (!Imports)
    return std::nullopt;
  for (const MDNode *Op : Imports->operands()) {
    if (Op->getNumOperands() != 4)
      continue;
    const auto *Symbol = dyn_cast_or_null<MDString>(Op->getOperand(0));
    const auto *Module = dyn_cast_or_null<MDString>(Op->getOperand(1));
    const auto *Entry = dyn_cast_or_null<MDString>(Op->getOperand(2));
    const auto *Kind =
        mdconst::dyn_extract_or_null<ConstantInt>(Op->getOperand(3));
    if (!Symbol || Symbol->getString() != Name || !Module ||
        Module->getString().empty() || !Entry || !Kind || !Kind->isOne())
      continue;
    return std::make_pair(
        Module->getString().str(),
        (Entry->getString().empty() ? Name : Entry->getString()).str());
  }
  return std::nullopt;
}

/// The C library of the ELF triple \p TT by its DT_SONAME, which defines
/// environ, dlopen and dlsym: glibc's libc.so.6 (dlopen and dlsym moved into
/// it in glibc 2.34) or musl's libc.so. Whole-program output imports envp's
/// environ from it when a constructor takes envp and the module imports no
/// environ itself (plan section 12, 'M22.b extended', defect 4).
static StringRef getCLibrarySoname(const Triple &TT) {
  return TT.isMusl() ? "libc.so" : "libc.so.6";
}

namespace llvm {
/// The text sink of cli::buildTrailer (plan 5.9.10). The trailer's entities
/// are the MSILMemberRefExprs that code uses, so its instructions are CIL_
/// MCInsts like those of a compiled function (MSILInstPrinter prints them,
/// and plan 5.1's text keeps one owner); the data-import helpers and their
/// string literals, which no MSILMemberRefExpr can spell (their signatures
/// take strings), go through MSILTargetStreamer's helper calls instead. The
/// sink records the RVA data and the methods, and the AsmPrinter emits them
/// at their places in the trailer (emitDataSegment, emitMethods).
class MSILTrailerTextSink final : public cli::TrailerSink {
  MSILAsmPrinter &AP;
  BumpPtrAllocator Alloc;
  /// Owns the names that MSILMemberRefExprs refer to.
  StringSaver Saver{Alloc};

  struct Entity {
    enum Kind : uint8_t { Member, Helper, String };
    Kind K = Member;
    /// Member: the operand of the instructions that name it.
    const MSILMemberRefExpr *Ref = nullptr;
    /// Helper: its name and signature; String: the literal.
    std::string Name;
    cli::MethodSig Sig;
  };
  /// EntityRef I is Entities[I - 1].
  std::vector<Entity> Entities;
  cli::EntityRef DataField = 0;
  struct RVAData {
    std::string FieldName;
    std::vector<uint8_t> Bytes;
  };
  std::vector<RVAData> RVA;
  /// The names of the fields that addTrailerField made, in order.
  std::vector<StringRef> TrailerFields;
  std::vector<cli::TrailerMethod> Methods;

  cli::EntityRef addEntity(Entity E) {
    Entities.push_back(std::move(E));
    return Entities.size();
  }
  void printMethod(const cli::TrailerMethod &M);

public:
  explicit MSILTrailerTextSink(MSILAsmPrinter &AP) : AP(AP) {}

  /// A member that MCInsts name.
  cli::EntityRef add(const MSILMemberRefExpr *Ref) {
    Entity E;
    E.Ref = Ref;
    return addEntity(std::move(E));
  }

  cli::EntityRef getDataField() override {
    if (!DataField)
      DataField = add(
          MSILMemberRefExpr::createField(cli::DataFieldName, AP.OutContext));
    return DataField;
  }
  cli::EntityRef addRVAData(StringRef TypeName, StringRef FieldName,
                            ArrayRef<uint8_t> Bytes) override {
    RVA.push_back({FieldName.str(), Bytes.vec()});
    return add(MSILMemberRefExpr::createValueTypeField(
        Saver.save(FieldName), Saver.save(TypeName), AP.OutContext));
  }
  cli::EntityRef getBCLMember(cli::BCLMemberId Id) override {
    return add(MSILMemberRefExpr::createBCL(Id, AP.OutContext));
  }
  cli::EntityRef declareHelper(StringRef Name,
                               const cli::MethodSig &Sig) override {
    Entity E;
    E.K = Entity::Helper;
    E.Name = Name.str();
    E.Sig = Sig;
    return addEntity(std::move(E));
  }
  cli::EntityRef addUserString(StringRef UTF8) override {
    Entity E;
    E.K = Entity::String;
    E.Name = UTF8.str();
    return addEntity(std::move(E));
  }
  cli::EntityRef addTrailerField(StringRef Name) override {
    StringRef Saved = Saver.save(Name);
    TrailerFields.push_back(Saved);
    return add(MSILMemberRefExpr::createField(Saved, AP.OutContext));
  }
  void emitMethod(const cli::TrailerMethod &M) override {
    Methods.push_back(M);
  }

  /// The fields that addTrailerField made ('<msil>argc' and '<msil>argv').
  ArrayRef<StringRef> getTrailerFields() const { return TrailerFields; }

  /// The bytes of the RVA field \p FieldName; empty if there is none.
  ArrayRef<uint8_t> getRVAData(StringRef FieldName) const {
    for (const RVAData &D : RVA)
      if (D.FieldName == FieldName)
        return D.Bytes;
    return {};
  }

  /// Emits the methods in the order cli::buildTrailer made them: the
  /// data-import helpers, <Module>::.cctor and '<msil>Main'.
  void emitMethods() {
    for (const cli::TrailerMethod &M : Methods)
      printMethod(M);
  }
};
} // end namespace llvm

void MSILTrailerTextSink::printMethod(const cli::TrailerMethod &M) {
  // Only msil-ld's -shared outputs initialize in two phases (plan 5.9.10).
  assert(M.Owner == cli::TrailerOwner::Module &&
         "whole-program output has one module initializer");
  MSILTargetStreamer &TS = AP.getTargetStreamer();
  MSILMethodDecl Decl;
  Decl.Name = M.Name;
  Decl.Public = false;
  if (M.Name == cli::ModuleInitializerName) {
    Decl.Kind = MSILMethodKind::ModuleInitializer;
    TS.emitMethodBegin(Decl);
  } else if (M.Name == cli::EntryPointName) {
    Decl.Kind = MSILMethodKind::EntryPoint;
    TS.emitMethodBegin(Decl);
    TS.emitEntryPoint();
  } else {
    TS.emitHelperBegin(M.Name, M.Flags, M.Sig);
  }
  const cli::ILBuilder &B = M.Body;
  TS.emitMaxStack(B.getMaxStack());
  // Never 'init' (decision D13): every local is written before it is read.
  TS.emitLocals(M.Locals);

  // Method-local labels, temporary symbols like the block labels: never the
  // symbol of a C function or variable of the same IL name. Text names them
  // '@<name>' (MCAsmInfo's InternalSymbolPrefix), e.g. '@argv_loop'.
  MCContext &Ctx = AP.OutContext;
  SmallVector<MCSymbol *, 4> LabelSyms;
  SmallVector<std::pair<uint32_t, unsigned>, 4> LabelOffsets;
  for (unsigned Id = 0, E = M.LabelNames.size(); Id != E; ++Id) {
    LabelSyms.push_back(
        Ctx.createTempSymbol(M.LabelNames[Id], /*AlwaysAddSuffix=*/false));
    LabelOffsets.push_back({B.getLabelOffset(cli::ILLabel{Id}), Id});
  }
  auto Label = [&](cli::ILLabel L) {
    assert(L.isValid() && L.Id < LabelSyms.size() && "unnamed label");
    return MCOperand::createExpr(MCSymbolRefExpr::create(LabelSyms[L.Id], Ctx));
  };
  // The labels in offset order, each emitted before the instruction at its
  // offset (or after the last one).
  llvm::sort(LabelOffsets);
  auto *NextLabel = LabelOffsets.begin();
  auto EmitLabelsUpTo = [&](uint32_t Offset) {
    for (; NextLabel != LabelOffsets.end() && NextLabel->first <= Offset;
         ++NextLabel)
      AP.OutStreamer->emitLabel(LabelSyms[NextLabel->second]);
  };

  for (const cli::ILInstr &I : B.instructions()) {
    EmitLabelsUpTo(I.Offset);
    MCInst Inst;
    Inst.setOpcode(MSIL::getMCOpcodeForCIL(I.Op));
    switch (cli::getOpcodeInfo(I.Op).Operand) {
    case cli::OperandKind::InlineNone:
      break;
    case cli::OperandKind::ShortInlineVar:
    case cli::OperandKind::InlineVar:
    case cli::OperandKind::ShortInlineI:
    case cli::OperandKind::InlineI:
    case cli::OperandKind::InlineI8:
      Inst.addOperand(MCOperand::createImm(I.Imm));
      break;
    case cli::OperandKind::ShortInlineR:
      Inst.addOperand(MCOperand::createSFPImm(static_cast<uint32_t>(I.FPBits)));
      break;
    case cli::OperandKind::InlineR:
      Inst.addOperand(MCOperand::createDFPImm(I.FPBits));
      break;
    case cli::OperandKind::ShortInlineBrTarget:
    case cli::OperandKind::InlineBrTarget:
      Inst.addOperand(Label(I.Target));
      break;
    case cli::OperandKind::InlineMethod:
    case cli::OperandKind::InlineField:
    case cli::OperandKind::InlineType:
    case cli::OperandKind::InlineTok:
    case cli::OperandKind::InlineSig:
    case cli::OperandKind::InlineString: {
      assert(I.Token.getKind() == cli::TokenRef::Kind::External &&
             I.Token.getValue() != 0 && I.Token.getValue() <= Entities.size() &&
             "unknown entity");
      const Entity &E = Entities[I.Token.getValue() - 1];
      if (E.K == Entity::String) {
        TS.emitStringInstruction(I.Op, E.Name);
        continue;
      }
      if (E.K == Entity::Helper) {
        TS.emitHelperInstruction(I.Op, E.Name, E.Sig);
        continue;
      }
      Inst.addOperand(MCOperand::createExpr(E.Ref));
      break;
    }
    case cli::OperandKind::InlineSwitch:
      for (cli::ILLabel L : B.getSwitchTargets(I))
        Inst.addOperand(Label(L));
      break;
    }
    // The module trailer has no MachineFunction: the TargetMachine's
    // subtarget (MSIL has one processor and no features, decision D7).
    AP.OutStreamer->emitInstruction(Inst, AP.TM.getMCSubtargetInfo());
  }
  EmitLabelsUpTo(B.getCodeSize());
  TS.emitMethodEnd(M.Name);
}

//===----------------------------------------------------------------------===//
// Signature services.
//===----------------------------------------------------------------------===//

MSILTargetStreamer &MSILAsmPrinter::getTargetStreamer() {
  MCTargetStreamer *TS = OutStreamer->getTargetStreamer();
  assert(TS && "the MSIL target registers an asm, an object and a null "
               "target streamer");
  return *static_cast<MSILTargetStreamer *>(TS);
}

const MSIL::Signature &MSILAsmPrinter::getSignature(const Function &F) {
  assert(!F.isIntrinsic() && "an intrinsic has no CIL method");
  auto [It, Inserted] = FuncSigs.try_emplace(&F, nullptr);
  if (!Inserted)
    return *It->second;

  // Only the IR function type counts, never a body or the caller: ISel lowers
  // the formal arguments and the return of a definition, and the operands and
  // results of every call to it, from this type (call sites whose type
  // differs from the callee's are diagnosed by LowerCall), so a declaration
  // gets the signature its definition would have. Narrow integers widen to
  // int32, 64-bit integers and pointers are native int (decision D2), and a
  // return of more than one value becomes a leading pointer parameter. The
  // variable part of a variadic function is not part of the signature
  // (variadic definitions and calls are diagnosed in Phase 1, D19).
  SmallVector<MVT, 4> Params;
  SmallVector<MVT, 1> Results;
  MSIL::computeSignatureVTs(F.getFunctionType(), &F, F, TM, Params, Results);
  unsigned PointerSizeInBits = F.getDataLayout().getPointerSizeInBits();
  auto *Sig = new (SigAlloc.Allocate()) MSIL::Signature();
  for (MVT VT : Results)
    Sig->Results.push_back(MSIL::toValType(VT, PointerSizeInBits));
  for (MVT VT : Params)
    Sig->Params.push_back(MSIL::toValType(VT, PointerSizeInBits));
  assert(Sig->Results.size() <= 1 && "a CIL method returns at most one value");
  It->second = Sig;
  return *Sig;
}

const MSIL::Signature &
MSILAsmPrinter::internSignature(const MSIL::Signature &S) {
  // std::set never moves its elements, so the reference stays valid.
  return *CalliSigs.insert(S).first;
}

void MSILAsmPrinter::noteExternFunction(const Function &F) {
  // An available_externally definition counts as undefined: it gets no
  // MachineFunction and therefore no method.
  assert(F.isDeclarationForLinker() && !F.isIntrinsic() &&
         "only undefined functions become P/Invoke declarations");
  ExternFuncs.insert({&F, &getSignature(F)});
}

const MSIL::Signature &
MSILAsmPrinter::noteExternalSymbol(StringRef Name, const MSIL::Signature &S) {
  // One P/Invoke declaration per name and signature serves every call site
  // with that signature. One name can come with several: llvm.ldexp with an
  // i32 and an i64 exponent, or the function of -ftrap-function that is also
  // a math library call. CIL methods may be overloaded by signature, so each
  // gets its own declaration. The map owns the name: some symbol names live
  // in storage that the printer does not own.
  auto It = ExtSymSigs.try_emplace(Name).first;
  for (const MSIL::Signature *Sig : It->second)
    if (*Sig == S)
      return *Sig;
  const auto *Sig = new (SigAlloc.Allocate()) MSIL::Signature(S);
  It->second.push_back(Sig);
  ExtSymOrder.push_back({It->getKey(), Sig});
  return *Sig;
}

void MSILAsmPrinter::noteExternGlobal(const GlobalVariable &GV) {
  assert(GV.isDeclarationForLinker() &&
         "only undefined global variables are reported by reference");
  UsedExternGlobals.insert(&GV);
}

void MSILAsmPrinter::noteNonDataGlobal(const GlobalVariable &GV) {
  assert(MF && MSIL::isSpecialLLVMGlobal(GV) &&
         "a non-data global referred to outside a function");
  if (!NonDataRefs.insert(&GV).second)
    return;
  const Function &F = MF->getFunction();
  F.getContext().emitError("function '" + MSIL::getILName(F, TM) +
                           "' refers to '" + MSIL::getILName(GV, TM) +
                           "', which is not program data");
}

std::string MSILAsmPrinter::noteFunctionAddress(const Function &F) {
  assert(!F.isIntrinsic() && "an intrinsic has no address");
  UsedFuncAddrs.insert(&F);
  return getFunctionAddressField(F);
}

std::string MSILAsmPrinter::getFunctionAddressFieldName(StringRef ILName) {
  return (Twine(MSIL::FnAddrFieldPrefix) + ILName).str();
}

std::string MSILAsmPrinter::getFunctionAddressField(const Function &F) const {
  return getFunctionAddressFieldName(MSIL::getILName(F, TM));
}

//===----------------------------------------------------------------------===//
// MachineFunctionPass Implementation.
//===----------------------------------------------------------------------===//

bool MSILAsmPrinter::doInitialization(Module &M) {
  // The tables belong to one module.
  FuncSigs.clear();
  ExtSymSigs.clear();
  ExtSymOrder.clear();
  CalliSigs.clear();
  ExternFuncs.clear();
  Methods.clear();
  ClashingMethods.clear();
  SigAlloc.DestroyAll();
  Data = std::make_unique<MSILDataSegment>();
  SymToGV.clear();
  SymToGVBuilt = false;
  UsedExternGlobals.clear();
  ExternGlobals.clear();
  Ctors.clear();
  UsedFuncAddrs.clear();
  FuncAddrs.clear();
  LinkedPInvokes.clear();
  LinkedPInvokeNames.clear();
  StartStopFields.clear();
  DataImportVars.clear();
  MCInstLowering = std::make_unique<MSILMCInstLower>(OutContext, *this);
  // CodeGenTargetMachineImpl created the streamer, which decides the mode,
  // before this pass.
  ObjectMode = static_cast<const MSILTargetMachine &>(TM).isObjectMode();
  Data->ObjectMode = ObjectMode;
  MethodRows.clear();
  MethodOrder.clear();
  MemberRefRows.clear();
  SymbolMethodRefs.clear();
  CallStubs.clear();
  CallStubSet.clear();
  CalledValues.clear();
  CalledNames.clear();
  CallABIs.clear();

  // The base class would copy module-level inline assembly into the output
  // verbatim. The legacy pass manager initializes the AsmPrinter before any
  // IR pass runs, so MSILPrepareIR cannot remove it first: diagnose it here
  // (decision D26) and drop it, so the output stays well formed.
  bool Changed = false;
  if (M.hasModuleInlineAsm()) {
    M.getContext().emitError(
        "module-level inline assembly is not supported for MSIL");
    M.removeModuleInlineAsm();
    Changed = true;
  }

  // For a "cfguard" module flag the base class would add a WinCFGuard
  // handler, which switches to the .gfids section at the end of the module;
  // the MSIL object format has none. As for module asm, MSILPrepareIR runs
  // too late: warn and clear the flag here, as for the stack protector
  // (decision D25). The flag keeps its merge behavior.
  if (M.getControlFlowGuardMode() != ControlFlowGuardMode::Disabled) {
    M.getContext().diagnose(DiagnosticInfoGeneric(
        "control flow guard is not supported for MSIL; ignored", DS_Warning));
    SmallVector<Module::ModuleFlagEntry, 8> Flags;
    M.getModuleFlagsMetadata(Flags);
    Module::ModFlagBehavior Behavior = Module::Warning;
    for (const Module::ModuleFlagEntry &Flag : Flags)
      if (Flag.Key->getString() == "cfguard")
        Behavior = Flag.Behavior;
    M.setModuleFlag(Behavior, "cfguard",
                    static_cast<uint32_t>(ControlFlowGuardMode::Disabled));
    Changed = true;
  }
  return AsmPrinter::doInitialization(M) || Changed;
}

bool MSILAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<MSILSubtarget>();
  MFI = MF.getInfo<MSILFunctionInfo>();
  NonDataRefs.clear();

  // The base class would print prefix data, prologue data and the
  // -fsanitize=function signature as raw data in front of the method. In
  // object mode MSILPrepareIR reports and removes them first, keeping the
  // method that the object's calls name; this is its backstop.
  const Function &F = MF.getFunction();
  if (F.hasPrefixData() || F.hasPrologueData() ||
      F.getMetadata(LLVMContext::MD_func_sanitize)) {
    F.getContext().diagnose(DiagnosticInfoUnsupported(
        F, "prefix data, prologue data and function sanitizer metadata are "
           "not supported for MSIL"));
    return false;
  }

  // The IR calls of undefined functions and aliases give the SIGNATURES
  // entries of the symbols they name (plan 5.8.8, rule R3).
  if (ObjectMode)
    collectCallABIs(F);

  AsmPrinter::runOnMachineFunction(MF);
  assert(!InMethod && "emitJumpTableInfo closes every method");
  return false;
}

bool MSILAsmPrinter::doFinalization(Module &M) {
  // The base class prints the module trailer (emitEndOfAsmFile).
  bool Changed = AsmPrinter::doFinalization(M);
  // Only code generation knows which library calls it made, so msil-ld's
  // --print-imports (MSILLinkHooksPass::doFinalization, which runs after
  // this) reads the P/Invokes of its linked text from here (plan 5.10.13,
  // 5.10.14). The IL is already printed; a standalone module, M14's
  // "msil-lto" module included, never gets the node. A node the input
  // already had is replaced.
  if (!MSIL::isLinkedModule(M))
    return Changed;
  LLVMContext &Ctx = M.getContext();
  NamedMDNode *Used = M.getOrInsertNamedMetadata("msil.imports.used");
  Used->clearOperands();
  for (StringRef Name : LinkedPInvokes)
    Used->addOperand(MDNode::get(Ctx, MDString::get(Ctx, Name)));
  return true;
}

void MSILAsmPrinter::emitMethodEnd() {
  assert(InMethod && "no method to close");
  getTargetStreamer().emitMethodEnd(MSIL::getILName(MF->getFunction(), TM));
  InMethod = false;
}

//===----------------------------------------------------------------------===//
// AsmPrinter Implementation.
//===----------------------------------------------------------------------===//

void MSILAsmPrinter::emitStartOfAsmFile(Module &M) {
  // Without a byte order mark ilasm reads the file in the ANSI code page and
  // mangles non-ASCII identifiers and quoted strings (decision D11). The
  // header comments escape non-ASCII bytes, so only the text that is printed
  // raw matters: the assembly name, the global names, and the library and
  // entry point strings of the P/Invoke declarations, which come from the
  // "msil-import-*" attributes (the names of library calls are ASCII) and,
  // in msil-ld's linked text, from the strings of !msil.imports.
  std::string AssemblyName = getAssemblyName(M);
  auto HasNonASCIIImport = [](const Function &F) {
    for (StringRef Kind : {"msil-import-module", "msil-import-name"})
      if (!isASCII(F.getFnAttribute(Kind).getValueAsString()))
        return true;
    return false;
  };
  auto HasNonASCIILinkedImport = [&M]() {
    if (!MSIL::isLinkedModule(M))
      return false;
    // Native data imports print their module and entry as string literals
    // (plan 5.10.12).
    for (const GlobalVariable &GV : M.globals())
      for (StringRef Kind : {"msil-import-module", "msil-import-name"})
        if (!isASCII(GV.getAttribute(Kind).getValueAsString()))
          return true;
    const NamedMDNode *Imports = M.getNamedMetadata("msil.imports");
    if (!Imports)
      return false;
    for (const MDNode *Op : Imports->operands())
      for (const MDOperand &S : Op->operands())
        if (const auto *Str = dyn_cast_or_null<MDString>(S.get());
            Str && !isASCII(Str->getString()))
          return true;
    return false;
  };
  bool HasNonASCIIName =
      any_of(M.global_values(),
             [](const GlobalValue &GV) { return !isASCII(GV.getName()); }) ||
      any_of(M.functions(), HasNonASCIIImport);

  MSILTargetStreamer &TS = getTargetStreamer();
  // An object prints the Module row's name instead of an assembly name, and
  // the import attributes of functions in its stubs (plan 5.8.4).
  if (ObjectMode) {
    TS.emitObjectHeader(HasNonASCIIName || !isASCII(getObjectModuleName(M)),
                        M.getSourceFileName(), TM.getTargetTriple().str());
    return;
  }
  bool BOM =
      !isASCII(AssemblyName) || HasNonASCIIName || HasNonASCIILinkedImport();
  TS.emitFileHeader(BOM, M.getSourceFileName(), TM.getTargetTriple().str());
  TS.emitAssemblyRefs();
  TS.emitAssembly(AssemblyName);
}

void MSILAsmPrinter::emitEndOfAsmFile(Module &M) {
  // An object has no trailer: its symbols, data and constructors go into the
  // link-info record (plan 5.8).
  if (ObjectMode)
    return emitObjectEnd(M);

  // Every global variable has been added (emitGlobalVariable runs for each
  // one before this hook). The layout comes first: a relocation can name a
  // function, whose address field it reads, and an undefined one, whose
  // 'ldftn' targets its P/Invoke declaration, as can a constructor, or an
  // undefined global variable, which is an error unless it is a
  // __start_/__stop_ symbol of an output section or native data.
  Data->finalize(getSectionRules(), getRelocLoopThreshold(M));
  checkSections(M);
  for (const MSILReloc &R : Data->Relocs) {
    if (const auto *F = dyn_cast<Function>(R.Target)) {
      noteFunctionAddress(*F);
      if (F->isDeclarationForLinker())
        noteExternFunction(*F);
    } else if (const auto *GV = dyn_cast<GlobalVariable>(R.Target);
               GV && GV->isDeclarationForLinker()) {
      noteExternGlobal(*GV);
    }
  }
  // finalize() reports a symbol difference with an undefined end; the
  // variable is still an unresolved reference.
  for (const MSILDiff &D : Data->Diffs)
    for (const GlobalValue *GV : {D.A, D.B})
      if (const auto *Var = dyn_cast<GlobalVariable>(GV);
          Var && Var->isDeclarationForLinker())
        noteExternGlobal(*Var);
  collectExternGlobals(M);
  collectFunctionAddresses(M);
  checkAddressFieldNames(M);
  SmallVector<const Function *, 8> CtorFns = collectConstructors(M);

  // ILAsm resolves forward references, so the order only serves the reader
  // (plan section 5.1): the P/Invoke declarations, the data segment with its
  // address fields, the address fields of functions, the module initializer
  // and the entry point.
  emitPInvokeDeclarations(M);
  emitTrailer(M, CtorFns);
}

cli::SectionRules MSILAsmPrinter::getSectionRules() const {
  return TM.getTargetTriple().isOSWindows() ? cli::SectionRules::COFF
                                            : cli::SectionRules::ELF;
}

void MSILAsmPrinter::checkSections(const Module &M) {
  // In IR order, as an object lists its segments.
  SmallVector<const MSILDataSegment::Entry *, 16> Entries;
  for (const MSILDataSegment::Entry &E : Data->Entries)
    Entries.push_back(&E);
  llvm::sort(Entries, [](const MSILDataSegment::Entry *L,
                         const MSILDataSegment::Entry *R) {
    return L->Index < R->Index;
  });
  // The texts of msil-ld (plan 5.9.9, 5.9.14), with the module's source file
  // name as the file. Such globals stay ordinary data (a thread-local
  // section stays out of the image), so the output stays well formed.
  LLVMContext &Ctx = M.getContext();
  StringRef File = M.getSourceFileName();
  cli::SectionRules Rules = getSectionRules();
  for (const MSILDataSegment::Entry *E : Entries) {
    switch (cli::classifySection(E->Section, Rules)) {
    case cli::SectionClass::Data:
    case cli::SectionClass::ConstructorTable:
      break;
    case cli::SectionClass::DestructorTable:
      Ctx.emitError("global destructors are not supported yet: " +
                    MSIL::getILName(*E->GV, TM) + " in " + File);
      break;
    case cli::SectionClass::Unsupported:
      Ctx.emitError("section '" + E->Section + "' in " + File +
                    " is not supported");
      break;
    case cli::SectionClass::TLS:
      // A thread_local variable reaches here only without MSILPrepareIR,
      // whose backstop in emitGlobalVariable has reported it.
      if (!E->GV->isThreadLocal())
        Ctx.emitError("thread-local storage is not supported yet: " +
                      MSIL::getILName(*E->GV, TM) + " in " + File);
      break;
    }
  }
}

const DenseMap<const MCSymbol *, const GlobalValue *> &
MSILAsmPrinter::getSymToGV(const Module &M) {
  // AsmPrinter::lowerConstant names a global value in an initializer by
  // getSymbol, so these are the symbols the data collector sees.
  if (!SymToGVBuilt) {
    for (const GlobalValue &GV : M.global_values())
      if (const auto *F = dyn_cast<Function>(&GV); !F || !F->isIntrinsic())
        SymToGV[getSymbol(&GV)] = &GV;
    SymToGVBuilt = true;
  }
  return SymToGV;
}

/// The native data import of the external variable \p GV, named \p Name, in
/// msil-ld's linked text (plan 5.10.12, 5.10.13): the module and the entry
/// of its import attributes ("msil-import-module", "msil-import-name"; the
/// entry defaults to the name), else of a kind-1 operand of !msil.imports
/// (getListedDataImport).
static std::optional<std::pair<std::string, std::string>>
getLinkedDataImport(const Module &M, const GlobalVariable &GV, StringRef Name) {
  if (StringRef Module =
          GV.getAttribute("msil-import-module").getValueAsString();
      !Module.empty()) {
    StringRef Entry = GV.getAttribute("msil-import-name").getValueAsString();
    return std::make_pair(Module.str(), (Entry.empty() ? Name : Entry).str());
  }
  return getListedDataImport(M, Name);
}

void MSILAsmPrinter::collectExternGlobals(const Module &M) {
  // A variable that another module defines cannot be resolved without a
  // linker (decision D26). Only a reference is an error (Design.md 5.2
  // step 4): a declaration that nothing emitted refers to (unused, or used
  // only by an available_externally function, which is not compiled) is
  // not. Two kinds resolve here, as a one-object link resolves them (plan
  // 5.9.9, 5.10.12): off Windows, __start_<X> and __stop_<X> of an output
  // section <X> (Windows triples keep the error, as native links leave them
  // undefined); and in msil-ld's linked text, native data.
  bool StartStop = !TM.getTargetTriple().isOSWindows();
  bool Linked = MSIL::isLinkedModule(M);
  for (const GlobalVariable &GV : M.globals()) {
    if (!UsedExternGlobals.contains(&GV))
      continue;
    StringRef ILName = MSIL::getILName(GV, TM);
    std::optional<uint64_t> Offset;
    if (StartStop)
      Offset = cli::getStartStopOffset(Data->Layout, ILName);
    std::optional<std::pair<std::string, std::string>> Import;
    if (!Offset && Linked)
      Import = getLinkedDataImport(M, GV, ILName);
    if (Offset)
      StartStopFields.push_back({&GV, *Offset});
    else if (Import)
      DataImportVars.push_back(
          {&GV, std::move(Import->first), std::move(Import->second)});
    else
      GV.getContext().emitError("external global variable '" + ILName +
                                "' is not supported (phase 2 linker)");
    // The address field is a new place where the name is printed (D11).
    if (ILName.size() > MSIL::MaxILNameBytes)
      GV.getContext().emitError(
          "global variable " + MSIL::getIRName(GV) + ": identifier of " +
          Twine(ILName.size()) + " bytes is longer than the " +
          Twine(MSIL::MaxILNameBytes) + " bytes ilasm accepts");
    ExternGlobals.push_back(&GV);
  }
}

void MSILAsmPrinter::collectFunctionAddresses(const Module &M) {
  // A function used only as a callee, the constructors that the module
  // initializer calls included, needs no address. An undefined function that
  // is diagnosed keeps its field, as a referenced extern global does, so the
  // code that reads it stays well formed.
  for (const Function &F : M.functions()) {
    if (!UsedFuncAddrs.contains(&F))
      continue;
    // The address field is a new place where the name is printed (D11). A
    // name that is too long by itself is reported with its function
    // (MSILPrepareIR).
    StringRef ILName = MSIL::getILName(F, TM);
    size_t Size = MSIL::FnAddrFieldPrefix.size() + ILName.size();
    if (ILName.size() <= MSIL::MaxILNameBytes && Size > MSIL::MaxILNameBytes)
      F.getContext().emitError(
          "function " + MSIL::getIRName(F) + ": address field identifier of " +
          Twine(Size) + " bytes is longer than the " +
          Twine(MSIL::MaxILNameBytes) + " bytes ilasm accepts");
    FuncAddrs.push_back(&F);
  }
}

void MSILAsmPrinter::checkAddressFieldNames(const Module &M) {
  // Every address field is a 'native int', so unlike methods, two fields
  // cannot share a name: ilasm rejects the second ('Duplicate field
  // declaration'), and an IL name drops the '\1' of an IR name (@foo and
  // @"\01foo") or comes from the Mangler (@0 is '__unnamed_1'). In module
  // order, so the first of two is the one named in the error.
  SmallPtrSet<const GlobalVariable *, 16> HasField;
  for (const MSILDataSegment::Entry &E : Data->Entries)
    HasField.insert(E.GV);
  HasField.insert_range(ExternGlobals);
  StringMap<const GlobalVariable *> Fields;
  for (const GlobalVariable &GV : M.globals()) {
    if (!HasField.contains(&GV))
      continue;
    StringRef Name = MSIL::getILName(GV, TM);
    if (Name == cli::DataFieldName) {
      GV.getContext().emitError("global variable " + MSIL::getIRName(GV) +
                                " has the reserved IL name '" + Name + "'");
      continue;
    }
    auto [It, Inserted] = Fields.try_emplace(Name, &GV);
    if (!Inserted)
      GV.getContext().emitError("global variable " + MSIL::getIRName(GV) +
                                " has the same IL name '" + Name + "' as " +
                                MSIL::getIRName(*It->second));
  }

  // The address fields of functions are '<msil>fn_<IL name>'. Two functions
  // with one IL name may both have methods (their signatures differ) but not
  // both fields, and an asm label can give a global variable the name of one.
  StringMap<const Function *> FuncFields;
  for (const Function *F : FuncAddrs) {
    std::string Name = getFunctionAddressField(*F);
    auto [It, Inserted] = FuncFields.try_emplace(Name, F);
    if (!Inserted) {
      F->getContext().emitError(
          "function " + MSIL::getIRName(*F) + " has the same IL name '" +
          MSIL::getILName(*F, TM) + "' as " + MSIL::getIRName(*It->second) +
          ", and the addresses of both are used");
      continue;
    }
    if (auto GVIt = Fields.find(Name); GVIt != Fields.end())
      GVIt->second->getContext().emitError(
          "global variable " + MSIL::getIRName(*GVIt->second) +
          " has the IL name '" + Name + "' of the address field of function " +
          MSIL::getIRName(*F));
  }
}

/// True if the 8-byte slot at \p Off of the constructor-table global \p T
/// is NULL: its bytes are zero (a zero-initialized global has none in the
/// image) and no relocation or symbol difference fills them. msil-ld's
/// isNullSlot sees both as relocations of the object's segment and takes
/// each as 8 bytes wide, so one that starts up to 7 bytes before the slot
/// counts here too, and the two agree on every slot.
static bool
isNullConstructorSlot(const MSILDataSegment &Data,
                      const MSILDataSegment::Entry &T, uint64_t Off,
                      const DenseMap<uint64_t, const MSILReloc *> &RelocAt) {
  if (T.Size - Off < 8)
    return false;
  uint64_t Begin = T.Offset + (Off < 7 ? 0 : Off - 7);
  uint64_t End = T.Offset + Off + 8;
  for (uint64_t Site = Begin; Site != End; ++Site)
    if (RelocAt.count(Site))
      return false;
  if (any_of(Data.Diffs, [&](const MSILDiff &D) {
        return D.Site >= Begin && D.Site < End;
      }))
    return false;
  if (T.IsZero)
    return true;
  ArrayRef<uint8_t> Image = Data.getImage();
  if (End > Image.size())
    return false;
  return all_of(Image.slice(T.Offset + Off, 8),
                [](uint8_t B) { return B == 0; });
}

SmallVector<const Function *, 8>
MSILAsmPrinter::collectConstructors(const Module &M) {
  cli::SectionRules Rules = getSectionRules();
  // The constructors with their keys and positions (plan 5.9.9), sorted at
  // the end; the Id of an entry is its function's index in Fns.
  SmallVector<cli::ConstructorEntry, 8> Entries;
  SmallVector<const Function *, 8> Fns;
  // The module initializer calls each constructor without arguments (C's
  // __attribute__((constructor)) functions take none), or off Windows with
  // argc, argv and envp as glibc calls an .init_array entry
  // (getConstructorArgs), and drops a result.
  const Triple &TT = TM.getTargetTriple();
  auto Add = [&](const Function &F, cli::ConstructorEntry E) {
    if (!getConstructorArgs(getSignature(F), TT)) {
      F.getContext().emitError("global constructor '" + MSIL::getILName(F, TM) +
                               "' takes arguments, which MSIL does not pass");
      return;
    }
    if (F.isDeclarationForLinker())
      noteExternFunction(F);
    E.Id = Fns.size();
    Fns.push_back(&F);
    Entries.push_back(std::move(E));
  };

  // llvm.global_ctors, sorted by priority (preprocessXXStructorList keeps
  // the array order of equal priorities): the INIT entries.
  for (unsigned I = 0, E = Ctors.size(); I != E; ++I) {
    const Structor &S = Ctors[I];
    // MSILPrepareIR replaces a diagnosed alias or ifunc by poison.
    if (isa<UndefValue>(S.Func))
      continue;
    const auto *F = dyn_cast<Function>(S.Func->stripPointerCasts());
    if (!F || F->isIntrinsic()) {
      S.Func->getContext().emitError(
          "a global constructor that is not a function is not supported for "
          "MSIL");
      continue;
    }
    cli::ConstructorEntry Entry;
    Entry.Key = cli::getConstructorKey(S.Priority, Rules);
    Entry.Offset = I;
    Add(*F, std::move(Entry));
  }

  // Every 8-byte slot of a constructor-table section (.init_array*,
  // .CRT$XC*) is a constructor too: the address of a function, which the
  // native startup code would call (plan 5.8.10, 5.9.9).
  DenseMap<uint64_t, const MSILReloc *> RelocAt;
  for (const MSILReloc &R : Data->Relocs)
    RelocAt.try_emplace(R.Site, &R);
  SmallVector<const MSILDataSegment::Entry *, 4> Tables;
  for (const MSILDataSegment::Entry &E : Data->Entries)
    if (E.InImage && cli::classifySection(E.Section, Rules) ==
                         cli::SectionClass::ConstructorTable)
      Tables.push_back(&E);
  llvm::sort(Tables, [](const MSILDataSegment::Entry *L,
                        const MSILDataSegment::Entry *R) {
    return L->Index < R->Index;
  });
  for (const MSILDataSegment::Entry *T : Tables) {
    for (uint64_t Off = 0; Off < T->Size; Off += 8) {
      const MSILReloc *R =
          T->Size - Off >= 8 ? RelocAt.lookup(T->Offset + Off) : nullptr;
      const auto *F =
          R && R->Size == 8 ? dyn_cast<Function>(R->Target) : nullptr;
      // A NULL slot is skipped on Windows triples, as the MSVC CRT's
      // _initterm skips it; glibc calls every .init_array entry, so ELF
      // triples keep the error (msil-ld agrees, section 12 'M21.b extended
      // (M20 follow-ups)').
      if (!R && cli::skipsNullConstructorSlots(Rules) &&
          isNullConstructorSlot(*Data, *T, Off, RelocAt))
        continue;
      if (!F || F->isIntrinsic()) {
        M.getContext().emitError(
            "constructor table entry at " + T->Section + "+" + Twine(Off) +
            " in " + M.getSourceFileName() + " is not a function address");
        continue;
      }
      cli::ConstructorEntry Entry;
      Entry.Key = cli::getConstructorKey(T->Section, Rules);
      Entry.IsSlot = true;
      Entry.Segment = T->Index;
      Entry.Offset = Off;
      Add(*F, std::move(Entry));
    }
  }

  cli::sortConstructors(Entries);
  SmallVector<const Function *, 8> Sorted;
  for (const cli::ConstructorEntry &E : Entries)
    Sorted.push_back(Fns[E.Id]);
  return Sorted;
}

/// Whether a native C function passes a parameter or returns a result of type
/// \p Ty exactly as the one CIL value that MSIL::computeSignatureVTs makes of
/// it, so that a P/Invoke of the function can pass it: an integer of at most
/// 64 bits (int32 or native int), a pointer, float or double. ISel splits or
/// demotes wider integers, vectors and aggregates, unlike the native calling
/// convention (a { i64, i64 } result comes back in two registers on x86-64
/// Linux, not through a pointer), and promotes half.
static bool isNativeScalar(const Type *Ty) {
  if (const auto *ITy = dyn_cast<IntegerType>(Ty))
    return ITy->getBitWidth() <= 64;
  return Ty->isPointerTy() || Ty->isFloatTy() || Ty->isDoubleTy();
}

/// Whether the P/Invoke of the undefined function \p F passes its arguments
/// and result as the native function expects them.
static bool hasNativeSignature(const Function &F) {
  const FunctionType *FTy = F.getFunctionType();
  if (!FTy->getReturnType()->isVoidTy() &&
      !isNativeScalar(FTy->getReturnType()))
    return false;
  for (unsigned I = 0, E = FTy->getNumParams(); I != E; ++I) {
    if (!isNativeScalar(FTy->getParamType(I)))
      return false;
    // The native callee finds a by-value aggregate in its argument area, but
    // the method would pass the address of a copy. The address of an
    // aggregate result (sret) goes in a register of its own on some targets
    // (X8 on AArch64), but the method passes it as an ordinary first
    // argument. A static chain (nest) has a register of its own too.
    if (F.hasParamAttribute(I, Attribute::ByVal) ||
        F.hasParamAttribute(I, Attribute::InAlloca) ||
        F.hasParamAttribute(I, Attribute::Preallocated) ||
        F.hasParamAttribute(I, Attribute::StructRet) ||
        F.hasParamAttribute(I, Attribute::Nest))
      return false;
  }
  return true;
}

/// Whether \p M is the merged module of msil-ld's LTO, on which msil-ld sets
/// the module flag "msil-lto" (i32 1). A value that is not an integer constant
/// counts as absent.
static bool isMSILLDModule(const Module &M) {
  const auto *Flag =
      mdconst::dyn_extract_or_null<ConstantInt>(M.getModuleFlag("msil-lto"));
  return Flag && !Flag->isZero();
}

/// Reports the undefined function or library call \p Name of a standalone
/// module, which is not a Phase-1 import of the standalone import table
/// (decision D14, frozen): no linker resolves it. An undefined function of
/// the module gets the same text whatever its name, the compiler-rt and
/// __atomic_* helpers that clang and AtomicExpand declare included. In
/// msil-ld's LTO module without msil-ld's import annotations (\p LTO: the
/// module flag "msil-lto" without "msil-linked", as M14 wrote it) the linker
/// has bound every symbol it could, so such a function is msil-ld's
/// 'undefined symbol' (plan 5.9.6). A library call (\p IsLibCall) gets the
/// text of LowerCall's library-call policy, which rejects such calls before
/// they are built, so that is only a backstop.
static void reportNotImported(LLVMContext &Ctx, StringRef Name, bool IsLibCall,
                              bool LTO) {
  if (!IsLibCall && LTO)
    Ctx.emitError("undefined symbol: " + Name);
  else if (!IsLibCall)
    Ctx.emitError("undefined function '" + Name +
                  "' is not defined in this module and is not a Phase-1 "
                  "import (puts, putchar, the C math library, "
                  "msil-import-module); Phase 1 has no linker");
  else if (Name.starts_with("__"))
    Ctx.emitError("runtime helper '" + Name +
                  "' is not available for MSIL yet (phase 3)");
  else
    Ctx.emitError("library call '" + Name + "' is not a Phase-1 import");
}

void MSILAsmPrinter::emitPInvokeDeclarations(const Module &M) {
  const Triple &TT = TM.getTargetTriple();
  LLVMContext &Ctx = M.getContext();
  MSILTargetStreamer &TS = getTargetStreamer();
  // msil-ld's linked text (plan 5.10.13) takes every import from msil-ld's
  // annotations (MSIL::getLinkedImport) and never consults the standalone
  // import table; its P/Invokes are 'assembly' (plan 5.10.11). Any other
  // module, msil-ld's LTO module without the annotations included, is
  // standalone.
  bool Linked = MSIL::isLinkedModule(M);
  bool LTO = !Linked && isMSILLDModule(M);

  // One declaration per method: a library call and a call of a declared
  // function of the same name and signature (sqrt from C and from the
  // legalizer) share it. CIL methods may differ in their signature only.
  std::set<std::pair<StringRef, MSIL::Signature>> Declared;
  auto Declare = [&](const Function *F, StringRef Name,
                     const MSIL::Signature &Sig, StringRef Lib,
                     StringRef Entry) {
    if (isReservedMethodName(Name)) {
      assert(F && "library calls have no constructor names");
      reportReservedMethodName(*F, Name);
      return;
    }
    if (!Declared.insert({Name, Sig}).second)
      return;
    // The symbol name (the IL name) of each P/Invoke of linked text, once
    // per name, for !msil.imports.used (doFinalization).
    if (Linked) {
      auto [It, New] = LinkedPInvokeNames.insert(Name);
      if (New)
        LinkedPInvokes.push_back(It->getKey());
    }
    if (Declared.size() == 1)
      TS.emitComment("---- undefined external functions: P/Invoke ----");
    // Blittable parameters only (int32, native int, float32, float64), so
    // 'preservesig' is all the marshaller needs (decision D10).
    TS.emitPInvoke({Name, &Sig, /*Public=*/!Linked}, Lib, Entry);
  };

  for (const auto &[F, Sig] : ExternFuncs) {
    StringRef Name = MSIL::getILName(*F, TM);
    // A function that this module defines with the same IL name and
    // signature is the one a native link would bind the symbol to. The calls
    // and 'ldftn's of F name a method by its name and signature, so they
    // already reach that method, and a P/Invoke would duplicate it.
    if (Methods.count({Name, *Sig}))
      continue;
    if (Linked) {
      // msil-ld reports an unresolved symbol first; this is a backstop. A
      // native callee is checked after native resolution, so the texts name
      // its module (plan 5.10.13, 5.10.14); LowerCall reports the calls of a
      // variadic one.
      // AtomicExpand declares the __atomic_* helpers during code generation,
      // after msil-ld's import analysis: like every '__' helper they wait
      // for the phase-3 runtime library, even when !msil.imports lists one
      // (plan 5.10.13), as in the library-call loop below and in LowerCall.
      // A '__' declaration that msil-ld bound carries its import attributes
      // (__errno_location) and is a P/Invoke.
      if (Name.starts_with("__") && !MSIL::hasImportModule(F)) {
        Ctx.emitError("runtime helper '" + Name +
                      "' is not available for MSIL yet (phase 3)");
        continue;
      }
      std::optional<MSIL::NativeImport> Import =
          MSIL::getLinkedImport(M, Name, F);
      if (!Import) {
        Ctx.emitError("undefined function '" + Name +
                      "' was not resolved by msil-ld");
        continue;
      }
      if (F->isVarArg()) {
        Ctx.emitError("native function '" + Name + "' (" + Import->Module +
                      ") is variadic; calls of native variadic functions are "
                      "not supported for MSIL yet (phase 3)");
        continue;
      }
      if (!hasNativeSignature(*F)) {
        Ctx.emitError("native function '" + Name + "' (" + Import->Module +
                      ") takes or returns a value that a P/Invoke cannot "
                      "pass natively yet (phase 3)");
        continue;
      }
      Declare(F, Name, *Sig, Import->Module, Import->Entry);
      continue;
    }
    // The variable arguments of a native variadic function need the calling
    // convention support of phase 3 (decisions D14 and D19).
    if (F->isVarArg()) {
      Ctx.emitError("undefined variadic function '" + Name +
                    "' is not supported for MSIL yet (phase 3)");
      continue;
    }
    std::optional<StringRef> Lib = MSIL::getPInvokeModule(TT, Name, F);
    if (!Lib) {
      reportNotImported(Ctx, Name, /*IsLibCall=*/false, LTO);
      continue;
    }
    // Only scalars cross the boundary as the native calling convention
    // passes them (the imports of D14 take and return nothing else).
    if (!hasNativeSignature(*F)) {
      Ctx.emitError("imported function '" + Name +
                    "' has a parameter or result that a P/Invoke cannot pass "
                    "natively (aggregates, vectors, 128-bit integers and half "
                    "are not supported for MSIL yet)");
      continue;
    }
    Declare(F, Name, *Sig, *Lib, MSIL::getPInvokeEntry(Name, F));
  }

  for (const auto &[Name, Sig] : ExtSymOrder) {
    // A library call of a method that this module defines (by IL name and
    // signature) calls that method, as a native link would resolve it.
    if (Methods.count({Name, *Sig}))
      continue;
    // A function of the same IL name takes part only if its method has this
    // signature, as in LowerCall (matchesSignature): a declaration supplies
    // its "msil-import-*" attributes. Otherwise, in a standalone module, the
    // library and the entry point come from the import table alone.
    const Function *Named = MSIL::getFunctionByILName(M, Name);
    if (Named && Named->isIntrinsic())
      Named = nullptr;
    const Function *F = Named && getSignature(*Named) == *Sig ? Named : nullptr;
    if (F && !F->isDeclarationForLinker())
      continue;
    if (Linked) {
      // LowerCall builds a library call only for an import; this is a
      // backstop. A symbol binds to one native function, whatever the
      // signature, so a declaration of that name supplies the module and the
      // entry point even when its method has another signature, as in
      // LowerCall.
      const Function *Decl =
          Named && Named->isDeclarationForLinker() ? Named : nullptr;
      if (Name.starts_with("__") && !MSIL::hasImportModule(Decl)) {
        Ctx.emitError("runtime helper '" + Name +
                      "' is not available for MSIL yet (phase 3)");
        continue;
      }
      std::optional<MSIL::NativeImport> Import =
          MSIL::getLinkedImport(M, Name, Decl);
      if (!Import) {
        Ctx.emitError("library call '" + Name +
                      "' was not resolved by msil-ld");
        continue;
      }
      Declare(F, Name, *Sig, Import->Module, Import->Entry);
      continue;
    }
    // LowerCall builds a library call only for a Phase-1 import (D14).
    std::optional<StringRef> Lib = MSIL::getPInvokeModule(TT, Name, F);
    if (!Lib) {
      reportNotImported(Ctx, Name, /*IsLibCall=*/true, LTO);
      continue;
    }
    Declare(F, Name, *Sig, *Lib, MSIL::getPInvokeEntry(Name, F));
  }

  // The native functions of the data-import helper (plan 5.10.12), which
  // msil-ld binds like imports and lists as kind-0 operands of
  // !msil.imports. Without one the data imports stay unset; msil-ld reports
  // that first ('cannot be imported'), so this is a backstop.
  if (!DataImportVars.empty()) {
    bool Bound = true;
    for (const auto &[Name, Sig] : getDataImportFunctions(TT)) {
      const Function *Named = MSIL::getFunctionByILName(M, Name);
      const Function *Decl =
          Named && Named->isDeclarationForLinker() ? Named : nullptr;
      std::optional<MSIL::NativeImport> Import =
          MSIL::getLinkedImport(M, Name, Decl);
      if (!Import) {
        Ctx.emitError("library call '" + Name +
                      "' was not resolved by msil-ld");
        Bound = false;
        continue;
      }
      const MSIL::Signature &S = internSignature(Sig);
      if (!Methods.count({Name, S}))
        Declare(Decl, Name, S, Import->Module, Import->Entry);
    }
    if (!Bound)
      DataImportVars.clear();
  }
  if (!Declared.empty())
    TS.emitBlankLine();
}

void MSILAsmPrinter::emitDataSegment(const MSILTrailerTextSink &Sink) {
  MSILTargetStreamer &TS = getTargetStreamer();
  if (!Data->Entries.empty()) {
    uint64_t InitSize = Data->InitSize;
    uint64_t TotalSize = Data->TotalSize;
    TS.emitComment("---- data segment: " + Twine(TotalSize) + " bytes (" +
                   Twine(InitSize) + " initialized + " +
                   Twine(TotalSize - InitSize) + " zero), align " +
                   Twine(Data->MaxAlign.value()) + " ----");
    // The image and the relocation table are only read (the module
    // initializer copies the image and walks the table): RVA memory is never
    // written, and it never holds '&(label)' (decision D12).
    if (ArrayRef<uint8_t> Image = Data->getImage(); !Image.empty())
      TS.emitDataImage(cli::ImageTypeName, cli::ImageFieldName, ImageLabel,
                       Image);
    if (ArrayRef<uint8_t> Relocs = Sink.getRVAData(cli::RelocsFieldName);
        !Relocs.empty())
      TS.emitDataImage(cli::RelocsTypeName, cli::RelocsFieldName, RelocsLabel,
                       Relocs);
    TS.emitAddressField(cli::DataFieldName, /*Public=*/false,
                        /*Generated=*/true);
    for (const MSILDataSegment::Entry &E : Data->Entries) {
      if (isVerbose() && E.InImage)
        OutStreamer->AddComment("+" + Twine(E.Offset));
      TS.emitAddressField(MSIL::getILName(*E.GV, TM),
                          /*Public=*/!E.GV->hasLocalLinkage());
    }
  }
  // Referenced declarations: __start_/__stop_ symbols of output sections,
  // native data in msil-ld's linked text, whose fields
  // '<msil>init_dataimports' stores (so they are not initonly), and errors
  // (collectExternGlobals). The fields of the errors keep the code and the
  // module initializer that refer to them well formed; nothing sets them.
  SmallPtrSet<const GlobalVariable *, 2> Imported;
  for (const DataImportVar &V : DataImportVars)
    Imported.insert(V.GV);
  for (const GlobalVariable *GV : ExternGlobals) {
    if (Imported.contains(GV))
      TS.emitDataImportField(MSIL::getILName(*GV, TM));
    else
      TS.emitAddressField(MSIL::getILName(*GV, TM), /*Public=*/true);
  }
  if (!Data->Entries.empty() || !ExternGlobals.empty())
    TS.emitBlankLine();
}

void MSILAsmPrinter::emitFunctionAddressFields() {
  MSILTargetStreamer &TS = getTargetStreamer();
  for (const Function *F : FuncAddrs)
    TS.emitAddressField(getFunctionAddressField(*F), /*Public=*/false,
                        /*Generated=*/true);
  if (!FuncAddrs.empty())
    TS.emitBlankLine();
}

void MSILAsmPrinter::emitTrailer(Module &M,
                                 ArrayRef<const Function *> CtorFns) {
  LLVMContext &Ctx = M.getContext();
  MSILTrailerTextSink Sink(*this);
  cli::TrailerInput In;
  In.RelocLoopThreshold = getRelocLoopThreshold(M);

  // The member references are the operands code uses (MSILMemberRefExpr):
  // one entity per field, so that the relocation loop has one address slot
  // per field and addend, as MSILDataSegment::finalize reserved them.
  DenseMap<const GlobalValue *, cli::EntityRef> FieldOf;
  auto GlobalField = [&](const GlobalVariable &GV) {
    auto [It, Inserted] = FieldOf.try_emplace(&GV, 0);
    if (Inserted) {
      StringRef Name = MSIL::getILName(GV, TM);
      It->second =
          Sink.add(GV.isDeclarationForLinker()
                       ? MSILMemberRefExpr::createExternField(Name, OutContext)
                       : MSILMemberRefExpr::createField(Name, OutContext));
    }
    return It->second;
  };
  auto FnAddrField = [&](const Function &F) {
    auto [It, Inserted] = FieldOf.try_emplace(&F, 0);
    if (Inserted)
      It->second = Sink.add(
          MSILMemberRefExpr::createFnAddr(MSIL::getILName(F, TM), OutContext));
    return It->second;
  };
  auto MethodRef = [&](const Function &F) {
    return Sink.add(MSILMCInstLower::createMethodRef(
        F, MSIL::getILName(F, TM), getSignature(F), OutContext));
  };

  if (!Data->Entries.empty()) {
    // Sizes and offsets are int32 constants: cpblk and initblk take an
    // unsigned int32 size, and 'add' an int32 offset (Phase 1's limit and
    // text; the trailer is still written, so the output stays well formed).
    if (!isInt<32>(Data->TotalSize + Data->MaxAlign.value() - 1))
      Ctx.emitError("the data segment of " + Twine(Data->TotalSize) +
                    " bytes is larger than MSIL supports (2 GiB)");
    In.Image = &Data->Layout;

    // The address field of every global, which code reads (GLOBAL_ADDR), and
    // of the __start_/__stop_ symbols, in image-offset order.
    DenseMap<const GlobalVariable *, uint64_t> ImageOffset;
    SmallPtrSet<const GlobalVariable *, 16> HasField;
    for (const MSILDataSegment::Entry &E : Data->Entries) {
      HasField.insert(E.GV);
      if (!E.InImage)
        continue;
      In.AddressFields.push_back({GlobalField(*E.GV), E.Offset});
      ImageOffset.try_emplace(E.GV, E.Offset);
    }
    for (const auto &[GV, Offset] : StartStopFields)
      In.AddressFields.push_back({GlobalField(*GV), Offset});
    llvm::stable_sort(In.AddressFields, [](const auto &L, const auto &R) {
      return L.second < R.second;
    });
    HasField.insert_range(ExternGlobals);

    // The relocations, in site order: the address of a global plus its
    // addend (its field, and its offset when it lives in the image), or the
    // address field of a function.
    for (const MSILReloc &R : Data->Relocs) {
      cli::RuntimeReloc X;
      X.Site = R.Site;
      X.Width = R.Size;
      if (const auto *F = dyn_cast<Function>(R.Target)) {
        assert(R.Addend == 0 && "the collector rejects function + offset");
        X.Field = FnAddrField(*F);
      } else {
        const auto *GV = dyn_cast<GlobalVariable>(R.Target);
        if (!GV || !HasField.contains(GV))
          Ctx.emitError("the initializer of a global variable refers to '" +
                        MSIL::getILName(*R.Target, TM) +
                        "', which is not program data");
        if (GV) {
          X.Field = GlobalField(*GV);
          if (auto It = ImageOffset.find(GV); It != ImageOffset.end())
            X.ImageOffset = It->second;
        } else {
          auto [It, Inserted] = FieldOf.try_emplace(R.Target, 0);
          if (Inserted)
            It->second = Sink.add(MSILMemberRefExpr::createField(
                MSIL::getILName(*R.Target, TM), OutContext));
          X.Field = It->second;
        }
        X.Addend = R.Addend;
      }
      In.Relocs.push_back(X);
    }
  }

  // The address field of every function whose address is used, set with
  // the module's only 'ldftn' of the function, so every pointer to it has
  // one value; an undefined function's is the 'ldftn' of its P/Invoke
  // declaration (M9).
  for (const Function *F : FuncAddrs)
    In.FnAddrFields.push_back({FnAddrField(*F), MethodRef(*F)});

  // Native data (plan 5.10.12), through the helper's native functions.
  if (!DataImportVars.empty()) {
    const Triple &TT = TM.getTargetTriple();
    cli::DataImportHelper H;
    H.K = TT.isOSWindows() ? cli::DataImportHelper::Kind::Kernel32
                           : cli::DataImportHelper::Kind::Dlfcn;
    auto Functions = getDataImportFunctions(TT);
    auto HelperRef = [&](unsigned I) {
      return Sink.add(MSILMemberRefExpr::createMethod(
          Functions[I].first, &internSignature(Functions[I].second),
          OutContext));
    };
    H.Load = HelperRef(0);
    H.GetSymbol = HelperRef(1);
    In.ImportHelper = H;
    for (const DataImportVar &V : DataImportVars)
      In.DataImports.push_back({GlobalField(*V.GV), V.Module, V.Entry});
  }

  // The constructors last, in run order: the data they may touch is in
  // place. Off Windows a constructor may take argc, argv and envp
  // (collectConstructors checked the shapes); envp is the value of the C
  // library's environ, a data import: the module's own import of it, else
  // one that this module adds (plan section 12, 'M22.b extended', defect 4).
  bool TakesEnvp = false;
  for (const Function *F : CtorFns) {
    const MSIL::Signature &Sig = getSignature(*F);
    In.Ctors.push_back({MethodRef(*F), !Sig.Results.empty()});
    TakesEnvp |= Sig.Params.size() == 3;
  }
  if (any_of(CtorFns, [&](const Function *F) {
        return !getSignature(*F).Params.empty();
      }))
    for (const Function *F : CtorFns)
      In.CtorArgs.push_back(getSignature(*F).Params.size());
  // The field of a data import of environ that the module does not make.
  std::optional<std::string> EnvironImportField;
  if (TakesEnvp) {
    for (const DataImportVar &V : DataImportVars)
      if (MSIL::getILName(*V.GV, TM) == "environ")
        In.EnvironField = GlobalField(*V.GV);
  }
  if (TakesEnvp && !In.EnvironField) {
    // The module's own import of environ: as msil-ld's linked text lists it,
    // else from the C library, as a native link binds it
    // (getCLibrarySoname). Its helper's native functions likewise, when no
    // other data import declared them (plan 5.10.12).
    const Triple &TT = TM.getTargetTriple();
    bool Linked = MSIL::isLinkedModule(M);
    StringRef CLib = getCLibrarySoname(TT);
    std::pair<std::string, std::string> Import = {CLib.str(), "environ"};
    if (std::optional<std::pair<std::string, std::string>> Listed =
            Linked ? getListedDataImport(M, "environ") : std::nullopt)
      Import = std::move(*Listed);
    if (!In.ImportHelper) {
      auto Functions = getDataImportFunctions(TT);
      cli::DataImportHelper H;
      H.K = cli::DataImportHelper::Kind::Dlfcn;
      MSILTargetStreamer &TS = getTargetStreamer();
      // A method of the same name and signature that the module defines or
      // that emitPInvokeDeclarations declared already.
      auto IsDeclared = [&](StringRef Name, const MSIL::Signature &S) {
        return Methods.count({Name, S}) ||
               any_of(ExternFuncs,
                      [&](const auto &P) {
                        return *P.second == S &&
                               MSIL::getILName(*P.first, TM) == Name;
                      }) ||
               any_of(ExtSymOrder, [&](const auto &P) {
                 return P.first == Name && *P.second == S;
               });
      };
      for (unsigned I = 0; I != 2; ++I) {
        StringRef Name = Functions[I].first;
        const MSIL::Signature &S = internSignature(Functions[I].second);
        std::optional<MSIL::NativeImport> Helper =
            Linked ? MSIL::getLinkedImport(M, Name, nullptr) : std::nullopt;
        if (!IsDeclared(Name, S)) {
          // Recorded for !msil.imports.used, as emitPInvokeDeclarations
          // records the P/Invokes that msil-ld bound.
          if (Helper) {
            auto [It, New] = LinkedPInvokeNames.insert(Name);
            if (New)
              LinkedPInvokes.push_back(It->getKey());
          }
          TS.emitPInvoke({Name, &S, /*Public=*/!Linked},
                         Helper ? Helper->Module : CLib,
                         Helper ? Helper->Entry : Name);
        }
        cli::EntityRef Ref =
            Sink.add(MSILMemberRefExpr::createMethod(Name, &S, OutContext));
        (I == 0 ? H.Load : H.GetSymbol) = Ref;
      }
      In.ImportHelper = H;
    }
    EnvironImportField = "<msil>environ";
    In.EnvironField = Sink.add(
        MSILMemberRefExpr::createField(*EnvironImportField, OutContext));
    In.DataImports.push_back(
        {In.EnvironField, std::move(Import.first), std::move(Import.second)});
  }

  // The entry function is main, or under msil-ld --lto-emit-asm --entry=<sym>
  // the function that the module flag msil-entry names (plan 5.9.3, section
  // 12 row 'M20.a (4) and M20.b (6)-(7) clarified'). Its CIL signature has
  // one of the shapes of C's two portable mains, 'int main(void)' and 'int
  // main(int, char **)' (decision D27, decided on the CIL signature as
  // msil-ld's writer decides it, section 12 row 'D27 clarified'): so a narrow
  // result (_Bool, char or short main), an int32 in CIL, qualifies, as does
  // an IR (i32, i64) entry on a 64-bit triple. An available_externally entry
  // has no method (no MachineFunction is emitted for it), so it gets no
  // entry point either.
  // In msil-ld's linked text the shape error is msil-ld's PE text, without
  // the location prefix of DiagnosticInfoUnsupported (plan 5.9.14), and an
  // entry that the flag names but the module does not define as a function
  // (a variable, or nothing) is that error too, as in the PE (5.9.12).
  StringRef EntryName = "main";
  const auto *EntryFlag =
      dyn_cast_or_null<MDString>(M.getModuleFlag("msil-entry"));
  if (EntryFlag)
    EntryName = EntryFlag->getString();
  std::string EntryError = ("unsupported signature for " + EntryName).str();
  const Function *Main = M.getFunction(EntryName);
  if (EntryFlag && (!Main || Main->isDeclarationForLinker()))
    Ctx.diagnose(DiagnosticInfoGeneric(EntryError));
  if (Main && !Main->isDeclarationForLinker()) {
    // The call spells the entry's signature exactly as its method header
    // does.
    const MSIL::Signature &Sig = getSignature(*Main);
    unsigned PointerSize = Main->getDataLayout().getPointerSize();
    std::optional<bool> HasArgs = cli::getEntryPointHasArgs(
        MSIL::toMethodSig(Sig), Main->isVarArg(), PointerSize);
    if (!HasArgs) {
      if (MSIL::isLinkedModule(M))
        Ctx.diagnose(DiagnosticInfoGeneric(EntryError));
      else
        Ctx.diagnose(DiagnosticInfoUnsupported(*Main, EntryError));
    } else {
      cli::EntryPoint E;
      E.Main = Sink.add(MSILMemberRefExpr::createMethod(
          MSIL::getILName(*Main, TM), &Sig, OutContext));
      E.HasArgs = *HasArgs;
      E.PointerSize = PointerSize;
      In.Entry = E;
    }
  }

  cli::buildTrailer(In, Sink);
  emitDataSegment(Sink);
  if (EnvironImportField) {
    getTargetStreamer().emitDataImportField(*EnvironImportField);
    getTargetStreamer().emitBlankLine();
  }
  // '<msil>argc' and '<msil>argv', in which the module initializer leaves
  // the argc and argv of the constructors for '<msil>Main'. Their names are
  // reserved when they exist, like cli::DataFieldName
  // (checkAddressFieldNames): an asm label can give a global variable one.
  if (ArrayRef<StringRef> Fields = Sink.getTrailerFields(); !Fields.empty()) {
    for (StringRef Name : Fields) {
      auto CheckName = [&](const GlobalVariable &GV) {
        if (MSIL::getILName(GV, TM) == Name)
          Ctx.emitError("global variable " + MSIL::getIRName(GV) +
                        " has the reserved IL name '" + Name + "'");
      };
      for (const MSILDataSegment::Entry &E : Data->Entries)
        CheckName(*E.GV);
      for (const GlobalVariable *GV : ExternGlobals)
        CheckName(*GV);
      getTargetStreamer().emitAddressField(Name, /*Public=*/false,
                                           /*Generated=*/true);
    }
    getTargetStreamer().emitBlankLine();
  }
  emitFunctionAddressFields();
  Sink.emitMethods();
}

void MSILAsmPrinter::emitLinkage(const GlobalValue *GV, MCSymbol *GVSym) const {
  // ILAsm has no linkage directives: the access of a method or field
  // ('public' or 'assembly') is part of its declaration.
}

void MSILAsmPrinter::emitConstantPool() {
  // FP immediates are legal (ldc.r4, ldc.r8), so nothing is ever placed in a
  // constant pool, and ILAsm could not express one.
  if (!MF->getConstantPool()->isEmpty())
    reportFatalInternalError("MSIL: constant pools are not supported");
}

void MSILAsmPrinter::emitJumpTableInfo() {
  // MSIL has no out-of-line jump tables (BR_TABLE_I32 prints 'switch' with its
  // targets inline), so this hook closes the method: the brace must follow
  // the body (plan 5.1, decision D29). AsmPrinter::emitFunctionBody calls it
  // for every function, after the end-of-function label, which the base
  // class prints after emitFunctionBodyEnd whenever the function has begin
  // and end labels (always with -g), and before the post-function sections,
  // the '-- End function' comment and the blank line. Closing here keeps that
  // label inside the method and puts the brace on the very next line.
  emitMethodEnd();
}

/// The llvm.global_ctors list \p List as AsmPrinter::preprocessXXStructorList
/// reads it, which casts every element to ConstantStruct: the elements before
/// the first one that is null as a whole (zeroinitializer, whose null
/// function ends the list, as it ends the object writer's INIT entries),
/// without the elements that are undef or poison as a whole (skipped, as the
/// INIT entries skip them). \p List itself when it has neither.
static const Constant *getStructorList(const Constant *List) {
  const auto *Array = dyn_cast<ConstantArray>(List);
  if (!Array)
    return List;
  SmallVector<Constant *, 8> Elements;
  bool Changed = false;
  for (const Use &Op : Array->operands()) {
    auto *C = cast<Constant>(Op.get());
    if (C->isNullValue()) {
      Changed = true;
      break;
    }
    if (!isa<ConstantStruct>(C)) {
      Changed = true;
      continue;
    }
    Elements.push_back(C);
  }
  if (!Changed)
    return List;
  return ConstantArray::get(
      ArrayType::get(Array->getType()->getElementType(), Elements.size()),
      Elements);
}

void MSILAsmPrinter::emitGlobalVariable(const GlobalVariable *GV) {
  StringRef Name = GV->getName();
  // The module initializer calls the constructors (decision D12); an object
  // lists them in the record, in array order (emitObjectEnd).
  if (Name == "llvm.global_ctors") {
    if (GV->hasInitializer() && !ObjectMode)
      preprocessXXStructorList(GV->getDataLayout(),
                               getStructorList(GV->getInitializer()), Ctors);
    return;
  }
  // MSILPrepareIR reports and erases a non-empty llvm.global_dtors; this is
  // the backstop for a pipeline without it.
  if (Name == "llvm.global_dtors") {
    if (GV->hasInitializer() && !GV->getInitializer()->isNullValue())
      GV->getContext().emitError("global destructors (llvm.global_dtors) are "
                                 "not supported for MSIL yet");
    return;
  }
  // llvm.used, llvm.compiler.used and data in the llvm.metadata section are
  // not program data. Any other llvm.* name (a C asm label can spell one) is
  // an ordinary global.
  if (MSIL::isSpecialLLVMGlobal(*GV))
    return;

  // A variable that another module defines gets no data; an
  // available_externally definition counts as such a declaration, as for
  // functions. MSILMCInstLower and emitEndOfAsmFile note the ones that the
  // code and the data refer to, and collectExternGlobals reports those.
  if (GV->isDeclarationForLinker())
    return;

  // The address field is a new place where the name is printed (D11).
  StringRef ILName = MSIL::getILName(*GV, TM);
  if (ILName.size() > MSIL::MaxILNameBytes)
    GV->getContext().emitError(
        "global variable " + MSIL::getIRName(*GV) + ": identifier of " +
        Twine(ILName.size()) + " bytes is longer than the " +
        Twine(MSIL::MaxILNameBytes) + " bytes ilasm accepts");

  // MSILPrepareIR reports a thread-local variable and makes it an ordinary
  // one; this is the backstop for a pipeline without it.
  if (GV->isThreadLocal())
    GV->getContext().emitError("thread-local variable " + MSIL::getIRName(*GV) +
                               " is not supported for MSIL yet");

  if (ObjectMode) {
    // The record encodes a segment's or common symbol's alignment as a
    // power of two of at most 30 (plan 5.8.8). Reported before the data
    // segment captures the global, and the global stays out of it, so the
    // diagnostic allocates nothing; emitObjectEnd leaves its symbol out.
    Align Alignment = getGVAlignment(GV, GV->getDataLayout());
    if (Log2(Alignment) > 30) {
      GV->getContext().emitError("global variable " + MSIL::getIRName(*GV) +
                                 ": an alignment of " +
                                 Twine(Alignment.value()) +
                                 " bytes is larger than MSIL supports (2^30)");
      return;
    }
    // A common symbol of an object has no bytes, only a size and an
    // alignment (plan 5.8.8).
    if (GV->hasCommonLinkage())
      return;
  }

  // Every definition, whatever its linkage, lives in the data segment
  // (decisions D12 and D26: comdats are ignored); in an object, in a segment
  // of its own (emitObjectEnd).
  Data->addGlobal(*GV, *this, getSymToGV(*GV->getParent()));
}

void MSILAsmPrinter::emitGlobalAlias(const Module &M, const GlobalAlias &GA) {
  // An alias of an object is a symbol of its record (emitObjectEnd).
  if (ObjectMode)
    return;
  // MSILPrepareIR replaces an alias of a defined function (offset 0) or of a
  // global variable (any constant offset) by its aliasee, every other alias by
  // poison, and erases them all (decision D26 as amended for Phase 2), so only
  // a pipeline that skips it gets here.
  GA.getContext().emitError("alias " + MSIL::getIRName(GA) +
                            " is not supported for MSIL");
}

void MSILAsmPrinter::emitGlobalIFunc(Module &M, const GlobalIFunc &GI) {
  // As for aliases (decision D26).
  GI.getContext().emitError("ifunc " + MSIL::getIRName(GI) +
                            " is not supported for MSIL");
}

void MSILAsmPrinter::emitFunctionEntryLabel() {
  // The method header takes the place of the entry label: CIL code never
  // refers to a method by address, only by its name and signature.
  const Function &F = MF->getFunction();
  const MSIL::Signature &Sig = getSignature(F);
  assert(getFunctionInfoSignature(
             *MFI, F.getDataLayout().getPointerSizeInBits()) == Sig &&
         "the method header and the lowered arguments and results disagree");
  StringRef Name = MSIL::getILName(F, TM);
  if (isReservedMethodName(Name))
    reportReservedMethodName(F, Name);
  // CIL methods may share a name when their signatures differ, but ilasm
  // rejects a second method with the same name and signature ('Duplicate
  // method declaration'), as for @f and @"\01f". Functions are printed in
  // module order, so the first of two is the one named in the error.
  if (auto [It, Inserted] = Methods.try_emplace({Name, Sig}, &F); !Inserted) {
    F.getContext().emitError(
        "function " + MSIL::getIRName(F) + " has the same IL name '" + Name +
        "' and signature as " + MSIL::getIRName(*It->second));
    ClashingMethods.insert(&F);
  }
  // An object's MethodDef rows follow the order of emission (rule R6).
  if (ObjectMode && MethodRows.try_emplace(&F, MethodOrder.size() + 1).second)
    MethodOrder.push_back(&F);
  getTargetStreamer().emitMethodBegin(
      {Name, &Sig, /*Public=*/!F.hasLocalLinkage()});
  InMethod = true;
}

void MSILAsmPrinter::emitFunctionBodyStart() {
  const Function &F = MF->getFunction();
  if (MFI->getParams().size() > MaxArgsOrLocals ||
      MFI->getLocals().size() > MaxArgsOrLocals)
    F.getContext().diagnose(DiagnosticInfoUnsupported(
        F, "a CIL method has at most " + Twine(MaxArgsOrLocals) +
               " arguments and " + Twine(MaxArgsOrLocals) + " locals"));

  // .maxstack comes from MSILStackCheck, the last machine pass.
  MSILTargetStreamer &TS = getTargetStreamer();
  TS.emitMaxStack(MFI->getMaxStack());
  unsigned PointerSizeInBits = F.getDataLayout().getPointerSizeInBits();
  SmallVector<cli::TypeSig, 16> Locals;
  for (MVT VT : MFI->getLocals())
    Locals.push_back(MSIL::toTypeSig(MSIL::toValType(VT, PointerSizeInBits)));
  TS.emitLocals(Locals);

  // msil-ld never relaxes branches (plan 5.8), so their forms are chosen
  // once, before the first instruction is emitted.
  MCInstLowering->computeBranchForms(*MF);
}

void MSILAsmPrinter::emitInstruction(const MachineInstr *MI) {
  assert(matchesCalleeSignature(*MI, *this, ObjectMode) &&
         "a direct call does not match its callee's signature");

  // One MCInst per CIL instruction. Every instruction goes through lower(),
  // which emits code for exactly the instructions that computeBranchForms
  // laid out (MSILMCInstLower::isLowered), so the branch forms it chose fit:
  // nothing for the ARGUMENT_* pseudos, and an internal error for any other
  // instruction (a target-independent one that AsmPrinter does not handle
  // itself, such as FENTRY_CALL).
  SmallVector<MCInst, 4> Insts;
  MCInstLowering->lower(*MI, Insts);
  // The token rows that these instructions name, in the order the streamer
  // receives them (rule R6).
  if (ObjectMode)
    noteObjectTokens(*MI, Insts);
  for (const MCInst &Inst : Insts)
    EmitToStreamer(*OutStreamer, Inst);
}

//===----------------------------------------------------------------------===//
// Object mode (plan 5.8): the events of a CIL object and its link-info record.
//===----------------------------------------------------------------------===//

namespace {
/// The parent and the signature kind of a MemberRef row of a CIL object (plan
/// 5.8.3), part of the row's identity (MSILAsmPrinter::MemberRefKey).
enum ObjectRefClass : uint8_t {
  /// [msil.extern]'<msil>Symbols'::'<name>'(<call-site signature>).
  SymbolsMethodRef,
  /// [msil.extern]'<msil>Symbols'::'<name>', signature FIELD native int.
  SymbolsFieldRef,
  /// [msil.extern]'<msil>FnAddr'::'<name>', signature FIELD native int.
  FnAddrRef,
  /// [msil.bcl]<Namespace>.<Type>::<member> (llvm/CLI/BCLMembers.def).
  BCLRef,
};

/// An import stub of an object (plan 5.8.3): the MethodDef of an undefined
/// function \p F with signature \p Sig, 'pinvokeimpl("<Module>" as
/// "<Entry>" nomangle cdecl)'.
struct ImportStub {
  const Function *F;
  const MSIL::Signature *Sig;
  StringRef Module;
  StringRef Entry;
};
} // end anonymous namespace

/// The global object that \p GA aliases, through chains of aliases, and in
/// \p Offset the constant byte offset into it (MSILPrepareIR's rule), or null
/// if the aliasee is not a global object plus a constant offset.
static const GlobalObject *getAliasedObject(const GlobalAlias &GA,
                                            APInt &Offset) {
  const DataLayout &DL = GA.getDataLayout();
  Offset = APInt(DL.getIndexTypeSizeInBits(GA.getType()), 0);
  SmallPtrSet<const GlobalAlias *, 4> Visited;
  Visited.insert(&GA);
  const Value *V = GA.getAliasee();
  while (true) {
    V = V->stripAndAccumulateConstantOffsets(DL, Offset,
                                             /*AllowNonInbounds=*/true);
    const auto *Next = dyn_cast<GlobalAlias>(V);
    if (!Next)
      return dyn_cast<GlobalObject>(V);
    if (!Visited.insert(Next).second)
      return nullptr;
    V = Next->getAliasee();
  }
}

/// The CIL signature of a call of the IR function type \p FTy in \p Caller:
/// MSIL::computeSignatureVTs, the lowering of LowerCall, so it equals the
/// signature of the call's token (MSILMCInstLower::getCallSiteSignature).
static MSIL::Signature getCallTypeSignature(const FunctionType *FTy,
                                            const Function &Caller,
                                            const TargetMachine &TM) {
  return MSIL::getFunctionTypeSignature(FTy, nullptr, Caller, TM);
}

MSIL::Signature MSIL::getFunctionTypeSignature(const FunctionType *FTy,
                                               const Function *TargetFunc,
                                               const Function &Context,
                                               const TargetMachine &TM) {
  SmallVector<MVT, 4> Params;
  SmallVector<MVT, 1> Results;
  computeSignatureVTs(FTy, TargetFunc, Context, TM, Params, Results);
  unsigned PointerSizeInBits = Context.getDataLayout().getPointerSizeInBits();
  MSIL::Signature Sig;
  for (MVT VT : Results)
    Sig.Results.push_back(MSIL::toValType(VT, PointerSizeInBits));
  for (MVT VT : Params)
    Sig.Params.push_back(MSIL::toValType(VT, PointerSizeInBits));
  return Sig;
}

/// The ABI descriptor (plan 5.8.8, SIGNATURES; its Token is left 0) of a call
/// or definition of the IR function type \p FTy with the attributes \p Attrs:
/// NO_PROTOTYPE if \p NoPrototype; NATIVE_INCOMPATIBLE if the result (unless
/// void) or a parameter is not a native scalar (isNativeScalar; plan 5.8.8 as
/// the section 12 row of 2026-10-25 amends it); SRET with the size and
/// alignment of the returned aggregate for an sret first parameter (IL
/// parameter 0); and the IL parameter index, size and alignment of each byval
/// parameter. The IL parameters are those of MSIL::computeSignatureVTs over
/// \p Context: a hidden leading pointer when the result takes more than one
/// value, then the values of each IR parameter. A byval parameter's alignment
/// is that of the copy LowerCall makes (the stack alignment, else the
/// alignment, else the type's ABI alignment, as TargetLowering decides it).
/// VARARG is never set (decision D19).
MSILLink::SignatureInfo MSIL::getABIDescriptor(const FunctionType *FTy,
                                               const AttributeList &Attrs,
                                               bool NoPrototype,
                                               const Function &Context,
                                               const TargetMachine &TM) {
  const DataLayout &DL = Context.getDataLayout();
  MSILLink::SignatureInfo SI = {};
  if (NoPrototype)
    SI.Flags |= MSILLink::NoPrototype;
  if ((!FTy->getReturnType()->isVoidTy() &&
       !isNativeScalar(FTy->getReturnType())) ||
      any_of(FTy->params(), [](Type *T) { return !isNativeScalar(T); }))
    SI.Flags |= MSILLink::NativeIncompatible;
  SmallVector<MVT, 2> Results;
  MSIL::computeLegalValueVTs(Context, TM, FTy->getReturnType(), Results);
  uint32_t ILIndex = Results.size() > 1 ? 1 : 0;
  for (unsigned I = 0, E = FTy->getNumParams(); I != E; ++I) {
    if (I == 0 && Attrs.hasParamAttr(0, Attribute::StructRet)) {
      Type *T = Attrs.getParamStructRetType(0);
      if (T && T->isSized()) {
        SI.Flags |= MSILLink::Sret;
        SI.SretSize = DL.getTypeAllocSize(T).getFixedValue();
        SI.SretAlignLog2 =
            Log2(Attrs.getParamAlignment(0).value_or(DL.getABITypeAlign(T)));
      }
    }
    if (Attrs.hasParamAttr(I, Attribute::ByVal)) {
      Type *T = Attrs.getParamByValType(I);
      if (T && T->isSized()) {
        MaybeAlign A = Attrs.getParamStackAlignment(I);
        if (!A)
          A = Attrs.getParamAlignment(I);
        SI.Byval.push_back(
            {ILIndex, DL.getTypeAllocSize(T).getFixedValue(),
             static_cast<uint8_t>(Log2(A.value_or(DL.getABITypeAlign(T))))});
      }
    }
    SmallVector<MVT, 2> VTs;
    MSIL::computeLegalValueVTs(Context, TM, FTy->getParamType(I), VTs);
    ILIndex += VTs.size();
  }
  return SI;
}

/// Whether \p SI says anything: a MethodDef or MemberRef without a SIGNATURES
/// entry is prototyped, not variadic, native-compatible (a void or native
/// scalar result and native scalar parameters), and has no sret or byval
/// parameter.
static bool hasABIInfo(const MSILLink::SignatureInfo &SI) {
  return SI.Flags != 0 || !SI.Byval.empty();
}

static bool isSameABI(const MSILLink::SignatureInfo &A,
                      const MSILLink::SignatureInfo &B) {
  return A.Flags == B.Flags && A.FixedParams == B.FixedParams &&
         A.SretSize == B.SretSize && A.SretAlignLog2 == B.SretAlignLog2 &&
         llvm::equal(
             A.Byval, B.Byval,
             [](const MSILLink::ByvalParam &X, const MSILLink::ByvalParam &Y) {
               return X.Param == Y.Param && X.Size == Y.Size &&
                      X.AlignLog2 == Y.AlignLog2;
             });
}

void MSILAsmPrinter::collectCallABIs(const Function &Caller) {
  for (const Instruction &I : instructions(Caller)) {
    const auto *CB = dyn_cast<CallBase>(&I);
    if (!CB || CB->isInlineAsm())
      continue;
    const auto *Callee =
        dyn_cast<GlobalValue>(CB->getCalledOperand()->stripPointerCasts());
    const auto *F = dyn_cast_or_null<Function>(Callee);
    // The calls that name a symbol through a placeholder MemberRef or an
    // import stub (rules R1, R3, R7): of a function that this object does not
    // define, or of an alias.
    if (F ? F->isIntrinsic() || !F->isDeclarationForLinker()
          : !isa_and_nonnull<GlobalAlias>(Callee))
      continue;
    MSILLink::SignatureInfo SI = MSIL::getABIDescriptor(
        CB->getFunctionType(), CB->getAttributes(),
        F && F->hasFnAttribute("no-prototype"), Caller, TM);
    std::pair<std::string, MSIL::Signature> Key(
        MSIL::getILName(*Callee, TM).str(),
        getCallTypeSignature(CB->getFunctionType(), Caller, TM));
    auto [It, Inserted] = CallABIs.try_emplace(std::move(Key), SI, false);
    if (Inserted || It->second.second || isSameABI(It->second.first, SI))
      continue;
    // Undefined behaviour in C (C11 6.5.2.2p9), and ilasm cannot express two
    // MemberRefs of one name and signature (rule R3). Reported at the
    // conflicting call, as MSILPrepareIR's warnings are, so that clang names
    // its source location.
    It->second.second = true;
    Caller.getContext().diagnose(DiagnosticInfoUnsupported(
        Caller,
        Twine("calls to '") + It->first.first +
            "' pass incompatible arguments; the object records the first "
            "call's layout",
        CB->getDebugLoc(), DS_Warning));
  }
}

void MSILAsmPrinter::noteObjectTokens(const MachineInstr &MI,
                                      ArrayRef<MCInst> Insts) {
  const GlobalValue *Callee = nullptr;
  if (MSIL::isCallDirect(MI.getOpcode())) {
    const MachineOperand &MO = MSIL::getCalleeOp(MI);
    if (MO.isGlobal()) {
      Callee = MO.getGlobal();
      CalledValues.insert(Callee);
    } else if (MO.isSymbol()) {
      CalledNames.insert(MO.getSymbolName());
    }
  }

  // The MemberRef rows in the order of the token operands that reach the
  // streamer, duplicates merged (rule R6), as ilasm numbers the object-form
  // text and MSILObjectWriter its rows.
  for (const MCInst &Inst : Insts) {
    for (const MCOperand &Op : Inst) {
      if (!Op.isExpr())
        continue;
      const auto *Ref = dyn_cast<MSILMemberRefExpr>(Op.getExpr());
      if (!Ref)
        continue;
      MemberRefKey Key;
      switch (Ref->getKind()) {
      case MSILMemberRefExpr::Method:
        // A method of this object, or the import stub of an undefined
        // function (rule R7): one stub per call signature, numbered after
        // the methods in first-use order.
        if (const auto *F = dyn_cast_or_null<Function>(Callee);
            F && F->isDeclarationForLinker() &&
            CallStubSet.insert({F, *Ref->getSignature()}).second)
          CallStubs.push_back({F, Ref->getSignature()});
        continue;
      case MSILMemberRefExpr::Field:
      case MSILMemberRefExpr::StandAloneSig:
        // A FieldDef of this object, or a StandAloneSig, which the record
        // never names.
        continue;
      case MSILMemberRefExpr::ExternMethod:
        Key = MemberRefKey(SymbolsMethodRef, Ref->getName().str(),
                           *Ref->getSignature(), 0);
        break;
      case MSILMemberRefExpr::ExternField:
        Key = MemberRefKey(SymbolsFieldRef, Ref->getName().str(),
                           MSIL::Signature(), 0);
        break;
      case MSILMemberRefExpr::FnAddr:
        Key =
            MemberRefKey(FnAddrRef, Ref->getName().str(), MSIL::Signature(), 0);
        break;
      case MSILMemberRefExpr::BCLMember:
        Key = MemberRefKey(BCLRef, std::string(), MSIL::Signature(),
                           static_cast<unsigned>(Ref->getBCLMember()));
        break;
      }
      unsigned Row = MemberRefRows.size() + 1;
      if (MemberRefRows.try_emplace(Key, Row).second &&
          std::get<0>(Key) == SymbolsMethodRef)
        SymbolMethodRefs.emplace_back(Row, std::get<1>(Key), std::get<2>(Key));
    }
  }
}

void MSILAsmPrinter::emitObjectEnd(Module &M) {
  LLVMContext &Ctx = M.getContext();
  const DataLayout &DL = M.getDataLayout();
  const Triple &TT = TM.getTargetTriple();
  MSILTargetStreamer &TS = getTargetStreamer();
  // Every global variable has been added; an object keeps the symbol
  // differences (DIFF relocations).
  Data->finalize();

  MSILLink::Record R;

  // The header (plan 5.8.8, 5.8.9): the normalized triple, ABI version 1, no
  // flags, and the data model. clang writes the wchar_size flag only when it
  // differs from the triple's default.
  R.Hdr.Triple = Triple::normalize(TT.str());
  unsigned WCharSize = TT.getDefaultWCharSize();
  if (const auto *Flag = mdconst::dyn_extract_or_null<ConstantInt>(
          M.getModuleFlag("wchar_size")))
    WCharSize = Flag->getZExtValue();
  MSILLink::setDataModel(R.Hdr, TT, DL.getPointerSize(0), WCharSize);

  // The undefined global values that the emitted code or data refer to: only
  // those are symbols (unreferenced declarations are omitted).
  SmallPtrSet<const GlobalValue *, 16> Referenced;
  for (const auto &Entry : ExternFuncs)
    Referenced.insert(Entry.first);
  Referenced.insert_range(UsedExternGlobals);
  SmallPtrSet<const Function *, 16> AddressTaken;
  AddressTaken.insert_range(UsedFuncAddrs);
  for (const MSILReloc &Rel : Data->Relocs) {
    Referenced.insert(Rel.Target);
    if (const auto *F = dyn_cast<Function>(Rel.Target))
      AddressTaken.insert(F);
  }
  for (const MSILDiff &D : Data->Diffs) {
    Referenced.insert(D.A);
    Referenced.insert(D.B);
  }

  // llvm.used: NO_STRIP, and RETAIN on a segment (llvm.compiler.used means
  // nothing to a linker; plan 5.8.10).
  SmallVector<GlobalValue *, 8> UsedList;
  collectUsedGlobalVariables(M, UsedList, /*CompilerUsed=*/false);
  SmallPtrSet<const GlobalValue *, 8> Used;
  Used.insert_range(UsedList);

  auto VisibilityFlags = [](const GlobalValue &GV) -> uint32_t {
    MSILLink::Visibility V = MSILLink::Visibility::Default;
    if (GV.hasHiddenVisibility())
      V = MSILLink::Visibility::Hidden;
    else if (GV.hasProtectedVisibility())
      V = MSILLink::Visibility::Protected;
    return static_cast<uint32_t>(V) << MSILLink::VisibilityShift;
  };
  auto DefinedFlags = [&](const GlobalValue &GV) {
    uint32_t Flags = VisibilityFlags(GV);
    if (GV.hasLocalLinkage())
      Flags |= MSILLink::BindingLocal;
    else if (GV.hasCommonLinkage())
      Flags |= MSILLink::BindingCommon;
    else if (GV.isWeakForLinker())
      Flags |= MSILLink::BindingWeak;
    if (GV.hasDLLExportStorageClass())
      Flags |= MSILLink::DLLExport;
    if (Used.contains(&GV))
      Flags |= MSILLink::NoStrip;
    return Flags;
  };
  auto UndefinedFlags = [&](const GlobalValue &GV) {
    uint32_t Flags = VisibilityFlags(GV) | MSILLink::Undefined;
    if (GV.hasExternalWeakLinkage())
      Flags |= MSILLink::BindingWeak;
    if (GV.hasDLLImportStorageClass())
      Flags |= MSILLink::DLLImport;
    return Flags;
  };

  // SYMTAB (plan 5.8.8), in its order: global variables, functions and
  // aliases in IR order, then the library calls in first-reference order.
  DenseMap<const GlobalValue *, unsigned> SymOf;
  StringMap<unsigned> SymByName;
  // The global value of each symbol; null for a library call.
  SmallVector<const GlobalValue *, 0> SymGV;
  // The global values that a diagnostic of this function leaves out of
  // SYMTAB. The object is not written then, but its events stay consistent
  // (plan section 2), and a reference to one of them is no second error.
  SmallPtrSet<const GlobalValue *, 4> Diagnosed;
  // Every string of the record is valid UTF-8 without NUL bytes (plan 5.8.8,
  // 'str'); IR names and strings may hold any byte.
  auto CheckString = [&](StringRef S, const Twine &What) {
    const UTF8 *P = reinterpret_cast<const UTF8 *>(S.data());
    if (!S.contains('\0') && isLegalUTF8String(&P, P + S.size()))
      return true;
    Ctx.emitError(What + " is not valid UTF-8 without NUL bytes, which an "
                         "MSIL object requires");
    return false;
  };
  auto AddSymbol = [&](const GlobalValue *GV, MSILLink::Symbol S) {
    auto Reject = [&] {
      if (GV)
        Diagnosed.insert(GV);
      return false;
    };
    if (!CheckString(S.Name,
                     "the name of " + (GV ? MSIL::getIRName(*GV)
                                          : "library call '" + S.Name + "'")) ||
        !CheckString(S.ImportModule,
                     "the import module of symbol '" + S.Name + "'") ||
        !CheckString(S.ImportName,
                     "the import name of symbol '" + S.Name + "'"))
      return Reject();
    auto [It, Inserted] = SymByName.try_emplace(S.Name, R.Symbols.size());
    if (!Inserted) {
      // Symbol names are unique (C8); an IL name drops the '\1' of an IR
      // name, so @f and @"\01f" are one symbol. A function that
      // emitFunctionEntryLabel has reported (its method has the IL name and
      // signature of an earlier one) is left out without a second error.
      if (const auto *F = dyn_cast_or_null<Function>(GV);
          F && ClashingMethods.contains(F))
        return Reject();
      const GlobalValue *Other = SymGV[It->second];
      Ctx.emitError(
          (GV ? MSIL::getIRName(*GV) : "library call '" + S.Name + "'") +
          " has the same IL name '" + S.Name + "' as " +
          (Other ? MSIL::getIRName(*Other) : "a library call of that name"));
      return Reject();
    }
    if (GV)
      SymOf[GV] = R.Symbols.size();
    SymGV.push_back(GV);
    R.Symbols.push_back(std::move(S));
    return true;
  };
  auto CheckNameLength = [&](const GlobalValue &GV, StringRef Name) {
    if (Name.size() <= MSIL::MaxILNameBytes)
      return true;
    Ctx.emitError(MSIL::getIRName(GV) + ": identifier of " +
                  Twine(Name.size()) + " bytes is longer than the " +
                  Twine(MSIL::MaxILNameBytes) + " bytes ilasm accepts");
    Diagnosed.insert(&GV);
    return false;
  };

  DenseMap<const GlobalVariable *, const MSILDataSegment::Entry *> EntryOf;
  for (const MSILDataSegment::Entry &E : Data->Entries)
    EntryOf[E.GV] = &E;
  // The segment of each defined global, and the address fields to emit, in
  // SYMTAB order: FieldDef row i + 1 is the field of Fields[i], and FieldOf
  // maps an IL name to its row.
  DenseMap<const GlobalVariable *, unsigned> SegOf;
  SmallVector<std::pair<StringRef, bool>, 16> Fields;
  StringMap<unsigned> FieldOf;

  for (const GlobalVariable &GV : M.globals()) {
    // llvm.used and the like hold no program data.
    if (MSIL::isSpecialLLVMGlobal(GV))
      continue;
    StringRef ILName = MSIL::getILName(GV, TM);
    MSILLink::Symbol S;
    S.Kind = MSILLink::SymbolKind::Data;
    S.Name = ILName.str();
    if (GV.isDeclarationForLinker()) {
      // A declaration, extern_weak or available_externally variable that the
      // object uses: DATA UNDEFINED, and IMPORT with the C import attributes
      // (plan 5.10.7, 5.10.12): the module (empty: msil-ld finds it natively)
      // and the entry, else the C name.
      if (!Referenced.contains(&GV) || !CheckNameLength(GV, ILName))
        continue;
      S.Flags = UndefinedFlags(GV);
      StringRef Module =
          GV.getAttribute("msil-import-module").getValueAsString();
      StringRef Entry = GV.getAttribute("msil-import-name").getValueAsString();
      if (!Module.empty() || !Entry.empty()) {
        S.Flags |= MSILLink::Import;
        S.ImportModule = Module.str();
        S.ImportName = Entry.empty() ? S.Name : Entry.str();
      }
      AddSymbol(&GV, std::move(S));
      continue;
    }

    // A definition, any linkage: its address field, and its segment or its
    // common size. The code names the field by its IL name, so the field is
    // emitted even when a diagnostic below leaves the symbol out; a second
    // definition of the same IL name (an error) has the first one's field.
    auto [FieldIt, NewField] = FieldOf.try_emplace(ILName, Fields.size() + 1);
    if (NewField)
      Fields.push_back({ILName, !GV.hasLocalLinkage()});
    S.Flags = DefinedFlags(GV);
    S.Token = cli::makeToken(cli::TableId::Field, FieldIt->second);
    // emitGlobalVariable has reported an alignment that the record cannot
    // encode and kept the global out of the data segment.
    Align Alignment = getGVAlignment(&GV, DL);
    if (Log2(Alignment) > 30) {
      Diagnosed.insert(&GV);
      continue;
    }
    std::optional<MSILLink::Segment> Seg;
    if (GV.hasCommonLinkage()) {
      S.CommonSize = GV.getGlobalSize(DL);
      S.CommonAlignLog2 = Log2(Alignment);
    } else {
      const MSILDataSegment::Entry *E = EntryOf.lookup(&GV);
      if (!E)
        reportFatalInternalError("MSIL: global variable '" + ILName +
                                 "' is missing from the data segment");
      if (E->Size >= (uint64_t(1) << 31)) {
        Ctx.emitError("global variable " + MSIL::getIRName(GV) + " of " +
                      Twine(E->Size) +
                      " bytes is larger than MSIL supports (2 GiB)");
        Diagnosed.insert(&GV);
        continue;
      }
      // The section the global's bytes go to: the IR section verbatim, else
      // the one a native object gives its kind; mergeable C strings,
      // llvm.used, zero-initialized and constant data are flagged as the
      // native section flags say (SHF_MERGE|SHF_STRINGS, SHF_GNU_RETAIN,
      // SHT_NOBITS, no SHF_WRITE).
      if (GV.hasSection() &&
          !CheckString(GV.getSection(),
                       "the section name of " + MSIL::getIRName(GV))) {
        Diagnosed.insert(&GV);
        continue;
      }
      SectionKind Kind = TargetLoweringObjectFile::getKindForGlobal(&GV, TM);
      Seg.emplace();
      Seg->Name = MSILDataSegment::getSegmentName(GV, Kind);
      if (Kind.isMergeable1ByteCString())
        Seg->Flags |= MSILLink::SegStrings;
      if (Used.contains(&GV))
        Seg->Flags |= MSILLink::SegRetain;
      if (E->IsZero)
        Seg->Flags |= MSILLink::SegZero;
      if (Kind.isReadOnly() || Kind.isReadOnlyWithRel())
        Seg->Flags |= MSILLink::SegReadOnly;
      Seg->AlignLog2 = Log2(Alignment);
      Seg->Size = E->Size;
      // Relocation sites hold zeros.
      if (!E->IsZero)
        Seg->Bytes.assign(Data->Image.begin() + E->Offset,
                          Data->Image.begin() + E->Offset + E->Size);
      S.Segment = R.Segments.size();
      S.Offset = 0;
      S.Size = E->Size;
    }
    if (!AddSymbol(&GV, std::move(S)))
      continue;
    if (Seg) {
      SegOf[&GV] = R.Segments.size();
      R.Segments.push_back(std::move(*Seg));
    }
  }

  for (const Function &F : M) {
    if (F.isIntrinsic())
      continue;
    MSILLink::Symbol S;
    S.Kind = MSILLink::SymbolKind::Function;
    S.Name = MSIL::getILName(F, TM).str();
    if (auto It = MethodRows.find(&F); It != MethodRows.end()) {
      S.Flags = DefinedFlags(F);
      S.Token = cli::makeToken(cli::TableId::MethodDef, It->second);
    } else if (F.isDeclarationForLinker() && Referenced.contains(&F)) {
      // A declaration, extern_weak or available_externally function that
      // the object calls or whose address it takes. EXPLICIT_IMPORT follows
      // with its stubs.
      S.Flags = UndefinedFlags(F);
    } else {
      continue;
    }
    AddSymbol(&F, std::move(S));
  }

  for (const GlobalAlias &GA : M.aliases()) {
    // MSILPrepareIR erases every alias that is not a definition plus a
    // constant offset that fits its kind; this is the backstop.
    APInt Offset;
    const GlobalObject *GO = getAliasedObject(GA, Offset);
    bool IsFunction = GA.getValueType()->isFunctionTy();
    auto It = GO ? SymOf.find(GO) : SymOf.end();
    StringRef ILName = MSIL::getILName(GA, TM);
    if (It == SymOf.end() ||
        R.Symbols[It->second].Kind != (IsFunction
                                           ? MSILLink::SymbolKind::Function
                                           : MSILLink::SymbolKind::Data) ||
        (R.Symbols[It->second].Flags & MSILLink::Undefined) ||
        (R.Symbols[It->second].Flags & MSILLink::BindingMask) ==
            MSILLink::BindingCommon ||
        (IsFunction && !Offset.isZero())) {
      // An alias of a global value that a diagnostic left out goes with it.
      if (!GO || !Diagnosed.contains(GO))
        Ctx.emitError("alias '" + ILName +
                      "' must point to a definition plus a constant offset "
                      "for MSIL");
      Diagnosed.insert(&GA);
      continue;
    }
    if (!CheckNameLength(GA, ILName))
      continue;
    MSILLink::Symbol S;
    S.Kind = R.Symbols[It->second].Kind;
    S.Flags = DefinedFlags(GA) | MSILLink::Alias;
    S.Name = ILName.str();
    S.Aliasee = It->second;
    if (!IsFunction) {
      S.AliasOffset = Offset.getSExtValue();
      S.Size = GA.getValueType()->isSized()
                   ? DL.getTypeAllocSize(GA.getValueType()).getFixedValue()
                   : 0;
    }
    AddSymbol(&GA, std::move(S));
  }

  for (const auto &[Name, Sig] : ExtSymOrder) {
    // A library call of a function that the module declares or defines is
    // that function's symbol.
    if (auto It = SymByName.find(Name); It != SymByName.end()) {
      if (R.Symbols[It->second].Kind != MSILLink::SymbolKind::Function)
        Ctx.emitError("library call '" + Name +
                      "' has the IL name of global variable " +
                      MSIL::getIRName(*SymGV[It->second]));
      continue;
    }
    MSILLink::Symbol S;
    S.Kind = MSILLink::SymbolKind::Function;
    S.Flags = MSILLink::Undefined;
    S.Name = Name.str();
    AddSymbol(nullptr, std::move(S));
  }

  // The import stubs (plan 5.8.3): the ones that calls name, in first-use
  // order, then one address-only stub for each undefined, prototyped function
  // whose address the object takes but that it never calls, with the
  // declaration's signature, in SYMTAB order (a variadic one is an error). A
  // stub is on the "msil-import-module" module, else on 'msil.unresolved'
  // (msil-ld binds it natively under its import name), and its entry is
  // "msil-import-name", else the C name.
  SmallVector<ImportStub, 4> Stubs;
  auto AddStub = [&](const Function &F, const MSIL::Signature &Sig) {
    StringRef Module =
        F.getFnAttribute("msil-import-module").getValueAsString();
    StringRef Entry = F.getFnAttribute("msil-import-name").getValueAsString();
    Stubs.push_back(
        {&F, &Sig,
         Module.empty() ? StringRef(MSILLink::UnresolvedModule) : Module,
         Entry.empty() ? MSIL::getILName(F, TM) : Entry});
    // An undefined function whose stubs name a module other than
    // 'msil.unresolved' (rule C4).
    if (auto It = SymOf.find(&F);
        It != SymOf.end() && Stubs.back().Module != MSILLink::UnresolvedModule)
      R.Symbols[It->second].Flags |= MSILLink::ExplicitImport;
  };
  // The calls name their stubs, so a function that a diagnostic left out of
  // SYMTAB keeps them too.
  for (const auto &[F, Sig] : CallStubs)
    AddStub(*F, *Sig);
  size_t NumCallStubs = Stubs.size();
  for (unsigned I = 0, E = R.Symbols.size(); I != E; ++I) {
    const auto *F = dyn_cast_or_null<Function>(SymGV[I]);
    if (!F || !(R.Symbols[I].Flags & MSILLink::Undefined) ||
        !AddressTaken.contains(F) || F->hasFnAttribute("no-prototype") ||
        CalledValues.contains(F) || CalledNames.contains(R.Symbols[I].Name))
      continue;
    // The variable arguments of a native variadic function need the calling
    // convention support of phase 3, as in -S (decision D19, plan 5.10.13):
    // record v1 never sets VARARG (5.8.8), so such a function gets no stub.
    // Its address is a FnAddr reference, which names no stub.
    if (F->isVarArg()) {
      Ctx.emitError("undefined variadic function '" + R.Symbols[I].Name +
                    "' is not supported for MSIL yet (phase 3)");
      continue;
    }
    AddStub(*F, getSignature(*F));
  }

  // COMDATS (plan 5.8.8): the groups of the definitions in SYMTAB order, their
  // members in SYMTAB order. MSILPrepareIR has reported every selection kind
  // but any and nodeduplicate (the values equal the record's).
  DenseMap<const Comdat *, unsigned> GroupOf;
  auto GetGroup = [&](const Comdat &C) {
    auto [It, Inserted] = GroupOf.try_emplace(&C, R.Comdats.size());
    if (Inserted) {
      // The error leaves the group in place: the object is not written.
      CheckString(C.getName(), "the name of comdat '" + C.getName() + "'");
      R.Comdats.push_back(
          {C.getName().str(),
           static_cast<MSILLink::ComdatSelection>(C.getSelectionKind()),
           {}});
    }
    return It->second;
  };
  for (unsigned I = 0, E = R.Symbols.size(); I != E; ++I) {
    const auto *GO = dyn_cast_or_null<GlobalObject>(SymGV[I]);
    if (!GO || !GO->hasComdat() || (R.Symbols[I].Flags & MSILLink::Undefined))
      continue;
    unsigned Group = GetGroup(*GO->getComdat());
    if (isa<Function>(GO))
      R.Comdats[Group].Members.push_back(
          {MSILLink::ComdatMemberKind::FunctionSymbol, I});
    else if (auto It = SegOf.find(cast<GlobalVariable>(GO)); It != SegOf.end())
      R.Comdats[Group].Members.push_back(
          {MSILLink::ComdatMemberKind::Segment, It->second});
  }

  // INIT_FINI (plan 5.8.8): llvm.global_ctors in array order, each with the
  // comdat of its associated global. Each constructor is a function that the
  // object defines, called without arguments, or off Windows with argc, argv
  // and envp as far as its signature takes them (getConstructorArgs); a
  // result is popped.
  if (const GlobalVariable *Ctors = M.getNamedGlobal("llvm.global_ctors");
      Ctors && Ctors->hasInitializer()) {
    if (const auto *List = dyn_cast<ConstantArray>(Ctors->getInitializer())) {
      for (const Use &Op : List->operands()) {
        // A null function ends the list, as in whole-program code generation
        // (AsmPrinter::preprocessXXStructorList after getStructorList), so
        // that both call the same constructors; an element that is null as a
        // whole (zeroinitializer) has one too. Both skip an element that is
        // undef or poison as a whole.
        if (cast<Constant>(Op.get())->isNullValue())
          break;
        const auto *CS = dyn_cast<ConstantStruct>(Op.get());
        if (!CS || CS->getNumOperands() < 2)
          continue;
        if (CS->getOperand(1)->isNullValue())
          break;
        const Constant *Fn = CS->getOperand(1)->stripPointerCasts();
        // The poison of a definition that MSILPrepareIR diagnosed.
        if (isa<ConstantPointerNull, UndefValue>(Fn))
          continue;
        const auto *FnGV = dyn_cast<GlobalValue>(Fn);
        const auto *Priority = dyn_cast<ConstantInt>(CS->getOperand(0));
        auto It = FnGV ? SymOf.find(FnGV) : SymOf.end();
        if (!FnGV || !Priority) {
          Ctx.emitError("a global constructor that is not a function is not "
                        "supported for MSIL");
          continue;
        }
        if (Diagnosed.contains(FnGV))
          continue;
        if (It == SymOf.end() ||
            R.Symbols[It->second].Kind != MSILLink::SymbolKind::Function ||
            (R.Symbols[It->second].Flags & MSILLink::Undefined)) {
          Ctx.emitError("global constructor '" + MSIL::getILName(*FnGV, TM) +
                        "' is not a function that this object defines, which "
                        "an MSIL object requires");
          continue;
        }
        APInt Offset;
        const auto *F = dyn_cast<Function>(FnGV);
        if (const auto *GA = dyn_cast<GlobalAlias>(FnGV))
          F = dyn_cast_or_null<Function>(getAliasedObject(*GA, Offset));
        if (F && !getConstructorArgs(getSignature(*F), TM.getTargetTriple())) {
          Ctx.emitError("global constructor '" + MSIL::getILName(*FnGV, TM) +
                        "' takes arguments, which MSIL does not pass");
          continue;
        }
        MSILLink::InitEntry Entry;
        Entry.Kind = MSILLink::InitKind::Init;
        Entry.Priority = Priority->getLimitedValue(65535);
        Entry.Symbol = It->second;
        if (CS->getNumOperands() > 2 && !CS->getOperand(2)->isNullValue())
          if (const auto *Assoc =
                  dyn_cast<GlobalValue>(CS->getOperand(2)->stripPointerCasts()))
            if (const Comdat *C = Assoc->getComdat())
              Entry.ComdatPlusOne = GetGroup(*C) + 1;
        R.Inits.push_back(Entry);
      }
    }
  }

  // DATA_RELOCS (plan 5.8.8): the sites of each segment, relative to it,
  // sorted. An address of a function is its pointer value (FNADDR).
  auto LookupSite =
      [&](const GlobalVariable *Owner,
          uint64_t Site) -> std::optional<std::pair<uint32_t, uint64_t>> {
    auto It = SegOf.find(Owner);
    if (It == SegOf.end())
      return std::nullopt;
    return std::make_pair(It->second, Site - EntryOf.lookup(Owner)->Offset);
  };
  auto LookupTarget =
      [&](const GlobalValue &Target,
          const GlobalVariable &Owner) -> std::optional<uint32_t> {
    if (auto It = SymOf.find(&Target); It != SymOf.end())
      return It->second;
    if (!Diagnosed.contains(&Target))
      Ctx.emitError("the initializer of '" + MSIL::getILName(Owner, TM) +
                    "' refers to '" + MSIL::getILName(Target, TM) +
                    "', which is not program data");
    return std::nullopt;
  };
  for (const MSILReloc &Rel : Data->Relocs) {
    std::optional<std::pair<uint32_t, uint64_t>> Site =
        LookupSite(Rel.Owner, Rel.Site);
    std::optional<uint32_t> Sym =
        Site ? LookupTarget(*Rel.Target, *Rel.Owner) : std::nullopt;
    if (!Sym)
      continue;
    MSILLink::Reloc X;
    bool IsFunction = R.Symbols[*Sym].Kind == MSILLink::SymbolKind::Function;
    if (IsFunction)
      X.Type = Rel.Size == 8 ? MSILLink::RelocType::FnAddr64
                             : MSILLink::RelocType::FnAddr32;
    else
      X.Type = Rel.Size == 8 ? MSILLink::RelocType::Addr64
                             : MSILLink::RelocType::Addr32;
    X.Segment = Site->first;
    X.Offset = Site->second;
    X.Symbol = *Sym;
    X.Addend = IsFunction ? 0 : Rel.Addend;
    R.Relocs.push_back(X);
  }
  for (const MSILDiff &D : Data->Diffs) {
    std::optional<std::pair<uint32_t, uint64_t>> Site =
        LookupSite(D.Owner, D.Site);
    if (!Site)
      continue;
    std::optional<uint32_t> A = LookupTarget(*D.A, *D.Owner);
    std::optional<uint32_t> B = LookupTarget(*D.B, *D.Owner);
    if (!A || !B)
      continue;
    MSILLink::Reloc X;
    X.Type = MSILLink::RelocType::Diff;
    X.Segment = Site->first;
    X.Offset = Site->second;
    X.Symbol = *A;
    X.Symbol2 = *B;
    X.Addend = D.Addend;
    X.Width = D.Size;
    R.Relocs.push_back(X);
  }
  llvm::sort(R.Relocs, [](const MSILLink::Reloc &L, const MSILLink::Reloc &RR) {
    return std::tie(L.Segment, L.Offset) < std::tie(RR.Segment, RR.Offset);
  });

  // SIGNATURES (plan 5.8.8), in token order: the definitions from their own
  // attributes; the call stubs and the '<msil>Symbols' method MemberRefs
  // from the IR calls (CallABIs, rule R3); an address-only stub from its
  // declaration.
  auto AddSignature = [&](uint32_t Token, MSILLink::SignatureInfo SI) {
    if (!hasABIInfo(SI))
      return;
    SI.Token = Token;
    R.Signatures.push_back(std::move(SI));
  };
  auto CallABIOf = [&](StringRef Name, const MSIL::Signature &Sig) {
    auto It = CallABIs.find({Name.str(), Sig});
    return It == CallABIs.end() ? MSILLink::SignatureInfo() : It->second.first;
  };
  for (unsigned I = 0, E = MethodOrder.size(); I != E; ++I) {
    const Function &F = *MethodOrder[I];
    AddSignature(cli::makeToken(cli::TableId::MethodDef, I + 1),
                 MSIL::getABIDescriptor(F.getFunctionType(), F.getAttributes(),
                                        /*NoPrototype=*/false, F, TM));
  }
  for (unsigned I = 0, E = Stubs.size(); I != E; ++I) {
    const ImportStub &St = Stubs[I];
    uint32_t Token =
        cli::makeToken(cli::TableId::MethodDef, MethodOrder.size() + I + 1);
    if (I < NumCallStubs)
      AddSignature(Token, CallABIOf(MSIL::getILName(*St.F, TM), *St.Sig));
    else
      AddSignature(Token, MSIL::getABIDescriptor(
                              St.F->getFunctionType(), St.F->getAttributes(),
                              /*NoPrototype=*/false, *St.F, TM));
  }
  for (const auto &[Row, Name, Sig] : SymbolMethodRefs)
    AddSignature(cli::makeToken(cli::TableId::MemberRef, Row),
                 CallABIOf(Name, Sig));

  // LINKER_OPTIONS (plan 5.8.8): each !llvm.dependent-libraries entry, then
  // each !llvm.linker.options node with its strings kept apart.
  if (const NamedMDNode *Libs = M.getNamedMetadata("llvm.dependent-libraries"))
    for (const MDNode *N : Libs->operands())
      for (const MDOperand &Op : N->operands())
        if (const auto *Str = dyn_cast_or_null<MDString>(Op.get());
            Str && CheckString(Str->getString(), "a dependent library name"))
          R.LinkerOptions.push_back({MSILLink::LinkerOptionKind::DependentLib,
                                     {Str->getString().str()}});
  if (const NamedMDNode *Opts = M.getNamedMetadata("llvm.linker.options")) {
    for (const MDNode *N : Opts->operands()) {
      MSILLink::LinkerOption Option{MSILLink::LinkerOptionKind::LinkerOption,
                                    {}};
      for (const MDOperand &Op : N->operands())
        if (const auto *Str = dyn_cast_or_null<MDString>(Op.get());
            Str && CheckString(Str->getString(), "a linker option"))
          Option.Args.push_back(Str->getString().str());
      R.LinkerOptions.push_back(std::move(Option));
    }
  }

  // The events after the methods: the address fields in SYMTAB order, the
  // import stubs, and the record.
  for (const auto &[Name, Public] : Fields)
    TS.emitAddressField(Name, Public);
  for (const ImportStub &St : Stubs) {
    StringRef Name = MSIL::getILName(*St.F, TM);
    if (isReservedMethodName(Name))
      reportReservedMethodName(*St.F, Name);
    TS.emitPInvoke({Name, St.Sig, /*Public=*/true}, St.Module, St.Entry);
  }
  if (!Fields.empty() || !Stubs.empty())
    TS.emitBlankLine();
  TS.emitLinkInfo(getObjectModuleName(M), R);
}

char MSILAsmPrinter::ID = 0;

INITIALIZE_PASS(MSILAsmPrinter, "msil-asm-printer", "MSIL Assembly Printer",
                false, false)

// Force static initialization.
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMSILAsmPrinter() {
  RegisterAsmPrinter<MSILAsmPrinter> X(getTheMSILTarget());
}
