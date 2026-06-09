/*
 * MicroLua - MLuaAlloc.h
 * Bump-pointer heap allocator
 */

#ifndef MLUA_ALLOC_H
#define MLUA_ALLOC_H

#include "MLuaCore.h"
#include "MLuaValue.h"

/* ========================================================================== */
/* Heap State                                                                 */
/* ========================================================================== */

typedef struct MLuaState MLuaState;
typedef struct MLuaGCRef MLuaGCRef;

/* Allocator function types for vector-state mode */
typedef void *(*MLuaAllocFunc)(MLuaState *L, void *ctx, Size size);
typedef void (*MLuaFreeFunc)(MLuaState *L, void *ctx, void *ptr);

/*
 * Initialize with a constrained memory buffer (fixed heap).
 * The caller provides the memory; MicroLua manages it.
 *
 * @param memory  Pointer to contiguous memory block
 * @param size    Size of the memory block in bytes
 * @return        Pointer to initialized state, or NULL on failure
 */
MLuaState *MLuaNewConstrainedState(void *memory, Size size);

/*
 * Initialize with custom allocator functions (vector-state mode).
 * Uses provided allocator for all memory operations.
 *
 * @param ctx       User context passed to allocator functions
 * @param allocFn   Function to allocate memory
 * @param freeFn    Function to free memory
 * @return          Pointer to initialized state, or NULL on failure
 */
MLuaState *MLuaNewVectorState(void *ctx, MLuaAllocFunc allocFn,
                              MLuaFreeFunc freeFn);

/*
 * Legacy alias for constrained state initialization.
 */
#define MLuaStateInit(mem, sz) MLuaNewConstrainedState((mem), (sz))

/*
 * Get amount of memory currently in use.
 */
Size MLuaMemoryUsed(MLuaState *L);

/*
 * Get amount of free memory remaining.
 */
Size MLuaMemoryFree(MLuaState *L);

/*
 * Get total heap size.
 */
Size MLuaMemoryTotal(MLuaState *L);

/* ========================================================================== */
/* Allocation Interface                                                       */
/* ========================================================================== */

/*
 * Allocate memory from the heap (bump-pointer allocation).
 * Returns 8-byte aligned pointer, or NULL if out of memory.
 * May trigger garbage collection if needed.
 *
 * @param L     Runtime state
 * @param size  Number of bytes to allocate
 * @return      Aligned pointer to allocated memory, or NULL
 */
void *MLuaAlloc(MLuaState *L, Size size);

/*
 * Allocate a GC-managed object with header.
 * The returned pointer points to the header, not the data.
 *
 * @param L         Runtime state
 * @param objType   OBJTYPE_* constant
 * @param dataSize  Size of object data (excluding header)
 * @return          Pointer to MLuaGCHeader, or NULL
 */
MLuaGCHeader *MLuaAllocObject(MLuaState *L, U8 objType, Size dataSize);

/*
 * Get header from object data pointer.
 */
#define MLUA_OBJHEADER(data)                                                   \
  ((MLuaGCHeader *)((U8 *)(data) - sizeof(MLuaGCHeader)))

/*
 * Get object size (including header).
 */
Size MLuaObjectSize(MLuaGCHeader *header);

/*
 * Offset of the first GC object in a constrained heap (after the state
 * struct and the carved-out execution arrays). The GC heap walk starts here.
 */
Size MLuaFirstObjOffset(MLuaState *L);

/* ========================================================================== */
/* Call Frames & Execution Contexts                                           */
/* ========================================================================== */

/*
 * One Lua call frame. The VM dispatch loop is frame-iterative: Lua-to-Lua
 * calls push frames instead of recursing in C, which is what allows a
 * coroutine to suspend at arbitrary depth and resume later.
 *
 * PC is stored as a byte OFFSET into the proto's code (never a pointer) and
 * Func as a GC-managed value, so suspended frames survive heap compaction.
 */
typedef struct MLuaFrame {
  MLuaValue Func;  /* The Lua closure being executed */
  Size PC;         /* Saved bytecode offset: resume point / return point */
  Size LocalsBase; /* This frame's base in the Locals array */
  Size EvalBase;   /* EvalStack height at entry; results land here */
  Size ArgsBase;   /* This frame's argument window start in Args */
  Size ArgsCount;  /* Argument count (drives OP_VARARG / MLuaGetArg) */
} MLuaFrame;

/*
 * A full execution context: everything that distinguishes one thread of
 * execution from another. The main thread's context lives in
 * MLuaState.MainCtx while a coroutine runs; each coroutine owns one.
 */
typedef struct MLuaExecCtx {
  MLuaValue *EvalStack;
  Size EvalStackSize;
  Size EvalTop;

  MLuaValue *Locals;
  Size LocalsSize;
  Size LocalsBase;
  Size LocalsTop;

  MLuaValue *Args;
  Size ArgsSize;
  Size ArgsBase;
  Size ArgsTop;
  Size ArgsCount;

  MLuaFrame *Frames;
  Size FrameCap;
  Size FrameTop;

  struct MLuaUpvalue *OpenUpvalues;
} MLuaExecCtx;

/* ========================================================================== */
/* MLuaState Structure                                                        */
/* ========================================================================== */
/*
 * The runtime state structure. Allocated at the start of the heap.
 */

struct MLuaState {
  /* Memory Management */
  U8 *HeapBase;  /* Start of heap memory (NULL for vector mode) */
  Size HeapSize; /* Total heap size (for constrained mode) */
  Size HeapTop;  /* Current bump pointer offset */

  /* Custom Allocator (vector mode) */
  void *AllocCtx;          /* User context for allocator */
  MLuaAllocFunc AllocFunc; /* Custom allocator (NULL = constrained) */
  MLuaFreeFunc FreeFunc;   /* Custom free function */

  /* GC State */
  U8 GCPhase;                  /* Current GC phase */
  U8 GCEnabled;                /* Is GC enabled? */
  Bool GCPending;              /* Threshold crossed: collect at the next VM
                                  safepoint (allocations never move objects
                                  out from under running C code) */
  Size GCThreshold;            /* Trigger GC when HeapTop exceeds this */
  Size GCGrayQueue;            /* Offset to head of gray object queue */
  struct MLuaGCRef *GCRefHead; /* Head of C-side GCRef list */

  /* Expression/Operand Stack */
  MLuaValue *EvalStack; /* Evaluation stack base */
  Size EvalStackSize;   /* Evaluation stack capacity */
  Size EvalTop;         /* Current evaluation stack top index */

  /* Locals Array (per-frame local variables) */
  MLuaValue *Locals; /* Locals array base */
  Size LocalsSize;   /* Locals array capacity */
  Size LocalsBase;   /* Current frame's base index in Locals */
  Size LocalsTop;    /* First free slot above the current frame; new frames
                        (including C-boundary calls like pcall/require) start
                        here, and the GC marks Locals[0..LocalsTop) */

  /* Arguments Array: stacked per-frame windows. The current frame's window
     is [ArgsBase, ArgsBase+ArgsCount); ArgsTop is the first free slot. */
  MLuaValue *Args; /* Arguments array base */
  Size ArgsSize;   /* Arguments array capacity */
  Size ArgsBase;   /* Current frame's window start */
  Size ArgsTop;    /* First free slot above all live windows */
  Size ArgsCount;  /* Current frame's argument count */

  /* Number of values the most recent call (OP_CALL/OP_CALLM/OP_VARARG-all)
     left on the EvalStack. Consumed by OP_ADJUST, OP_CALLM, OP_APPENDM and
     OP_GLOOP_STEP. */
  Size LastCallResults;

  /* Call Frames (frame-iterative dispatch; also drives stack traces) */
  MLuaFrame *Frames; /* Frame array (fixed allocation at state init) */
  Size FrameCap;     /* Capacity */
  Size FrameTop;     /* Current depth */

  /* C-boundary bookkeeping */
  Size CCallDepth; /* Nested MLuaCall entries (yield-across-C detection) */
  Bool YieldFlag;  /* Set by MLuaThreadYield; consumed by the dispatch loop */
  Bool InCCall;    /* Inside a light C function (selects GetTop semantics) */

  /* Upvalues */
  struct MLuaUpvalue *OpenUpvalues; /* List of open upvalues */

  /* String Table (for interning) */
  MLuaValue *StringTable; /* Array of string values */
  Size StringTableCap;    /* Table capacity */
  Size StringTableCount;  /* Number of entries */

  /* Globals */
  MLuaValue Registry; /* Registry table */
  MLuaValue Globals;  /* Global environment (_G) */

  /* Light C Functions */
  void **LightFuncs;   /* Array of registered C function pointers */
  Size LightFuncCount; /* Number of registered light functions */
  Size LightFuncCap;   /* Capacity of light function array */

  /* I/O Callbacks */
  void (*OutputFunc)(MLuaState *, int kind, const char *msg, Size len);
  MLuaValue (*RequireFunc)(MLuaState *, const char *modname);

  /* Coroutine Tracking */
  struct MLuaThread *CurrentThread; /* Running coroutine, NULL for main */
  MLuaExecCtx MainCtx; /* Main thread's saved context while a coroutine runs.
                          Only meaningful when CurrentThread != NULL. */

  /* Error Handling */
  void (*Panic)(MLuaState *); /* Panic handler */
  const char *ErrorMsg;       /* Last error message */
  Size ErrorLine;             /* Line number of last error (0 if unknown) */
  const char *StackTrace;     /* Stacktrace string (set on error) */
};

/* Output kinds for OutputFunc */
#define MLUA_OUTPUT_PRINT 0  /* User print() */
#define MLUA_OUTPUT_ERROR 1  /* Error messages */
#define MLUA_OUTPUT_SYSTEM 2 /* System messages */

/* Default sizes */
#define MLUA_DEFAULT_STACK_SIZE 256
#define MLUA_DEFAULT_GC_THRESHOLD_PERCENT 75

/* ========================================================================== */
/* GC Reference API                                                           */
/* ========================================================================== */

/*
 * Push a GC reference onto the reflist.
 * The reference keeps the value alive and is updated if the GC moves it.
 *
 * @param L    Runtime state
 * @param ref  Caller-allocated GCRef structure
 * @param val  Value to track
 */
void MLuaPushGCRef(MLuaState *L, MLuaGCRef *ref, MLuaValue val);

/*
 * Pop a GC reference from the reflist.
 *
 * @param L    Runtime state
 * @param ref  The same GCRef passed to MLuaPushGCRef
 */
void MLuaPopGCRef(MLuaState *L, MLuaGCRef *ref);

#endif /* MLUA_ALLOC_H */
