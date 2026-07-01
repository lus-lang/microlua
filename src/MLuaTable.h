/*
 * MicroLua - MLuaTable.h
 * Bifurcated table structure (array + hash parts)
 */

#ifndef MLUA_TABLE_H
#define MLUA_TABLE_H

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaValue.h"

/* ========================================================================== */
/* Table Structure                                                            */
/* ========================================================================== */
/*
 * Tables have two parts:
 * 1. Array part: contiguous array for integer keys 1..n (no holes allowed)
 * 2. Hash part: hash table for all other keys
 *
 * Tables can have a "forward" table for prototype delegation.
 * If a key is not found in this table, the lookup continues in forward.
 */

/* Hash table node */
typedef struct {
  MLuaValue Key;
  MLuaValue Value;
} MLuaTableNode;

#define MLUA_TABLE_INLINE_ARRAY_CAP 3
#define MLUA_TABLE_INLINE_HASH_CAP 1
#define MLUA_TABLE_NODE_COUNT_MASK 0x3FFFFFFFU
#define MLUA_TABLE_ARRAY_INLINE 0x40000000U
#define MLUA_TABLE_HASH_INLINE 0x80000000U

/* Table header (follows GC header) */
typedef struct {
  MLuaValue Forward;      /* Forward table for failed lookups */
  U32 ArraySize;          /* Current array capacity */
  U32 ArrayLen;           /* Actual length (# operator) */
  U32 NodeCapacity;       /* Number of hash slots */
  U32 NodeState;          /* Node count plus MLUA_TABLE_*_INLINE flags */
  MLuaValue InlineArray[MLUA_TABLE_INLINE_ARRAY_CAP];
  MLuaTableNode InlineNodes[MLUA_TABLE_INLINE_HASH_CAP];
} MLuaTableHeader;

/* Get table header from GC header */
#define MLUA_TABLEHEADER(gch) ((MLuaTableHeader *)MLUA_OBJDATA(gch))

/* Initial sizes */
#define MLUA_TABLE_INITIAL_ARRAY_SIZE 4
#define MLUA_TABLE_INITIAL_HASH_SIZE 4
#define MLUA_TABLE_LOAD_FACTOR 75

static inline Bool MLuaTableArrayIsInline(const MLuaTableHeader *th) {
  return (th->NodeState & MLUA_TABLE_ARRAY_INLINE) != 0;
}

static inline Bool MLuaTableHashIsInline(const MLuaTableHeader *th) {
  return (th->NodeState & MLUA_TABLE_HASH_INLINE) != 0;
}

static inline Size MLuaTableNodeCount(const MLuaTableHeader *th) {
  return (Size)(th->NodeState & MLUA_TABLE_NODE_COUNT_MASK);
}

static inline void MLuaTableSetNodeCount(MLuaTableHeader *th, Size count) {
  th->NodeState = (th->NodeState & ~MLUA_TABLE_NODE_COUNT_MASK) |
                  ((U32)count & MLUA_TABLE_NODE_COUNT_MASK);
}

static inline void MLuaTableIncNodeCount(MLuaTableHeader *th) {
  MLuaTableSetNodeCount(th, MLuaTableNodeCount(th) + 1);
}

static inline void MLuaTableDecNodeCount(MLuaTableHeader *th) {
  MLuaTableSetNodeCount(th, MLuaTableNodeCount(th) - 1);
}

static inline void MLuaTableSetArrayInline(MLuaTableHeader *th, Bool enabled) {
  if (enabled) {
    th->NodeState |= MLUA_TABLE_ARRAY_INLINE;
  } else {
    th->NodeState &= ~MLUA_TABLE_ARRAY_INLINE;
  }
}

static inline void MLuaTableSetHashInline(MLuaTableHeader *th, Bool enabled) {
  if (enabled) {
    th->NodeState |= MLUA_TABLE_HASH_INLINE;
  } else {
    th->NodeState &= ~MLUA_TABLE_HASH_INLINE;
  }
}

static inline MLuaValue *MLuaTableArrayData(MLuaTableHeader *th) {
  return MLuaTableArrayIsInline(th) ? th->InlineArray
                                    : (MLuaValue *)(UPtr)th->InlineArray[0];
}

static inline const MLuaValue *MLuaTableArrayDataConst(const MLuaTableHeader *th) {
  return MLuaTableArrayIsInline(th) ? th->InlineArray
                                    : (const MLuaValue *)(UPtr)th->InlineArray[0];
}

static inline MLuaTableNode *MLuaTableNodeData(MLuaTableHeader *th) {
  return MLuaTableHashIsInline(th) ? th->InlineNodes
                                   : (MLuaTableNode *)(UPtr)th->InlineNodes[0].Key;
}

static inline const MLuaTableNode *
MLuaTableNodeDataConst(const MLuaTableHeader *th) {
  return MLuaTableHashIsInline(th)
             ? th->InlineNodes
             : (const MLuaTableNode *)(UPtr)th->InlineNodes[0].Key;
}

static inline void MLuaTableSetArrayData(MLuaTableHeader *th,
                                         MLuaValue *array) {
  th->InlineArray[0] = (MLuaValue)(UPtr)array;
}

static inline void MLuaTableSetNodeData(MLuaTableHeader *th,
                                        MLuaTableNode *nodes) {
  th->InlineNodes[0].Key = (MLuaValue)(UPtr)nodes;
}

/* ========================================================================== */
/* Table API                                                                  */
/* ========================================================================== */

/*
 * Create a new empty table.
 */
MLuaValue MLuaTableNew(MLuaState *L);

/*
 * Create a table with pre-allocated sizes.
 */
MLuaValue MLuaTableNewSized(MLuaState *L, Size arrayHint, Size hashHint);

/*
 * Get a value from a table.
 * Follows forward chain if key not found.
 *
 * @param L     Runtime state
 * @param tbl   Table value
 * @param key   Key to look up
 * @return      Value or MLUA_NIL if not found
 */
MLuaValue MLuaTableGet(MLuaState *L, MLuaValue tbl, MLuaValue key);

/*
 * Set a value in a table.
 * Returns FALSE on error (e.g., memory allocation failed, hole in array).
 *
 * @param L     Runtime state
 * @param tbl   Table value
 * @param key   Key to set
 * @param value Value to set (MLUA_NIL to delete)
 * @return      TRUE on success
 */
Bool MLuaTableSet(MLuaState *L, MLuaValue tbl, MLuaValue key, MLuaValue value);

/*
 * Get the length of the array part (# operator).
 */
Size MLuaTableLen(MLuaValue tbl);

/*
 * Append a value to the array part (t[#t + 1] = v).
 */
Bool MLuaTableAppend(MLuaState *L, MLuaValue tbl, MLuaValue value);

/*
 * Set the forward (prototype) table.
 */
void MLuaTableSetForward(MLuaValue tbl, MLuaValue forward);

/*
 * Get the forward (prototype) table.
 */
MLuaValue MLuaTableGetForward(MLuaValue tbl);

/*
 * Get raw value without following forward chain.
 */
MLuaValue MLuaTableRawGet(MLuaValue tbl, MLuaValue key);

/*
 * Iterate over table (for pairs()).
 * Returns next key after 'key', or MLUA_NIL if done.
 * Sets *value to the value for the returned key.
 *
 * @param tbl   Table value
 * @param key   Current key (MLUA_NIL to start)
 * @param value Pointer to receive value
 * @return      Next key or MLUA_NIL
 */
MLuaValue MLuaTableNext(MLuaValue tbl, MLuaValue key, MLuaValue *value);

/* ========================================================================== */
/* Safe Table API (SPEC.ERRORS.md compliant)                                  */
/* ========================================================================== */

/* Note: MLuaStatus defined in MLuaCore.h (already included above) */

/*
 * Safe get - validates that tbl is a table before accessing.
 * Returns MLUA_ERR_RUNTIME if tbl is not a table.
 */
MLuaStatus MLuaTableGetSafe(MLuaState *L, MLuaValue tbl, MLuaValue key,
                            MLuaValue *out);

/*
 * Safe set - validates that tbl is a table before setting.
 * Returns MLUA_ERR_RUNTIME if tbl is not a table.
 */
MLuaStatus MLuaTableSetSafe(MLuaState *L, MLuaValue tbl, MLuaValue key,
                            MLuaValue value);

#endif /* MLUA_TABLE_H */
