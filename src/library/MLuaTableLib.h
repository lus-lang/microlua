/*
 * MicroLua - MLuaTableLib.h
 * Table library for MicroLua
 */

#ifndef MLUA_TABLELIB_H
#define MLUA_TABLELIB_H

#include "MLuaAlloc.h"

/*
 * Register the table library into the global environment.
 */
void MLuaOpenTable(MLuaState *L);

#endif /* MLUA_TABLELIB_H */
