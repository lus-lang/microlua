/*
 * MicroLua - MLuaThread.h
 * Coroutine (thread) implementation
 *
 * Coroutines in MicroLua are cooperative threads with their own stacks.
 * Each coroutine shares the same heap but has its own value stack.
 *
 * Note: coroutine.wrap is not supported because light C functions cannot
 * have upvalues to store the wrapped thread state.
 */

#ifndef MLUA_THREAD_H
#define MLUA_THREAD_H

#include "MLuaAlloc.h"
#include "MLuaValue.h"

/* ========================================================================== */
/* Thread Status                                                              */
/* ========================================================================== */

typedef enum {
  THREAD_SUSPENDED = 0, /* Not yet started or yielded */
  THREAD_RUNNING,       /* Currently executing */
  THREAD_NORMAL,        /* Resumed another coroutine */
  THREAD_DEAD           /* Finished or errored */
} MLuaThreadStatus;

/* ========================================================================== */
/* Thread Structure                                                           */
/* ========================================================================== */

/*
 * Thread (coroutine) structure, stored as a heap object.
 * Each thread has its own stack but shares the main state's heap.
 */
typedef struct MLuaThread {
  MLuaState *MainState; /* Reference to main state (for heap access) */

  /* Thread's own stack */
  MLuaValue *Stack;
  Size StackSize;
  Size StackTop;

  /* Call stack */
  Size CallStackTop;

  /* Execution state */
  U8 *PC;  /* Current program counter (if suspended) */
  Size FP; /* Frame pointer at suspension */
  MLuaThreadStatus Status;

  /* Upvalues belonging to this thread */
  struct MLuaUpvalue *OpenUpvalues;

  /* Error message if dead with error */
  const char *ErrorMsg;
} MLuaThread;

/* Get thread header from GC header */
#define MLUA_THREAD(gch) ((MLuaThread *)MLUA_OBJDATA(gch))

/* Default thread stack size */
#define MLUA_THREAD_STACK_SIZE 64

/* ========================================================================== */
/* Thread API                                                                 */
/* ========================================================================== */

/*
 * Create a new coroutine from a function.
 * @param L     Main state
 * @param func  The function to run in the coroutine (must be on stack)
 * @return      Thread value, or nil on failure
 */
MLuaValue MLuaThreadNew(MLuaState *L, MLuaValue func);

/*
 * Resume a suspended coroutine.
 * @param L       Main state
 * @param thread  The thread to resume
 * @param nargs   Number of arguments on L's stack to pass
 * @return        Status code (MLUA_OK, MLUA_YIELD, or error)
 *
 * On success, the return values are on L's stack.
 */
int MLuaThreadResume(MLuaState *L, MLuaValue thread, int nargs);

/*
 * Yield from the currently running coroutine.
 * @param L       The coroutine's state
 * @param nresults Number of results to yield
 * @return        This function does not return normally; it longjmps
 *
 * Note: MicroLua uses status propagation, so yield sets a flag and returns.
 */
int MLuaThreadYield(MLuaState *L, int nresults);

/*
 * Get the status of a coroutine.
 * @param thread  The thread to query
 * @return        Status enum value
 */
MLuaThreadStatus MLuaThreadGetStatus(MLuaValue thread);

/*
 * Check if a value is a thread.
 */
Bool MLuaIsThread(MLuaValue val);

#endif /* MLUA_THREAD_H */
