//===-- MSILDataSegment.h - MSIL module data segment ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// STUB: implemented in M8
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the data segment of an MSIL module: the layout of the
/// global variables in the unmanaged memory block that the module
/// initializer allocates, and their initializer bytes.
///
/// Until M8 the class is empty; M8 implements it.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILDATASEGMENT_H
#define LLVM_LIB_TARGET_MSIL_MSILDATASEGMENT_H

namespace llvm {

/// The layout and initializer bytes of the module's global variables.
class MSILDataSegment {};

} // end namespace llvm

#endif
