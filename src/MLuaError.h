/*
 * MicroLua - MLuaError.h
 * Soft Error Handling Macros (SPEC.ERRORS.md)
 *
 * Pattern: Status-Return with Propagation Macros
 * - All runtime functions that can fail return MLuaStatus
 * - Data is returned via out-parameters
 * - M_TRY flattens error propagation without nested if blocks
 */

#ifndef MLUA_ERROR_H
#define MLUA_ERROR_H

#include "MLuaAlloc.h" /* For MLuaState */
#include "MLuaCore.h"  /* For MLuaStatus */

/* ========================================================================== */
/* Error Propagation Macros                                                   */
/* ========================================================================== */

/*
 * M_TRY(expr)
 * Executes an expression that returns MLuaStatus.
 * If the result is NOT MLUA_OK, immediately returns the error code.
 *
 * Usage:
 *   M_TRY(MLuaTableGetSafe(L, tbl, key, &result));
 */
#define M_TRY(expr)                                                            \
  do {                                                                         \
    MLuaStatus _s = (expr);                                                    \
    if (_s != MLUA_OK)                                                         \
      return _s;                                                               \
  } while (0)

/*
 * M_FAIL(L, code, msg)
 * Sets the error message and returns the failure code immediately.
 *
 * Usage:
 *   M_FAIL(L, MLUA_ERR_RUNTIME, "attempt to index a nil value");
 */
#define M_FAIL(L, code, msg)                                                   \
  do {                                                                         \
    (L)->ErrorMsg = (msg);                                                     \
    return (code);                                                             \
  } while (0)

/*
 * M_ASSERT(L, cond, code, msg)
 * Quick validation helper. If condition is false, fails with the given error.
 *
 * Usage:
 *   M_ASSERT(L, IsTable(tbl), MLUA_ERR_RUNTIME, "attempt to index a nil
 * value");
 */
#define M_ASSERT(L, cond, code, msg)                                           \
  do {                                                                         \
    if (!(cond))                                                               \
      M_FAIL(L, code, msg);                                                    \
  } while (0)

#endif /* MLUA_ERROR_H */
