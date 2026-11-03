//===- LTO.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/LTO.cpp at 64f50ffd4f1c.
//
// LTO compiles the bitcode inputs for the codegen triple (plan 5.9.13):
// stock-clang bitcode for the native triple is compiled for its '-msil' twin
// (lto::Config::OverrideTriple), and MSILLinkHooksPass hands msil-ld's
// decisions to the MSIL backend as module flags of the merged module.
// Without --lto-emit-asm, LTO writes in-memory CIL objects (ThinLTO and
// --lto-partitions included), which msil-ld links like any object; the pass
// then sets only the module flags. Under --lto-emit-asm it is also the
// import-annotation pass of plan 5.10.13: the merged module's declarations
// are bound to the native libraries (or reported, after LTO, as lld/ELF and
// lld/wasm report undefined symbols), native data is imported through the
// data-import helper (plan 5.10.12), and the backend prints P/Invokes from
// the annotations alone; an entry point other than main reaches the backend
// as the module flag msil-entry (plan 5.9.3), and --msil-reloc-loop-threshold
// as the module flag msil-reloc-loop-threshold (plan 5.9.10). Under -shared
// the definitions a library may export stay visible (plan 5.9.13). LTO's
// diagnostic handler makes a signature mismatch of a helper's call inside
// LTO plan 5.14.2 N-1's error, as the objects' call-site check does. The
// prevailing linkonce_odr copies that LTO keeps external although no regular
// object needs them are linkonce_odr again before code generation, which
// drops the unused ones (decision D26). A prevailing definition of a name
// that the trailer defines in the output (plan 5.14.4) stays external, so
// that the calls that code generation makes after LTO bind to it.
//
//===----------------------------------------------------------------------===//

#include "LTO.h"
#include "Config.h"
#include "Exports.h"
#include "InputFiles.h"
#include "NativeLibraries.h"
#include "ReferenceSet.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "TrailerDefFile.h"
#include "Variadics.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Strings.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CLI/CallMismatchDiagnostic.h"
#include "llvm/CLI/ImageLayout.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/LTO/Config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Pass.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

using namespace llvm;
using namespace lld::msil;
using namespace lld;

// The dotted numbers after the last '_' of an ELF version (GLIBC_2.2.5 ->
// 2, 2, 5), which --print-imports compares for the version floor.
static SmallVector<unsigned, 4> getVersionNumbers(StringRef version) {
  size_t underscore = version.rfind('_');
  if (underscore != StringRef::npos)
    version = version.drop_front(underscore + 1);
  SmallVector<StringRef, 4> parts;
  version.split(parts, '.');
  SmallVector<unsigned, 4> numbers;
  for (StringRef part : parts) {
    unsigned n = 0;
    if (part.getAsInteger(10, n))
      n = 0;
    numbers.push_back(n);
  }
  return numbers;
}

// --print-imports (plan 5.10.14): one line per bound symbol, sorted by
// symbol, then one line per ELF module with the highest default version of
// its bound symbols, the oldest library version the output can run with.
void lld::msil::printImports(std::vector<PrintedImport> &rows) {
  llvm::sort(rows, [](const PrintedImport &a, const PrintedImport &b) {
    return std::tie(a.symbol, a.module, a.entry) <
           std::tie(b.symbol, b.module, b.entry);
  });
  std::map<std::string, std::string> floors;
  for (const PrintedImport &row : rows) {
    message(row.symbol + " -> " + row.module + "!" + row.entry + " [" +
            (row.isData ? "data" : "function") + ", " +
            (row.version.empty() ? "-" : row.version) + "]");
    if (!row.isELF)
      continue;
    std::string &floor = floors[row.module];
    if (row.version.empty())
      continue;
    if (floor.empty() ||
        std::make_pair(getVersionNumbers(floor), StringRef(floor)) <
            std::make_pair(getVersionNumbers(row.version),
                           StringRef(row.version)))
      floor = row.version;
  }
  for (const auto &[module, floor] : floors)
    message(module + ": version floor " + (floor.empty() ? "-" : floor));
}

namespace lld::msil {
/// What BitcodeCompiler::add records for one link, which
/// MSILLinkHooksPass::restoreDiscardableCopies reads: the IR names of the
/// prevailing bitcode definitions that no regular object needs
/// (lto::SymbolResolution::VisibleToRegularObj is false), and whether a
/// bitcode input has a ThinLTO module.
struct DiscardableCopies {
  StringSet<> irNames;
  bool anyThinLTOModule = false;
};
} // namespace lld::msil

// The names that the code generator may call (RuntimeLibcalls of the codegen
// triple), whose definitions MSILLinkHooksPass::restoreDiscardableCopies
// keeps external.
static StringSet<> getCodegenNames() {
  StringSet<> names;
  for (const char *name : lto::LTO::getRuntimeLibcallSymbols(ctx.arg.target))
    names.insert(name);
  return names;
}

// True if MSILLinkHooksPass::restoreDiscardableCopies makes the definition
// \p f linkonce_odr again: a prevailing weak_odr definition that no regular
// object needs (\p discardable, BitcodeCompiler::add's record) and that is
// not one of \p codegenNames (getCodegenNames), in a link that gives LTO one
// module (no ThinLTO module, --lto-partitions=1).
static bool isRestoredDiscardableCopy(const Function &f,
                                      const DiscardableCopies *discardable,
                                      const StringSet<> &codegenNames) {
  return discardable && !discardable->anyThinLTOModule &&
         ctx.arg.ltoPartitions == 1 && !f.isDeclaration() &&
         f.hasWeakODRLinkage() && discardable->irNames.contains(f.getName()) &&
         !codegenNames.contains(f.getName());
}

namespace {
/// What the import annotation decided for the merged module (plan 5.10.13).
/// The analysis runs in lto::Config::PreCodeGenModuleHook, which reports the
/// unresolved and unbindable references and skips code generation after an
/// error, so that the backend's backstop does not report them again;
/// MSILLinkHooksPass applies the result to the module.
struct ImportAnnotation {
  /// A declaration bound natively: its IR name, module and entry. A variadic
  /// declaration that code generation expands, bound to its va_list variant
  /// (plan 5.15.2 rule 2), also has its symbol: it gets "msil-import-name"
  /// only, and its module goes to a kind-0 operand of !msil.imports, because
  /// MSILPrepareVariadics takes a declaration with "msil-import-module" for
  /// an explicit import, which it calls with fixed arity.
  struct Binding {
    std::string irName;
    std::string module;
    std::string entry;
    std::string variantSymbol = {};
  };
  std::vector<Binding> bindings;
  /// /alternatename: a declaration (IR name) whose uses go to a definition
  /// of the merged module (IR name).
  std::vector<std::pair<std::string, std::string>> replacements;
  /// The kind-0 operands of !msil.imports: symbol, module and entry (the
  /// library functions that code generation may call, and the data-import
  /// helper's two functions).
  std::vector<std::array<std::string, 3>> libcalls;
  /// The kind-1 operands of !msil.imports (plan 5.10.12): native data that
  /// the output imports at startup, by symbol, module and entry.
  std::vector<std::array<std::string, 3>> dataImports;
  /// The natively bound variable declarations (IR names).
  std::vector<std::string> boundVariables;
  /// --print-imports: the rows of the declarations that the merged module
  /// refers to, explicit imports included.
  std::vector<PrintedImport> printed;
  /// --print-imports: the rows of the functions that only code generation
  /// may call, by symbol name: the kind-0 operands, the declarations bound
  /// quietly and the unused explicit imports. Each becomes a line when the
  /// AsmPrinter declares a P/Invoke for it (!msil.imports.used).
  StringMap<PrintedImport> codegenRows;
};

/// The UCRT stdio option bits of a link (Config::ucrtPrintfBits,
/// ucrtScanfBits).
struct UCRTStdioBits {
  uint64_t printfBits;
  uint64_t scanfBits;
};

/// msil-ld's hooks in LTO's code generator (plan 5.9.13), a legacy pass that
/// lto::Config::PreCodeGenPassesHook adds ahead of the MSIL backend's passes.
/// It sets module flags of the merged module: "msil-assembly-name" (MDString,
/// behavior Override), the assembly name that the AsmPrinter prints (decision
/// D28 as amended for M14; LTO names the merged module ld-temp.o), and
/// "msil-lto" (i32 1, behavior Override). Under --lto-emit-asm it is also
/// the import-annotation pass (plan 5.10.13): the flag "msil-linked" (i32 1,
/// Override), "msil-import-module"/"msil-import-name" on every natively
/// bound declaration (an extern_weak one becomes external), /alternatename
/// replacements, and the named metadata !msil.imports for the library
/// functions that the code generator may call (kind 0) and the native data
/// that the output imports (kind 1, with kind-0 operands for the data-import
/// helper's functions, plan 5.10.12); available_externally functions and
/// variables become declarations, which the output does not define (plan
/// 5.8.10); and an entry point other than main is the module flag
/// "msil-entry" (MDString, Override), around whose function the AsmPrinter
/// builds D27's '<msil>Main' (plan 5.9.3), and a given
/// --msil-reloc-loop-threshold=<n> is the module flag
/// "msil-reloc-loop-threshold" (i32 <n>, Override), so that the text applies
/// its data relocations as the PE of the same link does (plan 5.9.10). On
/// *-windows-msvc-msil it sets the UCRT stdio option bits of the link as the
/// module flags "msil-ucrt-printf-bits" and "msil-ucrt-scanf-bits" (i64,
/// Override; plan 5.15.2). After code generation it prints --print-imports.
/// Before it sets the flags, it makes the prevailing linkonce_odr copies that
/// LTO keeps external although no regular object needs them linkonce_odr
/// again (restoreDiscardableCopies), so that the backend drops the unused ones
/// (decision D26).
class MSILLinkHooksPass final : public ModulePass {
public:
  static char ID;

  MSILLinkHooksPass(std::string assemblyName, std::string entry,
                    std::optional<uint32_t> relocLoopThreshold,
                    std::optional<UCRTStdioBits> ucrtStdioBits,
                    std::shared_ptr<const ImportAnnotation> imports,
                    std::shared_ptr<const DiscardableCopies> discardable)
      : ModulePass(ID), assemblyName(std::move(assemblyName)),
        entry(std::move(entry)), relocLoopThreshold(relocLoopThreshold),
        ucrtStdioBits(ucrtStdioBits), imports(std::move(imports)),
        discardable(std::move(discardable)) {}

  StringRef getPassName() const override { return "msil-ld link hooks"; }

  // The flags and annotations are set while the pass manager initializes its
  // passes, not in runOnModule: the legacy pass manager initializes every
  // pass of the code generator before it runs the first one, and the
  // AsmPrinter prints the assembly name, and decides on a byte order mark
  // from the names it will print, when it is initialized
  // (emitStartOfAsmFile). This pass comes first, so its initialization runs
  // first.
  bool doInitialization(Module &m) override {
    LLVMContext &context = m.getContext();
    restoreDiscardableCopies(m);
    if (!assemblyName.empty())
      m.setModuleFlag(Module::Override, "msil-assembly-name",
                      MDString::get(context, assemblyName));
    m.setModuleFlag(Module::Override, "msil-lto", 1);
    // Without the flag the AsmPrinter's '<msil>Main' calls main.
    if (!entry.empty())
      m.setModuleFlag(Module::Override, "msil-entry",
                      MDString::get(context, entry));
    // Without the flag the AsmPrinter uses llc's threshold (default 32).
    if (relocLoopThreshold)
      m.setModuleFlag(Module::Override, "msil-reloc-loop-threshold",
                      *relocLoopThreshold);
    // Without the flags the AsmPrinter's module initializer ORs vcruntime's
    // default UCRT stdio option bits (plan 5.15.2).
    if (ucrtStdioBits) {
      m.setModuleFlag(Module::Override, "msil-ucrt-printf-bits",
                      ConstantInt::get(Type::getInt64Ty(context),
                                       ucrtStdioBits->printfBits));
      m.setModuleFlag(Module::Override, "msil-ucrt-scanf-bits",
                      ConstantInt::get(Type::getInt64Ty(context),
                                       ucrtStdioBits->scanfBits));
    }
    if (imports)
      annotate(m);
    return true;
  }

  bool runOnModule(Module &) override { return false; }

  // --print-imports (plan 5.10.13, 5.10.14) comes after code generation,
  // which alone knows the library functions it calls: the legacy pass
  // manager finalizes its passes in reverse order, so this pass, the first,
  // is finalized after the AsmPrinter, which records in !msil.imports.used
  // every P/Invoke it declared. The lines are the import analysis's and one
  // for each such function that a kind-0 !msil.imports operand or a quiet
  // binding bound.
  bool doFinalization(Module &m) override {
    if (!imports || !ctx.arg.printImports)
      return false;
    std::vector<PrintedImport> rows = imports->printed;
    StringSet<> symbols;
    for (const PrintedImport &row : rows)
      symbols.insert(row.symbol);
    if (const NamedMDNode *used = m.getNamedMetadata("msil.imports.used")) {
      for (const MDNode *op : used->operands()) {
        auto *name = op->getNumOperands()
                         ? dyn_cast<MDString>(op->getOperand(0))
                         : nullptr;
        if (!name)
          continue;
        auto it = imports->codegenRows.find(name->getString());
        if (it != imports->codegenRows.end() &&
            symbols.insert(name->getString()).second)
          rows.push_back(it->second);
      }
    }
    printImports(rows);
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &au) const override {
    au.setPreservesAll();
  }

private:
  // A prevailing weak_odr definition (a linkonce_odr copy, which LTO made
  // weak_odr) that no regular object needs (BitcodeCompiler::add) and that
  // LTO still keeps external, as its libcall rule does
  // (lto::InputFile::Symbol::isLibcall: the C library functions of
  // TargetLibraryInfo, such as the UCRT's inline sprintf, and the code
  // generator's library calls), is linkonce_odr again, so that the backend
  // drops it when the merged module does not use it (decision D26), as it
  // drops the copies in a 'clang -c' object. The names that the code
  // generator may call (RuntimeLibcalls) keep their definitions, and so does
  // every link that gives LTO more than one module (a ThinLTO module,
  // --lto-partitions), where another module may call the copy.
  void restoreDiscardableCopies(Module &m) {
    if (!discardable || discardable->anyThinLTOModule ||
        ctx.arg.ltoPartitions != 1)
      return;
    StringSet<> codegenNames = getCodegenNames();
    for (Function &f : m.functions())
      if (isRestoredDiscardableCopy(f, discardable.get(), codegenNames))
        f.setLinkage(GlobalValue::LinkOnceODRLinkage);
  }

  void annotate(Module &m) {
    LLVMContext &context = m.getContext();
    m.setModuleFlag(Module::Override, "msil-linked", 1);

    // The import analysis took available_externally functions and variables
    // for declarations, and the references in their bodies and initializers
    // for none (annotateImports), as the LTO pipeline's
    // EliminateAvailableExternally does from -O2: do what it does.
    for (GlobalVariable &gv : m.globals()) {
      if (!gv.hasAvailableExternallyLinkage())
        continue;
      gv.setInitializer(nullptr);
      gv.removeDeadConstantUsers();
      gv.setLinkage(GlobalValue::ExternalLinkage);
    }
    for (Function &f : m.functions()) {
      if (f.isDeclaration() || !f.hasAvailableExternallyLinkage())
        continue;
      f.deleteBody();
      f.removeDeadConstantUsers();
    }

    // A va_list variant binding (rule 6): the entry as "msil-import-name",
    // which ExpandVariadics' replacement declaration keeps, and the symbol,
    // module and entry as a kind-0 operand of !msil.imports, from which
    // MSIL::getLinkedImport takes the import of a declaration without
    // "msil-import-module".
    std::vector<std::array<std::string, 3>> variants;
    for (const ImportAnnotation::Binding &b : imports->bindings) {
      Function *f = m.getFunction(b.irName);
      if (!f)
        continue;
      if (b.variantSymbol.empty())
        f->addFnAttr("msil-import-module", b.module);
      else
        variants.push_back({b.variantSymbol, b.module, b.entry});
      f->addFnAttr("msil-import-name", b.entry);
      if (f->hasExternalWeakLinkage())
        f->setLinkage(GlobalValue::ExternalLinkage);
    }
    // A natively bound extern_weak variable is imported, not null.
    for (const std::string &name : imports->boundVariables)
      if (GlobalVariable *gv = m.getGlobalVariable(name, /*AllowLocal=*/true);
          gv && gv->hasExternalWeakLinkage())
        gv->setLinkage(GlobalValue::ExternalLinkage);

    for (const auto &[declName, defName] : imports->replacements) {
      GlobalValue *decl = m.getNamedValue(declName);
      GlobalValue *def = m.getNamedValue(defName);
      if (!decl || !def || decl == def || !decl->isDeclaration())
        continue;
      decl->replaceAllUsesWith(def);
      decl->eraseFromParent();
    }

    if (imports->libcalls.empty() && imports->dataImports.empty() &&
        variants.empty())
      return;
    NamedMDNode *md = m.getOrInsertNamedMetadata("msil.imports");
    Type *i32 = Type::getInt32Ty(context);
    auto addOperand = [&](const std::array<std::string, 3> &op, unsigned kind) {
      md->addOperand(MDNode::get(
          context,
          {MDString::get(context, op[0]), MDString::get(context, op[1]),
           MDString::get(context, op[2]),
           ConstantAsMetadata::get(ConstantInt::get(i32, kind))}));
    };
    for (const std::array<std::string, 3> &op : imports->libcalls)
      addOperand(op, 0);
    for (const std::array<std::string, 3> &op : variants)
      addOperand(op, 0);
    for (const std::array<std::string, 3> &op : imports->dataImports)
      addOperand(op, 1);
  }

  std::string assemblyName;
  /// --lto-emit-asm: the entry symbol when it is not main (else empty).
  std::string entry;
  /// --lto-emit-asm: the value of --msil-reloc-loop-threshold when it is
  /// given.
  std::optional<uint32_t> relocLoopThreshold;
  /// *-windows-msvc-msil: the UCRT stdio option bits of the link.
  std::optional<UCRTStdioBits> ucrtStdioBits;
  std::shared_ptr<const ImportAnnotation> imports;
  std::shared_ptr<const DiscardableCopies> discardable;
};
} // namespace

char MSILLinkHooksPass::ID = 0;

// The name of the output assembly: --assembly-name or -soname, else the stem
// of -o (only the last extension is dropped, so -o My.App.exe names the
// assembly My.App).
static std::string getAssemblyName() {
  if (!ctx.arg.assemblyName.empty())
    return ctx.arg.assemblyName.str();
  StringRef stem = sys::path::stem(ctx.arg.outputFile);
  if (stem.empty())
    stem = sys::path::filename(ctx.arg.outputFile);
  return stem.str();
}

// The linker name of \p gv, as the bitcode symbol table names it (without
// the '\1' prefix).
static std::string getSymbolName(const GlobalValue &gv) {
  std::string name;
  raw_string_ostream os(name);
  Mangler().getNameWithPrefix(os, &gv, /*CannotUsePrivateLabel=*/false);
  return name;
}

// True if the codegen triple's section rules define __start_<X> and
// __stop_<X> (ELF and Dotnet; plan 5.9.9, 5.18.5 LR9).
static bool definesStartStop() {
  return cli::definesStartStop(cli::getSectionRules(ctx.arg.target));
}

// True if \p name is __start_<X> or __stop_<X> on a triple that defines such
// names and a definition of \p m is in section <X>: whole-program code
// generation defines such a name (plan 5.9.6, 5.9.9), so it is neither bound
// nor reported.
static bool isSectionBoundary(const Module &m, StringRef name) {
  if (!definesStartStop())
    return false;
  StringRef section = name;
  if (!section.consume_front("__start_") && !section.consume_front("__stop_"))
    return false;
  return any_of(m.global_objects(), [&](const GlobalObject &go) {
    return !go.isDeclarationForLinker() && go.getSection() == section;
  });
}

// True if \p gv, a declaration named \p name, is a strong hidden or protected
// reference to __start_<X> or __stop_<X> on a triple that defines such names:
// a name that only the output can define, which never binds natively (plan
// 5.9.6).
static bool isHiddenSectionBoundaryReference(const GlobalValue &gv,
                                             StringRef name) {
  if (!definesStartStop() || gv.hasDefaultVisibility() ||
      gv.hasExternalWeakLinkage())
    return false;
  return name.starts_with("__start_") || name.starts_with("__stop_");
}

// The function definitions whose bodies code generation never compiles, so
// that the uses in them are no references: the ones that MSILPrepareIR
// drops as unreferenced discardable functions (findDroppedFunctions). The
// available_externally functions are never part of the output either.
using DroppedFunctions = SmallPtrSet<const Function *, 16>;

// True if the user \p u of a global value is a reference of the output: a
// use in a function or variable that the output defines, directly or through
// constants. An available_externally function or variable is not part of
// the output (code generation emits none, its symbol is undefined: plan
// 5.8.10, and MSILLinkHooksPass drops its body or initializer), and the
// backend drops the bodies of \p dropped, so the uses in them are not
// references. A constant that nothing uses counts as a reference, as
// Value::use_empty() counts it. \p visited holds the users already seen.
static bool isLiveUser(const User *start, const DroppedFunctions *dropped,
                       SmallPtrSetImpl<const User *> &visited) {
  auto isGone = [&](const Function *f) {
    return f->hasAvailableExternallyLinkage() ||
           (dropped && dropped->contains(f));
  };
  SmallVector<const User *, 8> worklist = {start};
  while (!worklist.empty()) {
    const User *u = worklist.pop_back_val();
    if (!visited.insert(u).second)
      continue;
    if (const auto *inst = dyn_cast<Instruction>(u)) {
      const Function *f = inst->getFunction();
      if (!f || !isGone(f))
        return true;
      continue;
    }
    if (const auto *g = dyn_cast<GlobalValue>(u)) {
      const auto *f = dyn_cast<Function>(g);
      if (f ? !isGone(f) : !g->hasAvailableExternallyLinkage())
        return true;
      continue;
    }
    if (u->use_empty())
      return true;
    append_range(worklist, u->users());
  }
  return false;
}

// True if the output refers to \p gv (isLiveUser).
static bool hasLiveUses(const GlobalValue &gv,
                        const DroppedFunctions *dropped = nullptr) {
  SmallPtrSet<const User *, 8> visited;
  return any_of(gv.users(),
                [&](const User *u) { return isLiveUser(u, dropped, visited); });
}

// True if the output takes the address of the function \p f: a reference
// (isLiveUser) other than the callee of a call.
static bool hasLiveAddressUse(const Function &f,
                              const DroppedFunctions *dropped) {
  SmallPtrSet<const User *, 8> visited;
  return any_of(f.uses(), [&](const Use &u) {
    const auto *call = dyn_cast<CallBase>(u.getUser());
    if (call && call->isCallee(&u))
      return false;
    return isLiveUser(u.getUser(), dropped, visited);
  });
}

// MSILPrepareIR's isUsedOutside on the merged module as the backend will see
// it: true if something other than the body of a function of \p bodies uses
// \p v, directly or through constants. The bodies and initializers of the
// available_externally functions and variables, which MSILLinkHooksPass
// drops before code generation, use nothing.
static bool isUsedOutside(const Value &v,
                          const SmallPtrSetImpl<const Function *> &bodies,
                          SmallPtrSetImpl<const Constant *> &visited) {
  for (const User *u : v.users()) {
    if (const auto *inst = dyn_cast<Instruction>(u)) {
      const Function *f = inst->getFunction();
      if (!bodies.contains(f) && !f->hasAvailableExternallyLinkage())
        return true;
      continue;
    }
    if (const auto *g = dyn_cast<Function>(u)) {
      if (g->hasAvailableExternallyLinkage())
        continue;
      if (!bodies.contains(g) || !g->hasPersonalityFn() ||
          g->getPersonalityFn() != &v)
        return true;
      continue;
    }
    if (const auto *gv = dyn_cast<GlobalVariable>(u);
        gv && gv->hasAvailableExternallyLinkage())
      continue;
    const auto *c = dyn_cast<Constant>(u);
    if (!c || isa<GlobalValue>(c))
      return true;
    if (visited.insert(c).second && isUsedOutside(*c, bodies, visited))
      return true;
  }
  return false;
}

// MSILPrepareIR's collectReferencedFunctions: adds to \p refs the functions
// that the body of \p f refers to, directly or through constants (not
// through the initializers of global variables), and its personality
// function.
static void
collectReferencedFunctions(const Function &f,
                           SmallVectorImpl<const Function *> &refs) {
  SmallPtrSet<const Constant *, 16> visited;
  SmallVector<const Constant *, 16> worklist;
  auto add = [&](const Value *v) {
    if (const auto *c = dyn_cast<Constant>(v); c && visited.insert(c).second)
      worklist.push_back(c);
  };
  for (const Instruction &inst : instructions(f))
    for (const Use &op : inst.operands())
      add(op.get());
  if (f.hasPersonalityFn())
    add(f.getPersonalityFn());
  while (!worklist.empty()) {
    const Constant *c = worklist.pop_back_val();
    if (const auto *g = dyn_cast<Function>(c))
      refs.push_back(g);
    else if (!isa<GlobalValue>(c))
      for (const Use &op : c->operands())
        add(op.get());
  }
}

// The function definitions of the merged module \p m whose bodies the
// backend drops as unreferenced discardable functions (decision D26:
// MSILPrepareIR's dropUnreferencedDiscardableFunctions, by the same
// reachability over the same candidates, on the module that msil-ld's
// MSILLinkHooksPass hands it): the linkonce and linkonce_odr definitions,
// the weak_odr copies that restoreDiscardableCopies makes linkonce_odr again
// (isRestoredDiscardableCopy with \p discardable), the local definitions in
// a comdat (the module has the flag msil-lto), and the local definitions
// that their bodies refer to, directly or through other candidates; a
// candidate is live when something other than a candidate's body uses it
// or a live candidate's body refers to it, and the others are dropped. The
// import analysis runs before code generation, so it must not count the
// references in those bodies (section 12 row 'M26.d extended (M24 leftover:
// the import analysis and D26's drop under --lto-emit-asm)').
static DroppedFunctions
findDroppedFunctions(const Module &m, const DiscardableCopies *discardable) {
  StringSet<> codegenNames = getCodegenNames();
  SmallPtrSet<const Function *, 16> candidates;
  SmallVector<const Function *, 16> order;
  for (const Function &f : m)
    if (!f.isDeclaration() &&
        (f.hasLinkOnceLinkage() || (f.hasLocalLinkage() && f.hasComdat()) ||
         isRestoredDiscardableCopy(f, discardable, codegenNames))) {
      candidates.insert(&f);
      order.push_back(&f);
    }
  for (unsigned i = 0; i != order.size(); ++i) {
    SmallVector<const Function *, 8> refs;
    collectReferencedFunctions(*order[i], refs);
    for (const Function *g : refs)
      if (!g->isDeclaration() && g->hasLocalLinkage() &&
          candidates.insert(g).second)
        order.push_back(g);
  }

  SmallPtrSet<const Function *, 16> live;
  SmallVector<const Function *, 16> worklist;
  for (const Function *f : order) {
    SmallPtrSet<const Constant *, 8> visited;
    if (isUsedOutside(*f, candidates, visited)) {
      live.insert(f);
      worklist.push_back(f);
    }
  }
  while (!worklist.empty()) {
    SmallVector<const Function *, 8> refs;
    collectReferencedFunctions(*worklist.pop_back_val(), refs);
    for (const Function *g : refs)
      if (candidates.contains(g) && live.insert(g).second)
        worklist.push_back(g);
  }

  DroppedFunctions dropped;
  for (const Function *f : order)
    if (!live.contains(f))
      dropped.insert(f);
  return dropped;
}

// True if the unbound declaration \p gv, whose linker name is \p name, keeps
// M12's null-plus-trap lowering (plan 5.9.6 step 3): its symbol's binding is
// WEAK, which it is only if every reference is weak (lld/ELF's rule,
// Symbol::resolve). The merged module's linkage cannot tell: LTO's IRMover
// keeps the first input's declaration, so an extern_weak declaration stays
// extern_weak when a later input's GLOBAL reference is linked in. Only a name
// that the symbol table does not know, a library function that the optimizer
// added, is decided by its declaration.
static bool isUnboundWeak(const GlobalValue &gv, StringRef name) {
  if (const Symbol *sym = symtab->find(name))
    return sym->isWeak();
  return gv.hasExternalWeakLinkage();
}

// Whether \p f takes argc, argv and envp as glibc passes them to an
// .init_array entry: three parameters whose CIL types are int32, native int
// and native int (int, char ** and char **), the shape for which
// whole-program code generation passes envp (MSILAsmPrinter's
// getConstructorArgs; section 12 row 'M22.b extended (Rust-found defects 2
// and 4)', item (4)).
static bool takesEnvp(const Function &f) {
  FunctionType *type = f.getFunctionType();
  if (type->isVarArg() || type->getNumParams() != 3)
    return false;
  auto isNativeInt = [](const Type *t) {
    return t->isPointerTy() ||
           (t->isIntegerTy() && t->getIntegerBitWidth() > 32 &&
            t->getIntegerBitWidth() <= 64);
  };
  const Type *argc = type->getParamType(0);
  return argc->isIntegerTy() && argc->getIntegerBitWidth() <= 32 &&
         isNativeInt(type->getParamType(1)) &&
         isNativeInt(type->getParamType(2));
}

// The function whose address the constructor \p c is (the function of an
// llvm.global_ctors entry, or the value of a constructor-table slot), or
// null: through pointer casts, zero-index GEPs, ptrtoint and inttoptr, and
// through the aliases that MSILPrepareIR's lowerAliases replaces by their
// aliasees before whole-program code generation sees the constructors
// (MSILAsmPrinter::collectConstructors), as the object writer's INIT entries
// resolve an alias to its aliased function too: a chain of aliases whose
// value types are function types, ending at a function at offset 0 (the IR
// verifier makes it a definition). MSILPrepareIR reports every other alias.
static const Function *getConstructorFunction(const Constant *c) {
  SmallPtrSet<const GlobalAlias *, 4> visited;
  while (true) {
    c = c->stripPointerCasts();
    if (const auto *ce = dyn_cast<ConstantExpr>(c);
        ce && (ce->getOpcode() == Instruction::PtrToInt ||
               ce->getOpcode() == Instruction::IntToPtr)) {
      c = ce->getOperand(0);
      continue;
    }
    const auto *ga = dyn_cast<GlobalAlias>(c);
    if (!ga)
      break;
    if (!ga->getValueType()->isFunctionTy() || !visited.insert(ga).second)
      return nullptr;
    c = ga->getAliasee();
  }
  const auto *f = dyn_cast<Function>(c);
  return f && !f->isIntrinsic() ? f : nullptr;
}

// The first constructor of \p m in run order (plan 5.9.9) that takes envp,
// or null: among the functions that llvm.global_ctors names and the function
// addresses in the 8-byte slots of the constructor-table sections
// (.init_array and .init_array.<N>), as whole-program code generation finds
// the constructors (MSILAsmPrinter::collectConstructors) and msil-ld orders
// those of the object that the LTO PE link writes
// (OutputImage::getConstructors): every constructor goes through
// getConstructorFunction, and cli::sortConstructors orders the entries by
// key (cli::getConstructorKey: an INIT entry's priority, clamped to 65535
// as AsmPrinter::preprocessXXStructorList and the object writer's INIT
// entries clamp it, or the priority of a slot's section), then the slots in
// (segment, offset) order (segments in IR order, as the data segment's
// entries and an object's SEGMENTS are), then the INIT entries in array
// order. In llvm.global_ctors, an element with a null function, or one that
// is null as a whole (zeroinitializer), ends the INIT entries, and one that
// is undef or poison as a whole is skipped, as whole-program code generation
// (preprocessXXStructorList after MSILAsmPrinter's getStructorList) and the
// object writer's INIT entries treat them.
// One difference remains: a slot's offset here counts the leaves of its
// initializer, not its bytes; in a table that code generation accepts every
// leaf is one 8-byte function address, so both order the slots alike (any
// other table is the link's error, and no output is written). Only the
// triples whose constructors take arguments (ELF rules) pass envp; the
// portable triples bind no environ (plan 5.18.6 PL2).
static const Function *findEnvpConstructor(const Module &m) {
  cli::SectionRules rules = cli::getSectionRules(ctx.arg.target);
  if (!cli::constructorsTakeArgs(rules))
    return nullptr;
  SmallVector<cli::ConstructorEntry, 4> entries;
  SmallVector<const Function *, 4> functions;
  auto add = [&](const Constant *c, cli::ConstructorEntry entry) {
    const Function *f = getConstructorFunction(c);
    if (!f || !takesEnvp(*f))
      return;
    entry.Id = functions.size();
    functions.push_back(f);
    entries.push_back(std::move(entry));
  };

  // The INIT entries, in array order.
  if (const GlobalVariable *ctors = m.getNamedGlobal("llvm.global_ctors");
      ctors && ctors->hasInitializer()) {
    if (const auto *list = dyn_cast<ConstantArray>(ctors->getInitializer())) {
      for (auto [i, op] : enumerate(list->operands())) {
        if (cast<Constant>(op.get())->isNullValue())
          break;
        const auto *cs = dyn_cast<ConstantStruct>(op.get());
        if (cs && cs->getNumOperands() >= 2 && cs->getOperand(1)->isNullValue())
          break;
        const auto *priority = cs && cs->getNumOperands() >= 2
                                   ? dyn_cast<ConstantInt>(cs->getOperand(0))
                                   : nullptr;
        if (!priority)
          continue;
        cli::ConstructorEntry entry;
        entry.Key = cli::getConstructorKey(
            static_cast<uint32_t>(priority->getLimitedValue(65535)), rules);
        entry.Offset = i;
        add(cs->getOperand(1), std::move(entry));
      }
    }
  }

  // The slots, in IR order of their variables and then in initializer order.
  for (auto [i, gv] : enumerate(m.globals())) {
    if (!gv.hasInitializer() || gv.isDeclarationForLinker() ||
        cli::classifySection(gv.getSection(), rules) !=
            cli::SectionClass::ConstructorTable)
      continue;
    uint64_t slot = 0;
    SmallVector<const Constant *, 8> worklist = {gv.getInitializer()};
    while (!worklist.empty()) {
      const Constant *c = worklist.pop_back_val();
      if (isa<ConstantAggregate>(c)) {
        for (const Use &op : reverse(c->operands()))
          worklist.push_back(cast<Constant>(op.get()));
        continue;
      }
      cli::ConstructorEntry entry;
      entry.Key = cli::getConstructorKey(gv.getSection(), rules);
      entry.IsSlot = true;
      entry.Segment = i;
      entry.Offset = slot++;
      add(c, std::move(entry));
    }
  }

  if (entries.empty())
    return nullptr;
  cli::sortConstructors(entries);
  return functions[entries.front().Id];
}

// True if \p m refers to a declaration of the C library's environ, which the
// import analysis binds natively or reports.
static bool refersToEnviron(const Module &m, const DroppedFunctions &dropped) {
  return any_of(m.globals(), [&](const GlobalVariable &gv) {
    return gv.isDeclarationForLinker() && getSymbolName(gv) == "environ" &&
           hasLiveUses(gv, &dropped);
  });
}

// The import annotation of plan 5.10.13 and the post-LTO report of plan
// 5.9.6, on the merged module \p m: every declaration that the module still
// refers to is bound or reported, and the library functions that the code
// generator may call are listed for !msil.imports. Errors use the texts of
// plan 5.9.14 and 5.10.14 with ">>> referenced by" lines. An
// available_externally function or variable is a declaration here: the
// output does not define it (plan 5.8.10), and MSILLinkHooksPass drops its
// body or initializer. Neither are the bodies of the discardable functions
// that the backend drops (findDroppedFunctions, with \p discardable). The
// variadic declarations that code generation expands (plan 5.15.2) bind to
// their va_list variants (rule 2, bindVaListVariant); the triple's
// fixed-arity natives, which MSILPrepareVariadics calls with fixed arity,
// and explicit imports bind as any function (rule 3); and the address of a
// natively bound variadic function is rule 4's error.
static void annotateImports(const Module &m, ImportAnnotation &out,
                            const DiscardableCopies *discardable) {
  DroppedFunctions dropped = findDroppedFunctions(m, discardable);
  // A variadic declaration with a prototype, and one that code generation
  // expands (ExpandVariadics, plan 5.15.2): not on the fixed-arity list of
  // the triple's C runtime, which the backend applies without the C runtime
  // identity of the link (getCRuntime with none), nor fixed-arity by its
  // attribute; with the conditions of ExpandVariadics' lowering (not naked,
  // the C calling convention).
  CRuntime tripleRuntime = getCRuntime(ctx.arg.target, StringRef());
  auto isVariadic = [](const Function &f) {
    return f.isVarArg() && !f.isIntrinsic() &&
           !f.hasFnAttribute("no-prototype");
  };
  auto isExpanded = [&](const Function &f, StringRef name) {
    return isVariadic(f) && !f.hasFnAttribute(Attribute::Naked) &&
           !f.hasFnAttribute("msil-fixed-arity") &&
           (f.getCallingConv() == CallingConv::C ||
            f.getCallingConv() == CallingConv::SPIR_FUNC) &&
           !isFixedArityFunction(name, tripleRuntime);
  };
  auto getRow = [](StringRef symbol, StringRef module, StringRef entry,
                   bool isData, const NativeExport *e) -> PrintedImport {
    auto *native = e ? dyn_cast_or_null<NativeLibraryFile>(e->File) : nullptr;
    return {symbol.str(),
            module.str(),
            entry.str(),
            isData,
            e ? e->Version.str() : std::string(),
            native && native->format == NativeLibraryFile::ELFShared};
  };
  // A P/Invoke of a reference of the module.
  auto record = [&](StringRef symbol, StringRef module, StringRef entry,
                    bool isData, const NativeExport *e) {
    if (ctx.arg.printImports)
      out.printed.push_back(getRow(symbol, module, entry, isData, e));
  };
  // A function that only code generation may call: a line once the
  // AsmPrinter declares its P/Invoke (MSILLinkHooksPass::doFinalization).
  auto recordCodegen = [&](StringRef symbol, StringRef module, StringRef entry,
                           const NativeExport *e) {
    if (ctx.arg.printImports)
      out.codegenRows.try_emplace(
          symbol, getRow(symbol, module, entry, /*isData=*/false, e));
  };

  // Looks up \p lookupName in the native libraries, or reuses late binding's
  // result for the symbol \p name (step 2 looked the symbol name up). A name
  // that no native library may define (plan 5.14.2, mayBindNatively) binds
  // nothing, so it is reported.
  auto lookup = [&](StringRef name, StringRef lookupName,
                    SmallVectorImpl<const NativeExport *> &rejected,
                    bool isFunctionReference) -> const NativeExport * {
    if (!mayBindNatively(name, ctx) || !mayBindNatively(lookupName, ctx))
      return nullptr;
    if (lookupName == name)
      if (auto *sym = dyn_cast_or_null<NativeSymbol>(symtab->find(name)))
        return sym->nativeExport;
    return ctx.nativeIndex.lookup(lookupName, rejected, isFunctionReference);
  };

  // Reports the reference \p name that no native library binds: undefined,
  // with plan 5.14.2's line for a name of classes N1-N3 (getNameClassLine),
  // a setjmp-family name (N4) as an error of each input that refers to it,
  // or the first definition that the lookup passed over: one that a P/Invoke
  // cannot bind, or data of a COFF import library or DLL, which a function
  // reference skips (plan 5.10.10).
  auto reportUnbound = [&](StringRef name,
                           ArrayRef<const NativeExport *> rejected) {
    if (rejected.empty() && isSetjmpFamilyName(name))
      symtab->reportSetjmpFamily(name, /*liveOnly=*/false);
    else if (rejected.empty())
      symtab->reportReference(
          ("undefined symbol: " + name + getNameClassLine(name, ctx)).str(),
          name);
    else if (rejected[0]->Why == NotBindable::None)
      symtab->reportReference((Twine("'") + name + "' in " +
                               rejected[0]->Module + " is data, but " +
                               symtab->getReferencingFile(name) +
                               " calls it as a function")
                                  .str(),
                              name);
    else
      symtab->reportReference(getNativeRejectionText(name, *rejected[0]), name);
  };

  // A P/Invoke cannot pass a native `long double` where it is wider than
  // double (plan 5.10.10).
  bool wideLongDouble = hasWideNativeLongDouble(ctx.arg.target);
  auto isNotPassable = [&](StringRef name) {
    return wideLongDouble && isNativeLongDoubleFunction(name);
  };

  // The definition of the merged module that /alternatename makes \p name
  // resolve to, or the name to look up natively instead.
  auto getAlternate = [&](StringRef name) -> StringRef {
    auto it = ctx.appliedAlternateNames.find(name);
    return it == ctx.appliedAlternateNames.end() ? StringRef() : it->second;
  };
  auto findDefinition = [&](StringRef name) -> const GlobalValue * {
    for (const GlobalValue &gv : m.global_values())
      if (!gv.isDeclarationForLinker() && getSymbolName(gv) == name)
        return &gv;
    return nullptr;
  };

  for (const Function &f : m.functions()) {
    if (!f.isDeclarationForLinker() || f.isIntrinsic())
      continue;
    std::string name = getSymbolName(f);
    if (isSectionBoundary(m, name))
      continue;
    // A trailer definition that the output is offered: the AsmPrinter
    // defines it in the module (plan 5.14.4 T6).
    if (findOfferedTrailerDef(name))
      continue;
    bool isUsed = hasLiveUses(f, &dropped);

    // An explicit import module is used as it is, unchecked (plan 5.10.7),
    // and exempt from the callback and `long double` checks. A variadic one
    // is called with fixed arity (rule 3), and its address is rule 4's error.
    StringRef module =
        f.getFnAttribute("msil-import-module").getValueAsString();
    StringRef importName =
        f.getFnAttribute("msil-import-name").getValueAsString();
    if (!module.empty()) {
      StringRef entry = importName.empty() ? StringRef(name) : importName;
      if (isUsed && isVariadic(f) && hasLiveAddressUse(f, &dropped))
        symtab->reportReference(getVariadicAddressText(name, module), name);
      else if (isUsed)
        record(name, module, entry, /*isData=*/false, nullptr);
      else
        recordCodegen(name, module, entry, nullptr);
      continue;
    }

    // An import name equal to the name counts as none (plan 5.9.6 step 2).
    StringRef lookupName = name;
    if (!importName.empty())
      lookupName = importName;
    if (StringRef to = getAlternate(name); !to.empty()) {
      if (const GlobalValue *def = findDefinition(to)) {
        out.replacements.push_back({f.getName().str(), def->getName().str()});
        continue;
      }
      lookupName = to;
    }

    SmallVector<const NativeExport *, 2> rejected;
    const NativeExport *e =
        lookup(name, lookupName, rejected, /*isFunctionReference=*/true);
    // Whether the symbol, lookup or entry name is in a list.
    auto isListed = [&](function_ref<bool(StringRef)> isInList) {
      return isInList(name) || isInList(lookupName) || isInList(e->Entry);
    };

    // A declaration that nothing refers to is neither reported nor checked,
    // but the code generator may still call it as a library function, with
    // its attributes, so it is bound quietly when a native library defines it
    // as a function, unless it is on the callback list, a P/Invoke cannot
    // pass its `long double`, or it is a variadic function that code
    // generation would expand (never a library call).
    if (!isUsed) {
      if (e &&
          (e->Kind == NativeKind::Function || e->Kind == NativeKind::NoType) &&
          !isListed(isNativeCallbackFunction) && !isListed(isNotPassable) &&
          !isExpanded(f, name)) {
        out.bindings.push_back(
            {f.getName().str(), e->Module.str(), e->Entry.str()});
        recordCodegen(name, e->Module, e->Entry, e);
      }
      continue;
    }

    if (!e) {
      // An unbound weak symbol keeps M12's lowering: null, and a trap stub
      // for calls (plan 5.9.6 step 3). An extern_weak declaration of a GLOBAL
      // symbol is reported here, so it never reaches that lowering.
      if (!isUnboundWeak(f, name))
        reportUnbound(name, rejected);
      continue;
    }
    // Data here comes from an ELF library, where the first definition in
    // link order binds whatever its type, as with ld.so (plan 5.10.10).
    if (e->Kind == NativeKind::Data) {
      symtab->reportReference(
          (Twine("'") + name + "' in " + e->Module + " is data, but " +
           symtab->getReferencingFile(name) + " calls it as a function")
              .str(),
          name);
      continue;
    }
    // Native code cannot call a managed function yet (plan 5.10.10,
    // 5.15.11): the functions of the callback list are errors, whether or not
    // the call passes a function.
    if (isListed(isNativeCallbackFunction)) {
      symtab->reportReference(
          (Twine("native function '") + name + "' (" + e->Module +
           ") takes a C function pointer; " +
           "passing C functions to native code is not supported for MSIL yet")
              .str(),
          name);
      continue;
    }
    // The IR signature of a native `long double` function says double
    // (decision D3), but the native function takes or returns an x87 or
    // binary128 value (plan 5.10.10). The helper archive's forwarders of the
    // functions that have a `double` counterpart are CIL objects, which
    // --lto-emit-asm text never links.
    if (isListed(isNotPassable)) {
      symtab->reportReference(getLongDoubleRejectionText(name, e->Module),
                              name);
      continue;
    }
    // Plan 5.15.2 rule 4: the address of a native variadic function needs
    // M28's adapter.
    if (isVariadic(f) && hasLiveAddressUse(f, &dropped)) {
      symtab->reportReference(getVariadicAddressText(name, e->Module), name);
      continue;
    }
    // Rule 2: an expanded declaration binds to its va_list variant, as
    // msil-import-name (rule 6), so that the AsmPrinter's P/Invoke keeps the
    // name and imports the variant ('as "vprintf"'). Its module goes to
    // !msil.imports, not to "msil-import-module", which would make it an
    // explicit import that the backend calls with fixed arity
    // (ImportAnnotation::Binding).
    if (isExpanded(f, name)) {
      VaListVariantBinding b = bindVaListVariant(name, *e, ctx);
      if (!b.Variant) {
        symtab->reportReference(std::move(b.Error), name);
        continue;
      }
      e = b.Variant;
      out.bindings.push_back(
          {f.getName().str(), e->Module.str(), e->Entry.str(), name});
    } else {
      out.bindings.push_back(
          {f.getName().str(), e->Module.str(), e->Entry.str()});
    }
    record(name, e->Module, e->Entry, /*isData=*/false, e);
  }

  // Undefined variables (plan 5.10.12): native data is imported at startup
  // through the data-import helper; an explicit import module is used as it
  // is, unchecked (plan 5.10.7).
  for (const GlobalVariable &gv : m.globals()) {
    if (!gv.isDeclarationForLinker() || gv.getName().starts_with("llvm.") ||
        !hasLiveUses(gv, &dropped))
      continue;
    std::string name = getSymbolName(gv);
    if (isSectionBoundary(m, name))
      continue;
    // A hidden or protected __start_<X> or __stop_<X> that late binding left
    // to code generation, because a local definition was in section <X>
    // before LTO removed it, binds nothing natively: it is reported as the
    // writer reports it for the LTO object without that segment (plan 5.9.6,
    // 5.9.9). A weak one keeps M12's null.
    if (isHiddenSectionBoundaryReference(gv, name)) {
      StringRef what = gv.hasProtectedVisibility()
                           ? "undefined protected symbol: "
                           : "undefined hidden symbol: ";
      symtab->reportReference((what + name).str(), name);
      continue;
    }
    StringRef module = gv.getAttribute("msil-import-module").getValueAsString();
    StringRef importName =
        gv.getAttribute("msil-import-name").getValueAsString();
    StringRef lookupName = importName.empty() ? StringRef(name) : importName;
    if (StringRef to = getAlternate(name); !to.empty()) {
      if (const GlobalValue *def = findDefinition(to)) {
        out.replacements.push_back({gv.getName().str(), def->getName().str()});
        continue;
      }
      lookupName = to;
    }

    if (!module.empty()) {
      StringRef entry = importName.empty() ? StringRef(name) : importName;
      out.dataImports.push_back({name, module.str(), entry.str()});
      out.boundVariables.push_back(gv.getName().str());
      record(name, module, entry, /*isData=*/true, nullptr);
      continue;
    }
    SmallVector<const NativeExport *, 2> rejected;
    const NativeExport *e =
        lookup(name, lookupName, rejected, /*isFunctionReference=*/false);
    if (e && e->Kind != NativeKind::Function) {
      out.dataImports.push_back({name, e->Module.str(), e->Entry.str()});
      out.boundVariables.push_back(gv.getName().str());
      record(name, e->Module, e->Entry, /*isData=*/true, e);
      continue;
    }
    if (e) {
      symtab->reportReference((Twine("symbol type mismatch: ") + name +
                               "\n>>> defined as function in " +
                               toString(e->File) + "\n>>> defined as data in " +
                               symtab->getReferencingFile(name))
                                  .str(),
                              name);
      continue;
    }
    if (!isUnboundWeak(gv, name))
      reportUnbound(name, rejected);
  }

  // envp of the constructors that take it (section 12 rows 'M22.b extended
  // (Rust-found defects 2 and 4)' item (4) and '5.9.10 amended (constructor
  // arguments)' of 2026-10-26): the value of the C library's environ. When
  // the module does not refer to environ itself, the text imports it as the
  // PE link of the same inputs does, from the first native data definition
  // in link order: a kind-1 operand of !msil.imports, which the AsmPrinter
  // reads, with the data-import helper below. Without such a definition the
  // link is the PE link's error, and no output is written.
  if (const Function *ctor = findEnvpConstructor(m);
      ctor && !refersToEnviron(m, dropped)) {
    SmallVector<const NativeExport *, 2> rejected;
    const NativeExport *e = ctx.nativeIndex.lookup(
        "environ", rejected, /*isFunctionReference=*/false);
    if (e && e->Kind != NativeKind::Function) {
      out.dataImports.push_back({"environ", e->Module.str(), e->Entry.str()});
      record("environ", e->Module, e->Entry, /*isData=*/true, e);
    } else {
      std::string ctorName =
          ctor->hasName()
              ? GlobalValue::dropLLVMManglingEscape(ctor->getName()).str()
              : getSymbolName(*ctor);
      error("global constructor '" + ctorName +
            "' takes envp, but no native library of the link defines "
            "'environ'");
    }
  }

  // The data-import helper (plan 5.10.12): on Windows triples LoadLibraryW
  // and GetProcAddress from kernel32.dll, elsewhere dlopen and dlsym as the
  // native libraries define them (findDataImportFunctions, which the output
  // writer shares); without them the imports are errors. The text is never
  // bound to the references (plan 5.11.8), so in a link with references the
  // error says so instead of naming NativeLibrary (plan 5.10.14).
  if (!out.dataImports.empty()) {
    SmallVector<DataImportFunction, 2> helpers;
    if (findDataImportFunctions(ctx.arg.target, ctx.nativeIndex, helpers)) {
      for (const DataImportFunction &h : helpers) {
        out.libcalls.push_back({h.name.str(), h.module.str(), h.entry.str()});
        recordCodegen(h.name, h.module, h.entry, h.def);
      }
    } else {
      for (const std::array<std::string, 3> &op : out.dataImports)
        symtab->reportReference(
            ctx.references->empty()
                ? getDataImportUnavailableText(op[0], op[1])
                : "data symbol '" + op[0] + "' in " + op[1] +
                      " cannot be imported: --lto-emit-asm text is never "
                      "bound to the references, and the native libraries "
                      "define no dlopen/dlsym",
            op[0]);
    }
  }

  // The library functions that the code generator may call (plan 5.10.13):
  // each name of RuntimeLibcalls without a '__' prefix (those are the
  // compiler-runtime and atomic names of plan 5.14.2, and the C library's
  // few) that the module neither defines nor declares, that may bind
  // natively (mayBindNatively) and that a native library binds as a
  // function, unless a P/Invoke cannot pass its `long double`.
  StringSet<> seen;
  for (const char *libcall :
       lto::LTO::getRuntimeLibcallSymbols(ctx.arg.target)) {
    StringRef name(libcall);
    if (name.starts_with("__") || !mayBindNatively(name, ctx) ||
        m.getNamedValue(name) || !seen.insert(name).second)
      continue;
    SmallVector<const NativeExport *, 2> rejected;
    const NativeExport *e =
        ctx.nativeIndex.lookup(name, rejected, /*isFunctionReference=*/true);
    if (!e ||
        (e->Kind != NativeKind::Function && e->Kind != NativeKind::NoType) ||
        isNotPassable(name) || isNotPassable(e->Entry))
      continue;
    out.libcalls.push_back({name.str(), e->Module.str(), e->Entry.str()});
    recordCodegen(name, e->Module, e->Entry, e);
  }
}

// msil-ld's LTO diagnostic handler: lld's (lld::diagnosticHandler), except
// that plan 5.9.8's signature mismatch of a helper's call inside LTO is plan
// 5.14.2 N-1's error, never 5.9.8's adapter and warning. On 5.9.8's LTO path,
// MSILFixFunctionBitcasts compares each call of a definition in an LTO module
// with it by cli::classifyCall and reports a difference as
// cli::DiagnosticInfoCallMismatch: a warning for an adapter, an error for a
// call that cannot be adapted, printed as 5.9.8's text minus its ' in <file>'
// parts. Only msil-ld knows the name classes (the N1 set is the codegen
// triple's, plan 5.14.2 N-2), so for a helper (isHelperName) msil-ld builds
// N-1's text from the diagnostic's name and printed shapes, as CallBinder
// does for the objects' calls: the first line names a helper, and the
// adapter's or the error's reason line goes. The text is an error, so the
// link stops before the backend's adapter reaches an output. Every other
// diagnostic, an ordinary name's mismatch included, is lld's. ThinLTO's
// backends call it from their threads.
static void handleLTODiagnostic(const DiagnosticInfo &di) {
  if (const auto *m = dyn_cast<cli::DiagnosticInfoCallMismatch>(&di)) {
    if (isHelperName(m->getName(), ctx)) {
      error("function signature mismatch: " + m->getName() +
            " (a toolchain or compiler runtime helper)\n>>> defined as " +
            m->getDefined() + "\n>>> called as " + m->getCalled());
      return;
    }
  }
  diagnosticHandler(di);
}

static lto::Config
createConfig(std::shared_ptr<const DiscardableCopies> discardable) {
  lto::Config c;
  c.LibraryOptions = &ctx.arg.libraryOptions;
  c.PTO = PipelineTuningOptions(c.LibraryOptions);
  c.Options = initTargetOptionsFromCodeGenFlags();

  // Always emit a section per function/data with LTO.
  c.Options.FunctionSections = true;
  c.Options.DataSections = true;

  c.DisableVerify = ctx.arg.disableVerify;
  c.DiagHandler = handleLTODiagnostic;
  c.OptLevel = ctx.arg.ltoo;
  c.CPU = getCPUStr();
  c.MAttrs = getMAttrs();
  c.CGOptLevel = ctx.arg.ltoCgo;

  // The codegen triple, an MSIL triple: inputs whose triple is its native
  // twin (stock-clang bitcode) are compiled for it too (lld-flavor 3.3).
  c.OverrideTriple = ctx.arg.target.str();

  // The MSIL backend computes every address at run time (decision D8).
  if (auto relocModel = getRelocModelFromCMModel())
    c.RelocModel = *relocModel;
  else
    c.RelocModel = Reloc::Static;

  // The MSIL target has no code models (decision D8: Small). Without a
  // configured one, LTO would take the "Code Model" module flag, which rustc
  // sets to kernel for x86_64-unknown-none, and creating the target machine
  // would stop with "Target does not support the kernel CodeModel", while
  // llc and clang -c compile the same module.
  c.CodeModel = CodeModel::Small;

  // --lto-emit-asm writes the ILAsm text of the whole program, with the
  // comments that llc and clang -S print (lld/ELF/LTO.cpp:169-172).
  if (ctx.arg.ltoEmitAsm) {
    c.CGFileType = CodeGenFileType::AssemblyFile;
    c.Options.MCOptions.AsmVerbose = true;
  }

  // Under --lto-emit-asm the merged module is the whole program: bind its
  // declarations and report what stays unresolved after LTO optimization,
  // before code generation, which an error skips (plan 5.9.6, 5.10.13). An
  // object-producing LTO link sets only the module flags: msil-ld binds the
  // LTO objects' symbols after LTO, like any object's.
  std::shared_ptr<ImportAnnotation> imports;
  if (ctx.arg.ltoEmitAsm) {
    imports = std::make_shared<ImportAnnotation>();
    c.PreCodeGenModuleHook = [imports, discardable](unsigned, const Module &m) {
      size_t errorsBefore = errorCount();
      *imports = ImportAnnotation();
      annotateImports(m, *imports, discardable.get());
      if (errorCount() == errorsBefore)
        return true;
      // Code generation, and with it MSILLinkHooksPass::doFinalization, does
      // not run: --print-imports prints the analysis's lines here.
      if (ctx.arg.printImports)
        printImports(imports->printed);
      return false;
    };
  }
  // The whole-program text's '<msil>Main' calls the entry point (plan 5.9.3,
  // D27): an entry other than main is the module flag msil-entry. The entry
  // symbol is visible to regular objects (LinkerDriver::linkerMain adds it
  // with SymbolTable::addUndefined), so LTO keeps its definition external.
  // CIL objects carry no entry point: the writer's '<msil>Main' calls it.
  std::string entry;
  if (ctx.arg.ltoEmitAsm && !ctx.arg.shared && !ctx.arg.entry.empty() &&
      ctx.arg.entry != "main")
    entry = ctx.arg.entry.str();
  // The text applies its data relocations through '<msil>relocs' above the
  // threshold that the PE writer uses (plan 5.9.10, O4): a given
  // --msil-reloc-loop-threshold reaches the AsmPrinter as a module flag,
  // which llc's -msil-reloc-loop-threshold (also through -mllvm) overrides.
  // The writer reads Config for the CIL objects' trailer, so object-producing
  // LTO needs no flag.
  std::optional<uint32_t> relocLoopThreshold;
  if (ctx.arg.ltoEmitAsm && ctx.arg.msilRelocLoopThresholdGiven)
    relocLoopThreshold = ctx.arg.msilRelocLoopThreshold;
  // The UCRT stdio option bits that the module initializer ORs in (plan
  // 5.15.2) reach the AsmPrinter as the module flags msil-ucrt-printf-bits
  // and msil-ucrt-scanf-bits, so that the whole-program text sets the bits
  // that the PE writer sets (O4). The opt-outs come from -l and from every
  // input's embedded requests, which the driver records as it loads the
  // inputs, all before compileBitcodeFiles creates this configuration. The
  // backend reads the flags on windows-msvc only.
  std::optional<UCRTStdioBits> ucrtStdioBits;
  if (ctx.arg.target.isWindowsMSVCEnvironment())
    ucrtStdioBits =
        UCRTStdioBits{ctx.arg.ucrtPrintfBits(), ctx.arg.ucrtScanfBits()};
  // The copies that the pass may make discardable again are BitcodeCompiler's
  // record of the link, complete once every bitcode input is added.
  c.PreCodeGenPassesHook = [name = getAssemblyName(), entry, relocLoopThreshold,
                            ucrtStdioBits, imports,
                            discardable](legacy::PassManager &pm) {
    pm.add(new MSILLinkHooksPass(name, entry, relocLoopThreshold, ucrtStdioBits,
                                 imports, discardable));
  };

  if (ctx.arg.saveTemps)
    checkError(c.addSaveTemps(ctx.arg.outputFile.str() + ".",
                              /*UseInputModulePath*/ true));
  return c;
}

// lld/wasm/LTO.cpp's undefine: once LTO runs, the prevailing bitcode
// definition is the LTO object's to define again. Nothing refers to the
// undefined symbol until an object does: a definition that LTO internalized
// or dropped stays unreferenced, so late binding and the undefined-symbol
// reports pass it over.
static void undefine(Symbol *sym) {
  Undefined(sym->getFile(), sym->getName(), ELF::STB_GLOBAL, sym->visibility(),
            sym->type, sym->tls)
      .overwrite(*sym);
  sym->referenced = false;
}

namespace lld::msil {
bool isThinLTO(const BitcodeFile *f) {
  Expected<std::vector<BitcodeModule>> modsOrErr =
      getBitcodeModuleList(f->obj->getFileBuffer());
  if (!modsOrErr) {
    consumeError(modsOrErr.takeError());
    return false;
  }
  for (BitcodeModule &bm : *modsOrErr) {
    Expected<BitcodeLTOInfo> info = bm.getLTOInfo();
    if (!info) {
      consumeError(info.takeError());
      continue;
    }
    if (info->IsThinLTO)
      return true;
  }
  return false;
}

BitcodeCompiler::BitcodeCompiler()
    : discardable(std::make_shared<DiscardableCopies>()) {
  // Initialize ltoObj. ThinLTO input and LTO partitions give one CIL object
  // per module; under --lto-emit-asm the driver rejects them before LTO runs.
  auto onIndexWrite = [&](StringRef s) { thinIndices.erase(s); };
  lto::ThinBackend backend = lto::createInProcessThinBackend(
      llvm::heavyweight_hardware_concurrency(ctx.arg.thinLTOJobs),
      onIndexWrite);
  ltoObj = std::make_unique<lto::LTO>(createConfig(discardable), backend,
                                      ctx.arg.ltoPartitions);

  // The sections that __start_<X> and __stop_<X> name (lld/ELF/LTO.cpp's
  // usedStartStop): their definitions stay visible, so that LTO keeps them
  // and the output defines the symbols (plan 5.9.9). Windows triples do not
  // define such symbols.
  if (ctx.bitcodeFiles.empty() || !definesStartStop())
    return;
  for (Symbol *sym : symtab->symbols()) {
    StringRef name = sym->getName();
    for (StringRef prefix : {"__start_", "__stop_"})
      if (name.starts_with(prefix))
        usedStartStop.insert(name.substr(prefix.size()));
  }
}

BitcodeCompiler::~BitcodeCompiler() = default;

// The IR names of the dllexport definitions of the bitcode input \p obj.
// irsymtab has no dllexport bit (it writes /EXPORT: linker options only for
// the COFF object format), so the modules are read lazily: their global
// values with their DLL storage classes, without function bodies. A module
// that cannot be read gives none (LTO reports it).
static StringSet<> getDLLExportNames(const lto::InputFile &obj) {
  StringSet<> names;
  Expected<std::vector<BitcodeModule>> mods =
      getBitcodeModuleList(obj.getFileBuffer());
  if (!mods) {
    consumeError(mods.takeError());
    return names;
  }
  for (BitcodeModule &bm : *mods) {
    LLVMContext context;
    Expected<std::unique_ptr<Module>> m =
        bm.getLazyModule(context, /*ShouldLazyLoadMetadata=*/true,
                         /*IsImporting=*/false);
    if (!m) {
      consumeError(m.takeError());
      continue;
    }
    for (const GlobalValue &gv : (*m)->global_values())
      if (gv.hasDLLExportStorageClass() && !gv.isDeclaration())
        names.insert(gv.getName());
  }
  return names;
}

// The UCRT's inline functions that return the address of its printf and
// scanf options words (plan 5.15.2, 'Startup options').
static bool isUCRTStdioOptionsFunction(StringRef name) {
  return name == "__local_stdio_printf_options" ||
         name == "__local_stdio_scanf_options";
}

void BitcodeCompiler::add(BitcodeFile &f) {
  lto::InputFile &obj = *f.obj;
  unsigned symNum = 0;
  ArrayRef<Symbol *> syms = f.getSymbols();
  std::vector<lto::SymbolResolution> resols(syms.size());
  // The IR names of the file's dllexport definitions, read on first use.
  std::optional<StringSet<>> dllExports;
  auto isDLLExport = [&](StringRef irName) {
    if (!dllExports)
      dllExports = getDLLExportNames(obj);
    return dllExports->contains(irName);
  };

  // Provide a resolution to the LTO API for each symbol.
  for (const lto::InputFile::Symbol &objSym : obj.symbols()) {
    Symbol *sym = syms[symNum];
    lto::SymbolResolution &r = resols[symNum];
    ++symNum;

    // Ideally we shouldn't check for SF_Undefined but currently IRObjectFile
    // reports two symbols for module ASM defined. Without this check, lld
    // flags an undefined in IR with a definition in ASM as prevailing.
    // Once IRObjectFile is fixed to report only one symbol this hack can
    // be removed.
    //
    // A definition in a discarded COMDAT copy enters the table as an
    // undefined symbol of the file that discarded it, so the symbol must
    // also be defined (or common) for this file's copy to prevail.
    r.Prevailing = !objSym.isUndefined() &&
                   (sym->isDefined() || sym->isCommon()) &&
                   sym->getFile() == &f;
    // Only the symbols that CIL objects refer to or define, the entry
    // symbol, -u symbols, NO_STRIP symbols, the definitions in a section
    // that __start_ or __stop_ names, the definitions of the names that the
    // trailer defines in this output and, under -shared, the definitions that
    // the library may export stay visible: every other definition is internal
    // to the merged module. irsymtab has no dllexport bit, so on Windows
    // triples the export selection applies to the LTO objects' DLLEXPORT
    // flags (plan 5.9.13). There is no --wrap (LinkerRedefined) in msil-ld.
    // A trailer definition's name (plan 5.14.4) is one that the code
    // generator calls after LTO (a single-thread fence becomes a call of
    // __msil_compiler_barrier), as LTO keeps the names of RuntimeLibcalls
    // external: the program's definition then wins over '<trailer>' in the
    // image and in --lto-emit-asm text, as an object's does (T5).
    // On Windows triples a definition in a COMDAT that is not dllexport
    // (the inline functions and selectany variables of headers, such as the
    // UCRT's sprintf) is never an export candidate, as Exports.cpp's
    // selection decides for CIL objects (section 12 rows 'Q22 as
    // implemented' and 'M26.d extended (M24 leftover: unused inline COMDAT
    // copies in -shared LTO links)'), so that LTO and the backend drop the
    // unused copies. On windows-msvc links the definitions of
    // __local_stdio_printf_options and __local_stdio_scanf_options (the
    // UCRT's inline functions, which only bitcode defines and calls) stay
    // too, so that msil-ld's startup options find them and the optimizer
    // cannot fold the reads of the options words (plan 5.9.13 as amended for
    // M26, 5.15.2's startup options); they stay COMDAT copies, which -shared
    // never exports.
    bool exportCandidate =
        ctx.arg.shared && r.Prevailing && mayBeExported(*sym) &&
        !(usesCOFFExportRules(ctx.arg.target) && objSym.getComdatIndex() >= 0 &&
          !isDLLExport(objSym.getIRName()));
    r.VisibleToRegularObj =
        sym->isUsedInRegularObj || sym->isNoStrip() ||
        (!objSym.getSectionName().empty() &&
         usedStartStop.contains(objSym.getSectionName())) ||
        (r.Prevailing && findOfferedTrailerDef(sym->getName())) ||
        exportCandidate ||
        (r.Prevailing && ctx.arg.target.isWindowsMSVCEnvironment() &&
         isUCRTStdioOptionsFunction(sym->getName()));
    // The prevailing definitions that no regular object needs: the copies
    // among them that LTO keeps external may become discardable again
    // (MSILLinkHooksPass::restoreDiscardableCopies).
    if (r.Prevailing && !r.VisibleToRegularObj && !objSym.getIRName().empty())
      discardable->irNames.insert(objSym.getIRName());
    // lld/wasm undefines the prevailing symbol here, so that the LTO object
    // defines it. Under --lto-emit-asm there is no LTO object, and the map
    // file shows the bitcode definitions.
    if (r.Prevailing && !ctx.arg.ltoEmitAsm)
      undefine(sym);
  }
  // A ThinLTO module may call the merged module's copies.
  if (!discardable->anyThinLTOModule && isThinLTO(&f))
    discardable->anyThinLTOModule = true;
  checkError(ltoObj->add(std::move(f.obj), resols));
}

void BitcodeCompiler::setBitcodeLibFuncs(ArrayRef<StringRef> bitcodeLibFuncs) {
  ltoObj->setBitcodeLibFuncs(bitcodeLibFuncs);
}

// Merge all the bitcode files we have seen, codegen the result
// and return the resulting objects.
SmallVector<InputFile *, 0> BitcodeCompiler::compile() {
  unsigned maxTasks = ltoObj->getMaxTasks();
  buf.resize(maxTasks);
  files.resize(maxTasks);
  filenames.resize(maxTasks);

  // The --thinlto-cache-dir option specifies the path to a directory in which
  // to cache native object files for ThinLTO incremental builds. If a path was
  // specified, configure LTO to use it as the cache directory.
  FileCache cache;
  if (!ctx.arg.thinLTOCacheDir.empty())
    cache = check(localCache("ThinLTO", "Thin", ctx.arg.thinLTOCacheDir,
                             [&](size_t task, const Twine &moduleName,
                                 std::unique_ptr<MemoryBuffer> mb) {
                               files[task] = std::move(mb);
                               filenames[task] = moduleName.str();
                             }));

  checkError(ltoObj->run(
      [&](size_t task, const Twine &moduleName) {
        buf[task].first = moduleName.str();
        return std::make_unique<CachedFileStream>(
            std::make_unique<raw_svector_ostream>(buf[task].second));
      },
      cache));

  if (!ctx.arg.thinLTOCacheDir.empty())
    check(pruneCache(ctx.arg.thinLTOCacheDir, CachePruningPolicy(), files));

  // --lto-emit-asm: the ILAsm text of the whole program goes to -o. LTO has
  // one task, the merged module: ThinLTO input and more than one partition
  // give one module per task, which only CIL objects can link, and the driver
  // reports them before LTO runs. Like lld's writers, msil-ld writes no
  // output after an error (msil-ld's import analysis reports unresolved
  // references and skips code generation; the backend reports errors found
  // during code generation, such as a VLA in a loop), and
  // the runs of LLD_IN_TEST that print nothing do not write standard output.
  if (ctx.arg.ltoEmitAsm) {
    assert(maxTasks == 1 && "ThinLTO and LTO partitions need CIL objects");
    if (errorCount() == 0 &&
        !(errorHandler().disableOutput && ctx.arg.outputFile == "-"))
      saveBuffer(buf[0].second, ctx.arg.outputFile);
    return {};
  }

  // Code generation reported an error: its objects may be incomplete.
  if (errorCount())
    return {};

  SmallVector<InputFile *, 0> ret;
  for (unsigned i = 0; i != maxTasks; ++i) {
    StringRef objBuf = buf[i].second;
    StringRef bitcodeFilePath = buf[i].first;
    if (files[i]) {
      // When files[i] is not null, we get the native relocatable file from the
      // cache. filenames[i] contains the original BitcodeFile's identifier.
      objBuf = files[i]->getBuffer();
      bitcodeFilePath = filenames[i];
    } else {
      objBuf = buf[i].second;
      bitcodeFilePath = buf[i].first;
    }
    if (objBuf.empty())
      continue;

    // If the input bitcode file is path/to/x.o and -o specifies a.out, the
    // corresponding native relocatable file path will look like:
    // path/to/a.out.lto.x.o.
    StringRef ltoObjName;
    if (bitcodeFilePath == "ld-temp.o") {
      ltoObjName =
          saver().save(Twine(ctx.arg.outputFile) + ".lto" +
                       (i == 0 ? Twine("") : Twine('.') + Twine(i)) + ".o");
    } else {
      StringRef directory = sys::path::parent_path(bitcodeFilePath);
      // For an archive member, which has an identifier like "d/a.a(coll.o at
      // 8)" (see BitcodeFile::BitcodeFile), use the filename; otherwise, use
      // the stem (d/a.o => a).
      StringRef baseName = bitcodeFilePath.ends_with(")")
                               ? sys::path::filename(bitcodeFilePath)
                               : sys::path::stem(bitcodeFilePath);
      StringRef outputFileBaseName = sys::path::filename(ctx.arg.outputFile);
      SmallString<256> path;
      sys::path::append(path, directory,
                        outputFileBaseName + ".lto." + baseName + ".o");
      sys::path::remove_dots(path, true);
      ltoObjName = saver().save(path.str());
    }
    if (ctx.arg.saveTemps)
      saveBuffer(objBuf, ltoObjName);
    if (InputFile *obj = createObjectFile(MemoryBufferRef(objBuf, ltoObjName)))
      ret.push_back(obj);
  }

  return ret;
}
} // namespace lld::msil
