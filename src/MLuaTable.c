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
    /* Simple integer hash */
    I32 i = GetInt(key);
    return (U32)((i * 2654435761U) & 0xFFFFFFFF);
  }

  if (IsShortStr(key)) {
    /* Hash the short string bytes */
    char buf[4];
    buf[0] = GetShortStrChar0(key);
    buf[1] = GetShortStrChar1(key);
    buf[2] = GetShortStrChar2(key);
    buf[3] = '\0';
    return MLuaStringHash(buf, MLuaShortStrLen(key));
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
  th->Array = NULL;
  th->ArraySize = 0;
  th->ArrayLen = 0;
  th->Nodes = NULL;
  th->NodeCapacity = 0;
  th->NodeCount = 0;
  th->Forward = MLUA_NIL;

  /* Pre-allocate array part if hint given */
  if (arrayHint > 0) {
    if (arrayHint > (Size)-1 / sizeof(MLuaValue)) {
      return MakePtr(gch);
    }
    Size arrayBytes = arrayHint * sizeof(MLuaValue);
    th->Array = (MLuaValue *)MLuaAlloc(L, arrayBytes);
    if (th->Array) {
      th->ArraySize = arrayHint;
      for (i = 0; i < arrayHint; i++) {
        th->Array[i] = MLUA_NIL;
      }
    }
  }

  /* Pre-allocate hash part if hint given */
  if (hashHint > 0) {
    if (hashHint > (Size)-1 / sizeof(MLuaTableNode)) {
      return MakePtr(gch);
    }
    Size hashBytes = hashHint * sizeof(MLuaTableNode);
    th->Nodes = (MLuaTableNode *)MLuaAlloc(L, hashBytes);
    if (th->Nodes) {
      th->NodeCapacity = hashHint;
      for (i = 0; i < hashHint; i++) {
        th->Nodes[i].Key = MLUA_NIL;
        th->Nodes[i].Value = MLUA_NIL;
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

  if (!IsInt(key)) {
    return FALSE;
  }

  i = GetInt(key);
  if (i > 0) {
    *index = (Size)i;
    return TRUE;
  }

  return FALSE;
}

static Bool ArrayGrow(MLuaState *L, MLuaTableHeader *th, Size newSize) {
  Size oldSize = th->ArraySize;
  Size newBytes;
  MLuaValue *newArray;
  Size i;

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
    newArray[i] = th->Array[i];
  }

  /* Initialize new elements to nil */
  for (i = oldSize; i < newSize; i++) {
    newArray[i] = MLUA_NIL;
  }

  th->Array = newArray;
  th->ArraySize = newSize;

  return TRUE;
}

static MLuaValue ArrayGet(MLuaTableHeader *th, Size index) {
  if (index == 0 || index > th->ArrayLen) {
    return MLUA_NIL;
  }
  return th->Array[index - 1]; /* Lua indices are 1-based */
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
    if (newSize == 0)
      newSize = MLUA_TABLE_INITIAL_ARRAY_SIZE;
    while (newSize < index) {
      newSize *= 2;
    }
    if (!ArrayGrow(L, th, newSize)) {
      return FALSE;
    }
  }

  th->Array[index - 1] = value;

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
  MLuaTableNode *oldNodes = th->Nodes;
  Size newCap;
  Size newBytes;
  Size i;

  if (oldCap > ((Size)-1 / 2)) {
    return FALSE;
  }
  newCap = (oldCap == 0) ? MLUA_TABLE_INITIAL_HASH_SIZE : oldCap * 2;
  if (newCap > (Size)-1 / sizeof(MLuaTableNode)) {
    return FALSE;
  }
  newBytes = newCap * sizeof(MLuaTableNode);

  th->Nodes = (MLuaTableNode *)MLuaAlloc(L, newBytes);
  if (!th->Nodes) {
    th->Nodes = oldNodes;
    return FALSE;
  }

  th->NodeCapacity = newCap;
  th->NodeCount = 0;

  /* Initialize new nodes */
  for (i = 0; i < newCap; i++) {
    th->Nodes[i].Key = MLUA_NIL;
    th->Nodes[i].Value = MLUA_NIL;
  }

  /* Reinsert old nodes */
  for (i = 0; i < oldCap; i++) {
    if (!IsNil(oldNodes[i].Key)) {
      U32 hash = HashValue(oldNodes[i].Key);
      Size slot = hash % newCap;
      Size j;

      for (j = 0; j < newCap; j++) {
        Size idx = (slot + j) % newCap;
        if (IsNil(th->Nodes[idx].Key)) {
          th->Nodes[idx] = oldNodes[i];
          th->NodeCount++;
          break;
        }
      }
    }
  }

  return TRUE;
}

static MLuaTableNode *HashFind(MLuaTableHeader *th, MLuaValue key) {
  U32 hash;
  Size slot;
  Size i;

  if (th->NodeCapacity == 0) {
    return NULL;
  }

  hash = HashValue(key);
  slot = hash % th->NodeCapacity;

  for (i = 0; i < th->NodeCapacity; i++) {
    Size idx = (slot + i) % th->NodeCapacity;
    MLuaTableNode *node = &th->Nodes[idx];

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

  /* Find existing node */
  MLuaTableNode *existing = HashFind(th, key);
  if (existing) {
    existing->Value = value;
    if (IsNil(value)) {
      /* Mark as deleted - for simplicity, we just set key to nil */
      /* A proper implementation would use tombstones */
      existing->Key = MLUA_NIL;
      th->NodeCount--;
    }
    return TRUE;
  }

  /* Don't insert nil values */
  if (IsNil(value)) {
    return TRUE;
  }

  /* Check if resize needed */
  threshold = (th->NodeCapacity * MLUA_TABLE_LOAD_FACTOR) / 100;
  if (th->NodeCount >= threshold || th->NodeCapacity == 0) {
    if (!HashGrow(L, th)) {
      return FALSE;
    }
  }

  /* Insert new node */
  hash = HashValue(key);
  slot = hash % th->NodeCapacity;

  for (i = 0; i < th->NodeCapacity; i++) {
    Size idx = (slot + i) % th->NodeCapacity;
    if (IsNil(th->Nodes[idx].Key)) {
      th->Nodes[idx].Key = key;
      th->Nodes[idx].Value = value;
      th->NodeCount++;
      return TRUE;
    }
  }

  return FALSE; /* Should never happen with proper resizing */
}

/* ========================================================================== */
/* Public Table API                                                           */
/* ========================================================================== */

MLuaValue MLuaTableRawGet(MLuaValue tbl, MLuaValue key) {
  MLuaGCHeader *gch;
  MLuaTableHeader *th;
  Size index;
  MLuaTableNode *node;

  if (!IsTable(tbl)) {
    return MLUA_NIL;
  }

  gch = (MLuaGCHeader *)GetPtr(tbl);
  th = MLUA_TABLEHEADER(gch);

  /* Check array part first */
  if (IsPositiveInt(key, &index)) {
    MLuaValue v = ArrayGet(th, index);
    if (!IsNil(v)) {
      return v;
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

  UNUSED(L);

  while (!IsNil(current) && depth < MAX_DEPTH) {
    MLuaValue v = MLuaTableRawGet(current, key);
    if (!IsNil(v)) {
      return v;
    }

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

MLuaValue MLuaTableNext(MLuaValue tbl, MLuaValue key, MLuaValue *value) {
  MLuaGCHeader *gch;
  MLuaTableHeader *th;
  Size i;
  Bool foundCurrent;

  if (!IsTable(tbl)) {
    return MLUA_NIL;
  }

  gch = (MLuaGCHeader *)GetPtr(tbl);
  th = MLUA_TABLEHEADER(gch);

  foundCurrent = IsNil(key); /* Start from beginning if key is nil */

  /* First iterate array part */
  for (i = 0; i < th->ArrayLen; i++) {
    MLuaValue arrKey = MakeInt((I32)(i + 1));

    if (foundCurrent) {
      if (!IsNil(th->Array[i])) {
        *value = th->Array[i];
        return arrKey;
      }
    } else if (IsInt(key) && GetInt(key) == (I32)(i + 1)) {
      foundCurrent = TRUE;
    }
  }

  /* Then iterate hash part */
  for (i = 0; i < th->NodeCapacity; i++) {
    MLuaTableNode *node = &th->Nodes[i];

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

  *out = MLuaTableGet(L, tbl, key);
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
