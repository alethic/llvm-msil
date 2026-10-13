//===- MSILInstrFrame.td - MSIL frame codegen support ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL frame code-gen constructs. The static frame of a function is one
/// 'localloc' block allocated at entry; its address is the frame base FP.
///
//===----------------------------------------------------------------------===//

// 'localloc' throws InvalidProgramException unless the size is the only value
// on the evaluation stack, so its def is never stackified and MSILStackCheck
// requires a stack depth of 1 when it executes.
let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
defm LOCALLOC : I<(outs I64:$p), (ins I32:$size), [], "localloc", 0xFE0F>;
