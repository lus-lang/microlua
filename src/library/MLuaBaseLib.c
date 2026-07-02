/*
 * MicroLua - MLuaBaseLib.c
 * Base library (core globals) implementation
 */

#include "MLuaBaseLib.h"
#include "../MLuaConvert.h"
#include "../MLuaCore.h"
#include "../MLuaString.h"
#include "../MLuaTable.h"
#include "../MLuaVM.h"

/* ========================================================================== */
/* assert                                                                     */
/* ========================================================================== */

static int BaseAssert(MLuaState *L) {
  MLuaValue v = MLuaGetStack(L, 1);
  if (IsFalsy(v)) {
    const char *msg = "assertion failed!";
    if (MLuaGetTop(L) >= 2) {
      MLuaValue msgv = MLuaGetStack(L, 2);
      msg = MLuaStringData(msgv);
      if (!msg)
        msg = "assertion failed!";
    }
    L->ErrorMsg = msg;
    return -1; /* Error */
  }
  /* Return all arguments (results must be pushed, not just counted) */
  {
    int top = MLuaGetTop(L);
    int i;
    for (i = 0; i < top; i++) {
      MLuaPush(L, MLuaGetArg(L, i));
    }
    return top;
  }
}

/* ========================================================================== */
/* error                                                                      */
/* ========================================================================== */

static int BaseError(MLuaState *L) {
  MLuaValue msgv = MLuaGetStack(L, 1);
  const char *msg = MLuaStringData(msgv);
  L->ErrorMsg = msg ? msg : "error";
  return -1; /* Error */
}

#if MLUA_ENABLE_COMPILER
/* ========================================================================== */
/* load                                                                       */
/* ========================================================================== */
static int BaseLoad(MLuaState *L) {
  MLuaValue chunk = MLuaGetStack(L, 1);
  const char *chunkname = "=(load)";
  const char *source;
  Size len;
  MLuaStatus status;

  /* Get chunk name if provided */
  if (MLuaGetTop(L) >= 4) {
    /* load(chunk, chunkname, mode, env) - use 4th arg as name */
    MLuaValue namev = MLuaGetStack(L, 2);
    const char *n = MLuaStringData(namev);
    if (n)
      chunkname = n;
  }

  /* Get source string */
  source = MLuaStringData(chunk);
  len = MLuaStringLen(chunk);

  if (!source || len == 0) {
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, MLuaStringNew(L, "chunk is not a string", 21));
    return 2;
  }

  status = MLuaLoadBuffer(L, source, len, chunkname);

  if (status == MLUA_OK) {
    /* Function is already on stack from MLuaLoadBuffer */
    return 1;
  } else {
    /* Error message is on stack from MLuaLoadBuffer */
    MLuaValue err = MLuaPop(L);
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, err);
    return 2;
  }
}
#endif

/* ========================================================================== */
/* next                                                                       */
/* ========================================================================== */

static int BaseNext(MLuaState *L) {
  MLuaValue tbl = MLuaGetStack(L, 1);
  MLuaValue key = MLuaGetTop(L) >= 2 ? MLuaGetStack(L, 2) : MLUA_NIL;
  MLuaValue nextVal;
  MLuaValue nextKey;

  nextKey = MLuaTableNext(tbl, key, &nextVal);
  if (IsNil(nextKey)) {
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  MLuaPush(L, nextKey);
  MLuaPush(L, nextVal);
  return 2;
}

/* ========================================================================== */
/* pairs                                                                      */
/* ========================================================================== */

static int PairsIter(MLuaState *L) { return BaseNext(L); }

static int BasePairs(MLuaState *L) {
  MLuaValue tbl = MLuaGetStack(L, 1);
  /* Return: next function, table, nil */
  MLuaPush(L, MLuaRegisterCFunc(L, PairsIter));
  MLuaPush(L, tbl);
  MLuaPush(L, MLUA_NIL);
  return 3;
}

/* ========================================================================== */
/* ipairs                                                                     */
/* ========================================================================== */

static int IpairsIter(MLuaState *L) {
  MLuaValue tbl = MLuaGetStack(L, 1);
  MLuaValue idx = MLuaGetStack(L, 2);
  I32 i = GetInt(idx) + 1;
  MLuaValue v = MLuaTableGet(L, tbl, MakeInt(i));

  if (IsNil(v)) {
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  MLuaPush(L, MakeInt(i));
  MLuaPush(L, v);
  return 2;
}

static int BaseIpairs(MLuaState *L) {
  MLuaValue tbl = MLuaGetStack(L, 1);
  /* Return: iterator, table, 0 */
  MLuaPush(L, MLuaRegisterCFunc(L, IpairsIter));
  MLuaPush(L, tbl);
  MLuaPush(L, MakeInt(0));
  return 3;
}

/* ========================================================================== */
/* pcall                                                                      */
/* ========================================================================== */

static int BasePcall(MLuaState *L) {
  int top = MLuaGetTop(L);
  Size entryTop;
  int status;
  int i;

  if (top < 1) {
    MLuaPush(L, MLUA_FALSE);
    MLuaPush(L, MLuaStringNew(L, "bad argument #1 (value expected)", 32));
    return 2;
  }

  /*
   * Our arguments live in the Args window (OP_CALL moved them there), but
   * MLuaPCall expects function+args on the EvalStack. Reserve the status
   * slot first so true/false precedes the results, then re-push.
   */
  entryTop = L->EvalTop;
  MLuaPush(L, MLUA_NIL); /* placeholder for the status boolean */
  for (i = 0; i < top; i++) {
    MLuaPush(L, MLuaGetArg(L, i));
  }

  status = MLuaPCall(L, top - 1, -1, 0);

  /* On success the results follow the placeholder; on error MLuaPCall
   * restored the stack and pushed the error message there instead. */
  L->EvalStack[entryTop] = (status == MLUA_OK) ? MLUA_TRUE : MLUA_FALSE;
  return (int)(L->EvalTop - entryTop);
}

/* ========================================================================== */
/* select                                                                     */
/* ========================================================================== */

static int BaseSelect(MLuaState *L) {
  int top = MLuaGetTop(L);
  MLuaValue idx = MLuaGetStack(L, 1);

  if (IsInt(idx)) {
    I32 n = MLuaGetIntVal(idx);
    int i;
    if (n < 0) {
      n = top + n + 1;
    }
    if (n < 1)
      n = 1;
    if (n > top) {
      return 0;
    }
    /* Return values from index n to top */
    for (i = n + 1; i <= top; i++) {
      MLuaPush(L, MLuaGetStack(L, i));
    }
    return top - n;
  } else {
    /* Check for "#" */
    const char *s = MLuaStringData(idx);
    if (s && s[0] == '#') {
      MLuaPush(L, MakeInt((I32)(top - 1)));
      return 1;
    }
  }
  return 0;
}

/* ========================================================================== */
/* tonumber                                                                   */
/* ========================================================================== */

static int BaseTonumber(MLuaState *L) {
  MLuaValue v = MLuaGetStack(L, 1);
  int base = 10;

  /* Check for base argument */
  if (MLuaGetTop(L) >= 2) {
    MLuaValue vbase = MLuaGetStack(L, 2);
    base = GetInt(vbase);
  }

  if (MLuaIsNumber(v) && base == 10) {
    MLuaPush(L, v);
    return 1;
  }

  /* Try to convert string to number */
  const char *s = MLuaStringData(v);
  Size len = MLuaStringLen(v);

  if (s && len > 0) {
    if (base == 10) {
      double result;
      if (MLuaStrToNumber(s, len, &result)) {
        MLuaPush(L, MLuaMakeNumber(L, result));
        return 1;
      }
    } else {
      I64 result;
      if (MLuaStrToInt(s, len, base, &result)) {
        MLuaPush(L, MLuaMakeInt(L, (I32)result));
        return 1;
      }
    }
  }

  MLuaPush(L, MLUA_NIL);
  return 1;
}

/* ========================================================================== */
/* tostring                                                                   */
/* ========================================================================== */

static int BaseTostring(MLuaState *L) {
  MLuaValue v = MLuaGetStack(L, 1);
  char buf[128];
  Size len;

  /* Strings are returned as-is. Numbers must be formatted instead — and on the
   * 32-bit path boxed integers and heap floats are heap pointers, while
   * MLuaStringData returns "" (a non-NULL pointer) for non-strings, so the
   * pointer test below cannot by itself exclude them. Gate on !MLuaIsNumber so
   * every numeric value falls through to MLuaValueToStr. */
  if (!MLuaIsNumber(v) && (IsShortStr(v) || (IsPtr(v) && MLuaStringData(v)))) {
    MLuaPush(L, v);
    return 1;
  }

  /* Use MLuaValueToStr for all other types */
  len = MLuaValueToStr(L, v, buf, sizeof(buf));
  {
    MLuaValue res = MLuaStringNew(L, buf, len);
    if (IsNil(res)) {
      return -1; /* ErrorMsg set by the failed creation */
    }
    MLuaPush(L, res);
  }
  return 1;
}

/* ========================================================================== */
/* type                                                                       */
/* ========================================================================== */

static int BaseType(MLuaState *L) {
  MLuaValue v = MLuaGetStack(L, 1);
  const char *typeName = MLuaTypeName(v);
  MLuaPush(L, MLuaStringNew(L, typeName, StrLen(typeName)));
  return 1;
}

/* ========================================================================== */
/* xpcall                                                                     */
/* ========================================================================== */

static int BaseXpcall(MLuaState *L) {
  /* xpcall(f, msgh, ...) - call f with error handler msgh */
  int top = MLuaGetTop(L);
  MLuaValue msgh;
  MLuaValue errval;
  Size entryTop;
  int status;
  int i;

  if (top < 2) {
    MLuaPush(L, MLUA_FALSE);
    MLuaPush(L, MLuaStringNew(L, "bad argument #2 (function expected)", 35));
    return 2;
  }

  /* Save error handler by value: the Args window is overwritten by the
   * protected call below */
  msgh = MLuaGetArg(L, 1);

  /* Same placeholder pattern as pcall; args 3..top follow the function */
  entryTop = L->EvalTop;
  MLuaPush(L, MLUA_NIL);
  MLuaPush(L, MLuaGetArg(L, 0));
  for (i = 2; i < top; i++) {
    MLuaPush(L, MLuaGetArg(L, i));
  }

  status = MLuaPCall(L, top - 2, -1, 0);

  if (status == MLUA_OK) {
    L->EvalStack[entryTop] = MLUA_TRUE;
    return (int)(L->EvalTop - entryTop);
  }

  /* Stack is [placeholder, errmsg]: take the error, run the handler */
  errval = MLuaPop(L);

  if (!IsNil(msgh) && (IsPtr(msgh) || IsLightFunc(msgh))) {
    MLuaPush(L, msgh);
    MLuaPush(L, errval);
    if (MLuaPCall(L, 1, 1, 0) == MLUA_OK) {
      errval = MLuaPop(L); /* Handler's result replaces the error */
    } else {
      MLuaPop(L); /* Handler failed: drop its error, keep the original */
    }
  }

  L->EvalStack[entryTop] = MLUA_FALSE;
  MLuaPush(L, errval);
  return (int)(L->EvalTop - entryTop);
}

/* ========================================================================== */
/* Library Registration                                                       */
/* ========================================================================== */

void MLuaOpenBase(MLuaState *L) {
  MLuaRegisterGlobal(L, "assert", BaseAssert);
  MLuaRegisterGlobal(L, "error", BaseError);
#if MLUA_ENABLE_COMPILER
  MLuaRegisterGlobal(L, "load", BaseLoad);
  MLuaRegisterGlobal(L, "loadstring", BaseLoad); /* Lua 5.1 alias */
#endif
  MLuaRegisterGlobal(L, "next", BaseNext);
  MLuaRegisterGlobal(L, "pairs", BasePairs);
  MLuaRegisterGlobal(L, "ipairs", BaseIpairs);
  MLuaRegisterGlobal(L, "pcall", BasePcall);
  MLuaRegisterGlobal(L, "select", BaseSelect);
  MLuaRegisterGlobal(L, "tonumber", BaseTonumber);
  MLuaRegisterGlobal(L, "tostring", BaseTostring);
  MLuaRegisterGlobal(L, "type", BaseType);
  MLuaRegisterGlobal(L, "xpcall", BaseXpcall);
}
