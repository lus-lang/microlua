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

  /* Arguments Array (for C function calls) */
  MLuaValue *Args; /* Arguments array base */
  Size ArgsSize;   /* Arguments array capacity */
  Size ArgsCount;  /* Current number of arguments */

  /* Call Stack (for stacktrace) */
  struct {
    void *Proto;     /* MLuaProto* for this frame */
    Size PC;         /* Program counter offset when call was made */
  } CallStack[64];   /* Fixed-size call stack (max nesting depth) */
  Size CallStackTop; /* Current call depth */

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
  void *CurrentThread; /* Currently running coroutine (MLuaThread*) or NULL for
                          main */
  Bool InCoroutine;    /* TRUE if currently in a coroutine (yieldable) */

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
