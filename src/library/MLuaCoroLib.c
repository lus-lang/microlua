/*
 * MicroLua - MLuaCoroLib.c
 * Coroutine library implementation
 */

#include "MLuaCoroLib.h"
#include "../MLuaCore.h"
#include "../MLuaString.h"
#include "../MLuaThread.h"
#include "../MLuaVM.h"

/* ========================================================================== */
/* coroutine.create                                                           */
/* ========================================================================== */

static int CoroCreate(MLuaState *L) {
  MLuaValue func = MLuaGetStack(L, 1);
  MLuaValue thread = MLuaThreadNew(L, func);
  MLuaPush(L, thread);
  return 1;
}

/* ========================================================================== */
/* coroutine.resume                                                           */
/* ========================================================================== */

static int CoroResume(MLuaState *L) {
  MLuaValue thread = MLuaGetStack(L, 1);
  int top = MLuaGetTop(L);
  int nargs = top - 1; /* Arguments after the thread */
  int result = MLuaThreadResume(L, thread, nargs);

  if (result == MLUA_OK || result == MLUA_YIELD) {
    /* Push true + any results */
    MLuaPush(L, MLUA_TRUE);
    return 1 + MLuaGetTop(L); /* true + results */
  } else {
    /* Push false + error message */
    MLuaPush(L, MLUA_FALSE);
    if (L->ErrorMsg) {
      MLuaPush(L, MLuaStringNew(L, L->ErrorMsg, StrLen(L->ErrorMsg)));
    } else {
      MLuaPush(L, MLuaStringNew(L, "error", 5));
    }
    return 2;
  }
}

/* ========================================================================== */
/* coroutine.yield                                                            */
/* ========================================================================== */

static int CoroYield(MLuaState *L) {
  int nresults = MLuaGetTop(L);
  return MLuaThreadYield(L, nresults);
}

/* ========================================================================== */
/* coroutine.status                                                           */
/* ========================================================================== */

static int CoroStatus(MLuaState *L) {
  MLuaValue thread = MLuaGetStack(L, 1);
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
    /* We're in a coroutine - return it and false */
    MLuaPush(L, (MLuaValue)((UPtr)L->CurrentThread | TAG_PTR));
    MLuaPush(L, MLUA_FALSE);
  } else {
    /* We're in the main thread - return nil and true */
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, MLUA_TRUE);
  }
  return 2;
}

/* ========================================================================== */
/* coroutine.isyieldable                                                      */
/* ========================================================================== */

static int CoroIsyieldable(MLuaState *L) {
  /* Returns true if currently in a yieldable context (coroutine) */
  MLuaPush(L, L->InCoroutine ? MLUA_TRUE : MLUA_FALSE);
  return 1;
}

/* ========================================================================== */
/* coroutine.close                                                            */
/* ========================================================================== */

static int CoroClose(MLuaState *L) {
  MLuaValue thread = MLuaGetStack(L, 1);
  /* Mark the coroutine as dead */
  if (MLuaIsThread(thread)) {
    MLuaGCHeader *gch = (MLuaGCHeader *)GetPtr(thread);
    MLuaThread *th = MLUA_THREAD(gch);
    th->Status = THREAD_DEAD;
    MLuaPush(L, MLUA_TRUE);
  } else {
    MLuaPush(L, MLUA_FALSE);
    MLuaPush(L, MLuaStringNew(L, "not a coroutine", 15));
    return 2;
  }
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
