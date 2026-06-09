/*
 * MicroLua - MLuaFunc.h
 * Function closures and upvalues
 */

#ifndef MLUA_FUNC_H
#define MLUA_FUNC_H

#include "MLuaAlloc.h"
#include "MLuaCode.h"
#include "MLuaCore.h"
#include "MLuaValue.h"

/* ========================================================================== */
/* Upvalue                                                                    */
/* ========================================================================== */

/*
 * Upvalue structure - represents a closed-over variable.
 * When open: points to a stack slot.
 * When closed: holds the value directly.
 */
typedef struct MLuaUpvalue MLuaUpvalue;
struct MLuaUpvalue {
  MLuaGCHeader Header; /* GC header (type = OBJTYPE_UPVALUE) */
  MLuaValue *Location; /* Pointer to value (stack or &Closed) */
  MLuaValue Closed;    /* Storage when closed */
  MLuaUpvalue *Next;   /* Linked list of open upvalues */
};

/* Get upvalue from GC header */
#define MLUA_UPVALUE(gch) ((MLuaUpvalue *)(gch))

/* ========================================================================== */
/* Closure (Lua Function)                                                     */
/* ========================================================================== */

/*
 * Lua closure - a function prototype with captured upvalues.
 * The upvalue array follows the struct in memory.
 */
typedef struct MLuaClosure MLuaClosure;
struct MLuaClosure {
  MLuaGCHeader Header; /* GC header (type = OBJTYPE_FUNCTION) */
  MLuaProto *Proto;    /* Function prototype */
  MLuaValue Env;       /* Function environment (_ENV) */
  U8 NumUpvalues;      /* Number of upvalues */
  /* MLuaUpvalue* Upvalues[NumUpvalues] follows in memory */
};

/* Get closure from GC header */
#define MLUA_CLOSURE(gch) ((MLuaClosure *)(gch))

/* Get upvalue array for closure */
#define MLUA_CLOSURE_UPVALS(cl)                                                \
  ((MLuaUpvalue **)((U8 *)(cl) + sizeof(MLuaClosure)))

/* ========================================================================== */
/* C Function                                                                 */
/* ========================================================================== */

/* C function signature: takes state, returns number of results */
typedef int (*MLuaCFunction)(MLuaState *L);

/*
 * C closure - a C function with optional upvalues stored as MLuaValues.
 */
typedef struct MLuaCClosure MLuaCClosure;
struct MLuaCClosure {
  MLuaGCHeader Header; /* GC header (type = OBJTYPE_FUNCTION) */
  MLuaCFunction Func;  /* C function pointer */
  U8 NumUpvalues;      /* Number of upvalues */
  U8 IsCClosure;       /* Flag: 1 = C closure, 0 = Lua closure */
  /* MLuaValue Upvalues[NumUpvalues] follows in memory */
};

#define MLUA_CCLOSURE(gch) ((MLuaCClosure *)(gch))
#define MLUA_CCLOSURE_UPVALS(cc)                                               \
  ((MLuaValue *)((U8 *)(cc) + sizeof(MLuaCClosure)))

/* (Call frames are MLuaFrame in MLuaAlloc.h — the VM is frame-iterative.) */

/* ========================================================================== */
/* Closure API                                                                */
/* ========================================================================== */

/*
 * Create a new Lua closure from a prototype.
 */
MLuaClosure *MLuaClosureNew(MLuaState *L, MLuaProto *proto, U8 numUpvalues);

/*
 * Create a new C closure.
 */
MLuaCClosure *MLuaCClosureNew(MLuaState *L, MLuaCFunction func, U8 numUpvalues);

/*
 * Create a new open upvalue pointing to a stack slot.
 */
MLuaUpvalue *MLuaUpvalueNew(MLuaState *L, MLuaValue *slot);

/*
 * Close an upvalue - copy value from stack to internal storage.
 */
void MLuaUpvalueClose(MLuaUpvalue *uv);

/*
 * Find an existing open upvalue for a stack slot, or create one.
 */
MLuaUpvalue *MLuaFindUpvalue(MLuaState *L, MLuaValue *slot);

/*
 * Close all upvalues at or above the given stack level.
 */
void MLuaCloseUpvalues(MLuaState *L, MLuaValue *level);

#endif /* MLUA_FUNC_H */
