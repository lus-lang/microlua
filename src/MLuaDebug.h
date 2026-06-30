/*
 * MicroLua - MLuaDebug.h
 * Debug utilities and macros
 *
 * Define MLUA_DEBUG to enable debug output.
 * Define MLUA_DEBUG_VERBOSE for detailed tracing.
 * All debug output goes through macros that can be disabled at compile time.
 */

#ifndef MLUA_DEBUG_H
#define MLUA_DEBUG_H

#ifdef MLUA_DEBUG
#include <stdio.h>

/* Basic debug print */
#define MLuaDebugPrint(...) fprintf(stderr, "[MLUA] " __VA_ARGS__)

/* Stack state dump */
#define MLuaDebugStack(L, msg)                                                 \
  do {                                                                         \
    fprintf(stderr, "[MLUA STACK] %s: StackTop=%zu CBase=%zu nargs=%d\n",      \
            (msg), (L)->StackTop, (L)->CBase,                                  \
            (int)((L)->StackTop - (L)->CBase));                                \
  } while (0)

/* Function call trace */
#define MLuaDebugCall(L, name)                                                 \
  do {                                                                         \
    fprintf(stderr, "[MLUA CALL] %s: StackTop=%zu CBase=%zu\n", (name),        \
            (L)->StackTop, (L)->CBase);                                        \
  } while (0)

/* Return trace */
#define MLuaDebugReturn(L, name, nresults)                                     \
  do {                                                                         \
    fprintf(stderr, "[MLUA RET] %s: nresults=%d StackTop=%zu\n", (name),       \
            (nresults), (L)->StackTop);                                        \
  } while (0)

#else
/* All debug macros become no-ops */
#define MLuaDebugPrint(...) ((void)0)
#define MLuaDebugStack(L, msg) ((void)0)
#define MLuaDebugCall(L, name) ((void)0)
#define MLuaDebugReturn(L, name, nresults) ((void)0)
#endif

/* Verbose mode - extra detailed tracing */
#ifdef MLUA_DEBUG_VERBOSE
#define MLuaDebugVerbose(...) fprintf(stderr, "[MLUA VERBOSE] " __VA_ARGS__)
#else
#define MLuaDebugVerbose(...) ((void)0)
#endif

#endif /* MLUA_DEBUG_H */
