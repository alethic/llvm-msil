//===- llvm/CLI/ManagedSignature.h - Managed-call declarations --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The texts that a managed-call declaration carries from clang to the MSIL
// target and msil-ld (ImplementationPlan.md 5.20.2-5.20.5): a C or C++
// declaration with __attribute__((msil_method(TYPE, MEMBER, SIGNATURE[,
// ASSEMBLY]))) and optional msil_catch(EXCEPTION_TYPE, CODE) attributes names
// one public .NET member, and every call of it calls a private stub that
// converts the arguments, accesses the member and converts the result.
//
// This header holds:
//   - the parser of TYPE, MEMBER and SIGNATURE (canonical listing v1 text,
//     plan 5.7.10) into ManagedType and ManagedSignature, and the shape check
//     that ties the three together;
//   - the conversion codes that Sema chooses per parameter and for the result
//     (plan 5.20.3 MS5), and the text of their IR attribute;
//   - the text of the ordered msil_catch list;
//   - the names of the IR function attributes, of the named metadata (and
//     the text of its attributes operand), the prefix and the
//     internal-linkage postfix of the IR name of a managed-call declaration,
//     and the names of the forwarders of exported and startup managed calls
//     (plan 5.20.4);
//   - for the .NET references of C (plan 5.21), 'T ^' of a struct with
//     __attribute__((cli_type(NAME))): the parser of NAME with its key and
//     display name, the IR type's name, the comparison helper's name, and the
//     shape of the cast stubs that a C cast between two classes calls.
//
// Header only, like BCLMembers.h: clang's Sema and CodeGen, the MSIL target
// and msil-ld share it, and clang links no new LLVM library for it.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_MANAGEDSIGNATURE_H
#define LLVM_CLI_MANAGEDSIGNATURE_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace llvm {
namespace cli {

//===----------------------------------------------------------------------===//
// Names shared by clang, the MSIL target and msil-ld (plan 5.20.4)
//===----------------------------------------------------------------------===//

/// The prefix of the IR name of a managed-call declaration (MR2):
/// '__msil_mc.<symbol>', where <symbol> is the C name, the asm label or the
/// C++ mangled name, or '__msil_mc.<symbol>.__uniq.<n>' for a declaration
/// with internal linkage (getManagedCallUniqPostfix). An internal
/// declaration's '.__uniq.<n>' hashes the strings of its !msil.managed.calls
/// operands 1-6 plus its function type (its C types, and the IR type and
/// extension that the C ABI gives its result and each parameter), not the main
/// file's name, so the internal declarations of two units share an IR name only
/// when their stubs are the same, and the IR linker may merge them (plan 5.20.4
/// as amended). It is a toolchain name (plan 5.14.2 N3, isToolchainName), so
/// nothing binds it late. In an object, the stub of that name is an internal
/// MethodDef and only a LOCAL FUNCTION symbol of the object's SYMTAB (plan
/// 5.8.5 C3; MW1 as amended): never a GLOBAL or WEAK symbol, never exported
/// and never imported. On the Windows MSIL triples the C name of a dllexport
/// managed call is also defined, by a weak_odr dllexport COMDAT forwarder
/// that calls the stub (ManagedForwarderAttr), so the stub stays LOCAL while
/// the C name is exported.
inline constexpr StringLiteral ManagedCallPrefix = "__msil_mc.";

/// The IR function attributes of a managed-call declaration (MR1), written by
/// clang's CodeGen. TYPE as written (with "valuetype " or "[]"), MEMBER and
/// SIGNATURE as written, ASSEMBLY only when given, the conversion codes
/// (printManagedConvList) and the msil_catch list (printManagedCatchList, only
/// with msil_catch).
inline constexpr StringLiteral ManagedTypeAttr = "msil-managed-type";
inline constexpr StringLiteral ManagedMemberAttr = "msil-managed-member";
inline constexpr StringLiteral ManagedSigAttr = "msil-managed-sig";
inline constexpr StringLiteral ManagedAsmAttr = "msil-managed-asm";
inline constexpr StringLiteral ManagedConvAttr = "msil-managed-conv";
inline constexpr StringLiteral ManagedCatchAttr = "msil-managed-catch";

/// The full name of the declaring type of the bound member M of a managed
/// call to a static method or to a 'this-val' or 'this-gch' instance method
/// (ML1, plan 5.20.3 MS5 and 5.20.5 ML2 (2) as amended): V's own full name
/// when V, the type that TYPE names, declares M, or the base type D that
/// declares M (System.ValueType, System.Object or System.Enum) when V only
/// inherits it. msil-ld sets it in LTO code generation (MSILLinkHooksPass)
/// on each such call that it binds, but on a static method only when V
/// (whether or not TYPE says 'valuetype') is a value type or an enum that
/// only inherits M. The backend reads it to write the final access form:
/// 'call V::M' when it names V; when it names D, 'call D::M' for a static
/// method and 'constrained. V' then 'callvirt D::M' for an instance method.
/// When it is absent, the backend writes 'call V::M' for a static method;
/// for an instance method, an object and object-form text (clang -c, llc in
/// object mode) get the placeholder 'constrained. V' + 'callvirt V::M', and
/// whole-program text (clang -S, llc) gets a final form that the backend
/// chooses without binding (plan 5.20.6 MW2 as amended). After binding,
/// msil-ld rewrites each site of an object whose form the attribute would
/// change; nothing rewrites whole-program text.
inline constexpr StringLiteral ManagedDeclAttr = "msil-managed-decl";

/// The attribute that MSIL::createManagedCallStubs puts on the internal stub
/// it makes of a managed-call declaration (plan 5.20.5 ML2).
inline constexpr StringLiteral ManagedStubAttr = "msil-managed-stub";

/// The function attribute on each forwarder that clang's CodeGen defines for
/// a managed call with dllexport, constructor or destructor: a function that
/// calls the managed-call declaration and nothing else. Its value is the
/// managed call's symbol (the C name, the asm label or the C++ mangled name),
/// which diagnostics about the forwarder name.
inline constexpr StringLiteral ManagedForwarderAttr = "msil-managed-forwarder";

/// The prefixes of the IR names of the forwarders that clang registers in
/// llvm.global_ctors and llvm.global_dtors for a managed call with constructor
/// or destructor, followed by the managed call's symbol
/// (isManagedForwarderName). A dllexport forwarder is named the symbol
/// itself. Names with these prefixes, like those with ManagedCallPrefix, are
/// reserved for the toolchain (plan 5.14.2 N3, isToolchainName): clang
/// refuses an asm label that starts with either prefix (T-M22), and msil-ld
/// refuses any non-local definition of such a name other than the forwarder
/// that clang makes (T-M23).
inline constexpr StringLiteral ManagedCtorForwarderPrefix = "__msil_mc_ctor.";
inline constexpr StringLiteral ManagedDtorForwarderPrefix = "__msil_mc_dtor.";

/// The named metadata with one operand per managed-call declaration (MR4):
/// !{!"<IR name>", !"<type>", !"<member>", !"<sig>", !"<asm>", !"<conv>",
/// !"<catch>"[, !"<attributes>"]}, where an absent ASSEMBLY or catch list is
/// "". The 8th operand is written only for an external declaration that has
/// dllexport, constructor or destructor: printManagedCallAttributes's text,
/// one string that joins with ',' (no spaces), in this order, whichever of
/// 'dllexport', 'constructor(<prio>)' and 'destructor(<prio>)' are present,
/// <prio> being the effective priority (65535 when none is given), for
/// example "dllexport,constructor(65535)". In all other cases the operand is
/// absent, and an internal declaration never has it. Bitcode inputs whose
/// compiled code still declares one IR name fail T-M17 when their entries for
/// it differ in any operand, the 8th included, except ASSEMBLY, which counts
/// only under --lto-emit-asm, as the scope that the text names: msil-ld
/// compares the entries before LTO (haveEqualManagedCallEntries in
/// ManagedCallIR.h) and removes the list from the modules that its LTO
/// compiles. Slot P0 thus compares all of a module's entries, every operand as
/// written, but only in a module that msil-ld did not link (llc); its T-M17
/// for a direct call of another function type, or with a 'signext' or
/// 'zeroext' of its own that would give the declaration another stub
/// (ManagedCallIR.h), reads no entry.
inline constexpr StringLiteral ManagedCallsMetadata = "msil.managed.calls";

/// The scope that text output gives a member without ASSEMBLY (MA4), as for
/// BCL members in text (plan 5.11.8).
inline constexpr StringLiteral ManagedDefaultAssembly = "mscorlib";

/// The value type whose handle is the C value itself (MS5's 'gch' rows).
inline constexpr StringLiteral GCHandleTypeName =
    "System.Runtime.InteropServices.GCHandle";

/// The name of the IR type of a .NET reference (plan 5.21.6 RI1),
/// target("msil.ref"), which has no parameters: a value that IL holds as an
/// 'object' in a local, an argument, a result or on the evaluation stack,
/// never in memory. Whatever the C class, the IR type is this one.
inline constexpr StringLiteral ReferenceTypeName = "msil.ref";

/// The prefix of the names that the toolchain reserves for the .NET reference
/// helpers (plan 5.21.6 RI6): a toolchain name (plan 5.14.2 N3,
/// isToolchainName), never bound natively. A definition of such a name is an
/// error (the backend's T-RB7, msil-ld's T-RL1), and so is a declaration
/// other than the helper's own.
inline constexpr StringLiteral ReferenceHelperPrefix = "__msil_ref.";

/// The comparison of two .NET references (plan 5.21.6 RI6): declared by
/// clang as 'declare i1 @"__msil_ref.eq"(target("msil.ref"),
/// target("msil.ref"))' with memory(none) nounwind willreturn speculatable,
/// true when both operands are the same object or both are null (IL 'ceq'),
/// and never defined: instruction selection makes each call a 'ceq', 'beq',
/// 'bne.un', 'brtrue' or 'brfalse', so no object names it.
inline constexpr StringLiteral ReferenceEqName = "__msil_ref.eq";

/// Whether \p Name starts with ReferenceHelperPrefix.
inline bool isReferenceHelperName(StringRef Name) {
  return Name.starts_with(ReferenceHelperPrefix);
}

/// The symbol part of the IR name of a cast stub's declaration (plan 5.21.9
/// RM8): '__msil_mc.<castclass>.__uniq.<n>', a managed-call name
/// (ManagedCallPrefix) whose '.__uniq.<n>' postfix (getManagedCallUniqPostfix)
/// hashes the declaration's !msil.managed.calls operands 1-6 and its function
/// type, so that each class has its own stub and equal casts of two units
/// merge under LTO. No C identifier or mangled name contains its '<', and
/// clang refuses an asm label that starts with it (T-M22).
inline constexpr StringLiteral ManagedCastSymbol = "<castclass>";

/// MEMBER, SIGNATURE and the conversions of every cast stub (plan 5.21.9 RM8,
/// isManagedCastShape). checkManagedMemberName refuses ManagedCastMember, so
/// no msil_method written in source names it.
inline constexpr StringLiteral ManagedCastMember = ".castclass";
inline constexpr StringLiteral ManagedCastSignature = "object(object)";
inline constexpr StringLiteral ManagedCastConversions = "ref;ref";

/// Whether \p IRName is the IR name of a managed-call declaration or stub.
inline bool isManagedCallName(StringRef IRName) {
  return IRName.starts_with(ManagedCallPrefix);
}

/// Whether \p IRName starts with ManagedCtorForwarderPrefix or
/// ManagedDtorForwarderPrefix: a reserved name of the forwarder of a
/// constructor or destructor managed call. clang's Sema refuses an asm label
/// that is such a name (T-M22), and msil-ld refuses a non-local definition of
/// such a name that is not the forwarder clang makes (T-M23).
inline bool isManagedForwarderName(StringRef IRName) {
  return IRName.starts_with(ManagedCtorForwarderPrefix) ||
         IRName.starts_with(ManagedDtorForwarderPrefix);
}

/// The start of the postfix that an internal-linkage managed-call
/// declaration's IR name ends with: '.__uniq.' and then decimal digits
/// (FunctionSamples::UniqSuffix, as -funique-internal-linkage-names writes it).
/// The digits hash the strings of the declaration's !msil.managed.calls
/// operands 1-6 plus its function type (its C types, and the IR type and
/// extension that the C ABI gives its result and each parameter;
/// getManagedCallUniqPostfix), not the main file's name, so the internal
/// declarations of two units share an IR name only when their stubs are the
/// same, and the IR linker may merge them; each unit's stub stays an internal
/// MethodDef and a LOCAL symbol.
inline constexpr StringLiteral ManagedCallUniqPostfix = ".__uniq.";

/// The postfix of the IR name of an internal-linkage managed-call
/// declaration: ManagedCallUniqPostfix, then the unsigned decimal value of
/// the 128-bit MD5 of \p Fields, printed as
/// llvm::getUniqueInternalLinkagePostfix prints its hash. clang passes the
/// strings of the declaration's !msil.managed.calls operands 1-6 (TYPE,
/// MEMBER, SIGNATURE, ASSEMBLY, the conversion and catch lists) and its
/// function type. Each field is hashed as its byte length in decimal, ':'
/// and its bytes, so two different lists never hash the same text.
inline std::string getManagedCallUniqPostfix(ArrayRef<StringRef> Fields) {
  MD5 Hash;
  for (StringRef Field : Fields) {
    Hash.update(utostr(Field.size()));
    Hash.update(":");
    Hash.update(Field);
  }
  MD5::MD5Result Result;
  Hash.final(Result);
  SmallString<32> Hex;
  MD5::stringifyResult(Result, Hex);
  APInt Value(128, Hex.str(), 16);
  std::string Postfix = ManagedCallUniqPostfix.str();
  Postfix += toString(Value, /*Radix=*/10, /*Signed=*/false);
  return Postfix;
}

/// The name of a managed-call function as diagnostics print it: \p IRName
/// without the '__msil_mc.' prefix and without a trailing '.__uniq.<digits>'
/// postfix (the C name, the asm label or the C++ mangled name, for internal
/// declarations too).
inline StringRef getManagedCallSymbol(StringRef IRName) {
  IRName.consume_front(ManagedCallPrefix);
  size_t Pos = IRName.rfind(ManagedCallUniqPostfix);
  if (Pos != StringRef::npos) {
    StringRef Digits = IRName.substr(Pos + ManagedCallUniqPostfix.size());
    if (!Digits.empty() && Digits.find_if_not(isDigit) == StringRef::npos)
      IRName = IRName.take_front(Pos);
  }
  return IRName;
}

//===----------------------------------------------------------------------===//
// Types and signatures (plan 5.20.2 MA1-MA3)
//===----------------------------------------------------------------------===//

/// One type of a member's signature, or the type that a declaration names:
/// a kind, an element type (pointers and arrays) and a type name (classes and
/// value types).
struct ManagedType {
  enum class TypeKind : uint8_t {
    // The listing primitives (plan 5.7.10); Void only as a return type or as
    // the pointee of a pointer.
    Void,
    Bool,
    Char,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float32,
    Float64,
    NativeInt,
    NativeUInt,
    String,
    Object,
    /// 'class Ns.T': a reference type (class or interface) named TypeName.
    Class,
    /// 'valuetype Ns.T': a value type (an enum included) named TypeName.
    ValueType,
    /// 'T*', an unmanaged pointer to Element.
    Pointer,
    /// 'T[]', a single-dimensional zero-based array of Element.
    SZArray,
  };

  TypeKind Kind = TypeKind::Void;
  /// The full name of a Class or ValueType: "Ns.Name", a nested type as
  /// "Ns.Outer/Inner"; empty for the other kinds.
  std::string TypeName;
  /// The element of a Pointer or SZArray; null for the other kinds.
  std::shared_ptr<const ManagedType> Element;

  static ManagedType get(TypeKind K) {
    assert(K != TypeKind::Class && K != TypeKind::ValueType &&
           K != TypeKind::Pointer && K != TypeKind::SZArray &&
           "not a primitive kind");
    ManagedType T;
    T.Kind = K;
    return T;
  }
  static ManagedType getClass(StringRef Name) {
    ManagedType T;
    T.Kind = TypeKind::Class;
    T.TypeName = Name.str();
    return T;
  }
  static ManagedType getValueType(StringRef Name) {
    ManagedType T;
    T.Kind = TypeKind::ValueType;
    T.TypeName = Name.str();
    return T;
  }
  static ManagedType getPointer(ManagedType Pointee) {
    ManagedType T;
    T.Kind = TypeKind::Pointer;
    T.Element = std::make_shared<const ManagedType>(std::move(Pointee));
    return T;
  }
  static ManagedType getSZArray(ManagedType Elem) {
    ManagedType T;
    T.Kind = TypeKind::SZArray;
    T.Element = std::make_shared<const ManagedType>(std::move(Elem));
    return T;
  }

  bool isVoid() const { return Kind == TypeKind::Void; }
  /// A class or a value type: a type with a TypeName.
  bool isNamed() const {
    return Kind == TypeKind::Class || Kind == TypeKind::ValueType;
  }
  /// One of the listing primitives, Void, String and Object included.
  bool isPrimitive() const {
    return !isNamed() && Kind != TypeKind::Pointer && Kind != TypeKind::SZArray;
  }
  /// A type whose values are object references: string, object, a class or
  /// an array.
  bool isReferenceType() const {
    return Kind == TypeKind::String || Kind == TypeKind::Object ||
           Kind == TypeKind::Class || Kind == TypeKind::SZArray;
  }
  /// 'valuetype System.Runtime.InteropServices.GCHandle'.
  bool isGCHandle() const {
    return Kind == TypeKind::ValueType && TypeName == GCHandleTypeName;
  }
  /// The element of a Pointer or SZArray.
  const ManagedType &getElement() const {
    assert(Element && "not a pointer or array");
    return *Element;
  }

  friend bool operator==(const ManagedType &A, const ManagedType &B) {
    if (A.Kind != B.Kind || A.TypeName != B.TypeName)
      return false;
    if (!A.Element || !B.Element)
      return !A.Element && !B.Element;
    return *A.Element == *B.Element;
  }
  friend bool operator!=(const ManagedType &A, const ManagedType &B) {
    return !(A == B);
  }

  /// Prints the type as canonical listing v1 text ("int32", "uint8*",
  /// "class System.IO.Stream", "string[]"), with the resolution scope
  /// \p Scope before the name of every class and value type when it is not
  /// empty, as ILAsm text names them ("valuetype [mscorlib]System.IO.FileMode",
  /// "class ['my lib']Widget"). \p Scope is the assembly name as written,
  /// never already quoted: printILAsmScope quotes it when it is not an ILAsm
  /// dotted name (plan 5.20.6 MW2 as amended). Each '/'-separated part of
  /// TypeName that is not an ILAsm dotted name (isILAsmBareDottedName) is
  /// single-quoted the same way ("valuetype 'value'",
  /// "class Ns.Outer/'value'"), so that ilasm reads it as one name.
  void print(raw_ostream &OS, StringRef Scope = "") const;
  /// The listing text of the type without a scope.
  std::string str() const {
    std::string S;
    raw_string_ostream OS(S);
    print(OS);
    return S;
  }
};

/// The listing spelling of a primitive kind ("native int" for NativeInt);
/// empty for the other kinds.
inline StringRef getManagedPrimitiveName(ManagedType::TypeKind K) {
  using TK = ManagedType::TypeKind;
  switch (K) {
  case TK::Void:
    return "void";
  case TK::Bool:
    return "bool";
  case TK::Char:
    return "char";
  case TK::Int8:
    return "int8";
  case TK::UInt8:
    return "uint8";
  case TK::Int16:
    return "int16";
  case TK::UInt16:
    return "uint16";
  case TK::Int32:
    return "int32";
  case TK::UInt32:
    return "uint32";
  case TK::Int64:
    return "int64";
  case TK::UInt64:
    return "uint64";
  case TK::Float32:
    return "float32";
  case TK::Float64:
    return "float64";
  case TK::NativeInt:
    return "native int";
  case TK::NativeUInt:
    return "native uint";
  case TK::String:
    return "string";
  case TK::Object:
    return "object";
  case TK::Class:
  case TK::ValueType:
  case TK::Pointer:
  case TK::SZArray:
    break;
  }
  return "";
}

//===----------------------------------------------------------------------===//
// Resolution scopes in ILAsm text (plan 5.20.6 MW2 as amended)
//===----------------------------------------------------------------------===//

/// Whether \p Name is a word that ilasm's lexer reads as a keyword or an
/// instruction mnemonic rather than as an identifier, so that it cannot stand
/// unquoted where ILAsm expects a name ("[int32]", "[value]", "[ldc.i4]" and
/// "[cil]" are syntax errors). The lexer looks the whole dotted token up, so
/// "System.int32" is a name. The keywords are the words of the .NET Framework
/// ilasm's tables that it rejects as an assembly name (its keywords, type
/// names and instruction aliases such as 'brnull' and 'ldc.i4.M1'), plus
/// 'aggressiveoptimization' and 'async' of the .NET ilasm; the mnemonics are
/// CILOpcodes.def's.
inline bool isILAsmReservedWord(StringRef Name) {
  // clang-format off
  static constexpr StringLiteral Keywords[] = {
    "abstract", "aggressiveinlining", "aggressiveoptimization", "algorithm",
    "alignment", "amd64", "ansi", "any", "arm", "arm64", "array", "as",
    "assembly", "assert", "async", "at", "auto", "autochar", "beforefieldinit",
    "bestfit", "blob", "blob_object", "bool", "brinst", "brinst.s", "brnull",
    "brnull.s", "brzero", "brzero.s", "bstr", "bytearray", "byvalstr",
    "callconv", "callmostderived", "carray", "catch", "cdecl", "cf", "char",
    "charmaperror", "cil", "class", "clsid", "codelabel", "currency", "custom",
    "date", "decimal", "default", "demand", "deny", "endfault", "endmac",
    "enum", "error", "explicit", "extends", "extern", "false", "famandassem",
    "family", "famorassem", "fastcall", "fault", "field", "filetime", "filter",
    "final", "finally", "fixed", "flags", "float", "float32", "float64",
    "forwarder", "forwardref", "fromunmanaged", "handler", "hidebysig",
    "hresult", "ia64", "idispatch", "iidparam", "il", "illegal", "implements",
    "import", "in", "inheritcheck", "init", "initonly", "instance", "int",
    "int16", "int32", "int64", "int8", "interface", "internalcall", "iunknown",
    "lasterr", "ldc.i4.M1", "ldelem.any", "ldelem.u8", "ldind.u8", "legacy",
    "library", "linkcheck", "literal", "lpstr", "lpstruct", "lptstr", "lpvoid",
    "lpwstr", "managed", "marshal", "mdtoken", "method", "modopt", "modreq",
    "native", "nested", "newslot", "noinlining", "nomangle", "nometadata",
    "noncasdemand", "noncasinheritance", "noncaslinkdemand", "nooptimization",
    "noplatform", "notserialized", "null", "nullref", "object", "objectref",
    "off", "on", "opt", "optil", "out", "permitonly", "pinned", "pinvokeimpl",
    "prefix1", "prefix2", "prefix3", "prefix4", "prefix5", "prefix6", "prefix7",
    "prefixref", "prejitdeny", "prejitgrant", "preservesig", "private",
    "privatescope", "property", "public", "record", "refany", "reqmin",
    "reqopt", "reqrefuse", "reqsecobj", "request", "retainappdomain",
    "retargetable", "rtspecialname", "runtime", "safearray", "sealed",
    "securitymitigations", "sequential", "serializable", "specialname",
    "static", "stdcall", "stelem.any", "storage", "stored_object", "stream",
    "streamed_object", "strict", "string", "struct", "synchronized", "syschar",
    "sysstring", "tbstr", "thiscall", "tls", "to", "true", "type", "typedref",
    "uint", "uint16", "uint32", "uint64", "uint8", "unicode", "unmanaged",
    "unmanagedexp", "unsigned", "unused", "userdefined", "value", "valuetype",
    "vararg", "variant", "vector", "virtual", "void", "wchar", "winapi",
    "windowsruntime", "with", "x86",
  };
  // clang-format on
  static constexpr StringLiteral Mnemonics[] = {
#define CIL_OPCODE(ENUM, MNEMONIC, ...) MNEMONIC,
#include "llvm/BinaryFormat/CILOpcodes.def"
  };
  for (StringLiteral K : Keywords)
    if (Name == K)
      return true;
  for (StringLiteral M : Mnemonics)
    if (Name == M)
      return true;
  return false;
}

/// Whether \p Name can stand unquoted where ILAsm text expects a dotted name,
/// as in the resolution scope "[System.Runtime]": one or more identifiers
/// joined by '.', each a letter, '_', '$' or '@' and then letters, digits,
/// '_', '$', '@', '?' or '`', and not a reserved word (isILAsmReservedWord).
/// A name with a space, ']', a quote, a '-', an empty or digit-led part or a
/// non-ASCII byte is not.
inline bool isILAsmBareDottedName(StringRef Name) {
  SmallVector<StringRef, 4> Parts;
  Name.split(Parts, '.', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
  for (StringRef Part : Parts) {
    if (Part.empty())
      return false;
    char First = Part.front();
    if (!isAlpha(First) && First != '_' && First != '$' && First != '@')
      return false;
    for (char C : Part.drop_front())
      if (!isAlnum(C) && C != '_' && C != '$' && C != '@' && C != '?' &&
          C != '`')
        return false;
  }
  return !isILAsmReservedWord(Name);
}

/// Prints \p Name single-quoted as MSIL::printQuotedName does: a quote
/// becomes \', a backslash \134 (ilasm drops the escape \\), bytes below 0x20
/// and 0x7f become three-digit octal escapes, and other bytes are printed
/// raw.
inline void printILAsmQuotedName(raw_ostream &OS, StringRef Name) {
  auto PrintOctal = [&OS](unsigned char C) {
    OS << '\\' << char('0' + (C >> 6)) << char('0' + ((C >> 3) & 7))
       << char('0' + (C & 7));
  };
  OS << '\'';
  for (unsigned char C : Name.bytes()) {
    if (C == '\'')
      OS << "\\'";
    else if (C == '\\' || C < 0x20 || C == 0x7f)
      PrintOctal(C);
    else
      OS << C;
  }
  OS << '\'';
}

/// Prints the resolution scope of the assembly \p Scope (its name as written,
/// not empty) in ILAsm text: "[mscorlib]", "[System.Runtime]" for an ILAsm
/// dotted name (isILAsmBareDottedName), else single-quoted ("['my lib']",
/// "['a]b']", "['int32']"). Every ILAsm text writer of a managed-call scope
/// prints it with this function, so it is quoted exactly once.
inline void printILAsmScope(raw_ostream &OS, StringRef Scope) {
  assert(!Scope.empty() && "an empty scope has no ILAsm text");
  OS << '[';
  if (isILAsmBareDottedName(Scope))
    OS << Scope;
  else
    printILAsmQuotedName(OS, Scope);
  OS << ']';
}

inline void ManagedType::print(raw_ostream &OS, StringRef Scope) const {
  switch (Kind) {
  case TypeKind::Class:
  case TypeKind::ValueType: {
    OS << (Kind == TypeKind::Class ? "class " : "valuetype ");
    if (!Scope.empty())
      printILAsmScope(OS, Scope);
    // Each '/'-separated part (the enclosing type, then each nested type) is
    // printed as the scope is: bare when ilasm reads it as a dotted name,
    // else single-quoted ("valuetype 'value'", "class Ns.Outer/'value'"; a
    // part with a non-ASCII byte is quoted whole).
    SmallVector<StringRef, 2> Parts;
    StringRef(TypeName).split(Parts, '/');
    ListSeparator Slash("/");
    for (StringRef Part : Parts) {
      OS << Slash;
      if (isILAsmBareDottedName(Part))
        OS << Part;
      else
        printILAsmQuotedName(OS, Part);
    }
    return;
  }
  case TypeKind::Pointer:
    getElement().print(OS, Scope);
    OS << '*';
    return;
  case TypeKind::SZArray:
    getElement().print(OS, Scope);
    OS << "[]";
    return;
  default:
    OS << getManagedPrimitiveName(Kind);
    return;
  }
}

/// A member's own signature (MA3): '[instance ]RET(P1, P2, ...)' for a
/// method or '[instance ]field T' for a field, whose type is Return.
struct ManagedSignature {
  /// 'instance': an instance method or field.
  bool HasThis = false;
  /// 'field T': a field read (ldsfld or ldfld); Params is empty.
  bool IsField = false;
  /// A method's return type, or a field's type.
  ManagedType Return;
  /// A method's parameter types, in order.
  SmallVector<ManagedType, 4> Params;

  friend bool operator==(const ManagedSignature &A, const ManagedSignature &B) {
    return A.HasThis == B.HasThis && A.IsField == B.IsField &&
           A.Return == B.Return && ArrayRef(A.Params) == ArrayRef(B.Params);
  }
  friend bool operator!=(const ManagedSignature &A, const ManagedSignature &B) {
    return !(A == B);
  }

  /// Prints the parameter list "(P1, P2)" (nothing for a field), with
  /// \p Scope as in ManagedType::print.
  void printParams(raw_ostream &OS, StringRef Scope = "") const {
    if (IsField)
      return;
    OS << '(';
    for (size_t I = 0, E = Params.size(); I != E; ++I) {
      if (I)
        OS << ", ";
      Params[I].print(OS, Scope);
    }
    OS << ')';
  }
  /// Prints the canonical text of the signature: "instance void(int32)",
  /// "field int64", with \p Scope as in ManagedType::print.
  void print(raw_ostream &OS, StringRef Scope = "") const {
    if (HasThis)
      OS << "instance ";
    if (IsField)
      OS << "field ";
    Return.print(OS, Scope);
    printParams(OS, Scope);
  }
  std::string str() const {
    std::string S;
    raw_string_ostream OS(S);
    print(OS);
    return S;
  }
};

/// Splits the full name of a class or value type into its namespace and its
/// name, BCLMembers.def's columns: "System.IO.FileStream" gives "System.IO"
/// and "FileStream", "Ns.Outer/Inner" gives "Ns" and "Outer/Inner", and a
/// name without a namespace gives "" and the name.
inline std::pair<StringRef, StringRef> splitManagedTypeName(StringRef Full) {
  StringRef Outer = Full.take_until([](char C) { return C == '/'; });
  size_t Dot = Outer.rfind('.');
  if (Dot == StringRef::npos)
    return {StringRef(), Full};
  return {Full.take_front(Dot), Full.drop_front(Dot + 1)};
}

namespace detail {

inline Error managedSigError(const Twine &Reason) {
  return createStringError(inconvertibleErrorCode(), Reason);
}

/// A character of a type or member name. '`' is included so that a generic
/// type's arity suffix is reported as such, not as an unknown character.
inline bool isManagedNameChar(char C) {
  return isAlnum(C) || C == '_' || C == '$' || C == '.' || C == '/' ||
         C == '`' || static_cast<unsigned char>(C) >= 0x80;
}

/// Whether \p Full is a full type name: segments separated by '.'
/// (namespaces) and '/' (nesting), each a non-empty identifier (letters,
/// digits, '_', '$' and bytes from 0x80) that does not start with a digit.
/// With \p AllowArity, a segment may also hold '`' after its first character,
/// as a generic type's name does ("List`1").
inline bool isManagedTypeNameText(StringRef Full, bool AllowArity) {
  bool AtSegmentStart = true;
  for (char C : Full) {
    if (C == '.' || C == '/') {
      if (AtSegmentStart)
        return false;
      AtSegmentStart = true;
      continue;
    }
    bool Valid = isAlnum(C) || C == '_' || C == '$' ||
                 static_cast<unsigned char>(C) >= 0x80 ||
                 (AllowArity && C == '`' && !AtSegmentStart);
    if (!Valid || (AtSegmentStart && isDigit(C)))
      return false;
    AtSegmentStart = false;
  }
  return !AtSegmentStart;
}

/// Checks the full name of a class or value type: segments separated by '.'
/// (namespaces) and '/' (nesting), each a non-empty identifier that does not
/// start with a digit. A '`' is a generic type's arity ("generic types are
/// not supported for MSIL yet") only after a valid name, that is, when the
/// name is otherwise valid with arity suffixes ("List`1", "Ns.Outer`1/Inner");
/// anywhere else, as in "`Foo", "Ns.`1" or the compiler-generated
/// "<>f__AnonymousType0`2", it makes the name invalid ("'<name>' is not a
/// valid type name", or the nested-type hint when the name has a '+'). The
/// rule is the same for every caller: TYPE with and without 'valuetype'
/// (parseManagedTypeName), an msil_catch type (parseManagedCatchType) and the
/// name after 'class' or 'valuetype' in SIGNATURE. Every caller tests for
/// generic type arguments first (hasManagedGenericArguments, or the same test
/// in SIGNATURE), so that a generic type's arity followed by its arguments
/// ("List`1<int32>") gives "generic instantiations are not supported for MSIL
/// yet" in each of them.
inline Error checkManagedTypeName(StringRef Full) {
  if (Full.empty())
    return managedSigError("a type name is missing");
  if (Full.contains('`') && isManagedTypeNameText(Full, /*AllowArity=*/true))
    return managedSigError("generic types are not supported for MSIL yet");
  if (Full.contains('+'))
    return managedSigError("'" + Full +
                           "' is not a valid type name: a nested type is "
                           "written 'Outer/Inner'");
  if (!isManagedTypeNameText(Full, /*AllowArity=*/false))
    return managedSigError("'" + Full + "' is not a valid type name");
  return Error::success();
}

/// Whether the TYPE text \p T (after any 'valuetype ' prefix) or the
/// EXCEPTION_TYPE text \p T has generic type arguments: a '<' after a valid
/// type name, a generic type's arity included ("List`1<int32>",
/// "Ns.Generic<int32>", "System.Nullable`1<int32>"). checkManagedTypeNameForms
/// tests it before checkManagedTypeName and gives "generic instantiations are
/// not supported for MSIL yet", and SIGNATURE's parser makes the same test on
/// the name after 'class' or 'valuetype'. A '<' after anything else, as in the
/// compiler-generated names "<Module>" and "Ns.<Module>", is part of an
/// invalid name, which checkManagedTypeName reports.
inline bool hasManagedGenericArguments(StringRef T) {
  size_t Less = T.find('<');
  return Less != StringRef::npos &&
         isManagedTypeNameText(T.take_front(Less).rtrim(),
                               /*AllowArity=*/true);
}

/// Checks the TYPE text \p T (after any 'valuetype ' prefix) or the
/// EXCEPTION_TYPE text \p T for the forms that write more than a type name,
/// in this order, each with the reason that names it (T-M2): a resolution
/// scope (a leading '[': "a type is named without a resolution scope; the
/// assembly argument sets the scope of text output"), generic type arguments
/// (hasManagedGenericArguments: "generic instantiations are not supported for
/// MSIL yet"), a byref (a '&': "byref types are not supported for MSIL yet")
/// and a pointer (a trailing '*': "a pointer type has no members; name the
/// type that declares the member"). parseManagedTypeName runs it with and
/// without 'valuetype', so that a form gets the same reason in both, and
/// parseManagedCatchType after its shape test, which has already refused a
/// '&' and a trailing '*'. The caller then checks the name itself
/// (checkManagedTypeName), which would report any of these forms as an
/// invalid name.
inline Error checkManagedTypeNameForms(StringRef T) {
  if (T.starts_with('['))
    return managedSigError("a type is named without a resolution scope; the "
                           "assembly argument sets the scope of text output");
  if (hasManagedGenericArguments(T))
    return managedSigError(
        "generic instantiations are not supported for MSIL yet");
  if (T.contains('&'))
    return managedSigError("byref types are not supported for MSIL yet");
  if (T.ends_with('*'))
    return managedSigError(
        "a pointer type has no members; name the type that declares the "
        "member");
  return Error::success();
}

/// A recursive-descent parser of canonical listing v1 signature text.
class ManagedSigParser {
  StringRef Rest;

  void skipSpace() { Rest = Rest.ltrim(); }
  /// The quoted remainder of the text, for errors.
  std::string here() const {
    if (Rest.empty())
      return "the end of the text";
    return ("'" + Rest + "'").str();
  }
  /// The run of name characters at the start of the text (not consumed).
  StringRef peekWord() {
    skipSpace();
    return Rest.take_while(isManagedNameChar);
  }
  bool consumeWord(StringRef W) {
    if (peekWord() != W)
      return false;
    Rest = Rest.drop_front(W.size());
    return true;
  }
  bool consumePunct(char C) {
    skipSpace();
    if (Rest.empty() || Rest.front() != C)
      return false;
    Rest = Rest.drop_front();
    return true;
  }

public:
  explicit ManagedSigParser(StringRef Text) : Rest(Text) {}

  bool atEnd() {
    skipSpace();
    return Rest.empty();
  }
  std::string rest() const { return here(); }

  /// The name after 'class' or 'valuetype', checked in this order: a
  /// resolution scope ('['), a nested type written with '+', generic type
  /// arguments, then the name itself (checkManagedTypeName). A '<' after a
  /// name that is valid with arity suffixes starts generic type arguments, as
  /// hasManagedGenericArguments has it for TYPE, so 'class List`1<int32>'
  /// gives "generic instantiations are not supported for MSIL yet", as
  /// 'class Ns.Generic<int32>' and TYPE 'List`1<int32>' do, while
  /// 'class List`1' is a generic type and 'class `Foo' an invalid name. A
  /// '<' after anything else ends an invalid name, which checkManagedTypeName
  /// reports ('Ns.' in 'class Ns.<Module>').
  Expected<std::string> parseTypeName(StringRef Keyword) {
    skipSpace();
    if (!Rest.empty() && Rest.front() == '[')
      return managedSigError("a type is named without a resolution scope; "
                             "the assembly argument sets the scope of text "
                             "output");
    StringRef Name = peekWord();
    if (Name.empty())
      return managedSigError("expected a type name after '" + Keyword +
                             "' at " + here());
    Rest = Rest.drop_front(Name.size());
    if (!Rest.empty() && Rest.front() == '+') {
      StringRef Nested = Rest.take_while(
          [](char C) { return C == '+' || isManagedNameChar(C); });
      return managedSigError("'" + Name + Nested +
                             "' is not a valid type name: a nested type is "
                             "written 'Outer/Inner'");
    }
    skipSpace();
    if (!Rest.empty() && Rest.front() == '<' &&
        isManagedTypeNameText(Name, /*AllowArity=*/true))
      return managedSigError(
          "generic instantiations are not supported for MSIL yet");
    if (Error E = checkManagedTypeName(Name))
      return std::move(E);
    return Name.str();
  }

  /// A type without its suffixes.
  Expected<ManagedType> parseBaseType() {
    using TK = ManagedType::TypeKind;
    skipSpace();
    if (Rest.empty())
      return managedSigError("expected a type at the end of the text");
    if (Rest.front() == '!')
      return managedSigError(
          "generic parameters are not supported for MSIL yet");
    if (Rest.front() == '[')
      return managedSigError("a type is named without a resolution scope; "
                             "the assembly argument sets the scope of text "
                             "output");
    StringRef Word = peekWord();
    if (Word.empty())
      return managedSigError("expected a type at " + here());
    if (Word == "native") {
      Rest = Rest.drop_front(Word.size());
      if (consumeWord("int"))
        return ManagedType::get(TK::NativeInt);
      if (consumeWord("uint"))
        return ManagedType::get(TK::NativeUInt);
      return managedSigError("expected 'int' or 'uint' after 'native' at " +
                             here());
    }
    if (Word == "class" || Word == "valuetype") {
      Rest = Rest.drop_front(Word.size());
      Expected<std::string> Name = parseTypeName(Word);
      if (!Name)
        return Name.takeError();
      return Word == "class" ? ManagedType::getClass(*Name)
                             : ManagedType::getValueType(*Name);
    }
    if (Word == "typedref")
      return managedSigError("typedref is not supported for MSIL yet");
    if (Word == "method")
      return managedSigError(
          "function pointer types are not supported for MSIL yet");
    if (Word == "vararg")
      return managedSigError(
          "vararg signatures are not supported for MSIL yet");
    static constexpr std::pair<StringLiteral, TK> Primitives[] = {
        {"void", TK::Void},       {"bool", TK::Bool},
        {"char", TK::Char},       {"int8", TK::Int8},
        {"uint8", TK::UInt8},     {"int16", TK::Int16},
        {"uint16", TK::UInt16},   {"int32", TK::Int32},
        {"uint32", TK::UInt32},   {"int64", TK::Int64},
        {"uint64", TK::UInt64},   {"float32", TK::Float32},
        {"float64", TK::Float64}, {"string", TK::String},
        {"object", TK::Object},
    };
    for (const auto &[Name, Kind] : Primitives) {
      if (Word == Name) {
        Rest = Rest.drop_front(Word.size());
        return ManagedType::get(Kind);
      }
    }
    return managedSigError("unknown type '" + Word + "'");
  }

  /// A type with its '*' and '[]' suffixes. 'void' is allowed alone only if
  /// \p AllowVoid, and always as a pointee.
  Expected<ManagedType> parseType(bool AllowVoid) {
    Expected<ManagedType> T = parseBaseType();
    if (!T)
      return T.takeError();
    while (true) {
      skipSpace();
      if (consumePunct('*')) {
        *T = ManagedType::getPointer(std::move(*T));
      } else if (consumePunct('&')) {
        return managedSigError("byref types are not supported for MSIL yet");
      } else if (consumePunct('[')) {
        if (!consumePunct(']'))
          return managedSigError(
              "multi-dimensional arrays are not supported for MSIL yet");
        if (T->isVoid())
          return managedSigError("'void' is not a valid array element type");
        *T = ManagedType::getSZArray(std::move(*T));
      } else if (!Rest.empty() && Rest.front() == '<') {
        return managedSigError(
            "generic instantiations are not supported for MSIL yet");
      } else if (peekWord() == "modreq" || peekWord() == "modopt") {
        return managedSigError(
            "custom modifiers are not supported for MSIL yet");
      } else if (peekWord() == "pinned") {
        return managedSigError("'pinned' is not valid in a member signature");
      } else {
        break;
      }
    }
    if (T->isVoid() && !AllowVoid)
      return managedSigError("'void' is only allowed as a return type");
    return T;
  }

  Expected<ManagedSignature> parseSignature() {
    ManagedSignature Sig;
    Sig.HasThis = consumeWord("instance");
    if (peekWord() == "explicit")
      return managedSigError("'explicit' is not valid in a member signature");
    if (peekWord() == "vararg")
      return managedSigError(
          "vararg signatures are not supported for MSIL yet");
    if (consumeWord("field")) {
      Sig.IsField = true;
      Expected<ManagedType> T = parseType(/*AllowVoid=*/false);
      if (!T)
        return T.takeError();
      Sig.Return = std::move(*T);
      if (!atEnd())
        return managedSigError("unexpected text " + here() +
                               " after the field type");
      return Sig;
    }
    Expected<ManagedType> Ret = parseType(/*AllowVoid=*/true);
    if (!Ret)
      return Ret.takeError();
    Sig.Return = std::move(*Ret);
    if (!consumePunct('('))
      return managedSigError("expected '(' after the return type at " + here());
    if (consumePunct(')')) {
      if (!atEnd())
        return managedSigError("unexpected text " + here() + " after ')'");
      return Sig;
    }
    while (true) {
      skipSpace();
      if (Rest.starts_with("..."))
        return managedSigError(
            "vararg signatures are not supported for MSIL yet");
      Expected<ManagedType> P = parseType(/*AllowVoid=*/false);
      if (!P)
        return P.takeError();
      Sig.Params.push_back(std::move(*P));
      if (consumePunct(','))
        continue;
      if (consumePunct(')'))
        break;
      return managedSigError("expected ',' or ')' at " + here());
    }
    if (!atEnd())
      return managedSigError("unexpected text " + here() + " after ')'");
    return Sig;
  }
};

} // namespace detail

/// Parses SIGNATURE (MA3): '[instance ]RET(P1, P2, ...)' or '[instance ]field
/// T' in canonical listing v1 text. Types are the listing primitives ('void'
/// only as the return type or a pointee), 'class Ns.T', 'valuetype Ns.T'
/// (nested types as 'Ns.Outer/Inner'), 'T*' and 'T[]'; spaces between tokens
/// are free. The error's message is the reason alone, which T-M2 quotes:
/// a form that the fuller managed calls add reads '<form> is not supported
/// for MSIL yet' (generic types, instantiations and parameters, byrefs,
/// custom modifiers, vararg, function pointers, typedref, multi-dimensional
/// arrays).
inline Expected<ManagedSignature> parseManagedSignature(StringRef Text) {
  detail::ManagedSigParser P(Text);
  if (P.atEnd())
    return detail::managedSigError("the signature is empty");
  return P.parseSignature();
}

/// Parses one type of canonical listing v1 text, as a signature spells it.
inline Expected<ManagedType> parseManagedSigType(StringRef Text) {
  detail::ManagedSigParser P(Text);
  Expected<ManagedType> T = P.parseType(/*AllowVoid=*/true);
  if (!T)
    return T.takeError();
  if (!P.atEnd())
    return detail::managedSigError("unexpected text " + P.rest() +
                                   " after the type");
  return T;
}

/// Parses TYPE (MA1): "Ns.Name" or "Ns.Outer/Inner" (a Class: a reference
/// type, class or interface), "valuetype Ns.Name" (a ValueType, an enum
/// included), or "<elem>[]" (an SZArray, valid only with MEMBER ".ctor")
/// whose element is a listing primitive other than 'void', 'string',
/// 'object' or 'class Ns.T'. The error's message is the reason (T-M2). The
/// text after any 'valuetype ' follows one rule with and without it, so that
/// a form gets the same reason in both. The forms that write more than a type
/// name are tested first, each with its own reason, in this order
/// (checkManagedTypeNameForms): a resolution scope ("[mscorlib]X"), generic
/// type arguments (a '<' after a valid name, hasManagedGenericArguments), a
/// byref ('&') and a pointer (a trailing '*'). Without 'valuetype', a leading
/// 'class ' is refused before them, and 'class' or 'valuetype' alone and a
/// listing primitive after them. The name itself is checked last
/// (checkManagedTypeName): a '`' in a name that is valid apart from its arity
/// suffixes is a generic type's arity ("generic types are not supported for
/// MSIL yet"); anywhere else, as in "<Module>", "`Foo" or
/// "<>f__AnonymousType0`2", a '<' or a '`' is part of an invalid name
/// ("'<name>' is not a valid type name").
inline Expected<ManagedType> parseManagedTypeName(StringRef Text) {
  StringRef T = Text.trim();
  if (T.empty())
    return detail::managedSigError("the type is empty");
  if (T.ends_with("]")) {
    Expected<ManagedType> A = parseManagedSigType(T);
    if (!A)
      return A.takeError();
    if (A->Kind != ManagedType::TypeKind::SZArray)
      return detail::managedSigError("'" + T + "' is not a valid type");
    const ManagedType &E = A->getElement();
    if (!(E.isPrimitive() || E.Kind == ManagedType::TypeKind::Class))
      return detail::managedSigError(
          "an array type's element is a primitive type, 'string', 'object' or "
          "'class <type>'");
    return A;
  }
  if (T.consume_front("valuetype ")) {
    T = T.ltrim();
    if (Error E = detail::checkManagedTypeNameForms(T))
      return std::move(E);
    if (Error E = detail::checkManagedTypeName(T))
      return std::move(E);
    return ManagedType::getValueType(T);
  }
  if (T.starts_with("class "))
    return detail::managedSigError("a reference type is named without 'class'");
  if (Error E = detail::checkManagedTypeNameForms(T))
    return std::move(E);
  if (T == "class" || T == "valuetype")
    return detail::managedSigError("a type name is missing");
  for (unsigned K = 0; K <= unsigned(ManagedType::TypeKind::Object); ++K) {
    if (T != getManagedPrimitiveName(ManagedType::TypeKind(K)))
      continue;
    // A listing primitive such as "string" or "int32": the declaring type is
    // named by its full name.
    return detail::managedSigError(
        "'" + T +
        "' is a signature type; name the declaring type by its full name, "
        "for example 'System.String' or 'valuetype System.Int32'");
  }
  if (Error E = detail::checkManagedTypeName(T))
    return std::move(E);
  return ManagedType::getClass(T);
}

/// Parses an msil_catch EXCEPTION_TYPE (MA5): "Ns.Name" or "Ns.Outer/Inner"
/// of a reference type. The error's message is the reason (T-M2). A text that
/// starts with 'valuetype ' or 'class ', or that is an array (a trailing
/// ']'), a pointer (a trailing '*') or a byref (a '&'), gives "an exception
/// type is a reference type named 'Ns.Name'". The restriction is permanent,
/// as an exception type is always a class, so the reason has no 'yet' (plan
/// 5.15.1 rule 2). Then a resolution scope and generic type arguments get
/// TYPE's reasons (checkManagedTypeNameForms; the stub's filter names each
/// catch type in the scope that ASSEMBLY sets for text output, as it names
/// TYPE), and the name is checked as TYPE's is (checkManagedTypeName), so a
/// '<' or a '`' counts as for TYPE (parseManagedTypeName).
inline Expected<ManagedType> parseManagedCatchType(StringRef Text) {
  StringRef T = Text.trim();
  if (T.empty())
    return detail::managedSigError("the type is empty");
  if (T.starts_with("valuetype ") || T.starts_with("class ") ||
      T.ends_with("]") || T.ends_with("*") || T.contains('&'))
    return detail::managedSigError(
        "an exception type is a reference type named 'Ns.Name'");
  if (Error E = detail::checkManagedTypeNameForms(T))
    return std::move(E);
  if (Error E = detail::checkManagedTypeName(T))
    return std::move(E);
  return ManagedType::getClass(T);
}

/// Checks MEMBER (MA2): a method or field name, ".ctor", or a property
/// accessor by its method name ("get_Length"). The error's message is the
/// reason (T-M2). A '`' (a generic method's arity) or '<' (its type
/// arguments) after a member name makes a generic method ("Empty`1",
/// "Empty<int32>"); anywhere else, as in the compiler-generated "<Clone>$",
/// it makes the name invalid. ".castclass" (ManagedCastMember), the MEMBER
/// of the cast stubs that clang's CodeGen declares, is invalid here too: the
/// backend and msil-ld test a cast stub (isManagedCastShape) before this.
inline Error checkManagedMemberName(StringRef Member) {
  if (Member.empty())
    return detail::managedSigError("the member name is empty");
  if (Member == ".ctor")
    return Error::success();
  if (Member == ".cctor")
    return detail::managedSigError("a type initializer cannot be called");
  size_t Generic = Member.find_first_of("`<");
  StringRef Name =
      Generic == StringRef::npos ? Member : Member.take_front(Generic).rtrim();
  bool Valid = !Name.empty() && !isDigit(Name.front());
  for (char C : Name)
    Valid &= isAlnum(C) || C == '_' || C == '$' ||
             static_cast<unsigned char>(C) >= 0x80;
  if (Generic != StringRef::npos &&
      (Valid || Name == ".ctor" || Name == ".cctor"))
    return detail::managedSigError(
        "generic methods are not supported for MSIL yet");
  if (!Valid)
    return detail::managedSigError("'" + Member +
                                   "' is not a valid member name");
  return Error::success();
}

/// The argument of msil_method that a shape problem is reported on (T-M2's
/// <argument>).
enum class ManagedCallArg : uint8_t { Type, Member, Signature };

/// A failure of checkManagedCallShape: the argument and the reason.
struct ManagedShapeProblem {
  ManagedCallArg Arg;
  std::string Reason;
};

/// The argument's name as T-M2 prints it: "type", "member" or "signature".
inline StringRef getManagedCallArgName(ManagedCallArg Arg) {
  switch (Arg) {
  case ManagedCallArg::Type:
    return "type";
  case ManagedCallArg::Member:
    return "member";
  case ManagedCallArg::Signature:
    return "signature";
  }
  return "";
}

/// Checks that SIGNATURE is consistent with MEMBER and TYPE (MS3): a '.ctor'
/// is 'instance' and returns void and is not a field; an array TYPE has only
/// the member '.ctor' with the signature 'instance void(int32)' (newarr's
/// length operand); a field signature names a field, not '.ctor'. It also
/// refuses the two forms that make a delegate over a C function, which come
/// with the fuller managed calls (plan 5.20.14, 'Delegates'): the '.ctor' of
/// a class (TYPE without 'valuetype') whose signature is 'instance
/// void(object, native int)', the shape of every delegate's constructor
/// ('class System.Object' counts as 'object'), whatever the class; and
/// System.Runtime.InteropServices.Marshal's GetDelegateForFunctionPointer,
/// whatever its signature, also with TYPE written 'valuetype'. A value
/// type's '.ctor(object, native int)', such as
/// System.Runtime.InteropServices.HandleRef's, is accepted. Returns the first
/// problem, or std::nullopt.
inline std::optional<ManagedShapeProblem>
checkManagedCallShape(const ManagedType &Type, StringRef Member,
                      const ManagedSignature &Sig) {
  using TK = ManagedType::TypeKind;
  if (Type.Kind == TK::SZArray) {
    if (Member != ".ctor")
      return ManagedShapeProblem{ManagedCallArg::Member,
                                 "an array type has only the member '.ctor'"};
    if (!Sig.HasThis || Sig.IsField || !Sig.Return.isVoid() ||
        Sig.Params.size() != 1 || Sig.Params[0].Kind != TK::Int32)
      return ManagedShapeProblem{
          ManagedCallArg::Signature,
          "an array type's '.ctor' has the signature 'instance void(int32)'"};
    return std::nullopt;
  }
  if (Member == ".ctor") {
    if (Sig.IsField)
      return ManagedShapeProblem{ManagedCallArg::Signature,
                                 "'.ctor' is a method, not a field"};
    if (!Sig.HasThis || !Sig.Return.isVoid())
      return ManagedShapeProblem{
          ManagedCallArg::Signature,
          "'.ctor' has an 'instance void(...)' signature"};
    auto IsObject = [](const ManagedType &T) {
      return T.Kind == TK::Object ||
             (T.Kind == TK::Class && T.TypeName == "System.Object");
    };
    if (Type.Kind == TK::Class && Sig.Params.size() == 2 &&
        IsObject(Sig.Params[0]) && Sig.Params[1].Kind == TK::NativeInt)
      return ManagedShapeProblem{
          ManagedCallArg::Signature,
          "a class constructor with the signature '(object, native int)', "
          "the shape of a delegate's constructor, is not supported for MSIL "
          "yet"};
  }
  // Whether or not TYPE says 'valuetype': text output names the type that
  // declares a static method without 'valuetype', so such text would still
  // call Marshal's method, unchecked.
  if (Type.isNamed() &&
      Type.TypeName == "System.Runtime.InteropServices.Marshal" &&
      Member == "GetDelegateForFunctionPointer")
    return ManagedShapeProblem{
        ManagedCallArg::Member,
        "delegates over function pointers are not supported for MSIL yet"};
  return std::nullopt;
}

/// Whether \p TypeName is one of the catch-all exception types that
/// msil_catch refuses (MS6, T-M11): System.Object, System.Exception,
/// System.SystemException, System.Runtime.InteropServices.ExternalException
/// and System.Runtime.InteropServices.SEHException.
inline bool isRefusedCatchType(StringRef TypeName) {
  return TypeName == "System.Object" || TypeName == "System.Exception" ||
         TypeName == "System.SystemException" ||
         TypeName == "System.Runtime.InteropServices.ExternalException" ||
         TypeName == "System.Runtime.InteropServices.SEHException";
}

//===----------------------------------------------------------------------===//
// The 'cli_type' attribute (plan 5.21.2 RT2, RT3; 5.21.3 RC4)
//===----------------------------------------------------------------------===//

/// The .NET type that a 'cli_type' attribute names (parseCLITypeName).
struct CLITypeName {
  /// The type, folded at every level (foldCLIType): String, Object, a Class
  /// or an SZArray.
  ManagedType Type;
  /// The key (getCLITypeKey): "string", "object", "class System.IO.Stream",
  /// "string[]", "class Ns.Outer/Inner[]". Two spellings of one class or array
  /// have one key, which identifies the C type of its references
  /// (clang::CLIReferenceType).
  std::string Key;
  /// The display name (getCLITypeDisplayName): the class in the TYPE grammar
  /// of msil_method (parseManagedTypeName), "System.String",
  /// "Ns.Outer/Inner", "string[]", "class Ns.T[]", by which clang's
  /// diagnostics, the debug information and the cast stubs name it.
  std::string DisplayName;
};

/// \p T folded at every level, as a 'cli_type' key folds it: 'class
/// System.String' is 'string' and 'class System.Object' is 'object', also as
/// the element of an array or a pointer. Every other type is unchanged.
inline ManagedType foldCLIType(const ManagedType &T) {
  using TK = ManagedType::TypeKind;
  switch (T.Kind) {
  case TK::Class:
    if (T.TypeName == "System.String")
      return ManagedType::get(TK::String);
    if (T.TypeName == "System.Object")
      return ManagedType::get(TK::Object);
    return T;
  case TK::Pointer:
    return ManagedType::getPointer(foldCLIType(T.getElement()));
  case TK::SZArray:
    return ManagedType::getSZArray(foldCLIType(T.getElement()));
  default:
    return T;
  }
}

namespace detail {
/// Prints \p T as listing text without a scope and with its names unquoted.
inline void printCLITypeKey(raw_ostream &OS, const ManagedType &T) {
  using TK = ManagedType::TypeKind;
  switch (T.Kind) {
  case TK::Class:
    OS << "class " << T.TypeName;
    return;
  case TK::ValueType:
    OS << "valuetype " << T.TypeName;
    return;
  case TK::Pointer:
    printCLITypeKey(OS, T.getElement());
    OS << '*';
    return;
  case TK::SZArray:
    printCLITypeKey(OS, T.getElement());
    OS << "[]";
    return;
  default:
    OS << getManagedPrimitiveName(T.Kind);
    return;
  }
}
} // namespace detail

/// The key of the .NET type \p T: the canonical listing text of foldCLIType
/// of \p T, without a resolution scope and with its names unquoted, so that
/// it reads back: "string", "object", "class System.IO.Stream", "string[]",
/// "int32[]", "class Ns.Outer/Inner[]" ("class Ns.value", which
/// ManagedType::print writes "class Ns.'value'"). The key of a 'cli_type'
/// attribute's type (parseCLITypeName) identifies the C type of its
/// references; a managed call's 'ref' and 'this-ref' conversions take a
/// reference whose key is the CIL type's (plan 5.21.9 RM2).
inline std::string getCLITypeKey(const ManagedType &T) {
  std::string S;
  raw_string_ostream OS(S);
  detail::printCLITypeKey(OS, foldCLIType(T));
  return S;
}

/// The display name of the key \p Key of a 'cli_type' type (getCLITypeKey;
/// plan 5.21.3 RC4): "System.String" for "string", "System.Object" for
/// "object", the full name for "class <name>", and an array's key itself
/// ("string[]", "class Ns.T[]"), which is TYPE text of msil_method too.
inline std::string getCLITypeDisplayName(StringRef Key) {
  if (Key == "string")
    return "System.String";
  if (Key == "object")
    return "System.Object";
  if (!Key.ends_with("[]"))
    Key.consume_front("class ");
  return Key.str();
}

/// Parses the argument of a 'cli_type' attribute (plan 5.21.2 RT2): the TYPE
/// grammar of msil_method (parseManagedTypeName) without value types:
/// "Ns.Name" or "Ns.Outer/Inner" for a class or an interface,
/// "System.String", "System.Object", or "<elem>[]" whose element is a listing
/// primitive other than 'void', 'string', 'object' or 'class Ns.T'. The
/// type is folded at every level (foldCLIType), and the key and the display
/// name follow from it. The error's message is the reason that T-R1 quotes:
///   - 'valuetype' alone or followed by a space gives "value types are not
///     supported for MSIL yet";
///   - a listing primitive written alone gives "'<text>' is a signature type;
///     name the type by its full name, for example 'System.String'";
///   - a resolution scope (a leading '[', or in an array a '[' after the
///     element's 'class' or 'valuetype': "class [x]Ns.T[]") gives "a type is
///     named without a resolution scope";
///   - a pointer (a trailing '*') gives "a pointer type is not a .NET
///     reference type";
///   - every other refusal is parseManagedTypeName's: 'class' outside an
///     array, generic types and instantiations, byrefs, invalid names and
///     invalid array elements.
inline Expected<CLITypeName> parseCLITypeName(StringRef Text) {
  using TK = ManagedType::TypeKind;
  StringRef T = Text.trim();
  if (T.empty())
    return detail::managedSigError("the type is empty");
  if (T == "valuetype" || T.starts_with("valuetype "))
    return detail::managedSigError(
        "value types are not supported for MSIL yet");
  // A resolution scope: a leading '[' (alone, before a name or before an
  // array's element) and, in an array, a '[' after the element's first word
  // 'class' or 'valuetype', the places where parseManagedSigType reads one.
  // parseManagedTypeName's reason for it names msil_method's assembly
  // argument, which 'cli_type' does not have.
  StringRef Scoped = T;
  if (T.ends_with("]")) {
    StringRef Word = T.take_while(detail::isManagedNameChar);
    if (Word == "class" || Word == "valuetype")
      Scoped = T.drop_front(Word.size()).ltrim();
  }
  if (Scoped.starts_with('['))
    return detail::managedSigError(
        "a type is named without a resolution scope");
  if (!T.ends_with("]")) {
    // After the forms that parseManagedTypeName reports first: 'class ',
    // generic type arguments and a byref.
    if (T.ends_with('*') && !T.starts_with("class ") &&
        !detail::hasManagedGenericArguments(T) && !T.contains('&'))
      return detail::managedSigError(
          "a pointer type is not a .NET reference type");
    for (unsigned K = 0; K <= unsigned(TK::Object); ++K)
      if (T == getManagedPrimitiveName(TK(K)))
        return detail::managedSigError(
            "'" + T +
            "' is a signature type; name the type by its full name, for "
            "example 'System.String'");
  }
  Expected<ManagedType> Parsed = parseManagedTypeName(T);
  if (!Parsed)
    return Parsed.takeError();
  CLITypeName Name;
  Name.Type = foldCLIType(*Parsed);
  Name.Key = getCLITypeKey(Name.Type);
  Name.DisplayName = getCLITypeDisplayName(Name.Key);
  return Name;
}

//===----------------------------------------------------------------------===//
// Conversion codes (plan 5.20.3 MS5) and their IR attribute
//===----------------------------------------------------------------------===//

/// The conversion of one C parameter or of the C result, as Sema chose it
/// from MS5's table. The backend executes it and decides nothing.
enum class ManagedConv : uint8_t {
  Bool,    ///< "bool": CIL bool; in: ldc.i4.0, cgt.un.
  Val,     ///< "val": the value as it is (integers, char, floating point,
           ///< native int, pointers).
  Enum,    ///< "enum": an integer for 'valuetype E' (msil-ld checks E).
  UTF8,    ///< "utf8": a C string decoded as UTF-8 (S1).
  UTF16,   ///< "utf16": a UTF-16 C string (S2).
  UTF32,   ///< "utf32": a UTF-32 C string (S3).
  Obj,     ///< "obj": a handle of string, object, class T or T[] (MH2, MH4).
  Boxed,   ///< "boxed": a handle of a boxed non-enum value type.
  GCH,     ///< "gch": the GCHandle's IntPtr itself.
  ThisObj, ///< "this-obj": the 'this' handle of a reference type.
  ThisVal, ///< "this-val": the 'this' handle of a boxed value type.
  ThisGCH, ///< "this-gch": the 'this' GCHandle.
  Void,    ///< "void": no result.
  Err,     ///< "err": the trailing 'int *' of msil_catch (5.20.8).
  Ref,     ///< "ref": a .NET reference (plan 5.21.9 RM1) of string, object,
           ///< class T or T[], whose key is the CIL type's; passed with a
           ///< castclass to the CIL type unless it is 'object', returned as
           ///< it is.
  ThisRef, ///< "this-ref": the 'this' reference of a class, an interface,
           ///< System.String or System.Object TYPE: castclass to TYPE unless
           ///< it is System.Object, then callvirt (ldfld for a field).
};

/// The code's text in the msil-managed-conv attribute.
inline StringRef getManagedConvName(ManagedConv C) {
  switch (C) {
  case ManagedConv::Bool:
    return "bool";
  case ManagedConv::Val:
    return "val";
  case ManagedConv::Enum:
    return "enum";
  case ManagedConv::UTF8:
    return "utf8";
  case ManagedConv::UTF16:
    return "utf16";
  case ManagedConv::UTF32:
    return "utf32";
  case ManagedConv::Obj:
    return "obj";
  case ManagedConv::Boxed:
    return "boxed";
  case ManagedConv::GCH:
    return "gch";
  case ManagedConv::ThisObj:
    return "this-obj";
  case ManagedConv::ThisVal:
    return "this-val";
  case ManagedConv::ThisGCH:
    return "this-gch";
  case ManagedConv::Void:
    return "void";
  case ManagedConv::Err:
    return "err";
  case ManagedConv::Ref:
    return "ref";
  case ManagedConv::ThisRef:
    return "this-ref";
  }
  return "";
}

/// The code whose text is \p Name, or std::nullopt.
inline std::optional<ManagedConv> parseManagedConvName(StringRef Name) {
  static constexpr ManagedConv All[] = {
      ManagedConv::Bool,    ManagedConv::Val,     ManagedConv::Enum,
      ManagedConv::UTF8,    ManagedConv::UTF16,   ManagedConv::UTF32,
      ManagedConv::Obj,     ManagedConv::Boxed,   ManagedConv::GCH,
      ManagedConv::ThisObj, ManagedConv::ThisVal, ManagedConv::ThisGCH,
      ManagedConv::Void,    ManagedConv::Err,     ManagedConv::Ref,
      ManagedConv::ThisRef,
  };
  for (ManagedConv C : All)
    if (getManagedConvName(C) == Name)
      return C;
  return std::nullopt;
}

/// The conversions of one declaration: one per C parameter, in order, and
/// the result's.
struct ManagedConvList {
  SmallVector<ManagedConv, 8> Params;
  ManagedConv Result = ManagedConv::Void;
};

/// The msil-managed-conv text (MR1): the parameters' codes joined by ',',
/// then ';' and the result's code, e.g. "utf8,enum,val,err;obj" or ";void".
inline std::string printManagedConvList(ArrayRef<ManagedConv> Params,
                                        ManagedConv Result) {
  std::string S;
  raw_string_ostream OS(S);
  for (size_t I = 0, E = Params.size(); I != E; ++I) {
    if (I)
      OS << ',';
    OS << getManagedConvName(Params[I]);
  }
  OS << ';' << getManagedConvName(Result);
  return S;
}

/// The attributes operand of a managed call's !msil.managed.calls entry, its
/// 8th (MR4, ManagedCallsMetadata): whichever of 'dllexport',
/// 'constructor(<prio>)' and 'destructor(<prio>)' are present, in that
/// order, joined with ',' and no spaces, for example
/// "dllexport,constructor(300),destructor(500)". \p Constructor and
/// \p Destructor hold the attribute's effective priority (65535 when it gives
/// none). Returns "" when none is present; the 8th operand is then not
/// written. clang's CodeGen writes the operand with it, and msil-ld spells a
/// CIL object's forwarders with it to compare them with a bitcode unit's
/// operand.
inline std::string
printManagedCallAttributes(bool DLLExport, std::optional<uint32_t> Constructor,
                           std::optional<uint32_t> Destructor) {
  std::string S;
  raw_string_ostream OS(S);
  ListSeparator LS(",");
  if (DLLExport)
    OS << LS << "dllexport";
  if (Constructor)
    OS << LS << "constructor(" << *Constructor << ')';
  if (Destructor)
    OS << LS << "destructor(" << *Destructor << ')';
  return S;
}

/// Parses the msil-managed-conv text. Besides the codes' spelling it checks
/// their positions: a 'this-*' code ('this-ref' included) only as the first
/// parameter, 'err' only as the last, 'void' only as the result, and the
/// result never a string, 'this-*' or 'err' code.
inline Expected<ManagedConvList> parseManagedConvList(StringRef Text) {
  auto [ParamText, ResultText] = Text.split(';');
  if (ParamText.size() == Text.size())
    return detail::managedSigError("'" + Text +
                                   "' has no ';' before the result's code");
  ManagedConvList L;
  if (!ParamText.empty()) {
    SmallVector<StringRef, 8> Parts;
    ParamText.split(Parts, ',');
    for (size_t I = 0, E = Parts.size(); I != E; ++I) {
      std::optional<ManagedConv> C = parseManagedConvName(Parts[I]);
      if (!C)
        return detail::managedSigError("unknown conversion code '" + Parts[I] +
                                       "'");
      bool IsThis = *C == ManagedConv::ThisObj || *C == ManagedConv::ThisVal ||
                    *C == ManagedConv::ThisGCH || *C == ManagedConv::ThisRef;
      if ((IsThis && I != 0) || (*C == ManagedConv::Err && I + 1 != E) ||
          *C == ManagedConv::Void)
        return detail::managedSigError("conversion code '" + Parts[I] +
                                       "' is not valid at parameter " +
                                       Twine(I + 1));
      L.Params.push_back(*C);
    }
  }
  std::optional<ManagedConv> R = parseManagedConvName(ResultText);
  if (!R)
    return detail::managedSigError("unknown conversion code '" + ResultText +
                                   "'");
  switch (*R) {
  case ManagedConv::UTF8:
  case ManagedConv::UTF16:
  case ManagedConv::UTF32:
  case ManagedConv::ThisObj:
  case ManagedConv::ThisVal:
  case ManagedConv::ThisGCH:
  case ManagedConv::ThisRef:
  case ManagedConv::Err:
    return detail::managedSigError("conversion code '" + ResultText +
                                   "' is not valid for the result");
  default:
    break;
  }
  L.Result = *R;
  return L;
}

/// Whether a managed-call declaration whose attributes hold TYPE \p Type,
/// MEMBER \p Member, SIGNATURE \p Signature and the conversions
/// \p Conversions is a cast stub (plan 5.21.9 RM8): MEMBER '.castclass'
/// (ManagedCastMember), SIGNATURE 'object(object)' and the conversions
/// 'ref;ref' (spaces between the signature's tokens are free), and TYPE a
/// type of the 'cli_type' grammar (parseCLITypeName) other than
/// System.Object, to which a cast emits nothing: a class, an interface,
/// System.String or an array. clang's CodeGen writes TYPE as the class's
/// display name (CLITypeName::DisplayName). The stub's body is 'ldarg.0;
/// castclass <TYPE>; ret', and it makes no member access. The backend
/// (MSIL::parseManagedCall) and msil-ld test this before
/// checkManagedMemberName, which refuses '.castclass' in every other
/// declaration. Its one parameter and one conversion leave no room for
/// msil_catch's error code, which their arity checks refuse.
inline bool isManagedCastShape(StringRef Type, StringRef Member,
                               StringRef Signature, StringRef Conversions) {
  using TK = ManagedType::TypeKind;
  if (Member != ManagedCastMember)
    return false;
  Expected<ManagedSignature> Sig = parseManagedSignature(Signature);
  if (!Sig) {
    consumeError(Sig.takeError());
    return false;
  }
  if (Sig->HasThis || Sig->IsField || Sig->Return.Kind != TK::Object ||
      Sig->Params.size() != 1 || Sig->Params[0].Kind != TK::Object)
    return false;
  Expected<ManagedConvList> Conv = parseManagedConvList(Conversions);
  if (!Conv) {
    consumeError(Conv.takeError());
    return false;
  }
  if (Conv->Params.size() != 1 || Conv->Params[0] != ManagedConv::Ref ||
      Conv->Result != ManagedConv::Ref)
    return false;
  Expected<CLITypeName> Name = parseCLITypeName(Type);
  if (!Name) {
    consumeError(Name.takeError());
    return false;
  }
  return Name->Type.Kind != TK::Object;
}

//===----------------------------------------------------------------------===//
// msil_catch lists (plan 5.20.2 MA5, 5.20.8)
//===----------------------------------------------------------------------===//

/// One msil_catch entry: an exception type's full name and its code (1 to
/// INT32_MAX).
struct ManagedCatch {
  std::string Type;
  int32_t Code = 0;

  friend bool operator==(const ManagedCatch &A, const ManagedCatch &B) {
    return A.Type == B.Type && A.Code == B.Code;
  }
};

/// The msil-managed-catch text (MR1): "<type>=<code>" per entry in the order
/// written, joined by ';'.
inline std::string printManagedCatchList(ArrayRef<ManagedCatch> List) {
  std::string S;
  raw_string_ostream OS(S);
  for (size_t I = 0, E = List.size(); I != E; ++I) {
    if (I)
      OS << ';';
    OS << List[I].Type << '=' << List[I].Code;
  }
  return S;
}

/// Parses the msil-managed-catch text; "" is an empty list.
inline Expected<SmallVector<ManagedCatch, 4>>
parseManagedCatchList(StringRef Text) {
  SmallVector<ManagedCatch, 4> List;
  if (Text.empty())
    return List;
  SmallVector<StringRef, 4> Parts;
  Text.split(Parts, ';');
  for (StringRef Part : Parts) {
    auto [Type, CodeText] = Part.rsplit('=');
    int32_t Code = 0;
    if (Type.size() == Part.size() || CodeText.getAsInteger(10, Code) ||
        Code < 1)
      return detail::managedSigError("'" + Part +
                                     "' is not a '<type>=<code>' entry");
    if (Expected<ManagedType> T = parseManagedCatchType(Type); !T)
      return T.takeError();
    List.push_back({Type.str(), Code});
  }
  return List;
}

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_MANAGEDSIGNATURE_H
