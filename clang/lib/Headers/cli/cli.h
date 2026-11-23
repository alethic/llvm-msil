/*===---- cli/cli.h - .NET reference types in C (MSIL) ---------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __CLANG_CLI_CLI_H
#define __CLANG_CLI_CLI_H

#ifndef __MSIL__
#error "<cli/cli.h> is only available on the MSIL targets"
#endif

/* CLI_TYPE("Ns.Name") makes a struct, which never has a body, stand for the
   .NET class Ns.Name; 'T ^' of such a struct T is a .NET object reference
   (clang's 'cli_type' attribute). */
#define CLI_TYPE(N) __attribute__((cli_type(N)))

#endif /* __CLANG_CLI_CLI_H */
