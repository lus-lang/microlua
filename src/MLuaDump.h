/*
 * MicroLua - MLuaDump.h
 * Bytecode serialization
 */

#ifndef MLUA_DUMP_H
#define MLUA_DUMP_H

#include "MLuaCore.h"
#include "MLuaValue.h"

/* v4: fused opcodes GETGLOBAL_K, GETTABLE_LL, SETTABLE_LL, SETTABLE_POP
 * v5: dead opcodes retired and the (always-empty) Exp-Golomb line-info
 *     section removed from the proto record */
#define MLUA_BYTECODE_VERSION 5
#define MLUA_BYTECODE_ENDIAN_LITTLE 0
#define MLUA_BYTECODE_ENDIAN_BIG 1
#define MLUA_BYTECODE_FLOAT_IEEE754 1

#if MLUA_ENABLE_DUMP
/*
 * Serialize a Lua closure in MicroLua's bytecode format.
 *
 * Passing buf == NULL or cap == 0 performs a sizing pass and returns the exact
 * number of bytes required. Passing a buffer writes up to cap bytes while still
 * returning the total required size, so callers can detect truncation.
 */
Size MLuaDumpFunction(MLuaState *L, MLuaValue func, char *buf, Size cap);

/*
 * Serialize with an explicit byte order. Endian must be
 * MLUA_BYTECODE_ENDIAN_LITTLE or MLUA_BYTECODE_ENDIAN_BIG.
 */
Size MLuaDumpFunctionEndian(MLuaState *L, MLuaValue func, char *buf, Size cap,
                            int endian);
#endif /* MLUA_ENABLE_DUMP */

#endif /* MLUA_DUMP_H */
