/*
 * MicroLua - MLuaCoroLib.h
 * Coroutine library for MicroLua
 */

#ifndef MLUA_COROLIB_H
#define MLUA_COROLIB_H

#include "MLuaAlloc.h"

/*
 * Register the coroutine library into the global environment.
 */
void MLuaOpenCoroutine(MLuaState *L);

#endif /* MLUA_COROLIB_H */
