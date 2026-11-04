/*===-- lua_nosignal.c - signal and sigaction as no-ops for lua.c ---------===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/
/* The test object of lua-suite.test (plan 5.17.15, M27). lua.c installs a
 * SIGINT handler around every chunk it runs (setsignal: signal(), or
 * sigaction() under LUA_USE_POSIX, which msil_lua_config.h selects on Linux
 * triples) and passes it the address of its C function laction. Handing a C
 * function to the host C library is M28's native callback, so until then
 * this object defines both functions, with the prototypes of the triple's
 * <signal.h>, as no-ops that install nothing: the MSIL program links these
 * definitions instead of the C library's, so no C function pointer reaches
 * native code. The Lua suite never raises a signal. lua-suite.test builds no
 * native program (FileCheck checks its end, not a transcript); on Windows a
 * native link of this object would meet the DLL C runtime's import of
 * signal, which lua.c's dllimport call pulls in. M28 runs the unmodified
 * lua.c without this object. */
#include <signal.h>
#include <string.h>

void (*signal(int sig, void (*func)(int)))(int) {
  (void)sig;
  (void)func;
  return SIG_DFL;
}

#if defined(__linux__)
int sigaction(int sig, const struct sigaction *restrict act,
              struct sigaction *restrict oact) {
  (void)sig;
  (void)act;
  if (oact)
    memset(oact, 0, sizeof *oact);
  return 0;
}
#endif
