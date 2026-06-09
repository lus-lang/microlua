/*
 * MicroLua - MLuaCoroLib.c
 * Coroutine library implementation
 */

#include "MLuaCoroLib.h"
#include "../MLuaCore.h"
#include "../MLuaFunc.h"
#include "../MLuaString.h"
#include "../MLuaThread.h"
#include "../MLuaVM.h"

/* ========================================================================== */
/* coroutine.create                                                           */
/* ========================================================================== */

static int CoroCreate(MLuaState *L) {
  MLuaValue func = MLuaGetArg(L, 0);
  MLuaValue thread = MLuaThreadNew(L, func);

  if (IsNil(thread)) {
    L->ErrorMsg = "bad argument #1 to 'create' (function expected)";
    return -1;
  }

  MLuaPush(L, thread);
  return 1;
}

/* ========================================================================== */
/* coroutine.resume                                                           */
/* ========================================================================== */

static int CoroResume(MLuaState *L) {
  MLuaValue thread = MLuaGetArg(L, 0);
  int top = MLuaGetTop(L);
  int nargs = top - 1; /* Arguments after the thread */
  Size entry = L->EvalTop;
  Size nres = 0;
  int status;

  if (nargs < 0) {
    nargs = 0;
  }

  /* Reserve the status slot so true/false precedes the transferred values.
   * The resume arguments live in our Args window; the pointer stays valid
   * across the context switch because each context has its own buffers. */
  MLuaPush(L, MLUA_NIL);

  status =
      MLuaThreadResume(L, thread, &L->Args[L->ArgsBase + 1], nargs, &nres);

  if (status == MLUA_OK || status == MLUA_YIELD) {
    L->EvalStack[entry] = MLUA_TRUE;
    return (int)(L->EvalTop - entry);
  }

  /* Error: false + message */
  L->EvalStack[entry] = MLUA_FALSE;
  if (L->ErrorMsg) {
    MLuaPush(L, MLuaStringNew(L, L->ErrorMsg, StrLen(L->ErrorMsg)));
  } else {
    MLuaPush(L, MLuaStringNew(L, "error", 5));
  }
  return 2;
}

/* ========================================================================== */
/* coroutine.yield                                                            */
/* ========================================================================== */

static int CoroYield(MLuaState *L) {
  /* On success the dispatch loop suspends after we return; the yield
   * values (our arguments) are handed over by MLuaThreadResume. */
  if (MLuaThreadYield(L, MLuaGetTop(L)) != MLUA_OK) {
    return -1; /* ErrorMsg set (outside coroutine / across C boundary) */
  }
  return 0;
}

/* ========================================================================== */
/* coroutine.status                                                           */
/* ========================================================================== */

static int CoroStatus(MLuaState *L) {
  MLuaValue thread = MLuaGetArg(L, 0);
  MLuaThreadStatus status = MLuaThreadGetStatus(thread);
  const char *statusStr;

  switch (status) {
  case THREAD_SUSPENDED:
    statusStr = "suspended";
    break;
  case THREAD_RUNNING:
    statusStr = "running";
    break;
  case THREAD_NORMAL:
    statusStr = "normal";
    break;
  case THREAD_DEAD:
  default:
    statusStr = "dead";
    break;
  }

  MLuaPush(L, MLuaStringNew(L, statusStr, StrLen(statusStr)));
  return 1;
}

/* ========================================================================== */
/* coroutine.running                                                          */
/* ========================================================================== */

static int CoroRunning(MLuaState *L) {
  /* Returns the running coroutine plus a boolean: true if main thread */
  if (L->CurrentThread) {
    MLuaPush(L, MakePtr(MLUA_OBJHEADER(L->CurrentThread)));
    MLuaPush(L, MLUA_FALSE);
  } else {
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, MLUA_TRUE);
  }
  return 2;
}

/* ========================================================================== */
/* coroutine.isyieldable                                                      */
/* ========================================================================== */

static int CoroIsyieldable(MLuaState *L) {
  Bool yieldable =
      L->CurrentThread && L->CCallDepth == L->CurrentThread->BaseCCalls;
  MLuaPush(L, yieldable ? MLUA_TRUE : MLUA_FALSE);
  return 1;
}

/* ========================================================================== */
/* coroutine.close                                                            */
/* ========================================================================== */

static int CoroClose(MLuaState *L) {
  MLuaValue thread = MLuaGetArg(L, 0);
  MLuaGCHeader *gch;
  MLuaThread *th;

  if (!MLuaIsThread(thread)) {
    MLuaPush(L, MLUA_FALSE);
    MLuaPush(L, MLuaStringNew(L, "not a coroutine", 15));
    return 2;
  }

  gch = (MLuaGCHeader *)GetPtr(thread);
  th = MLUA_THREAD(gch);

  if (th->Status == THREAD_RUNNING || th->Status == THREAD_NORMAL) {
    L->ErrorMsg = "cannot close a running coroutine";
    return -1;
  }

  /* Close any upvalues still open into the thread's locals */
  while (th->Ctx.OpenUpvalues) {
    MLuaUpvalue *uv = th->Ctx.OpenUpvalues;
    th->Ctx.OpenUpvalues = uv->Next;
    MLuaUpvalueClose(uv);
  }

  /* Drop all suspended state */
  th->Ctx.EvalTop = 0;
  th->Ctx.FrameTop = 0;
  th->Ctx.ArgsTop = 0;
  th->Ctx.LocalsTop = 0;

  if (th->Status == THREAD_DEAD && th->ErrorMsg) {
    /* Died with an error: report it (Lua 5.4 semantics) */
    MLuaPush(L, MLUA_FALSE);
    MLuaPush(L, MLuaStringNew(L, th->ErrorMsg, StrLen(th->ErrorMsg)));
    th->Status = THREAD_DEAD;
    return 2;
  }

  th->Status = THREAD_DEAD;
  MLuaPush(L, MLUA_TRUE);
  return 1;
}

/* ========================================================================== */
/* Library Registration                                                       */
/* ========================================================================== */

static const MLuaLibEntry CoroLibEntries[] = {{"close", CoroClose},
                                              {"create", CoroCreate},
                                              {"isyieldable", CoroIsyieldable},
                                              {"resume", CoroResume},
                                              {"running", CoroRunning},
                                              {"status", CoroStatus},
                                              {"yield", CoroYield},
                                              {NULL, NULL}};

void MLuaOpenCoroutine(MLuaState *L) {
  MLuaValue lib = MLuaNewLib(L, "coroutine");
  MLuaRegisterLib(L, lib, CoroLibEntries);
}
