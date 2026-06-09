/*
 * MicroLua - MLuaAlloc.c
 * Bump-pointer heap allocator implementation
 */

#include "MLuaAlloc.h"
#include "MLuaGC.h"

/* ========================================================================== */
/* State Initialization                                                       */
/* ========================================================================== */

/* Common initialization logic */
static void InitStateCommon(MLuaState *L, Size heapSize) {
  Size i;

  /* Initialize GC */
  L->GCEnabled = TRUE;
  L->GCPending = FALSE;
  L->GCThreshold = (heapSize * MLUA_DEFAULT_GC_THRESHOLD_PERCENT) / 100;
  L->GCPhase = 0;
  L->GCGrayQueue = 0;

  /* Initialize globals as nil (will be tables later) */
  L->Registry = MLUA_NIL;
  L->Globals = MLUA_NIL;

  /* No light functions registered yet */
  L->LightFuncs = NULL;
  L->LightFuncCount = 0;
  L->LightFuncCap = 0;

  /* No panic handler by default */
  L->Panic = NULL;
  L->ErrorMsg = NULL;

  L->FrameTop = 0;
  L->CCallDepth = 0;
  L->YieldFlag = FALSE;
  L->InCCall = FALSE;
  L->CurrentThread = NULL;
  L->LastCallResults = 0;

  /* GC references list starts empty */
  L->GCRefHead = NULL;

  /* Open upvalues list starts empty */
  L->OpenUpvalues = NULL;

  /* String table initialized on demand */
  L->StringTable = NULL;
  L->StringTableCap = 0;
  L->StringTableCount = 0;

  /* Initialize all three arrays with nil */
  for (i = 0; i < L->EvalStackSize; i++) {
    L->EvalStack[i] = MLUA_NIL;
  }
  for (i = 0; i < L->LocalsSize; i++) {
    L->Locals[i] = MLUA_NIL;
  }
  for (i = 0; i < L->ArgsSize; i++) {
    L->Args[i] = MLUA_NIL;
  }
}

Size MLuaFirstObjOffset(MLuaState *L) {
  Size off = ALIGN_UP(sizeof(MLuaState), MLUA_ALIGNMENT);
  off += ALIGN_UP(L->EvalStackSize * sizeof(MLuaValue), MLUA_ALIGNMENT);
  off += ALIGN_UP(L->LocalsSize * sizeof(MLuaValue), MLUA_ALIGNMENT);
  off += ALIGN_UP(L->ArgsSize * sizeof(MLuaValue), MLUA_ALIGNMENT);
  off += ALIGN_UP(L->FrameCap * sizeof(MLuaFrame), MLUA_ALIGNMENT);
  return off;
}

MLuaState *MLuaNewConstrainedState(void *memory, Size size) {
  U8 *base;
  MLuaState *L;
  Size stateSize;
  Size evalBytes, localsBytes, argsBytes, framesBytes;
  Size remaining;

  if (!memory || size < 4096) {
    return NULL; /* Need at least 4KB */
  }

  base = (U8 *)memory;

  /* Align base to 8 bytes */
  base = (U8 *)ALIGN_UP((UPtr)base, MLUA_ALIGNMENT);
  size = size - (Size)((U8 *)base - (U8 *)memory);

  /* State structure at start of heap */
  stateSize = ALIGN_UP(sizeof(MLuaState), MLUA_ALIGNMENT);
  if (size < stateSize + 2048) {
    return NULL; /* Not enough space */
  }

  L = (MLuaState *)base;
  MemSet(L, 0, sizeof(MLuaState));

  /* Initialize heap pointers */
  L->HeapBase = base;
  L->HeapSize = size;
  L->HeapTop = stateSize;

  /* Constrained mode: no custom allocator */
  L->AllocCtx = NULL;
  L->AllocFunc = NULL;
  L->FreeFunc = NULL;

  /* Calculate array sizes */
  evalBytes = MLUA_DEFAULT_STACK_SIZE * sizeof(MLuaValue);
  evalBytes = ALIGN_UP(evalBytes, MLUA_ALIGNMENT);
  localsBytes = MLUA_DEFAULT_STACK_SIZE * sizeof(MLuaValue);
  localsBytes = ALIGN_UP(localsBytes, MLUA_ALIGNMENT);
  argsBytes = 64 * sizeof(MLuaValue); /* Smaller args array */
  argsBytes = ALIGN_UP(argsBytes, MLUA_ALIGNMENT);
  framesBytes = 64 * sizeof(MLuaFrame);
  framesBytes = ALIGN_UP(framesBytes, MLUA_ALIGNMENT);

  remaining = size - L->HeapTop;
  if (remaining < evalBytes + localsBytes + argsBytes + framesBytes + 512) {
    return NULL; /* Not enough space for arrays */
  }

  /* Allocate EvalStack */
  L->EvalStack = (MLuaValue *)(base + L->HeapTop);
  L->EvalStackSize = MLUA_DEFAULT_STACK_SIZE;
  L->EvalTop = 0;
  L->HeapTop += evalBytes;

  /* Allocate Locals */
  L->Locals = (MLuaValue *)(base + L->HeapTop);
  L->LocalsSize = MLUA_DEFAULT_STACK_SIZE;
  L->LocalsBase = 0;
  L->LocalsTop = 0;
  L->HeapTop += localsBytes;

  /* Allocate Args */
  L->Args = (MLuaValue *)(base + L->HeapTop);
  L->ArgsSize = 64;
  L->ArgsBase = 0;
  L->ArgsTop = 0;
  L->ArgsCount = 0;
  L->HeapTop += argsBytes;

  /* Allocate Frames */
  L->Frames = (MLuaFrame *)(base + L->HeapTop);
  L->FrameCap = 64;
  L->FrameTop = 0;
  L->HeapTop += framesBytes;

  InitStateCommon(L, size);

  return L;
}

MLuaState *MLuaNewVectorState(void *ctx, MLuaAllocFunc allocFn,
                              MLuaFreeFunc freeFn) {
  MLuaState *L;
  Size evalBytes, localsBytes, argsBytes;
  Size i;

  if (!allocFn) {
    return NULL; /* Must provide allocator */
  }

  /* Allocate state structure using custom allocator */
  L = (MLuaState *)allocFn(NULL, ctx, sizeof(MLuaState));
  if (!L) {
    return NULL;
  }

  MemSet(L, 0, sizeof(MLuaState));

  /* Vector mode: use custom allocator */
  L->HeapBase = NULL; /* No fixed heap */
  L->HeapSize = 0;
  L->HeapTop = 0;
  L->AllocCtx = ctx;
  L->AllocFunc = allocFn;
  L->FreeFunc = freeFn;

  /* Allocate EvalStack */
  evalBytes = MLUA_DEFAULT_STACK_SIZE * sizeof(MLuaValue);
  L->EvalStack = (MLuaValue *)allocFn(L, ctx, evalBytes);
  if (!L->EvalStack) {
    if (freeFn)
      freeFn(L, ctx, L);
    return NULL;
  }
  L->EvalStackSize = MLUA_DEFAULT_STACK_SIZE;
  L->EvalTop = 0;

  /* Allocate Locals */
  localsBytes = MLUA_DEFAULT_STACK_SIZE * sizeof(MLuaValue);
  L->Locals = (MLuaValue *)allocFn(L, ctx, localsBytes);
  if (!L->Locals) {
    if (freeFn) {
      freeFn(L, ctx, L->EvalStack);
      freeFn(L, ctx, L);
    }
    return NULL;
  }
  L->LocalsSize = MLUA_DEFAULT_STACK_SIZE;
  L->LocalsBase = 0;
  L->LocalsTop = 0;

  /* Allocate Args */
  argsBytes = 64 * sizeof(MLuaValue);
  L->Args = (MLuaValue *)allocFn(L, ctx, argsBytes);
  if (!L->Args) {
    if (freeFn) {
      freeFn(L, ctx, L->Locals);
      freeFn(L, ctx, L->EvalStack);
      freeFn(L, ctx, L);
    }
    return NULL;
  }
  L->ArgsSize = 64;
  L->ArgsBase = 0;
  L->ArgsTop = 0;
  L->ArgsCount = 0;

  /* Allocate Frames */
  L->Frames = (MLuaFrame *)allocFn(L, ctx, 64 * sizeof(MLuaFrame));
  if (!L->Frames) {
    if (freeFn) {
      freeFn(L, ctx, L->Args);
      freeFn(L, ctx, L->Locals);
      freeFn(L, ctx, L->EvalStack);
      freeFn(L, ctx, L);
    }
    return NULL;
  }
  L->FrameCap = 64;
  L->FrameTop = 0;

  /* Initialize arrays with nil */
  for (i = 0; i < L->EvalStackSize; i++) {
    L->EvalStack[i] = MLUA_NIL;
  }
  for (i = 0; i < L->LocalsSize; i++) {
    L->Locals[i] = MLUA_NIL;
  }
  for (i = 0; i < L->ArgsSize; i++) {
    L->Args[i] = MLUA_NIL;
  }

  InitStateCommon(L, 0);     /* No fixed heap size in vector mode */
  L->GCThreshold = (Size)-1; /* Disable automatic GC in vector mode for now */

  return L;
}

/* ========================================================================== */
/* Memory Statistics                                                          */
/* ========================================================================== */

Size MLuaMemoryUsed(MLuaState *L) { return L->HeapTop; }

Size MLuaMemoryFree(MLuaState *L) {
  if (L->AllocFunc) {
    return (Size)-1; /* Unknown in vector mode */
  }
  return L->HeapSize - L->HeapTop;
}

Size MLuaMemoryTotal(MLuaState *L) { return L->HeapSize; }

/* ========================================================================== */
/* Bump-Pointer Allocation                                                    */
/* ========================================================================== */

/*
 * Core allocation: vector-mode delegates to the custom allocator;
 * constrained mode bump-allocates from the heap. Returns zeroed memory.
 *
 * Every constrained-mode allocation is header-prefixed by the callers
 * below, so the GC's linear heap walk always lands on valid headers.
 */
static void *CoreAlloc(MLuaState *L, Size size) {
  Size aligned;
  Size newTop;
  void *ptr;

  if (size == 0) {
    return NULL;
  }

  /* Vector mode: use custom allocator */
  if (L->AllocFunc) {
    ptr = L->AllocFunc(L, L->AllocCtx, size);
    if (ptr) {
      MemSet(ptr, 0, size);
    }
    return ptr;
  }

  /* Constrained mode: bump-pointer allocation */
  aligned = ALIGN_UP(size, MLUA_ALIGNMENT);

  /*
   * Crossing the threshold only REQUESTS a collection: the VM collects at
   * its next safepoint (instruction boundary), where every live pointer is
   * reloadable from the frames. Collecting here would move objects out
   * from under C code mid-operation.
   */
  if (L->GCEnabled && L->HeapTop + aligned > L->GCThreshold) {
    L->GCPending = TRUE;
  }

  newTop = L->HeapTop + aligned;
  if (newTop > L->HeapSize) {
    return NULL; /* Out of memory (a pending GC may free space later) */
  }

  ptr = L->HeapBase + L->HeapTop;
  L->HeapTop = newTop;

  /* Zero-initialize the allocation */
  MemSet(ptr, 0, aligned);

  return ptr;
}

void *MLuaAlloc(MLuaState *L, Size size) {
  MLuaGCHeader *header;
  Size totalSize;

  if (size == 0) {
    return NULL;
  }

  /*
   * Raw payloads (code buffers, table storage, thread stacks, parser
   * scratch, ...) get an OBJTYPE_RAW header so the heap walk can step over
   * them. They are MOVABLE: each owner marks its buffers live and remaps
   * its pointers during the update phase (a pinned buffer at the heap top
   * would make compaction useless for a bump allocator). Unmarked raw
   * buffers — transient scratch, dead owners — are reclaimed.
   */
  totalSize = sizeof(MLuaGCHeader) + size;
  header = (MLuaGCHeader *)CoreAlloc(L, totalSize);
  if (!header) {
    return NULL;
  }

  header->Flags = (U8)OBJTYPE_RAW;
  header->CachedSize = ALIGN_UP(totalSize, MLUA_ALIGNMENT);
  header->Forward = NULL;

  return MLUA_OBJDATA(header);
}

/* ========================================================================== */
/* GC-Managed Object Allocation                                               */
/* ========================================================================== */

MLuaGCHeader *MLuaAllocObject(MLuaState *L, U8 objType, Size dataSize) {
  Size totalSize;
  MLuaGCHeader *header;

  /* Calculate total size: header + data */
  totalSize = sizeof(MLuaGCHeader) + dataSize;

  header = (MLuaGCHeader *)CoreAlloc(L, totalSize);
  if (!header) {
    return NULL;
  }

  /* Initialize header with packed type in flags. CachedSize is the full
   * ALIGNED span so the heap walk steps exactly to the next header. */
  header->Flags = objType & GCFLAG_TYPE_MASK;
  header->CachedSize = ALIGN_UP(totalSize, MLUA_ALIGNMENT);
  header->Forward = NULL;

  return header;
}

/* ========================================================================== */
/* Object Size Retrieval                                                      */
/* ========================================================================== */

Size MLuaObjectSize(MLuaGCHeader *header) { return header->CachedSize; }

/* ========================================================================== */
/* GC Reference API                                                           */
/* ========================================================================== */

void MLuaPushGCRef(MLuaState *L, MLuaGCRef *ref, MLuaValue val) {
  ref->Value = val;
  ref->Prev = NULL;
  ref->Next = L->GCRefHead;

  if (L->GCRefHead) {
    L->GCRefHead->Prev = ref;
  }

  L->GCRefHead = ref;
}

void MLuaPopGCRef(MLuaState *L, MLuaGCRef *ref) {
  /* Remove from doubly-linked list */
  if (ref->Prev) {
    ref->Prev->Next = ref->Next;
  } else {
    /* This was the head */
    L->GCRefHead = ref->Next;
  }

  if (ref->Next) {
    ref->Next->Prev = ref->Prev;
  }

  ref->Next = NULL;
  ref->Prev = NULL;
  ref->Value = MLUA_NIL;
}
