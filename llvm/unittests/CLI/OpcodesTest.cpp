//===- OpcodesTest.cpp - ECMA-335 CIL opcode table ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/Opcodes.h (plan 5.7.5) against System.Reflection.Emit: every
// entry of the opcode table against an embedded copy of
// llvm/docs/MSIL/recon/p2/metadata-writer-files/dotnet-opcodes.tsv (the
// OpCodes fields that 'mdump --opcodes' dumped: name, value, operand type,
// size, stack behaviour pop and push, flow control, opcode type), plus ECMA's
// no. prefix (III.2.2), the one-byte holes, the reserved prefixes 0xF8-0xFF,
// lookups by name, operand sizes, the branch pairs and the stack effects.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/Opcodes.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/CLI.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

// dotnet-opcodes.tsv without its ILOpCode section, tabs turned into blanks:
// name, value, OperandType, Size, StackBehaviourPop, StackBehaviourPush,
// FlowControl, OpCodeType. The eight 'Nternal' rows are the reserved prefixes
// that System.Reflection.Emit lists but that are not opcodes.
const char *const DotnetOpcodes = R"(
nop 0x0000 InlineNone 1 Pop0 Push0 Next Primitive
break 0x0001 InlineNone 1 Pop0 Push0 Break Primitive
ldarg.0 0x0002 InlineNone 1 Pop0 Push1 Next Macro
ldarg.1 0x0003 InlineNone 1 Pop0 Push1 Next Macro
ldarg.2 0x0004 InlineNone 1 Pop0 Push1 Next Macro
ldarg.3 0x0005 InlineNone 1 Pop0 Push1 Next Macro
ldloc.0 0x0006 InlineNone 1 Pop0 Push1 Next Macro
ldloc.1 0x0007 InlineNone 1 Pop0 Push1 Next Macro
ldloc.2 0x0008 InlineNone 1 Pop0 Push1 Next Macro
ldloc.3 0x0009 InlineNone 1 Pop0 Push1 Next Macro
stloc.0 0x000A InlineNone 1 Pop1 Push0 Next Macro
stloc.1 0x000B InlineNone 1 Pop1 Push0 Next Macro
stloc.2 0x000C InlineNone 1 Pop1 Push0 Next Macro
stloc.3 0x000D InlineNone 1 Pop1 Push0 Next Macro
ldarg.s 0x000E ShortInlineVar 1 Pop0 Push1 Next Macro
ldarga.s 0x000F ShortInlineVar 1 Pop0 Pushi Next Macro
starg.s 0x0010 ShortInlineVar 1 Pop1 Push0 Next Macro
ldloc.s 0x0011 ShortInlineVar 1 Pop0 Push1 Next Macro
ldloca.s 0x0012 ShortInlineVar 1 Pop0 Pushi Next Macro
stloc.s 0x0013 ShortInlineVar 1 Pop1 Push0 Next Macro
ldnull 0x0014 InlineNone 1 Pop0 Pushref Next Primitive
ldc.i4.m1 0x0015 InlineNone 1 Pop0 Pushi Next Macro
ldc.i4.0 0x0016 InlineNone 1 Pop0 Pushi Next Macro
ldc.i4.1 0x0017 InlineNone 1 Pop0 Pushi Next Macro
ldc.i4.2 0x0018 InlineNone 1 Pop0 Pushi Next Macro
ldc.i4.3 0x0019 InlineNone 1 Pop0 Pushi Next Macro
ldc.i4.4 0x001A InlineNone 1 Pop0 Pushi Next Macro
ldc.i4.5 0x001B InlineNone 1 Pop0 Pushi Next Macro
ldc.i4.6 0x001C InlineNone 1 Pop0 Pushi Next Macro
ldc.i4.7 0x001D InlineNone 1 Pop0 Pushi Next Macro
ldc.i4.8 0x001E InlineNone 1 Pop0 Pushi Next Macro
ldc.i4.s 0x001F ShortInlineI 1 Pop0 Pushi Next Macro
ldc.i4 0x0020 InlineI 1 Pop0 Pushi Next Primitive
ldc.i8 0x0021 InlineI8 1 Pop0 Pushi8 Next Primitive
ldc.r4 0x0022 ShortInlineR 1 Pop0 Pushr4 Next Primitive
ldc.r8 0x0023 InlineR 1 Pop0 Pushr8 Next Primitive
dup 0x0025 InlineNone 1 Pop1 Push1_push1 Next Primitive
pop 0x0026 InlineNone 1 Pop1 Push0 Next Primitive
jmp 0x0027 InlineMethod 1 Pop0 Push0 Call Primitive
call 0x0028 InlineMethod 1 Varpop Varpush Call Primitive
calli 0x0029 InlineSig 1 Varpop Varpush Call Primitive
ret 0x002A InlineNone 1 Varpop Push0 Return Primitive
br.s 0x002B ShortInlineBrTarget 1 Pop0 Push0 Branch Macro
brfalse.s 0x002C ShortInlineBrTarget 1 Popi Push0 Cond_Branch Macro
brtrue.s 0x002D ShortInlineBrTarget 1 Popi Push0 Cond_Branch Macro
beq.s 0x002E ShortInlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
bge.s 0x002F ShortInlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
bgt.s 0x0030 ShortInlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
ble.s 0x0031 ShortInlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
blt.s 0x0032 ShortInlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
bne.un.s 0x0033 ShortInlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
bge.un.s 0x0034 ShortInlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
bgt.un.s 0x0035 ShortInlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
ble.un.s 0x0036 ShortInlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
blt.un.s 0x0037 ShortInlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
br 0x0038 InlineBrTarget 1 Pop0 Push0 Branch Primitive
brfalse 0x0039 InlineBrTarget 1 Popi Push0 Cond_Branch Primitive
brtrue 0x003A InlineBrTarget 1 Popi Push0 Cond_Branch Primitive
beq 0x003B InlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
bge 0x003C InlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
bgt 0x003D InlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
ble 0x003E InlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
blt 0x003F InlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
bne.un 0x0040 InlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
bge.un 0x0041 InlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
bgt.un 0x0042 InlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
ble.un 0x0043 InlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
blt.un 0x0044 InlineBrTarget 1 Pop1_pop1 Push0 Cond_Branch Macro
switch 0x0045 InlineSwitch 1 Popi Push0 Cond_Branch Primitive
ldind.i1 0x0046 InlineNone 1 Popi Pushi Next Primitive
ldind.u1 0x0047 InlineNone 1 Popi Pushi Next Primitive
ldind.i2 0x0048 InlineNone 1 Popi Pushi Next Primitive
ldind.u2 0x0049 InlineNone 1 Popi Pushi Next Primitive
ldind.i4 0x004A InlineNone 1 Popi Pushi Next Primitive
ldind.u4 0x004B InlineNone 1 Popi Pushi Next Primitive
ldind.i8 0x004C InlineNone 1 Popi Pushi8 Next Primitive
ldind.i 0x004D InlineNone 1 Popi Pushi Next Primitive
ldind.r4 0x004E InlineNone 1 Popi Pushr4 Next Primitive
ldind.r8 0x004F InlineNone 1 Popi Pushr8 Next Primitive
ldind.ref 0x0050 InlineNone 1 Popi Pushref Next Primitive
stind.ref 0x0051 InlineNone 1 Popi_popi Push0 Next Primitive
stind.i1 0x0052 InlineNone 1 Popi_popi Push0 Next Primitive
stind.i2 0x0053 InlineNone 1 Popi_popi Push0 Next Primitive
stind.i4 0x0054 InlineNone 1 Popi_popi Push0 Next Primitive
stind.i8 0x0055 InlineNone 1 Popi_popi8 Push0 Next Primitive
stind.r4 0x0056 InlineNone 1 Popi_popr4 Push0 Next Primitive
stind.r8 0x0057 InlineNone 1 Popi_popr8 Push0 Next Primitive
add 0x0058 InlineNone 1 Pop1_pop1 Push1 Next Primitive
sub 0x0059 InlineNone 1 Pop1_pop1 Push1 Next Primitive
mul 0x005A InlineNone 1 Pop1_pop1 Push1 Next Primitive
div 0x005B InlineNone 1 Pop1_pop1 Push1 Next Primitive
div.un 0x005C InlineNone 1 Pop1_pop1 Push1 Next Primitive
rem 0x005D InlineNone 1 Pop1_pop1 Push1 Next Primitive
rem.un 0x005E InlineNone 1 Pop1_pop1 Push1 Next Primitive
and 0x005F InlineNone 1 Pop1_pop1 Push1 Next Primitive
or 0x0060 InlineNone 1 Pop1_pop1 Push1 Next Primitive
xor 0x0061 InlineNone 1 Pop1_pop1 Push1 Next Primitive
shl 0x0062 InlineNone 1 Pop1_pop1 Push1 Next Primitive
shr 0x0063 InlineNone 1 Pop1_pop1 Push1 Next Primitive
shr.un 0x0064 InlineNone 1 Pop1_pop1 Push1 Next Primitive
neg 0x0065 InlineNone 1 Pop1 Push1 Next Primitive
not 0x0066 InlineNone 1 Pop1 Push1 Next Primitive
conv.i1 0x0067 InlineNone 1 Pop1 Pushi Next Primitive
conv.i2 0x0068 InlineNone 1 Pop1 Pushi Next Primitive
conv.i4 0x0069 InlineNone 1 Pop1 Pushi Next Primitive
conv.i8 0x006A InlineNone 1 Pop1 Pushi8 Next Primitive
conv.r4 0x006B InlineNone 1 Pop1 Pushr4 Next Primitive
conv.r8 0x006C InlineNone 1 Pop1 Pushr8 Next Primitive
conv.u4 0x006D InlineNone 1 Pop1 Pushi Next Primitive
conv.u8 0x006E InlineNone 1 Pop1 Pushi8 Next Primitive
callvirt 0x006F InlineMethod 1 Varpop Varpush Call Objmodel
cpobj 0x0070 InlineType 1 Popi_popi Push0 Next Objmodel
ldobj 0x0071 InlineType 1 Popi Push1 Next Objmodel
ldstr 0x0072 InlineString 1 Pop0 Pushref Next Objmodel
newobj 0x0073 InlineMethod 1 Varpop Pushref Call Objmodel
castclass 0x0074 InlineType 1 Popref Pushref Next Objmodel
isinst 0x0075 InlineType 1 Popref Pushi Next Objmodel
conv.r.un 0x0076 InlineNone 1 Pop1 Pushr8 Next Primitive
unbox 0x0079 InlineType 1 Popref Pushi Next Primitive
throw 0x007A InlineNone 1 Popref Push0 Throw Objmodel
ldfld 0x007B InlineField 1 Popref Push1 Next Objmodel
ldflda 0x007C InlineField 1 Popref Pushi Next Objmodel
stfld 0x007D InlineField 1 Popref_pop1 Push0 Next Objmodel
ldsfld 0x007E InlineField 1 Pop0 Push1 Next Objmodel
ldsflda 0x007F InlineField 1 Pop0 Pushi Next Objmodel
stsfld 0x0080 InlineField 1 Pop1 Push0 Next Objmodel
stobj 0x0081 InlineType 1 Popi_pop1 Push0 Next Primitive
conv.ovf.i1.un 0x0082 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.i2.un 0x0083 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.i4.un 0x0084 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.i8.un 0x0085 InlineNone 1 Pop1 Pushi8 Next Primitive
conv.ovf.u1.un 0x0086 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.u2.un 0x0087 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.u4.un 0x0088 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.u8.un 0x0089 InlineNone 1 Pop1 Pushi8 Next Primitive
conv.ovf.i.un 0x008A InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.u.un 0x008B InlineNone 1 Pop1 Pushi Next Primitive
box 0x008C InlineType 1 Pop1 Pushref Next Primitive
newarr 0x008D InlineType 1 Popi Pushref Next Objmodel
ldlen 0x008E InlineNone 1 Popref Pushi Next Objmodel
ldelema 0x008F InlineType 1 Popref_popi Pushi Next Objmodel
ldelem.i1 0x0090 InlineNone 1 Popref_popi Pushi Next Objmodel
ldelem.u1 0x0091 InlineNone 1 Popref_popi Pushi Next Objmodel
ldelem.i2 0x0092 InlineNone 1 Popref_popi Pushi Next Objmodel
ldelem.u2 0x0093 InlineNone 1 Popref_popi Pushi Next Objmodel
ldelem.i4 0x0094 InlineNone 1 Popref_popi Pushi Next Objmodel
ldelem.u4 0x0095 InlineNone 1 Popref_popi Pushi Next Objmodel
ldelem.i8 0x0096 InlineNone 1 Popref_popi Pushi8 Next Objmodel
ldelem.i 0x0097 InlineNone 1 Popref_popi Pushi Next Objmodel
ldelem.r4 0x0098 InlineNone 1 Popref_popi Pushr4 Next Objmodel
ldelem.r8 0x0099 InlineNone 1 Popref_popi Pushr8 Next Objmodel
ldelem.ref 0x009A InlineNone 1 Popref_popi Pushref Next Objmodel
stelem.i 0x009B InlineNone 1 Popref_popi_popi Push0 Next Objmodel
stelem.i1 0x009C InlineNone 1 Popref_popi_popi Push0 Next Objmodel
stelem.i2 0x009D InlineNone 1 Popref_popi_popi Push0 Next Objmodel
stelem.i4 0x009E InlineNone 1 Popref_popi_popi Push0 Next Objmodel
stelem.i8 0x009F InlineNone 1 Popref_popi_popi8 Push0 Next Objmodel
stelem.r4 0x00A0 InlineNone 1 Popref_popi_popr4 Push0 Next Objmodel
stelem.r8 0x00A1 InlineNone 1 Popref_popi_popr8 Push0 Next Objmodel
stelem.ref 0x00A2 InlineNone 1 Popref_popi_popref Push0 Next Objmodel
ldelem 0x00A3 InlineType 1 Popref_popi Push1 Next Objmodel
stelem 0x00A4 InlineType 1 Popref_popi_pop1 Push0 Next Objmodel
unbox.any 0x00A5 InlineType 1 Popref Push1 Next Objmodel
conv.ovf.i1 0x00B3 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.u1 0x00B4 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.i2 0x00B5 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.u2 0x00B6 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.i4 0x00B7 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.u4 0x00B8 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.i8 0x00B9 InlineNone 1 Pop1 Pushi8 Next Primitive
conv.ovf.u8 0x00BA InlineNone 1 Pop1 Pushi8 Next Primitive
refanyval 0x00C2 InlineType 1 Pop1 Pushi Next Primitive
ckfinite 0x00C3 InlineNone 1 Pop1 Pushr8 Next Primitive
mkrefany 0x00C6 InlineType 1 Popi Push1 Next Primitive
ldtoken 0x00D0 InlineTok 1 Pop0 Pushi Next Primitive
conv.u2 0x00D1 InlineNone 1 Pop1 Pushi Next Primitive
conv.u1 0x00D2 InlineNone 1 Pop1 Pushi Next Primitive
conv.i 0x00D3 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.i 0x00D4 InlineNone 1 Pop1 Pushi Next Primitive
conv.ovf.u 0x00D5 InlineNone 1 Pop1 Pushi Next Primitive
add.ovf 0x00D6 InlineNone 1 Pop1_pop1 Push1 Next Primitive
add.ovf.un 0x00D7 InlineNone 1 Pop1_pop1 Push1 Next Primitive
mul.ovf 0x00D8 InlineNone 1 Pop1_pop1 Push1 Next Primitive
mul.ovf.un 0x00D9 InlineNone 1 Pop1_pop1 Push1 Next Primitive
sub.ovf 0x00DA InlineNone 1 Pop1_pop1 Push1 Next Primitive
sub.ovf.un 0x00DB InlineNone 1 Pop1_pop1 Push1 Next Primitive
endfinally 0x00DC InlineNone 1 Pop0 Push0 Return Primitive
leave 0x00DD InlineBrTarget 1 Pop0 Push0 Branch Primitive
leave.s 0x00DE ShortInlineBrTarget 1 Pop0 Push0 Branch Primitive
stind.i 0x00DF InlineNone 1 Popi_popi Push0 Next Primitive
conv.u 0x00E0 InlineNone 1 Pop1 Pushi Next Primitive
prefix7 0x00F8 InlineNone 1 Pop0 Push0 Meta Nternal
prefix6 0x00F9 InlineNone 1 Pop0 Push0 Meta Nternal
prefix5 0x00FA InlineNone 1 Pop0 Push0 Meta Nternal
prefix4 0x00FB InlineNone 1 Pop0 Push0 Meta Nternal
prefix3 0x00FC InlineNone 1 Pop0 Push0 Meta Nternal
prefix2 0x00FD InlineNone 1 Pop0 Push0 Meta Nternal
prefix1 0x00FE InlineNone 1 Pop0 Push0 Meta Nternal
prefixref 0x00FF InlineNone 1 Pop0 Push0 Meta Nternal
arglist 0xFE00 InlineNone 2 Pop0 Pushi Next Primitive
ceq 0xFE01 InlineNone 2 Pop1_pop1 Pushi Next Primitive
cgt 0xFE02 InlineNone 2 Pop1_pop1 Pushi Next Primitive
cgt.un 0xFE03 InlineNone 2 Pop1_pop1 Pushi Next Primitive
clt 0xFE04 InlineNone 2 Pop1_pop1 Pushi Next Primitive
clt.un 0xFE05 InlineNone 2 Pop1_pop1 Pushi Next Primitive
ldftn 0xFE06 InlineMethod 2 Pop0 Pushi Next Primitive
ldvirtftn 0xFE07 InlineMethod 2 Popref Pushi Next Primitive
ldarg 0xFE09 InlineVar 2 Pop0 Push1 Next Primitive
ldarga 0xFE0A InlineVar 2 Pop0 Pushi Next Primitive
starg 0xFE0B InlineVar 2 Pop1 Push0 Next Primitive
ldloc 0xFE0C InlineVar 2 Pop0 Push1 Next Primitive
ldloca 0xFE0D InlineVar 2 Pop0 Pushi Next Primitive
stloc 0xFE0E InlineVar 2 Pop1 Push0 Next Primitive
localloc 0xFE0F InlineNone 2 Popi Pushi Next Primitive
endfilter 0xFE11 InlineNone 2 Popi Push0 Return Primitive
unaligned. 0xFE12 ShortInlineI 2 Pop0 Push0 Meta Prefix
volatile. 0xFE13 InlineNone 2 Pop0 Push0 Meta Prefix
tail. 0xFE14 InlineNone 2 Pop0 Push0 Meta Prefix
initobj 0xFE15 InlineType 2 Popi Push0 Next Objmodel
constrained. 0xFE16 InlineType 2 Pop0 Push0 Meta Prefix
cpblk 0xFE17 InlineNone 2 Popi_popi_popi Push0 Next Primitive
initblk 0xFE18 InlineNone 2 Popi_popi_popi Push0 Next Primitive
rethrow 0xFE1A InlineNone 2 Pop0 Push0 Throw Objmodel
sizeof 0xFE1C InlineType 2 Pop0 Pushi Next Primitive
refanytype 0xFE1D InlineNone 2 Pop1 Pushi Next Primitive
readonly. 0xFE1E InlineNone 2 Pop0 Push0 Meta Prefix
)";

struct Row {
  std::string Name;
  uint16_t Value;
  std::string Operand;
  unsigned Size;
  std::string Pop, Push, Flow, Type;
};

std::vector<Row> parseTable() {
  std::vector<Row> Rows;
  SmallVector<StringRef, 256> Lines;
  StringRef(DotnetOpcodes).split(Lines, '\n', -1, /*KeepEmpty=*/false);
  for (StringRef L : Lines) {
    SmallVector<StringRef, 8> F;
    L.split(F, ' ', -1, /*KeepEmpty=*/false);
    EXPECT_EQ(F.size(), 8u) << L;
    if (F.size() != 8)
      continue;
    Row R;
    R.Name = F[0].str();
    unsigned V = 0, S = 0;
    EXPECT_FALSE(F[1].drop_front(2).getAsInteger(16, V)) << L;
    EXPECT_FALSE(F[3].getAsInteger(10, S)) << L;
    R.Value = static_cast<uint16_t>(V);
    R.Operand = F[2].str();
    R.Size = S;
    R.Pop = F[4].str();
    R.Push = F[5].str();
    R.Flow = F[6].str();
    R.Type = F[7].str();
    Rows.push_back(R);
  }
  return Rows;
}

const std::vector<Row> &getTable() {
  static const std::vector<Row> Rows = parseTable();
  return Rows;
}

std::optional<OperandKind> operandKind(StringRef Name) {
  return StringSwitch<std::optional<OperandKind>>(Name)
      .Case("InlineNone", OperandKind::InlineNone)
      .Case("ShortInlineVar", OperandKind::ShortInlineVar)
      .Case("InlineVar", OperandKind::InlineVar)
      .Case("ShortInlineI", OperandKind::ShortInlineI)
      .Case("InlineI", OperandKind::InlineI)
      .Case("InlineI8", OperandKind::InlineI8)
      .Case("ShortInlineR", OperandKind::ShortInlineR)
      .Case("InlineR", OperandKind::InlineR)
      .Case("ShortInlineBrTarget", OperandKind::ShortInlineBrTarget)
      .Case("InlineBrTarget", OperandKind::InlineBrTarget)
      .Case("InlineMethod", OperandKind::InlineMethod)
      .Case("InlineField", OperandKind::InlineField)
      .Case("InlineType", OperandKind::InlineType)
      .Case("InlineTok", OperandKind::InlineTok)
      .Case("InlineSig", OperandKind::InlineSig)
      .Case("InlineString", OperandKind::InlineString)
      .Case("InlineSwitch", OperandKind::InlineSwitch)
      .Default(std::nullopt);
}

std::optional<StackBehaviour> stackBehaviour(StringRef Name) {
  return StringSwitch<std::optional<StackBehaviour>>(Name)
      .Case("Pop0", StackBehaviour::Pop0)
      .Case("Pop1", StackBehaviour::Pop1)
      .Case("Pop1_pop1", StackBehaviour::Pop1_pop1)
      .Case("Popi", StackBehaviour::Popi)
      .Case("Popi_pop1", StackBehaviour::Popi_pop1)
      .Case("Popi_popi", StackBehaviour::Popi_popi)
      .Case("Popi_popi8", StackBehaviour::Popi_popi8)
      .Case("Popi_popi_popi", StackBehaviour::Popi_popi_popi)
      .Case("Popi_popr4", StackBehaviour::Popi_popr4)
      .Case("Popi_popr8", StackBehaviour::Popi_popr8)
      .Case("Popref", StackBehaviour::Popref)
      .Case("Popref_pop1", StackBehaviour::Popref_pop1)
      .Case("Popref_popi", StackBehaviour::Popref_popi)
      .Case("Popref_popi_popi", StackBehaviour::Popref_popi_popi)
      .Case("Popref_popi_popi8", StackBehaviour::Popref_popi_popi8)
      .Case("Popref_popi_popr4", StackBehaviour::Popref_popi_popr4)
      .Case("Popref_popi_popr8", StackBehaviour::Popref_popi_popr8)
      .Case("Popref_popi_popref", StackBehaviour::Popref_popi_popref)
      .Case("Popref_popi_pop1", StackBehaviour::Popref_popi_pop1)
      .Case("Push0", StackBehaviour::Push0)
      .Case("Push1", StackBehaviour::Push1)
      .Case("Push1_push1", StackBehaviour::Push1_push1)
      .Case("Pushi", StackBehaviour::Pushi)
      .Case("Pushi8", StackBehaviour::Pushi8)
      .Case("Pushr4", StackBehaviour::Pushr4)
      .Case("Pushr8", StackBehaviour::Pushr8)
      .Case("Pushref", StackBehaviour::Pushref)
      .Case("Varpop", StackBehaviour::Varpop)
      .Case("Varpush", StackBehaviour::Varpush)
      .Default(std::nullopt);
}

std::optional<FlowControl> flowControl(StringRef Name) {
  return StringSwitch<std::optional<FlowControl>>(Name)
      .Case("Branch", FlowControl::Branch)
      .Case("Break", FlowControl::Break)
      .Case("Call", FlowControl::Call)
      .Case("Cond_Branch", FlowControl::Cond_Branch)
      .Case("Meta", FlowControl::Meta)
      .Case("Next", FlowControl::Next)
      .Case("Return", FlowControl::Return)
      .Case("Throw", FlowControl::Throw)
      .Default(std::nullopt);
}

unsigned value(Opcode Op) { return static_cast<unsigned>(Op); }

TEST(CLIOpcodesTest, EmbeddedTableShape) {
  const std::vector<Row> &Rows = getTable();
  ASSERT_EQ(Rows.size(), 226u);
  unsigned Reserved = 0;
  for (const Row &R : Rows)
    if (R.Type == "Nternal")
      ++Reserved;
  EXPECT_EQ(Reserved, 8u);
}

// Every System.Reflection.Emit opcode: value, mnemonic, operand kind, size,
// stack behaviour and flow control, found by value and by name.
TEST(CLIOpcodesTest, EveryEntryMatchesSystemReflectionEmit) {
  unsigned Checked = 0;
  for (const Row &R : getTable()) {
    if (R.Type == "Nternal")
      continue;
    SCOPED_TRACE(R.Name);
    std::optional<Opcode> Op = getOpcodeByValue(R.Value);
    ASSERT_TRUE(Op.has_value());
    EXPECT_EQ(value(*Op), R.Value);
    const OpcodeInfo &Info = getOpcodeInfo(*Op);
    EXPECT_EQ(value(Info.Op), R.Value);
    EXPECT_EQ(StringRef(Info.Name), StringRef(R.Name));
    std::optional<OperandKind> K = operandKind(R.Operand);
    ASSERT_TRUE(K.has_value()) << R.Operand;
    EXPECT_EQ(Info.Operand, *K) << R.Operand;
    std::optional<StackBehaviour> Pop = stackBehaviour(R.Pop);
    std::optional<StackBehaviour> Push = stackBehaviour(R.Push);
    ASSERT_TRUE(Pop.has_value() && Push.has_value()) << R.Pop << " " << R.Push;
    EXPECT_EQ(Info.Pop, *Pop) << R.Pop;
    EXPECT_EQ(Info.Push, *Push) << R.Push;
    std::optional<FlowControl> Flow = flowControl(R.Flow);
    ASSERT_TRUE(Flow.has_value()) << R.Flow;
    EXPECT_EQ(Info.Flow, *Flow) << R.Flow;
    EXPECT_EQ(getOpcodeSize(*Op), R.Size);
    std::optional<Opcode> ByName = getOpcodeByName(R.Name);
    ASSERT_TRUE(ByName.has_value());
    EXPECT_EQ(value(*ByName), R.Value);
    ++Checked;
  }
  EXPECT_EQ(Checked, 218u);
}

// no. (III.2.2): FE 19 followed by an unsigned int8 of check flags; ECMA has
// it, System.Reflection.Emit does not.
TEST(CLIOpcodesTest, NoPrefix) {
  std::optional<Opcode> No = getOpcodeByValue(0xFE19);
  ASSERT_TRUE(No.has_value());
  EXPECT_EQ(value(*No), 0xFE19u);
  const OpcodeInfo &Info = getOpcodeInfo(*No);
  EXPECT_EQ(StringRef(Info.Name), "no.");
  EXPECT_EQ(Info.Operand, OperandKind::ShortInlineI);
  EXPECT_EQ(Info.Pop, StackBehaviour::Pop0);
  EXPECT_EQ(Info.Push, StackBehaviour::Push0);
  EXPECT_EQ(Info.Flow, FlowControl::Meta);
  EXPECT_EQ(getOpcodeSize(*No), 2u);
  std::optional<Opcode> ByName = getOpcodeByName("no.");
  ASSERT_TRUE(ByName.has_value());
  EXPECT_EQ(value(*ByName), 0xFE19u);
}

TEST(CLIOpcodesTest, AllOpcodesInValueOrder) {
  ArrayRef<OpcodeInfo> All = getAllOpcodes();
  ASSERT_EQ(All.size(), 219u);
  std::set<unsigned> Expected = {0xFE19};
  for (const Row &R : getTable())
    if (R.Type != "Nternal")
      Expected.insert(R.Value);
  ASSERT_EQ(Expected.size(), 219u);
  std::set<unsigned> Seen;
  std::set<std::string> Names;
  for (size_t I = 0; I != All.size(); ++I) {
    const OpcodeInfo &E = All[I];
    SCOPED_TRACE(E.Name.str());
    if (I)
      EXPECT_LT(value(All[I - 1].Op), value(E.Op)) << "value order";
    EXPECT_TRUE(Seen.insert(value(E.Op)).second);
    EXPECT_TRUE(Names.insert(E.Name.str()).second);
    const OpcodeInfo &Same = getOpcodeInfo(E.Op);
    EXPECT_EQ(value(Same.Op), value(E.Op));
    EXPECT_EQ(Same.Name, E.Name);
    EXPECT_EQ(Same.Operand, E.Operand);
  }
  EXPECT_EQ(Seen, Expected);
  EXPECT_EQ(value(All.front().Op), 0x0000u);
  EXPECT_EQ(value(All.back().Op), 0xFE1Eu);
}

// The one-byte holes, the prefix byte and the reserved prefixes 0xF8-0xFF,
// the two-byte holes FE 08, FE 10 and FE 1B, and values past FE 1E.
TEST(CLIOpcodesTest, HolesAreNotOpcodes) {
  std::set<unsigned> Valid = {0xFE19};
  for (const Row &R : getTable())
    if (R.Type != "Nternal")
      Valid.insert(R.Value);
  unsigned Holes = 0;
  for (unsigned V = 0; V != 0x100; ++V) {
    if (Valid.count(V))
      continue;
    EXPECT_FALSE(getOpcodeByValue(V).has_value()) << "0x" << utohexstr(V);
    ++Holes;
  }
  // 0x24, 0x77-0x78, 0xA6-0xB2, 0xBB-0xC1, 0xC4-0xC5, 0xC7-0xCF, 0xE1-0xFF.
  EXPECT_EQ(Holes, 1u + 2 + 13 + 7 + 2 + 9 + 31);
  for (unsigned V : {0x24u, 0x77u, 0x78u, 0xA6u, 0xB2u, 0xBBu, 0xC1u, 0xC4u,
                     0xC5u, 0xC7u, 0xCFu, 0xE1u, 0xF8u, 0xFEu, 0xFFu})
    EXPECT_FALSE(getOpcodeByValue(V).has_value()) << "0x" << utohexstr(V);
  unsigned TwoByte = 0;
  for (unsigned V = 0xFE00; V != 0xFF00; ++V) {
    if (Valid.count(V)) {
      ++TwoByte;
      continue;
    }
    EXPECT_FALSE(getOpcodeByValue(V).has_value()) << "0x" << utohexstr(V);
  }
  EXPECT_EQ(TwoByte, 28u);
  for (unsigned V : {0xFE08u, 0xFE10u, 0xFE1Bu, 0xFE1Fu, 0xFEFFu, 0x0100u,
                     0x01FEu, 0xF800u, 0xFD00u, 0xFF00u, 0xFFFFu})
    EXPECT_FALSE(getOpcodeByValue(V).has_value()) << "0x" << utohexstr(V);
}

TEST(CLIOpcodesTest, LookupByName) {
  struct {
    const char *Name;
    unsigned Value;
  } Cases[] = {
      {"nop", 0x00},
      {"ldc.i4.m1", 0x15},
      {"ldc.i4.s", 0x1F},
      {"bne.un.s", 0x33},
      {"conv.ovf.u.un", 0x8B},
      {"unbox.any", 0xA5},
      {"leave.s", 0xDE},
      {"ldelem.ref", 0x9A},
      {"ceq", 0xFE01},
      {"unaligned.", 0xFE12},
      {"volatile.", 0xFE13},
      {"tail.", 0xFE14},
      {"constrained.", 0xFE16},
      {"readonly.", 0xFE1E},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.Name);
    std::optional<Opcode> Op = getOpcodeByName(C.Name);
    ASSERT_TRUE(Op.has_value());
    EXPECT_EQ(value(*Op), C.Value);
  }
  for (const char *Name : {"", "ldc.i4.9", "prefix1", "prefixref", "unaligned",
                           "tail", "ldc.i4.s ", "br.l", "ldarg.4", "nop."})
    EXPECT_FALSE(getOpcodeByName(Name).has_value()) << "'" << Name << "'";
}

TEST(CLIOpcodesTest, Sizes) {
  for (const OpcodeInfo &E : getAllOpcodes())
    EXPECT_EQ(getOpcodeSize(E.Op), value(E.Op) >= 0xFE00 ? 2u : 1u)
        << E.Name.str();
  struct {
    OperandKind K;
    unsigned Size;
  } Kinds[] = {
      {OperandKind::InlineNone, 0},
      {OperandKind::ShortInlineVar, 1},
      {OperandKind::InlineVar, 2},
      {OperandKind::ShortInlineI, 1},
      {OperandKind::InlineI, 4},
      {OperandKind::InlineI8, 8},
      {OperandKind::ShortInlineR, 4},
      {OperandKind::InlineR, 8},
      {OperandKind::ShortInlineBrTarget, 1},
      {OperandKind::InlineBrTarget, 4},
      {OperandKind::InlineMethod, 4},
      {OperandKind::InlineField, 4},
      {OperandKind::InlineType, 4},
      {OperandKind::InlineTok, 4},
      {OperandKind::InlineSig, 4},
      {OperandKind::InlineString, 4},
      {OperandKind::InlineSwitch, 4},
  };
  for (const auto &C : Kinds)
    EXPECT_EQ(getOperandSize(C.K), C.Size) << static_cast<int>(C.K);
}

// The 13 branches and leave, each with its .s form.
TEST(CLIOpcodesTest, BranchPairs) {
  const char *Long[] = {"br",     "brfalse", "brtrue", "beq",    "bge",
                        "bgt",    "ble",     "blt",    "bne.un", "bge.un",
                        "bgt.un", "ble.un",  "blt.un", "leave"};
  for (const char *Name : Long) {
    SCOPED_TRACE(Name);
    std::optional<Opcode> L = getOpcodeByName(Name);
    std::optional<Opcode> S = getOpcodeByName(std::string(Name) + ".s");
    ASSERT_TRUE(L && S);
    EXPECT_EQ(getOpcodeInfo(*L).Operand, OperandKind::InlineBrTarget);
    EXPECT_EQ(getOpcodeInfo(*S).Operand, OperandKind::ShortInlineBrTarget);
    std::optional<Opcode> Short = getShortBranchForm(*L);
    ASSERT_TRUE(Short.has_value());
    EXPECT_EQ(value(*Short), value(*S));
    std::optional<Opcode> Wide = getLongBranchForm(*S);
    ASSERT_TRUE(Wide.has_value());
    EXPECT_EQ(value(*Wide), value(*L));
  }
  EXPECT_EQ(value(*getShortBranchForm(Opcode::Br)), 0x2Bu);
  EXPECT_EQ(value(*getLongBranchForm(Opcode::Leave_s)), 0xDDu);
  for (Opcode Op :
       {Opcode::Nop, Opcode::Switch, Opcode::Call, Opcode::Ret, Opcode::Jmp,
        Opcode::Endfinally, Opcode::Ldc_i4_s, Opcode::Ldarg_s, Opcode::Throw}) {
    SCOPED_TRACE(getOpcodeInfo(Op).Name.str());
    EXPECT_FALSE(getShortBranchForm(Op).has_value());
    EXPECT_FALSE(getLongBranchForm(Op).has_value());
  }
}

TEST(CLIOpcodesTest, StackEffects) {
  struct {
    StackBehaviour B;
    int Pops;
  } PopCases[] = {
      {StackBehaviour::Pop0, 0},
      {StackBehaviour::Pop1, 1},
      {StackBehaviour::Pop1_pop1, 2},
      {StackBehaviour::Popi, 1},
      {StackBehaviour::Popi_pop1, 2},
      {StackBehaviour::Popi_popi, 2},
      {StackBehaviour::Popi_popi8, 2},
      {StackBehaviour::Popi_popi_popi, 3},
      {StackBehaviour::Popi_popr4, 2},
      {StackBehaviour::Popi_popr8, 2},
      {StackBehaviour::Popref, 1},
      {StackBehaviour::Popref_pop1, 2},
      {StackBehaviour::Popref_popi, 2},
      {StackBehaviour::Popref_popi_popi, 3},
      {StackBehaviour::Popref_popi_popi8, 3},
      {StackBehaviour::Popref_popi_popr4, 3},
      {StackBehaviour::Popref_popi_popr8, 3},
      {StackBehaviour::Popref_popi_popref, 3},
      {StackBehaviour::Popref_popi_pop1, 3},
      {StackBehaviour::Varpop, -1},
  };
  for (const auto &C : PopCases)
    EXPECT_EQ(getStackPops(C.B), C.Pops) << static_cast<int>(C.B);
  struct {
    StackBehaviour B;
    int Pushes;
  } PushCases[] = {
      {StackBehaviour::Push0, 0},       {StackBehaviour::Push1, 1},
      {StackBehaviour::Push1_push1, 2}, {StackBehaviour::Pushi, 1},
      {StackBehaviour::Pushi8, 1},      {StackBehaviour::Pushr4, 1},
      {StackBehaviour::Pushr8, 1},      {StackBehaviour::Pushref, 1},
      {StackBehaviour::Varpush, -1},
  };
  for (const auto &C : PushCases)
    EXPECT_EQ(getStackPushes(C.B), C.Pushes) << static_cast<int>(C.B);

  // Some whole instructions.
  auto Net = [](Opcode Op) {
    const OpcodeInfo &I = getOpcodeInfo(Op);
    return getStackPushes(I.Push) - getStackPops(I.Pop);
  };
  EXPECT_EQ(Net(Opcode::Dup), 1);
  EXPECT_EQ(Net(Opcode::Stelem_ref), -3);
  EXPECT_EQ(Net(Opcode::Cpblk), -3);
  EXPECT_EQ(Net(Opcode::Ldc_i4_s), 1);
  EXPECT_EQ(Net(Opcode::Add), -1);
  EXPECT_EQ(getStackPops(getOpcodeInfo(Opcode::Call).Pop), -1);
  EXPECT_EQ(getStackPushes(getOpcodeInfo(Opcode::Call).Push), -1);
  EXPECT_EQ(getStackPops(getOpcodeInfo(Opcode::Ret).Pop), -1);
  EXPECT_EQ(getStackPops(getOpcodeInfo(Opcode::Newobj).Pop), -1);
  EXPECT_EQ(getStackPushes(getOpcodeInfo(Opcode::Newobj).Push), 1);
}

} // namespace
