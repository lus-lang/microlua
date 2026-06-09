/*
 * MicroLua - MLuaVM.c
 * Virtual Machine interpreter (stack-based, variable-length bytecode)
 */

#include "MLuaVM.h"
#include "MLuaCode.h"
#include "MLuaConvert.h"
#include "MLuaDebug.h"
#include "MLuaError.h"
#include "MLuaGC.h"
#include "MLuaParse.h"
#include "MLuaString.h"

#include <stdio.h> /* For debug output */

/* ========================================================================== */
/* VM Error Handling with Line Info                                           */
/* ========================================================================== */

/*
 * VM_FAIL: Like M_FAIL but captures line info from current execution context.
 * Must be used inside MLuaExecute where proto, pc are in scope.
 * Computes PC offset and looks up line number before returning error.
 * Also builds stacktrace for multi-level error reporting.
 */
/*
 * VM_UNWIND: Frame cleanup shared by every error exit from MLuaExecute.
 * Closes any upvalues still pointing at this (or a deeper, abandoned) frame
 * so they cannot alias slots reused by later calls, then restores the
 * caller's locals window and call stack.
 */
#define VM_UNWIND(L)                                                           \
  do {                                                                         \
    if ((L)->OpenUpvalues) {                                                   \
      MLuaCloseUpvalues((L), &(L)->Locals[(L)->LocalsBase]);                   \
    }                                                                          \
    (L)->LocalsBase = savedLocalsBase;                                         \
    (L)->LocalsTop = savedLocalsTop;                                           \
    (L)->CallStackTop = savedCallStackTop;                                     \
  } while (0)

#define VM_FAIL(L, code, msg)                                                  \
  do {                                                                         \
    Size _pc = (Size)(pc - proto->Code);                                       \
    (L)->ErrorMsg = (msg);                                                     \
    (L)->ErrorLine = MLuaGetLine(proto, _pc);                                  \
    (L)->StackTrace = BuildStackTrace(L, proto, _pc);                          \
    VM_UNWIND(L);                                                              \
    return (code);                                                             \
  } while (0)

/*
 * VM_TRY: Like M_TRY but captures line info on error.
 * Used when calling helper functions that set ErrorMsg but not ErrorLine.
 */
#define VM_TRY(expr)                                                           \
  do {                                                                         \
    MLuaStatus _s = (expr);                                                    \
    if (_s != MLUA_OK) {                                                       \
      Size _pc = (Size)(pc - proto->Code);                                     \
      L->ErrorLine = MLuaGetLine(proto, _pc);                                  \
      L->StackTrace = BuildStackTrace(L, proto, _pc);                          \
      VM_UNWIND(L);                                                            \
      return _s;                                                               \
    }                                                                          \
  } while (0)

/*
 * VM_CHECK: Check if a helper function indicated error (returned sentinel + set
 * ErrorMsg). Captures line info and returns error.
 */
#define VM_CHECK_NIL(result)                                                   \
  do {                                                                         \
    if (IsNil(result) && L->ErrorMsg) {                                        \
      Size _pc = (Size)(pc - proto->Code);                                     \
      L->ErrorLine = MLuaGetLine(proto, _pc);                                  \
      L->StackTrace = BuildStackTrace(L, proto, _pc);                          \
      VM_UNWIND(L);                                                            \
      return MLUA_ERR_RUNTIME;                                                 \
    }                                                                          \
  } while (0)

/* ========================================================================== */
/* EvalStack Operations (Three-Array Architecture)                            */
/* ========================================================================== */

void MLuaPush(MLuaState *L, MLuaValue v) {
  /* Push to EvalStack */
  if (L->EvalTop >= L->EvalStackSize) {
    /* Stack overflow - should expand or error */
    return;
  }
  L->EvalStack[L->EvalTop++] = v;
}

MLuaValue MLuaPop(MLuaState *L) {
  /* Pop from EvalStack */
  if (L->EvalTop == 0) {
    return MLUA_NIL;
  }
  return L->EvalStack[--L->EvalTop];
}

/* Get argument from Args array (for C function calls) */
MLuaValue MLuaGetArg(MLuaState *L, int index) {
  if (index >= 0 && (Size)index < L->ArgsCount) {
    return L->Args[index];
  }
  return MLUA_NIL;
}

int MLuaGetArgCount(MLuaState *L) { return (int)L->ArgsCount; }

/* Push result to EvalStack (for C function returns) */
void MLuaPushResult(MLuaState *L, MLuaValue v) { MLuaPush(L, v); }

/* Legacy stack access for compatibility (now uses EvalStack) */
MLuaValue MLuaGetStack(MLuaState *L, int index) {
  if (index > 0) {
    /* Positive: relative from start of Args (for C functions) */
    return MLuaGetArg(L, index - 1);
  } else if (index < 0) {
    /* Negative: relative from EvalTop */
    int absIdx = (int)L->EvalTop + index;
    if (absIdx >= 0 && (Size)absIdx < L->EvalTop) {
      return L->EvalStack[absIdx];
    }
  }
  return MLUA_NIL;
}

void MLuaSetStack(MLuaState *L, int index, MLuaValue v) {
  if (index > 0) {
    /* Positive: set in Args */
    if ((Size)(index - 1) < L->ArgsSize) {
      L->Args[index - 1] = v;
    }
  } else if (index < 0) {
    /* Negative: relative from EvalTop */
    int absIdx = (int)L->EvalTop + index;
    if (absIdx >= 0 && (Size)absIdx < L->EvalTop) {
      L->EvalStack[absIdx] = v;
    }
  }
}

int MLuaGetTop(MLuaState *L) { return (int)L->ArgsCount; }

void MLuaSetTop(MLuaState *L, int index) {
  if (index >= 0) {
    /* Expand or shrink EvalStack */
    while ((Size)index > L->EvalTop && L->EvalTop < L->EvalStackSize) {
      L->EvalStack[L->EvalTop++] = MLUA_NIL;
    }
    if ((Size)index < L->EvalTop) {
      L->EvalTop = (Size)index;
    }
  } else {
    /* Relative shrink */
    int newTop = (int)L->EvalTop + index + 1;
    if (newTop >= 0) {
      L->EvalTop = (Size)newTop;
    }
  }
}

/* ========================================================================== */
/* Globals                                                                    */
/* ========================================================================== */

MLuaValue MLuaGetGlobal(MLuaState *L, const char *name) {
  MLuaValue key;

  if (IsNil(L->Globals)) {
    return MLUA_NIL;
  }

  key = MLuaStringNew(L, name, StrLen(name));
  return MLuaTableGet(L, L->Globals, key);
}

void MLuaSetGlobal(MLuaState *L, const char *name, MLuaValue value) {
  MLuaValue key;

  if (IsNil(L->Globals)) {
    return;
  }

  key = MLuaStringNew(L, name, StrLen(name));
  MLuaTableSet(L, L->Globals, key, value);
}

/* ========================================================================== */
/* Error Handling                                                             */
/* ========================================================================== */
/* Error Message Infrastructure                                               */
/* ========================================================================== */

#include <stdarg.h>

/* Static buffer for error messages - may be used for formatted errors later */
static char ErrorMsgBuffer[512];

/* Static buffer for stacktrace */
static char StackTraceBuffer[2048];

/*
 * Build a stacktrace string from the current call stack.
 * Returns pointer to static buffer with formatted trace.
 */
static const char *BuildStackTrace(MLuaState *L, MLuaProto *currentProto,
                                   Size currentPC) {
  char *p = StackTraceBuffer;
  char *end = StackTraceBuffer + sizeof(StackTraceBuffer) - 1;
  Size i;

  /* Start with current frame (where error occurred) */
  if (currentProto) {
    Size line = MLuaGetLine(currentProto, currentPC);
    const char *src = "?";
    Size srcLen = 1;
    const char *label =
        (L->CallStackTop == 1) ? "in main chunk" : "in function";

    /* Get source name if available */
    if (IsString(currentProto->Source)) {
      src = MLuaStringData(currentProto->Source);
      srcLen = MLuaStringLen(currentProto->Source);
    } else if (IsShortStr(currentProto->Source)) {
      src = MLuaStringData(currentProto->Source);
      srcLen = MLuaShortStrLen(currentProto->Source);
    }

    /* Format: "source:line: in function" */
    if (p < end) {
      int n = snprintf(p, (size_t)(end - p), "\t%.*s:%llu: %s\n", (int)srcLen,
                       src, (unsigned long long)line, label);
      if (n > 0)
        p += n;
    }
  }

  /* Walk call stack (most recent first, but we show bottom-up) */
  for (i = L->CallStackTop; i > 0; i--) {
    MLuaProto *proto = (MLuaProto *)L->CallStack[i - 1].Proto;
    Size framePC = L->CallStack[i - 1].PC;
    Size line;
    const char *src = "?";
    Size srcLen = 1;
    const char *label = (i == 1) ? "in main chunk" : "in function";

    if (!proto)
      continue;

    line = MLuaGetLine(proto, framePC);

    /* Get source name if available */
    if (IsString(proto->Source)) {
      src = MLuaStringData(proto->Source);
      srcLen = MLuaStringLen(proto->Source);
    } else if (IsShortStr(proto->Source)) {
      src = MLuaStringData(proto->Source);
      srcLen = MLuaShortStrLen(proto->Source);
    }

    if (p < end) {
      int n = snprintf(p, (size_t)(end - p), "\t%.*s:%llu: %s\n", (int)srcLen,
                       src, (unsigned long long)line, label);
      if (n > 0)
        p += n;
    }
  }

  *p = '\0';
  return StackTraceBuffer;
}

/* ========================================================================== */
/* Arithmetic Helpers                                                         */
/* ========================================================================== */

static double ToNumber(MLuaValue v) { return MLuaToNumber(v); }

static MLuaValue MakeNumber(MLuaState *L, double n) {
  return MLuaMakeNumber(L,
                        n); /* Use proper implementation with heap allocation */
}

MLuaValue MLuaArith(MLuaState *L, MLuaOpCode op, MLuaValue a, MLuaValue b) {
  /* Type check: both operands must be numbers (except for UNM which only uses
   * a) */
  Bool aIsNum = IsInt(a) || MLuaIsNumber(a);
  Bool bIsNum = (op == OP_UNM) || IsInt(b) || MLuaIsNumber(b);

  if (!aIsNum) {
    L->ErrorMsg = "attempt to perform arithmetic on a non-number value";
    return MLUA_NIL;
  }

  if (!bIsNum) {
    L->ErrorMsg = "attempt to perform arithmetic on a non-number value";
    return MLUA_NIL;
  }

  /* Integer fast-path: if both operands are integers, stay in integer domain */
  if (IsInt(a) && (op == OP_UNM || IsInt(b))) {
    I32 ia = GetInt(a);
    I32 ib = IsInt(b) ? GetInt(b) : 0;
    switch (op) {
    case OP_ADD:
      return MakeInt(ia + ib);
    case OP_SUB:
      return MakeInt(ia - ib);
    case OP_MUL:
      return MakeInt(ia * ib);
    case OP_IDIV:
      return ib != 0 ? MakeInt(ia / ib) : MakeInt(0);
    case OP_MOD:
      return ib != 0 ? MakeInt(ia % ib) : MakeInt(0);
    case OP_UNM:
      return MakeInt(-ia);
    default:
      break; /* Fall through to float path */
    }
  }

  /* Float path for DIV, POW, or when operands are floats */
  double na = ToNumber(a);
  double nb = ToNumber(b);

  switch (op) {
  case OP_ADD:
    return MakeNumber(L, na + nb);
  case OP_SUB:
    return MakeNumber(L, na - nb);
  case OP_MUL:
    return MakeNumber(L, na * nb);
  case OP_DIV:
    return MakeNumber(L, nb != 0 ? na / nb : 0);
  case OP_IDIV:
    return MakeNumber(L, nb != 0 ? (double)(I32)(na / nb) : 0);
  case OP_MOD:
    return MakeNumber(L, nb != 0 ? na - (I32)(na / nb) * nb : 0);
  case OP_POW: {
    /* Simple integer power */
    double result = 1.0;
    int exp = (int)nb;
    int i;
    if (exp >= 0) {
      for (i = 0; i < exp; i++)
        result *= na;
    } else {
      for (i = 0; i < -exp; i++)
        result /= na;
    }
    return MakeNumber(L, result);
  }
  case OP_UNM:
    return MakeNumber(L, -na);
  default:
    return MLUA_NIL;
  }
}

Bool MLuaCompare(MLuaState *L, MLuaOpCode op, MLuaValue a, MLuaValue b) {
  Bool aStr;
  Bool bStr;

  UNUSED(L);

  if (IsInt(a) && IsInt(b)) {
    I32 ia = GetInt(a);
    I32 ib = GetInt(b);
    switch (op) {
    case OP_EQ:
      return ia == ib;
    case OP_NEQ:
      return ia != ib;
    case OP_LT:
      return ia < ib;
    case OP_LE:
      return ia <= ib;
    default:
      return FALSE;
    }
  }

  /* Value equality (strings are interned: pointer equality is value
   * equality, and short strings compare by their packed bits) */
  if (op == OP_EQ)
    return a == b;
  if (op == OP_NEQ)
    return a != b;

  /* Lexicographic byte comparison for two strings */
  aStr = IsString(a) || IsShortStr(a);
  bStr = IsString(b) || IsShortStr(b);
  if (aStr && bStr) {
    int cmp = MLuaStringCompare(a, b);
    switch (op) {
    case OP_LT:
      return cmp < 0;
    case OP_LE:
      return cmp <= 0;
    default:
      return FALSE;
    }
  }

  /* Numeric comparison for numbers */
  {
    double na = ToNumber(a);
    double nb = ToNumber(b);
    switch (op) {
    case OP_LT:
      return na < nb;
    case OP_LE:
      return na <= nb;
    default:
      return FALSE;
    }
  }
}

MLuaValue MLuaLen(MLuaState *L, MLuaValue v) {
  if (IsString(v)) {
    return MakeInt((I32)MLuaStringLen(v));
  }
  if (IsShortStr(v)) {
    return MakeInt((I32)MLuaShortStrLen(v));
  }
  if (IsPtr(v)) {
    MLuaGCHeader *h = (MLuaGCHeader *)GetPtr(v);
    if (MLUA_OBJTYPE(h) == OBJTYPE_TABLE) {
      return MakeInt((I32)MLuaTableLen(v));
    }
  }
  /* Invalid type for length - set error message */
  L->ErrorMsg = IsNil(v) ? "attempt to get length of a nil value"
                         : "attempt to get length of a non-string/table value";
  return MLUA_NIL; /* Sentinel for error */
}

MLuaValue MLuaConcat(MLuaState *L, int count) {
  /* Concatenate 'count' values from top of stack into a single string */
  Size totalLen = 0;
  U8 *buffer;
  U8 *p;
  int i;

  if (count <= 0) {
    return MLuaStringNew(L, "", 0);
  }

  /* First pass: Type check and calculate total length */
  for (i = 0; i < count; i++) {
    MLuaValue v = L->EvalStack[L->EvalTop - count + i];
    if (IsString(v)) {
      totalLen += MLuaStringLen(v);
    } else if (IsShortStr(v)) {
      totalLen += MLuaShortStrLen(v);
    } else if (IsInt(v) || MLuaIsNumber(v)) {
      /* Estimate max digits for number */
      totalLen += 24;
    } else {
      /* Invalid type for concatenation */
      L->ErrorMsg = IsNil(v) ? "attempt to concatenate a nil value"
                             : "attempt to concatenate a non-string value";
      return MLUA_NIL;
    }
  }

  /* Allocate buffer */
  buffer = (U8 *)MLuaAlloc(L, totalLen + 1);
  if (!buffer) {
    return MLUA_NIL;
  }

  /* Copy strings into buffer */
  p = buffer;
  for (i = 0; i < count; i++) {
    MLuaValue v = L->EvalStack[L->EvalTop - count + i];
    if (IsString(v)) {
      const char *s = MLuaStringData(v);
      Size len = MLuaStringLen(v);
      MemCpy(p, s, len);
      p += len;
    } else if (IsShortStr(v)) {
      Size len = MLuaShortStrLen(v);
      if (len >= 1)
        *p++ = (U8)GetShortStrChar0(v);
      if (len >= 2)
        *p++ = (U8)GetShortStrChar1(v);
      if (len >= 3)
        *p++ = (U8)GetShortStrChar2(v);
    } else if (IsInt(v)) {
      /* Simple integer to string */
      I32 n = GetInt(v);
      char numBuf[16];
      char *np = numBuf + sizeof(numBuf) - 1;
      Bool neg = FALSE;
      Size numLen;
      if (n < 0) {
        neg = TRUE;
        n = -n;
      }
      *np = '\0';
      do {
        *--np = '0' + (n % 10);
        n /= 10;
      } while (n > 0);
      if (neg)
        *--np = '-';
      numLen = (numBuf + sizeof(numBuf) - 1) - np;
      MemCpy(p, np, numLen);
      p += numLen;
    }
  }
  *p = '\0';

  /* Create interned string */
  {
    MLuaValue result =
        MLuaStringNew(L, (const char *)buffer, (Size)(p - buffer));
    /* Note: buffer was allocated on heap - in a real impl we'd free it or use
     * stack */
    return result;
  }
}

/* ========================================================================== */
/* VM Interpreter Loop                                                        */
/* ========================================================================== */

#define READ_BYTE() (*pc++)
#define READ_U16() (pc += 2, (U16)((pc[-2]) | ((pc[-1]) << 8)))
#define READ_I16() ((I16)READ_U16())

/* Three-array architecture macros */
#define EVAL_PUSH(v)                                                           \
  do {                                                                         \
    L->EvalStack[L->EvalTop++] = (v);                                          \
  } while (0)
#define EVAL_POP() (L->EvalStack[--L->EvalTop])
#define EVAL_TOP() (L->EvalStack[L->EvalTop - 1])
#define EVAL_PEEK(n) (L->EvalStack[L->EvalTop - 1 - (n)])

#define LOCAL_GET(slot) (L->Locals[L->LocalsBase + (slot)])
#define LOCAL_SET(slot, v) (L->Locals[L->LocalsBase + (slot)] = (v))

/* Stack macros now redirect to EvalStack */
#define STACK_PUSH(v) EVAL_PUSH(v)
#define STACK_POP() EVAL_POP()
#define STACK_TOP() EVAL_TOP()

MLuaStatus MLuaExecute(MLuaState *L, MLuaClosure *cl, int nargs, int nresults) {
  const U8 *pc;
  MLuaProto *proto;
  Size savedLocalsBase;
  Size savedLocalsTop;
  Size savedCallStackTop;
  int i;

  UNUSED(nresults);

  if (!cl || !cl->Proto) {
    L->ErrorMsg = "attempt to execute an invalid closure";
    return MLUA_ERRRUN;
  }

  proto = cl->Proto;
  pc = proto->Code;

  /*
   * Three-array architecture:
   * - Arguments come from Args array (set by caller before CALL)
   * - Locals are stored in Locals array starting at LocalsBase
   * - Expression values go on EvalStack
   *
   * Every frame is allocated at LocalsTop, the first free slot above ALL
   * live frames. This holds for OP_CALL recursion and equally for calls
   * entering through the C boundary (pcall, require, the REPL): they must
   * not overwrite the locals of the frame that happens to be current.
   */
  savedLocalsBase = L->LocalsBase;
  savedLocalsTop = L->LocalsTop;
  savedCallStackTop = L->CallStackTop;

  /* Push call frame for stacktrace (if room) */
  if (L->CallStackTop < 64) {
    L->CallStack[L->CallStackTop].Proto = proto;
    L->CallStack[L->CallStackTop].PC = 0; /* Will be updated on call/error */
    L->CallStackTop++;
  }

  /* Set up new Locals frame for this function */
  {
    Size newBase = L->LocalsTop; /* First free slot above live frames */
    Size needed = newBase + proto->NumLocals;

    if (needed >= L->LocalsSize) {
      L->CallStackTop = savedCallStackTop; /* Cleanup on error */
      L->ErrorMsg = "stack overflow (too many locals)";
      return MLUA_ERRRUN;
    }

    /* Copy arguments from Args to Locals (first nargs slots) */
    for (i = 0; i < nargs && (Size)i < proto->NumLocals; i++) {
      L->Locals[newBase + i] = (Size)i < L->ArgsCount ? L->Args[i] : MLUA_NIL;
    }

    /* Initialize remaining locals to nil */
    for (; (Size)i < proto->NumLocals; i++) {
      L->Locals[newBase + i] = MLUA_NIL;
    }

    L->LocalsBase = newBase;
    L->LocalsTop = needed;
  }

  for (;;) {
    U8 op = READ_BYTE();

    switch (op) {
    case OP_NOP:
      break;

    case OP_LOADNIL:
      STACK_PUSH(MLUA_NIL);
      break;

    case OP_LOADFALSE:
      STACK_PUSH(MLUA_FALSE);
      break;

    case OP_LOADTRUE:
      STACK_PUSH(MLUA_TRUE);
      break;

    case OP_LOADINT: {
      I8 val = (I8)READ_BYTE();
      STACK_PUSH(MakeInt(val));
      break;
    }

    case OP_LOADK: {
      U8 k = READ_BYTE();
      if (k < proto->ConstantsSize) {
        STACK_PUSH(proto->Constants[k]);
      } else {
        STACK_PUSH(MLUA_NIL);
      }
      break;
    }

    case OP_GETLOCAL: {
      U8 slot = READ_BYTE();
      MLuaValue val = LOCAL_GET(slot);
      STACK_PUSH(val);
      break;
    }

    case OP_SETLOCAL: {
      U8 slot = READ_BYTE();
      MLuaValue val = STACK_POP();
      LOCAL_SET(slot, val);
      break;
    }

    case OP_GETGLOBAL: {
      /* Stack-based: pops key, pushes _G[key] */
      MLuaValue key = STACK_POP();
      MLuaValue val = MLuaTableGet(L, L->Globals, key);
      MLuaDebugPrint("OP_GETGLOBAL val_type=%llx\n", (unsigned long long)val);
      STACK_PUSH(val);
      break;
    }

    case OP_SETGLOBAL: {
      /* Stack-based: pops key and val, sets _G[key]=val */
      MLuaValue val = STACK_POP();
      MLuaValue key = STACK_POP();
      MLuaTableSet(L, L->Globals, key, val);
      break;
    }

    case OP_POP: {
      U8 count = READ_BYTE();
      L->EvalTop -= count;
      break;
    }

    case OP_DUP: {
      /* Duplicate top of stack */
      MLuaValue val = STACK_TOP();
      STACK_PUSH(val);
      break;
    }

    case OP_SWAP: {
      /* Swap top two values */
      MLuaValue a = STACK_POP();
      MLuaValue b = STACK_POP();
      STACK_PUSH(a);
      STACK_PUSH(b);
      break;
    }

    case OP_NEWTABLE: {
      MLuaValue t = MLuaTableNew(L);
      STACK_PUSH(t);
      break;
    }

    case OP_GETTABLE: {
      MLuaValue key = STACK_POP();
      MLuaValue tbl = STACK_POP();
      MLuaValue result;
      VM_TRY(MLuaTableGetSafe(L, tbl, key, &result));
      STACK_PUSH(result);
      break;
    }

    case OP_SETTABLE: {
      MLuaValue val = STACK_POP();
      MLuaValue key = STACK_POP();
      MLuaValue tbl = STACK_TOP();
      VM_TRY(MLuaTableSetSafe(L, tbl, key, val));
      break;
    }

    case OP_APPEND: {
      MLuaValue val = STACK_POP();
      MLuaValue tbl = STACK_TOP();
      MLuaTableAppend(L, tbl, val);
      break;
    }

    case OP_NOT: {
      MLuaValue v = STACK_POP();
      STACK_PUSH(IsTruthy(v) ? MLUA_FALSE : MLUA_TRUE);
      break;
    }

    case OP_EQ: {
      MLuaValue b = STACK_POP();
      MLuaValue a = STACK_POP();
      STACK_PUSH(MLuaCompare(L, OP_EQ, a, b) ? MLUA_TRUE : MLUA_FALSE);
      break;
    }

    case OP_NEQ: {
      MLuaValue b = STACK_POP();
      MLuaValue a = STACK_POP();
      STACK_PUSH(MLuaCompare(L, OP_NEQ, a, b) ? MLUA_TRUE : MLUA_FALSE);
      break;
    }

    case OP_LT: {
      MLuaValue b = STACK_POP();
      MLuaValue a = STACK_POP();
      STACK_PUSH(MLuaCompare(L, OP_LT, a, b) ? MLUA_TRUE : MLUA_FALSE);
      break;
    }

    case OP_LE: {
      MLuaValue b = STACK_POP();
      MLuaValue a = STACK_POP();
      STACK_PUSH(MLuaCompare(L, OP_LE, a, b) ? MLUA_TRUE : MLUA_FALSE);
      break;
    }

    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_POW: {
      MLuaValue b = STACK_POP();
      MLuaValue a = STACK_POP();
      MLuaValue result = MLuaArith(L, (MLuaOpCode)op, a, b);
      VM_CHECK_NIL(result); /* Type error in arithmetic */
      STACK_PUSH(result);
      break;
    }

    case OP_UNM: {
      MLuaValue a = STACK_POP();
      MLuaValue result = MLuaArith(L, OP_UNM, a, MLUA_NIL);
      VM_CHECK_NIL(result); /* Type error in unary minus */
      STACK_PUSH(result);
      break;
    }

    case OP_LEN: {
      MLuaValue v = STACK_POP();
      MLuaValue len = MLuaLen(L, v);
      VM_CHECK_NIL(len);
      STACK_PUSH(len);
      break;
    }

    case OP_CONCAT: {
      U8 count = READ_BYTE();
      MLuaValue result = MLuaConcat(L, count);
      VM_CHECK_NIL(result);
      L->EvalTop -= count;
      STACK_PUSH(result);
      break;
    }

    case OP_JMP: {
      I8 offset = (I8)READ_BYTE();
      pc += offset;
      break;
    }

    case OP_JMPF: {
      I8 offset = (I8)READ_BYTE();
      MLuaValue v = STACK_POP();
      if (!IsTruthy(v)) {
        pc += offset;
      }
      break;
    }

    case OP_JMPT: {
      I8 offset = (I8)READ_BYTE();
      MLuaValue v = STACK_POP();
      if (IsTruthy(v)) {
        pc += offset;
      }
      break;
    }

    case OP_LOOP: {
      U8 offset = READ_BYTE();
      pc -= offset;
      break;
    }

    case OP_JMP_S: {
      /* Long forward jump: signed offset pushed via constant */
      MLuaValue v = STACK_POP();
      pc += GetInt(v);
      break;
    }

    case OP_LOOP_S: {
      /* Long backward jump: unsigned distance pushed via constant */
      MLuaValue v = STACK_POP();
      pc -= GetInt(v);
      break;
    }

    case OP_NLOOP_PREP: {
      /*
       * NLOOP_PREP: Numeric for-loop preparation (SPEC.FORLOOP.md).
       * Format: [OP_NLOOP_PREP] [u8 Base_Index] (2 Bytes)
       * Stack: [Body_Target, Exit_Target, Step, Limit, Start] (5 items, TOS
       * first)
       *
       * 1. Pop 5 values from EvalStack
       * 2. Store: Locals[Base]=Start, [Base+1]=Limit, [Base+2]=Step,
       * [Base+3]=Body_Target
       * 3. If (Step>0 && Start>Limit) || (Step<0 && Start<Limit): Jump to
       * Exit_Target
       * 4. Else: Push Start to EvalStack, fallthrough
       */
      U8 base = READ_BYTE();

      /* Pop values from EvalStack (TOS first, so reverse order) */
      MLuaValue bodyTarget = STACK_POP();
      MLuaValue exitTarget = STACK_POP();
      MLuaValue stepVal = STACK_POP();
      MLuaValue limitVal = STACK_POP();
      MLuaValue startVal = STACK_POP();

      I32 start = GetInt(startVal);
      I32 limit = GetInt(limitVal);
      I32 step = GetInt(stepVal);
      I32 bodyPC = GetInt(bodyTarget);
      I32 exitPC = GetInt(exitTarget);

      /* Store to shadow locals */
      LOCAL_SET(base, startVal);     /* Index */
      LOCAL_SET(base + 1, limitVal); /* Limit */
      LOCAL_SET(base + 2, stepVal);  /* Step */
      LOCAL_SET(
          base + 3,
          MakeInt((I32)(pc - proto->Code))); /* Current PC for body reference */

      /* Pre-loop condition check */
      if ((step > 0 && start > limit) || (step < 0 && start < limit)) {
        /* Loop doesn't run - jump to Exit_Target (absolute address in bytecode)
         */
        pc = proto->Code + exitPC;
      } else {
        /* Loop runs - push Start for user 'i' assignment, jump to Body */
        STACK_PUSH(startVal);
        /* Note: Body follows immediately, or we could jump to bodyPC */
        (void)bodyPC; /* Body is inline, no jump needed */
      }
      break;
    }

    case OP_NLOOP_STEP: {
      /*
       * NLOOP_STEP: Numeric for-loop step (SPEC.FORLOOP.md).
       * Format: [OP_NLOOP_STEP] [u8 Base_Index] (2 Bytes)
       * Stack: None (empty)
       *
       * 1. Load Index, Limit, Step from Locals[Base..Base+2]
       * 2. Index = Index + Step
       * 3. Store Index back to Locals[Base]
       * 4. If (Step>0 && Index<=Limit) || (Step<0 && Index>=Limit):
       *    Push Index, Load Body_Target from Locals[Base+3], Jump
       * 5. Else: Fallthrough (loop ends)
       */
      U8 base = READ_BYTE();

      /* Load loop state from shadow locals */
      I32 idx = GetInt(LOCAL_GET(base));
      I32 limit = GetInt(LOCAL_GET(base + 1));
      I32 step = GetInt(LOCAL_GET(base + 2));
      I32 bodyPC = GetInt(LOCAL_GET(base + 3)); /* Stored body target PC */

      /* Increment index */
      idx += step;
      LOCAL_SET(base, MakeInt(idx));

      /* Boundary check */
      if ((step > 0 && idx <= limit) || (step < 0 && idx >= limit)) {
        /* Continue loop - push new index, jump to body */
        STACK_PUSH(MakeInt(idx));
        pc = proto->Code + bodyPC;
      }
      /* Else: loop ends, fallthrough */
      break;
    }

    case OP_GLOOP_SETUP: {
      /*
       * GLOOP_SETUP: Generic for-loop setup (SPEC.FORLOOP.md).
       * Format: [OP_GLOOP_SETUP] [u8 Base_Index] (2 Bytes)
       * Stack: [Body_Target, Func, State, Control] (4 items, TOS first)
       *
       * 1. Pop 4 values from EvalStack
       * 2. Store: Locals[Base]=Func, [Base+1]=State, [Base+2]=Control,
       * [Base+3]=Body_Target
       * 3. Fallthrough to loop head
       */
      U8 base = READ_BYTE();

      /* Pop values from EvalStack (TOS first) */
      MLuaValue bodyTarget = STACK_POP();
      MLuaValue iterFunc = STACK_POP();
      MLuaValue state = STACK_POP();
      MLuaValue ctrl = STACK_POP();

      /* Store to shadow locals */
      LOCAL_SET(base, iterFunc);       /* Func */
      LOCAL_SET(base + 1, state);      /* State */
      LOCAL_SET(base + 2, ctrl);       /* Control */
      LOCAL_SET(base + 3, bodyTarget); /* Body_Target */

      /* Fallthrough to loop head */
      break;
    }

    case OP_GLOOP_CALL: {
      /*
       * GLOOP_CALL: Generic for-loop call prep (SPEC.FORLOOP.md).
       * Format: [OP_GLOOP_CALL] [u8 Base_Index] (2 Bytes)
       * Stack: None -> [Func, State, Control] (pushed for CALL)
       *
       * 1. Push Locals[Base] (Func)
       * 2. Push Locals[Base+1] (State)
       * 3. Push Locals[Base+2] (Control)
       * 4. Fallthrough (followed by OP_CALL)
       */
      U8 base = READ_BYTE();

      /* Push Func, State, Control for iterator call */
      STACK_PUSH(LOCAL_GET(base));     /* Func */
      STACK_PUSH(LOCAL_GET(base + 1)); /* State */
      STACK_PUSH(LOCAL_GET(base + 2)); /* Control */

      /* Fallthrough to OP_CALL - iterator takes 2 args (state, ctrl) */
      break;
    }

    case OP_GLOOP_STEP: {
      /*
       * GLOOP_STEP: Generic for-loop step.
       * Format: [OP_GLOOP_STEP] [u8 Base_Index] (2 Bytes)
       * Stack: the iterator call's results (count = L->LastCallResults).
       * Locals: [Base]=Func, [Base+1]=State, [Base+2]=Control,
       *         [Base+3]=Body_Target, [Base+4]=VarCount (loop var count).
       *
       * 1. Normalize the results to exactly VarCount values (truncate or
       *    nil-pad). Only the iterator's own results are touched — never
       *    enclosing frames' stack (this loop may run inside nested calls).
       * 2. If the first result is nil: drop the results, fallthrough (exit).
       * 3. Else: Control = first result, jump to Body_Target (the body's
       *    SETLOCALs consume the results).
       */
      U8 base = READ_BYTE();
      I32 nvars = GetInt(LOCAL_GET(base + 4));
      Size nres = L->LastCallResults;
      MLuaValue firstResult;

      /* Normalize result count to the loop's variable count */
      while ((I32)nres > nvars && nres > 0) {
        L->EvalTop--;
        nres--;
      }
      while ((I32)nres < nvars) {
        STACK_PUSH(MLUA_NIL);
        nres++;
      }

      firstResult = L->EvalStack[L->EvalTop - (Size)nvars];

      if (IsNil(firstResult)) {
        /* End of iteration - drop exactly the iterator's results */
        L->EvalTop -= (Size)nvars;
        /* Fallthrough (loop ends) */
      } else {
        /* Continue: update control variable, jump to body */
        LOCAL_SET(base + 2, firstResult);
        pc = proto->Code + GetInt(LOCAL_GET(base + 3));
      }
      break;
    }

    case OP_CALL: {
      /*
       * Three-array architecture: Function call.
       * Stack has: [..., func, arg1, arg2, ...argN]
       * 1. Pop function and args from EvalStack
       * 2. Copy args to Args array
       * 3. Call function
       * 4. Result is pushed back to EvalStack
       */
      U8 nargs_call = READ_BYTE();
      int i;

      /* Get function from EvalStack (it's below the args) */
      Size funcPos = L->EvalTop - nargs_call - 1;
      MLuaValue func = L->EvalStack[funcPos];
      MLuaDebugPrint("OP_CALL nargs=%d func=%llx\n", nargs_call,
                     (unsigned long long)func);

      /* Copy args from EvalStack to Args array */
      for (i = 0; i < nargs_call && i < (int)L->ArgsSize; i++) {
        L->Args[i] = L->EvalStack[funcPos + 1 + i];
      }
      L->ArgsCount = nargs_call;

      /* Pop function and args from EvalStack (will push results later) */
      L->EvalTop = funcPos;

      if (IsLightFunc(func)) {
        /* Light C function */
        Size funcIdx = GetLightFuncIdx(func);
        MLuaDebugPrint("OP_CALL LightFunc idx=%zu\n", funcIdx);
        if (funcIdx < L->LightFuncCount && L->LightFuncs) {
          MLuaCFunction cfunc = (MLuaCFunction)L->LightFuncs[funcIdx];
          int results = cfunc(L);

          /* Check for error (negative return value) */
          if (results < 0) {
            /* C function signaled error - L->ErrorMsg should be set */
            VM_TRY(MLUA_ERRRUN);
          }

          /* C function pushes results to EvalStack via MLuaPush/MLuaPushResult
           */
          /* If no results, push nil */
          if (results == 0) {
            STACK_PUSH(MLUA_NIL);
          }
          L->LastCallResults = (results == 0) ? 1 : (Size)results;
        } else {
          /* Invalid function index */
          VM_FAIL(L, MLUA_ERR_RUNTIME, "attempt to call an invalid function");
        }
      } else if (IsPtr(func)) {
        /* Full closure */
        MLuaGCHeader *h = (MLuaGCHeader *)GetPtr(func);
        if (MLUA_OBJTYPE(h) == OBJTYPE_FUNCTION) {
          MLuaClosure *calledCl = MLUA_CLOSURE(h);
          if (calledCl->Proto) {
            /* For Lua closure, args are already in Args array;
             * MLuaExecute allocates the callee frame at LocalsTop and
             * copies them to Locals. */
            Size evalTopBeforeCall =
                L->EvalTop; /* Save to check for results after */
            MLuaStatus callStatus;

            /* Update our frame's PC for stacktrace (call site) */
            if (L->CallStackTop > 0) {
              L->CallStack[L->CallStackTop - 1].PC = (Size)(pc - proto->Code);
            }

            callStatus = MLuaExecute(L, calledCl, nargs_call, 1);

            /* Propagate error from callee (it already unwound itself;
             * unwind our own frame too) */
            if (callStatus != MLUA_OK) {
              VM_UNWIND(L);
              return callStatus;
            }

            /* Result should be on EvalStack now */
            /* If EvalTop hasn't increased, no result was pushed - push nil */
            if (L->EvalTop <= evalTopBeforeCall) {
              STACK_PUSH(MLUA_NIL);
            }
            L->LastCallResults = L->EvalTop - evalTopBeforeCall;
          } else {
            /* Not a Lua proto - invalid closure */
            VM_FAIL(L, MLUA_ERR_RUNTIME, "attempt to call an invalid closure");
          }
        } else {
          /* Pointer but not a function object */
          VM_FAIL(L, MLUA_ERR_RUNTIME, "attempt to call a non-function value");
        }
      } else {
        /* Not callable - error with type info */
        VM_FAIL(L, MLUA_ERR_RUNTIME,
                IsNil(func) ? "attempt to call a nil value"
                            : "attempt to call a non-function value");
      }
      break;
    }

    case OP_CLOSURE:
    case OP_CLOSURE_S: {
      /* Create closure from nested proto and capture its upvalues */
      Size protoIdx;
      MLuaProto *childProto;
      MLuaClosure *newCl;
      Size u;

      if (op == OP_CLOSURE) {
        protoIdx = READ_BYTE();
      } else {
        /* Stack variant: index was pushed by preceding instructions */
        MLuaValue idxVal = STACK_POP();
        protoIdx = (Size)GetInt(idxVal);
      }

      if (protoIdx >= proto->ProtosSize) {
        VM_FAIL(L, MLUA_ERR_RUNTIME, "invalid function prototype index");
      }

      childProto = proto->Protos[protoIdx];
      newCl = MLuaClosureNew(L, childProto, (U8)childProto->UpvaluesSize);
      if (!newCl) {
        VM_FAIL(L, MLUA_ERRMEM, "out of memory");
      }

      /* Root the closure on the EvalStack BEFORE capturing: creating an
       * upvalue allocates and may trigger a compacting collection. */
      STACK_PUSH(MakePtr(newCl));

      for (u = 0; u < childProto->UpvaluesSize; u++) {
        MLuaUpvalDesc d = childProto->Upvalues[u];
        MLuaUpvalue *uv;

        if (d.InStack) {
          /* Capture a local of THIS frame (open upvalue into Locals) */
          uv = MLuaFindUpvalue(L, &L->Locals[L->LocalsBase + d.Index]);
          if (!uv) {
            VM_FAIL(L, MLUA_ERRMEM, "out of memory");
          }
          /* Reload: the allocation above may have moved the closure */
          newCl = MLUA_CLOSURE((MLuaGCHeader *)GetPtr(EVAL_TOP()));
          childProto = newCl->Proto;
        } else {
          /* Share an upvalue of the currently-executing closure */
          if (!cl || d.Index >= cl->NumUpvalues) {
            VM_FAIL(L, MLUA_ERR_RUNTIME, "invalid upvalue reference");
          }
          uv = MLUA_CLOSURE_UPVALS(cl)[d.Index];
        }

        MLUA_CLOSURE_UPVALS(newCl)[u] = uv;
      }
      break;
    }

    case OP_GETUPVAL: {
      U8 idx = READ_BYTE();
      MLuaUpvalue *uv;
      if (!cl || idx >= cl->NumUpvalues ||
          !(uv = MLUA_CLOSURE_UPVALS(cl)[idx])) {
        VM_FAIL(L, MLUA_ERR_RUNTIME, "invalid upvalue");
      }
      STACK_PUSH(*uv->Location);
      break;
    }

    case OP_SETUPVAL: {
      U8 idx = READ_BYTE();
      MLuaValue val = STACK_POP();
      MLuaUpvalue *uv;
      if (!cl || idx >= cl->NumUpvalues ||
          !(uv = MLUA_CLOSURE_UPVALS(cl)[idx])) {
        VM_FAIL(L, MLUA_ERR_RUNTIME, "invalid upvalue");
      }
      *uv->Location = val;
      break;
    }

    case OP_CLOSE: {
      U8 base = READ_BYTE();
      if (L->OpenUpvalues) {
        MLuaCloseUpvalues(L, &L->Locals[L->LocalsBase + base]);
      }
      break;
    }

    case OP_VARARG: {
      /*
       * Three-array architecture: Push varargs from Args array.
       * Varargs are Args[NumParams .. ArgsCount-1]
       */
      U8 want = READ_BYTE();
      int varargCount = (int)L->ArgsCount - (int)proto->NumParams;
      Size varargStart = proto->NumParams;
      Size i;

      if (varargCount < 0) {
        varargCount = 0;
      }

      /* Push requested varargs (or nil if not enough) */
      for (i = 0; i < want; i++) {
        if ((int)i < varargCount) {
          STACK_PUSH(L->Args[varargStart + i]);
        } else {
          STACK_PUSH(MLUA_NIL);
        }
      }
      break;
    }

    case OP_RET: {
      U8 nret = READ_BYTE();
      /*
       * Three-array architecture:
       * Return values are already on EvalStack (top nret values).
       * The caller will handle cleanup and receive results.
       *
       * If nret is 0, push nil (Lua convention: function returns nil).
       */
      if (nret == 0) {
        STACK_PUSH(MLUA_NIL);
      }

      /* Close any upvalues still pointing into this frame (cheap NULL
       * check when nothing was captured) */
      if (L->OpenUpvalues) {
        MLuaCloseUpvalues(L, &L->Locals[L->LocalsBase]);
      }

      /* Pop frame: restore caller's locals window and call stack */
      L->LocalsBase = savedLocalsBase;
      L->LocalsTop = savedLocalsTop;
      L->CallStackTop = savedCallStackTop;

      return MLUA_OK;
    }

    default:
      VM_FAIL(L, MLUA_ERRRUN, "unknown opcode");
    }
  }
}

/* ==========================================================================
 */
/* High-Level API */
/* ==========================================================================
 */

MLuaStatus MLuaCall(MLuaState *L, int nargs, int nresults) {
  MLuaValue func;
  Size funcPos;

  UNUSED(nresults);

  /*
   * Three-array architecture: Function is at EvalTop - 1 - nargs.
   * Copy args to Args array, then call.
   */
  if (L->EvalTop < (Size)(nargs + 1)) {
    return MLUA_ERRRUN; /* Not enough values on stack */
  }

  funcPos = L->EvalTop - nargs - 1;
  func = L->EvalStack[funcPos];

  /* Copy args to Args array */
  {
    int i;
    for (i = 0; i < nargs && i < (int)L->ArgsSize; i++) {
      L->Args[i] = L->EvalStack[funcPos + 1 + i];
    }
    L->ArgsCount = nargs;
  }

  /* Pop func and args from EvalStack */
  L->EvalTop = funcPos;

  if (IsLightFunc(func)) {
    /* Light C function: mirror OP_CALL's light path */
    Size funcIdx = GetLightFuncIdx(func);
    if (funcIdx < L->LightFuncCount && L->LightFuncs) {
      MLuaCFunction cfunc = (MLuaCFunction)L->LightFuncs[funcIdx];
      int results = cfunc(L);
      if (results < 0) {
        return MLUA_ERRRUN; /* ErrorMsg set by the C function */
      }
      if (results == 0) {
        MLuaPush(L, MLUA_NIL);
      }
      return MLUA_OK;
    }
    L->ErrorMsg = "attempt to call an invalid function";
    return MLUA_ERRRUN;
  }

  if (IsPtr(func)) {
    MLuaGCHeader *h = (MLuaGCHeader *)GetPtr(func);
    if (MLUA_OBJTYPE(h) == OBJTYPE_FUNCTION) {
      MLuaClosure *cl = MLUA_CLOSURE(h);
      if (cl->Proto) {
        return MLuaExecute(L, cl, nargs, nresults);
      }
    }
  }

  L->ErrorMsg = IsNil(func) ? "attempt to call a nil value"
                            : "attempt to call a non-function value";
  return MLUA_ERRRUN;
}

MLuaStatus MLuaPCall(MLuaState *L, int nargs, int nresults, int errfunc) {
  /*
   * Protected call - saves EvalStack state and restores on error.
   * Note: Full implementation would use setjmp/longjmp, but we avoid
   * those for freestanding environments. Instead, we check return codes.
   */
  Size savedEvalTop = L->EvalTop;
  MLuaStatus status;

  UNUSED(errfunc);

  status = MLuaCall(L, nargs, nresults);

  if (status != MLUA_OK) {
    /* Restore EvalStack to saved position (minus func and args) */
    L->EvalTop = savedEvalTop - nargs - 1;
    /* Push error message if available */
    if (L->ErrorMsg) {
      STACK_PUSH(MLuaStringNew(L, L->ErrorMsg, StrLen(L->ErrorMsg)));
    } else {
      STACK_PUSH(MLuaStringNew(L, "error", 5));
    }
  }

  return status;
}

MLuaStatus MLuaDoString(MLuaState *L, const char *source, Size len,
                        const char *name) {
  MLuaProto *proto;
  MLuaClosure *cl;

  /* Parse and compile */
  proto = MLuaParse(L, source, len, name);
  if (!proto) {
    return MLUA_ERRRUN;
  }

  /* Create closure */
  cl = MLuaClosureNew(L, proto, 0);
  if (!cl) {
    return MLUA_ERRMEM;
  }

  /* Execute */
  return MLuaExecute(L, cl, 0, 0);
}

MLuaStatus MLuaLoadString(MLuaState *L, const char *source, Size len,
                          const char *name) {
  MLuaProto *proto;
  MLuaClosure *cl;

  /* Parse and compile */
  proto = MLuaParse(L, source, len, name);
  if (!proto) {
    MLuaPush(L, MLuaStringNew(L, "compile error", 13));
    return MLUA_ERRRUN;
  }

  /* Create closure */
  cl = MLuaClosureNew(L, proto, 0);
  if (!cl) {
    MLuaPush(L, MLuaStringNew(L, "memory error", 12));
    return MLUA_ERRMEM;
  }

  /* Push closure as a pointer value (will be called via CALL instruction) */
  MLuaPush(L, MakePtr(cl));

  return MLUA_OK;
}

/* ==========================================================================
 */
/* C Function Registration */
/* ==========================================================================
 */

MLuaValue MLuaRegisterCFunc(MLuaState *L, MLuaCFunction func) {
  Size idx;
  Size newCap;
  void **newFuncs;

  /* Check if we need to grow the light function array */
  if (L->LightFuncCount >= L->LightFuncCap) {
    newCap = L->LightFuncCap == 0 ? 16 : L->LightFuncCap * 2;
    newFuncs = (void **)MLuaAlloc(L, newCap * sizeof(void *));
    if (!newFuncs) {
      return MLUA_NIL;
    }
    /* Copy old array */
    if (L->LightFuncs && L->LightFuncCount > 0) {
      Size i;
      for (i = 0; i < L->LightFuncCount; i++) {
        newFuncs[i] = L->LightFuncs[i];
      }
    }
    L->LightFuncs = newFuncs;
    L->LightFuncCap = newCap;
  }

  /* Register the function */
  idx = L->LightFuncCount;
  L->LightFuncs[idx] = (void *)func;
  L->LightFuncCount++;

  return MakeLightFunc(idx);
}

void MLuaRegisterGlobal(MLuaState *L, const char *name, MLuaCFunction func) {
  MLuaValue fval = MLuaRegisterCFunc(L, func);
  if (!IsNil(fval)) {
    MLuaSetGlobal(L, name, fval);
  }
}

MLuaValue MLuaNewLib(MLuaState *L, const char *name) {
  MLuaValue lib = MLuaTableNewSized(L, 0, 8);
  if (IsNil(lib)) {
    return MLUA_NIL;
  }
  MLuaSetGlobal(L, name, lib);
  return lib;
}

void MLuaRegisterLib(MLuaState *L, MLuaValue lib, const MLuaLibEntry *entries) {
  while (entries->Name != NULL) {
    MLuaValue key = MLuaStringNew(L, entries->Name, StrLen(entries->Name));
    MLuaValue fval = MLuaRegisterCFunc(L, entries->Func);
    if (!IsNil(key) && !IsNil(fval)) {
      MLuaTableSet(L, lib, key, fval);
    }
    entries++;
  }
}

/* ==========================================================================
 */
/* I/O Configuration */
/* ==========================================================================
 */

void MLuaSetOutput(MLuaState *L, MLuaOutputFunc func) { L->OutputFunc = func; }

void MLuaSetRequirer(MLuaState *L, MLuaRequireFunc func) {
  L->RequireFunc = func;
}

/* print() global - only available if OutputFunc is set */
static int BasePrint(MLuaState *L) {
  int top = MLuaGetTop(L);
  int i;
  char buf[4096];
  Size bufpos = 0;

  MLuaDebugPrint("BasePrint called, top=%d, OutputFunc=%p\n", top,
                 (void *)L->OutputFunc);

  if (!L->OutputFunc) {
    return 0;
  }

  for (i = 1; i <= top; i++) {
    MLuaValue v = MLuaGetStack(L, i);

    /* Add tab separator BEFORE each value (except the first) */
    if (i > 1 && bufpos < sizeof(buf)) {
      buf[bufpos++] = '\t';
    }

    /* Use MLuaValueToStr for all type conversions */
    char tmp[256];
    Size len = MLuaValueToStr(L, v, tmp, sizeof(tmp));
    Size j;
    for (j = 0; j < len && bufpos < sizeof(buf) - 1; j++) {
      buf[bufpos++] = tmp[j];
    }
  }

  /* Add newline */
  if (bufpos < sizeof(buf)) {
    buf[bufpos++] = '\n';
  }

  L->OutputFunc(L, MLUA_OUTPUT_PRINT, buf, bufpos);
  return 0;
}

/* require() global - only available if RequireFunc is set */
static int BaseRequire(MLuaState *L) {
  MLuaValue modname = MLuaGetStack(L, 1);
  const char *name;
  MLuaValue result;

  MLuaDebugStack(L, "BaseRequire entry");

  if (!L->RequireFunc) {
    MLuaDebugPrint("BaseRequire: no RequireFunc\n");
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  name = MLuaStringData(modname);
  if (!name) {
    MLuaDebugPrint("BaseRequire: modname not string\n");
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  MLuaDebugPrint("BaseRequire: calling RequireFunc for '%s'\n", name);
  MLuaDebugStack(L, "before RequireFunc");

  result = L->RequireFunc(L, name);

  MLuaDebugPrint("BaseRequire: RequireFunc returned %llx\n",
                 (unsigned long long)result);
  MLuaDebugStack(L, "after RequireFunc");

  MLuaPush(L, result);
  MLuaDebugStack(L, "BaseRequire exit");
  return 1;
}

/* ==========================================================================
 */
/* Library Initialization */
/* ==========================================================================
 */

/* Forward declarations for library openers */
extern void MLuaOpenBase(MLuaState *L);
extern void MLuaOpenMath(MLuaState *L);
extern void MLuaOpenCoroutine(MLuaState *L);
extern void MLuaOpenString(MLuaState *L);
extern void MLuaOpenTable(MLuaState *L);

void MLuaOpenLibs(MLuaState *L) {
  /* Create globals table if not already created */
  if (IsNil(L->Globals)) {
    L->Globals = MLuaTableNewSized(L, 0, 32);
  }

  /* Open all standard libraries */
  MLuaOpenBase(L);
  MLuaOpenMath(L);
  MLuaOpenCoroutine(L);
  MLuaOpenString(L);
  MLuaOpenTable(L);

  /* Register print if output is available */
  if (L->OutputFunc) {
    MLuaRegisterGlobal(L, "print", BasePrint);
  }

  /* Register require if requirer is available */
  if (L->RequireFunc) {
    MLuaRegisterGlobal(L, "require", BaseRequire);
  }
}
