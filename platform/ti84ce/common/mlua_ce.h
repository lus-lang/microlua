/*
 * MicroLua on the TI-84 Plus CE - shared frontend interface.
 *
 * The frontend is CE-toolchain code (hosted headers allowed); the MicroLua
 * core it links against stays freestanding via ports/ti84ce.h.
 */

#ifndef MLUA_CE_H
#define MLUA_CE_H

#include "MLuaVM.h"

/* One static buffer handed to MLuaNewConstrainedState. The OS grants a C
 * program ~60 KB of bss+heap; this leaves headroom for the frontend's own
 * statics and the toolchain runtime. */
#define MLUA_CE_HEAP_SIZE (48 * 1024)

/* Longest script list the picker handles (each entry is an appvar name). */
#define MLUA_CE_MAX_SCRIPTS 24

/* console.c - text console on the OS home screen */
void MLuaCeConsoleInit(void);
void MLuaCeConsoleWrite(const char *msg, Size len);
void MLuaCeConsoleWriteStr(const char *msg);
void MLuaCeConsolePause(void);

/* requirer.c - appvar script access */
const char *MLuaCeLoadVar(const char *name, Size *lenOut);
Size MLuaCeListScripts(char names[][9], Size max);
MLuaValue MLuaCeRequire(MLuaState *L, const char *modname);

/* bindings_ce.c - gfx.* / key.* / timer.* calculator bindings */
void MLuaCeOpenLibs(MLuaState *L);
void MLuaCeGfxCleanup(void);

/* runner.c - state lifecycle, chunk execution, script picker */
MLuaState *MLuaCeNewState(void);
MLuaStatus MLuaCeRun(MLuaState *L, const char *data, Size len,
                     const char *name);
void MLuaCeRunLoop(void);

#if MLUA_ENABLE_COMPILER
/* repl_input.c (compiler builds only) - interactive line-per-chunk REPL */
void MLuaCeRepl(void);
#endif

#endif /* MLUA_CE_H */
