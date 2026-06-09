/*
 * MicroLua - MLuaGC.c
 * Mark-Compact Garbage Collector Implementation
 */

#include "MLuaGC.h"
#include "MLuaAlloc.h"
#include "MLuaFunc.h"
#include "MLuaTable.h"
#include "MLuaThread.h"

/* GCRef functions moved to MLuaAlloc.c */

/* ========================================================================== */
/* GC Control                                                                 */
/* ========================================================================== */

void MLuaGCEnable(MLuaState *L, Bool enable) { L->GCEnabled = enable; }

Bool MLuaGCIsEnabled(MLuaState *L) { return L->GCEnabled; }

void MLuaGCSetThreshold(MLuaState *L, Size thresholdBytes) {
  L->GCThreshold = thresholdBytes;
}

/* ========================================================================== */
/* Mark Phase                                                                 */
/* ========================================================================== */

Bool MLuaGCIsMarked(MLuaGCHeader *obj) {
  return (obj->Flags & GCFLAG_MARKED) != 0;
}

static void SetMarked(MLuaGCHeader *obj) { obj->Flags |= GCFLAG_MARKED; }

static void ClearMarked(MLuaGCHeader *obj) { obj->Flags &= ~GCFLAG_MARKED; }

void MLuaGCMarkObject(MLuaState *L, MLuaGCHeader *obj) {
  U8 objType;

  if (!obj || MLuaGCIsMarked(obj)) {
    return; /* Already marked or null */
  }

  /* Skip ROM objects - they don't participate in GC */
  if (obj->Flags & GCFLAG_ROM) {
    return;
  }

  SetMarked(obj);

  /* Get object type from packed flags */
  objType = MLUA_OBJTYPE(obj);

  /* Recursively mark children based on object type */
  switch (objType) {
  case OBJTYPE_TABLE: {
    /* Mark table keys and values */
    MLuaTableHeader *th = MLUA_TABLEHEADER(obj);
    Size i;
    /* Mark array part */
    for (i = 0; i < th->ArraySize; i++) {
      MLuaGCMark(L, th->Array[i]);
    }
    /* Mark hash part */
    for (i = 0; i < th->NodeCapacity; i++) {
      if (!IsNil(th->Nodes[i].Key)) {
        MLuaGCMark(L, th->Nodes[i].Key);
        MLuaGCMark(L, th->Nodes[i].Value);
      }
    }
    /* Mark forward table */
    MLuaGCMark(L, th->Forward);
    break;
  }
  case OBJTYPE_FUNCTION: {
    /* Mark closure's upvalues and prototype */
    MLuaClosure *cl = MLUA_CLOSURE(obj);
    Size i;
    /* Mark environment */
    MLuaGCMark(L, cl->Env);
    /* Mark upvalues */
    for (i = 0; i < cl->NumUpvalues; i++) {
      MLuaUpvalue *uv = MLUA_CLOSURE_UPVALS(cl)[i];
      if (uv) {
        MLuaGCMarkObject(L, &uv->Header);
      }
    }
    /* Note: Proto is allocated separately and needs marking via pool */
    break;
  }
  case OBJTYPE_UPVALUE: {
    /* Mark upvalue contents */
    MLuaUpvalue *uv = MLUA_UPVALUE(obj);
    if (uv->Location) {
      MLuaGCMark(L, *uv->Location);
    }
    break;
  }
  case OBJTYPE_THREAD: {
    /* Mark the thread's full suspended context */
    MLuaThread *th = MLUA_THREAD(obj);
    Size i;
    for (i = 0; i < th->Ctx.EvalTop; i++) {
      MLuaGCMark(L, th->Ctx.EvalStack[i]);
    }
    for (i = 0; i < th->Ctx.LocalsTop; i++) {
      MLuaGCMark(L, th->Ctx.Locals[i]);
    }
    for (i = 0; i < th->Ctx.ArgsTop; i++) {
      MLuaGCMark(L, th->Ctx.Args[i]);
    }
    for (i = 0; i < th->Ctx.FrameTop; i++) {
      MLuaGCMark(L, th->Ctx.Frames[i].Func);
    }
    /* Mark open upvalues belonging to this thread */
    {
      MLuaUpvalue *uv = th->Ctx.OpenUpvalues;
      while (uv) {
        MLuaGCMarkObject(L, &uv->Header);
        uv = uv->Next;
      }
    }
    break;
  }
  case OBJTYPE_STRING:
  case OBJTYPE_PROTO:
  case OBJTYPE_USERDATA:
    /* These don't contain GC references */
    break;
  }
}

void MLuaGCMark(MLuaState *L, MLuaValue value) {
  MLuaGCHeader *obj;

  /* Only heap pointers need marking */
  if (!IsPtr(value) || value == 0) {
    return;
  }

  obj = (MLuaGCHeader *)GetPtr(value);
  MLuaGCMarkObject(L, obj);
}

static void MarkRoots(MLuaState *L) {
  Size i;
  MLuaGCRef *ref;
  MLuaUpvalue *uv;

  /* Mark EvalStack values */
  for (i = 0; i < L->EvalTop; i++) {
    MLuaGCMark(L, L->EvalStack[i]);
  }

  /* Mark Locals values, INCLUDING the current frame (LocalsTop is the
   * first free slot above all live frames) */
  for (i = 0; i < L->LocalsTop; i++) {
    MLuaGCMark(L, L->Locals[i]);
  }

  /* Mark the open-upvalue list: the upvalue objects themselves are roots
   * while open (closures referencing them are found transitively, but an
   * upvalue between FindUpvalue and closure attachment must survive too) */
  for (uv = L->OpenUpvalues; uv != NULL; uv = uv->Next) {
    MLuaGCMarkObject(L, &uv->Header);
  }

  /* Mark Args values: every live window, not just the current frame's */
  for (i = 0; i < L->ArgsTop; i++) {
    MLuaGCMark(L, L->Args[i]);
  }

  /* Mark live call frames' functions */
  for (i = 0; i < L->FrameTop; i++) {
    MLuaGCMark(L, L->Frames[i].Func);
  }

  /* When a coroutine is running, the main thread's context is parked in
   * MainCtx and the resume chain is only reachable through it: mark both. */
  if (L->CurrentThread) {
    struct MLuaThread *t;
    for (i = 0; i < L->MainCtx.EvalTop; i++) {
      MLuaGCMark(L, L->MainCtx.EvalStack[i]);
    }
    for (i = 0; i < L->MainCtx.LocalsTop; i++) {
      MLuaGCMark(L, L->MainCtx.Locals[i]);
    }
    for (i = 0; i < L->MainCtx.ArgsTop; i++) {
      MLuaGCMark(L, L->MainCtx.Args[i]);
    }
    for (i = 0; i < L->MainCtx.FrameTop; i++) {
      MLuaGCMark(L, L->MainCtx.Frames[i].Func);
    }
    for (uv = L->MainCtx.OpenUpvalues; uv != NULL; uv = uv->Next) {
      MLuaGCMarkObject(L, &uv->Header);
    }
    for (t = L->CurrentThread; t != NULL; t = t->Resumer) {
      MLuaGCMarkObject(L, MLUA_OBJHEADER(t));
    }
  }

  /* Mark registry and globals */
  MLuaGCMark(L, L->Registry);
  MLuaGCMark(L, L->Globals);

  /* Mark string table entries */
  if (L->StringTable) {
    for (i = 0; i < L->StringTableCap; i++) {
      if (!IsNil(L->StringTable[i])) {
        MLuaGCMark(L, L->StringTable[i]);
      }
    }
  }

  /* Mark all GCRefs from C code */
  for (ref = L->GCRefHead; ref != NULL; ref = ref->Next) {
    MLuaGCMark(L, ref->Value);
  }
}

/* ========================================================================== */
/* Compute Addresses Phase                                                    */
/* ========================================================================== */

/*
 * Forwarding pointer storage:
 * During compaction, we need to store where each object will move.
 * We store this in the first bytes of the object's data area.
 * This is safe because we don't need the data during the update phase.
 */

#define FORWARD_PTR(obj) (*((U8 **)(((U8 *)(obj)) + sizeof(MLuaGCHeader))))

static Size ComputeAddresses(MLuaState *L) {
  U8 *scan;
  U8 *freePtr;
  U8 *heapEnd;
  MLuaGCHeader *obj;
  Size objSize;
  Size firstObjOffset = MLuaFirstObjOffset(L);

  scan = L->HeapBase + firstObjOffset;
  freePtr = scan;
  heapEnd = L->HeapBase + L->HeapTop;

  while (scan < heapEnd) {
    obj = (MLuaGCHeader *)scan;
    objSize = MLuaObjectSize(obj);

    if (objSize == 0) {
      break; /* Corrupted or end of objects */
    }

    if (MLuaGCIsMarked(obj)) {
      /* Store forwarding pointer */
      FORWARD_PTR(obj) = freePtr;
      freePtr += objSize;
    }

    scan += objSize;
  }

  /* Return new heap top */
  return (Size)(freePtr - L->HeapBase);
}

/* ========================================================================== */
/* Update References Phase                                                    */
/* ========================================================================== */

static MLuaValue UpdateValue(MLuaState *L, MLuaValue value) {
  MLuaGCHeader *obj;
  U8 *newAddr;

  UNUSED(L);

  if (!IsPtr(value) || value == 0) {
    return value; /* Not a heap pointer */
  }

  obj = (MLuaGCHeader *)GetPtr(value);

  if (obj->Flags & GCFLAG_ROM) {
    return value; /* ROM objects don't move */
  }

  if (!MLuaGCIsMarked(obj)) {
    return MLUA_NIL; /* Object is garbage, return nil */
  }

  /* Get forwarding pointer */
  newAddr = FORWARD_PTR(obj);
  return MakePtr(newAddr);
}

/* Update a raw pointer (not wrapped in MLuaValue) */
static void *UpdatePtr(MLuaState *L, void *ptr) {
  MLuaGCHeader *obj = (MLuaGCHeader *)ptr;

  UNUSED(L);

  if (!ptr) {
    return NULL;
  }

  if (obj->Flags & GCFLAG_ROM) {
    return ptr; /* ROM objects don't move */
  }

  if (!MLuaGCIsMarked(obj)) {
    return NULL; /* Object is garbage */
  }

  return (void *)FORWARD_PTR(obj);
}

static void UpdateReferences(MLuaState *L) {
  Size i;
  MLuaGCRef *ref;

  /* Update EvalStack values */
  for (i = 0; i < L->EvalTop; i++) {
    L->EvalStack[i] = UpdateValue(L, L->EvalStack[i]);
  }

  /* Update Locals values (including the current frame) */
  for (i = 0; i < L->LocalsTop; i++) {
    L->Locals[i] = UpdateValue(L, L->Locals[i]);
  }

  /* Re-link the open-upvalue list head (upvalue objects may move) */
  L->OpenUpvalues = (struct MLuaUpvalue *)UpdatePtr(L, L->OpenUpvalues);

  /* Update Args values */
  for (i = 0; i < L->ArgsCount; i++) {
    L->Args[i] = UpdateValue(L, L->Args[i]);
  }

  /* Update registry and globals */
  L->Registry = UpdateValue(L, L->Registry);
  L->Globals = UpdateValue(L, L->Globals);

  /* Update string table entries */
  if (L->StringTable) {
    for (i = 0; i < L->StringTableCap; i++) {
      if (!IsNil(L->StringTable[i])) {
        L->StringTable[i] = UpdateValue(L, L->StringTable[i]);
      }
    }
  }

  /* Update GCRefs */
  for (ref = L->GCRefHead; ref != NULL; ref = ref->Next) {
    ref->Value = UpdateValue(L, ref->Value);
  }

  /* Update references inside objects */
  {
    U8 *scan;
    U8 *heapEnd;
    Size firstObjOffset = MLuaFirstObjOffset(L);
    scan = L->HeapBase + firstObjOffset;
    heapEnd = L->HeapBase + L->HeapTop;

    while (scan < heapEnd) {
      MLuaGCHeader *obj = (MLuaGCHeader *)scan;
      Size objSize = MLuaObjectSize(obj);
      U8 objType;

      if (objSize == 0) {
        break;
      }

      if (!MLuaGCIsMarked(obj)) {
        scan += objSize;
        continue;
      }

      objType = MLUA_OBJTYPE(obj);

      switch (objType) {
      case OBJTYPE_TABLE: {
        MLuaTableHeader *th = MLUA_TABLEHEADER(obj);
        Size i;
        for (i = 0; i < th->ArraySize; i++) {
          th->Array[i] = UpdateValue(L, th->Array[i]);
        }
        for (i = 0; i < th->NodeCapacity; i++) {
          th->Nodes[i].Key = UpdateValue(L, th->Nodes[i].Key);
          th->Nodes[i].Value = UpdateValue(L, th->Nodes[i].Value);
        }
        th->Forward = UpdateValue(L, th->Forward);
        break;
      }
      case OBJTYPE_FUNCTION: {
        MLuaClosure *cl = MLUA_CLOSURE(obj);
        Size i;
        cl->Env = UpdateValue(L, cl->Env);
        for (i = 0; i < cl->NumUpvalues; i++) {
          MLuaUpvalue **uvPtr = &MLUA_CLOSURE_UPVALS(cl)[i];
          if (*uvPtr) {
            *uvPtr = (MLuaUpvalue *)UpdatePtr(L, *uvPtr);
          }
        }
        break;
      }
      case OBJTYPE_UPVALUE: {
        MLuaUpvalue *uv = MLUA_UPVALUE(obj);
        if (uv->Location == &uv->Closed) {
          /* Closed upvalue - update the closed value */
          uv->Closed = UpdateValue(L, uv->Closed);
        }
        /* Open upvalues' Location points into the Locals array, which never
         * moves — no update needed. The Next chain may move, though. */
        uv->Next = (MLuaUpvalue *)UpdatePtr(L, uv->Next);
        break;
      }
      default:
        break;
      }

      scan += objSize;
    }
  }
}

/* ========================================================================== */
/* Compact Phase                                                              */
/* ========================================================================== */

static void Compact(MLuaState *L, Size newHeapTop) {
  U8 *scan;
  U8 *heapEnd;
  MLuaGCHeader *obj;
  Size objSize;
  U8 *newAddr;
  Size firstObjOffset = MLuaFirstObjOffset(L);

  scan = L->HeapBase + firstObjOffset;
  heapEnd = L->HeapBase + L->HeapTop;

  while (scan < heapEnd) {
    obj = (MLuaGCHeader *)scan;
    objSize = MLuaObjectSize(obj);

    if (objSize == 0) {
      break;
    }

    if (MLuaGCIsMarked(obj)) {
      newAddr = FORWARD_PTR(obj);

      if (newAddr != scan) {
        /* Move the object */
        MemMove(newAddr, scan, objSize);
      }

      /* Clear mark bit on the moved object */
      ClearMarked((MLuaGCHeader *)newAddr);
    }

    scan += objSize;
  }

  /* Update heap top */
  L->HeapTop = newHeapTop;
}

/* ========================================================================== */
/* Full Collection                                                            */
/* ========================================================================== */

void MLuaGCCollect(MLuaState *L) {
  Size newHeapTop;

  if (!L) {
    return;
  }

  L->GCPhase = GC_PHASE_MARK;

  /* Phase 1: Mark all reachable objects */
  MarkRoots(L);

  L->GCPhase = GC_PHASE_COMPUTE;

  /* Phase 2: Compute new addresses */
  newHeapTop = ComputeAddresses(L);

  L->GCPhase = GC_PHASE_UPDATE;

  /* Phase 3: Update all references */
  UpdateReferences(L);

  L->GCPhase = GC_PHASE_COMPACT;

  /* Phase 4: Move objects */
  Compact(L, newHeapTop);

  L->GCPhase = GC_PHASE_IDLE;

  /* Update GC threshold based on new usage */
  L->GCThreshold = L->HeapTop + (L->HeapSize - L->HeapTop) / 2;
  if (L->GCThreshold > L->HeapSize) {
    L->GCThreshold = L->HeapSize;
  }
}

Bool MLuaGCStep(MLuaState *L) {
  /* For now, just do a full collection */
  MLuaGCCollect(L);
  return TRUE;
}
