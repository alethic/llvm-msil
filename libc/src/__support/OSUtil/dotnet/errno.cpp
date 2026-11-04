//===-- errno provider for the .NET portable targets ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"

#if LIBC_ERRNO_MODE != LIBC_ERRNO_MODE_EXTERNAL
#error "the dotnet C library defines __llvm_libc_errno: errno mode EXTERNAL"
#endif

namespace LIBC_NAMESPACE_DECL {

namespace {
// The single errno cell of the library. Every thread that calls into the
// library shares it, as in any single-threaded build of the library
// (LIBC_THREAD_MODE_SINGLE), until per-thread state exists.
int errno_cell;
} // namespace

// The public errno is (*__llvm_libc_errno()) (include/errno.h.def). This
// definition is hidden like every libc symbol (-fvisibility=hidden and the
// hidden LIBC_NAMESPACE_DECL), so a libc object in a user's shared link
// re-exports nothing. The shared library c.dll exports it only through its
// export list: msil-ld --export-list forces hidden names into the export set.
extern "C" int *__llvm_libc_errno() noexcept { return &errno_cell; }

} // namespace LIBC_NAMESPACE_DECL
