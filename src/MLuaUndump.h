/*
 * MicroLua - MLuaUndump.h
 * Bytecode deserialization.
 */

#ifndef MLUA_UNDUMP_H
#define MLUA_UNDUMP_H

#include "MLuaCode.h"
#include "MLuaCore.h"

MLuaProto *MLuaUndump(MLuaState *L, const char *data, Size len);

#endif /* MLUA_UNDUMP_H */
