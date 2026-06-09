/*
 * MicroLua - MLuaThread.h
 * Coroutine (thread) implementation
 *
 * Coroutines are cooperative threads. Each owns a full execution context
 * (EvalStack, Locals, Args, Frames, open upvalues); resuming swaps the
 * context into the shared MLuaState, suspending swaps it back out. Because
 * the VM is frame-iterative, a coroutine can yield from arbitrary Lua call
 * depth; the only restriction is Lua 5.1's: you cannot yield across a
 * C-call boundary (e.g. from inside a pcall'd function).
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
 * Thread (coroutine) structure, stored as a heap object. The context's
 * arrays are separate raw heap allocations (they never move, preserving
 * open-upvalue Location pointers into Locals).
 *
 * Not-started is encoded as Ctx.FrameTop == 0 with the function value at
 * Ctx.EvalStack[0].
 */
typedef struct MLuaThread {
  MLuaExecCtx Ctx; /* Full execution context */
  MLuaThreadStatus Status;

  struct MLuaThread *Resumer; /* Who resumed us (NULL = main thread) */
  Size BaseCCalls;            /* L->CCallDepth at this thread's entry; yield
                                 is only legal when depth equals this */

  /* Yield-value hand-off: the yield call's argument window inside Ctx.Args
     (stays reserved while suspended; released on the next resume) */
  Size XferBase;
  Size XferCount;

  const char *ErrorMsg; /* Error message if dead with error */
} MLuaThread;

/* Get thread header from GC header */
#define MLUA_THREAD(gch) ((MLuaThread *)MLUA_OBJDATA(gch))

/* Per-thread context sizes (entries, not bytes) */
#define MLUA_THREAD_EVAL_SIZE 64
#define MLUA_THREAD_LOCALS_SIZE 64
#define MLUA_THREAD_ARGS_SIZE 32
#define MLUA_THREAD_FRAMES_SIZE 16

/* ========================================================================== */
/* Thread API                                                                 */
/* ========================================================================== */

/*
 * Create a new coroutine from a function.
 * @param L     Main state
 * @param func  The function to run in the coroutine
 * @return      Thread value, or nil on failure
 */
MLuaValue MLuaThreadNew(MLuaState *L, MLuaValue func);

/*
 * Resume a suspended coroutine.
 * @param L       State (with the resumer's context live)
 * @param thread  The thread to resume
 * @param argv    Resume arguments (copied before any context switch)
 * @param nargs   Number of arguments
 * @param nres    Out: number of values transferred onto the resumer's
 *                EvalStack (yield values or return values)
 * @return        MLUA_YIELD (suspended), MLUA_OK (finished) or error code
 */
int MLuaThreadResume(MLuaState *L, MLuaValue thread, const MLuaValue *argv,
                     int nargs, Size *nres);

/*
 * Request a yield from the currently running coroutine. Returns MLUA_OK and
 * sets L->YieldFlag on success (the dispatch loop suspends after the
 * current C function returns); returns MLUA_ERRRUN with ErrorMsg set when
 * yielding is illegal (main thread, or across a C-call boundary).
 */
int MLuaThreadYield(MLuaState *L, int nresults);

/*
 * Get the status of a coroutine.
 */
MLuaThreadStatus MLuaThreadGetStatus(MLuaValue thread);

/*
 * Check if a value is a thread.
 */
Bool MLuaIsThread(MLuaValue val);

#endif /* MLUA_THREAD_H */
