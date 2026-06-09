/*
 * MicroLua - MLuaParse.h
 * Pratt parser for Lua source code
 */

#ifndef MLUA_PARSE_H
#define MLUA_PARSE_H

#include "MLuaCode.h"
#include "MLuaCore.h"
#include "MLuaLex.h"

/* ========================================================================== */
/* Parser State                                                               */
/* ========================================================================== */

typedef struct {
  MLuaLexer Lex;     /* Lexer state */
  MLuaFuncState *FS; /* Current function state */
  MLuaState *L;      /* Runtime state */
  const char *Error; /* Error message */
  Size ErrorLine;    /* Error line number */
  Bool LongJumps;    /* Emit long-form forward jumps (set on the re-parse
                        after a short I8 jump offset overflowed) */
} MLuaParser;

/* ========================================================================== */
/* Parser API                                                                 */
/* ========================================================================== */

/*
 * Parse a chunk of Lua source code.
 * Returns the compiled function prototype.
 *
 * @param L       Runtime state
 * @param source  Source code
 * @param len     Source length
 * @param name    Chunk name (for error messages)
 * @return        Compiled prototype, or NULL on error
 */
MLuaProto *MLuaParse(MLuaState *L, const char *source, Size len,
                     const char *name);

/*
 * Get the last parse error message.
 */
const char *MLuaParseError(MLuaParser *p);

#endif /* MLUA_PARSE_H */
