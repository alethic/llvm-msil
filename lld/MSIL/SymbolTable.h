//===- SymbolTable.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/SymbolTable.h at 64f50ffd4f1c.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_SYMBOL_TABLE_H
#define LLD_MSIL_SYMBOL_TABLE_H

#include "InputFiles.h"
#include "LTO.h"
#include "Symbols.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include <memory>
#include <string>
#include <vector>

namespace llvm::cli {
struct MethodSig;
} // namespace llvm::cli

namespace lld::msil {
class ManagedLibraryFile;
struct ManagedExport;
struct NativeExport;

// SymbolTable is a bucket of all known symbols, including defined,
// undefined, or lazy symbols (the last one is symbols in archive
// files whose archive members are not yet loaded).
//
// We put all symbols of all files to a SymbolTable, and the
// SymbolTable selects the "best" symbols if there are name
// conflicts. For example, obviously, a defined symbol is better than
// an undefined symbol. Or, if there's a conflict between a lazy and a
// undefined, it'll read an archive member to read a real definition
// to replace the lazy symbol. The rules are lld/ELF's (Symbol::resolve,
// plan 5.9.5); addSymbol() applies them and checks that one name is not used
// for a function and for data, or for TLS and non-TLS data.
//
// The driver's order (plan 5.9.6, M20.b (7)): addFile() for every input,
// extracted member and native library, addUndefined() for -u and the entry,
// recordReferences(); then, when LTO writes CIL objects,
// compileBitcodeFiles() (which parses the LTO objects), bindLateSymbols(),
// finalizeObjectSymbols() and resolveAlternateNames(), before markLive() and
// the writer; under --lto-emit-asm, bindLateSymbols() before
// compileBitcodeFiles(), whose import-annotation pass (plan 5.10.13) reports
// unresolved references after LTO through reportReference().
class SymbolTable {
public:
  // Every symbol in the table, in insertion order.
  ArrayRef<Symbol *> symbols() const { return symVector; }

  // Parses \p file: a lazy file adds lazy symbols, a CIL object or a bitcode
  // file its symbols, and a native library its definitions to ctx.nativeIndex
  // (they bind late), a native object given on the command line its
  // definitions as machine code (NativeIndex::addFile). A library built by
  // this toolchain (ManagedLibraryFile, plan 5.9.15) joins
  // getManagedLibraries() in command-line order and is parsed; its exports
  // bind late (plan 5.9.6 step 1). Under --lto-emit-asm it is the error
  // "<file>: --lto-emit-asm cannot import from a .NET library; link without
  // --lto-emit-asm" (M22.c item 8). A library whose assembly name (compared
  // ignoring case) is the name of an earlier one is skipped when it has the
  // same bytes, and is otherwise the error "<file>: library assembly '<name>'
  // is also linked from <file>; the CLR loads one assembly of a name"; one
  // whose assembly name is the output's (getOutputAssemblyName, compared
  // ignoring case) is the error "<file>: library assembly '<name>' has the
  // name of the output; the CLR loads one assembly of a name". \p symName
  // names the symbol whose lazy definition extracted the file, for
  // diagnostics.
  void addFile(InputFile *file, StringRef symName = {});

  // The libraries built by this toolchain, in command-line order (plan
  // 5.9.6 step 1): late binding searches them in this order, and the writer
  // gives each its AssemblyRef and phase 2's 'ldtoken' of its first export
  // class, used or not (M22.b).
  ArrayRef<ManagedLibraryFile *> getManagedLibraries() const {
    return managedLibraries;
  }

  // Plan 5.9.15's checks of every library against the link
  // (ManagedLibraryFile::check: ABI version, triple key, data model, C
  // runtime, the symbol map, the library's own AssemblyRefs), with the first
  // loaded CIL object, else the first bitcode file, as the model. A bitcode
  // definition (default or protected visibility) that a library exports
  // becomes isUsedInRegularObj, so that LTO keeps it global and late binding
  // warns about it under LTO as through objects. Runs once:
  // recordReferences() calls it, once every input is loaded and before LTO,
  // and bindLateSymbols() calls it if it has not run.
  void checkManagedLibraries();

  // This function is where all the optimizations of link-time
  // optimization happens. Under --lto-emit-asm it writes the ILAsm text of
  // the whole program to -o and adds no files; otherwise the in-memory CIL
  // objects that LTO writes (ThinLTO and --lto-partitions give several) are
  // parsed like any object, with COMDATs ignored (every group kept), and
  // appended to ctx.objectFiles (ObjFile::isLTO, plan 5.9.13). The
  // prevailing bitcode definitions become undefined symbols first, so that
  // the LTO objects define them again (lld/wasm's undefine).
  void compileBitcodeFiles();

  // Records which bitcode inputs refer to each symbol, for the diagnostics
  // after LTO, which takes the inputs' symbol tables, and checks the
  // libraries built by this toolchain against the link
  // (checkManagedLibraries). The driver calls it once every input is loaded,
  // before LTO, in every mode (--interface-only and --lto-emit-asm
  // included).
  void recordReferences() {
    (void)getReferences(nullptr);
    checkManagedLibraries();
  }

  // The plan 5.9.6 rules for the symbols that are still undefined after every
  // input is loaded (M15: bitcode inputs and native libraries; M22: the
  // libraries built by this toolchain, step 1, when LTO writes CIL objects):
  // - step 2: an undefined symbol of default visibility, weak or global (a
  //   lazy symbol that only weak references met included), binds to the first
  //   bindable definition of its name in the native libraries, in link order
  //   (NativeFunction or NativeData); --trace-symbol prints "<file>: shared
  //   definition of <sym>". Bitcode symbol tables carry no import names, so
  //   the symbol name is looked up. A name that a native object given on the
  //   command line defines binds nothing (NativeIndex::lookup): the
  //   import-annotation pass reports a reference that the merged module
  //   keeps, and a referenced weak symbol is reported here, with plan
  //   5.10.14's "symbol '<s>' is defined only in native object code
  //   (<file>); MSIL cannot link machine code";
  // - an undefined weak symbol that nothing binds stays undefined, for the
  //   backend (null, and a trap stub for calls; step 3);
  // - a definition whose COMDAT copy was discarded and that an input still
  //   refers to is "relocation refers to a symbol in a discarded section";
  // - an undefined hidden (or internal) symbol is "undefined hidden symbol"
  //   and an undefined protected one "undefined protected symbol" (except
  //   the __start_X and __stop_X below); neither binds late;
  // - off Windows, an undefined __start_X or __stop_X is neither bound nor
  //   reported, whatever its visibility, when a bitcode definition is in
  //   section X: code generation defines it as data (plan 5.9.6, 5.9.9), so
  //   a function reference of it is plan 5.9.5's "symbol type mismatch"
  //   against data "in <internal>";
  // - any other undefined symbol, function or data, goes to LTO, whose
  //   import-annotation pass binds or reports what the merged module still
  //   refers to (plan 5.9.6 "When it runs", 5.10.13);
  // - an entry symbol (ctx.arg.entry, unless -shared) that is not defined is
  //   "undefined symbol: <entry>" with ">>> referenced by the entry point".
  //   The driver does not report it again.
  // It also records which inputs refer to each symbol, for reportReference(),
  // because LTO takes the inputs' symbol tables.
  //
  // When LTO writes CIL objects (no --lto-emit-asm), it runs after LTO, over
  // the CIL objects and the LTO objects, which carry import names (plan
  // 5.9.6 "When it runs"):
  // - first, a definition of a CIL object or LTO object (default or
  //   protected visibility) that a library also exports keeps its own
  //   definition, with the warning "symbol '<s>' is defined in <file> and
  //   exported by <lib>; the output uses its own definition" (Q21, the first
  //   library that exports it);
  // - step 1 (plan 5.9.15): a referenced undefined symbol of default
  //   visibility, weak or global, without an explicit import module (an
  //   EXPLICIT_IMPORT stub, IMPORT data with a module) binds by its symbol
  //   name to the first library in command-line order that exports it
  //   (ManagedFunction or ManagedData), unless a native object given on the
  //   command line defines that name (NativeIndex::findObjectDefinition):
  //   that machine code comes before every library, built by this toolchain
  //   or native, so the symbol binds nothing and the definition is kept for
  //   getRejectedNative(), which the writer reports for a live reference,
  //   weak or not; --trace-symbol prints "<lib>: managed
  //   definition of <s>". Each later exporter is the warning "symbol '<s>' is
  //   exported by <lib1> and <lib2>; using <lib1>". A function reference of a
  //   data export, or the reverse, is plan 5.9.14's "symbol type mismatch:
  //   <s>" (">>> defined as function in <file>", ">>> defined as data in
  //   <lib>"), and a thread-local reference "TLS attribute mismatch"; the
  //   symbol binds to the export all the same, so nothing reports it again;
  // - step 2: a referenced undefined symbol of default visibility binds by
  //   its import name (the ImplMap import name of its msil.unresolved stubs,
  //   the ImportName of IMPORT data, else the symbol name); a symbol with an
  //   explicit module (EXPLICIT_IMPORT stubs, IMPORT data with a module)
  //   binds to that module and entry unchecked (isExplicitImport). The
  //   definitions the lookup passed over are kept for getRejectedNative();
  // - an undefined __start_X or __stop_X does not bind natively off Windows
  //   when a CIL object has a segment named X: the writer defines it as data
  //   (plan 5.9.9), so a function reference of it, whatever its visibility,
  //   is plan 5.9.5's "symbol type mismatch" against data "in <internal>";
  // - step 3: the call signatures of each undefined weak function are
  //   recorded for its trap stubs (getUndefinedCalls);
  // - only the entry symbol is reported here: the writer reports the other
  //   undefined, hidden and discarded symbols that live code refers to (plan
  //   5.9.6 step 4, 5.9.11; reportUnresolved()).
  void bindLateSymbols();

  // lld-link's /alternatename rule, last (plan 5.9.6), when LTO writes CIL
  // objects: each <from> of ctx.appliedAlternateNames that late binding left
  // undefined resolves to <to>'s definition (a copy of <to>'s symbol under
  // <from>'s name; a method and its '<msil>fn_' field are shared). Under
  // --lto-emit-asm the import-annotation pass does this on the merged module.
  void resolveAlternateNames();

  // After resolution and late binding (plan 5.9.5, 5.9.9): marks the method
  // or segment of every CIL object definition that lost to another input's
  // definition as discarded (a WEAK copy overridden by a GLOBAL definition or
  // a common symbol), unless a symbol that won still lies in it (an alias of
  // the losing definition: the writer then names the method or field apart
  // from the winner's), and gives every common symbol that survived a
  // SyntheticSegment at the SYMTAB position of its first declaration, in
  // ctx.syntheticSegments (objects in load order, SYMTAB order).
  void finalizeObjectSymbols();

  // Defines the linker-defined DATA symbol \p name at \p offset bytes into
  // the data image (plan 5.9.9's __start_X and __stop_X) if an input refers
  // to it and nothing defines it: an undefined or weak lazy entry that is not
  // a function (bindLateSymbols() reports a function reference, plan 5.9.5)
  // becomes a SyntheticData, appended to ctx.sym.startStop. Returns null
  // otherwise.
  SyntheticData *addSyntheticData(StringRef name, uint64_t offset);

  // The import attributes of the undefined symbol \p sym that \p file gives
  // (plan 5.10.7, wasm-ld's updateExistingUndefined): an empty module or a
  // name equal to the symbol name counts as none, and an input that gives
  // none takes the other's. Two inputs that give different modules or names
  // are "import module mismatch for symbol: <s>" / "import name mismatch for
  // symbol: <s>", each with two ">>> defined as <value> in <file>" lines.
  // Ignored once \p sym is defined.
  void addImportAttributes(Symbol &sym, InputFile &file, StringRef module,
                           StringRef name);

  // The import module and import name that CIL objects gave \p sym (empty if
  // none).
  StringRef getImportModule(const Symbol *sym) const;
  StringRef getImportName(const Symbol *sym) const;

  // True if late binding bound \p sym to its explicit import module,
  // unchecked (plan 5.10.7): the writer exempts such a binding from the
  // native callback and `long double` checks.
  bool isExplicitImport(const Symbol *sym) const;

  // The native definitions that late binding's lookup passed over for \p sym
  // (a P/Invoke cannot bind them, or a function reference skipped COFF
  // data), in link order: the writer's 5.10.14 texts for a symbol that stays
  // undefined.
  ArrayRef<const NativeExport *> getRejectedNative(const Symbol *sym) const;

  // A direct call of an undefined weak function (plan 5.9.6 step 3): the
  // call-side signature (as the object encodes it) and SIGNATURES entry of
  // one call site's token, and the object.
  struct UndefinedCall {
    ObjFile *file;
    const llvm::cli::MethodSig *signature;
    const llvm::MSILLink::SignatureInfo *signatureInfo;
  };
  // The calls of the undefined weak function \p sym, in first-use order
  // (objects in load order, methods in row order, sites in code order), one
  // per object and signature, and one per signature across objects when the
  // signature names no type (Phase 2's C signatures): the writer makes one
  // '<msil>undef_<name>' trap stub per call signature.
  ArrayRef<UndefinedCall> getUndefinedCalls(const Symbol *sym) const;

  // Reports the error \p msg about \p sym with lld/ELF's ">>> referenced by"
  // lines (at most three, then ">>> referenced N more times"): the bitcode
  // inputs that referred to it before LTO, then the methods and segments of
  // the CIL objects whose token sites or relocations name it, as
  // "<file>:(<name>)" (only live ones if \p liveOnly; the LTO objects' only
  // if no bitcode input referred to it).
  void reportUnresolved(std::string msg, const Symbol *sym, bool liveOnly);

  // reportUnresolved() with plan 5.9.14's first line for the undefined
  // symbol \p sym: "relocation refers to a symbol in a discarded section:
  // <s>" with ">>> defined in <file>" for a discarded COMDAT copy, "undefined
  // hidden symbol: <s>" (hidden or internal), "undefined protected symbol:
  // <s>", else "undefined symbol: <s>".
  void reportUndefined(const Symbol *sym, bool liveOnly);

  // Reports the error \p msg about the symbol \p name, which LTO's merged
  // module refers to, in lld/ELF's form: up to three ">>> referenced by
  // <file>:(<definition>)" lines for the inputs whose symbol tables refer to
  // it, or ">>> referenced by ld-temp.o" for a name that only the merged
  // module refers to (a library function the optimizer introduced; plan
  // 5.9.6, 5.10.14).
  void reportReference(std::string msg, StringRef name);

  // The first input whose symbol table refers to \p name, or "ld-temp.o" (as
  // reportReference() names them).
  std::string getReferencingFile(StringRef name);

  // Returns the symbol of \p name, or null if no input has presented it.
  Symbol *find(StringRef name);

  // Returns the table's entry for \p name, creating a placeholder (which the
  // caller must resolve at once) for a new name.
  Symbol *insert(StringRef name);

  // Resolves \p newSym against the table's entry of its name (plan 5.9.5),
  // after checking the kind and TLS attribute (plan 5.9.14's "symbol type
  // mismatch" and "TLS attribute mismatch"). Returns the entry.
  Symbol *addSymbol(const Symbol &newSym);

  // Adds a reference that no input file makes: -u <sym>, --undefined=<sym>
  // and the entry symbol (lld/ELF's addUnusedUndefined and handleUndefined).
  // The symbol is visible to regular objects, so LTO keeps it. A lazy symbol
  // is extracted, and --why-extract records \p reference ("-u foo",
  // "--entry") as the referencing side.
  Symbol *addUndefined(StringRef name, StringRef reference);

  // Adds the lazy definition of \p name in \p file (an archive member or a
  // --start-lib file).
  Symbol *addLazy(StringRef name, InputFile *file);

  // Returns true if \p name is a COMDAT group that no earlier input presented;
  // that input keeps its copy of the group (plan 5.9.5).
  bool addComdat(StringRef name);

  // Set a flag for --trace-symbol so that we can print out a log message
  // if a new symbol with the same name is inserted into the symbol table.
  void trace(StringRef name);

private:
  // An input file that refers to a symbol, and the symbol's name in that
  // file's IR.
  struct Reference {
    BitcodeFile *file;
    StringRef irName;
  };

  // The inputs that refer to \p sym, in load order.
  ArrayRef<Reference> getReferences(const Symbol *sym);

  void reportUnresolved(std::string msg, ArrayRef<Reference> refs, bool isEntry,
                        ArrayRef<std::string> objectRefs = {});

  // Late binding of one symbol of CIL objects (plan 5.9.6 step 2).
  void bindObjectSymbol(Symbol &sym);
  // Late binding of one symbol to a library export (plan 5.9.6 step 1).
  // Returns true if a library exports it.
  bool bindManagedSymbol(Symbol &sym);
  // Before step 1: returns true, and keeps the definition for
  // getRejectedNative(), if a native object given on the command line
  // defines the name of \p sym, which has no explicit import module.
  bool rejectObjectDefinition(Symbol &sym);
  // The first library that exports \p name, or null.
  const ManagedExport *findManagedExport(StringRef name) const;

  // The libraries built by this toolchain, in command-line order, and
  // whether checkManagedLibraries() has run.
  SmallVector<ManagedLibraryFile *, 0> managedLibraries;
  bool librariesChecked = false;

  // The import attributes per undefined symbol, with the files that gave
  // them.
  struct ImportInfo {
    StringRef module;
    const InputFile *moduleFile = nullptr;
    StringRef name;
    const InputFile *nameFile = nullptr;
    bool explicitBinding = false;
  };
  llvm::DenseMap<const Symbol *, ImportInfo> importInfo;
  llvm::DenseMap<const Symbol *, SmallVector<const NativeExport *, 1>>
      rejectedNative;
  llvm::DenseMap<const Symbol *, SmallVector<UndefinedCall, 1>> undefinedCalls;

  // The undefined symbols of the bitcode files' symbol tables, by symbol,
  // collected once every input is loaded.
  llvm::DenseMap<const Symbol *, SmallVector<Reference, 1>> references;
  bool haveReferences = false;

  // Maps symbol names to index into the symVector.  -1 means that symbols
  // is to not yet in the vector but it should have tracing enabled if it is
  // ever added.
  llvm::DenseMap<llvm::CachedHashStringRef, int> symMap;
  std::vector<Symbol *> symVector;

  // Comdat groups define "link once" sections. If two comdat groups have the
  // same name, only one of them is linked, and the other is ignored. This set
  // is used to uniquify them.
  llvm::DenseSet<llvm::CachedHashStringRef> comdatGroups;

  // Set once LTO has started, to prevent further bitcode files being
  // included. (lld/wasm's static BitcodeFile::doneLTO: a member here, because
  // the driver creates a new SymbolTable for every link.)
  bool doneLTO = false;

  // For LTO.
  std::unique_ptr<BitcodeCompiler> lto;
};

extern SymbolTable *symtab;
} // namespace lld::msil

#endif
