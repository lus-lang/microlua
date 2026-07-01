/*
 * MicroLua - MLuaGC.c
 * Mark-Compact Garbage Collector Implementation
 */

#include "MLuaGC.h"
#include "MLuaAlloc.h"
#include "MLuaFunc.h"
#include "MLuaString.h"
#include "MLuaTable.h"
#include "MLuaThread.h"

/* GCRef functions moved to MLuaAlloc.c */

/*
 * Optional GC phase tracing for debugging (debug builds link libc).
 * Enable with -DMLUA_GC_TRACE.
 */
#ifdef MLUA_GC_TRACE
#include <stdio.h>
#define GC_TRACE(...) fprintf(stderr, __VA_ARGS__)
#else
#define GC_TRACE(...) ((void)0)
#endif

#ifdef MLUA_GC_TRACE
/* Debug: walk the heap verifying header sanity; report the first bad one */
int MLuaGCVerifyHeap(MLuaState *L, const char *where) {
  U8 *scan = L->HeapBase + MLuaFirstObjOffset(L);
  U8 *heapEnd = L->HeapBase + L->HeapTop;
  while (scan < heapEnd) {
    MLuaGCHeader *obj = (MLuaGCHeader *)scan;
    Size objSize = obj->CachedSize;
    U8 type = MLUA_OBJTYPE(obj);
    if (objSize == 0 || objSize > L->HeapSize || type == 0 || type > 0x0A) {
      fprintf(stderr,
              "[gc] HEAP CORRUPT at %s: off=%lu flags=0x%02x size=%lu\n",
              where, (unsigned long)(scan - L->HeapBase), obj->Flags,
              (unsigned long)objSize);
      return 0;
    }
    scan += objSize;
  }
  return 1;
}
#endif

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

/*
 * Mark a raw (OBJTYPE_RAW) buffer live via its owner. Raw buffers carry no
 * references; an unmarked raw buffer at collection time is garbage (its
 * owner died, or it was transient scratch — safepoint collection guarantees
 * no C operation is mid-flight holding it).
 */
static void MarkRaw(MLuaState *L, void *buffer) {
  if (buffer) {
    MLuaGCMarkObject(L, MLUA_OBJHEADER(buffer));
  }
}

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
    MLuaValue *array = MLuaTableArrayData(th);
    MLuaTableNode *nodes = MLuaTableNodeData(th);
    Size i;
    /* The array/node buffers are owned raw allocations */
    if (!MLuaTableArrayIsInline(th) && th->ArraySize > 0) {
      MarkRaw(L, array);
    }
    if (!MLuaTableHashIsInline(th) && th->NodeCapacity > 0) {
      MarkRaw(L, nodes);
    }
    /* Mark array part */
    for (i = 0; i < th->ArraySize; i++) {
      MLuaGCMark(L, array[i]);
    }
    /* Mark hash part */
    for (i = 0; i < th->NodeCapacity; i++) {
      if (!IsNil(nodes[i].Key)) {
        MLuaGCMark(L, nodes[i].Key);
        MLuaGCMark(L, nodes[i].Value);
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
    /* Mark the prototype (a GC object in its own right) */
    if (cl->Proto) {
      MLuaGCMarkObject(L, MLUA_OBJHEADER(cl->Proto));
    }
    /* Mark upvalues */
    for (i = 0; i < cl->NumUpvalues; i++) {
      MLuaUpvalue *uv = MLUA_CLOSURE_UPVALS(cl)[i];
      if (uv) {
        MLuaGCMarkObject(L, &uv->Header);
      }
    }
    break;
  }
  case OBJTYPE_PROTO: {
    /* Mark the prototype's GC references (source name, constants, nested
     * prototypes) and its owned raw buffers (code, pools, debug info). */
    MLuaProto *proto = (MLuaProto *)MLUA_OBJDATA(obj);
    Size i;
    MarkRaw(L, proto->Code);
    MarkRaw(L, proto->Constants);
    MarkRaw(L, proto->Protos);
    MarkRaw(L, proto->Upvalues);
    MarkRaw(L, proto->LineInfo);
    MarkRaw(L, proto->LineMap);
    MLuaGCMark(L, proto->Source);
    for (i = 0; i < proto->ConstantsSize; i++) {
      MLuaGCMark(L, proto->Constants[i]);
    }
    for (i = 0; i < proto->ProtosSize; i++) {
      if (proto->Protos[i]) {
        MLuaGCMarkObject(L, MLUA_OBJHEADER(proto->Protos[i]));
      }
    }
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
    if (th == L->CurrentThread) {
      /* The RUNNING thread's context lives in L's registers (marked as
       * roots); its Ctx snapshot here is stale — possibly holding buffer
       * pointers from before an earlier compaction. Don't touch it. */
      break;
    }
    /* The context buffers are owned raw allocations */
    MarkRaw(L, th->Ctx.EvalStack);
    MarkRaw(L, th->Ctx.Locals);
    MarkRaw(L, th->Ctx.Args);
    MarkRaw(L, th->Ctx.Frames);
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
    MLuaGCMark(L, th->ErrorValue);
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
  case OBJTYPE_USERDATA:
  case OBJTYPE_NUMBER:
  case OBJTYPE_RAW:
  case OBJTYPE_INT:
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

  /* The string intern table is WEAK: it never keeps a string alive by
   * itself. Unreachable strings are tombstoned during the update phase so
   * the open-addressing probe chains stay intact. The table's backing
   * ARRAY is state-owned, though, as is the light-function registry. */
  MarkRaw(L, L->StringTable);
  MarkRaw(L, L->LightFuncs);

  /* Mark all GCRefs from C code */
  for (ref = L->GCRefHead; ref != NULL; ref = ref->Next) {
    MLuaGCMark(L, ref->Value);
  }
}

/* ========================================================================== */
/* Compute Addresses Phase                                                    */
/* ========================================================================== */

/*
 * Forwarding addresses live in the header's dedicated Forward field
 * (Lisp-2's "extra field"), so computing addresses never clobbers object
 * data that the update phase still needs (upvalue Locations, closure
 * Protos, table headers, ...).
 *
 * Pinned objects (RAW payloads) never move and act as sliding barriers:
 * live objects compact leftwards between pins.
 */

#define FORWARD_PTR(obj) ((obj)->Forward)

static Bool IsPinned(MLuaGCHeader *obj) {
  return (obj->Flags & GCFLAG_PINNED) != 0;
}

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

    if (IsPinned(obj) && MLuaGCIsMarked(obj)) {
      /* Live pin: stays in place; later objects slide to just past it */
      FORWARD_PTR(obj) = scan;
      freePtr = scan + objSize;
    } else if (MLuaGCIsMarked(obj)) {
      /* Store forwarding pointer */
      FORWARD_PTR(obj) = freePtr;
      freePtr += objSize;
    }
    /* Unmarked objects — including DEAD pins (raw buffers whose owner
     * died, or transient scratch) — are garbage and slide away. */

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
  void *newAddr;

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

  if (IsPinned(obj)) {
    return value; /* Live pinned objects don't move */
  }

  /* Get forwarding pointer */
  newAddr = FORWARD_PTR(obj);
  return MakePtr(newAddr);
}

/* Update a raw pointer to a GC header (not wrapped in MLuaValue) */
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

  if (IsPinned(obj)) {
    return ptr; /* Live pinned objects don't move */
  }

  return FORWARD_PTR(obj);
}

/* Update a pointer to object DATA (header-prefixed), e.g. MLuaProto* */
static void *UpdateDataPtr(MLuaState *L, void *dataPtr) {
  MLuaGCHeader *newHdr;

  if (!dataPtr) {
    return NULL;
  }

  newHdr = (MLuaGCHeader *)UpdatePtr(L, MLUA_OBJHEADER(dataPtr));
  return newHdr ? MLUA_OBJDATA(newHdr) : NULL;
}

static void UpdateReferences(MLuaState *L) {
  Size i;
  MLuaGCRef *ref;

  /* Update EvalStack values */
  for (i = 0; i < L->EvalTop; i++) {
#ifdef MLUA_GC_TRACE
    if (IsPtr(L->EvalStack[i]) &&
        !MLuaGCIsMarked((MLuaGCHeader *)GetPtr(L->EvalStack[i]))) {
      GC_TRACE("[gc] !! EvalStack[%lu] unmarked (type 0x%x)\n",
               (unsigned long)i,
               MLUA_OBJTYPE((MLuaGCHeader *)GetPtr(L->EvalStack[i])));
    }
#endif
    L->EvalStack[i] = UpdateValue(L, L->EvalStack[i]);
  }

  /* Update Locals values (including the current frame) */
  for (i = 0; i < L->LocalsTop; i++) {
#ifdef MLUA_GC_TRACE
    if (IsPtr(L->Locals[i]) &&
        !MLuaGCIsMarked((MLuaGCHeader *)GetPtr(L->Locals[i]))) {
      GC_TRACE("[gc] !! Locals[%lu] unmarked (type 0x%x, base=%lu top=%lu)\n",
               (unsigned long)i,
               MLUA_OBJTYPE((MLuaGCHeader *)GetPtr(L->Locals[i])),
               (unsigned long)L->LocalsBase, (unsigned long)L->LocalsTop);
    }
#endif
    L->Locals[i] = UpdateValue(L, L->Locals[i]);
  }

  /* Re-link the open-upvalue list head (upvalue objects may move) */
  L->OpenUpvalues = (struct MLuaUpvalue *)UpdatePtr(L, L->OpenUpvalues);

  /* Update every live Args window, not only the current frame's window. */
  for (i = 0; i < L->ArgsTop; i++) {
    L->Args[i] = UpdateValue(L, L->Args[i]);
  }

  /* Update registry and globals */
  L->Registry = UpdateValue(L, L->Registry);
  L->Globals = UpdateValue(L, L->Globals);

  /* Update string table entries (weak: dead strings become tombstones).
   * MLUA_FALSE keeps the linear-probe chain walkable — lookups skip
   * non-pointer slots — and resize drops tombstones when rehashing. */
  if (L->StringTable) {
    for (i = 0; i < L->StringTableCap; i++) {
      MLuaValue v = L->StringTable[i];
      if (!IsNil(v) && IsPtr(v)) {
        MLuaValue updated = UpdateValue(L, v);
        L->StringTable[i] = IsNil(updated) ? MLUA_FALSE : updated;
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
        MLuaValue *array = MLuaTableArrayData(th);
        MLuaTableNode *nodes = MLuaTableNodeData(th);
        Size i;
        /* Contents are updated through the OLD buffer (they move with it);
         * the buffer pointers are then remapped to the new locations. */
        for (i = 0; i < th->ArraySize; i++) {
          array[i] = UpdateValue(L, array[i]);
        }
        for (i = 0; i < th->NodeCapacity; i++) {
          nodes[i].Key = UpdateValue(L, nodes[i].Key);
          nodes[i].Value = UpdateValue(L, nodes[i].Value);
        }
        th->Forward = UpdateValue(L, th->Forward);
        if (!MLuaTableArrayIsInline(th) && th->ArraySize > 0) {
          MLuaTableSetArrayData(th, (MLuaValue *)UpdateDataPtr(L, array));
        }
        if (!MLuaTableHashIsInline(th) && th->NodeCapacity > 0) {
          MLuaTableSetNodeData(th, (MLuaTableNode *)UpdateDataPtr(L, nodes));
        }
        break;
      }
      case OBJTYPE_FUNCTION: {
        MLuaClosure *cl = MLUA_CLOSURE(obj);
        Size i;
        cl->Proto = (MLuaProto *)UpdateDataPtr(L, cl->Proto);
        for (i = 0; i < cl->NumUpvalues; i++) {
          MLuaUpvalue **uvPtr = &MLUA_CLOSURE_UPVALS(cl)[i];
          if (*uvPtr) {
            *uvPtr = (MLuaUpvalue *)UpdatePtr(L, *uvPtr);
          }
        }
        break;
      }
      case OBJTYPE_PROTO: {
        MLuaProto *proto = (MLuaProto *)MLUA_OBJDATA(obj);
        Size i;
        proto->Source = UpdateValue(L, proto->Source);
        for (i = 0; i < proto->ConstantsSize; i++) {
          proto->Constants[i] = UpdateValue(L, proto->Constants[i]);
        }
        for (i = 0; i < proto->ProtosSize; i++) {
          proto->Protos[i] = (MLuaProto *)UpdateDataPtr(L, proto->Protos[i]);
        }
        /* Remap the owned raw buffers (movable; the VM re-derives pc from
         * Code at every safepoint/frame reload) */
        proto->Code = (U8 *)UpdateDataPtr(L, proto->Code);
        proto->Constants = (MLuaValue *)UpdateDataPtr(L, proto->Constants);
        proto->Protos = (MLuaProto **)UpdateDataPtr(L, proto->Protos);
        proto->Upvalues = (MLuaUpvalDesc *)UpdateDataPtr(L, proto->Upvalues);
        proto->LineInfo = (U8 *)UpdateDataPtr(L, proto->LineInfo);
        proto->LineMap = UpdateDataPtr(L, proto->LineMap);
        break;
      }
      case OBJTYPE_UPVALUE: {
        MLuaUpvalue *uv = MLUA_UPVALUE(obj);
        if (uv->Location == &uv->Closed) {
          /* Closed upvalue: update the value AND recompute the
           * self-pointer against the object's post-move address */
          MLuaUpvalue *newUv = (MLuaUpvalue *)FORWARD_PTR(obj);
          uv->Closed = UpdateValue(L, uv->Closed);
          uv->Location = &newUv->Closed;
        }
        /* Open upvalues' Location points into a Locals array, which never
         * moves — no update needed. The Next chain may move, though. */
        uv->Next = (MLuaUpvalue *)UpdatePtr(L, uv->Next);
        break;
      }
      case OBJTYPE_THREAD: {
        MLuaThread *th = MLUA_THREAD(obj);
        Size i;
        MLuaValue *oldLocals;
        MLuaValue *newLocals;
        MLuaUpvalue *uv;

        if (th == L->CurrentThread) {
          /* The RUNNING thread's buffers are L's live arrays: its contents
           * and pointers are handled at the root level (its Ctx snapshot is
           * stale and gets overwritten by SaveCtx on suspend). Updating it
           * here too would double-update values — unsafe mid-cycle. */
          th->Resumer = (struct MLuaThread *)UpdateDataPtr(L, th->Resumer);
          break;
        }

        oldLocals = th->Ctx.Locals;
        newLocals = (MLuaValue *)UpdateDataPtr(L, oldLocals);

        /* Contents are updated through the OLD buffers (they move with
         * their buffer); pointers are remapped afterwards. */
        for (i = 0; i < th->Ctx.EvalTop; i++) {
          th->Ctx.EvalStack[i] = UpdateValue(L, th->Ctx.EvalStack[i]);
        }
        for (i = 0; i < th->Ctx.LocalsTop; i++) {
          th->Ctx.Locals[i] = UpdateValue(L, th->Ctx.Locals[i]);
        }
        for (i = 0; i < th->Ctx.ArgsTop; i++) {
          th->Ctx.Args[i] = UpdateValue(L, th->Ctx.Args[i]);
        }
        for (i = 0; i < th->Ctx.FrameTop; i++) {
          th->Ctx.Frames[i].Func = UpdateValue(L, th->Ctx.Frames[i].Func);
        }
        th->ErrorValue = UpdateValue(L, th->ErrorValue);

        /* Open upvalues point into this thread's (old) Locals buffer:
         * rebase them against the buffer's new address. The upvalue
         * objects themselves are updated in their own OBJTYPE_UPVALUE
         * pass; here we only fix the Location pointers. */
        if (newLocals && newLocals != oldLocals) {
          for (uv = th->Ctx.OpenUpvalues; uv != NULL; uv = uv->Next) {
            if (uv->Location >= oldLocals &&
                uv->Location < oldLocals + th->Ctx.LocalsSize) {
              uv->Location = newLocals + (uv->Location - oldLocals);
            }
          }
        }

        th->Ctx.EvalStack = (MLuaValue *)UpdateDataPtr(L, th->Ctx.EvalStack);
        th->Ctx.Locals = newLocals;
        th->Ctx.Args = (MLuaValue *)UpdateDataPtr(L, th->Ctx.Args);
        th->Ctx.Frames = (MLuaFrame *)UpdateDataPtr(L, th->Ctx.Frames);
        th->Ctx.OpenUpvalues =
            (struct MLuaUpvalue *)UpdatePtr(L, th->Ctx.OpenUpvalues);
        th->Resumer = (struct MLuaThread *)UpdateDataPtr(L, th->Resumer);
        break;
      }
      default:
        break;
      }

      scan += objSize;
    }
  }

  /* State-owned raw buffers move like everything else: remap their roots */
  L->StringTable = (MLuaValue *)UpdateDataPtr(L, L->StringTable);
  L->LightFuncs = (void **)UpdateDataPtr(L, L->LightFuncs);

  /* Live execution registers and saved contexts */
  for (i = 0; i < L->FrameTop; i++) {
    L->Frames[i].Func = UpdateValue(L, L->Frames[i].Func);
  }
  if (L->CurrentThread) {
    /* Main's context is parked: its arrays are the carved-out (non-heap)
     * ones, so only their CONTENTS need updating. */
    for (i = 0; i < L->MainCtx.EvalTop; i++) {
      L->MainCtx.EvalStack[i] = UpdateValue(L, L->MainCtx.EvalStack[i]);
    }
    for (i = 0; i < L->MainCtx.LocalsTop; i++) {
      L->MainCtx.Locals[i] = UpdateValue(L, L->MainCtx.Locals[i]);
    }
    for (i = 0; i < L->MainCtx.ArgsTop; i++) {
      L->MainCtx.Args[i] = UpdateValue(L, L->MainCtx.Args[i]);
    }
    for (i = 0; i < L->MainCtx.FrameTop; i++) {
      L->MainCtx.Frames[i].Func = UpdateValue(L, L->MainCtx.Frames[i].Func);
    }
    L->MainCtx.OpenUpvalues =
        (struct MLuaUpvalue *)UpdatePtr(L, L->MainCtx.OpenUpvalues);

    /* The RUNNING thread's buffers (live in L) are heap raws that MOVE:
     * rebase open-upvalue Locations into the live Locals buffer, then
     * remap L's array pointers. (When main runs, L's arrays are the carved
     * ones and must not be touched.) */
    {
      MLuaValue *oldLocals = L->Locals;
      MLuaValue *newLocals = (MLuaValue *)UpdateDataPtr(L, oldLocals);
      MLuaUpvalue *uv;

      if (newLocals && newLocals != oldLocals) {
        for (uv = L->OpenUpvalues; uv != NULL; uv = uv->Next) {
          if (uv->Location >= oldLocals &&
              uv->Location < oldLocals + L->LocalsSize) {
            uv->Location = newLocals + (uv->Location - oldLocals);
          }
        }
      }

      L->EvalStack = (MLuaValue *)UpdateDataPtr(L, L->EvalStack);
      L->Locals = newLocals;
      L->Args = (MLuaValue *)UpdateDataPtr(L, L->Args);
      L->Frames = (MLuaFrame *)UpdateDataPtr(L, L->Frames);
    }

    L->CurrentThread =
        (struct MLuaThread *)UpdateDataPtr(L, L->CurrentThread);
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

  /*
   * The compacted heap must stay WALKABLE: every byte below HeapTop belongs
   * to some header-prefixed span. Sliding stops short of live pins, which
   * would leave unwritten gap bytes the next walk misreads as headers — so
   * each gap is plugged: with a filler RAW header (unmarked, unpinned: it
   * reads as garbage and heals away next cycle), or, when the gap is
   * smaller than a header, by extending the previously placed object's
   * span. A sub-header gap always has a predecessor: with nothing placed
   * yet the gap consists of whole dead objects (each >= header size).
   */
  U8 *lastEnd = L->HeapBase + firstObjOffset;
  MLuaGCHeader *lastPlaced = NULL;

  scan = L->HeapBase + firstObjOffset;
  heapEnd = L->HeapBase + L->HeapTop;

  while (scan < heapEnd) {
    obj = (MLuaGCHeader *)scan;
    objSize = MLuaObjectSize(obj);

    if (objSize == 0) {
      break;
    }

    if (IsPinned(obj)) {
      if (MLuaGCIsMarked(obj)) {
        /* Live pin: plug the gap between the last placement and it */
        Size gap = (Size)(scan - lastEnd);
        if (gap >= sizeof(MLuaGCHeader)) {
          MLuaGCHeader *filler = (MLuaGCHeader *)lastEnd;
          filler->Flags = OBJTYPE_RAW; /* unpinned+unmarked => garbage */
          filler->CachedSize = gap;
          filler->Forward = NULL;
        } else if (gap > 0 && lastPlaced) {
          lastPlaced->CachedSize += gap;
        }
        ClearMarked(obj);
        lastEnd = scan + objSize;
        lastPlaced = obj;
      }
      /* Dead pin: abandoned; later placements slide over it */
    } else if (MLuaGCIsMarked(obj)) {
      newAddr = FORWARD_PTR(obj);

      if (newAddr != (void *)scan) {
        /* Move the object */
        MemMove(newAddr, scan, objSize);
      }

      /* Clear mark bit on the moved object */
      ClearMarked((MLuaGCHeader *)newAddr);
      lastEnd = (U8 *)newAddr + objSize;
      lastPlaced = (MLuaGCHeader *)newAddr;
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

  /* Vector mode has no walkable heap: nothing to compact */
  if (L->AllocFunc) {
    return;
  }

  L->GCPhase = GC_PHASE_MARK;

GC_TRACE("[gc] mark (top=%lu)\n", (unsigned long)L->HeapTop);
#ifdef MLUA_GC_TRACE
  MLuaGCVerifyHeap(L, "mark-start");
#endif

  /* Phase 1: Mark all reachable objects */
  MarkRoots(L);

  L->GCPhase = GC_PHASE_COMPUTE;
GC_TRACE("[gc] compute%s\n", "");

  /* Phase 2: Compute new addresses */
  newHeapTop = ComputeAddresses(L);

  L->GCPhase = GC_PHASE_UPDATE;
GC_TRACE("[gc] update (newtop=%lu)\n", (unsigned long)newHeapTop);
#ifdef MLUA_GC_TRACE
  MLuaGCVerifyHeap(L, "after-compute");
#endif

  /* Phase 3: Update all references */
  UpdateReferences(L);

  L->GCPhase = GC_PHASE_COMPACT;
GC_TRACE("[gc] compact%s\n", "");
#ifdef MLUA_GC_TRACE
  MLuaGCVerifyHeap(L, "after-update");
#endif

  /* Phase 4: Move objects */
  Compact(L, newHeapTop);

  L->GCPhase = GC_PHASE_IDLE;
GC_TRACE("[gc] done (top=%lu)\n", (unsigned long)L->HeapTop);
#ifdef MLUA_GC_TRACE
  MLuaGCVerifyHeap(L, "collect-end");
#endif

  /* A weak string table can be mostly tombstones after update/compact. If a
   * smaller backing array is installed, run one cleanup collection so the old
   * backing raw buffer does not remain retained until the next cycle. */
  if (MLuaStringTableShrink(L)) {
    MLuaGCCollect(L);
    return;
  }

  /* Update GC threshold based on live growth, not total heap capacity. */
  L->GCThreshold = MLuaNextGCThreshold(L, L->HeapTop);
  L->GCPending = FALSE;
}

Bool MLuaGCStep(MLuaState *L) {
  /* For now, just do a full collection */
  MLuaGCCollect(L);
  return TRUE;
}
