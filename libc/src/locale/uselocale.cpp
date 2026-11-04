//===-- Implementation of uselocale ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/uselocale.h"
#include "src/locale/locale.h"

#include "hdr/locale_macros.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(locale_t, uselocale, (locale_t newloc)) {
  // POSIX: return the locale in effect before the call. The current locale
  // starts as nullptr, which stands for the global locale.
  locale_t old = locale ? locale : LC_GLOBAL_LOCALE;
  if (newloc)
    locale = newloc;
  return old;
}

} // namespace LIBC_NAMESPACE_DECL
