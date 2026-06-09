/*
 * MicroLua - MLuaStdLib.h
 * OPTIONAL io/os extension (requires the C standard library).
 *
 * Implemented in src/extensions/MLuaStdLib.c, which is NOT part of the
 * freestanding core library: link it explicitly (the bundled REPL does)
 * and call MLuaOpenStdLib after MLuaOpenLibs.
 */

#ifndef MLUA_STDLIB_H
#define MLUA_STDLIB_H

#include "MLuaAlloc.h"

/*
 * Register io.*, os.*, dofile and loadfile.
 */
void MLuaOpenStdLib(MLuaState *L);

#endif /* MLUA_STDLIB_H */
