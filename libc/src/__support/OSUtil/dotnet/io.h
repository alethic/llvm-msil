//===-------------- .NET implementation of IO utils -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_DOTNET_IO_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_DOTNET_IO_H

#include "src/__support/CPP/string_view.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// Declared for LIBC_ASSERT and never defined: the C library of the portable
// .NET targets has no OS layer yet, so it has no output channel. A build that
// references this function (one with assertions enabled) fails to link; a
// release build (NDEBUG) references it nowhere.
void write_to_stderr(cpp::string_view msg);

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_DOTNET_IO_H
