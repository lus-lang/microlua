/*
 * MicroLua - embed_smoke.c
 * Minimal freestanding embedder smoke test: bring-your-own arena + an output
 * hook, compile and run an integer-only chunk, and check the captured output.
 * Proves the static core is usable from a tiny standalone embedder; it
 * complements the libc_free_symbols guard (which proves the core pulls in no
 * libc symbols) by proving the freestanding library actually links and runs.
 *
 * This harness (not the core) may use libc.
 */

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaVM.h"

#include <stdio.h>
#include <string.h>

static char OutBuf[256];
static Size OutLen;

static void CaptureOutput(MLuaState *L, int kind, const char *msg, Size len) {
  Size i;
  (void)L;
  (void)kind;
  for (i = 0; i < len && OutLen < sizeof(OutBuf) - 1; i++) {
    OutBuf[OutLen++] = msg[i];
  }
  OutBuf[OutLen] = '\0';
}

/* Bring-your-own heap: a plain static arena, no malloc. */
static U8 Arena[256 * 1024];

int main(void) {
  MLuaState *L = MLuaStateInit(Arena, sizeof(Arena));
  const char *src = "print(6 * 7)";
  MLuaStatus st;

  if (!L) {
    printf("embed smoke: state init failed\n");
    return 1;
  }

  MLuaSetOutput(L, CaptureOutput);
  MLuaOpenLibs(L); /* register the freestanding stdlib (print, math, ...) */

  st = MLuaDoString(L, src, (Size)strlen(src), "=embed");
  if (st != MLUA_OK) {
    printf("embed smoke: run failed (status %d)\n", (int)st);
    return 1;
  }

  if (strncmp(OutBuf, "42", 2) != 0) {
    printf("embed smoke: unexpected output [%s]\n", OutBuf);
    return 1;
  }

  printf("embed smoke OK: output=%.*s", (int)OutLen, OutBuf);
  return 0;
}
