/*
 * MicroLua - MLuaThread.c
 * Coroutine implementation
 *
 * Each coroutine owns a complete execution context (MLuaExecCtx). Resuming
 * saves the resumer's context (main's into L->MainCtx, a parent coroutine's
 * into its own thread object), loads the target's context into L, and runs
 * the frame machine. Yielding sets L->YieldFlag; the dispatch loop saves
 * the resume point and returns MLUA_YIELD up to MLuaThreadResume, which
 * swaps the contexts back and hands the yielded values to the resumer.
 */

#include "MLuaThread.h"
#include "MLuaFunc.h"
#include "MLuaGC.h"
#include "MLuaString.h"
#include "MLuaVM.h"

/* ========================================================================== */
/* Context Save/Load                                                          */
/* ========================================================================== */

static void SaveCtx(MLuaState *L, MLuaExecCtx *c) {
  c->EvalStack = L->EvalStack;
  c->EvalStackSize = L->EvalStackSize;
  c->EvalTop = L->EvalTop;

  c->Locals = L->Locals;
  c->LocalsSize = L->LocalsSize;
  c->LocalsBase = L->LocalsBase;
  c->LocalsTop = L->LocalsTop;

  c->Args = L->Args;
  c->ArgsSize = L->ArgsSize;
  c->ArgsBase = L->ArgsBase;
  c->ArgsTop = L->ArgsTop;
  c->ArgsCount = L->ArgsCount;

  c->Frames = L->Frames;
  c->FrameCap = L->FrameCap;
  c->FrameTop = L->FrameTop;

  c->OpenUpvalues = L->OpenUpvalues;
}

static void LoadCtx(MLuaState *L, const MLuaExecCtx *c) {
  L->EvalStack = c->EvalStack;
  L->EvalStackSize = c->EvalStackSize;
  L->EvalTop = c->EvalTop;

  L->Locals = c->Locals;
  L->LocalsSize = c->LocalsSize;
  L->LocalsBase = c->LocalsBase;
  L->LocalsTop = c->LocalsTop;

  L->Args = c->Args;
  L->ArgsSize = c->ArgsSize;
  L->ArgsBase = c->ArgsBase;
  L->ArgsTop = c->ArgsTop;
  L->ArgsCount = c->ArgsCount;

  L->Frames = c->Frames;
  L->FrameCap = c->FrameCap;
  L->FrameTop = c->FrameTop;

  L->OpenUpvalues = c->OpenUpvalues;
}

/* The context that describes the CURRENT thread of execution */
static MLuaExecCtx *OwnerCtx(MLuaState *L) {
  return L->CurrentThread ? &L->CurrentThread->Ctx : &L->MainCtx;
}

/* ========================================================================== */
/* Thread Creation                                                            */
/* ========================================================================== */

MLuaValue MLuaThreadNew(MLuaState *L, MLuaValue func) {
  MLuaGCHeader *gch;
  MLuaThread *th;
  MLuaValue *eval;
  MLuaValue *locals;
  MLuaValue *args;
  MLuaFrame *frames;
  Size i;

  /* Validate that func is a function */
  if (!IsFunction(func) && !IsLightFunc(func)) {
    return MLUA_NIL;
  }

  /* Allocate the context buffers FIRST (raw heap allocations that never
   * move), keeping the thread object allocation last so 'func' need not
   * survive an allocation while unanchored — pre-Phase-3 the GC cannot
   * relocate, and post-Phase-3 these are pinned RAW objects. */
  eval = (MLuaValue *)MLuaAlloc(L, MLUA_THREAD_EVAL_SIZE * sizeof(MLuaValue));
  locals =
      (MLuaValue *)MLuaAlloc(L, MLUA_THREAD_LOCALS_SIZE * sizeof(MLuaValue));
  args = (MLuaValue *)MLuaAlloc(L, MLUA_THREAD_ARGS_SIZE * sizeof(MLuaValue));
  frames =
      (MLuaFrame *)MLuaAlloc(L, MLUA_THREAD_FRAMES_SIZE * sizeof(MLuaFrame));
  if (!eval || !locals || !args || !frames) {
    return MLUA_NIL;
  }

  gch = MLuaAllocObject(L, OBJTYPE_THREAD, sizeof(MLuaThread));
  if (!gch) {
    return MLUA_NIL;
  }

  th = MLUA_THREAD(gch);

  th->Ctx.EvalStack = eval;
  th->Ctx.EvalStackSize = MLUA_THREAD_EVAL_SIZE;
  th->Ctx.Locals = locals;
  th->Ctx.LocalsSize = MLUA_THREAD_LOCALS_SIZE;
  th->Ctx.Args = args;
  th->Ctx.ArgsSize = MLUA_THREAD_ARGS_SIZE;
  th->Ctx.Frames = frames;
  th->Ctx.FrameCap = MLUA_THREAD_FRAMES_SIZE;

  for (i = 0; i < MLUA_THREAD_EVAL_SIZE; i++) {
    eval[i] = MLUA_NIL;
  }
  for (i = 0; i < MLUA_THREAD_LOCALS_SIZE; i++) {
    locals[i] = MLUA_NIL;
  }
  for (i = 0; i < MLUA_THREAD_ARGS_SIZE; i++) {
    args[i] = MLUA_NIL;
  }

  /* Not started: the function waits at EvalStack[0], FrameTop == 0 */
  th->Ctx.EvalStack[0] = func;
  th->Ctx.EvalTop = 1;
  th->Ctx.LocalsBase = 0;
  th->Ctx.LocalsTop = 0;
  th->Ctx.ArgsBase = 0;
  th->Ctx.ArgsTop = 0;
  th->Ctx.ArgsCount = 0;
  th->Ctx.FrameTop = 0;
  th->Ctx.OpenUpvalues = NULL;

  th->Status = THREAD_SUSPENDED;
  th->Resumer = NULL;
  th->BaseCCalls = 0;
  th->XferBase = 0;
  th->XferCount = 0;
  th->ErrorValue = MLUA_NIL;

  return MakePtr(gch);
}

/* ========================================================================== */
/* Thread Resume                                                              */
/* ========================================================================== */

int MLuaThreadResume(MLuaState *L, MLuaValue thread, const MLuaValue *argv,
                     int nargs, Size *nres) {
  MLuaGCHeader *gch;
  MLuaThread *th;
  MLuaThread *prev;
  MLuaStatus status;
  Size i;

  *nres = 0;

  if (!MLuaIsThread(thread)) {
    L->ErrorMsg = "cannot resume non-thread";
    return MLUA_ERRRUN;
  }

  gch = (MLuaGCHeader *)GetPtr(thread);
  th = MLUA_THREAD(gch);

  if (th->Status == THREAD_DEAD) {
    L->ErrorMsg = "cannot resume dead coroutine";
    return MLUA_ERRRUN;
  }
  if (th->Status != THREAD_SUSPENDED) {
    L->ErrorMsg = "cannot resume non-suspended coroutine";
    return MLUA_ERRRUN;
  }

  /* Save the resumer's context and switch over */
  SaveCtx(L, OwnerCtx(L));

  prev = L->CurrentThread;
  th->Resumer = prev;
  if (prev) {
    prev->Status = THREAD_NORMAL;
  }
  L->CurrentThread = th;
  th->Status = THREAD_RUNNING;

  LoadCtx(L, &th->Ctx);

  if (th->Ctx.FrameTop == 0) {
    /* First resume: the function sits at EvalStack[0]; arguments follow */
    if ((Size)nargs + 1 > L->EvalStackSize) {
      nargs = (int)(L->EvalStackSize - 1);
    }
    for (i = 0; i < (Size)nargs; i++) {
      L->EvalStack[1 + i] = argv[i];
    }
    L->EvalTop = 1 + (Size)nargs;

    /* MLuaCall increments CCallDepth itself; the body runs at depth+1 */
    th->BaseCCalls = L->CCallDepth + 1;
    status = MLuaCall(L, nargs, -1);
  } else {
    /* Resuming after a yield: release the yield-args window reserved at
     * suspension, then push the resume arguments as the pending yield
     * call's results. */
    L->ArgsTop = th->XferBase;

    if (nargs == 0) {
      MLuaPush(L, MLUA_NIL); /* >=1-result call convention */
      L->LastCallResults = 1;
    } else {
      for (i = 0; i < (Size)nargs; i++) {
        MLuaPush(L, argv[i]);
      }
      L->LastCallResults = (Size)nargs;
    }

    /* Re-entering the suspended frames is a C boundary too */
    L->CCallDepth++;
    th->BaseCCalls = L->CCallDepth;
    status = MLuaRunSuspended(L);
    L->CCallDepth--;
  }

  if (status != MLUA_OK && status != MLUA_YIELD) {
    const char *msg = L->ErrorMsg ? L->ErrorMsg : "error";
    th->ErrorValue = MLuaStringNew(L, msg, StrLen(msg));
  }

  /* Capture the thread's final state, switch back to the resumer */
  SaveCtx(L, &th->Ctx);

  L->CurrentThread = prev;
  if (prev) {
    prev->Status = THREAD_RUNNING;
  }
  LoadCtx(L, OwnerCtx(L));

  if (status == MLUA_YIELD) {
    th->Status = THREAD_SUSPENDED;

    /* Hand the yielded values (the yield call's arguments) to the resumer */
    for (i = 0; i < th->XferCount; i++) {
      MLuaPush(L, th->Ctx.Args[th->XferBase + i]);
    }
    *nres = th->XferCount;
    return MLUA_YIELD;
  }

  if (status == MLUA_OK) {
    /* Finished: frame 0's EvalBase is 0, so the return values are exactly
     * EvalStack[0..EvalTop) of the thread's context. */
    th->Status = THREAD_DEAD;
    for (i = 0; i < th->Ctx.EvalTop; i++) {
      MLuaPush(L, th->Ctx.EvalStack[i]);
    }
    *nres = th->Ctx.EvalTop;
    th->Ctx.EvalTop = 0;
    return MLUA_OK;
  }

  /* Error: the thread dies; its frames were already unwound by RunVM */
  th->Status = THREAD_DEAD;
  if (IsNil(th->ErrorValue)) {
    th->ErrorValue = MLuaStringNew(L, "error", 5);
  }
  return status;
}

/* ========================================================================== */
/* Thread Yield                                                               */
/* ========================================================================== */

int MLuaThreadYield(MLuaState *L, int nresults) {
  MLuaThread *th = L->CurrentThread;

  UNUSED(nresults);

  if (!th) {
    L->ErrorMsg = "attempt to yield from outside a coroutine";
    return MLUA_ERRRUN;
  }

  if (L->CCallDepth != th->BaseCCalls) {
    L->ErrorMsg = "attempt to yield across a C-call boundary";
    return MLUA_ERRRUN;
  }

  /* The yield values are exactly this C call's arguments: hand the window
   * over to the resumer (the dispatch loop keeps it reserved). */
  th->XferBase = L->ArgsBase;
  th->XferCount = L->ArgsCount;

  L->YieldFlag = TRUE;
  return MLUA_OK;
}

/* ========================================================================== */
/* Thread Status                                                              */
/* ========================================================================== */

MLuaThreadStatus MLuaThreadGetStatus(MLuaValue thread) {
  MLuaGCHeader *gch;
  MLuaThread *th;

  if (!MLuaIsThread(thread)) {
    return THREAD_DEAD;
  }

  gch = (MLuaGCHeader *)GetPtr(thread);
  th = MLUA_THREAD(gch);

  return th->Status;
}

/* ========================================================================== */
/* Type Check                                                                 */
/* ========================================================================== */

Bool MLuaIsThread(MLuaValue val) {
  MLuaGCHeader *gch;

  if (!IsPtr(val) || IsNil(val)) {
    return FALSE;
  }

  gch = (MLuaGCHeader *)GetPtr(val);
  return MLUA_OBJTYPE(gch) == OBJTYPE_THREAD;
}
