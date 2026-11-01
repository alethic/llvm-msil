//===- NativeLibraries.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The native libraries of msil-ld (llvm/docs/MSIL/ImplementationPlan.md
// 5.10.8): the readers of ELF shared objects, COFF and MinGW import
// libraries, weak-alias objects (oldnames.lib), archives of native objects and
// DLL export tables, the built-in library of --ucrtbase-imports, and the
// index that late binding (plan 5.9.6 step 2) and the import-annotation pass
// (plan 5.10.13) look names up in. Everything is read through llvm::object.
//
// Native definitions bind late: they never preempt a definition of a bitcode
// input and never extract an archive member. A name binds to the first
// bindable definition of the right kind in link order (plan 5.10.10: a call
// passes over the data of COFF import libraries and DLLs); definitions that a
// P/Invoke cannot reach (a hidden ELF version, thread-local data, a
// GLIBC_PRIVATE symbol, machine code in a static archive) are kept for the
// diagnostics of plan 5.10.14.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_NATIVE_LIBRARIES_H
#define LLD_MSIL_NATIVE_LIBRARIES_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <cstdint>
#include <string>

namespace llvm {
class Triple;
} // namespace llvm

namespace lld::msil {
class InputFile;
class NativeLibraryFile;
struct Ctx;

// What a native definition is: an ELF STT_FUNC or STT_GNU_IFUNC symbol, a
// COFF IMPORT_CODE member or an export in a code section is a Function;
// STT_OBJECT and STT_COMMON, IMPORT_DATA and IMPORT_CONST members and exports
// in other sections are Data; STT_TLS is TLS; an ELF STT_NOTYPE symbol has no
// type and matches the kind of the reference.
enum class NativeKind : uint8_t { Function, Data, TLS, NoType };

// Why a P/Invoke cannot bind a native definition (plan 5.10.10): None for a
// bindable one; an ELF definition with only a non-default (hidden) version,
// thread-local data, a GLIBC_PRIVATE symbol, or a symbol of native object code
// (machine code: a native object of an archive, or one given on the command
// line).
enum class NotBindable : uint8_t {
  None,
  HiddenVersion,
  ThreadLocal,
  PrivateVersion,
  NativeCodeOnly
};

// One native definition of a name.
struct NativeExport {
  // The ModuleRef string: DT_SONAME, the file name, or the DLL file name,
  // verbatim (plan 5.10.11). Empty for NativeCodeOnly.
  StringRef Module;
  // The ImplMap import name; "#<n>" for an ordinal-only import.
  StringRef Entry;
  NativeKind Kind;
  NotBindable Why;
  // The ELF version (the default one of a bindable definition, the hidden one
  // of a HiddenVersion definition), for --print-imports and diagnostics.
  StringRef Version;
  // The library (for NativeCodeOnly, the archive member or the native object
  // given on the command line), for ">>> defined in" lines and the map file.
  InputFile *File;
};

// Every native definition per name, in link order, and the COFF weak aliases
// of oldnames.lib-style objects.
class NativeIndex {
public:
  // Adds the definitions of the native library \p f for the codegen triple
  // \p tt. The driver reads and classifies the file (plan 5.10.9: a PE with a
  // CLI header is managed, never native), and SymbolTable::addFile registers
  // it (ctx.nativeLibraryFiles, ctx.loadedFiles) before it hands it here.
  // Reports a malformed file, a file of the object format that \p tt does
  // not read (getOtherFormatMessage) and a file for another machine (plan
  // 5.10.14), and adds nothing from it. An empty \p tt (no --target and no
  // bitcode input) skips the format and machine checks. A native object given
  // on the command line (NativeLibraryFile::NativeObject) is read as an
  // archive member is, with the same checks: its global definitions are
  // machine code (NotBindable::NativeCodeOnly, with the object as their
  // file), and its COFF weak externals with IMAGE_WEAK_EXTERN_SEARCH_ALIAS
  // are aliases (addAlias), as those of oldnames.lib.
  void addFile(NativeLibraryFile &f, const llvm::Triple &tt);

  // Returns the first bindable definition of \p name in link order, following
  // COFF weak aliases (strdup -> _strdup) when \p name has none; the result
  // then names the alias target's entry. A function reference
  // (\p isFunctionReference) never binds a COFF import of data (IMPORT_DATA,
  // IMPORT_CONST, a long-format import of data) or a PE data export, which
  // define no <name> for lld-link, and the lookup goes on with the later
  // libraries; an ELF definition binds whatever its type, as with ld.so
  // (plan 5.10.10). Definitions that were passed over, because a P/Invoke
  // cannot bind them or because they are such data, are appended to
  // \p rejected in link order, for the diagnostics. Returns null if nothing
  // binds. A definition in a native object given on the command line
  // (findObjectDefinition) comes before every library's, whatever the order
  // of the inputs, because a native linker always links such an object: the
  // lookup returns null, with that definition first among those it appends
  // to \p rejected (section 12 of 2026-10-26).
  const NativeExport *
  lookup(StringRef name, llvm::SmallVectorImpl<const NativeExport *> &rejected,
         bool isFunctionReference) const;

  // Returns the first definition of \p name in a native object given on the
  // command line (isNativeObjectDefinition), or null. Late binding asks this
  // before it consults the libraries built by this toolchain (plan 5.9.6
  // step 1): such a definition is machine code that wins over every library.
  const NativeExport *findObjectDefinition(StringRef name) const;

  // Forgets every definition (Ctx::reset(), so that LLD_IN_TEST=2 passes).
  void clear();

  // Used by the readers.
  void add(StringRef name, const NativeExport &e);
  void addAlias(StringRef name, StringRef target);

private:
  llvm::DenseMap<llvm::CachedHashStringRef, SmallVector<NativeExport *, 1>>
      defs;
  llvm::DenseMap<llvm::CachedHashStringRef, StringRef> aliases;
  llvm::SpecificBumpPtrAllocator<NativeExport> alloc;
};

// Returns true if \p e is a definition in a native object given on the
// command line (NotBindable::NativeCodeOnly, its file of format
// NativeLibraryFile::NativeObject), not in a member of an archive.
bool isNativeObjectDefinition(const NativeExport &e);

// Returns true if \p name is a native function that takes a C function
// pointer (plan 5.10.14's callback list, glibc's __REDIRECT targets of its
// functions included, and the exit and thread-exit registrations of plan
// 5.15.11): passing C functions to native code is not supported for MSIL yet
// (plan 5.10.10).
bool isNativeCallbackFunction(StringRef name);

// The name classes of plan 5.14.2, which decide where a name may be resolved
// from. Later milestones call these predicates, never a copy of them. N3,
// the toolchain names ("__msil_*"), is llvm::cli::isToolchainName
// (llvm/CLI/TrailerDefs.h); N5 needs no predicate.

// N1, a compiler-runtime name: a name that starts with "__" in the
// RuntimeLibcalls set of the codegen triple, other than N2's names and the C
// library's own entry points that the set also lists (__memcpy_chk,
// __stack_chk_fail, ...; NativeLibraries.cpp lists them), which keep binding
// natively; and clang's complex helpers, which CodeGen calls by name and the
// set lacks: __mulhc3, __mulsc3, __muldc3, __mulxc3, __multc3, __divhc3,
// __divsc3, __divdc3, __divxc3 and __divtc3. The driver computes the set once
// per link (getCompilerRuntimeNames, ctx.compilerRuntimeNames). Objects and
// archive members (the helper archive clang_rt.builtins) define these names;
// native libraries never do.
bool isCompilerRuntimeName(StringRef name, const Ctx &ctx);

// N2, an atomic library name: "__atomic_*", libatomic's interface (its sized
// and generic entry points). On Linux triples the native libraries may
// define them (libatomic.so.1 through -latomic, as a native build links it);
// on Windows triples only objects and archive members do.
bool isAtomicLibraryName(StringRef name);

// N4, the setjmp family: exactly _setjmp, setjmp, __sigsetjmp, sigsetjmp,
// _setjmpex, _setjmp3, longjmp, _longjmp, siglongjmp and __longjmp_chk,
// which the compiler lowers. A reference that no object or archive member
// defines is getSetjmpFamilyText's error.
bool isSetjmpFamilyName(StringRef name);

// Returns false if no native library may define \p name (plan 5.14.2 N-2):
// an N1, N3 or N4 name, and on Windows triples an N2 name. Every path that
// binds a name natively asks it before it consults the native index: late
// binding (SymbolTable::bindLateSymbols: CIL objects, LTO objects, and the
// bitcode inputs under --lto-emit-asm), LTO's import annotation
// (!msil.imports: the references of the merged module, the quiet binding of
// unused declarations and the library calls of code generation) and
// /alternatename. Such a name stays undefined, and its report carries
// getNameClassLine's line (an N4 name: getSetjmpFamilyText).
bool mayBindNatively(StringRef name, const Ctx &ctx);

// Returns true if the calls of \p name must match its definition exactly
// (plan 5.14.2 N-1): an N1, N2 or N3 name. A call-site signature that differs
// from such a definition is never adapted: it is the error "function
// signature mismatch: <name> (a toolchain or compiler runtime helper)" with
// the ">>> defined as" and ">>> called as" lines, for the calls that the
// objects make (CallBinder) and for those inside the LTO modules (LTO's
// diagnostic handler) alike.
bool isHelperName(StringRef name, const Ctx &ctx);

// The N1 names of \p runtimeLibcalls, the RuntimeLibcalls set of the codegen
// triple (lto::LTO::getRuntimeLibcallSymbols): its "__" names, other than
// N2's and the C library's. The driver keeps the result in
// ctx.compilerRuntimeNames.
llvm::DenseSet<StringRef>
getCompilerRuntimeNames(ArrayRef<const char *> runtimeLibcalls);

// The line of plan 5.14.2 that the report of the undefined symbol \p name
// adds after its first line, before the ">>> referenced by" lines, with its
// leading newline: for an N1 name "\n>>> a compiler runtime helper: link the
// toolchain's helper archive clang_rt.builtins", or, while an input archive
// named clang_rt.builtins.lib or libclang_rt.builtins.a (plan 5.14.3 A1)
// takes part in the link, "\n>>> a compiler runtime helper that the
// toolchain's helper archive clang_rt.builtins does not define"; for an N2
// name "\n>>> an atomic operation: link with -latomic, as a native build
// does" (Linux) or "\n>>> an atomic operation (libatomic's interface)"
// (Windows); and for an N3 name "\n>>> a toolchain helper: link the
// toolchain's helper archive clang_rt.builtins; if it is linked, the object
// needs a newer toolchain". Empty for any other name.
std::string getNameClassLine(StringRef name, const Ctx &ctx);

// N4's error for \p file, an input that refers to the setjmp-family name
// \p name, which no object or archive member defines: "<file>: '<name>'
// cannot be bound to a native library: setjmp and longjmp are lowered by the
// compiler".
std::string getSetjmpFamilyText(StringRef name, StringRef file);

// The 5.10.14 text for \p e, a native definition of \p name that a P/Invoke
// cannot bind (e.Why is not NotBindable::None): machine code in an archive,
// a hidden ELF version, thread-local data, or a GLIBC_PRIVATE symbol.
std::string getNativeRejectionText(StringRef name, const NativeExport &e);

// The 5.10.12 text for data symbol \p name of native module \p module when
// the data-import helper has no library loader: no reference defines
// System.Runtime.InteropServices.NativeLibrary and the native libraries
// define no dlopen/dlsym. The output writer and the LTO driver share it.
std::string getDataImportUnavailableText(StringRef name, StringRef module);

// One native function of the data-import helper '<msil>dataimport' (plan
// 5.10.12): its name (the P/Invoke stub's), module and entry, and the
// definition that native lookup found (null for kernel32.dll's).
struct DataImportFunction {
  StringRef name;
  StringRef module;
  StringRef entry;
  const NativeExport *def;
};

// The helper's two native functions for the codegen triple \p tt, the
// library loader first, in \p out: on Windows triples LoadLibraryW and
// GetProcAddress of kernel32.dll, which every Windows process has; on the
// other triples dlopen and dlsym, bound from the native libraries like any
// import (the first bindable function definitions in link order). Returns
// false, and leaves \p out empty, when the native libraries define no
// dlopen or no dlsym.
bool findDataImportFunctions(const llvm::Triple &tt, const NativeIndex &index,
                             llvm::SmallVectorImpl<DataImportFunction> &out);

// Returns true if \p name is a native function that takes or returns a
// `long double` (the `l` functions of <math.h> and <complex.h>, nexttoward
// and nexttowardf, strtold, wcstold and glibc's `l` extensions), or one of
// glibc's _Float64x and _Float128 functions: a name in which "f64x" or
// "f128" is followed by its end or by '_' (sqrtf64x, strtof64x_l,
// f64xaddf128, __strtof128_internal). MSIL triples map `long double` to
// `double` (decision D3), so where the native `long double` is wider
// (hasWideNativeLongDouble) a P/Invoke cannot pass such a value (plan
// 5.10.10, 5.10.14): the helper archive forwards the functions that have a
// `double` counterpart, and a native binding of any listed name is
// getLongDoubleRejectionText's error.
bool isNativeLongDoubleFunction(StringRef name);

// Returns true if the native `long double` of the codegen triple \p tt is
// not IEEE double but x87 extended precision or binary128: x86_64 and
// aarch64 Linux (gnu, musl) and x86_64 windows-gnu. windows-msvc and aarch64
// windows-gnu use double.
bool hasWideNativeLongDouble(const llvm::Triple &tt);

// The 5.10.14 text for a native binding of \p name, a native `long double`
// function (isNativeLongDoubleFunction), to the native module \p module on a
// triple of hasWideNativeLongDouble: "native function '<name>' (<module>)
// takes or returns a native 'long double', which is not supported for MSIL;
// the helper archive clang_rt.builtins forwards the 'long double' functions
// that have a 'double' counterpart". Archive members resolve before native
// libraries (plan 5.10.10), so such a binding happens only when no member
// defines the name: the helper archive's msil_longdouble.c forwards each
// listed function that has a `double` counterpart (plan 5.14.3 A4), and the
// rest stay this error. The writer (CallBinder::checkNativeReferences) and
// LTO's import annotation report it; an explicit import (plan 5.10.7) is
// never checked, and windows-msvc binds the functions as any other.
std::string getLongDoubleRejectionText(StringRef name, StringRef module);

// The 5.10.14 text for the native library \p path of the object format that
// the codegen triple \p tt does not read (plan 5.10.8): "<path>: ELF file
// cannot be imported for target <triple>" on a Windows triple, and
// "<path>: PE/COFF file cannot be imported for target <triple>" (a native
// DLL, a COFF or MinGW import library) on any other.
std::string getOtherFormatMessage(const llvm::Twine &path,
                                  const llvm::Triple &tt);

// The kinds of a library file that -l finds (plan 5.10.9).
enum class LibraryKind : uint8_t {
  // An archive with an LLVM bitcode or CIL object member (or with no member
  // at all), a bitcode file, or a linker script under a static name that
  // names one.
  Static,
  // A PE with a CLI header: a .NET assembly.
  Managed,
  // An archive of COFF short import members, MinGW long-format import
  // objects or weak-alias objects.
  Import,
  // An ELF shared object, a native DLL, a linker script under a shared name,
  // and any file msil-ld cannot classify (reading it reports why).
  NativeShared,
  // An archive (or a linker script under a static name) of native objects
  // only: machine code, which -l skips.
  NativeOnly,
  // A native library of the object format that the codegen triple does not
  // read (plan 5.10.8): an ELF shared object on a Windows triple; a native
  // DLL or an import library on any other. -l skips it.
  OtherFormat,
};

// Classifies the contents \p mb of a library candidate for the codegen
// triple \p tt (an empty one accepts both object formats). A linker script
// is NativeShared here; the driver resolves the inputs of a script under a
// static name itself.
LibraryKind classifyLibrary(MemoryBufferRef mb, const llvm::Triple &tt);

// Returns true if \p mb is a member of an archive that the native index
// reads: a COFF short import member, a COFF object (import, alias or native
// code) or an ELF relocatable object.
bool isNativeArchiveMember(MemoryBufferRef mb);

// An input that a GNU ld script names (plan 5.10.8): a file name, or a
// library name for "-l<name>" (isLibrary, without the -l), with its offset in
// the script for diagnostics.
struct LinkerScriptInput {
  StringRef name;
  bool isLibrary;
  size_t offset;
};

// Returns true if \p mb is a GNU ld script that msil-ld reads: text whose
// first command is GROUP, INPUT, OUTPUT_FORMAT or SEARCH_DIR.
bool isLinkerScript(MemoryBufferRef mb);

// Reads the inputs of the GNU ld script \p mb in order: GROUP, INPUT and
// AS_NEEDED with file names and -l<name>; OUTPUT_FORMAT and SEARCH_DIR are
// ignored. A syntax error is returned in lld/ELF's ScriptParser form
// (getScriptDiagnostic); \p inputs then holds what came before it.
llvm::Error readLinkerScript(MemoryBufferRef mb,
                             llvm::SmallVectorImpl<LinkerScriptInput> &inputs);

// A diagnostic about the script \p mb at \p offset, as lld/ELF's
// ScriptLexer::setError formats it: "<script>:<line>: <msg>", then the line
// and a caret under the offset, each after ">>> ".
std::string getScriptDiagnostic(MemoryBufferRef mb, size_t offset,
                                const llvm::Twine &msg);
} // namespace lld::msil

#endif
