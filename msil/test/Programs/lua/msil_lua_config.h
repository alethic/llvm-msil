/*===-- msil_lua_config.h - Lua's platform macro for both triples --------===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/
/* Force-included (-include) into every Lua compile of lua-embed.test,
 * lua-suite.test and ../det/lua.test, for the MSIL builds and the hosted
 * native builds alike (plan 5.17.15, M27). Lua's makefile selects the
 * platform with -DLUA_USE_LINUX ('make linux'); on Windows luaconf.h selects
 * LUA_USE_WINDOWS by itself. One REDEFINE: line cannot name a macro for one
 * triple only, so this header defines LUA_USE_LINUX under __linux__ (the
 * x86_64-unknown-linux-gnu-msil triple and its E7h oracle both define it).
 * LUA_USE_LINUX selects LUA_USE_POSIX (_setjmp and _longjmp for Lua's errors
 * and coroutine yields in ldo.c, mkstemp, popen, isatty, sigaction in lua.c)
 * and LUA_USE_DLOPEN (dlopen for package.loadlib). */
#ifndef MSIL_LUA_CONFIG_H
#define MSIL_LUA_CONFIG_H
#if defined(__linux__) && !defined(LUA_USE_LINUX)
#define LUA_USE_LINUX
#endif
#endif
