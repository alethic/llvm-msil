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

//===----------------------------------------------------------------------===//
// Floating-point conversions.
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
// float32 <-> float64. conv.r8 is exact; conv.r4 rounds to nearest even.
defm CONV_R8_F32 : I<(outs F64:$dst), (ins F32:$src),
                     [(set F64:$dst, (fpextend F32:$src))], "conv.r8", 0x6C>;
defm CONV_R4_F64 : I<(outs F32:$dst), (ins F64:$src),
                     [(set F32:$dst, (fpround F64:$src))], "conv.r4", 0x6B>;

// Signed integer to floating point: one correctly rounded conversion, from an
// int32 and from a native int alike (cil-runtime e05). A native int that
// float64 cannot hold exactly reaches CONV_R4_I64 only through
// MSILTargetLowering::LowerSINT_TO_FP, which rounds it to odd first: the .NET
// Framework JIT folds conv.r4 of a constant int64 through float64.
defm CONV_R4_I32 : I<(outs F32:$dst), (ins I32:$src),
                     [(set F32:$dst, (sint_to_fp I32:$src))], "conv.r4", 0x6B>;
defm CONV_R8_I32 : I<(outs F64:$dst), (ins I32:$src),
                     [(set F64:$dst, (sint_to_fp I32:$src))], "conv.r8", 0x6C>;
defm CONV_R4_I64 : I<(outs F32:$dst), (ins I64:$src),
                     [(set F32:$dst, (sint_to_fp I64:$src))], "conv.r4", 0x6B>;
defm CONV_R8_I64 : I<(outs F64:$dst), (ins I64:$src),
                     [(set F64:$dst, (sint_to_fp I64:$src))], "conv.r8", 0x6C>;

// Unsigned int32 to floating point: conv.r.un treats the int32 as unsigned
// and converts it exactly. There is no record for a native int operand:
// conv.r.un of a 64-bit value rounds twice on .NET 8, .NET 9 and .NET
// Framework (cil-runtime e05), so uint64 -> FP is custom-lowered to the
// sequence of decision D17 (MSILTargetLowering::LowerUINT_TO_FP).
defm CONV_R_UN_I32 : I<(outs F64:$dst), (ins I32:$src),
                       [(set F64:$dst, (uint_to_fp I32:$src))], "conv.r.un",
                       0x76>;

// Floating point to integer, truncating toward zero. An out-of-range or NaN
// operand gives a runtime-dependent value and never throws, which matches
// LLVM's poison. conv.u4 and conv.u are correct for the whole unsigned range
// (isel.md 3.2).
defm CONV_I4_F32 : I<(outs I32:$dst), (ins F32:$src),
                     [(set I32:$dst, (fp_to_sint F32:$src))], "conv.i4", 0x69>;
defm CONV_U4_F32 : I<(outs I32:$dst), (ins F32:$src),
                     [(set I32:$dst, (fp_to_uint F32:$src))], "conv.u4", 0x6D>;
defm CONV_I_F32 : I<(outs I64:$dst), (ins F32:$src),
                    [(set I64:$dst, (fp_to_sint F32:$src))], "conv.i", 0xD3>;
defm CONV_U_F32 : I<(outs I64:$dst), (ins F32:$src),
                    [(set I64:$dst, (fp_to_uint F32:$src))], "conv.u", 0xE0>;
defm CONV_I4_F64 : I<(outs I32:$dst), (ins F64:$src),
                     [(set I32:$dst, (fp_to_sint F64:$src))], "conv.i4", 0x69>;
defm CONV_U4_F64 : I<(outs I32:$dst), (ins F64:$src),
                     [(set I32:$dst, (fp_to_uint F64:$src))], "conv.u4", 0x6D>;
defm CONV_I_F64 : I<(outs I64:$dst), (ins F64:$src),
                    [(set I64:$dst, (fp_to_sint F64:$src))], "conv.i", 0xD3>;
defm CONV_U_F64 : I<(outs I64:$dst), (ins F64:$src),
                    [(set I64:$dst, (fp_to_uint F64:$src))], "conv.u", 0xE0>;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

// Unsigned int32 to float32: the exact float64 from conv.r.un, then a single
// rounding with conv.r4.
def : Pat<(f32 (uint_to_fp I32:$src)), (CONV_R4_F64 (CONV_R_UN_I32 I32:$src))>;
