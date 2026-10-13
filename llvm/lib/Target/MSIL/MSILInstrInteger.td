//===- MSILInstrInteger.td - MSIL integer codegen ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL integer arithmetic, shift and compare code-gen constructs on I32 and
/// I64 (CIL int32 and native int). The records arrive with integer
/// instruction selection.
///
//===----------------------------------------------------------------------===//
