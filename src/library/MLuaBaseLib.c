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
#include <stdio.h>

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
  /* Return all arguments */
  return MLuaGetTop(L);
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

  /* Compile the chunk */
  status = MLuaLoadString(L, source, len, chunkname);

  if (status == MLUA_OK) {
    /* Function is already on stack from MLuaLoadString */
    return 1;
  } else {
    /* Error message is on stack from MLuaLoadString */
    MLuaValue err = MLuaPop(L);
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, err);
    return 2;
  }
}

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
  int nargs = top - 1;
  int status;

  if (top < 1) {
    MLuaPush(L, MLUA_FALSE);
    return 1;
  }

  /* Get function and call it */
  status = MLuaPCall(L, nargs, -1, 0);

  if (status == MLUA_OK) {
    /* Prepend true to results */
    /* Results are already on stack, just add true at beginning */
    MLuaPush(L, MLUA_TRUE);
    return MLuaGetTop(L);
  } else {
    /* Return false + error message */
    MLuaPush(L, MLUA_FALSE);
    if (L->ErrorMsg) {
      MLuaPush(L, MLuaStringNew(L, L->ErrorMsg, StrLen(L->ErrorMsg)));
    } else {
      MLuaPush(L, MLuaStringNew(L, "error", 5));
    }
    return 2;
  }
}

/* ========================================================================== */
/* select                                                                     */
/* ========================================================================== */

static int BaseSelect(MLuaState *L) {
  int top = MLuaGetTop(L);
  MLuaValue idx = MLuaGetStack(L, 1);

  if (IsInt(idx)) {
    I32 n = GetInt(idx);
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
    Size len;
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
        MLuaPush(L, MakeInt((I32)result));
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

  /* Strings are returned as-is */
  if (IsShortStr(v) || (IsPtr(v) && MLuaStringData(v))) {
    MLuaPush(L, v);
    return 1;
  }

  /* Use MLuaValueToStr for all other types */
  len = MLuaValueToStr(L, v, buf, sizeof(buf));
  MLuaPush(L, MLuaStringNew(L, buf, len));
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
  int nargs = top - 2; /* Exclude f and msgh */
  MLuaValue msgh;
  int status;

  if (top < 2) {
    MLuaPush(L, MLUA_FALSE);
    MLuaPush(L, MLuaStringNew(L, "bad argument #2 (function expected)", 35));
    return 2;
  }

  /* Save error handler */
  msgh = MLuaGetStack(L, 2);

  /* Call the function (msgh position not used yet in simple impl) */
  status = MLuaPCall(L, nargs, -1, 0);

  if (status == MLUA_OK) {
    /* Prepend true to results */
    MLuaPush(L, MLUA_TRUE);
    return MLuaGetTop(L);
  } else {
    /* Get error message */
    const char *errmsg = L->ErrorMsg ? L->ErrorMsg : "error";
    MLuaValue errval = MLuaStringNew(L, errmsg, StrLen(errmsg));

    /* Call error handler if it's a function */
    if (!IsNil(msgh) && (IsPtr(msgh) || IsLightFunc(msgh))) {
      /* Push msgh and error, call it */
      MLuaPush(L, msgh);
      MLuaPush(L, errval);
      int hstatus = MLuaPCall(L, 1, 1, 0);
      if (hstatus == MLUA_OK) {
        /* Handler result is on stack */
        errval = MLuaPop(L);
      }
      /* If handler fails, we use original error */
    }

    /* Return false + processed error */
    MLuaPush(L, MLUA_FALSE);
    MLuaPush(L, errval);
    return 2;
  }
}

/* ========================================================================== */
/* Library Registration                                                       */
/* ========================================================================== */

void MLuaOpenBase(MLuaState *L) {
  MLuaRegisterGlobal(L, "assert", BaseAssert);
  MLuaRegisterGlobal(L, "error", BaseError);
  MLuaRegisterGlobal(L, "load", BaseLoad);
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
