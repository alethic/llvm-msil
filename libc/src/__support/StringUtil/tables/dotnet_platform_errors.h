//===-- Map of error numbers to strings for the .NET platform ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_STRINGUTIL_TABLES_DOTNET_PLATFORM_ERRORS_H
#define LLVM_LIBC_SRC___SUPPORT_STRINGUTIL_TABLES_DOTNET_PLATFORM_ERRORS_H

#include "hdr/errno_macros.h"
#include "posix_errors.h"
#include "src/__support/StringUtil/message_mapper.h"
#include "src/__support/macros/config.h"
#include "stdc_errors.h"

namespace LIBC_NAMESPACE_DECL {

// The .NET portable targets take the generic errno set
// (llvm-libc-macros/generic-error-number-macros.h). Every number in it is in
// the C standard's or the POSIX table except ENOTBLK, which this table adds,
// so strerror and printf's %m name every errno value the library defines.
LIBC_INLINE_VAR constexpr MsgTable<1> DOTNET_ERRORS = {
    MsgMapping(ENOTBLK, "Block device required"),
};

LIBC_INLINE_VAR constexpr MsgTable<1> DOTNET_ERRNO_NAMES = {
    MsgMapping(ENOTBLK, "ENOTBLK"),
};

LIBC_INLINE_VAR constexpr auto PLATFORM_ERRORS =
    STDC_ERRORS + POSIX_ERRORS + DOTNET_ERRORS;

LIBC_INLINE_VAR constexpr auto PLATFORM_ERRNO_NAMES =
    STDC_ERRNO_NAMES + POSIX_ERRNO_NAMES + DOTNET_ERRNO_NAMES;

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_STRINGUTIL_TABLES_DOTNET_PLATFORM_ERRORS_H
