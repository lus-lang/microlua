/*
 * MicroLua - MLuaVM.h
 * Virtual Machine interpreter
 */

#ifndef MLUA_VM_H
#define MLUA_VM_H

#include "MLuaAlloc.h"
#include "MLuaCode.h"
#include "MLuaCore.h"
#include "MLuaFunc.h"
#include "MLuaTable.h"
#include "MLuaValue.h"

/* ========================================================================== */
/* VM Execution Status - See MLuaCore.h for MLuaStatus enum                   */
/* ========================================================================== */

/* ========================================================================== */
/* VM Stack Operations                                                        */
/* ========================================================================== */

/*
 * Push a value onto the stack.
 */
void MLuaPush(MLuaState *L, MLuaValue v);

/*
 * Pop a value from the stack.
 */
MLuaValue MLuaPop(MLuaState *L);

/*
 * Get stack value at absolute index (1-based from bottom).
 */
MLuaValue MLuaGetStack(MLuaState *L, int index);

/*
 * Set stack value at absolute index.
 */
void MLuaSetStack(MLuaState *L, int index, MLuaValue v);

/*
 * Get current stack top.
 */
int MLuaGetTop(MLuaState *L);

/*
 * Get the i-th argument (0-based) of the current C function call,
 * or nil if out of range. Arguments live in the Args window.
 */
MLuaValue MLuaGetArg(MLuaState *L, int index);

/*
 * Number of arguments passed to the current C function call.
 */
int MLuaGetArgCount(MLuaState *L);

/*
 * Re-enter the dispatch loop for a suspended context (coroutine resume).
 */
MLuaStatus MLuaRunSuspended(MLuaState *L);

/*
 * Set stack top (expand or shrink).
 */
void MLuaSetTop(MLuaState *L, int index);

/* ========================================================================== */
/* VM Execution                                                               */
/* ========================================================================== */

/*
 * Execute a Lua closure.
 * Results are left on the stack.
 */
MLuaStatus MLuaExecute(MLuaState *L, MLuaClosure *cl, int nargs, int nresults);

/*
 * Call a function on the stack.
 * The function and arguments should already be pushed.
 * @param nargs   Number of arguments
 * @param nresults Number of results wanted (MLUA_MULTRET for all)
 */
#define MLUA_MULTRET (-1)
MLuaStatus MLuaCall(MLuaState *L, int nargs, int nresults);

/*
 * Protected call - catches errors.
 */
MLuaStatus MLuaPCall(MLuaState *L, int nargs, int nresults, int errfunc);

#if MLUA_ENABLE_COMPILER
/*
 * Load and execute a source chunk.
 */
MLuaStatus MLuaDoString(MLuaState *L, const char *source, Size len,
                        const char *name);

/*
 * Load (compile) a chunk without executing.
 * On success, pushes the compiled function onto the stack and returns MLUA_OK.
 * On failure, pushes an error message and returns error code.
 */
MLuaStatus MLuaLoadString(MLuaState *L, const char *source, Size len,
                          const char *name);
#endif

/*
 * Load and execute a precompiled bytecode chunk.
 */
MLuaStatus MLuaDoBytecode(MLuaState *L, const char *data, Size len,
                          const char *name);

/*
 * Load a precompiled bytecode chunk without executing.
 * On success, pushes the function onto the stack.
 */
MLuaStatus MLuaLoadBytecode(MLuaState *L, const char *data, Size len,
                            const char *name);

/*
 * Load a buffer by inspecting its magic. Bytecode is always accepted; source
 * text requires MLUA_ENABLE_COMPILER.
 */
MLuaStatus MLuaLoadBuffer(MLuaState *L, const char *data, Size len,
                          const char *name);
MLuaStatus MLuaDoBuffer(MLuaState *L, const char *data, Size len,
                        const char *name);

/*
 * Get global variable.
 */
MLuaValue MLuaGetGlobal(MLuaState *L, const char *name);

/*
 * Set global variable.
 */
void MLuaSetGlobal(MLuaState *L, const char *name, MLuaValue value);

/* ========================================================================== */
/* Error Handling                                                             */
/* ========================================================================== */

/*
 * NOTE: MLuaRaise has been replaced by M_FAIL/VM_FAIL macros in MLuaError.h.
 * See SPEC.ERRORS.md for the Status-Return error handling pattern.
 */

/* ========================================================================== */
/* Arithmetic and Comparison                                                  */
/* ========================================================================== */

/*
 * Perform arithmetic operation: a op b
 */
MLuaValue MLuaArith(MLuaState *L, MLuaOpCode op, MLuaValue a, MLuaValue b);

/*
 * Compare two values: a op b
 */
Bool MLuaCompare(MLuaState *L, MLuaOpCode op, MLuaValue a, MLuaValue b);

/*
 * Get length of value.
 */
MLuaValue MLuaLen(MLuaState *L, MLuaValue v);

/*
 * Concatenate values.
 */
MLuaValue MLuaConcat(MLuaState *L, int count);

/* ========================================================================== */
/* C Function Registration                                                    */
/* ========================================================================== */

/*
 * Register a C function and return its light function value.
 * Returns MLUA_NIL if registration fails.
 */
MLuaValue MLuaRegisterCFunc(MLuaState *L, MLuaCFunction func);

/*
 * Register a C function as a global.
 */
void MLuaRegisterGlobal(MLuaState *L, const char *name, MLuaCFunction func);

/*
 * Create a library table and register it as a global.
 * Returns the library table value.
 */
MLuaValue MLuaNewLib(MLuaState *L, const char *name);

/*
 * Library entry for batch registration.
 */
typedef struct {
  const char *Name;
  MLuaCFunction Func;
} MLuaLibEntry;

/*
 * Register multiple functions into a library table.
 * The entries array must be terminated with {NULL, NULL}.
 */
void MLuaRegisterLib(MLuaState *L, MLuaValue lib, const MLuaLibEntry *entries);

/* ========================================================================== */
/* I/O Configuration                                                          */
/* ========================================================================== */

typedef void (*MLuaOutputFunc)(MLuaState *L, int kind, const char *msg,
                               Size len);
typedef MLuaValue (*MLuaRequireFunc)(MLuaState *L, const char *modname);

/*
 * Set the output callback. Enables print, io.write, error output.
 */
void MLuaSetOutput(MLuaState *L, MLuaOutputFunc func);

/*
 * Set the require callback. Enables require() function.
 */
void MLuaSetRequirer(MLuaState *L, MLuaRequireFunc func);

/*
 * Open all standard libraries.
 */
void MLuaOpenLibs(MLuaState *L);

#if MLUA_PROFILE_OPS
/*
 * Report per-opcode dispatch counts through the output callback, one
 * "NAME<tab>count" line per opcode that executed. Counts accumulate for the
 * process lifetime (across states).
 */
void MLuaDumpOpProfile(MLuaState *L);
#endif

#endif /* MLUA_VM_H */
