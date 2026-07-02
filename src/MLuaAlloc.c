/*
 * MicroLua - MLuaAlloc.c
 * Bump-pointer heap allocator implementation
 */

#include "MLuaAlloc.h"
#include "MLuaCode.h"
#include "MLuaGC.h"
#include "MLuaString.h"
#include "MLuaTable.h"

/* ========================================================================== */
/* State Initialization                                                       */
/* ========================================================================== */

Size MLuaNextGCThreshold(MLuaState *L, Size used) {
  Size growth = (used * MLUA_DEFAULT_GC_THRESHOLD_PERCENT) / 100;
  Size threshold;
  Size reserve;
  Size ceiling;

  /*
   * Keep an allocation reserve below the heap wall. Allocations never
   * collect (safepoint model), so the collection must be REQUESTED while
   * the current instruction's allocations can still succeed from the
   * remaining space; a threshold at HeapSize would only fail the very
   * operation that crossed it, with the heap full of collectable garbage.
   */
  reserve = L->HeapSize / 8;
  if (reserve < 512) {
    reserve = 512;
  }
  if (reserve > 8192) {
    reserve = 8192;
  }
  ceiling = L->HeapSize - reserve;

  if (growth < 4096) {
    growth = 4096;
  }
  threshold = used + growth;
  if (threshold > ceiling) {
    threshold = ceiling;
  }
  if (threshold <= used) {
    /* Live data already sits above the ceiling: the reserve cannot be
     * kept. Hand out half of whatever room remains before the next
     * collection so consecutive collections amortize geometrically; a
     * fixed small batch here means a full mark-compact every few
     * allocations for as long as the heap stays this full. */
    Size batch = (L->HeapSize - used) / 2;
    if (batch < 256) {
      batch = 256;
    }
    threshold = used + batch;
  }
  return threshold;
}

/* Common initialization logic */
static void InitStateCommon(MLuaState *L, Size heapSize) {
  Size i;
  UNUSED(heapSize);

  /* Initialize GC */
  L->GCEnabled = TRUE;
  L->GCPending = FALSE;
  L->GCThreshold = MLuaNextGCThreshold(L, L->HeapTop);
  L->GCPhase = 0;
  L->GCGrayHead = NULL;

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
  argsBytes = MLUA_DEFAULT_ARGS_SIZE * sizeof(MLuaValue);
  argsBytes = ALIGN_UP(argsBytes, MLUA_ALIGNMENT);
  framesBytes = MLUA_DEFAULT_FRAMES_SIZE * sizeof(MLuaFrame);
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
  L->ArgsSize = MLUA_DEFAULT_ARGS_SIZE;
  L->ArgsBase = 0;
  L->ArgsTop = 0;
  L->ArgsCount = 0;
  L->HeapTop += argsBytes;

  /* Allocate Frames */
  L->Frames = (MLuaFrame *)(base + L->HeapTop);
  L->FrameCap = MLUA_DEFAULT_FRAMES_SIZE;
  L->FrameTop = 0;
  L->HeapTop += framesBytes;
  L->HeapPeak = L->HeapTop;
#ifdef MLUA_MEMORY_DIAGNOSTICS
  L->AllocCount = 0;
  L->AllocRequestedBytes = L->HeapTop;
  L->AllocAlignedBytes = L->HeapTop;
#endif

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
#ifdef MLUA_MEMORY_DIAGNOSTICS
  L->HeapPeak = 0;
  L->AllocCount = 0;
  L->AllocRequestedBytes = sizeof(MLuaState);
  L->AllocAlignedBytes = sizeof(MLuaState);
#endif
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
  argsBytes = MLUA_DEFAULT_ARGS_SIZE * sizeof(MLuaValue);
  L->Args = (MLuaValue *)allocFn(L, ctx, argsBytes);
  if (!L->Args) {
    if (freeFn) {
      freeFn(L, ctx, L->Locals);
      freeFn(L, ctx, L->EvalStack);
      freeFn(L, ctx, L);
    }
    return NULL;
  }
  L->ArgsSize = MLUA_DEFAULT_ARGS_SIZE;
  L->ArgsBase = 0;
  L->ArgsTop = 0;
  L->ArgsCount = 0;

  /* Allocate Frames */
  L->Frames =
      (MLuaFrame *)allocFn(L, ctx, MLUA_DEFAULT_FRAMES_SIZE * sizeof(MLuaFrame));
  if (!L->Frames) {
    if (freeFn) {
      freeFn(L, ctx, L->Args);
      freeFn(L, ctx, L->Locals);
      freeFn(L, ctx, L->EvalStack);
      freeFn(L, ctx, L);
    }
    return NULL;
  }
  L->FrameCap = MLUA_DEFAULT_FRAMES_SIZE;
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

Size MLuaMemoryPeak(MLuaState *L) { return L->HeapPeak; }

Size MLuaMemoryFree(MLuaState *L) {
  if (L->AllocFunc) {
    return (Size)-1; /* Unknown in vector mode */
  }
  return L->HeapSize - L->HeapTop;
}

Size MLuaMemoryTotal(MLuaState *L) { return L->HeapSize; }

void MLuaGetMemoryStats(MLuaState *L, MLuaMemoryStats *out) {
  U8 *scan;
  U8 *heapEnd;
  Size firstObjOffset;

  if (!out) {
    return;
  }
  MemSet(out, 0, sizeof(*out));

  out->HeapUsed = L->HeapTop;
  out->HeapPeak = L->HeapPeak;
  out->HeapTotal = L->HeapSize;
  out->HeapBaseline = MLuaFirstObjOffset(L);
  out->ExecReservedBytes =
      ALIGN_UP(L->EvalStackSize * sizeof(MLuaValue), MLUA_ALIGNMENT) +
      ALIGN_UP(L->LocalsSize * sizeof(MLuaValue), MLUA_ALIGNMENT) +
      ALIGN_UP(L->ArgsSize * sizeof(MLuaValue), MLUA_ALIGNMENT) +
      ALIGN_UP(L->FrameCap * sizeof(MLuaFrame), MLUA_ALIGNMENT);
  out->StringTableBytes = L->StringTableCap * sizeof(MLuaValue);
  out->LightFuncBytes = L->LightFuncCap * sizeof(void *);
#ifdef MLUA_MEMORY_DIAGNOSTICS
  out->AllocCount = L->AllocCount;
  out->AllocRequestedBytes = L->AllocRequestedBytes;
  out->AllocAlignedBytes = L->AllocAlignedBytes;
#endif

  if (L->AllocFunc || !L->HeapBase) {
    return;
  }

  firstObjOffset = MLuaFirstObjOffset(L);
  scan = L->HeapBase + firstObjOffset;
  heapEnd = L->HeapBase + L->HeapTop;
  while (scan < heapEnd) {
    MLuaGCHeader *obj = (MLuaGCHeader *)scan;
    Size objSize = MLuaObjectSize(obj);
    U8 objType = MLUA_OBJTYPE(obj);

    if (objSize == 0 || objSize > (Size)(heapEnd - scan)) {
      break;
    }

    if (objType < MLUA_MEMORY_TYPE_SLOTS) {
      out->ObjectCount[objType]++;
      out->ObjectBytes[objType] += objSize;
    }

    switch (objType) {
    case OBJTYPE_STRING: {
      MLuaStringHeader *sh = MLUA_STRHEADER(obj);
      out->StringPayloadBytes += MLuaStrHeaderLen(sh) + 1;
      break;
    }
    case OBJTYPE_TABLE: {
      MLuaTableHeader *th = MLUA_TABLEHEADER(obj);
      Size arrayBytes = th->ArraySize * sizeof(MLuaValue);
      Size hashBytes = th->NodeCapacity * sizeof(MLuaTableNode);
      out->TableArrayBytes += arrayBytes;
      out->TableHashBytes += hashBytes;
      if (MLuaTableArrayIsInline(th)) {
        out->TableInlineArrayBytes += arrayBytes;
        out->TableInlineArrayCount++;
      } else {
        out->TableExternalArrayBytes += arrayBytes;
      }
      if (MLuaTableHashIsInline(th)) {
        out->TableInlineHashBytes += hashBytes;
        out->TableInlineHashCount++;
      } else {
        out->TableExternalHashBytes += hashBytes;
      }
      break;
    }
    case OBJTYPE_PROTO: {
      MLuaProto *proto = MLUA_PROTOHEADER(obj);
      out->ProtoCodeBytes += proto->CodeCap * sizeof(U8);
      out->ProtoConstantsBytes += proto->ConstantsCap * sizeof(MLuaValue);
      out->ProtoProtosBytes += proto->ProtosSize * sizeof(MLuaProto *);
      out->ProtoUpvaluesBytes += proto->UpvaluesSize * sizeof(MLuaUpvalDesc);
#if MLUA_ENABLE_LINEINFO
      out->ProtoLineMapBytes += proto->LineMapCap * sizeof(proto->LineMap[0]);
#endif
      break;
    }
    default:
      break;
    }

    scan += objSize;
  }
}

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
  if (size > (Size)-1 - (MLUA_ALIGNMENT - 1)) {
    return NULL;
  }
  aligned = ALIGN_UP(size, MLUA_ALIGNMENT);

  /*
   * Crossing the threshold only REQUESTS a collection: the VM collects at
   * its next safepoint (instruction boundary), where every live pointer is
   * reloadable from the frames. Collecting here would move objects out
   * from under C code mid-operation.
   */
  if (aligned > (Size)-1 - L->HeapTop) {
    return NULL;
  }

  if (L->GCEnabled && L->HeapTop + aligned > L->GCThreshold) {
    L->GCPending = TRUE;
  }

  newTop = L->HeapTop + aligned;
  if (newTop > L->HeapSize) {
    return NULL; /* Out of memory (a pending GC may free space later) */
  }

  ptr = L->HeapBase + L->HeapTop;
  L->HeapTop = newTop;
#ifdef MLUA_MEMORY_DIAGNOSTICS
  L->AllocCount++;
  L->AllocRequestedBytes += size;
  L->AllocAlignedBytes += aligned;
#endif
  if (L->HeapTop > L->HeapPeak) {
    L->HeapPeak = L->HeapTop;
  }

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
  if (size > (Size)-1 - sizeof(MLuaGCHeader)) {
    return NULL;
  }
  totalSize = sizeof(MLuaGCHeader) + size;
  if (ALIGN_UP(totalSize, MLUA_ALIGNMENT) > (Size)0xFFFFFFFFU) {
    return NULL;
  }
  header = (MLuaGCHeader *)CoreAlloc(L, totalSize);
  if (!header) {
    return NULL;
  }

  header->Flags = (U8)OBJTYPE_RAW;
  header->CachedSize = (U32)ALIGN_UP(totalSize, MLUA_ALIGNMENT);
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
  if (dataSize > (Size)-1 - sizeof(MLuaGCHeader)) {
    return NULL;
  }
  totalSize = sizeof(MLuaGCHeader) + dataSize;
  if (ALIGN_UP(totalSize, MLUA_ALIGNMENT) > (Size)0xFFFFFFFFU) {
    return NULL;
  }

  header = (MLuaGCHeader *)CoreAlloc(L, totalSize);
  if (!header) {
    return NULL;
  }

  /* Initialize header with packed type in flags. CachedSize is the full
   * ALIGNED span so the heap walk steps exactly to the next header. */
  header->Flags = objType & GCFLAG_TYPE_MASK;
  header->CachedSize = (U32)ALIGN_UP(totalSize, MLUA_ALIGNMENT);
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
