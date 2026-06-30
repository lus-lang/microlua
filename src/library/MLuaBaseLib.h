/*
 * MicroLua - MLuaBaseLib.h
 * Base library (core globals) for MicroLua
 */

#ifndef MLUA_BASELIB_H
#define MLUA_BASELIB_H

#include "../MLuaAlloc.h"

/*
 * Register the base library (core globals) into the environment.
 */
void MLuaOpenBase(MLuaState *L);

#endif /* MLUA_BASELIB_H */
