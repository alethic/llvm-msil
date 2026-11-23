//===-- MSILManagedCalls.h - Managed-call stubs -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the backend's part of the managed calls from C and C++
/// (ImplementationPlan.md 5.20.5 ML2). clang names a declaration with
/// __attribute__((msil_method(...))) '__msil_mc.<symbol>' and writes the
/// member it names and the conversions Sema chose as "msil-managed-*"
/// function attributes (5.20.4). Here:
/// - MSIL::createManagedCallStubs turns each such declaration, in place, into
///   an internal stub whose IR body is 'unreachable' (delivery kind K5), after
///   it has checked that every translation unit agrees on its arguments
///   (T-M17) and refused every use of it but a direct call (T-M10);
/// - the AsmPrinter writes the stub's IL body instead of its machine
///   instructions (emitManagedCallStubBody): the argument conversions, the
///   one member access, the result conversion and, with msil_catch, a filter
///   clause that maps the listed exceptions to their codes (5.20.8); in
///   whole-program text, which ilasm assembles without msil-ld's checks of
///   an object, the body also checks those at run time: TYPE's kind (T-M16)
///   and the enum widths (T-M7) (5.20.6 MW2 as amended).
/// .NET references (5.21.9) pass through the stubs as 'object' values with
/// the 'ref' and 'this-ref' conversions, and a C cast between two classes
/// calls a cast stub, a declaration of the same kind whose body is one
/// 'castclass' (ManagedCall::IsCast).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILMANAGEDCALLS_H
#define LLVM_LIB_TARGET_MSIL_MSILMANAGEDCALLS_H

#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CLI/ManagedSignature.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/CLI/Signature.h"
#include "llvm/Support/Error.h"
#include <optional>
#include <string>

namespace llvm {
class Function;
class GlobalValue;
class MCContext;
class MCInst;
class MCInstrInfo;
class MCStreamer;
class Module;
class MSILTargetStreamer;
class TargetMachine;

namespace MSIL {

/// Turns every managed-call declaration of \p M (a declaration with
/// "msil-managed-type") into its stub, in place: an internal 'noinline'
/// definition whose IR body is 'unreachable' and which carries
/// "msil-managed-stub" besides its "msil-managed-*" attributes, so no call
/// is redirected (plan 5.20.5 ML2 (1), slot P0). First it reports T-M17 for
/// every IR name whose !msil.managed.calls entries differ, and for a
/// declaration that a direct call calls with another function type, or with
/// its own function type but a signext or zeroext of its own that would
/// change the stub (cli::hasManagedCallOfOtherExtension): the calls of units
/// that give it different C prototypes, merged by the IR linker, or one
/// unit's call through a cast pointer (T-M17 as amended). Every other use of
/// a declaration but a direct call with its own function type is T-M10,
/// unless the declaration got T-M17; each such use becomes poison (a
/// constant use null). A declaration whose attributes do not describe a
/// valid managed call is an error and its calls are erased. A declaration
/// without uses becomes a stub too. Returns true if \p M changed.
bool createManagedCallStubs(Module &M);

/// Whether \p F is a stub that createManagedCallStubs made: the AsmPrinter
/// writes its body.
bool isManagedCallStub(const Function &F);

/// The name by which diagnostics that print IL names (MSIL::getILName) name
/// \p GV. A forwarder of an exported or startup managed call, a function
/// with "msil-managed-forwarder" (plan 5.20.4 as amended: the dllexport
/// definition '<s>' or the '__msil_mc_ctor.<s>' or '__msil_mc_dtor.<s>' that
/// llvm.global_ctors or llvm.global_dtors names), is named by the attribute's
/// value <s>, the C name of the managed call (cli::getManagedCallSymbol); any
/// other value by MSIL::getILName.
StringRef getDiagnosticILName(const GlobalValue &GV, const TargetMachine &TM);

/// The IR spelling by which diagnostics name \p GV (MSIL::getIRName), with
/// a managed-call forwarder named as getDiagnosticILName says: "@<s>".
std::string getDiagnosticIRName(const GlobalValue &GV);

/// A managed call as the attributes of its stub describe it (plan 5.20.4
/// MR1), parsed and checked against the stub's IR type.
struct ManagedCall {
  /// The function's name without the '__msil_mc.' prefix, for diagnostics.
  std::string Name;
  /// TYPE: the class, value type or array type that declares the member.
  cli::ManagedType Type;
  /// MEMBER: a method, property accessor or field name, or ".ctor".
  std::string Member;
  /// SIGNATURE: the member's own signature.
  cli::ManagedSignature Sig;
  /// The scope of whole-program text: ASSEMBLY, else mscorlib (MA4).
  std::string Scope;
  /// The conversion of each C parameter, in order, and of the result.
  cli::ManagedConvList Conv;
  /// The msil_catch list in source order, and the type of each entry.
  SmallVector<cli::ManagedCatch, 4> Catches;
  SmallVector<cli::ManagedType, 4> CatchTypes;
  /// The bit width of each IR parameter that is an integer, else 0.
  SmallVector<unsigned, 8> ParamBits;
  /// How the caller widens an IR parameter to its stack slot: as its
  /// signext or zeroext attribute says, or not at all (None), which leaves
  /// the bits above an i1, i8 or i16 undefined in its int32 slot. The stub
  /// narrows such a value before the 'bool' row, and an i1 without zeroext
  /// before the 'val' row of an 8-bit type and the 'enum' row (plan 5.20.3
  /// MS5 as amended).
  enum class ParamExtension : uint8_t { None, Sign, Zero };
  /// The extension of each IR parameter, by C parameter.
  SmallVector<ParamExtension, 8> ParamExt;
  /// The bit width of the IR result if it is an integer, else 0.
  unsigned ResultBits = 0;
  /// The conversion that extends a result narrower than 32 bits as the IR
  /// result's signext or zeroext says (conv.i1, conv.u1, conv.i2, conv.u2).
  std::optional<cli::Opcode> ResultExtend;
  /// The IR result is i1 (a C _Bool or C++ bool) with conversion 'val' or
  /// 'enum': the member's value becomes 0 or 1 (C11 6.3.1.2) through
  /// 'ldc.i4.0; cgt.un', as for 'bool' (plan 5.20.3 MS5 as amended).
  bool NormalizeBoolResult = false;

  /// The access form of a 'this-val' or 'this-gch' instance method (plan
  /// 5.20.3 MS5, 5.20.5 ML2 (2) as amended), which the member's declaring
  /// type decides; "msil-managed-decl" (ML1) names it.
  /// Whole-program text (clang -S, llc text output) never keeps
  /// Placeholder: resolveStandaloneTextAccess chooses Declared or Inherited,
  /// since no msil-ld rewrites it (5.20.5 ML2 (2) and 5.20.6 MW2 as
  /// amended).
  enum class ValueThisAccess : uint8_t {
    /// No "msil-managed-decl" in object mode (clang -c, llc -filetype=obj,
    /// object-form text): the placeholder 'constrained. V' then 'callvirt
    /// V::M' (6 + 5 bytes), which msil-ld rewrites in place once it has
    /// bound the member.
    Placeholder,
    /// V declares the member: 'call V::M'.
    Declared,
    /// V inherits the member from InheritedFrom: 'constrained. V' then
    /// 'callvirt D::M'.
    Inherited,
  };
  ValueThisAccess ThisAccess = ValueThisAccess::Placeholder;
  /// A static method whose "msil-managed-decl" names the base type
  /// InheritedFrom, from which TYPE (bound to a value type or an enum,
  /// whether it is written 'valuetype V' or V) inherits the member: 'call
  /// D::M' instead of 'call V::M' (plan 5.20.3 MS5 as amended for static
  /// methods). Without the attribute, or with TYPE's own full name in it, a
  /// static method is called as 'call V::M'.
  bool StaticInherited = false;
  /// With ValueThisAccess::Inherited or StaticInherited, the class D that
  /// declares the member: System.ValueType, System.Object or System.Enum.
  cli::ManagedType InheritedFrom;

  /// A cast stub (plan 5.21.9 RM8, cli::isManagedCastShape): MEMBER
  /// '.castclass', SIGNATURE 'object(object)' and the conversions 'ref;ref',
  /// for the class, System.String or array type TYPE. Its body is 'ldarg.0;
  /// castclass <TYPE>; ret' (the SZARRAY token for an array). It makes no
  /// member access, so callers test it (isCast()) before the predicates below
  /// that describe the access, which answer for a cast stub:
  /// - isConstructor(): false;
  /// - isStaticMethod(): true for every cast stub, since 'object(object)' has
  ///   no 'this' and is not a field;
  /// - isArrayConstructor(): true for a cast to an array type, which
  ///   parseManagedCall's assertion that TYPE is named or an array type
  ///   relies on (an array type is not named).
  bool IsCast = false;

  /// A cast stub (IsCast).
  bool isCast() const { return IsCast; }
  /// A constructor: newobj, or newarr for an array type.
  bool isConstructor() const { return Member == ".ctor"; }
  /// TYPE is an array type: its constructor (newarr) or, with IsCast, the
  /// type that a cast stub casts to.
  bool isArrayConstructor() const {
    return Type.Kind == cli::ManagedType::TypeKind::SZArray;
  }
  /// A static method (no 'instance', not a field): its access is 'call'. It
  /// holds for a cast stub too, which makes no access (IsCast).
  bool isStaticMethod() const { return !Sig.HasThis && !Sig.IsField; }
  /// The C parameters start with the 'this' handle or, for 'this-ref' (plan
  /// 5.21.9 RM1), the 'this' reference.
  bool hasThisHandle() const { return Sig.HasThis && !isConstructor(); }
  /// An instance method whose 'this' is a managed pointer to a value type
  /// ('this-val' or 'this-gch'): its access depends on ThisAccess.
  bool hasValueThisMethod() const {
    return hasThisHandle() && !Sig.IsField && !Conv.Params.empty() &&
           (Conv.Params.front() == cli::ManagedConv::ThisVal ||
            Conv.Params.front() == cli::ManagedConv::ThisGCH);
  }
  /// The C parameters end with the error code pointer of msil_catch.
  bool hasErrorParam() const { return !Catches.empty(); }
  /// The type of the value that the access leaves on the stack: the
  /// constructed object or array, else the member's result or field type
  /// (void for none).
  const cli::ManagedType &getAccessResult() const {
    return isConstructor() ? Type : Sig.Return;
  }
};

/// Parses the "msil-managed-*" attributes of \p F and checks them against
/// its IR type: an Error whose message names the problem.
Expected<ManagedCall> parseManagedCall(const Function &F);

/// For a stub written as whole-program text (not object mode, and no
/// "msil-managed-decl", so its access is the Placeholder that only object
/// mode keeps): the final form that the text carries instead (plan 5.20.5 ML2
/// (2), 5.20.6 MW2 as amended). A method that matches one of System.Object's
/// public instance methods by name and signature (bool Equals(object), int32
/// GetHashCode(), string ToString(), class System.Type GetType()) is Inherited
/// from System.Object ('constrained. V' then 'callvirt
/// [mscorlib]System.Object::M', valid whether or not V overrides it); any other
/// is Declared ('call V::M'). Any other access is left as it is.
void resolveStandaloneTextAccess(ManagedCall &Call);

/// The locals that the body of the stub of \p Call with the IL signature
/// \p ILSig declares, appended to \p Locals after the locals the method has
/// already: an int32 length per UTF-8 or UTF-32 parameter, and with
/// msil_catch the int32 code and the result (plan 5.20.5 ML2 (2)). Returns
/// the index of the first one. They are all of primitive types, as
/// MSILTargetStreamer::emitLocals needs ('object' for a reference result,
/// plan 5.21.9 RM7); the run-time checks of whole-program text need no local
/// (emitManagedCallStubBody).
unsigned appendManagedCallStubLocals(const ManagedCall &Call,
                                     const Signature &ILSig,
                                     SmallVectorImpl<cli::TypeSig> &Locals);

/// Writes the IL body of the stub of \p Call (plan 5.20.5 ML2 (2), 5.20.8,
/// 5.20.9, 5.20.10), after emitLocals: with msil_catch its clause event
/// through \p TS first, then every instruction through \p EmitInst and
/// every label through \p OS. \p ILSig is the stub's IL signature,
/// \p FirstLocal the index that appendManagedCallStubLocals returned, and
/// \p FunctionNumber names the labels '@MC<f>_<k>'. The branch forms are
/// chosen as for lowered code (MSIL::relaxBranches with \p MCII), so a long
/// argument list or msil_catch list still fits. The tokens point into
/// \p Call, which must outlive them.
///
/// With \p TextChecks (whole-program text, which ilasm assembles without
/// msil-ld's checks of an object; plan 5.20.6 MW2 as amended) the body
/// checks at run time what msil-ld checks of an object (MK2, MK3):
/// - TYPE's kind (T-M16): the castclass of a 'this-obj' handle and of a
///   'this-ref' reference, the unbox of a 'this-val' handle, a cast stub's
///   castclass of a class (plan 5.21.9 RM7, RM8) and, after a constructor's
///   newobj, a class's castclass and a value type's box (that of a 'boxed'
///   result, else of a copy that 'dup' makes and 'pop' drops) name TYPE as a
///   TypeSpec of the declared kind ('class T', 'valuetype V'), which the
///   runtime checks against the type's definition when it compiles the stub,
///   unless that kind is fixed and right (System.String and System.Object as
///   classes, the one-byte value types of MSIL::isOneByteValueType as value
///   types);
/// - before anything else, for each 'enum' parameter, then an 'enum' result
///   (not a constructor's), that its 'valuetype E' is an enum of the C
///   integer's width: typeof(E).IsEnum ('ldtoken E', Type::GetTypeFromHandle,
///   Type::get_IsEnum) must be true and 'sizeof E' must be that width in
///   bytes, else it throws System.InvalidProgramException with msil-ld's
///   text (T-M7). No check needs a local. An E that is never an enum (a
///   one-byte value type, System.String, System.Object) gets only the throw.
void emitManagedCallStubBody(const ManagedCall &Call, const Signature &ILSig,
                             unsigned FirstLocal, unsigned FunctionNumber,
                             bool TextChecks, MSILTargetStreamer &TS,
                             MCStreamer &OS, MCContext &Ctx,
                             const MCInstrInfo &MCII,
                             function_ref<void(const MCInst &)> EmitInst);

/// The .maxstack that the body of the stub \p F needs, with the run-time
/// checks of whole-program text if \p TextChecks (emitManagedCallStubBody);
/// 0 if \p F is not a stub or its attributes do not parse (the AsmPrinter
/// then emits its machine code, the trap of its 'unreachable').
/// MSILStackCheck raises the computed value to it.
unsigned getManagedCallStubMaxStack(const Function &F, bool TextChecks);

} // end namespace MSIL
} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MSILMANAGEDCALLS_H
