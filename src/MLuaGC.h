/*
 * MicroLua - MLuaGC.h
 * Mark-Compact Garbage Collector
 */

#ifndef MLUA_GC_H
#define MLUA_GC_H

#include "MLuaCore.h"
#include "MLuaValue.h" /* provides the MLuaState forward typedef */

/* ========================================================================== */
/* GC Phases                                                                  */
/* ========================================================================== */

#define GC_PHASE_IDLE 0
#define GC_PHASE_MARK 1
#define GC_PHASE_COMPUTE 2
#define GC_PHASE_UPDATE 3
#define GC_PHASE_COMPACT 4

/* ========================================================================== */
/* GC Reference (C-side safety mechanism)                                     */
/* ========================================================================== */
/*
 * When C code needs to hold references to Lua objects across
 * allocations (which may trigger GC), it must use GCRef handles.
 * These handles are updated automatically if objects move.
 */

struct MLuaGCRef {
  MLuaGCRef *Prev; /* Linked list for tracking */
  MLuaGCRef *Next;
  MLuaValue Value; /* The referenced value */
};

/*
 * Push a GCRef onto the reference stack.
 * The referenced value will be kept alive and its pointer
 * will be updated if the object moves during GC.
 * (Defined in MLuaAlloc.c)
 */
void MLuaPushGCRef(MLuaState *L, MLuaGCRef *ref, MLuaValue value);

/*
 * Pop a GCRef from the reference stack.
 * The reference is no longer tracked.
 * (Defined in MLuaAlloc.c)
 */
void MLuaPopGCRef(MLuaState *L, MLuaGCRef *ref);

/* ========================================================================== */
/* GC Control                                                                 */
/* ========================================================================== */

/*
 * Run a full garbage collection cycle.
 * This performs Mark-Compact collection:
 * 1. Mark: Traverse from roots, mark all reachable objects
 * 2. Compute: Calculate new addresses for live objects
 * 3. Update: Update all pointers to use new addresses
 * 4. Compact: Move objects to their new locations
 */
void MLuaGCCollect(MLuaState *L);

/*
 * Enable or disable automatic GC.
 */
void MLuaGCEnable(MLuaState *L, Bool enable);

/*
 * Check if GC is enabled.
 */
Bool MLuaGCIsEnabled(MLuaState *L);

/*
 * Set GC threshold (percentage of heap that triggers collection).
 */
void MLuaGCSetThreshold(MLuaState *L, Size thresholdBytes);

/*
 * Step through one phase of GC (for incremental collection).
 * Returns TRUE if collection is complete.
 */
Bool MLuaGCStep(MLuaState *L);

/* ========================================================================== */
/* Internal GC Functions                                                      */
/* ========================================================================== */

/*
 * Mark an object as reachable.
 */
void MLuaGCMark(MLuaState *L, MLuaValue value);

/*
 * Mark an object header as reachable.
 */
void MLuaGCMarkObject(MLuaState *L, MLuaGCHeader *obj);

/*
 * Check if an object is marked.
 */
Bool MLuaGCIsMarked(MLuaGCHeader *obj);

/*
 * Get object size (including header).
 */
Size MLuaObjectSize(MLuaGCHeader *header);

#endif /* MLUA_GC_H */
