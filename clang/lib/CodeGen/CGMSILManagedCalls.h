//===--- CGMSILManagedCalls.h - MSIL managed calls from C/C++ ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The IR of an MSIL managed call (ImplementationPlan.md 5.20.4): a function
// declaration with msil_method, which clang/lib/Sema/SemaMSILManagedCall.cpp
// has checked, becomes an IR declaration '__msil_mc.<symbol>' with the
// managed-call attributes, the forwarders that dllexport, constructor and
// destructor ask for, and an operand of !msil.managed.calls. This header is
// private to clang/lib/CodeGen; CodeGenModule.cpp calls each entry point on
// the MSIL object-format triples only.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGMSILMANAGEDCALLS_H
#define LLVM_CLANG_LIB_CODEGEN_CGMSILMANAGEDCALLS_H

#include "clang/AST/GlobalDecl.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace llvm {
class Function;
class Module;
} // namespace llvm

namespace clang {

class CLIReferenceType;
class FunctionDecl;

namespace CodeGen {

class CodeGenModule;

/// getMangledName's step (plan 5.20.4 MR2 as amended): the IR declaration of
/// a function with msil_method is '__msil_mc.<symbol>', <symbol> being the
/// name \p MangledName that the declaration \p GD would otherwise have (the C
/// name, the asm label or the C++ mangled name), which this replaces. With
/// internal linkage (a C 'static' function, a C++ anonymous namespace) it is
/// '__msil_mc.<symbol>.__uniq.<n>', <n> hashing the strings of the
/// declaration's !msil.managed.calls operands 1-6 and its function type (its
/// C types, and the IR type and extension that the C ABI gives its result
/// and each parameter), not the main file's name: the internal declarations
/// of two units share an IR name only when their stubs are the same, so the
/// IR linker may merge them, and !msil.managed.calls (MR4) never sees two
/// different ones under one name. The postfix that
/// -funique-internal-linkage-names gives <symbol> is dropped first, so that
/// the name ends with exactly one '.__uniq.<n>' and
/// llvm::cli::getManagedCallSymbol gives <symbol> back. Any other declaration
/// keeps \p MangledName.
void mangleMSILManagedCallName(CodeGenModule &CGM, GlobalDecl GD,
                               std::string &MangledName);

/// SetFunctionAttributes' step: gives the IR declaration \p F of the
/// managed-call function \p FD, when \p FD has no body, the attributes of
/// plan 5.20.4 MR1: TYPE, MEMBER and SIGNATURE as written, ASSEMBLY when
/// given, the conversion of each C parameter and of the result as Sema
/// decided and recorded them on the attribute (its fake Conversions argument,
/// already in the "msil-managed-conv" encoding) and the ordered msil_catch
/// list. Every declaration of FD carries the same arguments (MS2) and the
/// conversions of the first, so the most recent one is read. Without recorded
/// conversions (Sema did not classify the declaration, and reported why)
/// nothing is added.
void addMSILManagedCallAttributes(const FunctionDecl &FD, llvm::Function &F);

/// The cast stub declaration (plan 5.21.9 RM8) that a C cast of a .NET
/// reference to the class of \p Target calls (RG5, emitMSILReferenceCast in
/// CGMSILReferences.h), created in \p CGM's module on first use: a
/// declaration 'target("msil.ref") (target("msil.ref"))' named
/// '__msil_mc.<castclass>.__uniq.<n>' (llvm::cli::ManagedCastSymbol), <n>
/// from llvm::cli::getManagedCallUniqPostfix over the strings of its
/// !msil.managed.calls operands 1-6 and its function type, with MR1's
/// attributes: "msil-managed-type" the class's display name
/// (llvm::cli::getCLITypeDisplayName of \p Target's key; a class, System.String
/// or an array, never System.Object, to which a cast emits nothing),
/// "msil-managed-member" '.castclass', "msil-managed-sig" 'object(object)'
/// and "msil-managed-conv" 'ref;ref' (llvm::cli::isManagedCastShape), and no
/// memory attributes, so that an unused cast still runs and throws. Its
/// !msil.managed.calls operand comes with every managed-call declaration's
/// (emitMSILManagedCallsMetadata). Equal casts of two units have one name, so
/// LTO merges them, and each class has its own stub.
llvm::Function *getOrCreateMSILCastDeclaration(CodeGenModule &CGM,
                                               const CLIReferenceType &Target);

/// EmitGlobal's step for the declaration \p GD: a managed-call declaration
/// with dllexport, constructor or destructor gets its IR declaration now,
/// where the declaration is, as a call would give it, and
/// emitMSILManagedCallForwarders defines its forwarders also when the
/// translation unit never calls it. Such a declaration in an AST file reaches
/// EmitGlobal from the AST reader: from a PCH or a header module (a module-map
/// module or a header unit) as the unit loads that file, and from a C++20 named
/// module when the module unit's object is compiled from its BMI or when a unit
/// names the managed call (clang/lib/Serialization/MSILManagedCallDecls.h). The
/// walk in emitMSILManagedCallForwarders declares the managed calls whose
/// attribute comes from a declaration that never reaches EmitGlobal (one at
/// block scope) or from one after the declaration that EmitGlobal saw. However
/// often a managed call is declared (from the AST reader, by EmitGlobal for
/// each redeclaration, by a call or by the walk), it has one IR declaration,
/// which GetAddrOfFunction finds by its IR name, and its forwarders are defined
/// once, at the end of the unit.
void declareMSILManagedCallWithForwarders(CodeGenModule &CGM, GlobalDecl GD);

/// Release's step before the constructor and destructor lists are emitted
/// (plan 5.20.4 as amended). It first walks the declarations that the
/// translation unit wrote, block scope included, and gives each managed call
/// whose latest declaration has dllexport, constructor or destructor its IR
/// declaration, as a call would give it. The latest declaration is the most
/// recent one outside template patterns: a block-scope redeclaration in a
/// template counts once instantiated. The walk reads noload_decls(): it
/// deserializes nothing from a PCH or a module this late. A managed call that a
/// PCH or a header module declares with one of the three needs no walk, whether
/// or not the unit names it: the AST file lists that declaration among its
/// eagerly deserialized declarations, and the AST reader passes it to the
/// consumer, so EmitGlobal declared it as the unit loaded that file
/// (declareMSILManagedCallWithForwarders; the predicate is in
/// clang/lib/Serialization/MSILManagedCallDecls.h). For a header module, that
/// holds in every unit that loads its module file, also one that does not
/// import it, and for the managed calls of every submodule, an explicit one
/// included, as it does for a function with the constructor attribute that a
/// header of the module defines. A C++20 named module lists the declaration
/// only for its own object, among its modular-codegen declarations, which the
/// AST reader deserializes eagerly only in a compile of that module file. So
/// the module unit's object defines the forwarders, whether compiled from its
/// source or from its BMI. An importing unit that names the managed call (a
/// call, a redeclaration or an unevaluated operand) deserializes the
/// declaration, which the AST reader passes to the consumer too, so that unit
/// defines the forwarders as well, and COMDAT keeps one per program. After the
/// walk, the step checks the managed-call functions that share one IR name
/// (T-M24) among the functions that \p MangledDeclNames names, which are all
/// those the translation unit uses and those that the walk and EmitGlobal
/// declared. It then defines the forwarders of the managed calls with
/// dllexport, constructor or destructor, read from the latest declaration of
/// each managed call (of the first written of the functions that share its IR
/// name) at the end of the translation unit, also when nothing calls it. For
/// the managed call '__msil_mc.<s>' (getManagedCallSymbol <s>):
///   - dllexport (the Windows triples): '<s>', weak_odr dllexport in a COMDAT
///     of its name, returning the managed call's result, so that the C name
///     is exported once while the stub stays a LOCAL symbol;
///   - constructor: '__msil_mc_ctor.<s>', registered in llvm.global_ctors;
///   - destructor: '__msil_mc_dtor.<s>', registered in llvm.global_dtors.
/// The constructor and destructor forwarders discard the result. For an
/// external declaration they are linkonce_odr hidden in a COMDAT of their
/// name, which keys their entry, so they run once per program; for an
/// internal one they are internal with an unkeyed entry, once per unit. An
/// entry's priority is the attribute's, 65535 when none is given. A
/// destructor entry is added to llvm.global_dtors directly, never through
/// atexit (-fregister-global-dtors-with-atexit), since an atexit call would
/// run the forwarder once per unit. Each forwarder carries
/// ManagedForwarderAttr, whose value <s> names it in diagnostics. Sema
/// refuses an asm label that starts with '__msil_mc_ctor.' or
/// '__msil_mc_dtor.' (T-M22), so no declaration that the source writes is
/// named like a constructor or destructor forwarder. A variable or a
/// definition that already has a forwarder's name is T-M25, which for those
/// two forwarders stays CodeGen's guard; the dllexport forwarder's name <s>
/// is the managed call's own symbol, which an asm label may give a definition
/// or a variable. \p Operands receives, by IR name, the
/// attributes operand of each external declaration with any of the three
/// (emitMSILManagedCallsMetadata).
void emitMSILManagedCallForwarders(
    CodeGenModule &CGM,
    const llvm::MapVector<GlobalDecl, llvm::StringRef> &MangledDeclNames,
    llvm::StringMap<std::string> &Operands);

/// Release's last step: appends one operand per managed-call declaration of
/// \p M to the named metadata !msil.managed.calls (plan 5.20.4 MR4): !{IR
/// name, type, member, signature, assembly, conversions, catch list[,
/// attributes]}, with "" for an absent assembly or catch list. The attributes
/// operand, from \p Forwarded (emitMSILManagedCallForwarders), is written only
/// for an external declaration with dllexport, constructor or destructor. The
/// IR linker concatenates the lists of the modules that it links. For each
/// external managed call, msil-ld compares, before LTO, the entries of the
/// bitcode inputs that still declare it (ASSEMBLY only under --lto-emit-asm,
/// as the scope that the text names), and it removes the list from the
/// modules that its LTO compiles. In a module that msil-ld did not link, such
/// as llvm-link output given to llc, the backend (slot P0) requires the
/// entries of one IR name to agree.
void emitMSILManagedCallsMetadata(
    llvm::Module &M, const llvm::StringMap<std::string> &Forwarded);

} // namespace CodeGen
} // namespace clang

#endif // LLVM_CLANG_LIB_CODEGEN_CGMSILMANAGEDCALLS_H
