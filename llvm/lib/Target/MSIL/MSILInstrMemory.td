//===- MSILInstrMemory.td - MSIL memory codegen support ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL memory access code-gen constructs: ldind/stind with a memflags_op
/// first operand, cpblk, initblk and the memmove and fence calls. The records
/// arrive with memory instruction selection.
///
//===----------------------------------------------------------------------===//
