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
#define MLUA_TABLE_NODE_COUNT_MASK 0x0FFFFFFFU
#define MLUA_TABLE_ARRAY_KIND_SHIFT 28
/* (U32)3, not 3U: on targets whose unsigned int is narrower than 32 bits
 * (e.g. 24-bit int), shifting the plain literal by 28 is undefined. */
#define MLUA_TABLE_ARRAY_KIND_MASK ((U32)3 << MLUA_TABLE_ARRAY_KIND_SHIFT)
#define MLUA_TABLE_ARRAY_INLINE 0x40000000U
#define MLUA_TABLE_HASH_INLINE 0x80000000U

/*
 * Array-part representation kind (NodeState bits 28-29).
 * ANY: generic MLuaValue slots; a fresh table that may still promote.
 * NUM: raw MLUA_FLOAT elements (32-bit tagging path only, behind
 *      MLUA_TABLE_NUM_ARRAYS); ArrayLen stays 0 so every generic fast path
 *      falls through to the kind-aware slow path by construction.
 * LOCKED: generic slots after a demotion; never promotes again.
 */
#define MLUA_TABLE_ARRAY_ANY 0U
#define MLUA_TABLE_ARRAY_NUM 1U
#define MLUA_TABLE_ARRAY_LOCKED 2U

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

static inline U32 MLuaTableArrayKind(const MLuaTableHeader *th) {
  return (th->NodeState & MLUA_TABLE_ARRAY_KIND_MASK) >>
         MLUA_TABLE_ARRAY_KIND_SHIFT;
}

static inline void MLuaTableSetArrayKind(MLuaTableHeader *th, U32 kind) {
  th->NodeState = (th->NodeState & ~MLUA_TABLE_ARRAY_KIND_MASK) |
                  (kind << MLUA_TABLE_ARRAY_KIND_SHIFT);
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
 * Get raw value without following forward chain. Reading a typed (NUM)
 * array element materializes a number value, which may allocate; on
 * allocation failure returns MLUA_NIL with L->ErrorMsg set.
 */
MLuaValue MLuaTableRawGet(MLuaState *L, MLuaValue tbl, MLuaValue key);

/*
 * Iterate over table (for pairs()).
 * Returns next key after 'key', or MLUA_NIL if done.
 * Sets *value to the value for the returned key. Typed (NUM) array
 * elements materialize on read; on allocation failure returns MLUA_NIL
 * with L->ErrorMsg set (callers must distinguish that from end-of-table).
 *
 * @param L     Runtime state
 * @param tbl   Table value
 * @param key   Current key (MLUA_NIL to start)
 * @param value Pointer to receive value
 * @return      Next key or MLUA_NIL
 */
MLuaValue MLuaTableNext(MLuaState *L, MLuaValue tbl, MLuaValue key,
                        MLuaValue *value);

#if MLUA_TABLE_NUM_ARRAYS
/*
 * Typed-array fast-path helpers for the VM's fused indexing opcodes: an
 * in-range read (materializes; FALSE on out-of-range or OOM - the generic
 * route then reports the error) and an in-place overwrite of an existing
 * slot with a representable value (appends/demotions return FALSE and take
 * the generic route). Callers must have checked the array kind is NUM.
 */
Bool MLuaTableNumGetFast(MLuaState *L, MLuaTableHeader *th, I32 i,
                         MLuaValue *out);
Bool MLuaTableNumSetFast(MLuaTableHeader *th, I32 i, MLuaValue val);
#endif

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
