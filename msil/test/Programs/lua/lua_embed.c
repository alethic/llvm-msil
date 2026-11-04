/*===-- lua_embed.c - Run Lua scripts in fresh states, report errors ------===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/
/* The embedding driver of lua-embed.test (plan 5.17.15, M27): Lua 5.4.9 as a
 * library (liblua.a), without lua.c and without signal(). Each script named
 * on the command line runs in a fresh state (luaL_newstate, luaL_openlibs,
 * luaL_dofile, lua_close); a script that fails prints its error message to
 * stdout. Then three chunks of the driver's own run in one more fresh state
 * through the C API: a top-level error() caught by lua_pcall with a message
 * handler, a runtime error raised by a C function (luaL_error) inside a Lua
 * call, and a coroutine resumed from C that yields twice and then fails.
 * Every Lua error is a longjmp from luaD_throw to the setjmp of
 * luaD_rawrunprotected (_setjmp/_longjmp under LUA_USE_POSIX on Linux
 * triples, setjmp/longjmp on Windows triples), every yield of a coroutine
 * too. The exit status is the number of scripts that failed, at most 255. */
#include <stdio.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

static int failures;

static void report(lua_State *L, const char *what, int status) {
  const char *msg = lua_tostring(L, -1);
  printf("%s: status %d: %s\n", what, status, msg ? msg : "(no message)");
  lua_pop(L, 1);
}

static int traceback_free(lua_State *L) {
  /* A message handler that runs on the failing stack, before the longjmp
   * unwinds it: it prefixes the message and keeps it deterministic (no
   * traceback, whose addresses and depths are not the point here). */
  const char *msg = lua_tostring(L, 1);
  lua_pushfstring(L, "handled <%s>", msg ? msg : "?");
  return 1;
}

static int c_fails(lua_State *L) {
  lua_Integer n = luaL_checkinteger(L, 1);
  if (n > 2)
    return luaL_error(L, "c_fails(%d): too large", (int)n);
  lua_pushinteger(L, n * 10);
  return 1;
}

static void run_script(const char *name) {
  lua_State *L = luaL_newstate();
  int status;
  if (L == NULL) {
    printf("%s: cannot create a state\n", name);
    ++failures;
    return;
  }
  luaL_openlibs(L);
  printf("== %s\n", name);
  fflush(stdout);
  status = luaL_dofile(L, name);
  fflush(stdout);
  if (status != LUA_OK) {
    report(L, name, status);
    ++failures;
  }
  lua_close(L);
}

static void run_driver_chunks(void) {
  lua_State *L = luaL_newstate();
  lua_State *co;
  int status, nres, i;
  if (L == NULL) {
    printf("driver: cannot create a state\n");
    ++failures;
    return;
  }
  luaL_openlibs(L);
  printf("== driver chunks\n");

  /* A top-level error caught by lua_pcall with a message handler. */
  lua_pushcfunction(L, traceback_free);
  status = luaL_loadstring(L, "local t = {} error('top-level ' .. #t + 3)");
  if (status == LUA_OK)
    status = lua_pcall(L, 0, 0, -2);
  report(L, "pcall", status);
  lua_pop(L, 1);

  /* A C function that raises an error inside a Lua call, and one that does
   * not. */
  lua_register(L, "c_fails", c_fails);
  status = luaL_dostring(L, "return c_fails(2), pcall(c_fails, 5)");
  if (status != LUA_OK) {
    report(L, "c_fails", status);
  } else {
    printf("c_fails: %d %s %s\n", (int)lua_tointeger(L, -3),
           lua_toboolean(L, -2) ? "true" : "false", lua_tostring(L, -1));
    lua_pop(L, 3);
  }

  /* A coroutine resumed from C: two yields, then an error. */
  co = lua_newthread(L);
  status = luaL_loadstring(
      co, "local a = coroutine.yield(1) local b = coroutine.yield(a + 1) "
          "error('after ' .. a .. ' and ' .. b)");
  for (i = 0; status == LUA_OK || status == LUA_YIELD; ++i) {
    if (i > 0)
      lua_pushinteger(co, i * 100);
    status = lua_resume(co, L, i > 0 ? 1 : 0, &nres);
    if (status == LUA_YIELD) {
      printf("resume %d: yield %d\n", i, (int)lua_tointeger(co, -1));
      lua_pop(co, nres);
    }
  }
  report(co, "resume", status);
  lua_close(L);
}

int main(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; ++i)
    run_script(argv[i]);
  run_driver_chunks();
  printf("scripts: %d, failed: %d\n", argc - 1, failures);
  return failures > 255 ? 255 : failures;
}
