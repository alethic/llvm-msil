//===- MSILInstrConv.td - MSIL conversion codegen ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL integer and floating-point conversion code-gen constructs (the CIL
/// conv.* opcodes, named CONV_<suffix>_<source class>). The records arrive
/// with integer and floating-point instruction selection.
///
//===----------------------------------------------------------------------===//
