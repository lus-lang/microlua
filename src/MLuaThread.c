/*
 * MicroLua - MLuaThread.c
 * Coroutine implementation
 */

#include "MLuaThread.h"
#include "MLuaFunc.h"
#include "MLuaGC.h"
#include "MLuaVM.h"

/* ========================================================================== */
/* Thread Creation                                                            */
/* ========================================================================== */

MLuaValue MLuaThreadNew(MLuaState *L, MLuaValue func) {
  MLuaGCHeader *gch;
  MLuaThread *th;
  Size stackBytes;
  Size i;

  /* Validate that func is a function */
  if (!IsFunction(func) && !IsLightFunc(func)) {
    return MLUA_NIL;
  }

  /* Allocate thread object */
  gch = MLuaAllocObject(L, OBJTYPE_THREAD, sizeof(MLuaThread));
  if (!gch) {
    return MLUA_NIL;
  }

  th = MLUA_THREAD(gch);
  th->MainState = L;
  th->Status = THREAD_SUSPENDED;
  th->PC = NULL;
  th->FP = 0;
  th->CallStackTop = 0;
  th->OpenUpvalues = NULL;
  th->ErrorMsg = NULL;

  /* Allocate thread's stack */
  stackBytes = MLUA_THREAD_STACK_SIZE * sizeof(MLuaValue);
  th->Stack = (MLuaValue *)MLuaAlloc(L, stackBytes);
  if (!th->Stack) {
    return MLUA_NIL;
  }

  th->StackSize = MLUA_THREAD_STACK_SIZE;
  th->StackTop = 0;

  /* Initialize stack with nil */
  for (i = 0; i < th->StackSize; i++) {
    th->Stack[i] = MLUA_NIL;
  }

  /* Push the function as first element (to be called on first resume) */
  th->Stack[th->StackTop++] = func;

  return MakePtr(gch);
}

/* ========================================================================== */
/* Thread Resume                                                              */
/* ========================================================================== */

int MLuaThreadResume(MLuaState *L, MLuaValue thread, int nargs) {
  MLuaGCHeader *gch;
  MLuaThread *th;
  Size i;
  int result;
  /* Three-array architecture: save EvalStack state, not Stack */
  MLuaValue *savedEvalStack;
  Size savedEvalTop;
  Size savedEvalStackSize;
  Size savedCallTop;
  struct MLuaUpvalue *savedOpenUpvalues;

  if (!MLuaIsThread(thread)) {
    L->ErrorMsg = "cannot resume non-thread";
    return MLUA_ERRRUN;
  }

  gch = (MLuaGCHeader *)GetPtr(thread);
  th = MLUA_THREAD(gch);

  /* Check status */
  if (th->Status == THREAD_DEAD) {
    L->ErrorMsg = "cannot resume dead coroutine";
    return MLUA_ERRRUN;
  }

  if (th->Status == THREAD_RUNNING) {
    L->ErrorMsg = "cannot resume running coroutine";
    return MLUA_ERRRUN;
  }

  /* Transfer arguments from L's EvalStack to thread's Stack */
  for (i = 0; i < (Size)nargs && L->EvalTop > 0; i++) {
    L->EvalTop--;
    if (th->StackTop < th->StackSize) {
      th->Stack[th->StackTop++] = L->EvalStack[L->EvalTop];
    }
  }

  /* Save main state's execution context (EvalStack) */
  savedEvalStack = L->EvalStack;
  savedEvalTop = L->EvalTop;
  savedEvalStackSize = L->EvalStackSize;
  savedCallTop = L->CallStackTop;
  savedOpenUpvalues = L->OpenUpvalues;

  /* Switch to thread's context: thread uses its own Stack as EvalStack */
  L->EvalStack = th->Stack;
  L->EvalTop = th->StackTop;
  L->EvalStackSize = th->StackSize;
  L->CallStackTop = th->CallStackTop;
  L->OpenUpvalues = th->OpenUpvalues;

  /* Track coroutine state */
  L->CurrentThread = th;
  L->InCoroutine = TRUE;

  /* Mark as running */
  th->Status = THREAD_RUNNING;

  /* Execute: if first resume, call the function; otherwise continue */
  if (th->PC == NULL) {
    /* First resume: call the function at stack[0] */
    MLuaValue func = th->Stack[0];
    Size numArgs = th->StackTop - 1; /* Everything above the function */

    /* Move function to proper position */
    /* Stack layout: [func, arg1, arg2, ...] */
    result = MLuaCall(L, func, numArgs);
  } else {
    /* Continuing from yield - the VM would need to restore PC/FP */
    /* For now, simplified: we don't support yield-in-middle */
    result = MLUA_OK;
  }

  /* Save thread's state back (from EvalStack which was pointing to th->Stack)
   */
  th->Stack = L->EvalStack;
  th->StackTop = L->EvalTop;
  th->StackSize = L->EvalStackSize;
  th->CallStackTop = L->CallStackTop;
  th->OpenUpvalues = L->OpenUpvalues;

  /* Restore main state's context */
  L->EvalStack = savedEvalStack;
  L->EvalTop = savedEvalTop;
  L->EvalStackSize = savedEvalStackSize;
  L->CallStackTop = savedCallTop;
  L->OpenUpvalues = savedOpenUpvalues;

  /* Reset coroutine tracking to main thread */
  L->CurrentThread = NULL;
  L->InCoroutine = FALSE;

  /* Update status based on result */
  if (result == MLUA_YIELD) {
    th->Status = THREAD_SUSPENDED;
    /* Transfer yielded values back to L's EvalStack */
    for (i = 0; i < th->StackTop && L->EvalTop < L->EvalStackSize; i++) {
      L->EvalStack[L->EvalTop++] = th->Stack[i];
    }
  } else if (result == MLUA_OK) {
    th->Status = THREAD_DEAD;
    /* Transfer return values back to L's EvalStack */
    for (i = 0; i < th->StackTop && L->EvalTop < L->EvalStackSize; i++) {
      L->EvalStack[L->EvalTop++] = th->Stack[i];
    }
  } else {
    th->Status = THREAD_DEAD;
    th->ErrorMsg = L->ErrorMsg;
  }

  return result;
}

/* ========================================================================== */
/* Thread Yield                                                               */
/* ========================================================================== */

int MLuaThreadYield(MLuaState *L, int nresults) {
  /*
   * Yield implementation:
   * In a full implementation, this would set a flag that the VM checks
   * and saves its state (PC, FP, etc.) before returning MLUA_YIELD.
   *
   * For now, we use a simplified approach where yield is a status return.
   * The VM loop checks for this and unwinds appropriately.
   */
  UNUSED(nresults);
  L->ErrorMsg = NULL;
  return MLUA_YIELD;
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
