/*
 * Exact Lua heap peak runner for Benchmark Game-derived comparisons.
 *
 * Runs one Lua script under lua_newstate with a tracking allocator. The script's
 * stdout is left untouched; the exact allocator high-water mark is printed to
 * stderr as "__BENCH_LUA_PEAK__ N".
 */

#include <stdio.h>
#include <stdlib.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

typedef struct {
  size_t Current;
  size_t Peak;
} PeakAlloc;

static void *PeakAllocator(void *ud, void *ptr, size_t osize, size_t nsize) {
  PeakAlloc *pa = (PeakAlloc *)ud;
  void *newPtr;

  if (nsize == 0) {
    if (ptr) {
      if (pa->Current >= osize) {
        pa->Current -= osize;
      } else {
        pa->Current = 0;
      }
      free(ptr);
    }
    return NULL;
  }

  newPtr = realloc(ptr, nsize);
  if (!newPtr) {
    return NULL;
  }

  if (nsize > osize) {
    pa->Current += nsize - osize;
    if (pa->Current > pa->Peak) {
      pa->Peak = pa->Current;
    }
  } else {
    pa->Current -= osize - nsize;
  }

  return newPtr;
}

int main(int argc, char **argv) {
  PeakAlloc pa = {0, 0};
  lua_State *L;
  int status;

  if (argc < 2) {
    fprintf(stderr, "usage: %s script.lua\n", argv[0]);
    return 2;
  }

  L = lua_newstate(PeakAllocator, &pa, 0);
  if (!L) {
    fprintf(stderr, "failed to create Lua state\n");
    return 1;
  }

  luaL_openlibs(L);

  status = luaL_loadfile(L, argv[1]);
  if (status == LUA_OK) {
    status = lua_pcall(L, 0, LUA_MULTRET, 0);
  }
  if (status != LUA_OK) {
    const char *msg = lua_tostring(L, -1);
    fprintf(stderr, "%s\n", msg ? msg : "Lua error");
    lua_close(L);
    return 1;
  }

  fprintf(stderr, "__BENCH_LUA_PEAK__ %lu\n", (unsigned long)pa.Peak);
  lua_close(L);
  return 0;
}
