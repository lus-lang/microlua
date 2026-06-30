/*
 * MicroLua - MLuaTableLib.c
 * Table library implementation
 */

#include "MLuaTableLib.h"
#include "../MLuaCore.h"
#include "../MLuaGC.h"
#include "../MLuaString.h"
#include "../MLuaTable.h"
#include "../MLuaVM.h"

/* ========================================================================== */
/* table.concat                                                               */
/* ========================================================================== */

static int TableConcat(MLuaState *L) {
  MLuaValue tbl = MLuaGetStack(L, 1);
  MLuaValue sepval = MLUA_NIL;
  Size seplen = 0;
  IPtr startI = 1;
  IPtr j;
  Size k;
  Size totalLen = 0;
  Size bufpos = 0;
  char *buf;
  int top = MLuaGetTop(L);

  if (top >= 2) {
    sepval = MLuaGetStack(L, 2);
    seplen = MLuaStringLen(sepval);
  }
  if (top >= 3) {
    startI = GetInt(MLuaGetStack(L, 3));
  }
  if (top >= 4) {
    j = GetInt(MLuaGetStack(L, 4));
  } else {
    j = (IPtr)MLuaTableLen(tbl);
  }

  if (startI < 1)
    startI = 1;
  if (j < startI) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  /*
   * First pass: compute the exact length (element bytes plus one separator
   * between each pair). A fixed stack buffer used to live here and silently
   * truncated at 4 KB; size a heap buffer exactly instead.
   */
  for (k = (Size)startI; k <= (Size)j; k++) {
    totalLen += MLuaStringLen(MLuaTableGet(L, tbl, MakeInt((I32)k)));
  }
  if (j >= startI) {
    totalLen += (Size)(j - startI) * seplen; /* separators BETWEEN elements */
  }

  if (totalLen == 0) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  buf = (char *)MLuaAlloc(L, totalLen);
  if (!buf) {
    L->ErrorMsg = "out of memory";
    return -1;
  }

  /*
   * Second pass: fill. No allocation happens between MLuaAlloc above and the
   * MLuaStringNew below, so 'buf' cannot move. Each operand's bytes are
   * re-fetched immediately before they are copied, so a short string's
   * rotating data buffer cannot alias across iterations (the bug the old
   * once-captured 'sep' pointer was exposed to).
   */
  for (k = (Size)startI; k <= (Size)j; k++) {
    MLuaValue v;
    const char *s;
    Size slen;
    Size t;

    if (k > (Size)startI && seplen > 0) {
      const char *sp = MLuaStringData(sepval);
      for (t = 0; t < seplen; t++) {
        buf[bufpos++] = sp[t];
      }
    }

    v = MLuaTableGet(L, tbl, MakeInt((I32)k));
    slen = MLuaStringLen(v);
    s = MLuaStringData(v);
    for (t = 0; t < slen; t++) {
      buf[bufpos++] = s[t];
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
    MLuaValue posv = MLuaGetStack(L, 2);
    MLuaValue value = MLuaGetStack(L, 3);
    Size pos;
    Size i;

    if (!IsInt(posv)) {
      L->ErrorMsg = "bad argument #2 to 'insert' (number expected)";
      return -1;
    }
    /* Position must land inside the sequence or exactly one past its end;
       anything else would create a hole. (Also guards the loop below from
       an unsigned underflow when pos < 1.) */
    if (GetInt(posv) < 1 || (Size)GetInt(posv) > len + 1) {
      L->ErrorMsg = "bad argument #2 to 'insert' (position out of bounds)";
      return -1;
    }
    pos = (Size)GetInt(posv);

    /* Shift elements up (top-down, so the array never transiently holds a
       hole) */
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
  IPtr i = 1;
  IPtr j;
  Size count = 0;

  if (top >= 2) {
    i = GetInt(MLuaGetStack(L, 2));
  }
  if (top >= 3) {
    j = GetInt(MLuaGetStack(L, 3));
  } else {
    j = (IPtr)MLuaTableLen(tbl);
  }

  if (i < 1)
    i = 1;
  if (j < i) {
    return 0;
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
typedef struct {
  MLuaGCRef Table;
  MLuaGCRef Comp;
  Bool HasComp;
  Bool Error;
} TableSortContext;

static Bool TableSortCompare(MLuaState *L, TableSortContext *ctx, MLuaValue a,
                             MLuaValue b) {
  if (ctx->HasComp && !IsNil(ctx->Comp.Value)) {
    MLuaGCRef aref;
    MLuaGCRef bref;
    MLuaStatus status;
    MLuaValue result;

    MLuaPushGCRef(L, &aref, a);
    MLuaPushGCRef(L, &bref, b);

    /* Use custom comparator */
    MLuaPush(L, ctx->Comp.Value);
    MLuaPush(L, aref.Value);
    MLuaPush(L, bref.Value);
    status = MLuaCall(L, 2, 1);
    if (status != MLUA_OK) {
      ctx->Error = TRUE;
      MLuaPopGCRef(L, &bref);
      MLuaPopGCRef(L, &aref);
      return FALSE;
    }
    result = MLuaPop(L);
    MLuaPopGCRef(L, &bref);
    MLuaPopGCRef(L, &aref);
    return IsTruthy(result);
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
static Size TablePartition(MLuaState *L, TableSortContext *ctx, Size lo,
                           Size hi) {
  MLuaGCRef pivotRef;
  MLuaValue pivot = MLuaTableGet(L, ctx->Table.Value, MakeInt((I32)hi));
  Size i = lo;
  Size j;

  MLuaPushGCRef(L, &pivotRef, pivot);

  for (j = lo; j < hi; j++) {
    MLuaValue val = MLuaTableGet(L, ctx->Table.Value, MakeInt((I32)j));
    if (TableSortCompare(L, ctx, val, pivotRef.Value)) {
      TableSwap(L, ctx->Table.Value, i, j);
      i++;
    }
    if (ctx->Error) {
      MLuaPopGCRef(L, &pivotRef);
      return i;
    }
  }
  TableSwap(L, ctx->Table.Value, i, hi);
  MLuaPopGCRef(L, &pivotRef);
  return i;
}

/* Quicksort recursive implementation (iterative via stack to avoid deep
 * recursion) */
static void TableQuicksort(MLuaState *L, TableSortContext *ctx, Size lo,
                           Size hi) {
  /* Use an explicit stack to avoid deep recursion */
  Size stack[64];
  int top = -1;

  stack[++top] = lo;
  stack[++top] = hi;

  while (top >= 0 && !ctx->Error) {
    Size high = stack[top--];
    Size low = stack[top--];

    if (low < high) {
      Size p = TablePartition(L, ctx, low, high);

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
  TableSortContext ctx;

  ctx.HasComp = hasComp;
  ctx.Error = FALSE;
  MLuaPushGCRef(L, &ctx.Table, tbl);
  MLuaPushGCRef(L, &ctx.Comp, compFunc);
  if (len > 1) {
    TableQuicksort(L, &ctx, 1, len);
  }
  MLuaPopGCRef(L, &ctx.Comp);
  MLuaPopGCRef(L, &ctx.Table);

  if (ctx.Error) {
    return -1;
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
