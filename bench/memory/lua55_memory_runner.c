#include <stdio.h>
#include <stdlib.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

typedef struct {
  size_t current;
  size_t peak;
  size_t requested;
  size_t count;
  struct Block *blocks;
} PeakAlloc;

typedef struct Block {
  void *ptr;
  size_t size;
  struct Block *next;
} Block;

static Block *FindBlock(PeakAlloc *pa, void *ptr, Block **prevOut) {
  Block *prev = NULL;
  Block *b = pa->blocks;
  while (b) {
    if (b->ptr == ptr) {
      if (prevOut) {
        *prevOut = prev;
      }
      return b;
    }
    prev = b;
    b = b->next;
  }
  if (prevOut) {
    *prevOut = NULL;
  }
  return NULL;
}

static void *PeakAllocator(void *ud, void *ptr, size_t osize, size_t nsize) {
  PeakAlloc *pa = (PeakAlloc *)ud;
  Block *block;
  Block *prev;
  void *newPtr;

  (void)osize;

  if (nsize == 0) {
    block = FindBlock(pa, ptr, &prev);
    if (block) {
      pa->current -= block->size;
      if (prev) {
        prev->next = block->next;
      } else {
        pa->blocks = block->next;
      }
      free(block);
      free(ptr);
    }
    return NULL;
  }

  block = ptr ? FindBlock(pa, ptr, NULL) : NULL;
  newPtr = realloc(ptr, nsize);
  if (!newPtr) {
    return NULL;
  }
  pa->count++;
  pa->requested += nsize;
  if (block) {
    if (pa->current >= block->size) {
      pa->current -= block->size;
    } else {
      pa->current = 0;
    }
    block->ptr = newPtr;
    block->size = nsize;
  } else {
    block = (Block *)malloc(sizeof(Block));
    if (!block) {
      free(newPtr);
      return NULL;
    }
    block->ptr = newPtr;
    block->size = nsize;
    block->next = pa->blocks;
    pa->blocks = block;
  }
  pa->current += nsize;
  if (pa->current > pa->peak) {
    pa->peak = pa->current;
  }
  return newPtr;
}

static void PrintPoint(const char *label, const PeakAlloc *pa) {
  fprintf(stderr,
          "\"%s\":{\"heap_current\":%llu,\"heap_peak\":%llu,"
          "\"alloc_count\":%llu,\"alloc_requested\":%llu}",
          label, (unsigned long long)pa->current, (unsigned long long)pa->peak,
          (unsigned long long)pa->count, (unsigned long long)pa->requested);
}

int main(int argc, char **argv) {
  PeakAlloc pa = {0, 0, 0, 0, NULL};
  lua_State *L;
  int status;
  PeakAlloc afterInit, afterLoad, afterExec, afterGC;

  if (argc != 2) {
    fprintf(stderr, "usage: %s script.lua\n", argv[0]);
    return 2;
  }

  L = lua_newstate(PeakAllocator, &pa, 0);
  if (!L) {
    fprintf(stderr, "failed to create Lua state\n");
    return 1;
  }
  luaL_openlibs(L);
  afterInit = pa;

  status = luaL_loadfile(L, argv[1]);
  afterLoad = pa;
  if (status == LUA_OK) {
    status = lua_pcall(L, 0, LUA_MULTRET, 0);
  }
  afterExec = pa;
  if (status != LUA_OK) {
    const char *msg = lua_tostring(L, -1);
    fprintf(stderr, "%s\n", msg ? msg : "Lua error");
    lua_close(L);
    return 1;
  }

  lua_gc(L, LUA_GCCOLLECT, 0);
  afterGC = pa;

  fprintf(stderr, "__LUA55_MEMORY_JSON__ {");
  PrintPoint("after_init", &afterInit);
  fprintf(stderr, ",");
  PrintPoint("after_load", &afterLoad);
  fprintf(stderr, ",");
  PrintPoint("after_execute", &afterExec);
  fprintf(stderr, ",");
  PrintPoint("after_gc", &afterGC);
  fprintf(stderr, "}\n");

  lua_close(L);
  return 0;
}
