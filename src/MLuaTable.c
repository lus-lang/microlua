/*
 * MicroLua - MLuaTable.c
 * Bifurcated table implementation
 */

#include "MLuaTable.h"
#include "MLuaGC.h"
#include "MLuaString.h"

/* ========================================================================== */
/* Hash Utilities                                                             */
/* ========================================================================== */

static U32 HashValue(MLuaValue key) {
  if (IsInt(key)) {
    /* Simple integer hash (by value, so a boxed int hashes like its I32) */
    I32 i = MLuaGetIntVal(key);
    return (U32)((i * 2654435761U) & 0xFFFFFFFF);
  }

  if (IsShortStr(key)) {
    /* Hash the short string bytes */
    char buf[MLUA_SHORTSTR_MAX];
    Size len = MLuaShortStrLen(key);
    const char *s = MLuaStringData(key);
    Size i;
    for (i = 0; i < len; i++) {
      buf[i] = s[i];
    }
    return MLuaStringHash(buf, len);
  }

  if (IsString(key)) {
    /* Use precomputed hash from string header */
    MLuaGCHeader *gch = (MLuaGCHeader *)GetPtr(key);
    MLuaStringHeader *sh = MLUA_STRHEADER(gch);
    return sh->Hash;
  }

  if (IsPtr(key)) {
    /* Hash pointer value */
    return (U32)((key * 2654435761U) & 0xFFFFFFFF);
  }

  /* For other types, use raw value */
  return (U32)(key & 0xFFFFFFFF);
}

/* ========================================================================== */
/* Table Creation                                                             */
/* ========================================================================== */

MLuaValue MLuaTableNew(MLuaState *L) { return MLuaTableNewSized(L, 0, 0); }

MLuaValue MLuaTableNewSized(MLuaState *L, Size arrayHint, Size hashHint) {
  MLuaGCHeader *gch;
  MLuaTableHeader *th;
  Size i;

  /* Allocate table object */
  gch = MLuaAllocObject(L, OBJTYPE_TABLE, sizeof(MLuaTableHeader));
  if (!gch) {
    return MLUA_NIL;
  }

  th = MLUA_TABLEHEADER(gch);
  th->ArraySize = 0;
  th->ArrayLen = 0;
  th->NodeCapacity = 0;
  th->Forward = MLUA_NIL;
  th->NodeState = 0;
  for (i = 0; i < MLUA_TABLE_INLINE_ARRAY_CAP; i++) {
    th->InlineArray[i] = MLUA_NIL;
  }
  for (i = 0; i < MLUA_TABLE_INLINE_HASH_CAP; i++) {
    th->InlineNodes[i].Key = MLUA_NIL;
    th->InlineNodes[i].Value = MLUA_NIL;
  }

  /* Pre-allocate array part if hint given */
  if (arrayHint > 0) {
    if (arrayHint <= MLUA_TABLE_INLINE_ARRAY_CAP) {
      MLuaTableSetArrayInline(th, TRUE);
      th->ArraySize = (U32)MLUA_TABLE_INLINE_ARRAY_CAP;
      for (i = 0; i < MLUA_TABLE_INLINE_ARRAY_CAP; i++) {
        th->InlineArray[i] = MLUA_NIL;
      }
    } else {
      if (arrayHint > (Size)0xFFFFFFFFU) {
        return MakePtr(gch);
      }
      if (arrayHint > (Size)-1 / sizeof(MLuaValue)) {
        return MakePtr(gch);
      }
      Size arrayBytes = arrayHint * sizeof(MLuaValue);
      MLuaValue *array = (MLuaValue *)MLuaAlloc(L, arrayBytes);
      if (array) {
        MLuaTableSetArrayData(th, array);
        th->ArraySize = (U32)arrayHint;
        for (i = 0; i < arrayHint; i++) {
          array[i] = MLUA_NIL;
        }
      }
    }
  }

  /* Pre-allocate hash part if hint given */
  if (hashHint > 0) {
    if (hashHint <= MLUA_TABLE_INLINE_HASH_CAP) {
      MLuaTableSetHashInline(th, TRUE);
      th->NodeCapacity = (U32)MLUA_TABLE_INLINE_HASH_CAP;
      for (i = 0; i < MLUA_TABLE_INLINE_HASH_CAP; i++) {
        th->InlineNodes[i].Key = MLUA_NIL;
        th->InlineNodes[i].Value = MLUA_NIL;
      }
    } else {
      if (hashHint > (Size)0xFFFFFFFFU) {
        return MakePtr(gch);
      }
      if (hashHint > (Size)-1 / sizeof(MLuaTableNode)) {
        return MakePtr(gch);
      }
      Size hashBytes = hashHint * sizeof(MLuaTableNode);
      MLuaTableNode *nodes = (MLuaTableNode *)MLuaAlloc(L, hashBytes);
      if (nodes) {
        MLuaTableSetNodeData(th, nodes);
        th->NodeCapacity = (U32)hashHint;
        for (i = 0; i < hashHint; i++) {
          nodes[i].Key = MLUA_NIL;
          nodes[i].Value = MLUA_NIL;
        }
      }
    }
  }

  return MakePtr(gch);
}

/* ========================================================================== */
/* Array Part Operations                                                      */
/* ========================================================================== */

static Bool IsPositiveInt(MLuaValue key, Size *index) {
  I32 i;

  /* Use the full integer value (inline or boxed) so a large positive key is
   * treated identically on every target: MicroLua rejects array holes by
   * design, so t[huge] is a runtime error on both the tagging and NaN-boxing
   * paths, never silently a hash-part key on one and an error on the other. */
  if (!IsInt(key)) {
    return FALSE;
  }

  i = MLuaGetIntVal(key);
  if (i > 0) {
    *index = (Size)i;
    return TRUE;
  }

  return FALSE;
}

static Bool ArrayGrow(MLuaState *L, MLuaTableHeader *th, Size newSize) {
  Size oldSize = th->ArraySize;
  MLuaValue *oldArray = MLuaTableArrayData(th);
  Size newBytes;
  MLuaValue *newArray;
  Size i;

  if (newSize > (Size)0xFFFFFFFFU) {
    return FALSE;
  }
  if (newSize > (Size)-1 / sizeof(MLuaValue)) {
    return FALSE;
  }
  newBytes = newSize * sizeof(MLuaValue);

  newArray = (MLuaValue *)MLuaAlloc(L, newBytes);
  if (!newArray) {
    return FALSE;
  }

  /* Copy existing elements */
  for (i = 0; i < oldSize; i++) {
    newArray[i] = oldArray[i];
  }

  /* Initialize new elements to nil */
  for (i = oldSize; i < newSize; i++) {
    newArray[i] = MLUA_NIL;
  }

  MLuaTableSetArrayData(th, newArray);
  th->ArraySize = (U32)newSize;
  MLuaTableSetArrayInline(th, FALSE);

  return TRUE;
}

#if MLUA_TABLE_NUM_ARRAYS
/* ========================================================================== */
/* Typed (raw MLUA_FLOAT) Array Part                                          */
/* ========================================================================== */
/*
 * A NUM-kind array stores raw floats in an external buffer; the table's
 * ArrayLen stays 0 so every generic array fast path (which gates on
 * `index <= ArrayLen`) falls through to the kind-aware routes here without
 * any change to its own code. The real length lives in the buffer.
 */

typedef struct {
  U32 Len;          /* element count (the # of the array part) */
  MLUA_FLOAT Data[]; /* raw elements */
} MLuaTableNumArray;

static MLuaTableNumArray *TypedArray(MLuaTableHeader *th) {
  return (MLuaTableNumArray *)MLuaTableArrayData(th);
}

static Size TypedLen(MLuaTableHeader *th) {
  return (Size)TypedArray(th)->Len;
}

/* Heap float box (never an int): the only value a NUM array stores as-is */
static Bool IsHeapNumber(MLuaValue v) {
  return IsPtr(v) &&
         MLUA_OBJTYPE((MLuaGCHeader *)GetPtr(v)) == OBJTYPE_NUMBER;
}

static MLUA_FLOAT HeapNumberValue(MLuaValue v) {
  return MLUA_NUMBER((MLuaGCHeader *)GetPtr(v))->Value;
}

/* Materialize element `index` (1-based, must be in range) as a value.
 * Canonicalizing: integral floats come back as plain ints (allocation-free);
 * others box. On OOM returns nil with L->ErrorMsg set. */
static MLuaValue TypedArrayGet(MLuaState *L, MLuaTableHeader *th,
                               Size index) {
  return MLuaMakeNumber(L, (double)TypedArray(th)->Data[index - 1]);
}

static Bool TypedArrayGrow(MLuaState *L, MLuaTableHeader *th, Size newSize) {
  MLuaTableNumArray *oldBuf = TypedArray(th);
  MLuaTableNumArray *newBuf;
  Size i;

  if (newSize > (Size)0xFFFFFFFFU ||
      newSize > ((Size)-1 - sizeof(MLuaTableNumArray)) / sizeof(MLUA_FLOAT)) {
    return FALSE;
  }
  newBuf = (MLuaTableNumArray *)MLuaAlloc(
      L, sizeof(MLuaTableNumArray) + newSize * sizeof(MLUA_FLOAT));
  if (!newBuf) {
    return FALSE;
  }
  newBuf->Len = oldBuf->Len;
  for (i = 0; i < (Size)oldBuf->Len; i++) {
    newBuf->Data[i] = oldBuf->Data[i];
  }
  MLuaTableSetArrayData(th, (MLuaValue *)newBuf);
  th->ArraySize = (U32)newSize;
  return TRUE;
}

/* One-way in-place demotion NUM -> LOCKED generic slots. Boxes every element
 * first, installs atomically after; on OOM the table is untouched. */
static Bool TypedArrayDemote(MLuaState *L, MLuaTableHeader *th) {
  MLuaTableNumArray *buf = TypedArray(th);
  Size len = (Size)buf->Len;
  Size cap = th->ArraySize;
  MLuaValue *newArray;
  Size i;

  if (cap < len) {
    cap = len; /* defensive; capacity always covers the length */
  }
  newArray = (MLuaValue *)MLuaAlloc(L, cap * sizeof(MLuaValue));
  if (!newArray) {
    return FALSE;
  }
  for (i = 0; i < len; i++) {
    /* Allocations never collect (GCPending only), so buf cannot move
     * while this loop boxes its elements. */
    newArray[i] = MLuaMakeNumber(L, (double)buf->Data[i]);
    if (IsNil(newArray[i]) && L->ErrorMsg) {
      return FALSE; /* abandoned newArray is unreachable garbage */
    }
  }
  for (i = len; i < cap; i++) {
    newArray[i] = MLUA_NIL;
  }
  MLuaTableSetArrayData(th, newArray);
  th->ArraySize = (U32)cap;
  th->ArrayLen = (U32)len;
  MLuaTableSetArrayKind(th, MLUA_TABLE_ARRAY_LOCKED);
  return TRUE;
}

/* Can `v` live in a NUM array without changing observable value? Heap
 * floats always; integers only when MLUA_FLOAT holds them exactly. */
static Bool TypedArrayAccepts(MLuaValue v, MLUA_FLOAT *out) {
  if (IsHeapNumber(v)) {
    *out = HeapNumberValue(v);
    return TRUE;
  }
  if (IsInt(v)) {
    I32 i = MLuaGetIntVal(v);
    MLUA_FLOAT f = (MLUA_FLOAT)i;
    if ((I32)f == i) {
      *out = f;
      return TRUE;
    }
  }
  return FALSE;
}

static Bool ArraySet(MLuaState *L, MLuaTableHeader *th, Size index,
                     MLuaValue value);

/* Store into a NUM array; demotes (then stores generically) when the value
 * doesn't fit the representation. Mirrors ArraySet's contract, including
 * the hole error message. */
static Bool TypedArraySet(MLuaState *L, MLuaTableHeader *th, Size index,
                          MLuaValue value) {
  MLuaTableNumArray *buf = TypedArray(th);
  Size len = (Size)buf->Len;
  MLUA_FLOAT f;

  if (index > len + 1) {
    if (IsNil(value)) {
      return TRUE; /* no-op delete of an absent element */
    }
    L->ErrorMsg = "attempt to create a hole in a table array";
    return FALSE;
  }

  if (IsNil(value)) {
    if (index == len && len > 0) {
      buf->Len--; /* deleting the tail shrinks, like the generic part */
      return TRUE;
    }
    if (index == len + 1) {
      return TRUE; /* deleting the absent one-past-end element */
    }
    /* Interior nil: generic arrays represent that; this one cannot. */
    if (!TypedArrayDemote(L, th)) {
      return FALSE;
    }
    return ArraySet(L, th, index, value);
  }

  if (!TypedArrayAccepts(value, &f)) {
    if (!TypedArrayDemote(L, th)) {
      return FALSE;
    }
    return ArraySet(L, th, index, value);
  }

  if (index == len + 1) {
    /* Append; grow with the generic policy (x2 below 1024, then +256) */
    if (index > th->ArraySize) {
      Size newSize = th->ArraySize ? th->ArraySize
                                   : (Size)MLUA_TABLE_INITIAL_ARRAY_SIZE;
      while (newSize < index) {
        newSize = (newSize >= 1024) ? newSize + 256 : newSize * 2;
      }
      if (!TypedArrayGrow(L, th, newSize)) {
        return FALSE;
      }
      buf = TypedArray(th);
    }
    buf->Data[index - 1] = f;
    buf->Len = (U32)index;
    return TRUE;
  }

  buf->Data[index - 1] = f;
  return TRUE;
}

/* First array store of a heap float into a virgin array part: adopt the
 * typed representation. Any pre-sized generic hint buffer is abandoned to
 * the collector. Returns FALSE (leaving the table generic) on OOM. */
static Bool TypedArrayPromote(MLuaState *L, MLuaTableHeader *th,
                              MLuaValue value) {
  MLuaTableNumArray *buf;

  buf = (MLuaTableNumArray *)MLuaAlloc(
      L, sizeof(MLuaTableNumArray) +
             MLUA_TABLE_INITIAL_ARRAY_SIZE * sizeof(MLUA_FLOAT));
  if (!buf) {
    return FALSE;
  }
  buf->Len = 1;
  buf->Data[0] = HeapNumberValue(value);
  MLuaTableSetArrayData(th, (MLuaValue *)buf);
  th->ArraySize = (U32)MLUA_TABLE_INITIAL_ARRAY_SIZE;
  th->ArrayLen = 0; /* stays 0 for NUM: generic fast paths must miss */
  MLuaTableSetArrayInline(th, FALSE);
  MLuaTableSetArrayKind(th, MLUA_TABLE_ARRAY_NUM);
  return TRUE;
}
Bool MLuaTableNumGetFast(MLuaState *L, MLuaTableHeader *th, I32 i,
                         MLuaValue *out) {
  MLuaTableNumArray *buf = TypedArray(th);
  MLuaValue v;
  if (i < 1 || (U32)i > buf->Len) {
    return FALSE;
  }
  v = MLuaMakeNumber(L, (double)buf->Data[i - 1]);
  if (IsNil(v)) {
    return FALSE; /* OOM: the generic route re-attempts and raises */
  }
  *out = v;
  return TRUE;
}

Bool MLuaTableNumSetFast(MLuaTableHeader *th, I32 i, MLuaValue val) {
  MLuaTableNumArray *buf = TypedArray(th);
  MLUA_FLOAT f;
  if (i < 1 || (U32)i > buf->Len) {
    return FALSE;
  }
  if (!TypedArrayAccepts(val, &f)) {
    return FALSE; /* generic route demotes */
  }
  buf->Data[i - 1] = f;
  return TRUE;
}
#endif /* MLUA_TABLE_NUM_ARRAYS */

static MLuaValue ArrayGet(MLuaTableHeader *th, Size index) {
  MLuaValue *array;
  if (index == 0 || index > th->ArrayLen) {
    return MLUA_NIL;
  }
  array = MLuaTableArrayData(th);
  return array[index - 1]; /* Lua indices are 1-based */
}

static Bool ArraySet(MLuaState *L, MLuaTableHeader *th, Size index,
                     MLuaValue value) {
  /* Check for holes - can only set if index == len + 1 or index <= len */
  if (index > th->ArrayLen + 1) {
    /* This would create a hole - not allowed */
    return FALSE;
  }

  /* Grow array if needed */
  if (index > th->ArraySize) {
    Size newSize = th->ArraySize;
    if (newSize == 0) {
      if (index <= MLUA_TABLE_INLINE_ARRAY_CAP) {
        Size i;
        MLuaTableSetArrayInline(th, TRUE);
        th->ArraySize = (U32)MLUA_TABLE_INLINE_ARRAY_CAP;
        for (i = 0; i < MLUA_TABLE_INLINE_ARRAY_CAP; i++) {
          th->InlineArray[i] = MLUA_NIL;
        }
        newSize = th->ArraySize;
      } else {
        newSize = MLUA_TABLE_INITIAL_ARRAY_SIZE;
      }
    }
    while (newSize < index) {
      if (newSize >= 1024) {
        newSize += 256;
      } else {
        newSize *= 2;
      }
    }
    if (index > th->ArraySize && !ArrayGrow(L, th, newSize)) {
      return FALSE;
    }
  }

  MLuaTableArrayData(th)[index - 1] = value;

  /* Update length */
  if (IsNil(value)) {
    /* Deleting - shrink length if at end */
    if (index == th->ArrayLen) {
      th->ArrayLen--;
    }
  } else {
    if (index > th->ArrayLen) {
      th->ArrayLen = index;
    }
  }

  return TRUE;
}

/* ========================================================================== */
/* Hash Part Operations                                                       */
/* ========================================================================== */

static Bool HashGrow(MLuaState *L, MLuaTableHeader *th) {
  Size oldCap = th->NodeCapacity;
  MLuaTableNode *oldNodes = MLuaTableNodeData(th);
  MLuaTableNode inlineCopy[MLUA_TABLE_INLINE_HASH_CAP];
  Bool oldInline = MLuaTableHashIsInline(th);
  Size newCap;
  Size newBytes;
  Size i;

  if (oldInline) {
    for (i = 0; i < oldCap; i++) {
      inlineCopy[i] = oldNodes[i];
    }
    oldNodes = inlineCopy;
  }

  /* The node count lives in NodeState's low bits; capacity (and therefore
   * count) must stay within that mask. */
  if (oldCap > ((Size)MLUA_TABLE_NODE_COUNT_MASK / 2)) {
    return FALSE;
  }
  newCap = (oldCap == 0) ? MLUA_TABLE_INITIAL_HASH_SIZE : oldCap * 2;
  if (newCap > (Size)-1 / sizeof(MLuaTableNode)) {
    return FALSE;
  }
  newBytes = newCap * sizeof(MLuaTableNode);

  MLuaTableNode *newNodes = (MLuaTableNode *)MLuaAlloc(L, newBytes);
  if (!newNodes) {
    return FALSE;
  }
  MLuaTableSetNodeData(th, newNodes);

  th->NodeCapacity = (U32)newCap;
  MLuaTableSetNodeCount(th, 0);
  MLuaTableSetHashInline(th, FALSE);

  /* Initialize new nodes */
  for (i = 0; i < newCap; i++) {
    newNodes[i].Key = MLUA_NIL;
    newNodes[i].Value = MLUA_NIL;
  }

  /* Reinsert old nodes */
  for (i = 0; i < oldCap; i++) {
    if (oldNodes && !IsNil(oldNodes[i].Key)) {
      U32 hash = HashValue(oldNodes[i].Key);
      Size slot = hash % newCap;
      Size j;

      for (j = 0; j < newCap; j++) {
        Size idx = (slot + j) % newCap;
        if (IsNil(newNodes[idx].Key)) {
          newNodes[idx] = oldNodes[i];
          MLuaTableIncNodeCount(th);
          break;
        }
      }
    }
  }

  return TRUE;
}

static MLuaTableNode *HashFind(MLuaTableHeader *th, MLuaValue key) {
  MLuaTableNode *nodes;
  U32 hash;
  Size slot;
  Size i;

  if (th->NodeCapacity == 0) {
    return NULL;
  }

  nodes = MLuaTableNodeData(th);
  hash = HashValue(key);
  slot = hash % th->NodeCapacity;

  for (i = 0; i < th->NodeCapacity; i++) {
    Size idx = (slot + i) % th->NodeCapacity;
    MLuaTableNode *node = &nodes[idx];

    if (IsNil(node->Key)) {
      /* Empty slot - key not found */
      return NULL;
    }

    if (MLuaRawEqual(node->Key, key)) {
      return node;
    }
  }

  return NULL;
}

static Bool HashSet(MLuaState *L, MLuaTableHeader *th, MLuaValue key,
                    MLuaValue value) {
  U32 hash;
  Size slot;
  Size i;
  Size threshold;
  MLuaTableNode *nodes;

  /* Find existing node */
  MLuaTableNode *existing = HashFind(th, key);
  if (existing) {
    existing->Value = value;
    if (IsNil(value)) {
      /* Mark as deleted - for simplicity, we just set key to nil */
      /* A proper implementation would use tombstones */
      existing->Key = MLUA_NIL;
      MLuaTableDecNodeCount(th);
    }
    return TRUE;
  }

  /* Don't insert nil values */
  if (IsNil(value)) {
    return TRUE;
  }

  /* Check if resize needed */
  if (th->NodeCapacity == 0) {
    MLuaTableSetHashInline(th, TRUE);
    th->NodeCapacity = (U32)MLUA_TABLE_INLINE_HASH_CAP;
    for (i = 0; i < MLUA_TABLE_INLINE_HASH_CAP; i++) {
      th->InlineNodes[i].Key = MLUA_NIL;
      th->InlineNodes[i].Value = MLUA_NIL;
    }
  }
  threshold = (th->NodeCapacity * MLUA_TABLE_LOAD_FACTOR) / 100;
  if (MLuaTableNodeCount(th) >= th->NodeCapacity ||
      (!MLuaTableHashIsInline(th) &&
       MLuaTableNodeCount(th) >= threshold)) {
    if (!HashGrow(L, th)) {
      return FALSE;
    }
  }

  /* Insert new node */
  nodes = MLuaTableNodeData(th);
  hash = HashValue(key);
  slot = hash % th->NodeCapacity;

  for (i = 0; i < th->NodeCapacity; i++) {
    Size idx = (slot + i) % th->NodeCapacity;
    if (IsNil(nodes[idx].Key)) {
      nodes[idx].Key = key;
      nodes[idx].Value = value;
      MLuaTableIncNodeCount(th);
      return TRUE;
    }
  }

  return FALSE; /* Should never happen with proper resizing */
}

/* ========================================================================== */
/* Public Table API                                                           */
/* ========================================================================== */

MLuaValue MLuaTableRawGet(MLuaState *L, MLuaValue tbl, MLuaValue key) {
  MLuaGCHeader *gch;
  MLuaTableHeader *th;
  Size index;
  MLuaTableNode *node;

  UNUSED(L);
  if (!IsTable(tbl)) {
    return MLUA_NIL;
  }

  gch = (MLuaGCHeader *)GetPtr(tbl);
  th = MLUA_TABLEHEADER(gch);

  /* Check array part first */
  if (IsPositiveInt(key, &index)) {
#if MLUA_TABLE_NUM_ARRAYS
    /* Kind dispatch must precede ArrayGet: a typed table's ArrayLen is 0,
     * so an in-range read would otherwise miss here and wrongly continue
     * into the hash part / Forward chain. */
    if (MLuaTableArrayKind(th) == MLUA_TABLE_ARRAY_NUM) {
      if (index <= TypedLen(th)) {
        return TypedArrayGet(L, th, index); /* nil+ErrorMsg on OOM */
      }
    } else
#endif
    {
      MLuaValue v = ArrayGet(th, index);
      if (!IsNil(v)) {
        return v;
      }
    }
  }

  /* Check hash part */
  node = HashFind(th, key);
  if (node) {
    return node->Value;
  }

  return MLUA_NIL;
}

MLuaValue MLuaTableGet(MLuaState *L, MLuaValue tbl, MLuaValue key) {
  MLuaValue current = tbl;
  int depth = 0;
  const int MAX_DEPTH = 100; /* Prevent infinite loops */

  while (!IsNil(current) && depth < MAX_DEPTH) {
    MLuaValue v = MLuaTableRawGet(L, current, key);
    if (!IsNil(v)) {
      return v;
    }
#if MLUA_TABLE_NUM_ARRAYS
    /* A nil from a typed-element materialization failure is an error, not
     * an absent key: it must not fall through to the Forward chain. */
    if (L->ErrorMsg) {
      return MLUA_NIL;
    }
#endif

    /* Follow forward chain */
    current = MLuaTableGetForward(current);
    depth++;
  }

  return MLUA_NIL;
}

Bool MLuaTableSet(MLuaState *L, MLuaValue tbl, MLuaValue key, MLuaValue value) {
  MLuaGCHeader *gch;
  MLuaTableHeader *th;
  Size index;

  if (!IsTable(tbl) || IsNil(key)) {
    return FALSE;
  }

  gch = (MLuaGCHeader *)GetPtr(tbl);
  th = MLUA_TABLEHEADER(gch);

  /* Positive integer keys belong to the contiguous array part. */
  if (IsPositiveInt(key, &index)) {
#if MLUA_TABLE_NUM_ARRAYS
    {
      U32 kind = MLuaTableArrayKind(th);
      if (kind == MLUA_TABLE_ARRAY_NUM) {
        return TypedArraySet(L, th, index, value);
      }
      /* A virgin array part whose first store is a heap float adopts the
       * typed representation. Int-first arrays never promote (they are
       * already unboxed in generic slots); a failed promotion allocation
       * just falls through to a generic store. */
      if (kind == MLUA_TABLE_ARRAY_ANY && index == 1 && th->ArrayLen == 0 &&
          IsHeapNumber(value) && TypedArrayPromote(L, th, value)) {
        return TRUE;
      }
    }
#endif
    if (index <= th->ArrayLen + 1) {
      return ArraySet(L, th, index, value);
    }
    /*
     * index > len + 1: storing a value here would leave a gap in the array
     * part, and holes are a runtime error by design (README / SPEC.BROAD).
     * Assigning nil is just a no-op delete of an absent element, so it must
     * succeed -- but it must NOT fall through to the hash part. Falling
     * through is exactly the bug this guards against: a sparse integer write
     * silently masquerading as a successful store in the hash.
     */
    if (IsNil(value)) {
      return TRUE;
    }
    L->ErrorMsg = "attempt to create a hole in a table array";
    return FALSE;
  }

  /* All other keys (strings, non-positive integers, ...) go to the hash. */
  return HashSet(L, th, key, value);
}

Size MLuaTableLen(MLuaValue tbl) {
  MLuaGCHeader *gch;
  MLuaTableHeader *th;

  if (!IsTable(tbl)) {
    return 0;
  }

  gch = (MLuaGCHeader *)GetPtr(tbl);
  th = MLUA_TABLEHEADER(gch);
#if MLUA_TABLE_NUM_ARRAYS
  if (MLuaTableArrayKind(th) == MLUA_TABLE_ARRAY_NUM) {
    return TypedLen(th);
  }
#endif
  return th->ArrayLen;
}

Bool MLuaTableAppend(MLuaState *L, MLuaValue tbl, MLuaValue value) {
  MLuaGCHeader *gch;
  MLuaTableHeader *th;
  Size newIndex;

  if (!IsTable(tbl)) {
    return FALSE;
  }

  gch = (MLuaGCHeader *)GetPtr(tbl);
  th = MLUA_TABLEHEADER(gch);
#if MLUA_TABLE_NUM_ARRAYS
  if (MLuaTableArrayKind(th) == MLUA_TABLE_ARRAY_NUM) {
    return TypedArraySet(L, th, TypedLen(th) + 1, value);
  }
  if (MLuaTableArrayKind(th) == MLUA_TABLE_ARRAY_ANY && th->ArrayLen == 0 &&
      IsHeapNumber(value) && TypedArrayPromote(L, th, value)) {
    return TRUE;
  }
#endif
  newIndex = th->ArrayLen + 1;

  return ArraySet(L, th, newIndex, value);
}

void MLuaTableSetForward(MLuaValue tbl, MLuaValue forward) {
  MLuaGCHeader *gch;
  MLuaTableHeader *th;

  if (!IsTable(tbl)) {
    return;
  }

  gch = (MLuaGCHeader *)GetPtr(tbl);
  th = MLUA_TABLEHEADER(gch);
  th->Forward = forward;
}

MLuaValue MLuaTableGetForward(MLuaValue tbl) {
  MLuaGCHeader *gch;
  MLuaTableHeader *th;

  if (!IsTable(tbl)) {
    return MLUA_NIL;
  }

  gch = (MLuaGCHeader *)GetPtr(tbl);
  th = MLUA_TABLEHEADER(gch);
  return th->Forward;
}

MLuaValue MLuaTableNext(MLuaState *L, MLuaValue tbl, MLuaValue key,
                        MLuaValue *value) {
  MLuaGCHeader *gch;
  MLuaTableHeader *th;
  Size i;
  Bool foundCurrent;
  MLuaValue *array;
  MLuaTableNode *nodes;

  UNUSED(L);
  if (!IsTable(tbl)) {
    return MLUA_NIL;
  }

  gch = (MLuaGCHeader *)GetPtr(tbl);
  th = MLUA_TABLEHEADER(gch);

  foundCurrent = IsNil(key); /* Start from beginning if key is nil */
  array = MLuaTableArrayData(th);

#if MLUA_TABLE_NUM_ARRAYS
  /* Typed array part: materialize elements as we pass them. The generic
   * array loop below is a no-op for NUM tables (ArrayLen is 0). */
  if (MLuaTableArrayKind(th) == MLUA_TABLE_ARRAY_NUM) {
    Size len = TypedLen(th);
    for (i = 0; i < len; i++) {
      if (foundCurrent) {
        MLuaValue v = TypedArrayGet(L, th, i + 1);
        if (IsNil(v)) {
          return MLUA_NIL; /* OOM: ErrorMsg set, caller distinguishes */
        }
        *value = v;
        return MakeInt((I32)(i + 1));
      }
      if (IsInt(key) && MLuaGetIntVal(key) == (I32)(i + 1)) {
        foundCurrent = TRUE;
      }
    }
  }
#endif

  /* First iterate array part */
  for (i = 0; i < th->ArrayLen; i++) {
    MLuaValue arrKey = MakeInt((I32)(i + 1));

    if (foundCurrent) {
      if (!IsNil(array[i])) {
        *value = array[i];
        return arrKey;
      }
    } else if (IsInt(key) && MLuaGetIntVal(key) == (I32)(i + 1)) {
      foundCurrent = TRUE;
    }
  }

  /* Then iterate hash part */
  nodes = MLuaTableNodeData(th);
  for (i = 0; i < th->NodeCapacity; i++) {
    MLuaTableNode *node = &nodes[i];

    if (IsNil(node->Key)) {
      continue;
    }

    if (foundCurrent) {
      *value = node->Value;
      return node->Key;
    } else if (MLuaRawEqual(node->Key, key)) {
      foundCurrent = TRUE;
    }
  }

  return MLUA_NIL; /* End of table */
}

/* ========================================================================== */
/* Safe Table API (SPEC.ERRORS.md compliant)                                  */
/* ========================================================================== */

#include "MLuaConvert.h" /* For MLuaTypeName */
#include "MLuaError.h"

MLuaStatus MLuaTableGetSafe(MLuaState *L, MLuaValue tbl, MLuaValue key,
                            MLuaValue *out) {
  /* Type check: must be a table */
  if (!IsTable(tbl)) {
    L->ErrorMsg = IsNil(tbl) ? "attempt to index a nil value"
                             : "attempt to index a non-table value";
    return MLUA_ERR_RUNTIME;
  }

  /* Fast path: inline-int key hitting a live array slot -- the common
   * shape of indexed reads. A nil slot must take the generic route (it
   * consults the hash part and then the Forward chain); boxed-int keys
   * (32-bit) do too. */
  if (IsInlineInt(key)) {
    I32 i = GetInt(key);
    MLuaTableHeader *th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tbl));
    if (i >= 1 && (U32)i <= th->ArrayLen) {
      MLuaValue v = MLuaTableArrayData(th)[i - 1];
      if (!IsNil(v)) {
        *out = v;
        return MLUA_OK;
      }
    }
  }

#if MLUA_TABLE_NUM_ARRAYS
  L->ErrorMsg = NULL;
  *out = MLuaTableGet(L, tbl, key);
  if (IsNil(*out) && L->ErrorMsg) {
    return MLUA_ERR_RUNTIME; /* typed-element materialization failed */
  }
#else
  *out = MLuaTableGet(L, tbl, key);
#endif
  return MLUA_OK;
}

MLuaStatus MLuaTableSetSafe(MLuaState *L, MLuaValue tbl, MLuaValue key,
                            MLuaValue value) {
  /* Type check: must be a table */
  if (!IsTable(tbl)) {
    L->ErrorMsg = IsNil(tbl) ? "attempt to index a nil value"
                             : "attempt to index a non-table value";
    return MLUA_ERR_RUNTIME;
  }

  /* Fast path: non-nil store to an existing array slot. Appends, nil
   * stores (length bookkeeping), growth, and holes take the generic
   * route. */
  if (IsInlineInt(key) && !IsNil(value)) {
    I32 i = GetInt(key);
    MLuaTableHeader *th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tbl));
    if (i >= 1 && (U32)i <= th->ArrayLen) {
      MLuaTableArrayData(th)[i - 1] = value;
      return MLUA_OK;
    }
  }

  L->ErrorMsg = NULL;
  if (!MLuaTableSet(L, tbl, key, value)) {
    /* MLuaTableSet sets a specific message for the hole case; fall back to a
       generic one for other failures (e.g. allocation failure). */
    if (!L->ErrorMsg) {
      L->ErrorMsg = "table operation failed";
    }
    return MLUA_ERR_RUNTIME;
  }

  return MLUA_OK;
}
