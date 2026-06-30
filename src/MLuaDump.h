/*
 * MicroLua - MLuaDump.h
 * Bytecode serialization
 */

#ifndef MLUA_DUMP_H
#define MLUA_DUMP_H

#include "MLuaCore.h"
#include "MLuaValue.h"

/*
 * Serialize a Lua closure in MicroLua's bytecode format.
 *
 * Passing buf == NULL or cap == 0 performs a sizing pass and returns the exact
 * number of bytes required. Passing a buffer writes up to cap bytes while still
 * returning the total required size, so callers can detect truncation.
 */
Size MLuaDumpFunction(MLuaState *L, MLuaValue func, char *buf, Size cap);

#endif /* MLUA_DUMP_H */
