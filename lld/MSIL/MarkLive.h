//===- MarkLive.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/MarkLive.h at 64f50ffd4f1c.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_MARKLIVE_H
#define LLD_MSIL_MARKLIVE_H

namespace lld::msil {
// --gc-sections (plan 5.9.11): sets the live bit of the methods, segments and
// synthetic segments of the CIL objects that the output keeps, and of the
// symbols that reach them. Without --gc-sections every chunk that is not
// discarded is live. --print-gc-sections lists the removed ones.
void markLive();
} // namespace lld::msil

#endif
