//===- ManagedCalls.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// msil-ld's checks of managed calls (llvm/docs/MSIL/ImplementationPlan.md
// 5.20.7 MK2-MK4, MK7). A managed call is a C or C++ declaration that names a
// .NET member; the backend gives it an internal stub method
// '__msil_mc.<symbol>' whose body converts the arguments, accesses the member
// and converts the result (5.20.5). The binder binds the member like any BCL
// request (MK1); these checks add what binding by signature cannot see:
//
// - MK2 (T-M7): an integer passed for a 'valuetype E' parameter, or a C
//   integer result read from a 'valuetype E' result or field, needs an enum
//   E whose underlying type has the integer's width. .NET Framework's JIT
//   silently accepts a scalar for a struct, so only the TypeDef tells a
//   wrong pairing apart.
// - MK2, MK3 (T-M8): a link without references has no TypeDef, so a managed
//   call that uses a value type other than GCHandle is an error there.
// - MK3 (T-M16): the 'valuetype' prefix and the boxed and 'this' conversions
//   must agree with the TypeDef's kind. An object gives a static method or
//   static field whose TYPE is 'valuetype T' a MemberRef on a TypeSpec
//   'valuetype T', which the checks read as that prefix; one whose TYPE
//   names T without it keeps T's TypeRef, which says nothing of the kind.
// - MK4 (T-M9): an msil_catch type must derive from System.Exception.
// - MK7 (T-M18): a field member must not be a literal (const) field, which
//   has no storage to read.
//
// The .NET references of C (plan 5.21.9, 5.21.10) pass through the stubs as
// 'object' values: a 'ref' argument and a 'this-ref' 'this' get a castclass
// to their class (none for System.Object), and a C cast from one class to
// another calls a cast stub, a managed call whose body is 'ldarg.0; castclass
// <TYPE>; ret' and makes no member access (RM8). MK3 checks the class that
// each castclass names as a reference type, in a stub's body (RL3) as in the
// attributes (RL2).
//
// The units that declare a managed call are checked here too (section 12 row
// '5.20.7 MK6 amended'), from one call each in ObjFile::parse and
// BitcodeFile::parse (T-M23: a definition of a reserved stub name, and
// T-RL1: one of a reference helper's, plan 5.21.10 RL4) and
// SymbolTable::recordReferences (T-M17 for the names that no bitcode input
// declares, then T-M21), from three in the BitcodeCompiler's constructor
// (T-M17 for the names that CIL objects and bitcode inputs both declare, then
// for the names that bitcode inputs declare, then T-M17 for a call of another
// function type or extension and T-M10 in the bitcode inputs' modules, in
// every LTO mode) and from two in
// SymbolTable::finalizeObjectSymbols (T-M17 for the CIL objects loaded after
// LTO, with the units that SymbolTable::compileBitcodeFiles recorded before
// LTO, then T-M21 again). A unit takes part in T-M17 only while its compiled
// code still declares the managed call: a CIL object by its stub, a bitcode
// input by its symbol table's undefined '__msil_mc.<symbol>'. A declaration
// that the unit's optimizer deleted with its last use takes no part, in every
// link mode.
//
// Object links read the stubs' bodies: the member access, the type tokens of
// the conversions, which stub parameter each unbox.any opens, and the
// filter's isinst tokens. A stub is a method '__msil_mc.<symbol>' with a
// LOCAL FUNCTION symbol (5.20.6 MW1 as amended); a GLOBAL or WEAK definition
// of such a name is T-M23 (checkReservedManagedCallNames), never a stub.
// --lto-emit-asm text, which binds nothing, reads the merged module's
// managed-call declarations (their msil-managed-* attributes, 5.20.4 MR1)
// instead, and only when the link has references (5.20.6 MW3).
//
// The member access of a 'this-val' or 'this-gch' instance method depends on
// the member's declaring type D, which only the binder's base-chain search
// knows (plan 5.20.3 MS5, 5.20.5 ML2 (2), section 12 row '5.20.3 MS5, 5.20.5
// ML2 (2) and 5.20.7 MK2 amended again'): 'call V::M' when V declares M, and
// 'constrained. V' + 'callvirt D::M' when V only inherits M from
// System.ValueType, System.Object or System.Enum. So does the access of a
// static method when V is a value type or an enum: the runtime resolves a
// method MemberRef upwards only from a reference-type parent, so it is 'call
// V::M' when V declares M and 'call D::M' when V only inherits M. Without
// LTO the backend writes the placeholder 'constrained. V' + 'callvirt V::M',
// or a static method's 'call V::M', which the writer rewrites in place after
// binding (bindManagedCallAccesses, applyManagedCallAccessRewrites); LTO code
// generation names D in the attribute "msil-managed-decl"
// (annotateManagedCallDeclaringTypes), so that the backend writes the final
// form itself.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_MANAGEDCALLS_H
#define LLD_MSIL_MANAGEDCALLS_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include <cstdint>
#include <optional>
#include <string>

namespace llvm {
class Module;
} // namespace llvm

namespace lld::msil {
class BCLBinder;
class BitcodeFile;
class InputFile;
class InputMethod;
class ObjFile;
class OutputModule;

/// One access of a stub, rewritten in its code copy (plan 5.20.5 ML2 (2) as
/// amended). A placeholder has the 6 bytes of 'constrained. V' at
/// \c prefixOffset and the 'callvirt' at \c accessOffset: with \c memberToken
/// 0 (V declares the member), the prefix becomes 6 nops and callvirt becomes
/// call; else the prefix stays and the callvirt's token becomes
/// \c memberToken. A static method's 'call' at \c accessOffset has no
/// \c prefixOffset, and its token becomes \c memberToken. A \c memberToken is
/// the output MemberRef with the member's name and signature on its declaring
/// type.
struct ManagedAccessRewrite {
  std::optional<uint32_t> prefixOffset;
  uint32_t accessOffset;
  uint32_t memberToken;
};
using ManagedAccessRewrites =
    llvm::DenseMap<const InputMethod *, SmallVector<ManagedAccessRewrite, 1>>;

/// Checks the managed-call stubs (the LOCAL '__msil_mc.<symbol>' methods) of
/// the kept methods of the CIL objects (LTO objects included) against
/// \p binder, the output's binder, in link order, and reports T-M7, T-M8,
/// T-M9, T-M16 and T-M18 as errors. It resolves types and emits no row. The
/// writer calls it once the kept methods are known and before it binds their
/// tokens.
void checkManagedCallStubs(BCLBinder &binder);

/// T-M23 (plan 5.20.3 and 5.20.6 MW1 as amended): a managed-call stub
/// '__msil_mc.<symbol>' is a toolchain name that only the backend defines, as
/// an internal function, which is a LOCAL FUNCTION symbol in an object. An
/// input that defines such a name otherwise (an asm label, hand-written IR)
/// would replace a managed call under LTO or pass for a stub in an object
/// link, so each GLOBAL, WEAK or COMMON definition of the CIL object \p f,
/// function or data, is "<file>: defines '<name>', which is reserved for the
/// MSIL toolchain (a managed-call stub)". The forwarders that clang registers
/// for a constructor or destructor managed call <s>, '__msil_mc_ctor.<s>' and
/// '__msil_mc_dtor.<s>', are toolchain names too: such a definition that is
/// not clang's forwarder (a WEAK, HIDDEN function in the COMDAT group of its
/// own name, with an INIT entry for a constructor or a FINI entry for a
/// destructor, in an object that declares <s> by its LOCAL stub
/// '__msil_mc.<s>') would replace or drop the startup call, and is the same
/// text ending "(a managed-call forwarder)". A LOCAL definition (the
/// internal forwarder of a 'static' managed call) is never reported. An LTO
/// object's names are its bitcode inputs', which are checked before the IR
/// merge. A dllexport forwarder is named <s> itself, which is no toolchain
/// name (T-M21 checks it).
///
/// T-RL1 (plan 5.21.10 RL4): the reference helpers '__msil_ref.<...>' (plan
/// 5.21.6 RI6; '__msil_ref.eq', the comparison of two .NET references) are
/// toolchain names that nothing defines: the backend makes each call of
/// '__msil_ref.eq' a ceq or a branch and refuses a definition of such a name
/// (T-RB7). A definition elsewhere (an asm label, hand-written IR, an object
/// of another toolchain) would replace the helper under LTO, so each GLOBAL,
/// WEAK or COMMON definition, function or data, is "<file>: defines '<name>',
/// which is reserved for the MSIL toolchain (a reference helper)". A LOCAL one,
/// which neither replaces the helper nor reaches another input, is not
/// reported, as the bitcode form cannot see one either.
///
/// ObjFile::parse calls it once the record is validated.
void checkReservedManagedCallNames(const ObjFile &f);

/// T-M23 and T-RL1 for each definition of the bitcode input \p f, before the
/// IR merge: irsymtab lists no local symbol, so an internal stub of
/// hand-written IR (msil-managed-stub) is not one, and neither is the
/// internal forwarder of a 'static' managed call or an internal definition of
/// a reference helper name (which code generation refuses, T-RB7). clang's
/// forwarder '__msil_mc_ctor.<s>' or '__msil_mc_dtor.<s>' is a WEAK, HIDDEN
/// function in the COMDAT group of its own name, in an input that declares
/// <s> (an undefined '__msil_mc.<s>'); irsymtab does not say which of
/// llvm.global_ctors and llvm.global_dtors lists it. BitcodeFile::parse calls
/// it first.
void checkReservedManagedCallNames(const BitcodeFile &f);

/// T-M17 in links with CIL objects (plan 5.20.4 MR4 as amended). For each
/// external managed call <s> that a CIL object (not an LTO object) declares
/// (its LOCAL stub '__msil_mc.<s>'), with the units that declare <s> in load
/// order, CIL objects and bitcode inputs (an undefined '__msil_mc.<s>' whose
/// declaration has the "msil-managed-type" attribute), when there are two or
/// more, whether or not a unit forwards <s> (dllexport, constructor or
/// destructor):
/// - the units' stubs differ: T-M17's first form, "managed call '<s>' is
///   declared with different 'msil_method' or 'msil_catch' arguments in
///   different translation units". A CIL object's stub is compared by its
///   text, which names what each token operand refers to instead of its
///   value; a bitcode input's is the stub that code generation makes of its
///   declaration alone (function type and attributes), as clang -c makes it
///   of the unit, compared the same way; each distinct declaration is
///   compiled once. Equal declarations give equal texts. An object names
///   every member in the scope [msil.bcl], and msil-ld binds each by name
///   against its references, so ASSEMBLY never makes two stubs differ;
/// - otherwise the units' attributes differ, which a CIL object shows by its
///   forwarders (a weak <s> in the COMDAT group <s>; the INIT_FINI entries of
///   '__msil_mc_ctor.<s>' and '__msil_mc_dtor.<s>' with their priorities)
///   and a bitcode input by the 8th operand of its !msil.managed.calls
///   entry: T-M17's second form, "managed call '<s>' is declared with
///   different 'dllexport', 'constructor' or 'destructor' attributes in
///   different translation units".
/// A name gets at most one of them, in every input order. A stub without a
/// text (one with a token of another kind or that does not decode; a bitcode
/// declaration whose stub code generation does not make, which slot P0 then
/// reports under LTO) leaves only the second form. The bitcode inputs among
/// themselves are checkBitcodeManagedCalls's.
///
/// checkObjectManagedCalls(), from SymbolTable::recordReferences(), decides
/// the names that no bitcode input declares, then reports T-M21
/// (checkExportedManagedCalls). checkMixedManagedCalls(), from the
/// BitcodeCompiler's constructor (before BitcodeCompiler::add gives the
/// inputs' symbol tables to LTO), decides the names that CIL objects and
/// bitcode inputs both declare and adds each name that gets T-M17 to
/// \p reported: a bitcode input whose modules do not read is the error
/// "<file>: <reason>", once, takes no part, and joins \p unreadable, which
/// BitcodeCompiler::add then does not give to LTO. It returns false if it
/// reported an error: BitcodeCompiler::compile then does not run LTO (which
/// would report the inputs that do not read again), so that no output is
/// written.
void checkObjectManagedCalls();
bool checkMixedManagedCalls(
    llvm::SmallPtrSetImpl<const BitcodeFile *> &unreadable,
    llvm::StringSet<> &reported);

/// T-M17 for the bitcode inputs, before LTO, in every LTO mode: full LTO with
/// one module or with --lto-partitions above 1, ThinLTO, --lto-emit-asm (plan
/// 5.20.4 MR4 as amended). No slot P0 sees the entries of exactly the units
/// that still declare a managed call: a merged module keeps the
/// !msil.managed.calls entry of a unit whose optimizer deleted the
/// declaration, ThinLTO merges no modules and brings another module's
/// entries only with an imported function, and every partition carries the
/// whole list. So, from the BitcodeCompiler's constructor beside
/// checkMixedManagedCalls, for each external managed call <s> that a bitcode
/// input declares (an undefined '__msil_mc.<s>' whose declaration has the
/// "msil-managed-type" attribute), whether or not a unit forwards it and
/// whether or not CIL objects declare it too, unless it is in \p reported
/// (checkMixedManagedCalls gave it T-M17), it applies slot P0's rule
/// (MSILManagedCalls.cpp's checkManagedCallsMetadata, then
/// cli::hasManagedCallOfOtherType) to the declaring inputs, then compares
/// their stubs as object and mixed links do, so that every LTO mode refuses
/// the same inputs, whatever ThinLTO imports, and what an object link of the
/// same units refuses:
/// - the entries of <s> in their modules differ: T-M17's second form if they
///   differ only in the attributes operand (the 8th, which one of them may
///   lack), else its first form. ASSEMBLY (the 5th operand) counts only
///   under --lto-emit-asm, as the scope that the text names
///   (cli::haveEqualManagedCallEntries): a link that writes an image binds
///   each member by its name, as object links do;
/// - otherwise their declarations' function types differ (as the IR linker
///   merges them): the first form;
/// - otherwise the stubs that code generation makes of their declarations
///   differ, each declaration compiled alone and compared by its text, as
///   checkMixedManagedCalls compares a bitcode input's: the first form. The
///   IR linker keeps the first declaration, with its parameter and result
///   attributes, and leaves each call its own, and slot P0 makes the one
///   stub of that declaration. A later unit's calls that match its own
///   declaration may then have a 'signext' or 'zeroext' of their own that
///   would change that stub (cli::hasManagedCallOfOtherExtension), calls
///   that checkBitcodeManagedCallUses, which applies that rule to each
///   unit's own module, does not see, and ThinLTO merges no declarations, so
///   declarations of one function type whose narrow results are signext in
///   one unit and zeroext in another (a 'short' and an 'unsigned short'
///   result on msil64-unknown-dotnet, whose stubs end in conv.i2 and
///   conv.u2) are refused here, in every LTO mode, as object links refuse
///   them. Attributes that do not change the stub (a signext and a zeroext
///   i8 or i16 parameter, which the stub passes as it is) give equal texts
///   and link, as they do as objects, and P0 accepts their calls in a merged
///   module. Declarations with equal function types and attribute lists
///   make equal stubs and are not compiled. A declaration without a stub
///   text (one whose stub code generation does not make, which slot P0 then
///   reports under LTO, or one with a token that does not decode) leaves the
///   name to LTO, as in a mixed link.
/// Two different entries of <s> in one input count too, once per name. An
/// input that llvm-link merged from several units keeps one declaration of
/// <s>, with the first unit's attributes, and every unit's entry, so a unit
/// whose own declaration its optimizer deleted still counts there beside a
/// unit that calls <s> (the limitation 'llvm-link'ed inputs keep one edge'),
/// and the stubs of its units are never compared with one another.
/// checkBitcodeManagedCallUses refuses such an input before LTO, by slot
/// P0's rule, as llc refuses such a module, where a later unit's call has
/// another function type, or a 'signext' or 'zeroext' of its own that would
/// change the first unit's stub, such as the signext call of a unit that
/// declared a 'short' result beside the zeroext declaration of an 'unsigned
/// short' one (cli::hasManagedCallOfOtherExtension). A bitcode input whose
/// modules do not read is the error "<file>: <reason>", once (not again
/// after checkMixedManagedCalls), takes no part, and joins \p unreadable.
/// Each name that gets T-M17 here joins \p reported, so that
/// checkBitcodeManagedCallUses skips it. It returns false if it
/// reported an error: BitcodeCompiler::compile then does not run LTO, and
/// neither the output nor a --save-temps bitcode file is written. When LTO
/// runs, MSILLinkHooksPass removes the list from every module that LTO
/// compiles, before slot P0, which so compares no entries (llc on a module
/// that msil-ld did not link keeps P0's whole check).
bool checkBitcodeManagedCalls(
    llvm::SmallPtrSetImpl<const BitcodeFile *> &unreadable,
    llvm::StringSet<> &reported);

/// Slot P0's T-M17 for a call of another function type or extension, and
/// T-M10, in the bitcode inputs, before LTO's optimizer, in every LTO mode:
/// full LTO with one module or with --lto-partitions above 1, ThinLTO,
/// --lto-emit-asm (plan 5.20.3 T-M10 and T-M17, 5.20.4 MR4 and 5.20.5 ML2
/// (1) as amended). Slot P0 reports T-M17's first form for a managed-call
/// declaration that a direct call calls with another function type, or with
/// a 'signext' or 'zeroext' of its own that would change its stub
/// (cli::hasManagedCallOfOtherType, cli::hasManagedCallOfOtherExtension),
/// and T-M10 for each use of any other declaration that is not a direct call
/// with its own function type, in the module that code generation gets,
/// which for clang -c is the unit after its own optimizer; under LTO it would
/// be the merged module or a ThinLTO module after LTO's optimizer, which can
/// delete such a call (in a function that LTO internalizes and finds unused)
/// or fold a use that a unit holds (an address in a global that LTO
/// internalizes, or compared with another address), and so link units that
/// clang -c refuses. So, from the BitcodeCompiler's constructor after
/// checkBitcodeManagedCalls, it reads the modules of each bitcode input that
/// LTO compiles (ctx.bitcodeFiles: the inputs and the archive members that
/// the link extracted, in load order) whose symbol table lists an undefined
/// '__msil_mc.' name, as the unit's own optimizer left them, without what no
/// output compiles: the bodies of their available_externally functions and
/// the initializers of their available_externally variables, which
/// EliminateAvailableExternallyPass drops before slot P0 in clang -c at -O1
/// and above, and in whose place LTO links another unit's definition (the
/// IR linker) or which it drops (DropImportedManagedCallHoldersPass in a
/// ThinLTO module). For each declaration there with the "msil-managed-type"
/// attribute, in the module's order:
/// - an external declaration whose name is in \p reported (T-M17 before
///   LTO) gets nothing more, as P0 gives no T-M10 after T-M17;
/// - one that P0's rule gives T-M17's first form in that module gets that
///   form, "managed call '<f>' is declared with different 'msil_method' or
///   'msil_catch' arguments in different translation units", with <f> its C
///   symbol (cli::getManagedCallSymbol, also for an internal declaration's
///   '.__uniq.<n>' name), once per name per link and never for a name in
///   \p reported, as clang -c reports it for the unit, and no T-M10;
/// - any other gets T-M10 for the holders of its other uses, which it finds
///   as P0 does (cli::collectManagedCallHolders): one line per holding
///   function, then one per holding global value, each "<file>: " (the input
///   as lld names it, an archive member as "<archive>(<member>)"), then
///   "function @<h>: " for a function whose instructions hold a use,
///   "global variable @<g>: " or "global value @<g>: " for a global value
///   whose initializer, aliasee, resolver, personality, prefix or prologue
///   holds one, then T-M10's text, "taking the address of the managed-call
///   function '<f>' is not supported for MSIL yet"
///   (cli::getManagedCallAddressText), with no source location and no
///   function type, each distinct line once per link, so that each input
///   prints its own lines.
/// The lines follow the inputs' order. A bitcode input whose modules do not
/// read is the error "<file>: <reason>", once (not again after the T-M17
/// checks), and joins \p unreadable. It returns false if it reported an
/// error: BitcodeCompiler::compile then does not run LTO, and neither the
/// output nor a --save-temps bitcode file is written. When LTO runs, slot P0
/// reports T-M17 or T-M10 in LTO's code generation only for a call or a use
/// that LTO's optimizer made, and for a declaration in an input whose symbol
/// table lists no '__msil_mc.' name (hand-written IR).
bool checkBitcodeManagedCallUses(
    llvm::SmallPtrSetImpl<const BitcodeFile *> &unreadable,
    const llvm::StringSet<> &reported);

/// The units that declared each external managed call before LTO took the
/// bitcode inputs' symbol tables (recordManagedCallUnits).
struct ManagedCallUnitRecord {
  /// Per external managed call <s> (a saved name): the CIL objects and
  /// bitcode inputs that declared it, in load order.
  llvm::MapVector<llvm::StringRef, llvm::SmallVector<InputFile *, 2>> units;
  /// The number of loaded files (ctx.loadedFiles) then: the CIL objects after
  /// them are archive members that the LTO objects extracted.
  size_t numLoadedFiles = 0;
};

/// SymbolTable::compileBitcodeFiles() records the units before LTO, unless
/// under --lto-emit-asm, which loads no CIL object.
ManagedCallUnitRecord recordManagedCallUnits();

/// T-M17 after LTO (plan 5.20.4 MR4 as amended): a CIL object that the link
/// loads after LTO, an archive member extracted for a reference that LTO's
/// code generation made, is compared as in an object or mixed link (stubs,
/// then attributes; ASSEMBLY never counts) for each external managed call
/// <s> that it declares, with the units that declared <s> before LTO
/// (\p record) and the other late objects. A bitcode input's declaration is
/// read again from its file, which msil-ld keeps (LTO took only its symbol
/// table); the LTO objects take no part, as their bitcode inputs stand for
/// them. A name gets at most one line; a link whose T-M17 before LTO
/// reported a name never gets here, since LTO then does not run.
/// SymbolTable::finalizeObjectSymbols() calls it before T-M21.
void checkLateManagedCalls(const ManagedCallUnitRecord &record);

/// T-M21 (plan 5.20.3 as amended, exported managed calls): clang defines the
/// C name <s> of a dllexport managed call on the Windows MSIL triples by a
/// weak_odr dllexport forwarder in the COMDAT group <s> that calls the unit's
/// stub. Any other non-local definition of <s> could replace it: a GLOBAL one
/// always (plan 5.9.5's ELF rule), a WEAK one or another COMDAT group <s>
/// when its input comes first. So, whatever the input order and whichever
/// definition resolution keeps, a forwarder input of <s> (a weak definition
/// <s> in its COMDAT group <s>, where the input declares the managed call
/// <s>: a CIL object's LOCAL stub '__msil_mc.<s>' or a bitcode file's
/// undefined '__msil_mc.<s>') and another loaded input that defines <s>,
/// GLOBAL or WEAK, function or data, without being a forwarder input of <s>
/// give "managed call '<s>' is exported from <A>, but <B> also defines
/// '<s>'", once per name, with <A> the first forwarder input and <B> the
/// first other definer in load order. The loaded inputs are the CIL objects,
/// the bitcode files and the archive members that the link extracted (one
/// that it never extracts takes no part), then the LTO objects. Runs from
/// checkObjectManagedCalls(), before LTO, and again from
/// SymbolTable::finalizeObjectSymbols(), with the LTO objects (whose bitcode
/// inputs LTO owns by then) and the archive members that they extracted.
void checkExportedManagedCalls();

/// Under --lto-emit-asm, with references: the same checks on the managed-call
/// declarations of the merged module \p m, before code generation, whose
/// slot P0 the errors would stop. So it skips every declaration that P0
/// reports itself: T-M17 for a direct call whose function type is not the
/// declaration's (cli::hasManagedCallOfOtherType) or that has a 'signext'
/// or 'zeroext' of its own that would change the declaration's stub
/// (cli::hasManagedCallOfOtherExtension), T-M10 (a use that is not a direct
/// call), and attributes that P0 refuses as invalid, the IR types that do
/// not fit their conversions with Sema's widths included. The calls and
/// uses that P0 refuses here are only those that LTO's optimizer made and
/// those of an input whose symbol table lists no '__msil_mc.' name
/// (checkBitcodeManagedCallUses). P0 compares no !msil.managed.calls entries
/// here: checkBitcodeManagedCalls decided T-M17 before LTO, which runs only
/// when no name got it and checkBitcodeManagedCallUses reported nothing, and
/// MSILLinkHooksPass removes the list.
void checkManagedCallDeclarations(const llvm::Module &m);

/// After the writer has bound the kept methods' tokens: the declaring type of
/// each 'constrained. V' + 'callvirt X::M' access of a kept stub, from \p
/// binder's member search for V::M with the MemberRef's signature. A
/// placeholder (X is V) gets its rewrite, and a MemberRef on D, which \p
/// binder binds, when V only inherits M; a site already in final form (X is
/// D, from LTO) needs none, and any other X is an error (MK2's stub shape).
/// A stub's access 'call V::M' of a static method gets the MemberRef on D
/// when V is a value type or an enum (by its TypeDef, whatever TYPE's
/// spelling) that only inherits M; any other V keeps it, as does a site
/// from LTO that names D already and every site of a link without
/// references.
ManagedAccessRewrites bindManagedCallAccesses(OutputModule &out,
                                              BCLBinder &binder);

/// Applies \p rewrites to \p code, a copy of a stub's code whose token
/// operands are already the output's. Code size, branch offsets, .maxstack
/// and the clauses do not change.
void applyManagedCallAccessRewrites(ArrayRef<ManagedAccessRewrite> rewrites,
                                    llvm::MutableArrayRef<uint8_t> code);

/// In LTO code generation (--lto-emit-asm and an object-producing LTO link),
/// before the AsmPrinter writes the stub bodies: sets "msil-managed-decl"
/// (cli::ManagedDeclAttr) to the full name of the bound member's declaring
/// type on every 'this-val' or 'this-gch' instance method managed call of
/// \p m, declaration or stub, and on every static method managed call whose
/// TYPE is a value type or an enum (by its TypeDef) that only inherits the
/// member, binding against the link's references or the built-in table. One
/// that does not bind gets no attribute: --lto-emit-asm text then has the
/// backend's standalone final form ('constrained. V' and 'callvirt
/// [mscorlib]System.Object::M' for System.Object's four public instance
/// methods, else 'call V::M'), and an object-producing link has the access
/// that the writer rewrites after binding. A cast stub (plan 5.21.9 RM8),
/// which accesses no member, never gets it. Thread-safe (ThinLTO and
/// --lto-partitions run code generation on several threads).
void annotateManagedCallDeclaringTypes(llvm::Module &m);
} // namespace lld::msil

#endif
