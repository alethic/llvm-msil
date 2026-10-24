//===- RoundTripTest.cpp - Builder, PE writer and reader round trip -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Builds a library that uses every table of 5.7.6's writer set with
// MetadataBuilder, writes it with writePEImage (5.7.7), reads it back with
// MetadataReader (5.7.8) and checks every row and column, the method bodies,
// signatures, custom attributes, properties and field data. The module is
// the one that the IL text in the comment of RtListing assembles to with
// Framework ilasm 4.8 (-dll -pe64 -x64); RtListing, HelloListing and
// ObjListing are M16's mdcanon listings (canonical listing v1, 5.7.10) of the
// ilasm images, made in scratch, and dumpCanonical must print them for the
// images this library writes. Expected values never come from the
// implementation under test.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/CustomAttribute.h"
#include "llvm/CLI/Dump.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/PEWriter.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CLI/TableSchema.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

using ByteVec = std::vector<uint8_t>;

uint32_t rowOf(uint32_t Token) { return getTokenRow(Token); }

EntityHandle rowHandle(TableId T, uint32_t Row) {
  EntityHandle H;
  H.Value = makeToken(T, Row);
  return H;
}

ByteVec bytes(std::initializer_list<uint8_t> B) { return ByteVec(B); }

// mdcanon's listing of the ilasm image of this IL (Framework ilasm 4.8,
// -dll -pe64 -x64 -output=rt.dll):
//
//   .assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 )
//                               .ver 4:0:0:0 }
//   .assembly rt {
//     .custom instance void [mscorlib]System.Runtime.CompilerServices.
//       CompilationRelaxationsAttribute::.ctor(int32) = (01 00 08 00 00 00 00
//       00)
//     .hash algorithm 0x00008004
//     .ver 1:2:3:4 }
//   .module rt.dll
//   .custom instance void [mscorlib]System.CLSCompliantAttribute::.ctor(bool)
//     = (01 00 01 00 00)
//   .module extern kernel32
//   .field public static valuetype NS.Blk 'blob' at D_0
//   .field assembly static int32 'counter' at D_1
//   .data D_0 = bytearray (01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10)
//   .data D_1 = int32(7)
//   .class public explicit ansi sealed NS.Blk extends
//   [mscorlib]System.ValueType { .pack 1 .size 16 .field [4] public int32 a }
//   .class public auto ansi beforefieldinit NS.Holder
//       extends [mscorlib]System.Object {
//     .class nested public auto ansi NestedT extends [mscorlib]System.Object {}
//     .field private int32 _value
//     .method public hidebysig specialname rtspecialname instance void .ctor()
//     { .maxstack 8 ldarg.0 call instance void [mscorlib]System.Object::.ctor()
//       ret }
//     .method public hidebysig specialname instance int32 get_Value()
//     { .maxstack 8 ldarg.0 ldfld int32 NS.Holder::_value ret }
//     .method public hidebysig specialname instance void set_Value(int32 v)
//     { .maxstack 8 ldarg.0 ldarg.1 stfld int32 NS.Holder::_value ret }
//     .property instance int32 Value() {
//       .get instance int32 NS.Holder::get_Value()
//       .set instance void NS.Holder::set_Value(int32) }
//     .method public hidebysig static int32 Add(int32 a, int32 b) {
//       .custom instance void [mscorlib]System.ObsoleteAttribute::.ctor()
//         = (01 00 00 00)
//       .maxstack 8 ldarg.0 ldarg.1 add ret }
//     .method public static pinvokeimpl("kernel32" cdecl) void Sleep(int32 ms)
//       cil managed preservesig {}
//     .method public static int32 Loop(int32 n) {
//       .maxstack 8 .locals init (int32 V_0, string V_1)
//       ldstr "hi" stloc.1 ldc.i4.0 stloc.0 br.s L_cond
//     L_body: ldloc.0 ldc.i4.1 add stloc.0
//     L_cond: ldloc.0 ldarg.0 blt.s L_body
//       .try { ldloc.1 call void [mscorlib]System.Console::WriteLine(string)
//              leave.s L_end }
//       catch [mscorlib]System.Exception { pop leave.s L_end }
//     L_end: call !!0[] [mscorlib]System.Array::Empty<int32>() pop
//       ldtoken int32[] pop ldsfld int32 'counter' pop ldloc.0 ret }
//   }
const char RtListing[] = R"canon(pe PE32+ machine=0x8664 dll subsystem=3
cor flags=0x00000001 runtime=2.5 metadata-version=v4.0.30319
assembly 'rt' ver 1.2.3.4 flags=0x00000000 hash=0x00008004 culture='' key=-
module 'rt.dll'
assemblyref 'mscorlib' ver 4.0.0.0 flags=0x00000000 key=B7 7A 5C 56 19 34 E0 89 culture=''
moduleref 'kernel32'

.class '<Module>' flags=0x00000000
  .field 'blob' valuetype 'NS.Blk' flags=0x0116
    .data 16 bytes
      01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10
  .field 'counter' int32 flags=0x0113
    .data 4 bytes
      07 00 00 00

.class 'NS.Blk' flags=0x00000111 extends [mscorlib]System.ValueType
  .layout pack 1 size 16
  .field 'a' int32 flags=0x0006 offset 4

.class 'NS.Holder' flags=0x00100001 extends [mscorlib]System.Object
  .field '_value' int32 flags=0x0001
  .property 'Value' instance int32() flags=0x0000
    .get instance int32() 'NS.Holder'::'get_Value'
    .set instance void(int32) 'NS.Holder'::'set_Value'
  .method '.ctor' instance void() flags=0x1886 impl=0x0000
    .maxstack 8
    L0: ldarg.0
    L1: call instance void() [mscorlib]System.Object::'.ctor'
    L2: ret
  .method 'Add' int32(int32, int32) flags=0x0096 impl=0x0000
    .param 1 'a' flags=0x0000
    .param 2 'b' flags=0x0000
    .custom instance void() [mscorlib]System.ObsoleteAttribute::'.ctor' = 01 00 00 00
    .maxstack 8
    L0: ldarg.0
    L1: ldarg.1
    L2: add
    L3: ret
  .method 'Loop' int32(int32) flags=0x0016 impl=0x0000
    .param 1 'n' flags=0x0000
    .maxstack 8 initlocals
    .locals (int32, string)
    L0: ldstr "hi"
    L1: stloc.1
    L2: ldc.i4.0
    L3: stloc.0
    L4: br.s L9
    L5: ldloc.0
    L6: ldc.i4.1
    L7: add
    L8: stloc.0
    L9: ldloc.0
    L10: ldarg.0
    L11: blt.s L5
    L12: ldloc.1
    L13: call void(string) [mscorlib]System.Console::'WriteLine'
    L14: leave.s L17
    L15: pop
    L16: leave.s L17
    L17: call <1> !!0[]() [mscorlib]System.Array::'Empty'<int32>
    L18: pop
    L19: ldtoken int32[]
    L20: pop
    L21: ldsfld int32 '<Module>'::'counter'
    L22: pop
    L23: ldloc.0
    L24: ret
    .try L12 to L15 catch [mscorlib]System.Exception handler L15 to L17
  .method 'Sleep' void(int32) flags=0x2016 impl=0x0080
    .pinvokeimpl 'kernel32' entry 'Sleep' flags=0x0200
    .param 1 'ms' flags=0x0000
  .method 'get_Value' instance int32() flags=0x0886 impl=0x0000
    .maxstack 8
    L0: ldarg.0
    L1: ldfld int32 'NS.Holder'::'_value'
    L2: ret
  .method 'set_Value' instance void(int32) flags=0x0886 impl=0x0000
    .param 1 'v' flags=0x0000
    .maxstack 8
    L0: ldarg.0
    L1: ldarg.1
    L2: stfld int32 'NS.Holder'::'_value'
    L3: ret

.class 'NS.Holder/NestedT' flags=0x00000002 extends [mscorlib]System.Object
assembly .custom instance void(int32) [mscorlib]System.Runtime.CompilerServices.CompilationRelaxationsAttribute::'.ctor' = 01 00 08 00 00 00 00 00
module .custom instance void(bool) [mscorlib]System.CLSCompliantAttribute::'.ctor' = 01 00 01 00 00
)canon";

// mdcanon's listing of the ilasm image (-exe) of
//   .assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 )
//                               .ver 4:0:0:0 }
//   .assembly hello { .ver 0:0:0:0 }
//   .module hello.exe
//   .method public static int32 main() cil managed
//   { .entrypoint .maxstack 8 ldc.i4.s 42 ret }
// (ilasm writes hash algorithm 0 without .hash algorithm).
const char HelloListing[] = R"canon(pe PE32+ machine=0x8664 exe subsystem=3
cor flags=0x00000001 runtime=2.5 metadata-version=v4.0.30319
entrypoint int32() '<Module>'::'main'
assembly 'hello' ver 0.0.0.0 flags=0x00000000 hash=0x00000000 culture='' key=-
module 'hello.exe'
assemblyref 'mscorlib' ver 4.0.0.0 flags=0x00000000 key=B7 7A 5C 56 19 34 E0 89 culture=''

.class '<Module>' flags=0x00000000
  .method 'main' int32() flags=0x0016 impl=0x0000
    .maxstack 8
    L0: ldc.i4.s 42
    L1: ret
)canon";

// mdcanon's listing of the ilasm image (-dll, no .assembly: a netmodule) of
//   .module 'a.c'
//   .field public static native int 'gv'
//   .method public static int32 'f'() cil managed
//   { .maxstack 8 ldc.i4.1 ret }
const char ObjListing[] = R"canon(pe PE32+ machine=0x8664 dll subsystem=3
cor flags=0x00000001 runtime=2.5 metadata-version=v4.0.30319
module 'a.c'

.class '<Module>' flags=0x00000000
  .field 'gv' native int flags=0x0016
  .method 'f' int32() flags=0x0016 impl=0x0000
    .maxstack 8
    L0: ldc.i4.1
    L1: ret
)canon";

//===----------------------------------------------------------------------===//
// The round-trip library
//===----------------------------------------------------------------------===//

struct Rt {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  ByteVec Mapped;
  ByteVec File;
  BlobContentId Id;
  std::unique_ptr<MetadataReader> R;

  EntityHandle Core, Kernel32;
  EntityHandle TValueType, TObject, TRelax, TCLS, TObsolete, TConsole,
      TException, TArray;
  EntityHandle ObjCtor, WriteLine, Empty, RelaxCtor, CLSCtor, ObsoleteCtor;
  EntityHandle Blk, Holder, NestedT;
  EntityHandle FBlob, FCounter, FA, FValue;
  EntityHandle Ctor, GetValue, SetValue, Add, Sleep, Loop;
  EntityHandle PV, PA, PB, PMs, PN;
  EntityHandle Prop, Locals, IntArray, EmptyInt;
  uint32_t Hi = 0;

  // Signatures as built.
  MethodSig CtorSig, GetSig, SetSig, AddSig, SleepSig, LoopSig, WriteLineSig,
      EmptySig, RelaxSig, CLSSig;
  TypeSig BlkField, I4Field;
  std::vector<TypeSig> LocalTypes, EmptyArgs;
  TypeSig IntArrayType;
  PropertySig ValueSig;

  // Bodies as encoded, by MethodDef token.
  struct Body {
    ByteVec Code;
    std::vector<ExceptionClause> Clauses;
    uint32_t LocalSig = 0;
    bool InitLocals = false;
  };
  std::map<uint32_t, Body> BodyOf;

  uint32_t tok(EntityHandle H) const { return MB.getToken(H); }
};

uint32_t addSig(MetadataBuilder &MB, const MethodSig &S) {
  SmallVector<uint8_t, 16> B;
  encodeMethodSig(S, B);
  return MB.addBlob(B);
}

uint32_t addFieldSig(MetadataBuilder &MB, const TypeSig &T) {
  SmallVector<uint8_t, 8> B;
  encodeFieldSig(T, B);
  return MB.addBlob(B);
}

MethodSig methodSig(uint8_t Header, TypeSig Ret, std::vector<TypeSig> Params) {
  MethodSig S;
  S.Header = Header;
  S.Return = Ret;
  S.Params = std::move(Params);
  return S;
}

void addBody(Rt &M, ILBuilder &B, EntityHandle Method, uint32_t LocalSig = 0,
             bool InitLocals = false) {
  ASSERT_THAT_ERROR(B.layout(), Succeeded());
  SmallVector<uint8_t, 64> Code;
  SmallVector<ExceptionClause, 2> Clauses;
  ASSERT_THAT_ERROR(
      B.encode(Code, Clauses, [&](TokenRef T) { return M.MB.resolveToken(T); }),
      Succeeded());
  MethodBodyInfo I;
  I.Code = Code;
  I.MaxStack = 8;
  I.LocalVarSigToken = LocalSig;
  I.InitLocals = InitLocals;
  I.Clauses = Clauses;
  M.MB.setMethodBodyOffset(Method, M.Bodies.addBody(I));
  Rt::Body &E = M.BodyOf[M.tok(Method)];
  E.Code.assign(Code.begin(), Code.end());
  E.Clauses.assign(Clauses.begin(), Clauses.end());
  E.LocalSig = LocalSig;
  E.InitLocals = InitLocals;
}

std::unique_ptr<Rt> buildRt() {
  auto M = std::make_unique<Rt>();
  MetadataBuilder &MB = M->MB;
  MB.setModuleName("rt.dll");
  AssemblyIdentity Id;
  Id.Name = "rt";
  Id.Version = {1, 2, 3, 4};
  Id.HashAlg = AssemblyHashAlgorithm::SHA1;
  MB.setAssembly(Id);
  AssemblyRefInfo Core;
  Core.Name = "mscorlib";
  Core.Version = {4, 0, 0, 0};
  Core.PublicKeyOrToken = {0xB7, 0x7A, 0x5C, 0x56, 0x19, 0x34, 0xE0, 0x89};
  M->Core = MB.addAssemblyRef(Core);
  M->Kernel32 = MB.addModuleRef("kernel32");
  M->TValueType = MB.addTypeRef(M->Core, "System", "ValueType");
  M->TObject = MB.addTypeRef(M->Core, "System", "Object");
  M->TRelax = MB.addTypeRef(M->Core, "System.Runtime.CompilerServices",
                            "CompilationRelaxationsAttribute");
  M->TCLS = MB.addTypeRef(M->Core, "System", "CLSCompliantAttribute");
  M->TObsolete = MB.addTypeRef(M->Core, "System", "ObsoleteAttribute");
  M->TConsole = MB.addTypeRef(M->Core, "System", "Console");
  M->TException = MB.addTypeRef(M->Core, "System", "Exception");
  M->TArray = MB.addTypeRef(M->Core, "System", "Array");

  const TypeSig Void, I4 = TypeSig::get(ELEMENT_TYPE_I4),
                      Str = TypeSig::get(ELEMENT_TYPE_STRING),
                      Bool = TypeSig::get(ELEMENT_TYPE_BOOLEAN);
  const uint8_t HasThis = SIG_DEFAULT | SIG_HASTHIS;
  M->CtorSig = methodSig(HasThis, Void, {});
  M->GetSig = methodSig(HasThis, I4, {});
  M->SetSig = methodSig(HasThis, Void, {I4});
  M->AddSig = methodSig(SIG_DEFAULT, I4, {I4, I4});
  M->SleepSig = methodSig(SIG_DEFAULT, Void, {I4});
  M->LoopSig = methodSig(SIG_DEFAULT, I4, {I4});
  M->WriteLineSig = methodSig(SIG_DEFAULT, Void, {Str});
  M->EmptySig =
      methodSig(SIG_DEFAULT | SIG_GENERIC,
                TypeSig::getSZArray(TypeSig::getGenericParam(true, 0)), {});
  M->EmptySig.GenericParamCount = 1;
  M->RelaxSig = methodSig(HasThis, Void, {I4});
  M->CLSSig = methodSig(HasThis, Void, {Bool});

  M->ObjCtor = MB.addMemberRef(M->TObject, ".ctor", addSig(MB, M->CtorSig));
  M->RelaxCtor = MB.addMemberRef(M->TRelax, ".ctor", addSig(MB, M->RelaxSig));
  M->CLSCtor = MB.addMemberRef(M->TCLS, ".ctor", addSig(MB, M->CLSSig));
  M->ObsoleteCtor =
      MB.addMemberRef(M->TObsolete, ".ctor", addSig(MB, M->CtorSig));
  M->WriteLine =
      MB.addMemberRef(M->TConsole, "WriteLine", addSig(MB, M->WriteLineSig));
  M->Empty = MB.addMemberRef(M->TArray, "Empty", addSig(MB, M->EmptySig));
  M->EmptyArgs = {I4};
  {
    SmallVector<uint8_t, 8> B;
    encodeMethodSpec(M->EmptyArgs, B);
    M->EmptyInt = MB.addMethodSpec(M->Empty, MB.addBlob(B));
  }
  M->IntArrayType = TypeSig::getSZArray(I4);
  {
    SmallVector<uint8_t, 8> B;
    encodeTypeSpec(M->IntArrayType, B);
    M->IntArray = MB.addTypeSpec(MB.addBlob(B));
  }
  M->LocalTypes = {I4, Str};
  {
    SmallVector<uint8_t, 8> B;
    encodeLocalSig(M->LocalTypes, B);
    M->Locals = MB.addStandAloneSig(MB.addBlob(B));
  }

  EntityHandle Mod = MB.getModuleType();
  M->Blk =
      MB.addTypeDef(TypeAttributes::Public | TypeAttributes::ExplicitLayout |
                        TypeAttributes::Sealed,
                    "NS", "Blk", M->TValueType);
  M->Holder =
      MB.addTypeDef(TypeAttributes::Public | TypeAttributes::BeforeFieldInit,
                    "NS", "Holder", M->TObject);
  M->NestedT = MB.addTypeDef(TypeAttributes::NestedPublic, "", "NestedT",
                             M->TObject, M->Holder);

  M->BlkField = TypeSig::getValueType(M->tok(M->Blk));
  M->I4Field = I4;
  M->FBlob = MB.addField(Mod,
                         FieldAttributes::Public | FieldAttributes::Static |
                             FieldAttributes::HasFieldRVA,
                         "blob", addFieldSig(MB, M->BlkField));
  M->FCounter =
      MB.addField(Mod,
                  FieldAttributes::Assembly | FieldAttributes::Static |
                      FieldAttributes::HasFieldRVA,
                  "counter", addFieldSig(MB, I4));
  M->FA =
      MB.addField(M->Blk, FieldAttributes::Public, "a", addFieldSig(MB, I4));
  M->FValue = MB.addField(M->Holder, FieldAttributes::Private, "_value",
                          addFieldSig(MB, I4));
  MB.addClassLayout(M->Blk, 1, 16);
  MB.addFieldLayout(M->FA, 4);
  M->Mapped = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
               0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x07, 0x00, 0x00, 0x00};
  MB.addFieldRVA(M->FBlob, 0);
  MB.addFieldRVA(M->FCounter, 16);

  const MethodAttributes Accessor = MethodAttributes::Public |
                                    MethodAttributes::HideBySig |
                                    MethodAttributes::SpecialName;
  const MethodImplAttributes IL =
      MethodImplAttributes::IL | MethodImplAttributes::Managed;
  M->Ctor = MB.addMethod(M->Holder, Accessor | MethodAttributes::RTSpecialName,
                         IL, ".ctor", addSig(MB, M->CtorSig));
  M->GetValue =
      MB.addMethod(M->Holder, Accessor, IL, "get_Value", addSig(MB, M->GetSig));
  M->SetValue =
      MB.addMethod(M->Holder, Accessor, IL, "set_Value", addSig(MB, M->SetSig));
  M->PV = MB.addParam(M->SetValue, 1, ParamAttributes::None, "v");
  M->Add = MB.addMethod(M->Holder,
                        MethodAttributes::Public | MethodAttributes::Static |
                            MethodAttributes::HideBySig,
                        IL, "Add", addSig(MB, M->AddSig));
  M->PA = MB.addParam(M->Add, 1, ParamAttributes::None, "a");
  M->PB = MB.addParam(M->Add, 2, ParamAttributes::None, "b");
  M->Sleep = MB.addMethod(M->Holder,
                          MethodAttributes::Public | MethodAttributes::Static |
                              MethodAttributes::PinvokeImpl,
                          MethodImplAttributes::PreserveSig, "Sleep",
                          addSig(MB, M->SleepSig));
  M->PMs = MB.addParam(M->Sleep, 1, ParamAttributes::None, "ms");
  MB.addImplMap(M->Sleep, PInvokeAttributes::CallConvCdecl, "Sleep",
                M->Kernel32);
  M->Loop = MB.addMethod(M->Holder,
                         MethodAttributes::Public | MethodAttributes::Static,
                         IL, "Loop", addSig(MB, M->LoopSig));
  M->PN = MB.addParam(M->Loop, 1, ParamAttributes::None, "n");

  M->ValueSig.HasThis = true;
  M->ValueSig.Type = I4;
  {
    SmallVector<uint8_t, 8> B;
    encodePropertySig(M->ValueSig, B);
    M->Prop = MB.addProperty(M->Holder, PropertyAttributes::None, "Value",
                             MB.addBlob(B));
  }
  MB.addMethodSemantics(M->Prop, MethodSemanticsAttributes::Getter,
                        M->GetValue);
  MB.addMethodSemantics(M->Prop, MethodSemanticsAttributes::Setter,
                        M->SetValue);

  MB.addCustomAttribute(rowHandle(TableId::Assembly, 1), M->RelaxCtor,
                        MB.addBlob(bytes({1, 0, 8, 0, 0, 0, 0, 0})));
  MB.addCustomAttribute(rowHandle(TableId::Module, 1), M->CLSCtor,
                        MB.addBlob(bytes({1, 0, 1, 0, 0})));
  MB.addCustomAttribute(M->Add, M->ObsoleteCtor,
                        MB.addBlob(bytes({1, 0, 0, 0})));
  M->Hi = MB.addUserString("hi");

  EXPECT_THAT_ERROR(MB.finalize(M->Bodies), Succeeded());

  // Bodies, encoded after finalize() numbered the member rows.
  {
    ILBuilder B(false);
    B.emitLdarg(0);
    B.emitCall(Opcode::Call, TokenRef::getHandle(M->ObjCtor), 1, 0);
    B.emitRet();
    addBody(*M, B, M->Ctor);
  }
  {
    ILBuilder B(true);
    B.emitLdarg(0);
    B.emitToken(Opcode::Ldfld, TokenRef::getHandle(M->FValue));
    B.emitRet();
    addBody(*M, B, M->GetValue);
  }
  {
    ILBuilder B(false);
    B.emitLdarg(0);
    B.emitLdarg(1);
    B.emitToken(Opcode::Stfld, TokenRef::getHandle(M->FValue));
    B.emitRet();
    addBody(*M, B, M->SetValue);
  }
  {
    ILBuilder B(true);
    B.emitLdarg(0);
    B.emitLdarg(1);
    B.emit(Opcode::Add);
    B.emitRet();
    addBody(*M, B, M->Add);
  }
  {
    ILBuilder B(true);
    ILLabel Body = B.createLabel(), Cond = B.createLabel(),
            TryBegin = B.createLabel(), Handler = B.createLabel(),
            End = B.createLabel();
    B.emitToken(Opcode::Ldstr, TokenRef::getToken(M->Hi));
    B.emitStloc(1);
    B.emitLdcI4(0);
    B.emitStloc(0);
    B.emitBranch(Opcode::Br_s, Cond);
    B.placeLabel(Body);
    B.emitLdloc(0);
    B.emitLdcI4(1);
    B.emit(Opcode::Add);
    B.emitStloc(0);
    B.placeLabel(Cond);
    B.emitLdloc(0);
    B.emitLdarg(0);
    B.emitBranch(Opcode::Blt_s, Body);
    B.placeLabel(TryBegin);
    B.emitLdloc(1);
    B.emitCall(Opcode::Call, TokenRef::getHandle(M->WriteLine), 1, 0);
    B.emitBranch(Opcode::Leave_s, End);
    B.placeLabel(Handler);
    B.emit(Opcode::Pop);
    B.emitBranch(Opcode::Leave_s, End);
    B.placeLabel(End);
    B.emitCall(Opcode::Call, TokenRef::getHandle(M->EmptyInt), 0, 1);
    B.emit(Opcode::Pop);
    B.emitToken(Opcode::Ldtoken, TokenRef::getHandle(M->IntArray));
    B.emit(Opcode::Pop);
    B.emitToken(Opcode::Ldsfld, TokenRef::getHandle(M->FCounter));
    B.emit(Opcode::Pop);
    B.emitLdloc(0);
    B.emitRet();
    ILExceptionClause C;
    C.Kind = ExceptionClauseKind::Catch;
    C.TryBegin = TryBegin;
    C.TryEnd = Handler;
    C.HandlerBegin = Handler;
    C.HandlerEnd = End;
    C.CatchType = TokenRef::getHandle(M->TException);
    B.addExceptionClause(C);
    addBody(*M, B, M->Loop, M->tok(M->Locals), /*InitLocals=*/true);
  }

  Expected<PEOptions> O = PEOptions::get(Triple("x86_64-pc-windows-msvc-msil"),
                                         PEImageKind::Library);
  EXPECT_THAT_EXPECTED(O, Succeeded());
  if (!O)
    return M;
  PEContent C;
  C.Metadata = &MB;
  C.ILStream = M->Bodies.getBytes();
  C.MappedFieldData = M->Mapped;
  SmallVector<uint8_t, 0> Out;
  EXPECT_THAT_ERROR(writePEImage(*O, C, Out, &M->Id), Succeeded());
  M->File.assign(Out.begin(), Out.end());
  Expected<std::unique_ptr<MetadataReader>> R = MetadataReader::create(M->File);
  EXPECT_THAT_EXPECTED(R, Succeeded());
  if (R)
    M->R = std::move(*R);
  return M;
}

// The library, built once.
const Rt &rt() {
  static std::unique_ptr<Rt> M = buildRt();
  return *M;
}

#define REQUIRE_READER(M)                                                      \
  do {                                                                         \
    ASSERT_TRUE((M).R) << "the round-trip library did not read back";          \
  } while (false)

ByteVec blobAt(const MetadataReader &R, uint32_t Offset) {
  Expected<ArrayRef<uint8_t>> B = R.getBlob(Offset);
  EXPECT_THAT_EXPECTED(B, Succeeded());
  return B ? ByteVec(B->begin(), B->end()) : ByteVec();
}

ByteVec encoded(const MethodSig &S) {
  SmallVector<uint8_t, 16> B;
  encodeMethodSig(S, B);
  return ByteVec(B.begin(), B.end());
}

ByteVec encodedField(const TypeSig &T) {
  SmallVector<uint8_t, 8> B;
  encodeFieldSig(T, B);
  return ByteVec(B.begin(), B.end());
}

//===----------------------------------------------------------------------===//
// Tables
//===----------------------------------------------------------------------===//

// The row counts, for every table: the 22 tables of the writer set are all
// present.
TEST(CLIRoundTripTest, RowCounts) {
  const Rt &M = rt();
  REQUIRE_READER(M);
  const MetadataReader &R = *M.R;
  for (unsigned T = 0;
       T <= static_cast<unsigned>(TableId::GenericParamConstraint); ++T) {
    TableId Id = static_cast<TableId>(T);
    EXPECT_EQ(R.getRowCount(Id), M.MB.getRowCount(Id))
        << getTableName(Id).str();
  }
  const TableId WriterSet[] = {
      TableId::Module,      TableId::TypeRef,         TableId::TypeDef,
      TableId::Field,       TableId::MethodDef,       TableId::Param,
      TableId::MemberRef,   TableId::CustomAttribute, TableId::ClassLayout,
      TableId::FieldLayout, TableId::StandAloneSig,   TableId::PropertyMap,
      TableId::Property,    TableId::MethodSemantics, TableId::ModuleRef,
      TableId::TypeSpec,    TableId::ImplMap,         TableId::FieldRVA,
      TableId::Assembly,    TableId::AssemblyRef,     TableId::NestedClass,
      TableId::MethodSpec};
  for (TableId T : WriterSet)
    EXPECT_GT(R.getRowCount(T), 0u) << getTableName(T).str();
  EXPECT_EQ(R.getRowCount(TableId::TypeDef), 4u);
  EXPECT_EQ(R.getRowCount(TableId::Field), 4u);
  EXPECT_EQ(R.getRowCount(TableId::MethodDef), 6u);
  EXPECT_EQ(R.getRowCount(TableId::Param), 5u);
  EXPECT_EQ(R.getRowCount(TableId::MemberRef), 6u);
  EXPECT_EQ(R.getRowCount(TableId::TypeRef), 8u);
  EXPECT_EQ(R.getRowCount(TableId::CustomAttribute), 3u);
  EXPECT_EQ(R.getRowCount(TableId::MethodSemantics), 2u);
  EXPECT_EQ(R.getRowCount(TableId::FieldRVA), 2u);
}

// Every cell of every present table reads; heap cells resolve in their heap
// and index cells are tokens of the column's tables.
TEST(CLIRoundTripTest, EveryColumnReads) {
  const Rt &M = rt();
  REQUIRE_READER(M);
  const MetadataReader &R = *M.R;
  for (unsigned T = 0;
       T <= static_cast<unsigned>(TableId::GenericParamConstraint); ++T) {
    TableId Id = static_cast<TableId>(T);
    ArrayRef<ColumnInfo> Cols = getTableColumns(Id);
    for (uint32_t Row = 1; Row <= R.getRowCount(Id); ++Row)
      for (unsigned C = 0; C < Cols.size(); ++C) {
        SCOPED_TRACE(getTableName(Id).str() + "[" + std::to_string(Row) + "]." +
                     Cols[C].Name.str());
        Expected<uint32_t> V = R.getColumnValue(Id, Row, C);
        ASSERT_THAT_EXPECTED(V, Succeeded());
        switch (Cols[C].Kind) {
        case ColumnKind::StringIdx:
          EXPECT_THAT_EXPECTED(R.getString(*V), Succeeded());
          break;
        case ColumnKind::BlobIdx:
          EXPECT_THAT_EXPECTED(R.getBlob(*V), Succeeded());
          break;
        case ColumnKind::GuidIdx:
          if (*V)
            EXPECT_THAT_EXPECTED(R.getGuid(*V), Succeeded());
          break;
        case ColumnKind::TableIdx:
          if (*V)
            EXPECT_EQ(getTokenType(*V), Cols[C].Target);
          break;
        case ColumnKind::CodedIdx: {
          if (*V == 0)
            break;
          ArrayRef<std::optional<TableId>> Tables =
              getCodedIndexTables(static_cast<CodedIndexKind>(Cols[C].Target));
          EXPECT_TRUE(llvm::any_of(Tables, [&](std::optional<TableId> X) {
            return X && static_cast<uint8_t>(*X) == getTokenType(*V);
          }));
          EXPECT_NE(getTokenRow(*V), 0u);
          EXPECT_LE(getTokenRow(*V),
                    R.getRowCount(static_cast<TableId>(getTokenType(*V))));
          break;
        }
        default:
          break;
        }
      }
  }
}

TEST(CLIRoundTripTest, ModuleAssemblyAndReferences) {
  const Rt &M = rt();
  REQUIRE_READER(M);
  const MetadataReader &R = *M.R;
  EXPECT_TRUE(R.isAssembly());
  EXPECT_EQ(R.getVersionString(), "v4.0.30319");
  Expected<ModuleRow> Mod = R.getModule();
  ASSERT_THAT_EXPECTED(Mod, Succeeded());
  EXPECT_EQ(Mod->Generation, 0);
  EXPECT_EQ(Mod->Name, "rt.dll");
  EXPECT_THAT_EXPECTED(R.getGuid(Mod->Mvid), HasValue(M.Id.Guid));
  EXPECT_THAT_EXPECTED(R.getColumnValue(TableId::Module, 1, 3), HasValue(0u));
  EXPECT_THAT_EXPECTED(R.getColumnValue(TableId::Module, 1, 4), HasValue(0u));

  Expected<AssemblyRow> A = R.getAssembly();
  ASSERT_THAT_EXPECTED(A, Succeeded());
  EXPECT_EQ(A->Name, "rt");
  EXPECT_EQ(A->Culture, "");
  EXPECT_EQ(A->Version, (AssemblyVersion{1, 2, 3, 4}));
  EXPECT_EQ(A->HashAlg, AssemblyHashAlgorithm::SHA1);
  EXPECT_EQ(static_cast<uint32_t>(A->Flags), 0u);
  EXPECT_EQ(blobAt(R, A->PublicKey), ByteVec());

  Expected<AssemblyRefRow> Ref = R.getAssemblyRef(rowOf(M.tok(M.Core)));
  ASSERT_THAT_EXPECTED(Ref, Succeeded());
  EXPECT_EQ(Ref->Name, "mscorlib");
  EXPECT_EQ(Ref->Culture, "");
  EXPECT_EQ(Ref->Version, (AssemblyVersion{4, 0, 0, 0}));
  EXPECT_EQ(static_cast<uint32_t>(Ref->Flags), 0u);
  EXPECT_EQ(blobAt(R, Ref->PublicKeyOrToken),
            bytes({0xB7, 0x7A, 0x5C, 0x56, 0x19, 0x34, 0xE0, 0x89}));
  EXPECT_EQ(blobAt(R, Ref->HashValue), ByteVec());

  Expected<ModuleRefRow> MR = R.getModuleRef(rowOf(M.tok(M.Kernel32)));
  ASSERT_THAT_EXPECTED(MR, Succeeded());
  EXPECT_EQ(MR->Name, "kernel32");

  struct {
    EntityHandle H;
    const char *NS, *Name;
  } TypeRefs[] = {{M.TValueType, "System", "ValueType"},
                  {M.TObject, "System", "Object"},
                  {M.TRelax, "System.Runtime.CompilerServices",
                   "CompilationRelaxationsAttribute"},
                  {M.TCLS, "System", "CLSCompliantAttribute"},
                  {M.TObsolete, "System", "ObsoleteAttribute"},
                  {M.TConsole, "System", "Console"},
                  {M.TException, "System", "Exception"},
                  {M.TArray, "System", "Array"}};
  for (const auto &T : TypeRefs) {
    SCOPED_TRACE(T.Name);
    Expected<TypeRefRow> Row = R.getTypeRef(rowOf(M.tok(T.H)));
    ASSERT_THAT_EXPECTED(Row, Succeeded());
    EXPECT_EQ(Row->ResolutionScope, M.tok(M.Core));
    EXPECT_EQ(Row->Namespace, T.NS);
    EXPECT_EQ(Row->Name, T.Name);
  }
}

TEST(CLIRoundTripTest, TypeDefsAndNesting) {
  const Rt &M = rt();
  REQUIRE_READER(M);
  const MetadataReader &R = *M.R;
  // Member runs follow the TypeDefs: the Field and MethodDef rows owned by
  // each type are [Begin, End), in insertion order within the owner.
  struct {
    EntityHandle H;
    TypeAttributes Flags;
    const char *NS, *Name;
    uint32_t Extends;
    std::vector<EntityHandle> Fields, Methods;
  } Types[] = {{M.MB.getModuleType(),
                TypeAttributes(),
                "",
                "<Module>",
                0,
                {M.FBlob, M.FCounter},
                {}},
               {M.Blk,
                TypeAttributes::Public | TypeAttributes::ExplicitLayout |
                    TypeAttributes::Sealed,
                "NS",
                "Blk",
                M.tok(M.TValueType),
                {M.FA},
                {}},
               {M.Holder,
                TypeAttributes::Public | TypeAttributes::BeforeFieldInit,
                "NS",
                "Holder",
                M.tok(M.TObject),
                {M.FValue},
                {M.Ctor, M.GetValue, M.SetValue, M.Add, M.Sleep, M.Loop}},
               {M.NestedT,
                TypeAttributes::NestedPublic,
                "",
                "NestedT",
                M.tok(M.TObject),
                {},
                {}}};
  uint32_t NextField = 1, NextMethod = 1;
  for (const auto &T : Types) {
    SCOPED_TRACE(T.Name);
    Expected<TypeDefRow> Row = R.getTypeDef(rowOf(M.tok(T.H)));
    ASSERT_THAT_EXPECTED(Row, Succeeded());
    EXPECT_EQ(Row->Flags, T.Flags);
    EXPECT_EQ(Row->Namespace, T.NS);
    EXPECT_EQ(Row->Name, T.Name);
    EXPECT_EQ(Row->Extends, T.Extends);
    EXPECT_EQ(Row->FieldBegin, NextField);
    EXPECT_EQ(Row->FieldEnd, NextField + T.Fields.size());
    EXPECT_EQ(Row->MethodBegin, NextMethod);
    EXPECT_EQ(Row->MethodEnd, NextMethod + T.Methods.size());
    for (unsigned I = 0; I < T.Fields.size(); ++I)
      EXPECT_EQ(M.tok(T.Fields[I]), makeToken(TableId::Field, NextField + I));
    for (unsigned I = 0; I < T.Methods.size(); ++I)
      EXPECT_EQ(M.tok(T.Methods[I]),
                makeToken(TableId::MethodDef, NextMethod + I));
    NextField += T.Fields.size();
    NextMethod += T.Methods.size();
  }
  Expected<NestedClassRow> N = R.getNestedClass(1);
  ASSERT_THAT_EXPECTED(N, Succeeded());
  EXPECT_EQ(N->NestedClass, M.tok(M.NestedT));
  EXPECT_EQ(N->EnclosingClass, M.tok(M.Holder));
  EXPECT_THAT_EXPECTED(R.getEnclosingType(M.tok(M.NestedT)),
                       HasValue(M.tok(M.Holder)));
  EXPECT_THAT_EXPECTED(R.getTypeFullName(M.tok(M.NestedT)),
                       HasValue("NS.Holder/NestedT"));
}

TEST(CLIRoundTripTest, FieldsLayoutAndData) {
  const Rt &M = rt();
  REQUIRE_READER(M);
  const MetadataReader &R = *M.R;
  struct {
    EntityHandle H;
    FieldAttributes Flags;
    const char *Name;
    TypeSig Type;
  } Fields[] = {{M.FBlob,
                 FieldAttributes::Public | FieldAttributes::Static |
                     FieldAttributes::HasFieldRVA,
                 "blob", M.BlkField},
                {M.FCounter,
                 FieldAttributes::Assembly | FieldAttributes::Static |
                     FieldAttributes::HasFieldRVA,
                 "counter", M.I4Field},
                {M.FA, FieldAttributes::Public, "a", M.I4Field},
                {M.FValue, FieldAttributes::Private, "_value", M.I4Field}};
  for (const auto &F : Fields) {
    SCOPED_TRACE(F.Name);
    Expected<FieldRow> Row = R.getField(rowOf(M.tok(F.H)));
    ASSERT_THAT_EXPECTED(Row, Succeeded());
    EXPECT_EQ(Row->Flags, F.Flags);
    EXPECT_EQ(Row->Name, F.Name);
    EXPECT_EQ(blobAt(R, Row->Signature), encodedField(F.Type));
    EXPECT_THAT_EXPECTED(decodeFieldSig(blobAt(R, Row->Signature)),
                         HasValue(F.Type));
  }

  Expected<ClassLayoutRow> CL = R.getClassLayout(1);
  ASSERT_THAT_EXPECTED(CL, Succeeded());
  EXPECT_EQ(CL->PackingSize, 1);
  EXPECT_EQ(CL->ClassSize, 16u);
  EXPECT_EQ(CL->Parent, M.tok(M.Blk));
  Expected<FieldLayoutRow> FL = R.getFieldLayout(1);
  ASSERT_THAT_EXPECTED(FL, Succeeded());
  EXPECT_EQ(FL->Offset, 4u);
  EXPECT_EQ(FL->Field, M.tok(M.FA));

  // FieldRVA rows are sorted by field; the data comes back byte for byte.
  Expected<FieldRVARow> R1 = R.getFieldRVA(1), R2 = R.getFieldRVA(2);
  ASSERT_THAT_EXPECTED(R1, Succeeded());
  ASSERT_THAT_EXPECTED(R2, Succeeded());
  EXPECT_EQ(R1->Field, M.tok(M.FBlob));
  EXPECT_EQ(R2->Field, M.tok(M.FCounter));
  EXPECT_EQ(R2->RVA, R1->RVA + 16);
  EXPECT_EQ(R1->RVA % 8, 0u);
  Expected<ArrayRef<uint8_t>> D1 = R.getFieldData(M.tok(M.FBlob), 16);
  ASSERT_THAT_EXPECTED(D1, Succeeded());
  EXPECT_EQ(*D1, ArrayRef<uint8_t>(M.Mapped).take_front(16));
  Expected<ArrayRef<uint8_t>> D2 = R.getFieldData(M.tok(M.FCounter), 4);
  ASSERT_THAT_EXPECTED(D2, Succeeded());
  EXPECT_EQ(ByteVec(D2->begin(), D2->end()), bytes({7, 0, 0, 0}));
}

TEST(CLIRoundTripTest, MethodsParamsAndPInvoke) {
  const Rt &M = rt();
  REQUIRE_READER(M);
  const MetadataReader &R = *M.R;
  const MethodAttributes Accessor = MethodAttributes::Public |
                                    MethodAttributes::HideBySig |
                                    MethodAttributes::SpecialName;
  struct {
    EntityHandle H;
    MethodAttributes Flags;
    MethodImplAttributes Impl;
    const char *Name;
    const MethodSig &Sig;
    std::vector<EntityHandle> Params;
  } Methods[] = {{M.Ctor,
                  Accessor | MethodAttributes::RTSpecialName,
                  MethodImplAttributes::IL,
                  ".ctor",
                  M.CtorSig,
                  {}},
                 {M.GetValue,
                  Accessor,
                  MethodImplAttributes::IL,
                  "get_Value",
                  M.GetSig,
                  {}},
                 {M.SetValue,
                  Accessor,
                  MethodImplAttributes::IL,
                  "set_Value",
                  M.SetSig,
                  {M.PV}},
                 {M.Add,
                  MethodAttributes::Public | MethodAttributes::Static |
                      MethodAttributes::HideBySig,
                  MethodImplAttributes::IL,
                  "Add",
                  M.AddSig,
                  {M.PA, M.PB}},
                 {M.Sleep,
                  MethodAttributes::Public | MethodAttributes::Static |
                      MethodAttributes::PinvokeImpl,
                  MethodImplAttributes::PreserveSig,
                  "Sleep",
                  M.SleepSig,
                  {M.PMs}},
                 {M.Loop,
                  MethodAttributes::Public | MethodAttributes::Static,
                  MethodImplAttributes::IL,
                  "Loop",
                  M.LoopSig,
                  {M.PN}}};
  uint32_t NextParam = 1;
  for (const auto &D : Methods) {
    SCOPED_TRACE(D.Name);
    Expected<MethodDefRow> Row = R.getMethodDef(rowOf(M.tok(D.H)));
    ASSERT_THAT_EXPECTED(Row, Succeeded());
    EXPECT_EQ(Row->Flags, D.Flags);
    EXPECT_EQ(Row->ImplFlags, D.Impl);
    EXPECT_EQ(Row->Name, D.Name);
    EXPECT_EQ(blobAt(R, Row->Signature), encoded(D.Sig));
    EXPECT_THAT_EXPECTED(decodeMethodSig(blobAt(R, Row->Signature)),
                         HasValue(D.Sig));
    EXPECT_EQ(Row->RVA == 0, D.H == M.Sleep);
    EXPECT_EQ(Row->ParamBegin, NextParam);
    EXPECT_EQ(Row->ParamEnd, NextParam + D.Params.size());
    NextParam += D.Params.size();
  }
  struct {
    EntityHandle H;
    uint16_t Sequence;
    const char *Name;
  } Params[] = {{M.PV, 1, "v"},
                {M.PA, 1, "a"},
                {M.PB, 2, "b"},
                {M.PMs, 1, "ms"},
                {M.PN, 1, "n"}};
  for (const auto &P : Params) {
    SCOPED_TRACE(P.Name);
    Expected<ParamRow> Row = R.getParam(rowOf(M.tok(P.H)));
    ASSERT_THAT_EXPECTED(Row, Succeeded());
    EXPECT_EQ(Row->Flags, ParamAttributes::None);
    EXPECT_EQ(Row->Sequence, P.Sequence);
    EXPECT_EQ(Row->Name, P.Name);
  }
  Expected<ImplMapRow> IM = R.getImplMap(1);
  ASSERT_THAT_EXPECTED(IM, Succeeded());
  EXPECT_EQ(IM->Flags, PInvokeAttributes::CallConvCdecl);
  EXPECT_EQ(IM->MemberForwarded, M.tok(M.Sleep));
  EXPECT_EQ(IM->ImportName, "Sleep");
  EXPECT_EQ(IM->ImportScope, M.tok(M.Kernel32));
}

TEST(CLIRoundTripTest, MemberRefsSpecsAndStandAloneSigs) {
  const Rt &M = rt();
  REQUIRE_READER(M);
  const MetadataReader &R = *M.R;
  struct {
    EntityHandle H, Parent;
    const char *Name;
    const MethodSig &Sig;
  } Refs[] = {{M.ObjCtor, M.TObject, ".ctor", M.CtorSig},
              {M.RelaxCtor, M.TRelax, ".ctor", M.RelaxSig},
              {M.CLSCtor, M.TCLS, ".ctor", M.CLSSig},
              {M.ObsoleteCtor, M.TObsolete, ".ctor", M.CtorSig},
              {M.WriteLine, M.TConsole, "WriteLine", M.WriteLineSig},
              {M.Empty, M.TArray, "Empty", M.EmptySig}};
  for (const auto &Ref : Refs) {
    SCOPED_TRACE(Ref.Name);
    Expected<MemberRefRow> Row = R.getMemberRef(rowOf(M.tok(Ref.H)));
    ASSERT_THAT_EXPECTED(Row, Succeeded());
    EXPECT_EQ(Row->Parent, M.tok(Ref.Parent));
    EXPECT_EQ(Row->Name, Ref.Name);
    EXPECT_EQ(blobAt(R, Row->Signature), encoded(Ref.Sig));
    EXPECT_THAT_EXPECTED(decodeMethodSig(blobAt(R, Row->Signature)),
                         HasValue(Ref.Sig));
  }
  Expected<MethodSpecRow> MS = R.getMethodSpec(rowOf(M.tok(M.EmptyInt)));
  ASSERT_THAT_EXPECTED(MS, Succeeded());
  EXPECT_EQ(MS->Method, M.tok(M.Empty));
  EXPECT_EQ(blobAt(R, MS->Instantiation), bytes({0x0A, 0x01, 0x08}));
  EXPECT_THAT_EXPECTED(decodeMethodSpec(blobAt(R, MS->Instantiation)),
                       HasValue(M.EmptyArgs));
  Expected<TypeSpecRow> TS = R.getTypeSpec(rowOf(M.tok(M.IntArray)));
  ASSERT_THAT_EXPECTED(TS, Succeeded());
  EXPECT_EQ(blobAt(R, TS->Signature), bytes({0x1D, 0x08}));
  EXPECT_THAT_EXPECTED(decodeTypeSpec(blobAt(R, TS->Signature)),
                       HasValue(M.IntArrayType));
  Expected<StandAloneSigRow> SA = R.getStandAloneSig(rowOf(M.tok(M.Locals)));
  ASSERT_THAT_EXPECTED(SA, Succeeded());
  EXPECT_EQ(blobAt(R, SA->Signature), bytes({0x07, 0x02, 0x08, 0x0E}));
  EXPECT_THAT_EXPECTED(decodeLocalSig(blobAt(R, SA->Signature)),
                       HasValue(M.LocalTypes));
}

TEST(CLIRoundTripTest, PropertiesAndSemantics) {
  const Rt &M = rt();
  REQUIRE_READER(M);
  const MetadataReader &R = *M.R;
  // PropertyMap: Parent, PropertyList.
  EXPECT_THAT_EXPECTED(R.getColumnValue(TableId::PropertyMap, 1, 0),
                       HasValue(M.tok(M.Holder)));
  EXPECT_THAT_EXPECTED(R.getColumnValue(TableId::PropertyMap, 1, 1),
                       HasValue(M.tok(M.Prop)));
  Expected<PropertyRow> P = R.getProperty(rowOf(M.tok(M.Prop)));
  ASSERT_THAT_EXPECTED(P, Succeeded());
  EXPECT_EQ(P->Flags, PropertyAttributes::None);
  EXPECT_EQ(P->Name, "Value");
  EXPECT_EQ(blobAt(R, P->Signature), bytes({0x28, 0x00, 0x08}));
  EXPECT_THAT_EXPECTED(decodePropertySig(blobAt(R, P->Signature)),
                       HasValue(M.ValueSig));
  Expected<std::pair<uint32_t, uint32_t>> Range =
      R.getPropertyRange(M.tok(M.Holder));
  ASSERT_THAT_EXPECTED(Range, Succeeded());
  EXPECT_EQ(Range->first, rowOf(M.tok(M.Prop)));
  EXPECT_EQ(Range->second, rowOf(M.tok(M.Prop)) + 1);
  // MethodSemantics: Semantics, Method, Association; ties keep insertion
  // order (getter, then setter).
  const uint32_t Want[2][3] = {{uint32_t(MethodSemanticsAttributes::Getter),
                                M.tok(M.GetValue), M.tok(M.Prop)},
                               {uint32_t(MethodSemanticsAttributes::Setter),
                                M.tok(M.SetValue), M.tok(M.Prop)}};
  for (uint32_t Row = 1; Row <= 2; ++Row)
    for (unsigned C = 0; C < 3; ++C)
      EXPECT_THAT_EXPECTED(R.getColumnValue(TableId::MethodSemantics, Row, C),
                           HasValue(Want[Row - 1][C]))
          << "row " << Row << " column " << C;
}

TEST(CLIRoundTripTest, CustomAttributes) {
  const Rt &M = rt();
  REQUIRE_READER(M);
  const MetadataReader &R = *M.R;
  struct {
    uint32_t Parent;
    EntityHandle Ctor;
    ByteVec Value;
  } Want[] = {
      {makeToken(TableId::Assembly, 1), M.RelaxCtor,
       bytes({1, 0, 8, 0, 0, 0, 0, 0})},
      {makeToken(TableId::Module, 1), M.CLSCtor, bytes({1, 0, 1, 0, 0})},
      {M.tok(M.Add), M.ObsoleteCtor, bytes({1, 0, 0, 0})}};
  // The table is sorted by the parent's HasCustomAttribute value.
  uint32_t Prev = 0;
  for (uint32_t Row = 1; Row <= R.getRowCount(TableId::CustomAttribute);
       ++Row) {
    Expected<CustomAttributeRow> CA = R.getCustomAttribute(Row);
    ASSERT_THAT_EXPECTED(CA, Succeeded());
    std::optional<uint32_t> Key =
        encodeCodedIndex(CodedIndexKind::HasCustomAttribute, CA->Parent);
    ASSERT_TRUE(Key.has_value());
    EXPECT_GE(*Key, Prev);
    Prev = *Key;
  }
  for (const auto &W : Want) {
    SCOPED_TRACE("parent 0x" + utohexstr(W.Parent));
    Expected<SmallVector<uint32_t, 4>> Rows = R.getCustomAttributes(W.Parent);
    ASSERT_THAT_EXPECTED(Rows, Succeeded());
    ASSERT_EQ(Rows->size(), 1u);
    Expected<CustomAttributeRow> CA = R.getCustomAttribute(rowOf((*Rows)[0]));
    ASSERT_THAT_EXPECTED(CA, Succeeded());
    EXPECT_EQ(CA->Parent, W.Parent);
    EXPECT_EQ(CA->Constructor, M.tok(W.Ctor));
    EXPECT_EQ(blobAt(R, CA->Value), W.Value);
  }
  // The values decode against their constructors.
  Expected<CustomAttributeValue> CLS =
      decodeCustomAttribute(M.CLSSig, Want[1].Value);
  ASSERT_THAT_EXPECTED(CLS, Succeeded());
  ASSERT_EQ(CLS->FixedArgs.size(), 1u);
  EXPECT_EQ(CLS->FixedArgs[0].K, CAValue::Kind::Bool);
  EXPECT_EQ(CLS->FixedArgs[0].Bits, 1u);
  Expected<CustomAttributeValue> Relax =
      decodeCustomAttribute(M.RelaxSig, Want[0].Value);
  ASSERT_THAT_EXPECTED(Relax, Succeeded());
  ASSERT_EQ(Relax->FixedArgs.size(), 1u);
  EXPECT_EQ(Relax->FixedArgs[0].Bits, 8u);
}

TEST(CLIRoundTripTest, MethodBodies) {
  const Rt &M = rt();
  REQUIRE_READER(M);
  const MetadataReader &R = *M.R;
  ASSERT_EQ(M.BodyOf.size(), 5u);
  for (const auto &[Token, Want] : M.BodyOf) {
    SCOPED_TRACE("method 0x" + utohexstr(Token));
    Expected<MethodBodyView> B = R.getMethodBody(Token);
    ASSERT_THAT_EXPECTED(B, Succeeded());
    EXPECT_EQ(B->Code, ArrayRef<uint8_t>(Want.Code));
    EXPECT_EQ(B->MaxStack, 8);
    EXPECT_EQ(B->LocalVarSigToken, Want.LocalSig);
    EXPECT_EQ(B->InitLocals, Want.InitLocals);
    ASSERT_EQ(B->Clauses.size(), Want.Clauses.size());
    for (unsigned I = 0; I < Want.Clauses.size(); ++I) {
      EXPECT_EQ(B->Clauses[I].Kind, Want.Clauses[I].Kind);
      EXPECT_EQ(B->Clauses[I].TryOffset, Want.Clauses[I].TryOffset);
      EXPECT_EQ(B->Clauses[I].TryLength, Want.Clauses[I].TryLength);
      EXPECT_EQ(B->Clauses[I].HandlerOffset, Want.Clauses[I].HandlerOffset);
      EXPECT_EQ(B->Clauses[I].HandlerLength, Want.Clauses[I].HandlerLength);
      EXPECT_EQ(B->Clauses[I].ClassTokenOrFilterOffset,
                Want.Clauses[I].ClassTokenOrFilterOffset);
    }
  }
  const Rt::Body &Loop = M.BodyOf.at(M.tok(M.Loop));
  ASSERT_EQ(Loop.Clauses.size(), 1u);
  EXPECT_EQ(Loop.Clauses[0].ClassTokenOrFilterOffset, M.tok(M.TException));
  EXPECT_EQ(Loop.LocalSig, M.tok(M.Locals));
  // The P/Invoke stub has no body.
  EXPECT_THAT_EXPECTED(R.getMethodBody(M.tok(M.Sleep)), Failed());
  // The ldstr operand is the user string "hi".
  EXPECT_THAT_EXPECTED(R.getUserString(getTokenRow(M.Hi)), HasValue("hi"));
}

//===----------------------------------------------------------------------===//
// dumpCanonical (5.7.10) on what the library wrote
//===----------------------------------------------------------------------===//

std::string canonical(const MetadataReader &R) {
  std::string S;
  raw_string_ostream OS(S);
  EXPECT_THAT_ERROR(dumpCanonical(R, OS), Succeeded());
  OS.flush();
  return S;
}

TEST(CLIRoundTripTest, CanonicalListingOfLibrary) {
  const Rt &M = rt();
  REQUIRE_READER(M);
  EXPECT_EQ(canonical(*M.R), RtListing);
}

TEST(CLIRoundTripTest, CanonicalListingOfExecutable) {
  MetadataBuilder MB;
  MB.setModuleName("hello.exe");
  AssemblyIdentity Id;
  Id.Name = "hello";
  Id.HashAlg = AssemblyHashAlgorithm::None;
  MB.setAssembly(Id);
  AssemblyRefInfo Core;
  Core.Name = "mscorlib";
  Core.Version = {4, 0, 0, 0};
  Core.PublicKeyOrToken = {0xB7, 0x7A, 0x5C, 0x56, 0x19, 0x34, 0xE0, 0x89};
  MB.addAssemblyRef(Core);
  EntityHandle Main = MB.addMethod(
      MB.getModuleType(), MethodAttributes::Public | MethodAttributes::Static,
      MethodImplAttributes::IL, "main", MB.addBlob(bytes({0x00, 0x00, 0x08})));
  MethodBodyStream Bodies;
  const uint8_t Code[] = {0x1F, 0x2A, 0x2A};
  MethodBodyInfo B;
  B.Code = Code;
  MB.setMethodBodyOffset(Main, Bodies.addBody(B));
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  Expected<PEOptions> O = PEOptions::get(Triple("x86_64-pc-windows-msvc-msil"),
                                         PEImageKind::Executable);
  ASSERT_THAT_EXPECTED(O, Succeeded());
  PEContent C;
  C.Metadata = &MB;
  C.ILStream = Bodies.getBytes();
  C.EntryPointToken = MB.getToken(Main);
  SmallVector<uint8_t, 0> File;
  ASSERT_THAT_ERROR(writePEImage(*O, C, File), Succeeded());
  Expected<std::unique_ptr<MetadataReader>> R = MetadataReader::create(File);
  ASSERT_THAT_EXPECTED(R, Succeeded());
  EXPECT_EQ(canonical(**R), HelloListing);
}

TEST(CLIRoundTripTest, CanonicalListingOfNetmodule) {
  MetadataBuilder MB;
  MB.setModuleName("a.c");
  MB.addField(MB.getModuleType(),
              FieldAttributes::Public | FieldAttributes::Static, "gv",
              MB.addBlob(bytes({0x06, 0x18})));
  EntityHandle F = MB.addMethod(
      MB.getModuleType(), MethodAttributes::Public | MethodAttributes::Static,
      MethodImplAttributes::IL, "f", MB.addBlob(bytes({0x00, 0x00, 0x08})));
  MethodBodyStream Bodies;
  const uint8_t Code[] = {0x17, 0x2A};
  MethodBodyInfo B;
  B.Code = Code;
  MB.setMethodBodyOffset(F, Bodies.addBody(B));
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  Expected<PEOptions> O = PEOptions::get(
      Triple("x86_64-unknown-linux-gnu-msil"), PEImageKind::Module);
  ASSERT_THAT_EXPECTED(O, Succeeded());
  PEContent C;
  C.Metadata = &MB;
  C.ILStream = Bodies.getBytes();
  SmallVector<uint8_t, 0> File;
  ASSERT_THAT_ERROR(writePEImage(*O, C, File), Succeeded());
  Expected<std::unique_ptr<MetadataReader>> R = MetadataReader::create(File);
  ASSERT_THAT_EXPECTED(R, Succeeded());
  EXPECT_FALSE((*R)->isAssembly());
  EXPECT_EQ(canonical(**R), ObjListing);
}

} // namespace
