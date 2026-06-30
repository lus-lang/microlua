/*
 * MicroLua - MLuaFunc.c
 * Function closures and upvalues
 */

#include "MLuaFunc.h"
#include "MLuaGC.h"

/* ========================================================================== */
/* Closure Creation                                                           */
/* ========================================================================== */

MLuaClosure *MLuaClosureNew(MLuaState *L, MLuaProto *proto, U8 numUpvalues) {
  Size size;
  MLuaGCHeader *header;
  MLuaClosure *cl;
  int i;

  if ((Size)numUpvalues > ((Size)-1 - sizeof(MLuaClosure)) /
                                sizeof(MLuaUpvalue *)) {
    return NULL;
  }
  size = sizeof(MLuaClosure) + (Size)numUpvalues * sizeof(MLuaUpvalue *);
  header = MLuaAllocObject(L, OBJTYPE_FUNCTION, size);

  if (!header) {
    return NULL;
  }

  cl = MLUA_CLOSURE(header);
  cl->Proto = proto;
  cl->Env = L->Globals; /* Default environment */
  cl->NumUpvalues = numUpvalues;

  /* Initialize upvalue pointers to NULL */
  for (i = 0; i < numUpvalues; i++) {
    MLUA_CLOSURE_UPVALS(cl)[i] = NULL;
  }

  return cl;
}

MLuaCClosure *MLuaCClosureNew(MLuaState *L, MLuaCFunction func,
                              U8 numUpvalues) {
  Size size;
  MLuaGCHeader *header;
  MLuaCClosure *cc;
  int i;

  if ((Size)numUpvalues > ((Size)-1 - sizeof(MLuaCClosure)) /
                                sizeof(MLuaValue)) {
    return NULL;
  }
  size = sizeof(MLuaCClosure) + (Size)numUpvalues * sizeof(MLuaValue);
  header = MLuaAllocObject(L, OBJTYPE_FUNCTION, size);

  if (!header) {
    return NULL;
  }

  cc = MLUA_CCLOSURE(header);
  cc->Func = func;
  cc->NumUpvalues = numUpvalues;
  cc->IsCClosure = 1;

  /* Initialize upvalues to nil */
  for (i = 0; i < numUpvalues; i++) {
    MLUA_CCLOSURE_UPVALS(cc)[i] = MLUA_NIL;
  }

  return cc;
}

/* ========================================================================== */
/* Upvalue Management                                                         */
/* ========================================================================== */

MLuaUpvalue *MLuaUpvalueNew(MLuaState *L, MLuaValue *slot) {
  MLuaGCHeader *header =
      MLuaAllocObject(L, OBJTYPE_UPVALUE, sizeof(MLuaUpvalue));
  MLuaUpvalue *uv;

  if (!header) {
    return NULL;
  }

  uv = MLUA_UPVALUE(header);
  uv->Location = slot;
  uv->Closed = MLUA_NIL;
  uv->Next = NULL;

  return uv;
}

void MLuaUpvalueClose(MLuaUpvalue *uv) {
  if (!uv)
    return;

  /* Copy value from stack to internal storage */
  uv->Closed = *uv->Location;
  /* Point to internal storage */
  uv->Location = &uv->Closed;
}

/*
 * Open upvalue list is stored per-state, sorted by stack slot address
 * (highest addresses first for efficient closing).
 */

MLuaUpvalue *MLuaFindUpvalue(MLuaState *L, MLuaValue *slot) {
  MLuaUpvalue *uv;
  MLuaUpvalue *prev = NULL;
  MLuaUpvalue *newUv;

  /* Search for existing upvalue pointing to this slot */
  for (uv = L->OpenUpvalues; uv != NULL; uv = uv->Next) {
    if (uv->Location == slot) {
      /* Found existing upvalue for this slot */
      return uv;
    }
    /* List is sorted by slot address (highest first) */
    if (uv->Location < slot) {
      break; /* Insert point found */
    }
    prev = uv;
  }

  /* Create new upvalue */
  newUv = MLuaUpvalueNew(L, slot);
  if (!newUv) {
    return NULL;
  }

  /* Insert into list at correct position */
  newUv->Next = uv;
  if (prev) {
    prev->Next = newUv;
  } else {
    L->OpenUpvalues = newUv;
  }

  return newUv;
}

void MLuaCloseUpvalues(MLuaState *L, MLuaValue *level) {
  /*
   * Close all open upvalues that point to stack slots at or above 'level'.
   * Since list is sorted by slot address (highest first), we can stop
   * when we hit a slot below level.
   */
  while (L->OpenUpvalues != NULL && L->OpenUpvalues->Location >= level) {
    MLuaUpvalue *uv = L->OpenUpvalues;
    L->OpenUpvalues = uv->Next;
    MLuaUpvalueClose(uv);
  }
}

/* ========================================================================== */
/* Closure Type Checking                                                      */
/* ========================================================================== */

/*
 * Check if a value is a Lua closure (not C closure).
 */
Bool MLuaIsLuaClosure(MLuaValue v) {
  MLuaClosure *cl;

  if (!IsPtr(v)) {
    return FALSE;
  }

  cl = (MLuaClosure *)GetPtr(v);
  /* Check if it's a function and not a C closure */
  if (MLUA_OBJTYPE(&cl->Header) != OBJTYPE_FUNCTION) {
    return FALSE;
  }

  /* C closures have IsCClosure flag at specific offset */
  /* For Lua closures, we check if Proto is non-null */
  return cl->Proto != NULL;
}

/*
 * Check if a value is a C function/closure.
 */
Bool MLuaIsCFunction(MLuaValue v) {
  MLuaCClosure *cc;

  if (!IsPtr(v)) {
    return FALSE;
  }

  cc = (MLuaCClosure *)GetPtr(v);
  if (MLUA_OBJTYPE(&cc->Header) != OBJTYPE_FUNCTION) {
    return FALSE;
  }

  return cc->IsCClosure != 0;
}
