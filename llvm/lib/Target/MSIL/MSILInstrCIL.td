//===- MSILInstrCIL.td - MC-level CIL instruction records --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// One MC-level record CIL_<Enum> for each entry of
/// llvm/include/llvm/BinaryFormat/CILOpcodes.def (ECMA-335 III), in value
/// order: the entry's Text is the mnemonic, its Value the CIL opcode
/// (TSFlags bits 18-3), and its OperandKind gives the one operand:
///   InlineNone                       none;
///   ShortInlineVar, InlineVar        arg_op or local_op (an index);
///   ShortInlineI, InlineI            i32imm_op; InlineI8 i64imm_op;
///   ShortInlineR, InlineR            f32imm_op, f64imm_op (bit patterns);
///   ShortInlineBrTarget,
///   InlineBrTarget                   bb_op (a label);
///   InlineMethod, InlineField, InlineType, InlineTok, InlineSig,
///   InlineString                     cil_token_op (an MSILMemberRefExpr);
///   InlineSwitch                     variable_ops (one label per target).
/// The FlowControl of the entry sets isBranch, isTerminator, isBarrier,
/// isReturn and isCall.
///
/// MSILMCInstLower lowers every MachineInstr into these records, and only
/// they reach a streamer: MSILInstPrinter prints them and MSILMCCodeEmitter
/// encodes them, so the text and the binary come from one record. The
/// MachineInstr-level records of the other files never reach MC.
///
/// Generated from CILOpcodes.def; keep the two in sync (the MSILCILRecords
/// unit tests check every entry).
///
//===----------------------------------------------------------------------===//

def CIL_Nop : CILInst<0x0000, (ins), "nop">;
def CIL_Break : CILInst<0x0001, (ins), "break">;
def CIL_Ldarg_0 : CILInst<0x0002, (ins), "ldarg.0">;
def CIL_Ldarg_1 : CILInst<0x0003, (ins), "ldarg.1">;
def CIL_Ldarg_2 : CILInst<0x0004, (ins), "ldarg.2">;
def CIL_Ldarg_3 : CILInst<0x0005, (ins), "ldarg.3">;
def CIL_Ldloc_0 : CILInst<0x0006, (ins), "ldloc.0">;
def CIL_Ldloc_1 : CILInst<0x0007, (ins), "ldloc.1">;
def CIL_Ldloc_2 : CILInst<0x0008, (ins), "ldloc.2">;
def CIL_Ldloc_3 : CILInst<0x0009, (ins), "ldloc.3">;
def CIL_Stloc_0 : CILInst<0x000A, (ins), "stloc.0">;
def CIL_Stloc_1 : CILInst<0x000B, (ins), "stloc.1">;
def CIL_Stloc_2 : CILInst<0x000C, (ins), "stloc.2">;
def CIL_Stloc_3 : CILInst<0x000D, (ins), "stloc.3">;
def CIL_Ldarg_s : CILInst<0x000E, (ins arg_op:$arg), "ldarg.s $arg">;
def CIL_Ldarga_s : CILInst<0x000F, (ins arg_op:$arg), "ldarga.s $arg">;
def CIL_Starg_s : CILInst<0x0010, (ins arg_op:$arg), "starg.s $arg">;
def CIL_Ldloc_s : CILInst<0x0011, (ins local_op:$local), "ldloc.s $local">;
def CIL_Ldloca_s : CILInst<0x0012, (ins local_op:$local), "ldloca.s $local">;
def CIL_Stloc_s : CILInst<0x0013, (ins local_op:$local), "stloc.s $local">;
def CIL_Ldnull : CILInst<0x0014, (ins), "ldnull">;
def CIL_Ldc_i4_m1 : CILInst<0x0015, (ins), "ldc.i4.m1">;
def CIL_Ldc_i4_0 : CILInst<0x0016, (ins), "ldc.i4.0">;
def CIL_Ldc_i4_1 : CILInst<0x0017, (ins), "ldc.i4.1">;
def CIL_Ldc_i4_2 : CILInst<0x0018, (ins), "ldc.i4.2">;
def CIL_Ldc_i4_3 : CILInst<0x0019, (ins), "ldc.i4.3">;
def CIL_Ldc_i4_4 : CILInst<0x001A, (ins), "ldc.i4.4">;
def CIL_Ldc_i4_5 : CILInst<0x001B, (ins), "ldc.i4.5">;
def CIL_Ldc_i4_6 : CILInst<0x001C, (ins), "ldc.i4.6">;
def CIL_Ldc_i4_7 : CILInst<0x001D, (ins), "ldc.i4.7">;
def CIL_Ldc_i4_8 : CILInst<0x001E, (ins), "ldc.i4.8">;
def CIL_Ldc_i4_s : CILInst<0x001F, (ins i32imm_op:$imm), "ldc.i4.s $imm">;
def CIL_Ldc_i4 : CILInst<0x0020, (ins i32imm_op:$imm), "ldc.i4 $imm">;
def CIL_Ldc_i8 : CILInst<0x0021, (ins i64imm_op:$imm), "ldc.i8 $imm">;
def CIL_Ldc_r4 : CILInst<0x0022, (ins f32imm_op:$imm), "ldc.r4 $imm">;
def CIL_Ldc_r8 : CILInst<0x0023, (ins f64imm_op:$imm), "ldc.r8 $imm">;
def CIL_Dup : CILInst<0x0025, (ins), "dup">;
def CIL_Pop : CILInst<0x0026, (ins), "pop">;
let isCall = 1 in
def CIL_Jmp : CILInst<0x0027, (ins cil_token_op:$tok), "jmp $tok">;
let isCall = 1 in
def CIL_Call : CILInst<0x0028, (ins cil_token_op:$tok), "call $tok">;
let isCall = 1 in
def CIL_Calli : CILInst<0x0029, (ins cil_token_op:$tok), "calli $tok">;
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def CIL_Ret : CILInst<0x002A, (ins), "ret">;
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def CIL_Br_s : CILInst<0x002B, (ins bb_op:$dst), "br.s $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Brfalse_s : CILInst<0x002C, (ins bb_op:$dst), "brfalse.s $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Brtrue_s : CILInst<0x002D, (ins bb_op:$dst), "brtrue.s $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Beq_s : CILInst<0x002E, (ins bb_op:$dst), "beq.s $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Bge_s : CILInst<0x002F, (ins bb_op:$dst), "bge.s $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Bgt_s : CILInst<0x0030, (ins bb_op:$dst), "bgt.s $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Ble_s : CILInst<0x0031, (ins bb_op:$dst), "ble.s $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Blt_s : CILInst<0x0032, (ins bb_op:$dst), "blt.s $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Bne_un_s : CILInst<0x0033, (ins bb_op:$dst), "bne.un.s $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Bge_un_s : CILInst<0x0034, (ins bb_op:$dst), "bge.un.s $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Bgt_un_s : CILInst<0x0035, (ins bb_op:$dst), "bgt.un.s $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Ble_un_s : CILInst<0x0036, (ins bb_op:$dst), "ble.un.s $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Blt_un_s : CILInst<0x0037, (ins bb_op:$dst), "blt.un.s $dst">;
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def CIL_Br : CILInst<0x0038, (ins bb_op:$dst), "br $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Brfalse : CILInst<0x0039, (ins bb_op:$dst), "brfalse $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Brtrue : CILInst<0x003A, (ins bb_op:$dst), "brtrue $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Beq : CILInst<0x003B, (ins bb_op:$dst), "beq $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Bge : CILInst<0x003C, (ins bb_op:$dst), "bge $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Bgt : CILInst<0x003D, (ins bb_op:$dst), "bgt $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Ble : CILInst<0x003E, (ins bb_op:$dst), "ble $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Blt : CILInst<0x003F, (ins bb_op:$dst), "blt $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Bne_un : CILInst<0x0040, (ins bb_op:$dst), "bne.un $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Bge_un : CILInst<0x0041, (ins bb_op:$dst), "bge.un $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Bgt_un : CILInst<0x0042, (ins bb_op:$dst), "bgt.un $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Ble_un : CILInst<0x0043, (ins bb_op:$dst), "ble.un $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Blt_un : CILInst<0x0044, (ins bb_op:$dst), "blt.un $dst">;
let isBranch = 1, isTerminator = 1 in
def CIL_Switch : CILInst<0x0045, (ins variable_ops), "switch">;
def CIL_Ldind_i1 : CILInst<0x0046, (ins), "ldind.i1">;
def CIL_Ldind_u1 : CILInst<0x0047, (ins), "ldind.u1">;
def CIL_Ldind_i2 : CILInst<0x0048, (ins), "ldind.i2">;
def CIL_Ldind_u2 : CILInst<0x0049, (ins), "ldind.u2">;
def CIL_Ldind_i4 : CILInst<0x004A, (ins), "ldind.i4">;
def CIL_Ldind_u4 : CILInst<0x004B, (ins), "ldind.u4">;
def CIL_Ldind_i8 : CILInst<0x004C, (ins), "ldind.i8">;
def CIL_Ldind_i : CILInst<0x004D, (ins), "ldind.i">;
def CIL_Ldind_r4 : CILInst<0x004E, (ins), "ldind.r4">;
def CIL_Ldind_r8 : CILInst<0x004F, (ins), "ldind.r8">;
def CIL_Ldind_ref : CILInst<0x0050, (ins), "ldind.ref">;
def CIL_Stind_ref : CILInst<0x0051, (ins), "stind.ref">;
def CIL_Stind_i1 : CILInst<0x0052, (ins), "stind.i1">;
def CIL_Stind_i2 : CILInst<0x0053, (ins), "stind.i2">;
def CIL_Stind_i4 : CILInst<0x0054, (ins), "stind.i4">;
def CIL_Stind_i8 : CILInst<0x0055, (ins), "stind.i8">;
def CIL_Stind_r4 : CILInst<0x0056, (ins), "stind.r4">;
def CIL_Stind_r8 : CILInst<0x0057, (ins), "stind.r8">;
def CIL_Add : CILInst<0x0058, (ins), "add">;
def CIL_Sub : CILInst<0x0059, (ins), "sub">;
def CIL_Mul : CILInst<0x005A, (ins), "mul">;
def CIL_Div : CILInst<0x005B, (ins), "div">;
def CIL_Div_un : CILInst<0x005C, (ins), "div.un">;
def CIL_Rem : CILInst<0x005D, (ins), "rem">;
def CIL_Rem_un : CILInst<0x005E, (ins), "rem.un">;
def CIL_And : CILInst<0x005F, (ins), "and">;
def CIL_Or : CILInst<0x0060, (ins), "or">;
def CIL_Xor : CILInst<0x0061, (ins), "xor">;
def CIL_Shl : CILInst<0x0062, (ins), "shl">;
def CIL_Shr : CILInst<0x0063, (ins), "shr">;
def CIL_Shr_un : CILInst<0x0064, (ins), "shr.un">;
def CIL_Neg : CILInst<0x0065, (ins), "neg">;
def CIL_Not : CILInst<0x0066, (ins), "not">;
def CIL_Conv_i1 : CILInst<0x0067, (ins), "conv.i1">;
def CIL_Conv_i2 : CILInst<0x0068, (ins), "conv.i2">;
def CIL_Conv_i4 : CILInst<0x0069, (ins), "conv.i4">;
def CIL_Conv_i8 : CILInst<0x006A, (ins), "conv.i8">;
def CIL_Conv_r4 : CILInst<0x006B, (ins), "conv.r4">;
def CIL_Conv_r8 : CILInst<0x006C, (ins), "conv.r8">;
def CIL_Conv_u4 : CILInst<0x006D, (ins), "conv.u4">;
def CIL_Conv_u8 : CILInst<0x006E, (ins), "conv.u8">;
let isCall = 1 in
def CIL_Callvirt : CILInst<0x006F, (ins cil_token_op:$tok), "callvirt $tok">;
def CIL_Cpobj : CILInst<0x0070, (ins cil_token_op:$tok), "cpobj $tok">;
def CIL_Ldobj : CILInst<0x0071, (ins cil_token_op:$tok), "ldobj $tok">;
def CIL_Ldstr : CILInst<0x0072, (ins cil_token_op:$tok), "ldstr $tok">;
let isCall = 1 in
def CIL_Newobj : CILInst<0x0073, (ins cil_token_op:$tok), "newobj $tok">;
def CIL_Castclass : CILInst<0x0074, (ins cil_token_op:$tok), "castclass $tok">;
def CIL_Isinst : CILInst<0x0075, (ins cil_token_op:$tok), "isinst $tok">;
def CIL_Conv_r_un : CILInst<0x0076, (ins), "conv.r.un">;
def CIL_Unbox : CILInst<0x0079, (ins cil_token_op:$tok), "unbox $tok">;
let isTerminator = 1, isBarrier = 1 in
def CIL_Throw : CILInst<0x007A, (ins), "throw">;
def CIL_Ldfld : CILInst<0x007B, (ins cil_token_op:$tok), "ldfld $tok">;
def CIL_Ldflda : CILInst<0x007C, (ins cil_token_op:$tok), "ldflda $tok">;
def CIL_Stfld : CILInst<0x007D, (ins cil_token_op:$tok), "stfld $tok">;
def CIL_Ldsfld : CILInst<0x007E, (ins cil_token_op:$tok), "ldsfld $tok">;
def CIL_Ldsflda : CILInst<0x007F, (ins cil_token_op:$tok), "ldsflda $tok">;
def CIL_Stsfld : CILInst<0x0080, (ins cil_token_op:$tok), "stsfld $tok">;
def CIL_Stobj : CILInst<0x0081, (ins cil_token_op:$tok), "stobj $tok">;
def CIL_Conv_ovf_i1_un : CILInst<0x0082, (ins), "conv.ovf.i1.un">;
def CIL_Conv_ovf_i2_un : CILInst<0x0083, (ins), "conv.ovf.i2.un">;
def CIL_Conv_ovf_i4_un : CILInst<0x0084, (ins), "conv.ovf.i4.un">;
def CIL_Conv_ovf_i8_un : CILInst<0x0085, (ins), "conv.ovf.i8.un">;
def CIL_Conv_ovf_u1_un : CILInst<0x0086, (ins), "conv.ovf.u1.un">;
def CIL_Conv_ovf_u2_un : CILInst<0x0087, (ins), "conv.ovf.u2.un">;
def CIL_Conv_ovf_u4_un : CILInst<0x0088, (ins), "conv.ovf.u4.un">;
def CIL_Conv_ovf_u8_un : CILInst<0x0089, (ins), "conv.ovf.u8.un">;
def CIL_Conv_ovf_i_un : CILInst<0x008A, (ins), "conv.ovf.i.un">;
def CIL_Conv_ovf_u_un : CILInst<0x008B, (ins), "conv.ovf.u.un">;
def CIL_Box : CILInst<0x008C, (ins cil_token_op:$tok), "box $tok">;
def CIL_Newarr : CILInst<0x008D, (ins cil_token_op:$tok), "newarr $tok">;
def CIL_Ldlen : CILInst<0x008E, (ins), "ldlen">;
def CIL_Ldelema : CILInst<0x008F, (ins cil_token_op:$tok), "ldelema $tok">;
def CIL_Ldelem_i1 : CILInst<0x0090, (ins), "ldelem.i1">;
def CIL_Ldelem_u1 : CILInst<0x0091, (ins), "ldelem.u1">;
def CIL_Ldelem_i2 : CILInst<0x0092, (ins), "ldelem.i2">;
def CIL_Ldelem_u2 : CILInst<0x0093, (ins), "ldelem.u2">;
def CIL_Ldelem_i4 : CILInst<0x0094, (ins), "ldelem.i4">;
def CIL_Ldelem_u4 : CILInst<0x0095, (ins), "ldelem.u4">;
def CIL_Ldelem_i8 : CILInst<0x0096, (ins), "ldelem.i8">;
def CIL_Ldelem_i : CILInst<0x0097, (ins), "ldelem.i">;
def CIL_Ldelem_r4 : CILInst<0x0098, (ins), "ldelem.r4">;
def CIL_Ldelem_r8 : CILInst<0x0099, (ins), "ldelem.r8">;
def CIL_Ldelem_ref : CILInst<0x009A, (ins), "ldelem.ref">;
def CIL_Stelem_i : CILInst<0x009B, (ins), "stelem.i">;
def CIL_Stelem_i1 : CILInst<0x009C, (ins), "stelem.i1">;
def CIL_Stelem_i2 : CILInst<0x009D, (ins), "stelem.i2">;
def CIL_Stelem_i4 : CILInst<0x009E, (ins), "stelem.i4">;
def CIL_Stelem_i8 : CILInst<0x009F, (ins), "stelem.i8">;
def CIL_Stelem_r4 : CILInst<0x00A0, (ins), "stelem.r4">;
def CIL_Stelem_r8 : CILInst<0x00A1, (ins), "stelem.r8">;
def CIL_Stelem_ref : CILInst<0x00A2, (ins), "stelem.ref">;
def CIL_Ldelem : CILInst<0x00A3, (ins cil_token_op:$tok), "ldelem $tok">;
def CIL_Stelem : CILInst<0x00A4, (ins cil_token_op:$tok), "stelem $tok">;
def CIL_Unbox_any : CILInst<0x00A5, (ins cil_token_op:$tok), "unbox.any $tok">;
def CIL_Conv_ovf_i1 : CILInst<0x00B3, (ins), "conv.ovf.i1">;
def CIL_Conv_ovf_u1 : CILInst<0x00B4, (ins), "conv.ovf.u1">;
def CIL_Conv_ovf_i2 : CILInst<0x00B5, (ins), "conv.ovf.i2">;
def CIL_Conv_ovf_u2 : CILInst<0x00B6, (ins), "conv.ovf.u2">;
def CIL_Conv_ovf_i4 : CILInst<0x00B7, (ins), "conv.ovf.i4">;
def CIL_Conv_ovf_u4 : CILInst<0x00B8, (ins), "conv.ovf.u4">;
def CIL_Conv_ovf_i8 : CILInst<0x00B9, (ins), "conv.ovf.i8">;
def CIL_Conv_ovf_u8 : CILInst<0x00BA, (ins), "conv.ovf.u8">;
def CIL_Refanyval : CILInst<0x00C2, (ins cil_token_op:$tok), "refanyval $tok">;
def CIL_Ckfinite : CILInst<0x00C3, (ins), "ckfinite">;
def CIL_Mkrefany : CILInst<0x00C6, (ins cil_token_op:$tok), "mkrefany $tok">;
def CIL_Ldtoken : CILInst<0x00D0, (ins cil_token_op:$tok), "ldtoken $tok">;
def CIL_Conv_u2 : CILInst<0x00D1, (ins), "conv.u2">;
def CIL_Conv_u1 : CILInst<0x00D2, (ins), "conv.u1">;
def CIL_Conv_i : CILInst<0x00D3, (ins), "conv.i">;
def CIL_Conv_ovf_i : CILInst<0x00D4, (ins), "conv.ovf.i">;
def CIL_Conv_ovf_u : CILInst<0x00D5, (ins), "conv.ovf.u">;
def CIL_Add_ovf : CILInst<0x00D6, (ins), "add.ovf">;
def CIL_Add_ovf_un : CILInst<0x00D7, (ins), "add.ovf.un">;
def CIL_Mul_ovf : CILInst<0x00D8, (ins), "mul.ovf">;
def CIL_Mul_ovf_un : CILInst<0x00D9, (ins), "mul.ovf.un">;
def CIL_Sub_ovf : CILInst<0x00DA, (ins), "sub.ovf">;
def CIL_Sub_ovf_un : CILInst<0x00DB, (ins), "sub.ovf.un">;
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def CIL_Endfinally : CILInst<0x00DC, (ins), "endfinally">;
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def CIL_Leave : CILInst<0x00DD, (ins bb_op:$dst), "leave $dst">;
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def CIL_Leave_s : CILInst<0x00DE, (ins bb_op:$dst), "leave.s $dst">;
def CIL_Stind_i : CILInst<0x00DF, (ins), "stind.i">;
def CIL_Conv_u : CILInst<0x00E0, (ins), "conv.u">;
def CIL_Arglist : CILInst<0xFE00, (ins), "arglist">;
def CIL_Ceq : CILInst<0xFE01, (ins), "ceq">;
def CIL_Cgt : CILInst<0xFE02, (ins), "cgt">;
def CIL_Cgt_un : CILInst<0xFE03, (ins), "cgt.un">;
def CIL_Clt : CILInst<0xFE04, (ins), "clt">;
def CIL_Clt_un : CILInst<0xFE05, (ins), "clt.un">;
def CIL_Ldftn : CILInst<0xFE06, (ins cil_token_op:$tok), "ldftn $tok">;
def CIL_Ldvirtftn : CILInst<0xFE07, (ins cil_token_op:$tok), "ldvirtftn $tok">;
def CIL_Ldarg : CILInst<0xFE09, (ins arg_op:$arg), "ldarg $arg">;
def CIL_Ldarga : CILInst<0xFE0A, (ins arg_op:$arg), "ldarga $arg">;
def CIL_Starg : CILInst<0xFE0B, (ins arg_op:$arg), "starg $arg">;
def CIL_Ldloc : CILInst<0xFE0C, (ins local_op:$local), "ldloc $local">;
def CIL_Ldloca : CILInst<0xFE0D, (ins local_op:$local), "ldloca $local">;
def CIL_Stloc : CILInst<0xFE0E, (ins local_op:$local), "stloc $local">;
def CIL_Localloc : CILInst<0xFE0F, (ins), "localloc">;
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def CIL_Endfilter : CILInst<0xFE11, (ins), "endfilter">;
def CIL_Unaligned : CILInst<0xFE12, (ins i32imm_op:$imm), "unaligned. $imm">;
def CIL_Volatile : CILInst<0xFE13, (ins), "volatile.">;
def CIL_Tail : CILInst<0xFE14, (ins), "tail.">;
def CIL_Initobj : CILInst<0xFE15, (ins cil_token_op:$tok), "initobj $tok">;
def CIL_Constrained : CILInst<0xFE16, (ins cil_token_op:$tok),
                              "constrained. $tok">;
def CIL_Cpblk : CILInst<0xFE17, (ins), "cpblk">;
def CIL_Initblk : CILInst<0xFE18, (ins), "initblk">;
def CIL_No : CILInst<0xFE19, (ins i32imm_op:$imm), "no. $imm">;
let isTerminator = 1, isBarrier = 1 in
def CIL_Rethrow : CILInst<0xFE1A, (ins), "rethrow">;
def CIL_Sizeof : CILInst<0xFE1C, (ins cil_token_op:$tok), "sizeof $tok">;
def CIL_Refanytype : CILInst<0xFE1D, (ins), "refanytype">;
def CIL_Readonly : CILInst<0xFE1E, (ins), "readonly.">;
