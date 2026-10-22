//===- llvm/BinaryFormat/CLI.h - ECMA-335 file format -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines manifest constants of the ECMA-335 Common Language
// Infrastructure (CLI) file format, the format of .NET assemblies and
// modules: metadata table ids and tokens, CIL opcodes and their operand
// kinds, signature element types and calling conventions, the attribute
// flags of the metadata tables, the CLI header, the metadata root, method
// body headers, compressed integers and the limits the runtimes enforce.
// The values are those of ECMA-335 6th edition, Partitions II and III; the
// table schema is in CLITables.def and the opcode table in CILOpcodes.def.
//
// Everything is in namespace llvm::cli, which the ECMA-335 library
// (llvm/CLI, component CLI) shares. This header is header-only.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_CLI_H
#define LLVM_BINARYFORMAT_CLI_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Endian.h"
#include <cassert>
#include <cstdint>
#include <optional>

namespace llvm {
namespace cli {

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

//===----------------------------------------------------------------------===//
// Limits (the CLR limits that the library enforces)
//===----------------------------------------------------------------------===//

/// The longest identifier, in UTF-8 bytes, that the library writes: both
/// ilasms accept no longer name, and PEVerify reports longer names as
/// invalid. The same value as the MSIL target's MSIL::MaxILNameBytes.
constexpr uint32_t MaxIdentifierBytes = 1023;
/// The most methods and fields that one TypeDef may own: a type with 65,525
/// methods or 65,536 fields fails to load on every runtime.
constexpr uint32_t MaxMethodsPerType = 65524;
constexpr uint32_t MaxFieldsPerType = 65534;
/// A token holds a row number in 24 bits.
constexpr uint32_t MaxTableRows = 0x00FFFFFF;
/// An ldstr token holds a #US offset in 24 bits.
constexpr uint32_t MaxUserStringHeapSize = 0x00FFFFFF;

//===----------------------------------------------------------------------===//
// Metadata tables and tokens (II.22, II.24.2.6)
//===----------------------------------------------------------------------===//

/// A metadata table (II.22): Module = 0x00, TypeRef = 0x01, ...,
/// GenericParamConstraint = 0x2C. Ids 0x30-0x37 (the Portable PDB tables) are
/// reserved and have neither an enumerator nor a schema.
enum class TableId : uint8_t {
#define CLI_TABLE(Id, Name, Sorted) Name = Id,
#include "llvm/BinaryFormat/CLITables.def"
};

/// The width of the #~ stream's Valid and Sorted bit vectors.
constexpr unsigned NumTableIds = 64;

/// A coded index (II.24.2.6), in CLITables.def order: TypeDefOrRef,
/// HasConstant, ..., TypeOrMethodDef.
enum class CodedIndexKind : uint8_t {
#define CLI_CODED_INDEX(Name, TagBits, ...) Name,
#include "llvm/BinaryFormat/CLITables.def"
};

/// The physical kind of a table column (CLITables.def).
enum class ColumnKind : uint8_t {
  U16,       ///< A 2-byte constant.
  U32,       ///< A 4-byte constant.
  StringIdx, ///< An index into the #Strings heap.
  GuidIdx,   ///< An index into the #GUID heap.
  BlobIdx,   ///< An index into the #Blob heap.
  TableIdx,  ///< A simple index into one table.
  CodedIdx,  ///< A coded index into one of several tables.
};

/// A metadata token: the table in the high byte and the row (1-based; 0 is
/// nil) in the low 24 bits.
constexpr uint32_t makeToken(TableId T, uint32_t Row) {
  assert(Row <= MaxTableRows && "row number does not fit in a token");
  return (static_cast<uint32_t>(T) << 24) | Row;
}

/// The high byte of a token: a TableId value, or UserStringTokenType.
constexpr uint8_t getTokenType(uint32_t Token) { return Token >> 24; }

/// The low 24 bits of a token: a row number, or a #US offset.
constexpr uint32_t getTokenRow(uint32_t Token) { return Token & MaxTableRows; }

/// The token type of an ldstr operand: 0x70000000 | #US offset.
constexpr uint8_t UserStringTokenType = 0x70;

//===----------------------------------------------------------------------===//
// CIL opcodes (Partition III)
//===----------------------------------------------------------------------===//

/// The encoding of an instruction's inline operand (III.1.2; the
/// System.Reflection.Emit.OperandType names).
enum class OperandKind : uint8_t {
  InlineNone,          ///< No operand.
  ShortInlineVar,      ///< An 8-bit argument or local number.
  InlineVar,           ///< A 16-bit argument or local number.
  ShortInlineI,        ///< An 8-bit integer.
  InlineI,             ///< A 32-bit integer.
  InlineI8,            ///< A 64-bit integer.
  ShortInlineR,        ///< A 32-bit IEEE float.
  InlineR,             ///< A 64-bit IEEE float.
  ShortInlineBrTarget, ///< An 8-bit offset from the next instruction.
  InlineBrTarget,      ///< A 32-bit offset from the next instruction.
  InlineMethod,        ///< A MethodDef, MemberRef or MethodSpec token.
  InlineField,         ///< A Field or MemberRef token.
  InlineType,          ///< A TypeDef, TypeRef or TypeSpec token.
  InlineTok,           ///< A type, method or field token (ldtoken).
  InlineSig,           ///< A StandAloneSig token.
  InlineString,        ///< A user-string token.
  InlineSwitch,        ///< A count N and N 32-bit offsets from the end of
                       ///< the instruction.
};

/// What an instruction pops and pushes, spelled as in
/// System.Reflection.Emit.StackBehaviour: one enumerator for each Pop and Push
/// name that CILOpcodes.def uses.
enum class StackBehaviour : uint8_t {
  Pop0,
  Pop1,
  Pop1_pop1,
  Popi,
  Popi_pop1,
  Popi_popi,
  Popi_popi8,
  Popi_popi_popi,
  Popi_popr4,
  Popi_popr8,
  Popref,
  Popref_pop1,
  Popref_popi,
  Popref_popi_popi,
  Popref_popi_popi8,
  Popref_popi_popr4,
  Popref_popi_popr8,
  Popref_popi_popref,
  Popref_popi_pop1,
  Push0,
  Push1,
  Push1_push1,
  Pushi,
  Pushi8,
  Pushr4,
  Pushr8,
  Pushref,
  Varpop,
  Varpush,
};

/// How an instruction affects control flow, spelled as in
/// System.Reflection.Emit.FlowControl.
enum class FlowControl : uint8_t {
  Branch,
  Break,
  Call,
  Cond_Branch,
  Meta,
  Next,
  Return,
  Throw,
};

/// A CIL opcode, valued as encoded: 0x00XX for a one-byte opcode, 0xFEXX for
/// a two-byte opcode (Nop = 0x0000, ..., Ldc_i4_s = 0x001F, ...,
/// Readonly = 0xFE1E).
enum class Opcode : uint16_t {
#define CIL_OPCODE(Enum, Text, Value, Operand, Pop, Push, Flow) Enum = Value,
#include "llvm/BinaryFormat/CILOpcodes.def"
};

/// The first byte of every two-byte opcode.
constexpr uint8_t TwoByteOpcodePrefix = 0xFE;

//===----------------------------------------------------------------------===//
// Signatures (II.23.1.16, II.23.2)
//===----------------------------------------------------------------------===//

/// The element types of signature blobs (II.23.1.16).
enum ElementType : uint8_t {
  ELEMENT_TYPE_END = 0x00,
  ELEMENT_TYPE_VOID = 0x01,
  ELEMENT_TYPE_BOOLEAN = 0x02,
  ELEMENT_TYPE_CHAR = 0x03,
  ELEMENT_TYPE_I1 = 0x04,
  ELEMENT_TYPE_U1 = 0x05,
  ELEMENT_TYPE_I2 = 0x06,
  ELEMENT_TYPE_U2 = 0x07,
  ELEMENT_TYPE_I4 = 0x08,
  ELEMENT_TYPE_U4 = 0x09,
  ELEMENT_TYPE_I8 = 0x0A,
  ELEMENT_TYPE_U8 = 0x0B,
  ELEMENT_TYPE_R4 = 0x0C,
  ELEMENT_TYPE_R8 = 0x0D,
  ELEMENT_TYPE_STRING = 0x0E,
  ELEMENT_TYPE_PTR = 0x0F,         ///< Followed by a type.
  ELEMENT_TYPE_BYREF = 0x10,       ///< Followed by a type.
  ELEMENT_TYPE_VALUETYPE = 0x11,   ///< Followed by a TypeDefOrRefOrSpec.
  ELEMENT_TYPE_CLASS = 0x12,       ///< Followed by a TypeDefOrRefOrSpec.
  ELEMENT_TYPE_VAR = 0x13,         ///< A type's generic parameter number.
  ELEMENT_TYPE_ARRAY = 0x14,       ///< Followed by a type and an ArrayShape.
  ELEMENT_TYPE_GENERICINST = 0x15, ///< A generic type instantiation.
  ELEMENT_TYPE_TYPEDBYREF = 0x16,
  ELEMENT_TYPE_I = 0x18,         ///< System.IntPtr (native int).
  ELEMENT_TYPE_U = 0x19,         ///< System.UIntPtr (native unsigned int).
  ELEMENT_TYPE_FNPTR = 0x1B,     ///< Followed by a method signature.
  ELEMENT_TYPE_OBJECT = 0x1C,    ///< System.Object.
  ELEMENT_TYPE_SZARRAY = 0x1D,   ///< A single-dimensional, zero-based array.
  ELEMENT_TYPE_MVAR = 0x1E,      ///< A method's generic parameter number.
  ELEMENT_TYPE_CMOD_REQD = 0x1F, ///< A required modifier and its type.
  ELEMENT_TYPE_CMOD_OPT = 0x20,  ///< An optional modifier and its type.
  ELEMENT_TYPE_INTERNAL = 0x21,  ///< Implemented within the CLI.
  ELEMENT_TYPE_MODIFIER = 0x40,  ///< Or'd with the following element types.
  ELEMENT_TYPE_SENTINEL = 0x41,  ///< Starts the varargs of a call site.
  ELEMENT_TYPE_PINNED = 0x45,    ///< A pinned local.
};

/// The type codes that only custom attribute blobs use (II.23.1.16, II.23.3).
enum SerializationTypeCode : uint8_t {
  SERIALIZATION_TYPE_TYPE = 0x50,          ///< A System.Type argument.
  SERIALIZATION_TYPE_TAGGED_OBJECT = 0x51, ///< A boxed object.
  SERIALIZATION_TYPE_FIELD = 0x53,         ///< A named field argument.
  SERIALIZATION_TYPE_PROPERTY = 0x54,      ///< A named property argument.
  SERIALIZATION_TYPE_ENUM = 0x55,          ///< An enum argument.
};

/// The first byte of a signature blob (II.23.2.1-II.23.2.6, II.23.2.15): a
/// kind in the low four bits, and flags.
enum SignatureHeader : uint8_t {
  SIG_DEFAULT = 0x00,
  SIG_C = 0x01,
  SIG_STDCALL = 0x02,
  SIG_THISCALL = 0x03,
  SIG_FASTCALL = 0x04,
  SIG_VARARG = 0x05,
  SIG_FIELD = 0x06,
  SIG_LOCAL = 0x07,
  SIG_PROPERTY = 0x08,
  SIG_UNMANAGED = 0x09,
  SIG_GENERICINST = 0x0A,
  SIG_KIND_MASK = 0x0F,
  SIG_GENERIC = 0x10,
  SIG_HASTHIS = 0x20,
  SIG_EXPLICITTHIS = 0x40,
};

//===----------------------------------------------------------------------===//
// Attribute flags (II.23.1)
//
// The enumerators carry ECMA's flag names in UpperCamelCase, spelled as
// System.Reflection spells them where it has the value; where ECMA spells a
// value differently, its spelling is a second enumerator. Multi-bit fields
// have a Mask enumerator. Every bit of a flags column is a valid enumerator
// value (LLVM_MARK_AS_BITMASK_ENUM names the column's top bit), so flags read
// from a file never trip BitmaskEnum's range assertion.
//===----------------------------------------------------------------------===//

/// TypeDef and ExportedType flags (II.23.1.15).
enum class TypeAttributes : uint32_t {
  VisibilityMask = 0x00000007,
  NotPublic = 0x00000000,
  Public = 0x00000001,
  NestedPublic = 0x00000002,
  NestedPrivate = 0x00000003,
  NestedFamily = 0x00000004,
  NestedAssembly = 0x00000005,
  NestedFamANDAssem = 0x00000006,
  NestedFamORAssem = 0x00000007,
  LayoutMask = 0x00000018,
  AutoLayout = 0x00000000,
  SequentialLayout = 0x00000008,
  ExplicitLayout = 0x00000010,
  ClassSemanticsMask = 0x00000020,
  Class = 0x00000000,
  Interface = 0x00000020,
  Abstract = 0x00000080,
  Sealed = 0x00000100,
  SpecialName = 0x00000400,
  Import = 0x00001000,
  Serializable = 0x00002000,
  StringFormatMask = 0x00030000,
  AnsiClass = 0x00000000,
  UnicodeClass = 0x00010000,
  AutoClass = 0x00020000,
  CustomFormatClass = 0x00030000,
  CustomFormatMask = 0x00C00000,
  CustomStringFormatMask = CustomFormatMask, ///< ECMA's spelling.
  BeforeFieldInit = 0x00100000,
  RTSpecialName = 0x00000800,
  HasSecurity = 0x00040000,
  Forwarder = 0x00200000,      ///< An ExportedType that forwards a type.
  IsTypeForwarder = Forwarder, ///< ECMA's spelling.
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ 0x80000000)
};

/// Field flags (II.23.1.5).
enum class FieldAttributes : uint16_t {
  FieldAccessMask = 0x0007,
  PrivateScope = 0x0000,
  CompilerControlled = PrivateScope, ///< ECMA's spelling.
  Private = 0x0001,
  FamANDAssem = 0x0002,
  Assembly = 0x0003,
  Family = 0x0004,
  FamORAssem = 0x0005,
  Public = 0x0006,
  Static = 0x0010,
  InitOnly = 0x0020,
  Literal = 0x0040,
  NotSerialized = 0x0080,
  SpecialName = 0x0200,
  PinvokeImpl = 0x2000,
  PInvokeImpl = PinvokeImpl, ///< ECMA's spelling.
  RTSpecialName = 0x0400,
  HasFieldMarshal = 0x1000,
  HasDefault = 0x8000,
  HasFieldRVA = 0x0100,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ 0x8000)
};

/// MethodDef flags (II.23.1.10).
enum class MethodAttributes : uint16_t {
  MemberAccessMask = 0x0007,
  PrivateScope = 0x0000,
  CompilerControlled = PrivateScope, ///< ECMA's spelling.
  Private = 0x0001,
  FamANDAssem = 0x0002,
  Assembly = 0x0003,
  Assem = Assembly, ///< ECMA's spelling.
  Family = 0x0004,
  FamORAssem = 0x0005,
  Public = 0x0006,
  Static = 0x0010,
  Final = 0x0020,
  Virtual = 0x0040,
  HideBySig = 0x0080,
  VtableLayoutMask = 0x0100,
  ReuseSlot = 0x0000,
  NewSlot = 0x0100,
  CheckAccessOnOverride = 0x0200,
  Strict = CheckAccessOnOverride, ///< ECMA's spelling.
  Abstract = 0x0400,
  SpecialName = 0x0800,
  PinvokeImpl = 0x2000,
  PInvokeImpl = PinvokeImpl, ///< ECMA's spelling.
  UnmanagedExport = 0x0008,
  RTSpecialName = 0x1000,
  HasSecurity = 0x4000,
  RequireSecObject = 0x8000,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ 0x8000)
};

/// MethodDef implementation flags (II.23.1.11).
enum class MethodImplAttributes : uint16_t {
  CodeTypeMask = 0x0003,
  IL = 0x0000,
  Native = 0x0001,
  OPTIL = 0x0002,
  Runtime = 0x0003,
  ManagedMask = 0x0004,
  Unmanaged = 0x0004,
  Managed = 0x0000,
  ForwardRef = 0x0010,
  PreserveSig = 0x0080,
  InternalCall = 0x1000,
  Synchronized = 0x0020,
  NoInlining = 0x0008,
  MaxMethodImplVal = 0xFFFF,
  NoOptimization = 0x0040,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ 0x8000)
};

/// Param flags (II.23.1.13).
enum class ParamAttributes : uint16_t {
  None = 0x0000,
  In = 0x0001,
  Out = 0x0002,
  Optional = 0x0010,
  HasDefault = 0x1000,
  HasFieldMarshal = 0x2000,
  Unused = 0xCFE0, ///< Reserved: zero in a conforming file.
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ 0x8000)
};

/// Property flags (II.23.1.14).
enum class PropertyAttributes : uint16_t {
  None = 0x0000,
  SpecialName = 0x0200,
  RTSpecialName = 0x0400,
  HasDefault = 0x1000,
  Unused = 0xE9FF, ///< Reserved: zero in a conforming file.
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ 0x8000)
};

/// MethodSemantics flags (II.23.1.12).
enum class MethodSemanticsAttributes : uint16_t {
  Setter = 0x0001,
  Getter = 0x0002,
  Other = 0x0004,
  Adder = 0x0008,
  AddOn = Adder, ///< ECMA's spelling.
  Remover = 0x0010,
  RemoveOn = Remover, ///< ECMA's spelling.
  Raiser = 0x0020,
  Fire = Raiser, ///< ECMA's spelling.
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ 0x8000)
};

/// ImplMap flags (II.23.1.8).
enum class PInvokeAttributes : uint16_t {
  NoMangle = 0x0001,
  CharSetMask = 0x0006,
  CharSetNotSpec = 0x0000,
  CharSetAnsi = 0x0002,
  CharSetUnicode = 0x0004,
  CharSetAuto = 0x0006,
  SupportsLastError = 0x0040,
  CallConvMask = 0x0700,
  CallConvPlatformapi = 0x0100,
  CallConvWinapi = CallConvPlatformapi, ///< ilasm's 'winapi'.
  CallConvCdecl = 0x0200,
  CallConvStdcall = 0x0300,
  CallConvThiscall = 0x0400,
  CallConvFastcall = 0x0500,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ 0x8000)
};

/// GenericParam flags (II.23.1.7).
enum class GenericParamAttributes : uint16_t {
  VarianceMask = 0x0003,
  None = 0x0000,
  Covariant = 0x0001,
  Contravariant = 0x0002,
  SpecialConstraintMask = 0x001C,
  ReferenceTypeConstraint = 0x0004,
  NotNullableValueTypeConstraint = 0x0008,
  DefaultConstructorConstraint = 0x0010,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ 0x8000)
};

/// Assembly and AssemblyRef flags (II.23.1.2).
enum class AssemblyFlags : uint32_t {
  PublicKey = 0x0001,
  Retargetable = 0x0100,
  DisableJitCompileOptimizer = 0x4000,
  DisableJITcompileOptimizer = DisableJitCompileOptimizer, ///< ECMA's spelling.
  EnableJitCompileTracking = 0x8000,
  EnableJITcompileTracking = EnableJitCompileTracking, ///< ECMA's spelling.
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ 0x80000000)
};

/// ManifestResource flags (II.23.1.9).
enum class ManifestResourceAttributes : uint32_t {
  VisibilityMask = 0x0007,
  Public = 0x0001,
  Private = 0x0002,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ 0x80000000)
};

/// File flags (II.23.1.6).
enum class FileAttributes : uint32_t {
  ContainsMetaData = 0x0000,
  ContainsNoMetaData = 0x0001,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ 0x80000000)
};

/// The hash algorithm of an Assembly row (II.23.1.1). A value, not flags.
enum class AssemblyHashAlgorithm : uint32_t {
  None = 0x0000,
  MD5 = 0x8003, ///< Reserved.
  SHA1 = 0x8004,
};

//===----------------------------------------------------------------------===//
// The CLI header and the debug directory (II.25.3.3)
//===----------------------------------------------------------------------===//

/// Cor20Header::Flags (II.25.3.3.1).
enum CorFlags : uint32_t {
  COMIMAGE_FLAGS_ILONLY = 0x00000001,
  COMIMAGE_FLAGS_32BITREQUIRED = 0x00000002,
  COMIMAGE_FLAGS_IL_LIBRARY = 0x00000004,
  COMIMAGE_FLAGS_STRONGNAMESIGNED = 0x00000008,
  COMIMAGE_FLAGS_NATIVE_ENTRYPOINT = 0x00000010,
  COMIMAGE_FLAGS_TRACKDEBUGDATA = 0x00010000,
  COMIMAGE_FLAGS_32BITPREFERRED = 0x00020000,
};

/// An RVA and a size, as the CLI header stores them.
struct CLIDataDirectory {
  support::ulittle32_t RVA;
  support::ulittle32_t Size;
};

/// The CLI header (II.25.3.3), which the PE's COM descriptor directory points
/// at. Writers store Cb = 72 and runtime version 2.5, as ilasm and Roslyn do.
struct Cor20Header {
  support::ulittle32_t Cb;                  ///< The header size, 72.
  support::ulittle16_t MajorRuntimeVersion; ///< 2.
  support::ulittle16_t MinorRuntimeVersion; ///< 5.
  CLIDataDirectory MetaData;
  support::ulittle32_t Flags;           ///< CorFlags.
  support::ulittle32_t EntryPointToken; ///< A MethodDef or File token, or 0.
  CLIDataDirectory Resources;
  CLIDataDirectory StrongNameSignature;
  CLIDataDirectory CodeManagerTable; ///< Always 0.
  CLIDataDirectory VTableFixups;
  CLIDataDirectory ExportAddressTableJumps; ///< Always 0.
  CLIDataDirectory ManagedNativeHeader;     ///< Always 0.
};
static_assert(sizeof(Cor20Header) == 72, "the CLI header is 72 bytes");

/// Debug directory entry types that COFF.h lacks (the PE/COFF specification):
/// an embedded Portable PDB and a PDB checksum.
constexpr uint32_t DebugTypeEmbeddedPortablePdb = 17, DebugTypePdbChecksum = 19;

//===----------------------------------------------------------------------===//
// The metadata root and the #~ stream (II.24.2.1, II.24.2.6)
//===----------------------------------------------------------------------===//

constexpr uint32_t MetadataSignature = 0x424A5342; ///< "BSJB"
constexpr uint16_t MetadataMajorVersion = 1;
constexpr uint16_t MetadataMinorVersion = 1;
/// The version string that .NET Framework 4 and every later runtime accept.
inline constexpr StringLiteral MetadataVersionString = "v4.0.30319";
constexpr uint8_t TablesStreamMajorVersion = 2;
constexpr uint8_t TablesStreamMinorVersion = 0;

/// The #~ stream's HeapSizes byte. HEAP_EXTRA_DATA (not in ECMA) marks an
/// extra 4-byte field after the row counts.
enum HeapSizeFlags : uint8_t {
  HEAP_STRINGS_4 = 0x01, ///< #Strings indexes are 4 bytes.
  HEAP_GUID_4 = 0x02,    ///< #GUID indexes are 4 bytes.
  HEAP_BLOB_4 = 0x04,    ///< #Blob indexes are 4 bytes.
  HEAP_EXTRA_DATA = 0x40,
};

/// The #~ Sorted vector: exactly the tables that II.22 requires to be sorted
/// (the Sorted column of CLITables.def), as Roslyn and
/// System.Reflection.Metadata write it.
constexpr uint64_t SortedTablesMask = 0x000016003301FA00;

//===----------------------------------------------------------------------===//
// Method bodies (II.25.4)
//===----------------------------------------------------------------------===//

/// The flags of a method header's first byte (II.25.4.1, II.25.4.4).
enum MethodHeaderFlags : uint16_t {
  MethodHeaderTinyFormat = 0x2,
  MethodHeaderFatFormat = 0x3,
  MethodHeaderMoreSects = 0x8,
  MethodHeaderInitLocals = 0x10,
};

/// The size of a fat method header in bytes (three 4-byte words).
constexpr unsigned FatHeaderSize = 12;
/// The largest code size and .maxstack that a tiny header can describe.
constexpr unsigned TinyMaxCodeSize = 63;
constexpr unsigned TinyMaxStack = 8;

/// The Kind byte of a method data section (II.25.4.5).
enum EHSectionFlags : uint8_t {
  EHSectionEHTable = 0x1,
  EHSectionOptILTable = 0x2,
  EHSectionFatFormat = 0x40,
  EHSectionMoreSects = 0x80,
};

/// The kind of an exception handling clause (II.25.4.6).
enum class ExceptionClauseKind : uint32_t {
  Catch = 0,
  Filter = 1,
  Finally = 2,
  Fault = 4,
};

//===----------------------------------------------------------------------===//
// Assembly versions
//===----------------------------------------------------------------------===//

/// The four-part version of an Assembly or AssemblyRef row.
struct AssemblyVersion {
  uint16_t Major = 0, Minor = 0, Build = 0, Revision = 0;

  friend bool operator==(const AssemblyVersion &L, const AssemblyVersion &R) {
    return L.Major == R.Major && L.Minor == R.Minor && L.Build == R.Build &&
           L.Revision == R.Revision;
  }
  friend bool operator!=(const AssemblyVersion &L, const AssemblyVersion &R) {
    return !(L == R);
  }
};

//===----------------------------------------------------------------------===//
// Compressed integers (II.23.2)
//===----------------------------------------------------------------------===//

/// The largest value that a compressed unsigned integer holds (29 bits).
constexpr uint32_t MaxCompressedUInt = 0x1FFFFFFF;

/// The number of bytes (1, 2 or 4) that encodeCompressedUInt writes for V.
inline unsigned getCompressedUIntSize(uint32_t V) {
  assert(V <= MaxCompressedUInt && "value too large for a compressed integer");
  return V < 0x80 ? 1 : V < 0x4000 ? 2 : 4;
}

/// Writes V as a compressed unsigned integer (big-endian, with the size in
/// the lead byte's top bits) and returns the number of bytes written.
inline unsigned encodeCompressedUInt(uint32_t V, uint8_t Out[4]) {
  assert(V <= MaxCompressedUInt && "value too large for a compressed integer");
  if (V < 0x80) {
    Out[0] = V;
    return 1;
  }
  if (V < 0x4000) {
    Out[0] = 0x80 | (V >> 8);
    Out[1] = V & 0xFF;
    return 2;
  }
  Out[0] = 0xC0 | (V >> 24);
  Out[1] = (V >> 16) & 0xFF;
  Out[2] = (V >> 8) & 0xFF;
  Out[3] = V & 0xFF;
  return 4;
}

/// Reads a compressed unsigned integer from the front of Bytes and drops its
/// bytes. Returns std::nullopt, and leaves Bytes unchanged, if Bytes is
/// truncated or starts with a lead byte of the form 111xxxxx (which no
/// compressed integer has; 0xFF is the null string of custom attributes).
inline std::optional<uint32_t> decodeCompressedUInt(ArrayRef<uint8_t> &Bytes) {
  if (Bytes.empty())
    return std::nullopt;
  uint8_t Lead = Bytes[0];
  if ((Lead & 0x80) == 0) {
    Bytes = Bytes.drop_front(1);
    return Lead;
  }
  if ((Lead & 0xC0) == 0x80) {
    if (Bytes.size() < 2)
      return std::nullopt;
    uint32_t V = (static_cast<uint32_t>(Lead & 0x3F) << 8) | Bytes[1];
    Bytes = Bytes.drop_front(2);
    return V;
  }
  if ((Lead & 0xE0) == 0xC0) {
    if (Bytes.size() < 4)
      return std::nullopt;
    uint32_t V = (static_cast<uint32_t>(Lead & 0x1F) << 24) |
                 (static_cast<uint32_t>(Bytes[1]) << 16) |
                 (static_cast<uint32_t>(Bytes[2]) << 8) | Bytes[3];
    Bytes = Bytes.drop_front(4);
    return V;
  }
  return std::nullopt;
}

/// Writes V as a compressed signed integer: V as a 7-, 14- or 29-bit two's
/// complement number rotated left by one bit (the sign in bit 0), in the
/// unsigned form's 1, 2 or 4 bytes. Requires -2^28 <= V < 2^28.
inline unsigned encodeCompressedInt(int32_t V, uint8_t Out[4]) {
  assert(V >= -(1 << 28) && V < (1 << 28) &&
         "value out of range for a compressed signed integer");
  uint32_t Sign = V < 0 ? 1 : 0;
  uint32_t Shifted = static_cast<uint32_t>(V) << 1;
  if (V >= -(1 << 6) && V < (1 << 6)) {
    Out[0] = (Shifted & 0x7E) | Sign;
    return 1;
  }
  if (V >= -(1 << 13) && V < (1 << 13)) {
    uint32_t R = (Shifted & 0x3FFE) | Sign;
    Out[0] = 0x80 | (R >> 8);
    Out[1] = R & 0xFF;
    return 2;
  }
  uint32_t R = (Shifted & 0x1FFFFFFE) | Sign;
  Out[0] = 0xC0 | (R >> 24);
  Out[1] = (R >> 16) & 0xFF;
  Out[2] = (R >> 8) & 0xFF;
  Out[3] = R & 0xFF;
  return 4;
}

/// Reads a compressed signed integer from the front of Bytes and drops its
/// bytes; std::nullopt (Bytes unchanged) as decodeCompressedUInt.
inline std::optional<int32_t> decodeCompressedInt(ArrayRef<uint8_t> &Bytes) {
  ArrayRef<uint8_t> Rest = Bytes;
  std::optional<uint32_t> R = decodeCompressedUInt(Rest);
  if (!R)
    return std::nullopt;
  size_t Size = Bytes.size() - Rest.size();
  unsigned Bits = Size == 1 ? 7 : Size == 2 ? 14 : 29;
  int32_t V = static_cast<int32_t>(*R >> 1);
  if (*R & 1)
    V -= int32_t(1) << (Bits - 1);
  Bytes = Rest;
  return V;
}

/// The TypeDefOrRefOrSpecEncoded value of a signature (II.23.2.8): the row
/// shifted left by two, or'd with 0 for a TypeDef, 1 for a TypeRef and 2 for
/// a TypeSpec token. The token must be one of those, with a row other than 0.
inline uint32_t encodeTypeDefOrRefOrSpec(uint32_t Token) {
  uint8_t Type = getTokenType(Token);
  uint32_t Row = getTokenRow(Token);
  assert((Type == static_cast<uint8_t>(TableId::TypeDef) ||
          Type == static_cast<uint8_t>(TableId::TypeRef) ||
          Type == static_cast<uint8_t>(TableId::TypeSpec)) &&
         Row != 0 && "not a TypeDef, TypeRef or TypeSpec token");
  uint32_t Tag = Type == static_cast<uint8_t>(TableId::TypeDef)   ? 0
                 : Type == static_cast<uint8_t>(TableId::TypeRef) ? 1
                                                                  : 2;
  return (Row << 2) | Tag;
}

/// The token of a TypeDefOrRefOrSpecEncoded value, or std::nullopt for tag 3,
/// row 0 or a row that does not fit in a token.
inline std::optional<uint32_t> decodeTypeDefOrRefOrSpec(uint32_t Coded) {
  uint32_t Row = Coded >> 2;
  if (Row == 0 || Row > MaxTableRows)
    return std::nullopt;
  switch (Coded & 3) {
  case 0:
    return makeToken(TableId::TypeDef, Row);
  case 1:
    return makeToken(TableId::TypeRef, Row);
  case 2:
    return makeToken(TableId::TypeSpec, Row);
  default:
    return std::nullopt;
  }
}

} // namespace cli
} // namespace llvm

#endif // LLVM_BINARYFORMAT_CLI_H
