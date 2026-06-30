/*
 * MicroLua - MLuaMathLib.h
 * Math library for MicroLua
 */

#ifndef MLUA_MATHLIB_H
#define MLUA_MATHLIB_H

#include "MLuaAlloc.h"

/*
 * Register the math library into the global environment.
 */
void MLuaOpenMath(MLuaState *L);

#endif /* MLUA_MATHLIB_H */
