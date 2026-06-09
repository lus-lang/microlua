/*
 * MicroLua - MLuaTableLib.c
 * Table library implementation
 */

#include "MLuaTableLib.h"
#include "../MLuaCore.h"
#include "../MLuaString.h"
#include "../MLuaTable.h"
#include "../MLuaVM.h"

/* ========================================================================== */
/* table.concat                                                               */
/* ========================================================================== */

static int TableConcat(MLuaState *L) {
  MLuaValue tbl = MLuaGetStack(L, 1);
  const char *sep = "";
  Size seplen = 0;
  Size i = 1;
  Size j;
  char buf[4096];
  Size bufpos = 0;
  int top = MLuaGetTop(L);

  if (top >= 2) {
    MLuaValue sepval = MLuaGetStack(L, 2);
    sep = MLuaStringData(sepval);
    seplen = MLuaStringLen(sepval);
  }
  if (top >= 3) {
    i = (Size)GetInt(MLuaGetStack(L, 3));
  }
  if (top >= 4) {
    j = (Size)GetInt(MLuaGetStack(L, 4));
  } else {
    j = MLuaTableLen(tbl);
  }

  if (!sep)
    sep = "";

  for (; i <= j; i++) {
    MLuaValue v = MLuaTableGet(L, tbl, MakeInt((I32)i));
    Size slen = MLuaStringLen(v);
    const char *s = MLuaStringData(v);

    /* Add separator if not first element */
    if (i > 1 && bufpos + seplen < sizeof(buf)) {
      Size k;
      for (k = 0; k < seplen; k++) {
        buf[bufpos++] = sep[k];
      }
    }

    /* Add element */
    if (s && bufpos + slen < sizeof(buf)) {
      Size k;
      for (k = 0; k < slen; k++) {
        buf[bufpos++] = s[k];
      }
    }
  }

  MLuaPush(L, MLuaStringNew(L, buf, bufpos));
  return 1;
}

/* ========================================================================== */
/* table.insert                                                               */
/* ========================================================================== */

static int TableInsert(MLuaState *L) {
  MLuaValue tbl = MLuaGetStack(L, 1);
  int top = MLuaGetTop(L);
  Size len = MLuaTableLen(tbl);

  if (top == 2) {
    /* Insert at end: table.insert(t, value) */
    MLuaValue value = MLuaGetStack(L, 2);
    MLuaTableSet(L, tbl, MakeInt((I32)(len + 1)), value);
  } else if (top >= 3) {
    /* Insert at position: table.insert(t, pos, value) */
    Size pos = (Size)GetInt(MLuaGetStack(L, 2));
    MLuaValue value = MLuaGetStack(L, 3);
    Size i;

    /* Shift elements up */
    for (i = len; i >= pos; i--) {
      MLuaValue v = MLuaTableGet(L, tbl, MakeInt((I32)i));
      MLuaTableSet(L, tbl, MakeInt((I32)(i + 1)), v);
    }
    MLuaTableSet(L, tbl, MakeInt((I32)pos), value);
  }

  return 0;
}

/* ========================================================================== */
/* table.pack                                                                 */
/* ========================================================================== */

static int TablePack(MLuaState *L) {
  int top = MLuaGetTop(L);
  MLuaValue tbl = MLuaTableNewSized(L, (Size)top, 1);
  int i;
  MLuaValue key;

  for (i = 1; i <= top; i++) {
    MLuaTableSet(L, tbl, MakeInt((I32)i), MLuaGetStack(L, i));
  }

  /* Set 'n' field */
  key = MLuaStringNew(L, "n", 1);
  MLuaTableSet(L, tbl, key, MakeInt((I32)top));

  MLuaPush(L, tbl);
  return 1;
}

/* ========================================================================== */
/* table.unpack                                                               */
/* ========================================================================== */

static int TableUnpack(MLuaState *L) {
  MLuaValue tbl = MLuaGetStack(L, 1);
  int top = MLuaGetTop(L);
  Size i = 1;
  Size j;
  Size count = 0;

  if (top >= 2) {
    i = (Size)GetInt(MLuaGetStack(L, 2));
  }
  if (top >= 3) {
    j = (Size)GetInt(MLuaGetStack(L, 3));
  } else {
    j = MLuaTableLen(tbl);
  }

  for (; i <= j; i++) {
    MLuaValue v = MLuaTableGet(L, tbl, MakeInt((I32)i));
    MLuaPush(L, v);
    count++;
  }

  return (int)count;
}

/* ========================================================================== */
/* table.remove                                                               */
/* ========================================================================== */

static int TableRemove(MLuaState *L) {
  MLuaValue tbl = MLuaGetStack(L, 1);
  int top = MLuaGetTop(L);
  Size len = MLuaTableLen(tbl);
  Size pos;
  MLuaValue removed;
  Size i;

  if (len == 0) {
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  if (top >= 2) {
    pos = (Size)GetInt(MLuaGetStack(L, 2));
  } else {
    pos = len;
  }

  /* Get the element to remove */
  removed = MLuaTableGet(L, tbl, MakeInt((I32)pos));

  /* Shift elements down */
  for (i = pos; i < len; i++) {
    MLuaValue v = MLuaTableGet(L, tbl, MakeInt((I32)(i + 1)));
    MLuaTableSet(L, tbl, MakeInt((I32)i), v);
  }

  /* Remove last element */
  MLuaTableSet(L, tbl, MakeInt((I32)len), MLUA_NIL);

  MLuaPush(L, removed);
  return 1;
}

/* ========================================================================== */
/* table.sort (quicksort implementation)                                      */
/* ========================================================================== */

/* Helper: compare two values, return true if a < b */
static Bool TableSortCompare(MLuaState *L, MLuaValue a, MLuaValue b,
                             Bool hasComp, MLuaValue compFunc) {
  if (hasComp && !IsNil(compFunc)) {
    /* Use custom comparator */
    MLuaPush(L, compFunc);
    MLuaPush(L, a);
    MLuaPush(L, b);
    MLuaCall(L, 2, 1);
    return IsTruthy(MLuaPop(L));
  } else {
    /* Default: Lua's '<' (numbers numerically, strings lexicographically) */
    return MLuaCompare(L, OP_LT, a, b);
  }
}

/* Helper: swap two table elements */
static void TableSwap(MLuaState *L, MLuaValue tbl, Size i, Size j) {
  MLuaValue a = MLuaTableGet(L, tbl, MakeInt((I32)i));
  MLuaValue b = MLuaTableGet(L, tbl, MakeInt((I32)j));
  MLuaTableSet(L, tbl, MakeInt((I32)i), b);
  MLuaTableSet(L, tbl, MakeInt((I32)j), a);
}

/* Quicksort partition */
static Size TablePartition(MLuaState *L, MLuaValue tbl, Size lo, Size hi,
                           Bool hasComp, MLuaValue compFunc) {
  MLuaValue pivot = MLuaTableGet(L, tbl, MakeInt((I32)hi));
  Size i = lo;
  Size j;

  for (j = lo; j < hi; j++) {
    MLuaValue val = MLuaTableGet(L, tbl, MakeInt((I32)j));
    if (TableSortCompare(L, val, pivot, hasComp, compFunc)) {
      TableSwap(L, tbl, i, j);
      i++;
    }
  }
  TableSwap(L, tbl, i, hi);
  return i;
}

/* Quicksort recursive implementation (iterative via stack to avoid deep
 * recursion) */
static void TableQuicksort(MLuaState *L, MLuaValue tbl, Size lo, Size hi,
                           Bool hasComp, MLuaValue compFunc) {
  /* Use an explicit stack to avoid deep recursion */
  Size stack[64];
  int top = -1;

  stack[++top] = lo;
  stack[++top] = hi;

  while (top >= 0) {
    Size high = stack[top--];
    Size low = stack[top--];

    if (low < high) {
      Size p = TablePartition(L, tbl, low, high, hasComp, compFunc);

      /* Push larger partition first (to minimize stack depth) */
      if (p > 1 && p - 1 - low > high - p - 1) {
        if (low < p - 1 && top < 62) {
          stack[++top] = low;
          stack[++top] = p - 1;
        }
        if (p + 1 < high && top < 62) {
          stack[++top] = p + 1;
          stack[++top] = high;
        }
      } else {
        if (p + 1 < high && top < 62) {
          stack[++top] = p + 1;
          stack[++top] = high;
        }
        if (p > 0 && low < p - 1 && top < 62) {
          stack[++top] = low;
          stack[++top] = p - 1;
        }
      }
    }
  }
}

static int TableSort(MLuaState *L) {
  MLuaValue tbl = MLuaGetStack(L, 1);
  Size len = MLuaTableLen(tbl);
  Bool hasComp = MLuaGetTop(L) >= 2;
  MLuaValue compFunc = hasComp ? MLuaGetStack(L, 2) : MLUA_NIL;

  if (len > 1) {
    TableQuicksort(L, tbl, 1, len, hasComp, compFunc);
  }

  return 0;
}

/* ========================================================================== */
/* table.forward (MicroLua extension)                                         */
/* ========================================================================== */

static int TableForwardF(MLuaState *L) {
  MLuaValue tbl = MLuaGetStack(L, 1);
  MLuaValue forward = MLuaGetStack(L, 2);
  MLuaTableSetForward(tbl, forward);
  return 0;
}

/* ========================================================================== */
/* table.maxn (Lua 5.1)                                                       */
/* ========================================================================== */

static int TableMaxn(MLuaState *L) {
  MLuaValue tbl = MLuaGetStack(L, 1);
  MLuaTableHeader *th;
  double maxn = 0.0;
  Size i;

  if (!IsPtr(tbl)) {
    MLuaPush(L, MakeInt(0));
    return 1;
  }
  th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tbl));

  for (i = 0; i < th->ArrayLen; i++) {
    if (!IsNil(th->Array[i]) && (double)(i + 1) > maxn) {
      maxn = (double)(i + 1);
    }
  }
  for (i = 0; i < th->NodeCapacity; i++) {
    if (!IsNil(th->Nodes[i].Key) && !IsNil(th->Nodes[i].Value)) {
      MLuaValue k = th->Nodes[i].Key;
      if (IsInt(k)) {
        if ((double)GetInt(k) > maxn) {
          maxn = (double)GetInt(k);
        }
      } else if (MLuaIsNumber(k)) {
        double kn = MLuaToNumber(k);
        if (kn > maxn) {
          maxn = kn;
        }
      }
    }
  }

  if (maxn == (double)(I32)maxn) {
    MLuaPush(L, MakeInt((I32)maxn));
  } else {
    MLuaPush(L, MLuaMakeNumber(L, maxn));
  }
  return 1;
}

/* ========================================================================== */
/* Library Registration                                                       */
/* ========================================================================== */

static const MLuaLibEntry TableLibEntries[] = {
    {"concat", TableConcat}, {"forward", TableForwardF},
    {"insert", TableInsert}, {"maxn", TableMaxn},
    {"pack", TablePack},     {"remove", TableRemove},
    {"sort", TableSort},     {"unpack", TableUnpack},
    {NULL, NULL}};

void MLuaOpenTable(MLuaState *L) {
  MLuaValue lib = MLuaNewLib(L, "table");
  MLuaRegisterLib(L, lib, TableLibEntries);

  /* Lua 5.1 global alias */
  MLuaRegisterGlobal(L, "unpack", TableUnpack);
}
