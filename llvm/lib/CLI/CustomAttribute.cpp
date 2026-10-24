//===- CustomAttribute.cpp - ECMA-335 custom attributes -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The custom attribute value blob of II.23.3:
//
//   CustomAttrib ::= Prolog (0x0001) FixedArg* NumNamed (u16) NamedArg*
//   FixedArg     ::= Elem | NumElem (u32, 0xFFFFFFFF = null) Elem*
//   NamedArg     ::= (FIELD 0x53 | PROPERTY 0x54) FieldOrPropType
//                    FieldOrPropName (SerString) FixedArg
//   Elem         ::= a scalar | SerString (string, Type)
//                  | FieldOrPropType Elem (a boxed 'object')
//   FieldOrPropType ::= BOOLEAN..R8 | STRING | TYPE (0x50)
//                  | TAGGED_OBJECT (0x51) | ENUM (0x55) SerString
//                  | SZARRAY (0x1D) FieldOrPropType
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/CustomAttribute.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/Blob.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;
using namespace llvm::cli;

#define DEBUG_TYPE "cli-customattribute"

static constexpr uint16_t CAProlog = 0x0001;
static constexpr uint32_t CANullArray = 0xFFFFFFFF;
static constexpr uint8_t SerializationSZArray = ELEMENT_TYPE_SZARRAY;

//===----------------------------------------------------------------------===//
// CAValue
//===----------------------------------------------------------------------===//

CAValue::CAValue(const CAValue &Other)
    : K(Other.K), Bits(Other.Bits), Str(Other.Str),
      EnumUnderlying(Other.EnumUnderlying), EnumTypeName(Other.EnumTypeName),
      BoxedValue(Other.BoxedValue ? std::make_unique<CAValue>(*Other.BoxedValue)
                                  : nullptr),
      Elements(Other.Elements) {}

CAValue &CAValue::operator=(const CAValue &Other) {
  if (this != &Other) {
    CAValue Copy(Other);
    *this = std::move(Copy);
  }
  return *this;
}

CAValue::~CAValue() = default;

namespace llvm {
namespace cli {
bool operator==(const CAValue &L, const CAValue &R) {
  if (L.K != R.K)
    return false;
  switch (L.K) {
  case CAValue::Kind::String:
  case CAValue::Kind::Type:
    return L.Str == R.Str;
  case CAValue::Kind::Enum:
    return L.Bits == R.Bits && L.EnumUnderlying == R.EnumUnderlying &&
           L.EnumTypeName == R.EnumTypeName;
  case CAValue::Kind::Boxed:
    if (!L.BoxedValue || !R.BoxedValue)
      return !L.BoxedValue && !R.BoxedValue;
    return *L.BoxedValue == *R.BoxedValue;
  case CAValue::Kind::Array:
    if (!(L.Elements == R.Elements))
      return false;
    // A non-empty array's element type is the kind of its elements. An empty
    // or null one keeps it in its own fields, which the encoder writes for
    // named arguments and boxed values.
    if (L.Elements && !L.Elements->empty())
      return true;
    return L.EnumUnderlying == R.EnumUnderlying &&
           (L.EnumUnderlying !=
                static_cast<ElementType>(SERIALIZATION_TYPE_ENUM) ||
            L.EnumTypeName == R.EnumTypeName);
  default:
    return L.Bits == R.Bits;
  }
}
} // namespace cli
} // namespace llvm

//===----------------------------------------------------------------------===//
// Types of values
//===----------------------------------------------------------------------===//

namespace {
/// The type of a value that is not an array: a parameter type, a
/// FieldOrPropType, or the element type of either.
struct ElemType {
  CAValue::Kind K = CAValue::Kind::I4;
  /// Enum: the underlying integer type.
  ElementType Underlying = ELEMENT_TYPE_I4;
  /// Enum: the serialized type name (named arguments and boxed values).
  std::string EnumName;
};

/// The type of a fixed argument, a named argument or a boxed value.
struct ArgType {
  bool IsArray = false;
  ElemType Elem;
};
} // namespace

static std::optional<CAValue::Kind> getScalarKind(uint8_t ET) {
  switch (ET) {
  case ELEMENT_TYPE_BOOLEAN:
    return CAValue::Kind::Bool;
  case ELEMENT_TYPE_CHAR:
    return CAValue::Kind::Char;
  case ELEMENT_TYPE_I1:
    return CAValue::Kind::I1;
  case ELEMENT_TYPE_U1:
    return CAValue::Kind::U1;
  case ELEMENT_TYPE_I2:
    return CAValue::Kind::I2;
  case ELEMENT_TYPE_U2:
    return CAValue::Kind::U2;
  case ELEMENT_TYPE_I4:
    return CAValue::Kind::I4;
  case ELEMENT_TYPE_U4:
    return CAValue::Kind::U4;
  case ELEMENT_TYPE_I8:
    return CAValue::Kind::I8;
  case ELEMENT_TYPE_U8:
    return CAValue::Kind::U8;
  case ELEMENT_TYPE_R4:
    return CAValue::Kind::R4;
  case ELEMENT_TYPE_R8:
    return CAValue::Kind::R8;
  default:
    return std::nullopt;
  }
}

/// The element type of a scalar kind (Bool..R8).
static ElementType getScalarElementType(CAValue::Kind K) {
  switch (K) {
  case CAValue::Kind::Bool:
    return ELEMENT_TYPE_BOOLEAN;
  case CAValue::Kind::Char:
    return ELEMENT_TYPE_CHAR;
  case CAValue::Kind::I1:
    return ELEMENT_TYPE_I1;
  case CAValue::Kind::U1:
    return ELEMENT_TYPE_U1;
  case CAValue::Kind::I2:
    return ELEMENT_TYPE_I2;
  case CAValue::Kind::U2:
    return ELEMENT_TYPE_U2;
  case CAValue::Kind::I4:
    return ELEMENT_TYPE_I4;
  case CAValue::Kind::U4:
    return ELEMENT_TYPE_U4;
  case CAValue::Kind::I8:
    return ELEMENT_TYPE_I8;
  case CAValue::Kind::U8:
    return ELEMENT_TYPE_U8;
  case CAValue::Kind::R4:
    return ELEMENT_TYPE_R4;
  case CAValue::Kind::R8:
    return ELEMENT_TYPE_R8;
  default:
    llvm_unreachable("not a scalar kind");
  }
}

static bool isScalarKind(CAValue::Kind K) { return K <= CAValue::Kind::R8; }

/// The integer types an enum may have as its underlying type.
static bool isEnumUnderlying(ElementType ET) {
  return ET == ELEMENT_TYPE_BOOLEAN || ET == ELEMENT_TYPE_CHAR ||
         (ET >= ELEMENT_TYPE_I1 && ET <= ELEMENT_TYPE_U8);
}

/// The size of a scalar of element type ET (BOOLEAN..R8).
static unsigned getScalarSize(ElementType ET) {
  switch (ET) {
  case ELEMENT_TYPE_BOOLEAN:
  case ELEMENT_TYPE_I1:
  case ELEMENT_TYPE_U1:
    return 1;
  case ELEMENT_TYPE_CHAR:
  case ELEMENT_TYPE_I2:
  case ELEMENT_TYPE_U2:
    return 2;
  case ELEMENT_TYPE_I4:
  case ELEMENT_TYPE_U4:
  case ELEMENT_TYPE_R4:
    return 4;
  case ELEMENT_TYPE_I8:
  case ELEMENT_TYPE_U8:
  case ELEMENT_TYPE_R8:
    return 8;
  default:
    llvm_unreachable("not a scalar element type");
  }
}

static StringRef getKindName(CAValue::Kind K) {
  switch (K) {
  case CAValue::Kind::Bool:
    return "bool";
  case CAValue::Kind::Char:
    return "char";
  case CAValue::Kind::I1:
    return "int8";
  case CAValue::Kind::U1:
    return "uint8";
  case CAValue::Kind::I2:
    return "int16";
  case CAValue::Kind::U2:
    return "uint16";
  case CAValue::Kind::I4:
    return "int32";
  case CAValue::Kind::U4:
    return "uint32";
  case CAValue::Kind::I8:
    return "int64";
  case CAValue::Kind::U8:
    return "uint64";
  case CAValue::Kind::R4:
    return "float32";
  case CAValue::Kind::R8:
    return "float64";
  case CAValue::Kind::String:
    return "string";
  case CAValue::Kind::Type:
    return "Type";
  case CAValue::Kind::Enum:
    return "enum";
  case CAValue::Kind::Boxed:
    return "boxed";
  case CAValue::Kind::Array:
    return "array";
  }
  llvm_unreachable("bad CAValue kind");
}

/// The FieldOrPropType code of an element type (an ElementType, or a
/// SerializationTypeCode cast to one), which an Array value records in its
/// EnumUnderlying.
static ElementType getElemCode(const ElemType &T) {
  if (isScalarKind(T.K))
    return getScalarElementType(T.K);
  switch (T.K) {
  case CAValue::Kind::String:
    return ELEMENT_TYPE_STRING;
  case CAValue::Kind::Type:
    return static_cast<ElementType>(SERIALIZATION_TYPE_TYPE);
  case CAValue::Kind::Enum:
    return static_cast<ElementType>(SERIALIZATION_TYPE_ENUM);
  case CAValue::Kind::Boxed:
    return static_cast<ElementType>(SERIALIZATION_TYPE_TAGGED_OBJECT);
  default:
    llvm_unreachable("arrays have no element code");
  }
}

static std::string printParamType(const TypeSig &T) {
  std::string S;
  raw_string_ostream OS(S);
  printTypeSig(OS, T, nullptr);
  return S;
}

/// The type of a constructor parameter (II.23.3): BOOLEAN..R8, STRING,
/// CLASS (System.Type, the only class allowed), OBJECT (boxed), VALUETYPE
/// (an enum) and SZARRAY of those; modifiers are ignored. Returns Resolve's
/// error unchanged; any other problem leaves a message in Why.
static Error getParamType(const TypeSig &P, EnumTypeResolver Resolve,
                          ArgType &T, std::string &Why) {
  T = ArgType();
  Why.clear();
  const TypeSig *E = &P;
  if (P.getElementType() == ELEMENT_TYPE_SZARRAY) {
    T.IsArray = true;
    E = &P.getOperands()[0];
  }
  ElementType ET = E->getElementType();
  if (std::optional<CAValue::Kind> K = getScalarKind(ET)) {
    T.Elem.K = *K;
    return Error::success();
  }
  switch (ET) {
  case ELEMENT_TYPE_STRING:
    T.Elem.K = CAValue::Kind::String;
    return Error::success();
  case ELEMENT_TYPE_CLASS:
    T.Elem.K = CAValue::Kind::Type;
    return Error::success();
  case ELEMENT_TYPE_OBJECT:
    T.Elem.K = CAValue::Kind::Boxed;
    return Error::success();
  case ELEMENT_TYPE_VALUETYPE: {
    if (!Resolve) {
      Why = "parameter type '" + printParamType(P) +
            "' is an enum and no enum type resolver was given";
      return Error::success();
    }
    Expected<ElementType> U = Resolve(E->getTypeRef());
    if (!U)
      return U.takeError();
    if (!isEnumUnderlying(*U)) {
      Why = "enum parameter type '" + printParamType(P) +
            "' has the underlying element type 0x" + utohexstr(*U) +
            ", not an integer type";
      return Error::success();
    }
    T.Elem.K = CAValue::Kind::Enum;
    T.Elem.Underlying = *U;
    return Error::success();
  }
  default:
    Why = "unsupported parameter type '" + printParamType(P) + "'";
    return Error::success();
  }
}

//===----------------------------------------------------------------------===//
// Encoder
//===----------------------------------------------------------------------===//

namespace {
class CAEncoder {
public:
  explicit CAEncoder(SmallVectorImpl<uint8_t> &Out) : W(Out) {}

  bool fail(const Twine &Cause) {
    if (!Msg)
      Msg = (Where + ": " + Cause).str();
    return false;
  }

  void writeScalar(ElementType ET, uint64_t Bits) {
    switch (getScalarSize(ET)) {
    case 1:
      W.writeU8(static_cast<uint8_t>(Bits));
      return;
    case 2:
      W.writeU16(static_cast<uint16_t>(Bits));
      return;
    case 4:
      W.writeU32(static_cast<uint32_t>(Bits));
      return;
    default:
      W.writeU64(Bits);
      return;
    }
  }

  bool expectKind(const CAValue &V, CAValue::Kind K) {
    if (V.K == K)
      return true;
    return fail("expected a value of kind " + getKindName(K) + ", found " +
                getKindName(V.K));
  }

  bool writeElem(const ElemType &T, const CAValue &V) {
    if (!expectKind(V, T.K))
      return false;
    if (isScalarKind(T.K)) {
      writeScalar(getScalarElementType(T.K), V.Bits);
      return true;
    }
    switch (T.K) {
    case CAValue::Kind::String:
    case CAValue::Kind::Type:
      if (V.Str)
        W.writeSerString(StringRef(*V.Str));
      else
        W.writeSerString(std::nullopt);
      return true;
    case CAValue::Kind::Enum:
      writeScalar(T.Underlying, V.Bits);
      return true;
    case CAValue::Kind::Boxed:
      if (!V.BoxedValue)
        return fail("boxed value without a value");
      if (V.BoxedValue->K == CAValue::Kind::Boxed)
        return fail("boxed value that is itself boxed");
      return writeTagged(*V.BoxedValue);
    default:
      llvm_unreachable("arrays are not elements");
    }
  }

  bool writeFixedArg(const ArgType &T, const CAValue &V) {
    if (!T.IsArray)
      return writeElem(T.Elem, V);
    if (!expectKind(V, CAValue::Kind::Array))
      return false;
    if (!V.Elements) {
      W.writeU32(CANullArray);
      return true;
    }
    assert(V.Elements->size() < CANullArray && "array too long");
    W.writeU32(V.Elements->size());
    for (const CAValue &E : *V.Elements)
      if (!writeElem(T.Elem, E))
        return false;
    return true;
  }

  /// The type of a value that is not an array, from its kind.
  bool getElemTypeOf(const CAValue &V, ElemType &T) {
    T = ElemType();
    T.K = V.K;
    switch (V.K) {
    case CAValue::Kind::Array:
      return fail("array of arrays");
    case CAValue::Kind::Enum:
      if (!isEnumUnderlying(V.EnumUnderlying))
        return fail("enum value with the underlying element type 0x" +
                    utohexstr(V.EnumUnderlying) + ", not an integer type");
      if (V.EnumTypeName.empty())
        return fail("enum value without an enum type name");
      T.Underlying = V.EnumUnderlying;
      T.EnumName = V.EnumTypeName;
      return true;
    default:
      return true;
    }
  }

  /// The type that a named argument or boxed value writes for V.
  bool getArgTypeOf(const CAValue &V, ArgType &T) {
    T = ArgType();
    if (V.K != CAValue::Kind::Array)
      return getElemTypeOf(V, T.Elem);
    T.IsArray = true;
    if (V.Elements && !V.Elements->empty()) {
      if (!getElemTypeOf(V.Elements->front(), T.Elem))
        return false;
      for (const CAValue &E : *V.Elements)
        if (E.K != T.Elem.K || (E.K == CAValue::Kind::Enum &&
                                (E.EnumTypeName != T.Elem.EnumName ||
                                 E.EnumUnderlying != T.Elem.Underlying)))
          return fail("array elements of different types");
      return true;
    }
    // An empty or null array: the element type is in the array's fields.
    uint8_t Code = V.EnumUnderlying;
    if (std::optional<CAValue::Kind> K = getScalarKind(Code)) {
      T.Elem.K = *K;
      return true;
    }
    switch (Code) {
    case ELEMENT_TYPE_STRING:
      T.Elem.K = CAValue::Kind::String;
      return true;
    case SERIALIZATION_TYPE_TYPE:
      T.Elem.K = CAValue::Kind::Type;
      return true;
    case SERIALIZATION_TYPE_TAGGED_OBJECT:
      T.Elem.K = CAValue::Kind::Boxed;
      return true;
    case SERIALIZATION_TYPE_ENUM:
      if (V.EnumTypeName.empty())
        return fail("enum array without an enum type name");
      T.Elem.K = CAValue::Kind::Enum;
      T.Elem.EnumName = V.EnumTypeName;
      return true;
    default:
      return fail("array with the element type code 0x" + utohexstr(Code));
    }
  }

  void writeFieldOrPropType(const ArgType &T) {
    if (T.IsArray)
      W.writeU8(SerializationSZArray);
    W.writeU8(getElemCode(T.Elem));
    if (T.Elem.K == CAValue::Kind::Enum)
      W.writeSerString(StringRef(T.Elem.EnumName));
  }

  /// A boxed value's FieldOrPropType and value.
  bool writeTagged(const CAValue &V) {
    ArgType T;
    if (!getArgTypeOf(V, T))
      return false;
    writeFieldOrPropType(T);
    return writeFixedArg(T, V);
  }

  bool writeNamedArg(const CANamedArg &A) {
    ArgType T;
    if (!getArgTypeOf(A.Value, T))
      return false;
    W.writeU8(A.IsField ? SERIALIZATION_TYPE_FIELD
                        : SERIALIZATION_TYPE_PROPERTY);
    writeFieldOrPropType(T);
    W.writeSerString(StringRef(A.Name));
    return writeFixedArg(T, A.Value);
  }

  BlobWriter W;
  std::string Where;
  std::optional<std::string> Msg;
};
} // namespace

static Error makeEncodeError(const Twine &Msg) {
  return createStringError(make_error_code(errc::invalid_argument),
                           "cannot encode custom attribute: " + Msg);
}

Error cli::encodeCustomAttribute(const MethodSig &Ctor,
                                 const CustomAttributeValue &V,
                                 SmallVectorImpl<uint8_t> &Out,
                                 EnumTypeResolver Resolve) {
  if (V.FixedArgs.size() != Ctor.Params.size())
    return makeEncodeError("the constructor has " + Twine(Ctor.Params.size()) +
                           " parameters and the value " +
                           Twine(V.FixedArgs.size()) + " fixed arguments");
  if (V.NamedArgs.size() > UINT16_MAX)
    return makeEncodeError(Twine(V.NamedArgs.size()) +
                           " named arguments (limit 65535)");
  SmallVector<uint8_t, 64> Blob;
  CAEncoder E(Blob);
  E.W.writeU16(CAProlog);
  for (size_t I = 0, N = V.FixedArgs.size(); I != N; ++I) {
    E.Where = ("fixed argument " + Twine(I + 1)).str();
    ArgType T;
    std::string Why;
    if (Error Err = getParamType(Ctor.Params[I], Resolve, T, Why))
      return Err;
    if (!Why.empty())
      return makeEncodeError(E.Where + ": " + Why);
    if (!E.writeFixedArg(T, V.FixedArgs[I]))
      return makeEncodeError(*E.Msg);
  }
  E.W.writeU16(V.NamedArgs.size());
  for (const CANamedArg &A : V.NamedArgs) {
    E.Where = "named argument '" + A.Name + "'";
    if (!E.writeNamedArg(A))
      return makeEncodeError(*E.Msg);
  }
  Out.append(Blob.begin(), Blob.end());
  return Error::success();
}

void cli::encodeByteArrayAttribute(ArrayRef<uint8_t> Payload,
                                   SmallVectorImpl<uint8_t> &Out) {
  assert(Payload.size() < CANullArray && "payload too large");
  BlobWriter W(Out);
  W.writeU16(CAProlog);
  W.writeU32(Payload.size());
  W.writeBytes(Payload);
  W.writeU16(0);
}

//===----------------------------------------------------------------------===//
// Decoder
//===----------------------------------------------------------------------===//

namespace {
/// The deepest nesting of boxed values that the decoder accepts.
static constexpr unsigned MaxBoxedDepth = 64;

class CADecoder {
public:
  CADecoder(ArrayRef<uint8_t> Blob, EnumNameResolver ResolveName = {})
      : Blob(Blob), R(Blob), ResolveName(ResolveName) {}

  bool fail(const Twine &Cause, size_t Offset) {
    if (ok())
      Msg = (Cause + " at offset 0x" + utohexstr(Offset)).str();
    return false;
  }

  ~CADecoder() { consumeError(std::move(ResolverErr)); }

  Error takeError() {
    Error E = R.takeError();
    if (HasResolverErr) {
      consumeError(std::move(E));
      HasResolverErr = false;
      return std::move(ResolverErr);
    }
    std::string Text = Msg ? *Msg : toString(std::move(E));
    if (Msg)
      consumeError(std::move(E));
    return createStringError(make_error_code(errc::illegal_byte_sequence),
                             "invalid custom attribute blob: " + Text);
  }

  bool ok() const { return !Msg && !R.hasError() && !HasResolverErr; }

  bool readScalar(ElementType ET, uint64_t &Bits) {
    switch (ET) {
    case ELEMENT_TYPE_I1:
      Bits = static_cast<uint64_t>(
          static_cast<int64_t>(static_cast<int8_t>(R.readU8())));
      break;
    case ELEMENT_TYPE_I2:
      Bits = static_cast<uint64_t>(
          static_cast<int64_t>(static_cast<int16_t>(R.readU16())));
      break;
    case ELEMENT_TYPE_I4:
      Bits = static_cast<uint64_t>(
          static_cast<int64_t>(static_cast<int32_t>(R.readU32())));
      break;
    default:
      switch (getScalarSize(ET)) {
      case 1:
        Bits = R.readU8();
        break;
      case 2:
        Bits = R.readU16();
        break;
      case 4:
        Bits = R.readU32();
        break;
      default:
        Bits = R.readU64();
        break;
      }
    }
    return !R.hasError();
  }

  bool readSerString(std::optional<std::string> &S) {
    std::optional<StringRef> V = R.readSerString();
    if (R.hasError())
      return false;
    S = V ? std::optional<std::string>(V->str()) : std::nullopt;
    return true;
  }

  bool readElem(const ElemType &T, CAValue &V) {
    V.K = T.K;
    if (isScalarKind(T.K))
      return readScalar(getScalarElementType(T.K), V.Bits);
    switch (T.K) {
    case CAValue::Kind::String:
    case CAValue::Kind::Type:
      return readSerString(V.Str);
    case CAValue::Kind::Enum:
      V.EnumUnderlying = T.Underlying;
      V.EnumTypeName = T.EnumName;
      return readScalar(T.Underlying, V.Bits);
    case CAValue::Kind::Boxed:
      V.BoxedValue = std::make_unique<CAValue>();
      return readTagged(*V.BoxedValue);
    default:
      llvm_unreachable("arrays are not elements");
    }
  }

  bool readFixedArg(const ArgType &T, CAValue &V) {
    if (!T.IsArray)
      return readElem(T.Elem, V);
    V.K = CAValue::Kind::Array;
    V.EnumUnderlying = getElemCode(T.Elem);
    V.EnumTypeName = T.Elem.EnumName;
    uint32_t N = R.readU32();
    if (R.hasError())
      return false;
    if (N == CANullArray) {
      V.Elements = std::nullopt;
      return true;
    }
    // Every element takes at least one byte; a count larger than what is
    // left fails as truncation without allocating for it.
    V.Elements.emplace();
    for (uint32_t I = 0; I != N; ++I) {
      CAValue E;
      if (!readElem(T.Elem, E))
        return false;
      V.Elements->push_back(std::move(E));
    }
    return true;
  }

  bool readElemType(uint8_t Code, size_t Off, ElemType &T) {
    T = ElemType();
    if (std::optional<CAValue::Kind> K = getScalarKind(Code)) {
      T.K = *K;
      return true;
    }
    switch (Code) {
    case ELEMENT_TYPE_STRING:
      T.K = CAValue::Kind::String;
      return true;
    case SERIALIZATION_TYPE_TYPE:
      T.K = CAValue::Kind::Type;
      return true;
    case SERIALIZATION_TYPE_TAGGED_OBJECT:
      T.K = CAValue::Kind::Boxed;
      return true;
    case SERIALIZATION_TYPE_ENUM: {
      size_t NameOff = R.tell();
      std::optional<std::string> Name;
      if (!readSerString(Name))
        return false;
      if (!Name)
        return fail("null enum type name", NameOff);
      // The blob names the enum only by its serialized name, and only
      // ResolveName gives its underlying type: the width of the value is not
      // guessed.
      if (!ResolveName)
        return fail("value of enum type '" + *Name +
                        "' and no enum name resolver was given",
                    NameOff);
      Expected<ElementType> U = ResolveName(*Name);
      if (!U) {
        if (ok()) {
          consumeError(std::move(ResolverErr));
          ResolverErr = U.takeError();
          HasResolverErr = true;
        } else {
          consumeError(U.takeError());
        }
        return false;
      }
      if (!isEnumUnderlying(*U))
        return fail("enum type '" + *Name +
                        "' has the underlying element type 0x" + utohexstr(*U) +
                        ", not an integer type",
                    NameOff);
      T.K = CAValue::Kind::Enum;
      T.Underlying = *U;
      T.EnumName = std::move(*Name);
      return true;
    }
    case SerializationSZArray:
      return fail("array of arrays", Off);
    default:
      return fail("unknown serialization type 0x" + utohexstr(Code), Off);
    }
  }

  bool readFieldOrPropType(ArgType &T) {
    T = ArgType();
    size_t Off = R.tell();
    uint8_t Code = R.readU8();
    if (R.hasError())
      return false;
    if (Code == SerializationSZArray) {
      T.IsArray = true;
      Off = R.tell();
      Code = R.readU8();
      if (R.hasError())
        return false;
    }
    return readElemType(Code, Off, T.Elem);
  }

  /// A boxed value: its FieldOrPropType and value.
  bool readTagged(CAValue &V) {
    size_t Off = R.tell();
    // object[] elements are boxed values that may be object[] again.
    if (BoxedDepth == MaxBoxedDepth)
      return fail("boxed values nested deeper than " + Twine(MaxBoxedDepth) +
                      " levels",
                  Off);
    ArgType T;
    if (!readFieldOrPropType(T))
      return false;
    if (!T.IsArray && T.Elem.K == CAValue::Kind::Boxed)
      return fail("boxed value of type object", Off);
    ++BoxedDepth;
    bool Result = readFixedArg(T, V);
    --BoxedDepth;
    return Result;
  }

  bool readNamedArg(CANamedArg &A) {
    size_t Off = R.tell();
    uint8_t Kind = R.readU8();
    if (R.hasError())
      return false;
    if (Kind != SERIALIZATION_TYPE_FIELD && Kind != SERIALIZATION_TYPE_PROPERTY)
      return fail("bad named argument kind 0x" + utohexstr(Kind), Off);
    A.IsField = Kind == SERIALIZATION_TYPE_FIELD;
    ArgType T;
    if (!readFieldOrPropType(T))
      return false;
    size_t NameOff = R.tell();
    std::optional<std::string> Name;
    if (!readSerString(Name))
      return false;
    if (!Name)
      return fail("null named argument name", NameOff);
    A.Name = std::move(*Name);
    return readFixedArg(T, A.Value);
  }

  bool readProlog() {
    uint16_t P = R.readU16();
    if (R.hasError())
      return false;
    if (P != CAProlog)
      return fail("bad prolog 0x" + utohexstr(P), 0);
    return true;
  }

  /// Checks that the whole blob was consumed.
  bool finish() {
    if (!ok())
      return false;
    if (R.empty())
      return true;
    size_t N = R.remaining();
    return fail(Twine(N) + (N == 1 ? " trailing byte" : " trailing bytes"),
                R.tell());
  }

  ArrayRef<uint8_t> Blob;
  BlobReader R;
  EnumNameResolver ResolveName;
  /// The boxed values being read, innermost included.
  unsigned BoxedDepth = 0;
  std::optional<std::string> Msg;
  /// An error of ResolveName, returned unchanged.
  Error ResolverErr = Error::success();
  bool HasResolverErr = false;
};
} // namespace

Expected<CustomAttributeValue>
cli::decodeCustomAttribute(const MethodSig &Ctor, ArrayRef<uint8_t> Blob,
                           EnumTypeResolver Resolve,
                           EnumNameResolver ResolveName) {
  // The parameter types. Resolve's errors are returned at once; any other
  // problem with a parameter type (an enum without Resolve, an enum whose
  // underlying type is not an integer, a type II.23.3 does not allow) is
  // kept and reported when the decoder reaches that argument, at its offset.
  SmallVector<ArgType, 4> Types;
  SmallVector<std::string, 4> Whys;
  for (const TypeSig &P : Ctor.Params) {
    ArgType T;
    std::string Why;
    if (Error Err = getParamType(P, Resolve, T, Why))
      return std::move(Err);
    Types.push_back(std::move(T));
    Whys.push_back(std::move(Why));
  }
  CADecoder D(Blob, ResolveName);
  if (!D.readProlog())
    return D.takeError();
  CustomAttributeValue V;
  for (size_t I = 0, N = Types.size(); I != N; ++I) {
    if (!Whys[I].empty()) {
      D.fail("fixed argument " + Twine(I + 1) + ": " + Whys[I], D.R.tell());
      return D.takeError();
    }
    CAValue A;
    if (!D.readFixedArg(Types[I], A))
      return D.takeError();
    V.FixedArgs.push_back(std::move(A));
  }
  uint16_t NumNamed = D.R.readU16();
  for (uint16_t I = 0; D.ok() && I != NumNamed; ++I) {
    CANamedArg A;
    if (!D.readNamedArg(A))
      break;
    V.NamedArgs.push_back(std::move(A));
  }
  if (!D.finish())
    return D.takeError();
  return V;
}

Expected<ArrayRef<uint8_t>>
cli::decodeByteArrayAttribute(ArrayRef<uint8_t> Blob) {
  CADecoder D(Blob);
  if (!D.readProlog())
    return D.takeError();
  size_t CountOff = D.R.tell();
  uint32_t N = D.R.readU32();
  if (!D.ok())
    return D.takeError();
  if (N == CANullArray) {
    D.fail("null byte array", CountOff);
    return D.takeError();
  }
  ArrayRef<uint8_t> Payload = D.R.readBytes(N);
  if (!D.ok())
    return D.takeError();
  size_t NamedOff = D.R.tell();
  uint16_t NumNamed = D.R.readU16();
  if (!D.ok())
    return D.takeError();
  if (NumNamed != 0) {
    D.fail(Twine(NumNamed) + " named arguments in a byte-array attribute",
           NamedOff);
    return D.takeError();
  }
  if (!D.finish())
    return D.takeError();
  return Payload;
}
