/*
 * MicroLua - SmallHeapRunner.c
 * Debug driver: run a Lua script in a small constrained heap so the GC
 * actually fires. Usage: small_heap_runner <script.lua> [heapKB]
 * Prints the global 'result' (when an int) or the error.
 */

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaGC.h"
#include "MLuaString.h"
#include "MLuaVM.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static U8 Heap[1024 * 1024] __attribute__((aligned(8)));

int main(int argc, char **argv) {
  FILE *f;
  long len;
  char *src;
  Size heapSize = 96 * 1024;
  MLuaState *L;
  MLuaStatus st;
  MLuaValue v;

  if (argc < 2) {
    fprintf(stderr, "usage: %s <script.lua> [heapKB]\n", argv[0]);
    return 2;
  }
  if (argc >= 3) {
    heapSize = (Size)atol(argv[2]) * 1024;
    if (heapSize > sizeof(Heap)) {
      heapSize = sizeof(Heap);
    }
  }

  f = fopen(argv[1], "rb");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", argv[1]);
    return 2;
  }
  fseek(f, 0, SEEK_END);
  len = ftell(f);
  fseek(f, 0, SEEK_SET);
  src = malloc((size_t)len + 1);
  fread(src, 1, (size_t)len, f);
  src[len] = 0;
  fclose(f);

  L = MLuaStateInit(Heap, heapSize);
  if (!L) {
    fprintf(stderr, "state init failed\n");
    return 2;
  }
  MLuaOpenLibs(L);

  st = MLuaDoString(L, src, (Size)len, argv[1]);
  if (st != MLUA_OK) {
    fprintf(stderr, "ERROR: %s\n", L->ErrorMsg ? L->ErrorMsg : "?");
    return 1;
  }

  v = MLuaGetGlobal(L, "result");
  if (IsInt(v)) {
    printf("result=%d\n", (int)GetInt(v));
  } else {
    printf("ok (no int result)\n");
  }
  printf("heap used: %lu\n", (unsigned long)MLuaMemoryUsed(L));
  return 0;
}
