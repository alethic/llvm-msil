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
/// 'localloc' block allocated at entry; its address is the frame base FP. A
/// dynamic alloca on no CFG cycle is a 'localloc' of its own in the middle of
/// the function (plan 5.15.8 H0).
///
//===----------------------------------------------------------------------===//

// 'localloc' throws InvalidProgramException unless the size is the only value
// on the evaluation stack, so its def is never stackified and MSILStackCheck
// requires a stack depth of 1 when it executes. LOCALLOC allocates the static
// frame, whose size ldc.i4 gives; LOCALLOC_I64 allocates a dynamic alloca,
// whose size is a native int: CIL 'localloc' takes a native unsigned int, and
// truncating a 64-bit size to int32 would allocate too little.
let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in {
defm LOCALLOC : I<(outs I64:$p), (ins I32:$size), [], "localloc", 0xFE0F>;
defm LOCALLOC_I64 : I<(outs I64:$p), (ins I64:$size), [], "localloc", 0xFE0F>;
}
