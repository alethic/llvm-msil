//===-- MSILMCTypeUtilities.h - MSIL type and name utilities ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// CIL value types, method signatures, and the ILAsm spellings of types and
/// identifiers. Everything that prints a method header, a call site, an
/// 'ldftn', a 'calli' signature or a P/Invoke declaration goes through these
/// functions, so a call site always spells the same signature as the method
/// definition it binds to.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCTYPEUTILITIES_H
#define LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCTYPEUTILITIES_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include <cstdint>

namespace llvm {

class raw_ostream;

namespace MSIL {

/// A CIL value type as it appears in signatures and '.locals'.
enum class ValType : uint8_t {
  I32,       ///< int32
  NativeInt, ///< native int: pointers and 64-bit integers on 64-bit triples
  I64,       ///< int64: 64-bit integers on 32-bit triples (not in Phase 1)
  F32,       ///< float32
  F64,       ///< float64
};

/// The calling convention of a call site signature ('calli').
enum class CallConv : uint8_t {
  Managed,          ///< default managed call
  UnmanagedCdecl,   ///< 'unmanaged cdecl'
  UnmanagedStdcall, ///< 'unmanaged stdcall'
};

/// A CIL method signature: at most one result and the parameter types.
struct Signature {
  SmallVector<ValType, 1> Results;
  SmallVector<ValType, 4> Params;
  CallConv CC = CallConv::Managed;

  bool operator==(const Signature &RHS) const;
  bool operator!=(const Signature &RHS) const { return !(*this == RHS); }
  /// A strict weak ordering (results, then parameters, then the calling
  /// convention), so signatures can be interned in ordered containers.
  bool operator<(const Signature &RHS) const;
};

/// Map a legal value type to its CIL type: i32 is int32, i64 is native int
/// when pointers are 64 bits wide (decision D2) and int64 otherwise, f32 and
/// f64 are float32 and float64.
ValType toValType(MVT VT, unsigned PointerSizeInBits);

/// The ILAsm spelling of \p T: "int32", "native int", "int64", "float32" or
/// "float64".
StringRef getValTypeName(ValType T);

/// Print the ILAsm spelling of \p T.
void printValType(raw_ostream &OS, ValType T);

/// Print the result type of \p S: "void" or the type of its only result.
void printResult(raw_ostream &OS, const Signature &S);

/// Print the parameter list of \p S, for example "(int32, native int)".
void printParamList(raw_ostream &OS, const Signature &S);

/// Print \p Name as a single-quoted ILAsm identifier. A quote becomes \',
/// a backslash \134 (ilasm drops the escape \\), bytes below 0x20 and 0x7f
/// become three-digit octal escapes, and other bytes (UTF-8 included) are
/// printed raw; a file with non-ASCII names must start with a UTF-8 BOM.
void printQuotedName(raw_ostream &OS, StringRef Name);

/// Print \p Str as a double-quoted ILAsm string (P/Invoke library and entry
/// point names): a double quote, a backslash, bytes below 0x20 and 0x7f
/// become three-digit octal escapes, and other bytes are printed raw.
void printQuotedString(raw_ostream &OS, StringRef Str);

} // end namespace MSIL
} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MCTARGETDESC_MSILMCTYPEUTILITIES_H
