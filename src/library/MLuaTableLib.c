/*
 * MicroLua - MLuaTableLib.c
 * Table library implementation
 */

#include "MLuaTableLib.h"
#include "../MLuaConvert.h"
#include "../MLuaCore.h"
#include "../MLuaGC.h"
#include "../MLuaString.h"
#include "../MLuaTable.h"
#include "../MLuaVM.h"

/* ========================================================================== */
/* table.concat                                                               */
/* ========================================================================== */

/* Concat joins strings AND numbers (reference Lua semantics; numbers used
 * to contribute nothing). Rendering is a pure function of the value, so the
 * sizing pass and the fill pass always agree. */
#define CONCAT_NUM_BUF 40
static Size ConcatElemToStr(MLuaState *L, MLuaValue v, char *numBuf) {
  if (IsAnyString(v)) {
    return 0; /* caller copies string bytes directly */
  }
  if (MLuaIsNumber(v)) {
    return MLuaValueToStr(L, v, numBuf, CONCAT_NUM_BUF);
  }
  return 0; /* other types contribute nothing (pre-existing tolerance) */
}

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
    startI = MLuaGetIntVal(MLuaGetStack(L, 3));
  }
  if (top >= 4) {
    j = MLuaGetIntVal(MLuaGetStack(L, 4));
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
    MLuaValue v = MLuaTableGet(L, tbl, MakeInt((I32)k));
    if (IsAnyString(v)) {
      totalLen += MLuaStringLen(v);
    } else {
      char numBuf[CONCAT_NUM_BUF];
      totalLen += ConcatElemToStr(L, v, numBuf);
    }
  }
  if (j >= startI) {
    totalLen += (Size)(j - startI) * seplen; /* separators BETWEEN elements */
  }

  if (totalLen == 0) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  if (L->GCPending || totalLen >= 1024) {
    L->GCPending = FALSE;
    MLuaGCCollect(L);
    tbl = MLuaGetStack(L, 1);
    if (top >= 2) {
      sepval = MLuaGetStack(L, 2);
    }
  }

  if (seplen == 0 && IsTable(tbl) && (Size)(j - startI + 1) <= 0x7FFFFFFFU) {
    MLuaTableHeader *th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tbl));
    if ((Size)j <= th->ArrayLen) {
      MLuaValue *array = MLuaTableArrayData(th);
      Bool allStrings = TRUE;
      Size count = (Size)(j - startI + 1);
      for (k = (Size)startI; k <= (Size)j; k++) {
        if (!IsAnyString(array[k - 1])) {
          allStrings = FALSE;
          break;
        }
      }
      if (allStrings) {
        MLuaPush(L, MLuaStringConcatMany(L, &array[startI - 1], (int)count));
        return 1;
      }
    }
  }

  buf = (char *)MLuaAlloc(L, totalLen);
  if (!buf) {
    L->ErrorMsg = "out of memory";
    return -1;
  }

  /*
   * Second pass: fill. Allocations below (typed-array reads materialize
   * number values) never collect, so 'buf' cannot move. Each operand's
   * bytes are re-fetched immediately before they are copied, so a short
   * string's rotating data buffer cannot alias across iterations (the bug
   * the old once-captured 'sep' pointer was exposed to).
   */
  L->ErrorMsg = NULL; /* distinguish OOM nils from legit interior nils */
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
    if (IsNil(v) && L->ErrorMsg) {
      return -1; /* typed-element materialization failed mid-concat */
    }
    if (IsAnyString(v)) {
      slen = MLuaStringLen(v);
      s = MLuaStringData(v);
      for (t = 0; t < slen; t++) {
        buf[bufpos++] = s[t];
      }
    } else {
      char numBuf[CONCAT_NUM_BUF];
      slen = ConcatElemToStr(L, v, numBuf);
      for (t = 0; t < slen; t++) {
        buf[bufpos++] = numBuf[t];
      }
    }
  }

  {
    MLuaValue res = MLuaStringNew(L, buf, bufpos);
    if (IsNil(res)) {
      return -1; /* ErrorMsg set by the failed creation */
    }
    MLuaPush(L, res);
  }
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
    if (MLuaGetIntVal(posv) < 1 || (Size)MLuaGetIntVal(posv) > len + 1) {
      L->ErrorMsg = "bad argument #2 to 'insert' (position out of bounds)";
      return -1;
    }
    pos = (Size)MLuaGetIntVal(posv);

    /* Raw array-part shift (one grow + MemMove) instead of a boxed-key
       get/set round trip per shifted element. */
    if (!MLuaTableArrayInsert(L, tbl, pos, value)) {
      if (L->ErrorMsg) {
        return -1;
      }
      L->ErrorMsg = "bad argument #1 to 'insert' (table expected)";
      return -1;
    }
    UNUSED(i);
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
    i = MLuaGetIntVal(MLuaGetStack(L, 2));
  }
  if (top >= 3) {
    j = MLuaGetIntVal(MLuaGetStack(L, 3));
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
    pos = (Size)MLuaGetIntVal(MLuaGetStack(L, 2));
  } else {
    pos = len;
  }

  /* Raw array-part shift; out-of-range positions keep the old tolerant
     behavior (remove nothing, return the read value). */
  if (!MLuaTableArrayRemove(L, tbl, pos, &removed)) {
    removed = MLuaTableGet(L, tbl, MakeInt((I32)pos));
  }
  UNUSED(i);

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
  /* Direct pointer into the table's generic array part, set ONLY for
   * default-comparator sorts over a fully-array-resident sequence: the
   * default compare (MLuaCompare) never allocates, so nothing can move
   * while the sort runs and every element access skips the boxed-key
   * generic table path. Custom comparators call back into Lua (GC can
   * move the buffer), so they always take the table-access route. */
  MLuaValue *Raw;
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

/* Element accessors: raw array slots when ctx->Raw is set, generic table
 * access otherwise. */
static MLuaValue SortGet(MLuaState *L, TableSortContext *ctx, Size i) {
  if (ctx->Raw) {
    return ctx->Raw[i - 1];
  }
  return MLuaTableGet(L, ctx->Table.Value, MakeInt((I32)i));
}

static void SortSwap(MLuaState *L, TableSortContext *ctx, Size i, Size j) {
  if (ctx->Raw) {
    MLuaValue tmp = ctx->Raw[i - 1];
    ctx->Raw[i - 1] = ctx->Raw[j - 1];
    ctx->Raw[j - 1] = tmp;
    return;
  }
  {
    MLuaValue a = MLuaTableGet(L, ctx->Table.Value, MakeInt((I32)i));
    MLuaValue b = MLuaTableGet(L, ctx->Table.Value, MakeInt((I32)j));
    MLuaTableSet(L, ctx->Table.Value, MakeInt((I32)i), b);
    MLuaTableSet(L, ctx->Table.Value, MakeInt((I32)j), a);
  }
}

/* Hoare partition around a median-of-3 pivot. Returns j such that
 * [lo..j] and [j+1..hi] are the two sides; with lo < hi, j is always in
 * [lo, hi-1], so both sides are non-empty ranges and the caller always
 * makes progress. Median-of-3 sinks sorted/reverse inputs' worst case, and
 * Hoare splits runs of equal elements evenly (Lomuto degrades to O(n^2) on
 * all-equal input). The i<hi / j>lo scan guards keep an inconsistent user
 * comparator memory-safe: the result is then some permutation, never an
 * out-of-range access. On comparator error, returns with ctx->Error set. */
static Size TablePartition(MLuaState *L, TableSortContext *ctx, Size lo,
                           Size hi) {
  MLuaGCRef pivotRef;
  Size mid = lo + (hi - lo) / 2;
  Size i;
  Size j;
  MLuaValue a;
  MLuaValue b;

  /* Order t[lo] <= t[mid] <= t[hi]; the ends double as scan sentinels. */
  a = SortGet(L, ctx, mid);
  b = SortGet(L, ctx, lo);
  if (TableSortCompare(L, ctx, a, b)) {
    SortSwap(L, ctx, mid, lo);
  }
  a = SortGet(L, ctx, hi);
  b = SortGet(L, ctx, mid);
  if (!ctx->Error && TableSortCompare(L, ctx, a, b)) {
    SortSwap(L, ctx, hi, mid);
    a = SortGet(L, ctx, mid);
    b = SortGet(L, ctx, lo);
    if (!ctx->Error && TableSortCompare(L, ctx, a, b)) {
      SortSwap(L, ctx, mid, lo);
    }
  }
  if (ctx->Error) {
    return lo;
  }

  MLuaPushGCRef(L, &pivotRef, SortGet(L, ctx, mid));

  i = lo;
  j = hi;
  for (;;) {
    for (;;) { /* scan right while t[i] < pivot */
      a = SortGet(L, ctx, i);
      if (ctx->Error || i >= hi ||
          !TableSortCompare(L, ctx, a, pivotRef.Value)) {
        break;
      }
      i++;
    }
    for (;;) { /* scan left while pivot < t[j] */
      b = SortGet(L, ctx, j);
      if (ctx->Error || j <= lo ||
          !TableSortCompare(L, ctx, pivotRef.Value, b)) {
        break;
      }
      j--;
    }
    if (ctx->Error || i >= j) {
      break;
    }
    SortSwap(L, ctx, i, j);
    i++;
    j--;
  }

  MLuaPopGCRef(L, &pivotRef);
  return (j < lo) ? lo : ((j >= hi) ? hi - 1 : j);
}

/* Iterative quicksort. Each partition pushes its LARGER side and loops on
 * the smaller, so the pending segment halves with every stacked range:
 * the stack never holds more than log2(length) ranges, and 32 ranges cover
 * any 32-bit length. (The previous scheme pushed both sides and silently
 * DROPPED ranges when its fixed stack filled, returning unsorted data.) */
static void TableQuicksort(MLuaState *L, TableSortContext *ctx, Size lo,
                           Size hi) {
  Size stack[64];
  int top = 0;

  for (;;) {
    while (lo < hi && !ctx->Error) {
      Size p = TablePartition(L, ctx, lo, hi);
      /* Sides: [lo..p] and [p+1..hi], both non-empty. */
      if (p - lo < hi - p) {
        stack[top++] = p + 1; /* push larger (right), iterate left */
        stack[top++] = hi;
        hi = p;
      } else {
        stack[top++] = lo; /* push larger (left), iterate right */
        stack[top++] = p;
        lo = p + 1;
      }
    }
    if (ctx->Error || top == 0) {
      return;
    }
    hi = stack[--top];
    lo = stack[--top];
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
  ctx.Raw = NULL;
  if (!hasComp && IsTable(tbl)) {
    MLuaTableHeader *th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tbl));
#if MLUA_TABLE_NUM_ARRAYS
    if (MLuaTableArrayKind(th) != MLUA_TABLE_ARRAY_NUM)
#endif
    {
      if ((Size)th->ArrayLen == len) {
        ctx.Raw = MLuaTableArrayData(th);
      }
    }
  }
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
  MLuaValue *array;
  MLuaTableNode *nodes;
  double maxn = 0.0;
  Size i;

  if (!IsPtr(tbl)) {
    MLuaPush(L, MakeInt(0));
    return 1;
  }
  th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tbl));
  array = MLuaTableArrayData(th);
  nodes = MLuaTableNodeData(th);

#if MLUA_TABLE_NUM_ARRAYS
  if (MLuaTableArrayKind(th) == MLUA_TABLE_ARRAY_NUM) {
    /* Typed arrays are dense (no interior nils): maxn is the length. The
     * generic loop below sees ArrayLen == 0 and is a no-op. */
    maxn = (double)MLuaTableLen(tbl);
  }
#endif
  for (i = 0; i < th->ArrayLen; i++) {
    if (!IsNil(array[i]) && (double)(i + 1) > maxn) {
      maxn = (double)(i + 1);
    }
  }
  for (i = 0; i < th->NodeCapacity; i++) {
    if (!IsNil(nodes[i].Key) && !IsNil(nodes[i].Value)) {
      MLuaValue k = nodes[i].Key;
      if (IsInt(k)) {
        if ((double)MLuaGetIntVal(k) > maxn) {
          maxn = (double)MLuaGetIntVal(k);
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
    MLuaPush(L, MLuaMakeInt(L, (I32)maxn));
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
