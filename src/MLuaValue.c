/*
 * MicroLua - MLuaValue.c
 * Value type utilities and helper functions
 */

#include "MLuaValue.h"

#include "MLuaAlloc.h"

/* ========================================================================== */
/* Value Type Names (for debugging/error messages)                            */
/* ========================================================================== */

static const char *const ObjTypeNames[] = {
    "invalid",  /* 0 - unused */
    "string",   /* OBJTYPE_STRING */
    "table",    /* OBJTYPE_TABLE */
    "function", /* OBJTYPE_FUNCTION */
    "proto",    /* OBJTYPE_PROTO */
    "userdata", /* OBJTYPE_USERDATA */
    "upvalue",  /* OBJTYPE_UPVALUE */
    "thread"    /* OBJTYPE_THREAD */
};

/*
 * Get human-readable type name for a value.
 * Returns standard Lua type names: nil, boolean, number, string, table,
 * function, thread, userdata
 */
const char *MLuaTypeName(MLuaValue v) {
  /* Check nil first */
  if (IsNil(v)) {
    return "nil";
  }

  /* Check booleans */
  if (IsBool(v)) {
    return "boolean";
  }

  /* Check numbers (integers and floats) */
  if (IsInt(v)) {
    return "number";
  }
#if MLUA_PTR_SIZE == 8
  if (IsDouble(v)) {
    return "number";
  }
#endif

  /* Check strings (both short and heap-allocated) */
  if (IsShortStr(v)) {
    return "string";
  }

  /* Check light functions */
  if (IsLightFunc(v)) {
    return "function";
  }

  /* Check heap objects */
  if (IsPtr(v)) {
    void *ptr = GetPtr(v);
    if (ptr == NULL) {
      return "nil";
    }
    MLuaGCHeader *h = (MLuaGCHeader *)ptr;
    U8 objType = MLUA_OBJTYPE(h);
#if MLUA_PTR_SIZE != 8
    /* Heap numbers (floats) and boxed integers exist only on the 32-bit path
     * and sit past OBJTYPE_THREAD in ObjTypeNames; classify them as numbers. */
    if (objType == OBJTYPE_NUMBER || objType == OBJTYPE_INT) {
      return "number";
    }
#endif
    if (objType > 0 && objType <= OBJTYPE_THREAD) {
      return ObjTypeNames[objType];
    }
    return "userdata"; /* Fallback for unknown heap objects */
  }

  return "unknown";
}

/* ========================================================================== */
/* Integer Overflow Checking                                                  */
/* ========================================================================== */

/*
 * Check if a 32-bit signed integer fits inline (without a heap box). This is
 * the whole I32 range on the NaN-boxing path and the 29-bit range on the 32-bit
 * tagging path; values outside it must be boxed by MLuaMakeIntSafe.
 */
Bool MLuaIntFits(I32 value) {
  return (value >= MLUA_INLINE_INT_MIN && value <= MLUA_INLINE_INT_MAX);
}

/*
 * Create an integer value. If it fits inline it is stored inline; otherwise
 * (only possible on the 32-bit tagging path) it spills to a heap OBJTYPE_INT
 * box so full 32-bit integer semantics are preserved. Mirrors the heap-number
 * spill in MLuaMakeNumber and obeys the same allocation/safepoint discipline.
 */
MLuaValue MLuaMakeIntSafe(MLuaState *L, I32 value) {
  if (MLuaIntFits(value)) {
    return MakeInt(value);
  }
#if MLUA_PTR_SIZE == 8
  /* Unreachable on the NaN-boxing path: every I32 fits inline. */
  UNUSED(L);
  return MakeInt(value);
#else
  {
    MLuaGCHeader *gch = MLuaAllocObject(L, OBJTYPE_INT, sizeof(I32));
    if (!gch) {
      return MakeInt(0); /* Allocation failed: match the heap-number fallback. */
    }
    MLUA_INTBOX(gch)->Value = value;
    return MakePtr(gch);
  }
#endif
}

/* ========================================================================== */
/* Short String Utilities                                                     */
/* ========================================================================== */

/*
 * Get the length of a short string.
 * 64-bit values store the length explicitly so embedded NUL bytes work.
 * 32-bit values retain the legacy NUL-padded 3-byte representation.
 */
Size MLuaShortStrLen(MLuaValue v) {
  if (!IsShortStr(v))
    return 0;

#if MLUA_PTR_SIZE == 8
  return GetShortStrEncodedLen(v);
#else
  Size len = 0;
  if (GetShortStrChar0(v))
    len = 1;
  if (GetShortStrChar1(v))
    len = 2;
  if (GetShortStrChar2(v))
    len = 3;
  return len;
#endif
}

/*
 * Create a short string from bytes.
 * Returns MLUA_NIL if the string is too long.
 */
MLuaValue MLuaMakeShortStr(const char *s, Size len) {
  if (len > MLUA_SHORTSTR_MAX) {
    return MLUA_NIL; /* Too long for short string */
  }

  char c0 = (len > 0) ? s[0] : 0;
  char c1 = (len > 1) ? s[1] : 0;
  char c2 = (len > 2) ? s[2] : 0;
#if MLUA_PTR_SIZE == 8
  char c3 = (len > 3) ? s[3] : 0;
  char c4 = (len > 4) ? s[4] : 0;
  return MakeShortStr5(c0, c1, c2, c3, c4, len);
#else

  return MakeShortStr(c0, c1, c2);
#endif
}

/* ========================================================================== */
/* Value Comparison                                                           */
/* ========================================================================== */

/*
 * Raw equality check (same bits = equal).
 * For heap objects, this compares pointers, not contents — except boxed
 * integers, which compare by value so that two distinct boxes holding the same
 * I32 are equal (table keys and constant dedup depend on this). Canonicalization
 * guarantees an inline integer and a boxed integer never share a value, so the
 * only bits-differ-but-equal case is boxed-vs-boxed.
 */
Bool MLuaRawEqual(MLuaValue a, MLuaValue b) {
#if MLUA_PTR_SIZE == 8
  return a == b;
#else
  if (a == b) {
    return TRUE;
  }
  if (IsBoxedInt(a) && IsBoxedInt(b)) {
    return MLuaGetIntVal(a) == MLuaGetIntVal(b);
  }
  return FALSE;
#endif
}

/*
 * Check if two integer values are equal.
 * Returns FALSE if either is not an integer.
 */
Bool MLuaIntEqual(MLuaValue a, MLuaValue b) {
  if (!IsInt(a) || !IsInt(b))
    return FALSE;
#if MLUA_PTR_SIZE == 8
  return a == b;
#else
  return MLuaGetIntVal(a) == MLuaGetIntVal(b);
#endif
}

/* ========================================================================== */
/* Heap Numbers                                                               */
/* ========================================================================== */

Bool MLuaIsNumber(MLuaValue v) {
  if (IsInt(v)) {
    return TRUE;
  }
#if MLUA_PTR_SIZE == 8
  /* On 64-bit: NaN-boxed doubles */
  if (IsDouble(v)) {
    return TRUE;
  }
#else
  /* On 32-bit: heap-allocated numbers */
  if (IsPtr(v)) {
    MLuaGCHeader *h = (MLuaGCHeader *)GetPtr(v);
    return MLUA_OBJTYPE(h) == OBJTYPE_NUMBER;
  }
#endif
  return FALSE;
}

double MLuaToNumber(MLuaValue v) {
  if (IsInt(v)) {
    return (double)MLuaGetIntVal(v);
  }
#if MLUA_PTR_SIZE == 8
  /* On 64-bit: NaN-boxed doubles */
  if (IsDouble(v)) {
    return GetDouble(v);
  }
#else
  /* On 32-bit: heap-allocated numbers */
  if (IsPtr(v)) {
    MLuaGCHeader *h = (MLuaGCHeader *)GetPtr(v);
    if (MLUA_OBJTYPE(h) == OBJTYPE_NUMBER) {
      return (double)MLUA_NUMBER(h)->Value;
    }
  }
#endif
  return 0.0;
}

MLuaValue MLuaMakeNumber(MLuaState *L, double n) {
#if MLUA_PTR_SIZE == 8
  UNUSED(L);
#endif

  /* Try to represent as an integer first (inline, or boxed on the 32-bit path
   * when the value is integral but outside the inline range). */
  if (n == (double)(I32)n) {
    I32 i = (I32)n;
    if (i >= MLUA_INT_MIN && i <= MLUA_INT_MAX) {
      return MLuaMakeInt(L, i);
    }
  }

#if MLUA_PTR_SIZE == 8
  /*
   * On 64-bit: Use NaN-boxing.
   * Doubles are stored directly with full 64-bit precision.
   * No heap allocation for numbers!
   */
  return MakeDouble(n);
#else
  /*
   * On 32-bit: We cannot fit 64-bit doubles inline.
   * Must heap-allocate. This is unavoidable with 32-bit values.
   */
  {
    MLuaGCHeader *gch = MLuaAllocObject(L, OBJTYPE_NUMBER, sizeof(MLUA_FLOAT));
    MLuaNumber *num;
    if (!gch) {
      return MakeInt(0); /* Fallback */
    }
    num = MLUA_NUMBER(gch);
    num->Value = (MLUA_FLOAT)n;
    return MakePtr(num);
  }
#endif
}

MLuaValue MLuaMakeFloat(MLuaState *L, double n) {
#if MLUA_PTR_SIZE == 8
  UNUSED(L);
  return MakeDouble(n);
#else
  MLuaGCHeader *gch = MLuaAllocObject(L, OBJTYPE_NUMBER, sizeof(MLUA_FLOAT));
  MLuaNumber *num;
  if (!gch) {
    return MakeInt(0);
  }
  num = MLUA_NUMBER(gch);
  num->Value = (MLUA_FLOAT)n;
  return MakePtr(num);
#endif
}
