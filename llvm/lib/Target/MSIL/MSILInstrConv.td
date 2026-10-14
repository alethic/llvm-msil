//===- MSILInstrConv.td - MSIL conversion codegen ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyInstrConv.td at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL integer and floating-point conversion code-gen constructs (the CIL
/// conv.* opcodes, named CONV_<suffix>_<source class>). Every record is one
/// CIL opcode; an extension that needs two opcodes is a pattern of two
/// records. None of the conversions traps (no conv.ovf.*).
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Integer conversions.
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
// int32 <-> native int (decision D2): conv.i sign-extends, conv.u
// zero-extends and conv.i4 keeps the low 32 bits.
defm CONV_I_I32 : I<(outs I64:$dst), (ins I32:$src),
                    [(set I64:$dst, (sext I32:$src))], "conv.i", 0xD3>;
defm CONV_U_I32 : I<(outs I64:$dst), (ins I32:$src),
                    [(set I64:$dst, (zext I32:$src))], "conv.u", 0xE0>;
defm CONV_I4_I64 : I<(outs I32:$dst), (ins I64:$src),
                     [(set I32:$dst, (trunc I64:$src))], "conv.i4", 0x69>;

// Sign and zero extension from 8 and 16 bits within an int32.
defm CONV_I1_I32 : I<(outs I32:$dst), (ins I32:$src),
                     [(set I32:$dst, (sext_inreg I32:$src, i8))],
                     "conv.i1", 0x67>;
defm CONV_I2_I32 : I<(outs I32:$dst), (ins I32:$src),
                     [(set I32:$dst, (sext_inreg I32:$src, i16))],
                     "conv.i2", 0x68>;
defm CONV_U1_I32 : I<(outs I32:$dst), (ins I32:$src),
                     [(set I32:$dst, (and I32:$src, 0xff))], "conv.u1", 0xD2>;
defm CONV_U2_I32 : I<(outs I32:$dst), (ins I32:$src),
                     [(set I32:$dst, (and I32:$src, 0xffff))], "conv.u2",
                     0xD1>;

// conv.i1 and conv.i2 of a native int: the low 8 or 16 bits, sign-extended
// to an int32. They exist for the native int sign extensions below.
defm CONV_I1_I64 : I<(outs I32:$dst), (ins I64:$src), [], "conv.i1", 0x67>;
defm CONV_I2_I64 : I<(outs I32:$dst), (ins I64:$src), [], "conv.i2", 0x68>;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

// Expand a "don't care" extend into zero-extend, as WebAssembly does.
def : Pat<(i64 (anyext I32:$src)), (CONV_U_I32 I32:$src)>;

// Sign and zero extension within a native int, through an int32:
// 'conv.i1; conv.i', 'conv.i2; conv.i', 'conv.i4; conv.i' and
// 'conv.i4; conv.u'.
def : Pat<(sext_inreg I64:$src, i8), (CONV_I_I32 (CONV_I1_I64 I64:$src))>;
def : Pat<(sext_inreg I64:$src, i16), (CONV_I_I32 (CONV_I2_I64 I64:$src))>;
def : Pat<(sext_inreg I64:$src, i32), (CONV_I_I32 (CONV_I4_I64 I64:$src))>;
def : Pat<(and I64:$src, 0xffffffff), (CONV_U_I32 (CONV_I4_I64 I64:$src))>;
