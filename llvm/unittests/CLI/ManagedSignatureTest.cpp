//===- ManagedSignatureTest.cpp - the managed-call signature parser -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/ManagedSignature.h (ImplementationPlan.md 5.20.5 ML1,
// 5.20.2 MA3; milestone MC-1): parseManagedSignature reads a managed-call
// SIGNATURE in canonical listing v1 text (5.7.10), '[instance ]RET(P1, ...)'
// or '[instance ]field T', into ManagedSignature { HasThis, IsField, Return,
// Params }.
// - The accepted types: the listing primitives ('void' as a return only),
//   'string', 'object', 'class Ns.T', 'valuetype Ns.T', nested types
//   'Ns.Outer/Inner', 'T*' and 'T[]'; methods with and without 'instance',
//   with no parameters, and the signatures MC-1's stubs use.
// - Fields: 'field T' and 'instance field T' have IsField, no parameters and
//   HasThis only with 'instance'.
// - The forms the fuller goal adds (generic instantiations and parameters,
//   byrefs, custom modifiers, vararg, function pointers, typedref,
//   multi-dimensional arrays) fail with '... not supported for MSIL yet'
//   (the T-M2 reason; the plan's example is 'byref types are not supported for
//   MSIL yet'); malformed text fails.
// - The parsed fields: every type of an accepted signature is checked kind by
//   kind (the listing primitive, 'class' or 'valuetype' with its full name,
//   nested names 'Ns.Outer/Inner' included, and the depth and order of '*'
//   and '[]'), through a description built from ManagedType's own fields
//   (Kind, TypeName, Element), not from its printer.
// - MS3's shape check (checkManagedCallShape) for the shapes of a delegate
//   over a C function (5.20.14 'Delegates', brief Q27 (a)), each reason
//   pinned whole: the '.ctor' of a class whose parsed signature is
//   'instance void(object, native int)' ('class System.Object' counts as
//   'object', and spacing is free) is refused on SIGNATURE, for any class,
//   and System.Runtime.InteropServices.Marshal::GetDelegateForFunctionPointer
//   on MEMBER, whatever its signature (also with TYPE written 'valuetype',
//   since text output names a static method's declaring type without it).
//   The same '.ctor' of a value type (HandleRef), the class's other
//   constructors and members and Marshal's other members stay accepted.
// - The other parsers of ML1: one type as a signature spells it
//   (parseManagedSigType), TYPE (MA1, parseManagedTypeName), an msil_catch
//   EXCEPTION_TYPE (MA5, parseManagedCatchType) and MEMBER (MA2,
//   checkManagedMemberName), each with an accept and a reject table. '<'
//   and '`' start generic arguments only after a valid name, the 'valuetype'
//   spelling of TYPE included, so a name with '<' or '`' that is not generic
//   ('<Module>', 'Ns.<Module>', '<Clone>$', '`Foo', 'Ns.`1',
//   '<>f__AnonymousType0`2', 'Ns.E/<Select>d__5`2') gets the invalid-name
//   reason, pinned whole, and a generic one ('List`1', 'Ns.Generic<int32>',
//   'valuetype System.Nullable`1<int32>') keeps its reason, also pinned
//   whole. In a signature, generic arguments after a generic type's arity
//   (also after a space) are a generic instantiation, pinned whole, while
//   the arity alone is a generic type. A TYPE with a resolution scope, a
//   byref or a pointer gets that form's reason, pinned whole, with and
//   without 'valuetype'; an EXCEPTION_TYPE with a scope gets the same scope
//   reason, and a byref or pointer one the reason that an exception type is
//   a reference type named 'Ns.Name'.
// - MR1's attribute texts: print(parse(x)) == x for the msil-managed-conv
//   and msil-managed-catch lists, their parsed entries, and malformed lists
//   that must fail.
// - MR2's IR names: isManagedCallName and getManagedCallSymbol, which strips
//   the '__msil_mc.' prefix and, from an internal-linkage declaration's name,
//   the '.__uniq.<n>' postfix (5.20.4 as amended in section 12 on
//   2026-11-04), so diagnostics name the C symbol or the C++ mangled name;
//   a '.__uniq.' without trailing digits is kept.
// - The '.__uniq.<n>' postfix itself (getManagedCallUniqPostfix; 5.20.4 as
//   amended again, the row '5.20.4 (IR name, MR4) amended again (...)'):
//   '.__uniq.' and decimal digits only; equal field lists give equal
//   postfixes and different lists different ones, the fields hashed as a
//   list (("ab", "c") and ("a", "bc") differ); the digits are the 128-bit MD5
//   of each field's byte length, ':' and bytes, in decimal, as the header
//   documents it.
// - Resolution scopes in ILAsm text (5.20.6 MW2 as amended in section 12 on
//   2026-11-04): printILAsmScope prints an ILAsm dotted name bare
//   ('[System.Runtime]') and quotes any other name ("['my lib']", "['a]b']",
//   "['int32']"); isILAsmBareDottedName's accept and reject tables; and
//   ManagedType::print with a scope before every class and value type name,
//   through pointers and arrays.
// - Type names in ILAsm text: ManagedType::print single-quotes each
//   '/'-separated part of a class or value type name that is not an ILAsm
//   dotted name, the way a scope is quoted ("valuetype 'value'",
//   "class Ns.Outer/'value'"; a part with a non-ASCII byte is quoted whole),
//   with or without a scope, through pointers, arrays and signatures, and
//   prints dotted names unchanged.
// - MR4's eighth operand (5.20.5 ML1 and 5.20.4 MR4 as amended in section
//   12, the row '5.20.5 ML1 and 5.20.4 MR4 amended (shared managed-call IR
//   rules)'): printManagedCallAttributes(DLLExport, Constructor, Destructor)
//   joins 'dllexport', 'constructor(<prio>)' and 'destructor(<prio>)' with
//   ',' and no spaces, in that order, and gives "" when none is present.
// Expected values come from the plan, never from the implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/ManagedSignature.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

struct Shape {
  const char *Text;
  bool HasThis;
  bool IsField;
  size_t NumParams;
};

void expectShape(const Shape &S) {
  SCOPED_TRACE(S.Text);
  Expected<ManagedSignature> Sig = parseManagedSignature(S.Text);
  ASSERT_THAT_EXPECTED(Sig, Succeeded());
  EXPECT_EQ(Sig->HasThis, S.HasThis);
  EXPECT_EQ(Sig->IsField, S.IsField);
  EXPECT_EQ(Sig->Params.size(), S.NumParams);
}

void expectRejected(const char *Text) {
  SCOPED_TRACE(Text);
  EXPECT_THAT_EXPECTED(parseManagedSignature(Text), Failed());
}

void expectDeferred(const char *Text) {
  SCOPED_TRACE(Text);
  EXPECT_THAT_EXPECTED(
      parseManagedSignature(Text),
      FailedWithMessage(testing::HasSubstr("not supported for MSIL yet")));
}

/// What checkManagedCallShape (MS3) reports for TYPE, MEMBER and SIGNATURE,
/// each parsed as Sema parses it; all three must parse.
std::optional<ManagedShapeProblem>
shapeProblem(const char *Type, const char *Member, const char *Signature) {
  Expected<ManagedType> T = parseManagedTypeName(Type);
  Expected<ManagedSignature> Sig = parseManagedSignature(Signature);
  EXPECT_THAT_ERROR(checkManagedMemberName(Member), Succeeded());
  EXPECT_THAT_EXPECTED(T, Succeeded());
  EXPECT_THAT_EXPECTED(Sig, Succeeded());
  if (!T || !Sig)
    return ManagedShapeProblem{ManagedCallArg::Type, "(does not parse)"};
  return checkManagedCallShape(*T, Member, *Sig);
}

/// Expects MS3 to refuse the declaration on \p Arg (T-M2's <argument>) with
/// exactly \p Reason.
void expectShapeRefused(const char *Type, const char *Member,
                        const char *Signature, ManagedCallArg Arg,
                        const char *Reason) {
  SCOPED_TRACE(std::string(Type) + " | " + Member + " | " + Signature);
  std::optional<ManagedShapeProblem> P = shapeProblem(Type, Member, Signature);
  ASSERT_TRUE(P.has_value());
  EXPECT_EQ(getManagedCallArgName(P->Arg), getManagedCallArgName(Arg));
  EXPECT_EQ(P->Reason, Reason);
}

void expectShapeAccepted(const char *Type, const char *Member,
                         const char *Signature) {
  SCOPED_TRACE(std::string(Type) + " | " + Member + " | " + Signature);
  std::optional<ManagedShapeProblem> P = shapeProblem(Type, Member, Signature);
  EXPECT_FALSE(P.has_value())
      << getManagedCallArgName(P->Arg).str() << ": " << P->Reason;
}

TEST(CLIManagedSignatureTest, Primitives) {
  const Shape Shapes[] = {
      {"void()", false, false, 0},
      {"int32(int32)", false, false, 1},
      {"float64(float64)", false, false, 1},
      {"bool(char)", false, false, 1},
      {"native int(native int)", false, false, 1},
      {"uint64(uint64, uint64)", false, false, 2},
      {"void(bool, char, int8, uint8, int16, uint16, int32, uint32, int64, "
       "uint64, float32, float64, native int, native uint, string, object)",
       false, false, 16},
      {"string(object)", false, false, 1},
      {"object(string, string)", false, false, 2},
      {"bool()", false, false, 0},
      {"native uint(native uint)", false, false, 1},
      {"float32(float32, int32)", false, false, 2},
  };
  for (const Shape &S : Shapes)
    expectShape(S);
}

TEST(CLIManagedSignatureTest, ClassAndValueType) {
  const Shape Shapes[] = {
      {"class System.IO.Stream()", false, false, 0},
      {"valuetype System.DateTime()", false, false, 0},
      {"int32(valuetype System.DateTime, valuetype System.DateTime)", false,
       false, 2},
      {"int32(valuetype System.IO.FileMode)", false, false, 1},
      {"instance void(string, valuetype System.IO.FileMode, valuetype "
       "System.IO.FileAccess, valuetype System.IO.FileShare, int32)",
       true, false, 5},
      {"instance void()", true, false, 0},
      {"instance int64()", true, false, 0},
      {"instance void(class System.IO.Stream, valuetype System.DateTime)", true,
       false, 2},
      {"valuetype System.Runtime.InteropServices.GCHandle(object)", false,
       false, 1},
      {"native int(valuetype System.Runtime.InteropServices.GCHandle)", false,
       false, 1},
      {"instance object()", true, false, 0},
      {"instance void(string)", true, false, 1},
  };
  for (const Shape &S : Shapes)
    expectShape(S);
  // A type name needs its 'class' or 'valuetype' keyword.
  expectRejected("void(System.IO.Stream)");
  expectRejected("System.DateTime()");
  expectRejected("void(class)");
  expectRejected("void(valuetype)");
}

TEST(CLIManagedSignatureTest, NestedTypes) {
  const Shape Shapes[] = {
      {"string(valuetype System.Environment/SpecialFolder)", false, false, 1},
      {"instance int32(class Ns.Outer/Inner)", true, false, 1},
      {"class Ns.Outer/Inner()", false, false, 0},
      {"valuetype Ns.Outer/Inner[](int32)", false, false, 1},
  };
  for (const Shape &S : Shapes)
    expectShape(S);
}

TEST(CLIManagedSignatureTest, ArraysAndPointers) {
  const Shape Shapes[] = {
      {"void(uint8[], int32, native int, int32)", false, false, 4},
      {"void(void*, void*, int64, int64)", false, false, 4},
      {"string[]()", false, false, 0},
      {"instance void(int32)", true, false, 1},
      {"instance string(uint8*, int32)", true, false, 2},
      {"instance void(char*)", true, false, 1},
      {"class System.IO.Stream[](int32*)", false, false, 1},
      {"object[](object[], string[])", false, false, 2},
      {"void(valuetype System.DateTime*, valuetype System.DateTime[])", false,
       false, 2},
      {"uint8**(int32)", false, false, 1},
      {"instance void(uint8[], int32, int32)", true, false, 3},
  };
  for (const Shape &S : Shapes)
    expectShape(S);
}

TEST(CLIManagedSignatureTest, Fields) {
  const Shape Shapes[] = {
      {"field int64", false, true, 0},
      {"field bool", false, true, 0},
      {"instance field int32", true, true, 0},
      {"instance field int64", true, true, 0},
      {"field valuetype System.DateTime", false, true, 0},
      {"field class System.IO.TextWriter", false, true, 0},
      {"field string[]", false, true, 0},
      {"instance field native int", true, true, 0},
      {"field valuetype Ns.Outer/Inner", false, true, 0},
  };
  for (const Shape &S : Shapes)
    expectShape(S);
  expectRejected("field");
  expectRejected("instance field");
  expectRejected("field void");
  expectRejected("field int32(int32)");
}

TEST(CLIManagedSignatureTest, DeferredForms) {
  // Generic instantiations and generic parameters.
  expectDeferred("void(class System.Collections.Generic.List`1<int32>)");
  expectDeferred("void(!0)");
  expectDeferred("!!0(int32)");
  // Byrefs: the plan's example of the T-M2 reason, pinned whole.
  EXPECT_THAT_EXPECTED(parseManagedSignature("void(int32&)"),
                       FailedWithMessage(testing::HasSubstr(
                           "byref types are not supported for MSIL yet")));
  expectDeferred("instance void(valuetype System.DateTime&)");
  // Custom modifiers.
  expectDeferred("void(int32 modreq(class "
                 "System.Runtime.CompilerServices.IsVolatile))");
  expectDeferred("void(int32 modopt(class "
                 "System.Runtime.CompilerServices.IsConst))");
  // vararg.
  expectDeferred("vararg void(int32)");
  // Function pointers.
  expectDeferred("void(method void*(int32))");
  // typedref.
  expectDeferred("void(typedref)");
  // Multi-dimensional arrays.
  expectDeferred("void(int32[,])");

  // Delegates over C functions (5.20.14 'Delegates', brief Q27 (a)), which
  // MS3's shape check refuses. (a) The '.ctor' of a class whose parsed
  // signature is 'instance void(object, native int)', the shape of every
  // delegate's constructor: System.Action, a third-party class and a nested
  // one, with 'class System.Object' for 'object' and with other spacing. The
  // reason names the shape, never the type.
  const char *CtorShape =
      "a class constructor with the signature '(object, native int)', the "
      "shape of a delegate's constructor, is not supported for MSIL yet";
  const std::pair<const char *, const char *> DelegateCtors[] = {
      {"System.Action", "instance void(object, native int)"},
      {"System.Action", "instance void(class System.Object, native int)"},
      {"System.Action", "  instance  void ( object ,native  int )  "},
      {"System.Action", "instance void (class  System.Object,native int)"},
      {"Contoso.Callback", "instance void(object, native int)"},
      {"Ns.Outer/Handler", "instance void(class System.Object, native int)"},
  };
  for (const auto &[Type, Sig] : DelegateCtors)
    expectShapeRefused(Type, ".ctor", Sig, ManagedCallArg::Signature,
                       CtorShape);
  // (b) Marshal::GetDelegateForFunctionPointer, whatever the signature.
  const char *GetDelegate =
      "delegates over function pointers are not supported for MSIL yet";
  expectShapeRefused("System.Runtime.InteropServices.Marshal",
                     "GetDelegateForFunctionPointer",
                     "class System.Delegate(native int, class System.Type)",
                     ManagedCallArg::Member, GetDelegate);
  expectShapeRefused("System.Runtime.InteropServices.Marshal",
                     "GetDelegateForFunctionPointer", "object(native int)",
                     ManagedCallArg::Member, GetDelegate);
  // Also when TYPE is written 'valuetype': text output names a static
  // method's declaring type without it, so the call would still reach
  // Marshal.
  expectShapeRefused("valuetype System.Runtime.InteropServices.Marshal",
                     "GetDelegateForFunctionPointer",
                     "class System.Delegate(native int, class System.Type)",
                     ManagedCallArg::Member, GetDelegate);
  // Accepted: the same '.ctor' of a value type (HandleRef), the class's
  // other constructors and members, and Marshal's other members, the one
  // with GetDelegateForFunctionPointer's parameters among them.
  expectShapeAccepted("valuetype System.Runtime.InteropServices.HandleRef",
                      ".ctor", "instance void(object, native int)");
  expectShapeAccepted("valuetype System.Runtime.InteropServices.HandleRef",
                      ".ctor",
                      "instance void(class System.Object, native int)");
  expectShapeAccepted("System.Action", "Invoke", "instance void()");
  expectShapeAccepted("System.Action", "BeginInvoke",
                      "instance class System.IAsyncResult(class "
                      "System.AsyncCallback, object)");
  expectShapeAccepted("Contoso.Callback", "Register",
                      "instance void(object, native int)");
  expectShapeAccepted("Contoso.Callback", "Register",
                      "void(object, native int)");
  const char *OtherCtors[] = {
      "instance void(object)",
      "instance void(native int)",
      "instance void(native int, object)",
      "instance void(object, native uint)",
      "instance void(string, native int)",
      "instance void(class System.String, native int)",
      "instance void(object, native int, int32)",
      "instance void(object[], native int)",
      "instance void(object, int64)",
  };
  for (const char *Sig : OtherCtors)
    expectShapeAccepted("Contoso.Callback", ".ctor", Sig);
  expectShapeAccepted("System.Runtime.InteropServices.Marshal",
                      "PtrToStructure",
                      "object(native int, class System.Type)");
  expectShapeAccepted("System.Runtime.InteropServices.Marshal",
                      "GetFunctionPointerForDelegate",
                      "native int(class System.Delegate)");
  expectShapeAccepted("System.Runtime.InteropServices.Marshal", "AllocHGlobal",
                      "native int(native int)");
}

TEST(CLIManagedSignatureTest, MalformedText) {
  const char *Texts[] = {
      "",
      " ",
      "int32",
      "int32(",
      "int32(int32",
      "int32(int32,)",
      "int32(,int32)",
      "int32(int32) extra",
      "(int32)",
      "frob(int32)",
      "int32(frob)",
      "void(void)",
      "int32(int32, void)",
      "instance",
      "instance instance void()",
      "native(int32)",
      "void(int32[)",
      "void(*)",
      "void([])",
  };
  for (const char *T : Texts)
    expectRejected(T);
}

//===----------------------------------------------------------------------===//
// The parsed fields of ManagedType
//===----------------------------------------------------------------------===//

/// The listing spelling of a primitive kind (plan 5.7.10), from the plan's
/// list, not from the header's table; null for the other kinds.
const char *primitiveName(ManagedType::TypeKind K) {
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
  default:
    return nullptr;
  }
}

/// A description of \p T built from its fields: a primitive's spelling,
/// 'class:<TypeName>', 'vt:<TypeName>', 'ptr(<element>)' and 'arr(<element>)'.
/// A field that does not fit the kind (a TypeName on an unnamed kind, an
/// Element on a kind without one, a missing Element or TypeName) shows as '!'.
std::string describe(const ManagedType &T) {
  using TK = ManagedType::TypeKind;
  switch (T.Kind) {
  case TK::Class:
  case TK::ValueType:
    if (T.Element || T.TypeName.empty())
      return "!";
    return (T.Kind == TK::Class ? "class:" : "vt:") + T.TypeName;
  case TK::Pointer:
  case TK::SZArray:
    if (!T.Element || !T.TypeName.empty())
      return "!";
    return (T.Kind == TK::Pointer ? "ptr(" : "arr(") + describe(*T.Element) +
           ")";
  default:
    if (T.Element || !T.TypeName.empty() || !primitiveName(T.Kind))
      return "!";
    return primitiveName(T.Kind);
  }
}

struct Fields {
  const char *Text;
  bool HasThis;
  bool IsField;
  const char *Return;
  std::vector<std::string> Params;
};

void expectFields(const Fields &F) {
  SCOPED_TRACE(F.Text);
  Expected<ManagedSignature> Sig = parseManagedSignature(F.Text);
  ASSERT_THAT_EXPECTED(Sig, Succeeded());
  EXPECT_EQ(Sig->HasThis, F.HasThis);
  EXPECT_EQ(Sig->IsField, F.IsField);
  EXPECT_EQ(describe(Sig->Return), F.Return);
  std::vector<std::string> Got;
  for (const ManagedType &P : Sig->Params)
    Got.push_back(describe(P));
  EXPECT_EQ(Got, F.Params);
}

TEST(CLIManagedSignatureTest, ParsedPrimitiveFields) {
  const Fields Cases[] = {
      {"void()", false, false, "void", {}},
      {"int32(int32)", false, false, "int32", {"int32"}},
      {"void(bool, char, int8, uint8, int16, uint16, int32, uint32, int64, "
       "uint64, float32, float64, native int, native uint, string, object)",
       false,
       false,
       "void",
       {"bool", "char", "int8", "uint8", "int16", "uint16", "int32", "uint32",
        "int64", "uint64", "float32", "float64", "native int", "native uint",
        "string", "object"}},
      {"native uint(native int)", false, false, "native uint", {"native int"}},
      {"object(string, string)", false, false, "object", {"string", "string"}},
      {"instance bool()", true, false, "bool", {}},
  };
  for (const Fields &F : Cases)
    expectFields(F);
}

TEST(CLIManagedSignatureTest, ParsedNamedTypeFields) {
  const Fields Cases[] = {
      {"class System.IO.Stream()", false, false, "class:System.IO.Stream", {}},
      {"valuetype System.DateTime()", false, false, "vt:System.DateTime", {}},
      {"instance void(string, valuetype System.IO.FileMode, valuetype "
       "System.IO.FileAccess, valuetype System.IO.FileShare, int32)",
       true,
       false,
       "void",
       {"string", "vt:System.IO.FileMode", "vt:System.IO.FileAccess",
        "vt:System.IO.FileShare", "int32"}},
      {"instance void(class System.IO.Stream, valuetype System.DateTime)",
       true,
       false,
       "void",
       {"class:System.IO.Stream", "vt:System.DateTime"}},
      {"valuetype System.Runtime.InteropServices.GCHandle(object)",
       false,
       false,
       "vt:System.Runtime.InteropServices.GCHandle",
       {"object"}},
      // Nested types keep their full 'Ns.Outer/Inner' name.
      {"string(valuetype System.Environment/SpecialFolder)",
       false,
       false,
       "string",
       {"vt:System.Environment/SpecialFolder"}},
      {"instance int32(class Ns.Outer/Inner)",
       true,
       false,
       "int32",
       {"class:Ns.Outer/Inner"}},
      {"class Ns.A/B/C()", false, false, "class:Ns.A/B/C", {}},
  };
  for (const Fields &F : Cases)
    expectFields(F);
}

TEST(CLIManagedSignatureTest, ParsedArrayAndPointerFields) {
  const Fields Cases[] = {
      {"void(uint8[], int32, native int, int32)",
       false,
       false,
       "void",
       {"arr(uint8)", "int32", "native int", "int32"}},
      {"void(void*, void*, int64, int64)",
       false,
       false,
       "void",
       {"ptr(void)", "ptr(void)", "int64", "int64"}},
      {"uint8**(int32)", false, false, "ptr(ptr(uint8))", {"int32"}},
      {"string[]()", false, false, "arr(string)", {}},
      {"object[](object[], string[])",
       false,
       false,
       "arr(object)",
       {"arr(object)", "arr(string)"}},
      {"class System.IO.Stream[](int32*)",
       false,
       false,
       "arr(class:System.IO.Stream)",
       {"ptr(int32)"}},
      {"void(valuetype System.DateTime*, valuetype System.DateTime[])",
       false,
       false,
       "void",
       {"ptr(vt:System.DateTime)", "arr(vt:System.DateTime)"}},
      {"valuetype Ns.Outer/Inner[](int32)",
       false,
       false,
       "arr(vt:Ns.Outer/Inner)",
       {"int32"}},
      // The suffixes apply left to right: an array of arrays, an array of
      // pointers.
      {"int32[][](int32*[])",
       false,
       false,
       "arr(arr(int32))",
       {"arr(ptr(int32))"}},
      {"instance string(uint8*, int32)",
       true,
       false,
       "string",
       {"ptr(uint8)", "int32"}},
  };
  for (const Fields &F : Cases)
    expectFields(F);
}

TEST(CLIManagedSignatureTest, ParsedFieldFields) {
  const Fields Cases[] = {
      {"field int64", false, true, "int64", {}},
      {"instance field int32", true, true, "int32", {}},
      {"field bool", false, true, "bool", {}},
      {"instance field valuetype System.DateTime",
       true,
       true,
       "vt:System.DateTime",
       {}},
      {"field class System.IO.TextWriter",
       false,
       true,
       "class:System.IO.TextWriter",
       {}},
      {"field string[]", false, true, "arr(string)", {}},
      {"instance field native uint", true, true, "native uint", {}},
      {"field valuetype Ns.Outer/Inner", false, true, "vt:Ns.Outer/Inner", {}},
      {"field uint8*", false, true, "ptr(uint8)", {}},
  };
  for (const Fields &F : Cases)
    expectFields(F);
}

//===----------------------------------------------------------------------===//
// One signature type (parseManagedSigType)
//===----------------------------------------------------------------------===//

TEST(CLIManagedSignatureTest, SigTypeAcceptReject) {
  const std::pair<const char *, const char *> Accepted[] = {
      {"int32", "int32"},
      {"native uint", "native uint"},
      {"string", "string"},
      {"object", "object"},
      {"class System.IO.Stream", "class:System.IO.Stream"},
      {"valuetype System.DateTime", "vt:System.DateTime"},
      {"valuetype System.Environment/SpecialFolder",
       "vt:System.Environment/SpecialFolder"},
      {"uint8**", "ptr(ptr(uint8))"},
      {"void*", "ptr(void)"},
      {"string[]", "arr(string)"},
      {"class System.IO.Stream[]", "arr(class:System.IO.Stream)"},
      {"valuetype System.DateTime[]", "arr(vt:System.DateTime)"},
  };
  for (const auto &[Text, Want] : Accepted) {
    SCOPED_TRACE(Text);
    Expected<ManagedType> T = parseManagedSigType(Text);
    ASSERT_THAT_EXPECTED(T, Succeeded());
    EXPECT_EQ(describe(*T), Want);
  }
  const char *Rejected[] = {
      "",
      "int32 int32",
      "int32)",
      "int32(int32)",
      "frob",
      "native",
      "System.String",
      "class",
      "valuetype",
      "class [mscorlib]System.String",
      "[mscorlib]System.String",
      "void[]",
      "class Ns.Outer+Inner",
      "class 1Ns.T",
      "class Ns..T",
      "class Ns.T.",
  };
  for (const char *Text : Rejected) {
    SCOPED_TRACE(Text);
    EXPECT_THAT_EXPECTED(parseManagedSigType(Text), Failed());
  }
  const char *Deferred[] = {
      "int32&",
      "!0",
      "!!0",
      "int32[,]",
      "typedref",
      "class System.Collections.Generic.List`1<int32>",
      "class System.Collections.Generic.List`1",
      "int32 modreq(class System.Runtime.CompilerServices.IsVolatile)",
      "method void*(int32)",
  };
  for (const char *Text : Deferred) {
    SCOPED_TRACE(Text);
    EXPECT_THAT_EXPECTED(
        parseManagedSigType(Text),
        FailedWithMessage(testing::HasSubstr("not supported for MSIL yet")));
  }
  // '`' starts a generic type's arity only after a valid name: after
  // 'class' or 'valuetype', '`Foo' is an invalid name (T-M2's reason, pinned
  // whole), in one type and in a signature, while 'List`1' stays generic.
  EXPECT_THAT_EXPECTED(parseManagedSigType("class `Foo"),
                       FailedWithMessage("'`Foo' is not a valid type name"));
  EXPECT_THAT_EXPECTED(parseManagedSigType("valuetype `Foo"),
                       FailedWithMessage("'`Foo' is not a valid type name"));
  EXPECT_THAT_EXPECTED(parseManagedSignature("void(class `Foo)"),
                       FailedWithMessage("'`Foo' is not a valid type name"));
  EXPECT_THAT_EXPECTED(
      parseManagedSigType("class List`1"),
      FailedWithMessage("generic types are not supported for MSIL yet"));
  // Generic arguments after a generic type's arity make a generic
  // instantiation, not a generic type (pinned whole): in a signature, after
  // 'class' and after 'valuetype', and in one type with a space before '<'.
  const char *GenericInst =
      "generic instantiations are not supported for MSIL yet";
  EXPECT_THAT_EXPECTED(
      parseManagedSignature(
          "void(class System.Collections.Generic.List`1<int32>)"),
      FailedWithMessage(GenericInst));
  EXPECT_THAT_EXPECTED(
      parseManagedSignature("void(valuetype System.Nullable`1<int32>)"),
      FailedWithMessage(GenericInst));
  EXPECT_THAT_EXPECTED(parseManagedSigType("class List`1 <int32>"),
                       FailedWithMessage(GenericInst));
}

//===----------------------------------------------------------------------===//
// TYPE (MA1), EXCEPTION_TYPE (MA5) and MEMBER (MA2)
//===----------------------------------------------------------------------===//

TEST(CLIManagedSignatureTest, TypeNameMA1) {
  // MA1's forms: a reference type by its full name (nested as
  // 'Ns.Outer/Inner'), 'valuetype Ns.Name', and '<elem>[]' whose element is
  // a listing primitive, 'string', 'object' or 'class Ns.T'.
  const std::pair<const char *, const char *> Accepted[] = {
      {"System.IO.FileStream", "class:System.IO.FileStream"},
      {"System.IDisposable", "class:System.IDisposable"},
      {"Ns.Outer/Inner", "class:Ns.Outer/Inner"},
      {"valuetype System.DateTime", "vt:System.DateTime"},
      {"valuetype System.Runtime.InteropServices.GCHandle",
       "vt:System.Runtime.InteropServices.GCHandle"},
      {"valuetype System.Environment/SpecialFolder",
       "vt:System.Environment/SpecialFolder"},
      {"uint8[]", "arr(uint8)"},
      {"int32[]", "arr(int32)"},
      {"native int[]", "arr(native int)"},
      {"string[]", "arr(string)"},
      {"object[]", "arr(object)"},
      {"class System.IO.Stream[]", "arr(class:System.IO.Stream)"},
      {"class Ns.Outer/Inner[]", "arr(class:Ns.Outer/Inner)"},
  };
  for (const auto &[Text, Want] : Accepted) {
    SCOPED_TRACE(Text);
    Expected<ManagedType> T = parseManagedTypeName(Text);
    ASSERT_THAT_EXPECTED(T, Succeeded());
    EXPECT_EQ(describe(*T), Want);
  }
  const char *Rejected[] = {
      "",
      // A reference type is named without 'class'.
      "class System.IO.Stream",
      // A listing primitive is a signature type, not a TYPE.
      "int32",
      "string",
      "object",
      // No pointer TYPE and no other array element (a named type's pointer
      // and a resolution scope are pinned whole below).
      "uint8*",
      "valuetype System.DateTime[]",
      "void[]",
      "uint8[][]",
      "System.IO.Stream[]",
      // Malformed names.
      "valuetype",
      "valuetype ",
      "Ns.Outer+Inner",
      "System..IO",
      "System.IO.",
      ".System",
      "1Ns.T",
      "Ns.T-1",
  };
  for (const char *Text : Rejected) {
    SCOPED_TRACE(Text);
    EXPECT_THAT_EXPECTED(parseManagedTypeName(Text), Failed());
  }
  const char *Deferred[] = {
      "System.Collections.Generic.List`1",
      "System.Collections.Generic.List`1<int32>",
      "int32[,]",
  };
  for (const char *Text : Deferred) {
    SCOPED_TRACE(Text);
    EXPECT_THAT_EXPECTED(
        parseManagedTypeName(Text),
        FailedWithMessage(testing::HasSubstr("not supported for MSIL yet")));
  }
  // A resolution scope, a byref and a pointer, each with its reason pinned
  // whole (T-M2's reason), for a reference type and after 'valuetype' alike:
  // TYPE names no scope (ASSEMBLY sets the scope of text output), byrefs are
  // a form of the fuller goal, and a pointer type has no members.
  const char *Scope = "a type is named without a resolution scope; the "
                      "assembly argument sets the scope of text output";
  const char *Byref = "byref types are not supported for MSIL yet";
  const char *Pointer =
      "a pointer type has no members; name the type that declares the member";
  const std::pair<const char *, const char *> ScopeByrefPointer[] = {
      {"[mscorlib]System.String", Scope},
      {"valuetype [mscorlib]System.TimeSpan", Scope},
      {"System.Int32&", Byref},
      {"valuetype System.Int32&", Byref},
      {"System.IO.Stream*", Pointer},
      {"valuetype System.Int32*", Pointer},
  };
  for (const auto &[Text, Message] : ScopeByrefPointer) {
    SCOPED_TRACE(Text);
    EXPECT_THAT_EXPECTED(parseManagedTypeName(Text),
                         FailedWithMessage(Message));
  }
  // '<' and '`' start generic arguments only after a valid name, with or
  // without 'valuetype'. A name with '<' or '`' anywhere else, such as the
  // compiler-generated '<Module>' and '<>f__AnonymousType0`2', is not
  // generic: it is an invalid name, reported with the name (T-M2's reason,
  // pinned whole; after 'valuetype', the name without it), while a generic
  // type or instantiation keeps its reason.
  const std::pair<const char *, const char *> InvalidNames[] = {
      {"<Module>", "'<Module>' is not a valid type name"},
      {"<PrivateImplementationDetails>",
       "'<PrivateImplementationDetails>' is not a valid type name"},
      {"Ns.<Module>", "'Ns.<Module>' is not a valid type name"},
      {"valuetype <Module>", "'<Module>' is not a valid type name"},
      {"`Foo", "'`Foo' is not a valid type name"},
      {"Ns.`1", "'Ns.`1' is not a valid type name"},
      {"<>f__AnonymousType0`2",
       "'<>f__AnonymousType0`2' is not a valid type name"},
      {"Ns.E/<Select>d__5`2", "'Ns.E/<Select>d__5`2' is not a valid type name"},
      {"valuetype `Foo", "'`Foo' is not a valid type name"},
  };
  for (const auto &[Text, Message] : InvalidNames) {
    SCOPED_TRACE(Text);
    EXPECT_THAT_EXPECTED(parseManagedTypeName(Text),
                         FailedWithMessage(Message));
  }
  const std::pair<const char *, const char *> GenericNames[] = {
      {"List`1", "generic types are not supported for MSIL yet"},
      {"List`1<int32>",
       "generic instantiations are not supported for MSIL yet"},
      {"Ns.Generic<int32>",
       "generic instantiations are not supported for MSIL yet"},
      {"System.Collections.Generic.List`1<int32>",
       "generic instantiations are not supported for MSIL yet"},
      {"valuetype Ns.Generic<int32>",
       "generic instantiations are not supported for MSIL yet"},
      {"valuetype System.Nullable<int32>",
       "generic instantiations are not supported for MSIL yet"},
      {"valuetype System.Nullable`1<int32>",
       "generic instantiations are not supported for MSIL yet"},
  };
  for (const auto &[Text, Message] : GenericNames) {
    SCOPED_TRACE(Text);
    EXPECT_THAT_EXPECTED(parseManagedTypeName(Text),
                         FailedWithMessage(Message));
  }
}

TEST(CLIManagedSignatureTest, CatchTypeMA5) {
  // MA5: EXCEPTION_TYPE is 'Ns.Name' of a reference type. (The catch-all
  // types of MS6 parse; Sema refuses them with T-M11.)
  const std::pair<const char *, const char *> Accepted[] = {
      {"System.IO.IOException", "class:System.IO.IOException"},
      {"System.IO.FileNotFoundException",
       "class:System.IO.FileNotFoundException"},
      {"System.Text.DecoderFallbackException",
       "class:System.Text.DecoderFallbackException"},
      {"Ns.Outer/InnerException", "class:Ns.Outer/InnerException"},
  };
  for (const auto &[Text, Want] : Accepted) {
    SCOPED_TRACE(Text);
    Expected<ManagedType> T = parseManagedCatchType(Text);
    ASSERT_THAT_EXPECTED(T, Succeeded());
    EXPECT_EQ(describe(*T), Want);
  }
  const char *Rejected[] = {
      "",
      "valuetype System.IO.IOException",
      "class System.IO.IOException",
      "System.Exception[]",
      "System.Exception*",
      "[mscorlib]System.Exception",
      "System.Collections.Generic.List`1<int32>",
      "System.Collections.Generic.List`1",
      "Ns.Outer+InnerException",
      "System..IOException",
      "1Ns.Exception",
  };
  for (const char *Text : Rejected) {
    SCOPED_TRACE(Text);
    EXPECT_THAT_EXPECTED(parseManagedCatchType(Text), Failed());
  }
  // A name with '<' or '`' that is not generic is an invalid name, as for
  // TYPE.
  EXPECT_THAT_EXPECTED(
      parseManagedCatchType("<Module>"),
      FailedWithMessage("'<Module>' is not a valid type name"));
  EXPECT_THAT_EXPECTED(
      parseManagedCatchType("<>f__AnonymousType0`2"),
      FailedWithMessage("'<>f__AnonymousType0`2' is not a valid type name"));
  // A resolution scope gets TYPE's reason; a byref and a pointer are not a
  // reference type named 'Ns.Name' (each pinned whole).
  EXPECT_THAT_EXPECTED(
      parseManagedCatchType("[mscorlib]System.IO.IOException"),
      FailedWithMessage("a type is named without a resolution scope; the "
                        "assembly argument sets the scope of text output"));
  const char *NotNsName =
      "an exception type is a reference type named 'Ns.Name'";
  EXPECT_THAT_EXPECTED(parseManagedCatchType("System.IO.IOException&"),
                       FailedWithMessage(NotNsName));
  EXPECT_THAT_EXPECTED(parseManagedCatchType("System.IO.IOException*"),
                       FailedWithMessage(NotNsName));
}

TEST(CLIManagedSignatureTest, MemberNameMA2) {
  // MA2: a method name, '.ctor', a property accessor by its method name, a
  // field name, and operators by their 'op_*' method names.
  const char *Accepted[] = {
      "Abs",       "Exists",      ".ctor",    "get_Length", "set_ExitCode",
      "Frequency", "op_Addition", "_private", "ToString",   "Write2",
  };
  for (const char *Text : Accepted) {
    SCOPED_TRACE(Text);
    EXPECT_THAT_ERROR(checkManagedMemberName(Text), Succeeded());
  }
  const char *Rejected[] = {
      "",      ".cctor", "1Abs",  "System.Math::Abs", "Math.Abs", "get Length",
      "add-x", "Abs()",  "ctor.",
  };
  for (const char *Text : Rejected) {
    SCOPED_TRACE(Text);
    EXPECT_THAT_ERROR(checkManagedMemberName(Text), Failed());
  }
  // Generic methods are not in MC-1.
  EXPECT_THAT_ERROR(
      checkManagedMemberName("Empty`1"),
      FailedWithMessage(testing::HasSubstr("not supported for MSIL yet")));
  EXPECT_THAT_ERROR(
      checkManagedMemberName("Empty<int32>"),
      FailedWithMessage("generic methods are not supported for MSIL yet"));
  // '<' and '`' start generic arguments only after a valid name: the
  // compiler-generated '<Clone>$', and '`Foo', are invalid names, not
  // generic methods.
  EXPECT_THAT_ERROR(checkManagedMemberName("<Clone>$"),
                    FailedWithMessage("'<Clone>$' is not a valid member name"));
  EXPECT_THAT_ERROR(checkManagedMemberName("`Foo"),
                    FailedWithMessage("'`Foo' is not a valid member name"));
}

//===----------------------------------------------------------------------===//
// MR1's list texts
//===----------------------------------------------------------------------===//

TEST(CLIManagedSignatureTest, ConvListRoundTrip) {
  // The text is the parameters' codes joined by ',', ';', then the result's
  // code (MR1); print(parse(x)) gives x back.
  const char *Texts[] = {
      ";void",
      ";obj",
      ";boxed",
      ";bool",
      "val;val",
      "val,val;val",
      "bool;void",
      "val;bool",
      "enum;val",
      "utf8;bool",
      "utf16;bool",
      "utf32;bool",
      "obj;bool",
      "obj;gch",
      "gch;val",
      "boxed,boxed;val",
      "val;boxed",
      "this-obj,obj,val,val;void",
      "this-val;val",
      "this-gch;void",
      "utf8,err;bool",
      "this-obj,val,err;val",
      "utf8,enum,enum,enum,val,err;obj",
  };
  for (const char *Text : Texts) {
    SCOPED_TRACE(Text);
    Expected<ManagedConvList> L = parseManagedConvList(Text);
    ASSERT_THAT_EXPECTED(L, Succeeded());
    EXPECT_EQ(printManagedConvList(L->Params, L->Result), Text);
  }
  // The parsed codes of MR1's example, in C parameter order.
  Expected<ManagedConvList> L =
      parseManagedConvList("utf8,enum,enum,enum,val,err;obj");
  ASSERT_THAT_EXPECTED(L, Succeeded());
  const ManagedConv Want[] = {ManagedConv::UTF8, ManagedConv::Enum,
                              ManagedConv::Enum, ManagedConv::Enum,
                              ManagedConv::Val,  ManagedConv::Err};
  EXPECT_EQ(ArrayRef<ManagedConv>(L->Params), ArrayRef<ManagedConv>(Want));
  EXPECT_EQ(L->Result, ManagedConv::Obj);
  Expected<ManagedConvList> Empty = parseManagedConvList(";void");
  ASSERT_THAT_EXPECTED(Empty, Succeeded());
  EXPECT_TRUE(Empty->Params.empty());
  EXPECT_EQ(Empty->Result, ManagedConv::Void);
  // The other codes' spellings, printed from the enumerators.
  const ManagedConv Others[] = {ManagedConv::ThisVal, ManagedConv::UTF16,
                                ManagedConv::UTF32,   ManagedConv::Boxed,
                                ManagedConv::GCH,     ManagedConv::Bool};
  EXPECT_EQ(printManagedConvList(Others, ManagedConv::Void),
            "this-val,utf16,utf32,boxed,gch,bool;void");
}

TEST(CLIManagedSignatureTest, ConvListMalformed) {
  const char *Texts[] = {
      "",
      ";",
      "val",
      "val,val",
      // The result's code is missing or unknown.
      "val;",
      "val;frob",
      // An unknown or empty parameter code.
      "frob;val",
      ",val;val",
      "val,;val",
      "val,,val;val",
      // Two results.
      "val;val;val",
      // 'err' (msil_catch's trailing 'int *') is the last parameter only.
      "err,val;val",
      "this-obj,err,val;void",
      // A 'this' handle is the first parameter only.
      "val,this-obj;void",
      "val,this-val;val",
      "val,this-gch;void",
      // 'void' is a result only.
      "void;void",
      "val,void;val",
      // A string, a 'this' handle or 'err' is never a result.
      "val;utf8",
      "val;utf16",
      "val;utf32",
      "val;this-obj",
      "val;this-val",
      "val;this-gch",
      "val;err",
      // Spelling is exact.
      "VAL;val",
      " val;val",
  };
  for (const char *Text : Texts) {
    SCOPED_TRACE(Text);
    EXPECT_THAT_EXPECTED(parseManagedConvList(Text), Failed());
  }
}

TEST(CLIManagedSignatureTest, CatchListRoundTrip) {
  // The text is '<type>=<code>' per entry in the order written, joined by
  // ';' (MR1); "" is the empty list.
  const char *const Three =
      "System.IO.FileNotFoundException=2;System.IO.DirectoryNotFoundException="
      "2;System.IO.IOException=5";
  const char *Texts[] = {
      "",
      "System.IO.IOException=5",
      Three,
      "Ns.Outer/InnerException=2147483647",
      "System.Text.DecoderFallbackException=84;System.IO.IOException=1",
  };
  for (const char *Text : Texts) {
    SCOPED_TRACE(Text);
    Expected<SmallVector<ManagedCatch, 4>> L = parseManagedCatchList(Text);
    ASSERT_THAT_EXPECTED(L, Succeeded());
    EXPECT_EQ(printManagedCatchList(*L), Text);
  }
  Expected<SmallVector<ManagedCatch, 4>> Empty = parseManagedCatchList("");
  ASSERT_THAT_EXPECTED(Empty, Succeeded());
  EXPECT_TRUE(Empty->empty());
  // The entries keep their order and codes.
  Expected<SmallVector<ManagedCatch, 4>> L = parseManagedCatchList(Three);
  ASSERT_THAT_EXPECTED(L, Succeeded());
  ASSERT_EQ(L->size(), 3u);
  EXPECT_EQ((*L)[0].Type, "System.IO.FileNotFoundException");
  EXPECT_EQ((*L)[0].Code, 2);
  EXPECT_EQ((*L)[1].Type, "System.IO.DirectoryNotFoundException");
  EXPECT_EQ((*L)[1].Code, 2);
  EXPECT_EQ((*L)[2].Type, "System.IO.IOException");
  EXPECT_EQ((*L)[2].Code, 5);
  Expected<SmallVector<ManagedCatch, 4>> Max =
      parseManagedCatchList("Ns.Outer/InnerException=2147483647");
  ASSERT_THAT_EXPECTED(Max, Succeeded());
  ASSERT_EQ(Max->size(), 1u);
  EXPECT_EQ((*Max)[0].Code, 2147483647);
}

TEST(CLIManagedSignatureTest, CatchListMalformed) {
  const char *Texts[] = {
      // An entry without '=<code>'.
      "System.IO.IOException",
      "System.IO.FileNotFoundException=2;System.IO.IOException",
      "System.IO.IOException=",
      // A code outside 1 to INT32_MAX, or not a number.
      "System.IO.IOException=0",
      "System.IO.IOException=-1",
      "System.IO.IOException=2147483648",
      "System.IO.IOException=five",
      // A type that is missing or not 'Ns.Name' of a reference type.
      "=5",
      "valuetype System.IO.IOException=5",
      "System.Exception[]=5",
      "Ns.Outer+Inner=5",
      // Empty entries.
      ";",
      "System.IO.IOException=5;",
      ";System.IO.IOException=5",
      "System.IO.IOException=5;;System.IO.EndOfStreamException=6",
  };
  for (const char *Text : Texts) {
    SCOPED_TRACE(Text);
    EXPECT_THAT_EXPECTED(parseManagedCatchList(Text), Failed());
  }
}

TEST(CLIManagedSignatureTest, ManagedCallSymbol) {
  // External declarations: '__msil_mc.<symbol>'.
  EXPECT_TRUE(isManagedCallName("__msil_mc.m_abs"));
  EXPECT_EQ(getManagedCallSymbol("__msil_mc.m_abs"), "m_abs");
  EXPECT_EQ(getManagedCallSymbol("__msil_mc._ZN2ns1gEi"), "_ZN2ns1gEi");
  EXPECT_EQ(getManagedCallSymbol("__msil_mc.?ov@@YA_J_J@Z"), "?ov@@YA_J_J@Z");
  EXPECT_FALSE(isManagedCallName("m_abs"));
  EXPECT_FALSE(isManagedCallName("__msil_mc"));
  // Internal linkage: '__msil_mc.<symbol>.__uniq.<n>', the postfix of
  // llvm::getUniqueInternalLinkagePostfix (decimal digits).
  EXPECT_TRUE(isManagedCallName("__msil_mc.get.__uniq.123"));
  EXPECT_EQ(getManagedCallSymbol("__msil_mc.get.__uniq.123"), "get");
  EXPECT_EQ(getManagedCallSymbol("__msil_mc.m_static.__uniq."
                                 "155253446432126233455893036013622474557"),
            "m_static");
  EXPECT_EQ(getManagedCallSymbol(
                "__msil_mc._ZN12_GLOBAL__N_16anon_fEii.__uniq.987654321"),
            "_ZN12_GLOBAL__N_16anon_fEii");
  EXPECT_EQ(
      getManagedCallSymbol("__msil_mc.?anon_f@?A0x1a2b3c4d@@YAHHH@Z.__uniq.42"),
      "?anon_f@?A0x1a2b3c4d@@YAHHH@Z");
  EXPECT_EQ(getManagedCallSymbol("__msil_mc.get.__uniq.42"), "get");
  // Only a postfix of one or more decimal digits at the end is stripped.
  EXPECT_EQ(getManagedCallSymbol("__msil_mc.get.__uniq."), "get.__uniq.");
  EXPECT_EQ(getManagedCallSymbol("__msil_mc.get.__uniq.12a"), "get.__uniq.12a");
}

// 5.20.4 as amended again: an internal-linkage declaration's postfix hashes
// the strings of its !msil.managed.calls operands 1-6 and its function type,
// so that equal names mean interchangeable declarations.
bool isUniqPostfix(StringRef Postfix) {
  if (!Postfix.consume_front(".__uniq.") || Postfix.empty())
    return false;
  return all_of(Postfix, isDigit);
}

TEST(CLIManagedSignatureTest, ManagedCallUniqPostfix) {
  // TYPE, MEMBER, SIGNATURE, ASSEMBLY, conversions, catch list, C type.
  const StringRef Abs[] = {"System.Math", "Abs", "int32(int32)", "",
                           "val;val",     "",    "int(int)"};
  const StringRef Sign[] = {"System.Math", "Sign", "int32(int32)", "",
                            "val;val",     "",     "int(int)"};
  const StringRef Short[] = {"System.Math", "Abs", "int16(int16)", "",
                             "val;val",     "",    "short(short)"};
  const StringRef UShort[] = {"System.Math",
                              "Abs",
                              "int16(int16)",
                              "",
                              "val;val",
                              "",
                              "unsigned short(unsigned short)"};
  std::string P = getManagedCallUniqPostfix(Abs);
  EXPECT_TRUE(isUniqPostfix(P)) << P;
  // Equal lists, built apart, give equal postfixes.
  std::vector<std::string> Copy;
  for (StringRef Field : Abs)
    Copy.push_back(Field.str());
  SmallVector<StringRef, 7> CopyRefs(Copy.begin(), Copy.end());
  EXPECT_EQ(getManagedCallUniqPostfix(CopyRefs), P);
  // A different member, or only a different C type, gives another postfix.
  std::string SignP = getManagedCallUniqPostfix(Sign);
  EXPECT_TRUE(isUniqPostfix(SignP)) << SignP;
  EXPECT_NE(SignP, P);
  EXPECT_NE(getManagedCallUniqPostfix(Short),
            getManagedCallUniqPostfix(UShort));
  // The fields are hashed as a list, not as their concatenation.
  std::string ABC = getManagedCallUniqPostfix({"ab", "c"});
  std::string ABC2 = getManagedCallUniqPostfix({"a", "bc"});
  EXPECT_TRUE(isUniqPostfix(ABC)) << ABC;
  EXPECT_TRUE(isUniqPostfix(ABC2)) << ABC2;
  EXPECT_NE(ABC, ABC2);
  EXPECT_NE(getManagedCallUniqPostfix({"", "a"}),
            getManagedCallUniqPostfix({"a", ""}));
  EXPECT_NE(getManagedCallUniqPostfix({"a"}),
            getManagedCallUniqPostfix({"a", ""}));
  // The digits: the unsigned decimal value of the 128-bit MD5 of "2:ab1:c"
  // (each field's byte length, ':' and its bytes), as the header documents.
  EXPECT_EQ(ABC, ".__uniq.115020388960338066259069194892983544220");
  // getManagedCallSymbol strips the postfix again.
  std::string Name = "__msil_mc.get" + P;
  EXPECT_EQ(getManagedCallSymbol(Name), "get");
}

// 5.20.6 MW2 as amended (section 12, 2026-11-04): a resolution scope in ILAsm
// text is printed bare when it is an ILAsm dotted name and single-quoted as
// MSIL::printQuotedName prints names otherwise (a space, ']', a quote, a
// reserved word, ...).
std::string scopeText(StringRef Scope) {
  std::string S;
  raw_string_ostream OS(S);
  printILAsmScope(OS, Scope);
  return S;
}

std::string typeText(StringRef Type, StringRef Scope) {
  Expected<ManagedType> T = parseManagedSigType(Type);
  if (!T) {
    consumeError(T.takeError());
    return "<parse error>";
  }
  std::string S;
  raw_string_ostream OS(S);
  T->print(OS, Scope);
  return S;
}

TEST(CLIManagedSignatureTest, ILAsmScope) {
  const std::pair<const char *, const char *> Cases[] = {
      {"mscorlib", "[mscorlib]"}, {"System.Runtime", "[System.Runtime]"},
      {"my lib", "['my lib']"},   {"a]b", "['a]b']"},
      {"int32", "['int32']"},     {"System.int32", "[System.int32]"},
  };
  for (const auto &[Scope, Text] : Cases) {
    SCOPED_TRACE(Scope);
    EXPECT_EQ(scopeText(Scope), Text);
  }

  for (const char *Name :
       {"mscorlib", "System.Runtime", "System.int32", "_a.$b.@c", "a1?`"}) {
    SCOPED_TRACE(Name);
    EXPECT_TRUE(isILAsmBareDottedName(Name));
  }
  for (const char *Name : {"my lib", "a]b", "it's", "int32", "ldc.i4", "value",
                           "v4.0", "a.", ".a", "a..b", "1abc", "a-b", ""}) {
    SCOPED_TRACE(Name);
    EXPECT_FALSE(isILAsmBareDottedName(Name));
  }
}

TEST(CLIManagedSignatureTest, TypePrintWithScope) {
  EXPECT_EQ(typeText("class Widget", "my lib"), "class ['my lib']Widget");
  EXPECT_EQ(typeText("valuetype Ns.Point", "my lib"),
            "valuetype ['my lib']Ns.Point");
  EXPECT_EQ(typeText("class Widget[]", "my lib"), "class ['my lib']Widget[]");
  EXPECT_EQ(typeText("valuetype Ns.Point*", "my lib"),
            "valuetype ['my lib']Ns.Point*");
  EXPECT_EQ(typeText("class Ns.Outer/Inner", "System.Runtime"),
            "class [System.Runtime]Ns.Outer/Inner");
  EXPECT_EQ(typeText("class Widget", "int32"), "class ['int32']Widget");
  // No scope: the listing text; primitives never take a scope.
  EXPECT_EQ(typeText("class Widget[]", ""), "class Widget[]");
  EXPECT_EQ(typeText("int32", "my lib"), "int32");
  EXPECT_EQ(typeText("string[]", "my lib"), "string[]");
}

std::string sigText(StringRef Sig, StringRef Scope) {
  Expected<ManagedSignature> S = parseManagedSignature(Sig);
  if (!S) {
    consumeError(S.takeError());
    return "<parse error>";
  }
  std::string Text;
  raw_string_ostream OS(Text);
  S->print(OS, Scope);
  return Text;
}

// Each '/'-separated part of a class or value type name that is not an ILAsm
// dotted name is single-quoted the way a scope is, so that ilasm reads it as
// one name ('value' and 'int32' are reserved words of its lexer); a dotted
// name prints unchanged.
TEST(CLIManagedSignatureTest, TypePrintQuotesNames) {
  EXPECT_EQ(typeText("valuetype value", ""), "valuetype 'value'");
  EXPECT_EQ(typeText("class Ns.Outer/value", ""), "class Ns.Outer/'value'");
  EXPECT_EQ(typeText("class value/Inner", ""), "class 'value'/Inner");
  EXPECT_EQ(typeText("class int32/value", ""), "class 'int32'/'value'");
  // With a scope, and through pointers and arrays.
  EXPECT_EQ(typeText("valuetype value", "my lib"),
            "valuetype ['my lib']'value'");
  EXPECT_EQ(typeText("class Ns.Outer/value", "mscorlib"),
            "class [mscorlib]Ns.Outer/'value'");
  EXPECT_EQ(typeText("valuetype value*", ""), "valuetype 'value'*");
  EXPECT_EQ(typeText("class Ns.Outer/value[]", "System.Runtime"),
            "class [System.Runtime]Ns.Outer/'value'[]");
  // A part with a non-ASCII byte is quoted whole, its bytes raw.
  EXPECT_EQ(typeText("class Ns.Caf\xC3\xA9", ""), "class 'Ns.Caf\xC3\xA9'");
  EXPECT_EQ(typeText("valuetype Ns.Outer/R\xC3\xA9", "mscorlib"),
            "valuetype [mscorlib]Ns.Outer/'R\xC3\xA9'");
  // A signature prints its types the same way.
  EXPECT_EQ(sigText("void(valuetype Ns.Outer/value)", "mscorlib"),
            "void(valuetype [mscorlib]Ns.Outer/'value')");
  EXPECT_EQ(sigText("instance class value(int32)", ""),
            "instance class 'value'(int32)");
  // Dotted names print unchanged.
  EXPECT_EQ(typeText("valuetype Ns.Point", ""), "valuetype Ns.Point");
  EXPECT_EQ(typeText("class System.IO.Stream", ""), "class System.IO.Stream");
  EXPECT_EQ(typeText("class Ns.Outer/Inner", ""), "class Ns.Outer/Inner");
  EXPECT_EQ(typeText("valuetype System.int32", ""), "valuetype System.int32");
  EXPECT_EQ(typeText("class Widget", "my lib"), "class ['my lib']Widget");
  EXPECT_EQ(sigText("void(valuetype Ns.Outer/Inner)", ""),
            "void(valuetype Ns.Outer/Inner)");
}

TEST(CLIManagedSignatureTest, PrintManagedCallAttributes) {
  // None of the three: no eighth operand text at all.
  EXPECT_EQ(printManagedCallAttributes(false, std::nullopt, std::nullopt), "");
  // Each alone, with the priorities given.
  EXPECT_EQ(printManagedCallAttributes(true, std::nullopt, std::nullopt),
            "dllexport");
  EXPECT_EQ(printManagedCallAttributes(false, 65535, std::nullopt),
            "constructor(65535)");
  EXPECT_EQ(printManagedCallAttributes(false, std::nullopt, 101),
            "destructor(101)");
  // Joined with ',' and no spaces, in the order dllexport, constructor,
  // destructor.
  EXPECT_EQ(printManagedCallAttributes(true, 300, std::nullopt),
            "dllexport,constructor(300)");
  EXPECT_EQ(printManagedCallAttributes(true, 300, 500),
            "dllexport,constructor(300),destructor(500)");
}

} // namespace
